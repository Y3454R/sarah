#ifndef EVAL_H
#define EVAL_H

#include "types.h"
#include "utils.h"
#include "math.h"
#include "magic.h"
#include "game.h"
#include "move_generation.h"

static void inline print_pair(const char *name, EvalPair p) {
    printf("%-20s W:%6d  B:%6d  Net:%6d\n",
           name, p.w, p.b, p.w - p.b);
}

static void inline debug_evaluate(EvalDebug *d) {


    
    printf("\n=== EVAL DEBUG ===\n");
    print_pair("Pawn structure", d->pawn);
    print_pair("Material", d->material);
    print_pair("King threat", d->king_threat);
    print_pair("King safety raw", d->king_safety_raw);
    print_pair("King safety scaled", d->king_safety_scaled);
    print_pair("Hanging pieces", d->hanging);
    print_pair("Rook files", d->rook_files);
    print_pair("Pins", d->pins);
    print_pair("Dev", d->development);

    printf("\nMG total: %d\n", d->mg_total);
    printf("EG total: %d\n", d->eg_total);
    printf("Phase: %d\n", d->phase);
    printf("Final eval: %d\n", d->final_score);
}
/* 
    @brief used to initialize evaluation and phase values when we set the board to a new fen. used to be way more useful, since our material is no longer incremental due to computation demands.
*/
void init_evaluate(Game * game);

// int see(Game *game, Move * move);

static inline int see(Game *game, Move move) {

    uint8_t sq = move_from(move);
    uint8_t from = move_to(move);
    bool cap = is_cap(game, move);

    PieceType capture_piece = move_cp(game, move);

    Side original_side = game->side_to_move;
    int side = side;

    uint64_t occ = game->board_pieces[BOTH]; 
    uint64_t temp_pieces[COLOR_MAX][PIECE_TYPES];

    for (int i = 0; i < PIECE_TYPES; i++){
        temp_pieces[BLACK][i] = game->pieces[BLACK][i];
        temp_pieces[WHITE][i] = game->pieces[WHITE][i];
    }
    int val = 0;
    int last_cap = 0;
    if (cap){
        
        // gain[depth] = PVAL[capture_piece];
        val = PVAL[capture_piece] - PVAL[move->piece];
        last_cap = val;
        occ &= ~(bits[sq]);
        temp_pieces[!side][capture_piece] &= ~(bits[sq]);  
    } else {
        // gain[depth] = 0;

        val = -PVAL[move->piece];
        last_cap = val;
    }

    occ &= ~(bits[from]);
    temp_pieces[side][move->piece] &= ~(bits[from]);  
    
    uint64_t attackers[2];
    attackers[WHITE] = attacker_mask_for_square(game, WHITE, temp_pieces[WHITE], move->end_index, occ);
    attackers[BLACK] = attacker_mask_for_square(game, BLACK, temp_pieces[BLACK], move->end_index, occ);
    // print_board(attackers[WHITE], WHITE_PAWN);
    // print_board(attackers[BLACK], BLACK_PAWN);

    side = !side; 

    while (true) {
        uint64_t att = attackers[side];
        if (!att || depth > 30) {
            // gain[depth] = 0;
            // depth--;
            val += -last_cap;
            break;
        }
        
        PieceType lva = (PieceType)0;
        int next_from = lva_from_attacker_mask(temp_pieces[side], att, &lva); 
        assert(next_from != -1);
        // printf("Depth %d: side %d captures with %c from %d\n",
        //        depth, side, PNAME[lva], next_from);
        depth++;
        // gain[depth] = PVAL[lva] - gain[depth-1];
        val += (side == original_side ? -PVAL[lva] : PVAL[lva]);
        last_cap = (side == original_side ? -PVAL[lva] : PVAL[lva]);

        occ ^= (bits[next_from]);
        temp_pieces[side][lva] ^= (bits[next_from]);
        attackers[side] ^= (bits[next_from]);

        uint64_t discovered_bishops = fetch_bishop_moves(sq, occ) & (temp_pieces[side][BISHOP] | temp_pieces[side][QUEEN]);
        uint64_t discovered_rooks = fetch_rook_moves(sq, occ) & (temp_pieces[side][ROOK] | temp_pieces[side][QUEEN]);
        attackers[side] |= discovered_bishops | discovered_rooks;
        
        uint64_t discovered_opp_bishops = fetch_bishop_moves(sq, occ) & (temp_pieces[!side][BISHOP] | temp_pieces[!side][QUEEN]);
        
        uint64_t discovered_opp_rooks = fetch_rook_moves(sq, occ) & (temp_pieces[!side][ROOK] | temp_pieces[!side][QUEEN]);
        attackers[!side] |= discovered_opp_bishops | discovered_opp_rooks;


        side = !side;
    }
    // if ((depth & 1) == 1)
    //     gain[depth - 1] = -gain[depth - 1];
    // for (int i = depth - 1; i >= 0; i--) 
    //     gain[i] = MAX(-gain[i], gain[i+1]);
    // val -= PVAL[move->piece];

    // printf("SEE: %d\n", val);
    return val;
}

/* 
  @brief does what you would expect, constants found in utils.
*/

static inline void evaluate_pawn_structure(Game * game, Side side, int * mg, int * eg, EvalMasks * masks){
    int score_mg = 0, score_eg = 0;

    // pawns

    uint64_t pawns = game->pieces[side][PAWN];
    uint64_t friendly_pawns = game->pieces[side][PAWN];
    uint64_t enemy_pawns = game->pieces[!side][PAWN];
    for (int i = 0; i < 8; i++){
        int count = __builtin_popcountll(pawns & file_masks[i]);
        // doubled pawn
        if (count > 1) score_mg += (count - 1) * DOUBLED_PAWN_PENALTY_MG;
        if (count > 1) score_eg += (count - 1) * DOUBLED_PAWN_PENALTY_EG;
    }
    uint64_t start_rank = 0;
    if (side == WHITE) {
        start_rank = rank_masks[1];
    } else {
        start_rank = rank_masks[6];
    }
    // uint64_t push_attacks = 0;

    uint64_t ra = !side ? (pawns << 7 & ~file_masks[7]) : pawns >> 9 & ~file_masks[7];
    uint64_t la = !side ? (pawns << 9 & ~file_masks[0]) : pawns >> 7 & ~file_masks[0];
    uint64_t attacks = ra | la;
    masks->pa[side] = attacks;

    

    while (pawns){
        int pos = __builtin_ctzll(pawns);
        pawns = pawns & (pawns - 1);

        // if (1ULL << pos & start_rank){
        //     push_attacks |= pawn_captures[side][pos + push_direction[side] * 16];
        // }
        // push_attacks |= pawn_captures[side][pos + push_direction[side] * 8];
        
        int file = SQ_TO_FILE[pos];
        int rank = side ? SQ_TO_RANK[pos] : SQ_TO_RANK[flip_square(pos)];
        bool enemy_pawn_on_file = enemy_pawns & in_front_file_masks[side][pos];
        bool enemy_pawn_in_front_adjacent = enemy_pawns & adjacent_in_front_masks[side][pos];
        bool is_passer = false;
        // passers
        if (!(game->pieces[!side][PAWN] & passed_pawn_masks[side][pos]) && !enemy_pawn_on_file) {
            score_mg += PASSED_PAWN_RANK_BONUS_MG[rank];
            score_eg += PASSED_PAWN_RANK_BONUS_EG[rank];
            is_passer = true;
            masks->passers[side] |= bits[pos];
        }

        // isolated
        if(!(game->pieces[side][PAWN] & adjacent_file_masks[file])) {
            if (is_passer){
                
                score_mg += ISOLATED_PAWN_PENALTY_MG / 2;
                score_eg += ISOLATED_PAWN_PENALTY_EG / 2;
            } else {
                score_mg += ISOLATED_PAWN_PENALTY_MG;
                score_eg += ISOLATED_PAWN_PENALTY_EG;
                
            }

        }
        // backward pawn
        bool has_supporting_adjacent = (friendly_pawns & (adjacent_file_masks[file] & in_front_ranks_masks[side][pos]));


        // check if a forward square is is controlled by an attacking enemy pawn
        int forward_sq = push_direction[side] * 8 + pos;
        bool enemy_controls_front = false;
        
        if (forward_sq >= 0 && forward_sq < 64){
            // from our side looking foward diagonally
            enemy_controls_front = pawn_captures[side][forward_sq] & enemy_pawns;
        }

        // is backward
        if (!has_supporting_adjacent && (enemy_controls_front || (bits[forward_sq] & game->pieces[!side][PAWN]))){
            score_mg += BACKWARD_PAWN_PENALTY_MG;
            score_eg += BACKWARD_PAWN_PENALTY_EG;
        }

        // candidate passer

        if (!enemy_pawn_on_file && !enemy_pawn_in_front_adjacent){
            score_mg += CANDIDATE_PASSER_BONUS_MG;
            score_eg += CANDIDATE_PASSER_BONUS_EG;
        }

        // chained pawn
        if (friendly_pawns & pawn_captures[side][pos]){
            
            score_mg += CHAINED_PAWN_BONUS_MG;
            score_eg += CHAINED_PAWN_BONUS_EG;
        }

        
        // int kfile = game->pieces[!side][KING] % 8;
    }
    // *ppa = push_attacks;
    *mg += score_mg;
    *eg += score_eg;
}

/* 
  @brief gets the space between both pawn lines and gives a bonus based on files that are closest to the middle 
  the goal with this function was to incentivize fighting for the center, especially when playing black, because I noticed a tendency to get caught in a blocked off position if we don't find early initiative. now with an opening book it doesn't *really* matter but I think it's still cool
*/

static inline int evaluate_pawn_space(Game * game, int * white, int  * black){
    
    uint64_t enemy_pawns = game->pieces[BLACK][PAWN];
    uint64_t friendly_pawns = game->pieces[WHITE][PAWN];
    int ws = 0, bs = 0;
    int sum_cnt_white = 0;
    int sum_cnt_black = 0;
    for (int f = 0; f < 8; f++){
        int fr = 0;
        int er = 0;

        uint64_t fp = (friendly_pawns & file_masks[f]);
        if (fp){
            int pos = __builtin_ctzll(fp);
            int r = SQ_TO_RANK[pos];
            fr = abs(7-(r));
            ws += (fr - 2) * PAWN_SPACE_FILE_WEIGHTS[f];
            sum_cnt_white++;
        } else {
        }
        uint64_t ep = (enemy_pawns & file_masks[f]);
        if (ep){
            // find lowest
            int pos = __builtin_ctzll(ep);
            // invert to compare
            er = SQ_TO_RANK[pos];
            bs += (er - 2) * PAWN_SPACE_FILE_WEIGHTS[f];
            sum_cnt_black++;
        } else {
        }
    }

    *white = (int )ws / (sum_cnt_white + 1);
    *black = (int )bs / (sum_cnt_black + 1);
    // *white = (int )ws / 8;
    // *black = (int )bs / 8;
    return ws - bs;
}


/* @brief connected rooks and open file bonus eval */

static inline void evaluate_rook_files(Game * game, int side, int  * mg, int  * eg) {

    uint64_t pawns = game->pieces[side][PAWN];
    uint64_t enemy_pawns = game->pieces[!side][PAWN];
    uint64_t rooks = game->pieces[side][ROOK];

    
    while (rooks) {
        int sq = __builtin_ctzll(rooks);
        rooks = rooks & (rooks - 1);
        int file = SQ_TO_FILE[sq];
        int rank = SQ_TO_RANK[sq];

        bool mine = pawns & file_masks[file];
        bool theirs = enemy_pawns & file_masks[file];

        if (!mine) {
            if (!theirs) {
                *mg += ROOK_OPEN_FILE_BONUS;
                *eg += ROOK_OPEN_FILE_BONUS;
            } else {
                *mg += ROOK_SEMI_OPEN_FILE_BONUS;
                *eg += ROOK_SEMI_OPEN_FILE_BONUS;
            }
        }
        bool connected_rooks = fetch_rook_moves(sq, game->board_pieces[BOTH]) & game->pieces[side][ROOK];
        if (connected_rooks){
            *mg += CONNECTED_ROOK_BONUS;
            *eg += CONNECTED_ROOK_BONUS;
            
        }
    }
}

/* @brief just a function that gives a bonus for late game king activity. this is supposed to just be handled by psqt, but I like to keep the king near to promoting pawns, especially stopping enemy promotions, with our lack of endgame tables */

static inline int evaluate_endgame_king(Game * game, Side side){
    
    const int MAX_MANHATTAN_DIST = 14;
    const int MAX_CENTER_MANHATTAN_DIST = 6;
    
    // get closest pawns to promotion and give a bonus for distance to it
    // TODO make the curve exponential and not linear
    int score = 0;
    int king_pos = __builtin_ctzll(game->pieces[side][KING]);
    int enemy_king_pos = __builtin_ctzll(game->pieces[!side][KING]);
    // uint64_t pawns = game->pieces[side][PAWN];
    // while (pawns){
    //     int pos = pop_lsb(&pawns);
    //     int pawn_val = PAWN_STORM_PSQT_EG[side][pos];

    //     // not close enough to promotion to be considered
    //     if (pawn_val < 4) continue;
    //     int dist = manhattan_distance[pos][king_pos];
    //     score += ((MAX_MANHATTAN_DIST - dist) * pawn_val) / 3;
    //     int enemy_dist = manhattan_distance[pos][enemy_king_pos];
    //     score -= ((MAX_MANHATTAN_DIST - enemy_dist) * pawn_val) / 3;
    // }
    int dist_to_center = center_manhattan_distance[king_pos];
    score += DIST_TO_CENTER_BONUS * (MAX_CENTER_MANHATTAN_DIST - dist_to_center);

    return score;
}


static inline int evaluate_early_game_development(Game * game, Side side){

    int eval = 0;
    for (int i = KNIGHT; i < KING; i++){
        if (STARTING_SQUARES[side][i] & game->pieces[side][i]){
            eval += STARTING_SQUARE_VALUES[i];
        }
    }
    return eval;
    
}



/* 
  @brief evaluates pins to a side's queen and king
  @return a score 
*/
static inline int evaluate_pins(Game * game, Side side, int ksq, EvalMasks * masks){
    // uint64_t queens = game->pieces[side][QUEEN];
    uint64_t pins = 0;
    // while (queens){
    //     int pos = __builtin_ctzll(queens);
    //     queens = queens & (queens - 1);
    //     pins |= piece_is_pinned(game, side, pos);
    // }
    pins |= piece_is_pinned(game, side, ksq);
    int pin_score = 0;
    uint64_t bad_pins = pins & masks->tdp[side];
    while(bad_pins){
        
        int pos = __builtin_ctzll(bad_pins);
        bad_pins = bad_pins & (bad_pins - 1);
        PieceInfo * info = &game->piece_info[game->sq_to_uid[pos]];
        pin_score += PIN_PT_PENALTY[info->p] * (side == game->side_to_move ? 1 : 2);
    }
    
    return pin_score;
}

static inline int opposite_direction(int dir){
    switch(dir){
        case 0:
            return 2;
        case 1:
            return 3;
        case 2:
            return 0;
        case 3:
            return 1;
        default:
            return 0;
    }
}

static inline void determine_queen_king_attack(Game * game, Side side, int pos, uint64_t ekz, int enemy_king_sq, uint64_t moves, int * mg, int * eg, int * ktv_mg, int * ktv_eg){
    
    int m = 0, e = 0;
    int km = 0, ke = 0;
    // if (rm & ekz){
        // find direction
    int dir = -1;
    for (int i = 0; i < 4; i++){
        if (LATERAL_RAYS[i][pos] & ekz){
             dir = i;
        }
    }
    // assert(dir != -1);
    if (dir != -1){
        uint64_t lr = LATERAL_RAYS[dir][pos] & moves;
        uint64_t lro = LATERAL_RAYS[opposite_direction(dir)][pos] & moves;
        if ((lr & game->pieces[side][ROOK]) || (lr & game->pieces[side][QUEEN]) ||lro & game->pieces[side][ROOK] || lr & game->pieces[side][QUEEN]){
             km += KING_ZONE_BATTERY_BONUS_MG;
             ke += KING_ZONE_BATTERY_BONUS_EG;
        }
        // if (LATERAL_RAYS[dir][pos] & game->pieces[!side][KING]){
        //     uint64_t ray_no_blocks = fetch_rook_moves(game, pos, bits[enemy_king_sq]) & LATERAL_RAYS[dir][pos] & ekz;
        //     int blockers = __builtin_popcountll(ray_no_blocks & game->board_pieces[!side]);
        
        //     const int blocker_offset = 3;
        //     km += (blocker_offset - blockers) * LATERAL_KING_ATTACK_THREAT_MG;
        //     ke += (blocker_offset - blockers) * LATERAL_KING_ATTACK_THREAT_EG;
            
        // }
        
    }
 
    int ddir = -1;
    for (int i = 0; i < 4; i++){
        if (DIAGONAL_RAYS[i][pos] & ekz){
             ddir = i;
        }
    }
    if (ddir != -1){
        uint64_t dr = DIAGONAL_RAYS[ddir][pos] & moves;
        uint64_t dro = DIAGONAL_RAYS[opposite_direction(ddir)][pos] & moves;
        if ((dr & game->pieces[side][BISHOP]) || (dr & game->pieces[side][QUEEN]) || dro & game->pieces[side][BISHOP] || dro & game->pieces[side][QUEEN]){
             km += KING_ZONE_BATTERY_BONUS_MG;
             ke += KING_ZONE_BATTERY_BONUS_EG;
        }
        // if (DIAGONAL_RAYS[dir][pos] & game->pieces[!side][KING]){
            
        //     uint64_t ray_no_blocks = fetch_bishop_moves(game, pos, bits[enemy_king_sq]) & DIAGONAL_RAYS[ddir][pos] & ekz;
        //     int blockers = __builtin_popcountll(ray_no_blocks & game->board_pieces[!side]);
        
        //     const int blocker_offset = 3;
        //     km += (blocker_offset - blockers) * DIAGONAL_KING_ATTACK_THREAT_MG;
        //     ke += (blocker_offset - blockers) * DIAGONAL_KING_ATTACK_THREAT_EG;
        // }
        
    }
    *mg += m;
    *eg += e;
    *ktv_mg += km;
    *ktv_eg += ke;
}

static inline void generate_attack_mask_and_eval_mobility(Game * game, Side side, EvalMasks * masks){
    
    // uint64_t attacked_squares = 0;
    // uint64_t datk = 0;
    // uint64_t a_nk = 0;
    int mc = 0;
    int mg = 0, eg = 0;
    int akz = 0;

    for (int i = 0; i < 32; i++){
        PieceInfo * info = &game->piece_info[i];
        if (info->side != side || !info->alive) continue;

        masks->tatk[side] |= masks->datk[side] & game->piece_attacks[i];
        masks->datk[side] |= masks->am[side] & game->piece_attacks[i];
        masks->am[side] |= game->piece_attacks[i];
        if (info->p != KING){
            masks->am_nk[side] |= game->piece_attacks[i];
        }
        if (info->p != PAWN){
            masks->am_np[side] |= game->piece_attacks[i];
        }
        if (info->p > BISHOP){
            masks->am_mtm[side] |= game->piece_attacks[i];
        }
        if (info->p > ROOK){
            masks->am_mtr[side] |= game->piece_attacks[i];
        }
        masks->am_p[side][info->p] |= game->piece_attacks[i];
    }

    
}



/* 
    @brief material and mobility eval, also to avoid unnecessary looping, we compute additional things such as tempo attacks based on piece values, and batteries and bishop pair etc.
    @param enemy_pawn_attacks attack mask from the pawn hash that helps us compute a penalty for pawns that restrict our mobility
    @return an attack mask used for later tempo eval 
*/
// uint64_t evaluate_material_weighted(Game * game, Side side, int  * out_mg, int  * out_eg, uint64_t enemy_pawn_attacks);

static inline uint64_t evaluate_material_weighted(Game * game, Side side, int  * out_mg, int  * out_eg, uint64_t enemy_pawn_attacks, int * enemy_ktv_mg, int * enemy_ktv_eg, uint64_t fam, uint64_t fdatk, uint64_t eam, uint64_t edatk, uint64_t e_ppa){
    
    const int MAX_MANHATTAN_DIST = 14;
    uint64_t both = game->board_pieces[BOTH];
    uint64_t our_pieces = game->board_pieces[side];
    uint64_t other_pieces = game->board_pieces[!side];
    int  mg = 0, eg = 0;
    uint64_t pawns = game->pieces[side][PAWN];
    // int pawn_count = __builtin_popcountll(pawns);
    uint64_t attack_mask = 0;
    // uint64_t katk_mask = 0;

    // hanging pieces
    // uint64_t fhp = our_pieces & ~fam;
    // uint64_t ehp = other_pieces & ~eam;

    
    int psqt_sign = 1;

    
    // this is here because of the texel tuning convention that our psqts end up negative for black. we still evaluate positive = white, but for simplicity's sake, we only subtract *after* this function returns
    if (side == BLACK) psqt_sign = -1;

    int moves_blocked_by_enemy_pawns = 0;
    int enemy_king_sq = __builtin_ctzll(game->pieces[!side][KING]);
    uint64_t ek = game->pieces[!side][KING];


    uint64_t enemy_king_zone = king_zone_masks[enemy_king_sq];
    int total = __builtin_popcountll(king_moves[enemy_king_sq]);
    int empty = __builtin_popcountll(king_moves[enemy_king_sq] & ~game->board_pieces[BOTH] & ~fam);
    int enemy_king_defended_squares = __builtin_popcountll(enemy_king_zone & eam);
    float ek_def_frac = (float)enemy_king_defended_squares / __builtin_popcountll(enemy_king_zone);
    int enemy_king_escape = empty;

    int ek_defenders = __builtin_popcountll((game->board_pieces[!side] & ~game->pieces[!side][PAWN]) & enemy_king_zone);


    
    int king_threat_level = 0;
    int ktv_mg = 0;
    int ktv_eg = 0;
    
    int piece_count = 0;

    uint64_t cb = game->pieces[side][BISHOP];
    bool has_dark_square_bishop = false;
    bool has_light_square_bishop = false;
    bool enemy_dark_square_bishop = false;
    bool enemy_light_square_bishop = false;
    if (game->pieces[side][BISHOP] & COLOR_SQUARES[BLACK]){
        has_dark_square_bishop = true;
    }
    if (game->pieces[side][BISHOP] & COLOR_SQUARES[WHITE]){
        has_light_square_bishop = true;
    }
    if (game->pieces[!side][BISHOP] & COLOR_SQUARES[BLACK]){
        enemy_dark_square_bishop = true;
    }
    if (game->pieces[!side][BISHOP] & COLOR_SQUARES[WHITE]){
        enemy_light_square_bishop = true;
    }

    int dark_square_pawns = __builtin_popcountll(game->pieces[side][PAWN] & COLOR_SQUARES[BLACK]);
    int light_square_pawns = __builtin_popcountll(game->pieces[side][PAWN] & COLOR_SQUARES[WHITE]);


    if (!has_light_square_bishop && has_dark_square_bishop){
        mg += light_square_pawns * COLOR_SQUARE_BASE_MG;
        if (!enemy_light_square_bishop){
            mg += COLOR_DEFENDER_ABSENT;
        }
    } 
    if (!has_dark_square_bishop && has_light_square_bishop){
        mg += dark_square_pawns * COLOR_SQUARE_BASE_MG;
        if (!enemy_dark_square_bishop){
            mg += COLOR_DEFENDER_ABSENT;
        }
    }
    if (game->phase < 8){
        if (!has_light_square_bishop && has_dark_square_bishop){
            eg += dark_square_pawns * COLOR_SQUARE_END_EG;
        } 
        if (!has_dark_square_bishop && has_light_square_bishop){
            eg += light_square_pawns * COLOR_SQUARE_END_EG;
        }
    }
    
    
    while (pawns){
        piece_count++;
        int pos = __builtin_ctzll(pawns);
        pawns = pawns & (pawns - 1);
        mg += PSQT_MG[side][PAWN][pos];
        eg += PSQT_EG[side][PAWN][pos];
        
        // if (atk){
        //     // int attack_pos = pop_lsb(&atk);
        //     PieceType p;
        //     int m = mva_from_attacker_mask(game, game->pieces[!side], atk, side, &p);
        //     if (1ULL << m & ehp){
        //         mg += ATTACKING_HIGHER_VALUE_BONUS[PAWN][p] * 2;
        //         eg += ATTACKING_HIGHER_VALUE_BONUS[PAWN][p] * 2;
        //     } else {
        //         mg += ATTACKING_HIGHER_VALUE_BONUS[PAWN][p];
        //         eg += ATTACKING_HIGHER_VALUE_BONUS[PAWN][p];
        //     }
        // }

        uint64_t p = bits[pos];
        bool defended = p & fam;
        if (defended){
            
            mg += DEFENDED_PIECE_BONUS;
            eg += DEFENDED_PIECE_BONUS;
        }
        if (p & eam){
            bool double_defended = p & fdatk;
            bool double_attacked = p & edatk;
            if (!defended || (double_attacked && !double_defended)){
                mg += THREAT_DEFICIT_PENALTY;
                eg += THREAT_DEFICIT_PENALTY;
            }
        }
        
        // if (game->phase > 12){
        //     if (manhattan_distance[pos][enemy_king_sq] < 5) {
        //         mg += (MAX_MANHATTAN_DIST - manhattan_distance[pos][enemy_king_sq]);
        //     }
        // }
        
        // if (game->phase < 10){
        //     if (pos % 2 == 1 && !has_dark_square_bishop){
        //         mg += PAWN_COLOR_PENALTY;
        //         eg += PAWN_COLOR_PENALTY;
        //     } else if (pos % 2 == 0 && !has_light_square_bishop){
        //         mg += PAWN_COLOR_PENALTY;
        //         eg += PAWN_COLOR_PENALTY;
        //     }
        // }
        // uint64_t pakz = pawn_captures[side][pos] & enemy_king_zone;
        // if (pakz){
        //     ktv_mg += PAWN_STORM_BONUS;
        // }

        // king_threat_level += (MAX_MANHATTAN_DIST - manhattan_distance[pos][enemy_king_sq]) * KING_THREAT_MULT[PAWN];
        // attack_mask |= pawn_captures[side][pos];
    }
    uint64_t knights = game->pieces[side][KNIGHT];
    int knight_count = __builtin_popcountll(knights);
    while (knights){
        piece_count++;
        int pos = __builtin_ctzll(knights);
        knights = knights & (knights - 1);
        uint64_t moves = knight_moves[pos] & ~our_pieces;
        int count = __builtin_popcountll(moves);
        mg += PSQT_MG[side][KNIGHT][pos];
        eg += PSQT_EG[side][KNIGHT][pos];
        // attack_mask |= knight_moves[pos];
        uint64_t attack = knight_moves[pos] & other_pieces;
        // if(attack){
        //     PieceType p;
        //     int m = mva_from_attacker_mask(game, game->pieces[!side], attack, side, &p);
        //     if (1ULL << m & ehp){
        //         mg += ATTACKING_HIGHER_VALUE_BONUS[KNIGHT][p] * 2;
        //         eg += ATTACKING_HIGHER_VALUE_BONUS[KNIGHT][p] * 2;
        //     } else {
        //         mg += ATTACKING_HIGHER_VALUE_BONUS[KNIGHT][p];
        //         eg += ATTACKING_HIGHER_VALUE_BONUS[KNIGHT][p];
        //     }

        // }

        uint64_t satk = moves & ~eam;
        int safe_attacks = __builtin_popcountll(satk);
        mg += (count - safe_attacks) * MOBILITY_BONUS_MG[KNIGHT];
        eg += (count - safe_attacks) * MOBILITY_BONUS_EG[KNIGHT];
        mg += safe_attacks * SAFE_MOBILITY_BONUS_MG[KNIGHT];
        eg += safe_attacks * SAFE_MOBILITY_BONUS_EG[KNIGHT];
        uint64_t datk = moves & fdatk;
        int double_attacks = __builtin_popcountll(datk);
        mg += double_attacks * DOUBLE_ATTACK_BONUS_MG;
        eg += double_attacks * DOUBLE_ATTACK_BONUS_EG;
        uint64_t katk = moves & enemy_king_zone;
        uint64_t kiatk = katk & king_moves[enemy_king_sq];

        uint64_t p = bits[pos];
        bool defended = p & fam;
        if (defended){
            
            mg += DEFENDED_PIECE_BONUS;
            eg += DEFENDED_PIECE_BONUS;
        }
        if (p & eam){
            bool double_defended = p & fdatk;
            bool double_attacked = p & edatk;
            if (pawn_captures[side][pos] & game->pieces[!side][PAWN]){
                mg += ATTACKED_BY_PAWN_PENALTY;
                eg += ATTACKED_BY_PAWN_PENALTY;
            }
            if (!defended || (double_attacked && !double_defended)){
                mg += THREAT_DEFICIT_PENALTY;
                eg += THREAT_DEFICIT_PENALTY;
                if (safe_attacks == 0){
                    mg += UNSTOPPABLE_ATTACK_PENALTY;
                    eg += UNSTOPPABLE_ATTACK_PENALTY;
                }
            }
        }
        if (katk) {
            // katk_mask |= katk;
            uint64_t dkatk = datk & enemy_king_zone;
            int king_attacks = __builtin_popcountll(katk);
            uint64_t skatk = katk & satk;
            int safe_king_attacks = __builtin_popcountll(skatk); 
            int double_king_attacks = __builtin_popcountll(dkatk);
            int double_safe_king_attacks = __builtin_popcountll(dkatk & ~(eam & ~king_moves[enemy_king_sq]));
            king_attacks -= safe_king_attacks;
            safe_king_attacks -= double_safe_king_attacks;
            ktv_mg += SAFE_KING_ATTACK_MG * safe_king_attacks;
            ktv_eg += SAFE_KING_ATTACK_EG * safe_king_attacks;
            ktv_mg += KING_THREAT_MG[KNIGHT] * king_attacks;
            ktv_eg += KING_THREAT_EG[KNIGHT] * king_attacks;

            // ktv_mg += double_king_attacks * DOUBLE_KING_ATTACK_BONUS_MG;
            // ktv_eg += double_king_attacks * DOUBLE_KING_ATTACK_BONUS_EG;
            ktv_mg += double_safe_king_attacks * DOUBLE_SAFE_KING_ATTACK_BONUS_MG;
            ktv_eg += double_safe_king_attacks * DOUBLE_SAFE_KING_ATTACK_BONUS_EG;
        }

        
        // king_threat_level += (MAX_MANHATTAN_DIST - manhattan_distance[pos][enemy_king_sq]) * KING_THREAT_MULT[KNIGHT];
    }
    uint64_t bishops = game->pieces[side][BISHOP];
    int bishop_count = 0;
    while (bishops){
        piece_count++;
        int pos = __builtin_ctzll(bishops);
        bishops = bishops & (bishops - 1);
        bishop_count++;
        uint64_t raw_moves = fetch_bishop_moves(pos, both);
        uint64_t moves = raw_moves & ~our_pieces;
        int count = __builtin_popcountll(moves);
        mg += PSQT_MG[side][BISHOP][pos];
        eg += PSQT_EG[side][BISHOP][pos];
        attack_mask |= raw_moves;
        uint64_t attack = raw_moves & other_pieces;
        // if(attack){
        //     PieceType p;
        //     int m = mva_from_attacker_mask(game, game->pieces[!side], attack, side, &p);
        //     if (1ULL << m & ehp){
        //         mg += ATTACKING_HIGHER_VALUE_BONUS[BISHOP][p] * 2;
        //         eg += ATTACKING_HIGHER_VALUE_BONUS[BISHOP][p] * 2;
        //     } else {
        //         mg += ATTACKING_HIGHER_VALUE_BONUS[BISHOP][p];
        //         eg += ATTACKING_HIGHER_VALUE_BONUS[BISHOP][p];
        //     }
        // }
        // king_threat_level += (MAX_MANHATTAN_DIST - manhattan_distance[pos][enemy_king_sq]) * KING_THREAT_MULT[BISHOP];
        uint64_t p = bits[pos];
        bool defended = p & fam;
        uint64_t satk = moves & ~eam;
        int safe_attacks = __builtin_popcountll(satk);
        if (defended){
            
            mg += DEFENDED_PIECE_BONUS;
            eg += DEFENDED_PIECE_BONUS;
        }
        if (p & eam){
            bool double_defended = p & fdatk;
            bool double_attacked = p & edatk;
            if (pawn_captures[side][pos] & game->pieces[!side][PAWN]){
                mg += ATTACKED_BY_PAWN_PENALTY;
                eg += ATTACKED_BY_PAWN_PENALTY;
            }
            if (!defended || (double_attacked && !double_defended)){
                mg += THREAT_DEFICIT_PENALTY;
                eg += THREAT_DEFICIT_PENALTY;
                if (safe_attacks == 0){
                    mg += UNSTOPPABLE_ATTACK_PENALTY;
                    eg += UNSTOPPABLE_ATTACK_PENALTY;
                }
            }
        }
        if (count <= 2){
            mg += SLIDER_BOXED_IN_PENALTY * (3 - count);
            eg += SLIDER_BOXED_IN_PENALTY * (3 - count);
        }
        mg += (count - safe_attacks) * MOBILITY_BONUS_MG[BISHOP];
        eg += (count - safe_attacks) * MOBILITY_BONUS_EG[BISHOP];
        mg += safe_attacks * SAFE_MOBILITY_BONUS_MG[BISHOP];
        eg += safe_attacks * SAFE_MOBILITY_BONUS_EG[BISHOP];
        uint64_t datk = moves & fdatk;
        int double_attacks = __builtin_popcountll(datk);
        mg += double_attacks * DOUBLE_ATTACK_BONUS_MG;
        eg += double_attacks * DOUBLE_ATTACK_BONUS_EG;
        uint64_t katk = moves & enemy_king_zone;
        if (katk) {
            
            // if (bishop_moves[pos] & ek){
            //     // find direction
            //     int dir = -1;
            //     for (int i = 0; i < 4; i++){
            //         if (DIAGONAL_RAYS[i][pos] & ek){
            //             dir = i;
            //         }
            //     }
            //     assert(dir != -1);
            //     uint64_t ray = DIAGONAL_RAYS[dir][pos] & fetch_bishop_moves(game, pos, 1ULL << enemy_king_sq);
            //     int eb = 0;
            //     int fb = 0;
            //     while (ray){
            //         int rp = pop_lsb(&ray);
            //         if (1ULL << rp & other_pieces){
            //             eb++;
            //         }
            //         if (1ULL << rp & our_pieces){
            //             fb++;
            //         }
            //     }
            //     const int blocker_offset = 3;
            //     ktv_mg += (blocker_offset - eb) * DIAGONAL_KING_ATTACK_THREAT_MG;
            //     ktv_eg += (blocker_offset - eb) * DIAGONAL_KING_ATTACK_THREAT_EG;
            // }
            
            // katk_mask |= katk;
            uint64_t dkatk = datk & enemy_king_zone;
            int king_attacks = __builtin_popcountll(katk);
            uint64_t skatk = katk & satk;
            int safe_king_attacks = __builtin_popcountll(skatk); 
            int double_king_attacks = __builtin_popcountll(dkatk);
            int double_safe_king_attacks = __builtin_popcountll(dkatk & ~(eam & ~king_moves[enemy_king_sq]));
            king_attacks -= safe_king_attacks;
            safe_king_attacks -= double_safe_king_attacks;
            ktv_mg += SAFE_KING_ATTACK_MG * safe_king_attacks;
            ktv_eg += SAFE_KING_ATTACK_EG * safe_king_attacks;
            ktv_mg += KING_THREAT_MG[BISHOP] * king_attacks;
            ktv_eg += KING_THREAT_EG[BISHOP] * king_attacks;
            // ktv_mg += double_king_attacks * DOUBLE_KING_ATTACK_BONUS_MG;
            // ktv_eg += double_king_attacks * DOUBLE_KING_ATTACK_BONUS_EG;
            ktv_mg += double_safe_king_attacks * DOUBLE_SAFE_KING_ATTACK_BONUS_MG;
            ktv_eg += double_safe_king_attacks * DOUBLE_SAFE_KING_ATTACK_BONUS_EG;
        }
    }
    // if  (bishop_count >= 2){
    //     mg += BISHOP_PAIR_BONUS;
    //     eg += BISHOP_PAIR_BONUS;
    // }
    uint64_t rooks = game->pieces[side][ROOK];
    int rook_count = __builtin_popcountll(rooks);
    while (rooks){
        piece_count++;
        int pos = __builtin_ctzll(rooks);
        rooks = rooks & (rooks - 1);
        uint64_t raw_moves = fetch_rook_moves(pos, both);
        uint64_t moves = raw_moves & ~our_pieces;
        int count = __builtin_popcountll(moves);
        mg += PSQT_MG[side][ROOK][pos];
        eg += PSQT_EG[side][ROOK][pos];
        attack_mask |= raw_moves;
        // we are attacking
        uint64_t attack = raw_moves & other_pieces;
        // if(attack){
        //     PieceType p;
        //     int m = mva_from_attacker_mask(game, game->pieces[!side], attack, side, &p);
        //     if (1ULL << m & ehp){
        //         mg += ATTACKING_HIGHER_VALUE_BONUS[ROOK][p] * 2;
        //         eg += ATTACKING_HIGHER_VALUE_BONUS[ROOK][p] * 2;
        //     } else {
        //         mg += ATTACKING_HIGHER_VALUE_BONUS[ROOK][p];
        //         eg += ATTACKING_HIGHER_VALUE_BONUS[ROOK][p];
        //     }
        // }
        // king_threat_level += (MAX_MANHATTAN_DIST - manhattan_distance[pos][enemy_king_sq]) * KING_THREAT_MULT[ROOK];
        uint64_t p = 1ULL << pos;
        bool defended = p & fam;
        uint64_t satk = moves & ~eam;
        int safe_attacks = __builtin_popcountll(satk);
        if (defended){
            
            mg += DEFENDED_PIECE_BONUS;
            eg += DEFENDED_PIECE_BONUS;
        }
        if (p & eam){
            bool double_defended = p & fdatk;
            bool double_attacked = p & edatk;
            if (pawn_captures[side][pos] & game->pieces[!side][PAWN]){
                mg += ATTACKED_BY_PAWN_PENALTY;
                eg += ATTACKED_BY_PAWN_PENALTY;
            }
            if (!defended || (double_attacked && !double_defended)){
                mg += THREAT_DEFICIT_PENALTY;
                eg += THREAT_DEFICIT_PENALTY;
                if (safe_attacks == 0){
                    mg += UNSTOPPABLE_ATTACK_PENALTY;
                    eg += UNSTOPPABLE_ATTACK_PENALTY;
                }
            }
        }

        if (count <= 2){
            mg += SLIDER_BOXED_IN_PENALTY * (3 - count);
            eg += SLIDER_BOXED_IN_PENALTY * (3 - count);
        }
        mg += (count - safe_attacks) * MOBILITY_BONUS_MG[ROOK];
        eg += (count - safe_attacks) * MOBILITY_BONUS_EG[ROOK];
        mg += safe_attacks * SAFE_MOBILITY_BONUS_MG[ROOK];
        eg += safe_attacks * SAFE_MOBILITY_BONUS_EG[ROOK];
        uint64_t datk = moves & fdatk;
        int double_attacks = __builtin_popcountll(datk);
        mg += double_attacks * DOUBLE_ATTACK_BONUS_MG;
        eg += double_attacks * DOUBLE_ATTACK_BONUS_EG;
        uint64_t katk = moves & enemy_king_zone;

        if (katk) {
            // if (rook_moves[pos] & ek){
            //     // find direction
            //     int dir = -1;
            //     for (int i = 0; i < 4; i++){
            //         if (LATERAL_RAYS[i][pos] & ek){
            //             dir = i;
            //         }
            //     }
            //     assert(dir != -1);
            //     uint64_t ray = LATERAL_RAYS[dir][pos] & fetch_rook_moves(game, pos, 1ULL << enemy_king_sq);
            //     int eb = 0;
            //     int fb = 0;
            //     while (ray){
            //         int rp = pop_lsb(&ray);
            //         if (1ULL << rp & other_pieces){
            //             eb++;
            //         }
            //         if (1ULL << rp & our_pieces){
            //             fb++;
            //         }
            //     }
            //     const int blocker_offset = 3;
            //     ktv_mg += (blocker_offset - eb) * LATERAL_KING_ATTACK_THREAT_MG;
            //     ktv_eg += (blocker_offset - eb) * LATERAL_KING_ATTACK_THREAT_EG;
            // }
            
            // katk_mask |= katk;
            uint64_t dkatk = datk & enemy_king_zone;
            int king_attacks = __builtin_popcountll(katk);
            uint64_t skatk = katk & satk;
            int safe_king_attacks = __builtin_popcountll(skatk); 
            int double_king_attacks = __builtin_popcountll(dkatk);
            int double_safe_king_attacks = __builtin_popcountll(dkatk & ~(eam & ~king_moves[enemy_king_sq]));
            king_attacks -= safe_king_attacks;
            safe_king_attacks -= double_safe_king_attacks;
            ktv_mg += double_safe_king_attacks * DOUBLE_SAFE_KING_ATTACK_BONUS_MG;
            ktv_eg += double_safe_king_attacks * DOUBLE_SAFE_KING_ATTACK_BONUS_EG;
            ktv_mg += SAFE_KING_ATTACK_MG * safe_king_attacks;
            ktv_eg += SAFE_KING_ATTACK_EG * safe_king_attacks;
            ktv_mg += KING_THREAT_MG[ROOK] * king_attacks;
            ktv_eg += KING_THREAT_EG[ROOK] * king_attacks;
            ktv_mg += double_king_attacks * DOUBLE_KING_ATTACK_BONUS_MG;
            ktv_eg += double_king_attacks * DOUBLE_KING_ATTACK_BONUS_EG;
        }
    }
    uint64_t queens = game->pieces[side][QUEEN];
    int queen_count = __builtin_popcountll(queens);
    while (queens){
        piece_count++;
        int pos = __builtin_ctzll(queens);
        queens = queens & (queens - 1);
        uint64_t q_rook_moves = fetch_rook_moves(pos, both);
        uint64_t q_bishop_moves =  fetch_bishop_moves(pos, both);
        uint64_t raw_moves = q_rook_moves | q_bishop_moves;
        uint64_t moves = raw_moves & ~our_pieces;
        int count = __builtin_popcountll(moves);
        // int swap_pos = pos;
        mg += PSQT_MG[side][QUEEN][pos];
        eg += PSQT_EG[side][QUEEN][pos];
        attack_mask |= raw_moves;
        // if (q_rook_moves & game->pieces[side][ROOK]){
        //     if (q_rook_moves & king_zone_masks[enemy_king_sq]){
        //         mg += KING_ZONE_BATTERY_BONUS;
        //         eg += KING_ZONE_BATTERY_BONUS;
        //     }
            
        //     mg += QUEEN_ROOK_CONNECTED_BONUS;
        //     eg += QUEEN_ROOK_CONNECTED_BONUS;
        // }
        // if (q_bishop_moves & game->pieces[side][BISHOP]){
        //     if (q_bishop_moves & king_zone_masks[enemy_king_sq]){
        //         mg += KING_ZONE_BATTERY_BONUS;
        //         eg += KING_ZONE_BATTERY_BONUS;
        //     }
        //     mg += QUEEN_BISHOP_CONNECTED_BONUS;
        //     eg += QUEEN_BISHOP_CONNECTED_BONUS;
        // }
        // we are attacking
        uint64_t attack = raw_moves & other_pieces;
        // if(attack){
        //     PieceType p;
        //     int m = mva_from_attacker_mask(game, game->pieces[!side], attack, side, &p);
        //     if (1ULL << m & ehp){
        //         mg += ATTACKING_HIGHER_VALUE_BONUS[QUEEN][p] * 2;
        //         eg += ATTACKING_HIGHER_VALUE_BONUS[QUEEN][p] * 2;
        //     } else {
        //         mg += ATTACKING_HIGHER_VALUE_BONUS[QUEEN][p];
        //         eg += ATTACKING_HIGHER_VALUE_BONUS[QUEEN][p];
        //     }
        // }
        // king_threat_level += (MAX_MANHATTAN_DIST - manhattan_distance[pos][enemy_king_sq]) * KING_THREAT_MULT[QUEEN];
        // if (count <= 2){
        //     mg += SLIDER_BOXED_IN_PENALTY * (2 - count);
        //     eg += SLIDER_BOXED_IN_PENALTY * (2 - count);
        // }
        uint64_t satk = moves & ~eam;
        int safe_attacks = __builtin_popcountll(satk);
        mg += (count - safe_attacks) * MOBILITY_BONUS_MG[QUEEN];
        eg += (count - safe_attacks) * MOBILITY_BONUS_EG[QUEEN];
        mg += safe_attacks * SAFE_MOBILITY_BONUS_MG[QUEEN];
        eg += safe_attacks * SAFE_MOBILITY_BONUS_EG[QUEEN];
        uint64_t datk = moves & fdatk;
        int double_attacks = __builtin_popcountll(datk);
        mg += double_attacks * DOUBLE_ATTACK_BONUS_MG;
        eg += double_attacks * DOUBLE_ATTACK_BONUS_EG;
        uint64_t katk = moves & enemy_king_zone;
        uint64_t p = 1ULL << pos;
        bool defended = p & fam;
        if (defended){
            
            mg += DEFENDED_PIECE_BONUS;
            eg += DEFENDED_PIECE_BONUS;
        }
        if (p & eam){
            bool double_defended = p & fdatk;
            bool double_attacked = p & edatk;
            if (pawn_captures[side][pos] & game->pieces[!side][PAWN]){
                mg += ATTACKED_BY_PAWN_PENALTY;
                eg += ATTACKED_BY_PAWN_PENALTY;
            }
            if (!defended || (double_attacked && !double_defended)){
                mg += THREAT_DEFICIT_PENALTY;
                eg += THREAT_DEFICIT_PENALTY;
                // if (safe_attacks == 0){
                //     mg += UNSTOPPABLE_ATTACK_PENALTY;
                //     eg += UNSTOPPABLE_ATTACK_PENALTY;
                // }
            }
        }

        // if (katk) {

            // if (rook_moves[pos] & ek){
            //     // find direction
            //     int dir = -1;
            //     for (int i = 0; i < 4; i++){
            //         if (LATERAL_RAYS[i][pos] & ek){
            //             dir = i;
            //         }
            //     }
            //     assert(dir != -1);
            //     uint64_t ray = LATERAL_RAYS[dir][pos] & fetch_rook_moves(game, pos, 1ULL << enemy_king_sq);
            //     int eb = 0;
            //     int fb = 0;
            //     while (ray){
            //         int rp = pop_lsb(&ray);
            //         if (1ULL << rp & other_pieces){
            //             eb++;
            //         }
            //         if (1ULL << rp & our_pieces){
            //             fb++;
            //         }
            //     }
            //     const int blocker_offset = 3;
            //     ktv_mg += (blocker_offset - eb) * LATERAL_KING_ATTACK_THREAT_MG;
            //     ktv_eg += (blocker_offset - eb) * LATERAL_KING_ATTACK_THREAT_EG;
            // } else if (bishop_moves[pos] & ek){
                
            //     int dir = -1;
            //     for (int i = 0; i < 4; i++){
            //         if (DIAGONAL_RAYS[i][pos] & ek){
            //             dir = i;
            //         }
            //     }
            //     assert(dir != -1);
            //     uint64_t ray = DIAGONAL_RAYS[dir][pos] & fetch_bishop_moves(game, pos, 1ULL << enemy_king_sq);
            //     int eb = 0;
            //     int fb = 0;
            //     while (ray){
            //         int rp = pop_lsb(&ray);
            //         if (1ULL << rp & other_pieces){
            //             eb++;
            //         }
            //         if (1ULL << rp & our_pieces){
            //             fb++;
            //         }
            //     }
            //     const int blocker_offset = 3;
            //     mg += (blocker_offset - eb) * DIAGONAL_KING_ATTACK_THREAT_MG;
            //     eg += (blocker_offset - eb) * DIAGONAL_KING_ATTACK_THREAT_EG;
            // }
            // katk_mask |= katk;
         uint64_t dkatk = datk & enemy_king_zone;
         int king_attacks = __builtin_popcountll(katk);
         uint64_t skatk = katk & satk;
         int safe_king_attacks = __builtin_popcountll(skatk); 
         int double_king_attacks = __builtin_popcountll(dkatk);
         int double_safe_king_attacks = __builtin_popcountll(dkatk & ~(eam & ~king_moves[enemy_king_sq]));

         king_attacks -= safe_king_attacks;
         safe_king_attacks -= double_safe_king_attacks;
         ktv_mg += KING_THREAT_MG[QUEEN] * king_attacks;
         ktv_eg += KING_THREAT_EG[QUEEN] * king_attacks;
         ktv_mg += SAFE_KING_ATTACK_MG * safe_king_attacks;
         ktv_eg += SAFE_KING_ATTACK_EG * safe_king_attacks;
         // ktv_mg += double_king_attacks * DOUBLE_KING_ATTACK_BONUS_MG;
         // ktv_eg += double_king_attacks * DOUBLE_KING_ATTACK_BONUS_EG;
         ktv_mg += double_safe_king_attacks * DOUBLE_SAFE_KING_ATTACK_BONUS_MG;
         ktv_eg += double_safe_king_attacks * DOUBLE_SAFE_KING_ATTACK_BONUS_EG;
        // }
    }


    uint64_t fsatk = fam & ~eam;
    uint64_t fkatk = fam & enemy_king_zone;
    if (fkatk){
        uint64_t fdkatk = fdatk & enemy_king_zone;
        uint64_t fskatk = fsatk & enemy_king_zone;
        uint64_t fdskatk = fdkatk & fskatk;
        int king_attacks = __builtin_popcountll(fdkatk);
        int safe_king_attacks = __builtin_popcountll(fskatk);
        int double_safe_king_attacks = __builtin_popcountll(fdskatk);
        king_attacks -= safe_king_attacks;
        safe_king_attacks -= double_safe_king_attacks;
        ktv_mg += KING_THREAT_MG[KNIGHT] * king_attacks;
        ktv_eg += KING_THREAT_MG[KNIGHT] * king_attacks;
        ktv_mg += double_safe_king_attacks * DOUBLE_SAFE_KING_ATTACK_BONUS_MG;
        ktv_eg += double_safe_king_attacks * DOUBLE_SAFE_KING_ATTACK_BONUS_EG;
        ktv_mg += SAFE_KING_ATTACK_MG * safe_king_attacks;
        ktv_eg += SAFE_KING_ATTACK_EG * safe_king_attacks;
        
    }
        
    int kpos = __builtin_ctzll(game->pieces[side][KING]);
    int swap_wkpos = kpos;
    piece_count += 1;
    // uint64_t king_moves = king_moves[kpos];
    mg += PSQT_MG[side][KING][kpos];
    eg += PSQT_EG[side][KING][kpos];
    attack_mask |= king_moves[kpos];
    // king_threat_level = pow(king_threat_level, 1.2) / piece_count / 1100;
    // king_threat_level = pow(king_threat_level, 1.2) / piece_count;
    // king_threat_level /= piece_count;
    // ktv_mg = pow((float)ktv_mg, 1.9);
    // ktv_eg = pow((float)ktv_eg, 1.9);
    ktv_mg = ktv_mg * ktv_mg;
    ktv_eg = ktv_eg * ktv_eg;
    // // ktv_mg /= (ek_defenders + 1) * 1.3;
    // // ktv_eg /= (ek_defenders + 1) * 1.3;
    // // ktv_mg /= (enemy_king_escape + 1) * 1.8;
    // // ktv_eg /= (enemy_king_escape + 1) * 1.8;
    if (enemy_king_defended_squares > 0){
        ktv_mg *= (float)(1-ek_def_frac * 0.4);
        ktv_eg *= (float)(1-ek_def_frac * 0.25);
        
    }
    if (ek_defenders > 0){
        ktv_mg /= (float)(ek_defenders) * 4.2;
        ktv_eg /= (float)(ek_defenders) * 2.7;
        
    }
    if (enemy_king_escape > 0){
        ktv_mg /= (float)(enemy_king_escape ) * 2.0;
        ktv_eg /= (float)(enemy_king_escape ) * 4.0;
        
    }
    *enemy_ktv_mg -= ktv_mg;
    *enemy_ktv_eg -= ktv_eg;
    
    // printf("KTV %d\n",ktv_mg);
    

    
    *(out_mg) += mg;
    *(out_eg) += eg;
    return attack_mask;
}


/* @brief evaluates open files and diagonals near the king, and checks for attackers on them. it used to be much more verbose, which is why that n exists as a diagonal
TODO check forward diagonals in king push direction, not just + 8 */

 // void evaluate_king_pawn_safety(Game * game, Side side, int  * out_mg, int  * out_eg);
static inline uint64_t evaluate_king_pawn_safety(Game * game, Side side, int  * out_mg, int  * out_eg, int * aks_mg, int * aks_eg){
    
    int king_sq = __builtin_ctzll(game->pieces[side][KING]);
    uint64_t our_pawns = game->pieces[side][PAWN];
    uint64_t their_pawns = game->pieces[!side][PAWN];
    uint64_t pawn_shield =  game->pieces[side][PAWN] & pawn_shield_masks[side][king_sq];    
    int  mg = 0, eg = 0;
    int a_mg = 0, a_eg = 0;
    int sign = 1;
    if (side == BLACK) sign = -1;
    int shield = __builtin_popcountll(pawn_shield);
    if (game->phase > 12){
        mg += shield * PAWN_SHIELD_BONUS;
        eg += shield * PAWN_SHIELD_BONUS / 3;
    }
    int open_file_penalties = 0;

    int file = SQ_TO_FILE[king_sq];
    uint64_t open_attack_mask = 0;
    bool open_file_cap = false;
    // if (file >= 1){
    //     bool semi_open = !(our_pawns & file_masks[file - 1]);
    //     if (semi_open){
    //         if (!(their_pawns & file_masks[file - 1])){
    //             if ((file_masks[file - 1] & game->pieces[!side][ROOK]) || file_masks[file - 1] & game->pieces[!side][QUEEN] && !open_file_cap){
    //                 a_mg += OPEN_FILE_NEAR_KING_WITH_ATTACKER;
    //                 a_eg += OPEN_FILE_NEAR_KING_WITH_ATTACKER;
    //                 open_file_cap = true;
                    
    //             } else {
    //                 mg += OPEN_FILE_NEAR_KING_PENALTY;
    //                 eg += OPEN_FILE_NEAR_KING_PENALTY;
    //                 open_attack_mask |= file_masks[file - 1];
                    
    //             }
    //         } else {
    //             mg += SEMI_OPEN_FILE_NEAR_KING_PENALTY;
    //             eg += SEMI_OPEN_FILE_NEAR_KING_PENALTY;
    //         }
    //     }
    // }
    // if (file <= 6) {
        
    //     bool semi_open = !(our_pawns & file_masks[file + 1]);
    //     if (semi_open){
    //         if (!(their_pawns & file_masks[file + 1])){
    //             if ((file_masks[file + 1] & game->pieces[!side][ROOK]) || file_masks[file + 1] & game->pieces[!side][QUEEN] && !open_file_cap){
    //                 a_mg += OPEN_FILE_NEAR_KING_WITH_ATTACKER;
    //                 a_eg += OPEN_FILE_NEAR_KING_WITH_ATTACKER;
    //                 open_file_cap = true;
                    
    //             } else {
    //                 mg += OPEN_FILE_NEAR_KING_PENALTY;
    //                 eg += OPEN_FILE_NEAR_KING_PENALTY;
    //                 open_attack_mask |= file_masks[file + 1];
                    
    //             }
    //         } else {
    //             mg += SEMI_OPEN_FILE_NEAR_KING_PENALTY;
    //             eg += SEMI_OPEN_FILE_NEAR_KING_PENALTY;
    //         }
    //     }
    // }

    // bool semi_open = !(our_pawns & file_masks[file]);
    // if (semi_open){
    //     if (!(their_pawns & file_masks[file])){
    //             if ((file_masks[file] & game->pieces[!side][ROOK]) || file_masks[file] & game->pieces[!side][QUEEN]){
    //                 a_mg += OPEN_FILE_FROM_KING_WITH_ATTACKER;
    //                 a_eg += OPEN_FILE_FROM_KING_WITH_ATTACKER;
                    
    //             } else {
    //                 mg += OPEN_FILE_NEAR_KING_PENALTY;
    //                 eg += OPEN_FILE_NEAR_KING_PENALTY;
                    
    //                 open_attack_mask |= file_masks[file];
    //             }
    //             // mg += OPEN_MIDDLE_FILE_NEAR_KING_ADDITIONAL_PENALTY;
    //             // eg += OPEN_MIDDLE_FILE_NEAR_KING_ADDITIONAL_PENALTY;
    //         } else {
    //         mg += SEMI_OPEN_FILE_NEAR_KING_PENALTY;
    //         eg += SEMI_OPEN_FILE_NEAR_KING_PENALTY;
    //     }
    // }

    // // diagonals
    // int open_diagonal_penalties = 0;
    // int n = king_sq + push_direction[side] * 8;
    // // int e = king_sq + 1;
    // // int w = king_sq - 1;
    // // int s = king_sq - 8;
    // // if (n >= 0 && n <= 63){
    // //     uint64_t diagonals_near_king_without_blockers = bishop_moves[n];
    // //     bool semi_open_diagonal = !(diagonals_near_king_without_blockers & our_pawns);
    // //     if (semi_open_diagonal){
    // //         if (!(diagonals_near_king_without_blockers & their_pawns)){
    // //             if ((diagonals_near_king_without_blockers & game->pieces[!side][QUEEN]) || diagonals_near_king_without_blockers & game->pieces[!side][BISHOP]){
    // //                 mg += OPEN_DIAGONAL_NEAR_KING_WITH_ATTACKER;
    // //                 eg += OPEN_DIAGONAL_NEAR_KING_WITH_ATTACKER;
                
    // //             } else {
    // //                 mg += OPEN_DIAGONAL_NEAR_KING_PENALTY;
    // //                 eg += OPEN_DIAGONAL_NEAR_KING_PENALTY;
                
    // //             }
    // //         } else {
    // //             mg += SEMI_OPEN_DIAGONAL_NEAR_KING_PENALTY;
    // //             eg += SEMI_OPEN_DIAGONAL_NEAR_KING_PENALTY;
            
    // //         }
    // //     }
    // // }
    
    // uint64_t diagonals_from_king_without_blockers = bishop_moves[king_sq];
    // if (!(diagonals_from_king_without_blockers & our_pawns)){
    //     if (!(diagonals_from_king_without_blockers & their_pawns)){
    //         if ((diagonals_from_king_without_blockers & game->pieces[!side][QUEEN]) || diagonals_from_king_without_blockers & game->pieces[!side][BISHOP]){
    //             mg += OPEN_DIAGONAL_FROM_KING_WITH_ATTACKER;
    //             eg += OPEN_DIAGONAL_FROM_KING_WITH_ATTACKER;
                
    //         } else {
    //             mg += OPEN_DIAGONAL_FROM_KING_PENALTY;
    //             eg += OPEN_DIAGONAL_FROM_KING_PENALTY;
                
    //         }
    //     } else {
    //         mg += SEMI_OPEN_DIAGONAL_NEAR_KING_PENALTY;
    //         eg += SEMI_OPEN_DIAGONAL_NEAR_KING_PENALTY;
            
    //     }
    // }
    
    // make this other piece bonus have a bigger ring than just the ring so it doesn't make us cramp our king
    // int shield_score = (shield) * texel_weights[params.pawn_shield] * sign; 
    // return ss + open_file_penalties + open_diagonal_penalties + shield;
    *out_mg += mg;
    *out_eg += eg;
    *aks_mg += a_mg;
    *aks_eg += a_eg;
    return open_attack_mask;
}



/* @brief evaluates overall safety by usual defender-attacker ideas, used to use manhattan distance but it was overengineered. needs work though */
// void evaluate_king_safety(Game *game, Side side, int  * mg, int  * eg, uint64_t our_attack_mask, uint64_t enemy_attack_mask);
static inline void evaluate_king_safety(Game *game, Side side, int  * mg, int  * eg, uint64_t our_attack_mask, uint64_t enemy_attack_mask){
    // if (game->phase < 10) return 0;

    int king_sq = __builtin_ctzll(game->pieces[side][KING]);

    int  attack_score = 0;
    // small penalty for squares attacked inside of the king zone
    int far_attacks = __builtin_popcountll(king_zone_masks[king_sq] & enemy_attack_mask);
    int zone_attacks = __builtin_popcountll(king_zone_masks[king_sq] & game->board_pieces[!side]);
    attack_score += zone_attacks * KING_ZONE_ATTACK_PENALTY ;
    


    int  defense_score = 0;
    int close_defenders = __builtin_popcountll(king_zone_masks[king_sq] & our_attack_mask);
    int zone_defense = __builtin_popcountll(king_zone_masks[king_sq] & game->board_pieces[side] & ~game->pieces[side][PAWN]);
    defense_score += zone_defense * KING_ZONE_DEFENSE_SCORE; 

    // bonus for king breathing room, not applied in very early game
    // half of the total spaces gives the most points
    // if (game->phase < 19){

    //     int total = __builtin_popcountll(king_zone_masks[king_sq]);
    //     int empty = __builtin_popcountll(king_zone_masks[king_sq] & ~game->board_pieces[BOTH]);
    //     int diff = abs((total / 2) - empty);
    //     *mg += diff * KING_SPACE_PENALTY_MG;
    //     *eg += diff * KING_SPACE_PENALTY_EG;
    // }

    

    
    *mg += attack_score + defense_score;
    *eg += attack_score + defense_score;
}


static inline void evaluate_king_threat(Game * game, Side side, EvalMasks * masks, int * ktv_out){

    const int MAX_KTV = 499;

    int ktv = *ktv_out;
    int mr_mg = 0, mr_eg = 0;
    int enemy_king_sq = game->k_sq[!side];
    uint64_t ek = game->pieces[!side][KING];
    uint64_t enemy_king_zone = king_zone_masks[enemy_king_sq];
    int empty = __builtin_popcountll(king_moves[enemy_king_sq] & ~game->board_pieces[BOTH] & ~masks->am[side]);
    int enemy_king_defended_squares = __builtin_popcountll(enemy_king_zone & masks->am_nk[!side] | (king_moves[enemy_king_sq] & ~masks->am[side]));
    int enemy_king_zone_count = __builtin_popcountll(enemy_king_zone);
    int ek_defenders = __builtin_popcountll((game->board_pieces[!side] & ~game->pieces[!side][PAWN]) & enemy_king_zone);

    // pawn shield only relevant early
    uint64_t pawn_shield =  game->pieces[!side][PAWN] & pawn_shield_masks[!side][enemy_king_sq];    
    int shield = 0;
    if (game->phase > 12){
        shield = __builtin_popcountll(pawn_shield);

    }

    uint64_t checks[PIECE_TYPES];
    checks[PAWN] = pawn_captures[!side][enemy_king_sq];
    checks[KNIGHT] = knight_moves[enemy_king_sq];
    uint64_t bm = fetch_bishop_moves(enemy_king_sq, game->board_pieces[BOTH]);
    uint64_t rm = fetch_rook_moves(enemy_king_sq, game->board_pieces[BOTH]);
    checks[BISHOP] = bm;
    checks[ROOK] = rm;
    checks[QUEEN] = bm | rm;
    checks[KING] = 0;
    bool is_stm = side == game->side_to_move;

    uint64_t weak_sq = masks->am[side] & ~masks->datk[!side] & (~masks->am[!side] | masks->am_p[!side][QUEEN] | masks->am_p[!side][KING]);
    uint64_t safe = masks->tdp[!side];
    
    int def_score = 0;

    int attacks = 0;
    uint64_t safe_attacks = safe & enemy_king_zone;
    int sa = __builtin_popcountll(safe_attacks);
    uint64_t double_safe = masks->datk[side] & safe & enemy_king_zone;
    int dsafe = __builtin_popcountll(double_safe);
    sa -= dsafe;

    // safe attacks are defined as on enemy tdp, which is attacked but not defended or double attacked but not double defended
    attacks += 4 * sa;
    attacks += DOUBLE_SAFE_KING_ATTACKS * __builtin_popcountll(double_safe);
    
    // checks and contact checks
    uint64_t ncheck = checks[KNIGHT] & masks->am_p[side][KNIGHT];
    if (ncheck){
        if (ncheck & safe){
            attacks += N_SAFE_CHECK;
        }
    }

    uint64_t bcheck = checks[BISHOP] & masks->am_p[side][BISHOP];
    if (bcheck){
        if (bcheck & safe){
            attacks += B_SAFE_CHECK;
        }
    }

    uint64_t rcheck = checks[ROOK] & masks->am_p[side][ROOK];
    if (rcheck){
        if (rcheck & safe){
            attacks += R_SAFE_CHECK;
            if (rcheck & safe & king_moves[enemy_king_sq] & ~masks->am_nk[!side]){
                attacks += ROOK_CONTACT_CHECK;
            }
        }
    }
    uint64_t qcheck = checks[QUEEN] & masks->am_p[side][QUEEN];
    if (qcheck){
        if (qcheck & safe){
            attacks += Q_SAFE_CHECK;
            if (qcheck & safe & king_moves[enemy_king_sq] & ~masks->am_nk[!side]){
                attacks += QUEEN_CONTACT_CHECK;
            }
        }
    }

    // correction for hanging pieces
    // extra correction for hanging pieces in the king zone
    // this handles throwing the queen in front of the king, it is conservative to make sure we dont overshoot, since it isn't infallible
    // historically this uncounted the piece's attack contribution, however this was too slow
    for (int i = 0; i < 32; i++){
        PieceInfo * info = &game->piece_info[i];
        if (!info->alive || info->p == PAWN || info->side != side) continue;
        int pos = info->pos;
        PieceType p = info->p;
        uint64_t m = game->piece_attacks[i];
        uint64_t mz = m & enemy_king_zone;
        if (mz){
            if (bits[pos] & masks->tdp[side]){
                attacks -= KING_THREAT_MG[p] * (game->side_to_move == side ? 1 : 2);
            }
        }
    }

    // if (is_stm) attacks += 15;
    int def_minor = __builtin_popcountll((masks->am_p[!side][KNIGHT] | masks->am_p[!side][BISHOP]) & king_moves[enemy_king_sq]);
    bool has_queen = game->pieces[side][QUEEN];
    int weak_cnt = __builtin_popcountll(masks->tdp[!side] & king_moves[enemy_king_sq]);
    int aikz = __builtin_popcountll(game->board_pieces[side] & enemy_king_zone & ~game->pieces[side][PAWN]);

    ktv += game->atk_kz[side];
    ktv += attacks;
    ktv += aikz * 7;
    ktv -= ((float)enemy_king_defended_squares / enemy_king_zone_count) * 16;
    ktv -= def_minor * 2;
    ktv -= !has_queen * 20;
    // ktv += weak_cnt * 4;
    ktv -= ek_defenders * 7;
    ktv -= shield * PAWN_SHIELD_BONUS * ((float)game->phase / MAX_PHASE);
    ktv -= empty * 10 * (1.0f - ((float)game->phase / MAX_PHASE));

    ktv = MAX(ktv, 0);
    ktv = pow(ktv, 1.35);
    // ktv = ktv * ktv / 24;

    *ktv_out = -ktv;
}
static inline bool material_is_lone_king(Game * game, Side side){
    if (game->pieces[side][KNIGHT] == 0 && game->pieces[side][BISHOP] == 0 && game->pieces[side][ROOK] == 0 && game->pieces[side][QUEEN] == 0) return true;
    return false;
}
static inline bool passer_is_unstoppable(Game * game, Side side, int pos, EvalMasks * masks){
    if (!material_is_lone_king(game, (Side)!side)) return false;

    if (game->board_pieces[BOTH] & in_front_file_masks[side][pos] || masks->am[!side] & in_front_file_masks[side][pos]) return false; 

    if (manhattan_distance[game->k_sq[!side]][pos] >= 2) return true;
    if (side ? SQ_TO_RANK[pos] >= 5 : SQ_TO_RANK[pos] <= 2 && (king_moves[game->k_sq[!side]] & in_front_file_masks[side][pos]) == 0) return true;

    // int rank = SQ_TO_RANK[pos];
    // int file = SQ_TO_FILE[pos];
    // int push = rank + push_direction[side];
    // uint64_t f_sq = rank_masks[push] & file_masks[file];
    return false;
}

static inline int mul_shift(int a, int b, int c){
   int bias = 1 << (c - 1);
   return (a * b + bias) >> c;
}

static inline void evaluate_material(Game * game, Side side, EvalMasks * masks, int * out_mg, int * out_eg, int * threats_mg, int * threats_eg, int * mobility_mg, int * mobility_eg){

    uint64_t fp = game->board_pieces[side];
    uint64_t ep = game->board_pieces[!side];
    int mg = 0, eg = 0;
    int t_mg = 0, t_eg = 0;
    int mob_mg = 0, mob_eg = 0;
    int phase = game->phase;
    uint64_t fpawns = game->pieces[side][PAWN];
    uint64_t epawns = game->pieces[!side][PAWN];
    int enemy_king_sq = __builtin_ctzll(game->pieces[!side][KING]);
    bool has_dark_square_bishop = false;
    bool has_light_square_bishop = false;
    bool enemy_dark_square_bishop = false;
    bool enemy_light_square_bishop = false;
    int bc = 0;
    if (game->pieces[side][BISHOP] & COLOR_SQUARES[BLACK]){
        has_dark_square_bishop = true;
        bc += 1;
    }
    if (game->pieces[side][BISHOP] & COLOR_SQUARES[WHITE]){
        has_light_square_bishop = true;
        bc += 1;
    }
    if (bc == 2){
        mg += BISHOP_PAIR_BONUS_MG;
        eg += BISHOP_PAIR_BONUS_EG;
    }

    // old color square logic, never gave us an elo increase
    
    // if (game->pieces[!side][BISHOP] & COLOR_SQUARES[BLACK]){
    //     enemy_dark_square_bishop = true;
    // }
    // if (game->pieces[!side][BISHOP] & COLOR_SQUARES[WHITE]){
    //     enemy_light_square_bishop = true;
    // }

    // int dark_square_pawns = __builtin_popcountll(game->pieces[side][PAWN] & COLOR_SQUARES[BLACK]);
    // int light_square_pawns = __builtin_popcountll(game->pieces[side][PAWN] & COLOR_SQUARES[WHITE]);

    // int pawn_overlap = __builtin_popcountll(masks->pa[!side] & masks->pa[side]);

    // if (phase > 12 && pawn_overlap <= 2){
    //     if (!has_light_square_bishop && has_dark_square_bishop){
    //         mg += light_square_pawns * COLOR_SQUARE_BASE_MG * (enemy_light_square_bishop ? 0.7 : 1.3) * (game->pieces[!side][QUEEN] > 0 ? 0.7 : 1.3);
    //     } 
    //     if (!has_dark_square_bishop && has_light_square_bishop){
    //         mg += dark_square_pawns * COLOR_SQUARE_BASE_MG * (enemy_dark_square_bishop ? 0.7 : 1.3) * (game->pieces[!side][QUEEN] > 0 ? 0.7 : 1.3);
    //     }
    // }
    // if (game->phase < 8){
    //     if (!has_light_square_bishop && has_dark_square_bishop){
    //         eg += dark_square_pawns * COLOR_SQUARE_END_EG * (enemy_dark_square_bishop ? 1 : 2);
    //     } 
    //     if (!has_dark_square_bishop && has_light_square_bishop){
    //         eg += light_square_pawns * COLOR_SQUARE_END_EG * (enemy_light_square_bishop ? 1 : 2);
    //     }
    // }
    // process defended / undefended threats
    // uint64_t defended = fp & masks->am[side];
    // uint64_t eattacked = ep & masks->am[side];
    // uint64_t edefended = ep & masks->am[!side];
    // uint64_t edouble_defended = ep & masks->datk[!side];
    // uint64_t edouble_attacked = ep & masks->datk[side];
    // uint64_t tdp = (eattacked & ~edefended) | (edouble_attacked & ~edouble_defended & ~masks->pa[!side]);



    int def_count = __builtin_popcountll(masks->am[side] & fp & ~fpawns);
    mg += DEFENDED_PIECE_BONUS * def_count;
    eg += DEFENDED_PIECE_BONUS * def_count;


    uint64_t pawn_attacks = masks->pa[side] & ep & ~epawns;
    t_mg += __builtin_popcountll(pawn_attacks) * PAWN_THREAT_VALUE;

    int passer_mg = 0, passer_eg = 0;

    // passers stage 1
    uint64_t passers = masks->passers[side];
    int passers_on_seven = 0;
    while (passers){
        int pos = __builtin_ctzll(passers);
        passers = passers & (passers - 1);
        
        if (passer_is_unstoppable(game, side, pos, masks)) {
            eg += PVAL[QUEEN];
        } else {
            int rank = SQ_TO_RANK[pos];
            if (rank == (side ? 6 : 1)){
                passers_on_seven++;
            }
            int file = SQ_TO_FILE[pos];
            int push = rank + push_direction[side];
            uint64_t f_sq = rank_masks[push] & file_masks[file];
            if (f_sq & masks->tdp[side]){
                passer_eg += PASSER_DEFICIT_PENALTY_EG;
            } else if (f_sq & masks->am[side]){
                passer_eg += PASSER_SUPPORTED_BONUS_EG;
            }
            if (in_front_file_masks[side][pos] & game->board_pieces[!side]){
                passer_eg += PASSER_BLOCKED_EG;
            }

            // if we are defended and about to promote, we are most likely locking enemy pieces into defending our promotion square. this bonus reflects that
            if (bits[pos] & ~masks->tdp[side] && game->phase >= 3 && game->phase <= 9 && (rank == 6 || rank == 1)){
                if (f_sq & ep){
                    passer_eg += OPPONENT_PIECE_STUCK_IN_FRONT_OF_PASSER;
                }
                if (promotion_ranks[side] & game->pieces[!side][ROOK]){
                    passer_eg += OPPONENT_ROOK_STUCK_ON_PROMOTION_RANK;
                }
                
            }
        }
    }

    // more than one passer about to promote
    passer_eg += MAX(passers_on_seven - 1, 0) * 20;


    // this generates pushes for pawns based on occupancy, and then gets a mask for attacks from only the safe pushes

    // if (phase > 12){
    //     CastleSide fside = BOTHSIDE; CastleSide eside = BOTHSIDE;
    //     if (bits[game->k_sq[side]] & castle_occupation_masks[side][QUEENSIDE]){
    //         fside = QUEENSIDE;
    //     } else if (bits[game->k_sq[side]] & castle_occupation_masks[side][KINGSIDE]){
    //         fside = KINGSIDE;
    //     // } else if (game->k_sq[side] == king_starting_locations[side]){
    //     //     // count pieces between rooks that can block castling
    //     //     int ocastle = __builtin_popcountll((castle_occupation_masks[side][KINGSIDE] | castle_occupation_masks[side][QUEENSIDE]) & game->board_pieces[BOTH]);

    //     //     mg += PIECE_OBSTRUCTS_CASTLE * ocastle;

    //     }
    //     if (bits[game->k_sq[!side]] & castle_occupation_masks[!side][QUEENSIDE]){
    //         fside = QUEENSIDE;
    //     } else if (bits[game->k_sq[!side]] & castle_occupation_masks[!side][KINGSIDE]){
    //         fside = KINGSIDE;
    //     }
    //     if ((fside == QUEENSIDE && eside == KINGSIDE)
    //         || (fside == KINGSIDE && eside == QUEENSIDE)){
    //         uint64_t storm = king_moves[enemy_king_sq + push_direction[!side] * 16] & fpawns;
    //         t_mg += PAWN_STORM_BONUS * __builtin_popcountll(storm);
    //     }
    // }
    
    if (phase > 8){
        // squares deep in enemy territory that we have higher threat on
        uint64_t ews = masks->tdp[!side] & masks->am[side] & ET2[side];
        int ews_count = __builtin_popcountll(ews);
        t_mg += TDP_WEAK_SQ[ews_count];
    }
    if (phase > 13){
        // undefended pieces in enemy territory
        uint64_t oed = masks->tdp[side] & ET3[side] & fp;
        int oed_count = __builtin_popcountll(oed);
        
        t_mg += TDP_OED_PEN[oed_count];
    }
    if (phase > 11){
        uint64_t awp = masks->am[side] & game->pieces[!side][PAWN] & ~masks->pa[!side] & masks->tdp[!side];
        int awp_count = __builtin_popcountll(awp);
        t_mg += awp_count * ATTACKING_WEAK_PAWN;
    }

    


    // process mobility queue
    for (int i = 0; i < 32; i++){
        PieceInfo * info = &game->piece_info[i];
        if (info->side != side || !info->alive || info->p == PAWN  || info->p == KING) continue;
        int pos = info->pos;
        PieceType p = info->p;
        uint64_t m = game->piece_attacks[i];

        
        uint64_t mob = m & ~masks->am[!side];
        int count = __builtin_popcountll(mob);
        uint64_t b = bits[pos];
        if (phase > 12){
            if (b & masks->out[!side]){
                mg += OCCUPIES_OUTPOST_SQUARE_BONUS[p];
            }
            if (m & masks->out[side]){
                mg += OUTPOST_CONTROLLED_BONUS;
            }
            if (!(mob & ET3[side]) && (info->p == KNIGHT || info->p == BISHOP)){
                mg += MINOR_CANNOT_ENTER_ENEMY_MG;
            }
        }
        mob_mg += MOBILITY_MG[p][count];
        mob_eg += MOBILITY_EG[p][count];

        if (count == 0 && (masks->tdp[side] & b) && phase > 12 && game->side_to_move != side){
            mob_mg += UNSTOPPABLE_ATTACK_PENALTY;
        }

        bool tdp = b & masks->tdp[side];

        uint64_t more_than = 0;
        if (p <= BISHOP){
            more_than = masks->am_mtm[!side];
        } else if (p == ROOK){
            more_than = masks->am_mtr[!side];
        }

        // defended by a more valuable piece 
        // int pseudo_see = __builtin_popcountll(more_than & m & masks->datk[side] & ~masks->datk[!side]);
        // t_mg += pseudo_see * 3;
        // t_eg += pseudo_see * 3;
        
        bool piece_attacked = b & masks->am[!side];
        uint64_t a = m & ep;
        while (a){
            int pos = __builtin_ctzll(a);
            a = a & (a - 1);
            int tv = THREAT_VALUES[p][game->piece_at[pos]];
            uint64_t p = bits[pos];
            if (p & king_moves[enemy_king_sq]){
                tv *= 2;
            }
            // if threat deficit OR defended by only a higher value piece
            // if (p & masks->tdp[!side] ||
            //     p & more_than & ~masks->datk[!side]){
            if (p & masks->tdp[!side]){
                if (side == game->side_to_move){
                    tv *= 2;
                } else {
                    tv *= 1.5;
                }
            }
            if (piece_attacked){
                tv *= 0.9;
            }
            if (tdp && side != game->side_to_move){
                tv *= 0.5;
            }

            t_mg += tv;
            t_eg += tv;
        }

        
        // // rook on passer file bonus
        if (phase < 12 && masks->passers[side] && info->p == ROOK){
            uint64_t passp = side ? masks->passers[side] >> 8 : masks->passers[side] << 8;
            if (masks->passers_file[side] & b){
                // passer_mg += ROOK_ON_PASSER_FILE_BONUS_MG;
                passer_eg += ROOK_ON_PASSER_FILE_BONUS_EG;

                // if rook is stuck in front of pawn, give a penalty
                if (passp & b && (side ? SQ_TO_RANK[pos] == 7 : SQ_TO_RANK[pos] == 0)){
                    // passer_mg += ROOK_STUCK_IN_FRONT_OF_PASSER_PENALTY;
                    passer_eg += ROOK_STUCK_IN_FRONT_OF_PASSER_PENALTY;
                }

            }
            
        }
        if (p == ROOK){
            
            int file = SQ_TO_FILE[pos];
            int rank = SQ_TO_RANK[pos];


            if (!(fpawns & file_masks[file])) {
                if (!(epawns & file_masks[file])) {
                    mg += ROOK_OPEN_FILE_BONUS;
                    eg += ROOK_OPEN_FILE_BONUS;
                } else {
                    mg += ROOK_SEMI_OPEN_FILE_BONUS;
                    eg += ROOK_SEMI_OPEN_FILE_BONUS;
                }
            }
            if (m & game->pieces[side][ROOK]){
                mg += CONNECTED_ROOK_BONUS;
                eg += CONNECTED_ROOK_BONUS;
            
            }
        } else if (p == QUEEN){
            
            if (rook_moves[pos] & m & game->pieces[side][ROOK]){
                mg += QUEEN_ROOK_CONNECTED_BONUS_MG;
                eg += QUEEN_ROOK_CONNECTED_BONUS_EG;
            }
            if (bishop_moves[pos] & m & game->pieces[side][BISHOP]){
                mg += QUEEN_BISHOP_CONNECTED_BONUS_MG;
                eg += QUEEN_BISHOP_CONNECTED_BONUS_EG;
            }
        } else if (p == BISHOP){
            int file = SQ_TO_FILE[pos];
            
            if (file <= 1 || file >= 6 && b & ET3[!side] && phase > 15 && count <= 3 && m & epawns){

                mob_mg += SLIDER_BOXED_IN_PENALTY;
                
            }
        }

    }
    
    // printf("TSCORE: %d\n", threat_score);
    // threat_score = THREAT_SCORE[MAX(threat_score + 3, 0)];
    // printf("TSCORE: %d\n", threat_score);

    // threat_score = pow(threat_score, 1.2) / 1.5;

    // float pphase = (float)MAX(MIN(12 - phase, 12), 0) / 52.0f;
        
    // if (passer_eg > 0){
        
    // passer_eg = PASSER_SCORE[passer_eg + 100];
    //     passer_eg = (int)pow(passer_eg, 1.0 + pphase);
    // }
    // printf("T: %d\n", passer_eg);
    mg += passer_mg;
    eg += passer_eg;
    *threats_mg = t_mg;
    *threats_eg += t_eg;
    *out_mg += mg;
    *out_eg += eg;
    *mobility_mg += mob_mg;
    *mobility_eg += mob_eg;

}

static inline int compute_king_danger(Game * game, Side side, int akz){
    const int MAX_MANHATTAN_DIST = 8;
    int enemy_king_sq = __builtin_ctzll(game->pieces[!side][KING]);
    int queen_sq = __builtin_ctzll(game->pieces[side][QUEEN]);
    // int qdist = MAX(MAX_MANHATTAN_DIST - manhattan_distance[queen_sq][enemy_king_sq], 0);
    uint64_t enemy_king_zone = king_zone_masks[enemy_king_sq];

    int of = 0;
    // check for open files
    if (game->phase > 12){
        int file = SQ_TO_FILE[enemy_king_sq];
        if (file_masks[file] & ~game->pieces[!side][PAWN]){
            of++;
        }
        if (file > 0){
            if (file_masks[file-1] & ~game->pieces[!side][PAWN]){
                of++;
            }
        }
        if (file < 7){
            if (file_masks[file+1] & ~game->pieces[!side][PAWN]){
                of++;
            }
        }
    }

    int aikz = __builtin_popcountll(enemy_king_zone & game->board_pieces[side]);
    int dikz = __builtin_popcountll(enemy_king_zone & game->board_pieces[!side] & ~game->pieces[!side][PAWN]);

    return MAX(akz * 3 + aikz * 5 + of * 5 - dikz * 2, 0);
    
}


static inline void create_passer_masks(Side side, EvalMasks * masks){
    
    uint64_t fpassf = 0, fpassif = 0;
    uint64_t passers = masks->passers[side];;
    // uint64_t fpass_pushed = side ? passers >> 8 : passers << 8;

    while (passers){
        int index = __builtin_ctzll(passers);
        passers = passers & (passers - 1);

        fpassf |= file_masks[SQ_TO_FILE[index]];
        fpassif |= in_front_file_masks[side][SQ_TO_FILE[index]];
    }
    masks->passers_front[side] = fpassif;
    masks->passers_file[side] = fpassf;
}



static inline int lazy_material_eval(Game * game, Side side, uint64_t eam, int * m_mg, int * m_eg){
    
    uint64_t fpawns = game->pieces[side][PAWN];
    int mob_mg = 0, mob_eg = 0;

    for (int i = 0; i < 32; i++){
        PieceInfo * info = &game->piece_info[i];
        if (info->side != side || !info->alive || info->p == PAWN) continue;
        int pos = info->pos;
        PieceType p = info->p;
        uint64_t m = game->piece_attacks[i];
        uint64_t mob = m & ~eam & ~fpawns;
        int count = __builtin_popcountll(mob);
        mob_mg += MOBILITY_BONUS_MG[p] * count;
        mob_eg += MOBILITY_BONUS_EG[p] * count;
    }
    *m_mg += mob_mg;
    *m_eg += mob_eg;
}


static inline int corrhist_eval(Game * game, Side side){
    const int CORRHIST_GRAIN = 256;
    const int w = 516;
    const int pw = 516;

    int cp = corrhist_p[side][game->piece_key[PAWN] & CORRHIST_MASK] * pw / w;
    int cnp_w = corrhist_nonpawns_w[side][game->nonpawn_key[WHITE] & CORRHIST_MASK] * pw / w;
    int cnp_b = corrhist_nonpawns_b[side][game->nonpawn_key[BLACK] & CORRHIST_MASK] * pw / w;
    
    uint64_t kbn = game->piece_key[KING] ^ game->piece_key[BISHOP] ^ game->piece_key[KNIGHT];

    int ckbn = corrhist_kbn[side][kbn & CORRHIST_MASK] * pw / w;

    uint64_t kqr = game->piece_key[KING] ^ game->piece_key[QUEEN] ^ game->piece_key[ROOK];
    int ckqr = corrhist_kqr[side][kqr & CORRHIST_MASK] * pw / w;

    int c = (cp + cnp_w + cnp_b + ckbn + ckqr) / CORRHIST_GRAIN;
    return c;
}


static inline int compute_mkd(Game * game){
    
    int w_kd = compute_king_danger(game, WHITE, game->atk_kz[WHITE]);
    int b_kd = compute_king_danger(game, BLACK, game->atk_kz[BLACK]);
    int mkd = MAX(w_kd, b_kd);
    return KING_DANGER[(size_t)mkd];
}



/* 
    @brief the giant eval function. uses the pawn hash to help out on the load. used to be in int space but now due to texel complication uses int s. i intend to go back, however. returns based on side to move, but everything is calculated from white as positive
    @param king_threat_value a pointer used for reducing pruning when a king is under heavy threat (wip)
*/

static inline int evaluate(Game * game, Side side, SearchData * search_data, int * threats, int mkd, int depth, int alpha, int beta, bool * lazy, bool pv_node, bool debug){

    EvalDebug dbg;

    EvalMasks masks;
    
    int phase = game->phase;
    int w_mat_mg = 0;
    int w_mat_eg = 0;
    int b_mat_mg = 0;
    int b_mat_eg = 0;
    int sign = 1;
    int mg = 0;
    int eg = 0;


    dbg = (EvalDebug){0};

    int p_mg = 0; int p_eg = 0;
    int king_pawn_shield = 0;
    // search for pawn hash entry
    int  pawn_score = 0;
    PawnHashEntry * entry = NULL;
    int w_ks_mg = 0, w_ks_eg = 0;
    int b_ks_mg = 0, b_ks_eg = 0;
    int w_aks_mg = 0, w_aks_eg = 0;
    int b_aks_mg = 0, b_aks_eg = 0;
    int ktv = 0;
    entry = search_for_pawn_hash_entry(game, game->piece_key[PAWN]);
    search_data->pawn_hash_probes += 1;
    if (entry){
        search_data->pawn_hash_hits += 1;
        mg += entry->mg_score;
        eg += entry->eg_score;
    } else {
        
        int w_ps_mg = 0, w_ps_eg = 0;
        int b_ps_mg = 0, b_ps_eg = 0;
        masks.pa[WHITE] = 0;
        masks.pa[BLACK] = 0;
        masks.passers[WHITE] = 0;
        masks.passers[BLACK] = 0;
        masks.passers_front[WHITE] = 0;
        masks.passers_front[BLACK] = 0;
        masks.passers_file[WHITE] = 0;
        masks.passers_file[BLACK] = 0;
        evaluate_pawn_structure(game, WHITE, &w_ps_mg, &w_ps_eg, &masks); 
        evaluate_pawn_structure(game, BLACK, &b_ps_mg, &b_ps_eg, &masks);
        int b_sp = 0;
        int w_sp = 0;
        create_passer_masks(WHITE, &masks);
        create_passer_masks(BLACK, &masks);
        // evaluate_pawn smargin += 740;_space(game,&w_sp, &b_sp);
        int pawn_mg = w_ps_mg - b_ps_mg + w_sp - b_sp;
        int pawn_eg = w_ps_eg - b_ps_eg + w_sp - b_sp;
        create_new_pawn_hash_entry(game, game->piece_key[PAWN], pawn_mg, pawn_eg, &masks);
        mg += pawn_mg;
        eg += pawn_eg;
    }


    int psqt_mg = game->psqt_evaluation_mg[WHITE] - game->psqt_evaluation_mg[BLACK];
    int psqt_eg = game->psqt_evaluation_eg[WHITE] - game->psqt_evaluation_eg[BLACK];
    mg += psqt_mg;
    eg += psqt_eg;

    // if (game->phase > 8){
    //     if (game->side_to_move == WHITE){
    //         mg += 10; eg += 10;
    //     } else {
    //         mg -= 10; eg -= 10;
    //     }
        
    // }

    // printf("MAT: %d\n", mkd);
    // if (abs(mkd) > search_data->highest_mat_reached ){
    //     int am = abs(mkd);
    //     printf("MAT: %d\n", mkd);
    //     // printf("STM: %d\n", game->side_to_move);
    //     // print_game_board(game);
    //     search_data->highest_mat_reached = am;
    // }
    
    // mkd *= 2;
    int smargin = 200 + mkd;
    if (masks.passers[WHITE] & rank_masks[7] || masks.passers[WHITE] & rank_masks[6] || masks.passers[BLACK] & rank_masks[1] || masks.passers[BLACK] & rank_masks[0]) smargin += 740; 
    int corrhist = corrhist_eval(game, side);
    int pe = ((eg * (MAX_PHASE - phase)) + (mg * phase)) / MAX_PHASE;

    if (side == WHITE){
    
        pe = corrhist + pe;
    } else {
        pe = corrhist - pe;
    
    }

    if (pe >= beta + smargin || pe <= alpha - smargin) {
        search_data->lazy_cutoffs_s1 += 1;
        *lazy = true;
        return pe;
    }

    
    int w_ktv = 0, b_ktv = 0;
    int w_threats_mg = 0, w_threats_eg = 0, b_threats_mg = 0, b_threats_eg = 0;
    int w_mob_mg = 0, w_mob_eg = 0, b_mob_mg = 0, b_mob_eg = 0;

    if (entry){
        masks.pa[WHITE] = entry->w_atk;
        masks.pa[BLACK] = entry->b_atk;
        masks.passers[WHITE] = entry->w_passers;
        masks.passers[BLACK] = entry->b_passers;
        masks.passers_front[WHITE] = entry->w_passers_front;
        masks.passers_front[BLACK] = entry->b_passers_front;
        masks.passers_file[WHITE] = entry->w_passers_file;
        masks.passers_file[BLACK] = entry->b_passers_file;
        
    }
    masks.wsq[WHITE] = masks.pa[BLACK] & ~masks.pa[WHITE];
    masks.wsq[BLACK] = masks.pa[WHITE] & ~masks.pa[BLACK];
    masks.out[WHITE] = masks.wsq[BLACK] & center_squares_mask;
    masks.out[BLACK] = masks.wsq[WHITE] & center_squares_mask;
    masks.am[WHITE] = 0;
    masks.am[BLACK] = 0;
    masks.datk[BLACK] = 0;
    masks.datk[WHITE] = 0;
    masks.tatk[BLACK] = 0;
    masks.tatk[WHITE] = 0;
    masks.kz_atk[BLACK] = 0;
    masks.kz_atk[WHITE] = 0;
    masks.am_nk[BLACK] = 0;
    masks.am_nk[WHITE] = 0;
    masks.am_np[BLACK] = 0;
    masks.am_np[WHITE] = 0;
    masks.am_p[BLACK][PAWN] = 0;
    masks.am_p[WHITE][PAWN] = 0;
    masks.am_p[BLACK][KNIGHT] = 0;
    masks.am_p[WHITE][KNIGHT] = 0;
    masks.am_p[BLACK][BISHOP] = 0;
    masks.am_p[WHITE][BISHOP] = 0;
    masks.am_p[BLACK][QUEEN] = 0;
    masks.am_p[WHITE][QUEEN] = 0;
    masks.am_p[BLACK][KING] = 0;
    masks.am_p[WHITE][KING] = 0;
    masks.am_mtm[BLACK] = 0;
    masks.am_mtm[WHITE] = 0;
    masks.am_mtr[BLACK] = 0;
    masks.am_mtr[WHITE] = 0;

    generate_attack_mask_and_eval_mobility(game, WHITE, &masks);
    generate_attack_mask_and_eval_mobility(game, BLACK, &masks);

    uint64_t wp = game->board_pieces[WHITE];
    uint64_t bp = game->board_pieces[BLACK];

    // masks.tdp[WHITE] = (masks.am[BLACK] & ~masks.am[WHITE]) | (masks.datk[BLACK] & ~masks.datk[WHITE] & ~masks.pa[WHITE]) | (masks.tatk[BLACK] & ~masks.tatk[WHITE] & ~masks.pa[WHITE]);
    // masks.tdp[BLACK] = (masks.am[WHITE] & ~masks.am[BLACK]) | (masks.datk[WHITE] & ~masks.datk[BLACK] & ~masks.pa[BLACK]) | (masks.tatk[WHITE] & ~masks.tatk[BLACK] & ~masks.pa[BLACK]);
    masks.tdp[WHITE] = (masks.am[BLACK] & ~masks.am[WHITE]) | (masks.datk[BLACK] & ~masks.datk[WHITE] & ~masks.am_p[BLACK][KING] & ~masks.am_p[BLACK][QUEEN]);
    masks.tdp[BLACK] = (masks.am[WHITE] & ~masks.am[BLACK]) | (masks.datk[WHITE] & ~masks.datk[BLACK] & ~masks.am_p[WHITE][KING] & ~masks.am_p[WHITE][QUEEN]);


    // masks.am_empty = ~(masks.am[WHITE] | masks.am[BLACK]);
    // masks.see_pos[WHITE] =
    //       ((masks.pa[WHITE] & masks.am_np[BLACK])
    //     | (masks.am_np[WHITE] & masks.am_mtm[BLACK])
    //     | (masks.am_mtm[WHITE] & masks.am_mtr[BLACK])
    //     | (masks.am[BLACK] & ~masks.am[WHITE]))
    //     & masks.tdp[BLACK];
    // masks.see_pos[BLACK] =
    //       ((masks.pa[BLACK] & masks.am_np[WHITE])
    //     | (masks.am_np[BLACK] & masks.am_mtm[WHITE])
    //     | (masks.am_mtm[BLACK] & masks.am_mtr[WHITE])
    //     | (masks.am[BLACK] & ~masks.am[WHITE]))
    //     & masks.tdp[WHITE];

    evaluate_material(game, WHITE, &masks, &w_mat_mg, &w_mat_eg, &w_threats_mg, &w_threats_eg, &w_mob_mg, &w_mob_eg);
    evaluate_material(game, BLACK, &masks, &b_mat_mg, &b_mat_eg, &b_threats_mg, &b_threats_eg, &b_mob_mg, &b_mob_eg);

    // int p_white = evaluate_pins(game, WHITE, game->k_sq[WHITE], &masks);
    // int p_black = evaluate_pins(game, BLACK, game->k_sq[BLACK], &masks);
    
    int t_mg = w_threats_mg + b_threats_mg;
    int t_eg = w_threats_eg + b_threats_eg;

    int t = ((t_eg * (MAX_PHASE - phase)) + (t_mg * phase)) / MAX_PHASE;

    int hthreats = t;
    *threats = t;
    int mob_mg = w_mob_mg - b_mob_mg;
    int mob_eg = w_mob_eg - b_mob_eg;
    int mat_mg = w_mat_mg - b_mat_mg;
    int mat_eg = w_mat_eg - b_mat_eg;
    
    mg += w_mat_mg - b_mat_mg;
    eg += w_mat_eg - b_mat_eg;
    mg += mob_mg;
    eg += mob_eg;
    int threats_mg = THREAT_SCORE[MAX(w_threats_mg, 0)] - THREAT_SCORE[MAX(b_threats_mg, 0)];
    int threats_eg = THREAT_SCORE[MAX(w_threats_eg, 0)] - THREAT_SCORE[MAX(b_threats_eg, 0)];
    // int threats_mg = w_threats_mg - b_threats_mg;
    // int threats_eg = w_threats_eg - b_threats_eg;
    mg += threats_mg;
    eg += threats_eg;
    // mg += p_white - p_black;
    // eg += p_white - p_black;
    // int mat_scaled_mg = w_mat_mg - b_mat_mg + mob_mg + w_threats_mg - b_threats_mg ;
    // int mat_scaled_eg = w_mat_eg - b_mat_eg + mob_eg + w_threats_eg - b_threats_eg;
    // int mat_scaled = ((mat_scaled_eg * (MAX_PHASE - phase)) + (mat_scaled_mg * phase)) / MAX_PHASE;
    // if (abs(mat_scaled) > search_data->highest_mat_reached ){
    //     int am = abs(mat_scaled);
    //     printf("MAT: %d\n", am);
    //     if (abs(mat_scaled) > 800){
    //         printf("MAT: %d MOB: %d THREATS: %d\n", w_mat_eg - b_mat_eg, mob_eg, w_threats_eg - b_threats_mg);
    //         print_game_board(game);
    //     }
    //     search_data->highest_mat_reached = am;
    // }

    int rmargin = 40 + mkd;
    int re = ((eg * (MAX_PHASE - phase)) + (mg * phase)) / MAX_PHASE;


    if (side == WHITE){
    
        re = corrhist + re;
    } else {
        re = corrhist - re;
    
    }

    if (re >= beta + rmargin || re <= alpha - rmargin) {
        search_data->lazy_cutoffs_s2 += 1;
        *lazy = true;
        return re;
    }

    
    // int w_mr_mg = 0, w_mr_eg = 0, b_mr_mg = 0, b_mr_eg = 0;
    
    evaluate_king_threat(game, WHITE, &masks, &b_ktv);
    evaluate_king_threat(game, BLACK, &masks, &w_ktv);
    // int mat_scaled_mg = w_ktv- b_ktv;
    // int mat_scaled_eg = w_ktv- b_ktv;
    // int mat_scaled = ((mat_scaled_eg * (MAX_PHASE - phase)) + (mat_scaled_mg * phase)) / MAX_PHASE;
    // if (abs(mat_scaled) > search_data->highest_mat_reached ){
    //     int am = abs(mat_scaled);
    //     printf("MAT: %d\n", mat_scaled);
    //     printf("STM: %d\n", game->side_to_move);
    //     print_game_board(game);
    //     search_data->highest_mat_reached = am;
    // }

    mg += w_ktv - b_ktv;
    eg += w_ktv - b_ktv;

    
    
    int w_eg_king = 0, b_eg_king = 0;

    int w_dev = 0, b_dev = 0;
    int e = ((eg * (MAX_PHASE - phase)) + (mg * phase)) / MAX_PHASE;


    
    const int MAX_DEPTH = 32;
    if (side == WHITE){
        
        int ee = corrhist + e;
        return ee;
    } else {
        int ee = corrhist - e;
        return ee;
        
    }
}

static inline int fast_eval(Game * game, Side side, SearchData * search_data){
    
    int mg = 0, eg = 0;
    PawnHashEntry * entry = search_for_pawn_hash_entry(game, game->piece_key[PAWN]);
    search_data->pawn_hash_probes += 1;
    EvalMasks masks;
    if (entry){
    search_data->pawn_hash_hits += 1;
    mg += entry->mg_score;
    eg += entry->eg_score;
    } else {
        
        int w_ps_mg = 0, w_ps_eg = 0;
        int b_ps_mg = 0, b_ps_eg = 0;
        masks.pa[WHITE] = 0;
        masks.pa[BLACK] = 0;
        masks.passers[WHITE] = 0;
        masks.passers[BLACK] = 0;
        masks.passers_front[WHITE] = 0;
        masks.passers_front[BLACK] = 0;
        masks.passers_file[WHITE] = 0;
        masks.passers_file[BLACK] = 0;
        evaluate_pawn_structure(game, WHITE, &w_ps_mg, &w_ps_eg, &masks); 
        evaluate_pawn_structure(game, BLACK, &b_ps_mg, &b_ps_eg, &masks);
        int b_sp = 0;
        int w_sp = 0;
        create_passer_masks(WHITE, &masks);
        create_passer_masks(BLACK, &masks);
        // evaluate_pawn_space(game,&w_sp, &b_sp);
        int pawn_mg = w_ps_mg - b_ps_mg + w_sp - b_sp;
        int pawn_eg = w_ps_eg - b_ps_eg + w_sp - b_sp;
        create_new_pawn_hash_entry(game, game->piece_key[PAWN], pawn_mg, pawn_eg, &masks);
        // dbg.pawn.w = w_ps_mg * phase + w_ps_eg * eg_phase + w_sp;
        // dbg.pawn.b = b_ps_mg * phase + b_ps_eg * eg_phase + b_sp;
        mg += pawn_mg;
        eg += pawn_eg;
    }

    // uint64_t w_atk_mask = 0, b_atk_mask = 0;
    // uint64_t w_atk_no_king = 0, b_atk_no_king = 0;
    // uint64_t w_datk_mask = 0, b_datk_mask = 0;
    // generate_attack_mask_and_eval_mobility(game, WHITE, &w_atk_mask, &w_datk_mask, &w_atk_no_king);
    // generate_attack_mask_and_eval_mobility(game, BLACK, &b_atk_mask, &b_datk_mask, &b_atk_no_king);

    // int w_m_mg = 0, w_m_eg = 0;
    // int b_m_mg = 0, b_m_eg = 0;
    // lazy_material_eval(game, WHITE, b_atk_mask, &w_m_mg, &w_m_eg);
    // lazy_material_eval(game, BLACK, w_atk_mask, &b_m_mg, &b_m_eg);

    mg += game->psqt_evaluation_mg[WHITE] - game->psqt_evaluation_mg[BLACK];
    eg += game->psqt_evaluation_eg[WHITE] - game->psqt_evaluation_eg[BLACK];
    mg += game->atk_kz[WHITE] - game->atk_kz[BLACK];
    eg += game->atk_kz[WHITE] - game->atk_kz[BLACK];

    // mg += w_m_mg - b_m_mg;
    // eg += w_m_eg - b_m_eg;

    int e = ((eg * (MAX_PHASE - game->phase)) + (mg * game->phase)) / MAX_PHASE;
    
    const int MAX_DEPTH = 32;
    if (side == WHITE){
        // return e;
        
        int eval = corrhist_eval(game, side) + e;
        // return (MAX_DEPTH - MIN(depth, 0)) * eval / MAX_DEPTH;
        return eval;
    } else {
        // return -e;
        int eval = corrhist_eval(game, side) - e;
        // return (MAX_DEPTH - MIN(depth, 0)) * eval / MAX_DEPTH;
        return eval;
    }

}


static inline bool is_mate(int score){
    return score < -MATE_SCORE + 500 || score > MATE_SCORE - 500;
}
static inline bool is_safe(Game * game, Move * move){
    if (move->piece == KING){
        return true;
    } else if ((move->type == CAPTURE || move->promotion_capture) && PVAL[move->capture_piece] >= PVAL[move->piece]){
        return true;
    } else if (move->type == PROMOTION && move->promotion_type != QUEEN){
        return false;
    } else {
        return see(game, move) >= 0;
    }
}




#endif
