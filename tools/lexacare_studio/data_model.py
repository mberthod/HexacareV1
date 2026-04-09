"""
data_model.py — Modèles de données Lexacare Studio
====================================================
Structures de données typées (dataclasses + numpy) représentant
les trames JSON reçues depuis le firmware ESP32-S3.

Deux formats sont supportés :
  · Format cible  : {"type": "sensor"|"sys", …}
  · Format legacy : {"uptime_s":…, "heap_free_internal":…, …}
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Optional

import numpy as np


# ─── Constantes matrice LIDAR ────────────────────────────────────────────────

LIDAR_ROWS: int = 8
LIDAR_COLS: int = 32

# Valeurs sentinelles pour données manquantes
LIDAR_INVALID_MM: float = 0.0
RADAR_INVALID_BPM: int = 0

# Événements IA
AI_NORMAL          = "AI_NORMAL"
AI_CHUTE_DETECTEE  = "AI_CHUTE_DETECTEE"
AI_MOUVEMENT_ANORMAL = "AI_MOUVEMENT_ANORMAL"


# ─── RadarData ───────────────────────────────────────────────────────────────

@dataclass
class RadarData:
    """
    Données vitales extraites du radar HLK-LD6002.

    Attributes:
        resp_phase:   Phase respiratoire (rad), signal brut oscilloscope.
        heart_phase:  Phase cardiaque (rad), signal brut oscilloscope.
        breath_rate:  Fréquence respiratoire calculée (cycles/min).
        heart_rate:   Fréquence cardiaque calculée (bpm).
        distance_mm:  Distance de la cible détectée (mm).
        presence:     True si une personne est détectée dans le champ.
    """
    resp_phase:  float = 0.0
    heart_phase: float = 0.0
    breath_rate: int   = RADAR_INVALID_BPM
    heart_rate:  int   = RADAR_INVALID_BPM
    distance_mm: int   = 0
    presence:    bool  = False

    @classmethod
    def empty(cls) -> "RadarData":
        """Retourne une instance avec toutes les valeurs à zéro."""
        return cls()

    @classmethod
    def from_dict(cls, d: dict) -> "RadarData":
        """Construit depuis un sous-dictionnaire JSON."""
        return cls(
            resp_phase  = float(d.get("resp_phase",   d.get("resp_phase",   0.0))),
            heart_phase = float(d.get("heart_phase",  d.get("heart_phase",  0.0))),
            breath_rate = int(d.get("breath_rate",    d.get("breath_rate",  0))),
            heart_rate  = int(d.get("heart_rate",     d.get("heart_rate",   0))),
            distance_mm = int(d.get("target_distance_mm", d.get("distance_mm", 0))),
            presence    = bool(d.get("presence", False)),
        )


# ─── SensorFrame ─────────────────────────────────────────────────────────────

@dataclass
class SensorFrame:
    """
    Trame capteur complète (LIDAR + Radar + événement IA).
    Produite à ~15 Hz par Task_Sensor_Acq + Task_AI_Inference.

    Attributes:
        timestamp:    Horodatage Python (time.monotonic()).
        lidar_matrix: Matrice 8×32 de distances (mm), float32.
                      Colonne N = LIDAR (N//8), ligne = rangée verticale.
        radar:        Données vitales radar (voir RadarData).
        ai_event:     Événement IA courant ("AI_NORMAL", …).
        confidence:   Confiance de l'événement IA (0–100 %).
    """
    timestamp:    float
    lidar_matrix: np.ndarray          # shape (8, 32), dtype float32
    radar:        RadarData
    ai_event:     str   = AI_NORMAL
    confidence:   int   = 0

    @classmethod
    def make_empty(cls) -> "SensorFrame":
        """Trame vide pour initialisation des widgets."""
        return cls(
            timestamp    = time.monotonic(),
            lidar_matrix = np.zeros((LIDAR_ROWS, LIDAR_COLS), dtype=np.float32),
            radar        = RadarData.empty(),
        )

    @property
    def is_fall_detected(self) -> bool:
        return self.ai_event == AI_CHUTE_DETECTEE

    @property
    def lidar_min_mm(self) -> float:
        valid = self.lidar_matrix[self.lidar_matrix > LIDAR_INVALID_MM]
        return float(valid.min()) if valid.size else 0.0

    @property
    def lidar_max_mm(self) -> float:
        return float(self.lidar_matrix.max())


# ─── SysFrame ────────────────────────────────────────────────────────────────

@dataclass
class SysFrame:
    """
    Trame de diagnostic système (émise toutes les 5 s par Task_Diag_PC).

    Attributes:
        timestamp:   Horodatage Python (time.monotonic()).
        uptime_s:    Uptime firmware (secondes).
        cpu_load:    Charge CPU estimée [core0%, core1%].
        heap_free:   Heap interne libre (octets).
        psram_free:  PSRAM libre (octets).
        tasks_hwm:   Dict {nom_tâche: high_water_mark_bytes}.
    """
    timestamp:  float
    uptime_s:   int            = 0
    cpu_load:   list[int]      = field(default_factory=lambda: [0, 0])
    heap_free:  int            = 0
    psram_free: int            = 0
    tasks_hwm:  dict[str, int] = field(default_factory=dict)

    @classmethod
    def make_empty(cls) -> "SysFrame":
        return cls(timestamp=time.monotonic())

    @property
    def heap_free_kb(self) -> float:
        return self.heap_free / 1024.0

    @property
    def psram_free_mb(self) -> float:
        return self.psram_free / (1024.0 * 1024.0)


# ─── LogChunk ────────────────────────────────────────────────────────────────

@dataclass
class LogChunk:
    """
    Bloc de données reçu lors d'un transfert de log LittleFS.

    Attributes:
        timestamp:   Horodatage Python de réception.
        raw_bytes:   Contenu brut du bloc (peut être texte ou binaire).
        is_header:   True si ce chunk contient l'en-tête de transfert.
        is_done:     True si ce chunk signale la fin du transfert.
        file_size:   Taille totale annoncée par l'en-tête (octets, 0 si inconnu).
    """
    timestamp:  float
    raw_bytes:  bytes
    is_header:  bool = False
    is_done:    bool = False
    file_size:  int  = 0

    @property
    def text(self) -> str:
        """Décode le contenu en UTF-8, remplace les caractères invalides."""
        return self.raw_bytes.decode("utf-8", errors="replace")
