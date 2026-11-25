// Factory voice presets (parsed from VoicePresets.inc) and kit presets (built
// from the New Idea generator with fixed seeds plus curated overrides).
#include "Presets.h"
#include "FactorySamples.h"
#include "../gen/Idea.h"
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace atm {

namespace {

const char* const kVoiceText =
#include "VoicePresets.inc"
;

struct ParsedVoice
{
    VoicePresetInfo info;
    std::string name, category;
    std::vector<std::pair<std::string, std::string>> kv;
};

std::string trim (const std::string& s)
{
    const size_t a = s.find_first_not_of (" \t\r\n"), b = s.find_last_not_of (" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr (a, b - a + 1);
}

int roleFromKey (const std::string& k)
{
    static const char* const keys[Role::count] = { "kick", "snare", "hat", "perc", "bass", "lead", "pad", "fx" };
    for (int i = 0; i < Role::count; ++i) if (k == keys[i]) return i;
    return Role::Perc;
}

const std::vector<ParsedVoice>& voices()
{
    static std::vector<ParsedVoice> list;
    static std::once_flag once;
    std::call_once (once, []
    {
        std::istringstream in (kVoiceText);
        std::string line;
        while (std::getline (in, line))
        {
            line = trim (line);
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> cols;
            size_t p = 0;
            for (int c = 0; c < 4; ++c)
            {
                const size_t bar = line.find ('|', p);
                if (bar == std::string::npos) break;
                cols.push_back (trim (line.substr (p, bar - p)));
                p = bar + 1;
            }
            if (cols.size() < 4) continue;
            ParsedVoice v;
            v.name = cols[0]; v.category = cols[1];
            v.info.engine = std::max (0, engineFromKey (cols[2].c_str()));
            v.info.role = roleFromKey (cols[3]);
            std::istringstream kvs (line.substr (p));
            std::string tok;
            while (kvs >> tok)
            {
                const size_t eq = tok.find ('=');
                if (eq != std::string::npos) v.kv.push_back ({ tok.substr (0, eq), tok.substr (eq + 1) });
            }
            list.push_back (std::move (v));
        }
        for (auto& v : list) { v.info.name = v.name.c_str(); v.info.category = v.category.c_str(); }
    });
    return list;
}

int srcFromKey (const std::string& k)
{
    static const char* const keys[Src::count] = { "none", "lfo1", "lfo2", "modenv", "ampenv", "stepmod", "velocity", "noternd", "note",
                                                  "macro1", "macro2", "macro3", "macro4", "chaosx", "chaosy", "follower" };
    for (int i = 0; i < Src::count; ++i) if (k == keys[i]) return i;
    return Src::None;
}

} // namespace

int numVoicePresets() { return (int) voices().size(); }
const VoicePresetInfo& voicePresetInfo (int i) { return voices()[(size_t) clamp (i, 0, numVoicePresets() - 1)].info; }
int findVoicePreset (const char* name)
{
    const auto& v = voices();
    for (size_t i = 0; i < v.size(); ++i) if (v[i].name == name) return (int) i;
    return -1;
}

bool loadVoicePresetChecked (int index, VoicePresetData& out, SampleRefIO* samples, std::string* errors)
{
    const auto& list = voices();
    if (index < 0 || index >= (int) list.size()) return false;
    const ParsedVoice& pv = list[(size_t) index];
    out = VoicePresetData{};
    TrackSound& s = out.sound;
    initTrackSound (s, pv.info.engine);
    std::snprintf (s.name, sizeof (s.name), "%s", pv.name.c_str());
    std::snprintf (out.category, sizeof (out.category), "%s", pv.category.c_str());
    s.role = (uint8_t) pv.info.role;
    s.baseNote = (s.role == Role::Bass) ? 36 : (s.role == Role::Lead || s.role == Role::Pad ? 60 : 60);
    if (s.role == Role::Bass || s.role == Role::Lead || s.role == Role::Pad) s.scaleOn = 1;
    bool ok = true;
    auto fail = [&] (const std::string& m) { ok = false; if (errors) *errors += pv.name + ": " + m + "\n"; };
    for (const auto& [k, v] : pv.kv)
    {
        if (k == "fxa" || k == "fxb")
        {
            const int t = fxFromKey (v.c_str());
            if (t < 0) { fail ("unknown effect " + v); continue; }
            const int slot = k == "fxa" ? 0 : 1;
            s.fxType[slot] = (uint8_t) t;
            setFxDefaults (&s.params[slot == 0 ? TP::FxA0 : TP::FxB0], t);
        }
        else if (k == "poly") s.poly = (uint8_t) (std::atoi (v.c_str()) != 0);
        else if (k == "scale") s.scaleOn = (uint8_t) (std::atoi (v.c_str()) != 0);
        else if (k == "base") s.baseNote = (uint8_t) clamp (std::atoi (v.c_str()), 0, 127);
        else if (k == "sample")
        {
            Json d = Json::object(); d.set ("factory", v);
            if (getFactorySample (v.c_str()) == nullptr) fail ("unknown factory sample " + v);
            else if (samples) s.sampleId = samples->resolveSample (d);
        }
        else if (k == "mod")
        {
            // source:dest:amount
            const size_t a = v.find (':'), b = v.rfind (':');
            if (a == std::string::npos || b == a) { fail ("bad mod " + v); continue; }
            const int src = srcFromKey (v.substr (0, a));
            const int dst = findTrackParam (s, v.substr (a + 1, b - a - 1).c_str());
            if (src == Src::None || dst < 0 || out.numMods >= 8) { fail ("bad mod " + v); continue; }
            ModSlot& m = out.mods[out.numMods++];
            m.src = (uint8_t) src; m.srcTrack = 0; m.dstTrack = 0; m.dstParam = (uint16_t) dst;
            m.amount = clamp ((float) std::atof (v.substr (b + 1).c_str()), -1.f, 1.f);
        }
        else if (! setTrackParamText (s, k.c_str(), v.c_str())) fail ("bad param " + k + "=" + v);
    }
    return ok;
}

bool loadVoicePreset (int index, VoicePresetData& out, SampleRefIO* samples)
{
    loadVoicePresetChecked (index, out, samples, nullptr);
    return index >= 0 && index < numVoicePresets();
}

void applyVoicePreset (Project& p, int track, const VoicePresetData& v)
{
    if (track < 0 || track >= kTracks) return;
    TrackSound& dst = p.kit.tracks[track];
    const TrackSound keep = dst;
    dst = v.sound;
    dst.mute = keep.mute; dst.solo = keep.solo; dst.locked = keep.locked;
    dst.midiOut = keep.midiOut; dst.midiCh = keep.midiCh; dst.directOut = keep.directOut;
    p.kit.fxType[track * kInsertsPerTrack] = dst.fxType[0];
    p.kit.fxType[track * kInsertsPerTrack + 1] = dst.fxType[1];
    for (ModSlot& m : p.kit.mods)
        if (m.src != Src::None && m.dstTrack == track && (! srcIsPerTrack (m.src) || m.srcTrack == track)) m = ModSlot{};
    for (int i = 0; i < v.numMods; ++i)
        for (ModSlot& m : p.kit.mods)
            if (m.src == Src::None)
            {
                m = v.mods[i];
                m.srcTrack = (uint8_t) track; m.dstTrack = (int8_t) track;
                break;
            }
}

} // namespace atm
