#ifndef SORT_H
#define SORT_H

#include "search.h"
#include "types.h"
#include "game.h"
#include "eval.h"
#include "zobrist.h"


static inline int compare_moves(const void * va, const void * vb){
    const Move *a = (Move*)va;
    const Move *b = (Move*)vb;
    if (a->score > b->score) return -1;
    if (a->score < b->score) return 1;
    return 0;
}
static inline int score_move(Game * game, Move * move, int ply){



    int score = 0;
    move->score = 0;
    // move->see = 0;
    
    // if (move->is_checking){
    //     int see_val = 0;
    //     if (move->see){
    //         see_val = move->see;
    //     } else {
    //         see_val = see(game, move);
    //         move->see = see_val;
    //     }
    //     if (see_val >= -100){
    //         score += 20000;
    //     } else {
    //         score += 1000;
    //     }
    // }
    // if (search_data){

    if (is_capture(move)){
        // int see_val = 0;
        // if (move->seed){
        //     see_val = move->see;
        // } else {
        //     see_val = see(game, move);
        //     move->seed = true;
        //     move->see = see_val;
        // }
        // score += mvv_lva[move->piece][move->capture_piece] * 5 + see_val;
        int s = mvv_lva[move->piece][move->capture_piece];
        // int see_val = 0;
        // if (move->seed){
        //     see_val = move->see;
        // } else {
        //     see_val = see(game, move);
        //     move->seed = true;
        //     move->see = see_val;
        // }
        // score +=s  + 8000000;
        // if (see_val >= -100 || (move->capture_piece >= move->piece)){
        // if (move->seed){
            // score += s * 1000 + see_val * 100 + capture_history[move->piece][move->end_index][move->capture_piece];
        score += PVAL[move->capture_piece] * 6 + capture_history[move->piece][move->end_index][move->capture_piece];
        // } else {
            
        // score +=             // see_val * 1000 +
        //     capture_history[move->piece][move->end_index][move->capture_piece];
        // score += s;
        // }
            
        // } else if (see_val >= -400){
        //  score += s + 0;
        // } else {
        //     score += s * 2 + see_val - 200000;
            
        // }
    } else {
        int h = history[move->side][move->piece][move->start_index][move->end_index];
        score += h;
        if (game->history_count > 0){

            PackedMove * lm = &game->last_move;
            int last_index = 
                PIECE_TYPES * BOARD_MAX * lm->side +
                BOARD_MAX * lm->piece +
                lm->end;
            int current_index = 
                PIECE_TYPES * BOARD_MAX * move->side +
                BOARD_MAX * move->piece +
                move->end_index;
            score += conthist[last_index][current_index];

        }
        if (game->history_count > 1){

            PackedMove * lm = &game->last_last_move;
            int last_index = 
                PIECE_TYPES * BOARD_MAX * lm->side +
                BOARD_MAX * lm->piece +
                lm->end;
            int current_index = 
                PIECE_TYPES * BOARD_MAX * move->side +
                BOARD_MAX * move->piece +
                move->end_index;
            score += conthist[last_index][current_index];

        }

    }
        
    // }


    
    return score;

}
static inline int score_qmove(Game * game, Move * move, int ply, PackedMove tt_move, bool has_tt_move){



    // print_move_full(move);
    move->see = 0;
    int score = 0;
    if (has_tt_move){
        if (packed_move_is_equal(move, tt_move)){
            score +=10000000;
            
        }
        
    }
    
    if (is_capture(move)){
        int s = mvv_lva[move->piece][move->capture_piece];
        score += s;
        // int see_val = see(game, move);
        // score += capture_history[move->piece][move->end_index][move->capture_piece]+ 9000000;
            
        // int see_val = 0;
        // if (move->seed){
        //     see_val = move->see;
        // } else {
        //     see_val = see(game, move);
        //     move->seed = true;
        //     move->see = see_val;
        // }
        // score += mvv_lva[move->piece][move->capture_piece] * 5 + see_val;
        // if (see_val >= 0){
        //     score += 100000;
        // } else {
        //     score += 50000;
        // }
    }
    // if (move->type == PROMOTION){
    //     score += 40000;
    // }
    // if (move->is_checking){
    //     score += 30000;
    // }
        // score +=s  + 8000000;
        // if (see_val >= -100 || (move->capture_piece >= move->piece)){
        // if (move->seed){
        //     score += s;
        // } else {
            
        // score +=
            // see_val * 1000 +
            // capture_history[move->piece][move->end_index][move->capture_piece];
        move->scored = true;
        // move->see = see_val;
        // move->seed = true;
    // } else if (move->is_checking){
    //     score += 1000000;
    // } else {
    //     int h = history[move->side][move->piece][move->start_index][move->end_index];
    //     score += h;
    //     if (game->history_count > 0){

    //         PackedMove * lm = &game->last_move;
    //         int last_index = 
    //             PIECE_TYPES * BOARD_MAX * lm->side +
    //             BOARD_MAX * lm->piece +
    //             lm->end;
    //         int current_index = 
    //             PIECE_TYPES * BOARD_MAX * move->side +
    //             BOARD_MAX * move->piece +
    //             move->end_index;
    //         score += conthist[last_index][current_index];

    //     }
    //     if (game->history_count > 1){

    //         PackedMove * lm = &game->last_last_move;
    //         int last_index = 
    //             PIECE_TYPES * BOARD_MAX * lm->side +
    //             BOARD_MAX * lm->piece +
    //             lm->end;
    //         int current_index = 
    //             PIECE_TYPES * BOARD_MAX * move->side +
    //             BOARD_MAX * move->piece +
    //             move->end_index;
    //         score += conthist[last_index][current_index];

    //     }

    // }
    
    return score;

}

static inline void sort_moves(Game * game, Move move_list[200], int move_count, PackedMove best_move, bool has_tt_move, SearchData * search_data, int ply){
    
    // print_moves(move_list, move_count);
    bool has_pv_move = false;
    // PackedMove countermove = get_countermove(game, &game->last_move);
    // Move c;
    bool has_countermove = false;
    bool has_refutation = false;
    // if (countermove){
    //     has_countermove = true;
    //     unpack_move(countermove, &c);
    // }
    
    
    // for (int i = 0; i < move_count; i++){
    //     Move * move = &move_list[i];
    //     move->score = score_move(game, move, search_data, ply, best_move, has_tt_move, countermove);
    // }
    // partial_selection_sort(move_list, move_count, 20);

}

static inline void init_ordering(int ordering[200], int move_count){
    
    for (int i = 0; i < move_count; i++){
        ordering[i] = i;
    }
}

static inline bool move_is_eligible(Game * game, Move * move, PickType stage, bool has_tt_move, PackedMove tt_move, int ply){

    switch (stage){
        case PICK_TT_MOVE:
            if (has_tt_move){
                return packed_move_is_equal(move, tt_move);
            } else return false;
            break;
        case PICK_GOOD_CAP:
            if (is_capture(move)){
                // if (move->capture_piece > move->piece) return true;
                int see_val = 0;
                if (move->seed){
                    see_val = move->see;
                } else {
                    see_val = see(game, move);
                    move->see = see_val;
                    move->seed = true;
                } 
                if (see_val >= 0) return true;
            }
            break;
        case PICK_PROMO:
            return move->type == PROMOTION;
            break;
        case PICK_KILLER:
            if (!is_capture(move)){
                    
                return packed_move_is_equal(move, killer_moves[ply][0]) || packed_move_is_equal(move, killer_moves[ply][1]);
            }
            break;
        // case PICK_MID_CAP:
        //     if (is_capture(move)){
        //         int see_val = 0;
        //         if (move->seed){
        //             see_val = move->see;
        //         } else {
        //             see_val = see(game, move);
        //             move->see = see_val;
        //             move->seed = true;
        //         } 
        //         if (see_val >= -350 || move->capture_piece > move->piece) return true;
        //     }
        //     break;
        case PICK_QUIET:
            return !is_capture(move);
            break;
        case PICK_BAD_CAP:
            if (is_capture(move)){
                return true;
            }
            break;
        default: break;
    }
    return false;
}

// returns null if none eligible
static inline Move pick_next_move(Game * game, Move move_list[200], int16_t scores[200], int ordering[200], int * current, int move_count, PackedMove best_move, bool has_tt_move, int ply, PickType stage, bool qsearch){

    int c = *current;
    int best = c;
    int best_score = -INT_MAX;

    int eligible_count = 0;
    for (int i = c; i < move_count; i++) {
        int m = ordering[i];
        if (move_is_eligible(game, &move_list[m], stage, has_tt_move, best_move, ply)){
            eligible_count += 1;
            if (!scores[m].score){
                scores[m].score = score_move(game, &move_list[m], ply);
            }

            if (scores[m].score > best_score) {
                best_score = scores[m].score;
                best = i;
            }
        
        }
            
    }


    Move r = NULL;
    if (eligible_count){
        int tmp = ordering[c];
        ordering[c] = ordering[best];
        ordering[best] = tmp;

        r = move_list[ordering[c]];
        (*current)++;
    }
    return r;

}


static inline void sort_qmoves(Game * game, Move move_list[200], int move_count, int ordering[200], PackedMove best_move, bool has_tt_move, int ply){
    
    
    for (int i = 0; i < move_count; i++){
        Move * move = &move_list[i];
        // move->score = score_qmove(game, move, ply, best_move, has_tt_move);
        move->score = score_move(game, move, ply);
    }
    // partial_selection_sort(move_list, move_count, 15);
    // for (int i = 0; i < MIN(15, move_count); ++i){
    //     // selection_extract_best(move_list, move_count, i);
    //     int best = i;
    //     int best_score = move_list[ordering[i]].score;
    //     for (int j = i + 1; j < move_count; ++j){
    //         if (move_list[ordering[j]].score > best_score){
    //             best_score = move_list[ordering[j]].score;
    //             best = j;
    //         }
    //     }
    //     if (best != i){
    //         int tmp = ordering[i];
    //         ordering[i] = ordering[best];
    //         ordering[best] = tmp;

            
    //     }
    // }

}
static inline Move * pick_next_qmove(Move move_list[200], int move_count, int ordering[200], int * current_index){

    int c = *current_index;
    int best_score = -INT_MAX;
    int idx = c;
    if (c >= move_count){
        printf("ERROR: NEXT QMOVE COUNT OOB\n");
        return NULL;
    }
    for (int i = c; i < move_count; i++){
        if (move_list[ordering[i]].score > best_score){
            best_score = move_list[ordering[i]].score;
            idx = i;
        }
    }

    int tmp = ordering[c];
    ordering[c] = ordering[idx];
    ordering[idx] = tmp;

    (*current_index)++;
    return &move_list[ordering[c]];
    
}


#endif
