#include <os/process.h>

namespace
{
    constexpr const char* SWITCH_APP_ROOT = "sdmc:/switch/UnleashedRecomp";
    constexpr const char* SWITCH_APP_PATH = "sdmc:/switch/UnleashedRecomp/UnleashedRecomp.nro";
}

std::filesystem::path os::process::GetExecutablePath()
{
    return SWITCH_APP_PATH;
}

std::filesystem::path os::process::GetExecutableRoot()
{
    return SWITCH_APP_ROOT;
}

std::filesystem::path os::process::GetWorkingDirectory()
{
    std::error_code ec;
    auto path = std::filesystem::current_path(ec);
    return ec ? GetExecutableRoot() : path;
}

bool os::process::SetWorkingDirectory(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::current_path(path, ec);
    return !ec;
}

bool os::process::StartProcess(const std::filesystem::path& path, const std::vector<std::string>& args, std::filesystem::path work)
{
    (void)path;
    (void)args;
    (void)work;
    return false;
}

void os::process::CheckConsole()
{
    g_consoleVisible = true;
}

void os::process::ShowConsole()
{
    g_consoleVisible = true;
}