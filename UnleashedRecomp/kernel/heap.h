#pragma once

#include "mutex.h"

struct Heap
{
    RecompMutex mutex;
    O1HeapInstance* heap{};

    RecompMutex physicalMutex;
    O1HeapInstance* physicalHeap{};

#if defined(__SWITCH__)
    size_t committedHeapPrefix{};
    size_t committedPhysicalHeapPrefix{};
    size_t touchedHeapPrefix{};
    size_t touchedPhysicalHeapPrefix{};
#endif

    void Init();

    void* Alloc(size_t size);
    void* AllocPhysical(size_t size, size_t alignment);
    void Free(void* ptr);

    size_t Size(void* ptr);

    template<typename T, typename... Args>
    T* Alloc(Args&&... args)
    {
        T* obj = (T*)Alloc(sizeof(T));
        if (obj == nullptr)
            return nullptr;

        new (obj) T(std::forward<Args>(args)...);
        return obj;
    }

    template<typename T, typename... Args>
    T* AllocPhysical(Args&&... args)
    {
        T* obj = (T*)AllocPhysical(sizeof(T), alignof(T));
        if (obj == nullptr)
            return nullptr;

        new (obj) T(std::forward<Args>(args)...);
        return obj;
    }
};

extern Heap g_userHeap;
