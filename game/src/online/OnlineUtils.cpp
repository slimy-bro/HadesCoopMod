//
// Licensed under the MIT license. See LICENSE file in the project root for details.
//

#include "pch.h"

#include "OnlineUtils.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace OnlineUtils {

std::wstring GetModuleDirectory() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&GetModuleDirectory), &module);

    wchar_t path[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return L"";

    std::wstring directory(path, length);
    size_t separator = directory.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return L"";

    return directory.substr(0, separator + 1);
}

std::string ToUtf8(const std::wstring &text) {
    if (text.empty())
        return {};

    int size =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

uint64_t GetTimeUs() {
    static const uint64_t frequency = [] {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return static_cast<uint64_t>(value.QuadPart);
    }();

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    uint64_t ticks = static_cast<uint64_t>(counter.QuadPart);
    return ticks / frequency * 1000000 + ticks % frequency * 1000000 / frequency;
}

uint32_t ReadConfigNumber(const std::wstring &path, const char *key, uint32_t defaultValue) {
    std::ifstream file{std::filesystem::path(path)};
    if (!file)
        return defaultValue;

    const size_t keyLength = std::strlen(key);
    std::string line;
    while (std::getline(file, line)) {
        size_t comment = line.find("--");
        if (comment != std::string::npos)
            line.resize(comment);

        size_t position = line.find(key);
        if (position == std::string::npos)
            continue;

        // Whole word only
        if (position > 0 && (std::isalnum(static_cast<unsigned char>(line[position - 1])) || line[position - 1] == '_'))
            continue;

        position += keyLength;
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])))
            position++;
        if (position >= line.size() || line[position] != '=')
            continue;
        position++;
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])))
            position++;

        const char *start = line.c_str() + position;
        char *end = nullptr;
        unsigned long value = std::strtoul(start, &end, 10);
        if (end != start)
            return static_cast<uint32_t>(value);
    }

    return defaultValue;
}

uint64_t ParseConnectLobby(const char *commandLine) {
    if (!commandLine)
        return 0;

    static const char ARGUMENT[] = "+connect_lobby";
    const char *position = std::strstr(commandLine, ARGUMENT);
    if (!position)
        return 0;

    position += sizeof(ARGUMENT) - 1;
    while (*position == ' ' || *position == '"')
        position++;

    return std::strtoull(position, nullptr, 10);
}

} // namespace OnlineUtils
