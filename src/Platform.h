#pragma once

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Winsock2.h>
#include <ws2tcpip.h>
#elif defined(__vita__)
#include <unistd.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <psp2/kernel/threadmgr.h>
#else
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#ifdef _WIN32
# define LC_WINDOWS
#else
# define LC_POSIX
# if defined(__APPLE__)
#  define LC_DARWIN
# endif
#endif

#include <stdio.h>
#include "Limelight.h"

#define Limelog(s, ...) \
    if (ListenerCallbacks.logMessage) \
        ListenerCallbacks.logMessage(s, ##__VA_ARGS__)

#if defined(LC_WINDOWS)
#include <crtdbg.h>
#ifdef LC_DEBUG
#define LC_ASSERT(x) __analysis_assume(x); \
                       _ASSERTE(x)
#else
#define LC_ASSERT(x)
#endif
#else
#ifndef LC_DEBUG
#ifndef NDEBUG
#define NDEBUG
#endif
#else
#ifdef NDEBUG
#undef NDEBUG
#endif
#endif
#include <assert.h>
#define LC_ASSERT(x) assert(x)
#endif

int initializePlatform(void);
void cleanupPlatform(void);

uint64_t PltGetMillis(void);
