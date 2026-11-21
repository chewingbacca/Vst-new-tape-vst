// Second page ("the machine"): one panel per module, built entirely from the
// parameter table. Float/Int -> knob, Choice -> stepped selector, Bool -> switch,
// the module's "xxxOn" Bool -> the panel's power button.
#pragma once

#include "ChainModel.h"
#include "ParamControls.h"

class SwoonAudioProcessor;

namespace swoon::ui
{

class ModulePanel final : public juce::Component
{
public:
    ModulePanel (SwoonAudioProcessor&, Module, const std::vector<const ParamDef*>& controlDefs);

    Module getModule() const noexcept { return module; }
    int getNumColumns() const noexcept { return juce::jmax (1, ((int) controls.size() + kRows - 1) / kRows); }
    void setBadge (const juce::String&);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kRows = 2;
    static constexpr int kPadX = 7;
    static constexpr int kHeaderH = 31;

private:
    void updateEnabledLook();

    Module module;
    juce::String title, badge;
    juce::Colour accent;
    std::unique_ptr<PowerButton> power;
    std::vector<std::unique_ptr<ParamControl>> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModulePanel)
};

//==============================================================================
class MachinePage final : public juce::Component,
                          private juce::ChangeListener
{
public:
    MachinePage (SwoonAudioProcessor&, ChainModel&);
    ~MachinePage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateBadges();

    ChainModel& model;
    std::vector<std::unique_ptr<ModulePanel>> panels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MachinePage)
};

} // namespace swoon::ui
