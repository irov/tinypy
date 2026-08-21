#include "tinypy/long.h"

#include "internal.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define TINYPY_LONG_BASE15_MASK UINT16_C(0x7fff)

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static inline size_t __tinypy_internal_long_allocation_size(size_t digit_count) {
    size_t header_size = offsetof(tinypy_long_object_t, digits);

    if (digit_count > (SIZE_MAX - header_size) / sizeof(uint16_t)) {
        return 0U;
    }
    return header_size + digit_count * sizeof(uint16_t);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_long_allocate_digits(tinypy_vm_t *vm, int32_t sign, size_t digit_count, tinypy_error_t **out_error) {
    size_t allocation_size = __tinypy_internal_long_allocation_size(digit_count);
    tinypy_value_t *result;

    if (allocation_size == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long int is too large", out_error);
        return NULL;
    }
    result = tinypy_internal_object_allocate_checked(vm, &vm->types[TINYPY_VALUE_LONG], allocation_size, out_error);
    if (result == NULL) {
        return NULL;
    }
    TINYPY_LONG_OBJECT(result)->digit_count = digit_count;
    TINYPY_LONG_OBJECT(result)->sign = sign;
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_long_from_base15_digits_checked(tinypy_vm_t *vm, int32_t sign, const uint16_t *digits, size_t digit_count, tinypy_error_t **out_error) {
    tinypy_value_t *result = tinypy_internal_long_allocate_digits(vm, sign, digit_count, out_error);

    if (result != NULL && digit_count != 0U) {
        (void)memcpy(TINYPY_LONG_OBJECT(result)->digits, digits, digit_count * sizeof(*digits));
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_long_from_base15_digits(tinypy_vm_t *vm, int32_t sign, const uint16_t *digits, size_t digit_count) {
    size_t allocation_size;

    allocation_size = __tinypy_internal_long_allocation_size(digit_count);
    if (allocation_size == 0U) {
        return NULL;
    }

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
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_long_bit(const tinypy_long_object_t *value, size_t bit_index) {
    size_t digit_index = bit_index / 15U;
    size_t digit_bit = bit_index % 15U;

    return (value->digits[digit_index] & (uint16_t)(UINT16_C(1) << digit_bit)) != 0U
        ? TINYPY_TRUE
        : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_long_as_double(const tinypy_value_t *value, double *out_value, tinypy_error_t **out_error) {
    const tinypy_long_object_t *long_value = TINYPY_LONG_OBJECT((tinypy_value_t *)value);
    size_t digit_count = long_value->digit_count;
    size_t bit_length;
    size_t bit_index;
    uint64_t significand = UINT64_C(0);
    double result;

    if (digit_count == 0U || long_value->sign == 0) {
        *out_value = 0.0;
        return TINYPY_TRUE;
    }

    bit_length = (digit_count - 1U) * 15U;
    {
        uint16_t most_significant = long_value->digits[digit_count - 1U];

        while (most_significant != 0U) {
            bit_length += 1U;
            most_significant >>= 1U;
        }
    }
    if (bit_length > (size_t)DBL_MAX_EXP) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_OVERFLOW, "long int too large to convert to float", out_error);
        return TINYPY_FALSE;
    }

    if (bit_length <= (size_t)DBL_MANT_DIG) {
        for (bit_index = bit_length; bit_index != 0U; --bit_index) {
            significand = (significand << 1U) | (uint64_t)__tinypy_internal_long_bit(long_value, bit_index - 1U);
        }
        result = (double)significand;
    }
    else {
        size_t shift = bit_length - (size_t)DBL_MANT_DIG;
        tinypy_bool_t halfway;
        tinypy_bool_t sticky = TINYPY_FALSE;

        for (bit_index = bit_length; bit_index != shift; --bit_index) {
            significand = (significand << 1U) | (uint64_t)__tinypy_internal_long_bit(long_value, bit_index - 1U);
        }
        halfway = __tinypy_internal_long_bit(long_value, shift - 1U);
        for (bit_index = 0U; bit_index + 1U < shift; ++bit_index) {
            if (__tinypy_internal_long_bit(long_value, bit_index) != 0) {
                sticky = TINYPY_TRUE;
                break;
            }
        }
        if (halfway != 0 && (sticky != 0 || (significand & UINT64_C(1)) != 0U)) {
            significand += UINT64_C(1);
        }
        result = ldexp((double)significand, (int)shift);
    }

    if (isfinite(result) == 0) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_OVERFLOW, "long int too large to convert to float", out_error);
        return TINYPY_FALSE;
    }
    *out_value = long_value->sign < 0 ? -result : result;
    return TINYPY_TRUE;
}
