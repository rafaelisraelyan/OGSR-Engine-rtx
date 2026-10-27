#include "b3b2_camera_stream.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <new>
#include <sddl.h>
#include <thread>
#include <utility>

namespace b3b2
{
namespace
{
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
std::atomic<StreamFaultSite> g_streamFault{StreamFaultSite::None};
std::atomic<bool> g_preSubmitBlock{false};
std::atomic<bool> g_preSubmitEntered{false};
std::atomic<bool> g_clientClosePause{false};
std::atomic<bool> g_clientClosePauseEntered{false};
std::atomic<bool> g_workerPauseBeforeLoop{false};
std::atomic<bool> g_workerPauseBeforeLoopEntered{false};
std::atomic<bool> g_clientConnectPause{false};
std::atomic<bool> g_clientConnectPauseEntered{false};

bool ConsumeFault(StreamFaultSite site) noexcept
{
    StreamFaultSite expected = site;
    return g_streamFault.compare_exchange_strong(
        expected, StreamFaultSite::None, std::memory_order_acq_rel);
}

void ThrowIfFault(StreamFaultSite site)
{
    if (ConsumeFault(site))
        throw std::bad_alloc();
}
#endif

#ifndef PIPE_REJECT_REMOTE_CLIENTS
#define PIPE_REJECT_REMOTE_CLIENTS 0x00000008
#endif

constexpr DWORD kPipeBufferBytes = 64 * 1024;
constexpr DWORD kWriteTimeoutMs = 1000;
constexpr DWORD kIdleWaitMs = 50;
constexpr DWORD kReconnectDelayMs = 50;

bool IsValidPipeName(const std::wstring& name) noexcept
{
    constexpr wchar_t prefix[] = L"\\\\.\\pipe\\";
    return name.size() > (sizeof(prefix) / sizeof(prefix[0])) - 1 &&
           name.compare(0, (sizeof(prefix) / sizeof(prefix[0])) - 1, prefix) == 0;
}

void PutU16(uint8_t* out, uint16_t value) noexcept
{
    out[0] = static_cast<uint8_t>(value & 0xFF);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void PutU32(uint8_t* out, uint32_t value) noexcept
{
    for (uint32_t i = 0; i < 4; ++i)
        out[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
}

void PutU64(uint8_t* out, uint64_t value) noexcept
{
    for (uint32_t i = 0; i < 8; ++i)
        out[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
}

uint16_t GetU16(const uint8_t* in) noexcept
{
    return static_cast<uint16_t>(in[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(in[1]) << 8);
}

uint32_t GetU32(const uint8_t* in) noexcept
{
    return static_cast<uint32_t>(in[0]) |
           (static_cast<uint32_t>(in[1]) << 8) |
           (static_cast<uint32_t>(in[2]) << 16) |
           (static_cast<uint32_t>(in[3]) << 24);
}

uint64_t GetU64(const uint8_t* in) noexcept
{
    return static_cast<uint64_t>(GetU32(in)) |
           (static_cast<uint64_t>(GetU32(in + 4)) << 32);
}

uint64_t QpcNow() noexcept
{
    LARGE_INTEGER value{};
    ::QueryPerformanceCounter(&value);
    return static_cast<uint64_t>(value.QuadPart);
}

uint64_t QpcFrequency() noexcept
{
    static const uint64_t frequency = []() {
        LARGE_INTEGER value{};
        ::QueryPerformanceFrequency(&value);
        return value.QuadPart > 0 ? static_cast<uint64_t>(value.QuadPart)
                                  : 1ull;
    }();
    return frequency;
}

uint64_t QpcToNanoseconds(uint64_t ticks) noexcept
{
    const uint64_t frequency = QpcFrequency();
    const uint64_t seconds = ticks / frequency;
    const uint64_t remainder = ticks % frequency;
    if (seconds > std::numeric_limits<uint64_t>::max() / 1000000000ull)
        return std::numeric_limits<uint64_t>::max();
    return seconds * 1000000000ull +
           (remainder * 1000000000ull) / frequency;
}

std::string Win32Error(const char* operation)
{
    const DWORD error = ::GetLastError();
    char text[160] = {};
    std::snprintf(text, sizeof(text), "%s failed (win32=%lu)", operation,
                  static_cast<unsigned long>(error));
    return text;
}

HANDLE CreateLocalPipeInstance(const std::wstring& pipeName) noexcept
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    // Local owner, SYSTEM and local administrators only. Remote clients are
    // rejected independently by PIPE_REJECT_REMOTE_CLIENTS.
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)", SDDL_REVISION_1,
            &descriptor, nullptr))
        return INVALID_HANDLE_VALUE;

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;

    HANDLE pipe = ::CreateNamedPipeW(
        pipeName.c_str(),
        PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1, kPipeBufferBytes, kPipeBufferBytes, 0, &attributes);
    ::LocalFree(descriptor);
    return pipe;
}

struct ScopedWinHandle
{
    ScopedWinHandle() = default;
    explicit ScopedWinHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~ScopedWinHandle() { Reset(); }
    ScopedWinHandle(const ScopedWinHandle&) = delete;
    ScopedWinHandle& operator=(const ScopedWinHandle&) = delete;

    HANDLE Get() const noexcept { return handle_; }
    explicit operator bool() const noexcept
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE Release() noexcept
    {
        const HANDLE handle = handle_;
        handle_ = nullptr;
        return handle;
    }

    void Reset(HANDLE handle = nullptr) noexcept
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            ::CloseHandle(handle_);
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};

struct IoCompletion
{
    IoOperationState state = IoOperationState::NotStarted;
    DWORD bytes = 0;
    DWORD error = 0;
    bool timedOut = false;
};

// Wait until the OS reports a terminal result. This function deliberately does
// not return on CancelIoEx alone: CancelIoEx only requests cancellation. The
// OVERLAPPED, event and I/O buffer must stay alive until GetOverlappedResult
// reports success, ERROR_OPERATION_ABORTED, or a confirmed completion state.
IoCompletion WaitForTerminalCompletion(HANDLE pipe, OVERLAPPED& overlapped,
                                       HANDLE ioEvent, HANDLE stopEvent,
                                       DWORD timeoutMs,
                                       bool forceWaitFailed = false) noexcept
{
    IoCompletion result;
    result.state = IoOperationState::Pending;
    HANDLE waitStop = stopEvent;
    DWORD waitTimeout = timeoutMs;
    bool injectWaitFailure = forceWaitFailed;

    for (;;)
    {
        HANDLE waits[2] = {ioEvent, waitStop};
        DWORD wait = ::WaitForMultipleObjects(waitStop ? 2 : 1, waits, FALSE,
                                              waitTimeout);
        if (injectWaitFailure)
        {
            injectWaitFailure = false;
            wait = WAIT_FAILED;
        }
        if (wait == WAIT_OBJECT_0)
        {
            DWORD bytes = 0;
            if (::GetOverlappedResult(pipe, &overlapped, &bytes, FALSE))
            {
                result.state = IoOperationState::Completed;
                result.bytes = bytes;
                return result;
            }
            const DWORD error = ::GetLastError();
            if (error == ERROR_OPERATION_ABORTED)
            {
                result.state = IoOperationState::Cancelled;
                result.error = error;
                return result;
            }
            if (error == ERROR_IO_INCOMPLETE)
                continue;
            if (error == ERROR_NOT_FOUND)
            {
                // ERROR_NOT_FOUND is not proof by itself. Confirm that the
                // event is signaled and the overlapped storage is completed.
                if (HasOverlappedIoCompleted(&overlapped) &&
                    ::WaitForSingleObject(ioEvent, 0) == WAIT_OBJECT_0)
                {
                    result.state = result.state == IoOperationState::CancelRequested
                                       ? IoOperationState::Cancelled
                                       : IoOperationState::FailedCompleted;
                    result.error = error;
                    return result;
                }
                // The event cannot be reaped by GetOverlappedResult here.
                // Keep the storage alive and avoid a hot spin while the
                // terminal state propagates.
                ::Sleep(1);
                continue;
            }
            result.state = IoOperationState::FailedCompleted;
            result.error = error;
            return result;
        }
        if (wait == WAIT_OBJECT_0 + 1 || wait == WAIT_TIMEOUT ||
            wait == WAIT_FAILED)
        {
            const bool waitFailed = wait == WAIT_FAILED;
            if (result.state != IoOperationState::CancelRequested)
            {
                result.state = IoOperationState::CancelRequested;
                result.timedOut = wait == WAIT_TIMEOUT;
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
                if (!ConsumeFault(StreamFaultSite::CancelIoNotFound))
                {
                    ::CancelIoEx(pipe, &overlapped);
                }
                else
                {
                    // ERROR_NOT_FOUND is not a completion proof. Keep waiting
                    // for the real terminal notification.
                    result.error = ERROR_NOT_FOUND;
                }
#else
                ::CancelIoEx(pipe, &overlapped);
#endif
            }
            // Cancellation is only a request. Do not free stack OVERLAPPED,
            // event or buffer until the I/O manager confirms terminal state.
            waitStop = nullptr;
            waitTimeout = INFINITE;
            if (waitFailed)
                ::Sleep(1);
            continue;
        }
        result.state = IoOperationState::FailedCompleted;
        result.error = ::GetLastError();
        return result;
    }
}

class OverlappedPipeStream final : public IByteStream
{
public:
    OverlappedPipeStream(HANDLE pipe, HANDLE stopEvent,
                         bool ownsPipe) noexcept
        : pipe_(pipe), stopEvent_(stopEvent), ownsPipe_(ownsPipe)
    {
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
        if (ConsumeFault(StreamFaultSite::OverlappedEventCreate))
            return;
#endif
        ioEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }

    ~OverlappedPipeStream() override { Close(); }

    OverlappedPipeStream(const OverlappedPipeStream&) = delete;
    OverlappedPipeStream& operator=(const OverlappedPipeStream&) = delete;

    bool Valid() const noexcept
    {
        ::AcquireSRWLockExclusive(&ioLock_);
        const bool valid = ValidNoLock();
        ::ReleaseSRWLockExclusive(&ioLock_);
        return valid;
    }

    void Close() noexcept
    {
        // A concurrent Read/Write holds ioLock_ for the whole operation,
        // including terminal cancellation. Do not close its handles first.
        // stopEvent_ is cancellation input owned by the caller: closing a
        // connection must not signal the producer-wide stop event.
        ::AcquireSRWLockExclusive(&ioLock_);
        if (pipe_ != INVALID_HANDLE_VALUE && pipe_ != nullptr)
        {
            ::CancelIoEx(pipe_, nullptr);
            if (ownsPipe_)
                ::CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
        if (ioEvent_ != nullptr)
        {
            ::CloseHandle(ioEvent_);
            ioEvent_ = nullptr;
        }
        ::ReleaseSRWLockExclusive(&ioLock_);
    }

    StreamIoStatus Read(void* buffer, size_t bytes, size_t& transferred,
                        DWORD timeoutMs) noexcept override
    {
        return DoIo(true, buffer, bytes, transferred, timeoutMs);
    }

    StreamIoStatus Write(const void* buffer, size_t bytes, size_t& transferred,
                         DWORD timeoutMs) noexcept override
    {
        return DoIo(false, buffer, bytes, transferred, timeoutMs);
    }

private:
    bool ValidNoLock() const noexcept
    {
        return pipe_ != INVALID_HANDLE_VALUE && pipe_ != nullptr &&
               ioEvent_ != nullptr;
    }

    StreamIoStatus DoIo(bool read, const void* input, size_t bytes,
                        size_t& transferred, DWORD timeoutMs) noexcept
    {
        transferred = 0;
        if (bytes == 0)
            return StreamIoStatus::Ok;
        if (!Valid())
            return StreamIoStatus::Failed;
        if (bytes > 0x7FFFFFFFu)
            return StreamIoStatus::Failed;

        ::AcquireSRWLockExclusive(&ioLock_);
        if (!ValidNoLock())
        {
            ::ReleaseSRWLockExclusive(&ioLock_);
            return StreamIoStatus::Closed;
        }

        OVERLAPPED overlapped{};
        overlapped.hEvent = ioEvent_;
        ::ResetEvent(ioEvent_);

        DWORD actual = 0;
        BOOL started = FALSE;
        if (read)
        {
            started = ::ReadFile(pipe_, const_cast<void*>(input),
                                 static_cast<DWORD>(bytes), &actual,
                                 &overlapped);
        }
        else
        {
            started = ::WriteFile(pipe_, input, static_cast<DWORD>(bytes),
                                  &actual, &overlapped);
        }

        IoCompletion completion;
        if (started)
        {
            completion.state = IoOperationState::Completed;
            completion.bytes = actual;
        }
        else
        {
            const DWORD error = ::GetLastError();
            if (error != ERROR_IO_PENDING)
            {
                completion.state = IoOperationState::FailedCompleted;
                completion.error = error;
            }
            else
            {
                bool forceWaitFailed = false;
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
                forceWaitFailed =
                    ConsumeFault(read ? StreamFaultSite::ReadCancelBeforeCompletion
                                      : StreamFaultSite::WriteCancelBeforeCompletion);
#endif
                completion = WaitForTerminalCompletion(
                    pipe_, overlapped, ioEvent_, stopEvent_, timeoutMs,
                    forceWaitFailed);
            }
        }

        StreamIoStatus status = StreamIoStatus::Failed;
        switch (completion.state)
        {
        case IoOperationState::Completed:
            transferred = completion.bytes;
            status = completion.bytes == 0 ? StreamIoStatus::Closed
                                           : StreamIoStatus::Ok;
            break;
        case IoOperationState::Cancelled:
            status = completion.timedOut ? StreamIoStatus::Timeout
                                         : StreamIoStatus::Closed;
            break;
        case IoOperationState::FailedCompleted:
        case IoOperationState::NotStarted:
        case IoOperationState::Pending:
        case IoOperationState::CancelRequested:
        case IoOperationState::CompletionUnknown:
            status = StreamIoStatus::Failed;
            break;
        }
        ::ReleaseSRWLockExclusive(&ioLock_);
        return status;
    }

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE stopEvent_ = nullptr;
    HANDLE ioEvent_ = nullptr;
    bool ownsPipe_ = false;
    mutable SRWLOCK ioLock_{};
};

enum class ExactStatus : uint8_t
{
    Ok = 0,
    Timeout,
    Closed,
    Truncated
};

ExactStatus ReadExact(IByteStream& stream, void* buffer, size_t bytes,
                      DWORD timeoutMs, bool& sawBytes) noexcept
{
    uint8_t* cursor = static_cast<uint8_t*>(buffer);
    size_t total = 0;
    sawBytes = false;
    while (total < bytes)
    {
        size_t transferred = 0;
        const StreamIoStatus status =
            stream.Read(cursor + total, bytes - total, transferred, timeoutMs);
        if (status == StreamIoStatus::Timeout)
            return total == 0 ? ExactStatus::Timeout : ExactStatus::Truncated;
        if (status == StreamIoStatus::Closed ||
            status == StreamIoStatus::Failed)
            return total == 0 ? ExactStatus::Closed : ExactStatus::Truncated;
        if (transferred == 0)
            return total == 0 ? ExactStatus::Closed : ExactStatus::Truncated;
        total += transferred;
        sawBytes = true;
    }
    return ExactStatus::Ok;
}

StreamIoStatus WriteExact(IByteStream& stream, std::span<const uint8_t> bytes,
                          DWORD timeoutMs) noexcept
{
    size_t total = 0;
    while (total < bytes.size())
    {
        size_t transferred = 0;
        const StreamIoStatus status =
            stream.Write(bytes.data() + total, bytes.size() - total,
                         transferred, timeoutMs);
        if (status != StreamIoStatus::Ok)
            return status;
        if (transferred == 0)
            return StreamIoStatus::Closed;
        total += transferred;
    }
    return StreamIoStatus::Ok;
}

void WriteHeader(uint8_t* header, StreamMessageType type, uint32_t payloadBytes,
                 uint32_t crc) noexcept
{
    std::memcpy(header, kProtocolMagic, sizeof(kProtocolMagic));
    PutU16(header + 4, kProtocolVersion);
    PutU16(header + 6, static_cast<uint16_t>(type));
    PutU32(header + 8, payloadBytes);
    PutU32(header + 12, crc);
    PutU32(header + 16, 0);
}

bool IsKnownMessageType(uint16_t value) noexcept
{
    return value >= static_cast<uint16_t>(StreamMessageType::Hello) &&
           value <= static_cast<uint16_t>(StreamMessageType::Shutdown);
}

void HashBytes(uint64_t& hash, const void* data, size_t bytes) noexcept
{
    const uint8_t* cursor = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i)
    {
        hash ^= cursor[i];
        hash *= 1099511628211ull;
    }
}

void HashU32(uint64_t& hash, uint32_t value) noexcept
{
    uint8_t bytes[4] = {};
    PutU32(bytes, value);
    HashBytes(hash, bytes, sizeof(bytes));
}

void HashU64(uint64_t& hash, uint64_t value) noexcept
{
    uint8_t bytes[8] = {};
    PutU64(bytes, value);
    HashBytes(hash, bytes, sizeof(bytes));
}

void HashFloat(uint64_t& hash, float value) noexcept
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    HashU32(hash, bits);
}

void UpdateLastSentSample(SentSample& sample, const CameraPacket& packet,
                          uint64_t sentQpc) noexcept
{
    sample.packet = packet;
    sample.hash = ComputePacketHash(packet.capture);
    sample.sentQpc = sentQpc;
}

} // namespace

const char* WorkerStateName(WorkerState state) noexcept
{
    switch (state)
    {
    case WorkerState::Starting: return "Starting";
    case WorkerState::Running: return "Running";
    case WorkerState::Recovering: return "Recovering";
    case WorkerState::Stopping: return "Stopping";
    case WorkerState::Stopped: return "Stopped";
    case WorkerState::Failed: return "Failed";
    }
    return "Unknown";
}

const char* IoOperationStateName(IoOperationState state) noexcept
{
    switch (state)
    {
    case IoOperationState::NotStarted: return "NotStarted";
    case IoOperationState::Pending: return "Pending";
    case IoOperationState::CancelRequested: return "CancelRequested";
    case IoOperationState::Completed: return "Completed";
    case IoOperationState::Cancelled: return "Cancelled";
    case IoOperationState::FailedCompleted: return "FailedCompleted";
    case IoOperationState::CompletionUnknown: return "CompletionUnknown";
    }
    return "Unknown";
}

const char* MessageTypeName(StreamMessageType type) noexcept
{
    switch (type)
    {
    case StreamMessageType::Hello: return "HELLO";
    case StreamMessageType::CameraFrame: return "CAMERA_FRAME";
    case StreamMessageType::InvalidateSession: return "INVALIDATE_SESSION";
    case StreamMessageType::Shutdown: return "SHUTDOWN";
    }
    return "UNKNOWN";
}

const char* InvalidateReasonName(InvalidateReason reason) noexcept
{
    switch (reason)
    {
    case InvalidateReason::LevelReady: return "level_ready";
    case InvalidateReason::Reset: return "reset";
    case InvalidateReason::Teardown: return "teardown";
    case InvalidateReason::Shutdown: return "shutdown";
    }
    return "unknown";
}

const char* MessageReadStatusName(MessageReadStatus status) noexcept
{
    switch (status)
    {
    case MessageReadStatus::Ok: return "ok";
    case MessageReadStatus::Timeout: return "timeout";
    case MessageReadStatus::Closed: return "closed";
    case MessageReadStatus::Truncated: return "truncated";
    case MessageReadStatus::BadMagic: return "bad_magic";
    case MessageReadStatus::UnsupportedVersion: return "unsupported_version";
    case MessageReadStatus::UnknownType: return "unknown_type";
    case MessageReadStatus::Oversized: return "oversized";
    case MessageReadStatus::CrcMismatch: return "crc_mismatch";
    case MessageReadStatus::InternalError: return "internal_error";
    case MessageReadStatus::InvalidPayload: return "invalid_payload";
    }
    return "unknown";
}

std::vector<uint8_t> SerializeHello(const HelloPayload& hello)
{
    std::vector<uint8_t> payload(kHelloPayloadBytes, 0);
    PutU32(payload.data() + 0, hello.producerPid);
    PutU64(payload.data() + 4, hello.producerStartId);
    PutU64(payload.data() + 12, hello.levelSession);
    PutU64(payload.data() + 20, hello.captureEpoch);
    PutU32(payload.data() + 28, 0);
    return payload;
}

bool ParseHello(std::span<const uint8_t> payload, HelloPayload& hello) noexcept
{
    if (payload.size() != kHelloPayloadBytes)
        return false;
    hello.producerPid = GetU32(payload.data() + 0);
    hello.producerStartId = GetU64(payload.data() + 4);
    hello.levelSession = GetU64(payload.data() + 12);
    hello.captureEpoch = GetU64(payload.data() + 20);
    return GetU32(payload.data() + 28) == 0;
}

std::vector<uint8_t> SerializeInvalidate(const InvalidatePayload& invalidate)
{
    std::vector<uint8_t> payload(kInvalidatePayloadBytes, 0);
    PutU64(payload.data() + 0, invalidate.levelSession);
    PutU64(payload.data() + 8, invalidate.captureEpoch);
    PutU32(payload.data() + 16, static_cast<uint32_t>(invalidate.reason));
    PutU32(payload.data() + 20, 0);
    return payload;
}

bool ParseInvalidate(std::span<const uint8_t> payload,
                     InvalidatePayload& invalidate) noexcept
{
    if (payload.size() != kInvalidatePayloadBytes)
        return false;
    invalidate.levelSession = GetU64(payload.data() + 0);
    invalidate.captureEpoch = GetU64(payload.data() + 8);
    const uint32_t reason = GetU32(payload.data() + 16);
    if (reason < static_cast<uint32_t>(InvalidateReason::LevelReady) ||
        reason > static_cast<uint32_t>(InvalidateReason::Shutdown))
        return false;
    invalidate.reason = static_cast<InvalidateReason>(reason);
    return GetU32(payload.data() + 20) == 0;
}

std::vector<uint8_t> SerializeCameraFrame(const CameraFrameMeta& meta,
                                          std::span<const uint8_t> codecBytes)
{
    if (codecBytes.size() > kMaxCodecBytes ||
        codecBytes.size() + kCameraFrameHeaderBytes > kMaxPayloadBytes)
        return {};
    std::vector<uint8_t> payload(kCameraFrameHeaderBytes + codecBytes.size(), 0);
    PutU64(payload.data() + 0, meta.publishQpc);
    PutU32(payload.data() + 8, static_cast<uint32_t>(codecBytes.size()));
    PutU32(payload.data() + 12, meta.flags);
    if (!codecBytes.empty())
        std::memcpy(payload.data() + kCameraFrameHeaderBytes, codecBytes.data(),
                    codecBytes.size());
    return payload;
}

bool ParseCameraFrame(std::span<const uint8_t> payload, CameraFrameMeta& meta,
                      std::span<const uint8_t>& codecBytes) noexcept
{
    if (payload.size() < kCameraFrameHeaderBytes)
        return false;
    meta.publishQpc = GetU64(payload.data() + 0);
    meta.codecBytes = GetU32(payload.data() + 8);
    meta.flags = GetU32(payload.data() + 12);
    if (meta.flags != 0 || meta.codecBytes > kMaxCodecBytes ||
        meta.codecBytes != payload.size() - kCameraFrameHeaderBytes)
        return false;
    codecBytes = payload.subspan(kCameraFrameHeaderBytes, meta.codecBytes);
    return true;
}

MessageReadStatus ReadMessage(IByteStream& stream, StreamMessage& message,
                              DWORD timeoutMs) noexcept
{
    uint8_t header[kFrameHeaderBytes] = {};
    bool sawBytes = false;
    const ExactStatus headerStatus =
        ReadExact(stream, header, sizeof(header), timeoutMs, sawBytes);
    if (headerStatus != ExactStatus::Ok)
    {
        if (headerStatus == ExactStatus::Timeout)
            return MessageReadStatus::Timeout;
        if (headerStatus == ExactStatus::Closed)
            return MessageReadStatus::Closed;
        return MessageReadStatus::Truncated;
    }

    if (std::memcmp(header, kProtocolMagic, sizeof(kProtocolMagic)) != 0)
        return MessageReadStatus::BadMagic;
    if (GetU16(header + 4) != kProtocolVersion)
        return MessageReadStatus::UnsupportedVersion;
    const uint16_t type = GetU16(header + 6);
    if (!IsKnownMessageType(type))
        return MessageReadStatus::UnknownType;
    const uint32_t payloadBytes = GetU32(header + 8);
    if (payloadBytes > kMaxPayloadBytes)
        return MessageReadStatus::Oversized;
    if (GetU32(header + 16) != 0)
        return MessageReadStatus::InvalidPayload;
    const uint32_t expectedCrc = GetU32(header + 12);

    std::vector<uint8_t> payload;
    try
    {
        payload.resize(payloadBytes);
    }
    catch (...)
    {
        return MessageReadStatus::InternalError;
    }
    if (payloadBytes != 0)
    {
        bool payloadSawBytes = false;
        const ExactStatus payloadStatus =
            ReadExact(stream, payload.data(), payloadBytes, timeoutMs,
                      payloadSawBytes);
        if (payloadStatus != ExactStatus::Ok)
        {
            if (payloadStatus == ExactStatus::Timeout)
                return MessageReadStatus::Truncated;
            if (payloadStatus == ExactStatus::Closed)
                return MessageReadStatus::Truncated;
            return MessageReadStatus::Truncated;
        }
    }
    if (cameracapture12::Crc32(payload) != expectedCrc)
        return MessageReadStatus::CrcMismatch;

    message.type = static_cast<StreamMessageType>(type);
    message.payload = std::move(payload);
    return MessageReadStatus::Ok;
}

StreamIoStatus WriteMessage(IByteStream& stream, StreamMessageType type,
                            std::span<const uint8_t> payload,
                            DWORD timeoutMs) noexcept
{
    if (payload.size() > kMaxPayloadBytes)
        return StreamIoStatus::Failed;
    uint8_t header[kFrameHeaderBytes] = {};
    WriteHeader(header, type, static_cast<uint32_t>(payload.size()),
                cameracapture12::Crc32(payload));
    const StreamIoStatus headerStatus =
        WriteExact(stream, std::span<const uint8_t>(header, sizeof(header)),
                   timeoutMs);
    if (headerStatus != StreamIoStatus::Ok)
        return headerStatus;
    if (payload.empty())
        return StreamIoStatus::Ok;
    return WriteExact(stream, payload, timeoutMs);
}

uint64_t ComputePacketHash(
    const cameracapture12::CameraCaptureFile12& capture) noexcept
{
    uint64_t hash = 1469598103934665603ull;
    size_t nameLength = 0;
    while (nameLength < sizeof(capture.levelName) &&
           capture.levelName[nameLength] != '\0')
        ++nameLength;
    HashBytes(hash, capture.levelName, nameLength);
    HashU64(hash, capture.input.levelSession);
    HashU64(hash, capture.input.captureEpoch);
    HashU64(hash, capture.input.captureSequence);
    HashU32(hash, capture.input.sourceGameFrame);
    HashU32(hash, capture.input.viewportWidth);
    HashU32(hash, capture.input.viewportHeight);
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t column = 0; column < 4; ++column)
        {
            HashFloat(hash, capture.input.view.m[row][column]);
            HashFloat(hash, capture.input.projection.m[row][column]);
            HashFloat(hash, capture.input.fullTransform.m[row][column]);
        }
    HashFloat(hash, capture.input.position.x);
    HashFloat(hash, capture.input.position.y);
    HashFloat(hash, capture.input.position.z);
    return hash;
}

StreamCountersSnapshot SnapshotCounters(const StreamCounters& counters) noexcept
{
    StreamCountersSnapshot snapshot;
    snapshot.published = counters.published.load(std::memory_order_relaxed);
    snapshot.enqueued = counters.enqueued.load(std::memory_order_relaxed);
    snapshot.overwritten = counters.overwritten.load(std::memory_order_relaxed);
    snapshot.lockDropped = counters.lockDropped.load(std::memory_order_relaxed);
    snapshot.encoded = counters.encoded.load(std::memory_order_relaxed);
    snapshot.sent = counters.sent.load(std::memory_order_relaxed);
    snapshot.rejected = counters.rejected.load(std::memory_order_relaxed);
    snapshot.disconnected = counters.disconnected.load(std::memory_order_relaxed);
    snapshot.reconnected = counters.reconnected.load(std::memory_order_relaxed);
    return snapshot;
}

void EnqueueLatencyRing::Record(uint64_t nanoseconds) noexcept
{
    values_[writeIndex_] = nanoseconds;
    writeIndex_ = (writeIndex_ + 1) % kCapacity;
    if (validCount_ < kCapacity)
        ++validCount_;
}

void EnqueueLatencyRing::Reset() noexcept
{
    values_.fill(0);
    writeIndex_ = 0;
    validCount_ = 0;
}

EnqueueStats EnqueueLatencyRing::Stats() const noexcept
{
    EnqueueStats stats;
    stats.samples = validCount_;
    if (validCount_ == 0)
        return stats;
    std::array<uint64_t, kCapacity> values = values_;
    std::sort(values.begin(), values.begin() + validCount_);
    auto percentile = [&](double fraction) -> uint64_t {
        const size_t index = static_cast<size_t>(
            (static_cast<double>(validCount_) - 1.0) * fraction);
        return values[std::min(index, static_cast<size_t>(validCount_ - 1))];
    };
    stats.medianNs = percentile(0.50);
    stats.p95Ns = percentile(0.95);
    stats.p99Ns = percentile(0.99);
    stats.maxNs = values[validCount_ - 1];
    return stats;
}

LatestOnlyMailbox::LatestOnlyMailbox(StreamCounters& counters) noexcept
    : counters_(counters)
{
    wakeEvent_ = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

LatestOnlyMailbox::~LatestOnlyMailbox()
{
    if (wakeEvent_ != nullptr)
        ::CloseHandle(wakeEvent_);
}

void LatestOnlyMailbox::SignalWake() const noexcept
{
    if (wakeEvent_ != nullptr)
        ::SetEvent(wakeEvent_);
}

bool LatestOnlyMailbox::EnqueueCamera(const CameraPacket& packet) noexcept
{
    counters_.published.fetch_add(1, std::memory_order_relaxed);
    if (!::TryAcquireSRWLockExclusive(&lock_))
    {
        counters_.lockDropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (!accepting_)
    {
        ::ReleaseSRWLockExclusive(&lock_);
        counters_.rejected.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (hasPendingCamera_)
        counters_.overwritten.fetch_add(1, std::memory_order_relaxed);
    pendingCamera_ = packet;
    hasPendingCamera_ = true;
    counters_.enqueued.fetch_add(1, std::memory_order_relaxed);
    ::ReleaseSRWLockExclusive(&lock_);
    SignalWake();
    return true;
}

void LatestOnlyMailbox::SetControlLocked(WorkItem::Kind kind,
                                         uint64_t levelSession,
                                         uint64_t captureEpoch,
                                         InvalidateReason reason) noexcept
{
    if (hasPendingCamera_)
    {
        counters_.overwritten.fetch_add(1, std::memory_order_relaxed);
        hasPendingCamera_ = false;
    }
    control_.kind = kind;
    control_.levelSession = levelSession;
    control_.captureEpoch = captureEpoch;
    control_.reason = reason;
    hasControl_ = true;
    generation_.fetch_add(1, std::memory_order_acq_rel);
}

void LatestOnlyMailbox::BeginAccepting() noexcept
{
    ::AcquireSRWLockExclusive(&lock_);
    accepting_ = true;
    ::ReleaseSRWLockExclusive(&lock_);
}

void LatestOnlyMailbox::Abandon() noexcept
{
    ::AcquireSRWLockExclusive(&lock_);
    accepting_ = false;
    hasPendingCamera_ = false;
    pendingCamera_ = CameraPacket{};
    hasControl_ = false;
    control_ = WorkItem{};
    sendInProgress_ = false;
    generation_.fetch_add(1, std::memory_order_acq_rel);
    ::ReleaseSRWLockExclusive(&lock_);
    SignalWake();
}

bool LatestOnlyMailbox::TryBeginSend(uint64_t generation) noexcept
{
    ::AcquireSRWLockExclusive(&lock_);
    const bool accepted = accepting_ && !sendInProgress_ &&
                          generation_.load(std::memory_order_acquire) ==
                              generation;
    if (accepted)
        sendInProgress_ = true;
    ::ReleaseSRWLockExclusive(&lock_);
    return accepted;
}

void LatestOnlyMailbox::EndSend() noexcept
{
    ::AcquireSRWLockExclusive(&lock_);
    sendInProgress_ = false;
    ::ReleaseSRWLockExclusive(&lock_);
}

void LatestOnlyMailbox::Invalidate(uint64_t levelSession,
                                   uint64_t captureEpoch,
                                   InvalidateReason reason) noexcept
{
    ::AcquireSRWLockExclusive(&lock_);
    SetControlLocked(WorkItem::Kind::Invalidate, levelSession, captureEpoch,
                     reason);
    ::ReleaseSRWLockExclusive(&lock_);
    SignalWake();
}

void LatestOnlyMailbox::RequestShutdown() noexcept
{
    ::AcquireSRWLockExclusive(&lock_);
    SetControlLocked(WorkItem::Kind::Shutdown, 0, 0,
                     InvalidateReason::Shutdown);
    ::ReleaseSRWLockExclusive(&lock_);
    SignalWake();
}

bool LatestOnlyMailbox::Take(WorkItem& item) noexcept
{
    ::AcquireSRWLockExclusive(&lock_);
    if (hasControl_)
    {
        item = control_;
        item.generation = generation_.load(std::memory_order_acquire);
        control_ = WorkItem{};
        hasControl_ = false;
        ::ReleaseSRWLockExclusive(&lock_);
        return true;
    }
    if (hasPendingCamera_ && accepting_)
    {
        item = WorkItem{};
        item.kind = WorkItem::Kind::Camera;
        item.camera = pendingCamera_;
        item.generation = generation_.load(std::memory_order_acquire);
        pendingCamera_ = CameraPacket{};
        hasPendingCamera_ = false;
        ::ReleaseSRWLockExclusive(&lock_);
        return true;
    }
    ::ReleaseSRWLockExclusive(&lock_);
    return false;
}

#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
void LatestOnlyMailbox::LockForTest() noexcept
{
    ::AcquireSRWLockExclusive(&lock_);
}

void LatestOnlyMailbox::UnlockForTest() noexcept
{
    ::ReleaseSRWLockExclusive(&lock_);
}
#endif

struct CameraStreamProducer::Impl
{
    StreamCounters counters;
    LatestOnlyMailbox mailbox{counters};
    std::wstring pipeName;
    std::thread worker;
    HANDLE stopEvent = nullptr;
    std::atomic<WorkerState> state{WorkerState::Stopped};
    std::atomic<uint32_t> lastFailure{0};
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
    std::atomic<HANDLE> firstPipeProbe{INVALID_HANDLE_VALUE};
#endif
    bool started = false;
    uint32_t producerPid = 0;
    uint64_t producerStartId = 0;
    std::atomic<uint64_t> levelSession{0};
    std::atomic<uint64_t> captureEpoch{0};

    SRWLOCK lastSentLock{};
    SentSample lastSent{};
    bool hasLastSent = false;

    EnqueueLatencyRing enqueueLatency;

    bool CreateFirstPipe(HANDLE& pipe, std::string& error)
    {
        pipe = CreateLocalPipeInstance(pipeName);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            error = Win32Error("CreateNamedPipeW");
            return false;
        }
        return true;
    }

    bool ConnectPipe(HANDLE pipe)
    {
        OVERLAPPED overlapped{};
        overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr)
            return false;
        ::ResetEvent(overlapped.hEvent);
        const BOOL connected = ::ConnectNamedPipe(pipe, &overlapped);
        if (connected)
        {
            ::CloseHandle(overlapped.hEvent);
            return true;
        }
        const DWORD error = ::GetLastError();
        if (error == ERROR_PIPE_CONNECTED)
        {
            ::CloseHandle(overlapped.hEvent);
            return true;
        }
        if (error != ERROR_IO_PENDING)
        {
            ::CloseHandle(overlapped.hEvent);
            return false;
        }

        bool forceWaitFailed = false;
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
        forceWaitFailed = ConsumeFault(StreamFaultSite::ConnectWaitFailed);
#endif
        const IoCompletion completion = WaitForTerminalCompletion(
            pipe, overlapped, overlapped.hEvent, stopEvent, INFINITE,
            forceWaitFailed);
        ::CloseHandle(overlapped.hEvent);
        return completion.state == IoOperationState::Completed;
    }

    bool SendHello(OverlappedPipeStream& stream)
    {
        HelloPayload hello;
        hello.producerPid = producerPid;
        hello.producerStartId = producerStartId;
        hello.levelSession = levelSession.load(std::memory_order_relaxed);
        hello.captureEpoch = captureEpoch.load(std::memory_order_relaxed);
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
        ThrowIfFault(StreamFaultSite::HelloSerialize);
#endif
        const std::vector<uint8_t> payload = SerializeHello(hello);
        return WriteMessage(stream, StreamMessageType::Hello, payload,
                            kWriteTimeoutMs) == StreamIoStatus::Ok;
    }

    bool SendInvalidate(OverlappedPipeStream& stream, const WorkItem& item)
    {
        InvalidatePayload invalidate;
        invalidate.levelSession = item.levelSession;
        invalidate.captureEpoch = item.captureEpoch;
        invalidate.reason = item.reason;
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
        ThrowIfFault(StreamFaultSite::InvalidateSerialize);
#endif
        const std::vector<uint8_t> payload = SerializeInvalidate(invalidate);
        return WriteMessage(stream, StreamMessageType::InvalidateSession,
                            payload, kWriteTimeoutMs) == StreamIoStatus::Ok;
    }

    bool SendShutdown(OverlappedPipeStream& stream)
    {
        return WriteMessage(stream, StreamMessageType::Shutdown, {},
                            kWriteTimeoutMs) == StreamIoStatus::Ok;
    }

    bool SendCamera(OverlappedPipeStream& stream, const CameraPacket& packet,
                    uint64_t expectedGeneration, uint64_t& sentQpc)
    {
        if (mailbox.Generation() != expectedGeneration)
        {
            counters.rejected.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        std::vector<uint8_t> codec;
        cameracapture12::FileError fileError = cameracapture12::FileError::None;
        if (!cameracapture12::Encode(packet.capture, codec, fileError))
        {
            counters.rejected.fetch_add(1, std::memory_order_relaxed);
            return true; // encoding rejection is not a connection failure
        }
        counters.encoded.fetch_add(1, std::memory_order_relaxed);
        if (codec.size() > kMaxCodecBytes)
        {
            counters.rejected.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
        ThrowIfFault(StreamFaultSite::CameraSerialize);
        if (g_preSubmitBlock.load(std::memory_order_acquire))
        {
            g_preSubmitEntered.store(true, std::memory_order_release);
            while (g_preSubmitBlock.load(std::memory_order_acquire))
                ::Sleep(1);
        }
#endif
        // Linearization point: a subsequent invalidation can no longer revoke
        // this packet, but an invalidation before this point must win.
        if (!mailbox.TryBeginSend(expectedGeneration))
        {
            counters.rejected.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        struct EndSendGuard
        {
            LatestOnlyMailbox& mailbox;
            ~EndSendGuard() { mailbox.EndSend(); }
        } sendGuard{mailbox};

        CameraFrameMeta meta;
        meta.publishQpc = packet.publishQpc;
        meta.codecBytes = static_cast<uint32_t>(codec.size());
        const std::vector<uint8_t> payload =
            SerializeCameraFrame(meta, codec);
        if (payload.empty())
        {
            counters.rejected.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        const StreamIoStatus status =
            WriteMessage(stream, StreamMessageType::CameraFrame, payload,
                         kWriteTimeoutMs);
        if (status != StreamIoStatus::Ok)
            return false;
        sentQpc = QpcNow();
        counters.sent.fetch_add(1, std::memory_order_relaxed);
        SentSample sample;
        UpdateLastSentSample(sample, packet, sentQpc);
        ::AcquireSRWLockExclusive(&lastSentLock);
        lastSent = sample;
        hasLastSent = true;
        ::ReleaseSRWLockExclusive(&lastSentLock);
        return true;
    }

    void RunWorker(HANDLE firstPipe)
    {
        // The first pipe is owned before the fault hook and before the stop
        // check. If the worker exits before its first loop, this guard closes
        // it; after a successful stream construction the stream owns it.
        ScopedWinHandle pipeOwner(firstPipe);
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
        ThrowIfFault(StreamFaultSite::WorkerUnexpectedThrow);
        if (ConsumeFault(StreamFaultSite::WorkerPauseBeforeLoop))
        {
            g_workerPauseBeforeLoopEntered.store(true,
                                                 std::memory_order_release);
            const ULONGLONG deadline = ::GetTickCount64() + 10000;
            while (g_workerPauseBeforeLoop.load(std::memory_order_acquire) &&
                   ::GetTickCount64() < deadline)
            {
                ::Sleep(1);
            }
        }
#endif
        bool everConnected = false;

        while (!stopEvent || ::WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0)
        {
            if (!pipeOwner)
            {
                const HANDLE created = CreateLocalPipeInstance(pipeName);
                if (created == INVALID_HANDLE_VALUE)
                {
                    counters.disconnected.fetch_add(1, std::memory_order_relaxed);
                    ::Sleep(kReconnectDelayMs);
                    continue;
                }
                pipeOwner.Reset(created);
            }

            const HANDLE pipe = pipeOwner.Get();
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
            if (ConsumeFault(StreamFaultSite::ProducerStreamAllocate))
            {
                pipeOwner.Reset();
                counters.disconnected.fetch_add(1, std::memory_order_relaxed);
                ::Sleep(kReconnectDelayMs);
                continue;
            }
#endif
            auto stream = std::unique_ptr<OverlappedPipeStream>(
                new (std::nothrow)
                    OverlappedPipeStream(pipe, stopEvent, true));
            if (!stream)
            {
                // The stream was not constructed, so the guard still owns the
                // pipe and must close it exactly once.
                pipeOwner.Reset();
                counters.disconnected.fetch_add(1, std::memory_order_relaxed);
                ::Sleep(kReconnectDelayMs);
                continue;
            }

            // Ownership moved to the stream during construction, including the
            // case where ioEvent creation made the stream invalid.
            pipeOwner.Release();
            if (!stream->Valid())
            {
                stream->Close();
                counters.disconnected.fetch_add(1, std::memory_order_relaxed);
                ::Sleep(kReconnectDelayMs);
                continue;
            }

#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
            // Exception after ownership transfer must be closed by ~stream,
            // never by a second CloseHandle in WorkerMain's catch.
            ThrowIfFault(StreamFaultSite::WorkerThrowAfterOwnership);
#endif

            if (!ConnectPipe(pipe))
            {
                stream->Close();
                if (::WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
                    break;
                counters.disconnected.fetch_add(1, std::memory_order_relaxed);
                ::Sleep(kReconnectDelayMs);
                continue;
            }

            if (everConnected)
                counters.reconnected.fetch_add(1, std::memory_order_relaxed);
            everConnected = true;

            bool healthy = SendHello(*stream);
            if (!healthy)
                counters.disconnected.fetch_add(1, std::memory_order_relaxed);

            while (healthy &&
                   ::WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0)
            {
                WorkItem item;
                if (!mailbox.Take(item))
                {
                    HANDLE waits[2] = {stopEvent, mailbox.WakeEvent()};
                    ::WaitForMultipleObjects(2, waits, FALSE, kIdleWaitMs);
                    continue;
                }

                if (item.kind == WorkItem::Kind::Camera)
                {
                    uint64_t sentQpc = 0;
                    const bool sent = SendCamera(*stream, item.camera,
                                                 item.generation, sentQpc);
                    if (!sent)
                    {
                        healthy = false;
                        counters.disconnected.fetch_add(
                            1, std::memory_order_relaxed);
                        break;
                    }
                }
                else if (item.kind == WorkItem::Kind::Invalidate)
                {
                    levelSession.store(item.levelSession,
                                       std::memory_order_relaxed);
                    captureEpoch.store(item.captureEpoch,
                                       std::memory_order_relaxed);
                    if (!SendInvalidate(*stream, item))
                    {
                        healthy = false;
                        counters.disconnected.fetch_add(
                            1, std::memory_order_relaxed);
                    }
                }
                else if (item.kind == WorkItem::Kind::Shutdown)
                {
                    SendShutdown(*stream);
                    stream->Close();
                    return;
                }
            }

            if (::WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
                SendShutdown(*stream);
            stream->Close();
            if (::WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
                break;
        }
    }

    void WorkerMain(HANDLE firstPipe) noexcept
    {
        if (state.load(std::memory_order_acquire) != WorkerState::Stopping)
            state.store(WorkerState::Running, std::memory_order_release);
        try
        {
            RunWorker(firstPipe);
            if (state.load(std::memory_order_acquire) != WorkerState::Failed)
                state.store(WorkerState::Stopped, std::memory_order_release);
        }
        catch (...)
        {
            lastFailure.store(ERROR_UNHANDLED_EXCEPTION,
                              std::memory_order_release);
            counters.disconnected.fetch_add(1, std::memory_order_relaxed);
            counters.rejected.fetch_add(1, std::memory_order_relaxed);
            mailbox.Abandon();
            state.store(WorkerState::Failed, std::memory_order_release);
        }
    }

    void StopWorker() noexcept
    {
        if (!started)
            return;
        // Preserve a failure already reported by the worker: the failure state
        // must stay observable after Stop.
        if (state.load(std::memory_order_acquire) != WorkerState::Failed)
            state.store(WorkerState::Stopping, std::memory_order_release);
        // The 50 ms grace only gives a connected worker a chance to send
        // SHUTDOWN. It is not an upper bound on Stop: the join below waits for
        // confirmed terminal completion of outstanding overlapped I/O.
        mailbox.RequestShutdown();
        mailbox.SignalWake();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (stopEvent != nullptr)
            ::SetEvent(stopEvent);
        mailbox.SignalWake();
        if (worker.joinable())
            worker.join();
        mailbox.Abandon();
        if (stopEvent != nullptr)
        {
            ::CloseHandle(stopEvent);
            stopEvent = nullptr;
        }
        if (state.load(std::memory_order_acquire) != WorkerState::Failed)
            state.store(WorkerState::Stopped, std::memory_order_release);
        // started remains true: this producer is a one-shot object. A second
        // Start is rejected after Stop instead of resurrecting a dead queue.
    }

    EnqueueStats ComputeEnqueueStats() const
    {
        return enqueueLatency.Stats();
    }
};

CameraStreamProducer::CameraStreamProducer()
    : impl_(std::make_unique<Impl>())
{
}

CameraStreamProducer::~CameraStreamProducer()
{
    Stop();
}

std::wstring CameraStreamProducer::MakePipeName()
{
    static std::atomic<uint64_t> sequence{0};
    wchar_t name[160] = {};
    std::swprintf(name, std::size(name), L"\\\\.\\pipe\\OGSR-B3B2A-%lu-%llu-%llu",
                  static_cast<unsigned long>(::GetCurrentProcessId()),
                  static_cast<unsigned long long>(::GetTickCount64()),
                  static_cast<unsigned long long>(
                      sequence.fetch_add(1, std::memory_order_relaxed)));
    return name;
}

bool CameraStreamProducer::Start(const std::wstring& pipeName,
                                 std::string& error) noexcept
{
    HANDLE firstPipe = INVALID_HANDLE_VALUE;
    try
    {
        if (Running())
            return true;
        if (impl_->started)
        {
            error = "stream producer has already been started";
            return false;
        }
        if (!IsValidPipeName(pipeName))
        {
            error = "pipe name must start with \\\\.\\pipe\\";
            return false;
        }

        impl_->pipeName = pipeName;
        impl_->producerPid = ::GetCurrentProcessId();
        impl_->producerStartId =
            QpcNow() ^ (static_cast<uint64_t>(impl_->producerPid) << 32) ^
            ::GetTickCount64();
        impl_->stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (impl_->stopEvent == nullptr)
        {
            error = Win32Error("CreateEventW(stop)");
            return false;
        }

        if (!impl_->CreateFirstPipe(firstPipe, error))
        {
            ::CloseHandle(impl_->stopEvent);
            impl_->stopEvent = nullptr;
            return false;
        }
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
        impl_->firstPipeProbe.store(firstPipe, std::memory_order_release);
#endif

        impl_->started = true;
        impl_->mailbox.BeginAccepting();
        impl_->state.store(WorkerState::Starting, std::memory_order_release);
        try
        {
            impl_->worker =
                std::thread(&Impl::WorkerMain, impl_.get(), firstPipe);
        }
        catch (...)
        {
            impl_->state.store(WorkerState::Failed, std::memory_order_release);
            impl_->lastFailure.store(ERROR_UNHANDLED_EXCEPTION,
                                     std::memory_order_release);
            impl_->started = false;
            ::CloseHandle(firstPipe);
            firstPipe = INVALID_HANDLE_VALUE;
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
            impl_->firstPipeProbe.store(INVALID_HANDLE_VALUE,
                                        std::memory_order_release);
#endif
            ::CloseHandle(impl_->stopEvent);
            impl_->stopEvent = nullptr;
            error = "failed to create camera stream worker thread";
            return false;
        }
        return true;
    }
    catch (...)
    {
        if (firstPipe != INVALID_HANDLE_VALUE)
            ::CloseHandle(firstPipe);
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
        impl_->firstPipeProbe.store(INVALID_HANDLE_VALUE,
                                    std::memory_order_release);
#endif
        if (impl_->stopEvent != nullptr)
        {
            ::CloseHandle(impl_->stopEvent);
            impl_->stopEvent = nullptr;
        }
        impl_->started = false;
        impl_->state.store(WorkerState::Failed, std::memory_order_release);
        impl_->lastFailure.store(ERROR_UNHANDLED_EXCEPTION,
                                 std::memory_order_release);
        return false;
    }
}

void CameraStreamProducer::Stop() noexcept
{
    if (impl_)
        impl_->StopWorker();
}

bool CameraStreamProducer::Running() const noexcept
{
    if (!impl_)
        return false;
    const WorkerState state = impl_->state.load(std::memory_order_acquire);
    // Starting is already a live, accepting producer: Publish/OnLevelReady may
    // be called immediately after Start returns. Failed/Stopping/Stopped are
    // not accepting work.
    return state == WorkerState::Starting || state == WorkerState::Running;
}

WorkerState CameraStreamProducer::State() const noexcept
{
    return impl_ ? impl_->state.load(std::memory_order_acquire)
                 : WorkerState::Stopped;
}

uint32_t CameraStreamProducer::LastFailure() const noexcept
{
    return impl_ ? impl_->lastFailure.load(std::memory_order_acquire) : 0;
}

std::wstring CameraStreamProducer::PipeName() const
{
    return impl_ ? impl_->pipeName : std::wstring();
}

bool CameraStreamProducer::Publish(const CameraPacket& packet) noexcept
{
    if (!Running())
        return false;
    const uint64_t start = QpcNow();
    CameraPacket copy = packet;
    copy.publishQpc = start;
    const bool result = impl_->mailbox.EnqueueCamera(copy);
    const uint64_t elapsed = QpcToNanoseconds(QpcNow() - start);
    impl_->enqueueLatency.Record(elapsed);
    return result;
}

void CameraStreamProducer::OnLevelReady(uint64_t levelSession,
                                        uint64_t captureEpoch) noexcept
{
    if (!Running())
        return;
    impl_->levelSession.store(levelSession, std::memory_order_relaxed);
    impl_->captureEpoch.store(captureEpoch, std::memory_order_relaxed);
    impl_->mailbox.Invalidate(levelSession, captureEpoch,
                              InvalidateReason::LevelReady);
}

void CameraStreamProducer::Invalidate(uint64_t levelSession,
                                      uint64_t captureEpoch,
                                      InvalidateReason reason) noexcept
{
    if (!Running())
        return;
    impl_->levelSession.store(levelSession, std::memory_order_relaxed);
    impl_->captureEpoch.store(captureEpoch, std::memory_order_relaxed);
    impl_->mailbox.Invalidate(levelSession, captureEpoch, reason);
}

StreamCountersSnapshot CameraStreamProducer::Counters() const noexcept
{
    return impl_ ? SnapshotCounters(impl_->counters) : StreamCountersSnapshot{};
}

EnqueueStats CameraStreamProducer::EnqueueLatency() const noexcept
{
    return impl_ ? impl_->ComputeEnqueueStats() : EnqueueStats{};
}

bool CameraStreamProducer::CopyLastSent(SentSample& sample) const noexcept
{
    if (!impl_)
        return false;
    if (!::TryAcquireSRWLockShared(&impl_->lastSentLock))
        return false;
    const bool has = impl_->hasLastSent;
    if (has)
        sample = impl_->lastSent;
    ::ReleaseSRWLockShared(&impl_->lastSentLock);
    return has;
}

#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
HANDLE CameraStreamProducer::FirstPipeForTest() const noexcept
{
    return impl_ ? impl_->firstPipeProbe.load(std::memory_order_acquire)
                 : INVALID_HANDLE_VALUE;
}
#endif

struct CameraStreamClient::Impl
{
    std::unique_ptr<OverlappedPipeStream> stream;
    HANDLE stopEvent = nullptr;
    SRWLOCK clientLock{};
    mutable SRWLOCK lifecycleLock{};
    uint64_t lifecycleGeneration = 0;

    ~Impl() { Close(); }

    // All lifecycle mutations are serialized by lifecycleLock. Lock order is
    // lifecycleLock -> clientLock. Read takes clientLock shared only and never
    // takes lifecycleLock, so Close can signal the stop event first and then
    // wait for a pending Read to finish without deadlocking.
    uint64_t BeginLifecycle() noexcept
    {
        ::AcquireSRWLockExclusive(&lifecycleLock);
        ++lifecycleGeneration;
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
        if (ConsumeFault(StreamFaultSite::ClientClosePauseBeforeSignal))
        {
            g_clientClosePauseEntered.store(true, std::memory_order_release);
            const ULONGLONG deadline = ::GetTickCount64() + 10000;
            while (g_clientClosePause.load(std::memory_order_acquire) &&
                   ::GetTickCount64() < deadline)
            {
                ::Sleep(1);
            }
        }
#endif
        if (stopEvent != nullptr)
            ::SetEvent(stopEvent);

        ::AcquireSRWLockExclusive(&clientLock);
        std::unique_ptr<OverlappedPipeStream> dying = std::move(stream);
        const HANDLE handle = stopEvent;
        stopEvent = nullptr;
        ::ReleaseSRWLockExclusive(&clientLock);

        if (dying)
            dying.reset();
        if (handle != nullptr)
            ::CloseHandle(handle);
        const uint64_t generation = lifecycleGeneration;
        ::ReleaseSRWLockExclusive(&lifecycleLock);
        return generation;
    }

    void Close() noexcept { (void)BeginLifecycle(); }

    bool GenerationMatches(uint64_t generation) noexcept
    {
        ::AcquireSRWLockShared(&lifecycleLock);
        const bool matches = lifecycleGeneration == generation;
        ::ReleaseSRWLockShared(&lifecycleLock);
        return matches;
    }

    bool Publish(std::unique_ptr<OverlappedPipeStream>& newStream,
                 HANDLE newStop, uint64_t generation) noexcept
    {
        ::AcquireSRWLockExclusive(&lifecycleLock);
        if (lifecycleGeneration != generation)
        {
            ::ReleaseSRWLockExclusive(&lifecycleLock);
            return false;
        }

        ::AcquireSRWLockExclusive(&clientLock);
        std::unique_ptr<OverlappedPipeStream> dying = std::move(stream);
        const HANDLE oldStop = stopEvent;
        stream = std::move(newStream);
        stopEvent = newStop;
        ::ReleaseSRWLockExclusive(&clientLock);

        if (dying)
            dying.reset();
        if (oldStop != nullptr)
            ::CloseHandle(oldStop);
        ::ReleaseSRWLockExclusive(&lifecycleLock);
        return true;
    }

#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
    HANDLE StopEventForTest() const noexcept
    {
        ::AcquireSRWLockShared(&lifecycleLock);
        const HANDLE handle = stopEvent;
        ::ReleaseSRWLockShared(&lifecycleLock);
        return handle;
    }
#endif
};

CameraStreamClient::CameraStreamClient()
    : impl_(std::make_unique<Impl>())
{
}

CameraStreamClient::~CameraStreamClient()
{
    Close();
}

bool CameraStreamClient::Connect(const std::wstring& pipeName,
                                 DWORD timeoutMs, std::string& error) noexcept
{
    try
    {
        if (!impl_)
        {
            error = "camera stream client is not initialized";
            return false;
        }
        if (!IsValidPipeName(pipeName))
        {
            error = "pipe name must start with \\\\.\\pipe\\";
            return false;
        }

        // Connect starts a new client lifecycle. Any previous connection is
        // detached and closed under the lifecycle lock before IPC begins.
        const uint64_t generation = impl_->BeginLifecycle();
#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
        if (ConsumeFault(StreamFaultSite::ClientConnectPauseAfterBegin))
        {
            g_clientConnectPauseEntered.store(true, std::memory_order_release);
            const ULONGLONG deadline = ::GetTickCount64() + 10000;
            while (g_clientConnectPause.load(std::memory_order_acquire) &&
                   ::GetTickCount64() < deadline)
            {
                ::Sleep(1);
            }
        }
#endif

        // The raw pipe handle is owned locally until the stream object is
        // constructed and published. Any failure path therefore closes it.
        ScopedWinHandle pipeHandle;
        const ULONGLONG deadline = ::GetTickCount64() + timeoutMs;
        for (;;)
        {
            if (!impl_->GenerationMatches(generation))
            {
                error = "client closed during connect";
                return false;
            }
            const HANDLE pipe = ::CreateFileW(
                pipeName.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe != INVALID_HANDLE_VALUE)
            {
                pipeHandle.Reset(pipe);
                break;
            }
            const DWORD win32 = ::GetLastError();
            if (::GetTickCount64() >= deadline)
            {
                char text[160] = {};
                std::snprintf(text, sizeof(text),
                              "CreateFileW(pipe) failed (win32=%lu)",
                              static_cast<unsigned long>(win32));
                error = text;
                return false;
            }
            ::Sleep(kReconnectDelayMs);
        }

        if (!impl_->GenerationMatches(generation))
        {
            error = "client closed during connect";
            return false;
        }

#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
        const bool injectEventFailure =
            ConsumeFault(StreamFaultSite::ClientEventCreate);
#else
        const bool injectEventFailure = false;
#endif
        const HANDLE stop =
            injectEventFailure ? nullptr
                               : ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stop == nullptr)
        {
            error = injectEventFailure ? "CreateEventW(client stop) injected"
                                       : Win32Error("CreateEventW(client stop)");
            return false;
        }
        ScopedWinHandle stopHandle(stop);

#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
        if (ConsumeFault(StreamFaultSite::ClientStreamAllocate))
        {
            error = "failed to allocate overlapped pipe stream (injected)";
            return false;
        }
#endif
        std::unique_ptr<OverlappedPipeStream> stream(
            new (std::nothrow)
                OverlappedPipeStream(pipeHandle.Get(), stop, true));
        if (!stream)
        {
            error = "failed to allocate overlapped pipe stream";
            return false;
        }
        // The stream object now owns the pipe handle. Release the local guard
        // before validating so a failed ioEvent creation cannot double-close.
        pipeHandle.Release();
        if (!stream->Valid())
        {
            error = "failed to create overlapped pipe event";
            return false;
        }

        // Publication is the Connect linearization point. If a concurrent
        // Close happened after BeginLifecycle, the generation check fails and
        // the local stream/event are destroyed without publishing.
        if (!impl_->Publish(stream, stopHandle.Get(), generation))
        {
            error = "client closed during connect";
            return false;
        }
        stopHandle.Release();
        return true;
    }
    catch (...)
    {
        // Do not call Close() here: a newer concurrent Connect may already own
        // the client state. Local guards close only this attempt's resources.
        try
        {
            error = "camera stream client connect failed";
        }
        catch (...)
        {
        }
        return false;
    }
}

void CameraStreamClient::Close() noexcept
{
    if (impl_)
        impl_->Close();
}

bool CameraStreamClient::IsOpen() const noexcept
{
    if (!impl_)
        return false;
    ::AcquireSRWLockShared(&impl_->clientLock);
    const bool open = impl_->stream && impl_->stream->Valid();
    ::ReleaseSRWLockShared(&impl_->clientLock);
    return open;
}

MessageReadStatus CameraStreamClient::Read(StreamMessage& message,
                                           DWORD timeoutMs) noexcept
{
    if (!impl_)
        return MessageReadStatus::Closed;
    ::AcquireSRWLockShared(&impl_->clientLock);
    MessageReadStatus status = MessageReadStatus::Closed;
    if (impl_->stream && impl_->stream->Valid())
        status = ReadMessage(*impl_->stream, message, timeoutMs);
    ::ReleaseSRWLockShared(&impl_->clientLock);
    return status;
}

#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
HANDLE CameraStreamClient::StopEventForTest() const noexcept
{
    return impl_ ? impl_->StopEventForTest() : nullptr;
}

void CameraStreamClient::AdoptStopEventForTest(HANDLE stopEvent) noexcept
{
    if (!impl_)
    {
        if (stopEvent != nullptr)
            ::CloseHandle(stopEvent);
        return;
    }
    impl_->Close();
    if (stopEvent == nullptr)
        return;
    ::AcquireSRWLockExclusive(&impl_->lifecycleLock);
    impl_->stopEvent = stopEvent;
    ::ReleaseSRWLockExclusive(&impl_->lifecycleLock);
}
#endif

void ReceiverValidationState::BeginConnection(uint64_t generation) noexcept
{
    connectionGeneration = generation;
    haveHello = false;
}

bool ReceiverValidationState::ObserveHello(const HelloPayload& hello,
                                           std::string& reason) noexcept
{
    const bool newProducer =
        producerStartId != 0 && hello.producerStartId != producerStartId;
    if (producerStartId == 0 || newProducer)
    {
        producerStartId = hello.producerStartId;
        producerPid = hello.producerPid;
        haveSession = false;
        haveSequence = false;
        shutdown = false;
        lastLevelSession = hello.levelSession;
        lastCaptureEpoch = hello.captureEpoch;
        lastCaptureSequence = 0;
        if (hello.levelSession != 0 && hello.captureEpoch != 0)
            haveSession = true;
    }
    else if (hello.producerPid != producerPid)
    {
        reason = "producer pid changed without a new producer lifetime";
        ++rejectedPackets;
        return false;
    }
    haveHello = true;
    shutdown = false;
    return true;
}

bool ReceiverValidationState::ObserveInvalidate(
    const InvalidatePayload& invalidate, std::string& reason) noexcept
{
    if (!haveHello)
    {
        reason = "invalidate before HELLO";
        ++rejectedPackets;
        return false;
    }
    if (invalidate.levelSession < lastLevelSession)
    {
        reason = "stale invalidate session";
        ++staleSessionPackets;
        ++rejectedPackets;
        return false;
    }
    if (invalidate.levelSession == lastLevelSession &&
        invalidate.captureEpoch < lastCaptureEpoch)
    {
        reason = "stale invalidate epoch";
        ++staleEpochPackets;
        ++rejectedPackets;
        return false;
    }

    lastLevelSession = invalidate.levelSession;
    lastCaptureEpoch = invalidate.captureEpoch;
    lastCaptureSequence = 0;
    haveSession = invalidate.levelSession != 0 && invalidate.captureEpoch != 0;
    haveSequence = false;
    shutdown = false;

    switch (invalidate.reason)
    {
    case InvalidateReason::LevelReady:
    case InvalidateReason::Reset:
        // The next camera of this generation establishes the sequence
        // baseline; gaps are counted only between received cameras of the
        // same session and epoch.
        break;
    case InvalidateReason::Teardown:
        // No camera from this generation may be accepted after teardown; the
        // next level session is strictly greater and starts a new sequence.
        haveSequence = true;
        lastCaptureSequence = std::numeric_limits<uint64_t>::max();
        break;
    case InvalidateReason::Shutdown:
        shutdown = true;
        haveSequence = true;
        lastCaptureSequence = std::numeric_limits<uint64_t>::max();
        break;
    }
    return true;
}

bool ReceiverValidationState::ObserveCamera(
    const cameracapture12::CameraCaptureFile12& capture,
    std::string& reason) noexcept
{
    if (!haveHello)
    {
        reason = "camera before HELLO";
        ++rejectedPackets;
        return false;
    }
    if (shutdown)
    {
        reason = "camera after shutdown";
        ++rejectedPackets;
        return false;
    }
    const uint64_t session = capture.input.levelSession;
    const uint64_t epoch = capture.input.captureEpoch;
    const uint64_t sequence = capture.input.captureSequence;
    if (session == 0 || epoch == 0 || sequence == 0)
    {
        reason = "zero session/epoch/sequence";
        ++rejectedPackets;
        return false;
    }

    if (!haveSession)
    {
        lastLevelSession = session;
        lastCaptureEpoch = epoch;
        lastCaptureSequence = sequence;
        haveSession = true;
        haveSequence = true;
        ++acceptedFrames;
        return true;
    }

    if (session < lastLevelSession)
    {
        reason = "stale session";
        ++staleSessionPackets;
        ++rejectedPackets;
        return false;
    }
    if (session > lastLevelSession)
    {
        lastLevelSession = session;
        lastCaptureEpoch = epoch;
        lastCaptureSequence = sequence;
        haveSequence = true;
        ++acceptedFrames;
        return true;
    }
    if (epoch < lastCaptureEpoch)
    {
        reason = "stale epoch";
        ++staleEpochPackets;
        ++rejectedPackets;
        return false;
    }
    if (epoch > lastCaptureEpoch)
    {
        lastCaptureEpoch = epoch;
        lastCaptureSequence = sequence;
        haveSequence = true;
        ++acceptedFrames;
        return true;
    }
    if (!haveSequence)
    {
        lastCaptureSequence = sequence;
        haveSequence = true;
        ++acceptedFrames;
        return true;
    }
    if (sequence == lastCaptureSequence)
    {
        reason = "duplicate sequence";
        ++duplicatePackets;
        ++rejectedPackets;
        return false;
    }
    if (sequence < lastCaptureSequence)
    {
        reason = "reordered sequence";
        ++reorderedPackets;
        ++rejectedPackets;
        return false;
    }

    gapFrames += sequence - lastCaptureSequence - 1;
    lastCaptureSequence = sequence;
    ++acceptedFrames;
    return true;
}

#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
void SetStreamFault(StreamFaultSite site) noexcept
{
    g_streamFault.store(site, std::memory_order_release);
}

void ClearStreamFault() noexcept
{
    g_streamFault.store(StreamFaultSite::None, std::memory_order_release);
}

StreamFaultSite CurrentStreamFault() noexcept
{
    return g_streamFault.load(std::memory_order_acquire);
}

void SetPreSubmitBlock(bool block) noexcept
{
    g_preSubmitEntered.store(false, std::memory_order_release);
    g_preSubmitBlock.store(block, std::memory_order_release);
}

bool WaitPreSubmitEntered(DWORD timeoutMs) noexcept
{
    const ULONGLONG deadline = ::GetTickCount64() + timeoutMs;
    while (!g_preSubmitEntered.load(std::memory_order_acquire))
    {
        if (::GetTickCount64() >= deadline)
            return false;
        ::Sleep(1);
    }
    return true;
}

void ReleasePreSubmitBlock() noexcept
{
    g_preSubmitBlock.store(false, std::memory_order_release);
}

void SetClientClosePause(bool pause) noexcept
{
    g_clientClosePauseEntered.store(false, std::memory_order_release);
    g_clientClosePause.store(pause, std::memory_order_release);
}

bool WaitClientClosePauseEntered(DWORD timeoutMs) noexcept
{
    const ULONGLONG deadline = ::GetTickCount64() + timeoutMs;
    while (!g_clientClosePauseEntered.load(std::memory_order_acquire))
    {
        if (::GetTickCount64() >= deadline)
            return false;
        ::Sleep(1);
    }
    return true;
}

void ReleaseClientClosePause() noexcept
{
    g_clientClosePause.store(false, std::memory_order_release);
}

void SetWorkerPauseBeforeLoop(bool pause) noexcept
{
    g_workerPauseBeforeLoopEntered.store(false, std::memory_order_release);
    g_workerPauseBeforeLoop.store(pause, std::memory_order_release);
}

bool WaitWorkerPauseBeforeLoopEntered(DWORD timeoutMs) noexcept
{
    const ULONGLONG deadline = ::GetTickCount64() + timeoutMs;
    while (!g_workerPauseBeforeLoopEntered.load(std::memory_order_acquire))
    {
        if (::GetTickCount64() >= deadline)
            return false;
        ::Sleep(1);
    }
    return true;
}

void ReleaseWorkerPauseBeforeLoop() noexcept
{
    g_workerPauseBeforeLoop.store(false, std::memory_order_release);
}

void SetClientConnectPause(bool pause) noexcept
{
    g_clientConnectPauseEntered.store(false, std::memory_order_release);
    g_clientConnectPause.store(pause, std::memory_order_release);
}

bool WaitClientConnectPauseEntered(DWORD timeoutMs) noexcept
{
    const ULONGLONG deadline = ::GetTickCount64() + timeoutMs;
    while (!g_clientConnectPauseEntered.load(std::memory_order_acquire))
    {
        if (::GetTickCount64() >= deadline)
            return false;
        ::Sleep(1);
    }
    return true;
}

void ReleaseClientConnectPause() noexcept
{
    g_clientConnectPause.store(false, std::memory_order_release);
}

OverlappedProbeResult RunOverlappedIoProbe(HANDLE pipe, HANDLE stopEvent,
                                           void* buffer, DWORD bytes, bool read,
                                           DWORD timeoutMs) noexcept
{
    OverlappedProbeResult probe;
    OVERLAPPED overlapped{};
    overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr)
    {
        probe.error = ::GetLastError();
        return probe;
    }
    ::ResetEvent(overlapped.hEvent);

    DWORD actual = 0;
    const BOOL started =
        read ? ::ReadFile(pipe, buffer, bytes, &actual, &overlapped)
             : ::WriteFile(pipe, buffer, bytes, &actual, &overlapped);
    IoCompletion completion;
    if (started)
    {
        completion.state = IoOperationState::Completed;
        completion.bytes = actual;
    }
    else
    {
        const DWORD error = ::GetLastError();
        if (error == ERROR_IO_PENDING)
        {
            completion = WaitForTerminalCompletion(pipe, overlapped,
                                                  overlapped.hEvent, stopEvent,
                                                  timeoutMs);
        }
        else
        {
            completion.state = IoOperationState::FailedCompleted;
            completion.error = error;
        }
    }

    probe.returned = true;
    probe.state = completion.state;
    probe.bytes = completion.bytes;
    probe.error = completion.error;
    probe.overlappedCompleteAfterReturn =
        HasOverlappedIoCompleted(&overlapped) != FALSE;
    probe.eventSignaledAfterReturn =
        ::WaitForSingleObject(overlapped.hEvent, 0) == WAIT_OBJECT_0;
    ::CloseHandle(overlapped.hEvent);
    return probe;
}
#endif

} // namespace b3b2
