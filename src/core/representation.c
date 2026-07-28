#include "tinypy/representation.h"

#include "internal.h"

#include <math.h>
#include <string.h>

typedef struct tinypy_representation_builder_t {
    tinypy_vm_t *vm;
    uint8_t *bytes;
    size_t size;
    size_t capacity;
    tinypy_value_t **active;
    size_t active_count;
    size_t active_capacity;
} tinypy_representation_builder_t;

//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_reserve(tinypy_representation_builder_t *builder, size_t additional) {
    size_t required;
    size_t capacity;

    required = builder->size + additional;
    if (required <= builder->capacity) {
        return;
    }
    capacity = builder->capacity == 0U ? 64U : builder->capacity;
    while (capacity < required) {
        capacity *= 2U;
    }
    if (builder->bytes == NULL) {
        builder->bytes = (uint8_t *)tinypy_internal_vm_allocate(builder->vm, capacity);
    }
    else {
        builder->bytes = (uint8_t *)tinypy_internal_vm_reallocate(builder->vm, builder->bytes, builder->capacity, capacity);
    }
    builder->capacity = capacity;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_append(tinypy_representation_builder_t *builder, const void *bytes, size_t size) {
    if (size == 0U) {
        return;
    }
    __tinypy_representation_reserve(builder, size);
    (void)memcpy(builder->bytes + builder->size, bytes, size);
    builder->size += size;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_append_character(tinypy_representation_builder_t *builder, uint8_t character) {
    __tinypy_representation_append(builder, &character, 1U);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_representation_enter(tinypy_representation_builder_t *builder, tinypy_value_t *value) {
    size_t index;

    for (index = 0U; index < builder->active_count; ++index) {
        if (builder->active[index] == value) {
            return TINYPY_FALSE;
        }
    }
    if (builder->active_count == builder->active_capacity) {
        size_t capacity = builder->active_capacity == 0U ? 16U : builder->active_capacity * 2U;
        size_t old_size = builder->active_capacity * sizeof(*builder->active);
        size_t new_size = capacity * sizeof(*builder->active);

        if (builder->active == NULL) {
            builder->active = (tinypy_value_t **)tinypy_internal_vm_allocate(builder->vm, new_size);
        }
        else {
            builder->active = (tinypy_value_t **)tinypy_internal_vm_reallocate(builder->vm, builder->active, old_size, new_size);
        }
        builder->active_capacity = capacity;
    }
    builder->active[builder->active_count] = value;
    builder->active_count += 1U;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_leave(tinypy_representation_builder_t *builder, tinypy_value_t *value) {
    (void)value;
    builder->active_count -= 1U;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_unsigned_decimal(tinypy_representation_builder_t *builder, uint64_t value, size_t minimum_digits) {
    uint8_t reverse[32];
    size_t count = 0U;

    do {
        reverse[count] = (uint8_t)('0' + value % UINT64_C(10));
        count += 1U;
        value /= UINT64_C(10);
    } while (value != UINT64_C(0));
    while (count < minimum_digits) {
        reverse[count] = (uint8_t)'0';
        count += 1U;
    }
    while (count != 0U) {
        count -= 1U;
        __tinypy_representation_append_character(builder, reverse[count]);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_integer(tinypy_representation_builder_t *builder, int64_t value) {
    uint64_t magnitude;

    if (value < 0) {
        __tinypy_representation_append_character(builder, (uint8_t)'-');
        magnitude = (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1);
    }
    else {
        magnitude = (uint64_t)value;
    }
    __tinypy_representation_unsigned_decimal(builder, magnitude, 1U);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_long(tinypy_representation_builder_t *builder, const tinypy_value_t *value) {
    size_t digit_count = TINYPY_LONG_DIGIT_COUNT(value);
    uint16_t *work;
    uint32_t *chunks;
    size_t work_size;
    size_t chunks_size;
    size_t chunk_count = 0U;
    size_t active_digits;

    if (TINYPY_LONG_SIGN(value) < 0) {
        __tinypy_representation_append_character(builder, (uint8_t)'-');
    }
    if (digit_count == 0U) {
        __tinypy_representation_append(builder, "0L", 2U);
        return;
    }
    work_size = digit_count * sizeof(*work);
    chunks_size = digit_count * sizeof(*chunks);
    work = (uint16_t *)tinypy_internal_vm_allocate(builder->vm, work_size);
    chunks = (uint32_t *)tinypy_internal_vm_allocate(builder->vm, chunks_size);
    (void)memcpy(work, TINYPY_LONG_OBJECT(value)->digits, work_size);
    active_digits = digit_count;
    while (active_digits != 0U) {
        uint64_t remainder = UINT64_C(0);
        size_t index = active_digits;

        while (index != 0U) {
            uint64_t current;

            index -= 1U;
            current = (remainder << 15U) | (uint64_t)work[index];
            work[index] = (uint16_t)(current / UINT64_C(1000000000));
            remainder = current % UINT64_C(1000000000);
        }
        chunks[chunk_count] = (uint32_t)remainder;
        chunk_count += 1U;
        while (active_digits != 0U && work[active_digits - 1U] == 0U) {
            active_digits -= 1U;
        }
    }
    __tinypy_representation_unsigned_decimal(builder, chunks[chunk_count - 1U], 1U);
    while (chunk_count > 1U) {
        chunk_count -= 1U;
        __tinypy_representation_unsigned_decimal(builder, chunks[chunk_count - 1U], 9U);
    }
    __tinypy_representation_append_character(builder, (uint8_t)'L');
    tinypy_internal_vm_deallocate(builder->vm, chunks, chunks_size);
    tinypy_internal_vm_deallocate(builder->vm, work, work_size);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_double(tinypy_representation_builder_t *builder, double value) {
    uint32_t digits[18];
    size_t digit_count = 17U;
    int32_t exponent;
    double magnitude;
    double normalized;
    size_t index;
    tinypy_bool_t scientific;

    if (isnan(value)) {
        __tinypy_representation_append(builder, "nan", 3U);
        return;
    }
    if (isinf(value)) {
        __tinypy_representation_append(builder, value < 0.0 ? "-inf" : "inf", value < 0.0 ? 4U : 3U);
        return;
    }
    if (signbit(value) != 0) {
        __tinypy_representation_append_character(builder, (uint8_t)'-');
        magnitude = -value;
    }
    else {
        magnitude = value;
    }
    if (magnitude == 0.0) {
        __tinypy_representation_append(builder, "0.0", 3U);
        return;
    }
    double logarithm = log10(magnitude);
    exponent = (int32_t)floor(logarithm);
    normalized = magnitude / pow(10.0, (double)exponent);
    if (normalized >= 10.0) {
        normalized /= 10.0;
        exponent += 1;
    }
    else if (normalized < 1.0) {
        normalized *= 10.0;
        exponent -= 1;
    }
    for (index = 0U; index < 18U; ++index) {
        uint32_t digit = (uint32_t)floor(normalized);

        if (digit > 9U) {
            digit = 9U;
        }
        digits[index] = digit;
        normalized = (normalized - (double)digit) * 10.0;
    }
    if (digits[17] >= 5U) {
        index = 17U;
        while (index != 0U) {
            index -= 1U;
            digits[index] += 1U;
            if (digits[index] != 10U) {
                break;
            }
            digits[index] = 0U;
        }
        if (index == 0U && digits[0] == 0U) {
            digits[0] = 1U;
            exponent += 1;
        }
    }
    while (digit_count > 1U && digits[digit_count - 1U] == 0U) {
        digit_count -= 1U;
    }
    scientific = exponent < -4 || exponent >= 16;
    if (scientific != 0) {
        __tinypy_representation_append_character(builder, (uint8_t)('0' + digits[0]));
        if (digit_count > 1U) {
            __tinypy_representation_append_character(builder, (uint8_t)'.');
            for (index = 1U; index < digit_count; ++index) {
                __tinypy_representation_append_character(builder, (uint8_t)('0' + digits[index]));
            }
        }
        __tinypy_representation_append_character(builder, (uint8_t)'e');
        if (exponent < 0) {
            __tinypy_representation_append_character(builder, (uint8_t)'-');
            __tinypy_representation_unsigned_decimal(builder, (uint64_t)(-exponent), 2U);
        }
        else {
            __tinypy_representation_append_character(builder, (uint8_t)'+');
            __tinypy_representation_unsigned_decimal(builder, (uint64_t)exponent, 2U);
        }
        return;
    }
    if (exponent < 0) {
        __tinypy_representation_append(builder, "0.", 2U);
        for (index = 0U; index < (size_t)(-exponent - 1); ++index) {
            __tinypy_representation_append_character(builder, (uint8_t)'0');
        }
        for (index = 0U; index < digit_count; ++index) {
            __tinypy_representation_append_character(builder, (uint8_t)('0' + digits[index]));
        }
        return;
    }
    for (index = 0U; index <= (size_t)exponent; ++index) {
        uint32_t digit = index < digit_count ? digits[index] : 0U;

        __tinypy_representation_append_character(builder, (uint8_t)('0' + digit));
    }
    if ((size_t)exponent + 1U < digit_count) {
        __tinypy_representation_append_character(builder, (uint8_t)'.');
        for (index = (size_t)exponent + 1U; index < digit_count; ++index) {
            __tinypy_representation_append_character(builder, (uint8_t)('0' + digits[index]));
        }
    }
    else {
        __tinypy_representation_append(builder, ".0", 2U);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_quoted(tinypy_representation_builder_t *builder, const tinypy_value_t *value) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(value);
    size_t size = TINYPY_TEXT_BYTE_SIZE(value);
    size_t index;
    static const uint8_t hexadecimal[] = "0123456789abcdef";

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        __tinypy_representation_append_character(builder, (uint8_t)'u');
    }
    __tinypy_representation_append_character(builder, (uint8_t)'\'');
    for (index = 0U; index < size; ++index) {
        uint8_t byte = bytes[index];

        if (byte == '\\' || byte == '\'') {
            __tinypy_representation_append_character(builder, (uint8_t)'\\');
            __tinypy_representation_append_character(builder, byte);
        }
        else if (byte == '\n') {
            __tinypy_representation_append(builder, "\\n", 2U);
        }
        else if (byte == '\r') {
            __tinypy_representation_append(builder, "\\r", 2U);
        }
        else if (byte == '\t') {
            __tinypy_representation_append(builder, "\\t", 2U);
        }
        else if (byte >= 0x20U && byte < 0x7fU) {
            __tinypy_representation_append_character(builder, byte);
        }
        else {
            uint8_t escaped[4];

            escaped[0] = (uint8_t)'\\';
            escaped[1] = (uint8_t)'x';
            escaped[2] = hexadecimal[byte >> 4U];
            escaped[3] = hexadecimal[byte & 0x0fU];
            __tinypy_representation_append(builder, escaped, sizeof(escaped));
        }
    }
    __tinypy_representation_append_character(builder, (uint8_t)'\'');
}

static tinypy_bool_t __tinypy_representation_value(tinypy_representation_builder_t *builder, tinypy_value_t *value, tinypy_bool_t raw, tinypy_error_t **out_error);

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_representation_sequence(tinypy_representation_builder_t *builder, tinypy_value_t *value, uint8_t open, uint8_t close, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    size_t size = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(value) : TINYPY_LIST_SIZE(value);
    tinypy_value_t *const *iterator = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_ITERATOR_BEGIN(value) : TINYPY_LIST_ITERATOR_BEGIN(value);
    tinypy_value_t *const *iterator_begin = iterator;
    tinypy_value_t *const *iterator_end = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_ITERATOR_END(value) : TINYPY_LIST_ITERATOR_END(value);

    if (__tinypy_representation_enter(builder, value) == 0) {
        __tinypy_representation_append(builder, kind == TINYPY_VALUE_TUPLE ? "(...)" : "[...]", 5U);
        return TINYPY_TRUE;
    }
    __tinypy_representation_append_character(builder, open);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;

        if (iterator != iterator_begin) {
            __tinypy_representation_append(builder, ", ", 2U);
        }
        if (__tinypy_representation_value(builder, item, INT32_C(0), out_error) == 0) {
            __tinypy_representation_leave(builder, value);
            return TINYPY_FALSE;
        }
    }
    if (kind == TINYPY_VALUE_TUPLE && size == 1U) {
        __tinypy_representation_append_character(builder, (uint8_t)',');
    }
    __tinypy_representation_append_character(builder, close);
    __tinypy_representation_leave(builder, value);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_representation_dict(tinypy_representation_builder_t *builder, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(value);
    tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(value);
    size_t emitted = 0U;

    if (__tinypy_representation_enter(builder, value) == 0) {
        __tinypy_representation_append(builder, "{...}", 5U);
        return TINYPY_TRUE;
    }
    __tinypy_representation_append_character(builder, (uint8_t)'{');
    for (; iterator != iterator_end; ++iterator) {
        if (iterator->state != TINYPY_DICT_ENTRY_ACTIVE) {
            continue;
        }
        if (emitted != 0U) {
            __tinypy_representation_append(builder, ", ", 2U);
        }
        if (__tinypy_representation_value(builder, iterator->key, INT32_C(0), out_error) == 0) {
            __tinypy_representation_leave(builder, value);
            return TINYPY_FALSE;
        }
        __tinypy_representation_append(builder, ": ", 2U);
        if (__tinypy_representation_value(builder, iterator->value, INT32_C(0), out_error) == 0) {
            __tinypy_representation_leave(builder, value);
            return TINYPY_FALSE;
        }
        emitted += 1U;
    }
    __tinypy_representation_append_character(builder, (uint8_t)'}');
    __tinypy_representation_leave(builder, value);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_representation_set(tinypy_representation_builder_t *builder, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *dict = TINYPY_SET_OBJECT(value)->dict;
    tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(dict);
    tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(dict);
    tinypy_bool_t frozen = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FROZENSET ? TINYPY_TRUE : TINYPY_FALSE;
    size_t emitted = 0U;

    if (__tinypy_representation_enter(builder, value) == 0) {
        __tinypy_representation_append(builder, frozen != 0 ? "frozenset([...])" : "set([...])", frozen != 0 ? 16U : 10U);
        return TINYPY_TRUE;
    }
    __tinypy_representation_append(builder, frozen != 0 ? "frozenset([" : "set([", frozen != 0 ? 11U : 5U);
    for (; iterator != iterator_end; ++iterator) {
        if (iterator->state != TINYPY_DICT_ENTRY_ACTIVE) {
            continue;
        }
        if (emitted != 0U) {
            __tinypy_representation_append(builder, ", ", 2U);
        }
        if (__tinypy_representation_value(builder, iterator->key, INT32_C(0), out_error) == 0) {
            __tinypy_representation_leave(builder, value);
            return TINYPY_FALSE;
        }
        emitted += 1U;
    }
    __tinypy_representation_append(builder, "])", 2U);
    __tinypy_representation_leave(builder, value);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_pointer(tinypy_representation_builder_t *builder, const void *pointer) {
    uintptr_t value = (uintptr_t)pointer;
    uint8_t reverse[2U * sizeof(uintptr_t)];
    size_t count = 0U;
    static const uint8_t hexadecimal[] = "0123456789abcdef";

    __tinypy_representation_append(builder, "0x", 2U);
    do {
        reverse[count] = hexadecimal[value & (uintptr_t)0x0fU];
        count += 1U;
        value >>= 4U;
    } while (value != (uintptr_t)0U);
    while (count != 0U) {
        count -= 1U;
        __tinypy_representation_append_character(builder, reverse[count]);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_representation_custom(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_value_t *args;
    tinypy_value_t *result;

    tinypy_value_t *attribute = tinypy_internal_object_has_special(value, name, name_size) != 0 ? value : NULL;
    if (attribute == NULL) {
        return NULL;
    }
    tinypy_value_t *method = tinypy_object_get_attr(value, name, name_size, out_error);
    if (method == NULL) {
        return NULL;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    args = tinypy_tuple_from_items(vm, NULL, 0U);
    result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    if (result == NULL) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_UNICODE) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object representation method returned a non-string", out_error);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_representation_value(tinypy_representation_builder_t *builder, tinypy_value_t *value, tinypy_bool_t raw, tinypy_error_t **out_error) {
    tinypy_bool_t function_result;
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_unary_slot_t representation_slot = raw != 0 ? value->type->string : value->type->repr;

    if (representation_slot != NULL) {
        tinypy_value_t *representation = representation_slot(value, out_error);

        if (representation == NULL) {
            return TINYPY_FALSE;
        }
        if (TINYPY_VALUE_KIND(representation) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(representation) != TINYPY_VALUE_UNICODE) {
            TINYPY_DECREF(representation);
            tinypy_internal_make_vm_error(builder->vm, TINYPY_ERROR_TYPE, "representation slot returned a non-string", out_error);
            return TINYPY_FALSE;
        }
        const uint8_t *bytes = TINYPY_TEXT_BYTES(representation);
        size_t byte_size = TINYPY_TEXT_BYTE_SIZE(representation);
        __tinypy_representation_append(builder, bytes, byte_size);
        TINYPY_DECREF(representation);
        return TINYPY_TRUE;
    }

    if (raw != 0 && (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE)) {
        const uint8_t *bytes = TINYPY_TEXT_BYTES(value);
        size_t byte_size = TINYPY_TEXT_BYTE_SIZE(value);
        __tinypy_representation_append(builder, bytes, byte_size);
        return TINYPY_TRUE;
    }
    switch (kind) {
    case TINYPY_VALUE_NONE:
        __tinypy_representation_append(builder, "None", 4U);
        return TINYPY_TRUE;
    case TINYPY_VALUE_NOT_IMPLEMENTED:
        __tinypy_representation_append(builder, "NotImplemented", 14U);
        return TINYPY_TRUE;
    case TINYPY_VALUE_ELLIPSIS:
        __tinypy_representation_append(builder, "Ellipsis", 8U);
        return TINYPY_TRUE;
    case TINYPY_VALUE_BOOL:
        __tinypy_representation_append(builder, TINYPY_INTEGER_VALUE(value) != 0 ? "True" : "False", TINYPY_INTEGER_VALUE(value) != 0 ? 4U : 5U);
        return TINYPY_TRUE;
    case TINYPY_VALUE_INTEGER:
        __tinypy_representation_integer(builder, TINYPY_INTEGER_VALUE(value));
        return TINYPY_TRUE;
    case TINYPY_VALUE_LONG:
        __tinypy_representation_long(builder, value);
        return TINYPY_TRUE;
    case TINYPY_VALUE_FLOAT:
        __tinypy_representation_double(builder, TINYPY_FLOAT_OBJECT(value)->value);
        return TINYPY_TRUE;
    case TINYPY_VALUE_COMPLEX:
        __tinypy_representation_append_character(builder, (uint8_t)'(');
        __tinypy_representation_double(builder, TINYPY_COMPLEX_OBJECT(value)->real);
        if (TINYPY_COMPLEX_OBJECT(value)->imaginary >= 0.0) {
            __tinypy_representation_append_character(builder, (uint8_t)'+');
        }
        __tinypy_representation_double(builder, TINYPY_COMPLEX_OBJECT(value)->imaginary);
        __tinypy_representation_append(builder, "j)", 2U);
        return TINYPY_TRUE;
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_UNICODE:
        __tinypy_representation_quoted(builder, value);
        return TINYPY_TRUE;
    case TINYPY_VALUE_TUPLE:
        function_result = __tinypy_representation_sequence(builder, value, (uint8_t)'(', (uint8_t)')', out_error);
        return function_result;
    case TINYPY_VALUE_LIST:
        function_result = __tinypy_representation_sequence(builder, value, (uint8_t)'[', (uint8_t)']', out_error);
        return function_result;
    case TINYPY_VALUE_DICT:
        function_result = __tinypy_representation_dict(builder, value, out_error);
        return function_result;
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
        function_result = __tinypy_representation_set(builder, value, out_error);
        return function_result;
    case TINYPY_VALUE_TYPE:
        __tinypy_representation_append(builder, "<type '", 7U);
        __tinypy_representation_append(builder, ((tinypy_type_t *)value)->name, ((tinypy_type_t *)value)->name_size);
        __tinypy_representation_append(builder, "'>", 2U);
        return TINYPY_TRUE;
    case TINYPY_VALUE_FUNCTION:
        __tinypy_representation_append(builder, "<function ", 10U);
        tinypy_value_t *function_name = tinypy_function_name(value);
        const uint8_t *bytes_2 = TINYPY_TEXT_BYTES(function_name);
        size_t byte_size_2 = TINYPY_TEXT_BYTE_SIZE(function_name);
        __tinypy_representation_append(builder, bytes_2, byte_size_2);
        __tinypy_representation_append(builder, " at ", 4U);
        __tinypy_representation_pointer(builder, value);
        __tinypy_representation_append_character(builder, (uint8_t)'>');
        return TINYPY_TRUE;
    case TINYPY_VALUE_MODULE:
        __tinypy_representation_append(builder, "<module '", 9U);
        tinypy_value_t *module_name = tinypy_module_name(value);
        const uint8_t *bytes_3 = TINYPY_TEXT_BYTES(module_name);
        size_t byte_size_3 = TINYPY_TEXT_BYTE_SIZE(module_name);
        __tinypy_representation_append(builder, bytes_3, byte_size_3);
        __tinypy_representation_append(builder, "'>", 2U);
        return TINYPY_TRUE;
    case TINYPY_VALUE_SLICE:
        __tinypy_representation_append(builder, "slice(", 6U);
        tinypy_value_t *slice_start = tinypy_slice_start(value);
        if (__tinypy_representation_value(builder, slice_start, INT32_C(0), out_error) == 0) {
            return TINYPY_FALSE;
        }
        __tinypy_representation_append(builder, ", ", 2U);
        tinypy_value_t *slice_stop = tinypy_slice_stop(value);
        if (__tinypy_representation_value(builder, slice_stop, INT32_C(0), out_error) == 0) {
            return TINYPY_FALSE;
        }
        __tinypy_representation_append(builder, ", ", 2U);
        tinypy_value_t *slice_step = tinypy_slice_step(value);
        if (__tinypy_representation_value(builder, slice_step, INT32_C(0), out_error) == 0) {
            return TINYPY_FALSE;
        }
        __tinypy_representation_append_character(builder, (uint8_t)')');
        return TINYPY_TRUE;
    case TINYPY_VALUE_XRANGE: {
        tinypy_xrange_object_t *range = TINYPY_XRANGE_OBJECT(value);
        int64_t stop = range->start + (int64_t)range->length * range->step;

        __tinypy_representation_append(builder, "xrange(", 7U);
        if (range->start == 0 && range->step == 1) {
            __tinypy_representation_integer(builder, stop);
        }
        else {
            __tinypy_representation_integer(builder, range->start);
            __tinypy_representation_append(builder, ", ", 2U);
            __tinypy_representation_integer(builder, stop);
            if (range->step != 1) {
                __tinypy_representation_append(builder, ", ", 2U);
                __tinypy_representation_integer(builder, range->step);
            }
        }
        __tinypy_representation_append_character(builder, (uint8_t)')');
        return TINYPY_TRUE;
    }
    default: {
        tinypy_value_t *custom = __tinypy_representation_custom(value, raw != 0 ? "__str__" : "__repr__", raw != 0 ? 7U : 8U, out_error);

        if (custom != NULL) {
            const uint8_t *bytes = TINYPY_TEXT_BYTES(custom);
            size_t byte_size = TINYPY_TEXT_BYTE_SIZE(custom);
            __tinypy_representation_append(builder, bytes, byte_size);
            TINYPY_DECREF(custom);
            return TINYPY_TRUE;
        }
        if (out_error != NULL && *out_error != NULL) {
            return TINYPY_FALSE;
        }
        __tinypy_representation_append_character(builder, (uint8_t)'<');
        __tinypy_representation_append(builder, value->type->name, value->type->name_size);
        __tinypy_representation_append(builder, " object at ", 11U);
        __tinypy_representation_pointer(builder, value);
        __tinypy_representation_append_character(builder, (uint8_t)'>');
        return TINYPY_TRUE;
    }
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_representation_build(tinypy_value_t *value, tinypy_bool_t raw, tinypy_error_t **out_error) {
    tinypy_representation_builder_t builder;

    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = TINYPY_VALUE_VM(value);
    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_representation_value(&builder, value, raw, out_error) == 0) {
        if (builder.active != NULL) {
            tinypy_internal_vm_deallocate(builder.vm, builder.active, builder.active_capacity * sizeof(*builder.active));
        }
        if (builder.bytes != NULL) {
            tinypy_internal_vm_deallocate(builder.vm, builder.bytes, builder.capacity);
        }
        return NULL;
    }
    tinypy_value_t *result = tinypy_string_from_bytes(builder.vm, builder.bytes, builder.size);
    if (builder.active != NULL) {
        tinypy_internal_vm_deallocate(builder.vm, builder.active, builder.active_capacity * sizeof(*builder.active));
    }
    if (builder.bytes != NULL) {
        tinypy_internal_vm_deallocate(builder.vm, builder.bytes, builder.capacity);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_object_repr(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_representation_build(value, INT32_C(0), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_object_str(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_representation_build(value, INT32_C(1), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_representation_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    tinypy_bool_t condition = (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 1U;
    if (condition == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        condition = TINYPY_VALUE_KIND(item) != TINYPY_VALUE_FLOAT;
    }
    if (condition) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor requires a float object", out_error);
        return NULL;
    }
    tinypy_value_t *selected_value;
    if (user_data != NULL) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        selected_value = tinypy_object_str(item, out_error);
    }
    else {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        selected_value = tinypy_object_repr(item, out_error);
    }
    return selected_value;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_representation_types(tinypy_vm_t *vm) {
    tinypy_value_t *repr_function = tinypy_native_function_new(vm, "__repr__", 8U, __tinypy_representation_method, NULL, NULL);
    tinypy_value_t *str_function = tinypy_native_function_new(vm, "__str__", 7U, __tinypy_representation_method, (void *)(intptr_t)1, NULL);
    tinypy_value_t *repr_key = tinypy_string_from_bytes(vm, "__repr__", 8U);
    tinypy_value_t *str_key = tinypy_string_from_bytes(vm, "__str__", 7U);

    tinypy_dict_set(vm->types[TINYPY_VALUE_FLOAT].dict, repr_key, repr_function);
    tinypy_dict_set(vm->types[TINYPY_VALUE_FLOAT].dict, str_key, str_function);
    TINYPY_DECREF(str_key);
    TINYPY_DECREF(repr_key);
    TINYPY_DECREF(str_function);
    TINYPY_DECREF(repr_function);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) > 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "str constructor received invalid arguments", out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(item) == TINYPY_VALUE_BYTEARRAY) {
        tinypy_value_t *item_3 = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_t *return_value_2 = tinypy_internal_bytearray_string(item_3, out_error);
        return return_value_2;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_3 = tinypy_object_str(item_2, out_error);
    return return_value_3;
}
