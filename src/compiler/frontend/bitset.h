#ifndef TINYPY_COMPILER_BITSET_H
#define TINYPY_COMPILER_BITSET_H

#include "tinypy/types.h"

typedef char tinypy_bitset_byte_t;
typedef tinypy_bitset_byte_t *tinypy_parser_bitset_t;

#define TINYPY_BITSET_BITS_PER_BYTE 8
#define TINYPY_BITSET_BYTE_INDEX(bit) ((bit) / TINYPY_BITSET_BITS_PER_BYTE)
#define TINYPY_BITSET_MASK(bit) (1U << ((bit) % TINYPY_BITSET_BITS_PER_BYTE))
#define TINYPY_BITSET_TEST(set, bit) (((set)[TINYPY_BITSET_BYTE_INDEX(bit)] & TINYPY_BITSET_MASK(bit)) != 0U)

#endif
