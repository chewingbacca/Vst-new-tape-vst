#include "PresetManager.h"
#include "PluginProcessor.h"

#include "dsp/Presets.h"

struct PresetManager::Listener final : public juce::AudioProcessorParameter::Listener
{
    explicit Listener (PresetManager& o) : owner (o) {}

    void parameterValueChanged (int, float) override
    {
        // May be called from any thread: only touch atomics here.
        if (! owner.loading.load (std::memory_order_relaxed))
            owner.modified.store (true, std::memory_order_relaxed);
    }
    void parameterGestureChanged (int, bool) override {}

    PresetManager& owner;
};

PresetManager::PresetManager (SwoonAudioProcessor& p) : processor (p)
{
    for (const auto& preset : swoon::factoryPresets())
        entries.push_back ({ preset.name, preset.category, preset.note, true, {} });

    numFactory = (int) entries.size();
    currentName = entries.empty() ? juce::String() : entries.front().name;
    rescanUserPresets();

    listener = std::make_unique<Listener> (*this);
    for (const auto& d : swoon::paramDefs())
        if (! swoon::isPresetExempt (d.index))
            processor.getParam (d.index).addListener (listener.get());
}

juce::File PresetManager::getUserPresetDirectory() const
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");   // JUCE returns ~/Library on macOS
   #endif
    return base.getChildFile ("Swoon").getChildFile ("Presets");
}

juce::StringArray PresetManager::getCategories() const
{
    juce::StringArray cats;
    for (const auto& e : entries)
        cats.addIfNotAlreadyThere (e.category);
    return cats;
}

void PresetManager::rescanUserPresets()
{
    const juce::ScopedLock sl (stateLock);
    const juce::String keepName = currentIndex.load() >= numFactory ? currentName : juce::String();
    entries.resize ((size_t) numFactory);

    auto files = getUserPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.swoon");
    files.sort();

    for (const auto& f : files)
        entries.push_back ({ f.getFileNameWithoutExtension(), "User", {}, false, f });

    if (keepName.isNotEmpty())
    {
        currentIndex = -1;
        for (int i = numFactory; i < (int) entries.size(); ++i)
            if (entries[(size_t) i].name == keepName)
                currentIndex = i;
    }
}

void PresetManager::applyValues (const swoon::ParamValues& values)
{
    loading.store (true);

    for (const auto& d : swoon::paramDefs())
    {
        if (swoon::isPresetExempt (d.index))
            continue;

        auto& param = processor.getParam (d.index);
        const float normalised = param.convertTo0to1 (values[d.index]);
        if (std::abs (param.getValue() - normalised) > 1.0e-7f)
            param.setValueNotifyingHost (normalised);
    }

    loading.store (false);
}

void PresetManager::markLoaded (int index, const juce::String& name)
{
    {
        const juce::ScopedLock sl (stateLock);
        currentIndex = index;
        currentName = name;
    }
    modified.store (false);
    sendChangeMessage();
}

void PresetManager::loadPreset (int index)
{
    if (index < 0 || index >= (int) entries.size())
        return;

    const auto& entry = entries[(size_t) index];

    if (entry.isFactory)
    {
        applyValues (swoon::presetValues (swoon::factoryPresets()[(size_t) index]));
        markLoaded (index, entry.name);
        return;
    }

    if (auto xml = juce::XmlDocument::parse (entry.file))
    {
        if (xml->hasTagName ("SwoonPreset"))
        {
            swoon::ParamValues values;   // defaults for anything the file doesn't mention
            for (auto* e : xml->getChildWithTagNameIterator ("Param"))
            {
                const auto p = swoon::paramFromId (e->getStringAttribute ("id").toStdString());
                if (p != swoon::P::Count)
                {
                    const auto& d = swoon::def (p);
                    values[p] = juce::jlimit (d.min, d.max, (float) e->getDoubleAttribute ("value", d.def));
                }
            }
            applyValues (values);
            markLoaded (index, entry.name);
        }
    }
}

void PresetManager::loadNext()
{
    if (entries.empty())
        return;
    const int cur = currentIndex.load();
    loadPreset (cur < 0 ? 0 : (cur + 1) % (int) entries.size());
}

void PresetManager::loadPrevious()
{
    if (entries.empty())
        return;
    const int n = (int) entries.size();
    const int cur = currentIndex.load();
    loadPreset (cur < 0 ? n - 1 : (cur + n - 1) % n);
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return false;

    const auto dir = getUserPresetDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory())
        return false;

    juce::XmlElement xml ("SwoonPreset");
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", 1);

    for (const auto& d : swoon::paramDefs())
    {
        if (swoon::isPresetExempt (d.index))
            continue;

        auto& param = processor.getParam (d.index);
        auto* e = xml.createNewChildElement ("Param");
        e->setAttribute ("id", d.id);
        e->setAttribute ("value", (double) param.convertFrom0to1 (param.getValue()));
    }

    if (! xml.writeTo (dir.getChildFile (name + ".swoon")))
        return false;

    rescanUserPresets();
    for (int i = numFactory; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].name == name)
            markLoaded (i, name);

    return true;
}

bool PresetManager::deleteUserPreset (int index)
{
    if (index < numFactory || index >= (int) entries.size())
        return false;

    if (! entries[(size_t) index].file.deleteFile())
        return false;

    const bool wasCurrent = index == currentIndex.load();
    if (currentIndex.load() > index)
        --currentIndex;

    rescanUserPresets();
    if (wasCurrent)
        currentIndex = -1;

    sendChangeMessage();
    return true;
}

juce::String PresetManager::getFactoryPresetName (int index) const
{
    return index >= 0 && index < numFactory ? entries[(size_t) index].name : juce::String();
}

void PresetManager::loadFactoryPreset (int index)
{
    if (index >= 0 && index < numFactory)
        loadPreset (index);
}

void PresetManager::restoreCurrentName (const juce::String& name)
{
    {
        const juce::ScopedLock sl (stateLock);
        currentName = name;
        currentIndex = -1;
        for (int i = 0; i < (int) entries.size(); ++i)
            if (entries[(size_t) i].name == name)
            {
                currentIndex = i;
                break;
            }
    }

    modified.store (false);
    sendChangeMessage();
}

PresetManager::~PresetManager()
{
    for (const auto& d : swoon::paramDefs())
        if (! swoon::isPresetExempt (d.index))
            processor.getParam (d.index).removeListener (listener.get());
}
