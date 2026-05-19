import time
import os
import sys

# 解决在某些环境下 Windows 输出 utf-8 报错的问题
import io
if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')

from game2048 import Game2048
# 默认采用和 base_agent.py 相同的 agent
from base_agent.base_agent import TDAgent as TDAgentBase
TDAgent8k4k = __import__('8k4k_agent.8k4k_agent', fromlist=['TDAgent']).TDAgent
TDAgent16k8k4k = __import__('16k8k4k_agent.16k8k4k_agent', fromlist=['TDAgent']).TDAgent

def get_max_tile(grid):
    return max(max(row) for row in grid)

def main():
    print("初始化游戏和 Agent...")
    game = Game2048()
    
    try:
        ai_base = TDAgentBase()
        ai_8k4k = TDAgent8k4k()
        ai_16k8k4k = TDAgent16k8k4k()
    except FileNotFoundError as e:
        print(e)
        return
        
    # 尝试加载模型
    script_dir = os.path.dirname(os.path.abspath(__file__))
    
    if ai_base.load_model(os.path.join(script_dir, "base_agent", "base_model.bin")):
        print(f"成功加载模型: base_model.bin")
    else:
        print(f"未找到基础模型，使用随机权重。")
        
    if ai_8k4k.load_model(os.path.join(script_dir, "8k4k_agent", "8k4k_model.bin")):
        print(f"成功加载模型: 8k4k_model.bin")
    else:
        print(f"未找到 8k4k 模型，使用随机权重。")
        
    if ai_16k8k4k.load_model(os.path.join(script_dir, "16k8k4k_agent", "16k8k4k_model.bin")):
        print(f"成功加载模型: 16k8k4k_model.bin")
    else:
        print(f"未找到 16k8k4k 模型，使用随机权重。")

    def get_best_move(grid):
        flat_grid = [tile for row in grid for tile in row]
        large_vals = set(x for x in flat_grid if x >= 4096)
        A = len(large_vals)
        
        if A <= 1:
            return ai_base.get_best_move(grid)
        mapped_flat = list(flat_grid)
        min_large = min(large_vals)
        ratio = min_large // 4096
        if ratio > 1:
            for i in range(16):
                if mapped_flat[i] >= 4096:
                    mapped_flat[i] = mapped_flat[i] // ratio
        mapped_grid = [mapped_flat[i*4:(i+1)*4] for i in range(4)]
        if A == 2:
            return ai_8k4k.get_best_move(mapped_grid)
        return ai_16k8k4k.get_best_move(mapped_grid)

    steps = 0
    start_time = time.perf_counter()
    
    print("开始游戏评测...")
    while not game.is_game_over():
        # 获取最佳移动方向
        move = get_best_move(game.grid)
        if move is None:
            print("模型未返回有效移动，游戏终止。")
            break
            
        # 移动
        moved = game.move(move)
        
        # 即使无法移动（极少出现因为get_best_move排除了非法），也避免死循环
        if not moved:
            # 尝试随机取一个合法方向
            fallback_moved = False
            for d in range(4):
                if game.move(d):
                    fallback_moved = True
                    break
            if not fallback_moved:
                break
                
        steps += 1
        
        # 每1000步输出一次信息
        if steps % 1000 == 0:
            current_time = time.perf_counter() - start_time
            current_max = get_max_tile(game.grid)
            print(f"已走步数: {steps}, 耗时: {current_time:.3f}秒, 当前最大方块: {current_max}")

    end_time = time.perf_counter()
    total_time = end_time - start_time
    max_tile = get_max_tile(game.grid)
    steps_per_sec = steps / total_time if total_time > 0 else 0
    
    print("-" * 30)
    print("测试完成！统计结果：")
    print(f"最大方块 (Max Tile) : {max_tile}")
    print(f"最终分数 (Score)    : {game.score}")
    print(f"总步数 (Steps)      : {steps}")
    print(f"总耗时 (Time)       : {total_time:.3f} 秒")
    print(f"评估速度 (Speed)    : {steps_per_sec:.2f} 步/秒")
    print("-" * 30)

if __name__ == "__main__":
    main()
