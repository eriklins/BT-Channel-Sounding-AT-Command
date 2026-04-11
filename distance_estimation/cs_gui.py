#!/usr/bin/env python3
"""
GUI front-end for the BT Channel Sounding distance estimation pipeline.

Wraps the parser and DSP from cs_distance.py with a Tkinter window:
  - left chart: time series of RTT / IFFT / Slope / Combined / Averaged
  - right chart: IFFT CIR magnitude vs distance with the selected peak marked
  - large readout of the current averaged distance
  - serial port discovery (filtered to tty.usbmodem*) + baud selection
  - live computation settings (weights, averaging window, IFFT mode, antenna
    path combination)

Run:
    python cs_gui.py
"""

import fnmatch
import os
import queue
import sys
import threading
import time
import tkinter as tk
from collections import deque
from tkinter import messagebox, ttk

import numpy as np
import serial
from serial.tools import list_ports

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

# Allow `python distance_estimation/cs_gui.py` from the project root.
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from cs_distance import (
    DistanceEstimate,
    DistanceTracker,
    IQReport,
    combine_antenna_paths,
    compute_cir,
    compute_distance,
    estimate_noise_floor,
    parse_iq_line,
)


PORT_GLOB = "*usbmodem*"
BAUD_RATES = [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600]
DEFAULT_BAUD = 115200
DEFAULT_OVERSAMPLE = 16
TIME_SERIES_MAX = 300
REDRAW_INTERVAL_S = 0.1   # cap chart redraws at ~10 Hz
QUEUE_POLL_MS = 50
MAX_PLOT_DISTANCE_M = 30.0

METRIC_LABELS_LIGHT = [
    ("rtt",      "RTT",      "tab:blue"),
    ("ifft",     "IFFT",     "tab:orange"),
    ("slope",    "Slope",    "tab:green"),
    ("combined", "Combined", "tab:red"),
    ("avg",      "Averaged", "black"),
]

METRIC_LABELS_DARK = [
    ("rtt",      "RTT",      "#6cb4ff"),
    ("ifft",     "IFFT",     "#ffb454"),
    ("slope",    "Slope",    "#7fd17f"),
    ("combined", "Combined", "#ff6b6b"),
    ("avg",      "Averaged", "#ffffff"),
]

# Chart theme palettes selected based on the detected Tk theme.
LIGHT_THEME = {
    "fig_bg":     "#ffffff",
    "axes_bg":    "#ffffff",
    "fg":         "#000000",
    "grid":       "#b0b0b0",
    "cir":        "tab:purple",
    "peak":       "red",
    "thr":        "tab:gray",
    "metrics":    METRIC_LABELS_LIGHT,
}
DARK_THEME = {
    "fig_bg":     "#2b2b2b",
    "axes_bg":    "#1e1e1e",
    "fg":         "#e0e0e0",
    "grid":       "#555555",
    "cir":        "#c48bff",
    "peak":       "#ff6b6b",
    "thr":        "#a0a0a0",
    "metrics":    METRIC_LABELS_DARK,
}


# ---------------------------------------------------------------------------
# Background serial worker
# ---------------------------------------------------------------------------


class SerialWorker(threading.Thread):
    """Reads +IQ lines from UART, runs DSP, pushes results onto a queue."""

    def __init__(self, port, baud, settings_fn, result_queue, stop_event):
        super().__init__(daemon=True)
        self.port = port
        self.baud = baud
        self._settings_fn = settings_fn
        self._q = result_queue
        self._stop = stop_event

    def run(self):
        ser = None
        try:
            try:
                ser = serial.Serial(self.port, self.baud, timeout=1)
            except serial.SerialException as exc:
                self._q.put({"type": "error", "msg": f"Open failed: {exc}"})
                return

            # Let the USB CDC line coding settle before writing. On macOS,
            # baud rates above 230400 go through IOSSIOSPEED and the first
            # write can race the VCOM baud switch, garbling bytes.
            time.sleep(0.2)

            try:
                ser.reset_input_buffer()
                ser.write(b"AT+IQ on\r\n")
                ser.flush()
            except serial.SerialException as exc:
                self._q.put({"type": "error", "msg": f"AT+IQ on failed: {exc}"})
                return

            deadline = time.monotonic() + 5.0
            ok = False
            while time.monotonic() < deadline and not self._stop.is_set():
                try:
                    raw = ser.readline()
                except serial.SerialException as exc:
                    self._q.put({"type": "error", "msg": f"Read failed: {exc}"})
                    return
                line = raw.decode("ascii", errors="replace").strip()
                if not line:
                    continue
                if line == "OK":
                    ok = True
                    break
                if line.startswith("ERROR"):
                    self._q.put({"type": "error", "msg": f"Device error: {line}"})
                    return
            if self._stop.is_set():
                return
            if not ok:
                self._q.put({"type": "error", "msg": "Timeout waiting for OK from device"})
                return

            self._q.put({"type": "connected"})

            while not self._stop.is_set():
                try:
                    raw = ser.readline()
                except serial.SerialException as exc:
                    self._q.put({"type": "error", "msg": f"Read failed: {exc}"})
                    return
                if not raw:
                    continue
                line = raw.decode("ascii", errors="replace").strip()
                if not line.startswith("+IQ:"):
                    continue
                report = parse_iq_line(line)
                if report is None:
                    continue
                s = self._settings_fn()
                est = compute_distance(
                    report,
                    oversample=s["oversample"],
                    weights=s["weights"],
                    ifft_mode=s["ifft_mode"],
                    ifft_thr_mode=s["ifft_thr_mode"],
                    ifft_thr_value=s["ifft_thr_value"],
                )
                cir = compute_cir(report, oversample=s["oversample"])
                self._q.put({
                    "type": "sample",
                    "report": report,
                    "estimate": est,
                    "cir": cir,
                })
        finally:
            if ser is not None:
                try:
                    if ser.is_open:
                        ser.write(b"AT+IQ off\r\n")
                        ser.flush()
                except Exception:
                    pass
                try:
                    ser.close()
                except Exception:
                    pass
            self._q.put({"type": "disconnected"})


# ---------------------------------------------------------------------------
# Main application
# ---------------------------------------------------------------------------


class CSApp:
    AP_ALL = -1   # synthetic key used when antenna paths are combined

    def __init__(self, root: tk.Tk):
        self.root = root
        root.title("CS Distance GUI")
        root.geometry("1500x850")

        # session state
        self.result_queue: queue.Queue = queue.Queue()
        self.stop_event = threading.Event()
        self.worker: SerialWorker | None = None
        self.var_running = False

        # DSP state
        self.tracker = DistanceTracker(window_size=5)
        self._avg_session_buf: dict[int, DistanceEstimate] = {}
        self.known_aps: list[int] = [0]
        self.sample_counters: dict[int, int] = {}

        # plot data
        self.ts_buffers: dict[int, dict[str, deque]] = {}
        self.cir_data: dict[int, tuple[np.ndarray, np.ndarray] | None] = {}
        self.peak_distance: dict[int, float | None] = {}
        self.last_avg_value: float | None = None
        self._last_redraw = 0.0

        # Tk variables
        self.var_port = tk.StringVar()
        self.var_baud = tk.StringVar(value=str(DEFAULT_BAUD))
        self.var_w_rtt = tk.StringVar(value="0.10")
        self.var_w_ifft = tk.StringVar(value="0.50")
        self.var_w_slope = tk.StringVar(value="0.40")
        self.var_w_status = tk.StringVar(value="sum: 1.00 OK")
        self.var_avg_window = tk.IntVar(value=5)
        self.var_ifft_mode = tk.StringVar(value="highest")
        self.var_thr_mode = tk.StringVar(value="rel")
        self.var_ifft_thr_rel = tk.DoubleVar(value=0.5)
        self.var_ifft_thr_nfl = tk.DoubleVar(value=10.0)
        self.var_ant_comb = tk.StringVar(value="none")
        self._weights_valid = True

        self.theme = self._detect_theme()
        self.metric_labels = self.theme["metrics"]

        self._build_ui()
        self._refresh_ports()
        self._validate_weights()
        self._rebuild_subplot_grid()
        self._poll_queue()

        root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _on_close(self):
        # Give the worker a chance to send "AT+IQ off" before the daemon
        # thread is killed on interpreter shutdown.
        worker = self.worker
        if worker is not None and worker.is_alive():
            self.stop_event.set()
            worker.join(timeout=3.0)
        self.root.destroy()

    # --------------------------------------------------------------- theme

    def _detect_theme(self):
        try:
            bg = ttk.Style(self.root).lookup("TFrame", "background") or \
                 self.root.cget("background")
            r, g, b = self.root.winfo_rgb(bg)
            luminance = (0.299 * r + 0.587 * g + 0.114 * b) / 65535.0
        except (tk.TclError, ValueError):
            luminance = 1.0
        return DARK_THEME if luminance < 0.5 else LIGHT_THEME

    # ------------------------------------------------------------------ UI

    def _build_ui(self):
        root = self.root

        top = ttk.Frame(root, padding=(8, 6))
        top.pack(side="top", fill="x")
        ttk.Label(top, text="Port:").pack(side="left")
        self.cb_port = ttk.Combobox(
            top, textvariable=self.var_port, width=30, state="readonly",
        )
        self.cb_port.pack(side="left", padx=(4, 4))
        self.btn_refresh = ttk.Button(top, text="Refresh", command=self._refresh_ports)
        self.btn_refresh.pack(side="left", padx=(0, 12))
        ttk.Label(top, text="Baud:").pack(side="left")
        self.cb_baud = ttk.Combobox(
            top, textvariable=self.var_baud,
            values=[str(b) for b in BAUD_RATES], width=8, state="readonly",
        )
        self.cb_baud.pack(side="left", padx=(4, 12))
        self.btn_start = ttk.Button(top, text="Start", command=self._toggle_start)
        self.btn_start.pack(side="left")

        main = ttk.Frame(root)
        main.pack(side="top", fill="both", expand=True)
        chart_col = ttk.Frame(main)
        chart_col.pack(side="left", fill="both", expand=True)
        side = ttk.Frame(main, padding=(8, 8))
        side.pack(side="right", fill="y")

        self.fig = Figure(
            figsize=(10, 7), dpi=100, facecolor=self.theme["fig_bg"],
        )
        self.canvas = FigureCanvasTkAgg(self.fig, master=chart_col)
        self.canvas.get_tk_widget().pack(side="top", fill="both", expand=True)
        self.axes_ts: dict = {}
        self.axes_cir: dict = {}
        self.lines_ts: dict = {}
        self.line_cir: dict = {}
        self.line_peak: dict = {}
        self.line_thr: dict = {}
        self.line_nfl: dict = {}

        self.lbl_avg = ttk.Label(
            chart_col, text="Averaged Distance: --- m",
            font=("TkDefaultFont", 32),
            anchor="center",
        )
        self.lbl_avg.pack(side="bottom", fill="x", padx=8, pady=(4, 8))

        s = ttk.Style()
        s.configure('Settings.TLabelframe.Label', font=('arial', 11, 'bold'))

        # ---- weights ----
        weights_frame = ttk.LabelFrame(side, text="Combined Weights", style="Settings.TLabelframe", padding=8)
        weights_frame.pack(fill="x", pady=(0, 8))
        for label, var in (
            ("RTT", self.var_w_rtt),
            ("IFFT", self.var_w_ifft),
            ("Slope", self.var_w_slope),
        ):
            row = ttk.Frame(weights_frame)
            row.pack(fill="x", pady=2)
            ttk.Label(row, text=label, width=6).pack(side="left")
            ttk.Spinbox(
                row, from_=0.0, to=1.0, increment=0.05,
                textvariable=var, width=8,
                command=self._validate_weights,
            ).pack(side="left")
            var.trace_add("write", lambda *_: self._validate_weights())
        self.lbl_weight_status = ttk.Label(weights_frame, textvariable=self.var_w_status)
        self.lbl_weight_status.pack(anchor="w", pady=(4, 0))

        # ---- averaging window ----
        avg_frame = ttk.LabelFrame(side, text="Averaging Window", style="Settings.TLabelframe", padding=8)
        avg_frame.pack(fill="x", pady=(0, 8))
        self.lbl_avg_window = ttk.Label(
            avg_frame, text=f"{self.var_avg_window.get()} samples",
        )
        self.lbl_avg_window.pack(anchor="w")
        self.scale_avg = ttk.Scale(
            avg_frame, from_=1, to=50, orient="horizontal",
            command=self._on_avg_window_changed,
        )
        self.scale_avg.set(self.var_avg_window.get())
        self.scale_avg.pack(fill="x")

        # ---- IFFT mode ----
        ifft_frame = ttk.LabelFrame(side, text="IFFT Mode", style="Settings.TLabelframe", padding=8)
        ifft_frame.pack(fill="x", pady=(0, 8))
        ttk.Radiobutton(
            ifft_frame, text="Highest peak", value="highest",
            variable=self.var_ifft_mode, command=self._on_ifft_mode,
        ).pack(anchor="w")
        ttk.Radiobutton(
            ifft_frame, text="Earliest peak", value="earliest",
            variable=self.var_ifft_mode, command=self._on_ifft_mode,
        ).pack(anchor="w")

        # Threshold mode selection (only meaningful for earliest peak).
        self.rb_thr_rel = ttk.Radiobutton(
            ifft_frame, text="Relative", value="rel",
            variable=self.var_thr_mode, command=self._on_thr_mode_changed,
        )
        self.rb_thr_rel.pack(anchor="w", pady=(6, 0))
        rel_row = ttk.Frame(ifft_frame)
        rel_row.pack(fill="x")
        ttk.Label(rel_row, text="Threshold:").pack(side="left")
        self.lbl_thr_rel = ttk.Label(
            rel_row, text=f"{self.var_ifft_thr_rel.get():.2f}",
        )
        self.lbl_thr_rel.pack(side="right")
        self.scale_thr_rel = ttk.Scale(
            ifft_frame, from_=0.05, to=0.95, orient="horizontal",
            command=self._on_thr_rel_changed,
        )
        self.scale_thr_rel.set(self.var_ifft_thr_rel.get())
        self.scale_thr_rel.pack(fill="x")

        self.rb_thr_nfl = ttk.Radiobutton(
            ifft_frame, text="Above noise floor", value="nfl",
            variable=self.var_thr_mode, command=self._on_thr_mode_changed,
        )
        self.rb_thr_nfl.pack(anchor="w", pady=(4, 0))
        nfl_row = ttk.Frame(ifft_frame)
        nfl_row.pack(fill="x")
        ttk.Label(nfl_row, text="Threshold:").pack(side="left")
        self.lbl_thr_nfl = ttk.Label(
            nfl_row, text=f"{self.var_ifft_thr_nfl.get():.1f} dB",
        )
        self.lbl_thr_nfl.pack(side="right")
        self.scale_thr_nfl = ttk.Scale(
            ifft_frame, from_=3.0, to=30.0, orient="horizontal",
            command=self._on_thr_nfl_changed,
        )
        self.scale_thr_nfl.set(self.var_ifft_thr_nfl.get())
        self.scale_thr_nfl.pack(fill="x")
        self._on_ifft_mode()

        # ---- antenna paths ----
        ant_frame = ttk.LabelFrame(side, text="Antenna Paths", style="Settings.TLabelframe", padding=8)
        ant_frame.pack(fill="x", pady=(0, 8))
        ttk.Radiobutton(
            ant_frame, text="None (per-AP)", value="none",
            variable=self.var_ant_comb, command=self._on_ant_comb_changed,
        ).pack(anchor="w")
        ttk.Radiobutton(
            ant_frame, text="Average", value="avg",
            variable=self.var_ant_comb, command=self._on_ant_comb_changed,
        ).pack(anchor="w")

    # ----------------------------------------------------------- ports

    def _refresh_ports(self):
        ports = sorted(
            p.device for p in list_ports.comports()
            if fnmatch.fnmatch(p.device, PORT_GLOB)
        )
        prev = self.var_port.get()
        self.cb_port["values"] = ports
        if prev in ports:
            self.var_port.set(prev)
        elif ports:
            self.var_port.set(ports[0])
        else:
            self.var_port.set("")

    # -------------------------------------------------------- settings

    def _validate_weights(self):
        try:
            w = (
                float(self.var_w_rtt.get()),
                float(self.var_w_ifft.get()),
                float(self.var_w_slope.get()),
            )
        except (ValueError, tk.TclError):
            self.var_w_status.set("invalid number")
            self.lbl_weight_status.configure(foreground="red")
            self._weights_valid = False
            self._update_start_state()
            return
        if any(x < 0.0 or x > 1.0 for x in w):
            self.var_w_status.set("each weight must be 0..1")
            self.lbl_weight_status.configure(foreground="red")
            self._weights_valid = False
        elif sum(w) > 1.0 + 1e-9:
            self.var_w_status.set(f"sum: {sum(w):.2f} > 1.0")
            self.lbl_weight_status.configure(foreground="red")
            self._weights_valid = False
        else:
            self.var_w_status.set(f"sum: {sum(w):.2f} OK")
            self.lbl_weight_status.configure(foreground="dark green")
            self._weights_valid = True
        self._update_start_state()

    def _update_start_state(self):
        if self.var_running:
            return
        self.btn_start.configure(state="normal" if self._weights_valid else "disabled")

    def _on_avg_window_changed(self, value):
        n = max(1, int(float(value)))
        if n == self.var_avg_window.get():
            return
        self.var_avg_window.set(n)
        self.lbl_avg_window.configure(text=f"{n} samples")
        self.tracker = DistanceTracker(window_size=n)
        for buf in self.ts_buffers.values():
            buf["avg"].clear()

    def _on_ifft_mode(self):
        earliest = self.var_ifft_mode.get() == "earliest"
        rb_state = "normal" if earliest else "disabled"
        self.rb_thr_rel.configure(state=rb_state)
        self.rb_thr_nfl.configure(state=rb_state)
        self._on_thr_mode_changed()

    def _on_thr_mode_changed(self):
        earliest = self.var_ifft_mode.get() == "earliest"
        rel_active = earliest and self.var_thr_mode.get() == "rel"
        nfl_active = earliest and self.var_thr_mode.get() == "nfl"
        self.scale_thr_rel.configure(state="normal" if rel_active else "disabled")
        self.scale_thr_nfl.configure(state="normal" if nfl_active else "disabled")

    def _on_thr_rel_changed(self, value):
        v = float(value)
        self.var_ifft_thr_rel.set(v)
        self.lbl_thr_rel.configure(text=f"{v:.2f}")

    def _on_thr_nfl_changed(self, value):
        v = float(value)
        self.var_ifft_thr_nfl.set(v)
        self.lbl_thr_nfl.configure(text=f"{v:.1f} dB")

    def _on_ant_comb_changed(self):
        self._avg_session_buf.clear()
        self.tracker = DistanceTracker(window_size=self.var_avg_window.get())
        if self.var_ant_comb.get() == "avg":
            self.known_aps = [self.AP_ALL]
        else:
            self.known_aps = [0]
        self.ts_buffers.clear()
        self.cir_data.clear()
        self.peak_distance.clear()
        self.sample_counters.clear()
        self.last_avg_value = None
        self.lbl_avg.configure(text="Averaged: --- m")
        self._rebuild_subplot_grid()

    def _get_settings(self):
        try:
            weights = (
                float(self.var_w_rtt.get() or 0),
                float(self.var_w_ifft.get() or 0),
                float(self.var_w_slope.get() or 0),
            )
        except ValueError:
            weights = (0.10, 0.50, 0.40)
        thr_mode = self.var_thr_mode.get()
        if thr_mode == "nfl":
            thr_value = float(self.var_ifft_thr_nfl.get())
        else:
            thr_value = float(self.var_ifft_thr_rel.get())
        return {
            "oversample": DEFAULT_OVERSAMPLE,
            "weights": weights,
            "ifft_mode": self.var_ifft_mode.get(),
            "ifft_thr_mode": thr_mode,
            "ifft_thr_value": thr_value,
            "ant_comb": self.var_ant_comb.get(),
        }

    # ------------------------------------------------------- start/stop

    def _toggle_start(self):
        if self.var_running:
            self._stop_session()
        else:
            self._start_session()

    def _start_session(self):
        port = self.var_port.get()
        if not port:
            messagebox.showerror("No port", "No serial port selected.")
            return
        try:
            baud = int(self.var_baud.get())
        except ValueError:
            messagebox.showerror("Bad baud", "Invalid baud rate.")
            return
        if not self._weights_valid:
            messagebox.showerror("Weights", "Combined weights are invalid.")
            return

        self.stop_event = threading.Event()
        self.result_queue = queue.Queue()
        self.worker = SerialWorker(
            port, baud, self._get_settings, self.result_queue, self.stop_event,
        )
        self.worker.start()
        self.var_running = True
        self.btn_start.configure(text="Stop")
        self.cb_port.configure(state="disabled")
        self.cb_baud.configure(state="disabled")
        self.btn_refresh.configure(state="disabled")

    def _stop_session(self):
        if self.worker is not None:
            self.stop_event.set()
        self.btn_start.configure(text="Stopping...", state="disabled")

    def _on_disconnected(self):
        self.var_running = False
        self.worker = None
        self.btn_start.configure(text="Start", state="normal")
        self.cb_port.configure(state="readonly")
        self.cb_baud.configure(state="readonly")
        self.btn_refresh.configure(state="normal")
        self._update_start_state()

    # ---------------------------------------------------------- queue

    def _poll_queue(self):
        try:
            while True:
                item = self.result_queue.get_nowait()
                self._handle_item(item)
        except queue.Empty:
            pass
        self._maybe_redraw()
        self.root.after(QUEUE_POLL_MS, self._poll_queue)

    def _handle_item(self, item):
        t = item.get("type")
        if t == "error":
            messagebox.showerror("Serial error", item["msg"])
            return
        if t == "disconnected":
            self._on_disconnected()
            return
        if t != "sample":
            return

        report: IQReport = item["report"]
        est: DistanceEstimate = item["estimate"]
        cir = item["cir"]

        if self.var_ant_comb.get() == "avg":
            # AP 0 with a non-empty buffer marks the start of a new round —
            # flush the previous round before buffering the new sample. The
            # firmware reuses the same session id across rounds so we key off
            # the AP index, matching cs_distance.py's CLI behaviour.
            if report.ap == 0 and self._avg_session_buf:
                combined_est = combine_antenna_paths(
                    list(self._avg_session_buf.values()), "avg",
                )
                self._record_sample(self.AP_ALL, combined_est, None)
                self._avg_session_buf.clear()
            self._avg_session_buf[report.ap] = est
            # Always show the latest report's CIR with its own peak.
            if cir is not None:
                self.cir_data[self.AP_ALL] = cir
                self.peak_distance[self.AP_ALL] = est.ifft_m
            return

        # ant_comb == "none": one row per antenna path
        self._record_sample(report.ap, est, cir)

    def _record_sample(self, ap, est, cir):
        if ap not in self.known_aps:
            self.known_aps = sorted(set(self.known_aps + [ap]))
            if self.var_ant_comb.get() == "none":
                self._rebuild_subplot_grid()

        if est.combined_m is not None:
            avg = self.tracker.update(0, ap, est.combined_m)
        else:
            avg = self.tracker.average(0, ap)

        if avg == avg:  # not NaN
            self.last_avg_value = avg
            avg_value: float | None = avg
        else:
            avg_value = None

        idx = self.sample_counters.get(ap, 0)
        self.sample_counters[ap] = idx + 1
        buffers = self.ts_buffers.setdefault(ap, {
            key: deque(maxlen=TIME_SERIES_MAX) for key, _, _ in self.metric_labels
        })
        values = {
            "rtt": est.rtt_m,
            "ifft": est.ifft_m,
            "slope": est.phase_slope_m,
            "combined": est.combined_m,
            "avg": avg_value,
        }
        for key, val in values.items():
            buffers[key].append((idx, val))

        if cir is not None:
            self.cir_data[ap] = cir
            self.peak_distance[ap] = est.ifft_m

    # ----------------------------------------------------------- plots

    def _style_axes(self, ax):
        fg = self.theme["fg"]
        ax.set_facecolor(self.theme["axes_bg"])
        ax.tick_params(colors=fg, which="both")
        for spine in ax.spines.values():
            spine.set_color(fg)
        ax.xaxis.label.set_color(fg)
        ax.yaxis.label.set_color(fg)
        ax.title.set_color(fg)
        ax.grid(True, alpha=0.3, color=self.theme["grid"])
        leg = ax.get_legend()
        if leg is not None:
            frame = leg.get_frame()
            frame.set_facecolor(self.theme["axes_bg"])
            frame.set_edgecolor(self.theme["grid"])
            for text in leg.get_texts():
                text.set_color(fg)

    def _rebuild_subplot_grid(self):
        self.fig.clf()
        self.axes_ts.clear()
        self.axes_cir.clear()
        self.lines_ts.clear()
        self.line_cir.clear()
        self.line_peak.clear()
        self.line_thr.clear()
        self.line_nfl.clear()

        aps = self.known_aps if self.known_aps else [0]
        n = len(aps)
        for row, ap in enumerate(aps):
            ax_ts = self.fig.add_subplot(n, 2, row * 2 + 1)
            ax_cir = self.fig.add_subplot(n, 2, row * 2 + 2)
            label = "combined" if ap == self.AP_ALL else f"AP {ap}"

            ax_ts.set_title(f"Distance estimates ({label})")
            ax_ts.set_xlabel("sample")
            ax_ts.set_ylabel("Distance (m)")
            ax_ts.grid(True, alpha=0.3)
            line_dict = {}
            for key, lbl, color in self.metric_labels:
                lw = 2.8 if key == "avg" else 1.4
                (ln,) = ax_ts.plot([], [], label=lbl, color=color, linewidth=lw)
                line_dict[key] = ln
            ax_ts.legend(loc="upper right", fontsize=8)
            self._style_axes(ax_ts)
            self.axes_ts[ap] = ax_ts
            self.lines_ts[ap] = line_dict

            ax_cir.set_title(f"IFFT CIR ({label})")
            ax_cir.set_xlabel("Distance (m)")
            ax_cir.set_ylabel("|CIR|")
            ax_cir.set_xlim(0, MAX_PLOT_DISTANCE_M)
            ax_cir.grid(True, alpha=0.3)
            (cl,) = ax_cir.plot(
                [], [], color=self.theme["cir"], linewidth=1.4, label="CIR",
            )
            (pk,) = ax_cir.plot(
                [], [], color=self.theme["peak"], linewidth=1.0,
                linestyle="--", label="selected peak",
            )
            (thr,) = ax_cir.plot(
                [], [], color=self.theme["thr"], linewidth=1.0,
                linestyle="--", label="threshold",
            )
            (nfl,) = ax_cir.plot(
                [], [], color=self.theme["thr"], linewidth=1.0,
                linestyle=":", label="noise floor",
            )
            ax_cir.legend(loc="upper right", fontsize=8)
            self._style_axes(ax_cir)
            self.axes_cir[ap] = ax_cir
            self.line_cir[ap] = cl
            self.line_peak[ap] = pk
            self.line_thr[ap] = thr
            self.line_nfl[ap] = nfl

        self.fig.tight_layout()
        self.canvas.draw_idle()

    def _maybe_redraw(self):
        now = time.monotonic()
        if now - self._last_redraw < REDRAW_INTERVAL_S:
            return
        self._last_redraw = now

        if self.last_avg_value is not None:
            self.lbl_avg.configure(text=f"Averaged: {self.last_avg_value:.2f} m")

        for ap, buffers in self.ts_buffers.items():
            if ap not in self.axes_ts:
                continue
            ax = self.axes_ts[ap]
            ymin, ymax = float("inf"), float("-inf")
            for key, _, _ in self.metric_labels:
                pts = [(i, v) for i, v in buffers[key] if v is not None]
                if pts:
                    xs, ys = zip(*pts)
                    self.lines_ts[ap][key].set_data(xs, ys)
                    ymin = min(ymin, min(ys))
                    ymax = max(ymax, max(ys))
                else:
                    self.lines_ts[ap][key].set_data([], [])
            counter = self.sample_counters.get(ap, 0)
            x0 = max(0, counter - TIME_SERIES_MAX)
            ax.set_xlim(x0, max(counter, x0 + 10))
            if ymin < ymax:
                margin = max(0.5, 0.1 * (ymax - ymin))
                ax.set_ylim(
                    max(0.0, ymin - margin),
                    min(MAX_PLOT_DISTANCE_M, ymax + margin),
                )

        for ap, cir in self.cir_data.items():
            if ap not in self.line_cir or cir is None:
                continue
            distances, mag = cir
            self.line_cir[ap].set_data(distances, mag)
            ax = self.axes_cir[ap]
            valid = distances <= MAX_PLOT_DISTANCE_M
            mvalid = mag[valid]
            if mvalid.size:
                ax.set_ylim(0, mvalid.max() * 1.1 + 1e-9)
            peak = self.peak_distance.get(ap)
            if peak is not None and 0 <= peak <= MAX_PLOT_DISTANCE_M:
                top = mvalid.max() if mvalid.size else 1.0
                self.line_peak[ap].set_data([peak, peak], [0, top])
            else:
                self.line_peak[ap].set_data([], [])

            if self.var_ifft_mode.get() == "earliest" and mag.size:
                if self.var_thr_mode.get() == "nfl":
                    nfl_level = estimate_noise_floor(mag)
                    thr_db = float(self.var_ifft_thr_nfl.get())
                    thr_level = nfl_level * 10.0 ** (thr_db / 20.0)
                    self.line_nfl[ap].set_data(
                        [0, MAX_PLOT_DISTANCE_M], [nfl_level, nfl_level],
                    )
                else:
                    thr_level = float(self.var_ifft_thr_rel.get()) * float(mag.max())
                    self.line_nfl[ap].set_data([], [])
                self.line_thr[ap].set_data(
                    [0, MAX_PLOT_DISTANCE_M], [thr_level, thr_level],
                )
            else:
                self.line_thr[ap].set_data([], [])
                self.line_nfl[ap].set_data([], [])

        self.canvas.draw_idle()


def main():
    root = tk.Tk()
    CSApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
