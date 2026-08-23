import random

from game2048 import Game2048


WALL_TILES = (65536, 131072, 262144, 524288, 1048576, 2097152)
WALL_TILE_MIN = min(WALL_TILES)

ENDGAME_CONFIGS = {
    "L3t": (
        (9, 10, 11, 13, 14, 15), (5, 9, 10, 11, 13, 14),
        (6, 9, 10, 11, 13, 14), (7, 9, 10, 11, 13, 14),
        (6, 7, 9, 10, 11, 13), (6, 7, 9, 10, 11, 14),
        (5, 6, 9, 10, 11, 13), (5, 6, 9, 10, 11, 14),
        (1, 5, 6, 10, 11, 14), (2, 5, 6, 10, 11, 14),
        (6, 7, 9, 10, 13, 14), (5, 6, 9, 10, 13, 15),
    ),
    "442t": (
        (10, 11, 12, 13, 14, 15), (4, 10, 11, 13, 14, 15),
        (8, 10, 11, 13, 14, 15), (7, 10, 11, 12, 13, 14),
        (7, 8, 10, 11, 13, 14), (6, 8, 10, 11, 13, 14),
        (5, 10, 11, 12, 13, 14), (6, 10, 11, 12, 13, 14),
        (6, 10, 11, 12, 13, 15), (6, 7, 10, 11, 12, 13),
        (5, 6, 10, 11, 12, 13), (5, 10, 11, 12, 13, 15),
    ),
    "T": (
        (8, 9, 11, 12, 13, 15), (3, 7, 8, 9, 12, 13),
        (7, 8, 9, 11, 12, 13), (4, 8, 9, 11, 13, 15),
        (5, 8, 9, 11, 13, 15), (6, 8, 9, 11, 12, 13),
        (5, 8, 9, 11, 12, 13),
        (4, 7, 8, 9, 11, 13), (5, 7, 8, 9, 11, 13),
        (5, 8, 9, 11, 12, 15), (4, 5, 8, 9, 11, 15),
        (0, 4, 9, 11, 13, 15),
    ),
}

ENDGAME_SPECS = {
    "L3t_512": {"folder": "endgame_L3t", "prefix": "L3t", "target": 512, "walls": ENDGAME_CONFIGS["L3t"][0], "cfgs": ENDGAME_CONFIGS["L3t"], "min_prob": 0.80},
    "L3t_1024": {"folder": "endgame_L3t", "prefix": "L3t", "target": 1024, "walls": ENDGAME_CONFIGS["L3t"][0], "cfgs": ENDGAME_CONFIGS["L3t"], "min_prob": 0.10},
    "442t_512": {"folder": "endgame_442t", "prefix": "442t", "target": 512, "walls": ENDGAME_CONFIGS["442t"][0], "cfgs": ENDGAME_CONFIGS["442t"], "min_prob": 0.80},
    "442t_1024": {"folder": "endgame_442t", "prefix": "442t", "target": 1024, "walls": ENDGAME_CONFIGS["442t"][0], "cfgs": ENDGAME_CONFIGS["442t"], "min_prob": 0.07},
    "T_512": {"folder": "endgame_T", "prefix": "T", "target": 512, "walls": ENDGAME_CONFIGS["T"][0], "cfgs": ENDGAME_CONFIGS["T"], "min_prob": 0.80},
    "T_1024": {"folder": "endgame_T", "prefix": "T", "target": 1024, "walls": ENDGAME_CONFIGS["T"][0], "cfgs": ENDGAME_CONFIGS["T"], "min_prob": 0.18},
}


def is_endgame_mode(mode):
    return mode in ENDGAME_SPECS


def wall_cells(grid):
    return frozenset(
        index
        for index, value in enumerate(tile for row in grid for tile in row)
        if value >= WALL_TILE_MIN
    )


def install_walls(values, cells):
    for index, value in zip(cells, WALL_TILES):
        values[index] = value
    return values


def create_initial_game(mode, query_probs):
    spec = ENDGAME_SPECS[mode]
    wall_positions = frozenset(spec["walls"])
    free_cells = [(row, col) for row in range(4) for col in range(4) if row * 4 + col not in wall_positions]
    while True:
        game = Game2048(4, 4)
        board = [[0] * 4 for _ in range(4)]
        for index, value in zip(spec["walls"], WALL_TILES):
            board[index // 4][index % 4] = value
        for row, col in random.sample(free_cells, random.randint(1, 4)):
            board[row][col] = 2 if random.random() < 0.9 else 4
        game.grid = board

        probs = query_probs(game.board_view, spec["target"])
        best_prob = max((value for value in probs.values() if value is not None), default=0.0)
        if best_prob > spec["min_prob"]:
            return game