    inline int get_index(const int* p, const Feature& f) const {
        const uint8_t* pos = f.pos.data();
        int index = p[pos[0]];
        index = index * LUT_RADIX + p[pos[1]];
        index = index * LUT_RADIX + p[pos[2]];
        index = index * LUT_RADIX + p[pos[3]];
        index = index * LUT_RADIX + p[pos[4]];
        return index * LUT_RADIX + p[pos[5]];
    }
    unsigned long long get_hash(const int* grid) const {
        unsigned long long h = 0;
        for(int i=0; i<12; i++) {
            unsigned long long p = grid[i] > 0 ? __builtin_ctz(grid[i]) : 0;
            h |= (p << (4ULL * i));
        }
        return h;
    }
    inline double evaluate_mapped(board_t bb) const {
        int p[12];
        for(int i=0; i<12; i++) p[i] = std::min<int>((bb >> (i * 4)) & 0xF, LUT_RADIX - 1);
        double v = 0;
        for (const auto& f : features) v += mapped_data[f.mapped_offset + get_index(p, f)];
        return v;
    }

    inline double evaluate_trained(board_t bb) const {
        int p[12];
        for(int i=0; i<12; i++) p[i] = std::min<int>((bb >> (i * 4)) & 0xF, LUT_RADIX - 1);
        double v = 0;
        for (const auto& f : features) v += lut[f.group][get_index(p, f)];
        return v;
    }

    inline double evaluate(board_t bb) const {
        if (is_mapped) return evaluate_mapped(bb);
        return evaluate_trained(bb);
    }

    void update_weights(board_t bb, double tgt) {
        
        if (is_mapped) return;
        int p[12];
        for(int i=0; i<12; i++) p[i] = std::min<int>((bb >> (i * 4)) & 0xF, LUT_RADIX - 1);
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
        board_t occupied = bb | (bb >> 1) | (bb >> 2) | (bb >> 3);
        board_t empty_mask = ~occupied & 0x0000111111111111ULL;
        int count = __builtin_popcountll(empty_mask);
        if (count == 0) return 0;
        double expected_v = 0;
        while (empty_mask) {
            int idx = __builtin_ctzll(empty_mask) >> 2;
            empty_mask &= empty_mask - 1;
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
        board_t root_bb = get_hash(grid);
        for (int d = 0; d < 4; ++d) {
            int inc;
            board_t ng = BitBoardEngine::execute_move(root_bb, d, inc);
            if (ng == root_bb || inc < 1000) continue;
            const int corners[4] = {0, 3, 8, 11};
            int chosen_corner = -1;
            for (int i = 0; i < 4; ++i) {
                int val = (root_bb >> (corners[i] * 4)) & 0xF;
                if (val >= 11) chosen_corner = corners[i];
            }

            bool allow = false;
            if (chosen_corner == 0) allow = (d == 0 || d == 2);
            else if (chosen_corner == 3) allow = (d == 0 || d == 3);
            else if (chosen_corner == 8) allow = (d == 1 || d == 2);
            else if (chosen_corner == 11) allow = (d == 1 || d == 3);
            if (allow) return d;
        }

        int depth = 3;

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

#pragma omp parallel for schedule(static) num_threads(4)
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