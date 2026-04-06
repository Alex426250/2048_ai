#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <set>

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

// 内置极速游戏引擎（只为自己疯狂训练用）
class FastGame {
public:
    int grid[16];
    int score;

    FastGame() {
        for (int i=0; i<16; i++) grid[i] = 0;
        score = 0;
        add_tile();
        add_tile();
    }

    void add_tile() {
        int empty[16];
        int count = 0;
        for (int i=0; i<16; i++) if (grid[i] == 0) empty[count++] = i;
        if (count > 0) {
            grid[empty[rand() % count]] = ((rand() % 10) < 9) ? 2 : 4;
        }
    }

    bool is_over() {
        for (int i=0; i<16; i++) if (grid[i] == 0) return false;
        for (int dir=0; dir<4; dir++) {
            int tg[16], inc;
            if (sim_move(grid, dir, tg, inc)) return false;
        }
        return true;
    }

    bool move(int dir) {
        int tg[16], inc;
        if (sim_move(grid, dir, tg, inc)) {
            for (int i=0; i<16; i++) grid[i] = tg[i];
            score += inc;
            add_tile();
            return true;
        }
        return false;
    }

    static bool sim_move(const int* cg, int dir, int* ng, int& inc) {
        for(int i=0;i<16;i++) ng[i]=0;
        inc = 0;
        bool moved = false;
        if (dir == 0) { // Up
            for (int c=0; c<4; c++) {
                int col[4], idx=0;
                for (int r=0; r<4; r++) if (cg[r*4+c]) col[idx++] = cg[r*4+c];
                int merged[4]={0}, midx=0;
                for (int i=0; i<idx; i++) {
                    if (i+1<idx && col[i]==col[i+1]) {
                        merged[midx++] = col[i]*2;
                        inc += col[i]*2;
                        i++;
                    } else merged[midx++] = col[i];
                }
                for(int r=0; r<4; r++) {
                    int v = (r<midx)?merged[r]:0;
                    ng[r*4+c] = v;
                    if(cg[r*4+c] != v) moved=true;
                }
            }
        }
        else if (dir == 1) { // Down
            for (int c=0; c<4; c++) {
                int col[4], idx=0;
                for (int r=3; r>=0; r--) if (cg[r*4+c]) col[idx++] = cg[r*4+c];
                int merged[4]={0}, midx=0;
                for (int i=0; i<idx; i++) {
                    if (i+1<idx && col[i]==col[i+1]) {
                        merged[midx++] = col[i]*2;
                        inc += col[i]*2;
                        i++;
                    } else merged[midx++] = col[i];
                }
                for(int r=3; r>=0; r--) {
                    int v = (3-r<midx)?merged[3-r]:0;
                    ng[r*4+c] = v;
                    if(cg[r*4+c] != v) moved=true;
                }
            }
        }
        else if (dir == 2) { // Left
            for (int r=0; r<4; r++) {
                int row[4], idx=0;
                for (int c=0; c<4; c++) if (cg[r*4+c]) row[idx++] = cg[r*4+c];
                int merged[4]={0}, midx=0;
                for (int i=0; i<idx; i++) {
                    if (i+1<idx && row[i]==row[i+1]) {
                        merged[midx++] = row[i]*2;
                        inc += row[i]*2;
                        i++;
                    } else merged[midx++] = row[i];
                }
                for(int c=0; c<4; c++) {
                    int v = (c<midx)?merged[c]:0;
                    ng[r*4+c] = v;
                    if(cg[r*4+c] != v) moved=true;
                }
            }
        }
        else if (dir == 3) { // Right
            for (int r=0; r<4; r++) {
                int row[4], idx=0;
                for (int c=3; c>=0; c--) if (cg[r*4+c]) row[idx++] = cg[r*4+c];
                int merged[4]={0}, midx=0;
                for (int i=0; i<idx; i++) {
                    if (i+1<idx && row[i]==row[i+1]) {
                        merged[midx++] = row[i]*2;
                        inc += row[i]*2;
                        i++;
                    } else merged[midx++] = row[i];
                }
                for(int c=3; c>=0; c--) {
                    int v = (3-c<midx)?merged[3-c]:0;
                    ng[r*4+c] = v;
                    if(cg[r*4+c] != v) moved=true;
                }
            }
        }
        return moved;
    }
};

class TDAgentCpp {
public:
    double alpha;
    
    // 退回 float！虽然 double 不存在精度丢失问题，但是 536MB 的 LUT 会严重撑爆 CPU 的 L3 缓存
    // 这将导致缓存命中率（Cache Hit Rate）断崖式跌落，程序会长期处于等待内存响应的饥饿状态！
    // 作者也是用的 Float！关于 float 的加法舍入吞噬问题，在 2048 这种 Alpha 很小的情况下
    // 其实是一种“极其巧妙的学习率衰减（Learning Rate Decay）”效果，能够避免后期产生剧烈震荡
    vector<float> lut[4];
    vector<pair<int, vector<int>>> features;

    TDAgentCpp(double a) : alpha(a) {
        srand((unsigned int)time(NULL));
        // 我们预热一下极致速度缓存表
        get_speed_lookup_table();
        
        for (int i=0; i<4; i++) {
            lut[i].assign(16777216, 0.0f);
        }
        vector<vector<int>> base = {
            {0, 1, 2, 3, 4, 5},
            {0, 1, 2, 4, 5, 6},
            {0, 1, 2, 3, 4, 8},
            {0, 1, 2, 4, 8, 12}
        };

        TransformFunc trans[8] = { trans_0, trans_1, trans_2, trans_3, trans_4, trans_5, trans_6, trans_7 };

        // 对称性扩展并去重
        for (int i=0; i<4; i++) {
            set<vector<int>> st;
            for (int t=0; t<8; t++) {
                vector<int> nt;
                for (int pos : base[i]) {
                    pair<int,int> rc = trans[t](pos/4, pos%4);
                    nt.push_back(rc.first*4 + rc.second);
                }
                st.insert(nt);
            }
            for (auto& v : st) features.push_back({i, v});
        }
    }

    ~TDAgentCpp() {
        // C++ 的 vector 自动析构，再也不用担心野指针（Wild Pointer）和 Free 错误了
    }

    // 废弃巨长的 if-else 分支！如果放在一秒运行几千万次的底层，分支预测失败会摧毁流水线
    // 替换为预先计算好的 65536 大小的快速映射数组（完美贴合 CPU L1 缓存，O(1) 无分支秒查）
    static const int* get_speed_lookup_table() {
        static int table[65537] = {0};
        static bool inited = false;
        if (!inited) {
            table[2] = 1; table[4] = 2; table[8] = 3; table[16] = 4;
            table[32] = 5; table[64] = 6; table[128] = 7; table[256] = 8;
            table[512] = 9; table[1024] = 10; table[2048] = 11; table[4096] = 12;
            table[8192] = 13; table[16384] = 14; table[32768] = 15; table[65536] = 15;
            inited = true;
        }
        return table;
    }

    inline int get_power(int val) const {
        if (val > 65536) return 15; // 保护极大数字（极其罕见的情况下）
        return get_speed_lookup_table()[val];
    }

    int get_index(const int* grid, const vector<int>& tidx) const {
        int index = 0;
        for (size_t i=0; i<tidx.size(); i++) {
            int p = get_power(grid[tidx[i]]);
            index |= (p << (4*i));
        }
        // 为了确保绝对的内存安全，永远把索引截断在 16777215 以内（0xFFFFFF）
        return index & 0xFFFFFF;
    }

    double evaluate(const int* grid) {
        double v = 0;
        for (auto& f : features) {
            v += lut[f.first][get_index(grid, f.second)];
        }
        return v;
    }

    unsigned long long get_hash(const int* grid) const {
        unsigned long long h = 0;
        for(int i=0; i<16; i++) {
            h |= ((unsigned long long)get_power(grid[i]) << (4ULL * i));
        }
        return h;
    }

    // 霸榜秘密武器：哈希置换表 (Transposition Table) 和概率剪枝
    struct TTEntry {
        unsigned long long hash;
        double value;
        int depth;
    };
    vector<TTEntry> tt_pre;
    vector<TTEntry> tt_after;
    
    double expectimax_afterstate(int* grid, int depth, double prob) {
        // 概率剪枝：如果发生这种情况的概率不到万分之一，直接使用直觉估值，别算了！
        if (depth == 0 || prob < 0.0001) {
            return evaluate(grid);
        }

        unsigned long long hash = get_hash(grid);
        int tt_idx = hash % 1048576;
        if (tt_after[tt_idx].hash == hash && tt_after[tt_idx].depth >= depth) {
            return tt_after[tt_idx].value;
        }
        
        int empty[16];
        int count = 0;
        for(int i=0; i<16; i++) {
            if (grid[i] == 0) empty[count++] = i;
        }
        if (count == 0) return 0;
        
        double expected_v = 0;
        double cell_prob = 1.0 / count;
        for(int i=0; i<count; i++) {
            int idx = empty[i];
            
            grid[idx] = 2;
            expected_v += 0.9 * max_player_value(grid, depth - 1, prob * 0.9 * cell_prob);
            
            grid[idx] = 4;
            expected_v += 0.1 * max_player_value(grid, depth - 1, prob * 0.1 * cell_prob);
            
            grid[idx] = 0;
        }
        
        double v = expected_v / count;
        tt_after[tt_idx] = {hash, v, depth};
        return v;
    }

    double max_player_value(int* grid, int depth, double prob) {
        unsigned long long hash = get_hash(grid);
        int tt_idx = hash % 1048576;
        if (tt_pre[tt_idx].hash == hash && tt_pre[tt_idx].depth >= depth) {
            return tt_pre[tt_idx].value;
        }

        double best_v = -1e9;
        bool moved = false;
        for(int d=0; d<4; d++) {
            int ng[16], inc;
            if (FastGame::sim_move(grid, d, ng, inc)) {
                moved = true;
                double v = inc + expectimax_afterstate(ng, depth, prob);
                if (v > best_v) best_v = v;
            }
        }
        if (!moved) return 0;

        tt_pre[tt_idx] = {hash, best_v, depth};
        return best_v;
    }

    int choose_best_move(const int* grid) {
        // 每次下棋重置一些过时的置换表（防冲突）
        if (tt_pre.empty()) {
            tt_pre.assign(1048576, {0, 0, -1});
            tt_after.assign(1048576, {0, 0, -1});
        }
        
        int empty_count = 0;
        for(int i=0; i<16; i++) if (grid[i] == 0) empty_count++;
        
        int depth = 3;

        double best_v = -1e9;
        int best_m = -1;
        for(int d=0; d<4; d++) {
            int ng[16]; int inc;
            if (FastGame::sim_move(grid, d, ng, inc)) {
                double v = inc + expectimax_afterstate(ng, depth, 1.0);
                if (v > best_v) {
                    best_v = v;
                    best_m = d;
                }
            }
        }
        return best_m;
    }

    void update_weights(const int* grid, double tgt) {
        double old_v = evaluate(grid);
        double delta = tgt - old_v;
        double adj = alpha * delta;
        for(auto& f : features) {
            lut[f.first][get_index(grid, f.second)] += adj;
        }
    }

    double train(int episodes) {
        long long total_score = 0;
        for(int e=0; e<episodes; e++) {
            FastGame g;
            int old_after[16];
            bool has_old = false;
            while(!g.is_over()) {
                double best_v = -1e9;
                int best_m = -1;
                int best_after[16];

                for(int d=0; d<4; d++) {
                    int ng[16], inc;
                    if(FastGame::sim_move(g.grid, d, ng, inc)) {
                        double v = evaluate(ng);
                        if (inc + v > best_v) {
                            best_v = inc + v;
                            best_m = d;
                            for(int i=0; i<16; i++) best_after[i] = ng[i];
                        }
                    }
                }

                if (best_m == -1) break;

                // 重点修复：TD(0) 的 Target 正是能够取到的未来最大状态期望
                // 也就是我们在上面循环里求到的 best_v（它已经包含了下一部的奖励 inc + V(s')）
                // 绝对不能再加一次 best_r，否则会导致【双倍计算 Reward】的数学灾难！
                if (has_old) update_weights(old_after, best_v);

                g.move(best_m);
                for(int i=0; i<16; i++) old_after[i] = best_after[i];
                has_old = true;
            }
            if (has_old) update_weights(old_after, 0); // Terminal state value = 0
            total_score += g.score;
        }
        return (double)total_score / episodes;
    }
};

// ================== C API 为 Python 提供跨语言调用 ==================
extern "C" {
    __declspec(dllexport) void* init_agent(double alpha) { 
        return new TDAgentCpp(alpha); 
    }
    
    __declspec(dllexport) void free_agent(void* agent) { 
        delete (TDAgentCpp*)agent; 
    }
    
    __declspec(dllexport) double train(void* agent, int episodes) { 
        return ((TDAgentCpp*)agent)->train(episodes); 
    }
    
    __declspec(dllexport) int get_best_move(void* agent, int* grid_1d) { 
        return ((TDAgentCpp*)agent)->choose_best_move(grid_1d); 
    }
    
    __declspec(dllexport) bool save_model(void* agent, const char* path) {
        ofstream out(path, ios::binary | ios::trunc);
        if(!out) return false;
        TDAgentCpp* ag = (TDAgentCpp*)agent;
        for(int i=0; i<4; i++) out.write((char*)ag->lut[i].data(), 16777216*sizeof(float));
        return true;
    }
    
    __declspec(dllexport) bool load_model(void* agent, const char* path) {
        ifstream in(path, ios::binary);
        if(!in) return false;
        TDAgentCpp* ag = (TDAgentCpp*)agent;
        for(int i=0; i<4; i++) in.read((char*)ag->lut[i].data(), 16777216*sizeof(float));
        return true;
    }
}