"""
main.py — Lexacare Studio : Point d'entrée et fenêtre principale
================================================================
Lance l'application PyQt6 avec un DockArea PyQtGraph comme conteneur
des quatre modules (A–D). Gère :

  · Configuration du port série (toolbar)
  · Démarrage / arrêt de l'enregistrement
  · Mode Playback (rejeu JSONL)
  · Connexion de tous les signaux SerialWorker → widgets
  · Barre de statut (connexion, FPS capteur, compteurs)

Lancement :
    python -m lexacare_studio.main
    # ou depuis tools/ :
    python -c "from lexacare_studio.main import main; main()"
"""

from __future__ import annotations

import logging
import sys
from pathlib import Path
from typing import Optional

import pyqtgraph as pg
from pyqtgraph.dockarea import Dock, DockArea
from PyQt6.QtCore import QTimer, Qt, pyqtSlot
from PyQt6.QtGui import QAction, QColor, QFont, QPalette
from PyQt6.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QStatusBar,
    QToolBar,
    QWidget,
)

from .data_logger import DataLogger, PlaybackWorker
from .data_model import LogChunk, SensorFrame, SysFrame
from .serial_worker import SerialWorker, list_serial_ports
from .simulator import SimulatorWorker
from .widgets.heatmap_widget import HeatmapWidget
from .widgets.logexplorer_widget import LogExplorerWidget
from .widgets.radar_widget import RadarWidget
from .widgets.syshealth_widget import SysHealthWidget

log = logging.getLogger(__name__)

# ─── Thème PyQtGraph dark mode ───────────────────────────────────────────────
pg.setConfigOption("background", "#04080f")
pg.setConfigOption("foreground", "#dce8f5")
pg.setConfigOption("antialias",  True)

# ─── Feuille de style Qt globale ─────────────────────────────────────────────
_QSS = """
QMainWindow, QWidget {
    background-color: #04080f;
    color: #dce8f5;
    font-family: 'Segoe UI', Arial, sans-serif;
    font-size: 12px;
}
QToolBar {
    background: #0a1628;
    border-bottom: 1px solid #1a3a5c;
    spacing: 6px;
    padding: 4px 8px;
}
QToolBar QLabel { color: #4a6fa5; font-size: 11px; }
QPushButton {
    background: #0f2040;
    color: #dce8f5;
    border: 1px solid #1a3a5c;
    border-radius: 5px;
    padding: 5px 14px;
    font-size: 11px;
    font-weight: 500;
}
QPushButton:hover  { border-color: #00d4ff; color: #00d4ff; }
QPushButton:pressed { background: #006888; }
QPushButton#btn_connect {
    background: #006888;
    color: #00d4ff;
    border-color: #00d4ff;
    font-weight: 700;
}
QPushButton#btn_connect:hover { background: #008aab; }
QPushButton#btn_record {
    background: #2a0010;
    color: #ff3366;
    border-color: #ff3366;
}
QPushButton#btn_record.recording {
    background: #3d0015;
    font-weight: 700;
}
QComboBox {
    background: #0f2040;
    color: #dce8f5;
    border: 1px solid #1a3a5c;
    border-radius: 5px;
    padding: 4px 10px;
    font-size: 11px;
}
QComboBox:hover { border-color: #00d4ff; }
QComboBox QAbstractItemView {
    background: #0a1628;
    color: #dce8f5;
    selection-background-color: #006888;
}
QStatusBar {
    background: #0a1628;
    color: #4a6fa5;
    font-size: 10px;
    border-top: 1px solid #1a3a5c;
}
/* DockArea — override pyqtgraph defaults */
QDockWidget { color: #dce8f5; }
"""


# ─── MainWindow ───────────────────────────────────────────────────────────────

class MainWindow(QMainWindow):
    """
    Fenêtre principale de Lexacare Studio.

    Layout DockArea :
        ┌──────────────────────────────────────────────────┐
        │  Dock A : LIDAR Heatmap (gauche, 60%)            │
        │  ┌────────────────────┐  Dock B : Radar (haut)  │
        │  │                    │  Dock C : SysHealth       │
        │  └────────────────────┘                           │
        │  Dock D : Log Explorer (bas, pleine largeur)     │
        └──────────────────────────────────────────────────┘
    """

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Lexacare Studio — Diagnostic ESP32-S3")
        self.setMinimumSize(1280, 720)
        self.resize(1600, 900)

        self._worker:    Optional[SerialWorker | SimulatorWorker] = None
        self._playback:  Optional[PlaybackWorker]                = None
        self._simulator: Optional[SimulatorWorker]               = None
        self._logger:    DataLogger                              = DataLogger()
        self._sensor_fps_counter: int           = 0

        self._build_toolbar()
        self._build_dock_area()
        self._build_status_bar()
        self._connect_logger_slots()

        # Timer FPS (mise à jour barre de statut toutes les secondes)
        self._fps_timer = QTimer(self)
        self._fps_timer.timeout.connect(self._update_fps)
        self._fps_timer.start(1000)

    # ── Toolbar ───────────────────────────────────────────────────────────────

    def _build_toolbar(self) -> None:
        """Construit la barre d'outils principale."""
        tb = QToolBar("Main Toolbar", self)
        tb.setMovable(False)
        self.addToolBar(tb)

        # --- Logo / titre ---
        lbl_logo = QLabel("◆ LEXACARE STUDIO")
        lbl_logo.setStyleSheet(
            "color: #ffd700; font-size: 15px; font-weight: 900; "
            "letter-spacing: 2px; padding-right: 16px;")
        tb.addWidget(lbl_logo)

        # --- Séparateur ---
        tb.addSeparator()

        # --- Sélection port ---
        lbl_port = QLabel(" Port : ")
        tb.addWidget(lbl_port)
        self._combo_port = QComboBox()
        self._combo_port.setFixedWidth(160)
        self._refresh_ports()
        tb.addWidget(self._combo_port)

        btn_refresh = QPushButton("↺")
        btn_refresh.setFixedWidth(30)
        btn_refresh.setToolTip("Actualiser la liste des ports")
        btn_refresh.clicked.connect(self._refresh_ports)
        tb.addWidget(btn_refresh)

        # --- Sélection baudrate ---
        lbl_baud = QLabel("  Baud : ")
        tb.addWidget(lbl_baud)
        self._combo_baud = QComboBox()
        self._combo_baud.setFixedWidth(90)
        for b in ["115200", "230400", "460800", "921600"]:
            self._combo_baud.addItem(b)
        tb.addWidget(self._combo_baud)

        # --- Bouton Connecter ---
        self._btn_connect = QPushButton("CONNECTER")
        self._btn_connect.setObjectName("btn_connect")
        self._btn_connect.setFixedWidth(110)
        self._btn_connect.clicked.connect(self._toggle_connection)
        tb.addWidget(self._btn_connect)

        tb.addSeparator()

        # --- Enregistrement ---
        self._btn_record = QPushButton("⏺ ENREG.")
        self._btn_record.setObjectName("btn_record")
        self._btn_record.setFixedWidth(90)
        self._btn_record.setToolTip("Démarrer / arrêter l'enregistrement JSONL")
        self._btn_record.clicked.connect(self._toggle_recording)
        tb.addWidget(self._btn_record)

        # --- Simulation / Démo ---
        self._btn_demo = QPushButton("⚡ DÉMO")
        self._btn_demo.setFixedWidth(80)
        self._btn_demo.setToolTip("Simuler des données LIDAR + Radar sans matériel")
        self._btn_demo.setStyleSheet("""
            QPushButton {
                background: #1a0a30; color: #a855f7;
                border: 1px solid #a855f7; border-radius: 5px;
                padding: 5px 10px; font-size: 11px; font-weight: 700;
            }
            QPushButton:hover  { background: #2a0a50; }
            QPushButton:checked { background: #300050; border-width: 2px; }
        """)
        self._btn_demo.setCheckable(True)
        self._btn_demo.clicked.connect(self._toggle_simulation)
        tb.addWidget(self._btn_demo)

        # --- Playback ---
        btn_play = QPushButton("▶ REJOUER")
        btn_play.setFixedWidth(95)
        btn_play.setToolTip("Rejouer un fichier .jsonl enregistré")
        btn_play.clicked.connect(self._open_playback)
        tb.addWidget(btn_play)

        tb.addSeparator()

        # --- Espaceur flexible ---
        spacer = QWidget()
        spacer.setSizePolicy(
            spacer.sizePolicy().horizontalPolicy(),
            spacer.sizePolicy().verticalPolicy(),
        )
        from PyQt6.QtWidgets import QSizePolicy
        spacer.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        tb.addWidget(spacer)

        # --- Indicateur de connexion ---
        self._lbl_conn_dot = QLabel("●")
        self._lbl_conn_dot.setStyleSheet("color: #1e3a5c; font-size: 16px;")
        self._lbl_conn_status = QLabel("DÉCONNECTÉ")
        self._lbl_conn_status.setStyleSheet("color: #4a6fa5; font-size: 10px; letter-spacing: 1px;")
        tb.addWidget(self._lbl_conn_dot)
        tb.addWidget(self._lbl_conn_status)

    # ── DockArea ──────────────────────────────────────────────────────────────

    def _build_dock_area(self) -> None:
        """Construit le DockArea avec les 4 modules."""
        area = DockArea()
        self.setCentralWidget(area)

        # Instantiation des widgets
        self._w_heatmap  = HeatmapWidget()
        self._w_radar    = RadarWidget()
        self._w_syshealth = SysHealthWidget()
        self._w_logs     = LogExplorerWidget()

        # Création des Docks avec noms industriels
        dock_a = Dock("A — LIDAR Depth Map  8×32",      size=(900, 550))
        dock_b = Dock("B — Radar Vital Signs",           size=(500, 300))
        dock_c = Dock("C — ESP32 System Health",         size=(500, 250))
        dock_d = Dock("D — LittleFS Log Explorer",       size=(1400, 160))

        # Ajout des widgets dans leurs docks
        dock_a.addWidget(self._w_heatmap)
        dock_b.addWidget(self._w_radar)
        dock_c.addWidget(self._w_syshealth)
        dock_d.addWidget(self._w_logs)

        # Disposition initiale
        area.addDock(dock_a, "left")
        area.addDock(dock_b, "right", dock_a)
        area.addDock(dock_c, "bottom", dock_b)
        area.addDock(dock_d, "bottom", dock_a)

        # Signal du LogExplorer → send command
        self._w_logs.send_command.connect(self._on_send_command)

    # ── Barre de statut ───────────────────────────────────────────────────────

    def _build_status_bar(self) -> None:
        """Construit la barre de statut."""
        sb = QStatusBar(self)
        self.setStatusBar(sb)

        self._lbl_fps    = QLabel("Capteur : — Hz")
        self._lbl_frames = QLabel("Trames : 0")
        self._lbl_errors = QLabel("Erreurs : 0")
        self._lbl_record = QLabel("")
        self._lbl_record.setStyleSheet("color: #ff3366;")

        for lbl in [self._lbl_fps, self._lbl_frames, self._lbl_errors, self._lbl_record]:
            sb.addPermanentWidget(lbl)

        sb.showMessage("Lexacare Studio prêt — Sélectionnez un port et cliquez CONNECTER")

    # ── Connexion des signaux ─────────────────────────────────────────────────

    def _wire_worker_signals(self, worker: SerialWorker | SimulatorWorker | PlaybackWorker) -> None:
        """Connecte les signaux du worker aux slots des widgets et de l'UI."""
        worker.sensor_data_received.connect(self._on_sensor_data)
        worker.sys_data_received.connect(self._on_sys_data)
        worker.log_chunk_received.connect(self._on_log_chunk)

        if isinstance(worker, SerialWorker):
            worker.connection_state_changed.connect(self._on_connection_changed)
            worker.error_occurred.connect(self._on_error)
            worker.stats_updated.connect(self._on_stats_updated)

        if isinstance(worker, PlaybackWorker):
            worker.playback_finished.connect(self._on_playback_finished)

    def _connect_logger_slots(self) -> None:
        """Pré-connexion logger (sera effectivement utilisé après start_recording)."""
        pass  # Les connexions sont faites dans _toggle_recording

    # ── Slots : données capteurs ──────────────────────────────────────────────

    @pyqtSlot(object)
    def _on_sensor_data(self, frame: SensorFrame) -> None:
        self._w_heatmap.update_sensor(frame)
        self._w_radar.update_sensor(frame)
        self._sensor_fps_counter += 1
        if self._logger.is_recording:
            self._logger.log_sensor(frame)

    @pyqtSlot(object)
    def _on_sys_data(self, frame: SysFrame) -> None:
        self._w_syshealth.update_sys(frame)
        if self._logger.is_recording:
            self._logger.log_sys(frame)

    @pyqtSlot(object)
    def _on_log_chunk(self, chunk: LogChunk) -> None:
        self._w_logs.append_chunk(chunk)
        if self._logger.is_recording:
            self._logger.log_chunk(chunk)

    @pyqtSlot(dict)
    def _on_send_command(self, cmd: dict) -> None:
        if self._worker and isinstance(self._worker, SerialWorker):
            self._worker.send(cmd)

    # ── Slots : connexion ─────────────────────────────────────────────────────

    @pyqtSlot(bool, str)
    def _on_connection_changed(self, connected: bool, info: str) -> None:
        if connected:
            self._lbl_conn_dot.setStyleSheet("color: #00ff88; font-size: 16px;")
            self._lbl_conn_status.setStyleSheet(
                "color: #00ff88; font-size: 10px; letter-spacing: 1px;")
            self._lbl_conn_status.setText("CONNECTÉ")
            self._btn_connect.setText("DÉCONNECTER")
            self.statusBar().showMessage(f"Connecté : {info}")
        else:
            self._lbl_conn_dot.setStyleSheet("color: #ff3366; font-size: 16px;")
            self._lbl_conn_status.setStyleSheet(
                "color: #ff3366; font-size: 10px; letter-spacing: 1px;")
            self._lbl_conn_status.setText("DÉCONNECTÉ")
            self._btn_connect.setText("CONNECTER")
            self.statusBar().showMessage(f"Déconnecté : {info}")

    @pyqtSlot(str)
    def _on_error(self, msg: str) -> None:
        log.warning("SerialWorker : %s", msg)
        self.statusBar().showMessage(msg, 5000)

    @pyqtSlot(int, int)
    def _on_stats_updated(self, parsed: int, malformed: int) -> None:
        self._lbl_frames.setText(f"Trames : {parsed}")
        self._lbl_errors.setText(f"Erreurs : {malformed}")

    # ── Slots : Playback ──────────────────────────────────────────────────────

    @pyqtSlot()
    def _on_playback_finished(self) -> None:
        self.statusBar().showMessage("Rejeu terminé.", 5000)
        self._lbl_conn_dot.setStyleSheet("color: #1e3a5c; font-size: 16px;")
        self._lbl_conn_status.setText("DÉCONNECTÉ")
        self._playback = None

    # ── FPS timer ─────────────────────────────────────────────────────────────

    @pyqtSlot()
    def _update_fps(self) -> None:
        fps = self._sensor_fps_counter
        self._lbl_fps.setText(f"Capteur : {fps} Hz")
        self._sensor_fps_counter = 0

    # ── Actions UI ────────────────────────────────────────────────────────────

    def _refresh_ports(self) -> None:
        """Actualise la liste des ports série disponibles."""
        self._combo_port.clear()
        ports = list_serial_ports()
        if ports:
            self._combo_port.addItems(ports)
        else:
            self._combo_port.addItem("— Aucun port —")

    def _toggle_connection(self) -> None:
        """Connecte ou déconnecte le port série."""
        if self._worker and self._worker.isRunning():
            self._disconnect()
        else:
            self._connect()

    def _connect(self) -> None:
        """Démarre le SerialWorker sur le port sélectionné."""
        port = self._combo_port.currentText()
        if not port or port.startswith("—"):
            QMessageBox.warning(self, "Port invalide",
                                "Sélectionnez un port série valide.")
            return
        baud = int(self._combo_baud.currentText())
        self._worker = SerialWorker(port, baud)
        self._wire_worker_signals(self._worker)
        self._worker.start()
        log.info("SerialWorker démarré : %s @ %d", port, baud)

    def _disconnect(self) -> None:
        """Arrête le SerialWorker ou SimulatorWorker proprement."""
        if self._worker:
            self._worker.stop()
            self._worker.wait(3000)
            self._worker = None
        self._btn_demo.setChecked(False)
        self._btn_demo.setText("⚡ DÉMO")
        self._on_connection_changed(False, "Déconnexion manuelle")

    def _toggle_simulation(self) -> None:
        """Démarre ou arrête le SimulatorWorker (mode démo sans matériel)."""
        if self._btn_demo.isChecked():
            # Arrêter tout worker existant
            if self._worker and self._worker.isRunning():
                self._worker.stop()
                self._worker.wait(2000)
            # Démarrer le simulateur
            self._simulator = SimulatorWorker()
            self._worker    = self._simulator
            self._wire_worker_signals(self._simulator)
            self._simulator.start()
            self._btn_demo.setText("⏹ STOP DÉMO")
            self._btn_connect.setText("CONNECTER")
            self.statusBar().showMessage(
                "Mode SIMULATION — LIDAR 1–4 synthétiques @15 Hz — chute simulée toutes les ~30 s")
            log.info("SimulatorWorker démarré.")
        else:
            self._disconnect()
            self.statusBar().showMessage("Simulation arrêtée.", 4000)

    def _toggle_recording(self) -> None:
        """Démarre ou arrête l'enregistrement JSONL."""
        if self._logger.is_recording:
            count = self._logger.stop_recording()
            self._btn_record.setText("⏺ ENREG.")
            self._lbl_record.setText("")
            self.statusBar().showMessage(
                f"Enregistrement arrêté — {count} trames dans {self._logger.current_path}", 8000)
        else:
            path = self._logger.start_recording()
            self._btn_record.setText("⏹ STOP")
            self._lbl_record.setText(f"● REC  {path.name}")
            self.statusBar().showMessage(f"Enregistrement : {path}")

    def _open_playback(self) -> None:
        """Ouvre un sélecteur de fichier et démarre le PlaybackWorker."""
        recordings_dir = str(Path(__file__).parent.parent / "recordings")
        path, _ = QFileDialog.getOpenFileName(
            self, "Ouvrir une session JSONL", recordings_dir,
            "Sessions Lexacare (*.jsonl);;Tous les fichiers (*)")
        if not path:
            return

        # Arrêter le worker actif si nécessaire
        self._disconnect()
        if self._playback and self._playback.isRunning():
            self._playback.stop()
            self._playback.wait(2000)

        self._playback = PlaybackWorker(Path(path), speed=1.0)
        self._wire_worker_signals(self._playback)
        self._playback.playback_progress.connect(self._on_playback_progress)
        self._playback.start()

        self._lbl_conn_dot.setStyleSheet("color: #ffd700; font-size: 16px;")
        self._lbl_conn_status.setText("PLAYBACK")
        self._lbl_conn_status.setStyleSheet(
            "color: #ffd700; font-size: 10px; letter-spacing: 1px;")
        self.statusBar().showMessage(f"Rejeu : {Path(path).name}")

    @pyqtSlot(int, int)
    def _on_playback_progress(self, current: int, total: int) -> None:
        if total:
            pct = int(current / total * 100)
            self.statusBar().showMessage(f"Rejeu : {pct} %  ({current}/{total} trames)")

    # ── Fermeture ─────────────────────────────────────────────────────────────

    def closeEvent(self, event) -> None:
        """Arrête proprement les threads avant de fermer."""
        if self._logger.is_recording:
            self._logger.stop_recording()
        self._disconnect()
        if self._playback and self._playback.isRunning():
            self._playback.stop()
            self._playback.wait(2000)
        if self._simulator and self._simulator.isRunning():
            self._simulator.stop()
            self._simulator.wait(2000)
        event.accept()


# ─── Point d'entrée ───────────────────────────────────────────────────────────

def _configure_logging() -> None:
    logging.basicConfig(
        level  = logging.INFO,
        format = "%(asctime)s  %(levelname)-8s  %(name)s : %(message)s",
        datefmt= "%H:%M:%S",
    )


def main() -> None:
    """Démarre Lexacare Studio."""
    _configure_logging()

    app = QApplication(sys.argv)
    app.setApplicationName("Lexacare Studio")
    app.setOrganizationName("Hexacore")
    app.setApplicationVersion("1.0.0")
    app.setStyleSheet(_QSS)

    # Palette sombre système
    pal = app.palette()
    pal.setColor(QPalette.ColorRole.Window,     QColor("#04080f"))
    pal.setColor(QPalette.ColorRole.WindowText, QColor("#dce8f5"))
    pal.setColor(QPalette.ColorRole.Base,       QColor("#0a1628"))
    pal.setColor(QPalette.ColorRole.Text,       QColor("#dce8f5"))
    pal.setColor(QPalette.ColorRole.Button,     QColor("#0f2040"))
    pal.setColor(QPalette.ColorRole.ButtonText, QColor("#dce8f5"))
    pal.setColor(QPalette.ColorRole.Highlight,  QColor("#006888"))
    app.setPalette(pal)

    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
