"""
simulator.py — Générateur de données LIDAR synthétiques Lexacare Studio
========================================================================
SimulatorWorker : QThread émettant les mêmes signaux que SerialWorker,
permettant de tester et démontrer l'interface sans matériel connecté.

Scénario simulé :
  · Fond : mur / plafond à ~3000 mm (bruit gaussien réaliste)
  · Personne : blob de profondeur ~600 mm se déplaçant horizontalement
  · Chute : toutes les ~30 s, la personne « tombe » (distance plancher ~200 mm)
            et l'événement AI_CHUTE_DETECTEE est émis pendant ~3 s
  · Radar : ondes respiratoire et cardiaque synthétiques (sinus + bruit)
  · Système : métriques CPU/heap variant progressivement
"""

from __future__ import annotations

import math
import random
import time
from typing import Optional

import numpy as np
from PyQt6.QtCore import QThread, pyqtSignal

from .data_model import (
    AI_CHUTE_DETECTEE,
    AI_MOUVEMENT_ANORMAL,
    AI_NORMAL,
    RadarData,
    SensorFrame,
    SysFrame,
)

# ─── Paramètres de la scène ───────────────────────────────────────────────────

_ROWS          = 8
_COLS          = 32
_FPS           = 15          # Hz — fréquence d'acquisition simulée
_DT            = 1.0 / _FPS

_WALL_DIST_MM  = 3000.0     # Distance fond / mur
_PERSON_DIST_MM = 620.0     # Distance de la personne (corps)
_FALL_DIST_MM  = 210.0      # Distance au sol lors d'une chute
_BLOB_SIGMA_COL = 2.5       # Largeur gaussienne du blob personne (colonnes)
_BLOB_SIGMA_ROW = 1.5       # Largeur gaussienne du blob personne (rangées)

_FALL_PERIOD_S  = 30.0      # Intervalle entre les chutes simulées
_FALL_DURATION_S = 3.5      # Durée de l'événement chute
_MOVE_SPEED     = 0.08      # Vitesse de déplacement horizontal (rad/s)


# ─── SimulatorWorker ──────────────────────────────────────────────────────────

class SimulatorWorker(QThread):
    """
    QThread de simulation de données LexaCare.

    Émet les mêmes signaux que SerialWorker :
      · sensor_data_received(SensorFrame)
      · sys_data_received(SysFrame)
      · connection_state_changed(bool, str)

    Usage::

        sim = SimulatorWorker()
        sim.sensor_data_received.connect(widget.update_sensor)
        sim.start()
        ...
        sim.stop(); sim.wait()
    """

    sensor_data_received:     pyqtSignal = pyqtSignal(object)
    sys_data_received:        pyqtSignal = pyqtSignal(object)
    connection_state_changed: pyqtSignal = pyqtSignal(bool, str)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._running: bool = False
        self._rng = np.random.default_rng(seed=42)

    def stop(self) -> None:
        """Demande l'arrêt du simulateur."""
        self._running = False

    def run(self) -> None:
        """Boucle principale de génération de données."""
        self._running = True
        self.connection_state_changed.emit(True, "Simulation")

        t            = 0.0          # Temps simulé (secondes)
        next_sys     = 0.0          # Prochain envoi SysFrame
        sys_interval = 2.0          # Intervalle entre SysFrames (s)
        next_fall    = _FALL_PERIOD_S + random.uniform(-5, 5)
        fall_end     = 0.0
        in_fall      = False
        uptime       = 0

        while self._running:
            t_wall = time.monotonic()

            # ── Événement chute ──────────────────────────────────────────────
            if not in_fall and t >= next_fall:
                in_fall  = True
                fall_end = t + _FALL_DURATION_S

            if in_fall and t >= fall_end:
                in_fall   = False
                next_fall = t + _FALL_PERIOD_S + random.uniform(-5, 5)

            # ── Génération SensorFrame ────────────────────────────────────────
            matrix  = self._gen_lidar_matrix(t, in_fall)
            radar   = self._gen_radar(t)
            ai_evt  = AI_CHUTE_DETECTEE if in_fall else self._ambient_ai(t)
            conf    = 92 if in_fall else (30 if ai_evt == AI_MOUVEMENT_ANORMAL else 0)

            sensor = SensorFrame(
                timestamp    = time.monotonic(),
                lidar_matrix = matrix,
                radar        = radar,
                ai_event     = ai_evt,
                confidence   = conf,
            )
            self.sensor_data_received.emit(sensor)

            # ── Génération SysFrame (moins fréquente) ─────────────────────────
            uptime += 1
            if t >= next_sys:
                next_sys += sys_interval
                sys_frame = self._gen_sys(t, uptime)
                self.sys_data_received.emit(sys_frame)

            # ── Timing précis ─────────────────────────────────────────────────
            t += _DT
            elapsed = time.monotonic() - t_wall
            sleep   = max(0.0, _DT - elapsed)
            if sleep > 0:
                self.msleep(int(sleep * 1000))

        self.connection_state_changed.emit(False, "Simulation arrêtée")

    # ── Générateurs ───────────────────────────────────────────────────────────

    def _gen_lidar_matrix(self, t: float, in_fall: bool) -> np.ndarray:
        """
        Génère une matrice 8×32 réaliste avec une personne et un fond.

        Args:
            t:       Temps simulé (secondes).
            in_fall: True pendant un événement chute.

        Returns:
            ndarray float32 de shape (8, 32).
        """
        # Fond : mur légèrement rugueux (bruit gaussien)
        noise = self._rng.normal(0, 80, (_ROWS, _COLS)).astype(np.float32)
        matrix = np.full((_ROWS, _COLS), _WALL_DIST_MM, dtype=np.float32) + noise

        # Position horizontale de la personne (oscillation lente)
        person_col = 16.0 + 10.0 * math.sin(_MOVE_SPEED * t)
        # Rangée verticale : le corps d'une personne debout couvre les rows 2-6
        person_row_center = 4.0

        if in_fall:
            # ── Chute : corps sur le sol, rangées 4-7 très proches ──────────
            # Pas de mélange gaussien : la personne à terre donne une
            # réflexion nette et uniforme à très courte distance.
            col_lo = max(0, int(person_col) - 5)
            col_hi = min(_COLS, int(person_col) + 6)
            for row in range(4, _ROWS):
                # Distance plancher qui s'intensifie vers le bas
                floor_dist = _FALL_DIST_MM + 50 * (_ROWS - 1 - row)
                for col in range(col_lo, col_hi):
                    matrix[row, col] = float(
                        floor_dist + self._rng.normal(0, 20)
                    )
            # Transition : rangées 1-3 montrent le corps qui tombe encore
            standing_dist = _PERSON_DIST_MM + 40 * math.sin(0.3 * t)
            for row in range(1, 4):
                for col in range(col_lo, col_hi):
                    d_col = (col - person_col) / _BLOB_SIGMA_COL
                    weight = math.exp(-0.5 * d_col**2)
                    if weight > 0.15:
                        matrix[row, col] = float(
                            standing_dist * weight
                            + _WALL_DIST_MM * (1.0 - weight)
                            + self._rng.normal(0, 30)
                        )
        else:
            # ── Personne debout : blob gaussien ─────────────────────────────
            person_row_center = 4.0
            target_dist       = _PERSON_DIST_MM + 40 * math.sin(0.3 * t)
            for row in range(_ROWS):
                for col in range(_COLS):
                    d_col = (col - person_col) / _BLOB_SIGMA_COL
                    d_row = (row - person_row_center) / _BLOB_SIGMA_ROW
                    weight = math.exp(-0.5 * (d_col**2 + d_row**2))
                    if weight > 0.05:
                        matrix[row, col] = float(
                            target_dist * weight
                            + _WALL_DIST_MM * (1.0 - weight)
                            + self._rng.normal(0, 30)
                        )

        return np.clip(matrix, 50.0, 5000.0)

    def _gen_radar(self, t: float) -> RadarData:
        """Génère des données radar synthétiques (ondes vitales)."""
        # Onde respiratoire : ~0.25 Hz (15 cycles/min) + bruit
        resp = 0.8 * math.sin(2 * math.pi * 0.25 * t) + 0.1 * math.sin(0.7 * t)
        resp += self._rng.normal(0, 0.05)

        # Onde cardiaque : ~1.2 Hz (72 BPM) + harmoniques
        heart = (
            0.4 * math.sin(2 * math.pi * 1.2 * t)
            + 0.15 * math.sin(2 * math.pi * 2.4 * t)
            + 0.05 * math.sin(2 * math.pi * 3.6 * t)
        )
        heart += self._rng.normal(0, 0.03)

        breath_rate = int(15 + 2 * math.sin(0.01 * t) + self._rng.normal(0, 0.5))
        heart_rate  = int(72 + 3 * math.sin(0.005 * t) + self._rng.normal(0, 1))

        return RadarData(
            resp_phase  = float(resp),
            heart_phase = float(heart),
            breath_rate = max(8, min(30, breath_rate)),
            heart_rate  = max(50, min(120, heart_rate)),
            distance_mm = int(620 + 30 * math.sin(0.1 * t)),
            presence    = True,
        )

    def _gen_sys(self, t: float, uptime_s: int) -> SysFrame:
        """Génère des métriques système synthétiques."""
        core0 = int(45 + 20 * math.sin(0.05 * t) + self._rng.normal(0, 3))
        core1 = int(75 + 10 * math.sin(0.07 * t + 1) + self._rng.normal(0, 4))
        heap  = int(8_700_000 - 50_000 * math.sin(0.02 * t) + self._rng.normal(0, 5000))
        psram = int(8_300_000 - 20_000 * math.sin(0.01 * t))

        return SysFrame(
            timestamp  = time.monotonic(),
            uptime_s   = uptime_s,
            cpu_load   = [max(0, min(100, core0)), max(0, min(100, core1))],
            heap_free  = max(1_000_000, heap),
            psram_free = max(1_000_000, psram),
            tasks_hwm  = {
                "Task_Sensor_Acq":    2048,
                "Task_AI_Inference":  1800 + int(50 * math.sin(t * 0.1)),
                "Task_Mesh_Com":      1200,
                "Task_Diag_PC":       800,
            },
        )

    @staticmethod
    def _ambient_ai(t: float) -> str:
        """Génère des événements AI ambiants rares (mouvement anormal ~5%)."""
        # Mouvement anormal rare, périodique
        cycle = t % 12.0
        if 5.0 < cycle < 6.5:
            return AI_MOUVEMENT_ANORMAL
        return AI_NORMAL
