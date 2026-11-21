#!/usr/bin/env python3
"""Renders short audition clips of selected factory presets to docs/audio/*.mp3.

The source is the harness's synthesised strummed-guitar phrase (a plucked-string model, mono DI, peaks at
-6 dBFS), so these show what each preset does to a plain guitar-like signal - they are not a substitute for
trying the plugin on a real instrument.

    python3 tools/make_demos.py [--out docs/audio] [preset name fragments...]
"""
import os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import soundfile as sf

DEFAULT = ["Swoon", "Soft Focus Morning", "Glass Lanterns", "Held Arm", "Strum Dip", "Wall of Rose", "Smeared Wall",
           "Octave Halo", "Velcro Bloom", "Diving Bell", "Cascade", "Backwards Postcard", "Rack Unit Bloom"]


def main():
    args = sys.argv[1:]
    out = "docs/audio"
    if "--out" in args:
        i = args.index("--out"); out = args[i + 1]; del args[i:i + 2]
    wanted = args or DEFAULT

    import tempfile
    os.environ.setdefault("SWOON_TMP", tempfile.mkdtemp(prefix="swoon-demo-"))
    import swoonlab as lab
    out = os.path.join(lab.ROOT, out) if not os.path.isabs(out) else out
    os.makedirs(out, exist_ok=True)

    listing = [l.split("\t") for l in subprocess.run([lab.RENDER, "list-presets"], capture_output=True, text=True).stdout.strip().splitlines()]

    def save(name, x, fs, tail_keep):
        n = min(len(x), int((10.0 + tail_keep) * fs))
        x = x[:n].copy()
        fade = int(1.5 * fs)
        x[-fade:] *= np.linspace(1.0, 0.0, fade)[:, None] ** 2
        path = os.path.join(out, name + ".mp3")
        sf.write(path, x.astype(np.float32), fs, format="MP3", subtype="MPEG_LAYER_III")
        print(f"{path}  ({os.path.getsize(path) // 1024} kB)")

    # the dry phrase, for comparison (through nothing but the bypassed engine)
    x, fs, _ = lab.render("demo_dry", bare=True, tail=2.0, sets=dict(bypass=1))
    save("00-dry-di", x, fs, 1.0)

    count = 1
    for idx, cat, name in listing:
        if not any(w.lower() in name.lower() for w in wanted):
            continue
        x, fs, info = lab.render("demo", preset=idx, tail=14.0)
        slug = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
        save(f"{count:02d}-{slug}", x, fs, 9.0 if cat in ("Ambient",) else 6.0)
        count += 1


if __name__ == "__main__":
    main()
