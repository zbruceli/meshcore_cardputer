#!/usr/bin/env python3
"""Relative LoRa TX power with an RTL-SDR: one line per burst, so devices can be compared at the same spot.

The dongle isn't calibrated: only compare runs taken with the same gain, position and antenna.

Setup:   brew install librtlsdr && pip install "pyrtlsdr==0.2.93" numpy
         (newer pyrtlsdr needs rtlsdr_set_dithering, which Homebrew's librtlsdr 2.0.x doesn't have)
Check:   rtl_test -t
Measure: python3 scripts/sdr_burst.py --label tdeck --gain 10 --csv bursts.csv     (Ctrl-C to stop)
         python3 scripts/sdr_burst.py --label cardputer-ours --gain 10 --csv bursts.csv
Compare: python3 scripts/sdr_burst.py --compare bursts.csv
Offline: rtl_sdr -f 910275000 -s 1024000 -g 10 rec.cu8, then --iq-file rec.cu8 (same --offset and --rate)
Raw IQ:  add --save-iq DIR to keep each burst as DIR/<label>-<time>-<n>.cu8 (~2 MB per second of burst)
Plot:    python3 scripts/sdr_burst.py --plot DIR/*.cu8 --out bursts.png   (needs matplotlib; same --offset/--rate)

Lower --gain until no burst is flagged CLIP; keep it fixed for every device in a session.
"""
import argparse
import csv
import os
import sys
import time

import numpy as np

BLOCK_MS = 1                  # power is measured per 1 ms block
CHUNK = 1 << 18               # samples per read (~0.26 s at 1.024 MS/s)
START_DB, END_DB = 10, 6      # burst starts this far above the noise floor, ends when it drops below END_DB
MIN_BURST_MS, END_HOLD_MS = 5, 3
PRE_MS = 20                   # --save-iq keeps this much noise before each burst
RING_S = 4                    # seconds of raw samples kept for --save-iq (longest MeshCore packet is ~1.5 s)


def lowpass_taps(rate, cutoff, n=129):
    t = np.arange(n) - (n - 1) / 2
    h = np.sinc(2 * cutoff / rate * t) * np.hamming(n)
    return (h / h.sum()).astype(np.float32)


class BurstMeter:
    def __init__(self, a):
        self.a = a
        self.blk = int(a.rate * BLOCK_MS / 1000)
        # Wider than the LoRa channel so a few tens of ppm of dongle error still fits inside
        self.taps = lowpass_taps(a.rate, a.bw / 2 + 40e3)
        self.tail = np.zeros(len(self.taps) - 1, np.complex64)
        self.n = 0                       # samples processed, for the mixer phase
        self.floor_hist = []             # recent quiet-block powers
        self.burst = None                # list of (power, spectrum centroid) while in a burst
        self.quiet = 0
        self.clip = 0
        self.results = []
        self.ring = []                   # (first sample index, raw chunk) for --save-iq
        self.b_start = 0                 # sample index where the current burst starts

    def floor(self):
        return np.median(self.floor_hist) if self.floor_hist else None

    def feed(self, x, clip_frac):
        a = self.a
        n0 = self.n
        if a.save_iq:
            self.ring = [c for c in self.ring if c[0] + len(c[1]) > n0 - RING_S * a.rate] + [(n0, x)]
        t = (n0 + np.arange(len(x))) / a.rate
        self.n += len(x)
        y = x * np.exp(-2j * np.pi * a.offset * t).astype(np.complex64)   # channel to 0 Hz
        z = np.convolve(np.concatenate([self.tail, y]), self.taps, mode="valid")
        self.tail = y[-(len(self.taps) - 1):]
        nb = len(z) // self.blk
        z = z[:nb * self.blk].reshape(nb, self.blk)
        p = 10 * np.log10(np.mean(np.abs(z) ** 2, axis=1) + 1e-20)
        spec = np.abs(np.fft.fft(z, axis=1)) ** 2
        f = np.fft.fftfreq(self.blk, 1 / a.rate)
        keep = np.abs(f) < a.bw / 2 + 40e3
        cent = (spec[:, keep] * f[keep]).sum(1) / (spec[:, keep].sum(1) + 1e-20)
        for k, (pi, ci) in enumerate(zip(p, cent)):
            self.step(pi, ci, clip_frac, n0 + (k + 1) * self.blk)

    def step(self, p, cent, clip_frac, end):   # end: sample index just after this block
        fl = self.floor()
        if self.burst is None:
            if fl is not None and p > fl + START_DB:
                self.burst, self.quiet, self.clip = [(p, cent)], 0, clip_frac
                self.b_start = end - self.blk
            else:
                self.floor_hist = (self.floor_hist + [p])[-2000:]
            return
        self.clip = max(self.clip, clip_frac)
        if p < fl + END_DB:
            self.quiet += 1
            if self.quiet >= END_HOLD_MS:
                self.close(fl, end)
            return
        self.quiet = 0
        self.burst.append((p, cent))

    def close(self, fl, end):
        b, self.burst = self.burst, None
        if len(b) < MIN_BURST_MS:
            return
        core = b[2:-2] if len(b) > 8 else b          # skip the PA ramp at either end
        pw = np.array([v[0] for v in core])
        lin = 10 ** (pw / 10)
        r = {
            "label": self.a.label,
            "time": time.strftime("%H:%M:%S"),
            "dur_ms": len(b) * BLOCK_MS,
            "mean_dbfs": round(10 * np.log10(lin.mean()), 2),
            "peak_dbfs": round(float(pw.max()), 2),
            "snr_db": round(10 * np.log10(lin.mean()) - fl, 1),
            "foff_hz": int(np.mean([v[1] for v in core])),
            "clip": int(self.clip > 0.001),
        }
        self.results.append(r)
        print(f"{r['time']}  {r['label']:<16} {r['dur_ms']:5d} ms  {r['mean_dbfs']:7.2f} dBFS  "
              f"snr {r['snr_db']:5.1f} dB  foff {r['foff_hz']:+6d} Hz{'  CLIP' if r['clip'] else ''}", flush=True)
        if self.a.save_iq:
            self.save(r, end)
        if self.a.csv:
            new = not os.path.exists(self.a.csv)
            with open(self.a.csv, "a", newline="") as fh:
                w = csv.DictWriter(fh, fieldnames=list(r))
                if new:
                    w.writeheader()
                w.writerow(r)


    def save(self, r, end):
        """Write the burst (plus PRE_MS of noise before it) as rtl_sdr-style unsigned 8-bit IQ."""
        start = self.b_start - int(PRE_MS / 1000 * self.a.rate)
        x = np.concatenate([c[1] for c in self.ring])[max(0, start - self.ring[0][0]):end - self.ring[0][0]]
        u = np.empty(2 * len(x), np.uint8)
        u[0::2] = np.clip(np.round(x.real * 127.5 + 127.5), 0, 255)
        u[1::2] = np.clip(np.round(x.imag * 127.5 + 127.5), 0, 255)
        os.makedirs(self.a.save_iq, exist_ok=True)
        path = os.path.join(self.a.save_iq, f"{r['label']}-{r['time'].replace(':', '')}-{len(self.results)}.cu8")
        u.tofile(path)
        print(f"  saved {path}", flush=True)


def read_cu8(path):
    u = np.fromfile(path, np.uint8).astype(np.float32)
    return (((u[0::2] - 127.5) + 1j * (u[1::2] - 127.5)) / 127.5).astype(np.complex64)


def plot(a):
    """Amplitude over time and averaged spectrum of saved bursts, one colour per label (file name before the time)."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, (ax_t, ax_f) = plt.subplots(1, 2, figsize=(12, 4.8))
    colours, labels = plt.rcParams["axes.prop_cycle"].by_key()["color"], []
    blk, taps = int(a.rate * BLOCK_MS / 1000), lowpass_taps(a.rate, a.bw / 2 + 40e3)
    nfft = 4096
    win = np.hanning(nfft).astype(np.float32)
    for path in a.plot:
        x = read_cu8(path)
        label = os.path.basename(path).rsplit("-", 2)[0]
        if label not in labels:
            labels.append(label)
        c = colours[labels.index(label) % len(colours)]
        # amplitude: channel filtered to 0 Hz, power per 1 ms block
        y = x * np.exp(-2j * np.pi * a.offset * np.arange(len(x)) / a.rate).astype(np.complex64)
        z = np.convolve(y, taps, mode="same")
        nb = len(z) // blk
        pw = 10 * np.log10(np.mean(np.abs(z[:nb * blk].reshape(nb, blk)) ** 2, axis=1) + 1e-20)
        ax_t.plot(np.arange(nb) * BLOCK_MS, pw, color=c, lw=1, alpha=0.8)
        # spectrum: Welch average over the burst (skipping the leading noise), centred on the channel
        seg = x[int(PRE_MS / 1000 * a.rate):]
        n = len(seg) // nfft
        if n == 0:
            continue
        spec = np.mean(np.abs(np.fft.fft(seg[:n * nfft].reshape(n, nfft) * win, axis=1)) ** 2, axis=0)
        f = np.fft.fftshift(np.fft.fftfreq(nfft, 1 / a.rate)) - a.offset
        ax_f.plot(f / 1e3, 10 * np.log10(np.fft.fftshift(spec) / (win.sum() ** 2) + 1e-20), color=c, lw=1, alpha=0.8)
    for i, label in enumerate(labels):
        ax_t.plot([], [], color=colours[i % len(colours)], label=label)
    ax_t.set(title="Burst amplitude (channel-filtered, 1 ms blocks)", xlabel="ms from file start", ylabel="dBFS")
    ax_f.axvspan(-a.bw / 2e3, a.bw / 2e3, color="grey", alpha=0.12, label=f"LoRa channel ({a.bw / 1e3:g} kHz)")
    ax_f.set(title=f"Spectrum around {a.freq / 1e6:.3f} MHz", xlabel="kHz from channel centre", ylabel="dBFS / bin",
             xlim=(-3 * a.bw / 1e3, 3 * a.bw / 1e3))
    for ax in (ax_t, ax_f):
        ax.grid(alpha=0.3)
        ax.legend(fontsize=8)
    fig.suptitle(f"{len(a.plot)} bursts, {', '.join(labels)}", fontsize=10)
    fig.tight_layout()
    fig.savefig(a.out, dpi=130)
    print(f"wrote {a.out}")


def chunks_from_sdr(a):
    # ctypes doesn't search Homebrew's lib dirs on macOS
    os.environ.setdefault("DYLD_FALLBACK_LIBRARY_PATH", "/opt/homebrew/lib:/usr/local/lib:/usr/lib")
    from rtlsdr import RtlSdr
    sdr = RtlSdr()
    sdr.sample_rate = a.rate
    sdr.center_freq = a.freq - a.offset
    if a.ppm:
        sdr.freq_correction = a.ppm
    sdr.gain = a.gain
    print(f"tuned {sdr.center_freq / 1e6:.4f} MHz (channel {a.freq / 1e6:.4f} MHz at +{a.offset / 1e3:.0f} kHz), "
          f"{sdr.sample_rate / 1e6:.3f} MS/s, gain {sdr.gain} dB, ppm {a.ppm}", file=sys.stderr)
    sdr.read_samples(CHUNK)   # discard the first read (settling)
    try:
        while True:
            x = sdr.read_samples(CHUNK).astype(np.complex64)
            yield x, float(np.mean((np.abs(x.real) > 0.99) | (np.abs(x.imag) > 0.99)))
    finally:
        sdr.close()


def chunks_from_file(a):
    raw = np.fromfile(a.iq_file, np.uint8)
    for i in range(0, len(raw) - 1, 2 * CHUNK):
        u = raw[i:i + 2 * CHUNK].astype(np.float32)
        x = ((u[0::2] - 127.5) + 1j * (u[1::2] - 127.5)) / 127.5
        clip = float(np.mean((raw[i:i + 2 * CHUNK] == 0) | (raw[i:i + 2 * CHUNK] == 255)))
        yield x.astype(np.complex64), clip


def summary(rows):
    by = {}
    for r in rows:
        by.setdefault(r["label"], []).append(r)
    ref = None
    print(f"\n{'label':<16} {'n':>3} {'median dBFS':>12} {'spread':>7} {'vs first':>9} {'dur ms':>7} {'foff Hz':>8} clip")
    for label, rs in by.items():
        p = np.array([float(r["mean_dbfs"]) for r in rs])
        med = float(np.median(p))
        ref = med if ref is None else ref
        print(f"{label:<16} {len(rs):3d} {med:12.2f} {p.max() - p.min():7.2f} {med - ref:+9.2f} "
              f"{int(np.median([int(r['dur_ms']) for r in rs])):7d} {int(np.median([int(r['foff_hz']) for r in rs])):+8d} "
              f"{sum(int(r['clip']) for r in rs)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--label", default="device", help="name for this device/run, e.g. tdeck, cardputer-fork")
    ap.add_argument("--freq", type=float, default=910.525e6, help="LoRa channel centre (Hz)")
    ap.add_argument("--bw", type=float, default=62.5e3, help="LoRa bandwidth (Hz)")
    ap.add_argument("--offset", type=float, default=250e3, help="tune this far below the channel, away from the DC spike")
    ap.add_argument("--rate", type=float, default=1.024e6, help="sample rate (S/s)")
    ap.add_argument("--gain", type=float, default=10, help="manual tuner gain (dB); keep fixed in a session")
    ap.add_argument("--ppm", type=int, default=0, help="dongle frequency correction")
    ap.add_argument("--csv", help="append one row per burst to this file")
    ap.add_argument("--iq-file", help="read an rtl_sdr .cu8 recording instead of the dongle")
    ap.add_argument("--compare", metavar="CSV", help="print per-label medians from a CSV and exit")
    ap.add_argument("--save-iq", metavar="DIR", help="save each burst's raw IQ as DIR/<label>-<time>-<n>.cu8")
    ap.add_argument("--plot", nargs="+", metavar="CU8", help="plot amplitude and spectrum of saved bursts and exit")
    ap.add_argument("--out", default="bursts.png", help="image written by --plot")
    a = ap.parse_args()

    if a.plot:
        plot(a)
        return

    if a.compare:
        with open(a.compare) as fh:
            summary(list(csv.DictReader(fh)))
        return
    m = BurstMeter(a)
    try:
        for x, clip in (chunks_from_file(a) if a.iq_file else chunks_from_sdr(a)):
            m.feed(x, clip)
    except KeyboardInterrupt:
        pass
    if m.results:
        summary(m.results)
    else:
        print("no bursts detected", file=sys.stderr)


if __name__ == "__main__":
    main()
