// F4.0-B3-B2A CPU-only transport tests. No Engine, Device or D3D12 device is
// created. The real named-pipe test uses a local producer/client pair.

#include "b3b2_camera_stream.h"
#include "dx12gamecamera.h"

#include <Windows.h>
#include <DirectXMath.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <thread>
#include <vector>

#ifndef __SANITIZE_ADDRESS__
namespace
{
std::atomic<size_t> g_failAllocMin{0};
std::atomic<size_t> g_failAllocMax{0};
std::atomic<bool> g_failAllocArmed{false};
std::atomic<size_t> g_failedAllocSize{0};
} // namespace

void* operator new(std::size_t size)
{
    if (g_failAllocArmed.load(std::memory_order_acquire) &&
        size >= g_failAllocMin.load(std::memory_order_relaxed) &&
        size <= g_failAllocMax.load(std::memory_order_relaxed))
    {
        g_failedAllocSize.store(size, std::memory_order_release);
        g_failAllocArmed.store(false, std::memory_order_release);
        throw std::bad_alloc();
    }
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr)
        throw std::bad_alloc();
    return memory;
}

void* operator new[](std::size_t size) { return ::operator new(size); }

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

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
    return ::operator new(size, tag);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
#endif

using cameracapture12::CameraCaptureFile12;
using b3b2::CameraPacket;
using b3b2::CameraStreamClient;
using b3b2::CameraStreamProducer;
using b3b2::HelloPayload;
using b3b2::InvalidatePayload;
using b3b2::MessageReadStatus;
using b3b2::ReceiverValidationState;
using b3b2::StreamMessage;
using b3b2::StreamMessageType;

namespace
{
int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* what)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

void PutU16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
{
    bytes[offset + 0] = static_cast<uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void PutU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    for (uint32_t i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
}

gamecamera12::CameraCaptureInput12 MakeInput(uint64_t session,
                                             uint64_t epoch,
                                             uint64_t sequence,
                                             uint32_t frame)
{
    gamecamera12::CameraCaptureInput12 input{};
    const DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(
        DirectX::XMVectorSet(10.0f, 5.0f, -3.0f, 0.0f),
        DirectX::XMVectorSet(0.0f, 1.0f, 4.0f, 0.0f),
        DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    const DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XM_PIDIV4, 16.0f / 9.0f, 0.2f, 350.0f);
    DirectX::XMStoreFloat4x4(&input.view, view);
    DirectX::XMStoreFloat4x4(&input.projection, projection);
    DirectX::XMStoreFloat4x4(&input.fullTransform, view * projection);
    input.hasFullTransform = true;
    input.position = {10.0f, 5.0f, -3.0f};
    input.sourceGameFrame = frame;
    input.captureSequence = sequence;
    input.levelSession = session;
    input.captureEpoch = epoch;
    input.viewportWidth = 1920;
    input.viewportHeight = 1080;
    return input;
}

CameraCaptureFile12 MakeCapture(uint64_t session = 2, uint64_t epoch = 9,
                                uint64_t sequence = 7, uint32_t frame = 1234)
{
    CameraCaptureFile12 capture{};
    std::snprintf(capture.levelName, sizeof(capture.levelName), "%s", "jupiter");
    capture.input = MakeInput(session, epoch, sequence, frame);
    return capture;
}

CameraPacket MakePacket(uint64_t session = 2, uint64_t epoch = 9,
                        uint64_t sequence = 7, uint32_t frame = 1234)
{
    CameraPacket packet;
    packet.capture = MakeCapture(session, epoch, sequence, frame);
    packet.publishQpc = 1;
    return packet;
}

class MockStream final : public b3b2::IByteStream
{
public:
    std::vector<uint8_t> input;
    std::vector<uint8_t> output;
    size_t readPosition = 0;
    size_t maxChunk = 3;
    bool failWrite = false;
    bool timeoutOnEmpty = false;

    b3b2::StreamIoStatus Read(void* buffer, size_t bytes, size_t& transferred,
                              DWORD) noexcept override
    {
        transferred = 0;
        if (readPosition >= input.size())
            return timeoutOnEmpty ? b3b2::StreamIoStatus::Timeout
                                  : b3b2::StreamIoStatus::Closed;
        const size_t count =
            std::min({bytes, maxChunk, input.size() - readPosition});
        std::memcpy(buffer, input.data() + readPosition, count);
        readPosition += count;
        transferred = count;
        return b3b2::StreamIoStatus::Ok;
    }

    b3b2::StreamIoStatus Write(const void* buffer, size_t bytes,
                               size_t& transferred, DWORD) noexcept override
    {
        transferred = 0;
        if (failWrite)
            return b3b2::StreamIoStatus::Failed;
        const size_t count = std::min(bytes, maxChunk);
        const uint8_t* data = static_cast<const uint8_t*>(buffer);
        output.insert(output.end(), data, data + count);
        transferred = count;
        return b3b2::StreamIoStatus::Ok;
    }
};

std::vector<uint8_t> BuildValidMessage(StreamMessageType type,
                                       std::span<const uint8_t> payload)
{
    MockStream stream;
    stream.maxChunk = 1024;
    const b3b2::StreamIoStatus status =
        b3b2::WriteMessage(stream, type, payload, 1000);
    Check(status == b3b2::StreamIoStatus::Ok, "test message writes");
    return stream.output;
}

void TestProtocol()
{
    const HelloPayload hello{1234, 0x1122334455667788ull, 5, 9};
    const std::vector<uint8_t> helloBytes = b3b2::SerializeHello(hello);
    HelloPayload parsedHello{};
    Check(helloBytes.size() == b3b2::kHelloPayloadBytes,
          "HELLO payload size");
    Check(b3b2::ParseHello(helloBytes, parsedHello), "HELLO parses");
    Check(parsedHello.producerPid == hello.producerPid,
          "HELLO pid round-trip");
    Check(parsedHello.producerStartId == hello.producerStartId,
          "HELLO generation round-trip");
    Check(parsedHello.levelSession == hello.levelSession &&
              parsedHello.captureEpoch == hello.captureEpoch,
          "HELLO session/epoch round-trip");

    const InvalidatePayload invalidate{7, 11, b3b2::InvalidateReason::Reset};
    const std::vector<uint8_t> invalidateBytes =
        b3b2::SerializeInvalidate(invalidate);
    InvalidatePayload parsedInvalidate{};
    Check(b3b2::ParseInvalidate(invalidateBytes, parsedInvalidate),
          "INVALIDATE parses");
    Check(parsedInvalidate.levelSession == invalidate.levelSession &&
              parsedInvalidate.captureEpoch == invalidate.captureEpoch &&
              parsedInvalidate.reason == invalidate.reason,
          "INVALIDATE fields round-trip");

    const std::vector<uint8_t> helloMessage =
        BuildValidMessage(StreamMessageType::Hello, helloBytes);
    MockStream partial;
    partial.input = helloMessage;
    partial.maxChunk = 1;
    StreamMessage message;
    Check(b3b2::ReadMessage(partial, message, 1000) ==
              MessageReadStatus::Ok,
          "partial HELLO read succeeds");
    Check(message.type == StreamMessageType::Hello, "HELLO type preserved");
    Check(b3b2::ParseHello(message.payload, parsedHello),
          "partial read payload decodes");

    MockStream writer;
    writer.maxChunk = 1;
    Check(b3b2::WriteMessage(writer, StreamMessageType::Hello, helloBytes,
                             1000) == b3b2::StreamIoStatus::Ok,
          "partial HELLO write succeeds");
    Check(writer.output == helloMessage, "partial write produces exact bytes");

    {
        std::vector<uint8_t> bad = helloMessage;
        bad[0] ^= 0x01;
        MockStream stream;
        stream.input = bad;
        Check(b3b2::ReadMessage(stream, message, 1000) ==
                  MessageReadStatus::BadMagic,
              "bad magic rejected");
    }
    {
        std::vector<uint8_t> bad = helloMessage;
        PutU16(bad, 4, 2);
        MockStream stream;
        stream.input = bad;
        Check(b3b2::ReadMessage(stream, message, 1000) ==
                  MessageReadStatus::UnsupportedVersion,
              "unsupported version rejected");
    }
    {
        std::vector<uint8_t> bad = helloMessage;
        PutU16(bad, 6, 99);
        MockStream stream;
        stream.input = bad;
        Check(b3b2::ReadMessage(stream, message, 1000) ==
                  MessageReadStatus::UnknownType,
              "unknown type rejected");
    }
    {
        std::vector<uint8_t> bad = helloMessage;
        PutU32(bad, 8, b3b2::kMaxPayloadBytes + 1);
        MockStream stream;
        stream.input = bad;
        Check(b3b2::ReadMessage(stream, message, 1000) ==
                  MessageReadStatus::Oversized,
              "oversized length rejected before allocation");
    }
    {
        std::vector<uint8_t> bad = helloMessage;
        PutU32(bad, 12, 0);
        MockStream stream;
        stream.input = bad;
        Check(b3b2::ReadMessage(stream, message, 1000) ==
                  MessageReadStatus::CrcMismatch,
              "bad CRC rejected");
    }
    {
        std::vector<uint8_t> bad = helloMessage;
        bad.resize(10);
        MockStream stream;
        stream.input = bad;
        Check(b3b2::ReadMessage(stream, message, 1000) ==
                  MessageReadStatus::Truncated,
              "truncated header rejected");
    }
    {
        std::vector<uint8_t> bad = helloMessage;
        bad.resize(helloMessage.size() - 3);
        MockStream stream;
        stream.input = bad;
        Check(b3b2::ReadMessage(stream, message, 1000) ==
                  MessageReadStatus::Truncated,
              "truncated payload rejected");
    }
    {
        MockStream stream;
        stream.timeoutOnEmpty = true;
        Check(b3b2::ReadMessage(stream, message, 10) ==
                  MessageReadStatus::Timeout,
              "empty stream timeout is distinct from truncation");
    }
    {
        std::vector<uint8_t> bad = helloMessage;
        PutU32(bad, 16, 1);
        MockStream stream;
        stream.input = bad;
        Check(b3b2::ReadMessage(stream, message, 1000) ==
                  MessageReadStatus::InvalidPayload,
              "non-zero reserved header field rejected");
    }

    std::vector<uint8_t> encoded;
    cameracapture12::FileError fileError = cameracapture12::FileError::None;
    Check(cameracapture12::Encode(MakeCapture(), encoded, fileError),
          "test camera encodes");
    b3b2::CameraFrameMeta meta{123456, static_cast<uint32_t>(encoded.size()), 0};
    const std::vector<uint8_t> framePayload =
        b3b2::SerializeCameraFrame(meta, encoded);
    b3b2::CameraFrameMeta parsedMeta{};
    std::span<const uint8_t> parsedCodec;
    Check(b3b2::ParseCameraFrame(framePayload, parsedMeta, parsedCodec),
          "camera frame payload parses");
    Check(parsedMeta.publishQpc == meta.publishQpc &&
              parsedCodec.size() == encoded.size(),
          "camera frame metadata round-trip");
    CameraCaptureFile12 decoded{};
    Check(cameracapture12::Decode(parsedCodec, decoded, fileError),
          "camera frame codec decodes");
    Check(fileError == cameracapture12::FileError::None,
          "camera frame codec reports no error");
    Check(b3b2::ComputePacketHash(decoded) ==
              b3b2::ComputePacketHash(MakeCapture()),
          "camera frame matrix hash round-trip");
}

void TestMailbox()
{
    b3b2::StreamCounters counters;
    b3b2::LatestOnlyMailbox mailbox(counters);
    const CameraPacket first = MakePacket(2, 9, 7);
    const CameraPacket second = MakePacket(2, 9, 8);
    Check(mailbox.EnqueueCamera(first), "first camera enqueued");
    Check(mailbox.EnqueueCamera(second), "second camera enqueued");
    b3b2::WorkItem item;
    Check(mailbox.Take(item), "latest camera taken");
    Check(item.kind == b3b2::WorkItem::Kind::Camera,
          "camera work item kind");
    Check(item.camera.capture.input.captureSequence == 8,
          "latest-only overwrite returns newest camera");
    Check(counters.published.load() == 2, "published counter");
    Check(counters.enqueued.load() == 2, "enqueued counter");
    Check(counters.overwritten.load() == 1, "overwritten counter");
    Check(!mailbox.Take(item), "mailbox empty after take");

    mailbox.LockForTest();
    Check(!mailbox.EnqueueCamera(first), "lock contention drops enqueue");
    mailbox.UnlockForTest();
    Check(counters.lockDropped.load() == 1, "lock dropped counter");

    mailbox.EnqueueCamera(first);
    mailbox.Invalidate(5, 11, b3b2::InvalidateReason::Reset);
    Check(mailbox.Take(item), "invalidate work item taken");
    Check(item.kind == b3b2::WorkItem::Kind::Invalidate &&
              item.levelSession == 5 && item.captureEpoch == 11 &&
              item.reason == b3b2::InvalidateReason::Reset,
          "invalidate fields preserved");
    Check(!mailbox.Take(item), "invalidate clears pending camera");

    mailbox.RequestShutdown();
    Check(mailbox.Take(item) &&
              item.kind == b3b2::WorkItem::Kind::Shutdown,
          "shutdown work item taken");

    // Concurrent producers are not a supported Engine topology, but the
    // mailbox must remain memory-safe if a test drives it that way.
    constexpr int kThreads = 4;
    constexpr int kIterations = 2000;
    std::atomic<bool> producersDone{false};
    std::atomic<int> producerCount{0};
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t)
    {
        producers.emplace_back([&]() {
            for (int i = 0; i < kIterations; ++i)
                mailbox.EnqueueCamera(MakePacket(2, 9, static_cast<uint64_t>(i + 1)));
            if (producerCount.fetch_add(1) + 1 == kThreads)
                producersDone.store(true);
        });
    }
    b3b2::WorkItem stressItem;
    while (!producersDone.load())
    {
        mailbox.Take(stressItem);
        std::this_thread::yield();
    }
    for (std::thread& thread : producers)
        thread.join();
    while (mailbox.Take(stressItem))
    {
    }
    Check(counters.published.load() ==
              2 + 1 + 1 + static_cast<uint64_t>(kThreads * kIterations),
          "stress published count");
    Check(counters.enqueued.load() + counters.lockDropped.load() ==
              counters.published.load(),
          "stress enqueue/drop invariant");
    Check(counters.overwritten.load() <= counters.enqueued.load(),
          "stress overwrite invariant");
}

void TestReceiverValidation()
{
    ReceiverValidationState state;
    state.BeginConnection(1);
    std::string reason;
    HelloPayload hello{77, 0xABC, 10, 20};
    Check(state.ObserveHello(hello, reason), "receiver HELLO accepted");
    Check(state.ObserveCamera(MakeCapture(10, 20, 7), reason),
          "first camera accepted");
    CameraCaptureFile12 gap = MakeCapture(10, 20, 9);
    Check(state.ObserveCamera(gap, reason), "sequence gap accepted");
    Check(state.gapFrames == 1, "sequence gap counted");
    Check(!state.ObserveCamera(MakeCapture(10, 20, 9), reason),
          "duplicate sequence rejected");
    Check(state.duplicatePackets == 1, "duplicate counter");
    Check(!state.ObserveCamera(MakeCapture(10, 20, 8), reason),
          "reordered sequence rejected");
    Check(state.reorderedPackets == 1, "reordered counter");
    Check(!state.ObserveCamera(MakeCapture(10, 19, 10), reason),
          "stale epoch rejected");
    Check(state.staleEpochPackets == 1, "stale epoch counter");
    Check(!state.ObserveCamera(MakeCapture(9, 20, 10), reason),
          "stale session rejected");
    Check(state.staleSessionPackets == 1, "stale session counter");
    Check(state.ObserveCamera(MakeCapture(10, 21, 1), reason),
          "new epoch accepted");
    Check(state.ObserveInvalidate({10, 22, b3b2::InvalidateReason::Reset},
                                  reason),
          "reset invalidate accepted");
    Check(!state.ObserveCamera(MakeCapture(10, 21, 2), reason),
          "old epoch after reset rejected");
    Check(state.ObserveCamera(MakeCapture(10, 22, 1), reason),
          "new epoch after reset accepted");
    Check(state.ObserveInvalidate({10, 22, b3b2::InvalidateReason::Teardown},
                                  reason),
          "teardown invalidate accepted");
    Check(!state.ObserveCamera(MakeCapture(10, 22, 2), reason),
          "camera after teardown rejected");
    Check(state.ObserveInvalidate({11, 23, b3b2::InvalidateReason::LevelReady},
                                  reason),
          "new level invalidate accepted");
    Check(state.ObserveCamera(MakeCapture(11, 23, 1), reason),
          "new level camera accepted");
    Check(state.ObserveInvalidate({11, 24, b3b2::InvalidateReason::Shutdown},
                                  reason),
          "shutdown invalidate accepted");
    Check(!state.ObserveCamera(MakeCapture(11, 24, 2), reason),
          "camera after shutdown invalidate rejected");
    state.BeginConnection(2);
    hello.producerStartId = 0xDEF;
    hello.levelSession = 1;
    hello.captureEpoch = 1;
    Check(state.ObserveHello(hello, reason),
          "new producer HELLO accepted after shutdown");
    Check(state.ObserveCamera(MakeCapture(1, 1, 1), reason),
          "new producer resets session/epoch ordering");
}

void TestNamedPipe()
{
    const std::wstring pipeName = CameraStreamProducer::MakePipeName();
    CameraStreamProducer producer;
    std::string error;
    Check(producer.Start(pipeName, error), "producer starts local pipe");
    producer.OnLevelReady(2, 9);

    CameraStreamClient client;
    Check(client.Connect(pipeName, 5000, error), "receiver connects to pipe");

    StreamMessage message;
    bool sawHello = false;
    for (int i = 0; i < 8; ++i)
    {
        const MessageReadStatus status = client.Read(message, 3000);
        Check(status == MessageReadStatus::Ok, "pipe initial message read");
        if (status != MessageReadStatus::Ok)
            break;
        if (message.type == StreamMessageType::Hello)
        {
            HelloPayload hello{};
            Check(b3b2::ParseHello(message.payload, hello), "pipe HELLO parses");
            Check(hello.producerPid == ::GetCurrentProcessId(),
                  "pipe HELLO pid");
            Check(hello.levelSession == 2 && hello.captureEpoch == 9,
                  "pipe HELLO session/epoch");
            sawHello = true;
            break;
        }
    }
    Check(sawHello, "pipe HELLO observed");

    const CameraPacket packet = MakePacket(2, 9, 7);
    Check(producer.Publish(packet), "producer enqueues packet");
    bool sawCamera = false;
    for (int i = 0; i < 8; ++i)
    {
        const MessageReadStatus status = client.Read(message, 3000);
        Check(status == MessageReadStatus::Ok, "pipe camera message read");
        if (status != MessageReadStatus::Ok)
            break;
        if (message.type == StreamMessageType::CameraFrame)
        {
            b3b2::CameraFrameMeta meta{};
            std::span<const uint8_t> codec;
            Check(b3b2::ParseCameraFrame(message.payload, meta, codec),
                  "pipe camera payload parses");
            CameraCaptureFile12 decoded{};
            cameracapture12::FileError fileError =
                cameracapture12::FileError::None;
            Check(cameracapture12::Decode(codec, decoded, fileError),
                  "pipe camera codec decodes");
            Check(b3b2::ComputePacketHash(decoded) ==
                      b3b2::ComputePacketHash(packet.capture),
                  "pipe camera matrix hash matches");
            sawCamera = true;
            break;
        }
    }
    Check(sawCamera, "pipe camera observed");

    producer.Invalidate(2, 10, b3b2::InvalidateReason::Reset);
    bool sawInvalidate = false;
    for (int i = 0; i < 8; ++i)
    {
        const MessageReadStatus status = client.Read(message, 3000);
        Check(status == MessageReadStatus::Ok, "pipe invalidate read");
        if (status != MessageReadStatus::Ok)
            break;
        if (message.type == StreamMessageType::InvalidateSession)
        {
            InvalidatePayload invalidate{};
            Check(b3b2::ParseInvalidate(message.payload, invalidate),
                  "pipe invalidate parses");
            Check(invalidate.levelSession == 2 &&
                      invalidate.captureEpoch == 10 &&
                      invalidate.reason == b3b2::InvalidateReason::Reset,
                  "pipe invalidate fields");
            sawInvalidate = true;
            break;
        }
    }
    Check(sawInvalidate, "pipe invalidate observed");
    Check(producer.Counters().sent >= 1, "pipe sent counter");
    Check(producer.Counters().encoded >= 1, "pipe encoded counter");

    producer.Stop();
    bool sawShutdown = false;
    for (int i = 0; i < 8; ++i)
    {
        const MessageReadStatus status = client.Read(message, 2000);
        if (status != MessageReadStatus::Ok)
            break;
        if (message.type == StreamMessageType::Shutdown)
        {
            sawShutdown = true;
            break;
        }
    }
    Check(sawShutdown, "producer sends SHUTDOWN on stop");
    client.Close();
    Check(!producer.Running(), "producer stopped");
}

void TestReconnect()
{
    const std::wstring pipeName = CameraStreamProducer::MakePipeName();
    CameraStreamProducer producer;
    std::string error;
    Check(producer.Start(pipeName, error), "reconnect producer starts");
    producer.OnLevelReady(2, 9);

    CameraStreamClient first;
    Check(first.Connect(pipeName, 5000, error), "first receiver connects");
    StreamMessage message;
    bool hello = false;
    for (int i = 0; i < 8; ++i)
    {
        if (first.Read(message, 3000) != MessageReadStatus::Ok)
            break;
        if (message.type == StreamMessageType::Hello)
        {
            hello = true;
            break;
        }
    }
    Check(hello, "first receiver sees HELLO");
    first.Close();

    // Force the producer to notice the broken connection, then connect again.
    producer.Publish(MakePacket(2, 9, 8));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CameraStreamClient second;
    Check(second.Connect(pipeName, 5000, error), "second receiver reconnects");
    hello = false;
    for (int i = 0; i < 8; ++i)
    {
        if (second.Read(message, 3000) != MessageReadStatus::Ok)
            break;
        if (message.type == StreamMessageType::Hello)
        {
            hello = true;
            break;
        }
    }
    Check(hello, "second receiver sees HELLO");
    Check(producer.Counters().reconnected >= 1, "producer reconnected counter");

    producer.Publish(MakePacket(2, 9, 9));
    bool camera = false;
    for (int i = 0; i < 8; ++i)
    {
        if (second.Read(message, 3000) != MessageReadStatus::Ok)
            break;
        if (message.type == StreamMessageType::CameraFrame)
        {
            camera = true;
            break;
        }
    }
    Check(camera, "second receiver gets camera after reconnect");
    second.Close();
    producer.Stop();
}

void TestNoReceiverAndCancellation()
{
    CameraStreamProducer producer;
    std::string error;
    const std::wstring pipeName = CameraStreamProducer::MakePipeName();
    Check(producer.Start(pipeName, error), "no-receiver producer starts");
    for (int i = 0; i < 100; ++i)
        producer.Publish(MakePacket(2, 9, static_cast<uint64_t>(i + 1)));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto before = std::chrono::steady_clock::now();
    producer.Stop();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - before)
                             .count();
    Check(elapsed < 3000, "producer shutdown during no receiver is bounded");
    Check(producer.Counters().published >= 100,
          "no-receiver published counter");
    Check(producer.Counters().sent == 0, "no-receiver sent counter is zero");
    Check(producer.State() == b3b2::WorkerState::Stopped,
          "no-receiver Stop ends Stopped");
    Check(!producer.Running(), "no-receiver Running false after Stop");
}

bool WaitForState(const CameraStreamProducer& producer,
                  b3b2::WorkerState expected, DWORD timeoutMs)
{
    const ULONGLONG deadline = ::GetTickCount64() + timeoutMs;
    while (producer.State() != expected)
    {
        if (::GetTickCount64() >= deadline)
            return false;
        ::Sleep(1);
    }
    return true;
}

DWORD CurrentHandleCount()
{
    DWORD count = 0;
    ::GetProcessHandleCount(::GetCurrentProcess(), &count);
    return count;
}

bool IsHandleOpen(HANDLE handle)
{
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        return false;
    DWORD flags = 0;
    return ::GetHandleInformation(handle, &flags) != FALSE;
}

std::wstring MakeTestPipeName()
{
    static std::atomic<unsigned> sequence{0};
    wchar_t buffer[160] = {};
    std::swprintf(buffer, 160, L"\\\\.\\pipe\\ogsr-b3b2-test-%lu-%llu-%u",
                  static_cast<unsigned long>(::GetCurrentProcessId()),
                  static_cast<unsigned long long>(::GetTickCount64()),
                  sequence.fetch_add(1, std::memory_order_relaxed));
    return buffer;
}

struct TestPipeServer
{
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::wstring name;

    bool Create()
    {
        name = MakeTestPipeName();
        handle = ::CreateNamedPipeW(
            name.c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0,
            nullptr);
        return handle != INVALID_HANDLE_VALUE;
    }

    ~TestPipeServer()
    {
        if (handle != INVALID_HANDLE_VALUE)
            ::CloseHandle(handle);
    }
};

struct TestOverlappedPair
{
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;

    bool Create(DWORD bufferBytes = 4096)
    {
        const std::wstring name = MakeTestPipeName();
        server = ::CreateNamedPipeW(
            name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, bufferBytes,
            bufferBytes, 0, nullptr);
        if (server == INVALID_HANDLE_VALUE)
            return false;
        client = ::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (client == INVALID_HANDLE_VALUE)
            return false;

        HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event == nullptr)
            return false;
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        BOOL connected = ::ConnectNamedPipe(server, &overlapped);
        if (!connected && ::GetLastError() == ERROR_IO_PENDING)
        {
            ::WaitForSingleObject(event, 5000);
            DWORD bytes = 0;
            connected = ::GetOverlappedResult(server, &overlapped, &bytes, FALSE);
        }
        else if (!connected && ::GetLastError() == ERROR_PIPE_CONNECTED)
        {
            connected = TRUE;
        }
        ::CloseHandle(event);
        return connected != FALSE;
    }

    ~TestOverlappedPair()
    {
        if (client != INVALID_HANDLE_VALUE)
            ::CloseHandle(client);
        if (server != INVALID_HANDLE_VALUE)
            ::CloseHandle(server);
    }
};

bool ReadUntilType(CameraStreamClient& client, StreamMessageType type,
                   DWORD timeoutMs, StreamMessage& message)
{
    for (int i = 0; i < 8; ++i)
    {
        if (client.Read(message, timeoutMs) != MessageReadStatus::Ok)
            return false;
        if (message.type == type)
            return true;
    }
    return false;
}

// P3: the write index keeps rotating after the ring is full, while the valid
// sample count stays capped at 4096.
void TestLatencyRing()
{
    b3b2::EnqueueLatencyRing ring;
    Check(ring.WriteIndex() == 0 && ring.ValidCount() == 0,
          "latency ring starts empty");
    Check(ring.Stats().samples == 0, "latency empty stats");

    for (uint64_t i = 1; i <= 100; ++i)
        ring.Record(i);
    const b3b2::EnqueueStats partial = ring.Stats();
    Check(partial.samples == 100, "latency partial sample count");
    Check(partial.medianNs == 50, "latency partial median");
    Check(partial.p95Ns == 95, "latency partial p95");
    Check(partial.p99Ns == 99, "latency partial p99");
    Check(partial.maxNs == 100, "latency partial max");

    for (uint64_t i = 101; i <= 4096; ++i)
        ring.Record(i);
    Check(ring.ValidCount() == 4096, "latency full valid count");
    Check(ring.WriteIndex() == 0, "latency full write index wraps");
    const b3b2::EnqueueStats full = ring.Stats();
    Check(full.medianNs == 2048 && full.p95Ns == 3891 && full.maxNs == 4096,
          "latency full stats");

    for (uint64_t i = 4097; i <= 9000; ++i)
        ring.Record(i);
    const b3b2::EnqueueStats wrapped = ring.Stats();
    Check(ring.ValidCount() == 4096, "latency wrapped valid count stays 4096");
    Check(ring.WriteIndex() == 808, "latency wrapped write index rotates");
    Check(wrapped.samples == 4096, "latency wrapped samples");
    Check(wrapped.medianNs == 6952, "latency wrapped median 6952");
    Check(wrapped.p95Ns == 8795, "latency wrapped p95 8795");
    Check(wrapped.p99Ns == 8959, "latency wrapped p99 8959");
    Check(wrapped.maxNs == 9000, "latency wrapped max");

    ring.Reset();
    Check(ring.WriteIndex() == 0 && ring.ValidCount() == 0,
          "latency reset clears indices");
}

// P2 generation: Take captures the generation under the mailbox lock, and the
// send decision is atomic with invalidation.
void TestGenerationMailbox()
{
    b3b2::StreamCounters counters;
    b3b2::LatestOnlyMailbox mailbox(counters);
    mailbox.EnqueueCamera(MakePacket(2, 9, 20));

    b3b2::WorkItem item;
    Check(mailbox.Take(item), "generation camera taken");
    Check(item.generation == mailbox.Generation(),
          "generation captured during Take");
    Check(mailbox.TryBeginSend(item.generation), "generation send accepted");
    Check(!mailbox.TryBeginSend(item.generation),
          "generation second send rejected while in flight");
    mailbox.EndSend();

    mailbox.EnqueueCamera(MakePacket(2, 9, 21));
    Check(mailbox.Take(item), "generation stale camera taken");
    const uint64_t staleGeneration = item.generation;
    mailbox.Invalidate(2, 10, b3b2::InvalidateReason::Reset);
    Check(mailbox.Generation() != staleGeneration,
          "invalidation advances generation");
    Check(!mailbox.TryBeginSend(staleGeneration),
          "stale generation cannot begin send after invalidation");

    b3b2::WorkItem control;
    Check(mailbox.Take(control) &&
              control.kind == b3b2::WorkItem::Kind::Invalidate,
          "invalidation control taken after stale camera");
    Check(control.generation == mailbox.Generation(),
          "control generation captured during Take");
}

void TestCameraSerializationFailure()
{
    const std::wstring pipeName = CameraStreamProducer::MakePipeName();
    CameraStreamProducer producer;
    std::string error;
    Check(producer.Start(pipeName, error), "camera fault producer starts");
    producer.OnLevelReady(2, 9);
    CameraStreamClient client;
    Check(client.Connect(pipeName, 5000, error), "camera fault client connects");
    Check(WaitForState(producer, b3b2::WorkerState::Running, 3000),
          "camera fault worker running");

    b3b2::SetStreamFault(b3b2::StreamFaultSite::CameraSerialize);
    Check(producer.Publish(MakePacket(2, 9, 30)),
          "camera fault packet published");
    Check(WaitForState(producer, b3b2::WorkerState::Failed, 3000),
          "camera Serialize bad_alloc reaches Failed");
    b3b2::ClearStreamFault();
    Check(!producer.Running(), "camera fault Running false");
    Check(producer.LastFailure() == ERROR_UNHANDLED_EXCEPTION,
          "camera fault LastFailure recorded");
    Check(!producer.Publish(MakePacket(2, 9, 31)),
          "camera fault publish rejected after failure");
    producer.Stop();
    Check(producer.State() == b3b2::WorkerState::Failed,
          "camera fault Failed survives Stop");
    Check(!producer.Start(pipeName, error),
          "camera fault Start rejected after failure");
    client.Close();
}

void TestInvalidateSerializationFailure()
{
    const std::wstring pipeName = CameraStreamProducer::MakePipeName();
    CameraStreamProducer producer;
    std::string error;
    Check(producer.Start(pipeName, error), "invalidate fault producer starts");
    producer.OnLevelReady(2, 9);
    CameraStreamClient client;
    Check(client.Connect(pipeName, 5000, error),
          "invalidate fault client connects");
    Check(WaitForState(producer, b3b2::WorkerState::Running, 3000),
          "invalidate fault worker running");

    b3b2::SetStreamFault(b3b2::StreamFaultSite::InvalidateSerialize);
    producer.Invalidate(2, 10, b3b2::InvalidateReason::Reset);
    Check(WaitForState(producer, b3b2::WorkerState::Failed, 3000),
          "invalidate Serialize bad_alloc reaches Failed");
    b3b2::ClearStreamFault();
    Check(!producer.Running(), "invalidate fault Running false");
    producer.Stop();
    Check(producer.State() == b3b2::WorkerState::Failed,
          "invalidate fault Failed survives Stop");
    client.Close();
}

void TestHelloSerializationFailure()
{
    const std::wstring pipeName = CameraStreamProducer::MakePipeName();
    CameraStreamProducer producer;
    std::string error;
    Check(producer.Start(pipeName, error), "hello fault producer starts");
    b3b2::SetStreamFault(b3b2::StreamFaultSite::HelloSerialize);
    CameraStreamClient client;
    Check(client.Connect(pipeName, 5000, error), "hello fault client connects");
    Check(WaitForState(producer, b3b2::WorkerState::Failed, 3000),
          "hello Serialize bad_alloc reaches Failed");
    b3b2::ClearStreamFault();
    Check(!producer.Running(), "hello fault Running false");
    producer.Stop();
    Check(producer.State() == b3b2::WorkerState::Failed,
          "hello fault Failed survives Stop");
    client.Close();
}

void TestWorkerUnexpectedTermination()
{
    const std::wstring pipeName = CameraStreamProducer::MakePipeName();
    CameraStreamProducer producer;
    std::string error;
    b3b2::SetStreamFault(b3b2::StreamFaultSite::WorkerUnexpectedThrow);
    Check(producer.Start(pipeName, error), "worker fault producer starts");
    Check(WaitForState(producer, b3b2::WorkerState::Failed, 3000),
          "unexpected worker exception reaches Failed");
    b3b2::ClearStreamFault();
    Check(!producer.Running(), "worker fault Running false");
    Check(producer.LastFailure() == ERROR_UNHANDLED_EXCEPTION,
          "worker fault LastFailure recorded");
    Check(!producer.Publish(MakePacket(2, 9, 40)),
          "worker fault publish rejected");
    producer.Stop();
    Check(producer.State() == b3b2::WorkerState::Failed,
          "worker fault Failed survives Stop");
    Check(!producer.Start(pipeName, error),
          "worker fault Start rejected by one-shot lifecycle");
}

void TestPreSubmitInvalidation()
{
    const std::wstring pipeName = CameraStreamProducer::MakePipeName();
    CameraStreamProducer producer;
    std::string error;
    Check(producer.Start(pipeName, error), "pre-submit producer starts");
    producer.OnLevelReady(2, 9);
    CameraStreamClient client;
    Check(client.Connect(pipeName, 5000, error), "pre-submit client connects");
    StreamMessage message;
    Check(ReadUntilType(client, StreamMessageType::Hello, 3000, message),
          "pre-submit HELLO observed");

    b3b2::SetPreSubmitBlock(true);
    Check(producer.Publish(MakePacket(2, 9, 50, 500)),
          "pre-submit camera published");
    Check(b3b2::WaitPreSubmitEntered(3000),
          "pre-submit gate entered after encode");
    producer.Invalidate(2, 10, b3b2::InvalidateReason::Reset);
    b3b2::ReleasePreSubmitBlock();

    bool sawInvalidate = false;
    bool sawStaleCamera = false;
    for (int i = 0; i < 8; ++i)
    {
        if (client.Read(message, 2000) != MessageReadStatus::Ok)
            break;
        if (message.type == StreamMessageType::InvalidateSession)
        {
            InvalidatePayload invalidate{};
            if (b3b2::ParseInvalidate(message.payload, invalidate) &&
                invalidate.levelSession == 2 && invalidate.captureEpoch == 10)
            {
                Check(invalidate.reason == b3b2::InvalidateReason::Reset,
                      "pre-submit invalidation reason");
                sawInvalidate = true;
            }
        }
        if (message.type == StreamMessageType::CameraFrame)
            sawStaleCamera = true;
    }
    Check(sawInvalidate, "pre-submit invalidation delivered");
    Check(!sawStaleCamera,
          "camera encoded before invalidation is not submitted");

    Check(producer.Publish(MakePacket(2, 10, 1, 501)),
          "pre-submit fresh camera published");
    bool sawFreshCamera = false;
    for (int i = 0; i < 8; ++i)
    {
        if (client.Read(message, 2000) != MessageReadStatus::Ok)
            break;
        if (message.type != StreamMessageType::CameraFrame)
            continue;
        b3b2::CameraFrameMeta meta{};
        std::span<const uint8_t> codec;
        if (!b3b2::ParseCameraFrame(message.payload, meta, codec))
            continue;
        CameraCaptureFile12 decoded{};
        cameracapture12::FileError fileError = cameracapture12::FileError::None;
        if (cameracapture12::Decode(codec, decoded, fileError) &&
            decoded.input.captureSequence == 1)
        {
            sawFreshCamera = true;
            break;
        }
    }
    Check(sawFreshCamera, "fresh camera delivered after invalidation");
    producer.Stop();
    client.Close();
}

#ifndef __SANITIZE_ADDRESS__
void ArmAllocationFailure(size_t minimum, size_t maximum)
{
    g_failedAllocSize.store(0, std::memory_order_release);
    g_failAllocMin.store(minimum, std::memory_order_relaxed);
    g_failAllocMax.store(maximum, std::memory_order_relaxed);
    g_failAllocArmed.store(true, std::memory_order_release);
}

size_t DisarmAllocationFailure()
{
    g_failAllocArmed.store(false, std::memory_order_release);
    return g_failedAllocSize.load(std::memory_order_acquire);
}

// A real allocation failure (not the fault-hook seam) in the same size class
// as the OverlappedPipeStream object. No custom allocator is active here.
void TestRealAllocationFailure()
{
    TestPipeServer server;
    Check(server.Create(), "real allocation pipe server created");
    CameraStreamClient client;
    DWORD baseline = 0;
    ::GetProcessHandleCount(::GetCurrentProcess(), &baseline);

    ArmAllocationFailure(32, 64);
    std::string error;
    const bool connected = client.Connect(server.name, 1000, error);
    const size_t failedSize = DisarmAllocationFailure();
    std::printf("real_alloc_failed_size=%zu\n", failedSize);
    Check(!connected, "real stream allocation failure rejects connect");
    Check(failedSize >= 32 && failedSize <= 64,
          "real allocation failure hit the stream object size class");
    Check(!error.empty(), "real allocation failure error preserved");
    client.Close();

    DWORD after = 0;
    ::GetProcessHandleCount(::GetCurrentProcess(), &after);
    Check(after == baseline, "real allocation failure handle delta is zero");
}
#endif

void TestClientHandleOwnership()
{
    const b3b2::StreamFaultSite faults[] = {
        b3b2::StreamFaultSite::ClientStreamAllocate,
        b3b2::StreamFaultSite::ClientEventCreate,
        b3b2::StreamFaultSite::OverlappedEventCreate};

    for (const b3b2::StreamFaultSite fault : faults)
    {
        TestPipeServer server;
        Check(server.Create(), "ownership test pipe server created");
        DWORD baseline = 0;
        ::GetProcessHandleCount(::GetCurrentProcess(), &baseline);
        b3b2::SetStreamFault(fault);
        CameraStreamClient client;
        std::string error;
        const bool connected = client.Connect(server.name, 1000, error);
        b3b2::ClearStreamFault();
        Check(!connected, "client fault connect returns false");
        Check(!error.empty(), "client fault error preserved");
        client.Close();
        DWORD after = 0;
        ::GetProcessHandleCount(::GetCurrentProcess(), &after);
        Check(after == baseline, "client fault handle delta is zero");
    }

    // Real client close after a successful connect must also balance handles.
    TestPipeServer server;
    Check(server.Create(), "client close pipe server created");
    DWORD baseHandles = 0;
    ::GetProcessHandleCount(::GetCurrentProcess(), &baseHandles);
    {
        CameraStreamClient client;
        std::string error;
        Check(client.Connect(server.name, 1000, error),
              "handle test client connects");
        DWORD during = 0;
        ::GetProcessHandleCount(::GetCurrentProcess(), &during);
        Check(during > baseHandles, "connected client owns handles");
    }
    DWORD after = 0;
    ::GetProcessHandleCount(::GetCurrentProcess(), &after);
    Check(after == baseHandles, "client close handle delta is zero");
}

void TestClientReadCancellation()
{
    TestPipeServer server;
    Check(server.Create(), "read cancel pipe server created");
    DWORD before = 0;
    ::GetProcessHandleCount(::GetCurrentProcess(), &before);

    CameraStreamClient client;
    std::string error;
    Check(client.Connect(server.name, 1000, error),
          "read cancel client connects");
    std::atomic<bool> readReturned{false};
    MessageReadStatus readStatus = MessageReadStatus::Ok;
    const ULONGLONG start = ::GetTickCount64();
    std::thread reader([&]() {
        StreamMessage message;
        readStatus = client.Read(message, 60000);
        readReturned.store(true, std::memory_order_release);
    });
    ::Sleep(100);
    client.Close();
    reader.join();
    const ULONGLONG elapsed = ::GetTickCount64() - start;
    Check(readReturned.load(std::memory_order_acquire),
          "pending client read returned after Close");
    Check(readStatus == MessageReadStatus::Closed,
          "pending client read reports Closed");
    Check(elapsed < 3000, "pending client read cancellation is prompt");
    DWORD after = 0;
    ::GetProcessHandleCount(::GetCurrentProcess(), &after);
    Check(after == before, "read cancel handle delta is zero");
}

void TestClientCloseConcurrency()
{
    const DWORD baseline = CurrentHandleCount();
#ifdef __SANITIZE_ADDRESS__
    constexpr int kSequential = 500;
    constexpr int kConcurrent = 25;
#else
    constexpr int kSequential = 9000;
    constexpr int kConcurrent = 1000;
#endif
    int createFailures = 0;
    int reused = 0;
    int unrelatedSignaled = 0;

    for (int i = 0; i < kSequential + kConcurrent; ++i)
    {
        CameraStreamClient client;
        HANDLE stop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stop == nullptr)
        {
            ++createFailures;
            break;
        }
        client.AdoptStopEventForTest(stop);
        if (client.StopEventForTest() != stop)
        {
            ++createFailures;
            ::CloseHandle(stop);
            break;
        }

        if (i < kSequential)
        {
            client.Close();
            client.Close();
        }
        else
        {
            std::thread first([&]() { client.Close(); });
            std::thread second([&]() { client.Close(); });
            first.join();
            second.join();
        }

        if (client.StopEventForTest() != nullptr)
            ++createFailures;

        HANDLE unrelated = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (unrelated == nullptr)
        {
            ++createFailures;
            break;
        }
        if (unrelated == stop)
            ++reused;
        if (::WaitForSingleObject(unrelated, 0) == WAIT_OBJECT_0)
            ++unrelatedSignaled;
        ::CloseHandle(unrelated);
    }

    Check(createFailures == 0, "close/close lifecycle operations succeeded");
    Check(unrelatedSignaled == 0,
          "close/close never signals an unrelated reused event");
    Check(CurrentHandleCount() == baseline,
          "close/close handle count returns to baseline");
    std::printf("close_close_iterations=%d concurrent_pairs=%d reused=%d "
                "unrelated_signaled=%d\n",
                kSequential + kConcurrent, kConcurrent, reused,
                unrelatedSignaled);
}

void TestClientClosePause()
{
    const DWORD baseline = CurrentHandleCount();
    CameraStreamClient client;
    HANDLE stop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(stop != nullptr, "close pause event created");
    client.AdoptStopEventForTest(stop);

    b3b2::SetClientClosePause(true);
    b3b2::SetStreamFault(b3b2::StreamFaultSite::ClientClosePauseBeforeSignal);
    std::atomic<bool> secondDone{false};
    std::thread first([&]() { client.Close(); });
    const bool paused = b3b2::WaitClientClosePauseEntered(3000);
    Check(paused, "first Close paused inside lifecycle");
    std::thread second([&]() {
        client.Close();
        secondDone.store(true, std::memory_order_release);
    });
    ::Sleep(200);
    Check(!secondDone.load(std::memory_order_acquire),
          "second Close waits for lifecycle while first is paused");

    HANDLE unrelated = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(unrelated != nullptr, "paused close unrelated event created");
    Check(::WaitForSingleObject(unrelated, 0) != WAIT_OBJECT_0,
          "paused Close does not signal an unrelated event");

    b3b2::ReleaseClientClosePause();
    first.join();
    second.join();
    b3b2::ClearStreamFault();

    Check(secondDone.load(std::memory_order_acquire),
          "second Close completes after pause release");
    Check(::WaitForSingleObject(unrelated, 0) != WAIT_OBJECT_0,
          "unrelated event remains unsignaled after both Closes");
    Check(client.StopEventForTest() == nullptr,
          "client has no stop event after concurrent Close");
    ::CloseHandle(unrelated);
    Check(CurrentHandleCount() == baseline,
          "close pause handle count returns to baseline");
}

void TestClientReadCloseClose()
{
    TestPipeServer server;
    Check(server.Create(), "read+close+close server created");
    const DWORD baseline = CurrentHandleCount();

    CameraStreamClient client;
    std::string error;
    Check(client.Connect(server.name, 1000, error),
          "read+close+close client connects");
    std::atomic<bool> readReturned{false};
    MessageReadStatus status = MessageReadStatus::Ok;
    const ULONGLONG start = ::GetTickCount64();
    std::thread reader([&]() {
        StreamMessage message;
        status = client.Read(message, 60000);
        readReturned.store(true, std::memory_order_release);
    });
    ::Sleep(100);
    std::thread first([&]() { client.Close(); });
    std::thread second([&]() { client.Close(); });
    reader.join();
    first.join();
    second.join();

    Check(readReturned.load(std::memory_order_acquire),
          "read+close+close read returned");
    Check(status == MessageReadStatus::Closed,
          "read+close+close read reports Closed");
    Check(::GetTickCount64() - start < 3000,
          "read+close+close completes promptly");
    Check(client.StopEventForTest() == nullptr,
          "read+close+close client is closed");
    Check(CurrentHandleCount() == baseline,
          "read+close+close handle count returns to baseline");
}

std::wstring MakeMissingPipeName()
{
    wchar_t buffer[160] = {};
    std::swprintf(buffer, 160, L"\\\\.\\pipe\\ogsr-handlefix-missing-%lu-%llu",
                  static_cast<unsigned long>(::GetCurrentProcessId()),
                  static_cast<unsigned long long>(::GetTickCount64()));
    return buffer;
}

void TestConnectCloseLifecycle()
{
    const DWORD baseline = CurrentHandleCount();

    // Connect failure followed by Close.
    {
        CameraStreamClient client;
        std::string error;
        Check(!client.Connect(MakeMissingPipeName(), 100, error),
              "connect failure returns false");
        client.Close();
        Check(client.StopEventForTest() == nullptr,
              "close after connect failure leaves no event");
    }
    Check(CurrentHandleCount() == baseline,
          "connect failure + close handle delta is zero");

    // Deterministic Connect+Close: Close wins after BeginLifecycle and before
    // publication. Connect must discard its local stream/event and fail.
    {
        TestPipeServer server;
        Check(server.Create(), "connect/close pause server created");
        CameraStreamClient client;
        std::atomic<bool> done{false};
        bool result = true;
        std::string error;
        b3b2::SetClientConnectPause(true);
        b3b2::SetStreamFault(b3b2::StreamFaultSite::ClientConnectPauseAfterBegin);
        std::thread connect([&]() {
            result = client.Connect(server.name, 1000, error);
            done.store(true, std::memory_order_release);
        });
        Check(b3b2::WaitClientConnectPauseEntered(3000),
              "Connect paused after BeginLifecycle");
        client.Close();
        b3b2::ReleaseClientConnectPause();
        connect.join();
        b3b2::ClearStreamFault();
        Check(done.load(std::memory_order_acquire),
              "paused Connect finished after Close");
        Check(!result, "Close after Connect begin aborts publication");
        Check(client.StopEventForTest() == nullptr,
              "aborted Connect leaves no stop event");
    }
    Check(CurrentHandleCount() == baseline,
          "connect/close pause handle delta is zero");

    // Concurrent Connect to a missing pipe and Close. The generation check
    // must abort the retry loop instead of publishing after Close.
    {
        CameraStreamClient client;
        std::atomic<bool> done{false};
        bool result = true;
        std::string error;
        const ULONGLONG start = ::GetTickCount64();
        std::thread connect([&]() {
            result = client.Connect(MakeMissingPipeName(), 1000, error);
            done.store(true, std::memory_order_release);
        });
        ::Sleep(100);
        client.Close();
        connect.join();
        Check(done.load(std::memory_order_acquire),
              "concurrent connect finished after Close");
        Check(!result, "concurrent connect fails after Close");
        Check(::GetTickCount64() - start < 3000,
              "concurrent connect aborts promptly");
        Check(client.StopEventForTest() == nullptr,
              "concurrent connect leaves no event after Close");
        client.Close();
    }
    Check(CurrentHandleCount() == baseline,
          "concurrent connect + close handle delta is zero");

    // Successful Connect followed by Close, then a fresh receiver.
    {
        TestPipeServer server;
        Check(server.Create(), "connect+close server created");
        CameraStreamClient client;
        std::string error;
        Check(client.Connect(server.name, 1000, error),
              "connect+close client connects");
        client.Close();
        Check(!client.IsOpen(), "connect+close client is closed");
    }
    Check(CurrentHandleCount() == baseline,
          "connect+close handle delta is zero");
}

void TestFirstPipeOwnership()
{
    const DWORD baseline = CurrentHandleCount();

    // A: throw before the worker loop.
    {
        CameraStreamProducer producer;
        std::string error;
        const DWORD blockHandles = CurrentHandleCount();
        b3b2::SetStreamFault(b3b2::StreamFaultSite::WorkerUnexpectedThrow);
        Check(producer.Start(CameraStreamProducer::MakePipeName(), error),
              "firstPipe throw producer starts");
        Check(WaitForState(producer, b3b2::WorkerState::Failed, 3000),
              "firstPipe throw reaches Failed");
        const HANDLE first = producer.FirstPipeForTest();
        Check(first != INVALID_HANDLE_VALUE,
              "firstPipe throw probe captured");
        Check(!IsHandleOpen(first),
              "firstPipe closed on throw before loop");
        producer.Stop();
        std::printf("firstpipe_throw_open=%d handle_delta=%d\n",
                    IsHandleOpen(first) ? 1 : 0,
                    static_cast<int>(CurrentHandleCount()) -
                        static_cast<int>(blockHandles));
        Check(CurrentHandleCount() == blockHandles,
              "firstPipe throw handle delta is zero");
        b3b2::ClearStreamFault();
    }

    // B/J: Stop is set before the first loop iteration.
    {
        CameraStreamProducer producer;
        std::string error;
        const DWORD blockHandles = CurrentHandleCount();
        b3b2::SetWorkerPauseBeforeLoop(true);
        b3b2::SetStreamFault(b3b2::StreamFaultSite::WorkerPauseBeforeLoop);
        Check(producer.Start(CameraStreamProducer::MakePipeName(), error),
              "firstPipe stop producer starts");
        Check(b3b2::WaitWorkerPauseBeforeLoopEntered(3000),
              "worker paused before first loop");
        const HANDLE first = producer.FirstPipeForTest();
        std::atomic<bool> stopDone{false};
        std::thread stopper([&]() {
            producer.Stop();
            stopDone.store(true, std::memory_order_release);
        });
        ::Sleep(150);
        b3b2::ReleaseWorkerPauseBeforeLoop();
        stopper.join();
        b3b2::ClearStreamFault();
        Check(stopDone.load(std::memory_order_acquire),
              "Stop completed after early-loop pause");
        Check(first != INVALID_HANDLE_VALUE,
              "firstPipe stop probe captured");
        Check(!IsHandleOpen(first),
              "firstPipe closed when stop wins before loop");
        Check(producer.State() == b3b2::WorkerState::Stopped,
              "firstPipe stop ends Stopped");
        std::printf("firstpipe_stop_open=%d handle_delta=%d\n",
                    IsHandleOpen(first) ? 1 : 0,
                    static_cast<int>(CurrentHandleCount()) -
                        static_cast<int>(blockHandles));
        Check(CurrentHandleCount() == blockHandles,
              "firstPipe stop handle delta is zero");
    }

    // C: stream allocation failure.
    {
        CameraStreamProducer producer;
        std::string error;
        const DWORD blockHandles = CurrentHandleCount();
        b3b2::SetStreamFault(b3b2::StreamFaultSite::ProducerStreamAllocate);
        Check(producer.Start(CameraStreamProducer::MakePipeName(), error),
              "firstPipe allocation fault producer starts");
        ::Sleep(100);
        const HANDLE first = producer.FirstPipeForTest();
        producer.Stop();
        Check(!IsHandleOpen(first),
              "firstPipe closed on stream allocation failure");
        Check(CurrentHandleCount() == blockHandles,
              "firstPipe allocation handle delta is zero");
        b3b2::ClearStreamFault();
    }

    // D: overlapped event creation failure.
    {
        CameraStreamProducer producer;
        std::string error;
        const DWORD blockHandles = CurrentHandleCount();
        b3b2::SetStreamFault(b3b2::StreamFaultSite::OverlappedEventCreate);
        Check(producer.Start(CameraStreamProducer::MakePipeName(), error),
              "firstPipe event fault producer starts");
        ::Sleep(100);
        const HANDLE first = producer.FirstPipeForTest();
        producer.Stop();
        Check(!IsHandleOpen(first),
              "firstPipe closed on ioEvent failure");
        Check(CurrentHandleCount() == blockHandles,
              "firstPipe event handle delta is zero");
        b3b2::ClearStreamFault();
    }

    // E: pending connect cancellation.
    {
        CameraStreamProducer producer;
        std::string error;
        const DWORD blockHandles = CurrentHandleCount();
        b3b2::SetStreamFault(b3b2::StreamFaultSite::ConnectWaitFailed);
        Check(producer.Start(CameraStreamProducer::MakePipeName(), error),
              "firstPipe connect fault producer starts");
        ::Sleep(100);
        const HANDLE first = producer.FirstPipeForTest();
        producer.Stop();
        Check(!IsHandleOpen(first),
              "firstPipe closed on connect failure/cancel");
        Check(CurrentHandleCount() == blockHandles,
              "firstPipe connect handle delta is zero");
        b3b2::ClearStreamFault();
    }

    // I: exception after stream ownership transfer.
    {
        CameraStreamProducer producer;
        std::string error;
        const DWORD blockHandles = CurrentHandleCount();
        b3b2::SetStreamFault(b3b2::StreamFaultSite::WorkerThrowAfterOwnership);
        Check(producer.Start(CameraStreamProducer::MakePipeName(), error),
              "firstPipe post-owner throw producer starts");
        Check(WaitForState(producer, b3b2::WorkerState::Failed, 3000),
              "firstPipe post-owner throw reaches Failed");
        const HANDLE first = producer.FirstPipeForTest();
        Check(!IsHandleOpen(first),
              "firstPipe closed by stream after ownership throw");
        producer.Stop();
        Check(CurrentHandleCount() == blockHandles,
              "firstPipe post-owner handle delta is zero");
        b3b2::ClearStreamFault();
    }

    // F: normal connection and Stop after ownership transfer.
    {
        const std::wstring pipeName = CameraStreamProducer::MakePipeName();
        CameraStreamProducer producer;
        std::string error;
        const DWORD blockHandles = CurrentHandleCount();
        Check(producer.Start(pipeName, error), "firstPipe normal producer starts");
        producer.OnLevelReady(2, 9);
        CameraStreamClient client;
        Check(client.Connect(pipeName, 5000, error),
              "firstPipe normal client connects");
        StreamMessage message;
        Check(ReadUntilType(client, StreamMessageType::Hello, 3000, message),
              "firstPipe normal HELLO observed");
        producer.Stop();
        Check(!IsHandleOpen(producer.FirstPipeForTest()),
              "firstPipe normal closed after Stop");
        client.Close();
        Check(producer.State() == b3b2::WorkerState::Stopped,
              "firstPipe normal stop ends Stopped");
        Check(CurrentHandleCount() == blockHandles,
              "firstPipe normal handle delta is zero");
    }
    Check(CurrentHandleCount() == baseline,
          "firstPipe overall handle delta is zero");
}

void TestConnectCancellationAndWaitFailed()
{
    // D: Stop while ConnectNamedPipe is pending.
    {
        const std::wstring pipeName = CameraStreamProducer::MakePipeName();
        CameraStreamProducer producer;
        std::string error;
        Check(producer.Start(pipeName, error), "connect cancel producer starts");
        Check(WaitForState(producer, b3b2::WorkerState::Running, 3000),
              "connect cancel worker running");
        ::Sleep(100);
        const ULONGLONG start = ::GetTickCount64();
        producer.Stop();
        const ULONGLONG elapsed = ::GetTickCount64() - start;
        Check(elapsed < 3000, "pending connect cancellation is prompt");
        Check(producer.State() == b3b2::WorkerState::Stopped,
              "connect cancel ends Stopped");
    }

    // E: WAIT_FAILED injected on the pending connect. The worker must not
    // terminate and must accept a later client on a fresh pipe instance.
    {
        const std::wstring pipeName = CameraStreamProducer::MakePipeName();
        CameraStreamProducer producer;
        std::string error;
        b3b2::SetStreamFault(b3b2::StreamFaultSite::ConnectWaitFailed);
        Check(producer.Start(pipeName, error),
              "connect WAIT_FAILED producer starts");
        ::Sleep(150);
        Check(producer.State() != b3b2::WorkerState::Failed,
              "connect WAIT_FAILED does not fail worker");
        CameraStreamClient client;
        bool hello = false;
        for (int attempt = 0; attempt < 3 && !hello; ++attempt)
        {
            if (!client.Connect(pipeName, 2000, error))
                continue;
            StreamMessage message;
            hello = ReadUntilType(client, StreamMessageType::Hello, 2000,
                                  message);
            if (!hello)
                client.Close();
        }
        Check(hello, "connect WAIT_FAILED recovers and delivers HELLO");
        producer.Stop();
        client.Close();
    }
}

void TestOverlappedProbes()
{
    struct ProbeRun
    {
        b3b2::OverlappedProbeResult result;
        ULONGLONG elapsedMs = 0;
    };

    // F: pending Read cancelled by the stop event.
    {
        TestOverlappedPair pair;
        Check(pair.Create(), "read probe pipe created");
        HANDLE stop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        char buffer[32] = {};
        ProbeRun run;
        std::thread worker([&]() {
            const ULONGLONG start = ::GetTickCount64();
            run.result = b3b2::RunOverlappedIoProbe(
                pair.server, stop, buffer, sizeof(buffer), true, 60000);
            run.elapsedMs = ::GetTickCount64() - start;
        });
        ::Sleep(100);
        ::SetEvent(stop);
        worker.join();
        Check(run.result.returned, "read probe returned");
        Check(run.result.state == b3b2::IoOperationState::Cancelled,
              "read probe terminal Cancelled");
        Check(run.result.overlappedCompleteAfterReturn,
              "read probe OVERLAPPED terminal on return");
        Check(run.result.eventSignaledAfterReturn,
              "read probe event terminal on return");
        Check(run.elapsedMs < 3000, "read probe cancellation bounded");
        ::CloseHandle(stop);
    }

    // G: pending Write cancelled by the stop event.
    {
        TestOverlappedPair pair;
        Check(pair.Create(), "write probe pipe created");
        HANDLE stop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::vector<char> buffer(1024 * 1024, 'w');
        ProbeRun run;
        std::thread worker([&]() {
            const ULONGLONG start = ::GetTickCount64();
            run.result = b3b2::RunOverlappedIoProbe(
                pair.server, stop, buffer.data(),
                static_cast<DWORD>(buffer.size()), false, 60000);
            run.elapsedMs = ::GetTickCount64() - start;
        });
        ::Sleep(100);
        ::SetEvent(stop);
        worker.join();
        Check(run.result.returned, "write probe returned");
        Check(run.result.state == b3b2::IoOperationState::Cancelled,
              "write probe terminal Cancelled");
        Check(run.result.overlappedCompleteAfterReturn,
              "write probe OVERLAPPED terminal on return");
        Check(run.result.eventSignaledAfterReturn,
              "write probe event terminal on return");
        Check(run.elapsedMs < 3000, "write probe cancellation bounded");
        ::CloseHandle(stop);
    }

    // H: normal completion racing with cancellation.
    {
        TestOverlappedPair pair;
        Check(pair.Create(), "race probe pipe created");
        HANDLE stop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        char buffer[32] = {};
        ProbeRun run;
        std::thread worker([&]() {
            const ULONGLONG start = ::GetTickCount64();
            run.result = b3b2::RunOverlappedIoProbe(
                pair.server, stop, buffer, sizeof(buffer), true, 60000);
            run.elapsedMs = ::GetTickCount64() - start;
        });
        ::Sleep(100);
        ::SetEvent(stop);
        const char byte = 'x';
        DWORD written = 0;
        ::WriteFile(pair.client, &byte, 1, &written, nullptr);
        worker.join();
        Check(run.result.returned, "race probe returned");
        Check(run.result.state == b3b2::IoOperationState::Completed ||
                  run.result.state == b3b2::IoOperationState::Cancelled,
              "race probe terminal Completed or Cancelled");
        Check(run.result.overlappedCompleteAfterReturn,
              "race probe OVERLAPPED terminal on return");
        Check(run.result.eventSignaledAfterReturn,
              "race probe event terminal on return");
        Check(run.elapsedMs < 3000, "race probe bounded");
        ::CloseHandle(stop);
    }

    // I/J: CancelIoEx reports ERROR_NOT_FOUND (no cancellation performed).
    // The helper must keep waiting for real completion and must not free the
    // OVERLAPPED before the delayed data arrives.
    for (int variant = 0; variant < 2; ++variant)
    {
        TestOverlappedPair pair;
        Check(pair.Create(), "not-found probe pipe created");
        HANDLE stop = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        char buffer[32] = {};
        ProbeRun run;
        b3b2::SetStreamFault(b3b2::StreamFaultSite::CancelIoNotFound);
        std::thread worker([&]() {
            const ULONGLONG start = ::GetTickCount64();
            run.result = b3b2::RunOverlappedIoProbe(
                pair.server, stop, buffer, sizeof(buffer), true, 60000);
            run.elapsedMs = ::GetTickCount64() - start;
        });
        ::Sleep(100);
        ::SetEvent(stop);
        ::Sleep(variant == 0 ? 100 : 350);
        const char byte = 'y';
        DWORD written = 0;
        ::WriteFile(pair.client, &byte, 1, &written, nullptr);
        worker.join();
        b3b2::ClearStreamFault();
        Check(run.result.returned, "not-found probe returned");
        Check(run.result.state == b3b2::IoOperationState::Completed,
              "not-found probe waits for real completion");
        Check(run.result.bytes == 1, "not-found probe delivered data");
        Check(run.result.overlappedCompleteAfterReturn,
              "not-found probe OVERLAPPED terminal on return");
        Check(run.result.eventSignaledAfterReturn,
              "not-found probe event terminal on return");
        Check(run.elapsedMs >= (variant == 0 ? 150u : 400u),
              "not-found probe did not return on cancellation request");
        ::CloseHandle(stop);
    }
}

void TestStopDuringReconnect()
{
    const std::wstring pipeName = CameraStreamProducer::MakePipeName();
    CameraStreamProducer producer;
    std::string error;
    Check(producer.Start(pipeName, error), "reconnect-stop producer starts");
    producer.OnLevelReady(2, 9);
    CameraStreamClient client;
    Check(client.Connect(pipeName, 5000, error),
          "reconnect-stop client connects");
    StreamMessage message;
    Check(ReadUntilType(client, StreamMessageType::Hello, 3000, message),
          "reconnect-stop HELLO observed");
    client.Close();
    producer.Publish(MakePacket(2, 9, 60));
    ::Sleep(100);
    const ULONGLONG start = ::GetTickCount64();
    producer.Stop();
    const ULONGLONG elapsed = ::GetTickCount64() - start;
    Check(elapsed < 5000, "Stop during reconnect is prompt");
    Check(producer.State() == b3b2::WorkerState::Stopped,
          "Stop during reconnect ends Stopped");
    Check(!producer.Running(), "Stop during reconnect Running false");
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("B3B2A_CAMERA_STREAM_CPU_TESTS\n");
    TestProtocol();
    std::printf("protocol_done checks=%d failures=%d\n", g_checks, g_failures);
    TestMailbox();
    std::printf("mailbox_done checks=%d failures=%d\n", g_checks, g_failures);
    TestReceiverValidation();
    std::printf("validation_done checks=%d failures=%d\n", g_checks,
                g_failures);
    TestNamedPipe();
    std::printf("named_pipe_done checks=%d failures=%d\n", g_checks,
                g_failures);
    TestReconnect();
    std::printf("reconnect_done checks=%d failures=%d\n", g_checks,
                g_failures);
    TestNoReceiverAndCancellation();
    std::printf("no_receiver_done checks=%d failures=%d\n", g_checks,
                g_failures);
    TestLatencyRing();
    std::printf("latency_done checks=%d failures=%d\n", g_checks, g_failures);
    TestGenerationMailbox();
    std::printf("generation_done checks=%d failures=%d\n", g_checks,
                g_failures);
    TestCameraSerializationFailure();
    TestInvalidateSerializationFailure();
    TestHelloSerializationFailure();
    TestWorkerUnexpectedTermination();
    std::printf("worker_faults_done checks=%d failures=%d\n", g_checks,
                g_failures);
    TestPreSubmitInvalidation();
    std::printf("presubmit_done checks=%d failures=%d\n", g_checks,
                g_failures);
#ifndef __SANITIZE_ADDRESS__
    TestRealAllocationFailure();
#endif
    TestClientHandleOwnership();
    TestClientReadCancellation();
    TestClientReadCloseClose();
    TestClientClosePause();
    TestClientCloseConcurrency();
    std::printf("client_close_done checks=%d failures=%d\n", g_checks,
                g_failures);
    TestConnectCloseLifecycle();
    std::printf("connect_close_done checks=%d failures=%d\n", g_checks,
                g_failures);
    TestConnectCancellationAndWaitFailed();
    std::printf("connect_faults_done checks=%d failures=%d\n", g_checks,
                g_failures);
    TestFirstPipeOwnership();
    std::printf("first_pipe_done checks=%d failures=%d\n", g_checks,
                g_failures);
    TestOverlappedProbes();
    std::printf("overlapped_probes_done checks=%d failures=%d\n", g_checks,
                g_failures);
    TestStopDuringReconnect();
    std::printf("B3B2A_STREAM_SUMMARY checks=%d failures=%d\n", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
