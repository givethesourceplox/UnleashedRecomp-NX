#pragma once

#ifdef _WIN32

struct RecompMutex : CRITICAL_SECTION
{
    RecompMutex()
    {
        InitializeCriticalSection(this);
    }
    ~RecompMutex()
    {
        DeleteCriticalSection(this);
    }

    void lock()
    {
        EnterCriticalSection(this);
    }

    void unlock()
    {
        LeaveCriticalSection(this);
    }
};

#else

using RecompMutex = std::mutex;

#endif

#if !defined(__SWITCH__)
using Mutex = RecompMutex;
#endif
