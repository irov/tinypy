#include "tinypy/hash.h"

#include "internal.h"

#include <math.h>
#include <string.h>

#define TINYPY_LONG_DIGIT_MASK UINT16_C(0x7fff)
#define TINYPY_DOUBLE_LONG_DIGITS ((size_t)70U)
#define TINYPY_INTERNAL_HASH_TUPLE_INLINE_FRAMES 16U

//////////////////////////////////////////////////////////////////////////
static inline tinypy_hash_t __tinypy_internal_hash_fix(uint64_t value) {
    if (value == UINT64_MAX) {
        value = UINT64_MAX - UINT64_C(1);
    }
    return (tinypy_hash_t)value;
}
//////////////////////////////////////////////////////////////////////////
static inline uint64_t __tinypy_internal_rotate_left(uint64_t value, uint32_t amount) {
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
    tinypy_hash_t return_value_1 = __tinypy_internal_hash_fix(hash);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_double_integer_digits(double value, int32_t *out_sign, uint16_t digits[TINYPY_DOUBLE_LONG_DIGITS]) {
    uint64_t bits;
    uint64_t significand;
    uint32_t exponent_bits;
    int32_t exponent;
    int32_t shift;
    size_t digit_count;
    uint32_t bit;

    (void)memset(digits, 0, TINYPY_DOUBLE_LONG_DIGITS * sizeof(*digits));
    (void)memcpy(&bits, &value, sizeof(bits));
    *out_sign = (bits >> 63U) != 0U ? -1 : 1;
    exponent_bits = (uint32_t)((bits >> 52U) & UINT64_C(0x7ff));
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
    exponent = (int32_t)exponent_bits - 1023;
    shift = exponent - 52;
    if (shift < 0) {
        uint32_t right = (uint32_t)(-shift);
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
    for (bit = 0U; bit < 53U; ++bit) {
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
    int32_t sign;

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
    tinypy_hash_t return_value_1 = __tinypy_internal_hash_fix(hash);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_internal_hash_double(double value) {
    double integral;
    double fraction;
    double mantissa;
    int64_t high;
    int64_t low;
    uint64_t combined;
    int32_t exponent;

    if (isinf(value)) {
        return value < 0.0 ? (tinypy_hash_t)-271828 : (tinypy_hash_t)314159;
    }
    if (isnan(value)) {
        return (tinypy_hash_t)0;
    }

    fraction = modf(value, &integral);
    if (fraction == 0.0) {
        tinypy_hash_t return_value_1 = __tinypy_internal_hash_integral_double(integral);
        return return_value_1;
    }

    mantissa = frexp(value, &exponent);
    mantissa *= 2147483648.0;
    high = (int64_t)mantissa;
    mantissa = (mantissa - (double)high) * 2147483648.0;
    low = (int64_t)mantissa;
    combined = (uint64_t)(tinypy_hash_t)high;
    combined += (uint64_t)(tinypy_hash_t)low;
    combined += (uint64_t)(tinypy_hash_t)((int64_t)exponent * INT64_C(32768));
    tinypy_hash_t return_value_2 = __tinypy_internal_hash_fix(combined);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_hash_t tinypy_internal_hash_bytes(const tinypy_vm_t *vm, const uint8_t *bytes, size_t size) {
    uint64_t hash;
    size_t index;

    if (size == 0U) {
        return (tinypy_hash_t)0;
    }
    hash = vm->hash_secret_prefix ^ ((uint64_t)bytes[0] << 7U);
    for (index = 0U; index < size; ++index) {
        hash = (hash * UINT64_C(1000003)) ^ (uint64_t)bytes[index];
    }
    hash ^= (uint64_t)size;
    hash ^= vm->hash_secret_suffix;
    tinypy_hash_t return_value_1 = __tinypy_internal_hash_fix(hash);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_utf8_next(const uint8_t *bytes, size_t size, size_t *offset, uint32_t *out_code_point) {
    uint8_t first;
    uint32_t code_point;
    size_t width;
    size_t index;

    if (*offset >= size) {
        return TINYPY_FALSE;
    }
    first = bytes[*offset];
    if (first < 0x80U) {
        *out_code_point = (uint32_t)first;
        *offset += 1U;
        return TINYPY_TRUE;
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
        return TINYPY_FALSE;
    }
    for (index = 1U; index < width; ++index) {
        code_point = (code_point << 6U) | (uint32_t)(bytes[*offset + index] & 0x3fU);
    }
    *offset += width;
    *out_code_point = code_point;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_internal_hash_unicode(const tinypy_vm_t *vm, const tinypy_value_t *value) {
    const uint8_t *bytes = TINYPY_UNICODE_OBJECT(value)->utf8;
    size_t byte_size = TINYPY_UNICODE_OBJECT(value)->byte_size;
    size_t point_count = TINYPY_SIZED_SIZE(value);
    size_t offset = 0U;
    size_t index;
    uint32_t point;
    uint64_t hash;

    if (point_count == 0U) {
        return (tinypy_hash_t)0;
    }
    if (__tinypy_internal_utf8_next(bytes, byte_size, &offset, &point) == TINYPY_FALSE) {
        return (tinypy_hash_t)0;
    }
    hash = vm->hash_secret_prefix ^ ((uint64_t)point << 7U);
    for (index = 0U; index < point_count; ++index) {
        if (index != 0U) {
            if (__tinypy_internal_utf8_next(bytes, byte_size, &offset, &point) == TINYPY_FALSE) {
                return (tinypy_hash_t)0;
            }
        }
        hash = (hash * UINT64_C(1000003)) ^ (uint64_t)point;
    }
    hash ^= (uint64_t)point_count;
    hash ^= vm->hash_secret_suffix;
    tinypy_hash_t return_value_1 = __tinypy_internal_hash_fix(hash);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_internal_hash_tuple_frame_t {
    const tinypy_value_t *tuple;
    size_t index;
    uint64_t hash;
    uint64_t multiplier;
} tinypy_internal_hash_tuple_frame_t;
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_hash_tuple_begin(tinypy_internal_hash_tuple_frame_t *frame, const tinypy_value_t *tuple) {
    frame->tuple = tuple;
    frame->index = 0U;
    frame->hash = UINT64_C(0x345678);
    frame->multiplier = UINT64_C(1000003);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_hash_tuple_fold(tinypy_internal_hash_tuple_frame_t *frame, tinypy_hash_t item_hash) {
    size_t remaining = TINYPY_SIZED_SIZE(frame->tuple) - frame->index - 1U;

    frame->hash = (frame->hash ^ (uint64_t)item_hash) * frame->multiplier;
    frame->multiplier += UINT64_C(82520) + (uint64_t)remaining + (uint64_t)remaining;
    frame->index += 1U;
}
//////////////////////////////////////////////////////////////////////////
/* tuplehash; exact tuples nested in a tuple are hashed with an explicit stack
   rather than C recursion, so deep nesting cannot exhaust the native stack. */
static tinypy_hash_t __tinypy_internal_hash_tuple(const tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_hash_tuple_frame_t inline_frames[TINYPY_INTERNAL_HASH_TUPLE_INLINE_FRAMES];
    tinypy_internal_hash_tuple_frame_t *frames = inline_frames;
    size_t capacity = TINYPY_INTERNAL_HASH_TUPLE_INLINE_FRAMES;
    size_t depth = 1U;
    tinypy_hash_t hash = (tinypy_hash_t)0;

    __tinypy_internal_hash_tuple_begin(&frames[0], value);
    while (depth != 0U) {
        tinypy_internal_hash_tuple_frame_t *frame = &frames[depth - 1U];

        if (frame->index == TINYPY_SIZED_SIZE(frame->tuple)) {
            hash = __tinypy_internal_hash_fix(frame->hash + UINT64_C(97531));
            depth -= 1U;
            if (depth != 0U) {
                __tinypy_internal_hash_tuple_fold(&frames[depth - 1U], hash);
            }
            continue;
        }
        const tinypy_value_t *item = tinypy_internal_tuple_items(frame->tuple)[frame->index];
        if (item->type != &vm->types[TINYPY_VALUE_TUPLE]) {
            tinypy_value_t *previous_raised = vm->raised_value;
            tinypy_hash_t item_hash = tinypy_internal_hash_value(item, out_error);

            if ((out_error != NULL && *out_error != NULL) || vm->raised_value != previous_raised) {
                hash = (tinypy_hash_t)0;
                break;
            }
            __tinypy_internal_hash_tuple_fold(frame, item_hash);
            continue;
        }
        if (depth == capacity) {
            size_t size = capacity * sizeof(*frames);
            tinypy_internal_hash_tuple_frame_t *grown = frames == inline_frames
                                                            ? (tinypy_internal_hash_tuple_frame_t *)tinypy_internal_vm_allocate_checked(vm, size * 2U, out_error)
                                                            : (tinypy_internal_hash_tuple_frame_t *)tinypy_internal_vm_reallocate_checked(vm, frames, size, size * 2U, out_error);

            if (grown == NULL) {
                hash = (tinypy_hash_t)0;
                break;
            }
            if (frames == inline_frames) {
                (void)memcpy(grown, inline_frames, size);
            }
            frames = grown;
            capacity *= 2U;
        }
        __tinypy_internal_hash_tuple_begin(&frames[depth], item);
        depth += 1U;
    }
    if (frames != inline_frames) {
        tinypy_internal_vm_deallocate(vm, frames, capacity * sizeof(*frames));
    }
    return hash;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_hash_special(const tinypy_value_t *value, tinypy_bool_t overrides_only, tinypy_hash_t *out_hash, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *mutable_value = (tinypy_value_t *)value;

    if (overrides_only != 0 && __tinypy_internal_object_overrides_dispatch(mutable_value, TINYPY_INTERNAL_DISPATCH_BIT(HASH)) == 0) {
        return INT32_C(0);
    }
    tinypy_value_t *function = tinypy_internal_type_function_key(mutable_value, vm->internal_special_hash_key);
    tinypy_value_t *result;

    if (function != NULL) {
        result = tinypy_internal_call_type_function(function, mutable_value, NULL, 0U, out_error);
    }
    else {
        tinypy_value_t *method;
        int32_t found = tinypy_internal_object_lookup_special_key(mutable_value, vm->internal_special_hash_key, &method, out_error);

        if (found <= 0) {
            return found;
        }
        if (TINYPY_VALUE_KIND(method) == TINYPY_VALUE_NONE && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_OLD_INSTANCE) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("unhashable type: '"),
                TINYPY_MESSAGE_PART_TYPE_NAME(mutable_value),
                TINYPY_MESSAGE_PART_LITERAL("'"),
            };

            TINYPY_DECREF(method);
            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
            return INT32_C(-1);
        }
        tinypy_value_t *empty = TINYPY_RET_EMPTY_TUPLE(vm);
        result = tinypy_call(method, empty, NULL, out_error);
        TINYPY_DECREF(empty);
        TINYPY_DECREF(method);
    }
    if (result == NULL) {
        return INT32_C(-1);
    }
    tinypy_value_type_e result_kind = TINYPY_VALUE_KIND(result);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE && result_kind != TINYPY_VALUE_BOOL && result_kind != TINYPY_VALUE_INTEGER && result_kind != TINYPY_VALUE_LONG) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__hash__() should return an int", out_error);
        return INT32_C(-1);
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        *out_hash = tinypy_internal_hash_value(result, out_error);
    }
    else if (result_kind == TINYPY_VALUE_LONG) {
        *out_hash = tinypy_internal_hash_builtin_value(result, out_error);
    }
    else {
        int64_t integer;
        if (tinypy_internal_number_as_i64(result, &integer, out_error) == 0) {
            TINYPY_DECREF(result);
            return INT32_C(-1);
        }
        *out_hash = integer == INT64_C(-1) ? (tinypy_hash_t)-2 : (tinypy_hash_t)integer;
    }
    TINYPY_DECREF(result);
    tinypy_bool_t failed = out_error != NULL && *out_error != NULL;

    return failed != 0 ? INT32_C(-1) : INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
/* instance_hash: a classic instance with __eq__ or __cmp__, looked up in that
   order, needs its own __hash__. */
static int32_t __tinypy_internal_hash_classic_comparable(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *const names[] = {vm->internal_special_eq_key, vm->internal_special_cmp_key};

    for (size_t index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        tinypy_value_t *method;
        int32_t found = tinypy_internal_object_lookup_special_key(value, names[index], &method, out_error);

        if (found > 0) {
            TINYPY_DECREF(method);
        }
        if (found != 0) {
            return found;
        }
    }
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
tinypy_hash_t tinypy_internal_hash_value(const tinypy_value_t *value, tinypy_error_t **out_error) {
    if ((value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U) {
        tinypy_hash_t hash;
        int32_t special = __tinypy_internal_hash_special(value, TINYPY_TRUE, &hash, out_error);

        if (special > 0) {
            return hash;
        }
        if (special < 0) {
            return (tinypy_hash_t)0;
        }
    }
    if (value->type->hash != NULL) {
        tinypy_hash_t return_value_1 = value->type->hash((tinypy_value_t *)value, out_error);
        return return_value_1;
    }
    tinypy_hash_t return_value_1 = tinypy_internal_hash_builtin_value(value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_hash_t tinypy_internal_hash_builtin_value(const tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_hash_t function_result;
    double real;

    switch (TINYPY_VALUE_KIND(value)) {
    case TINYPY_VALUE_NONE:
        function_result = __tinypy_internal_hash_fix(
                    (uint64_t)((uintptr_t)value >> 4U));
        return function_result;
    case TINYPY_VALUE_BOOL:
        function_result = (tinypy_hash_t)TINYPY_INTEGER_VALUE(value);
        return function_result;
    case TINYPY_VALUE_INTEGER:
        function_result = TINYPY_INTEGER_VALUE(value) == INT64_C(-1)
                           ? (tinypy_hash_t)-2
                           : (tinypy_hash_t)TINYPY_INTEGER_VALUE(value);
        return function_result;
    case TINYPY_VALUE_LONG:
        function_result = __tinypy_internal_hash_long(value);
        return function_result;
    case TINYPY_VALUE_FLOAT:
        real = TINYPY_FLOAT_OBJECT(value)->value;
        tinypy_hash_t return_value_2 = __tinypy_internal_hash_double(real);
        return return_value_2;
    case TINYPY_VALUE_COMPLEX: {
        tinypy_hash_t real_hash = __tinypy_internal_hash_double(
            TINYPY_COMPLEX_OBJECT(value)->real);
        tinypy_hash_t imaginary_hash = __tinypy_internal_hash_double(
            TINYPY_COMPLEX_OBJECT(value)->imaginary);
        uint64_t combined = (uint64_t)real_hash + UINT64_C(1000003) * (uint64_t)imaginary_hash;

        tinypy_hash_t return_value_3 = __tinypy_internal_hash_fix(combined);
        return return_value_3;
    }
    case TINYPY_VALUE_STRING: {
        const tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_string_object_t *string = TINYPY_STRING_OBJECT((tinypy_value_t *)value);

        if (string->hash_computed == 0) {
            string->hash = tinypy_internal_hash_bytes(vm, string->bytes, TINYPY_SIZED_SIZE(value));
            string->hash_computed = INT32_C(1);
        }
        return string->hash;
    }
    case TINYPY_VALUE_UNICODE: {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_unicode_object_t *unicode = TINYPY_UNICODE_OBJECT((tinypy_value_t *)value);

        if (unicode->hash_computed == 0) {
            unicode->hash = __tinypy_internal_hash_unicode(vm, value);
            unicode->hash_computed = INT32_C(1);
        }
        return unicode->hash;
    }
    case TINYPY_VALUE_BUFFER: {
        const tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_buffer_object_t *buffer = TINYPY_BUFFER_OBJECT((tinypy_value_t *)value);

        if (buffer->hash_computed == 0) {
            size_t size;
            const uint8_t *bytes = (const uint8_t *)tinypy_buffer_view(value, &size);

            buffer->hash = tinypy_internal_hash_bytes(vm, bytes, size);
            buffer->hash_computed = TINYPY_TRUE;
        }
        return buffer->hash;
    }
    case TINYPY_VALUE_TUPLE:
        function_result = __tinypy_internal_hash_tuple(value, out_error);
        return function_result;
    case TINYPY_VALUE_FROZENSET:
        function_result = tinypy_internal_frozenset_hash(value);
        return function_result;
    case TINYPY_VALUE_WEAKREF: {
        tinypy_hash_t return_value_5 = tinypy_internal_weakref_hash((tinypy_value_t *)value, out_error);
        return return_value_5;
    }
    case TINYPY_VALUE_METHOD: {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT((tinypy_value_t *)value);
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_value_t *previous_raised = vm->raised_value;
        tinypy_hash_t function_hash = tinypy_internal_hash_value(method->function, out_error);

        if ((out_error != NULL && *out_error != NULL) || vm->raised_value != previous_raised) {
            return (tinypy_hash_t)0;
        }
        tinypy_value_t *self = method->self != NULL ? method->self : &vm->none_object.base;
        tinypy_hash_t self_hash = tinypy_internal_hash_value(self, out_error);

        if ((out_error != NULL && *out_error != NULL) || vm->raised_value != previous_raised) {
            return (tinypy_hash_t)0;
        }
        tinypy_hash_t return_value_6 = __tinypy_internal_hash_fix((uint64_t)function_hash ^ (uint64_t)self_hash);
        return return_value_6;
    }
    case TINYPY_VALUE_LIST:
    case TINYPY_VALUE_DICT:
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_BYTEARRAY: {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("unhashable type: '"),
            TINYPY_MESSAGE_PART_TYPE_NAME(value),
            TINYPY_MESSAGE_PART_LITERAL("'"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
        return (tinypy_hash_t)0;
    }
    default: {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_hash_t hash;

        /* PyObject_Hash readies a type without tp_hash. */
        value->type->flags &= ~TINYPY_TYPE_FLAG_NEEDS_ATTRIBUTE_READY;
        int32_t special = __tinypy_internal_hash_special(value, TINYPY_FALSE, &hash, out_error);

        if (special > 0) {
            return hash;
        }
        if (special < 0) {
            return (tinypy_hash_t)0;
        }
        if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
            int32_t comparable = __tinypy_internal_hash_classic_comparable((tinypy_value_t *)value, out_error);

            if (comparable < 0) {
                return (tinypy_hash_t)0;
            }
            if (comparable > 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unhashable instance", out_error);
                return (tinypy_hash_t)0;
            }
        }
        function_result = __tinypy_internal_hash_fix(
                    (uint64_t)((uintptr_t)value >> 4U));
        return function_result;
    }
    }
}
//////////////////////////////////////////////////////////////////////////
static inline tinypy_bool_t __tinypy_internal_value_is_numeric(const tinypy_value_t *value) {
    tinypy_bool_t return_value_1 = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_INTEGER || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FLOAT || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_COMPLEX;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_integer_digits(const tinypy_value_t *value, int32_t *out_sign, uint16_t local_digits[5], const uint16_t **out_digits) {
    uint64_t magnitude;
    size_t count = 0U;
    int64_t integer;

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG) {
        *out_sign = TINYPY_LONG_SIGN(value);
        *out_digits = TINYPY_LONG_OBJECT(value)->digits;
        size_t return_value_1 = TINYPY_LONG_DIGIT_COUNT(value);
        return return_value_1;
    }

    integer = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BOOL
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
static tinypy_bool_t __tinypy_internal_integer_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    uint16_t left_local[5];
    uint16_t right_local[5];
    const uint16_t *left_digits;
    const uint16_t *right_digits;
    size_t left_count;
    size_t right_count;
    int32_t left_sign;
    int32_t right_sign;

    if (TINYPY_VALUE_KIND(left) != TINYPY_VALUE_LONG && TINYPY_VALUE_KIND(right) != TINYPY_VALUE_LONG) {
        tinypy_bool_t machine_equal = TINYPY_INTEGER_VALUE(left) == TINYPY_INTEGER_VALUE(right) ? TINYPY_TRUE : TINYPY_FALSE;

        return machine_equal;
    }
    left_count = __tinypy_internal_integer_digits(
        left, &left_sign, left_local, &left_digits);
    right_count = __tinypy_internal_integer_digits(
        right, &right_sign, right_local, &right_digits);
    tinypy_bool_t return_value_1 = left_sign == right_sign && left_count == right_count && (left_count == 0U || memcmp(
        left_digits,
        right_digits,
        left_count * sizeof(*left_digits)) == 0);
    return return_value_1;
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
    int32_t left_sign;
    int32_t right_sign;
    int32_t magnitude_order;

    if (TINYPY_VALUE_KIND(left) != TINYPY_VALUE_LONG && TINYPY_VALUE_KIND(right) != TINYPY_VALUE_LONG) {
        int64_t left_integer = TINYPY_INTEGER_VALUE(left);
        int64_t right_integer = TINYPY_INTEGER_VALUE(right);

        return left_integer < right_integer ? -1 : (left_integer > right_integer ? 1 : 0);
    }
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
    int32_t integer_sign;
    int32_t floating_sign;
    int32_t magnitude_order;

    if (isinf(floating)) {
        return floating < 0.0 ? 1 : -1;
    }
    if (TINYPY_VALUE_KIND(integer) != TINYPY_VALUE_LONG) {
        /* A machine integer compares against the floor of the float. */
        int64_t value = TINYPY_INTEGER_VALUE(integer);
        double floored;
        int64_t floored_value;

        if (floating >= 9223372036854775808.0) {
            return -1;
        }
        if (floating < -9223372036854775808.0) {
            return 1;
        }
        floored = floor(floating);
        floored_value = (int64_t)floored;
        if (value < floored_value) {
            return -1;
        }
        if (value > floored_value) {
            return 1;
        }
        return floating != floored ? -1 : 0;
    }
    integer_count = __tinypy_internal_integer_digits(integer, &integer_sign, integer_local, &integer_digits);
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
    magnitude_order = __tinypy_internal_compare_digit_magnitudes(integer_digits, integer_count, floating_digits, floating_count);
    if (magnitude_order == 0 && absolute != integral) {
        magnitude_order = -1;
    }
    return integer_sign < 0 ? -magnitude_order : magnitude_order;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_numeric_order(const tinypy_value_t *left, const tinypy_value_t *right, int32_t *out_order) {
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);
    int32_t left_integer = left_kind == TINYPY_VALUE_BOOL || left_kind == TINYPY_VALUE_INTEGER || left_kind == TINYPY_VALUE_LONG;
    int32_t right_integer = right_kind == TINYPY_VALUE_BOOL || right_kind == TINYPY_VALUE_INTEGER || right_kind == TINYPY_VALUE_LONG;

    if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
        return TINYPY_FALSE;
    }
    if (left_integer != 0 && right_integer != 0) {
        *out_order = __tinypy_internal_integer_order(left, right);
        return TINYPY_TRUE;
    }
    if (left_integer != 0) {
        double floating = TINYPY_FLOAT_OBJECT(right)->value;

        if (isnan(floating)) {
            return TINYPY_FALSE;
        }
        *out_order = __tinypy_internal_integer_double_order(left, floating);
        return TINYPY_TRUE;
    }
    if (right_integer != 0) {
        double floating = TINYPY_FLOAT_OBJECT(left)->value;

        if (isnan(floating)) {
            return TINYPY_FALSE;
        }
        *out_order = -__tinypy_internal_integer_double_order(right, floating);
        return TINYPY_TRUE;
    }
    if (isnan(TINYPY_FLOAT_OBJECT(left)->value) || isnan(TINYPY_FLOAT_OBJECT(right)->value)) {
        return TINYPY_FALSE;
    }
    *out_order = TINYPY_FLOAT_OBJECT(left)->value < TINYPY_FLOAT_OBJECT(right)->value ? -1 : (TINYPY_FLOAT_OBJECT(left)->value > TINYPY_FLOAT_OBJECT(right)->value ? 1 : 0);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_integer_equal_double(const tinypy_value_t *integer, double floating) {
    uint16_t local[5];
    uint16_t double_digits[TINYPY_DOUBLE_LONG_DIGITS];
    const uint16_t *integer_digits;
    size_t integer_count;
    size_t double_count;
    int32_t integer_sign;
    int32_t double_sign;

    double_count = __tinypy_internal_double_integer_digits(
        floating, &double_sign, double_digits);
    if (double_count == SIZE_MAX) {
        return TINYPY_FALSE;
    }
    integer_count = __tinypy_internal_integer_digits(
        integer, &integer_sign, local, &integer_digits);
    tinypy_bool_t return_value_1 = integer_sign == double_sign && integer_count == double_count && (integer_count == 0U || memcmp(
        integer_digits,
        double_digits,
        integer_count * sizeof(*integer_digits)) == 0);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_numeric_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);
    tinypy_bool_t left_integer = TINYPY_VALUE_KIND(left) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_INTEGER || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_LONG;
    tinypy_bool_t right_integer = TINYPY_VALUE_KIND(right) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_INTEGER || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_LONG;
    double left_real;
    double right_real;

    if (left_kind == TINYPY_VALUE_COMPLEX && right_kind == TINYPY_VALUE_COMPLEX) {
        tinypy_bool_t return_value_4 = TINYPY_COMPLEX_OBJECT(left)->real ==
                           TINYPY_COMPLEX_OBJECT(right)->real && TINYPY_COMPLEX_OBJECT(left)->imaginary ==
                           TINYPY_COMPLEX_OBJECT(right)->imaginary;
        return return_value_4;
    }
    if (left_kind == TINYPY_VALUE_COMPLEX) {
        if (TINYPY_COMPLEX_OBJECT(left)->imaginary != 0.0) {
            return TINYPY_FALSE;
        }
        if (right_integer) {
            tinypy_bool_t return_value_5 = __tinypy_internal_integer_equal_double(
                right, TINYPY_COMPLEX_OBJECT(left)->real);
            return return_value_5;
        }
        tinypy_bool_t return_value_6 = TINYPY_COMPLEX_OBJECT(left)->real ==
                       TINYPY_FLOAT_OBJECT(right)->value;
        return return_value_6;
    }
    if (right_kind == TINYPY_VALUE_COMPLEX) {
        if (TINYPY_COMPLEX_OBJECT(right)->imaginary != 0.0) {
            return TINYPY_FALSE;
        }
        if (left_integer) {
            tinypy_bool_t return_value_7 = __tinypy_internal_integer_equal_double(
                left, TINYPY_COMPLEX_OBJECT(right)->real);
            return return_value_7;
        }
        tinypy_bool_t return_value_8 = TINYPY_FLOAT_OBJECT(left)->value ==
                       TINYPY_COMPLEX_OBJECT(right)->real;
        return return_value_8;
    }

    if (left_integer && right_integer) {
        tinypy_bool_t return_value_1 = __tinypy_internal_integer_equal(left, right);
        return return_value_1;
    }
    if (left_integer && TINYPY_VALUE_KIND(right) == TINYPY_VALUE_FLOAT) {
        right_real = TINYPY_FLOAT_OBJECT(right)->value;
        tinypy_bool_t return_value_2 = __tinypy_internal_integer_equal_double(left, right_real);
        return return_value_2;
    }
    if (right_integer && TINYPY_VALUE_KIND(left) == TINYPY_VALUE_FLOAT) {
        left_real = TINYPY_FLOAT_OBJECT(left)->value;
        tinypy_bool_t return_value_3 = __tinypy_internal_integer_equal_double(right, left_real);
        return return_value_3;
    }

    left_real = TINYPY_FLOAT_OBJECT(left)->value;
    right_real = TINYPY_FLOAT_OBJECT(right)->value;
    return left_real == right_real;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_text_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    const uint8_t *left_bytes = TINYPY_TEXT_BYTES(left);
    const uint8_t *right_bytes = TINYPY_TEXT_BYTES(right);
    size_t left_size = TINYPY_TEXT_BYTE_SIZE(left);
    size_t right_size = TINYPY_TEXT_BYTE_SIZE(right);
    size_t index;

    if (left->type != right->type) {
        const tinypy_value_t *string = TINYPY_VALUE_KIND(left) == TINYPY_VALUE_STRING ? left : right;
        const uint8_t *string_bytes = TINYPY_TEXT_BYTES(string);

        for (index = 0U;
             index < TINYPY_TEXT_BYTE_SIZE(string);
             ++index) {
            if (string_bytes[index] >= 0x80U) {
                return TINYPY_FALSE;
            }
        }
    }
    tinypy_bool_t return_value_1 = left_size == right_size && (left_size == 0U || memcmp(left_bytes, right_bytes, left_size) == 0);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_bytes_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    const uint8_t *left_bytes;
    const uint8_t *right_bytes;
    size_t left_size;
    size_t right_size;

    if (tinypy_internal_bytes_view(left, &left_bytes, &left_size) == 0 || tinypy_internal_bytes_view(right, &right_bytes, &right_size) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = left_size == right_size && (left_size == 0U || memcmp(left_bytes, right_bytes, left_size) == 0) ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_text_order(const tinypy_value_t *left, const tinypy_value_t *right) {
    const uint8_t *left_bytes = TINYPY_TEXT_BYTES(left);
    const uint8_t *right_bytes = TINYPY_TEXT_BYTES(right);
    size_t left_size = TINYPY_TEXT_BYTE_SIZE(left);
    size_t right_size = TINYPY_TEXT_BYTE_SIZE(right);
    size_t common_size = left_size < right_size ? left_size : right_size;
    int32_t comparison = common_size != 0U ? memcmp(left_bytes, right_bytes, common_size) : 0;

    if (comparison != 0) {
        return comparison < 0 ? -1 : 1;
    }
    return left_size < right_size ? -1 : (left_size > right_size ? 1 : 0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_sequence_equal(const tinypy_value_t *left, const tinypy_value_t *right) {
    tinypy_value_t *const *left_items;
    tinypy_value_t *const *right_items;
    size_t left_size;
    size_t right_size;
    size_t index;

    if (TINYPY_VALUE_KIND(left) != TINYPY_VALUE_KIND(right)) {
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_TUPLE) {
        left_items = tinypy_internal_tuple_items(left);
        right_items = tinypy_internal_tuple_items(right);
        left_size = TINYPY_SIZED_SIZE(left);
        right_size = TINYPY_SIZED_SIZE(right);
    }
    else {
        left_items = TINYPY_LIST_OBJECT(left)->items;
        right_items = TINYPY_LIST_OBJECT(right)->items;
        left_size = TINYPY_SIZED_SIZE(left);
        right_size = TINYPY_SIZED_SIZE(right);
    }
    if (left_size != right_size) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index < left_size; ++index) {
        if (tinypy_internal_equal_value(
                left_items[index], right_items[index], 1) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_equal_value(const tinypy_value_t *left, const tinypy_value_t *right, tinypy_bool_t identity_implies_equal) {
    if (left == right && identity_implies_equal) {
        return TINYPY_TRUE;
    }
    if (__tinypy_internal_value_is_numeric(left) && __tinypy_internal_value_is_numeric(right)) {
        tinypy_bool_t return_value_1 = (int32_t)__tinypy_internal_numeric_equal(left, right);
        return return_value_1;
    }
    if ((TINYPY_VALUE_KIND(left) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_UNICODE) && (TINYPY_VALUE_KIND(right) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_UNICODE)) {
        tinypy_bool_t return_value_2 = (int32_t)__tinypy_internal_text_equal(left, right);
        return return_value_2;
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_BYTEARRAY || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_BYTEARRAY || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_BUFFER || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_BUFFER) {
        tinypy_bool_t return_value_3 = __tinypy_internal_bytes_equal(left, right);
        return return_value_3;
    }
    if ((TINYPY_VALUE_KIND(left) == TINYPY_VALUE_TUPLE || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_LIST) && (TINYPY_VALUE_KIND(right) == TINYPY_VALUE_TUPLE || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_LIST)) {
        tinypy_bool_t return_value_4 = __tinypy_internal_sequence_equal(left, right);
        return return_value_4;
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_DICT && TINYPY_VALUE_KIND(right) == TINYPY_VALUE_DICT) {
        tinypy_bool_t return_value_5 = tinypy_internal_dict_equal(left, right);
        return return_value_5;
    }
    if ((TINYPY_VALUE_KIND(left) == TINYPY_VALUE_SET || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_FROZENSET) && (TINYPY_VALUE_KIND(right) == TINYPY_VALUE_SET || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_FROZENSET)) {
        tinypy_bool_t return_value_6 = tinypy_internal_set_equal(left, right);
        return return_value_6;
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_WEAKREF && TINYPY_VALUE_KIND(right) == TINYPY_VALUE_WEAKREF) {
        tinypy_value_t *left_object = TINYPY_WEAKREF_OBJECT((tinypy_value_t *)left)->object;
        tinypy_value_t *right_object = TINYPY_WEAKREF_OBJECT((tinypy_value_t *)right)->object;

        if (left->type != right->type || left_object == NULL || right_object == NULL) {
            return left == right ? TINYPY_TRUE : TINYPY_FALSE;
        }
        tinypy_bool_t return_value_7 = tinypy_internal_equal_value(left_object, right_object, 1);
        return return_value_7;
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_METHOD && TINYPY_VALUE_KIND(right) == TINYPY_VALUE_METHOD) {
        tinypy_method_object_t *left_method = TINYPY_METHOD_OBJECT((tinypy_value_t *)left);
        tinypy_method_object_t *right_method = TINYPY_METHOD_OBJECT((tinypy_value_t *)right);

        return left_method->function == right_method->function && left_method->self == right_method->self ? TINYPY_TRUE : TINYPY_FALSE;
    }
    return left == right ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_hash_t tinypy_hash(const tinypy_value_t *value) {

    tinypy_hash_t return_value_1 = tinypy_internal_hash_value(value, NULL);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_equal(const tinypy_value_t *left, const tinypy_value_t *right) {

    tinypy_bool_t return_value_1 = tinypy_internal_equal_value(left, right, 0);
    return return_value_1;
}
