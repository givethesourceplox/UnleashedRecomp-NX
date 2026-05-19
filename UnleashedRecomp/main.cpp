#include <stdafx.h>
#ifdef __x86_64__
#include <cpuid.h>
#endif
#include <cpu/guest_thread.h>
#include <gpu/video.h>
#include <kernel/function.h>
#include <kernel/memory.h>
#include <kernel/heap.h>
#include <kernel/xam.h>
#include <kernel/io/file_system.h>
#include <file.h>
#include <xex.h>
#include <apu/audio.h>
#include <hid/hid.h>
#include <user/config.h>
#include <user/paths.h>
#include <user/persistent_storage_manager.h>
#include <user/registry.h>
#include <kernel/xdbf.h>
#include <install/installer.h>
#include <install/update_checker.h>
#include <os/logger.h>
#include <os/process.h>
#include <os/registry.h>
#include <ui/game_window.h>
#include <ui/installer_wizard.h>
#include <mod/mod_loader.h>
#include <preload_executable.h>

#ifdef _WIN32
#include <timeapi.h>
#endif

#if defined(_WIN32) && defined(UNLEASHED_RECOMP_D3D12)
static std::array<std::string_view, 3> g_D3D12RequiredModules =
{
    "D3D12/D3D12Core.dll",
    "dxcompiler.dll",
    "dxil.dll"
};
#endif

const size_t XMAIOBegin = 0x7FEA0000;
const size_t XMAIOEnd = XMAIOBegin + 0x0000FFFF;

Memory g_memory;
Heap g_userHeap;
XDBFWrapper g_xdbfWrapper;
std::unordered_map<uint16_t, GuestTexture*> g_xdbfTextureCache;

void HostStartup()
{
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif

    hid::Init();
}

// Name inspired from nt's entry point
void KiSystemStartup()
{
    if (g_memory.base == nullptr)
    {
#if defined(__SWITCH__)
        LOGFN_ERROR("Switch PPC memory init failed: reason='{}', result=0x{:08X}, selectedBase=0x{:016X}, alias=0x{:016X}+0x{:016X}, aslr=0x{:016X}+0x{:016X}, heap=0x{:016X}+0x{:016X}, aliasFallback={}",
            g_memory.switchInitFailureReason ? g_memory.switchInitFailureReason : "unknown",
            g_memory.switchInitResult,
            g_memory.switchSelectedBase,
            g_memory.switchAliasBase,
            g_memory.switchAliasSize,
            g_memory.switchAslrBase,
            g_memory.switchAslrSize,
            g_memory.switchHeapBase,
            g_memory.switchHeapSize,
            g_memory.switchUsingAliasBaseFallback);
        LOGFN_ERROR("Switch PPC memory commit failure: guestOffset=0x{:016X}, hostAddress=0x{:016X}, queriedMemory=0x{:016X}+0x{:016X}, type=0x{:X}, attr=0x{:X}, perm=0x{:X}, pageInfo=0x{:X}",
            g_memory.switchCommitFailureOffset,
            g_memory.switchCommitFailureAddress,
            g_memory.switchCommitFailureMemoryBase,
            g_memory.switchCommitFailureMemorySize,
            g_memory.switchCommitFailureMemoryType,
            g_memory.switchCommitFailureMemoryAttr,
            g_memory.switchCommitFailureMemoryPerm,
            g_memory.switchCommitFailurePageInfo);
#endif
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), Localise("System_MemoryAllocationFailed").c_str(), GameWindow::s_pWindow);
        std::_Exit(1);
    }

#if defined(__SWITCH__)
    LOGFN("Switch PPC memory ready: base=0x{:016X}", g_memory.switchSelectedBase);
#endif

    g_userHeap.Init();

#if defined(__SWITCH__)
    LOGFN("Switch guest heap ready: heap={}, physicalHeap={}", static_cast<const void*>(g_userHeap.heap), static_cast<const void*>(g_userHeap.physicalHeap));
#endif

    const auto gameContent = XamMakeContent(XCONTENTTYPE_RESERVED, "Game");
    const auto updateContent = XamMakeContent(XCONTENTTYPE_RESERVED, "Update");
    const std::string gamePath = (const char*)(GetGamePath() / "game").u8string().c_str();
    const std::string updatePath = (const char*)(GetGamePath() / "update").u8string().c_str();
    XamRegisterContent(gameContent, gamePath);
    XamRegisterContent(updateContent, updatePath);

#if defined(__SWITCH__)
    LOGFN("Switch content roots prepared: game='{}', update='{}'", gamePath, updatePath);
#endif

    const auto saveFilePath = GetSaveFilePath(true);
    bool saveFileExists = std::filesystem::exists(saveFilePath);

#if defined(__SWITCH__)
    LOGFN("Switch save probe: path='{}', exists={}", saveFilePath.string(), saveFileExists);
#endif

    if (!saveFileExists)
    {
        // Copy base save data to modded save as fallback.
        std::error_code ec;
        std::filesystem::create_directories(saveFilePath.parent_path(), ec);

        if (!ec)
        {
            std::filesystem::copy_file(GetSaveFilePath(false), saveFilePath, ec);
            saveFileExists = !ec;
        }

#if defined(__SWITCH__)
        LOGFN("Switch save fallback copy: path='{}', result={}, ec='{}'",
            saveFilePath.string(),
            saveFileExists,
            ec.message());
#endif
    }

    if (saveFileExists)
    {
        std::u8string savePathU8 = saveFilePath.parent_path().u8string();
        XamRegisterContent(XamMakeContent(XCONTENTTYPE_SAVEDATA, "SYS-DATA"), (const char*)(savePathU8.c_str()));

#if defined(__SWITCH__)
        LOGFN("Switch save content registered: root='{}'", reinterpret_cast<const char*>(savePathU8.c_str()));
#endif
    }

    // Mount game
    const auto gameMountResult = XamContentCreateEx(0, "game", &gameContent, OPEN_EXISTING, nullptr, nullptr, 0, 0, nullptr);
    const auto updateMountResult = XamContentCreateEx(0, "update", &updateContent, OPEN_EXISTING, nullptr, nullptr, 0, 0, nullptr);

    // OS mounts game data to D:
    const auto dMountResult = XamContentCreateEx(0, "D", &gameContent, OPEN_EXISTING, nullptr, nullptr, 0, 0, nullptr);

#if defined(__SWITCH__)
    LOGFN("Switch content mounts: game=0x{:08X}, update=0x{:08X}, D=0x{:08X}",
        gameMountResult,
        updateMountResult,
        dMountResult);
#endif

    std::error_code ec;
    uint32_t dlcCount = 0;
    for (auto& file : std::filesystem::directory_iterator(GetGamePath() / "dlc", ec))
    {
        if (file.is_directory())
        {
            std::u8string fileNameU8 = file.path().filename().u8string();
            std::u8string filePathU8 = file.path().u8string();
            XamRegisterContent(XamMakeContent(XCONTENTTYPE_DLC, (const char*)(fileNameU8.c_str())), (const char*)(filePathU8.c_str()));
            ++dlcCount;
        }
    }

#if defined(__SWITCH__)
    LOGFN("Switch DLC scan: root='{}', count={}, ec='{}'",
        (GetGamePath() / "dlc").string(),
        dlcCount,
        ec.message());
#endif

    XAudioInitializeSystem();
}

uint32_t LdrLoadModule(const std::filesystem::path &path)
{
#if defined(__SWITCH__)
    LOGFN("Switch loading module: {}", path.string());
#endif

    auto loadResult = LoadFile(path);
    if (loadResult.empty())
    {
#if defined(__SWITCH__)
        LOGFN_ERROR("Switch module load failed: '{}'", path.string());
#endif
        assert("Failed to load module" && false);
        return 0;
    }

#if defined(__SWITCH__)
    LOGFN("Switch module file loaded: bytes={}", loadResult.size());
#endif

    auto* header = reinterpret_cast<const Xex2Header*>(loadResult.data());
    auto* security = reinterpret_cast<const Xex2SecurityInfo*>(loadResult.data() + header->securityOffset);
    const auto* fileFormatInfo = reinterpret_cast<const Xex2OptFileFormatInfo*>(getOptHeaderPtr(loadResult.data(), XEX_HEADER_FILE_FORMAT_INFO));
    auto entry = *reinterpret_cast<const uint32_t*>(getOptHeaderPtr(loadResult.data(), XEX_HEADER_ENTRY_POINT));
    ByteSwapInplace(entry);

#if defined(__SWITCH__)
    LOGFN("Switch XEX header: headerSize=0x{:X}, securityOffset=0x{:X}, load=0x{:08X}, imageSize=0x{:X}, compression={}, entry=0x{:08X}",
        static_cast<uint32_t>(header->headerSize),
        static_cast<uint32_t>(header->securityOffset),
        static_cast<uint32_t>(security->loadAddress),
        static_cast<uint32_t>(security->imageSize),
        static_cast<uint16_t>(fileFormatInfo->compressionType),
        entry);
#endif

    auto srcData = loadResult.data() + header->headerSize;
    auto destData = reinterpret_cast<uint8_t*>(g_memory.Translate(security->loadAddress));

    if (fileFormatInfo->compressionType == XEX_COMPRESSION_NONE)
    {
        memcpy(destData, srcData, security->imageSize);
    }
    else if (fileFormatInfo->compressionType == XEX_COMPRESSION_BASIC)
    {
        auto* blocks = reinterpret_cast<const Xex2FileBasicCompressionBlock*>(fileFormatInfo + 1);
        const size_t numBlocks = (fileFormatInfo->infoSize / sizeof(Xex2FileBasicCompressionInfo)) - 1;

        for (size_t i = 0; i < numBlocks; i++)
        {
            memcpy(destData, srcData, blocks[i].dataSize);

            srcData += blocks[i].dataSize;
            destData += blocks[i].dataSize;

            memset(destData, 0, blocks[i].zeroSize);
            destData += blocks[i].zeroSize;
        }
    }
    else
    {
#if defined(__SWITCH__)
        LOGFN_ERROR("Switch unsupported XEX compression: {}", static_cast<uint16_t>(fileFormatInfo->compressionType));
#endif
        assert(false && "Unknown compression type.");
    }

    auto res = reinterpret_cast<const Xex2ResourceInfo*>(getOptHeaderPtr(loadResult.data(), XEX_HEADER_RESOURCE_INFO));

    g_xdbfWrapper = XDBFWrapper((uint8_t*)g_memory.Translate(res->offset.get()), res->sizeOfData);

#if defined(__SWITCH__)
    LOGFN("Switch module ready: entry=0x{:08X}, load=0x{:08X}, imageSize=0x{:X}, compression={}, xdbf=0x{:08X}+0x{:X}",
        entry,
        static_cast<uint32_t>(security->loadAddress),
        static_cast<uint32_t>(security->imageSize),
        static_cast<uint16_t>(fileFormatInfo->compressionType),
        static_cast<uint32_t>(res->offset.get()),
        static_cast<uint32_t>(res->sizeOfData));
#endif

    return entry;
}

#ifdef __x86_64__
__attribute__((constructor(101), target("no-avx,no-avx2"), noinline))
void init()
{
    uint32_t eax, ebx, ecx, edx;

    // Execute CPUID for processor info and feature bits.
    __get_cpuid(1, &eax, &ebx, &ecx, &edx);

    // Check for AVX support.
    if ((ecx & (1 << 28)) == 0)
    {
        printf("[*] CPU does not support the AVX instruction set.\n");

#ifdef _WIN32
        MessageBoxA(nullptr, "Your CPU does not meet the minimum system requirements.", "Unleashed Recompiled", MB_ICONERROR);
#endif

        std::_Exit(1);
    }
}
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    timeBeginPeriod(1);
#endif

    os::process::CheckConsole();

    if (!os::registry::Init())
        LOGN_WARNING("OS does not support registry.");

    os::logger::Init();

    PreloadContext preloadContext;
    preloadContext.PreloadExecutable();

    bool forceInstaller = false;
    bool forceDLCInstaller = false;
    bool useDefaultWorkingDirectory = false;
    bool forceInstallationCheck = false;
    bool graphicsApiRetry = false;
    const char *sdlVideoDriver = nullptr;

    for (uint32_t i = 1; i < argc; i++)
    {
        forceInstaller = forceInstaller || (strcmp(argv[i], "--install") == 0);
        forceDLCInstaller = forceDLCInstaller || (strcmp(argv[i], "--install-dlc") == 0);
        useDefaultWorkingDirectory = useDefaultWorkingDirectory || (strcmp(argv[i], "--use-cwd") == 0);
        forceInstallationCheck = forceInstallationCheck || (strcmp(argv[i], "--install-check") == 0);
        graphicsApiRetry = graphicsApiRetry || (strcmp(argv[i], "--graphics-api-retry") == 0);

        if (strcmp(argv[i], "--sdl-video-driver") == 0)
        {
            if ((i + 1) < argc)
                sdlVideoDriver = argv[++i];
            else
                LOGN_WARNING("No argument was specified for --sdl-video-driver. Option will be ignored.");
        }
    }

    if (!useDefaultWorkingDirectory)
    {
        // Set the current working directory to the executable's path.
        std::error_code ec;
        std::filesystem::current_path(os::process::GetExecutableRoot(), ec);
    }

    Config::Load();

    if (forceInstallationCheck)
    {
        // Create the console to show progress to the user, otherwise it will seem as if the game didn't boot at all.
        os::process::ShowConsole();

        Journal journal;
        double lastProgressMiB = 0.0;
        double lastTotalMib = 0.0;
        Installer::checkInstallIntegrity(GAME_INSTALL_DIRECTORY, journal, [&]()
        {
            constexpr double MiBDivisor = 1024.0 * 1024.0;
            constexpr double MiBProgressThreshold = 128.0;
            double progressMiB = double(journal.progressCounter) / MiBDivisor;
            double totalMiB = double(journal.progressTotal) / MiBDivisor;
            if (journal.progressCounter > 0)
            {
                if ((progressMiB - lastProgressMiB) > MiBProgressThreshold)
                {
                    fprintf(stdout, "Checking files: %0.2f MiB / %0.2f MiB\n", progressMiB, totalMiB);
                    lastProgressMiB = progressMiB;
                }
            }
            else
            {
                if ((totalMiB - lastTotalMib) > MiBProgressThreshold)
                {
                    fprintf(stdout, "Scanning files: %0.2f MiB\n", totalMiB);
                    lastTotalMib = totalMiB;
                }
            }

            return true;
        });

        char resultText[512];
        uint32_t messageBoxStyle;
        if (journal.lastResult == Journal::Result::Success)
        {
            snprintf(resultText, sizeof(resultText), "%s", Localise("IntegrityCheck_Success").c_str());
            fprintf(stdout, "%s\n", resultText);
            messageBoxStyle = SDL_MESSAGEBOX_INFORMATION;
        }
        else
        {
            snprintf(resultText, sizeof(resultText), Localise("IntegrityCheck_Failed").c_str(), journal.lastErrorMessage.c_str());
            fprintf(stderr, "%s\n", resultText);
            messageBoxStyle = SDL_MESSAGEBOX_ERROR;
        }

        SDL_ShowSimpleMessageBox(messageBoxStyle, GameWindow::GetTitle(), resultText, GameWindow::s_pWindow);
        std::_Exit(int(journal.lastResult));
    }

#if defined(_WIN32) && defined(UNLEASHED_RECOMP_D3D12)
    for (auto& dll : g_D3D12RequiredModules)
    {
        if (!std::filesystem::exists(g_executableRoot / dll))
        {
            char text[512];
            snprintf(text, sizeof(text), Localise("System_Win32_MissingDLLs").c_str(), dll.data());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), text, GameWindow::s_pWindow);
            std::_Exit(1);
        }
    }
#endif

    // Check the time since the last time an update was checked. Store the new time if the difference is more than six hours.
    constexpr double TimeBetweenUpdateChecksInSeconds = 6 * 60 * 60;
    time_t timeNow = std::time(nullptr);
    double timeDifferenceSeconds = difftime(timeNow, Config::LastChecked);
    if (timeDifferenceSeconds > TimeBetweenUpdateChecksInSeconds)
    {
        UpdateChecker::initialize();
        UpdateChecker::start();
        Config::LastChecked = timeNow;
        Config::Save();
    }

    if (Config::ShowConsole)
        os::process::ShowConsole();

    HostStartup();

#if defined(__SWITCH__)
    LOGN("Switch HostStartup completed");
#endif

    std::filesystem::path modulePath;
    bool isGameInstalled = Installer::checkGameInstall(GetGamePath(), modulePath);
    bool runInstallerWizard = forceInstaller || forceDLCInstaller || !isGameInstalled;

#if defined(__SWITCH__)
    LOGFN("Switch install check: installed={}, runInstaller={}, module='{}'",
        isGameInstalled,
        runInstallerWizard,
        modulePath.string());
#endif

    if (runInstallerWizard)
    {
#if defined(__SWITCH__)
        LOGN("Switch creating host video device for installer");
#endif

        if (!Video::CreateHostDevice(sdlVideoDriver, graphicsApiRetry))
        {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), Localise("Video_BackendError").c_str(), GameWindow::s_pWindow);
            std::_Exit(1);
        }

#if defined(__SWITCH__)
        LOGN("Switch host video device ready for installer");
#endif

        if (!InstallerWizard::Run(GetGamePath(), isGameInstalled && forceDLCInstaller))
        {
            std::_Exit(0);
        }

        isGameInstalled = Installer::checkGameInstall(GetGamePath(), modulePath);
        if (!isGameInstalled)
        {
            constexpr auto message = "Installation completed, but installed game files could not be found.";
            LOGN_ERROR(message);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), message, GameWindow::s_pWindow);
            std::_Exit(1);
        }
    }

    ModLoader::Init();

#if defined(__SWITCH__)
    LOGN("Switch ModLoader initialized");
#endif

    if (!PersistentStorageManager::LoadBinary())
        LOGFN_ERROR("Failed to load persistent storage binary... (status code {})", (int)PersistentStorageManager::BinStatus);

#if defined(__SWITCH__)
    LOGN("Switch persistent storage step completed");
#endif

    KiSystemStartup();

#if defined(__SWITCH__)
    LOGN("Switch kernel startup completed");
#endif

    uint32_t entry = LdrLoadModule(modulePath);

    if (!runInstallerWizard)
    {
#if defined(__SWITCH__)
        LOGN("Switch creating host video device for game");
#endif

        if (!Video::CreateHostDevice(sdlVideoDriver, graphicsApiRetry))
        {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), Localise("Video_BackendError").c_str(), GameWindow::s_pWindow);
            std::_Exit(1);
        }

#if defined(__SWITCH__)
        LOGN("Switch host video device ready for game");
#endif
    }

    Video::StartPipelinePrecompilation();

#if defined(__SWITCH__)
    LOGFN("Switch starting guest entry: 0x{:08X}", entry);
#endif

    GuestThread::Start({ entry, 0, 0 });

#if defined(__SWITCH__)
    LOGN("Switch guest entry returned");
#endif

    return 0;
}

GUEST_FUNCTION_STUB(__imp__vsprintf);
GUEST_FUNCTION_STUB(__imp___vsnprintf);
GUEST_FUNCTION_STUB(__imp__sprintf);
GUEST_FUNCTION_STUB(__imp___snprintf);
GUEST_FUNCTION_STUB(__imp___snwprintf);
GUEST_FUNCTION_STUB(__imp__vswprintf);
GUEST_FUNCTION_STUB(__imp___vscwprintf);
GUEST_FUNCTION_STUB(__imp__swprintf);
