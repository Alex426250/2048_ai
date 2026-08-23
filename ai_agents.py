import ctypes
import importlib.util
import math
import os

from endgame_rules import WALL_TILE_MIN

class TableQueryAI:
    def __init__(self, root_dir, folder, prefix, rows, cols, targets, max_exp):
        self.rows = rows
        self.cols = cols
        self.targets = targets
        self.max_exp = max_exp
        self.last_error = None
        folder_path = os.path.join(root_dir, folder)
        dll_path = os.path.join(folder_path, f"query_{prefix}.dll")
        if not os.path.exists(dll_path):
            raise FileNotFoundError(f"找不到 DLL: {dll_path}")

        self.lib = ctypes.CDLL(dll_path)
        self.lib.init_model.argtypes = [ctypes.c_char_p, ctypes.c_int]
        self.lib.init_model.restype = ctypes.c_int
        self.lib.query_probs.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_double)]
        self.lib.query_probs.restype = ctypes.c_int
        for target in targets:
            model_path = os.path.join(folder_path, f"table_{prefix}_{target}.bin")
            if self.lib.init_model(model_path.encode(), target) != 0:
                raise RuntimeError(f"初始化 {target} 模型失败，请检查模型文件是否存在且格式正确。")

    def query_probs(self, grid, target):
        try:
            self.last_error = None
            digits = []
            for row in range(self.rows):
                for col in range(self.cols):
                    value = grid[row][col]
                    if value == 0:
                        digits.append("0")
                        continue
                    exponent = int(round(math.log2(value)))
                    if (1 << exponent) != value or exponent > self.max_exp:
                        raise ValueError(f"invalid tile value: {value}")
                    digits.append(format(exponent, "X"))
            out_probs = (ctypes.c_double * 4)()
            status = self.lib.query_probs("".join(digits).encode(), target, out_probs)
            if status != 0:
                self.last_error = f"query_{self.rows}x{self.cols} failed with status {status}"
                return None
            return {name: None if out_probs[index] < 0 else out_probs[index]
                    for index, name in enumerate(("U", "D", "L", "R"))}
        except (IndexError, OverflowError, TypeError, ValueError) as exc:
            self.last_error = str(exc)
            return None

class EndgameTableQueryAI(TableQueryAI):
    def __init__(self, root_dir, folder, prefix, target):
        super().__init__(root_dir, folder, prefix, 4, 4, (target,), 10)
        self.lib.free_models.argtypes = []
        self.lib.free_models.restype = None

    def close(self):
        self.lib.free_models()

    def query_probs(self, grid, target):
        try:
            self.last_error = None
            digits = []
            for value in (tile for row in grid for tile in row):
                if value >= WALL_TILE_MIN:
                    digits.append("F")
                elif value == 0:
                    digits.append("0")
                else:
                    exponent = int(round(math.log2(value)))
                    if (1 << exponent) != value or exponent > self.max_exp:
                        raise ValueError(f"invalid endgame tile value: {value}")
                    digits.append(format(exponent, "X"))
            out_probs = (ctypes.c_double * 4)()
            status = self.lib.query_probs("".join(digits).encode(), target, out_probs)
            if status != 0:
                self.last_error = f"endgame query failed with status {status}"
                return None
            return {name: None if out_probs[index] < 0 else out_probs[index]
                    for index, name in enumerate(("U", "D", "L", "R"))}
        except (IndexError, OverflowError, TypeError, ValueError) as exc:
            self.last_error = str(exc)
            return None

def load_td_agent(module_name, module_path):
    spec = importlib.util.spec_from_file_location(module_name, module_path)
    if spec is None or spec.loader is None:
        raise ImportError(f"无法加载 AI 模块: {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.TDAgent