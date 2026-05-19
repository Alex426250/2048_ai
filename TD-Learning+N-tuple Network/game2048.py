import random
import math

# --- 全局 BitBoard 预计算查表 ---
_row_left_table = [0] * 65536
_row_right_table = [0] * 65536
_score_table = [0] * 65536
COL_MASK = 0x000F000F000F000F

def _init_tables():
    for row in range(65536):
        vals = [(row >> (i * 4)) & 0xF for i in range(4)]
        
        # Left
        nz = [v for v in vals if v != 0]
        res_left = []
        score = 0
        i = 0
        while i < len(nz):
            if i + 1 < len(nz) and nz[i] == nz[i+1]:
                res_left.append(nz[i] + 1)
                score += (1 << (nz[i] + 1))
                i += 2
            else:
                res_left.append(nz[i])
                i += 1
        while len(res_left) < 4: res_left.append(0)
        left_val = 0
        for idx, val in enumerate(res_left): left_val |= (val << (idx * 4))
        _row_left_table[row] = left_val
        _score_table[row] = score
        
        # Right
        res_right = []
        i = len(nz) - 1
        while i >= 0:
            if i - 1 >= 0 and nz[i] == nz[i-1]:
                res_right.append(nz[i] + 1)
                i -= 2
            else:
                res_right.append(nz[i])
                i -= 1
        while len(res_right) < 4: res_right.append(0)
        res_right.reverse()
        right_val = 0
        for idx, val in enumerate(res_right): right_val |= (val << (idx * 4))
        _row_right_table[row] = right_val

_init_tables()

class Game2048:
    def __init__(self):
        self.size = 4
        self.board = 0  # 64-bit BitBoard
        self.score = 0
        self.add_new_tile()
        self.add_new_tile()

    @property
    def grid(self):
        grid_2d = [[0] * 4 for _ in range(4)]
        for i in range(16):
            val = (self.board >> (i * 4)) & 0xF
            grid_2d[i // 4][i % 4] = (1 << val) if val > 0 else 0
        return grid_2d

    @grid.setter
    def grid(self, new_grid):
        self.board = 0
        for r in range(4):
            for c in range(4):
                val = new_grid[r][c]
                if val > 0: self.board |= (int(math.log2(val)) << ((r * 4 + c) * 4))

    def get_empty_cells(self, grid=None):
        if grid is not None and isinstance(grid, list):
            return [(r, c) for r in range(4) for c in range(4) if grid[r][c] == 0]
        empty = []
        b = self.board
        for i in range(16):
            if ((b >> (i * 4)) & 0xF) == 0:
                empty.append((i // 4, i % 4))
        return empty

    def add_new_tile(self):
        empty = self.get_empty_cells()
        if empty:
            r, c = random.choice(empty)
            pos = r * 4 + c
            val = 1 if random.random() < 0.9 else 2
            self.board |= (val << (pos * 4))

    def move(self, direction):
        new_board, inc, moved = self.simulate_move(self.board, direction)
        if moved:
            self.board = new_board
            self.score += inc
            self.add_new_tile()
        return moved

    @staticmethod
    def execute_move(board, direction):
        new_board = 0
        score = 0
        if direction == 2:
            for idx in range(4):
                shift = idx * 16
                row = (board >> shift) & 0xFFFF
                new_board |= (_row_left_table[row] << shift)
                score += _score_table[row]
        elif direction == 3:
            for idx in range(4):
                shift = idx * 16
                row = (board >> shift) & 0xFFFF
                new_board |= (_row_right_table[row] << shift)
                score += _score_table[row]
        elif direction == 0:
            for col in range(4):
                c = (board >> (col * 4)) & COL_MASK
                row = (c | (c >> 12) | (c >> 24) | (c >> 36)) & 0xFFFF
                new_row = _row_left_table[row]
                score += _score_table[row]
                unpacked = (new_row | (new_row << 12) | (new_row << 24) | (new_row << 36)) & COL_MASK
                new_board |= (unpacked << (col * 4))
        elif direction == 1:
            for col in range(4):
                c = (board >> (col * 4)) & COL_MASK
                row = (c | (c >> 12) | (c >> 24) | (c >> 36)) & 0xFFFF
                new_row = _row_right_table[row]
                score += _score_table[row]
                unpacked = (new_row | (new_row << 12) | (new_row << 24) | (new_row << 36)) & COL_MASK
                new_board |= (unpacked << (col * 4))
        return new_board, score

    @staticmethod
    def simulate_move(grid_board, direction):
        if isinstance(grid_board, list):
            b = 0
            for r in range(4):
                for c in range(4):
                    val = grid_board[r][c]
                    if val > 0: b |= (int(math.log2(val)) << ((r * 4 + c) * 4))
            new_b, score = Game2048.execute_move(b, direction)
            moved = (new_b != b)
            nb_grid = [[0]*4 for _ in range(4)]
            for i in range(16):
                val = (new_b >> (i * 4)) & 0xF
                nb_grid[i // 4][i % 4] = (1 << val) if val > 0 else 0
            return nb_grid, score, moved
        else:
            new_board, score = Game2048.execute_move(grid_board, direction)
            return new_board, score, (new_board != grid_board)

    def is_game_over(self):
        if self.get_empty_cells(): return False
        for direction in range(4):
            new_board, _ = self.execute_move(self.board, direction)
            if new_board != self.board: return False
        return True
