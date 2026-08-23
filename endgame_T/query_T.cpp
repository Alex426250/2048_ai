#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#include "build_config.h"

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

#if defined(__GNUC__) || defined(__clang__)
#define T_PREFETCH(p) __builtin_prefetch((const void *)(p))
#elif defined(_MSC_VER)
#include <intrin.h>
#define T_PREFETCH(p) _mm_prefetch((const char *)(p), _MM_HINT_T0)
#else
#define T_PREFETCH(p) ((void)0)
#endif

using namespace std;

namespace {

constexpr int ROWS = 4, COLS = 4, CELLS = 16, FREE_N = 10, NCFG = ENDGAME_NCFG;
constexpr int WALLSYM = 200;

constexpr int CFG_CELLS[NCFG][6] = ENDGAME_CFG_CELLS;
constexpr const char *TAG = ENDGAME_TAG;

struct Geometry {
    uint16_t mask[NCFG];
    int free_cell[NCFG][FREE_N], path[8][4];
    int8_t cfg_of[1 << CELLS];
    Geometry() {
        for (size_t i = 0; i < sizeof(cfg_of) / sizeof(cfg_of[0]); ++i) cfg_of[i] = -1;
        for (int g = 0; g < NCFG; ++g) {
            uint16_t m = 0;
            for (int j = 0; j < 6; ++j) m = (uint16_t)(m | (1u << CFG_CELLS[g][j]));
            mask[g] = m;
            cfg_of[m] = (int8_t)g;
            int n = 0;
            for (int c = 0; c < CELLS; ++c) if (!((m >> c) & 1u)) free_cell[g][n++] = c;
        }
        for (int r = 0; r < ROWS; ++r) for (int c = 0; c < COLS; ++c) path[r][c] = r * COLS + c;
        for (int c = 0; c < COLS; ++c) for (int r = 0; r < ROWS; ++r) path[4 + c][r] = r * COLS + c;
    }
};
const Geometry GEO;

#pragma pack(push, 1)
struct THeader {
    char magic[8];
    uint32_t version, rows, cols, wall_mask, free_count, key_bytes;
    uint32_t target, target_exp, base;
    uint64_t key_space;
    uint32_t layer_count, block_size, prob_scale, value_bytes, seed_tiles;
    uint64_t state_count, layer_dir_off, file_size;
    uint32_t cfg_count, cfg_mask[16];
    uint64_t reserved[2];
};

struct TLayerDir {
    uint64_t block_first_off, block_off_off, key_data_off, values_off;
    uint32_t n_states, n_blocks;
};
#pragma pack(pop)

struct MappedFile {
    const uint8_t *data = nullptr;
    uint64_t size = 0;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;

    ~MappedFile() {
        if (data) UnmapViewOfFile(data);
        if (mapping) CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }

    void open_file(const string &path) {
        file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw runtime_error("cannot open model: " + path);
        LARGE_INTEGER s;
        if (!GetFileSizeEx(file, &s) || s.QuadPart <= 0) throw runtime_error("cannot get model size: " + path);
        size = (uint64_t)s.QuadPart;
        mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping) throw runtime_error("cannot map model: " + path);
        data = (const uint8_t *)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        if (!data) throw runtime_error("cannot view model: " + path);
    }
#else
    int fd = -1;

    ~MappedFile() {
        if (data) munmap(const_cast<uint8_t *>(data), size);
        if (fd >= 0) close(fd);
    }

    void open_file(const string &path) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) throw runtime_error("cannot open model: " + path);
        struct stat st {};
        if (fstat(fd, &st) != 0 || st.st_size <= 0) throw runtime_error("cannot stat model: " + path);
        size = (uint64_t)st.st_size;
        data = (const uint8_t *)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) { data = nullptr; throw runtime_error("cannot map model: " + path); }
    }
#endif
};

void compress4(const int *line, bool toward_end, int *out, int target_exp, bool &won) {
    int seq[4];
    for (int k = 0; k < 4; ++k) seq[k] = toward_end ? line[3 - k] : line[k];
    int xs[4], n = 0;
    for (int k = 0; k < 4; ++k) if (seq[k] != 0) xs[n++] = seq[k];
    int o[4] = {0, 0, 0, 0}, m = 0;
    won = false;
    for (int i = 0; i < n;) {
        if (i + 1 < n && xs[i] == xs[i + 1] && xs[i] != WALLSYM) {
           int mg = xs[i] + 1;
           if (mg >= target_exp) won = true;
           o[m++] = mg;
           i += 2;
        } else {
            o[m++] = xs[i++];
        }
    }
    for (int k = 0; k < 4; ++k) out[k] = toward_end ? o[3 - k] : o[k];
}

int move_board(const int *b, char dir, int target_exp, 
                int *out, bool &won, bool &changed) {
    const int p0 = (dir == 'U' || dir == 'D') ? 4 : 0;
    const bool toward_end = (dir == 'D' || dir == 'R');
    won = changed = false;
    uint16_t wm = 0;
    for (int L = 0; L < 4; ++L) {
        const int *pp = GEO.path[p0 + L];
        int line[4], res[4];
        for (int k = 0; k < 4; ++k) line[k] = b[pp[k]];
        bool w = false;
        compress4(line, toward_end, res, target_exp, w);
        won = won || w;
        for (int k = 0; k < 4; ++k) {
            if (line[k] != res[k]) changed = true;
            if (res[k] == WALLSYM) wm = (uint16_t)(wm | (1u << pp[k]));
            out[pp[k]] = res[k];
        }
    }
    return GEO.cfg_of[wm];
}

struct Table {
    MappedFile mf;
    THeader h{};
    const TLayerDir *dir = nullptr;
    uint64_t pw[FREE_N + 1] = {0};

    uint64_t rd(uint64_t p, int bytes) const {
        uint64_t x = 0;
        memcpy(&x, mf.data + p, (size_t)bytes);
        return x;
    }

    static inline uint64_t varint(const uint8_t *&p) {
        uint64_t b0 = *p++;
        if (b0 < 128) return b0;
        uint64_t b1 = *p++;
        if (b1 < 128) return (b0 & 127u) | (b1 << 7);
        uint64_t x = (b0 & 127u) | ((b1 & 127u) << 7);
        for (int sh = 14;; sh += 7) {
            uint64_t b = *p++;
            x |= (b & 127u) << sh;
            if (b < 128) return x;
        }
    }

    void load(const string &file, uint32_t target) {
        mf.open_file(file);
        if (mf.size < sizeof(THeader)) throw runtime_error("model too small: " + file);
        memcpy(&h, mf.data, sizeof(h));
        if (memcmp(h.magic, "TWIN", 4) != 0 || h.version != 1 || h.rows != 4 || h.cols != 4 ||
            h.free_count != FREE_N || h.cfg_count != NCFG || h.value_bytes != 3 || 
            (h.key_bytes != 4 && h.key_bytes != 8) ||
            h.prob_scale == 0 || h.file_size != mf.size)
            throw runtime_error("bad T model: " + file);
        for (int g = 0; g < NCFG && g < 16; ++g)
            if (h.cfg_mask[g] != (uint32_t)GEO.mask[g])
                throw runtime_error("model was built for a different formation: " + file);
        if (target && h.target != target)
            throw runtime_error("model target mismatch: file has " + to_string(h.target) + 
                                 ", requested " + to_string(target));
        if (h.layer_dir_off + (uint64_t)h.layer_count * sizeof(TLayerDir) > mf.size) throw runtime_error("truncated layer directory: " + file);
        dir = (const TLayerDir *)(mf.data + h.layer_dir_off);
        pw[0] = 1;
        for (int i = 1; i <= FREE_N; ++i) pw[i] = pw[i - 1] * h.base;
    }

    double dequant(uint32_t q) const { return (double)q / (double)h.prob_scale; }

    double lookup(uint64_t key, int layer) const {
        if (layer < 0 || (uint32_t)layer >= h.layer_count) return 0.0;
        const TLayerDir &ld = dir[layer];
        if (!ld.n_states || !ld.n_blocks) return 0.0;

        const uint8_t *bf = mf.data + ld.block_first_off;
        uint32_t lo = 0, hi = ld.n_blocks;
        if (h.key_bytes == 4) {
            const uint32_t k32 = (uint32_t)key;
            if (key > UINT32_MAX) return 0.0;
            const uint32_t *a = (const uint32_t *)bf;
            if (k32 < a[0]) return 0.0;
            while (lo < hi) { uint32_t m = lo + ((hi - lo) >> 1); if (a[m] <= k32) lo = m + 1; else hi = m; }
        } else {
            const uint64_t *a = (const uint64_t *)bf;
            if (key < a[0]) return 0.0;
            while (lo < hi) { uint32_t m = lo + ((hi - lo) >> 1); if (a[m] <= key) lo = m + 1; else hi = m; }
        }
        const uint32_t b = lo - 1;
        uint64_t cur = rd(ld.block_first_off + (uint64_t)b * h.key_bytes, (int)h.key_bytes);

        const uint32_t *bo = (const uint32_t *)(mf.data + ld.block_off_off);
        const uint8_t *p = mf.data + ld.key_data_off + bo[b];
        const uint64_t idx0 = (uint64_t)b * h.block_size;
        const uint64_t cnt = min<uint64_t>(h.block_size, ld.n_states - idx0);
        const uint8_t *val = mf.data + ld.values_off + idx0 * 3;
        T_PREFETCH(val);
        for (uint64_t i = 0; i < cnt; ++i) {
            cur += varint(p);
            if (cur >= key) {
                if (cur != key) return 0.0;
                const uint8_t *v = val + i * 3;
                return dequant((uint32_t)v[0] | ((uint32_t)v[1] << 8) | ((uint32_t)v[2] << 16));
            }
        }
        return 0.0;
    }

    double prob_after(const int *after, int cfg) const {
        uint64_t key = (uint64_t)cfg * pw[FREE_N];
        int sum = 0;
        for (int i = 0; i < FREE_N; ++i) {
            const int e = after[GEO.free_cell[cfg][i]];
            if (e >= (int)h.target_exp) return 1.0;
            if (e > 0) { key += (uint64_t)e * pw[i]; sum += (1 << e); }
        }
        return lookup(key, sum / 2);
    }
};

int hex_exp(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    throw runtime_error("board must be 16 hex digits");
}

bool already_won(const string &hex_in, const Table &tab) {
    string hex = hex_in;
    if (hex.size() == 18 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex = hex.substr(2);
    if (hex.size() != CELLS) throw runtime_error("board must be exactly 16 hex digits (row-major)");
    int big_count = 0;
    for (char c : hex) if (hex_exp(c) >= (int)tab.h.target_exp) ++big_count;
    return big_count > 6;
}

int parse_board(const string &hex_in, const Table &tab, int *board) {
    string hex = hex_in;
    if (hex.size() == 18 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex = hex.substr(2);
    if (hex.size() != CELLS) throw runtime_error("board must be exactly 16 hex digits (row-major)");
    uint16_t wm = 0;
    for (int c = 0; c < CELLS; ++c) {
        const int e = hex_exp(hex[(size_t)c]);
        board[c] = e;
        if (e >= (int)tab.h.target_exp) { board[c] = WALLSYM; wm = (uint16_t)(wm | (1u << c)); }
    }
    const int g = GEO.cfg_of[wm];
    if (g < 0) throw runtime_error("the 6 big numbers (hex digit >= target_exp) must form one of the "
                                    "allowed configurations");
    return g;
}

struct DirResult { bool is_none; double prob; };

DirResult eval_dir(const Table &tab, const int *board, char dir) {
    int after[CELLS];
    bool won, changed;
    const int ng = move_board(board, dir, (int)tab.h.target_exp, after, won, changed);
    if (!changed) return {true, -1.0};
    if (ng < 0) return {true, -1.0};
    if (won) return {false, 1.0};
    return {false, tab.prob_after(after, ng)};
}

const char *DIR_NAMES = "UDLR";

}

#ifdef _WIN32
#define T_EXPORT __declspec(dllexport)
#else
#define T_EXPORT __attribute__((visibility("default")))
#endif

static map<int, Table *> g_tables;

static const Table *pick_table(int target) {
    auto it = g_tables.find(target);
    if (it != g_tables.end()) return it->second;
    if (target == 0 && g_tables.size() == 1) return g_tables.begin()->second;
    return nullptr;
}
 
extern "C" T_EXPORT int init_model(const char *model_path, int target) {
    try {
        Table *tab = new Table();
        tab->load(string(model_path), (uint32_t)(target > 0 ? target : 0));
        int key = (int)tab->h.target;
        auto it = g_tables.find(key);
        if (it != g_tables.end()) { delete it->second; g_tables.erase(it); }
        g_tables[key] = tab;
        return 0;
    } catch (const exception &e) {
        fprintf(stderr, "init_model error: %s\n", e.what());
        return -1;
    }
}

extern "C" T_EXPORT int query_probs(const char *board_hex, int target, double out_probs[4]) {
    const Table *t = pick_table(target);
    if (!t) return -2;
    const Table &tab = *t;
    try {
        if (already_won(string(board_hex), tab)) {
            for (int i = 0; i < 4; ++i) out_probs[i] = 1.0;
            return 0;
        }
        int board[CELLS];
        parse_board(string(board_hex), tab, board);
 
        for (int i = 0; i < 4; ++i) {
            DirResult r = eval_dir(tab, board, DIR_NAMES[i]);
            out_probs[i] = r.is_none ? -1.0 : r.prob;
        }
        return 0;
    } catch (const exception &e) {
        fprintf(stderr, "query_probs error: %s\n", e.what());
        return -5;
    }
}

extern "C" T_EXPORT int query_state(const char *board_hex, int target, double *out) {
    const Table *t = pick_table(target);
    if (!t) return -2;
    const Table &tab = *t;
    try {
        if (already_won(string(board_hex), tab)) {
            *out = 1.0;
            return 0;
        }
        int board[CELLS];
        const int g = parse_board(string(board_hex), tab, board);
        *out = tab.prob_after(board, g);
        return 0;
    } catch (const exception &e) {
        fprintf(stderr, "query_state error: %s\n", e.what());
        return -5;
    }
}

extern "C" T_EXPORT void free_models() {
    for (auto &kv : g_tables) delete kv.second;
    g_tables.clear();
}

namespace {

void usage(const char *a0) {
    fprintf(stderr,
        "用法:\n"
        "  %s <target> <16位十六进制盘面>\n"
        "  %s --model FILE <target> <16位十六进制盘面>\n"
        "  %s --info [--model FILE] <target>\n"
        "\n"
        "盘面：16 位十六进制**指数**，行主序。0=空 1=2 2=4 ... 9=512 A=1024 B=2048\n"
        "      恰好 6 个数码必须 >= 目标指数，并组成允许的 T 形大数配置。\n"
        "例：  %s 512 00000000AA0AAA0A\n", a0, a0, a0, a0);
}

}

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    try {
        string model;
        bool info = false;
        int arg = 1;
        while (arg < argc) {
            string a = argv[arg];
            if (a == "--model") {
                if (arg + 1 >= argc) throw runtime_error("missing value for --model");
                model = argv[arg + 1];
                arg += 2;
            } else if (a == "--info") {
                info = true;
                ++arg;
            } else if (a == "-h" || a == "--help") {
                usage(argv[0]);
                return 0;
            } else break;
        }

        if (argc - arg < 1) { usage(argv[0]); return 1; }
        int target = stoi(argv[arg++]);
        if (model.empty()) model = string("table_") + TAG + "_" + to_string(target) + ".bin";

        Table tab;
        tab.load(model, (uint32_t)target);

        if (info) {
            printf("model        %s\n", model.c_str());
            printf("target       %u (target_exp=%u, base=%u)\n", tab.h.target, tab.h.target_exp, tab.h.base);
            printf("key_space    %llu  key_bytes=%u\n", (unsigned long long)tab.h.key_space, tab.h.key_bytes);
            printf("layers       %u   block_size=%u\n", tab.h.layer_count, tab.h.block_size);
            printf("states       %llu\n", (unsigned long long)tab.h.state_count);
            printf("value enc    u%u fixed-point, scale=%u (7 位小数)\n", tab.h.value_bytes * 8, tab.h.prob_scale);
            printf("seed_tiles   %u\n", tab.h.seed_tiles);
            printf("file_size    %llu\n", (unsigned long long)tab.h.file_size);
            return 0;
        }

        if (argc - arg != 1) { usage(argv[0]); return 1; }
        string hex = argv[arg++];

        if (already_won(hex, tab)) {
            cout << fixed << setprecision(7);
            for (char dir : string(DIR_NAMES)) cout << dir << " 1.0000000\n";
            return 0;
        }
        int board[CELLS];
        parse_board(hex, tab, board);

        pair<DirResult, char> res[4];
        for (int i = 0; i < 4; ++i) res[i] = {eval_dir(tab, board, DIR_NAMES[i]), DIR_NAMES[i]};
        
        cout << fixed << setprecision(7);
        for (int i = 0; i < 4; ++i) {
            cout << res[i].second << ' ';
            if (res[i].first.is_none) cout << "None\n";
            else cout << res[i].first.prob << '\n';
        }
        return 0;
    } catch (const exception &e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
