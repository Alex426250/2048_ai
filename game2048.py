import random
import copy

class Game2048:
    def __init__(self, rows=4, cols=4):
        """
        通用矩形棋盘 2048
        :param rows: 行数（默认 4）
        :param cols: 列数（默认 4）
        """
        self.rows = rows
        self.cols = cols
        self.board = [[0] * cols for _ in range(rows)]
        self.score = 0
        self.state_version = 0
        self.add_new_tile()
        self.add_new_tile()

    @property
    def grid(self):
        """返回当前棋盘的深拷贝（保护内部状态）"""
        return copy.deepcopy(self.board)

    @grid.setter
    def grid(self, new_grid):
        """设置棋盘（用于撤销/重做、自定义棋盘）"""
        self.board = copy.deepcopy(new_grid)
        self.rows = len(new_grid)
        self.cols = len(new_grid[0]) if self.rows > 0 else 0
        self.state_version += 1

    @property
    def board_view(self):
        return self.board

    @staticmethod
    def _merge_line(values):
        """
        合并一维列表（核心原子操作）
        返回 (合并后的列表, 本次合并得分)
        """
        tiles = [v for v in values if v != 0]
        merged = []
        score = 0
        i = 0
        while i < len(tiles):
            if i + 1 < len(tiles) and tiles[i] == tiles[i + 1]:
                new_val = tiles[i] * 2
                merged.append(new_val)
                score += new_val
                i += 2
            else:
                merged.append(tiles[i])
                i += 1
        merged.extend([0] * (len(values) - len(merged)))
        return merged, score

    def get_empty_cells(self, grid=None):
        """返回所有空格子的坐标列表 [(r, c), ...]"""
        board = grid if grid is not None else self.board
        return [(r, c) for r in range(self.rows) for c in range(self.cols) if board[r][c] == 0]

    def add_new_tile(self):
        """在随机空格生成新块（90% 概率为 2，10% 为 4）"""
        empty = self.get_empty_cells()
        if not empty:
            return
        r, c = random.choice(empty)
        self.board[r][c] = 2 if random.random() < 0.9 else 4
        self.state_version += 1

    @staticmethod
    def execute_move(grid_board, direction):
        """
        静态方法：对任意矩形棋盘执行移动
        direction: 0=上, 1=下, 2=左, 3=右
        返回 (new_board, score)
        """
        if direction not in (0, 1, 2, 3):
            raise ValueError(f"invalid direction: {direction}")
        rows = len(grid_board)
        cols = len(grid_board[0]) if rows > 0 else 0
        new_board = [[0] * cols for _ in range(rows)]
        total_score = 0

        if direction == 2:     
            for r in range(rows):
                merged, sc = Game2048._merge_line(grid_board[r])
                new_board[r] = merged
                total_score += sc
        elif direction == 3:     
            for r in range(rows):
                reversed_row = list(reversed(grid_board[r]))
                merged, sc = Game2048._merge_line(reversed_row)
                new_board[r] = list(reversed(merged))
                total_score += sc
        elif direction == 0:     
            for c in range(cols):
                col = [grid_board[r][c] for r in range(rows)]
                merged, sc = Game2048._merge_line(col)
                for r in range(rows):
                    new_board[r][c] = merged[r]
                total_score += sc
        elif direction == 1:     
            for c in range(cols):
                col = [grid_board[r][c] for r in range(rows)]
                reversed_col = list(reversed(col))
                merged, sc = Game2048._merge_line(reversed_col)
                merged = list(reversed(merged))             
                for r in range(rows):
                    new_board[r][c] = merged[r]
                total_score += sc

        return new_board, total_score

    @staticmethod
    def simulate_move(grid_board, direction):
        """模拟移动，返回 (new_board, score, moved)"""
        new_board, score = Game2048.execute_move(grid_board, direction)
        return new_board, score, (new_board != grid_board)

    def move(self, direction):
        """执行真实移动，更新内部状态，返回是否移动成功"""
        new_board, inc, moved = self.simulate_move(self.board, direction)
        if moved:
            self.board = new_board
            self.score += inc
            self.add_new_tile()
        return moved

    def is_game_over(self):
        """检查游戏是否结束（无空格且无相邻可合并）"""
        if self.get_empty_cells():
            return False
        for direction in range(4):
            _, _, moved = self.simulate_move(self.board, direction)
            if moved:
                return False
        return True
