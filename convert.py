#!/usr/bin/env python3
"""Converte un video qualsiasi in un file leggero per il lettore DSi.

Uscita: MJPEG 256x192 + audio IMA ADPCM mono, dentro un contenitore QuickTime
salvato con estensione .mp4 (la struttura dei box e' la stessa dell'MP4).

Uso:
    python convert.py video.mp4
    python convert.py video.mkv --fps 12 --quality 6
    python convert.py cartella/            (converte tutti i video della cartella)

Richiede ffmpeg nel PATH.
"""
import argparse
import subprocess
import sys
from pathlib import Path

EXTS = {".mp4", ".mkv", ".avi", ".mov", ".webm", ".m4v", ".flv", ".wmv"}


def convert(src: Path, dst: Path, fps: int, quality: int, rate: int) -> bool:
    vf = (
        "scale=256:192:force_original_aspect_ratio=decrease:flags=lanczos,"
        "pad=256:192:(ow-iw)/2:(oh-ih)/2:black,"
        f"fps={fps},format=yuvj420p"
    )
    cmd = [
        "ffmpeg", "-y", "-i", str(src),
        "-vf", vf,
        "-c:v", "mjpeg", "-q:v", str(quality),
        "-c:a", "adpcm_ima_qt", "-ar", str(rate), "-ac", "1",
        "-movflags", "+faststart",
        "-f", "mov",            # muxer QuickTime, ma il file si chiama .mp4
        str(dst),
    ]
    print(f"> {src.name} -> {dst.name}")
    return subprocess.run(cmd).returncode == 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    p.add_argument("input", help="file video o cartella")
    p.add_argument("--fps", type=int, default=15, help="frame rate (default 15)")
    p.add_argument("--quality", type=int, default=7,
                   help="qualita' JPEG 2-31, piu' basso = migliore ma piu' pesante (default 7)")
    p.add_argument("--rate", type=int, default=22050, help="frequenza audio Hz (default 22050)")
    p.add_argument("--out", default="ds_out", help="cartella di uscita (default ds_out)")
    a = p.parse_args()

    inp = Path(a.input)
    files = sorted(f for f in inp.iterdir() if f.suffix.lower() in EXTS) if inp.is_dir() else [inp]
    if not files:
        print("Nessun video trovato.")
        return 1

    out = Path(a.out)
    out.mkdir(exist_ok=True)

    ok = 0
    for f in files:
        if convert(f, out / f"{f.stem}_ds.mp4", a.fps, a.quality, a.rate):
            ok += 1
    print(f"\nFatto: {ok}/{len(files)} file in '{out}/'. Copiali sulla SD del DSi.")
    return 0 if ok == len(files) else 2


if __name__ == "__main__":
    sys.exit(main())
