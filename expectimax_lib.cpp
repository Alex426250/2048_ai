#include <iostream>
#include <cmath>
#include <unordered_map>
#include <cstring>
#include <functional>
#include <cstdint>

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif

// 定义缓存状态字典
struct CacheState {
    int grid[16];
    int depth;
    bool is_player;
    double cprob; // 恢复 cprob
    
    // 构造函数，用以清除结构体内编译器由于对齐而生成的3字节 padding (垃圾数据越界访问的隐患)
    CacheState() {
        std::memset(this, 0, sizeof(CacheState));
    }

    bool operator==(const CacheState& other) const {
        if (depth != other.depth || is_player != other.is_player) return false;
        
        // 必须与 Hash 函数保证完全一致的量化判断逻辑（严格保持等价的传递性）
        // 否则会破坏 C++ STL 底层哈希桶的指针分配，直接导致 Access Violation (0x2F) 的终极元凶
        uint64_t c1 = static_cast<uint64_t>(std::round(cprob * 1e8));
        uint64_t c2 = static_cast<uint64_t>(std::round(other.cprob * 1e8));
        if (c1 != c2) return false;
        
        return std::memcmp(grid, other.grid, 16 * sizeof(int)) == 0;
    }
};

struct CacheStateHash {
    std::size_t operator()(const CacheState& s) const {
        std::size_t h = 0;
        for (int i = 0; i < 16; ++i) {
            h ^= std::hash<int>()(s.grid[i]) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        h ^= std::hash<int>()(s.depth) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>()(s.is_player) + 0x9e3779b9 + (h << 6) + (h >> 2);
        
        // 强制降维量化为一个极大整数，配合 round 抹平极微小的位级浮点波动
        uint64_t cprob_quantized = static_cast<uint64_t>(std::round(s.cprob * 1e8));
        h ^= std::hash<uint64_t>()(cprob_quantized) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

std::unordered_map<CacheState, double, CacheStateHash> transposition_table;

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
                                if (r == 1) {
                                    monotonicity_penalty -= diff * 5.0; // 致死级惩罚死角倒挂
                                } else {
                                monotonicity_penalty -= diff * 5.0; 
                                }
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
                                    if (r == 0) {
                                        monotonicity_penalty -= diff * 50.0; // 致死级惩罚死角倒挂
                                    } else {
                                    monotonicity_penalty -= diff * 50.0;
                                    } // 致死级惩罚死角倒挂
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

    // 防爆内存安全锁：完全释放哈希表的 Bucket 内存，而不仅仅是清空元素，预防底层 bad_alloc 导致的库崩溃
    if (transposition_table.size() > 2000000) {
        std::unordered_map<CacheState, double, CacheStateHash>().swap(transposition_table);
    }

    CacheState state; // 这里由于有了我们的新构造函数，内存已经是干净全 0 的了
    std::memcpy(state.grid, grid, 16 * sizeof(int));
    state.depth = depth;
    state.is_player = is_player_turn;
    state.cprob = cprob;

    auto it = transposition_table.find(state);
    if (it != transposition_table.end()) {
        return it->second;
    }

    double result = 0;

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
            result = -1e15; 
        } else {
            result = best_score;
        }
    } else {
        int empty_cells[16];
        int num_empty = 0;
        for (int i = 0; i < 16; i++) {
            if (grid[i] == 0) {
                empty_cells[num_empty++] = i;
            }
        }
        
        if (num_empty == 0) {
            result = evaluate_board(grid);
        } else {
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
            result = expected_score;
        }
    }

    transposition_table[state] = result;
    return result;
}

// 供 Python ctypes 调用的主入口
EXPORT int get_best_move_c(int* flat_grid) {
    std::unordered_map<CacheState, double, CacheStateHash>().swap(transposition_table); // 每次调用彻底清空缓存并释放底层 Bucket 内存

    int empty_count = 0;
    for (int i = 0; i < 16; i++) {
        if (flat_grid[i] == 0) empty_count++;
    }
    
    int current_depth;
    if (empty_count >= 8) {
        current_depth = 7;
    } else if (empty_count >= 5) {
        current_depth = 8;
    } else {
        current_depth = 9;
    }
    
    double best_score = -1e15;
    int best_move = -1;
    
    // Fallback，防止完全没路或者所有路的分数全都是极惩罚分数-1e15，导致 best_move 还是 -1 不走
    int first_valid_move = -1; 
    
    int new_grid[16];
    
    for (int direction = 0; direction < 4; direction++) {
        if (simulate_move(flat_grid, direction, new_grid)) {
            // 只要有任何方向可走，将其记录为打底退路
            if (first_valid_move == -1) {
                first_valid_move = direction;
            }
            
            // 恢复起始累计概率为 1.0
            double score = expectimax(new_grid, current_depth, 1.0, false);
            // printf("Move %d score: %f\n", direction, score);
            if (score > best_score) {
                best_score = score;
                best_move = direction;
            }
        }
    }
    
    // 如果所有有效移动的 score 都是 -1e15（被认定为必死），best_move 将未能被更新(依然是-1)。
    // 这种情况下，我们强制走我们发现的第一个有效移动，站着死不如走完最后一步。
    if (best_move == -1 && first_valid_move != -1) {
        best_move = first_valid_move;
    }
    
    return best_move;
}
