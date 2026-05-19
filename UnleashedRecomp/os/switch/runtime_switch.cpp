#include <cstddef>
#include <switch.h>

extern "C"
{
    u32 __NvOptimusEnablement = 1;
    u32 __NvDeveloperOption = 1;
    u32 __nx_applet_type = AppletType_Application;
    size_t __nx_heap_size = 0;

    alignas(16) u8 __nx_exception_stack[0x4000];
    u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
}