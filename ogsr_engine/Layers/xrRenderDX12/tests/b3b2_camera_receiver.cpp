// F4.0-B3-B2A standalone CPU-only receiver for the published B2.3 camera
// stream. It never links the Engine and never creates a D3D11/D3D12 device.

#include "b3b2_camera_stream.h"
#include "dx12cameracapturefile.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>

using b3b2::CameraStreamClient;

namespace
{
std::atomic<bool> g_stop{false};

BOOL WINAPI ConsoleHandler(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT)
    {
        g_stop.store(true);
        return TRUE;
    }
    return FALSE;
}

uint64_t QpcNow() noexcept
{
    LARGE_INTEGER value{};
    ::QueryPerformanceCounter(&value);
    return static_cast<uint64_t>(value.QuadPart);
}

uint64_t QpcFrequency() noexcept
{
    LARGE_INTEGER value{};
    ::QueryPerformanceFrequency(&value);
    return value.QuadPart > 0 ? static_cast<uint64_t>(value.QuadPart) : 1ull;
}

double QpcDeltaMs(uint64_t start, uint64_t end) noexcept
{
    if (end <= start)
        return 0.0;
    return static_cast<double>(end - start) * 1000.0 /
           static_cast<double>(QpcFrequency());
}

uint64_t Percentile(std::vector<uint64_t>& values, double fraction)
{
    if (values.empty())
        return 0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(
        (static_cast<double>(values.size()) - 1.0) * fraction);
    return values[std::min(index, values.size() - 1)];
}

struct Options
{
    std::wstring pipeName;
    const char* telemetryPath = nullptr;
    const char* summaryPath = nullptr;
    DWORD durationMs = 15000;
    DWORD connectTimeoutMs = 5000;
    DWORD readTimeoutMs = 2000;
    uint64_t maxFrames = 0;
};

struct ReceiverStats
{
    uint64_t connections = 0;
    uint64_t disconnects = 0;
    uint64_t messages = 0;
    uint64_t helloMessages = 0;
    uint64_t invalidateMessages = 0;
    uint64_t shutdownMessages = 0;
    uint64_t cameraMessages = 0;
    uint64_t acceptedFrames = 0;
    uint64_t decodeErrors = 0;
    uint64_t crcErrors = 0;
    uint64_t protocolErrors = 0;
    uint64_t rejectedPackets = 0;
    uint64_t duplicatePackets = 0;
    uint64_t reorderedPackets = 0;
    uint64_t staleSessionPackets = 0;
    uint64_t staleEpochPackets = 0;
    uint64_t gapFrames = 0;
    uint64_t firstAgeMs = 0;
    uint64_t lastAgeMs = 0;
    uint64_t maxAgeMs = 0;
    std::vector<uint64_t> agesMs;
    uint64_t lastSession = 0;
    uint64_t lastEpoch = 0;
    uint64_t lastSequence = 0;
    uint32_t producerPid = 0;
    uint64_t producerStartId = 0;
    bool sawValidCamera = false;
};

bool ParseWide(const char* text, std::wstring& out)
{
    if (text == nullptr || text[0] == '\0')
        return false;
    const int length = static_cast<int>(std::strlen(text));
    const int required = ::MultiByteToWideChar(CP_UTF8, 0, text, length,
                                               nullptr, 0);
    if (required <= 0)
        return false;
    out.resize(static_cast<size_t>(required));
    return ::MultiByteToWideChar(CP_UTF8, 0, text, length, out.data(),
                                 required) == required;
}

bool ParseOptions(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        const char* arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc)
            {
                std::printf("missing value for %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (std::strcmp(arg, "--pipe") == 0)
        {
            const char* value = next("--pipe");
            if (value == nullptr || !ParseWide(value, options.pipeName))
                return false;
        }
        else if (std::strcmp(arg, "--telemetry") == 0)
        {
            options.telemetryPath = next("--telemetry");
            if (options.telemetryPath == nullptr)
                return false;
        }
        else if (std::strcmp(arg, "--summary") == 0)
        {
            options.summaryPath = next("--summary");
            if (options.summaryPath == nullptr)
                return false;
        }
        else if (std::strcmp(arg, "--duration-ms") == 0)
        {
            const char* value = next("--duration-ms");
            if (value == nullptr)
                return false;
            options.durationMs = static_cast<DWORD>(std::strtoul(value, nullptr, 10));
        }
        else if (std::strcmp(arg, "--connect-timeout-ms") == 0)
        {
            const char* value = next("--connect-timeout-ms");
            if (value == nullptr)
                return false;
            options.connectTimeoutMs =
                static_cast<DWORD>(std::strtoul(value, nullptr, 10));
        }
        else if (std::strcmp(arg, "--read-timeout-ms") == 0)
        {
            const char* value = next("--read-timeout-ms");
            if (value == nullptr)
                return false;
            options.readTimeoutMs =
                static_cast<DWORD>(std::strtoul(value, nullptr, 10));
        }
        else if (std::strcmp(arg, "--max-frames") == 0)
        {
            const char* value = next("--max-frames");
            if (value == nullptr)
                return false;
            options.maxFrames = std::strtoull(value, nullptr, 10);
        }
        else if (std::strcmp(arg, "--help") == 0)
        {
            std::printf("usage: b3b2_camera_receiver --pipe \\\\.\\pipe\\NAME "
                        "[--duration-ms N] [--max-frames N] "
                        "[--telemetry PATH] [--summary PATH]\n");
            return false;
        }
        else
        {
            std::printf("unknown argument %s\n", arg);
            return false;
        }
    }
    return !options.pipeName.empty();
}

const char* CameraCodecError(cameracapture12::FileError error) noexcept
{
    return cameracapture12::FileErrorMessage(error);
}

void WriteTelemetryHeader(std::FILE* file)
{
    if (file == nullptr)
        return;
    std::fprintf(file,
                 "connection,type,session,epoch,sequence,frame,level,"
                 "viewport_width,viewport_height,hash,age_ms,status\n");
}

void WriteTelemetry(std::FILE* file, uint64_t connection,
                    const char* type, uint64_t session, uint64_t epoch,
                    uint64_t sequence, uint32_t frame, const char* level,
                    uint32_t viewportWidth, uint32_t viewportHeight,
                    uint64_t hash, uint64_t ageMs, const char* status)
{
    if (file == nullptr)
        return;
    std::fprintf(file,
                 "%llu,%s,%llu,%llu,%llu,%u,%s,%u,%u,%016llX,%llu,%s\n",
                 static_cast<unsigned long long>(connection), type,
                 static_cast<unsigned long long>(session),
                 static_cast<unsigned long long>(epoch),
                 static_cast<unsigned long long>(sequence), frame,
                 level ? level : "", viewportWidth, viewportHeight,
                 static_cast<unsigned long long>(hash),
                 static_cast<unsigned long long>(ageMs), status);
}

void LogFirstMatrices(const cameracapture12::CameraCaptureFile12& capture)
{
    for (uint32_t row = 0; row < 4; ++row)
    {
        std::printf("recv view[%u] = %.9g %.9g %.9g %.9g\n", row,
                    capture.input.view.m[row][0],
                    capture.input.view.m[row][1],
                    capture.input.view.m[row][2],
                    capture.input.view.m[row][3]);
        std::printf("recv proj[%u] = %.9g %.9g %.9g %.9g\n", row,
                    capture.input.projection.m[row][0],
                    capture.input.projection.m[row][1],
                    capture.input.projection.m[row][2],
                    capture.input.projection.m[row][3]);
        std::printf("recv full[%u] = %.9g %.9g %.9g %.9g\n", row,
                    capture.input.fullTransform.m[row][0],
                    capture.input.fullTransform.m[row][1],
                    capture.input.fullTransform.m[row][2],
                    capture.input.fullTransform.m[row][3]);
    }
}

int Failure(const char* message)
{
    std::printf("B3B2A_RECEIVER_ERROR %s\n", message);
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
        return Failure("invalid arguments");
    ::SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    std::FILE* telemetry = nullptr;
    if (options.telemetryPath != nullptr)
    {
        telemetry = std::fopen(options.telemetryPath, "wb");
        if (telemetry == nullptr)
            return Failure("cannot open telemetry file");
        WriteTelemetryHeader(telemetry);
    }

    ReceiverStats stats;
    b3b2::ReceiverValidationState validation;
    const uint64_t start = ::GetTickCount64();
    const uint64_t duration = options.durationMs;
    uint64_t connectionGeneration = 0;
    uint64_t lastAcceptedLog = 0;
    bool producerShutdown = false;

    FILETIME creationTime{};
    FILETIME exitTime{};
    FILETIME kernelTimeStart{};
    FILETIME userTimeStart{};
    ::GetProcessTimes(::GetCurrentProcess(), &creationTime, &exitTime,
                      &kernelTimeStart, &userTimeStart);

    CameraStreamClient client;
    while (!g_stop.load() && (::GetTickCount64() - start) < duration)
    {
        if (!client.IsOpen())
        {
            std::string error;
            if (!client.Connect(options.pipeName, options.connectTimeoutMs,
                                error))
            {
                std::printf("receiver_connect_failed reason='%s'\n",
                            error.c_str());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            ++connectionGeneration;
            ++stats.connections;
            validation.BeginConnection(connectionGeneration);
            std::printf("receiver_connected connection=%llu pipe='%ls'\n",
                        static_cast<unsigned long long>(connectionGeneration),
                        options.pipeName.c_str());
        }

        b3b2::StreamMessage message;
        const b3b2::MessageReadStatus readStatus =
            client.Read(message, options.readTimeoutMs);
        if (readStatus == b3b2::MessageReadStatus::Timeout)
            continue;
        if (readStatus == b3b2::MessageReadStatus::Closed)
        {
            ++stats.disconnects;
            client.Close();
            std::printf("receiver_disconnected connection=%llu\n",
                        static_cast<unsigned long long>(connectionGeneration));
            continue;
        }
        if (readStatus != b3b2::MessageReadStatus::Ok)
        {
            ++stats.protocolErrors;
            if (readStatus == b3b2::MessageReadStatus::CrcMismatch)
                ++stats.crcErrors;
            std::printf("receiver_protocol_error status=%s\n",
                        b3b2::MessageReadStatusName(readStatus));
            client.Close();
            continue;
        }

        ++stats.messages;
        if (message.type == b3b2::StreamMessageType::Hello)
        {
            b3b2::HelloPayload hello{};
            if (!b3b2::ParseHello(message.payload, hello))
            {
                ++stats.protocolErrors;
                continue;
            }
            ++stats.helloMessages;
            std::string reason;
            if (!validation.ObserveHello(hello, reason))
            {
                ++stats.rejectedPackets;
                std::printf("receiver_hello_rejected reason='%s'\n",
                            reason.c_str());
                continue;
            }
            stats.producerPid = hello.producerPid;
            stats.producerStartId = hello.producerStartId;
            std::printf("receiver_hello connection=%llu pid=%u start=%016llX "
                        "session=%llu epoch=%llu\n",
                        static_cast<unsigned long long>(connectionGeneration),
                        hello.producerPid,
                        static_cast<unsigned long long>(hello.producerStartId),
                        static_cast<unsigned long long>(hello.levelSession),
                        static_cast<unsigned long long>(hello.captureEpoch));
            WriteTelemetry(telemetry, connectionGeneration, "HELLO",
                           hello.levelSession, hello.captureEpoch, 0, 0, "",
                           0, 0, 0, 0, "ok");
        }
        else if (message.type == b3b2::StreamMessageType::InvalidateSession)
        {
            b3b2::InvalidatePayload invalidate{};
            if (!b3b2::ParseInvalidate(message.payload, invalidate))
            {
                ++stats.protocolErrors;
                continue;
            }
            ++stats.invalidateMessages;
            std::string reason;
            if (!validation.ObserveInvalidate(invalidate, reason))
            {
                ++stats.rejectedPackets;
                std::printf("receiver_invalidate_rejected reason='%s'\n",
                            reason.c_str());
                continue;
            }
            stats.lastSession = invalidate.levelSession;
            stats.lastEpoch = invalidate.captureEpoch;
            std::printf("receiver_invalidate connection=%llu session=%llu "
                        "epoch=%llu reason=%s\n",
                        static_cast<unsigned long long>(connectionGeneration),
                        static_cast<unsigned long long>(invalidate.levelSession),
                        static_cast<unsigned long long>(invalidate.captureEpoch),
                        b3b2::InvalidateReasonName(invalidate.reason));
            WriteTelemetry(telemetry, connectionGeneration, "INVALIDATE",
                           invalidate.levelSession, invalidate.captureEpoch, 0,
                           0, "", 0, 0, 0, 0,
                           b3b2::InvalidateReasonName(invalidate.reason));
            if (invalidate.reason == b3b2::InvalidateReason::Shutdown)
                producerShutdown = true;
        }
        else if (message.type == b3b2::StreamMessageType::Shutdown)
        {
            ++stats.shutdownMessages;
            producerShutdown = true;
            WriteTelemetry(telemetry, connectionGeneration, "SHUTDOWN", 0, 0,
                           0, 0, "", 0, 0, 0, 0, "ok");
            std::printf("receiver_shutdown connection=%llu\n",
                        static_cast<unsigned long long>(connectionGeneration));
        }
        else if (message.type == b3b2::StreamMessageType::CameraFrame)
        {
            ++stats.cameraMessages;
            b3b2::CameraFrameMeta meta{};
            std::span<const uint8_t> codec;
            if (!b3b2::ParseCameraFrame(message.payload, meta, codec))
            {
                ++stats.protocolErrors;
                continue;
            }
            cameracapture12::CameraCaptureFile12 capture{};
            cameracapture12::FileError fileError =
                cameracapture12::FileError::None;
            if (!cameracapture12::Decode(codec, capture, fileError))
            {
                ++stats.decodeErrors;
                std::printf("receiver_decode_error codec=%s\n",
                            CameraCodecError(fileError));
                continue;
            }
            const uint64_t ageMs =
                static_cast<uint64_t>(QpcDeltaMs(meta.publishQpc, QpcNow()));
            stats.firstAgeMs = stats.firstAgeMs == 0 ? ageMs : stats.firstAgeMs;
            stats.lastAgeMs = ageMs;
            stats.maxAgeMs = std::max(stats.maxAgeMs, ageMs);
            if (stats.agesMs.size() < 8192)
                stats.agesMs.push_back(ageMs);

            std::string reason;
            if (!validation.ObserveCamera(capture, reason))
            {
                ++stats.rejectedPackets;
                stats.duplicatePackets = validation.duplicatePackets;
                stats.reorderedPackets = validation.reorderedPackets;
                stats.staleSessionPackets = validation.staleSessionPackets;
                stats.staleEpochPackets = validation.staleEpochPackets;
                if (stats.rejectedPackets <= 3 ||
                    (stats.rejectedPackets % 120) == 0)
                    std::printf("receiver_reject session=%llu epoch=%llu "
                                "sequence=%llu reason='%s'\n",
                                static_cast<unsigned long long>(
                                    capture.input.levelSession),
                                static_cast<unsigned long long>(
                                    capture.input.captureEpoch),
                                static_cast<unsigned long long>(
                                    capture.input.captureSequence),
                                reason.c_str());
                WriteTelemetry(telemetry, connectionGeneration, "CAMERA_REJECT",
                               capture.input.levelSession,
                               capture.input.captureEpoch,
                               capture.input.captureSequence,
                               capture.input.sourceGameFrame, capture.levelName,
                               capture.input.viewportWidth,
                               capture.input.viewportHeight,
                               b3b2::ComputePacketHash(capture), ageMs,
                               reason.c_str());
                continue;
            }

            ++stats.acceptedFrames;
            stats.sawValidCamera = true;
            stats.gapFrames = validation.gapFrames;
            stats.duplicatePackets = validation.duplicatePackets;
            stats.reorderedPackets = validation.reorderedPackets;
            stats.staleSessionPackets = validation.staleSessionPackets;
            stats.staleEpochPackets = validation.staleEpochPackets;
            stats.lastSession = capture.input.levelSession;
            stats.lastEpoch = capture.input.captureEpoch;
            stats.lastSequence = capture.input.captureSequence;
            const uint64_t hash = b3b2::ComputePacketHash(capture);
            WriteTelemetry(telemetry, connectionGeneration, "CAMERA",
                           capture.input.levelSession, capture.input.captureEpoch,
                           capture.input.captureSequence,
                           capture.input.sourceGameFrame, capture.levelName,
                           capture.input.viewportWidth,
                           capture.input.viewportHeight, hash, ageMs, "accept");
            if (stats.acceptedFrames <= 3 ||
                (stats.acceptedFrames - lastAcceptedLog) >= 60)
            {
                lastAcceptedLog = stats.acceptedFrames;
                std::printf("receiver_camera connection=%llu session=%llu "
                            "epoch=%llu sequence=%llu frame=%u level='%s' "
                            "position=(%.3f,%.3f,%.3f) viewport=%ux%u "
                            "hash=%016llX age_ms=%llu gaps=%llu\n",
                            static_cast<unsigned long long>(connectionGeneration),
                            static_cast<unsigned long long>(
                                capture.input.levelSession),
                            static_cast<unsigned long long>(
                                capture.input.captureEpoch),
                            static_cast<unsigned long long>(
                                capture.input.captureSequence),
                            capture.input.sourceGameFrame, capture.levelName,
                            capture.input.position.x, capture.input.position.y,
                            capture.input.position.z, capture.input.viewportWidth,
                            capture.input.viewportHeight,
                            static_cast<unsigned long long>(hash),
                            static_cast<unsigned long long>(ageMs),
                            static_cast<unsigned long long>(validation.gapFrames));
            }
            if (stats.acceptedFrames <= 3 ||
                (stats.acceptedFrames % 60) == 0)
                LogFirstMatrices(capture);
            if (options.maxFrames != 0 &&
                stats.acceptedFrames >= options.maxFrames)
                g_stop.store(true);
        }

        if (producerShutdown)
        {
            client.Close();
            break;
        }
    }

    client.Close();
    if (telemetry != nullptr)
        std::fclose(telemetry);

    FILETIME kernelTimeEnd{};
    FILETIME userTimeEnd{};
    ::GetProcessTimes(::GetCurrentProcess(), &creationTime, &exitTime,
                      &kernelTimeEnd, &userTimeEnd);
    auto fileTimeToU64 = [](const FILETIME& time) -> uint64_t {
        ULARGE_INTEGER value{};
        value.LowPart = time.dwLowDateTime;
        value.HighPart = time.dwHighDateTime;
        return value.QuadPart;
    };
    const uint64_t cpu100ns =
        (fileTimeToU64(kernelTimeEnd) - fileTimeToU64(kernelTimeStart)) +
        (fileTimeToU64(userTimeEnd) - fileTimeToU64(userTimeStart));
    const uint64_t cpuMs = cpu100ns / 10000;

    std::vector<uint64_t> ages = stats.agesMs;
    const uint64_t medianAge = Percentile(ages, 0.50);
    const uint64_t p95Age = Percentile(ages, 0.95);
    const uint64_t p99Age = Percentile(ages, 0.99);
    const bool protocolClean = stats.decodeErrors == 0 &&
                               stats.crcErrors == 0 &&
                               stats.protocolErrors == 0;
    const char* streamStatus =
        !stats.sawValidCamera ? "NO_VALID_CAMERA"
                              : (protocolClean ? "PASS" : "FAIL");
    const int exitCode = !stats.sawValidCamera ? 2 : (protocolClean ? 0 : 1);

    std::printf("B3B2A_RECEIVER_SUMMARY connections=%llu valid_frames=%llu "
                "messages=%llu camera=%llu hello=%llu invalidate=%llu "
                "shutdown=%llu gaps=%llu rejected=%llu duplicate=%llu "
                "reordered=%llu stale_session=%llu stale_epoch=%llu "
                "decode_errors=%llu crc_errors=%llu protocol_errors=%llu "
                "age_ms_first=%llu median=%llu p95=%llu p99=%llu max=%llu "
                "cpu_ms=%llu producer_pid=%u producer_start=%016llX "
                "session=%llu epoch=%llu sequence=%llu "
                "stream_status=%s exit=%d\n",
                static_cast<unsigned long long>(stats.connections),
                static_cast<unsigned long long>(stats.acceptedFrames),
                static_cast<unsigned long long>(stats.messages),
                static_cast<unsigned long long>(stats.cameraMessages),
                static_cast<unsigned long long>(stats.helloMessages),
                static_cast<unsigned long long>(stats.invalidateMessages),
                static_cast<unsigned long long>(stats.shutdownMessages),
                static_cast<unsigned long long>(stats.gapFrames),
                static_cast<unsigned long long>(stats.rejectedPackets),
                static_cast<unsigned long long>(stats.duplicatePackets),
                static_cast<unsigned long long>(stats.reorderedPackets),
                static_cast<unsigned long long>(stats.staleSessionPackets),
                static_cast<unsigned long long>(stats.staleEpochPackets),
                static_cast<unsigned long long>(stats.decodeErrors),
                static_cast<unsigned long long>(stats.crcErrors),
                static_cast<unsigned long long>(stats.protocolErrors),
                static_cast<unsigned long long>(stats.firstAgeMs),
                static_cast<unsigned long long>(medianAge),
                static_cast<unsigned long long>(p95Age),
                static_cast<unsigned long long>(p99Age),
                static_cast<unsigned long long>(stats.maxAgeMs),
                static_cast<unsigned long long>(cpuMs), stats.producerPid,
                static_cast<unsigned long long>(stats.producerStartId),
                static_cast<unsigned long long>(stats.lastSession),
                static_cast<unsigned long long>(stats.lastEpoch),
                static_cast<unsigned long long>(stats.lastSequence),
                streamStatus, exitCode);

    if (options.summaryPath != nullptr)
    {
        std::FILE* summary = std::fopen(options.summaryPath, "wb");
        if (summary == nullptr)
        {
            std::printf("receiver_summary_write_failed\n");
            return 1;
        }
        std::fprintf(summary,
                     "B3B2A_RECEIVER_SUMMARY\nconnections=%llu\n"
                     "valid_frames=%llu\nmessages=%llu\ncamera=%llu\n"
                     "hello=%llu\ninvalidate=%llu\nshutdown=%llu\ngaps=%llu\n"
                     "rejected=%llu\nduplicate=%llu\nreordered=%llu\n"
                     "stale_session=%llu\nstale_epoch=%llu\n"
                     "decode_errors=%llu\ncrc_errors=%llu\n"
                     "protocol_errors=%llu\nage_ms_first=%llu\n"
                     "age_ms_median=%llu\nage_ms_p95=%llu\nage_ms_p99=%llu\n"
                     "age_ms_max=%llu\ncpu_ms=%llu\nproducer_pid=%u\n"
                     "producer_start=%016llX\nsession=%llu\nepoch=%llu\n"
                     "sequence=%llu\nstream_status=%s\nexit=%d\n",
                     static_cast<unsigned long long>(stats.connections),
                     static_cast<unsigned long long>(stats.acceptedFrames),
                     static_cast<unsigned long long>(stats.messages),
                     static_cast<unsigned long long>(stats.cameraMessages),
                     static_cast<unsigned long long>(stats.helloMessages),
                     static_cast<unsigned long long>(stats.invalidateMessages),
                     static_cast<unsigned long long>(stats.shutdownMessages),
                     static_cast<unsigned long long>(stats.gapFrames),
                     static_cast<unsigned long long>(stats.rejectedPackets),
                     static_cast<unsigned long long>(stats.duplicatePackets),
                     static_cast<unsigned long long>(stats.reorderedPackets),
                     static_cast<unsigned long long>(stats.staleSessionPackets),
                     static_cast<unsigned long long>(stats.staleEpochPackets),
                     static_cast<unsigned long long>(stats.decodeErrors),
                     static_cast<unsigned long long>(stats.crcErrors),
                     static_cast<unsigned long long>(stats.protocolErrors),
                     static_cast<unsigned long long>(stats.firstAgeMs),
                     static_cast<unsigned long long>(medianAge),
                     static_cast<unsigned long long>(p95Age),
                     static_cast<unsigned long long>(p99Age),
                     static_cast<unsigned long long>(stats.maxAgeMs),
                     static_cast<unsigned long long>(cpuMs), stats.producerPid,
                     static_cast<unsigned long long>(stats.producerStartId),
                     static_cast<unsigned long long>(stats.lastSession),
                     static_cast<unsigned long long>(stats.lastEpoch),
                     static_cast<unsigned long long>(stats.lastSequence),
                     streamStatus, exitCode);
        std::fclose(summary);
    }
    return exitCode;
}
