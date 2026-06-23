    inline int get_index(const int* p, const Feature& f) const {
        int index = 0;
        for (int i = 0; i < f.len; i++) index = index * LUT_RADIX + p[f.pos[i]];
        return index;
    }
    unsigned long long get_hash(const int* grid) const {
        unsigned long long h = 0;
        for(int i=0; i<16; i++) {
            unsigned long long p = grid[i] > 0 ? __builtin_ctz(grid[i]) : 0;
            h |= (p << (4ULL * i));
        }
        return h;
    }
    inline double evaluate(board_t bb) {
        int p[16];
        for(int i=0; i<16; i++) p[i] = std::min<int>((bb >> (i * 4)) & 0xF, LUT_RADIX - 1);
        double v = 0;
        for (const auto& f : features) v += lut[f.group][get_index(p, f)];
        return v;
    }

    void update_weights(board_t bb, double tgt) {
        int p[16];
        for(int i=0; i<16; i++) p[i] = std::min<int>((bb >> (i * 4)) & 0xF, LUT_RADIX - 1);
        double old_v = evaluate(bb);
        double delta = tgt - old_v;
        double adj = alpha * delta;
        for(const auto& f : features) lut[f.group][get_index(p, f)] += adj;
    }

    struct HashEntry {
        std::atomic<board_t> key;
        std::atomic<int> depth;
        std::atomic<double> value;

        HashEntry() : key(0), depth(0), value(0.0) {}
        HashEntry(const HashEntry& o) {
            key.store(o.key.load(std::memory_order_relaxed), std::memory_order_relaxed);
            depth.store(o.depth.load(std::memory_order_relaxed), std::memory_order_relaxed);
            value.store(o.value.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
    };
    vector<HashEntry> tt_pre;
    vector<HashEntry> tt_after;

    inline unsigned long long murmurhash3(unsigned long long k) {
        k ^= k >> 33;
        k *= 0xff51afd7ed558ccdULL;
        k ^= k >> 33;
        k *= 0xc4ceb9fe1a85ec53ULL;
        k ^= k >> 33;
        return k;
    }

    double expectimax_afterstate(board_t bb, int depth) {
        if (depth == 0) return evaluate(bb);
        int tt_idx = murmurhash3(bb) & 0xFFFFF;
        if (tt_after[tt_idx].key.load(std::memory_order_relaxed) == bb && 
            tt_after[tt_idx].depth.load(std::memory_order_relaxed) == depth) {
            return tt_after[tt_idx].value.load(std::memory_order_relaxed);
        }
        int empty[16];
        int count = 0;
        for(int i=0; i<16; i++) if (((bb >> (i * 4)) & 0xF) == 0) empty[count++] = i;
        if (count == 0) return 0;
        double expected_v = 0;
        for(int i=0; i<count; i++) {
            int idx = empty[i];
            int shift = idx * 4;
            board_t child_2 = bb | (1ULL << shift);
            expected_v += 0.9 * max_player_value(child_2, depth - 1);
            board_t child_4 = bb | (2ULL << shift);
            expected_v += 0.1 * max_player_value(child_4, depth - 1);
        }
        double v = expected_v / count;
        tt_after[tt_idx].key.store(bb, std::memory_order_relaxed);
        tt_after[tt_idx].depth.store(depth, std::memory_order_relaxed);
        tt_after[tt_idx].value.store(v, std::memory_order_relaxed);
        return v;
    }

    double max_player_value(board_t bb, int depth) {
        int tt_idx = murmurhash3(bb) & 0xFFFFF;
        if (tt_pre[tt_idx].key.load(std::memory_order_relaxed) == bb && 
            tt_pre[tt_idx].depth.load(std::memory_order_relaxed) == depth) {
            return tt_pre[tt_idx].value.load(std::memory_order_relaxed);
        }
        double best_v = -1e15;
        bool moved = false;
        for(int d=0; d<4; d++) {
            int inc;
            board_t ng = BitBoardEngine::execute_move(bb, d, inc);
            if (ng != bb) {
                moved = true;
                double v = inc + expectimax_afterstate(ng, depth);
                if (v > best_v) best_v = v;
            }
        }
        if (!moved) return 0;
        tt_pre[tt_idx].key.store(bb, std::memory_order_relaxed);
        tt_pre[tt_idx].depth.store(depth, std::memory_order_relaxed);
        tt_pre[tt_idx].value.store(best_v, std::memory_order_relaxed);
        return best_v;
    }

    int choose_best_move(int* grid) {
        BitBoardEngine::init_tables();
        int best_greedy_dir = -1;
        bool has_same_corner = false;
        int old_max_val = 0;
        for (int i = 0; i < 16; i++) if (grid[i] > old_max_val) old_max_val = grid[i];
        board_t root_bb = get_hash(grid);
        for (int d = 0; d < 4; d++) {
            int inc;
            board_t ng = BitBoardEngine::execute_move(root_bb, d, inc);
            if (ng != root_bb) {
                if (inc >= 4000) {
                    int max_val = 0;
                    for (int i = 0; i < 16; i++) {
                        int power = (ng >> (i * 4)) & 0xF;
                        int val = power ? (1 << power) : 0;
                        if (val > max_val) max_val = val;
                    }
                    bool corner_valid = false;
                    bool same_corner = false;
                    int corners[4] = {0, 3, 12, 15};
                    for (int c : corners) {
                        int power = (ng >> (c * 4)) & 0xF;
                        int val = power ? (1 << power) : 0;
                        if (val == max_val) {
                            corner_valid = true;
                            if (grid[c] == old_max_val) same_corner = true;
                        }
                    }
                    if (corner_valid) {
                        if (best_greedy_dir == -1 || (!has_same_corner && same_corner)) {
                            best_greedy_dir = d;
                            has_same_corner = same_corner;
                        }
                    }
                }
            }
        }
        if (best_greedy_dir != -1) return best_greedy_dir;
        int depth = 3;
        unsigned int tile_mask = 0;
        for (int i = 0; i < 16; i++) if (grid[i] >= 256) tile_mask |= grid[i];
        int count_distinct = __builtin_popcount(tile_mask);
        //if (count_distinct >= 5) depth = 4;

        if (tt_pre.empty()) {
            tt_pre.resize(1048576);
            tt_after.resize(1048576);
        }

#ifdef _OPENMP
        if (omp_get_max_threads() > 1) {
            struct RootResult {
                double v;
                int dir;
                bool valid;
            };
            array<RootResult, 4> results{};

#pragma omp parallel for schedule(static)
            for (int d = 0; d < 4; d++) {
                int inc;
                board_t ng = BitBoardEngine::execute_move(root_bb, d, inc);
                if (ng != root_bb) {
                    results[d] = {inc + expectimax_afterstate(ng, depth), d, true};
                } else {
                    results[d] = {-1e15, d, false};
                }
            }

            double best_v_parallel = -1e15;
            int best_m_parallel = -1;
            for (const auto& r : results) {
                if (r.valid && r.v > best_v_parallel) {
                    best_v_parallel = r.v;
                    best_m_parallel = r.dir;
                }
            }
            if (best_m_parallel != -1) return best_m_parallel;
        }
#endif
        double best_v = -1e15;
        int best_m = -1;
        for(int d=0; d<4; d++) {
            int inc;
            board_t ng = BitBoardEngine::execute_move(root_bb, d, inc);
            if (ng != root_bb) {
                double v = inc + expectimax_afterstate(ng, depth);
                if (v > best_v) {
                    best_v = v;
                    best_m = d;
                }
            }
        }
        return best_m;
    }

