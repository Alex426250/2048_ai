#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std;

namespace {

constexpr int N = 3;
constexpr int CELLS = N * N;
constexpr int SYMS = 8;
constexpr int DEFAULT_TARGET = 512;
constexpr int PROGRESS_SUM_STEP = 256;

constexpr uint8_t SYM_POS[SYMS][CELLS] = {
	{0, 1, 2, 3, 4, 5, 6, 7, 8},
	{2, 5, 8, 1, 4, 7, 0, 3, 6},
	{8, 7, 6, 5, 4, 3, 2, 1, 0},
	{6, 3, 0, 7, 4, 1, 8, 5, 2},
	{2, 1, 0, 5, 4, 3, 8, 7, 6},
	{6, 7, 8, 3, 4, 5, 0, 1, 2},
	{0, 3, 6, 1, 4, 7, 2, 5, 8},
	{8, 5, 2, 7, 4, 1, 6, 3, 0}
};

struct Config {
	int target = DEFAULT_TARGET;
	int target_exp = 9;
	int max_exp = 8;
	int base = 9;
	uint32_t key_space = 0;
	int max_sum = 0;
	int threads = 1;
	string output = "table_3x3_512.bin";
};

struct Timer {
	chrono::steady_clock::time_point t0 = chrono::steady_clock::now();

	double seconds() const {
		auto now = chrono::steady_clock::now();
		return chrono::duration<double>(now - t0).count();
	}
};

struct AtomicBitSet {
	unique_ptr<atomic<uint64_t>[]> words;
	size_t word_count = 0;

	explicit AtomicBitSet(uint32_t nbits)
		: words(make_unique<atomic<uint64_t>[]>((static_cast<size_t>(nbits) + 63u) >> 6)),
		  word_count((static_cast<size_t>(nbits) + 63u) >> 6) {
		for (size_t i = 0; i < word_count; ++i) words[i].store(0, memory_order_relaxed);
	}

	bool set_if_new(uint32_t idx) {
		uint64_t mask = 1ull << (idx & 63u);
		auto &word = words[idx >> 6];
		if (word.load(memory_order_relaxed) & mask) return false;
		uint64_t old = word.fetch_or(mask, memory_order_relaxed);
		return (old & mask) == 0;
	}
};

struct LineMove {
	uint16_t out = 0;
	uint8_t d0 = 0;
	uint8_t d1 = 0;
	uint8_t d2 = 0;
	bool changed = false;
	bool won = false;
};

struct Tables {
	Config cfg;
	array<uint32_t, CELLS + 1> pow_base{};
	vector<LineMove> move_left;
	vector<LineMove> move_right;
};

uint64_t pow_u64(uint64_t a, int b) {
	uint64_t r = 1;
	for (int i = 0; i<b; ++i) r *= a;
	return r;
}

bool is_power_of_two(int x) {
	return x > 0 && (x & (x-1)) == 0;
}

int log2_int(int x) {
	int e = 0;
	while ((1 << e) < x) ++e;
	return e;
}

string mib(size_t bytes) {
	double v = static_cast<double>(bytes) / 1024.0 / 1024.0;
	string s = to_string(v);
	size_t dot = s.find('.');
	if (dot != string::npos && dot + 3 <s.size()) s.resize(dot + 3);
	return s + " MiB";
}

string now_seconds(double s) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%.2fs", s);
	return buf;
}

Config parse_args(int argc, char **argv) {
	Config cfg;
	cfg.threads = static_cast<int>(thread::hardware_concurrency());
	if (cfg.threads <= 0) cfg.threads = 1;

	for (int i = 1; i < argc; ++i) {
		string arg = argv[i];

		auto need_value = [&](const string &name) -> string {
			if ( i + 1 >= argc) throw runtime_error("missing value for " + name);
			return argv[++i];
		};

		if (arg == "--target") cfg.target = stoi(need_value(arg));
		else if (arg == "--threads") cfg.threads = max(1, stoi(need_value(arg)));
		else if (arg == "--output") cfg.output = need_value(arg);
		else if (arg == "--help" || arg == "-h") cerr << "Usage: " << argv[0] << " [--target N] [--threads N] [--output FILE]\n", exit(0);
		else throw runtime_error("unknown argument: " + arg);
	}

	if (!is_power_of_two(cfg.target) || cfg.target < 8) throw runtime_error("--target must be a power of two >= 8");
	cfg.target_exp = log2_int(cfg.target);
	cfg.max_exp = cfg.target_exp - 1;
	cfg.base = cfg.target_exp;
	uint64_t key_space = pow_u64(static_cast<uint64_t>(cfg.base), CELLS);
	if (key_space > UINT32_MAX) throw runtime_error("key space exceeds uint32_t; this builder is intended for 512/1024 3x3 tables");
	cfg.key_space = static_cast<uint32_t>(key_space);
	cfg.max_sum = CELLS * (1 << cfg.max_exp);

	if (cfg.output == "table_3x3_512.bin" && cfg.target != DEFAULT_TARGET) cfg.output = "table_3x3_" + to_string(cfg.target) + ".bin";

	return cfg;
}

uint16_t line_code(const Tables &tab, int a, int b, int c) {
	return static_cast<uint16_t>(a + tab.cfg.base * b + tab.cfg.base * tab.cfg.base * c);
}

array<int, 3> decode_line(const Tables &tab, uint16_t code) {
	int base = tab.cfg.base;
	return {code % base, (code / base) % base, (code / (base * base)) % base};
}

uint16_t reverse_line(const Tables &tab, uint16_t code) {
	auto d = decode_line(tab, code);
	return line_code(tab, d[2], d[1], d[0]);
}

LineMove build_left_move(const Tables &tab, uint16_t code) {
	auto in = decode_line(tab, code);
	array<int, 3> vals{};
	int n = 0;
	for (int x : in) if (x != 0) vals[n++] = x;

	array<int, 3> out{};
	int m = 0;
	for (int i = 0; i < n;) {
		if ( i + 1 < n && vals[i] == vals[i + 1]) {
			int merged = vals[i] + 1;
			if (merged >= tab.cfg.target_exp) return {0, 0, 0, 0, true, true};
			out[m++] = merged;
			i += 2;
		} else out[m++] = vals[i++];
	}

	uint16_t out_code = line_code(tab, out[0], out[1], out[2]);
	return {out_code, static_cast<uint8_t>(out[0]), static_cast<uint8_t>(out[1]), static_cast<uint8_t>(out[2]), out_code != code, false};
}

void build_tables(Tables &tab) {
	tab.pow_base[0] = 1;
	for (int i = 1; i <= CELLS; ++i) tab.pow_base[i] = tab.pow_base[i - 1] * static_cast<uint32_t>(tab.cfg.base);

	int line_count = tab.cfg.base * tab.cfg.base * tab.cfg.base;
	tab.move_left.resize(line_count);
	tab.move_right.resize(line_count);

	for (int code = 0; code < line_count; ++code) {
		LineMove left = build_left_move(tab, static_cast<uint16_t>(code));
		tab.move_left[code] = left;

		uint16_t reversed = reverse_line(tab, static_cast<uint16_t>(code));
		LineMove left_reversed = build_left_move(tab, reversed);

		LineMove right;
		right.won = left_reversed.won;
		right.changed = left_reversed.changed;
		if (!right.won) {
			right.out = reverse_line(tab, left_reversed.out);
			right.d0 = left_reversed.d2;
			right.d1 = left_reversed.d1;
			right.d2 = left_reversed.d0;
			right.changed = right.out != code;
		}
		tab.move_right[code] = right;
	}
}

void decode_board(const Tables &tab, uint32_t key, array<uint8_t, CELLS> &d) {
	int base = tab.cfg.base;
	for (int i = 0; i < CELLS; ++i) {
		d[i] =  static_cast<uint8_t>(key % base);
		key /= static_cast<uint32_t>(base);
	}
}

int tile_value(int exp) {
	return exp == 0 ? 0 : (1 << exp);
}

array<uint32_t, SYMS> symmetry_keys_from_digits(const Tables &tab, const array<uint8_t, CELLS> &d) {
	const auto &p = tab.pow_base;
	array<uint32_t, SYMS> k{};
	for (int s = 0; s < SYMS; ++s)
		for (int i = 0; i < CELLS; ++i)
			k[s] += static_cast<uint32_t>(d[i]) * p[SYM_POS[s][i]];
	return k;
}
uint32_t canonical_key_from_digits(const Tables &tab, const array<uint8_t, CELLS> &d) {
	auto keys = symmetry_keys_from_digits(tab, d);
	return *min_element(keys.begin(), keys.end());
}

uint32_t min_sym_key_with_tile(const Tables &tab, const array<uint32_t, SYMS> &sym, int pos, uint32_t tile) {
	uint32_t best = UINT32_MAX;
	for (int i = 0; i < SYMS; ++i) best = min(best, sym[i] + tile * tab.pow_base[SYM_POS[i][pos]]);
	return best;
}

void write_symmetry_values(const Tables &tab, float *raw, uint32_t key, float value) {
	array<uint8_t, CELLS> d{};
	decode_board(tab, key, d);
	auto keys = symmetry_keys_from_digits(tab, d);
	for (uint32_t sym : keys) raw[sym] = value;
}

bool apply_line(const LineMove &lm, array<uint8_t, CELLS> &out, int a, int b, int c, bool &changed) {
	if (lm.won) return true;
	out[a] = lm.d0;
	out[b] = lm.d1;
	out[c] = lm.d2;
	changed = changed || lm.changed;
	return false;
}

bool move_board_after(const Tables &tab, const array<uint8_t, CELLS> &in, char dir, array<uint8_t, CELLS> &out, bool &won) {
	bool changed = false;
	won = false;

	if (dir == 'L') {
		const auto &m = tab.move_left;
		const LineMove &a = m[line_code(tab, in[0], in[1], in[2])];
		if (apply_line(a, out, 0, 1, 2, changed)) return won = true;
		const LineMove &b = m[line_code(tab, in[3], in[4], in[5])];
		if (apply_line(b, out, 3, 4, 5, changed)) return won = true;
		const LineMove &c = m[line_code(tab, in[6], in[7], in[8])];
		if (apply_line(c, out, 6, 7, 8, changed)) return won = true;
	} else if (dir == 'R') {
		const auto &m = tab.move_right;
		const LineMove &a = m[line_code(tab, in[0], in[1], in[2])];
		if (apply_line(a, out, 0, 1, 2, changed)) return won = true;
		const LineMove &b = m[line_code(tab, in[3], in[4], in[5])];
		if (apply_line(b, out, 3, 4, 5, changed)) return won = true;
		const LineMove &c = m[line_code(tab, in[6], in[7], in[8])];
		if (apply_line(c, out, 6, 7, 8, changed)) return won = true;
	} else if (dir == 'U') {
		const auto &m = tab.move_left;
		const LineMove &a = m[line_code(tab, in[0], in[3], in[6])];
		if (apply_line(a, out, 0, 3, 6, changed)) return won = true;
		const LineMove &b = m[line_code(tab, in[1], in[4], in[7])];
		if (apply_line(b, out, 1, 4, 7, changed)) return won = true;
		const LineMove &c = m[line_code(tab, in[2], in[5], in[8])];
		if (apply_line(c, out, 2, 5, 8, changed)) return won = true;
	} else {
		const auto &m = tab.move_right;
		const LineMove &a = m[line_code(tab, in[0], in[3], in[6])];
		if (apply_line(a, out, 0, 3, 6, changed)) return won = true;
		const LineMove &b = m[line_code(tab, in[1], in[4], in[7])];
		if (apply_line(b, out, 1, 4, 7, changed)) return won = true;
		const LineMove &c = m[line_code(tab, in[2], in[5], in[8])];
		if (apply_line(c, out, 2, 5, 8, changed)) return won = true;
	}

	return changed;
}

class ParallelRunner {
public:
	explicit ParallelRunner(int requested_threads)
		: threads_(max(1, requested_threads)) {
		workers_.reserve(static_cast<size_t>(max(0, threads_ - 1)));
		for (int tid = 1; tid < threads_; ++tid) workers_.emplace_back([this, tid]() { worker_loop(tid); });
	}

	~ParallelRunner() {
		{
			lock_guard<mutex> lock(m_);
			stop_ = true;
			++generation_;
		}
		cv_start_.notify_all();
		for (auto &w : workers_) if (w.joinable()) w.join();
	}

	int thread_count() const {
		return threads_;
	}

	template <typename Func>
	void run(size_t n, Func &&func) {
		if (n == 0) return;
		if (threads_ == 1 || n < 4096) {
			func(0, 0, n);
			return;
		}

		{
			lock_guard<mutex> lock(m_);
			n_ = n;
			block_ = (n + static_cast<size_t>(threads_) - 1) / static_cast<size_t>(threads_);
			finished_workers_ = 0;
			job_ = [&](int tid, size_t begin, size_t end) {func(tid, begin, end); };
			++generation_;
		}
		cv_start_.notify_all();

		size_t main_end = min(n, block_);
		func(0, 0, main_end);

		unique_lock<mutex> lock(m_);
		cv_done_.wait(lock, [&]() { return finished_workers_ == threads_ - 1; });
		job_ = nullptr;
	}

private:
	void worker_loop(int tid) {
		uint64_t observed_generation = 0;

		while (true) {
			function<void(int, size_t, size_t)> job;
			size_t n = 0;
			size_t block = 0;

			{
				unique_lock<mutex> lock(m_);
				cv_start_.wait(lock, [&]() {
					return stop_ || generation_ != observed_generation;
				});
				if (stop_) return;

				observed_generation = generation_;
				job = job_;
				n = n_;
				block = block_;
			}

			size_t begin = static_cast<size_t>(tid) * block;
			size_t end = min(n, begin + block);
			if (begin < end) job(tid, begin, end);

			{
				lock_guard<mutex> lock(m_);
				++finished_workers_;
				if (finished_workers_ == threads_ -1) cv_done_.notify_one();
			}
		}
	}

	int threads_ = 1;
	vector<thread> workers_;
	mutex m_;
	condition_variable cv_start_;
	condition_variable cv_done_;
	function<void(int, size_t, size_t)> job_;
	size_t n_ = 0;
	size_t block_ = 0;
	int finished_workers_ = 0;
	uint64_t generation_ = 0;
	bool stop_ = false;
};

struct FlatBuckets {
	vector<uint32_t> keys;
	vector<uint64_t> offsets;
};

FlatBuckets flatten_buckets(vector<vector<uint32_t>> &buckets) {
	FlatBuckets flat;
	flat.offsets.resize(buckets.size() + 1, 0);
	for (size_t i = 0; i < buckets.size(); ++i) flat.offsets[i + 1] = flat.offsets[i] + buckets[i].size();

	flat.keys.resize(static_cast<size_t>(flat.offsets.back()));
	for (size_t i = 0; i < buckets.size(); ++i) {
		if (!buckets[i].empty()) memcpy(flat.keys.data() + flat.offsets[i], buckets[i].data(), buckets[i].size() * sizeof(uint32_t));
		vector<uint32_t>().swap(buckets[i]);
	}
	vector<vector<uint32_t>>().swap(buckets);
	return flat;
}

struct StateSets {
	FlatBuckets p;
	FlatBuckets r;
};

StateSets generate_states(const Tables &tab) {
	Timer timer;
	AtomicBitSet seen_p(tab.cfg.key_space);
	AtomicBitSet seen_r(tab.cfg.key_space);
	vector<vector<uint32_t>> p_by_sum(static_cast<size_t>(tab.cfg.max_sum + 1));
	vector<vector<uint32_t>> r_by_sum(static_cast<size_t>(tab.cfg.max_sum + 1));

	cerr << "seen_p atomic bitset: " << mib(seen_p.word_count * sizeof(uint64_t)) << "\n";
	cerr << "seen_r atomic bitset: " << mib(seen_r.word_count * sizeof(uint64_t)) << "\n";

	uint64_t p_seen_count = 0;
	uint64_t r_seen_count = 0;

	for (int i = 0; i < CELLS; ++i) {
		for (int j = i + 1; j < CELLS; ++j) {
			for (int a = 1; a <= 2; ++a) {
				for (int b = 1; b <= 2; ++b) {
					array<uint8_t, CELLS> d{};
					d[i] = static_cast<uint8_t>(a);
					d[j] = static_cast<uint8_t>(b);
					uint32_t canon = canonical_key_from_digits(tab, d);
					if (seen_p.set_if_new(canon)) {
						p_by_sum[tile_value(a) + tile_value(b)].push_back(canon);
						++p_seen_count;
					}
				}
			}
		}
	}

	struct LocalGen {
		vector<uint32_t> r;
		vector<uint32_t> p2;
		vector<uint32_t> p4;
	};

	const char dirs[4] = {'L', 'R', 'U', 'D'};

	auto append_all = [](vector<uint32_t> &dst, vector<uint32_t> &src) {
		if (!src.empty()) {
			dst.insert(dst.end(), src.begin(), src.end());
			src.clear();
		}
	};

	ParallelRunner runner(tab.cfg.threads);
	vector<LocalGen> locals(static_cast<size_t>(runner.thread_count()));
	int max_pending_sum = 8;

	for (int sum = 0; sum <= max_pending_sum; ++sum) {
		auto &bucket = p_by_sum[sum];
		if (bucket.empty()) {
			if (sum % PROGRESS_SUM_STEP == 0) 
				cerr << "generate sum " << sum << ": p_seen=" << p_seen_count << ", r_seen=" << r_seen_count<< ", elapsed=" << now_seconds(timer.seconds()) << "\n";
			continue;
		}

		int local_count = runner.thread_count();
		size_t block = (bucket.size() + static_cast<size_t>(local_count) - 1) / static_cast<size_t>(local_count);

		for (int t = 0; t < local_count; ++t) {
			size_t begin = static_cast<size_t>(t) * block;
			size_t end = min(bucket.size(), begin + block);
			size_t chunk = begin < end ? end - begin : 0;
			locals[t].r.clear();
			locals[t].p2.clear();
			locals[t].p4.clear();
			locals[t].r.reserve(chunk * 2);
			locals[t].p2.reserve(chunk * 8);
			locals[t].p4.reserve(chunk * 8);
		}

		auto worker = [&](int tid, size_t begin, size_t end) {
			LocalGen &local = locals[static_cast<size_t>(tid)];

			for (size_t idx = begin; idx < end; ++idx) {
				uint32_t key = bucket[idx];
				array<uint8_t, CELLS> board{};
				decode_board(tab, key, board);

				for (char dir : dirs) {
					array<uint8_t, CELLS> after;
					bool won = false;
					bool valid = move_board_after(tab, board, dir, after, won);
					if (!valid || won) continue;

					auto sym = symmetry_keys_from_digits(tab, after);
					uint32_t r_canon = *min_element(sym.begin(), sym.end());
					if (seen_r.set_if_new(r_canon)) local.r.push_back(r_canon);

					for (int pos = 0; pos < CELLS; ++pos) {
						if (after[pos] != 0) continue;
						if (sum + 2 <= tab.cfg.max_sum) {
							uint32_t canon = min_sym_key_with_tile(tab, sym, pos, 1);
							if (seen_p.set_if_new(canon)) local.p2.push_back(canon);
						}
						if (sum + 4 <= tab.cfg.max_sum) {
							uint32_t canon = min_sym_key_with_tile(tab, sym, pos, 2);
							if (seen_p.set_if_new(canon)) local.p4.push_back(canon);
						}
					}
				}
			}
		};

		runner.run(bucket.size(), worker);

		for (auto &local : locals) {
			r_seen_count += local.r.size();
			p_seen_count += local.p2.size() + local.p4.size();
			if (!local.p2.empty()) max_pending_sum = max(max_pending_sum, sum + 2);
			if (!local.p4.empty()) max_pending_sum = max(max_pending_sum, sum + 4);
			append_all(r_by_sum[sum], local.r);
			if (sum + 2 <= tab.cfg.max_sum) append_all(p_by_sum[sum + 2], local.p2);
			if (sum + 4 <= tab.cfg.max_sum) append_all(p_by_sum[sum + 4], local.p4);
		}

		if (sum % PROGRESS_SUM_STEP == 0) 
			cerr << "generate sum " << sum << ": p_seen=" << p_seen_count << ", r_seen=" << r_seen_count<< ", elapsed=" << now_seconds(timer.seconds()) << "\n";
	}

	if (max_pending_sum < tab.cfg.max_sum && max_pending_sum % PROGRESS_SUM_STEP != 0)
		cerr << "generate sum " << max_pending_sum << ": p_seen=" << p_seen_count << ", r_seen=" << r_seen_count<< ", elapsed=" << now_seconds(timer.seconds()) << "\n";

	StateSets sets;
	sets.p = flatten_buckets(p_by_sum);
	sets.r = flatten_buckets(r_by_sum);
	cerr << "generated P states: " << sets.p.keys.size() << "\n";
	cerr << "generated R states: " << sets.r.keys.size() << "\n";
	cerr << "state generation elapsed: " << now_seconds(timer.seconds()) << "\n";
	return sets;
}

float calc_r_value(const Tables &tab, const float *p_raw, uint32_t r_key) {
	array<int, CELLS> empties{};
	int empty_count = 0;
	uint32_t key = r_key;
	int base = tab.cfg.base;

	for (int i = 0; i < CELLS; ++i) {
		if (key % static_cast<uint32_t>(base) == 0) empties[empty_count++] = i;
		key /= static_cast<uint32_t>(base);
	}

	if (empty_count == 0) return p_raw[r_key];

	double acc = 0.0;
	double inv = 1.0 / static_cast<double>(empty_count);

	for (int e = 0; e < empty_count; ++e) {
		int pos = empties[e];
		uint32_t child2 = r_key + tab.pow_base[pos];
		uint32_t child4 = r_key + 2u * tab.pow_base[pos];
		acc += inv * (0.9 * static_cast<double>(p_raw[child2]) + 0.1 * static_cast<double>(p_raw[child4]));
	}

	return static_cast<float>(acc);
}

float calc_p_value(const Tables &tab, const float *r_raw, uint32_t p_key) {
	array<uint8_t, CELLS> d{};
	decode_board(tab, p_key, d);
	const auto &p = tab.pow_base;
	float best = 0.0f;

	auto take = [&](const LineMove &a, const LineMove &b, const LineMove &c, uint32_t key) {
		if (a.won || b.won || c.won) {
			best = 1.0f;
			return;
		}
		if (!(a.changed || b.changed || c.changed)) return;
		float v = r_raw[key];
		if (v > best) best = v;
	};

	{
		const auto &m = tab.move_left;
		const LineMove &a = m[line_code(tab, d[0], d[1], d[2])];
		const LineMove &b = m[line_code(tab, d[3], d[4], d[5])];
		const LineMove &c = m[line_code(tab, d[6], d[7], d[8])];
		take(a, b, c, static_cast<uint32_t>(a.out) + static_cast<uint32_t>(b.out) * p[3] + static_cast<uint32_t>(c.out) * p[6]);
	}
	{
		const auto &m = tab.move_right;
		const LineMove &a = m[line_code(tab, d[0], d[1], d[2])];
		const LineMove &b = m[line_code(tab, d[3], d[4], d[5])];
		const LineMove &c = m[line_code(tab, d[6], d[7], d[8])];
		take(a, b, c, static_cast<uint32_t>(a.out) + static_cast<uint32_t>(b.out) * p[3] + static_cast<uint32_t>(c.out) * p[6]);
	}
	{
		const auto &m = tab.move_left;
		const LineMove &a = m[line_code(tab, d[0], d[3], d[6])];
		const LineMove &b = m[line_code(tab, d[1], d[4], d[7])];
		const LineMove &c = m[line_code(tab, d[2], d[5], d[8])];
		uint32_t key = static_cast<uint32_t>(a.d0) * p[0] + static_cast<uint32_t>(a.d1) * p[3] + static_cast<uint32_t>(a.d2) * p[6]
				       + static_cast<uint32_t>(b.d0) * p[1] + static_cast<uint32_t>(b.d1) * p[4] + static_cast<uint32_t>(b.d2) * p[7]
				       + static_cast<uint32_t>(c.d0) * p[2] + static_cast<uint32_t>(c.d1) * p[5] + static_cast<uint32_t>(c.d2) * p[8];
		take(a, b, c, key);
	}
	{
		const auto &m = tab.move_right;
		const LineMove &a = m[line_code(tab, d[0], d[3], d[6])];
		const LineMove &b = m[line_code(tab, d[1], d[4], d[7])];
		const LineMove &c = m[line_code(tab, d[2], d[5], d[8])];
		uint32_t key = static_cast<uint32_t>(a.d0) * p[0] + static_cast<uint32_t>(a.d1) * p[3] + static_cast<uint32_t>(a.d2) * p[6]
				       + static_cast<uint32_t>(b.d0) * p[1] + static_cast<uint32_t>(b.d1) * p[4] + static_cast<uint32_t>(b.d2) * p[7]
				       + static_cast<uint32_t>(c.d0) * p[2] + static_cast<uint32_t>(c.d1) * p[5] + static_cast<uint32_t>(c.d2) * p[8];
		take(a, b, c, key);
	}

	return best;
}

void compute_dp(const Tables &tab, const StateSets &sets, unique_ptr<float[]> &p_raw, unique_ptr<float[]> &r_raw) {
	Timer timer;
	p_raw.reset(new float[tab.cfg.key_space]);
	r_raw.reset(new float[tab.cfg.key_space]);

	cerr << "P_raw: " << mib(static_cast<size_t>(tab.cfg.key_space) * sizeof(float)) << " (uninitialized)\n";
	cerr << "R_raw: " << mib(static_cast<size_t>(tab.cfg.key_space) * sizeof(float)) << " (uninitialized)\n";

	ParallelRunner runner(tab.cfg.threads);
	int start_sum = tab.cfg.max_sum;
	while (start_sum > 0 && sets.r.offsets[start_sum] == sets.r.offsets[start_sum + 1] && 
			sets.p.offsets[start_sum] == sets.p.offsets[start_sum + 1]) --start_sum;
	bool printed_first_dp = false;

	for (int sum = start_sum; sum >= 0; --sum) {
		uint64_t rb = sets.r.offsets[sum];
		uint64_t re = sets.r.offsets[sum + 1];
		uint64_t rn = re - rb;

		runner.run(static_cast<size_t>(rn), [&](int, size_t begin, size_t end) {
			for (size_t local = begin; local < end; ++local) {
				uint32_t key = sets.r.keys[static_cast<size_t>(rb) + local];
				float value = calc_r_value(tab, p_raw.get(), key);
				write_symmetry_values(tab, r_raw.get(), key, value);
			}
		});

		uint64_t pb = sets.p.offsets[sum];
		uint64_t pe = sets.p.offsets[sum + 1];
		uint64_t pn = pe - pb;

		runner.run(static_cast<size_t>(pn), [&](int, size_t begin, size_t end) {
			for (size_t local = begin; local < end; ++local) {
				uint32_t key = sets.p.keys[static_cast<size_t>(pb) + local];
				float value = calc_p_value(tab, r_raw.get(), key);
				write_symmetry_values(tab, p_raw.get(), key, value);
			}
		});

		if ((rn + pn) > 0 && (!printed_first_dp || sum % PROGRESS_SUM_STEP == 0)) {
			printed_first_dp = true;
			cerr << "dp sum " << sum << ": R=" << rn << ", P="<< pn << ", elapsed=" << now_seconds(timer.seconds()) << "\n";
		}
	}

	cerr << "DP elapsed: " << now_seconds(timer.seconds()) << "\n";
}

#pragma pack(push, 1)
struct ModelHeader {
	char magic[8];
	uint32_t version, rows, cols, cells, target, target_exp, base, max_exp, key_space, block_size, block_count;
	uint64_t state_count;
	uint32_t value_count, value_bits;
	uint64_t block_keys_offset, block_offsets_offset, dict_offset, key_data_offset, value_ids_offset, file_size;
	uint32_t prob_type, reserved0;
	uint64_t reserved1, reserved2;
};
#pragma pack(pop)

uint64_t align_up(uint64_t x, uint64_t alignment) {
	return (x + alignment - 1) / alignment * alignment;
}

void write_padding(ofstream &out, uint64_t current, uint64_t target) {
	static const char zeros[64] = {};
	while (current < target) {
		uint64_t n = min<uint64_t>(sizeof(zeros), target - current);
		out.write(zeros, static_cast<streamsize>(n));
		current += n;
	}
}

void write_bytes(ofstream &out, uint64_t &pos, uint64_t off, const void *data, uint64_t bytes) {
	write_padding(out, pos, off);
	if (bytes != 0) out.write(reinterpret_cast<const char *>(data), static_cast<streamsize>(bytes));
	pos = off + bytes;
}

template <class T>
void write_vec(ofstream &out, uint64_t &pos, uint64_t off, const vector<T> &v) {
	write_bytes(out, pos, off, v.data(), static_cast<uint64_t>(v.size()) * sizeof(T));
}

uint32_t quantize_prob(float p) {
	if (p <= 0.0f) return 0;
	double q = static_cast<double>(p) * 10000000.0 + 0.5;
	if (q >= 10000000.0) return 10000000u;
	return static_cast<uint32_t>(q);
}

uint32_t bit_width(uint32_t n) {
	uint32_t v = n > 0 ? n - 1 : 0;
	uint32_t bits = 0;
	while (v != 0) ++bits, v >>= 1;
	return max(1u, bits);
}

void write_uvarint(vector<uint8_t> &out, uint32_t x) {
	while (x >= 128) {
		out.push_back(static_cast<uint8_t>((x & 127u) | 128u));
		x >>= 7;
	}
	out.push_back(static_cast<uint8_t>(x));
}

void append_u24(vector<uint8_t> &out, uint32_t x) {
	out.push_back(static_cast<uint8_t>(x & 255u));
	out.push_back(static_cast<uint8_t>((x >> 8) & 255u));
	out.push_back(static_cast<uint8_t>((x >> 16) & 255u));
}

void append_bits(vector<uint8_t> &out, uint32_t value, uint32_t bits, uint64_t &buf, uint32_t &fill) {
	buf |= static_cast<uint64_t>(value) << fill;
	fill += bits;
	while (fill >= 8) {
		out.push_back(static_cast<uint8_t>(buf & 255u));
		buf >>= 8;
		fill -= 8;
	}
}

void flush_bits(vector<uint8_t> &out, uint64_t &buf, uint32_t &fill) {
	if (fill != 0) out.push_back(static_cast<uint8_t>(buf & 255u));
	buf = 0;
	fill = 0;
}

void radix_sort_states(vector<uint64_t> &a) {
	if (a.size() < 2) return;
	vector <uint64_t> b(a.size());
	vector <size_t> cnt(1u << 16);

	for (int shift = 0; shift < 32; shift += 16) {
		fill(cnt.begin(), cnt.end(), 0);
		for (uint64_t x : a) ++cnt[(x >> shift) & 65535u];
		size_t sum  = 0;
		for (size_t &c : cnt) {
			size_t n = c;
			c = sum;
			sum += n;
		}
		for (uint64_t x : a) b[cnt[(x >> shift) & 65535u]++] = x;
		a.swap(b);
	}
}

void export_model(const Tables &tab, const FlatBuckets &keys, const float *raw) {
	Timer timer;
	constexpr uint32_t BLOCK_SIZE = 256;

	vector<uint64_t> states;
	vector<uint32_t> q_to_id(10000001u, UINT32_MAX), q_values;
	states.reserve(keys.keys.size());

	for (uint32_t key : keys.keys) {
		uint32_t q = quantize_prob(raw[key]);
		if (q == 0) continue;
		states.push_back((static_cast<uint64_t>(q) << 32) | key);
		if (q_to_id[q] == UINT32_MAX) q_to_id[q] = 0, q_values.push_back(q);
	}

	radix_sort_states(states);
	sort(q_values.begin(), q_values.end());
	for (uint32_t i = 0; i < q_values.size(); ++i) q_to_id[q_values[i]] = i;

	uint32_t value_bits = bit_width(static_cast<uint32_t>(q_values.size()));
	uint32_t block_count = static_cast<uint32_t>((states.size() + BLOCK_SIZE - 1) / BLOCK_SIZE);
	vector<uint32_t> block_keys;
	vector<uint64_t> block_offsets;
	vector<uint8_t> key_data, dict_data, value_ids;

	block_keys.reserve(block_count);
	block_offsets.reserve(static_cast<size_t>(block_count) + 1);
	key_data.reserve(states.size() * 2);
	dict_data.reserve(q_values.size() * 3);
	value_ids.reserve((static_cast<uint64_t>(states.size()) * value_bits + 7) >> 3);

	for (uint32_t q : q_values) append_u24(dict_data, q);

	uint32_t prev = 0;
	uint64_t bit_buf = 0;
	uint32_t bit_fill = 0;
	for (size_t i = 0; i < states.size(); ++i) {
		uint32_t key = static_cast<uint32_t>(states[i]);
		if((i & (BLOCK_SIZE - 1)) ==0) {
			block_keys.push_back(key);
			block_offsets.push_back(key_data.size());
			prev = key;
			write_uvarint(key_data, 0);
		} else {
			write_uvarint(key_data, key -  prev);
			prev = key;
		}
		append_bits(value_ids, q_to_id[static_cast<uint32_t>(states[i] >> 32)], value_bits, bit_buf, bit_fill);
	}
	flush_bits(value_ids, bit_buf, bit_fill);
	block_offsets.push_back(key_data.size());

	uint64_t block_keys_offset = align_up(sizeof(ModelHeader), 64), block_keys_bytes = static_cast<uint64_t>(block_keys.size()) * sizeof(uint32_t);
	uint64_t block_offsets_offset =  align_up(block_keys_offset + block_keys_bytes, 64), block_offsets_bytes = static_cast<uint64_t>(block_offsets.size()) * sizeof(uint64_t);
	uint64_t dict_offset = align_up(block_offsets_offset + block_offsets_bytes, 64), dict_bytes = static_cast<uint64_t>(dict_data.size());
	uint64_t key_data_offset = align_up(dict_offset + dict_bytes, 64), key_data_bytes = static_cast<uint64_t>(key_data.size());
	uint64_t value_ids_offset = align_up(key_data_offset + key_data_bytes, 64), value_ids_bytes = static_cast<uint64_t>(value_ids.size());
	uint64_t file_size = value_ids_offset + value_ids_bytes;

	ModelHeader h{};
	memcpy(h.magic, "3X3RQ71", 7);
	h.version = 1, h.rows = 3, h.cols = 3, h.cells = CELLS;
	h.target = static_cast<uint32_t>(tab.cfg.target), h.target_exp = static_cast<uint32_t>(tab.cfg.target_exp), h.base = static_cast<uint32_t>(tab.cfg.base), h.max_exp = static_cast<uint32_t>(tab.cfg.max_exp), h.key_space = tab.cfg.key_space;
	h.block_size = BLOCK_SIZE, h.block_count = block_count, h.state_count = static_cast<uint64_t>(states.size()), h.value_count = static_cast<uint32_t>(q_values.size()), h.value_bits = value_bits;
	h.block_keys_offset = block_keys_offset, h.block_offsets_offset = block_offsets_offset, h.dict_offset = dict_offset, h.key_data_offset = key_data_offset;
	h.value_ids_offset = value_ids_offset, h.file_size = file_size, h.prob_type = 3;

	ofstream out(tab.cfg.output, ios::binary);
	if (!out) throw runtime_error("cannot open output file: " + tab.cfg.output);

	out.write(reinterpret_cast<const char *>(&h), sizeof(h));
	uint64_t pos = sizeof(h);
	write_vec(out, pos, block_keys_offset, block_keys);
	write_vec(out, pos, block_offsets_offset, block_offsets);
	write_vec(out, pos, dict_offset, dict_data);
	write_vec(out, pos, key_data_offset, key_data);
	write_vec(out, pos, value_ids_offset, value_ids);

	if (!out) throw runtime_error("failed while writing output file: " + tab.cfg.output);
	out.close();

	cerr << "export elapsed: " << now_seconds(timer.seconds()) << "\n" << "model file: " << tab.cfg.output << "\n"
		<< "model size: " << mib(static_cast<size_t>(file_size)) << "\n" << "states exported: " << states.size() << "\n"
		<< "value_count: " << q_values.size() << "\n" << "value_bits: " << value_bits << "\n"
		<< "key data: " << mib(key_data.size()) << "\n";
}

void print_config(const Tables &tab) {
	cerr << "target: " << tab.cfg.target << "\n" << "target_exp: " << tab.cfg.target_exp << "\n"
		<< "base: " << tab.cfg.base << "\n" << "key_space: " << tab.cfg.key_space << "\n"
		<< "max_sum: " << tab.cfg.max_sum << "\n" << "threads: " << tab.cfg.threads << "\n"
		<< "output: " << tab.cfg.output << "\n";
}

} 

int main(int argc, char **argv) {
	try {
		Tables tab;
		tab.cfg = parse_args(argc, argv);
		build_tables(tab);
		print_config(tab);

		Timer total;
		StateSets sets = generate_states(tab);

		unique_ptr<float[]> p_raw;
		unique_ptr<float[]> r_raw;
		compute_dp(tab, sets, p_raw, r_raw);

		export_model(tab, sets.r, r_raw.get());

		cerr << "total elapsed: " << now_seconds(total.seconds()) << "\n";
		return 0;
	} catch (const exception &e) {
		cerr << "error: " << e.what() << "\n";
		return 1;
	}
}
