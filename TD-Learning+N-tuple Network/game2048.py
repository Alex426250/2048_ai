import random
import copy

class Game2048:
    def __init__(self):
        self.size = 4
        self.grid = [[0] * self.size for _ in range(self.size)]
        self.score = 0
        self.add_new_tile()
        self.add_new_tile()

    def add_new_tile(self):
        empty_cells = self.get_empty_cells(self.grid)
        if empty_cells:
            r, c = random.choice(empty_cells)
            self.grid[r][c] = 2 if random.random() < 0.9 else 4

    def get_empty_cells(self, grid):
        return [(r, c) for r in range(self.size) for c in range(self.size) if grid[r][c] == 0]

    def move(self, direction):
        # 0: Up, 1: Down, 2: Left, 3: Right
        new_grid, score_increment, moved = self.simulate_move(self.grid, direction)
        if moved:
            self.grid = new_grid
            self.score += score_increment
            self.add_new_tile()
        return moved

    @staticmethod
    def simulate_move(grid, direction):
        # Returns a tuple: (new_grid, score_increment, moved_boolean)
        size = len(grid)
        new_grid = [[0] * size for _ in range(size)]
        score_increment = 0
        moved = False

        if direction == 0:  # Up
            for c in range(size):
                col = [grid[r][c] for r in range(size) if grid[r][c] != 0]
                merged_col, inc = Game2048.merge_line(col)
                score_increment += inc
                for r in range(size):
                    val = merged_col[r] if r < len(merged_col) else 0
                    new_grid[r][c] = val
                    if grid[r][c] != val: moved = True

        elif direction == 1:  # Down
            for c in range(size):
                col = [grid[r][c] for r in range(size) if grid[r][c] != 0]
                merged_col, inc = Game2048.merge_line(col[::-1])
                merged_col = merged_col[::-1]
                score_increment += inc
                for r in range(size):
                    val = merged_col[r - (size - len(merged_col))] if r >= size - len(merged_col) else 0
                    new_grid[r][c] = val
                    if grid[r][c] != val: moved = True

        elif direction == 2:  # Left
            for r in range(size):
                row = [grid[r][c] for c in range(size) if grid[r][c] != 0]
                merged_row, inc = Game2048.merge_line(row)
                score_increment += inc
                for c in range(size):
                    val = merged_row[c] if c < len(merged_row) else 0
                    new_grid[r][c] = val
                    if grid[r][c] != val: moved = True

        elif direction == 3:  # Right
            for r in range(size):
                row = [grid[r][c] for c in range(size) if grid[r][c] != 0]
                merged_row, inc = Game2048.merge_line(row[::-1])
                merged_row = merged_row[::-1]
                score_increment += inc
                for c in range(size):
                    val = merged_row[c - (size - len(merged_row))] if c >= size - len(merged_row) else 0
                    new_grid[r][c] = val
                    if grid[r][c] != val: moved = True

        return new_grid, score_increment, moved

    @staticmethod
    def merge_line(line):
        merged = []
        score_increment = 0
        skip = False
        for i in range(len(line)):
            if skip:
                skip = False
                continue
            if i + 1 < len(line) and line[i] == line[i + 1]:
                merged.append(line[i] * 2)
                score_increment += line[i] * 2
                skip = True
            else:
                merged.append(line[i])
        return merged, score_increment

    def is_game_over(self):
        if self.get_empty_cells(self.grid):
            return False
        for direction in range(4):
            _, _, moved = self.simulate_move(self.grid, direction)
            if moved:
                return False
        return True
