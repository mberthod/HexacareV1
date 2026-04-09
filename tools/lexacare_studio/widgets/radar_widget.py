"""
radar_widget.py — Module B : Radar Vital Signs Oscilloscope
============================================================
STUB — Sera implémenté sur instruction.

Contrat d'interface :
    update_sensor(frame: SensorFrame) -> None
"""
from __future__ import annotations

from PyQt6.QtWidgets import QLabel, QVBoxLayout, QWidget

from ..data_model import SensorFrame


class RadarWidget(QWidget):
    """Placeholder — Module B : Oscilloscope respiratoire/cardiaque."""

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        lay = QVBoxLayout(self)
        lbl = QLabel("Module B — Radar Vital Signs\n[À implémenter]")
        lbl.setStyleSheet("color:#00ff88; font-size:14px; font-weight:bold;")
        lay.addWidget(lbl)
        self.setMinimumSize(300, 150)

    def update_sensor(self, frame: SensorFrame) -> None:
        pass
