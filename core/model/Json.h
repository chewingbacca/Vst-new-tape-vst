// A small JSON value with parser and writer. Message-thread only (allocates).
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace atm {

class Json
{
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json (bool b) : type (Bool), num (b ? 1.0 : 0.0) {}
    Json (double d) : type (Number), num (d) {}
    Json (float d) : type (Number), num ((double) d) {}
    Json (int d) : type (Number), num ((double) d) {}
    Json (unsigned d) : type (Number), num ((double) d) {}
    Json (long long d) : type (Number), num ((double) d) {}
    Json (const char* s) : type (String), str (s) {}
    Json (std::string s) : type (String), str (std::move (s)) {}

    static Json array() { Json j; j.type = Array; return j; }
    static Json object() { Json j; j.type = Object; return j; }

    Type getType() const { return type; }
    bool isNull() const { return type == Null; }
    bool isObject() const { return type == Object; }
    bool isArray() const { return type == Array; }
    bool isNumber() const { return type == Number; }
    bool isString() const { return type == String; }

    double asDouble (double def = 0.0) const { return (type == Number || type == Bool) ? num : def; }
    float  asFloat (float def = 0.f) const { return (float) asDouble ((double) def); }
    int    asInt (int def = 0) const { return (type == Number || type == Bool) ? (int) std::llround (num) : def; }
    bool   asBool (bool def = false) const { return (type == Number || type == Bool) ? num != 0.0 : def; }
    const std::string& asString() const { static const std::string empty; return type == String ? str : empty; }

    // arrays
    size_t size() const { return type == Array ? arr.size() : (type == Object ? obj.size() : 0); }
    const Json& at (size_t i) const { static const Json null; return (type == Array && i < arr.size()) ? arr[i] : null; }
    const Json& operator[] (size_t i) const { return at (i); }
    const Json& operator[] (int i) const { return at ((size_t) (i < 0 ? ~(size_t) 0 : (size_t) i)); }
    Json& push (Json v) { type = Array; arr.push_back (std::move (v)); return arr.back(); }
    const std::vector<Json>& items() const { return arr; }

    // objects (insertion ordered)
    const Json& operator[] (const char* key) const
    {
        static const Json null;
        if (type == Object) for (auto& kv : obj) if (kv.first == key) return kv.second;
        return null;
    }
    bool has (const char* key) const { if (type == Object) for (auto& kv : obj) if (kv.first == key) return true; return false; }
    Json& set (const std::string& key, Json v)
    {
        type = Object;
        for (auto& kv : obj) if (kv.first == key) { kv.second = std::move (v); return kv.second; }
        obj.emplace_back (key, std::move (v));
        return obj.back().second;
    }
    const std::vector<std::pair<std::string, Json>>& members() const { return obj; }

    // ---- writing
    std::string dump() const { std::string out; write (out); return out; }
    void write (std::string& out) const
    {
        switch (type)
        {
            case Null: out += "null"; break;
            case Bool: out += num != 0.0 ? "true" : "false"; break;
            case Number:
            {
                char buf[40];
                if (! std::isfinite (num)) { out += "0"; break; }
                if (num == std::floor (num) && std::fabs (num) < 1.0e15) std::snprintf (buf, sizeof (buf), "%lld", (long long) num);
                else std::snprintf (buf, sizeof (buf), "%.9g", num);
                out += buf;
                break;
            }
            case String: writeString (out, str); break;
            case Array:
                out += '[';
                for (size_t i = 0; i < arr.size(); ++i) { if (i) out += ','; arr[i].write (out); }
                out += ']';
                break;
            case Object:
                out += '{';
                for (size_t i = 0; i < obj.size(); ++i)
                {
                    if (i) out += ',';
                    writeString (out, obj[i].first); out += ':'; obj[i].second.write (out);
                }
                out += '}';
                break;
        }
    }

    // ---- parsing. Returns a Null value and sets ok=false on malformed input.
    static Json parse (const std::string& text, bool* ok = nullptr)
    {
        const char* p = text.c_str();
        const char* end = p + text.size();
        bool good = true;
        Json v = parseValue (p, end, good, 0);
        skipWs (p, end);
        if (p != end) good = false;
        if (ok) *ok = good;
        return good ? v : Json();
    }

private:
    static void writeString (std::string& out, const std::string& s)
    {
        out += '"';
        for (unsigned char c : s)
        {
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) { char b[8]; std::snprintf (b, sizeof (b), "\\u%04x", c); out += b; }
                    else out += (char) c;
            }
        }
        out += '"';
    }
    static void skipWs (const char*& p, const char* end) { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }

    static Json parseValue (const char*& p, const char* end, bool& ok, int depth)
    {
        skipWs (p, end);
        if (p >= end || depth > 64) { ok = false; return {}; }
        const char c = *p;
        if (c == '{')
        {
            ++p; Json o = object();
            skipWs (p, end);
            if (p < end && *p == '}') { ++p; return o; }
            while (ok)
            {
                skipWs (p, end);
                if (p >= end || *p != '"') { ok = false; break; }
                std::string key = parseString (p, end, ok);
                skipWs (p, end);
                if (p >= end || *p != ':') { ok = false; break; }
                ++p;
                o.obj.emplace_back (std::move (key), parseValue (p, end, ok, depth + 1));
                skipWs (p, end);
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') { ++p; break; }
                ok = false;
            }
            return o;
        }
        if (c == '[')
        {
            ++p; Json a = array();
            skipWs (p, end);
            if (p < end && *p == ']') { ++p; return a; }
            while (ok)
            {
                a.arr.push_back (parseValue (p, end, ok, depth + 1));
                skipWs (p, end);
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') { ++p; break; }
                ok = false;
            }
            return a;
        }
        if (c == '"') return Json (parseString (p, end, ok));
        if (end - p >= 4 && std::strncmp (p, "true", 4) == 0) { p += 4; return Json (true); }
        if (end - p >= 5 && std::strncmp (p, "false", 5) == 0) { p += 5; return Json (false); }
        if (end - p >= 4 && std::strncmp (p, "null", 4) == 0) { p += 4; return {}; }
        char* e = nullptr;
        const double d = std::strtod (p, &e);
        if (e == p || e > end) { ok = false; return {}; }
        p = e;
        return Json (d);
    }

    static std::string parseString (const char*& p, const char* end, bool& ok)
    {
        std::string s;
        ++p;    // opening quote
        while (p < end && *p != '"')
        {
            if (*p == '\\' && p + 1 < end)
            {
                ++p;
                switch (*p)
                {
                    case 'n': s += '\n'; break;
                    case 'r': s += '\r'; break;
                    case 't': s += '\t'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'u':
                    {
                        if (end - p < 5) { ok = false; return s; }
                        char hex[5] = { p[1], p[2], p[3], p[4], 0 };
                        const unsigned cp = (unsigned) std::strtoul (hex, nullptr, 16);
                        if (cp < 0x80) s += (char) cp;
                        else if (cp < 0x800) { s += (char) (0xC0 | (cp >> 6)); s += (char) (0x80 | (cp & 0x3F)); }
                        else { s += (char) (0xE0 | (cp >> 12)); s += (char) (0x80 | ((cp >> 6) & 0x3F)); s += (char) (0x80 | (cp & 0x3F)); }
                        p += 4;
                        break;
                    }
                    default: s += *p; break;
                }
                ++p;
            }
            else s += *p++;
        }
        if (p >= end) { ok = false; return s; }
        ++p;    // closing quote
        return s;
    }

    Type type = Null;
    double num = 0.0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;
};

} // namespace atm
