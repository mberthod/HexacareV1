#!/usr/bin/env python3
"""
LexaCare V1 — Station de Monitoring Professionnelle
====================================================
Interface haute performance pour la validation hardware du firmware LexaCare.

Fonctionnalités :
  · Visualisation temps réel de la matrice LIDAR 8×32 (carte de profondeur)
  · Affichage thermique MLX90640 32×24 (disponible après intégration driver)
  · Monitoring IA : détection de chute, confiance, historique d'événements
  · Validation/invalidation des composants (LIDARs 1–4, alimentations)
  · Métriques système (heap, PSRAM, tâches FreeRTOS, uptime)
  · Console USB temps réel avec filtrage et coloration syntaxique

Protocole firmware :
  RX  JSON toutes les 5 s  : {"uptime_s":…, "heap_free_internal":…, "tasks":[…]}
  TX  Commandes JSON       : {"cmd":"get_diag"} | {"cmd":"download_logs"} | …
  TX  Étendu (à venir)    : {"cmd":"get_lidar"} → matrice 8×32

Lancement :
  pip install -r requirements.txt
  python lexacare_monitor.py
"""

import sys
import json
import time
import math
import random
import threading
from datetime import datetime, timedelta
from collections import deque

import numpy as np
import serial
import serial.tools.list_ports
import matplotlib
matplotlib.use('QtAgg')
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg
from matplotlib.figure import Figure
from matplotlib.ticker import MaxNLocator

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QGridLayout, QLabel, QPushButton, QComboBox, QTextEdit,
    QFrame, QSizePolicy, QSplitter, QGroupBox, QProgressBar,
    QSpacerItem, QScrollArea, QSlider, QCheckBox, QLineEdit,
    QTableWidget, QTableWidgetItem, QHeaderView, QAbstractItemView,
)
from PyQt6.QtCore import (
    Qt, QTimer, QThread, pyqtSignal, QSize, QRect, QPoint,
    QPropertyAnimation, QEasingCurve, QObject, QRegularExpression,
)
from PyQt6.QtGui import (
    QFont, QColor, QPalette, QPixmap, QPainter, QBrush, QPen,
    QLinearGradient, QRadialGradient, QFontDatabase, QIcon,
    QTextCursor, QTextCharFormat, QSyntaxHighlighter,
    QPainterPath,
)

# ─── Constantes de design ─────────────────────────────────────────────────────
C_BG        = "#04080f"   # fond principal
C_SURFACE   = "#0a1628"   # surface des panneaux
C_SURFACE2  = "#0f2040"   # surface secondaire / survol
C_BORDER    = "#1a3a5c"   # bordure fine
C_BORDER2   = "#0d2540"   # bordure très subtile
C_CYAN      = "#00d4ff"   # accent principal
C_CYAN_DIM  = "#006888"   # accent atténué
C_GREEN     = "#00ff88"   # succès / actif
C_AMBER     = "#ffc400"   # avertissement
C_RED       = "#ff3366"   # danger / erreur
C_PURPLE    = "#a855f7"   # accent secondaire
C_TEXT      = "#dce8f5"   # texte principal
C_TEXT_DIM  = "#4a6fa5"   # texte secondaire
C_TEXT_MUT  = "#1e3a5c"   # texte très atténué
C_GOLD      = "#ffd700"   # accent premium

LIDAR_ROWS  = 8
LIDAR_COLS  = 32
MLX_ROWS    = 24
MLX_COLS    = 32

# ─── Feuille de style globale ─────────────────────────────────────────────────
QSS = f"""
QMainWindow, QWidget {{
    background-color: {C_BG};
    color: {C_TEXT};
    font-family: 'Segoe UI', 'Inter', 'SF Pro Display', Arial, sans-serif;
}}
QFrame.card {{
    background-color: {C_SURFACE};
    border: 1px solid {C_BORDER};
    border-radius: 10px;
}}
QFrame.card-accent {{
    background-color: {C_SURFACE};
    border: 1px solid {C_CYAN};
    border-radius: 10px;
}}
QPushButton {{
    background-color: {C_SURFACE2};
    color: {C_TEXT};
    border: 1px solid {C_BORDER};
    border-radius: 6px;
    padding: 7px 16px;
    font-size: 12px;
    font-weight: 500;
}}
QPushButton:hover {{
    background-color: #0d2a4a;
    border-color: {C_CYAN};
    color: {C_CYAN};
}}
QPushButton:pressed {{
    background-color: {C_CYAN_DIM};
}}
QPushButton.primary {{
    background-color: {C_CYAN_DIM};
    color: {C_CYAN};
    border-color: {C_CYAN};
    font-weight: 600;
}}
QPushButton.primary:hover {{
    background-color: #008aa0;
}}
QPushButton.danger {{
    background-color: #3d0015;
    color: {C_RED};
    border-color: {C_RED};
}}
QPushButton.active-toggle {{
    background-color: #003320;
    color: {C_GREEN};
    border: 2px solid {C_GREEN};
    font-weight: 700;
}}
QPushButton.inactive-toggle {{
    background-color: #3d0015;
    color: {C_RED};
    border: 2px solid {C_RED};
    font-weight: 700;
}}
QComboBox {{
    background-color: {C_SURFACE2};
    color: {C_TEXT};
    border: 1px solid {C_BORDER};
    border-radius: 6px;
    padding: 6px 12px;
    font-size: 12px;
}}
QComboBox::drop-down {{
    border: none;
    width: 20px;
}}
QComboBox:hover {{
    border-color: {C_CYAN};
}}
QComboBox QAbstractItemView {{
    background-color: {C_SURFACE};
    color: {C_TEXT};
    selection-background-color: {C_CYAN_DIM};
    border: 1px solid {C_BORDER};
}}
QLabel.title {{
    color: {C_CYAN};
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 2px;
    text-transform: uppercase;
}}
QLabel.value {{
    color: {C_TEXT};
    font-size: 22px;
    font-weight: 700;
}}
QLabel.metric {{
    color: {C_TEXT_DIM};
    font-size: 11px;
    font-weight: 500;
}}
QLabel.badge-ok {{
    color: {C_GREEN};
    background-color: #002a18;
    border: 1px solid {C_GREEN};
    border-radius: 4px;
    padding: 2px 8px;
    font-size: 11px;
    font-weight: 700;
}}
QLabel.badge-err {{
    color: {C_RED};
    background-color: #3d0015;
    border: 1px solid {C_RED};
    border-radius: 4px;
    padding: 2px 8px;
    font-size: 11px;
    font-weight: 700;
}}
QLabel.badge-warn {{
    color: {C_AMBER};
    background-color: #2a1800;
    border: 1px solid {C_AMBER};
    border-radius: 4px;
    padding: 2px 8px;
    font-size: 11px;
    font-weight: 700;
}}
QProgressBar {{
    background-color: {C_SURFACE2};
    border: 1px solid {C_BORDER};
    border-radius: 4px;
    height: 6px;
    text-align: center;
    font-size: 10px;
}}
QProgressBar::chunk {{
    border-radius: 3px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 {C_CYAN_DIM}, stop:1 {C_CYAN});
}}
QTextEdit {{
    background-color: #020912;
    color: #7dc4e0;
    border: 1px solid {C_BORDER2};
    border-radius: 6px;
    font-family: 'Cascadia Code', 'JetBrains Mono', 'Consolas', monospace;
    font-size: 11px;
    padding: 4px;
}}
QScrollBar:vertical {{
    background: {C_SURFACE2};
    width: 6px;
    border-radius: 3px;
}}
QScrollBar::handle:vertical {{
    background: {C_BORDER};
    border-radius: 3px;
    min-height: 20px;
}}
QScrollBar::handle:vertical:hover {{
    background: {C_CYAN_DIM};
}}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ height: 0; }}
QTableWidget {{
    background-color: {C_SURFACE};
    color: {C_TEXT};
    border: 1px solid {C_BORDER};
    border-radius: 6px;
    gridline-color: {C_BORDER2};
    font-size: 11px;
}}
QTableWidget::item:selected {{
    background-color: {C_CYAN_DIM};
}}
QHeaderView::section {{
    background-color: {C_SURFACE2};
    color: {C_TEXT_DIM};
    border: none;
    border-bottom: 1px solid {C_BORDER};
    padding: 4px 8px;
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 1px;
}}
"""


# ─── Thread de communication série ───────────────────────────────────────────
class SerialWorker(QThread):
    data_received   = pyqtSignal(dict)
    raw_log         = pyqtSignal(str)
    connection_changed = pyqtSignal(bool)

    def __init__(self, port: str, baudrate: int = 115200):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self._running = True
        self._serial: serial.Serial | None = None
        self._send_queue: list[str] = []
        self._lock = threading.Lock()

    def send(self, cmd: dict):
        with self._lock:
            self._send_queue.append(json.dumps(cmd) + "\n")

    def stop(self):
        self._running = False

    def run(self):
        try:
            self._serial = serial.Serial(self.port, self.baudrate, timeout=0.1)
            self.connection_changed.emit(True)
        except Exception as e:
            self.raw_log.emit(f"[ERR] Connexion série échouée : {e}")
            self.connection_changed.emit(False)
            return

        buf = ""
        while self._running:
            # Envoi des commandes en attente
            with self._lock:
                cmds = self._send_queue[:]
                self._send_queue.clear()
            for cmd in cmds:
                try:
                    self._serial.write(cmd.encode())
                except Exception:
                    pass

            # Réception
            try:
                raw = self._serial.read(4096).decode(errors='replace')
            except Exception:
                break

            if raw:
                buf += raw
                while '\n' in buf:
                    line, buf = buf.split('\n', 1)
                    line = line.strip()
                    if not line:
                        continue
                    self.raw_log.emit(line)
                    if line.startswith('{'):
                        try:
                            data = json.loads(line)
                            self.data_received.emit(data)
                        except json.JSONDecodeError:
                            pass

        if self._serial and self._serial.is_open:
            self._serial.close()
        self.connection_changed.emit(False)


# ─── Simulation de données (mode démo sans matériel) ─────────────────────────
class DataSimulator(QThread):
    data_received = pyqtSignal(dict)
    raw_log       = pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self._running = True
        self._t = 0.0
        self._uptime = 0

    def stop(self):
        self._running = False

    def run(self):
        logs = [
            "[I] ai_engine: Mode seuil actif (seuil: 80%, historique: 8 trames)",
            "[I] hw_diag: LIDAR1 SPI OK (NCS=GPIO1, LPn=IO0.6)",
            "[I] hw_diag: LIDAR2 SPI OK (NCS=GPIO2, LPn=IO0.5)",
            "[I] hw_diag: LIDAR3 SPI OK (NCS=GPIO42, LPn=IO0.4)",
            "[I] hw_diag: LIDAR4 SPI OK (NCS=GPIO41, LPn=IO0.3)",
            "[I] hw_diag: Radar LD6002 : trame TinyFrame détectée",
            "[I] mesh_mgr: MAC: AA:BB:CC:DD:EE:FF  Rôle: ROOT",
            "[I] pc_diag: Task_Diag_PC démarrée sur Core 0",
            "[I] ai_engine: NORMAL (confiance 0%)",
        ]
        log_idx = 0
        while self._running:
            self._t += 0.1
            self._uptime += 1

            # Matrice LIDAR simulée (onde sinusoïdale + bruit)
            lidar = []
            for row in range(LIDAR_ROWS):
                r = []
                for col in range(LIDAR_COLS):
                    base = 1500 + 1000 * math.sin(self._t * 0.3 + col * 0.2)
                    wave = 300 * math.sin(self._t * 0.8 + row * 0.5)
                    noise = random.gauss(0, 80)
                    # Simuler une "personne" qui se déplace
                    person_col = 16 + int(8 * math.sin(self._t * 0.2))
                    if abs(col - person_col) < 4:
                        base = 600 + 200 * math.sin(self._t * 0.5)
                    r.append(max(100, int(base + wave + noise)))
                lidar.append(r)

            # Radar simulé
            ai_state = 0
            ai_conf  = 0
            if random.random() < 0.005:  # chute rare
                ai_state = 1
                ai_conf  = random.randint(70, 99)

            data = {
                "uptime_s": self._uptime,
                "heap_free_internal": 8_738_768 - random.randint(0, 50000),
                "heap_min_internal":  7_500_000,
                "heap_free_psram":    8_386_192 - random.randint(0, 100000),
                "tasks": [
                    {"name": "Task_Sensor_Acq", "hwm_bytes": 2048, "priority": 10},
                    {"name": "Task_AI_Inference", "hwm_bytes": 1800, "priority": 9},
                    {"name": "Task_Mesh_Com",    "hwm_bytes": 1200, "priority": 8},
                    {"name": "Task_Diag_PC",     "hwm_bytes":  800, "priority": 3},
                ],
                "last_ai_event": {"state": ai_state, "confidence": ai_conf},
                "lidar_matrix": lidar,
                "radar": {
                    "breath_bpm": 14 + random.gauss(0, 1),
                    "heart_bpm":  72 + random.gauss(0, 2),
                    "distance_mm": 1200 + random.randint(-50, 50),
                    "presence": True,
                },
            }
            self.data_received.emit(data)

            # Log périodique
            if self._uptime % 5 == 0:
                self.raw_log.emit(logs[log_idx % len(logs)])
                log_idx += 1

            time.sleep(1.0)


# ─── Widget : Heatmap matplotlib générique ───────────────────────────────────
class HeatmapWidget(QWidget):
    def __init__(self, rows: int, cols: int, title: str,
                 cmap: str = 'plasma', vmin: float = 0, vmax: float = 5000,
                 unit: str = "mm", parent=None):
        super().__init__(parent)
        self._rows  = rows
        self._cols  = cols
        self._vmin  = vmin
        self._vmax  = vmax
        self._unit  = unit
        self._data  = np.full((rows, cols), (vmin + vmax) / 2)
        self._offline = False

        fig = Figure(figsize=(1, 1), facecolor=C_SURFACE)
        self._canvas = FigureCanvasQTAgg(fig)
        self._ax = fig.add_axes([0.06, 0.06, 0.82, 0.88])
        self._ax.set_facecolor(C_BG)
        for sp in self._ax.spines.values():
            sp.set_color(C_BORDER)
        self._ax.tick_params(colors=C_TEXT_DIM, labelsize=7)
        self._ax.set_title(title, color=C_TEXT_DIM, fontsize=9,
                           fontweight='bold', pad=4)

        self._im = self._ax.imshow(
            self._data, cmap=cmap, vmin=vmin, vmax=vmax,
            aspect='auto', interpolation='bilinear', origin='upper',
        )
        cbar = fig.colorbar(self._im, ax=self._ax, fraction=0.04, pad=0.02)
        cbar.ax.tick_params(colors=C_TEXT_DIM, labelsize=7)
        cbar.set_label(unit, color=C_TEXT_DIM, fontsize=8)
        cbar.outline.set_color(C_BORDER)

        self._offline_text = self._ax.text(
            0.5, 0.5, '', ha='center', va='center',
            transform=self._ax.transAxes,
            fontsize=14, color=C_TEXT_DIM, fontweight='bold',
            bbox=dict(boxstyle='round,pad=0.5', facecolor='#04080fe0',
                      edgecolor=C_BORDER, linewidth=1.5),
        )

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self._canvas)

    def update_data(self, matrix: list[list[float]]):
        arr = np.array(matrix, dtype=float)
        if arr.shape == (self._rows, self._cols):
            self._data = arr
            self._im.set_data(arr)
            self._canvas.draw_idle()

    def set_offline(self, msg: str = ""):
        self._offline = bool(msg)
        self._offline_text.set_text(msg)
        self._canvas.draw_idle()


# ─── Widget : Jauge circulaire ────────────────────────────────────────────────
class GaugeWidget(QWidget):
    def __init__(self, label: str, unit: str, vmax: float,
                 color: str = C_CYAN, parent=None):
        super().__init__(parent)
        self._label = label
        self._unit  = unit
        self._vmax  = vmax
        self._value = 0.0
        self._color = QColor(color)
        self.setMinimumSize(100, 100)
        self.setSizePolicy(QSizePolicy.Policy.Expanding,
                           QSizePolicy.Policy.Preferred)

    def set_value(self, v: float):
        self._value = max(0, min(v, self._vmax))
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        if w < 30 or h < 30:
            return
        cx, cy = w // 2, h // 2
        r = max(10, min(w, h) // 2 - 10)

        # Fond arc
        pen = QPen(QColor(C_SURFACE2), 8)
        pen.setCapStyle(Qt.PenCapStyle.RoundCap)
        p.setPen(pen)
        p.drawArc(cx - r, cy - r, r * 2, r * 2, 225 * 16, -270 * 16)

        # Arc valeur
        ratio = self._value / self._vmax if self._vmax > 0 else 0
        span  = int(-270 * 16 * ratio)
        if span != 0:
            col = QColor(self._color)
            grd = QLinearGradient(cx - r, cy, cx + r, cy)
            grd.setColorAt(0, QColor(C_CYAN_DIM))
            grd.setColorAt(1, col)
            pen2 = QPen(QBrush(grd), 8)
            pen2.setCapStyle(Qt.PenCapStyle.RoundCap)
            p.setPen(pen2)
            p.drawArc(cx - r, cy - r, r * 2, r * 2, 225 * 16, span)

        # Texte valeur
        p.setPen(QPen(QColor(C_TEXT)))
        f = QFont("Segoe UI", 11, QFont.Weight.Bold)
        p.setFont(f)
        p.drawText(QRect(cx - r, cy - 18, r * 2, 24),
                   Qt.AlignmentFlag.AlignCenter,
                   f"{self._value:.1f}")
        p.setPen(QPen(QColor(C_TEXT_DIM)))
        f2 = QFont("Segoe UI", 7)
        p.setFont(f2)
        p.drawText(QRect(cx - r, cy + 6, r * 2, 16),
                   Qt.AlignmentFlag.AlignCenter, self._unit)
        p.drawText(QRect(cx - r, cy + r - 6, r * 2, 14),
                   Qt.AlignmentFlag.AlignCenter, self._label)


# ─── Widget : Indicateur de statut IA ────────────────────────────────────────
class AIStatusWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._state = 0
        self._conf  = 0
        self._events: deque[tuple[str, int, str]] = deque(maxlen=50)
        self._setup_ui()

    def _setup_ui(self):
        lay = QHBoxLayout(self)
        lay.setContentsMargins(16, 10, 16, 10)
        lay.setSpacing(20)

        # Indicateur principal
        left = QVBoxLayout()
        left.setSpacing(2)
        self._lbl_state = QLabel("● NORMAL")
        self._lbl_state.setStyleSheet(
            f"color: {C_GREEN}; font-size: 18px; font-weight: 800; "
            f"letter-spacing: 2px;")
        left.addWidget(self._lbl_state)
        lbl_ai = QLabel("MOTEUR IA — DÉTECTION DE CHUTE")
        lbl_ai.setStyleSheet(
            f"color: {C_TEXT_DIM}; font-size: 9px; letter-spacing: 1.5px;")
        left.addWidget(lbl_ai)
        lay.addLayout(left)

        # Barre de confiance
        conf_lay = QVBoxLayout()
        conf_lay.setSpacing(4)
        self._lbl_conf = QLabel("Confiance : 0 %")
        self._lbl_conf.setStyleSheet(
            f"color: {C_TEXT_DIM}; font-size: 10px;")
        conf_lay.addWidget(self._lbl_conf)
        self._bar_conf = QProgressBar()
        self._bar_conf.setRange(0, 100)
        self._bar_conf.setValue(0)
        self._bar_conf.setFixedHeight(8)
        self._bar_conf.setTextVisible(False)
        conf_lay.addWidget(self._bar_conf)
        lay.addLayout(conf_lay)
        lay.setStretch(1, 2)

        lay.addStretch()

        # Compteurs
        for label, attr in [("NORMAUX", "_cnt_normal"),
                             ("ALERTES", "_cnt_alert"),
                             ("ANOMALIES", "_cnt_anom")]:
            col = QVBoxLayout()
            col.setSpacing(0)
            lbl_n = QLabel("0")
            lbl_n.setStyleSheet(
                f"color: {C_CYAN}; font-size: 24px; font-weight: 800;")
            lbl_n.setAlignment(Qt.AlignmentFlag.AlignCenter)
            col.addWidget(lbl_n)
            lbl_t = QLabel(label)
            lbl_t.setStyleSheet(
                f"color: {C_TEXT_DIM}; font-size: 8px; letter-spacing: 1px;")
            lbl_t.setAlignment(Qt.AlignmentFlag.AlignCenter)
            col.addWidget(lbl_t)
            lay.addLayout(col)
            setattr(self, attr, lbl_n)
            setattr(self, f"_{label.lower()}_count", 0)

    def update_ai(self, state: int, confidence: int):
        self._state = state
        self._conf  = confidence
        self._bar_conf.setValue(confidence)

        if state == 1:
            self._lbl_state.setText("⚠ CHUTE DÉTECTÉE")
            self._lbl_state.setStyleSheet(
                f"color: {C_RED}; font-size: 18px; font-weight: 800; "
                f"letter-spacing: 2px;")
            self._bar_conf.setStyleSheet(
                "QProgressBar::chunk { background: " + C_RED + "; }")
            self.__dict__['_alertes_count'] = (
                self.__dict__.get('_alertes_count', 0) + 1)
            self._cnt_alert.setText(str(self._alertes_count))
        elif state == 2:
            self._lbl_state.setText("~ MOUVEMENT ANORMAL")
            self._lbl_state.setStyleSheet(
                f"color: {C_AMBER}; font-size: 18px; font-weight: 800; "
                f"letter-spacing: 2px;")
            self._bar_conf.setStyleSheet(
                "QProgressBar::chunk { background: " + C_AMBER + "; }")
        else:
            self._lbl_state.setText("● NORMAL")
            self._lbl_state.setStyleSheet(
                f"color: {C_GREEN}; font-size: 18px; font-weight: 800; "
                f"letter-spacing: 2px;")
            self._bar_conf.setStyleSheet("")
            self.__dict__['_normaux_count'] = (
                self.__dict__.get('_normaux_count', 0) + 1)
            self._cnt_normal.setText(str(self._normaux_count))

        self._lbl_conf.setText(f"Confiance : {confidence} %")


# ─── Widget : Panneau de validation composants ───────────────────────────────
class ValidationPanel(QWidget):
    command_requested = pyqtSignal(dict)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._states: dict[str, bool] = {}
        self._buttons: dict[str, QPushButton] = {}
        self._setup_ui()

    def _setup_ui(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(12, 8, 12, 8)
        lay.setSpacing(8)

        title = QLabel("VALIDATION COMPOSANTS")
        title.setStyleSheet(
            f"color: {C_CYAN}; font-size: 10px; font-weight: 700; "
            f"letter-spacing: 2px;")
        lay.addWidget(title)

        # LIDARs
        lidar_lay = QGridLayout()
        lidar_lay.setSpacing(6)
        for i in range(1, 5):
            key = f"LIDAR {i}"
            btn = QPushButton(f"◉  LIDAR {i}")
            btn.setFixedHeight(34)
            btn.clicked.connect(lambda checked, k=key: self._toggle(k))
            self._states[key] = True
            self._buttons[key] = btn
            self._apply_style(key)
            lidar_lay.addWidget(btn, (i - 1) // 2, (i - 1) % 2)
        lay.addLayout(lidar_lay)

        # Séparateur
        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet(f"color: {C_BORDER};")
        lay.addWidget(sep)

        # Alimentations
        pwr_grid = QGridLayout()
        pwr_grid.setSpacing(6)
        pwr_items = [
            ("FAN", "VENTIL."),
            ("RADAR", "RADAR"),
            ("MIC", "MICRO"),
            ("MLX", "MLX"),
        ]
        for idx, (key, lbl) in enumerate(pwr_items):
            k = f"PWR_{key}"
            btn = QPushButton(f"⚡ {lbl}")
            btn.setFixedHeight(34)
            btn.clicked.connect(lambda checked, ki=k: self._toggle(ki))
            self._states[k] = False  # alimentations éteintes par défaut
            self._buttons[k] = btn
            self._apply_style(k)
            pwr_grid.addWidget(btn, idx // 2, idx % 2)
        lay.addLayout(pwr_grid)
        lay.addStretch()

    def _toggle(self, key: str):
        self._states[key] = not self._states[key]
        self._apply_style(key)
        # Construire la commande firmware
        cmd = self._build_command(key, self._states[key])
        if cmd:
            self.command_requested.emit(cmd)

    def _apply_style(self, key: str):
        btn = self._buttons[key]
        if self._states[key]:
            btn.setProperty("class", "active-toggle")
        else:
            btn.setProperty("class", "inactive-toggle")
        btn.setStyle(btn.style())

    @staticmethod
    def _build_command(key: str, active: bool) -> dict | None:
        mapping = {
            "LIDAR 1": "lidar1", "LIDAR 2": "lidar2",
            "LIDAR 3": "lidar3", "LIDAR 4": "lidar4",
            "PWR_FAN": "power_fan", "PWR_RADAR": "power_radar",
            "PWR_MIC": "power_mic", "PWR_MLX": "power_mlx",
        }
        target = mapping.get(key)
        if target:
            return {"cmd": "set_component",
                    "target": target, "enable": active}
        return None


# ─── Widget : Métriques système ──────────────────────────────────────────────
class SystemMetrics(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._start = time.time()
        self._setup_ui()

    def _setup_ui(self):
        lay = QGridLayout(self)
        lay.setContentsMargins(12, 8, 12, 8)
        lay.setSpacing(6)

        metrics = [
            ("HEAP INT.", "heap_int", C_CYAN,   "MB"),
            ("HEAP MIN.", "heap_min", C_AMBER,   "MB"),
            ("PSRAM",     "psram",    C_PURPLE,  "MB"),
            ("UPTIME",    "uptime",   C_GREEN,   ""),
        ]
        self._labels: dict[str, QLabel] = {}
        self._bars: dict[str, QProgressBar] = {}

        title = QLabel("MÉTRIQUES SYSTÈME")
        title.setStyleSheet(
            f"color: {C_CYAN}; font-size: 10px; font-weight: 700; "
            f"letter-spacing: 2px;")
        lay.addWidget(title, 0, 0, 1, 2)

        for row, (name, key, color, unit) in enumerate(metrics, 1):
            lbl_name = QLabel(name)
            lbl_name.setStyleSheet(
                f"color: {C_TEXT_DIM}; font-size: 10px;")
            lbl_val = QLabel("—")
            lbl_val.setStyleSheet(
                f"color: {color}; font-size: 13px; font-weight: 700;")
            lbl_val.setAlignment(Qt.AlignmentFlag.AlignRight)
            bar = QProgressBar()
            bar.setRange(0, 100)
            bar.setValue(0)
            bar.setFixedHeight(4)
            bar.setTextVisible(False)
            bar.setStyleSheet(
                f"QProgressBar::chunk {{ background: {color}; }}")

            lay.addWidget(lbl_name, row, 0)
            lay.addWidget(lbl_val,  row, 1)
            lay.addWidget(bar, row + len(metrics), 0, 1, 2)

            self._labels[key] = lbl_val
            self._bars[key]   = bar

        # Tâches FreeRTOS
        self._task_table = QTableWidget(0, 3)
        self._task_table.setHorizontalHeaderLabels(["Tâche", "HWM", "Prio"])
        self._task_table.horizontalHeader().setSectionResizeMode(
            0, QHeaderView.ResizeMode.Stretch)
        self._task_table.setSelectionMode(
            QAbstractItemView.SelectionMode.NoSelection)
        self._task_table.setFixedHeight(110)
        lay.addWidget(self._task_table, row + len(metrics) + 1, 0, 1, 2)

        # Rôle + IDF
        self._lbl_role = QLabel("Rôle : —")
        self._lbl_role.setStyleSheet(
            f"color: {C_TEXT_DIM}; font-size: 10px;")
        lay.addWidget(self._lbl_role, row + len(metrics) + 2, 0, 1, 2)

    def update(self, data: dict):
        h_int   = data.get("heap_free_internal", 0) / 1e6
        h_min   = data.get("heap_min_internal",  0) / 1e6
        h_psram = data.get("heap_free_psram",    0) / 1e6
        up      = data.get("uptime_s", 0)

        self._labels["heap_int"].setText(f"{h_int:.2f} MB")
        self._labels["heap_min"].setText(f"{h_min:.2f} MB")
        self._labels["psram"].setText(f"{h_psram:.2f} MB")
        td = timedelta(seconds=up)
        self._labels["uptime"].setText(
            f"{td.seconds//3600:02d}:{(td.seconds%3600)//60:02d}:{td.seconds%60:02d}")

        self._bars["heap_int"].setValue(int(h_int / 8.7 * 100))
        self._bars["heap_min"].setValue(int(h_min / 8.7 * 100))
        self._bars["psram"].setValue(int(h_psram / 8.4 * 100))

        tasks = data.get("tasks", [])
        self._task_table.setRowCount(len(tasks))
        for i, t in enumerate(tasks):
            for j, val in enumerate([
                t.get("name", "?"),
                f"{t.get('hwm_bytes', 0)} B",
                str(t.get("priority", 0)),
            ]):
                item = QTableWidgetItem(val)
                item.setForeground(QColor(C_TEXT))
                self._task_table.setItem(i, j, item)


# ─── Panneau header ──────────────────────────────────────────────────────────
class HeaderBar(QWidget):
    connect_requested    = pyqtSignal(str, int)
    disconnect_requested = pyqtSignal()
    sim_requested        = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedHeight(56)
        self.setStyleSheet(
            f"background-color: {C_SURFACE}; "
            f"border-bottom: 1px solid {C_BORDER};")
        lay = QHBoxLayout(self)
        lay.setContentsMargins(20, 0, 20, 0)
        lay.setSpacing(12)

        # Logo / titre
        logo = QLabel("◆")
        logo.setStyleSheet(
            f"color: {C_GOLD}; font-size: 20px; font-weight: 900;")
        lay.addWidget(logo)

        title = QLabel("LEXACARE V1")
        title.setStyleSheet(
            f"color: {C_TEXT}; font-size: 16px; font-weight: 800; "
            f"letter-spacing: 3px;")
        lay.addWidget(title)

        sub = QLabel("MONITORING STATION")
        sub.setStyleSheet(
            f"color: {C_TEXT_DIM}; font-size: 10px; letter-spacing: 2px;")
        sub.setContentsMargins(0, 6, 0, 0)
        lay.addWidget(sub)

        lay.addStretch()

        # Connexion série
        self._port_combo = QComboBox()
        self._port_combo.setFixedWidth(180)
        self._refresh_ports()
        lay.addWidget(self._port_combo)

        baud_combo = QComboBox()
        baud_combo.addItems(["115200", "230400", "460800", "921600"])
        baud_combo.setFixedWidth(90)
        self._baud_combo = baud_combo
        lay.addWidget(baud_combo)

        btn_refresh = QPushButton("↺")
        btn_refresh.setFixedWidth(32)
        btn_refresh.setToolTip("Actualiser les ports")
        btn_refresh.clicked.connect(self._refresh_ports)
        lay.addWidget(btn_refresh)

        self._btn_connect = QPushButton("CONNECTER")
        self._btn_connect.setProperty("class", "primary")
        self._btn_connect.setFixedWidth(110)
        self._btn_connect.clicked.connect(self._on_connect)
        lay.addWidget(self._btn_connect)

        btn_sim = QPushButton("⚙ DÉMO")
        btn_sim.setToolTip("Mode simulation (sans matériel)")
        btn_sim.setFixedWidth(80)
        btn_sim.clicked.connect(self.sim_requested)
        lay.addWidget(btn_sim)

        # Indicateur de connexion
        self._dot = QLabel("●")
        self._dot.setStyleSheet(f"color: {C_TEXT_MUT}; font-size: 14px;")
        self._lbl_status = QLabel("DÉCONNECTÉ")
        self._lbl_status.setStyleSheet(
            f"color: {C_TEXT_MUT}; font-size: 10px; letter-spacing: 1px;")
        lay.addWidget(self._dot)
        lay.addWidget(self._lbl_status)

    def _refresh_ports(self):
        self._port_combo.clear()
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self._port_combo.addItems(ports or ["—"])

    def _on_connect(self):
        port = self._port_combo.currentText()
        if port == "—":
            return
        baud = int(self._baud_combo.currentText())
        self.connect_requested.emit(port, baud)

    def set_connected(self, ok: bool):
        if ok:
            self._dot.setStyleSheet(
                f"color: {C_GREEN}; font-size: 14px;")
            self._lbl_status.setStyleSheet(
                f"color: {C_GREEN}; font-size: 10px; letter-spacing: 1px;")
            self._lbl_status.setText("CONNECTÉ")
            self._btn_connect.setText("DÉCONNECTER")
            self._btn_connect.setProperty("class", "danger")
        else:
            self._dot.setStyleSheet(
                f"color: {C_RED}; font-size: 14px;")
            self._lbl_status.setStyleSheet(
                f"color: {C_RED}; font-size: 10px; letter-spacing: 1px;")
            self._lbl_status.setText("DÉCONNECTÉ")
            self._btn_connect.setText("CONNECTER")
            self._btn_connect.setProperty("class", "primary")
        self._btn_connect.setStyle(self._btn_connect.style())


# ─── Console USB (log coloré) ─────────────────────────────────────────────────
class ConsoleWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(4)

        hdr = QHBoxLayout()
        lbl = QLabel("CONSOLE USB")
        lbl.setStyleSheet(
            f"color: {C_CYAN}; font-size: 10px; font-weight: 700; "
            f"letter-spacing: 2px;")
        hdr.addWidget(lbl)
        hdr.addStretch()
        btn_clear = QPushButton("Effacer")
        btn_clear.setFixedHeight(22)
        btn_clear.setFixedWidth(70)
        btn_clear.clicked.connect(self._clear)
        hdr.addWidget(btn_clear)
        self._chk_scroll = QCheckBox("Auto-scroll")
        self._chk_scroll.setChecked(True)
        self._chk_scroll.setStyleSheet(f"color: {C_TEXT_DIM}; font-size: 10px;")
        hdr.addWidget(self._chk_scroll)
        lay.addLayout(hdr)

        self._text = QTextEdit()
        self._text.setReadOnly(True)
        self._text.setFixedHeight(130)
        lay.addWidget(self._text)

        # Barre de commande
        cmd_lay = QHBoxLayout()
        self._cmd_input = QLineEdit()
        self._cmd_input.setPlaceholderText(
            '{"cmd":"get_diag"}  ·  {"cmd":"download_logs"}')
        self._cmd_input.setStyleSheet(
            f"background: #020912; color: {C_CYAN}; "
            f"border: 1px solid {C_BORDER}; border-radius: 4px; "
            f"padding: 4px 8px; font-family: monospace; font-size: 11px;")
        cmd_lay.addWidget(self._cmd_input)
        btn_send = QPushButton("Envoyer →")
        btn_send.setFixedWidth(90)
        btn_send.clicked.connect(self._send_cmd)
        cmd_lay.addWidget(btn_send)
        lay.addLayout(cmd_lay)

        self._send_callback = None

    def set_send_callback(self, cb):
        self._send_callback = cb
        self._cmd_input.returnPressed.connect(self._send_cmd)

    def _send_cmd(self):
        txt = self._cmd_input.text().strip()
        if txt and self._send_callback:
            try:
                cmd = json.loads(txt)
                self._send_callback(cmd)
                self.append(f"[TX] {txt}", C_GOLD)
                self._cmd_input.clear()
            except json.JSONDecodeError:
                self.append("[ERR] JSON invalide", C_RED)

    def append(self, line: str, color: str = C_TEXT):
        ts = datetime.now().strftime("%H:%M:%S")
        cursor = self._text.textCursor()
        cursor.movePosition(QTextCursor.MoveOperation.End)
        fmt = QTextCharFormat()
        fmt.setForeground(QColor(C_TEXT_MUT))
        cursor.insertText(f"{ts} ", fmt)
        fmt.setForeground(QColor(color))
        cursor.insertText(line + "\n", fmt)
        if self._chk_scroll.isChecked():
            self._text.setTextCursor(cursor)
            self._text.ensureCursorVisible()

    def _clear(self):
        self._text.clear()

    def log_raw(self, line: str):
        color = C_TEXT
        if line.startswith("[E]"):   color = C_RED
        elif line.startswith("[W]"): color = C_AMBER
        elif line.startswith("[I]"): color = "#7dc4e0"
        elif line.startswith("[D]"): color = C_TEXT_DIM
        elif line.startswith("[TX]"):color = C_GOLD
        self.append(line, color)


# ─── Fenêtre principale ───────────────────────────────────────────────────────
class LexaCareMonitor(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("LexaCare V1 — Monitoring Station")
        self.setMinimumSize(1400, 860)
        self.resize(1600, 920)
        self.setStyleSheet(QSS)

        self._worker: SerialWorker | DataSimulator | None = None
        self._setup_ui()
        self._blink_timer = QTimer()
        self._blink_timer.timeout.connect(self._blink_alert)
        self._blink_state = False

    def _setup_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # ── Header ──────────────────────────────────────────────────────────
        self._header = HeaderBar()
        self._header.connect_requested.connect(self._connect_serial)
        self._header.disconnect_requested.connect(self._disconnect)
        self._header.sim_requested.connect(self._start_simulation)
        root.addWidget(self._header)

        # ── Zone contenu principale ─────────────────────────────────────────
        content = QWidget()
        content_lay = QVBoxLayout(content)
        content_lay.setContentsMargins(12, 12, 12, 8)
        content_lay.setSpacing(8)
        root.addWidget(content)

        # === Ligne 1 : LIDAR + MLX + Radar métriques ========================
        row1 = QHBoxLayout()
        row1.setSpacing(8)

        # --- LIDAR depth map ---
        card_lidar = self._card()
        cl = QVBoxLayout(card_lidar)
        cl.setContentsMargins(10, 8, 10, 8)
        cl.setSpacing(4)
        header_lidar = QLabel("⬛  LIDAR VL53L8CX  —  CARTE DE PROFONDEUR  8 × 32")
        header_lidar.setStyleSheet(
            f"color: {C_CYAN}; font-size: 10px; font-weight: 700; "
            f"letter-spacing: 1.5px;")
        cl.addWidget(header_lidar)
        self._lidar_heatmap = HeatmapWidget(
            LIDAR_ROWS, LIDAR_COLS,
            title="Distance (mm) · 8 rangées × 32 colonnes",
            cmap='plasma_r', vmin=0, vmax=4000, unit="mm")
        cl.addWidget(self._lidar_heatmap)

        # Légende LIDARs
        leg = QHBoxLayout()
        leg.setSpacing(12)
        self._lidar_badges: list[QLabel] = []
        for i in range(1, 5):
            b = QLabel(f"LIDAR {i}")
            b.setProperty("class", "badge-ok")
            b.setAlignment(Qt.AlignmentFlag.AlignCenter)
            b.setFixedWidth(64)
            leg.addWidget(b)
            self._lidar_badges.append(b)
        leg.addStretch()
        cl.addLayout(leg)
        row1.addWidget(card_lidar, 3)

        # --- MLX thermique ---
        card_mlx = self._card()
        cm = QVBoxLayout(card_mlx)
        cm.setContentsMargins(10, 8, 10, 8)
        cm.setSpacing(4)
        header_mlx = QLabel("🌡  MLX90640  —  CAMÉRA THERMIQUE  32 × 24")
        header_mlx.setStyleSheet(
            f"color: {C_AMBER}; font-size: 10px; font-weight: 700; "
            f"letter-spacing: 1.5px;")
        cm.addWidget(header_mlx)
        self._mlx_heatmap = HeatmapWidget(
            MLX_ROWS, MLX_COLS,
            title="Température · 32×24 pixels",
            cmap='inferno', vmin=20, vmax=40, unit="°C")
        self._mlx_heatmap.set_offline("◌  HORS LIGNE\n\nDriver MLX non implémenté\nAffichage disponible prochainement")
        cm.addWidget(self._mlx_heatmap)

        # Métriques radar
        rad_lay = QHBoxLayout()
        rad_lay.setSpacing(16)
        for lbl, attr in [("RESP. bpm", "_lbl_breath"),
                           ("CARDIO bpm", "_lbl_heart"),
                           ("DIST. mm", "_lbl_dist")]:
            col = QVBoxLayout()
            col.setSpacing(0)
            v = QLabel("—")
            v.setStyleSheet(
                f"color: {C_AMBER}; font-size: 16px; font-weight: 800;")
            v.setAlignment(Qt.AlignmentFlag.AlignCenter)
            t = QLabel(lbl)
            t.setStyleSheet(
                f"color: {C_TEXT_DIM}; font-size: 8px; letter-spacing: 1px;")
            t.setAlignment(Qt.AlignmentFlag.AlignCenter)
            col.addWidget(v)
            col.addWidget(t)
            rad_lay.addLayout(col)
            setattr(self, attr, v)
        rad_lay.addStretch()
        cm.addLayout(rad_lay)
        row1.addWidget(card_mlx, 2)
        content_lay.addLayout(row1, 3)

        # === Ligne 2 : AI + Système + Validation ============================
        row2 = QHBoxLayout()
        row2.setSpacing(8)

        # --- IA ---
        card_ai = self._card()
        lay_ai = QVBoxLayout(card_ai)
        lay_ai.setContentsMargins(0, 0, 0, 0)
        lay_ai.setSpacing(0)
        self._ai_widget = AIStatusWidget()
        lay_ai.addWidget(self._ai_widget)
        row2.addWidget(card_ai, 3)

        # --- Métriques système ---
        card_sys = self._card()
        lay_sys = QVBoxLayout(card_sys)
        lay_sys.setContentsMargins(0, 0, 0, 0)
        lay_sys.setSpacing(0)
        self._sys_metrics = SystemMetrics()
        lay_sys.addWidget(self._sys_metrics)
        row2.addWidget(card_sys, 2)

        # --- Validation ---
        card_val = self._card()
        lay_val = QVBoxLayout(card_val)
        lay_val.setContentsMargins(0, 0, 0, 0)
        lay_val.setSpacing(0)
        self._validation = ValidationPanel()
        self._validation.command_requested.connect(self._send_command)
        lay_val.addWidget(self._validation)
        row2.addWidget(card_val, 2)

        content_lay.addLayout(row2, 2)

        # === Ligne 3 : Console =============================================
        card_console = self._card()
        self._console = ConsoleWidget()
        self._console.set_send_callback(self._send_command)
        cl2 = QVBoxLayout(card_console)
        cl2.setContentsMargins(10, 8, 10, 8)
        cl2.addWidget(self._console)
        content_lay.addWidget(card_console, 1)

        self._console.append(
            "LexaCare Monitor prêt · Cliquez CONNECTER ou DÉMO pour commencer", C_CYAN)

    @staticmethod
    def _card(accent: bool = False) -> QFrame:
        """Retourne un QFrame stylisé SANS layout — le caller en crée un."""
        f = QFrame()
        f.setProperty("class", "card-accent" if accent else "card")
        return f

    # ── Connexion / déconnexion ─────────────────────────────────────────────
    def _connect_serial(self, port: str, baud: int):
        self._disconnect()
        self._worker = SerialWorker(port, baud)
        self._worker.data_received.connect(self._on_data)
        self._worker.raw_log.connect(self._console.log_raw)
        self._worker.connection_changed.connect(self._header.set_connected)
        self._worker.start()
        self._console.append(f"Connexion sur {port} @ {baud} bauds…", C_AMBER)

    def _start_simulation(self):
        self._disconnect()
        self._worker = DataSimulator()
        self._worker.data_received.connect(self._on_data)
        self._worker.raw_log.connect(self._console.log_raw)
        self._worker.start()
        self._header.set_connected(True)
        self._console.append("Mode SIMULATION activé (données générées)", C_GOLD)

    def _disconnect(self):
        if self._worker:
            self._worker.stop()
            self._worker.wait(2000)
            self._worker = None
        self._header.set_connected(False)

    def _send_command(self, cmd: dict):
        if isinstance(self._worker, SerialWorker):
            self._worker.send(cmd)

    # ── Traitement des données firmware ────────────────────────────────────
    def _on_data(self, data: dict):
        # LIDAR
        mat = data.get("lidar_matrix")
        if mat:
            self._lidar_heatmap.update_data(mat)

        # MLX (futur)
        mlx = data.get("mlx_matrix")
        if mlx:
            self._mlx_heatmap.set_offline("")
            self._mlx_heatmap.update_data(mlx)

        # IA
        ai = data.get("last_ai_event", {})
        self._ai_widget.update_ai(ai.get("state", 0), ai.get("confidence", 0))
        if ai.get("state") == 1:
            self._blink_timer.start(400)
        else:
            self._blink_timer.stop()

        # Radar
        radar = data.get("radar", {})
        if radar:
            self._lbl_breath.setText(f"{radar.get('breath_bpm', 0):.0f}")
            self._lbl_heart.setText(f"{radar.get('heart_bpm', 0):.0f}")
            self._lbl_dist.setText(f"{radar.get('distance_mm', 0):.0f}")

        # Système
        self._sys_metrics.update(data)

    def _blink_alert(self):
        self._blink_state = not self._blink_state
        for b in self._lidar_badges:
            b.setStyleSheet(
                f"color: {'#ff0000' if self._blink_state else C_RED}; "
                f"background: {'#5a0020' if self._blink_state else '#3d0015'}; "
                f"border: 2px solid {C_RED}; border-radius: 4px; "
                f"padding: 2px 8px; font-size: 11px; font-weight: 700;")

    def closeEvent(self, event):
        self._disconnect()
        event.accept()


# ─── Point d'entrée ──────────────────────────────────────────────────────────
def main():
    app = QApplication(sys.argv)
    app.setApplicationName("LexaCare Monitor")
    app.setOrganizationName("Hexacore")

    # Palette globale sombre
    pal = QPalette()
    pal.setColor(QPalette.ColorRole.Window,     QColor(C_BG))
    pal.setColor(QPalette.ColorRole.WindowText, QColor(C_TEXT))
    pal.setColor(QPalette.ColorRole.Base,       QColor(C_SURFACE))
    pal.setColor(QPalette.ColorRole.Text,       QColor(C_TEXT))
    pal.setColor(QPalette.ColorRole.Button,     QColor(C_SURFACE2))
    pal.setColor(QPalette.ColorRole.ButtonText, QColor(C_TEXT))
    pal.setColor(QPalette.ColorRole.Highlight,  QColor(C_CYAN_DIM))
    app.setPalette(pal)

    win = LexaCareMonitor()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
