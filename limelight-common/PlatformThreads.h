#include "Platform.h"

typedef void (*ThreadEntry)(void *context);

#ifdef _WIN32
typedef HANDLE PLT_THREAD;
typedef HANDLE PLT_MUTEX;
typedef HANDLE PLT_EVENT;
#else
#endif

int PltCreateMutex(PLT_MUTEX *mutex);
void PltDeleteMutex(PLT_MUTEX mutex);
void PltLockMutex(PLT_MUTEX mutex);
void PltUnlockMutex(PLT_MUTEX mutex);

int PltCreateThread(ThreadEntry entry, void* context, PLT_THREAD *thread);
void PltCloseThread(PLT_THREAD thread);
void PltJoinThread(PLT_THREAD thread);

int PltCreateEvent(PLT_EVENT *event);
void PltCloseEvent(PLT_EVENT event);
void PltSetEvent(PLT_EVENT event);
void PltClearEvent(PLT_EVENT event);
void PltPulseEvent(PLT_EVENT event);
void PltWaitForEvent(PLT_EVENT event);