// Copyright OpenBattlefield contributors. Licensed under the Apache License, Version 2.0.
#pragma once

// Support utilities for the benchmark harness: wall/CPU clocks, process memory, allocation counters,
// percentile statistics, a tiny JSON emitter and command-line parsing. No third-party dependencies.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h> // must precede psapi.h, which is not self-contained
#include <psapi.h>
// clang-format on
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/time.h>
#else
#include <sys/resource.h>
#include <sys/time.h>
#endif

namespace obbench
{

// --- Time -------------------------------------------------------------------------------------------

inline uint64_t NowNs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

/// Process CPU time (user + system) in nanoseconds.
inline uint64_t CpuNs()
{
#if defined(_WIN32)
    FILETIME creation, exit, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        return 0;
    const uint64_t k = (static_cast<uint64_t>(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
    const uint64_t u = (static_cast<uint64_t>(user.dwHighDateTime) << 32) | user.dwLowDateTime;
    return (k + u) * 100;
#else
    rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return 0;
    const uint64_t user = static_cast<uint64_t>(ru.ru_utime.tv_sec) * 1000000000ull +
                          static_cast<uint64_t>(ru.ru_utime.tv_usec) * 1000ull;
    const uint64_t sys = static_cast<uint64_t>(ru.ru_stime.tv_sec) * 1000000000ull +
                         static_cast<uint64_t>(ru.ru_stime.tv_usec) * 1000ull;
    return user + sys;
#endif
}

struct ScopedTimer
{
    uint64_t start = NowNs();
    uint64_t ElapsedNs() const { return NowNs() - start; }
};

// --- Memory -----------------------------------------------------------------------------------------

struct MemoryInfo
{
    uint64_t currentRssBytes = 0;
    uint64_t peakRssBytes = 0;
    bool valid = false;
};

inline MemoryInfo QueryMemory()
{
    MemoryInfo m;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        m.currentRssBytes = pmc.WorkingSetSize;
        m.peakRssBytes = pmc.PeakWorkingSetSize;
        m.valid = true;
    }
#elif defined(__APPLE__)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) ==
        KERN_SUCCESS)
    {
        m.currentRssBytes = info.resident_size;
        m.peakRssBytes = info.resident_size_max;
        m.valid = true;
    }
#else
    if (FILE* f = std::fopen("/proc/self/status", "r"))
    {
        char line[256];
        while (std::fgets(line, sizeof(line), f))
        {
            unsigned long long kb = 0;
            if (std::sscanf(line, "VmRSS: %llu kB", &kb) == 1)
            {
                m.currentRssBytes = kb * 1024ull;
                m.valid = true;
            }
            else if (std::sscanf(line, "VmHWM: %llu kB", &kb) == 1)
                m.peakRssBytes = kb * 1024ull;
        }
        std::fclose(f);
    }
#endif
    return m;
}

// --- Allocation counters (hooked in ObBench.cpp through global operator new/delete) ----------------

struct AllocCounters
{
    uint64_t allocations = 0;
    uint64_t frees = 0;
    uint64_t bytes = 0;
};

inline AllocCounters& GlobalAllocCounters()
{
    static AllocCounters c;
    return c;
}

struct AllocDelta
{
    AllocCounters start = GlobalAllocCounters();
    uint64_t Allocations() const { return GlobalAllocCounters().allocations - start.allocations; }
    uint64_t Bytes() const { return GlobalAllocCounters().bytes - start.bytes; }
};

// --- Statistics -------------------------------------------------------------------------------------

struct LatencyStats
{
    size_t count = 0;
    double minNs = 0, medianNs = 0, meanNs = 0, p95Ns = 0, p99Ns = 0, maxNs = 0;
};

inline LatencyStats ComputeStats(std::vector<double> samples)
{
    LatencyStats s;
    if (samples.empty())
        return s;
    std::sort(samples.begin(), samples.end());
    s.count = samples.size();
    auto pct = [&](double p)
    {
        // Nearest-rank percentile.
        size_t rank = static_cast<size_t>(p * static_cast<double>(samples.size()) + 0.999999);
        if (rank == 0)
            rank = 1;
        if (rank > samples.size())
            rank = samples.size();
        return samples[rank - 1];
    };
    double sum = 0;
    for (double v : samples)
        sum += v;
    s.minNs = samples.front();
    s.maxNs = samples.back();
    s.meanNs = sum / static_cast<double>(samples.size());
    s.medianNs = pct(0.5);
    s.p95Ns = pct(0.95);
    s.p99Ns = pct(0.99);
    return s;
}

// --- JSON -------------------------------------------------------------------------------------------

class Json
{
public:
    void BeginObject(const char* key = nullptr)
    {
        Key(key);
        out_ += "{";
        stack_.push_back(0);
    }
    void EndObject()
    {
        stack_.pop_back();
        Newline();
        out_ += "}";
        Bump();
    }
    void BeginArray(const char* key = nullptr)
    {
        Key(key);
        out_ += "[";
        stack_.push_back(0);
    }
    void EndArray()
    {
        stack_.pop_back();
        Newline();
        out_ += "]";
        Bump();
    }
    void String(const char* key, const std::string& v)
    {
        Key(key);
        out_ += "\"" + Escape(v) + "\"";
        Bump();
    }
    void Number(const char* key, double v)
    {
        Key(key);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.6g", v);
        out_ += buf;
        Bump();
    }
    void Integer(const char* key, uint64_t v)
    {
        Key(key);
        out_ += std::to_string(v);
        Bump();
    }
    void Bool(const char* key, bool v)
    {
        Key(key);
        out_ += v ? "true" : "false";
        Bump();
    }
    void Hex64(const char* key, uint64_t v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(v));
        String(key, buf);
    }
    const std::string& Str() const { return out_; }

private:
    void Key(const char* key)
    {
        if (!stack_.empty() && stack_.back() > 0)
            out_ += ",";
        if (!stack_.empty())
            Newline();
        if (key)
        {
            out_ += "\"";
            out_ += Escape(key);
            out_ += "\": ";
        }
    }
    void Bump()
    {
        if (!stack_.empty())
            ++stack_.back();
    }
    void Newline()
    {
        out_ += "\n";
        out_.append(stack_.size() * 2, ' ');
    }
    static std::string Escape(const std::string& s)
    {
        std::string r;
        for (char c : s)
        {
            if (c == '"' || c == '\\')
            {
                r += '\\';
                r += c;
            }
            else if (c == '\n')
                r += "\\n";
            else
                r += c;
        }
        return r;
    }
    std::string out_;
    std::vector<int> stack_;
};

// --- Command line -----------------------------------------------------------------------------------

struct Args
{
    std::map<std::string, std::string> values;
    bool Has(const std::string& k) const { return values.count(k) != 0; }
    std::string Get(const std::string& k, const std::string& def) const
    {
        auto it = values.find(k);
        return it == values.end() ? def : it->second;
    }
    uint64_t GetU64(const std::string& k, uint64_t def) const
    {
        auto it = values.find(k);
        return it == values.end() ? def : std::strtoull(it->second.c_str(), nullptr, 0);
    }
};

inline Args ParseArgs(int argc, char** argv)
{
    Args a;
    for (int i = 1; i < argc; ++i)
    {
        std::string s = argv[i];
        if (s.size() > 2 && s[0] == '-' && s[1] == '-')
        {
            s = s.substr(2);
            const size_t eq = s.find('=');
            if (eq != std::string::npos)
                a.values[s.substr(0, eq)] = s.substr(eq + 1);
            else if (i + 1 < argc && argv[i + 1][0] != '-')
                a.values[s] = argv[++i];
            else
                a.values[s] = "1";
        }
    }
    return a;
}

/// Reads "key=value" lines ('#' starts a comment). Returns false if the file cannot be opened.
inline bool LoadConfigFile(const std::string& path, Args& out)
{
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f)
        return false;
    char line[512];
    while (std::fgets(line, sizeof(line), f))
    {
        std::string s = line;
        const size_t hash = s.find('#');
        if (hash != std::string::npos)
            s = s.substr(0, hash);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
            s.pop_back();
        size_t start = 0;
        while (start < s.size() && (s[start] == ' ' || s[start] == '\t'))
            ++start;
        s = s.substr(start);
        if (s.empty())
            continue;
        const size_t eq = s.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = s.substr(0, eq);
        std::string value = s.substr(eq + 1);
        while (!key.empty() && key.back() == ' ')
            key.pop_back();
        while (!value.empty() && value.front() == ' ')
            value.erase(value.begin());
        out.values[key] = value;
    }
    std::fclose(f);
    return true;
}

inline std::string UtcTimestamp()
{
    const std::time_t t = std::time(nullptr);
    char buf[64];
#if defined(_WIN32)
    std::tm tmv;
    gmtime_s(&tmv, &t);
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
#else
    std::tm tmv;
    gmtime_r(&t, &tmv);
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
#endif
    return buf;
}

inline const char* PlatformName()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

inline const char* ArchName()
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#else
    return "unknown";
#endif
}

} // namespace obbench
