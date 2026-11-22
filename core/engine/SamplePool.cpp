#include "SamplePool.h"
#include "../factory/FactorySamples.h"
#include <algorithm>

namespace atm {

const SamplePool::Entry* SamplePool::find (int id) const
{
    for (auto& e : list) if (e->id == id && ! e->retired) return e.get();
    return nullptr;
}
SamplePool::Entry* SamplePool::findMutable (int id)
{
    for (auto& e : list) if (e->id == id) return e.get();
    return nullptr;
}

int SamplePool::addFactory (const std::string& name)
{
    for (auto& e : list) if (e->kind == Factory && e->name == name && ! e->retired) return e->id;
    const SampleData* d = getFactorySample (name.c_str());
    if (d == nullptr) return 0;
    auto e = std::make_unique<Entry>();
    e->id = nextId++; e->kind = Factory; e->name = name; e->data = d;
    list.push_back (std::move (e));
    return list.back()->id;
}

int SamplePool::addOwned (SampleData* data, const std::string& name, const std::string& path)
{
    if (data == nullptr) return 0;
    auto e = std::make_unique<Entry>();
    e->id = nextId++; e->kind = File; e->name = name; e->path = path;
    e->owned.reset (data); e->data = data;
    data->id = e->id;
    list.push_back (std::move (e));
    return list.back()->id;
}

int SamplePool::addFile (const std::string& path)
{
    for (auto& e : list) if (e->kind == File && e->path == path && ! e->retired) return e->id;
    if (! fileLoader) return 0;
    SampleData* d = fileLoader (path);
    if (d == nullptr) return 0;
    std::string name = path;
    const size_t slash = name.find_last_of ("/\\");
    if (slash != std::string::npos) name = name.substr (slash + 1);
    const size_t dot = name.find_last_of ('.');
    if (dot != std::string::npos && dot > 0) name = name.substr (0, dot);
    return addOwned (d, name, path);
}

void SamplePool::audioAcquire (const SampleData* d)
{
    if (d == nullptr) return;
    for (auto& e : list) if (e->data == d) { ++e->audioRefs; return; }
}

void SamplePool::audioRelease (const SampleData* d)
{
    if (d == nullptr) return;
    for (auto& e : list) if (e->data == d) { e->audioRefs = std::max (0, e->audioRefs - 1); break; }
    list.erase (std::remove_if (list.begin(), list.end(), [] (const std::unique_ptr<Entry>& e) { return e->retired && e->audioRefs == 0; }), list.end());
}

void SamplePool::purge (const std::vector<int>& usedIds)
{
    for (auto& e : list)
        if (e->kind == File && std::find (usedIds.begin(), usedIds.end(), e->id) == usedIds.end()) e->retired = true;
    list.erase (std::remove_if (list.begin(), list.end(), [] (const std::unique_ptr<Entry>& e) { return e->retired && e->audioRefs == 0; }), list.end());
}

Json SamplePool::describeSample (int id)
{
    const Entry* e = find (id);
    if (e == nullptr) return Json();
    Json j = Json::object();
    if (e->kind == Factory) j.set ("factory", e->name);
    else { j.set ("file", e->path); j.set ("name", e->name); j.set ("key", e->id); }
    return j;
}

int SamplePool::resolveSample (const Json& d)
{
    if (! d.isObject()) return 0;
    if (d.has ("factory")) return addFactory (d["factory"].asString());
    if (d.has ("file"))
    {
        const int key = d["key"].asInt (0);
        auto it = embedded.find (key);
        if (it != embedded.end() && it->second != nullptr)
        {
            SampleData* data = it->second;
            it->second = nullptr;                      // ownership moves into the pool (a second track with the same key reuses the id)
            const int id = addOwned (data, d["name"].asString(), d["file"].asString());
            embeddedIds[key] = id;
            return id;
        }
        auto done = embeddedIds.find (key);
        if (done != embeddedIds.end()) return done->second;
        return addFile (d["file"].asString());
    }
    return 0;
}

} // namespace atm
