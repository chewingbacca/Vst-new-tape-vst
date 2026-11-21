# Swoon verification report

531 checks, 0 failures, 507 s

## Unit tests

```
ok    params_table_is_consistent
ok    fast_math_is_accurate
ok    order_resolution_is_a_stable_permutation
ok    presets_are_valid
ok    oversampler_is_transparent_in_band_with_exact_latency
ok    diode_stage_is_bounded_monotonic_and_settles_to_static_curve
ok    fuzz_latency_matches_and_dry_is_aligned
ok    grain_shifter_shifts_by_the_requested_ratio_and_is_passive
ok    wash_decays_never_grows_and_freeze_holds
ok    wash_freeze_is_lossless_after_a_size_change_and_does_not_survive_switch_off
ok    reverse_mode_switch_does_not_replay_old_audio
ok    glide_follows_requested_pitch_and_survives_splices
ok    engine_bypass_is_bit_transparent_delay
ok    engine_latency_is_constant_and_wet_path_is_aligned
ok    engine_output_is_identical_for_any_block_size
ok    engine_reorder_never_gets_stuck
ok    engine_bypass_engaged_at_run_time_is_bit_exact
ok    engine_swallows_non_finite_input
ok    engine_survives_parameter_abuse
ok    engine_process_never_allocates
ok    engine_limiter_holds_the_ceiling
ok    engine_silence_in_gives_silence_out_and_reset_clears_tails
ok    every_preset_renders_sanely

9813153 checks, 0 failures
```

## Modules, solo

```
module   signal      peak     rms        dc     end  notes
fuzz     impulse    -20.3   -60.6  0.000000  -240.0  ok
fuzz     sweep      -12.6   -22.4  0.000000  -240.0  ok
fuzz     strum      -12.8   -21.6  0.000000  -240.0  ok
fuzz     bursts     -12.7   -28.6  0.000000  -240.0  ok
fuzz     silence   -240.0  -240.0  0.000000  -240.0  ok
glide    impulse    -14.0   -70.2  0.000000  -240.0  ok
glide    sweep      -12.0   -17.6  0.000000  -240.0  ok
glide    strum       -6.1   -29.6  0.000000  -240.0  ok
glide    bursts     -11.9   -26.5  0.000000  -240.0  ok
glide    silence   -240.0  -240.0  0.000000  -240.0  ok
amp      impulse    -20.8   -71.0  0.000000  -240.0  ok
amp      sweep       -6.9   -18.0  0.000000  -240.0  ok
amp      strum       -6.8   -27.6  0.000000  -240.0  ok
amp      bursts     -12.6   -27.1  0.000000  -240.0  ok
amp      silence   -240.0  -240.0  0.000000  -240.0  ok
mod      impulse    -15.0   -70.3  0.000000  -240.0  ok
mod      sweep       -6.4   -17.6  0.000000  -240.0  ok
mod      strum       -5.3   -29.7  0.000000  -240.0  ok
mod      bursts      -7.4   -26.4  0.000000  -240.0  ok
mod      silence   -240.0  -240.0  0.000000  -240.0  ok
focus    impulse    -13.4   -69.9  0.000000  -240.0  ok
focus    sweep       -7.5   -18.5  0.000000  -207.6  ok
focus    strum       -7.0   -30.4  0.000000  -240.0  ok
focus    bursts     -12.0   -27.5  0.000000  -189.7  ok
focus    silence   -240.0  -240.0  0.000000  -240.0  ok
reverse  impulse    -16.6   -71.0  0.000000  -240.0  ok
reverse  sweep       -1.5   -16.8  0.000000  -240.0  ok
reverse  strum       -5.0   -28.0  0.000000  -240.0  ok
reverse  bursts      -7.2   -25.4  0.000000  -240.0  ok
reverse  silence   -240.0  -240.0  0.000000  -240.0  ok
wash     impulse    -14.4   -70.9  0.000000  -182.8  ok
wash     sweep       -5.6   -19.2  0.000000  -123.2  ok
wash     strum       -8.8   -30.9  0.000000  -173.4  ok
wash     bursts     -12.8   -27.7  0.000000  -125.7  ok
wash     silence   -240.0  -240.0  0.000000  -240.0  ok
tape     impulse    -20.6   -73.9  0.000000  -240.0  ok
tape     sweep      -10.1   -17.7  0.000000  -240.0  ok
tape     strum       -8.3   -29.2  0.000000  -240.0  ok
tape     bursts     -11.2   -26.1  0.000000  -240.0  ok
tape     silence   -240.0  -240.0  0.000000  -240.0  ok
```

## Factory presets (strummed phrase)

```
 # preset                 cat          rms   peak   lim |   125   250   500  1000  2000  4000  8000 |  corr  tail   cpu
 0 Swoon                  Wall       -17.4   -5.9   0.0 |  -4.6   2.2   6.8   8.1   6.1   0.3 -18.9 | -0.01   3.9  12.6
 1 Soft Focus Morning     Dream Pop  -20.3   -4.1   0.0 |  -5.6   0.9   4.6   7.9   8.1   2.0 -17.9 |  0.31   3.1   7.1
 2 Glass Lanterns         Dream Pop  -20.6   -3.5   0.0 |  -9.2   0.0   4.0   6.3   7.9   4.6 -13.7 |  0.19   2.5   6.1
 3 Souvenir Tape          Dream Pop  -20.5   -8.1   0.0 |   0.5   3.8   4.8   6.1   5.7   0.3 -21.2 |  0.28   1.9   5.8
 4 Pale Blue Hours        Dream Pop  -20.3   -3.5   0.0 |  -2.3  -0.2   4.2   7.2   7.4   1.7 -17.9 |  0.18   4.8  13.4
 5 Lullaby Engine         Dream Pop  -20.4   -5.2   0.0 |  -4.3   0.6   5.7   8.6   8.1   1.2 -20.0 |  0.17   6.2   9.1
 6 Sea Glass              Dream Pop  -20.4   -3.1   0.0 |  -7.6  -1.6   2.4   6.5   6.8   5.9 -12.5 |  0.34   3.8   7.2
 7 Honeyed Static         Dream Pop  -20.4   -9.4   0.0 |   0.6   2.5   3.9   6.9   5.7  -0.3 -19.2 |  0.35   2.0  12.7
 8 Held Arm               Glide      -17.4   -7.4   0.0 |  -2.2   3.7   7.6   9.5   7.4  -0.4 -25.5 | -0.04   1.6  11.9
 9 Tilted Room            Glide      -20.3   -7.3   0.0 |  -5.9   0.8   3.8   7.4   8.0   2.8 -16.8 |  0.00   3.1   7.8
10 Strum Dip              Glide      -17.4   -6.6   0.0 |  -2.3  -2.2   2.8   8.1   8.6   4.3 -19.4 | -0.01   2.1  14.3
11 Warped Acetate         Glide      -20.7   -7.6   0.0 |   0.1   1.9   3.9   7.0   6.9   0.9 -20.7 |  0.06   1.8   6.6
12 Feather Bender         Glide      -20.5   -4.1   0.0 |  -6.3   1.0   3.7   5.7   7.2   3.6 -14.8 |  0.39   3.0   6.8
13 Drifting Apart         Glide      -17.5   -7.0   0.0 |  -3.0   1.5   6.3   8.2   6.8   1.9 -21.7 |  0.01   3.6  14.1
14 Wall of Rose           Wall       -17.6   -7.3   0.0 |  -4.7   0.6   5.7   8.1   7.2   2.9 -19.8 | -0.00   4.5  12.6
15 Smeared Wall           Wall       -17.4   -8.6   0.0 |  -3.3   1.1   6.2   8.7   7.4   1.8 -21.9 |  0.00   3.9  15.1
16 Scooped Avalanche      Wall       -17.3   -6.9   0.0 |  -2.7   0.6   0.6   2.5   8.0   6.7 -15.7 | -0.03   2.8  14.9
17 Cathedral of Amps      Wall       -17.5   -6.0   0.0 |  -3.2  -0.1   3.4   7.5   7.9   3.6 -19.1 | -0.02   8.0  14.0
18 Octave Halo            Wall       -17.5   -7.0   0.0 |  -7.0  -3.5  -2.0   5.0   8.9   6.9  -8.3 | -0.03   4.8  13.6
19 Velcro Bloom           Wall       -17.5   -6.0   0.0 |  -4.3   2.2   7.3   8.5   6.7   0.0 -20.3 | -0.01   4.2  13.5
20 Blown Speaker Hymn     Wall       -17.6   -9.5   0.0 |   1.2   2.4   5.7   6.5   4.8  -0.3 -20.2 | -0.05   2.6  11.8
21 Static Flowers         Wall       -17.6   -8.0   0.0 |  -2.1  -2.3   1.2   6.0   7.2   3.7 -13.8 |  0.00   5.2  13.9
22 Blanket Fuzz           Wall       -17.6   -8.2   0.0 |  -1.8   6.0   9.4   8.1   4.6  -2.6 -23.6 |  0.02   2.9  12.3
23 Diving Bell            Ambient    -22.1   -6.5   0.0 |  -4.8   2.6   6.0   8.5   7.4   0.3 -20.1 |  0.20  15.3   6.9
24 Cascade                Ambient    -22.0   -6.4   0.0 |  -6.2   2.7   4.1   6.5   6.7   1.5 -15.4 |  0.27   9.2   6.8
25 Fifths in Fog          Ambient    -22.0   -5.5   0.0 |  -3.1   2.9   5.2   7.7   6.6   0.3 -19.5 |  0.25  10.2   6.9
26 Undertow               Ambient    -21.9   -7.1   0.0 |   0.1   4.9   7.5   6.8   4.8  -1.1 -22.9 |  0.16  10.4  14.1
27 Frozen Lake            Ambient    -22.1   -7.8   0.0 |  -5.7   3.0   5.4   8.9   8.1   0.8 -20.5 |  0.21  23.3   6.8
28 Aurora                 Ambient    -22.1   -6.0   0.0 |  -6.0   1.7   5.0   6.8   6.5   2.0 -15.9 |  0.00  10.7   8.7
29 Backwards Postcard     Texture    -20.6   -4.5   0.0 |  -4.0   2.9   5.0   8.4   8.4   1.8 -22.4 |  0.60   3.4   8.8
30 Rack Unit Bloom        Texture    -20.7   -5.5   0.0 |  -1.1   2.5   6.0   8.6   9.0   2.1 -27.1 |  0.02   1.3   5.0
31 Seasick Lullaby        Texture    -20.4   -5.9   0.0 |  -4.1   1.8   4.1   6.9   7.5   1.9 -18.2 |  0.00   2.3   6.0
32 Jet Stream             Texture    -17.3   -6.8   0.0 |   1.2   2.0   0.5   1.6   6.7   4.8 -16.8 | -0.02   2.4  13.6
33 Moth Wings             Texture    -20.6   -7.6   0.0 |  -5.3  -1.6   2.9   4.7   6.4   3.8 -10.9 |  0.01   1.9  14.5
34 Pillow Fort            Texture    -20.7   -6.6   0.0 |   4.1   8.1   7.6   8.0   4.5  -3.3 -28.9 |  0.45   3.0   8.0
35 Clean Slate            Utility    -23.7   -8.1   0.0 |  -5.0   1.3   3.5   6.7   7.8   2.9 -17.2 |  1.00   0.0   1.4
36 DI Amp Only            Utility    -23.0   -6.8   0.0 |  -6.0   0.8   3.2   6.5   7.7   3.3 -15.4 |  1.00   0.0   1.4
37 Just the Fuzz          Utility    -17.7   -9.3   0.0 |  -6.0   1.4   6.1   7.1   5.9   1.2 -15.6 |  1.00   0.1   8.0
38 Wash Send              Utility    -28.8  -12.5   0.0 |  -9.1   3.0  10.7   9.0   5.2  -2.8 -16.0 |  0.05   4.1   2.5
highest preset CPU: 15.1 % of one core at 48 kHz, 256-sample blocks (this machine)
```

## Fuzz aliasing (full sustain, sine in; inharmonic energy relative to harmonics)

```
sustain   1000 Hz: inharmonic total  -118.1 dB, worst line  -122.3 dB at 200 Hz
sustain   2500 Hz: inharmonic total   -92.2 dB, worst line   -96.5 dB at 1500 Hz
sustain   5000 Hz: inharmonic total   -73.2 dB, worst line   -74.7 dB at 1000 Hz
octave    1000 Hz: inharmonic total  -114.7 dB, worst line  -117.2 dB at 667 Hz
octave    2500 Hz: inharmonic total   -76.2 dB, worst line   -81.6 dB at 4000 Hz
octave    5000 Hz: inharmonic total   -59.1 dB, worst line   -63.7 dB at 9000 Hz
velcro    1000 Hz: inharmonic total  -114.0 dB, worst line  -118.2 dB at 200 Hz
velcro    2500 Hz: inharmonic total   -57.1 dB, worst line   -62.7 dB at 1500 Hz
velcro    5000 Hz: inharmonic total   -49.8 dB, worst line   -54.6 dB at 4000 Hz
```

## Sample rates and block sizes (everything on)

```
 44100 Hz  latency  238  b1:  -21.6 dB,  14.2 %  b32:  -19.9 dB,  15.5 %  b333:  -19.9 dB,  14.7 %  b4096:  -19.9 dB,  14.0 %
 48000 Hz  latency  251  b1:  -21.6 dB,  15.3 %  b32:  -19.9 dB,  16.2 %  b333:  -19.9 dB,  15.3 %  b4096:  -19.9 dB,  13.8 %
 88200 Hz  latency  390  b1:  -21.0 dB,  21.0 %  b32:  -19.4 dB,  24.0 %  b333:  -19.4 dB,  21.9 %  b4096:  -19.4 dB,  21.1 %
 96000 Hz  latency  417  b1:  -21.4 dB,  24.6 %  b32:  -19.4 dB,  23.0 %  b333:  -19.4 dB,  23.1 %  b4096:  -19.4 dB,  23.8 %
176400 Hz  latency  694  b1:  -20.8 dB,  45.4 %  b32:  -19.2 dB,  40.5 %  b333:  -19.2 dB,  38.0 %  b4096:  -19.2 dB,  41.7 %
192000 Hz  latency  748  b1:  -20.8 dB,  43.5 %  b32:  -19.4 dB,  44.9 %  b333:  -19.4 dB,  54.2 %  b4096:  -19.4 dB,  52.7 %
level spread across sample rates: 0.70 dB
```

## Click tests

```
ok   bypass toggle (full chain)             event HF   -45.7 dB  steady   -47.0 dB  delta    1.3  (worst at 4.0s)
ok   bypass toggle (no fuzz)                event HF   -80.2 dB  steady  -110.0 dB  delta   29.8  (worst at 1.5s)
ok   mix 0<->1 (no fuzz)                    event HF   -86.2 dB  steady  -110.0 dB  delta   23.8  (worst at 1.5s)
ok   fuzzOn toggle                          event HF   -46.7 dB  steady   -47.1 dB  delta    0.5  (worst at 4.0s)
ok   glideOn toggle                         event HF   -90.7 dB  steady  -108.7 dB  delta   18.0  (worst at 1.5s)
ok   ampOn toggle                           event HF   -80.7 dB  steady  -104.9 dB  delta   24.2  (worst at 1.5s)
ok   modOn toggle                           event HF   -62.0 dB  steady   -92.6 dB  delta   30.6  (worst at 3.5s)
ok   focOn toggle                           event HF   -61.2 dB  steady   -81.4 dB  delta   20.2  (worst at 5.0s)
ok   revOn toggle                           event HF   -64.9 dB  steady   -59.8 dB  delta   -5.1  (worst at 2.5s)
ok   washOn toggle                          event HF   -83.9 dB  steady   -69.6 dB  delta  -14.3  (worst at 4.0s)
ok   tapeOn toggle                          event HF   -72.1 dB  steady  -107.7 dB  delta   35.6  (worst at 1.5s)
ok   order: fuzz<->wash swap (no fuzz)      event HF  -104.3 dB  steady  -104.9 dB  delta    0.5  (worst at 4.5s)
ok   order: fuzz<->wash swap (full chain)   event HF   -38.3 dB  steady   -39.0 dB  delta    0.7  (worst at 5.0s)
ok   order: reverse whole chain             event HF   -75.3 dB  steady   -82.6 dB  delta    7.4  (worst at 4.5s)
ok   fuzzType cycle                         event HF   -40.8 dB  steady   -41.8 dB  delta    1.0  (worst at 4.5s)
ok   fuzzVoice cycle                        event HF   -40.8 dB  steady   -40.3 dB  delta   -0.4  (worst at 5.0s)
ok   ampCab cycle                           event HF   -75.8 dB  steady  -100.1 dB  delta   24.3  (worst at 6.0s)
ok   modMode cycle                          event HF   -59.5 dB  steady   -84.6 dB  delta   25.1  (worst at 3.0s)
ok   revMode cycle                          event HF   -65.7 dB  steady   -56.5 dB  delta   -9.1  (worst at 2.0s)
ok   shimmer mode cycle                     event HF   -83.9 dB  steady   -87.0 dB  delta    3.1  (worst at 5.0s)
ok   freeze toggle                          event HF  -108.9 dB  steady  -112.7 dB  delta    3.8  (worst at 1.5s)
ok   macro jumps 0<->1                      event HF   -58.2 dB  steady   -65.0 dB  delta    6.8  (worst at 5.5s)
ok   big knob jumps                         event HF   -43.0 dB  steady   -41.9 dB  delta   -1.1  (worst at 5.0s)
ok   bypass toggle @ 96 kHz                 event HF   -76.2 dB  steady  -109.9 dB  delta   33.7  (worst at 3.5s)

24/24 click cases passed
```

## Preset cycling (new preset every 0.8 s on a held tone and on the strummed phrase)

```
sine:220 : peak  -1.84 dBFS, limiter max 2.7 dB, largest sample step 0.105
strum    : peak  -2.41 dBFS, limiter max 0.0 dB, largest sample step 0.355
```

## pluginval (strictness 10)

```
SUCCESS
```

