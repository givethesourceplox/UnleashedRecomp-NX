#include <os/version.h>

#include <switch.h>

os::version::OSVersion os::version::GetOSVersion()
{
    const u32 version = hosversionGet();

    return
    {
        HOSVER_MAJOR(version),
        HOSVER_MINOR(version),
        HOSVER_MICRO(version)
    };
}
