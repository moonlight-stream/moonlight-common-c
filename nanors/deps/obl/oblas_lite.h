#include <stdint.h>

#include "gf2_8_tables.h"

typedef uint8_t u8;
typedef uint32_t u32;

void obl_axpy(u8 *a, u8 *b, u8 u, unsigned k);
void obl_scal(u8 *a, u8 u, unsigned k);
void obl_swap(u8 *a, u8 *b, unsigned k);
void obl_axpyb32(u8 *a, u32 *b, u8 u, unsigned k);
