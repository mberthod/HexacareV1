"""
serial_worker.py — Thread d'acquisition série Lexacare Studio
==============================================================
Gère la connexion USB-CDC avec l'ESP32-S3 dans un QThread dédié.

Responsabilités :
  · Ouverture / fermeture du port série
  · Lecture ligne par ligne avec timeout
  · Reconnexion automatique (backoff exponentiel : 0.5 s → 8 s)
  · Envoi de commandes JSON de façon thread-safe (Queue)
  · Émission de signaux PyQt vers l'UI (jamais d'appel UI direct)
  · Délégation du parsing à FrameParser

Signaux émis (connectables depuis le thread principal Qt) :
  · sensor_data_received(SensorFrame)
  · sys_data_received(SysFrame)
  · log_chunk_received(LogChunk)
  · connection_state_changed(bool, str)   # (connected, port_or_error)
  · error_occurred(str)                   # message d'erreur humain
  · raw_line_received(str)                # ligne brute pour la console

Usage minimal::

    worker = SerialWorker(port="COM7", baudrate=115200)
    worker.sensor_data_received.connect(on_sensor)
    worker.connection_state_changed.connect(on_conn_change)
    worker.start()
    ...
    worker.stop()
    worker.wait()
"""

from __future__ import annotations

import logging
import queue
import time
from typing import Optional

import serial
import serial.tools.list_ports
from PyQt6.QtCore import QThread, pyqtSignal

from .data_model import LogChunk, SensorFrame, SysFrame
from .frame_parser import FrameParser

log = logging.getLogger(__name__)


# ─── Constantes de reconnexion ───────────────────────────────────────────────

_RECONNECT_INITIAL_DELAY: float = 0.5   # secondes
_RECONNECT_MAX_DELAY:     float = 8.0   # secondes
_RECONNECT_BACKOFF:       float = 2.0   # multiplicateur
_SERIAL_TIMEOUT:          float = 0.1   # timeout readline (secondes)
_SEND_QUEUE_MAX:          int   = 64    # taille max de la queue d'envoi


# ─── Utilitaire : liste des ports disponibles ─────────────────────────────────

def list_serial_ports() -> list[str]:
    """
    Retourne la liste des ports série disponibles sur le système.
    Filtre en priorité les ports USB-CDC (ESP32, STM32, …).

    Returns:
        Liste de chaînes ("COM7", "/dev/ttyUSB0", …), triée.
    """
    all_ports = serial.tools.list_ports.comports()
    # Prioriser les ports ESP32 / USB-CDC
    usb_ports = [p.device for p in all_ports if "USB" in (p.description or "").upper()
                 or "CP210" in (p.description or "").upper()
                 or "CH340" in (p.description or "").upper()
                 or "JTAG"  in (p.description or "").upper()]
    other_ports = [p.device for p in all_ports if p.device not in usb_ports]
    return sorted(usb_ports) + sorted(other_ports)


# ─── SerialWorker ─────────────────────────────────────────────────────────────

class SerialWorker(QThread):
    """
    QThread de lecture série avec reconnexion automatique.

    Signals:
        sensor_data_received:       Nouvelle trame SensorFrame disponible.
        sys_data_received:          Nouvelle trame SysFrame disponible.
        log_chunk_received:         Bloc de log LittleFS reçu.
        connection_state_changed:   (True, port) à la connexion,
                                    (False, error_msg) à la déconnexion.
        error_occurred:             Message d'erreur non-fatal (string).
        raw_line_received:          Ligne brute pour affichage console.
        stats_updated:              (parsed_count, malformed_count).
    """

    sensor_data_received:     pyqtSignal = pyqtSignal(object)   # SensorFrame
    sys_data_received:        pyqtSignal = pyqtSignal(object)   # SysFrame
    log_chunk_received:       pyqtSignal = pyqtSignal(object)   # LogChunk
    connection_state_changed: pyqtSignal = pyqtSignal(bool, str)
    error_occurred:           pyqtSignal = pyqtSignal(str)
    raw_line_received:        pyqtSignal = pyqtSignal(str)
    stats_updated:            pyqtSignal = pyqtSignal(int, int)  # parsed, malformed

    def __init__(
        self,
        port:     str,
        baudrate: int = 115_200,
        parent=None,
    ) -> None:
        """
        Args:
            port:     Nom du port série (ex : "COM7", "/dev/ttyUSB0").
            baudrate: Vitesse de communication (défaut : 115 200).
            parent:   Parent Qt optionnel.
        """
        super().__init__(parent)
        self.port     = port
        self.baudrate = baudrate

        self._parser:   FrameParser   = FrameParser()
        self._send_q:   queue.Queue[str] = queue.Queue(maxsize=_SEND_QUEUE_MAX)
        self._running:  bool          = False
        self._serial:   Optional[serial.Serial] = None

    # ── API publique ──────────────────────────────────────────────────────────

    def send(self, cmd: dict) -> bool:
        """
        Enfile une commande JSON pour envoi vers l'ESP32.
        Thread-safe — peut être appelé depuis le thread UI.

        Args:
            cmd: Dictionnaire sérialisable en JSON (ex: {"cmd": "get_diag"}).

        Returns:
            True si la commande a été enfilée, False si la queue est pleine.
        """
        try:
            payload = __import__("json").dumps(cmd) + "\n"
            self._send_q.put_nowait(payload)
            return True
        except queue.Full:
            log.warning("Queue d'envoi pleine — commande ignorée : %s", cmd)
            return False

    def stop(self) -> None:
        """Demande l'arrêt propre du thread. Appeler wait() ensuite."""
        log.info("Arrêt du SerialWorker demandé.")
        self._running = False
        if self._serial and self._serial.is_open:
            try:
                self._serial.cancel_read()
            except Exception:
                pass

    def change_port(self, port: str, baudrate: Optional[int] = None) -> None:
        """
        Modifie le port cible. Le thread se reconnectera au prochain cycle.
        Thread-safe.
        """
        self.port = port
        if baudrate is not None:
            self.baudrate = baudrate
        if self._serial and self._serial.is_open:
            try:
                self._serial.close()
            except Exception:
                pass

    # ── Boucle principale QThread ─────────────────────────────────────────────

    def run(self) -> None:
        """Boucle de connexion + lecture avec backoff exponentiel."""
        self._running = True
        self._parser.reset_stats()
        delay = _RECONNECT_INITIAL_DELAY

        while self._running:
            # Tentative de connexion
            try:
                self._open_serial()
                delay = _RECONNECT_INITIAL_DELAY  # reset backoff après succès
                self._read_loop()
            except serial.SerialException as exc:
                msg = f"Erreur série sur {self.port} : {exc}"
                log.warning(msg)
                self.connection_state_changed.emit(False, msg)
            except OSError as exc:
                msg = f"Port {self.port} inaccessible : {exc}"
                log.error(msg)
                self.connection_state_changed.emit(False, msg)
            finally:
                self._close_serial()

            if not self._running:
                break

            # Attente avant reconnexion (backoff exponentiel)
            log.info("Reconnexion dans %.1f s…", delay)
            self.error_occurred.emit(f"Reconnexion dans {delay:.0f} s…")
            self._sleep_interruptible(delay)
            delay = min(delay * _RECONNECT_BACKOFF, _RECONNECT_MAX_DELAY)

        log.info("SerialWorker terminé.")

    # ── Gestion du port série ─────────────────────────────────────────────────

    def _open_serial(self) -> None:
        """Ouvre le port série et émet le signal de connexion."""
        log.info("Ouverture %s @ %d bauds", self.port, self.baudrate)
        self._serial = serial.Serial(
            port        = self.port,
            baudrate    = self.baudrate,
            timeout     = _SERIAL_TIMEOUT,
            write_timeout = 1.0,
        )
        self.connection_state_changed.emit(True, self.port)
        log.info("Connecté : %s", self.port)

    def _close_serial(self) -> None:
        """Ferme le port série proprement."""
        if self._serial and self._serial.is_open:
            try:
                self._serial.close()
            except Exception:
                pass
        self._serial = None

    # ── Boucle de lecture ─────────────────────────────────────────────────────

    def _read_loop(self) -> None:
        """
        Lit le port série ligne par ligne.
        Envoie les commandes en attente dans la queue.
        Délègue le parsing à FrameParser.
        """
        assert self._serial is not None

        stats_counter = 0

        while self._running:
            # --- Envoi des commandes en attente ---
            self._flush_send_queue()

            # --- Lecture d'une ligne ---
            try:
                raw_bytes = self._serial.readline()
            except serial.SerialException as exc:
                log.warning("Erreur lecture série : %s", exc)
                raise  # Remonte vers la boucle run() pour reconnexion

            if not raw_bytes:
                continue  # Timeout readline — normal, continuer

            # Décodage UTF-8 (robuste aux caractères spéciaux)
            try:
                line = raw_bytes.decode("utf-8", errors="replace").rstrip("\r\n")
            except Exception:
                continue

            if not line:
                continue

            # Émission brute pour la console
            self.raw_line_received.emit(line)

            # --- Parsing ---
            result = self._parser.parse_line(line)
            self._dispatch_result(result)

            # --- Statistiques périodiques ---
            stats_counter += 1
            if stats_counter >= 50:
                stats_counter = 0
                s = self._parser.stats()
                self.stats_updated.emit(s["parsed"], s["malformed"])

    def _flush_send_queue(self) -> None:
        """Envoie tous les messages en attente dans la queue de commandes."""
        assert self._serial is not None
        while not self._send_q.empty():
            try:
                payload = self._send_q.get_nowait()
            except queue.Empty:
                break
            try:
                self._serial.write(payload.encode("utf-8"))
                self._serial.flush()
                log.debug("TX → %s", payload.strip())
            except serial.SerialException as exc:
                log.warning("Erreur envoi TX : %s", exc)
                self._send_q.task_done()
                raise  # Déclenche reconnexion
            self._send_q.task_done()

    # ── Dispatch des résultats de parsing ─────────────────────────────────────

    def _dispatch_result(self, result: object) -> None:
        """Émet le signal approprié selon le type de trame parsée."""
        if result is None:
            return

        if isinstance(result, SensorFrame):
            self.sensor_data_received.emit(result)

        elif isinstance(result, SysFrame):
            self.sys_data_received.emit(result)

        elif isinstance(result, LogChunk):
            self.log_chunk_received.emit(result)

        else:
            log.debug("Type de résultat inconnu : %s", type(result))

    # ── Utilitaire : sleep interruptible ──────────────────────────────────────

    def _sleep_interruptible(self, seconds: float) -> None:
        """
        Attend `seconds` secondes avec vérification du flag _running
        toutes les 100 ms pour permettre un arrêt rapide.
        """
        deadline = time.monotonic() + seconds
        while self._running and time.monotonic() < deadline:
            time.sleep(0.1)
