#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

#include <fmt/core.h>
#include <os/logger.h>

namespace
{
    constexpr const char* kLogDirectory = "sdmc:/switch/UnleashedRecomp";
    constexpr const char* kLogPath = "sdmc:/switch/UnleashedRecomp/UnleashedRecomp.log";
    constexpr uint32_t kEarlyFlushLines = 96;
    constexpr uint32_t kFlushInterval = 64;

    std::mutex g_logMutex;
    FILE* g_logFile = nullptr;
    uint32_t g_logLineCount = 0;

    std::string FormatLogLine(std::string_view str, os::logger::ELogType type, const char* func)
    {
        const char* prefix = "";

        switch (type)
        {
        case os::logger::ELogType::Utility:
            prefix = "utility";
            break;
        case os::logger::ELogType::Warning:
            prefix = "warning";
            break;
        case os::logger::ELogType::Error:
            prefix = "error";
            break;
        default:
            break;
        }

        if (func != nullptr && prefix[0] != '\0')
            return fmt::format("[{}] [{}] {}", func, prefix, str);
        if (func != nullptr)
            return fmt::format("[{}] {}", func, str);
        if (prefix[0] != '\0')
            return fmt::format("[{}] {}", prefix, str);

        return std::string(str);
    }
}

void os::logger::Init()
{
    std::lock_guard lock(g_logMutex);

    if (g_logFile != nullptr)
    {
        fclose(g_logFile);
        g_logFile = nullptr;
    }

    g_logLineCount = 0;

    std::error_code ec;
    std::filesystem::create_directories(kLogDirectory, ec);

    g_logFile = fopen(kLogPath, "w");
    if (g_logFile != nullptr)
        setvbuf(g_logFile, nullptr, _IOFBF, 64 * 1024);
}

void os::logger::Log(const std::string_view str, ELogType type, const char* func)
{
    const auto line = FormatLogLine(str, type, func);
    std::lock_guard lock(g_logMutex);

    if (g_logFile != nullptr)
    {
        fputs(line.c_str(), g_logFile);
        fputc('\n', g_logFile);

        ++g_logLineCount;

        if (type == ELogType::Error ||
            type == ELogType::Warning ||
            g_logLineCount <= kEarlyFlushLines ||
            (g_logLineCount % kFlushInterval) == 0)
        {
            fflush(g_logFile);
        }
    }
}
