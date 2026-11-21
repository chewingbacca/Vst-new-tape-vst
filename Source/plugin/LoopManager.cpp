#include "LoopManager.h"

using namespace juce;
using namespace splinter;

LoopManager::LoopManager() : Thread ("Splinter loop worker")
{
    startThread (Thread::Priority::low);
}

LoopManager::~LoopManager()
{
    cancelPendingUpdate();
    signalThreadShouldExit();
    notify();
    stopThread (60000);        // analysis of a long file is not interruptible; never kill the thread
    delete pending.exchange (nullptr);
    delete leaked.exchange (nullptr);
    delete current;
    RenderedLoop* r = nullptr;
    while (retired.pop (r)) delete r;
}

bool LoopManager::canLoad (const File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg");
}

void LoopManager::submit (Job&& j)
{
    {
        const ScopedLock sl (lock);
        queuedJob = std::make_unique<Job> (std::move (j));
        ++epoch;
        statusInfo.loading = true; statusInfo.error.clear();
    }
    busy.store (true);
    notify();
}

void LoopManager::loadDemo (int index, bool keepSlices)
{
    Job j; j.type = Job::tDemo; j.demoIndex = jlimit (0, kNumDemoLoops - 1, index); j.keepSlices = keepSlices;
    submit (std::move (j));
}

void LoopManager::loadFile (const File& file, bool keepSlices)
{
    Job j; j.type = Job::tFile; j.file = file; j.keepSlices = keepSlices;
    submit (std::move (j));
}

void LoopManager::loadAudio (splinter::AudioData&& audio, const String& name, double bpmHint, LoopInfo::Kind kind, bool keepSlices)
{
    Job j; j.type = Job::tAudio; j.audio = std::move (audio); j.name = name; j.bpmHint = bpmHint; j.kind = kind; j.keepSlices = keepSlices;
    submit (std::move (j));
}

void LoopManager::clear()
{
    const ScopedLock sl (lock);
    source.reset(); queuedJob.reset(); ++epoch;
    statusInfo = LoopInfo(); statusInfo.generation = ++generationCounter;
    clearRequest.store (true);
}

void LoopManager::setBeats (double beats)
{
    {
        const ScopedLock sl (lock);
        if (source == nullptr || beats < 1.0 || beats > 128.0) return;
        auto copy = std::make_shared<Source> (*source);
        copy->analysis.beats = beats;
        copy->analysis.bpm = beats * 60.0 / std::max (1.0e-3, copy->info.seconds);
        copy->info.beats = beats; copy->info.sourceBpm = copy->analysis.bpm;
        copy->info.generation = ++generationCounter;
        source = copy; statusInfo = copy->info;
        notifyKeepSlices = false;
    }
    busy.store (true);
    notify();
    triggerAsyncUpdate();
}

LoopInfo LoopManager::getInfo() const
{
    const ScopedLock sl (lock);
    return statusInfo;
}

std::vector<float> LoopManager::getPeaks() const
{
    const ScopedLock sl (lock);
    return source != nullptr ? source->peaks : std::vector<float>();
}

SliceSet LoopManager::detectSlices (float sensitivity) const
{
    std::shared_ptr<Source> s;
    { const ScopedLock sl (lock); s = source; }
    if (s == nullptr) { SliceSet g; g.makeGrid (8.0f, 4); return g; }
    return slicesFromOnsets (s->analysis, s->info.seconds, sensitivity);
}

void LoopManager::handleAsyncUpdate()
{
    bool keep;
    { const ScopedLock sl (lock); keep = notifyKeepSlices; }
    if (onLoopChanged) onLoopChanged (keep);
}

bool LoopManager::waitUntilReady (int timeoutMs)
{
    // the worker only notices a new target when it next wakes, so insist on two
    // complete passes that both end idle
    const auto start = Time::getMillisecondCounter();
    const uint32_t first = passCount.load();
    for (;;)
    {
        notify();
        if (passCount.load() - first >= 2 && ! busy.load()) return true;
        if ((int) (Time::getMillisecondCounter() - start) > timeoutMs) return false;
        Thread::sleep (2);
    }
}

// ---------------------------------------------------------------- worker
bool LoopManager::decode (InputStream* stream, const String&, splinter::AudioData& out, String& error)
{
    AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (std::unique_ptr<InputStream> (stream)));
    if (reader == nullptr) { error = "Unsupported or unreadable audio file"; return false; }
    if (reader->lengthInSamples < 256) { error = "File is too short"; return false; }
    if ((double) reader->lengthInSamples / reader->sampleRate > 120.0) { error = "File is longer than two minutes"; return false; }
    const int n = (int) reader->lengthInSamples;
    AudioBuffer<float> buf (2, n);
    buf.clear();
    reader->read (&buf, 0, n, 0, true, true);
    if (reader->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, n);
    out.sampleRate = reader->sampleRate;
    out.resize (n);
    for (int c = 0; c < 2; ++c) std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + n, out.ch[c].begin());
    return true;
}

void LoopManager::computePeaks (Source& s)
{
    const int cols = 2048;
    const int64_t n = s.audio.length();
    s.peaks.assign ((size_t) cols * 2, 0.0f);
    if (n <= 0) return;
    for (int c = 0; c < cols; ++c)
    {
        const int64_t a = n * c / cols, b = std::max (a + 1, n * (c + 1) / cols);
        float lo = 0.0f, hi = 0.0f;
        for (int64_t i = a; i < b && i < n; ++i)
        {
            const float v = 0.5f * (s.audio.ch[0][(size_t) i] + s.audio.ch[1][(size_t) i]);
            lo = std::min (lo, v); hi = std::max (hi, v);
        }
        s.peaks[(size_t) c * 2] = lo; s.peaks[(size_t) c * 2 + 1] = hi;
    }
}

MemoryBlock LoopManager::encodeFlac (const splinter::AudioData& a)
{
    MemoryBlock mb;
    const int n = (int) a.length();
    if (n <= 0) return mb;
    FlacAudioFormat flac;
    auto stream = std::make_unique<MemoryOutputStream> (mb, false);
    std::unique_ptr<OutputStream> os (stream.release());
    auto options = AudioFormatWriterOptions().withSampleRate (a.sampleRate).withNumChannels (2).withBitsPerSample (24);
    if (auto writer = flac.createWriterFor (os, options))
    {
        AudioBuffer<float> buf (2, n);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, a.ch[c].data(), n);
        writer->writeFromAudioSampleBuffer (buf, 0, n);
        writer->flush();
        writer.reset();
    }
    return mb;
}

bool LoopManager::runJob (Job& job, uint32_t jobEpoch)
{
    auto s = std::make_shared<Source>();
    String error;
    double bpmHint = job.bpmHint;
    switch (job.type)
    {
        case Job::tDemo:
        {
            const auto& d = demoLoopInfo (job.demoIndex);
            generateDemoLoop (job.demoIndex, 48000.0, s->audio);
            s->info.kind = LoopInfo::demo; s->info.demoIndex = job.demoIndex; s->info.name = d.name;
            bpmHint = d.bpm;
            break;
        }
        case Job::tFile:
        {
            if (! job.file.existsAsFile()) { error = "File not found: " + job.file.getFileName(); break; }
            if (! decode (job.file.createInputStream().release(), job.file.getFileName(), s->audio, error)) break;
            s->info.kind = LoopInfo::file; s->info.name = job.file.getFileNameWithoutExtension(); s->info.filePath = job.file.getFullPathName();
            break;
        }
        case Job::tEncoded:
        {
            if (! decode (new MemoryInputStream (job.data, true), job.name, s->audio, error)) break;
            s->info.kind = job.kind; s->info.name = job.name; s->info.filePath = job.path;
            s->encoded = job.data;
            if (job.beatsHint > 0.0) bpmHint = job.beatsHint * 60.0 / ((double) s->audio.length() / s->audio.sampleRate);
            break;
        }
        case Job::tAudio:
            s->audio = std::move (job.audio);
            s->info.kind = job.kind; s->info.name = job.name;
            break;
        default: return false;
    }
    if (error.isNotEmpty() || s->audio.length() < 256)
    {
        const ScopedLock sl (lock);
        if (epoch != jobEpoch) return false;
        statusInfo.loading = false;
        statusInfo.error = error.isNotEmpty() ? error : String ("Empty audio");
        return false;
    }
    s->analysis = analyseLoop (s->audio, bpmHint);
    if (job.type == Job::tDemo)
    {
        const auto& d = demoLoopInfo (job.demoIndex);
        s->analysis.key = d.key; s->analysis.scale = d.scale; s->analysis.keyConfidence = 1.0f; s->analysis.beats = d.beats; s->analysis.bpm = d.bpm;
    }
    s->info.seconds = (double) s->audio.length() / s->audio.sampleRate;
    s->info.sourceBpm = s->analysis.bpm; s->info.beats = s->analysis.beats;
    s->info.key = s->analysis.key; s->info.scale = s->analysis.scale;
    s->info.keyConfidence = s->analysis.keyConfidence; s->info.percussiveness = s->analysis.percussiveness;
    computePeaks (*s);
    {
        const ScopedLock sl (lock);
        if (epoch != jobEpoch) return false;          // a newer load (or a clear) superseded this one
        s->info.generation = ++generationCounter;
        source = s; statusInfo = s->info; statusInfo.loading = false;
        notifyKeepSlices = job.keepSlices;
    }
    triggerAsyncUpdate();
    return true;
}

void LoopManager::renderIfNeeded()
{
    std::shared_ptr<Source> s;
    { const ScopedLock sl (lock); s = source; }
    const double sr = targetSr.load(), bpm = targetBpm.load();
    const int mode = targetMode.load();
    if (s == nullptr || sr <= 0.0 || bpm <= 0.0) return;
    const bool materialChanged = s->info.generation != renderedGen || sr != renderedSr || mode != renderedMode;
    const bool tempoChanged = std::fabs (bpm - renderedBpm) > renderedBpm * 2.0e-4;
    if (! materialChanged && ! tempoChanged) return;
    // debounce tempo ramps: wait until two consecutive polls agree (unless nothing is rendered yet)
    if (! materialChanged && std::fabs (bpm - lastSeenBpm) > bpm * 2.0e-4) { lastSeenBpm = bpm; busy.store (true); return; }
    lastSeenBpm = bpm;

    auto* r = new RenderedLoop();
    renderLoop (s->audio, s->analysis, sr, bpm, (StretchMode) jlimit (0, 3, mode), *r);
    r->generation = s->info.generation;
    {
        const ScopedLock sl (lock);
        if (source != s) { delete r; return; }        // the material changed while we were rendering
    }
    renderedGen = s->info.generation; renderedSr = sr; renderedBpm = bpm; renderedMode = mode;
    delete pending.exchange (r, std::memory_order_acq_rel);
}

void LoopManager::run()
{
    while (! threadShouldExit())
    {
        RenderedLoop* dead = nullptr;
        while (retired.pop (dead)) delete dead;
        delete leaked.exchange (nullptr);

        std::unique_ptr<Job> job; uint32_t jobEpoch = 0;
        { const ScopedLock sl (lock); job = std::move (queuedJob); jobEpoch = epoch; }
        try
        {
            if (job != nullptr) runJob (*job, jobEpoch);
            renderIfNeeded();
        }
        catch (const std::bad_alloc&)
        {
            const ScopedLock sl (lock);
            statusInfo.loading = false; statusInfo.error = "Not enough memory for this loop";
            renderedBpm = targetBpm.load(); renderedSr = targetSr.load(); renderedMode = targetMode.load(); renderedGen = statusInfo.generation;
        }

        bool more, settled;
        {
            const ScopedLock sl (lock);
            more = queuedJob != nullptr;
            const double bpm = targetBpm.load();
            settled = ! more && (source == nullptr || targetSr.load() <= 0.0
                                 || (renderedGen == statusInfo.generation && renderedSr == targetSr.load() && renderedMode == targetMode.load()
                                     && std::fabs (bpm - renderedBpm) <= renderedBpm * 2.0e-4));
        }
        busy.store (! settled);
        passCount.fetch_add (1);
        if (settled) wait (40); else wait (more ? 1 : 15);
    }
}

// ---------------------------------------------------------------- state
void LoopManager::writeState (ValueTree& parent) const
{
    std::shared_ptr<Source> s;
    { const ScopedLock sl (lock); s = source; }
    ValueTree t ("Loop");
    if (s != nullptr)
    {
        t.setProperty ("kind", (int) s->info.kind, nullptr);
        t.setProperty ("name", s->info.name, nullptr);
        t.setProperty ("beats", s->info.beats, nullptr);
        if (s->info.kind == LoopInfo::demo) t.setProperty ("demo", s->info.demoIndex, nullptr);
        else
        {
            t.setProperty ("path", s->info.filePath, nullptr);
            // embed the audio so the project is self-contained (captures have no file at all)
            if (s->encoded.isEmpty())
            {
                MemoryBlock mb = encodeFlac (s->audio);
                const ScopedLock sl (lock);
                if (source == s) const_cast<Source*> (s.get())->encoded = mb;
                t.setProperty ("audio", mb, nullptr);
            }
            else t.setProperty ("audio", s->encoded, nullptr);
        }
    }
    parent.appendChild (t, nullptr);
}

void LoopManager::readState (const ValueTree& parent)
{
    const ValueTree t = parent.getChildWithName ("Loop");
    if (! t.isValid() || ! t.hasProperty ("kind")) { clear(); return; }
    const int kind = t["kind"];
    if (kind == LoopInfo::demo) { loadDemo ((int) t["demo"], true); return; }
    if (auto* mb = t["audio"].getBinaryData(); mb != nullptr && mb->getSize() > 0)
    {
        Job j; j.type = Job::tEncoded; j.data = *mb; j.name = t["name"].toString(); j.path = t["path"].toString();
        j.kind = (LoopInfo::Kind) kind; j.beatsHint = (double) t["beats"]; j.keepSlices = true;
        submit (std::move (j));
        return;
    }
    const File f (t["path"].toString());
    if (f.existsAsFile()) loadFile (f, true); else clear();
}
