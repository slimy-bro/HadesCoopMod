//
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

#include "pch.h"

#include "OnlineLog.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace OnlineLog {

static std::mutex g_mutex;
static FILE *g_file = nullptr;

void Open(const std::wstring &path) {
    std::lock_guard lock(g_mutex);
    if (g_file)
        return;

    if (_wfopen_s(&g_file, path.c_str(), L"w") != 0) {
        // The game folder can be read only
        g_file = nullptr;
        wchar_t tempPath[MAX_PATH] = {};
        if (GetTempPathW(MAX_PATH, tempPath) &&
            _wfopen_s(&g_file, (std::wstring(tempPath) + L"HadesCoopOnline.log").c_str(), L"w") != 0)
            g_file = nullptr;
    }
}

void Write(const char *format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    SYSTEMTIME time;
    GetLocalTime(&time);

    std::lock_guard lock(g_mutex);
    if (!g_file)
        return;

    fprintf(g_file, "[%02u:%02u:%02u.%03u] %s\n", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, message);
    fflush(g_file);
}

} // namespace OnlineLog
