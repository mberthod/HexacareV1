#!/usr/bin/env python3
"""
Enregistre sur le PC le flux audio depuis la carte Lexa (16 kHz mono int16 LE).

Deux modes :

1) Trames LXCS (stéréo) ou LXCA (mono) — firmware capture_audio_host :
     python3 tools/record_lexa_audio.py --port /dev/ttyACM0 --duration 10 -o capture.wav

2) PCM brut continu (firmware capture_audio_raw), sans attente LXCA :
     pio run -e capture_audio_raw -t upload
     python3 tools/record_lexa_audio.py --port /dev/ttyACM0 --raw-pcm --skip-bytes 4096 --duration 10 -o raw.wav

   --skip-bytes : octets à jeter au début (logs de boot / bruit avant flux stable).
   Aligner --skip-bytes sur un multiple de 2 pour rester sur des frontières int16.

IMPORTANT — Ne pas ouvrir le « Serial Monitor » en parallèle sur le même port.

Prérequis : pip install pyserial
"""
from __future__ import annotations

import argparse
import struct
import sys
import time
import wave
from pathlib import Path

try:
    import serial
except ImportError:
    print("Installe pyserial : pip install pyserial", file=sys.stderr)
    sys.exit(1)

MAGIC_STEREO = b"LXCS"
MAGIC_MONO = b"LXCA"
HDR_LEN = 12
SAMPLE_RATE = 16000
MAX_SAMPLES_PER_FRAME = 65536


def read_exact(ser: serial.Serial, n: int, timeout: float | None) -> bytes | None:
    old_timeout = ser.timeout
    ser.timeout = timeout
    buf = bytearray()
    try:
        while len(buf) < n:
            chunk = ser.read(n - len(buf))
            if not chunk:
                return None
            buf.extend(chunk)
        return bytes(buf)
    finally:
        ser.timeout = old_timeout


def discard_bytes(ser: serial.Serial, n: int, chunk_timeout: float) -> None:
    """Ignore n octets (débit série)."""
    left = n
    while left > 0:
        chunk = ser.read(min(left, 8192))
        if not chunk:
            break
        left -= len(chunk)


def resync_to_audio_magic(ser: serial.Serial, max_skip: int = 2_000_000) -> bytes | None:
    """Retourne LXCS ou LXCA une fois aligné, ou None."""
    window = bytearray()
    for _ in range(max_skip):
        b = ser.read(1)
        if not b:
            return None
        window.append(b[0])
        if len(window) > 4:
            window = window[-4:]
        g = bytes(window[-4:])
        if g == MAGIC_STEREO or g == MAGIC_MONO:
            return g
    return None


def record_wav(
    port: str,
    baudrate: int,
    duration_s: float,
    out_path: Path,
    chunk_timeout: float,
) -> None:
    ser = serial.Serial(port, baudrate, timeout=0.5)
    ser.reset_input_buffer()

    cur_magic = resync_to_audio_magic(ser)
    if cur_magic is None:
        print(
            "Impossible de trouver LXCS/LXCA — firmware capture_audio_host ou --raw-pcm.",
            file=sys.stderr,
        )
        sys.exit(2)

    stereo = cur_magic == MAGIC_STEREO
    all_pcm = bytearray()
    t_end = time.monotonic() + duration_s
    frames = 0
    need_header_suffix = True

    try:
        while time.monotonic() < t_end:
            if need_header_suffix:
                rest = read_exact(ser, 8, chunk_timeout)
                need_header_suffix = False
                if rest is None:
                    break
                hdr = cur_magic + rest
            else:
                hdr = read_exact(ser, HDR_LEN, chunk_timeout)
                if hdr is None:
                    break
                if len(hdr) != HDR_LEN or hdr[:4] != cur_magic:
                    m = resync_to_audio_magic(ser)
                    if m is None:
                        break
                    cur_magic = m
                    stereo = cur_magic == MAGIC_STEREO
                    need_header_suffix = True
                    continue

            _seq, ns = struct.unpack_from("<II", hdr, 4)
            if ns == 0 or ns > MAX_SAMPLES_PER_FRAME:
                m = resync_to_audio_magic(ser)
                if m is None:
                    break
                cur_magic = m
                stereo = cur_magic == MAGIC_STEREO
                need_header_suffix = True
                continue

            payload = read_exact(ser, int(ns) * 2, chunk_timeout)
            if payload is None or len(payload) != int(ns) * 2:
                m = resync_to_audio_magic(ser)
                if m is None:
                    break
                cur_magic = m
                stereo = cur_magic == MAGIC_STEREO
                need_header_suffix = True
                continue

            all_pcm.extend(payload)
            frames += 1
            if frames % 50 == 0:
                print(f"  {frames} trames, ~{len(all_pcm) // 2} int16...", flush=True)
    finally:
        ser.close()

    if len(all_pcm) < SAMPLE_RATE // 10 * (2 if stereo else 1):
        print(
            "Très peu de données — vérifie le port et le firmware.",
            file=sys.stderr,
        )
        sys.exit(3)

    _write_wav(out_path, all_pcm, stereo=stereo)


def record_wav_raw_pcm(
    port: str,
    baudrate: int,
    duration_s: float,
    out_path: Path,
    chunk_timeout: float,
    skip_bytes: int,
) -> None:
    """Lit int16 LE en continu, sans entête LXCA (firmware capture_audio_raw)."""
    if skip_bytes < 0:
        skip_bytes = 0
    # Stéréo interleaved LRLR (firmware capture_audio_raw actuel).
    want_bytes = int(duration_s * SAMPLE_RATE * 2 * 2)
    if want_bytes < 2:
        print("--duration trop court", file=sys.stderr)
        sys.exit(4)

    ser = serial.Serial(port, baudrate, timeout=0.5)
    ser.reset_input_buffer()

    try:
        if skip_bytes:
            print(f"  Ignore les {skip_bytes} premiers octets...", flush=True)
            discard_bytes(ser, skip_bytes, chunk_timeout)

        raw = read_exact(ser, want_bytes, chunk_timeout)
        if raw is None or len(raw) < 2:
            print("Lecture PCM brute vide ou timeout — augmente --chunk-timeout ?", file=sys.stderr)
            sys.exit(3)
        if len(raw) % 2 != 0:
            raw = raw[: len(raw) - 1]
        all_pcm = bytearray(raw)
    finally:
        ser.close()

    if len(all_pcm) < SAMPLE_RATE // 10:
        print("Très peu d’octets PCM — augmente --duration ou vérifie le firmware raw.", file=sys.stderr)
        sys.exit(3)

    _write_wav(out_path, all_pcm, stereo=True)


def _write_wav(out_path: Path, all_pcm: bytearray, stereo: bool) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    ch = 2 if stereo else 1
    with wave.open(str(out_path), "wb") as wf:
        wf.setnchannels(ch)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(bytes(all_pcm))

    sec = len(all_pcm) / (2 * SAMPLE_RATE * ch)
    print(f"Écrit {out_path} (~{sec:.2f} s, {ch} canaux, {len(all_pcm)} octets PCM).")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", default="/dev/ttyACM0", help="Port série (ex. /dev/ttyACM0, COM5)")
    p.add_argument(
        "--baud",
        type=int,
        default=921600,
        help="Débit série (aligné platformio monitor_speed). USB-Serial/JTAG : souvent peu d’effet ; utile si UART externe.",
    )
    p.add_argument("--duration", type=float, default=10.0, help="Durée d'enregistrement en secondes")
    p.add_argument("-o", "--output", type=Path, default=Path("lexa_capture.wav"), help="Fichier WAV de sortie")
    p.add_argument("--chunk-timeout", type=float, default=2.0, help="Timeout par lecture de trame (s)")
    p.add_argument(
        "--raw-pcm",
        action="store_true",
        help="Pas de sync LXCA : lit int16 LE en continu (firmware capture_audio_raw).",
    )
    p.add_argument(
        "--skip-bytes",
        type=int,
        default=0,
        help="Avec --raw-pcm : octets à ignorer au début (boot / logs), idéalement pair.",
    )
    args = p.parse_args()

    if args.raw_pcm:
        record_wav_raw_pcm(
            args.port,
            args.baud,
            args.duration,
            args.output,
            args.chunk_timeout,
            args.skip_bytes,
        )
    else:
        record_wav(args.port, args.baud, args.duration, args.output, args.chunk_timeout)


if __name__ == "__main__":
    main()
