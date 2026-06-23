import os
import sys
import io
if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')
import os
import ctypes
if os.name == 'nt':
    ctypes.windll.kernel32.SetConsoleOutputCP(65001)
from multiprocessing import Pool
import time

def worker_run(args):
    worker_id, num_games = args
    import game2048
    from base_agent.base_agent import TDAgent as TDAgentBase
    TDAgent8k4k = __import__('8k4k_agent.8k4k_agent', fromlist=['TDAgent']).TDAgent
    TDAgent16k8k4k = __import__('16k8k4k_agent.16k8k4k_agent', fromlist=['TDAgent']).TDAgent

    # 1. 实例化 AI
    ai_base = TDAgentBase()
    ai_base.load_model("base_agent/base_model.bin")

    ai_8k4k = TDAgent8k4k()
    ai_8k4k.load_model("8k4k_agent/8k4k_model.bin")

    ai_16k8k4k = TDAgent16k8k4k()
    ai_16k8k4k.load_model("16k8k4k_agent/16k8k4k_model.bin")

    # 完全复刻 main_gui.py 的模型分发逻辑
    def get_best_move(grid):
        flat_grid = [tile for row in grid for tile in row] if isinstance(grid[0], list) else grid
        
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
            

    results = []
    for i in range(num_games):
        game = game2048.Game2048()
        steps = 0
        while not game.is_game_over():
            move = get_best_move(game.grid)
            if move is None:
                break
            if game.move(move):
                steps += 1
            else:
                break
                
        flat = [val for row in game.grid for val in row]
        max_val = max(flat)
        results.append((game.score, max_val, steps))
        
        # 打完一局立刻写入（追加模式，不覆盖）
        global_index = worker_id * num_games + i + 1
        with open("data_10000_games.txt", "a", encoding="utf-8") as f:
            f.write(f"{global_index} {game.score} {max_val}\n")
            
        # 避免刷屏，每10局或者出了超大数字再单独打印
        if (i+1) % 10 == 0 or max_val >= 32768:
            print(f"[Worker {worker_id}] Game {i+1}/{num_games} finished | max tile: {max_val} | score: {game.score} | steps: {steps}")
            sys.stdout.flush()

    return results

if __name__ == "__main__":
    TOTAL_GAMES = 10000
    NUM_CORES = 5
    games_per_worker = TOTAL_GAMES // NUM_CORES

    print(f"=== 开始 2048 流水线并行测试 ===")
    print(f"模型数量: 3 (base, 8k4k, 16k8k4k)")
    print(f"总局数: {TOTAL_GAMES}, 核心数: {NUM_CORES}, 每个核心负荷: {games_per_worker} 局")

    args = [(i, games_per_worker) for i in range(NUM_CORES)]

    start_time = time.time()
    
    with Pool(processes=NUM_CORES) as pool:
        all_results = pool.map(worker_run, args)

    end_time = time.time()
    
    # 汇总数据
    flat_results = [game for worker_res in all_results for game in worker_res]
    
    scores = [r[0] for r in flat_results]
    max_tiles = [r[1] for r in flat_results]

    min_score = min(scores)
    max_score = max(scores)
    avg_score = sum(scores) / len(scores)
    
    count_8192 = sum(1 for t in max_tiles if t >= 8192)
    count_16384 = sum(1 for t in max_tiles if t >= 16384)
    count_32768 = sum(1 for t in max_tiles if t >= 32768)

    print("\n" + "="*40)
    print("=== 测试统计结果 ===")
    print("="*40)
    print(f"总耗时: {end_time - start_time:.2f} 秒")
    print(f"最低分: {min_score}")
    print(f"最高分: {max_score}")
    print(f"平均分: {avg_score:.2f}")
    print("-" * 40)
    
    print(f"合出8192概率: {(count_8192 / TOTAL_GAMES) * 100:.2f}% ({count_8192}/{TOTAL_GAMES})")
    print(f"合出16384概率: {(count_16384 / TOTAL_GAMES) * 100:.2f}% ({count_16384}/{TOTAL_GAMES})")
    print(f"合出32768概率: {(count_32768 / TOTAL_GAMES) * 100:.2f}% ({count_32768}/{TOTAL_GAMES})")

    print("="*40)
