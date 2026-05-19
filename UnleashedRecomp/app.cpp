#include "app.h"
#include <api/SWA.h>
#include <gpu/video.h>
#include <install/installer.h>
#include <kernel/function.h>
#include <os/logger.h>
#include <os/process.h>
#include <patches/audio_patches.h>
#include <patches/inspire_patches.h>
#include <ui/game_window.h>
#include <user/config.h>
#include <user/paths.h>
#include <user/registry.h>

#include <atomic>

#if defined(__SWITCH__)
static void LogSwitchBootTrace(const char* name, const char* phase, PPCContext& ctx)
{
    LOGFN("Switch boot trace: {} {} r1=0x{:08X} r3=0x{:08X} r4=0x{:08X}",
        name,
        phase,
        ctx.r1.u32,
        ctx.r3.u32,
        ctx.r4.u32);
}

static bool ShouldLogSwitchFrameTrace()
{
    static std::atomic<uint32_t> count = 0;
    const auto value = count.fetch_add(1, std::memory_order_relaxed) + 1;
    return value <= 4 || (value % 300) == 0;
}

static bool IsSwitchGuestRange(uint32_t address, uint32_t size)
{
    return address != 0 && size <= PPC_MEMORY_SIZE && address <= PPC_MEMORY_SIZE - size;
}

static uint32_t LoadSwitchGuestU32(uint8_t* base, uint32_t address)
{
    return IsSwitchGuestRange(address, sizeof(uint32_t)) ? PPC_LOAD_U32(address) : 0;
}

static uint32_t LoadSwitchEventHandle(uint8_t* base, uint32_t object)
{
    return IsSwitchGuestRange(object, 8) ? PPC_LOAD_U32(object + 4) : 0;
}

static uint32_t GetSwitchVirtualTarget(uint8_t* base, uint32_t object, uint32_t vtableOffset)
{
    const uint32_t vtable = LoadSwitchGuestU32(base, object);
    if (vtable == 0 || !IsSwitchGuestRange(vtable, vtableOffset + sizeof(uint32_t)))
        return 0;

    return PPC_LOAD_U32(vtable + vtableOffset);
}
#endif

void App::Restart(std::vector<std::string> restartArgs)
{
    os::process::StartProcess(os::process::GetExecutablePath(), restartArgs, os::process::GetWorkingDirectory());
    Exit();
}

void App::Exit()
{
    Config::Save();

#ifdef _WIN32
    timeEndPeriod(1);
#endif

    std::_Exit(0);
}

// SWA::CApplication::CApplication
PPC_FUNC_IMPL(__imp__sub_824EB490);
PPC_FUNC(sub_824EB490)
{
#if defined(__SWITCH__)
    LOGN("Switch guest CApplication constructor reached");
#endif

    App::s_isInit = true;
    App::s_isMissingDLC = !Installer::checkAllDLC(GetGamePath());
    App::s_language = Config::Language;

    SWA::SGlobals::Init();
    Registry::Save();

    __imp__sub_824EB490(ctx, base);

#if defined(__SWITCH__)
    LOGN("Switch guest CApplication constructor returned");
#endif
}

#if defined(__SWITCH__)
// XEX entry point.
PPC_FUNC_IMPL(__imp___xstart);
PPC_FUNC(_xstart)
{
    LogSwitchBootTrace("_xstart", "enter", ctx);
    __imp___xstart(ctx, base);
    LogSwitchBootTrace("_xstart", "leave", ctx);
}

// Game bootstrap before the main application loop.
PPC_FUNC_IMPL(__imp__sub_822C3AC0);
PPC_FUNC(sub_822C3AC0)
{
    LogSwitchBootTrace("sub_822C3AC0 bootstrap", "enter", ctx);
    __imp__sub_822C3AC0(ctx, base);
    LogSwitchBootTrace("sub_822C3AC0 bootstrap", "leave", ctx);
}

// SWA::CApplication construction and early renderer setup.
PPC_FUNC_IMPL(__imp__sub_822C1558);
PPC_FUNC(sub_822C1558)
{
    LogSwitchBootTrace("sub_822C1558 app-init", "enter", ctx);
    __imp__sub_822C1558(ctx, base);
    LogSwitchBootTrace("sub_822C1558 app-init", "leave", ctx);
}

// Init helper called from app construction before renderer bootstrap.
PPC_FUNC_IMPL(__imp__sub_822C1328);
PPC_FUNC(sub_822C1328)
{
    LogSwitchBootTrace("sub_822C1328 app-init-helper", "enter", ctx);
    __imp__sub_822C1328(ctx, base);
    LogSwitchBootTrace("sub_822C1328 app-init-helper", "leave", ctx);
}

// Renderer/window bootstrap called just before the main loop.
PPC_FUNC_IMPL(__imp__sub_824EB730);
PPC_FUNC(sub_824EB730)
{
    LogSwitchBootTrace("sub_824EB730 render-bootstrap", "enter", ctx);
    __imp__sub_824EB730(ctx, base);
    LogSwitchBootTrace("sub_824EB730 render-bootstrap", "leave", ctx);
}

// Main application loop. This normally does not return.
PPC_FUNC_IMPL(__imp__sub_822C0E18);
PPC_FUNC(sub_822C0E18)
{
    LogSwitchBootTrace("sub_822C0E18 main-loop", "enter", ctx);
    __imp__sub_822C0E18(ctx, base);
    LogSwitchBootTrace("sub_822C0E18 main-loop", "leave", ctx);
}

// Called once per app loop iteration after the update vcall.
PPC_FUNC_IMPL(__imp__sub_824EB8F0);
PPC_FUNC(sub_824EB8F0)
{
    if (ShouldLogSwitchFrameTrace())
        LOGFN("Switch boot trace: sub_824EB8F0 frame tick f1={:.6f}", ctx.f1.f64);

    __imp__sub_824EB8F0(ctx, base);
}

#define SWITCH_BOOT_TRACE_WRAPPER(func, label) \
PPC_FUNC_IMPL(__imp__##func); \
PPC_FUNC(func) \
{ \
    static std::atomic<uint32_t> callCount = 0; \
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1; \
    const bool shouldLog = callIndex <= 64; \
    if (shouldLog) \
        LOGFN("Switch boot trace: {} enter#{} r1=0x{:08X} r3=0x{:08X} r4=0x{:08X}", label, callIndex, ctx.r1.u32, ctx.r3.u32, ctx.r4.u32); \
    __imp__##func(ctx, base); \
    if (shouldLog) \
        LOGFN("Switch boot trace: {} leave#{} r1=0x{:08X} r3=0x{:08X} r4=0x{:08X}", label, callIndex, ctx.r1.u32, ctx.r3.u32, ctx.r4.u32); \
}

SWITCH_BOOT_TRACE_WRAPPER(sub_82FE0258, "sub_82FE0258 render global init")
SWITCH_BOOT_TRACE_WRAPPER(sub_82DFB728, "sub_82DFB728 path/string construct")
SWITCH_BOOT_TRACE_WRAPPER(sub_82DFC788, "sub_82DFC788 path/string append")
SWITCH_BOOT_TRACE_WRAPPER(sub_82DFB148, "sub_82DFB148 path/string destroy")
SWITCH_BOOT_TRACE_WRAPPER(sub_82DFAED0, "sub_82DFAED0 path/string cstr")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E59F78, "sub_82E59F78 memory/system init")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E86708, "sub_82E86708 resource init")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EB2D8, "sub_824EB2D8 render helper init")
SWITCH_BOOT_TRACE_WRAPPER(sub_82DFA0B0, "sub_82DFA0B0 alloc")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F2FE0, "sub_824F2FE0 app document ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F6ED0, "sub_824F6ED0 app document init")
SWITCH_BOOT_TRACE_WRAPPER(sub_82519918, "sub_82519918 app mode/config init")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F5EB8, "sub_824F5EB8 app document startup")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F4508, "sub_824F4508 object release")
SWITCH_BOOT_TRACE_WRAPPER(sub_82DFA0A0, "sub_82DFA0A0 free")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EC550, "sub_824EC550 renderer object init")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EB430, "sub_824EB430 render singleton init")
SWITCH_BOOT_TRACE_WRAPPER(sub_82AE29B8, "sub_82AE29B8 render singleton ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_831B0E98, "sub_831B0E98 atexit/register")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EB3E0, "sub_824EB3E0 render singleton get")
SWITCH_BOOT_TRACE_WRAPPER(sub_82DF98C0, "sub_82DF98C0 smart pointer assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_82AE2608, "sub_82AE2608 set resolution")
SWITCH_BOOT_TRACE_WRAPPER(sub_82DF9958, "sub_82DF9958 smart pointer release")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EB338, "sub_824EB338 render helper finalize")
SWITCH_BOOT_TRACE_WRAPPER(sub_822E9FF8, "sub_822E9FF8 document worker thread")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E01860, "sub_82E01860 document task execute")
SWITCH_BOOT_TRACE_WRAPPER(sub_82DFD788, "sub_82DFD788 task dispatch")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E6E2A8, "sub_82E6E2A8 resource helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E6BD90, "sub_82E6BD90 resource helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E6B830, "sub_82E6B830 resource helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E6BCE0, "sub_82E6BCE0 resource helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E6C520, "sub_82E6C520 resource helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E6D7D0, "sub_82E6D7D0 resource helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E09BF8, "sub_82E09BF8 resource helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_82B85970, "sub_82B85970 startup option bind")
SWITCH_BOOT_TRACE_WRAPPER(sub_82B9BA88, "sub_82B9BA88 startup option finalize")
SWITCH_BOOT_TRACE_WRAPPER(sub_8257BCF0, "sub_8257BCF0 startup ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F3570, "sub_824F3570 startup assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_82B281D8, "sub_82B281D8 startup attach")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F35E0, "sub_824F35E0 startup assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_82B286E0, "sub_82B286E0 startup attach")
SWITCH_BOOT_TRACE_WRAPPER(sub_82B27D88, "sub_82B27D88 startup ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F3650, "sub_824F3650 startup assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_82B27DA8, "sub_82B27DA8 startup attach")
SWITCH_BOOT_TRACE_WRAPPER(sub_82520328, "sub_82520328 startup ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF718, "sub_824EF718 startup assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_825204D8, "sub_825204D8 startup attach")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF788, "sub_824EF788 startup assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_824B77D8, "sub_824B77D8 startup ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F36C0, "sub_824F36C0 startup assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_824B7BB0, "sub_824B7BB0 startup attach")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E535D0, "sub_82E535D0 startup wait/fallback")
SWITCH_BOOT_TRACE_WRAPPER(sub_82426318, "sub_82426318 startup final attach")
SWITCH_BOOT_TRACE_WRAPPER(sub_824ED938, "sub_824ED938 startup tail")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF0C8, "sub_824EF0C8 startup tail")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF140, "sub_824EF140 startup tail")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EE500, "sub_824EE500 startup tail")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF390, "sub_824EF390 startup helper")

#undef SWITCH_BOOT_TRACE_WRAPPER

PPC_FUNC_IMPL(__imp__sub_82456270);
PPC_FUNC(sub_82456270)
{
    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = callIndex <= 16;

    uint32_t table = 0;
    uint32_t target = 0;
    if (ctx.r3.u32 != 0 && ctx.r3.u32 < PPC_MEMORY_SIZE - 8)
    {
        table = PPC_LOAD_U32(ctx.r3.u32);
        if (table != 0 && table < PPC_MEMORY_SIZE - 8)
            target = PPC_LOAD_U32(table + 4);
    }

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_82456270 task virtual dispatch enter#{} r1=0x{:08X} r3=0x{:08X} table=0x{:08X} target=0x{:08X}",
            callIndex,
            ctx.r1.u32,
            ctx.r3.u32,
            table,
            target);
    }

    __imp__sub_82456270(ctx, base);

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_82456270 task virtual dispatch leave#{} r1=0x{:08X} r3=0x{:08X}",
            callIndex,
            ctx.r1.u32,
            ctx.r3.u32);
    }
}

PPC_FUNC_IMPL(__imp__sub_830C9F68);
PPC_FUNC(sub_830C9F68)
{
    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = callIndex <= 32;

    uint32_t target = 0;
    uint32_t argument = 0;
    if (ctx.r3.u32 != 0 && ctx.r3.u32 < PPC_MEMORY_SIZE - 8)
    {
        target = PPC_LOAD_U32(ctx.r3.u32 + 0);
        argument = PPC_LOAD_U32(ctx.r3.u32 + 4);
    }

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_830C9F68 task thunk enter#{} r1=0x{:08X} thunk=0x{:08X} target=0x{:08X} arg=0x{:08X}",
            callIndex,
            ctx.r1.u32,
            ctx.r3.u32,
            target,
            argument);
    }

    __imp__sub_830C9F68(ctx, base);

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_830C9F68 task thunk leave#{} r1=0x{:08X} r3=0x{:08X}",
            callIndex,
            ctx.r1.u32,
            ctx.r3.u32);
    }
}

PPC_FUNC_IMPL(__imp__sub_82E0BD28);
PPC_FUNC(sub_82E0BD28)
{
    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = callIndex <= 16;

    uint32_t target = 0;
    uint32_t baseArgument = 0;
    uint32_t offset = 0;
    if (ctx.r3.u32 != 0 && ctx.r3.u32 < PPC_MEMORY_SIZE - 12)
    {
        target = PPC_LOAD_U32(ctx.r3.u32 + 0);
        offset = PPC_LOAD_U32(ctx.r3.u32 + 4);
        baseArgument = PPC_LOAD_U32(ctx.r3.u32 + 8);
    }

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_82E0BD28 offset thunk enter#{} r1=0x{:08X} thunk=0x{:08X} target=0x{:08X} base=0x{:08X} offset=0x{:08X} arg=0x{:08X}",
            callIndex,
            ctx.r1.u32,
            ctx.r3.u32,
            target,
            baseArgument,
            offset,
            baseArgument + offset);
    }

    __imp__sub_82E0BD28(ctx, base);

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_82E0BD28 offset thunk leave#{} r1=0x{:08X} r3=0x{:08X}",
            callIndex,
            ctx.r1.u32,
            ctx.r3.u32);
    }
}

PPC_FUNC_IMPL(__imp__sub_82E0C4E8);
PPC_FUNC(sub_82E0C4E8)
{
    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = callIndex <= 8;
    const uint32_t object = ctx.r3.u32;
    const uint32_t workObject = LoadSwitchGuestU32(base, object + 12);

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_82E0C4E8 loop enter#{} object=0x{:08X} workObj=0x{:08X} workTarget=0x{:08X} stopFlag={}",
            callIndex,
            object,
            workObject,
            GetSwitchVirtualTarget(base, workObject, 12),
            IsSwitchGuestRange(object + 20, 1) ? static_cast<uint32_t>(PPC_LOAD_U8(object + 20)) : 0);
    }

    __imp__sub_82E0C4E8(ctx, base);

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_82E0C4E8 loop leave#{} object=0x{:08X} r3=0x{:08X} stopFlag={}",
            callIndex,
            object,
            ctx.r3.u32,
            IsSwitchGuestRange(object + 20, 1) ? static_cast<uint32_t>(PPC_LOAD_U8(object + 20)) : 0);
    }
}

PPC_FUNC_IMPL(__imp__sub_82E01C20);
PPC_FUNC(sub_82E01C20)
{
    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = callIndex <= 16;
    const uint32_t object = ctx.r3.u32;

    if (shouldLog)
    {
        LOGFN("Switch worker setup: sub_82E01C20 ctor enter#{} object=0x{:08X} taskSrc=0x{:08X} thunkSrc=0x{:08X} arg0=0x{:08X} arg1=0x{:08X}",
            callIndex,
            object,
            ctx.r4.u32,
            ctx.r5.u32,
            ctx.r6.u32,
            ctx.r7.u32);
    }

    __imp__sub_82E01C20(ctx, base);

    if (shouldLog)
    {
        const uint32_t taskObject = LoadSwitchGuestU32(base, object + 32);
        const uint32_t waitObject = LoadSwitchGuestU32(base, object + 40);
        const uint32_t signalObject = LoadSwitchGuestU32(base, object + 48);

        LOGFN("Switch worker setup: sub_82E01C20 ctor leave#{} object=0x{:08X} task=0x{:08X} waitObj=0x{:08X} waitHandle=0x{:08X} signalObj=0x{:08X} signalHandle=0x{:08X} flag={} r3=0x{:08X}",
            callIndex,
            object,
            taskObject,
            waitObject,
            LoadSwitchEventHandle(base, waitObject),
            signalObject,
            LoadSwitchEventHandle(base, signalObject),
            IsSwitchGuestRange(object + 56, 1) ? static_cast<uint32_t>(PPC_LOAD_U8(object + 56)) : 0,
            ctx.r3.u32);
    }
}

PPC_FUNC_IMPL(__imp__sub_82E01918);
PPC_FUNC(sub_82E01918)
{
    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = callIndex <= 64;
    const uint32_t object = ctx.r3.u32;

    if (shouldLog)
    {
        const uint32_t waitObject = LoadSwitchGuestU32(base, object + 40);
        const uint32_t signalObject = LoadSwitchGuestU32(base, object + 48);

        LOGFN("Switch worker kick: sub_82E01918 enter#{} object=0x{:08X} waitObj=0x{:08X} waitHandle=0x{:08X} signalObj=0x{:08X} signalHandle=0x{:08X} flag={}",
            callIndex,
            object,
            waitObject,
            LoadSwitchEventHandle(base, waitObject),
            signalObject,
            LoadSwitchEventHandle(base, signalObject),
            IsSwitchGuestRange(object + 56, 1) ? static_cast<uint32_t>(PPC_LOAD_U8(object + 56)) : 0);
    }

    __imp__sub_82E01918(ctx, base);

    if (shouldLog)
    {
        const uint32_t waitObject = LoadSwitchGuestU32(base, object + 40);
        const uint32_t signalObject = LoadSwitchGuestU32(base, object + 48);

        LOGFN("Switch worker kick: sub_82E01918 leave#{} object=0x{:08X} waitObj=0x{:08X} waitHandle=0x{:08X} signalObj=0x{:08X} signalHandle=0x{:08X} flag={} r3=0x{:08X}",
            callIndex,
            object,
            waitObject,
            LoadSwitchEventHandle(base, waitObject),
            signalObject,
            LoadSwitchEventHandle(base, signalObject),
            IsSwitchGuestRange(object + 56, 1) ? static_cast<uint32_t>(PPC_LOAD_U8(object + 56)) : 0,
            ctx.r3.u32);
    }
}

PPC_FUNC_IMPL(__imp__sub_82E01968);
PPC_FUNC(sub_82E01968)
{
    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = callIndex <= 64;
    const uint32_t object = ctx.r3.u32;
    const uint32_t requestedTimeout = ctx.r4.u32;

    if (shouldLog)
    {
        const uint32_t signalObject = LoadSwitchGuestU32(base, object + 48);

        LOGFN("Switch worker done-check: sub_82E01968 enter#{} object=0x{:08X} requestedTimeout=0x{:08X} signalObj=0x{:08X} signalHandle=0x{:08X} flag={}",
            callIndex,
            object,
            requestedTimeout,
            signalObject,
            LoadSwitchEventHandle(base, signalObject),
            IsSwitchGuestRange(object + 56, 1) ? static_cast<uint32_t>(PPC_LOAD_U8(object + 56)) : 0);
    }

    __imp__sub_82E01968(ctx, base);

    if (shouldLog)
    {
        LOGFN("Switch worker done-check: sub_82E01968 leave#{} object=0x{:08X} result=0x{:08X}",
            callIndex,
            object,
            ctx.r3.u32);
    }
}

PPC_FUNC_IMPL(__imp__sub_82E03460);
PPC_FUNC(sub_82E03460)
{
    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = callIndex <= 128;
    const uint32_t object = ctx.r3.u32;
    const uint32_t handle = LoadSwitchEventHandle(base, object);

    if (shouldLog)
        LOGFN("Switch event wrapper: sub_82E03460 set enter#{} object=0x{:08X} handle=0x{:08X}", callIndex, object, handle);

    __imp__sub_82E03460(ctx, base);

    if (shouldLog)
        LOGFN("Switch event wrapper: sub_82E03460 set leave#{} object=0x{:08X} handle=0x{:08X} r3=0x{:08X}", callIndex, object, handle, ctx.r3.u32);
}

PPC_FUNC_IMPL(__imp__sub_82E03468);
PPC_FUNC(sub_82E03468)
{
    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = callIndex <= 128;
    const uint32_t object = ctx.r3.u32;
    const uint32_t handle = LoadSwitchEventHandle(base, object);

    if (shouldLog)
        LOGFN("Switch event wrapper: sub_82E03468 reset enter#{} object=0x{:08X} handle=0x{:08X}", callIndex, object, handle);

    __imp__sub_82E03468(ctx, base);

    if (shouldLog)
        LOGFN("Switch event wrapper: sub_82E03468 reset leave#{} object=0x{:08X} handle=0x{:08X} r3=0x{:08X}", callIndex, object, handle, ctx.r3.u32);
}

PPC_FUNC_IMPL(__imp__sub_82E03470);
PPC_FUNC(sub_82E03470)
{
    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = callIndex <= 128;
    const uint32_t object = ctx.r3.u32;
    const uint32_t handle = LoadSwitchEventHandle(base, object);

    if (shouldLog)
        LOGFN("Switch event wrapper: sub_82E03470 wait enter#{} object=0x{:08X} handle=0x{:08X}", callIndex, object, handle);

    __imp__sub_82E03470(ctx, base);

    if (shouldLog)
        LOGFN("Switch event wrapper: sub_82E03470 wait leave#{} object=0x{:08X} handle=0x{:08X} r3=0x{:08X}", callIndex, object, handle, ctx.r3.u32);
}

PPC_FUNC_IMPL(__imp__sub_82E03480);
PPC_FUNC(sub_82E03480)
{
    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = callIndex <= 128;
    const uint32_t object = ctx.r3.u32;
    const uint32_t handle = LoadSwitchEventHandle(base, object);

    if (shouldLog)
        LOGFN("Switch event wrapper: sub_82E03480 try-wait enter#{} object=0x{:08X} handle=0x{:08X}", callIndex, object, handle);

    __imp__sub_82E03480(ctx, base);

    if (shouldLog)
        LOGFN("Switch event wrapper: sub_82E03480 try-wait leave#{} object=0x{:08X} handle=0x{:08X} r3=0x{:08X}", callIndex, object, handle, ctx.r3.u32);
}

PPC_FUNC_IMPL(__imp__sub_82E01BA8);
PPC_FUNC(sub_82E01BA8)
{
    PPC_FUNC_PROLOGUE();

    static std::atomic<uint32_t> callCount = 0;
    static std::atomic<uint32_t> detailCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const uint32_t object = ctx.r3.u32;
    const uint32_t callerStack = ctx.r1.u32;

    auto shouldLogDetail = [&](uint32_t iteration) -> bool
    {
        return callIndex <= 8 && iteration <= 4 &&
            detailCount.fetch_add(1, std::memory_order_relaxed) < 96;
    };

    if (callIndex <= 8)
    {
        const uint32_t waitObject = PPC_LOAD_U32(object + 40);
        const uint32_t signalObject = PPC_LOAD_U32(object + 48);
        LOGFN("Switch worker loop: sub_82E01BA8 enter#{} object=0x{:08X} flag={} waitObj=0x{:08X} waitHandle=0x{:08X} waitTarget=0x{:08X} signalObj=0x{:08X} signalHandle=0x{:08X} signalTarget=0x{:08X}",
            callIndex,
            object,
            static_cast<uint32_t>(PPC_LOAD_U8(object + 56)),
            waitObject,
            LoadSwitchEventHandle(base, waitObject),
            GetSwitchVirtualTarget(base, waitObject, 12),
            signalObject,
            LoadSwitchEventHandle(base, signalObject),
            GetSwitchVirtualTarget(base, signalObject, 4));
    }

    ctx.r1.u32 -= 96;
    PPC_STORE_U32(ctx.r1.u32, callerStack);

    uint32_t iteration = 0;
    while (PPC_LOAD_U8(object + 56) != 0)
    {
        iteration++;

        uint32_t waitObject = PPC_LOAD_U32(object + 40);
        uint32_t waitTarget = GetSwitchVirtualTarget(base, waitObject, 12);
        const bool logThisIteration = shouldLogDetail(iteration);

        if (logThisIteration)
        {
            LOGFN("Switch worker loop: sub_82E01BA8 iter#{} call#{} wait enter object=0x{:08X} waitObj=0x{:08X} waitHandle=0x{:08X} target=0x{:08X} flag={}",
                iteration,
                callIndex,
                object,
                waitObject,
                LoadSwitchEventHandle(base, waitObject),
                waitTarget,
                static_cast<uint32_t>(PPC_LOAD_U8(object + 56)));
        }

        ctx.r3.u64 = waitObject;
        PPC_CALL_INDIRECT_FUNC(waitTarget);

        if (logThisIteration)
        {
            LOGFN("Switch worker loop: sub_82E01BA8 iter#{} call#{} wait leave object=0x{:08X} flag={} r3=0x{:08X}",
                iteration,
                callIndex,
                object,
                static_cast<uint32_t>(PPC_LOAD_U8(object + 56)),
                ctx.r3.u32);
        }

        if (PPC_LOAD_U8(object + 56) != 0)
        {
            if (logThisIteration)
            {
                const uint32_t table = PPC_LOAD_U32(object);
                LOGFN("Switch worker loop: sub_82E01BA8 iter#{} call#{} dispatch enter object=0x{:08X} table=0x{:08X} target=0x{:08X}",
                    iteration,
                    callIndex,
                    object,
                    table,
                    table != 0 ? PPC_LOAD_U32(table + 4) : 0);
            }

            ctx.r3.u64 = object;
            sub_82456270(ctx, base);

            if (logThisIteration)
            {
                LOGFN("Switch worker loop: sub_82E01BA8 iter#{} call#{} dispatch leave object=0x{:08X} flag={} r3=0x{:08X}",
                    iteration,
                    callIndex,
                    object,
                    static_cast<uint32_t>(PPC_LOAD_U8(object + 56)),
                    ctx.r3.u32);
            }
        }

        uint32_t signalObject = PPC_LOAD_U32(object + 48);
        uint32_t signalTarget = GetSwitchVirtualTarget(base, signalObject, 4);

        if (logThisIteration)
        {
            LOGFN("Switch worker loop: sub_82E01BA8 iter#{} call#{} signal enter object=0x{:08X} signalObj=0x{:08X} signalHandle=0x{:08X} target=0x{:08X} flag={}",
                iteration,
                callIndex,
                object,
                signalObject,
                LoadSwitchEventHandle(base, signalObject),
                signalTarget,
                static_cast<uint32_t>(PPC_LOAD_U8(object + 56)));
        }

        ctx.r3.u64 = signalObject;
        PPC_CALL_INDIRECT_FUNC(signalTarget);

        if (logThisIteration)
        {
            LOGFN("Switch worker loop: sub_82E01BA8 iter#{} call#{} signal leave object=0x{:08X} flag={} r3=0x{:08X}",
                iteration,
                callIndex,
                object,
                static_cast<uint32_t>(PPC_LOAD_U8(object + 56)),
                ctx.r3.u32);
        }
    }

    ctx.r1.u32 = callerStack;

    if (callIndex <= 8)
    {
        LOGFN("Switch worker loop: sub_82E01BA8 leave#{} object=0x{:08X} iterations={} flag={} r3=0x{:08X}",
            callIndex,
            object,
            iteration,
            static_cast<uint32_t>(PPC_LOAD_U8(object + 56)),
            ctx.r3.u32);
    }
}
#endif

static std::thread::id g_mainThreadId = std::this_thread::get_id();

// SWA::CApplication::Update
PPC_FUNC_IMPL(__imp__sub_822C1130);
PPC_FUNC(sub_822C1130)
{
#if defined(__SWITCH__)
    static bool loggedFirstUpdate = false;
    if (!loggedFirstUpdate)
    {
        LOGN("Switch guest CApplication update reached");
        loggedFirstUpdate = true;
    }
#endif

    Video::WaitOnSwapChain();

    // Correct small delta time errors.
    if (Config::FPS >= FPS_MIN && Config::FPS < FPS_MAX)
    {
        double targetDeltaTime = 1.0 / Config::FPS;

        if (abs(ctx.f1.f64 - targetDeltaTime) < 0.00001)
            ctx.f1.f64 = targetDeltaTime;
    }

    App::s_deltaTime = ctx.f1.f64;
    App::s_time += App::s_deltaTime;

    // This function can also be called by the loading thread,
    // which SDL does not like. To prevent the OS from thinking
    // the process is unresponsive, we will flush while waiting
    // for the pipelines to finish compiling in video.cpp.
    if (std::this_thread::get_id() == g_mainThreadId)
    {
#if !defined(__SWITCH__)
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
#endif
        GameWindow::Update();
    }

    AudioPatches::Update(App::s_deltaTime);
    InspirePatches::Update();

    // Apply subtitles option.
    if (auto pApplicationDocument = SWA::CApplicationDocument::GetInstance())
        pApplicationDocument->m_InspireSubtitles = Config::Subtitles;

    if (Config::EnableEventCollisionDebugView)
        *SWA::SGlobals::ms_IsTriggerRender = true;

    if (Config::EnableGIMipLevelDebugView)
        *SWA::SGlobals::ms_VisualizeLoadedLevel = true;

    if (Config::EnableObjectCollisionDebugView)
        *SWA::SGlobals::ms_IsObjectCollisionRender = true;

    if (Config::EnableStageCollisionDebugView)
        *SWA::SGlobals::ms_IsCollisionRender = true;

    __imp__sub_822C1130(ctx, base);
}
