#include "tinypy/operator.h"

#include "internal.h"

#include <errno.h>
#include <float.h>
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

typedef struct tinypy_operator_long_modulus_t {
    const uint32_t *words;
    const uint32_t *normalized;
    uint32_t *product;
    size_t count;
    uint32_t shift;
} tinypy_operator_long_modulus_t;

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_is_integer(tinypy_value_type_e kind) {
    return kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_operator_integer_sign(const tinypy_value_t *value) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG) {
        int32_t long_sign = TINYPY_LONG_SIGN(value);

        return long_sign;
    }
    int64_t integer = TINYPY_INTEGER_VALUE(value);
    int32_t sign = integer < 0 ? -1 : (integer > 0 ? 1 : 0);

    return sign;
}
//////////////////////////////////////////////////////////////////////////
/* "unsupported operand type(s) for +: 'int' and 'str'", as binary_op reports. */
static tinypy_value_t *__tinypy_operator_unsupported(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, const char *symbol, tinypy_error_t **out_error) {
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("unsupported operand type(s) for "),
        {symbol, strlen(symbol)},
        TINYPY_MESSAGE_PART_LITERAL(": '"),
        TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(left, 100U),
        TINYPY_MESSAGE_PART_LITERAL("' and '"),
        TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(right, 100U),
        TINYPY_MESSAGE_PART_LITERAL("'"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_bad_unary(tinypy_vm_t *vm, tinypy_value_t *value, const char *operation, tinypy_error_t **out_error) {
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("bad operand type for "),
        {operation, strlen(operation)},
        TINYPY_MESSAGE_PART_LITERAL(": '"),
        TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(value, 200U),
        TINYPY_MESSAGE_PART_LITERAL("'"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return NULL;
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
static size_t __tinypy_operator_view_bit_length(const tinypy_integer_view_t *view) {
    size_t bits;
    uint16_t top;

    if (view->count == 0U) {
        return 0U;
    }
    bits = (view->count - 1U) * 15U;
    top = view->digits[view->count - 1U];
    while (top != 0U) {
        bits += 1U;
        top >>= 1U;
    }
    return bits;
}
//////////////////////////////////////////////////////////////////////////
/* The 30-bit word of a magnitude at index: the pair of 15-bit digits it
   covers. */
static uint32_t __tinypy_operator_view_word(const tinypy_integer_view_t *view, size_t index) {
    size_t digit_index = index * 2U;
    uint32_t word = view->digits[digit_index];

    if (digit_index + 1U < view->count) {
        word |= (uint32_t)view->digits[digit_index + 1U] << 15U;
    }
    return word;
}
//////////////////////////////////////////////////////////////////////////
/* The digit count and the most significant digit of a magnitude in
   CPython's 30-bit long digits. */
static void __tinypy_operator_view_top_digit30(const tinypy_integer_view_t *view, size_t *out_count, uint32_t *out_top) {
    size_t count = (__tinypy_operator_view_bit_length(view) + 29U) / 30U;
    uint32_t top = count != 0U ? __tinypy_operator_view_word(view, count - 1U) : 0U;

    *out_count = count;
    *out_top = top;
}
//////////////////////////////////////////////////////////////////////////
/* long_divrem hands the dividend itself back as the remainder when its
   magnitude is below the divisor's by 30-bit digits, and l_divmod keeps it
   unless the signs differ, so a long subclass survives '%' and divmod(). */
static tinypy_bool_t __tinypy_operator_remainder_is_dividend(const tinypy_integer_view_t *left, const tinypy_integer_view_t *right) {
    if (right->sign == 0 || (left->sign != 0 && left->sign != right->sign)) {
        return TINYPY_FALSE;
    }
    size_t left_count;
    uint32_t left_top;
    __tinypy_operator_view_top_digit30(left, &left_count, &left_top);
    size_t right_count;
    uint32_t right_top;
    __tinypy_operator_view_top_digit30(right, &right_count, &right_top);
    tinypy_bool_t smaller = left_count < right_count || (left_count == right_count && left_top < right_top) ? TINYPY_TRUE : TINYPY_FALSE;

    return smaller;
}
//////////////////////////////////////////////////////////////////////////
/* Unary plus and abs() of a numeric subclass instance yield the exact base
   type, as int_pos and friends do. */
static tinypy_value_t *__tinypy_operator_exact_number(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (value->type == &vm->types[kind] && kind != TINYPY_VALUE_BOOL) {
        return TINYPY_RET(value);
    }
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        tinypy_value_t *integer = tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));
        return integer;
    }
    if (kind == TINYPY_VALUE_LONG) {
        tinypy_value_t *copy = tinypy_internal_long_from_base15_digits_checked(vm, TINYPY_LONG_SIGN(value), TINYPY_LONG_OBJECT(value)->digits, TINYPY_LONG_DIGIT_COUNT(value), out_error);
        return copy;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        tinypy_value_t *copy = tinypy_float_from_double(vm, TINYPY_FLOAT_OBJECT(value)->value);
        return copy;
    }
    tinypy_value_t *copy = tinypy_complex_from_doubles(vm, TINYPY_COMPLEX_OBJECT(value)->real, TINYPY_COMPLEX_OBJECT(value)->imaginary);
    return copy;
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
    digits = (uint16_t *)__tinypy_internal_vm_allocate_checked(vm, capacity * sizeof(*digits), out_error);
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
static size_t __tinypy_operator_trim_digits(const uint16_t *digits, size_t count) {
    while (count != 0U && digits[count - 1U] == 0U) {
        count -= 1U;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
/* The multiplication and division kernels work on CPython's 30-bit digits,
   called words here: each packs the pair of 15-bit digits it covers, least
   significant first. Operands small enough use a local buffer; the
   Karatsuba cutoffs are measured for these kernels. */
#define TINYPY_LONG_WORD_BASE UINT32_C(0x40000000)
#define TINYPY_LONG_WORD_MASK UINT32_C(0x3fffffff)
#define TINYPY_LONG_LOCAL_WORDS 96U
#define TINYPY_LONG_KARATSUBA_CUTOFF 60U
#define TINYPY_LONG_KARATSUBA_SQUARE_CUTOFF 80U
#define TINYPY_LONG_FIVEARY_CUTOFF 240U
//////////////////////////////////////////////////////////////////////////
TINYPY_NOINLINE tinypy_bool_t tinypy_internal_long_poll_interrupt(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    vm->long_poll_steps = TINYPY_LONG_POLL_STEPS;
    if (vm->compile_depth != 0U) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t running = tinypy_internal_eval_poll_interrupt(vm, out_error);
    return running;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_operator_long_word_count(size_t digit_count) {
    size_t word_count = digit_count / 2U + (digit_count & 1U);

    return word_count;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_operator_long_pack_words(uint32_t *words, const uint16_t *digits, size_t digit_count) {
    size_t pair_count = digit_count / 2U;

    for (size_t index = 0U; index != pair_count; ++index) {
        words[index] = (uint32_t)digits[index * 2U] | ((uint32_t)digits[index * 2U + 1U] << 15U);
    }
    if ((digit_count & 1U) == 0U) {
        return pair_count;
    }
    words[pair_count] = digits[digit_count - 1U];
    return pair_count + 1U;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_operator_long_trim_words(const uint32_t *words, size_t count) {
    while (count != 0U && words[count - 1U] == 0U) {
        count -= 1U;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
/* A new long of the sign and the magnitude the words hold. */
static tinypy_value_t *__tinypy_operator_long_from_words(tinypy_vm_t *vm, int32_t sign, const uint32_t *words, size_t count, tinypy_error_t **out_error) {
    count = __tinypy_operator_long_trim_words(words, count);
    if (count == 0U) {
        tinypy_value_t *zero = tinypy_internal_long_allocate_digits(vm, 0, 0U, out_error);
        return zero;
    }
    size_t last = count - 1U;
    size_t digit_count = words[last] > TINYPY_LONG_MASK ? count * 2U : count * 2U - 1U;
    tinypy_value_t *result = tinypy_internal_long_allocate_digits(vm, sign, digit_count, out_error);

    if (result == NULL) {
        return NULL;
    }
    uint16_t *digits = TINYPY_LONG_OBJECT(result)->digits;

    for (size_t index = 0U; index != last; ++index) {
        digits[index * 2U] = (uint16_t)(words[index] & TINYPY_LONG_MASK);
        digits[index * 2U + 1U] = (uint16_t)(words[index] >> 15U);
    }
    digits[last * 2U] = (uint16_t)(words[last] & TINYPY_LONG_MASK);
    if (digit_count != last * 2U + 1U) {
        digits[last * 2U + 1U] = (uint16_t)(words[last] >> 15U);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static uint32_t *__tinypy_operator_long_allocate_words(tinypy_vm_t *vm, uint32_t *local_words, size_t count, tinypy_error_t **out_error) {
    if (count <= TINYPY_LONG_LOCAL_WORDS) {
        return local_words;
    }
    if (count > SIZE_MAX / sizeof(*local_words)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long int temporary storage is too large", out_error);
        return NULL;
    }
    uint32_t *words = (uint32_t *)__tinypy_internal_vm_allocate_checked(vm, count * sizeof(*words), out_error);
    return words;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_operator_long_release_words(tinypy_vm_t *vm, uint32_t *words, const uint32_t *local_words, size_t count) {
    if (words != local_words) {
        tinypy_internal_vm_deallocate(vm, words, count * sizeof(*words));
    }
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_operator_long_compare_words(const uint32_t *left, size_t left_count, const uint32_t *right, size_t right_count) {
    if (left_count != right_count) {
        return left_count < right_count ? -1 : 1;
    }
    for (size_t index = left_count; index != 0U; --index) {
        if (left[index - 1U] != right[index - 1U]) {
            return left[index - 1U] < right[index - 1U] ? -1 : 1;
        }
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_operator_long_carry_words(uint32_t *output, size_t output_capacity, size_t index, uint64_t carry) {
    while (carry != 0U && index < output_capacity) {
        carry += output[index];
        output[index] = (uint32_t)(carry & TINYPY_LONG_WORD_MASK);
        carry >>= 30U;
        index += 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
/* output[0, output_capacity) += left * right, as x_mul computes it: a square
   takes every cross product once, doubled. */
static void __tinypy_operator_long_multiply_schoolbook(uint32_t *output, size_t output_capacity, const uint32_t *left, size_t left_count, const uint32_t *right, size_t right_count) {
    if (left == right && left_count == right_count) {
        for (size_t left_index = 0U; left_index != left_count; ++left_index) {
            uint64_t factor = left[left_index];
            size_t output_index = left_index * 2U;
            uint64_t carry = (uint64_t)output[output_index] + factor * factor;

            output[output_index] = (uint32_t)(carry & TINYPY_LONG_WORD_MASK);
            carry >>= 30U;
            factor <<= 1U;
            for (size_t right_index = left_index + 1U; right_index != left_count; ++right_index) {
                output_index += 1U;
                carry += (uint64_t)output[output_index] + (uint64_t)left[right_index] * factor;
                output[output_index] = (uint32_t)(carry & TINYPY_LONG_WORD_MASK);
                carry >>= 30U;
            }
            __tinypy_operator_long_carry_words(output, output_capacity, output_index + 1U, carry);
        }
        return;
    }
    if (left_count > right_count) {
        const uint32_t *words = left;
        size_t count = left_count;

        left = right;
        left_count = right_count;
        right = words;
        right_count = count;
    }
    for (size_t left_index = 0U; left_index != left_count; ++left_index) {
        uint64_t factor = left[left_index];
        uint32_t *window = output + left_index;
        uint64_t carry = 0U;

        for (size_t right_index = 0U; right_index != right_count; ++right_index) {
            carry += (uint64_t)window[right_index] + (uint64_t)right[right_index] * factor;
            window[right_index] = (uint32_t)(carry & TINYPY_LONG_WORD_MASK);
            carry >>= 30U;
        }
        __tinypy_operator_long_carry_words(output, output_capacity, left_index + right_count, carry);
    }
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_operator_long_add_words(uint32_t *output, const uint32_t *left, size_t left_count, const uint32_t *right, size_t right_count) {
    size_t count = left_count > right_count ? left_count : right_count;
    uint32_t carry = 0U;

    for (size_t index = 0U; index != count; ++index) {
        uint32_t sum = (index < left_count ? left[index] : 0U) + (index < right_count ? right[index] : 0U) + carry;

        output[index] = sum & TINYPY_LONG_WORD_MASK;
        carry = sum >> 30U;
    }
    if (carry != 0U) {
        output[count++] = carry;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
/* target -= subtrahend for a target at least as large; returns the trimmed
   count. */
static size_t __tinypy_operator_long_subtract_words(uint32_t *target, size_t target_count, const uint32_t *subtrahend, size_t subtrahend_count) {
    uint32_t borrow = 0U;

    for (size_t index = 0U; index != target_count; ++index) {
        uint32_t difference = target[index] - ((index < subtrahend_count ? subtrahend[index] : 0U) + borrow);

        borrow = difference >> 31U;
        target[index] = difference & TINYPY_LONG_WORD_MASK;
    }
    size_t count = __tinypy_operator_long_trim_words(target, target_count);
    return count;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_operator_long_add_shifted(uint32_t *output, size_t output_capacity, const uint32_t *words, size_t count, size_t shift) {
    uint32_t carry = 0U;

    for (size_t index = 0U; index != count; ++index) {
        uint32_t sum = output[shift + index] + words[index] + carry;

        output[shift + index] = sum & TINYPY_LONG_WORD_MASK;
        carry = sum >> 30U;
    }
    __tinypy_operator_long_carry_words(output, output_capacity, shift + count, carry);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_long_multiply_words(tinypy_vm_t *vm, uint32_t *output, size_t output_capacity, const uint32_t *left, size_t left_count, const uint32_t *right, size_t right_count, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
/* output[0, output_capacity) += left * right with the larger factor taken in
   slices of slice_size words, each a product added in at its own offset. */
static TINYPY_NOINLINE tinypy_bool_t __tinypy_operator_long_multiply_slices(tinypy_vm_t *vm, uint32_t *output, size_t output_capacity, const uint32_t *left, size_t left_count, const uint32_t *right, size_t right_count, size_t slice_size, tinypy_error_t **out_error) {
    const uint32_t *smaller = left_count < right_count ? left : right;
    const uint32_t *larger = left_count < right_count ? right : left;
    size_t smaller_count = left_count < right_count ? left_count : right_count;
    size_t larger_count = left_count < right_count ? right_count : left_count;

    for (size_t done = 0U; done < larger_count; done += slice_size) {
        size_t slice_count = larger_count - done < slice_size ? larger_count - done : slice_size;

        if (__tinypy_operator_long_multiply_words(vm, output + done, output_capacity - done, smaller, smaller_count, larger + done, slice_count, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* output[0, output_capacity) += left * right: k_mul splits balanced operands
   past the cutoffs, and squares when left and right are the same words. */
static tinypy_bool_t __tinypy_operator_long_multiply_words(tinypy_vm_t *vm, uint32_t *output, size_t output_capacity, const uint32_t *left, size_t left_count, const uint32_t *right, size_t right_count, tinypy_error_t **out_error) {
    tinypy_bool_t square = left == right && left_count == right_count ? TINYPY_TRUE : TINYPY_FALSE;
    size_t minimum_count = left_count < right_count ? left_count : right_count;
    size_t maximum_count = left_count > right_count ? left_count : right_count;

    if (minimum_count == 0U) {
        return TINYPY_TRUE;
    }
    if (minimum_count < (square != 0 ? TINYPY_LONG_KARATSUBA_SQUARE_CUTOFF : TINYPY_LONG_KARATSUBA_CUTOFF)) {
        /* Below the cutoffs the smaller factor has few words: a product of
           at most TINYPY_LONG_POLL_STEPS word steps polls once for all, a
           longer one polls per slice of as many, as x_mul's SIGCHECK does
           per word of the smaller factor. */
        size_t steps = maximum_count > SIZE_MAX / TINYPY_LONG_KARATSUBA_SQUARE_CUTOFF ? SIZE_MAX : maximum_count * minimum_count;

        if (steps > TINYPY_LONG_POLL_STEPS) {
            tinypy_bool_t sliced = __tinypy_operator_long_multiply_slices(vm, output, output_capacity, left, left_count, right, right_count, TINYPY_LONG_POLL_STEPS / minimum_count, out_error);
            return sliced;
        }
        if (__tinypy_internal_long_poll(vm, steps, out_error) == 0) {
            return TINYPY_FALSE;
        }
        __tinypy_operator_long_multiply_schoolbook(output, output_capacity, left, left_count, right, right_count);
        return TINYPY_TRUE;
    }
    /* k_lopsided_mul: splitting would leave the smaller operand without a
       high half, so the larger one is taken in slices of the smaller one's
       size, each a balanced product. */
    if (minimum_count <= SIZE_MAX / 2U && maximum_count >= minimum_count * 2U) {
        tinypy_bool_t sliced = __tinypy_operator_long_multiply_slices(vm, output, output_capacity, left, left_count, right, right_count, minimum_count, out_error);
        return sliced;
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
    uint32_t *scratch;
    uint32_t *zero_low;
    uint32_t *zero_high;
    uint32_t *left_sum;
    uint32_t *right_sum;
    uint32_t *cross;
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
    scratch = (uint32_t *)__tinypy_internal_vm_allocate_checked(vm, scratch_capacity * sizeof(*scratch), out_error);
    if (scratch == NULL) {
        return TINYPY_FALSE;
    }
    zero_low = scratch;
    zero_high = zero_low + zero_low_capacity;
    left_sum = zero_high + zero_high_capacity;
    right_sum = left_sum + left_sum_capacity;
    cross = right_sum + right_sum_capacity;
    (void)memset(scratch, 0, scratch_capacity * sizeof(*scratch));
    left_sum_count = __tinypy_operator_long_add_words(left_sum, left, left_low_count, left + split, left_high_count);
    if (square != 0) {
        right_sum = left_sum;
        right_sum_count = left_sum_count;
    }
    else {
        right_sum_count = __tinypy_operator_long_add_words(right_sum, right, right_low_count, right + split, right_high_count);
    }

    if (__tinypy_operator_long_multiply_words(vm, zero_low, zero_low_capacity, left, left_low_count, right, right_low_count, out_error) == 0
        || __tinypy_operator_long_multiply_words(vm, zero_high, zero_high_capacity, left + split, left_high_count, right + split, right_high_count, out_error) == 0
        || __tinypy_operator_long_multiply_words(vm, cross, cross_capacity, left_sum, left_sum_count, right_sum, right_sum_count, out_error) == 0) {
        tinypy_internal_vm_deallocate(vm, scratch, scratch_capacity * sizeof(*scratch));
        return TINYPY_FALSE;
    }
    zero_low_count = __tinypy_operator_long_trim_words(zero_low, zero_low_capacity);
    zero_high_count = __tinypy_operator_long_trim_words(zero_high, zero_high_capacity);
    cross_count = __tinypy_operator_long_trim_words(cross, cross_capacity);
    cross_count = __tinypy_operator_long_subtract_words(cross, cross_count, zero_low, zero_low_count);
    cross_count = __tinypy_operator_long_subtract_words(cross, cross_count, zero_high, zero_high_count);
    __tinypy_operator_long_add_shifted(output, output_capacity, zero_low, zero_low_count, 0U);
    __tinypy_operator_long_add_shifted(output, output_capacity, cross, cross_count, split);
    __tinypy_operator_long_add_shifted(output, output_capacity, zero_high, zero_high_count, split * 2U);

    tinypy_internal_vm_deallocate(vm, scratch, scratch_capacity * sizeof(*scratch));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* The product of magnitudes of three words or more, multiplied as words. */
static TINYPY_NOINLINE tinypy_value_t *__tinypy_operator_long_multiply_packed(tinypy_vm_t *vm, int32_t sign, const tinypy_integer_view_t *left, const tinypy_integer_view_t *right, tinypy_error_t **out_error) {
    size_t left_count = __tinypy_operator_long_word_count(left->count);
    size_t right_count = __tinypy_operator_long_word_count(right->count);
    tinypy_bool_t square = left->digits == right->digits && left->count == right->count ? TINYPY_TRUE : TINYPY_FALSE;
    size_t product_count = left_count + right_count;
    size_t scratch_count = (square != 0 ? left_count : product_count) + product_count;
    uint32_t local_words[TINYPY_LONG_LOCAL_WORDS];
    uint32_t *scratch = __tinypy_operator_long_allocate_words(vm, local_words, scratch_count, out_error);

    if (scratch == NULL) {
        return NULL;
    }
    uint32_t *left_words = scratch;
    uint32_t *right_words = left_words;
    uint32_t *product = scratch + scratch_count - product_count;

    (void)__tinypy_operator_long_pack_words(left_words, left->digits, left->count);
    if (square == 0) {
        right_words = left_words + left_count;
        (void)__tinypy_operator_long_pack_words(right_words, right->digits, right->count);
    }
    (void)memset(product, 0, product_count * sizeof(*product));
    tinypy_value_t *result = NULL;

    if (__tinypy_operator_long_multiply_words(vm, product, product_count, left_words, left_count, right_words, right_count, out_error) != 0) {
        result = __tinypy_operator_long_from_words(vm, sign, product, product_count, out_error);
    }
    __tinypy_operator_long_release_words(vm, scratch, local_words, scratch_count);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_long_multiply_views(tinypy_vm_t *vm, const tinypy_integer_view_t *left, const tinypy_integer_view_t *right, tinypy_error_t **out_error) {
    if (left->sign == 0 || right->sign == 0) {
        tinypy_value_t *zero = tinypy_internal_long_allocate_digits(vm, 0, 0U, out_error);
        return zero;
    }
    int32_t sign = left->sign == right->sign ? 1 : -1;
    const tinypy_integer_view_t *smaller = left->count <= right->count ? left : right;
    const tinypy_integer_view_t *larger = left->count <= right->count ? right : left;

    if (smaller->count <= 4U) {
        /* A factor of up to two words scales the zeroed digits of the result
           directly: its low word writes them, its high word adds in. */
        size_t capacity = larger->count + smaller->count;
        tinypy_value_t *result = tinypy_internal_long_allocate_digits(vm, sign, capacity, out_error);

        if (result == NULL) {
            return NULL;
        }
        uint16_t *digits = TINYPY_LONG_OBJECT(result)->digits;
        uint64_t factor = __tinypy_operator_view_word(smaller, 0U);
        uint64_t carry = 0U;

        for (size_t index = 0U; index < larger->count; ++index) {
            carry += (uint64_t)larger->digits[index] * factor;
            digits[index] = (uint16_t)(carry & TINYPY_LONG_MASK);
            carry >>= 15U;
        }
        for (size_t index = larger->count; carry != 0U; ++index) {
            digits[index] = (uint16_t)(carry & TINYPY_LONG_MASK);
            carry >>= 15U;
        }
        if (smaller->count > 2U) {
            uint16_t *window = digits + 2U;

            factor = __tinypy_operator_view_word(smaller, 1U);
            for (size_t index = 0U; index < larger->count; ++index) {
                carry += window[index] + (uint64_t)larger->digits[index] * factor;
                window[index] = (uint16_t)(carry & TINYPY_LONG_MASK);
                carry >>= 15U;
            }
            for (size_t index = larger->count; carry != 0U; ++index) {
                carry += window[index];
                window[index] = (uint16_t)(carry & TINYPY_LONG_MASK);
                carry >>= 15U;
            }
        }
        TINYPY_LONG_OBJECT(result)->digit_count = __tinypy_operator_trim_digits(digits, capacity);
        return result;
    }
    tinypy_value_t *result = __tinypy_operator_long_multiply_packed(vm, sign, left, right, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Shifts the words left by shift bits, below 30, into output, which may be
   the words themselves, and returns the bits shifted out of the top word. */
static uint32_t __tinypy_operator_long_shift_words_left(uint32_t *output, const uint32_t *words, size_t count, uint32_t shift) {
    uint32_t carry = 0U;

    for (size_t index = 0U; index != count; ++index) {
        uint64_t shifted = ((uint64_t)words[index] << shift) | carry;

        output[index] = (uint32_t)(shifted & TINYPY_LONG_WORD_MASK);
        carry = (uint32_t)(shifted >> 30U);
    }
    return carry;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_operator_long_shift_words_right(uint32_t *output, const uint32_t *words, size_t count, uint32_t shift) {
    uint32_t mask = (UINT32_C(1) << shift) - 1U;
    uint32_t carry = 0U;

    for (size_t index = count; index != 0U; --index) {
        uint32_t word = words[index - 1U];

        output[index - 1U] = (uint32_t)((((uint64_t)carry << 30U) | word) >> shift);
        carry = word & mask;
    }
}
//////////////////////////////////////////////////////////////////////////
/* The shift that sets the high bit of a nonzero top word, as x_divrem
   normalizes its divisor. */
static uint32_t __tinypy_operator_long_normal_shift(uint32_t top) {
    uint32_t shift = 0U;

    while (top < (TINYPY_LONG_WORD_BASE >> 1U)) {
        top <<= 1U;
        shift += 1U;
    }
    return shift;
}
//////////////////////////////////////////////////////////////////////////
/* The loop of x_divrem for a normalized divisor of two words or more and a
   dividend whose top word is below the divisor's, over its windows from
   offset down to end: each step replaces a window of the dividend by its
   remainder, so the loop down to the first window leaves the remainder in
   the low divisor_count words. quotient, unless NULL, receives the words
   from offset - 1 down to end. */
static inline void __tinypy_operator_long_divide_words(uint32_t *dividend, size_t offset, size_t end, const uint32_t *divisor, size_t divisor_count, uint32_t *quotient) {
    uint32_t divisor_top = divisor[divisor_count - 1U];
    uint32_t divisor_next = divisor[divisor_count - 2U];

    for (; offset != end; --offset) {
        uint32_t *window = dividend + offset - 1U;
        uint32_t window_top = window[divisor_count];
        uint64_t numerator = ((uint64_t)window_top << 30U) | window[divisor_count - 1U];
        uint64_t estimate = numerator / divisor_top;
        uint64_t estimate_remainder = numerator - estimate * divisor_top;

        while (estimate * divisor_next > ((estimate_remainder << 30U) | window[divisor_count - 2U])) {
            estimate -= 1U;
            estimate_remainder += divisor_top;
            if (estimate_remainder >= TINYPY_LONG_WORD_BASE) {
                break;
            }
        }
        uint32_t digit = (uint32_t)estimate;
        uint64_t carry = 0U;

        /* The borrow of each word joins the carry of the product into the
           next one. */
        for (size_t index = 0U; index != divisor_count; ++index) {
            uint64_t product = (uint64_t)digit * divisor[index] + carry;
            uint32_t difference = window[index] - (uint32_t)(product & TINYPY_LONG_WORD_MASK);

            carry = (product >> 30U) + (difference >> 31U);
            window[index] = difference & TINYPY_LONG_WORD_MASK;
        }
        if (window_top < carry) {
            uint32_t sum = 0U;

            for (size_t index = 0U; index != divisor_count; ++index) {
                sum += window[index] + divisor[index];
                window[index] = sum & TINYPY_LONG_WORD_MASK;
                sum >>= 30U;
            }
            digit -= 1U;
        }
        if (quotient != NULL) {
            quotient[offset - 1U] = digit;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
/* The SIGCHECK of x_divrem for a division of TINYPY_LONG_POLL_STEPS word
   steps or more: the host is polled before each run of windows that takes
   as many. */
static TINYPY_NOINLINE tinypy_bool_t __tinypy_operator_long_divide_words_polled(tinypy_vm_t *vm, uint32_t *dividend, size_t offset, const uint32_t *divisor, size_t divisor_count, uint32_t *quotient, tinypy_error_t **out_error) {
    size_t run = TINYPY_LONG_POLL_STEPS / divisor_count + 1U;

    while (offset != 0U) {
        size_t end = offset > run ? offset - run : 0U;

        if (__tinypy_internal_long_poll(vm, (offset - end) * divisor_count, out_error) == 0) {
            return TINYPY_FALSE;
        }
        __tinypy_operator_long_divide_words(dividend, offset, end, divisor, divisor_count, quotient);
        offset = end;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* x_divrem by a divisor of two words or more already shifted left by shift
   bits: the dividend, of at least divisor_count words and with room for one
   more, is shifted alike, divided, and its remainder shifted back into the
   low divisor_count words; out_quotient_count receives the number of
   quotient words. A division of at most TINYPY_LONG_POLL_STEPS word steps
   accounts for them with one poll before its loop. */
static inline tinypy_bool_t __tinypy_operator_long_divide_normalized(tinypy_vm_t *vm, uint32_t *dividend, size_t dividend_count, const uint32_t *divisor, size_t divisor_count, uint32_t shift, uint32_t *quotient, size_t *out_quotient_count, tinypy_error_t **out_error) {
    uint32_t carry = __tinypy_operator_long_shift_words_left(dividend, dividend, dividend_count, shift);

    if (carry != 0U || dividend[dividend_count - 1U] >= divisor[divisor_count - 1U]) {
        dividend[dividend_count] = carry;
        dividend_count += 1U;
    }
    size_t quotient_count = dividend_count - divisor_count;

    if (quotient_count <= TINYPY_LONG_POLL_STEPS / divisor_count) {
        if (__tinypy_internal_long_poll(vm, quotient_count * divisor_count, out_error) == 0) {
            return TINYPY_FALSE;
        }
        __tinypy_operator_long_divide_words(dividend, quotient_count, 0U, divisor, divisor_count, quotient);
    }
    else if (__tinypy_operator_long_divide_words_polled(vm, dividend, quotient_count, divisor, divisor_count, quotient, out_error) == 0) {
        return TINYPY_FALSE;
    }
    __tinypy_operator_long_shift_words_right(dividend, dividend, divisor_count, shift);
    *out_quotient_count = quotient_count;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_long_divide_views(tinypy_vm_t *vm, const tinypy_integer_view_t *left, const tinypy_integer_view_t *right, tinypy_operator_long_division_result_e result_kind, tinypy_error_t **out_error) {
    if (right->sign == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "long division or modulo by zero", out_error);
        return NULL;
    }
    size_t dividend_words = __tinypy_operator_long_word_count(left->count);
    size_t divisor_words = __tinypy_operator_long_word_count(right->count);
    /* The dividend with a spare word, the divisor, then the quotient with a
       spare word for the floor adjustment. */
    size_t scratch_count = dividend_words * 2U + divisor_words + 2U;
    uint32_t local_words[TINYPY_LONG_LOCAL_WORDS];
    uint32_t *scratch = __tinypy_operator_long_allocate_words(vm, local_words, scratch_count, out_error);

    if (scratch == NULL) {
        return NULL;
    }
    uint32_t *dividend = scratch;
    uint32_t *divisor = dividend + dividend_words + 1U;
    uint32_t *quotient = divisor + divisor_words;
    size_t dividend_count = __tinypy_operator_long_pack_words(dividend, left->digits, left->count);
    size_t divisor_count = __tinypy_operator_long_pack_words(divisor, right->digits, right->count);
    int32_t comparison = __tinypy_operator_long_compare_words(dividend, dividend_count, divisor, divisor_count);
    uint32_t *remainder = dividend;
    size_t remainder_count = dividend_count;
    size_t quotient_count = 0U;
    uint32_t shift = 0U;

    if (comparison >= 0 && divisor_count == 1U) {
        uint64_t divisor_word = divisor[0];
        uint64_t rest = 0U;

        for (size_t index = dividend_count; index != 0U; --index) {
            uint64_t current = (rest << 30U) | dividend[index - 1U];

            quotient[index - 1U] = (uint32_t)(current / divisor_word);
            rest = current % divisor_word;
        }
        quotient_count = dividend_count;
        dividend[0] = (uint32_t)rest;
        remainder_count = 1U;
    }
    else if (comparison >= 0) {
        shift = __tinypy_operator_long_normal_shift(divisor[divisor_count - 1U]);
        (void)__tinypy_operator_long_shift_words_left(divisor, divisor, divisor_count, shift);
        if (__tinypy_operator_long_divide_normalized(vm, dividend, dividend_count, divisor, divisor_count, shift, quotient, &quotient_count, out_error) == 0) {
            __tinypy_operator_long_release_words(vm, scratch, local_words, scratch_count);
            return NULL;
        }
        remainder_count = divisor_count;
    }
    quotient_count = __tinypy_operator_long_trim_words(quotient, quotient_count);
    remainder_count = __tinypy_operator_long_trim_words(remainder, remainder_count);
    int32_t quotient_sign = left->sign == right->sign ? 1 : -1;

    if (left->sign != right->sign && remainder_count != 0U) {
        /* Floor division moves the quotient away from zero and gives the
           remainder the divisor's sign. */
        size_t index = 0U;

        quotient[quotient_count] = 0U;
        while (quotient[index] == TINYPY_LONG_WORD_MASK) {
            quotient[index] = 0U;
            index += 1U;
        }
        quotient[index] += 1U;
        quotient_count += 1U;
        __tinypy_operator_long_shift_words_right(divisor, divisor, divisor_count, shift);
        remainder_count = __tinypy_operator_long_subtract_words(divisor, divisor_count, remainder, remainder_count);
        remainder = divisor;
    }
    tinypy_value_t *result;

    if (result_kind == TINYPY_OPERATOR_LONG_DIVISION_PAIR) {
        tinypy_value_t *items[2];

        items[0] = __tinypy_operator_long_from_words(vm, quotient_sign, quotient, quotient_count, out_error);
        if (items[0] == NULL) {
            result = NULL;
        }
        else {
            items[1] = __tinypy_operator_long_from_words(vm, right->sign, remainder, remainder_count, out_error);
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
        result = __tinypy_operator_long_from_words(vm, right->sign, remainder, remainder_count, out_error);
    }
    else {
        result = __tinypy_operator_long_from_words(vm, quotient_sign, quotient, quotient_count, out_error);
    }
    __tinypy_operator_long_release_words(vm, scratch, local_words, scratch_count);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Bits of a magnitude from position up, within one of its 15-bit digits. */
static uint32_t __tinypy_operator_view_bits(const tinypy_integer_view_t *view, size_t position, uint32_t width) {
    uint32_t digit = view->digits[position / 15U];
    uint32_t bits = (digit >> (position % 15U)) & ((UINT32_C(1) << width) - 1U);

    return bits;
}
//////////////////////////////////////////////////////////////////////////
/* Reduces the words, with room for one more, in place by the modulus and
   replaces their count by the count of the remainder. */
static tinypy_bool_t __tinypy_operator_long_reduce_words(tinypy_vm_t *vm, const tinypy_operator_long_modulus_t *modulus, uint32_t *words, size_t *in_out_count, tinypy_error_t **out_error) {
    size_t count = *in_out_count;

    if (count < modulus->count) {
        return TINYPY_TRUE;
    }
    if (modulus->count == 1U) {
        uint64_t rest = 0U;

        for (size_t index = count; index != 0U; --index) {
            rest = ((rest << 30U) | words[index - 1U]) % modulus->words[0];
        }
        words[0] = (uint32_t)rest;
        *in_out_count = rest != 0U ? 1U : 0U;
        return TINYPY_TRUE;
    }
    size_t quotient_count;
    if (__tinypy_operator_long_divide_normalized(vm, words, count, modulus->normalized, modulus->count, modulus->shift, NULL, &quotient_count, out_error) == 0) {
        return TINYPY_FALSE;
    }
    *in_out_count = __tinypy_operator_long_trim_words(words, modulus->count);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* output = left * right reduced by the modulus, as MULT in long_pow: both
   below the modulus, output may be either of them. */
static tinypy_bool_t __tinypy_operator_long_multiply_modulo(tinypy_vm_t *vm, const tinypy_operator_long_modulus_t *modulus, uint32_t *output, size_t *output_count, const uint32_t *left, size_t left_count, const uint32_t *right, size_t right_count, tinypy_error_t **out_error) {
    uint32_t *product = modulus->product;
    size_t product_count = left_count + right_count;

    (void)memset(product, 0, product_count * sizeof(*product));
    if (__tinypy_operator_long_multiply_words(vm, product, product_count, left, left_count, right, right_count, out_error) == 0) {
        return TINYPY_FALSE;
    }
    product_count = __tinypy_operator_long_trim_words(product, product_count);
    if (__tinypy_operator_long_reduce_words(vm, modulus, product, &product_count, out_error) == 0) {
        return TINYPY_FALSE;
    }
    (void)memcpy(output, product, product_count * sizeof(*product));
    *output_count = product_count;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* power = base ** exponent reduced by the modulus, as long_pow computes it:
   zero for a modulus of one, else left-to-right binary exponentiation up to
   the 5-ary cutoff, then 5-bit windows over the table of the base's powers
   1 to 31. */
static tinypy_bool_t __tinypy_operator_long_power_words(tinypy_vm_t *vm, const tinypy_operator_long_modulus_t *modulus, uint32_t *power, size_t *power_count, const uint32_t *base, size_t base_count, uint32_t *table, const tinypy_integer_view_t *exponent, tinypy_error_t **out_error) {
    size_t exponent_bits = __tinypy_operator_view_bit_length(exponent);

    if (modulus->count == 1U && modulus->words[0] == 1U) {
        *power_count = 0U;
        return TINYPY_TRUE;
    }
    if (exponent_bits == 0U) {
        power[0] = 1U;
        *power_count = 1U;
        return TINYPY_TRUE;
    }
    if (exponent_bits <= TINYPY_LONG_FIVEARY_CUTOFF) {
        (void)memcpy(power, base, base_count * sizeof(*power));
        *power_count = base_count;
        for (size_t position = exponent_bits - 1U; position != 0U; --position) {
            if (__tinypy_operator_long_multiply_modulo(vm, modulus, power, power_count, power, *power_count, power, *power_count, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (__tinypy_operator_view_bits(exponent, position - 1U, 1U) == 0U) {
                continue;
            }
            if (__tinypy_operator_long_multiply_modulo(vm, modulus, power, power_count, power, *power_count, base, base_count, out_error) == 0) {
                return TINYPY_FALSE;
            }
        }
        return TINYPY_TRUE;
    }
    size_t table_counts[32];

    (void)memcpy(table, base, base_count * sizeof(*table));
    table_counts[1] = base_count;
    for (size_t index = 2U; index != 32U; ++index) {
        uint32_t *previous = table + (index - 2U) * modulus->count;

        if (__tinypy_operator_long_multiply_modulo(vm, modulus, previous + modulus->count, &table_counts[index], previous, table_counts[index - 1U], base, base_count, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    size_t window = (exponent_bits + 4U) / 5U - 1U;
    uint32_t index = __tinypy_operator_view_bits(exponent, window * 5U, 5U);

    (void)memcpy(power, table + (index - 1U) * modulus->count, table_counts[index] * sizeof(*power));
    *power_count = table_counts[index];
    for (; window != 0U; --window) {
        for (size_t step = 0U; step != 5U; ++step) {
            if (__tinypy_operator_long_multiply_modulo(vm, modulus, power, power_count, power, *power_count, power, *power_count, out_error) == 0) {
                return TINYPY_FALSE;
            }
        }
        index = __tinypy_operator_view_bits(exponent, (window - 1U) * 5U, 5U);
        if (index == 0U) {
            continue;
        }
        if (__tinypy_operator_long_multiply_modulo(vm, modulus, power, power_count, power, *power_count, table + (index - 1U) * modulus->count, table_counts[index], out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* pow(base, exponent, modulus) of integers past int_pow, as long_pow: the
   base is reduced by the modulus magnitude and every product reduced in
   place by the normalized modulus; a negative modulus moves a nonzero
   result below zero. */
static tinypy_value_t *__tinypy_operator_long_power_modulo(tinypy_vm_t *vm, const tinypy_integer_view_t *base, const tinypy_integer_view_t *exponent, const tinypy_integer_view_t *modulus, tinypy_error_t **out_error) {
    size_t count = __tinypy_operator_long_word_count(modulus->count);
    size_t base_word_count = __tinypy_operator_long_word_count(base->count);
    size_t base_capacity = base_word_count >= count ? base_word_count + 1U : count;
    size_t table_count = __tinypy_operator_view_bit_length(exponent) > TINYPY_LONG_FIVEARY_CUTOFF ? 31U : 0U;
    size_t buffer_count = 5U + table_count;

    /* The modulus, its normalized copy, the product with a spare word, the
       power, the table and the base. */
    if (count > (SIZE_MAX - base_capacity - 1U) / buffer_count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "long int temporary storage is too large", out_error);
        return NULL;
    }
    size_t scratch_count = count * buffer_count + 1U + base_capacity;
    uint32_t local_words[TINYPY_LONG_LOCAL_WORDS];
    uint32_t *scratch = __tinypy_operator_long_allocate_words(vm, local_words, scratch_count, out_error);

    if (scratch == NULL) {
        return NULL;
    }
    uint32_t *modulus_words = scratch;
    uint32_t *normalized = modulus_words + count;
    uint32_t *product = normalized + count;
    uint32_t *power = product + count * 2U + 1U;
    uint32_t *table = power + count;
    uint32_t *reduced_base = table + count * table_count;
    tinypy_operator_long_modulus_t reduction;

    (void)__tinypy_operator_long_pack_words(modulus_words, modulus->digits, modulus->count);
    reduction.shift = __tinypy_operator_long_normal_shift(modulus_words[count - 1U]);
    (void)__tinypy_operator_long_shift_words_left(normalized, modulus_words, count, reduction.shift);
    reduction.words = modulus_words;
    reduction.normalized = normalized;
    reduction.product = product;
    reduction.count = count;
    size_t base_count = __tinypy_operator_long_pack_words(reduced_base, base->digits, base->count);

    if (__tinypy_operator_long_reduce_words(vm, &reduction, reduced_base, &base_count, out_error) == 0) {
        __tinypy_operator_long_release_words(vm, scratch, local_words, scratch_count);
        return NULL;
    }
    if (base->sign < 0 && base_count != 0U) {
        (void)memcpy(product, modulus_words, count * sizeof(*product));
        base_count = __tinypy_operator_long_subtract_words(product, count, reduced_base, base_count);
        (void)memcpy(reduced_base, product, base_count * sizeof(*product));
    }
    size_t power_count = 0U;
    if (__tinypy_operator_long_power_words(vm, &reduction, power, &power_count, reduced_base, base_count, table, exponent, out_error) == 0) {
        __tinypy_operator_long_release_words(vm, scratch, local_words, scratch_count);
        return NULL;
    }
    tinypy_value_t *result;

    if (modulus->sign < 0 && power_count != 0U) {
        size_t negative_count = __tinypy_operator_long_subtract_words(modulus_words, count, power, power_count);

        result = __tinypy_operator_long_from_words(vm, -1, modulus_words, negative_count, out_error);
    }
    else {
        result = __tinypy_operator_long_from_words(vm, 1, power, power_count, out_error);
    }
    __tinypy_operator_long_release_words(vm, scratch, local_words, scratch_count);
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
/* float_divmod: the floored quotient, or the remainder taking the divisor's
   sign, of a nonzero divisor. */
static double __tinypy_operator_float_floor_division(double dividend, double divisor, tinypy_bool_t remainder_only) {
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
    return remainder_only != 0 ? remainder : quotient;
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
    tinypy_bool_t return_value_1 = tinypy_long_as_double(value, out_value, out_error);
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
/* The complex arithmetic follows c_prod, c_quot, c_powu, c_powi and c_pow of
   Python 2.7's complexobject.c operation for operation, so overflows and
   NaNs come out as they do there. */
static void __tinypy_operator_complex_product(double left_real, double left_imaginary, double right_real, double right_imaginary, double *result_real, double *result_imaginary) {
    *result_real = left_real * right_real - left_imaginary * right_imaginary;
    *result_imaginary = left_real * right_imaginary + left_imaginary * right_real;
}
//////////////////////////////////////////////////////////////////////////
/* c_powi of an exponent up to 100 in magnitude; a negative power divides
   one by the positive one, which fails for a zero power. */
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
/* complex_pow: c_powi for an integral exponent up to 100 in magnitude and
   c_pow otherwise, where a zero base to a negative or complex power is a
   zero division. Any infinite component of the result is an overflow. */
static tinypy_operator_complex_power_result_e __tinypy_operator_complex_power(double left_real, double left_imaginary, double right_real, double right_imaginary, double *result_real, double *result_imaginary) {
    if (right_imaginary == 0.0 && right_real >= -100.0 && right_real <= 100.0 && right_real == trunc(right_real)) {
        if (__tinypy_operator_complex_integer_power(left_real, left_imaginary, (int32_t)right_real, result_real, result_imaginary) == 0) {
            return TINYPY_OPERATOR_COMPLEX_POWER_ZERO_DIVISION;
        }
    }
    else if (left_real == 0.0 && left_imaginary == 0.0) {
        if (right_imaginary != 0.0 || right_real < 0.0) {
            return TINYPY_OPERATOR_COMPLEX_POWER_ZERO_DIVISION;
        }
        *result_real = 0.0;
        *result_imaginary = 0.0;
    }
    else {
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
    if (isinf(*result_real) != 0 || isinf(*result_imaginary) != 0) {
        return TINYPY_OPERATOR_COMPLEX_POWER_OVERFLOW;
    }
    return TINYPY_OPERATOR_COMPLEX_POWER_OK;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_numeric_add(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, int32_t subtract, tinypy_error_t **out_error) {
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        tinypy_value_t *unsupported = __tinypy_operator_unsupported(vm, left, right, subtract != 0 ? "-" : "+", out_error);
        return unsupported;
    }
    if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
        double left_real, left_imaginary, right_real, right_imaginary;
        if (__tinypy_operator_as_complex(left, &left_real, &left_imaginary, out_error) == 0 || __tinypy_operator_as_complex(right, &right_real, &right_imaginary, out_error) == 0) {
            return NULL;
        }
        /* Like c_diff, a difference subtracts rather than adds the negated
           operand, which would flip the sign of a NaN. Compilers may merge a
           difference and a sum that share their continuation into such an
           addition; storing the difference keeps the subtraction. */
        if (subtract != 0) {
            volatile double real_difference = left_real - right_real;
            volatile double imaginary_difference = left_imaginary - right_imaginary;
            tinypy_value_t *difference = tinypy_complex_from_doubles(vm, real_difference, imaginary_difference);
            return difference;
        }
        tinypy_value_t *return_value_1 = tinypy_complex_from_doubles(vm, left_real + right_real, left_imaginary + right_imaginary);
        return return_value_1;
    }
    if (left_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_FLOAT) {
        double operator_as_double;
        double right_value;

        if (__tinypy_operator_as_double(left, &operator_as_double, out_error) == 0 || __tinypy_operator_as_double(right, &right_value, out_error) == 0) {
            return NULL;
        }
        /* Like float_sub, a difference subtracts, stored like the complex
           one. */
        if (subtract != 0) {
            volatile double float_difference = operator_as_double - right_value;
            tinypy_value_t *difference = tinypy_float_from_double(vm, float_difference);
            return difference;
        }
        tinypy_value_t *return_value_2 = tinypy_float_from_double(vm, operator_as_double + right_value);
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
    tinypy_value_t *unsupported = __tinypy_operator_unsupported(vm, left, right, subtract != 0 ? "-" : "+", out_error);
    return unsupported;
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
        tinypy_value_t *string = TINYPY_VALUE_KIND(left) == TINYPY_VALUE_STRING ? left : right;

        if (__tinypy_internal_text_needs_decode(string) != 0) {
            tinypy_value_t *decoded = tinypy_internal_text_to_unicode(vm, string, out_error);

            if (decoded == NULL) {
                return NULL;
            }
            tinypy_value_t *result = string == left
                ? __tinypy_operator_concat_text(vm, decoded, right, TINYPY_TRUE, out_error)
                : __tinypy_operator_concat_text(vm, left, decoded, TINYPY_TRUE, out_error);

            TINYPY_DECREF(decoded);
            return result;
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
        tinypy_value_t *return_value_1 = unicode != 0 ? tinypy_unicode_from_utf8(vm, "", 0U) : TINYPY_RET_EMPTY_STRING(vm);
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
static tinypy_value_t *__tinypy_operator_integer_from_digits(tinypy_vm_t *vm, int32_t sign, uint16_t *digits, size_t count, tinypy_bool_t prefer_long, tinypy_error_t **out_error) {
    int64_t integer;

    count = __tinypy_operator_trim_digits(digits, count);
    if (count == 0U) {
        sign = 0;
    }
    if (prefer_long == 0 && tinypy_internal_long_digits_as_i64(sign, digits, count, &integer) != 0) {
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
        (void)__tinypy_operator_unsupported(vm, left, right, operation == 0 ? "&" : (operation == 1 ? "^" : "|"), out_error);
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
    scratch = (uint16_t *)__tinypy_internal_vm_allocate_checked(vm, width * 2U * sizeof(*scratch), out_error);
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
/* The count converts to an int64 on every host, as to Py_ssize_t on 64-bit
   CPython, so that the outcome of a shift does not depend on size_t. */
static tinypy_bool_t __tinypy_operator_shift_count(tinypy_vm_t *vm, tinypy_value_t *value, uint64_t *out_shift, tinypy_error_t **out_error) {
    tinypy_integer_view_t view;
    uint64_t shift = 0U;
    uint64_t limit;
    size_t index;

    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    if (__tinypy_operator_is_integer(kind) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "shift count must be an integer", out_error);
        return TINYPY_FALSE;
    }
    __tinypy_operator_integer_view(value, &view);
    limit = view.sign < 0 ? (uint64_t)INT64_MAX + 1U : (uint64_t)INT64_MAX;
    for (index = view.count; index != 0U; index -= 1U) {
        if (shift > (limit >> 15U)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "long int too large to convert to int", out_error);
            return TINYPY_FALSE;
        }
        shift = (shift << 15U) | view.digits[index - 1U];
        if (shift > limit) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "long int too large to convert to int", out_error);
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
/* A result with more digits than the host can address is a MemoryError, as
   _PyLong_New reports it. */
static tinypy_value_t *__tinypy_operator_integer_left_shift(tinypy_vm_t *vm, tinypy_value_t *left, uint64_t shift, tinypy_bool_t prefer_long, tinypy_error_t **out_error) {
    tinypy_integer_view_t view;
    size_t bit_shift = (size_t)(shift % 15U);
    size_t digit_shift;
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
    if (view.count >= (size_t)PTRDIFF_MAX || shift / 15U > (uint64_t)((size_t)PTRDIFF_MAX - view.count - 1U)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "left shift result is too large", out_error);
        return NULL;
    }
    digit_shift = (size_t)(shift / 15U);
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
static tinypy_value_t *__tinypy_operator_integer_right_shift(tinypy_vm_t *vm, tinypy_value_t *left, uint64_t shift, tinypy_bool_t prefer_long, tinypy_error_t **out_error) {
    tinypy_integer_view_t view;
    size_t bit_shift = (size_t)(shift % 15U);
    size_t digit_shift;
    size_t capacity;
    uint16_t *digits;
    int32_t discarded = 0;
    size_t index;
    __tinypy_operator_integer_view(left, &view);
    if (view.sign == 0) {
        tinypy_value_t *return_value_1 = prefer_long != 0 ? tinypy_long_from_base15_digits(vm, 0, NULL, 0U) : tinypy_integer_from_i64(vm, 0);
        return return_value_1;
    }
    if (shift / 15U >= (uint64_t)view.count) {
        tinypy_value_t *return_value_2 = prefer_long != 0 ? tinypy_long_from_i64(vm, view.sign < 0 ? -1 : 0) : tinypy_integer_from_i64(vm, view.sign < 0 ? -1 : 0);
        return return_value_2;
    }
    digit_shift = (size_t)(shift / 15U);
    if (view.count - digit_shift == SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "right shift temporary storage is too large", out_error);
        return NULL;
    }
    capacity = view.count - digit_shift + 1U;
    if (capacity > SIZE_MAX / sizeof(*digits)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "right shift temporary storage is too large", out_error);
        return NULL;
    }
    digits = (uint16_t *)__tinypy_internal_vm_allocate_checked(vm, capacity * sizeof(*digits), out_error);
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
/* An exponent beyond size_t takes a base of magnitude two or more past any
   memory the host can address. */
static tinypy_bool_t __tinypy_operator_integer_exponent(tinypy_vm_t *vm, tinypy_value_t *value, size_t *out_exponent, tinypy_error_t **out_error) {
    tinypy_integer_view_t view;
    size_t exponent = 0U;
    size_t index;

    __tinypy_operator_integer_view(value, &view);
    for (index = view.count; index != 0U; index -= 1U) {
        if (exponent > (SIZE_MAX >> 15U)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "exponent is too large", out_error);
            return TINYPY_FALSE;
        }
        exponent = (exponent << 15U) | view.digits[index - 1U];
    }
    *out_exponent = exponent;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_call_unary_special(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_value_t *method = tinypy_internal_object_get_special_key(value, name, out_error);

    if (method == NULL) {
        return NULL;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *args = TINYPY_RET_EMPTY_TUPLE(vm);
    tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* instance_neg and friends look the method up like any attribute, so a
   missing one is the instance's AttributeError. */
static tinypy_value_t *__tinypy_operator_call_classic_unary(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_value_t *method = tinypy_internal_object_get_attr_key(value, name, out_error);

    if (method == NULL) {
        return NULL;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *args = TINYPY_RET_EMPTY_TUPLE(vm);
    tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_positive(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind;

    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *classic = __tinypy_operator_call_classic_unary(value, vm->internal_special_pos_key, out_error);
        return classic;
    }
    if (tinypy_internal_object_has_special_override_key(value, vm->internal_special_pos_key) != 0) {
        tinypy_value_t *return_value_1 = __tinypy_operator_call_unary_special(value, vm->internal_special_pos_key, out_error);
        return return_value_1;
    }
    kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_BOOL) {
        tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));
        return return_value_2;
    }
    if (__tinypy_operator_is_integer(kind) != 0 || kind == TINYPY_VALUE_FLOAT || kind == TINYPY_VALUE_COMPLEX) {
        tinypy_value_t *exact = __tinypy_operator_exact_number(vm, value, out_error);
        return exact;
    }
    if (value->type->number_slots != NULL && value->type->number_slots->positive != NULL) {
        tinypy_value_t *return_value_3 = value->type->number_slots->positive(value, out_error);
        return return_value_3;
    }
    if (tinypy_internal_object_has_special_key(value, vm->internal_special_pos_key) != 0) {
        tinypy_value_t *return_value_4 = __tinypy_operator_call_unary_special(value, vm->internal_special_pos_key, out_error);
        return return_value_4;
    }
    tinypy_value_t *failure = __tinypy_operator_bad_unary(vm, value, "unary +", out_error);
    return failure;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_negative(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *classic = __tinypy_operator_call_classic_unary(value, vm->internal_special_neg_key, out_error);
        return classic;
    }
    if (tinypy_internal_object_has_special_override_key(value, vm->internal_special_neg_key) != 0) {
        tinypy_value_t *return_value_1 = __tinypy_operator_call_unary_special(value, vm->internal_special_neg_key, out_error);
        return return_value_1;
    }
    kind = TINYPY_VALUE_KIND(value);
    if (__tinypy_operator_is_number(kind) != 0) {
        tinypy_value_t *negative = tinypy_internal_unary_builtin(value, 1, out_error);
        return negative;
    }
    if (value->type->number_slots != NULL && value->type->number_slots->negative != NULL) {
        tinypy_value_t *return_value_6 = value->type->number_slots->negative(value, out_error);
        return return_value_6;
    }
    if (tinypy_internal_object_has_special_key(value, vm->internal_special_neg_key) != 0) {
        tinypy_value_t *return_value_7 = __tinypy_operator_call_unary_special(value, vm->internal_special_neg_key, out_error);
        return return_value_7;
    }
    tinypy_value_t *failure = __tinypy_operator_bad_unary(vm, value, "unary -", out_error);
    return failure;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_invert(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *classic = __tinypy_operator_call_classic_unary(value, vm->internal_special_invert_key, out_error);
        return classic;
    }
    if (tinypy_internal_object_has_special_override_key(value, vm->internal_special_invert_key) != 0) {
        tinypy_value_t *return_value_1 = __tinypy_operator_call_unary_special(value, vm->internal_special_invert_key, out_error);
        return return_value_1;
    }
    kind = TINYPY_VALUE_KIND(value);
    if (__tinypy_operator_is_integer(kind) != 0) {
        tinypy_value_t *inverted = tinypy_internal_unary_builtin(value, 2, out_error);
        return inverted;
    }
    if (value->type->number_slots != NULL && value->type->number_slots->invert != NULL) {
        tinypy_value_t *return_value_3 = value->type->number_slots->invert(value, out_error);
        return return_value_3;
    }
    if (tinypy_internal_object_has_special_key(value, vm->internal_special_invert_key) != 0) {
        tinypy_value_t *return_value_4 = __tinypy_operator_call_unary_special(value, vm->internal_special_invert_key, out_error);
        return return_value_4;
    }
    tinypy_value_t *failure = __tinypy_operator_bad_unary(vm, value, "unary ~", out_error);
    return failure;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_number_absolute(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *classic = __tinypy_operator_call_classic_unary(value, vm->internal_special_abs_key, out_error);
        return classic;
    }
    if (tinypy_internal_object_has_special_override_key(value, vm->internal_special_abs_key) != 0) {
        tinypy_value_t *special = __tinypy_operator_call_unary_special(value, vm->internal_special_abs_key, out_error);
        return special;
    }
    if (__tinypy_operator_is_number(TINYPY_VALUE_KIND(value)) != 0) {
        tinypy_value_t *absolute = tinypy_internal_unary_builtin(value, 3, out_error);
        return absolute;
    }
    if (value->type->number_slots != NULL && value->type->number_slots->absolute != NULL) {
        tinypy_value_t *native = value->type->number_slots->absolute(value, out_error);
        return native;
    }
    if (tinypy_internal_object_has_special_key(value, vm->internal_special_abs_key) != 0) {
        tinypy_value_t *method = __tinypy_operator_call_unary_special(value, vm->internal_special_abs_key, out_error);
        return method;
    }
    tinypy_value_t *failure = __tinypy_operator_bad_unary(vm, value, "abs()", out_error);
    return failure;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_concat_sequence(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    size_t left_size = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(left) : TINYPY_LIST_SIZE(left);
    size_t right_size = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(right) : TINYPY_LIST_SIZE(right);
    size_t total_size;

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
        tinypy_value_t *tuple = tinypy_internal_tuple_join_items_checked(vm, NULL, tinypy_internal_tuple_items(left), left_size, tinypy_internal_tuple_items(right), right_size, out_error);
        return tuple;
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    if (tinypy_internal_list_reserve_checked(vm, result, total_size, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    /* Both halves fit the reserved storage. */
    tinypy_list_extend(result, TINYPY_LIST_OBJECT(left)->items, left_size);
    tinypy_list_extend(result, TINYPY_LIST_OBJECT(right)->items, right_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_repeat(tinypy_vm_t *vm, tinypy_value_t *sequence, uint64_t count, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(sequence);
    size_t unit_size;
    size_t total_size;

    /* Immutable sequences repeated once are returned as they are, the way
       tuplerepeat and string_repeat short-circuit in Python 2.7. */
    if (count == UINT64_C(1) && sequence->type == &vm->types[kind] && (kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE)) {
        return TINYPY_RET(sequence);
    }

    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        const uint8_t *bytes = TINYPY_TEXT_BYTES(sequence);
        unit_size = TINYPY_TEXT_BYTE_SIZE(sequence);
        /* string_repeat and unicode_repeat of 64-bit CPython: a length past
           Py_ssize_t, or for unicode one whose UCS4 code units and terminator
           a size_t cannot count in bytes, is too long, while a length the host
           cannot hold runs out of memory. */
        uint64_t length = kind == TINYPY_VALUE_STRING ? (uint64_t)unit_size : (uint64_t)TINYPY_SIZED_SIZE(sequence);
        uint64_t longest = kind == TINYPY_VALUE_STRING ? (uint64_t)INT64_MAX : (UINT64_MAX >> 2U) - 1U;
        if (length != 0U && count > longest / length) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated string is too long", out_error);
            return NULL;
        }
        uint64_t total = (uint64_t)unit_size * count;
        if (total >= (uint64_t)PTRDIFF_MAX) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "", out_error);
            return NULL;
        }
        total_size = (size_t)total;
        if (total_size == 0U) {
            tinypy_value_t *return_value_1 = kind == TINYPY_VALUE_STRING ? TINYPY_RET_EMPTY_STRING(vm) : tinypy_unicode_from_utf8(vm, NULL, 0U);
            return return_value_1;
        }
        size_t code_point_count = kind == TINYPY_VALUE_STRING ? total_size : TINYPY_SIZED_SIZE(sequence) * (size_t)count;
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
    if (unit_size != 0U && count > (uint64_t)(SIZE_MAX / unit_size)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "", out_error);
        return NULL;
    }
    total_size = unit_size * (size_t)count;
    if (total_size >= (size_t)PTRDIFF_MAX || total_size > SIZE_MAX / sizeof(tinypy_value_t *)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "", out_error);
        return NULL;
    }
    tinypy_value_t *const *unit = kind == TINYPY_VALUE_TUPLE ? tinypy_internal_tuple_items(sequence) : TINYPY_LIST_OBJECT(sequence)->items;
    if (kind == TINYPY_VALUE_TUPLE) {
        tinypy_value_t *tuple = tinypy_internal_tuple_repeat_checked(vm, unit, unit_size, (size_t)count, out_error);
        return tuple;
    }
    tinypy_value_t *list = tinypy_internal_list_repeat_checked(vm, unit, unit_size, (size_t)count, out_error);
    return list;
}
//////////////////////////////////////////////////////////////////////////
/* The binary operators in the order of the built-in numeric modes. */
typedef enum tinypy_operator_binary_e {
    TINYPY_OPERATOR_BINARY_ADD = 0,
    TINYPY_OPERATOR_BINARY_SUBTRACT = 1,
    TINYPY_OPERATOR_BINARY_MULTIPLY = 2,
    TINYPY_OPERATOR_BINARY_DIVIDE = 3,
    TINYPY_OPERATOR_BINARY_FLOOR_DIVIDE = 4,
    TINYPY_OPERATOR_BINARY_TRUE_DIVIDE = 5,
    TINYPY_OPERATOR_BINARY_REMAINDER = 6,
    TINYPY_OPERATOR_BINARY_DIVMOD = 7,
    TINYPY_OPERATOR_BINARY_POWER = 8,
    TINYPY_OPERATOR_BINARY_LEFT_SHIFT = 9,
    TINYPY_OPERATOR_BINARY_RIGHT_SHIFT = 10,
    TINYPY_OPERATOR_BINARY_BIT_AND = 11,
    TINYPY_OPERATOR_BINARY_BIT_XOR = 12,
    TINYPY_OPERATOR_BINARY_BIT_OR = 13
} tinypy_operator_binary_e;
//////////////////////////////////////////////////////////////////////////
/* What a type contributes to a numeric operator, like its nb_* slot: a
   built-in implementation, a C-API slot, the methods of a dictionary view
   or a set, the slot_nb_* dispatch of Python methods, or instance_* of a
   classic instance. */
typedef enum tinypy_operator_slot_e {
    TINYPY_OPERATOR_SLOT_NONE,
    TINYPY_OPERATOR_SLOT_BUILTIN,
    TINYPY_OPERATOR_SLOT_NATIVE,
    TINYPY_OPERATOR_SLOT_METHOD,
    TINYPY_OPERATOR_SLOT_PYTHON,
    TINYPY_OPERATOR_SLOT_CLASSIC
} tinypy_operator_slot_e;
//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_operator_sequence_slot_e {
    TINYPY_OPERATOR_SEQUENCE_CONCAT,
    TINYPY_OPERATOR_SEQUENCE_INPLACE_CONCAT,
    TINYPY_OPERATOR_SEQUENCE_REPEAT,
    TINYPY_OPERATOR_SEQUENCE_INPLACE_REPEAT
} tinypy_operator_sequence_slot_e;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_operator_binary_spec_t {
    tinypy_operator_binary_e operation;
    tinypy_internal_dispatch_e name;
    tinypy_internal_dispatch_e reverse_name;
    tinypy_internal_dispatch_e inplace_name;
    size_t native_offset;
    size_t native_reflected_offset;
    size_t native_inplace_offset;
    const char *symbol;
    const char *inplace_symbol;
    tinypy_binary_slot_t function;
    tinypy_binary_slot_t inplace_function;
} tinypy_operator_binary_spec_t;
//////////////////////////////////////////////////////////////////////////
#define TINYPY_OPERATOR_NO_DISPATCH TINYPY_INTERNAL_DISPATCH_COUNT
#define TINYPY_OPERATOR_NATIVE_OFFSET(slot) offsetof(tinypy_number_slots_t, slot)
#define TINYPY_OPERATOR_NO_OFFSET SIZE_MAX
//////////////////////////////////////////////////////////////////////////
static const tinypy_operator_binary_spec_t __tinypy_operator_binary_specs[] = {
    {TINYPY_OPERATOR_BINARY_ADD, TINYPY_INTERNAL_DISPATCH_ADD, TINYPY_INTERNAL_DISPATCH_RADD, TINYPY_INTERNAL_DISPATCH_IADD, TINYPY_OPERATOR_NATIVE_OFFSET(add), TINYPY_OPERATOR_NATIVE_OFFSET(reflected_add), TINYPY_OPERATOR_NATIVE_OFFSET(inplace_add), "+", "+=", tinypy_add, tinypy_inplace_add},
    {TINYPY_OPERATOR_BINARY_SUBTRACT, TINYPY_INTERNAL_DISPATCH_SUB, TINYPY_INTERNAL_DISPATCH_RSUB, TINYPY_INTERNAL_DISPATCH_ISUB, TINYPY_OPERATOR_NATIVE_OFFSET(subtract), TINYPY_OPERATOR_NATIVE_OFFSET(reflected_subtract), TINYPY_OPERATOR_NATIVE_OFFSET(inplace_subtract), "-", "-=", tinypy_subtract, tinypy_inplace_subtract},
    {TINYPY_OPERATOR_BINARY_MULTIPLY, TINYPY_INTERNAL_DISPATCH_MUL, TINYPY_INTERNAL_DISPATCH_RMUL, TINYPY_INTERNAL_DISPATCH_IMUL, TINYPY_OPERATOR_NATIVE_OFFSET(multiply), TINYPY_OPERATOR_NATIVE_OFFSET(reflected_multiply), TINYPY_OPERATOR_NATIVE_OFFSET(inplace_multiply), "*", "*=", tinypy_multiply, tinypy_inplace_multiply},
    {TINYPY_OPERATOR_BINARY_DIVIDE, TINYPY_INTERNAL_DISPATCH_DIV, TINYPY_INTERNAL_DISPATCH_RDIV, TINYPY_INTERNAL_DISPATCH_IDIV, TINYPY_OPERATOR_NATIVE_OFFSET(divide), TINYPY_OPERATOR_NATIVE_OFFSET(reflected_divide), TINYPY_OPERATOR_NATIVE_OFFSET(inplace_divide), "/", "/=", tinypy_divide, tinypy_inplace_divide},
    {TINYPY_OPERATOR_BINARY_FLOOR_DIVIDE, TINYPY_INTERNAL_DISPATCH_FLOORDIV, TINYPY_INTERNAL_DISPATCH_RFLOORDIV, TINYPY_INTERNAL_DISPATCH_IFLOORDIV, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, "//", "//=", tinypy_floor_divide, tinypy_inplace_floor_divide},
    {TINYPY_OPERATOR_BINARY_TRUE_DIVIDE, TINYPY_INTERNAL_DISPATCH_TRUEDIV, TINYPY_INTERNAL_DISPATCH_RTRUEDIV, TINYPY_INTERNAL_DISPATCH_ITRUEDIV, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, "/", "/=", tinypy_true_divide, tinypy_inplace_true_divide},
    {TINYPY_OPERATOR_BINARY_REMAINDER, TINYPY_INTERNAL_DISPATCH_MOD, TINYPY_INTERNAL_DISPATCH_RMOD, TINYPY_INTERNAL_DISPATCH_IMOD, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, "%", "%=", tinypy_remainder, tinypy_inplace_remainder},
    {TINYPY_OPERATOR_BINARY_DIVMOD, TINYPY_INTERNAL_DISPATCH_DIVMOD, TINYPY_INTERNAL_DISPATCH_RDIVMOD, TINYPY_OPERATOR_NO_DISPATCH, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, "divmod()", NULL, tinypy_divmod, NULL},
    {TINYPY_OPERATOR_BINARY_POWER, TINYPY_INTERNAL_DISPATCH_POW, TINYPY_INTERNAL_DISPATCH_RPOW, TINYPY_INTERNAL_DISPATCH_IPOW, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, "** or pow()", "** or pow()", tinypy_power, tinypy_inplace_power},
    {TINYPY_OPERATOR_BINARY_LEFT_SHIFT, TINYPY_INTERNAL_DISPATCH_LSHIFT, TINYPY_INTERNAL_DISPATCH_RLSHIFT, TINYPY_INTERNAL_DISPATCH_ILSHIFT, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, "<<", "<<=", tinypy_left_shift, tinypy_inplace_left_shift},
    {TINYPY_OPERATOR_BINARY_RIGHT_SHIFT, TINYPY_INTERNAL_DISPATCH_RSHIFT, TINYPY_INTERNAL_DISPATCH_RRSHIFT, TINYPY_INTERNAL_DISPATCH_IRSHIFT, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, ">>", ">>=", tinypy_right_shift, tinypy_inplace_right_shift},
    {TINYPY_OPERATOR_BINARY_BIT_AND, TINYPY_INTERNAL_DISPATCH_AND, TINYPY_INTERNAL_DISPATCH_RAND, TINYPY_INTERNAL_DISPATCH_IAND, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, "&", "&=", tinypy_bit_and, tinypy_inplace_bit_and},
    {TINYPY_OPERATOR_BINARY_BIT_XOR, TINYPY_INTERNAL_DISPATCH_XOR, TINYPY_INTERNAL_DISPATCH_RXOR, TINYPY_INTERNAL_DISPATCH_IXOR, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, "^", "^=", tinypy_bit_xor, tinypy_inplace_bit_xor},
    {TINYPY_OPERATOR_BINARY_BIT_OR, TINYPY_INTERNAL_DISPATCH_OR, TINYPY_INTERNAL_DISPATCH_ROR, TINYPY_INTERNAL_DISPATCH_IOR, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, TINYPY_OPERATOR_NO_OFFSET, "|", "|=", tinypy_bit_or, tinypy_inplace_bit_or},
};
//////////////////////////////////////////////////////////////////////////
static tinypy_binary_slot_t __tinypy_operator_native_slot(const tinypy_type_t *type, size_t offset) {
    if (type->number_slots == NULL || offset == TINYPY_OPERATOR_NO_OFFSET) {
        return NULL;
    }
    tinypy_binary_slot_t slot = *(const tinypy_binary_slot_t *)((const uint8_t *)type->number_slots + offset);

    return slot;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_is_exact_builtin(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_bool_t exact = (size_t)kind < TINYPY_BUILTIN_TYPE_COUNT && value->type == &vm->types[kind] ? TINYPY_TRUE : TINYPY_FALSE;

    return exact;
}
//////////////////////////////////////////////////////////////////////////
/* The rank of a built-in number: the slot of bool and int, long, float or
   complex accepts operands up to its own rank, the way int_add declines a
   long and float_add a complex. Zero for anything else. */
static int32_t __tinypy_operator_number_rank(tinypy_value_type_e kind) {
    switch (kind) {
    case TINYPY_VALUE_BOOL:
    case TINYPY_VALUE_INTEGER:
        return 1;
    case TINYPY_VALUE_LONG:
        return 2;
    case TINYPY_VALUE_FLOAT:
        return 3;
    case TINYPY_VALUE_COMPLEX:
        return 4;
    default:
        return 0;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_is_set(tinypy_value_type_e kind) {
    tinypy_bool_t result = kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET ? TINYPY_TRUE : TINYPY_FALSE;

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_is_set_operation(tinypy_operator_binary_e operation) {
    tinypy_bool_t result = operation == TINYPY_OPERATOR_BINARY_SUBTRACT || operation >= TINYPY_OPERATOR_BINARY_BIT_AND ? TINYPY_TRUE : TINYPY_FALSE;

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Whether a built-in type implements the operator at all: shifts and bitwise
   operators belong to int and long, str and unicode only format with '%',
   sets only combine. */
static tinypy_bool_t __tinypy_operator_builtin_defines(tinypy_operator_binary_e operation, tinypy_value_type_e kind) {
    int32_t rank = __tinypy_operator_number_rank(kind);

    if (rank != 0) {
        tinypy_bool_t number = operation < TINYPY_OPERATOR_BINARY_LEFT_SHIFT || rank <= 2 ? TINYPY_TRUE : TINYPY_FALSE;

        return number;
    }
    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        tinypy_bool_t text = operation == TINYPY_OPERATOR_BINARY_REMAINDER ? TINYPY_TRUE : TINYPY_FALSE;

        return text;
    }
    tinypy_bool_t set = __tinypy_operator_is_set(kind) != 0 && __tinypy_operator_is_set_operation(operation) != 0 ? TINYPY_TRUE : TINYPY_FALSE;

    return set;
}
//////////////////////////////////////////////////////////////////////////
/* Whether the built-in slot of the owner's type computes the pair instead
   of declining with NotImplemented: string_mod formats a left operand of its
   own kind, set_sub and friends want two sets, and a number converts the
   numbers up to its own rank. */
static tinypy_bool_t __tinypy_operator_builtin_accepts(tinypy_operator_binary_e operation, tinypy_value_type_e owner_kind, tinypy_value_t *left, tinypy_value_t *right) {
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    if (__tinypy_operator_builtin_defines(operation, owner_kind) == 0) {
        return TINYPY_FALSE;
    }
    if (owner_kind == TINYPY_VALUE_STRING || owner_kind == TINYPY_VALUE_UNICODE) {
        tinypy_bool_t text = left_kind == owner_kind ? TINYPY_TRUE : TINYPY_FALSE;

        return text;
    }
    if (__tinypy_operator_is_set(owner_kind) != 0) {
        tinypy_bool_t sets = __tinypy_operator_is_set(left_kind) != 0 && __tinypy_operator_is_set(right_kind) != 0 ? TINYPY_TRUE : TINYPY_FALSE;

        return sets;
    }
    int32_t rank = __tinypy_operator_number_rank(owner_kind);
    int32_t left_rank = __tinypy_operator_number_rank(left_kind);
    int32_t right_rank = __tinypy_operator_number_rank(right_kind);
    tinypy_bool_t numbers = left_rank != 0 && right_rank != 0 && left_rank <= rank && right_rank <= rank ? TINYPY_TRUE : TINYPY_FALSE;

    return numbers;
}
//////////////////////////////////////////////////////////////////////////
/* The slot a value's type contributes to a binary operator. A Python method
   for either side of the operator makes it slot_nb_*, as update_one_slot
   does, whether the type defines or inherits that method. */
static tinypy_operator_slot_e __tinypy_operator_binary_slot(const tinypy_operator_binary_spec_t *spec, tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_OLD_INSTANCE) {
        return TINYPY_OPERATOR_SLOT_CLASSIC;
    }
    if (__tinypy_operator_is_exact_builtin(value) == 0) {
        if (__tinypy_internal_object_overrides_dispatch(value, TINYPY_INTERNAL_DISPATCH_MASK(spec->name) | TINYPY_INTERNAL_DISPATCH_MASK(spec->reverse_name)) != 0) {
            return TINYPY_OPERATOR_SLOT_PYTHON;
        }
        if (__tinypy_operator_native_slot(value->type, spec->native_offset) != NULL || __tinypy_operator_native_slot(value->type, spec->native_reflected_offset) != NULL) {
            return TINYPY_OPERATOR_SLOT_NATIVE;
        }
    }
    if (__tinypy_operator_builtin_defines(spec->operation, kind) != 0) {
        return TINYPY_OPERATOR_SLOT_BUILTIN;
    }
    /* dictviews_sub and friends are the methods of keys and items views. */
    if ((kind == TINYPY_VALUE_DICT_KEYS || kind == TINYPY_VALUE_DICT_ITEMS) && __tinypy_operator_is_set_operation(spec->operation) != 0) {
        return TINYPY_OPERATOR_SLOT_METHOD;
    }
    return TINYPY_OPERATOR_SLOT_NONE;
}
//////////////////////////////////////////////////////////////////////////
/* binary_op1 skips the right operand's slot when it is the left one's. */
static tinypy_bool_t __tinypy_operator_same_slot(const tinypy_operator_binary_spec_t *spec, tinypy_value_t *left, tinypy_operator_slot_e left_slot, tinypy_value_t *right, tinypy_operator_slot_e right_slot) {
    if (left_slot != right_slot) {
        return TINYPY_FALSE;
    }
    if (left_slot == TINYPY_OPERATOR_SLOT_BUILTIN) {
        tinypy_bool_t same_rank = __tinypy_operator_number_rank(TINYPY_VALUE_KIND(left)) == __tinypy_operator_number_rank(TINYPY_VALUE_KIND(right)) ? TINYPY_TRUE : TINYPY_FALSE;

        return same_rank;
    }
    if (left_slot == TINYPY_OPERATOR_SLOT_NATIVE) {
        tinypy_bool_t same_native = __tinypy_operator_native_slot(left->type, spec->native_offset) == __tinypy_operator_native_slot(right->type, spec->native_offset) ? TINYPY_TRUE : TINYPY_FALSE;

        return same_native;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* call_maybe and call_method of typeobject.c: the special method comes from
   the receiver's type; a missing one is NotImplemented, or an AttributeError
   naming it when the slot requires the method. */
static tinypy_value_t *__tinypy_operator_call_type_method(tinypy_value_t *receiver, tinypy_value_t *name, tinypy_value_t *const *arguments, size_t argument_count, tinypy_bool_t required, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(receiver);
    tinypy_bool_t slot;
    tinypy_value_t *attribute = __tinypy_internal_object_special_attribute(vm, receiver, name, &slot);

    if (attribute == NULL) {
        if (required != 0) {
            const tinypy_message_part_t parts[] = {TINYPY_MESSAGE_PART_TEXT(name)};

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_ATTRIBUTE, parts, 1U, out_error);
            return NULL;
        }
        tinypy_value_t *missing = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return missing;
    }
    if (attribute->type == &vm->types[TINYPY_VALUE_FUNCTION]) {
        tinypy_value_t *direct = tinypy_internal_call_type_function(attribute, receiver, arguments, argument_count, out_error);
        return direct;
    }
    tinypy_value_t *method = tinypy_internal_descriptor_get_value(vm, attribute, receiver, receiver->type, out_error);
    if (method == NULL) {
        return NULL;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, arguments, argument_count);
    tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The sequence slots of bytearray and buffer are methods of their types,
   which a subclass keeps as its C slot whatever update_one_slot passed over
   under their names. */
static tinypy_value_t *__tinypy_operator_call_sequence_method(tinypy_value_t *sequence, tinypy_value_t *name, tinypy_value_t *argument, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(sequence);
    tinypy_value_t *method = tinypy_internal_type_lookup_key(vm, &vm->types[TINYPY_VALUE_KIND(sequence)], name);
    tinypy_value_t *result = tinypy_internal_native_function_call_method(method, sequence, &argument, 1U, NULL, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Calls a method found on a classic instance with the instance's own
   attribute lookup; AttributeError means NotImplemented. */
static tinypy_value_t *__tinypy_operator_call_classic_method(tinypy_value_t *instance, tinypy_value_t *name, tinypy_value_t *const *arguments, size_t argument_count, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);

    tinypy_value_t *method;
    int32_t status = tinypy_internal_object_get_optional_attr_key(instance, name, &method, out_error);
    if (status < 0) {
        return NULL;
    }
    if (status == 0) {
        tinypy_value_t *missing = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return missing;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, arguments, argument_count);
    tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* method_is_overloaded: the right operand's type defines a reflected method
   that differs from the left operand type's one. */
static tinypy_bool_t __tinypy_operator_reflected_overloaded(tinypy_value_t *left, tinypy_value_t *right, tinypy_value_t *reverse_name) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_internal_exception_state_t state;
    tinypy_error_t *error = NULL;
    tinypy_value_t *left_method = NULL;
    tinypy_value_t *right_method;
    tinypy_bool_t overloaded = TINYPY_FALSE;

    tinypy_internal_exception_preserve_begin(vm, &state);
    tinypy_value_t *type = TINYPY_RET(&right->type->base.base);
    right_method = tinypy_object_get_attr_value(type, reverse_name, &error);
    TINYPY_DECREF(type);
    if (right_method == NULL) {
        goto cleanup;
    }
    type = TINYPY_RET(&left->type->base.base);
    left_method = tinypy_object_get_attr_value(type, reverse_name, &error);
    TINYPY_DECREF(type);
    if (left_method == NULL) {
        overloaded = TINYPY_TRUE;
        goto cleanup;
    }
    int32_t different = tinypy_compare_bool(left_method, right_method, TINYPY_COMPARE_NOT_EQUAL, &error);

    overloaded = different > 0 ? TINYPY_TRUE : TINYPY_FALSE;
cleanup:
    if (left_method != NULL) {
        TINYPY_DECREF(left_method);
    }
    if (right_method != NULL) {
        TINYPY_DECREF(right_method);
    }
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_internal_exception_preserve_end(vm, &state);
    return overloaded;
}
//////////////////////////////////////////////////////////////////////////
/* SLOT1BINFULL: the left operand asks its own method first unless the right
   one is a subtype overloading the reflected method; the right operand's
   reflected method follows when its type dispatches to Python too. */
static tinypy_value_t *__tinypy_operator_python_slot(const tinypy_operator_binary_spec_t *spec, tinypy_value_t *left, tinypy_value_t *right, tinypy_operator_slot_e left_slot, tinypy_operator_slot_e right_slot, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *name = tinypy_internal_dispatch_key(vm, spec->name);
    tinypy_value_t *reverse_name = tinypy_internal_dispatch_key(vm, spec->reverse_name);
    tinypy_bool_t try_reflected = left->type != right->type && right_slot == TINYPY_OPERATOR_SLOT_PYTHON ? TINYPY_TRUE : TINYPY_FALSE;

    if (left_slot == TINYPY_OPERATOR_SLOT_PYTHON) {
        if (try_reflected != 0 && tinypy_type_is_subtype(right->type, left->type) != 0 && __tinypy_operator_reflected_overloaded(left, right, reverse_name) != 0) {
            tinypy_value_t *reflected = __tinypy_operator_call_type_method(right, reverse_name, &left, 1U, TINYPY_FALSE, out_error);

            if (reflected == NULL || reflected != &vm->not_implemented_object.base) {
                return reflected;
            }
            TINYPY_DECREF(reflected);
            try_reflected = TINYPY_FALSE;
        }
        tinypy_value_t *direct = __tinypy_operator_call_type_method(left, name, &right, 1U, TINYPY_FALSE, out_error);

        if (direct == NULL || direct != &vm->not_implemented_object.base || left->type == right->type) {
            return direct;
        }
        TINYPY_DECREF(direct);
    }
    if (try_reflected != 0) {
        tinypy_value_t *reflected = __tinypy_operator_call_type_method(right, reverse_name, &left, 1U, TINYPY_FALSE, out_error);
        return reflected;
    }
    tinypy_value_t *result = TINYPY_RET_NOT_IMPLEMENTED(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* half_binop: a classic instance coerces the pair through __coerce__ first;
   a coerced pair no longer led by a classic instance re-runs the operator. */
static tinypy_value_t *__tinypy_operator_classic_half(tinypy_value_t *instance, tinypy_value_t *other, tinypy_value_t *name, tinypy_bool_t swapped, tinypy_binary_slot_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    tinypy_value_t *result;

    if (TINYPY_VALUE_KIND(instance) != TINYPY_VALUE_OLD_INSTANCE) {
        result = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return result;
    }
    tinypy_value_t *coerced = __tinypy_operator_call_classic_method(instance, vm->internal_special_coerce_key, &other, 1U, out_error);
    if (coerced == NULL) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(coerced) == TINYPY_VALUE_NONE || coerced == &vm->not_implemented_object.base) {
        TINYPY_DECREF(coerced);
        result = __tinypy_operator_call_classic_method(instance, name, &other, 1U, out_error);
        return result;
    }
    if (TINYPY_VALUE_KIND(coerced) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(coerced) != 2U) {
        TINYPY_DECREF(coerced);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "coercion should return None or 2-tuple", out_error);
        return NULL;
    }
    tinypy_value_t *first = TINYPY_TUPLE_GET(coerced, 0U);
    tinypy_value_t *second = TINYPY_TUPLE_GET(coerced, 1U);

    if (TINYPY_VALUE_KIND(first) == TINYPY_VALUE_OLD_INSTANCE) {
        result = __tinypy_operator_call_classic_method(first, name, &second, 1U, out_error);
    }
    else if (tinypy_internal_recursion_check(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded after coercion", out_error) == 0) {
        result = NULL;
    }
    else {
        vm->evaluation_depth += 1U;
        result = swapped != 0 ? operation(second, first, out_error) : operation(first, second, out_error);
        vm->evaluation_depth -= 1U;
    }
    TINYPY_DECREF(coerced);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* do_binop: the left classic instance's method, then the right classic
   instance's reflected one. */
static tinypy_value_t *__tinypy_operator_classic_binary(tinypy_value_t *left, tinypy_value_t *right, tinypy_value_t *name, tinypy_value_t *reverse_name, tinypy_binary_slot_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *result = __tinypy_operator_classic_half(left, right, name, TINYPY_FALSE, operation, out_error);

    if (result == NULL || result != &vm->not_implemented_object.base) {
        return result;
    }
    TINYPY_DECREF(result);
    result = __tinypy_operator_classic_half(right, left, reverse_name, TINYPY_TRUE, operation, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* do_binop_inplace: the in-place method of the classic instance, then the
   whole binary protocol. */
static tinypy_value_t *__tinypy_operator_classic_inplace(tinypy_value_t *left, tinypy_value_t *right, tinypy_value_t *inplace_name, tinypy_value_t *name, tinypy_value_t *reverse_name, tinypy_binary_slot_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *result = __tinypy_operator_classic_half(left, right, inplace_name, TINYPY_FALSE, operation, out_error);

    if (result == NULL || result != &vm->not_implemented_object.base) {
        return result;
    }
    TINYPY_DECREF(result);
    result = __tinypy_operator_classic_binary(left, right, name, reverse_name, operation, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Calls the slot one operand's type contributes on the pair, the way
   binary_op1 calls slotv(v, w) or slotw(v, w); left_slot and right_slot
   describe both operands for SLOT1BINFULL. */
static tinypy_value_t *__tinypy_operator_call_slot(const tinypy_operator_binary_spec_t *spec, tinypy_operator_slot_e slot, tinypy_bool_t reflected, tinypy_value_t *left, tinypy_value_t *right, tinypy_operator_slot_e left_slot, tinypy_operator_slot_e right_slot, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *owner = reflected != 0 ? right : left;
    tinypy_value_t *result;

    switch (slot) {
    case TINYPY_OPERATOR_SLOT_BUILTIN:
        if (__tinypy_operator_builtin_accepts(spec->operation, TINYPY_VALUE_KIND(owner), left, right) != 0) {
            result = tinypy_internal_operator_builtin(left, right, (int32_t)spec->operation, out_error);
            return result;
        }
        break;
    case TINYPY_OPERATOR_SLOT_NATIVE: {
        tinypy_binary_slot_t native = __tinypy_operator_native_slot(owner->type, reflected != 0 ? spec->native_reflected_offset : spec->native_offset);

        if (native != NULL) {
            result = reflected != 0 ? native(right, left, out_error) : native(left, right, out_error);
            return result;
        }
        break;
    }
    case TINYPY_OPERATOR_SLOT_METHOD:
        if (reflected != 0) {
            result = __tinypy_operator_call_type_method(right, tinypy_internal_dispatch_key(vm, spec->reverse_name), &left, 1U, TINYPY_FALSE, out_error);
            return result;
        }
        result = __tinypy_operator_call_type_method(left, tinypy_internal_dispatch_key(vm, spec->name), &right, 1U, TINYPY_FALSE, out_error);
        return result;
    case TINYPY_OPERATOR_SLOT_PYTHON:
        result = __tinypy_operator_python_slot(spec, left, right, left_slot, right_slot, out_error);
        return result;
    case TINYPY_OPERATOR_SLOT_CLASSIC:
        result = __tinypy_operator_classic_binary(left, right, tinypy_internal_dispatch_key(vm, spec->name), tinypy_internal_dispatch_key(vm, spec->reverse_name), spec->function, out_error);
        return result;
    case TINYPY_OPERATOR_SLOT_NONE:
        break;
    }
    result = TINYPY_RET_NOT_IMPLEMENTED(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* NEW_STYLE_NUMBER: classes, weak proxies and the built-in numbers, strings
   and sets check the types of their operands themselves; other built-in
   types, exceptions among them, are coerced first. */
static tinypy_bool_t __tinypy_operator_new_style_number(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if ((value->type->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) != 0U || value->type == vm->weak_proxy_type || value->type == vm->callable_weak_proxy_type) {
        return TINYPY_TRUE;
    }
    switch (kind) {
    case TINYPY_VALUE_BOOL:
    case TINYPY_VALUE_INTEGER:
    case TINYPY_VALUE_LONG:
    case TINYPY_VALUE_FLOAT:
    case TINYPY_VALUE_COMPLEX:
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_UNICODE:
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
    case TINYPY_VALUE_OLD_INSTANCE:
    case TINYPY_VALUE_DICT_KEYS:
    case TINYPY_VALUE_DICT_ITEMS:
        return TINYPY_TRUE;
    default:
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
/* Replaces the pair with the coerced 2-tuple a __coerce__ method returned;
   the tuple's items become new references. */
static int32_t __tinypy_operator_take_coerced(tinypy_value_t *coerced, tinypy_value_t **receiver, tinypy_value_t **other, tinypy_bool_t swapped, const char *message, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(coerced) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(coerced) != 2U) {
        TINYPY_DECREF(coerced);
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(*receiver), TINYPY_ERROR_TYPE, message, out_error);
        return -INT32_C(1);
    }
    *receiver = TINYPY_RET(TINYPY_TUPLE_GET(coerced, swapped != 0 ? 1U : 0U));
    *other = TINYPY_RET(TINYPY_TUPLE_GET(coerced, swapped != 0 ? 0U : 1U));
    TINYPY_DECREF(coerced);
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
/* int_coerce, long_coerce, float_coerce and complex_coerce: a number takes
   a number of its own rank or lower over, converted to its own kind. */
static int32_t __tinypy_operator_coerce_number(tinypy_value_t **receiver, tinypy_value_t **other, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(*receiver);
    int32_t rank = __tinypy_operator_number_rank(TINYPY_VALUE_KIND(*receiver));
    int32_t other_rank = __tinypy_operator_number_rank(TINYPY_VALUE_KIND(*other));
    tinypy_value_t *converted;

    if (other_rank == 0 || other_rank > rank) {
        return INT32_C(1);
    }
    if (other_rank == rank) {
        converted = TINYPY_RET(*other);
    }
    else if (rank == 2) {
        converted = tinypy_long_from_i64(vm, TINYPY_INTEGER_VALUE(*other));
    }
    else {
        double real;
        double imaginary;

        if (__tinypy_operator_as_complex(*other, &real, &imaginary, out_error) == 0) {
            return -INT32_C(1);
        }
        converted = rank == 3 ? tinypy_float_from_double(vm, real) : tinypy_complex_from_doubles(vm, real, imaginary);
    }
    *receiver = TINYPY_RET(*receiver);
    *other = converted;
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
/* nb_coerce of the receiver's type: instance_coerce of a classic instance,
   slot_nb_coerce of a Python __coerce__ or the built-in number coercion.
   0 replaces both operands with new references, 1 declines. */
static int32_t __tinypy_operator_coerce_slot(tinypy_value_t **receiver, tinypy_value_t **other, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(*receiver);
    tinypy_value_t *coerce_key = vm->internal_special_coerce_key;
    tinypy_value_t *coerced;

    if (TINYPY_VALUE_KIND(*receiver) == TINYPY_VALUE_OLD_INSTANCE) {
        coerced = __tinypy_operator_call_classic_method(*receiver, coerce_key, other, 1U, out_error);
        if (coerced == NULL) {
            return -INT32_C(1);
        }
        if (TINYPY_VALUE_KIND(coerced) == TINYPY_VALUE_NONE || coerced == &vm->not_implemented_object.base) {
            TINYPY_DECREF(coerced);
            return INT32_C(1);
        }
        int32_t classic = __tinypy_operator_take_coerced(coerced, receiver, other, TINYPY_FALSE, "coercion should return None or 2-tuple", out_error);
        return classic;
    }
    if (__tinypy_internal_object_overrides_dispatch(*receiver, TINYPY_INTERNAL_DISPATCH_BIT(COERCE)) != 0) {
        coerced = __tinypy_operator_call_type_method(*receiver, coerce_key, other, 1U, TINYPY_FALSE, out_error);
        if (coerced == NULL) {
            return -INT32_C(1);
        }
        if (coerced != &vm->not_implemented_object.base) {
            int32_t direct = __tinypy_operator_take_coerced(coerced, receiver, other, TINYPY_FALSE, "__coerce__ didn't return a 2-tuple", out_error);
            return direct;
        }
        TINYPY_DECREF(coerced);
        if (TINYPY_VALUE_KIND(*other) == TINYPY_VALUE_OLD_INSTANCE || __tinypy_internal_object_overrides_dispatch(*other, TINYPY_INTERNAL_DISPATCH_BIT(COERCE)) == 0) {
            return INT32_C(1);
        }
        coerced = __tinypy_operator_call_type_method(*other, coerce_key, receiver, 1U, TINYPY_FALSE, out_error);
        if (coerced == NULL) {
            return -INT32_C(1);
        }
        if (coerced == &vm->not_implemented_object.base) {
            TINYPY_DECREF(coerced);
            return INT32_C(1);
        }
        int32_t reflected = __tinypy_operator_take_coerced(coerced, receiver, other, TINYPY_TRUE, "__coerce__ didn't return a 2-tuple", out_error);
        return reflected;
    }
    if (__tinypy_operator_number_rank(TINYPY_VALUE_KIND(*receiver)) != 0) {
        int32_t number = __tinypy_operator_coerce_number(receiver, other, out_error);
        return number;
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
/* PyNumber_CoerceEx: 0 replaces both operands with new references, 1 leaves
   them alone, -1 reports an error. */
int32_t tinypy_internal_number_coerce(tinypy_value_t **left, tinypy_value_t **right, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    if ((*left)->type == (*right)->type && __tinypy_operator_new_style_number(*left) == 0) {
        *left = TINYPY_RET(*left);
        *right = TINYPY_RET(*right);
        return INT32_C(0);
    }
    int32_t status = __tinypy_operator_coerce_slot(left, right, out_error);
    if (status <= 0) {
        return status;
    }
    status = __tinypy_operator_coerce_slot(right, left, out_error);
    return status;
}
//////////////////////////////////////////////////////////////////////////
/* binary_op1 coerces a pair holding an operand that does not check types
   itself and lets the coerced left operand's slot have the last word. */
static tinypy_value_t *__tinypy_operator_coerced_binary(const tinypy_operator_binary_spec_t *spec, tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *first = left;
    tinypy_value_t *second = right;

    if (__tinypy_operator_new_style_number(left) != 0 && __tinypy_operator_new_style_number(right) != 0) {
        tinypy_value_t *skipped = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return skipped;
    }
    int32_t status = tinypy_internal_number_coerce(&first, &second, out_error);
    if (status < 0) {
        return NULL;
    }
    if (status > 0) {
        tinypy_value_t *declined = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return declined;
    }
    tinypy_operator_slot_e first_slot = __tinypy_operator_binary_slot(spec, first);
    tinypy_operator_slot_e second_slot = first->type != second->type ? __tinypy_operator_binary_slot(spec, second) : TINYPY_OPERATOR_SLOT_NONE;
    tinypy_value_t *result = __tinypy_operator_call_slot(spec, first_slot, TINYPY_FALSE, first, second, first_slot, second_slot, out_error);
    TINYPY_DECREF(second);
    TINYPY_DECREF(first);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* binary_op1 of Python 2.7: the left operand's slot runs first unless the
   right operand is a subtype contributing a different slot. */
static tinypy_value_t *__tinypy_operator_binary_op1(const tinypy_operator_binary_spec_t *spec, tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *result;

    /* Exact built-in slots have no side effects, so their order is moot. */
    if (__tinypy_operator_is_exact_builtin(left) != 0 && __tinypy_operator_is_exact_builtin(right) != 0 &&
        __tinypy_operator_builtin_defines(spec->operation, TINYPY_VALUE_KIND(left)) != 0 && __tinypy_operator_builtin_defines(spec->operation, TINYPY_VALUE_KIND(right)) != 0) {
        if (__tinypy_operator_builtin_accepts(spec->operation, TINYPY_VALUE_KIND(left), left, right) != 0 || __tinypy_operator_builtin_accepts(spec->operation, TINYPY_VALUE_KIND(right), left, right) != 0) {
            result = tinypy_internal_operator_builtin(left, right, (int32_t)spec->operation, out_error);
            return result;
        }
        result = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return result;
    }
    tinypy_operator_slot_e left_slot = __tinypy_operator_binary_slot(spec, left);
    tinypy_operator_slot_e right_slot = left->type != right->type ? __tinypy_operator_binary_slot(spec, right) : TINYPY_OPERATOR_SLOT_NONE;
    tinypy_bool_t try_right = right_slot != TINYPY_OPERATOR_SLOT_NONE && __tinypy_operator_same_slot(spec, left, left_slot, right, right_slot) == 0 ? TINYPY_TRUE : TINYPY_FALSE;

    if (left_slot != TINYPY_OPERATOR_SLOT_NONE) {
        if (try_right != 0 && tinypy_type_is_subtype(right->type, left->type) != 0) {
            result = __tinypy_operator_call_slot(spec, right_slot, TINYPY_TRUE, left, right, left_slot, right_slot, out_error);
            if (result == NULL || result != &vm->not_implemented_object.base) {
                return result;
            }
            TINYPY_DECREF(result);
            try_right = TINYPY_FALSE;
        }
        result = __tinypy_operator_call_slot(spec, left_slot, TINYPY_FALSE, left, right, left_slot, right_slot, out_error);
        if (result == NULL || result != &vm->not_implemented_object.base) {
            return result;
        }
        TINYPY_DECREF(result);
    }
    if (try_right != 0) {
        result = __tinypy_operator_call_slot(spec, right_slot, TINYPY_TRUE, left, right, left_slot, right_slot, out_error);
        if (result == NULL || result != &vm->not_implemented_object.base) {
            return result;
        }
        TINYPY_DECREF(result);
    }
    result = __tinypy_operator_coerced_binary(spec, left, right, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Whether a type defines one of the methods wrapping a sequence slot and
   overrides none, once the dispatch memo has to learn it. */
static TINYPY_NOINLINE tinypy_bool_t __tinypy_operator_memoize_sequence_slot(tinypy_value_t *value, uint64_t specials) {
    tinypy_bool_t kept = __tinypy_internal_object_defines_dispatch(value, specials) != 0 && __tinypy_internal_object_overrides_dispatch(value, specials) == 0 ? TINYPY_TRUE : TINYPY_FALSE;

    return kept;
}
//////////////////////////////////////////////////////////////////////////
/* sq_concat, sq_inplace_concat, sq_repeat and sq_inplace_repeat survive in a
   subclass only while it leaves the methods wrapping them alone; a slot
   wrapper of another type in their place leaves the slot empty. */
static TINYPY_ALWAYS_INLINE tinypy_bool_t __tinypy_operator_keeps_sequence_slot(tinypy_value_t *value, tinypy_operator_sequence_slot_e slot) {
    static const uint64_t wrapping_specials[] = {
        [TINYPY_OPERATOR_SEQUENCE_CONCAT] = TINYPY_INTERNAL_DISPATCH_BIT(ADD),
        [TINYPY_OPERATOR_SEQUENCE_INPLACE_CONCAT] = TINYPY_INTERNAL_DISPATCH_BIT(IADD),
        [TINYPY_OPERATOR_SEQUENCE_REPEAT] = TINYPY_INTERNAL_DISPATCH_BIT(MUL) | TINYPY_INTERNAL_DISPATCH_BIT(RMUL),
        [TINYPY_OPERATOR_SEQUENCE_INPLACE_REPEAT] = TINYPY_INTERNAL_DISPATCH_BIT(IMUL),
    };
    uint64_t specials = wrapping_specials[slot];
    const tinypy_type_t *type = value->type;

    if (__tinypy_internal_object_known_dispatch(value, specials) == 0) {
        tinypy_bool_t memoized = __tinypy_operator_memoize_sequence_slot(value, specials);
        return memoized;
    }
    tinypy_bool_t kept = (type->dispatch_defines & specials) != 0U && (type->dispatch_overrides & specials) == 0U ? TINYPY_TRUE : TINYPY_FALSE;
    return kept;
}
//////////////////////////////////////////////////////////////////////////
/* The native instances with sequence slots are arrays. Only Python classes
   derive from array.array, so a host native type needs no MRO walk, and the
   test stays out of line for the built-in sequences. */
static TINYPY_NOINLINE tinypy_bool_t __tinypy_operator_array_has_sequence_slot(tinypy_value_t *value, tinypy_operator_sequence_slot_e slot) {
    if (value->type != TINYPY_VALUE_VM(value)->array_type && ((value->type->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) == 0U || tinypy_internal_array_check(value) == TINYPY_FALSE)) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t kept = __tinypy_operator_keeps_sequence_slot(value, slot);
    return kept;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_operator_has_sequence_slot(tinypy_value_t *value, tinypy_operator_sequence_slot_e slot) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_bool_t inplace = slot == TINYPY_OPERATOR_SEQUENCE_INPLACE_CONCAT || slot == TINYPY_OPERATOR_SEQUENCE_INPLACE_REPEAT ? TINYPY_TRUE : TINYPY_FALSE;

    switch (kind) {
    case TINYPY_VALUE_LIST:
    case TINYPY_VALUE_BYTEARRAY:
        break;
    case TINYPY_VALUE_NATIVE_INSTANCE: {
        tinypy_bool_t array = __tinypy_operator_array_has_sequence_slot(value, slot);
        return array;
    }
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_UNICODE:
    case TINYPY_VALUE_TUPLE:
    case TINYPY_VALUE_BUFFER:
        if (inplace != 0) {
            return TINYPY_FALSE;
        }
        break;
    default:
        return TINYPY_FALSE;
    }
    if (__tinypy_operator_is_exact_builtin(value) != 0) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t kept = __tinypy_operator_keeps_sequence_slot(value, slot);
    return kept;
}
//////////////////////////////////////////////////////////////////////////
/* Whether the type has tp_as_sequence at all, which keeps `x *= sequence`
   from repeating the right operand. */
static tinypy_bool_t __tinypy_operator_has_sequence_protocol(tinypy_value_t *value) {
    if (__tinypy_operator_is_exact_builtin(value) == 0) {
        tinypy_bool_t custom = (value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U || value->type->sequence_slots != NULL ? TINYPY_TRUE : TINYPY_FALSE;

        return custom;
    }
    switch (TINYPY_VALUE_KIND(value)) {
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_UNICODE:
    case TINYPY_VALUE_TUPLE:
    case TINYPY_VALUE_LIST:
    case TINYPY_VALUE_DICT:
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
    case TINYPY_VALUE_XRANGE:
    case TINYPY_VALUE_BUFFER:
    case TINYPY_VALUE_OLD_INSTANCE:
    case TINYPY_VALUE_BYTEARRAY:
    case TINYPY_VALUE_DICT_KEYS:
    case TINYPY_VALUE_DICT_VALUES:
    case TINYPY_VALUE_DICT_ITEMS:
        return TINYPY_TRUE;
    default:
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
/* string_concat, PyUnicode_Concat, list_concat and tuple concatenation:
   an operand they cannot use is a TypeError, never NotImplemented. */
static tinypy_value_t *__tinypy_operator_sequence_concat(tinypy_value_t *sequence, tinypy_value_t *other, tinypy_bool_t inplace, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(sequence);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(sequence);
    tinypy_value_type_e other_kind = TINYPY_VALUE_KIND(other);

    if (kind == TINYPY_VALUE_LIST && inplace != 0) {
        tinypy_value_t *extended = tinypy_internal_list_inplace_concat(sequence, other, out_error);

        return extended;
    }
    /* bytearray and buffer concatenation are their types' own methods. */
    if (kind == TINYPY_VALUE_BYTEARRAY || kind == TINYPY_VALUE_BUFFER) {
        tinypy_value_t *method_name = inplace != 0 ? vm->internal_special_iadd_key : vm->internal_special_add_key;
        tinypy_value_t *native = __tinypy_operator_call_sequence_method(sequence, method_name, other, out_error);

        return native;
    }
    if ((kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) && (other_kind == TINYPY_VALUE_STRING || other_kind == TINYPY_VALUE_UNICODE)) {
        tinypy_bool_t unicode = kind == TINYPY_VALUE_UNICODE || other_kind == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
        tinypy_value_t *text = __tinypy_operator_concat_text(vm, sequence, other, unicode, out_error);

        return text;
    }
    if (kind == TINYPY_VALUE_STRING && other_kind == TINYPY_VALUE_BYTEARRAY) {
        const uint8_t *other_bytes;
        size_t other_size;

        (void)tinypy_internal_bytes_view(other, &other_bytes, &other_size);
        tinypy_value_t *bytes = tinypy_internal_bytearray_concat_bytes(vm, TINYPY_TEXT_BYTES(sequence), TINYPY_TEXT_BYTE_SIZE(sequence), other_bytes, other_size, out_error);
        return bytes;
    }
    /* PyUnicode_FromObject decodes the character buffer of a buffer: the
       default encoding of a unicode it views, with its bounds applied. */
    if (kind == TINYPY_VALUE_UNICODE && other_kind == TINYPY_VALUE_BUFFER) {
        tinypy_value_t *characters = tinypy_internal_buffer_unicode_source(other, out_error);
        if (characters == NULL) {
            return NULL;
        }
        tinypy_value_t *text = __tinypy_operator_concat_text(vm, sequence, characters, TINYPY_TRUE, out_error);
        TINYPY_DECREF(characters);
        return text;
    }
    if ((kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_LIST) && other_kind == kind) {
        tinypy_value_t *items = __tinypy_operator_concat_sequence(vm, sequence, other, out_error);
        return items;
    }
    /* The native instances with sequence slots are arrays. */
    if (kind == TINYPY_VALUE_NATIVE_INSTANCE) {
        tinypy_value_t *concatenated = tinypy_internal_array_concat(sequence, other, inplace, out_error);

        return concatenated;
    }
    if (kind == TINYPY_VALUE_STRING) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("cannot concatenate 'str' and '"),
            TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(other, 200U),
            TINYPY_MESSAGE_PART_LITERAL("' objects"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (kind == TINYPY_VALUE_UNICODE && other_kind == TINYPY_VALUE_BYTEARRAY) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "decoding bytearray is not supported", out_error);
        return NULL;
    }
    if (kind == TINYPY_VALUE_UNICODE) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("coercing to Unicode: need string or buffer, "),
            TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(other, 80U),
            TINYPY_MESSAGE_PART_LITERAL(" found"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    const tinypy_type_t *sequence_type = &vm->types[kind];
    const tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("can only concatenate "),
        {sequence_type->name, sequence_type->name_size},
        TINYPY_MESSAGE_PART_LITERAL(" (not \""),
        TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(other, 200U),
        TINYPY_MESSAGE_PART_LITERAL("\") to "),
        {sequence_type->name, sequence_type->name_size},
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* The sq_concat of str, unicode, list and tuple behind their __add__. */
tinypy_value_t *tinypy_internal_sequence_concat(tinypy_value_t *sequence, tinypy_value_t *other, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *result = __tinypy_operator_sequence_concat(sequence, other, TINYPY_FALSE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* PyNumber_AsSsize_t: an index too large for the platform raises the given
   error naming the original operand's type. */
tinypy_bool_t tinypy_internal_number_as_index(tinypy_value_t *value, tinypy_error_kind_e overflow_kind, int64_t *out_index, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_bool_t fits = TINYPY_TRUE;

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *index = tinypy_internal_index_value(value, out_error);
    if (index == NULL) {
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(index) == TINYPY_VALUE_LONG) {
        fits = tinypy_internal_long_digits_as_i64(TINYPY_LONG_SIGN(index), TINYPY_LONG_OBJECT(index)->digits, TINYPY_LONG_DIGIT_COUNT(index), out_index);
    }
    else {
        *out_index = TINYPY_INTEGER_VALUE(index);
    }
    TINYPY_DECREF(index);
    if (fits == 0) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("cannot fit '"),
            TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(value, 200U),
            TINYPY_MESSAGE_PART_LITERAL("' into an index-sized integer"),
        };

        tinypy_internal_make_vm_error_parts(vm, overflow_kind, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_number_as_index(tinypy_value_t *value, tinypy_error_kind_e overflow_kind, int64_t *out_index, tinypy_error_t **out_error) {
    tinypy_bool_t result = tinypy_internal_number_as_index(value, overflow_kind, out_index, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* sequence_repeat of abstract.c: PyNumber_AsSsize_t with OverflowError for
   an index count, the sequence's own repeat for the result. */
static tinypy_value_t *__tinypy_operator_sequence_repeat(tinypy_value_t *sequence, tinypy_value_t *count_value, tinypy_bool_t inplace, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(sequence);
    tinypy_value_type_e count_kind = TINYPY_VALUE_KIND(count_value);
    tinypy_bool_t index_check = __tinypy_operator_is_integer(count_kind) != 0 || count_kind == TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_object_has_special_key(count_value, vm->internal_special_index_key) != 0 ? TINYPY_TRUE : TINYPY_FALSE;

    if (index_check == 0) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("can't multiply sequence by non-int of type '"),
            TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(count_value, 200U),
            TINYPY_MESSAGE_PART_LITERAL("'"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    int64_t count;
    if (tinypy_internal_number_as_index(count_value, TINYPY_ERROR_OVERFLOW, &count, out_error) == 0) {
        return NULL;
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(sequence);
    if (kind == TINYPY_VALUE_NATIVE_INSTANCE) {
        tinypy_value_t *repeated = tinypy_internal_array_repeat(sequence, count, inplace, out_error);

        return repeated;
    }
    if (inplace != 0 || kind == TINYPY_VALUE_BYTEARRAY || kind == TINYPY_VALUE_BUFFER) {
        tinypy_value_t *integer = tinypy_integer_from_i64(vm, count);
        tinypy_value_t *method_name = inplace != 0 ? vm->internal_special_imul_key : vm->internal_special_mul_key;
        tinypy_value_t *native = __tinypy_operator_call_sequence_method(sequence, method_name, integer, out_error);

        TINYPY_DECREF(integer);
        return native;
    }
    uint64_t repeat_count = count <= 0 ? UINT64_C(0) : (uint64_t)count;
    tinypy_value_t *repeated = __tinypy_operator_repeat(vm, sequence, repeat_count, out_error);
    return repeated;
}
//////////////////////////////////////////////////////////////////////////
/* binary_op and binary_iop of operands whose exact built-in types settle the
   outcome before any other slot could take part: numbers compute in their
   own slots, str and unicode format with '%', and str, unicode and
   tuple concatenate in sq_concat. A list concatenates that way only out of
   place, its in-place concatenation extends it. Any other pair is
   NotImplemented here and takes the whole protocol. */
static tinypy_value_t *__tinypy_operator_exact_binary(const tinypy_operator_binary_spec_t *spec, tinypy_value_t *left, tinypy_value_t *right, tinypy_bool_t inplace, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    const tinypy_type_t *integer_type = &vm->types[TINYPY_VALUE_INTEGER];
    const tinypy_type_t *float_type = &vm->types[TINYPY_VALUE_FLOAT];
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);
    tinypy_operator_binary_e operation = spec->operation;
    tinypy_bool_t remainder = operation == TINYPY_OPERATOR_BINARY_REMAINDER ? TINYPY_TRUE : TINYPY_FALSE;

    /* int_div, int_mod, float_floor_div and float_rem inline the divisions
       that can neither fail nor widen to a long. */
    if (left->type == integer_type && right->type == integer_type && (operation == TINYPY_OPERATOR_BINARY_DIVIDE || operation == TINYPY_OPERATOR_BINARY_FLOOR_DIVIDE || remainder != 0)) {
        int64_t dividend = TINYPY_INTEGER_VALUE(left);
        int64_t divisor = TINYPY_INTEGER_VALUE(right);

        if (divisor != 0 && (divisor != -1 || dividend != INT64_MIN)) {
            int64_t floored = __tinypy_internal_integer_floor_division(dividend, divisor, remainder);
            tinypy_value_t *integer = tinypy_integer_from_i64(vm, floored);
            return integer;
        }
    }
    if (left->type == float_type && right->type == float_type && (operation == TINYPY_OPERATOR_BINARY_FLOOR_DIVIDE || remainder != 0) && TINYPY_FLOAT_OBJECT(right)->value != 0.0) {
        double floored = __tinypy_operator_float_floor_division(TINYPY_FLOAT_OBJECT(left)->value, TINYPY_FLOAT_OBJECT(right)->value, remainder);
        tinypy_value_t *number = tinypy_float_from_double(vm, floored);
        return number;
    }
    if ((left->type == integer_type || left->type == float_type) && (right->type == integer_type || right->type == float_type)) {
        if (operation < TINYPY_OPERATOR_BINARY_LEFT_SHIFT || (left->type == integer_type && right->type == integer_type)) {
            tinypy_value_t *number = tinypy_internal_operator_builtin(left, right, (int32_t)operation, out_error);
            return number;
        }
    }
    else if (__tinypy_operator_is_exact_builtin(left) != 0 && __tinypy_operator_is_exact_builtin(right) != 0) {
        tinypy_bool_t left_text = left_kind == TINYPY_VALUE_STRING || left_kind == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
        tinypy_bool_t right_text = right_kind == TINYPY_VALUE_STRING || right_kind == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;

        /* Of two built-in numbers whose types both implement the operator,
           the higher ranked one takes the pair. */
        if (__tinypy_operator_number_rank(left_kind) != 0 && __tinypy_operator_number_rank(right_kind) != 0 && __tinypy_operator_builtin_defines(operation, left_kind) != 0 && __tinypy_operator_builtin_defines(operation, right_kind) != 0) {
            tinypy_value_t *number = tinypy_internal_operator_builtin(left, right, (int32_t)operation, out_error);
            return number;
        }
        if (remainder != 0 && left_text != 0 && right_kind != TINYPY_VALUE_OLD_INSTANCE) {
            tinypy_value_t *formatted = tinypy_internal_string_percent(left, right, out_error);
            return formatted;
        }
        if (operation == TINYPY_OPERATOR_BINARY_ADD && left_text != 0 && right_text != 0) {
            tinypy_value_t *text = __tinypy_operator_concat_text(vm, left, right, left_kind == TINYPY_VALUE_UNICODE || right_kind == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE, out_error);
            return text;
        }
        if (operation == TINYPY_OPERATOR_BINARY_ADD && left_kind == right_kind && (left_kind == TINYPY_VALUE_TUPLE || (left_kind == TINYPY_VALUE_LIST && inplace == 0))) {
            tinypy_value_t *items = __tinypy_operator_concat_sequence(vm, left, right, out_error);
            return items;
        }
    }
    tinypy_value_t *declined = TINYPY_RET_NOT_IMPLEMENTED(vm);
    return declined;
}
//////////////////////////////////////////////////////////////////////////
/* binary_op: a pair no slot handles falls back to the sequence protocol for
   '+' and '*' and is an unsupported operand error otherwise. */
static tinypy_value_t *__tinypy_operator_binary(const tinypy_operator_binary_spec_t *spec, tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *result = __tinypy_operator_exact_binary(spec, left, right, TINYPY_FALSE, out_error);
    if (result == NULL || result != &vm->not_implemented_object.base) {
        return result;
    }
    TINYPY_DECREF(result);
    result = __tinypy_operator_binary_op1(spec, left, right, out_error);
    if (result == NULL || result != &vm->not_implemented_object.base) {
        return result;
    }
    TINYPY_DECREF(result);
    if (spec->operation == TINYPY_OPERATOR_BINARY_ADD && __tinypy_operator_has_sequence_slot(left, TINYPY_OPERATOR_SEQUENCE_CONCAT) != 0) {
        result = __tinypy_operator_sequence_concat(left, right, TINYPY_FALSE, out_error);
        return result;
    }
    if (spec->operation == TINYPY_OPERATOR_BINARY_MULTIPLY) {
        if (__tinypy_operator_has_sequence_slot(left, TINYPY_OPERATOR_SEQUENCE_REPEAT) != 0) {
            result = __tinypy_operator_sequence_repeat(left, right, TINYPY_FALSE, out_error);
            return result;
        }
        if (__tinypy_operator_has_sequence_slot(right, TINYPY_OPERATOR_SEQUENCE_REPEAT) != 0) {
            result = __tinypy_operator_sequence_repeat(right, left, TINYPY_FALSE, out_error);
            return result;
        }
    }
    result = __tinypy_operator_unsupported(vm, left, right, spec->symbol, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* update_one_slot takes the inherited __iadd__ wrapper of list, bytearray
   and array.array for nb_inplace_add of a subclass that keeps it, so their
   in-place concatenation runs before any binary method there. Only Python
   classes derive from them: the flags rule out host native types, and an
   MRO walk tells an array from another native instance last. */
static TINYPY_ALWAYS_INLINE tinypy_bool_t __tinypy_operator_inherits_inplace_concat(tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if ((value->type->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) == 0U || (kind != TINYPY_VALUE_LIST && kind != TINYPY_VALUE_BYTEARRAY && kind != TINYPY_VALUE_NATIVE_INSTANCE)) {
        return TINYPY_FALSE;
    }
    if (__tinypy_internal_object_defines_dispatch(value, TINYPY_INTERNAL_DISPATCH_BIT(IADD)) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t inherited = kind != TINYPY_VALUE_NATIVE_INSTANCE || tinypy_internal_array_check(value) != TINYPY_FALSE ? TINYPY_TRUE : TINYPY_FALSE;
    return inherited;
}
//////////////////////////////////////////////////////////////////////////
/* The nb_inplace_* slot of the left operand's type: instance_i* of classic
   instances, slot_nb_inplace_* of a Python method, a C-API slot or the
   in-place set methods. */
static tinypy_operator_slot_e __tinypy_operator_inplace_slot(const tinypy_operator_binary_spec_t *spec, tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_OLD_INSTANCE) {
        return TINYPY_OPERATOR_SLOT_CLASSIC;
    }
    if (__tinypy_operator_is_exact_builtin(value) == 0) {
        if (__tinypy_internal_object_overrides_dispatch(value, TINYPY_INTERNAL_DISPATCH_MASK(spec->inplace_name)) != 0) {
            return TINYPY_OPERATOR_SLOT_PYTHON;
        }
        if (__tinypy_operator_native_slot(value->type, spec->native_inplace_offset) != NULL) {
            return TINYPY_OPERATOR_SLOT_NATIVE;
        }
        if (spec->operation == TINYPY_OPERATOR_BINARY_ADD && __tinypy_operator_inherits_inplace_concat(value) != 0) {
            return TINYPY_OPERATOR_SLOT_METHOD;
        }
    }
    if (kind == TINYPY_VALUE_SET && __tinypy_operator_is_set_operation(spec->operation) != 0) {
        return TINYPY_OPERATOR_SLOT_METHOD;
    }
    return TINYPY_OPERATOR_SLOT_NONE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_call_inplace_slot(const tinypy_operator_binary_spec_t *spec, tinypy_operator_slot_e slot, tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *inplace_name = tinypy_internal_dispatch_key(vm, spec->inplace_name);
    tinypy_value_t *result;

    if (slot == TINYPY_OPERATOR_SLOT_CLASSIC) {
        result = __tinypy_operator_classic_inplace(left, right, inplace_name, tinypy_internal_dispatch_key(vm, spec->name), tinypy_internal_dispatch_key(vm, spec->reverse_name), spec->inplace_function, out_error);
        return result;
    }
    if (slot == TINYPY_OPERATOR_SLOT_NATIVE) {
        tinypy_binary_slot_t native = __tinypy_operator_native_slot(left->type, spec->native_inplace_offset);

        result = native(left, right, out_error);
        return result;
    }
    /* The inherited __iadd__ of list stands for list_inplace_concat. */
    if (slot == TINYPY_OPERATOR_SLOT_METHOD && TINYPY_VALUE_KIND(left) == TINYPY_VALUE_LIST) {
        result = tinypy_internal_list_inplace_concat(left, right, out_error);
        return result;
    }
    /* slot_nb_inplace_* requires the method; a set's own one may decline. */
    result = __tinypy_operator_call_type_method(left, inplace_name, &right, 1U, slot == TINYPY_OPERATOR_SLOT_PYTHON ? TINYPY_TRUE : TINYPY_FALSE, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* binary_iop: the left operand's in-place slot, then binary_op1 and the
   in-place sequence protocol for '+=' and '*='. */
static tinypy_value_t *__tinypy_operator_inplace(const tinypy_operator_binary_spec_t *spec, tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *result = __tinypy_operator_exact_binary(spec, left, right, TINYPY_TRUE, out_error);
    if (result == NULL || result != &vm->not_implemented_object.base) {
        return result;
    }
    TINYPY_DECREF(result);
    tinypy_operator_slot_e slot = __tinypy_operator_inplace_slot(spec, left);
    if (slot != TINYPY_OPERATOR_SLOT_NONE) {
        result = __tinypy_operator_call_inplace_slot(spec, slot, left, right, out_error);
        if (result == NULL || result != &vm->not_implemented_object.base) {
            return result;
        }
        TINYPY_DECREF(result);
    }
    result = __tinypy_operator_binary_op1(spec, left, right, out_error);
    if (result == NULL || result != &vm->not_implemented_object.base) {
        return result;
    }
    TINYPY_DECREF(result);
    if (spec->operation == TINYPY_OPERATOR_BINARY_ADD) {
        if (__tinypy_operator_has_sequence_slot(left, TINYPY_OPERATOR_SEQUENCE_INPLACE_CONCAT) != 0) {
            result = __tinypy_operator_sequence_concat(left, right, TINYPY_TRUE, out_error);
            return result;
        }
        if (__tinypy_operator_has_sequence_slot(left, TINYPY_OPERATOR_SEQUENCE_CONCAT) != 0) {
            result = __tinypy_operator_sequence_concat(left, right, TINYPY_FALSE, out_error);
            return result;
        }
    }
    if (spec->operation == TINYPY_OPERATOR_BINARY_MULTIPLY) {
        if (__tinypy_operator_has_sequence_slot(left, TINYPY_OPERATOR_SEQUENCE_INPLACE_REPEAT) != 0) {
            result = __tinypy_operator_sequence_repeat(left, right, TINYPY_TRUE, out_error);
            return result;
        }
        if (__tinypy_operator_has_sequence_slot(left, TINYPY_OPERATOR_SEQUENCE_REPEAT) != 0) {
            result = __tinypy_operator_sequence_repeat(left, right, TINYPY_FALSE, out_error);
            return result;
        }
        if (__tinypy_operator_has_sequence_protocol(left) == 0 && __tinypy_operator_has_sequence_slot(right, TINYPY_OPERATOR_SEQUENCE_REPEAT) != 0) {
            result = __tinypy_operator_sequence_repeat(right, left, TINYPY_FALSE, out_error);
            return result;
        }
    }
    result = __tinypy_operator_unsupported(vm, left, right, spec->inplace_symbol, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_add(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_binary(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_ADD], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_subtract(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_binary(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_SUBTRACT], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_multiply(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_binary(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_MULTIPLY], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_binary(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_DIVIDE], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_floor_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_binary(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_FLOOR_DIVIDE], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_true_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_binary(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_TRUE_DIVIDE], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_remainder(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_binary(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_REMAINDER], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_divmod(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_binary(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_DIVMOD], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_left_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_binary(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_LEFT_SHIFT], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_right_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_binary(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_RIGHT_SHIFT], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bit_and(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_binary(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_BIT_AND], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bit_xor(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_binary(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_BIT_XOR], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bit_or(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_binary(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_BIT_OR], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_add(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_inplace(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_ADD], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_subtract(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_inplace(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_SUBTRACT], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_multiply(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_inplace(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_MULTIPLY], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_inplace(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_DIVIDE], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_floor_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_inplace(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_FLOOR_DIVIDE], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_true_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_inplace(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_TRUE_DIVIDE], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_remainder(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_inplace(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_REMAINDER], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_left_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_inplace(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_LEFT_SHIFT], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_right_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_inplace(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_RIGHT_SHIFT], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_bit_and(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_inplace(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_BIT_AND], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_bit_xor(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_inplace(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_BIT_XOR], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_inplace_bit_or(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_inplace(&__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_BIT_OR], left, right, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* c_quot: Smith's division by the larger component of a divisor its callers
   know is not zero. A NaN component fails both comparisons and gives
   Py_NAN. */
static void __tinypy_operator_complex_quotient(double ar, double ai, double br, double bi, double *out_real, double *out_imaginary) {
    double absolute_real = fabs(br);
    double absolute_imaginary = fabs(bi);

    if (absolute_real >= absolute_imaginary) {
        double ratio = bi / br;
        double denominator = br + bi * ratio;

        *out_real = (ar + ai * ratio) / denominator;
        *out_imaginary = (ai - ar * ratio) / denominator;
    }
    else if (absolute_imaginary >= absolute_real) {
        double ratio = br / bi;
        double denominator = br * ratio + bi;

        *out_real = (ar * ratio + ai) / denominator;
        *out_imaginary = (ai * ratio - ar) / denominator;
    }
    else {
        *out_real = NAN;
        *out_imaginary = NAN;
    }
}
//////////////////////////////////////////////////////////////////////////
static const char *__tinypy_operator_division_symbol(tinypy_operator_division_e division) {
    const char *symbol = division == TINYPY_OPERATOR_DIVISION_FLOOR ? "//" : (division == TINYPY_OPERATOR_DIVISION_REMAINDER ? "%" : "/");

    return symbol;
}
//////////////////////////////////////////////////////////////////////////
/* True division of two integers, correctly rounded the way long_true_divide
   does in Python 2.7: the quotient is computed with two or three extra bits
   plus a sticky bit and rounded half to even before scaling. */
static tinypy_value_t *__tinypy_operator_integer_true_divide(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_integer_view_t dividend;
    tinypy_integer_view_t divisor;
    tinypy_integer_view_t quotient_view;
    tinypy_value_t *shifted = NULL;
    tinypy_value_t *magnitude;
    tinypy_value_t *pair;
    tinypy_value_t *quotient;
    size_t dividend_bits;
    size_t divisor_bits;
    size_t quotient_bits;
    ptrdiff_t diff;
    ptrdiff_t shift;
    ptrdiff_t extra_bits;
    tinypy_bool_t inexact = TINYPY_FALSE;
    tinypy_bool_t negate;
    uint32_t low;
    uint32_t mask;
    double result;
    size_t index;

    __tinypy_operator_integer_view(left, &dividend);
    __tinypy_operator_integer_view(right, &divisor);
    if (divisor.sign == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "division by zero", out_error);
        return NULL;
    }
    negate = (dividend.sign < 0) != (divisor.sign < 0) ? TINYPY_TRUE : TINYPY_FALSE;
    if (dividend.sign == 0) {
        tinypy_value_t *zero = tinypy_float_from_double(vm, negate != 0 ? -0.0 : 0.0);
        return zero;
    }
    dividend_bits = __tinypy_operator_view_bit_length(&dividend);
    divisor_bits = __tinypy_operator_view_bit_length(&divisor);
    if (dividend_bits <= (size_t)DBL_MANT_DIG && divisor_bits <= (size_t)DBL_MANT_DIG) {
        double dividend_value = 0.0;
        double divisor_value = 0.0;

        for (index = dividend.count; index != 0U; --index) {
            dividend_value = dividend_value * 32768.0 + (double)dividend.digits[index - 1U];
        }
        for (index = divisor.count; index != 0U; --index) {
            divisor_value = divisor_value * 32768.0 + (double)divisor.digits[index - 1U];
        }
        result = dividend_value / divisor_value;
        tinypy_value_t *exact = tinypy_float_from_double(vm, negate != 0 ? -result : result);
        return exact;
    }
    diff = (ptrdiff_t)dividend_bits - (ptrdiff_t)divisor_bits;
    if (diff > DBL_MAX_EXP) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "integer division result too large for a float", out_error);
        return NULL;
    }
    if (diff < DBL_MIN_EXP - DBL_MANT_DIG - 1) {
        tinypy_value_t *zero = tinypy_float_from_double(vm, negate != 0 ? -0.0 : 0.0);
        return zero;
    }
    shift = (diff > DBL_MIN_EXP ? diff : DBL_MIN_EXP) - DBL_MANT_DIG - 2;
    magnitude = tinypy_internal_long_from_base15_digits_checked(vm, 1, dividend.digits, dividend.count, out_error);
    if (magnitude == NULL) {
        return NULL;
    }
    if (shift <= 0) {
        shifted = __tinypy_operator_integer_left_shift(vm, magnitude, (size_t)(-shift), TINYPY_TRUE, out_error);
    }
    else {
        size_t dropped = (size_t)shift;
        size_t whole = dropped / 15U;

        for (index = 0U; index < whole && index < dividend.count && inexact == 0; ++index) {
            if (dividend.digits[index] != 0U) {
                inexact = TINYPY_TRUE;
            }
        }
        if (inexact == 0 && whole < dividend.count && (dividend.digits[whole] & (uint16_t)((1U << (dropped % 15U)) - 1U)) != 0U) {
            inexact = TINYPY_TRUE;
        }
        shifted = __tinypy_operator_integer_right_shift(vm, magnitude, dropped, TINYPY_TRUE, out_error);
    }
    TINYPY_DECREF(magnitude);
    if (shifted == NULL) {
        return NULL;
    }
    tinypy_integer_view_t shifted_view;
    tinypy_integer_view_t divisor_magnitude = divisor;

    divisor_magnitude.sign = 1;
    __tinypy_operator_integer_view(shifted, &shifted_view);
    pair = __tinypy_operator_long_divide_views(vm, &shifted_view, &divisor_magnitude, TINYPY_OPERATOR_LONG_DIVISION_PAIR, out_error);
    TINYPY_DECREF(shifted);
    if (pair == NULL) {
        return NULL;
    }
    quotient = TINYPY_TUPLE_GET(pair, 0U);
    __tinypy_operator_integer_view(TINYPY_TUPLE_GET(pair, 1U), &quotient_view);
    if (quotient_view.sign != 0) {
        inexact = TINYPY_TRUE;
    }
    __tinypy_operator_integer_view(quotient, &quotient_view);
    quotient_bits = __tinypy_operator_view_bit_length(&quotient_view);
    extra_bits = ((ptrdiff_t)quotient_bits > DBL_MIN_EXP - shift ? (ptrdiff_t)quotient_bits : DBL_MIN_EXP - shift) - DBL_MANT_DIG;
    mask = UINT32_C(1) << (extra_bits - 1);
    low = (uint32_t)quotient_view.digits[0] | (inexact != 0 ? UINT32_C(1) : UINT32_C(0));
    if ((low & mask) != 0U && (low & (3U * mask - 1U)) != 0U) {
        low += mask;
    }
    low &= ~(mask - 1U);
    result = 0.0;
    for (index = quotient_view.count; index != 1U; --index) {
        result = result * 32768.0 + (double)quotient_view.digits[index - 1U];
    }
    result = result * 32768.0 + (double)low;
    TINYPY_DECREF(pair);
    if (shift + (ptrdiff_t)quotient_bits >= DBL_MAX_EXP && (shift + (ptrdiff_t)quotient_bits > DBL_MAX_EXP || result == ldexp(1.0, (int)quotient_bits))) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "integer division result too large for a float", out_error);
        return NULL;
    }
    result = ldexp(result, (int)shift);
    tinypy_value_t *value = tinypy_float_from_double(vm, negate != 0 ? -result : result);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_operator_division_e division, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    if ((left_kind == TINYPY_VALUE_INTEGER || left_kind == TINYPY_VALUE_BOOL) && (right_kind == TINYPY_VALUE_INTEGER || right_kind == TINYPY_VALUE_BOOL) && division != TINYPY_OPERATOR_DIVISION_TRUE) {
        int64_t dividend = TINYPY_INTEGER_VALUE(left);
        int64_t divisor = TINYPY_INTEGER_VALUE(right);
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
        int64_t floored = __tinypy_internal_integer_floor_division(dividend, divisor, division == TINYPY_OPERATOR_DIVISION_REMAINDER ? TINYPY_TRUE : TINYPY_FALSE);
        tinypy_value_t *return_value_9 = tinypy_integer_from_i64(vm, floored);
        return return_value_9;
    }
    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        tinypy_value_t *unsupported = __tinypy_operator_unsupported(vm, left, right, __tinypy_operator_division_symbol(division), out_error);
        return unsupported;
    }
    if (division == TINYPY_OPERATOR_DIVISION_TRUE && __tinypy_operator_is_integer(left_kind) != 0 && __tinypy_operator_is_integer(right_kind) != 0) {
        tinypy_value_t *quotient = __tinypy_operator_integer_true_divide(vm, left, right, out_error);
        return quotient;
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
            double floored = __tinypy_operator_float_floor_division(dividend, divisor, division == TINYPY_OPERATOR_DIVISION_REMAINDER ? TINYPY_TRUE : TINYPY_FALSE);
            tinypy_value_t *return_value_4 = tinypy_float_from_double(vm, floored);
            return return_value_4;
        }
        tinypy_value_t *return_value_5 = tinypy_float_from_double(vm, dividend / divisor);
        return return_value_5;
    }
    if (__tinypy_operator_is_integer(left_kind) != 0 && __tinypy_operator_is_integer(right_kind) != 0) {
        tinypy_integer_view_t left_view;
        tinypy_integer_view_t right_view;

        __tinypy_operator_integer_view(left, &left_view);
        __tinypy_operator_integer_view(right, &right_view);
        if (division == TINYPY_OPERATOR_DIVISION_REMAINDER && left_kind == TINYPY_VALUE_LONG && __tinypy_operator_remainder_is_dividend(&left_view, &right_view) != 0) {
            return TINYPY_RET(left);
        }
        tinypy_operator_long_division_result_e result_kind = division == TINYPY_OPERATOR_DIVISION_REMAINDER ? TINYPY_OPERATOR_LONG_DIVISION_REMAINDER : TINYPY_OPERATOR_LONG_DIVISION_QUOTIENT;
        tinypy_value_t *return_value_6 = __tinypy_operator_long_divide_views(vm, &left_view, &right_view, result_kind, out_error);
        return return_value_6;
    }
    tinypy_value_t *unsupported = __tinypy_operator_unsupported(vm, left, right, __tinypy_operator_division_symbol(division), out_error);
    return unsupported;
}
//////////////////////////////////////////////////////////////////////////
/* PyErr_SetFromErrno for float_pow: OverflowError for ERANGE, ValueError
   otherwise, both carrying (errno, strerror(errno)). */
static void __tinypy_operator_raise_errno(tinypy_vm_t *vm, int error_number, tinypy_error_t **out_error) {
    const char *text = strerror(error_number);
    tinypy_value_t *items[2];

    items[0] = tinypy_integer_from_i64(vm, error_number);
    items[1] = tinypy_string_from_bytes(vm, text, strlen(text));
    tinypy_value_t *args = tinypy_tuple_from_items(vm, items, 2U);
    TINYPY_DECREF(items[1]);
    TINYPY_DECREF(items[0]);
    tinypy_exception_type_index_e index = error_number == ERANGE ? TINYPY_EXCEPTION_OVERFLOW_ERROR : TINYPY_EXCEPTION_VALUE_ERROR;
    tinypy_internal_exception_raise_args(vm, index, args, out_error);
    TINYPY_DECREF(args);
}
//////////////////////////////////////////////////////////////////////////
/* int_pow of a non-negative exponent, reduced by the modulus unless it is
   zero. A step that overflows hands the operands to long_pow, whose result
   is a long even when the reduced power would fit. */
tinypy_bool_t tinypy_internal_integer_power(int64_t base, int64_t exponent, int64_t modulus, int64_t *out_result) {
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
                return TINYPY_FALSE;
            }
        }
        exponent >>= 1U;
        if (exponent == 0) {
            break;
        }
        int64_t previous = factor;

        factor = (int64_t)((uint64_t)factor * (uint64_t)factor);
        if (previous != 0 && ((factor == INT64_MIN && previous == -1) || factor / previous != previous)) {
            return TINYPY_FALSE;
        }
        if (modulus != 0) {
            if ((result == INT64_MIN || factor == INT64_MIN) && modulus == -1) {
                return TINYPY_FALSE;
            }
            result %= modulus;
            factor %= modulus;
        }
    }
    if (modulus != 0) {
        if (result == INT64_MIN && modulus == -1) {
            return TINYPY_FALSE;
        }
        result = __tinypy_internal_integer_floor_division(result, modulus, TINYPY_TRUE);
    }
    *out_result = result;
    return TINYPY_TRUE;
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
        tinypy_value_t *unsupported = __tinypy_operator_unsupported(vm, left, right, "** or pow()", out_error);
        return unsupported;
    }
    if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
        double left_real;
        double left_imaginary;
        double right_real;
        double right_imaginary;

        if (__tinypy_operator_as_complex(left, &left_real, &left_imaginary, out_error) == 0 || __tinypy_operator_as_complex(right, &right_real, &right_imaginary, out_error) == 0) {
            return NULL;
        }
        double real;
        double imaginary;
        tinypy_operator_complex_power_result_e power_result = __tinypy_operator_complex_power(left_real, left_imaginary, right_real, right_imaginary, &real, &imaginary);

        if (power_result == TINYPY_OPERATOR_COMPLEX_POWER_ZERO_DIVISION) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "0.0 to a negative or complex power", out_error);
            return NULL;
        }
        if (power_result == TINYPY_OPERATOR_COMPLEX_POWER_OVERFLOW) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "complex exponentiation", out_error);
            return NULL;
        }
        tinypy_value_t *return_value_4 = tinypy_complex_from_doubles(vm, real, imaginary);
        return return_value_4;
    }
    /* int_pow and long_pow leave a negative exponent to float_pow. */
    if (left_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_FLOAT || __tinypy_operator_integer_sign(right) < 0) {
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
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ZERO_DIVISION, "0.0 cannot be raised to a negative power", out_error);
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
        if (isinf(result) && isfinite(base) && isfinite(exponent)) {
            errno = ERANGE;
        }
        else if (errno == ERANGE && result == 0.0) {
            errno = 0;
        }
        if (errno != 0) {
            __tinypy_operator_raise_errno(vm, errno, out_error);
            return NULL;
        }
        tinypy_value_t *return_value_11 = tinypy_float_from_double(vm, result);
        return return_value_11;
    }
    tinypy_bool_t prefer_long = left_kind == TINYPY_VALUE_LONG || right_kind == TINYPY_VALUE_LONG;

    if (prefer_long == 0) {
        int64_t power = 0;
        if (tinypy_internal_integer_power(TINYPY_INTEGER_VALUE(left), TINYPY_INTEGER_VALUE(right), INT64_C(0), &power) != 0) {
            tinypy_value_t *integer = tinypy_integer_from_i64(vm, power);
            return integer;
        }
    }
    tinypy_value_t *result = tinypy_internal_integer_power_long(left, right, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* long_pow of integers with a non-negative exponent, which int_pow hands its
   operands to on overflow; the result is a long when an operand is one. */
tinypy_value_t *tinypy_internal_integer_power_long(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_integer_view_t exponent_view;
    tinypy_integer_view_t base_view;
    size_t exponent;
    tinypy_bool_t prefer_long = TINYPY_VALUE_KIND(left) == TINYPY_VALUE_LONG || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_LONG;

    __tinypy_operator_integer_view(right, &exponent_view);
    __tinypy_operator_integer_view(left, &base_view);
    if (base_view.count == 0U || (base_view.count == 1U && base_view.digits[0] == 1U)) {
        int64_t trivial = base_view.sign == 0 ? (exponent_view.sign == 0 ? 1 : 0) : (base_view.sign > 0 ? 1 : ((exponent_view.count != 0U && (exponent_view.digits[0] & 1U) != 0U) ? -1 : 1));
        tinypy_value_t *trivial_result = prefer_long != 0 ? tinypy_long_from_i64(vm, trivial) : tinypy_integer_from_i64(vm, trivial);
        return trivial_result;
    }
    if (__tinypy_operator_integer_exponent(vm, right, &exponent, out_error) == 0) {
        return NULL;
    }
    size_t mask = 1U;

    while (mask <= exponent / 2U) {
        mask <<= 1U;
    }
    /* Left-to-right binary exponentiation, as long_pow: the partial power is
       squared, then multiplied by the base for every set bit. An int base
       takes the steps in int64 until one would overflow, and the values take
       the rest from that step. */
    int64_t partial = INT64_C(1);
    tinypy_bool_t square_pending = TINYPY_FALSE;

    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_INTEGER) {
        int64_t base = TINYPY_INTEGER_VALUE(left);

        for (; mask != 0U; mask >>= 1U) {
            if (square_pending != 0 && __tinypy_operator_multiply_overflow(partial, partial, &partial) != 0) {
                break;
            }
            square_pending = TINYPY_FALSE;
            if ((exponent & mask) != 0U && __tinypy_operator_multiply_overflow(partial, base, &partial) != 0) {
                break;
            }
            square_pending = TINYPY_TRUE;
        }
    }
    tinypy_value_t *result = prefer_long != 0 ? tinypy_long_from_i64(vm, partial) : tinypy_integer_from_i64(vm, partial);

    for (; mask != 0U; mask >>= 1U) {
        if (square_pending != 0) {
            tinypy_value_t *squared = tinypy_internal_operator_builtin(result, result, 2, out_error);

            TINYPY_DECREF(result);
            if (squared == NULL) {
                return NULL;
            }
            result = squared;
        }
        square_pending = TINYPY_TRUE;
        if ((exponent & mask) == 0U) {
            continue;
        }
        tinypy_value_t *multiplied = tinypy_internal_operator_builtin(result, left, 2, out_error);

        TINYPY_DECREF(result);
        if (multiplied == NULL) {
            return NULL;
        }
        result = multiplied;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_power_modulo_builtin(tinypy_value_t *base_value, tinypy_value_t *exponent_value, tinypy_value_t *modulus_value, tinypy_error_t **out_error) {
    tinypy_value_type_e base_kind = TINYPY_VALUE_KIND(base_value);
    tinypy_value_type_e exponent_kind = TINYPY_VALUE_KIND(exponent_value);
    tinypy_value_type_e modulus_kind = TINYPY_VALUE_KIND(modulus_value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(base_value);
    tinypy_integer_view_t exponent;
    tinypy_integer_view_t modulus;

    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_operator_is_integer(base_kind) == 0 || __tinypy_operator_is_integer(exponent_kind) == 0 || __tinypy_operator_is_integer(modulus_kind) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "pow() 3rd argument not allowed unless all arguments are integers", out_error);
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
    if (base_kind != TINYPY_VALUE_LONG && exponent_kind != TINYPY_VALUE_LONG && modulus_kind != TINYPY_VALUE_LONG) {
        int64_t power = 0;
        if (tinypy_internal_integer_power(TINYPY_INTEGER_VALUE(base_value), TINYPY_INTEGER_VALUE(exponent_value), TINYPY_INTEGER_VALUE(modulus_value), &power) != 0) {
            tinypy_value_t *integer = tinypy_integer_from_i64(vm, power);
            return integer;
        }
    }
    tinypy_integer_view_t base;

    __tinypy_operator_integer_view(base_value, &base);
    tinypy_value_t *result = __tinypy_operator_long_power_modulo(vm, &base, &exponent, &modulus, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The nb_power or nb_inplace_power slot of a value's type. Only Python
   methods and classic instances define the in-place one. */
static tinypy_operator_slot_e __tinypy_operator_power_slot(tinypy_value_t *value, tinypy_bool_t inplace) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_OLD_INSTANCE) {
        return TINYPY_OPERATOR_SLOT_CLASSIC;
    }
    if (__tinypy_operator_is_exact_builtin(value) == 0) {
        if (inplace != 0) {
            tinypy_operator_slot_e inplace_slot = __tinypy_internal_object_overrides_dispatch(value, TINYPY_INTERNAL_DISPATCH_BIT(IPOW)) != 0 ? TINYPY_OPERATOR_SLOT_PYTHON : TINYPY_OPERATOR_SLOT_NONE;

            return inplace_slot;
        }
        if (__tinypy_internal_object_overrides_dispatch(value, TINYPY_INTERNAL_DISPATCH_BIT(POW) | TINYPY_INTERNAL_DISPATCH_BIT(RPOW)) != 0) {
            return TINYPY_OPERATOR_SLOT_PYTHON;
        }
        if (value->type->number_slots != NULL && value->type->number_slots->power != NULL) {
            return TINYPY_OPERATOR_SLOT_NATIVE;
        }
    }
    if (inplace == 0 && __tinypy_operator_number_rank(kind) != 0) {
        return TINYPY_OPERATOR_SLOT_BUILTIN;
    }
    return TINYPY_OPERATOR_SLOT_NONE;
}
//////////////////////////////////////////////////////////////////////////
/* ternary_op skips a slot already tried for another operand. */
static tinypy_bool_t __tinypy_operator_same_power_slot(tinypy_value_t *value, tinypy_operator_slot_e slot, tinypy_value_t *other, tinypy_operator_slot_e other_slot) {
    if (slot != other_slot || slot == TINYPY_OPERATOR_SLOT_NONE) {
        return TINYPY_FALSE;
    }
    if (slot == TINYPY_OPERATOR_SLOT_BUILTIN) {
        tinypy_bool_t same_rank = __tinypy_operator_number_rank(TINYPY_VALUE_KIND(value)) == __tinypy_operator_number_rank(TINYPY_VALUE_KIND(other)) ? TINYPY_TRUE : TINYPY_FALSE;

        return same_rank;
    }
    if (slot == TINYPY_OPERATOR_SLOT_NATIVE) {
        tinypy_bool_t same_native = value->type->number_slots->power == other->type->number_slots->power ? TINYPY_TRUE : TINYPY_FALSE;

        return same_native;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* int_pow, long_pow, float_pow and complex_pow convert the base and the
   exponent up to their own rank. float_pow rejects a modulus before looking
   at its operands, int_pow a negative exponent before the modulus, and the
   integers take a modulus of their own rank only. */
static TINYPY_ALWAYS_INLINE tinypy_bool_t __tinypy_operator_power_accepts(tinypy_value_type_e owner_kind, tinypy_value_t *base, tinypy_value_t *exponent, tinypy_value_t *modulus) {
    int32_t rank = __tinypy_operator_number_rank(owner_kind);
    int32_t base_rank = __tinypy_operator_number_rank(TINYPY_VALUE_KIND(base));
    int32_t exponent_rank = __tinypy_operator_number_rank(TINYPY_VALUE_KIND(exponent));
    int32_t modulus_rank = __tinypy_operator_number_rank(TINYPY_VALUE_KIND(modulus));
    tinypy_bool_t has_modulus = TINYPY_VALUE_KIND(modulus) != TINYPY_VALUE_NONE ? TINYPY_TRUE : TINYPY_FALSE;

    if (rank == 3 && has_modulus != 0) {
        return TINYPY_TRUE;
    }
    if (base_rank == 0 || exponent_rank == 0 || base_rank > rank || exponent_rank > rank) {
        return TINYPY_FALSE;
    }
    if (has_modulus == 0 || rank > 2 || (rank == 1 && __tinypy_operator_integer_sign(exponent) < 0)) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t integral = modulus_rank != 0 && modulus_rank <= rank ? TINYPY_TRUE : TINYPY_FALSE;

    return integral;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_builtin_power(tinypy_value_type_e owner_kind, tinypy_value_t *base, tinypy_value_t *exponent, tinypy_value_t *modulus, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(base);
    int32_t rank = __tinypy_operator_number_rank(owner_kind);

    if (TINYPY_VALUE_KIND(modulus) == TINYPY_VALUE_NONE) {
        tinypy_value_t *power = tinypy_internal_operator_builtin(base, exponent, (int32_t)TINYPY_OPERATOR_BINARY_POWER, out_error);
        return power;
    }
    if (rank == 3) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "pow() 3rd argument not allowed unless all arguments are integers", out_error);
        return NULL;
    }
    if (__tinypy_operator_integer_sign(exponent) < 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "pow() 2nd argument cannot be negative when 3rd argument specified", out_error);
        return NULL;
    }
    tinypy_value_t *power = tinypy_internal_power_modulo_builtin(base, exponent, modulus, out_error);
    return power;
}
//////////////////////////////////////////////////////////////////////////
/* complex_pow converts both operands (TO_COMPLEX) before it rejects a
   modulus: an operand it cannot convert declines the call, a long too large
   for a float raises OverflowError. */
static tinypy_value_t *__tinypy_operator_complex_power_modulo(tinypy_value_t *base, tinypy_value_t *exponent, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(base);
    tinypy_value_t *operands[2] = {base, exponent};

    for (size_t index = 0U; index != 2U; ++index) {
        double real;
        double imaginary;

        if (__tinypy_operator_number_rank(TINYPY_VALUE_KIND(operands[index])) == 0) {
            tinypy_value_t *declined = TINYPY_RET_NOT_IMPLEMENTED(vm);
            return declined;
        }
        if (__tinypy_operator_as_complex(operands[index], &real, &imaginary, out_error) == 0) {
            return NULL;
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "complex modulo", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* The nb_power of int, long, float or complex, which declines operands it
   does not convert. */
tinypy_value_t *tinypy_internal_builtin_power_slot(tinypy_value_type_e owner_kind, tinypy_value_t *base, tinypy_value_t *exponent, tinypy_value_t *modulus, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    if (owner_kind == TINYPY_VALUE_COMPLEX && TINYPY_VALUE_KIND(modulus) != TINYPY_VALUE_NONE) {
        tinypy_value_t *complex_power = __tinypy_operator_complex_power_modulo(base, exponent, out_error);
        return complex_power;
    }
    if (__tinypy_operator_power_accepts(owner_kind, base, exponent, modulus) == 0) {
        tinypy_value_t *declined = TINYPY_RET_NOT_IMPLEMENTED(TINYPY_VALUE_VM(base));
        return declined;
    }
    tinypy_value_t *power = __tinypy_operator_builtin_power(owner_kind, base, exponent, modulus, out_error);
    return power;
}
//////////////////////////////////////////////////////////////////////////
/* instance_pow and instance_ipow: two operands take the classic binary
   protocol, a modulus calls the base's __pow__ or __ipow__ directly. */
static tinypy_value_t *__tinypy_operator_classic_power(tinypy_value_t *base, tinypy_value_t *exponent, tinypy_value_t *modulus, tinypy_bool_t inplace, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(base);
    tinypy_value_t *method;

    if (TINYPY_VALUE_KIND(modulus) == TINYPY_VALUE_NONE) {
        tinypy_value_t *binary = inplace != 0
            ? __tinypy_operator_classic_inplace(base, exponent, vm->internal_special_ipow_key, vm->internal_special_pow_key, vm->internal_special_rpow_key, tinypy_inplace_power, out_error)
            : __tinypy_operator_classic_binary(base, exponent, vm->internal_special_pow_key, vm->internal_special_rpow_key, tinypy_power, out_error);

        return binary;
    }
    if (inplace != 0) {
        int32_t status = tinypy_internal_object_get_optional_attr_key(base, vm->internal_special_ipow_key, &method, out_error);
        if (status < 0) {
            return NULL;
        }
        if (status == 0) {
            tinypy_value_t *power = __tinypy_operator_classic_power(base, exponent, modulus, TINYPY_FALSE, out_error);
            return power;
        }
    }
    else {
        method = tinypy_internal_object_get_attr_key(base, vm->internal_special_pow_key, out_error);
        if (method == NULL) {
            return NULL;
        }
    }
    tinypy_value_t *arguments[2] = {exponent, modulus};
    tinypy_value_t *args = tinypy_tuple_from_items(vm, arguments, 2U);
    tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Calls the power slot of the owner's type on the triple, the way
   ternary_op calls slotv, slotw or slotz(v, w, z). */
static tinypy_value_t *__tinypy_operator_call_power_slot(tinypy_operator_slot_e slot, tinypy_value_t *owner, tinypy_value_t *base, tinypy_value_t *exponent, tinypy_value_t *modulus, tinypy_bool_t inplace, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(base);
    const tinypy_operator_binary_spec_t *spec = &__tinypy_operator_binary_specs[TINYPY_OPERATOR_BINARY_POWER];
    tinypy_value_t *result;

    switch (slot) {
    case TINYPY_OPERATOR_SLOT_BUILTIN:
        result = tinypy_internal_builtin_power_slot(TINYPY_VALUE_KIND(owner), base, exponent, modulus, out_error);
        return result;
    case TINYPY_OPERATOR_SLOT_NATIVE:
        result = owner->type->number_slots->power(base, exponent, modulus, out_error);
        return result;
    case TINYPY_OPERATOR_SLOT_PYTHON:
        if (inplace != 0) {
            result = __tinypy_operator_call_type_method(base, vm->internal_special_ipow_key, &exponent, 1U, TINYPY_TRUE, out_error);
            return result;
        }
        if (TINYPY_VALUE_KIND(modulus) == TINYPY_VALUE_NONE) {
            tinypy_operator_slot_e base_slot = __tinypy_operator_power_slot(base, TINYPY_FALSE);
            tinypy_operator_slot_e exponent_slot = __tinypy_operator_power_slot(exponent, TINYPY_FALSE);

            result = __tinypy_operator_python_slot(spec, base, exponent, base_slot, exponent_slot, out_error);
            return result;
        }
        /* Three-argument power never uses __rpow__. */
        if (__tinypy_operator_power_slot(base, TINYPY_FALSE) == TINYPY_OPERATOR_SLOT_PYTHON) {
            tinypy_value_t *arguments[2] = {exponent, modulus};

            result = __tinypy_operator_call_type_method(base, vm->internal_special_pow_key, arguments, 2U, TINYPY_TRUE, out_error);
            return result;
        }
        break;
    case TINYPY_OPERATOR_SLOT_CLASSIC:
        result = __tinypy_operator_classic_power(base, exponent, modulus, inplace, out_error);
        return result;
    default:
        break;
    }
    result = TINYPY_RET_NOT_IMPLEMENTED(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_power_unsupported(tinypy_value_t *base, tinypy_value_t *exponent, tinypy_value_t *modulus, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(base);

    if (TINYPY_VALUE_KIND(modulus) == TINYPY_VALUE_NONE) {
        tinypy_value_t *binary = __tinypy_operator_unsupported(vm, base, exponent, "** or pow()", out_error);
        return binary;
    }
    const tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("unsupported operand type(s) for pow(): '"),
        TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(base, 100U),
        TINYPY_MESSAGE_PART_LITERAL("', '"),
        TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(exponent, 100U),
        TINYPY_MESSAGE_PART_LITERAL("', '"),
        TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(modulus, 100U),
        TINYPY_MESSAGE_PART_LITERAL("'")
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* The coercion step of ternary_op for an operand that does not check types
   itself. Failure of any coercion is reported as the unsupported operands
   of the pair coerced so far. */
static tinypy_value_t *__tinypy_operator_coerced_power(tinypy_value_t *base, tinypy_value_t *exponent, tinypy_value_t *modulus, tinypy_bool_t inplace, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(base);
    tinypy_value_t *first = base;
    tinypy_value_t *second = exponent;
    tinypy_value_t *result = NULL;

    if (tinypy_internal_number_coerce(&first, &second, out_error) != 0) {
        first = TINYPY_RET(base);
        second = TINYPY_RET(exponent);
        goto unsupported;
    }
    if (TINYPY_VALUE_KIND(modulus) == TINYPY_VALUE_NONE) {
        tinypy_operator_slot_e slot = __tinypy_operator_power_slot(first, inplace);

        if (slot == TINYPY_OPERATOR_SLOT_NONE) {
            goto unsupported;
        }
        result = __tinypy_operator_call_power_slot(slot, first, first, second, modulus, inplace, out_error);
        goto cleanup;
    }
    tinypy_value_t *coerced_base = first;
    tinypy_value_t *base_modulus = modulus;
    if (tinypy_internal_number_coerce(&coerced_base, &base_modulus, out_error) != 0) {
        goto unsupported;
    }
    tinypy_value_t *coerced_exponent = second;
    tinypy_value_t *coerced_modulus = base_modulus;
    if (tinypy_internal_number_coerce(&coerced_exponent, &coerced_modulus, out_error) != 0) {
        TINYPY_DECREF(base_modulus);
        TINYPY_DECREF(coerced_base);
        goto unsupported;
    }
    tinypy_operator_slot_e slot = __tinypy_operator_power_slot(coerced_base, inplace);
    if (slot != TINYPY_OPERATOR_SLOT_NONE) {
        result = __tinypy_operator_call_power_slot(slot, coerced_base, coerced_base, coerced_exponent, coerced_modulus, inplace, out_error);
    }
    TINYPY_DECREF(coerced_modulus);
    TINYPY_DECREF(coerced_exponent);
    TINYPY_DECREF(base_modulus);
    TINYPY_DECREF(coerced_base);
    if (slot != TINYPY_OPERATOR_SLOT_NONE) {
        goto cleanup;
    }
unsupported:
    tinypy_internal_exception_clear_raised(vm);
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
    }
    result = __tinypy_operator_power_unsupported(first, second, modulus, out_error);
cleanup:
    TINYPY_DECREF(second);
    TINYPY_DECREF(first);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* ternary_op of Python 2.7 over nb_power, or over nb_inplace_power when the
   base's type defines it: the base's and exponent's slots as in binary_op1,
   then the modulus' own slot, then coercion of old-style operands. */
static tinypy_value_t *__tinypy_operator_ternary(tinypy_value_t *base, tinypy_value_t *exponent, tinypy_value_t *modulus, tinypy_bool_t inplace, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(base);
    tinypy_value_t *result;

    TINYPY_CLEAR_ERROR(out_error);
    /* Exact built-in numbers end in the built-in power of the higher ranked
       type whichever slot is tried first, so the slot search is skipped. */
    if (__tinypy_operator_is_exact_builtin(base) != 0 && __tinypy_operator_is_exact_builtin(exponent) != 0) {
        tinypy_value_type_e base_kind = TINYPY_VALUE_KIND(base);
        tinypy_value_type_e exponent_kind = TINYPY_VALUE_KIND(exponent);
        tinypy_value_type_e modulus_kind = TINYPY_VALUE_KIND(modulus);

        if (modulus_kind == TINYPY_VALUE_NONE && __tinypy_operator_number_rank(base_kind) != 0 && __tinypy_operator_number_rank(exponent_kind) != 0) {
            result = __tinypy_operator_power_builtin(base, exponent, out_error);
            return result;
        }
        if (__tinypy_operator_is_integer(base_kind) != 0 && __tinypy_operator_is_integer(exponent_kind) != 0 && __tinypy_operator_is_integer(modulus_kind) != 0 && __tinypy_operator_is_exact_builtin(modulus) != 0) {
            result = tinypy_internal_power_modulo_builtin(base, exponent, modulus, out_error);
            return result;
        }
    }
    tinypy_operator_slot_e base_slot = __tinypy_operator_power_slot(base, inplace);
    tinypy_operator_slot_e exponent_slot = exponent->type != base->type ? __tinypy_operator_power_slot(exponent, inplace) : TINYPY_OPERATOR_SLOT_NONE;

    if (__tinypy_operator_same_power_slot(base, base_slot, exponent, exponent_slot) != 0) {
        exponent_slot = TINYPY_OPERATOR_SLOT_NONE;
    }
    if (base_slot != TINYPY_OPERATOR_SLOT_NONE) {
        if (exponent_slot != TINYPY_OPERATOR_SLOT_NONE && tinypy_type_is_subtype(exponent->type, base->type) != 0) {
            result = __tinypy_operator_call_power_slot(exponent_slot, exponent, base, exponent, modulus, inplace, out_error);
            if (result == NULL || result != &vm->not_implemented_object.base) {
                return result;
            }
            TINYPY_DECREF(result);
            exponent_slot = TINYPY_OPERATOR_SLOT_NONE;
        }
        result = __tinypy_operator_call_power_slot(base_slot, base, base, exponent, modulus, inplace, out_error);
        if (result == NULL || result != &vm->not_implemented_object.base) {
            return result;
        }
        TINYPY_DECREF(result);
    }
    if (exponent_slot != TINYPY_OPERATOR_SLOT_NONE) {
        result = __tinypy_operator_call_power_slot(exponent_slot, exponent, base, exponent, modulus, inplace, out_error);
        if (result == NULL || result != &vm->not_implemented_object.base) {
            return result;
        }
        TINYPY_DECREF(result);
    }
    tinypy_bool_t has_modulus = TINYPY_VALUE_KIND(modulus) != TINYPY_VALUE_NONE ? TINYPY_TRUE : TINYPY_FALSE;
    if (has_modulus != 0) {
        tinypy_operator_slot_e modulus_slot = __tinypy_operator_power_slot(modulus, inplace);

        if (modulus_slot != TINYPY_OPERATOR_SLOT_NONE && __tinypy_operator_same_power_slot(modulus, modulus_slot, base, base_slot) == 0 && __tinypy_operator_same_power_slot(modulus, modulus_slot, exponent, exponent_slot) == 0) {
            result = __tinypy_operator_call_power_slot(modulus_slot, modulus, base, exponent, modulus, inplace, out_error);
            if (result == NULL || result != &vm->not_implemented_object.base) {
                return result;
            }
            TINYPY_DECREF(result);
        }
    }
    if (__tinypy_operator_new_style_number(base) == 0 || __tinypy_operator_new_style_number(exponent) == 0 || (has_modulus != 0 && __tinypy_operator_new_style_number(modulus) == 0)) {
        result = __tinypy_operator_coerced_power(base, exponent, modulus, inplace, out_error);
        return result;
    }
    result = __tinypy_operator_power_unsupported(base, exponent, modulus, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_power(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *result = __tinypy_operator_ternary(left, right, &vm->none_object.base, TINYPY_FALSE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_power_modulo(tinypy_value_t *base_value, tinypy_value_t *exponent_value, tinypy_value_t *modulus_value, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_operator_ternary(base_value, exponent_value, modulus_value, TINYPY_FALSE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* PyNumber_InPlacePower: a base without an in-place power slot takes the
   plain one; neither retries the other. */
tinypy_value_t *tinypy_inplace_power(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_bool_t inplace = __tinypy_operator_power_slot(left, TINYPY_TRUE) != TINYPY_OPERATOR_SLOT_NONE ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_value_t *result = __tinypy_operator_ternary(left, right, &vm->none_object.base, inplace, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_multiply_builtin(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    /* The repeat of str, unicode, tuple and list behind their __mul__. */
    tinypy_bool_t left_sequence = left_kind == TINYPY_VALUE_STRING || left_kind == TINYPY_VALUE_UNICODE || left_kind == TINYPY_VALUE_TUPLE || left_kind == TINYPY_VALUE_LIST ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_bool_t right_sequence = right_kind == TINYPY_VALUE_STRING || right_kind == TINYPY_VALUE_UNICODE || right_kind == TINYPY_VALUE_TUPLE || right_kind == TINYPY_VALUE_LIST ? TINYPY_TRUE : TINYPY_FALSE;
    if (left_sequence != 0 && right_sequence == 0) {
        tinypy_value_t *repeated = __tinypy_operator_sequence_repeat(left, right, TINYPY_FALSE, out_error);
        return repeated;
    }
    if (right_sequence != 0 && left_sequence == 0) {
        tinypy_value_t *repeated = __tinypy_operator_sequence_repeat(right, left, TINYPY_FALSE, out_error);
        return repeated;
    }
    if (__tinypy_operator_is_number(left_kind) == 0 || __tinypy_operator_is_number(right_kind) == 0) {
        tinypy_value_t *unsupported = __tinypy_operator_unsupported(vm, left, right, "*", out_error);
        return unsupported;
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
            if (left_kind == TINYPY_VALUE_LONG && __tinypy_operator_remainder_is_dividend(&left_view, &right_view) != 0) {
                items[0] = tinypy_long_from_i64(vm, INT64_C(0));
                items[1] = left;
                result = tinypy_internal_tuple_from_items_checked(vm, items, 2U, out_error);
                TINYPY_DECREF(items[0]);
                return result;
            }
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
        uint64_t shift;

        if (__tinypy_operator_is_integer(left_kind) == 0 || __tinypy_operator_is_integer(right_kind) == 0) {
            tinypy_value_t *unsupported = __tinypy_operator_unsupported(vm, left, right, mode == 9 ? "<<" : ">>", out_error);
            return unsupported;
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
            tinypy_value_t *result = tinypy_internal_long_from_base15_digits_checked(vm, TINYPY_LONG_SIGN(value), TINYPY_LONG_OBJECT(value)->digits, TINYPY_LONG_DIGIT_COUNT(value), out_error);

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
            tinypy_value_t *result = tinypy_internal_long_from_base15_digits_checked(vm, -view.sign, view.digits, view.count, out_error);

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
        tinypy_value_t *result = TINYPY_LONG_SIGN(value) < 0 ? tinypy_internal_unary_builtin(value, 1, out_error) : tinypy_internal_long_from_base15_digits_checked(vm, TINYPY_LONG_SIGN(value), TINYPY_LONG_OBJECT(value)->digits, TINYPY_LONG_DIGIT_COUNT(value), out_error);

        return result;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        tinypy_value_t *result = tinypy_float_from_double(vm, fabs(TINYPY_FLOAT_OBJECT(value)->value));

        return result;
    }
    double real = TINYPY_COMPLEX_OBJECT(value)->real;
    double imaginary = TINYPY_COMPLEX_OBJECT(value)->imaginary;

    /* c_abs applies the C99 rule itself rather than trusting hypot with
       signalling NaNs: an infinite part gives infinity even beside a NaN,
       and any other NaN part gives the default NaN. */
    if (isinf(real) || isinf(imaginary)) {
        tinypy_value_t *infinite = tinypy_float_from_double(vm, INFINITY);
        return infinite;
    }
    if (isnan(real) || isnan(imaginary)) {
        tinypy_value_t *not_a_number = tinypy_float_from_double(vm, NAN);
        return not_a_number;
    }
    double absolute = hypot(real, imaginary);

    if (isinf(absolute)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "absolute value too large", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_float_from_double(vm, absolute);

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* wrap_binaryfunc, wrap_binaryfunc_r and wrap_ternaryfunc expose the
   existing instance_* slot, including coercion and the reflected half. */
static tinypy_value_t *__tinypy_operator_classic_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t mode = (size_t)(uintptr_t)user_data;
    const tinypy_operator_binary_spec_t *spec = &__tinypy_operator_binary_specs[mode / 3U];
    size_t direction = mode % 3U;
    tinypy_bool_t power = spec->operation == TINYPY_OPERATOR_BINARY_POWER;
    tinypy_bool_t ternary = power != 0 && direction != 2U;
    size_t maximum = ternary != 0 ? 2U : 1U;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, maximum, ternary != 0 ? TINYPY_ARITY_STYLE_UNPACK : TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, direction == 1U ? 1U : 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, direction == 1U ? 0U : 1U);
    tinypy_value_t *result;

    if (power != 0) {
        tinypy_value_t *modulus = TINYPY_TUPLE_SIZE(args) == 3U ? TINYPY_TUPLE_GET(args, 2U) : &vm->none_object.base;

        result = __tinypy_operator_classic_power(left, right, modulus, direction == 2U, out_error);
        return result;
    }
    tinypy_value_t *name = tinypy_internal_dispatch_key(vm, spec->name);
    tinypy_value_t *reverse = tinypy_internal_dispatch_key(vm, spec->reverse_name);

    if (direction == 2U) {
        result = __tinypy_operator_classic_inplace(left, right, tinypy_internal_dispatch_key(vm, spec->inplace_name), name, reverse, spec->inplace_function, out_error);
        return result;
    }
    result = __tinypy_operator_classic_binary(left, right, name, reverse, spec->function, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_operator_classic_coerce_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *operands[2] = {TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U)};
    int32_t status = __tinypy_operator_coerce_slot(&operands[0], &operands[1], out_error);

    if (status < 0) {
        return NULL;
    }
    if (status > 0) {
        return TINYPY_RET_NOT_IMPLEMENTED(vm);
    }
    tinypy_value_t *result = tinypy_tuple_from_items(vm, operands, 2U);

    TINYPY_DECREF(operands[1]);
    TINYPY_DECREF(operands[0]);
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_classic_operator_methods(tinypy_vm_t *vm) {
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_OLD_INSTANCE];

    for (size_t index = 0U; index < sizeof(__tinypy_operator_binary_specs) / sizeof(__tinypy_operator_binary_specs[0]); ++index) {
        const tinypy_operator_binary_spec_t *spec = &__tinypy_operator_binary_specs[index];
        tinypy_internal_dispatch_e names[3] = {spec->name, spec->reverse_name, spec->inplace_name};

        for (size_t direction = 0U; direction < 3U; ++direction) {
            if (names[direction] == TINYPY_OPERATOR_NO_DISPATCH) {
                continue;
            }
            tinypy_internal_type_add_method(type, tinypy_internal_dispatch_key(vm, names[direction]), __tinypy_operator_classic_method, (void *)(uintptr_t)(index * 3U + direction), NULL, TINYPY_NATIVE_DESCRIPTOR_WRAPPER);
        }
    }
    tinypy_internal_type_add_method(type, vm->internal_special_coerce_key, __tinypy_operator_classic_coerce_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_WRAPPER);
}
//////////////////////////////////////////////////////////////////////////
