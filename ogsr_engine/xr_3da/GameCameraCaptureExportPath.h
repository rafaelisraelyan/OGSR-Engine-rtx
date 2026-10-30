#pragma once

// B2 camera export option parser.
//
// This header is compiled both by the engine adapter and by the Windows-only
// regression test. MultiByteToWideChar is the real Win32 conversion; there is
// no ASCII test stub in this path.

#include <cstddef>
#include <cstring>
#include <cwchar>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace b2camera_export
{
inline bool IsParameterSpace(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
           value == '\v' || value == '\f';
}

inline bool IsParameterSpaceW(wchar_t value) noexcept
{
    return value == L' ' || value == L'\t' || value == L'\r' ||
           value == L'\n' || value == L'\v' || value == L'\f';
}

constexpr const char* kB3ExportPrefix = "-b3_camera_export=";
constexpr size_t kB3ExportPrefixLength = 18;
constexpr const wchar_t* kB3ExportPrefixW = L"-b3_camera_export=";
constexpr size_t kB3ExportPrefixLengthW = 18;
// Windows extended-length path ceiling. The caller's output buffer is usually
// smaller; both limits are enforced, so an oversized value is never truncated.
constexpr size_t kB3ExportMaxPathChars = 32767;

// Exact "-b3_camera_export=<path>" lookup (optionally quoted). Substring
// occurrences and the bare flag are rejected.
inline bool ExtractB3ExportPath(const char* params, wchar_t* out,
                                size_t outCount) noexcept
{
    if (params == nullptr || out == nullptr || outCount < 2)
        return false;
    const char* cursor = params;
    while (*cursor)
    {
        while (*cursor && IsParameterSpace(*cursor))
            ++cursor;
        if (!*cursor)
            break;
        const char* token = cursor;
        if (*cursor == '"')
        {
            ++cursor;
            token = cursor;
            while (*cursor && *cursor != '"')
                ++cursor;
            if (*cursor != '"')
                return false; // unterminated whole-option quote
        }
        else
        {
            // Handle a quoted value after '=' without splitting on spaces:
            // -b3_camera_export="C:\My Folder\x.bin".
            while (*cursor && !IsParameterSpace(*cursor))
            {
                if (*cursor != '"')
                {
                    ++cursor;
                    continue;
                }
                ++cursor;
                while (*cursor && *cursor != '"')
                    ++cursor;
                if (*cursor != '"')
                    return false; // unterminated quoted value
                ++cursor;
            }
        }
        const size_t length = static_cast<size_t>(cursor - token);
        if (length > kB3ExportPrefixLength &&
            std::strncmp(token, kB3ExportPrefix, kB3ExportPrefixLength) == 0)
        {
            const char* value = token + kB3ExportPrefixLength;
            size_t valueLength = length - kB3ExportPrefixLength;
            if (valueLength >= 2 && value[0] == '"' &&
                value[valueLength - 1] == '"')
            {
                ++value;
                valueLength -= 2;
            }
            else if (valueLength == 0 || value[0] == '"' ||
                     value[valueLength - 1] == '"')
                return false; // malformed or empty argument
            if (valueLength == 0 || valueLength >= outCount)
                return false;
            const int converted = ::MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, value,
                static_cast<int>(valueLength), out,
                static_cast<int>(outCount) - 1);
            if (converted <= 0)
                return false;
            out[converted] = L'\0';
            return true;
        }
        if (*cursor == '"')
            ++cursor;
    }
    return false;
}

// Parse the real Windows command line (GetCommandLineW) for the one-shot
// camera export option. This is deliberately strict: a malformed, duplicated
// or ambiguous export argument disables the export entirely instead of
// silently choosing one interpretation.
//
// Accepted forms:
//   -b3_camera_export=C:\path\x.bin
//   -b3_camera_export="C:\path with spaces\x.bin"
//   "-b3_camera_export=C:\path with spaces\x.bin"
//
// Rejected: bare flag, empty value, unterminated quote, trailing characters
// after the closing quote, embedded quotes, duplicate options and values that
// do not fit the caller-provided buffer.
inline bool ExtractB3ExportPathW(const wchar_t* commandLine, wchar_t* out,
                                 size_t outCount) noexcept
{
    if (commandLine == nullptr || out == nullptr || outCount < 2)
        return false;

    const wchar_t* valueStart = nullptr;
    size_t valueLength = 0;
    bool haveExport = false;

    const wchar_t* cursor = commandLine;
    while (*cursor)
    {
        while (*cursor && IsParameterSpaceW(*cursor))
            ++cursor;
        if (!*cursor)
            break;

        const wchar_t* token = nullptr;
        const wchar_t* tokenEnd = nullptr;
        bool wholeOptionQuoted = false;

        if (*cursor == L'"')
        {
            wholeOptionQuoted = true;
            token = ++cursor;
            while (*cursor && *cursor != L'"')
                ++cursor;
            if (*cursor != L'"')
                return false; // unterminated whole-option quote
            tokenEnd = cursor;
            ++cursor;
            // CommandLineToArgvW merges text after a closing quote into the
            // same argument ("C:\good.bin"tail -> C:\good.bintail). That is
            // ambiguous for an export path, so reject it explicitly.
            if (*cursor && !IsParameterSpaceW(*cursor))
                return false;
        }
        else
        {
            token = cursor;
            while (*cursor && !IsParameterSpaceW(*cursor))
            {
                if (*cursor == L'"')
                {
                    ++cursor;
                    while (*cursor && *cursor != L'"')
                        ++cursor;
                    if (*cursor != L'"')
                        return false; // unterminated quoted value
                    ++cursor;
                    continue;
                }
                ++cursor;
            }
            tokenEnd = cursor;
        }

        const size_t tokenLength = static_cast<size_t>(tokenEnd - token);
        bool exportCandidate = false;
        const wchar_t* candidateValue = nullptr;
        size_t candidateLength = 0;

        if (tokenLength >= kB3ExportPrefixLengthW &&
            std::wcsncmp(token, kB3ExportPrefixW, kB3ExportPrefixLengthW) == 0)
        {
            exportCandidate = true;
            candidateValue = token + kB3ExportPrefixLengthW;
            candidateLength = tokenLength - kB3ExportPrefixLengthW;
        }
        else if (tokenLength == kB3ExportPrefixLengthW - 1 &&
                 std::wcsncmp(token, L"-b3_camera_export",
                              kB3ExportPrefixLengthW - 1) == 0)
        {
            return false; // bare flag is a malformed export request
        }

        if (!exportCandidate)
            continue;

        // Fail closed on duplicates: either one unambiguous export or none.
        if (haveExport)
            return false;

        if (!wholeOptionQuoted && candidateLength > 0 &&
            candidateValue[0] == L'"')
        {
            if (candidateLength < 2 ||
                candidateValue[candidateLength - 1] != L'"')
                return false;
            // No text may follow the closing quote and no quote may appear
            // inside the value.
            for (size_t i = 1; i + 1 < candidateLength; ++i)
                if (candidateValue[i] == L'"')
                    return false;
            ++candidateValue;
            candidateLength -= 2;
        }
        else
        {
            // An unquoted value must not contain quote characters at all.
            for (size_t i = 0; i < candidateLength; ++i)
                if (candidateValue[i] == L'"')
                    return false;
        }

        if (candidateLength == 0 || candidateLength >= outCount ||
            candidateLength >= kB3ExportMaxPathChars)
            return false;

        valueStart = candidateValue;
        valueLength = candidateLength;
        haveExport = true;
    }

    if (!haveExport || valueLength == 0 || valueLength >= outCount)
        return false;

    std::wmemcpy(out, valueStart, valueLength);
    out[valueLength] = L'\0';
    return true;
}
} // namespace b2camera_export
