#if defined(__AVX512F__)
#define OBLAS_AVX512
#else
#if defined(__AVX2__)
#define OBLAS_AVX2
#else
#if defined(__SSSE3__) || (defined(_MSC_VER) && defined(_M_X64) && !defined(_M_ARM64))
#define OBLAS_SSE3
#endif
#endif
#endif
