#!/usr/bin/env python3
"""Verdant analysis helpers: spectrograms, contact sheets and measurements for rendered WAVs.

  analyze.py spec  in.wav out.png            log-frequency spectrogram + waveform + level
  analyze.py sheet out.png a.wav b.wav ...   grid of spectrograms
  analyze.py stats in.wav                    numeric summary
  analyze.py report DIR                      summarise DIR/report.jsonl from `verdant_render --suite`
"""
import json, sys, os
import numpy as np
from scipy.io import wavfile
from scipy import signal

def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim == 1: x = np.stack([x, x], axis=1)
    return sr, x

def db(x): return 20 * np.log10(np.maximum(np.abs(x), 1e-12))

def spectrogram(ax, sr, mono, title='', fmin=30.0, vmin=-100, vmax=-10):
    nfft = 4096 if sr <= 50000 else 8192
    f, t, S = signal.spectrogram(mono, sr, window='hann', nperseg=nfft, noverlap=nfft * 3 // 4, scaling='spectrum', mode='magnitude')
    S = db(S)
    ax.pcolormesh(t, f[1:], S[1:], vmin=vmin, vmax=vmax, cmap='magma', shading='auto', rasterized=True)
    ax.set_yscale('log'); ax.set_ylim(fmin, min(20000, sr / 2))
    ax.set_title(title, fontsize=8); ax.tick_params(labelsize=6)

def level_curve(sr, x, win=0.05):
    n = int(win * sr)
    m = len(x) // n
    e = np.sqrt(np.mean(x[:m * n].reshape(m, n, -1) ** 2, axis=(1, 2)))
    return np.arange(m) * win, db(e)

def cmd_spec(inp, out):
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    sr, x = load(inp)
    fig, axs = plt.subplots(3, 1, figsize=(10, 6.5), gridspec_kw={'height_ratios': [3, 1, 1]})
    spectrogram(axs[0], sr, x.mean(axis=1), os.path.basename(inp))
    t = np.arange(len(x)) / sr
    axs[1].plot(t, x[:, 0], lw=0.3, color='#2a7'); axs[1].plot(t, x[:, 1], lw=0.3, color='#a52', alpha=0.6)
    axs[1].set_xlim(0, t[-1]); axs[1].set_ylim(-1, 1); axs[1].tick_params(labelsize=6)
    tt, e = level_curve(sr, x)
    axs[2].plot(tt, e, lw=0.8); axs[2].set_ylim(-100, 0); axs[2].set_xlim(0, t[-1]); axs[2].grid(alpha=0.3); axs[2].tick_params(labelsize=6)
    fig.tight_layout(); fig.savefig(out, dpi=90); plt.close(fig)

def cmd_sheet(out, files, cols=3):
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    rows = (len(files) + cols - 1) // cols
    fig, axs = plt.subplots(rows, cols, figsize=(4.2 * cols, 2.4 * rows), squeeze=False)
    for ax in axs.flat: ax.axis('off')
    for ax, f in zip(axs.flat, files):
        ax.axis('on')
        sr, x = load(f)
        spectrogram(ax, sr, x.mean(axis=1), os.path.basename(f).replace('.wav', ''))
    fig.tight_layout(); fig.savefig(out, dpi=80); plt.close(fig)

def stats(path):
    sr, x = load(path)
    m, s = x.mean(axis=1), (x[:, 0] - x[:, 1]) / 2
    f, P = signal.welch(m, sr, nperseg=8192)
    def band(lo, hi):
        k = (f >= lo) & (f < hi)
        return 10 * np.log10(max(P[k].sum() * (f[1] - f[0]), 1e-20))
    centroid = float((f * P).sum() / max(P.sum(), 1e-20))
    return dict(peak_db=float(db(np.max(np.abs(x)))), rms_db=float(db(np.sqrt(np.mean(x ** 2)))),
                dc=float(x.mean()), side_to_mid_db=float(db(np.sqrt(np.mean(s ** 2))) - db(np.sqrt(np.mean(m ** 2)))),
                corr=float(np.corrcoef(x[:, 0], x[:, 1])[0, 1]) if x[:, 0].std() > 0 and x[:, 1].std() > 0 else 1.0,
                centroid_hz=centroid, sub_db=band(20, 80), low_db=band(80, 300), mid_db=band(300, 2500),
                high_db=band(2500, 8000), air_db=band(8000, 20000),
                crest_db=float(db(np.max(np.abs(x))) - db(np.sqrt(np.mean(x ** 2)))))

def max_click(path, hp=6000.0):
    """Largest sample-to-sample jump relative to local level: a crude click detector."""
    sr, x = load(path)
    d = np.abs(np.diff(x, axis=0)).max(axis=1)
    return float(d.max()), float(np.argmax(d) / sr)

def cmd_report(d):
    rows = [json.loads(l) for l in open(os.path.join(d, 'report.jsonl'))]
    bad = [r for r in rows if not r['ok']]
    print(f"{len(rows)} renders, {len(bad)} failed")
    for r in bad: print('  FAIL', r['name'], r['failures'])
    at48 = [r for r in rows if r['sr'] == 48000]
    print('worst cpu @48k: %.2f %% (%s)' % max((r['cpu_pct'], r['name']) for r in at48))
    print('max peak: %.2f dBFS' % max(r['peak_db'] for r in rows))
    print('max |dc|: %.5f' % max(abs(r['dc']) for r in rows if r['signal'] != 'hostile' and not r['name'].startswith('automation')))

if __name__ == '__main__':
    c = sys.argv[1] if len(sys.argv) > 1 else ''
    if c == 'spec': cmd_spec(sys.argv[2], sys.argv[3])
    elif c == 'sheet': cmd_sheet(sys.argv[2], sys.argv[3:])
    elif c == 'stats': print(json.dumps(stats(sys.argv[2]), indent=1))
    elif c == 'report': cmd_report(sys.argv[2])
    else: print(__doc__)
