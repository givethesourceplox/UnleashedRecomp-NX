#pragma once

#if defined(__SWITCH__)
#include <os/switch_atomic_wait.h>
#endif

#ifndef _WIN32
#define MEM_COMMIT  0x00001000  
#define MEM_RESERVE 0x00002000  
#endif

struct Memory
{
    uint8_t* base{};

    Memory();

#if defined(__SWITCH__)
    bool CommitRange(size_t offset, size_t size) noexcept;
    bool CommitHostRange(const void* host, size_t size) noexcept;
#endif

    bool IsInMemoryRange(const void* host) const noexcept
    {
        return host >= base && host < (base + PPC_MEMORY_SIZE);
    }

    void* Translate(size_t offset) const noexcept
    {
        if (offset)
            assert(offset < PPC_MEMORY_SIZE);

        return base + offset;
    }

    uint32_t MapVirtual(const void* host) const noexcept
    {
        if (host)
            assert(IsInMemoryRange(host));

        return static_cast<uint32_t>(static_cast<const uint8_t*>(host) - base);
    }

    PPCFunc* FindFunction(uint32_t guest) const noexcept
    {
        return PPC_LOOKUP_FUNC(base, guest);
    }

    void InsertFunction(uint32_t guest, PPCFunc* host)
    {
        PPC_LOOKUP_FUNC(base, guest) = host;
    }

#if defined(__SWITCH__)
    struct SwitchCommitChunk
    {
        size_t offset{};
        size_t size{};
        void* backing{};
        void* codeAlias{};
    };

    void* reservation{};
    std::vector<uint8_t> committedPages;
    std::vector<SwitchCommitChunk> switchCommitChunks;
    mutable std::mutex commitMutex;
    uint64_t switchAliasBase{};
    uint64_t switchAliasSize{};
    uint64_t switchAslrBase{};
    uint64_t switchAslrSize{};
    uint64_t switchHeapBase{};
    uint64_t switchHeapSize{};
    uint64_t switchSelectedBase{};
    uint64_t switchCommitFailureOffset{};
    uint64_t switchCommitFailureAddress{};
    uint64_t switchCommitFailureMemoryBase{};
    uint64_t switchCommitFailureMemorySize{};
    uint32_t switchCommitFailureMemoryType{};
    uint32_t switchCommitFailureMemoryAttr{};
    uint32_t switchCommitFailureMemoryPerm{};
    uint32_t switchCommitFailurePageInfo{};
    uint32_t switchInitResult{};
    const char* switchInitFailureReason{"not started"};
    bool switchUsingAliasBaseFallback{};
#endif
};

extern "C" void* MmGetHostAddress(uint32_t ptr);
extern Memory g_memory;
