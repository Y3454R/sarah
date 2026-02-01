#ifndef GAME_H
#define GAME_H

// #include "move_generation.h"
// #include "move_generation.h"
#include "game_old.h"
#include "magic.h"
#include "types.h"
#include "math.h"
#include "utils.h"
#include "zobrist.h"
#include "magic.h"
// #include "tuner.h"


// must be called before making a move
static inline bool is_double_push(Game * game, Move m){
    uint8_t from = move_from(m);
    if (move_type(m) != NORMAL || game->piece_at[from] != PAWN) return false;
    uint8_t to = move_to(m);
    if (abs(from - to) == 16) return true;
    return false;
}

static inline uint64_t moves_at(Side side, uint8_t sq, PieceType p, uint64_t occ){
    
    switch (p){
        case PAWN:
            return pawn_captures[side][sq];
            break;
        case KNIGHT:
            return knight_moves[sq];
            break;
        case BISHOP:
            return fetch_bishop_moves(sq, occ);
            break;
        case ROOK:
            return fetch_rook_moves(sq, occ);
            break;
        case QUEEN:
            return fetch_queen_moves(sq, occ);
            break;
        case KING:
            return king_moves[sq];
            break;
        default:
            return 0;
    }
}


static inline bool move_causes_check(Game * game, Move m){
    PieceType p = move_piece(game, m);

    MoveType mt = move_type(m);
    uint8_t from = move_from(m), to = move_to(m);
    if (bits[to] & game->st->ci.check_sq[p] && mt != PROMOTION) {
        return true;
    }

    if ((game->st->ci.blockers_for_king[!game->side_to_move] & bits[from])
        && !is_aligned(from, to, game->st->k_sq[!game->side_to_move])) {
        return true;
    }
    switch (mt){
        case NORMAL: break;
        case ENPASSANT:
            {
                uint64_t occ = game->board_pieces[BOTH];
                occ ^= from;
                occ |= to;
                occ ^= pawn_moves[!game->side_to_move][to];
                return (fetch_rook_moves(game->st->k_sq[!game->side_to_move], occ) & (game->pieces[game->side_to_move][ROOK] | game->pieces[game->side_to_move][QUEEN])) |
                (fetch_bishop_moves(game->st->k_sq[!game->side_to_move], occ) & (game->pieces[game->side_to_move][BISHOP] | game->pieces[game->side_to_move][QUEEN]));
            }
        case CASTLE:
            {
                CastleSide cs = (king_end_locations[game->side_to_move][KINGSIDE] == to ? KINGSIDE : QUEENSIDE);
                uint8_t rfrom = rook_castle_locations[game->side_to_move][cs][START], rto = rook_castle_locations[game->side_to_move][cs][END];
                return (rook_moves[rto] & game->pieces[!game->side_to_move][KING]) && (fetch_rook_moves(rto, game->board_pieces[BOTH] ^ rfrom ^ from | to | rto) & game->pieces[!game->side_to_move][KING]);
            }
            break;
        case PROMOTION:
            return moves_at(game->side_to_move, to, move_promotion_type(m), game->board_pieces[BOTH] ^ from) & game->pieces[!game->side_to_move][KING];
        default: break;
    }



    return false;
}

static inline uint64_t pawn_attacks(Side side, uint64_t bb){
    
    uint64_t ra = !side ? (bb << 7 & ~file_masks[7]) : bb >> 9 & ~file_masks[7];
    uint64_t la = !side ? (bb << 9 & ~file_masks[0]) : bb >> 7 & ~file_masks[0];
    return ra | la;
}

static inline uint64_t pawn_pushes(Side side, uint64_t bb, uint64_t occ){
    
    uint64_t pushes = !side ? (bb << 8 & ~occ) : bb >> 8 & ~occ;
    uint64_t double_pushes = !side ? (pushes << 8 & double_push_masks[side] &~occ) : (pushes >> 8 & double_push_masks[side] & ~occ);
    return pushes | double_pushes;
}

// we take in a side to determine which enemy sliders for this function, since we don't have an efficient way to infer both color pieces without having extra instructions
static inline uint64_t slider_blockers(Game * game, Side side, uint64_t sliders, uint8_t sq, uint64_t * pinners){

    uint64_t blockers = 0;
    *pinners = 0;

    
    uint64_t p_sliders =
        rook_moves[sq] & (game->pieces[side][QUEEN] | game->pieces[side][ROOK]) |
        bishop_moves[sq] & (game->pieces[side][QUEEN] | game->pieces[side][BISHOP]) & sliders;
    
    uint64_t occ = game->board_pieces[BOTH] ^ p_sliders;
    while (p_sliders){
        int pos = __builtin_ctzll(p_sliders);
        p_sliders = p_sliders & (p_sliders - 1);
        uint64_t b = between_sq[sq][pos] & occ;
        if (b && !more_than_one(b)){
            blockers |= b;
            if (b & game->board_pieces[!side]){
                *pinners |= bits[pos];
            }
        }
        
    }

    return blockers;
}

static inline void compute_check_info(Game * game, StateInfo * st){
    
    assert(st == game->st);
    st->ci.blockers_for_king[WHITE] = slider_blockers(game, BLACK, game->board_pieces[BLACK], st->k_sq[WHITE], &st->pinners[BLACK]);
    st->ci.blockers_for_king[BLACK] = slider_blockers(game, WHITE, game->board_pieces[WHITE], st->k_sq[BLACK], &st->pinners[WHITE]);
    
    uint8_t ksq = st->k_sq[!game->side_to_move];
    st->ci.check_sq[PAWN] = pawn_captures[!game->side_to_move][ksq];
    st->ci.check_sq[KNIGHT] = knight_moves[ksq];
    st->ci.check_sq[BISHOP] = fetch_bishop_moves(ksq, game->board_pieces[BOTH]);
    st->ci.check_sq[ROOK] = fetch_rook_moves(ksq, game->board_pieces[BOTH]);
    st->ci.check_sq[QUEEN] = st->ci.check_sq[BISHOP] | st->ci.check_sq[ROOK];
    
    
}


/*
    @brief used to mainly detect checking moves. ordered in roughly the order I would assume to be most efficient 
    @param side the ATTACKING side
    @param index the square we are checking attacks to
*/

static inline bool is_square_attacked(Game * game, Side side, int index){
    
    if (pawn_captures[!side][index] & game->pieces[side][PAWN]) {
        return true;
    }
    if (knight_moves[index] & game->pieces[side][KNIGHT]) {
        return true;
    }
    uint64_t bishop_rays = fetch_bishop_moves(index, game->board_pieces[BOTH]);
    
    if (bishop_rays & game->pieces[side][BISHOP]) {
        return true;
    }
    
    uint64_t rook_rays = fetch_rook_moves(index, game->board_pieces[BOTH]);
    
    if (rook_rays & game->pieces[side][ROOK]) {
        return true;
    }
    
    if ((bishop_rays | rook_rays) & game->pieces[side][QUEEN]) {
        return true; 
    }


    if (king_moves[index] & game->pieces[side][KING]) {
        return true;
    }
    

    return false;
    
}

/* @brief determines if a side is in check. used during make / undo move to check for legality and int main search to determine reductions */

static inline bool in_check(Game * game, Side side){
    
    return is_square_attacked(game, (Side)!side, __builtin_ctzll(game->pieces[side][KING]));

}



// this assumes that the uid is at the target position for the correct piece. during make move this must occur before this is called
static inline void put_piece(Game * game, StateInfo * st, Side side, PieceType start_p, PieceType p, uint8_t id, int start, int pos, uint64_t blockers, Undo * undo){

    game->st->key ^= get_piece_random(start_p, side, start);
    game->st->key ^= get_piece_random(p, side, pos);
    game->st->piece_key[start_p] ^= piece_random[side][start];
    game->st->piece_key[p] ^= piece_random[side][pos];
    if (p == KING){
        assert(start == st->k_sq[side]);
        game->st->k_sq[side] = pos;
    }
    if (start_p != PAWN){
        game->st->nonpawn_key[side] ^= piece_random[side][start];
    }
    if (p != PAWN){
        game->st->nonpawn_key[side] ^= piece_random[side][pos];
    }
    
    game->sq_to_uid[pos] = id;
    game->piece_info[id].alive = true;
    game->piece_info[id].p = p;
    game->piece_info[id].pos = pos;
    
}

static inline void remove_victim(Game * game, StateInfo * st, Side side, PieceType p, int pos, Undo * undo){

    game->st->key ^= get_piece_random(p, side, pos);
    game->st->piece_key[p] ^= piece_random[side][pos];

    uint8_t id = game->sq_to_uid[pos];
    if (p != PAWN){
        game->st->nonpawn_key[side] ^= piece_random[side][pos];
    }
    game->piece_attacks[id] = 0;
    game->sq_to_uid[pos] = 0;
    game->piece_info[id].alive = false;
}


static inline void recalculate_sliders_at_sq(Game * game, uint64_t blockers, int index, Undo * undo){


    // recompute sliding attacks that intersect the square
    uint64_t b = fetch_bishop_moves(index, blockers);
    uint64_t r = fetch_rook_moves(index, blockers);
    uint64_t eb = (game->pieces[WHITE][BISHOP] | game->pieces[BLACK][BISHOP]) & b & ~undo->attack_undo_mask;
    uint64_t er = (game->pieces[WHITE][ROOK] | game->pieces[BLACK][ROOK]) & r & ~undo->attack_undo_mask;
    uint64_t q = game->pieces[WHITE][QUEEN] | game->pieces[BLACK][QUEEN];
    uint64_t eq = ((q & (r | b))) & ~undo->attack_undo_mask;
    
    undo->attack_undo_mask |= eb | er | eq;
    while (eb){
        int pos = __builtin_ctzll(eb);
        eb = eb & (eb - 1);
        uint8_t id = game->sq_to_uid[pos];
        uint64_t a = fetch_bishop_moves(pos, blockers);
        undo->attack_undos[undo->attack_undo_count++] = (AttackUndo){.id = id, .attacks = game->piece_attacks[id]};
        Side side = game->piece_info[id].side;
        if (game->piece_attacks[id] & king_zone_masks[undo->k_sq[!side]]){
            game->atk_kz[side] -= KING_THREAT_MG[BISHOP];
        }
        if (a & king_zone_masks[game->k_sq[!side]]){
            game->atk_kz[side] += KING_THREAT_MG[BISHOP];
        }
        game->piece_attacks[id] = a;

    }
    while (er){
        int pos = __builtin_ctzll(er);
        er = er & (er - 1);
        uint8_t id = game->sq_to_uid[pos];
        uint64_t a = fetch_rook_moves(pos, blockers);
        undo->attack_undos[undo->attack_undo_count++] = (AttackUndo){.id = id, .attacks = game->piece_attacks[id]};
        Side side = game->piece_info[id].side;
        if (game->piece_attacks[id] & king_zone_masks[undo->k_sq[!side]]){
            game->atk_kz[side] -= KING_THREAT_MG[ROOK];
        }
        if (a & king_zone_masks[game->k_sq[!side]]){
            game->atk_kz[side] += KING_THREAT_MG[ROOK];
        }
        game->piece_attacks[id] = a;
        
    }
    while (eq){
        int pos = __builtin_ctzll(eq);
        eq = eq & (eq - 1);
        uint8_t id = game->sq_to_uid[pos];
        uint64_t a = fetch_queen_moves(pos, blockers);
        undo->attack_undos[undo->attack_undo_count++] = (AttackUndo){.id = id, .attacks = game->piece_attacks[id]};
        Side side = game->piece_info[id].side;
        if (game->piece_attacks[id] & king_zone_masks[undo->k_sq[!side]]){
            game->atk_kz[side] -= KING_THREAT_MG[QUEEN];
        }
        if (a & king_zone_masks[game->k_sq[!side]]){
            game->atk_kz[side] += KING_THREAT_MG[QUEEN];
        }
        game->piece_attacks[id] = a;
    }
}

static inline void restore_attack_from_undo(Game * game, AttackUndo * undo){
    game->piece_attacks[undo->id] = undo->attacks;
}


/* @brief handles polyglot hash key / tt updates, bitboard, and "piece_at[][]" changes. also stores history, and replaces last_move which is used for refutations near the end of the function. */

// void undo_move(Game * game, Move * move);

static inline void undo_move(Game * game, Move m, SearchStack * stack, Undo * undo){

        // Side side = (Side)!game->side_to_move;
    // Side enemy_side = (Side)!side;
    Side enemy_side = (Side)game->side_to_move;
    Side side = (Side)!enemy_side;
    uint8_t from = move_from(m);
    uint8_t to = move_to(m);
    MoveType mt = move_type(m);
    // assert(game->piece_at[from] == PIECE_NONE);
    PieceType p = undo->p;
    // assert(p == game->piece_at[to] || mt == PROMOTION);

    PieceType cp = undo->cp;
    uint64_t * piece_board = &game->pieces[side][p];
    uint64_t * side_to_move_pieces = &game->board_pieces[side];
    uint64_t * other_side_pieces = &game->board_pieces[!side];
    uint64_t our_pieces = *side_to_move_pieces;
    uint64_t their_pieces = *other_side_pieces;

    uint8_t id = undo->p_id;
    uint8_t cap_id = undo->cap_id;
    bool cap = undo->was_capture;


    assert((game->board_pieces[WHITE] & game->board_pieces[BLACK]) == 0);

    switch(mt){
        case NORMAL:
            {

                uint64_t move_board = (bits[from] | bits[to]);
                // move piece on its board
                *piece_board ^= move_board;

                // move piece on its color's board
                *side_to_move_pieces ^= move_board;
                // assert(game->piece_at[from] == PIECE_NONE);

                game->piece_at[from] = p;

                game->sq_to_uid[from] = id;
                game->piece_info[id].pos = from;
                if (cap){
                    
                    // assert(cp < PIECE_NONE);
                    game->pieces[!side][cp] |= bits[to]; 
                    *other_side_pieces |= bits[to];
                    game->phase += phase_values[cp];
                    game->piece_at[to] = cp;
                    game->sq_to_uid[to] = cap_id;
                    game->piece_info[cap_id].pos = to;
                    game->piece_info[cap_id].alive = true;
                } else {
                    
                    game->piece_at[to] = PIECE_NONE;
                }
                
                break;

            }
        case ENPASSANT:
            {
                // TODO optimize this by precomputing these bits and literally looking them up in an array, this is unnecessary
                uint8_t capture_square = __builtin_ctzll(pawn_moves[!side][to]);
                uint64_t move_board = (bits[from] | bits[to]);
                uint64_t cap_sq = bits[capture_square];
                
                *piece_board ^= move_board;

                game->pieces[!side][cp] |= cap_sq;

                *side_to_move_pieces ^= move_board;

                *other_side_pieces |= cap_sq;
                
                game->phase += phase_values[cp];
                game->piece_at[from] = p;
                game->piece_at[to] = PIECE_NONE;
                game->piece_at[capture_square] = cp;

                    
                game->sq_to_uid[from] = id;
                game->piece_info[id].pos = from;

                game->sq_to_uid[capture_square] = cap_id;
                game->piece_info[cap_id].pos = capture_square;
                game->piece_info[cap_id].alive = true;
            }
            break;
        case CASTLE:
            {

                CastleSide cs = (king_end_locations[side][KINGSIDE] == to ? KINGSIDE : QUEENSIDE);
                uint64_t move_board = (bits[from] | bits[to]);
                uint8_t rfrom = rook_castle_locations[side][cs][START], rend = rook_castle_locations[side][cs][END];
                uint64_t cb = 
                    bits[rfrom] | bits[rend];
                    
                *piece_board ^= move_board;

                game->pieces[side][ROOK] ^= cb;

                *side_to_move_pieces ^= cb | move_board;
                

                game->piece_at[from] = p;
                game->piece_at[to] = PIECE_NONE;
                game->piece_at[rfrom] = ROOK;
                game->piece_at[rend] = PIECE_NONE;

                game->sq_to_uid[from] = id;
                game->piece_info[id].pos = from;

                game->sq_to_uid[rfrom] = cap_id;
                game->piece_info[cap_id].pos = rfrom;
            }
            break;
        case PROMOTION:
            {

                uint64_t s = bits[from];
                uint64_t e = bits[to];
                PieceType promo = move_promotion_type(m);
                // assert(p == promo);
                // assert(promo < PIECE_NONE);
                
                game->pieces[side][PAWN] |= s;

                game->pieces[side][promo] ^= e;

                *side_to_move_pieces ^= (s | e);

                if (cap){
                   
                    // assert(cp < PIECE_NONE);
                    game->pieces[!side][cp] |= e;
                    
                    *other_side_pieces |= e;
                

                    game->phase += phase_values[cp];
                    game->piece_at[to] = cp;

                    game->sq_to_uid[to] = cap_id;
                    game->piece_info[cap_id].pos = to;
                    game->piece_info[cap_id].alive = true;
                    
                } else {
                    game->piece_at[to] = PIECE_NONE;
                }


                game->phase -= phase_values[promo];
                game->piece_at[from] = PAWN;

                game->sq_to_uid[from] = id;
                game->piece_info[id].pos = from;
                game->piece_info[id].p = PAWN;

            }
            break;
    }


    
    // game->en_passant_index = undo->ep_sq;
    // game->castle_flags[side][QUEENSIDE] = undo->castle_flags[side][QUEENSIDE];    
    // game->castle_flags[side][KINGSIDE] = undo->castle_flags[side][KINGSIDE];    
    // game->castle_flags[!side][QUEENSIDE] = undo->castle_flags[!side][QUEENSIDE];    
    // game->castle_flags[!side][KINGSIDE] = undo->castle_flags[!side][KINGSIDE];    
    
    // game->psqt_evaluation_mg[BLACK] = undo->psqt_eval_mg[BLACK];
    // game->psqt_evaluation_mg[WHITE] = undo->psqt_eval_mg[WHITE];
    // game->psqt_evaluation_eg[BLACK] = undo->psqt_eval_eg[BLACK];
    // game->psqt_evaluation_eg[WHITE] = undo->psqt_eval_eg[WHITE];
    // game->k_sq[WHITE] = undo->k_sq[WHITE];
    // game->k_sq[BLACK] = undo->k_sq[BLACK];


    

    stack->current_move = 0;
    stack->moved_piece = PIECE_NONE;
    stack->cap_piece = PIECE_NONE;
    game->board_pieces[BOTH] = game->board_pieces[WHITE] | game->board_pieces[BLACK];
    game->side_to_move = (Side)!game->side_to_move;
    game->key_history[game->history_count] = 0;
    game->history_count -= 1;


    // if (move->piece == PAWN || move->type == CAPTURE){
    game->fifty_move_clock = game->fifty_move_clock_was;
    // } else {
    //     game->fifty_move_clock -= 1;
    // }
    // this move is the move we just played and are undoing.
    // this data is needed for refutation.
    // game->nonpawn_key[BLACK] = undo->nonpawn_key[BLACK];
    // game->nonpawn_key[WHITE] = undo->nonpawn_key[WHITE];
    // game->piece_key[PAWN] = undo->piece_key[PAWN];
    // game->piece_key[KNIGHT] = undo->piece_key[KNIGHT];
    // game->piece_key[BISHOP] = undo->piece_key[BISHOP];
    // game->piece_key[ROOK] = undo->piece_key[ROOK];
    // game->piece_key[QUEEN] = undo->piece_key[QUEEN];
    // game->piece_key[KING] = undo->piece_key[KING];
    // printf("FROM: %d TO %d PIECE: %d TYPE: %d\n", from, to, p, mt);
    // print_game_board(game);
    //
    // assert(game->st != game->st->pst);
    game->st = game->st->pst;
    
}




/* @brief handles polyglot hash key / tt updates, bitboard, and "piece_at[][]" changes. also stores history, and replaces last_move which is used for refutations near the end of the function. */



// returns if move was valid - if false, undo this move
static inline bool make_move(Game * game, Move m, StateInfo * st, SearchStack * stack, Undo * undo){

    
    memcpy(st, game->st, sizeof(StateInfo));
    st->pst = game->st;
    game->st = st;
    uint64_t k = st->key;

    
    Side side = game->side_to_move;
    Side enemy_side = (Side)!side;
    uint8_t from = move_from(m);
    uint8_t to = move_to(m);
    MoveType mt = move_type(m);
    PieceType p = game->piece_at[from];
    // assert(p < PIECE_NONE);
    // assert(p == game->piece_info[game->sq_to_uid[from]].p);
    // assert(bits[from] & game->pieces[side][p]);
    PieceType cp = game->piece_at[to];
    undo->p = p;
    undo->cp = cp;
    int en_passant_index = st->en_passant_index;
    int ep_sq = st->en_passant_index;
    uint64_t blockers = game->board_pieces[BOTH];
    uint64_t * piece_board = &game->pieces[side][p];
    uint64_t * side_to_move_pieces = &game->board_pieces[side];
    uint64_t * other_side_pieces = &game->board_pieces[!side];
    undo->attack_undo_mask = 0;
    uint8_t id = game->sq_to_uid[from];
    uint8_t cap_id = game->sq_to_uid[to];
    bool dp = false;
    if (p == PAWN){
        dp = is_double_push(game, m);
    }

    bool cap = is_cap(game, m);
    undo->p_id = id;
    undo->cap_id = cap_id;
    undo->was_capture = cap;
    // assert(from != to);
    // assert((game->board_pieces[WHITE] & game->board_pieces[BLACK]) == 0);
    
    undo->attack_undo_count = 0;

    // undo->nonpawn_key[BLACK] = game->nonpawn_key[BLACK];
    // undo->nonpawn_key[WHITE] = game->nonpawn_key[WHITE];
    // undo->piece_key[PAWN] = game->piece_key[PAWN];
    // undo->piece_key[KNIGHT] = game->piece_key[KNIGHT];
    // undo->piece_key[BISHOP] = game->piece_key[BISHOP];
    // undo->piece_key[ROOK] = game->piece_key[ROOK];
    // undo->piece_key[QUEEN] = game->piece_key[QUEEN];
    // undo->piece_key[KING] = game->piece_key[KING];
    // undo->k_sq[WHITE] = game->k_sq[WHITE];
    // undo->k_sq[BLACK] = game->k_sq[BLACK];

    uint64_t our_pieces = *side_to_move_pieces;
    uint64_t their_pieces = *other_side_pieces;
    // undo->psqt_eval_mg[BLACK] = game->psqt_evaluation_mg[BLACK];
    // undo->psqt_eval_mg[WHITE] = game->psqt_evaluation_mg[WHITE];
    // undo->psqt_eval_eg[BLACK] = game->psqt_evaluation_eg[BLACK];
    // undo->psqt_eval_eg[WHITE] = game->psqt_evaluation_eg[WHITE];


    if (en_passant_index != -1){
        if (pawn_captures[!side][en_passant_index] & game->pieces[side][PAWN]) {
        
            st->key ^= get_en_passant_random(en_passant_index);
        }
        
    }

    
    switch(mt){
        case NORMAL:
            {


                uint64_t move_board = (bits[from] | bits[to]);
                *piece_board ^= move_board;

                *side_to_move_pieces ^= move_board;

                game->piece_at[to] = p;
                game->piece_at[from] = PIECE_NONE;
                
                st->psqt_evaluation_mg[side] -= PSQT_MG[side][p][from];
                st->psqt_evaluation_eg[side] -= PSQT_EG[side][p][from];
                st->psqt_evaluation_mg[side] += PSQT_MG[side][p][to];
                st->psqt_evaluation_eg[side] += PSQT_EG[side][p][to];

                if (cap){
                    
                    assert(cp < PIECE_NONE);
                    game->pieces[!side][cp] ^= bits[to]; 
                    *other_side_pieces ^= bits[to];
                    game->phase -= phase_values[cp];
                    st->psqt_evaluation_mg[!side] -= PSQT_MG[!side][cp][to];
                    st->psqt_evaluation_eg[!side] -= PSQT_EG[!side][cp][to];
                    blockers ^= bits[from];
                    remove_victim(game, st, enemy_side, cp, to, undo);
                } else {
                    
                    blockers ^= move_board;
                }

                put_piece(game, st, side, p, p, id, from, to, blockers, undo);
                
                break;
            }
        case ENPASSANT:
            {
                uint8_t capture_square = __builtin_ctzll(pawn_moves[!side][to]);
                cp = PAWN;
                assert(game->piece_at[capture_square] == PAWN);
                
                uint64_t move_board = (bits[from] | bits[to]);
                uint64_t cap_board = bits[capture_square];
                *piece_board ^= move_board;

                game->pieces[enemy_side][cp] ^= cap_board;

                *side_to_move_pieces ^= move_board;

                *other_side_pieces ^= cap_board;
                game->phase -= phase_values[cp];

                game->piece_at[to] = p;
                game->piece_at[from] = PIECE_NONE;
                game->piece_at[capture_square] = PIECE_NONE;


                st->psqt_evaluation_mg[side] -= PSQT_MG[side][p][from];
                st->psqt_evaluation_eg[side] -= PSQT_EG[side][p][from];
                st->psqt_evaluation_mg[side] += PSQT_MG[side][p][to];
                st->psqt_evaluation_eg[side] += PSQT_EG[side][p][to];

                st->psqt_evaluation_mg[!side] -= PSQT_MG[!side][cp][capture_square];
                st->psqt_evaluation_eg[!side] -= PSQT_EG[!side][cp][capture_square];


                blockers ^= (move_board | cap_board);

                undo->cp = PAWN;
                undo->cap_id = game->sq_to_uid[capture_square];
                remove_victim(game, st, enemy_side, cp, capture_square, undo);
                put_piece(game, st, side, p, p, id, from, to, blockers, undo);
            }
            break;
        case CASTLE:
            {
                CastleSide cs = (king_end_locations[side][KINGSIDE] == to ? KINGSIDE : QUEENSIDE);
                uint64_t move_board = (bits[from] | bits[to]);
                uint8_t rfrom = rook_castle_locations[side][cs][START], rend = rook_castle_locations[side][cs][END];
                uint64_t cb = 
                    bits[rfrom] | bits[rend];

                *piece_board ^= move_board;

                game->pieces[side][ROOK] ^= cb;


                *side_to_move_pieces ^= cb | move_board;

                st->psqt_evaluation_mg[side] -= PSQT_MG[side][p][from];
                st->psqt_evaluation_eg[side] -= PSQT_EG[side][p][from];
                st->psqt_evaluation_mg[side] += PSQT_MG[side][p][to];
                st->psqt_evaluation_eg[side] += PSQT_EG[side][p][to];


                st->psqt_evaluation_mg[side] -= PSQT_MG[side][ROOK][rfrom];
                st->psqt_evaluation_eg[side] -= PSQT_EG[side][ROOK][rfrom];
                st->psqt_evaluation_mg[side] += PSQT_MG[side][ROOK][rend];
                st->psqt_evaluation_eg[side] += PSQT_EG[side][ROOK][rend];

                game->piece_at[to] = p;
                game->piece_at[from] = PIECE_NONE;
                game->piece_at[rend] = ROOK;
                game->piece_at[rfrom] = PIECE_NONE;

                blockers ^= cb | move_board;
                uint8_t rid = game->sq_to_uid[rfrom];
                undo->cap_id = rid;
                put_piece(game, st, side, p, p, id, from, to, blockers, undo);
                put_piece(game, st, side, ROOK, ROOK, rid, rook_castle_locations[side][cs][START], rook_castle_locations[side][cs][END], blockers, undo);
                undo->attack_undo_mask |= bits[rend];


            }
            break;
        case PROMOTION:
            {

                // assert(p == PAWN);
                uint64_t s = bits[from];
                uint64_t e = bits[to];
                PieceType promo = move_promotion_type(m);
                // assert(promo < PIECE_NONE);
                
                *piece_board ^= s;

                game->pieces[side][promo] |= e;

                *side_to_move_pieces ^= (s | e);
                blockers ^= (s | e);

                if (cap){
                    
                    // assert(cp < PIECE_NONE);
                    // assert(e & game->pieces[!side][cp]);
                    // assert(cp == game->piece_info[game->sq_to_uid[to]].p);
                    game->pieces[!side][cp] ^= e;                    
                    *other_side_pieces ^= e;
                    blockers ^= e;
                
                    st->psqt_evaluation_mg[!side] -= PSQT_MG[!side][cp][to];
                    st->psqt_evaluation_eg[!side] -= PSQT_EG[!side][cp][to];

                    game->phase -= phase_values[cp];
                    remove_victim(game, st, enemy_side, cp, to, undo);
                    
                }

                st->psqt_evaluation_mg[side] -= PSQT_MG[side][p][from];
                st->psqt_evaluation_eg[side] -= PSQT_EG[side][p][from];
                st->psqt_evaluation_mg[side] += PSQT_MG[side][promo][to];
                st->psqt_evaluation_eg[side] += PSQT_EG[side][promo][to];


                game->phase += phase_values[promo];
                game->piece_at[to] = promo;
                game->piece_at[from] = PIECE_NONE;


                put_piece(game, st, side, PAWN, promo, id, from, to, blockers, undo);
                undo->p = promo;
                undo->attack_undo_mask |= bits[to];
            }
            break;
    }


    undo->ep_sq = en_passant_index;
    // undo->castle_flags[side][QUEENSIDE] = game->castle_flags[side][QUEENSIDE];    
    // undo->castle_flags[side][KINGSIDE] = game->castle_flags[side][KINGSIDE];    
    // undo->castle_flags[!side][QUEENSIDE] = game->castle_flags[!side][QUEENSIDE];    
    // undo->castle_flags[!side][KINGSIDE] = game->castle_flags[!side][KINGSIDE];    
    

    
    if (dp){
        st->en_passant_index = -push_direction[side] * 8 + to;
        // if we create an en passant square, we need to check for the special polyglot square as well

        if (game->pieces[enemy_side][PAWN] & pawn_captures[side][st->en_passant_index]){
            st->key ^= get_en_passant_random(st->en_passant_index);
        }

        
    } else {
        st->en_passant_index = -1;
    }

    
    if (to == rook_starting_locations[!side][QUEENSIDE]){
        if (st->castle_flags[!side][QUEENSIDE] == true){
            st->key ^= get_castling_random(enemy_side, QUEENSIDE);
        }
        st->castle_flags[!side][QUEENSIDE] = false;
    }
    if (to == rook_starting_locations[enemy_side][KINGSIDE]){
        if (st->castle_flags[!side][KINGSIDE] == true){
            st->key ^= get_castling_random(enemy_side, KINGSIDE);
        }
        st->castle_flags[!side][KINGSIDE] = false;
    }
    CastleSide rights;
    bool loses_rights = false;
    
    if (from == rook_starting_locations[side][QUEENSIDE]){
        loses_rights = true;
        rights = QUEENSIDE;
    } else if (from == rook_starting_locations[side][KINGSIDE]){
        loses_rights = true;
        rights = KINGSIDE;
    } else if (from == king_starting_locations[side]){
        loses_rights = true;
        rights = BOTHSIDE;
    }
    if (loses_rights){
        switch(rights){
            case QUEENSIDE:
                if (st->castle_flags[side][QUEENSIDE]){
                    st->key ^= get_castling_random(side, QUEENSIDE);
                }
                st->castle_flags[side][QUEENSIDE] = false;
                break;
            case KINGSIDE:
                if (st->castle_flags[side][KINGSIDE]){
                    st->key ^= get_castling_random(side, KINGSIDE);
                }
                st->castle_flags[side][KINGSIDE] = false;
                break;
            case BOTHSIDE:
                if (st->castle_flags[side][QUEENSIDE]){
                    st->key ^= get_castling_random(side, QUEENSIDE);
                }
                if (st->castle_flags[side][KINGSIDE]){
                    st->key ^= get_castling_random(side, KINGSIDE);
                }
                st->castle_flags[side][QUEENSIDE] = false;
                st->castle_flags[side][KINGSIDE] = false;
                break;
        
            default:
                break;
        }
    }

    stack->current_move = m;
    stack->moved_piece = p;
    stack->cap_piece = cp;
    st->key ^= get_turn_random();
    game->board_pieces[BOTH] = blockers;
    game->side_to_move = (Side)!game->side_to_move;

    game->key_history[game->history_count] = st->key;
    game->history_count += 1;
    

    game->fifty_move_clock_was = game->fifty_move_clock;
    if (p == PAWN || cap){
        // yes it starts at one don't ask me why i don't know why
        game->fifty_move_clock = 1;        
    } else {
        game->fifty_move_clock += 1;
    }
    // assert((game->board_pieces[WHITE] & game->board_pieces[BLACK]) == 0);


    // precompute checking info
    
    compute_check_info(game, st);
    
    return !in_check(game, side);
}



int set_board_to_fen(Game * game, char fen[MAX_FEN]);







/* @brief checks against our hash key history to avoid drawing when we are up in material. if we are down in eval, then our engine naturally plays to draw on purpose. */

static inline bool three_fold_repetition(Game * game, uint64_t key){
    int count = 0;
    for (int i = 0; i < game->history_count; i++){
        if (key == game->key_history[i]){
            count += 1;
        }
    }
    if (count >= 3){
        return true;
    }
    return false;
}

static inline void dump_stack_moves(SearchStack * stack, int ply){
    for (int i = 0; i < ply; i++){
        print_move_full(stack[i].current_move);
    }
}
static inline void dump_state_keys(const StateInfo * st){
    
    const StateInfo* cur = st;
    int count = 0;

    for (const StateInfo* s = st; s; s = s->pst) {
        printf("%d: %lx\n", count, s->key);
        count++;
    }
}

static inline bool threefold(const StateInfo* st) {
    const StateInfo* cur = st;
    int count = 0;

    for (const StateInfo* s = st->pst; s; s = s->pst) {

        if (s->key == cur->key) {
            count++;
            if (count >= 2){
                return true;
                
            }
        }
    }

    return false;
}

static inline bool three_fold_repetition_root(Game * game, uint64_t key){

    int count = 0;
    for (int i = 0; i < game->history_count; i++){
        if (key == game->key_history[i]){
            count += 1;
        }
    }
    if (count >= 3){
        return true;
    }
    return false;
}


/* @brief stores pv into the triangular pv table. you can find this implementation from code monkey king / chess programming wiki */

static inline void store_pv(int ply, Move move, SearchData * search_data) {
    if (ply >= 63) return;
    search_data->pv_table[ply][0] = move;

    for (int i = 0; i < search_data->pv_length[ply + 1]; i++) {
        search_data->pv_table[ply][i + 1] = search_data->pv_table[ply + 1][i];
    }

    search_data->pv_length[ply] = search_data->pv_length[ply + 1] + 1;
}



/* 
    @brief determines if a piece is pinned
    @param pinned_to_sq the square we are checking for pins TO 
*/


static inline uint64_t piece_is_pinned(Game * game, Side side, int pinned_to_sq){
    uint64_t occ = game->board_pieces[BOTH];
    uint64_t bishop_rays = fetch_bishop_moves(pinned_to_sq, occ);
    uint64_t rook_rays = fetch_rook_moves(pinned_to_sq, occ);
    uint64_t our_pieces = game->board_pieces[side];

    uint64_t rook_blockers = rook_rays & our_pieces;
    uint64_t bishop_blockers = bishop_rays & our_pieces;

    uint64_t rooks_and_queens = game->pieces[!side][QUEEN] | game->pieces[!side][ROOK];
    uint64_t bishops_and_queens = game->pieces[!side][QUEEN] | game->pieces[!side][BISHOP];

    uint64_t pinned = 0;

    while (rook_blockers){
        int pos = pop_lsb(&rook_blockers);
        if (game->piece_at[pos] == PAWN) continue;
        uint64_t ray = fetch_rook_moves(pinned_to_sq, occ ^ (bits[pos]));
        if (ray & rooks_and_queens) pinned |= (bits[pos]);
    }
    while (bishop_blockers){
        int pos = pop_lsb(&bishop_blockers);
        if (game->piece_at[pos] == PAWN) continue;
        uint64_t ray = fetch_bishop_moves(pinned_to_sq, occ ^ (bits[pos]));
        if (ray & bishops_and_queens) pinned |= (bits[pos]);
    }

    return pinned;
}


static inline void compute_pin_masks(Game * game, Side side, MovementMasks * masks){


    int ksq = 0;
    int qsq = 0;
    uint64_t our_pieces = game->board_pieces[side];
    uint64_t enemy_pieces = game->board_pieces[!side];
    uint64_t queens = game->pieces[side][QUEEN];
    uint64_t pins = 0;
    while (queens){
        int pos = pop_lsb(&queens);
        masks->our_pinned_pieces |= piece_is_pinned(game, side, pos);
    }
    ksq = bit_scan_forward(&game->pieces[side][KING]);
    masks->our_pinned_pieces |= piece_is_pinned(game, side, ksq);
    
    int e_ksq = 0;
    int e_qsq = 0;
    uint64_t e_queens = game->pieces[!side][QUEEN];
    uint64_t e_pins = 0;
    while (e_queens){
        int pos = pop_lsb(&e_queens);
        masks->enemy_pinned_pieces |= piece_is_pinned(game, (Side)!side, pos);
    }
    e_ksq = bit_scan_forward(&game->pieces[!side][KING]);
    masks->enemy_pinned_pieces |= piece_is_pinned(game, (Side)!side, e_ksq);


    
}

/* @brief only used during board setup, simply puts a piece on its board and updates piece_at */
static inline void set_piece(Game * game, uint64_t * board, Side side, PieceType type, int index){

    *board = set_bit(*board, index);    
    game->piece_at[index] = type;
    game->piece_info[game->piece_uid] = (PieceInfo){
        .p = type,
        .side = side,
        .pos = (uint8_t)index,
        .alive = true
    };
    game->sq_to_uid[index] = game->piece_uid;
    game->piece_uid += 1;

}


// basically just makes a state for the root node so that we don't dereference garbage
static inline void init_st(Game * game, StateInfo * st){

    game->st = st;
    memcpy(st, &game->os, sizeof(StateInfo));
    compute_check_info(game, st);
}


/* @brief main input loop, commands in types.h, leads to uci input if "uci" is invoked */

void handle_input(Game * game);

/* @brief sets up a blank new game from startpos */

int init_new_game(Game * game, Side color);


/* @brief sets up a blank new game from fen */

void init_game_from_fen(Game * game, Side color, char fen[MAX_FEN]);

/* @brief prints game to fen, invoked using "display" command */

void output_game_to_fen(Game * game, char fen[MAX_FEN]);

#endif
