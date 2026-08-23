#ifndef _2048_ENGINE_CORE_H_
#define _2048_ENGINE_CORE_H_

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <set>
#include <array>
#include <cstdint>
#include <atomic>
#include <string>
#include <cstring>
#ifdef _OPENMP
#include <omp.h>
#endif

using namespace std;
typedef pair<int, int> (*TransformFunc)(int, int);
pair<int, int> trans_0(int r, int c) { return {r, c}; }
pair<int, int> trans_1(int r, int c) { return {r, 3 - c}; }
pair<int, int> trans_2(int r, int c) { return {2 - r, c}; }
pair<int, int> trans_3(int r, int c) { return {2 - r, 3-c}; }
typedef uint64_t board_t;
TransformFunc trans[4] = { trans_0, trans_1, trans_2, trans_3 };

class BitBoardEngine {
public:
    static constexpr int ROWS = 3;
    static constexpr int COLS = 4;
    static constexpr int CELLS = ROWS * COLS;

    static uint16_t row_left_table[65536];
    static uint16_t row_right_table[65536];
    static int row_score_table[65536];
    static uint16_t col_up_table[4096];
    static uint16_t col_down_table[4096];
    static int col_score_table[4096];
    static bool inited;

    static void merge_line(int* line, int len, int& score) {
        int out[4] = {0, 0, 0, 0};
        int out_n = 0;
        for (int i = 0; i < len; i++) if (line[i] != 0) out[out_n++] = line[i];
        int merged[4] = {0, 0, 0, 0};
        int m = 0;
        for (int i = 0; i < out_n; i++) {
            if (i +1 < out_n && out[i] == out[i + 1]) {
                if (out[i] != 0xF) {
                    merged[m] = out[i] + 1;
                    score += (1 << merged[m]);
                } else merged[m] = out[i];
                i++;
            } else merged[m] = out[i];
            m++;
        }

        for (int i = 0; i < len; i++) line[i] = merged[i];
    }

    static uint16_t reverse_row(uint16_t row) {
        return (uint16_t)(((row >>12) & 0xF) | (((row >> 8) & 0xF) << 4) | (((row >> 4) & 0xF) << 8) | ((row & 0xF) << 12));
    }

    static uint16_t reverse_col(uint16_t col) {
        return (uint16_t)(((col >> 8) & 0xF) | (((col >> 4) & 0xF) << 4) | ((col & 0xF) << 8));
    }

    static void init_tables() {
        if (inited) return;

        for (int row = 0; row < 65536; ++row) {
            int line_left[4] = {(row >> 0) & 0xF,(row >> 4) & 0xF,(row >> 8) & 0xF,(row >> 12) & 0xF};
            int score = 0;
            merge_line(line_left, 4, score);
            uint16_t left = (uint16_t)((line_left[0] << 0) | (line_left[1] << 4) | (line_left[2] << 8) | (line_left[3] << 12));

            uint16_t rev = reverse_row((uint16_t)row);
            int line_right[4] = {(rev >> 0) & 0xF, (rev >> 4) & 0xF, (rev >> 8) & 0xF, (rev >> 12) & 0xF};
            int unused_score = 0;
            merge_line(line_right, 4, unused_score);
            uint16_t right_left_space = (uint16_t)((line_right[0] << 0) | (line_right[1] << 4) | (line_right[2] << 8) | (line_right[3] << 12));

            row_left_table[row] = left;
            row_right_table[row] = reverse_row(right_left_space);
            row_score_table[row] = score;
        }

        for (int col = 0; col < 4096; ++col) {
            int line_up[3] = {(col >> 0) & 0xF,(col >> 4) & 0xF,(col >> 8) & 0xF};
            int score = 0;
            merge_line(line_up, 3, score);
            uint16_t up = (uint16_t)((line_up[0] << 0) | (line_up[1] << 4) | (line_up[2] << 8));

            uint16_t rev = reverse_col((uint16_t)col);
            int line_down[3] = {(rev >> 0) & 0xF, (rev >> 4) & 0xF, (rev >> 8) & 0xF};
            int unused_score = 0;
            merge_line(line_down, 3, unused_score);
            uint16_t down_up_space = (uint16_t)((line_down[0] << 0) | (line_down[1] << 4) | (line_down[2] << 8));

            col_up_table[col] = up;
            col_down_table[col] = reverse_col(down_up_space);
            col_score_table[col] = score;
        }

        inited = true;
    }

    static inline board_t get_cell(board_t board, int idx) {
        return (board >> (idx * 4)) & 0xFULL;
    }

    static inline board_t set_cell(board_t board, int idx, board_t value) {
        const int shift = idx * 4;
        board &= ~(0xFULL << shift);
        board |= (value & 0xFULL) << shift;
        return board;
    }
    
    static inline board_t execute_move(board_t board, int dir, int& inc) {
        inc = 0;
        board_t res = 0;

        if (dir == 2 || dir == 3) {
            for (int r = 0; r < ROWS; r++) {
                const int shift = r * COLS * 4;
                uint16_t row = (uint16_t)((board >> shift) & 0xFFFF);
                uint16_t moved = dir == 2 ? row_left_table[row] : row_right_table[row];
                res |= ((board_t)moved << shift);
                inc += row_score_table[row];
            }
            return res;
        }

        for (int c = 0; c < COLS; c++) {
            uint16_t col = 0;
            for (int r = 0; r < ROWS; r++) col |= (uint16_t)(get_cell(board, r * COLS + c) << (r * 4));
            uint16_t moved = dir == 0 ? col_up_table[col] : col_down_table[col];
            inc += col_score_table[col];
            for (int r = 0; r < ROWS; r++) res = set_cell(res, r * COLS + c, (moved >> (r * 4)) & 0xF);
        }
        return res;
    }
};

uint16_t BitBoardEngine::row_left_table[65536];
uint16_t BitBoardEngine::row_right_table[65536];
int BitBoardEngine::row_score_table[65536];
uint16_t BitBoardEngine::col_up_table[4096];
uint16_t BitBoardEngine::col_down_table[4096];
int BitBoardEngine::col_score_table[4096];
bool BitBoardEngine::inited = false;

class FastGame {
public:
    board_t board;
    int score;

    FastGame() {
        board = 0;
        score = 0;
        add_tile();
        add_tile();
    }

    void add_tile() {
        int empty[12];
        int count = 0;
        for (int i=0; i<12; i++) if (((board >> (i * 4)) & 0xF) == 0) empty[count++] = i;
        if (count > 0) {
            uint64_t val = ((rand() % 10) < 9) ? 1ULL : 2ULL;
            board |= (val << (empty[rand() % count] * 4));
        }
    }

    bool is_over() {
        for (int i=0; i<12; i++) if (((board >> (i * 4)) & 0xF) == 0) return false;
        for (int dir=0; dir<4; dir++) {
            int inc;
            board_t ng = BitBoardEngine::execute_move(board, dir, inc);
            if (ng != board) return false;
        }
        return true;
    }

    bool move(int dir) {
        int inc;
        board_t ng = BitBoardEngine::execute_move(board, dir, inc);
        if (ng != board) {
            board = ng;
            score += inc;
            add_tile();
            return true;
        }
        return false;
    }
};

#endif 
