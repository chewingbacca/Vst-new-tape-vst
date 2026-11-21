#include "PluginEditor.h"

namespace
{
using namespace swoon::ui;

constexpr float kMinScale = 0.8f, kMaxScale = 1.7f;      // 800 x 528 ... 1700 x 1122
const juce::Identifier kPageId ("page"), kWidthId ("width"), kHeightId ("height");
} // namespace

SwoonAudioProcessorEditor::SwoonAudioProcessorEditor (SwoonAudioProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      chain (p),
      header (p),
      sleeve (p, chain),
      machine (p, chain),
      browser (p.getPresetManager())
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    content.setInterceptsMouseClicks (false, true);
    content.addAndMakeVisible (sleeve);
    content.addChildComponent (machine);
    content.addAndMakeVisible (header);
    content.addAndMakeVisible (statusLine);
    content.addChildComponent (browser);
    addAndMakeVisible (content);

    content.setBounds (0, 0, kBaseW, kBaseH);
    header.setBounds (0, 0, kBaseW, kHeaderH);
    sleeve.setBounds (0, kHeaderH, kBaseW, kBaseH - kHeaderH - kStatusH);
    machine.setBounds (sleeve.getBounds());
    browser.setBounds (sleeve.getBounds());
    statusLine.setBounds (0, kBaseH - kStatusH, kBaseW, kStatusH);

    header.onPageSelected = [this] (int newPage) { setPage (newPage); };
    header.onPresetNameClicked = [this] { setBrowserOpen (! browser.isVisible()); };
    browser.onClose = [this] { setBrowserOpen (false); };

    // double-click a chip on the sleeve: go to that module's panel
    sleeve.getChain().onOpenModule = [this] (swoon::Module module)
    {
        setPage (1);
        machine.spotlight (module);
    };

    const auto state = proc.getUiState();
    setPage (juce::jlimit (0, 1, (int) state.getProperty (kPageId, 0)));

    setResizable (true, true);
    if (auto* limits = getConstrainer())
        limits->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);

    setResizeLimits (juce::roundToInt ((float) kBaseW * kMinScale), juce::roundToInt ((float) kBaseH * kMinScale),
                     juce::roundToInt ((float) kBaseW * kMaxScale), juce::roundToInt ((float) kBaseH * kMaxScale));

    int width = (int) state.getProperty (kWidthId, kBaseW);
    int height = (int) state.getProperty (kHeightId, kBaseH);
    if (width < juce::roundToInt ((float) kBaseW * kMinScale) || width > juce::roundToInt ((float) kBaseW * kMaxScale) || height < 100)
    {
        width = kBaseW;
        height = kBaseH;
    }

    proc.getPresetManager().addChangeListener (this);
    updateSleeveText();

    constructed = true;
    setSize (width, height);
    startTimerHz (30);
}

SwoonAudioProcessorEditor::~SwoonAudioProcessorEditor()
{
    stopTimer();
    proc.getPresetManager().removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void SwoonAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::ink);   // only visible as letterboxing when a host forces another aspect ratio
}

void SwoonAudioProcessorEditor::resized()
{
    if (! constructed)
        return;

    const float w = (float) getWidth(), h = (float) getHeight();
    const float scale = juce::jmin (w / (float) kBaseW, h / (float) kBaseH);

    content.setTransform (juce::AffineTransform::scale (scale)
                              .translated ((w - (float) kBaseW * scale) * 0.5f, (h - (float) kBaseH * scale) * 0.5f));

    auto state = proc.getUiState();
    state.setProperty (kWidthId, getWidth(), nullptr);
    state.setProperty (kHeightId, getHeight(), nullptr);
}

void SwoonAudioProcessorEditor::setPage (int newPage)
{
    page = juce::jlimit (0, 1, newPage);
    sleeve.setVisible (page == 0);
    machine.setVisible (page == 1);
    header.setPage (page);
    proc.getUiState().setProperty (kPageId, page, nullptr);

    statusLine.clearHint();
    updateIdleHint();
}

void SwoonAudioProcessorEditor::showHelp (const juce::String& title, const juce::String& text, const juce::String& value)
{
    statusLine.setHint (title, text, value);
}

void SwoonAudioProcessorEditor::clearHelp()
{
    statusLine.clearHint();
}

void SwoonAudioProcessorEditor::setBrowserOpen (bool shouldBeOpen)
{
    if (shouldBeOpen)
        browser.open();
    else
        browser.setVisible (false);

    header.setBrowserOpen (shouldBeOpen);
    updateIdleHint();
}

void SwoonAudioProcessorEditor::updateIdleHint()
{
    statusLine.setIdleText (browser.isVisible()
        ? "Up / Down steps through the list  -  type to search  -  Esc closes"
        : page == 0
            ? "Drag a knob up or down  -  Shift for fine  -  double-click to reset  -  click a value to type it"
            : "Drag up or down  -  Shift for fine  -  double-click to reset  -  click a readout (or Ctrl/Cmd-click a knob) to type a value");
}

//==============================================================================
void SwoonAudioProcessorEditor::timerCallback()
{
    // nothing animates while the host keeps the window hidden (no peer = offscreen rendering, e.g. the snapshot tool)
    if (getPeer() != nullptr && ! isShowing())
        return;

    header.tick();

    // the haze rests while it is hidden or dimmed behind the browser
    if (page == 0 && ! browser.isVisible())
        sleeve.getHaze().advance();
}

void SwoonAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateSleeveText();
}

void SwoonAudioProcessorEditor::updateSleeveText()
{
    auto& presets = proc.getPresetManager();
    const auto& entries = presets.getEntries();
    const int index = presets.getCurrentIndex();

    auto name = presets.getCurrentName();
    juce::String category, note;

    if (index >= 0 && index < (int) entries.size())
    {
        category = entries[(size_t) index].isFactory ? entries[(size_t) index].category : juce::String ("User");
        note = entries[(size_t) index].note;
    }

    sleeve.getHaze().setSleeveText (name.isEmpty() ? juce::String ("Untitled") : name, category, note);
}
