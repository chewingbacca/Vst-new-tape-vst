#!/usr/bin/env python3
"""Click tests: toggle/jump parameters on a steady 220 Hz sine and look for high-frequency spikes at the
change points. A smooth change adds almost nothing above 6 kHz; a click is broadband.

Pass criterion per case: the HF peak near any event is either below ABS_OK dBFS, or no more than REL_OK dB
above the steady-state HF level of that configuration (for cases where the effect itself is bright)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from swoonlab import *

ABS_OK = -58.0
REL_OK = 9.0


def case(name, toggles, sets=None, dur=8.0, sr=48000):
    ev, times = [], []
    t, i = 1.0, 0
    while t < dur - 0.5:
        for (k, vals) in toggles:
            ev.append((t, k, vals[i % len(vals)]))
        times.append(t); t += 0.5; i += 1
    x, fs, info = render("clk", signal="sine:220", seconds=dur, tail=0.5, sets=sets, events=ev, block=64, sr=sr)
    worst, steady, wt = click_report(x, fs, times)
    ok = info["bad"] == 0 and (worst < ABS_OK or worst - steady < REL_OK)
    print(f"{'ok  ' if ok else 'FAIL'} {name:38s} event HF {worst:7.1f} dB  steady {steady:7.1f} dB  delta {worst - steady:6.1f}  (worst at {wt}s)")
    return ok


def main():
    clean = dict(fuzzOn=0)   # everything else on: a smooth signal, so clicks stand out
    results = []
    results.append(case("bypass toggle (full chain)", [("bypass", [1, 0])]))
    results.append(case("bypass toggle (no fuzz)", [("bypass", [1, 0])], sets=clean))
    results.append(case("mix 0<->1 (no fuzz)", [("mix", [0, 1])], sets=clean))
    for mod in ("fuzzOn", "glideOn", "ampOn", "modOn", "focOn", "revOn", "washOn", "tapeOn"):
        results.append(case(f"{mod} toggle", [(mod, [0, 1])], sets={} if mod == "fuzzOn" else clean))
    results.append(case("order: fuzz<->wash swap (no fuzz)", [("ordFuzz", [7, 2]), ("ordWash", [2, 7])], sets=clean))
    results.append(case("order: fuzz<->wash swap (full chain)", [("ordFuzz", [7, 2]), ("ordWash", [2, 7])]))
    results.append(case("order: reverse whole chain", [("ordGlide", [7, 1]), ("ordFuzz", [6, 2]), ("ordAmp", [5, 3]),
                                                      ("ordFocus", [3, 5]), ("ordReverse", [2, 6]), ("ordWash", [1, 7])], sets=clean))
    results.append(case("fuzzType cycle", [("fuzzType", [1, 2, 0])]))
    results.append(case("fuzzVoice cycle", [("fuzzVoice", [0, 2, 1])]))
    results.append(case("ampCab cycle", [("ampCab", [0, 2, 3, 1])], sets=clean))
    results.append(case("modMode cycle", [("modMode", [1, 2, 3, 4, 0])], sets=clean))
    results.append(case("revMode cycle", [("revMode", [1, 2, 0])], sets=dict(clean, revOn=1)))
    results.append(case("shimmer mode cycle", [("washShimMode", [1, 2, 3, 0])], sets=dict(clean, washShimmer=0.5)))
    results.append(case("freeze toggle", [("washFreeze", [1, 0])], sets=clean))
    results.append(case("macro jumps 0<->1", [("wall", [0, 1]), ("wash", [1, 0]), ("glide", [0, 1]), ("blur", [1, 0]),
                                             ("warmth", [0, 1])], sets=clean))
    results.append(case("big knob jumps", [("washSize", [0, 1]), ("washDecay", [0.3, 60]), ("focTime", [60, 900]),
                                          ("revTime", [60, 1600]), ("washPre", [0, 250]), ("glideRate", [0.05, 8]),
                                          ("modRate", [0.02, 10]), ("outTilt", [-1, 1]), ("outWidth", [0, 2]),
                                          ("ampBass", [0, 1]), ("ampTreble", [1, 0]), ("tapeAge", [0, 1]),
                                          ("inGain", [-12, 6]), ("outGain", [-12, 0])], sets=dict(clean, revOn=1)))
    results.append(case("bypass toggle @ 96 kHz", [("bypass", [1, 0])], sets=clean, sr=96000, dur=5.0))
    print(f"\n{sum(results)}/{len(results)} click cases passed")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
