#pragma once

#include "Platform.h"

typedef void (*ThreadEntry)(void *context);

#if defined(LC_WINDOWS) || defined(LC_WINDOWS_PHONE)
typedef struct _PLT_THREAD {
	HANDLE handle;
	int cancelled;
	DWORD tid;
	HANDLE termevent;

	struct _PLT_THREAD *next;
} PLT_THREAD;
typedef HANDLE PLT_MUTEX;
typedef HANDLE PLT_EVENT;
#elif defined (LC_POSIX)
typedef pthread_t PLT_THREAD;
typedef pthread_mutex_t PLT_MUTEX;
typedef struct _PLT_EVENT {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int signalled;
} PLT_EVENT;
#else
#error Unsupported platform
#endif

#ifdef LC_WINDOWS_PHONE
WINBASEAPI
_Ret_maybenull_
HANDLE
WINAPI
CreateThread(
	_In_opt_ LPSECURITY_ATTRIBUTES lpThreadAttributes,
	_In_ SIZE_T dwStackSize,
	_In_ LPTHREAD_START_ROUTINE lpStartAddress,
	_In_opt_ __drv_aliasesMem LPVOID lpParameter,
	_In_ DWORD dwCreationFlags,
	_Out_opt_ LPDWORD lpThreadId
);
#endif

int initializePlatformThreads(void);
void cleanupPlatformThreads(void);

int PltCreateMutex(PLT_MUTEX *mutex);
void PltDeleteMutex(PLT_MUTEX *mutex);
void PltLockMutex(PLT_MUTEX *mutex);
void PltUnlockMutex(PLT_MUTEX *mutex);

int PltCreateThread(ThreadEntry entry, void* context, PLT_THREAD *thread);
void PltCloseThread(PLT_THREAD *thread);
void PltInterruptThread(PLT_THREAD *thread);
int PltIsThreadInterrupted(PLT_THREAD *thread);
void PltJoinThread(PLT_THREAD *thread);

int PltCreateEvent(PLT_EVENT *event);
void PltCloseEvent(PLT_EVENT *event);
void PltSetEvent(PLT_EVENT *event);
void PltClearEvent(PLT_EVENT *event);
int PltWaitForEvent(PLT_EVENT *event);

#define PLT_WAIT_SUCCESS 0
#define PLT_WAIT_INTERRUPTED 1

void PltSleepMs(int ms);