#include "dx12log.h"

#include <Windows.h>

namespace
{
    FILE* g_log = nullptr;
    wchar_t g_logPath[MAX_PATH] = {};

    void Timestamp(char* buf, size_t bufSize)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        _snprintf_s(buf, bufSize, _TRUNCATE, "%04u-%02u-%02u %02u:%02u:%02u.%03u",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    }
}

namespace dx12log
{
    bool Open()
    {
        Close();

        wchar_t modulePath[MAX_PATH] = {};
        const DWORD len = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            return false;

        wchar_t* slash = wcsrchr(modulePath, L'\\');
        if (!slash)
            return false;

        // Лог кладётся рядом с exe: x64\ReleaseDX12\xrDX12Smoke.log
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - modulePath), L"xrDX12Smoke.log");
        wcscpy_s(g_logPath, MAX_PATH, modulePath);

        // Обычный бинарный режим. Прежний режим "w, ccs=UTF-8" включал
        // Unicode-oriented преобразование CRT, несовместимое с нашим
        // смешением fprintf (Write) и ручным UTF-16 -> UTF-8 (WriteWide).
        // В "wb" обе функции пишут одинаковые UTF-8/ASCII байты, и
        // wide/narrow операции на одном CRT stream не смешиваются.
        const errno_t err = _wfopen_s(&g_log, modulePath, L"wb");
        if (err != 0 || !g_log)
        {
            g_logPath[0] = L'\0';
            return false;
        }

        Write("=== xrDX12Smoke log opened ===");
        return true;
    }

    void Close()
    {
        if (g_log)
        {
            fclose(g_log);
            g_log = nullptr;
        }
    }

    void Write(const char* fmt, ...)
    {
        if (!g_log)
            return;

        char ts[32] = {};
        Timestamp(ts, sizeof(ts));
        fprintf(g_log, "[%s] ", ts);

        va_list args;
        va_start(args, fmt);
        vfprintf(g_log, fmt, args);
        va_end(args);

        fputc('\n', g_log);
        fflush(g_log);
    }

    void WriteWide(const wchar_t* fmt, ...)
    {
        if (!g_log)
            return;

        wchar_t wide[1024] = {};
        va_list args;
        va_start(args, fmt);
        _vsnwprintf_s(wide, _TRUNCATE, fmt, args);
        va_end(args);

        // UTF-16 -> UTF-8: описания GPU содержат только ASCII/латиницу,
        // но WideCharToMultiByte покрывает и локализованные имена адаптеров.
        char utf8[2048] = {};
        const int written = WideCharToMultiByte(CP_UTF8, 0, wide, -1,
                                                utf8, sizeof(utf8) - 1, nullptr, nullptr);
        if (written <= 0)
            return;

        char ts[32] = {};
        Timestamp(ts, sizeof(ts));
        fprintf(g_log, "[%s] %s\n", ts, utf8);
        fflush(g_log);
    }

    const wchar_t* Path()
    {
        return g_logPath;
    }
}
