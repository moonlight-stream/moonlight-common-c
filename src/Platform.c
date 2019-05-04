#define _GNU_SOURCE

#include "PlatformThreads.h"
#include "Platform.h"

#include <enet/enet.h>

// The maximum amount of time before observing an interrupt
// in PltSleepMsInterruptible().
#define INTERRUPT_PERIOD_MS 50

int initializePlatformSockets(void);
void cleanupPlatformSockets(void);

struct thread_context {
    ThreadEntry entry;
    void* context;
    const char* name;
#if defined(__vita__)
    PLT_THREAD* thread;
#endif
};

static int activeThreads = 0;
static int activeMutexes = 0;
static int activeEvents = 0;

#if defined(LC_WINDOWS)

#pragma pack(push, 8)
typedef struct tagTHREADNAME_INFO
{
    DWORD dwType; // Must be 0x1000.
    LPCSTR szName; // Pointer to name (in user addr space).
    DWORD dwThreadID; // Thread ID (-1=caller thread).
    DWORD dwFlags; // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

typedef HRESULT (WINAPI *SetThreadDescription_t)(HANDLE, PCWSTR);

void setThreadNameWin32(const char* name) {
    HMODULE hKernel32;
    SetThreadDescription_t setThreadDescriptionFunc;

    // This function is only supported on Windows 10 RS1 and later
    hKernel32 = LoadLibraryA("kernel32.dll");
    setThreadDescriptionFunc = (SetThreadDescription_t)GetProcAddress(hKernel32, "SetThreadDescription");
    if (setThreadDescriptionFunc != NULL) {
        WCHAR nameW[16];
        size_t chars;

        mbstowcs_s(&chars, nameW, ARRAYSIZE(nameW), name, _TRUNCATE);
        setThreadDescriptionFunc(GetCurrentThread(), nameW);
    }
    FreeLibrary(hKernel32);

#ifdef _MSC_VER
    // This method works on legacy OSes and older tools not updated to use SetThreadDescription yet,
    // but it's only safe on MSVC with SEH
    if (IsDebuggerPresent()) {
        THREADNAME_INFO info;
        info.dwType = 0x1000;
        info.szName = name;
        info.dwThreadID = (DWORD)-1;
        info.dwFlags = 0;
        __try {
            RaiseException(0x406D1388, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // Nothing
        }
    }
#endif
}

DWORD WINAPI ThreadProc(LPVOID lpParameter) {
    struct thread_context* ctx = (struct thread_context*)lpParameter;
#elif defined(__vita__)
int ThreadProc(SceSize args, void *argp) {
    struct thread_context* ctx = (struct thread_context*)argp;
#else
void* ThreadProc(void* context) {
    struct thread_context* ctx = (struct thread_context*)context;
#endif

#if defined(LC_WINDOWS)
    setThreadNameWin32(ctx->name);
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), ctx->name);
#endif

    ctx->entry(ctx->context);

#if defined(__vita__)
    ctx->thread->alive = 0;
#else
    free(ctx);
#endif

#if defined(LC_WINDOWS) || defined(__vita__)
    return 0;
#else
    return NULL;
#endif
}

void PltSleepMs(int ms) {
#if defined(LC_WINDOWS)
    WaitForSingleObjectEx(GetCurrentThread(), ms, FALSE);
#elif defined(__vita__)
    sceKernelDelayThread(ms * 1000);
#else
    useconds_t usecs = ms * 1000;
    usleep(usecs);
#endif
}

void PltSleepMsInterruptible(PLT_THREAD* thread, int ms) {
    while (ms > 0 && !PltIsThreadInterrupted(thread)) {
        int msToSleep = ms < INTERRUPT_PERIOD_MS ? ms : INTERRUPT_PERIOD_MS;
        PltSleepMs(msToSleep);
        ms -= msToSleep;
    }
}

int PltCreateMutex(PLT_MUTEX* mutex) {
#if defined(LC_WINDOWS)
    *mutex = CreateMutexEx(NULL, NULL, 0, MUTEX_ALL_ACCESS);
    if (!*mutex) {
        return -1;
    }
#elif defined(__vita__)
    *mutex = sceKernelCreateMutex("", 0, 0, NULL);
    if (*mutex < 0) {
        return -1;
    }
#else
    int err = pthread_mutex_init(mutex, NULL);
    if (err != 0) {
        return err;
    }
#endif
    activeMutexes++;
    return 0;
}

void PltDeleteMutex(PLT_MUTEX* mutex) {
    activeMutexes--;
#if defined(LC_WINDOWS)
    CloseHandle(*mutex);
#elif defined(__vita__)
    sceKernelDeleteMutex(*mutex);
#else
    pthread_mutex_destroy(mutex);
#endif
}

void PltLockMutex(PLT_MUTEX* mutex) {
#if defined(LC_WINDOWS)
    int err;
    err = WaitForSingleObjectEx(*mutex, INFINITE, FALSE);
    if (err != WAIT_OBJECT_0) {
        LC_ASSERT(FALSE);
    }
#elif defined(__vita__)
    sceKernelLockMutex(*mutex, 1, NULL);
#else
    pthread_mutex_lock(mutex);
#endif
}

void PltUnlockMutex(PLT_MUTEX* mutex) {
#if defined(LC_WINDOWS)
    ReleaseMutex(*mutex);
#elif defined(__vita__)
    sceKernelUnlockMutex(*mutex, 1);
#else
    pthread_mutex_unlock(mutex);
#endif
}

void PltJoinThread(PLT_THREAD* thread) {
    LC_ASSERT(thread->cancelled);
#if defined(LC_WINDOWS)
    WaitForSingleObjectEx(thread->handle, INFINITE, FALSE);
#elif defined(__vita__)
    while(thread->alive) {
        PltSleepMs(10);
    }
    if (thread->context != NULL)
        free(thread->context);
#else
    pthread_join(thread->thread, NULL);
#endif
}

void PltCloseThread(PLT_THREAD* thread) {
    activeThreads--;
#if defined(LC_WINDOWS)
    CloseHandle(thread->handle);
#elif defined(__vita__)
    sceKernelDeleteThread(thread->handle);
#endif
}

int PltIsThreadInterrupted(PLT_THREAD* thread) {
    return thread->cancelled;
}

void PltInterruptThread(PLT_THREAD* thread) {
    thread->cancelled = 1;
}

int PltCreateThread(const char* name, ThreadEntry entry, void* context, PLT_THREAD* thread) {
    struct thread_context* ctx;

    ctx = (struct thread_context*)malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return -1;
    }

    ctx->entry = entry;
    ctx->context = context;
    ctx->name = name;
    
    thread->cancelled = 0;

#if defined(LC_WINDOWS)
    {
        thread->handle = CreateThread(NULL, 0, ThreadProc, ctx, 0, NULL);
        if (thread->handle == NULL) {
            free(ctx);
            return -1;
        }
    }
#elif defined(__vita__)
    {
        thread->alive = 1;
        thread->context = ctx;
        ctx->thread = thread;
        thread->handle = sceKernelCreateThread(name, ThreadProc, 0, 0x40000, 0, 0, NULL);
        if (thread->handle < 0) {
            free(ctx);
            return -1;
        }
        sceKernelStartThread(thread->handle, sizeof(struct thread_context), ctx);
    }
#else
    {
        int err = pthread_create(&thread->thread, NULL, ThreadProc, ctx);
        if (err != 0) {
            free(ctx);
            return err;
        }
    }
#endif

    activeThreads++;

    return 0;
}

int PltCreateEvent(PLT_EVENT* event) {
#if defined(LC_WINDOWS)
    *event = CreateEventEx(NULL, NULL, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
    if (!*event) {
        return -1;
    }
#elif defined(__vita__)
    event->mutex = sceKernelCreateMutex("", 0, 0, NULL);
    if (event->mutex < 0) {
        return -1;
    }
    event->cond = sceKernelCreateCond("", 0, event->mutex, NULL);
    if (event->cond < 0) {
        sceKernelDeleteMutex(event->mutex);
        return -1;
    }
    event->signalled = 0;
#else
    pthread_mutex_init(&event->mutex, NULL);
    pthread_cond_init(&event->cond, NULL);
    event->signalled = 0;
#endif
    activeEvents++;
    return 0;
}

void PltCloseEvent(PLT_EVENT* event) {
    activeEvents--;
#if defined(LC_WINDOWS)
    CloseHandle(*event);
#elif defined(__vita__)
    sceKernelDeleteCond(event->cond);
    sceKernelDeleteMutex(event->mutex);
#else
    pthread_mutex_destroy(&event->mutex);
    pthread_cond_destroy(&event->cond);
#endif
}

void PltSetEvent(PLT_EVENT* event) {
#if defined(LC_WINDOWS)
    SetEvent(*event);
#elif defined(__vita__)
    sceKernelLockMutex(event->mutex, 1, NULL);
    event->signalled = 1;
    sceKernelUnlockMutex(event->mutex, 1);
    sceKernelSignalCondAll(event->cond);
#else
    pthread_mutex_lock(&event->mutex);
    event->signalled = 1;
    pthread_mutex_unlock(&event->mutex);
    pthread_cond_broadcast(&event->cond);
#endif
}

void PltClearEvent(PLT_EVENT* event) {
#if defined(LC_WINDOWS)
    ResetEvent(*event);
#else
    event->signalled = 0;
#endif
}

int PltWaitForEvent(PLT_EVENT* event) {
#if defined(LC_WINDOWS)
    DWORD error;

    error = WaitForSingleObjectEx(*event, INFINITE, FALSE);
    if (error == WAIT_OBJECT_0) {
        return PLT_WAIT_SUCCESS;
    }
    else {
        LC_ASSERT(0);
        return -1;
    }
#elif defined(__vita__)
    sceKernelLockMutex(event->mutex, 1, NULL);
    while (!event->signalled) {
        sceKernelWaitCond(event->cond, NULL);
    }
    sceKernelUnlockMutex(event->mutex, 1);

    return PLT_WAIT_SUCCESS;
#else
    pthread_mutex_lock(&event->mutex);
    while (!event->signalled) {
        pthread_cond_wait(&event->cond, &event->mutex);
    }
    pthread_mutex_unlock(&event->mutex);
    
    return PLT_WAIT_SUCCESS;
#endif
}

uint64_t PltGetMillis(void) {
#if defined(LC_WINDOWS)
    return GetTickCount64();
#elif HAVE_CLOCK_GETTIME
    struct timespec tv;
    
    clock_gettime(CLOCK_MONOTONIC, &tv);
    
    return (tv.tv_sec * 1000) + (tv.tv_nsec / 1000000);
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
#endif
}

int initializePlatform(void) {
    int err;

    err = initializePlatformSockets();
    if (err != 0) {
        return err;
    }
    
    err = enet_initialize();
    if (err != 0) {
        return err;
    }

	return 0;
}

void cleanupPlatform(void) {
    cleanupPlatformSockets();
    
    enet_deinitialize();

    LC_ASSERT(activeThreads == 0);
    LC_ASSERT(activeMutexes == 0);
    LC_ASSERT(activeEvents == 0);
}
