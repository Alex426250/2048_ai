import tkinter as tk
import copy
import os
import sys
import io
if sys.stdout.encoding != 'utf-8':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
if sys.stderr.encoding != 'utf-8':
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')
import ctypes
if os.name == 'nt':
    ctypes.windll.kernel32.SetConsoleOutputCP(65001)
import traceback
from game2048 import Game2048
from base_agent.base_agent import TDAgent as TDAgentBase
TDAgent8k4k = __import__('8k4k_agent.8k4k_agent', fromlist=['TDAgent']).TDAgent
TDAgent16k8k4k = __import__('16k8k4k_agent.16k8k4k_agent', fromlist=['TDAgent']).TDAgent

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))

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
    4096: '#3c3a32',
    8192: '#3c3a32',
    16384: '#3c3a32',
    32768: '#3c3a32',
    65536: '#3c3a32',
}

FG_COLORS = {
    2: '#776e65',
    4: '#776e65',
    'default': '#f9f6f2'
}

class GUI2048(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("2048 AI")
        self.geometry("400x550")
        self.configure(bg=COLORS['bg'])
        
        self.game = Game2048()
        self.ai_base = TDAgentBase()
        self.ai_base.load_model("base_agent/base_model.bin")
        self.ai_8k4k = TDAgent8k4k()
        self.ai_8k4k.load_model("8k4k_agent/8k4k_model.bin")
        self.ai_16k8k4k = TDAgent16k8k4k()
        self.ai_16k8k4k.load_model("16k8k4k_agent/16k8k4k_model.bin")
        self.ai_enabled = False
        self.history = []  # 用于存储历史状态(grid, score, steps)
        self.future_history = []  # 缓存撤销的路线，保证重做时一致
        self.steps = 0
        
        self.init_ui()
        self.update_ui()
        
        self.bind("<Key>", self.key_pressed)

    def init_ui(self):
        # 顶部面板 (分数、步数和AI控制)
        top_frame = tk.Frame(self, bg=COLORS['bg'])
        top_frame.pack(fill=tk.X, padx=10, pady=10)
        
        self.score_label = tk.Label(top_frame, text="Score: 0", font=("Helvetica", 17, "bold"), bg=COLORS['bg'], fg="white")
        self.score_label.pack(side=tk.LEFT)
        
        self.step_label = tk.Label(top_frame, text="Steps: 0", font=("Helvetica", 17, "bold"), bg=COLORS['bg'], fg="#f9f6f2")
        self.step_label.pack(side=tk.LEFT, padx=4)
        
        self.ai_btn = tk.Button(top_frame, text="Start AI", font=("Helvetica", 12), command=self.toggle_ai)
        self.ai_btn.pack(side=tk.RIGHT)
        
        # 步进控制面板
        step_frame = tk.Frame(self, bg=COLORS['bg'])
        step_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.prev_10_btn = tk.Button(step_frame, text="⏮ 退十步", font=("Helvetica", 10), command=self.step_backward_10)
        self.prev_10_btn.pack(side=tk.LEFT, padx=5)
        
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
            
        # 居中显示 Game Over 标签 (初始隐藏)
        self.game_over_label = tk.Label(
            self.board_frame, 
            text="Game Over!", 
            font=("Helvetica", 36, "bold"), 
            bg=COLORS['bg'], 
            fg="black", 
            bd=0, 
            highlightthickness=0, 
            relief="flat"
        )
            
        # 底部控制面板
        bottom_frame = tk.Frame(self, bg=COLORS['bg'])
        bottom_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.custom_btn = tk.Button(bottom_frame, text="自定义盘面", font=("Helvetica", 10), command=self.open_custom_board_dialog)
        self.custom_btn.pack(side=tk.LEFT, padx=5)

        self.restart_btn = tk.Button(bottom_frame, text="重开", font=("Helvetica", 10), command=self.restart_game)
        self.restart_btn.pack(side=tk.LEFT, padx=5)
        
        class CircleSlider(tk.Canvas):
            def __init__(self, master, from_val=500, to_val=1, command=None):
                super().__init__(master, height=44, bg=COLORS['bg'], highlightthickness=0)
                self.from_val = from_val
                self.to_val = to_val
                self.command = command
                self.val = to_val
                self.bind("<Configure>", self.draw)
                self.bind("<Button-1>", self.update_val)
                self.bind("<B1-Motion>", self.update_val)
    
            def draw(self, event=None):
                self.delete("all")
                w = self.winfo_width()
                h = self.winfo_height()
                
                # 滑块线和球体的纵坐标位于控件绝对中心，这样就能与旁边的 Button 在视觉上完美中心对齐
                cy = h // 2  
                
                margin = 22 # 收缩边距使轨道更长，同时保留足够宽度防止挡字
                if w < margin * 2: return
                
                self.create_line(margin, cy, w-margin, cy, fill="#a09488", width=4, capstyle=tk.ROUND)
                prop = (self.val - self.from_val) / (self.to_val - self.from_val) if self.to_val != self.from_val else 0
                x = margin + prop * (w - margin * 2)
                
                # 文字显示在滑块正上方
                self.create_text(x, cy - 16, text=f"{int(float(self.val))} ms", font=("Helvetica", 10, "bold"), fill="#f9f6f2")
                
                # 画一个完美的同心圆滑块 (还原之前的 9 像素无损抗锯齿版本)
                self.create_oval(x-9, cy-9, x+9, cy+9, fill="#f9f6f2", outline="#8f7a66", width=2)
                
            def set(self, val):
                self.val = val
                self.draw()
    
            def get(self):
                return self.val
    
            def update_val(self, event):
                w = self.winfo_width()
                margin = 22
                if w < margin * 2: return
                x = max(margin, min(event.x, w-margin))
                prop = (x - margin) / (w - margin * 2)
                self.val = self.from_val + prop * (self.to_val - self.from_val)
                self.draw()
                if self.command: self.command(self.val)

        self.speed_scale = CircleSlider(bottom_frame, from_val=500, to_val=1)
        self.speed_scale.set(1)
        self.speed_scale.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(10, 5))

    def restart_game(self):
        self.ai_enabled = False
        self.ai_btn.config(text="Start AI")
        self.game = Game2048()
        self.history.clear()
        self.future_history.clear()
        self.steps = 0
        self.update_ui()

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
                    bg_color = COLORS.get(val, '#3c3a32')
                    fg_color = FG_COLORS.get(val, FG_COLORS['default'])
                    label.config(text=str(val), bg=bg_color, fg=fg_color)
                    label.master.config(bg=bg_color)
                    
        self.update_idletasks()
        
        if self.game.is_game_over():
            # 获取 board_frame 的准确底色以实现无缝融合
            self.game_over_label.config(bg=self.board_frame.cget("bg"))
            self.game_over_label.place(relx=0.5, rely=0.5, anchor="center")
            self.ai_enabled = False
            self.ai_btn.config(text="Start AI")
        else:
            self.game_over_label.place_forget()

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
            best_dir = self.get_best_move()
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

    def step_backward_10(self):
        if self.ai_enabled or not self.history:
            return

        steps_to_undo = min(10, len(self.history))
        for _ in range(steps_to_undo):
            self.future_history.append((copy.deepcopy(self.game.grid), self.game.score, self.steps))
            prev_grid, prev_score, prev_steps = self.history.pop()
            self.game.grid = copy.deepcopy(prev_grid)
            self.game.score = prev_score
            self.steps = prev_steps

        self.update_ui()

    def open_custom_board_dialog(self):
        from tkinter import simpledialog, messagebox
        import re
        if self.ai_enabled:
            return

        user_input = simpledialog.askstring("自定义盘面", "请输入16个数字，用逗号或空格分隔（顺序为从左到右、从上到下）：\n0表示空", parent=self)
        if user_input is not None:
            try:
                nums = [int(x) for x in re.split(r'[, \t]+', user_input.strip()) if x]
                if len(nums) == 16:
                    self.save_state()
                    self.future_history.clear()
                    
                    new_grid = []
                    for i in range(4):
                        new_grid.append(nums[i*4:(i+1)*4])
                    
                    self.game.grid = new_grid
                    self.update_ui()
                else:
                    messagebox.showerror("错误", f"必须输入恰好16个数字！你输入了{len(nums)}个。")
            except ValueError:
                messagebox.showerror("错误", "输入包含非数字内容！")

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
            self.after(int(float(self.speed_scale.get())), self.ai_move)
        else:
            if self.game.is_game_over():
                self.ai_enabled = False
                self.ai_btn.config(text="Start AI")
                return
                
            best_dir = self.get_best_move()
            if best_dir is not None:
                self.save_state()
                if self.game.move(best_dir):
                    self.steps += 1
                    self.update_ui()
                    self.after(int(float(self.speed_scale.get())), self.ai_move)
                else:
                    self.history.pop()
                    self.ai_enabled = False
                    self.ai_btn.config(text="Start AI")
            else:
                self.ai_enabled = False
                self.ai_btn.config(text="Start AI")

    def key_pressed(self, event):
        if self.ai_enabled: return # AI 运行时禁用手动键入
        
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

    def get_best_move(self):
        grid = self.game.grid
        flat_grid = [tile for row in grid for tile in row] if isinstance(grid[0], list) else grid
        
        large_vals = set(x for x in flat_grid if x >= 4096)
        A = len(large_vals)
        
        if A <= 1:
            return self.ai_base.get_best_move(grid)
        mapped_flat = list(flat_grid)
        min_large = min(large_vals)
        ratio = min_large // 4096
        if ratio > 1:
            for i in range(16):
                if mapped_flat[i] >= 4096:
                    mapped_flat[i] = mapped_flat[i] // ratio
        mapped_grid = [mapped_flat[i*4:(i+1)*4] for i in range(4)]
        if A == 2:
            return self.ai_8k4k.get_best_move(mapped_grid)
        return self.ai_16k8k4k.get_best_move(mapped_grid)
            

if __name__ == "__main__":
    app = GUI2048()
    app.mainloop()