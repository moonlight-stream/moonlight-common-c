#pragma once

#include "Limelight.h"
#include "Platform.h"

typedef void(*ThreadEntry)(void* context);

#if defined(LC_WINDOWS)
typedef HANDLE PLT_MUTEX;
typedef HANDLE PLT_EVENT;
typedef struct _PLT_THREAD {
    HANDLE handle;
    int cancelled;
} PLT_THREAD;
#elif defined(__vita__)
typedef int PLT_MUTEX;
typedef struct _PLT_EVENT {
    int mutex;
    int cond;
    int signalled;
} PLT_EVENT;
typedef struct _PLT_THREAD {
    int handle;
    int cancelled;
    void *context;
    int alive;
} PLT_THREAD;
#elif defined (LC_POSIX)
typedef pthread_mutex_t PLT_MUTEX;
typedef struct _PLT_EVENT {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int signalled;
} PLT_EVENT;
typedef struct _PLT_THREAD {
    pthread_t thread;
    int cancelled;
} PLT_THREAD;
#else
#error Unsupported platform
#endif

int PltCreateMutex(PLT_MUTEX* mutex);
void PltDeleteMutex(PLT_MUTEX* mutex);
void PltLockMutex(PLT_MUTEX* mutex);
void PltUnlockMutex(PLT_MUTEX* mutex);

int PltCreateThread(const char* name, ThreadEntry entry, void* context, PLT_THREAD* thread);
void PltCloseThread(PLT_THREAD* thread);
void PltInterruptThread(PLT_THREAD* thread);
int PltIsThreadInterrupted(PLT_THREAD* thread);
void PltJoinThread(PLT_THREAD* thread);

int PltCreateEvent(PLT_EVENT* event);
void PltCloseEvent(PLT_EVENT* event);
void PltSetEvent(PLT_EVENT* event);
void PltClearEvent(PLT_EVENT* event);
int PltWaitForEvent(PLT_EVENT* event);

void PltRunThreadProc(void);

#define PLT_WAIT_SUCCESS 0
#define PLT_WAIT_INTERRUPTED 1

void PltSleepMs(int ms);
void PltSleepMsInterruptible(PLT_THREAD* thread, int ms);
