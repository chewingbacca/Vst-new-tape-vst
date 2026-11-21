#!/usr/bin/env python3
"""Analysis helpers for Swoon's offline renders.

  analyze.py spec  in.wav out.png [--title T]      waveform + spectrogram (+ L/R correlation)
  analyze.py alias in.wav f0                        harmonic vs inharmonic (aliasing) energy for a sine test
  analyze.py spectrum in.wav out.png [--start s --dur s]   averaged magnitude spectrum
  analyze.py env in.wav                             print a coarse RMS envelope (dB per 100 ms)
"""
import sys
import numpy as np
import soundfile as sf


def load(path):
    x, sr = sf.read(path, always_2d=True)
    return x.astype(np.float64), sr


def db(x):
    return 20.0 * np.log10(np.maximum(np.abs(x), 1e-12))


def alias_report(path, f0, start=0.5, dur=1.0):
    """Returns (harmonic_db, inharmonic_db, worst_inharmonic_freq) relative to the strongest harmonic."""
    x, sr = load(path)
    m = x[:, 0]
    a = int(start * sr)
    n = int(dur * sr)
    seg = m[a:a + n]
    win = np.blackman(len(seg))
    spec = np.abs(np.fft.rfft(seg * win)) / (np.sum(win) / 2)
    freqs = np.fft.rfftfreq(len(seg), 1 / sr)
    binw = freqs[1]
    mask = np.zeros(len(spec), dtype=bool)
    k = 1
    while k * f0 < sr / 2:
        idx = int(round(k * f0 / binw))
        mask[max(0, idx - 6):idx + 7] = True
        k += 1
    mask[:int(30 / binw)] = True          # ignore DC / subsonic
    harm = spec.copy(); harm[~mask] = 0
    inharm = spec.copy(); inharm[mask] = 0
    audible = freqs < 20000
    ref = harm.max()
    worst = np.argmax(inharm * audible)
    total_inh = np.sqrt(np.sum((inharm * audible) ** 2))
    total_h = np.sqrt(np.sum(harm ** 2))
    return dict(worst_db=db(inharm[worst] / ref), worst_hz=freqs[worst],
                inharmonic_total_db=db(total_inh / total_h), fundamental_db=db(ref))


def spectrogram_png(path, out, title=None):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    x, sr = load(path)
    t = np.arange(len(x)) / sr
    fig, ax = plt.subplots(3, 1, figsize=(12, 9), gridspec_kw={"height_ratios": [1, 2.2, 0.8]})
    ax[0].plot(t, x[:, 0], lw=0.4, color="#c0507a")
    ax[0].plot(t, x[:, 1], lw=0.4, color="#4070b0", alpha=0.6)
    ax[0].set_xlim(0, t[-1]); ax[0].set_ylim(-1.05, 1.05); ax[0].set_ylabel("amp")
    ax[0].set_title(title or path)
    mono = x.mean(axis=1)
    nfft = 4096
    ax[1].specgram(mono + 1e-9 * np.random.randn(len(mono)), NFFT=nfft, Fs=sr, noverlap=nfft * 3 // 4,
                   cmap="magma", vmin=-130, vmax=-30, scale="dB")
    ax[1].set_yscale("symlog", linthresh=200); ax[1].set_ylim(30, sr / 2); ax[1].set_ylabel("Hz")
    # running RMS (dB) and L/R correlation
    hop = int(0.05 * sr)
    nfr = len(x) // hop
    rms = np.array([np.sqrt(np.mean(x[i * hop:(i + 1) * hop] ** 2)) for i in range(nfr)])
    corr = []
    for i in range(nfr):
        l = x[i * hop:(i + 1) * hop, 0]; r = x[i * hop:(i + 1) * hop, 1]
        d = np.sqrt(np.sum(l * l) * np.sum(r * r))
        corr.append(np.sum(l * r) / d if d > 1e-12 else 1.0)
    tt = np.arange(nfr) * hop / sr
    ax[2].plot(tt, db(rms), color="k", lw=0.8); ax[2].set_ylim(-100, 0); ax[2].set_ylabel("RMS dB")
    ax2 = ax[2].twinx(); ax2.plot(tt, corr, color="#40a070", lw=0.6); ax2.set_ylim(-1.05, 1.05); ax2.set_ylabel("L/R corr")
    ax[2].set_xlim(0, t[-1]); ax[2].set_xlabel("s")
    fig.tight_layout(); fig.savefig(out, dpi=80); plt.close(fig)


def spectrum_png(path, out, start=0.0, dur=None, title=None):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    x, sr = load(path)
    a = int(start * sr)
    b = len(x) if dur is None else a + int(dur * sr)
    seg = x[a:b, 0]
    win = np.blackman(len(seg))
    spec = np.abs(np.fft.rfft(seg * win)) / (np.sum(win) / 2)
    freqs = np.fft.rfftfreq(len(seg), 1 / sr)
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.semilogx(freqs[1:], db(spec[1:]), lw=0.5)
    ax.set_xlim(20, sr / 2); ax.set_ylim(-140, 0); ax.grid(True, which="both", alpha=0.3)
    ax.set_title(title or path)
    fig.tight_layout(); fig.savefig(out, dpi=80); plt.close(fig)


def envelope(path, hop_s=0.1):
    x, sr = load(path)
    hop = int(hop_s * sr)
    vals = [db(np.sqrt(np.mean(x[i:i + hop] ** 2))) for i in range(0, len(x) - hop + 1, hop)]
    return np.array(vals)


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "spec":
        title = sys.argv[sys.argv.index("--title") + 1] if "--title" in sys.argv else None
        spectrogram_png(sys.argv[2], sys.argv[3], title)
    elif cmd == "alias":
        print(alias_report(sys.argv[2], float(sys.argv[3])))
    elif cmd == "spectrum":
        start = float(sys.argv[sys.argv.index("--start") + 1]) if "--start" in sys.argv else 0.0
        dur = float(sys.argv[sys.argv.index("--dur") + 1]) if "--dur" in sys.argv else None
        spectrum_png(sys.argv[2], sys.argv[3], start, dur)
    elif cmd == "env":
        e = envelope(sys.argv[2])
        print(" ".join(f"{v:.0f}" for v in e))
