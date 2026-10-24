# /// script
# dependencies = ["numpy"]
# ///
"""Find RF bursts in an rtl_sdr .cu8 recording and characterise each one.

Usage: uv run bursts.py FILE.cu8 [--rate 1024000] [--center 915.05e6]

For every burst prints: start time (s into the file), duration, peak power (dBFS),
carrier offset/frequency, FM deviation (FSK if large) and AM depth (ASK/OOK if large).
"""
import argparse

import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument("file")
ap.add_argument("--rate", type=float, default=1_024_000)
ap.add_argument("--center", type=float, default=915.05e6)
ap.add_argument("--thresh-db", type=float, default=12, help="dB above noise floor")
a = ap.parse_args()

raw = np.fromfile(a.file, dtype=np.uint8).astype(np.float32) - 127.5
iq = (raw[0::2] + 1j * raw[1::2]) / 127.5
fs = a.rate
win = int(fs * 0.0005)  # 0.5 ms power smoothing
pwr = np.convolve(np.abs(iq) ** 2, np.ones(win) / win, mode="same")
noise = np.median(pwr)
on = pwr > noise * 10 ** (a.thresh_db / 10)

edges = np.flatnonzero(np.diff(on.astype(np.int8)))
if on[0]:
    edges = np.r_[0, edges]
if on[-1]:
    edges = np.r_[edges, len(on) - 1]
bursts = [(s, e) for s, e in zip(edges[0::2], edges[1::2]) if (e - s) / fs > 0.0005]

# merge bursts separated by < 1 ms
merged = []
for s, e in bursts:
    if merged and (s - merged[-1][1]) / fs < 0.001:
        merged[-1] = (merged[-1][0], e)
    else:
        merged.append((s, e))

print(f"file={a.file} samples={len(iq)} ({len(iq)/fs:.1f}s) noise={10*np.log10(noise):.1f} dBFS bursts={len(merged)}")
print(" #   start_s   dur_ms  peak_dBFS  clip%  carrier_MHz  fm_dev_kHz  am_depth")
for i, (s, e) in enumerate(merged, 1):
    seg = iq[s:e]
    clip = np.mean((np.abs(raw[2 * s:2 * e]) > 126)) * 100
    ph = np.unwrap(np.angle(seg))
    inst_f = np.diff(ph) * fs / (2 * np.pi)
    inst_f = np.convolve(inst_f, np.ones(16) / 16, mode="valid")
    mag = np.abs(seg)
    strong = mag > np.percentile(mag, 50)
    f_med = np.median(inst_f[strong[:len(inst_f)]]) if strong.any() else np.median(inst_f)
    dev = (np.percentile(inst_f, 90) - np.percentile(inst_f, 10)) / 2
    am = 1 - np.percentile(mag, 10) / max(np.percentile(mag, 90), 1e-9)
    print(f"{i:2} {s/fs:9.3f} {1000*(e-s)/fs:8.2f} {10*np.log10(np.max(pwr[s:e])):10.1f} {clip:5.1f} "
          f"{(a.center + f_med)/1e6:12.4f} {dev/1000:11.1f} {am:9.2f}")
