#include <stdafx.h>
#include <cstdint>
#include "heap.h"
#include "memory.h"
#include "function.h"

constexpr size_t USER_HEAP_BEGIN = 0x20000;
constexpr size_t RESERVED_BEGIN = 0x7FEA0000;
constexpr size_t RESERVED_END = 0xA0000000;
constexpr size_t SWITCH_ADDRESS_SPACE_SIZE = 0x100000000ull;
constexpr size_t USER_HEAP_SIZE = RESERVED_BEGIN - USER_HEAP_BEGIN;
constexpr size_t PHYSICAL_HEAP_SIZE = SWITCH_ADDRESS_SPACE_SIZE - RESERVED_END;

#if defined(__SWITCH__)
namespace
{
constexpr size_t SWITCH_HEAP_INITIAL_COMMIT = 1 * 1024 * 1024;
constexpr size_t SWITCH_HEAP_COMMIT_GRANULARITY = 16 * 1024 * 1024;

size_t AlignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

size_t RoundHeapFragmentSize(size_t size)
{
    size = std::max<size_t>(1, size) + O1HEAP_ALIGNMENT;

    size_t rounded = 1;
    while (rounded < size)
        rounded <<= 1;

    return rounded;
}

bool EnsureCommittedPrefix(size_t rangeStart, size_t rangeSize, size_t& committedPrefix, size_t requestedPrefix)
{
    requestedPrefix = std::min(rangeSize, AlignUp(requestedPrefix, SWITCH_HEAP_COMMIT_GRANULARITY));

    if (requestedPrefix <= committedPrefix)
        return true;

    if (!g_memory.CommitRange(rangeStart + committedPrefix, requestedPrefix - committedPrefix))
        return false;

    committedPrefix = requestedPrefix;
    return true;
}
}
#endif

void Heap::Init()
{
#if defined(__SWITCH__)
    committedHeapPrefix = 0;
    committedPhysicalHeapPrefix = 0;
    touchedHeapPrefix = 0;
    touchedPhysicalHeapPrefix = 0;

    if (!EnsureCommittedPrefix(USER_HEAP_BEGIN, USER_HEAP_SIZE, committedHeapPrefix, SWITCH_HEAP_INITIAL_COMMIT) ||
        !EnsureCommittedPrefix(RESERVED_END, PHYSICAL_HEAP_SIZE, committedPhysicalHeapPrefix, SWITCH_HEAP_INITIAL_COMMIT))
    {
        heap = nullptr;
        physicalHeap = nullptr;
        return;
    }
#endif

    heap = o1heapInit(g_memory.Translate(USER_HEAP_BEGIN), USER_HEAP_SIZE);
    physicalHeap = o1heapInit(g_memory.Translate(RESERVED_END), PHYSICAL_HEAP_SIZE);
}

void* Heap::Alloc(size_t size)
{
    size = std::max<size_t>(1, size);

    std::lock_guard lock(mutex);
    if (heap == nullptr)
        return nullptr;

#if defined(__SWITCH__)
    const size_t touchedEnd = touchedHeapPrefix + RoundHeapFragmentSize(size);
    if (!EnsureCommittedPrefix(USER_HEAP_BEGIN, USER_HEAP_SIZE, committedHeapPrefix, touchedEnd))
    {
        return nullptr;
    }
    touchedHeapPrefix = std::min(USER_HEAP_SIZE, touchedEnd);
#endif

    void* ptr = o1heapAllocate(heap, size);

#if defined(__SWITCH__)
    if (ptr != nullptr && !g_memory.CommitHostRange(static_cast<uint8_t*>(ptr) - O1HEAP_ALIGNMENT, size + O1HEAP_ALIGNMENT))
    {
        o1heapFree(heap, ptr);
        return nullptr;
    }
#endif

    return ptr;
}

void* Heap::AllocPhysical(size_t size, size_t alignment)
{
    size = std::max<size_t>(1, size);
    alignment = alignment == 0 ? 0x1000 : std::max<size_t>(16, alignment);
    const size_t allocationSize = size + alignment;

    std::lock_guard lock(physicalMutex);
    if (physicalHeap == nullptr)
        return nullptr;

#if defined(__SWITCH__)
    const size_t touchedEnd = touchedPhysicalHeapPrefix + RoundHeapFragmentSize(allocationSize);
    if (!EnsureCommittedPrefix(RESERVED_END, PHYSICAL_HEAP_SIZE, committedPhysicalHeapPrefix, touchedEnd))
    {
        return nullptr;
    }
    touchedPhysicalHeapPrefix = std::min(PHYSICAL_HEAP_SIZE, touchedEnd);
#endif

    void* ptr = o1heapAllocate(physicalHeap, allocationSize);
    if (ptr == nullptr)
        return nullptr;

#if defined(__SWITCH__)
    if (!g_memory.CommitHostRange(ptr, allocationSize))
    {
        o1heapFree(physicalHeap, ptr);
        return nullptr;
    }
#endif

    size_t aligned = ((size_t)ptr + alignment) & ~(alignment - 1);

    *((void**)aligned - 1) = ptr;
    *((size_t*)aligned - 2) = size + O1HEAP_ALIGNMENT;

    return (void*)aligned;
}

void Heap::Free(void* ptr)
{
    if (ptr == nullptr)
        return;

    if (physicalHeap != nullptr && reinterpret_cast<uintptr_t>(ptr) >= reinterpret_cast<uintptr_t>(physicalHeap))
    {
        std::lock_guard lock(physicalMutex);
        o1heapFree(physicalHeap, *((void**)ptr - 1));
    }
    else
    {
        std::lock_guard lock(mutex);
        if (heap != nullptr)
            o1heapFree(heap, ptr);
    }
}

size_t Heap::Size(void* ptr)
{
    if (ptr)
        return *((size_t*)ptr - 2) - O1HEAP_ALIGNMENT; // relies on fragment header in o1heap.c

    return 0;
}

uint32_t RtlAllocateHeap(uint32_t heapHandle, uint32_t flags, uint32_t size)
{
    void* ptr = g_userHeap.Alloc(size);
    assert(ptr);
    if (ptr == nullptr)
        return 0;

    if ((flags & 0x8) != 0)
        memset(ptr, 0, size);

    return g_memory.MapVirtual(ptr);
}

uint32_t RtlReAllocateHeap(uint32_t heapHandle, uint32_t flags, uint32_t memoryPointer, uint32_t size)
{
    void* ptr = g_userHeap.Alloc(size);
    assert(ptr);
    if (ptr == nullptr)
        return 0;

    if ((flags & 0x8) != 0)
        memset(ptr, 0, size);

    if (memoryPointer != 0)
    {
        void* oldPtr = g_memory.Translate(memoryPointer);
        memcpy(ptr, oldPtr, std::min<size_t>(size, g_userHeap.Size(oldPtr)));
        g_userHeap.Free(oldPtr);
    }

    return g_memory.MapVirtual(ptr);
}

uint32_t RtlFreeHeap(uint32_t heapHandle, uint32_t flags, uint32_t memoryPointer)
{
    if (memoryPointer != NULL)
        g_userHeap.Free(g_memory.Translate(memoryPointer));

    return true;
}

uint32_t RtlSizeHeap(uint32_t heapHandle, uint32_t flags, uint32_t memoryPointer)
{
    if (memoryPointer != NULL)
        return (uint32_t)g_userHeap.Size(g_memory.Translate(memoryPointer));

    return 0;
}

uint32_t XAllocMem(uint32_t size, uint32_t flags)
{
    void* ptr = (flags & 0x80000000) != 0 ?
        g_userHeap.AllocPhysical(size, (1ull << ((flags >> 24) & 0xF))) :
        g_userHeap.Alloc(size);

    assert(ptr);
    if (ptr == nullptr)
        return 0;

    if ((flags & 0x40000000) != 0)
        memset(ptr, 0, size);

    return g_memory.MapVirtual(ptr);
}

void XFreeMem(uint32_t baseAddress, uint32_t flags)
{
    if (baseAddress != NULL)
        g_userHeap.Free(g_memory.Translate(baseAddress));
}

GUEST_FUNCTION_STUB(sub_82BD7788); // HeapCreate
GUEST_FUNCTION_STUB(sub_82BD9250); // HeapDestroy

GUEST_FUNCTION_HOOK(sub_82BD7D30, RtlAllocateHeap);
GUEST_FUNCTION_HOOK(sub_82BD8600, RtlFreeHeap);
GUEST_FUNCTION_HOOK(sub_82BD88F0, RtlReAllocateHeap);
GUEST_FUNCTION_HOOK(sub_82BD6FD0, RtlSizeHeap);

GUEST_FUNCTION_HOOK(sub_831CC9C8, XAllocMem);
GUEST_FUNCTION_HOOK(sub_831CCA60, XFreeMem);
