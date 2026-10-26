#pragma once

// F4.0-B3-B1: CPU-only wire format for a real game camera capture.
//
// The format is intentionally independent of D3D12, DXGI, the Engine and the
// file system: it turns a validated B2.3 publication into bytes and back.
// Both the game-side exporter and the standalone DX12 replay test use this
// single codec, so the replay consumes exactly what the game published.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "dx12gamecamera.h"

namespace cameracapture12
{
constexpr uint16_t kFormatVersion = 1;
constexpr uint8_t kMagic[8] = {'O', 'G', 'S', 'R', 'C', 'A', 'M', '1'};
constexpr uint32_t kHeaderSize = 20;
constexpr uint32_t kMaxPayloadBytes = 4096;
constexpr uint32_t kMaxLevelNameBytes = 127;
constexpr uint32_t kMaxViewportDimension = 16384;

enum class FileError : uint8_t
{
    None = 0,
    EmptyInput,
    BadMagic,
    UnsupportedVersion,
    TruncatedHeader,
    PayloadTooLarge,
    LengthMismatch,
    CrcMismatch,
    InvalidLevelName,
    InvalidToken,
    InvalidViewport,
    NonFiniteValue,
    InvalidCamera,
    InvalidFullTransform,
    InvalidPosition,
    InvalidReservedField,
    EncodeRejected
};

const char* FileErrorMessage(FileError error) noexcept;

// Wire payload: the validated capture fields plus the level name. Matrices are
// kept as exact float32 bit patterns in X-Ray logical row-vector layout; the
// full transform is serialized, never recomputed by the consumer.
struct CameraCaptureFile12
{
    char levelName[128]{};
    gamecamera12::CameraCaptureInput12 input{};
};

// Encodes a validated capture. Rejects invalid cameras instead of writing a
// file that cannot be replayed later.
bool Encode(const CameraCaptureFile12& capture, std::vector<uint8_t>& bytes,
            FileError& error) noexcept;

// Decodes and fully validates a capture file: magic, version, length, CRC32,
// level name, token, viewport, finite values and the B2 camera invariants.
bool Decode(std::span<const uint8_t> bytes, CameraCaptureFile12& capture,
            FileError& error) noexcept;

// CRC-32 (IEEE 802.3) over the payload.
uint32_t Crc32(std::span<const uint8_t> bytes) noexcept;

// --- Engine-side export gate (pure, engine-free) -------------------------
//
// The game adapter fills this state from B2.3 publication facts; the decision
// itself is CPU-tested so pending, stale, teardown and shutdown captures can
// never be exported.
struct ExportGateState
{
    bool exportRequested = false;
    bool alreadyExported = false;
    bool ownerThread = false;
    bool shuttingDown = false;
    bool levelActive = false;
    bool teardownPending = false;
    bool hasPublished = false;
    bool publishedTokenMatches = false;
    bool levelNameMatches = false;
    bool cameraValid = false;
};

enum class ExportDecision : uint8_t
{
    Write = 0,
    NotRequested,
    AlreadyExported,
    WrongThread,
    Shutdown,
    NoLevel,
    Teardown,
    NoPublished,
    StaleToken,
    LevelMismatch,
    InvalidCamera
};

ExportDecision EvaluateExportGate(const ExportGateState& state) noexcept;
const char* ExportDecisionName(ExportDecision decision) noexcept;

// --- Transactional file output -------------------------------------------
//
// Writes to a temporary file in the destination directory, flushes it and then
// renames it into place. An existing destination is never overwritten.
enum class WriteError : uint8_t
{
    None = 0,
    EmptyPath,
    AllocationFailed,
    DestinationExists,
    DirectoryMissing,
    CreateFailed,
    WriteFailed,
    FlushFailed,
    CloseFailed,
    PublishFailed,
    CleanupFailed,
    // A previous unrecoverable close filled the fixed HANDLE retention store;
    // further exports are rejected before creating another file.
    RetentionFull
};

#ifdef OGSR_CAMERACAPTURE_TEST_HOOKS
// Narrow fault injection for CPU tests. It exists only in the codec
// translation unit compiled with OGSR_CAMERACAPTURE_TEST_HOOKS; production
// builds contain neither the state nor the branches.
struct WriteFaultInjection
{
    int allocationCountdown = -1; // -1 disabled, 0 fails the next allocation
    bool failCreate = false;
    bool failWrite = false;
    bool failFlush = false;
    bool failMove = false;
    bool failDelete = false;
    bool failClose = false;
    // Creates the real output file with HANDLE_FLAG_PROTECT_FROM_CLOSE.
    // CloseHandle then fails for real and the codec must keep ownership.
    bool protectCreatedHandle = false;
    // Makes the controlled unprotect step fail, forcing the retention path.
    bool failUnprotect = false;
};

void SetWriteFaultInjection(const WriteFaultInjection& faults) noexcept;
void ClearWriteFaultInjection() noexcept;
HANDLE LastProtectedHandle() noexcept;
HANDLE LastRetainedHandle() noexcept;
HANDLE LastUnretainedHandle() noexcept;
size_t RetainedHandleCount() noexcept;
HANDLE RetainedHandleAt(size_t index) noexcept;
bool RetentionFull() noexcept;
// Test-only cleanup: the test closes every handle first, then clears the
// process-lifetime store and the fail-closed latch.
void ResetRetentionForTests() noexcept;
#endif

// Optional diagnostics that distinguish the primary failure from cleanup.
// `error` always keeps the primary cause; cleanup problems never hide it.
struct WriteDiagnostics
{
    WriteError primary = WriteError::None;
    WriteError cleanup = WriteError::None;
    bool handleReleased = true;
    // PROTECT_FROM_CLOSE was cleared and the retry close succeeded.
    bool handleRecovered = false;
    // The handle could not be closed and is kept in the process-lifetime
    // retention store; the OS releases it at process exit.
    bool handleRetained = false;
    // The retention store was full. The handle remains open, the OS releases
    // it at process exit, and the exporter is latched fail-closed.
    bool retentionOverflow = false;
    bool temporaryRemoved = true;
};

const char* WriteErrorMessage(WriteError error) noexcept;

bool WriteFileAtomic(const wchar_t* path, std::span<const uint8_t> bytes,
                     WriteError& error,
                     WriteDiagnostics* diagnostics = nullptr) noexcept;

} // namespace cameracapture12
