// F4.0-B3-B1: CPU-only tests for the camera capture wire format, the engine
// export gate and the transactional writer. No D3D12 device is created.

#include "b3b1_sha256.h"
#include "dx12cameracapturefile.h"
#include "dx12gamecamera.h"

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using cameracapture12::CameraCaptureFile12;
using cameracapture12::ExportDecision;
using cameracapture12::ExportGateState;
using cameracapture12::FileError;

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

gamecamera12::CameraCaptureInput12 MakeInput(uint64_t session = 2,
                                               uint64_t sequence = 7)
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
    input.sourceGameFrame = 1234;
    input.captureSequence = sequence;
    input.levelSession = session;
    input.captureEpoch = 9;
    input.viewportWidth = 1920;
    input.viewportHeight = 1080;
    return input;
}

CameraCaptureFile12 MakeCapture()
{
    CameraCaptureFile12 capture{};
    std::snprintf(capture.levelName, sizeof(capture.levelName), "%s", "jupiter");
    capture.input = MakeInput();
    return capture;
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

void PutU64(std::vector<uint8_t>& bytes, size_t offset, uint64_t value)
{
    for (uint32_t i = 0; i < 8; ++i)
        bytes[offset + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
}

void PutF32(std::vector<uint8_t>& bytes, size_t offset, float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    PutU32(bytes, offset, bits);
}

void FixCrc(std::vector<uint8_t>& bytes)
{
    const std::span<const uint8_t> payload(bytes.data() + 20, bytes.size() - 20);
    PutU32(bytes, 16, cameracapture12::Crc32(payload));
}

// Payload field offsets for the canonical "jupiter" name (7 bytes).
struct Offsets
{
    size_t levelSession = 22 + 7;
    size_t captureEpoch = levelSession + 8;
    size_t captureSequence = captureEpoch + 8;
    size_t sourceGameFrame = captureSequence + 8;
    size_t viewportWidth = sourceGameFrame + 4;
    size_t viewportHeight = viewportWidth + 4;
    size_t view = viewportHeight + 4;
    size_t projection = view + 64;
    size_t fullTransform = projection + 64;
    size_t position = fullTransform + 64;
};

void TestRoundTrip()
{
    const CameraCaptureFile12 capture = MakeCapture();
    std::vector<uint8_t> bytes;
    FileError error = FileError::None;
    Check(cameracapture12::Encode(capture, bytes, error), "encode accepts a valid capture");
    Check(error == FileError::None, "encode reports no error");
    Check(bytes.size() == 20 + 242 + 7, "payload size is fixed for the name length");

    CameraCaptureFile12 decoded{};
    Check(cameracapture12::Decode(bytes, decoded, error), "decode accepts encoded bytes");
    Check(std::strcmp(decoded.levelName, "jupiter") == 0, "level name round-trips");
    Check(decoded.input.levelSession == capture.input.levelSession, "session round-trips");
    Check(decoded.input.captureEpoch == capture.input.captureEpoch, "epoch round-trips");
    Check(decoded.input.captureSequence == capture.input.captureSequence, "sequence round-trips");
    Check(decoded.input.sourceGameFrame == capture.input.sourceGameFrame, "frame round-trips");
    Check(decoded.input.viewportWidth == capture.input.viewportWidth, "viewport width round-trips");
    Check(decoded.input.viewportHeight == capture.input.viewportHeight, "viewport height round-trips");

    bool bitsEqual = true;
    auto compareMatrix = [&](const DirectX::XMFLOAT4X4& a,
                             const DirectX::XMFLOAT4X4& b) {
        for (uint32_t row = 0; row < 4; ++row)
            for (uint32_t column = 0; column < 4; ++column)
            {
                uint32_t left = 0, right = 0;
                std::memcpy(&left, &a.m[row][column], 4);
                std::memcpy(&right, &b.m[row][column], 4);
                if (left != right)
                    bitsEqual = false;
            }
    };
    compareMatrix(capture.input.view, decoded.input.view);
    compareMatrix(capture.input.projection, decoded.input.projection);
    compareMatrix(capture.input.fullTransform, decoded.input.fullTransform);
    for (const auto& pair : {std::pair{&capture.input.position.x, &decoded.input.position.x},
                             std::pair{&capture.input.position.y, &decoded.input.position.y},
                             std::pair{&capture.input.position.z, &decoded.input.position.z}})
    {
        uint32_t left = 0, right = 0;
        std::memcpy(&left, pair.first, 4);
        std::memcpy(&right, pair.second, 4);
        if (left != right)
            bitsEqual = false;
    }
    Check(bitsEqual, "all camera floats round-trip bit-exactly");

    std::vector<uint8_t> again;
    Check(cameracapture12::Encode(decoded, again, error), "re-encode succeeds");
    Check(again == bytes, "re-encode is byte-identical");
}

void TestFormatHeader()
{
    const CameraCaptureFile12 capture = MakeCapture();
    std::vector<uint8_t> bytes;
    FileError error = FileError::None;
    Check(cameracapture12::Encode(capture, bytes, error), "encode for header test");
    Check(std::memcmp(bytes.data(), "OGSRCAM1", 8) == 0, "magic is OGSRCAM1");
    Check(bytes[8] == 1 && bytes[9] == 0, "version is 1 (LE)");
    const uint32_t length = static_cast<uint32_t>(bytes[12]) |
                            (static_cast<uint32_t>(bytes[13]) << 8) |
                            (static_cast<uint32_t>(bytes[14]) << 16) |
                            (static_cast<uint32_t>(bytes[15]) << 24);
    Check(length == bytes.size() - 20, "payload length matches remaining bytes");
    const std::span<const uint8_t> payload(bytes.data() + 20, bytes.size() - 20);
    const uint32_t crc = static_cast<uint32_t>(bytes[16]) |
                         (static_cast<uint32_t>(bytes[17]) << 8) |
                         (static_cast<uint32_t>(bytes[18]) << 16) |
                         (static_cast<uint32_t>(bytes[19]) << 24);
    Check(crc == cameracapture12::Crc32(payload), "CRC matches payload");
    Check(bytes[20] == 7 && bytes[21] == 0, "level name length is little-endian");
}

void TestCorruption()
{
    const CameraCaptureFile12 capture = MakeCapture();
    std::vector<uint8_t> bytes;
    FileError error = FileError::None;
    cameracapture12::Encode(capture, bytes, error);

    {
        std::vector<uint8_t> mutated = bytes;
        mutated[bytes.size() - 1] ^= 0xFF;
        CameraCaptureFile12 decoded{};
        Check(!cameracapture12::Decode(mutated, decoded, error), "flipped payload byte is rejected");
        Check(error == FileError::CrcMismatch, "flipped payload byte reports CRC mismatch");
    }
    {
        std::vector<uint8_t> mutated = bytes;
        mutated.push_back(0);
        CameraCaptureFile12 decoded{};
        Check(!cameracapture12::Decode(mutated, decoded, error), "trailing byte is rejected");
        Check(error == FileError::LengthMismatch, "trailing byte reports length mismatch");
    }
    {
        std::vector<uint8_t> mutated(bytes.begin(), bytes.end() - 1);
        CameraCaptureFile12 decoded{};
        Check(!cameracapture12::Decode(mutated, decoded, error), "truncated payload is rejected");
    }
    for (size_t length = 0; length < 20; ++length)
    {
        std::vector<uint8_t> mutated(bytes.begin(), bytes.begin() + length);
        CameraCaptureFile12 decoded{};
        Check(!cameracapture12::Decode(mutated, decoded, error), "truncated header is rejected");
    }
    {
        std::vector<uint8_t> mutated = bytes;
        mutated[0] = 'X';
        CameraCaptureFile12 decoded{};
        Check(!cameracapture12::Decode(mutated, decoded, error), "bad magic is rejected");
        Check(error == FileError::BadMagic, "bad magic reports BadMagic");
    }
    {
        std::vector<uint8_t> mutated = bytes;
        mutated[8] = 2;
        CameraCaptureFile12 decoded{};
        Check(!cameracapture12::Decode(mutated, decoded, error), "unknown version is rejected");
        Check(error == FileError::UnsupportedVersion, "unknown version reports UnsupportedVersion");
    }
    {
        std::vector<uint8_t> mutated = bytes;
        PutU32(mutated, 12, static_cast<uint32_t>(bytes.size()));
        CameraCaptureFile12 decoded{};
        Check(!cameracapture12::Decode(mutated, decoded, error), "wrong payload length is rejected");
        Check(error == FileError::LengthMismatch, "wrong payload length reports LengthMismatch");
    }
}

void TestInvalidFields()
{
    const Offsets offsets;
    const CameraCaptureFile12 capture = MakeCapture();
    std::vector<uint8_t> base;
    FileError error = FileError::None;
    cameracapture12::Encode(capture, base, error);

    auto decodeMutated = [&](std::vector<uint8_t> bytes, FileError& result) {
        FixCrc(bytes);
        CameraCaptureFile12 decoded{};
        const bool ok = cameracapture12::Decode(bytes, decoded, result);
        return ok;
    };

    {
        std::vector<uint8_t> bytes = base;
        PutF32(bytes, offsets.view + 5 * 4, std::nanf(""));
        FileError result = FileError::None;
        Check(!decodeMutated(bytes, result), "NaN matrix element is rejected");
        Check(result == FileError::NonFiniteValue, "NaN reports NonFiniteValue");
    }
    {
        std::vector<uint8_t> bytes = base;
        PutF32(bytes, offsets.position, INFINITY);
        FileError result = FileError::None;
        Check(!decodeMutated(bytes, result), "infinite position is rejected");
        Check(result == FileError::NonFiniteValue, "infinite position reports NonFiniteValue");
    }
    {
        std::vector<uint8_t> bytes = base;
        PutU64(bytes, offsets.levelSession, 0);
        FileError result = FileError::None;
        Check(!decodeMutated(bytes, result), "zero session is rejected");
        Check(result == FileError::InvalidToken, "zero session reports InvalidToken");
    }
    {
        std::vector<uint8_t> bytes = base;
        PutU64(bytes, offsets.captureSequence, 0);
        FileError result = FileError::None;
        Check(!decodeMutated(bytes, result), "zero sequence is rejected");
        Check(result == FileError::InvalidToken, "zero sequence reports InvalidToken");
    }
    {
        std::vector<uint8_t> bytes = base;
        PutU32(bytes, offsets.viewportWidth, 0);
        FileError result = FileError::None;
        Check(!decodeMutated(bytes, result), "zero viewport is rejected");
        Check(result == FileError::InvalidViewport, "zero viewport reports InvalidViewport");
    }
    {
        std::vector<uint8_t> bytes = base;
        PutU32(bytes, offsets.viewportHeight, 100000);
        FileError result = FileError::None;
        Check(!decodeMutated(bytes, result), "oversized viewport is rejected");
        Check(result == FileError::InvalidViewport, "oversized viewport reports InvalidViewport");
    }
    {
        std::vector<uint8_t> bytes = base;
        for (size_t index = 0; index < 64; index += 4)
            PutF32(bytes, offsets.view + index, 0.0f);
        FileError result = FileError::None;
        Check(!decodeMutated(bytes, result), "degenerate view is rejected");
        Check(result == FileError::InvalidCamera, "degenerate view reports InvalidCamera");
    }
    {
        std::vector<uint8_t> bytes = base;
        const size_t element = offsets.fullTransform + 2 * 4;
        float value = capture.input.fullTransform.m[0][2];
        PutF32(bytes, element, value + 1.0f);
        FileError result = FileError::None;
        Check(!decodeMutated(bytes, result), "inconsistent full transform is rejected");
        Check(result == FileError::InvalidFullTransform,
              "inconsistent full transform reports InvalidFullTransform");
    }
}

void TestLevelNames()
{
    std::vector<uint8_t> bytes;
    FileError error = FileError::None;

    CameraCaptureFile12 empty = MakeCapture();
    empty.levelName[0] = '\0';
    Check(!cameracapture12::Encode(empty, bytes, error), "empty level name is rejected");
    Check(error == FileError::InvalidLevelName, "empty level name reports InvalidLevelName");

    CameraCaptureFile12 control = MakeCapture();
    std::snprintf(control.levelName, sizeof(control.levelName), "jup%cter", 1);
    Check(!cameracapture12::Encode(control, bytes, error), "control character is rejected");

    CameraCaptureFile12 tooLong = MakeCapture();
    // Craft a payload whose declared name length is 128 (> the 127 limit).
    std::vector<uint8_t> craftedName;
    FileError craftError = FileError::None;
    Check(cameracapture12::Encode(MakeCapture(), craftedName, craftError), "name craft base");
    PutU16(craftedName, 20, 128);
    craftedName.insert(craftedName.begin() + 22, 'x');
    PutU32(craftedName, 12, static_cast<uint32_t>(craftedName.size() - 20));
    FixCrc(craftedName);
    CameraCaptureFile12 craftedDecoded{};
    Check(!cameracapture12::Decode(craftedName, craftedDecoded, craftError),
          "128-byte declared name is rejected");
    Check(craftError == FileError::InvalidLevelName, "128-byte name reports InvalidLevelName");

    CameraCaptureFile12 longest = MakeCapture();
    std::memset(longest.levelName, 'a', 127);
    longest.levelName[127] = '\0';
    Check(cameracapture12::Encode(longest, bytes, error), "127-byte name is accepted");
    (void)tooLong;

    // Crafted payload with an embedded NUL inside the declared name.
    std::vector<uint8_t> corrupted;
    Check(cameracapture12::Encode(MakeCapture(), corrupted, error), "base encode");
    const size_t nameOffset = 22;
    corrupted[nameOffset] = '\0';
    FixCrc(corrupted);
    CameraCaptureFile12 decoded{};
    Check(!cameracapture12::Decode(corrupted, decoded, error), "embedded NUL name is rejected");
    Check(error == FileError::InvalidLevelName, "embedded NUL reports InvalidLevelName");
}

void TestEncodeRejectsInvalidCamera()
{
    std::vector<uint8_t> bytes;
    FileError error = FileError::None;

    CameraCaptureFile12 badToken = MakeCapture();
    badToken.input.captureEpoch = 0;
    Check(!cameracapture12::Encode(badToken, bytes, error), "encode rejects zero epoch");

    CameraCaptureFile12 badViewport = MakeCapture();
    badViewport.input.viewportWidth = 0;
    Check(!cameracapture12::Encode(badViewport, bytes, error), "encode rejects zero viewport");

    CameraCaptureFile12 badFull = MakeCapture();
    badFull.input.fullTransform.m[1][1] += 5.0f;
    Check(!cameracapture12::Encode(badFull, bytes, error), "encode rejects inconsistent full transform");
    Check(error == FileError::InvalidFullTransform, "inconsistent full transform code");
}

ExportGateState Gate()
{
    ExportGateState state;
    state.exportRequested = true;
    state.ownerThread = true;
    state.levelActive = true;
    state.hasPublished = true;
    state.publishedTokenMatches = true;
    state.levelNameMatches = true;
    state.cameraValid = true;
    return state;
}

void TestExportGate()
{
    using D = ExportDecision;
    Check(cameracapture12::EvaluateExportGate(Gate()) == D::Write, "complete gate writes");

    auto expect = [](const ExportGateState& state, D expected, const char* what) {
        Check(cameracapture12::EvaluateExportGate(state) == expected, what);
    };

    ExportGateState state = Gate();
    state.exportRequested = false;
    expect(state, D::NotRequested, "gate rejects without request");
    state = Gate();
    state.alreadyExported = true;
    expect(state, D::AlreadyExported, "gate rejects a second export");
    state = Gate();
    state.ownerThread = false;
    expect(state, D::WrongThread, "gate rejects wrong thread");
    state = Gate();
    state.shuttingDown = true;
    expect(state, D::Shutdown, "gate rejects shutdown");
    state = Gate();
    state.levelActive = false;
    expect(state, D::NoLevel, "gate rejects no level");
    state = Gate();
    state.teardownPending = true;
    expect(state, D::Teardown, "gate rejects teardown");
    state = Gate();
    state.hasPublished = false;
    expect(state, D::NoPublished, "gate rejects pending-only capture");
    state = Gate();
    state.publishedTokenMatches = false;
    expect(state, D::StaleToken, "gate rejects a stale token");
    state = Gate();
    state.levelNameMatches = false;
    expect(state, D::LevelMismatch, "gate rejects a level mismatch");
    state = Gate();
    state.cameraValid = false;
    expect(state, D::InvalidCamera, "gate rejects an invalid camera");
}

gamecamera12::CameraCaptureInput12 InputFor(
    const gamecamera12::CameraPublicationState12& publication,
    uint64_t session, uint64_t sequence)
{
    gamecamera12::CameraCaptureInput12 input = MakeInput(session, sequence);
    input.captureEpoch = publication.CurrentEpoch();
    return input;
}

void TestPublicationLifecycle()
{
    using gamecamera12::CameraError12;
    using gamecamera12::CameraPublicationState12;

    CameraPublicationState12 publication;
    ExportGateState gate = Gate();
    gate.hasPublished = false;
    gate.publishedTokenMatches = false;
    Check(cameracapture12::EvaluateExportGate(gate) != ExportDecision::Write,
          "no publication before a session");

    Check(publication.BeginLevelSession(2) == CameraError12::None, "begin session");
    const auto capture = publication.Capture(InputFor(publication, 2, 7));
    Check(capture.error == CameraError12::None && capture.camera.has_value(), "capture accepted");
    Check(!publication.HasPublished(), "capture stays pending");
    gate = Gate();
    gate.hasPublished = publication.HasPublished();
    gate.publishedTokenMatches = publication.HasPublished();
    Check(cameracapture12::EvaluateExportGate(gate) == ExportDecision::NoPublished,
          "pending capture is not exportable");

    Check(publication.PublishPending(capture.camera->Token()) == CameraError12::None,
          "publish pending");
    Check(publication.HasPublished(), "published snapshot exists");
    gate = Gate();
    gate.hasPublished = true;
    gate.publishedTokenMatches =
        publication.Published()->Token() == capture.camera->Token();
    Check(gate.publishedTokenMatches, "published token matches capture token");
    Check(cameracapture12::EvaluateExportGate(gate) == ExportDecision::Write,
          "published capture is exportable");

    Check(publication.BeginTeardown() == CameraError12::None, "begin teardown");
    gate = Gate();
    gate.teardownPending = true;
    gate.hasPublished = publication.HasPublished();
    Check(cameracapture12::EvaluateExportGate(gate) == ExportDecision::Teardown,
          "teardown blocks export");

    Check(publication.CompleteTeardown() == CameraError12::None, "complete teardown");
    gate = Gate();
    gate.levelActive = false;
    gate.hasPublished = publication.HasPublished();
    Check(cameracapture12::EvaluateExportGate(gate) == ExportDecision::NoLevel,
          "no live level blocks export");

    Check(publication.BeginLevelSession(3) == CameraError12::None, "second session");
    const auto second = publication.Capture(InputFor(publication, 3, 8));
    Check(second.error == CameraError12::None, "second capture");
    Check(publication.PublishPending(second.camera->Token()) == CameraError12::None,
          "second publish");
    Check(publication.Published()->Token() != capture.camera->Token(),
          "second session has a different token");

    Check(publication.Shutdown() == CameraError12::None, "shutdown");
    gate = Gate();
    gate.shuttingDown = true;
    gate.hasPublished = publication.HasPublished();
    Check(cameracapture12::EvaluateExportGate(gate) == ExportDecision::Shutdown,
          "shutdown blocks export");
}

void TestWriter()
{
    const CameraCaptureFile12 capture = MakeCapture();
    std::vector<uint8_t> bytes;
    FileError error = FileError::None;
    cameracapture12::Encode(capture, bytes, error);

    const fs::path root = fs::temp_directory_path() / "ogsr-b3b1-writer";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    Check(!ec, "writer temp directory created");

    const fs::path destination = root / "camera.bin";
    cameracapture12::WriteError writeError = cameracapture12::WriteError::None;
    Check(cameracapture12::WriteFileAtomic(destination.c_str(), bytes, writeError),
          "atomic write succeeds");
    Check(writeError == cameracapture12::WriteError::None, "atomic write reports no error");

    std::vector<uint8_t> readBack;
    {
        std::FILE* file = nullptr;
        _wfopen_s(&file, destination.c_str(), L"rb");
        Check(file != nullptr, "written file opens");
        if (file)
        {
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            readBack.resize(static_cast<size_t>(size));
            const size_t read = std::fread(readBack.data(), 1, readBack.size(), file);
            std::fclose(file);
            Check(read == readBack.size(), "written file reads back");
        }
    }
    Check(readBack == bytes, "written bytes are identical");

    size_t leftovers = 0;
    for (const auto& entry : fs::directory_iterator(root))
    {
        if (entry.path().filename().wstring().find(L".tmp-") != std::wstring::npos)
            ++leftovers;
    }
    Check(leftovers == 0, "no temporary files remain after success");

    // Existing destination is never overwritten.
    std::vector<uint8_t> original = readBack;
    Check(!cameracapture12::WriteFileAtomic(destination.c_str(), bytes, writeError),
          "existing destination is rejected");
    Check(writeError == cameracapture12::WriteError::DestinationExists,
          "existing destination reports DestinationExists");
    std::vector<uint8_t> afterFailure;
    {
        std::FILE* file = nullptr;
        _wfopen_s(&file, destination.c_str(), L"rb");
        if (file)
        {
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            afterFailure.resize(static_cast<size_t>(size));
            (void)std::fread(afterFailure.data(), 1, afterFailure.size(), file);
            std::fclose(file);
        }
    }
    Check(afterFailure == original, "rejected write leaves the destination untouched");

    // Missing directory.
    const fs::path missing = root / "missing" / "camera.bin";
    Check(!cameracapture12::WriteFileAtomic(missing.c_str(), bytes, writeError),
          "missing directory is rejected");
    Check(writeError == cameracapture12::WriteError::DirectoryMissing,
          "missing directory reports DirectoryMissing");

    // Empty path.
    Check(!cameracapture12::WriteFileAtomic(L"", bytes, writeError), "empty path is rejected");
    Check(writeError == cameracapture12::WriteError::EmptyPath, "empty path reports EmptyPath");

    fs::remove_all(root, ec);
}

void TestDecodeEncodeSymmetry()
{
    const CameraCaptureFile12 capture = MakeCapture();
    std::vector<uint8_t> base;
    FileError error = FileError::None;
    Check(cameracapture12::Encode(capture, base, error), "symmetry base encode");

    {
        std::vector<uint8_t> mutated = base;
        mutated[22] = 0x01; // SOH control character inside the name
        FixCrc(mutated);
        CameraCaptureFile12 decoded{};
        Check(!cameracapture12::Decode(mutated, decoded, error),
              "control character 0x01 in level name is rejected");
        Check(error == FileError::InvalidLevelName,
              "0x01 reports InvalidLevelName");
    }
    {
        std::vector<uint8_t> mutated = base;
        mutated[22] = 0x7F; // DEL control character
        FixCrc(mutated);
        CameraCaptureFile12 decoded{};
        Check(!cameracapture12::Decode(mutated, decoded, error),
              "control character 0x7F in level name is rejected");
        Check(error == FileError::InvalidLevelName,
              "0x7F reports InvalidLevelName");
    }
    {
        std::vector<uint8_t> mutated = base;
        PutU16(mutated, 10, 1);
        CameraCaptureFile12 decoded{};
        Check(!cameracapture12::Decode(mutated, decoded, error),
              "reserved field 1 is rejected");
        Check(error == FileError::InvalidReservedField,
              "reserved 1 reports InvalidReservedField");
    }
    {
        std::vector<uint8_t> mutated = base;
        PutU16(mutated, 10, 0xFFFF);
        CameraCaptureFile12 decoded{};
        Check(!cameracapture12::Decode(mutated, decoded, error),
              "reserved field 0xFFFF is rejected");
        Check(error == FileError::InvalidReservedField,
              "reserved 0xFFFF reports InvalidReservedField");
    }
    {
        CameraCaptureFile12 decoded{};
        Check(cameracapture12::Decode(base, decoded, error),
              "original capture remains valid");
        Check(std::strcmp(decoded.levelName, "jupiter") == 0,
              "original level name preserved");
        uint32_t a = 0, b = 0;
        std::memcpy(&a, &decoded.input.view.m[1][2], 4);
        std::memcpy(&b, &capture.input.view.m[1][2], 4);
        Check(a == b, "original matrix bits preserved");
    }
}

#ifdef OGSR_CAMERACAPTURE_TEST_HOOKS
size_t CountTempFiles(const fs::path& directory)
{
    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(directory))
    {
        if (entry.path().filename().wstring().find(L".tmp-") !=
            std::wstring::npos)
            ++count;
    }
    return count;
}

void TestWriteFaultInjection()
{
    const CameraCaptureFile12 capture = MakeCapture();
    std::vector<uint8_t> bytes;
    FileError fileError = FileError::None;
    cameracapture12::Encode(capture, bytes, fileError);

    const fs::path root = fs::temp_directory_path() / "ogsr-b3b1-faults";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    Check(!ec, "fault directory created");

    auto clear = []() { cameracapture12::ClearWriteFaultInjection(); };
    auto tempCount = [&]() { return CountTempFiles(root); };

    const fs::path destination = root / "camera.bin";
    cameracapture12::WriteError writeError = cameracapture12::WriteError::None;

    // Allocation failures at each of the three path-construction points.
    for (int countdown = 0; countdown < 3; ++countdown)
    {
        cameracapture12::WriteFaultInjection faults;
        faults.allocationCountdown = countdown;
        cameracapture12::SetWriteFaultInjection(faults);
        const bool ok = cameracapture12::WriteFileAtomic(destination.c_str(),
                                                         bytes, writeError);
        clear();
        Check(!ok && writeError == cameracapture12::WriteError::AllocationFailed,
              "allocation failure returns AllocationFailed");
        Check(!fs::exists(destination), "allocation failure leaves no destination");
        Check(tempCount() == 0, "allocation failure leaves no temp file");
    }

    struct ApiFault
    {
        bool cameracapture12::WriteFaultInjection::*flag;
        cameracapture12::WriteError expected;
        const char* label;
    };
    const ApiFault apiFaults[] = {
        {&cameracapture12::WriteFaultInjection::failCreate,
         cameracapture12::WriteError::CreateFailed, "CreateFileW"},
        {&cameracapture12::WriteFaultInjection::failWrite,
         cameracapture12::WriteError::WriteFailed, "WriteFile"},
        {&cameracapture12::WriteFaultInjection::failFlush,
         cameracapture12::WriteError::FlushFailed, "FlushFileBuffers"},
        {&cameracapture12::WriteFaultInjection::failMove,
         cameracapture12::WriteError::PublishFailed, "MoveFileExW"},
        {&cameracapture12::WriteFaultInjection::failClose,
         cameracapture12::WriteError::CloseFailed, "CloseHandle"},
    };
    for (const ApiFault& fault : apiFaults)
    {
        cameracapture12::WriteFaultInjection faults;
        (faults.*fault.flag) = true;
        cameracapture12::SetWriteFaultInjection(faults);
        cameracapture12::WriteDiagnostics diagnostics;
        const bool ok = cameracapture12::WriteFileAtomic(
            destination.c_str(), bytes, writeError, &diagnostics);
        clear();
        Check(!ok, "API fault fails the export");
        Check(writeError == fault.expected, "API fault reports the expected code");
        Check(diagnostics.primary == fault.expected,
              "diagnostics preserve the primary error");
        Check(!fs::exists(destination), "API fault leaves no destination");
        Check(tempCount() == 0, "API fault leaves no temp file");
    }

    // Cleanup failure is reported without hiding the primary cause.
    {
        cameracapture12::WriteFaultInjection faults;
        faults.failMove = true;
        faults.failDelete = true;
        cameracapture12::SetWriteFaultInjection(faults);
        cameracapture12::WriteDiagnostics diagnostics;
        const bool ok = cameracapture12::WriteFileAtomic(
            destination.c_str(), bytes, writeError, &diagnostics);
        clear();
        Check(!ok, "cleanup failure fails the export");
        Check(writeError == cameracapture12::WriteError::PublishFailed,
              "cleanup failure preserves the primary PublishFailed");
        Check(diagnostics.cleanup == cameracapture12::WriteError::CleanupFailed,
              "diagnostics report CleanupFailed");
        Check(!diagnostics.temporaryRemoved,
              "diagnostics report that the temp file was not removed");
        Check(!fs::exists(destination), "cleanup failure leaves no destination");
        Check(tempCount() == 0, "cleanup fault is best-effort removed by the test");
    }

    // A subsequent normal write still succeeds after all injected failures.
    Check(cameracapture12::WriteFileAtomic(destination.c_str(), bytes, writeError),
          "normal write succeeds after fault injection");
    Check(tempCount() == 0, "normal write leaves no temp file");
    fs::remove_all(root, ec);
}

void TestProtectedHandleOwnership(bool includeOverflow)
{
    // Real Win32 proof that CloseHandle can fail while the handle stays valid.
    const fs::path root = fs::temp_directory_path() / "ogsr-b3b1-protect";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const fs::path rawPath = root / "raw.bin";

    HANDLE raw = ::CreateFileW(rawPath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(raw != INVALID_HANDLE_VALUE, "raw handle created");
    if (raw != INVALID_HANDLE_VALUE)
    {
        Check(::SetHandleInformation(raw, HANDLE_FLAG_PROTECT_FROM_CLOSE,
                                     HANDLE_FLAG_PROTECT_FROM_CLOSE) != FALSE,
              "HANDLE_FLAG_PROTECT_FROM_CLOSE set");
        Check(::CloseHandle(raw) == FALSE, "protected CloseHandle fails");
        DWORD flags = 0;
        Check(::GetHandleInformation(raw, &flags) != FALSE,
              "protected handle remains valid after failed close");
        Check(::SetHandleInformation(raw, HANDLE_FLAG_PROTECT_FROM_CLOSE, 0) != FALSE,
              "protection cleared");
        Check(::CloseHandle(raw) != FALSE, "handle closes after protection cleared");
    }

    // Codec path: PROTECT_FROM_CLOSE makes the first CloseHandle fail. The
    // codec must clear the protection, retry the close, and report the primary
    // failure without publishing the destination.
    const CameraCaptureFile12 capture = MakeCapture();
    std::vector<uint8_t> bytes;
    FileError fileError = FileError::None;
    cameracapture12::Encode(capture, bytes, fileError);
    const fs::path destination = root / "camera.bin";

    cameracapture12::WriteFaultInjection faults;
    faults.protectCreatedHandle = true;
    cameracapture12::SetWriteFaultInjection(faults);
    cameracapture12::WriteDiagnostics diagnostics;
    cameracapture12::WriteError writeError = cameracapture12::WriteError::None;
    const bool ok = cameracapture12::WriteFileAtomic(
        destination.c_str(), bytes, writeError, &diagnostics);
    cameracapture12::ClearWriteFaultInjection();

    Check(!ok, "protected output handle fails the export");
    Check(writeError == cameracapture12::WriteError::CloseFailed,
          "protected handle reports CloseFailed");
    Check(diagnostics.handleReleased,
          "codec recovered and released the protected handle");
    Check(diagnostics.handleRecovered,
          "diagnostics report PROTECT_FROM_CLOSE recovery");
    Check(diagnostics.temporaryRemoved,
          "temp file removed after the recovered close");
    Check(diagnostics.cleanup == cameracapture12::WriteError::None,
          "no cleanup failure after the recovered close");
    Check(!fs::exists(destination), "protected handle export creates no destination");
    Check(cameracapture12::LastRetainedHandle() == INVALID_HANDLE_VALUE,
          "recovered handle was not placed in retention");

    // Retention path: make the controlled unprotect fail. The codec must keep
    // the handle in the fixed retention store and must not claim cleanup.
    cameracapture12::WriteFaultInjection retentionFaults;
    retentionFaults.protectCreatedHandle = true;
    retentionFaults.failUnprotect = true;
    cameracapture12::SetWriteFaultInjection(retentionFaults);
    cameracapture12::WriteDiagnostics retentionDiagnostics;
    writeError = cameracapture12::WriteError::None;
    const bool retainedOk = cameracapture12::WriteFileAtomic(
        destination.c_str(), bytes, writeError, &retentionDiagnostics);
    cameracapture12::ClearWriteFaultInjection();

    Check(!retainedOk, "unrecoverable protected handle fails the export");
    Check(writeError == cameracapture12::WriteError::CloseFailed,
          "unrecoverable close keeps CloseFailed as the primary cause");
    Check(!retentionDiagnostics.handleReleased,
          "unrecoverable handle is not reported as released");
    Check(retentionDiagnostics.handleRetained,
          "unrecoverable handle is placed in retention");
    Check(!retentionDiagnostics.temporaryRemoved,
          "locked temp file is not reported as removed");
    Check(retentionDiagnostics.cleanup == cameracapture12::WriteError::CloseFailed,
          "diagnostics report the close cleanup cause");
    Check(!fs::exists(destination),
          "retained handle export creates no destination");

    HANDLE retained = cameracapture12::LastRetainedHandle();
    Check(retained != INVALID_HANDLE_VALUE,
          "retention store exposes the unreleased handle");
    if (retained != INVALID_HANDLE_VALUE)
    {
        DWORD flags = 0;
        Check(::GetHandleInformation(retained, &flags) != FALSE,
              "retained handle is still valid");
        Check(::SetHandleInformation(retained, HANDLE_FLAG_PROTECT_FROM_CLOSE, 0) != FALSE,
              "retained handle protection cleared by the test");
        Check(::CloseHandle(retained) != FALSE,
              "retained handle closed by the test");
    }
    cameracapture12::ResetRetentionForTests();

    // Remove the locked temp file now that the handle is closed.
    for (const auto& entry : fs::directory_iterator(root))
    {
        if (entry.path().filename().wstring().find(L".tmp-") !=
            std::wstring::npos)
            fs::remove(entry.path(), ec);
    }

    if (!includeOverflow)
    {
        fs::remove_all(root, ec);
        return;
    }

    // Retention capacity: four handles are honestly retained, the fifth
    // overflows without a false handleRetained=1, and the sixth is rejected
    // before creating any file (fail-closed latch).
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        const fs::path overflowRoot =
            root / ("overflow" + std::to_string(attempt));
        fs::create_directories(overflowRoot, ec);
        const fs::path overflowDestination = overflowRoot / "camera.bin";

        cameracapture12::WriteFaultInjection overflowFaults;
        overflowFaults.protectCreatedHandle = true;
        overflowFaults.failUnprotect = true;
        cameracapture12::SetWriteFaultInjection(overflowFaults);
        cameracapture12::WriteDiagnostics overflowDiagnostics;
        cameracapture12::WriteError overflowError =
            cameracapture12::WriteError::None;
        const bool overflowOk = cameracapture12::WriteFileAtomic(
            overflowDestination.c_str(), bytes, overflowError,
            &overflowDiagnostics);
        cameracapture12::ClearWriteFaultInjection();

        Check(!overflowOk, "protected overflow attempt fails the export");
        Check(!fs::exists(overflowDestination),
              "protected overflow attempt publishes no destination");
        std::printf("retention_attempt=%d retained=%d released=%d overflow=%d "
                    "store_count=%zu primary=%s cleanup=%s temporary_removed=%d\n",
                    attempt,
                    overflowDiagnostics.handleRetained ? 1 : 0,
                    overflowDiagnostics.handleReleased ? 1 : 0,
                    overflowDiagnostics.retentionOverflow ? 1 : 0,
                    cameracapture12::RetainedHandleCount(),
                    cameracapture12::WriteErrorMessage(overflowError),
                    cameracapture12::WriteErrorMessage(overflowDiagnostics.cleanup),
                    overflowDiagnostics.temporaryRemoved ? 1 : 0);
        if (attempt < 4)
        {
            Check(overflowDiagnostics.handleRetained,
                  "first four unrecoverable handles are retained");
            Check(!overflowDiagnostics.handleReleased,
                  "retained handle is not reported as released");
            Check(!overflowDiagnostics.retentionOverflow,
                  "first four handles do not report overflow");
            Check(cameracapture12::RetainedHandleCount() ==
                      static_cast<size_t>(attempt + 1),
                  "retention count grows to four");
        }
        else
        {
            Check(!overflowDiagnostics.handleRetained,
                  "fifth handle is not falsely reported as retained");
            Check(!overflowDiagnostics.handleReleased,
                  "fifth handle is honestly reported as unreleased");
            Check(overflowDiagnostics.retentionOverflow,
                  "fifth handle reports retention overflow");
            Check(cameracapture12::RetainedHandleCount() == 4,
                  "retention store stays capped at four");
            Check(cameracapture12::RetentionFull(),
                  "retention overflow latches fail-closed");
            Check(cameracapture12::LastUnretainedHandle() !=
                      INVALID_HANDLE_VALUE,
                  "overflow handle is exposed for test cleanup");
        }
    }

    // Sixth export: fail-closed before CreateFile, no new temp file.
    {
        const fs::path blockedRoot = root / "overflow5";
        fs::create_directories(blockedRoot, ec);
        const fs::path blockedDestination = blockedRoot / "camera.bin";
        cameracapture12::WriteFaultInjection blockedFaults;
        blockedFaults.protectCreatedHandle = true;
        blockedFaults.failUnprotect = true;
        cameracapture12::SetWriteFaultInjection(blockedFaults);
        cameracapture12::WriteDiagnostics blockedDiagnostics;
        cameracapture12::WriteError blockedError =
            cameracapture12::WriteError::None;
        const bool blockedOk = cameracapture12::WriteFileAtomic(
            blockedDestination.c_str(), bytes, blockedError,
            &blockedDiagnostics);
        cameracapture12::ClearWriteFaultInjection();
        Check(!blockedOk, "sixth export is rejected after retention overflow");
        std::printf("retention_attempt=5 retained=%d released=%d overflow=%d "
                    "store_count=%zu primary=%s cleanup=%s temporary_removed=%d "
                    "fail_closed=%d\n",
                    blockedDiagnostics.handleRetained ? 1 : 0,
                    blockedDiagnostics.handleReleased ? 1 : 0,
                    blockedDiagnostics.retentionOverflow ? 1 : 0,
                    cameracapture12::RetainedHandleCount(),
                    cameracapture12::WriteErrorMessage(blockedError),
                    cameracapture12::WriteErrorMessage(blockedDiagnostics.cleanup),
                    blockedDiagnostics.temporaryRemoved ? 1 : 0,
                    cameracapture12::RetentionFull() ? 1 : 0);
        Check(blockedError == cameracapture12::WriteError::RetentionFull,
              "sixth export reports RetentionFull");
        Check(blockedDiagnostics.primary ==
                  cameracapture12::WriteError::RetentionFull,
              "diagnostics preserve RetentionFull as the primary cause");
        Check(blockedDiagnostics.retentionOverflow,
              "diagnostics report the fail-closed latch");
        Check(cameracapture12::RetainedHandleCount() == 4,
              "sixth export does not add a retained handle");
        Check(!fs::exists(blockedDestination),
              "sixth export creates no destination");
        Check(CountTempFiles(blockedRoot) == 0,
              "sixth export creates no temp file");
    }

    // Close every handle that the test kept alive, then reset the test hook.
    for (size_t i = 0; i < 4; ++i)
    {
        HANDLE handle = cameracapture12::RetainedHandleAt(i);
        Check(handle != INVALID_HANDLE_VALUE,
              "retained store slot is occupied");
        if (handle != INVALID_HANDLE_VALUE)
        {
            DWORD flags = 0;
            Check(::GetHandleInformation(handle, &flags) != FALSE,
                  "overflow-retained handle is valid");
            Check(::SetHandleInformation(handle, HANDLE_FLAG_PROTECT_FROM_CLOSE,
                                         0) != FALSE,
                  "overflow-retained protection cleared");
            Check(::CloseHandle(handle) != FALSE,
                  "overflow-retained handle closed by the test");
        }
    }
    HANDLE unretained = cameracapture12::LastUnretainedHandle();
    Check(unretained != INVALID_HANDLE_VALUE,
          "overflow handle is available for cleanup");
    if (unretained != INVALID_HANDLE_VALUE)
    {
        DWORD flags = 0;
        Check(::GetHandleInformation(unretained, &flags) != FALSE,
              "overflow handle is valid");
        Check(::SetHandleInformation(unretained,
                                     HANDLE_FLAG_PROTECT_FROM_CLOSE, 0) != FALSE,
              "overflow handle protection cleared");
        Check(::CloseHandle(unretained) != FALSE,
              "overflow handle closed by the test");
    }
    cameracapture12::ResetRetentionForTests();
    Check(cameracapture12::RetainedHandleCount() == 0,
          "retention store reset after overflow test");
    Check(!cameracapture12::RetentionFull(),
          "fail-closed latch reset after overflow test");

    fs::remove_all(root, ec);
}
#endif

void TestRealCaptureFile(const char* path, uint64_t expectedFrame,
                         const char* expectedSha)
{
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
    {
        Check(false, "real capture file opens");
        return;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    const size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    Check(read == bytes.size(), "real capture file read");

    const std::string sha = b3b1::Sha256Hex(bytes);
    std::printf("fixture_path=%s\nfixture_bytes=%zu\nfixture_sha256=%s\n"
                "fixture_expected_sha256=%s\nfixture_expected_frame=%llu\n",
                path, bytes.size(), sha.c_str(),
                expectedSha ? expectedSha : "",
                static_cast<unsigned long long>(expectedFrame));
    if (expectedSha != nullptr && expectedSha[0] != '\0')
        Check(sha == expectedSha, "fixture SHA-256 matches the expected value");

    CameraCaptureFile12 capture{};
    FileError error = FileError::None;
    Check(cameracapture12::Decode(bytes, capture, error),
          "real capture decodes");
    Check(error == FileError::None, "real capture reports no error");
    Check(std::strcmp(capture.levelName, "jupiter") == 0,
          "real capture level is jupiter");
    Check(capture.input.levelSession == 1 && capture.input.captureEpoch == 1 &&
              capture.input.captureSequence == 1,
          "real capture session/epoch/sequence preserved");
    Check(capture.input.sourceGameFrame == expectedFrame,
          "real capture frame matches the expected fixture metadata");
    Check(capture.input.viewportWidth == 2560 &&
              capture.input.viewportHeight == 1600,
          "real capture viewport preserved");

    std::vector<uint8_t> reencoded;
    Check(cameracapture12::Encode(capture, reencoded, error),
          "real capture re-encodes");
    Check(reencoded == bytes, "real capture round-trips byte-exactly");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "--legacy-terminate") == 0)
    {
        // Negative control: the pre-fix pattern (throwing inside a noexcept
        // function) terminates the process. The fixed exporter must never take
        // this path; the parent checks the difference.
        auto legacyNoexceptThrow = []() noexcept { throw std::bad_alloc(); };
        legacyNoexceptThrow();
        return 0;
    }

    const char* fixturePath = nullptr;
    const char* expectedSha = nullptr;
    uint64_t expectedFrame = 0;
    bool haveFrame = false;
    bool includeOverflow = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--fixture") == 0 && i + 1 < argc)
            fixturePath = argv[++i];
        else if (std::strcmp(argv[i], "--expect-frame") == 0 && i + 1 < argc)
        {
            expectedFrame = std::strtoull(argv[++i], nullptr, 10);
            haveFrame = true;
        }
        else if (std::strcmp(argv[i], "--expect-sha") == 0 && i + 1 < argc)
            expectedSha = argv[++i];
        else if (std::strcmp(argv[i], "--retention-overflow") == 0)
            includeOverflow = true;
        else
        {
            std::printf("unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    if (fixturePath != nullptr && !haveFrame)
    {
        std::printf("--fixture requires --expect-frame\n");
        return 2;
    }

    std::printf("B3B1_CAMERA_CAPTURE_CPU_TESTS\n");
    TestRoundTrip(); std::fflush(stdout);
    TestFormatHeader(); std::fflush(stdout);
    TestCorruption(); std::fflush(stdout);
    TestInvalidFields(); std::fflush(stdout);
    TestLevelNames(); std::fflush(stdout);
    TestEncodeRejectsInvalidCamera(); std::fflush(stdout);
    TestExportGate(); std::fflush(stdout);
    TestPublicationLifecycle(); std::fflush(stdout);
    TestDecodeEncodeSymmetry(); std::fflush(stdout);
#ifdef OGSR_CAMERACAPTURE_TEST_HOOKS
    TestWriteFaultInjection(); std::fflush(stdout);
    TestProtectedHandleOwnership(includeOverflow); std::fflush(stdout);
#endif
    TestWriter(); std::fflush(stdout);
    if (fixturePath != nullptr)
    {
        TestRealCaptureFile(fixturePath, expectedFrame, expectedSha);
        std::fflush(stdout);
    }
    std::printf("CAMERA_CAPTURE_SUMMARY checks=%d failures=%d\n", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
