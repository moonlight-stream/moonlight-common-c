#include "PlatformThreads.h"

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
#error POSIX threads not implemented
#endif

int PltCreateMutex(PLT_MUTEX *mutex) {
#ifdef _WIN32
	*mutex = CreateMutex(NULL, FALSE, NULL);
	if (!*mutex) {
		return -1;
	}
	return 0;
#else
#endif
}

void PltDeleteMutex(PLT_MUTEX mutex) {
#ifdef _WIN32
	CloseHandle(mutex);
#else
#endif
}

void PltLockMutex(PLT_MUTEX mutex) {
#ifdef _WIN32
	WaitForSingleObject(mutex, INFINITE);
#else
#endif
}

void PltUnlockMutex(PLT_MUTEX mutex) {
#ifdef _WIN32
	ReleaseMutex(mutex);
#else
#endif
}

void PltJoinThread(PLT_THREAD thread) {
#ifdef _WIN32
	WaitForSingleObject(thread, INFINITE);
#else
#endif
}

void PltCloseThread(PLT_THREAD thread) {
#ifdef _WIN32
	CloseHandle(thread);
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
#endif
}

void PltCloseEvent(PLT_EVENT event) {
#ifdef _WIN32
	CloseHandle(event);
#else
#endif
}

void PltSetEvent(PLT_EVENT event) {
#ifdef _WIN32
	SetEvent(event);
#else
#endif
}

void PltClearEvent(PLT_EVENT event) {
#ifdef _WIN32
	ResetEvent(event);
#else
#endif
}

void PltPulseEvent(PLT_EVENT event) {
#ifdef _WIN32
	PulseEvent(event);
#else
#endif
}

void PltWaitForEvent(PLT_EVENT event) {
#ifdef _WIN32
	WaitForSingleObject(event, INFINITE);
#else
#endif
}



