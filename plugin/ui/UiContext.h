// Shared UI state and the single place where widgets read and write parameters.
#pragma once
#include "Theme.h"
#include "../PluginProcessor.h"

namespace ui {

struct ParamRef
{
    bool global = false;
    int  track = 0;         // for track params; -1 = the selected track
    int  id = 0;            // TP::* or GP::*
    static ParamRef T (int tp, int track = -1) { return { false, track, tp }; }
    static ParamRef G (int gp) { return { true, 0, gp }; }
    bool operator== (const ParamRef& o) const { return global == o.global && track == o.track && id == o.id; }
};

enum Tab { TabSound, TabSteps, TabSeq, TabMod, TabFx, TabSong, numTabs };

class Ctx
{
public:
    explicit Ctx (AutomataProcessor& p) : proc (p), sess (p.session()), eng (p.engine()) {}

    AutomataProcessor& proc;
    atm::Session& sess;
    atm::AudioEngine& eng;

    // ---- selection
    int  track = 0;
    int  stepA = -1, stepB = -1;      // selected step range on the selected track (inclusive); -1 = none
    bool lockMode = false;            // knob edits write parameter locks onto the selected steps
    int  tab = TabSound;
    int  fxPage = 0;                  // 0 track inserts, 1 sends, 2 master
    uint32_t uiRev = 1;               // bumped when selection / tab changes

    const atm::Project& proj() const { return sess.project(); }
    int pattern() const { return sess.editPattern(); }
    const atm::TrackPattern& tp (int t) const { return proj().patterns[pattern()].tracks[t]; }
    const atm::TrackPattern& tp() const { return tp (track); }
    const atm::TrackSound& ts (int t) const { return proj().kit.tracks[t]; }
    const atm::TrackSound& ts() const { return ts (track); }
    bool hasStepSelection() const { return stepA >= 0; }
    int selLo() const { return juce::jmin (stepA, stepB); }
    int selHi() const { return juce::jmax (stepA, stepB); }
    bool stepSelected (int t, int i) const { return t == track && stepA >= 0 && i >= selLo() && i <= selHi(); }

    void selectTrack (int t)
    {
        t = juce::jlimit (0, atm::kTracks - 1, t);
        if (t == track) return;
        track = t; stepA = stepB = -1; lockMode = false; ++uiRev;
        sess.setSelectedTrack (t);
    }
    void selectSteps (int a, int b, bool lock)
    {
        stepA = a; stepB = b; lockMode = lock && a >= 0; ++uiRev;
    }
    void clearStepSelection() { stepA = stepB = -1; lockMode = false; ++uiRev; }
    void setTab (int t) { tab = t; ++uiRev; }

    // ---- parameters
    int resolveTrack (const ParamRef& r) const { return r.track < 0 ? track : r.track; }
    const atm::ParamDesc& desc (const ParamRef& r) const
    {
        if (r.global) return atm::globalParamDesc (r.id, proj().kit.fxType);
        return atm::trackParamDesc (r.id, ts (resolveTrack (r)).ctx());
    }
    int destOf (const ParamRef& r) const { return r.global ? atm::kDestGlobalBase + r.id : r.id; }
    float base (const ParamRef& r) const
    {
        return r.global ? proj().kit.gparams[r.id] : ts (resolveTrack (r)).params[r.id];
    }
    // Is this control currently editing a lock (lock mode on, and the param lives on / is lockable from the selected track)?
    bool editsLock (const ParamRef& r) const { return lockMode && hasStepSelection() && (r.global || resolveTrack (r) == track); }
    bool lockAt (const ParamRef& r, int step, float& v) const { return tp().getLock (step, destOf (r), v); }
    bool hasAnyLock (const ParamRef& r) const
    {
        if (! r.global && resolveTrack (r) != track) return false;
        const int l = tp().findLane (destOf (r));
        return l >= 0 && tp().lanes[l].mask != 0;
    }
    // The value a control shows: the lock value of the first selected step when editing locks, else the base value.
    float shown (const ParamRef& r) const
    {
        float v;
        if (editsLock (r) && lockAt (r, selLo(), v)) return v;
        return base (r);
    }
    bool shownIsLock (const ParamRef& r) const { float v; return editsLock (r) && lockAt (r, selLo(), v); }

    void begin (const ParamRef& r, const char* name = "Parameter")
    {
        sess.beginTransaction (name);
        if (! editsLock (r)) proc.beginGesture (r.global ? -1 : resolveTrack (r), r.id);
    }
    void end (const ParamRef& r)
    {
        if (! editsLock (r)) proc.endGesture (r.global ? -1 : resolveTrack (r), r.id);
        sess.endTransaction();
    }
    void set (const ParamRef& r, float v)
    {
        v = juce::jlimit (0.f, 1.f, v);
        if (editsLock (r))
        {
            atm::TrackPattern p = tp();
            bool ok = true;
            for (int i = selLo(); i <= selHi() && i < p.length; ++i) ok &= p.setLock (i, destOf (r), v);
            if (ok) sess.setTrackPattern (pattern(), track, p);
            else status = "No free lock lane on this track (12 parameters can be locked per pattern)";
            return;
        }
        if (r.global) proc.setGlobalParam (r.id, v);
        else proc.setTrackParam (resolveTrack (r), r.id, v);
    }
    void clearLocks (const ParamRef& r, bool selectedStepsOnly)
    {
        atm::TrackPattern p = tp();
        const int lo = selectedStepsOnly && hasStepSelection() ? selLo() : 0;
        const int hi = selectedStepsOnly && hasStepSelection() ? selHi() : atm::kMaxSteps - 1;
        for (int i = lo; i <= hi; ++i) p.clearLock (i, destOf (r));
        sess.setTrackPattern (pattern(), track, p);
    }
    // Live value including modulation and locks (only meaningful for the selected track and globals).
    float live (const ParamRef& r) const
    {
        if (r.global) return eng.shared.globalModValue[r.id].load (std::memory_order_relaxed);
        if (resolveTrack (r) != track) return base (r);
        return eng.shared.modValue[r.id].load (std::memory_order_relaxed);
    }
    // Sum of |amount| of the mod slots aimed at this parameter (0 = none) and the signed total.
    float modDepth (const ParamRef& r, float* signedSum = nullptr) const
    {
        float a = 0.f, s = 0.f;
        const int dt = r.global ? -1 : resolveTrack (r);
        for (const atm::ModSlot& m : proj().kit.mods)
            if (m.src != atm::Src::None && m.dstTrack == dt && m.dstParam == r.id) { a += std::abs (m.amount); s += m.amount; }
        if (signedSum) *signedSum = s;
        return a;
    }
    juce::String format (const ParamRef& r, float v) const
    {
        char buf[40];
        if (r.global && r.id >= atm::GP::SendFx0)
        {
            const int rel = r.id - atm::GP::SendFx0, slot = atm::kFirstSendSlot + rel / atm::kFxStride, idx = rel % atm::kFxStride;
            if (idx < atm::kFxParams && atm::formatFxParamCustom (proj().kit.fxType[slot], idx, &proj().kit.gparams[atm::fxSlotParamBase (slot)], buf, 40)) return buf;
        }
        else if (! r.global && r.id >= atm::TP::FxA0 && r.id <= atm::TP::FxBMix)
        {
            const int k = r.id >= atm::TP::FxB0 ? 1 : 0, idx = r.id - (k ? (int) atm::TP::FxB0 : (int) atm::TP::FxA0);
            const atm::TrackSound& s = ts (resolveTrack (r));
            if (idx < atm::kFxParams && atm::formatFxParamCustom (s.fxType[k], idx, &s.params[k ? atm::TP::FxB0 : atm::TP::FxA0], buf, 40)) return buf;
        }
        atm::formatParam (desc (r), v, buf, 40);
        return buf;
    }

    // ---- status line (hover help, messages)
    juce::String status;
    juce::String hoverHelp;

    // ---- clipboard
    std::unique_ptr<atm::TrackPattern> clipPattern;
    std::unique_ptr<atm::TrackSound> clipSound;
};

} // namespace ui
