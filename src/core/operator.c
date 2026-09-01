#include "tinypy/operator.h"

#include "internal.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#define TINYPY_LONG_BASE UINT32_C(32768)
#define TINYPY_LONG_MASK UINT32_C(32767)

typedef struct tinypy_integer_view_t {
    int32_t sign;
    const uint16_t *digits;
    size_t count;
    uint16_t local_digits[5];
} tinypy_integer_view_t;

typedef enum tinypy_operator_division_e {
    TINYPY_OPERATOR_DIVISION_CLASSIC,
    TINYPY_OPERATOR_DIVISION_TRUE,
    TINYPY_OPERATOR_DIVISION_FLOOR,
    TINYPY_OPERATOR_DIVISION_REMAINDER
} tinypy_operator_division_e;

typedef enum tinypy_operator_complex_power_result_e {
    TINYPY_OPERATOR_COMPLEX_POWER_OK,
    TINYPY_OPERATOR_COMPLEX_POWER_ZERO_DIVISION,
    TINYPY_OPERATOR_COMPLEX_POWER_OVERFLOW
} tinypy_operator_complex_power_result_e;

typedef enum tinypy_operator_long_division_result_e {
    TINYPY_OPERATOR_LONG_DIVISION_QUOTIENT,
    TINYPY_OPERATOR_LONG_DIVISION_REMAINDER,
    TINYPY_OPERATOR_LONG_DIVISION_PAIR
} tinypy_operator_long_division_result_e;

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_is_integer(tinypy_value_type_e kind) {
    return kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_is_number(tinypy_value_type_e kind) {
    tinypy_bool_t return_value_1 = __tinypy_operator_is_integer(kind) != 0 || kind == TINYPY_VALUE_FLOAT || kind == TINYPY_VALUE_COMPLEX;
    return return_value_1;
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
static int32_t __tinypy_operator_magnitude_compare(const tinypy_integer_view_t *left, const tinypy_integer_view_t *right) {
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
static tinypy_value_t *__tinypy_operator_long_add_views(tinypy_vm_t *vm, const tinypy_integer_view_t *left, const tinypy_integer_view_t *right, int32_t subtract_right, tinypy_error_t **out_error) {
    int32_t right_sign = subtract_right != 0 ? -right->sign : right->sign;
    size_t maximum_count = left->count > right->count ? left->count : right->count;
    size_t capacity;
    uint16_t *digits;
    size_t count = 0U;
    int32_t sign;

    if (maximum_count == SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long int is too large", out_error);
        return NULL;
    }
    capacity = maximum_count + 1U;
    if (capacity > SIZE_MAX / sizeof(*digits)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long int is too large", out_error);
        return NULL;
    }
    digits = (uint16_t *)tinypy_internal_vm_allocate_checked(vm, capacity * sizeof(*digits), out_error);
    if (digits == NULL) {
        return NULL;
    }
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
        int32_t comparison = __tinypy_operator_magnitude_compare(left, right);
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
    tinypy_value_t *result = tinypy_internal_long_from_base15_digits_checked(vm, sign, digits, count, out_error);
    tinypy_internal_vm_deallocate(vm, digits, capacity * sizeof(*digits));
    return result;
}
//////////////////////////////////////////////////////////////////////////
#define TINYPY_LONG_KARATSUBA_CUTOFF 128U

static size_t __tinypy_operator_trim_digits(const uint16_t *digits, size_t count);

static void __tinypy_operator_long_multiply_schoolbook(uint16_t *output, size_t output_capacity, const uint16_t *left, size_t left_count, const uint16_t *right, size_t right_count) {
    size_t left_index;

    for (left_index = 0U; left_index < left_count; ++left_index) {
        uint32_t carry = 0U;
        size_t right_index;

        for (right_index = 0U; right_index < right_count; ++right_index) {
            size_t output_index = left_index + right_index;
            uint32_t product = (uint32_t)output[output_index] + (uint32_t)left[left_index] * (uint32_t)right[right_index] + carry;

            output[output_index] = (uint16_t)(product & TINYPY_LONG_MASK);
            carry = product >> 15U;
        }
        right_index = left_index + right_count;
        while (carry != 0U && right_index < output_capacity) {
            uint32_t sum = (uint32_t)output[right_index] + carry;

            output[right_index] = (uint16_t)(sum & TINYPY_LONG_MASK);
            carry = sum >> 15U;
            right_index += 1U;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_operator_long_add_digits(uint16_t *output, const uint16_t *left, size_t left_count, const uint16_t *right, size_t right_count) {
    size_t count = left_count > right_count ? left_count : right_count;
    uint32_t carry = 0U;
    size_t index;

    for (index = 0U; index < count; ++index) {
        uint32_t sum = (index < left_count ? (uint32_t)left[index] : 0U) + (index < right_count ? (uint32_t)right[index] : 0U) + carry;

        output[index] = (uint16_t)(sum & TINYPY_LONG_MASK);
        carry = sum >> 15U;
    }
    if (carry != 0U) {
        output[count++] = (uint16_t)carry;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_operator_long_subtract_digits(uint16_t *target, size_t target_count, const uint16_t *subtrahend, size_t subtrahend_count) {
    uint32_t borrow = 0U;
    size_t index;

    for (index = 0U; index < target_count; ++index) {
        uint32_t amount = (index < subtrahend_count ? (uint32_t)subtrahend[index] : 0U) + borrow;

        if ((uint32_t)target[index] < amount) {
            target[index] = (uint16_t)((uint32_t)target[index] + TINYPY_LONG_BASE - amount);
            borrow = 1U;
        }
        else {
            target[index] = (uint16_t)((uint32_t)target[index] - amount);
            borrow = 0U;
        }
    }
    size_t return_value_1 = __tinypy_operator_trim_digits(target, target_count);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_operator_long_add_shifted(uint16_t *output, size_t output_capacity, const uint16_t *digits, size_t count, size_t shift) {
    uint32_t carry = 0U;
    size_t index;

    for (index = 0U; index < count; ++index) {
        uint32_t sum = (uint32_t)output[shift + index] + (uint32_t)digits[index] + carry;

        output[shift + index] = (uint16_t)(sum & TINYPY_LONG_MASK);
        carry = sum >> 15U;
    }
    index = shift + count;
    while (carry != 0U && index < output_capacity) {
        uint32_t sum = (uint32_t)output[index] + carry;

        output[index] = (uint16_t)(sum & TINYPY_LONG_MASK);
        carry = sum >> 15U;
        index += 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_long_multiply_digits(tinypy_vm_t *vm, uint16_t *output, size_t output_capacity, const uint16_t *left, size_t left_count, const uint16_t *right, size_t right_count, tinypy_error_t **out_error) {
    size_t minimum_count = left_count < right_count ? left_count : right_count;
    size_t maximum_count = left_count > right_count ? left_count : right_count;

    if (minimum_count == 0U) {
        return TINYPY_TRUE;
    }
    if (minimum_count < TINYPY_LONG_KARATSUBA_CUTOFF || (minimum_count <= SIZE_MAX / 2U && maximum_count >= minimum_count * 2U)) {
        __tinypy_operator_long_multiply_schoolbook(output, output_capacity, left, left_count, right, right_count);
        return TINYPY_TRUE;
    }

    size_t split = maximum_count / 2U;
    size_t left_low_count = split;
    size_t right_low_count = split;
    size_t left_high_count = left_count - split;
    size_t right_high_count = right_count - split;
    size_t zero_low_capacity;
    size_t zero_high_capacity;
    size_t left_sum_capacity;
    size_t right_sum_capacity;
    size_t cross_capacity;
    size_t scratch_capacity;
    uint16_t *scratch;
    uint16_t *zero_low;
    uint16_t *zero_high;
    uint16_t *left_sum;
    uint16_t *right_sum;
    uint16_t *cross;
    size_t left_sum_count;
    size_t right_sum_count;
    size_t zero_low_count;
    size_t zero_high_count;
    size_t cross_count;

    size_t left_sum_maximum = left_low_count > left_high_count ? left_low_count : left_high_count;
    size_t right_sum_maximum = right_low_count > right_high_count ? right_low_count : right_high_count;

    if (left_low_count > SIZE_MAX - right_low_count
        || left_high_count > SIZE_MAX - right_high_count
        || left_sum_maximum == SIZE_MAX
        || right_sum_maximum == SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long multiplication temporary storage is too large", out_error);
        return TINYPY_FALSE;
    }
    zero_low_capacity = left_low_count + right_low_count;
    zero_high_capacity = left_high_count + right_high_count;
    left_sum_capacity = left_sum_maximum + 1U;
    right_sum_capacity = right_sum_maximum + 1U;
    if (left_sum_capacity > SIZE_MAX - right_sum_capacity) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long multiplication temporary storage is too large", out_error);
        return TINYPY_FALSE;
    }
    cross_capacity = left_sum_capacity + right_sum_capacity;
    if (zero_low_capacity > SIZE_MAX - zero_high_capacity
        || left_sum_capacity > SIZE_MAX - zero_low_capacity - zero_high_capacity
        || right_sum_capacity > SIZE_MAX - zero_low_capacity - zero_high_capacity - left_sum_capacity
        || cross_capacity > SIZE_MAX - zero_low_capacity - zero_high_capacity - left_sum_capacity - right_sum_capacity) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long multiplication temporary storage is too large", out_error);
        return TINYPY_FALSE;
    }
    scratch_capacity = zero_low_capacity + zero_high_capacity + left_sum_capacity + right_sum_capacity + cross_capacity;
    if (scratch_capacity > SIZE_MAX / sizeof(*scratch)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long multiplication temporary storage is too large", out_error);
        return TINYPY_FALSE;
    }
    scratch = (uint16_t *)tinypy_internal_vm_allocate_checked(vm, scratch_capacity * sizeof(*scratch), out_error);
    if (scratch == NULL) {
        return TINYPY_FALSE;
    }
    zero_low = scratch;
    zero_high = zero_low + zero_low_capacity;
    left_sum = zero_high + zero_high_capacity;
    right_sum = left_sum + left_sum_capacity;
    cross = right_sum + right_sum_capacity;
    (void)memset(scratch, 0, scratch_capacity * sizeof(*scratch));
    left_sum_count = __tinypy_operator_long_add_digits(left_sum, left, left_low_count, left + split, left_high_count);
    right_sum_count = __tinypy_operator_long_add_digits(right_sum, right, right_low_count, right + split, right_high_count);

    if (__tinypy_operator_long_multiply_digits(vm, zero_low, zero_low_capacity, left, left_low_count, right, right_low_count, out_error) == 0
        || __tinypy_operator_long_multiply_digits(vm, zero_high, zero_high_capacity, left + split, left_high_count, right + split, right_high_count, out_error) == 0
        || __tinypy_operator_long_multiply_digits(vm, cross, cross_capacity, left_sum, left_sum_count, right_sum, right_sum_count, out_error) == 0) {
        tinypy_internal_vm_deallocate(vm, scratch, scratch_capacity * sizeof(*scratch));
        return TINYPY_FALSE;
    }
    zero_low_count = __tinypy_operator_trim_digits(zero_low, zero_low_capacity);
    zero_high_count = __tinypy_operator_trim_digits(zero_high, zero_high_capacity);
    cross_count = __tinypy_operator_trim_digits(cross, cross_capacity);
    cross_count = __tinypy_operator_long_subtract_digits(cross, cross_count, zero_low, zero_low_count);
    cross_count = __tinypy_operator_long_subtract_digits(cross, cross_count, zero_high, zero_high_count);
    __tinypy_operator_long_add_shifted(output, output_capacity, zero_low, zero_low_count, 0U);
    __tinypy_operator_long_add_shifted(output, output_capacity, cross, cross_count, split);
    __tinypy_operator_long_add_shifted(output, output_capacity, zero_high, zero_high_count, split * 2U);

    tinypy_internal_vm_deallocate(vm, scratch, scratch_capacity * sizeof(*scratch));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_long_multiply_views(tinypy_vm_t *vm, const tinypy_integer_view_t *left, const tinypy_integer_view_t *right, tinypy_error_t **out_error) {
    size_t capacity;
    uint16_t *digits;
    size_t count;

    if (left->sign == 0 || right->sign == 0) {
        tinypy_value_t *return_value_1 = tinypy_internal_long_allocate_digits(vm, 0, 0U, out_error);
        return return_value_1;
    }
    if (left->count > SIZE_MAX - right->count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long int is too large", out_error);
        return NULL;
    }
    capacity = left->count + right->count;
    if (capacity > SIZE_MAX / sizeof(*digits)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long int is too large", out_error);
        return NULL;
    }
    digits = (uint16_t *)tinypy_internal_vm_allocate_checked(vm, capacity * sizeof(*digits), out_error);
    if (digits == NULL) {
        return NULL;
    }
    (void)memset(digits, 0, capacity * sizeof(*digits));
    if (__tinypy_operator_long_multiply_digits(vm, digits, capacity, left->digits, left->count, right->digits, right->count, out_error) == 0) {
        tinypy_internal_vm_deallocate(vm, digits, capacity * sizeof(*digits));
        return NULL;
    }
    count = __tinypy_operator_trim_digits(digits, capacity);
    tinypy_value_t *result = tinypy_internal_long_from_base15_digits_checked(vm, left->sign == right->sign ? 1 : -1, digits, count, out_error);
    tinypy_internal_vm_deallocate(vm, digits, capacity * sizeof(*digits));
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
static int32_t __tinypy_operator_compare_digits(const uint16_t *left, size_t left_count, const uint16_t *right, size_t right_count) {
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
static tinypy_value_t *__tinypy_operator_long_divide_views(tinypy_vm_t *vm, const tinypy_integer_view_t *left, const tinypy_integer_view_t *right, tinypy_operator_long_division_result_e result_kind, tinypy_error_t **out_error) {
    size_t quotient_capacity;
    size_t remainder_capacity;
    size_t division_scratch_capacity;
    uint16_t *division_scratch;
    uint16_t *quotient;
    uint16_t *remainder_digits;
    size_t quotient_count = 0U;
    size_t remainder_count = 0U;
    int32_t quotient_sign;
    int32_t remainder_sign;
    int32_t comparison;

    if (right->sign == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "long division or modulo by zero", out_error);
        return NULL;
    }
    if (left->count == SIZE_MAX || right->count == SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long division temporary storage is too large", out_error);
        return NULL;
    }
    quotient_capacity = left->count + 1U;
    remainder_capacity = right->count + 1U;
    if (quotient_capacity > SIZE_MAX - remainder_capacity
        || quotient_capacity + remainder_capacity > SIZE_MAX / sizeof(*division_scratch)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long division temporary storage is too large", out_error);
        return NULL;
    }
    division_scratch_capacity = quotient_capacity + remainder_capacity;
    division_scratch = (uint16_t *)tinypy_internal_vm_allocate_checked(vm, division_scratch_capacity * sizeof(*division_scratch), out_error);
    if (division_scratch == NULL) {
        return NULL;
    }
    quotient = division_scratch;
    remainder_digits = quotient + quotient_capacity;
    (void)memset(division_scratch, 0, division_scratch_capacity * sizeof(*division_scratch));
    comparison = __tinypy_operator_compare_digits(left->digits, left->count, right->digits, right->count);
    if (left->sign == 0) {
        quotient_count = 0U;
        remainder_count = 0U;
    }
    else if (comparison < 0) {
        (void)memcpy(remainder_digits, left->digits, left->count * sizeof(*remainder_digits));
        remainder_count = left->count;
    }
    else if (right->count == 1U) {
        uint32_t remainder = 0U;
        size_t index;

        for (index = left->count; index != 0U; --index) {
            uint32_t current = remainder * TINYPY_LONG_BASE + (uint32_t)left->digits[index - 1U];

            quotient[index - 1U] = (uint16_t)(current / (uint32_t)right->digits[0]);
            remainder = current % (uint32_t)right->digits[0];
        }
        quotient_count = __tinypy_operator_trim_digits(quotient, left->count);
        if (remainder != 0U) {
            remainder_digits[0] = (uint16_t)remainder;
            remainder_count = 1U;
        }
    }
    else {
        size_t divisor_count = right->count;
        size_t dividend_count = left->count;
        size_t quotient_digits = dividend_count - divisor_count + 1U;
        uint32_t normalizer = TINYPY_LONG_BASE / ((uint32_t)right->digits[divisor_count - 1U] + 1U);
        size_t normalized_dividend_capacity;
        size_t normalized_capacity;
        uint16_t *normalized_storage;
        uint16_t *normalized_divisor;
        uint16_t *normalized_dividend;
        uint32_t carry = 0U;
        size_t index;
        size_t quotient_index;

        if (dividend_count == SIZE_MAX) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long division temporary storage is too large", out_error);
            tinypy_internal_vm_deallocate(vm, division_scratch, division_scratch_capacity * sizeof(*division_scratch));
            return NULL;
        }
        normalized_dividend_capacity = dividend_count + 1U;
        if (divisor_count > SIZE_MAX - normalized_dividend_capacity
            || divisor_count + normalized_dividend_capacity > SIZE_MAX / sizeof(*normalized_storage)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long division temporary storage is too large", out_error);
            tinypy_internal_vm_deallocate(vm, division_scratch, division_scratch_capacity * sizeof(*division_scratch));
            return NULL;
        }
        normalized_capacity = divisor_count + normalized_dividend_capacity;
        normalized_storage = (uint16_t *)tinypy_internal_vm_allocate_checked(vm, normalized_capacity * sizeof(*normalized_storage), out_error);
        if (normalized_storage == NULL) {
            tinypy_internal_vm_deallocate(vm, division_scratch, division_scratch_capacity * sizeof(*division_scratch));
            return NULL;
        }
        normalized_divisor = normalized_storage;
        normalized_dividend = normalized_divisor + divisor_count;
        for (index = 0U; index < divisor_count; ++index) {
            uint32_t product = (uint32_t)right->digits[index] * normalizer + carry;

            normalized_divisor[index] = (uint16_t)(product & TINYPY_LONG_MASK);
            carry = product >> 15U;
        }
        carry = 0U;
        for (index = 0U; index < dividend_count; ++index) {
            uint32_t product = (uint32_t)left->digits[index] * normalizer + carry;

            normalized_dividend[index] = (uint16_t)(product & TINYPY_LONG_MASK);
            carry = product >> 15U;
        }
        normalized_dividend[dividend_count] = (uint16_t)carry;

        for (quotient_index = quotient_digits; quotient_index != 0U; --quotient_index) {
            size_t offset = quotient_index - 1U;
            uint64_t numerator = (uint64_t)normalized_dividend[offset + divisor_count] * (uint64_t)TINYPY_LONG_BASE + (uint64_t)normalized_dividend[offset + divisor_count - 1U];
            uint64_t quotient_estimate = numerator / (uint64_t)normalized_divisor[divisor_count - 1U];
            uint64_t estimate_remainder = numerator % (uint64_t)normalized_divisor[divisor_count - 1U];
            uint32_t product_carry = 0U;
            uint32_t borrow = 0U;
            int32_t top_difference;

            if (quotient_estimate >= (uint64_t)TINYPY_LONG_BASE) {
                quotient_estimate = (uint64_t)TINYPY_LONG_BASE - UINT64_C(1);
                estimate_remainder = numerator - quotient_estimate * (uint64_t)normalized_divisor[divisor_count - 1U];
            }
            while (quotient_estimate * (uint64_t)normalized_divisor[divisor_count - 2U] > estimate_remainder * (uint64_t)TINYPY_LONG_BASE + (uint64_t)normalized_dividend[offset + divisor_count - 2U]) {
                quotient_estimate -= UINT64_C(1);
                estimate_remainder += (uint64_t)normalized_divisor[divisor_count - 1U];
                if (estimate_remainder >= (uint64_t)TINYPY_LONG_BASE) {
                    break;
                }
            }
            for (index = 0U; index < divisor_count; ++index) {
                uint32_t product = (uint32_t)quotient_estimate * (uint32_t)normalized_divisor[index] + product_carry;
                uint32_t subtrahend = (product & TINYPY_LONG_MASK) + borrow;

                product_carry = product >> 15U;
                if ((uint32_t)normalized_dividend[offset + index] < subtrahend) {
                    normalized_dividend[offset + index] = (uint16_t)((uint32_t)normalized_dividend[offset + index] + TINYPY_LONG_BASE - subtrahend);
                    borrow = 1U;
                }
                else {
                    normalized_dividend[offset + index] = (uint16_t)((uint32_t)normalized_dividend[offset + index] - subtrahend);
                    borrow = 0U;
                }
            }
            top_difference = (int32_t)normalized_dividend[offset + divisor_count] - (int32_t)product_carry - (int32_t)borrow;
            if (top_difference < 0) {
                uint32_t add_carry = 0U;

                quotient_estimate -= UINT64_C(1);
                normalized_dividend[offset + divisor_count] = (uint16_t)(top_difference + (int32_t)TINYPY_LONG_BASE);
                for (index = 0U; index < divisor_count; ++index) {
                    uint32_t sum = (uint32_t)normalized_dividend[offset + index] + (uint32_t)normalized_divisor[index] + add_carry;

                    normalized_dividend[offset + index] = (uint16_t)(sum & TINYPY_LONG_MASK);
                    add_carry = sum >> 15U;
                }
                normalized_dividend[offset + divisor_count] = (uint16_t)(((uint32_t)normalized_dividend[offset + divisor_count] + add_carry) & TINYPY_LONG_MASK);
            }
            else {
                normalized_dividend[offset + divisor_count] = (uint16_t)top_difference;
            }
            quotient[offset] = (uint16_t)quotient_estimate;
        }
        quotient_count = __tinypy_operator_trim_digits(quotient, quotient_digits);
        if (normalizer == 1U) {
            (void)memcpy(remainder_digits, normalized_dividend, divisor_count * sizeof(*remainder_digits));
        }
        else {
            uint32_t division_remainder = 0U;

            for (index = divisor_count; index != 0U; --index) {
                uint32_t current = division_remainder * TINYPY_LONG_BASE + (uint32_t)normalized_dividend[index - 1U];

                remainder_digits[index - 1U] = (uint16_t)(current / normalizer);
                division_remainder = current % normalizer;
            }
        }
        remainder_count = __tinypy_operator_trim_digits(remainder_digits, divisor_count);
        tinypy_internal_vm_deallocate(vm, normalized_storage, normalized_capacity * sizeof(*normalized_storage));
    }
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
            remainder_count = __tinypy_operator_trim_digits(remainder_digits, right->count);
        }
        remainder_sign = right->sign;
    }
    tinypy_value_t *result;

    if (result_kind == TINYPY_OPERATOR_LONG_DIVISION_PAIR) {
        tinypy_value_t *items[2];

        items[0] = tinypy_internal_long_from_base15_digits_checked(vm, quotient_sign, quotient, quotient_count, out_error);
        if (items[0] == NULL) {
            result = NULL;
        }
        else {
            items[1] = tinypy_internal_long_from_base15_digits_checked(vm, remainder_sign, remainder_digits, remainder_count, out_error);
            if (items[1] == NULL) {
                result = NULL;
            }
            else {
                result = tinypy_internal_tuple_from_items_checked(vm, items, 2U, out_error);
                TINYPY_DECREF(items[1]);
            }
            TINYPY_DECREF(items[0]);
        }
    }
    else if (result_kind == TINYPY_OPERATOR_LONG_DIVISION_REMAINDER) {
        result = tinypy_internal_long_from_base15_digits_checked(vm, remainder_sign, remainder_digits, remainder_count, out_error);
    }
    else {
        result = tinypy_internal_long_from_base15_digits_checked(vm, quotient_sign, quotient, quotient_count, out_error);
    }
    tinypy_internal_vm_deallocate(vm, division_scratch, division_scratch_capacity * sizeof(*division_scratch));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_add_overflow(int64_t left, int64_t right, int64_t *result) {
    if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right)) {
        return TINYPY_TRUE;
    }
    *result = left + right;
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_subtract_overflow(int64_t left, int64_t right, int64_t *result) {
    if ((right < 0 && left > INT64_MAX + right) || (right > 0 && left < INT64_MIN + right)) {
        return TINYPY_TRUE;
    }
    *result = left - right;
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_multiply_overflow(int64_t left, int64_t right, int64_t *result) {
    if (left == 0 || right == 0) {
        *result = 0;
        return TINYPY_FALSE;
    }
    if ((left == -1 && right == INT64_MIN) || (right == -1 && left == INT64_MIN)) {
        return TINYPY_TRUE;
    }
    if (left > 0) {
        if ((right > 0 && left > INT64_MAX / right) || (right < 0 && right < INT64_MIN / left)) {
            return TINYPY_TRUE;
        }
    }
    else if ((right > 0 && left < INT64_MIN / right) || (right < 0 && left < INT64_MAX / right)) {
        return TINYPY_TRUE;
    }
    *result = left * right;
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_as_double(const tinypy_value_t *value, double *out_value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_FLOAT) {
        *out_value = TINYPY_FLOAT_OBJECT(value)->value;
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = (double)TINYPY_INTEGER_VALUE(value);
        return TINYPY_TRUE;
    }
    tinypy_bool_t return_value_1 = tinypy_internal_long_as_double(value, out_value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_as_complex(const tinypy_value_t *value, double *real, double *imaginary, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_COMPLEX) {
        *real = TINYPY_COMPLEX_OBJECT(value)->real;
        *imaginary = TINYPY_COMPLEX_OBJECT(value)->imaginary;
        return TINYPY_TRUE;
    }
    if (__tinypy_operator_as_double(value, real, out_error) == 0) {
        return TINYPY_FALSE;
    }
    *imaginary = 0.0;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_operator_complex_quotient(double ar, double ai, double br, double bi, double *out_real, double *out_imaginary);
//////////////////////////////////////////////////////////////////////////
static void __tinypy_operator_complex_product(double left_real, double left_imaginary, double right_real, double right_imaginary, double *result_real, double *result_imaginary) {
    *result_real = left_real * right_real - left_imaginary * right_imaginary;
    *result_imaginary = left_real * right_imaginary + left_imaginary * right_real;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_complex_integer_power(double left_real, double left_imaginary, int32_t exponent, double *result_real, double *result_imaginary) {
    uint32_t magnitude = exponent < 0 ? (uint32_t)(-exponent) : (uint32_t)exponent;
    double power_real = left_real;
    double power_imaginary = left_imaginary;
    double accumulated_real = 1.0;
    double accumulated_imaginary = 0.0;

    while (magnitude != 0U) {
        if ((magnitude & 1U) != 0U) {
            double next_real;
            double next_imaginary;

            __tinypy_operator_complex_product(accumulated_real, accumulated_imaginary, power_real, power_imaginary, &next_real, &next_imaginary);
            accumulated_real = next_real;
            accumulated_imaginary = next_imaginary;
        }
        magnitude >>= 1U;
        if (magnitude != 0U) {
            double next_real;
            double next_imaginary;

            __tinypy_operator_complex_product(power_real, power_imaginary, power_real, power_imaginary, &next_real, &next_imaginary);
            power_real = next_real;
            power_imaginary = next_imaginary;
        }
    }
    if (exponent < 0) {
        if (accumulated_real == 0.0 && accumulated_imaginary == 0.0) {
            return TINYPY_FALSE;
        }
        __tinypy_operator_complex_quotient(1.0, 0.0, accumulated_real, accumulated_imaginary, result_real, result_imaginary);
    }
    else {
        *result_real = accumulated_real;
        *result_imaginary = accumulated_imaginary;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_operator_complex_power_result_e __tinypy_operator_complex_power(double left_real, double left_imaginary, double right_real, double right_imaginary, double *result_real, double *result_imaginary) {
    if (right_imaginary == 0.0 && right_real >= -100.0 && right_real <= 100.0 && right_real == trunc(right_real)) {
        if (__tinypy_operator_complex_integer_power(left_real, left_imaginary, (int32_t)right_real, result_real, result_imaginary) == 0) {
            return TINYPY_OPERATOR_COMPLEX_POWER_ZERO_DIVISION;
        }
        if (isfinite(left_real) != 0 && isfinite(left_imaginary) != 0 && (isinf(*result_real) != 0 || isinf(*result_imaginary) != 0)) {
            return TINYPY_OPERATOR_COMPLEX_POWER_OVERFLOW;
        }
        return TINYPY_OPERATOR_COMPLEX_POWER_OK;
    }
    errno = 0;
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
    if ((isfinite(left_real) != 0 && isfinite(left_imaginary) != 0 && isfinite(right_real) != 0 && isfinite(right_imaginary) != 0 && (isinf(*result_real) != 0 || isinf(*result_imaginary) != 0)) || (right_imaginary == 0.0 && right_real > 0.0 && isinf(radius) != 0 && isnan(left_real) == 0 && isnan(left_imaginary) == 0)) {
        return TINYPY_OPERATOR_COMPLEX_POWER_OVERFLOW;
    }
    return TINYPY_OPERATOR_COMPLEX_POWER_OK;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_numeric_add(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, int32_t subtract, tinypy_error_t **out_error) {
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported numeric operands", out_error);
        return NULL;
    }
    if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
        double left_real, left_imaginary, right_real, right_imaginary;
        if (__tinypy_operator_as_complex(left, &left_real, &left_imaginary, out_error) == 0 || __tinypy_operator_as_complex(right, &right_real, &right_imaginary, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *return_value_1 = tinypy_complex_from_doubles(vm, left_real + (subtract != 0 ? -right_real : right_real), left_imaginary + (subtract != 0 ? -right_imaginary : right_imaginary));
        return return_value_1;
    }
    if (left_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_FLOAT) {
        double operator_as_double;
        double right_value;

        if (__tinypy_operator_as_double(left, &operator_as_double, out_error) == 0 || __tinypy_operator_as_double(right, &right_value, out_error) == 0) {
            return NULL;
        }
        double result = subtract != 0 ? operator_as_double - right_value : operator_as_double + right_value;
        tinypy_value_t *return_value_2 = tinypy_float_from_double(vm, result);
        return return_value_2;
    }
    if (__tinypy_operator_is_integer(left_kind) != 0 && __tinypy_operator_is_integer(right_kind) != 0) {
        if (left_kind != TINYPY_VALUE_LONG && right_kind != TINYPY_VALUE_LONG) {
            int64_t value;
            int32_t overflow = subtract != 0 ? __tinypy_operator_subtract_overflow(TINYPY_INTEGER_VALUE(left), TINYPY_INTEGER_VALUE(right), &value) : __tinypy_operator_add_overflow(TINYPY_INTEGER_VALUE(left), TINYPY_INTEGER_VALUE(right), &value);
            if (overflow == 0) {
                tinypy_value_t *return_value_3 = tinypy_integer_from_i64(vm, value);
                return return_value_3;
            }
        }
        tinypy_integer_view_t left_view;
        tinypy_integer_view_t right_view;
        __tinypy_operator_integer_view(left, &left_view);
        __tinypy_operator_integer_view(right, &right_view);
        tinypy_value_t *return_value_4 = __tinypy_operator_long_add_views(vm, &left_view, &right_view, subtract, out_error);
        return return_value_4;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported numeric operands", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_concat_text(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, tinypy_bool_t unicode, tinypy_error_t **out_error) {
    const void *left_bytes;
    const void *right_bytes;
    size_t left_size;
    size_t right_size;
    uint8_t *output;

    if (unicode != 0) {
        if (tinypy_internal_text_ascii_compatible(vm, left, out_error) == 0 || tinypy_internal_text_ascii_compatible(vm, right, out_error) == 0) {
            return NULL;
        }
        left_bytes = TINYPY_TEXT_BYTES(left);
        left_size = TINYPY_TEXT_BYTE_SIZE(left);
        right_bytes = TINYPY_TEXT_BYTES(right);
        right_size = TINYPY_TEXT_BYTE_SIZE(right);
    }
    else {
        left_bytes = tinypy_string_view(left, &left_size);
        right_bytes = tinypy_string_view(right, &right_size);
    }
    if (left_size > SIZE_MAX - right_size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "concatenated string is too large", out_error);
        return NULL;
    }
    if (left_size + right_size == 0U) {
        tinypy_value_t *return_value_1 = unicode != 0 ? tinypy_unicode_from_utf8(vm, "", 0U) : tinypy_string_from_bytes(vm, NULL, 0U);
        return return_value_1;
    }
    size_t character_count = unicode != 0 ? TINYPY_SIZED_SIZE(left) + TINYPY_SIZED_SIZE(right) : left_size + right_size;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(
        vm,
        unicode != 0 ? TINYPY_VALUE_UNICODE : TINYPY_VALUE_STRING,
        left_size + right_size,
        character_count,
        &output,
        out_error);
    if (result == NULL) {
        return NULL;
    }
    if (left_size != 0U) {
        (void)memcpy(output, left_bytes, left_size);
    }
    if (right_size != 0U) {
        (void)memcpy(output + left_size, right_bytes, right_size);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_digits_as_i64(int32_t sign, const uint16_t *digits, size_t count, int64_t *out_value) {
    uint64_t magnitude = 0U;
    size_t index;

    if (count > 5U) {
        return TINYPY_FALSE;
    }
    for (index = count; index != 0U; index -= 1U) {
        if (magnitude > (UINT64_MAX >> 15U)) {
            return TINYPY_FALSE;
        }
        magnitude = (magnitude << 15U) | digits[index - 1U];
    }
    if (sign >= 0) {
        if (magnitude > (uint64_t)INT64_MAX) {
            return TINYPY_FALSE;
        }
        *out_value = (int64_t)magnitude;
    }
    else {
        uint64_t limit = (uint64_t)INT64_MAX + UINT64_C(1);

        if (magnitude > limit) {
            return TINYPY_FALSE;
        }
        *out_value = magnitude == limit ? INT64_MIN : -(int64_t)magnitude;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_integer_from_digits(tinypy_vm_t *vm, int32_t sign, uint16_t *digits, size_t count, tinypy_bool_t prefer_long, tinypy_error_t **out_error) {
    int64_t integer;

    count = __tinypy_operator_trim_digits(digits, count);
    if (count == 0U) {
        sign = 0;
    }
    if (prefer_long == 0 && __tinypy_operator_digits_as_i64(sign, digits, count, &integer) != 0) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, integer);
        return return_value_1;
    }
    tinypy_value_t *return_value_2 = tinypy_internal_long_from_base15_digits_checked(vm, sign, digits, count, out_error);
    return return_value_2;
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
static tinypy_value_t *__tinypy_operator_integer_bitwise(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error) {
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);
    tinypy_integer_view_t left_view;
    tinypy_integer_view_t right_view;
    size_t width;
    uint16_t *left_digits;
    uint16_t *right_digits;
    uint16_t *scratch;
    size_t index;
    int32_t sign;
    tinypy_bool_t prefer_long;

    if (__tinypy_operator_is_integer(left_kind) == 0 || __tinypy_operator_is_integer(right_kind) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bitwise operands must be integers", out_error);
        return NULL;
    }
    if (left_kind != TINYPY_VALUE_LONG && right_kind != TINYPY_VALUE_LONG) {
        int64_t left_integer = TINYPY_INTEGER_VALUE(left);
        int64_t right_integer = TINYPY_INTEGER_VALUE(right);
        int64_t integer = operation == 0 ? left_integer & right_integer : (operation == 1 ? left_integer ^ right_integer : left_integer | right_integer);

        if (left_kind == TINYPY_VALUE_BOOL && right_kind == TINYPY_VALUE_BOOL) {
            tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, integer != 0);
            return return_value_1;
        }
        tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, integer);
        return return_value_2;
    }
    __tinypy_operator_integer_view(left, &left_view);
    __tinypy_operator_integer_view(right, &right_view);
    width = left_view.count > right_view.count ? left_view.count : right_view.count;
    if (width == SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "bitwise temporary storage is too large", out_error);
        return NULL;
    }
    width += 1U;
    if (width > SIZE_MAX / (2U * sizeof(*scratch))) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "bitwise temporary storage is too large", out_error);
        return NULL;
    }
    scratch = (uint16_t *)tinypy_internal_vm_allocate_checked(vm, width * 2U * sizeof(*scratch), out_error);
    if (scratch == NULL) {
        return NULL;
    }
    left_digits = scratch;
    right_digits = left_digits + width;
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
    tinypy_value_t *result = __tinypy_operator_integer_from_digits(vm, sign, left_digits, width, prefer_long, out_error);
    tinypy_internal_vm_deallocate(vm, scratch, width * 2U * sizeof(*scratch));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_shift_count(tinypy_vm_t *vm, tinypy_value_t *value, size_t *out_shift, tinypy_error_t **out_error) {
    tinypy_integer_view_t view;
    size_t shift = 0U;
    size_t limit;
    size_t index;

    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    if (__tinypy_operator_is_integer(kind) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "shift count must be an integer", out_error);
        return TINYPY_FALSE;
    }
    __tinypy_operator_integer_view(value, &view);
    limit = view.sign < 0 ? (size_t)PTRDIFF_MAX + 1U : (size_t)PTRDIFF_MAX;
    for (index = view.count; index != 0U; index -= 1U) {
        if (shift > (limit >> 15U)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "shift count is too large", out_error);
            return TINYPY_FALSE;
        }
        shift = (shift << 15U) | view.digits[index - 1U];
        if (shift > limit) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "shift count is too large", out_error);
            return TINYPY_FALSE;
        }
    }
    if (view.sign < 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "negative shift count", out_error);
        return TINYPY_FALSE;
    }
    *out_shift = shift;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_integer_left_shift(tinypy_vm_t *vm, tinypy_value_t *left, size_t shift, tinypy_bool_t prefer_long, tinypy_error_t **out_error) {
    tinypy_integer_view_t view;
    size_t digit_shift = shift / 15U;
    size_t bit_shift = shift % 15U;
    size_t capacity;
    uint16_t local_digits[5];
    uint16_t *digits;
    tinypy_value_t *result = NULL;
    uint32_t carry = 0U;
    size_t index;

    __tinypy_operator_integer_view(left, &view);
    if (view.sign == 0) {
        tinypy_value_t *return_value_1 = prefer_long != 0 ? tinypy_long_from_base15_digits(vm, 0, NULL, 0U) : tinypy_integer_from_i64(vm, 0);
        return return_value_1;
    }
    if (view.count >= (size_t)PTRDIFF_MAX || digit_shift > (size_t)PTRDIFF_MAX - view.count - 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "left shift result is too large", out_error);
        return NULL;
    }
    carry = bit_shift != 0U ? (uint32_t)view.digits[view.count - 1U] >> (15U - bit_shift) : 0U;
    capacity = view.count + digit_shift + (carry != 0U ? 1U : 0U);
    if (capacity <= sizeof(local_digits) / sizeof(local_digits[0])) {
        digits = local_digits;
    }
    else {
        result = tinypy_internal_long_allocate_digits(vm, view.sign, capacity, out_error);
        if (result == NULL) {
            return NULL;
        }
        digits = TINYPY_LONG_OBJECT(result)->digits;
    }
    (void)memset(digits, 0, capacity * sizeof(*digits));
    carry = 0U;
    for (index = 0U; index < view.count; ++index) {
        uint32_t shifted = ((uint32_t)view.digits[index] << bit_shift) | carry;

        digits[index + digit_shift] = (uint16_t)(shifted & TINYPY_LONG_MASK);
        carry = shifted >> 15U;
    }
    if (carry != 0U) {
        digits[view.count + digit_shift] = (uint16_t)carry;
    }
    if (result == NULL) {
        result = __tinypy_operator_integer_from_digits(vm, view.sign, digits, capacity, prefer_long, out_error);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_integer_right_shift(tinypy_vm_t *vm, tinypy_value_t *left, size_t shift, tinypy_bool_t prefer_long, tinypy_error_t **out_error) {
    tinypy_integer_view_t view;
    size_t digit_shift = shift / 15U;
    size_t bit_shift = shift % 15U;
    size_t capacity;
    uint16_t *digits;
    int32_t discarded = 0;
    size_t index;
    __tinypy_operator_integer_view(left, &view);
    if (view.sign == 0) {
        tinypy_value_t *return_value_1 = prefer_long != 0 ? tinypy_long_from_base15_digits(vm, 0, NULL, 0U) : tinypy_integer_from_i64(vm, 0);
        return return_value_1;
    }
    if (digit_shift >= view.count) {
        tinypy_value_t *return_value_2 = prefer_long != 0 ? tinypy_long_from_i64(vm, view.sign < 0 ? -1 : 0) : tinypy_integer_from_i64(vm, view.sign < 0 ? -1 : 0);
        return return_value_2;
    }
    if (view.count - digit_shift == SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "right shift temporary storage is too large", out_error);
        return NULL;
    }
    capacity = view.count - digit_shift + 1U;
    if (capacity > SIZE_MAX / sizeof(*digits)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "right shift temporary storage is too large", out_error);
        return NULL;
    }
    digits = (uint16_t *)tinypy_internal_vm_allocate_checked(vm, capacity * sizeof(*digits), out_error);
    if (digits == NULL) {
        return NULL;
    }
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
    }
    tinypy_value_t *result = __tinypy_operator_integer_from_digits(vm, view.sign, digits, capacity, prefer_long, out_error);
    tinypy_internal_vm_deallocate(vm, digits, capacity * sizeof(*digits));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_integer_exponent(tinypy_vm_t *vm, tinypy_value_t *value, size_t *out_exponent, tinypy_error_t **out_error) {
    tinypy_integer_view_t view;
    size_t exponent = 0U;
    size_t index;

    __tinypy_operator_integer_view(value, &view);
    for (index = view.count; index != 0U; index -= 1U) {
        if (exponent > (SIZE_MAX >> 15U)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "exponent is too large", out_error);
            return TINYPY_FALSE;
        }
        exponent = (exponent << 15U) | view.digits[index - 1U];
    }
    *out_exponent = exponent;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_call_unary_special(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_value_t *method = tinypy_internal_object_get_special(value, name, name_size, out_error);

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

    TINYPY_CLEAR_ERROR(out_error);
    if (tinypy_internal_object_has_special_override(value, "__pos__", 7U) != 0) {
        tinypy_value_t *return_value_1 = __tinypy_operator_call_unary_special(value, "__pos__", 7U, out_error);
        return return_value_1;
    }
    kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_BOOL) {
        tinypy_value_t *return_value_2 = tinypy_integer_from_i64(TINYPY_VALUE_VM(value), TINYPY_INTEGER_VALUE(value));
        return return_value_2;
    }
    if (__tinypy_operator_is_integer(kind) != 0 || kind == TINYPY_VALUE_FLOAT || kind == TINYPY_VALUE_COMPLEX) {
        TINYPY_INCREF(value);
        return value;
    }
    if (value->type->number_slots != NULL && value->type->number_slots->positive != NULL) {
        tinypy_value_t *return_value_3 = value->type->number_slots->positive(value, out_error);
        return return_value_3;
    }
    if (tinypy_internal_object_has_special(value, "__pos__", 7U) != 0) {
        tinypy_value_t *return_value_4 = __tinypy_operator_call_unary_special(value, "__pos__", 7U, out_error);
        return return_value_4;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bad operand for unary plus", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_negative(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    TINYPY_CLEAR_ERROR(out_error);
    if (tinypy_internal_object_has_special_override(value, "__neg__", 7U) != 0) {
        tinypy_value_t *return_value_1 = __tinypy_operator_call_unary_special(value, "__neg__", 7U, out_error);
        return return_value_1;
    }
    kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        int64_t integer = TINYPY_INTEGER_VALUE(value);
        if (integer != INT64_MIN) {
            tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, -integer);
            return return_value_2;
        }
    }
    if (__tinypy_operator_is_integer(kind) != 0) {
        tinypy_integer_view_t view;
        __tinypy_operator_integer_view(value, &view);
        tinypy_value_t *return_value_3 = tinypy_long_from_base15_digits(vm, -view.sign, view.digits, view.count);
        return return_value_3;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        tinypy_value_t *return_value_4 = tinypy_float_from_double(vm, -TINYPY_FLOAT_OBJECT(value)->value);
        return return_value_4;
    }
    if (kind == TINYPY_VALUE_COMPLEX) {
        tinypy_value_t *return_value_5 = tinypy_complex_from_doubles(vm, -TINYPY_COMPLEX_OBJECT(value)->real, -TINYPY_COMPLEX_OBJECT(value)->imaginary);
        return return_value_5;
    }
    if (value->type->number_slots != NULL && value->type->number_slots->negative != NULL) {
        tinypy_value_t *return_value_6 = value->type->number_slots->negative(value, out_error);
        return return_value_6;
    }
    if (tinypy_internal_object_has_special(value, "__neg__", 7U) != 0) {
        tinypy_value_t *return_value_7 = __tinypy_operator_call_unary_special(value, "__neg__", 7U, out_error);
        return return_value_7;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bad operand for unary minus", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_invert(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    TINYPY_CLEAR_ERROR(out_error);
    if (tinypy_internal_object_has_special_override(value, "__invert__", 10U) != 0) {
        tinypy_value_t *return_value_1 = __tinypy_operator_call_unary_special(value, "__invert__", 10U, out_error);
        return return_value_1;
    }
    kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, ~TINYPY_INTEGER_VALUE(value));
        return return_value_2;
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
        tinypy_value_t *return_value_3 = value->type->number_slots->invert(value, out_error);
        return return_value_3;
    }
    if (tinypy_internal_object_has_special(value, "__invert__", 10U) != 0) {
        tinypy_value_t *return_value_4 = __tinypy_operator_call_unary_special(value, "__invert__", 10U, out_error);
        return return_value_4;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bad operand for unary invert", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_call_special(tinypy_value_t *receiver, const char *name, size_t name_size, tinypy_value_t *argument, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(receiver);
    tinypy_value_t *method = tinypy_internal_object_get_special(receiver, name, name_size, out_error);

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
static tinypy_value_t *__tinypy_operator_special_binary(tinypy_value_t *left, tinypy_value_t *right, const char *name, size_t name_size, const char *reverse_name, size_t reverse_name_size, tinypy_bool_t *out_handled, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_bool_t reverse_first = right->type != left->type && tinypy_type_is_subtype(right->type, left->type) != 0 && tinypy_internal_object_has_special_override(right, reverse_name, reverse_name_size) != 0;

    *out_handled = INT32_C(0);
    if (reverse_first != 0) {
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
    if (tinypy_internal_object_has_special_override(left, name, name_size) != 0) {
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
    if (reverse_first == 0 && tinypy_internal_object_has_special_override(right, reverse_name, reverse_name_size) != 0) {
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
static tinypy_value_t *__tinypy_operator_regular_binary(tinypy_value_t *left, tinypy_value_t *right, const char *name, size_t name_size, const char *reverse_name, size_t reverse_name_size, tinypy_bool_t *out_handled, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    *out_handled = INT32_C(0);
    if (tinypy_internal_object_has_special(left, name, name_size) != 0 && tinypy_internal_object_has_special_override(left, name, name_size) == 0) {
        tinypy_value_t *result = __tinypy_operator_call_special(left, name, name_size, right, out_error);

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
    if (tinypy_internal_object_has_special(right, reverse_name, reverse_name_size) != 0 && tinypy_internal_object_has_special_override(right, reverse_name, reverse_name_size) == 0) {
        tinypy_value_t *result = __tinypy_operator_call_special(right, reverse_name, reverse_name_size, left, out_error);

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
static tinypy_value_t *__tinypy_operator_native_binary(tinypy_value_t *left, tinypy_value_t *right, tinypy_operator_native_binary_e operation, tinypy_bool_t *out_handled, tinypy_error_t **out_error) {
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
static tinypy_value_t *__tinypy_operator_concat_sequence(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    size_t left_size = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(left) : TINYPY_LIST_SIZE(left);
    size_t right_size = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(right) : TINYPY_LIST_SIZE(right);
    size_t total_size;
    size_t index;
    tinypy_value_t *result;

    if (right_size > SIZE_MAX - left_size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "concatenated sequence is too large", out_error);
        return NULL;
    }
    total_size = left_size + right_size;
    if (total_size >= (size_t)PTRDIFF_MAX || total_size > SIZE_MAX / sizeof(tinypy_value_t *)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "concatenated sequence is too large", out_error);
        return NULL;
    }
    if (kind == TINYPY_VALUE_TUPLE) {
        result = tinypy_internal_tuple_new_checked(vm, total_size, out_error);
    }
    else {
        result = tinypy_list_from_items(vm, NULL, 0U);
        if (tinypy_internal_list_reserve_checked(vm, result, total_size, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    if (result == NULL) {
        return NULL;
    }
    for (index = 0U; index < left_size; ++index) {
        tinypy_value_t *item = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(left, index) : TINYPY_LIST_GET(left, index);

        if (kind == TINYPY_VALUE_TUPLE) {
            tinypy_tuple_set(result, index, item);
        }
        else if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    for (index = 0U; index < right_size; ++index) {
        tinypy_value_t *item = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(right, index) : TINYPY_LIST_GET(right, index);

        if (kind == TINYPY_VALUE_TUPLE) {
            tinypy_tuple_set(result, left_size + index, item);
        }
        else if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_operator_repeat_count(tinypy_value_t *value, size_t *out_count, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    int64_t count;

    if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG) {
        tinypy_value_t *method;
        tinypy_value_t *args;
        tinypy_value_t *converted;
        int32_t result;

        if (tinypy_internal_object_has_special(value, "__index__", 9U) == 0) {
            return 0;
        }
        method = tinypy_internal_object_get_special(value, "__index__", 9U, out_error);
        if (method == NULL) {
            return -1;
        }
        args = tinypy_tuple_from_items(vm, NULL, 0U);
        converted = tinypy_call(method, args, NULL, out_error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(method);
        if (converted == NULL) {
            return -1;
        }
        kind = TINYPY_VALUE_KIND(converted);
        if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG) {
            TINYPY_DECREF(converted);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__index__ returned a non-integer", out_error);
            return -1;
        }
        result = __tinypy_operator_repeat_count(converted, out_count, out_error);
        TINYPY_DECREF(converted);
        return result;
    }
    if (tinypy_internal_index_as_i64(value, &count, TINYPY_FALSE, out_error) == 0) {
        return -1;
    }
    *out_count = count <= 0 ? 0U : (size_t)count;
    return count <= 0 || (uint64_t)count <= (uint64_t)SIZE_MAX ? 1 : -1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_repeat(tinypy_vm_t *vm, tinypy_value_t *sequence, size_t count, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(sequence);
    size_t unit_size;
    size_t total_size;
    size_t index;

    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        const uint8_t *bytes = TINYPY_TEXT_BYTES(sequence);
        unit_size = TINYPY_TEXT_BYTE_SIZE(sequence);
        if (unit_size != 0U && count > SIZE_MAX / unit_size) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated string is too large", out_error);
            return NULL;
        }
        total_size = unit_size * count;
        if (total_size >= (size_t)PTRDIFF_MAX) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated string is too large", out_error);
            return NULL;
        }
        if (total_size == 0U) {
            tinypy_value_t *return_value_1 = kind == TINYPY_VALUE_STRING ? tinypy_string_from_bytes(vm, NULL, 0U) : tinypy_unicode_from_utf8(vm, NULL, 0U);
            return return_value_1;
        }
        size_t code_point_count = kind == TINYPY_VALUE_STRING ? total_size : TINYPY_SIZED_SIZE(sequence) * count;
        uint8_t *buffer;
        tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, kind, total_size, code_point_count, &buffer, out_error);

        if (result == NULL) {
            return NULL;
        }

        (void)memcpy(buffer, bytes, unit_size);
        size_t copied = unit_size;
        while (copied < total_size) {
            size_t chunk = copied < total_size - copied ? copied : total_size - copied;

            (void)memcpy(buffer + copied, buffer, chunk);
            copied += chunk;
        }
        return result;
    }
    unit_size = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(sequence) : TINYPY_LIST_SIZE(sequence);
    if (unit_size != 0U && count > SIZE_MAX / unit_size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated sequence is too large", out_error);
        return NULL;
    }
    total_size = unit_size * count; {
        if (total_size >= (size_t)PTRDIFF_MAX || total_size > SIZE_MAX / sizeof(tinypy_value_t *)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated sequence is too large", out_error);
            return NULL;
        }
        tinypy_value_t *result = kind == TINYPY_VALUE_TUPLE ? tinypy_internal_tuple_new_checked(vm, total_size, out_error) : tinypy_list_from_items(vm, NULL, 0U);

        if (result == NULL) {
            return NULL;
        }

        if (kind == TINYPY_VALUE_LIST) {
            if (tinypy_internal_list_reserve_checked(vm, result, total_size, out_error) == 0) {
                TINYPY_DECREF(result);
                return NULL;
            }
        }
        for (index = 0U; index < total_size; ++index) {
            tinypy_value_t *item = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(sequence, index % unit_size) : TINYPY_LIST_GET(sequence, index % unit_size);

            if (kind == TINYPY_VALUE_TUPLE) {
                tinypy_tuple_set(result, index, item);
            }
            else if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
                TINYPY_DECREF(result);
                return NULL;
            }
        }
        return result;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_add(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_type_e left_kind;
    tinypy_value_type_e right_kind;
    tinypy_bool_t handled;
    tinypy_value_t *native_result;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__add__", 7U, "__radd__", 8U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    native_result = __tinypy_operator_native_binary(left, right, TINYPY_OPERATOR_NATIVE_ADD, &handled, out_error);
    if (handled != 0) {
        return native_result;
    }
    left_kind = TINYPY_VALUE_KIND(left);
    right_kind = TINYPY_VALUE_KIND(right);
    if ((left_kind == TINYPY_VALUE_STRING || left_kind == TINYPY_VALUE_UNICODE) && (right_kind == TINYPY_VALUE_STRING || right_kind == TINYPY_VALUE_UNICODE)) {
        tinypy_bool_t unicode = left_kind == TINYPY_VALUE_UNICODE || right_kind == TINYPY_VALUE_UNICODE;

        tinypy_value_t *return_value_1 = __tinypy_operator_concat_text(vm, left, right, unicode, out_error);
        return return_value_1;
    }
    if ((left_kind == TINYPY_VALUE_TUPLE || left_kind == TINYPY_VALUE_LIST) && left_kind == right_kind) {
        tinypy_value_t *return_value_2 = __tinypy_operator_concat_sequence(vm, left, right, out_error);
        return return_value_2;
    }
    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        special = __tinypy_operator_regular_binary(left, right, "__add__", 7U, "__radd__", 8U, &handled, out_error);
        if (handled != 0) {
            return special;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported add operands", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_3 = __tinypy_operator_numeric_add(vm, left, right, 0, out_error);
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_subtract(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_bool_t handled;
    tinypy_value_t *native_result;

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__sub__", 7U, "__rsub__", 8U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    native_result = __tinypy_operator_native_binary(left, right, TINYPY_OPERATOR_NATIVE_SUBTRACT, &handled, out_error);
    if (handled != 0) {
        return native_result;
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_SET || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_FROZENSET) {
        tinypy_value_t *return_value_1 = tinypy_internal_set_binary(left, right, INT32_C(3), out_error);
        return return_value_1;
    }
    if (__tinypy_operator_is_number(TINYPY_VALUE_KIND(left)) == 0 || __tinypy_operator_is_number(TINYPY_VALUE_KIND(right)) == 0) {
        special = __tinypy_operator_regular_binary(left, right, "__sub__", 7U, "__rsub__", 8U, &handled, out_error);
        if (handled != 0) {
            return special;
        }
    }
    tinypy_value_t *return_value_2 = __tinypy_operator_numeric_add(vm, left, right, 1, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_multiply(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_type_e left_kind;
    tinypy_value_type_e right_kind;
    size_t repeat_count;
    tinypy_bool_t handled;
    tinypy_value_t *native_result;

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__mul__", 7U, "__rmul__", 8U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    native_result = __tinypy_operator_native_binary(left, right, TINYPY_OPERATOR_NATIVE_MULTIPLY, &handled, out_error);
    if (handled != 0) {
        return native_result;
    }
    left_kind = TINYPY_VALUE_KIND(left);
    right_kind = TINYPY_VALUE_KIND(right);
    if (left_kind == TINYPY_VALUE_STRING || left_kind == TINYPY_VALUE_UNICODE || left_kind == TINYPY_VALUE_TUPLE || left_kind == TINYPY_VALUE_LIST) {
        int32_t repeat = __tinypy_operator_repeat_count(right, &repeat_count, out_error);

        if (repeat < 0) {
            return NULL;
        }
        if (repeat != 0) {
            tinypy_value_t *return_value_1 = __tinypy_operator_repeat(vm, left, repeat_count, out_error);
            return return_value_1;
        }
    }
    if ((right_kind == TINYPY_VALUE_STRING || right_kind == TINYPY_VALUE_UNICODE || right_kind == TINYPY_VALUE_TUPLE || right_kind == TINYPY_VALUE_LIST)) {
        int32_t repeat = __tinypy_operator_repeat_count(left, &repeat_count, out_error);

        if (repeat < 0) {
            return NULL;
        }
        if (repeat != 0) {
            tinypy_value_t *return_value_2 = __tinypy_operator_repeat(vm, right, repeat_count, out_error);
            return return_value_2;
        }
    }
    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        special = __tinypy_operator_regular_binary(left, right, "__mul__", 7U, "__rmul__", 8U, &handled, out_error);
        if (handled != 0) {
            return special;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported multiply operands", out_error);
        return NULL;
    }
    if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
        double ar, ai, br, bi;
        if (__tinypy_operator_as_complex(left, &ar, &ai, out_error) == 0 || __tinypy_operator_as_complex(right, &br, &bi, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *return_value_3 = tinypy_complex_from_doubles(vm, ar * br - ai * bi, ar * bi + ai * br);
        return return_value_3;
    }
    if (left_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_FLOAT) {
        double operator_as_double;
        double operator_as_double_2;

        if (__tinypy_operator_as_double(left, &operator_as_double, out_error) == 0 || __tinypy_operator_as_double(right, &operator_as_double_2, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *return_value_4 = tinypy_float_from_double(vm, operator_as_double * operator_as_double_2);
        return return_value_4;
    }
    if (__tinypy_operator_is_integer(left_kind) != 0 && __tinypy_operator_is_integer(right_kind) != 0) {
        if (left_kind != TINYPY_VALUE_LONG && right_kind != TINYPY_VALUE_LONG) {
            int64_t value;
            if (__tinypy_operator_multiply_overflow(TINYPY_INTEGER_VALUE(left), TINYPY_INTEGER_VALUE(right), &value) == 0) {
                tinypy_value_t *return_value_5 = tinypy_integer_from_i64(vm, value);
                return return_value_5;
            }
        }
        tinypy_integer_view_t left_view;
        tinypy_integer_view_t right_view;
        __tinypy_operator_integer_view(left, &left_view);
        __tinypy_operator_integer_view(right, &right_view);
        tinypy_value_t *return_value_6 = __tinypy_operator_long_multiply_views(vm, &left_view, &right_view, out_error);
        return return_value_6;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported multiply operands", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_operator_float_divmod(double dividend, double divisor, double *out_floor, double *out_remainder) {
    double remainder = fmod(dividend, divisor);
    double quotient = (dividend - remainder) / divisor;

    if (remainder != 0.0) {
        if ((divisor < 0.0) != (remainder < 0.0)) {
            remainder += divisor;
            quotient -= 1.0;
        }
    }
    else {
        remainder = copysign(0.0, divisor);
    }
    if (quotient != 0.0) {
        double floor_quotient = floor(quotient);

        if (quotient - floor_quotient > 0.5) {
            floor_quotient += 1.0;
        }
        quotient = floor_quotient;
    }
    else {
        quotient = copysign(0.0, dividend / divisor);
    }
    *out_floor = quotient;
    *out_remainder = remainder;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_operator_complex_quotient(double ar, double ai, double br, double bi, double *out_real, double *out_imaginary) {
    if (fabs(br) >= fabs(bi)) {
        double ratio = bi / br;
        double denominator = br + bi * ratio;

        *out_real = (ar + ai * ratio) / denominator;
        *out_imaginary = (ai - ar * ratio) / denominator;
    }
    else {
        double ratio = br / bi;
        double denominator = br * ratio + bi;

        *out_real = (ar * ratio + ai) / denominator;
        *out_imaginary = (ai * ratio - ar) / denominator;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_operator_division_e division, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported division operands", out_error);
        return NULL;
    }

    if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
        double ar, ai, br, bi;
        double quotient_real;
        double quotient_imaginary;

        if (__tinypy_operator_as_complex(left, &ar, &ai, out_error) == 0 || __tinypy_operator_as_complex(right, &br, &bi, out_error) == 0) {
            return NULL;
        }
        if (br == 0.0 && bi == 0.0) {
            const char *message = division == TINYPY_OPERATOR_DIVISION_FLOOR ? "complex divmod()" : (division == TINYPY_OPERATOR_DIVISION_REMAINDER ? "complex remainder" : "complex division by zero");

            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, message, out_error);
            return NULL;
        }
        __tinypy_operator_complex_quotient(ar, ai, br, bi, &quotient_real, &quotient_imaginary);
        if (division == TINYPY_OPERATOR_DIVISION_FLOOR || division == TINYPY_OPERATOR_DIVISION_REMAINDER) {
            double floor_real = floor(quotient_real);

            if (division == TINYPY_OPERATOR_DIVISION_REMAINDER) {
                double product_real;
                double product_imaginary;

                __tinypy_operator_complex_product(br, bi, floor_real, 0.0, &product_real, &product_imaginary);
                tinypy_value_t *return_value_1 = tinypy_complex_from_doubles(vm, ar - product_real, ai - product_imaginary);
                return return_value_1;
            }
            tinypy_value_t *return_value_2 = tinypy_complex_from_doubles(vm, floor_real, 0.0);
            return return_value_2;
        }
        tinypy_value_t *return_value_3 = tinypy_complex_from_doubles(vm, quotient_real, quotient_imaginary);
        return return_value_3;
    }
    if (left_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_FLOAT || division == TINYPY_OPERATOR_DIVISION_TRUE) {
        double divisor;
        double dividend;

        if (__tinypy_operator_as_double(right, &divisor, out_error) == 0 || __tinypy_operator_as_double(left, &dividend, out_error) == 0) {
            return NULL;
        }

        if (divisor == 0.0) {
            const char *message;

            if (division == TINYPY_OPERATOR_DIVISION_FLOOR) {
                message = "float divmod()";
            }
            else if (division == TINYPY_OPERATOR_DIVISION_REMAINDER) {
                message = "float modulo";
            }
            else {
                message = left_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_FLOAT ? "float division by zero" : "division by zero";
            }
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, message, out_error);
            return NULL;
        }
        if (division == TINYPY_OPERATOR_DIVISION_FLOOR || division == TINYPY_OPERATOR_DIVISION_REMAINDER) {
            double floor_quotient;
            double remainder;

            __tinypy_operator_float_divmod(dividend, divisor, &floor_quotient, &remainder);
            tinypy_value_t *return_value_4 = tinypy_float_from_double(vm, division == TINYPY_OPERATOR_DIVISION_REMAINDER ? remainder : floor_quotient);
            return return_value_4;
        }
        tinypy_value_t *return_value_5 = tinypy_float_from_double(vm, dividend / divisor);
        return return_value_5;
    }
    if (__tinypy_operator_is_integer(left_kind) != 0 && __tinypy_operator_is_integer(right_kind) != 0) {
        if (left_kind == TINYPY_VALUE_LONG || right_kind == TINYPY_VALUE_LONG) {
            tinypy_integer_view_t left_view;
            tinypy_integer_view_t right_view;

            __tinypy_operator_integer_view(left, &left_view);
            __tinypy_operator_integer_view(right, &right_view);
            tinypy_operator_long_division_result_e result_kind = division == TINYPY_OPERATOR_DIVISION_REMAINDER ? TINYPY_OPERATOR_LONG_DIVISION_REMAINDER : TINYPY_OPERATOR_LONG_DIVISION_QUOTIENT;
            tinypy_value_t *return_value_6 = __tinypy_operator_long_divide_views(vm, &left_view, &right_view, result_kind, out_error);
            return return_value_6;
        }
        int64_t dividend = TINYPY_INTEGER_VALUE(left);
        int64_t divisor = TINYPY_INTEGER_VALUE(right);
        int64_t quotient;
        int64_t modulo;
        if (divisor == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "integer division or modulo by zero", out_error);
            return NULL;
        }
        if (dividend == INT64_MIN && divisor == -1) {
            if (division == TINYPY_OPERATOR_DIVISION_REMAINDER) {
                tinypy_value_t *return_value_7 = tinypy_internal_long_allocate_digits(vm, 0, 0U, out_error);
                return return_value_7;
            }
            tinypy_integer_view_t left_view;
            tinypy_integer_view_t minus_one;
            __tinypy_operator_integer_view(left, &left_view);
            __tinypy_operator_integer_view(right, &minus_one);
            tinypy_value_t *return_value_8 = __tinypy_operator_long_multiply_views(vm, &left_view, &minus_one, out_error);
            return return_value_8;
        }
        quotient = dividend / divisor;
        modulo = dividend % divisor;
        if (modulo != 0 && ((modulo < 0) != (divisor < 0))) {
            modulo += divisor;
            quotient -= 1;
        }
        tinypy_value_t *return_value_9 = tinypy_integer_from_i64(vm, division == TINYPY_OPERATOR_DIVISION_REMAINDER ? modulo : quotient);
        return return_value_9;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported division operands", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_bool_t handled;
    tinypy_value_t *special;
    tinypy_value_t *native_result;

    TINYPY_CLEAR_ERROR(out_error);
    special = __tinypy_operator_special_binary(left, right, "__div__", 7U, "__rdiv__", 8U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    native_result = __tinypy_operator_native_binary(left, right, TINYPY_OPERATOR_NATIVE_DIVIDE, &handled, out_error);
    if (handled != 0) {
        return native_result;
    }
    tinypy_value_t *return_value_1 = __tinypy_operator_divide(left, right, TINYPY_OPERATOR_DIVISION_CLASSIC, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_inplace_binary(tinypy_value_t *left, tinypy_value_t *right, tinypy_binary_slot_t inplace_slot, const char *special_name, size_t special_name_size, tinypy_binary_slot_t fallback, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    TINYPY_CLEAR_ERROR(out_error);
    if (tinypy_internal_object_has_special_override(left, special_name, special_name_size) != 0) {
        tinypy_value_t *result = __tinypy_operator_call_special(left, special_name, special_name_size, right, out_error);

        if (result == NULL) {
            return NULL;
        }
        if (result != &vm->not_implemented_object.base) {
            return result;
        }
        TINYPY_DECREF(result);
    }
    if (inplace_slot != NULL) {
        tinypy_value_t *result = inplace_slot(left, right, out_error);

        if (result == NULL) {
            return NULL;
        }
        if (result != &vm->not_implemented_object.base) {
            return result;
        }
        TINYPY_DECREF(result);
        tinypy_value_t *return_value_1 = fallback(left, right, out_error);
        return return_value_1;
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
    tinypy_value_t *return_value_2 = fallback(left, right, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_add(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_binary_slot_t slot;

    slot = left->type->number_slots != NULL ? left->type->number_slots->inplace_add : NULL;
    tinypy_value_t *return_value_1 = __tinypy_operator_inplace_binary(left, right, slot, "__iadd__", 8U, tinypy_add, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_subtract(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_binary_slot_t slot;

    slot = left->type->number_slots != NULL ? left->type->number_slots->inplace_subtract : NULL;
    tinypy_value_t *return_value_1 = __tinypy_operator_inplace_binary(left, right, slot, "__isub__", 8U, tinypy_subtract, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_multiply(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_binary_slot_t slot;

    slot = left->type->number_slots != NULL ? left->type->number_slots->inplace_multiply : NULL;
    tinypy_value_t *return_value_1 = __tinypy_operator_inplace_binary(left, right, slot, "__imul__", 8U, tinypy_multiply, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_binary_slot_t slot;

    slot = left->type->number_slots != NULL ? left->type->number_slots->inplace_divide : NULL;
    tinypy_value_t *return_value_1 = __tinypy_operator_inplace_binary(left, right, slot, "__idiv__", 8U, tinypy_divide, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_floor_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_operator_inplace_binary(left, right, NULL, "__ifloordiv__", 13U, tinypy_floor_divide, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_true_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_operator_inplace_binary(left, right, NULL, "__itruediv__", 12U, tinypy_true_divide, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_remainder(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_operator_inplace_binary(left, right, NULL, "__imod__", 8U, tinypy_remainder, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_power(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_operator_inplace_binary(left, right, NULL, "__ipow__", 8U, tinypy_power, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_left_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_operator_inplace_binary(left, right, NULL, "__ilshift__", 11U, tinypy_left_shift, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_right_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_operator_inplace_binary(left, right, NULL, "__irshift__", 11U, tinypy_right_shift, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_bit_and(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_operator_inplace_binary(left, right, NULL, "__iand__", 8U, tinypy_bit_and, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_bit_xor(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_operator_inplace_binary(left, right, NULL, "__ixor__", 8U, tinypy_bit_xor, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_bit_or(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_operator_inplace_binary(left, right, NULL, "__ior__", 7U, tinypy_bit_or, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_floor_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_bool_t handled;
    tinypy_value_t *special;

    TINYPY_CLEAR_ERROR(out_error);
    special = __tinypy_operator_special_binary(left, right, "__floordiv__", 12U, "__rfloordiv__", 13U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    tinypy_value_t *return_value_1 = __tinypy_operator_divide(left, right, TINYPY_OPERATOR_DIVISION_FLOOR, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_true_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_bool_t handled;
    tinypy_value_t *special;

    TINYPY_CLEAR_ERROR(out_error);
    special = __tinypy_operator_special_binary(left, right, "__truediv__", 11U, "__rtruediv__", 12U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    tinypy_value_t *return_value_1 = __tinypy_operator_divide(left, right, TINYPY_OPERATOR_DIVISION_TRUE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_remainder(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_bool_t handled;
    tinypy_value_t *special;

    TINYPY_CLEAR_ERROR(out_error);
    special = __tinypy_operator_special_binary(left, right, "__mod__", 7U, "__rmod__", 8U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *return_value_1 = tinypy_internal_string_percent(left, right, out_error);
        return return_value_1;
    }
    tinypy_value_t *return_value_2 = __tinypy_operator_divide(left, right, TINYPY_OPERATOR_DIVISION_REMAINDER, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_divmod(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_bool_t handled;
    tinypy_value_t *special;
    tinypy_value_t *quotient;
    tinypy_value_t *remainder;
    tinypy_value_t *items[2];
    tinypy_value_t *result;

    TINYPY_CLEAR_ERROR(out_error);
    special = __tinypy_operator_special_binary(left, right, "__divmod__", 10U, "__rdivmod__", 11U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    if (__tinypy_operator_is_integer(TINYPY_VALUE_KIND(left)) != 0 && __tinypy_operator_is_integer(TINYPY_VALUE_KIND(right)) != 0 && (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_LONG || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_LONG)) {
        tinypy_integer_view_t left_view;
        tinypy_integer_view_t right_view;

        __tinypy_operator_integer_view(left, &left_view);
        __tinypy_operator_integer_view(right, &right_view);
        tinypy_value_t *return_value_1 = __tinypy_operator_long_divide_views(vm, &left_view, &right_view, TINYPY_OPERATOR_LONG_DIVISION_PAIR, out_error);
        return return_value_1;
    }
    quotient = tinypy_floor_divide(left, right, out_error);
    if (quotient == NULL) {
        return NULL;
    }
    remainder = tinypy_remainder(left, right, out_error);
    if (remainder == NULL) {
        TINYPY_DECREF(quotient);
        return NULL;
    }
    items[0] = quotient;
    items[1] = remainder;
    result = tinypy_internal_tuple_from_items_checked(vm, items, 2U, out_error);
    TINYPY_DECREF(remainder);
    TINYPY_DECREF(quotient);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_power_builtin(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_type_e left_kind;
    tinypy_value_type_e right_kind;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    TINYPY_CLEAR_ERROR(out_error);
    left_kind = TINYPY_VALUE_KIND(left);
    right_kind = TINYPY_VALUE_KIND(right);
    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "power operands must be numeric", out_error);
        return NULL;
    }
    if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
        double left_real;
        double left_imaginary;
        double right_real;
        double right_imaginary;

        if (__tinypy_operator_as_complex(left, &left_real, &left_imaginary, out_error) == 0 || __tinypy_operator_as_complex(right, &right_real, &right_imaginary, out_error) == 0) {
            return NULL;
        }
        if (right_real == 1.0 && right_imaginary == 0.0) {
            double real;
            double imaginary;

            __tinypy_operator_complex_product(1.0, 0.0, left_real, left_imaginary, &real, &imaginary);
            if (isnan(left_real) == 0 && isnan(left_imaginary) == 0 && ((isinf(left_real) != 0) != (isinf(left_imaginary) != 0))) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "complex exponentiation", out_error);
                return NULL;
            }
            tinypy_value_t *return_value_1 = tinypy_complex_from_doubles(vm, real, imaginary);
            return return_value_1;
        }
        if (left_real == 0.0 && left_imaginary == 0.0) {
            if (right_real < 0.0 || right_imaginary != 0.0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "zero cannot be raised to a negative or complex power", out_error);
                return NULL;
            }
            if (right_real == 0.0) {
                tinypy_value_t *return_value_2 = tinypy_complex_from_doubles(vm, 1.0, 0.0);
                return return_value_2;
            }
            tinypy_value_t *return_value_3 = tinypy_complex_from_doubles(vm, 0.0, 0.0);
            return return_value_3;
        }
        double real;
        double imaginary;
        tinypy_operator_complex_power_result_e power_result = __tinypy_operator_complex_power(left_real, left_imaginary, right_real, right_imaginary, &real, &imaginary);

        if (power_result == TINYPY_OPERATOR_COMPLEX_POWER_ZERO_DIVISION) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "zero to a negative or complex power", out_error);
            return NULL;
        }
        if (power_result == TINYPY_OPERATOR_COMPLEX_POWER_OVERFLOW) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "complex exponentiation", out_error);
            return NULL;
        }
        tinypy_value_t *return_value_4 = tinypy_complex_from_doubles(vm, real, imaginary);
        return return_value_4;
    }
    if (left_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_FLOAT) {
        double base;
        double exponent;
        double result;
        tinypy_bool_t negate_result = TINYPY_FALSE;

        if (__tinypy_operator_as_double(left, &base, out_error) == 0 || __tinypy_operator_as_double(right, &exponent, out_error) == 0) {
            return NULL;
        }

        if (exponent == 0.0) {
            tinypy_value_t *return_value_4 = tinypy_float_from_double(vm, 1.0);
            return return_value_4;
        }
        if (isnan(base)) {
            tinypy_value_t *return_value_5 = tinypy_float_from_double(vm, base);
            return return_value_5;
        }
        if (isnan(exponent)) {
            tinypy_value_t *return_value_6 = tinypy_float_from_double(vm, base == 1.0 ? 1.0 : exponent);
            return return_value_6;
        }
        if (isinf(exponent)) {
            double magnitude = fabs(base);

            result = magnitude == 1.0 ? 1.0 : ((exponent > 0.0) == (magnitude > 1.0) ? fabs(exponent) : 0.0);
            tinypy_value_t *return_value_7 = tinypy_float_from_double(vm, result);
            return return_value_7;
        }
        if (isinf(base)) {
            tinypy_bool_t odd = fmod(fabs(exponent), 2.0) == 1.0 ? TINYPY_TRUE : TINYPY_FALSE;

            result = exponent > 0.0 ? (odd != 0 ? base : fabs(base)) : (odd != 0 ? copysign(0.0, base) : 0.0);
            tinypy_value_t *return_value_8 = tinypy_float_from_double(vm, result);
            return return_value_8;
        }
        if (base == 0.0) {
            tinypy_bool_t odd = fmod(fabs(exponent), 2.0) == 1.0 ? TINYPY_TRUE : TINYPY_FALSE;

            if (exponent < 0.0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "zero cannot be raised to a negative power", out_error);
                return NULL;
            }
            tinypy_value_t *return_value_9 = tinypy_float_from_double(vm, odd != 0 ? base : 0.0);
            return return_value_9;
        }
        if (base < 0.0) {
            if (floor(exponent) != exponent) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "negative number cannot be raised to a fractional power", out_error);
                return NULL;
            }
            base = -base;
            negate_result = fmod(fabs(exponent), 2.0) == 1.0 ? TINYPY_TRUE : TINYPY_FALSE;
        }
        if (base == 1.0) {
            tinypy_value_t *return_value_10 = tinypy_float_from_double(vm, negate_result != 0 ? -1.0 : 1.0);
            return return_value_10;
        }
        errno = 0;
        result = pow(base, exponent);
        if (negate_result != 0) {
            result = -result;
        }
        if ((errno == ERANGE && isinf(result)) || (isinf(result) && isfinite(base) && isfinite(exponent))) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "floating point power overflow", out_error);
            return NULL;
        }
        if (errno != 0 && errno != ERANGE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid floating point power", out_error);
            return NULL;
        }
        tinypy_value_t *return_value_11 = tinypy_float_from_double(vm, result);
        return return_value_11;
    }
    tinypy_integer_view_t exponent_view;
    size_t exponent;
    tinypy_bool_t prefer_long = left_kind == TINYPY_VALUE_LONG || right_kind == TINYPY_VALUE_LONG;
    tinypy_value_t *result;
    tinypy_value_t *base;

    __tinypy_operator_integer_view(right, &exponent_view);
    if (exponent_view.sign < 0) {
        double base_value;
        double exponent_value;

        if (__tinypy_operator_as_double(left, &base_value, out_error) == 0 || __tinypy_operator_as_double(right, &exponent_value, out_error) == 0) {
            return NULL;
        }

        if (base_value == 0.0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "zero cannot be raised to a negative power", out_error);
            return NULL;
        }
        double power = pow(base_value, exponent_value);
        tinypy_value_t *return_value_5 = tinypy_float_from_double(vm, power);
        return return_value_5;
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
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_power(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_bool_t handled;
    tinypy_value_t *special;

    TINYPY_CLEAR_ERROR(out_error);
    special = __tinypy_operator_special_binary(left, right, "__pow__", 7U, "__rpow__", 8U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    tinypy_value_t *result = __tinypy_operator_power_builtin(left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_power_modulo_promotes_i64(int64_t base, int64_t exponent, int64_t modulus) {
    int64_t result = INT64_C(1);
    int64_t factor = base;

    while (exponent != 0) {
        if ((exponent & INT64_C(1)) != 0) {
            int64_t previous = result;

            result = (int64_t)((uint64_t)result * (uint64_t)factor);
            if (factor == 0) {
                break;
            }
            if ((result == INT64_MIN && factor == -1) || result / factor != previous) {
                return TINYPY_TRUE;
            }
        }
        exponent >>= 1U;
        if (exponent == 0) {
            break;
        }
        int64_t previous = factor;

        factor = (int64_t)((uint64_t)factor * (uint64_t)factor);
        if (previous != 0 && ((factor == INT64_MIN && previous == -1) || factor / previous != previous)) {
            return TINYPY_TRUE;
        }
        if (modulus != 0) {
            if ((result == INT64_MIN || factor == INT64_MIN) && modulus == -1) {
                return TINYPY_TRUE;
            }
            result %= modulus;
            factor %= modulus;
        }
    }
    return result == INT64_MIN && modulus == -1 ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_power_modulo_builtin(tinypy_value_t *base_value, tinypy_value_t *exponent_value, tinypy_value_t *modulus_value, tinypy_error_t **out_error) {
    tinypy_value_type_e base_kind = TINYPY_VALUE_KIND(base_value);
    tinypy_value_type_e exponent_kind = TINYPY_VALUE_KIND(exponent_value);
    tinypy_value_type_e modulus_kind = TINYPY_VALUE_KIND(modulus_value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(base_value);
    tinypy_integer_view_t exponent;
    tinypy_integer_view_t modulus;
    tinypy_value_t *result;
    tinypy_value_t *base;
    size_t digit_index;
    size_t bit_index;
    tinypy_bool_t prefer_long;

    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_operator_is_integer(base_kind) == 0 || __tinypy_operator_is_integer(exponent_kind) == 0 || __tinypy_operator_is_integer(modulus_kind) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "pow() 3rd argument requires integer operands", out_error);
        return NULL;
    }
    __tinypy_operator_integer_view(exponent_value, &exponent);
    __tinypy_operator_integer_view(modulus_value, &modulus);
    if (exponent.sign < 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "pow() 2nd argument cannot be negative when 3rd argument specified", out_error);
        return NULL;
    }
    if (modulus.sign == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "pow() 3rd argument cannot be 0", out_error);
        return NULL;
    }
    prefer_long = base_kind == TINYPY_VALUE_LONG || exponent_kind == TINYPY_VALUE_LONG || modulus_kind == TINYPY_VALUE_LONG;
    if (prefer_long == 0) {
        prefer_long = __tinypy_operator_power_modulo_promotes_i64(
            TINYPY_INTEGER_VALUE(base_value),
            TINYPY_INTEGER_VALUE(exponent_value),
            TINYPY_INTEGER_VALUE(modulus_value));
    }
    result = prefer_long != 0 ? tinypy_long_from_i64(vm, INT64_C(1)) : tinypy_integer_from_i64(vm, INT64_C(1));
    {
        tinypy_value_t *reduced = tinypy_remainder(result, modulus_value, out_error);

        TINYPY_DECREF(result);
        if (reduced == NULL) {
            return NULL;
        }
        result = reduced;
    }
    base = tinypy_remainder(base_value, modulus_value, out_error);
    if (base == NULL) {
        TINYPY_DECREF(result);
        return NULL;
    }
    for (digit_index = 0U; digit_index < exponent.count; ++digit_index) {
        uint16_t digit = exponent.digits[digit_index];
        size_t bits_in_digit = 15U;

        if (digit_index + 1U == exponent.count) {
            bits_in_digit = 0U;
            while ((digit >> bits_in_digit) != 0U) {
                bits_in_digit += 1U;
            }
        }

        for (bit_index = 0U; bit_index < bits_in_digit; ++bit_index) {
            tinypy_value_t *squared;
            tinypy_value_t *square_reduced;

            if ((digit & (UINT16_C(1) << bit_index)) != 0U) {
                tinypy_value_t *multiplied = tinypy_multiply(result, base, out_error);
                tinypy_value_t *reduced;

                if (multiplied == NULL) {
                    TINYPY_DECREF(base);
                    TINYPY_DECREF(result);
                    return NULL;
                }
                reduced = tinypy_remainder(multiplied, modulus_value, out_error);
                TINYPY_DECREF(multiplied);
                if (reduced == NULL) {
                    TINYPY_DECREF(base);
                    TINYPY_DECREF(result);
                    return NULL;
                }
                TINYPY_DECREF(result);
                result = reduced;
            }
            if (digit_index + 1U == exponent.count && bit_index + 1U == bits_in_digit) {
                break;
            }
            squared = tinypy_multiply(base, base, out_error);
            if (squared == NULL) {
                TINYPY_DECREF(base);
                TINYPY_DECREF(result);
                return NULL;
            }
            square_reduced = tinypy_remainder(squared, modulus_value, out_error);
            TINYPY_DECREF(squared);
            if (square_reduced == NULL) {
                TINYPY_DECREF(base);
                TINYPY_DECREF(result);
                return NULL;
            }
            TINYPY_DECREF(base);
            base = square_reduced;
        }
    }
    TINYPY_DECREF(base);
    if (prefer_long != 0 && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_LONG) {
        tinypy_value_t *promoted = tinypy_long_from_i64(vm, TINYPY_INTEGER_VALUE(result));

        TINYPY_DECREF(result);
        result = promoted;
    }
    else if (prefer_long == 0 && TINYPY_VALUE_KIND(result) == TINYPY_VALUE_LONG) {
        tinypy_value_t *demoted = tinypy_integer_from_i64(vm, tinypy_long_as_i64(result));

        TINYPY_DECREF(result);
        result = demoted;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_power_modulo(tinypy_value_t *base_value, tinypy_value_t *exponent_value, tinypy_value_t *modulus_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(base_value);

    TINYPY_CLEAR_ERROR(out_error);
    if (base_value->type->number_slots != NULL && base_value->type->number_slots->power != NULL) {
        tinypy_value_t *return_value_1 = base_value->type->number_slots->power(base_value, exponent_value, modulus_value, out_error);
        return return_value_1;
    }
    if (tinypy_internal_object_has_special(base_value, "__pow__", 7U) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special(base_value, "__pow__", 7U, out_error);
        tinypy_value_t *arguments[2] = {exponent_value, modulus_value};
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) {
            return NULL;
        }
        args = tinypy_tuple_from_items(vm, arguments, 2U);
        result = tinypy_call(method, args, NULL, out_error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(method);
        if (result == NULL || result != &vm->not_implemented_object.base) {
            return result;
        }
        TINYPY_DECREF(result);
    }
    tinypy_value_t *return_value_2 = tinypy_internal_power_modulo_builtin(base_value, exponent_value, modulus_value, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_left_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    size_t shift;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_bool_t handled;
    tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__lshift__", 10U, "__rlshift__", 11U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    tinypy_bool_t condition_6 = __tinypy_operator_is_integer(kind) == 0;
    if (condition_6 == 0) {
        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(right);
        condition_6 = __tinypy_operator_is_integer(kind_2) == 0;
    }
    if (condition_6) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "left shift operand must be an integer", out_error);
        return NULL;
    }
    if (__tinypy_operator_shift_count(vm, right, &shift, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_operator_integer_left_shift(vm, left, shift, TINYPY_VALUE_KIND(left) == TINYPY_VALUE_LONG || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_LONG, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_right_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    size_t shift;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_bool_t handled;
    tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__rshift__", 10U, "__rrshift__", 11U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    tinypy_bool_t condition_7 = __tinypy_operator_is_integer(kind) == 0;
    if (condition_7 == 0) {
        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(right);
        condition_7 = __tinypy_operator_is_integer(kind_2) == 0;
    }
    if (condition_7) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "right shift operand must be an integer", out_error);
        return NULL;
    }
    if (__tinypy_operator_shift_count(vm, right, &shift, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_operator_integer_right_shift(vm, left, shift, TINYPY_VALUE_KIND(left) == TINYPY_VALUE_LONG || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_LONG, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bit_and(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_bool_t handled;
    tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__and__", 7U, "__rand__", 8U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_SET || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_FROZENSET) {
        tinypy_value_t *return_value_1 = tinypy_internal_set_binary(left, right, INT32_C(0), out_error);
        return return_value_1;
    }
    if (__tinypy_operator_is_integer(TINYPY_VALUE_KIND(left)) == 0 || __tinypy_operator_is_integer(TINYPY_VALUE_KIND(right)) == 0) {
        special = __tinypy_operator_regular_binary(left, right, "__and__", 7U, "__rand__", 8U, &handled, out_error);
        if (handled != 0) {
            return special;
        }
    }
    tinypy_value_t *return_value_2 = __tinypy_operator_integer_bitwise(vm, left, right, 0, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bit_xor(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_bool_t handled;
    tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__xor__", 7U, "__rxor__", 8U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_SET || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_FROZENSET) {
        tinypy_value_t *return_value_1 = tinypy_internal_set_binary(left, right, INT32_C(1), out_error);
        return return_value_1;
    }
    if (__tinypy_operator_is_integer(TINYPY_VALUE_KIND(left)) == 0 || __tinypy_operator_is_integer(TINYPY_VALUE_KIND(right)) == 0) {
        special = __tinypy_operator_regular_binary(left, right, "__xor__", 7U, "__rxor__", 8U, &handled, out_error);
        if (handled != 0) {
            return special;
        }
    }
    tinypy_value_t *return_value_2 = __tinypy_operator_integer_bitwise(vm, left, right, 1, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bit_or(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_bool_t handled;
    tinypy_value_t *special = __tinypy_operator_special_binary(left, right, "__or__", 6U, "__ror__", 7U, &handled, out_error);
    if (handled != 0) {
        return special;
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_SET || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_FROZENSET) {
        tinypy_value_t *return_value_1 = tinypy_internal_set_binary(left, right, INT32_C(2), out_error);
        return return_value_1;
    }
    if (__tinypy_operator_is_integer(TINYPY_VALUE_KIND(left)) == 0 || __tinypy_operator_is_integer(TINYPY_VALUE_KIND(right)) == 0) {
        special = __tinypy_operator_regular_binary(left, right, "__or__", 6U, "__ror__", 7U, &handled, out_error);
        if (handled != 0) {
            return special;
        }
    }
    tinypy_value_t *return_value_2 = __tinypy_operator_integer_bitwise(vm, left, right, 2, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_multiply_builtin(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);
    size_t repeat_count;

    if (left_kind == TINYPY_VALUE_STRING || left_kind == TINYPY_VALUE_UNICODE || left_kind == TINYPY_VALUE_TUPLE || left_kind == TINYPY_VALUE_LIST) {
        int32_t repeat = __tinypy_operator_repeat_count(right, &repeat_count, out_error);

        if (repeat < 0) {
            return NULL;
        }
        if (repeat != 0) {
            tinypy_value_t *result = __tinypy_operator_repeat(vm, left, repeat_count, out_error);

            return result;
        }
    }
    if (right_kind == TINYPY_VALUE_STRING || right_kind == TINYPY_VALUE_UNICODE || right_kind == TINYPY_VALUE_TUPLE || right_kind == TINYPY_VALUE_LIST) {
        int32_t repeat = __tinypy_operator_repeat_count(left, &repeat_count, out_error);

        if (repeat < 0) {
            return NULL;
        }
        if (repeat != 0) {
            tinypy_value_t *result = __tinypy_operator_repeat(vm, right, repeat_count, out_error);

            return result;
        }
    }
    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unsupported multiply operands", out_error);
        return NULL;
    }
    if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
        double ar;
        double ai;
        double br;
        double bi;

        if (__tinypy_operator_as_complex(left, &ar, &ai, out_error) == 0 || __tinypy_operator_as_complex(right, &br, &bi, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *result = tinypy_complex_from_doubles(vm, ar * br - ai * bi, ar * bi + ai * br);

        return result;
    }
    if (left_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_FLOAT) {
        double left_value;
        double right_value;

        if (__tinypy_operator_as_double(left, &left_value, out_error) == 0 || __tinypy_operator_as_double(right, &right_value, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *result = tinypy_float_from_double(vm, left_value * right_value);

        return result;
    }
    if (left_kind != TINYPY_VALUE_LONG && right_kind != TINYPY_VALUE_LONG) {
        int64_t value;

        if (__tinypy_operator_multiply_overflow(TINYPY_INTEGER_VALUE(left), TINYPY_INTEGER_VALUE(right), &value) == 0) {
            tinypy_value_t *result = tinypy_integer_from_i64(vm, value);

            return result;
        }
    }
    tinypy_integer_view_t left_view;
    tinypy_integer_view_t right_view;

    __tinypy_operator_integer_view(left, &left_view);
    __tinypy_operator_integer_view(right, &right_view);
    tinypy_value_t *result = __tinypy_operator_long_multiply_views(vm, &left_view, &right_view, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_operator_builtin(tinypy_value_t *left, tinypy_value_t *right, int32_t mode, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);
    tinypy_value_t *shared_result;

    TINYPY_CLEAR_ERROR(out_error);
    switch (mode) {
    case 0:
        if ((left_kind == TINYPY_VALUE_STRING || left_kind == TINYPY_VALUE_UNICODE) && (right_kind == TINYPY_VALUE_STRING || right_kind == TINYPY_VALUE_UNICODE)) {
            tinypy_value_t *result = __tinypy_operator_concat_text(vm, left, right, left_kind == TINYPY_VALUE_UNICODE || right_kind == TINYPY_VALUE_UNICODE, out_error);

            return result;
        }
        if ((left_kind == TINYPY_VALUE_TUPLE || left_kind == TINYPY_VALUE_LIST) && left_kind == right_kind) {
            tinypy_value_t *result = __tinypy_operator_concat_sequence(vm, left, right, out_error);

            return result;
        }
        shared_result = __tinypy_operator_numeric_add(vm, left, right, 0, out_error);

        return shared_result;
    case 1:
        if (left_kind == TINYPY_VALUE_SET || left_kind == TINYPY_VALUE_FROZENSET) {
            tinypy_value_t *result = tinypy_internal_set_binary(left, right, INT32_C(3), out_error);

            return result;
        }
        shared_result = __tinypy_operator_numeric_add(vm, left, right, 1, out_error);

        return shared_result;
    case 2:
        shared_result = __tinypy_operator_multiply_builtin(left, right, out_error);
        return shared_result;
    case 3:
        shared_result = __tinypy_operator_divide(left, right, TINYPY_OPERATOR_DIVISION_CLASSIC, out_error);
        return shared_result;
    case 4:
        shared_result = __tinypy_operator_divide(left, right, TINYPY_OPERATOR_DIVISION_FLOOR, out_error);
        return shared_result;
    case 5:
        shared_result = __tinypy_operator_divide(left, right, TINYPY_OPERATOR_DIVISION_TRUE, out_error);
        return shared_result;
    case 6:
        if (left_kind == TINYPY_VALUE_STRING || left_kind == TINYPY_VALUE_UNICODE) {
            tinypy_value_t *result = tinypy_internal_string_percent(left, right, out_error);

            return result;
        }
        shared_result = __tinypy_operator_divide(left, right, TINYPY_OPERATOR_DIVISION_REMAINDER, out_error);
        return shared_result;
    case 7: {
        tinypy_value_t *quotient;
        tinypy_value_t *remainder;
        tinypy_value_t *items[2];
        tinypy_value_t *result;

        if (__tinypy_operator_is_integer(left_kind) != 0 && __tinypy_operator_is_integer(right_kind) != 0 && (left_kind == TINYPY_VALUE_LONG || right_kind == TINYPY_VALUE_LONG)) {
            tinypy_integer_view_t left_view;
            tinypy_integer_view_t right_view;

            __tinypy_operator_integer_view(left, &left_view);
            __tinypy_operator_integer_view(right, &right_view);
            tinypy_value_t *long_result = __tinypy_operator_long_divide_views(vm, &left_view, &right_view, TINYPY_OPERATOR_LONG_DIVISION_PAIR, out_error);

            return long_result;
        }
        quotient = __tinypy_operator_divide(left, right, TINYPY_OPERATOR_DIVISION_FLOOR, out_error);
        if (quotient == NULL) {
            return NULL;
        }
        remainder = __tinypy_operator_divide(left, right, TINYPY_OPERATOR_DIVISION_REMAINDER, out_error);
        if (remainder == NULL) {
            TINYPY_DECREF(quotient);
            return NULL;
        }
        items[0] = quotient;
        items[1] = remainder;
        result = tinypy_internal_tuple_from_items_checked(vm, items, 2U, out_error);
        TINYPY_DECREF(remainder);
        TINYPY_DECREF(quotient);
        return result;
    }
    case 8:
        shared_result = __tinypy_operator_power_builtin(left, right, out_error);
        return shared_result;
    case 9:
    case 10: {
        size_t shift;

        if (__tinypy_operator_is_integer(left_kind) == 0 || __tinypy_operator_is_integer(right_kind) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "shift operands must be integers", out_error);
            return NULL;
        }
        if (__tinypy_operator_shift_count(vm, right, &shift, out_error) == 0) {
            return NULL;
        }
        if (mode == 9) {
            tinypy_value_t *result = __tinypy_operator_integer_left_shift(vm, left, shift, left_kind == TINYPY_VALUE_LONG || right_kind == TINYPY_VALUE_LONG, out_error);

            return result;
        }
        tinypy_value_t *result = __tinypy_operator_integer_right_shift(vm, left, shift, left_kind == TINYPY_VALUE_LONG || right_kind == TINYPY_VALUE_LONG, out_error);

        return result;
    }
    case 11:
    case 12:
    default:
        if (left_kind == TINYPY_VALUE_SET || left_kind == TINYPY_VALUE_FROZENSET) {
            tinypy_value_t *result = tinypy_internal_set_binary(left, right, mode == 11 ? INT32_C(0) : (mode == 12 ? INT32_C(1) : INT32_C(2)), out_error);

            return result;
        }
        shared_result = __tinypy_operator_integer_bitwise(vm, left, right, mode == 11 ? 0 : (mode == 12 ? 1 : 2), out_error);

        return shared_result;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_unary_builtin(tinypy_value_t *value, int32_t mode, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_operator_is_number(kind) == 0 || (mode == 2 && __tinypy_operator_is_integer(kind) == 0)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin numeric descriptor requires a numeric operand", out_error);
        return NULL;
    }
    if (mode == 0) {
        if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
            tinypy_value_t *result = tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));

            return result;
        }
        if (kind == TINYPY_VALUE_LONG) {
            tinypy_value_t *result = tinypy_long_from_base15_digits(vm, TINYPY_LONG_SIGN(value), TINYPY_LONG_OBJECT(value)->digits, TINYPY_LONG_DIGIT_COUNT(value));

            return result;
        }
        if (kind == TINYPY_VALUE_FLOAT) {
            tinypy_value_t *result = tinypy_float_from_double(vm, TINYPY_FLOAT_OBJECT(value)->value);

            return result;
        }
        tinypy_value_t *result = tinypy_complex_from_doubles(vm, TINYPY_COMPLEX_OBJECT(value)->real, TINYPY_COMPLEX_OBJECT(value)->imaginary);

        return result;
    }
    if (mode == 1) {
        if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
            int64_t integer = TINYPY_INTEGER_VALUE(value);

            if (integer != INT64_MIN) {
                tinypy_value_t *result = tinypy_integer_from_i64(vm, -integer);

                return result;
            }
        }
        if (__tinypy_operator_is_integer(kind) != 0) {
            tinypy_integer_view_t view;

            __tinypy_operator_integer_view(value, &view);
            tinypy_value_t *result = tinypy_long_from_base15_digits(vm, -view.sign, view.digits, view.count);

            return result;
        }
        if (kind == TINYPY_VALUE_FLOAT) {
            tinypy_value_t *result = tinypy_float_from_double(vm, -TINYPY_FLOAT_OBJECT(value)->value);

            return result;
        }
        tinypy_value_t *result = tinypy_complex_from_doubles(vm, -TINYPY_COMPLEX_OBJECT(value)->real, -TINYPY_COMPLEX_OBJECT(value)->imaginary);

        return result;
    }
    if (mode == 2) {
        if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
            tinypy_value_t *result = tinypy_integer_from_i64(vm, ~TINYPY_INTEGER_VALUE(value));

            return result;
        }
        tinypy_value_t *negative = tinypy_internal_unary_builtin(value, 1, out_error);
        tinypy_value_t *one;
        tinypy_value_t *result;

        if (negative == NULL) {
            return NULL;
        }
        one = tinypy_integer_from_i64(vm, 1);
        result = __tinypy_operator_numeric_add(vm, negative, one, 1, out_error);
        TINYPY_DECREF(one);
        TINYPY_DECREF(negative);
        return result;
    }
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        tinypy_value_t *result = TINYPY_INTEGER_VALUE(value) < 0 ? tinypy_internal_unary_builtin(value, 1, out_error) : tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));

        return result;
    }
    if (kind == TINYPY_VALUE_LONG) {
        tinypy_value_t *result = TINYPY_LONG_SIGN(value) < 0 ? tinypy_internal_unary_builtin(value, 1, out_error) : tinypy_long_from_base15_digits(vm, TINYPY_LONG_SIGN(value), TINYPY_LONG_OBJECT(value)->digits, TINYPY_LONG_DIGIT_COUNT(value));

        return result;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        tinypy_value_t *result = tinypy_float_from_double(vm, fabs(TINYPY_FLOAT_OBJECT(value)->value));

        return result;
    }
    tinypy_value_t *result = tinypy_float_from_double(vm, hypot(TINYPY_COMPLEX_OBJECT(value)->real, TINYPY_COMPLEX_OBJECT(value)->imaginary));

    return result;
}
