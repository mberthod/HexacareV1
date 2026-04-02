"""
data_logger.py — Enregistrement et rejeu de sessions Lexacare Studio
=====================================================================
Deux modes opératoires :

RECORD MODE
    Enregistre toutes les trames reçues dans un fichier .jsonl
    (une trame JSON sérialisée par ligne). Démarrage/arrêt à chaud.

PLAYBACK MODE
    Relit un fichier .jsonl enregistré et réémet les trames avec
    le même débit temporel que la session originale (ou à vitesse ×N).
    Le PlaybackWorker émet les mêmes signaux que SerialWorker, ce qui
    permet de brancher les widgets sans modification.

Format JSONL :
    {"_ts": 1720000000.123, "_fmt": "sensor", "lidar_matrix": [...], ...}
    {"_ts": 1720000005.001, "_fmt": "sys",    "cpu_load": [...], ...}

Les champs "_ts" et "_fmt" sont ajoutés par DataLogger.
"""

from __future__ import annotations

import json
import logging
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

from PyQt6.QtCore import QThread, pyqtSignal

from .data_model import LogChunk, SensorFrame, SysFrame
from .frame_parser import FrameParser

log = logging.getLogger(__name__)


# ─── DataLogger ──────────────────────────────────────────────────────────────

class DataLogger:
    """
    Enregistreur de session en temps réel au format JSONL.

    Usage::

        logger = DataLogger()
        logger.start_recording()          # crée auto un fichier horodaté
        # … brancher sur les signaux du SerialWorker :
        worker.sensor_data_received.connect(logger.log_sensor)
        worker.sys_data_received.connect(logger.log_sys)
        worker.log_chunk_received.connect(logger.log_chunk)
        # …
        logger.stop_recording()
    """

    def __init__(self, output_dir: Optional[Path] = None) -> None:
        """
        Args:
            output_dir: Répertoire de destination des fichiers.
                        Défaut : sous-répertoire ``recordings/`` à côté du script.
        """
        if output_dir is None:
            output_dir = Path(__file__).parent.parent / "recordings"
        self._output_dir = Path(output_dir)
        self._file = None
        self._path: Optional[Path] = None
        self._recording: bool = False
        self._frame_count: int = 0

    # ── Contrôle ──────────────────────────────────────────────────────────────

    def start_recording(self, filename: Optional[str] = None) -> Path:
        """
        Démarre l'enregistrement dans un fichier JSONL.

        Args:
            filename: Nom de fichier optionnel (sans extension).
                      Défaut : ``session_YYYYMMDD_HHMMSS.jsonl``.

        Returns:
            Chemin absolu du fichier créé.
        """
        if self._recording:
            log.warning("Enregistrement déjà en cours : %s", self._path)
            return self._path  # type: ignore[return-value]

        self._output_dir.mkdir(parents=True, exist_ok=True)
        if filename is None:
            ts = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"session_{ts}.jsonl"
        self._path = self._output_dir / filename
        self._file = open(self._path, "w", encoding="utf-8", buffering=1)
        self._recording = True
        self._frame_count = 0
        log.info("Enregistrement démarré : %s", self._path)
        return self._path

    def stop_recording(self) -> int:
        """
        Arrête l'enregistrement et ferme le fichier.

        Returns:
            Nombre de trames enregistrées.
        """
        if not self._recording:
            return 0
        self._recording = False
        if self._file:
            self._file.close()
            self._file = None
        log.info("Enregistrement arrêté — %d trames dans %s",
                 self._frame_count, self._path)
        return self._frame_count

    @property
    def is_recording(self) -> bool:
        return self._recording

    @property
    def current_path(self) -> Optional[Path]:
        return self._path

    @property
    def frame_count(self) -> int:
        return self._frame_count

    # ── Slots d'enregistrement (connectables aux signaux) ─────────────────────

    def log_sensor(self, frame: SensorFrame) -> None:
        """Enregistre une SensorFrame."""
        if not self._recording:
            return
        record = {
            "_ts":  frame.timestamp,
            "_fmt": "sensor",
            "lidar_matrix": frame.lidar_matrix.tolist(),
            "radar": {
                "resp_phase":   frame.radar.resp_phase,
                "heart_phase":  frame.radar.heart_phase,
                "breath_rate":  frame.radar.breath_rate,
                "heart_rate":   frame.radar.heart_rate,
                "distance_mm":  frame.radar.distance_mm,
                "presence":     frame.radar.presence,
            },
            "ai_event":  frame.ai_event,
            "confidence": frame.confidence,
        }
        self._write(record)

    def log_sys(self, frame: SysFrame) -> None:
        """Enregistre une SysFrame."""
        if not self._recording:
            return
        record = {
            "_ts":      frame.timestamp,
            "_fmt":     "sys",
            "uptime_s": frame.uptime_s,
            "cpu_load": frame.cpu_load,
            "heap_free":  frame.heap_free,
            "psram_free": frame.psram_free,
            "tasks_hwm":  frame.tasks_hwm,
        }
        self._write(record)

    def log_chunk(self, chunk: LogChunk) -> None:
        """Enregistre un LogChunk (métadonnées seulement, pas les données brutes)."""
        if not self._recording:
            return
        record = {
            "_ts":      chunk.timestamp,
            "_fmt":     "log",
            "is_header": chunk.is_header,
            "is_done":  chunk.is_done,
            "file_size": chunk.file_size,
            "text":     chunk.text[:4096],  # tronqué si trop long
        }
        self._write(record)

    # ── Interne ───────────────────────────────────────────────────────────────

    def _write(self, record: dict) -> None:
        """Sérialise et écrit un enregistrement dans le fichier JSONL."""
        try:
            self._file.write(json.dumps(record, ensure_ascii=False) + "\n")
            self._frame_count += 1
        except Exception as exc:
            log.error("Erreur écriture enregistrement : %s", exc)
            self.stop_recording()


# ─── PlaybackWorker ───────────────────────────────────────────────────────────

class PlaybackWorker(QThread):
    """
    QThread de rejeu d'une session JSONL enregistrée.

    Émet les mêmes signaux que SerialWorker pour une substitution transparente.
    La vitesse de rejeu est contrôlable (1.0 = temps réel, 2.0 = 2× plus vite).

    Signals:
        sensor_data_received:   SensorFrame reconstituée.
        sys_data_received:      SysFrame reconstituée.
        log_chunk_received:     LogChunk reconstituée.
        playback_progress:      (frame_index, total_frames).
        playback_finished:      Émis à la fin du fichier.
    """

    sensor_data_received: pyqtSignal = pyqtSignal(object)
    sys_data_received:    pyqtSignal = pyqtSignal(object)
    log_chunk_received:   pyqtSignal = pyqtSignal(object)
    playback_progress:    pyqtSignal = pyqtSignal(int, int)
    playback_finished:    pyqtSignal = pyqtSignal()

    def __init__(
        self,
        path:  Path,
        speed: float = 1.0,
        parent=None,
    ) -> None:
        """
        Args:
            path:   Chemin vers le fichier .jsonl à rejouer.
            speed:  Facteur de vitesse (1.0 = temps réel, 0 = aussi vite que possible).
            parent: Parent Qt optionnel.
        """
        super().__init__(parent)
        self._path:    Path  = Path(path)
        self._speed:   float = max(0.0, speed)
        self._running: bool  = False
        self._parser:  FrameParser = FrameParser()

    def stop(self) -> None:
        """Interrompt le rejeu."""
        self._running = False

    def run(self) -> None:
        """Lit le fichier JSONL et réémet les trames avec timing original."""
        self._running = True

        try:
            lines = self._path.read_text(encoding="utf-8").splitlines()
        except OSError as exc:
            log.error("Impossible de lire %s : %s", self._path, exc)
            return

        total = len(lines)
        prev_ts: Optional[float] = None

        for idx, line in enumerate(lines):
            if not self._running:
                break

            line = line.strip()
            if not line:
                continue

            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                log.warning("Ligne JSONL invalide ignorée : %.60s", line)
                continue

            # Timing : respecte l'intervalle entre trames
            cur_ts: float = float(record.get("_ts", time.monotonic()))
            if prev_ts is not None and self._speed > 0:
                gap = (cur_ts - prev_ts) / self._speed
                if 0 < gap < 60:  # Ignore les gaps aberrants (>1 min)
                    self._sleep_interruptible(gap)
            prev_ts = cur_ts

            # Reconstruction + émission
            fmt = record.get("_fmt", "")
            self._replay_record(fmt, record)

            self.playback_progress.emit(idx + 1, total)

        self.playback_finished.emit()
        log.info("Rejeu terminé : %s (%d trames)", self._path.name, total)

    # ── Reconstruction des frames ─────────────────────────────────────────────

    def _replay_record(self, fmt: str, record: dict) -> None:
        """Reconstruit et émet la frame correspondant au format."""
        import numpy as np
        from .data_model import RadarData, SensorFrame, SysFrame, LogChunk

        ts = float(record.get("_ts", time.monotonic()))

        if fmt == "sensor":
            matrix_raw = record.get("lidar_matrix", [])
            try:
                matrix = np.array(matrix_raw, dtype=np.float32)
                if matrix.shape != (8, 32):
                    matrix = matrix.reshape((8, 32)) if matrix.size == 256 else np.zeros((8, 32), dtype=np.float32)
            except Exception:
                matrix = np.zeros((8, 32), dtype=np.float32)

            radar = RadarData.from_dict(record.get("radar", {}))
            frame = SensorFrame(
                timestamp    = ts,
                lidar_matrix = matrix,
                radar        = radar,
                ai_event     = str(record.get("ai_event", "AI_NORMAL")),
                confidence   = int(record.get("confidence", 0)),
            )
            self.sensor_data_received.emit(frame)

        elif fmt == "sys":
            frame = SysFrame(
                timestamp  = ts,
                uptime_s   = int(record.get("uptime_s", 0)),
                cpu_load   = list(record.get("cpu_load", [0, 0])),
                heap_free  = int(record.get("heap_free", 0)),
                psram_free = int(record.get("psram_free", 0)),
                tasks_hwm  = dict(record.get("tasks_hwm", {})),
            )
            self.sys_data_received.emit(frame)

        elif fmt == "log":
            chunk = LogChunk(
                timestamp = ts,
                raw_bytes = record.get("text", "").encode("utf-8"),
                is_header = bool(record.get("is_header", False)),
                is_done   = bool(record.get("is_done",   False)),
                file_size = int(record.get("file_size",  0)),
            )
            self.log_chunk_received.emit(chunk)

    def _sleep_interruptible(self, seconds: float) -> None:
        """Sleep interruptible par stop()."""
        deadline = time.monotonic() + seconds
        while self._running and time.monotonic() < deadline:
            time.sleep(0.01)
