#include "tinypy/representation.h"

#include "internal.h"

#include <assert.h>
#include <math.h>
#include <string.h>

typedef struct tinypy_representation_builder_t {
    tinypy_vm_t *vm;
    unsigned char *bytes;
    size_t size;
    size_t capacity;
    tinypy_value_t **active;
    size_t active_count;
    size_t active_capacity;
} tinypy_representation_builder_t;

static void __tinypy_representation_reserve(tinypy_representation_builder_t *builder, size_t additional)
{
    size_t required;
    size_t capacity;

    assert(additional <= SIZE_MAX - builder->size);
    required = builder->size + additional;
    if (required <= builder->capacity) return;
    capacity = builder->capacity == 0U ? 64U : builder->capacity;
    while (capacity < required) {
        assert(capacity <= SIZE_MAX / 2U);
        capacity *= 2U;
    }
    if (builder->bytes == NULL) builder->bytes = (unsigned char *)tinypy_internal_vm_allocate(builder->vm, capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    else builder->bytes = (unsigned char *)tinypy_internal_vm_reallocate(builder->vm, builder->bytes, builder->capacity, capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    builder->capacity = capacity;
}

static void __tinypy_representation_append(tinypy_representation_builder_t *builder, const void *bytes, size_t size)
{
    assert(bytes != NULL || size == 0U);
    if (size == 0U) return;
    __tinypy_representation_reserve(builder, size);
    (void)memcpy(builder->bytes + builder->size, bytes, size);
    builder->size += size;
}

static void __tinypy_representation_append_character(tinypy_representation_builder_t *builder, unsigned char character)
{
    __tinypy_representation_append(builder, &character, 1U);
}

static int32_t __tinypy_representation_enter(tinypy_representation_builder_t *builder, tinypy_value_t *value)
{
    size_t index;

    for (index = 0U; index < builder->active_count; index += 1U) {
        if (builder->active[index] == value) return INT32_C(0);
    }
    if (builder->active_count == builder->active_capacity) {
        size_t capacity = builder->active_capacity == 0U ? 16U : builder->active_capacity * 2U;
        size_t old_size = builder->active_capacity * sizeof(*builder->active);
        size_t new_size = capacity * sizeof(*builder->active);

        assert(capacity > builder->active_capacity);
        if (builder->active == NULL) builder->active = (tinypy_value_t **)tinypy_internal_vm_allocate(builder->vm, new_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        else builder->active = (tinypy_value_t **)tinypy_internal_vm_reallocate(builder->vm, builder->active, old_size, new_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        builder->active_capacity = capacity;
    }
    builder->active[builder->active_count] = value;
    builder->active_count += 1U;
    return INT32_C(1);
}

static void __tinypy_representation_leave(tinypy_representation_builder_t *builder, tinypy_value_t *value)
{
    assert(builder->active_count != 0U);
    assert(builder->active[builder->active_count - 1U] == value);
    (void)value;
    builder->active_count -= 1U;
}

static void __tinypy_representation_unsigned_decimal(tinypy_representation_builder_t *builder, uint64_t value, size_t minimum_digits)
{
    unsigned char reverse[32];
    size_t count = 0U;

    do {
        reverse[count] = (unsigned char)('0' + value % UINT64_C(10));
        count += 1U;
        value /= UINT64_C(10);
    } while (value != UINT64_C(0));
    while (count < minimum_digits) {
        reverse[count] = (unsigned char)'0';
        count += 1U;
    }
    while (count != 0U) {
        count -= 1U;
        __tinypy_representation_append_character(builder, reverse[count]);
    }
}

static void __tinypy_representation_integer(tinypy_representation_builder_t *builder, int64_t value)
{
    uint64_t magnitude;

    if (value < 0) {
        __tinypy_representation_append_character(builder, (unsigned char)'-');
        magnitude = (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1);
    } else {
        magnitude = (uint64_t)value;
    }
    __tinypy_representation_unsigned_decimal(builder, magnitude, 1U);
}

static void __tinypy_representation_long(tinypy_representation_builder_t *builder, const tinypy_value_t *value)
{
    size_t digit_count = TINYPY_LONG_DIGIT_COUNT(value);
    uint16_t *work;
    uint32_t *chunks;
    size_t work_size;
    size_t chunks_size;
    size_t chunk_count = 0U;
    size_t active_digits;

    if (TINYPY_LONG_SIGN(value) < 0) __tinypy_representation_append_character(builder, (unsigned char)'-');
    if (digit_count == 0U) {
        __tinypy_representation_append(builder, "0L", 2U);
        return;
    }
    work_size = digit_count * sizeof(*work);
    chunks_size = digit_count * sizeof(*chunks);
    work = (uint16_t *)tinypy_internal_vm_allocate(builder->vm, work_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    chunks = (uint32_t *)tinypy_internal_vm_allocate(builder->vm, chunks_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
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
        while (active_digits != 0U && work[active_digits - 1U] == 0U) active_digits -= 1U;
    }
    __tinypy_representation_unsigned_decimal(builder, chunks[chunk_count - 1U], 1U);
    while (chunk_count > 1U) {
        chunk_count -= 1U;
        __tinypy_representation_unsigned_decimal(builder, chunks[chunk_count - 1U], 9U);
    }
    __tinypy_representation_append_character(builder, (unsigned char)'L');
    tinypy_internal_vm_deallocate(builder->vm, chunks, chunks_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    tinypy_internal_vm_deallocate(builder->vm, work, work_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
}

static void __tinypy_representation_double(tinypy_representation_builder_t *builder, double value)
{
    unsigned int digits[18];
    size_t digit_count = 17U;
    int exponent;
    double magnitude;
    double normalized;
    size_t index;
    int scientific;

    if (isnan(value)) {
        __tinypy_representation_append(builder, "nan", 3U);
        return;
    }
    if (isinf(value)) {
        __tinypy_representation_append(builder, value < 0.0 ? "-inf" : "inf", value < 0.0 ? 4U : 3U);
        return;
    }
    if (signbit(value) != 0) {
        __tinypy_representation_append_character(builder, (unsigned char)'-');
        magnitude = -value;
    } else {
        magnitude = value;
    }
    if (magnitude == 0.0) {
        __tinypy_representation_append(builder, "0.0", 3U);
        return;
    }
    exponent = (int)floor(log10(magnitude));
    normalized = magnitude / pow(10.0, (double)exponent);
    if (normalized >= 10.0) {
        normalized /= 10.0;
        exponent += 1;
    } else if (normalized < 1.0) {
        normalized *= 10.0;
        exponent -= 1;
    }
    for (index = 0U; index < 18U; index += 1U) {
        unsigned int digit = (unsigned int)floor(normalized);

        if (digit > 9U) digit = 9U;
        digits[index] = digit;
        normalized = (normalized - (double)digit) * 10.0;
    }
    if (digits[17] >= 5U) {
        index = 17U;
        while (index != 0U) {
            index -= 1U;
            digits[index] += 1U;
            if (digits[index] != 10U) break;
            digits[index] = 0U;
        }
        if (index == 0U && digits[0] == 0U) {
            digits[0] = 1U;
            exponent += 1;
        }
    }
    while (digit_count > 1U && digits[digit_count - 1U] == 0U) digit_count -= 1U;
    scientific = exponent < -4 || exponent >= 16;
    if (scientific != 0) {
        __tinypy_representation_append_character(builder, (unsigned char)('0' + digits[0]));
        if (digit_count > 1U) {
            __tinypy_representation_append_character(builder, (unsigned char)'.');
            for (index = 1U; index < digit_count; index += 1U) __tinypy_representation_append_character(builder, (unsigned char)('0' + digits[index]));
        }
        __tinypy_representation_append_character(builder, (unsigned char)'e');
        if (exponent < 0) {
            __tinypy_representation_append_character(builder, (unsigned char)'-');
            __tinypy_representation_unsigned_decimal(builder, (uint64_t)(-exponent), 2U);
        } else {
            __tinypy_representation_append_character(builder, (unsigned char)'+');
            __tinypy_representation_unsigned_decimal(builder, (uint64_t)exponent, 2U);
        }
        return;
    }
    if (exponent < 0) {
        __tinypy_representation_append(builder, "0.", 2U);
        for (index = 0U; index < (size_t)(-exponent - 1); index += 1U) __tinypy_representation_append_character(builder, (unsigned char)'0');
        for (index = 0U; index < digit_count; index += 1U) __tinypy_representation_append_character(builder, (unsigned char)('0' + digits[index]));
        return;
    }
    for (index = 0U; index <= (size_t)exponent; index += 1U) {
        unsigned int digit = index < digit_count ? digits[index] : 0U;

        __tinypy_representation_append_character(builder, (unsigned char)('0' + digit));
    }
    if ((size_t)exponent + 1U < digit_count) {
        __tinypy_representation_append_character(builder, (unsigned char)'.');
        for (index = (size_t)exponent + 1U; index < digit_count; index += 1U) __tinypy_representation_append_character(builder, (unsigned char)('0' + digits[index]));
    } else {
        __tinypy_representation_append(builder, ".0", 2U);
    }
}

static void __tinypy_representation_quoted(tinypy_representation_builder_t *builder, const tinypy_value_t *value)
{
    const unsigned char *bytes = tinypy_internal_text_bytes(value);
    size_t size = tinypy_internal_text_byte_size(value);
    size_t index;
    static const unsigned char hexadecimal[] = "0123456789abcdef";

    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_UNICODE) __tinypy_representation_append_character(builder, (unsigned char)'u');
    __tinypy_representation_append_character(builder, (unsigned char)'\'');
    for (index = 0U; index < size; index += 1U) {
        unsigned char byte = bytes[index];

        if (byte == '\\' || byte == '\'') {
            __tinypy_representation_append_character(builder, (unsigned char)'\\');
            __tinypy_representation_append_character(builder, byte);
        } else if (byte == '\n') __tinypy_representation_append(builder, "\\n", 2U);
        else if (byte == '\r') __tinypy_representation_append(builder, "\\r", 2U);
        else if (byte == '\t') __tinypy_representation_append(builder, "\\t", 2U);
        else if (byte >= 0x20U && byte < 0x7fU) __tinypy_representation_append_character(builder, byte);
        else {
            unsigned char escaped[4];

            escaped[0] = (unsigned char)'\\';
            escaped[1] = (unsigned char)'x';
            escaped[2] = hexadecimal[byte >> 4U];
            escaped[3] = hexadecimal[byte & 0x0fU];
            __tinypy_representation_append(builder, escaped, sizeof(escaped));
        }
    }
    __tinypy_representation_append_character(builder, (unsigned char)'\'');
}

static int32_t __tinypy_representation_value(tinypy_representation_builder_t *builder, tinypy_value_t *value, int32_t raw, tinypy_error_t **out_error);

static int32_t __tinypy_representation_sequence(tinypy_representation_builder_t *builder, tinypy_value_t *value, unsigned char open, unsigned char close, tinypy_error_t **out_error)
{
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);
    size_t size = kind == TINYPY_VALUE_TUPLE ? tinypy_tuple_size(value) : tinypy_list_size(value);
    size_t index;

    if (__tinypy_representation_enter(builder, value) == 0) {
        __tinypy_representation_append(builder, kind == TINYPY_VALUE_TUPLE ? "(...)" : "[...]", 5U);
        return INT32_C(1);
    }
    __tinypy_representation_append_character(builder, open);
    for (index = 0U; index < size; index += 1U) {
        tinypy_value_t *item = kind == TINYPY_VALUE_TUPLE ? tinypy_tuple_get(value, index) : tinypy_list_get(value, index);

        if (index != 0U) __tinypy_representation_append(builder, ", ", 2U);
        if (__tinypy_representation_value(builder, item, INT32_C(0), out_error) == 0) {
            __tinypy_representation_leave(builder, value);
            return INT32_C(0);
        }
    }
    if (kind == TINYPY_VALUE_TUPLE && size == 1U) __tinypy_representation_append_character(builder, (unsigned char)',');
    __tinypy_representation_append_character(builder, close);
    __tinypy_representation_leave(builder, value);
    return INT32_C(1);
}

static int32_t __tinypy_representation_dict(tinypy_representation_builder_t *builder, tinypy_value_t *value, tinypy_error_t **out_error)
{
    tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(value);
    size_t index;
    size_t emitted = 0U;

    if (__tinypy_representation_enter(builder, value) == 0) {
        __tinypy_representation_append(builder, "{...}", 5U);
        return INT32_C(1);
    }
    __tinypy_representation_append_character(builder, (unsigned char)'{');
    for (index = 0U; index <= dict->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &dict->table[index];

        if (entry->state != TINYPY_DICT_ENTRY_ACTIVE) continue;
        if (emitted != 0U) __tinypy_representation_append(builder, ", ", 2U);
        if (__tinypy_representation_value(builder, entry->key, INT32_C(0), out_error) == 0) {
            __tinypy_representation_leave(builder, value);
            return INT32_C(0);
        }
        __tinypy_representation_append(builder, ": ", 2U);
        if (__tinypy_representation_value(builder, entry->value, INT32_C(0), out_error) == 0) {
            __tinypy_representation_leave(builder, value);
            return INT32_C(0);
        }
        emitted += 1U;
    }
    __tinypy_representation_append_character(builder, (unsigned char)'}');
    __tinypy_representation_leave(builder, value);
    return INT32_C(1);
}

static int32_t __tinypy_representation_set(tinypy_representation_builder_t *builder, tinypy_value_t *value, tinypy_error_t **out_error)
{
    tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(TINYPY_SET_OBJECT(value)->dict);
    int32_t frozen = tinypy_internal_value_kind(value) == TINYPY_VALUE_FROZENSET;
    size_t index;
    size_t emitted = 0U;

    if (__tinypy_representation_enter(builder, value) == 0) {
        __tinypy_representation_append(builder, frozen != 0 ? "frozenset([...])" : "set([...])", frozen != 0 ? 16U : 10U);
        return INT32_C(1);
    }
    __tinypy_representation_append(builder, frozen != 0 ? "frozenset([" : "set([", frozen != 0 ? 11U : 5U);
    for (index = 0U; index <= dict->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &dict->table[index];

        if (entry->state != TINYPY_DICT_ENTRY_ACTIVE) continue;
        if (emitted != 0U) __tinypy_representation_append(builder, ", ", 2U);
        if (__tinypy_representation_value(builder, entry->key, INT32_C(0), out_error) == 0) {
            __tinypy_representation_leave(builder, value);
            return INT32_C(0);
        }
        emitted += 1U;
    }
    __tinypy_representation_append(builder, "])", 2U);
    __tinypy_representation_leave(builder, value);
    return INT32_C(1);
}

static void __tinypy_representation_pointer(tinypy_representation_builder_t *builder, const void *pointer)
{
    uintptr_t value = (uintptr_t)pointer;
    unsigned char reverse[2U * sizeof(uintptr_t)];
    size_t count = 0U;
    static const unsigned char hexadecimal[] = "0123456789abcdef";

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

static tinypy_value_t *__tinypy_representation_custom(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error)
{
    tinypy_value_t *attribute;
    tinypy_value_t *method;
    tinypy_value_t *args;
    tinypy_value_t *result;

    attribute = tinypy_internal_object_has_special(value, name, name_size) != 0 ? value : NULL;
    if (attribute == NULL) return NULL;
    method = tinypy_object_get_attr(value, name, name_size, out_error);
    if (method == NULL) return NULL;
    args = tinypy_tuple_from_items(tinypy_internal_value_vm(value), NULL, 0U);
    result = tinypy_call(method, args, NULL, out_error);
    tinypy_release(args);
    tinypy_release(method);
    if (result == NULL) return NULL;
    if (tinypy_internal_value_kind(result) != TINYPY_VALUE_STRING && tinypy_internal_value_kind(result) != TINYPY_VALUE_UNICODE) {
        tinypy_release(result);
        tinypy_internal_make_vm_error(tinypy_internal_value_vm(value), TINYPY_ERROR_TYPE, "object representation method returned a non-string", out_error);
        return NULL;
    }
    return result;
}

static int32_t __tinypy_representation_value(tinypy_representation_builder_t *builder, tinypy_value_t *value, int32_t raw, tinypy_error_t **out_error)
{
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);
    tinypy_unary_slot_t representation_slot = raw != 0 ? value->type->string : value->type->repr;

    if (representation_slot != NULL) {
        tinypy_value_t *representation = representation_slot(value, out_error);

        if (representation == NULL) return INT32_C(0);
        if (tinypy_internal_value_kind(representation) != TINYPY_VALUE_STRING && tinypy_internal_value_kind(representation) != TINYPY_VALUE_UNICODE) {
            tinypy_release(representation);
            tinypy_internal_make_vm_error(builder->vm, TINYPY_ERROR_TYPE, "representation slot returned a non-string", out_error);
            return INT32_C(0);
        }
        __tinypy_representation_append(builder, tinypy_internal_text_bytes(representation), tinypy_internal_text_byte_size(representation));
        tinypy_release(representation);
        return INT32_C(1);
    }

    if (raw != 0 && (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE)) {
        __tinypy_representation_append(builder, tinypy_internal_text_bytes(value), tinypy_internal_text_byte_size(value));
        return INT32_C(1);
    }
    switch (kind) {
    case TINYPY_VALUE_NONE: __tinypy_representation_append(builder, "None", 4U); return INT32_C(1);
    case TINYPY_VALUE_NOT_IMPLEMENTED: __tinypy_representation_append(builder, "NotImplemented", 14U); return INT32_C(1);
    case TINYPY_VALUE_ELLIPSIS: __tinypy_representation_append(builder, "Ellipsis", 8U); return INT32_C(1);
    case TINYPY_VALUE_BOOL: __tinypy_representation_append(builder, TINYPY_INTEGER_VALUE(value) != 0 ? "True" : "False", TINYPY_INTEGER_VALUE(value) != 0 ? 4U : 5U); return INT32_C(1);
    case TINYPY_VALUE_INTEGER: __tinypy_representation_integer(builder, TINYPY_INTEGER_VALUE(value)); return INT32_C(1);
    case TINYPY_VALUE_LONG: __tinypy_representation_long(builder, value); return INT32_C(1);
    case TINYPY_VALUE_FLOAT: __tinypy_representation_double(builder, TINYPY_FLOAT_OBJECT(value)->value); return INT32_C(1);
    case TINYPY_VALUE_COMPLEX:
        __tinypy_representation_append_character(builder, (unsigned char)'(');
        __tinypy_representation_double(builder, TINYPY_COMPLEX_OBJECT(value)->real);
        if (TINYPY_COMPLEX_OBJECT(value)->imaginary >= 0.0) __tinypy_representation_append_character(builder, (unsigned char)'+');
        __tinypy_representation_double(builder, TINYPY_COMPLEX_OBJECT(value)->imaginary);
        __tinypy_representation_append(builder, "j)", 2U);
        return INT32_C(1);
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_UNICODE: __tinypy_representation_quoted(builder, value); return INT32_C(1);
    case TINYPY_VALUE_TUPLE: return __tinypy_representation_sequence(builder, value, (unsigned char)'(', (unsigned char)')', out_error);
    case TINYPY_VALUE_LIST: return __tinypy_representation_sequence(builder, value, (unsigned char)'[', (unsigned char)']', out_error);
    case TINYPY_VALUE_DICT: return __tinypy_representation_dict(builder, value, out_error);
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET: return __tinypy_representation_set(builder, value, out_error);
    case TINYPY_VALUE_TYPE:
        __tinypy_representation_append(builder, "<type '", 7U);
        __tinypy_representation_append(builder, ((tinypy_type_t *)value)->name, ((tinypy_type_t *)value)->name_size);
        __tinypy_representation_append(builder, "'>", 2U);
        return INT32_C(1);
    case TINYPY_VALUE_FUNCTION:
        __tinypy_representation_append(builder, "<function ", 10U);
        __tinypy_representation_append(builder, tinypy_internal_text_bytes(tinypy_function_name(value)), tinypy_internal_text_byte_size(tinypy_function_name(value)));
        __tinypy_representation_append(builder, " at ", 4U);
        __tinypy_representation_pointer(builder, value);
        __tinypy_representation_append_character(builder, (unsigned char)'>');
        return INT32_C(1);
    case TINYPY_VALUE_MODULE:
        __tinypy_representation_append(builder, "<module '", 9U);
        __tinypy_representation_append(builder, tinypy_internal_text_bytes(tinypy_module_name(value)), tinypy_internal_text_byte_size(tinypy_module_name(value)));
        __tinypy_representation_append(builder, "'>", 2U);
        return INT32_C(1);
    case TINYPY_VALUE_SLICE:
        __tinypy_representation_append(builder, "slice(", 6U);
        if (__tinypy_representation_value(builder, tinypy_slice_start(value), INT32_C(0), out_error) == 0) return INT32_C(0);
        __tinypy_representation_append(builder, ", ", 2U);
        if (__tinypy_representation_value(builder, tinypy_slice_stop(value), INT32_C(0), out_error) == 0) return INT32_C(0);
        __tinypy_representation_append(builder, ", ", 2U);
        if (__tinypy_representation_value(builder, tinypy_slice_step(value), INT32_C(0), out_error) == 0) return INT32_C(0);
        __tinypy_representation_append_character(builder, (unsigned char)')');
        return INT32_C(1);
    case TINYPY_VALUE_XRANGE: {
        tinypy_xrange_object_t *range = TINYPY_XRANGE_OBJECT(value);
        int64_t stop = range->start + (int64_t)range->length * range->step;

        __tinypy_representation_append(builder, "xrange(", 7U);
        if (range->start == 0 && range->step == 1) {
            __tinypy_representation_integer(builder, stop);
        } else {
            __tinypy_representation_integer(builder, range->start);
            __tinypy_representation_append(builder, ", ", 2U);
            __tinypy_representation_integer(builder, stop);
            if (range->step != 1) {
                __tinypy_representation_append(builder, ", ", 2U);
                __tinypy_representation_integer(builder, range->step);
            }
        }
        __tinypy_representation_append_character(builder, (unsigned char)')');
        return INT32_C(1);
    }
    default: {
        tinypy_value_t *custom = __tinypy_representation_custom(value, raw != 0 ? "__str__" : "__repr__", raw != 0 ? 7U : 8U, out_error);

        if (custom != NULL) {
            __tinypy_representation_append(builder, tinypy_internal_text_bytes(custom), tinypy_internal_text_byte_size(custom));
            tinypy_release(custom);
            return INT32_C(1);
        }
        if (out_error != NULL && *out_error != NULL) return INT32_C(0);
        __tinypy_representation_append_character(builder, (unsigned char)'<');
        __tinypy_representation_append(builder, value->type->name, value->type->name_size);
        __tinypy_representation_append(builder, " object at ", 11U);
        __tinypy_representation_pointer(builder, value);
        __tinypy_representation_append_character(builder, (unsigned char)'>');
        return INT32_C(1);
    }
    }
}

static tinypy_value_t *__tinypy_representation_build(tinypy_value_t *value, int32_t raw, tinypy_error_t **out_error)
{
    tinypy_representation_builder_t builder;
    tinypy_value_t *result;

    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = tinypy_internal_value_vm(value);
    tinypy_internal_clear_error(out_error);
    if (__tinypy_representation_value(&builder, value, raw, out_error) == 0) {
        if (builder.active != NULL) tinypy_internal_vm_deallocate(builder.vm, builder.active, builder.active_capacity * sizeof(*builder.active), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        if (builder.bytes != NULL) tinypy_internal_vm_deallocate(builder.vm, builder.bytes, builder.capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        return NULL;
    }
    assert(builder.active_count == 0U);
    result = tinypy_string_from_bytes(builder.vm, builder.bytes, builder.size);
    if (builder.active != NULL) tinypy_internal_vm_deallocate(builder.vm, builder.active, builder.active_capacity * sizeof(*builder.active), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    if (builder.bytes != NULL) tinypy_internal_vm_deallocate(builder.vm, builder.bytes, builder.capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}

tinypy_value_t *tinypy_object_repr(tinypy_value_t *value, tinypy_error_t **out_error)
{
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    return __tinypy_representation_build(value, INT32_C(0), out_error);
}

tinypy_value_t *tinypy_object_str(tinypy_value_t *value, tinypy_error_t **out_error)
{
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    return __tinypy_representation_build(value, INT32_C(1), out_error);
}

static tinypy_value_t *__tinypy_representation_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    if ((kwargs != NULL && tinypy_dict_size(kwargs) != 0U) || tinypy_tuple_size(args) != 1U || tinypy_internal_value_kind(tinypy_tuple_get(args, 0U)) != TINYPY_VALUE_FLOAT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor requires a float object", out_error);
        return NULL;
    }
    return user_data != NULL ? tinypy_object_str(tinypy_tuple_get(args, 0U), out_error) : tinypy_object_repr(tinypy_tuple_get(args, 0U), out_error);
}

void tinypy_internal_initialize_representation_types(tinypy_vm_t *vm)
{
    tinypy_value_t *repr_function = tinypy_native_function_new(vm, "__repr__", 8U, __tinypy_representation_method, NULL, NULL);
    tinypy_value_t *str_function = tinypy_native_function_new(vm, "__str__", 7U, __tinypy_representation_method, (void *)(intptr_t)1, NULL);
    tinypy_value_t *repr_key = tinypy_string_from_bytes(vm, "__repr__", 8U);
    tinypy_value_t *str_key = tinypy_string_from_bytes(vm, "__str__", 7U);

    tinypy_dict_set(vm->float_type.dict, repr_key, repr_function);
    tinypy_dict_set(vm->float_type.dict, str_key, str_function);
    tinypy_release(str_key);
    tinypy_release(repr_key);
    tinypy_release(str_function);
    tinypy_release(repr_function);
}

tinypy_value_t *tinypy_internal_string_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;

    if ((kwargs != NULL && tinypy_dict_size(kwargs) != 0U) || tinypy_tuple_size(args) > 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "str constructor received invalid arguments", out_error);
        return NULL;
    }
    if (tinypy_tuple_size(args) == 0U) return tinypy_string_from_bytes(vm, NULL, 0U);
    if (tinypy_internal_value_kind(tinypy_tuple_get(args, 0U)) == TINYPY_VALUE_BYTEARRAY) return tinypy_internal_bytearray_string(tinypy_tuple_get(args, 0U), out_error);
    return tinypy_object_str(tinypy_tuple_get(args, 0U), out_error);
}
