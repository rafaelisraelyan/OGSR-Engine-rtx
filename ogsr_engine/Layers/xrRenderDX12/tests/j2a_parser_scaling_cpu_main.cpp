#include "dx12levelhierarchy.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace
{
bool g_failAlloc = false;
unsigned g_injectedAllocations = 0;
bool g_countAlloc = false;
unsigned g_allocationCount = 0;
unsigned g_failAfter = 0;

void AppendU32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
}

void AppendChunk(std::vector<uint8_t>& out, uint32_t type,
                 const std::vector<uint8_t>& payload)
{
    AppendU32(out, type);
    AppendU32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> MakeHeader()
{
    std::vector<uint8_t> payload(levelhierarchy12::kOgfHeaderSize, 0);
    payload[0] = 4;
    payload[1] = levelhierarchy12::kMtNormal;
    payload[2] = 1;
    payload[3] = 0;
    const float minimum = -1.0f;
    const float maximum = 1.0f;
    for (int i = 0; i < 3; ++i)
    {
        std::memcpy(payload.data() + 4 + i * 4, &minimum, sizeof(float));
        std::memcpy(payload.data() + 16 + i * 4, &maximum, sizeof(float));
    }
    return payload;
}

std::vector<uint8_t> MakeVisual()
{
    std::vector<uint8_t> children;
    AppendU32(children, 0);
    std::vector<uint8_t> visual;
    AppendChunk(visual, levelhierarchy12::kOgfChildrenLink, children);
    AppendChunk(visual, levelhierarchy12::kOgfHeader, MakeHeader());
    return visual;
}

std::vector<uint8_t> MakeLevelFromIds(const std::vector<uint32_t>& ids)
{
    std::vector<uint8_t> visuals;
    const std::vector<uint8_t> visual = MakeVisual();
    for (const uint32_t id : ids)
        AppendChunk(visuals, id, visual);

    std::vector<uint8_t> level;
    AppendChunk(level, levelhierarchy12::kLevelVisualsChunk, visuals);

    std::vector<uint8_t> root;
    AppendU32(root, ids.empty() ? 1u : ids.front());
    std::vector<uint8_t> sector;
    AppendChunk(sector, levelhierarchy12::kSectorRootChunk, root);
    std::vector<uint8_t> sectors;
    AppendChunk(sectors, 0, sector);
    AppendChunk(level, levelhierarchy12::kLevelSectorsChunk, sectors);
    return level;
}

std::vector<uint8_t> MakeUniqueLevel(uint32_t count)
{
    std::vector<uint32_t> ids;
    ids.reserve(count);
    for (uint32_t id = 1; id <= count; ++id)
        ids.push_back(id);
    return MakeLevelFromIds(ids);
}

struct Timing
{
    bool ok = false;
    double minMs = 0.0;
    double medianMs = 0.0;
    uint32_t iterations = 0;
};

Timing Measure(const std::vector<uint8_t>& level, uint32_t iterations)
{
    levelhierarchy12::LevelHierarchy hierarchy;
    std::string reason;
    Timing timing;
    timing.iterations = iterations;

    if (!levelhierarchy12::Parse(level, hierarchy, reason))
        return timing;

    std::vector<double> samples;
    samples.reserve(iterations);
    for (uint32_t i = 0; i < iterations; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        const bool ok = levelhierarchy12::Parse(level, hierarchy, reason);
        const auto end = std::chrono::steady_clock::now();
        if (!ok)
            return timing;
        samples.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(samples.begin(), samples.end());
    timing.ok = true;
    timing.minMs = samples.front();
    timing.medianMs = samples[samples.size() / 2];
    return timing;
}

bool ExpectDuplicate(const char* name, const std::vector<uint32_t>& ids)
{
    levelhierarchy12::LevelHierarchy hierarchy;
    std::string reason;
    const bool parsed = levelhierarchy12::Parse(MakeLevelFromIds(ids), hierarchy,
                                                reason);
    const bool ok = !parsed && reason == "duplicate visual id";
    std::printf("PARSER_DUP %-8s result=%s reason='%s'\n", name,
                ok ? "PASS" : "FAIL", reason.c_str());
    return ok;
}

bool TestAllocationFailure(const std::vector<uint8_t>& level)
{
    levelhierarchy12::LevelHierarchy hierarchy;
    hierarchy.visuals.push_back({});
    std::string reason;
    g_injectedAllocations = 0;
    g_failAlloc = true;
    bool failed = false;
    try
    {
        failed = !levelhierarchy12::Parse(level, hierarchy, reason);
    }
    catch (const std::bad_alloc&)
    {
        failed = true;
    }
    g_failAlloc = false;

    const bool ok = failed && g_injectedAllocations > 0 &&
                    hierarchy.visuals.empty() && hierarchy.sectors.empty();
    std::printf("PARSER_ALLOC_FAIL result=%s injected=%u output_empty=%d\n",
                ok ? "PASS" : "FAIL", g_injectedAllocations,
                (hierarchy.visuals.empty() && hierarchy.sectors.empty()) ? 1
                                                                         : 0);
    return ok;
}

unsigned CountParseAllocations(const std::vector<uint8_t>& level,
                               bool& parsed)
{
    levelhierarchy12::LevelHierarchy hierarchy;
    std::string reason;
    g_allocationCount = 0;
    g_countAlloc = true;
    parsed = levelhierarchy12::Parse(level, hierarchy, reason);
    g_countAlloc = false;
    return g_allocationCount;
}

bool TestAllocationFailureAtCapacity(const std::vector<uint8_t>& level,
                                     unsigned normalAllocations)
{
    levelhierarchy12::LevelHierarchy hierarchy;
    hierarchy.visuals.push_back({});
    std::string reason;
    g_injectedAllocations = 0;
    g_failAfter = normalAllocations == 0 ? 1 : normalAllocations;
    bool failed = false;
    try
    {
        failed = !levelhierarchy12::Parse(level, hierarchy, reason);
    }
    catch (const std::bad_alloc&)
    {
        failed = true;
    }
    g_failAfter = 0;

    const bool ok = failed && g_injectedAllocations > 0 &&
                    hierarchy.visuals.empty() && hierarchy.sectors.empty();
    std::printf(
        "PARSER_ALLOC_BOUNDARY result=%s injected=%u output_empty=%d\n",
        ok ? "PASS" : "FAIL", g_injectedAllocations,
        (hierarchy.visuals.empty() && hierarchy.sectors.empty()) ? 1 : 0);
    return ok;
}
} // namespace

void* operator new(std::size_t size)
{
    if (g_countAlloc)
        ++g_allocationCount;
    if (g_failAlloc)
    {
        ++g_injectedAllocations;
        throw std::bad_alloc();
    }
    if (g_failAfter != 0 && --g_failAfter == 0)
    {
        ++g_injectedAllocations;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return ::operator new(size, std::nothrow);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

int main()
{
    bool allOk = true;
    const uint32_t sizes[] = {1024, 4096, 16384, 65536};
    for (const uint32_t size : sizes)
    {
        const std::vector<uint8_t> level = MakeUniqueLevel(size);
        const uint32_t iterations = size >= 65536 ? 3u : 5u;
        const Timing timing = Measure(level, iterations);
        const bool ok = timing.ok && level.size() > 0;
        allOk &= ok;
        std::printf(
            "PARSER_SIZE %u bytes=%zu iterations=%u min_ms=%.3f "
            "median_ms=%.3f result=%s\n",
            size, level.size(), timing.iterations, timing.minMs,
            timing.medianMs, ok ? "PASS" : "FAIL");
    }

    std::vector<uint32_t> ids;
    ids.reserve(1025);
    for (uint32_t id = 1; id <= 1024; ++id)
        ids.push_back(id);

    std::vector<uint32_t> duplicateFirst = ids;
    duplicateFirst.insert(duplicateFirst.begin() + 1, 1);
    std::vector<uint32_t> duplicateMiddle = ids;
    duplicateMiddle.insert(duplicateMiddle.begin() + 512, 512);
    std::vector<uint32_t> duplicateLast = ids;
    duplicateLast.push_back(1024);

    allOk &= ExpectDuplicate("first", duplicateFirst);
    allOk &= ExpectDuplicate("middle", duplicateMiddle);
    allOk &= ExpectDuplicate("last", duplicateLast);

    {
        const std::vector<uint32_t> orderIds = {300, 100, 200};
        levelhierarchy12::LevelHierarchy hierarchy;
        std::string reason;
        const bool parsed = levelhierarchy12::Parse(
            MakeLevelFromIds(orderIds), hierarchy, reason);
        const bool ok = parsed && hierarchy.visuals.size() == 3 &&
                        hierarchy.visuals[0].id == 300 &&
                        hierarchy.visuals[1].id == 100 &&
                        hierarchy.visuals[2].id == 200;
        allOk &= ok;
        std::printf("PARSER_ORDER result=%s\n", ok ? "PASS" : "FAIL");
    }

    {
        levelhierarchy12::LevelHierarchy hierarchy;
        std::string reason;
        const bool parsed = levelhierarchy12::Parse(
            MakeUniqueLevel(levelhierarchy12::kMaxVisuals), hierarchy, reason);
        const bool ok = parsed && hierarchy.visuals.size() ==
                                      levelhierarchy12::kMaxVisuals;
        allOk &= ok;
        std::printf("PARSER_CAPACITY max=%u result=%s\n",
                    levelhierarchy12::kMaxVisuals, ok ? "PASS" : "FAIL");
    }
    {
        levelhierarchy12::LevelHierarchy hierarchy;
        std::string reason;
        const bool parsed = levelhierarchy12::Parse(
            MakeUniqueLevel(levelhierarchy12::kMaxVisuals + 1), hierarchy,
            reason);
        const bool ok = !parsed && reason == "visual count exceeds limit";
        allOk &= ok;
        std::printf("PARSER_OVER_CAPACITY result=%s reason='%s'\n",
                    ok ? "PASS" : "FAIL", reason.c_str());
    }
    {
        std::vector<uint32_t> idsAtCapacity;
        idsAtCapacity.reserve(levelhierarchy12::kMaxVisuals + 1);
        for (uint32_t id = 1; id <= levelhierarchy12::kMaxVisuals; ++id)
            idsAtCapacity.push_back(id);
        idsAtCapacity.push_back(levelhierarchy12::kMaxVisuals);
        levelhierarchy12::LevelHierarchy hierarchy;
        std::string reason;
        const bool parsed = levelhierarchy12::Parse(
            MakeLevelFromIds(idsAtCapacity), hierarchy, reason);
        const bool ok = !parsed && reason == "duplicate visual id";
        allOk &= ok;
        std::printf("PARSER_DUP_AT_CAPACITY result=%s reason='%s'\n",
                    ok ? "PASS" : "FAIL", reason.c_str());
    }
    {
        const std::vector<uint8_t> atCapacity =
            MakeUniqueLevel(levelhierarchy12::kMaxVisuals);
        const std::vector<uint8_t> overCapacity =
            MakeUniqueLevel(levelhierarchy12::kMaxVisuals + 1);
        std::vector<uint32_t> idsAtCapacity;
        idsAtCapacity.reserve(levelhierarchy12::kMaxVisuals + 1);
        for (uint32_t id = 1; id <= levelhierarchy12::kMaxVisuals; ++id)
            idsAtCapacity.push_back(id);
        idsAtCapacity.push_back(levelhierarchy12::kMaxVisuals);
        const std::vector<uint8_t> duplicateAtCapacity =
            MakeLevelFromIds(idsAtCapacity);

        bool parsedAtCapacity = false;
        bool parsedOverCapacity = false;
        bool parsedDuplicateAtCapacity = false;
        const unsigned capacityAllocations =
            CountParseAllocations(atCapacity, parsedAtCapacity);
        const unsigned overCapacityAllocations =
            CountParseAllocations(overCapacity, parsedOverCapacity);
        const unsigned duplicateCapacityAllocations =
            CountParseAllocations(duplicateAtCapacity,
                                  parsedDuplicateAtCapacity);
        const bool ok = parsedAtCapacity && !parsedOverCapacity &&
                        !parsedDuplicateAtCapacity &&
                        overCapacityAllocations ==
                            duplicateCapacityAllocations;
        allOk &= ok;
        std::printf(
            "PARSER_CAPACITY_ALLOC capacity=%u over=%u duplicate=%u "
            "equal=%d result=%s\n",
            capacityAllocations, overCapacityAllocations,
            duplicateCapacityAllocations,
            overCapacityAllocations == duplicateCapacityAllocations ? 1 : 0,
            ok ? "PASS" : "FAIL");
        allOk &=
            TestAllocationFailureAtCapacity(atCapacity, capacityAllocations);
    }
    {
        std::vector<uint8_t> malformed = MakeUniqueLevel(1024);
        malformed.pop_back();
        levelhierarchy12::LevelHierarchy hierarchy;
        std::string reason;
        const bool parsed = levelhierarchy12::Parse(malformed, hierarchy, reason);
        const bool ok = !parsed;
        allOk &= ok;
        std::printf("PARSER_MALFORMED result=%s reason='%s'\n",
                    ok ? "PASS" : "FAIL", reason.c_str());
    }

    allOk &= TestAllocationFailure(MakeUniqueLevel(1024));

    std::printf("J2A_PARSER_SCALING_RESULT %s\n", allOk ? "PASS" : "FAIL");
    return allOk ? 0 : 1;
}
