"""
logexplorer_widget.py — Module D : LittleFS Log Explorer
=========================================================
STUB — Sera implémenté sur instruction.

Contrat d'interface :
    append_chunk(chunk: LogChunk) -> None
    send_command: pyqtSignal(dict)
"""
from __future__ import annotations

from PyQt6.QtCore import pyqtSignal
from PyQt6.QtWidgets import QLabel, QVBoxLayout, QWidget

from ..data_model import LogChunk


class LogExplorerWidget(QWidget):
    """Placeholder — Module D : Log Explorer LittleFS."""

    send_command: pyqtSignal = pyqtSignal(dict)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        lay = QVBoxLayout(self)
        lbl = QLabel("Module D — LittleFS Log Explorer\n[À implémenter]")
        lbl.setStyleSheet("color:#a855f7; font-size:14px; font-weight:bold;")
        lay.addWidget(lbl)
        self.setMinimumSize(600, 100)

    def append_chunk(self, chunk: LogChunk) -> None:
        pass
