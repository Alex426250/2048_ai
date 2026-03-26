import ctypes
import os

class ExpectimaxAI:
    def __init__(self, max_depth=10):
        """
        这里的 max_depth 实际上在 C++ 内部已经动态写死了，
        你可以根据需要去 expectimax_lib.cpp 里修改 get_best_move_c 函数底部的 depth 控制。
        """
        # 加载编译好的动态链接库 (.dll 适用于 Windows, .so 适用于 Linux)
        lib_path = os.path.join(os.path.dirname(__file__), 'expectimax_lib.dll')
        
        if not os.path.exists(lib_path):
            raise FileNotFoundError(f"找不到编译好的 C++ 库：{lib_path}。\n请先参考教程编译 expectimax_lib.cpp")
            
        self.cpp_lib = ctypes.CDLL(lib_path)
        
        # 声明 c++ 函数参数和返回类型
        # int get_best_move_c(int* flat_grid);
        self.cpp_lib.get_best_move_c.argtypes = [ctypes.POINTER(ctypes.c_int)]
        self.cpp_lib.get_best_move_c.restype = ctypes.c_int

    def get_best_move(self, grid):
        # 将 Python 的 2D list 转换为 C++ 中可用的 1D 整型数组 (int array[16])
        flat_grid = [val for row in grid for val in row]
        
        IntArray16 = ctypes.c_int * 16
        c_grid = IntArray16(*flat_grid)
        
        # 调用 C++ 核心计算
        best_move = self.cpp_lib.get_best_move_c(c_grid)
        
        # 如果返回 -1 则代表游戏无路可走
        return best_move if best_move != -1 else None