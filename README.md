# 2048 AI

这是一个支持普通棋盘模式和 6 个残局胜率模式的 2048 AI 项目。

| 模式 | 用途 | 运行时依赖 |
| --- | --- | --- |
| 4x4 | 完整 4x4 对局 | 预训练模型与 C++ DLL |
| 3x4 | 完整 3x4 对局 | 预训练模型与 C++ DLL |
| 3x3 | 目标块胜率查询 | 胜率表与查询 DLL |
| 2x4 | 目标块胜率查询 | 胜率表与查询 DLL |
| L3t_512 / L3t_1024 | 4x4 L3t 残局胜率查询 | 胜率表与查询 DLL |
| 442t_512 / 442t_1024 | 4x4 442t 残局胜率查询 | 胜率表与查询 DLL |
| T_512 / T_1024 | 4x4 T 残局胜率查询 | 胜率表与查询 DLL |

项目无需 GPU。`main_gui.py` 提供可视化对局、AI 自动运行、单步、撤销/重做、自定义盘面和胜率展示。

## 环境要求

- Python 3
- 支持 C++17 的 `g++`
- OpenMP：4x4 与 3x4 AI 的 DLL 编译需要
- Git LFS：获取 4x4 与 3x4 的大型预训练模型

Windows 下可使用 MinGW，例如 `C:\MinGW\bin\g++.EXE`；Linux/macOS 需要将 DLL 输出后缀改为 `.so`，并同步修改 Python 中的加载路径。

当前 Windows MinGW GCC 13.2.0 在以 `-O3` 编译较大的残局构表模板时，可能偶发报告 `internal compiler error`，例如优化阶段的 `Segmentation fault` 或 `Illegal instruction`。这是编译器自身崩溃，而非普通 C++ 诊断；同一命令重试通常可以成功。若持续复现，请保留编译命令与 GCC 版本，改用已验证的 GCC/Clang 工具链，或按 GCC 提示提交最小复现。

## 获取模型

4x4 和 3x4 的预训练权重文件较大。克隆仓库前请初始化 Git LFS：

```bash
git lfs install
git clone https://github.com/Alex426250/2048_ai.git
```

未下载完整模型时，4x4 或 3x4 模式会加载失败或无法正常决策。

## 快速开始

### 1. 编译 4x4 AI

进入 `2048_4x4`：

```bash
g++ -O3 -flto -shared -fPIC -march=native -fopenmp base_agent/base_agent.cpp -o base_agent/base_agent.dll
g++ -O3 -flto -shared -fPIC -march=native -fopenmp 8k4k_agent/8k4k_agent.cpp -o 8k4k_agent/8k4k_agent.dll
g++ -O3 -flto -shared -fPIC -march=native -fopenmp 16k8k4k_agent/16k8k4k_agent.cpp -o 16k8k4k_agent/16k8k4k_agent.dll
```

4x4 后端每局搜索最多使用 4 个 OpenMP 线程，适合与 `measure_10000_games.py` 的多进程 worker 并行运行。

### 2. 编译 3x4 AI

进入 `2048_3x4`：

```bash
g++ -O3 -shared -fPIC -march=native -fopenmp base_agent/base_agent.cpp -o base_agent/base_agent.dll
g++ -O3 -shared -fPIC -march=native -fopenmp 2k1k_agent/2k1k_agent.cpp -o 2k1k_agent/2k1k_agent.dll
```

### 3. 编译 3x3 与 2x4 胜率表查询器

进入 `2048_3x3`：

```bash
g++ -O3 -march=native -std=c++17 -Wall -Wextra -pthread build_3x3_table.cpp -o build_3x3_table.exe
g++ -O3 -march=native -std=c++17 -Wall -Wextra -pthread query_3x3.cpp -o query_3x3.exe
g++ -O3 -shared -fPIC -march=native query_3x3.cpp -o query_3x3.dll
```

进入 `2048_2x4`：

```bash
g++ -O3 -march=native -std=c++17 -Wall -Wextra -pthread build_2x4_table.cpp -o build_2x4_table.exe
g++ -O3 -march=native -std=c++17 -Wall -Wextra -pthread query_2x4.cpp -o query_2x4.exe
g++ -O3 -shared -fPIC -march=native query_2x4.cpp -o query_2x4.dll
```

若缺少表文件，可构建：

```bash
cd 2048_3x3
./build_3x3_table.exe --target 512 --threads 32 --output table_3x3_512.bin
./build_3x3_table.exe --target 1024 --threads 32 --output table_3x3_1024.bin

cd ../2048_2x4
./build_2x4_table.exe --target 256 --threads 32 --output table_2x4_256.bin
./build_2x4_table.exe --target 512 --threads 32 --output table_2x4_512.bin
```

### 4. 编译残局查询器与构建胜率表

三个残局目录均需分别编译查询器 DLL、命令行查询器和构表程序。以 `endgame_L3t` 为例：

```bash
g++ -O3 -march=native query_L3t.cpp -o query_L3t.exe
g++ -O3 -shared -fPIC -march=native query_L3t.cpp -o query_L3t.dll
g++ -O3 -march=native -std=c++17 -pthread build_L3t_table.cpp -o build_L3t_table.exe
```

`endgame_442t`：

```bash
g++ -O3 -march=native query_442t.cpp -o query_442t.exe
g++ -O3 -shared -fPIC -march=native query_442t.cpp -o query_442t.dll
g++ -O3 -march=native -std=c++17 -pthread build_442t_table.cpp -o build_442t_table.exe
```

`endgame_T`：

```bash
g++ -O3 -march=native query_T.cpp -o query_T.exe
g++ -O3 -shared -fPIC -march=native query_T.cpp -o query_T.dll
g++ -O3 -march=native -std=c++17 -pthread build_T_table.cpp -o build_T_table.exe
```

每个目录需要对应的 `query_<定式>.dll` 与 `table_<定式>_512.bin`、`table_<定式>_1024.bin`。如需重建全部残局表：

```bash
cd endgame_L3t
./build_L3t_table.exe --target 512 --threads 32 --threshold --output table_L3t_512.bin
./build_L3t_table.exe --target 1024 --threads 32 --threshold --output table_L3t_1024.bin

cd ../endgame_442t
./build_442t_table.exe --target 512 --threads 32 --threshold --output table_442t_512.bin
./build_442t_table.exe --target 1024 --threads 32 --threshold --output table_442t_1024.bin

cd ../endgame_T
./build_T_table.exe --target 512 --threads 32 --threshold --output table_T_512.bin
./build_T_table.exe --target 1024 --threads 32 --threshold --output table_T_1024.bin
```

`--threshold` 是无参数开关，同时启用 P 状态和 R 状态剪枝：构表器保留最佳合法方向胜率达到阈值的可达 P 状态，并只导出达到该阈值的 R 状态。胜率为 `1` 的 P 仍会生成并导出其非获胜方向的 R，但不会再通过随机落子扩展后继 P。`512` 自动使用 `0.05`，`1024` 自动使用 `0.01`；省略该开关则关闭剪枝。该开关仅支持这两个目标。被 R 剪枝排除的合法方向在查询结果中显示为 `0`，而 `None` 表示该方向本身不合法。构表器每次都会重新生成临时分层文件，并在完成后自动删除临时目录。

L3t、442t 与 T 的构表算法共享 `endgame_table_core.inl` 核心；三个 `build_*_table.cpp` 均只包含同目录的 `build_config.h` 和共享核心。各定式的墙位 CFG、表魔数和临时目录定义在自己的 `build_config.h`。修改构表算法时应修改共享核心，再重新编译并重建三种定式的表。

CFG 定义变更后，必须同时重新编译对应查询器并重新构表；旧 `.bin` 表会因 CFG 数量和墙位掩码不匹配而无法加载。

## 运行

在项目根目录启动 GUI：

```bash
python main_gui.py
```

### 残局 CFG 与自定义盘面

CFG 是 Configuration（配置）的缩写，表示残局盘面中 6 个大数（内部墙块）所在的格子位置组合。格子编号从左到右、从上到下按 `0` 到 `15` 编号。

| 定式 | 支持的墙位 CFG |
| --- | --- |
| L3t | `{9,10,11,13,14,15}`、`{5,9,10,11,13,14}`、`{6,9,10,11,13,14}`、`{7,9,10,11,13,14}`、`{6,7,9,10,11,13}`、`{6,7,9,10,11,14}`、`{5,6,9,10,11,13}`、`{5,6,9,10,11,14}`、`{1,5,6,10,11,14}`、`{2,5,6,10,11,14}`、`{6,7,9,10,13,14}`、`{5,6,9,10,13,15}` |
| 442t | `{10,11,12,13,14,15}`、`{4,10,11,13,14,15}`、`{8,10,11,13,14,15}`、`{7,10,11,12,13,14}`、`{7,8,10,11,13,14}`、`{6,8,10,11,13,14}`、`{5,10,11,12,13,14}`、`{6,10,11,12,13,14}`、`{6,10,11,12,13,15}`、`{6,7,10,11,12,13}`、`{5,6,10,11,12,13}`、`{5,10,11,12,13,15}` |
| T | `{8,9,11,12,13,15}`、`{3,7,8,9,12,13}`、`{7,8,9,11,12,13}`、`{4,8,9,11,13,15}`、`{5,8,9,11,13,15}`、`{6,8,9,11,12,13}`、`{5,8,9,11,12,13}`、`{4,7,8,9,11,13}`、`{5,7,8,9,11,13}`、`{5,8,9,11,12,15}`、`{4,5,8,9,11,15}`、`{0,4,9,11,13,15}` |

GUI 的“自定义盘面”要求输入 16 个十六进制指数。单数码可连续输入，也可使用空格、逗号或换行分隔；出现 `10` 这类多位指数时必须使用分隔符。残局模式必须恰好输入 6 个大数 `A-F`；这 6 个数的位置必须完整匹配上表中当前定式的一组 CFG。程序会将它们标准化为不可移动的内部墙块，因此棋盘上显示为全黑且不会参与合并。其余 10 个可玩格只能输入 `0-9`。

残局模式的 `Start AI` 会选择当前胜率最高的合法方向。当最佳合法方向的胜率为 `1` 或 `0` 时，AI 自动停止；它不再以盘面是否已经合出目标块作为停止条件。

## 命令行查询

盘面均按行主序以十六进制指数编码：`0` 表示空格，`1` 表示 `2`，`9` 表示 `512`，`A` 表示 `1024`。

```bash
cd 2048_3x3
./query_3x3.exe 1024 110000000

cd ../2048_2x4
./query_2x4.exe 512 11000000

cd ../endgame_L3t
./query_L3t.exe 512 110000000AAA0AAA
./query_L3t.exe 1024 110000000AAA0AAA
```

残局查询盘面中的 6 个大数必须满足对应程序预定义的 CFG 配置；会使大数分布离开允许配置的方向返回 `None`。GUI 自定义盘面与命令行均可按上表输入任一支持的 CFG。

## 评测与训练

| 目录 | 命令 | 用途 |
| --- | --- | --- |
| `2048_4x4` | `python measure_speed.py` | 单局后台速度评测 |
| `2048_4x4` | `python measure_10000_games.py` | 多进程连续对局统计 |
| `2048_3x4` | `python measure_speed.py` | 单局后台速度评测 |
| `2048_3x4` | `python measure_10000_games.py` | 多进程连续对局统计 |
| `2048_4x4/*_agent` | `python *_agent.py` | 对应阶段模型训练 |
| `2048_3x4/*_agent` | `python *_agent.py` | 对应阶段模型训练 |

训练前如需从零开始，请删除对应的模型文件。后台速度会受到搜索深度、CPU 频率、内存带宽、OpenMP 配置和并发 worker 数量影响；请以本机实际测量结果为准。

### 可复现实验

本仓库的性能表是参考结果，不应视为跨机器可直接复现的基准结论。提交或引用新的实验结果时，应同时记录：提交版本、操作系统、Python 与编译器版本、完整编译参数、CPU 型号、OpenMP 线程数、worker 数、模型文件的 SHA-256、对局数和统计口径。

`measure_10000_games.py` 在 4x4 与 3x4 中均默认运行 10000 局，可通过 `--games`、`--workers`、`--output` 调整，并可用 `--seed N` 为每个 worker 分配确定性 Python 随机种子。结果默认覆盖输出文件；需保留历史结果时传入 `--append`。当前 C++ 训练流程仍未固定随机种子；因此若需要严格可重复的逐局结果，应同时记录模型、编译器和所有随机源，再报告均值、分位数或置信区间，而非只报告单次运行结果。

## 性能数据

速度为后台评测吞吐，不能直接代表 GUI 中的速度。GUI 每步都需要刷新棋盘、分数、步数和胜率信息，渲染与事件调度会使运行速度**显著变慢**。当前 4x4 后端速度在 **Intel Core i9-14900HX** 上测得；使用当前 `-O3 -flto -march=native -fopenmp` 构建，4 层搜索（`depth=3`）约为 **1200 steps/s**。

下表其余数据为项目已有的 10000 局评测记录。

### 4x4

性能表中的 "adaptive 5-ply" 指：盘面上不小于 `256` 的**不同数值**达到至少 5 种时使用 `depth=4`，否则使用 `depth=3`。当前默认 4x4 构建在代码中固定使用 `depth=3`，不会自动启用该自适应策略。

| 搜索深度 | 3-ply | 4-ply | adaptive 5-ply |
| --- | --- | --- | --- |
| 运行速度 | ~8500 steps/s | ~1100 steps/s | ~550 steps/s |
| 平均分 | 539449 | 568017 | **578585** |
| 8192 成功率 | 99.75% | **99.84%** | 99.83% |
| 16384 成功率 | 95.60% | 96.67% | **96.94%** |
| 32768 成功率 | 53.85% | 59.86% | **61.82%** |
| 超高分(>800000) | 10.29% | 13.77% | **14.86%** |
### 3x4

| 搜索深度 | 3-ply | 4-ply | 5-ply |
| --- | --- | --- | --- |
| 运行速度 | ~11000 steps/s | ~3000 steps/s | ~450 steps/s |
| 平均分 | 46152 | 46902 | **47330** |
| 2048 成功率 | 96.97% | 96.99% | **97.71%** |
| 4096 成功率 | 39.39% | 41.32% | **42.09%** |

## 已知限制

- 4x4 与 3x4 依赖本地模型文件；模型缺失、损坏或格式不匹配时无法正常运行。
- 3x3、2x4 与各残局模式依赖各自的查询 DLL 和 `.bin` 胜率表；文件名和目录位置必须与程序加载路径一致。
- L3t、442t、T 仅适用于各自预定义的大数配置残局，不是完整 4x4 对局的通用替代方案。

## 参考文献

1. Hung Guei, Lung-Pin Chen, and I-Chen Wu. Optimistic temporal difference learning for 2048. IEEE Transactions on Games, 14(2):283-292, 2022.
2. Wojciech Jaskowski. Mastering 2048 with delayed temporal coherence learning, multi-stage weight promotion, redundant encoding, and carousel shaping. IEEE Transactions on Computational Intelligence and AI in Games, 10(2):122-134, 2018.
3. Marcin Szubert and Wojciech Jaskowski. Temporal difference learning of n-tuple networks for the game 2048. In 2014 IEEE Conference on Computational Intelligence and Games.
4. Kun-Hao Yeh et al. Multi-stage temporal difference learning for 2048-like games. IEEE Transactions on Computational Intelligence and AI in Games, 9(4):369-380, 2017.
