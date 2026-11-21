#!/usr/bin/env python3
"""Swoon offline verification suite.

Renders test signals through every module and every factory preset, measures the results, writes WAVs,
spectrograms and a Markdown report, and exits non-zero if any check fails.

    python3 tools/run_suite.py --out out/suite [--quick] [--no-pluginval] [--no-spectrograms]

Checks
  * unit tests (swoon_tests)
  * every module solo x {impulse, sweep, strum, bursts, silence}: finite, under the ceiling, no DC,
    silence in -> silence out, tails die away
  * every factory preset on the strummed phrase: finite, under the ceiling, not silent, limiter not
    working hard, tail decays; loudness and spectral balance are tabulated
  * fuzz aliasing: inharmonic energy on sine tests for each fuzz type
  * sample rates 44.1-192 kHz x block sizes 1-4096: finite, level consistent across rates
  * click tests: bypass, module toggles, reordering, mode switches, macro and knob jumps
  * preset cycling: stepping through every preset on a held tone, finite and bounded
  * CPU per preset (reported; machine dependent)
  * pluginval at strictness 10 if a binary is available (PLUGINVAL env var or on PATH)
"""
import argparse, json, os, shutil, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

CEILING_DB = -0.15          # the limiter's ceiling is about -0.2 dBFS
MODULES = ["fuzz", "glide", "amp", "mod", "focus", "reverse", "wash", "tape"]


class Report:
    def __init__(self):
        self.sections, self.failures, self.checks = [], [], 0

    def section(self, title):
        self.sections.append([title, []])
        print(f"\n== {title} ==", flush=True)

    def line(self, text):
        self.sections[-1][1].append(text)
        print(text, flush=True)

    def check(self, ok, what):
        self.checks += 1
        if not ok:
            self.failures.append(f"{self.sections[-1][0]}: {what}")
            self.line(f"FAIL  {what}")
        return ok

    def write(self, path, elapsed):
        with open(path, "w") as f:
            f.write("# Swoon verification report\n\n")
            f.write(f"{self.checks} checks, {len(self.failures)} failures, {elapsed:.0f} s\n\n")
            if self.failures:
                f.write("## Failures\n\n" + "\n".join(f"- {x}" for x in self.failures) + "\n\n")
            for title, lines in self.sections:
                f.write(f"## {title}\n\n```\n" + "\n".join(lines) + "\n```\n\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="out/suite")
    ap.add_argument("--quick", action="store_true", help="fewer sample-rate/block combinations, no spectrograms")
    ap.add_argument("--no-pluginval", action="store_true")
    ap.add_argument("--no-spectrograms", action="store_true")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    os.environ["SWOON_TMP"] = out
    import swoonlab as lab
    import numpy as np
    import analyze, click_tests, preset_report

    root = lab.ROOT
    rep = Report()
    t0 = time.time()
    spectro = not (args.quick or args.no_spectrograms)

    def keep(name, x, fs, title=None):
        """Renders are already on disk as <name>.wav; optionally add a spectrogram."""
        if spectro:
            analyze.spectrogram_png(os.path.join(out, name + ".wav"), os.path.join(out, name + ".png"), title or name)

    # ------------------------------------------------------------------ unit tests
    rep.section("Unit tests")
    res = subprocess.run([os.path.join(root, "build", "swoon_tests")], capture_output=True, text=True)
    for l in res.stdout.strip().splitlines():
        rep.line(l)
    rep.check(res.returncode == 0, "swoon_tests returned non-zero")

    # ------------------------------------------------------------------ modules
    rep.section("Modules, solo")
    rep.line(f"{'module':8s} {'signal':8s} {'peak':>7} {'rms':>7} {'dc':>9} {'end':>7}  notes")
    for mod in MODULES:
        extra = {"revOn": 1} if mod == "reverse" else {}
        for sig, secs, tail in [("impulse", 1.0, 9.0), ("sweep", 8.0, 6.0), ("strum", 10.0, 12.0), ("bursts:220", 6.0, 8.0),
                                ("silence", 4.0, 0.0)]:
            name = f"module_{mod}_{sig.split(':')[0]}"
            x, fs, info = lab.render(name, signal=sig, solo=mod, seconds=secs, tail=tail, level=-12, sets=extra)
            end = lab.rms_db(x[-int(0.5 * fs):])
            notes = []
            ok = rep.check(info["bad"] == 0, f"{name}: non-finite samples")
            ok &= rep.check(info["peakDb"] <= CEILING_DB, f"{name}: peak {info['peakDb']:.2f} dBFS over ceiling")
            ok &= rep.check(info["dc"] < 2e-3, f"{name}: DC offset {info['dc']:.5f}")
            if sig == "silence":
                ok &= rep.check(info["peakDb"] < -120.0, f"{name}: silence in gave {info['peakDb']:.1f} dBFS out")
            else:
                ok &= rep.check(info["rmsDb"] > -80.0, f"{name}: output is silent")
                ok &= rep.check(end < -70.0, f"{name}: tail still at {end:.1f} dBFS at the end (runaway?)")
            rep.line(f"{mod:8s} {sig.split(':')[0]:8s} {info['peakDb']:7.1f} {info['rmsDb']:7.1f} {info['dc']:9.6f} {end:7.1f}  {'ok' if ok else 'FAIL'}")
            if sig == "strum":
                keep(name, x, fs, f"{mod} solo, strummed phrase")

    # ------------------------------------------------------------------ presets
    rep.section("Factory presets (strummed phrase)")
    names = [l.split("\t") for l in subprocess.run([lab.RENDER, "list-presets"], capture_output=True, text=True).stdout.strip().splitlines()]
    rep.check(len(names) >= 25, f"only {len(names)} factory presets")
    rep.line(f"{'#':>2} {'preset':22s} {'cat':9s} {'rms':>6} {'peak':>6} {'lim':>5} | " + " ".join(f"{b:>5}" for b in preset_report.BANDS)
             + f" | {'corr':>5} {'tail':>5} {'cpu':>5}")
    worst_cpu = 0.0
    for idx, cat, pname in names:
        name = f"preset_{int(idx):02d}"
        x, fs, info = lab.render(name, preset=idx, tail=40.0)
        play = x[int(1.7 * fs):int(9.2 * fs)]
        bands = preset_report.bands_db(play, fs); bands = bands - bands.mean()
        l, r = play[:, 0], play[:, 1]
        corr = np.sum(l * r) / np.sqrt(np.sum(l * l) * np.sum(r * r) + 1e-30)
        rms = lab.rms_db(play)
        tail = preset_report.tail_seconds(x, fs)
        early, late = lab.rms_db(x[int(11 * fs):int(12 * fs)]), lab.rms_db(x[-int(1.0 * fs):])
        worst_cpu = max(worst_cpu, info["cpuPct"])
        rep.check(info["bad"] == 0, f"{pname}: non-finite samples")
        rep.check(info["peakDb"] <= CEILING_DB, f"{pname}: peak {info['peakDb']:.2f} dBFS")
        rep.check(info["dc"] < 2e-3, f"{pname}: DC offset {info['dc']:.5f}")
        rep.check(rms > -40.0, f"{pname}: nearly silent ({rms:.1f} dBFS)")
        rep.check(info["limiterDb"] < 6.0, f"{pname}: limiter working hard ({info['limiterDb']:.1f} dB)")
        rep.check(late < -90.0 or late < early - 20.0, f"{pname}: tail not decaying ({early:.1f} -> {late:.1f} dBFS)")
        rep.check(corr > -0.3, f"{pname}: strongly out-of-phase stereo (corr {corr:.2f})")
        rep.line(f"{idx:>2} {pname[:22]:22s} {cat[:9]:9s} {rms:6.1f} {info['peakDb']:6.1f} {info['limiterDb']:5.1f} | "
                 + " ".join(f"{v:5.1f}" for v in bands) + f" | {corr:5.2f} {tail:5.1f} {info['cpuPct']:5.1f}")
        keep(name, x, fs, f"{pname} ({cat})")
    rep.line(f"highest preset CPU: {worst_cpu:.1f} % of one core at 48 kHz, 256-sample blocks (this machine)")

    # ------------------------------------------------------------------ fuzz aliasing
    rep.section("Fuzz aliasing (full sustain, sine in; inharmonic energy relative to harmonics)")
    limits = {0: {1000: -90, 2500: -70, 5000: -60}, 1: {1000: -80, 2500: -60, 5000: -50}, 2: {1000: -80, 2500: -55, 5000: -45}}   # velcro is the deliberately harsh one: faster edges, more to fold
    for ftype, label in enumerate(["sustain", "octave", "velcro"]):
        for f0, limit in limits[ftype].items():
            name = f"alias_{label}_{f0}"
            x, fs, info = lab.render(name, signal=f"sine:{f0}", solo="fuzz", seconds=2.0, tail=0.2,
                                     sets=dict(fuzzType=ftype, fuzzSustain=1, fuzzStack=0, fuzzGate=0))
            a = analyze.alias_report(os.path.join(out, name + ".wav"), f0)
            rep.check(a["inharmonic_total_db"] < limit, f"{label} @ {f0} Hz: inharmonic {a['inharmonic_total_db']:.1f} dB (limit {limit})")
            rep.line(f"{label:8s} {f0:5d} Hz: inharmonic total {a['inharmonic_total_db']:7.1f} dB, worst line {a['worst_db']:7.1f} dB at {a['worst_hz']:.0f} Hz")

    # ------------------------------------------------------------------ sample rates / block sizes
    rep.section("Sample rates and block sizes (everything on)")
    rates = [44100, 48000, 96000, 192000] if args.quick else [44100, 48000, 88200, 96000, 176400, 192000]
    blocks = [64, 333] if args.quick else [1, 32, 333, 4096]
    everything = dict(revOn=1, washShimmer=0.4)
    levels = {}
    for sr in rates:
        row = []
        for block in blocks:
            secs = 3.0 if block == 1 else 10.0          # block size 1 is slow to render
            x, fs, info = lab.render("matrix", sr=sr, block=block, seconds=secs, tail=3.0, sets=everything)
            rep.check(info["bad"] == 0, f"{sr} Hz / block {block}: non-finite samples")
            rep.check(info["peakDb"] <= CEILING_DB, f"{sr} Hz / block {block}: peak {info['peakDb']:.2f}")
            if block != 1:
                levels.setdefault(sr, []).append(lab.rms_db(x[int(2 * fs):int(9 * fs)]))
            row.append(f"b{block}: {info['rmsDb']:6.1f} dB, {info['cpuPct']:5.1f} %")
        rep.line(f"{sr:6d} Hz  latency {info['latency']:4d}  " + "  ".join(row))
    means = {sr: float(np.mean(v)) for sr, v in levels.items()}
    spread = max(means.values()) - min(means.values())
    rep.check(spread < 2.0, f"level differs by {spread:.2f} dB across sample rates")
    rep.line(f"level spread across sample rates: {spread:.2f} dB")

    # ------------------------------------------------------------------ clicks
    rep.section("Click tests")
    import io, contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = click_tests.main()
    for l in buf.getvalue().strip().splitlines():
        rep.line(l)
    rep.check(rc == 0, "click tests failed")

    # ------------------------------------------------------------------ preset cycling
    rep.section("Preset cycling (new preset every 0.8 s on a held tone and on the strummed phrase)")
    n = len(names)
    for sig in ["sine:220", "strum"]:
        secs = 0.8 * n * (1 if sig == "strum" else 1)
        x, fs, info = lab.render("cycle_" + sig.split(":")[0], signal=sig, seconds=max(secs, 10.0), tail=4.0, cycle_presets=0.8, block=128)
        rep.check(info["bad"] == 0, f"cycling on {sig}: non-finite samples")
        rep.check(info["peakDb"] <= CEILING_DB, f"cycling on {sig}: peak {info['peakDb']:.2f}")
        rep.line(f"{sig:9s}: peak {info['peakDb']:6.2f} dBFS, limiter max {info['limiterDb']:.1f} dB, largest sample step {info['maxStep']:.3f}")

    # ------------------------------------------------------------------ pluginval
    if not args.no_pluginval:
        rep.section("pluginval (strictness 10)")
        pv = os.environ.get("PLUGINVAL") or shutil.which("pluginval")
        vst3 = os.path.join(root, "build", "Swoon_artefacts", "Release", "VST3", "Swoon.vst3")
        if pv and os.path.exists(vst3):
            cmd = [pv, "--strictness-level", "10", "--validate-in-process", "--timeout-ms", "600000", vst3]
            if sys.platform.startswith("linux") and not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
                cmd = ["xvfb-run", "-a"] + cmd
            env = dict(os.environ, HOME=os.path.join(out, "pluginval-home"))    # keep test presets out of the real home
            os.makedirs(env["HOME"], exist_ok=True)
            res = subprocess.run(cmd, capture_output=True, text=True, env=env)
            log = res.stdout + res.stderr
            open(os.path.join(out, "pluginval.log"), "w").write(log)
            for l in [l for l in log.splitlines() if "!!!" in l][:20]:
                rep.line(l)
            rep.line(log.strip().splitlines()[-1] if log.strip() else "(no output)")
            rep.check(res.returncode == 0, "pluginval failed (see pluginval.log)")
        else:
            rep.line("skipped: no pluginval binary (set PLUGINVAL=/path/to/pluginval) or plugin not built")

    elapsed = time.time() - t0
    rep.write(os.path.join(out, "report.md"), elapsed)
    print(f"\n{rep.checks} checks, {len(rep.failures)} failures, {elapsed:.0f} s. Report: {os.path.join(out, 'report.md')}")
    for f in rep.failures:
        print("  FAIL " + f)
    return 0 if not rep.failures else 1


if __name__ == "__main__":
    sys.exit(main())
