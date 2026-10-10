#include "internal.h"

#include "double_conversion_intrinsics.h"
#include "decimal_powers_of_five.h"

#include <string.h>

#define TINYPY_DECIMAL_INLINE_WORDS ((size_t)64U)
/* 767 significant digits plus a sticky digit decide the rounding of any
   double; longer significands are truncated to this prefix. */
#define TINYPY_DECIMAL_MAX_SIGNIFICANT_DIGITS ((size_t)800U)
/* The significand digits a uint64_t holds whatever they are. */
#define TINYPY_DECIMAL_FAST_DIGITS ((size_t)19U)
/* The digits and the power of five one word multiplication applies. */
#define TINYPY_DECIMAL_WORD_DIGITS ((size_t)9U)
#define TINYPY_DECIMAL_WORD_POWER_OF_FIVE ((size_t)13U)

typedef struct tinypy_decimal_bigint_t {
    tinypy_compile_ctx_t *ctx;
    uint32_t *words;
    size_t count;
    size_t capacity;
    uint32_t inline_words[TINYPY_DECIMAL_INLINE_WORDS];
} tinypy_decimal_bigint_t;

//////////////////////////////////////////////////////////////////////////
static const uint32_t TINYPY_DECIMAL_POWERS_OF_TEN[TINYPY_DECIMAL_WORD_DIGITS + 1U] = {
    1U, 10U, 100U, 1000U, 10000U, 100000U, 1000000U, 10000000U, 100000000U, 1000000000U};
static const uint32_t TINYPY_DECIMAL_POWERS_OF_FIVE[TINYPY_DECIMAL_WORD_POWER_OF_FIVE + 1U] = {
    1U, 5U, 25U, 125U, 625U, 3125U, 15625U, 78125U, 390625U, 1953125U, 9765625U, 48828125U, 244140625U, 1220703125U};
//////////////////////////////////////////////////////////////////////////
static void __tinypy_decimal_bigint_initialize(tinypy_decimal_bigint_t *value, tinypy_compile_ctx_t *ctx) {
    value->ctx = ctx;
    value->words = value->inline_words;
    value->count = 0U;
    value->capacity = TINYPY_DECIMAL_INLINE_WORDS;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_decimal_bigint_reserve(tinypy_decimal_bigint_t *value, size_t capacity) {
    uint32_t *words;
    size_t new_capacity;

    if (capacity <= value->capacity) {
        return TINYPY_TRUE;
    }
    if (capacity > SIZE_MAX / sizeof(*words)) {
        return TINYPY_FALSE;
    }
    new_capacity = value->capacity == 0U ? 4U : value->capacity;
    while (new_capacity < capacity) {
        if (new_capacity > SIZE_MAX / 2U) {
            new_capacity = capacity;
            break;
        }
        new_capacity *= 2U;
    }
    words = (uint32_t *)tinypy_internal_compiler_arena_allocate(value->ctx, new_capacity * sizeof(*words));
    if (words == NULL) {
        return TINYPY_FALSE;
    }
    if (value->count != 0U) {
        (void)memcpy(words, value->words, value->count * sizeof(*words));
    }
    value->words = words;
    value->capacity = new_capacity;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_decimal_bigint_set_u32(tinypy_decimal_bigint_t *value, uint32_t integer) {
    if (__tinypy_decimal_bigint_reserve(value, 1U) == 0) {
        return TINYPY_FALSE;
    }
    value->words[0] = integer;
    value->count = integer == 0U ? 0U : 1U;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_decimal_bigint_multiply_add(tinypy_decimal_bigint_t *value, uint32_t multiplier, uint32_t addition) {
    uint64_t carry = addition;
    size_t index;

    if (value->count == SIZE_MAX || __tinypy_decimal_bigint_reserve(value, value->count + 1U) == 0) {
        return TINYPY_FALSE;
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
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_decimal_bigint_multiply_power_of_five(tinypy_decimal_bigint_t *value, size_t exponent) {
    while (exponent != 0U) {
        size_t step = exponent < TINYPY_DECIMAL_WORD_POWER_OF_FIVE ? exponent : TINYPY_DECIMAL_WORD_POWER_OF_FIVE;

        if (__tinypy_decimal_bigint_multiply_add(value, TINYPY_DECIMAL_POWERS_OF_FIVE[step], 0U) == 0) {
            return TINYPY_FALSE;
        }
        exponent -= step;
    }
    return TINYPY_TRUE;
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
static tinypy_bool_t __tinypy_decimal_bigint_shift_left(tinypy_decimal_bigint_t *output, const tinypy_decimal_bigint_t *input, size_t shift) {
    size_t count = __tinypy_decimal_bigint_shifted_count(input, shift);
    size_t index;

    if (count == 0U) {
        tinypy_bool_t return_value_1 = __tinypy_decimal_bigint_set_u32(output, 0U);
        return return_value_1;
    }
    if (__tinypy_decimal_bigint_reserve(output, count) == 0) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index < count; ++index) {
        output->words[index] = __tinypy_decimal_bigint_shifted_word(input, index, shift);
    }
    output->count = count;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Knuth's algorithm D: the quotient of the numerator by a denominator whose
   top word has its high bit set, which must fit 64 bits; the remainder
   replaces the numerator. */
static tinypy_bool_t __tinypy_decimal_bigint_divide(tinypy_decimal_bigint_t *numerator, const tinypy_decimal_bigint_t *denominator, uint64_t *out_quotient) {
    const uint32_t *divisor = denominator->words;
    size_t divisor_count = denominator->count;
    uint64_t divisor_high = divisor[divisor_count - 1U];
    uint64_t quotient = 0U;
    size_t position;

    *out_quotient = 0U;
    if (numerator->count < divisor_count) {
        return TINYPY_TRUE;
    }
    if (__tinypy_decimal_bigint_reserve(numerator, numerator->count + 1U) == 0) {
        return TINYPY_FALSE;
    }
    uint32_t *dividend = numerator->words;
    dividend[numerator->count] = 0U;
    position = numerator->count - divisor_count + 1U;
    while (position != 0U) {
        uint64_t carry = 0U;
        uint32_t borrow = 0U;
        size_t index;

        position -= 1U;
        uint64_t top = (uint64_t)dividend[position + divisor_count] << 32U | dividend[position + divisor_count - 1U];
        uint64_t estimate = top / divisor_high;
        uint64_t estimate_remainder = top - estimate * divisor_high;
        /* The estimate exceeds the quotient word by at most two; the second
           divisor word removes almost every excess before the subtraction. */
        while (estimate > UINT32_MAX || (divisor_count > 1U && estimate * divisor[divisor_count - 2U] > (estimate_remainder << 32U | dividend[position + divisor_count - 2U]))) {
            estimate -= 1U;
            estimate_remainder += divisor_high;
            if (estimate_remainder > UINT32_MAX) {
                break;
            }
        }
        for (index = 0U; index < divisor_count; ++index) {
            uint64_t product = estimate * divisor[index] + carry;
            uint32_t product_low = (uint32_t)product;
            uint32_t word = dividend[position + index];

            carry = product >> 32U;
            dividend[position + index] = word - product_low - borrow;
            borrow = (uint64_t)word < (uint64_t)product_low + borrow ? 1U : 0U;
        }
        uint32_t top_word = dividend[position + divisor_count];
        dividend[position + divisor_count] = top_word - (uint32_t)carry - borrow;
        if ((uint64_t)top_word < carry + borrow) {
            uint64_t sum_carry = 0U;

            estimate -= 1U;
            for (index = 0U; index < divisor_count; ++index) {
                uint64_t sum = (uint64_t)dividend[position + index] + divisor[index] + sum_carry;

                dividend[position + index] = (uint32_t)sum;
                sum_carry = sum >> 32U;
            }
            dividend[position + divisor_count] += (uint32_t)sum_carry;
        }
        if (position < 2U) {
            quotient |= estimate << (32U * position);
        }
    }
    while (numerator->count != 0U && dividend[numerator->count - 1U] == 0U) {
        numerator->count -= 1U;
    }
    *out_quotient = quotient;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_decimal_leading_zeros(uint64_t value) {
    uint32_t count = 0U;

    if ((value >> 32U) == 0U) {
        count += 32U;
        value <<= 32U;
    }
    if ((value >> 48U) == 0U) {
        count += 16U;
        value <<= 16U;
    }
    if ((value >> 56U) == 0U) {
        count += 8U;
        value <<= 8U;
    }
    if ((value >> 60U) == 0U) {
        count += 4U;
        value <<= 4U;
    }
    if ((value >> 62U) == 0U) {
        count += 2U;
        value <<= 2U;
    }
    if ((value >> 63U) == 0U) {
        count += 1U;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
/* floor(log2(10^exponent)) + 63, the exponent of the 128-bit significand of
   the power of five shifted to a power of ten. */
static int64_t __tinypy_decimal_power_binary_exponent(int64_t exponent) {
    int64_t scaled = exponent * INT64_C(217706);
    int64_t floor_scaled = scaled >= 0 ? scaled / INT64_C(65536) : -((-scaled + INT64_C(65535)) / INT64_C(65536));

    return floor_scaled + INT64_C(63);
}
//////////////////////////////////////////////////////////////////////////
/* The Eisel-Lemire algorithm: the double nearest to significand * 10^exponent
   from the 128-bit product of the significand and the truncated power of
   five. FALSE when that product cannot decide the rounding, which the exact
   conversion settles. */
static tinypy_bool_t __tinypy_decimal_eisel_lemire(uint64_t significand, int64_t exponent, uint64_t *out_bits) {
    const uint64_t *power = __tinypy_decimal_powers_of_five[exponent - TINYPY_DECIMAL_POWER_OF_FIVE_MIN_EXPONENT];
    uint32_t leading_zeros = __tinypy_decimal_leading_zeros(significand);
    uint64_t normalized = significand << leading_zeros;
    uint64_t high;
    uint64_t low = __tinypy_double_multiply_128(normalized, power[0], &high);

    /* Below the 55 bits the rounding needs, only a run of ones may carry the
       truncated part of the power into them. */
    if ((high & UINT64_C(0x1ff)) == UINT64_C(0x1ff)) {
        uint64_t second_high;
        (void)__tinypy_double_multiply_128(normalized, power[1], &second_high);
        low += second_high;
        if (second_high > low) {
            high += 1U;
        }
    }
    /* Powers of five past these exponents are inexact in 128 bits, so a
       product that still ends in ones may be one unit short. */
    if (low == UINT64_MAX && (exponent < -27 || exponent > 55)) {
        return TINYPY_FALSE;
    }
    uint32_t upper_bit = (uint32_t)(high >> 63U);
    uint32_t shift = upper_bit + 9U;
    uint64_t mantissa = high >> shift;
    int64_t biased_exponent = __tinypy_decimal_power_binary_exponent(exponent) + (int64_t)upper_bit - (int64_t)leading_zeros + INT64_C(1023);

    if (biased_exponent <= 0) {
        /* A subnormal keeps the bits above its unit and rounds on the next
           one: no significand of 64 bits makes a tie this far below one. A
           mantissa rounded up to 2^52 is the smallest normal number. */
        if (1 - biased_exponent >= 64) {
            *out_bits = 0U;
            return TINYPY_TRUE;
        }
        mantissa >>= (uint32_t)(1 - biased_exponent);
        mantissa += mantissa & 1U;
        *out_bits = mantissa >> 1U;
        return TINYPY_TRUE;
    }
    /* A product exact below the rounding bit is a tie, which rounds to even
       instead of up; only powers of five within 64 bits make one. */
    if (low <= 1U && exponent >= -4 && exponent <= 23 && (mantissa & 3U) == 1U && (mantissa << shift) == high) {
        mantissa &= ~UINT64_C(1);
    }
    mantissa += mantissa & 1U;
    mantissa >>= 1U;
    if (mantissa >= UINT64_C(1) << 53U) {
        mantissa >>= 1U;
        biased_exponent += 1;
    }
    if (biased_exponent >= INT64_C(0x7ff)) {
        *out_bits = UINT64_C(0x7ff0000000000000);
        return TINYPY_TRUE;
    }
    *out_bits = (uint64_t)biased_exponent << 52U | (mantissa & UINT64_C(0x000fffffffffffff));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_decimal_fail_limit(tinypy_compile_ctx_t *ctx, int32_t line_number, int32_t column_offset) {
    tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "floating-point literal exceeds compiler arena limit", line_number, column_offset, ctx->out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* The exact conversion: the significand digits and the power of ten as the
   ratio of two integers, divided to the 53 bits of the double and rounded
   half to even by the remainder. */
static tinypy_bool_t __tinypy_decimal_exact_bits(tinypy_compile_ctx_t *ctx, const char *text, size_t significand_begin, size_t significand_end, int64_t decimal_exponent, uint64_t *out_bits) {
    tinypy_decimal_bigint_t numerator;
    tinypy_decimal_bigint_t denominator;
    tinypy_decimal_bigint_t scaled_numerator;
    tinypy_decimal_bigint_t scaled_denominator;
    size_t accumulated_digits = 0U;
    size_t dropped_digits = 0U;
    uint32_t sticky_digit = 0U;
    uint32_t chunk = 0U;
    size_t chunk_digits = 0U;
    int64_t binary_exponent;
    int64_t scale;
    size_t index;

    __tinypy_decimal_bigint_initialize(&numerator, ctx);
    __tinypy_decimal_bigint_initialize(&denominator, ctx);
    __tinypy_decimal_bigint_initialize(&scaled_numerator, ctx);
    __tinypy_decimal_bigint_initialize(&scaled_denominator, ctx);
    if (__tinypy_decimal_bigint_set_u32(&denominator, 1U) == 0) {
        return TINYPY_FALSE;
    }
    for (index = significand_begin; index < significand_end; ++index) {
        uint8_t byte = (uint8_t)text[index];

        if (byte == '.' || (accumulated_digits == 0U && byte == '0')) {
            continue;
        }
        if (accumulated_digits == TINYPY_DECIMAL_MAX_SIGNIFICANT_DIGITS) {
            /* Digits past the correctly rounding prefix only matter through a
               sticky digit, which keeps the conversion linear in the input. */
            dropped_digits += 1U;
            if (byte != '0') {
                sticky_digit = 1U;
            }
            continue;
        }
        chunk = chunk * 10U + (uint32_t)(byte - '0');
        chunk_digits += 1U;
        accumulated_digits += 1U;
        if (chunk_digits == TINYPY_DECIMAL_WORD_DIGITS) {
            if (__tinypy_decimal_bigint_multiply_add(&numerator, TINYPY_DECIMAL_POWERS_OF_TEN[chunk_digits], chunk) == 0) {
                return TINYPY_FALSE;
            }
            chunk = 0U;
            chunk_digits = 0U;
        }
    }
    if (dropped_digits != 0U) {
        chunk = chunk * 10U + sticky_digit;
        chunk_digits += 1U;
        decimal_exponent += (int64_t)dropped_digits - INT64_C(1);
    }
    if (chunk_digits != 0U) {
        if (__tinypy_decimal_bigint_multiply_add(&numerator, TINYPY_DECIMAL_POWERS_OF_TEN[chunk_digits], chunk) == 0) {
            return TINYPY_FALSE;
        }
    }
    if (decimal_exponent >= 0) {
        if (__tinypy_decimal_bigint_multiply_power_of_five(&numerator, (size_t)decimal_exponent) == 0) {
            return TINYPY_FALSE;
        }
    }
    else if (__tinypy_decimal_bigint_multiply_power_of_five(&denominator, (size_t)(-decimal_exponent)) == 0) {
        return TINYPY_FALSE;
    }
    binary_exponent = (int64_t)__tinypy_decimal_bigint_bit_length(&numerator) - (int64_t)__tinypy_decimal_bigint_bit_length(&denominator);
    if (binary_exponent >= 0) {
        if (__tinypy_decimal_bigint_compare_shifted(&numerator, &denominator, (size_t)binary_exponent) < 0) {
            binary_exponent -= 1;
        }
    }
    else if (__tinypy_decimal_bigint_compare_shifted(&denominator, &numerator, (size_t)(-binary_exponent)) > 0) {
        binary_exponent -= 1;
    }
    binary_exponent += decimal_exponent;
    if (binary_exponent > INT64_C(1023)) {
        *out_bits = UINT64_C(0x7ff0000000000000);
        return TINYPY_TRUE;
    }
    scale = binary_exponent >= -INT64_C(1022) ? decimal_exponent - (binary_exponent - INT64_C(52)) : decimal_exponent + INT64_C(1074);
    size_t numerator_shift = scale >= 0 ? (size_t)scale : 0U;
    size_t denominator_shift = scale >= 0 ? 0U : (size_t)(-scale);
    /* Algorithm D wants the high bit of the divisor set; shifting both
       operands keeps the quotient and scales the remainder alike. */
    size_t normalization = (32U - (__tinypy_decimal_bigint_bit_length(&denominator) + denominator_shift) % 32U) % 32U;
    if (__tinypy_decimal_bigint_shift_left(&scaled_numerator, &numerator, numerator_shift + normalization) == 0) {
        return TINYPY_FALSE;
    }
    if (__tinypy_decimal_bigint_shift_left(&scaled_denominator, &denominator, denominator_shift + normalization) == 0) {
        return TINYPY_FALSE;
    }
    uint64_t quotient;
    if (__tinypy_decimal_bigint_divide(&scaled_numerator, &scaled_denominator, &quotient) == 0) {
        return TINYPY_FALSE;
    }
    /* The remainder against half the denominator rounds the quotient. */
    int32_t rounding = -__tinypy_decimal_bigint_compare_shifted(&scaled_denominator, &scaled_numerator, 1U);
    if (rounding > 0 || (rounding == 0 && (quotient & UINT64_C(1)) != 0U)) {
        quotient += UINT64_C(1);
    }
    if (binary_exponent < -INT64_C(1022)) {
        *out_bits = quotient >= (UINT64_C(1) << 52U) ? UINT64_C(0x0010000000000000) : quotient;
        return TINYPY_TRUE;
    }
    if (quotient == (UINT64_C(1) << 53U)) {
        quotient >>= 1U;
        binary_exponent += 1;
    }
    if (binary_exponent > INT64_C(1023)) {
        *out_bits = UINT64_C(0x7ff0000000000000);
        return TINYPY_TRUE;
    }
    *out_bits = (uint64_t)(binary_exponent + INT64_C(1023)) << 52U | (quotient & UINT64_C(0x000fffffffffffff));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_compiler_decimal_double(tinypy_compile_ctx_t *ctx, const char *text, size_t size, double *out_value, int32_t line_number, int32_t column_offset) {
    size_t position = 0U;
    size_t significand_begin;
    size_t significand_end;
    size_t fractional_digits = 0U;
    size_t significant_digits = 0U;
    uint64_t leading_significand = 0U;
    int32_t saw_nonzero = 0;
    int32_t past_decimal = 0;
    int32_t negative = 0;
    int32_t exponent_negative = 0;
    int64_t exponent_value = 0;
    int64_t decimal_exponent;
    int64_t adjusted_decimal_exponent;
    uint64_t bits;

    if (position < size && (text[position] == '+' || text[position] == '-')) {
        negative = text[position] == '-' ? 1 : 0;
        position += 1U;
    }
    significand_begin = position;
    while (position < size && text[position] != 'e' && text[position] != 'E') {
        uint8_t byte = (uint8_t)text[position];

        if (byte == '.') {
            past_decimal = 1;
            position += 1U;
            continue;
        }
        if (byte < '0' || byte > '9') {
            return TINYPY_FALSE;
        }
        if (saw_nonzero != 0 || byte != '0') {
            saw_nonzero = 1;
            significant_digits += 1U;
            if (significant_digits <= TINYPY_DECIMAL_FAST_DIGITS) {
                leading_significand = leading_significand * 10U + (uint64_t)(byte - '0');
            }
        }
        if (past_decimal != 0) {
            fractional_digits += 1U;
        }
        position += 1U;
    }
    significand_end = position;
    if (position < size) {
        position += 1U;
        if (position < size && (text[position] == '+' || text[position] == '-')) {
            exponent_negative = text[position] == '-' ? 1 : 0;
            position += 1U;
        }
        if (position == size) {
            return TINYPY_FALSE;
        }
        while (position < size) {
            uint8_t byte = (uint8_t)text[position];

            if (byte < '0' || byte > '9') {
                return TINYPY_FALSE;
            }
            if (exponent_value < INT64_C(1000000000)) {
                exponent_value = exponent_value * INT64_C(10) + (int64_t)(byte - '0');
            }
            position += 1U;
        }
    }
    decimal_exponent = (exponent_negative != 0 ? -exponent_value : exponent_value) - (int64_t)fractional_digits;
    adjusted_decimal_exponent = decimal_exponent + (int64_t)significant_digits - INT64_C(1);
    if (saw_nonzero == 0 || adjusted_decimal_exponent < -INT64_C(324)) {
        bits = 0U;
    }
    else if (adjusted_decimal_exponent > INT64_C(308)) {
        bits = UINT64_C(0x7ff0000000000000);
    }
    else {
        size_t truncated_digits = significant_digits > TINYPY_DECIMAL_FAST_DIGITS ? significant_digits - TINYPY_DECIMAL_FAST_DIGITS : 0U;
        int64_t leading_exponent = decimal_exponent + (int64_t)truncated_digits;
        tinypy_bool_t decided = __tinypy_decimal_eisel_lemire(leading_significand, leading_exponent, &bits);

        if (decided != 0 && truncated_digits != 0U) {
            /* The leading digits bound the significand from below and their
               successor from above; the bounds rounding alike decide it. */
            uint64_t upper_bits;
            tinypy_bool_t upper_decided = __tinypy_decimal_eisel_lemire(leading_significand + 1U, leading_exponent, &upper_bits);

            decided = upper_decided != 0 && upper_bits == bits ? TINYPY_TRUE : TINYPY_FALSE;
        }
        if (decided == 0) {
            tinypy_bool_t converted = __tinypy_decimal_exact_bits(ctx, text, significand_begin, significand_end, decimal_exponent, &bits);

            if (converted == 0) {
                tinypy_bool_t limit_result = __tinypy_decimal_fail_limit(ctx, line_number, column_offset);
                return limit_result;
            }
        }
    }
    if (negative != 0) {
        bits |= UINT64_C(0x8000000000000000);
    }
    (void)memcpy(out_value, &bits, sizeof(bits));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_decimal_double(tinypy_vm_t *vm, const char *text, size_t size, double *out_value) {
    tinypy_compile_ctx_t ctx;
    tinypy_bool_t result;

    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.vm = vm;
    result = tinypy_internal_compiler_decimal_double(&ctx, text, size, out_value, 0, 0);
    tinypy_internal_compiler_arena_destroy(&ctx);
    return result;
}
