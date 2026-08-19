/* Derived from Ulf Adams' double-to-string algorithm, Copyright 2018 Ulf Adams.
 * Distributed under the Boost Software License, Version 1.0. The complete
 * license notice is included in double_conversion.c. */
#ifndef TINYPY_CORE_DOUBLE_CONVERSION_INTRINSICS_H
#define TINYPY_CORE_DOUBLE_CONVERSION_INTRINSICS_H

static inline uint64_t __tinypy_double_multiply_128(uint64_t left, uint64_t right, uint64_t *out_high) {
    uint32_t left_low = (uint32_t)left;
    uint32_t left_high = (uint32_t)(left >> 32U);
    uint32_t right_low = (uint32_t)right;
    uint32_t right_high = (uint32_t)(right >> 32U);
    uint64_t product_00 = (uint64_t)left_low * right_low;
    uint64_t product_01 = (uint64_t)left_low * right_high;
    uint64_t product_10 = (uint64_t)left_high * right_low;
    uint64_t product_11 = (uint64_t)left_high * right_high;
    uint32_t product_00_low = (uint32_t)product_00;
    uint32_t product_00_high = (uint32_t)(product_00 >> 32U);
    uint64_t middle_1 = product_10 + product_00_high;
    uint32_t middle_1_low = (uint32_t)middle_1;
    uint32_t middle_1_high = (uint32_t)(middle_1 >> 32U);
    uint64_t middle_2 = product_01 + middle_1_low;
    uint32_t middle_2_low = (uint32_t)middle_2;
    uint32_t middle_2_high = (uint32_t)(middle_2 >> 32U);

    *out_high = product_11 + middle_1_high + middle_2_high;
    return ((uint64_t)middle_2_low << 32U) | product_00_low;
}

static inline uint64_t __tinypy_double_shift_right_128(uint64_t low, uint64_t high, uint32_t distance) { return (high << (64U - distance)) | (low >> distance); }

#if UINTPTR_MAX == UINT32_MAX
static inline uint64_t __tinypy_double_multiply_high(uint64_t left, uint64_t right) {
    uint64_t high;

    (void)__tinypy_double_multiply_128(left, right, &high);
    return high;
}

static inline uint64_t __tinypy_double_divide_5(uint64_t value) {
    uint64_t product = __tinypy_double_multiply_high(value, UINT64_C(0xcccccccccccccccd));

    return product >> 2U;
}

static inline uint64_t __tinypy_double_divide_10(uint64_t value) {
    uint64_t product = __tinypy_double_multiply_high(value, UINT64_C(0xcccccccccccccccd));

    return product >> 3U;
}

static inline uint64_t __tinypy_double_divide_100(uint64_t value) {
    uint64_t product = __tinypy_double_multiply_high(value >> 2U, UINT64_C(0x28f5c28f5c28f5c3));

    return product >> 2U;
}

static inline uint64_t __tinypy_double_divide_100000000(uint64_t value) {
    uint64_t product = __tinypy_double_multiply_high(value, UINT64_C(0xabcc77118461cefd));

    return product >> 26U;
}
#else
static inline uint64_t __tinypy_double_divide_5(uint64_t value) { return value / UINT64_C(5); }

static inline uint64_t __tinypy_double_divide_10(uint64_t value) { return value / UINT64_C(10); }

static inline uint64_t __tinypy_double_divide_100(uint64_t value) { return value / UINT64_C(100); }

static inline uint64_t __tinypy_double_divide_100000000(uint64_t value) { return value / UINT64_C(100000000); }
#endif

static inline uint32_t __tinypy_double_power_of_five_factor(uint64_t value) {
    const uint64_t inverse = UINT64_C(14757395258967641293);
    const uint64_t limit = UINT64_C(3689348814741910323);
    uint32_t count = UINT32_C(0);

    for (;;) {
        value *= inverse;
        if (value > limit) {
            break;
        }
        count += UINT32_C(1);
    }
    return count;
}

static inline tinypy_bool_t __tinypy_double_multiple_of_power_of_five(uint64_t value, uint32_t power) {
    uint32_t factor = __tinypy_double_power_of_five_factor(value);

    return factor >= power ? TINYPY_TRUE : TINYPY_FALSE;
}

static inline tinypy_bool_t __tinypy_double_multiple_of_power_of_two(uint64_t value, uint32_t power) { return (value & ((UINT64_C(1) << power) - UINT64_C(1))) == UINT64_C(0) ? TINYPY_TRUE : TINYPY_FALSE; }

static inline uint64_t __tinypy_double_multiply_shift_all(uint64_t value, const uint64_t *multiplier, int32_t shift, uint64_t *out_upper, uint64_t *out_lower, uint32_t lower_shift) {
    uint64_t temporary;
    uint64_t low;
    uint64_t high;
    uint64_t middle;
    uint64_t low_2;
    uint64_t middle_2;
    uint64_t high_2;
    uint64_t result;

    value <<= 1U;
    low = __tinypy_double_multiply_128(value, multiplier[0], &temporary);
    middle = temporary + __tinypy_double_multiply_128(value, multiplier[1], &high);
    high += middle < temporary ? UINT64_C(1) : UINT64_C(0);
    low_2 = low + multiplier[0];
    middle_2 = middle + multiplier[1] + (low_2 < low ? UINT64_C(1) : UINT64_C(0));
    high_2 = high + (middle_2 < middle ? UINT64_C(1) : UINT64_C(0));
    *out_upper = __tinypy_double_shift_right_128(middle_2, high_2, (uint32_t)(shift - INT32_C(65)));
    if (lower_shift == UINT32_C(1)) {
        uint64_t low_3 = low - multiplier[0];
        uint64_t middle_3 = middle - multiplier[1] - (low_3 > low ? UINT64_C(1) : UINT64_C(0));
        uint64_t high_3 = high - (middle_3 > middle ? UINT64_C(1) : UINT64_C(0));

        *out_lower = __tinypy_double_shift_right_128(middle_3, high_3, (uint32_t)(shift - INT32_C(65)));
    } else {
        uint64_t low_3 = low + low;
        uint64_t middle_3 = middle + middle + (low_3 < low ? UINT64_C(1) : UINT64_C(0));
        uint64_t high_3 = high + high + (middle_3 < middle ? UINT64_C(1) : UINT64_C(0));
        uint64_t low_4 = low_3 - multiplier[0];
        uint64_t middle_4 = middle_3 - multiplier[1] - (low_4 > low_3 ? UINT64_C(1) : UINT64_C(0));
        uint64_t high_4 = high_3 - (middle_4 > middle_3 ? UINT64_C(1) : UINT64_C(0));

        *out_lower = __tinypy_double_shift_right_128(middle_4, high_4, (uint32_t)(shift - INT32_C(64)));
    }
    result = __tinypy_double_shift_right_128(middle, high, (uint32_t)(shift - INT32_C(65)));
    return result;
}

#endif
