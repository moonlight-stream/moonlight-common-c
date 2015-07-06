#include "PlatformThreads.h"
#include "Platform.h"

int initializePlatformSockets(void);
void cleanupPlatformSockets(void);

#if defined(LC_WINDOWS)
void LimelogWindows(char* Format, ...) {
	va_list va;
	char buffer[1024];

	va_start(va, Format);
	vsprintf(buffer, Format, va);
	va_end(va);

	OutputDebugStringA(buffer);
}
#endif

#if defined(LC_WINDOWS)
PLT_MUTEX thread_list_lock;
PLT_THREAD *thread_head;

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
	WaitForSingleObjectEx(GetCurrentThread(), ms, FALSE);
#else
    useconds_t usecs = ms * 1000;
    usleep(usecs);
#endif
}

int PltCreateMutex(PLT_MUTEX *mutex) {
#if defined(LC_WINDOWS)
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
#if defined(LC_WINDOWS)
	CloseHandle(*mutex);
#else
    pthread_mutex_destroy(mutex);
#endif
}

void PltLockMutex(PLT_MUTEX *mutex) {
#if defined(LC_WINDOWS)
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
#if defined(LC_WINDOWS)
	ReleaseMutex(*mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

void PltJoinThread(PLT_THREAD *thread) {
#if defined(LC_WINDOWS)
	WaitForSingleObjectEx(thread->handle, INFINITE, FALSE);
#else
    pthread_join(*thread, NULL);
#endif
}

void PltCloseThread(PLT_THREAD *thread) {
#if defined(LC_WINDOWS)
	PLT_THREAD *current_thread;
	
	PltLockMutex(&thread_list_lock);

	if (thread_head == thread)
	{
		// Remove the thread from the head
		thread_head = thread_head->next;
	}
	else
	{
		// Find the thread in the list
		current_thread = thread_head;
		while (current_thread != NULL) {
			if (current_thread->next == thread) {
				break;
			}

			current_thread = current_thread->next;
		}

		LC_ASSERT(current_thread != NULL);

		// Unlink this thread
		current_thread->next = thread->next;
	}

	PltUnlockMutex(&thread_list_lock);

	CloseHandle(thread->termRequested);
	CloseHandle(thread->handle);
#else
#endif
}

int PltIsThreadInterrupted(PLT_THREAD *thread) {
#if defined(LC_WINDOWS)
	return thread->cancelled;
#else
	// The thread will die here if a cancellation was requested
	pthread_testcancel();
	return 0;
#endif
}

void PltInterruptThread(PLT_THREAD *thread) {
#if defined(LC_WINDOWS)
	thread->cancelled = 1;
	SetEvent(thread->termRequested);
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

#if defined(LC_WINDOWS)
	{
		thread->termRequested = CreateEventEx(NULL, NULL, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
		if (thread->termRequested == NULL) {
			free(ctx);
			return -1;
		}

		thread->cancelled = 0;

		thread->handle = CreateThread(NULL, 0, ThreadProc, ctx, CREATE_SUSPENDED, &thread->tid);
		if (thread->handle == NULL) {
			CloseHandle(thread->termRequested);
			free(ctx);
			return -1;
		}
		else {
			// Add this thread to the thread list
			PltLockMutex(&thread_list_lock);
			thread->next = thread_head;
			thread_head = thread;
			PltUnlockMutex(&thread_list_lock);

			// Now the thread can run
			ResumeThread(thread->handle);

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
#if defined(LC_WINDOWS)
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
#if defined(LC_WINDOWS)
	CloseHandle(*event);
#else
    pthread_mutex_destroy(&event->mutex);
    pthread_cond_destroy(&event->cond);
#endif
}

void PltSetEvent(PLT_EVENT *event) {
#if defined(LC_WINDOWS)
	SetEvent(*event);
#else
    event->signalled = 1;
    pthread_cond_broadcast(&event->cond);
#endif
}

void PltClearEvent(PLT_EVENT *event) {
#if defined(LC_WINDOWS)
	ResetEvent(*event);
#else
    event->signalled = 0;
#endif
}

int PltWaitForEvent(PLT_EVENT *event) {
#if defined(LC_WINDOWS)
	DWORD error;
	PLT_THREAD *current_thread;
	HANDLE objects[2];

	PltLockMutex(&thread_list_lock);
	current_thread = thread_head;
	while (current_thread != NULL) {
		if (current_thread->tid == GetCurrentThreadId()) {
			break;
		}

		current_thread = current_thread->next;
	}
	PltUnlockMutex(&thread_list_lock);

	LC_ASSERT(current_thread != NULL);

	objects[0] = *event;
	objects[1] = current_thread->termRequested;
	error = WaitForMultipleObjectsEx(2, objects, FALSE, INFINITE, FALSE);
	if (error == WAIT_OBJECT_0) {
		return PLT_WAIT_SUCCESS;
	}
	else if (error == WAIT_OBJECT_0 + 1) {
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
	return PLT_WAIT_SUCCESS;
#endif
}

uint64_t PltGetMillis(void) {
#if defined(LC_WINDOWS)
	return GetTickCount64();
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

#if defined(LC_WINDOWS)
	return PltCreateMutex(&thread_list_lock);
#else
	return 0;
#endif
}

void cleanupPlatform(void) {
	cleanupPlatformSockets();

#if defined(LC_WINDOWS)
	LC_ASSERT(thread_head == NULL);

	PltDeleteMutex(&thread_list_lock);
#else
#endif
}