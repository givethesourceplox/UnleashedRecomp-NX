#include <os/version.h>

#include <switch.h>

os::version::OSVersion os::version::GetOSVersion()
{
    return
    {
        HOSVER_MAJOR(version),
        HOSVER_MINOR(version),
        HOSVER_MICRO(version)
    };
}