"""
syshealth_widget.py — Module C : ESP32 System Health Monitor
=============================================================
STUB — Sera implémenté sur instruction.

Contrat d'interface :
    update_sys(frame: SysFrame) -> None
"""
from __future__ import annotations

from PyQt6.QtWidgets import QLabel, QVBoxLayout, QWidget

from ..data_model import SysFrame


class SysHealthWidget(QWidget):
    """Placeholder — Module C : CPU/RAM/Tasks."""

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        lay = QVBoxLayout(self)
        lbl = QLabel("Module C — System Health\n[À implémenter]")
        lbl.setStyleSheet("color:#ffc400; font-size:14px; font-weight:bold;")
        lay.addWidget(lbl)
        self.setMinimumSize(300, 150)

    def update_sys(self, frame: SysFrame) -> None:
        pass
