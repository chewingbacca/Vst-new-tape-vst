// Offline host harness: instantiates the real AudioProcessor, simulates a host
// (tempo, transport, loop, MIDI, changing block sizes) and renders to WAV.
//
//   automata_render [options]
//     --project <file.automata>     load a saved project / preset file
//     --factory <name|index>        load a factory preset
//     --idea <style>:<seed>         generate a New Idea project
//     --out <file.wav>              main mix output (24-bit)
//     --tracks <prefix>             also write <prefix>_t1.wav ... (enables the track buses)
//     --midi <file.txt>             dump MIDI output events as text
//     --bars N --bpm B --sr R --block N --varblock --loop A:B (bars) --tail S
//     --set <key>=<norm>            set a global parameter by key (normalised 0..1), repeatable
//     --state-roundtrip             save state, load it into a second instance, render that instead
//     --list-presets                print factory preset names and exit
//     --stats                       print peak/rms/NaN stats as JSON on stdout
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "HostHarness.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    return harness::runCli (argc, argv);
}
