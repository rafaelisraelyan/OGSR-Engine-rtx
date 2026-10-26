#include "dx12cameracapturefile.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <new>
#include <string>

namespace cameracapture12
{
namespace
{
constexpr uint32_t kCrcPolynomial = 0xEDB88320u;

void AppendU16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

void AppendU32(std::vector<uint8_t>& out, uint32_t value)
{
    for (uint32_t shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFFu));
}

void AppendU64(std::vector<uint8_t>& out, uint64_t value)
{
    for (uint32_t shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFFu));
}

uint32_t AppendFloat(std::vector<uint8_t>& out, float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU32(out, bits);
    return bits;
}

struct Reader
{
    std::span<const uint8_t> bytes;
    size_t position = 0;
    bool ok = true;

    bool Need(size_t count) noexcept
    {
        if (!ok || count > bytes.size() - position)
        {
            ok = false;
            return false;
        }
        return true;
    }

    uint8_t U8() noexcept
    {
        if (!Need(1))
            return 0;
        return bytes[position++];
    }

    uint16_t U16() noexcept
    {
        if (!Need(2))
            return 0;
        const uint16_t value = static_cast<uint16_t>(bytes[position]) |
                               static_cast<uint16_t>(bytes[position + 1] << 8);
        position += 2;
        return value;
    }

    uint32_t U32() noexcept
    {
        if (!Need(4))
            return 0;
        uint32_t value = 0;
        for (uint32_t index = 0; index < 4; ++index)
            value |= static_cast<uint32_t>(bytes[position + index]) << (8 * index);
        position += 4;
        return value;
    }

    uint64_t U64() noexcept
    {
        if (!Need(8))
            return 0;
        uint64_t value = 0;
        for (uint32_t index = 0; index < 8; ++index)
            value |= static_cast<uint64_t>(bytes[position + index]) << (8 * index);
        position += 8;
        return value;
    }

    float F32() noexcept
    {
        const uint32_t bits = U32();
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
};

template <typename T>
bool FiniteValue(T value) noexcept
{
    return std::isfinite(static_cast<double>(value));
}

bool ValidateLevelName(const char* name, FileError& error) noexcept
{
    if (name == nullptr)
    {
        error = FileError::InvalidLevelName;
        return false;
    }
    size_t length = 0;
    while (length <= kMaxLevelNameBytes && name[length] != '\0')
    {
        const unsigned char value = static_cast<unsigned char>(name[length]);
        if (value < 0x20 || value == 0x7F)
        {
            error = FileError::InvalidLevelName;
            return false;
        }
        ++length;
    }
    if (length == 0 || length > kMaxLevelNameBytes)
    {
        error = FileError::InvalidLevelName;
        return false;
    }
    return true;
}

bool ValidatePayload(const gamecamera12::CameraCaptureInput12& input,
                     FileError& error) noexcept
{
    if (input.levelSession == 0 || input.captureSequence == 0 ||
        input.captureEpoch == 0)
    {
        error = FileError::InvalidToken;
        return false;
    }
    if (input.viewportWidth == 0 || input.viewportHeight == 0 ||
        input.viewportWidth > kMaxViewportDimension ||
        input.viewportHeight > kMaxViewportDimension)
    {
        error = FileError::InvalidViewport;
        return false;
    }

    const DirectX::XMFLOAT4X4* matrices[] = {&input.view, &input.projection,
                                             &input.fullTransform};
    for (const DirectX::XMFLOAT4X4* matrix : matrices)
    {
        for (uint32_t row = 0; row < 4; ++row)
            for (uint32_t column = 0; column < 4; ++column)
                if (!FiniteValue(matrix->m[row][column]))
                {
                    error = FileError::NonFiniteValue;
                    return false;
                }
    }
    if (!FiniteValue(input.position.x) || !FiniteValue(input.position.y) ||
        !FiniteValue(input.position.z))
    {
        error = FileError::NonFiniteValue;
        return false;
    }

    if (!input.hasFullTransform)
    {
        error = FileError::InvalidFullTransform;
        return false;
    }

    const gamecamera12::CameraCaptureResult12 result =
        gamecamera12::ValidateCamera12(input);
    if (result.error != gamecamera12::CameraError12::None || !result.camera)
    {
        switch (result.error)
        {
        case gamecamera12::CameraError12::InconsistentFullTransform:
            error = FileError::InvalidFullTransform;
            break;
        case gamecamera12::CameraError12::InvalidPosition:
            error = FileError::InvalidPosition;
            break;
        default:
            error = FileError::InvalidCamera;
            break;
        }
        return false;
    }
    return true;
}
} // namespace

const char* FileErrorMessage(FileError error) noexcept
{
    switch (error)
    {
    case FileError::None: return "none";
    case FileError::EmptyInput: return "empty input";
    case FileError::BadMagic: return "bad magic";
    case FileError::UnsupportedVersion: return "unsupported version";
    case FileError::TruncatedHeader: return "truncated header";
    case FileError::PayloadTooLarge: return "payload too large";
    case FileError::LengthMismatch: return "payload length mismatch";
    case FileError::CrcMismatch: return "payload CRC mismatch";
    case FileError::InvalidLevelName: return "invalid level name";
    case FileError::InvalidToken: return "invalid capture token";
    case FileError::InvalidViewport: return "invalid viewport";
    case FileError::NonFiniteValue: return "non-finite value";
    case FileError::InvalidCamera: return "invalid camera";
    case FileError::InvalidFullTransform: return "invalid full transform";
    case FileError::InvalidPosition: return "invalid position";
    case FileError::InvalidReservedField: return "non-zero reserved field";
    case FileError::EncodeRejected: return "encode rejected";
    }
    return "unknown error";
}

uint32_t Crc32(std::span<const uint8_t> bytes) noexcept
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : bytes)
    {
        crc ^= byte;
        for (uint32_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (kCrcPolynomial & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool Encode(const CameraCaptureFile12& capture, std::vector<uint8_t>& bytes,
            FileError& error) noexcept
{
    error = FileError::None;
    if (!ValidateLevelName(capture.levelName, error))
        return false;
    if (!ValidatePayload(capture.input, error))
        return false;

    try
    {
        const size_t nameLength = std::strlen(capture.levelName);
        std::vector<uint8_t> payload;
        payload.reserve(256);
        AppendU16(payload, static_cast<uint16_t>(nameLength));
        payload.insert(payload.end(), capture.levelName,
                       capture.levelName + nameLength);
        AppendU64(payload, capture.input.levelSession);
        AppendU64(payload, capture.input.captureEpoch);
        AppendU64(payload, capture.input.captureSequence);
        AppendU32(payload, capture.input.sourceGameFrame);
        AppendU32(payload, capture.input.viewportWidth);
        AppendU32(payload, capture.input.viewportHeight);
        for (uint32_t row = 0; row < 4; ++row)
            for (uint32_t column = 0; column < 4; ++column)
                AppendFloat(payload, capture.input.view.m[row][column]);
        for (uint32_t row = 0; row < 4; ++row)
            for (uint32_t column = 0; column < 4; ++column)
                AppendFloat(payload, capture.input.projection.m[row][column]);
        for (uint32_t row = 0; row < 4; ++row)
            for (uint32_t column = 0; column < 4; ++column)
                AppendFloat(payload, capture.input.fullTransform.m[row][column]);
        AppendFloat(payload, capture.input.position.x);
        AppendFloat(payload, capture.input.position.y);
        AppendFloat(payload, capture.input.position.z);

        if (payload.size() > kMaxPayloadBytes)
        {
            error = FileError::PayloadTooLarge;
            return false;
        }

        bytes.clear();
        bytes.reserve(kHeaderSize + payload.size());
        bytes.insert(bytes.end(), kMagic, kMagic + sizeof(kMagic));
        AppendU16(bytes, kFormatVersion);
        AppendU16(bytes, 0);
        AppendU32(bytes, static_cast<uint32_t>(payload.size()));
        AppendU32(bytes, Crc32(payload));
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        return true;
    }
    catch (...)
    {
        error = FileError::EncodeRejected;
        bytes.clear();
        return false;
    }
}

bool Decode(std::span<const uint8_t> bytes, CameraCaptureFile12& capture,
            FileError& error) noexcept
{
    error = FileError::None;
    if (bytes.empty())
    {
        error = FileError::EmptyInput;
        return false;
    }
    if (bytes.size() < kHeaderSize)
    {
        error = FileError::TruncatedHeader;
        return false;
    }
    if (std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0)
    {
        error = FileError::BadMagic;
        return false;
    }

    Reader reader{bytes};
    reader.position = sizeof(kMagic);
    const uint16_t version = reader.U16();
    const uint16_t reserved = reader.U16();
    const uint32_t payloadLength = reader.U32();
    const uint32_t expectedCrc = reader.U32();
    if (!reader.ok)
    {
        error = FileError::TruncatedHeader;
        return false;
    }
    if (version != kFormatVersion)
    {
        error = FileError::UnsupportedVersion;
        return false;
    }
    if (reserved != 0)
    {
        // Format v1 defines the reserved field as zero; a non-zero value is a
        // different (future) layout and must not be interpreted as v1.
        error = FileError::InvalidReservedField;
        return false;
    }
    if (payloadLength > kMaxPayloadBytes)
    {
        error = FileError::PayloadTooLarge;
        return false;
    }
    if (bytes.size() - kHeaderSize != payloadLength)
    {
        error = FileError::LengthMismatch;
        return false;
    }

    const std::span<const uint8_t> payload = bytes.subspan(kHeaderSize);
    if (Crc32(payload) != expectedCrc)
    {
        error = FileError::CrcMismatch;
        return false;
    }

    Reader body{payload};
    const uint16_t nameLength = body.U16();
    if (!body.ok || nameLength == 0 || nameLength > kMaxLevelNameBytes ||
        !body.Need(nameLength))
    {
        error = FileError::InvalidLevelName;
        return false;
    }
    std::memset(capture.levelName, 0, sizeof(capture.levelName));
    std::memcpy(capture.levelName, payload.data() + body.position, nameLength);
    body.position += nameLength;
    if (std::strlen(capture.levelName) != nameLength)
    {
        // Embedded NUL or missing terminator inside the declared name.
        error = FileError::InvalidLevelName;
        return false;
    }
    // Same rule as Encode: reject control characters and out-of-range names
    // instead of accepting bytes that the writer would never produce.
    if (!ValidateLevelName(capture.levelName, error))
        return false;

    gamecamera12::CameraCaptureInput12& input = capture.input;
    input = {};
    input.levelSession = body.U64();
    input.captureEpoch = body.U64();
    input.captureSequence = body.U64();
    input.sourceGameFrame = body.U32();
    input.viewportWidth = body.U32();
    input.viewportHeight = body.U32();
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            input.view.m[row][column] = body.F32();
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            input.projection.m[row][column] = body.F32();
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            input.fullTransform.m[row][column] = body.F32();
    input.position.x = body.F32();
    input.position.y = body.F32();
    input.position.z = body.F32();
    input.hasFullTransform = true;
    if (!body.ok)
    {
        error = FileError::LengthMismatch;
        return false;
    }
    if (body.position != payload.size())
    {
        error = FileError::LengthMismatch;
        return false;
    }

    if (!ValidatePayload(input, error))
        return false;
    return true;
}


ExportDecision EvaluateExportGate(const ExportGateState& state) noexcept
{
    if (!state.exportRequested)
        return ExportDecision::NotRequested;
    if (state.alreadyExported)
        return ExportDecision::AlreadyExported;
    if (!state.ownerThread)
        return ExportDecision::WrongThread;
    if (state.shuttingDown)
        return ExportDecision::Shutdown;
    if (!state.levelActive)
        return ExportDecision::NoLevel;
    if (state.teardownPending)
        return ExportDecision::Teardown;
    if (!state.hasPublished)
        return ExportDecision::NoPublished;
    if (!state.publishedTokenMatches)
        return ExportDecision::StaleToken;
    if (!state.levelNameMatches)
        return ExportDecision::LevelMismatch;
    if (!state.cameraValid)
        return ExportDecision::InvalidCamera;
    return ExportDecision::Write;
}

const char* ExportDecisionName(ExportDecision decision) noexcept
{
    switch (decision)
    {
    case ExportDecision::Write: return "write";
    case ExportDecision::NotRequested: return "not_requested";
    case ExportDecision::AlreadyExported: return "already_exported";
    case ExportDecision::WrongThread: return "wrong_thread";
    case ExportDecision::Shutdown: return "shutdown";
    case ExportDecision::NoLevel: return "no_level";
    case ExportDecision::Teardown: return "teardown";
    case ExportDecision::NoPublished: return "no_published";
    case ExportDecision::StaleToken: return "stale_token";
    case ExportDecision::LevelMismatch: return "level_mismatch";
    case ExportDecision::InvalidCamera: return "invalid_camera";
    }
    return "unknown";
}

const char* WriteErrorMessage(WriteError error) noexcept
{
    switch (error)
    {
    case WriteError::None: return "none";
    case WriteError::EmptyPath: return "empty path";
    case WriteError::AllocationFailed: return "allocation failed";
    case WriteError::DestinationExists: return "destination already exists";
    case WriteError::DirectoryMissing: return "destination directory missing";
    case WriteError::CreateFailed: return "create failed";
    case WriteError::WriteFailed: return "write failed";
    case WriteError::FlushFailed: return "flush failed";
    case WriteError::CloseFailed: return "close failed";
    case WriteError::PublishFailed: return "publish failed";
    case WriteError::CleanupFailed: return "cleanup failed";
    case WriteError::RetentionFull: return "handle retention full";
    }
    return "unknown error";
}

#ifdef OGSR_CAMERACAPTURE_TEST_HOOKS
WriteFaultInjection g_writeFaults;

void SetWriteFaultInjection(const WriteFaultInjection& faults) noexcept
{
    g_writeFaults = faults;
}

void ClearWriteFaultInjection() noexcept
{
    g_writeFaults = WriteFaultInjection{};
}

void MaybeFailAllocation()
{
    if (g_writeFaults.allocationCountdown < 0)
        return;
    if (g_writeFaults.allocationCountdown == 0)
        throw std::bad_alloc();
    --g_writeFaults.allocationCountdown;
}
#else
void MaybeFailAllocation() noexcept
{
}
#endif

#ifdef OGSR_CAMERACAPTURE_TEST_HOOKS
HANDLE g_lastProtectedHandle = INVALID_HANDLE_VALUE;

HANDLE LastProtectedHandle() noexcept
{
    return g_lastProtectedHandle;
}

BOOL HookSetHandleInformation(HANDLE file, DWORD mask, DWORD flags) noexcept
{
    if (g_writeFaults.failUnprotect)
    {
        ::SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return ::SetHandleInformation(file, mask, flags);
}

HANDLE HookCreateFile(LPCWSTR name, DWORD access, DWORD share,
                      LPSECURITY_ATTRIBUTES attributes, DWORD disposition,
                      DWORD flags, HANDLE templateFile) noexcept
{
    if (g_writeFaults.failCreate)
    {
        ::SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE file = ::CreateFileW(name, access, share, attributes, disposition,
                                flags, templateFile);
    if (file != INVALID_HANDLE_VALUE && g_writeFaults.protectCreatedHandle)
    {
        if (::SetHandleInformation(file, HANDLE_FLAG_PROTECT_FROM_CLOSE,
                                   HANDLE_FLAG_PROTECT_FROM_CLOSE))
            g_lastProtectedHandle = file;
    }
    return file;
}

BOOL HookWriteFile(HANDLE file, LPCVOID buffer, DWORD count,
                   LPDWORD written, LPOVERLAPPED overlapped) noexcept
{
    if (g_writeFaults.failWrite)
    {
        ::SetLastError(ERROR_WRITE_FAULT);
        return FALSE;
    }
    return ::WriteFile(file, buffer, count, written, overlapped);
}

BOOL HookFlush(HANDLE file) noexcept
{
    if (g_writeFaults.failFlush)
    {
        ::SetLastError(ERROR_WRITE_FAULT);
        return FALSE;
    }
    return ::FlushFileBuffers(file);
}

BOOL HookMove(LPCWSTR from, LPCWSTR to, DWORD flags) noexcept
{
    if (g_writeFaults.failMove)
    {
        ::SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return ::MoveFileExW(from, to, flags);
}

BOOL HookDelete(LPCWSTR name) noexcept
{
    if (g_writeFaults.failDelete)
    {
        // Perform the real delete so the test does not litter, but report the
        // failure path to the caller.
        ::DeleteFileW(name);
        ::SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return ::DeleteFileW(name);
}

BOOL HookClose(HANDLE file) noexcept
{
    if (g_writeFaults.failClose)
    {
        // Close for real to avoid leaking, but report failure to the caller.
        ::CloseHandle(file);
        ::SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return ::CloseHandle(file);
}
#else
HANDLE HookCreateFile(LPCWSTR name, DWORD access, DWORD share,
                      LPSECURITY_ATTRIBUTES attributes, DWORD disposition,
                      DWORD flags, HANDLE templateFile) noexcept
{
    return ::CreateFileW(name, access, share, attributes, disposition, flags,
                         templateFile);
}

BOOL HookWriteFile(HANDLE file, LPCVOID buffer, DWORD count,
                   LPDWORD written, LPOVERLAPPED overlapped) noexcept
{
    return ::WriteFile(file, buffer, count, written, overlapped);
}

BOOL HookFlush(HANDLE file) noexcept
{
    return ::FlushFileBuffers(file);
}

BOOL HookMove(LPCWSTR from, LPCWSTR to, DWORD flags) noexcept
{
    return ::MoveFileExW(from, to, flags);
}

BOOL HookDelete(LPCWSTR name) noexcept
{
    return ::DeleteFileW(name);
}

BOOL HookClose(HANDLE file) noexcept
{
    return ::CloseHandle(file);
}

BOOL HookSetHandleInformation(HANDLE file, DWORD mask, DWORD flags) noexcept
{
    return ::SetHandleInformation(file, mask, flags);
}
#endif

// Process-lifetime retention for handles whose CloseHandle cannot be completed
// (for example an unrecoverable PROTECT_FROM_CLOSE failure). The OS closes
// them at process exit. Fixed capacity, no dynamic allocation.
constexpr size_t kHandleRetentionSlots = 4;
HANDLE g_retainedHandles[kHandleRetentionSlots] = {};
bool g_retentionFull = false;

#ifdef OGSR_CAMERACAPTURE_TEST_HOOKS
HANDLE g_lastUnretainedHandle = INVALID_HANDLE_VALUE;

HANDLE LastRetainedHandle() noexcept
{
    for (size_t i = 0; i < kHandleRetentionSlots; ++i)
        if (g_retainedHandles[i] != nullptr)
            return g_retainedHandles[i];
    return INVALID_HANDLE_VALUE;
}

HANDLE LastUnretainedHandle() noexcept
{
    return g_lastUnretainedHandle;
}

size_t RetainedHandleCount() noexcept
{
    size_t count = 0;
    for (HANDLE handle : g_retainedHandles)
        count += handle != nullptr ? 1u : 0u;
    return count;
}

HANDLE RetainedHandleAt(size_t index) noexcept
{
    return index < kHandleRetentionSlots ? g_retainedHandles[index]
                                         : INVALID_HANDLE_VALUE;
}

bool RetentionFull() noexcept
{
    return g_retentionFull;
}

void ResetRetentionForTests() noexcept
{
    for (HANDLE& handle : g_retainedHandles)
        handle = nullptr;
    g_lastUnretainedHandle = INVALID_HANDLE_VALUE;
    g_retentionFull = false;
}
#endif

// Returns false when the fixed store is full. A false result is not cleanup:
// the handle is still open, is recorded for diagnostics, and the exporter is
// latched fail-closed so repeated exports cannot silently leak more handles.
bool RetainUnclosedHandle(HANDLE file) noexcept
{
    for (HANDLE& slot : g_retainedHandles)
    {
        if (slot == nullptr)
        {
            slot = file;
            return true;
        }
    }
    g_retentionFull = true;
#ifdef OGSR_CAMERACAPTURE_TEST_HOOKS
    g_lastUnretainedHandle = file;
#endif
    return false;
}

bool WriteFileAtomic(const wchar_t* path, std::span<const uint8_t> bytes,
                     WriteError& error,
                     WriteDiagnostics* diagnostics) noexcept
{
    error = WriteError::None;
    WriteDiagnostics localDiagnostics;
    WriteDiagnostics& diag = diagnostics ? *diagnostics : localDiagnostics;
    diag = WriteDiagnostics{};

    HANDLE file = INVALID_HANDLE_VALUE;
    std::wstring temporary;
    bool temporaryCreated = false;
    bool published = false;
    bool ok = false;

    auto attempt = [&]() -> bool
    {
        if (path == nullptr || path[0] == L'\0')
        {
            error = WriteError::EmptyPath;
            return false;
        }
        if (bytes.empty() || bytes.size() > 0x7FFFFFFFu)
        {
            error = WriteError::WriteFailed;
            return false;
        }
        if (g_retentionFull)
        {
            // Fail closed: an earlier unrecoverable close already consumed the
            // fixed retention store. Do not create another file that could
            // leak a handle without any way to report it.
            error = WriteError::RetentionFull;
            diag.retentionOverflow = true;
            return false;
        }
        if (::GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        {
            error = WriteError::DestinationExists;
            return false;
        }

        MaybeFailAllocation();
        std::wstring fullPath(path);
        const size_t separator = fullPath.find_last_of(L"\\/");
        MaybeFailAllocation();
        std::wstring directory =
            separator == std::wstring::npos ? std::wstring(L".")
                                            : fullPath.substr(0, separator);
        const DWORD directoryAttributes = ::GetFileAttributesW(directory.c_str());
        if (directoryAttributes == INVALID_FILE_ATTRIBUTES ||
            (directoryAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            error = WriteError::DirectoryMissing;
            return false;
        }

        wchar_t suffix[64] = {};
        std::swprintf(suffix, std::size(suffix), L".tmp-%lu-%llu",
                      static_cast<unsigned long>(::GetCurrentProcessId()),
                      static_cast<unsigned long long>(::GetTickCount64()));
        MaybeFailAllocation();
        temporary = fullPath;
        temporary += suffix;

        file = HookCreateFile(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = WriteError::CreateFailed;
            return false;
        }
        temporaryCreated = true;

        size_t written = 0;
        while (written < bytes.size())
        {
            const DWORD chunk = static_cast<DWORD>(
                (bytes.size() - written) > 0x40000000u
                    ? 0x40000000u
                    : (bytes.size() - written));
            DWORD chunkWritten = 0;
            if (!HookWriteFile(file, bytes.data() + written, chunk,
                               &chunkWritten, nullptr) ||
                chunkWritten == 0)
            {
                error = WriteError::WriteFailed;
                return false;
            }
            written += chunkWritten;
        }
        if (!HookFlush(file))
        {
            error = WriteError::FlushFailed;
            return false;
        }
        if (!HookClose(file))
        {
            // CloseHandle can fail while the handle is still open (for example
            // HANDLE_FLAG_PROTECT_FROM_CLOSE). Keep ownership: the cleanup
            // path below is the only place allowed to retry the close, and the
            // destination is not published in this state.
            diag.handleReleased = false;
            error = WriteError::CloseFailed;
            return false;
        }
        file = INVALID_HANDLE_VALUE;
        diag.handleReleased = true;

        if (!HookMove(temporary.c_str(), path, MOVEFILE_WRITE_THROUGH))
        {
            error = WriteError::PublishFailed;
            return false;
        }
        published = true;
        return true;
    };

    try
    {
        ok = attempt();
    }
    catch (...)
    {
        // Keep the public noexcept contract: allocation or any other host
        // exception becomes an explicit export failure, never std::terminate.
        error = WriteError::AllocationFailed;
        ok = false;
    }

    if (file != INVALID_HANDLE_VALUE)
    {
        // The first close either succeeded (file is INVALID here) or failed
        // while the handle stayed open. Never double-close a handle whose
        // close succeeded.
        diag.handleReleased = false;
        if (HookClose(file))
        {
            diag.handleReleased = true;
            file = INVALID_HANDLE_VALUE;
        }
        else
        {
            // CloseHandle failed. Check the real state: if the handle is still
            // valid it may be protected; clear PROTECT_FROM_CLOSE once and
            // retry the close. Otherwise retain the handle for process exit.
            DWORD flags = 0;
            const bool stillValid =
                ::GetHandleInformation(file, &flags) != FALSE;
            if (!stillValid)
            {
                diag.handleReleased = true;
                file = INVALID_HANDLE_VALUE;
            }
            else
            {
                if (HookSetHandleInformation(file, HANDLE_FLAG_PROTECT_FROM_CLOSE,
                                             0) &&
                    HookClose(file))
                {
                    diag.handleReleased = true;
                    diag.handleRecovered = true;
                    file = INVALID_HANDLE_VALUE;
                }
                if (!diag.handleReleased)
                {
                    const bool retained = RetainUnclosedHandle(file);
                    diag.handleRetained = retained;
                    diag.retentionOverflow = !retained;
                    if (diag.cleanup == WriteError::None)
                        diag.cleanup = WriteError::CloseFailed;
                    if (error == WriteError::None)
                        error = WriteError::CloseFailed;
                    if (!retained)
                    {
                        // The handle remains open and is deliberately not
                        // claimed as retained. The OS owns it until process
                        // exit. g_retentionFull latches future exports closed.
                        diag.cleanup = WriteError::CloseFailed;
                    }
                }
            }
        }
    }

    if (!ok && temporaryCreated && !published && !temporary.empty())
    {
        if (HookDelete(temporary.c_str()) ||
            ::GetLastError() == ERROR_FILE_NOT_FOUND)
        {
            diag.temporaryRemoved = true;
        }
        else
        {
            diag.temporaryRemoved = false;
            if (diag.cleanup == WriteError::None)
                diag.cleanup = WriteError::CleanupFailed;
            if (error == WriteError::None)
                error = WriteError::CleanupFailed;
        }
    }

    diag.primary = error;
    return ok;
}

} // namespace cameracapture12
