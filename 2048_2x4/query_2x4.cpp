#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

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

using namespace std;

namespace {

constexpr int ROWS = 2;
constexpr int COLS = 4;
constexpr int CELLS = ROWS * COLS;

constexpr uint8_t SYM_POS[4][CELLS] = {
	{0, 1, 2, 3, 4, 5, 6, 7},
	{7, 6, 5, 4, 3, 2, 1, 0},
	{3, 2, 1, 0, 7, 6, 5, 4},
	{4, 5, 6, 7, 0, 1, 2, 3}
};

#pragma pack(push, 1)
struct Header {
	char magic[8];
	uint32_t version, rows, cols, cells, target, target_exp, base, max_exp, key_space, block_size, block_count;
	uint64_t state_count;
	uint32_t value_count, value_bits;
	uint64_t block_keys_offset, block_offsets_offset, dict_offset, key_data_offset, value_ids_offset, file_size;
	uint32_t prob_type, reserved0;
	uint64_t reserved1, reserved2;
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
		file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) throw runtime_error("cannot open model: " + path);
		LARGE_INTEGER s;
		if (!GetFileSizeEx(file, &s) || s.QuadPart <= 0) throw runtime_error("cannot get model size: " + path);
		size = static_cast<uint64_t>(s.QuadPart);
		mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
		if (!mapping) throw runtime_error("cannot map model: " + path);
		data = static_cast<const uint8_t *>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
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
		struct stat st{};
		if (fstat(fd, &st) != 0 || st.st_size <= 0) throw runtime_error("cannot stat model: " + path);
		size = static_cast<uint64_t>(st.st_size);
		data = static_cast<const uint8_t *>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
		if (data == MAP_FAILED) {
			data = nullptr;
			throw runtime_error("cannot map model: " + path);
		}
	}
#endif
};

struct Table {
	MappedFile mf;
	Header h{};
	array<uint32_t, CELLS + 1> pow_base{};

	uint32_t u32(uint64_t p) const {
		uint32_t x;
		memcpy(&x, mf.data + p, 4);
		return x;
	}

	uint64_t u64(uint64_t p) const {
		uint64_t x;
		memcpy(&x, mf.data + p, 8);
		return x;
	}

	uint32_t block_key(uint32_t i) const { return u32(h.block_keys_offset + uint64_t(i) * 4); }
	uint64_t block_off(uint32_t i) const { return u64(h.block_offsets_offset + uint64_t(i) * 8); }

	static inline uint32_t varint(const uint8_t *&p) {
		uint32_t b0 = *p++;
		if (b0 < 128) return b0;
		uint32_t b1 = *p++;
		if (b1 < 128) return (b0 & 127u) | (b1 << 7);
		uint32_t x = (b0 & 127u) | ((b1 & 127u) << 7);
		for (int sh = 14;; sh += 7) { uint32_t b = *p++; x |= (b & 127u) << sh; if (b < 128) return x; }
	}

	void load(const string &file, uint32_t target) {
		mf.open_file(file);
		if (mf.size < sizeof(Header)) throw runtime_error("model too small: " + file);
		memcpy(&h, mf.data, sizeof(h));
		if (memcmp(h.magic, "2X4RQ71", 7) != 0 || h.version != 1 || h.rows != 2 || h.cols != 4 || h.cells != CELLS || 
			h.target != target || h.prob_type != 3 || h.file_size != mf.size) throw runtime_error("bad 2x4 R model: " + file);
		pow_base[0] = 1;
		for (int i = 1; i <= CELLS; ++i) pow_base[i] = pow_base[i - 1] * h.base;
	}

	uint32_t dict(uint32_t id) const {
		uint64_t p = h.dict_offset + uint64_t(id) * 3;
		return uint32_t(mf.data[p]) | (uint32_t(mf.data[p + 1]) << 8) | (uint32_t(mf.data[p + 2]) << 16);
	}

	uint32_t value_id(uint64_t idx) const {
		uint64_t bit = idx * h.value_bits, p = h.value_ids_offset + (bit >> 3), buf = 0;
		uint32_t shift = uint32_t(bit & 7u);
		memcpy(&buf, mf.data + p, min<uint64_t>(8, h.file_size - p));
		return uint32_t((buf >> shift) & ((1ull << h.value_bits) - 1ull));
	}

	uint32_t canonical_key(const array<uint8_t, CELLS> &d) const {
		uint32_t best = UINT32_MAX;
		for (auto &s : SYM_POS) {
			uint32_t key = 0;
			for (int i = 0; i < CELLS; ++i) key += uint32_t(d[i]) * pow_base[s[i]];
			best = min(best, key);
		}
		return best;
	}

	uint32_t lookup_q(uint32_t key) const {
		if (h.block_count == 0 || key < block_key(0)) return 0;
		uint32_t lo = 0, hi = h.block_count;
		while (lo < hi) {
			uint32_t mid = lo + ((hi - lo) >> 1);
			if (block_key(mid) <= key) lo = mid + 1;
			else hi = mid;
		}
		uint32_t b = lo - 1, cur = block_key(b);
		const uint64_t idx0 = uint64_t(b) * h.block_size;
		const uint64_t cnt = min<uint64_t>(h.block_size, h.state_count - idx0);
		const uint8_t *p = mf.data + h.key_data_offset + block_off(b);
		for (uint64_t i = 0; i < cnt; ++i) {
			cur += varint(p);
			if (cur >= key) return cur == key ? dict(value_id(idx0 + i)) : 0;
		}
		return 0;
	}

	double prob_after_move(const array<uint8_t, CELLS> &after, bool already_won) const {
		if (already_won) return 1.0;
		for (uint8_t x : after) if (x >= h.target_exp) return 1.0;
		return double(lookup_q(canonical_key(after))) / 10000000.0;
	}
};

uint8_t hex_exp(char c) {
	if ('0' <= c && c <= '9') return uint8_t(c - '0');
	if ('a' <= c && c <= 'f') return uint8_t(c - 'a' + 10);
	if ('A' <= c && c <= 'F') return uint8_t(c - 'A' + 10);
	throw runtime_error("board must be 8 hex digits");
}

template <size_t L>
bool move_line(const array<uint8_t, CELLS> &in, const array<int, L> &pos, array<uint8_t, CELLS> &out) {
	array<uint8_t, L> vals{}, line{};
	int n = 0, w = 0;
	for (int p : pos) if (in[p]) vals[n++] = in[p];
	for (int i = 0; i < n;) {
		if (i + 1 < n && vals[i] == vals[i + 1]) line[w++] =  vals[i] + 1, i += 2;
		else line[w++] = vals[i++];
	}
	bool changed = false;
	for (int i = 0; i < (int)L; ++i) out[pos[i]] = line[i], changed = changed || out[pos[i]] != in[pos[i]];
	return changed;
}

bool move_board(const array<uint8_t, CELLS> &b, char dir, array<uint8_t, CELLS> &a) {
	a.fill(0);
	bool changed = false;
	if (dir == 'L' || dir == 'R') {
		for (int r = 0; r < ROWS; ++r) {
			if (dir == 'L') changed = move_line<4>(b, {r * COLS, r * COLS + 1, r * COLS + 2, r * COLS + 3}, a) || changed;
			else changed = move_line<4>(b, {r * COLS + 3, r * COLS + 2, r * COLS + 1, r * COLS}, a) || changed;
		}
	} else {
		for (int c = 0; c < COLS; ++c) {
			if (dir == 'U') changed = move_line<2>(b, {c, c + COLS}, a) || changed;
			else changed = move_line<2>(b, {c+ COLS, c}, a) || changed;
		}
	}
	return changed;
}

void usage(const char *argv0) {
	cerr << "Usage:\n" << "  " << argv0 << " 256|512 HEX\n" << "  " << argv0
		<< " --model FILE 256|512 HEX\n" << "HEX: 0=empty, 1=2, ..., 8=256, 9=512\n";
}

} 

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#include <unordered_map>

static std::unordered_map<int, Table*> g_tables;  

extern "C" EXPORT int init_model(const char* model_path, int target) {
    if (target != 256 && target != 512) return -1;
    try {
        
        auto it = g_tables.find(target);
        if (it != g_tables.end()) {
            delete it->second;
            g_tables.erase(it);
        }
        Table* tab = new Table();
        tab->load(std::string(model_path), static_cast<uint32_t>(target));
        g_tables[target] = tab;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "init_model error: " << e.what() << std::endl;
        return -1;
    }
}

extern "C" EXPORT int query_probs(const char* board_hex, int target, double out_probs[4]) {
    if (target != 256 && target != 512) return -1;
    auto it = g_tables.find(target);
    if (it == g_tables.end()) return -2;  

    Table* tab = it->second;
    try {
        
        std::string hex(board_hex);
        if (hex.size() == 10 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
            hex = hex.substr(2);
        if (hex.size() != CELLS) return -3;

        std::array<uint8_t, CELLS> board{};
        bool already_won = false;
        for (int i = 0; i < CELLS; ++i) {
            board[i] = hex_exp(hex[i]);
            if (board[i] > 9) return -4;
            if ((target == 256 && board[i] >= 8) || (target == 512 && board[i] >= 9))
                already_won = true;
        }

        const std::array<std::pair<const char*, char>, 4> dirs = {{
            {"U", 'U'}, {"D", 'D'}, {"L", 'L'}, {"R", 'R'}
        }};

        for (size_t idx = 0; idx < dirs.size(); ++idx) {
            std::array<uint8_t, CELLS> after{};
            bool changed = move_board(board, dirs[idx].second, after);
            if (!changed) out_probs[idx] = -1.0;   
            else {
                double prob = tab->prob_after_move(after, already_won);
                out_probs[idx] = prob;
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "query_probs error: " << e.what() << std::endl;
        return -5;
    }
}

int main(int argc, char **argv) {
	try {
		string model;
		int arg = 1;
		if (argc > 1 && string(argv[1]) == "--model") {
			if (argc < 4) throw runtime_error("missing --model arguments");
			model = argv[2];
			arg = 3;
		}
		if (argc - arg != 2) {
			usage(argv[0]);
			return 1;
		}

		int target = stoi(argv[arg++]);
		if (target != 256 && target != 512) throw runtime_error("target must be 256 or 512");
		if (model.empty()) model = "table_2x4_" + to_string(target) + ".bin";

		string hex = argv[arg++];
		if (hex.size() == 10 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex = hex.substr(2);
		if (hex.size() != CELLS) throw runtime_error("board must be exactly 8 hex digits");
		array<uint8_t, CELLS> board{};
		bool already_won = false;
		for (int i = 0; i < CELLS; ++i) {
			board[i] = hex_exp(hex[i]);
			if (board[i] > 9) throw runtime_error("hex digit cannot exceed 9 for 256/512 table");
			if ((target == 256 && board[i] >= 8) || (target == 512 && board[i] >= 9)) already_won = true;
		}

		Table tab;
		tab.load(model, uint32_t(target));
		cout << fixed << setprecision(7);
		for (auto [name, dir] : array<pair<const char *, char>, 4>{{{"U", 'U'}, {"D", 'D'}, {"L", 'L'}, {"R", 'R'}}}) {
			array<uint8_t, CELLS> after{};
			if (!move_board(board, dir, after)) cout << name << " None\n";
			else cout << name << ' ' << tab.prob_after_move(after, already_won) << '\n';
		}
	} catch (const exception &e) {
		cerr << "error: " << e.what() << '\n';
		return 1;
	}
}
