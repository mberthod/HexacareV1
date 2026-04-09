"""
heatmap_widget.py — Module A : LIDAR 8×32 Depth Heatmap
=========================================================
Affiche la matrice de profondeur des 4 capteurs VL53L8CX (LIDAR 1–4)
fusionnée en une image unifiée 8 rangées × 32 colonnes.

Fonctionnalités :
  · Heatmap PyQtGraph ImageItem (palette inferno — accélération matérielle)
  · 4 secteurs visuels (LIDAR 1 = col 0-7, LIDAR 2 = col 8-15, ...)
  · Niveaux automatiques ou manuels (0–4000 mm par défaut)
  · Interpolation bilinéaire activable (numpy, sans scipy)
  · Bounding box rouge animée en cas d'événement AI_CHUTE_DETECTEE
  · Barre colorimétrique latérale (distance en mm)
  · Statistiques temps réel : distance min/max, FPS
"""

from __future__ import annotations

import math
import time
from typing import Optional

import numpy as np
import pyqtgraph as pg
from PyQt6.QtCore import Qt, QTimer, pyqtSlot
from PyQt6.QtGui import QFont
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)

from ..data_model import AI_CHUTE_DETECTEE, AI_MOUVEMENT_ANORMAL, SensorFrame

# ─── Constantes LIDAR ────────────────────────────────────────────────────────

_ROWS          = 8
_COLS          = 32
_COLS_PER_SENSOR = 8
_N_SENSORS     = 4

_SENSOR_LABELS = ["LIDAR 1", "LIDAR 2", "LIDAR 3", "LIDAR 4"]
_SECTOR_COLORS = ["#00d4ff", "#00ff88", "#ffc400", "#a855f7"]

_DIST_MIN_MM  = 100.0     # Valeur minimale de la plage d'affichage
_DIST_MAX_MM  = 4000.0    # Valeur maximale de la plage d'affichage

# Niveaux ImageItem : inverted → close objects appear BRIGHT (hot)
_LEVELS_DEFAULT = (_DIST_MAX_MM, _DIST_MIN_MM)


# ─── Helpers ─────────────────────────────────────────────────────────────────

def _bilinear_upsample(data: np.ndarray, factor: int) -> np.ndarray:
    """
    Interpolation bilinéaire pure numpy (sans scipy).

    Args:
        data:   Tableau 2D float32 d'entrée.
        factor: Facteur d'agrandissement (ex : 4 → ×4 en chaque dimension).

    Returns:
        Tableau agrandi de shape (h*factor, w*factor).
    """
    h, w = data.shape
    nh, nw = h * factor, w * factor

    y = np.linspace(0, h - 1, nh)
    x = np.linspace(0, w - 1, nw)

    y0 = np.floor(y).astype(int).clip(0, h - 1)
    y1 = np.minimum(y0 + 1, h - 1)
    x0 = np.floor(x).astype(int).clip(0, w - 1)
    x1 = np.minimum(x0 + 1, w - 1)

    fy = (y - y0).reshape(-1, 1)
    fx = (x - x0).reshape(1, -1)

    result = (
        data[np.ix_(y0, x0)] * (1 - fy) * (1 - fx)
        + data[np.ix_(y0, x1)] * (1 - fy) * fx
        + data[np.ix_(y1, x0)] * fy * (1 - fx)
        + data[np.ix_(y1, x1)] * fy * fx
    )
    return result.astype(np.float32)


# ─── HeatmapWidget ────────────────────────────────────────────────────────────

class HeatmapWidget(QWidget):
    """
    Module A — Affichage de la matrice LIDAR 8×32 en heatmap temps réel.

    Ce widget est conçu pour fonctionner à 15 Hz sans dégradation de
    performance grâce à l'accélération matérielle de PyQtGraph.
    """

    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._auto_levels:  bool  = False
        self._interp:       bool  = False
        self._interp_factor: int  = 4
        self._levels: tuple[float, float] = _LEVELS_DEFAULT
        self._frame_count:  int   = 0
        self._last_fps_ts:  float = time.monotonic()
        self._fps:          float = 0.0

        # Blink state for fall alert
        self._blink_timer = QTimer(self)
        self._blink_timer.timeout.connect(self._blink_fall_box)
        self._blink_state: bool = False

        self._setup_ui()

    # ── Construction UI ───────────────────────────────────────────────────────

    def _setup_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(6, 4, 6, 4)
        root.setSpacing(4)

        # Toolbar
        root.addLayout(self._build_toolbar())

        # PyQtGraph canvas (stretches to fill available space)
        self._canvas = pg.GraphicsLayoutWidget()
        self._canvas.setBackground("#04080f")
        self._canvas.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        root.addWidget(self._canvas, stretch=1)

        self._setup_pg()

        # AI event status bar
        self._lbl_ai = QLabel("●  AI : NORMAL")
        self._lbl_ai.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._lbl_ai.setStyleSheet(
            "color: #00ff88; font-size: 11px; font-weight: 700; padding: 2px;")
        root.addWidget(self._lbl_ai)

    def _build_toolbar(self) -> QHBoxLayout:
        bar = QHBoxLayout()
        bar.setSpacing(10)

        lbl_title = QLabel("⬛  LIDAR DEPTH MAP — 4 × VL53L8CX  /  8×32 zones")
        lbl_title.setStyleSheet(
            "color: #00d4ff; font-size: 11px; font-weight: 700; letter-spacing: 1px;")
        bar.addWidget(lbl_title)
        bar.addStretch()

        self._lbl_min = QLabel("Proche : —")
        self._lbl_min.setStyleSheet("color: #ffd700; font-size: 11px;")
        bar.addWidget(self._lbl_min)

        self._lbl_max = QLabel("Loin : —")
        self._lbl_max.setStyleSheet("color: #4a6fa5; font-size: 11px;")
        bar.addWidget(self._lbl_max)

        self._lbl_fps = QLabel("0 Hz")
        self._lbl_fps.setStyleSheet("color: #4a6fa5; font-size: 10px;")
        bar.addWidget(self._lbl_fps)

        self._btn_interp = self._make_toggle_btn("Interp. OFF", "#4a6fa5", "#00d4ff")
        self._btn_interp.clicked.connect(self._toggle_interp)
        bar.addWidget(self._btn_interp)

        self._btn_auto = self._make_toggle_btn("Auto-niveaux", "#4a6fa5", "#00ff88")
        self._btn_auto.clicked.connect(self._toggle_auto)
        bar.addWidget(self._btn_auto)

        return bar

    @staticmethod
    def _make_toggle_btn(text: str, color_off: str, color_on: str) -> QPushButton:
        btn = QPushButton(text)
        btn.setCheckable(True)
        btn.setFixedWidth(100)
        btn.setStyleSheet(f"""
            QPushButton {{
                background: #0f2040; color: {color_off};
                border: 1px solid #1a3a5c; border-radius: 4px;
                padding: 3px 8px; font-size: 10px;
            }}
            QPushButton:checked {{
                background: #001a30; color: {color_on};
                border-color: {color_on};
            }}
        """)
        return btn

    # ── PyQtGraph setup ───────────────────────────────────────────────────────

    def _setup_pg(self) -> None:
        """Initialise le plot PyQtGraph avec ImageItem, colorbar, overlays."""

        # --- Plot item ---
        self._plot = self._canvas.addPlot(row=0, col=0)
        self._plot.setTitle(
            "<span style='color:#1e3a5c;font-size:9pt'>"
            "Distance (mm) — 100 (proche / chaud) → 4000 (loin / froid)</span>")
        self._plot.setLabel(
            "left",
            "<span style='color:#2a4a7c;font-size:9pt'>Rangée</span>")
        self._plot.setLabel(
            "bottom",
            "<span style='color:#2a4a7c;font-size:9pt'>Zone (col.)</span>")

        # Style des axes
        for axis in ("left", "bottom", "right", "top"):
            ax = self._plot.getAxis(axis)
            ax.setPen(pg.mkPen("#1a3a5c"))
            ax.setTextPen(pg.mkPen("#2a4a7c"))
            ax.setStyle(tickFont=QFont("Segoe UI", 7))

        self._plot.setMouseEnabled(x=False, y=False)
        self._plot.hideButtons()
        self._plot.showGrid(x=False, y=False)

        # --- ImageItem ---
        self._img = pg.ImageItem()
        self._plot.addItem(self._img)

        # Colormap inferno (built-in PyQtGraph ≥ 0.12)
        try:
            cmap = pg.colormap.get("inferno")
        except KeyError:
            cmap = pg.colormap.get("CET-L1")  # Fallback
        self._img.setColorMap(cmap)
        self._img.setLevels(_LEVELS_DEFAULT)

        # Initial black frame — shape (COLS, ROWS) for ImageItem convention
        self._img.setImage(
            np.zeros((_COLS, _ROWS), dtype=np.float32),
            autoLevels=False,
        )

        # Fix view range: x=0..32, y=0..8
        self._plot.setXRange(0, _COLS, padding=0.02)
        self._plot.setYRange(0, _ROWS, padding=0.02)

        # --- ColorBar (pyqtgraph ≥ 0.12.3) ---
        try:
            self._colorbar = self._canvas.addColorBar(
                self._img,
                colorMap=cmap,
                values=(_DIST_MIN_MM, _DIST_MAX_MM),
                label="mm",
            )
        except Exception:
            self._colorbar = None  # Not critical

        # --- LIDAR sector separator lines ---
        for s in range(1, _N_SENSORS):
            x = s * _COLS_PER_SENSOR
            line = pg.InfiniteLine(
                pos=x,
                angle=90,
                pen=pg.mkPen(color="#00d4ff", width=1.2,
                             style=Qt.PenStyle.DashLine),
            )
            self._plot.addItem(line)

        # --- Sector labels ---
        font_lbl = QFont("Segoe UI", 8, QFont.Weight.Bold)
        for i, (label, color) in enumerate(zip(_SENSOR_LABELS, _SECTOR_COLORS)):
            x_center = i * _COLS_PER_SENSOR + _COLS_PER_SENSOR / 2
            txt = pg.TextItem(text=label, color=color, anchor=(0.5, 0.0))
            txt.setFont(font_lbl)
            self._plot.addItem(txt)
            # Place label just above row 0 (y=8 = top of plot since y-axis may be inverted)
            txt.setPos(x_center, _ROWS - 0.1)

        # --- Fall detection bounding box ---
        # Drawn as a closed PlotDataItem curve for clean no-handle rendering
        xs = [0, _COLS, _COLS, 0, 0]
        ys = [0, 0, _ROWS, _ROWS, 0]
        self._fall_curve = pg.PlotDataItem(
            x=xs, y=ys,
            pen=pg.mkPen("#ff3366", width=3),
        )
        self._fall_curve.hide()
        self._plot.addItem(self._fall_curve)

        # --- Row axis ticks ---
        self._plot.getAxis("left").setTicks([
            [(i, str(i)) for i in range(_ROWS + 1)]
        ])
        self._plot.getAxis("bottom").setTicks([
            [(i * _COLS_PER_SENSOR, str(i * _COLS_PER_SENSOR))
             for i in range(_N_SENSORS + 1)]
        ])

    # ── Slot principal ────────────────────────────────────────────────────────

    @pyqtSlot(object)
    def update_sensor(self, frame: SensorFrame) -> None:
        """
        Met à jour la heatmap avec une nouvelle SensorFrame.
        Appelé depuis le thread UI après réception du signal SerialWorker.

        Args:
            frame: Trame capteur contenant lidar_matrix (8×32) et ai_event.
        """
        matrix = frame.lidar_matrix  # shape (8, 32), float32

        # PyQtGraph ImageItem convention : data[x, y] = data[col, row]
        # Notre matrice est [row, col] → transposer pour [col, row]
        data = matrix.T.copy()  # shape (32, 8)

        if self._interp:
            # Interpolation bilinéaire ×4 (32*4=128, 8*4=32)
            data_disp = _bilinear_upsample(data, self._interp_factor)
            cols_disp, rows_disp = data_disp.shape
        else:
            data_disp = data
            cols_disp, rows_disp = _COLS, _ROWS

        if self._auto_levels:
            valid = data[data > 0]
            if valid.size:
                levels = (float(valid.max()), max(float(valid.min()), 1.0))
            else:
                levels = _LEVELS_DEFAULT
        else:
            levels = self._levels

        self._img.setImage(data_disp, autoLevels=False, levels=levels)

        # Synchroniser l'échelle visuelle si interpolation ON/OFF changée
        self._plot.setXRange(0, cols_disp, padding=0.02)
        self._plot.setYRange(0, rows_disp, padding=0.02)

        # --- Stats ---
        valid = matrix[matrix > 0]
        if valid.size:
            self._lbl_min.setText(f"Proche : {valid.min():.0f} mm")
            self._lbl_max.setText(f"Loin : {valid.max():.0f} mm")

        # --- FPS ---
        self._frame_count += 1
        now = time.monotonic()
        elapsed = now - self._last_fps_ts
        if elapsed >= 1.0:
            self._fps = self._frame_count / elapsed
            self._lbl_fps.setText(f"{self._fps:.1f} Hz")
            self._frame_count = 0
            self._last_fps_ts = now

        # --- AI event overlay ---
        self._update_ai_overlay(frame.ai_event, frame.confidence)

    # ── Overlay IA ────────────────────────────────────────────────────────────

    def _update_ai_overlay(self, ai_event: str, confidence: int) -> None:
        """Met à jour le bounding box et la barre de statut IA."""
        if ai_event == AI_CHUTE_DETECTEE:
            if not self._blink_timer.isActive():
                self._blink_timer.start(350)
            self._lbl_ai.setText(
                f"⚠  CHUTE DÉTECTÉE  —  confiance {confidence} %  ⚠")
            self._lbl_ai.setStyleSheet(
                "color: #ff3366; font-size: 13px; font-weight: 900; "
                "background: #3d0015; border-radius: 4px; padding: 3px;")
        elif ai_event == AI_MOUVEMENT_ANORMAL:
            self._blink_timer.stop()
            self._fall_curve.show()
            self._fall_curve.setPen(pg.mkPen("#ffc400", width=2))
            self._lbl_ai.setText(f"~  MOUVEMENT ANORMAL  —  {confidence} %")
            self._lbl_ai.setStyleSheet(
                "color: #ffc400; font-size: 11px; font-weight: 700; "
                "background: #2a1500; border-radius: 4px; padding: 2px;")
        else:
            self._blink_timer.stop()
            self._fall_curve.hide()
            self._lbl_ai.setText("●  AI : NORMAL")
            self._lbl_ai.setStyleSheet(
                "color: #00ff88; font-size: 11px; font-weight: 700; padding: 2px;")

    @pyqtSlot()
    def _blink_fall_box(self) -> None:
        """Fait clignoter le bounding box rouge lors d'une chute."""
        self._blink_state = not self._blink_state
        if self._blink_state:
            self._fall_curve.show()
            self._fall_curve.setPen(pg.mkPen("#ff3366", width=4))
        else:
            self._fall_curve.hide()

    # ── Contrôles ─────────────────────────────────────────────────────────────

    @pyqtSlot()
    def _toggle_interp(self) -> None:
        """Active / désactive l'interpolation bilinéaire."""
        self._interp = self._btn_interp.isChecked()
        self._btn_interp.setText("Interp. ON" if self._interp else "Interp. OFF")

    @pyqtSlot()
    def _toggle_auto(self) -> None:
        """Active / désactive les niveaux automatiques."""
        self._auto_levels = self._btn_auto.isChecked()
        self._btn_auto.setText(
            "Auto ON" if self._auto_levels else "Auto-niveaux")
