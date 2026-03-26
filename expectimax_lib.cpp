#include <iostream>
#include <cmath>

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif

// 权重矩阵
const double flat_weight_matrix[16] = {
    pow(2,16), pow(2,15), pow(2,14), pow(2,13),
    pow(2,9), pow(2,10), pow(2,11), pow(2,12),
    pow(2,8),  pow(2,7),  pow(2,6),  pow(2,5),
    pow(2,1),  pow(2,2),  pow(2,3),  pow(2,4)
};

// 避免使用慢速的math.log2()函数
inline int get_log2(int val) {
    if (val == 0) return 0;
    int res = 0;
    while (val >>= 1) res++;
    return res;
}

// 模拟移动规则: 0:Up, 1:Down, 2:Left, 3:Right
bool simulate_move(const int grid[16], int direction, int new_grid[16]) {
    bool moved = false;
    for(int i = 0; i < 16; i++) new_grid[i] = 0;
    
    if (direction == 0) { // Up
        for (int c = 0; c < 4; c++) {
            int merged[4] = {0};
            int m_idx = 0;
            int last_val = 0;
            for (int r = 0; r < 4; r++) {
                int val = grid[r * 4 + c];
                if (val != 0) {
                    if (last_val == 0) {
                        last_val = val;
                    } else if (last_val == val) {
                        merged[m_idx++] = val * 2;
                        last_val = 0; 
                    } else {
                        merged[m_idx++] = last_val;
                        last_val = val;
                    }
                }
            }
            if (last_val != 0) merged[m_idx++] = last_val;
            
            for (int r = 0; r < 4; r++) {
                new_grid[r * 4 + c] = merged[r];
                if (grid[r * 4 + c] != merged[r]) moved = true;
            }
        }
    } else if (direction == 1) { // Down
        for (int c = 0; c < 4; c++) {
            int merged[4] = {0};
            int m_idx = 3;
            int last_val = 0;
            for (int r = 3; r >= 0; r--) {
                int val = grid[r * 4 + c];
                if (val != 0) {
                    if (last_val == 0) {
                        last_val = val;
                    } else if (last_val == val) {
                        merged[m_idx--] = val * 2;
                        last_val = 0;
                    } else {
                        merged[m_idx--] = last_val;
                        last_val = val;
                    }
                }
            }
            if (last_val != 0) merged[m_idx--] = last_val;
            
            for (int r = 0; r < 4; r++) {
                new_grid[r * 4 + c] = merged[r];
                if (grid[r * 4 + c] != merged[r]) moved = true;
            }
        }
    } else if (direction == 2) { // Left
        for (int r = 0; r < 4; r++) {
            int merged[4] = {0};
            int m_idx = 0;
            int last_val = 0;
            for (int c = 0; c < 4; c++) {
                int val = grid[r * 4 + c];
                if (val != 0) {
                    if (last_val == 0) {
                        last_val = val;
                    } else if (last_val == val) {
                        merged[m_idx++] = val * 2;
                        last_val = 0;
                    } else {
                        merged[m_idx++] = last_val;
                        last_val = val;
                    }
                }
            }
            if (last_val != 0) merged[m_idx++] = last_val;
            
            for (int c = 0; c < 4; c++) {
                new_grid[r * 4 + c] = merged[c];
                if (grid[r * 4 + c] != merged[c]) moved = true;
            }
        }
    } else if (direction == 3) { // Right
        for (int r = 0; r < 4; r++) {
            int merged[4] = {0};
            int m_idx = 3;
            int last_val = 0;
            for (int c = 3; c >= 0; c--) {
                int val = grid[r * 4 + c];
                if (val != 0) {
                    if (last_val == 0) {
                        last_val = val;
                    } else if (last_val == val) {
                        merged[m_idx--] = val * 2;
                        last_val = 0;
                    } else {
                        merged[m_idx--] = last_val;
                        last_val = val;
                    }
                }
            }
            if (last_val != 0) merged[m_idx--] = last_val;
            
            for (int c = 0; c < 4; c++) {
                new_grid[r * 4 + c] = merged[c];
                if (grid[r * 4 + c] != merged[c]) moved = true;
            }
        }
    }
    return moved;
}

double evaluate_board(const int grid[16]) {
    int empty_count = 0;
    double max_weight_score = 0;
    int max_val = 0;
    
    for (int i = 0; i < 16; i++) {
        int val = grid[i];
        if (val == 0) empty_count++;
        if (val > max_val) max_val = val;
        max_weight_score += val * flat_weight_matrix[i];
    }
    
    double smoothness_penalty = 0;
    double monotonicity_penalty = 0;
    
    double corner_penalty = 0;
    if (max_val > 0 && grid[0] != max_val) {
        corner_penalty = -5000000.0 - max_val * 1000.0; // 加倍惩罚，严禁离开左上角
    }
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int val_raw = grid[r * 4 + c];
            if (val_raw > 0) {
                // Right
                if (c < 3) {
                    int right_raw = grid[r * 4 + (c + 1)];
                    if (right_raw > 0) {
                        smoothness_penalty -= std::abs(val_raw - right_raw);
                        
                        // 第 0、2 行（偶数行）应该向右递减（左 > 右）
                        if (r % 2 == 0) {
                            if (val_raw < right_raw) {
                                // 倒挂了！加倍惩罚或者二次方级惩罚。特别是大块被挡住
                                double diff = (double)(right_raw - val_raw);
                                monotonicity_penalty -= diff * 5.0; // 加倍惩罚
                            }
                        } 
                        // 第 1、3 行（奇数行）必须向右递增！（左 < 右）
                        else {
                            if (val_raw > right_raw) {
                                double diff = (double)(val_raw - right_raw);
                                monotonicity_penalty -= diff * 5.0; 
                            }
                        }
                    }
                }
                
                // Down
                if (r < 3) {
                    int down_raw = grid[(r + 1) * 4 + c];
                    if (down_raw > 0) {
                        smoothness_penalty -= std::abs(val_raw - down_raw);
                        
                        // 修正：蛇形结构的垂直单调性不能一刀切全部要求 上 > 下。
                        // 在蛇形拐角处（例如第 0 行末尾，和第 1 行末尾），上 > 下 是正确的。
                        // 甚至我们可以精细化：偶数行最右侧必须 上 > 下，奇数行最左侧必须 上 > 下
                        if (r % 2 == 0) {
                            if (c == 3) { // 拐角在最右侧 (0,3) -> (1,3), 还有 (2,3) -> (3,3)
                                if (val_raw < down_raw) {
                                    double diff = (double)(down_raw - val_raw);
                                    monotonicity_penalty -= diff * 50.0; // 致死级惩罚死角倒挂
                                }
                            } else {
                                if (val_raw < down_raw) monotonicity_penalty -= (down_raw - val_raw) * 2;
                            }
                        } else {
                            if (c == 0) { // 拐角在最左侧 (1,0) -> (2,0)
                                if (val_raw < down_raw) {
                                    double diff = (double)(down_raw - val_raw);
                                    monotonicity_penalty -= diff * 50.0; 
                                }
                            } else {
                                if (val_raw < down_raw) monotonicity_penalty -= (down_raw - val_raw) * 2;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 综合评分：完全采用顶尖高分AI方案：平滑度和单调性根据方块真实值按比例扣分！
    return max_weight_score + (empty_count * 50000.0) + (smoothness_penalty * 200.0) + (monotonicity_penalty * 25000.0) + corner_penalty;
}

double expectimax(const int grid[16], int depth, double cprob, bool is_player_turn) {
    if (depth == 0 || cprob < 0.0001) {
        return evaluate_board(grid);
    }

    if (is_player_turn) {
        double best_score = -1e15;
        bool moved_any = false;
        int new_grid[16];
        
        for (int direction = 0; direction < 4; direction++) {
            if (simulate_move(grid, direction, new_grid)) {
                moved_any = true;
                double score = expectimax(new_grid, depth - 1, cprob, false);
                if (score > best_score) best_score = score;
            }
        }
        if (!moved_any) {
            // 游戏结束！没有任何合法移动，必须给予极度严厉的死亡惩罚
            return -1e15; 
        }
        return best_score;
    } else {
        int empty_cells[16];
        int num_empty = 0;
        for (int i = 0; i < 16; i++) {
            if (grid[i] == 0) {
                empty_cells[num_empty++] = i;
            }
        }
        
        if (num_empty == 0) return evaluate_board(grid);
        
        double expected_score = 0;
        double prob2 = 0.9 / num_empty;
        double prob4 = 0.1 / num_empty;
        
        for (int i = 0; i < num_empty; i++) {
            int idx = empty_cells[i];
            int next_grid[16];
            for (int k = 0; k < 16; k++) next_grid[k] = grid[k];
            
            // gen 2
            next_grid[idx] = 2;
            expected_score += prob2 * expectimax(next_grid, depth - 1, cprob * prob2, true);
            
            // gen 4
            next_grid[idx] = 4;
            expected_score += prob4 * expectimax(next_grid, depth - 1, cprob * prob4, true);
        }
        return expected_score;
    }
}

// 供 Python ctypes 调用的主入口
EXPORT int get_best_move_c(int* flat_grid) {
    int empty_count = 0;
    for (int i = 0; i < 16; i++) {
        if (flat_grid[i] == 0) empty_count++;
    }
    
    int current_depth;
    if (empty_count >= 8) {
        current_depth = 7
        ;
    } else if (empty_count >= 5) {
        current_depth = 8;
    } else {
        current_depth = 9;
    }
    
    double best_score = -1e15;
    int best_move = -1;
    int new_grid[16];
    
    for (int direction = 0; direction < 4; direction++) {
        if (simulate_move(flat_grid, direction, new_grid)) {
            // 起始累计概率 cprob = 1.0
            double score = expectimax(new_grid, current_depth, 1.0, false);
            // printf("Move %d score: %f\n", direction, score);
            if (score > best_score) {
                best_score = score;
                best_move = direction;
            }
        }
    }
    
    return best_move;
}