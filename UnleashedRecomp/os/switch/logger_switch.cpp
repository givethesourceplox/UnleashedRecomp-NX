#include <os/logger.h>

void os::logger::Init()
{
}

void os::logger::Log(const std::string_view str, ELogType type, const char* func)
{
    const char* prefix = "";

    switch (type)
    {
    case ELogType::Utility:
        prefix = "utility";
        break;
    case ELogType::Warning:
        prefix = "warning";
        break;
    case ELogType::Error:
        prefix = "error";
        break;
    default:
        break;
    }

    if (func != nullptr && prefix[0] != '\0')
        fmt::println("[{}] [{}] {}", func, prefix, str);
    else if (func != nullptr)
        fmt::println("[{}] {}", func, str);
    else if (prefix[0] != '\0')
        fmt::println("[{}] {}", prefix, str);
    else
        fmt::println("{}", str);
}