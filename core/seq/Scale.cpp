#include "Scale.h"
#include <sstream>
#include <cstdio>
#include <cstdlib>

namespace atm {

static const ScaleDef kScales[kNumScales] = {
    { "Chromatic",      0b111111111111 },
    { "Major",          0b101010110101 },
    { "Minor",          0b010110101101 },
    { "Dorian",         0b011010101101 },
    { "Phrygian",       0b010110101011 },
    { "Lydian",         0b101011010101 },
    { "Mixolydian",     0b011010110101 },
    { "Locrian",        0b010101101011 },
    { "Harmonic Minor", 0b100110101101 },
    { "Melodic Minor",  0b101010101101 },
    { "Pentatonic Maj", 0b001010010101 },
    { "Pentatonic Min", 0b010010101001 },
    { "Blues",          0b010011101001 },
    { "Whole Tone",     0b010101010101 },
    { "Diminished",     0b101101101101 },
    { "Hirajoshi",      0b000110001101 },
    { "In-Sen",         0b010010100011 },
    { "Phrygian Dom",   0b010110110011 },
    { "Hungarian Min",  0b100111001101 },
    { "Prometheus",     0b011001010101 },
};
const ScaleDef& scaleDef (int i) { return kScales[clamp (i, 0, kNumScales - 1)]; }

void ScaleTable::build (const Tuning& t)
{
    root = clamp ((int) t.root, 0, 11);
    if (t.scale == 255 && t.customCount > 0)
    {
        count = clamp (t.customCount, 1, kMaxScaleNotes);
        period = t.customCents[count - 1];
        if (! (period > 1.f)) period = 1200.f;
        cents[0] = 0.f;
        for (int i = 1; i < count; ++i) cents[i] = t.customCents[i - 1];
        return;
    }
    const uint16_t mask = scaleDef (t.scale).mask | 1u;
    count = 0; period = 1200.f;
    for (int i = 0; i < 12; ++i) if (mask & (1u << i)) cents[count++] = 100.f * (float) i;
}

int ScaleTable::toneAtOrBelow (float note) const
{
    const float c = (note - (float) root) * 100.f;
    const int oct = (int) std::floor (c / period);
    const float rem = c - (float) oct * period;
    int deg = 0;
    for (int i = count - 1; i >= 0; --i) if (cents[i] <= rem + 0.01f) { deg = i; break; }
    return oct * count + deg;
}

bool parseScala (const std::string& text, Tuning& out, std::string& error)
{
    std::istringstream in (text);
    std::string line;
    int stage = 0, expected = 0, got = 0;
    Tuning t = out;
    t.scale = 255; t.customCount = 0;
    std::string desc;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r') line.pop_back();
        size_t p = line.find_first_not_of (" \t");
        if (p != std::string::npos && line[p] == '!') continue;
        if (stage == 0) { desc = line; stage = 1; continue; }
        if (p == std::string::npos) continue;
        if (stage == 1)
        {
            expected = std::atoi (line.c_str() + p);
            if (expected <= 0 || expected > kMaxScaleNotes) { error = "Scale must have 1.." + std::to_string (kMaxScaleNotes) + " notes"; return false; }
            stage = 2; continue;
        }
        if (got >= expected) break;
        // pitch: contains '.' => cents; otherwise ratio n/d or integer n
        size_t e = line.find_first_of (" \t", p);
        std::string tok = line.substr (p, e == std::string::npos ? std::string::npos : e - p);
        double cents = 0;
        if (tok.find ('.') != std::string::npos) cents = std::atof (tok.c_str());
        else
        {
            const size_t slash = tok.find ('/');
            const double num = std::atof (tok.substr (0, slash).c_str());
            const double den = slash == std::string::npos ? 1.0 : std::atof (tok.substr (slash + 1).c_str());
            if (num <= 0 || den <= 0) { error = "Bad ratio: " + tok; return false; }
            cents = 1200.0 * std::log2 (num / den);
        }
        t.customCents[got++] = (float) cents;
    }
    if (stage < 2 || got != expected) { error = "Incomplete Scala file"; return false; }
    // degrees must ascend for degree mapping to make sense; sort defensively
    std::sort (t.customCents, t.customCents + got);
    if (t.customCents[got - 1] < 1.f) { error = "Scale period must be positive"; return false; }
    t.customCount = got;
    std::snprintf (t.name, sizeof (t.name), "%s", desc.empty() ? "Scala" : desc.c_str());
    out = t;
    return true;
}

} // namespace atm
