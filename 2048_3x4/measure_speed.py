import time
import os
import sys

import io
if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')

_ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _ROOT_DIR not in sys.path:
    sys.path.insert(0, _ROOT_DIR)

from game2048 import Game2048
from agent_selection import select_3x4_move
TDAgentBase = __import__('base_agent.base_agent', fromlist=['TDAgent']).TDAgent
TDAgent2k1k = __import__('2k1k_agent.2k1k_agent', fromlist=['TDAgent']).TDAgent

def get_max_tile(grid):
    return max(max(row) for row in grid)

def main():
    print("初始化游戏和 Agent...")
    game = Game2048(3, 4)
    
    try:
        ai_base = TDAgentBase()
        ai_2k1k = TDAgent2k1k()
    except FileNotFoundError as e:
        print(e)
        return
        
    script_dir = os.path.dirname(os.path.abspath(__file__))
    
    if ai_base.load_model(os.path.join(script_dir, "base_agent", "base_model.bin")):
        print(f"成功加载模型: base_model.bin")
    else:
        print(f"未找到基础模型，使用随机权重。")
        
    if ai_2k1k.load_model(os.path.join(script_dir, "2k1k_agent", "2k1k_model.bin")):
        print(f"成功加载模型: 2k1k_model.bin")
    else:
        print(f"未找到 2k1k 模型，使用随机权重。")

    agents = {"base": ai_base, "2k1k": ai_2k1k}

    steps = 0
    start_time = time.perf_counter()
    
    print("开始游戏评测...")
    while not game.is_game_over():
                  
        move = select_3x4_move(game.board_view, agents)
        if move is None:
            print("模型未返回有效移动，游戏终止。")
            break
            
        moved = game.move(move)
        
        if not moved:
                         
            fallback_moved = False
            for d in range(4):
                if game.move(d):
                    fallback_moved = True
                    break
            if not fallback_moved:
                break
                
        steps += 1
        
        if steps % 1000 == 0:
            current_time = time.perf_counter() - start_time
            current_max = get_max_tile(game.board_view)
            print(f"已走步数: {steps}, 耗时: {current_time:.3f}秒, 当前最大方块: {current_max}")

    end_time = time.perf_counter()
    total_time = end_time - start_time
    max_tile = get_max_tile(game.board_view)
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
