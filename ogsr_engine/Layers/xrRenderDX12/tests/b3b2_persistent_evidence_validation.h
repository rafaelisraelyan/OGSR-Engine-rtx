#pragma once
// F4.0-B3-B2B Codex P1-P3: portable, fail-closed evidence semantics.
// No DirectX or Win32 dependencies. Tested separately by evidence-validation-cpu.cpp.
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace b3b2evidence
{
using Fields = std::map<std::string, std::string>;

inline bool Unsigned(const std::string& s, uint64_t& out)
{
    if (s.empty()) return false;
    uint64_t value = 0;
    for (char ch : s)
    {
        if (ch < '0' || ch > '9') return false;
        const unsigned d = static_cast<unsigned>(ch - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - d) / 10) return false;
        value = value * 10 + d;
    }
    out = value;
    return true;
}

// Bounded, reversible description bytes for the single-line InfoQueue wire
// format.  The limit is intentionally well above real Debug Layer messages
// while keeping malformed evidence from forcing an unbounded allocation.
constexpr uint64_t kMaxInfoQueueDescriptionBytes = 1u << 20;

inline std::string HexEncodeDescription(const char* data, size_t size)
{
    if (data == nullptr && size != 0) return {};
    if (size > kMaxInfoQueueDescriptionBytes) return {};
    static const char digits[] = "0123456789ABCDEF";
    std::string encoded(size * 2, '0');
    for (size_t i = 0; i < size; ++i)
    {
        const unsigned byte = static_cast<unsigned char>(data[i]);
        encoded[i * 2] = digits[byte >> 4];
        encoded[i * 2 + 1] = digits[byte & 0x0F];
    }
    return encoded;
}

inline bool HexDecodeDescription(const std::string& hex, std::string& decoded)
{
    decoded.clear();
    if (hex.size() % 2 != 0) return false;
    if (hex.size() > kMaxInfoQueueDescriptionBytes * 2) return false;
    auto nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    decoded.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        const int high = nibble(hex[i]);
        const int low = nibble(hex[i + 1]);
        if (high < 0 || low < 0) return false;
        decoded.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

inline bool ParseFields(const std::string& text, Fields& fields,
                        bool allowRepeatedCapabilities = false)
{
    fields.clear();
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Accept a standard UTF-8 BOM on the first manifest/summary field.
        // The BOM remains part of the file hash; only the first key is
        // stripped for parsing.
        if (line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF)
            line.erase(0, 3);
        if (line.empty()) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 > line.size()) return false;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        const auto inserted = fields.emplace(key, value);
        if (!inserted.second && !(allowRepeatedCapabilities &&
            (key == "debug_layer_available" || key == "gbv_requested") &&
            inserted.first->second == value)) return false;
    }
    return !input.bad();
}

inline bool Get(const Fields& fields, const char* key, std::string& value)
{
    auto it = fields.find(key);
    if (it == fields.end()) return false;
    value = it->second;
    return true;
}

inline bool GetU64(const Fields& fields, const char* key, uint64_t& value)
{
    std::string text;
    return Get(fields, key, text) && Unsigned(text, value);
}

inline bool U64Equals(const Fields& fields, const char* key, uint64_t expected)
{
    uint64_t actual = 0;
    return GetU64(fields, key, actual) && actual == expected;
}

inline bool IsSha256(const std::string& value)
{
    if (value.size() != 64) return false;
    for (char ch : value)
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
    return true;
}

inline bool CsvFields(const std::string& line, std::vector<std::string>& columns)
{
    columns.clear();
    std::string field;
    bool quoted = false;
    bool justClosed = false;
    bool atStart = true;
    for (size_t i = 0; i < line.size(); ++i)
    {
        const char ch = line[i];
        if (quoted)
        {
            if (ch == '"')
            {
                if (i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
                else { quoted = false; justClosed = true; }
            }
            else field += ch;
        }
        else if (ch == ',')
        {
            columns.push_back(field);
            field.clear(); atStart = true; justClosed = false;
        }
        else if (ch == '"' && atStart)
        {
            quoted = true; atStart = false;
        }
        else
        {
            if (justClosed || ch == '"') return false;
            field += ch; atStart = false;
        }
    }
    if (quoted) return false;
    columns.push_back(field);
    return true;
}

inline constexpr const char* kCsvHeader =
    "run_id,frame_index,kind,connection_generation,level_session,"
    "capture_epoch,capture_sequence,source_game_frame,camera_hash,"
    "camera_sha256,viewport,position,frame_received_qpc,gpu_submit_qpc,"
    "gpu_completion_qpc,frame_age_ms,camera_decode_us,draw_record_us,"
    "gpu_wait_us,fence_value,sample,coverage,non_clear_pixels,bbox,"
    "depth_min,depth_max,status";

struct CsvSample
{
    uint64_t frameIndex = 0;
    uint64_t cameraHashValue = 0;
    uint64_t nonClearPixels = 0;
    uint64_t bboxMinX = 0, bboxMinY = 0, bboxMaxX = 0, bboxMaxY = 0;
    double depthMin = 0.0, depthMax = 0.0;
    std::string coverage;
    std::string levelSession, captureEpoch, captureSequence, sourceGameFrame;
    std::string cameraSha, fenceValue, viewport;
};
struct CsvRow
{
    uint64_t frameIndex = 0;
    uint64_t connectionGeneration = 0;
    uint64_t levelSession = 0;
    uint64_t captureEpoch = 0;
    uint64_t captureSequence = 0;
    uint64_t sourceGameFrame = 0;
    uint64_t cameraHashValue = 0;
    uint64_t fenceValue = 0;
    uint64_t nonClearPixels = 0;
    uint64_t bboxMinX = 0, bboxMinY = 0, bboxMaxX = 0, bboxMaxY = 0;
    double frameAgeMs = 0.0;
    double depthMin = 0.0;
    double depthMax = 0.0;
    std::string kind;
    bool sampled = false;
    std::string cameraSha;
    std::string viewport;
    std::string position;
    std::string coverage;
    std::string status;
};

// Independent per-frame producer evidence.  These logs are written by the
// DX12 consumer, not by the sampled-artifact writer, so the verifier can
// reconstruct the mandatory frame/readback set without trusting
// summary.txt, persistent-counters.txt or sampled/evidence-manifest.txt.
struct CameraAuditRecord
{
    uint64_t frameIndex = 0;
    uint64_t levelSession = 0;
    uint64_t captureEpoch = 0;
    uint64_t captureSequence = 0;
    uint64_t cameraHashValue = 0;
    uint64_t fenceValue = 0;
    std::string status;
};

struct ReadbackAuditRecord
{
    uint64_t frameIndex = 0;
    uint64_t levelSession = 0;
    uint64_t captureEpoch = 0;
    uint64_t captureSequence = 0;
    uint32_t width = 0, height = 0;
    uint64_t nonClearPixels = 0;
    uint64_t bboxMinX = 0, bboxMinY = 0, bboxMaxX = 0, bboxMaxY = 0;
    double depthMin = 0.0;
    double depthMax = 0.0;
    std::string confidence;
    std::string status;
};
struct CsvEvidence
{
    uint64_t frames = 0;
    uint64_t sampled = 0;
    uint64_t visible = 0;
    uint64_t lastFrames = 0;
    std::set<uint64_t> indices;
    std::vector<CsvSample> samples;
    std::vector<CsvRow> rows;
};

inline bool StrictDecimal(const std::string& value, double* parsed = nullptr)
{
    if (value.empty()) return false;
    size_t i = 0;
    if (value[i] == '-') ++i;
    else if (value[i] == '+') return false;
    bool digits = false;
    while (i < value.size() && value[i] >= '0' && value[i] <= '9')
    {
        digits = true;
        ++i;
    }
    if (i < value.size() && value[i] == '.')
    {
        ++i;
        while (i < value.size() && value[i] >= '0' && value[i] <= '9')
        {
            digits = true;
            ++i;
        }
    }
    if (!digits) return false;
    if (i < value.size() && (value[i] == 'e' || value[i] == 'E'))
    {
        ++i;
        if (i < value.size() && (value[i] == '+' || value[i] == '-')) ++i;
        const size_t exponentStart = i;
        while (i < value.size() && value[i] >= '0' && value[i] <= '9') ++i;
        if (i == exponentStart) return false;
    }
    if (i != value.size()) return false;
    char* end = nullptr;
    errno = 0;
    const double number = std::strtod(value.c_str(), &end);
    if (end != value.c_str() + value.size() || !std::isfinite(number) ||
        errno == ERANGE)
        return false;
    if (parsed) *parsed = number;
    return true;
}

// Non-negative numeric fields must reject negative zero as well as real
// negative values; "-1e-9999" underflows to -0.0 and is still invalid.
inline bool NonNegativeDecimal(const std::string& value, double* parsed = nullptr)
{
    double number = 0.0;
    if (!StrictDecimal(value, &number)) return false;
    if (number < 0.0 || (number == 0.0 && std::signbit(number))) return false;
    if (parsed) *parsed = number;
    return true;
}

inline bool Position3(const std::string& value, double& x, double& y, double& z)
{
    if (value.size() < 5 || value.front() != '(' || value.back() != ')')
        return false;
    const std::string inner = value.substr(1, value.size() - 2);
    const size_t first = inner.find(',');
    if (first == std::string::npos) return false;
    const size_t second = inner.find(',', first + 1);
    if (second == std::string::npos ||
        inner.find(',', second + 1) != std::string::npos) return false;
    return StrictDecimal(inner.substr(0, first), &x) &&
           StrictDecimal(inner.substr(first + 1, second - first - 1), &y) &&
           StrictDecimal(inner.substr(second + 1), &z);
}

// Canonical writer output is 16 hex characters, but earlier genuine writer
// builds emitted uint64 without zero padding. Both are the same numeric
// identity; malformed, empty, signed, prefixed or >16-digit values are not.
inline bool HexU64(const std::string& value, uint64_t& out)
{
    if (value.empty() || value.size() > 16) return false;
    uint64_t parsed = 0;
    for (char ch : value)
    {
        unsigned digit = 0;
        if (ch >= '0' && ch <= '9') digit = static_cast<unsigned>(ch - '0');
        else if (ch >= 'a' && ch <= 'f') digit = static_cast<unsigned>(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') digit = static_cast<unsigned>(ch - 'A' + 10);
        else return false;
        parsed = (parsed << 4) | digit;
    }
    out = parsed;
    return true;
}

inline bool BoundingBox(const std::string& value, uint32_t width, uint32_t height,
                        uint64_t nonClear, uint64_t& minX, uint64_t& minY,
                        uint64_t& maxX, uint64_t& maxY)
{
    const size_t firstComma = value.find(',');
    const size_t dots = value.find("..", firstComma == std::string::npos ? 0 : firstComma + 1);
    const size_t lastComma = value.rfind(',');
    if (firstComma == std::string::npos || dots == std::string::npos ||
        lastComma == std::string::npos || !(firstComma < dots) ||
        !(dots + 2 <= lastComma))
        return false;
    const std::string a = value.substr(0, firstComma);
    const std::string b = value.substr(firstComma + 1, dots - firstComma - 1);
    const std::string c = value.substr(dots + 2, lastComma - dots - 2);
    const std::string d = value.substr(lastComma + 1);
    if (!Unsigned(a, minX) || !Unsigned(b, minY) || !Unsigned(c, maxX) ||
        !Unsigned(d, maxY))
        return false;
    if (nonClear == 0)
    {
        const bool zeroSentinel = minX == 0 && minY == 0 && maxX == 0 && maxY == 0;
        const bool emptyImageSentinel = minX == width && minY == height &&
                                        maxX == 0 && maxY == 0;
        return zeroSentinel || emptyImageSentinel;
    }
    if (minX > maxX || minY > maxY || maxX >= width || maxY >= height)
        return false;
    return true;
}

inline bool Viewport(const std::string& value, uint32_t& width, uint32_t& height);

inline bool CsvValid(const std::string& text, const std::string& runId,
                     uint64_t expectedFrames, CsvEvidence& result)
{
    result = {};
    std::istringstream stream(text);
    std::string line;
    if (!std::getline(stream, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != kCsvHeader) return false;
    std::vector<std::string> c;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || !CsvFields(line, c) || c.size() != 27) return false;
        for (unsigned char ch : line)
            if (ch < 0x20 || ch == 0x7F) return false;
        if (c[0] != runId || (c[2] != "offline" && c[2] != "live" && c[2] != "last")) return false;
        uint64_t frameIndex = 0;
        uint64_t connectionGeneration = 0, levelSession = 0, captureEpoch = 0;
        uint64_t captureSequence = 0, sourceGameFrame = 0;
        uint64_t frameReceived = 0, submit = 0, completion = 0;
        uint64_t decodeUs = 0, drawUs = 0, waitUs = 0, fence = 0, nonClear = 0;
        if (!Unsigned(c[1], frameIndex) || frameIndex == 0 ||
            frameIndex > expectedFrames || !result.indices.insert(frameIndex).second ||
            !Unsigned(c[3], connectionGeneration) || connectionGeneration == 0 ||
            !Unsigned(c[4], levelSession) || levelSession == 0 ||
            !Unsigned(c[5], captureEpoch) || captureEpoch == 0 ||
            !Unsigned(c[6], captureSequence) || captureSequence == 0 ||
            !Unsigned(c[7], sourceGameFrame) || sourceGameFrame == 0 ||
            !Unsigned(c[12], frameReceived) || frameReceived == 0 ||
            !Unsigned(c[13], submit) || submit == 0 ||
            !Unsigned(c[14], completion) || completion < submit ||
            frameReceived > submit ||
            !Unsigned(c[16], decodeUs) || !Unsigned(c[17], drawUs) ||
            !Unsigned(c[18], waitUs) || !Unsigned(c[19], fence) ||
            !Unsigned(c[22], nonClear))
            return false;
        if (c[20] != "0" && c[20] != "1") return false;
        uint32_t viewportWidth = 0, viewportHeight = 0;
        double frameAge = 0.0, depthMin = 0.0, depthMax = 0.0;
        double positionX = 0.0, positionY = 0.0, positionZ = 0.0;
        uint64_t cameraHashValue = 0;
        uint64_t bboxMinX = 0, bboxMinY = 0, bboxMaxX = 0, bboxMaxY = 0;
        if (!Viewport(c[10], viewportWidth, viewportHeight) ||
            !HexU64(c[8], cameraHashValue) || cameraHashValue == 0 ||
            !IsSha256(c[9]) ||
            !BoundingBox(c[23], viewportWidth, viewportHeight, nonClear,
                         bboxMinX, bboxMinY, bboxMaxX, bboxMaxY) ||
            !NonNegativeDecimal(c[15], &frameAge) || frameAge > 86400000.0 ||
            !NonNegativeDecimal(c[24], &depthMin) ||
            !NonNegativeDecimal(c[25], &depthMax) ||
            depthMin > 1.0 || depthMax > 1.0 || depthMin > depthMax ||
            !Position3(c[11], positionX, positionY, positionZ))
            return false;
        if (fence == 0) return false;
        if (static_cast<uint64_t>(viewportWidth) * viewportHeight < nonClear)
            return false;
        const bool sampled = (c[20] == "1");
        if (c[2] == "last")
        {
            if (!sampled || frameIndex != expectedFrames || ++result.lastFrames > 1)
                return false;
        }
        if (sampled)
        {
            ++result.sampled;
            if (c[26] != c[21] ||
                (c[21] != "VISIBLE_DRAW_VERIFIED" &&
                 c[21] != "NO_PIXEL_COVERAGE" &&
                 c[21] != "OUT_OF_FRUSTUM" &&
                 c[21] != "VISIBILITY_NOT_VERIFIED")) return false;
            if (c[21] == "VISIBLE_DRAW_VERIFIED" && nonClear == 0) return false;
            if ((c[21] == "NO_PIXEL_COVERAGE" || c[21] == "OUT_OF_FRUSTUM") &&
                nonClear != 0) return false;
            CsvSample sample;
            sample.frameIndex = frameIndex;
            sample.cameraHashValue = cameraHashValue;
            sample.nonClearPixels = nonClear;
            sample.bboxMinX = bboxMinX;
            sample.bboxMinY = bboxMinY;
            sample.bboxMaxX = bboxMaxX;
            sample.bboxMaxY = bboxMaxY;
            sample.depthMin = depthMin;
            sample.depthMax = depthMax;
            sample.coverage = c[21];
            sample.levelSession = c[4];
            sample.captureEpoch = c[5];
            sample.captureSequence = c[6];
            sample.sourceGameFrame = c[7];
            sample.cameraSha = c[9];
            sample.fenceValue = c[19];
            sample.viewport = c[10];
            result.samples.push_back(sample);
        }
        else if (c[21] != "GPU_COMPLETED_NOT_READBACK_VERIFIED" ||
                 c[26] != "GPU_COMPLETED_NOT_READBACK_VERIFIED" ||
                 nonClear != 0) return false;
        if (c[26] == "VISIBLE_DRAW_VERIFIED") ++result.visible;
        CsvRow row;
        row.frameIndex = frameIndex;
        row.connectionGeneration = connectionGeneration;
        row.levelSession = levelSession;
        row.captureEpoch = captureEpoch;
        row.captureSequence = captureSequence;
        row.sourceGameFrame = sourceGameFrame;
        row.cameraHashValue = cameraHashValue;
        row.fenceValue = fence;
        row.nonClearPixels = nonClear;
        row.bboxMinX = bboxMinX;
        row.bboxMinY = bboxMinY;
        row.bboxMaxX = bboxMaxX;
        row.bboxMaxY = bboxMaxY;
        row.frameAgeMs = frameAge;
        row.depthMin = depthMin;
        row.depthMax = depthMax;
        row.kind = c[2];
        row.sampled = sampled;
        row.cameraSha = c[9];
        row.viewport = c[10];
        row.position = c[11];
        row.coverage = c[21];
        row.status = c[26];
        result.rows.push_back(std::move(row));
        ++result.frames;
    }
    if (stream.bad() || result.frames != expectedFrames) return false;
    for (uint64_t i = 1; i <= expectedFrames; ++i)
        if (!result.indices.count(i)) return false;
    std::sort(result.samples.begin(), result.samples.end(),
              [](const CsvSample& a, const CsvSample& b) { return a.frameIndex < b.frameIndex; });
    return expectedFrames > 0 && result.visible > 0;
}

// The sampled set is a property of the mode and the sampling policy, not of
// the mutable sampled counter. Offline runs sample every rendered frame; live
// runs sample the first frame, every Nth frame, and either the final accepted
// frame or the extra kind=last frame. The CSV rows must agree exactly.
inline bool ExpectedSamples(const CsvEvidence& csv, const std::string& mode,
                            uint64_t sampleEvery, std::set<uint64_t>& expected)
{
    expected.clear();
    if (csv.rows.empty()) return false;
    if (mode == "offline")
    {
        if (csv.lastFrames != 0) return false;
        for (const CsvRow& row : csv.rows)
        {
            if (row.kind != "offline" || !row.sampled ||
                row.fenceValue != row.frameIndex + 1) return false;
            expected.insert(row.frameIndex);
        }
        return expected.size() == csv.sampled && expected.size() == csv.rows.size();
    }
    if (mode != "live" || sampleEvery > 1000000) return false;
    const uint64_t every = sampleEvery == 0 ? 1 : sampleEvery;
    uint64_t liveCount = 0, lastCount = 0;
    std::set<uint64_t> liveIndices;
    bool lastSampled = false;
    uint64_t lastIndex = 0;
    for (const CsvRow& row : csv.rows)
    {
        if (row.kind == "live")
        {
            ++liveCount;
            liveIndices.insert(row.frameIndex);
        }
        else if (row.kind == "last")
        {
            ++lastCount;
            lastSampled = row.sampled;
            lastIndex = row.frameIndex;
        }
        else return false;
    }
    if (liveCount == 0 || lastCount > 1 || liveIndices.size() != liveCount)
        return false;
    for (uint64_t i = 1; i <= liveCount; ++i)
        if (!liveIndices.count(i)) return false;
    const bool hasLast = lastCount == 1;
    if (hasLast && (!lastSampled || lastIndex != liveCount + 1)) return false;
    const CsvRow* finalLive = nullptr;
    for (const CsvRow& row : csv.rows)
        if (row.kind == "live" && row.frameIndex == liveCount) finalLive = &row;
    if (finalLive == nullptr) return false;
    if (hasLast == finalLive->sampled) return false; // last exists only when final live was unsampled
    for (uint64_t i = 1; i <= liveCount; ++i)
    {
        const bool regular = (i == 1) || (i % every == 0);
        const bool wanted = regular || (!hasLast && i == liveCount);
        if (wanted) expected.insert(i);
    }
    if (hasLast) expected.insert(liveCount + 1);
    for (const CsvRow& row : csv.rows)
    {
        const bool wanted = expected.count(row.frameIndex) != 0;
        if (row.sampled != wanted) return false;
        if (row.fenceValue != row.frameIndex + 1) return false;
    }
    return expected.size() == csv.sampled;
}

struct SampleEntry
{
    std::string color, depth, meta;
};
inline bool SampleManifest(const std::string& text, const std::string& runId,
                           uint64_t expected, std::map<uint64_t, SampleEntry>& entries)
{
    entries.clear();
    std::istringstream lines(text);
    std::string line;
    if (!std::getline(lines, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "run_id=" + runId) return false;
    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) return false;
        std::istringstream words(line);
        std::string part;
        std::map<std::string, std::string> values;
        while (words >> part)
        {
            const size_t eq = part.find('=');
            if (eq == std::string::npos || !values.emplace(part.substr(0, eq), part.substr(eq + 1)).second)
                return false;
        }
        if (values.size() != 4 || !values.count("frame") || !values.count("color_sha256") ||
            !values.count("depth_sha256") || !values.count("meta_sha256")) return false;
        uint64_t frame = 0;
        if (!Unsigned(values["frame"], frame) || frame == 0 || frame > expected ||
            !IsSha256(values["color_sha256"]) || !IsSha256(values["depth_sha256"]) ||
            !IsSha256(values["meta_sha256"])) return false;
        if (!entries.emplace(frame, SampleEntry{values["color_sha256"],
                values["depth_sha256"], values["meta_sha256"]}).second) return false;
    }
    if (lines.bad() || entries.size() != expected) return false;
    for (uint64_t i = 1; i <= expected; ++i)
        if (!entries.count(i)) return false;
    return true;
}

inline bool Viewport(const std::string& value, uint32_t& width, uint32_t& height)
{
    const size_t x = value.find('x');
    if (x == std::string::npos) return false;
    uint64_t w = 0, h = 0;
    if (!Unsigned(value.substr(0, x), w) || !Unsigned(value.substr(x + 1), h) ||
        w == 0 || h == 0 || w > 16384 || h > 16384) return false;
    width = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);
    return true;
}

inline bool SampleMetadata(const std::string& text, const std::string& runId,
                          uint64_t frame, uint32_t& w, uint32_t& h,
                          const CsvSample* expected = nullptr)
{
    Fields m;
    std::string viewport, status, coverage, run, hash;
    std::string bbox;
    uint64_t fence = 0, cameraHashValue = 0, nonClear = 0;
    uint64_t bboxMinX = 0, bboxMinY = 0, bboxMaxX = 0, bboxMaxY = 0;
    double depthMin = 0.0, depthMax = 0.0;
    const bool basic = ParseFields(text, m) && Get(m, "run_id", run) && run == runId &&
           U64Equals(m, "frame_index", frame) &&
           Get(m, "viewport", viewport) && Viewport(viewport, w, h) &&
           Get(m, "camera_hash", hash) && HexU64(hash, cameraHashValue) &&
           Get(m, "camera_sha256", hash) && IsSha256(hash) &&
           GetU64(m, "fence_value", fence) && fence > 0 &&
           Get(m, "gpu_status", status) && status == "GPU_COMPLETED" &&
           Get(m, "coverage_status", coverage) &&
           (coverage == "VISIBLE_DRAW_VERIFIED" || coverage == "NO_PIXEL_COVERAGE" ||
            coverage == "OUT_OF_FRUSTUM" || coverage == "VISIBILITY_NOT_VERIFIED") &&
           GetU64(m, "non_clear_pixels", nonClear) &&
           Get(m, "bbox", bbox) &&
           BoundingBox(bbox, w, h, nonClear, bboxMinX, bboxMinY, bboxMaxX, bboxMaxY) &&
           Get(m, "depth_min", hash) && NonNegativeDecimal(hash, &depthMin) &&
           Get(m, "depth_max", hash) && NonNegativeDecimal(hash, &depthMax) &&
           depthMin <= 1.0 && depthMax <= 1.0 && depthMin <= depthMax;
    if (!basic) return false;
    if (!expected) return true;
    const double depthTolerance = 1.0e-4;
    if (coverage != expected->coverage || nonClear != expected->nonClearPixels ||
        bboxMinX != expected->bboxMinX || bboxMinY != expected->bboxMinY ||
        bboxMaxX != expected->bboxMaxX || bboxMaxY != expected->bboxMaxY ||
        cameraHashValue != expected->cameraHashValue ||
        std::fabs(depthMin - expected->depthMin) > depthTolerance ||
        std::fabs(depthMax - expected->depthMax) > depthTolerance)
        return false;
    for (const auto& field : {std::pair<const char*, const std::string*>{"level_session", &expected->levelSession},
                              {"capture_epoch", &expected->captureEpoch},
                              {"capture_sequence", &expected->captureSequence},
                              {"source_game_frame", &expected->sourceGameFrame},
                              {"camera_sha256", &expected->cameraSha},
                              {"fence_value", &expected->fenceValue},
                              {"viewport", &expected->viewport}})
    {
        std::string actual;
        if (!Get(m, field.first, actual) || actual != *field.second) return false;
    }
    return true;
}

inline bool SpaceFields(const std::string& line, Fields& fields)
{
    fields.clear();
    std::istringstream words(line);
    std::string part;
    while (words >> part)
    {
        const size_t eq = part.find('=');
        if (eq == std::string::npos || eq == 0 ||
            !fields.emplace(part.substr(0, eq), part.substr(eq + 1)).second)
            return false;
    }
    return !words.bad() && !fields.empty();
}

inline bool CoverageStatus(const std::string& value)
{
    return value == "VISIBLE_DRAW_VERIFIED" ||
           value == "NO_PIXEL_COVERAGE" ||
           value == "OUT_OF_FRUSTUM" ||
           value == "VISIBILITY_NOT_VERIFIED";
}

inline bool ParseCameraAudit(const std::string& text,
                             std::vector<CameraAuditRecord>& records)
{
    records.clear();
    std::istringstream lines(text);
    std::string line;
    uint64_t expected = 1;
    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) return false;
        Fields values;
        std::string hash, status;
        CameraAuditRecord record;
        if (!SpaceFields(line, values) || values.size() != 7 ||
            !GetU64(values, "frame_index", record.frameIndex) ||
            !GetU64(values, "session", record.levelSession) ||
            !GetU64(values, "epoch", record.captureEpoch) ||
            !GetU64(values, "sequence", record.captureSequence) ||
            !Get(values, "camera_hash", hash) ||
            !HexU64(hash, record.cameraHashValue) ||
            !GetU64(values, "fence", record.fenceValue) ||
            !Get(values, "status", status) || !CoverageStatus(status) ||
            record.frameIndex != expected || record.levelSession == 0 ||
            record.captureEpoch == 0 || record.captureSequence == 0 ||
            record.fenceValue < record.frameIndex + 1 ||
            record.fenceValue > record.frameIndex + 2)
            return false;
        record.status = status;
        records.push_back(record);
        ++expected;
    }
    return !lines.bad() && !records.empty();
}

inline bool ParseReadbackAudit(const std::string& text,
                               std::vector<ReadbackAuditRecord>& records)
{
    records.clear();
    std::istringstream lines(text);
    std::string line;
    uint64_t expected = 1;
    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) return false;
        Fields values;
        std::string viewport, bbox, depthMin, depthMax, confidence, status;
        ReadbackAuditRecord record;
        if (!SpaceFields(line, values) || values.size() != 11 ||
            !GetU64(values, "frame_index", record.frameIndex) ||
            !GetU64(values, "session", record.levelSession) ||
            !GetU64(values, "epoch", record.captureEpoch) ||
            !GetU64(values, "sequence", record.captureSequence) ||
            !Get(values, "viewport", viewport) ||
            !Viewport(viewport, record.width, record.height) ||
            !GetU64(values, "non_clear_pixels", record.nonClearPixels) ||
            !Get(values, "bbox", bbox) ||
            !BoundingBox(bbox, record.width, record.height,
                         record.nonClearPixels, record.bboxMinX,
                         record.bboxMinY, record.bboxMaxX, record.bboxMaxY) ||
            !Get(values, "depth_min", depthMin) ||
            !NonNegativeDecimal(depthMin, &record.depthMin) ||
            !Get(values, "depth_max", depthMax) ||
            !NonNegativeDecimal(depthMax, &record.depthMax) ||
            record.depthMin > 1.0 || record.depthMax > 1.0 ||
            record.depthMin > record.depthMax ||
            !Get(values, "cpu_confidence", confidence) || confidence.empty() ||
            !Get(values, "status", status) || !CoverageStatus(status) ||
            record.frameIndex != expected || record.levelSession == 0 ||
            record.captureEpoch == 0 || record.captureSequence == 0)
            return false;
        record.confidence = confidence;
        record.status = status;
        records.push_back(record);
        ++expected;
    }
    return !lines.bad() && !records.empty();
}

inline bool CsvMatchesCameraAudit(const CsvRow& row,
                                  const CameraAuditRecord& record)
{
    if (row.frameIndex != record.frameIndex ||
        row.levelSession != record.levelSession ||
        row.captureEpoch != record.captureEpoch ||
        row.captureSequence != record.captureSequence ||
        row.cameraHashValue != record.cameraHashValue ||
        !CoverageStatus(record.status))
        return false;
    if (row.kind == "live")
    {
        // The final live camera may be followed by an extra kind=last
        // readback that shares the next fence value.  The audit records the
        // completed fence, so allow one fence of headroom for a live row.
        if (record.fenceValue < row.fenceValue ||
            record.fenceValue > row.fenceValue + 1) return false;
    }
    else if (row.fenceValue != record.fenceValue) return false;
    if (row.sampled && row.coverage != record.status) return false;
    return true;
}

inline bool CsvMatchesReadbackAudit(const CsvRow& row,
                                    const ReadbackAuditRecord& record)
{
    if (row.frameIndex != record.frameIndex ||
        row.levelSession != record.levelSession ||
        row.captureEpoch != record.captureEpoch ||
        row.captureSequence != record.captureSequence ||
        row.viewport != std::to_string(record.width) + "x" +
                            std::to_string(record.height) ||
        !CoverageStatus(record.status))
        return false;
    if (row.sampled &&
        (row.nonClearPixels != record.nonClearPixels ||
         row.bboxMinX != record.bboxMinX || row.bboxMinY != record.bboxMinY ||
         row.bboxMaxX != record.bboxMaxX || row.bboxMaxY != record.bboxMaxY ||
         std::fabs(row.depthMin - record.depthMin) > 1.0e-4 ||
         std::fabs(row.depthMax - record.depthMax) > 1.0e-4 ||
         row.coverage != record.status))
        return false;
    return true;
}

inline bool ValidCounters(const Fields& counters, const Fields* summary,
                          uint64_t expectedFrames, uint64_t expectedSamples)
{
    if (!U64Equals(counters, "frames_completed", expectedFrames) ||
        !U64Equals(counters, "frames_submitted", expectedFrames) ||
        !U64Equals(counters, "frames_sampled", expectedSamples) ||
        !U64Equals(counters, "frames_failed", 0)) return false;
    for (const char* key : {"device_create_count", "queue_create_count", "pso_create_count",
                             "geometry_upload_count", "vertex_buffer_create_count",
                             "index_buffer_create_count"})
        if (!U64Equals(counters, key, 1)) return false;
    if (summary)
    {
        for (const char* key : {"frames_received", "frames_submitted", "frames_completed",
                                "frames_sampled", "device_create_count", "queue_create_count",
                                "pso_create_count", "geometry_upload_count",
                                "vertex_buffer_create_count", "index_buffer_create_count",
                                "render_target_recreate_count"})
        {
            uint64_t a = 0, b = 0;
            if (!GetU64(counters, key, a) || !GetU64(*summary, key, b) || a != b)
                return false;
        }
    }
    return true;
}

inline bool ValidValidation(const Fields& fields)
{
    std::string status;
    if (!Get(fields, "validation_status", status) || status != "PASS") return false;
    std::string removed;
    if (!Get(fields, "device_removal_reason", removed) ||
        removed != "0x00000000") return false;
    for (const char* key : {"infoqueue_available", "debug_layer_available",
                             "gbv_active", "completion_confirmed"})
        if (!U64Equals(fields, key, 1)) return false;
    for (const char* key : {"infoqueue_error", "infoqueue_corruption",
                             "infoqueue_warning", "infoqueue_discarded",
                             "infoqueue_hidden_after_baseline", "infoqueue_retrieval_failures"})
        if (!U64Equals(fields, key, 0)) return false;
    uint64_t stored = 0, enumerated = 0;
    return GetU64(fields, "infoqueue_stored", stored) &&
           GetU64(fields, "infoqueue_enumerated", enumerated) && stored == enumerated;
}

inline bool ValidFenceLog(const std::string& text, uint64_t expectedFrames)
{
    if (expectedFrames == 0 || expectedFrames > 1000000) return false;
    std::istringstream lines(text);
    std::string line;
    uint64_t confirmations = 0;
    uint64_t lastExpected = 0;
    while (std::getline(lines, line))
    {
        if (line.rfind("fence stage=", 0) != 0) continue;
        std::map<std::string, std::string> values;
        std::istringstream words(line);
        std::string word;
        while (words >> word)
        {
            size_t eq = word.find('=');
            if (eq != std::string::npos && !values.emplace(word.substr(0, eq), word.substr(eq + 1)).second)
                return false;
        }
        uint64_t requested = 0, completed = 0;
        if (!values.count("stage") || !values.count("expected") ||
            !values.count("completed") || !values.count("signal") ||
            !values.count("arm") || !values.count("wait") ||
            !values.count("outcome") || !values.count("confirmed") ||
            !values.count("removed") ||
            !Unsigned(values["expected"], requested) ||
            !Unsigned(values["completed"], completed) || requested == 0 ||
            completed == std::numeric_limits<uint64_t>::max() ||
            requested > expectedFrames + 1 || completed > expectedFrames + 1 ||
            completed != requested ||
            requested != confirmations + 1 ||
            requested <= lastExpected || values["confirmed"] != "1" ||
            values["signal"] != "1" || values["arm"] != "1" ||
            values["wait"] != "1" || values["outcome"] != "Completed" ||
            values["removed"] != "0x00000000" ||
            (confirmations == 0 ? values["stage"] != "geometry"
                                : values["stage"] != "camera")) return false;
        lastExpected = requested;
        ++confirmations;
    }
    // Upload is submitted and fenced once before N camera-frame submissions.
    return !lines.bad() && confirmations == expectedFrames + 1;
}

struct InfoQueueCounts
{
    uint64_t total = 0;
    uint64_t info = 0;
    uint64_t warning = 0;
    uint64_t error = 0;
    uint64_t corruption = 0;
    uint64_t message = 0;
};

// D3D12_MESSAGE_SEVERITY values. D3D12_MESSAGE::Severity is the only
// severity authority; D3D12_MESSAGE::pDescription is arbitrary text.
enum InfoQueueSeverity : uint64_t
{
    kInfoQueueSeverityCorruption = 0,
    kInfoQueueSeverityError = 1,
    kInfoQueueSeverityWarning = 2,
    kInfoQueueSeverityInfo = 3,
    kInfoQueueSeverityMessage = 4,
};

inline bool ConsistentInfoQueueHistogram(const InfoQueueCounts& counts)
{
    return counts.total == counts.info + counts.warning + counts.error +
                                counts.corruption + counts.message;
}

// Structural parser for the InfoQueue wire format.  It validates framing,
// duplicate/malformed fields, the D3D12 severity enum and per-severity
// histogram consistency.  It deliberately does not classify text: words such
// as ERROR, WARNING or CORRUPTION inside a description are payload, not
// structured severity.
inline bool ParseInfoQueue(const std::string& text, InfoQueueCounts& counts)
{
    counts = InfoQueueCounts{};
    std::istringstream lines(text);
    std::string line;
    uint64_t nextIndex = 0;
    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        uint64_t index = 0, value = 0, id = 0, descriptionLength = 0;
        auto skipWhitespace = [&](size_t& p) {
            while (p < line.size() &&
                   (line[p] == ' ' || line[p] == '\t')) ++p;
        };
        auto consume = [&](size_t& p, const char* prefix,
                           std::string& out) -> bool {
            skipWhitespace(p);
            const size_t length = std::strlen(prefix);
            if (line.compare(p, length, prefix) != 0) return false;
            p += length;
            const size_t end = line.find_first_of(" \t", p);
            if (end == std::string::npos || end == p) return false;
            out = line.substr(p, end - p);
            p = end;
            return true;
        };

        if (line.rfind("format=v2 ", 0) == 0)
        {
            // v2 is self-framing: description bytes are hex-encoded, so LF,
            // CRLF, tabs and backslashes cannot create continuation records.
            size_t pos = 10;
            std::string indexText, severityText, idText, lengthText;
            if (!consume(pos, "index=", indexText) ||
                !consume(pos, "severity=", severityText) ||
                !consume(pos, "id=", idText) ||
                !consume(pos, "description_length=", lengthText) ||
                !Unsigned(indexText, index) ||
                !Unsigned(severityText, value) ||
                value > kInfoQueueSeverityMessage ||
                !Unsigned(idText, id) ||
                !Unsigned(lengthText, descriptionLength))
                return false;
            constexpr const char kHex[] = "description_hex=";
            const size_t hexPrefix = sizeof(kHex) - 1;
            skipWhitespace(pos);
            if (line.compare(pos, hexPrefix, kHex) != 0) return false;
            pos += hexPrefix;
            const std::string hex = line.substr(pos);
            if (descriptionLength == 0 ||
                descriptionLength > kMaxInfoQueueDescriptionBytes ||
                hex.size() != static_cast<size_t>(descriptionLength) * 2)
                return false;
            std::string payload;
            if (!HexDecodeDescription(hex, payload) ||
                payload.size() != descriptionLength)
                return false;
            const size_t nul = payload.find('\0');
            if (nul != std::string::npos)
            {
                // The API returns a C string whose DescriptionByteLength
                // includes the terminal NUL.  Embedded NULs are ambiguous.
                if (nul + 1 != payload.size()) return false;
                payload.pop_back();
            }
        }
        else if (line.rfind("index=", 0) == 0)
        {
            // Legacy v1: one physical line, raw single-line description.  It
            // remains accepted for historical evidence; multiline v1 is
            // rejected because it has no unambiguous framing.
            size_t pos = 0;
            std::string indexText, severityText, idText, lengthText;
            if (!consume(pos, "index=", indexText) ||
                !consume(pos, "severity=", severityText) ||
                !consume(pos, "id=", idText) ||
                !consume(pos, "description_length=", lengthText) ||
                !Unsigned(indexText, index) ||
                !Unsigned(severityText, value) ||
                value > kInfoQueueSeverityMessage ||
                !Unsigned(idText, id) ||
                !Unsigned(lengthText, descriptionLength))
                return false;
            constexpr const char kDescription[] = "description=";
            const size_t descriptionPrefix = sizeof(kDescription) - 1;
            skipWhitespace(pos);
            if (line.compare(pos, descriptionPrefix, kDescription) != 0)
                return false;
            pos += descriptionPrefix;
            const std::string description = line.substr(pos);
            if (descriptionLength == 0 ||
                descriptionLength > kMaxInfoQueueDescriptionBytes ||
                description.empty() || description.find('\0') != std::string::npos ||
                (description.size() != descriptionLength &&
                 description.size() + 1 != descriptionLength))
                return false;
        }
        else
        {
            // Any non-empty line outside a framed/legacy record is ambiguous
            // evidence.  This also rejects v1 multiline continuations.
            return false;
        }

        if (index != nextIndex) return false;
        ++nextIndex;

        // Structured severity is authoritative.  The decoded description is
        // retained only as opaque payload and is never searched for words.
        switch (value)
        {
        case kInfoQueueSeverityCorruption: ++counts.corruption; break;
        case kInfoQueueSeverityError: ++counts.error; break;
        case kInfoQueueSeverityWarning: ++counts.warning; break;
        case kInfoQueueSeverityInfo: ++counts.info; break;
        case kInfoQueueSeverityMessage: ++counts.message; break;
        default: return false;
        }
        ++counts.total;
    }
    return !lines.bad() && counts.total > 0 &&
           ConsistentInfoQueueHistogram(counts);
}

// Acceptance gate for evidence claiming zero GPU warnings/errors/corruption.
// Parsing is separate so tests and diagnostics can distinguish malformed
// evidence from a structurally valid record that violates the zero policy.
inline bool ValidInfoQueue(const std::string& text,
                           InfoQueueCounts* observed = nullptr)
{
    InfoQueueCounts counts;
    if (!ParseInfoQueue(text, counts)) return false;
    if (observed) *observed = counts;
    return counts.corruption == 0 && counts.error == 0 &&
           counts.warning == 0;
}

inline bool PassSummary(const Fields& summary)
{
    for (const auto& pair : {std::pair<const char*, const char*>{"acceptance_status", "PASS"},
                             {"evidence_status", "PASS"}, {"gpu_status", "PASS"},
                             {"validation_status", "PASS"}, {"visibility_status", "PASS"},
                             {"validation_passed", "1"}, {"status", "PASS"},
                             {"evidence_failures", "0"}, {"render_failures", "0"}})
    {
        std::string value;
        if (!Get(summary, pair.first, value) || value != pair.second) return false;
    }
    uint64_t visible = 0;
    return GetU64(summary, "sampled_visible_frames", visible) && visible > 0;
}
} // namespace b3b2evidence
