#!/usr/bin/env python3
"""
Visualisation live des lignes FRAME: (8×32 mm) émises par le firmware LexaCare.

Usage typique (même format que LEXACARE_ARDUINO/python/read_matrix.py) :
  pio run -e production_tof_frame_ascii -t upload
  pip install -r tools/requirements_tof_frame.txt
  python3 tools/read_lexa_tof_frame.py --port /dev/ttyACM0 --baud 115200

Pour le flux USB multiplexé (LXCS + LXCL + JSON à 921600), utiliser
tools/lexa_live_monitor.py — pas ce script (évite le mélange binaire / texte).
"""

from __future__ import annotations

import argparse
import sys

import numpy as np
import serial
import matplotlib.pyplot as plt

ROWS = 8
COLS = 32
NCELLS = ROWS * COLS


def parse_frame(line: str):
    if not line.startswith("FRAME:"):
        return None
    payload = line[len("FRAME:") :].strip()
    if not payload:
        return None
    try:
        values = [int(x) for x in payload.split(",")]
    except ValueError:
        return None
    if len(values) != NCELLS:
        return None
    return np.asarray(values, dtype=np.int32).reshape(ROWS, COLS)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0", help="Port série")
    parser.add_argument("--baud", type=int, default=115200, help="Débit (115200 avec production_tof_frame_ascii)")
    parser.add_argument("--vmin", type=int, default=0, help="Colormap min (mm)")
    parser.add_argument("--vmax", type=int, default=4000, help="Colormap max (mm)")
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=1)
    print(f"Écoute {args.port} @ {args.baud} baud", file=sys.stderr)

    plt.ion()
    fig, ax = plt.subplots(figsize=(12, 3.5))
    initial = np.zeros((ROWS, COLS), dtype=np.int32)
    im = ax.imshow(
        initial,
        cmap="viridis",
        vmin=args.vmin,
        vmax=args.vmax,
        aspect="auto",
        interpolation="nearest",
    )
    cbar = fig.colorbar(im, ax=ax)
    cbar.set_label("Distance (mm)")
    ax.set_title("LexaCare — attente première trame")
    ax.set_xlabel("Colonnes (gauche: LiDAR #3, puis #4, #2, #1)")
    ax.set_ylabel("Lignes")
    for x in (7.5, 15.5, 23.5):
        ax.axvline(x, color="white", linewidth=0.5, alpha=0.6)

    frame_count = 0
    try:
        while plt.fignum_exists(fig.number):
            raw = ser.readline()
            if not raw:
                plt.pause(0.01)
                continue
            try:
                line = raw.decode("ascii", errors="replace").rstrip()
            except UnicodeDecodeError:
                continue

            frame = parse_frame(line)
            if frame is None:
                if line:
                    print(line, file=sys.stderr)
                continue

            frame_count += 1
            im.set_data(frame)
            valid = frame[frame > 0]
            if valid.size:
                ax.set_title(
                    f"frame {frame_count}  "
                    f"min={int(valid.min())} mm  "
                    f"max={int(valid.max())} mm  "
                    f"mean={int(valid.mean())} mm"
                )
            else:
                ax.set_title(f"frame {frame_count}  (aucun pixel > 0)")
            fig.canvas.draw_idle()
            plt.pause(0.001)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        plt.ioff()
        plt.close(fig)


if __name__ == "__main__":
    main()
