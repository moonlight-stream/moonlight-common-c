#include "PlatformThreads.h"
#include "Platform.h"

struct thread_context {
	ThreadEntry entry;
	void* context;
};

#if defined(LC_WINDOWS)
VOID WINAPI ApcFunc(ULONG_PTR parameter) {
	return;
}
#endif

#if defined(LC_WINDOWS_PHONE)
WCHAR DbgBuf[512];
#endif

#ifdef LC_WINDOWS
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
#if defined(LC_WINDOWS)
	Sleep(ms);
#elif defined (LC_WINDOWS_PHONE)
	WaitForSingleObjectEx(GetCurrentThread(), ms, FALSE);
#else
    long usecs = (long)ms * 1000;
    usleep(usecs);
#endif
}

int PltCreateMutex(PLT_MUTEX *mutex) {
#if defined(LC_WINDOWS) || defined(LC_WINDOWS_PHONE)
	*mutex = CreateMutexEx(NULL, NULL, 0, MUTEX_ALL_ACCESS);
	if (!*mutex) {
		return -1;
	}
	return 0;
#else
    return pthread_mutex_init(mutex, NULL);
#endif
}

void PltDeleteMutex(PLT_MUTEX *mutex) {
#if defined(LC_WINDOWS) || defined(LC_WINDOWS_PHONE)
	CloseHandle(*mutex);
#else
    pthread_mutex_destroy(mutex);
#endif
}

void PltLockMutex(PLT_MUTEX *mutex) {
#if defined(LC_WINDOWS) || defined(LC_WINDOWS_PHONE)
	int err;
	err = WaitForSingleObjectEx(*mutex, INFINITE, FALSE);
	if (err != WAIT_OBJECT_0) {
		LC_ASSERT(FALSE);
	}
#else
    pthread_mutex_lock(mutex);
#endif
}

void PltUnlockMutex(PLT_MUTEX *mutex) {
#if defined(LC_WINDOWS) || defined(LC_WINDOWS_PHONE)
	ReleaseMutex(*mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

void PltJoinThread(PLT_THREAD *thread) {
#if defined(LC_WINDOWS) || defined(LC_WINDOWS_PHONE)
	WaitForSingleObjectEx(thread->handle, INFINITE, FALSE);
#else
    pthread_join(*thread, NULL);
#endif
}

void PltCloseThread(PLT_THREAD *thread) {
#if defined(LC_WINDOWS) || defined(LC_WINDOWS_PHONE)
	CloseHandle(thread->handle);
#else
#endif
}

int PltIsThreadInterrupted(PLT_THREAD *thread) {
#if defined(LC_WINDOWS) || defined(LC_WINDOWS_PHONE)
	return thread->cancelled;
#else
#endif
}

void PltInterruptThread(PLT_THREAD *thread) {
#if defined(LC_WINDOWS)
	thread->cancelled = 1;
	QueueUserAPC(ApcFunc, thread->handle, 0);
#elif defined(LC_WINDOWS_PHONE)
#else
	pthread_cancel(*thread);
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
		thread->cancelled = 0;
		thread->handle = CreateThread(NULL, 0, ThreadProc, ctx, 0, NULL);
		if (thread->handle == NULL) {
			free(ctx);
			return -1;
		}
		else {
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
#if defined(LC_WINDOWS) || defined(LC_WINDOWS_PHONE)
	*event = CreateEventEx(NULL, NULL, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
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

int PltWaitForEvent(PLT_EVENT *event) {
#ifdef _WIN32
	DWORD error = WaitForSingleObjectEx(*event, INFINITE, TRUE);
	if (error == STATUS_WAIT_0) {
		return PLT_WAIT_SUCCESS;
	}
	else if (error == WAIT_IO_COMPLETION) {
		return PLT_WAIT_INTERRUPTED;
	}
	else {
		LC_ASSERT(0);
		return -1;
	}
#else
	pthread_mutex_lock(&event->mutex);
    while (!event->signalled) {
        pthread_cond_wait(&event->cond, &event->mutex);
    }
	pthread_mutex_unlock(&event->mutex);
#endif
}
