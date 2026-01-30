#ifndef MOVE_GENERATION_H
#define MOVE_GENERATION_H

#include "types.h"
#include "math.h"
#include "game.h"
#include "magic.h"
#include "utils.h"

static inline void push_move(Move move_list[200], Move move, int * move_index){

    move_list[(*move_index)++] = move;
    
}
/* @brief generates an attack mask from scratch for one side's pieces */


static inline uint64_t generate_attack_mask(Game * game, Side side, uint64_t * datk_mask){
    
    uint64_t attacked_squares = 0;
    uint64_t datk = 0;

    uint64_t pawns = game->pieces[side][PAWN];
    uint64_t ra = !side ? (pawns << 7 & ~file_masks[7]) : pawns >> 9 & ~file_masks[7];
    uint64_t la = !side ? (pawns << 9 & ~file_masks[0]) : pawns >> 7 & ~file_masks[0];
    datk |= la & ra;
    attacked_squares |= la | ra;
    uint64_t knights = game->pieces[side][KNIGHT];
    while (knights){
        int index = __builtin_ctzll(knights);
        knights = knights & (knights - 1);
        if (attacked_squares & knight_moves[index]){
            datk |= attacked_squares & knight_moves[index];
        }
        attacked_squares |= knight_moves[index];
    }

    uint64_t bishops = game->pieces[side][BISHOP];
    while (bishops){
        int index = __builtin_ctzll(bishops);
        bishops = bishops & (bishops - 1);
        uint64_t moves = fetch_bishop_moves(index, game->board_pieces[BOTH]);
        if (attacked_squares & moves){
            datk |= attacked_squares & moves;
        }
        attacked_squares |= moves;
    }
    uint64_t rooks = game->pieces[side][ROOK];
    while (rooks){
        int index = __builtin_ctzll(rooks);
        rooks = rooks & (rooks - 1);
        uint64_t moves = fetch_rook_moves(index, game->board_pieces[BOTH]);
        if (attacked_squares & moves){
            datk |= attacked_squares & moves;
        }
        attacked_squares |= moves;
    }
    uint64_t queens = game->pieces[side][QUEEN];
    while (queens){
        int index = __builtin_ctzll(queens);
        queens = queens & (queens - 1);
        uint64_t moves = fetch_queen_moves(index, game->board_pieces[BOTH]);
        if (attacked_squares & moves){
            datk |= attacked_squares & moves;
        }
        attacked_squares |= moves;
    }
    
    int kpos = __builtin_ctzll(game->pieces[side][KING]);
    uint64_t moves = king_moves[kpos];
    if (attacked_squares & moves){
        datk |= attacked_squares & moves;
    }
    attacked_squares |= king_moves[kpos];
    if (datk_mask){
        *datk_mask = datk;
    }
    
    return attacked_squares;
    
}
/* @brief calculated once a node to determine good quiets. may be used later to further improve move ordering in heavy tactical sequences */

static inline void generate_movement_masks(Game * game, Side side, MovementMasks * masks){
    
    uint64_t fdatk = 0, edatk = 0;
    masks->our_attack_mask = generate_attack_mask(game, side, &fdatk);
    masks->enemy_attack_mask = generate_attack_mask(game, (Side)!side, &edatk);

    masks->our_attacked_pieces = game->board_pieces[!side] & masks->our_attack_mask;
    masks->enemy_attacked_pieces = game->board_pieces[side] & masks->enemy_attack_mask;

    // dont include pawns
    masks->our_hanging_pieces = (game->board_pieces[side] & ~(masks->our_attack_mask & game->board_pieces[side]) & ~game->pieces[side][PAWN]);
    masks->enemy_hanging_pieces = (game->board_pieces[!side] & ~(masks->enemy_attack_mask & game->board_pieces[!side]) & ~game->pieces[!side][PAWN]);
    
}
/* @brief creates an attacker mask just for pieces that are looking at a certain square. takes in a special piece array due to see usage, we need a working set of piece boards while we loop through and pop them. */

static inline uint64_t attacker_mask_for_square(Game * game, Side side, uint64_t pieces[PIECE_TYPES], int index, uint64_t occupancy){

    uint64_t attackers = 0;
    uint64_t bishop_rays = fetch_bishop_moves(index, occupancy);
    
    attackers |= bishop_rays & pieces[BISHOP];
    uint64_t rook_rays = fetch_rook_moves(index, occupancy);
    
    attackers |= rook_rays & pieces[ROOK];
    
    attackers |= (bishop_rays | rook_rays) & pieces[QUEEN];

    attackers |= (knight_moves[index] & pieces[KNIGHT]);
    attackers |= pawn_captures[!side][index] & pieces[PAWN];
    attackers |= king_moves[index] & pieces[KING];
    
    return attackers;

}

static inline int lva_from_attacker_mask(uint64_t pieces[PIECE_TYPES], uint64_t mask, PieceType * p){
    
    for (int i = 0; i < PIECE_TYPES; i++){
        uint64_t overlap = mask & pieces[i];
        if (overlap){
            *p = (PieceType)i;
            return __builtin_ctzll(overlap);
        }
    }
    return -1;
}


static inline int mva_from_attacker_mask(Game * game, uint64_t pieces[PIECE_TYPES], uint64_t mask, Side side, PieceType * p){
    for (int i = PIECE_TYPES - 1; i >= 0; i++){
        uint64_t overlap = mask & pieces[i];
        if (overlap){
            *p = (PieceType)i;
            return bit_scan_forward(&overlap);
        }
    }
    return -1;
}



static inline void push_promo(Move move_list[256], uint8_t * move_count, uint8_t start, uint8_t end){
    
    int mc = *move_count;
    move_list[mc++] = create_promotion(start, end, KNIGHT);
    move_list[mc++] = create_promotion(start, end, BISHOP);
    move_list[mc++] = create_promotion(start, end, ROOK);
    move_list[mc++] = create_promotion(start, end, QUEEN);
    *move_count = mc;
}


static inline void generate_pawn_moves_new(Game * game, Move move_list[256], uint8_t * move_count, Side side, uint64_t potential_captures, uint64_t own_pieces, bool only_non_quiet, bool generate_checks){
    
    uint64_t pawns = game->pieces[side][PAWN];
    uint64_t blockers = game->board_pieces[BOTH];

    uint64_t pushes = !side ? (pawns << 8 & ~blockers) : pawns >> 8 & ~blockers;
    uint64_t double_pushes = !side ? (pushes << 8 & double_push_masks[side] & ~blockers) : (pushes >> 8 & double_push_masks[side] & ~blockers);
    uint64_t ra = !side ? (pawns << 7 & ~file_masks[7]) : pawns >> 9 & ~file_masks[7];
    uint64_t la = !side ? (pawns << 9 & ~file_masks[0]) : pawns >> 7 & ~file_masks[0];
    uint64_t cap_la = la & potential_captures;
    uint64_t cap_ra = ra & potential_captures;
    
    uint64_t promo = pushes & promotion_ranks[side];
    uint64_t promo_la = cap_la & promotion_ranks[side];
    uint64_t promo_ra = cap_ra & promotion_ranks[side];
    pushes &= ~promo;
    cap_la &= ~promo_la;
    cap_ra &= ~promo_ra;

    uint8_t mc = *move_count;
    while (promo){
        int move_index = __builtin_ctzll(promo);
        int start = move_index - push_direction[side] * 8;
        promo = promo & (promo- 1);
        push_promo(move_list, &mc, start, move_index);
    }
    while (promo_la){
        int move_index = __builtin_ctzll(promo_la);
        int start = move_index - push_direction[side] * 8 - 1;
        promo_la = promo_la & (promo_la- 1);
        push_promo(move_list, &mc, start, move_index);
    }
    while (promo_ra){
        int move_index = __builtin_ctzll(promo_ra);
        int start = move_index - push_direction[side] * 8 + 1;
        promo_ra = promo_ra & (promo_ra - 1);
        push_promo(move_list, &mc, start, move_index);
    }
    while (cap_la){
        uint8_t to = __builtin_ctzll(cap_la);
        uint8_t start = to - push_direction[side] * 8 - 1;
        cap_la = cap_la & (cap_la - 1);
        move_list[mc++] = create_move(start, to, NORMAL);
    }
    while (cap_ra){
        uint8_t to = __builtin_ctzll(cap_ra);
        uint8_t start = to - push_direction[side] * 8 + 1;
        cap_ra = cap_ra & (cap_ra - 1);
        move_list[mc++] = create_move(start, to, NORMAL);
    }
    if (game->st->en_passant_index != -1){
        uint64_t ep = bits[game->st->en_passant_index];
        if (ep & la){
            uint8_t to = game->st->en_passant_index;
            uint8_t from = to - push_direction[side] * 8 - 1;
            move_list[mc++] = create_move(from, to, ENPASSANT);
        }
        if (ep & ra){
            uint8_t to = game->st->en_passant_index;
            uint8_t from = to - push_direction[side] * 8 + 1;
            move_list[mc++] = create_move(from, to, ENPASSANT);
        }
    }
    if (only_non_quiet && !generate_checks) {
        *move_count = mc;    
        return;
    }
    while (pushes){
        uint8_t to = __builtin_ctzll(pushes);
        uint8_t from = to - push_direction[side] * 8;
        pushes = pushes & (pushes - 1);
    
        move_list[mc++] = create_move(from, to, NORMAL);

    }
    while (double_pushes){
        
        uint8_t to = __builtin_ctzll(double_pushes);
        uint8_t from = to - push_direction[side] * 16;
        double_pushes = double_pushes & (double_pushes - 1);
    
        move_list[mc++] = create_move(from, to, NORMAL);
    }
    *move_count = mc;    
}



static inline void generate_bishop_moves(Game * game, Move move_list[256], uint8_t * move_count, Side side, uint64_t potential_captures, uint64_t own_pieces, uint64_t blockers, bool only_non_quiet, bool generate_checks){

    uint64_t bishops = game->pieces[side][BISHOP];
    uint8_t mc = *move_count;
    while (bishops){
        uint8_t from = __builtin_ctzll(bishops);
        bishops= bishops& (bishops- 1);
        uint64_t moves = fetch_bishop_moves(from, blockers);
        uint64_t captures = moves & potential_captures;

        while (captures){
            uint8_t to = __builtin_ctzll(captures);
            captures = captures & (captures - 1);
            move_list[mc++] = create_move(from, to, NORMAL);
        }
        if (only_non_quiet && !generate_checks) continue;

        moves &= ~game->board_pieces[BOTH];
        while (moves){
            uint8_t to = __builtin_ctzll(moves);
            moves = moves & (moves - 1);

            move_list[mc++] = create_move(from, to, NORMAL);
        }
            
    }
    *move_count = mc;
}
static inline void generate_rook_moves(Game * game, Move move_list[256], uint8_t * move_count, Side side, uint64_t potential_captures, uint64_t own_pieces, uint64_t blockers, bool only_non_quiet, bool generate_checks){

    CastleSide rights_toggle = NONESIDE;
    bool loses_rights = false;
    uint64_t rooks = game->pieces[side][ROOK];
    uint8_t mc = *move_count;
    while (rooks){
        uint8_t from = __builtin_ctzll(rooks);
        rooks = rooks & (rooks - 1);
        uint64_t moves = fetch_rook_moves(from, blockers);
        uint64_t captures = moves & potential_captures;
        while (captures){
            uint8_t to = __builtin_ctzll(captures);
            captures = captures & (captures - 1);

            move_list[mc++] = create_move(from, to, NORMAL);

        }
        if (only_non_quiet && !generate_checks) continue;
        moves &= ~game->board_pieces[BOTH];


        while (moves){
            uint8_t to = __builtin_ctzll(moves);
            moves = moves & (moves - 1);

            move_list[mc++] = create_move(from, to, NORMAL);
        }
        
    }
    *move_count = mc;
}

static inline void generate_queen_moves(Game * game, Move move_list[256], uint8_t * move_count, Side side, uint64_t potential_captures, uint64_t own_pieces, uint64_t blockers, bool only_non_quiet, bool generate_checks){

    uint64_t queens = game->pieces[side][QUEEN];
    uint8_t mc = *move_count;
    while (queens){
        uint8_t from = __builtin_ctzll(queens);
        queens = queens & (queens - 1);
        uint64_t moves = fetch_queen_moves(from, blockers);
        uint64_t captures = moves & potential_captures;
        while (captures){
        
            int to = __builtin_ctzll(captures);
            captures = captures & (captures - 1);
            move_list[mc++] = create_move(from, to, NORMAL);
        }
        if (only_non_quiet && !generate_checks) continue;
        moves &= ~game->board_pieces[BOTH];


        while (moves){
            uint8_t to = __builtin_ctzll(moves);
            moves = moves & (moves - 1);
            move_list[mc++] = create_move(from, to, NORMAL);
        }
            
    }
    *move_count = mc;
}

static inline void generate_king_moves(Game * game, Move move_list[256], uint8_t * move_count, Side side, uint64_t potential_captures, uint64_t own_pieces, bool only_non_quiet){


    uint64_t kings = game->pieces[side][KING];

    uint8_t from = __builtin_ctzll(kings);

    uint64_t moves = king_moves[from];
    moves &= ~own_pieces;
    uint64_t captures = moves & potential_captures;
    moves &= ~captures;
    uint8_t mc = *move_count;

    while (captures){
        uint8_t to = __builtin_ctzll(captures);
        captures = captures & (captures - 1);
        move_list[mc++] = create_move(from, to, NORMAL);
    }
    if (only_non_quiet){
        *move_count = mc; return;
    }
    while (moves){

        uint8_t to = __builtin_ctzll(moves);
        moves = moves & (moves - 1);

        move_list[mc++] = create_move(from, to, NORMAL);
    }
    *move_count = mc;
    
}

static inline void generate_knight_moves(Game * game, Move move_list[256], uint8_t * move_count, Side side, uint64_t potential_captures, uint64_t own_pieces, bool only_non_quiet, bool generate_checks){
    
    uint64_t knights = game->pieces[side][KNIGHT];
    int mc = *move_count;
    while (knights){

        uint8_t from = __builtin_ctzll(knights);
        knights = knights & (knights - 1);
        
        uint64_t moves = knight_moves[from];
        moves &= ~own_pieces;
        uint64_t captures = moves & potential_captures;
        moves &= ~captures;

        while (captures){
        
            uint8_t to = __builtin_ctzll(captures);
            captures = captures & (captures - 1);
            move_list[mc++] = create_move(from, to, NORMAL);
        }
        if (only_non_quiet && !generate_checks) continue;
        while (moves){
    
            uint8_t to = __builtin_ctzll(moves);
            moves = moves & (moves - 1);
                
            move_list[mc++] = create_move(from, to, NORMAL);
        }

    }
    *move_count = mc;
    
}

/* @brief found on chess programming wiki */

static inline int manhattanDistance(int sq1, int sq2) {
   int file1, file2, rank1, rank2;
   int rankDistance, fileDistance;
   file1 = sq1  & 7;
   file2 = sq2  & 7;
   rank1 = sq1 >> 3;
   rank2 = sq2 >> 3;
   rankDistance = abs (rank2 - rank1);
   fileDistance = abs (file2 - file1);
   return rankDistance + fileDistance;
}



// these functions just generate const bitboard masks for moves.

void generate_pawn_boards(Game * game);
void generate_knight_boards(Game * game);
void generate_king_boards(Game * game);




void init_piece_boards(Game * game);
void update_blocker_masks(Game * game);

static inline void generate_non_quiet_moves(Game * game, Side side, Move move_list[256], uint8_t * move_count, bool generate_checks){
    uint64_t blockers = game->board_pieces[BOTH];

    uint64_t potential_captures = 0;
    uint64_t own_pieces = 0;
    uint64_t attack_mask = 0;
    Side other_side = (Side)!side;
    
    potential_captures = game->board_pieces[other_side];
    own_pieces = game->board_pieces[side];
    
    generate_pawn_moves_new(game, move_list, move_count, side, potential_captures, own_pieces, true, generate_checks);
    generate_knight_moves(game, move_list, move_count, side, potential_captures, own_pieces, true, generate_checks);
    generate_bishop_moves(game, move_list, move_count, side, potential_captures, own_pieces, blockers, true, generate_checks);
    generate_rook_moves(game, move_list, move_count, side, potential_captures, own_pieces, blockers, true, generate_checks);
    generate_queen_moves(game, move_list, move_count, side, potential_captures, own_pieces, blockers, true, generate_checks);
    generate_king_moves(game, move_list, move_count, side, potential_captures, own_pieces, true);
}

static inline void generate_moves(Game * game, Side side, Move move_list[256], uint8_t * move_count){

    uint64_t blockers = game->board_pieces[BOTH];

    uint64_t potential_captures = 0;
    uint64_t own_pieces = 0;
    Side other_side = (Side)!side;
    potential_captures = game->board_pieces[other_side];
    own_pieces = game->board_pieces[side];

    uint64_t e_am = 0, e_am_datk = 0;
    if (game->st->castle_flags & (side ? CWQ : CBQ)){
        
        if (!(game->board_pieces[BOTH] & castle_occupation_masks[side][QUEENSIDE])){

            e_am = generate_attack_mask(game, (Side)!side, &e_am_datk);
            
            if ((e_am & castle_masks[side][QUEENSIDE]) == 0){
                move_list[(*move_count)++] = create_move(king_castle_locations[side][QUEENSIDE][START], king_castle_locations[side][QUEENSIDE][END], CASTLE);
            }
        }
    }

    if (game->st->castle_flags & (side ? CWK : CBK)){
        
        if (!(game->board_pieces[BOTH] & castle_occupation_masks[side][KINGSIDE])){

            if (!e_am){
                e_am = generate_attack_mask(game, (Side)!side, &e_am_datk);
            }
            
            if ((e_am & castle_masks[side][KINGSIDE]) == 0){
                move_list[(*move_count)++] = create_move(king_castle_locations[side][KINGSIDE][START], king_castle_locations[side][KINGSIDE][END], CASTLE);
            }
        }
    } 
    
    
    generate_pawn_moves_new(game, move_list, move_count, side, potential_captures, own_pieces, false, true);
    generate_knight_moves(game, move_list, move_count, side, potential_captures, own_pieces, false, true);
    generate_bishop_moves(game, move_list, move_count, side, potential_captures, own_pieces, blockers, false, true);
    generate_rook_moves(game, move_list, move_count, side, potential_captures, own_pieces, blockers, false, true);
    generate_queen_moves(game, move_list, move_count, side, potential_captures, own_pieces, blockers, false, true);
    generate_king_moves(game, move_list, move_count, side, potential_captures, own_pieces, false);

    

}

#endif
