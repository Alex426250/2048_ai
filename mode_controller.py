import math
import os

from ai_agents import EndgameTableQueryAI, TableQueryAI, load_td_agent
from agent_selection import select_3x4_move, select_4x4_move
from game2048 import Game2048
from endgame_rules import ENDGAME_SPECS, create_initial_game, install_walls, is_endgame_mode

class ModeController:
    """封装各模式的模型、规则和选步策略。"""

    def __init__(self, root_dir):
        self.root_dir = root_dir
        self.mode = None
        self.agents = {}

    @property
    def rows(self):
        return {"4x4": 4, "3x4": 3, "3x3": 3, "2x4": 2, **{mode: 4 for mode in ENDGAME_SPECS}}[self.mode]

    @property
    def cols(self):
        return {"4x4": 4, "3x4": 4, "3x3": 3, "2x4": 4, **{mode: 4 for mode in ENDGAME_SPECS}}[self.mode]

    @property
    def table_targets(self):
        if is_endgame_mode(self.mode):
            return (ENDGAME_SPECS[self.mode]["target"],)
        return {"3x3": (512, 1024), "2x4": (256, 512)}.get(self.mode)

    def activate(self, mode):
        if mode != self.mode:
            for cached_mode in tuple(self.agents):
                if cached_mode != mode:
                    self._close_agent(self.agents[cached_mode])
                    del self.agents[cached_mode]
        self.mode = mode
        self._agent()
        return self.create_game()

    def close(self):
        for agent in self.agents.values():
            self._close_agent(agent)
        self.agents.clear()

    @staticmethod
    def _close_agent(agent):
        agents = agent.values() if isinstance(agent, dict) else (agent,)
        for item in agents:
            close = getattr(item, "close", None)
            if close is not None:
                close()

    def create_game(self):
        if is_endgame_mode(self.mode):
            return create_initial_game(self.mode, self.query_probs)
        return Game2048(self.rows, self.cols)

    def query_probs(self, grid, target):
        return self._agent().query_probs(grid, target)

    def best_move(self, grid):
        if self.mode == "4x4":
            return self._best_4x4(grid)
        if self.mode == "3x4":
            return self._best_3x4(grid)
        target = self.table_targets[0] if is_endgame_mode(self.mode) else self.table_targets[1]
        probs = self.query_probs(grid, target)
        if not probs:
            return None
        best = max((name for name, value in probs.items() if value is not None), key=lambda name: probs[name], default=None)
        return {"U": 0, "D": 1, "L": 2, "R": 3}.get(best)

    def is_move_allowed(self, grid, direction):
        if not is_endgame_mode(self.mode):
            return True
        probs = self.query_probs(grid, self.table_targets[0])
        return bool(probs and probs.get(("U", "D", "L", "R")[direction]) is not None)

    def is_finished(self, grid, probs=None):
        if not is_endgame_mode(self.mode):
            return False
        if probs is None:
            probs = self.query_probs(grid, self.table_targets[0])
        values = [value for value in (probs or {}).values() if value is not None]
        if not values:
            return False
        best_prob = max(values)
        return best_prob <= 0.0 or best_prob >= 1.0

    def parse_custom_board(self, user_input):
        tokens = user_input.upper().replace(",", " ").split()
        cell_count = self.rows * self.cols
        digits = tokens if len(tokens) == cell_count else list("".join(tokens))
        if len(digits) != cell_count:
            raise ValueError(f"必须输入恰好{cell_count}个16进制数！你输入了{len(digits)}个。")
        if any(not digit or any(char not in "0123456789ABCDEF" for char in digit) for digit in digits):
            raise ValueError("输入包含非法字符，请只使用16进制数字。")
        if is_endgame_mode(self.mode):
            spec = ENDGAME_SPECS[self.mode]
            wall_positions = tuple(index for index, digit in enumerate(digits) if digit in "ABCDEF")
            if len(wall_positions) != 6:
                raise ValueError("残局盘面必须恰好包含6个大数（A-F）。")
            if wall_positions not in spec["cfgs"]:
                raise ValueError(f"6个大数的位置不符合{self.mode}支持的CFG。")
            max_small_exp = int(math.log2(spec["target"])) - 1
            if any(int(digit, 16) > max_small_exp for index, digit in enumerate(digits) if index not in wall_positions):
                raise ValueError(f"{self.mode}模式的非大数格只允许输入0-{max_small_exp:X}。")
            values = [0 if digit == "0" else (1 << int(digit, 16)) for digit in digits]
            install_walls(values, wall_positions)
            return [values[index * self.cols:(index + 1) * self.cols] for index in range(self.rows)]
        if self.table_targets is not None:
            max_target = self.table_targets[-1]
            max_exp = int(math.log2(max_target))
            if any(int(digit, 16) > max_exp for digit in digits):
                raise ValueError(f"{self.mode}模式不允许输入超过{max_target}的数码。")
        values = [0 if digit == "0" else (1 << int(digit, 16)) for digit in digits]
        return [values[index * self.cols:(index + 1) * self.cols] for index in range(self.rows)]

    def _agent(self):
        if self.mode in self.agents:
            return self.agents[self.mode]
        if self.mode == "3x3":
            agent = TableQueryAI(self.root_dir, "2048_3x3", "3x3", 3, 3, (512, 1024), 10)
        elif self.mode == "2x4":
            agent = TableQueryAI(self.root_dir, "2048_2x4", "2x4", 2, 4, (256, 512), 9)
        elif is_endgame_mode(self.mode):
            spec = ENDGAME_SPECS[self.mode]
            agent = EndgameTableQueryAI(self.root_dir, spec["folder"], spec["prefix"], spec["target"])
        elif self.mode == "4x4":
            agent = self._load_4x4_agents()
        else:
            agent = self._load_3x4_agents()
        self.agents[self.mode] = agent
        return agent

    def _load_4x4_agents(self):
        folder = os.path.join(self.root_dir, "2048_4x4")
        specs = (("base", "base_agent", "base_model.bin"), ("8k4k", "8k4k_agent", "8k4k_model.bin"), ("16k8k4k", "16k8k4k_agent", "16k8k4k_model.bin"))
        return {name: self._load_td(folder, name, subfolder, model) for name, subfolder, model in specs}

    def _load_3x4_agents(self):
        folder = os.path.join(self.root_dir, "2048_3x4")
        specs = (("base", "base_agent", "base_model.bin"), ("2k1k", "2k1k_agent", "2k1k_model.bin"))
        return {name: self._load_td(folder, name, subfolder, model) for name, subfolder, model in specs}

    def _load_td(self, folder, name, subfolder, model):
        agent_class = load_td_agent(f"{self.mode}_{name}_agent", os.path.join(folder, subfolder, f"{subfolder}.py"))
        agent = agent_class()
        model_path = os.path.join(folder, subfolder, model)
        if not agent.load_model(model_path):
            raise RuntimeError(f"加载模型失败: {model_path}")
        return agent

    def _best_4x4(self, grid):
        return select_4x4_move(grid, self._agent())

    def _best_3x4(self, grid):
        return select_3x4_move(grid, self._agent())