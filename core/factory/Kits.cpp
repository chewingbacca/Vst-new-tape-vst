// Factory kits: each is a New Idea result for a fixed (style, seed, tempo), plus a
// few curated global overrides. Building them from the generator keeps them small
// and guarantees they are reproducible.
#include "Presets.h"
#include "../gen/Idea.h"
#include <sstream>

namespace atm {

namespace {
struct KitDef { KitPresetInfo info; uint32_t seed; const char* overrides; };

// overrides: global parameter keys with physical values, plus evolve=1 / song=1
const KitDef kKits[] = {
    // ---------------------------------------------------------------- broken beat
    { { "Copper Pulse",     Style::BrokenBeat, 112 }, 1207, "complexity=0.2" },
    { { "Fractured Sunday", Style::BrokenBeat, 96 },  3391, "swing=0.6 mutation=0.18" },
    { { "Lantern Steps",    Style::BrokenBeat, 124 }, 5523, "" },
    { { "Mossbank",         Style::BrokenBeat, 104 }, 8810, "space=0.6 chaos=0.12" },
    { { "Paper Gears",      Style::BrokenBeat, 118 }, 2046, "glitch=0.12" },
    { { "Slow Ferro",       Style::BrokenBeat, 92 },  7177, "density=-0.15 space=0.55" },
    { { "Tilt Shift",       Style::BrokenBeat, 130 }, 4412, "complexity=0.3 evolve=1" },
    { { "Velvet Static",    Style::BrokenBeat, 100 }, 9935, "chaos=0.15" },
    // ---------------------------------------------------------------- drill & bass
    { { "Shatterpoint",     Style::DrillBass, 174 }, 1717, "complexity=0.5 glitch=0.2" },
    { { "Gnat Swarm",       Style::DrillBass, 168 }, 2828, "density=0.25" },
    { { "Ratchet Choir",    Style::DrillBass, 178 }, 3939, "complexity=0.6" },
    { { "Bone Lattice",     Style::DrillBass, 172 }, 4040, "glitch=0.3" },
    { { "Scissor Kite",     Style::DrillBass, 165 }, 5151, "mutation=0.25" },
    { { "Hyperfold",        Style::DrillBass, 180 }, 6262, "complexity=0.55 chaos=0.2" },
    { { "Splinter Glass",   Style::DrillBass, 170 }, 7373, "glitch=0.25 space=0.45" },
    { { "Murmuration",      Style::DrillBass, 176 }, 8484, "evolve=1" },
    // ---------------------------------------------------------------- ambient melodic
    { { "Polaroid Lake",    Style::AmbientMelodic, 84 },  1111, "space=0.8" },
    { { "Weatherglass",     Style::AmbientMelodic, 78 },  2222, "" },
    { { "Cassette Pines",   Style::AmbientMelodic, 90 },  3333, "chaos=0.2" },
    { { "Halflight",        Style::AmbientMelodic, 72 },  4444, "space=0.85 density=-0.2" },
    { { "Orchard Hum",      Style::AmbientMelodic, 96 },  5555, "mutation=0.25" },
    { { "Low Tide Radio",   Style::AmbientMelodic, 80 },  6666, "evolve=1" },
    { { "Sodium Fields",    Style::AmbientMelodic, 88 },  7777, "" },
    { { "Kite Memory",      Style::AmbientMelodic, 100 }, 8888, "space=0.7" },
    // ---------------------------------------------------------------- clicks & cuts
    { { "Micro Loom",       Style::ClicksCuts, 120 }, 1313, "glitch=0.35" },
    { { "Dust Protocol",    Style::ClicksCuts, 112 }, 2424, "" },
    { { "Static Garden",    Style::ClicksCuts, 126 }, 3535, "chaos=0.3" },
    { { "Hairline",         Style::ClicksCuts, 108 }, 4646, "density=-0.2 glitch=0.25" },
    { { "Relay Moth",       Style::ClicksCuts, 118 }, 5757, "complexity=0.3" },
    { { "Glass Abacus",     Style::ClicksCuts, 124 }, 6868, "" },
    { { "Null Tick",        Style::ClicksCuts, 116 }, 7979, "glitch=0.45 evolve=1" },
    { { "Carbon Lace",      Style::ClicksCuts, 130 }, 8080, "mutation=0.2" },
    // ---------------------------------------------------------------- acid
    { { "Chrome Bile",      Style::Acid, 132 }, 1919, "" },
    { { "Wet Circuit",      Style::Acid, 128 }, 2929, "space=0.5" },
    { { "Sour Voltage",     Style::Acid, 136 }, 3939, "macro1=0.3" },
    { { "Rubber Sun",       Style::Acid, 124 }, 4949, "swing=0.55" },
    { { "Neon Gut",         Style::Acid, 140 }, 5959, "complexity=0.12" },
    { { "Squelch Theory",   Style::Acid, 130 }, 6969, "mutation=0.15 evolve=1" },
    { { "Copper Lung",      Style::Acid, 126 }, 7979, "" },
    { { "Peel Away",        Style::Acid, 138 }, 8989, "macro1=0.2 glitch=0.08" },
};
constexpr int kNum = (int) (sizeof (kKits) / sizeof (kKits[0]));
}

int numKitPresets() { return kNum; }
const KitPresetInfo& kitPresetInfo (int i) { return kKits[clamp (i, 0, kNum - 1)].info; }

bool buildKitPreset (int index, Project& out, SampleRefIO* samples)
{
    if (index < 0 || index >= kNum) return false;
    const KitDef& d = kKits[index];
    initProject (out);
    IdeaOptions o;
    o.style = d.info.style; o.seed = d.seed; o.tempoMin = o.tempoMax = d.info.bpm; o.keepLocked = false;
    generateIdea (out, o, samples);
    std::snprintf (out.name, sizeof (out.name), "%s", d.info.name);
    std::istringstream in (d.overrides);
    std::string tok;
    while (in >> tok)
    {
        const size_t eq = tok.find ('=');
        if (eq == std::string::npos) continue;
        const std::string k = tok.substr (0, eq), v = tok.substr (eq + 1);
        if (k == "evolve") { out.evolveOn = (uint8_t) (std::atoi (v.c_str()) != 0); continue; }
        if (k == "song") { out.song.songMode = (uint8_t) (std::atoi (v.c_str()) != 0); continue; }
        const int gp = globalParamFromKey (k.c_str());
        if (gp < 0) continue;
        float n;
        if (parseParam (globalParamDesc (gp, out.kit.fxType), v.c_str(), n)) out.kit.gparams[gp] = n;
    }
    return true;
}

} // namespace atm
