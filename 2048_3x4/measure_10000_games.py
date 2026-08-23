import argparse
import os
import random
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

_ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _ROOT_DIR not in sys.path:
    sys.path.insert(0, _ROOT_DIR)
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

from game2048 import Game2048
from agent_selection import select_3x4_move
from multiprocessing import Pool
import time

def worker_run(args):
    worker_id, num_games, seed = args
    if seed is not None:
        random.seed(seed + worker_id)
    TDAgentBase = __import__('base_agent.base_agent', fromlist=['TDAgent']).TDAgent
    TDAgent2k1k = __import__('2k1k_agent.2k1k_agent', fromlist=['TDAgent']).TDAgent

    ai_base = TDAgentBase()
    base_model = os.path.join(_SCRIPT_DIR, "base_agent", "base_model.bin")
    if not ai_base.load_model(base_model):
        raise RuntimeError(f"加载模型失败: {base_model}")
    ai_2k1k = TDAgent2k1k()
    stage_model = os.path.join(_SCRIPT_DIR, "2k1k_agent", "2k1k_model.bin")
    if not ai_2k1k.load_model(stage_model):
        raise RuntimeError(f"加载模型失败: {stage_model}")

    agents = {"base": ai_base, "2k1k": ai_2k1k}

    results = []
    for i in range(num_games):
        game = Game2048(3, 4)
        steps = 0
        while not game.is_game_over():
            move = select_3x4_move(game.board_view, agents)
            if move is None:
                break
            if game.move(move):
                steps += 1
            else:
                break
                
        flat = [val for row in game.board_view for val in row]
        max_val = max(flat)
        results.append((game.score, max_val, steps))
        
        if (i+1) % 10 == 0 or max_val >= 4096:
            print(f"[Worker {worker_id}] Game {i+1}/{num_games} finished | max tile: {max_val} | score: {game.score} | steps: {steps}")
            sys.stdout.flush()

    return results

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="3x4 连续对局评测")
    parser.add_argument("--games", type=int, default=10000)
    parser.add_argument("--workers", type=int, default=min(5, os.cpu_count() or 1))
    parser.add_argument("--seed", type=int)
    parser.add_argument("--output", default="data_10000_games.txt")
    parser.add_argument("--append", action="store_true")
    options = parser.parse_args()
    if options.games < 1 or options.workers < 1:
        parser.error("--games 和 --workers 必须为正整数")

    TOTAL_GAMES = options.games
    NUM_CORES = min(options.workers, TOTAL_GAMES)
    games_per_worker, extra_games = divmod(TOTAL_GAMES, NUM_CORES)

    print(f"=== 开始 2048 流水线并行测试 ===")
    print(f"模型数量: 2 (base, 2k1k)")
    print(f"总局数: {TOTAL_GAMES}, 核心数: {NUM_CORES}, 每个核心负荷: {games_per_worker} 局")

    args = [(i, games_per_worker + (i < extra_games), options.seed) for i in range(NUM_CORES)]

    start_time = time.time()
    
    with Pool(processes=NUM_CORES) as pool:
        all_results = pool.map(worker_run, args)

    end_time = time.time()
    
    flat_results = [game for worker_res in all_results for game in worker_res]
    with open(options.output, "a" if options.append else "w", encoding="utf-8") as f:
        for index, (score, max_tile, _) in enumerate(flat_results, 1):
            f.write(f"{index} {score} {max_tile}\n")
    
    scores = [r[0] for r in flat_results]
    max_tiles = [r[1] for r in flat_results]

    min_score = min(scores)
    max_score = max(scores)
    avg_score = sum(scores) / len(scores)
    
    count_2048 = sum(1 for t in max_tiles if t >= 2048)
    count_4096 = sum(1 for t in max_tiles if t >= 4096)
    count_8192 = sum(1 for t in max_tiles if t >= 8192)

    print("\n" + "="*40)
    print("=== 测试统计结果 ===")
    print("="*40)
    print(f"总耗时: {end_time - start_time:.2f} 秒")
    print(f"最低分: {min_score}")
    print(f"最高分: {max_score}")
    print(f"平均分: {avg_score:.2f}")
    print("-" * 40)
    
    print(f"合出2048概率: {(count_2048 / TOTAL_GAMES) * 100:.2f}% ({count_2048}/{TOTAL_GAMES})")
    print(f"合出4096概率: {(count_4096 / TOTAL_GAMES) * 100:.2f}% ({count_4096}/{TOTAL_GAMES})")
    print(f"合出8192概率: {(count_8192 / TOTAL_GAMES) * 100:.2f}% ({count_8192}/{TOTAL_GAMES})")

    print("="*40)
