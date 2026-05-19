#include <stdafx.h>
#include "memory.h"

#if defined(__SWITCH__)
#include <cstring>
#include <limits>
#include <switch.h>
#include <malloc.h>

namespace
{
constexpr size_t SWITCH_PAGE_SIZE = 0x1000;
constexpr size_t SWITCH_LOW_MEMORY_COMMIT_END = 0x20000;
constexpr size_t SWITCH_XMAIO_BEGIN = 0x7FEA0000;
constexpr size_t SWITCH_XMAIO_SIZE = 0x10000;
constexpr unsigned SWITCH_SVC_MAP_PROCESS_MEMORY = 0x74;
constexpr unsigned SWITCH_SVC_UNMAP_PROCESS_MEMORY = 0x75;
constexpr unsigned SWITCH_SVC_MAP_PROCESS_CODE_MEMORY = 0x77;
constexpr unsigned SWITCH_SVC_UNMAP_PROCESS_CODE_MEMORY = 0x78;

constexpr uintptr_t AlignUp(uintptr_t value, uintptr_t alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

constexpr uintptr_t AlignDown(uintptr_t value, uintptr_t alignment) noexcept
{
    return value & ~(alignment - 1);
}

bool AddOverflows(uintptr_t value, size_t addend) noexcept
{
    return value > std::numeric_limits<uintptr_t>::max() - addend;
}

void CaptureSwitchMemoryRegions(Memory& memory) noexcept
{
    svcGetInfo(&memory.switchAliasBase, InfoType_AliasRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&memory.switchAliasSize, InfoType_AliasRegionSize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&memory.switchAslrBase, InfoType_AslrRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&memory.switchAslrSize, InfoType_AslrRegionSize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&memory.switchHeapBase, InfoType_HeapRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&memory.switchHeapSize, InfoType_HeapRegionSize, CUR_PROCESS_HANDLE, 0);
}

bool HasSwitchProcessMemorySyscalls() noexcept
{
    return envIsSyscallHinted(SWITCH_SVC_MAP_PROCESS_MEMORY) &&
        envIsSyscallHinted(SWITCH_SVC_UNMAP_PROCESS_MEMORY) &&
        envIsSyscallHinted(SWITCH_SVC_MAP_PROCESS_CODE_MEMORY) &&
        envIsSyscallHinted(SWITCH_SVC_UNMAP_PROCESS_CODE_MEMORY);
}

void CaptureSwitchCommitFailure(Memory& memory, uintptr_t guestOffset, uintptr_t address, Result result) noexcept
{
    memory.switchInitResult = static_cast<uint32_t>(result);
    memory.switchCommitFailureOffset = guestOffset;
    memory.switchCommitFailureAddress = address;

    MemoryInfo info{};
    u32 pageInfo = 0;
    if (R_SUCCEEDED(svcQueryMemory(&info, &pageInfo, address)))
    {
        memory.switchCommitFailureMemoryBase = info.addr;
        memory.switchCommitFailureMemorySize = info.size;
        memory.switchCommitFailureMemoryType = info.type;
        memory.switchCommitFailureMemoryAttr = info.attr;
        memory.switchCommitFailureMemoryPerm = info.perm;
        memory.switchCommitFailurePageInfo = pageInfo;
    }
}

bool MapSwitchProcessMemoryRange(Memory& memory, size_t offset, size_t size) noexcept
{
    if (size == 0)
        return true;

    if (AddOverflows(offset, size))
        return false;

    const size_t alignedOffset = AlignDown(offset, SWITCH_PAGE_SIZE);
    const size_t alignedEnd = AlignUp(offset + size, SWITCH_PAGE_SIZE);
    const size_t alignedSize = alignedEnd - alignedOffset;
    const uintptr_t destination = reinterpret_cast<uintptr_t>(memory.base) + alignedOffset;

    void* backing = memalign(SWITCH_PAGE_SIZE, alignedSize);
    if (backing == nullptr)
    {
        memory.switchInitFailureReason = "Switch backing allocation failed";
        CaptureSwitchCommitFailure(memory, alignedOffset, destination, 0);
        return false;
    }

    std::memset(backing, 0, alignedSize);

    void* codeAlias = nullptr;
    bool codeAliasMapped = false;
    Result rc = 0;

    virtmemLock();
    codeAlias = virtmemFindCodeMemory(alignedSize, SWITCH_PAGE_SIZE);
    if (codeAlias != nullptr)
    {
        rc = svcMapProcessCodeMemory(envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), reinterpret_cast<uintptr_t>(backing), alignedSize);
        if (R_SUCCEEDED(rc))
        {
            codeAliasMapped = true;
            rc = svcSetProcessMemoryPermission(envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), alignedSize, Perm_Rw);
        }
    }
    virtmemUnlock();

    if (codeAlias == nullptr)
    {
        free(backing);
        memory.switchInitFailureReason = "virtmemFindCodeMemory failed";
        CaptureSwitchCommitFailure(memory, alignedOffset, destination, 0);
        return false;
    }

    if (R_FAILED(rc))
    {
        if (codeAliasMapped)
            svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), reinterpret_cast<uintptr_t>(backing), alignedSize);

        free(backing);
        memory.switchInitFailureReason = "code memory alias setup failed";
        CaptureSwitchCommitFailure(memory, alignedOffset, reinterpret_cast<uintptr_t>(codeAlias), rc);
        return false;
    }

    rc = svcMapProcessMemory(reinterpret_cast<void*>(destination), envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), alignedSize);
    if (R_FAILED(rc))
    {
        svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), reinterpret_cast<uintptr_t>(backing), alignedSize);
        free(backing);
        memory.switchInitFailureReason = "svcMapProcessMemory failed";
        CaptureSwitchCommitFailure(memory, alignedOffset, destination, rc);
        return false;
    }

    try
    {
        memory.switchCommitChunks.push_back({ alignedOffset, alignedSize, backing, codeAlias });
    }
    catch (...)
    {
        svcUnmapProcessMemory(reinterpret_cast<void*>(destination), envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), alignedSize);
        svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), reinterpret_cast<uintptr_t>(codeAlias), reinterpret_cast<uintptr_t>(backing), alignedSize);
        free(backing);
        memory.switchInitFailureReason = "commit chunk tracking allocation failed";
        CaptureSwitchCommitFailure(memory, alignedOffset, destination, 0);
        return false;
    }

    return true;
}
}
#endif

Memory::Memory()
{
#ifdef _WIN32
    base = (uint8_t*)VirtualAlloc((void*)0x100000000ull, PPC_MEMORY_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (base == nullptr)
        base = (uint8_t*)VirtualAlloc(nullptr, PPC_MEMORY_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (base == nullptr)
        return;

    DWORD oldProtect;
    VirtualProtect(base, 4096, PAGE_NOACCESS, &oldProtect);
#elif defined(__SWITCH__)
    CaptureSwitchMemoryRegions(*this);

    const size_t lookupTableSize = static_cast<size_t>(PPC_CODE_SIZE) * 2;
    const size_t imageAndLookupSize = static_cast<size_t>(PPC_IMAGE_SIZE) + lookupTableSize;

    if (!HasSwitchProcessMemorySyscalls())
    {
        switchInitFailureReason = "process memory syscalls are not hinted";
        return;
    }

    virtmemLock();
    base = static_cast<uint8_t*>(virtmemFindAslr(PPC_MEMORY_SIZE, SWITCH_PAGE_SIZE));
    if (base != nullptr)
        reservation = virtmemAddReservation(base, PPC_MEMORY_SIZE);
    virtmemUnlock();

    if (base == nullptr)
    {
        switchInitFailureReason = "virtmemFindAslr 4GB window failed";
        return;
    }

    switchSelectedBase = reinterpret_cast<uintptr_t>(base);

    if (reservation == nullptr)
    {
        switchInitFailureReason = "virtmemAddReservation failed";
        base = nullptr;
        return;
    }

    try
    {
        committedPages.assign(PPC_MEMORY_SIZE / SWITCH_PAGE_SIZE, 0);
    }
    catch (...)
    {
        switchInitFailureReason = "committed page table allocation failed";
        base = nullptr;
        return;
    }

    if (!CommitRange(SWITCH_PAGE_SIZE, SWITCH_LOW_MEMORY_COMMIT_END - SWITCH_PAGE_SIZE))
    {
        switchInitFailureReason = "initial low memory commit failed";
        base = nullptr;
        return;
    }

    if (!CommitRange(PPC_IMAGE_BASE, imageAndLookupSize))
    {
        switchInitFailureReason = "initial image/lookup commit failed";
        base = nullptr;
        return;
    }

    if (!CommitRange(SWITCH_XMAIO_BEGIN, SWITCH_XMAIO_SIZE))
    {
        switchInitFailureReason = "initial XMA IO commit failed";
        base = nullptr;
        return;
    }

    switchInitFailureReason = nullptr;
#else
    base = (uint8_t*)mmap((void*)0x100000000ull, PPC_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

    if (base == (uint8_t*)MAP_FAILED)
        base = (uint8_t*)mmap(NULL, PPC_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

    if (base == nullptr)
        return;

    mprotect(base, 4096, PROT_NONE);
#endif

    for (size_t i = 0; PPCFuncMappings[i].guest != 0; i++)
    {
        if (PPCFuncMappings[i].host != nullptr)
            InsertFunction(PPCFuncMappings[i].guest, PPCFuncMappings[i].host);
    }
}

#if defined(__SWITCH__)
bool Memory::CommitRange(size_t offset, size_t size) noexcept
{
    if (base == nullptr || size == 0)
        return base != nullptr;

    if (offset >= PPC_MEMORY_SIZE || size > PPC_MEMORY_SIZE - offset)
        return false;

    const size_t begin = AlignDown(offset, SWITCH_PAGE_SIZE);
    const size_t end = AlignUp(offset + size, SWITCH_PAGE_SIZE);

    std::lock_guard lock(commitMutex);

    size_t pageOffset = begin;
    while (pageOffset < end)
    {
        const size_t pageIndex = pageOffset / SWITCH_PAGE_SIZE;
        if (committedPages[pageIndex])
        {
            pageOffset += SWITCH_PAGE_SIZE;
            continue;
        }

        const size_t runStart = pageOffset;
        do
        {
            pageOffset += SWITCH_PAGE_SIZE;
        }
        while (pageOffset < end && !committedPages[pageOffset / SWITCH_PAGE_SIZE]);

        if (!MapSwitchProcessMemoryRange(*this, runStart, pageOffset - runStart))
            return false;

        for (size_t committedOffset = runStart; committedOffset < pageOffset; committedOffset += SWITCH_PAGE_SIZE)
            committedPages[committedOffset / SWITCH_PAGE_SIZE] = 1;
    }

    return true;
}

bool Memory::CommitHostRange(const void* host, size_t size) noexcept
{
    if (host == nullptr || size == 0)
        return true;

    if (!IsInMemoryRange(host))
        return false;

    return CommitRange(static_cast<const uint8_t*>(host) - base, size);
}
#endif

void* MmGetHostAddress(uint32_t ptr)
{
    return g_memory.Translate(ptr);
}
