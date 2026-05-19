#include "app.h"
#include <api/SWA.h>
#include <apu/audio.h>
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
static constexpr bool kEnableSwitchBootTrace = false;

static void LogSwitchBootTrace(const char* name, const char* phase, PPCContext& ctx)
{
    if (!kEnableSwitchBootTrace)
        return;

    LOGFN("Switch boot trace: {} {} r1=0x{:08X} r3=0x{:08X} r4=0x{:08X}",
        name,
        phase,
        ctx.r1.u32,
        ctx.r3.u32,
        ctx.r4.u32);
}

static bool ShouldLogSwitchFrameTrace()
{
    if (!kEnableSwitchBootTrace)
        return false;

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

static const char* ReadSwitchGuestCString(uint8_t* base, uint32_t address, char* buffer, size_t bufferSize)
{
    if (bufferSize == 0)
        return "";

    buffer[0] = '\0';

    if (!IsSwitchGuestRange(address, 1))
        return buffer;

    size_t index = 0;
    for (; index + 1 < bufferSize && IsSwitchGuestRange(address + static_cast<uint32_t>(index), 1); index++)
    {
        const char value = static_cast<char>(PPC_LOAD_U8(address + static_cast<uint32_t>(index)));
        if (value == '\0')
            break;
        buffer[index] = value;
    }

    buffer[index] = '\0';
    return buffer;
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

#define SWITCH_BOOT_TRACE_WRAPPER_LIMIT(func, label, limit) \
PPC_FUNC_IMPL(__imp__##func); \
PPC_FUNC(func) \
{ \
    if (!kEnableSwitchBootTrace) \
    { \
        __imp__##func(ctx, base); \
        return; \
    } \
    static std::atomic<uint32_t> callCount = 0; \
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1; \
    const bool shouldLog = callIndex <= (limit); \
    if (shouldLog) \
        LOGFN("Switch boot trace: {} enter#{} r1=0x{:08X} r3=0x{:08X} r4=0x{:08X} r5=0x{:08X}", label, callIndex, ctx.r1.u32, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32); \
    __imp__##func(ctx, base); \
    if (shouldLog) \
        LOGFN("Switch boot trace: {} leave#{} r1=0x{:08X} r3=0x{:08X} r4=0x{:08X} r5=0x{:08X}", label, callIndex, ctx.r1.u32, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32); \
}

#define SWITCH_BOOT_TRACE_WRAPPER(func, label) SWITCH_BOOT_TRACE_WRAPPER_LIMIT(func, label, 64)

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
SWITCH_BOOT_TRACE_WRAPPER(sub_8248EEE0, "sub_8248EEE0 document startup helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_827BF9A8, "sub_827BF9A8 document startup helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_827CEC38, "sub_827CEC38 document startup helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_822C0390, "sub_822C0390 alloc tracked")
SWITCH_BOOT_TRACE_WRAPPER(sub_831B4C20, "sub_831B4C20 time/random helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E864A8, "sub_82E864A8 startup object ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_822C0270, "sub_822C0270 release")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E1B6C0, "sub_82E1B6C0 task object init")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E1A6C8, "sub_82E1A6C8 task object config")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF220, "sub_824EF220 startup assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F4540, "sub_824F4540 startup subsystem")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EC268, "sub_824EC268 startup subsystem")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EFCD8, "sub_824EFCD8 startup subsystem")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F4690, "sub_824F4690 startup subsystem")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E01348, "sub_82E01348 worker system init")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E01520, "sub_82E01520 worker system init")
SWITCH_BOOT_TRACE_WRAPPER(sub_822C3A00, "sub_822C3A00 runtime init")
SWITCH_BOOT_TRACE_WRAPPER(sub_8250B948, "sub_8250B948 app state ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_822C0000, "sub_822C0000 ctor helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EFD28, "sub_824EFD28 startup subsystem")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EDE18, "sub_824EDE18 startup subsystem")
SWITCH_BOOT_TRACE_WRAPPER(sub_822E9FE0, "sub_822E9FE0 job flags init")
SWITCH_BOOT_TRACE_WRAPPER(sub_8259F4C8, "sub_8259F4C8 resource query")
SWITCH_BOOT_TRACE_WRAPPER(sub_8259F650, "sub_8259F650 resource query")
SWITCH_BOOT_TRACE_WRAPPER(sub_822C6570, "sub_822C6570 pointer pair assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E06C40, "sub_82E06C40 resource state check")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF400, "sub_824EF400 scene bind")
SWITCH_BOOT_TRACE_WRAPPER(sub_8259F4E0, "sub_8259F4E0 resource cleanup")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF498, "sub_824EF498 startup helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF4F8, "sub_824EF4F8 startup helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_82B666A8, "sub_82B666A8 service ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF558, "sub_824EF558 service assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EFA88, "sub_824EFA88 startup helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EE358, "sub_824EE358 startup helper")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E6B4D0, "sub_82E6B4D0 resource bind")
SWITCH_BOOT_TRACE_WRAPPER(sub_8250BE48, "sub_8250BE48 scene manager init")
SWITCH_BOOT_TRACE_WRAPPER(sub_8252DC70, "sub_8252DC70 scene manager ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_82541138, "sub_82541138 scene manager base ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_8250BC80, "sub_8250BC80 scene holder init")
SWITCH_BOOT_TRACE_WRAPPER(sub_822C0988, "sub_822C0988 small alloc")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E676A0, "sub_82E676A0 scene tree attach")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E669D8, "sub_82E669D8 scene tree attach impl")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E63550, "sub_82E63550 scene lookup")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E07A30, "sub_82E07A30 scene lock")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E63A38, "sub_82E63A38 scene detach")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E62870, "sub_82E62870 scene state notify")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E628D0, "sub_82E628D0 scene dirty notify")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E62D20, "sub_82E62D20 scene node ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E63BD0, "sub_82E63BD0 scene node ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E64398, "sub_82E64398 scene node assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E62E10, "sub_82E62E10 scene node commit")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E62E70, "sub_82E62E70 scene node validate")
SWITCH_BOOT_TRACE_WRAPPER(sub_82EA6718, "sub_82EA6718 tree insert")
SWITCH_BOOT_TRACE_WRAPPER(sub_82EA67B0, "sub_82EA67B0 tree rebalance")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E66800, "sub_82E66800 scene iterator")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E64980, "sub_82E64980 scene relation")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E61DA0, "sub_82E61DA0 scene ready")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E66778, "sub_82E66778 scene finalize")
SWITCH_BOOT_TRACE_WRAPPER(sub_8245B128, "sub_8245B128 save service ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F5E48, "sub_824F5E48 save service assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EEB00, "sub_824EEB00 platform result assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F58D0, "sub_824F58D0 level loader ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F5A70, "sub_824F5A70 level loader assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_82421F88, "sub_82421F88 resource lookup")
SWITCH_BOOT_TRACE_WRAPPER(sub_822E07C0, "sub_822E07C0 lookup bind")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E0EBE8, "sub_82E0EBE8 resource handle copy")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E627E0, "sub_82E627E0 resource handle normalize")
SWITCH_BOOT_TRACE_WRAPPER(sub_8255B218, "sub_8255B218 loader bind")
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_824F2C40, "sub_824F2C40 startup smart ref init", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82B28120, "sub_82B28120 startup attach ref init", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82DFAE10, "sub_82DFAE10 startup string init", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_824ED808, "sub_824ED808 startup loader singleton", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825DE468, "sub_825DE468 startup resource iterator init", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825DE520, "sub_825DE520 startup resource iterator destroy", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825DDD78, "sub_825DDD78 startup resource iterator current", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825DF458, "sub_825DF458 startup resource batch init", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825DF068, "sub_825DF068 startup resource next", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825DF570, "sub_825DF570 startup resource batch reset", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825DF4D8, "sub_825DF4D8 startup resource field bind", 256)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825DF510, "sub_825DF510 startup resource field bind", 256)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825DF4D0, "sub_825DF4D0 startup resource batch commit", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82AFAF88, "sub_82AFAF88 startup attach resource table", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825DF180, "sub_825DF180 startup resource advance", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825DF490, "sub_825DF490 startup resource batch destroy", 128)
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF958, "sub_824EF958 document state")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EC308, "sub_824EC308 mission registry")
SWITCH_BOOT_TRACE_WRAPPER(sub_82DFA088, "sub_82DFA088 raw alloc")
SWITCH_BOOT_TRACE_WRAPPER(sub_824DC318, "sub_824DC318 service ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF5C8, "sub_824EF5C8 service assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_82E23E30, "sub_82E23E30 service register")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F2338, "sub_824F2338 service finalize")
SWITCH_BOOT_TRACE_WRAPPER(sub_824E54C8, "sub_824E54C8 service ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF638, "sub_824EF638 service assign")
SWITCH_BOOT_TRACE_WRAPPER(sub_824F2760, "sub_824F2760 service finalize")
SWITCH_BOOT_TRACE_WRAPPER(sub_82BCFFB0, "sub_82BCFFB0 startup service ctor")
SWITCH_BOOT_TRACE_WRAPPER(sub_824EF6A8, "sub_824EF6A8 startup service assign")
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_824FD110, "sub_824FD110 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82555CB8, "sub_82555CB8 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_8255A000, "sub_8255A000 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_8259C590, "sub_8259C590 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825CE300, "sub_825CE300 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825CF410, "sub_825CF410 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825CF7C8, "sub_825CF7C8 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_827A21F8, "sub_827A21F8 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_827C65B0, "sub_827C65B0 option bind batch", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_827FE048, "sub_827FE048 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82B2C6B8, "sub_82B2C6B8 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82B40110, "sub_82B40110 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82B98380, "sub_82B98380 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82E09098, "sub_82E09098 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82E12DC8, "sub_82E12DC8 option bind caller", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_8258DA78, "sub_8258DA78 light option batch", 32)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82AE3AC0, "sub_82AE3AC0 light option apply", 64)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82AE40F0, "sub_82AE40F0 light option apply", 64)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82AE4510, "sub_82AE4510 light option apply", 64)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82AE4FE8, "sub_82AE4FE8 light option apply", 64)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82AE6678, "sub_82AE6678 light option apply", 64)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82AE7128, "sub_82AE7128 light option apply", 64)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82AE80D8, "sub_82AE80D8 light option apply", 64)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82AE81D8, "sub_82AE81D8 light option apply", 64)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82AE8AF0, "sub_82AE8AF0 light option apply", 64)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82AE9570, "sub_82AE9570 light option apply", 64)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82AE97F8, "sub_82AE97F8 light option apply", 64)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82B9DEF0, "sub_82B9DEF0 light option finalize", 64)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825531D8, "sub_825531D8 option map lookup", 256)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82DF9D90, "sub_82DF9D90 option alloc", 256)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_831B1570, "sub_831B1570 guest memmove", 256)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825ACDA0, "sub_825ACDA0 option vector move tail", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82DFB920, "sub_82DFB920 string copy", 256)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82DFB9C0, "sub_82DFB9C0 string copy", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_825050B0, "sub_825050B0 option index assign", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82503D18, "sub_82503D18 option index check", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82498048, "sub_82498048 option publish", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82B96560, "sub_82B96560 option resolve", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_827C68B8, "sub_827C68B8 option table cleanup", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82530D08, "sub_82530D08 option table clear", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82DF9E50, "sub_82DF9E50 option free", 128)
SWITCH_BOOT_TRACE_WRAPPER_LIMIT(sub_82E61910, "sub_82E61910 scene option helper", 128)

#undef SWITCH_BOOT_TRACE_WRAPPER
#undef SWITCH_BOOT_TRACE_WRAPPER_LIMIT

static uint32_t SwitchVectorCount4(uint32_t begin, uint32_t end)
{
    if (begin == 0 || end < begin)
        return 0;

    return (end - begin) / 4;
}

PPC_FUNC_IMPL(__imp__sub_82493FB0);
PPC_FUNC(sub_82493FB0)
{
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_82493FB0(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && (callIndex <= 64 || (callIndex % 256) == 0);
    const uint32_t object = ctx.r3.u32;
    const uint32_t requested = ctx.r4.u32;
    const uint32_t defaultValue = ctx.r5.u32;
    const bool hasVectorFields = IsSwitchGuestRange(object, 16);
    const uint32_t begin = hasVectorFields ? LoadSwitchGuestU32(base, object + 4) : 0;
    const uint32_t end = hasVectorFields ? LoadSwitchGuestU32(base, object + 8) : 0;
    const uint32_t capacityEnd = hasVectorFields ? LoadSwitchGuestU32(base, object + 12) : 0;

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_82493FB0 option reserve enter#{} r1=0x{:08X} object=0x{:08X} requested={} default=0x{:08X} begin=0x{:08X} end=0x{:08X} capacityEnd=0x{:08X} count={} capacity={}",
            callIndex,
            ctx.r1.u32,
            object,
            requested,
            defaultValue,
            begin,
            end,
            capacityEnd,
            SwitchVectorCount4(begin, end),
            SwitchVectorCount4(begin, capacityEnd));
    }

    __imp__sub_82493FB0(ctx, base);

    if (shouldLog)
    {
        const bool hasUpdatedVectorFields = IsSwitchGuestRange(object, 16);
        const uint32_t updatedBegin = hasUpdatedVectorFields ? LoadSwitchGuestU32(base, object + 4) : 0;
        const uint32_t updatedEnd = hasUpdatedVectorFields ? LoadSwitchGuestU32(base, object + 8) : 0;
        const uint32_t updatedCapacityEnd = hasUpdatedVectorFields ? LoadSwitchGuestU32(base, object + 12) : 0;
        LOGFN("Switch boot trace: sub_82493FB0 option reserve leave#{} r1=0x{:08X} object=0x{:08X} r3=0x{:08X} begin=0x{:08X} end=0x{:08X} capacityEnd=0x{:08X} count={} capacity={}",
            callIndex,
            ctx.r1.u32,
            object,
            ctx.r3.u32,
            updatedBegin,
            updatedEnd,
            updatedCapacityEnd,
            SwitchVectorCount4(updatedBegin, updatedEnd),
            SwitchVectorCount4(updatedBegin, updatedCapacityEnd));
    }
}

PPC_FUNC_IMPL(__imp__sub_824978C0);
PPC_FUNC(sub_824978C0)
{
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_824978C0(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && (callIndex <= 64 || (callIndex % 256) == 0);
    const uint32_t object = ctx.r3.u32;
    const uint32_t insertAt = ctx.r4.u32;
    const uint32_t insertCount = ctx.r5.u32;
    const uint32_t defaultPointer = ctx.r6.u32;
    const bool hasVectorFields = IsSwitchGuestRange(object, 16);
    const uint32_t begin = hasVectorFields ? LoadSwitchGuestU32(base, object + 4) : 0;
    const uint32_t end = hasVectorFields ? LoadSwitchGuestU32(base, object + 8) : 0;
    const uint32_t capacityEnd = hasVectorFields ? LoadSwitchGuestU32(base, object + 12) : 0;
    const uint32_t defaultValue = IsSwitchGuestRange(defaultPointer, 4) ? LoadSwitchGuestU32(base, defaultPointer) : 0;

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_824978C0 option vector grow enter#{} r1=0x{:08X} object=0x{:08X} insertAt=0x{:08X} insertCount={} defaultPtr=0x{:08X} default=0x{:08X} begin=0x{:08X} end=0x{:08X} capacityEnd=0x{:08X} count={} capacity={}",
            callIndex,
            ctx.r1.u32,
            object,
            insertAt,
            insertCount,
            defaultPointer,
            defaultValue,
            begin,
            end,
            capacityEnd,
            SwitchVectorCount4(begin, end),
            SwitchVectorCount4(begin, capacityEnd));
    }

    __imp__sub_824978C0(ctx, base);

    if (shouldLog)
    {
        const bool hasUpdatedVectorFields = IsSwitchGuestRange(object, 16);
        const uint32_t updatedBegin = hasUpdatedVectorFields ? LoadSwitchGuestU32(base, object + 4) : 0;
        const uint32_t updatedEnd = hasUpdatedVectorFields ? LoadSwitchGuestU32(base, object + 8) : 0;
        const uint32_t updatedCapacityEnd = hasUpdatedVectorFields ? LoadSwitchGuestU32(base, object + 12) : 0;
        LOGFN("Switch boot trace: sub_824978C0 option vector grow leave#{} r1=0x{:08X} object=0x{:08X} r3=0x{:08X} begin=0x{:08X} end=0x{:08X} capacityEnd=0x{:08X} count={} capacity={}",
            callIndex,
            ctx.r1.u32,
            object,
            ctx.r3.u32,
            updatedBegin,
            updatedEnd,
            updatedCapacityEnd,
            SwitchVectorCount4(updatedBegin, updatedEnd),
            SwitchVectorCount4(updatedBegin, updatedCapacityEnd));
    }
}

PPC_FUNC_IMPL(__imp__sub_822C0890);
PPC_FUNC(sub_822C0890)
{
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_822C0890(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && callIndex <= 128;
    const uint32_t object = ctx.r3.u32;
    const uint32_t vtable = IsSwitchGuestRange(object, 12) ? LoadSwitchGuestU32(base, object) : 0;
    const uint32_t releaseTarget = IsSwitchGuestRange(vtable, 12) ? LoadSwitchGuestU32(base, vtable + 4) : 0;
    const uint32_t destroyTarget = IsSwitchGuestRange(vtable, 12) ? LoadSwitchGuestU32(base, vtable + 8) : 0;
    const uint32_t strongCount = IsSwitchGuestRange(object, 12) ? LoadSwitchGuestU32(base, object + 4) : 0;
    const uint32_t weakCount = IsSwitchGuestRange(object, 12) ? LoadSwitchGuestU32(base, object + 8) : 0;

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_822C0890 ref release enter#{} object=0x{:08X} vtable=0x{:08X} strong={} weak={} releaseTarget=0x{:08X} destroyTarget=0x{:08X} r1=0x{:08X}",
            callIndex,
            object,
            vtable,
            strongCount,
            weakCount,
            releaseTarget,
            destroyTarget,
            ctx.r1.u32);
    }

    __imp__sub_822C0890(ctx, base);

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_822C0890 ref release leave#{} object=0x{:08X} strong={} weak={} r1=0x{:08X} r3=0x{:08X}",
            callIndex,
            object,
            IsSwitchGuestRange(object, 12) ? LoadSwitchGuestU32(base, object + 4) : 0,
            IsSwitchGuestRange(object, 12) ? LoadSwitchGuestU32(base, object + 8) : 0,
            ctx.r1.u32,
            ctx.r3.u32);
    }
}

PPC_FUNC_IMPL(__imp__sub_82B85970);
PPC_FUNC(sub_82B85970)
{
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_82B85970(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && (callIndex <= 64 || (callIndex % 512) == 0);

    const uint32_t keyObject = ctx.r5.u32;
    const uint32_t keyPointer = IsSwitchGuestRange(keyObject, 8) ? PPC_LOAD_U32(keyObject) : 0;
    const uint32_t keyValue = IsSwitchGuestRange(keyObject, 8) ? PPC_LOAD_U32(keyObject + 4) : 0;
    char key[96]{};
    if (shouldLog)
        ReadSwitchGuestCString(base, keyPointer, key, sizeof(key));

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_82B85970 startup option bind enter#{} r1=0x{:08X} r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} keyObj=0x{:08X} keyPtr=0x{:08X} keyValue=0x{:08X} key=\"{}\"",
            callIndex,
            ctx.r1.u32,
            ctx.r3.u32,
            ctx.r4.u32,
            ctx.r5.u32,
            keyObject,
            keyPointer,
            keyValue,
            key);
    }

    __imp__sub_82B85970(ctx, base);

    if (shouldLog)
    {
        LOGFN("Switch boot trace: sub_82B85970 startup option bind leave#{} r1=0x{:08X} r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} key=\"{}\"",
            callIndex,
            ctx.r1.u32,
            ctx.r3.u32,
            ctx.r4.u32,
            ctx.r5.u32,
            key);
    }
}

PPC_FUNC_IMPL(__imp__sub_82456270);
PPC_FUNC(sub_82456270)
{
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_82456270(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && callIndex <= 16;

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
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_830C9F68(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && callIndex <= 32;

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
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_82E0BD28(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && callIndex <= 16;

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
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_82E0C4E8(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && callIndex <= 8;
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
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_82E01C20(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && callIndex <= 16;
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
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_82E01918(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && callIndex <= 64;
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
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_82E01968(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && callIndex <= 64;
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
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_82E03460(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && callIndex <= 128;
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
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_82E03468(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && callIndex <= 128;
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
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_82E03470(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && callIndex <= 128;
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
    if (!kEnableSwitchBootTrace)
    {
        __imp__sub_82E03480(ctx, base);
        return;
    }

    static std::atomic<uint32_t> callCount = 0;
    const auto callIndex = callCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool shouldLog = kEnableSwitchBootTrace && callIndex <= 128;
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
    __imp__sub_82E01BA8(ctx, base);
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
        XAudioSetGuestCallbacksEnabled(true);
        LOGN("Switch XAudio guest callbacks enabled");
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
