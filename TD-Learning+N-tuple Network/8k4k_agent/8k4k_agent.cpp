#include "../2048_engine_core.h"

class TDAgentCpp {
public:
    struct Feature {
        uint8_t group;
        uint8_t len;
        array<uint8_t, 6> pos;
    };
    static constexpr int LUT_GROUP_COUNT = 7;
    static constexpr int LUT_RADIX = 14;
    static size_t pow_u(size_t b, int e) {
        size_t r = 1;
        for (int i = 0; i < e; i++) r *= b;
        return r;
    }
    static const vector<vector<int>>& get_base_patterns() {
        static const vector<vector<int>> base = {
            {0, 1, 2, 3, 4, 5},
            {4, 5, 6, 7, 8, 9},
            {8, 9, 10, 11, 12, 13},
            {0, 1, 2, 4, 5, 6},
            {4, 5, 6, 8, 9, 10},
            {0, 1, 2, 3, 4, 8},
            {4, 5, 6, 7, 8, 12}
        };
        return base;
    }
    static const vector<size_t>& get_lut_sizes() {
        static const vector<size_t> sizes = []() {
            vector<size_t> s;
            const auto& base = get_base_patterns();
            s.reserve(base.size());
            for (const auto& t : base) s.push_back(pow_u(LUT_RADIX, (int)t.size()));
            return s;
        }();
        return sizes;
    }

    double alpha;
    vector<float> lut[LUT_GROUP_COUNT];
    vector<Feature> features;
    vector<vector<int>> snapshot_pool;

    TDAgentCpp(double a) : alpha(a) {
        srand((unsigned int)time(NULL));
        const auto& base = get_base_patterns();
        const auto& lut_sizes = get_lut_sizes();
        for (int i=0; i<LUT_GROUP_COUNT; i++) lut[i].assign(lut_sizes[i], 0.0f);
        TransformFunc trans[8] = { trans_0, trans_1, trans_2, trans_3, trans_4, trans_5, trans_6, trans_7 };
        for (int i=0; i<(int)base.size(); i++) {
            set<vector<int>> st;
            for (int t=0; t<8; t++) {
                vector<int> nt;
                for (int pos : base[i]) {
                    pair<int,int> rc = trans[t](pos/4, pos%4);
                    nt.push_back(rc.first*4 + rc.second);
                }
                st.insert(nt);
            }
            for (auto& v : st) {
                Feature f{};
                f.group = (uint8_t)i;
                f.len = (uint8_t)v.size();
                for (size_t j = 0; j < v.size(); j++) f.pos[j] = (uint8_t)v[j];
                features.push_back(f);
            }
        }
        init_random_snapshots();
    }

    ~TDAgentCpp() {}

    #include "../2048_ai_core.inl"

    int random_small_tile() const {
        int r = rand() % 100;
        if (r < 80) return 0;
        if (r < 85) return 2;
        if (r < 92) return 4;
        if (r < 96) return 8;
        return 16;
    }

    void init_random_snapshots() {
        if (snapshot_pool.empty()) snapshot_pool.reserve(1000);
        TransformFunc trans[8] = { trans_0, trans_1, trans_2, trans_3, trans_4, trans_5, trans_6, trans_7 };
        while (snapshot_pool.size() < 1000) {
            vector<int> b(16, 0);
            int pos_16384 = rand() % 16;
            int pos_4096 = rand() % 15;
            int mode = rand() % 10;
            if (mode < 9) {
                int t = rand() % 8;
                pair<int, int> p16384 = trans[t](0, 0);
                pair<int, int> p4096= trans[t](0, 1);
                pos_16384 = p16384.first * 4 + p16384.second;
                pos_4096 = p4096.first * 4 + p4096.second;
            }      
            else if (pos_4096 >= pos_16384) pos_4096++;
            b[pos_16384] = 16384;
            b[pos_4096] = 4096;
            for (int i = 0; i < 16; i++) {
                if (b[i]) continue;
                b[i] = random_small_tile();
            }
            snapshot_pool.push_back(b);
        }
    }

    void train(int episodes, double* avg_score, int* merged_8192_out) {
        BitBoardEngine::init_tables();
        long long total_score = 0;
        int merged_8192_count = 0;
        for (int ep = 0; ep < episodes; ep++) {
            if (snapshot_pool.size() < 1000) init_random_snapshots();
            FastGame g;
            int r = rand() % snapshot_pool.size();
            board_t initial_bb = 0;
            for (int i = 0; i < 16; i++) {
                int val = snapshot_pool[r][i];
                int power = (val == 0) ? 0 : __builtin_ctz(val);
                initial_bb |= ((board_t)power << (4 * i));
            }
            g.board = initial_bb;
            g.score = 0;
            board_t old_after = 0;
            bool has_old = false;
            bool reached_8192 = false;
            while(!g.is_over()) {
                double best_v = -1e15;
                int best_m = -1;
                board_t best_after = 0;
                for(int d=0; d<4; d++) {
                    int inc;
                    board_t ng = BitBoardEngine::execute_move(g.board, d, inc);
                    if(ng != g.board) {
                        board_t patched_ng = ng;
                        for (int i=0; i<16; i++) if (((patched_ng >> (i * 4)) & 0xF) == 13) patched_ng = (patched_ng & ~(0xFULL << (4 * i))) | (12ULL << (4 * i));
                        double v = evaluate(patched_ng);
                        if (inc + v > best_v) {
                            best_v = inc + v;
                            best_m = d;
                            best_after = patched_ng;
                        }
                    }
                }
                if (best_m == -1) break;
                if (reached_8192) break;
                if (has_old) update_weights(old_after, best_v);
                g.move(best_m);
                for (int i=0; i<16; i++) if (((g.board >> (i * 4)) & 0xF) == 13) reached_8192 = true;
                old_after = best_after;
                has_old = true;
            }
            if (reached_8192) merged_8192_count += 1;
            if (has_old && !reached_8192) update_weights(old_after, 0);
            total_score += g.score;
            if ((ep + 1) % 10000 == 0 && snapshot_pool.size() >= 2) {
                for (int k = 0; k < 2; k++) {
                    int r_idx = rand() % snapshot_pool.size();
                    snapshot_pool[r_idx] = snapshot_pool.back();
                    snapshot_pool.pop_back();
                }
            }
        }
        *avg_score = (double)total_score / episodes; 
        if (merged_8192_out) *merged_8192_out = merged_8192_count;
    }
};

extern "C" {
    __declspec(dllexport) void* init_agent(double alpha) { 
        return new TDAgentCpp(alpha); 
    }
    __declspec(dllexport) void free_agent(void* agent) { 
        delete (TDAgentCpp*)agent; 
    }
    __declspec(dllexport) void train(void* agent, int episodes, double* avg_s, int* merged_8192_s) { 
        ((TDAgentCpp*)agent)->train(episodes, avg_s, merged_8192_s); 
    }
    __declspec(dllexport) int get_best_move(void* agent, int* grid_1d) { 
        return ((TDAgentCpp*)agent)->choose_best_move(grid_1d); 
    }
    __declspec(dllexport) bool save_model(void* agent, const char* path) {
        ofstream out(path, ios::binary | ios::trunc);
        if(!out) return false;
        TDAgentCpp* ag = (TDAgentCpp*)agent;
        const auto& lut_sizes = TDAgentCpp::get_lut_sizes();
        for(int i=0; i<TDAgentCpp::LUT_GROUP_COUNT; i++) out.write((char*)ag->lut[i].data(), lut_sizes[i] * sizeof(float));
        return true;
    }
    __declspec(dllexport) bool load_model(void* agent, const char* path) {
        ifstream in(path, ios::binary);
        if(!in) return false;
        TDAgentCpp* ag = (TDAgentCpp*)agent;
        const auto& lut_sizes = TDAgentCpp::get_lut_sizes();
        for(int i=0; i<TDAgentCpp::LUT_GROUP_COUNT; i++) in.read((char*)ag->lut[i].data(), lut_sizes[i] * sizeof(float));
        return true;
    }
}


