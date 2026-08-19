/* Derived from Ulf Adams' double-to-string algorithm, Copyright 2018 Ulf Adams.
 * Distributed under the Boost Software License, Version 1.0. The complete
 * license notice is included in double_conversion.c. */
#ifndef TINYPY_CORE_DOUBLE_CONVERSION_COMMON_H
#define TINYPY_CORE_DOUBLE_CONVERSION_COMMON_H

static inline int32_t __tinypy_double_power_of_five_bits(int32_t exponent) { return (int32_t)((((uint32_t)exponent * UINT32_C(1217359)) >> 19U) + 1U); }

static inline uint32_t __tinypy_double_log10_power_of_two(int32_t exponent) { return ((uint32_t)exponent * UINT32_C(78913)) >> 18U; }

static inline uint32_t __tinypy_double_log10_power_of_five(int32_t exponent) { return ((uint32_t)exponent * UINT32_C(732923)) >> 20U; }

static inline int32_t __tinypy_double_copy_special(char *result, tinypy_bool_t sign, tinypy_bool_t exponent, tinypy_bool_t mantissa) {
    if (mantissa != 0) {
        (void)memcpy(result, "NaN", 3U);
        return INT32_C(3);
    }
    if (sign != 0) {
        result[0] = '-';
    }
    if (exponent != 0) {
        (void)memcpy(result + sign, "Infinity", 8U);
        return (int32_t)sign + INT32_C(8);
    }
    (void)memcpy(result + sign, "0E0", 3U);
    return (int32_t)sign + INT32_C(3);
}

static inline uint64_t __tinypy_double_to_bits(double value) {
    uint64_t bits = UINT64_C(0);

    (void)memcpy(&bits, &value, sizeof(value));
    return bits;
}

#endif
