#include "tinypy/long.h"

#include "internal.h"

#include <string.h>

#define TINYPY_LONG_BASE15_MASK UINT16_C(0x7fff)

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static inline size_t __tinypy_internal_long_allocation_size(size_t digit_count) {
    size_t payload_size;

    payload_size = digit_count * sizeof(uint16_t);
    return offsetof(tinypy_long_object_t, digits) + payload_size;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_long_from_base15_digits(tinypy_vm_t *vm, int32_t sign, const uint16_t *digits, size_t digit_count) {
    size_t allocation_size;

    allocation_size = __tinypy_internal_long_allocation_size(digit_count);

    tinypy_value_t *result = tinypy_internal_value_allocate(
        vm,
        TINYPY_VALUE_LONG,
        allocation_size);
    TINYPY_LONG_OBJECT(result)->digit_count = digit_count;
    TINYPY_LONG_OBJECT(result)->sign = (int32_t)sign;
    if (digit_count != 0U) {
        (void)memcpy(
            TINYPY_LONG_OBJECT(result)->digits,
            digits,
            digit_count * sizeof(*digits));
    }

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_long_from_i64(tinypy_vm_t *vm, int64_t value) {
    uint16_t digits[5];
    uint64_t magnitude;
    size_t digit_count = 0U;
    int32_t sign;

    if (value == INT64_C(0)) {
        tinypy_value_t *return_value_1 = tinypy_long_from_base15_digits(vm, 0, NULL, 0U);
        return return_value_1;
    }

    if (value < INT64_C(0)) {
        sign = -1;
        magnitude = (uint64_t)(-(value + INT64_C(1)));
        magnitude += UINT64_C(1);
    }
    else {
        sign = 1;
        magnitude = (uint64_t)value;
    }

    while (magnitude != UINT64_C(0)) {
        digits[digit_count] = (uint16_t)(magnitude & (uint64_t)TINYPY_LONG_BASE15_MASK);
        digit_count += 1U;
        magnitude >>= 15U;
    }

    tinypy_value_t *return_value_2 = tinypy_long_from_base15_digits(
        vm,
        sign,
        digits,
        digit_count);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
const uint16_t *tinypy_long_base15_view(const tinypy_value_t *value, int32_t *out_sign, size_t *out_digit_count) {

    *out_sign = TINYPY_LONG_SIGN(value);
    *out_digit_count = TINYPY_LONG_DIGIT_COUNT(value);
    const uint16_t *return_value_1 = TINYPY_LONG_DIGIT_COUNT(value) != 0U
                   ? TINYPY_LONG_OBJECT(value)->digits
                   : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int64_t tinypy_long_as_i64(const tinypy_value_t *value) {
    const uint16_t *digits;
    uint64_t magnitude = UINT64_C(0);
    size_t index;
    uint64_t negative_limit = (uint64_t)INT64_MAX + UINT64_C(1);

    digits = TINYPY_LONG_OBJECT(value)->digits;
    index = TINYPY_LONG_DIGIT_COUNT(value);
    while (index != 0U) {
        index -= 1U;
        magnitude <<= 15U;
        magnitude += (uint64_t)digits[index];
    }

    if (TINYPY_LONG_SIGN(value) > 0) {
        return (int64_t)magnitude;
    }
    if (TINYPY_LONG_SIGN(value) < 0) {
        if (magnitude == negative_limit) {
            return INT64_MIN;
        }
        return -(int64_t)magnitude;
    }

    return INT64_C(0);
}
