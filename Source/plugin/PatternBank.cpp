#include "PatternBank.h"

using namespace juce;
using namespace splinter;

namespace bankutil
{
Curve flatTime() { Curve c; c.clear(); c.lengthBeats = 4.0f; c.rangeBeats = 4.0f; c.add (0.0f, 0.0f); c.add (1.0f, 0.0f); c.setName ("Straight"); return c; }
Curve flatVol() { Curve c; c.clear(); c.lengthBeats = 4.0f; c.rangeBeats = 1.0f; c.add (0.0f, 1.0f); c.add (1.0f, 1.0f); c.setName ("Open"); return c; }

// A staircase between x0 and x1: every step jumps back by stepHeight (a repeat).
void addStairs (Curve& c, int steps, float stepHeight, float x0, float x1)
{
    for (int i = 0; i < steps; ++i)
        c.add (x0 + (x1 - x0) * (float) i / (float) steps, stepHeight * (float) i, 0.0f, true);
}

Curve gate (int cells, float duty, float floorLevel, float attack)
{
    Curve c; c.clear(); c.lengthBeats = 4.0f; c.rangeBeats = 1.0f;
    for (int i = 0; i < cells; ++i)
    {
        const float a = (float) i / (float) cells, w = 1.0f / (float) cells;
        if (attack > 0.0f) { c.add (a, floorLevel); c.add (a + w * attack, 1.0f, 0.0f, true); }
        else c.add (a, 1.0f, 0.0f, true);
        c.add (a + w * duty, floorLevel, 0.0f, true);
    }
    return c;
}
} // namespace bankutil

void PatternBank::reset()
{
    using namespace bankutil;
    // ------------------------------------------------ time curve library
    // y is "fraction of rangeBeats behind real time"; range is 4 beats unless noted.
    for (auto& c : time) c = flatTime();
    {   // 2: half-time — falls two beats behind over the bar, snaps back on the one
        Curve& c = time[1]; c.clear(); c.add (0.0f, 0.0f); c.add (1.0f, 0.5f); c.setName ("Half-Time");
    }
    {   // 3: half-time per two beats (tighter, keeps more of the groove)
        Curve& c = time[2]; c.clear(); c.add (0.0f, 0.0f); c.add (0.5f, 0.25f); c.add (0.5f, 0.0f); c.add (1.0f, 0.25f); c.setName ("Half x2");
    }
    {   // 4: last beat tape stop
        Curve& c = time[3]; c.clear(); c.add (0.0f, 0.0f); c.add (0.75f, 0.0f, 0.3333f); c.add (1.0f, 0.125f); c.setName ("Tape Stop 4");   // delay grows as t^2: speed 1 -> 0
    }
    {   // 5: rewind — second half plays backwards through the first half
        Curve& c = time[4]; c.clear(); c.add (0.0f, 0.0f); c.add (0.5f, 0.0f); c.add (1.0f, 1.0f); c.setName ("Rewind");
    }
    {   // 6: eighth repeats on beat 4
        Curve& c = time[5]; c.clear(); c.add (0.0f, 0.0f); c.add (0.75f, 0.0f, 0.0f, true); c.add (0.875f, 0.125f, 0.0f, true); c.setName ("Repeat 8th");
    }
    {   // 7: build — 1/8, 1/16 then 1/32 repeats through the last two beats
        Curve& c = time[6]; c.clear(); c.add (0.0f, 0.0f, 0.0f, true);
        c.add (0.5f, 0.0f, 0.0f, true); c.add (0.625f, 0.125f, 0.0f, true);
        for (int i = 0; i < 2; ++i) c.add (0.75f + 0.0625f * (float) i, 0.25f + 0.0625f * (float) i, 0.0f, true);
        for (int i = 0; i < 4; ++i) c.add (0.875f + 0.03125f * (float) i, 0.375f + 0.03125f * (float) i, 0.0f, true);
        c.setName ("Build Up");
    }
    {   // 8: scratch — rocks back and forth over beat 3
        Curve& c = time[7]; c.clear(); c.add (0.0f, 0.0f); c.add (0.5f, 0.0f, -0.4f); c.add (0.5625f, 0.11f, 0.4f); c.add (0.625f, 0.02f, -0.4f);
        c.add (0.6875f, 0.15f, 0.4f); c.add (0.75f, 0.0f); c.add (1.0f, 0.0f); c.setName ("Scratch");
    }
    {   // 9: triplet repeats across the second half
        Curve& c = time[8]; c.clear(); c.add (0.0f, 0.0f, 0.0f, true);
        for (int i = 0; i < 6; ++i) c.add (0.5f + (float) i / 12.0f, (float) i / 12.0f, 0.0f, true);
        c.setName ("Triplets");
    }
    {   // 10: whole bar reversed (reads the previous bar backwards)
        Curve& c = time[9]; c.clear(); c.rangeBeats = 8.0f; c.add (0.0f, 0.0f); c.add (1.0f, 1.0f); c.setName ("Reverse Bar");
    }
    {   // 11: beat repeat — each beat plays beat one
        Curve& c = time[10]; c.clear(); addStairs (c, 4, 0.25f); c.setName ("Beat Hold");
    }
    {   // 12: swing drag — lazy second eighth of every beat
        Curve& c = time[11]; c.clear();
        for (int b = 0; b < 4; ++b) { const float x = (float) b * 0.25f; c.add (x, 0.0f); c.add (x + 0.125f, 0.0f, 0.5f); c.add (x + 0.1875f, 0.02f, -0.5f); c.add (x + 0.2499f, 0.0f); }
        c.setName ("Drag");
    }

    // ------------------------------------------------ volume curve library
    for (auto& c : vol) c = flatVol();
    {   // 2: sidechain pump per beat
        Curve& c = vol[1]; c.clear();
        for (int b = 0; b < 4; ++b) { const float x = (float) b * 0.25f; c.add (x, 0.08f, -0.55f); c.add (x + 0.2f, 1.0f); c.add (x + 0.2499f, 1.0f); }
        c.setName ("Pump");
    }
    { vol[2] = gate (16, 0.5f); vol[2].setName ("Gate 16"); }
    { vol[3] = gate (8, 0.5f); vol[3].setName ("Gate 8"); }
    {   // 5: offbeat only
        Curve& c = vol[4]; c.clear();
        for (int b = 0; b < 4; ++b) { const float x = (float) b * 0.25f; c.add (x, 0.0f, 0.0f, true); c.add (x + 0.125f, 1.0f, 0.0f, true); }
        c.setName ("Offbeat");
    }
    {   // 6: classic trance pattern x.xx.xx.x.xx.xx.
        Curve& c = vol[5]; c.clear();
        const int pat[16] = { 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0 };
        for (int i = 0; i < 16; ++i) { c.add ((float) i / 16.0f, pat[i] ? 1.0f : 0.0f, 0.0f, true); if (pat[i]) c.add (((float) i + 0.8f) / 16.0f, 0.0f, 0.0f, true); }
        c.setName ("Trance");
    }
    {   // 7: smooth tremolo in eighths
        Curve& c = vol[6]; c.clear();
        for (int i = 0; i < 8; ++i) { const float x = (float) i / 8.0f; c.add (x, 1.0f, 0.3f); c.add (x + 0.0625f, 0.25f, -0.3f); }
        c.add (1.0f, 1.0f); c.setName ("Tremolo");
    }
    { vol[7] = gate (12, 0.55f); vol[7].setName ("Triplet Gate"); }
    {   // 9: swell into each half bar
        Curve& c = vol[8]; c.clear(); c.add (0.0f, 0.0f, 0.45f); c.add (0.4999f, 1.0f); c.add (0.5f, 0.0f, 0.45f); c.add (1.0f, 1.0f); c.setName ("Swell");
    }
    {   // 10: fade the bar out
        Curve& c = vol[9]; c.clear(); c.add (0.0f, 1.0f, 0.3f); c.add (1.0f, 0.0f); c.setName ("Fade Bar");
    }
    {   // 11: stabs — short hits on a syncopated rhythm
        Curve& c = vol[10]; c.clear();
        const int hits[5] = { 0, 3, 6, 10, 13 };
        c.add (0.0f, 0.0f, 0.0f, true);
        for (int h : hits) { c.add ((float) h / 16.0f, 1.0f, 0.4f); c.add (((float) h + 1.5f) / 16.0f, 0.0f, 0.0f, true); }
        c.setName ("Stabs");
    }
    {   // 12: hard pump in eighths
        Curve& c = vol[11]; c.clear();
        for (int i = 0; i < 8; ++i) { const float x = (float) i / 8.0f; c.add (x, 0.0f, -0.4f); c.add (x + 0.1f, 1.0f); c.add (x + 0.1249f, 1.0f); }
        c.setName ("Pump 8");
    }

    for (int i = 0; i < kNumSlots; ++i) { seq[i] = SeqPattern(); seq[i].setName (("Pattern " + String (i + 1)).toRawUTF8()); }
    slices.makeGrid (8.0f, 4);
    sliceMode = 0; sliceSensitivity = 0.5f;
}

// ---------------------------------------------------------------- serialisation
static ValueTree curveToTree (const Curve& c, const Identifier& type, int slot)
{
    ValueTree t (type);
    t.setProperty ("slot", slot, nullptr);
    t.setProperty ("name", String (c.name), nullptr);
    t.setProperty ("len", c.lengthBeats, nullptr);
    t.setProperty ("range", c.rangeBeats, nullptr);
    String s;
    for (int i = 0; i < c.numNodes; ++i)
        s << String (c.nodes[i].x, 5) << ' ' << String (c.nodes[i].y, 5) << ' ' << String (c.nodes[i].tension, 3) << ' ' << (int) c.nodes[i].hold << ';';
    t.setProperty ("nodes", s, nullptr);
    return t;
}

static void curveFromTree (const ValueTree& t, Curve& c)
{
    c.clear();
    c.setName (t["name"].toString().toRawUTF8());
    c.lengthBeats = jlimit (0.25f, 64.0f, (float) t.getProperty ("len", 4.0f));
    c.rangeBeats = jlimit (0.25f, 32.0f, (float) t.getProperty ("range", 4.0f));
    StringArray nodes; nodes.addTokens (t["nodes"].toString(), ";", "");
    for (auto& n : nodes)
    {
        StringArray v; v.addTokens (n.trim(), " ", "");
        if (v.size() >= 2)
            c.add (jlimit (0.0f, 1.0f, v[0].getFloatValue()), jlimit (0.0f, 1.0f, v[1].getFloatValue()),
                   v.size() > 2 ? jlimit (-1.0f, 1.0f, v[2].getFloatValue()) : 0.0f, v.size() > 3 && v[3].getIntValue() != 0);
    }
}

ValueTree PatternBank::toTree (bool includeSlices) const
{
    ValueTree root ("Patterns");
    for (int i = 0; i < kNumSlots; ++i) root.appendChild (curveToTree (time[i], "Time", i), nullptr);
    for (int i = 0; i < kNumSlots; ++i) root.appendChild (curveToTree (vol[i], "Vol", i), nullptr);
    for (int i = 0; i < kNumSlots; ++i)
    {
        const SeqPattern& p = seq[i];
        ValueTree t ("Seq");
        t.setProperty ("slot", i, nullptr);
        t.setProperty ("name", String (p.name), nullptr);
        t.setProperty ("steps", p.numSteps, nullptr);
        t.setProperty ("spb", p.stepsPerBeat, nullptr);
        String locks; for (int l = 0; l < kNumLanes; ++l) locks << (p.locked[l] ? '1' : '0');
        t.setProperty ("locks", locks, nullptr);
        for (int l = 0; l < kNumLanes; ++l)
        {
            String s;
            for (int k = 0; k < kMaxSteps; ++k)
                if (p.steps[l][k].on) s << k << ' ' << (int) p.steps[l][k].prob << ' ' << (int) p.steps[l][k].var << ';';
            if (s.isNotEmpty()) { ValueTree lt ("Lane"); lt.setProperty ("id", laneId (l), nullptr); lt.setProperty ("steps", s, nullptr); t.appendChild (lt, nullptr); }
        }
        root.appendChild (t, nullptr);
    }
    if (includeSlices)
    {
        ValueTree t ("Slices");
        t.setProperty ("beats", slices.loopBeats, nullptr);
        t.setProperty ("mode", sliceMode, nullptr);
        t.setProperty ("sens", sliceSensitivity, nullptr);
        String s; for (int i = 0; i < slices.count; ++i) s << String (slices.startBeat[i], 5) << ' ';
        t.setProperty ("starts", s, nullptr);
        root.appendChild (t, nullptr);
    }
    return root;
}

void PatternBank::fromTree (const ValueTree& root, bool includeSlices)
{
    if (! root.isValid()) return;
    for (const auto& t : root)
    {
        const int slot = jlimit (0, kNumSlots - 1, (int) t.getProperty ("slot", 0));
        if (t.hasType ("Time")) curveFromTree (t, time[slot]);
        else if (t.hasType ("Vol")) curveFromTree (t, vol[slot]);
        else if (t.hasType ("Seq"))
        {
            SeqPattern& p = seq[slot];
            p = SeqPattern();
            p.setName (t["name"].toString().toRawUTF8());
            p.numSteps = jlimit (1, kMaxSteps, (int) t.getProperty ("steps", 16));
            p.stepsPerBeat = jlimit (1, 8, (int) t.getProperty ("spb", 4));
            const String locks = t["locks"].toString();
            for (int l = 0; l < kNumLanes && l < locks.length(); ++l) p.locked[l] = locks[l] == '1';
            for (const auto& lt : t)
            {
                int lane = -1;
                for (int l = 0; l < kNumLanes; ++l) if (lt["id"].toString() == laneId (l)) lane = l;
                if (lane < 0) continue;
                StringArray steps; steps.addTokens (lt["steps"].toString(), ";", "");
                for (auto& s : steps)
                {
                    StringArray v; v.addTokens (s.trim(), " ", "");
                    if (v.size() < 1 || v[0].isEmpty()) continue;
                    const int k = v[0].getIntValue();
                    if (k < 0 || k >= kMaxSteps) continue;
                    p.steps[lane][k].on = 1;
                    p.steps[lane][k].prob = (uint8_t) jlimit (0, 100, v.size() > 1 ? v[1].getIntValue() : 100);
                    p.steps[lane][k].var = (uint8_t) jlimit (0, 255, v.size() > 2 ? v[2].getIntValue() : 0);
                }
            }
        }
        else if (t.hasType ("Slices") && includeSlices)
        {
            slices.loopBeats = jlimit (1.0f, 128.0f, (float) t.getProperty ("beats", 8.0f));
            sliceMode = jlimit (0, 5, (int) t.getProperty ("mode", 0));
            sliceSensitivity = jlimit (0.0f, 1.0f, (float) t.getProperty ("sens", 0.5f));
            StringArray v; v.addTokens (t["starts"].toString().trim(), " ", "");
            slices.count = 0;
            for (auto& s : v)
                if (s.isNotEmpty() && slices.count < kMaxSlices) slices.startBeat[slices.count++] = jlimit (0.0f, slices.loopBeats, s.getFloatValue());
            if (slices.count == 0) slices.makeGrid (slices.loopBeats, 4);
        }
    }
}
