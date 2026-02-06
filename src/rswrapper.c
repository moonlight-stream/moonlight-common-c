/**
 * @file src/rswrapper.c
 * @brief Wrappers for nanors vectorization with different ISA options
 */

// _FORTIY_SOURCE can cause some versions of GCC to try to inline
// memset() with incompatible target options when compiling rs.c
#ifdef _FORTIFY_SOURCE
  #undef _FORTIFY_SOURCE
#endif

// The assert() function is decorated with __cold on macOS which
// is incompatible with Clang's target multiversioning feature
#ifndef NDEBUG
  #define NDEBUG
#endif

#define DECORATE_FUNC_I(a, b) a##b
#define DECORATE_FUNC(a, b) DECORATE_FUNC_I(a, b)

// Append an ISA suffix to the public RS API
#define reed_solomon_init DECORATE_FUNC(reed_solomon_init, ISA_SUFFIX)
#define reed_solomon_new DECORATE_FUNC(reed_solomon_new, ISA_SUFFIX)
#define reed_solomon_new_static DECORATE_FUNC(reed_solomon_new_static, ISA_SUFFIX)
#define reed_solomon_release DECORATE_FUNC(reed_solomon_release, ISA_SUFFIX)
#define reed_solomon_decode DECORATE_FUNC(reed_solomon_decode, ISA_SUFFIX)
#define reed_solomon_encode DECORATE_FUNC(reed_solomon_encode, ISA_SUFFIX)

// Append an ISA suffix to internal functions to prevent multiple definition errors
#define obl_axpy_ref DECORATE_FUNC(obl_axpy_ref, ISA_SUFFIX)
#define obl_scal_ref DECORATE_FUNC(obl_scal_ref, ISA_SUFFIX)
#define obl_axpyb32_ref DECORATE_FUNC(obl_axpyb32_ref, ISA_SUFFIX)
#define obl_axpy DECORATE_FUNC(obl_axpy, ISA_SUFFIX)
#define obl_scal DECORATE_FUNC(obl_scal, ISA_SUFFIX)
#define obl_swap DECORATE_FUNC(obl_swap, ISA_SUFFIX)
#define obl_axpyb32 DECORATE_FUNC(obl_axpyb32, ISA_SUFFIX)
#define axpy DECORATE_FUNC(axpy, ISA_SUFFIX)
#define scal DECORATE_FUNC(scal, ISA_SUFFIX)
#define gemm DECORATE_FUNC(gemm, ISA_SUFFIX)
#define invert_mat DECORATE_FUNC(invert_mat, ISA_SUFFIX)

#if defined(__x86_64__) || defined(__i386__) || (defined(_MSC_VER) && !defined(__aarch64__))

  // Compile a variant for SSSE3
  #if defined(__clang__)
    #pragma clang attribute push(__attribute__((target("ssse3"))), apply_to = function)
  #elif __GNUC__
    #pragma GCC push_options
    #pragma GCC target("ssse3")
  #endif
  #define ISA_SUFFIX _ssse3
  #define OBLAS_SSE3
  #include "../nanors/rs.c"
  #undef OBLAS_SSE3
  #undef ISA_SUFFIX
  #if defined(__clang__)
    #pragma clang attribute pop
  #elif __GNUC__
    #pragma GCC pop_options
  #endif

  // Compile a variant for AVX2
  #if defined(__clang__)
    #pragma clang attribute push(__attribute__((target("avx2"))), apply_to = function)
  #elif __GNUC__
    #pragma GCC push_options
    #pragma GCC target("avx2")
  #endif
  #define ISA_SUFFIX _avx2
  #define OBLAS_AVX2
  #include "../nanors/rs.c"
  #undef OBLAS_AVX2
  #undef ISA_SUFFIX
  #if defined(__clang__)
    #pragma clang attribute pop
  #elif __GNUC__
    #pragma GCC pop_options
  #endif

  // Compile a variant for AVX512BW
  #if defined(__clang__)
    #pragma clang attribute push(__attribute__((target("avx512f,avx512bw"))), apply_to = function)
  #elif __GNUC__
    #pragma GCC push_options
    #pragma GCC target("avx512f,avx512bw")
  #endif
  #define ISA_SUFFIX _avx512
  #define OBLAS_AVX512
  #include "../nanors/rs.c"
  #undef OBLAS_AVX512
  #undef ISA_SUFFIX
  #if defined(__clang__)
    #pragma clang attribute pop
  #elif __GNUC__
    #pragma GCC pop_options
  #endif

#endif

// Compile a default variant, this will be the NEON version if __aarch64__
// and the SSE3 version when built with MSVC on Windows.
#define ISA_SUFFIX _def
#include "../nanors/deps/obl/autoshim.h"
#include "../nanors/rs.c"
#undef ISA_SUFFIX

#undef reed_solomon_init
#undef reed_solomon_new
#undef reed_solomon_new_static
#undef reed_solomon_release
#undef reed_solomon_decode
#undef reed_solomon_encode

#include "rswrapper.h"

reed_solomon_new_t reed_solomon_new_fn;
reed_solomon_release_t reed_solomon_release_fn;
reed_solomon_encode_t reed_solomon_encode_fn;
reed_solomon_decode_t reed_solomon_decode_fn;

#if defined(_MSC_VER) && !defined(__aarch64__)
  // https://learn.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex?view=msvc-170

  // The EBX/ECX registers indicate CPU feature flags using bits.
  // SSSE3: bit 9 of ECX
  // AVX2: bit 5 of EBX
  // AVX512F: bit 16 of EBX
  // AVX512BW: bit 30 of EBX

  #include <intrin.h>

  int _msc_check_ebx(int bit) {
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 0);
    int maxFunctionId = cpuInfo[0];
    if (maxFunctionId >= 7) {
      __cpuidex(cpuInfo, 7, 0);
      return (cpuInfo[1] & (1 << bit)) != 0;
    }
    return 0;
  }

  int _msc_supports_avx2(void)     { return _msc_check_ebx(5); }
  int _msc_supports_avx512f(void)  { return _msc_check_ebx(16); }
  int _msc_supports_avx512bw(void) { return _msc_check_ebx(30); }

  int _msc_supports_ssse3(void) {
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 1);
    return (cpuInfo[2] & (1 << 9)) != 0;
  }
#endif

/**
 * @brief This initializes the RS function pointers to the best vectorized version available.
 * @details The streaming code will directly invoke these function pointers during encoding.
 */
void reed_solomon_init(void) {
#if defined(_MSC_VER) && !defined(__aarch64__)
  // Visual Studio
  if (_msc_supports_avx512f() && _msc_supports_avx512bw()) {
    reed_solomon_new_fn = reed_solomon_new_avx512;
    reed_solomon_release_fn = reed_solomon_release_avx512;
    reed_solomon_encode_fn = reed_solomon_encode_avx512;
    reed_solomon_decode_fn = reed_solomon_decode_avx512;
    reed_solomon_init_avx512();
  } else if (_msc_supports_avx2()) {
    reed_solomon_new_fn = reed_solomon_new_avx2;
    reed_solomon_release_fn = reed_solomon_release_avx2;
    reed_solomon_encode_fn = reed_solomon_encode_avx2;
    reed_solomon_decode_fn = reed_solomon_decode_avx2;
    reed_solomon_init_avx2();
  } else if (_msc_supports_ssse3()) {
    reed_solomon_new_fn = reed_solomon_new_ssse3;
    reed_solomon_release_fn = reed_solomon_release_ssse3;
    reed_solomon_encode_fn = reed_solomon_encode_ssse3;
    reed_solomon_decode_fn = reed_solomon_decode_ssse3;
    reed_solomon_init_ssse3();
  } else

#elif defined(__x86_64__)
  // gcc & clang
  if (__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw")) {
    reed_solomon_new_fn = reed_solomon_new_avx512;
    reed_solomon_release_fn = reed_solomon_release_avx512;
    reed_solomon_encode_fn = reed_solomon_encode_avx512;
    reed_solomon_decode_fn = reed_solomon_decode_avx512;
    reed_solomon_init_avx512();
  } else if (__builtin_cpu_supports("avx2")) {
    reed_solomon_new_fn = reed_solomon_new_avx2;
    reed_solomon_release_fn = reed_solomon_release_avx2;
    reed_solomon_encode_fn = reed_solomon_encode_avx2;
    reed_solomon_decode_fn = reed_solomon_decode_avx2;
    reed_solomon_init_avx2();
  } else if (__builtin_cpu_supports("ssse3")) {
    reed_solomon_new_fn = reed_solomon_new_ssse3;
    reed_solomon_release_fn = reed_solomon_release_ssse3;
    reed_solomon_encode_fn = reed_solomon_encode_ssse3;
    reed_solomon_decode_fn = reed_solomon_decode_ssse3;
    reed_solomon_init_ssse3();
  } else

#endif
  //
  {
    reed_solomon_new_fn = reed_solomon_new_def;
    reed_solomon_release_fn = reed_solomon_release_def;
    reed_solomon_encode_fn = reed_solomon_encode_def;
    reed_solomon_decode_fn = reed_solomon_decode_def;
    reed_solomon_init_def();
  }
}
