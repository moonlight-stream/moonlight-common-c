#include "oblas_lite.h"

#if defined(OBLAS_TINY)
static inline uint8_t gf2_8_mul(uint16_t a, uint16_t b)
{
    if (!a || !b) {
        return 0;
    }

    // Perform 8-bit, carry-less multiplication of |a| and |b|.
    return GF2_8_EXP[GF2_8_LOG[a] + GF2_8_LOG[b]];
}

static void obl_axpy_ref(u8 *a, u8 *b, u8 u, unsigned k)
{
    register u8 *ap = a, *ae = &a[k], *bp = b;
    for (; ap != ae; ap++, bp++)
        *ap ^= gf2_8_mul(u, *bp);
}

static void obl_scal_ref(u8 *a, u8 *b, u8 u, unsigned k)
{
    (void)b;
    register u8 *ap = a, *ae = &a[k];
    for (; ap != ae; ap++)
        *ap = gf2_8_mul(u, *ap);
}

#else
#include "gf2_8_mul_table.h"

static void obl_axpy_ref(u8 *a, u8 *b, u8 u, unsigned k)
{
    register const u8 *u_row = &GF2_8_MUL[u << 8];
    register u8 *ap = a, *ae = &a[k], *bp = b;
    for (; ap != ae; ap++, bp++)
        *ap ^= u_row[*bp];
}

static void obl_scal_ref(u8 *a, u8 *b, u8 u, unsigned k)
{
    (void)b;
    register const u8 *u_row = &GF2_8_MUL[u << 8];
    register u8 *ap = a, *ae = &a[k];
    for (; ap != ae; ap++)
        *ap = u_row[*ap];
}
#endif

void obl_axpyb32_ref(u8 *a, u32 *b, u8 u, unsigned k)
{
    for (unsigned idx = 0, p = 0; idx < k; idx += 8 * sizeof(u32), p++) {
        u32 tmp = b[p];
        while (tmp > 0) {
            unsigned tz = __builtin_ctz(tmp);
            tmp = tmp & (tmp - 1);
            a[tz + idx] ^= u;
        }
    }
}

#if defined(OBLAS_AVX512)
#include <immintrin.h>

#define OBLAS_ALIGN 64

#define OBL_SHUF(op, a, b, f)                                                                                                      \
    do {                                                                                                                           \
        const u8 *u_lo = GF2_8_SHUF_LO + u * 16;                                                                                   \
        const u8 *u_hi = GF2_8_SHUF_HI + u * 16;                                                                                   \
        const __m512i mask = _mm512_set1_epi8(0x0f);                                                                               \
        const __m128i ulo_128 = _mm_loadu_si128((__m128i *)u_lo);                                                                  \
        const __m128i uhi_128 = _mm_loadu_si128((__m128i *)u_hi);                                                                  \
        const __m512i urow_lo = _mm512_broadcast_i32x4(ulo_128);                                                                   \
        const __m512i urow_hi = _mm512_broadcast_i32x4(uhi_128);                                                                   \
        __m512i *ap = (__m512i *)a, *ae = (__m512i *)(a + k - (k % sizeof(__m512i))), *bp = (__m512i *)b;                          \
        for (; ap < ae; ap++, bp++) {                                                                                              \
            __m512i bx = _mm512_loadu_si512(bp);                                                                                   \
            __m512i lo = _mm512_and_si512(bx, mask);                                                                               \
            bx = _mm512_srli_epi64(bx, 4);                                                                                         \
            __m512i hi = _mm512_and_si512(bx, mask);                                                                               \
            lo = _mm512_shuffle_epi8(urow_lo, lo);                                                                                 \
            hi = _mm512_shuffle_epi8(urow_hi, hi);                                                                                 \
            _mm512_storeu_si512(ap, f(_mm512_loadu_si512(ap), _mm512_xor_si512(lo, hi)));                                          \
        }                                                                                                                          \
        op##_ref((u8 *)ap, (u8 *)bp, u, k % sizeof(__m512i));                                                                      \
    } while (0)

#define OBL_SHUF_XOR _mm512_xor_si512

#define OBL_AXPYB32(a, b, u, k)                                                                                                    \
    do {                                                                                                                           \
        __m512i *ap = (__m512i *)a, *ae = (__m512i *)(a + k);                                                                      \
        __m512i scatter =                                                                                                          \
            _mm512_set_epi32(0x03030303, 0x03030303, 0x02020202, 0x02020202, 0x01010101, 0x01010101, 0x00000000, 0x00000000,       \
                             0x03030303, 0x03030303, 0x02020202, 0x02020202, 0x01010101, 0x01010101, 0x00000000, 0x00000000);      \
        __m512i cmpmask =                                                                                                          \
            _mm512_set_epi32(0x80402010, 0x08040201, 0x80402010, 0x08040201, 0x80402010, 0x08040201, 0x80402010, 0x08040201,       \
                             0x80402010, 0x08040201, 0x80402010, 0x08040201, 0x80402010, 0x08040201, 0x80402010, 0x08040201);      \
        __m512i up = _mm512_set1_epi8(u);                                                                                          \
        for (unsigned p = 0; ap < ae; p++, ap++) {                                                                                 \
            __m512i bcast = _mm512_set1_epi32(b[p]);                                                                               \
            __m512i ret = _mm512_shuffle_epi8(bcast, scatter);                                                                     \
            ret = _mm512_andnot_si512(ret, cmpmask);                                                                               \
            __mmask64 tmp = _mm512_cmpeq_epi8_mask(ret, _mm512_setzero_si512());                                                   \
            ret = _mm512_mask_blend_epi8(tmp, _mm512_setzero_si512(), up);                                                         \
            _mm512_storeu_si512(ap, _mm512_xor_si512(_mm512_loadu_si512(ap), ret));                                                \
        }                                                                                                                          \
    } while (0)

#else
#if defined(OBLAS_AVX2)
#include <immintrin.h>

#define OBLAS_ALIGN 32

#define OBL_SHUF(op, a, b, f)                                                                                                      \
    do {                                                                                                                           \
        const u8 *u_lo = GF2_8_SHUF_LO + u * 16;                                                                                   \
        const u8 *u_hi = GF2_8_SHUF_HI + u * 16;                                                                                   \
        const __m256i mask = _mm256_set1_epi8(0x0f);                                                                               \
        const __m256i urow_lo = _mm256_loadu2_m128i((__m128i *)u_lo, (__m128i *)u_lo);                                             \
        const __m256i urow_hi = _mm256_loadu2_m128i((__m128i *)u_hi, (__m128i *)u_hi);                                             \
        __m256i *ap = (__m256i *)a, *ae = (__m256i *)(a + k - (k % sizeof(__m256i))), *bp = (__m256i *)b;                          \
        for (; ap < ae; ap++, bp++) {                                                                                              \
            __m256i bx = _mm256_loadu_si256(bp);                                                                                   \
            __m256i lo = _mm256_and_si256(bx, mask);                                                                               \
            bx = _mm256_srli_epi64(bx, 4);                                                                                         \
            __m256i hi = _mm256_and_si256(bx, mask);                                                                               \
            lo = _mm256_shuffle_epi8(urow_lo, lo);                                                                                 \
            hi = _mm256_shuffle_epi8(urow_hi, hi);                                                                                 \
            _mm256_storeu_si256(ap, f(_mm256_loadu_si256(ap), _mm256_xor_si256(lo, hi)));                                          \
        }                                                                                                                          \
        op##_ref((u8 *)ap, (u8 *)bp, u, k % sizeof(__m256i));                                                                      \
    } while (0)

#define OBL_SHUF_XOR _mm256_xor_si256

#define OBL_AXPYB32(a, b, u, k)                                                                                                    \
    do {                                                                                                                           \
        __m256i *ap = (__m256i *)a, *ae = (__m256i *)(a + k);                                                                      \
        __m256i scatter =                                                                                                          \
            _mm256_set_epi32(0x03030303, 0x03030303, 0x02020202, 0x02020202, 0x01010101, 0x01010101, 0x00000000, 0x00000000);      \
        __m256i cmpmask =                                                                                                          \
            _mm256_set_epi32(0x80402010, 0x08040201, 0x80402010, 0x08040201, 0x80402010, 0x08040201, 0x80402010, 0x08040201);      \
        __m256i up = _mm256_set1_epi8(u);                                                                                          \
        for (unsigned p = 0; ap < ae; p++, ap++) {                                                                                 \
            __m256i bcast = _mm256_set1_epi32(b[p]);                                                                               \
            __m256i ret = _mm256_shuffle_epi8(bcast, scatter);                                                                     \
            ret = _mm256_andnot_si256(ret, cmpmask);                                                                               \
            ret = _mm256_and_si256(_mm256_cmpeq_epi8(ret, _mm256_setzero_si256()), up);                                            \
            _mm256_storeu_si256(ap, _mm256_xor_si256(_mm256_loadu_si256(ap), ret));                                                \
        }                                                                                                                          \
    } while (0)

#else
#if defined(OBLAS_SSE3) || defined(OBLAS_NEON)
#if defined(OBLAS_NEON)
#include "sse2neon/sse2neon.h"
#else
#include <emmintrin.h>
#include <tmmintrin.h>
#endif

#define OBLAS_ALIGN 16

#define OBL_SHUF(op, a, b, f)                                                                                                      \
    do {                                                                                                                           \
        const u8 *u_lo = GF2_8_SHUF_LO + u * 16;                                                                                   \
        const u8 *u_hi = GF2_8_SHUF_HI + u * 16;                                                                                   \
        const __m128i mask = _mm_set1_epi8(0x0f);                                                                                  \
        const __m128i urow_lo = _mm_loadu_si128((__m128i *)u_lo);                                                                  \
        const __m128i urow_hi = _mm_loadu_si128((__m128i *)u_hi);                                                                  \
        __m128i *ap = (__m128i *)a, *ae = (__m128i *)(a + k - (k % sizeof(__m128i))), *bp = (__m128i *)b;                          \
        for (; ap < ae; ap++, bp++) {                                                                                              \
            __m128i bx = _mm_loadu_si128(bp);                                                                                      \
            __m128i lo = _mm_and_si128(bx, mask);                                                                                  \
            bx = _mm_srli_epi64(bx, 4);                                                                                            \
            __m128i hi = _mm_and_si128(bx, mask);                                                                                  \
            lo = _mm_shuffle_epi8(urow_lo, lo);                                                                                    \
            hi = _mm_shuffle_epi8(urow_hi, hi);                                                                                    \
            _mm_storeu_si128(ap, f(_mm_loadu_si128(ap), _mm_xor_si128(lo, hi)));                                                   \
        }                                                                                                                          \
        op##_ref((u8 *)ap, (u8 *)bp, u, k % sizeof(__m128i));                                                                      \
    } while (0)

#define OBL_SHUF_XOR _mm_xor_si128

#define OBL_AXPYB32(a, b, u, k)                                                                                                    \
    do {                                                                                                                           \
        __m128i *ap = (__m128i *)a, *ae = (__m128i *)(a + k);                                                                      \
        __m128i scatter_hi = _mm_set_epi32(0x03030303, 0x03030303, 0x02020202, 0x02020202);                                        \
        __m128i scatter_lo = _mm_set_epi32(0x01010101, 0x01010101, 0x00000000, 0x00000000);                                        \
        __m128i cmpmask = _mm_set_epi32(0x80402010, 0x08040201, 0x80402010, 0x08040201);                                           \
        __m128i up = _mm_set1_epi8(u);                                                                                             \
        for (unsigned p = 0; ap < ae; p++, ap++) {                                                                                 \
            __m128i bcast = _mm_set1_epi32(b[p]);                                                                                  \
            __m128i ret_lo = _mm_shuffle_epi8(bcast, scatter_lo);                                                                  \
            __m128i ret_hi = _mm_shuffle_epi8(bcast, scatter_hi);                                                                  \
            ret_lo = _mm_andnot_si128(ret_lo, cmpmask);                                                                            \
            ret_hi = _mm_andnot_si128(ret_hi, cmpmask);                                                                            \
            ret_lo = _mm_and_si128(_mm_cmpeq_epi8(ret_lo, _mm_setzero_si128()), up);                                               \
            ret_hi = _mm_and_si128(_mm_cmpeq_epi8(ret_hi, _mm_setzero_si128()), up);                                               \
            _mm_storeu_si128(ap, _mm_xor_si128(_mm_loadu_si128(ap), ret_lo));                                                      \
            ap++;                                                                                                                  \
            _mm_storeu_si128(ap, _mm_xor_si128(_mm_loadu_si128(ap), ret_hi));                                                      \
        }                                                                                                                          \
    } while (0)

#else

#define OBLAS_ALIGN (sizeof(void *))
#define OBL_SHUF(op, a, b, f)                                                                                                      \
    do {                                                                                                                           \
        op##_ref(a, b, u, k);                                                                                                      \
    } while (0)

#define OBL_SHUF_XOR

#define OBL_AXPYB32 obl_axpyb32_ref

#endif
#endif
#endif

#define OBL_NOOP(a, b) (b)
void obl_axpy(u8 *a, u8 *b, u8 u, unsigned k)
{
    if (u == 1) {
        register u8 *ap = a, *ae = &a[k], *bp = b;
        for (; ap < ae; ap++, bp++)
            *ap ^= *bp;
    } else {
        OBL_SHUF(obl_axpy, a, b, OBL_SHUF_XOR);
    }
}

void obl_scal(u8 *a, u8 u, unsigned k)
{
    OBL_SHUF(obl_scal, a, a, OBL_NOOP);
}

void obl_swap(u8 *a, u8 *b, unsigned k)
{
    register u8 *ap = a, *ae = &a[k], *bp = b;
    for (; ap < ae; ap++, bp++) {
        u8 tmp = *ap;
        *ap = *bp;
        *bp = tmp;
    }
}

void obl_axpyb32(u8 *a, u32 *b, u8 u, unsigned k)
{
    OBL_AXPYB32(a, b, u, k);
}
