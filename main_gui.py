import tkinter as tk
import copy
from game2048 import Game2048
from expectimax_ai_cpp import ExpectimaxAI

# 颜色配置
COLORS = {
    'bg': '#92877d',
    'cell_empty': '#9e948a',
    2: '#eee4da',
    4: '#ede0c8',
    8: '#f2b179',
    16: '#f59563',
    32: '#f67c5f',
    64: '#f65e3b',
    128: '#edcf72',
    256: '#edcc61',
    512: '#edc850',
    1024: '#edc53f',
    2048: '#edc22e',
}

FG_COLORS = {
    2: '#776e65',
    4: '#776e65',
    'default': '#f9f6f2'
}

class GUI2048(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("2048 with Expectimax AI")
        self.geometry("400x550")
        self.configure(bg=COLORS['bg'])
        
        self.game = Game2048()
        self.ai = ExpectimaxAI(max_depth=7)
        self.ai_enabled = False
        self.history = []  # 用于存储历史状态 (grid, score, steps)
        self.future_history = []  # 缓存撤销的路线，保证重做时一致
        self.steps = 0
        
        self.init_ui()
        self.update_ui()
        
        self.bind("<Key>", self.key_pressed)

    def init_ui(self):
        # 顶部面板 (分数和AI控制)
        top_frame = tk.Frame(self, bg=COLORS['bg'])
        top_frame.pack(fill=tk.X, padx=10, pady=10)
        
        self.score_label = tk.Label(top_frame, text="Score: 0", font=("Helvetica", 18, "bold"), bg=COLORS['bg'], fg="white")
        self.score_label.pack(side=tk.LEFT)
        
        self.step_label = tk.Label(top_frame, text="Steps: 0", font=("Helvetica", 18, "bold"), bg=COLORS['bg'], fg="#f9f6f2")
        self.step_label.pack(side=tk.LEFT, padx=15)
        
        self.ai_btn = tk.Button(top_frame, text="Start AI", font=("Helvetica", 12), command=self.toggle_ai)
        self.ai_btn.pack(side=tk.RIGHT)
        
        # 步进控制面板
        step_frame = tk.Frame(self, bg=COLORS['bg'])
        step_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.prev_btn = tk.Button(step_frame, text="⏪ 退一步", font=("Helvetica", 10), command=self.step_backward)
        self.prev_btn.pack(side=tk.LEFT, padx=5)
        
        self.next_btn = tk.Button(step_frame, text="前进一步(AI) ⏩", font=("Helvetica", 10), command=self.step_forward)
        self.next_btn.pack(side=tk.LEFT, padx=5)
        
        # 游戏棋盘
        self.board_frame = tk.Frame(self, bg=COLORS['bg'])
        self.board_frame.pack(padx=10, pady=10)
        
        self.cells = []
        for r in range(4):
            row_cells = []
            for c in range(4):
                cell = tk.Frame(self.board_frame, width=80, height=80, bg=COLORS['cell_empty'])
                cell.grid(row=r, column=c, padx=5, pady=5)
                label = tk.Label(cell, text="", font=("Helvetica", 24, "bold"), bg=COLORS['cell_empty'], fg="white")
                label.place(relx=0.5, rely=0.5, anchor="center")
                row_cells.append(label)
            self.cells.append(row_cells)

    def update_ui(self):
        self.score_label.config(text=f"Score: {self.game.score}")
        self.step_label.config(text=f"Steps: {self.steps}")
        for r in range(4):
            for c in range(4):
                val = self.game.grid[r][c]
                label = self.cells[r][c]
                if val == 0:
                    label.config(text="", bg=COLORS['cell_empty'])
                    label.master.config(bg=COLORS['cell_empty'])
                else:
                    bg_color = COLORS.get(val, COLORS[2048])
                    fg_color = FG_COLORS.get(val, FG_COLORS['default'])
                    label.config(text=str(val), bg=bg_color, fg=fg_color)
                    label.master.config(bg=bg_color)
                    
        self.update_idletasks()
        
        if self.game.is_game_over():
            self.score_label.config(text=f"Game Over! Score: {self.game.score}")
            self.ai_enabled = False
            self.ai_btn.config(text="Start AI")

    def toggle_ai(self):
        self.ai_enabled = not self.ai_enabled
        self.ai_btn.config(text="Stop AI" if self.ai_enabled else "Start AI")
        if self.ai_enabled:
            self.ai_move()

    def save_state(self):
        # 记录当前状态以便后退
        self.history.append((copy.deepcopy(self.game.grid), self.game.score, self.steps))

    def step_forward(self):
        if self.ai_enabled:
            return
            
        if self.future_history:
            # 严格重做：沿用撤销掉的未来状态（这意味着连随机生成的底块也会一模一样）
            self.save_state()
            next_grid, next_score, next_steps = self.future_history.pop()
            self.game.grid = copy.deepcopy(next_grid)
            self.game.score = next_score
            self.steps = next_steps
            self.update_ui()
        else:
            # 如果没有未来（到达当前时间线末端），那就运行AI生成新的一步
            if self.game.is_game_over(): 
                return
            best_dir = self.ai.get_best_move(self.game.grid)
            if best_dir is not None:
                self.save_state()
                if self.game.move(best_dir):
                    self.steps += 1
                    self.update_ui()
                else:
                    self.history.pop()

    def step_backward(self):
        if self.ai_enabled or not self.history:
            return
        
        # 将当下的盘面压入“未来历史”，用于前进一步时完美复现
        self.future_history.append((copy.deepcopy(self.game.grid), self.game.score, self.steps))
        
        prev_grid, prev_score, prev_steps = self.history.pop()
        self.game.grid = copy.deepcopy(prev_grid)
        self.game.score = prev_score
        self.steps = prev_steps
        self.update_ui()

    def ai_move(self):
        if not self.ai_enabled:
            return
            
        if self.future_history:
            # 如果撤销到了半中间直接开启全自动 AI，AI会先沿着我们给的“准确重演”之路把未来历史跑完
            self.save_state()
            next_grid, next_score, next_steps = self.future_history.pop()
            self.game.grid = copy.deepcopy(next_grid)
            self.game.score = next_score
            self.steps = next_steps
            self.update_ui()
            self.after(10, self.ai_move)
        else:
            if self.game.is_game_over():
                self.ai_enabled = False
                self.ai_btn.config(text="Start AI")
                return
                
            best_dir = self.ai.get_best_move(self.game.grid)
            if best_dir is not None:
                self.save_state()
                if self.game.move(best_dir):
                    self.steps += 1
                    self.update_ui()
                    self.after(10, self.ai_move)
                else:
                    self.history.pop()
                    self.ai_enabled = False
                    self.ai_btn.config(text="Start AI")
            else:
                self.ai_enabled = False
                self.ai_btn.config(text="Start AI")

    def key_pressed(self, event):
        if self.ai_enabled: return # AI 运行时禁用手动键盘
        
        key_mapping = {
            'Up': 0, 'w': 0, 'W': 0,
            'Down': 1, 's': 1, 'S': 1,
            'Left': 2, 'a': 2, 'A': 2,
            'Right': 3, 'd': 3, 'D': 3
        }
        
        if event.keysym in key_mapping:
            direction = key_mapping[event.keysym]
            self.save_state()
            
            if self.game.move(direction):
                self.steps += 1
                # 核心逻辑：玩家一旦通过键盘（手动）强行发生了新操作，我们就在该节点产生时间线分歧！
                # 清空所有的未来历史，因为新的平行宇宙开启了！
                self.future_history.clear()
                self.update_ui()
            else:
                self.history.pop()

if __name__ == "__main__":
    app = GUI2048()
    app.mainloop()
