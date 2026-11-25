// Factory voice presets. One per line:
//   Name | Category | engine | role | key=value ...
// Keys are the parameter keys from ParamTables.cpp in physical units (labels for switches).
// Extra keys: fxa=/fxb= (insert effect, then a.<key>= / b.<key>= for its params), poly=1,
// scale=1, base=<midi note>, sample=<factory sample name>, mod=<source>:<dest key>:<amount>.
// Values with spaces are not allowed (use the label index for such switches).
R"PRESETS(
# ---------------------------------------------------------------- KICK
Sub Pressure      | Kick | drum | kick | model=Kick tune=-5 decay=0.55 sweep=0.55 sweept=0.035 snap=0.25 noise=0.05 ampd=1.2
Dry Thud          | Kick | drum | kick | model=Kick tune=0 decay=0.18 sweep=0.4 sweept=0.02 snap=0.45 noise=0.15 ndecay=0.03 tone=0.3
Long Boom 808     | Kick | drum | kick | model=Kick tune=-7 decay=2.2 sweep=0.35 sweept=0.06 snap=0.1 noise=0 ampd=4
Click Kick        | Kick | drum | kick | model=Kick tune=7 decay=0.09 sweep=0.75 sweept=0.008 snap=0.8 noise=0.2 ndecay=0.01 ncolor=0.8
Folded Kick       | Kick | drum | kick | model=Kick tune=-2 decay=0.35 sweep=0.6 sweept=0.03 shape=0.65 dirt=0.3 snap=0.3
Zap Kick          | Kick | drum | kick | model=Kick tune=3 decay=0.25 sweep=1 sweept=0.09 snap=0.2 fm=0.15 fmratio=1.5
Tape Kick         | Kick | drum | kick | model=Kick tune=-3 decay=0.4 sweep=0.45 sweept=0.03 snap=0.35 dirt=0.45 drive=0.25 drvtype=Tube cutoff=6000 flttype=LP12
Rubber Kick       | Kick | drum | kick | model=Kick tune=-9 decay=0.7 sweep=0.25 sweept=0.12 snap=0.15 hold=0.04
Pillow Kick       | Kick | drum | kick | model=Kick tune=-4 decay=0.3 sweep=0.3 sweept=0.025 snap=0 noise=0.1 ndecay=0.05 ncolor=0.2 flttype=LP24 cutoff=900
Grit Kick         | Kick | drum | kick | model=Kick tune=0 decay=0.28 sweep=0.55 sweept=0.025 snap=0.5 dirt=0.8 noise=0.3 ncolor=0.4
FM Kick           | Kick | drum | kick | model=Kick tune=-1 decay=0.32 sweep=0.5 sweept=0.03 fm=0.45 fmratio=0.5 snap=0.3
Hollow Kick       | Kick | drum | kick | model=Kick tune=5 decay=0.22 sweep=0.35 sweept=0.015 shape=0.3 flttype=HP12 cutoff=70 res=0.4
Infra Kick        | Kick | drum | kick | model=Kick tune=-12 decay=1.4 sweep=0.6 sweept=0.05 snap=0.4 ampd=3
Gabber Splat      | Kick | drum | kick | model=Kick tune=2 decay=0.45 sweep=0.7 sweept=0.04 dirt=1 drive=0.5 drvtype=Hard snap=0.6
# ---------------------------------------------------------------- SNARE
Tight Snare       | Snare | drum | snare | model=Snare tune=2 decay=0.12 tone=0.5 noise=0.6 ndecay=0.16 ncolor=0.65 snap=0.4
Fat Snare         | Snare | drum | snare | model=Snare tune=-3 decay=0.22 tone=0.35 noise=0.5 ndecay=0.25 ncolor=0.5 snap=0.3 dirt=0.25
Rim Crack         | Snare | drum | snare | model=Snare tune=12 decay=0.04 tone=0.8 noise=0.2 ndecay=0.03 ncolor=0.9 snap=0.9
Ghost Brush       | Snare | drum | snare | model=Snare tune=0 decay=0.03 tone=0.2 noise=0.8 ndecay=0.3 ncolor=0.45 snap=0 ampvel=1
Tin Snare         | Snare | drum | snare | model=Snare tune=9 decay=0.08 tone=0.9 noise=0.7 ndecay=0.1 ncolor=0.85 snap=0.6 flttype=HP12 cutoff=400
Gated Snare       | Snare | drum | snare | model=Snare tune=-1 decay=0.18 tone=0.5 noise=0.8 ndecay=0.6 ncolor=0.55 amps=1 ampd=0.2 ampr=0.01 fxa=plate a.decay=0.8 a.mix=0.35
Crushed Snare     | Snare | drum | snare | model=Snare tune=3 decay=0.1 tone=0.6 noise=0.6 ndecay=0.15 snap=0.5 fxa=crush a.bits=6 a.rate=11000 a.mix=0.8
Lo-Fi Snare       | Snare | drum | snare | model=Snare tune=-5 decay=0.15 tone=0.3 noise=0.55 ndecay=0.18 ncolor=0.35 flttype=LP12 cutoff=3500 drive=0.3 drvtype=Tube
Snap Snare        | Snare | drum | snare | model=Snare tune=5 decay=0.06 tone=0.7 noise=0.45 ndecay=0.09 ncolor=0.75 snap=1
Bent Snare        | Snare | drum | snare | model=Snare tune=0 decay=0.2 sweep=0.8 sweept=0.08 tone=0.4 noise=0.5 ndecay=0.2
Metal Snare       | Snare | drum | snare | model=Snare tune=4 decay=0.14 tone=0.6 noise=0.4 ndecay=0.14 fm=0.4 fmratio=3.7 snap=0.4
Wide Snare        | Snare | drum | snare | model=Snare tune=1 decay=0.13 tone=0.55 noise=0.65 ndecay=0.2 spread=0.9 snap=0.35
# ---------------------------------------------------------------- HAT
Closed Hat        | Hat | drum | hat | model=Hat decay=0.045 tone=0.55 noise=0.2 ndecay=0.03 snap=0.2
Open Hat          | Hat | drum | hat | model=Hat decay=0.45 tone=0.5 noise=0.3 ndecay=0.3
Tick Hat          | Hat | drum | hat | model=Hat decay=0.012 tone=0.8 noise=0.1 ndecay=0.01 snap=0.6
Dusty Hat         | Hat | drum | hat | model=Hat decay=0.06 tone=0.3 noise=0.6 ndecay=0.05 ncolor=0.5 flttype=LP12 cutoff=7000
Pedal Hat         | Hat | drum | hat | model=Hat decay=0.09 tone=0.45 noise=0.15 tune=-5
Sizzle Hat        | Hat | drum | hat | model=Hat decay=0.2 tone=0.7 noise=0.5 ndecay=0.25 spread=0.8
Glass Hat         | Hat | drum | hat | model=Hat decay=0.07 tone=0.9 spread=0.15 fm=0.3 fmratio=2.3 tune=7
Shaker            | Hat | drum | hat | model=Hat decay=0.03 tone=0.6 noise=0.9 ndecay=0.07 ncolor=0.75 hold=0.02 snap=0
Crushed Hat       | Hat | drum | hat | model=Hat decay=0.05 tone=0.6 fxa=crush a.bits=4 a.rate=9000 a.mix=0.7
Trash Hat         | Hat | drum | hat | model=Hat decay=0.12 tone=0.4 shape=0.8 dirt=0.6 noise=0.4
Cymbal Wash       | Hat | drum | hat | model=Metal decay=2.5 tone=0.45 noise=0.4 ndecay=2 spread=0.5
Ride Ping         | Hat | drum | hat | model=Metal decay=1.2 tone=0.7 tune=5 spread=0.25 snap=0.3 noise=0.1
# ---------------------------------------------------------------- CLAP / PERC
Classic Clap      | Perc | drum | snare | model=Clap tone=0.45 decay=0.25 spread=0.35 noise=0.2 ndecay=0.1
Tight Clap        | Perc | drum | snare | model=Clap tone=0.6 decay=0.1 spread=0.2 snap=0.4
Room Clap         | Perc | drum | snare | model=Clap tone=0.4 decay=0.6 spread=0.5 noise=0.3 ndecay=0.4 fxa=plate a.decay=1.2 a.mix=0.3
Scatter Clap      | Perc | drum | snare | model=Clap tone=0.5 decay=0.2 spread=1 snap=0.2
Finger Snap       | Perc | drum | perc | model=Clap tone=0.85 decay=0.05 spread=0.05 snap=0.9 tune=12
Bongo Hi          | Perc | drum | perc | model=Tom tune=19 decay=0.12 sweep=0.25 sweept=0.01 snap=0.3 tone=0.6
Bongo Lo          | Perc | drum | perc | model=Tom tune=12 decay=0.16 sweep=0.25 sweept=0.012 snap=0.25 tone=0.5
Woodblock         | Perc | drum | perc | model=Tom tune=24 decay=0.04 sweep=0.1 shape=0.5 snap=0.6 flttype=BP cutoff=1800 res=0.4
Cowbell Bent      | Perc | drum | perc | model=Metal tune=-3 decay=0.25 tone=0.3 spread=0.05 fm=0.2 fmratio=1.48
Rim Click         | Perc | drum | perc | model=Tom tune=24 decay=0.015 snap=1 noise=0.2 ndecay=0.01 ncolor=0.9
Laser Perc        | Perc | drum | perc | model=Tom tune=24 decay=0.18 sweep=1 sweept=0.15 fm=0.3 fmratio=2
Blip              | Perc | drum | perc | model=Kick tune=24 decay=0.03 sweep=0.3 sweept=0.004 snap=0.2
Conga Hit         | Perc | drum | perc | model=Tom tune=7 decay=0.22 sweep=0.2 sweept=0.015 snap=0.2 noise=0.1 tone=0.4
Knock             | Perc | drum | perc | model=Snare tune=8 decay=0.05 tone=0.2 noise=0.05 snap=0.7 shape=0.4
# ---------------------------------------------------------------- TOM / METAL
Floor Tom         | Tom | drum | perc | model=Tom tune=-5 decay=0.5 sweep=0.4 sweept=0.04 snap=0.2
Synth Tom         | Tom | drum | perc | model=Tom tune=0 decay=0.35 sweep=0.7 sweept=0.08 snap=0.15 noise=0.1
Electro Tom       | Tom | drum | perc | model=Tom tune=5 decay=0.25 sweep=0.9 sweept=0.05 fm=0.1 fmratio=1.5
Pipe Hit          | Tom | drum | perc | model=Metal tune=-10 decay=0.4 tone=0.2 spread=0.1 snap=0.4
Gamelan Gong      | Tom | drum | perc | model=Metal tune=-12 decay=3 tone=0.25 spread=0.35 fm=0.15 fmratio=0.75
Bell Tree         | Tom | drum | perc | model=Metal tune=14 decay=1.5 tone=0.85 spread=0.7
Scrap Metal       | Tom | drum | perc | model=Metal tune=0 decay=0.3 tone=0.5 shape=1 dirt=0.5 noise=0.5 ndecay=0.2
Anvil             | Tom | drum | perc | model=Metal tune=-5 decay=0.8 tone=0.4 spread=0.2 snap=0.8 fm=0.3 fmratio=2.7
Spring Hit        | Tom | drum | perc | model=Tom tune=10 decay=0.6 sweep=0.5 sweept=0.3 fm=0.5 fmratio=7
Sheet Metal       | Tom | drum | perc | model=Metal tune=3 decay=1 tone=0.6 spread=0.9 noise=0.3 ndecay=0.8 dirt=0.2
# ---------------------------------------------------------------- FM PERCUSSION / BELLS
Glass Bell        | FM Bell | fm | lead | algo=3 ratio1=1 ratio2=3.5 ratio3=1 ratio4=7 level2=0.55 level4=0.3 decay1=2.5 decay2=1.2 decay4=0.6 ampd=4 amps=0
Tubular           | FM Bell | fm | lead | algo=1 ratio1=1 ratio2=3.5 ratio3=2 ratio4=5 level2=0.5 level3=0.6 level4=0.4 decay1=4 decay2=2 decay3=3 decay4=1 ampd=6 amps=0
Music Box         | FM Bell | fm | lead | algo=3 ratio1=2 ratio2=8 ratio3=1 ratio4=4 level2=0.4 level4=0.25 decay1=1.5 decay2=0.3 decay4=0.2 ampd=2 amps=0
Toy Piano         | FM Bell | fm | lead | algo=6 ratio1=1 ratio2=4 ratio3=1 ratio4=11 level1=1 level2=0.4 level3=0.5 level4=0.35 decay1=1.2 decay2=0.8 decay3=0.5 decay4=0.15 ampd=1.5 amps=0
Struck Glass      | FM Perc | fm | perc | algo=0 ratio1=1 ratio2=2.7 ratio3=5.4 ratio4=9.1 level2=0.6 level3=0.5 level4=0.4 decay1=0.6 decay2=0.3 decay3=0.2 decay4=0.1 ampd=0.8 amps=0
Metal Tick        | FM Perc | fm | perc | algo=0 ratio1=1 ratio2=7.3 ratio3=3 ratio4=11 level2=0.8 level3=0.7 level4=0.6 decay1=0.06 decay2=0.04 decay3=0.03 decay4=0.02 ampd=0.1 amps=0 fb=0.3
FM Clave          | FM Perc | fm | perc | algo=2 ratio1=2 ratio2=5 ratio3=3 ratio4=1 level2=0.5 level3=0.3 level4=0.2 decay1=0.08 decay2=0.03 decay3=0.03 decay4=0.03 ampd=0.12 amps=0
Noise Snap FM     | FM Perc | fm | perc | algo=0 fb=0.9 ratio1=1 ratio2=1 ratio3=1 ratio4=1 level2=0.9 level3=0.8 level4=0.9 decay1=0.08 decay2=0.05 decay3=0.04 decay4=0.03 ampd=0.1 amps=0
DX Marimba        | FM Bell | fm | lead | algo=1 ratio1=1 ratio2=3 ratio3=1 ratio4=10 level2=0.45 level4=0.35 decay1=0.6 decay2=0.12 decay3=0.4 decay4=0.05 ampd=0.8 amps=0
Bonk              | FM Perc | fm | perc | algo=0 ratio1=1 ratio2=1.5 ratio3=1 ratio4=1 level2=0.7 level3=0.3 decay1=0.15 decay2=0.05 decay3=0.05 penv=24 penvt=0.03 ampd=0.2 amps=0
Thai Bell         | FM Bell | fm | lead | algo=3 ratio1=1 ratio2=1.41 ratio3=2.83 ratio4=5.66 level2=0.5 level3=0.3 level4=0.2 decay1=3 decay2=2 decay3=1 decay4=0.5 ampd=5 amps=0
Bit Chirp         | FM Perc | fm | perc | algo=0 ratio1=1 ratio2=16 ratio3=0.5 ratio4=13 level2=0.9 level3=0.9 level4=0.9 decay1=0.05 decay2=0.05 decay3=0.05 decay4=0.05 penv=48 penvt=0.02 ampd=0.06 amps=0
Hollow Bell       | FM Bell | fm | lead | algo=5 ratio1=0.5 ratio2=1 ratio3=3 ratio4=5 level1=0.6 level2=0.6 level3=0.5 level4=0.3 decay1=3 decay2=2 decay3=1 decay4=0.6 ampd=4 amps=0 fxa=delay a.time=0.55 a.feedback=0.4 a.mix=0.25
Steel Drum        | FM Bell | fm | lead | algo=3 ratio1=1 ratio2=2 ratio3=1.5 ratio4=3 level2=0.4 level3=0.25 level4=0.2 decay1=0.8 decay2=0.3 decay3=0.4 decay4=0.2 ampd=1 amps=0
Clock Tick        | FM Perc | fm | perc | algo=0 ratio1=4 ratio2=11 ratio3=7 ratio4=13 level2=0.6 level3=0.5 level4=0.4 decay1=0.02 decay2=0.01 decay3=0.01 decay4=0.01 ampd=0.03 amps=0
Pong              | FM Perc | fm | perc | algo=7 ratio1=1 ratio2=2 ratio3=3 ratio4=4 level1=0.9 level2=0.3 level3=0.15 level4=0.08 decay1=0.12 decay2=0.08 decay3=0.06 decay4=0.04 ampd=0.15 amps=0
# ---------------------------------------------------------------- FM BASS
FM Sub            | FM Bass | fm | bass | algo=0 ratio1=1 ratio2=1 ratio3=1 ratio4=1 level2=0.25 level3=0 level4=0 decay1=10 decay2=0.4 amps=1 ampd=1 ampr=0.08 base=36
Rubber FM Bass    | FM Bass | fm | bass | algo=0 ratio1=1 ratio2=1 ratio3=2 ratio4=1 level2=0.65 level3=0.3 level4=0 decay1=10 decay2=0.25 decay3=0.15 amps=1 ampr=0.06 base=36
Growl FM          | FM Bass | fm | bass | algo=0 fb=0.5 ratio1=1 ratio2=0.5 ratio3=1 ratio4=2 level2=0.7 level3=0.6 level4=0.5 decay1=10 decay2=10 decay3=0.5 decay4=0.3 amps=1 ampr=0.05 base=36 mod=lfo1:level2:0.15
Pluck FM Bass     | FM Bass | fm | bass | algo=1 ratio1=1 ratio2=1 ratio3=1 ratio4=3 level2=0.6 level3=0.8 level4=0.4 decay1=0.6 decay2=0.12 decay3=0.4 decay4=0.08 ampd=0.6 amps=0 base=36
Metal Bass        | FM Bass | fm | bass | algo=0 ratio1=1 ratio2=1.41 ratio3=1 ratio4=2.7 level2=0.6 level3=0.4 level4=0.3 decay1=10 decay2=0.5 decay3=0.4 decay4=0.2 amps=1 ampr=0.05 base=36
Sine Drone Bass   | FM Bass | fm | bass | algo=7 ratio1=1 ratio2=2 ratio3=3 ratio4=0.5 level1=1 level2=0.2 level3=0.08 level4=0.4 decay1=10 decay2=10 decay3=10 decay4=10 amps=1 ampa=0.02 ampr=0.4 base=36
Wobble FM         | FM Bass | fm | bass | algo=0 ratio1=1 ratio2=1 ratio3=1 ratio4=1 level2=0.5 decay1=10 decay2=10 amps=1 ampr=0.05 base=36 lfo1rate=0.6 mod=lfo1:level2:0.35
Dub FM Bass       | FM Bass | fm | bass | algo=0 ratio1=1 ratio2=2 ratio3=1 ratio4=1 level2=0.3 decay1=10 decay2=0.8 amps=1 ampr=0.2 flttype=LP24 cutoff=600 base=36
# ---------------------------------------------------------------- SYNTH BASS / ACID
Acid Squelch      | Acid | synth | bass | wave1=0.45 flttype=Ladder cutoff=380 res=0.78 fltenv=0.55 modd=0.22 mods=0 accent=0.8 drive=0.25 drvtype=Soft ampd=0.4 amps=0.6 glide=0.06 base=36
Acid Rubber       | Acid | synth | bass | wave1=0.75 pw=0.5 flttype=Ladder cutoff=300 res=0.7 fltenv=0.6 modd=0.15 mods=0 accent=0.7 ampd=0.3 amps=0.5 glide=0.05 base=36
Acid Scream       | Acid | synth | bass | wave1=0.45 flttype=Ladder cutoff=700 res=0.88 fltenv=0.45 modd=0.3 mods=0.1 accent=1 drive=0.55 drvtype=Hard glide=0.04 base=36 fxa=delay a.time=0.4 a.feedback=0.45 a.mix=0.25
Acid Deep         | Acid | synth | bass | wave1=0.5 sub=0.4 flttype=Ladder cutoff=220 res=0.65 fltenv=0.5 modd=0.35 mods=0 accent=0.5 glide=0.08 base=36
Saw Bass          | Synth Bass | synth | bass | wave1=0.45 wave2=0.45 detune=10 mix=0.5 sub=0.3 flttype=LP24 cutoff=900 res=0.2 fltenv=0.3 modd=0.25 base=36
Square Sub        | Synth Bass | synth | bass | wave1=0.75 sub=0.5 flttype=LP24 cutoff=600 res=0.1 base=36 ampd=1 amps=0.8
Reese             | Synth Bass | synth | bass | wave1=0.45 wave2=0.45 detune=25 mix=0.5 unison=3 unidet=20 flttype=LP24 cutoff=1200 res=0.15 base=36 drive=0.2 lfo1rate=0.2 mod=lfo1:cutoff:0.08
Pluck Bass        | Synth Bass | synth | bass | wave1=0.5 flttype=LP24 cutoff=250 res=0.35 fltenv=0.65 modd=0.12 mods=0 ampd=0.35 amps=0 base=36
Wobble Bass       | Synth Bass | synth | bass | wave1=0.45 wave2=0.75 mix=0.4 flttype=LP24 cutoff=500 res=0.5 base=36 lfo1rate=0.62 lfo1shape=Triangle mod=lfo1:cutoff:0.3
Fold Bass         | Synth Bass | synth | bass | wave1=0.1 sub=0.3 drive=0.65 drvtype=Fold flttype=LP12 cutoff=3000 base=36 mod=modenv:drive:0.3 modd=0.3 mods=0
Sync Bass         | Synth Bass | synth | bass | wave1=0.45 wave2=0.45 mix=1 sync=3.5 flttype=LP24 cutoff=2500 res=0.2 base=36 mod=modenv:sync:0.35 modd=0.25 mods=0
Tape Bass         | Synth Bass | synth | bass | wave1=0.2 sub=0.3 drift=0.5 flttype=LP24 cutoff=700 drive=0.3 drvtype=Tube base=36
Organ Bass        | Synth Bass | synth | bass | mode=5 wave1=0.3 flttype=LP12 cutoff=1500 base=36 ampa=0.003
Vowel Bass        | Synth Bass | synth | bass | mode=2 wave1=0.2 flttype=LP24 cutoff=2500 base=36 mod=modenv:wave1:0.5 modd=0.4 mods=0
# ---------------------------------------------------------------- SYNTH LEAD
Square Lead       | Lead | synth | lead | wave1=0.75 pw=0.4 flttype=LP24 cutoff=3500 res=0.2 glide=0.03 lfo1rate=0.55 mod=lfo1:pw:0.1
Sync Lead         | Lead | synth | lead | wave1=0.45 wave2=0.45 mix=0.8 sync=2.5 flttype=LP24 cutoff=5000 mod=lfo1:sync:0.2 lfo1rate=0.4
Chip Lead         | Lead | synth | lead | wave1=0.9 pw=0.25 flttype=Off ampd=0.2 amps=0.5 glide=0.01
Detuned Lead      | Lead | synth | lead | wave1=0.45 unison=5 unidet=25 width=0.8 flttype=LP24 cutoff=4000 res=0.15
Whistle Lead      | Lead | synth | lead | wave1=0 flttype=BP cutoff=1800 res=0.6 fltkey=1 drift=0.3 ampa=0.02
Digital Lead      | Lead | synth | lead | mode=4 wave1=0.4 flttype=LP12 cutoff=6000 mod=lfo1:wave1:0.3 lfo1rate=0.35
Metallic Lead     | Lead | synth | lead | mode=3 wave1=0.5 flttype=LP24 cutoff=4000 res=0.3 fxa=delay a.time=0.45 a.feedback=0.35 a.mix=0.2
FM Glass Lead     | Lead | synth | lead | wave1=0.1 wave2=0 pitch2=12 fm=0.35 flttype=LP24 cutoff=6000 ampd=0.6 amps=0.4
Warble Lead       | Lead | synth | lead | wave1=0.5 drift=0.9 flttype=LP24 cutoff=2500 glide=0.08 ampa=0.01
Sine Arp          | Lead | synth | lead | wave1=0 flttype=Off ampd=0.25 amps=0 ampr=0.1 senda=0.3
# ---------------------------------------------------------------- PAD / WARM
Warm Tape Pad     | Pad | synth | pad | wave1=0.45 wave2=0.45 detune=12 mix=0.5 unison=3 unidet=18 drift=0.6 width=0.8 flttype=LP24 cutoff=1400 res=0.1 ampa=0.6 ampd=2 amps=0.8 ampr=1.5 poly=1 senda=0.35
Dusty Strings     | Pad | synth | pad | wave1=0.45 unison=5 unidet=30 drift=0.7 width=1 flttype=LP12 cutoff=2200 ampa=0.8 ampr=2 amps=1 poly=1 lfo1rate=0.25 mod=lfo1:cutoff:0.06
Glass Pad         | Pad | synth | pad | mode=4 wave1=0.3 wave2=0.6 mix=0.5 detune=8 unison=3 width=0.9 flttype=LP24 cutoff=5000 ampa=1 ampr=2.5 amps=1 poly=1 mod=lfo1:wave1:0.2 lfo1rate=0.2
Choir Pad         | Pad | synth | pad | mode=2 wave1=0.5 wave2=0.3 mix=0.4 unison=5 unidet=15 width=1 drift=0.4 flttype=LP24 cutoff=3000 ampa=0.9 ampr=2 amps=1 poly=1 lfo1rate=0.15 mod=lfo1:wave1:0.25
Wobbly VHS Pad    | Pad | synth | pad | wave1=0.3 wave2=0.45 detune=20 mix=0.5 drift=1 width=0.7 flttype=LP24 cutoff=1100 ampa=0.5 ampr=1.8 amps=0.9 poly=1 drive=0.2 drvtype=Tube senda=0.4
Organ Pad         | Pad | synth | pad | mode=5 wave1=0.6 unison=2 unidet=10 width=0.6 flttype=LP12 cutoff=4000 ampa=0.2 ampr=0.8 amps=1 poly=1
Dark Pad          | Pad | synth | pad | wave1=0.45 wave2=0.75 pitch2=-12 mix=0.4 unison=3 drift=0.5 flttype=LP24 cutoff=600 res=0.25 ampa=1.5 ampr=3 amps=1 poly=1
Bright Shimmer    | Pad | synth | pad | wave1=0.45 unison=7 unidet=35 width=1 flttype=HP12 cutoff=300 ampa=1.2 ampr=3 amps=1 poly=1 fxa=cloud a.decay=12 a.shimmer=0.4 a.mix=0.5
Hollow Fifths     | Pad | synth | pad | wave1=0.1 wave2=0.1 pitch2=7 mix=0.5 detune=5 drift=0.4 flttype=LP24 cutoff=1800 ampa=0.7 ampr=2 amps=1 poly=1
Pulse Pad         | Pad | synth | pad | wave1=0.8 pw=0.3 unison=3 unidet=12 flttype=LP24 cutoff=2000 ampa=0.5 ampr=1.5 amps=1 poly=1 lfo1rate=0.3 mod=lfo1:pw:0.25
# ---------------------------------------------------------------- RESONATOR
Nylon Pluck       | Pluck | resonator | lead | model=String exciter=Impulse decay=1.2 damp=0.55 struct=0.05 position=0.2
Steel Pluck       | Pluck | resonator | lead | model=String exciter=Impulse decay=2.5 damp=0.25 struct=0.15 position=0.12 exccolor=0.8
Harp Pluck        | Pluck | resonator | lead | model=String exciter=Noise excdecay=0.004 decay=3 damp=0.35 position=0.3 poly=1 senda=0.3
Koto              | Pluck | resonator | lead | model=String exciter=Impulse decay=1.8 damp=0.3 struct=0.35 position=0.08 penv=0.5 penvt=0.06
Wood Bar          | Mallet | resonator | perc | model=Bar exciter=Mallet decay=0.6 damp=0.7 struct=0.1 position=0.25 hard=0.4
Marimba           | Mallet | resonator | lead | model=Bar exciter=Mallet decay=0.9 damp=0.55 position=0.3 hard=0.5 poly=1
Vibes             | Mallet | resonator | lead | model=Bar exciter=Mallet decay=3 damp=0.2 position=0.22 hard=0.6 lfo1rate=0.5 mod=lfo1:level:0.08 poly=1
Church Bell       | Mallet | resonator | lead | model=Bell exciter=Mallet decay=6 damp=0.2 struct=0.2 partials=16 hard=0.8 poly=1
Glass Harmonica   | Mallet | resonator | pad | model=Glass exciter=Bow bow=0.6 decay=4 damp=0.15 ampa=0.3 amps=1 ampr=1.5 poly=1
Tabla             | Mallet | resonator | perc | model=Membrane exciter=Mallet decay=0.35 damp=0.4 struct=0.2 position=0.1 penv=4 penvt=0.05
Tube Thump        | Mallet | resonator | perc | model=Tube exciter=Impulse decay=0.3 damp=0.6 position=0.4
Bowed Metal       | Mallet | resonator | pad | model=Bar exciter=Bow bow=0.7 decay=5 damp=0.1 struct=0.6 ampa=0.4 amps=1 ampr=2 poly=1
Struck Pipe       | Mallet | resonator | perc | model=Tube exciter=Mallet decay=1.5 damp=0.3 struct=0.3 hard=0.8
Kalimba           | Mallet | resonator | lead | model=Bar exciter=Impulse decay=1.4 damp=0.4 struct=0.25 position=0.05 poly=1
# ---------------------------------------------------------------- SAMPLER
Break Chop        | Sampler | sampler | perc | sample=Brk_Clatter mode=Slice slices=16 xfade=0.01 ampd=8 amps=1 ampr=0.02
Break Chop Rev    | Sampler | sampler | perc | sample=Brk_Tumble mode=Slice slices=16 reverse=On xfade=0.02 ampr=0.02
Break Transient   | Sampler | sampler | perc | sample=Brk_Ghost mode=Slice sliceby=Transient slices=32 ampr=0.02
Lazy Break        | Sampler | sampler | perc | sample=Brk_Dust mode=Slice slices=8 ampr=0.05 flttype=LP12 cutoff=6000 drive=0.2 drvtype=Tube
Tape Loop         | Sampler | sampler | fx | sample=Tex_TapeHiss loop=Forward xfade=0.2 mode=Gate amps=1 ampa=0.2 ampr=0.6 keytrack=Off
Vox Shot          | Sampler | sampler | lead | sample=Vox_Ah mode=OneShot ampd=1 amps=0 keytrack=On
Reverse Swell     | Sampler | sampler | fx | sample=Tone_WarmPad reverse=On mode=OneShot keytrack=On
Bell Sample       | Sampler | sampler | lead | sample=Tone_FMBell keytrack=On poly=1
Kick Sample       | Sampler | sampler | kick | sample=Hit_SubKick keytrack=Off
Pitched Click     | Sampler | sampler | perc | sample=Hit_Click keytrack=On penv=24 penvt=0.02
# ---------------------------------------------------------------- GRANULAR
Grain Cloud       | Granular | granular | pad | sample=Tone_WarmPad pos=0.3 size=0.12 density=40 spray=0.2 spread=0.8 ampa=0.6 amps=1 ampr=1.5 senda=0.3
Frozen Choir      | Granular | granular | pad | sample=Vox_Ah pos=0.4 size=0.2 density=60 spray=0.05 scan=0 spread=1 ampa=1 amps=1 ampr=2.5
Grain Glitch      | Granular | granular | fx | sample=Brk_Clatter pos=0.5 size=0.02 density=120 spray=0.6 scatter=12 quant=Octaves jitter=0.8 amps=1 ampr=0.3
Scanning Texture  | Granular | granular | pad | sample=Tex_Static pos=0 size=0.08 density=50 scan=0.3 spray=0.3 spread=0.9 amps=1 ampa=0.5 ampr=1
Shimmer Grains    | Granular | granular | pad | sample=Tone_FMBell pos=0.1 size=0.15 density=30 scatter=12 quant=Octaves spread=1 amps=1 ampa=0.8 ampr=3 fxa=cloud a.decay=10 a.mix=0.4
Buzz Swarm        | Granular | granular | fx | sample=Tone_Saw pos=0.2 size=0.01 density=200 spray=0.4 scatter=3 jitter=1 amps=1 ampr=0.4
Rev Grain Pad     | Granular | granular | pad | sample=Tone_Glass pos=0.5 size=0.3 density=20 revprob=0.5 spread=0.8 amps=1 ampa=1.2 ampr=3
Grain Stutter     | Granular | granular | perc | sample=Brk_Ghost pos=0.25 size=0.05 density=24 jitter=0 spray=0 amps=1 ampr=0.05
# ---------------------------------------------------------------- FX / NOISE
White Riser       | FX | drum | fx | model=Hat decay=3 noise=1 ndecay=3 tone=0 flttype=BP cutoff=800 res=0.5 mod=modenv:cutoff:0.5 moda=2 mods=1 amps=1 ampa=2
Laser Zap         | FX | synth | fx | wave1=0.45 flttype=LP24 cutoff=8000 mod=modenv:coarse:0.6 moda=0.0001 modd=0.15 mods=0 ampd=0.2 amps=0
Static Burst      | FX | drum | fx | model=Snare tune=24 decay=0.01 noise=1 ndecay=0.4 ncolor=1 fxa=crush a.bits=3 a.rate=4000 a.mix=0.6
Sub Drop          | FX | drum | fx | model=Kick tune=-12 decay=3 sweep=0.9 sweept=0.5 snap=0 ampd=4
Radio Noise       | FX | sampler | fx | sample=Tex_Static loop=Forward mode=Gate amps=1 keytrack=Off flttype=BP cutoff=2000 res=0.5 lfo1rate=0.3 lfo1shape=Smooth mod=lfo1:cutoff:0.25
Vinyl Bed         | FX | sampler | fx | sample=Tex_Vinyl loop=Forward mode=Gate amps=1 keytrack=Off ampr=0.5
Glitch Bleep      | FX | fm | fx | algo=0 ratio1=1 ratio2=13 ratio3=7 ratio4=3 level2=0.9 level3=0.8 level4=0.6 fb=0.6 decay1=0.04 decay2=0.04 decay3=0.04 decay4=0.04 ampd=0.05 amps=0 penv=-36 penvt=0.05
Feedback Whine    | FX | resonator | fx | model=String exciter=Bow bow=1 decay=8 damp=0.05 struct=0.9 amps=1 ampa=0.5 ampr=1 fxa=freqshift a.shift=0.55 a.feedback=0.6 a.mix=0.5
)PRESETS"
