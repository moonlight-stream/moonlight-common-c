#include "Platform.h"

typedef void (*ThreadEntry)(void *context);

#ifdef _WIN32
typedef HANDLE PLT_THREAD;
typedef HANDLE PLT_MUTEX;
#else
#endif

int PltCreateMutex(PLT_MUTEX *mutex);
void PltDeleteMutex(PLT_MUTEX *mutex);
void PltLockMutex(PLT_MUTEX *mutex);
void PltUnlockMutex(PLT_MUTEX *mutex);

int PltCreateThread(ThreadEntry entry, void* context, PLT_THREAD *thread);
void PltCloseThread(PLT_THREAD thread);
void PltJoinThread(PLT_THREAD thread);
