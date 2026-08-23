import tkinter as tk
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
from game_session import GameSession
from endgame_rules import is_endgame_mode, wall_cells
from mode_controller import ModeController

_ROOT_DIR = os.path.dirname(os.path.abspath(__file__))

COLORS = {
    'bg': '#92877d',
    'cell_empty': '#9e948a',
    2: '#eee4da', 4: '#ede0c8', 8: '#f2b179', 16: '#f59563',
    32: '#f67c5f', 64: '#f65e3b', 128: '#edcf72', 256: '#edcc61',
    512: '#edc850', 1024: '#edc53f', 2048: '#edc22e', 4096: '#3c3a32',
    8192: '#3c3a32', 16384: '#3c3a32', 32768: '#3c3a32', 65536: '#3c3a32',
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
        
        self.mode = None
        self.rows = None
        self.cols = None
        self.board_size = None
        self.session = GameSession()
        self.mode_controller = ModeController(_ROOT_DIR)
        self.ai_enabled = False
        self.steps = 0
        self.last_table_probs_key = None
        self.last_table_probs_by_target = {}
        self.ai_job = None
        self._rendered_cells = []
        self._rendered_score = None
        self._rendered_steps = None
        
        self.show_mode_selector()
        self.protocol("WM_DELETE_WINDOW", self.shutdown)
        self.bind("<Key>", self.key_pressed)

    @property
    def game(self):
        return self.session.game

    @game.setter
    def game(self, value):
        self.session.game = value

    @property
    def history(self):
        return self.session.history

    @property
    def future_history(self):
        return self.session.future_history

    @property
    def steps(self):
        return self.session.steps

    @steps.setter
    def steps(self, value):
        self.session.steps = value

    def clear_window(self):
        for child in self.winfo_children():
            child.destroy()

    def stop_ai(self):
        self.ai_enabled = False
        if self.ai_job is not None:
            try:
                self.after_cancel(self.ai_job)
            except tk.TclError:
                pass
            self.ai_job = None

    def shutdown(self):
        self.stop_ai()
        self.mode_controller.close()
        self.destroy()

    def _schedule_ai_move(self):
        if self.ai_enabled:
            self.ai_job = self.after(int(float(self.speed_scale.get())), self.ai_move)

    def show_mode_selector(self):
        self.stop_ai()
        self.clear_window()
        self.mode = None
        self.title("2048 AI - 请选择模式")
        selector = tk.Frame(self, bg=COLORS['bg'])
        selector.pack(fill=tk.BOTH, expand=True)
        tk.Label(selector, text="选择棋盘尺寸", font=("Helvetica", 22, "bold"), bg=COLORS['bg'], fg="white").pack(pady=(28, 10))

        normal_frame = tk.Frame(selector, bg=COLORS['bg'])
        normal_frame.pack()
        normal_modes = (("4x4", lambda: self.start_mode(4, 4)), ("3x3", lambda: self.start_mode(3, 3)),
                        ("3x4", lambda: self.start_mode(3, 4)), ("2x4", lambda: self.start_mode(2, 4)))
        for index, (label, command) in enumerate(normal_modes):
            tk.Button(normal_frame, text=label, font=("Helvetica", 16, "bold"), width=9, height=2, command=command).grid(row=index // 2, column=index % 2, padx=10, pady=6)

        tk.Frame(selector, bg="#a69b90", height=1).pack(fill=tk.X, padx=48, pady=15)
        tk.Label(selector, text="残局胜率表", font=("Helvetica", 13, "bold"), bg=COLORS['bg'], fg="#f9f6f2").pack(pady=(0, 8))
        endgame_frame = tk.Frame(selector, bg=COLORS['bg'])
        endgame_frame.pack()
        endgame_modes = (("L3t_512", lambda: self.start_endgame_mode("L3t_512")), ("L3t_1024", lambda: self.start_endgame_mode("L3t_1024")),
                         ("442t_512", lambda: self.start_endgame_mode("442t_512")), ("442t_1024", lambda: self.start_endgame_mode("442t_1024")),
                         ("T_512", lambda: self.start_endgame_mode("T_512")), ("T_1024", lambda: self.start_endgame_mode("T_1024")))
        for index, (label, command) in enumerate(endgame_modes):
            tk.Button(endgame_frame, text=label, font=("Helvetica", 13, "bold"), width=10, height=2, command=command).grid(row=index // 2, column=index % 2, padx=9, pady=5)

    def start_mode(self, rows, cols):
        self._start_game(f"{rows}x{cols}", rows, cols)

    def start_endgame_mode(self, mode):
        self._start_game(mode, 4, 4)

    def _start_game(self, mode, rows, cols):
        self.rows = rows
        self.cols = cols
        self.board_size = rows * cols
        self.mode = mode
        self.session.reset(self.mode_controller.activate(self.mode))
        self._clear_table_prob_cache()

        self.clear_window()
        self.title(f"2048 AI - {self.mode}")
        self.init_ui()
        self.update_ui()

    def init_ui(self):
                           
        top_frame = tk.Frame(self, bg=COLORS['bg'])
        top_frame.pack(fill=tk.X, padx=10, pady=10)
        
        self.score_label = tk.Label(top_frame, text="Score: 0", font=("Helvetica", 17, "bold"), bg=COLORS['bg'], fg="white")
        self.score_label.pack(side=tk.LEFT)
        
        self.step_label = tk.Label(top_frame, text="Steps: 0", font=("Helvetica", 17, "bold"), bg=COLORS['bg'], fg="#f9f6f2")
        self.step_label.pack(side=tk.LEFT, padx=4)
        
        ai_control_frame = tk.Frame(top_frame, bg=COLORS['bg'])
        ai_control_frame.pack(side=tk.RIGHT)

        self.ai_btn = tk.Button(ai_control_frame, text="Start AI", font=("Helvetica", 12), command=self.toggle_ai)
        self.ai_btn.pack(side=tk.TOP)
        
        step_frame = tk.Frame(self, bg=COLORS['bg'])
        step_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.prev_10_btn = tk.Button(step_frame, text="⏮ 退十步", font=("Helvetica", 10), command=self.step_backward_10)
        self.prev_10_btn.pack(side=tk.LEFT, padx=5)
        
        self.prev_btn = tk.Button(step_frame, text="⏪ 退一步", font=("Helvetica", 10), command=self.step_backward)
        self.prev_btn.pack(side=tk.LEFT, padx=5)
        
        self.next_btn = tk.Button(step_frame, text="前进一步(AI) ⏩", font=("Helvetica", 10), command=self.step_forward)
        self.next_btn.pack(side=tk.LEFT, padx=5)

        self.switch_btn = tk.Button(step_frame, text="切换模式", font=("Helvetica", 10), command=self.show_mode_selector)
        self.switch_btn.pack(side=tk.LEFT, padx=5)
        
        self.board_container = tk.Frame(self, bg=COLORS['bg'])
        self.board_container.pack(padx=20, pady=10, anchor='w')

        self.board_frame = tk.Frame(self.board_container, bg=COLORS['bg'])
        self.board_frame.pack(side=tk.LEFT)

        is_2x4 = (self.mode == "2x4")
        if is_2x4:
            self.prob_panel = tk.Frame(self, bg=COLORS['bg'])                                  
        else:
            self.prob_panel = tk.Frame(self.board_container, bg=COLORS['bg'])

        self.prob_title1 = tk.Label(self.prob_panel, text="", font=("Helvetica", 12, "bold"), bg=COLORS['bg'], fg="#f9f6f2")
        self.prob_label1 = tk.Label(self.prob_panel, text="", justify=tk.LEFT, font=("Helvetica", 12, "bold"), bg=COLORS['bg'], fg="#f9f6f2")
        self.prob_title2 = tk.Label(self.prob_panel, text="", font=("Helvetica", 12, "bold"), bg=COLORS['bg'], fg="#f9f6f2")
        self.prob_label2 = tk.Label(self.prob_panel, text="", justify=tk.LEFT, font=("Helvetica", 12, "bold"), bg=COLORS['bg'], fg="#f9f6f2")

        if is_2x4:
            self.prob_panel.grid_columnconfigure(0, weight=1)
            self.prob_panel.grid_columnconfigure(1, weight=1)
            self.prob_title1.grid(row=0, column=0, sticky='w', pady=2)
            self.prob_label1.grid(row=1, column=0, sticky='w', pady=2)
            self.prob_title2.grid(row=0, column=1, sticky='e', pady=2)
            self.prob_label2.grid(row=1, column=1, sticky='e', pady=2)
        else:
            self.prob_title1.pack(anchor="w")
            self.prob_label1.pack(anchor="w")
            self.prob_title2.pack(anchor="w")
            self.prob_label2.pack(anchor="w")

        if self._table_targets() is not None:
            if is_2x4:
                self.prob_panel.pack(side=tk.TOP, fill=tk.X, padx=20, pady=5)
            else:
                self.prob_panel.pack(side=tk.LEFT, padx=(10, 0), anchor="n")
        
        self.canvas_tiles = []
        self._rendered_cells = [[None] * self.cols for _ in range(self.rows)]
        self._rendered_score = None
        self._rendered_steps = None
        self.cell_size = 54 if is_endgame_mode(self.mode) else (80 if self.cols == 4 else 65)
        self.cell_font_sizes = (22, 20, 17, 15, 13) if is_endgame_mode(self.mode) else (
            (30, 28, 24, 21, 18) if self.cols == 4 else (26, 24, 21, 18, 16)
        )
        cell_padding = 3 if is_endgame_mode(self.mode) else 5
        board_width = self.cols * (self.cell_size + cell_padding * 2)
        board_height = self.rows * (self.cell_size + cell_padding * 2)
        self.board_canvas = tk.Canvas(
            self.board_frame,
            width=board_width,
            height=board_height,
            bg=COLORS['bg'],
            highlightthickness=0,
        )
        self.board_canvas.pack()
        for r in range(self.rows):
            row_cells = []
            for c in range(self.cols):
                x0 = c * (self.cell_size + cell_padding * 2) + cell_padding
                y0 = r * (self.cell_size + cell_padding * 2) + cell_padding
                rectangle = self.board_canvas.create_rectangle(
                    x0, y0, x0 + self.cell_size, y0 + self.cell_size,
                    fill=COLORS['cell_empty'], outline="",
                )
                text = self.board_canvas.create_text(
                    x0 + self.cell_size / 2,
                    y0 + self.cell_size / 2,
                    text="",
                    font=("Helvetica", self.cell_font_sizes[0], "bold"),
                    fill="white",
                )
                row_cells.append((rectangle, text))
            self.canvas_tiles.append(row_cells)
            
        bottom_frame = tk.Frame(self, bg=COLORS['bg'])
        bottom_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.custom_btn = tk.Button(bottom_frame, text="自定义盘面", font=("Helvetica", 10), command=self.open_custom_board_dialog)
        self.custom_btn.pack(side=tk.LEFT, padx=5)

        self.copy_btn = tk.Button(bottom_frame, text="复制盘面", font=("Helvetica", 10), command=self.copy_current_board)
        self.copy_btn.pack(side=tk.LEFT, padx=5)

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
                
                cy = h // 2  
                
                left_margin, right_margin = 24, 14
                if w < left_margin + right_margin: return
                
                self.create_line(left_margin, cy, w-right_margin, cy, fill="#a09488", width=4, capstyle=tk.ROUND)
                prop = (self.val - self.from_val) / (self.to_val - self.from_val) if self.to_val != self.from_val else 0
                x = left_margin + prop * (w - left_margin - right_margin)
                
                self.create_text(x, cy - 16, text=f"{int(float(self.val))} ms", font=("Helvetica", 10, "bold"), fill="#f9f6f2")
                
                self.create_oval(x-9, cy-9, x+9, cy+9, fill="#f9f6f2", outline="#8f7a66", width=2)
                
            def set(self, val):
                self.val = val
                self.draw()
    
            def get(self):
                return self.val
    
            def update_val(self, event):
                w = self.winfo_width()
                left_margin, right_margin = 24, 14
                if w < left_margin + right_margin: return
                x = max(left_margin, min(event.x, w-right_margin))
                prop = (x - left_margin) / (w - left_margin - right_margin)
                self.val = self.from_val + prop * (self.to_val - self.from_val)
                self.draw()
                if self.command: self.command(self.val)

        self.speed_scale = CircleSlider(bottom_frame, from_val=500, to_val=1)
        self.speed_scale.set(1)
        self.speed_scale.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(10, 5))

    def restart_game(self):
        self.stop_ai()
        self.ai_btn.config(text="Start AI")
        self.session.reset(self.mode_controller.create_game())
        self._clear_table_prob_cache()
        self.update_ui()

    def _table_targets(self):
        return self.mode_controller.table_targets

    def _clear_table_prob_cache(self):
        self.last_table_probs_key = None
        self.last_table_probs_by_target = {}

    def _board_hex(self, grid):
        walls = wall_cells(grid) if is_endgame_mode(self.mode) else frozenset()
        digits = [
            "f" if index in walls else "0" if value == 0 else format(value.bit_length() - 1, "x")
            for index, value in enumerate(tile for row in grid for tile in row)
        ]
        return "".join(digits) if all(len(digit) == 1 for digit in digits) else " ".join(digits)

    def copy_current_board(self):
        self.clipboard_clear()
        self.clipboard_append(self._board_hex(self.game.board_view))
        self.update()

    def _get_cached_table_probs(self, target, grid=None):
        targets = self._table_targets()
        if targets is None:
            return None
        if grid is None:
            grid = self.game.board_view
        key = (self.mode, self.game.state_version)
        if key == self.last_table_probs_key:
            return self.last_table_probs_by_target.get(target)

        self.last_table_probs_key = key
        self.last_table_probs_by_target = {t: self.mode_controller.query_probs(grid, t) for t in targets}
        return self.last_table_probs_by_target.get(target)

    @staticmethod
    def _format_probs_text(probs):
        if not probs:
            return "上: None\n右: None\n下: None\n左: None"

        dir_name = {"U": "上", "R": "右", "D": "下", "L": "左"}
        tie_order = {"U": 0, "R": 1, "D": 2, "L": 3}

        def score_of(value):
            if value is None:
                return -1.0
            return value

        items = [(direction, probs.get(direction)) for direction in ("U", "R", "D", "L")]
        items.sort(key=lambda item: (score_of(item[1]), -tie_order[item[0]]), reverse=True)

        lines = []
        for direction, value in items:
            shown = "None" if value is None else f"{value:.7f}".rstrip("0").rstrip(".")
            lines.append(f"{dir_name[direction]}: {shown}")
        return "\n".join(lines)

    def _transform_grid(self, grid):
        targets = self._table_targets()
        if targets is None:
            return grid
        high_target = targets[1]
        flat = [value for row in grid for value in row]
        if max(flat) < high_target:
            return grid

        k = self._find_missing_power_k(grid)
        if k is None:
            return grid

        mapped = []
        for row in grid:
            mapped_row = []
            for value in row:
                mapped_row.append(value // 2 if value > k else value)
            mapped.append(mapped_row)
        return mapped

    @staticmethod
    def _find_missing_power_k(grid):
        present = {value for row in grid for value in row if value > 0}
        for exp in range(len(grid) * len(grid[0]) + 1, 0, -1):
            value = 1 << exp
            if value not in present:
                return value
        return None

    @staticmethod
    def _set_widget_visible(widget, visible, **pack_kwargs):
        if visible:
            if widget.winfo_manager():
                widget.pack_configure(**pack_kwargs)
            else:
                widget.pack(**pack_kwargs)
        elif widget.winfo_manager():
            widget.pack_forget()

    def _all_prob_widgets(self):
        return self.prob_title1, self.prob_label1, self.prob_title2, self.prob_label2

    def _show_prob_labels(self, labels):
        is_2x4 = (self.mode == "2x4")
        if is_2x4:
            for widget in self._all_prob_widgets():
                widget.grid_remove()
            for widget, kwargs in labels:
                widget.grid()
        else:
            for widget in self._all_prob_widgets():
                self._set_widget_visible(widget, False)
            for widget, kwargs in labels:
                self._set_widget_visible(widget, True, **kwargs)

    def _update_table_prob_panel(self, grid):
        targets = self._table_targets()
        if targets is None:
            self._show_prob_labels([])
            return

        if is_endgame_mode(self.mode):
            target = targets[0]
            self.prob_title1.config(text=f"{target} 胜率")
            self.prob_label1.config(text=self._format_probs_text(self._get_cached_table_probs(target, grid)))
            self._show_prob_labels([
                (self.prob_title1, {"anchor": "w", "pady": (0, 2)}),
                (self.prob_label1, {"anchor": "w"})
            ])
            return

        low_target, high_target = targets
        max_tile = max(value for row in grid for value in row)

        if max_tile < low_target:
            probs1 = self._get_cached_table_probs(low_target, grid)
            probs2 = self._get_cached_table_probs(high_target, grid)
            self.prob_title1.config(text=f"{low_target} 胜率")
            self.prob_title2.config(text=f"{high_target} 胜率")
            self.prob_label1.config(text=self._format_probs_text(probs1))
            self.prob_label2.config(text=self._format_probs_text(probs2))
            self._show_prob_labels([
                (self.prob_title1, {"anchor": "w", "pady": (0, 2)}),
                (self.prob_label1, {"anchor": "w", "pady": (0, 8)}),
                (self.prob_title2, {"anchor": "w", "pady": (0, 2)}),
                (self.prob_label2, {"anchor": "w"})
            ])
            return

        if max_tile < high_target:
            probs2 = self._get_cached_table_probs(high_target, grid)
            self.prob_title1.config(text=f"{high_target} 胜率")
            self.prob_label1.config(text=self._format_probs_text(probs2))
            self._show_prob_labels([
                (self.prob_title1, {"anchor": "w"}),
                (self.prob_label1, {"anchor": "w"})
            ])
            return

        k = self._find_missing_power_k(grid)
        if k is None or k <= 8:
            self._show_prob_labels([])
            return

        transformed_grid = self._transform_grid(grid)
        probs_special = self.mode_controller.query_probs(transformed_grid, target=high_target)
        self.prob_title1.config(text=f"再合出{k}胜率")
        self.prob_label1.config(text=self._format_probs_text(probs_special))
        self._show_prob_labels([
            (self.prob_title1, {"anchor": "w"}),
            (self.prob_label1, {"anchor": "w"})
        ])

    def update_ui(self):
        if self.game.score != self._rendered_score:
            self.score_label.config(text=f"Score: {self.game.score}")
            self._rendered_score = self.game.score
        if self.steps != self._rendered_steps:
            self.step_label.config(text=f"Steps: {self.steps}")
            self._rendered_steps = self.steps
        grid = self.game.board_view
        self.title(f"2048 AI - {self.mode} - {self._board_hex(grid)}")
        walls = wall_cells(grid) if is_endgame_mode(self.mode) else frozenset()
        for r in range(self.rows):
            for c in range(self.cols):
                val = grid[r][c]
                rectangle, text_item = self.canvas_tiles[r][c]
                index = r * self.cols + c
                if index in walls:
                    state = ("", "#3c3a32", FG_COLORS['default'], None)
                elif val == 0:
                    state = ("", COLORS['cell_empty'], None, None)
                else:
                    bg_color = COLORS.get(val, '#3c3a32')
                    fg_color = FG_COLORS.get(val, FG_COLORS['default'])
                    text = str(val)
                    font_size = self.cell_font_sizes[min(len(text), len(self.cell_font_sizes)) - 1]
                    state = (text, bg_color, fg_color, font_size)
                if state != self._rendered_cells[r][c]:
                    text, bg_color, fg_color, font_size = state
                    self.board_canvas.itemconfigure(rectangle, fill=bg_color)
                    self.board_canvas.itemconfigure(
                        text_item,
                        text=text,
                        fill=fg_color or FG_COLORS['default'],
                    )
                    if font_size is not None:
                        self.board_canvas.itemconfigure(text_item, font=("Helvetica", font_size, "bold"))
                    self._rendered_cells[r][c] = state

        if self._table_targets() is not None:
            self._update_table_prob_panel(grid)

        endgame_probs = self._get_cached_table_probs(self._table_targets()[0], grid) if is_endgame_mode(self.mode) else None
        if self.game.is_game_over() or self.mode_controller.is_finished(grid, endgame_probs):
            self.ai_enabled = False
            self.ai_btn.config(text="Start AI")

    def toggle_ai(self):
        if self.ai_enabled:
            self.stop_ai()
            self.ai_btn.config(text = "Start AI")
            return
        if self.mode_controller.is_finished(self.game.board_view):
            return
        self.ai_enabled = True
        self.ai_btn.config(text = "Stop AI")
        self.future_history.clear()
        self.ai_move()

    def step_forward(self):
        if self.ai_enabled or self.mode_controller.is_finished(self.game.board_view):
            return
            
        if self.future_history:
            self.session.redo()
            self.update_ui()
        else:
                                            
            if self.game.is_game_over(): 
                return
            best_dir = self.get_best_move()
            if best_dir is not None:
                if self.session.move_new(best_dir):
                    self.update_ui()

    def step_backward(self):
        if self.ai_enabled or not self.history:
            return

        self.session.undo()
        self.update_ui()

    def step_backward_10(self):
        if self.ai_enabled or not self.history:
            return

        self.session.undo(10)
        self.update_ui()

    def open_custom_board_dialog(self):
        from tkinter import simpledialog, messagebox
        if self.ai_enabled:
            return

        if is_endgame_mode(self.mode):
            prompt = (
                "请输入16个数（从左到右、从上到下，可包含空格或逗号）：\n"
                "必须恰好6个A-F表示大数，位置必须匹配支持的CFG；其余格填写小数码。"
            )
        else:
            prompt = (
                f"请输入{self.board_size}个16进制数（顺序为从左到右、从上到下，可包含空格或逗号）：\n"
                "0表示空，其他数码k表示2^k"
            )
        user_input = simpledialog.askstring(
            "自定义盘面",
            prompt,
            parent=self,
        )
        if user_input is not None:
            try:
                new_grid = self.mode_controller.parse_custom_board(user_input)
                self.game.grid = new_grid
                self.game.score = 0
                self.session.history.clear()
                self.session.future_history.clear()
                self.steps = 0
                self.update_ui()
            except ValueError as exc:
                messagebox.showerror("错误", str(exc))

    def ai_move(self):
        self.ai_job = None
        if not self.ai_enabled:
            return
            
        if self.future_history:
            self.session.redo()
            self.update_ui()
            self._schedule_ai_move()
        else:
            if self.game.is_game_over():
                self.ai_enabled = False
                self.ai_btn.config(text="Start AI")
                return
                
            best_dir = self.get_best_move()
            if best_dir is not None:
                if self.session.move_new(best_dir):
                    self.update_ui()
                    self._schedule_ai_move()
                else:
                    self.ai_enabled = False
                    self.ai_btn.config(text="Start AI")
            else:
                self.ai_enabled = False
                self.ai_btn.config(text="Start AI")

    def key_pressed(self, event):
        if self.ai_enabled: return               
        
        key_mapping = {
            'Up': 0, 'w': 0, 'W': 0,
            'Down': 1, 's': 1, 'S': 1,
            'Left': 2, 'a': 2, 'A': 2,
            'Right': 3, 'd': 3, 'D': 3
        }
        
        if event.keysym in key_mapping:
            direction = key_mapping[event.keysym]
            if not self.mode_controller.is_move_allowed(self.game.board_view, direction):
                return
            if self.session.move_new(direction, clear_future=True):
                self.update_ui()

    def get_best_move(self):
        return self.mode_controller.best_move(self.game.board_view)

if __name__ == "__main__":
    app = GUI2048()
    app.mainloop()