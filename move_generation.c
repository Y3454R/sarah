#include "game.h"
#include "types.h"
#include "math.h"
#include "utils.h"
#include "move_generation.h"
#include "magic.h"
#include "zobrist.h"




void update_blocker_masks(Game * game){


    game->board_pieces[WHITE] |= game->pieces[WHITE][PAWN];
    game->board_pieces[WHITE] |= game->pieces[WHITE][KING];
    game->board_pieces[WHITE] |= game->pieces[WHITE][BISHOP];
    game->board_pieces[WHITE] |= game->pieces[WHITE][KNIGHT];
    game->board_pieces[WHITE] |= game->pieces[WHITE][ROOK];
    game->board_pieces[WHITE] |= game->pieces[WHITE][QUEEN];
    game->board_pieces[BLACK] |= game->pieces[BLACK][PAWN];
    game->board_pieces[BLACK] |= game->pieces[BLACK][KING];
    game->board_pieces[BLACK] |= game->pieces[BLACK][BISHOP];
    game->board_pieces[BLACK] |= game->pieces[BLACK][KNIGHT];
    game->board_pieces[BLACK] |= game->pieces[BLACK][ROOK];
    game->board_pieces[BLACK] |= game->pieces[BLACK][QUEEN];
    
    
    game->board_pieces[BOTH] = game->board_pieces[WHITE] | game->board_pieces[BLACK];
}


// finds which piece we are capturing and then pushes it to the move list







// flips psqt for black
void init_psqt(Game * game){

    // black (flip ranks)
    for (int i = 0;  i < PIECE_TYPES; i++){
        for (int r = 0; r < 8; r++){
            for (int f = 0; f < 8; f++){
                PSQT_MG[BLACK][i][r * 8 + f] = PSQT_MG[WHITE][i][abs(r - 7) * 8 + f];
                PSQT_EG[BLACK][i][r * 8 + f] = PSQT_EG[WHITE][i][abs(r - 7) * 8 + f];
            }
        }
    }
}

void init_passed_pawn_masks(){

    // printf("INITING\n");
    // for white
    for (int i = 0; i < 64; i++){
        uint64_t mask = 0;
        uint64_t in_front_mask = 0;
        int start_file = (i % 8) - 1;
        File file_right = start_file + 2;
        uint64_t ranks = 0;
        if (start_file < 0) {
            mask |= file_masks[file_right];
        } else if (start_file == 6){
            mask |= file_masks[start_file];
        } else {
            mask |= file_masks[start_file];
            mask |= file_masks[file_right];
        }
        mask |= file_masks[i % 8];
        in_front_mask |= file_masks[i % 8];
        int above_index = i - 8;
        while (above_index >= 0){
            ranks |= rank_masks[abs((above_index / 8) - 7)];
            above_index -= 8;
        }
        mask &= ranks;
        in_front_mask &= ranks;
        // mask &= ~(1ULL << i);
        
        // print_board(mask, WHITE_PAWN);
        passed_pawn_masks[WHITE][i] = mask;
        in_front_file_masks[WHITE][i] = in_front_mask;
    }



    for (int i = 0; i < 64; i++){
        uint64_t mask = 0;
        uint64_t in_front_mask = 0;
        int start_file = (i % 8) - 1;
        File file_right = start_file + 2;
        uint64_t ranks = 0;
        if (start_file < 0) {
            mask |= file_masks[file_right];
        } else if (start_file == 6){
            mask |= file_masks[start_file];
        } else {
            mask |= file_masks[start_file];
            mask |= file_masks[file_right];
        }
        mask |= file_masks[i % 8];
        in_front_mask |= file_masks[i % 8];
        int above_index = i + 8;
        while (above_index < 64){
            ranks |= rank_masks[abs((above_index / 8) - 7)];
            above_index += 8;
        }
        mask &= ranks;
        // mask &= ~(1ULL << i);
        in_front_mask &= ranks;
        
        // print_board(mask, WHITE_PAWN);
        passed_pawn_masks[BLACK][i] = mask;
        in_front_file_masks[BLACK][i] = in_front_mask;
    }
    
    
}

void init_pawn_shield_masks(Game * game){
    

    for (int i = 0; i < 56; i++){
        
        uint64_t mask = 0;
        int start_file = (i % 8);
        if (start_file <= 0) {
            mask |= (1ULL << (i + 8));
            mask |= (1ULL << (i + 9));
        } else if (start_file == 7){
            mask |= (1ULL << (i + 8));
            mask |= (1ULL << (i + 7));
        } else {
            if (i != 0){
                mask |= (1ULL << (i + 9));
            }
            mask |= (1ULL << (i + 7));
            mask |= (1ULL << (i + 8));
        }
        pawn_shield_masks[BLACK][i] = mask;
        // print_board(pawn_shield_masks[BLACK][i], WHITE_PAWN);
    }
    for (int i = 8; i < 64; i++){
        
        uint64_t mask = 0;
        int start_file = (i % 8);
        if (start_file <= 0) {
            mask |= (1ULL << (i - 8));
            mask |= (1ULL << (i - 7));
        } else if (start_file == 7){
            mask |= (1ULL << (i - 8));
            mask |= (1ULL << (i - 9));
        } else {
            if (i != 8){
                mask |= (1ULL << (i - 9));
                
            }
            mask |= (1ULL << (i - 7));
            mask |= (1ULL << (i - 8));
        }
        pawn_shield_masks[WHITE][i] = mask;
        // print_board(pawn_shield_masks[WHITE][i], WHITE_PAWN);
    }
    
}


// used for tuning values without hard coding

void init_tuning(Game * game){
    
    // mobility blockers
    for (int i = 0; i < 2; i++){
        for (int p = 0; p < PIECE_TYPES; p++){
            for (int x = 0; x < PIECE_TYPES; x++){
                MOBILITY_BLOCKER_VALUES[i][p][x] /= 2;
            }
        }
    }
    for (int c = 0; c < 2; c++){
        for (int i = 0; i < 64; i++){
            PAWN_STORM_PSQT_MG[c][i] *= 3;
            PAWN_STORM_PSQT_EG[c][i] *= 3;
        }
    }

    // DOUBLED_PAWN_PENALTY *= 2;
    // ISOLATED_PAWN_PENALTY *= 2;
    // PASSED_PAWN_BONUS *= 2;
    // BISHOP_PAIR_BONUS *= 2;
}


void init_manhattan_dist(){
    int max_dist = 0;
    for (int i = 0; i < 64; i++){
        for (int x = 0; x < 64; x++){
            manhattan_distance[i][x] = manhattanDistance(i, x);
        }
    }
}

void init_pawn_randoms(){
    uint64_t seed = 0x123456789ABCDEFULL;

    for (int side = 0; side < 2; side++) {
        for (int sq = 0; sq < 64; sq++) {
            piece_random[side][sq] = splitmix64(&seed);
        }
    }
    for (int side = 0; side < 2; side++) {
        for (int p = 0; p < PIECE_TYPES; p++) {
            for (int c = 0; c < 12; c++){
                
                material_randoms[side][p][c] = splitmix64(&seed);
            }
        }
    }

    for (int side = 0; side < COLOR_MAX; side++) {
        for (int z = 0; z < 64; z++) { 
            king_location_random[side][z] = splitmix64(&seed);
        }
    }
    
}

void init_assorted_masks(Game * game){

    uint64_t full_mask = 0;
    for (int i = 63; i >= 0; i--){
        if (i / 8 < 7){
            full_mask |= rank_masks[6 - (i/8)];
            in_front_ranks_masks[BLACK][i] = full_mask;
        }
    }

    full_mask = 0;
    for (int i = 0; i < 64; i++){
        if (i / 8 > 0){
            full_mask |= rank_masks[7-(i/8) + 1];
            in_front_ranks_masks[WHITE][i] = full_mask;
        }
    }

    for (int r = 0; r < 8; r++){

        for (int f = 0; f < 8; f++){
            if (r == 7){
                
                adjacent_in_front_masks[WHITE][r * 8 + f] = pawn_shield_masks[WHITE][r * 8 + f];
            } else {
                adjacent_in_front_masks[WHITE][r * 8 + f] = pawn_shield_masks[WHITE][r * 8 + f] | pawn_shield_masks[WHITE][(r - 1) * 8 + f];
                
            }
        }
    }
    for (int r = 0; r < 8; r++){

        for (int f = 0; f < 8; f++){
            if (r == 7){
                
                adjacent_in_front_masks[BLACK][r * 8 + f] = pawn_shield_masks[BLACK][r * 8 + f];
            } else {
                adjacent_in_front_masks[BLACK][r * 8 + f] = pawn_shield_masks[BLACK][r * 8 + f] | pawn_shield_masks[BLACK][(r + 1) * 8 + f];
                
            }
        }
    }

    
    for (int i = 0; i < 64; i ++){
        int file = i % 8;
        int rank = i / 8;
        uint64_t w_zone = king_moves[i];
        uint64_t b_zone = king_moves[i];

        if (rank - 2 >= 0){
            w_zone |= 1ULL << (i - 16);
            if (file - 1 >= 0){
                w_zone |= 1ULL << (i - 17);
            }
            if (file + 1 <= 7){
                w_zone |= 1ULL << (i - 15);
            }
        }
        if (rank + 2 <= 7){
            
            b_zone |= 1ULL << (i + 16);
            if (file - 1 >= 0){
                b_zone |= 1ULL << (i + 15);
            }
            if (file + 1 <= 7){
                b_zone |= 1ULL << (i + 17);
            }
        }
        king_zone_masks[WHITE][i] = w_zone;
        king_zone_masks[BLACK][i] = b_zone;
        // print_board(w_zone, WHITE_KING);
        // print_board(b_zone, WHITE_KING);
    }

    for (int c = 0; c < COLOR_MAX; c++){
        for (int i = 0; i < PIECE_TYPES; i++){
            for (int s = 0; s < PIECE_TYPES; s++){
                MOBILITY_BLOCKER_VALUES[c][i][s] *= 1;
                
            }
        }
    }

    for (int i = 0; i < 64; i++){
        SQ_TO_FILE[i] = i % 8;
    }
    for (int i = 0; i < 64; i++){
        SQ_TO_RANK[i] = abs(7-(i / 8));
    }
    
    for (int i = 0; i < 64; i++){
        if (i / 8 == 6){
            double_pushed_pawn_squares[WHITE][i] = i - 16;
            // print_board(1ULL << double_pushed_pawn_squares[WHITE][i], WHITE_PAWN);
        }
    }
    for (int i = 0; i < 64; i++){
        if (i / 8 == 1){
            double_pushed_pawn_squares[BLACK][i] = i + 16;
            // print_board(1ULL << double_pushed_pawn_squares[BLACK][i], BLACK_PAWN);
        }
    }



    for (int a = 0; a < 64; a++) {
        for (int b = 0; b < 64; b++) {
            between_sq[a][b] = 0;

            if (a == b)
                continue;

            for (int d = 0; d < 4; d++) {
                if (!(DIAGONAL_RAYS[d][a] & (1ULL << b)))
                    continue;

                uint64_t bb = DIAGONAL_RAYS[d][a];

                bb &= ~DIAGONAL_RAYS[d][b];

                bb &= ~(1ULL << b);
                bb &= ~(1ULL << a);

                between_sq[a][b] = bb;
                break;
            }
            for (int l = 0; l < 4; l++) {
                if (!(LATERAL_RAYS[l][a] & (1ULL << b)))
                    continue;

                uint64_t bb = LATERAL_RAYS[l][a];

                bb &= ~LATERAL_RAYS[l][b];

                bb &= ~(1ULL << b);
                bb &= ~(1ULL << a);

                between_sq[a][b] = bb;
                break;
            }
        }
    }

    for (int i = 0; i < 64; i++){
        castling_rights_masks[i] = 0;
    }

    castling_rights_masks[0] = CBQ;
    castling_rights_masks[4] = CBQ | CBK;
    castling_rights_masks[7] = CBK;
    castling_rights_masks[56] = CWQ;
    castling_rights_masks[60] = CWQ | CWK;
    castling_rights_masks[63] = CWK;
    
}



void init_piece_boards(Game * game){

    // generate_pawn_boards(game);
    // generate_knight_boards(game);
    // generate_king_boards(game);
    init_sliding_piece_tables(game);
    init_directional_ray_tables();
    // generate_bishop_boards(game);
    // generate_rook_boards(game);
    init_psqt(game);
    init_passed_pawn_masks();
    init_pawn_shield_masks(game);
    init_tuning(game);
    init_manhattan_dist();
    init_pawn_randoms();
    init_assorted_masks(game);

    
}

