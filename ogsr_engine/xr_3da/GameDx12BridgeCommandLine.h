#pragma once

// F4.0-G1B Round 2: exact Windows command-line argument matching.
//
// Core.Params is the full GetCommandLine() string, including the executable
// path. This parser implements the documented Windows CommandLineToArgvW
// quoting rules for the narrow (ANSI) string used by the engine:
//   - space and tab separate arguments outside quotes;
//   - a quote toggles quoted mode and may appear mid-argument;
//   - 2N backslashes before a quote produce N backslashes and toggle quoting;
//   - 2N+1 backslashes before a quote produce N backslashes and a literal quote;
//   - backslashes not immediately before a quote are literal;
//   - an unclosed quote makes the whole command line ambiguous and is rejected.
//
// The function compares complete argument values, never substrings. It is
// header-only so the MSVC probe can compile and test this exact production
// implementation without duplicating it.

#include <cstddef>
#include <cstring>

inline bool GameDx12Bridge_HasExactCommandLineFlag(
    const char* commandLine, const char* flag, const char** argumentStart = nullptr) noexcept
{
    if (argumentStart != nullptr)
        *argumentStart = nullptr;
    if (commandLine == nullptr || flag == nullptr || flag[0] == '\0')
        return false;

    const std::size_t flagLength = std::strlen(flag);
    char token[1024] = {};
    std::size_t tokenLength = 0;
    bool tokenStarted = false;
    bool tokenOverflow = false;
    bool inQuotes = false;
    const char* tokenStart = commandLine;
    const char* foundFlag = nullptr;

    const auto append = [&](char value) {
        tokenStarted = true;
        if (tokenLength + 1 < sizeof(token))
            token[tokenLength++] = value;
        else
            tokenOverflow = true;
    };

    const auto finishToken = [&]() -> bool {
        const bool match = tokenStarted && !tokenOverflow &&
                           tokenLength == flagLength &&
                           std::memcmp(token, flag, flagLength) == 0;
        tokenLength = 0;
        tokenStarted = false;
        tokenOverflow = false;
        return match;
    };

    std::size_t index = 0;
    while (commandLine[index] != '\0')
    {
        const char value = commandLine[index];

        if (!inQuotes && (value == ' ' || value == '\t'))
        {
            if (finishToken())
                foundFlag = foundFlag == nullptr ? tokenStart : foundFlag;
            ++index;
            tokenStart = commandLine + index;
            continue;
        }

        if (value == '\\')
        {
            std::size_t end = index;
            while (commandLine[end] == '\\')
                ++end;
            const std::size_t count = end - index;
            const char next = commandLine[end];

            if (next == '"')
            {
                const std::size_t literalBackslashes = count / 2;
                for (std::size_t i = 0; i < literalBackslashes; ++i)
                    append('\\');

                if ((count & 1u) != 0u)
                {
                    append('"');
                }
                else
                {
                    inQuotes = !inQuotes;
                    tokenStarted = true;
                }
                index = end + 1;
            }
            else
            {
                for (std::size_t i = 0; i < count; ++i)
                    append('\\');
                index = end;
            }
            continue;
        }

        if (value == '"')
        {
            inQuotes = !inQuotes;
            tokenStarted = true;
            ++index;
            continue;
        }

        append(value);
        ++index;
    }

    if (inQuotes)
        return false;
    if (finishToken())
        foundFlag = foundFlag == nullptr ? tokenStart : foundFlag;
    if (argumentStart != nullptr)
        *argumentStart = foundFlag;
    return foundFlag != nullptr;
}

inline const char* GameDx12Bridge_NativeStartupCommand(const char* commandLine) noexcept
{
#ifdef OGSR_ENABLE_EXPERIMENTAL_DX12_GAME_BRIDGE
    const char* start = nullptr;
    if (!GameDx12Bridge_HasExactCommandLineFlag(commandLine, "-dx12_native_game") ||
        GameDx12Bridge_HasExactCommandLineFlag(commandLine, "-dx12_game_bridge") ||
        !GameDx12Bridge_HasExactCommandLineFlag(commandLine, "-start", &start) ||
        std::strncmp(start, "-start ", 7) != 0)
        return nullptr;

    const char* target = start + 7;
    while (*target == ' ')
        ++target;
    if (std::strncmp(target, "server(", 7) != 0)
        return nullptr;
    target += 7;
    const char* end = std::strchr(target, ')');
    if (end == nullptr || end == target || std::strcspn(target, " \t\"(") < static_cast<std::size_t>(end - target))
        return nullptr;
    return start + 1;
#else
    (void)commandLine;
    return nullptr;
#endif
}

inline bool GameDx12Bridge_NativeRequested(const char* commandLine) noexcept
{
    return GameDx12Bridge_NativeStartupCommand(commandLine) != nullptr;
}
