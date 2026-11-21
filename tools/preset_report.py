#!/usr/bin/env python3
"""Renders the strummed phrase through every factory preset and prints a balance table.

Columns: playing RMS (1.7-9.2 s), peak, max limiter reduction, octave-band balance relative to the band
average (125 Hz .. 8 kHz), mean L/R correlation while playing, tail length (time for the output to fall 50 dB
below the playing level after the last note is muted at 9.2 s) and CPU."""
import sys, os, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from swoonlab import *

BANDS = [125, 250, 500, 1000, 2000, 4000, 8000]


def bands_db(x, fs):
    seg = x.mean(axis=1)
    n = 1 << 15
    hop = n // 2
    w = np.hanning(n)
    acc = np.zeros(n // 2 + 1)
    cnt = 0
    for a in range(0, len(seg) - n, hop):
        acc += np.abs(np.fft.rfft(seg[a:a + n] * w)) ** 2
        cnt += 1
    f = np.fft.rfftfreq(n, 1 / fs)
    out = []
    for c in BANDS:
        sel = (f >= c / 2 ** 0.5) & (f < c * 2 ** 0.5)
        out.append(10 * np.log10(acc[sel].sum() / max(cnt, 1) + 1e-30))
    return np.array(out)


def tail_seconds(x, fs, t_end=9.2, drop_db=50.0):
    play = rms_db(x[int(1.7 * fs):int(9.2 * fs)])
    hop = int(0.05 * fs)
    start = int(t_end * fs)
    last = 0.0
    for i in range(start, len(x) - hop, hop):
        if rms_db(x[i:i + hop]) > play - drop_db:
            last = (i - start) / fs
    return last


def main():
    names = [l.split("\t") for l in subprocess.run([RENDER, "list-presets"], capture_output=True, text=True).stdout.strip().splitlines()]
    only = sys.argv[1:] 
    print(f"{'#':>2} {'preset':22s} {'cat':9s} {'rms':>6} {'peak':>6} {'lim':>5} | " + " ".join(f"{b:>5}" for b in BANDS) + f" | {'corr':>5} {'tail':>5} {'cpu':>5}")
    rows = []
    for idx, cat, name in names:
        if only and not any(o.lower() in name.lower() or o == idx for o in only):
            continue
        x, fs, info = render("preset", preset=idx, tail=30.0)
        play = x[int(1.7 * fs):int(9.2 * fs)]
        b = bands_db(play, fs); b = b - b.mean()
        l, r = play[:, 0], play[:, 1]
        corr = np.sum(l * r) / np.sqrt(np.sum(l * l) * np.sum(r * r) + 1e-30)
        hop = int(0.05 * fs)
        env = np.array([rms_db(play[i:i + hop]) for i in range(0, len(play) - hop, hop)])
        row = dict(idx=idx, name=name, cat=cat, rms=rms_db(play), peak=info["peakDb"], lim=info["limiterDb"], bands=b,
                   dyn=float(np.percentile(env, 95) - np.percentile(env, 5)),
                   corr=corr, tail=tail_seconds(x, fs), cpu=info["cpuPct"], bad=info["bad"])
        rows.append(row)
        flag = " BAD" if row["bad"] else ""
        print(f"{idx:>2} {name[:22]:22s} {cat[:9]:9s} {row['rms']:6.1f} {row['peak']:6.1f} {row['lim']:5.1f} | " +
              " ".join(f"{v:5.1f}" for v in b) + f" | {corr:5.2f} {row['tail']:5.1f} {row['cpu']:5.1f}{flag}")
    return rows


# loudness targets (dBFS RMS of the playing section) by character
def target_for(row):
    if row["cat"] == "Utility":
        return None
    # sustaining fuzz walls have almost no dynamics left (long reverbs flatten dynamics too, hence the category test)
    fuzzy = row["dyn"] < 7.0 and row["cat"] != "Ambient"
    if fuzzy:
        return -17.5
    return -22.0 if row["cat"] == "Ambient" else -20.5


def write_trims(rows, path):
    """Trim = target - measured, limited so peaks stay under -2.5 dBFS and the trim within -9 .. +9 dB."""
    lines = []
    for row in rows:
        t = target_for(row)
        if t is None:
            continue
        trim = t - row["rms_untrimmed"]
        trim = min(trim, -2.5 - row["peak_untrimmed"])
        trim = max(-9.0, min(9.0, round(trim * 2) / 2))
        lines.append(f'        {{ "{row["name"]}", {trim:.1f}f }},')
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {len(lines)} trims to {path}")


if __name__ == "__main__":
    if "--trims" in sys.argv:
        # measure with trims neutralised, then write the table
        sys.argv.remove("--trims")
        import re
        inc = os.path.join(ROOT, "Source", "dsp", "PresetTrims.inc")
        current = dict(re.findall(r'\{ "([^"]*)", ([-0-9.]+)f \}', open(inc).read()))
        rows = main()
        for row in rows:
            applied = float(current.get(row["name"], 0.0))
            row["rms_untrimmed"] = row["rms"] - applied
            row["peak_untrimmed"] = row["peak"] - applied
        write_trims(rows, inc)
    else:
        main()
