#define _FILE_OFFSET_BITS 64

// Shared endgame table builder. Other formations define ENDGAME_* settings and include this file.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <direct.h>
#include <windows.h>
#define T_MKDIR(p) _mkdir(p)
#define T_RMDIR(p) _rmdir(p)
#define T_FSEEK _fseeki64
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define T_MKDIR(p) mkdir(p, 0755)
#define T_RMDIR(p) rmdir(p)
#define T_FSEEK fseeko
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

#if !defined(ENDGAME_NCFG) || !defined(ENDGAME_TAG) || !defined(ENDGAME_MAGIC) || \
    !defined(ENDGAME_TMPDIR) || !defined(ENDGAME_OPENING) || !defined(ENDGAME_CFG_CELLS)
#error "Include the formation-specific build_config.h before endgame_table_core.inl"
#endif

constexpr int ROWS = 4, COLS = 4, CELLS = 16, FREE_N = 10, NCFG = ENDGAME_NCFG;
constexpr int WALLSYM = 200;

constexpr int CFG_CELLS[NCFG][6] = ENDGAME_CFG_CELLS;
constexpr const char *TAG = ENDGAME_TAG;

struct Geometry {
    uint16_t mask[NCFG];
    int free_cell[NCFG][FREE_N], free_slot[NCFG][CELLS], path[8][4];
    int8_t cfg_of[1 << CELLS];

    Geometry() {
        for (size_t i = 0; i < sizeof(cfg_of) / sizeof(cfg_of[0]); ++i) cfg_of[i] = -1;
        for (int g = 0; g < NCFG; ++g) {
            uint16_t m = 0;
            for (int j = 0; j < 6; ++j) m = (uint16_t)(m | (1u << CFG_CELLS[g][j]));
            mask[g] = m;
            if (cfg_of[m] >= 0) throw runtime_error("duplicate wall configuration");
            cfg_of[m] = (int8_t)g;
            int n = 0;
            for (int c = 0; c < CELLS; ++c) free_slot[g][c] = -1;
            for (int c = 0; c < CELLS; ++c)
                if (!((m >> c) & 1u)) { free_slot[g][c] = n; free_cell[g][n] = c; ++n; }
            if (n != FREE_N) throw runtime_error("each configuration must leave exactly 10 free cells");
        }
        for (int r = 0; r < ROWS; ++r) for (int c = 0; c < COLS; ++c) path[r][c] = r * COLS + c;
        for (int c = 0; c < COLS; ++c) for (int r = 0; r < ROWS; ++r) path[4 + c][r] = r * COLS + c;
    }
};
const Geometry GEO;
static_assert(NCFG >= 1 && NCFG <= 127, "NCFG 受 cfg_of 的 int8_t 限制");

constexpr uint64_t ipow_u64(uint64_t b, int e) {
    uint64_t r = 1;
    while (e-- > 0) r *= b;
    return r;
}

inline int popcount16(uint16_t x) {
    int n = 0;
    while (x) { x &= (uint16_t)(x - 1); ++n; }
    return n;
}

struct Timer {
    chrono::steady_clock::time_point t0 = chrono::steady_clock::now();
    double seconds() const { return chrono::duration<double>(chrono::steady_clock::now() - t0).count(); }
    double lap() { double s = seconds(); t0 = chrono::steady_clock::now(); return s; }
};

struct Prof {
    vector<pair<string, double>> slots;
    void add(const string &k, double s) {
        for (auto &p : slots) if (p.first == k) { p.second += s; return; }
        slots.emplace_back(k, s);
    }
    void dump(const char *title) {
        double tot = 0;
        for (auto &p : slots) tot += p.second;
        fprintf(stderr, "  --- %s 分段计时 (合计 %.1fs) ---\n", title, tot);
        for (auto &p : slots)
            fprintf(stderr, "      %-22s %7.2fs  %5.1f%%\n", p.first.c_str(), p.second,
                    tot > 0 ? 100.0 * p.second / tot : 0.0);
        slots.clear();
    }
};

string fmt_secs(double s) {
    char b[64];
    if (s < 90.0) snprintf(b, sizeof(b), "%.1fs", s);
    else if (s < 5400.0) snprintf(b, sizeof(b), "%.1fm", s / 60.0);
    else snprintf(b, sizeof(b), "%.2fh", s / 3600.0);
    return b;
}

string fmt_bytes(uint64_t b) {
    char s[64];
    double v = (double)b;
    const char *u = "B";
    if (v >= 1024.0) { v /= 1024.0; u = "KiB"; }
    if (v >= 1024.0) { v /= 1024.0; u = "MiB"; }
    if (v >= 1024.0) { v /= 1024.0; u = "GiB"; }
    snprintf(s, sizeof(s), "%.2f %s", v, u);
    return s;
}

string fmt_num(uint64_t n) {
    string s = to_string(n), o;
    int c = 0;
    for (int i = (int)s.size() - 1; i >= 0; --i) {
        o.push_back(s[(size_t)i]);
        if (++c % 3 == 0 && i > 0) o.push_back(',');
    }
    reverse(o.begin(), o.end());
    return o;
}

template <class T>
struct NoInit {
    using value_type = T;
    NoInit() = default;
    template <class U> NoInit(const NoInit<U> &) {}
    T *allocate(size_t n) { return (T *)::operator new(n * sizeof(T)); }
    void deallocate(T *p, size_t) { ::operator delete(p); }
    template <class U> void construct(U *) {}
    template <class U, class... A> void construct(U *p, A &&...a) { ::new ((void *)p) U(std::forward<A>(a)...); }
    template <class U> void destroy(U *) {}
    bool operator==(const NoInit &) const { return true; }
    bool operator!=(const NoInit &) const { return false; }
};

template <class T> using RawVec = vector<T, NoInit<T>>;

string exe_dir(const char *argv0) {
    string full;
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) full.assign(buf, n);
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) full.assign(buf, (size_t)n);
#endif
    if (full.empty() && argv0) full = argv0;
    size_t cut = full.find_last_of("/\\");
    return cut == string::npos ? string(".") : full.substr(0, cut);
}

FILE *xfopen(const string &p, const char *m) {
    FILE *f = fopen(p.c_str(), m);
    if (!f) throw runtime_error("cannot open file: " + p + " (mode " + m + ")");
    return f;
}

void xwrite(FILE *f, const void *d, size_t n) {
    if (n && fwrite(d, 1, n, f) != n) throw runtime_error("short write (disk full?)");
}

void xread(FILE *f, void *d, size_t n) {
    if (n && fread(d, 1, n, f) != n) throw runtime_error("short read (truncated temp file?)");
}

inline int uvarint_len(uint64_t x) {
    int n = 1;
    while (x >= 128) { x >>= 7; ++n; }
    return n;
}

inline uint8_t *put_uvarint_p(uint8_t *p, uint64_t x) {
    while (x >= 128) { *p++ = (uint8_t)((x & 127u) | 128u); x >>= 7; }
    *p++ = (uint8_t)x;
    return p;
}

inline uint64_t get_uvarint_p(const uint8_t *&p) {
    uint64_t x = 0;
    int s = 0;
    for (;;) {
        uint8_t b = *p++;
        x |= (uint64_t)(b & 127u) << s;
        if ((b & 128u) == 0) return x;
        s += 7;
    }
}

constexpr uint32_t TMP_BLOCK = 4096;

class Pool {
public:
    using Fn = function<void(int, size_t, size_t)>;

    explicit Pool(int nthreads) : nt_(max(1, nthreads)) {
        ws_.reserve((size_t)max(0, nt_ - 1));
        for (int t = 1; t < nt_; ++t) ws_.emplace_back([this, t] { loop(t); });
    }

    ~Pool() {
        {
            lock_guard<mutex> lk(m_);
            stop_ = true;
            ++gen_;
        }
        cv_s_.notify_all();
        for (auto &w : ws_) if (w.joinable()) w.join();
    }

    int threads() const { return nt_; }

    void run(size_t n, size_t chunk, const Fn &fn) { dispatch(n, max<size_t>(1, chunk), false, fn, 8192); }
    void run_blocks(size_t n, size_t chunk, const Fn &fn) { dispatch(n, max<size_t>(1, chunk), false, fn, 2); }
    void run_ranges(size_t n, const Fn &fn) { dispatch(n, 0, true, fn, 0); }

private:
    void dispatch(size_t n, size_t chunk, bool ranges, const Fn &fn, size_t min_par) {
        if (n == 0) return;
        if (nt_ == 1 || (!ranges && n < min_par)) { fn(0, 0, n); return; }
        {
            lock_guard<mutex> lk(m_);
            fn_ = &fn; n_ = n; chunk_ = chunk; ranges_ = ranges;
            next_.store(0, memory_order_relaxed);
            done_ = 0;
            ++gen_;
        }
        cv_s_.notify_all();
        work(0);
        unique_lock<mutex> lk(m_);
        cv_d_.wait(lk, [&] { return done_ == nt_ - 1; });
        fn_ = nullptr;
    }

    void work(int tid) {
        if (ranges_) {
            size_t blk = (n_ + (size_t)nt_ - 1) / (size_t)nt_;
            size_t b = (size_t)tid * blk, e = min(n_, b + blk);
            if (b < e) (*fn_)(tid, b, e);
        } else {
            for (;;) {
                size_t b = next_.fetch_add(chunk_, memory_order_relaxed);
                if (b >= n_) break;
                (*fn_)(tid, b, min(n_, b + chunk_));
            }
        }
    }

    void loop(int tid) {
        uint64_t seen = 0;
        for (;;) {
            {
                unique_lock<mutex> lk(m_);
                cv_s_.wait(lk, [&] { return stop_ || gen_ != seen; });
                if (stop_) return;
                seen = gen_;
            }
            work(tid);
            {
                lock_guard<mutex> lk(m_);
                ++done_;
                if (done_ == nt_ - 1) cv_d_.notify_one();
            }
        }
    }

    int nt_;
    vector<thread> ws_;
    mutex m_;
    condition_variable cv_s_, cv_d_;
    const Fn *fn_ = nullptr;
    atomic<size_t> next_{0};
    size_t n_ = 0, chunk_ = 0;
    bool ranges_ = false;
    uint64_t gen_ = 0;
    int done_ = 0;
    bool stop_ = false;
};

template <class T>
void lsd_in_cache(T *p, size_t m, int bits, RawVec<T> &tmp) {
    if (m < 2 || bits <= 0) return;
    if (m <= 32) { sort(p, p + m); return; }
    const int passes = max(1, (bits + 10) / 11);
    const int ib = (bits + passes - 1) / passes;
    const size_t nb = (size_t)1 << ib;
    const uint64_t mask = nb - 1;
    tmp.resize(m);
    static thread_local vector<uint32_t> cnt;
    cnt.resize(nb);
    T *src = p, *dst = tmp.data();
    for (int q = 0; q < passes; ++q) {
        const int sh = q * ib;
        fill(cnt.begin(), cnt.end(), 0u);
        for (size_t i = 0; i < m; ++i) ++cnt[(size_t)((src[i] >> sh) & (T)mask)];
        uint32_t acc = 0;
        for (size_t b = 0; b < nb; ++b) { uint32_t c = cnt[b]; cnt[b] = acc; acc += c; }
        for (size_t i = 0; i < m; ++i) dst[cnt[(size_t)((src[i] >> sh) & (T)mask)]++] = src[i];
        swap(src, dst);
    }
    if (src != p) memcpy(p, src, m * sizeof(T));
}

template <class T, class A>
void radix_sort_unique(Pool &pool, vector<T, A> &a, vector<T, A> &scratch, int key_bits) {
    const size_t n = a.size();

    constexpr size_t TARGET = 32768 / sizeof(T);
    int msd = 1;
    while (msd < key_bits && msd < 16 && (n >> msd) > TARGET) ++msd;
    const int shift = key_bits - msd;
    const int low_bits = shift;
    scratch.resize(n);
    const int NT = pool.threads();
    const size_t NB = (size_t)1 << msd;
    const uint64_t MASK = NB - 1;
    static vector<size_t> off, total;
    off.assign((size_t)NT * NB, 0);
    total.assign(NB, 0);

    static vector<size_t> bstart;
    bstart.assign(NB + 1, 0);

    {
        const T *src = a.data();
        T *dst = scratch.data();
        pool.run_ranges(n, [&](int tid, size_t b, size_t e) {
            size_t *h = off.data() + (size_t)tid * NB;
            for (size_t i = b; i < e; ++i) ++h[(size_t)((src[i] >> shift) & (T)MASK)];
        });
        pool.run(NB, 256, [&](int, size_t b0, size_t b1) {
            for (size_t bkt = b0; bkt < b1; ++bkt) {
                size_t s = 0;
                for (int t = 0; t < NT; ++t) s += off[(size_t)t * NB + bkt];
                total[bkt] = s;
            }
        });
        size_t acc = 0;
        for (size_t bkt = 0; bkt < NB; ++bkt) { size_t c = total[bkt]; total[bkt] = acc; bstart[bkt] = acc; acc += c; }
        bstart[NB] = acc;
        pool.run(NB, 256, [&](int, size_t b0, size_t b1) {
            for (size_t bkt = b0; bkt < b1; ++bkt) {
                size_t s = total[bkt];
                for (int t = 0; t < NT; ++t) {
                    size_t &slot = off[(size_t)t * NB + bkt];
                    size_t c = slot;
                    slot = s;
                    s += c;
                }
            }
        });
        pool.run_ranges(n, [&](int tid, size_t b, size_t e) {
            size_t *h = off.data() + (size_t)tid * NB;
            for (size_t i = b; i < e; ++i) dst[h[(size_t)((src[i] >> shift) & (T)MASK)]++] = src[i];
        });
    }

    static vector<RawVec<T>> tls;
    if ((int)tls.size() < NT) tls.resize((size_t)NT);
    static RawVec<size_t> bcnt;
    bcnt.resize(NB + 1);
    bcnt[0] = 0;
    T *base = scratch.data();
    pool.run_blocks(NB, 8, [&](int tid, size_t b0, size_t b1) {
        RawVec<T> &tmp = tls[(size_t)tid];
        for (size_t b = b0; b < b1; ++b) {
            T *p = base + bstart[b];
            const size_t m = bstart[b + 1] - bstart[b];
            if (low_bits > 0) lsd_in_cache(p, m, low_bits, tmp);
            size_t w = 0;
            for (size_t i = 0; i < m; ++i) if (!i || p[i] != p[i - 1]) p[w++] = p[i];
            bcnt[b + 1] = w;
        }
    });
    for (size_t b = 0; b < NB; ++b) bcnt[b + 1] += bcnt[b];

    a.resize(bcnt[NB]);
    T *out = a.data();
    pool.run_blocks(NB, 8, [&](int, size_t b0, size_t b1) {
        for (size_t b = b0; b < b1; ++b) {
            const size_t w = bcnt[b + 1] - bcnt[b];
            if (w) memcpy(out + bcnt[b], base + bstart[b], w * sizeof(T));
        }
    });
}

template <class T, class A>
void sort_unique(Pool &pool, vector<T, A> &a, vector<T, A> &scratch, int key_bits) {
    if (a.size() < 2) return;
    if (a.size() < 200000) {
        sort(a.begin(), a.end());
        a.erase(unique(a.begin(), a.end()), a.end());
        return;
    }
    radix_sort_unique(pool, a, scratch, key_bits);
    if (scratch.capacity() > a.capacity() * 4 + (1u << 20)) vector<T, A>().swap(scratch);
}

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

constexpr uint32_t BLOCK_SIZE = 256;

constexpr uint32_t PROB_SCALE = 10000000u;

inline uint32_t quantize_prob(double p) {
    if (!(p > 0.0)) return 0;
    double q = p * (double)PROB_SCALE + 0.5;
    if (q >= (double)PROB_SCALE) return PROB_SCALE;
    return (uint32_t)q;
}

struct Config {
    int target = 512, target_exp = 9, base = 9, key_bits = 32;
    uint64_t key_space = 0;
    int max_sum = 0, layer_count = 0, threads = 1, seed_tiles = 4, progress_every = 32;
    uint32_t threshold = 0;
    bool threshold_enabled = false, profile = false;
    string tmpdir, output;
};

template <class Key, int BASE>
struct Solver {
    static constexpr int FN = FREE_N;
    static constexpr size_t EMIT = 512;
    static constexpr size_t DCACHE = 4096;
    using KV = RawVec<Key>;
    using BV = RawVec<uint8_t>;

    Config &cfg;
    Pool pool;
    int8_t cfg_of[1 << CELLS];
    Key pw[FN + 1];
    int max_layer;

    Prof prof;
    RawVec<Key> dcache_buf;
    RawVec<uint16_t> mask_buf;
    RawVec<uint8_t> ecnt_buf;
    KV merge_prev, merge_samp;
    vector<KV> merge_buf;
    RawVec<Key> merge_split;
    RawVec<size_t> merge_sz;
    atomic<uint64_t> miss_child{0};
    atomic<uint64_t> miss_move{0};

    struct LT {
        int nfree = 0;
        int slot[4] = {0, 0, 0, 0};
        vector<Key> contrib;
        vector<uint8_t> flags;
        vector<uint16_t> wmask;
        vector<uint32_t> outd;
    };
    LT lt[NCFG][8][2];

    vector<uint64_t> p_off, r_off, p_cnt, r_cnt, p_bytes, r_bytes, pv_off;

    explicit Solver(Config &c) : cfg(c), pool(c.threads) {
        pw[0] = 1;
        for (int i = 1; i <= FN; ++i) pw[i] = (Key)(pw[i - 1] * (Key)BASE);
        memcpy(cfg_of, GEO.cfg_of, sizeof(cfg_of));
        max_layer = cfg.max_sum / 2;
        dcache_buf.assign((size_t)pool.threads() * DCACHE, (Key)0);
        build_line_tables();
    }

    static void compress4(const int *line, bool toward_end, int *out, bool &won) {
        int seq[4];
        for (int k = 0; k < 4; ++k) seq[k] = toward_end ? line[3 - k] : line[k];
        int xs[4], n = 0;
        for (int k = 0; k < 4; ++k) if (seq[k] != 0) xs[n++] = seq[k];
        int o[4] = {0, 0, 0, 0}, m = 0;
        won = false;
        for (int i = 0; i < n;) {
            if (i + 1 < n && xs[i] == xs[i + 1] && xs[i] != WALLSYM) {
                int mg = xs[i] + 1;
                if (mg >= BASE) won = true;
                o[m++] = mg;
                i += 2;
            } else {
                o[m++] = xs[i++];
            }
        }
        for (int k = 0; k < 4; ++k) out[k] = toward_end ? o[3 - k] : o[k];
    }

    void build_line_tables() {
        for (int g = 0; g < NCFG; ++g)
        for (int p = 0; p < 8; ++p) {
            int nf = 0, slots[4] = {0, 0, 0, 0}, fpos[4] = {0, 0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                const int cell = GEO.path[p][k];
                if (GEO.free_slot[g][cell] >= 0) { slots[nf] = GEO.free_slot[g][cell]; fpos[nf] = k; ++nf; }
            }
            for (int o = 0; o < 2; ++o) {
                LT &t = lt[g][p][o];
                t.nfree = nf;
                for (int j = 0; j < nf; ++j) t.slot[j] = slots[j];
                size_t sz = 1;
                for (int j = 0; j < nf; ++j) sz *= (size_t)BASE;
                t.contrib.assign(sz, (Key)0);
                t.flags.assign(sz, 0);
                t.wmask.assign(sz, 0);
                t.outd.assign(sz, 0);
                for (size_t code = 0; code < sz; ++code) {
                    int fd[4] = {0, 0, 0, 0};
                    size_t c = code;
                    for (int j = 0; j < nf; ++j) { fd[j] = (int)(c % (size_t)BASE); c /= (size_t)BASE; }
                    int line[4];
                    for (int k = 0; k < 4; ++k) line[k] = GEO.free_slot[g][GEO.path[p][k]] < 0 ? WALLSYM : 0;
                    for (int j = 0; j < nf; ++j) line[fpos[j]] = fd[j];

                    int outl[4];
                    bool won = false;
                    compress4(line, o == 1, outl, won);

                    bool wall_moved = false, changed = false;
                    uint16_t wm = 0;
                    uint32_t od = 0;
                    for (int k = 0; k < 4; ++k) {
                        if ((line[k] == WALLSYM) != (outl[k] == WALLSYM)) wall_moved = true;
                        if (line[k] != outl[k]) changed = true;
                        if (outl[k] == WALLSYM) wm = (uint16_t)(wm | (1u << GEO.path[p][k]));
                        od |= (uint32_t)(outl[k] & 255) << (8 * k);
                    }
                    Key ctr = 0;
                    if (!won && !wall_moved) for (int j = 0; j < nf; ++j) ctr += (Key)outl[fpos[j]] * pw[slots[j]];

                    t.contrib[code] = ctr;
                    t.flags[code] = (uint8_t)((changed ? 1 : 0) | (won ? 2 : 0) | (wall_moved ? 4 : 0));
                    t.wmask[code] = wm;
                    t.outd[code] = od;
                }
            }
        }
    }

    static constexpr uint32_t HALF = (uint32_t)ipow_u64((uint64_t)BASE, FN / 2);
    static constexpr Key P10 = (Key)ipow_u64((uint64_t)BASE, FN);

    inline int decode(Key key, uint8_t *d) const {
        const int g = (int)(key / P10);
        Key rest = (Key)(key - (Key)g * P10);
        uint32_t lo = (uint32_t)(rest % (Key)HALF);
        uint32_t hi = (uint32_t)(rest / (Key)HALF);
        for (int i = 0; i < FN / 2; ++i) {
            d[i] = (uint8_t)(lo % BASE);
            lo /= BASE;
            d[i + FN / 2] = (uint8_t)(hi % BASE);
            hi /= BASE;
        }
        return g;
    }

    inline uint16_t empty_mask(const uint8_t *d) const {
        uint16_t m = 0;
        for (int i = 0; i < FN; ++i) if (!d[i]) m = (uint16_t)(m | (1u << i));
        return m;
    }

    inline size_t line_code(const uint8_t *d, const LT &t) const {
        switch (t.nfree) {
            case 0: return 0;
            case 1: return (size_t)d[t.slot[0]];
            case 2: return (size_t)d[t.slot[0]] + (size_t)d[t.slot[1]] * BASE;
            case 3: return (size_t)d[t.slot[0]] + 
                            ((size_t)d[t.slot[1]] + (size_t)d[t.slot[2]] * BASE) * BASE;
            default: return (size_t)d[t.slot[0]] + 
                            ((size_t)d[t.slot[1]] + 
                             ((size_t)d[t.slot[2]] + (size_t)d[t.slot[3]] * BASE) * BASE) * BASE;
        }
    }

    Key slow_key(int g, int dir, const uint8_t *d, uint16_t nm) const {
        const int ng = cfg_of[nm];
        int b[CELLS];
        const int p0 = (dir < 2) ? 4 : 0, o = (dir == 1 || dir == 3) ? 1 : 0;
        for (int L = 0; L < 4; ++L) {
            const LT &t = lt[g][p0 + L][o];
            const uint32_t w = t.outd[line_code(d, t)];
            const int *pp = GEO.path[p0 + L];
            for (int k = 0; k < 4; ++k) b[pp[k]] = (int)((w >> (8 * k)) & 255u);
        }
        Key key = (Key)ng * P10;
        const int *fc = GEO.free_cell[ng];
        for (int i = 0; i < FN; ++i) key += (Key)b[fc[i]] * pw[i];
        return key;
    }

    inline void move_all(int g, const uint8_t *d, Key *nk, uint8_t *fl) const {
        const LT (*L)[2] = lt[g];
        Key k[4] = {0, 0, 0, 0};
        uint8_t f[4] = {0, 0, 0, 0};
        uint16_t wm[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            const LT &r0 = L[i][0], &r1 = L[i][1];
            const size_t rc = line_code(d, r0);
            k[2] += r0.contrib[rc]; f[2] = (uint8_t)(f[2] | r0.flags[rc]); wm[2] = (uint16_t)(wm[2] | r0.wmask[rc]);
            k[3] += r1.contrib[rc]; f[3] = (uint8_t)(f[3] | r1.flags[rc]); wm[3] = (uint16_t)(wm[3] | r1.wmask[rc]);
            const LT &c0 = L[4 + i][0], &c1 = L[4 + i][1];
            const size_t cc = line_code(d, c0);
            k[0] += c0.contrib[cc]; f[0] = (uint8_t)(f[0] | c0.flags[cc]); wm[0] = (uint16_t)(wm[0] | c0.wmask[cc]);
            k[1] += c1.contrib[cc]; f[1] = (uint8_t)(f[1] | c1.flags[cc]); wm[1] = (uint16_t)(wm[1] | c1.wmask[cc]);
        }
        const Key base_g = (Key)g * P10;
        for (int dir = 0; dir < 4; ++dir) {
            uint8_t r = (uint8_t)(f[dir] & 3);
            if (f[dir] & 4) {
                const int ng = cfg_of[wm[dir]];
                if (ng < 0) r |= 4;
                else if (!(r & 2)) nk[dir] = slow_key(g, dir, d, wm[dir]);
            } else {
                nk[dir] = base_g + k[dir];
            }
            fl[dir] = r;
        }
    }

    inline bool is_win(uint8_t fl) const { return (fl & 2) && !(fl & 4); }

    string ppath() const { return cfg.tmpdir + "/p.bin"; }
    string rpath() const { return cfg.tmpdir + "/r.bin"; }
    string pvpath() const { return cfg.tmpdir + "/pv.bin"; }

    void write_layer(FILE *f, const KV &keys, BV &buf) {
        const size_t n = keys.size();
        if (!n) { buf.clear(); return; }
        const size_t nb = (n + TMP_BLOCK - 1) / TMP_BLOCK;
        static vector<uint32_t> boff;
        boff.assign(nb + 1, 0);

        pool.run_blocks(nb, 4, [&](int, size_t b0, size_t b1) {
            for (size_t b = b0; b < b1; ++b) {
                size_t i0 = b * TMP_BLOCK, i1 = min(n, i0 + TMP_BLOCK);
                uint32_t len = (uint32_t)uvarint_len((uint64_t)keys[i0]);
                for (size_t i = i0 + 1; i < i1; ++i) len += (uint32_t)uvarint_len((uint64_t)keys[i] - (uint64_t)keys[i - 1]);
                boff[b + 1] = len;
            }
        });
        for (size_t b = 0; b < nb; ++b) boff[b + 1] += boff[b];

        const size_t hdr = 4 + (nb + 1) * 4;
        buf.resize(hdr + boff[nb]);
        uint32_t nb32 = (uint32_t)nb;
        memcpy(buf.data(), &nb32, 4);
        memcpy(buf.data() + 4, boff.data(), (nb + 1) * 4);
        uint8_t *base = buf.data() + hdr;

        pool.run_blocks(nb, 4, [&](int, size_t b0, size_t b1) {
            for (size_t b = b0; b < b1; ++b) {
                size_t i0 = b * TMP_BLOCK, i1 = min(n, i0 + TMP_BLOCK);
                uint8_t *p = base + boff[b];
                p = put_uvarint_p(p, (uint64_t)keys[i0]);
                for (size_t i = i0 + 1; i < i1; ++i) p = put_uvarint_p(p, (uint64_t)keys[i] - (uint64_t)keys[i - 1]);
            }
        });
        xwrite(f, buf.data(), buf.size());
    }

    void read_layer(FILE *f, uint64_t off, uint64_t cnt, uint64_t nbytes, 
                    KV &keys, BV &buf) {
        keys.clear();
        if (!cnt) return;
        if (T_FSEEK(f, (int64_t)off, SEEK_SET) != 0) throw runtime_error("seek failed on temp file");
        buf.resize((size_t)nbytes);
        xread(f, buf.data(), buf.size());

        uint32_t nb = 0;
        memcpy(&nb, buf.data(), 4);
        const size_t hdr = 4 + ((size_t)nb + 1) * 4;
        if (hdr > buf.size()) throw runtime_error("corrupt temp layer header");
        static vector<uint32_t> boff;
        boff.assign((size_t)nb + 1, 0);
        memcpy(boff.data(), buf.data() + 4, ((size_t)nb + 1) * 4);
        const uint8_t *base = buf.data() + hdr;

        keys.resize((size_t)cnt);
        pool.run_blocks(nb, 4, [&](int, size_t b0, size_t b1) {
            for (size_t b = b0; b < b1; ++b) {
                const uint8_t *p = base + boff[b];
                size_t i0 = b * TMP_BLOCK, i1 = min((size_t)cnt, i0 + TMP_BLOCK);
                uint64_t cur = get_uvarint_p(p);
                keys[i0] = (Key)cur;
                for (size_t i = i0 + 1; i < i1; ++i) { cur += get_uvarint_p(p); keys[i] = (Key)cur; }
            }
        });
    }

    void moves_from_p(const KV &pk, KV &out) {
        const size_t n = pk.size();
        out.resize(n * 4);
        Key *dst = out.data();
        atomic<size_t> cursor{0};
        pool.run(n, 16384, [&](int tid, size_t b, size_t e) {
            Key *dcache = dcache_buf.data() + (size_t)tid * DCACHE;
            Key buf[EMIT];
            size_t k = 0;
            uint8_t d[FN];
            Key nk[4];
            uint8_t fl[4];
            for (size_t i = b; i < e; ++i) {
                const int g = decode(pk[i], d);
                move_all(g, d, nk, fl);
                for (int dir = 0; dir < 4; ++dir) {
                    const Key v = nk[dir];
                    const bool ok = (fl[dir] & 7u) == 1u;
                    Key &slot = dcache[(size_t)((v * (Key)0x9E3779B1u) >> 13) & (DCACHE - 1)];
                    const bool dup = (slot == v) & ok;
                    if (ok) slot = v;
                    buf[k] = v;
                    k += (size_t)(ok && !dup);
                    if (k == EMIT) {
                        memcpy(dst + cursor.fetch_add(k, memory_order_relaxed), buf, k * sizeof(Key));
                        k = 0;
                    }
                }
            }
            if (k) memcpy(dst + cursor.fetch_add(k, memory_order_relaxed), buf, k * sizeof(Key));
        });
        out.resize(cursor.load());
    }

    void prepare_pv_offsets() {
        pv_off.resize((size_t)cfg.layer_count + 1);
        uint64_t offset = 0;
        for (int L = 0; L < cfg.layer_count; ++L) {
            pv_off[(size_t)L] = offset;
            offset += r_cnt[(size_t)L] * sizeof(uint32_t);
        }
    }

    void read_value_layer(FILE *f, int layer, RawVec<uint32_t> &vals) const {
        const size_t n = (size_t)r_cnt[(size_t)layer];
        vals.resize(n);
        if (!n) return;
        if (T_FSEEK(f, (int64_t)pv_off[(size_t)layer], SEEK_SET) != 0)
            throw runtime_error("seek failed on temp value file");
        xread(f, vals.data(), n * sizeof(uint32_t));
    }

    uint64_t seed_pending(vector<KV> &pending) const {
        uint64_t nseed = 0;
        for (uint32_t sel = 1; sel < (1u << FN); ++sel) {
            const int k = popcount16((uint16_t)sel);
            if (k > cfg.seed_tiles) continue;
            for (uint32_t m = 0; m < (1u << k); ++m) {
                Key key = 0;
                int s = 0, j = 0;
                for (int i = 0; i < FN; ++i) {
                    if (!((sel >> i) & 1u)) continue;
                    const int e = ((m >> j++) & 1u) ? 2 : 1;
                    key += (Key)e * pw[i];
                    s += 1 << e;
                }
                pending[(size_t)(s / 2)].push_back(key);
                ++nseed;
            }
        }
        return nseed;
    }

    void spawn_merge(const KV &rk, const uint16_t *mask, int v, KV &out) {
        const size_t n = rk.size();
        constexpr Key SENT = ~(Key)0;
        Key off[FN];
        for (int i = 0; i < FN; ++i) off[i] = (Key)((Key)v * pw[i]);

        merge_prev.swap(out);
        const Key *pv = merge_prev.data();
        const size_t pn = merge_prev.size();
        if (!n && !pn) { out.clear(); return; }

        const int NC = max(8, pool.threads() * 8);
        merge_samp.clear();
        for (size_t t = 0; t < 64; ++t) {
            if (n) { size_t j = n * t / 64; if (j < n) for (int i = 0; i < FN; ++i) merge_samp.push_back((Key)(rk[j] + off[i])); }
            if (pn) { size_t j = pn * t / 64; if (j < pn) merge_samp.push_back(pv[j]); }
        }
        sort(merge_samp.begin(), merge_samp.end());
        merge_split.resize((size_t)NC + 1);
        merge_split[0] = 0;
        merge_split[(size_t)NC] = SENT;
        for (int c = 1; c < NC; ++c) merge_split[(size_t)c] = merge_samp.empty() ? SENT : merge_samp[merge_samp.size() * (size_t)c / (size_t)NC];
        for (int c = 1; c < NC; ++c) if (merge_split[(size_t)c] < merge_split[(size_t)c - 1]) merge_split[(size_t)c] = merge_split[(size_t)c - 1];

        if ((int)merge_buf.size() < NC) merge_buf.resize((size_t)NC);
        merge_sz.resize((size_t)NC + 1);
        merge_sz[0] = 0;
        const Key *rp = rk.data();

        pool.run_blocks((size_t)NC, 1, [&](int, size_t c0, size_t c1) {
            for (size_t c = c0; c < c1; ++c) {
                const Key K0 = merge_split[c], K1 = merge_split[c + 1];
                KV &B = merge_buf[c];
                B.clear();
                size_t idx[FN];
                Key head[FN];
                for (int i = 0; i < FN; ++i) {
                    const Key lo = K0 > off[i] ? (Key)(K0 - off[i]) : (Key)0;
                    size_t j = (size_t)(lower_bound(rp, rp + n, lo) - rp);
                    while (j < n && !((mask[j] >> i) & 1u)) ++j;
                    idx[i] = j;
                    head[i] = j < n ? (Key)(rp[j] + off[i]) : SENT;
                }
                size_t pj = (size_t)(lower_bound(pv, pv + pn, K0) - pv);
                Key ph = pj < pn ? pv[pj] : SENT;
                for (;;) {
                    Key m = ph;
                    for (int i = 0; i < FN; ++i) m = min(m, head[i]);
                    if (m >= K1) break;
                    B.push_back(m);
                    for (int i = 0; i < FN; ++i)
                        if (head[i] == m) {
                            size_t j = idx[i] + 1;
                            while (j < n && !((mask[j] >> i) & 1u)) ++j;
                            idx[i] = j;
                            head[i] = j < n ? (Key)(rp[j] + off[i]) : SENT;
                        }
                    if (ph == m) { ++pj; ph = pj < pn ? pv[pj] : SENT; }
                }
                merge_sz[c + 1] = B.size();
            }
        });

        for (int c = 0; c < NC; ++c) merge_sz[(size_t)c + 1] += merge_sz[(size_t)c];
        out.resize(merge_sz[(size_t)NC]);
        Key *dst = out.data();
        pool.run_blocks((size_t)NC, 1, [&](int, size_t c0, size_t c1) {
            for (size_t c = c0; c < c1; ++c)
                if (!merge_buf[c].empty()) memcpy(dst + merge_sz[c], merge_buf[c].data(), merge_buf[c].size() * sizeof(Key));
        });
    }

    void generate() {
        Timer tm;
        fprintf(stderr, "[gen] 开始正向分层生成 (seed_tiles=%d)\n", cfg.seed_tiles);

        vector<KV> pending((size_t)cfg.layer_count + 3);
        vector<uint8_t> pend_sorted((size_t)cfg.layer_count + 3, 0);

        fprintf(stderr, "[gen] 种子局面 %s 个\n", fmt_num(seed_pending(pending)).c_str());

        {
            KV sc;
            for (size_t i = 0; i < pending.size(); ++i)
                if (!pending[i].empty()) {
                    sort_unique(pool, pending[i], sc, cfg.key_bits);
                    pend_sorted[i] = 1;
                }
        }

        p_off.assign((size_t)cfg.layer_count, 0);
        r_off.assign((size_t)cfg.layer_count, 0);
        p_cnt.assign((size_t)cfg.layer_count, 0);
        r_cnt.assign((size_t)cfg.layer_count, 0);
        p_bytes.assign((size_t)cfg.layer_count, 0);
        r_bytes.assign((size_t)cfg.layer_count, 0);

        T_MKDIR(cfg.tmpdir.c_str());
        FILE *pf = xfopen(ppath(), "wb");
        FILE *rf = xfopen(rpath(), "wb");
        setvbuf(pf, nullptr, _IOFBF, 1u << 22);
        setvbuf(rf, nullptr, _IOFBF, 1u << 22);

        KV scratch, P, R, batch;
        BV iobuf;
        uint64_t ptot = 0, rtot = 0, poff = 0, roff = 0;
        size_t peak_layer = 0;

        for (int L = 1; L <= max_layer; ++L) {
            if (pending[(size_t)L].empty()) continue;

            Timer sec;
            const bool already = pend_sorted[(size_t)L] != 0;
            P.swap(pending[(size_t)L]);
            KV().swap(pending[(size_t)L]);
            if (!already) sort_unique(pool, P, scratch, cfg.key_bits);
            prof.add("A1 sort P(L)", sec.lap());

            p_off[(size_t)L] = poff;
            p_cnt[(size_t)L] = P.size();
            write_layer(pf, P, iobuf);
            prof.add("A2 write P", sec.lap());
            p_bytes[(size_t)L] = iobuf.size();
            poff += iobuf.size();
            ptot += P.size();

            batch.clear();
            moves_from_p(P, batch);
            prof.add("A3 moves P->R", sec.lap());
            sort_unique(pool, batch, scratch, cfg.key_bits);
            prof.add("A4 sort R", sec.lap());
            R.swap(batch);
            P.clear();

            r_off[(size_t)L] = roff;
            r_cnt[(size_t)L] = R.size();
            write_layer(rf, R, iobuf);
            prof.add("A5 write R", sec.lap());
            r_bytes[(size_t)L] = iobuf.size();
            roff += iobuf.size();
            rtot += R.size();

            if (!R.empty()) {
                mask_buf.resize(R.size());
                uint16_t *mk = mask_buf.data();
                pool.run(R.size(), 65536, [&](int, size_t b, size_t e) {
                    uint8_t d[FN];
                    for (size_t i = b; i < e; ++i) { decode(R[i], d); mk[i] = empty_mask(d); }
                });
                prof.add("A6 spawn mask", sec.lap());
                if (L + 1 < cfg.layer_count) { spawn_merge(R, mk, 1, pending[(size_t)L + 1]); pend_sorted[(size_t)L + 1] = 1; }
                if (L + 2 < cfg.layer_count) { spawn_merge(R, mk, 2, pending[(size_t)L + 2]); pend_sorted[(size_t)L + 2] = 1; }
                prof.add("A7 spawn merge", sec.lap());
            }
            peak_layer = max(peak_layer, max(p_cnt[(size_t)L], r_cnt[(size_t)L]));
            R.clear();

            if (L % cfg.progress_every == 0) 
                fprintf(stderr, "[gen] layer %4d/%d (sum=%5d)  P=%-12s R=%-12s  累计 P=%s R=%s  %s\n",
                        L, max_layer, L * 2, fmt_num(p_cnt[(size_t)L]).c_str(),
                        fmt_num(r_cnt[(size_t)L]).c_str(), fmt_num(ptot).c_str(),
                        fmt_num(rtot).c_str(), fmt_secs(tm.seconds()).c_str());
        }

        fclose(pf);
        fclose(rf);
        if (cfg.profile) prof.dump("Phase A");
        fprintf(stderr, "[gen] 完成：P 状态 %s，R 状态 %s，峰值单层 %s\n",
                fmt_num(ptot).c_str(), fmt_num(rtot).c_str(), fmt_num(peak_layer).c_str());
        fprintf(stderr, "[gen] 临时文件 p.bin %s  r.bin %s，用时 %s\n",
                fmt_bytes(poff).c_str(), fmt_bytes(roff).c_str(), fmt_secs(tm.seconds()).c_str());
    }

    struct RIndex {
        int shift = 0;
        RawVec<uint32_t> bucket;
        const Key *keys = nullptr;

        inline void prefetch(Key key) const {
            size_t b = (size_t)((uint64_t)key >> shift);
            if (b + 1 < bucket.size()) T_PREFETCH(&bucket[b]);
        }

        inline uint32_t find(Key key) const {
            size_t b = (size_t)((uint64_t)key >> shift);
            if (b + 1 >= bucket.size()) return UINT32_MAX;
            uint32_t lo = bucket[b], hi = bucket[b + 1];
            for (uint32_t i = lo; i < hi; ++i) if (keys[i] == key) return i;
            return UINT32_MAX;
        }
    };

    void build_index(RIndex &ix, const KV &keys, uint64_t key_space) {
        ix.keys = keys.data();
        if (keys.empty()) { ix.shift = 0; ix.bucket.assign(2, 0); return; }
        uint64_t want = max<uint64_t>(1024, keys.size() * 2);
        int sh = 0;
        while ((key_space >> sh) > want) ++sh;
        ix.shift = sh;
        size_t nb = (size_t)(key_space >> sh) + 1;
        ix.bucket.resize(nb + 1);
        const size_t n = keys.size();
        pool.run(nb, 65536, [&](int, size_t b0, size_t b1) {
            size_t idx = (size_t)(lower_bound(keys.begin(), keys.end(), (Key)((uint64_t)b0 << sh)) - keys.begin());
            for (size_t b = b0; b < b1; ++b) {
                while (idx < n && ((uint64_t)keys[idx] >> sh) < b) ++idx;
                ix.bucket[b] = (uint32_t)idx;
            }
        });
        ix.bucket[nb] = (uint32_t)n;
    }

    struct PLayer {
        int layer = -1;
        KV keys;
        RawVec<double> vals;
        void clear() { keys.clear(); vals.clear(); layer = -1; }
    };

    void calc_r_layer(const KV &rk, RawVec<double> &rv, 
                      const PLayer &p2, const PLayer &p4) {
        const size_t n = rk.size();
        if (!n) { rv.clear(); return; }
        rv.resize(n);

        mask_buf.resize(n);
        ecnt_buf.resize(n);
        uint16_t *mask = mask_buf.data();
        uint8_t *ecnt = ecnt_buf.data();
        pool.run(n, 65536, [&](int, size_t b, size_t e) {
            uint8_t d[FN];
            for (size_t i = b; i < e; ++i) {
                decode(rk[i], d);
                uint16_t m = empty_mask(d);
                mask[i] = m;
                ecnt[i] = (uint8_t)popcount16(m);
            }
        });

        pool.run_ranges(n, [&](int, size_t b, size_t e) {
            if (b >= e) return;
            const Key *pk[2];
            const double *pv[2];
            size_t pn[2];
            for (int v = 0; v < 2; ++v) {
                const PLayer &P = v ? p4 : p2;
                pk[v] = P.keys.data();
                pv[v] = P.vals.data();
                pn[v] = P.keys.size();
            }
            size_t cur[FN][2];
            for (int i = 0; i < FN; ++i)
                for (int v = 0; v < 2; ++v)
                    cur[i][v] = pn[v] ? (size_t)(lower_bound(pk[v], pk[v] + pn[v], 
                                        (Key)(rk[b] + (Key)((v + 1) * pw[i]))) - pk[v]) : 0;

            uint64_t local_miss = 0;
            for (size_t idx = b; idx < e; ++idx) {
                const uint16_t m = mask[idx];
                if (!m) { rv[idx] = 0.0; continue; }
                const Key k = rk[idx];
                double a = 0.0;
                for (int i = 0; i < FN; ++i) {
                    if (!((m >> i) & 1u)) continue;
                    for (int v = 0; v < 2; ++v) {
                        if (!pn[v]) { ++local_miss; continue; }
                        const Key child = (Key)(k + (Key)((v + 1) * pw[i]));
                        size_t c = cur[i][v];
                        const Key *kk = pk[v];
                        while (c < pn[v] && kk[c] < child) ++c;
                        cur[i][v] = c;
                        if (c < pn[v] && kk[c] == child) a += (v ? 0.1 : 0.9) * pv[v][c];
                        else ++local_miss;
                    }
                }
                rv[idx] = a / (double)ecnt[idx];
            }
            if (local_miss) miss_child.fetch_add(local_miss, memory_order_relaxed);
        });
    }

    static double prob_value(double value) { return value; }
    static double prob_value(uint32_t value) { return (double)value / (double)PROB_SCALE; }

    template <class V>
    void calc_p_layer(const KV &pk, RawVec<double> &pv,
                      const RawVec<V> &rv, const RIndex &ix, bool immediate_win = true) {
        const size_t n = pk.size();
        if (!n) { pv.clear(); return; }
        pv.resize(n);
        pool.run(n, 32768, [&](int, size_t b, size_t e) {
            uint8_t d[FN];
            uint64_t local_miss = 0;
            Key nk[4];
            uint8_t fl[4];
            for (size_t i = b; i < e; ++i) {
                const int g = decode(pk[i], d);
                move_all(g, d, nk, fl);
                bool win = false, usable[4];
                for (int dir = 0; dir < 4; ++dir) {
                    usable[dir] = (fl[dir] & 7u) == 1u;
                    if (is_win(fl[dir])) win = true;
                }
                if (immediate_win && win) { pv[i] = 1.0; continue; }
                for (int dir = 0; dir < 4; ++dir) if (usable[dir]) ix.prefetch(nk[dir]);
                double best = 0.0;
                for (int dir = 0; dir < 4; ++dir) {
                    if (!usable[dir]) continue;
                    uint32_t j = ix.find(nk[dir]);
                    if (j != UINT32_MAX) { double v = prob_value(rv[j]); if (v > best) best = v; }
                    else ++local_miss;
                }
                pv[i] = best;
            }
            if (local_miss) miss_move.fetch_add(local_miss, memory_order_relaxed);
        });
    }

    struct Exporter {
        FILE *f = nullptr;
        vector<TLayerDir> dir;
        uint64_t pos = 0;
        uint64_t states = 0;
        const Config *cfg = nullptr;

        void open(const Config &c) {
            cfg = &c;
            f = xfopen(c.output, "wb+");
            setvbuf(f, nullptr, _IOFBF, 1u << 22);
            dir.assign((size_t)c.layer_count, TLayerDir{0, 0, 0, 0, 0, 0});
            THeader h{};
            vector<uint8_t> zeros(sizeof(THeader) + dir.size() * sizeof(TLayerDir), 0);
            memcpy(zeros.data(), &h, sizeof(h));
            xwrite(f, zeros.data(), zeros.size());
            pos = zeros.size();
        }

        void pad(uint64_t align) {
            static const char z[64] = {};
            while (pos % align) { uint64_t n = min<uint64_t>(align - (pos % align), 64); xwrite(f, z, (size_t)n); pos += n; }
        }

        RawVec<uint32_t> q, keep, boff;
        RawVec<size_t> chcnt;
        RawVec<uint8_t> kdata, qv, bfirst_buf;

        template <class K>
        void write_layer(int layer, const K *keys, size_t n, const RawVec<double> &vals, Pool &pool) {
            q.resize(n);
            pool.run(n, 65536, [&](int, size_t b, size_t d) {
                for (size_t i = b; i < d; ++i) q[i] = quantize_prob(vals[i]);
            });
            write_quantized_layer(layer, keys, n, q, pool);
        }

        template <class K>
        void write_quantized_layer(int layer, const K *keys, size_t n, const RawVec<uint32_t> &vals, Pool &pool) {
            TLayerDir &e = dir[(size_t)layer];
            e.n_states = 0;
            e.n_blocks = 0;
            if (!n) return;

            keep.resize(n);
            constexpr size_t CH = 1u << 16;
            const size_t nch = (n + CH - 1) / CH;
            chcnt.resize(nch + 1);
            chcnt[0] = 0;
            pool.run_blocks(nch, 8, [&](int, size_t c0, size_t c1) {
                for (size_t c = c0; c < c1; ++c) {
                    size_t b = c * CH, d = min(n, b + CH), k = 0;
                    for (size_t i = b; i < d; ++i) if (vals[i]) ++k;
                    chcnt[c + 1] = k;
                }
            });
            for (size_t c = 0; c < nch; ++c) chcnt[c + 1] += chcnt[c];
            const size_t m = chcnt[nch];
            if (!m) return;
            pool.run_blocks(nch, 8, [&](int, size_t c0, size_t c1) {
                for (size_t c = c0; c < c1; ++c) {
                    size_t b = c * CH, d = min(n, b + CH), w = chcnt[c];
                    for (size_t i = b; i < d; ++i) if (vals[i]) keep[w++] = (uint32_t)i;
                }
            });

            const size_t nb = (m + BLOCK_SIZE - 1) / BLOCK_SIZE;
            boff.resize(nb + 1);
            boff[0] = 0;
            bfirst_buf.resize(nb * sizeof(K));
            K *bfirst = (K *)bfirst_buf.data();

            pool.run_blocks(nb, 32, [&](int, size_t b0, size_t b1) {
                for (size_t b = b0; b < b1; ++b) {
                    size_t i0 = b * BLOCK_SIZE, i1 = min(m, i0 + BLOCK_SIZE);
                    bfirst[b] = keys[keep[i0]];
                    uint32_t len = 1;
                    for (size_t i = i0 + 1; i < i1; ++i) len += (uint32_t)uvarint_len((uint64_t)keys[keep[i]] - (uint64_t)keys[keep[i - 1]]);
                    boff[b + 1] = len;
                }
            });
            for (size_t b = 0; b < nb; ++b) boff[b + 1] += boff[b];

            kdata.resize(boff[nb]);
            qv.resize(m * 3);
            pool.run_blocks(nb, 32, [&](int, size_t b0, size_t b1) {
                for (size_t b = b0; b < b1; ++b) {
                    size_t i0 = b * BLOCK_SIZE, i1 = min(m, i0 + BLOCK_SIZE);
                    uint8_t *p = kdata.data() + boff[b];
                    *p++ = 0;
                    for (size_t i = i0 + 1; i < i1; ++i) p = put_uvarint_p(p, (uint64_t)keys[keep[i]] - (uint64_t)keys[keep[i - 1]]);
                    for (size_t i = i0; i < i1; ++i) {
                        uint32_t v = vals[keep[i]];
                        uint8_t *o = qv.data() + i * 3;
                        o[0] = (uint8_t)(v & 255u);
                        o[1] = (uint8_t)((v >> 8) & 255u);
                        o[2] = (uint8_t)((v >> 16) & 255u);
                    }
                }
            });

            const uint32_t cnt = (uint32_t)m;
            e.n_states = cnt;
            e.n_blocks = (uint32_t)nb;

            pad(8); e.block_first_off = pos; xwrite(f, bfirst, nb * sizeof(K));         pos += nb * sizeof(K);
            pad(8); e.block_off_off = pos;   xwrite(f, boff.data(), (nb + 1) * 4);     pos += (nb + 1) * 4;
            pad(8); e.key_data_off = pos;    xwrite(f, kdata.data(), kdata.size());    pos += kdata.size();
            pad(8); e.values_off = pos;      xwrite(f, qv.data(), qv.size());          pos += qv.size();
            states += cnt;
        }

        void finish(const Config &c, uint32_t key_bytes) {
            THeader h{};
            memcpy(h.magic, ENDGAME_MAGIC, strlen(ENDGAME_MAGIC));
            h.version = 1;
            h.rows = ROWS; h.cols = COLS;
            h.wall_mask = GEO.mask[0];
            h.free_count = FREE_N;
            h.cfg_count = NCFG;
            for (int i = 0; i < NCFG && i < 16; ++i) h.cfg_mask[i] = GEO.mask[i];
            h.key_bytes = key_bytes;
            h.target = (uint32_t)c.target;
            h.target_exp = (uint32_t)c.target_exp;
            h.base = (uint32_t)c.base;
            h.key_space = c.key_space;
            h.layer_count = (uint32_t)c.layer_count;
            h.block_size = BLOCK_SIZE;
            h.prob_scale = PROB_SCALE;
            h.value_bytes = 3;
            h.seed_tiles = (uint32_t)c.seed_tiles;
            h.state_count = states;
            h.layer_dir_off = sizeof(THeader);
            h.file_size = pos;
            if (T_FSEEK(f, 0, SEEK_SET) != 0) throw runtime_error("seek failed on output");
            xwrite(f, &h, sizeof(h));
            xwrite(f, dir.data(), dir.size() * sizeof(TLayerDir));
            fclose(f);
            f = nullptr;
        }
    };

    void partition_closure_p(KV &pk, const RawVec<double> &pv, KV &continuing_p,
                             RawVec<Key> &work, RawVec<size_t> &kept_counts,
                             RawVec<size_t> &continuing_counts) {
        constexpr size_t CH = 1u << 16;
        const size_t nch = (pk.size() + CH - 1) / CH;
        kept_counts.resize(nch + 1);
        continuing_counts.resize(nch + 1);
        kept_counts[0] = 0;
        continuing_counts[0] = 0;
        pool.run_blocks(nch, 8, [&](int, size_t c0, size_t c1) {
            for (size_t c = c0; c < c1; ++c) {
                const size_t b = c * CH, e = min(pk.size(), b + CH);
                size_t kept = 0;
                size_t continuing = 0;
                for (size_t i = b; i < e; ++i) {
                    const uint32_t q = quantize_prob(pv[i]);
                    if (q >= cfg.threshold) {
                        ++kept;
                        continuing += q < PROB_SCALE;
                    }
                }
                kept_counts[c + 1] = kept;
                continuing_counts[c + 1] = continuing;
            }
        });
        for (size_t c = 0; c < nch; ++c) {
            kept_counts[c + 1] += kept_counts[c];
            continuing_counts[c + 1] += continuing_counts[c];
        }
        work.resize(kept_counts[nch]);
        continuing_p.resize(continuing_counts[nch]);
        pool.run_blocks(nch, 8, [&](int, size_t c0, size_t c1) {
            for (size_t c = c0; c < c1; ++c) {
                const size_t b = c * CH, e = min(pk.size(), b + CH);
                size_t kept = kept_counts[c];
                size_t continuing = continuing_counts[c];
                for (size_t i = b; i < e; ++i) {
                    const uint32_t q = quantize_prob(pv[i]);
                    if (q >= cfg.threshold) {
                        work[kept++] = pk[i];
                        if (q < PROB_SCALE) continuing_p[continuing++] = pk[i];
                    }
                }
            }
        });
        pk.swap(work);
    }

    void intersect_closure_r(const KV &moves, const KV &rk, const RawVec<uint32_t> &rv,
                             KV &kept_rk, RawVec<uint32_t> &kept_rv, RawVec<size_t> &chcnt) {
        constexpr size_t CH = 1u << 16;
        const size_t nch = (moves.size() + CH - 1) / CH;
        chcnt.resize(nch + 1);
        chcnt[0] = 0;
        pool.run_blocks(nch, 8, [&](int, size_t c0, size_t c1) {
            for (size_t c = c0; c < c1; ++c) {
                const size_t b = c * CH, e = min(moves.size(), b + CH);
                size_t ri = (size_t)(lower_bound(rk.begin(), rk.end(), moves[b]) - rk.begin());
                size_t kept = 0;
                for (size_t i = b; i < e; ++i) {
                    while (ri < rk.size() && rk[ri] < moves[i]) ++ri;
                    kept += ri < rk.size() && rk[ri] == moves[i];
                }
                chcnt[c + 1] = kept;
            }
        });
        for (size_t c = 0; c < nch; ++c) chcnt[c + 1] += chcnt[c];
        kept_rk.resize(chcnt[nch]);
        kept_rv.resize(chcnt[nch]);
        pool.run_blocks(nch, 8, [&](int, size_t c0, size_t c1) {
            for (size_t c = c0; c < c1; ++c) {
                const size_t b = c * CH, e = min(moves.size(), b + CH);
                size_t ri = (size_t)(lower_bound(rk.begin(), rk.end(), moves[b]) - rk.begin());
                size_t w = chcnt[c];
                for (size_t i = b; i < e; ++i) {
                    while (ri < rk.size() && rk[ri] < moves[i]) ++ri;
                    if (ri < rk.size() && rk[ri] == moves[i]) {
                        kept_rk[w] = moves[i];
                        kept_rv[w++] = rv[ri];
                    }
                }
            }
        });
    }

    void export_closure(FILE *rf, const vector<uint64_t> &rbytes, int max_L, Exporter &ex) {
        Timer tm;
        fprintf(stderr, "[closure] 开始正向 P 可达闭包 (threshold=%u/%u)\n",
                cfg.threshold, PROB_SCALE);

        FILE *vf = xfopen(pvpath(), "rb");
        setvbuf(vf, nullptr, _IOFBF, 1u << 22);
        vector<KV> pending((size_t)cfg.layer_count + 3);
        seed_pending(pending);

        KV scratch, pk, moves, continuing_p, continuing_moves, rk, kept_rk, continuing_rk, work;
        RawVec<double> pv;
        RawVec<uint32_t> rv, kept_rv, continuing_rv;
        RawVec<size_t> chcnt, continuing_chcnt;
        BV iobuf;
        RIndex ix;
        uint64_t retained_p = 0, reached_r = 0;

        for (int L = 1; L <= max_L; ++L) {
            if (pending[(size_t)L].empty()) continue;

            pk.swap(pending[(size_t)L]);
            sort_unique(pool, pk, scratch, cfg.key_bits);
            read_layer(rf, r_off[(size_t)L], r_cnt[(size_t)L], rbytes[(size_t)L], rk, iobuf);
            read_value_layer(vf, L, rv);
            build_index(ix, rk, cfg.key_space);
            calc_p_layer(pk, pv, rv, ix);

            partition_closure_p(pk, pv, continuing_p, work, chcnt, continuing_chcnt);
            retained_p += pk.size();

            moves_from_p(pk, moves);
            sort_unique(pool, moves, scratch, cfg.key_bits);
            intersect_closure_r(moves, rk, rv, kept_rk, kept_rv, chcnt);
            reached_r += kept_rk.size();

            dcache_buf.assign((size_t)pool.threads() * DCACHE, (Key)0);
            moves_from_p(continuing_p, continuing_moves);
            sort_unique(pool, continuing_moves, scratch, cfg.key_bits);
            intersect_closure_r(continuing_moves, rk, rv, continuing_rk, continuing_rv, chcnt);

            if (!continuing_rk.empty()) {
                mask_buf.resize(continuing_rk.size());
                uint16_t *mask = mask_buf.data();
                pool.run(continuing_rk.size(), 65536, [&](int, size_t b, size_t e) {
                    uint8_t d[FN];
                    for (size_t i = b; i < e; ++i) { decode(continuing_rk[i], d); mask[i] = empty_mask(d); }
                });
                if (L + 1 < cfg.layer_count) spawn_merge(continuing_rk, mask, 1, pending[(size_t)L + 1]);
                if (L + 2 < cfg.layer_count) spawn_merge(continuing_rk, mask, 2, pending[(size_t)L + 2]);
            }

            if (cfg.threshold) {
                size_t w = 0;
                for (size_t i = 0; i < kept_rk.size(); ++i) {
                    if (kept_rv[i] >= cfg.threshold) {
                        kept_rk[w] = kept_rk[i];
                        kept_rv[w++] = kept_rv[i];
                    }
                }
                kept_rk.resize(w);
                kept_rv.resize(w);
            }
            ex.write_quantized_layer(L, kept_rk.data(), kept_rk.size(), kept_rv, pool);

            if (L % cfg.progress_every == 0)
                fprintf(stderr, "[closure] layer %4d/%d  retained P=%-12s reached R=%-12s  已导出 %s  %s\n",
                        L, max_L, fmt_num(retained_p).c_str(), fmt_num(reached_r).c_str(),
                        fmt_num(ex.states).c_str(), fmt_secs(tm.seconds()).c_str());
        }
        fclose(vf);
        fprintf(stderr, "[closure] 完成：保留 P=%s，可达 R=%s，用时 %s\n",
                fmt_num(retained_p).c_str(), fmt_num(reached_r).c_str(), fmt_secs(tm.seconds()).c_str());
    }

    void solve() {
        Timer tm;
        if (cfg.threshold) prepare_pv_offsets();

        int max_L = 0;
        for (int L = 0; L < cfg.layer_count; ++L) if (p_cnt[(size_t)L] || r_cnt[(size_t)L]) max_L = L;

        fprintf(stderr, "[dp] 开始逆向 DP，最高非空层 %d (sum=%d)\n", max_L, max_L * 2);

        FILE *pf = xfopen(ppath(), "rb");
        FILE *rf = xfopen(rpath(), "rb");
        setvbuf(pf, nullptr, _IOFBF, 1u << 22);
        setvbuf(rf, nullptr, _IOFBF, 1u << 22);

        FILE *vf = cfg.threshold ? xfopen(pvpath(), "wb+") : nullptr;
        if (vf) setvbuf(vf, nullptr, _IOFBF, 1u << 22);

        Exporter ex;
        ex.open(cfg);

        PLayer P[3];
        KV rk;
        RawVec<double> rv;
        RawVec<uint32_t> rvq;
        BV iobuf;
        RIndex ix;
        uint64_t exported = 0;

        for (int L = max_L; L >= 1; --L) {
            PLayer &cur = P[(size_t)(L % 3)];
            const PLayer &n1 = P[(size_t)((L + 1) % 3)];
            const PLayer &n2 = P[(size_t)((L + 2) % 3)];

            Timer sec;
            read_layer(rf, r_off[(size_t)L], r_cnt[(size_t)L], r_bytes[(size_t)L], rk, iobuf);
            prof.add("B1 read R", sec.lap());
            if (!rk.empty()) {
                calc_r_layer(rk, rv, n1, n2);
                prof.add("B2 calc V_R", sec.lap());
                if (vf) {
                    rvq.resize(rv.size());
                    pool.run(rv.size(), 65536, [&](int, size_t b, size_t e) {
                        for (size_t i = b; i < e; ++i) rvq[i] = quantize_prob(rv[i]);
                    });
                    if (T_FSEEK(vf, (int64_t)pv_off[(size_t)L], SEEK_SET) != 0)
                        throw runtime_error("seek failed on temp value file");
                    xwrite(vf, rvq.data(), rvq.size() * sizeof(uint32_t));
                } else {
                    ex.write_layer(L, rk.data(), rk.size(), rv, pool);
                }
                prof.add(vf ? "B3 write R values" : "B3 export", sec.lap());
                exported = ex.states;
            } else {
                rv.clear();
            }

            cur.clear();
            cur.layer = L;
            read_layer(pf, p_off[(size_t)L], p_cnt[(size_t)L], p_bytes[(size_t)L], cur.keys, iobuf);
            prof.add("B4 read P", sec.lap());
            if (!cur.keys.empty()) {
                build_index(ix, rk, cfg.key_space);
                prof.add("B5 build index", sec.lap());
                calc_p_layer(cur.keys, cur.vals, rv, ix);
                prof.add("B6 calc V_P", sec.lap());

            }

            rk.clear();
            rv.clear();

            if (L % cfg.progress_every == 0)
                fprintf(stderr, "[dp]  layer %4d/%d (sum=%5d)  R=%-12s P=%-12s  已导出 %s  %s\n",
                        L, max_L, L * 2, fmt_num(r_cnt[(size_t)L]).c_str(),
                        fmt_num(p_cnt[(size_t)L]).c_str(), fmt_num(exported).c_str(),
                        fmt_secs(tm.seconds()).c_str());
        }

        fclose(pf);
        if (vf) {
            fclose(vf);
            export_closure(rf, r_bytes, max_L, ex);
        }
        fclose(rf);
        ex.finish(cfg, (uint32_t)sizeof(Key));

        if (cfg.profile) prof.dump("Phase B");
        fprintf(stderr, "[dp] 完成，用时 %s\n", fmt_secs(tm.seconds()).c_str());
        fprintf(stderr, "[out] 文件 %s  大小 %s  导出状态 %s\n",
                cfg.output.c_str(), fmt_bytes(ex.pos).c_str(), fmt_num(ex.states).c_str());
        if (miss_child.load() || miss_move.load())
            fprintf(stderr, "[warn] 一致性异常：child miss=%s, move miss=%s（正常应为 0）\n",
                    fmt_num(miss_child.load()).c_str(), fmt_num(miss_move.load()).c_str());
    }

    void report_opening() {
        fprintf(stderr, "[out] 查开局：query_%s %d " ENDGAME_OPENING "   （6 个大数格填 >= target 的十六进制位）\n", TAG, cfg.target);
    }

    void cleanup_tmp() const {
        remove(ppath().c_str());
        remove(rpath().c_str());
        remove(pvpath().c_str());
        if (T_RMDIR(cfg.tmpdir.c_str()) == 0)
            fprintf(stderr, "[tmp] 已清理临时目录 %s\n", cfg.tmpdir.c_str());
        else
            fprintf(stderr, "[tmp] 临时文件已删除（目录 %s 非空，保留）\n", cfg.tmpdir.c_str());
    }

    int run() {
        Timer total;
        fprintf(stderr, "target=%d base=%d key_space=%s key=%zuB layers=%d threads=%d "
                "cfg0=0x%04X 值=7位小数(u24)\n",
                cfg.target, cfg.base, fmt_num(cfg.key_space).c_str(), sizeof(Key),
                cfg.layer_count, cfg.threads, (unsigned)GEO.mask[0]);
        int rc;
        try {
            generate();
            solve();
            rc = (miss_child.load() || miss_move.load()) ? 2 : 0;
        } catch (...) {
            cleanup_tmp();
            throw;
        }
        cleanup_tmp();
        report_opening();
        fprintf(stderr, "总用时 %s\n", fmt_secs(total.seconds()).c_str());
        return rc;
    }
};

template <int BASE>
int dispatch_base(Config &cfg) {
    constexpr uint64_t KS = (uint64_t)NCFG * ipow_u64((uint64_t)BASE, FREE_N);
    if constexpr (KS <= 0xFFFFFFFFull) {
        return Solver<uint32_t, BASE>(cfg).run();
    } else {
        return Solver<uint64_t, BASE>(cfg).run();
    }
}

bool is_pow2(int x) { return x > 0 && (x & (x - 1)) == 0; }

int log2i(int x) { int e = 0; while ((1 << e) < x) ++e; return e; }

void usage(const char *a0) {
    fprintf(stderr,
            "用法: %s [选项]\n"
            "  --target N          目标牌，2 的幂，8..2048（默认 512）\n"
            "  --threads N         线程数（默认 = 硬件并发数）\n"
            "  --output FILE       成品表（默认 <可执行文件目录>/table_<定式>_<target>.bin）\n"
            "  --seed-tiles N      开局最多几张 2/4（默认 4）\n"
            "  --threshold          启用按目标自动选择的 P/R 胜率剪枝（512: 0.05；1024: 0.01）\n"
            "  --progress N        每 N 层打印一次进度（默认 32）\n"
            "  --profile           打印分段计时\n", a0);
}

Config parse_args(int argc, char **argv) {
    const string home = exe_dir(argc > 0 ? argv[0] : nullptr);
    Config c;
    c.threads = (int)thread::hardware_concurrency();
    if (c.threads <= 0) c.threads = 1;

    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        auto val = [&](const string &n) -> string {
            if (i + 1 >= argc) throw runtime_error("missing value for " + n);
            return argv[++i];
        };
        if (a == "--target") c.target = stoi(val(a));
        else if (a == "--threads") c.threads = max(1, stoi(val(a)));
        else if (a == "--output") c.output = val(a);
        else if (a == "--seed-tiles") c.seed_tiles = stoi(val(a));
        else if (a == "--threshold") c.threshold_enabled = true;
        else if (a == "--progress") c.progress_every = max(1, stoi(val(a)));
        else if (a == "--profile") c.profile = true;
        else if (a == "-h" || a == "--help") { usage(argv[0]); exit(0); }
        else throw runtime_error("unknown argument: " + a);
    }

    if (!is_pow2(c.target) || c.target < 8 || c.target > 2048) throw runtime_error("--target must be a power of two in [8, 2048]");
    if (c.seed_tiles < 1 || c.seed_tiles > FREE_N) throw runtime_error("--seed-tiles must be in [1, 10]");
    if (c.threshold_enabled) {
        if (c.target == 512) c.threshold = quantize_prob(0.05);
        else if (c.target == 1024) c.threshold = quantize_prob(0.01);
        else throw runtime_error("--threshold is only supported for target 512 or 1024");
    }

    c.target_exp = log2i(c.target);
    c.base = c.target_exp;
    c.key_space = (uint64_t)NCFG * ipow_u64((uint64_t)c.base, FREE_N);
    c.key_bits = 0;
    while (((uint64_t)1 << c.key_bits) < c.key_space) ++c.key_bits;
    c.max_sum = FREE_N * (1 << (c.base - 1));
    c.layer_count = c.max_sum / 2 + 1;
    if (c.output.empty()) c.output = home + "/table_" + TAG + "_" + to_string(c.target) + ".bin";
    c.tmpdir = home + "/" ENDGAME_TMPDIR;
    return c;
}

}

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    try {
        Config cfg = parse_args(argc, argv);
        switch (cfg.base) {
            case 3:  return dispatch_base<3>(cfg);
            case 4:  return dispatch_base<4>(cfg);
            case 5:  return dispatch_base<5>(cfg);
            case 6:  return dispatch_base<6>(cfg);
            case 7:  return dispatch_base<7>(cfg);
            case 8:  return dispatch_base<8>(cfg);
            case 9:  return dispatch_base<9>(cfg);
            case 10: return dispatch_base<10>(cfg);
            case 11: return dispatch_base<11>(cfg);
            default: throw runtime_error("unsupported base");
        }
    } catch (const exception &e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}