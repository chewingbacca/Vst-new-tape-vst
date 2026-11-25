// FX tab: the selected track's two inserts, the two send buses (two slots each)
// and the master chain (four slots + limiter). Every effect parameter is a
// normal parameter: lockable per step and modulatable.
#include "DetailPanel.h"

namespace ui {

using namespace atm;

namespace {

class FxSlotView : public juce::Component
{
public:
    FxSlotView (Ctx& c, int slot_, juce::Colour accent_) : ctx (c), slot (slot_), accent (accent_)
    {
        picker.ctx = &ctx; picker.colour = accent;
        picker.help = "Effect in this slot";
        picker.items = [] { juce::StringArray a; for (int i = 0; i < Fx::count; ++i) a.add (juce::String (fxName (i)).toUpperCase()); return a; };
        picker.get = [this] { return (int) getFxType (ctx.proj().kit, slot); };
        picker.set = [this] (int v) { ctx.sess.setFxType (slot, v); };
        addAndMakeVisible (picker);
        build();
    }

    void build()
    {
        widgets.clear();
        type = getFxType (ctx.proj().kit, slot);
        if (type == Fx::None) { resized(); return; }
        const int base = fxSlotParamBase (slot);
        for (int i = 0; i <= kFxParams; ++i)
        {
            const ParamRef r = fxSlotIsInsert (slot) ? ParamRef::T (base + i, fxSlotTrack (slot)) : ParamRef::G (base + i);
            auto w = makeParamWidget (ctx, r);
            if (w == nullptr) continue;
            w->setAccent (accent);
            addAndMakeVisible (*w);
            widgets.push_back (std::move (w));
        }
        resized();
    }

    void sync()
    {
        picker.sync();
        if (getFxType (ctx.proj().kit, slot) != type) build();
        for (auto& w : widgets) w->sync();
    }

    void resized() override
    {
        picker.setBounds (8, 18, 124, 26);
        const int cw = 58, ch = 64;
        const int perRow = juce::jmax (1, (getWidth() - 140) / cw);
        for (size_t i = 0; i < widgets.size(); ++i)
            widgets[i]->setBounds (138 + (int) (i % (size_t) perRow) * cw, 4 + (int) (i / (size_t) perRow) * ch, cw, ch);
    }

    void paint (juce::Graphics& g) override
    {
        fillPanel (g, getLocalBounds(), col::panel(), 3.f);
        g.setColour (accent); g.fillRect (6, 5, 3, 9);
        drawLabel (g, title, { 13, 2, 120, 14 }, col::dim(), 9.5f, juce::Justification::centredLeft, true);
        if (type == Fx::None) drawLabel (g, "EMPTY SLOT", getLocalBounds().withTrimmedLeft (140), col::faint(), 10.f);
    }

    juce::String title;

private:
    Ctx& ctx;
    int slot, type = -1;
    juce::Colour accent;
    Picker picker;
    std::vector<std::unique_ptr<ParamWidget>> widgets;
};

class FxPage : public Page
{
public:
    explicit FxPage (Ctx& c) : Page (c)
    {
        static const char* const names[3] = { "TRACK INSERTS", "SEND BUSES", "MASTER" };
        static const char* const helps[3] = { "The selected track's two insert effects (after the glitcher)",
                                               "Send A and B: two effects each; tracks feed them with their Send knobs; Space scales all sends",
                                               "Master chain: four effects, then the soft limiter" };
        for (int i = 0; i < 3; ++i)
        {
            sub[i].text = names[i]; sub[i].help = helps[i]; sub[i].ctx = &ctx;
            sub[i].onClick = [this, i] { ctx.fxPage = i; ++ctx.uiRev; rebuild(); };
            addAndMakeVisible (sub[i]);
        }
    }

    void rebuild() override
    {
        slots.clear();
        extra.reset();
        const juce::Colour tc = col::track (ctx.track);
        auto addSlot = [this] (int slot, juce::Colour c, const juce::String& title)
        {
            auto v = std::make_unique<FxSlotView> (ctx, slot, c);
            v->title = title;
            addAndMakeVisible (*v);
            slots.push_back (std::move (v));
        };
        if (ctx.fxPage == 0)
        {
            addSlot (ctx.track * kInsertsPerTrack, tc, "INSERT A");
            addSlot (ctx.track * kInsertsPerTrack + 1, tc, "INSERT B");
            extra = std::make_unique<ParamGroup> (ctx, "Track " + juce::String (ctx.track + 1) + " sends");
            extra->accent = tc; extra->rows = 2;
            for (int tp : { TP::SendA, TP::SendB, TP::GlitchSens, TP::Level }) extra->add (ParamRef::T (tp));
        }
        else if (ctx.fxPage == 1)
        {
            addSlot (kFirstSendSlot + 0, col::mod(), "SEND A  1");
            addSlot (kFirstSendSlot + 1, col::mod(), "SEND A  2");
            addSlot (kFirstSendSlot + 2, col::lock(), "SEND B  1");
            addSlot (kFirstSendSlot + 3, col::lock(), "SEND B  2");
            extra = std::make_unique<ParamGroup> (ctx, "Returns");
            extra->accent = col::text(); extra->rows = 2;
            for (int gp : { GP::SendAReturn, GP::SendBReturn, GP::Space }) extra->add (ParamRef::G (gp));
        }
        else
        {
            for (int k = 0; k < kMasterSlots; ++k) addSlot (kFirstMasterSlot + k, col::accent(), "MASTER " + juce::String (k + 1));
            extra = std::make_unique<ParamGroup> (ctx, "Limiter");
            extra->accent = col::text(); extra->rows = 2;
            for (int gp : { GP::LimitOn, GP::LimitDrive, GP::Master }) extra->add (ParamRef::G (gp));
        }
        addAndMakeVisible (*extra);
        for (int i = 0; i < 3; ++i) { sub[i].colour = i == 0 ? tc : col::text(); sub[i].setLit (i == ctx.fxPage); }
        resized();
        tick (true);
    }

    void tick (bool) override
    {
        for (auto& s : slots) s->sync();
        if (extra) extra->sync();
    }

    void resized() override
    {
        for (int i = 0; i < 3; ++i) sub[i].setBounds (10 + i * 128, 4, 122, 22);
        auto r = getLocalBounds().withTrimmedTop (32).reduced (8, 0).withTrimmedBottom (6);
        auto side = r.removeFromRight (150);
        if (extra) extra->setBounds (side.withTrimmedLeft (6).withHeight (juce::jmin (side.getHeight(), 150)));
        const int n = (int) slots.size();
        if (n == 0) return;
        if (n <= 2)
        {
            const int h = (r.getHeight() - 6) / 2;
            for (int i = 0; i < n; ++i) slots[(size_t) i]->setBounds (r.getX(), r.getY() + i * (h + 6), r.getWidth(), h);
        }
        else
        {
            const int w = (r.getWidth() - 6) / 2, h = (r.getHeight() - 6) / 2;
            for (int i = 0; i < n; ++i)
                slots[(size_t) i]->setBounds (r.getX() + (i % 2) * (w + 6), r.getY() + (i / 2) * (h + 6), w, h);
        }
    }

    void paint (juce::Graphics& g) override { g.fillAll (col::bg()); }

private:
    Btn sub[3];
    std::vector<std::unique_ptr<FxSlotView>> slots;
    std::unique_ptr<ParamGroup> extra;
};

} // namespace

std::unique_ptr<Page> makeFxPage (Ctx& c) { return std::make_unique<FxPage> (c); }

} // namespace ui
