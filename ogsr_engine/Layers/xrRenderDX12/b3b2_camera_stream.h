#pragma once

// F4.0-B3-B2A: CPU-only live published-camera transport.
//
// The Engine owner thread copies an already-published B2.3 camera into a
// fixed-size latest-only mailbox. A dedicated worker thread encodes and
// writes it to a local named pipe. No Engine pointer, GPU object or Device
// state crosses the thread or process boundary.

#include "dx12cameracapturefile.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace b3b2
{
constexpr uint8_t kProtocolMagic[4] = {'O', 'G', 'S', '2'};
constexpr uint16_t kProtocolVersion = 1;
constexpr size_t kFrameHeaderBytes = 20;
constexpr size_t kHelloPayloadBytes = 32;
constexpr size_t kInvalidatePayloadBytes = 24;
constexpr size_t kCameraFrameHeaderBytes = 16;
// Codec payload is limited to 4096 bytes; the extra framing is small. Reject
// anything outside this hard bound before allocating a payload buffer.
constexpr uint32_t kMaxPayloadBytes = 8192;
constexpr uint32_t kMaxCodecBytes = cameracapture12::kMaxPayloadBytes;

enum class StreamMessageType : uint16_t
{
    Hello = 1,
    CameraFrame = 2,
    InvalidateSession = 3,
    Shutdown = 4
};

enum class InvalidateReason : uint32_t
{
    LevelReady = 1,
    Reset = 2,
    Teardown = 3,
    Shutdown = 4
};

enum class WorkerState : uint8_t
{
    Starting = 0,
    Running,
    Recovering,
    Stopping,
    Stopped,
    Failed
};

const char* WorkerStateName(WorkerState state) noexcept;

// Terminal-aware overlapped operation contract. A pending operation is never
// abandoned to stack unwinding: the implementation waits for a terminal state
// before destroying OVERLAPPED, event or buffer storage.
enum class IoOperationState : uint8_t
{
    NotStarted = 0,
    Pending,
    CancelRequested,
    Completed,
    Cancelled,
    FailedCompleted,
    CompletionUnknown
};

const char* IoOperationStateName(IoOperationState state) noexcept;

const char* MessageTypeName(StreamMessageType type) noexcept;
const char* InvalidateReasonName(InvalidateReason reason) noexcept;

enum class StreamIoStatus : uint8_t
{
    Ok = 0,
    Timeout,
    Closed,
    Failed
};

class IByteStream
{
public:
    virtual ~IByteStream() = default;
    virtual StreamIoStatus Read(void* buffer, size_t bytes, size_t& transferred,
                                DWORD timeoutMs) noexcept = 0;
    virtual StreamIoStatus Write(const void* buffer, size_t bytes,
                                 size_t& transferred,
                                 DWORD timeoutMs) noexcept = 0;
};

struct StreamMessage
{
    StreamMessageType type = StreamMessageType::Shutdown;
    std::vector<uint8_t> payload;
};

enum class MessageReadStatus : uint8_t
{
    Ok = 0,
    Timeout,
    Closed,
    Truncated,
    BadMagic,
    UnsupportedVersion,
    UnknownType,
    Oversized,
    CrcMismatch,
    InternalError,
    InvalidPayload
};

const char* MessageReadStatusName(MessageReadStatus status) noexcept;

struct HelloPayload
{
    uint32_t producerPid = 0;
    uint64_t producerStartId = 0;
    uint64_t levelSession = 0;
    uint64_t captureEpoch = 0;
};

struct InvalidatePayload
{
    uint64_t levelSession = 0;
    uint64_t captureEpoch = 0;
    InvalidateReason reason = InvalidateReason::LevelReady;
};

struct CameraFrameMeta
{
    uint64_t publishQpc = 0;
    uint32_t codecBytes = 0;
    uint32_t flags = 0;
};

std::vector<uint8_t> SerializeHello(const HelloPayload& hello);
bool ParseHello(std::span<const uint8_t> payload, HelloPayload& hello) noexcept;
std::vector<uint8_t> SerializeInvalidate(const InvalidatePayload& invalidate);
bool ParseInvalidate(std::span<const uint8_t> payload,
                     InvalidatePayload& invalidate) noexcept;

std::vector<uint8_t> SerializeCameraFrame(const CameraFrameMeta& meta,
                                          std::span<const uint8_t> codecBytes);
bool ParseCameraFrame(std::span<const uint8_t> payload, CameraFrameMeta& meta,
                      std::span<const uint8_t>& codecBytes) noexcept;

// Wire framing. ReadMessage handles partial reads and rejects unknown versions,
// types, oversized lengths and bad CRC before exposing a payload. WriteMessage
// handles partial writes.
MessageReadStatus ReadMessage(IByteStream& stream, StreamMessage& message,
                              DWORD timeoutMs) noexcept;
StreamIoStatus WriteMessage(IByteStream& stream, StreamMessageType type,
                            std::span<const uint8_t> payload,
                            DWORD timeoutMs) noexcept;

struct CameraPacket
{
    cameracapture12::CameraCaptureFile12 capture{};
    uint64_t publishQpc = 0;
};

uint64_t ComputePacketHash(
    const cameracapture12::CameraCaptureFile12& capture) noexcept;

struct StreamCounters
{
    std::atomic<uint64_t> published{0};
    std::atomic<uint64_t> enqueued{0};
    std::atomic<uint64_t> overwritten{0};
    std::atomic<uint64_t> lockDropped{0};
    std::atomic<uint64_t> encoded{0};
    std::atomic<uint64_t> sent{0};
    std::atomic<uint64_t> rejected{0};
    std::atomic<uint64_t> disconnected{0};
    std::atomic<uint64_t> reconnected{0};
};

struct StreamCountersSnapshot
{
    uint64_t published = 0;
    uint64_t enqueued = 0;
    uint64_t overwritten = 0;
    uint64_t lockDropped = 0;
    uint64_t encoded = 0;
    uint64_t sent = 0;
    uint64_t rejected = 0;
    uint64_t disconnected = 0;
    uint64_t reconnected = 0;
};

StreamCountersSnapshot SnapshotCounters(const StreamCounters& counters) noexcept;

struct WorkItem
{
    enum class Kind : uint8_t
    {
        None = 0,
        Camera,
        Invalidate,
        Shutdown
    };

    Kind kind = Kind::None;
    CameraPacket camera{};
    uint64_t generation = 0;
    uint64_t levelSession = 0;
    uint64_t captureEpoch = 0;
    InvalidateReason reason = InvalidateReason::LevelReady;
};

// One pending packet plus one in-flight packet is the whole queue. A newer
// camera replaces an unclaimed pending camera. Enqueue never waits for the
// worker: TryAcquireSRWLockExclusive failure counts lockDropped.
class LatestOnlyMailbox
{
public:
    explicit LatestOnlyMailbox(StreamCounters& counters) noexcept;
    ~LatestOnlyMailbox();

    LatestOnlyMailbox(const LatestOnlyMailbox&) = delete;
    LatestOnlyMailbox& operator=(const LatestOnlyMailbox&) = delete;

    bool EnqueueCamera(const CameraPacket& packet) noexcept;
    void Invalidate(uint64_t levelSession, uint64_t captureEpoch,
                    InvalidateReason reason) noexcept;
    void RequestShutdown() noexcept;
    bool Take(WorkItem& item) noexcept;

    // Linearization point for a taken camera packet. The mailbox generation is
    // checked atomically with marking the send as in-flight. After true is
    // returned, a later invalidation cannot retroactively revoke the write.
    bool TryBeginSend(uint64_t generation) noexcept;
    void EndSend() noexcept;
    void BeginAccepting() noexcept;
    void Abandon() noexcept;

    HANDLE WakeEvent() const noexcept { return wakeEvent_; }
    void SignalWake() const noexcept;
    uint64_t Generation() const noexcept
    {
        return generation_.load(std::memory_order_acquire);
    }
    StreamCounters& Counters() noexcept { return counters_; }
    const StreamCounters& Counters() const noexcept { return counters_; }

#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
    void LockForTest() noexcept;
    void UnlockForTest() noexcept;
#endif

private:
    void SetControlLocked(WorkItem::Kind kind, uint64_t levelSession,
                          uint64_t captureEpoch,
                          InvalidateReason reason) noexcept;

    StreamCounters& counters_;
    SRWLOCK lock_{};
    HANDLE wakeEvent_ = nullptr;
    CameraPacket pendingCamera_{};
    bool hasPendingCamera_ = false;
    WorkItem control_{};
    bool hasControl_ = false;
    bool accepting_ = true;
    bool sendInProgress_ = false;
    std::atomic<uint64_t> generation_{0};
};

struct EnqueueStats
{
    uint64_t samples = 0;
    uint64_t medianNs = 0;
    uint64_t p95Ns = 0;
    uint64_t p99Ns = 0;
    uint64_t maxNs = 0;
};

// Fixed-capacity, allocation-free owner-thread latency history. The write
// index keeps rotating after the window is full; the valid count stays capped.
class EnqueueLatencyRing
{
public:
    static constexpr size_t kCapacity = 4096;

    void Record(uint64_t nanoseconds) noexcept;
    void Reset() noexcept;
    EnqueueStats Stats() const noexcept;

    uint32_t WriteIndex() const noexcept { return writeIndex_; }
    uint32_t ValidCount() const noexcept { return validCount_; }

private:
    std::array<uint64_t, kCapacity> values_{};
    uint32_t writeIndex_ = 0;
    uint32_t validCount_ = 0;
};

struct SentSample
{
    CameraPacket packet{};
    uint64_t hash = 0;
    uint64_t sentQpc = 0;
};

// Engine-side producer. Start() creates the first local pipe instance and
// starts one worker. Stop() cancels outstanding overlapped I/O and then joins
// the worker. The join is intentionally not time-bounded: freeing OVERLAPPED,
// event, pipe or buffer storage before confirmed terminal completion is not
// permitted. On a pathological OS stall, shutdown can therefore wait; this is
// the documented Variant A contract. Publish() never performs I/O or waits.
class CameraStreamProducer
{
public:
    CameraStreamProducer();
    ~CameraStreamProducer();
    CameraStreamProducer(const CameraStreamProducer&) = delete;
    CameraStreamProducer& operator=(const CameraStreamProducer&) = delete;

    bool Start(const std::wstring& pipeName, std::string& error) noexcept;
    void Stop() noexcept;
    bool Running() const noexcept;
    WorkerState State() const noexcept;
    uint32_t LastFailure() const noexcept;
    std::wstring PipeName() const;

    bool Publish(const CameraPacket& packet) noexcept;
    void OnLevelReady(uint64_t levelSession, uint64_t captureEpoch) noexcept;
    void Invalidate(uint64_t levelSession, uint64_t captureEpoch,
                    InvalidateReason reason) noexcept;

    StreamCountersSnapshot Counters() const noexcept;
    EnqueueStats EnqueueLatency() const noexcept;
    bool CopyLastSent(SentSample& sample) const noexcept;

#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
    // Raw value of the first pipe HANDLE passed to the worker. The test only
    // probes whether it is still open after an early exit.
    HANDLE FirstPipeForTest() const noexcept;
#endif

    static std::wstring MakePipeName();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Receiver-side client used by the standalone consumer and transport tests.
class CameraStreamClient
{
public:
    CameraStreamClient();
    ~CameraStreamClient();
    CameraStreamClient(const CameraStreamClient&) = delete;
    CameraStreamClient& operator=(const CameraStreamClient&) = delete;

    bool Connect(const std::wstring& pipeName, DWORD timeoutMs,
                 std::string& error) noexcept;
    void Close() noexcept;
    bool IsOpen() const noexcept;
    MessageReadStatus Read(StreamMessage& message, DWORD timeoutMs) noexcept;

#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
    HANDLE StopEventForTest() const noexcept;
    // Takes ownership of a caller-created event. Used only to exercise the
    // client lifecycle without a real pipe.
    void AdoptStopEventForTest(HANDLE stopEvent) noexcept;
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Pure receiver validation: no OS, Engine or codec side effects. Codec Decode
// has already validated the camera; this state enforces connection generation,
// session/epoch monotonicity and sequence gaps/reordering.
class ReceiverValidationState
{
public:
    void BeginConnection(uint64_t connectionGeneration) noexcept;
    bool ObserveHello(const HelloPayload& hello, std::string& reason) noexcept;
    bool ObserveInvalidate(const InvalidatePayload& invalidate,
                           std::string& reason) noexcept;
    bool ObserveCamera(const cameracapture12::CameraCaptureFile12& capture,
                       std::string& reason) noexcept;

    uint64_t connectionGeneration = 0;
    uint64_t producerStartId = 0;
    uint32_t producerPid = 0;
    bool haveHello = false;
    bool haveSession = false;
    bool haveSequence = false;
    bool shutdown = false;
    uint64_t lastLevelSession = 0;
    uint64_t lastCaptureEpoch = 0;
    uint64_t lastCaptureSequence = 0;
    uint64_t acceptedFrames = 0;
    uint64_t gapFrames = 0;
    uint64_t duplicatePackets = 0;
    uint64_t reorderedPackets = 0;
    uint64_t staleSessionPackets = 0;
    uint64_t staleEpochPackets = 0;
    uint64_t rejectedPackets = 0;
};

#ifdef OGSR_B3B2_STREAM_TEST_HOOKS
enum class StreamFaultSite : uint8_t
{
    None = 0,
    HelloSerialize,
    InvalidateSerialize,
    CameraSerialize,
    ClientStreamAllocate,
    ConnectWaitFailed,
    ReadCancelBeforeCompletion,
    WriteCancelBeforeCompletion,
    CancelIoNotFound,
    WorkerUnexpectedThrow,
    ClientEventCreate,
    OverlappedEventCreate,
    WorkerThrowAfterOwnership,
    ClientClosePauseBeforeSignal,
    ProducerStreamAllocate,
    WorkerPauseBeforeLoop,
    ClientConnectPauseAfterBegin
};

void SetStreamFault(StreamFaultSite site) noexcept;
void ClearStreamFault() noexcept;
StreamFaultSite CurrentStreamFault() noexcept;

// Test-only gate between camera encoding and the send linearization point.
// It lets a test invalidate the session exactly inside that window.
void SetPreSubmitBlock(bool block) noexcept;
bool WaitPreSubmitEntered(DWORD timeoutMs) noexcept;
void ReleasePreSubmitBlock() noexcept;

// Test-only pause inside CameraStreamClient::Impl::Close after the lifecycle
// lock is held and before the stop event is signaled.
void SetClientClosePause(bool pause) noexcept;
bool WaitClientClosePauseEntered(DWORD timeoutMs) noexcept;
void ReleaseClientClosePause() noexcept;

// Test-only pause at worker entry before the first loop condition.
void SetWorkerPauseBeforeLoop(bool pause) noexcept;
bool WaitWorkerPauseBeforeLoopEntered(DWORD timeoutMs) noexcept;
void ReleaseWorkerPauseBeforeLoop() noexcept;

// Test-only pause in Connect after BeginLifecycle and before IPC.
void SetClientConnectPause(bool pause) noexcept;
bool WaitClientConnectPauseEntered(DWORD timeoutMs) noexcept;
void ReleaseClientConnectPause() noexcept;

struct OverlappedProbeResult
{
    bool returned = false;
    IoOperationState state = IoOperationState::NotStarted;
    DWORD bytes = 0;
    DWORD error = 0;
    bool overlappedCompleteAfterReturn = false;
    bool eventSignaledAfterReturn = false;
};

// Runs exactly one overlapped read/write on an already connected overlapped
// pipe through the production terminal-completion policy. Used by the fault
// suite to prove that OVERLAPPED/event storage is terminal on return.
OverlappedProbeResult RunOverlappedIoProbe(HANDLE pipe, HANDLE stopEvent,
                                           void* buffer, DWORD bytes, bool read,
                                           DWORD timeoutMs) noexcept;
#endif

} // namespace b3b2
