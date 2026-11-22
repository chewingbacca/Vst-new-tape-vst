// Owns the samples a project uses and maps them to/from their saved description.
// Message-thread side only.
#pragma once
#include "../model/Serialize.h"
#include "../voices/SampleData.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace atm {

class SamplePool : public SampleRefIO
{
public:
    enum Kind { Factory, File };
    struct Entry
    {
        int id = 0;
        Kind kind = File;
        std::string name;                 // factory name, or display name of a file
        std::string path;                 // original file path (may no longer exist)
        const SampleData* data = nullptr;
        std::unique_ptr<SampleData> owned;   // null for factory samples (shared, never freed)
        int audioRefs = 0;                // how many times the audio thread currently holds it
        bool retired = false;
    };

    // Decodes an audio file into a SampleData (plugin layer supplies this; returns nullptr on failure).
    std::function<SampleData* (const std::string& path)> fileLoader;
    // Blobs saved inside the project, keyed by the "key" written by describeSample (filled by the
    // plugin layer before a project is parsed, consumed by resolveSample).
    std::map<int, SampleData*> embedded;
    std::map<int, int> embeddedIds;       // key -> pool id for blobs already taken
    void clearEmbedded() { for (auto& kv : embedded) delete kv.second; embedded.clear(); embeddedIds.clear(); }

    int addFactory (const std::string& name);                              // 0 if unknown
    int addFile (const std::string& path);                                 // 0 if it cannot be loaded
    int addOwned (SampleData* data, const std::string& name, const std::string& path);   // takes ownership

    const Entry* find (int id) const;
    const SampleData* data (int id) const { const Entry* e = find (id); return e ? e->data : nullptr; }
    const std::vector<std::unique_ptr<Entry>>& entries() const { return list; }

    // Audio-thread bookkeeping (called from the message thread when posting / collecting garbage)
    void audioAcquire (const SampleData* d);
    void audioRelease (const SampleData* d);
    // Free file samples that no id in `usedIds` references and the audio thread no longer holds.
    void purge (const std::vector<int>& usedIds);

    Json describeSample (int sampleId) override;
    int resolveSample (const Json& description) override;

private:
    Entry* findMutable (int id);
    std::vector<std::unique_ptr<Entry>> list;
    int nextId = 1;
};

} // namespace atm
