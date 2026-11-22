#include "TestFramework.h"
#include "util/Denormals.h"
#include <cstring>

int main (int argc, char** argv)
{
    atm::ScopedFlushDenormals fz;
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& c : t::cases())
    {
        if (filter && std::strstr (c.name, filter) == nullptr) continue;
        const int before = t::failures();
        c.fn();
        ++ran;
        std::printf ("%s %s\n", t::failures() == before ? "ok  " : "FAIL", c.name);
    }
    std::printf ("\n%d tests, %d checks, %d failures\n", ran, t::checks(), t::failures());
    return t::failures() == 0 ? 0 : 1;
}
