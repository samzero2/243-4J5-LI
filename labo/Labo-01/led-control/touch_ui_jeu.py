#!/usr/bin/env python3
import curses
import threading
import time
from queue import Queue, Empty

from evdev import InputDevice, ecodes, list_devices
import serial


SERIAL_PORT = "/dev/ttyACM0"
SERIAL_BAUD = 115200


class TouchReader(threading.Thread):
    def __init__(self, event_queue: Queue):
        super().__init__(daemon=True)
        self.event_queue = event_queue
        self.device = self._find_touch_device()
        if not self.device:
            raise RuntimeError("Aucun ecran tactile detecte.")

        abs_x = self.device.absinfo(ecodes.ABS_MT_POSITION_X)
        abs_y = self.device.absinfo(ecodes.ABS_MT_POSITION_Y)
        self.min_x, self.max_x = abs_x.min, abs_x.max
        self.min_y, self.max_y = abs_y.min, abs_y.max
        self.current_x = (self.min_x + self.max_x) // 2
        self.current_y = (self.min_y + self.max_y) // 2

    def _find_touch_device(self):
        for path in list_devices():
            dev = InputDevice(path)
            name = dev.name.lower()
            if "touch" in name or "ft5406" in name:
                return dev
        return None

    def run(self):
        for event in self.device.read_loop():
            if event.type == ecodes.EV_ABS:
                if event.code == ecodes.ABS_MT_POSITION_X:
                    self.current_x = event.value
                elif event.code == ecodes.ABS_MT_POSITION_Y:
                    self.current_y = event.value
            elif event.type == ecodes.EV_KEY and event.code == ecodes.BTN_TOUCH and event.value == 1:
                self.event_queue.put(("tap", self.current_x, self.current_y))


class SerialReader(threading.Thread):
    def __init__(self, serial_port, line_queue: Queue):
        super().__init__(daemon=True)
        self.serial_port = serial_port
        self.line_queue = line_queue
        self.running = True

    def run(self):
        while self.running:
            try:
                line = self.serial_port.readline().decode("utf-8", errors="ignore").strip()
                if line:
                    self.line_queue.put(line)
            except Exception:
                time.sleep(0.05)

    def stop(self):
        self.running = False


class GameUI:
    def __init__(self, stdscr, touch_reader: TouchReader, touch_queue: Queue):
        self.stdscr = stdscr
        self.touch_reader = touch_reader
        self.touch_queue = touch_queue
        self.serial_queue = Queue()
        self.running = True
        self.status_message = "Initialisation..."
        self.feedback = []
        self.max_feedback = 12
        self.buttons = []
        self.serial_port = None
        self.serial_reader = None
        self.last_status_req = 0.0

        self.game_state = "WAIT"
        self.score = "0/10"
        self.target = "X+"
        self.pot = "0"
        self.round_ms = "0"
        self.rem_ms = "0"
        self.accel = "0,0,0"

        self._connect_serial()

    def _connect_serial(self):
        try:
            self.serial_port = serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=0.2)
            time.sleep(1.2)
            self.serial_reader = SerialReader(self.serial_port, self.serial_queue)
            self.serial_reader.start()
            self.status_message = f"Serie OK: {SERIAL_PORT}"
            self._send_command("STATUS")
        except Exception as exc:
            self.status_message = f"Erreur serie: {exc}"

    def _send_command(self, cmd: str):
        if not self.serial_port or not self.serial_port.is_open:
            self.status_message = "Port serie indisponible."
            return
        try:
            self.serial_port.write((cmd + "\n").encode())
            self.status_message = f"Commande: {cmd}"
        except Exception as exc:
            self.status_message = f"Echec envoi: {exc}"

    def _init_colors(self):
        curses.start_color()
        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_BLACK, curses.COLOR_CYAN)
        curses.init_pair(2, curses.COLOR_WHITE, curses.COLOR_BLUE)
        curses.init_pair(3, curses.COLOR_BLACK, curses.COLOR_GREEN)
        curses.init_pair(4, curses.COLOR_WHITE, curses.COLOR_RED)
        curses.init_pair(5, curses.COLOR_YELLOW, -1)

    def _build_buttons(self, h, w):
        self.buttons = []
        btn_h = 3
        btn_w = max(18, (w // 2) - 3)
        left_x = 1
        right_x = w - btn_w - 1
        start_y = max(8, h // 2 - 3)

        config = [
            ("START/RESET", left_x, start_y, 3),
            ("VALIDER", right_x, start_y, 1),
            ("STATUS", left_x, start_y + 4, 2),
            ("QUIT", right_x, start_y + 4, 4),
        ]

        for label, col, row, color in config:
            self.buttons.append(
                {
                    "label": label,
                    "row": row,
                    "col": col,
                    "height": btn_h,
                    "width": btn_w,
                    "color": color,
                    "active": False,
                }
            )

    def _draw_button(self, btn, h):
        attr = curses.color_pair(btn["color"]) | (curses.A_BOLD if btn["active"] else 0)
        for r in range(btn["row"], btn["row"] + btn["height"]):
            if 0 <= r < h:
                self.stdscr.attron(attr)
                self.stdscr.addstr(r, btn["col"], " " * btn["width"])
                self.stdscr.attroff(attr)
        label = f"[ {btn['label']} ]"
        label_row = btn["row"] + btn["height"] // 2
        label_col = btn["col"] + max(0, (btn["width"] - len(label)) // 2)
        if 0 <= label_row < h:
            self.stdscr.addstr(label_row, label_col, label)

    def _parse_status_line(self, line: str):
        if not line.startswith("state="):
            return
        parts = line.split()
        data = {}
        for part in parts:
            if "=" in part:
                k, v = part.split("=", 1)
                data[k] = v

        self.game_state = data.get("stateName", self.game_state)
        self.score = data.get("score", self.score)
        self.target = data.get("target", self.target)
        self.pot = data.get("pot", self.pot)
        self.round_ms = data.get("roundMs", self.round_ms)
        self.rem_ms = data.get("remMs", self.rem_ms)
        self.accel = data.get("accel", self.accel).strip("()")

    def _consume_serial_feedback(self):
        try:
            while True:
                line = self.serial_queue.get_nowait()
                self.feedback.append(line)
                if len(self.feedback) > self.max_feedback:
                    self.feedback.pop(0)
                self._parse_status_line(line)
        except Empty:
            pass

    def _draw(self):
        self.stdscr.erase()
        h, w = self.stdscr.getmaxyx()
        self._build_buttons(h, w)

        title = " Jeu Reflexe + Equilibre (Ecran tactile) "
        self.stdscr.attron(curses.A_BOLD)
        self.stdscr.addstr(0, max(0, (w - len(title)) // 2), title[: w - 1])
        self.stdscr.attroff(curses.A_BOLD)

        info_l1 = f"Etat: {self.game_state} | Score: {self.score} | Cible: {self.target}"
        info_l2 = f"Temps restant: {self.rem_ms} ms | Fenetre: {self.round_ms} ms | Pot: {self.pot}"
        info_l3 = f"Accel: {self.accel}"
        self.stdscr.addstr(1, 1, info_l1[: w - 2], curses.A_BOLD)
        self.stdscr.addstr(2, 1, info_l2[: w - 2])
        self.stdscr.addstr(3, 1, info_l3[: w - 2])
        self.stdscr.addstr(4, 1, "Incline selon LED cible puis touche VALIDER.", curses.A_BOLD)

        feedback_title = "--- Feedback ESP32 ---"
        self.stdscr.attron(curses.A_UNDERLINE)
        self.stdscr.addstr(6, 1, feedback_title[: w - 2])
        self.stdscr.attroff(curses.A_UNDERLINE)

        start_row = 7
        for i, line in enumerate(self.feedback[-self.max_feedback :]):
            row = start_row + i
            if row < h - 4:
                self.stdscr.addstr(row, 1, line[: w - 2])

        for btn in self.buttons:
            self._draw_button(btn, h)

        self.stdscr.attron(curses.color_pair(5))
        self.stdscr.addstr(h - 2, 1, f"Status: {self.status_message[: w - 10]}")
        self.stdscr.attroff(curses.color_pair(5))
        self.stdscr.addstr(h - 1, 1, "Touches: q quitter | s start | v valider | t status")
        self.stdscr.refresh()

    def _touch_to_row_col(self, x_raw, y_raw):
        h, w = self.stdscr.getmaxyx()
        dx = max(1, self.touch_reader.max_x - self.touch_reader.min_x)
        dy = max(1, self.touch_reader.max_y - self.touch_reader.min_y)
        x_norm = (x_raw - self.touch_reader.min_x) / dx
        y_norm = (y_raw - self.touch_reader.min_y) / dy
        col = int(x_norm * (w - 1))
        row = int(y_norm * (h - 1))
        return max(0, min(h - 1, row)), max(0, min(w - 1, col))

    def _button_at(self, row, col):
        for btn in self.buttons:
            if btn["row"] <= row < btn["row"] + btn["height"] and btn["col"] <= col < btn["col"] + btn["width"]:
                return btn
        return None

    def _set_active_button(self, label):
        for btn in self.buttons:
            btn["active"] = btn["label"] == label

    def _handle_action(self, label):
        if label == "START/RESET":
            self._send_command("START")
        elif label == "VALIDER":
            self._send_command("ACTION")
        elif label == "STATUS":
            self._send_command("STATUS")
        elif label == "QUIT":
            self.running = False
            self.status_message = "Arret demande."

    def run(self):
        self.stdscr.nodelay(True)
        curses.curs_set(0)
        self._init_colors()
        last_redraw = 0.0

        while self.running:
            now = time.time()
            self._consume_serial_feedback()

            if now - self.last_status_req > 0.35:
                self._send_command("STATUS")
                self.last_status_req = now

            if now - last_redraw >= 0.05:
                self._draw()
                last_redraw = now

            try:
                ch = self.stdscr.getch()
            except curses.error:
                ch = -1

            if ch == ord("q"):
                self.running = False
            elif ch == ord("s"):
                self._handle_action("START/RESET")
            elif ch == ord("v"):
                self._handle_action("VALIDER")
            elif ch == ord("t"):
                self._handle_action("STATUS")

            try:
                event = self.touch_queue.get_nowait()
            except Empty:
                event = None

            if event and event[0] == "tap":
                row, col = self._touch_to_row_col(event[1], event[2])
                btn = self._button_at(row, col)
                self._set_active_button(btn["label"] if btn else "")
                if btn:
                    self._handle_action(btn["label"])

            time.sleep(0.01)

        if self.serial_reader:
            self.serial_reader.stop()
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()


def main(stdscr):
    touch_queue = Queue()
    touch_reader = TouchReader(touch_queue)
    touch_reader.start()
    ui = GameUI(stdscr, touch_reader, touch_queue)
    ui.run()


if __name__ == "__main__":
    curses.wrapper(main)
