#include "internal.h"

#include <string.h>

typedef struct tinypy_decimal_bigint_t {
    tinypy_compile_ctx_t *ctx;
    uint32_t *words;
    size_t count;
    size_t capacity;
} tinypy_decimal_bigint_t;

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_decimal_bigint_reserve(tinypy_decimal_bigint_t *value, size_t capacity) {
    uint32_t *words;
    size_t new_capacity;

    if (capacity <= value->capacity) {
        return INT32_C(1);
    }
    new_capacity = value->capacity == 0U ? 4U : value->capacity;
    while (new_capacity < capacity) {
        assert(new_capacity <= SIZE_MAX / 2U);
        new_capacity *= 2U;
    }
    assert(new_capacity <= SIZE_MAX / sizeof(*words));
    words = (uint32_t *)tinypy_internal_compiler_arena_allocate(value->ctx, new_capacity * sizeof(*words));
    if (words == NULL) {
        return INT32_C(0);
    }
    if (value->count != 0U) {
        (void)memcpy(words, value->words, value->count * sizeof(*words));
    }
    value->words = words;
    value->capacity = new_capacity;
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_decimal_bigint_set_u32(tinypy_decimal_bigint_t *value, uint32_t integer) {
    if (__tinypy_decimal_bigint_reserve(value, 1U) == 0) {
        return INT32_C(0);
    }
    value->words[0] = integer;
    value->count = integer == 0U ? 0U : 1U;
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_decimal_bigint_multiply_add(tinypy_decimal_bigint_t *value, uint32_t multiplier, uint32_t addition) {
    uint64_t carry = addition;
    size_t index;

    if (__tinypy_decimal_bigint_reserve(value, value->count + 1U) == 0) {
        return INT32_C(0);
    }
    for (index = 0U; index < value->count; ++index) {
        uint64_t product = (uint64_t)value->words[index] * (uint64_t)multiplier + carry;

        value->words[index] = (uint32_t)product;
        carry = product >> 32U;
    }
    if (carry != 0U) {
        value->words[value->count] = (uint32_t)carry;
        value->count += 1U;
    }
    else if (value->count == 0U && addition != 0U) {
        value->words[0] = addition;
        value->count = 1U;
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_decimal_bigint_bit_length(const tinypy_decimal_bigint_t *value) {
    uint32_t high;
    size_t bits;

    if (value->count == 0U) {
        return 0U;
    }
    high = value->words[value->count - 1U];
    bits = (value->count - 1U) * 32U;
    while (high != 0U) {
        high >>= 1U;
        bits += 1U;
    }
    return bits;
}
//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_decimal_bigint_shifted_word(const tinypy_decimal_bigint_t *value, size_t output_index, size_t shift) {
    size_t word_shift = shift / 32U;
    uint32_t bit_shift = (uint32_t)(shift % 32U);
    uint64_t word = 0U;

    if (output_index >= word_shift) {
        size_t source_index = output_index - word_shift;

        if (source_index < value->count) {
            word |= (uint64_t)value->words[source_index] << bit_shift;
        }
        if (bit_shift != 0U && source_index != 0U && source_index - 1U < value->count) {
            word |= (uint64_t)value->words[source_index - 1U] >> (32U - bit_shift);
        }
    }
    return (uint32_t)word;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_decimal_bigint_shifted_count(const tinypy_decimal_bigint_t *value, size_t shift) {
    size_t count;

    if (value->count == 0U) {
        return 0U;
    }
    count = value->count + shift / 32U + (shift % 32U != 0U ? 1U : 0U);
    while (count != 0U && __tinypy_decimal_bigint_shifted_word(value, count - 1U, shift) == 0U) {
        count -= 1U;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_decimal_bigint_compare_shifted(const tinypy_decimal_bigint_t *left, const tinypy_decimal_bigint_t *right, size_t right_shift) {
    size_t right_count = __tinypy_decimal_bigint_shifted_count(right, right_shift);
    size_t index;

    if (left->count != right_count) {
        return left->count < right_count ? -1 : 1;
    }
    index = left->count;
    while (index != 0U) {
        uint32_t left_word;
        uint32_t right_word;

        index -= 1U;
        left_word = left->words[index];
        right_word = __tinypy_decimal_bigint_shifted_word(right, index, right_shift);
        if (left_word != right_word) {
            return left_word < right_word ? -1 : 1;
        }
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_decimal_bigint_subtract_shifted(tinypy_decimal_bigint_t *left, const tinypy_decimal_bigint_t *right, size_t right_shift) {
    size_t right_count = __tinypy_decimal_bigint_shifted_count(right, right_shift);
    uint64_t borrow = 0U;
    size_t index;

    assert(__tinypy_decimal_bigint_compare_shifted(left, right, right_shift) >= 0);
    for (index = 0U; index < left->count; ++index) {
        uint64_t subtrahend = (index < right_count ? (uint64_t)__tinypy_decimal_bigint_shifted_word(right, index, right_shift) : 0U) + borrow;
        uint64_t minuend = left->words[index];

        left->words[index] = (uint32_t)(minuend - subtrahend);
        borrow = minuend < subtrahend ? 1U : 0U;
    }
    assert(borrow == 0U);
    while (left->count != 0U && left->words[left->count - 1U] == 0U) {
        left->count -= 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_decimal_bigint_shift_left(tinypy_decimal_bigint_t *output, const tinypy_decimal_bigint_t *input, size_t shift) {
    size_t count = __tinypy_decimal_bigint_shifted_count(input, shift);
    size_t index;

    if (count == 0U) {
        return __tinypy_decimal_bigint_set_u32(output, 0U);
    }
    if (__tinypy_decimal_bigint_reserve(output, count) == 0) {
        return INT32_C(0);
    }
    for (index = 0U; index < count; ++index) {
        output->words[index] = __tinypy_decimal_bigint_shifted_word(input, index, shift);
    }
    output->count = count;
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_decimal_bigint_copy(tinypy_decimal_bigint_t *output, const tinypy_decimal_bigint_t *input) {
    if (input->count == 0U) {
        return __tinypy_decimal_bigint_set_u32(output, 0U);
    }
    if (__tinypy_decimal_bigint_reserve(output, input->count) == 0) {
        return INT32_C(0);
    }
    (void)memcpy(output->words, input->words, input->count * sizeof(*input->words));
    output->count = input->count;
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_decimal_bigint_quotient_u64(tinypy_decimal_bigint_t *numerator, const tinypy_decimal_bigint_t *denominator, uint64_t *out_quotient) {
    size_t numerator_bits = __tinypy_decimal_bigint_bit_length(numerator);
    size_t denominator_bits = __tinypy_decimal_bigint_bit_length(denominator);
    size_t shift;
    uint64_t quotient = 0U;

    assert(denominator_bits != 0U);
    if (numerator_bits < denominator_bits) {
        *out_quotient = 0U;
        return INT32_C(1);
    }
    shift = numerator_bits - denominator_bits;
    assert(shift < 64U);
    for (;;) {
        if (__tinypy_decimal_bigint_compare_shifted(numerator, denominator, shift) >= 0) {
            __tinypy_decimal_bigint_subtract_shifted(numerator, denominator, shift);
            quotient |= UINT64_C(1) << shift;
        }
        if (shift == 0U) {
            break;
        }
        shift -= 1U;
    }
    *out_quotient = quotient;
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_decimal_fail_limit(tinypy_compile_ctx_t *ctx, int32_t line_number, int32_t column_offset) {
    tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "floating-point literal exceeds compiler arena limit", line_number, column_offset, ctx->out_error);
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_compiler_decimal_double(tinypy_compile_ctx_t *ctx, const char *text, size_t size, double *out_value, int32_t line_number, int32_t column_offset) {
    tinypy_decimal_bigint_t numerator = {ctx, NULL, 0U, 0U};
    tinypy_decimal_bigint_t denominator = {ctx, NULL, 0U, 0U};
    tinypy_decimal_bigint_t scaled_numerator = {ctx, NULL, 0U, 0U};
    tinypy_decimal_bigint_t scaled_denominator = {ctx, NULL, 0U, 0U};
    tinypy_decimal_bigint_t doubled_remainder = {ctx, NULL, 0U, 0U};
    size_t position = 0U;
    size_t fractional_digits = 0U;
    size_t significant_digits = 0U;
    int32_t saw_nonzero = 0;
    int32_t past_decimal = 0;
    int32_t negative = 0;
    int32_t exponent_negative = 0;
    int64_t exponent_value = 0;
    int64_t decimal_exponent;
    int64_t adjusted_decimal_exponent;
    int64_t binary_exponent;
    int64_t scale;
    uint64_t quotient;
    uint64_t bits;
    size_t index;

    assert(ctx != NULL);
    assert(text != NULL);
    assert(out_value != NULL);
    if (position < size && (text[position] == '+' || text[position] == '-')) {
        negative = text[position] == '-' ? 1 : 0;
        position += 1U;
    }
    if (__tinypy_decimal_bigint_set_u32(&numerator, 0U) == 0 || __tinypy_decimal_bigint_set_u32(&denominator, 1U) == 0) {
        return __tinypy_decimal_fail_limit(ctx, line_number, column_offset);
    }
    while (position < size && text[position] != 'e' && text[position] != 'E') {
        unsigned char byte = (unsigned char)text[position];

        if (byte == '.') {
            past_decimal = 1;
            position += 1U;
            continue;
        }
        if (byte < '0' || byte > '9') {
            return INT32_C(0);
        }
        if (__tinypy_decimal_bigint_multiply_add(&numerator, 10U, (uint32_t)(byte - '0')) == 0) {
            return __tinypy_decimal_fail_limit(ctx, line_number, column_offset);
        }
        if (saw_nonzero != 0 || byte != '0') {
            saw_nonzero = 1;
            significant_digits += 1U;
        }
        if (past_decimal != 0) {
            fractional_digits += 1U;
        }
        position += 1U;
    }
    if (saw_nonzero == 0) {
        bits = negative != 0 ? UINT64_C(0x8000000000000000) : UINT64_C(0);
        (void)memcpy(out_value, &bits, sizeof(bits));
        return INT32_C(1);
    }
    if (position < size) {
        position += 1U;
        if (position < size && (text[position] == '+' || text[position] == '-')) {
            exponent_negative = text[position] == '-' ? 1 : 0;
            position += 1U;
        }
        if (position == size) {
            return INT32_C(0);
        }
        while (position < size) {
            unsigned char byte = (unsigned char)text[position];

            if (byte < '0' || byte > '9') {
                return INT32_C(0);
            }
            if (exponent_value < INT64_C(1000000000)) {
                exponent_value = exponent_value * INT64_C(10) + (int64_t)(byte - '0');
            }
            position += 1U;
        }
    }
    decimal_exponent = (exponent_negative != 0 ? -exponent_value : exponent_value) - (int64_t)fractional_digits;
    adjusted_decimal_exponent = decimal_exponent + (int64_t)significant_digits - INT64_C(1);
    if (adjusted_decimal_exponent > INT64_C(308)) {
        bits = UINT64_C(0x7ff0000000000000) | (negative != 0 ? UINT64_C(0x8000000000000000) : UINT64_C(0));
        (void)memcpy(out_value, &bits, sizeof(bits));
        return INT32_C(1);
    }
    if (adjusted_decimal_exponent < -INT64_C(324)) {
        bits = negative != 0 ? UINT64_C(0x8000000000000000) : UINT64_C(0);
        (void)memcpy(out_value, &bits, sizeof(bits));
        return INT32_C(1);
    }
    if (decimal_exponent >= 0) {
        for (index = 0U; index < (size_t)decimal_exponent; ++index) {
            if (__tinypy_decimal_bigint_multiply_add(&numerator, 5U, 0U) == 0) {
                return __tinypy_decimal_fail_limit(ctx, line_number, column_offset);
            }
        }
    }
    else {
        for (index = 0U; index < (size_t)(-decimal_exponent); ++index) {
            if (__tinypy_decimal_bigint_multiply_add(&denominator, 5U, 0U) == 0) {
                return __tinypy_decimal_fail_limit(ctx, line_number, column_offset);
            }
        }
    }
    binary_exponent = (int64_t)__tinypy_decimal_bigint_bit_length(&numerator) - (int64_t)__tinypy_decimal_bigint_bit_length(&denominator);
    if (binary_exponent >= 0) {
        if (__tinypy_decimal_bigint_compare_shifted(&numerator, &denominator, (size_t)binary_exponent) < 0) {
            binary_exponent -= 1;
        }
    }
    else {
        tinypy_decimal_bigint_t shifted = {ctx, NULL, 0U, 0U};

        if (__tinypy_decimal_bigint_shift_left(&shifted, &numerator, (size_t)(-binary_exponent)) == 0) {
            return __tinypy_decimal_fail_limit(ctx, line_number, column_offset);
        }
        if (__tinypy_decimal_bigint_compare_shifted(&shifted, &denominator, 0U) < 0) {
            binary_exponent -= 1;
        }
    }
    binary_exponent += decimal_exponent;
    if (binary_exponent > INT64_C(1023)) {
        bits = UINT64_C(0x7ff0000000000000) | (negative != 0 ? UINT64_C(0x8000000000000000) : UINT64_C(0));
        (void)memcpy(out_value, &bits, sizeof(bits));
        return INT32_C(1);
    }
    scale = binary_exponent >= -INT64_C(1022) ? decimal_exponent - (binary_exponent - INT64_C(52)) : decimal_exponent + INT64_C(1074);
    if (scale >= 0) {
        if (__tinypy_decimal_bigint_shift_left(&scaled_numerator, &numerator, (size_t)scale) == 0 || __tinypy_decimal_bigint_copy(&scaled_denominator, &denominator) == 0) {
            return __tinypy_decimal_fail_limit(ctx, line_number, column_offset);
        }
    }
    else {
        if (__tinypy_decimal_bigint_copy(&scaled_numerator, &numerator) == 0 || __tinypy_decimal_bigint_shift_left(&scaled_denominator, &denominator, (size_t)(-scale)) == 0) {
            return __tinypy_decimal_fail_limit(ctx, line_number, column_offset);
        }
    }
    if (__tinypy_decimal_bigint_quotient_u64(&scaled_numerator, &scaled_denominator, &quotient) == 0) {
        return INT32_C(0);
    }
    if (__tinypy_decimal_bigint_shift_left(&doubled_remainder, &scaled_numerator, 1U) == 0) {
        return __tinypy_decimal_fail_limit(ctx, line_number, column_offset);
    } {
        int32_t rounding = __tinypy_decimal_bigint_compare_shifted(&doubled_remainder, &scaled_denominator, 0U);

        if (rounding > 0 || (rounding == 0 && (quotient & UINT64_C(1)) != 0U)) {
            quotient += UINT64_C(1);
        }
    }
    if (binary_exponent >= -INT64_C(1022)) {
        if (quotient == (UINT64_C(1) << 53U)) {
            quotient >>= 1U;
            binary_exponent += 1;
        }
        if (binary_exponent > INT64_C(1023)) {
            bits = UINT64_C(0x7ff0000000000000);
        }
        else {
            bits = (uint64_t)(binary_exponent + INT64_C(1023)) << 52U | (quotient & UINT64_C(0x000fffffffffffff));
        }
    }
    else {
        if (quotient >= (UINT64_C(1) << 52U)) {
            bits = UINT64_C(0x0010000000000000);
        }
        else {
            bits = quotient;
        }
    }
    if (negative != 0) {
        bits |= UINT64_C(0x8000000000000000);
    }
    (void)memcpy(out_value, &bits, sizeof(bits));
    return INT32_C(1);
}
