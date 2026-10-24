# /// script
# dependencies = ["numpy"]
# ///
"""Dechirp a LoRa burst from an rtl_sdr .cu8 recording and print its raw symbols.

Usage: uv run lora_symbols.py FILE.cu8 START_S [START_S ...] [--sf 7] [--bw 125e3] [--tuned 915e6]

For each burst: estimates the chirp center, resamples to the chip rate, finds the
preamble, then prints symbol values (0..2^SF-1) relative to the preamble. In a
standard LoRa frame the two symbols after the preamble encode the sync word
(SX127x: sync byte nibbles x 8, e.g. 0x12 -> 8, 16; 0x34 -> 24, 32).
"""
import argparse

import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument("file")
ap.add_argument("starts", type=float, nargs="+")
ap.add_argument("--sf", type=int, default=7)
ap.add_argument("--bw", type=float, default=125e3)
ap.add_argument("--fs", type=float, default=1_024_000)
ap.add_argument("--tuned", type=float, default=915e6)
ap.add_argument("--dur-ms", type=float, default=100)
a = ap.parse_args()

N = 2 ** a.sf
fs, bw = a.fs, a.bw
n = np.arange(N)
up = np.exp(1j * np.pi * n * n / N - 1j * np.pi * n)  # base upchirp at 1 sample/chip
down = np.conj(up)

taps = 101
t = np.arange(taps) - taps // 2
lp = np.sinc(2 * 70e3 / fs * t) * np.hamming(taps)
lp /= lp.sum()


def load(start):
    s = int((start - 0.002) * fs)
    cnt = int((a.dur_ms / 1000 + 0.004) * fs)
    raw = np.fromfile(a.file, dtype=np.uint8, count=2 * cnt, offset=2 * s).astype(np.float32) - 127.5
    return raw[0::2] + 1j * raw[1::2]


def symbols(x, off):
    """Dechirp starting at sample offset `off` (chip rate), return list of (bin, peak/mean)."""
    out = []
    for k in range(off, len(x) - N, N):
        spec = np.abs(np.fft.fft(x[k:k + N] * down))
        b = int(np.argmax(spec))
        out.append((b, spec[b] / (spec.mean() + 1e-9)))
    return out


for start in a.starts:
    iq = load(start)
    f = np.angle(iq[1:] * np.conj(iq[:-1])) * fs / (2 * np.pi)
    mag = np.abs(iq[1:])
    strong = f[mag > 0.5 * np.percentile(mag, 95)]
    center = (np.percentile(strong, 2) + np.percentile(strong, 98)) / 2
    tt = np.arange(len(iq)) / fs
    sh = np.convolve(iq * np.exp(-2j * np.pi * center * tt), lp, mode="same")
    tc = np.arange(0, tt[-1], 1 / bw)
    x = np.interp(tc, tt, sh.real) + 1j * np.interp(tc, tt, sh.imag)

    # find the timing (0..N-1) that makes the preamble cleanest, by scanning offsets
    best = None
    for off in range(N):
        syms = symbols(x, off)
        vals = [b for b, q in syms[:20] if q > 8]
        if len(vals) < 6:
            continue
        mode = max(set(vals), key=vals.count)
        score = vals.count(mode)
        if best is None or score > best[0]:
            best = (score, off, mode)
    if best is None:
        print(f"{start:9.3f}s: no LoRa preamble found")
        continue
    _, off, pre = best
    syms = symbols(x, off)
    rel = [((b - pre) % N, q) for b, q in syms]
    # first symbol after the preamble run
    i = 0
    while i < len(rel) and (rel[i][0] != 0 or rel[i][1] < 8):
        i += 1
    while i < len(rel) and rel[i][0] == 0:
        i += 1
    npre = i
    body = [b for b, q in rel[i:] if q > 4]
    print(f"{start:9.3f}s center {(a.tuned + center)/1e6:.4f} MHz  preamble≥{npre} sym  "
          f"sync symbols {body[:2]}  then {len(body) - 2} symbols")
    print("   " + " ".join(f"{b:3d}" for b in body[:90]))
