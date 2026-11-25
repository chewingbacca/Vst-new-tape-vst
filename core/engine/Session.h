// The message-thread side of the instrument: owns the canonical Project, the
// undo history and the sample pool, and mirrors every edit to the audio engine
// through its command queue. The UI and the plugin wrapper talk to this class;
// nothing here is ever called from the audio thread.
#pragma once
#include "AudioEngine.h"
#include "SamplePool.h"
#include <deque>
#include <mutex>
#include <string>

namespace atm {

class Session
{
public:
    explicit Session (AudioEngine& engine);
    ~Session();

    AudioEngine& engine() { return eng; }
    const Project& project() const { return proj; }
    SamplePool& samples() { return pool; }
    std::recursive_mutex& mutex() { return lock; }

    // Bumped on every change to the project; the UI polls it to know when to repaint/rebuild.
    uint64_t revision() const { return rev; }
    // Bumped only when the structure changes (project replaced, engine or fx type changed).
    uint64_t structureRevision() const { return structRev; }

    // ---- which pattern the UI edits (independent of what is playing while a switch is queued)
    int editPattern() const { return editPat; }
    void setEditPattern (int p) { editPat = clamp (p, 0, kPatterns - 1); ++rev; }

    // ---- edits (undoable; wrap gestures in a Transaction to merge them into one undo step)
    void setTrackParam (int track, int tp, float value);
    void setGlobalParam (int gp, float value);
    void setStep (int pattern, int track, int step, const Step& s);
    void setTrackPattern (int pattern, int track, const TrackPattern& tp);
    void setPatternBars (int pattern, int bars);
    void setTrackSound (int track, const TrackSound& ts);
    void setTrackFlags (int track, bool mute, bool solo);
    void setTrackLocked (int track, bool locked);
    void setFxType (int slot, int fxType);
    void setModSlot (int index, const ModSlot& m);
    // Adds or updates the routing src -> dst. Returns the slot index or -1 if the matrix is full.
    int  addMod (const ModSlot& m);
    void removeMod (int index);
    void setTuning (const Tuning& t);
    void setSong (const Song& s);
    void setTrackSample (int track, int sampleId);
    // Put a voice preset on a track (sound + its modulation routings), as one undo step.
    void applyVoice (int track, const VoicePresetData& v);
    void setProjectMeta (const char* name, uint32_t seed, int style, int midiPatternCh);
    void replaceProject (const Project& p);             // whole project (preset, New Idea)
    void replacePattern (int pattern, const Pattern& p);

    // ---- host automation: values the host owns are mirrored here without undo or re-posting
    void noteHostGlobal (int gp, float v) { if (gp >= 0 && gp < GP::count) proj.kit.gparams[gp] = clamp01 (v); }
    void noteHostTrack (int t, int tp, float v) { if (t >= 0 && t < kTracks && tp >= 0 && tp < TP::count) proj.kit.tracks[t].params[tp] = clamp01 (v); }
    void noteHostMute (int t, bool m) { if (t >= 0 && t < kTracks) proj.kit.tracks[t].mute = m ? 1 : 0; }

    // ---- performance (not undoable)
    void selectPattern (int pattern, bool immediate);
    void setPlay (bool on);
    void setFill (bool on);
    void setBreak (bool on);
    void setEvolve (bool on);
    void setSelectedTrack (int t);
    void previewNote (int track, int pitchOffset, float velocity, bool on);
    void allNotesOff();

    // ---- undo
    struct Transaction
    {
        Transaction (Session& s, const char* name) : session (s) { session.beginTransaction (name); }
        ~Transaction() { session.endTransaction(); }
        Session& session;
    };
    void beginTransaction (const char* name);
    void endTransaction();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    const char* undoName() const { return undoStack.empty() ? "" : undoStack.back().name.c_str(); }
    const char* redoName() const { return redoStack.empty() ? "" : redoStack.back().name.c_str(); }
    bool undo();
    bool redo();
    void clearUndo();

    // ---- housekeeping: call regularly (e.g. 30 Hz) from the message thread
    void tick();
    // Called after the engine's sample rate changed: rebuilds nothing by itself, the engine re-prepares its effects.
    void resyncEngine() { pushWholeProject(); }

    // ---- persistence
    Json toJson();
    bool fromJson (const Json& j);      // replaces the project (clears undo)

    // Fired (message thread) after the whole project was replaced, so the plugin layer can update host parameters.
    std::function<void()> onProjectReplaced;

private:
    enum class Obj : uint8_t { TrackPattern, TrackSound, KitGlobals, Song, PatternMeta, Meta, Whole };
    struct Snapshot
    {
        Obj obj; int a = 0, b = 0;
        std::vector<uint8_t> before, after;
    };
    struct UndoEntry { std::string name; std::vector<Snapshot> snaps; size_t bytes = 0; };
    struct KitGlobals { float gparams[GP::count]; uint8_t fxType[kNumFxSlots]; ModSlot mods[kModSlots]; Tuning tuning; };
    struct MetaData { char name[40]; uint32_t seed; uint8_t style, midiPatternCh, evolveOn; };

    void touch (Obj obj, int a = 0, int b = 0);
    std::vector<uint8_t> capture (Obj obj, int a, int b) const;
    void restore (const Snapshot& s, bool useAfter);
    void applyEntry (const UndoEntry& e, bool useAfter);
    void pushUndo (UndoEntry&& e);

    void post (const Cmd& c);
    void pushWholeProject();
    void pushTrackSound (int track, const TrackSound* previous);
    void pushFx (int slot, int type);
    void pushSample (int track);
    void pushKitGlobals (const KitGlobals* previous);
    std::vector<int> usedSampleIds() const;

    AudioEngine& eng;
    Project proj;
    SamplePool pool;
    std::recursive_mutex lock;
    uint64_t rev = 1, structRev = 1;
    int editPat = 0;

    int txnDepth = 0;
    UndoEntry current;
    std::deque<UndoEntry> undoStack, redoStack;
    size_t undoBytes = 0;
    bool needResync = false;
    bool applyingUndo = false;
};

} // namespace atm
