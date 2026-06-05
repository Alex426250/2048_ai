#pragma once
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
#ifdef _OPENMP
#include <omp.h>
#endif
using namespace std;
typedef pair<int, int> (*TransformFunc)(int, int);
pair<int, int> trans_0(int r, int c) { return {r, c}; }
pair<int, int> trans_1(int r, int c) { return {c, 3 - r}; }
pair<int, int> trans_2(int r, int c) { return {3 - r, 3 - c}; }
pair<int, int> trans_3(int r, int c) { return {3 - c, r}; }
pair<int, int> trans_4(int r, int c) { return {r, 3 - c}; }
pair<int, int> trans_5(int r, int c) { return {3 - r, c}; }
pair<int, int> trans_6(int r, int c) { return {c, r}; }
pair<int, int> trans_7(int r, int c) { return {3 - c, 3 - r}; }
typedef uint64_t board_t;

class BitBoardEngine {
public:
    static uint16_t row_left_table[65536];
    static uint16_t row_right_table[65536];
    static int row_score_table[65536];
    static bool inited;
    static void init_tables() {
        if (inited) return;
        for (int row = 0; row < 65536; ++row) {
            int line[4] = {(row >> 0) & 0xF,(row >> 4) & 0xF,(row >> 8) & 0xF,(row >> 12) & 0xF};
            int score = 0;
            int i, j;
            for (i = 0; i < 3; ++i) {
                for (j = i + 1; j < 4; ++j) if (line[j] != 0) break;
                if (j == 4) break; 
                if (line[i] == 0) {
                    line[i] = line[j];
                    line[j] = 0;
                    i--;
                } else if (line[i] == line[j]) {
                    if (line[i] != 0xF) { 
                        line[i]++;
                        score += (1 << line[i]);
                    }
                    line[j] = 0;
                }
            }
            uint16_t res_left = (line[0] << 0) | (line[1] << 4) | (line[2] << 8) | (line[3] << 12);
            row_left_table[row] = res_left;
            row_score_table[row] = score;
            int rev_row = ((row >> 12) & 0xF) | (((row >> 8) & 0xF) << 4) | (((row >> 4) & 0xF) << 8) | ((row & 0xF) << 12);
            int rev_res = ((res_left >> 12) & 0xF) | (((res_left >> 8) & 0xF) << 4) | (((res_left >> 4) & 0xF) << 8) | ((res_left & 0xF) << 12);
            row_right_table[rev_row] = rev_res;
        }
        inited = true;
    }

    static inline board_t transpose(board_t x) {
        board_t a1 = x & 0xF0F00F0FF0F00F0FULL;
        board_t a2 = x & 0x0000F0F00000F0F0ULL;
        board_t a3 = x & 0x0F0F00000F0F0000ULL;
        board_t a = a1 | (a2 << 12) | (a3 >> 12);
        board_t b1 = a & 0xFF00FF0000FF00FFULL;
        board_t b2 = a & 0x00000000FF00FF00ULL;
        board_t b3 = a & 0x00FF00FF00000000ULL;
        return b1 | (b2 << 24) | (b3 >> 24);
    }
    
    static inline board_t execute_move(board_t board, int dir, int& inc) {
        inc = 0;
        board_t res = board;
        if (dir == 0) { // Up
            board_t t = transpose(board);
            res = ((board_t)row_left_table[(t >> 0) & 0xFFFF] << 0) |
                  ((board_t)row_left_table[(t >> 16) & 0xFFFF] << 16) |
                  ((board_t)row_left_table[(t >> 32) & 0xFFFF] << 32) |
                  ((board_t)row_left_table[(t >> 48) & 0xFFFF] << 48);
            res = transpose(res);
            inc += row_score_table[(t >> 0) & 0xFFFF] + row_score_table[(t >> 16) & 0xFFFF] +
                   row_score_table[(t >> 32) & 0xFFFF] + row_score_table[(t >> 48) & 0xFFFF];
        }
        else if (dir == 1) { // Down
            board_t t = transpose(board);
            res = ((board_t)row_right_table[(t >> 0) & 0xFFFF] << 0) |
                  ((board_t)row_right_table[(t >> 16) & 0xFFFF] << 16) |
                  ((board_t)row_right_table[(t >> 32) & 0xFFFF] << 32) |
                  ((board_t)row_right_table[(t >> 48) & 0xFFFF] << 48);
            res = transpose(res);
            inc += row_score_table[(t >> 0) & 0xFFFF] + row_score_table[(t >> 16) & 0xFFFF] +
                   row_score_table[(t >> 32) & 0xFFFF] + row_score_table[(t >> 48) & 0xFFFF];
        }
        else if (dir == 2) { // Left
            res = ((board_t)row_left_table[(board >> 0) & 0xFFFF] << 0) |
                  ((board_t)row_left_table[(board >> 16) & 0xFFFF] << 16) |
                  ((board_t)row_left_table[(board >> 32) & 0xFFFF] << 32) |
                  ((board_t)row_left_table[(board >> 48) & 0xFFFF] << 48);
            inc += row_score_table[(board >> 0) & 0xFFFF] + row_score_table[(board >> 16) & 0xFFFF] +
                   row_score_table[(board >> 32) & 0xFFFF] + row_score_table[(board >> 48) & 0xFFFF];
        }
        else if (dir == 3) { // Right
            res = ((board_t)row_right_table[(board >> 0) & 0xFFFF] << 0) |
                  ((board_t)row_right_table[(board >> 16) & 0xFFFF] << 16) |
                  ((board_t)row_right_table[(board >> 32) & 0xFFFF] << 32) |
                  ((board_t)row_right_table[(board >> 48) & 0xFFFF] << 48);
            inc += row_score_table[(board >> 0) & 0xFFFF] + row_score_table[(board >> 16) & 0xFFFF] +
                   row_score_table[(board >> 32) & 0xFFFF] + row_score_table[(board >> 48) & 0xFFFF];
        }
        return res;
    }
};

uint16_t BitBoardEngine::row_left_table[65536];
uint16_t BitBoardEngine::row_right_table[65536];
int BitBoardEngine::row_score_table[65536];
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
        int empty[16];
        int count = 0;
        for (int i=0; i<16; i++) if (((board >> (i * 4)) & 0xF) == 0) empty[count++] = i;
        if (count > 0) {
            uint64_t val = ((rand() % 10) < 9) ? 1ULL : 2ULL;
            board |= (val << (empty[rand() % count] * 4));
        }
    }

    bool is_over() {
        for (int i=0; i<16; i++) if (((board >> (i * 4)) & 0xF) == 0) return false;
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

