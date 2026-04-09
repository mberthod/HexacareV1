"""
frame_parser.py — Parseur de trames JSON Lexacare Studio
=========================================================
Transforme les lignes brutes reçues depuis le port série en objets
typés (SensorFrame, SysFrame, LogChunk).

Deux formats firmware sont supportés de façon transparente :

Format cible (nouveau) :
    {"type": "sensor", "lidar_matrix": [[...]], "radar": {...}, "ai_event": "..."}
    {"type": "sys",    "cpu_load": [...], "heap_free": ..., "psram_free": ..., ...}

Format legacy (pc_diag.c actuel) :
    {"uptime_s": ..., "heap_free_internal": ..., "heap_free_psram": ...,
     "tasks": [...], "last_ai_event": {"state": 0, "confidence": 0}}

Le parseur ne lève jamais d'exception vers l'appelant :
    · JSON invalide    → retourne None, incrémente malformed_count
    · Champ manquant   → valeur par défaut via .get()
    · Matrice invalide → matrice de zéros (8×32)
"""

from __future__ import annotations

import json
import logging
import time
from typing import Optional, Union

import numpy as np

from .data_model import (
    LIDAR_ROWS, LIDAR_COLS,
    AI_NORMAL, AI_CHUTE_DETECTEE, AI_MOUVEMENT_ANORMAL,
    LogChunk, RadarData, SensorFrame, SysFrame,
)

log = logging.getLogger(__name__)

# Alias de type pour le résultat du parseur
ParseResult = Optional[Union[SensorFrame, SysFrame, LogChunk]]

# Mapping état IA (format legacy int → string)
_AI_STATE_MAP: dict[int, str] = {
    0: AI_NORMAL,
    1: AI_CHUTE_DETECTEE,
    2: AI_MOUVEMENT_ANORMAL,
}


class FrameParser:
    """
    Parseur stateless de trames JSON ligne par ligne.

    Usage::

        parser = FrameParser()
        result = parser.parse_line(raw_line)
        if isinstance(result, SensorFrame):
            ...
        elif isinstance(result, SysFrame):
            ...

    Attributes:
        malformed_count:  Nombre cumulé de lignes non parsées.
        parsed_count:     Nombre cumulé de trames parsées avec succès.
        in_log_transfer:  True si un transfert de log est en cours.
    """

    def __init__(self) -> None:
        self.malformed_count: int = 0
        self.parsed_count:    int = 0
        self.in_log_transfer: bool = False
        self._log_file_size:  int = 0

    # ── Point d'entrée public ────────────────────────────────────────────────

    def parse_line(self, raw: str) -> ParseResult:
        """
        Parse une ligne texte brute reçue depuis le port série.

        Args:
            raw: Ligne décodée (sans \\n terminal).

        Returns:
            SensorFrame, SysFrame, LogChunk, ou None si non parsable.
        """
        line = raw.strip()
        if not line:
            return None

        # Détection transfert de log (données brutes non-JSON)
        if self.in_log_transfer:
            return self._handle_log_data(line)

        # Tentative de parse JSON
        if not line.startswith("{"):
            log.debug("Ligne ignorée (non-JSON) : %.60s", line)
            return None

        try:
            data: dict = json.loads(line)
        except json.JSONDecodeError as exc:
            self.malformed_count += 1
            log.warning("JSON invalide [%d] : %s | %.80s", self.malformed_count, exc, line)
            return None

        result = self._dispatch(data)
        if result is not None:
            self.parsed_count += 1
        return result

    # ── Dispatch selon le format ─────────────────────────────────────────────

    def _dispatch(self, data: dict) -> ParseResult:
        """Sélectionne le parser selon la présence et la valeur de "type"."""
        frame_type = data.get("type")

        if frame_type == "sensor":
            return self._parse_sensor_frame(data)

        if frame_type == "sys":
            return self._parse_sys_frame(data)

        # Format legacy : détection par clé caractéristique
        if "uptime_s" in data or "heap_free_internal" in data:
            return self._parse_legacy_frame(data)

        # Réponse à "download_logs" → début de transfert
        if data.get("status") == "ok" and "filename" in data:
            return self._handle_log_header(data)

        # Fin de transfert
        if data.get("status") == "done":
            return self._handle_log_done()

        log.debug("Trame de type inconnu ignorée : %s", list(data.keys()))
        return None

    # ── Format cible : sensor ────────────────────────────────────────────────

    def _parse_sensor_frame(self, data: dict) -> Optional[SensorFrame]:
        """Parse {"type": "sensor", "lidar_matrix": ..., "radar": ..., "ai_event": ...}."""
        matrix = self._parse_lidar_matrix(data.get("lidar_matrix"))
        radar_raw = data.get("radar", {})
        radar = RadarData.from_dict(radar_raw) if isinstance(radar_raw, dict) else RadarData.empty()
        ai_event = str(data.get("ai_event", AI_NORMAL))
        confidence = int(data.get("confidence", 0))

        return SensorFrame(
            timestamp    = time.monotonic(),
            lidar_matrix = matrix,
            radar        = radar,
            ai_event     = ai_event,
            confidence   = confidence,
        )

    # ── Format cible : sys ───────────────────────────────────────────────────

    def _parse_sys_frame(self, data: dict) -> SysFrame:
        """Parse {"type": "sys", "cpu_load": [...], "heap_free": ..., ...}."""
        cpu_raw = data.get("cpu_load", [0, 0])
        cpu_load = [int(x) for x in cpu_raw[:2]] if isinstance(cpu_raw, list) else [0, 0]
        while len(cpu_load) < 2:
            cpu_load.append(0)

        tasks_raw = data.get("tasks_hwm", {})
        tasks_hwm = {str(k): int(v) for k, v in tasks_raw.items()} if isinstance(tasks_raw, dict) else {}

        return SysFrame(
            timestamp  = time.monotonic(),
            uptime_s   = int(data.get("uptime_s", 0)),
            cpu_load   = cpu_load,
            heap_free  = int(data.get("heap_free", 0)),
            psram_free = int(data.get("psram_free", 0)),
            tasks_hwm  = tasks_hwm,
        )

    # ── Format legacy (pc_diag.c actuel) ─────────────────────────────────────

    def _parse_legacy_frame(self, data: dict) -> Union[SensorFrame, SysFrame]:
        """
        Parse le format actuel du firmware :
        {"uptime_s":…, "heap_free_internal":…, "tasks":[…], "last_ai_event":{…}}

        Produit un SysFrame enrichi d'un SensorFrame minimal.
        Le SysFrame est retourné ; un SensorFrame minimal est construit
        à partir du last_ai_event si disponible.
        """
        # Construire SysFrame depuis le format legacy
        tasks_raw = data.get("tasks", [])
        tasks_hwm: dict[str, int] = {}
        if isinstance(tasks_raw, list):
            for t in tasks_raw:
                name = t.get("name", "?")
                hwm  = t.get("hwm_bytes", t.get("usStackHighWaterMark", 0))
                tasks_hwm[name] = int(hwm) * 4 if hwm < 1000 else int(hwm)

        sys_frame = SysFrame(
            timestamp  = time.monotonic(),
            uptime_s   = int(data.get("uptime_s", 0)),
            cpu_load   = [0, 0],   # non fourni par le firmware actuel
            heap_free  = int(data.get("heap_free_internal", 0)),
            psram_free = int(data.get("heap_free_psram", 0)),
            tasks_hwm  = tasks_hwm,
        )

        # Extraire l'événement IA (legacy: state int + confidence int)
        ai_raw = data.get("last_ai_event", {})
        if isinstance(ai_raw, dict):
            state_int  = int(ai_raw.get("state", 0))
            confidence = int(ai_raw.get("confidence", 0))
            ai_event   = _AI_STATE_MAP.get(state_int, AI_NORMAL)
            # Enrichir le SysFrame avec l'info AI pour usage par les widgets
            sys_frame.__dict__["_ai_event"]   = ai_event
            sys_frame.__dict__["_confidence"] = confidence

        return sys_frame

    # ── Gestion des transferts de logs ───────────────────────────────────────

    def _handle_log_header(self, data: dict) -> LogChunk:
        """Initialise le transfert de log depuis la réponse d'en-tête."""
        self.in_log_transfer = True
        self._log_file_size = int(data.get("size", 0))
        header_json = json.dumps(data).encode()
        log.info("Début transfert log — taille annoncée : %d octets", self._log_file_size)
        return LogChunk(
            timestamp = time.monotonic(),
            raw_bytes = header_json,
            is_header = True,
            file_size = self._log_file_size,
        )

    def _handle_log_data(self, line: str) -> LogChunk:
        """Reçoit un bloc de données du log en cours de transfert."""
        # Détecter la fin de transfert ({"status":"done"})
        try:
            d = json.loads(line)
            if d.get("status") == "done":
                return self._handle_log_done()
            if d.get("status") == "error":
                self.in_log_transfer = False
                log.warning("Erreur lors du transfert de log : %s", d.get("msg"))
                return LogChunk(
                    timestamp = time.monotonic(),
                    raw_bytes = line.encode(),
                    is_done   = True,
                )
        except json.JSONDecodeError:
            pass  # Données brutes normales (contenu du fichier)

        return LogChunk(
            timestamp = time.monotonic(),
            raw_bytes = (line + "\n").encode(),
        )

    def _handle_log_done(self) -> LogChunk:
        """Signale la fin du transfert de log."""
        self.in_log_transfer = False
        log.info("Transfert log terminé.")
        return LogChunk(
            timestamp = time.monotonic(),
            raw_bytes = b"",
            is_done   = True,
        )

    # ── Utilitaires ──────────────────────────────────────────────────────────

    @staticmethod
    def _parse_lidar_matrix(raw: object) -> np.ndarray:
        """
        Convertit la valeur JSON "lidar_matrix" en ndarray (8, 32) float32.
        Retourne une matrice de zéros si le format est invalide.
        """
        if not isinstance(raw, list):
            return np.zeros((LIDAR_ROWS, LIDAR_COLS), dtype=np.float32)

        try:
            arr = np.array(raw, dtype=np.float32)
            if arr.shape == (LIDAR_ROWS, LIDAR_COLS):
                return arr
            if arr.size == LIDAR_ROWS * LIDAR_COLS:
                return arr.reshape((LIDAR_ROWS, LIDAR_COLS))
            log.warning("Matrice LIDAR forme inattendue : %s", arr.shape)
        except (ValueError, TypeError) as exc:
            log.warning("Matrice LIDAR invalide : %s", exc)

        return np.zeros((LIDAR_ROWS, LIDAR_COLS), dtype=np.float32)

    def reset_stats(self) -> None:
        """Réinitialise les compteurs de diagnostic."""
        self.malformed_count = 0
        self.parsed_count    = 0

    def stats(self) -> dict[str, int]:
        """Retourne un dictionnaire des statistiques courantes."""
        return {
            "parsed":    self.parsed_count,
            "malformed": self.malformed_count,
        }
