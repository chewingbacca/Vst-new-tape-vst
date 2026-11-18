#include "PluginEditor.h"

using namespace verdant;
using namespace verdant::ui;

namespace
{
    constexpr float kHeaderHeight = 54.0f;
    constexpr float kStatusHeight = 28.0f;
    constexpr float kMargin       = 16.0f;
    constexpr int   kTimerHz      = 30;

    constexpr int kMinWidth = int (float (kBaseWidth) * kMinScale);
    constexpr int kMaxWidth = int (float (kBaseWidth) * kMaxScale);

    int heightFor (int width) noexcept
    {
        return juce::roundToInt (double (width) * double (kBaseHeight) / double (kBaseWidth));
    }

    // First visible ChoicePill for a parameter, searching a component tree.
    ChoicePill* findPill (juce::Component& root, PID pid)
    {
        for (auto* child : root.getChildren())
        {
            if (! child->isVisible())
                continue;

            if (auto* pill = dynamic_cast<ChoicePill*> (child))
                if (pill->getDef().pid == pid)
                    return pill;

            if (auto* found = findPill (*child, pid))
                return found;
        }

        return nullptr;
    }
}

//==============================================================================
VerdantEditor::VerdantEditor (VerdantProcessor& p)
    : juce::AudioProcessorEditor (p),
      proc (p),
      ctx (p),
      status (ctx),
      choicePopup (ctx),
      header (ctx),
      gardenPage (ctx),
      soilPage (ctx),
      browser (ctx)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    setWantsKeyboardFocus (true);

    ctx.editor = this;
    ctx.hoverBegin = [this] (juce::Component* owner, const juce::String& title, juce::RangedAudioParameter* source, const juce::String& help)
    {
        status.setHover (owner, title, source, help);
    };
    ctx.hoverEnd = [this] (juce::Component* owner) { status.clearHover (owner); };
    ctx.showChoices = [this] (ChoicePill& pill)
    {
        choicePopup.setBounds (getLocalBounds());
        choicePopup.show (pill);
    };

    addAndMakeVisible (header);
    addChildComponent (gardenPage);
    addChildComponent (soilPage);
    addAndMakeVisible (status);
    addChildComponent (browser);
    addChildComponent (choicePopup);

    {
        const auto dot = juce::String::fromUTF8 ("   \xc2\xb7   ");
        status.setIdleText ("Drag a knob to adjust" + dot + "Shift for fine control" + dot + "double-click to reset" + dot + "wheel to nudge");
    }
    status.setRightText (juce::String ("v") + JucePlugin_VersionString);

    header.tabs.onChange = [this] (int index) { setPage (index); };
    header.presetBar.onToggleBrowser = [this] { setPresetBrowserOpen (! browser.isOpen()); };
    header.presetBar.onPresetChanged = [this] { browser.refreshCurrent(); };
    browser.onClose = [this] { header.presetBar.setBrowserOpen (false); };
    browser.onPresetChanged = [this] { header.presetBar.refreshName(); };

    page = juce::jlimit (0, 1, proc.editorPage);
    header.tabs.setSelected (page, false);
    gardenPage.setVisible (page == garden);
    soilPage.setVisible (page == soil);

    // Resizable with a fixed aspect ratio; everything scales from the width.
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (double (kBaseWidth) / double (kBaseHeight));
    setResizeLimits (kMinWidth, heightFor (kMinWidth), kMaxWidth, heightFor (kMaxWidth));

    const int width = proc.editorWidth > 0 ? juce::jlimit (kMinWidth, kMaxWidth, proc.editorWidth) : kBaseWidth;
    constructed = true;
    setSize (width, heightFor (width));

    lastTickMs = juce::Time::getMillisecondCounterHiRes();
    updateTimerState();
}

VerdantEditor::~VerdantEditor()
{
    stopTimer();
    choicePopup.dismiss();

    // Controls call back into the context from their destructors (to clear the help line);
    // disconnect it before any member goes away.
    ctx.hoverBegin = nullptr;
    ctx.hoverEnd = nullptr;
    ctx.showChoices = nullptr;
    ctx.editor = nullptr;

    setLookAndFeel (nullptr);
}

//==============================================================================
void VerdantEditor::setPage (int newPage)
{
    newPage = juce::jlimit (0, 1, newPage);
    choicePopup.dismiss();

    page = newPage;
    proc.editorPage = page;
    header.tabs.setSelected (page, false);
    gardenPage.setVisible (page == garden);
    soilPage.setVisible (page == soil);
    repaint();
}

void VerdantEditor::setPresetBrowserOpen (bool shouldBeOpen)
{
    choicePopup.dismiss();

    if (shouldBeOpen)
        browser.open();
    else
        browser.close();

    header.presetBar.setBrowserOpen (browser.isOpen());
}

GardenVisual& VerdantEditor::getActiveVisual()
{
    return page == garden ? gardenPage.visual : soilPage.strip;
}

//==============================================================================
void VerdantEditor::resized()
{
    if (! constructed)
        return;

    ctx.scale = juce::jlimit (0.4f, 4.0f, float (getWidth()) / float (kBaseWidth));

    // remembered with the session
    proc.editorWidth = getWidth();
    proc.editorHeight = getHeight();

    auto b = getLocalBounds();
    header.setBounds (b.removeFromTop (ctx.pxi (kHeaderHeight)));
    status.setBounds (b.removeFromBottom (ctx.pxi (kStatusHeight)));

    browser.setBounds (b);

    auto pageArea = b.reduced (ctx.pxi (kMargin), 0).withTrimmedTop (ctx.pxi (6.0f)).withTrimmedBottom (ctx.pxi (8.0f));
    gardenPage.setBounds (pageArea);
    soilPage.setBounds (pageArea);

    choicePopup.setBounds (getLocalBounds());
}

void VerdantEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (backdropGradient (b));
    g.fillRect (b);

    // a breath of light behind the header
    juce::ColourGradient glow (juce::Colour (0xff1d4634).withAlpha (0.55f), b.getCentreX(), 0.0f,
                               juce::Colour (0xff1d4634).withAlpha (0.0f), b.getCentreX(), ctx.px (150.0f), false);
    g.setGradientFill (glow);
    g.fillRect (b.withHeight (ctx.px (150.0f)));
}

bool VerdantEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (choicePopup.isOpen())
        {
            choicePopup.dismiss();
            return true;
        }

        if (browser.isOpen())
        {
            setPresetBrowserOpen (false);
            return true;
        }
    }

    return false;
}

//==============================================================================
void VerdantEditor::visibilityChanged()         { updateTimerState(); }
void VerdantEditor::parentHierarchyChanged()    { updateTimerState(); }

void VerdantEditor::updateTimerState()
{
    // No timer work while the editor is hidden or not on screen.
    if (isShowing())
    {
        if (! isTimerRunning())
        {
            lastTickMs = juce::Time::getMillisecondCounterHiRes();
            startTimerHz (kTimerHz);
        }
    }
    else
    {
        stopTimer();
    }
}

void VerdantEditor::timerCallback()
{
    if (! isShowing())
    {
        stopTimer();
        return;
    }

    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = float (juce::jlimit (0.0, 0.1, (now - lastTickMs) * 0.001));
    lastTickMs = now;
    step (dt);
}

void VerdantEditor::step (float dt)
{
    header.meters.update (proc.engine.meters, dt);
    header.presetBar.refreshName();
    browser.refreshCurrent();
    status.refresh();

    if (page == garden)
    {
        gardenPage.visual.advance (dt);
        gardenPage.refresh();
    }
    else
    {
        soilPage.strip.advance (dt);
    }
}

void VerdantEditor::advanceAnimation (double seconds)
{
    const float dt = 1.0f / float (kTimerHz);
    for (int i = juce::jmax (1, juce::roundToInt (seconds * double (kTimerHz))); --i >= 0;)
        step (dt);
}

//==============================================================================
void VerdantEditor::debugShowHelpFor (PID pid)
{
    const auto& def = kParams[pid];
    status.setHover (this, def.name, proc.getParam (pid), def.help);
}

bool VerdantEditor::debugOpenChoiceList (PID pid)
{
    if (auto* pill = findPill (*this, pid))
    {
        ctx.showChoices (*pill);
        return true;
    }

    return false;
}

void VerdantEditor::debugSavePreset (const juce::String& name)
{
    setPresetBrowserOpen (true);
    browser.debugSaveAs (name);
}

void VerdantEditor::debugSetExtraPresets (const juce::Array<VerdantProcessor::PresetInfo>& extras)
{
    browser.setDebugExtraPresets (extras);
}
