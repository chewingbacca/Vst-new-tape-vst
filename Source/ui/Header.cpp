#include "Header.h"

#include "../PluginProcessor.h"
#include "StatusLine.h"

namespace swoon::ui
{

namespace
{
float levelToProportion (float linear) noexcept
{
    // -54 dB .. +6 dB across the bar; 0 dB sits at 0.9
    const float db = juce::Decibels::gainToDecibels (linear, -90.0f);
    return juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 60.0f);
}

constexpr float kMeterLabelW = 27.0f;
} // namespace

//==============================================================================
MeterView::MeterView (const VizData& v) : viz (v), labelFont (sansFont (9.6f, 1, 0.1f))
{
    setInterceptsMouseClicks (false, false);
    setTitle ("Level meters");
}

void MeterView::tick()
{
    const float newIn = levelToProportion (viz.inLevel.load (std::memory_order_relaxed));
    const float newL = levelToProportion (viz.outLevelL.load (std::memory_order_relaxed));
    const float newR = levelToProportion (viz.outLevelR.load (std::memory_order_relaxed));
    const float newGr = juce::jlimit (0.0f, 1.0f, std::abs (viz.limiterReductionDb.load (std::memory_order_relaxed)) / 12.0f);

    const float step = 1.0f / juce::jmax (40.0f, (float) getWidth() - kMeterLabelW);   // about one base pixel

    if (std::abs (newIn - in) > step || std::abs (newL - outL) > step || std::abs (newR - outR) > step
        || std::abs (newGr - reduction) > step || ((newGr <= 0.0f) != (reduction <= 0.0f)))
    {
        in = newIn;
        outL = newL;
        outR = newR;
        reduction = newGr;
        repaint();
    }
}

void MeterView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float x0 = kMeterLabelW, barW = b.getWidth() - x0;

    juce::ColourGradient levelGradient (col::seaglass, x0, 0.0f, col::peach, x0 + barW, 0.0f, false);
    levelGradient.addColour (0.55, col::lavender);
    levelGradient.addColour (0.86, col::rose);

    const auto bar = [&] (float y, float h, float proportion)
    {
        g.setColour (juce::Colours::white.withAlpha (0.075f));
        g.fillRoundedRectangle (x0, y, barW, h, h * 0.5f);

        if (proportion > 0.004f)
        {
            g.setGradientFill (levelGradient);
            g.fillRoundedRectangle (x0, y, juce::jmax (h, barW * proportion), h, h * 0.5f);
        }
    };

    bar (4.0f, 3.4f, in);
    bar (13.0f, 3.4f, outL);
    bar (18.4f, 3.4f, outR);

    // 0 dB mark
    g.setColour (col::fog.withAlpha (0.28f));
    g.fillRect (x0 + barW * 0.9f - 0.5f, 11.0f, 1.0f, 12.8f);

    // limiter gain reduction grows from the right
    const float gy = 27.0f, gh = 2.6f;
    g.setColour (juce::Colours::white.withAlpha (0.075f));
    g.fillRoundedRectangle (x0, gy, barW, gh, gh * 0.5f);

    if (reduction > 0.0f)
    {
        const float w = juce::jmax (gh, barW * reduction);
        g.setGradientFill (juce::ColourGradient (col::rose, x0 + barW - w, 0.0f, col::peach, x0 + barW, 0.0f, false));
        g.fillRoundedRectangle (x0 + barW - w, gy, w, gh, gh * 0.5f);
    }

    g.setFont (labelFont);
    g.setColour (col::dusk);
    g.drawText ("IN", juce::Rectangle<float> (0.0f, 0.5f, x0 - 5.0f, 10.0f), juce::Justification::centredRight, false);
    g.drawText ("OUT", juce::Rectangle<float> (0.0f, 12.2f, x0 - 5.0f, 10.0f), juce::Justification::centredRight, false);
    g.setColour (reduction > 0.0f ? col::peach : col::dusk);
    g.drawText ("GR", juce::Rectangle<float> (0.0f, 23.4f, x0 - 5.0f, 10.0f), juce::Justification::centredRight, false);
}

//==============================================================================
Header::Header (SwoonAudioProcessor& p)
    : presets (p.getPresetManager()),
      meters (p.getVizData()),
      bypass (p.getParam (P::bypass), col::rose, true)
{
    addAndMakeVisible (meters);
    addAndMakeVisible (bypass);

    modified = presets.isModified();
    presets.addChangeListener (this);
}

Header::~Header()
{
    presets.removeChangeListener (this);
}

void Header::changeListenerCallback (juce::ChangeBroadcaster*)
{
    modified = presets.isModified();
    repaint();

    if (hover != Zone::none)
        postHoverHelp();
}

juce::String Header::positionText() const
{
    const int index = presets.getCurrentIndex(), total = (int) presets.getEntries().size();
    return index >= 0 && index < total ? juce::String (index + 1) + " / " + juce::String (total) : juce::String();
}

void Header::postHoverHelp()
{
    switch (hover)
    {
        case Zone::tabSleeve:
            postHelp (*this, "Sleeve", "The front page: six macros, the signal path and the haze, which moves with the audio.");
            break;
        case Zone::tabMachine:
            postHelp (*this, "Machine", "Every parameter of every module.");
            break;
        case Zone::previous:
            postHelp (*this, "Previous preset", "Steps back through the factory and user presets.", positionText());
            break;
        case Zone::next:
            postHelp (*this, "Next preset", "Steps forward through the factory and user presets.", positionText());
            break;
        case Zone::name:
            postHelp (*this, "Presets", modified ? "Click to browse, search and save. The dot means this sound has been edited since it was loaded."
                                                 : "Click to browse, search and save.", positionText());
            break;
        case Zone::none:
            clearHelp (*this);
            break;
    }
}

void Header::setPage (int newPage)
{
    if (page != newPage)
    {
        page = newPage;
        repaint();
    }
}

void Header::setBrowserOpen (bool isOpen)
{
    if (browserOpen != isOpen)
    {
        browserOpen = isOpen;
        repaint();
    }
}

void Header::tick()
{
    meters.tick();

    const bool nowModified = presets.isModified();
    if (nowModified != modified)
    {
        modified = nowModified;
        repaint (presetBounds.getSmallestIntegerContainer());
    }
}

void Header::resized()
{
    const float h = (float) getHeight();

    tabBounds[0] = { 148.0f, 0.0f, 74.0f, h };
    tabBounds[1] = { 224.0f, 0.0f, 84.0f, h };
    presetBounds = juce::Rectangle<float> (300.0f, 32.0f).withCentre ({ (float) getWidth() * 0.5f + 20.0f, h * 0.5f });

    bypass.setBounds (getWidth() - 58, (getHeight() - 38) / 2, 38, 38);
    meters.setBounds (getWidth() - 236, (getHeight() - 33) / 2, 158, 33);
}

Header::Zone Header::zoneAt (juce::Point<float> p) const
{
    if (tabBounds[0].contains (p)) return Zone::tabSleeve;
    if (tabBounds[1].contains (p)) return Zone::tabMachine;

    if (presetBounds.contains (p))
    {
        if (p.x < presetBounds.getX() + 34.0f)     return Zone::previous;
        if (p.x > presetBounds.getRight() - 34.0f) return Zone::next;
        return Zone::name;
    }

    return Zone::none;
}

void Header::setHover (Zone zone)
{
    if (hover != zone)
    {
        hover = zone;
        setMouseCursor (zone == Zone::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        postHoverHelp();
        repaint();
    }
}

void Header::mouseMove (const juce::MouseEvent& e) { setHover (zoneAt (e.position)); }
void Header::mouseExit (const juce::MouseEvent&)   { setHover (Zone::none); }

void Header::mouseDown (const juce::MouseEvent& e)
{
    pressed = e.mods.isPopupMenu() ? Zone::none : zoneAt (e.position);
    repaint();
}

void Header::mouseUp (const juce::MouseEvent& e)
{
    const auto zone = pressed;
    pressed = Zone::none;
    repaint();

    if (zone == Zone::none || zone != zoneAt (e.position))
        return;

    switch (zone)
    {
        case Zone::tabSleeve:   if (onPageSelected != nullptr) onPageSelected (0); break;
        case Zone::tabMachine:  if (onPageSelected != nullptr) onPageSelected (1); break;
        case Zone::previous:    presets.loadPrevious(); break;
        case Zone::next:        presets.loadNext(); break;
        case Zone::name:        if (onPresetNameClicked != nullptr) onPresetNameClicked(); break;
        case Zone::none:        break;
    }
}

//==============================================================================
void Header::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float h = b.getHeight();

    g.setGradientFill (juce::ColourGradient (col::ink, 0.0f, 0.0f, col::night, 0.0f, h, false));
    g.fillRect (b);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (0.0f, h - 1.0f, b.getWidth(), 1.0f);

    // wordmark
    {
        const juce::Rectangle<float> area (22.0f, 0.0f, 120.0f, h - 2.0f);
        fillGlow (g, { 70.0f, h * 0.5f }, 78.0f, col::lavender.withAlpha (0.16f));
        g.setGradientFill (juce::ColourGradient (col::fog, area.getX(), 0.0f, col::rose.interpolatedWith (col::fog, 0.35f), area.getX() + 96.0f, 0.0f, false));
        g.setFont (serifFont (38.0f));
        g.drawText ("Swoon", area, juce::Justification::centredLeft, false);
    }

    // page switch
    {
        static constexpr const char* names[] = { "SLEEVE", "MACHINE" };
        const auto font = sansFont (11.2f, 1, 0.22f);

        for (int i = 0; i < 2; ++i)
        {
            const bool current = page == i;
            const bool hovered = hover == (i == 0 ? Zone::tabSleeve : Zone::tabMachine);
            const auto area = tabBounds[i];

            g.setColour (current ? col::fog : hovered ? col::mist : col::dusk);
            g.setFont (font);
            g.drawText (names[i], area.translated (1.2f, 0.0f), juce::Justification::centred, false);

            if (current)
            {
                const float w = textWidth (font, names[i]) - 2.0f;
                const juce::Rectangle<float> line (area.getCentreX() - w * 0.5f, h * 0.5f + 11.0f, w, 1.6f);
                fillGlow (g, line.getCentre(), w * 0.7f, col::rose.withAlpha (0.16f));
                g.setGradientFill (juce::ColourGradient (col::rose, line.getX(), 0.0f, col::lavender, line.getRight(), 0.0f, false));
                g.fillRoundedRectangle (line, 0.8f);
            }
        }
    }

    // preset bar
    {
        const auto pb = presetBounds;
        const float r = pb.getHeight() * 0.5f;

        g.setColour (col::panel.withAlpha (browserOpen ? 1.0f : 0.8f));
        g.fillRoundedRectangle (pb, r);
        g.setColour (browserOpen ? col::lavender.withAlpha (0.6f)
                                 : juce::Colours::white.withAlpha (hover == Zone::name ? 0.2f : 0.1f));
        g.drawRoundedRectangle (pb.reduced (0.5f), r, 1.0f);

        const auto chevron = [&] (float cx, bool left, Zone zone)
        {
            const float s = 3.6f, dir = left ? -1.0f : 1.0f;
            juce::Path p;
            p.startNewSubPath (cx - dir * s * 0.5f, pb.getCentreY() - s);
            p.lineTo (cx + dir * s * 0.5f, pb.getCentreY());
            p.lineTo (cx - dir * s * 0.5f, pb.getCentreY() + s);

            if (hover == zone)
            {
                g.setColour (juce::Colours::white.withAlpha (pressed == zone ? 0.14f : 0.08f));
                g.fillEllipse (cx - 11.0f, pb.getCentreY() - 11.0f, 22.0f, 22.0f);
            }

            g.setColour (hover == zone ? col::fog : col::mist);
            g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };

        chevron (pb.getX() + 17.0f, true, Zone::previous);
        chevron (pb.getRight() - 17.0f, false, Zone::next);

        auto name = presets.getCurrentName();
        if (name.isEmpty())
            name = "Untitled";

        auto font = sansFont (13.2f, 1, 0.03f);
        const float maxW = pb.getWidth() - 96.0f;
        const float nameW = textWidth (font, name);
        if (nameW > maxW)
            font = font.withHorizontalScale (juce::jmax (0.75f, maxW / nameW));

        const float shownW = juce::jmin (maxW, nameW);
        const juce::Rectangle<float> nameArea (pb.getCentreX() - shownW * 0.5f, pb.getY(), shownW + 1.0f, pb.getHeight() - 1.0f);

        g.setColour (hover == Zone::name || browserOpen ? col::fog : col::fog.withAlpha (0.9f));
        g.setFont (font);
        g.drawText (name, nameArea, juce::Justification::centredLeft, true);

        if (modified)
        {
            // edited since it was loaded
            const juce::Point<float> dot (nameArea.getRight() + 7.0f, pb.getCentreY());
            fillGlow (g, dot, 8.0f, col::rose.withAlpha (0.5f));
            g.setColour (col::rose);
            g.fillEllipse (dot.x - 2.3f, dot.y - 2.3f, 4.6f, 4.6f);
        }

        // position in the library, tucked in before the next arrow when the name leaves room
        const auto position = positionText();
        if (position.isNotEmpty())
        {
            const auto positionFont = sansFont (9.8f, 0, 0.06f);
            const float w = textWidth (positionFont, position) + 2.0f;
            const float x = pb.getRight() - 34.0f - w;

            if (x > nameArea.getRight() + (modified ? 22.0f : 12.0f))
            {
                g.setColour (col::dusk);
                g.setFont (positionFont);
                g.drawText (position, juce::Rectangle<float> (x, pb.getY(), w, pb.getHeight() - 1.0f), juce::Justification::centredRight, false);
            }
        }
    }

    // divider before the meters
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRect ((float) meters.getX() - 16.0f, h * 0.5f - 11.0f, 1.0f, 22.0f);
}

} // namespace swoon::ui
