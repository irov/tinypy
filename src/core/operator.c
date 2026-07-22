#include "tinypy/operator.h"

#include "internal.h"

#include <assert.h>
#include <math.h>
#include <string.h>

#define TINYPY_LONG_BASE UINT32_C(32768)
#define TINYPY_LONG_MASK UINT32_C(32767)

typedef struct tinypy_integer_view_t {
    int sign;
    const uint16_t *digits;
    size_t count;
    uint16_t local_digits[5];
} tinypy_integer_view_t;

//////////////////////////////////////////////////////////////////////////
static int __tinypy_operator_is_integer(tinypy_value_type_e kind) {
    return kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_operator_is_number(tinypy_value_type_e kind) {
    return __tinypy_operator_is_integer(kind) != 0 || kind == TINYPY_VALUE_FLOAT || kind == TINYPY_VALUE_COMPLEX;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_operator_integer_view(const tinypy_value_t *value, tinypy_integer_view_t *view) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_LONG) {
        view->sign = TINYPY_LONG_SIGN(value);
        view->digits = TINYPY_LONG_OBJECT(value)->digits;
        view->count = TINYPY_LONG_DIGIT_COUNT(value);
    }
    else {
        int64_t signed_value = TINYPY_INTEGER_VALUE(value);
        uint64_t magnitude = signed_value < 0 ? (uint64_t)(-(signed_value + 1)) + UINT64_C(1) : (uint64_t)signed_value;

        view->sign = signed_value < 0 ? -1 : (signed_value > 0 ? 1 : 0);
        view->count = 0U;
        while (magnitude != 0U) {
            view->local_digits[view->count] = (uint16_t)(magnitude & TINYPY_LONG_MASK);
            view->count += 1U;
            magnitude >>= 15U;
        }
        view->digits = view->local_digits;
    }
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_operator_magnitude_compare(const tinypy_integer_view_t *left, const tinypy_integer_view_t *right) {
    size_t index;

    if (left->count != right->count) {
        return left->count < right->count ? -1 : 1;
    }
    for (index = left->count; index != 0U; index -= 1U) {
        if (left->digits[index - 1U] != right->digits[index - 1U]) {
            return left->digits[index - 1U] < right->digits[index - 1U] ? -1 : 1;
        }
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_long_add_views(tinypy_vm_t *vm, const tinypy_integer_view_t *left, const tinypy_integer_view_t *right, int subtract_right) {
    int right_sign = subtract_right != 0 ? -right->sign : right->sign;
    size_t maximum_count = left->count > right->count ? left->count : right->count;
    size_t capacity;
    uint16_t *digits;
    size_t count = 0U;
    int sign;

    assert(maximum_count != SIZE_MAX);
    capacity = maximum_count + 1U;
    assert(capacity <= SIZE_MAX / sizeof(*digits));
    digits = (uint16_t *)tinypy_internal_vm_allocate(vm, capacity * sizeof(*digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    if (left->sign == 0) {
        size_t index;
        sign = right_sign;
        for (index = 0U; index < right->count; ++index) {
            digits[index] = right->digits[index];
        }
        count = right->count;
    }
    else if (right_sign == 0) {
        size_t index;
        sign = left->sign;
        for (index = 0U; index < left->count; ++index) {
            digits[index] = left->digits[index];
        }
        count = left->count;
    }
    else if (left->sign == right_sign) {
        uint32_t carry = 0U;
        size_t index;
        sign = left->sign;
        count = left->count > right->count ? left->count : right->count;
        for (index = 0U; index < count; ++index) {
            uint32_t sum = carry;
            if (index < left->count) {
                sum += left->digits[index];
            }
            if (index < right->count) {
                sum += right->digits[index];
            }
            digits[index] = (uint16_t)(sum & TINYPY_LONG_MASK);
            carry = sum >> 15U;
        }
        if (carry != 0U) {
            digits[count] = (uint16_t)carry;
            count += 1U;
        }
    }
    else {
        const tinypy_integer_view_t *larger;
        const tinypy_integer_view_t *smaller;
        int comparison = __tinypy_operator_magnitude_compare(left, right);
        int32_t borrow = 0;
        size_t index;

        if (comparison == 0) {
            sign = 0;
            count = 0U;
        }
        else {
            larger = comparison > 0 ? left : right;
            smaller = comparison > 0 ? right : left;
            sign = comparison > 0 ? left->sign : right_sign;
            count = larger->count;
            for (index = 0U; index < count; ++index) {
                int32_t difference = (int32_t)larger->digits[index] - borrow - (index < smaller->count ? (int32_t)smaller->digits[index] : 0);
                if (difference < 0) {
                    difference += (int32_t)TINYPY_LONG_BASE;
                    borrow = 1;
                }
                else {
                    borrow = 0;
                }
                digits[index] = (uint16_t)difference;
            }
            while (count != 0U && digits[count - 1U] == 0U) {
                count -= 1U;
            }
            if (count == 0U) {
                sign = 0;
            }
        }
    }
    tinypy_value_t *result = tinypy_long_from_base15_digits(vm, sign, digits, count);
    tinypy_internal_vm_deallocate(vm, digits, capacity * sizeof(*digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_long_multiply_views(tinypy_vm_t *vm, const tinypy_integer_view_t *left, const tinypy_integer_view_t *right) {
    size_t capacity;
    uint16_t *digits;
    size_t left_index;
    size_t count;

    if (left->sign == 0 || right->sign == 0) {
        return tinypy_long_from_base15_digits(vm, 0, NULL, 0U);
    }
    assert(left->count <= SIZE_MAX - right->count);
    capacity = left->count + right->count;
    assert(capacity <= SIZE_MAX / sizeof(*digits));
    digits = (uint16_t *)tinypy_internal_vm_allocate(vm, capacity * sizeof(*digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    (void)memset(digits, 0, capacity * sizeof(*digits));
    for (left_index = 0U; left_index < left->count; ++left_index) {
        uint32_t carry = 0U;
        size_t right_index;

        for (right_index = 0U; right_index < right->count; ++right_index) {
            size_t output_index = left_index + right_index;
            uint32_t product = (uint32_t)digits[output_index] + (uint32_t)left->digits[left_index] * (uint32_t)right->digits[right_index] + carry;

            digits[output_index] = (uint16_t)(product & TINYPY_LONG_MASK);
            carry = product >> 15U;
        }
        digits[left_index + right->count] = (uint16_t)carry;
    }
    count = capacity;
    while (count != 0U && digits[count - 1U] == 0U) {
        count -= 1U;
    }
    tinypy_value_t *result = tinypy_long_from_base15_digits(vm, left->sign == right->sign ? 1 : -1, digits, count);
    tinypy_internal_vm_deallocate(vm, digits, capacity * sizeof(*digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_operator_trim_digits(const uint16_t *digits, size_t count) {
    while (count != 0U && digits[count - 1U] == 0U) {
        count -= 1U;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_operator_compare_digits(const uint16_t *left, size_t left_count, const uint16_t *right, size_t right_count) {
    size_t index;

    left_count = __tinypy_operator_trim_digits(left, left_count);
    right_count = __tinypy_operator_trim_digits(right, right_count);
    if (left_count != right_count) {
        return left_count < right_count ? -1 : 1;
    }
    for (index = left_count; index != 0U; index -= 1U) {
        if (left[index - 1U] != right[index - 1U]) {
            return left[index - 1U] < right[index - 1U] ? -1 : 1;
        }
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_operator_subtract_digits(uint16_t *left, size_t left_count, const uint16_t *right, size_t right_count) {
    int32_t borrow = 0;
    size_t index;

    assert(__tinypy_operator_compare_digits(left, left_count, right, right_count) >= 0);
    for (index = 0U; index < left_count; ++index) {
        int32_t difference = (int32_t)left[index] - borrow - (index < right_count ? (int32_t)right[index] : 0);
        if (difference < 0) {
            difference += (int32_t)TINYPY_LONG_BASE;
            borrow = 1;
        }
        else {
            borrow = 0;
        }
        left[index] = (uint16_t)difference;
    }
    assert(borrow == 0);
    return __tinypy_operator_trim_digits(left, left_count);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_long_divide_views(tinypy_vm_t *vm, const tinypy_integer_view_t *left, const tinypy_integer_view_t *right, int want_remainder, tinypy_error_t **out_error) {
    size_t quotient_capacity;
    size_t remainder_capacity;
    uint16_t *quotient;
    uint16_t *remainder_digits;
    size_t quotient_count;
    size_t remainder_count = 0U;
    size_t total_bits;
    size_t bit_index;
    int quotient_sign;
    int remainder_sign;

    if (right->sign == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "long division by zero", out_error);
        return NULL;
    }
    assert(left->count != SIZE_MAX);
    quotient_capacity = left->count + 1U;
    assert(right->count != SIZE_MAX);
    remainder_capacity = right->count + 1U;
    assert(quotient_capacity <= SIZE_MAX / sizeof(*quotient));
    assert(remainder_capacity <= SIZE_MAX / sizeof(*remainder_digits));
    quotient = (uint16_t *)tinypy_internal_vm_allocate(vm, quotient_capacity * sizeof(*quotient), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    remainder_digits = (uint16_t *)tinypy_internal_vm_allocate(vm, remainder_capacity * sizeof(*remainder_digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    (void)memset(quotient, 0, quotient_capacity * sizeof(*quotient));
    (void)memset(remainder_digits, 0, remainder_capacity * sizeof(*remainder_digits));
    assert(left->count <= SIZE_MAX / 15U);
    total_bits = left->count * 15U;
    for (bit_index = total_bits; bit_index != 0U; bit_index -= 1U) {
        uint32_t carry = (uint32_t)((left->digits[(bit_index - 1U) / 15U] >> ((bit_index - 1U) % 15U)) & UINT16_C(1));
        size_t index;

        for (index = 0U; index < remainder_count; ++index) {
            uint32_t shifted = (uint32_t)remainder_digits[index] * 2U + carry;
            remainder_digits[index] = (uint16_t)(shifted & TINYPY_LONG_MASK);
            carry = shifted >> 15U;
        }
        if (carry != 0U) {
            remainder_digits[remainder_count] = (uint16_t)carry;
            remainder_count += 1U;
        }
        if (__tinypy_operator_compare_digits(remainder_digits, remainder_count, right->digits, right->count) >= 0) {
            remainder_count = __tinypy_operator_subtract_digits(remainder_digits, remainder_count, right->digits, right->count);
            quotient[(bit_index - 1U) / 15U] = (uint16_t)(quotient[(bit_index - 1U) / 15U] | (uint16_t)(UINT16_C(1) << ((bit_index - 1U) % 15U)));
        }
    }
    quotient_count = __tinypy_operator_trim_digits(quotient, quotient_capacity);
    quotient_sign = quotient_count == 0U ? 0 : (left->sign == right->sign ? 1 : -1);
    remainder_sign = remainder_count == 0U ? 0 : right->sign;
    if (left->sign != right->sign && remainder_count != 0U) {
        uint32_t carry = 1U;
        size_t index;

        for (index = 0U; index < quotient_capacity && carry != 0U; ++index) {
            uint32_t incremented = (uint32_t)quotient[index] + carry;
            quotient[index] = (uint16_t)(incremented & TINYPY_LONG_MASK);
            carry = incremented >> 15U;
        }
        assert(carry == 0U);
        quotient_count = __tinypy_operator_trim_digits(quotient, quotient_capacity);
        quotient_sign = -1; {
            int32_t borrow = 0;
            size_t previous_remainder_count = remainder_count;

            for (index = 0U; index < right->count; ++index) {
                int32_t difference = (int32_t)right->digits[index] - borrow - (index < previous_remainder_count ? (int32_t)remainder_digits[index] : 0);
                if (difference < 0) {
                    difference += (int32_t)TINYPY_LONG_BASE;
                    borrow = 1;
                }
                else {
                    borrow = 0;
                }
                remainder_digits[index] = (uint16_t)difference;
            }
            assert(borrow == 0);
            remainder_count = __tinypy_operator_trim_digits(remainder_digits, right->count);
        }
        remainder_sign = right->sign;
    }
    tinypy_value_t *result = want_remainder != 0 ? tinypy_long_from_base15_digits(vm, remainder_sign, remainder_digits, remainder_count) : tinypy_long_from_base15_digits(vm, quotient_sign, quotient, quotient_count);
    tinypy_internal_vm_deallocate(vm, remainder_digits, remainder_capacity * sizeof(*remainder_digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    tinypy_internal_vm_deallocate(vm, quotient, quotient_capacity * sizeof(*quotient), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_operator_add_overflow(int64_t left, int64_t right, int64_t *result) {
    if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right)) {
        return 1;
    }
    *result = left + right;
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_operator_subtract_overflow(int64_t left, int64_t right, int64_t *result) {
    if ((right < 0 && left > INT64_MAX + right) || (right > 0 && left < INT64_MIN + right)) {
        return 1;
    }
    *result = left - right;
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_operator_multiply_overflow(int64_t left, int64_t right, int64_t *result) {
    if (left == 0 || right == 0) {
        *result = 0;
        return 0;
    }
    if ((left == -1 && right == INT64_MIN) || (right == -1 && left == INT64_MIN)) {
        return 1;
    }
    if (left > 0) {
        if ((right > 0 && left > INT64_MAX / right) || (right < 0 && right < INT64_MIN / left)) {
            return 1;
        }
    }
    else if ((right > 0 && left < INT64_MIN / right) || (right < 0 && left < INT64_MAX / right)) {
        return 1;
    }
    *result = left * right;
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static double __tinypy_operator_as_double(const tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_FLOAT) {
        return TINYPY_FLOAT_OBJECT(value)->value;
    }
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        return (double)TINYPY_INTEGER_VALUE(value);
    } {
        double result = 0.0;
        size_t index = TINYPY_LONG_DIGIT_COUNT(value);
        while (index != 0U) {
            index -= 1U;
            result = result * (double)TINYPY_LONG_BASE + (double)TINYPY_LONG_OBJECT(value)->digits[index];
        }
        return TINYPY_LONG_SIGN(value) < 0 ? -result : result;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_operator_as_complex(const tinypy_value_t *value, double *real, double *imaginary) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_COMPLEX) {
        *real = TINYPY_COMPLEX_OBJECT(value)->real;
        *imaginary = TINYPY_COMPLEX_OBJECT(value)->imaginary;
    }
    else {
        *real = __tinypy_operator_as_double(value);
        *imaginary = 0.0;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_operator_complex_power(double left_real, double left_imaginary, double right_real, double right_imaginary, double *result_real, double *result_imaginary) {
    double radius = hypot(left_real, left_imaginary);
    double magnitude = pow(radius, right_real);
    double argument = atan2(left_imaginary, left_real);
    double phase = argument * right_real;

    if (right_imaginary != 0.0) {
        magnitude /= exp(argument * right_imaginary);
        phase += right_imaginary * log(radius);
    }
    *result_real = magnitude * cos(phase);
    *result_imaginary = magnitude * sin(phase);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_numeric_add(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, int subtract, tinypy_error_t **out_error) {
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported numeric operands", out_error);
        return NULL;
    }
    if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
        double left_real, left_imaginary, right_real, right_imaginary;
        __tinypy_operator_as_complex(left, &left_real, &left_imaginary);
        __tinypy_operator_as_complex(right, &right_real, &right_imaginary);
        return tinypy_complex_from_doubles(vm, left_real + (subtract != 0 ? -right_real : right_real), left_imaginary + (subtract != 0 ? -right_imaginary : right_imaginary));
    }
    if (left_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_FLOAT) {
        double operator_as_double = __tinypy_operator_as_double(left);
        double right_value = __tinypy_operator_as_double(right);
        double result = subtract != 0 ? operator_as_double - right_value : operator_as_double + right_value;
        return tinypy_float_from_double(vm, result);
    }
    if (__tinypy_operator_is_integer(left_kind) != 0 && __tinypy_operator_is_integer(right_kind) != 0) {
        if (left_kind != TINYPY_VALUE_LONG && right_kind != TINYPY_VALUE_LONG) {
            int64_t value;
            int overflow = subtract != 0 ? __tinypy_operator_subtract_overflow(TINYPY_INTEGER_VALUE(left), TINYPY_INTEGER_VALUE(right), &value) : __tinypy_operator_add_overflow(TINYPY_INTEGER_VALUE(left), TINYPY_INTEGER_VALUE(right), &value);
            if (overflow == 0) {
                return tinypy_integer_from_i64(vm, value);
            }
        } {
            tinypy_integer_view_t left_view;
            tinypy_integer_view_t right_view;
            __tinypy_operator_integer_view(left, &left_view);
            __tinypy_operator_integer_view(right, &right_view);
            return __tinypy_operator_long_add_views(vm, &left_view, &right_view, subtract);
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported numeric operands", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_concat_text(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, int unicode) {
    const void *left_bytes;
    const void *right_bytes;
    size_t left_size;
    size_t right_size;
    unsigned char *buffer;

    if (unicode != 0) {
        size_t left_code_points;
        size_t right_code_points;

        left_bytes = tinypy_unicode_utf8_view(left, &left_size, &left_code_points);
        right_bytes = tinypy_unicode_utf8_view(right, &right_size, &right_code_points);
    }
    else {
        left_bytes = tinypy_string_view(left, &left_size);
        right_bytes = tinypy_string_view(right, &right_size);
    }
    assert(left_size <= SIZE_MAX - right_size);
    if (left_size + right_size == 0U) {
        return unicode != 0 ? tinypy_unicode_from_utf8(vm, "", 0U) : tinypy_string_from_bytes(vm, NULL, 0U);
    }
    buffer = (unsigned char *)tinypy_internal_vm_allocate(vm, left_size + right_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    if (left_size != 0U) {
        (void)memcpy(buffer, left_bytes, left_size);
    }
    if (right_size != 0U) {
        (void)memcpy(buffer + left_size, right_bytes, right_size);
    }
    tinypy_value_t *result = unicode != 0 ? tinypy_unicode_from_utf8(vm, (const char *)buffer, left_size + right_size) : tinypy_string_from_bytes(vm, buffer, left_size + right_size);
    tinypy_internal_vm_deallocate(vm, buffer, left_size + right_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_operator_digits_as_i64(int sign, const uint16_t *digits, size_t count, int64_t *out_value) {
    uint64_t magnitude = 0U;
    size_t index;

    if (count > 5U) {
        return 0;
    }
    for (index = count; index != 0U; index -= 1U) {
        if (magnitude > (UINT64_MAX >> 15U)) {
            return 0;
        }
        magnitude = (magnitude << 15U) | digits[index - 1U];
    }
    if (sign >= 0) {
        if (magnitude > (uint64_t)INT64_MAX) {
            return 0;
        }
        *out_value = (int64_t)magnitude;
    }
    else {
        uint64_t limit = (uint64_t)INT64_MAX + UINT64_C(1);

        if (magnitude > limit) {
            return 0;
        }
        *out_value = magnitude == limit ? INT64_MIN : -(int64_t)magnitude;
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_integer_from_digits(tinypy_vm_t *vm, int sign, uint16_t *digits, size_t count, int prefer_long) {
    int64_t integer;

    count = __tinypy_operator_trim_digits(digits, count);
    if (count == 0U) {
        sign = 0;
    }
    if (prefer_long == 0 && __tinypy_operator_digits_as_i64(sign, digits, count, &integer) != 0) {
        return tinypy_integer_from_i64(vm, integer);
    }
    return tinypy_long_from_base15_digits(vm, sign, digits, count);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_operator_twos_complement(const tinypy_integer_view_t *view, uint16_t *digits, size_t width) {
    size_t index;

    for (index = 0U; index < width; ++index) {
        digits[index] = index < view->count ? view->digits[index] : 0U;
    }
    if (view->sign < 0) {
        uint32_t carry = 1U;

        for (index = 0U; index < width; ++index) {
            uint32_t value = (uint32_t)(TINYPY_LONG_MASK ^ digits[index]) + carry;

            digits[index] = (uint16_t)(value & TINYPY_LONG_MASK);
            carry = value >> 15U;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_integer_bitwise(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, int operation, tinypy_error_t **out_error) {
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);
    tinypy_integer_view_t left_view;
    tinypy_integer_view_t right_view;
    size_t width;
    uint16_t *left_digits;
    uint16_t *right_digits;
    size_t index;
    int sign;
    int prefer_long;

    if (__tinypy_operator_is_integer(left_kind) == 0 || __tinypy_operator_is_integer(right_kind) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bitwise operands must be integers", out_error);
        return NULL;
    }
    if (left_kind != TINYPY_VALUE_LONG && right_kind != TINYPY_VALUE_LONG) {
        int64_t left_integer = TINYPY_INTEGER_VALUE(left);
        int64_t right_integer = TINYPY_INTEGER_VALUE(right);
        int64_t integer = operation == 0 ? left_integer & right_integer : (operation == 1 ? left_integer ^ right_integer : left_integer | right_integer);

        if (left_kind == TINYPY_VALUE_BOOL && right_kind == TINYPY_VALUE_BOOL) {
            return tinypy_bool_from_i32(vm, integer != 0);
        }
        return tinypy_integer_from_i64(vm, integer);
    }
    __tinypy_operator_integer_view(left, &left_view);
    __tinypy_operator_integer_view(right, &right_view);
    width = (left_view.count > right_view.count ? left_view.count : right_view.count) + 1U;
    assert(width <= SIZE_MAX / sizeof(*left_digits));
    left_digits = (uint16_t *)tinypy_internal_vm_allocate(vm, width * sizeof(*left_digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    right_digits = (uint16_t *)tinypy_internal_vm_allocate(vm, width * sizeof(*right_digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    __tinypy_operator_twos_complement(&left_view, left_digits, width);
    __tinypy_operator_twos_complement(&right_view, right_digits, width);
    for (index = 0U; index < width; ++index) {
        left_digits[index] = (uint16_t)(operation == 0 ? left_digits[index] & right_digits[index] : (operation == 1 ? left_digits[index] ^ right_digits[index] : left_digits[index] | right_digits[index]));
    }
    sign = (left_digits[width - 1U] & UINT16_C(0x4000)) != 0U ? -1 : 1;
    if (sign < 0) {
        uint32_t carry = 1U;

        for (index = 0U; index < width; ++index) {
            uint32_t value = (uint32_t)(TINYPY_LONG_MASK ^ left_digits[index]) + carry;

            left_digits[index] = (uint16_t)(value & TINYPY_LONG_MASK);
            carry = value >> 15U;
        }
    }
    prefer_long = left_kind == TINYPY_VALUE_LONG || right_kind == TINYPY_VALUE_LONG;
    tinypy_value_t *result = __tinypy_operator_integer_from_digits(vm, sign, left_digits, width, prefer_long);
    tinypy_internal_vm_deallocate(vm, right_digits, width * sizeof(*right_digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    tinypy_internal_vm_deallocate(vm, left_digits, width * sizeof(*left_digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_operator_shift_count(tinypy_vm_t *vm, tinypy_value_t *value, size_t *out_shift, tinypy_error_t **out_error) {
    tinypy_integer_view_t view;
    size_t shift = 0U;
    size_t limit;
    size_t index;

    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    if (__tinypy_operator_is_integer(kind) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "shift count must be an integer", out_error);
        return 0;
    }
    __tinypy_operator_integer_view(value, &view);
    limit = view.sign < 0 ? (size_t)PTRDIFF_MAX + 1U : (size_t)PTRDIFF_MAX;
    for (index = view.count; index != 0U; index -= 1U) {
        if (shift > (limit >> 15U)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "shift count is too large", out_error);
            return 0;
        }
        shift = (shift << 15U) | view.digits[index - 1U];
        if (shift > limit) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "shift count is too large", out_error);
            return 0;
        }
    }
    if (view.sign < 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "negative shift count", out_error);
        return 0;
    }
    *out_shift = shift;
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_integer_left_shift(tinypy_vm_t *vm, tinypy_value_t *left, size_t shift, int prefer_long, tinypy_error_t **out_error) {
    tinypy_integer_view_t view;
    size_t digit_shift = shift / 15U;
    size_t bit_shift = shift % 15U;
    size_t capacity;
    uint16_t *digits;
    uint32_t carry = 0U;
    size_t index;

    __tinypy_operator_integer_view(left, &view);
    if (view.sign == 0) {
        return prefer_long != 0 ? tinypy_long_from_base15_digits(vm, 0, NULL, 0U) : tinypy_integer_from_i64(vm, 0);
    }
    if (digit_shift > (size_t)PTRDIFF_MAX - view.count - 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "left shift result is too large", out_error);
        return NULL;
    }
    capacity = view.count + digit_shift + (bit_shift != 0U ? 1U : 0U);
    assert(capacity != 0U && capacity <= SIZE_MAX / sizeof(*digits));
    digits = (uint16_t *)tinypy_internal_vm_allocate(vm, capacity * sizeof(*digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    (void)memset(digits, 0, capacity * sizeof(*digits));
    for (index = 0U; index < view.count; ++index) {
        uint32_t shifted = ((uint32_t)view.digits[index] << bit_shift) | carry;

        digits[index + digit_shift] = (uint16_t)(shifted & TINYPY_LONG_MASK);
        carry = shifted >> 15U;
    }
    if (bit_shift != 0U) {
        digits[view.count + digit_shift] = (uint16_t)carry;
    }
    tinypy_value_t *result = __tinypy_operator_integer_from_digits(vm, view.sign, digits, capacity, prefer_long);
    tinypy_internal_vm_deallocate(vm, digits, capacity * sizeof(*digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_integer_right_shift(tinypy_vm_t *vm, tinypy_value_t *left, size_t shift, int prefer_long) {
    tinypy_integer_view_t view;
    size_t digit_shift = shift / 15U;
    size_t bit_shift = shift % 15U;
    size_t capacity;
    uint16_t *digits;
    int discarded = 0;
    size_t index;
    __tinypy_operator_integer_view(left, &view);
    if (view.sign == 0) {
        return prefer_long != 0 ? tinypy_long_from_base15_digits(vm, 0, NULL, 0U) : tinypy_integer_from_i64(vm, 0);
    }
    if (digit_shift >= view.count) {
        return prefer_long != 0 ? tinypy_long_from_i64(vm, view.sign < 0 ? -1 : 0) : tinypy_integer_from_i64(vm, view.sign < 0 ? -1 : 0);
    }
    capacity = view.count - digit_shift + 1U;
    assert(capacity <= SIZE_MAX / sizeof(*digits));
    digits = (uint16_t *)tinypy_internal_vm_allocate(vm, capacity * sizeof(*digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    (void)memset(digits, 0, capacity * sizeof(*digits));
    for (index = 0U; index < digit_shift; ++index) {
        if (view.digits[index] != 0U) {
            discarded = 1;
        }
    }
    if (bit_shift != 0U && (view.digits[digit_shift] & (uint16_t)((UINT16_C(1) << bit_shift) - 1U)) != 0U) {
        discarded = 1;
    }
    for (index = 0U; index < view.count - digit_shift; ++index) {
        size_t source = index + digit_shift;
        uint32_t value = (uint32_t)view.digits[source] >> bit_shift;

        if (bit_shift != 0U && source + 1U < view.count) {
            value |= ((uint32_t)view.digits[source + 1U] << (15U - bit_shift)) & TINYPY_LONG_MASK;
        }
        digits[index] = (uint16_t)value;
    }
    if (view.sign < 0 && discarded != 0) {
        uint32_t carry = 1U;

        for (index = 0U; index < capacity && carry != 0U; ++index) {
            uint32_t value = (uint32_t)digits[index] + carry;

            digits[index] = (uint16_t)(value & TINYPY_LONG_MASK);
            carry = value >> 15U;
        }
        assert(carry == 0U);
    }
    tinypy_value_t *result = __tinypy_operator_integer_from_digits(vm, view.sign, digits, capacity, prefer_long);
    tinypy_internal_vm_deallocate(vm, digits, capacity * sizeof(*digits), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_operator_integer_exponent(tinypy_vm_t *vm, tinypy_value_t *value, size_t *out_exponent, tinypy_error_t **out_error) {
    tinypy_integer_view_t view;
    size_t exponent = 0U;
    size_t index;

    assert(__tinypy_operator_is_integer(TINYPY_VALUE_KIND(value)) != 0);
    __tinypy_operator_integer_view(value, &view);
    assert(view.sign >= 0);
    for (index = view.count; index != 0U; index -= 1U) {
        if (exponent > (SIZE_MAX >> 15U)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "exponent is too large", out_error);
            return 0;
        }
        exponent = (exponent << 15U) | view.digits[index - 1U];
    }
    *out_exponent = exponent;
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_call_unary_special(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_value_t *method = tinypy_object_get_attr(value, name, name_size, out_error);

    if (method == NULL) {
        return NULL;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, NULL, 0U);
    tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_positive(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;

    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    TINYPY_CLEAR_ERROR(out_error);
    kind = TINYPY_VALUE_KIND(value);
    if (__tinypy_operator_is_integer(kind) != 0 || kind == TINYPY_VALUE_FLOAT || kind == TINYPY_VALUE_COMPLEX) {
        TINYPY_INCREF(value);
        return value;
    }
    if (value->type->number_slots != NULL && value->type->number_slots->positive != NULL) {
        return value->type->number_slots->positive(value, out_error);
    }
    if (tinypy_internal_object_has_special(value, "__pos__", 7U) != 0) {
        return __tinypy_operator_call_unary_special(value, "__pos__", 7U, out_error);
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bad operand for unary plus", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_negative(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;

    assert(value != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    assert(tinypy_internal_vm_valid(vm));
    TINYPY_CLEAR_ERROR(out_error);
    kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        int64_t integer = TINYPY_INTEGER_VALUE(value);
        if (integer != INT64_MIN) {
            return tinypy_integer_from_i64(vm, -integer);
        }
    }
    if (__tinypy_operator_is_integer(kind) != 0) {
        tinypy_integer_view_t view;
        __tinypy_operator_integer_view(value, &view);
        return tinypy_long_from_base15_digits(vm, -view.sign, view.digits, view.count);
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        return tinypy_float_from_double(vm, -TINYPY_FLOAT_OBJECT(value)->value);
    }
    if (kind == TINYPY_VALUE_COMPLEX) {
        return tinypy_complex_from_doubles(vm, -TINYPY_COMPLEX_OBJECT(value)->real, -TINYPY_COMPLEX_OBJECT(value)->imaginary);
    }
    if (value->type->number_slots != NULL && value->type->number_slots->negative != NULL) {
        return value->type->number_slots->negative(value, out_error);
    }
    if (tinypy_internal_object_has_special(value, "__neg__", 7U) != 0) {
        return __tinypy_operator_call_unary_special(value, "__neg__", 7U, out_error);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bad operand for unary minus", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_invert(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;

    assert(value != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    assert(tinypy_internal_vm_valid(vm));
    TINYPY_CLEAR_ERROR(out_error);
    kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        return tinypy_integer_from_i64(vm, ~TINYPY_INTEGER_VALUE(value));
    }
    if (kind == TINYPY_VALUE_LONG) {
        tinypy_value_t *negative = tinypy_negative(value, out_error);
        tinypy_value_t *one;
        tinypy_value_t *result;

        if (negative == NULL) {
            return NULL;
        }
        one = tinypy_integer_from_i64(vm, 1);
        result = tinypy_subtract(negative, one, out_error);
        TINYPY_DECREF(one);
        TINYPY_DECREF(negative);
        return result;
    }
    if (value->type->number_slots != NULL && value->type->number_slots->invert != NULL) {
        return value->type->number_slots->invert(value, out_error);
    }
    if (tinypy_internal_object_has_special(value, "__invert__", 10U) != 0) {
        return __tinypy_operator_call_unary_special(value, "__invert__", 10U, out_error);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bad operand for unary invert", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_call_special(tinypy_value_t *receiver, const char *name, size_t name_size, tinypy_value_t *argument, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(receiver);
    tinypy_value_t *method = tinypy_object_get_attr(receiver, name, name_size, out_error);

    if (method == NULL) {
        return NULL;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &argument, 1U);
    tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_special_binary(tinypy_value_t *left, tinypy_value_t *right, const char *name, size_t name_size, const char *reverse_name, size_t reverse_name_size, int32_t *out_handled, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    *out_handled = INT32_C(0);
    if (tinypy_internal_object_has_special(left, name, name_size) != 0) {
        tinypy_value_t *result;

        *out_handled = INT32_C(1);
        result = __tinypy_operator_call_special(left, name, name_size, right, out_error);
        if (result == NULL) {
            return NULL;
        }
        if (result != &vm->not_implemented_object.base) {
            return result;
        }
        TINYPY_DECREF(result);
    }
    if (tinypy_internal_object_has_special(right, reverse_name, reverse_name_size) != 0) {
        tinypy_value_t *result;

        *out_handled = INT32_C(1);
        result = __tinypy_operator_call_special(right, reverse_name, reverse_name_size, left, out_error);
        if (result == NULL) {
            return NULL;
        }
        if (result != &vm->not_implemented_object.base) {
            return result;
        }
        TINYPY_DECREF(result);
    }
    *out_handled = INT32_C(0);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_operator_native_binary_e {
    TINYPY_OPERATOR_NATIVE_ADD,
    TINYPY_OPERATOR_NATIVE_SUBTRACT,
    TINYPY_OPERATOR_NATIVE_MULTIPLY,
    TINYPY_OPERATOR_NATIVE_DIVIDE
} tinypy_operator_native_binary_e;
//////////////////////////////////////////////////////////////////////////
static tinypy_binary_slot_t __tinypy_operator_native_binary_slot(const tinypy_type_t *type, tinypy_operator_native_binary_e operation, int32_t reflected) {
    const tinypy_number_slots_t *slots = type->number_slots;

    if (slots == NULL) {
        return NULL;
    }
    switch (operation) {
    case TINYPY_OPERATOR_NATIVE_ADD:
        return reflected != 0 ? slots->reflected_add : slots->add;
    case TINYPY_OPERATOR_NATIVE_SUBTRACT:
        return reflected != 0 ? slots->reflected_subtract : slots->subtract;
    case TINYPY_OPERATOR_NATIVE_MULTIPLY:
        return reflected != 0 ? slots->reflected_multiply : slots->multiply;
    case TINYPY_OPERATOR_NATIVE_DIVIDE:
        return reflected != 0 ? slots->reflected_divide : slots->divide;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_native_binary(tinypy_value_t *left, tinypy_value_t *right, tinypy_operator_native_binary_e operation, int32_t *out_handled, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_binary_slot_t left_slot = __tinypy_operator_native_binary_slot(left->type, operation, INT32_C(0));
    tinypy_binary_slot_t right_slot = NULL;

    *out_handled = INT32_C(0);
    if (right->type != left->type) {
        tinypy_binary_slot_t right_direct_slot = __tinypy_operator_native_binary_slot(right->type, operation, INT32_C(0));

        if (right_direct_slot != left_slot) {
            right_slot = __tinypy_operator_native_binary_slot(right->type, operation, INT32_C(1));
        }
    }
    if (left_slot != NULL) {
        tinypy_value_t *result;

        if (right_slot != NULL && tinypy_type_is_subtype(right->type, left->type) != 0) {
            result = right_slot(right, left, out_error);
            if (result == NULL) {
                *out_handled = INT32_C(1);
                return NULL;
            }
            if (result != &vm->not_implemented_object.base) {
                *out_handled = INT32_C(1);
                return result;
            }
            TINYPY_DECREF(result);
            right_slot = NULL;
        }
        result = left_slot(left, right, out_error);
        if (result == NULL) {
            *out_handled = INT32_C(1);
            return NULL;
        }
        if (result != &vm->not_implemented_object.base) {
            *out_handled = INT32_C(1);
            return result;
        }
        TINYPY_DECREF(result);
    }
    if (right_slot != NULL) {
        tinypy_value_t *result = right_slot(right, left, out_error);

        if (result == NULL) {
            *out_handled = INT32_C(1);
            return NULL;
        }
        if (result != &vm->not_implemented_object.base) {
            *out_handled = INT32_C(1);
            return result;
        }
        TINYPY_DECREF(result);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_concat_sequence(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    size_t left_size = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(left) : TINYPY_LIST_SIZE(left);
    size_t right_size = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(right) : TINYPY_LIST_SIZE(right);
    size_t index;

    assert(left_size <= SIZE_MAX - right_size);
    tinypy_value_t **items = left_size + right_size != 0U ? (tinypy_value_t **)tinypy_internal_vm_allocate(vm, (left_size + right_size) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY) : NULL;
    for (index = 0U; index < left_size; ++index) {
        items[index] = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(left, index) : TINYPY_LIST_GET(left, index);
    }
    for (index = 0U; index < right_size; ++index) {
        items[left_size + index] = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(right, index) : TINYPY_LIST_GET(right, index);
    }
    tinypy_value_t *result = kind == TINYPY_VALUE_TUPLE ? tinypy_tuple_from_items(vm, items, left_size + right_size) : tinypy_list_from_items(vm, items, left_size + right_size);
    if (items != NULL) {
        tinypy_internal_vm_deallocate(vm, items, (left_size + right_size) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_operator_repeat_count(tinypy_value_t *value, size_t *out_count) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        int64_t count = TINYPY_INTEGER_VALUE(value);

        *out_count = count <= 0 ? 0U : (size_t)count;
        return count <= 0 || (uint64_t)count <= (uint64_t)SIZE_MAX ? INT32_C(1) : INT32_C(0);
    }
    if (kind == TINYPY_VALUE_LONG && TINYPY_LONG_DIGIT_COUNT(value) <= 4U) {
        uint64_t count = 0U;
        size_t index = TINYPY_LONG_DIGIT_COUNT(value);

        if (TINYPY_LONG_SIGN(value) <= 0) {
            *out_count = 0U;
            return INT32_C(1);
        }
        while (index != 0U) {
            index -= 1U;
            count = (count << 15U) | TINYPY_LONG_OBJECT(value)->digits[index];
        }
        if (count <= (uint64_t)SIZE_MAX) {
            *out_count = (size_t)count;
            return INT32_C(1);
        }
    }
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_repeat(tinypy_vm_t *vm, tinypy_value_t *sequence, size_t count) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(sequence);
    size_t unit_size;
    size_t total_size;
    size_t index;

    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        const unsigned char *bytes = TINYPY_TEXT_BYTES(sequence);
        unit_size = TINYPY_TEXT_BYTE_SIZE(sequence);
        assert(unit_size == 0U || count <= SIZE_MAX / unit_size);
        total_size = unit_size * count;
        if (total_size == 0U) {
            return kind == TINYPY_VALUE_STRING ? tinypy_string_from_bytes(vm, NULL, 0U) : tinypy_unicode_from_utf8(vm, NULL, 0U);
        } {
            unsigned char *buffer = (unsigned char *)tinypy_internal_vm_allocate(vm, total_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            tinypy_value_t *result;

            for (index = 0U; index < count; ++index) {
                (void)memcpy(buffer + index * unit_size, bytes, unit_size);
            }
            result = kind == TINYPY_VALUE_STRING ? tinypy_string_from_bytes(vm, buffer, total_size) : tinypy_unicode_from_utf8(vm, (const char *)buffer, total_size);
            tinypy_internal_vm_deallocate(vm, buffer, total_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            return result;
        }
    }
    unit_size = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(sequence) : TINYPY_LIST_SIZE(sequence);
    assert(unit_size == 0U || count <= SIZE_MAX / unit_size);
    total_size = unit_size * count; {
        tinypy_value_t **items = total_size != 0U ? (tinypy_value_t **)tinypy_internal_vm_allocate(vm, total_size * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY) : NULL;
        tinypy_value_t *result;

        for (index = 0U; index < total_size; ++index) {
            items[index] = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(sequence, index % unit_size) : TINYPY_LIST_GET(sequence, index % unit_size);
        }
        result = kind == TINYPY_VALUE_TUPLE ? tinypy_tuple_from_items(vm, items, total_size) : tinypy_list_from_items(vm, items, total_size);
        if (items != NULL) {
            tinypy_internal_vm_deallocate(vm, items, total_size * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        }
        return result;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_add(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_type_e left_kind;
    tinypy_value_type_e right_kind;
    int32_t handled;
    tinypy_value_t *native_result;

    assert(left != NULL && right != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, right));
    TINYPY_CLEAR_ERROR(out_error);
    native_result = __tinypy_operator_native_binary(left, right, TINYPY_OPERATOR_NATIVE_ADD, &handled, out_error);
    if (handled != 0) {
        return native_result;
    }
    left_kind = TINYPY_VALUE_KIND(left);
    right_kind = TINYPY_VALUE_KIND(right);
    if (left_kind == TINYPY_VALUE_STRING && right_kind == TINYPY_VALUE_STRING) {
        return __tinypy_operator_concat_text(vm, left, right, 0);
    }
    if (left_kind == TINYPY_VALUE_UNICODE && right_kind == TINYPY_VALUE_UNICODE) {
        return __tinypy_operator_concat_text(vm, left, right, 1);
    }
    if ((left_kind == TINYPY_VALUE_TUPLE || left_kind == TINYPY_VALUE_LIST) && left_kind == right_kind) {
        return __tinypy_operator_concat_sequence(vm, left, right);
    }
    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__add__", 7U, "__radd__", 8U, &handled, out_error);

        if (handled != 0) {
            return special;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported add operands", out_error);
        return NULL;
    }
    return __tinypy_operator_numeric_add(vm, left, right, 0, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_subtract(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    int32_t handled;
    tinypy_value_t *native_result;

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, right));
    TINYPY_CLEAR_ERROR(out_error);
    native_result = __tinypy_operator_native_binary(left, right, TINYPY_OPERATOR_NATIVE_SUBTRACT, &handled, out_error);
    if (handled != 0) {
        return native_result;
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_SET || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_FROZENSET) {
        return tinypy_internal_set_binary(left, right, INT32_C(3), out_error);
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    int condition = __tinypy_operator_is_number(kind) == 0;
    if (condition == 0) {
        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(right);
        condition = __tinypy_operator_is_number(kind_2) == 0;
    }
    if (condition) {
        tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__sub__", 7U, "__rsub__", 8U, &handled, out_error);

        if (handled != 0) {
            return special;
        }
    }
    return __tinypy_operator_numeric_add(vm, left, right, 1, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_multiply(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_type_e left_kind;
    tinypy_value_type_e right_kind;
    size_t repeat_count;
    int32_t handled;
    tinypy_value_t *native_result;

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, right));
    TINYPY_CLEAR_ERROR(out_error);
    native_result = __tinypy_operator_native_binary(left, right, TINYPY_OPERATOR_NATIVE_MULTIPLY, &handled, out_error);
    if (handled != 0) {
        return native_result;
    }
    left_kind = TINYPY_VALUE_KIND(left);
    right_kind = TINYPY_VALUE_KIND(right);
    if ((left_kind == TINYPY_VALUE_STRING || left_kind == TINYPY_VALUE_UNICODE || left_kind == TINYPY_VALUE_TUPLE || left_kind == TINYPY_VALUE_LIST) && __tinypy_operator_repeat_count(right, &repeat_count) != 0) {
        return __tinypy_operator_repeat(vm, left, repeat_count);
    }
    if ((right_kind == TINYPY_VALUE_STRING || right_kind == TINYPY_VALUE_UNICODE || right_kind == TINYPY_VALUE_TUPLE || right_kind == TINYPY_VALUE_LIST)) {
        if (__tinypy_operator_repeat_count(left, &repeat_count) != 0) {
            return __tinypy_operator_repeat(vm, right, repeat_count);
        }
    }
    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__mul__", 7U, "__rmul__", 8U, &handled, out_error);

        if (handled != 0) {
            return special;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported multiply operands", out_error);
        return NULL;
    }
    if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
        double ar, ai, br, bi;
        __tinypy_operator_as_complex(left, &ar, &ai);
        __tinypy_operator_as_complex(right, &br, &bi);
        return tinypy_complex_from_doubles(vm, ar * br - ai * bi, ar * bi + ai * br);
    }
    if (left_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_FLOAT) {
        double operator_as_double = __tinypy_operator_as_double(left);
        double operator_as_double_2 = __tinypy_operator_as_double(right);
        return tinypy_float_from_double(vm, operator_as_double * operator_as_double_2);
    }
    if (__tinypy_operator_is_integer(left_kind) != 0 && __tinypy_operator_is_integer(right_kind) != 0) {
        if (left_kind != TINYPY_VALUE_LONG && right_kind != TINYPY_VALUE_LONG) {
            int64_t value;
            if (__tinypy_operator_multiply_overflow(TINYPY_INTEGER_VALUE(left), TINYPY_INTEGER_VALUE(right), &value) == 0) {
                return tinypy_integer_from_i64(vm, value);
            }
        } {
            tinypy_integer_view_t left_view;
            tinypy_integer_view_t right_view;
            __tinypy_operator_integer_view(left, &left_view);
            __tinypy_operator_integer_view(right, &right_view);
            return __tinypy_operator_long_multiply_views(vm, &left_view, &right_view);
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported multiply operands", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_divide(tinypy_value_t *left, tinypy_value_t *right, int true_division, int remainder, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported division operands", out_error);
        return NULL;
    }

    if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
        double ar, ai, br, bi, denominator;
        if (remainder != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "complex modulo is not supported", out_error);
            return NULL;
        }
        __tinypy_operator_as_complex(left, &ar, &ai);
        __tinypy_operator_as_complex(right, &br, &bi);
        denominator = br * br + bi * bi;
        if (denominator == 0.0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "complex division by zero", out_error);
            return NULL;
        }
        return tinypy_complex_from_doubles(vm, (ar * br + ai * bi) / denominator, (ai * br - ar * bi) / denominator);
    }
    if (left_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_FLOAT || true_division != 0) {
        double divisor = __tinypy_operator_as_double(right);
        double dividend = __tinypy_operator_as_double(left);
        double quotient;
        if (divisor == 0.0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "division by zero", out_error);
            return NULL;
        }
        if (remainder != 0) {
            double value = fmod(dividend, divisor);
            if (value != 0.0 && ((divisor < 0.0) != (value < 0.0))) {
                value += divisor;
            }
            return tinypy_float_from_double(vm, value);
        }
        quotient = true_division != 0 || left_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_FLOAT ? dividend / divisor : floor(dividend / divisor);
        return tinypy_float_from_double(vm, quotient);
    }
    if (__tinypy_operator_is_integer(left_kind) != 0 && __tinypy_operator_is_integer(right_kind) != 0) {
        if (left_kind == TINYPY_VALUE_LONG || right_kind == TINYPY_VALUE_LONG) {
            tinypy_integer_view_t left_view;
            tinypy_integer_view_t right_view;

            __tinypy_operator_integer_view(left, &left_view);
            __tinypy_operator_integer_view(right, &right_view);
            return __tinypy_operator_long_divide_views(vm, &left_view, &right_view, remainder, out_error);
        }
        int64_t dividend = TINYPY_INTEGER_VALUE(left);
        int64_t divisor = TINYPY_INTEGER_VALUE(right);
        int64_t quotient;
        int64_t modulo;
        if (divisor == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "integer division by zero", out_error);
            return NULL;
        }
        if (dividend == INT64_MIN && divisor == -1) {
            if (remainder != 0) {
                return tinypy_integer_from_i64(vm, 0);
            } {
                tinypy_integer_view_t left_view;
                tinypy_integer_view_t minus_one;
                __tinypy_operator_integer_view(left, &left_view);
                __tinypy_operator_integer_view(right, &minus_one);
                return __tinypy_operator_long_multiply_views(vm, &left_view, &minus_one);
            }
        }
        quotient = dividend / divisor;
        modulo = dividend % divisor;
        if (modulo != 0 && ((modulo < 0) != (divisor < 0))) {
            modulo += divisor;
            quotient -= 1;
        }
        return tinypy_integer_from_i64(vm, remainder != 0 ? modulo : quotient);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported division operands", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    int32_t handled;
    tinypy_value_t *special;
    tinypy_value_t *native_result;

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(left)));
    assert(tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(left), right));
    TINYPY_CLEAR_ERROR(out_error);
    native_result = __tinypy_operator_native_binary(left, right, TINYPY_OPERATOR_NATIVE_DIVIDE, &handled, out_error);
    if (handled != 0) {
        return native_result;
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    int condition_2 = __tinypy_operator_is_number(kind) == 0;
    if (condition_2 == 0) {
        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(right);
        condition_2 = __tinypy_operator_is_number(kind_2) == 0;
    }
    if (condition_2) {
        special = __tinypy_operator_special_binary(left, right, "__div__", 7U, "__rdiv__", 8U, &handled, out_error);
        if (handled != 0) {
            return special;
        }
    }
    return __tinypy_operator_divide(left, right, 0, 0, out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_inplace_binary(tinypy_value_t *left, tinypy_value_t *right, tinypy_binary_slot_t inplace_slot, const char *special_name, size_t special_name_size, tinypy_binary_slot_t fallback, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    TINYPY_CLEAR_ERROR(out_error);
    if (inplace_slot != NULL) {
        tinypy_value_t *result = inplace_slot(left, right, out_error);

        if (result == NULL) {
            return NULL;
        }
        if (result != &vm->not_implemented_object.base) {
            return result;
        }
        TINYPY_DECREF(result);
        return fallback(left, right, out_error);
    }
    if (tinypy_internal_object_has_special(left, special_name, special_name_size) != 0) {
        tinypy_value_t *result = __tinypy_operator_call_special(left, special_name, special_name_size, right, out_error);

        if (result == NULL) {
            return NULL;
        }
        if (result != &vm->not_implemented_object.base) {
            return result;
        }
        TINYPY_DECREF(result);
    }
    return fallback(left, right, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_add(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_binary_slot_t slot;

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(left)));
    assert(tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(left), right));
    slot = left->type->number_slots != NULL ? left->type->number_slots->inplace_add : NULL;
    return __tinypy_operator_inplace_binary(left, right, slot, "__iadd__", 8U, tinypy_add, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_subtract(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_binary_slot_t slot;

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(left)));
    assert(tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(left), right));
    slot = left->type->number_slots != NULL ? left->type->number_slots->inplace_subtract : NULL;
    return __tinypy_operator_inplace_binary(left, right, slot, "__isub__", 8U, tinypy_subtract, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_multiply(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_binary_slot_t slot;

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(left)));
    assert(tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(left), right));
    slot = left->type->number_slots != NULL ? left->type->number_slots->inplace_multiply : NULL;
    return __tinypy_operator_inplace_binary(left, right, slot, "__imul__", 8U, tinypy_multiply, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_binary_slot_t slot;

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(left)));
    assert(tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(left), right));
    slot = left->type->number_slots != NULL ? left->type->number_slots->inplace_divide : NULL;
    return __tinypy_operator_inplace_binary(left, right, slot, "__idiv__", 8U, tinypy_divide, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_floor_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    int32_t handled;
    tinypy_value_t *special;

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(left)));
    assert(tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(left), right));
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    int condition_3 = __tinypy_operator_is_number(kind) == 0;
    if (condition_3 == 0) {
        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(right);
        condition_3 = __tinypy_operator_is_number(kind_2) == 0;
    }
    if (condition_3) {
        special = __tinypy_operator_special_binary(left, right, "__floordiv__", 12U, "__rfloordiv__", 13U, &handled, out_error);
        if (handled != 0) {
            return special;
        }
    }
    return __tinypy_operator_divide(left, right, 0, 0, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_true_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    int32_t handled;
    tinypy_value_t *special;

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(left)));
    assert(tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(left), right));
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    int condition_4 = __tinypy_operator_is_number(kind) == 0;
    if (condition_4 == 0) {
        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(right);
        condition_4 = __tinypy_operator_is_number(kind_2) == 0;
    }
    if (condition_4) {
        special = __tinypy_operator_special_binary(left, right, "__truediv__", 11U, "__rtruediv__", 12U, &handled, out_error);
        if (handled != 0) {
            return special;
        }
    }
    return __tinypy_operator_divide(left, right, 1, 0, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_remainder(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    int32_t handled;
    tinypy_value_t *special;

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(left)));
    assert(tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(left), right));
    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_UNICODE) {
        return tinypy_internal_string_percent(left, right, out_error);
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    int condition_5 = __tinypy_operator_is_number(kind) == 0;
    if (condition_5 == 0) {
        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(right);
        condition_5 = __tinypy_operator_is_number(kind_2) == 0;
    }
    if (condition_5) {
        special = __tinypy_operator_special_binary(left, right, "__mod__", 7U, "__rmod__", 8U, &handled, out_error);
        if (handled != 0) {
            return special;
        }
    }
    return __tinypy_operator_divide(left, right, 0, 1, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_power(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_type_e left_kind;
    tinypy_value_type_e right_kind;

    assert(left != NULL && right != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, right));
    TINYPY_CLEAR_ERROR(out_error);
    left_kind = TINYPY_VALUE_KIND(left);
    right_kind = TINYPY_VALUE_KIND(right);
    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        int32_t handled;
        tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__pow__", 7U, "__rpow__", 8U, &handled, out_error);

        if (handled != 0) {
            return special;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "power operands must be numeric", out_error);
        return NULL;
    }
    if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
        double left_real;
        double left_imaginary;
        double right_real;
        double right_imaginary;

        __tinypy_operator_as_complex(left, &left_real, &left_imaginary);
        __tinypy_operator_as_complex(right, &right_real, &right_imaginary);
        if (left_real == 0.0 && left_imaginary == 0.0) {
            if (right_real < 0.0 || right_imaginary != 0.0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "zero cannot be raised to a negative or complex power", out_error);
                return NULL;
            }
            if (right_real == 0.0) {
                return tinypy_complex_from_doubles(vm, 1.0, 0.0);
            }
            return tinypy_complex_from_doubles(vm, 0.0, 0.0);
        }
        double real;
        double imaginary;
        __tinypy_operator_complex_power(left_real, left_imaginary, right_real, right_imaginary, &real, &imaginary);
        return tinypy_complex_from_doubles(vm, real, imaginary);
    }
    if (left_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_FLOAT) {
        double base = __tinypy_operator_as_double(left);
        double exponent = __tinypy_operator_as_double(right);
        double result;

        if (base == 0.0 && exponent < 0.0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "zero cannot be raised to a negative power", out_error);
            return NULL;
        }
        if (base < 0.0 && floor(exponent) != exponent) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "negative number cannot be raised to a fractional power", out_error);
            return NULL;
        }
        result = pow(base, exponent);
        if (isnan(result)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid floating point power", out_error);
            return NULL;
        }
        if (isinf(result) && isfinite(base) && isfinite(exponent)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "floating point power overflow", out_error);
            return NULL;
        }
        return tinypy_float_from_double(vm, result);
    } {
        tinypy_integer_view_t exponent_view;
        size_t exponent;
        int prefer_long = left_kind == TINYPY_VALUE_LONG || right_kind == TINYPY_VALUE_LONG;
        tinypy_value_t *result;
        tinypy_value_t *base;

        __tinypy_operator_integer_view(right, &exponent_view);
        if (exponent_view.sign < 0) {
            double base_value = __tinypy_operator_as_double(left);
            double exponent_value = __tinypy_operator_as_double(right);

            if (base_value == 0.0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "zero cannot be raised to a negative power", out_error);
                return NULL;
            }
            double power = pow(base_value, exponent_value);
            return tinypy_float_from_double(vm, power);
        }
        if (__tinypy_operator_integer_exponent(vm, right, &exponent, out_error) == 0) {
            return NULL;
        }
        result = prefer_long != 0 ? tinypy_long_from_i64(vm, 1) : tinypy_integer_from_i64(vm, 1);
        base = left;
        TINYPY_INCREF(base);
        while (exponent != 0U) {
            if ((exponent & 1U) != 0U) {
                tinypy_value_t *multiplied = tinypy_multiply(result, base, out_error);

                TINYPY_DECREF(result);
                if (multiplied == NULL) {
                    TINYPY_DECREF(base);
                    return NULL;
                }
                result = multiplied;
            }
            exponent >>= 1U;
            if (exponent != 0U) {
                tinypy_value_t *squared = tinypy_multiply(base, base, out_error);

                TINYPY_DECREF(base);
                if (squared == NULL) {
                    TINYPY_DECREF(result);
                    return NULL;
                }
                base = squared;
            }
        }
        TINYPY_DECREF(base);
        return result;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_left_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    size_t shift;

    assert(left != NULL && right != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, right));
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    int condition_6 = __tinypy_operator_is_integer(kind) == 0;
    if (condition_6 == 0) {
        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(right);
        condition_6 = __tinypy_operator_is_integer(kind_2) == 0;
    }
    if (condition_6) {
        int32_t handled;
        tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__lshift__", 10U, "__rlshift__", 11U, &handled, out_error);

        if (handled != 0) {
            return special;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "left shift operand must be an integer", out_error);
        return NULL;
    }
    if (__tinypy_operator_shift_count(vm, right, &shift, out_error) == 0) {
        return NULL;
    }
    return __tinypy_operator_integer_left_shift(vm, left, shift, TINYPY_VALUE_KIND(left) == TINYPY_VALUE_LONG || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_LONG, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_right_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    size_t shift;

    assert(left != NULL && right != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, right));
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    int condition_7 = __tinypy_operator_is_integer(kind) == 0;
    if (condition_7 == 0) {
        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(right);
        condition_7 = __tinypy_operator_is_integer(kind_2) == 0;
    }
    if (condition_7) {
        int32_t handled;
        tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__rshift__", 10U, "__rrshift__", 11U, &handled, out_error);

        if (handled != 0) {
            return special;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "right shift operand must be an integer", out_error);
        return NULL;
    }
    if (__tinypy_operator_shift_count(vm, right, &shift, out_error) == 0) {
        return NULL;
    }
    return __tinypy_operator_integer_right_shift(vm, left, shift, TINYPY_VALUE_KIND(left) == TINYPY_VALUE_LONG || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_LONG);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bit_and(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, right));
    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_SET || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_FROZENSET) {
        return tinypy_internal_set_binary(left, right, INT32_C(0), out_error);
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    int condition_8 = __tinypy_operator_is_integer(kind) == 0;
    if (condition_8 == 0) {
        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(right);
        condition_8 = __tinypy_operator_is_integer(kind_2) == 0;
    }
    if (condition_8) {
        int32_t handled;
        tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__and__", 7U, "__rand__", 8U, &handled, out_error);

        if (handled != 0) {
            return special;
        }
    }
    return __tinypy_operator_integer_bitwise(vm, left, right, 0, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bit_xor(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, right));
    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_SET || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_FROZENSET) {
        return tinypy_internal_set_binary(left, right, INT32_C(1), out_error);
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    int condition_9 = __tinypy_operator_is_integer(kind) == 0;
    if (condition_9 == 0) {
        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(right);
        condition_9 = __tinypy_operator_is_integer(kind_2) == 0;
    }
    if (condition_9) {
        int32_t handled;
        tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__xor__", 7U, "__rxor__", 8U, &handled, out_error);

        if (handled != 0) {
            return special;
        }
    }
    return __tinypy_operator_integer_bitwise(vm, left, right, 1, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bit_or(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, right));
    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_SET || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_FROZENSET) {
        return tinypy_internal_set_binary(left, right, INT32_C(2), out_error);
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    int condition_10 = __tinypy_operator_is_integer(kind) == 0;
    if (condition_10 == 0) {
        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(right);
        condition_10 = __tinypy_operator_is_integer(kind_2) == 0;
    }
    if (condition_10) {
        int32_t handled;
        tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__or__", 6U, "__ror__", 7U, &handled, out_error);

        if (handled != 0) {
            return special;
        }
    }
    return __tinypy_operator_integer_bitwise(vm, left, right, 2, out_error);
}
