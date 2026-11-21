#include "HelpText.h"

#include "Theme.h"

#include <cstring>

namespace swoonui
{

namespace
{
struct Entry
{
    const char* id;
    const char* text;
};

// Keyed by parameter id so the table survives reordering of the enum. Wording follows the
// README's parameter guide. Plain ASCII on purpose.
constexpr Entry kEntries[] = {
    // macros / global
    { "wall",        "Fuzz amount and density. 50 % is the preset as designed; lower fades the fuzz out, higher pushes past it." },
    { "wash",        "Reverb size and mix: hall mix, decay and room size together. 50 % is the preset as designed." },
    { "glide",       "Pitch drift: scales the wobble, the random drift and the strum dip. 50 % is the preset as designed." },
    { "blur",        "Soft focus and modulation: their mixes, detune and haze. 50 % is the preset as designed." },
    { "warmth",      "Darker, softer and more saturated above 50 %; brighter and cleaner below." },
    { "mix",         "Dry/wet for the whole plugin. The dry signal is delay-compensated." },
    { "bypass",      "Click-free, latency-compensated bypass." },
    { "inGain",      "Input trim so the fuzz and gate see a sensible level. Not changed by presets." },
    { "inSwell",     "Fades each note in, like riding a volume pedal." },

    // fuzz
    { "fuzzType",    "Sustain: thick and singing. Octave: a rectifier adds an octave up. Velcro: spits and cuts notes off." },
    { "fuzzVoice",   "Mid response of the tone stack: slightly dipped, pushed low mids, or the classic deep scoop." },
    { "fuzzSustain", "Gain into the clippers (a 40 dB range). There is some fuzz even at zero." },
    { "fuzzTone",    "Dark to bright." },
    { "fuzzGate",    "Closes the input below a threshold so the noise floor isn't amplified. 0 is off." },
    { "fuzzStack",   "Feeds each side a slightly different, drifting delay before the fuzz: two guitars instead of one." },
    { "fuzzLevel",   "Fuzz output level." },
    { "fuzzMix",     "Fuzz against clean." },

    // glide
    { "glideWobble", "Depth of the slow up-and-down pitch wobble." },
    { "glideRate",   "Wobble speed. It wanders slightly, like a hand." },
    { "glideDrift",  "Random, slow pitch wandering." },
    { "glideDip",    "How far the pitch drops on each detected strum. Harder strums dip further." },
    { "glideReturn", "How long the pitch takes to swim back up after a dip." },
    { "glideSpread", "Lets left and right drift apart. Low values stay centred; high values sound like two players." },
    { "glideMix",    "Below 100 % the drifting copy sits beside the dry signal as a double." },

    // reverse
    { "revMode",     "Swell: taps that rise and then stop, so notes bloom. Tape: true reverse delay. Both: reverse into the swell." },
    { "revTime",     "Length of the swell, or of each reversed slice." },
    { "revFeedback", "Repeats." },
    { "revSmear",    "Shape of the rise and amount of diffusion." },
    { "revTone",     "Low-pass on the wet signal. Turn it down for the classic sound." },
    { "revMix",      "At 100 % there is no dry signal at all, so notes bloom out of nothing." },

    // soft focus
    { "focTime",     "Left delay time; the right is always 1.52x longer." },
    { "focFeedback", "How long the two delays keep feeding each other." },
    { "focDetune",   "Peak detune of the four chorus voices." },
    { "focHaze",     "Amount and length of the reverb inside the patch." },
    { "focTone",     "Darkens the repeats and the haze." },
    { "focMix",      "Soft focus against the direct signal." },

    // modulation
    { "modMode",     "Chorus, Ensemble (three-voice string-machine chorus), Vibrato, Flanger or Phaser." },
    { "modRate",     "Modulation speed." },
    { "modDepth",    "Modulation depth." },
    { "modColor",    "Feedback (chorus, flanger, phaser) or tone (ensemble, vibrato)." },
    { "modWidth",    "Phase offset between left and right." },
    { "modMix",      "Equal-power mix; 100 % is fully wet (true vibrato)." },

    // wash
    { "washDecay",   "Mid-band decay time of the hall." },
    { "washSize",    "Small room to enormous hall." },
    { "washPre",     "Gap between the note and the start of the reverb." },
    { "washDamp",    "Damping: how quickly the top end dies. Warmth moves this too." },
    { "washLow",     "Keeps the low end out of the tail." },
    { "washMod",     "Chorusing inside the tail." },
    { "washShimmer", "Adds a pitch-shifted copy inside the reverb loop, so the tail keeps climbing. It cannot run away." },
    { "washShimMode","Interval of the shimmer voice." },
    { "washDuck",    "Lowers the reverb while you play and lets it rise between phrases." },
    { "washFreeze",  "Holds the current tail indefinitely and closes the input." },
    { "washMix",     "Reverb against the direct signal." },

    // amp
    { "ampDrive",    "Clean through edge-of-breakup to overdriven." },
    { "ampBass",     "Low shelf. Centre is flat." },
    { "ampTreble",   "High shelf. Centre is flat." },
    { "ampCab",      "Hand-voiced cabinet filters, not impulse responses. Direct bypasses the cabinet." },
    { "ampLevel",    "Amp output level." },
    { "ampMix",      "Amp and cabinet against the unprocessed signal." },

    // tape
    { "tapeDrive",   "Tape saturation." },
    { "tapeWow",     "Slow speed variation." },
    { "tapeFlutter", "Fast speed variation." },
    { "tapeAge",     "Loses top end, adds head bump and a little level instability." },
    { "tapeMix",     "Tape against the clean signal." },

    // output
    { "outTilt",     "Tilts the spectrum around 700 Hz, darker to brighter." },
    { "outWidth",    "Mono to extra wide. Widening leaves the bass centred." },
    { "outGain",     "Output level. A soft limiter after this holds the output under -0.2 dBFS whatever you do." },
};

bool isOrderParam (swoon::P p) noexcept
{
    for (auto o : swoon::kOrderParams)
        if (o == p)
            return true;
    return false;
}
} // namespace

juce::String helpTextFor (swoon::P p)
{
    if (p == swoon::P::Count)
        return {};

    const auto& d = swoon::def (p);

    for (const auto& e : kEntries)
        if (std::strcmp (e.id, d.id) == 0)
            return e.text;

    // --- no hand-written entry: say something true from the table -------------------------
    const juce::String module (swoon::ui::moduleName (d.module));

    if (isOrderParam (p))
        return "Place of this module in the chain (1 is first). Equal values fall back to the default order.";

    if (&d == swoon::ui::powerParamFor (d.module))
        return "Switches the " + module + " module on or off.";

    switch (d.kind)
    {
        case swoon::ParamKind::Bool:
            return juce::String (d.name) + ": on or off.";

        case swoon::ParamKind::Choice:
            return juce::String (d.name) + ": " + juce::StringArray::fromTokens (d.choices, "|", "").joinIntoString (" / ") + ".";

        case swoon::ParamKind::Int:
            return juce::String (d.name) + ": " + juce::String ((int) d.min) + " to " + juce::String ((int) d.max) + ".";

        case swoon::ParamKind::Float:
            break;
    }

    const juce::String unit (d.unit);
    const bool percent = unit == "%";
    const auto number = [percent] (float v) { return juce::String (percent ? v * 100.0f : v, 0); };
    return juce::String (d.name) + ": " + number (d.min) + " to " + number (d.max) + (unit.isNotEmpty() ? " " + unit : juce::String()) + ".";
}

} // namespace swoonui
