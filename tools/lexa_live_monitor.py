#!/usr/bin/env python3
"""
Moniteur graphique LexaCare : série USB (débit max), télémétrie **LXJS** + JSON (schéma v3),
éventuellement audio/lidar binaires (LXCS / LXCA / LXCL) pour anciens firmwares ou autres builds.

Usage :
  pip install -r tools/requirements_lexa_gui.txt
  python3 tools/lexa_live_monitor.py --config tools/lexa_live_config.example.json

Firmware **production_usb_telemetry** (MBH_USB_TELEMETRY_STREAM) : flux unique **LXJS**
+ JSON v3 (capteurs, `audio_left` / `audio_right`, `lidar_matrix` en mm uint16, `thermal_image`).
Pas d’émission LXCS/LXCL sur ce mode.

Décodage LXCS / LXCA / LXCL : conservé pour compatibilité (capture host, anciennes images).

Les lignes ASCII « FRAME:… » (8×32 mm, firmware production_tof_frame_ascii) sont
détectées automatiquement sur le flux série (même logique que read_lexa_tof_frame.py).
Ne pas mélanger sur un même port : soit LXJS (+ binaire optionnel) à débit élevé, soit surtout
FRAME: + logs (115200) — le démultiplexeur prend l’événement le plus tôt dans le buffer.

Bouton « Déconnecter » : ferme le port série (libère pour reprogrammer l’ESP).
"""
from __future__ import annotations

import argparse
import json
import queue
import struct
import threading
import time
import tkinter as tk
from collections import deque
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from typing import Any

import numpy as np

import matplotlib

matplotlib.use("TkAgg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
from matplotlib.axes import Axes

# Palette « console clinique » — lisible longue durée, contraste WCAG-friendly
_UI = {
    "bg": "#14161a",
    "surface": "#1c2028",
    "surface2": "#252b35",
    "border": "#2d3542",
    "text": "#e6e9ef",
    "muted": "#8b93a7",
    "accent": "#3d8bfd",
    "accent_dim": "#2563c4",
    "success": "#34d399",
    "warn": "#fbbf24",
}

try:
    import serial
except ImportError as e:
    raise SystemExit("Installe les dépendances : pip install -r tools/requirements_lexa_gui.txt") from e

MAGIC_AUDIO_ST = b"LXCS"
MAGIC_AUDIO_MO = b"LXCA"
MAGIC_LIDAR = b"LXCL"
MAGIC_JSON_TELEM = b"LXJS"
HDR_AUDIO = 12
HDR_LIDAR = 16
SAMPLE_RATE = 16000

LIDAR_ROWS = 8
LIDAR_COLS = 32
# Aligné sur task_usb_telemetry.c (PCM_MAX_PAIRS) : ns = paires L/R sur le fil USB.
PCM_MAX_PAIRS_USB = 512
LIDAR_CELLS = LIDAR_ROWS * LIDAR_COLS
FRAME_PREFIX = b"FRAME:"
# Limite de sécurité pour une ligne FRAME: (256 entiers + séparateurs)
FRAME_LINE_MAX = 16384
LXJS_JSON_MAX_BYTES = 256 * 1024


def load_config(path: Path) -> dict[str, Any]:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _json_object_starts_at(buf: bytearray, i: int) -> bool:
    """True si à l’index i on a bien « { » puis espaces puis « " » (objet JSON typique, ex. {"uptime_ms":…})."""
    if i < 0 or i >= len(buf) or buf[i] != ord("{"):
        return False
    k = i + 1
    while k < len(buf) and buf[k] in (9, 10, 13, 32):
        k += 1
    return k < len(buf) and buf[k] == ord('"')


def find_json_start(buf: bytearray) -> int:
    """Premier candidat JSON plausible (évite les 0x7B dans LXCL/LXCS binaires)."""
    limit = min(len(buf), 8192)
    for i in range(limit):
        if buf[i] == ord("{") and _json_object_starts_at(buf, i):
            return i
    return -1


def find_complete_frame_line(buf: bytearray) -> tuple[int, int] | None:
    """Première ligne complète « FRAME:…\\n » : retourne (début, fin_exclusive) ou None."""
    search = 0
    while search < len(buf):
        i = buf.find(FRAME_PREFIX, search)
        if i < 0:
            return None
        nl = buf.find(b"\n", i)
        if nl < 0:
            return None
        if nl - i > FRAME_LINE_MAX:
            search = i + len(FRAME_PREFIX)
            continue
        return (i, nl + 1)
    return None


def parse_frame_line_to_event(raw_line: bytes) -> tuple[str, dict[str, Any]] | None:
    """Parse une ligne FRAME:… (mm) → événement lidar unifié (z_mm float32, forme 8×32)."""
    line = raw_line.decode("ascii", errors="replace").strip()
    if not line.startswith("FRAME:"):
        return None
    payload = line[len("FRAME:") :].strip()
    if not payload:
        return None
    try:
        values = [int(x) for x in payload.split(",")]
    except ValueError:
        return None
    if len(values) != LIDAR_CELLS:
        return None
    z_mm = np.asarray(values, dtype=np.float32).reshape(LIDAR_ROWS, LIDAR_COLS)
    return ("lidar", {"mode": "ascii_mm", "z_mm": z_mm})


def _scan_balanced_json_object_end(buf: bytearray, start: int, max_span: int) -> int | None:
    """Depuis buf[start] == « { », retourne l’index exclusif juste après le « } » racine, ou None."""
    if start < 0 or start >= len(buf) or buf[start] != ord("{"):
        return None
    depth = 0
    in_str = False
    esc = False
    end_limit = min(len(buf), start + max_span)
    i = start
    while i < end_limit:
        c = buf[i]
        if in_str:
            if esc:
                esc = False
            elif c == ord("\\"):
                esc = True
            elif c == ord('"'):
                in_str = False
        else:
            if c == ord('"'):
                in_str = True
            elif c == ord("{"):
                depth += 1
            elif c == ord("}"):
                depth -= 1
                if depth == 0:
                    return i + 1
        i += 1
    return None


def try_pop_lxjs_json(buf: bytearray) -> tuple[dict[str, Any] | None, int]:
    """« LXJS » + objet JSON (une ligne ou multi-lignes) ; accolades équilibrées.

    Retourne (obj, n_consommé). Si le JSON est invalide mais l’objet est terminé,
    n_consommé > 0 et obj vaut None (on avance pour resynchroniser).
    """
    if len(buf) < 4 or not buf.startswith(MAGIC_JSON_TELEM):
        return None, 0
    j = 4
    while j < len(buf) and buf[j] in (9, 10, 13, 32):
        j += 1
    if j >= len(buf) or buf[j] != ord("{"):
        return None, 0
    obj_end = _scan_balanced_json_object_end(buf, j, LXJS_JSON_MAX_BYTES)
    if obj_end is None:
        if len(buf) > j + LXJS_JSON_MAX_BYTES:
            return None, 4
        return None, 0
    raw = bytes(buf[j:obj_end])
    n = obj_end
    while n < len(buf) and buf[n] in (9, 10, 13, 32):
        n += 1
    try:
        text = raw.decode("utf-8")
        obj = json.loads(text)
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None, n
    if isinstance(obj, dict):
        return obj, n
    return None, n


def _lxcs_payload_byte_len(buf: bytearray, ns: int) -> int | None:
    """Taille du bloc PCM après l’en-tête 12 o pour LXCS/LXCA.

    - Firmware **usb_telemetry** : `ns` = nombre de **paires** stéréo (≤512), payload = ns×4 o.
    - **capture_audio_host** : `ns` = nombre d’**int16** sur le fil, payload = ns×2 o.

    Si `ns ≤ 512` et le buffer n’a pas encore ns×4+12 o, on retourne None (attendre la trame
    complète) au lieu de lire une demi-trame — évite de désynchroniser le flux.
    """
    if ns == 0 or ns > 65536:
        return None
    bl = len(buf)
    need_usb = HDR_AUDIO + ns * 4
    need_i16 = HDR_AUDIO + ns * 2
    if ns <= PCM_MAX_PAIRS_USB:
        if bl >= need_usb:
            return ns * 4
        if bl < need_usb:
            return None
    if bl >= need_i16:
        return ns * 2
    return None


def try_pop_json(buf: bytearray) -> tuple[dict[str, Any] | None, int]:
    """Si le buffer commence (après espaces) par un objet JSON, le parse et retourne (obj, taille)."""
    i = 0
    while i < len(buf) and buf[i] in (9, 10, 13, 32):
        i += 1
    if i >= len(buf) or not _json_object_starts_at(buf, i):
        return None, 0
    depth = 0
    for j in range(i, min(len(buf), i + 16384)):
        c = buf[j]
        if c == ord("{"):
            depth += 1
        elif c == ord("}"):
            depth -= 1
            if depth == 0:
                raw = bytes(buf[i : j + 1])
                try:
                    text = raw.decode("utf-8")
                except UnicodeDecodeError:
                    return None, 0
                try:
                    obj = json.loads(text)
                except json.JSONDecodeError:
                    return None, 0
                if isinstance(obj, dict):
                    return obj, j + 1
                return None, 0
    return None, 0


def pop_one_event(buf: bytearray) -> tuple[str, Any] | None:
    """Extrait le prochain événement (binaire LX*, LXJS+JSON, JSON nu hérité, ou FRAME: mm)."""
    frame_span = find_complete_frame_line(buf)
    lxjs_i = buf.find(MAGIC_JSON_TELEM)
    json_i = -1
    if lxjs_i < 0:
        json_i = find_json_start(buf)
    pos_st = buf.find(MAGIC_AUDIO_ST)
    pos_mo = buf.find(MAGIC_AUDIO_MO)
    pos_ld = buf.find(MAGIC_LIDAR)

    positions: list[int] = []
    if frame_span is not None:
        positions.append(frame_span[0])
    if lxjs_i >= 0:
        positions.append(lxjs_i)
    if json_i >= 0:
        positions.append(json_i)
    if pos_st >= 0:
        positions.append(pos_st)
    if pos_mo >= 0:
        positions.append(pos_mo)
    if pos_ld >= 0:
        positions.append(pos_ld)

    if not positions:
        if len(buf) > 262144:
            del buf[: len(buf) // 2]
        return None

    pos0 = min(positions)
    del buf[:pos0]

    if buf.startswith(FRAME_PREFIX):
        nl = buf.find(b"\n")
        if nl < 0:
            return None
        raw_line = bytes(buf[: nl + 1])
        del buf[: nl + 1]
        return parse_frame_line_to_event(raw_line)

    js_lx, nlx = try_pop_lxjs_json(buf)
    if nlx > 0:
        del buf[:nlx]
        if js_lx is not None:
            return ("json", js_lx)
        return None

    js, njs = try_pop_json(buf)
    if njs > 0:
        del buf[:njs]
        return ("json", js)
    if len(buf) > 6000 and buf[0] == ord("{"):
        del buf[0]
        return None

    cands: list[tuple[int, bytes]] = []
    if buf.find(MAGIC_AUDIO_ST) == 0:
        cands.append((0, MAGIC_AUDIO_ST))
    if buf.find(MAGIC_AUDIO_MO) == 0:
        cands.append((0, MAGIC_AUDIO_MO))
    if buf.find(MAGIC_LIDAR) == 0:
        cands.append((0, MAGIC_LIDAR))
    if not cands:
        if len(buf) > 262144:
            del buf[: len(buf) // 2]
        elif len(buf) > 0:
            del buf[:1]
        return None
    _pos, magic = cands[0]

    if magic in (MAGIC_AUDIO_ST, MAGIC_AUDIO_MO):
        if len(buf) < HDR_AUDIO:
            return None
        _seq, ns = struct.unpack_from("<II", buf, 4)
        if magic == MAGIC_AUDIO_MO:
            if ns == 0 or ns > 65536 or len(buf) < HDR_AUDIO + ns * 2:
                del buf[:1]
                return None
            pay = ns * 2
        else:
            pay = _lxcs_payload_byte_len(buf, ns)
            if pay is None:
                return None
        pcm = bytes(buf[HDR_AUDIO : HDR_AUDIO + pay])
        del buf[: HDR_AUDIO + pay]
        # LXCS : stéréo si payload = multiple de 4 o (L,R int16) — usb ou capture host.
        is_lr = magic == MAGIC_AUDIO_ST and len(pcm) >= 4 and (len(pcm) % 4 == 0)
        return ("audio", {"stereo": is_lr, "pcm": pcm})

    if magic == MAGIC_LIDAR:
        if len(buf) < HDR_LIDAR:
            return None
        _seq, w, h = struct.unpack_from("<Iii", buf, 4)
        w, h = int(w), int(h)
        if w != LIDAR_COLS or h != LIDAR_ROWS:
            del buf[:1]
            return None
        nb = w * h * 4
        if len(buf) < HDR_LIDAR + nb:
            return None
        f32 = bytes(buf[HDR_LIDAR : HDR_LIDAR + nb])
        del buf[: HDR_LIDAR + nb]
        return ("lidar", {"w": w, "h": h, "f32": f32})

    return None


def serial_reader(
    ser_holder: list[serial.Serial | None],
    cfg: dict[str, Any],
    q: queue.Queue,
    stop: threading.Event,
) -> None:
    sc = cfg.get("serial", {})
    try:
        chunk = int(sc.get("read_chunk") or 32768)
    except (TypeError, ValueError):
        chunk = 32768
    chunk = max(256, min(chunk, 1_048_576))
    buf = bytearray()
    while not stop.is_set():
        ser = ser_holder[0]
        if ser is None:
            time.sleep(0.05)
            continue
        try:
            data = ser.read(chunk)
        except (serial.SerialException, OSError):
            q.put(("error", "Lecture série interrompue"))
            ser_holder[0] = None
            continue
        if not data:
            continue
        buf.extend(data)
        for _ in range(256):
            ev = pop_one_event(buf)
            if ev is None:
                break
            try:
                q.put_nowait(ev)
            except queue.Full:
                try:
                    q.get_nowait()
                except queue.Empty:
                    pass
                try:
                    q.put_nowait(ev)
                except queue.Full:
                    pass


class LexaLiveApp(tk.Tk):
    def __init__(self, config_path: Path) -> None:
        super().__init__()
        self.title("LexaCare — Surveillance temps réel")
        self.minsize(1080, 720)
        self.cfg = load_config(config_path)
        self.config_path = config_path
        self.ser_holder: list[serial.Serial | None] = [None]
        self.stop = threading.Event()
        self.q: queue.Queue = queue.Queue(maxsize=500)
        self.reader: threading.Thread | None = None

        bf = self.cfg.get("buffers", {})
        self.audio_max = int(bf.get("audio_waveform_samples", 8000))
        self.curve_max = int(bf.get("sensor_curve_points", 400))
        self.lidar_ms = int(bf.get("lidar_fps_hint_ms", 80))
        self.lidar_mm_max = float(bf.get("lidar_mm_max", 4000))

        self.buf_l = deque(maxlen=self.audio_max)
        self.buf_r = deque(maxlen=self.audio_max)
        self.curves: dict[str, deque] = {}
        self.curve_t = deque(maxlen=self.curve_max)
        for c in self.cfg.get("sensor_curves", []):
            k = c.get("json_key")
            if k:
                self.curves[k] = deque(maxlen=self.curve_max)

        self._vision_fall = tk.DoubleVar(value=0.0)
        self._audio_fall = tk.DoubleVar(value=0.0)
        self._field_labels: dict[str, ttk.Label] = {}
        self._sensor_leds: dict[str, tuple[tk.Canvas, int]] = {}
        self._ax_sens_list: list[Axes] = []
        self._cv_sens: FigureCanvasTkAgg | None = None
        self._lidar_frame_count = 0
        self._lidar_last_source = "—"

        plt.style.use("dark_background")
        plt.rcParams.update(
            {
                "figure.facecolor": _UI["bg"],
                "axes.facecolor": _UI["surface2"],
                "axes.edgecolor": _UI["border"],
                "axes.labelcolor": _UI["muted"],
                "text.color": _UI["text"],
                "xtick.color": _UI["muted"],
                "ytick.color": _UI["muted"],
                "grid.color": _UI["border"],
                "grid.alpha": 0.35,
                "font.family": "sans-serif",
                "font.sans-serif": ["DejaVu Sans", "Segoe UI", "Liberation Sans", "Arial"],
            }
        )

        self._setup_theme()
        self._build_ui()

        self.after(40, self._drain_queue)
        self._last_lidar_draw = 0.0
        self._lidar_pending = np.zeros((LIDAR_ROWS, LIDAR_COLS), dtype=np.float32)

    def _setup_theme(self) -> None:
        self.configure(background=_UI["bg"])
        st = ttk.Style()
        try:
            st.theme_use("clam")
        except tk.TclError:
            pass
        st.configure(".", background=_UI["bg"], foreground=_UI["text"], fieldbackground=_UI["surface2"])
        st.configure("TFrame", background=_UI["bg"])
        st.configure("TLabel", background=_UI["bg"], foreground=_UI["text"])
        st.configure("Title.TLabel", font=("", 16, "bold"), foreground=_UI["text"], background=_UI["surface"])
        st.configure("Sub.TLabel", font=("", 10), foreground=_UI["muted"], background=_UI["surface"])
        st.configure("TLabelframe", background=_UI["surface"], foreground=_UI["accent"])
        st.configure("TLabelframe.Label", background=_UI["surface"], foreground=_UI["accent"])
        st.configure("TButton", padding=(14, 8))
        st.map(
            "TButton",
            background=[("active", _UI["accent_dim"]), ("!disabled", _UI["accent"])],
            foreground=[("!disabled", "#ffffff")],
        )
        st.configure("Card.TFrame", background=_UI["surface"], relief="flat")
        st.configure("Status.TLabel", background=_UI["surface2"], foreground=_UI["muted"], font=("", 9))
        st.configure("OnCard.TLabel", background=_UI["surface"], foreground=_UI["text"])
        st.configure("OnCardMuted.TLabel", background=_UI["surface"], foreground=_UI["muted"])
        st.configure(
            "Horizontal.TProgressbar",
            troughcolor=_UI["surface2"],
            background=_UI["accent"],
            lightcolor=_UI["accent"],
            darkcolor=_UI["accent_dim"],
        )

    def _build_right_sensor_column(self, parent: ttk.Frame) -> None:
        """Colonne droite : voyants conn_* (vertical) + un subplot par entrée sensor_curves."""
        default_leds: list[dict[str, str]] = [
            {"json_key": "conn_ds3231", "label": "DS3231"},
            {"json_key": "conn_cat24", "label": "EEPROM"},
            {"json_key": "conn_bme280", "label": "BME280"},
            {"json_key": "conn_hdc1080", "label": "HDC1080"},
            {"json_key": "conn_tmp117", "label": "TMP117"},
            {"json_key": "conn_vl53l0", "label": "VL53L0"},
            {"json_key": "conn_mlx90640", "label": "MLX90640"},
        ]
        leds_cfg = self.cfg.get("sensor_conn_leds")
        leds: list[dict[str, str]] = leds_cfg if isinstance(leds_cfg, list) and leds_cfg else default_leds

        outer = ttk.LabelFrame(parent, text=" Capteurs (JSON) ", padding=(8, 10))
        outer.pack(fill=tk.BOTH, expand=True)

        led_fr = ttk.Frame(outer, style="Card.TFrame")
        led_fr.pack(fill=tk.X, pady=(0, 8))
        for item in leds:
            key = str(item.get("json_key", "")).strip()
            if not key:
                continue
            row = ttk.Frame(led_fr, style="Card.TFrame")
            row.pack(side=tk.TOP, anchor=tk.W, pady=3, fill=tk.X)
            cv = tk.Canvas(
                row,
                width=26,
                height=22,
                highlightthickness=0,
                bg=_UI["surface"],
                bd=0,
            )
            cv.pack(side=tk.LEFT)
            oid = cv.create_oval(5, 4, 21, 18, fill=_UI["warn"], outline=_UI["border"], width=1)
            self._sensor_leds[key] = (cv, oid)
            ttk.Label(
                row,
                text=str(item.get("label", key)),
                style="OnCardMuted.TLabel",
                font=("", 9),
            ).pack(side=tk.LEFT, padx=(8, 0))

        curves_cfg = self.cfg.get("sensor_curves", [])
        n_ax = max(1, len(curves_cfg))
        h_in = max(2.2, 0.72 * n_ax)
        self.fig_sens = Figure(figsize=(3.8, h_in), dpi=100)
        self.fig_sens.patch.set_facecolor(_UI["bg"])
        self._ax_sens_list = []
        for i in range(n_ax):
            ax = self.fig_sens.add_subplot(n_ax, 1, i + 1)
            ax.set_facecolor(_UI["surface2"])
            ax.grid(True, alpha=0.25)
            ax.tick_params(labelsize=7)
            if i < n_ax - 1:
                ax.set_xticklabels([])
            self._ax_sens_list.append(ax)
        if self._ax_sens_list:
            self._ax_sens_list[-1].set_xlabel("t - t0 (ms)", fontsize=8, color=_UI["muted"])
        self.fig_sens.subplots_adjust(left=0.28, right=0.96, hspace=0.35)
        self._cv_sens = FigureCanvasTkAgg(self.fig_sens, master=outer)
        self._cv_sens.get_tk_widget().pack(fill=tk.BOTH, expand=True)

    def _build_ui(self) -> None:
        header = ttk.Frame(self, padding=(16, 12, 16, 8), style="Card.TFrame")
        header.pack(fill=tk.X)
        ttk.Label(header, text="LexaCare", style="Title.TLabel").pack(anchor=tk.W)
        ttk.Label(
            header,
            text="Flux série — LXJS JSON v3 (audio / lidar / capteurs), LXCS·LXCL·FRAME: si présents",
            style="Sub.TLabel",
        ).pack(anchor=tk.W, pady=(2, 0))

        bar = ttk.Frame(self, padding=(16, 8))
        bar.pack(fill=tk.X)
        ttk.Button(bar, text="Connexion", command=self._connect).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(bar, text="Déconnecter", command=self._disconnect).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Button(bar, text="Charger config JSON…", command=self._pick_config).pack(side=tk.LEFT, padx=(0, 8))
        self.status = ttk.Label(bar, text="● Hors ligne", foreground=_UI["warn"])
        self.status.pack(side=tk.LEFT, padx=(24, 0))

        body = ttk.Frame(self)
        body.pack(fill=tk.BOTH, expand=True)
        body.grid_columnconfigure(0, weight=4, uniform="lexa_cols")
        body.grid_columnconfigure(1, weight=1, minsize=200, uniform="lexa_cols")
        body.grid_rowconfigure(0, weight=1)

        left = ttk.Frame(body, padding=(12, 4, 8, 8))
        left.grid(row=0, column=0, sticky="nsew")

        right = ttk.Frame(body, padding=(4, 4, 16, 8))
        right.grid(row=0, column=1, sticky="nsew")
        self._build_right_sensor_column(right)

        main = left
        tof_card = ttk.LabelFrame(main, text=" Temps de vol (ToF) ", padding=12)
        tof_card.pack(fill=tk.BOTH, expand=True, pady=(0, 12))

        row0 = ttk.Frame(tof_card)
        row0.pack(fill=tk.BOTH, expand=True)
        self.fig_lidar = Figure(figsize=(10.5, 3.2), dpi=100)
        self.fig_lidar.patch.set_facecolor(_UI["bg"])
        self.ax_lidar = self.fig_lidar.add_subplot(111)
        self._im = self.ax_lidar.imshow(
            np.zeros((LIDAR_ROWS, LIDAR_COLS)),
            vmin=0,
            vmax=self.lidar_mm_max,
            cmap="turbo",
            aspect="equal",
            interpolation="nearest",
        )
        self.ax_lidar.set_title("Carte de distance agrégée — 8 × 32 cellules (mm)")
        self.ax_lidar.set_xlabel("Axe horizontal — 4 capteurs VL53L8CX (ordre #3 · #4 · #2 · #1)")
        self.ax_lidar.set_ylabel("Rangée")
        for xv in (7.5, 15.5, 23.5):
            self.ax_lidar.axvline(xv, color="#ffffff", linewidth=0.6, alpha=0.35)
        cbar = self.fig_lidar.colorbar(self._im, ax=self.ax_lidar, fraction=0.046, pad=0.02)
        cbar.set_label("Distance (mm)")
        cv_ld = FigureCanvasTkAgg(self.fig_lidar, master=row0)
        cv_ld.get_tk_widget().pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        side_ld = ttk.Frame(row0, width=200)
        side_ld.pack(side=tk.LEFT, fill=tk.Y, padx=(16, 0))
        self._lidar_stats = ttk.Label(
            side_ld,
            text="En attente de trames ToF…\n\nSource : —\nTrames : 0",
            justify=tk.LEFT,
            wraplength=190,
            style="OnCard.TLabel",
        )
        self._lidar_stats.pack(anchor=tk.NW, pady=(0, 16))

        tel = self.cfg.get("telemetry", {})
        vcfg = next((v for v in tel.values() if v.get("widget") == "progress_lidar"), None)
        ttk.Label(
            side_ld,
            text=(vcfg or {}).get("label", "Indice chute — vision (%)"),
            style="OnCardMuted.TLabel",
        ).pack(anchor=tk.W)
        self.pb_vision = ttk.Progressbar(
            side_ld, orient=tk.HORIZONTAL, length=200, maximum=100, mode="determinate"
        )
        self.pb_vision.pack(fill=tk.X, pady=(4, 12))

        row1 = ttk.LabelFrame(main, text=" Audio stéréo ", padding=8)
        row1.pack(fill=tk.BOTH, expand=True, pady=(0, 10))
        au_inner = ttk.Frame(row1)
        au_inner.pack(fill=tk.BOTH, expand=True)

        self.fig_ml = Figure(figsize=(4.5, 2.4), dpi=100)
        self.fig_ml.patch.set_facecolor(_UI["bg"])
        self.ax_ml = self.fig_ml.add_subplot(111)
        self.ax_ml.set_title("Canal gauche")
        self.ax_ml.grid(True, alpha=0.25)
        FigureCanvasTkAgg(self.fig_ml, master=au_inner).get_tk_widget().pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        side_au = ttk.Frame(au_inner, width=200)
        side_au.pack(side=tk.LEFT, fill=tk.Y, padx=(12, 0))
        acfg = next((v for v in tel.values() if v.get("widget") == "progress_audio"), None)
        ttk.Label(
            side_au,
            text=(acfg or {}).get("label", "Indice chute — audio (%)"),
            style="OnCardMuted.TLabel",
        ).pack(anchor=tk.W)
        self.pb_audio = ttk.Progressbar(
            side_au, orient=tk.HORIZONTAL, length=200, maximum=100, mode="determinate"
        )
        self.pb_audio.pack(fill=tk.X, pady=(4, 12))

        self.fig_mr = Figure(figsize=(4.5, 2.4), dpi=100)
        self.fig_mr.patch.set_facecolor(_UI["bg"])
        self.ax_mr = self.fig_mr.add_subplot(111)
        self.ax_mr.set_title("Canal droit")
        self.ax_mr.grid(True, alpha=0.25)
        FigureCanvasTkAgg(self.fig_mr, master=au_inner).get_tk_widget().pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        fld = self.cfg.get("display_fields", {})
        if fld:
            fr = ttk.LabelFrame(main, text=" Champs JSON affichés ", padding=8)
            fr.pack(fill=tk.X, pady=(4, 0))
            for key, label in fld.items():
                row = ttk.Frame(fr)
                row.pack(fill=tk.X, pady=2)
                ttk.Label(row, text=f"{label}", width=26).pack(side=tk.LEFT)
                ttk.Label(row, text=f"({key})", foreground=_UI["muted"]).pack(side=tk.LEFT, padx=(4, 8))
                lb = ttk.Label(row, text="—", font=("", 10, "bold"))
                lb.pack(side=tk.LEFT)
                self._field_labels[key] = lb

        foot = ttk.Frame(self, padding=(16, 8), style="Card.TFrame")
        foot.pack(fill=tk.X, side=tk.BOTTOM)
        self._footer = ttk.Label(
            foot,
            text="LexaCare Clinical Monitor — démultiplexage série sécurisé",
            style="Status.TLabel",
        )
        self._footer.pack(anchor=tk.W)

    def _pick_config(self) -> None:
        p = filedialog.askopenfilename(filetypes=[("JSON", "*.json"), ("Tous", "*")])
        if not p:
            return
        try:
            self.cfg = load_config(Path(p))
            self.config_path = Path(p)
            messagebox.showinfo("Config", f"Rechargé : {p}\nRedémarrer l’appli pour tout réinitialiser si besoin.")
        except OSError as e:
            messagebox.showerror("Erreur", str(e))

    def _connect(self) -> None:
        if self.ser_holder[0] is not None:
            return
        self.stop.clear()
        sc = self.cfg.get("serial", {})
        port = str(sc.get("port", "/dev/ttyACM0"))
        baud = int(sc.get("baudrate", 921600))
        try:
            ser = serial.Serial(port, baud, timeout=0.05)
            ser.reset_input_buffer()
            self.ser_holder[0] = ser
        except (serial.SerialException, OSError) as e:
            messagebox.showerror("Série", str(e))
            return
        self.reader = threading.Thread(
            target=serial_reader,
            args=(self.ser_holder, self.cfg, self.q, self.stop),
            daemon=True,
        )
        self.reader.start()
        self.status.config(text=f"● Connecté  {port}  @  {baud} baud", foreground=_UI["success"])

    def _disconnect(self) -> None:
        self.stop.set()
        ser = self.ser_holder[0]
        if ser is not None:
            try:
                ser.reset_input_buffer()
                ser.close()
            except Exception:
                pass
            self.ser_holder[0] = None
        time.sleep(0.25)
        self.status.config(text="● Hors ligne — port libre pour reprogrammation", foreground=_UI["warn"])
        try:
            while True:
                self.q.get_nowait()
        except queue.Empty:
            pass

    def _drain_queue(self) -> None:
        try:
            while True:
                kind, data = self.q.get_nowait()
                if kind == "error":
                    self.status.config(text=str(data), foreground=_UI["warn"])
                elif kind == "audio":
                    self._on_audio(data)
                elif kind == "lidar":
                    self._on_lidar(data)
                elif kind == "json":
                    self._on_json(data)
        except queue.Empty:
            pass
        self._redraw_plots()
        self.after(40, self._drain_queue)

    def _on_audio(self, d: dict[str, Any]) -> None:
        pcm = d["pcm"]
        arr = np.frombuffer(pcm, dtype="<i2")
        if d.get("stereo") and len(arr) >= 2:
            self.buf_l.extend(arr[0::2].tolist())
            self.buf_r.extend(arr[1::2].tolist())
        else:
            self.buf_l.extend(arr.tolist())
            self.buf_r.extend(arr.tolist())

    def _show_lidar_mm(self, z: np.ndarray, source: str, clim_mode: str) -> None:
        """Affiche une grille ToF en millimètres. `clim_mode` : ``fixed`` (0…lidar_mm_max) ou ``auto``."""
        self._lidar_pending = np.asarray(z, dtype=np.float32)
        self._lidar_last_source = source
        now = time.monotonic()
        if now - self._last_lidar_draw < self.lidar_ms / 1000.0:
            return
        self._last_lidar_draw = now
        zd = self._lidar_pending
        self._im.set_data(zd)
        if clim_mode == "fixed":
            self._im.set_clim(0.0, self.lidar_mm_max)
        else:
            lo = float(np.min(zd))
            hi = float(np.max(zd))
            if not (np.isfinite(lo) and np.isfinite(hi)):
                lo, hi = 0.0, self.lidar_mm_max
            if hi <= lo:
                hi = lo + 1.0
            pad = (hi - lo) * 0.05
            self._im.set_clim(max(0.0, lo - pad), min(self.lidar_mm_max, hi + pad))

        self._lidar_frame_count += 1
        valid = zd[np.isfinite(zd) & (zd > 0)]
        if valid.size:
            stats = (
                f"min {int(valid.min())} mm\n"
                f"max {int(valid.max())} mm\n"
                f"moy. {int(valid.mean())} mm"
            )
        else:
            stats = "aucune mesure > 0"
        self._lidar_stats.config(
            text=f"{stats}\n\nSource : {self._lidar_last_source}\nTrames : {self._lidar_frame_count}"
        )

    def _on_lidar(self, d: dict[str, Any]) -> None:
        if d.get("mode") == "ascii_mm":
            z = np.asarray(d["z_mm"], dtype=np.float32)
            self._show_lidar_mm(z, "FRAME: (mm)", "fixed")
            return
        w, h = d["w"], d["h"]
        raw = d["f32"]
        need = w * h * 4
        if len(raw) < need:
            return
        zn = np.frombuffer(raw[:need], dtype="<f4").reshape(h, w)
        zn = np.nan_to_num(zn, nan=0.0, posinf=1.0, neginf=0.0)
        zn = np.clip(zn, 0.0, 1.0)
        z = zn * self.lidar_mm_max
        self._show_lidar_mm(z, "LXCL (float → mm)", "auto")

    def _on_json(self, obj: dict[str, Any]) -> None:
        tel = self.cfg.get("telemetry", {})
        for _k, spec in tel.items():
            jk = spec.get("json_key")
            if not jk or jk not in obj:
                continue
            try:
                v = float(obj[jk])
            except (TypeError, ValueError):
                continue
            v = max(0.0, min(100.0, v))
            if spec.get("widget") == "progress_lidar":
                self.pb_vision["value"] = v
            elif spec.get("widget") == "progress_audio":
                self.pb_audio["value"] = v

        t = float(obj.get("t_ms", time.time() * 1000))
        self.curve_t.append(t)
        for c in self.cfg.get("sensor_curves", []):
            jk = c.get("json_key")
            if jk and jk in obj and jk in self.curves and obj[jk] is not None:
                try:
                    self.curves[jk].append(float(obj[jk]))
                except (TypeError, ValueError):
                    pass

        for key, lb in self._field_labels.items():
            if key in obj:
                lb.config(text=str(obj[key]))

        al = obj.get("audio_left")
        ar = obj.get("audio_right")
        if isinstance(al, list) and isinstance(ar, list) and len(al) == len(ar):
            for a, b in zip(al, ar):
                try:
                    self.buf_l.append(int(a))
                    self.buf_r.append(int(b))
                except (TypeError, ValueError):
                    continue

        lm = obj.get("lidar_matrix")
        if isinstance(lm, dict):
            try:
                rows = int(lm.get("rows", LIDAR_ROWS))
                cols = int(lm.get("cols", LIDAR_COLS))
            except (TypeError, ValueError):
                rows, cols = LIDAR_ROWS, LIDAR_COLS
            data = lm.get("data")
            if isinstance(data, list) and rows * cols == len(data):
                z = np.asarray(data, dtype=np.float32).reshape(rows, cols)
                self._show_lidar_mm(z, "JSON lidar_matrix (mm)", "fixed")

        for conn_key, (cv, oid) in self._sensor_leds.items():
            if conn_key not in obj:
                continue
            raw = obj[conn_key]
            ok = raw in (1, True, "1") or (isinstance(raw, (int, float)) and int(raw) != 0)
            cv.itemconfig(oid, fill=_UI["success"] if ok else "#f87171")

    def _redraw_plots(self) -> None:
        if self.buf_l:
            xs = np.arange(len(self.buf_l))
            self.ax_ml.clear()
            self.ax_ml.plot(xs, np.fromiter(self.buf_l, dtype=np.float32), lw=0.45, color="#5eead4", alpha=0.92)
            self.ax_ml.set_title("Canal gauche", color=_UI["text"])
            self.ax_ml.set_facecolor(_UI["surface2"])
            self.ax_ml.grid(True, alpha=0.25)
            self.ax_ml.set_ylim(-32768, 32767)
        if self.buf_r:
            xs = np.arange(len(self.buf_r))
            self.ax_mr.clear()
            self.ax_mr.plot(xs, np.fromiter(self.buf_r, dtype=np.float32), lw=0.45, color="#c4b5fd", alpha=0.92)
            self.ax_mr.set_title("Canal droit", color=_UI["text"])
            self.ax_mr.set_facecolor(_UI["surface2"])
            self.ax_mr.grid(True, alpha=0.25)
            self.ax_mr.set_ylim(-32768, 32767)
        self.fig_ml.canvas.draw_idle()
        self.fig_mr.canvas.draw_idle()

        if self._ax_sens_list and self._cv_sens is not None and any(len(v) > 1 for v in self.curves.values()):
            t0 = self.curve_t[0] if self.curve_t else 0
            tx = [x - t0 for x in self.curve_t]
            curves_cfg = self.cfg.get("sensor_curves", [])
            for i, c in enumerate(curves_cfg):
                if i >= len(self._ax_sens_list):
                    break
                ax = self._ax_sens_list[i]
                ax.clear()
                jk = c.get("json_key")
                if not jk or jk not in self.curves:
                    ax.set_facecolor(_UI["surface2"])
                    ax.grid(True, alpha=0.25)
                    ax.set_ylabel(str(c.get("label", jk))[:14], fontsize=7, color=_UI["muted"])
                    continue
                series = list(self.curves[jk])
                n = min(len(series), len(tx))
                if n >= 2:
                    ax.plot(
                        tx[:n],
                        series[:n],
                        color=c.get("color") or "#7dd3fc",
                        lw=0.9,
                    )
                ax.set_ylabel(str(c.get("label", jk))[:14], fontsize=7, color=_UI["muted"])
                ax.set_facecolor(_UI["surface2"])
                ax.grid(True, alpha=0.25)
                ax.tick_params(labelsize=7)
            self.fig_sens.subplots_adjust(left=0.28, right=0.96, hspace=0.35)
            self._cv_sens.draw_idle()
        self.fig_lidar.canvas.draw_idle()

    def destroy(self) -> None:  # type: ignore[override]
        self._disconnect()
        super().destroy()


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument(
        "--config",
        type=Path,
        default=Path(__file__).resolve().parent / "lexa_live_config.example.json",
        help="Fichier JSON de configuration UI / série / clés télémétrie",
    )
    args = p.parse_args()
    if not args.config.is_file():
        raise SystemExit(f"Config introuvable : {args.config}")
    app = LexaLiveApp(args.config)
    app.mainloop()


if __name__ == "__main__":
    main()
