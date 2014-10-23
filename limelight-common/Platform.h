#pragma once

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <Winsock2.h>
#else
#include <unistd.h>
#include <pthread.h>
#endif

#ifdef _WIN32
# if WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP
#  define LC_WINDOWS_PHONE
# elif WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP
#  define LC_WINDOWS
# endif
#else
# define LC_POSIX
# if defined(__APPLE__)
#  define LC_DARWIN
# endif
#endif

#include <stdio.h>
#if defined(LC_WINDOWS_PHONE) || defined(LC_WINDOWS)
extern WCHAR DbgBuf[512];
#define Limelog(s, ...) \
	swprintf(DbgBuf, sizeof(DbgBuf) / sizeof(WCHAR), L ## s, ##__VA_ARGS__); \
	OutputDebugStringW(DbgBuf)
#else
#define Limelog printf
#endif

#if defined(LC_WINDOWS_PHONE) || defined(LC_WINDOWS)
#include <crtdbg.h>
#define LC_ASSERT(x) __analysis_assume(x); \
                     _ASSERTE(x)
#else
#define LC_ASSERT(x)
#endif