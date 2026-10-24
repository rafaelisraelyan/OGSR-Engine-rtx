#pragma once

// Narrow CPU-only helpers shared by the B2.3 adapter and its isolated tests.
// This header has no Engine, Device or D3D dependencies beyond the B2 CPU DTOs.
#include "../Layers/xrRenderDX12/dx12gamecamera.h"

namespace b2camera_diagnostics
{
inline bool IsArgumentSpace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
           value == '\v' || value == '\f';
}

inline bool MatchesUnquotedArgument(const char* argument,
                                    const char* expected) noexcept
{
    while (*argument && !IsArgumentSpace(*argument))
    {
        if (*expected == '\0' || *argument != *expected)
            return false;
        ++argument;
        ++expected;
    }
    return *expected == '\0';
}

inline bool MatchesQuotedArgument(const char* argument,
                                  const char* expected) noexcept
{
    // Supports a complete quoted token ("-b2_camera_capture") with optional
    // surrounding whitespace. A suffix after the closing quote is not accepted.
    ++argument; // opening quote
    while (*argument && *argument != '"')
    {
        if (*expected == '\0' || *argument != *expected)
            return false;
        ++argument;
        ++expected;
    }
    if (*argument != '"' || *expected != '\0')
        return false;
    ++argument;
    return *argument == '\0' || IsArgumentSpace(*argument);
}

inline void SkipArgument(const char*& argument) noexcept
{
    if (*argument == '"')
    {
        ++argument;
        while (*argument && *argument != '"')
            ++argument;
        if (*argument == '"')
            ++argument;
    }
    while (*argument && !IsArgumentSpace(*argument))
        ++argument;
}

// Exact, allocation-free command-line argument lookup. It deliberately does
// not use substring search: "-b2_camera_capture_disabled", "=0", quoted
// unrelated arguments and embedded occurrences never match.
inline bool HasExactArgument(const char* params, const char* expected) noexcept
{
    if (!params || !expected || !expected[0])
        return false;

    const char* cursor = params;
    while (*cursor)
    {
        while (*cursor && IsArgumentSpace(*cursor))
            ++cursor;
        if (!*cursor)
            break;

        if (*cursor == '"')
        {
            if (MatchesQuotedArgument(cursor, expected))
                return true;
            SkipArgument(cursor);
        }
        else
        {
            if (MatchesUnquotedArgument(cursor, expected))
                return true;
            SkipArgument(cursor);
        }
    }
    return false;
}

inline gamecamera12::CameraError12 SelectCaptureError(
    const gamecamera12::CameraCaptureResult12& result) noexcept
{
    // Preserve a concrete validator error. InvalidCamera is only the
    // contract-violation fallback for an unexpected error=None/empty-camera
    // combination.
    return result.error != gamecamera12::CameraError12::None
               ? result.error
               : gamecamera12::CameraError12::InvalidCamera;
}

// The gate owns tiny value copies only. It never stores Device, Fmatrix or
// stack pointers. The matrix input is retained until a successful publish of
// exactly the token produced by that validation.
class MatrixDumpGate
{
public:
    void Reset(bool requested) noexcept
    {
        requested_ = requested;
        dumped_ = false;
        pending_ = false;
        input_ = {};
        token_ = {};
    }

    void OnCapture(const gamecamera12::CameraCaptureInput12& input,
                   const gamecamera12::CaptureToken12& token) noexcept
    {
        input_ = input;
        token_ = token;
        pending_ = true;
    }

    void ClearPending() noexcept { pending_ = false; }

    bool OnPublished(const gamecamera12::CaptureToken12& token) noexcept
    {
        if (!pending_ || token != token_)
        {
            pending_ = false;
            return false;
        }

        pending_ = false;
        if (!requested_ || dumped_)
            return false;

        dumped_ = true;
        return true;
    }

    const gamecamera12::CameraCaptureInput12& Input() const noexcept
    {
        return input_;
    }

    bool Dumped() const noexcept { return dumped_; }

private:
    bool requested_ = false;
    bool dumped_ = false;
    bool pending_ = false;
    gamecamera12::CameraCaptureInput12 input_{};
    gamecamera12::CaptureToken12 token_{};
};
} // namespace b2camera_diagnostics
