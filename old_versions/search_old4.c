#include "game.h"
#include "math.h"
#include "move_generation.h"
#include "types.h"
#include "utils.h"
#include "magic.h"
#include "zobrist.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "sort.h"
#include "eval.h"
#include "search.h"
#include "./tables/tbconfig.h"
#include "./tables/tbprobe.h"

uint64_t rook_table[ROOK_TABLE_SIZE];
uint64_t bishop_table[BISHOP_TABLE_SIZE];

Move killer_moves[64][2];
int conthist[COLOR_MAX * PIECE_TYPES * 64][COLOR_MAX * PIECE_TYPES * 64];
int history[COLOR_MAX][PIECE_TYPES][64][64];
int capture_history[PIECE_TYPES][BOARD_MAX][PIECE_TYPES];

int corrhist_p[COLOR_MAX][CORRHIST_SIZE];
int corrhist_nonpawns_w[COLOR_MAX][CORRHIST_SIZE];
int corrhist_nonpawns_b[COLOR_MAX][CORRHIST_SIZE];
int corrhist_kbn[COLOR_MAX][CORRHIST_SIZE];
int corrhist_kqr[COLOR_MAX][CORRHIST_SIZE];
TTBucket tt[TT_SIZE];
PawnHashEntry pawn_hash_table[PAWN_SIZE];
EvalEntry eval_table[EVAL_SIZE];

int LMR_PV[64][64];
int LMR_NON_PV[64][64];
int LMP[64];
int FP[64][64];

void init_search_tables(){

    // LMR
    for (int i = 0; i < 64; i++){
        for (int m = 0; m < 64; m++){
            LMR_PV[i][m] = (log(i) * log(m)) * 0.32 + 0.1;
            LMR_NON_PV[i][m] = (log(i) * log(m)) * 0.51 + 0.6;
        }
    }

    // LMP
    
    for (int i = 0; i < 64; i++){
        LMP[i] = 8 + pow(i, 2);
    }

    // FUTILITY
    
    for (int i = 0; i < 64; i++){
        for (int m = 0; m < 64; m++){
            // FP[i][m] = 120 * (log((double)(i * i) / 2) / log(2.0) + 1) - m * 5 + 80;        
            // FP[i][m] = 175 - 50   
        }
    }

    
}

int quiesce(NodeType nt, Game * game, int alpha, int beta, SearchData * search_data, int16_t depth, int16_t ply, int q_ply, ThreadData * td){
    
    RootData * root = td->root;

    if (search_data->stop) {
        return 0;
    } else {
        bool stop = atomic_load(&root->stop);
        if (stop) {
            search_data->stop = true;
            return 0;
        }
    }

    search_data->pv_length[ply] = 0;
    bool pv_node = nt == PV;
    int old_alpha = alpha;

    Move best_move;
    PackedMove tt_move;
    bool has_best_move = false;

    assert(alpha < beta);
    assert(pv_node || alpha == beta - 1);
    search_data->tt_probes += 1;

    bool is_in_check = false;

    if (in_check(game, game->side_to_move)) is_in_check = true;
    int eval = -INT_MAX;
    int best_score = -INT_MAX;
    int futility_stand = -INT_MAX;
    int threats = 0;
    if (ply >= 63){
        return 0;
    }
    int mkd = compute_mkd(game);
    
    TTEntry * tt_entry = search_for_tt_entry(game, game->key);
    if (tt_entry ){
        search_data->tt_hits += 1;


        if (tt_entry->has_best_move && !search_data->disable_writes){

            tt_move = tt_entry->move;
            has_best_move = true;

        }
        if (!pv_node ){
    
            int adjusted_score = adjust_mate_score_from_tt(tt_entry->score, tt_entry->ply, ply);
            if (tt_entry->type == EXACT) return adjusted_score;
            if (tt_entry->type == UPPER && adjusted_score <= alpha) return adjusted_score;
            if (tt_entry->type == LOWER && adjusted_score >= beta) return adjusted_score;
        }
            

    }
    if (!is_in_check){
        EvalEntry * eval_entry = NULL;

        eval_entry = search_for_eval(game, game->key);
        if (eval_entry){
            if (eval_entry->side != game->side_to_move){
                eval= -eval_entry->eval;
            } else {
                eval = eval_entry->eval;
            }
            threats = eval_entry->threats;
        } else {
            bool lazy = false;
            eval = evaluate(game, game->side_to_move, search_data, &threats, mkd, depth,alpha, beta, &lazy, pv_node, false);
            if (!lazy){
                hash_eval(game, game->key, eval, threats, mkd, game->side_to_move);
    
            }
        }

        best_score = eval;
        
        if (best_score >= beta){
            // create_new_tt_entry(game, game->key, best_score, LOWER, depth, ply, NULL);
            return best_score;
        } 

        if (best_score > alpha){
            alpha = best_score;
        }
        futility_stand = eval + 260;

    }

    Move * new_best = NULL;
    TTType hash_flag = UPPER;
    Move move_list[200];
    int ordering[200];
    int current_index = 0;
    int move_count = 0;
    Undo undo;
    const PickType Q_STAGES[3] = {
        PICK_TT_MOVE,
        // bad cap is just captures
        PICK_BAD_CAP,
        PICK_QUIET,
    };
    int s_count = 0;
    PickType stage = Q_STAGES[0];
    if (is_in_check){
        
     generate_moves(game, game->side_to_move, move_list, &move_count);
    } else {
        
     generate_non_quiet_moves(game, game->side_to_move, move_list, &move_count, depth < -1 ? false : true);
        // generate_non_quiet_moves(game, game->side_to_move, move_list, &move_count, false);
    }
    init_ordering(ordering, move_count);
    // sort_qmoves(game, move_list, move_count, ordering, tt_move, has_best_move, ply);
    int legal_moves = 0;
    bool exhausted = false;
    for (int i = 0; i < move_count; i++){
        if (search_data->stop) {
            return 0;
        } else {
            bool stop = atomic_load(&root->stop);
            if (stop) {
                search_data->stop = true;
                return 0;
            }
        }
        bool is_tt_move = false;

        // implementing a move picker in qsearch has not proven to be an elo increase. for this reason evasions are probably inefficiently unordered, which is why we only let checks in at depth 0
        // Move * move = pick_next_qmove(move_list, move_count, ordering, &current_index);

        Move * move = pick_next_move(game, move_list, ordering, &current_index, move_count, tt_move, has_best_move, ply, stage, true);
        if (move && stage == PICK_TT_MOVE) is_tt_move = true;
        while (!move){
            s_count += 1;
            if (s_count >= 3) {
                exhausted = true; break;
            }
            stage = Q_STAGES[s_count];
            move = pick_next_move(game, move_list, ordering, &current_index, move_count, tt_move, has_best_move, ply, stage, true);
        }
        if (stage == PICK_TT_MOVE) stage += 1;
        if (exhausted) break;

        // assert(move->type == CAPTURE || move->type == EN_PASSANT || move->type == PROMOTION || move->is_checking || is_in_check);
        
        // delta pruning
        // if (!pv_node && !is_in_check && !move->is_checking && !is_tt_move && move->type != PROMOTION) {

        //     int futility = futility_stand + (move->type == EN_PASSANT ? PVAL[PAWN] : PSQT_EG[!game->side_to_move][move->capture_piece][move->end_index]);

        //     if (futility <= alpha){
        //         // int k = compute_mkd(game);
                
        //         // if (k > 200){
        //         //     printf("SCORE: %d MOVE:\n", eval);
        //         //     print_move_full(move);
        //         //     print_game_board(game);
                    
        //         // }
        //         search_data->delta_prunes += 1;
        //         continue;
        //     }

        //     // int see_val = 0;
        //     // if (move->seed){
        //     //     see_val = move->see;
        //     // } else {
        //     //     move->seed = true;
        //     //     see_val = see(game, move);
        //     //     move->see = see_val;
        //     // }
        //     // if (see_val == 0 && futility_stand <= alpha){
        //     //     search_data->delta_prunes += 1;
        //     //     continue;
        //     // }
            
        // }
        // // // see pruning
        // if (!pv_node &&
        //     !is_in_check &&
        //     !move->is_checking &&
        //      // either low king safety or contact checks are exempt from see pruning
        //      // (mkd < 200 ||
        //      //  (bits[move->end_index] & king_moves[game->k_sq[!game->side_to_move]])== 0)) &&
        //     move->type != PROMOTION &&
        //     !is_tt_move){
        //     int see_val = 0;
        //     if (move->seed){
        //         see_val = move->see;
        //     } else {
        //         move->seed = true;
        //         see_val = see(game, move);
        //         move->see = see_val;
        //     }
        //     if (see_val < -100){
        //         search_data->q_see_prunes += 1;
        //         continue;
            
        //     }
        // }

        // // prune non-dangerous checks
        // if (!pv_node && !is_in_check && move->is_checking && !is_tt_move && move->type != PROMOTION && !is_capture(move) && !check_is_dangerous(game, move, futility_stand, beta) && eval + 150 < beta){
        //     continue;
        // }

        if(!make_move(game, move, &undo)){
            undo_move(game, move, &undo);


        } else {
            search_data->qnodes += 1;
            legal_moves += 1;


            bool move_causes_check = in_check(game, game->side_to_move);
            int score = -quiesce(nt, game, -beta, -alpha, search_data, depth - 1, ply + 1, q_ply, td);
            undo_move(game, move, &undo);
            assert(score > -INT_MAX && score < INT_MAX);

            if (score > best_score){
                best_score = score;
                if (score > alpha){
                    if (score >= beta){
                        create_new_tt_entry(game, game->key, score, LOWER, depth, ply, move);
                        return score;
                    }
                    hash_flag = EXACT;
                    new_best = move;
                    store_pv(ply, new_best, search_data);
                    alpha = score;
                }
            }


        }
    }
    if (legal_moves == 0 && is_in_check){
        // if no legal quiet moves, we should check the rest to determine if we are in checkmate

        // create_new_tt_entry(game, game->key, -MATE_SCORE + ply, EXACT, depth, ply, NULL);
        return -MATE_SCORE + ply;
        
    }
    
    assert(best_score != -INT_MAX && best_score != INT_MAX);
    // create_new_tt_entry(game, game->key, best_score, best_score > old_alpha ? EXACT : UPPER, depth, ply, new_best);
    create_new_tt_entry(game, game->key, best_score, hash_flag, depth, ply, new_best);
    return best_score;
    
}


int search(NodeType nt, Game * game, int  alpha, int  beta, int16_t depth, SearchData * search_data, int16_t ply, int extensions, bool skip_null_move, Move * excluded_move, Undo * last_undo, ThreadData * td){
    
    RootData * root = td->root;

    if (search_data->stop) {
        return 0;
    } else {
        bool stop = atomic_load(&root->stop);
        if (stop) {
            search_data->stop = true;
            return 0;
        }
    }
    
    // if (search_data->enable_time){
        double elapsed_time = (double)(now_seconds() - search_data->start_time); 
        if (elapsed_time >= search_data->max_time) {
            // printf("ELAPSED: %f MAX: %f", elapsed_time, search_data->max_time);
            atomic_store(&root->stop, true);
            search_data->stop = true;
            return 0;
        }
    // }
    Move refuter;
    bool was_refuted = false;
    int old_alpha = alpha;
    PackedMove tt_move;
    bool has_best_move = false;
    bool found_entry = false;
    bool is_in_check = in_check(game, game->side_to_move);
    bool queen_is_attacked = false;

    // bool pv_node = depth > 0 && alpha != beta - 1;
    bool pv_node = nt == PV;
    bool cut_node = false;
    TTType found_flag = 0;
    bool use_tt = true;
    int tt_depth = depth;
    TTEntry * tt_entry = NULL;
    search_data->pv_length[ply] = 0;

    assert(alpha < beta);
    assert(pv_node || alpha == beta - 1);

    // TODO detect other draws. i have the stuff already
    if (three_fold_repetition(game, game->key)){
        return 0;
    }

    // if 3-5 men, then you need 7-9 more for 12 angry men
    // we probe the tables here
    int pc_count = __builtin_popcountll(game->board_pieces[BOTH]);
    bool no_castling = game->castle_flags[0][0] == 0 && game->castle_flags[0][1] == 0 && game->castle_flags[1][0] == 0 && game->castle_flags[1][1] == 0;
    int stm = 0;
    if (game->side_to_move == WHITE) stm = 1;
    if (pc_count <= TB_LARGEST && no_castling && game->en_passant_index == -1){
        int s = tb_probe_wdl(
            game->board_pieces[WHITE], 
            game->board_pieces[BLACK], 
            game->pieces[WHITE][KING] | game->pieces[BLACK][KING], 
            game->pieces[WHITE][QUEEN] | game->pieces[BLACK][QUEEN], 
            game->pieces[WHITE][ROOK] | game->pieces[BLACK][ROOK], 
            game->pieces[WHITE][BISHOP] | game->pieces[BLACK][BISHOP], 
            game->pieces[WHITE][KNIGHT] | game->pieces[BLACK][KNIGHT], 
            game->pieces[WHITE][PAWN] | game->pieces[BLACK][PAWN], 
            game->fifty_move_clock, 0, 0, stm);
        if (s){
            if (s == TB_WIN){
                return MATE_SCORE - ply;
            } else if (s == TB_LOSS){
                return -MATE_SCORE + ply;
            }
        }
    }



    

    int w_ktv = 0, b_ktv = 0;

    // qsearch
    if (depth <= 0){

        return quiesce(nt , game, alpha, beta, search_data, 0, ply, ply, td);
    }

    // probe tt
    bool depth_gate = false;
    if (use_tt){
        
        search_data->tt_probes += 1;
        tt_entry = search_for_tt_entry(game, game->key);

    
        if (tt_entry){
            search_data->tt_hits += 1;


            if (tt_entry->has_best_move && !search_data->disable_writes){

                // unpack_move(tt_entry->move32, &best_move);
                tt_move = tt_entry->move;
                has_best_move = true;

            }
            if (tt_entry->depth >= depth && !pv_node && excluded_move == NULL){
        
                int adjusted_score = adjust_mate_score_from_tt(tt_entry->score, tt_entry->ply, ply);
                if (tt_entry->type == EXACT) return adjusted_score;
                if (tt_entry->type == UPPER && adjusted_score <= alpha) return adjusted_score;
                if (tt_entry->type == LOWER && adjusted_score >= beta) return adjusted_score;
            }
                

        }
    }
        


    // eval
    int stand= 0;
    int threats = 0;
    EvalEntry * eval_entry = NULL;
    int mkd = compute_mkd(game);

    // probe eval hash
    eval_entry = search_for_eval(game, game->key);
    if (eval_entry){
        if (eval_entry->side != game->side_to_move){
            stand = -eval_entry->eval;
        } else {
            stand = eval_entry->eval;
        }
        threats = eval_entry->threats;
    } else {
        bool lazy = false;
        stand = evaluate(game, game->side_to_move, search_data, &threats, mkd, depth, alpha, beta, &lazy, pv_node, false);
        if (!lazy){
            hash_eval(game, game->key, stand, threats, mkd, game->side_to_move);
        }
    }
    // bail out
    if (ply >= 63){
        return stand;
    }

    // find if we are improving from 2 moves ago
    bool improving = false;
    if (!is_in_check){
        if (game->history_count - 2 >= 0){
            
            int last_eval= 0;
            int lktv = 0;
            EvalEntry * last_eval_entry = NULL;

            last_eval_entry = search_for_eval(game, game->key_history[game->history_count - 2]);
            if (last_eval_entry){
                if (last_eval_entry->side != game->side_to_move){
                    last_eval = -last_eval_entry->eval;
                } else {
                    last_eval = last_eval_entry->eval;
                }
                if (stand > last_eval){
                    improving = true;
                }
            }
        }
    }

    // reverse futility pruning
    // the reverse of futility pruning. if our eval - margin * depth still clears beta, we can early out on the same premise as NMP.
    int rfp_depth = 5;
    if (!pv_node && depth <= rfp_depth && !is_in_check && excluded_move == NULL && stand < 2000){
        int eval = stand - (depth * 50 + 10 + 20 * improving);
        // int eval = stand - futility_margin(improving, depth);
        if (eval >= beta) {
            search_data->rfp += 1;
            return eval;
        }
    }

    // this idea is credited to yukari, basically has a verification that the score is still under alpha after qsearch. works very well and lets us prune more aggressively
    if (!pv_node && depth <= 3 && !is_in_check && stand + 47 * depth <= alpha && !is_mate(beta) && !is_mate(alpha) && excluded_move == NULL ){
        int score = quiesce(NON_PV, game, alpha, alpha + 1, search_data, depth, ply, ply, td);
        if (score <= alpha){
            search_data->razoring += 1;
            return score;
       }
    }
    // razoring
    // similar to futility pruning but we drop into qsearch
    // if (!pv_node && depth <= 1 && !is_in_check && stand <= alpha - 370 && !is_mate(beta) && !is_mate(alpha) && excluded_move == NULL && !has_best_move){
    //     search_data->razoring += 1;
    //     return quiesce(NON_PV, game, alpha, beta, search_data, 0, ply, ply, td);
    // }
    

    int score = -INT_MAX;
    bool allow_nmp = true;



    // null move pruning
    // if we don't make a move and we still clear beta, we can return lower bound, since that should never be the case (except for in endgame, which is why it's gated by phase)
    if (depth >= 2 && !is_in_check && game->phase > 4 && !pv_node && !is_mate(beta) && !is_mate(alpha) && !is_mate(stand) && stand >= beta && excluded_move == NULL && !skip_null_move) {
        int r = 0;
        if (depth > 10){
            r = 4;
        } else if (depth > 6){
            r = 3;
        } else {
            r = 2;
        }
        // r += MAX((stand - beta) / 200, 0);
        // r += improving;
        // credit: yukari

        if (game->en_passant_index != -1){
            if (pawn_captures[!game->side_to_move][game->en_passant_index] & game->pieces[game->side_to_move][PAWN]) {

                game->key ^= get_en_passant_random(game->en_passant_index);
            }

        }
        game->side_to_move = !game->side_to_move;
        game->key ^= get_turn_random();
        int old_en_passant_index = game->en_passant_index;
        game->en_passant_index = -1;

        Undo undo; undo.was_capture = false;
        
        int null_score = -INT_MAX;
        null_score = -search(NON_PV, game, -beta, -beta + 1, depth-r-1, search_data, ply + 1, extensions, true, NULL, &undo, td);
        // null_score = -search(NON_PV, game, -beta, -alpha, depth-r-1, search_data, ply + 1, extensions, false, NULL, &undo, td);
        
        game->side_to_move = !game->side_to_move;
        game->key ^= get_turn_random();
        game->en_passant_index = old_en_passant_index;

        if (game->en_passant_index != -1){
            if (pawn_captures[!game->side_to_move][game->en_passant_index] & game->pieces[game->side_to_move][PAWN]) {

                game->key ^= get_en_passant_random(game->en_passant_index);
            }

        }
        if (null_score >= beta){
            search_data->null_prunes += 1;
            return null_score;
        }
    }
    
    Move move_list[200];
    int ordering[200];
    int current_index = 0;
    int move_count = 0;
    PickType stage = PICK_TT_MOVE;
    generate_moves(game, game->side_to_move, move_list, &move_count);
    init_ordering(ordering, move_count);
    Undo undo;

    // probcut
    // if (depth >= 6 && !pv_node && game->phase > 4 && !is_mate(beta) && excluded_move == NULL){
        
    //     int rbeta = beta + 270 - 50 * improving;
    //     bool exhausted = false;
        
    //     int lm = 0;
    //     while (!exhausted || lm > 8 || stage >= PICK_QUIET){
    //         Move * move = pick_next_move(game, move_list, ordering, &current_index, move_count, tt_move, has_best_move, ply, stage, false);
    //         while (!move){
    //             stage += 1;
    //             if (stage > PICK_QUIET) {
    //                 exhausted = true;
    //                 break;
    //             }
    //             move = pick_next_move(game, move_list, ordering, &current_index, move_count, tt_move, has_best_move, ply, stage, false);
    //         }
    //         if (stage == PICK_TT_MOVE) stage += 1;
    //         if (exhausted) break;


    //         if (!make_move(game, move, &undo)){
    //             undo_move(game, move, &undo);
    //         } else {
    //             lm++;
    //             int q = -quiesce(NON_PV, game, -rbeta, -rbeta+1, search_data, 0, ply, ply, td);
    //             if (q >= rbeta){
                    
    //                 int v = -search(NON_PV, game, -rbeta, -rbeta+1, depth - 3, search_data, ply, extensions, true, NULL, &undo, td);
    //                 if (v >= rbeta){
    //                     undo_move(game, move, &undo);
    //                     return v;
    //                 }
    //             }
    //             undo_move(game, move, &undo);
    //         }
            
    //     }
    // }


    // internal iterative deepening
    // if we don't have a tt move, do a low depth search to find one
    if (depth >= 8 && !has_best_move) {

        search(nt, game, alpha, beta, depth - 7, search_data, ply, extensions, true, NULL, last_undo, td);

        TTEntry * new_entry = search_for_tt_entry(game, game->key);
        if (new_entry){
            if (new_entry->has_best_move){
                search_data->successful_iids += 1;
                has_best_move = true;
                tt_move = new_entry->move;
            }
        }
    }

    
    

    // cache pawn hash for passed pawn ext
    PawnHashEntry * phash = search_for_pawn_hash_entry(game, game->key);

        
    current_index = 0;
    stage = PICK_TT_MOVE;
    TTType hash_type = UPPER;
    Move * new_best = &move_list[0];
    // must be set to a move for corrhist to not segfault :>
    Move * best_move_found = &move_list[0];
    int best_move_found_index = 0;
    int legal_moves = 0;
    bool exhausted = false;
    Move * last_quiet_failed = NULL;
    while (!exhausted){
        if (search_data->stop) {
            return 0;
        } else {
            bool stop = atomic_load(&root->stop);
            if (stop) {
                search_data->stop = true;
                return 0;
            }
        }
        // Move * move = &move_list[i];
        Move * move = pick_next_move(game, move_list, ordering, &current_index, move_count, tt_move, has_best_move, ply, stage, false);
        while (!move){
            stage += 1;
            if (stage > PICK_BAD_CAP) {
                exhausted = true;
                break;
            }
            move = pick_next_move(game, move_list, ordering, &current_index, move_count, tt_move, has_best_move, ply, stage, false);
        }
        if (stage == PICK_TT_MOVE) stage += 1;
        if (exhausted) break;
        if (excluded_move != NULL){
            if (move_is_equal(move, excluded_move)){
                continue;
            }
        }

        // singular extensions
        // check if the tt move is much better than all other moves by excluding it from the search
        int extension = 0;
        int ext_red = 0;
        if (tt_entry && ply > 0 && has_best_move && packed_move_is_equal(move, tt_move) && excluded_move == NULL){
            
            if (depth >= 8 && tt_entry->type == LOWER && !is_mate(tt_entry->score) && tt_entry->depth >= depth - 3){
                int singular_beta = MAX((tt_entry->score - depth * 2), -MATE_SCORE + 1);
                int singular_depth = (depth + 1) / 2;
                int s = search(NON_PV, game, singular_beta - 1, singular_beta, singular_depth, search_data, ply + 1, extensions, true, move, last_undo, td);
                if (s < singular_beta){
                    extension += 1;

                // } else if (s >= singular_beta && singular_beta >= beta){
                //     return singular_beta;
                }
            }
        }


        

        if (!make_move(game, move, &undo)){
            undo_move(game, move, &undo);
        } else {

            // move is legal
            legal_moves += 1;

            bool move_causes_check = in_check(game, game->side_to_move);
            bool move_attacks_queen = false;
            bool recapture = move->end_index == game->recap_square && is_capture(move);

            // detect passed pawn push
            bool is_passed_pawn_push = false;
            if (move->piece == PAWN && phash){
                is_passed_pawn_push = bits[move->start_index] & (phash->w_passers | phash->b_passers);
            }

            // dangerous heuristic
            bool dangerous = move_causes_check || move->type == CASTLE || move->type == PROMOTION || is_passed_pawn_push;

            // compute king danger after move
            int cmkd = compute_mkd(game);

            int reduction = 1;
            // extensions
            int e = 0;
            // if (dangerous && pv_node) e = 1;
            if (move_causes_check){ // extend on pv node checks or see positive checks
                // if (pv_node){
                //     e = 1;
                // } else {
                //     int see_val = 0;
                //     if (move->seed){
                //         see_val = move->see;
                //     } else {
                //         see_val = see(game, move);
                //         move->seed = true;
                //         move->see = see_val;
                //     }
                //     if (see_val >= 0){
                //         e = 1;
                //     }
                // }
                e = 1;
            }
            // if (is_passed_pawn_push && pv_node) e = 1; // passed pawn extension

            // calculate depth after extension
            int ext = MIN(MAX(e + extension, 0), 1);
            int new_depth = depth + ext;
            search_data->extensions += ext;


            // LMR
            if (depth >= 3 && legal_moves >= 4 && !is_capture(move)){

                float r = 0;
                r = LMR_NON_PV[depth][legal_moves];
                    
                // scale based on history, yes this also goes negative
                // r -= (float)move->score / 16324;
                r -= (float)move->score / 48000;
                // reduce less on improving / pv / dangerous moves
                r -= improving;
                r -= pv_node;
                r -= dangerous;

                // -2 because reduction is initialized to 1
                r = MAX(MIN(r, new_depth - 2), 0);
                reduction += (int)(r + 0.5f);
            }
            // precalculate depth - 1
            int pred_depth = MAX(new_depth - reduction, 1);


            // Move Pruning
            if (move->type != PROMOTION && legal_moves > 1 && !dangerous && !is_mate(score) && !is_capture(move) && !pv_node){


                // late move pruning, margin scales with depth
                // formula from yukari
                int lmp = lmp_margin(improving, depth);
                if (depth <= 3 && legal_moves > lmp && !is_capture(move)){
                    search_data->late_move_prunes += 1;
                    undo_move(game, move, &undo);
                    continue;
                }

                if (pred_depth <= 3){
                    
                     if (stand + 200 + 220 * pred_depth + ((float)move->score / 48000) * 50 < alpha){
                         search_data->futility_prunes += 1;
                         undo_move(game, move, &undo);
                         continue;
                     }

                }

                // see pruning
                // discards moves if they lose statically, margin scales with depth
                if (depth <= 3){
            
                    const int see_prune_quiet_margin = -40;
                    const int see_prune_nonquiet_margin = 80;
                    int see_val = 0;
                    if (!move->seed){
                        move->see = see(game, move);
                        see_val = move->see;
                        move->seed = true;
                    } else {
                        see_val = move->see;
                    }
                    // if (move_is_quiet(game, move)){
                        if (see_val < depth * see_prune_quiet_margin){
                            search_data->see_prunes += 1;
                            undo_move(game, move, &undo);
                            continue;
                        }
                    // } else {
                
                        // if (see_val < -depth * depth * see_prune_nonquiet_margin){
                        //     search_data->see_prunes += 1;
                        //     undo_move(game, move, &undo);
                        //     continue;
                        // }
                    // }
                }


                
            }

            bool extend = false;



            
            search_data->node_count += 1;


            search_data->reductions += reduction;

            reduction += ext_red;
            // if (extension){
            //     reduction = 0;
            // }

            int current_score = -INT_MAX;

            bool lmr_research = false;
                
            // lmr search
            if (reduction > 1){
                current_score = -search(NON_PV, game, -(alpha+1), -alpha , new_depth - reduction, search_data, ply + 1, extensions + extension, false, NULL, &undo, td);
                if (current_score > alpha) lmr_research = true;
            }

            // full depth search (for lmr research, non pv nodes without lmr, or pv nodes past move one)
            if ((reduction == 1 && (!pv_node || legal_moves > 1)) || lmr_research){
                current_score = -search(NON_PV, game, -(alpha+1), -alpha , new_depth - 1, search_data, ply + 1, extensions + extension, false, NULL, &undo, td);
            }

            // pv search / research
            if (pv_node && (legal_moves == 1 || current_score > alpha)){
                current_score = -search(PV, game, -beta, -alpha , new_depth - 1, search_data, ply + 1, extensions + extension, false, NULL, &undo, td);
            }

                
            if (current_score == INT_MAX || current_score == -INT_MAX){
                printf("CURRENT SCORE INT MAX\n");
                fflush(stdout);
            }
            assert(current_score != -INT_MAX && current_score != INT_MAX);
            undo_move(game, move, &undo);

            if (search_data->stop) {
                return 0;
            } else {
                bool stop = atomic_load(&root->stop);
                if (stop) {
                    search_data->stop = true;
                    return 0;
                }
            }

            if (current_score > score){
                score = current_score;
                new_best = move;
                new_best->score = 1000000;
                if (ply == 0){
                    store_pv(ply, new_best, search_data);
                }
                if (current_score > alpha){
                    if (current_score >= beta){
                        if (legal_moves == 1) search_data->ordering_success += 1;
        
                        if (!is_capture(move)){
    
                            killer_moves[ply][1] = killer_moves[ply][0];
                            killer_moves[ply][0] = create_packed_move(move);
                        
                            int bonus = 300 * depth + 250;
                            int pen = 300 * depth + 250;

                    
                            // int bonus = 20 * depth * depth + 152 * depth - 145;
                            // int pen = 20 * depth * depth + 152 * depth - 145;
                            for (int l = 0; l < current_index; l++){
                                Move * cm = &move_list[ordering[l]];
                                if (is_capture(cm)) continue;
                                update_history(game, cm->side, cm->piece, cm->start_index, cm->end_index, -pen);
                            }
                            update_history(game, move->side, move->piece, move->start_index, move->end_index, bonus);


                    
                        } else {
                            // capture hist
                            // still unsure if using it is actually good or not.
                            int bonus = 300 * depth + 250;
                            int pen = 300 * depth + 250;
                            // int bonus = 20 * depth * depth + 152 * depth - 145;
                            // int pen = 20 * depth * depth + 152 * depth - 145;

                            for (int l = 0; l < current_index; l++){
                                Move * cm = &move_list[ordering[l]];
                                if (!is_capture(cm)) continue;
                                update_cap_hist(cm->piece, cm->end_index, cm->capture_piece, -pen);
                            }
                            update_cap_hist(move->piece, move->end_index, move->capture_piece, bonus);

                        }
                        // to fail high we break so we can still update corrhist if necessary
                        break;
                    }

                    // otherwise we raise alpha
                    if (legal_moves == 1) search_data->ordering_success += 1;
                    hash_type = EXACT;
                    store_pv(ply, new_best, search_data);
                    alpha = score;
                }
            
            }
        }
    }

    if (legal_moves == 0 && is_in_check){

        // create_new_tt_entry(game, game->key, -MATE_SCORE + ply, EXACT, depth, ply, NULL);
        return -MATE_SCORE + ply;
    
    } else if (legal_moves == 0 && excluded_move == NULL){
        create_new_tt_entry(game, game->key, 0, EXACT, depth, ply, NULL);
        return 0;
    } else if (excluded_move == NULL) {
        
        if (score >= beta) {
            hash_type = LOWER;
            
        }

        create_new_tt_entry(game, game->key, score, hash_type, depth, ply, new_best);
            
    } else if (legal_moves == 0 && excluded_move != NULL){
        score = alpha;
    }

    assert(score > -INT_MAX && score < INT_MAX);
    if (!is_in_check
    && !is_capture(new_best)
    && (hash_type == EXACT
    || (score >= beta && score >= stand)
    || score <= alpha && score <= stand)){
        update_corrhist(game, game->side_to_move, depth, score - stand);
    }

    
    return score;
    
}


int search_root(Game * game, int depth, int  alpha, int  beta, SearchData * search_data, Move move_list[200], int move_count, ThreadData * td){

    if (search_data->stop) return 0;
    bool pv_node = true;

    // decay_history_table();
    // decay_conthist();

    search_data->pv_length[0] = 0;
    TTEntry * entry = NULL;

    int score = -INT_MAX;
    PackedMove tt_move;
    Move * best_move = NULL;
    // if (search_data->max_depth == 1){
        // sort_moves(game, move_list, move_count, tt_move, false, search_data, 0);
    // }

    search_data->ply = 0;
    
    int legal_moves = 0;
    bool is_in_check = in_check(game, game->side_to_move);
    Move refuter;
    Undo undo;
    // int last_node_count = 0;
    for (int i = 0; i < move_count; i++){
        
        Move * move = &move_list[i];
        if (make_move(game, move, &undo)){
            legal_moves += 1;
            bool move_causes_check = in_check(game, game->side_to_move);
            bool dangerous = is_in_check || move->type == PROMOTION || move->type == CAPTURE || move->type == CASTLE || move->is_checking;
        
            int extension = 0;
            bool extend = false;
            int reduction = 0;
            int current_score = 0;

            current_score = -search(PV, game, -beta, -alpha, depth + extension - 1, search_data, 1, 0, false, NULL, &undo, td);
            // int nc = search_data->node_count - last_node_count;
            // last_node_count = search_data->node_count;
            move_list[i].score = current_score;

            if (current_score > score){
                score = current_score;
                best_move = &move_list[i];
                search_data->current_best = move_list[i];
                store_pv(0, best_move, search_data);
            }

        }
        undo_move(game, &move_list[i], &undo);
        search_data->ply -= 1;

        // if (search_data->stop) return 0;
        // clock_t end_time = clock();
        // double elapsed_time = (double)(end_time - search_data->start_time) / CLOCKS_PER_SEC; 
        // if (elapsed_time >= search_data->max_time) {
        //     search_data->stop = true;
        //     return 0;
        // }
    }
    // if (best_move){
    //     // store_pv(0, best_move, search_data);
    //     search_data->current_best = *best_move;
    if (legal_moves == 0 && in_check(game, game->side_to_move)){
        search_data->flags.mate = true;
    } else if (legal_moves == 0){
        search_data->flags.three_fold_repetition = true;
    } else if (three_fold_repetition_root(game, game->key)){
        search_data->flags.three_fold_repetition = true;
    }
    return score;
    
}

void add_search_data_to_root(ThreadData * td){
    
    SearchData * search_data = &td->search_data;
    RootData * root = td->root;
    pthread_mutex_lock(&root->sd_lock);
    root->sd.aspiration_fail += td->search_data.aspiration_fail;
    root->sd.tt_hits+= td->search_data.tt_hits;
    root->sd.tt_probes+= td->search_data.tt_probes;
    root->sd.aspiration_fail+= td->search_data.aspiration_fail;
    root->sd.lmrs_tried+= td->search_data.lmrs_tried;
    root->sd.lmrs_researched+= td->search_data.lmrs_researched;
    root->sd.null_prunes+= td->search_data.null_prunes;
    root->sd.qnodes+= td->search_data.qnodes;
    root->sd.ordering_success+= td->search_data.ordering_success;
    root->sd.futility_prunes+= td->search_data.futility_prunes;
    root->sd.see_prunes+= td->search_data.see_prunes;
    root->sd.beta_prunes+= td->search_data.beta_prunes;
    root->sd.pawn_hash_probes+= td->search_data.pawn_hash_probes;
    root->sd.pawn_hash_hits+= td->search_data.pawn_hash_hits;
    root->sd.late_move_prunes+= td->search_data.late_move_prunes;
    root->sd.q_see_prunes+= td->search_data.q_see_prunes;
    root->sd.rfp+= td->search_data.rfp;
    root->sd.razoring+= td->search_data.razoring;
    root->sd.check_extensions+= td->search_data.check_extensions;
    root->sd.delta_prunes+= td->search_data.delta_prunes;
    root->sd.qdelta_prunes+= td->search_data.qdelta_prunes;
    root->sd.lazy_cutoffs_s1+= td->search_data.lazy_cutoffs_s1;
    root->sd.lazy_cutoffs_s2+= td->search_data.lazy_cutoffs_s2;
    root->sd.lazy_cutoffs_s3+= td->search_data.lazy_cutoffs_s3;
    root->sd.highest_mat_reached+= td->search_data.highest_mat_reached;
    root->sd.fast_evals += td->search_data.fast_evals;
    root->sd.node_count += td->search_data.node_count;
    root->sd.extensions += td->search_data.extensions;
    root->sd.reductions += td->search_data.reductions;

    pthread_mutex_unlock(&root->sd_lock);
}

void * search_thread(void * thread_data){

    ThreadData * td = (ThreadData * )thread_data;
    SearchData * search_data = &td->search_data;
    RootData * root = td->root;
    bool pv_node = true;

    if (three_fold_repetition_root(&td->game, td->game.key)){
        atomic_store(&root->game_end, true);
        atomic_store(&root->stop, true);
    }
    if (search_data->stop) {
        return 0;
    } else {
        bool stop = atomic_load(&root->stop);
        if (stop) {
            search_data->stop = true;
            return 0;
        }
    }
    if (search_data->enable_time){
        double elapsed_time = (double)(now_seconds() - search_data->start_time); 
        if (elapsed_time >= search_data->max_time) {
            atomic_store(&root->stop, true);
            search_data->stop = true;
            return 0;
        }
    }

    search_data->pv_length[0] = 0;
    TTEntry * entry = NULL;

    int score = -INT_MAX;
    PackedMove tt_move;
    Move * best_move = NULL;
    search_data->ply = 0;
    
    int legal_moves = 0;
    bool is_in_check = in_check(&td->game, td->game.side_to_move);
    Move refuter;
    Undo undo;

    while (1){
        int c = atomic_fetch_add(&root->next_index, 1);
        if (c >= root->move_count) break;
        Move * move = &root->move_list[c];
        Game game;
        memcpy(&game, &td->game, sizeof(Game));
        if (make_move(&game, move, &undo)){

            legal_moves = atomic_fetch_add(&root->legal_moves, 1);
        
            int extension = 0;
            bool extend = false;
            int reduction = 0;
            int current_score = 0;

            current_score = -search(PV, &game, -root->beta, -root->alpha, search_data->max_depth + extension - 1, search_data, 1, 0, false, NULL, &undo, td);
            int old = atomic_load(&root->best_score);
            if (current_score > old){
                if (atomic_compare_exchange_strong(&root->best_score, &old, current_score)) {
                    store_pv(0, move, search_data);
                    root->best_move = *move;
                    atomic_store(&root->best_score, current_score);

                    pthread_mutex_lock(&root->pv_lock);
                    root->pv_length = td->search_data.pv_length[0];
                    memcpy(root->pv, td->search_data.pv_table[0],
                           root->pv_length * sizeof(Move));
                    pthread_mutex_unlock(&root->pv_lock);
                }
            }

        }
        undo_move(&game, move, &undo);
    
        if (search_data->stop) {
            break;
        } else {
            bool stop = atomic_load(&root->stop);
            if (stop) {
                search_data->stop = true;
                break;
            }
        }
        if (search_data->enable_time){
            double elapsed_time = (double)(now_seconds() - search_data->start_time); 
            if (elapsed_time >= search_data->max_time) {
                atomic_store(&root->stop, true);
                search_data->stop = true;
                break;
            }
        }

    }
    legal_moves = atomic_load(&root->legal_moves);
    // if (legal_moves == 0 && is_in_check){
    //     atomic_store(&root->game_end, true);
    // } else if (legal_moves == 0){
    //     atomic_store(&root->game_end, true);
    // atomic_fetch_add(&root->node_count, search_data->node_count);
    add_search_data_to_root(td);
    
    return NULL;
}
void reset_root(RootData * data){
    atomic_store(&data->next_index, 0);
    atomic_store(&data->best_score, -INT_MAX);
    atomic_store(&data->legal_moves, 0);
    // atomic_store(&data->stop, 0);
    atomic_store(&data->game_end, 0);
    memset(data->pv, 0, sizeof(data->pv));
    data->pv_length = 0;
    data->alpha = 0;
    data->beta = 0;
}


Move iterative_search(Game * game, SearchFlags * flags){

    int MAX_DEPTH = 30;
    Move current_best;

    game->gen += 1;
    // memset(search_data.history, 0, sizeof(search_data.history));
    memset(killer_moves, 0, sizeof(PackedMove) * 64);
    double start_time = now_seconds();
    double max_time = flags->max_time;
    // double max_time = 0.4;
    // double max_time = 20;
    int thread_count = MAX(MIN(flags->threads, 8), 4);

    

    int best_depth = 1;

    double elapsed_time = 0; 

    int best_move_score = 0;

    int delta = 70;
    int alpha = -INT_MAX;
    int beta = INT_MAX;
    int A = -INT_MAX, B = INT_MAX;
    // int a = 0, b = 0;
    int c = 0;
    const int MAX_WIDEN = 3;

    RootData root;
    root.move_count = 0;
    atomic_store(&root.next_index, 0);
    atomic_store(&root.best_score, -INT_MAX);
    atomic_store(&root.game_end, false);
    pthread_mutex_init(&root.pv_lock, NULL);
    pthread_mutex_init(&root.sd_lock, NULL);
    atomic_store(&root.stop, false);

    bool game_end = false;
    // Move move_list[200];
    // int move_count = 0;
    generate_moves(game, game->side_to_move, root.move_list, &root.move_count);
    printf("MOVES GEN\n");

    // game->halfmove = 40;



    
    // sort_moves(game, move_list, move_count, NULL, &search_data, 0);

    if (game->history_count < 12){
        TTEntry * tt_entry = search_for_tt_entry(game, game->st->key);
        if (tt_entry){
            if (tt_entry->is_opening_book){
                PackedMove m; 
                // unpack_move(tt_entry->move32, &m);

                m = tt_entry->move;

                // print_move_full(&m);
                fflush(stdout);
                bool promo = m.type == PROMOTION;
                Move * resolved_move = find_move(root.move_list, root.move_count, m.start, m.end, promo, m.promo_piece);

                if (resolved_move){
                    // fflush(stdout);
                    printf("bestmove ");
                    print_move_algebraic(resolved_move);
                    printf("\n");
                    fflush(stdout);
                   return *resolved_move; 
                }
            }
        }
        
    }
    SearchData * search_data = &root.sd;
    // search_data.flags = *flags;
    // game->fifty_move_clock = 30;
    
    // game->fifty_move_clock_was = 30;
    
    bool syzygy_found = false;
    int pc_count = __builtin_popcountll(game->board_pieces[BOTH]);
    bool no_castling = game->castle_flags[0][0] == 0 && game->castle_flags[0][1] == 0 && game->castle_flags[1][0] == 0 && game->castle_flags[1][1] == 0;
    if (pc_count <= TB_LARGEST && no_castling && game->en_passant_index == -1){


        unsigned results[TB_MAX_MOVES];
        unsigned s = tb_probe_root(
            game->board_pieces[WHITE], 
            game->board_pieces[BLACK], 
            game->pieces[WHITE][KING] | game->pieces[BLACK][KING], 
            game->pieces[WHITE][QUEEN] | game->pieces[BLACK][QUEEN], 
            game->pieces[WHITE][ROOK] | game->pieces[BLACK][ROOK], 
            game->pieces[WHITE][BISHOP] | game->pieces[BLACK][BISHOP], 
            game->pieces[WHITE][KNIGHT] | game->pieces[BLACK][KNIGHT], 
            game->pieces[WHITE][PAWN] | game->pieces[BLACK][PAWN], 
            game->fifty_move_clock, 0, 0, game->side_to_move, results);

        int best = -1;
        for (int i = 0; i < root.move_count; i++){
            unsigned f = TB_GET_FROM(results[i]);
            unsigned t = TB_GET_TO(results[i]);
            unsigned p = TB_GET_PROMOTES(results[i]);

            Move * m = find_move(root.move_list, root.move_count, f, t, p, p);
            if (m){
                unsigned res = TB_GET_WDL(results[i]);
                unsigned z = TB_GET_DTZ(results[i]);
                if (res == TB_WIN){
                    m->score = 1000000 - z;
                    
                } else if (res == TB_LOSS){
                    m->score = -1000000 + z;
                }
            } else {
            }
        }
        qsort(root.move_list, root.move_count, sizeof(Move), compare_moves);
        syzygy_found = true;
    }
    int current_score = 0;
    for (int i = 1; i < MAX_DEPTH; i++){
        // init_search_data(&search_data, start_time, max_time, i);
        init_search_data(search_data, start_time, max_time, i);

        atomic_store(&root.node_count, 0);

        if (i > 5){
            delta = 80 + i * 8;
            alpha = current_score - delta;
            beta = current_score + delta;
        }

        reset_root(&root);
        root.alpha = alpha;
        root.beta = beta;

        pthread_t threads[thread_count];
        ThreadData td[thread_count];

        for (int t = 0; t < thread_count; t++){
            td[t].id = t;
            memcpy(&td[t].game, game, sizeof(Game));
            init_search_data(&td[t].search_data, start_time, max_time, i);
            td[t].root = &root;
            if (t == 0) {
                td[t].search_data.enable_time = true;
            }
            pthread_create(&threads[t], NULL, search_thread, &td[t]);
        }


        
        for (int i = 0; i < thread_count; i++) {
            pthread_join(threads[i], NULL);
        }


        if (i == 1){
            int legal_moves = atomic_load(&root.legal_moves);
            if (legal_moves == 0 && in_check(game, game->side_to_move)){
                game_end = true;
                break;
            } else if (legal_moves == 0){
                game_end = true;
                break;
            }
            bool ge = atomic_load(&root.game_end);
            if (ge){
                game_end = true;
                break;
            }
            
        }
        current_score = atomic_load(&root.best_score);
        bool stop = atomic_load(&root.stop);
        game_end = atomic_load(&root.game_end);
        double end_time = now_seconds();
        double elapsed_time = (double)(end_time - start_time); 
        if (elapsed_time >= max_time || stop || game_end) {
            break;
        }
        if (i > 5){
            int new_best = atomic_load(&root.best_score);
            bool fail = false;
            if (new_best <= alpha){
                alpha = A;
                fail = true;
            } else if (new_best >= beta){
                beta = B;
                fail = true;
            }
            if (fail){
                
                reset_root(&root);
                root.alpha = alpha;
                root.beta = beta;

                pthread_t threads[thread_count];
                ThreadData td[thread_count];

                for (int t = 0; t < thread_count; t++){
                    td[t].id = t;
                    memcpy(&td[t].game, game, sizeof(Game));
                    init_search_data(&td[t].search_data, start_time, max_time, i);
                    td[t].root = &root;
                    if (t == 0) {
                        td[t].search_data.enable_time = true;
                    }
                    pthread_create(&threads[t], NULL, search_thread, &td[t]);
                }
                for (int i = 0; i < thread_count; i++) {
                    pthread_join(threads[i], NULL);
                }
            }
            current_score = atomic_load(&root.best_score);
        }

        // game_end = atomic_load(&root.game_end);
        // if (!syzygy_found){
        //     qsort(move_list, move_count, sizeof(Move), compare_moves);
        // }

        // if we ran out of time, exit and discard that move
        stop = atomic_load(&root.stop);
        end_time = now_seconds();
        elapsed_time = (double)(end_time - start_time); 
        if (elapsed_time >= max_time || stop || game_end) {
            printf("OUT OF TIME\n");
            break;
        }
        best_depth = i;
        // best_move_score = current_score;

        // debug info!
        printf("Depth %d nodes %d qnodes %d reductions %.1f extensions %.1f tt hits %d tt probes %d asp fails %d fast %d lazy 1 %d 2 %d 3 %d lmr tried %d lmr research %d nmp %d ordering success %d futility prunes %d lmp %d rfp %d razor %d check ext %d delta %d qdelta %d see prunes %d q see prunes %d pawn hash probes %d hits %d nps %f\n",
        search_data->max_depth,
        search_data->node_count,
        search_data->qnodes,
        search_data->reductions,
        search_data->extensions,
        search_data->tt_hits,
        search_data->tt_probes,
        search_data->aspiration_fail,
        search_data->fast_evals,
        search_data->lazy_cutoffs_s1,
        search_data->lazy_cutoffs_s2,
        search_data->lazy_cutoffs_s3,
        search_data->lmrs_tried,
        search_data->lmrs_researched,
        search_data->null_prunes,
        search_data->ordering_success,
        search_data->futility_prunes,
        search_data->late_move_prunes,
        search_data->rfp,
        search_data->razoring,
        search_data->check_extensions,
        search_data->delta_prunes,
        search_data->qdelta_prunes,
        search_data->see_prunes,
        search_data->q_see_prunes,
        search_data->pawn_hash_probes,
        search_data->pawn_hash_hits,
        (search_data->node_count + search_data->qnodes) / elapsed_time);


        // uci printouts
        if (!game_end){
            int nc = atomic_load(&root.node_count);
            printf("info depth %d score cp %d nodes %d pv ", i, best_move_score, nc);
            pthread_mutex_lock(&root.pv_lock);
            for (int i = 0; i < root.pv_length; i++) {
                print_move_algebraic(&root.pv[i]);
                printf(" ");
            }
            pthread_mutex_unlock(&root.pv_lock);
            printf("\n");
            current_best = root.best_move;
            best_move_score = atomic_load(&root.best_score);
            fflush(stdout);
            
        }
        // if (!search_data.flags.mate && !search_data.flags.three_fold_repetition){
            // printf("info depth %d score cp %d nodes %d pv ", i, best_move_score, search_data.node_count);
            // // for (int i = 0; i < search_data.pv_length[0]; i++) {
            // //     print_move_algebraic(&search_data.pv_table[0][i]);
            // //     printf(" ");
            // // }
            // printf("\n");
            // current_best = search_data.current_best;
            // search_data.current_best_score = current_score;
            // fflush(stdout);
            
        // }

    }

    if (!game_end){
        printf("bestmove ");
        print_move_algebraic(&current_best);
        printf("\n");
        fflush(stdout);
        
    } else {
        printf("Mate or Draw by Repetition detected.");
        if (flags){
            flags->mate = true;
            
        }
    }
    return current_best;
    
}


uint64_t perft(Game * game, int depth){
    // SearchData data;

    if (depth == 0) {
        // int ktv = 0;
        return 1ULL;
    }
    
    Move move_list[200];
    int move_count = 0;
    generate_moves(game, game->side_to_move, move_list, &move_count);
    uint64_t nodes = 0;

    Undo undo;
    bool print = false;
    for (int i = 0; i < move_count; i++){
        
        Move * move = &move_list[i];

        // if illegal move
        if (!make_move(game, move, &undo)){

            undo_move(game, move, &undo);

        } else {
            uint64_t move_nodes = perft(game, depth - 1);
            nodes += move_nodes;

            undo_move(game, move, &undo);
        }
    }
    return nodes;
}

void perft_root(Game * game, int depth){

    clock_t start_time = clock();
    Move move_list[200];
    int move_count = 0;
    generate_moves(game, game->side_to_move, move_list, &move_count);
    uint64_t nodes = 0;
    Undo undo;

    for (int i = 0; i < move_count; i++){
        
        Move * move = &move_list[i];

        // if illegal move
        if (!make_move(game, move, &undo)){

            undo_move(game, move, &undo);

        } else {

            uint64_t move_nodes = perft(game, depth - 1);
            print_move_algebraic(move);
            printf(" Nodes: %ld\n", move_nodes);
            nodes += move_nodes;

            undo_move(game, move, &undo);
        }
    }

    clock_t end_time = clock();
    
    double elapsed_time = (double)(end_time - start_time) ; 
    
    printf("Depth %d Nodes: %ld and NPS: %f\n", depth, nodes, nodes / elapsed_time);
}
