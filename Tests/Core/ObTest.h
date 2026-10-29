// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

// Minimal self-contained test framework. Deliberately dependency free so the core can be verified on
// any machine with a C++17 compiler. Tests register themselves statically; main() runs them all and
// returns non-zero on any failure.

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace obtest
{

struct TestCase
{
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& FailureCount()
{
    static int failures = 0;
    return failures;
}

inline const char*& CurrentTest()
{
    static const char* current = "";
    return current;
}

struct Registrar
{
    Registrar(const char* name, std::function<void()> fn) { Registry().push_back({name, std::move(fn)}); }
};

inline void ReportFailure(const char* file, int line, const std::string& message)
{
    ++FailureCount();
    std::fprintf(stderr, "  FAIL %s:%d [%s] %s\n", file, line, CurrentTest(), message.c_str());
}

inline int RunAll(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const TestCase& t : Registry())
    {
        if (filter && std::string(t.name).find(filter) == std::string::npos)
            continue;
        CurrentTest() = t.name;
        const int before = FailureCount();
        t.fn();
        ++run;
        std::printf("%s %s\n", FailureCount() == before ? "[ OK ]" : "[FAIL]", t.name);
    }
    std::printf("%d test(s) run, %d failure(s)\n", run, FailureCount());
    return FailureCount() == 0 && run > 0 ? 0 : 1;
}

} // namespace obtest

#define OB_TEST(Name)                                                                                        \
    static void Name();                                                                                      \
    static ::obtest::Registrar Name##_registrar(#Name, &Name);                                               \
    static void Name()

#define OB_CHECK(cond)                                                                                       \
    do                                                                                                       \
    {                                                                                                        \
        if (!(cond))                                                                                         \
            ::obtest::ReportFailure(__FILE__, __LINE__, "check failed: " #cond);                             \
    } while (0)

#define OB_CHECK_EQ(a, b)                                                                                    \
    do                                                                                                       \
    {                                                                                                        \
        if (!((a) == (b)))                                                                                   \
            ::obtest::ReportFailure(__FILE__, __LINE__,                                                      \
                                    std::string("expected equal: ") + #a + " == " + #b + " (" +              \
                                        ::obtest::Str(a) + " vs " + ::obtest::Str(b) + ")");                 \
    } while (0)

#define OB_REQUIRE(cond)                                                                                     \
    do                                                                                                       \
    {                                                                                                        \
        if (!(cond))                                                                                         \
        {                                                                                                    \
            ::obtest::ReportFailure(__FILE__, __LINE__, "requirement failed: " #cond);                       \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

#define OB_TEST_MAIN()                                                                                       \
    int main(int argc, char** argv)                                                                          \
    {                                                                                                        \
        return ::obtest::RunAll(argc, argv);                                                                 \
    }

namespace obtest
{
inline std::string Str(const char* v)
{
    return v;
}
inline std::string Str(const std::string& v)
{
    return v;
}
template <typename T> std::string Str(const T& v)
{
    if constexpr (std::is_enum<T>::value)
        return std::to_string(static_cast<long long>(v));
    else if constexpr (std::is_same<T, bool>::value)
        return v ? "true" : "false";
    else
        return std::to_string(v);
}
} // namespace obtest
