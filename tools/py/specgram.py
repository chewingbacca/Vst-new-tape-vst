"""Render a waveform + log-frequency spectrogram PNG for a WAV file (a substitute for listening)."""
import sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from wavio import read_wav

def main():
    path, out = sys.argv[1], sys.argv[2]
    t0 = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
    t1 = float(sys.argv[4]) if len(sys.argv) > 4 else None
    x, sr = read_wav(path)
    m = x.mean(axis=1)
    a = int(t0 * sr); b = int(t1 * sr) if t1 else len(m)
    m = m[a:b]
    fig, ax = plt.subplots(2, 1, figsize=(14, 7), gridspec_kw={'height_ratios': [1, 2.4]})
    t = np.arange(len(m)) / sr + t0
    ax[0].plot(t, m, lw=0.4, color='#234')
    ax[0].set_xlim(t[0], t[-1]); ax[0].set_ylim(-1.05, 1.05); ax[0].grid(alpha=0.3)
    n = 2048; hop = 256
    win = np.hanning(n)
    frames = max(1, (len(m) - n) // hop)
    S = np.empty((n // 2 + 1, frames))
    for i in range(frames):
        S[:, i] = np.abs(np.fft.rfft(m[i * hop:i * hop + n] * win))
    S = 20 * np.log10(S / (n / 4) + 1e-7)
    f = np.fft.rfftfreq(n, 1 / sr)
    ax[1].pcolormesh(np.arange(frames) * hop / sr + t0, f[1:], S[1:], vmin=-90, vmax=-10, cmap='magma', shading='auto')
    ax[1].set_yscale('log'); ax[1].set_ylim(30, sr / 2)
    ax[1].set_ylabel('Hz'); ax[1].set_xlabel('s')
    plt.tight_layout(); plt.savefig(out, dpi=70)

if __name__ == '__main__':
    main()
