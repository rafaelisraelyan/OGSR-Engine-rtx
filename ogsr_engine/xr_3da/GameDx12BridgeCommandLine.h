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
    const char* commandLine, const char* flag) noexcept
{
    if (commandLine == nullptr || flag == nullptr || flag[0] == '\0')
        return false;

    const std::size_t flagLength = std::strlen(flag);
    char token[1024] = {};
    std::size_t tokenLength = 0;
    bool tokenStarted = false;
    bool tokenOverflow = false;
    bool inQuotes = false;
    bool foundFlag = false;

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
                foundFlag = true;
            ++index;
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
        foundFlag = true;
    return foundFlag;
}
