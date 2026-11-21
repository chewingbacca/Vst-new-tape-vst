"""Small lab helpers used while tuning: render through swoon_render and measure."""
import json, os, subprocess, tempfile
import numpy as np
import soundfile as sf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RENDER = os.environ.get("SWOON_RENDER", os.path.join(ROOT, "build", "swoon_render"))
TMP = os.environ.get("SWOON_TMP", tempfile.gettempdir())


def render(name, signal="strum", solo=None, bare=False, preset=None, sets=None, seconds=10.0, tail=4.0,
           level=-12.0, sr=48000, block=256, keep_input=False, events=None, cycle_presets=None):
    out = os.path.join(TMP, name + ".wav")
    cmd = [RENDER, "render", "--out", out, "--signal", signal, "--seconds", str(seconds), "--tail", str(tail),
           "--level", str(level), "--sr", str(sr), "--block", str(block)]
    if solo: cmd += ["--solo", solo]
    if bare: cmd += ["--bare"]
    if preset is not None: cmd += ["--preset", str(preset)]
    for k, v in (sets or {}).items():
        cmd += ["--set", f"{k}={v}"]
    for (t, k, v) in (events or []):
        cmd += ["--at", f"{t}:{k}={v}"]
    if cycle_presets:
        cmd += ["--cycle-presets", str(cycle_presets)]
    if keep_input:
        cmd += ["--write-input", os.path.join(TMP, name + "_in.wav")]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode not in (0, 2):
        raise RuntimeError(res.stderr)
    info = json.loads(res.stdout.strip().splitlines()[-1])
    x, fs = sf.read(out, always_2d=True)
    return x.astype(np.float64), fs, info


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-12))


def rms_db(x):
    return db(np.sqrt(np.mean(np.square(x))))


def rt60(ir, fs, start_db=-5.0, end_db=-35.0):
    """Schroeder backward integration; returns T60 extrapolated from the start..end dB slope."""
    e = np.cumsum((ir ** 2)[::-1])[::-1]
    e_db = 10 * np.log10(e / e[0] + 1e-30)
    i0 = np.argmax(e_db <= start_db); i1 = np.argmax(e_db <= end_db)
    if i1 <= i0:
        return float("nan")
    t = np.arange(len(ir)) / fs
    slope = np.polyfit(t[i0:i1], e_db[i0:i1], 1)[0]
    return -60.0 / slope


def pitch_track_cents(x, fs, f0, smooth_ms=20.0):
    """Instantaneous frequency deviation (cents) of a sine-ish signal around f0."""
    from scipy.signal import hilbert, butter, sosfiltfilt
    sos = butter(4, [f0 * 0.7, f0 * 1.4], btype="band", fs=fs, output="sos")
    y = sosfiltfilt(sos, x)
    ph = np.unwrap(np.angle(hilbert(y)))
    inst = np.diff(ph) * fs / (2 * np.pi)
    k = max(1, int(smooth_ms * 0.001 * fs))
    inst = np.convolve(inst, np.ones(k) / k, mode="same")
    return 1200 * np.log2(np.maximum(inst, 1e-3) / f0)


def band_rt60(ir, fs, lo, hi):
    from scipy.signal import butter, sosfiltfilt
    sos = butter(4, [lo, hi], btype="band", fs=fs, output="sos")
    return rt60(sosfiltfilt(sos, ir), fs)


def render_array(name, x, fs=48000, **kw):
    """Renders a numpy array (mono or stereo) through the engine."""
    path = os.path.join(TMP, name + "_src.wav")
    sf.write(path, x.astype(np.float32), fs, subtype="FLOAT")
    return render(name, signal="file:" + path, seconds=0, sr=fs, **kw)


def click_report(x, fs, event_times, hp_hz=6000.0, window_ms=30.0):
    """High-passes the output and compares the peak near each event with the steady level elsewhere.
    Returns (worst_event_peak_db, steady_p999_db, worst_time)."""
    from scipy.signal import butter, sosfilt
    sos = butter(6, hp_hz, btype="high", fs=fs, output="sos")
    h = np.max(np.abs(sosfilt(sos, x, axis=0)), axis=1)
    w = int(window_ms * 0.001 * fs)
    near = np.zeros(len(h), dtype=bool)
    worst, worst_t = -200.0, None
    for t in event_times:
        a = max(0, int(t * fs) - w // 6); b = min(len(h), int(t * fs) + w)
        near[a:b] = True
        if b > a:
            v = db(h[a:b].max())
            if v > worst:
                worst, worst_t = v, t
    steady = h[~near]
    return worst, db(np.percentile(steady, 99.9)) if len(steady) else -200.0, worst_t
