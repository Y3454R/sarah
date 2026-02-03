#ifndef ZOBRIST_H
#define ZOBRIST_H

#include "types.h"
#include "utils.h"

/* this file contains all of our different hash tables. they are:
transposition table
pawn eval hash
full eval hash
refutation table
countermove table */



extern const uint64_t Random64[781];
extern uint64_t piece_random[COLOR_MAX][64];
extern uint64_t king_location_random[COLOR_MAX][64];

extern int polyglot_piece_types[COLOR_MAX][PIECE_TYPES];
extern int polyglot_castle_offsets[COLOR_MAX][CASTLESIDE_MAX];

void init_tt(Game * game);
void reset_tt(Game * game);
void init_pawn_hash(Game * game);
void reset_pawn_hash(Game * game);
void init_eval_table(Game * game);
void reset_eval_table(Game * game);
void reset_countermove_and_refutation_tables(Game * game);
void reset_corrhist(Game * game);

uint64_t create_material_hash_from_scratch(Game * game);
uint64_t create_nonpawn_hash_from_scratch(Game * game, Side side);
uint64_t create_piece_hash_from_scratch(Game * game, PieceType p);
uint64_t create_pawn_hash_from_scratch(Game * game);

static inline size_t pawn_hash_index(uint64_t key){
    return (size_t)(key & PAWN_MASK);
}
static inline size_t tt_hash_index(uint64_t key){
    return (size_t)(key & TT_MASK);
}
static inline size_t eval_hash_index(uint64_t key){
    return (size_t)(key & EVAL_MASK);
}


static inline TTEntry * search_for_tt_entry(Game * game, uint64_t key){
  size_t hashed_index = tt_hash_index(key);
  TTBucket * possible_entry = &tt[hashed_index];
  // for (int i = 0; i < 4; i++){
    if (key == possible_entry->entries[0].key ){
      return &possible_entry->entries[0];
    }
    
  // }
    
  return NULL;
}

static inline void reset_piece_keys(Game * game){
  

    game->os.piece_key[PAWN] = 0;
    game->os.piece_key[KNIGHT] = 0;
    game->os.piece_key[BISHOP] = 0;
    game->os.piece_key[ROOK] = 0;
    game->os.piece_key[QUEEN] = 0;
    game->os.piece_key[KING] = 0;
}

static inline PawnHashEntry * search_for_pawn_hash_entry(Game * game, uint64_t key){
  size_t index = pawn_hash_index(key);
  if (key == pawn_hash_table[index].key){
    return &pawn_hash_table[index];
  } else {
    return NULL;
  }
}

static inline void create_new_pawn_hash_entry(Game * game, uint64_t key, Score s, EvalMasks * masks){
  
  size_t hashed_index = pawn_hash_index(key);
  PawnHashEntry* entry = &pawn_hash_table[hashed_index];

  pawn_hash_table[hashed_index] = (PawnHashEntry){
    .key = key,
    .s = s,
    .w_atk = masks->am_p[WHITE][PAWN],
    .b_atk = masks->am_p[BLACK][PAWN],
    .w_passers = masks->passers[WHITE],
    .b_passers = masks->passers[BLACK],
  };

}


void init_opening_book(Game * game, const char * path);

static inline int adjust_mate_score_from_tt(int score, int stored_ply, int current_ply) {
    if (score >= MATE_SCORE - 200 || score <= -MATE_SCORE + 200) {
        if (score > 0) {
            return score + stored_ply - current_ply;
        } else {
            return score - stored_ply + current_ply;
        }
    }
    return score;
}

static inline TTEntry * get_replace_slot_from_bucket(Game * game, TTBucket * bucket, uint64_t key){
  
  int index = 0;
  int lowest = INT_MAX;
  for (int i = 0; i < BUCKET_SIZE; i++) {
    TTEntry *e = &bucket->entries[i];
    
    if (e->key == 0 || e->key == key) {
        index = i;
        break;
    }
    
    int score = e->depth - 4 * (game->gen - e->gen);
    if (score < lowest) {
        lowest = score;
        index = i;
    }
  }
  return &bucket->entries[index];
}


static inline void create_new_tt_entry(Game * game, uint64_t key, int score, TTType type, int16_t depth, uint8_t ply, Move move){
  
  size_t hashed_index = tt_hash_index(key);
  TTBucket * bucket = &tt[hashed_index];


  // TTEntry * entry = get_replace_slot_from_bucket(game, bucket, key);
  // TTEntry * entry = &tt[hashed_index];
  TTEntry * entry = &bucket->entries[0];

  if (key != entry->key ||
      depth > entry->depth - 1 || type == EXACT || entry->type == UPPER){
    bool has_eval = false;
    *entry = (TTEntry){
      .key = key,
      .move = move,
      .score = score,
      .depth = (int16_t)depth,
      .type = (uint8_t)type,
      .ply = ply,
      .gen = game->gen,
      .is_opening_book = false,
    };
  }

}
static inline EvalEntry * search_for_eval(Game * game, uint64_t key){
  
  size_t hashed_index = eval_hash_index(key);
  EvalEntry * e = &eval_table[hashed_index];
  if (e->key == key){
    return e;
  }
  return NULL;
}


    
static inline void hash_eval(Game * game, uint64_t key, int eval, Side side){
  size_t hashed_index = eval_hash_index(key);
  EvalEntry * e = &eval_table[hashed_index];

  e->key = key;
  e->eval = eval;
  e->side = side;
  
}



static inline int sq_from_file_rank(int file, int rank) {
    return rank * 8 + file; 
}

/* @brief polyglot parsing, see bluefever software's tutorial */

static inline Move polyglot_decode(uint16_t m)
{
    int to_file   =  m & 0x7;
    int to_rank   = (m >> 3) & 0x7;
    int from_file = (m >> 6) & 0x7;
    int from_rank = (m >> 9) & 0x7;
    int promo     = (m >> 12) & 0x7;

    Move move;
    MoveType type = NORMAL;
    uint8_t from = 0, to = 0;
    PieceType promo_type = PIECE_NONE;
    from = file_and_rank_to_index((File)from_file, (Rank)from_rank);
    to = file_and_rank_to_index((File)to_file, (Rank)to_rank);
    if (promo != 0){
      promo_type = (PieceType)promo;
      type = PROMOTION;
    }


    // due to polyglot convention, change castling moves to king moves
    if (from == 60 && to == 63){
      type = CASTLE;
      from = 60;
      to = 62;
    } 
    if (from == 60 && to == 56){
      type = CASTLE;
      from = 60;
      to = 58;
      
    }
    if (from == 4 && to == 0){
      type = CASTLE;
      from = 4;
      to = 2;
      
    }
    if (from == 4 && to == 7){
      type = CASTLE;
      from = 4;
      to = 6;
    }
    if (type == PROMOTION){
      move = create_promotion(from, to, promo_type);
      
    } else {
      
      move = create_move(from, to, type);
    }
    return move;
}
static inline void create_new_polyglot_entry(Game * game, uint64_t key, uint16_t weight, Move move){

  size_t hashed_index = tt_hash_index(key);

  TTEntry * entry = get_replace_slot_from_bucket(game, &tt[hashed_index], key);
  // TTEntry * entry = &tt[hashed_index];

  *entry = (TTEntry){
    .key = key,
    // .move32 = pack_move(&move),
    .move = move,
    .depth = -20,
    .weight = weight,
    .is_opening_book = true,
  };
  
}


static inline uint64_t get_piece_random(PieceType piece, Side side, int index){
  
  // see polyglot format for what this means
  return Random64[(64 * polyglot_piece_types[side][piece]) + (8 * SQ_TO_RANK[index]) + SQ_TO_FILE[index]];
}

static inline uint64_t get_castling_random(uint8_t bit){
  return Random64[CASTLING_OFFSET + bit];
}

static inline uint64_t get_castling_randomr(Side side, CastleSide castle_side){
  return Random64[CASTLING_OFFSET + polyglot_castle_offsets[side][castle_side]];
}

static inline uint64_t get_en_passant_random(int index){
  return Random64[EN_PASSANT_OFFSET + SQ_TO_FILE[index]];
}
static inline uint64_t get_turn_random(){
  return Random64[TURN_OFFSET];
}

uint64_t create_zobrist_from_scratch(Game * game);

static inline bool check_hash(Game * game){
    uint64_t zob = create_zobrist_from_scratch(game);

    return (zob == game->st->key);
}

static inline uint32_t mix32(uint32_t x){
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    return x;
}
// to generate randoms for our hash tables
static inline uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

#endif
