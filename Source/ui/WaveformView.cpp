#include "WaveformView.h"
#include "StepGrid.h"

using namespace juce;
using namespace splinter;

WaveformView::WaveformView (SplinterProcessor& p) : proc (p)
{
    for (auto& v : voicePos) v = -1.0f;
    setMouseCursor (MouseCursor::NormalCursor);
}

Rectangle<float> WaveformView::waveArea() const { return getLocalBounds().toFloat().reduced (10.0f, 0.0f).withTrimmedTop (22.0f).withTrimmedBottom (20.0f); }
double WaveformView::loopBeats() const { return live ? liveWindowBeats : jmax (1.0, (double) slices.loopBeats); }
float WaveformView::beatToX (double beat) const { const auto a = waveArea(); return a.getX() + a.getWidth() * (float) (beat / loopBeats()); }
double WaveformView::xToBeat (float x) const { const auto a = waveArea(); return jlimit (0.0, loopBeats(), (double) ((x - a.getX()) / a.getWidth()) * loopBeats()); }

void WaveformView::refreshLivePeaks()
{
    // Reads the capture ring while the audio thread writes it: harmless for a display.
    const auto& ring = proc.engine.getLiveRing();
    const double bpm = proc.engine.visual.bpm.load();
    const double spb = proc.getSampleRate() > 0 ? proc.getSampleRate() * 60.0 / jmax (20.0, bpm) : 24000.0;
    const int64_t end = ring.written();
    const int64_t len = jmin<int64_t> ((int64_t) (liveWindowBeats * spb), ring.capacity() - 4096);
    const int cols = 512;
    peaks.assign ((size_t) cols * 2, 0.0f);
    if (end <= 0 || len <= cols) return;
    const int64_t stride = jmax<int64_t> (1, len / (cols * 48));
    for (int c = 0; c < cols; ++c)
    {
        const int64_t a = end - len + len * c / cols, b = end - len + len * (c + 1) / cols;
        float lo = 0.0f, hi = 0.0f;
        for (int64_t i = a; i < b; i += stride)
        {
            if (i < 0) continue;
            const float v = 0.5f * (ring.sampleAbs (0, i) + ring.sampleAbs (1, i));
            lo = jmin (lo, v); hi = jmax (hi, v);
        }
        peaks[(size_t) c * 2] = lo; peaks[(size_t) c * 2 + 1] = hi;
    }
}

void WaveformView::tick()
{
    auto& vis = proc.engine.visual;
    const bool nowLive = proc.getParam (pid::sourceMode) > 0.5f;
    bool changed = nowLive != live;
    live = nowLive;
    liveWindowBeats = 4.0 * jlimit (1, 8, (int) proc.getParam (pid::liveBars));

    if (proc.getBankVersion() != bankVersion)
    {
        bankVersion = proc.getBankVersion();
        if (dragMarker < 0) slices = proc.getBank().slices;
        changed = true;
    }
    const LoopInfo ni = proc.loops.getInfo();
    if (ni.generation != info.generation || ni.loading != info.loading || ni.error != info.error) changed = true;
    info = ni;
    if (live)
    {
        if ((liveCounter++ % 3) == 0) { refreshLivePeaks(); changed = true; }
        peaksGeneration = 0xffffffff;
    }
    else if (peaksGeneration != info.generation)
    {
        peaksGeneration = info.generation;
        peaks = proc.loops.getPeaks();
        changed = true;
    }

    const float np = vis.loopPos.load (std::memory_order_relaxed), ng = vis.loopGain.load (std::memory_order_relaxed);
    if (np != playPos || ng != loopGain) changed = true;
    playPos = np; loopGain = ng; playRate = vis.playRate.load (std::memory_order_relaxed);
    for (int i = 0; i < kMaxSliceVoices; ++i)
    {
        const float v = vis.voicePos[i].load (std::memory_order_relaxed);
        if (v != voicePos[i]) changed = true;
        voicePos[i] = v;
    }
    for (int l = 0; l < kNumLanes; ++l)
    {
        const float lvl = vis.laneLevel[l].load (std::memory_order_relaxed);
        const float g = lvl > 0.5f ? 1.0f : laneGlow[l] * 0.82f;
        if (std::fabs (g - laneGlow[l]) > 0.005f) changed = true;
        laneGlow[l] = g < 0.01f ? 0.0f : g;
        laneVar[l] = vis.laneVar[l].load (std::memory_order_relaxed);
    }
    if (changed) repaint();
}

void WaveformView::paint (Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    theme::drawPanel (g, full.reduced (0.5f), theme::panel, 12.0f);
    const auto a = waveArea();
    const double lb = loopBeats();
    const bool haveAudio = live || (info.kind != LoopInfo::none && ! peaks.empty());

    // which slice is sounding
    const double headBeat = live ? jmax (0.0, lb - (double) playPos) : (double) playPos;
    const int activeSlice = (! live && loopGain > 0.01f && slices.count > 0) ? slices.sliceAt (headBeat) : -1;

    // slice backgrounds
    if (! live)
        for (int i = 0; i < slices.count; ++i)
        {
            const float x0 = beatToX (slices.startBeat[i]), x1 = beatToX (slices.sliceEnd (i));
            Rectangle<float> r (x0, a.getY(), x1 - x0, a.getHeight());
            bool voiced = false;
            for (float v : voicePos) if (v >= 0.0f && slices.sliceAt (v) == i) voiced = true;
            if (i == activeSlice) { g.setColour (theme::lime.withAlpha (0.13f)); g.fillRect (r); }
            else if (voiced) { g.setColour (theme::pink.withAlpha (0.16f)); g.fillRect (r); }
            else if (i & 1) { g.setColour (Colours::white.withAlpha (0.018f)); g.fillRect (r); }
        }

    // beat grid
    for (int b = 0; b <= (int) lb; ++b)
    {
        const float x = beatToX (b);
        g.setColour (b % 4 == 0 ? theme::gridStrong : theme::grid);
        g.drawVerticalLine (roundToInt (x), a.getY(), a.getBottom());
        if (b % 4 == 0 && b < (int) lb)
            theme::drawLabel (g, String (b / 4 + 1), Rectangle<int> (roundToInt (x) + 3, roundToInt (a.getBottom()) + 2, 30, 14), theme::textFaint, 9.5f, Justification::centredLeft);
    }
    g.setColour (theme::grid); g.drawHorizontalLine (roundToInt (a.getCentreY()), a.getX(), a.getRight());

    // waveform
    if (haveAudio && peaks.size() >= 4)
    {
        const int cols = (int) peaks.size() / 2;
        const float mid = a.getCentreY(), half = a.getHeight() * 0.47f;
        Path wave;
        const int px = jmax (2, (int) a.getWidth());
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < px; ++i)
            {
                const int k = pass == 0 ? i : px - 1 - i;
                const int c0 = jlimit (0, cols - 1, k * cols / px), c1 = jlimit (c0, cols - 1, (k + 1) * cols / px);
                float v = 0.0f;
                for (int c = c0; c <= c1; ++c) v = pass == 0 ? jmax (v, peaks[(size_t) c * 2 + 1]) : jmin (v, peaks[(size_t) c * 2]);
                const float y = mid - jlimit (-1.0f, 1.0f, v * 1.25f) * half;
                const float x = a.getX() + (float) k;
                if (pass == 0 && i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
            }
        wave.closeSubPath();
        g.setGradientFill (ColourGradient (theme::lime.withAlpha (0.85f), 0.0f, a.getY(), theme::cyan.withAlpha (0.75f), 0.0f, a.getBottom(), false));
        g.fillPath (wave);
        // light the playing slice a touch more
        if (activeSlice >= 0)
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (Rectangle<float> (beatToX (slices.startBeat[activeSlice]), a.getY(), beatToX (slices.sliceEnd (activeSlice)) - beatToX (slices.startBeat[activeSlice]), a.getHeight()).toNearestInt());
            g.setColour (Colours::white.withAlpha (0.35f)); g.fillPath (wave);
        }
    }
    else
    {
        g.setColour (theme::textDim); g.setFont (theme::font (15.0f, true));
        g.drawText (info.loading ? "Analysing loop..." : "Drop a loop here  (WAV, AIFF, FLAC, MP3)", a.toNearestInt(), Justification::centred, false);
    }

    // slice markers
    if (! live)
        for (int i = 0; i < slices.count; ++i)
        {
            const float x = beatToX (slices.startBeat[i]);
            const bool hot = i == hoverMarker || i == dragMarker;
            g.setColour (hot ? theme::white : theme::pink.withAlpha (i == 0 ? 0.35f : 0.8f));
            g.drawVerticalLine (roundToInt (x), a.getY() - 6.0f, a.getBottom());
            Path flag; flag.addTriangle (x, a.getY() - 8.0f, x + (hot ? 10.0f : 8.0f), a.getY() - 8.0f, x, a.getY() + (hot ? 4.0f : 2.0f));
            g.fillPath (flag);
            if (slices.count <= 24 || hot)
                theme::drawLabel (g, String (i + 1), Rectangle<int> (roundToInt (x) + 9, roundToInt (a.getY()) - 11, 22, 11), hot ? theme::white : theme::textFaint, 8.5f, Justification::centredLeft);
        }

    // playheads
    if (loopGain > 0.01f)
    {
        const float x = live ? beatToX (jmax (0.0, lb - (double) playPos)) : beatToX (playPos);
        const Colour c = playRate < -0.05f ? theme::pink : (std::fabs (playRate - 1.0f) > 0.03f ? theme::amber : theme::white);
        g.setColour (c.withAlpha (0.25f)); g.fillRect (Rectangle<float> (x - 3.0f, a.getY(), 6.0f, a.getHeight()));
        g.setColour (c); g.fillRect (Rectangle<float> (x - 0.75f, a.getY() - 2.0f, 1.5f, a.getHeight() + 4.0f));
    }
    for (float v : voicePos)
        if (v >= 0.0f)
        {
            const float x = live ? beatToX (jmax (0.0, lb - (double) v)) : beatToX (v);
            g.setColour (theme::pink); g.fillRect (Rectangle<float> (x - 1.0f, a.getY(), 2.0f, a.getHeight()));
        }

    // glitch chips: one per lane that is firing right now
    {
        float x = full.getRight() - 12.0f;
        for (int l = kNumLanes - 1; l >= 0; --l)
        {
            if (laneGlow[l] <= 0.02f) continue;
            String t = String (laneName (l)).toUpperCase();
            const String vt = laneVarText (l, laneVar[l]);
            if (vt.isNotEmpty() && laneGlow[l] > 0.9f) t << " " << vt;
            const float w = 14.0f + 6.3f * (float) t.length();
            x -= w + 4.0f;
            const Rectangle<float> r (x, 4.0f, w, 15.0f);
            const Colour c = theme::laneColour (l);
            g.setColour (c.withAlpha (0.9f * laneGlow[l])); g.fillPath (theme::chamfer (r, 4.0f));
            g.setColour (theme::bg.withAlpha (laneGlow[l])); g.setFont (theme::font (9.5f, true));
            g.drawText (t, r.toNearestInt(), Justification::centred, false);
        }
    }

    // header text
    String head;
    if (live) head = "LIVE INPUT   last " + String ((int) (lb / 4.0)) + " bars";
    else if (info.kind != LoopInfo::none)
        head = info.name.toUpperCase() + "   " + String (info.sourceBpm, 1) + " BPM   " + String (info.beats / 4.0, info.beats / 4.0 == std::floor (info.beats / 4.0) ? 0 : 2) + " bars   "
             + String (slices.count) + " slices";
    if (info.error.isNotEmpty() && ! live) head = info.error;
    theme::drawLabel (g, head, Rectangle<int> (14, 4, getWidth() / 2, 15), info.error.isNotEmpty() && ! live ? theme::red : theme::textDim, 10.5f, Justification::centredLeft);

    if (dragOver)
    {
        g.setColour (theme::lime.withAlpha (0.12f)); g.fillPath (theme::chamfer (full.reduced (2.0f), 12.0f));
        g.setColour (theme::lime); g.strokePath (theme::chamfer (full.reduced (2.0f), 12.0f), PathStrokeType (2.0f));
    }
}

int WaveformView::markerAt (Point<float> p) const
{
    if (live) return -1;
    const auto a = waveArea();
    for (int i = 1; i < slices.count; ++i)
    {
        const float x = beatToX (slices.startBeat[i]);
        if (std::fabs (p.x - x) < 5.0f || (p.y < a.getY() + 4.0f && p.x >= x - 2.0f && p.x < x + 11.0f)) return i;
    }
    return -1;
}

void WaveformView::commitSlices()
{
    const SliceSet s = slices;
    proc.editBank ([&] (PatternBank& b) { b.slices = s; b.sliceMode = 5; }, SplinterProcessor::slicesBit);
}

void WaveformView::mouseMove (const MouseEvent& e)
{
    const int m = markerAt (e.position);
    if (m != hoverMarker) { hoverMarker = m; repaint(); }
    setMouseCursor (m >= 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
}

void WaveformView::mouseDown (const MouseEvent& e)
{
    if (live) return;
    const int m = markerAt (e.position);
    if (e.mods.isPopupMenu())
    {
        PopupMenu menu;
        if (m > 0) menu.addItem (1, "Remove marker " + String (m + 1));
        menu.addItem (2, "Add marker here", slices.count < kMaxSlices);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe = Component::SafePointer<WaveformView> (this), m, beat = xToBeat (e.position.x)] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& slices = safe->slices;
            if (r == 1 && m > 0 && m < slices.count)
            {
                for (int i = m; i + 1 < slices.count; ++i) slices.startBeat[i] = slices.startBeat[i + 1];
                --slices.count; safe->commitSlices();
            }
            else if (r == 2 && slices.count < kMaxSlices)
            {
                int at = slices.count;
                for (int i = 0; i < slices.count; ++i) if (slices.startBeat[i] > beat) { at = i; break; }
                for (int i = slices.count; i > at; --i) slices.startBeat[i] = slices.startBeat[i - 1];
                slices.startBeat[at] = (float) beat; ++slices.count; safe->commitSlices();
            }
            safe->repaint();
        });
        return;
    }
    dragMarker = m;
    if (m < 0 && waveArea().contains (e.position) && slices.count > 0)
    {
        // click a slice to audition it
        const int s = slices.sliceAt (xToBeat (e.position.x));
        proc.keyboardState.noteOn (1, 36 + s, 0.9f);
        auditionNote = 36 + s;
    }
}

void WaveformView::mouseDrag (const MouseEvent& e)
{
    if (dragMarker <= 0 || dragMarker >= slices.count) return;
    double beat = xToBeat (e.position.x);
    if (! e.mods.isShiftDown())
    {
        const double snapped = std::round (beat * 4.0) / 4.0;          // magnetic 1/16 grid
        if (std::fabs (beatToX (snapped) - beatToX (beat)) < 6.0f) beat = snapped;
    }
    const double lo = slices.startBeat[dragMarker - 1] + 0.02, hi = (dragMarker + 1 < slices.count ? slices.startBeat[dragMarker + 1] : loopBeats()) - 0.02;
    slices.startBeat[dragMarker] = (float) jlimit (lo, jmax (lo, hi), beat);
    commitSlices(); repaint();
}

void WaveformView::mouseUp (const MouseEvent&)
{
    dragMarker = -1;
    if (auditionNote >= 0) { proc.keyboardState.noteOff (1, auditionNote, 0.0f); auditionNote = -1; }
}

void WaveformView::mouseDoubleClick (const MouseEvent& e)
{
    if (live) return;
    const int m = markerAt (e.position);
    if (m > 0)
    {
        for (int i = m; i + 1 < slices.count; ++i) slices.startBeat[i] = slices.startBeat[i + 1];
        --slices.count;
    }
    else if (slices.count < kMaxSlices && waveArea().contains (e.position))
    {
        const double beat = xToBeat (e.position.x);
        int at = slices.count;
        for (int i = 0; i < slices.count; ++i) if (slices.startBeat[i] > beat) { at = i; break; }
        for (int i = slices.count; i > at; --i) slices.startBeat[i] = slices.startBeat[i - 1];
        slices.startBeat[at] = (float) beat; ++slices.count;
    }
    commitSlices(); repaint();
}

bool WaveformView::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (LoopManager::canLoad (File (f))) return true;
    return false;
}

void WaveformView::filesDropped (const StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (LoopManager::canLoad (File (f)))
        {
            proc.loops.loadFile (File (f));
            proc.setParam (pid::sourceMode, 0.0f);
            break;
        }
    repaint();
}
