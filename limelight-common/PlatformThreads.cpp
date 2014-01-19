#include "PlatformThreads.h"
#include "Platform.h"

struct thread_context {
	ThreadEntry entry;
	void* context;
};

#ifdef _WIN32
DWORD WINAPI ThreadProc(LPVOID lpParameter) {
	struct thread_context *ctx = (struct thread_context *)lpParameter;

	ctx->entry(ctx->context);

	free(ctx);

	return 0;
}
#else
void* ThreadProc(void* context) {
    struct thread_context *ctx = (struct thread_context *)context;

    ctx->entry(ctx->context);
    
	free(ctx);
    
	return NULL;
}
#endif

void PltSleepMs(int ms) {
#ifdef _WIN32
	Sleep(ms);
#else
    long usecs = (long)ms * 1000;
    usleep(usecs);
#endif
}

int PltCreateMutex(PLT_MUTEX *mutex) {
#ifdef _WIN32
	*mutex = CreateMutex(NULL, FALSE, NULL);
	if (!*mutex) {
		return -1;
	}
	return 0;
#else
    return pthread_mutex_init(mutex, NULL);
#endif
}

void PltDeleteMutex(PLT_MUTEX *mutex) {
#ifdef _WIN32
	CloseHandle(*mutex);
#else
    pthread_mutex_destroy(mutex);
#endif
}

void PltLockMutex(PLT_MUTEX *mutex) {
#ifdef _WIN32
	WaitForSingleObject(*mutex, INFINITE);
#else
    pthread_mutex_lock(mutex);
#endif
}

void PltUnlockMutex(PLT_MUTEX *mutex) {
#ifdef _WIN32
	ReleaseMutex(*mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

void PltJoinThread(PLT_THREAD *thread) {
#ifdef _WIN32
	WaitForSingleObject(*thread, INFINITE);
#else
    pthread_join(*thread, NULL);
#endif
}

void PltCloseThread(PLT_THREAD *thread) {
#ifdef _WIN32
	CloseHandle(*thread);
#else
#endif
}

int PltCreateThread(ThreadEntry entry, void* context, PLT_THREAD *thread) {
	struct thread_context *ctx;
	int err;

	ctx = (struct thread_context *)malloc(sizeof(*ctx));
	if (ctx == NULL) {
		return -1;
	}

	ctx->entry = entry;
	ctx->context = context;

#ifdef _WIN32
	{
		HANDLE hThread = CreateThread(NULL, 0, ThreadProc, ctx, 0, NULL);
		if (hThread == NULL) {
			free(ctx);
			return -1;
		}
		else {
			CloseHandle(hThread);
			err = 0;
		}
	}
#else
    {
        err = pthread_create(thread, NULL, ThreadProc, ctx);
        if (err != 0) {
            free(ctx);
        }
    }
#endif

	return err;
}

int PltCreateEvent(PLT_EVENT *event) {
#ifdef _WIN32
	*event = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!*event) {
		return -1;
	}

	return 0;
#else
    pthread_mutex_init(&event->mutex, NULL);
    pthread_cond_init(&event->cond, NULL);
    event->signalled = 0;
    return 0;
#endif
}

void PltCloseEvent(PLT_EVENT *event) {
#ifdef _WIN32
	CloseHandle(*event);
#else
    pthread_mutex_destroy(&event->mutex);
    pthread_cond_destroy(&event->cond);
#endif
}

void PltSetEvent(PLT_EVENT *event) {
#ifdef _WIN32
	SetEvent(*event);
#else
    event->signalled = 1;
    pthread_cond_broadcast(&event->cond);
#endif
}

void PltClearEvent(PLT_EVENT *event) {
#ifdef _WIN32
	ResetEvent(*event);
#else
    event->signalled = 0;
#endif
}

void PltPulseEvent(PLT_EVENT *event) {
#ifdef _WIN32
	PulseEvent(*event);
#else
    pthread_mutex_lock(&event->mutex);
    event->signalled = 1;
    pthread_cond_broadcast(&event->cond);
    event->signalled = 0;
    pthread_mutex_unlock(&event->mutex);
#endif
}

void PltWaitForEvent(PLT_EVENT *event) {
#ifdef _WIN32
	WaitForSingleObject(*event, INFINITE);
#else
    while (!event->signalled) {
        pthread_cond_wait(&event->cond, &event->mutex);
    }
#endif
}



