from collections import deque


class GameSession:
    """保存当前对局及其可重放的撤销历史。"""

    MAX_HISTORY = 500

    def __init__(self):
        self.game = None
        self.history = deque(maxlen=self.MAX_HISTORY)
        self.future_history = deque(maxlen=self.MAX_HISTORY)
        self.steps = 0

    def reset(self, game):
        self.game = game
        self.history.clear()
        self.future_history.clear()
        self.steps = 0

    def _snapshot(self):
        return [row.copy() for row in self.game.board]

    def save_state(self):
        self.history.append((self._snapshot(), self.game.score, self.steps))

    def move_new(self, direction, clear_future=False):
        self.save_state()
        if not self.game.move(direction):
            self.history.pop()
            return False
        if clear_future:
            self.future_history.clear()
        self.steps += 1
        return True

    def undo(self, count=1):
        undone = 0
        while self.history and undone < count:
            self.future_history.append((self._snapshot(), self.game.score, self.steps))
            grid, score, steps = self.history.pop()
            self.game.grid = grid
            self.game.score = score
            self.steps = steps
            undone += 1
        return undone

    def redo(self):
        if not self.future_history:
            return False
        self.save_state()
        grid, score, steps = self.future_history.pop()
        self.game.grid = grid
        self.game.score = score
        self.steps = steps
        return True