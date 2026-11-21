// Factory + user presets. Factory presets come from the JUCE-free table in
// dsp/Presets.h; user presets are XML files in the user's app-data folder.
// Message thread, except restoreCurrentName() and the factory-program calls,
// which a host may make from elsewhere.
#pragma once

#include "dsp/Params.h"

#include <juce_audio_utils/juce_audio_utils.h>

class SwoonAudioProcessor;

class PresetManager final : public juce::ChangeBroadcaster
{
public:
    struct Entry
    {
        juce::String name;
        juce::String category;   // factory category, or "User"
        juce::String note;       // one-line description (may be empty)
        bool isFactory = true;
        juce::File file;         // user presets only
    };

    explicit PresetManager (SwoonAudioProcessor&);
    ~PresetManager() override;

    /** Factory presets first (in table order), then user presets sorted by name. */
    const std::vector<Entry>& getEntries() const noexcept { return entries; }
    juce::StringArray getCategories() const;

    void rescanUserPresets();

    /** Loads entry `index`; sends a change message. */
    void loadPreset (int index);
    void loadNext();
    void loadPrevious();

    /** -1 when the state was restored from a session and matches no known entry. */
    int getCurrentIndex() const noexcept { return currentIndex.load(); }
    juce::String getCurrentName() const { const juce::ScopedLock sl (stateLock); return currentName; }

    /** True once any parameter has changed since the last preset load. */
    bool isModified() const noexcept { return modified.load(); }

    /** Saves the current settings as a user preset (overwrites same name). Returns false on I/O failure. */
    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (int index);
    juce::File getUserPresetDirectory() const;

    // --- host program interface (factory presets only) -----------------------------------
    int getNumFactoryPresets() const noexcept { return numFactory; }
    int getCurrentFactoryIndex() const noexcept { const int i = currentIndex.load(); return i >= 0 && i < numFactory ? i : -1; }
    juce::String getFactoryPresetName (int index) const;
    void loadFactoryPreset (int index);

    /** Called by the processor after restoring session state. */
    void restoreCurrentName (const juce::String& name);

private:
    void applyValues (const swoon::ParamValues& values);
    void markLoaded (int index, const juce::String& name);

    struct Listener;
    SwoonAudioProcessor& processor;
    std::vector<Entry> entries;
    int numFactory = 0;
    // Hosts may restore state or change program off the message thread: the name and index are guarded.
    mutable juce::CriticalSection stateLock;
    std::atomic<int> currentIndex { 0 };
    juce::String currentName;
    std::atomic<bool> modified { false };
    std::atomic<bool> loading { false };
    std::unique_ptr<Listener> listener;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};
