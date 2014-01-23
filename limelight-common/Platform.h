#pragma once

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <Windows.h>
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
#endif

#if defined(LC_WINDOWS_PHONE) || defined(LC_WINDOWS)
#include <crtdbg.h>
#define LC_ASSERT _ASSERTE
#else
#define LC_ASSERT(x)
#endif