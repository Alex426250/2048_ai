import ctypes
import os
import sys
import io

if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')
if os.name == 'nt':
    ctypes.windll.kernel32.SetConsoleOutputCP(65001)

def append_training_data(data_path, episodes, avg_score, merged_8192):
    with open(data_path, "a", encoding="utf-8") as f:
        f.write(f"{episodes} {avg_score:.2f} {int(merged_8192)} \n")

class TDAgent:
    def __init__(self, alpha=0.0025, dll_path="base_agent.dll"):
        # 获取 dll 的绝对路径
        script_dir = os.path.dirname(os.path.abspath(__file__))
        full_dll_path = os.path.join(script_dir, dll_path)
        
        if not os.path.exists(full_dll_path):
            raise FileNotFoundError(f"找不到编译好的 C++ 动态库: {full_dll_path}。\n请先运行: g++ -O3 -shared -fPIC base_agent.cpp -o base_agent.dll")
        
        # 加载动态链接库
        self.lib = ctypes.CDLL(full_dll_path)
        
        # 定义 C API 参数和返回值类型
        self.lib.init_agent.argtypes = [ctypes.c_double]
        self.lib.init_agent.restype = ctypes.c_void_p
        
        self.lib.free_agent.argtypes = [ctypes.c_void_p]
        
        self.lib.train.argtypes = [
            ctypes.c_void_p,
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_int),
        ]
        self.lib.train.restype = None
        
        self.lib.get_best_move.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
        self.lib.get_best_move.restype = ctypes.c_int
        
        self.lib.save_model.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        self.lib.save_model.restype = ctypes.c_bool
        
        self.lib.load_model.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        self.lib.load_model.restype = ctypes.c_bool
        
        # 初始化 C++ Agent 实例
        self.agent_ptr = self.lib.init_agent(alpha)

    def __del__(self):
        if hasattr(self, 'agent_ptr') and self.agent_ptr:
            self.lib.free_agent(self.agent_ptr)

    def train(self, episodes):
        """让 C++ 引擎在底层全速自我对弈训练"""
        avg_score = ctypes.c_double(0.0)
        merged_8192 = ctypes.c_int(0)
        self.lib.train(self.agent_ptr, episodes, ctypes.byref(avg_score), ctypes.byref(merged_8192))
        return avg_score.value, merged_8192.value

    def get_best_move(self, grid_2d):
        """被 Python 的 GUI 或其它脚本调用：传入 4x4 list，返回最优方向 0,1,2,3"""
        flat_grid = []
        for r in range(4):
            for c in range(4):
                flat_grid.append(grid_2d[r][c])
                
        # 转换为 C 数组
        c_array = (ctypes.c_int * 16)(*flat_grid)
        move = self.lib.get_best_move(self.agent_ptr, c_array)
        return move if move != -1 else None

    def save_model(self, path="base_model.bin"):
        encoded_path = path.encode('utf-8')
        return self.lib.save_model(self.agent_ptr, encoded_path)

    def load_model(self, path="base_model.bin"):
        encoded_path = path.encode('utf-8')
        return self.lib.load_model(self.agent_ptr, encoded_path)

if __name__ == "__main__":
    # 极速训练范例演示模块
    try:
        agent = TDAgent()
    except FileNotFoundError as e:
        print(e)
        sys.exit(1)
        
    model_path = "base_model.bin"
    if agent.load_model(model_path):
        print(f"已加载现有模型: {model_path}，继续进化...")
    else:
        print("未找到已有模型，从头开始炼丹...")
        
    data_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data_base.txt")
    print("开始极速训练（预定总量 10,000,000 episodes，按 Ctrl+C 可中止保存）...")
    try:
        total_episodes = 0
        target_episodes = 50_000_000
        while total_episodes < target_episodes:
            # 保持 1万局 汇报一次分数（纯内存运行，极致顺滑不用等硬盘）
            batch = min(10000, target_episodes - total_episodes)
            avg_score, merged_8192= agent.train(batch)
            total_episodes += batch
            append_training_data(data_path, total_episodes, avg_score, merged_8192)
            print(
                f"目前已对弈 {total_episodes} 局, 本批次得分统计 -> "
                f"平均: {avg_score:.2f} | 合成8192次数: {int(merged_8192)} "
            )
            
            # 【硬盘阻塞致命修复】：积攒满 10万局后再触发一次存盘！
            if total_episodes % 100000 == 0:
                agent.save_model(model_path)
                print(f" >>> [自动备份] 模型权重的 256MB 数据已安全快照保存至硬盘。")

        agent.save_model(model_path)
        print("训练达到 10,000,000 episodes，模型已保存。")
    except KeyboardInterrupt:
        print("\n停止训练。正在保存模型...")
        agent.save_model(model_path)
        print("模型已保存！请现在运行 main_gui.py 使用此脑容量查看效果。")
