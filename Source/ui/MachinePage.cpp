#include "MachinePage.h"

#include "../PluginProcessor.h"

#include <cstring>

namespace swoon::ui
{

namespace
{
constexpr int kPanelRows = 3;
constexpr int kGap = 9;

bool isOrderParam (P p) noexcept
{
    for (auto o : kOrderParams)
        if (o == p)
            return true;
    return false;
}

/** Parameters that live on the front page / header rather than in a module panel. */
bool isFrontPageParam (const ParamDef& d) noexcept
{
    static constexpr const char* ids[] = { "wall", "wash", "glide", "blur", "warmth", "mix", "bypass" };
    for (const char* id : ids)
        if (std::strcmp (d.id, id) == 0)
            return true;
    return isOrderParam (d.index);
}

/** Small per-id layout hints; anything not listed gets a label derived from its name. */
juce::String shortLabel (const ParamDef& d)
{
    static constexpr std::pair<const char*, const char*> overrides[] = {
        { "washShimMode", "Interval" },
        { "washPre", "Pre-Delay" },
    };

    for (const auto& [id, text] : overrides)
        if (std::strcmp (d.id, id) == 0)
            return text;

    juce::String name (d.name);
    const auto prefix = juce::String (moduleName (d.module)) + " ";
    if (name.startsWith (prefix) && name.length() > prefix.length())
        name = name.substring (prefix.length());

    return name;
}

bool isBipolar (const ParamDef& d) noexcept
{
    return d.kind == ParamKind::Float && d.min < 0.0f && std::abs (d.min + d.max) < 1.0e-4f && std::abs (d.def) < 1.0e-4f;
}

int slotForModule (Module m) noexcept
{
    for (int slot = 0; slot < kNumSlots; ++slot)
        if (ChainModel::moduleForSlot (slot) == m)
            return slot;
    return -1;
}
} // namespace

//==============================================================================
ModulePanel::ModulePanel (SwoonAudioProcessor& processor, Module m, const std::vector<const ParamDef*>& controlDefs)
    : module (m), title (moduleName (m)), accent (moduleColour (m))
{
    if (m == Module::Global)
    {
        // the global extras are input conditioning today; fall back to "Global" if that ever changes
        bool allInput = ! controlDefs.empty();
        for (const auto* d : controlDefs)
            allInput = allInput && juce::String (d->id).startsWith ("in");

        if (allInput)
            title = "Input";
    }

    if (const auto* powerDef = powerParamFor (m))
    {
        power = std::make_unique<PowerButton> (processor.getParam (powerDef->index), accent);
        power->onValueChange = [this] { updateEnabledLook(); };
        addAndMakeVisible (*power);
    }

    for (const auto* d : controlDefs)
    {
        auto& param = processor.getParam (d->index);
        const auto label = shortLabel (*d);
        std::unique_ptr<ParamControl> control;

        switch (d->kind)
        {
            case ParamKind::Choice:
                control = std::make_unique<ChoiceSelector> (param, label, accent);
                break;
            case ParamKind::Bool:
                control = std::make_unique<Toggle> (param, label, accent);
                break;
            case ParamKind::Float:
            case ParamKind::Int:
            {
                auto knob = std::make_unique<Knob> (param, Knob::Style::small, label, accent);
                knob->setBipolar (isBipolar (*d));
                control = std::move (knob);
                break;
            }
        }

        addAndMakeVisible (*control);
        controls.push_back (std::move (control));
    }

    setTitle (title);
    updateEnabledLook();
}

void ModulePanel::setBadge (const juce::String& text)
{
    if (badge != text)
    {
        badge = text;
        repaint();
    }
}

void ModulePanel::setHighlight (float amount)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    if (! juce::approximatelyEqual (amount, highlight))
    {
        highlight = amount;
        repaint();
    }
}

void ModulePanel::updateEnabledLook()
{
    const bool on = power == nullptr || power->isOn();

    for (auto& c : controls)
        c->setAlpha (on ? 1.0f : 0.38f);

    repaint();
}

void ModulePanel::paint (juce::Graphics& g)
{
    const bool on = power == nullptr || power->isOn();
    const auto b = getLocalBounds().toFloat();

    drawPanel (g, b, 12.0f, accent, on ? 0.045f : 0.0f);

    // a breath of the module's colour in the top-left corner
    if (on)
    {
        g.saveState();
        juce::Path clip;
        clip.addRoundedRectangle (b, 12.0f);
        g.reduceClipRegion (clip);
        fillGlow (g, { b.getX() + 14.0f, b.getY() + 4.0f }, 110.0f, accent.withAlpha (0.10f));
        g.restoreState();
    }

    if (highlight > 0.0f)
    {
        g.setColour (accent.withAlpha (0.10f * highlight));
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (accent.withAlpha (0.9f * highlight));
        g.drawRoundedRectangle (b.reduced (0.75f), 12.0f, 1.5f);
    }

    auto header = b.withHeight ((float) kHeaderH).reduced (11.0f, 0.0f).translated (0.0f, 1.0f);
    if (power != nullptr)
        header.removeFromLeft (20.0f);

    const auto titleFont = sansFont (11.6f, 2, 0.2f);
    const auto titleText = title.toUpperCase();

    // the chain-position badge gives way when a narrow panel needs the room for its name
    if (badge.isNotEmpty() && header.getWidth() - textWidth (titleFont, titleText) > 40.0f)
    {
        g.setColour (col::dusk);
        g.setFont (sansFont (10.0f, 1, 0.16f));
        g.drawText (badge, header.removeFromRight (34.0f), juce::Justification::centredRight, false);
    }

    g.setColour (on ? accent.interpolatedWith (col::fog, 0.3f) : col::dusk);
    drawFitted (g, titleText, header, juce::Justification::centredLeft, titleFont, 0.7f);
}

void ModulePanel::resized()
{
    if (power != nullptr)
        power->setBounds (4, (kHeaderH - 26) / 2 + 1, 26, 26);

    const auto content = getLocalBounds().withTrimmedTop (kHeaderH).withTrimmedBottom (5).reduced (kPadX, 0).toFloat();
    const int columns = getNumColumns();
    const float cw = content.getWidth() / (float) columns;
    const float ch = content.getHeight() / (float) kRows;

    for (size_t i = 0; i < controls.size(); ++i)
    {
        const int column = (int) i % columns, row = (int) i / columns;
        controls[i]->setBounds (juce::Rectangle<float> (content.getX() + (float) column * cw, content.getY() + (float) row * ch, cw, ch)
                                    .toNearestInt());
    }
}

//==============================================================================
MachinePage::MachinePage (SwoonAudioProcessor& processor, ChainModel& m) : model (m)
{
    static constexpr Module displayOrder[] = { Module::Glide, Module::Fuzz, Module::Amp, Module::Mod, Module::Focus,
                                               Module::Reverse, Module::Wash, Module::Tape, Module::Output, Module::Global };

    for (auto module : displayOrder)
    {
        const auto* powerDef = powerParamFor (module);
        std::vector<const ParamDef*> defs;

        for (const auto& d : paramDefs())
            if (d.module == module && &d != powerDef && ! isFrontPageParam (d))
                defs.push_back (&d);

        if (defs.empty() && powerDef == nullptr)
            continue;

        auto panel = std::make_unique<ModulePanel> (processor, module, defs);
        addAndMakeVisible (*panel);
        panels.push_back (std::move (panel));
    }

    model.addChangeListener (this);
    updateBadges();
}

MachinePage::~MachinePage()
{
    stopTimer();
    model.removeChangeListener (this);
}

void MachinePage::spotlight (Module module)
{
    for (auto& panel : panels)
        panel->setHighlight (panel->getModule() == module ? 1.0f : 0.0f);

    startTimerHz (30);
}

void MachinePage::timerCallback()
{
    bool anyLit = false;

    for (auto& panel : panels)
    {
        const float h = panel->getHighlight();
        if (h > 0.0f)
        {
            panel->setHighlight (h < 0.03f ? 0.0f : h * 0.93f);
            anyLit = anyLit || panel->getHighlight() > 0.0f;
        }
    }

    if (! anyLit)
        stopTimer();
}

void MachinePage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateBadges();
}

void MachinePage::updateBadges()
{
    for (auto& panel : panels)
    {
        const auto module = panel->getModule();
        const int slot = slotForModule (module);

        if (slot >= 0)
            panel->setBadge (juce::String (model.positionOf (slot) + 1) + " / " + juce::String (kNumSlots));
        else
            panel->setBadge (module == Module::Tape ? "POST" : module == Module::Output ? "OUT" : "PRE");
    }
}

void MachinePage::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (col::night, 0.0f, 0.0f, col::ink, 0.0f, b.getHeight(), false));
    g.fillRect (b);

    // still haze behind the panels
    fillGlow (g, { b.getWidth() * 0.16f, b.getHeight() * 0.1f }, 420.0f, col::lavender.withAlpha (0.09f));
    fillGlow (g, { b.getWidth() * 0.88f, b.getHeight() * 0.45f }, 380.0f, col::rose.withAlpha (0.07f));
    fillGlow (g, { b.getWidth() * 0.4f, b.getHeight() * 1.0f }, 440.0f, col::seaglass.withAlpha (0.06f));
}

void MachinePage::resized()
{
    const int n = (int) panels.size();
    if (n == 0)
        return;

    // Split the panels (kept in order) into rows so the widest row is as narrow as possible.
    std::vector<float> weight ((size_t) n);
    for (int i = 0; i < n; ++i)
        weight[(size_t) i] = (float) panels[(size_t) i]->getNumColumns() + 0.3f;

    const int rows = juce::jmin (kPanelRows, n);
    std::array<int, kPanelRows + 1> split { 0, n, n, n };

    if (rows == 2)
    {
        float best = 1.0e9f;
        for (int i = 1; i < n; ++i)
        {
            float a = 0.0f, c = 0.0f;
            for (int k = 0; k < n; ++k)
                (k < i ? a : c) += weight[(size_t) k];

            const float cost = juce::jmax (a, c);
            if (cost < best) { best = cost; split = { 0, i, n, n }; }
        }
    }
    else if (rows == 3)
    {
        float best = 1.0e9f;
        for (int i = 1; i < n - 1; ++i)
        {
            for (int j = i + 1; j < n; ++j)
            {
                float a = 0.0f, c = 0.0f, e = 0.0f;
                for (int k = 0; k < n; ++k)
                    (k < i ? a : k < j ? c : e) += weight[(size_t) k];

                const float hi = juce::jmax (a, c, e), lo = juce::jmin (a, c, e);
                const float cost = hi * 100.0f + (hi - lo);
                if (cost < best) { best = cost; split = { 0, i, j, n }; }
            }
        }
    }

    const auto area = getLocalBounds().reduced (12, 0).withTrimmedTop (10).withTrimmedBottom (8);
    const float rowH = ((float) area.getHeight() - (float) (kGap * (kPanelRows - 1))) / (float) kPanelRows;

    for (int row = 0; row < rows; ++row)
    {
        const int first = split[(size_t) row], last = split[(size_t) row + 1];
        const int count = last - first;
        if (count <= 0)
            continue;

        int columns = 0;
        for (int k = first; k < last; ++k)
            columns += panels[(size_t) k]->getNumColumns();

        const float chrome = (float) (count * ModulePanel::kPadX * 2 + (count - 1) * kGap);
        const float cellW = ((float) area.getWidth() - chrome) / (float) columns;
        const float y = (float) area.getY() + (float) row * (rowH + (float) kGap);
        float x = (float) area.getX();

        for (int k = first; k < last; ++k)
        {
            const float w = (float) panels[(size_t) k]->getNumColumns() * cellW + (float) (ModulePanel::kPadX * 2);
            const int x0 = juce::roundToInt (x), x1 = juce::roundToInt (x + w);
            panels[(size_t) k]->setBounds (x0, juce::roundToInt (y), x1 - x0, juce::roundToInt (y + rowH) - juce::roundToInt (y));
            x += w + (float) kGap;
        }
    }
}

} // namespace swoon::ui
