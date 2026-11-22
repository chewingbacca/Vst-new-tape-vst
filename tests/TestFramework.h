// Minimal self-registering test framework (no dependencies).
#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace t {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
struct Reg { Reg (const char* n, std::function<void()> f) { cases().push_back ({ n, std::move (f) }); } };
}

#define TEST(name) static void test_##name(); static t::Reg reg_##name (#name, test_##name); static void test_##name()
#define CHECK(cond) do { ++t::checks(); if (! (cond)) { ++t::failures(); std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_MSG(cond, ...) do { ++t::checks(); if (! (cond)) { ++t::failures(); std::printf ("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)
#define CHECK_NEAR(a, b, eps) do { ++t::checks(); const double _a = (a), _b = (b); if (! (std::fabs (_a - _b) <= (eps))) { ++t::failures(); std::printf ("  FAIL %s:%d  %s = %g, expected %g (+-%g)\n", __FILE__, __LINE__, #a, _a, _b, (double) (eps)); } } while (0)
