"""Print measurements for a WAV file: levels, DC, clicks, spectrum summary, stereo correlation.

usage: analyze.py file.wav [t0 t1]
"""
import sys
import numpy as np
from wavio import read_wav


def db(v):
    return 20 * np.log10(max(float(v), 1e-12))


def click_report(m, sr):
    """Flag isolated sample-to-sample jumps far larger than their neighbourhood (a crude click detector)."""
    d = np.abs(np.diff(m))
    if len(d) < 256:
        return []
    win = max(32, int(sr * 0.002))
    # local scale: median of |diff| in a window around each sample
    k = len(d) // win
    med = np.median(d[:k * win].reshape(k, win), axis=1)
    med = np.repeat(med, win)
    med = np.pad(med, (0, len(d) - len(med)), mode='edge')
    idx = np.where((d > 0.2) & (d > 12 * (med + 1e-4)))[0]
    out = []
    last = -10 ** 9
    for i in idx:
        if i - last > win:
            out.append((int(i), float(d[i])))
        last = i
    return out


def main():
    path = sys.argv[1]
    x, sr = read_wav(path)
    if len(sys.argv) > 3:
        x = x[int(float(sys.argv[2]) * sr):int(float(sys.argv[3]) * sr)]
    n = len(x)
    print(f'{path}: {n / sr:.3f} s, {x.shape[1]} ch, {sr} Hz')
    if not np.all(np.isfinite(x)):
        print('  ** contains NaN/Inf **')
        x = np.nan_to_num(x)
    for c in range(x.shape[1]):
        ch = x[:, c]
        print(f'  ch{c}: peak {db(np.abs(ch).max()):6.1f} dB  rms {db(np.sqrt(np.mean(ch ** 2))):6.1f} dB  dc {ch.mean():+.5f}'
              f'  max step {np.abs(np.diff(ch)).max():.3f}')
    m = x.mean(axis=1)
    if x.shape[1] > 1:
        a, b = x[:, 0], x[:, 1]
        denom = np.sqrt((a ** 2).sum() * (b ** 2).sum()) + 1e-20
        print(f'  stereo correlation {float((a * b).sum() / denom):+.3f}')
    clicks = click_report(m, sr)
    print(f'  click candidates: {len(clicks)}' + (''.join(f'\n    at {i / sr:.4f} s (sample {i}) step {v:.3f}' for i, v in clicks[:8])))
    # spectrum of the whole thing
    N = 1 << int(np.ceil(np.log2(max(1024, min(n, sr * 4)))))
    seg = m[:N] if n >= N else np.pad(m, (0, N - n))
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) / (N / 4)
    f = np.fft.rfftfreq(N, 1 / sr)
    p = S ** 2
    cent = float((f * p).sum() / (p.sum() + 1e-20))
    print(f'  spectral centroid {cent:.0f} Hz')
    bands = [(20, 60), (60, 150), (150, 400), (400, 1000), (1000, 2500), (2500, 6000), (6000, 12000), (12000, sr / 2)]
    tot = p.sum() + 1e-20
    print('  band energy: ' + '  '.join(f'{int(lo)}-{int(hi)}: {10 * np.log10(p[(f >= lo) & (f < hi)].sum() / tot + 1e-12):.0f}dB' for lo, hi in bands))
    # strongest peaks
    Sd = 20 * np.log10(S + 1e-12)
    pk = [i for i in range(2, len(S) - 2) if S[i] > S[i - 1] and S[i] >= S[i + 1] and S[i] > S[i - 2] and S[i] > S[i + 2]]
    pk.sort(key=lambda i: -S[i])
    print('  top peaks: ' + ', '.join(f'{f[i]:.1f}Hz {Sd[i]:.0f}dB' for i in sorted(pk[:10])))
    # envelope: time to -20 / -60 dB from the peak
    env = np.abs(m)
    w = max(1, int(sr * 0.005))
    env = np.convolve(env, np.ones(w) / w, mode='same')
    ip = int(np.argmax(env)); pkv = env[ip]
    def time_to(dbdown):
        th = pkv * 10 ** (-dbdown / 20)
        after = np.where(env[ip:] < th)[0]
        return after[0] / sr if len(after) else float('nan')
    print(f'  envelope: peak at {ip / sr * 1000:.1f} ms, -20 dB after {time_to(20) * 1000:.0f} ms, -60 dB after {time_to(60) * 1000:.0f} ms')


if __name__ == '__main__':
    main()
