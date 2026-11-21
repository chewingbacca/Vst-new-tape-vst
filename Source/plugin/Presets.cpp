#include "Presets.h"
#include "Parameters.h"

using namespace juce;
using namespace splinter;

namespace
{
// Variation values, named for readability.
enum : int { d4 = 1, d8 = 2, d8t = 3, d16 = 4, d16t = 5, d32 = 6, d32t = 7, d64 = 8, d128 = 9 };   // stutter divisions
enum : int { tsStop = 1, tsSpin = 2, tsDrag = 3 };
enum : int { fLpClose = 1, fLpOpen, fHpRise, fHpFall, fBpUp, fBpDown };
inline int pitchVar (int degrees) { return degrees < 0 ? degrees + 8 : degrees + 7; }                 // -7..-1 => 1..7, +1..+7 => 8..14

struct Builder
{
    PresetData& d;
    Builder& p (const String& id, float v) { d.params.push_back ({ id, v }); return *this; }
    Builder& macros (float chop, float stutter, float warp, float harmony, float crush, float chaos, float mix = 1.0f)
    {
        return p (pid::chop, chop).p (pid::stutter, stutter).p (pid::warp, warp).p (pid::harmony, harmony).p (pid::crush, crush).p (pid::chaos, chaos).p (pid::mix, mix);
    }
    Builder& slots (int time, int vol, int seq = 1) { return p (pid::timeSlot, (float) time).p (pid::volSlot, (float) vol).p (pid::seqSlot, (float) seq); }
    Builder& voice (int i, bool on, int interval, float pan, float detune = 0.0f, float level = 0.75f)
    {
        return p (pid::voice (i, "On"), on ? 1.0f : 0.0f).p (pid::voice (i, "Interval"), (float) interval).p (pid::voice (i, "Pan"), pan)
              .p (pid::voice (i, "Detune"), detune).p (pid::voice (i, "Level"), level);
    }
    SeqPattern& seq (int slot, const char* name, int steps = 16, int stepsPerBeat = 4)
    {
        SeqPattern& s = d.bank.seq[slot - 1];
        s = SeqPattern(); s.numSteps = steps; s.stepsPerBeat = stepsPerBeat; s.setName (name);
        return s;
    }
};

void on (SeqPattern& s, int lane, std::initializer_list<int> steps, int var = 0, int prob = 100)
{
    for (int k : steps) if (k >= 0 && k < kMaxSteps) { s.steps[lane][k].on = 1; s.steps[lane][k].var = (uint8_t) var; s.steps[lane][k].prob = (uint8_t) prob; }
}
void run (SeqPattern& s, int lane, int start, int len, int var = 0, int prob = 100)
{
    for (int k = start; k < start + len && k < kMaxSteps; ++k) { s.steps[lane][k].on = 1; s.steps[lane][k].var = (uint8_t) var; s.steps[lane][k].prob = (uint8_t) prob; }
}

// Slots 2-4 always hold playable variations so the pattern keys do something
// useful with every preset: a fill, a breakdown and a clean pass.
void standardVariations (Builder& b, int mainSlot = 1)
{
    const SeqPattern main = b.d.bank.seq[mainSlot - 1];
    SeqPattern& fill = b.d.bank.seq[1]; fill = main; fill.setName ("Fill");
    const int last = fill.numSteps - 4 * fill.stepsPerBeat / 4;
    run (fill, laneStutter, jmax (0, last), fill.stepsPerBeat / 2, d16);
    run (fill, laneStutter, jmax (0, last + fill.stepsPerBeat / 2), fill.stepsPerBeat / 2, d32);
    on (fill, laneReverb, { jmax (0, last) }, 2);
    on (fill, laneChop, { 3, 7, 11 }, 0, 70);
    SeqPattern& brk = b.d.bank.seq[2]; brk = SeqPattern(); brk.numSteps = main.numSteps; brk.stepsPerBeat = main.stepsPerBeat; brk.setName ("Breakdown");
    run (brk, laneFilter, 0, brk.numSteps - brk.stepsPerBeat, fLpClose);
    run (brk, laneTapeStop, brk.numSteps - brk.stepsPerBeat, brk.stepsPerBeat, tsStop);
    on (brk, laneDelay, { brk.stepsPerBeat * 2 }, 3);
    SeqPattern& clean = b.d.bank.seq[3]; clean = SeqPattern(); clean.numSteps = main.numSteps; clean.stepsPerBeat = main.stepsPerBeat; clean.setName ("Clean");
}

struct Def { const char* name; const char* category; void (*build) (Builder&); };

// ================================================================== the presets
const Def defs[] = {
// ------------------------------------------------------------------ Groove: subtle variation
{ "Pocket Change", "Groove", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (1, 1);
    auto& s = b.seq (1, "Pocket");
    on (s, laneChop, { 6, 14 }, 0, 55); on (s, laneChop, { 10 }, 0, 30);
    run (s, laneStutter, 14, 2, d16, 60); on (s, laneDelay, { 12 }, 3, 40); on (s, laneReverse, { 7 }, 0, 25);
    b.p (pid::delayFeedback, 0.42f).p (pid::delayLevel, 0.5f);
} },
{ "Ghost Notes", "Groove", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.06f).slots (1, 1).p (pid::seed, 12);
    auto& s = b.seq (1, "Ghosts");
    on (s, laneReverse, { 7, 15 }, 0, 45); on (s, lanePitch, { 10 }, pitchVar (-1), 35); on (s, laneChop, { 3, 11 }, 0, 40);
    on (s, laneReverb, { 4 }, 1, 30); b.p (pid::reverbLevel, 0.4f);
} },
{ "Loose Screws", "Groove", [] (Builder& b) {
    b.macros (0.56f, 0.56f, 0.5f, 0.0f, 0.5f, 0.22f).slots (1, 1).p (pid::seed, 7);
    auto& s = b.seq (1, "Loose");
    on (s, laneChop, { 5, 13 }, 0, 60); run (s, laneStutter, 15, 1, d32, 70); on (s, laneDelay, { 6, 14 }, 2, 35);
    b.p (pid::delayFeedback, 0.35f).p (pid::delayLevel, 0.45f);
} },
{ "Skip Stone", "Groove", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (1, 1);
    auto& s = b.seq (1, "Skips");
    on (s, laneChop, { 2 }, 1); on (s, laneChop, { 6 }, 5); on (s, laneChop, { 10 }, 3, 80); on (s, laneChop, { 13 }, 1, 70); on (s, laneChop, { 15 }, 0, 60);
    run (s, laneStutter, 7, 1, d32, 50); on (s, laneFilter, { 15 }, fHpRise, 60);
} },
{ "Sunday Driver", "Groove", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (12, 2).p (pid::volDepth, 0.35f).p (pid::tilt, 1.0f).p (pid::volSmooth, 8.0f);
    auto& s = b.seq (1, "Cruise");
    on (s, laneDelay, { 14 }, 3, 60); on (s, laneReverse, { 15 }, 0, 40); b.p (pid::delayFeedback, 0.5f).p (pid::delayLevel, 0.4f);
} },
{ "Call & Response", "Groove", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (1, 1);
    auto& s = b.seq (1, "Two Bar", 32);
    run (s, laneStutter, 28, 2, d16); run (s, laneStutter, 30, 2, d32); on (s, laneReverb, { 28 }, 2);
    on (s, laneChop, { 22, 26 }, 0, 70); on (s, laneReverse, { 23 }, 0, 60); on (s, laneDelay, { 12 }, 3, 50);
    b.p (pid::stutterPitch, 5.0f);
} },
{ "Rim Shot Roulette", "Groove", [] (Builder& b) {
    b.macros (0.62f, 0.5f, 0.5f, 0.0f, 0.5f, 0.1f).slots (1, 1).p (pid::seed, 31);
    auto& s = b.seq (1, "Roulette");
    on (s, laneChop, { 2, 6, 10, 14 }, 0, 45); on (s, laneChop, { 7, 15 }, 0, 65); run (s, laneStutter, 11, 1, d32, 35);
} },

// ------------------------------------------------------------------ Half-Time: trap and slow motion
{ "Half Moon", "Half-Time", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (2, 1).p (pid::lpFreq, 11000.0f);
    auto& s = b.seq (1, "Moon");
    on (s, laneReverb, { 12 }, 2, 70); run (s, laneStutter, 14, 2, d16t, 50); b.p (pid::reverbLevel, 0.5f);
} },
{ "Codeine Step", "Half-Time", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (3, 1).p (pid::crushBits, 10.0f).p (pid::crushRate, 14000.0f).p (pid::tilt, -1.5f);
    auto& s = b.seq (1, "Lean");
    run (s, laneStutter, 12, 4, d8t); run (s, laneCrush, 12, 4); on (s, laneDelay, { 6 }, 3, 60); b.p (pid::stutterPitch, -7.0f);
} },
{ "Slow Pour", "Half-Time", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (1, 1).p (pid::tapeTime, 0.9f);
    auto& s = b.seq (1, "Pour", 32);
    run (s, laneTapeStop, 8, 8, tsDrag); run (s, laneTapeStop, 24, 4, tsDrag); run (s, laneTapeStop, 28, 4, tsStop);
    on (s, laneReverb, { 8, 24 }, 3, 80); b.p (pid::reverbLevel, 0.45f);
} },
{ "Trap Door", "Half-Time", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (2, 1).p (pid::stutterPitch, -12.0f).p (pid::tapeTime, 0.7f);
    auto& s = b.seq (1, "Door", 32);
    run (s, laneStutter, 14, 2, d32t); run (s, laneStutter, 22, 2, d16t, 70); run (s, laneTapeStop, 28, 4, tsStop);
    on (s, laneFilter, { 12, 13 }, fLpClose, 60);
} },
{ "Low Tide", "Half-Time", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.55f, 0.5f, 0.0f).slots (2, 2).p (pid::volDepth, 0.5f).p (pid::volSmooth, 10.0f).p (pid::formant, 1.0f);
    b.voice (0, true, -7, 0.0f, 0.0f, 0.9f).voice (1, false, 4, 0.4f);
    auto& s = b.seq (1, "Tide"); on (s, laneReverb, { 4, 12 }, 3, 50);
} },
{ "Double Cup", "Half-Time", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (3, 1);
    auto& s = b.seq (1, "Cup", 32);
    run (s, laneFilter, 16, 12, fLpClose); run (s, laneStutter, 28, 4, d8t); on (s, laneChop, { 6, 14 }, 0, 70); on (s, laneDelay, { 30 }, 3);
    b.p (pid::filterReso, 0.45f).p (pid::stutterPitch, 7.0f);
} },
{ "Syrup Clock", "Half-Time", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (4, 1).p (pid::tilt, -2.0f).p (pid::width, 1.2f);
    auto& s = b.seq (1, "Clock");
    run (s, laneTapeStop, 4, 4, tsDrag, 80); on (s, lanePitch, { 10, 11 }, pitchVar (-3), 70); b.p (pid::pitchGlide, 1.0f);
} },

// ------------------------------------------------------------------ Mayhem: IDM
{ "Wire Cutter", "Mayhem", [] (Builder& b) {
    b.macros (0.7f, 0.7f, 0.55f, 0.0f, 0.6f, 0.3f).slots (1, 1).p (pid::seed, 101).p (pid::stutterPitch, 12.0f).p (pid::stutterGate, 0.8f);
    auto& s = b.seq (1, "Cutter");
    on (s, laneChop, { 1, 3, 5, 9, 11, 13 }, 0, 75); run (s, laneStutter, 6, 2, d32); run (s, laneStutter, 14, 2, d64);
    on (s, laneRing, { 10 }, 5, 60); on (s, laneCrush, { 2, 12 }, 3, 60); on (s, laneReverse, { 7, 15 }, 0, 50);
} },
{ "Circuit Bent", "Mayhem", [] (Builder& b) {
    b.macros (0.6f, 0.55f, 0.5f, 0.0f, 0.75f, 0.2f).slots (1, 1).p (pid::seed, 55).p (pid::combFeedback, 0.93f).p (pid::ringOctave, 2);
    auto& s = b.seq (1, "Bent");
    on (s, laneCrush, { 0, 1 }, 4, 80); on (s, laneCrush, { 6, 7, 13 }, 3); on (s, laneRing, { 4, 5 }, 3); on (s, laneRing, { 11 }, 6, 70);
    on (s, laneComb, { 8, 9 }, 5); on (s, laneComb, { 14, 15 }, 1); on (s, laneChop, { 3, 10 }, 0, 80); run (s, laneStutter, 12, 1, d64);
} },
{ "Bit Confetti", "Mayhem", [] (Builder& b) {
    b.macros (0.6f, 0.65f, 0.5f, 0.0f, 0.6f, 0.15f).slots (1, 1).p (pid::seed, 9).p (pid::stutterAccel, 0.6f).p (pid::stutterDiv, 1).p (pid::delayFeedback, 0.6f);
    auto& s = b.seq (1, "Confetti");
    run (s, laneStutter, 4, 4, 0); run (s, laneStutter, 12, 4, 0, 80); on (s, laneReverse, { 2, 10 }); on (s, laneDelay, { 3, 11 }, 1, 70);
    on (s, laneCrush, { 8, 9 }, 2);
} },
{ "Tape Chewer", "Mayhem", [] (Builder& b) {
    b.macros (0.55f, 0.5f, 0.6f, 0.0f, 0.5f, 0.15f).slots (8, 1).p (pid::seed, 77).p (pid::tapeTime, 0.6f).p (pid::pitchGlide, 1.0f);
    auto& s = b.seq (1, "Chewed");
    run (s, laneTapeStop, 2, 2, tsStop); run (s, laneTapeStop, 4, 1, tsSpin); run (s, laneReverse, 12, 2); on (s, lanePitch, { 6, 7 }, pitchVar (-4));
    on (s, lanePitch, { 14, 15 }, pitchVar (4), 70);
} },
{ "Glitch Hop", "Mayhem", [] (Builder& b) {
    b.macros (0.6f, 0.6f, 0.5f, 0.0f, 0.5f, 0.1f).slots (1, 1).p (pid::seed, 23).p (pid::filterReso, 0.6f);
    auto& s = b.seq (1, "Hop");
    on (s, laneChop, { 3 }, 1); on (s, laneChop, { 7 }, 5); on (s, laneChop, { 11 }, 9); run (s, laneStutter, 6, 1, d16t); run (s, laneStutter, 13, 1, d32t);
    run (s, laneFilter, 8, 2, fBpUp); run (s, laneFilter, 14, 2, fBpDown); on (s, laneReverse, { 15 }, 0, 70);
} },
{ "Data Rot", "Mayhem", [] (Builder& b) {
    b.macros (0.55f, 0.6f, 0.5f, 0.0f, 0.7f, 0.45f).slots (1, 1).p (pid::seed, 404).p (pid::freezeMs, 45.0f).p (pid::crushBits, 5.0f).p (pid::crushRate, 4200.0f);
    auto& s = b.seq (1, "Rot");
    run (s, laneFreeze, 2, 2, 1); run (s, laneFreeze, 10, 3, 2); run (s, laneCrush, 6, 2); on (s, laneComb, { 13 }, 4); on (s, laneCrush, { 14, 15 }, 4, 70);
} },
{ "Tuesday Machine", "Mayhem", [] (Builder& b) {
    b.macros (0.65f, 0.6f, 0.5f, 0.0f, 0.55f, 0.2f).slots (1, 1).p (pid::seed, 3).p (pid::stutterGate, 0.6f);
    auto& s = b.seq (1, "Machine", 32, 8);       // 1/32 grid, one bar
    on (s, laneChop, { 3, 6, 11, 14, 19, 22, 27 }, 0, 80); run (s, laneStutter, 8, 2, d64); run (s, laneStutter, 24, 4, d128, 80);
    on (s, laneReverse, { 5, 13, 21, 29 }, 0, 60); on (s, laneRing, { 16, 17 }, 8, 60); on (s, laneCrush, { 30, 31 }, 3);
} },
{ "Micro Edits", "Mayhem", [] (Builder& b) {
    b.macros (0.6f, 0.55f, 0.5f, 0.0f, 0.5f, 0.12f).slots (1, 1).p (pid::seed, 64).p (pid::declick, 2.0f);
    auto& s = b.seq (1, "Edits", 32, 8);
    on (s, laneChop, { 2, 5, 9, 13, 18, 21, 26, 29 }, 0, 60); on (s, laneReverse, { 7, 15, 23, 31 }, 0, 60); run (s, laneStutter, 28, 2, d64, 70);
    on (s, lanePitch, { 10, 11 }, pitchVar (2), 50); on (s, laneDelay, { 12 }, 1, 50);
} },
{ "Broken Abacus", "Mayhem", [] (Builder& b) {
    b.macros (0.75f, 0.7f, 0.6f, 0.0f, 0.65f, 0.6f).slots (9, 3).p (pid::seed, 512).p (pid::volDepth, 0.5f).p (pid::stutterPitch, -9.0f);
    auto& s = b.seq (1, "Abacus");
    on (s, laneChop, { 1, 5, 9, 13 }, 0, 80); run (s, laneStutter, 3, 1, d32); on (s, laneComb, { 7 }, 3); on (s, laneRing, { 11 }, 2);
} },

// ------------------------------------------------------------------ Harmony: harmonised chops
{ "Third Rail", "Harmony", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.7f, 0.5f, 0.0f).slots (1, 1);
    b.voice (0, true, 2, -0.45f, -4.0f, 0.8f).voice (1, true, 4, 0.45f, 4.0f, 0.65f);
    auto& s = b.seq (1, "Rail"); on (s, laneChop, { 6, 14 }, 0, 50); on (s, laneReverb, { 12 }, 2, 50);
} },
{ "Choir Stack", "Harmony", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.8f, 0.5f, 0.0f).slots (1, 9).p (pid::volDepth, 0.4f).p (pid::formant, 1.0f).p (pid::reverbLevel, 0.55f).p (pid::reverbTime, 3.5f);
    b.voice (0, true, 2, -0.7f, -6.0f, 0.7f).voice (1, true, 4, 0.7f, 6.0f, 0.7f).voice (2, true, 7, -0.25f, 3.0f, 0.5f).voice (3, true, -7, 0.25f, -3.0f, 0.6f);
    auto& s = b.seq (1, "Choir"); on (s, laneReverb, { 0, 8 }, 3);
} },
{ "Arp Scatter", "Harmony", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.3f, 0.5f, 0.0f).slots (1, 1).p (pid::stutterArp, 1).p (pid::stutterDiv, 3).p (pid::stutterGate, 0.85f).p (pid::delayLevel, 0.45f);
    b.voice (0, true, 4, 0.3f, 0.0f, 0.6f).voice (1, false, 2, -0.3f);
    auto& s = b.seq (1, "Scatter");
    run (s, laneStutter, 4, 4, d16); run (s, laneStutter, 12, 4, d16t); on (s, laneDelay, { 7, 15 }, 3, 70);
} },
{ "Octave Halo", "Harmony", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.6f, 0.5f, 0.0f).slots (1, 1).p (pid::harmMode, 1).p (pid::formant, 0.6f).p (pid::width, 1.3f);
    b.voice (0, true, 12, -0.6f, 7.0f, 0.55f).voice (1, true, -12, 0.0f, 0.0f, 0.75f).voice (2, true, 19, 0.6f, -7.0f, 0.35f).voice (3, false, 7, 0.0f);
    auto& s = b.seq (1, "Halo"); on (s, laneReverb, { 4, 12 }, 3, 60); b.p (pid::reverbLevel, 0.5f);
} },
{ "Chord Stabs", "Harmony", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.85f, 0.5f, 0.0f).slots (1, 11).p (pid::harmGate, 1).p (pid::volDepth, 0.85f).p (pid::volSmooth, 4.0f);
    b.voice (0, true, 2, -0.5f, 0.0f, 0.8f).voice (1, true, 4, 0.5f, 0.0f, 0.8f).voice (2, true, 6, 0.0f, 0.0f, 0.55f);
    auto& s = b.seq (1, "Stabs");
    run (s, laneHarmony, 0, 2, 0); run (s, laneHarmony, 3, 2, 3); run (s, laneHarmony, 6, 2, 0); run (s, laneHarmony, 10, 2, 4); run (s, laneHarmony, 13, 2, 5);
    on (s, laneDelay, { 13 }, 3, 60);
} },
{ "Glass Ladder", "Harmony", [] (Builder& b) {
    b.macros (0.5f, 0.55f, 0.5f, 0.45f, 0.5f, 0.0f).slots (1, 1).p (pid::stutterArp, 3).p (pid::stutterDiv, 2).p (pid::delayFeedback, 0.55f).p (pid::delayLevel, 0.5f).p (pid::delayDiv, 2);
    b.voice (0, true, 4, 0.5f, 5.0f, 0.6f).voice (1, true, 7, -0.5f, -5.0f, 0.4f);
    auto& s = b.seq (1, "Ladder", 32);
    run (s, laneStutter, 8, 8, d8t); run (s, laneStutter, 24, 8, d16); on (s, laneDelay, { 15, 31 }, 3);
} },
{ "Power Fifths", "Harmony", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.65f, 0.55f, 0.0f).slots (1, 1).p (pid::harmMode, 1).p (pid::formant, 0.3f).p (pid::crushBits, 11.0f).p (pid::crushRate, 16000.0f);
    b.voice (0, true, 7, -0.4f, 0.0f, 0.75f).voice (1, true, -5, 0.4f, 0.0f, 0.75f);
    auto& s = b.seq (1, "Fifths"); run (s, laneCrush, 12, 4); run (s, laneStutter, 14, 2, d16, 60);
} },
{ "Sus Machine", "Harmony", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.7f, 0.5f, 0.0f).slots (1, 1).p (pid::harmGate, 1);
    b.voice (0, true, 3, -0.5f, 0.0f, 0.75f).voice (1, true, 4, 0.5f, 0.0f, 0.75f).voice (2, true, -4, 0.0f, 0.0f, 0.5f);
    auto& s = b.seq (1, "Sus", 32);
    run (s, laneHarmony, 0, 6, 0); run (s, laneHarmony, 8, 6, 1); run (s, laneHarmony, 16, 6, 5); run (s, laneHarmony, 24, 8, 2);
    on (s, laneReverb, { 24 }, 3); on (s, laneChop, { 14, 30 }, 0, 60);
} },
{ "Shadow Twin", "Harmony", [] (Builder& b) {
    b.macros (0.55f, 0.5f, 0.5f, 0.5f, 0.5f, 0.05f).slots (3, 1).p (pid::formant, 1.0f);
    b.voice (0, true, -2, -0.6f, -9.0f, 0.8f).voice (1, true, -7, 0.6f, 9.0f, 0.6f);
    auto& s = b.seq (1, "Twin"); on (s, laneChop, { 5, 13 }, 0, 60); on (s, laneReverse, { 15 }, 0, 50);
} },

// ------------------------------------------------------------------ Gates & Pumps
{ "Trancegate '99", "Gates", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (1, 6).p (pid::volSmooth, 2.0f).p (pid::delayDiv, 2).p (pid::delayFeedback, 0.55f).p (pid::delayLevel, 0.4f);
    auto& s = b.seq (1, "Gate"); on (s, laneDelay, { 3, 11 }, 3);
} },
{ "Side Chain Gang", "Gates", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (1, 2).p (pid::volSmooth, 6.0f);
    auto& s = b.seq (1, "Gang"); on (s, laneReverb, { 12 }, 2, 50);
} },
{ "Sixteen Teeth", "Gates", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (1, 3).p (pid::volSmooth, 1.5f).p (pid::filterReso, 0.5f);
    auto& s = b.seq (1, "Teeth", 32); run (s, laneFilter, 16, 16, fLpOpen); on (s, laneChop, { 7, 15, 23 }, 0, 50);
} },
{ "Offbeat Ghost", "Gates", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (1, 5).p (pid::volSmooth, 5.0f).p (pid::reverbLevel, 0.5f);
    auto& s = b.seq (1, "Ghost"); on (s, laneReverb, { 2, 10 }, 2); on (s, laneReverse, { 14, 15 }, 0, 60);
} },
{ "Triplet Shears", "Gates", [] (Builder& b) {
    b.macros (0.5f, 0.55f, 0.5f, 0.0f, 0.5f, 0.0f).slots (9, 8).p (pid::volSmooth, 2.0f);
    auto& s = b.seq (1, "Shears"); on (s, laneDelay, { 14 }, 5, 70); b.p (pid::delayDiv, 4);
} },

// ------------------------------------------------------------------ Tape & Texture
{ "VHS Memory", "Texture", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.05f).slots (12, 1).p (pid::tilt, -2.5f).p (pid::crushBits, 11.0f).p (pid::crushRate, 11000.0f).p (pid::lpFreq, 9000.0f).p (pid::width, 0.85f);
    auto& s = b.seq (1, "VHS"); run (s, laneCrush, 0, 16); run (s, laneTapeStop, 14, 2, tsDrag, 40);
} },
{ "Frozen Lake", "Texture", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (3, 1).p (pid::freezeMs, 140.0f).p (pid::reverbTime, 5.0f).p (pid::reverbLevel, 0.6f);
    auto& s = b.seq (1, "Lake", 32); run (s, laneFreeze, 6, 2, 3); run (s, laneFreeze, 20, 4, 4); on (s, laneReverb, { 6, 20 }, 3);
} },
{ "Rewind Selector", "Texture", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (1, 1).p (pid::tapeTime, 0.75f);
    auto& s = b.seq (1, "Selector", 32);
    run (s, laneReverse, 24, 4); run (s, laneTapeStop, 28, 4, tsStop); on (s, laneDelay, { 23 }, 3); run (s, laneFilter, 24, 4, fHpRise);
} },
{ "Resonator Bay", "Texture", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.6f, 0.0f).slots (1, 1).p (pid::combFeedback, 0.95f).p (pid::combDamp, 4500.0f).p (pid::combOctave, 0);
    auto& s = b.seq (1, "Bay");
    run (s, laneComb, 0, 2, 1); run (s, laneComb, 4, 2, 3); run (s, laneComb, 8, 2, 5); run (s, laneComb, 11, 2, 4); run (s, laneComb, 14, 2, 8);
} },
{ "Radio Drift", "Texture", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.6f, 0.1f).slots (1, 7).p (pid::seed, 88).p (pid::volDepth, 0.5f).p (pid::hpFreq, 180.0f).p (pid::lpFreq, 6000.0f).p (pid::ringOctave, 2);
    auto& s = b.seq (1, "Drift", 32); run (s, laneRing, 12, 4, 5, 70); run (s, laneFilter, 24, 8, fBpUp); run (s, laneCrush, 28, 4, 2);
} },
{ "Clean Slate", "Init", [] (Builder& b) {
    b.macros (0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f).slots (1, 1);
    b.seq (1, "Empty");
} },
};
constexpr int kNumDefs = (int) (sizeof (defs) / sizeof (defs[0]));
} // namespace

namespace presets
{
int numFactory() { return kNumDefs; }
String factoryName (int index) { return defs[jlimit (0, kNumDefs - 1, index)].name; }
String factoryCategory (int index) { return defs[jlimit (0, kNumDefs - 1, index)].category; }

void buildFactory (int index, PresetData& out)
{
    const Def& def = defs[jlimit (0, kNumDefs - 1, index)];
    out = PresetData();
    out.name = def.name; out.category = def.category;
    Builder b { out };
    def.build (b);
    if (String (def.category) != "Init") standardVariations (b);
}

bool isSessionParam (const String& id)
{
    static const StringArray session { pid::sourceMode, pid::liveBars, pid::midiMode, pid::sliceTrig, pid::loopPlay, pid::key, pid::scale,
                                       pid::inputMonitor, pid::bypass, pid::stretchMode, pid::quantize, pid::declick };
    return session.contains (id);
}

File userDirectory()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Splinter").getChildFile ("Presets");
}

Array<File> userPresetFiles()
{
    Array<File> files;
    const File dir = userDirectory();
    if (dir.isDirectory()) files = dir.findChildFiles (File::findFiles, false, "*.splinter");
    files.sort();
    return files;
}
} // namespace presets
