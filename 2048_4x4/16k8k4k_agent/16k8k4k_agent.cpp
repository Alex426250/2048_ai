#include "../2048_engine_core.h"
using namespace std;

class TDAgentCpp {
public:
    struct Feature {
        uint8_t group;
        uint8_t len;
        array<uint8_t, 6> pos;
        uint32_t mapped_offset;
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

#ifdef _WIN32
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMapping = nullptr;
#else
    int fd = -1;
#endif
    const float* mapped_data = nullptr;
    bool is_mapped = false;
    size_t total_floats = 0;

    TDAgentCpp(double a) : alpha(a) {
        srand((unsigned int)time(NULL));
        const auto& base = get_base_patterns();
        const auto& lut_sizes = get_lut_sizes();
        for (int i=0; i<LUT_GROUP_COUNT; i++) lut[i].assign(lut_sizes[i], 0.0f);
        TransformFunc trans[8] = { trans_0, trans_1, trans_2, trans_3, trans_4, trans_5, trans_6, trans_7 };
        size_t mapped_offset = 0;
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
                f.mapped_offset = (uint32_t)mapped_offset;
                for (size_t j = 0; j < v.size() && j < 6; j++) f.pos[j] = (uint8_t)v[j];
                features.push_back(f);
            }
            mapped_offset += lut_sizes[i];
        }
        init_random_snapshots();
    }

    ~TDAgentCpp() {
        unmap_if_mapped();  
        
    }

    void unmap_if_mapped() {
        if (!is_mapped) return;
#ifdef _WIN32
        if (mapped_data) { UnmapViewOfFile(mapped_data); mapped_data = nullptr; }
        if (hMapping) { CloseHandle(hMapping); hMapping = nullptr; }
        if (hFile != INVALID_HANDLE_VALUE) { CloseHandle(hFile); hFile = INVALID_HANDLE_VALUE; }
#else
        if (mapped_data) { munmap((void*)mapped_data, total_floats * sizeof(float)); mapped_data = nullptr; }
        if (fd >= 0) { close(fd); fd = -1; }
#endif
        is_mapped = false;
        total_floats = 0;
    }

    bool load_model(const string& path) {
        if (is_mapped) unmap_if_mapped();

        const auto& lut_sizes = get_lut_sizes();
        size_t expected_floats = 0;
        for (size_t sz : lut_sizes) {
            expected_floats += sz;
        }
        total_floats = expected_floats;
        size_t expected_bytes = expected_floats * sizeof(float);

#ifdef _WIN32
        HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0) {
            CloseHandle(hFile);
            return false;
        }
        if ((size_t)fileSize.QuadPart != expected_bytes) {
            CloseHandle(hFile);
            return false;
        }

        HANDLE hMapping = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMapping) {
            CloseHandle(hFile);
            return false;
        }
        const float* mapped = (const float*)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
        if (!mapped) {
            CloseHandle(hMapping);
            CloseHandle(hFile);
            return false;
        }

        this->hFile = hFile;
        this->hMapping = hMapping;
        this->mapped_data = mapped;
        this->is_mapped = true;
        return true;
#else
        int fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;

        struct stat st;
        if (fstat(fd, &st) != 0 || st.st_size <= 0) {
            close(fd);
            return false;
        }
        if ((size_t)st.st_size != expected_bytes) {
            close(fd);
            return false;
        }

        void* addr = mmap(nullptr, expected_bytes, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED) {
            close(fd);
            return false;
        }

        this->fd = fd;
        this->mapped_data = (const float*)addr;
        this->is_mapped = true;
        return true;
#endif
    }

    bool save_model(const string& path) const {
        ofstream out(path, ios::binary | ios::trunc);
        if (!out) return false;

        if (is_mapped && mapped_data) {
            out.write((char*)mapped_data, total_floats * sizeof(float));
        } else {
            const auto& lut_sizes = get_lut_sizes();
            for (int i=0; i<LUT_GROUP_COUNT; i++) {
                out.write((char*)lut[i].data(), lut_sizes[i] * sizeof(float));
            }
        }
        return !!out;
    }

    #include "../2048_ai_core.inl"

    int random_small_tile() const {
        int r = rand() % 100;
        if (r < 80) return 0;
        if (r < 90) return 2;
        if (r < 95) return 4;
        if (r < 98) return 8;
        return 16;
    }

    void init_random_snapshots() {
        if (snapshot_pool.empty()) snapshot_pool.reserve(1000);
        while (snapshot_pool.size() < 1000) {
            vector<int> b(16, 0);
            int mode = rand() % 10;
            if (mode < 9) b[0] = 32768, b[1] = 16384, b[2] = 4096;
            else {
                int slots[16];
                for (int i = 0; i < 16; i++) slots[i] = i;
                for (int i = 15; i > 0; i--) {
                    int j = rand() % (i + 1);
                    std::swap(slots[i], slots[j]);
                }
                b[slots[0]] = 32768;
                b[slots[1]] = 16384;
                b[slots[2]] = 4096;
            }
            for (int i = 0; i < 16; i++) if (!b[i]) b[i] = random_small_tile();
            snapshot_pool.push_back(b);
        }
    }

    void train(int episodes, double* avg_score, int* merged_8192_out, int* merged_2048_out) {
        
        if (is_mapped) {
            const auto& lut_sizes = get_lut_sizes();
            size_t offset = 0;
            for (int i=0; i<LUT_GROUP_COUNT; i++) {
                size_t sz = lut_sizes[i];
                if (sz > 0) {
                    if (lut[i].size() != sz) lut[i].assign(sz, 0.0f);
                    memcpy(lut[i].data(), mapped_data + offset, sz * sizeof(float));
                    offset += sz;
                }
            }
            unmap_if_mapped();  
        }

        BitBoardEngine::init_tables();
        long long total_score = 0;
        int merged_8192_count = 0;
        int merged_2048_count = 0;
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
            bool reached_2048 = false;
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
                for (int i=0; i<16; i++) {
                    uint64_t p = (g.board >> (i * 4)) & 0xF;
                    if (p == 13) reached_8192 = true;
                    if (p == 11) reached_2048 = true;
                }
                old_after = best_after;
                has_old = true;
            }
            if (reached_8192) merged_8192_count++;
            if (reached_2048) merged_2048_count++;
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
        if (merged_2048_out) *merged_2048_out = merged_2048_count;
    }
};

extern "C" {
    __declspec(dllexport) void* init_agent(double alpha) {
        return new TDAgentCpp(alpha);
    }
    __declspec(dllexport) void free_agent(void* agent) {
        delete (TDAgentCpp*)agent;
    }
    __declspec(dllexport) void train(void* agent, int episodes, double* avg_s, int* merged_8192_s, int* merged_2048_s) {
        ((TDAgentCpp*)agent)->train(episodes, avg_s, merged_8192_s, merged_2048_s);
    }
    __declspec(dllexport) int get_best_move(void* agent, int* grid_1d) {
        return ((TDAgentCpp*)agent)->choose_best_move(grid_1d);
    }
    __declspec(dllexport) bool save_model(void* agent, const char* path) {
        return ((TDAgentCpp*)agent)->save_model(path);
    }
    __declspec(dllexport) bool load_model(void* agent, const char* path) {
        return ((TDAgentCpp*)agent)->load_model(path);
    }
}