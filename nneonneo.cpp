// Generated dynamically from nneonneo origin
#include <iostream>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ctime>

// Simple RNG for `draw_tile`
static inline unsigned unif_random(unsigned n) {
    return rand() % n;
}

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif

inline int get_log2(int val) {
    if (val == 0) return 0;
    int res = 0;
    while (val >>= 1) res++;
    return res;
}

#include <stdlib.h>


/* The fundamental trick: the 4x4 board is represented as a 64-bit word,
 * with each board square packed into a single 4-bit nibble.
 * 
 * The maximum possible board value that can be supported is 32768 (2^15), but
 * this is a minor limitation as achieving 65536 is highly unlikely under normal circumstances.
 * 
 * The space and computation savings from using this representation should be significant.
 * 
 * The nibble shift can be computed as (r,c) -> shift (4*r + c). That is, (0,0) is the LSB.
 */

typedef uint64_t board_t;
typedef uint16_t row_t;

struct trans_table_entry_t{
    board_t key;
    uint8_t rem_depth;
    float heuristic;
};

const uint32_t TT_CAPACITY = 1 << 24; // 16M elements, ~256MB
const uint32_t TT_MASK = TT_CAPACITY - 1;
static trans_table_entry_t* global_tt = nullptr;

inline void init_global_tt() {
    if (!global_tt) {
        global_tt = new trans_table_entry_t[TT_CAPACITY]();
    }
}

static const board_t ROW_MASK = 0xFFFFULL;
static const board_t COL_MASK = 0x000F000F000F000FULL;

static inline void print_board(board_t board) {
    int i,j;
    for(i=0; i<4; i++) {
        for(j=0; j<4; j++) {
            uint8_t powerVal = (board) & 0xf;
            //printf("%6u", (powerVal == 0) ? 0 : 1 << powerVal);
            board >>= 4;
        }
        //printf("\n");
    }
    //printf("\n");
}

static inline board_t unpack_col(row_t row) {
    board_t tmp = row;
    return (tmp | (tmp << 12ULL) | (tmp << 24ULL) | (tmp << 36ULL)) & COL_MASK;
}

static inline row_t reverse_row(row_t row) {
    return (row >> 12) | ((row >> 4) & 0x00F0)  | ((row << 4) & 0x0F00) | (row << 12);
}

/* Functions */
#ifdef __cplusplus
extern "C" {
#endif

 void init_tables();
 board_t execute_move(int move, board_t board);

typedef int (*get_move_func_t)(board_t);
 float score_toplevel_move(board_t board, int move);
 int find_best_move(board_t board);
 int ask_for_move(board_t board);
 void play_game(get_move_func_t get_move);

#ifdef __cplusplus
}
#endif


#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>






/* MSVC compatibility: undefine max and min macros */
#if defined(max)
#undef max
#endif

#if defined(min)
#undef min
#endif

// Transpose rows/columns in a board:
//   0123       048c
//   4567  -->  159d
//   89ab       26ae
//   cdef       37bf
static inline board_t transpose(board_t x)
{
    board_t a1 = x & 0xF0F00F0FF0F00F0FULL;
    board_t a2 = x & 0x0000F0F00000F0F0ULL;
    board_t a3 = x & 0x0F0F00000F0F0000ULL;
    board_t a = a1 | (a2 << 12) | (a3 >> 12);
    board_t b1 = a & 0xFF00FF0000FF00FFULL;
    board_t b2 = a & 0x00FF00FF00000000ULL;
    board_t b3 = a & 0x00000000FF00FF00ULL;
    return b1 | (b2 >> 24) | (b3 << 24);
}

// Count the number of empty positions (= zero nibbles) in a board.
// Precondition: the board cannot be fully empty.
static int count_empty(board_t x)
{
    x |= (x >> 2) & 0x3333333333333333ULL;
    x |= (x >> 1);
    x = ~x & 0x1111111111111111ULL;
    // At this point each nibble is:
    //  0 if the original nibble was non-zero
    //  1 if the original nibble was zero
    // Next sum them all
    x += x >> 32;
    x += x >> 16;
    x += x >>  8;
    x += x >>  4; // this can overflow to the next nibble if there were 16 empty positions
    return x & 0xf;
}

/* We can perform state lookups one row at a time by using arrays with 65536 entries. */

/* Move tables. Each row or compressed column is mapped to (oldrow^newrow) assuming row/col 0.
 *
 * Thus, the value is 0 if there is no move, and otherwise equals a value that can easily be
 * xor'ed into the current board state to update the board. */
static row_t row_left_table [65536];
static row_t row_right_table[65536];
static board_t col_up_table[65536];
static board_t col_down_table[65536];
static float heur_score_table[65536];
static float score_table[65536];

// Heuristic scoring settings
static const float SCORE_LOST_PENALTY = 200000.0f;
static const float SCORE_MONOTONICITY_POWER = 4.0f;
static const float SCORE_MONOTONICITY_WEIGHT = 47.0f;
static const float SCORE_SUM_POWER = 3.5f;
static const float SCORE_SUM_WEIGHT = 11.0f;
static const float SCORE_MERGES_WEIGHT = 700.0f;
static const float SCORE_EMPTY_WEIGHT = 270.0f;

void init_tables() {
    for (unsigned row = 0; row < 65536; ++row) {
        unsigned line[4] = {
                (row >>  0) & 0xf,
                (row >>  4) & 0xf,
                (row >>  8) & 0xf,
                (row >> 12) & 0xf
        };

        // Score
        float score = 0.0f;
        for (int i = 0; i < 4; ++i) {
            int rank = line[i];
            if (rank >= 2) {
                // the score is the total sum of the tile and all intermediate merged tiles
                score += (rank - 1) * (1 << rank);
            }
        }
        score_table[row] = score;


        // Heuristic score
        float sum = 0;
        int empty = 0;
        int merges = 0;

        int prev = 0;
        int counter = 0;
        for (int i = 0; i < 4; ++i) {
            int rank = line[i];
            sum += pow(rank, SCORE_SUM_POWER);
            if (rank == 0) {
                empty++;
            } else {
                if (prev == rank) {
                    counter++;
                } else if (counter > 0) {
                    merges += 1 + counter;
                    counter = 0;
                }
                prev = rank;
            }
        }
        if (counter > 0) {
            merges += 1 + counter;
        }

        float monotonicity_left = 0;
        float monotonicity_right = 0;
        for (int i = 1; i < 4; ++i) {
            if (line[i-1] > line[i]) {
                monotonicity_left += pow(line[i-1], SCORE_MONOTONICITY_POWER) - pow(line[i], SCORE_MONOTONICITY_POWER);
            } else {
                monotonicity_right += pow(line[i], SCORE_MONOTONICITY_POWER) - pow(line[i-1], SCORE_MONOTONICITY_POWER);
            }
        }

        heur_score_table[row] = SCORE_LOST_PENALTY +
            SCORE_EMPTY_WEIGHT * empty +
            SCORE_MERGES_WEIGHT * merges -
            SCORE_MONOTONICITY_WEIGHT * std::min(monotonicity_left, monotonicity_right) -
            SCORE_SUM_WEIGHT * sum;

        // execute a move to the left
        for (int i = 0; i < 3; ++i) {
            int j;
            for (j = i + 1; j < 4; ++j) {
                if (line[j] != 0) break;
            }
            if (j == 4) break; // no more tiles to the right

            if (line[i] == 0) {
                line[i] = line[j];
                line[j] = 0;
                i--; // retry this entry
            } else if (line[i] == line[j]) {
                if(line[i] != 0xf) {
                    /* Pretend that 32768 + 32768 = 32768 (representational limit). */
                    line[i]++;
                }
                line[j] = 0;
            }
        }

        row_t result = (line[0] <<  0) |
                       (line[1] <<  4) |
                       (line[2] <<  8) |
                       (line[3] << 12);
        row_t rev_result = reverse_row(result);
        unsigned rev_row = reverse_row(row);

        row_left_table [    row] =                row  ^                result;
        row_right_table[rev_row] =            rev_row  ^            rev_result;
        col_up_table   [    row] = unpack_col(    row) ^ unpack_col(    result);
        col_down_table [rev_row] = unpack_col(rev_row) ^ unpack_col(rev_result);
    }
}

static inline board_t execute_move_0(board_t board) {
    board_t ret = board;
    board_t t = transpose(board);
    ret ^= col_up_table[(t >>  0) & ROW_MASK] <<  0;
    ret ^= col_up_table[(t >> 16) & ROW_MASK] <<  4;
    ret ^= col_up_table[(t >> 32) & ROW_MASK] <<  8;
    ret ^= col_up_table[(t >> 48) & ROW_MASK] << 12;
    return ret;
}

static inline board_t execute_move_1(board_t board) {
    board_t ret = board;
    board_t t = transpose(board);
    ret ^= col_down_table[(t >>  0) & ROW_MASK] <<  0;
    ret ^= col_down_table[(t >> 16) & ROW_MASK] <<  4;
    ret ^= col_down_table[(t >> 32) & ROW_MASK] <<  8;
    ret ^= col_down_table[(t >> 48) & ROW_MASK] << 12;
    return ret;
}

static inline board_t execute_move_2(board_t board) {
    board_t ret = board;
    ret ^= board_t(row_left_table[(board >>  0) & ROW_MASK]) <<  0;
    ret ^= board_t(row_left_table[(board >> 16) & ROW_MASK]) << 16;
    ret ^= board_t(row_left_table[(board >> 32) & ROW_MASK]) << 32;
    ret ^= board_t(row_left_table[(board >> 48) & ROW_MASK]) << 48;
    return ret;
}

static inline board_t execute_move_3(board_t board) {
    board_t ret = board;
    ret ^= board_t(row_right_table[(board >>  0) & ROW_MASK]) <<  0;
    ret ^= board_t(row_right_table[(board >> 16) & ROW_MASK]) << 16;
    ret ^= board_t(row_right_table[(board >> 32) & ROW_MASK]) << 32;
    ret ^= board_t(row_right_table[(board >> 48) & ROW_MASK]) << 48;
    return ret;
}

/* Execute a move. */
board_t execute_move(int move, board_t board) {
    switch(move) {
    case 0: // up
        return execute_move_0(board);
    case 1: // down
        return execute_move_1(board);
    case 2: // left
        return execute_move_2(board);
    case 3: // right
        return execute_move_3(board);
    default:
        return ~0ULL;
    }
}

static inline int get_max_rank(board_t board) {
    int maxrank = 0;
    while (board) {
        maxrank = std::max(maxrank, int(board & 0xf));
        board >>= 4;
    }
    return maxrank;
}

static inline int count_distinct_tiles(board_t board) {
    uint16_t bitset = 0;
    while (board) {
        bitset |= 1<<(board & 0xf);
        board >>= 4;
    }

    // Don't count empty tiles.
    bitset >>= 1;

    int count = 0;
    while (bitset) {
        bitset &= bitset - 1;
        count++;
    }
    return count;
}

/* Optimizing the game */

struct eval_state {
    int maxdepth;
    int curdepth;
    int cachehits;
    unsigned long moves_evaled;
    int depth_limit;

    eval_state() : maxdepth(0), curdepth(0), cachehits(0), moves_evaled(0), depth_limit(0) {
    }
};

// score a single board heuristically
static float score_heur_board(board_t board);
// score a single board actually (adding in the score from spawned 4 tiles)
static float score_board(board_t board);
// score over all possible moves
static float score_move_node(eval_state &state, board_t board, float cprob);
// score over all possible tile choices and placements
static float score_tilechoose_node(eval_state &state, board_t board, float cprob);


static float score_helper(board_t board, const float* table) {
    return table[(board >>  0) & ROW_MASK] +
           table[(board >> 16) & ROW_MASK] +
           table[(board >> 32) & ROW_MASK] +
           table[(board >> 48) & ROW_MASK];
}

static float score_heur_board(board_t board) {
    return score_helper(          board , heur_score_table) +
           score_helper(transpose(board), heur_score_table);
}

static float score_board(board_t board) {
    return score_helper(board, score_table);
}

// Statistics and controls
// cprob: cumulative probability
// don't recurse into a node with a cprob less than this threshold
static const float CPROB_THRESH_BASE = 0.0001f;
static const int CACHE_DEPTH_LIMIT  = 15;

static float score_tilechoose_node(eval_state &state, board_t board, float cprob) {
    if (cprob < CPROB_THRESH_BASE || state.curdepth >= state.depth_limit) {
        state.maxdepth = std::max(state.curdepth, state.maxdepth);
        return score_heur_board(board);
    }
    if (state.curdepth < CACHE_DEPTH_LIMIT) {
        int rem_depth = state.depth_limit - state.curdepth;
        init_global_tt();
        uint64_t hash = board;
        hash ^= hash >> 30;
        hash *= 0xbf58476d1ce4e5b9ULL;
        hash ^= hash >> 27;
        hash *= 0x94d049bb133111ebULL;
        hash ^= hash >> 31;
        uint32_t idx = hash & TT_MASK;
        trans_table_entry_t entry = global_tt[idx];
        if (entry.key == board) {
            if(entry.rem_depth >= rem_depth) {
                state.cachehits++;
                return entry.heuristic;
            }
        }
    }

    int num_open = count_empty(board);
    cprob /= num_open;

    float res = 0.0f;
    board_t tmp = board;
    board_t tile_2 = 1;
    while (tile_2) {
        if ((tmp & 0xf) == 0) {
            res += score_move_node(state, board |  tile_2      , cprob * 0.9f) * 0.9f;
            res += score_move_node(state, board | (tile_2 << 1), cprob * 0.1f) * 0.1f;
        }
        tmp >>= 4;
        tile_2 <<= 4;
    }
    res = res / num_open;

    if (state.curdepth < CACHE_DEPTH_LIMIT) {
        int rem_depth = state.depth_limit - state.curdepth;
        init_global_tt();
        uint64_t hash = board;
        hash ^= hash >> 30;
        hash *= 0xbf58476d1ce4e5b9ULL;
        hash ^= hash >> 27;
        hash *= 0x94d049bb133111ebULL;
        hash ^= hash >> 31;
        uint32_t idx = hash & TT_MASK;
        if (global_tt[idx].key != board || rem_depth >= global_tt[idx].rem_depth) {
            global_tt[idx].key = board;
            global_tt[idx].rem_depth = static_cast<uint8_t>(rem_depth);
            global_tt[idx].heuristic = res;
        }
    }

    return res;
}

static float score_move_node(eval_state &state, board_t board, float cprob) {
    float best = 0.0f;
    state.curdepth++;
    for (int move = 0; move < 4; ++move) {
        board_t newboard = execute_move(move, board);
        state.moves_evaled++;

        if (board != newboard) {
            best = std::max(best, score_tilechoose_node(state, newboard, cprob));
        }
    }
    state.curdepth--;

    return best;
}

static float _score_toplevel_move(eval_state &state, board_t board, int move) {
    //int maxrank = get_max_rank(board);
    board_t newboard = execute_move(move, board);

    if(board == newboard)
        return 0;

    return score_tilechoose_node(state, newboard, 1.0f) + 1e-6;
}

float score_toplevel_move(board_t board, int move) {
    float res;
    
    
    eval_state state;
    state.depth_limit = std::max(3, count_distinct_tiles(board) - 2);

    
    res = _score_toplevel_move(state, board, move);
    

    double elapsed = 0;
    

    
    return res;
}

/* Find the best move for a given board. */
int find_best_move(board_t board) {
    int move;
    float best = 0;
    int bestmove = -1;

    
    //printf("Current scores: heur %.0f, actual %.0f\n", score_heur_board(board), score_board(board));

    for(move=0; move<4; move++) {
        float res = score_toplevel_move(board, move);

        if(res > best) {
            best = res;
            bestmove = move;
        }
    }

    return bestmove;
}

int ask_for_move(board_t board) {
    int move;
    char validstr[5];
    char *validpos = validstr;

    

    for(move=0; move<4; move++) {
        if(execute_move(move, board) != board)
            *validpos++ = "UDLR"[move];
    }
    *validpos = 0;
    if(validpos == validstr)
        return -1;

    while(1) {
        char movestr[64];
        const char *allmoves = "UDLR";

        //printf("Move [%s]? ", validstr);

        if(!fgets(movestr, sizeof(movestr)-1, stdin))
            return -1;

        if(!strchr(validstr, toupper(movestr[0]))) {
            //printf("Invalid move.\n");
            continue;
        }

        return strchr(allmoves, toupper(movestr[0])) - allmoves;
    }
}

/* Playing the game */
static board_t draw_tile() {
    return (unif_random(10) < 9) ? 1 : 2;
}

static board_t insert_tile_rand(board_t board, board_t tile) {
    int index = unif_random(count_empty(board));
    board_t tmp = board;
    while (true) {
        while ((tmp & 0xf) != 0) {
            tmp >>= 4;
            tile <<= 4;
        }
        if (index == 0) break;
        --index;
        tmp >>= 4;
        tile <<= 4;
    }
    return board | tile;
}

static board_t initial_board() {
    board_t board = draw_tile() << (4 * unif_random(16));
    return insert_tile_rand(board, draw_tile());
}

void play_game(get_move_func_t get_move) {
    board_t board = initial_board();
    int moveno = 0;
    int scorepenalty = 0; // "penalty" for obtaining free 4 tiles

    while(1) {
        int move;
        board_t newboard;

        for(move = 0; move < 4; move++) {
            if(execute_move(move, board) != board)
                break;
        }
        if(move == 4)
            break; // no legal moves

        //printf("\nMove #%d, current score=%.0f\n", ++moveno, score_board(board) - scorepenalty);

        move = get_move(board);
        if(move < 0)
            break;

        newboard = execute_move(move, board);
        if(newboard == board) {
            //printf("Illegal move!\n");
            moveno--;
            continue;
        }

        board_t tile = draw_tile();
        if (tile == 2) scorepenalty += 4;
        board = insert_tile_rand(newboard, tile);
    }

    
    //printf("\nGame over. Your score is %.0f. The highest rank you achieved was %d.\n", score_board(board) - scorepenalty, get_max_rank(board));
}




EXPORT int get_best_move_c(int* flat_grid) {
    static bool init = false;
    if (!init) {
        srand(time(NULL));
        init_tables();
        init = true;
    }
    
    board_t board = 0;
    for (int i = 0; i < 16; i++) {
        uint64_t tile = get_log2(flat_grid[i]);
        board |= (tile << (4 * i));
    }
    
    // nneonneo's find_best_move maps:
    // 0: Up
    // 1: Down
    // 2: Left
    // 3: Right
    
    // This perfectly matches our Python game2048.py:
    // UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3
    
    int move = find_best_move(board);
    return move;
}
