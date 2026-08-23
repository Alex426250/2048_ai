def select_4x4_move(grid, agents):
    flat = [tile for row in grid for tile in row]
    large = {value for value in flat if value >= 4096}
    if len(large) <= 1:
        return agents["base"].get_best_move(grid)

    ratio = min(large) // 4096
    mapped = [value // ratio if value >= 4096 and ratio > 1 else value for value in flat]
    board = [mapped[index * 4:(index + 1) * 4] for index in range(4)]
    agent_name = "8k4k" if len(large) == 2 else "16k8k4k"
    return agents[agent_name].get_best_move(board)


def select_3x4_move(grid, agents):
    flat = [tile for row in grid for tile in row]
    large = {value for value in flat if value >= 1024}
    if len(large) <= 1:
        return agents["base"].get_best_move(grid)
    if len(large) == 2:
        return agents["2k1k"].get_best_move(grid)

    missing = 1024
    present = set(flat)
    while missing >= 2 and missing in present:
        missing //= 2
    if missing < 128:
        return agents["2k1k"].get_best_move(grid)

    mapped = [value // 2 if value > missing else value for value in flat]
    board = [mapped[index * 4:(index + 1) * 4] for index in range(3)]
    return agents["2k1k"].get_best_move(board)
