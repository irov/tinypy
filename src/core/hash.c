#include "tinypy/hash.h"

#include "internal.h"

#include <math.h>
#include <string.h>

#define TINYPY_COMPARE_RECURSION_LIMIT ((size_t)1000U)
#define TINYPY_LONG_DIGIT_MASK UINT16_C(0x7fff)
#define TINYPY_DOUBLE_LONG_DIGITS ((size_t)70U)

//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_internal_hash_fix(uint64_t value) {
    if (value == UINT64_MAX) {
        value = UINT64_MAX - UINT64_C(1);
    }
    return (tinypy_hash_t)value;
}

//////////////////////////////////////////////////////////////////////////
static uint64_t __tinypy_internal_rotate_left(uint64_t value, unsigned int amount) {
    amount &= 63U;
    if (amount == 0U) {
        return value;
    }
    return (value << amount) | (value >> (64U - amount));
}

//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_internal_hash_long(const tinypy_value_t *value) {
    const uint16_t *digits = TINYPY_LONG_OBJECT(value)->digits;
    size_t index = TINYPY_LONG_DIGIT_COUNT(value);
    uint64_t hash = UINT64_C(0);

    while (index != 0U) {
        uint64_t previous;

        index -= 1U;
        hash = __tinypy_internal_rotate_left(hash, 15U);
        previous = hash;
        hash += (uint64_t)digits[index];
        if (hash < previous) {
            hash += UINT64_C(1);
        }
    }
    if (TINYPY_LONG_SIGN(value) < 0) {
        hash = UINT64_C(0) - hash;
    }
    return __tinypy_internal_hash_fix(hash);
}

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_double_integer_digits(double value, int *out_sign, uint16_t digits[TINYPY_DOUBLE_LONG_DIGITS]) {
    uint64_t bits;
    uint64_t significand;
    unsigned int exponent_bits;
    int exponent;
    int shift;
    size_t digit_count;
    unsigned int bit;

    (void)memset(digits, 0, TINYPY_DOUBLE_LONG_DIGITS * sizeof(*digits));
    (void)memcpy(&bits, &value, sizeof(bits));
    *out_sign = (bits >> 63U) != 0U ? -1 : 1;
    exponent_bits = (unsigned int)((bits >> 52U) & UINT64_C(0x7ff));
    significand = bits & UINT64_C(0x000fffffffffffff);

    if (exponent_bits == 0x7ffU) {
        return SIZE_MAX;
    }
    if (exponent_bits == 0U) {
        if (significand == UINT64_C(0)) {
            *out_sign = 0;
            return 0U;
        }
        return SIZE_MAX;
    }

    significand |= UINT64_C(1) << 52U;
    exponent = (int)exponent_bits - 1023;
    shift = exponent - 52;
    if (shift < 0) {
        unsigned int right = (unsigned int)(-shift);
        uint64_t mask;

        if (right >= 64U) {
            return SIZE_MAX;
        }
        mask = (UINT64_C(1) << right) - UINT64_C(1);
        if ((significand & mask) != UINT64_C(0)) {
            return SIZE_MAX;
        }
        significand >>= right;
        shift = 0;
    }

    if (significand == UINT64_C(0)) {
        *out_sign = 0;
        return 0U;
    }

    digit_count = ((size_t)shift + 53U + 14U) / 15U;
    if (digit_count > TINYPY_DOUBLE_LONG_DIGITS) {
        return SIZE_MAX;
    }
    for (bit = 0U; bit < 53U; bit += 1U) {
        if ((significand & (UINT64_C(1) << bit)) != UINT64_C(0)) {
            size_t position = (size_t)shift + (size_t)bit;
            digits[position / 15U] |=
                (uint16_t)(UINT16_C(1) << (position % 15U));
        }
    }
    while (digit_count != 0U && digits[digit_count - 1U] == 0U) {
        digit_count -= 1U;
    }
    return digit_count;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_internal_hash_integral_double(double value) {
    uint16_t digits[TINYPY_DOUBLE_LONG_DIGITS];
    size_t digit_count;
    size_t index;
    uint64_t hash = UINT64_C(0);
    int sign;

    digit_count = __tinypy_internal_double_integer_digits(value, &sign, digits);
    if (digit_count == SIZE_MAX) {
        return (tinypy_hash_t)0;
    }
    index = digit_count;
    while (index != 0U) {
        uint64_t previous;

        index -= 1U;
        hash = __tinypy_internal_rotate_left(hash, 15U);
        previous = hash;
        hash += (uint64_t)digits[index];
        if (hash < previous) {
            hash += UINT64_C(1);
        }
    }
    if (sign < 0) {
        hash = UINT64_C(0) - hash;
    }
    return __tinypy_internal_hash_fix(hash);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_internal_hash_double(double value) {
    double integral;
    double fraction;
    double mantissa;
    int64_t high;
    int64_t low;
    uint64_t combined;
    int exponent;

    if (isinf(value)) {
        return value < 0.0 ? (tinypy_hash_t)-271828 : (tinypy_hash_t)314159;
    }
    if (isnan(value)) {
        return (tinypy_hash_t)0;
    }

    fraction = modf(value, &integral);
    if (fraction == 0.0) {
        return __tinypy_internal_hash_integral_double(integral);
    }

    mantissa = frexp(value, &exponent);
    mantissa *= 2147483648.0;
    high = (int64_t)mantissa;
    mantissa = (mantissa - (double)high) * 2147483648.0;
    low = (int64_t)mantissa;
    combined = (uint64_t)(tinypy_hash_t)high;
    combined += (uint64_t)(tinypy_hash_t)low;
    combined += (uint64_t)(tinypy_hash_t)((int64_t)exponent * INT64_C(32768));
    return __tinypy_internal_hash_fix(combined);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_internal_hash_bytes(const unsigned char *bytes, size_t size, uint64_t prefix, uint64_t suffix) {
    uint64_t hash;
    size_t index;

    if (size == 0U) {
        return (tinypy_hash_t)0;
    }
    hash = prefix ^ ((uint64_t)bytes[0] << 7U);
    for (index = 0U; index < size; index += 1U) {
        hash = (hash * UINT64_C(1000003)) ^ (uint64_t)bytes[index];
    }
    hash ^= (uint64_t)size;
    hash ^= suffix;
    return __tinypy_internal_hash_fix(hash);
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_internal_utf8_next(const unsigned char *bytes, size_t size, size_t *offset, uint32_t *out_code_point) {
    unsigned char first;
    uint32_t code_point;
    size_t width;
    size_t index;

    if (*offset >= size) {
        return 0;
    }
    first = bytes[*offset];
    if (first < 0x80U) {
        *out_code_point = (uint32_t)first;
        *offset += 1U;
        return 1;
    }
    if (first < 0xe0U) {
        code_point = (uint32_t)(first & 0x1fU);
        width = 2U;
    }
    else if (first < 0xf0U) {
        code_point = (uint32_t)(first & 0x0fU);
        width = 3U;
    }
    else {
        code_point = (uint32_t)(first & 0x07U);
        width = 4U;
    }
    if (width > size - *offset) {
        return 0;
    }
    for (index = 1U; index < width; index += 1U) {
        code_point = (code_point << 6U) | (uint32_t)(bytes[*offset + index] & 0x3fU);
    }
    *offset += width;
    *out_code_point = code_point;
    return 1;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_internal_hash_unicode(const tinypy_vm_t *vm, const tinypy_value_t *value) {
    const unsigned char *bytes = TINYPY_UNICODE_OBJECT(value)->utf8;
    size_t byte_size = TINYPY_UNICODE_OBJECT(value)->byte_size;
    size_t point_count = (size_t)TINYPY_SIZE(value);
    size_t offset = 0U;
    size_t index;
    uint32_t point;
    uint64_t hash;

    if (point_count == 0U) {
        return (tinypy_hash_t)0;
    }
    (void)__tinypy_internal_utf8_next(bytes, byte_size, &offset, &point);
    hash = vm->hash_secret_prefix ^ ((uint64_t)point << 7U);
    for (index = 0U; index < point_count; index += 1U) {
        if (index != 0U) {
            (void)__tinypy_internal_utf8_next(bytes, byte_size, &offset, &point);
        }
        hash = (hash * UINT64_C(1000003)) ^ (uint64_t)point;
    }
    hash ^= (uint64_t)point_count;
    hash ^= vm->hash_secret_suffix;
    return __tinypy_internal_hash_fix(hash);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_internal_hash_tuple(const tinypy_value_t *value) {
    tinypy_value_t *const *items = tinypy_internal_tuple_items(value);
    size_t remaining = (size_t)TINYPY_SIZE(value);
    size_t index = 0U;
    uint64_t hash = UINT64_C(0x345678);
    uint64_t multiplier = UINT64_C(1000003);
#ifndef NDEBUG
    tinypy_vm_t *vm = tinypy_internal_value_vm(value);
#endif

#ifndef NDEBUG
    assert(vm->hash_depth < TINYPY_COMPARE_RECURSION_LIMIT);
    vm->hash_depth += 1U;
#endif
    while (remaining != 0U) {
        tinypy_hash_t item_hash = tinypy_internal_hash_value(items[index]);

        remaining -= 1U;
        hash = (hash ^ (uint64_t)item_hash) * multiplier;
        multiplier += UINT64_C(82520) + (uint64_t)remaining + (uint64_t)remaining;
        index += 1U;
    }
#ifndef NDEBUG
    vm->hash_depth -= 1U;
#endif
    hash += UINT64_C(97531);
    return __tinypy_internal_hash_fix(hash);
}

//////////////////////////////////////////////////////////////////////////
tinypy_hash_t tinypy_internal_hash_value(const tinypy_value_t *value) {
    double real;

    switch (tinypy_internal_value_kind(value)) {
    case TINYPY_VALUE_NONE:
        return __tinypy_internal_hash_fix(
            (uint64_t)((uintptr_t)value >> 4U));
    case TINYPY_VALUE_BOOL:
        return (tinypy_hash_t)TINYPY_INTEGER_VALUE(value);
    case TINYPY_VALUE_INTEGER:
        return TINYPY_INTEGER_VALUE(value) == INT64_C(-1)
                   ? (tinypy_hash_t)-2
                   : (tinypy_hash_t)TINYPY_INTEGER_VALUE(value);
    case TINYPY_VALUE_LONG:
        return __tinypy_internal_hash_long(value);
    case TINYPY_VALUE_FLOAT:
        real = TINYPY_FLOAT_OBJECT(value)->value;
        return __tinypy_internal_hash_double(real);
    case TINYPY_VALUE_COMPLEX: {
        tinypy_hash_t real_hash = __tinypy_internal_hash_double(
            TINYPY_COMPLEX_OBJECT(value)->real);
        tinypy_hash_t imaginary_hash = __tinypy_internal_hash_double(
            TINYPY_COMPLEX_OBJECT(value)->imaginary);
        uint64_t combined = (uint64_t)real_hash + UINT64_C(1000003) * (uint64_t)imaginary_hash;

        return __tinypy_internal_hash_fix(combined);
    }
    case TINYPY_VALUE_STRING: {
        const tinypy_vm_t *vm = tinypy_internal_value_vm(value);

        return __tinypy_internal_hash_bytes(
            TINYPY_STRING_OBJECT(value)->bytes,
            (size_t)TINYPY_SIZE(value),
            vm->hash_secret_prefix,
            vm->hash_secret_suffix);
    }
    case TINYPY_VALUE_UNICODE: {
        tinypy_vm_t *vm = tinypy_internal_value_vm(value);
        return __tinypy_internal_hash_unicode(vm, value);
    }
    case TINYPY_VALUE_BUFFER: {
        const tinypy_vm_t *vm = tinypy_internal_value_vm(value);
        size_t size;
        const unsigned char *bytes = (const unsigned char *)tinypy_buffer_view(value, &size);

        return __tinypy_internal_hash_bytes(bytes, size, vm->hash_secret_prefix, vm->hash_secret_suffix);
    }
    case TINYPY_VALUE_TUPLE:
        return __tinypy_internal_hash_tuple(value);
    case TINYPY_VALUE_FROZENSET:
        return tinypy_internal_frozenset_hash(value);
    case TINYPY_VALUE_WEAKREF: {
        tinypy_weakref_object_t *weakref = TINYPY_WEAKREF_OBJECT((tinypy_value_t *)value);

        if (weakref->hash_computed == 0) {
            if (weakref->object == NULL) {
                return __tinypy_internal_hash_fix((uint64_t)((uintptr_t)value >> 4U));
            }
            weakref->hash = tinypy_internal_hash_value(weakref->object);
            weakref->hash_computed = INT32_C(1);
        }
        return weakref->hash;
    }
    case TINYPY_VALUE_LIST:
    case TINYPY_VALUE_DICT:
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_BYTEARRAY:
        assert(0 && "value is not hashable");
        return (tinypy_hash_t)0;
    default:
        return __tinypy_internal_hash_fix(
            (uint64_t)((uintptr_t)value >> 4U));
    }
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_internal_value_is_numeric(const tinypy_value_t *value) {
    return tinypy_internal_value_kind(value) == TINYPY_VALUE_BOOL || tinypy_internal_value_kind(value) == TINYPY_VALUE_INTEGER || tinypy_internal_value_kind(value) == TINYPY_VALUE_LONG || tinypy_internal_value_kind(value) == TINYPY_VALUE_FLOAT || tinypy_internal_value_kind(value) == TINYPY_VALUE_COMPLEX;
}

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_integer_digits(const tinypy_value_t *value, int *out_sign, uint16_t local_digits[5], const uint16_t **out_digits) {
    uint64_t magnitude;
    size_t count = 0U;
    int64_t integer;

    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_LONG) {
        *out_sign = TINYPY_LONG_SIGN(value);
        *out_digits = TINYPY_LONG_OBJECT(value)->digits;
        return TINYPY_LONG_DIGIT_COUNT(value);
    }

    integer = tinypy_internal_value_kind(value) == TINYPY_VALUE_BOOL
                  ? TINYPY_INTEGER_VALUE(value)
                  : TINYPY_INTEGER_VALUE(value);
    if (integer == INT64_C(0)) {
        *out_sign = 0;
        *out_digits = local_digits;
        return 0U;
    }
    if (integer < INT64_C(0)) {
        *out_sign = -1;
        magnitude = (uint64_t)(-(integer + INT64_C(1))) + UINT64_C(1);
    }
    else {
        *out_sign = 1;
        magnitude = (uint64_t)integer;
    }
    while (magnitude != UINT64_C(0)) {
        local_digits[count] = (uint16_t)(magnitude & TINYPY_LONG_DIGIT_MASK);
        count += 1U;
        magnitude >>= 15U;
    }
    *out_digits = local_digits;
    return count;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_internal_integer_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    uint16_t left_local[5];
    uint16_t right_local[5];
    const uint16_t *left_digits;
    const uint16_t *right_digits;
    size_t left_count;
    size_t right_count;
    int left_sign;
    int right_sign;

    left_count = __tinypy_internal_integer_digits(
        left, &left_sign, left_local, &left_digits);
    right_count = __tinypy_internal_integer_digits(
        right, &right_sign, right_local, &right_digits);
    return left_sign == right_sign && left_count == right_count && (left_count == 0U || memcmp(
                left_digits,
                right_digits,
                left_count * sizeof(*left_digits)) == 0);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_compare_digit_magnitudes(const uint16_t *left, size_t left_count, const uint16_t *right, size_t right_count) {
    size_t index;

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
static int32_t __tinypy_internal_integer_order(const tinypy_value_t *left, const tinypy_value_t *right) {
    uint16_t left_local[5];
    uint16_t right_local[5];
    const uint16_t *left_digits;
    const uint16_t *right_digits;
    size_t left_count;
    size_t right_count;
    int left_sign;
    int right_sign;
    int32_t magnitude_order;

    left_count = __tinypy_internal_integer_digits(left, &left_sign, left_local, &left_digits);
    right_count = __tinypy_internal_integer_digits(right, &right_sign, right_local, &right_digits);
    if (left_sign != right_sign) {
        return left_sign < right_sign ? -1 : 1;
    }
    magnitude_order = __tinypy_internal_compare_digit_magnitudes(left_digits, left_count, right_digits, right_count);
    return left_sign < 0 ? -magnitude_order : magnitude_order;
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_integer_double_order(const tinypy_value_t *integer, double floating) {
    uint16_t integer_local[5];
    uint16_t floating_digits[TINYPY_DOUBLE_LONG_DIGITS];
    const uint16_t *integer_digits;
    double absolute;
    double integral;
    size_t integer_count;
    size_t floating_count;
    int integer_sign;
    int floating_sign;
    int32_t magnitude_order;

    integer_count = __tinypy_internal_integer_digits(integer, &integer_sign, integer_local, &integer_digits);
    if (isinf(floating)) {
        return floating < 0.0 ? 1 : -1;
    }
    floating_sign = floating < 0.0 ? -1 : (floating > 0.0 ? 1 : 0);
    if (integer_sign != floating_sign) {
        return integer_sign < floating_sign ? -1 : 1;
    }
    if (integer_sign == 0) {
        return 0;
    }
    absolute = fabs(floating);
    integral = floor(absolute);
    floating_count = __tinypy_internal_double_integer_digits(integral, &floating_sign, floating_digits);
    assert(floating_count != SIZE_MAX);
    magnitude_order = __tinypy_internal_compare_digit_magnitudes(integer_digits, integer_count, floating_digits, floating_count);
    if (magnitude_order == 0 && absolute != integral) {
        magnitude_order = -1;
    }
    return integer_sign < 0 ? -magnitude_order : magnitude_order;
}

//////////////////////////////////////////////////////////////////////////
int tinypy_internal_numeric_order(const tinypy_value_t *left, const tinypy_value_t *right, int32_t *out_order) {
    tinypy_value_type_e left_kind = tinypy_internal_value_kind(left);
    tinypy_value_type_e right_kind = tinypy_internal_value_kind(right);
    int left_integer = left_kind == TINYPY_VALUE_BOOL || left_kind == TINYPY_VALUE_INTEGER || left_kind == TINYPY_VALUE_LONG;
    int right_integer = right_kind == TINYPY_VALUE_BOOL || right_kind == TINYPY_VALUE_INTEGER || right_kind == TINYPY_VALUE_LONG;

    assert(out_order != NULL);
    if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
        return 0;
    }
    if (left_integer != 0 && right_integer != 0) {
        *out_order = __tinypy_internal_integer_order(left, right);
        return 1;
    }
    if (left_integer != 0) {
        double floating = TINYPY_FLOAT_OBJECT(right)->value;

        if (isnan(floating)) {
            return 0;
        }
        *out_order = __tinypy_internal_integer_double_order(left, floating);
        return 1;
    }
    if (right_integer != 0) {
        double floating = TINYPY_FLOAT_OBJECT(left)->value;

        if (isnan(floating)) {
            return 0;
        }
        *out_order = -__tinypy_internal_integer_double_order(right, floating);
        return 1;
    }
    if (isnan(TINYPY_FLOAT_OBJECT(left)->value) || isnan(TINYPY_FLOAT_OBJECT(right)->value)) {
        return 0;
    }
    *out_order = TINYPY_FLOAT_OBJECT(left)->value < TINYPY_FLOAT_OBJECT(right)->value ? -1 : (TINYPY_FLOAT_OBJECT(left)->value > TINYPY_FLOAT_OBJECT(right)->value ? 1 : 0);
    return 1;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_internal_integer_equal_double(const tinypy_value_t *integer, double floating) {
    uint16_t local[5];
    uint16_t double_digits[TINYPY_DOUBLE_LONG_DIGITS];
    const uint16_t *integer_digits;
    size_t integer_count;
    size_t double_count;
    int integer_sign;
    int double_sign;

    double_count = __tinypy_internal_double_integer_digits(
        floating, &double_sign, double_digits);
    if (double_count == SIZE_MAX) {
        return 0;
    }
    integer_count = __tinypy_internal_integer_digits(
        integer, &integer_sign, local, &integer_digits);
    return integer_sign == double_sign && integer_count == double_count && (integer_count == 0U || memcmp(
                integer_digits,
                double_digits,
                integer_count * sizeof(*integer_digits)) == 0);
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_internal_numeric_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    tinypy_value_type_e left_kind = tinypy_internal_value_kind(left);
    tinypy_value_type_e right_kind = tinypy_internal_value_kind(right);
    int left_integer = tinypy_internal_value_kind(left) == TINYPY_VALUE_BOOL || tinypy_internal_value_kind(left) == TINYPY_VALUE_INTEGER || tinypy_internal_value_kind(left) == TINYPY_VALUE_LONG;
    int right_integer = tinypy_internal_value_kind(right) == TINYPY_VALUE_BOOL || tinypy_internal_value_kind(right) == TINYPY_VALUE_INTEGER || tinypy_internal_value_kind(right) == TINYPY_VALUE_LONG;
    double left_real;
    double right_real;

    if (left_kind == TINYPY_VALUE_COMPLEX && right_kind == TINYPY_VALUE_COMPLEX) {
        return TINYPY_COMPLEX_OBJECT(left)->real ==
                   TINYPY_COMPLEX_OBJECT(right)->real && TINYPY_COMPLEX_OBJECT(left)->imaginary ==
                   TINYPY_COMPLEX_OBJECT(right)->imaginary;
    }
    if (left_kind == TINYPY_VALUE_COMPLEX) {
        if (TINYPY_COMPLEX_OBJECT(left)->imaginary != 0.0) {
            return 0;
        }
        if (right_integer) {
            return __tinypy_internal_integer_equal_double(
                right, TINYPY_COMPLEX_OBJECT(left)->real);
        }
        assert(right_kind == TINYPY_VALUE_FLOAT);
        return TINYPY_COMPLEX_OBJECT(left)->real ==
               TINYPY_FLOAT_OBJECT(right)->value;
    }
    if (right_kind == TINYPY_VALUE_COMPLEX) {
        if (TINYPY_COMPLEX_OBJECT(right)->imaginary != 0.0) {
            return 0;
        }
        if (left_integer) {
            return __tinypy_internal_integer_equal_double(
                left, TINYPY_COMPLEX_OBJECT(right)->real);
        }
        assert(left_kind == TINYPY_VALUE_FLOAT);
        return TINYPY_FLOAT_OBJECT(left)->value ==
               TINYPY_COMPLEX_OBJECT(right)->real;
    }

    if (left_integer && right_integer) {
        return __tinypy_internal_integer_equal(left, right);
    }
    if (left_integer && tinypy_internal_value_kind(right) == TINYPY_VALUE_FLOAT) {
        right_real = TINYPY_FLOAT_OBJECT(right)->value;
        return __tinypy_internal_integer_equal_double(left, right_real);
    }
    if (right_integer && tinypy_internal_value_kind(left) == TINYPY_VALUE_FLOAT) {
        left_real = TINYPY_FLOAT_OBJECT(left)->value;
        return __tinypy_internal_integer_equal_double(right, left_real);
    }

    assert(tinypy_internal_value_kind(left) == TINYPY_VALUE_FLOAT);
    assert(tinypy_internal_value_kind(right) == TINYPY_VALUE_FLOAT);
    left_real = TINYPY_FLOAT_OBJECT(left)->value;
    right_real = TINYPY_FLOAT_OBJECT(right)->value;
    return left_real == right_real;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_internal_text_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    const unsigned char *left_bytes = tinypy_internal_text_bytes(left);
    const unsigned char *right_bytes = tinypy_internal_text_bytes(right);
    size_t left_size = tinypy_internal_text_byte_size(left);
    size_t right_size = tinypy_internal_text_byte_size(right);
    size_t index;

    if (left->type != right->type) {
        const tinypy_value_t *string = tinypy_internal_value_kind(left) == TINYPY_VALUE_STRING ? left : right;
        const unsigned char *string_bytes = tinypy_internal_text_bytes(string);

        for (index = 0U;
             index < tinypy_internal_text_byte_size(string);
             index += 1U) {
            if (string_bytes[index] >= 0x80U) {
                return 0;
            }
        }
    }
    return left_size == right_size && (left_size == 0U || memcmp(left_bytes, right_bytes, left_size) == 0);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_bytes_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    const unsigned char *left_bytes;
    const unsigned char *right_bytes;
    size_t left_size;
    size_t right_size;

    if (tinypy_internal_bytes_view(left, &left_bytes, &left_size) == 0 || tinypy_internal_bytes_view(right, &right_bytes, &right_size) == 0) {
        return INT32_C(0);
    }
    return left_size == right_size && (left_size == 0U || memcmp(left_bytes, right_bytes, left_size) == 0) ? INT32_C(1) : INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_text_order(const tinypy_value_t *left, const tinypy_value_t *right) {
    const unsigned char *left_bytes = tinypy_internal_text_bytes(left);
    const unsigned char *right_bytes = tinypy_internal_text_bytes(right);
    size_t left_size = tinypy_internal_text_byte_size(left);
    size_t right_size = tinypy_internal_text_byte_size(right);
    size_t common_size = left_size < right_size ? left_size : right_size;
    int comparison = common_size != 0U ? memcmp(left_bytes, right_bytes, common_size) : 0;

    if (comparison != 0) {
        return comparison < 0 ? -1 : 1;
    }
    return left_size < right_size ? -1 : (left_size > right_size ? 1 : 0);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_sequence_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    tinypy_value_t *const *left_items;
    tinypy_value_t *const *right_items;
    size_t left_size;
    size_t right_size;
    size_t index;
#ifndef NDEBUG
    tinypy_vm_t *vm;
#endif

    if (tinypy_internal_value_kind(left) != tinypy_internal_value_kind(right)) {
        return 0;
    }
    if (tinypy_internal_value_kind(left) == TINYPY_VALUE_TUPLE) {
        left_items = tinypy_internal_tuple_items(left);
        right_items = tinypy_internal_tuple_items(right);
        left_size = (size_t)TINYPY_SIZE(left);
        right_size = (size_t)TINYPY_SIZE(right);
    }
    else {
        left_items = TINYPY_LIST_OBJECT(left)->items;
        right_items = TINYPY_LIST_OBJECT(right)->items;
        left_size = (size_t)TINYPY_SIZE(left);
        right_size = (size_t)TINYPY_SIZE(right);
    }
    if (left_size != right_size) {
        return 0;
    }
#ifndef NDEBUG
    vm = tinypy_internal_value_vm(left);
    assert(vm->equality_depth < TINYPY_COMPARE_RECURSION_LIMIT);
    vm->equality_depth += 1U;
#endif
    for (index = 0U; index < left_size; index += 1U) {
        if (tinypy_internal_equal_value(
                left_items[index], right_items[index], 1) == 0) {
#ifndef NDEBUG
            vm->equality_depth -= 1U;
#endif
            return 0;
        }
    }
#ifndef NDEBUG
    vm->equality_depth -= 1U;
#endif
    return 1;
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_equal_value(const tinypy_value_t *left, const tinypy_value_t *right, int identity_implies_equal) {
    if (left == right && identity_implies_equal) {
        return 1;
    }
    if (__tinypy_internal_value_is_numeric(left) && __tinypy_internal_value_is_numeric(right)) {
        return (int32_t)__tinypy_internal_numeric_equal(left, right);
    }
    if ((tinypy_internal_value_kind(left) == TINYPY_VALUE_STRING || tinypy_internal_value_kind(left) == TINYPY_VALUE_UNICODE) && (tinypy_internal_value_kind(right) == TINYPY_VALUE_STRING || tinypy_internal_value_kind(right) == TINYPY_VALUE_UNICODE)) {
        return (int32_t)__tinypy_internal_text_equal(left, right);
    }
    if (tinypy_internal_value_kind(left) == TINYPY_VALUE_BYTEARRAY || tinypy_internal_value_kind(right) == TINYPY_VALUE_BYTEARRAY || tinypy_internal_value_kind(left) == TINYPY_VALUE_BUFFER || tinypy_internal_value_kind(right) == TINYPY_VALUE_BUFFER) {
        return __tinypy_internal_bytes_equal(left, right);
    }
    if ((tinypy_internal_value_kind(left) == TINYPY_VALUE_TUPLE || tinypy_internal_value_kind(left) == TINYPY_VALUE_LIST) && (tinypy_internal_value_kind(right) == TINYPY_VALUE_TUPLE || tinypy_internal_value_kind(right) == TINYPY_VALUE_LIST)) {
        return __tinypy_internal_sequence_equal(left, right);
    }
    if (tinypy_internal_value_kind(left) == TINYPY_VALUE_DICT && tinypy_internal_value_kind(right) == TINYPY_VALUE_DICT) {
        return tinypy_internal_dict_equal(left, right);
    }
    if ((tinypy_internal_value_kind(left) == TINYPY_VALUE_SET || tinypy_internal_value_kind(left) == TINYPY_VALUE_FROZENSET) && (tinypy_internal_value_kind(right) == TINYPY_VALUE_SET || tinypy_internal_value_kind(right) == TINYPY_VALUE_FROZENSET)) {
        return tinypy_internal_set_equal(left, right);
    }
    if (tinypy_internal_value_kind(left) == TINYPY_VALUE_WEAKREF && tinypy_internal_value_kind(right) == TINYPY_VALUE_WEAKREF) {
        tinypy_value_t *left_object = TINYPY_WEAKREF_OBJECT((tinypy_value_t *)left)->object;
        tinypy_value_t *right_object = TINYPY_WEAKREF_OBJECT((tinypy_value_t *)right)->object;

        if (left_object == NULL || right_object == NULL) {
            return left == right ? INT32_C(1) : INT32_C(0);
        }
        return tinypy_internal_equal_value(left_object, right_object, 1);
    }
    return left == right ? 1 : 0;
}

//////////////////////////////////////////////////////////////////////////
tinypy_hash_t tinypy_hash(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));

    return tinypy_internal_hash_value(value);
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    assert(left != NULL);
    assert(right != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(left)));
    assert(tinypy_internal_value_belongs_to(
        tinypy_internal_value_vm(left), right));

    return tinypy_internal_equal_value(left, right, 0);
}
