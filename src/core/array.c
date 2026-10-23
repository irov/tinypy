#include "internal.h"

#include <stdio.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
#define TINYPY_ARRAY_ITEM_SIZE_MAX 8U
#define TINYPY_ARRAY_UNICODE_MAX UINT32_C(0x10ffff)
//////////////////////////////////////////////////////////////////////////
/* The items are stored in host byte order and read and written through
   memcpy, so the storage needs no alignment. A conversion writes the item
   aside before the caller stores it, as the callbacks it runs may resize the
   array. */
typedef tinypy_value_t *(*tinypy_array_item_get_t)(tinypy_vm_t *vm, const uint8_t *item, tinypy_error_t **out_error);
typedef tinypy_bool_t (*tinypy_array_item_convert_t)(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_item, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_array_descriptor_t {
    uint8_t typecode;
    size_t item_size;
    tinypy_array_item_get_t get;
    tinypy_array_item_convert_t convert;
} tinypy_array_descriptor_t;
//////////////////////////////////////////////////////////////////////////
/* exports counts the C callers that hold the items across Python callbacks;
   the array refuses to resize meanwhile, so the items stay in place. */
typedef struct tinypy_array_payload_t {
    const tinypy_array_descriptor_t *descriptor;
    uint8_t *items;
    size_t size;
    size_t capacity;
    size_t exports;
} tinypy_array_payload_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_array_payload_t *__tinypy_array_payload(tinypy_value_t *value) {
    tinypy_array_payload_t *payload = (tinypy_array_payload_t *)tinypy_native_instance_payload(value);

    return payload;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_array_payload_t *__tinypy_array_const_payload(const tinypy_value_t *value) {
    const tinypy_array_payload_t *payload = (const tinypy_array_payload_t *)tinypy_native_instance_const_payload(value);

    return payload;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_array_bounded(tinypy_vm_t *vm, int64_t value, int64_t minimum, int64_t maximum, const char *below, const char *above, tinypy_error_t **out_error) {
    if (value < minimum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, below, out_error);
        return TINYPY_FALSE;
    }
    if (value > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, above, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* The 'h' and 'i' formats of PyArg_Parse, which the item conversions of the
   integer typecodes start with: a float is refused before PyInt_AsLong reads
   the value. */
static tinypy_bool_t __tinypy_array_parsed_integer(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_bool_t short_format, int64_t *out_value, tinypy_error_t **out_error) {
    if (tinypy_internal_integer_as_ssize(value, out_value, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    if (short_format != TINYPY_FALSE) {
        tinypy_bool_t bounded = __tinypy_array_bounded(vm, *out_value, INT16_MIN, INT16_MAX, "signed short integer is less than minimum", "signed short integer is greater than maximum", out_error);
        return bounded;
    }
    tinypy_bool_t bounded = __tinypy_array_bounded(vm, *out_value, INT32_MIN, INT32_MAX, "signed integer is less than minimum", "signed integer is greater than maximum", out_error);
    return bounded;
}
//////////////////////////////////////////////////////////////////////////
/* II_setitem and LL_setitem: a long converts as PyLong_AsUnsignedLong does,
   any other number as a C long that must not be negative. */
static tinypy_bool_t __tinypy_array_unsigned(tinypy_vm_t *vm, tinypy_value_t *value, uint64_t maximum, const char *below, const char *above, uint64_t *out_value, tinypy_error_t **out_error) {
    uint64_t magnitude = UINT64_C(0);

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG) {
        int32_t sign;
        size_t digit_count;
        const uint16_t *digits = tinypy_long_base15_view(value, &sign, &digit_count);

        if (sign < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "can't convert negative value to unsigned long", out_error);
            return TINYPY_FALSE;
        }
        while (digit_count != 0U) {
            if (magnitude > UINT64_MAX >> 15U) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "long int too large to convert", out_error);
                return TINYPY_FALSE;
            }
            digit_count -= 1U;
            magnitude = (magnitude << 15U) | (uint64_t)digits[digit_count];
        }
    }
    else {
        int64_t integer;

        if (tinypy_internal_integer_as_ssize(value, &integer, out_error) == TINYPY_FALSE) {
            return TINYPY_FALSE;
        }
        if (integer < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, below, out_error);
            return TINYPY_FALSE;
        }
        magnitude = (uint64_t)integer;
    }
    if (magnitude > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, above, out_error);
        return TINYPY_FALSE;
    }
    *out_value = magnitude;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_long_from_u64(tinypy_vm_t *vm, uint64_t value) {
    uint16_t digits[5];
    size_t digit_count = 0U;

    if (value <= (uint64_t)INT64_MAX) {
        tinypy_value_t *result = tinypy_long_from_i64(vm, (int64_t)value);
        return result;
    }
    while (value != UINT64_C(0)) {
        digits[digit_count] = (uint16_t)(value & UINT64_C(0x7fff));
        digit_count += 1U;
        value >>= 15U;
    }
    tinypy_value_t *result = tinypy_long_from_base15_digits(vm, INT32_C(1), digits, digit_count);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The unicode of count UCS-4 items. A UCS-4 build of CPython 2.7 keeps any
   32-bit item as a character; the UTF-8 of a tinypy unicode holds code
   points up to U+10FFFF, so a larger item, which byteswap or fromstring can
   store, raises ValueError instead. */
static tinypy_value_t *__tinypy_array_unicode(tinypy_vm_t *vm, const uint8_t *items, size_t count, tinypy_error_t **out_error) {
    size_t byte_size = 0U;
    size_t index;

    for (index = 0U; index < count; ++index) {
        uint32_t code_point;

        (void)memcpy(&code_point, items + index * sizeof(code_point), sizeof(code_point));
        if (code_point > TINYPY_ARRAY_UNICODE_MAX) {
            char message[64];

            (void)snprintf(message, sizeof(message), "character U+%x is not in range [U+0000; U+10ffff]", (unsigned int)code_point);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, message, out_error);
            return NULL;
        }
        byte_size += code_point < UINT32_C(0x80) ? 1U : (code_point < UINT32_C(0x800) ? 2U : (code_point < UINT32_C(0x10000) ? 3U : 4U));
    }
    if (count == 1U) {
        uint8_t bytes[4];
        uint32_t code_point;

        (void)memcpy(&code_point, items, sizeof(code_point));
        size_t size = tinypy_internal_utf8_encode(code_point, bytes);
        tinypy_value_t *result = tinypy_internal_unicode_from_utf8_checked(vm, (const char *)bytes, size, out_error);
        return result;
    }
    if (count == 0U) {
        tinypy_value_t *result = TINYPY_RET_EMPTY_UNICODE(vm);
        return result;
    }
    uint8_t *bytes;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_UNICODE, byte_size, count, &bytes, out_error);
    if (result == NULL) {
        return NULL;
    }
    size_t offset = 0U;
    for (index = 0U; index < count; ++index) {
        uint32_t code_point;

        (void)memcpy(&code_point, items + index * sizeof(code_point), sizeof(code_point));
        offset += tinypy_internal_utf8_encode(code_point, bytes + offset);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_get_char(tinypy_vm_t *vm, const uint8_t *item, tinypy_error_t **out_error) {
    tinypy_value_t *result = tinypy_internal_string_from_bytes_checked(vm, item, 1U, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_get_signed_char(tinypy_vm_t *vm, const uint8_t *item, tinypy_error_t **out_error) {
    int8_t value;

    (void)out_error;
    (void)memcpy(&value, item, sizeof(value));
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_get_unsigned_char(tinypy_vm_t *vm, const uint8_t *item, tinypy_error_t **out_error) {
    (void)out_error;
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)item[0]);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_get_unicode(tinypy_vm_t *vm, const uint8_t *item, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_array_unicode(vm, item, 1U, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_get_short(tinypy_vm_t *vm, const uint8_t *item, tinypy_error_t **out_error) {
    int16_t value;

    (void)out_error;
    (void)memcpy(&value, item, sizeof(value));
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_get_unsigned_short(tinypy_vm_t *vm, const uint8_t *item, tinypy_error_t **out_error) {
    uint16_t value;

    (void)out_error;
    (void)memcpy(&value, item, sizeof(value));
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_get_int(tinypy_vm_t *vm, const uint8_t *item, tinypy_error_t **out_error) {
    int32_t value;

    (void)out_error;
    (void)memcpy(&value, item, sizeof(value));
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* II_getitem and LL_getitem return PyLong_FromUnsignedLong. */
static tinypy_value_t *__tinypy_array_get_unsigned_int(tinypy_vm_t *vm, const uint8_t *item, tinypy_error_t **out_error) {
    uint32_t value;

    (void)out_error;
    (void)memcpy(&value, item, sizeof(value));
    tinypy_value_t *result = tinypy_long_from_i64(vm, (int64_t)value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_get_long(tinypy_vm_t *vm, const uint8_t *item, tinypy_error_t **out_error) {
    int64_t value;

    (void)out_error;
    (void)memcpy(&value, item, sizeof(value));
    tinypy_value_t *result = tinypy_integer_from_i64(vm, value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_get_unsigned_long(tinypy_vm_t *vm, const uint8_t *item, tinypy_error_t **out_error) {
    uint64_t value;

    (void)out_error;
    (void)memcpy(&value, item, sizeof(value));
    tinypy_value_t *result = __tinypy_array_long_from_u64(vm, value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_get_float(tinypy_vm_t *vm, const uint8_t *item, tinypy_error_t **out_error) {
    float value;

    (void)out_error;
    (void)memcpy(&value, item, sizeof(value));
    tinypy_value_t *result = tinypy_float_from_double(vm, (double)value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_get_double(tinypy_vm_t *vm, const uint8_t *item, tinypy_error_t **out_error) {
    double value;

    (void)out_error;
    (void)memcpy(&value, item, sizeof(value));
    tinypy_value_t *result = tinypy_float_from_double(vm, value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The 'c' format of PyArg_Parse takes a str of length one. */
static tinypy_bool_t __tinypy_array_convert_char(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_item, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_STRING || TINYPY_SIZED_SIZE(value) != 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "array item must be char", out_error);
        return TINYPY_FALSE;
    }
    out_item[0] = TINYPY_STRING_OBJECT(value)->bytes[0];
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* b_setitem parses a short, as the 'b' format of PyArg_Parse is unsigned,
   and checks the range of a signed char itself. */
static tinypy_bool_t __tinypy_array_convert_signed_char(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_item, tinypy_error_t **out_error) {
    int64_t integer;

    if (__tinypy_array_parsed_integer(vm, value, TINYPY_TRUE, &integer, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    if (__tinypy_array_bounded(vm, integer, INT8_MIN, INT8_MAX, "signed char is less than minimum", "signed char is greater than maximum", out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    int8_t item = (int8_t)integer;
    (void)memcpy(out_item, &item, sizeof(item));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_array_convert_unsigned_char(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_item, tinypy_error_t **out_error) {
    int64_t integer;

    if (tinypy_internal_integer_as_ssize(value, &integer, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    if (__tinypy_array_bounded(vm, integer, 0, UINT8_MAX, "unsigned byte integer is less than minimum", "unsigned byte integer is greater than maximum", out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    out_item[0] = (uint8_t)integer;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* The 'u#' format of PyArg_Parse takes a unicode, whose length must be one. */
static tinypy_bool_t __tinypy_array_convert_unicode(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_item, tinypy_error_t **out_error) {
    uint32_t code_point;

    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_UNICODE || TINYPY_SIZED_SIZE(value) != 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "array item must be unicode character", out_error);
        return TINYPY_FALSE;
    }
    (void)tinypy_internal_utf8_decode(TINYPY_TEXT_BYTES(value), TINYPY_TEXT_BYTE_SIZE(value), &code_point);
    (void)memcpy(out_item, &code_point, sizeof(code_point));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_array_convert_short(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_item, tinypy_error_t **out_error) {
    int64_t integer;

    if (__tinypy_array_parsed_integer(vm, value, TINYPY_TRUE, &integer, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    int16_t item = (int16_t)integer;
    (void)memcpy(out_item, &item, sizeof(item));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* HH_setitem parses an int and checks the range of an unsigned short. */
static tinypy_bool_t __tinypy_array_convert_unsigned_short(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_item, tinypy_error_t **out_error) {
    int64_t integer;

    if (__tinypy_array_parsed_integer(vm, value, TINYPY_FALSE, &integer, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    if (__tinypy_array_bounded(vm, integer, 0, UINT16_MAX, "unsigned short is less than minimum", "unsigned short is greater than maximum", out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    uint16_t item = (uint16_t)integer;
    (void)memcpy(out_item, &item, sizeof(item));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_array_convert_int(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_item, tinypy_error_t **out_error) {
    int64_t integer;

    if (__tinypy_array_parsed_integer(vm, value, TINYPY_FALSE, &integer, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    int32_t item = (int32_t)integer;
    (void)memcpy(out_item, &item, sizeof(item));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_array_convert_unsigned_int(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_item, tinypy_error_t **out_error) {
    uint64_t integer;

    if (__tinypy_array_unsigned(vm, value, UINT32_MAX, "unsigned int is less than minimum", "unsigned int is greater than maximum", &integer, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    uint32_t item = (uint32_t)integer;
    (void)memcpy(out_item, &item, sizeof(item));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_array_convert_long(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_item, tinypy_error_t **out_error) {
    int64_t integer;

    (void)vm;
    if (tinypy_internal_integer_as_ssize(value, &integer, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    (void)memcpy(out_item, &integer, sizeof(integer));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_array_convert_unsigned_long(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_item, tinypy_error_t **out_error) {
    uint64_t integer;

    if (__tinypy_array_unsigned(vm, value, UINT64_MAX, "unsigned long is less than minimum", "unsigned long is greater than maximum", &integer, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    (void)memcpy(out_item, &integer, sizeof(integer));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_array_convert_float(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_item, tinypy_error_t **out_error) {
    double number;

    (void)vm;
    if (tinypy_internal_number_as_double(value, &number, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    float item = (float)number;
    (void)memcpy(out_item, &item, sizeof(item));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_array_convert_double(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_item, tinypy_error_t **out_error) {
    double number;

    (void)vm;
    if (tinypy_internal_number_as_double(value, &number, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    (void)memcpy(out_item, &number, sizeof(number));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* The item sizes of the LP64 build of CPython 2.7 with UCS-4 unicode, on
   every target: 'l' and 'L' hold 64 bits as tinypy's int does. */
static const tinypy_array_descriptor_t __tinypy_array_descriptors[] = {
    {(uint8_t)'c', 1U, __tinypy_array_get_char, __tinypy_array_convert_char},
    {(uint8_t)'b', 1U, __tinypy_array_get_signed_char, __tinypy_array_convert_signed_char},
    {(uint8_t)'B', 1U, __tinypy_array_get_unsigned_char, __tinypy_array_convert_unsigned_char},
    {(uint8_t)'u', 4U, __tinypy_array_get_unicode, __tinypy_array_convert_unicode},
    {(uint8_t)'h', 2U, __tinypy_array_get_short, __tinypy_array_convert_short},
    {(uint8_t)'H', 2U, __tinypy_array_get_unsigned_short, __tinypy_array_convert_unsigned_short},
    {(uint8_t)'i', 4U, __tinypy_array_get_int, __tinypy_array_convert_int},
    {(uint8_t)'I', 4U, __tinypy_array_get_unsigned_int, __tinypy_array_convert_unsigned_int},
    {(uint8_t)'l', 8U, __tinypy_array_get_long, __tinypy_array_convert_long},
    {(uint8_t)'L', 8U, __tinypy_array_get_unsigned_long, __tinypy_array_convert_unsigned_long},
    {(uint8_t)'f', 4U, __tinypy_array_get_float, __tinypy_array_convert_float},
    {(uint8_t)'d', 8U, __tinypy_array_get_double, __tinypy_array_convert_double}
};
//////////////////////////////////////////////////////////////////////////
static const tinypy_array_descriptor_t *__tinypy_array_descriptor(uint32_t typecode) {
    for (size_t index = 0U; index < sizeof(__tinypy_array_descriptors) / sizeof(__tinypy_array_descriptors[0]); ++index) {
        if ((uint32_t)__tinypy_array_descriptors[index].typecode == typecode) {
            return &__tinypy_array_descriptors[index];
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_array_check(const tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_bool_t result = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NATIVE_INSTANCE && vm->array_type != NULL && tinypy_type_is_subtype(value->type, vm->array_type) != 0 ? TINYPY_TRUE : TINYPY_FALSE;

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The bytes the old buffer protocol of an array exposes, which its next
   resize may move: an empty array without storage exposes an empty one, as
   the emptybuf of CPython. */
const uint8_t *tinypy_internal_array_view(const tinypy_value_t *value, size_t *out_size) {
    const tinypy_array_payload_t *payload = __tinypy_array_const_payload(value);

    *out_size = payload->size * payload->descriptor->item_size;
    const uint8_t *result = payload->items != NULL ? payload->items : (const uint8_t *)"";
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_array_export(tinypy_value_t *value, int32_t change) {
    tinypy_array_payload_t *payload = __tinypy_array_payload(value);

    if (change > 0) {
        payload->exports += 1U;
    }
    else {
        payload->exports -= 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_array_resize_allowed(tinypy_value_t *array, size_t size, tinypy_error_t **out_error) {
    const tinypy_array_payload_t *payload = __tinypy_array_payload(array);

    if (size != payload->size && payload->exports != 0U) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(array), TINYPY_ERROR_BUFFER, "cannot resize an array that is exporting buffers", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Gives the storage of the items back, which leaves the array empty. */
static void __tinypy_array_release_items(tinypy_value_t *array) {
    tinypy_array_payload_t *payload = __tinypy_array_payload(array);

    if (payload->items != NULL) {
        tinypy_internal_vm_deallocate(TINYPY_VALUE_VM(array), payload->items, payload->capacity * payload->descriptor->item_size);
        payload->items = NULL;
    }
    payload->size = 0U;
    payload->capacity = 0U;
}
//////////////////////////////////////////////////////////////////////////
/* array_resize: the storage stays until the size falls 16 items below the
   size it had, and a new storage adds a sixteenth and a few items, which
   keeps a series of appends linear. A shrink never fails once the exports
   allow it: a smaller storage that the allocator refuses leaves the larger
   one in place. */
static tinypy_bool_t __tinypy_array_resize(tinypy_value_t *array, size_t size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(array);
    tinypy_array_payload_t *payload = __tinypy_array_payload(array);
    size_t item_size = payload->descriptor->item_size;

    if (size == payload->size) {
        return TINYPY_TRUE;
    }
    if (__tinypy_array_resize_allowed(array, size, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    if (payload->capacity >= size && payload->size < size + 16U && payload->items != NULL) {
        payload->size = size;
        return TINYPY_TRUE;
    }
    size_t slack = (size >> 4U) + (payload->size < 8U ? 3U : 7U);
    if (payload->items != NULL && size < payload->size) {
        if (size + slack < payload->capacity) {
            uint8_t *shrunk = (uint8_t *)tinypy_internal_vm_reallocate(vm, payload->items, payload->capacity * item_size, (size + slack) * item_size);

            if (shrunk != NULL) {
                payload->items = shrunk;
                payload->capacity = size + slack;
            }
        }
        payload->size = size;
        return TINYPY_TRUE;
    }
    size_t limit = (size_t)PTRDIFF_MAX / item_size;
    if (size > limit - limit / 16U - 8U) {
        tinypy_internal_vm_allocation_error(vm, out_error);
        return TINYPY_FALSE;
    }
    size_t capacity = size + slack;
    uint8_t *items = payload->items == NULL
                         ? (uint8_t *)__tinypy_internal_vm_allocate_checked(vm, capacity * item_size, out_error)
                         : (uint8_t *)__tinypy_internal_vm_reallocate_checked(vm, payload->items, payload->capacity * item_size, capacity * item_size, out_error);
    if (items == NULL) {
        return TINYPY_FALSE;
    }
    payload->items = items;
    payload->capacity = capacity;
    payload->size = size;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* newarrayobject: storage for exactly size items. */
static tinypy_value_t *__tinypy_array_new(tinypy_type_t *type, const tinypy_array_descriptor_t *descriptor, size_t size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if (size > (size_t)PTRDIFF_MAX / descriptor->item_size) {
        tinypy_internal_vm_allocation_error(vm, out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_object_allocate_checked(vm, type, type->basic_size, out_error);
    if (result == NULL) {
        return NULL;
    }
    tinypy_array_payload_t *payload = __tinypy_array_payload(result);
    payload->descriptor = descriptor;
    if (size != 0U) {
        payload->items = (uint8_t *)__tinypy_internal_vm_allocate_checked(vm, size * descriptor->item_size, out_error);
        if (payload->items == NULL) {
            TINYPY_DECREF(result);
            return NULL;
        }
        payload->capacity = size;
        payload->size = size;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* array_slice and the step loop of array_subscr: always an array.array. The
   position of each item is reached from start, as a step past the last item
   may leave the range of int64_t. */
static tinypy_value_t *__tinypy_array_slice(tinypy_value_t *array, size_t start, int64_t step, size_t length, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(array);
    const tinypy_array_payload_t *payload = __tinypy_array_payload(array);
    size_t item_size = payload->descriptor->item_size;
    tinypy_value_t *result = __tinypy_array_new(vm->array_type, payload->descriptor, length, out_error);

    if (result == NULL || length == 0U) {
        return result;
    }
    uint8_t *items = __tinypy_array_payload(result)->items;
    if (step == 1) {
        (void)memcpy(items, payload->items + start * item_size, length * item_size);
        return result;
    }
    for (size_t index = 0U; index < length; ++index) {
        size_t position = (size_t)((int64_t)start + (int64_t)index * step);

        (void)memcpy(items + index * item_size, payload->items + position * item_size, item_size);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_copy(tinypy_value_t *array, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_array_slice(array, 0U, 1, __tinypy_array_payload(array)->size, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_item(tinypy_value_t *array, size_t index, tinypy_error_t **out_error) {
    const tinypy_array_payload_t *payload = __tinypy_array_payload(array);
    const tinypy_array_descriptor_t *descriptor = payload->descriptor;
    tinypy_value_t *result = descriptor->get(TINYPY_VALUE_VM(array), payload->items + index * descriptor->item_size, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The item at index after the conversion of value, whose callbacks may have
   resized the array: an index the array no longer has is out of range, where
   CPython 2.7 writes past the items. */
static tinypy_bool_t __tinypy_array_store(tinypy_value_t *array, size_t index, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_array_payload_t *payload = __tinypy_array_payload(array);
    const tinypy_array_descriptor_t *descriptor = payload->descriptor;
    uint8_t item[TINYPY_ARRAY_ITEM_SIZE_MAX];

    if (descriptor->convert(TINYPY_VALUE_VM(array), value, item, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    if (index >= payload->size) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(array), TINYPY_ERROR_INDEX, "array assignment index out of range", out_error);
        return TINYPY_FALSE;
    }
    (void)memcpy(payload->items + index * descriptor->item_size, item, descriptor->item_size);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* ins1 converts the item to check it before the array grows and again to
   store it, so a conversion callback runs twice. The position is read after
   the first conversion; an item the second one fails to convert keeps the
   value of the first one, where CPython 2.7 leaves the slot unset. */
static tinypy_bool_t __tinypy_array_insert(tinypy_value_t *array, int64_t where, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(array);
    tinypy_array_payload_t *payload = __tinypy_array_payload(array);
    const tinypy_array_descriptor_t *descriptor = payload->descriptor;
    size_t item_size = descriptor->item_size;
    uint8_t item[TINYPY_ARRAY_ITEM_SIZE_MAX];

    if (descriptor->convert(vm, value, item, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    size_t size = payload->size;
    if (where < 0) {
        where += (int64_t)size;
        if (where < 0) {
            where = 0;
        }
    }
    size_t position = (uint64_t)where > (uint64_t)size ? size : (size_t)where;
    if (__tinypy_array_resize(array, size + 1U, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    (void)memmove(payload->items + (position + 1U) * item_size, payload->items + position * item_size, (size - position) * item_size);
    (void)memcpy(payload->items + position * item_size, item, item_size);
    if (descriptor->convert(vm, value, item, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    if (position < payload->size) {
        (void)memcpy(payload->items + position * item_size, item, item_size);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Appends count items that do not alias the storage of the array. */
static tinypy_bool_t __tinypy_array_append_items(tinypy_value_t *array, const uint8_t *items, size_t count, tinypy_error_t **out_error) {
    tinypy_array_payload_t *payload = __tinypy_array_payload(array);
    size_t size = payload->size;
    size_t item_size = payload->descriptor->item_size;

    if (count == 0U) {
        return TINYPY_TRUE;
    }
    if (count > (size_t)PTRDIFF_MAX / item_size - size) {
        tinypy_internal_vm_allocation_error(TINYPY_VALUE_VM(array), out_error);
        return TINYPY_FALSE;
    }
    if (__tinypy_array_resize(array, size + count, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    (void)memcpy(payload->items + size * item_size, items, count * item_size);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* array_iter_extend: every item of the iterable is appended as ins1 does. */
static tinypy_bool_t __tinypy_array_extend_iterator(tinypy_value_t *array, tinypy_value_t *iterator, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(array);
    tinypy_error_t *iteration_error = NULL;

    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        tinypy_bool_t inserted = __tinypy_array_insert(array, (int64_t)__tinypy_array_payload(array)->size, item, out_error);
        TINYPY_DECREF(item);
        if (inserted == TINYPY_FALSE) {
            return TINYPY_FALSE;
        }
    }
    if (tinypy_internal_iteration_failed(vm, iteration_error, out_error) != 0) {
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* array_do_extend: an array of the same typecode, itself included, or the
   items of an iterable. */
static tinypy_bool_t __tinypy_array_extend(tinypy_value_t *array, tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(array);
    tinypy_array_payload_t *payload = __tinypy_array_payload(array);

    if (tinypy_internal_array_check(source) == TINYPY_FALSE) {
        tinypy_value_t *iterator = tinypy_iter(source, out_error);
        if (iterator == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t extended = __tinypy_array_extend_iterator(array, iterator, out_error);
        TINYPY_DECREF(iterator);
        return extended;
    }
    tinypy_array_payload_t *source_payload = __tinypy_array_payload(source);
    if (source_payload->descriptor != payload->descriptor) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "can only extend with array of same kind", out_error);
        return TINYPY_FALSE;
    }
    size_t size = payload->size;
    size_t count = source_payload->size;
    size_t item_size = payload->descriptor->item_size;
    if (count == 0U) {
        return TINYPY_TRUE;
    }
    if (count > (size_t)PTRDIFF_MAX / item_size - size) {
        tinypy_internal_vm_allocation_error(vm, out_error);
        return TINYPY_FALSE;
    }
    if (__tinypy_array_resize(array, size + count, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    /* The items of the source are read after the resize, which moves them
       when the source is the array itself. */
    (void)memcpy(payload->items + size * item_size, source_payload->items, count * item_size);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* array_ass_slice with step 1: the items from start to stop are replaced by
   the count items of replacement, which never aliases the storage. */
static tinypy_bool_t __tinypy_array_replace(tinypy_value_t *array, size_t start, size_t stop, const uint8_t *replacement, size_t count, tinypy_error_t **out_error) {
    tinypy_array_payload_t *payload = __tinypy_array_payload(array);
    size_t item_size = payload->descriptor->item_size;
    size_t size = payload->size;
    size_t length = stop - start;

    if (length > count) {
        if (__tinypy_array_resize_allowed(array, size - (length - count), out_error) == TINYPY_FALSE) {
            return TINYPY_FALSE;
        }
        (void)memmove(payload->items + (start + count) * item_size, payload->items + stop * item_size, (size - stop) * item_size);
        (void)__tinypy_array_resize(array, size - (length - count), NULL);
    }
    else if (length < count) {
        if (count - length > (size_t)PTRDIFF_MAX / item_size - size) {
            tinypy_internal_vm_allocation_error(TINYPY_VALUE_VM(array), out_error);
            return TINYPY_FALSE;
        }
        if (__tinypy_array_resize(array, size + (count - length), out_error) == TINYPY_FALSE) {
            return TINYPY_FALSE;
        }
        (void)memmove(payload->items + (start + count) * item_size, payload->items + stop * item_size, (size - stop) * item_size);
    }
    if (count != 0U) {
        (void)memcpy(payload->items + start * item_size, replacement, count * item_size);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Deletes length items from start on, each step after the previous one,
   with step positive. */
static tinypy_bool_t __tinypy_array_delete_extended(tinypy_value_t *array, size_t start, size_t step, size_t length, tinypy_error_t **out_error) {
    tinypy_array_payload_t *payload = __tinypy_array_payload(array);
    size_t item_size = payload->descriptor->item_size;
    size_t size = payload->size;

    if (length == 0U) {
        return TINYPY_TRUE;
    }
    if (__tinypy_array_resize_allowed(array, size - length, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    size_t target = start;
    for (size_t index = 0U; index < length; ++index) {
        size_t current = start + index * step;
        size_t next = index + 1U < length ? current + step : size;

        (void)memmove(payload->items + target * item_size, payload->items + (current + 1U) * item_size, (next - current - 1U) * item_size);
        target += next - current - 1U;
    }
    (void)__tinypy_array_resize(array, size - length, NULL);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_array_index_error(tinypy_vm_t *vm, const char *message, tinypy_error_t **out_error) {
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, message, out_error);
}
//////////////////////////////////////////////////////////////////////////
/* array_subscr and array_ass_subscr convert an index with
   PyNumber_AsSsize_t, IndexError for an overflow, and count a negative one
   from the end of the array as long as the conversion left it. An int is
   its own index. */
static tinypy_bool_t __tinypy_array_index(tinypy_value_t *array, tinypy_value_t *key, const char *range_message, size_t *out_index, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(key);
    int64_t index;

    if (kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_BOOL) {
        index = TINYPY_INTEGER_VALUE(key);
    }
    else if (tinypy_internal_number_as_index(key, TINYPY_ERROR_INDEX, &index, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    size_t size = __tinypy_array_payload(array)->size;
    if (index < 0) {
        index += (int64_t)size;
    }
    if (index < 0 || (uint64_t)index >= (uint64_t)size) {
        __tinypy_array_index_error(TINYPY_VALUE_VM(array), range_message, out_error);
        return TINYPY_FALSE;
    }
    *out_index = (size_t)index;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_subscript(tinypy_value_t *instance, void *payload, tinypy_value_t *key, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);

    (void)payload;
    (void)user_data;
    if (tinypy_internal_index_check(key) != TINYPY_FALSE) {
        size_t index;

        if (__tinypy_array_index(instance, key, "array index out of range", &index, out_error) == TINYPY_FALSE) {
            return NULL;
        }
        tinypy_value_t *result = __tinypy_array_item(instance, index, out_error);
        return result;
    }
    if (TINYPY_VALUE_KIND(key) != TINYPY_VALUE_SLICE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "array indices must be integers", out_error);
        return NULL;
    }
    tinypy_internal_slice_indices_t slice;
    if (tinypy_internal_slice_unpack(key, &slice, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (tinypy_internal_slice_adjust_indices(vm, __tinypy_array_payload(instance)->size, &slice, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_array_slice(instance, slice.length != 0U ? (size_t)slice.start : 0U, slice.step, slice.length, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The value that replaces a slice of another array: an array of the same
   typecode, or NULL for a deletion. */
static TINYPY_ALWAYS_INLINE tinypy_bool_t __tinypy_array_replacement_check(tinypy_value_t *array, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(array);

    if (value == NULL) {
        return TINYPY_TRUE;
    }
    if (tinypy_internal_array_check(value) == TINYPY_FALSE) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("can only assign array (not \""),
            TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(value, 200U),
            TINYPY_MESSAGE_PART_LITERAL("\") to array slice")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    if (__tinypy_array_payload(value)->descriptor != __tinypy_array_payload(array)->descriptor) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bad argument type for built-in operation", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* array_slice and array_ass_slice bound both offsets by the size, the
   second one by the first one too. */
static void __tinypy_array_bound_offsets(tinypy_value_t *array, int64_t low, int64_t high, size_t *out_start, size_t *out_stop) {
    size_t size = __tinypy_array_payload(array)->size;
    size_t start = low < 0 ? 0U : ((uint64_t)low > (uint64_t)size ? size : (size_t)low);
    size_t stop = high < 0 ? 0U : ((uint64_t)high > (uint64_t)size ? size : (size_t)high);

    *out_start = start;
    *out_stop = stop < start ? start : stop;
}
//////////////////////////////////////////////////////////////////////////
/* array_slice, the sq_slice of an array, which apply_slice calls with the
   offsets of a[i:j]. */
tinypy_value_t *tinypy_internal_array_slice(tinypy_value_t *array, int64_t low, int64_t high, tinypy_error_t **out_error) {
    size_t start;
    size_t stop;

    TINYPY_CLEAR_ERROR(out_error);
    __tinypy_array_bound_offsets(array, low, high, &start, &stop);
    tinypy_value_t *result = __tinypy_array_slice(array, start, 1, stop - start, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* array_ass_slice, the sq_ass_slice of an array, which assign_slice calls
   with the offsets of a[i:j]: value NULL deletes, and an array assigned to
   itself is copied first. */
tinypy_bool_t tinypy_internal_array_assign_slice(tinypy_value_t *array, int64_t low, int64_t high, tinypy_value_t *value, tinypy_error_t **out_error) {
    size_t start;
    size_t stop;

    TINYPY_CLEAR_ERROR(out_error);
    if (value == array) {
        tinypy_value_t *copy = __tinypy_array_copy(array, out_error);
        if (copy == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t assigned = tinypy_internal_array_assign_slice(array, low, high, copy, out_error);
        TINYPY_DECREF(copy);
        return assigned;
    }
    if (__tinypy_array_replacement_check(array, value, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    __tinypy_array_bound_offsets(array, low, high, &start, &stop);
    const tinypy_array_payload_t *source = value != NULL ? __tinypy_array_payload(value) : NULL;
    tinypy_bool_t replaced = __tinypy_array_replace(array, start, stop, source != NULL ? source->items : NULL, source != NULL ? source->size : 0U, out_error);
    return replaced;
}
//////////////////////////////////////////////////////////////////////////
/* array_ass_subscr for a slice whose indices the caller adjusted. */
static tinypy_bool_t __tinypy_array_assign_slice(tinypy_value_t *array, tinypy_internal_slice_indices_t *slice, tinypy_value_t *replacement, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(array);
    const tinypy_array_payload_t *source = replacement != NULL ? __tinypy_array_payload(replacement) : NULL;
    size_t needed = source != NULL ? source->size : 0U;
    const uint8_t *items = source != NULL ? source->items : NULL;

    /* For a[2:1] = ... the insertion point is start, not stop. */
    if ((slice->step > 0 && slice->stop < slice->start) || (slice->step < 0 && slice->stop > slice->start)) {
        slice->stop = slice->start;
    }
    if (slice->step == 1) {
        tinypy_bool_t replaced = __tinypy_array_replace(array, (size_t)slice->start, (size_t)slice->stop, items, needed, out_error);
        return replaced;
    }
    if (needed == 0U) {
        size_t start = (size_t)slice->start;
        size_t step = (size_t)slice->step;

        if (slice->step < 0 && slice->length != 0U) {
            start = (size_t)(slice->start + slice->step * (int64_t)(slice->length - 1U));
            step = (size_t)(-slice->step);
        }
        tinypy_bool_t deleted = __tinypy_array_delete_extended(array, start, step, slice->length, out_error);
        return deleted;
    }
    if (needed != slice->length) {
        char needed_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
        char length_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
        size_t needed_size = tinypy_internal_format_size(needed_buffer, needed);
        size_t length_size = tinypy_internal_format_size(length_buffer, slice->length);
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("attempt to assign array of size "),
            {needed_buffer, needed_size},
            TINYPY_MESSAGE_PART_LITERAL(" to extended slice of size "),
            {length_buffer, length_size}
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    tinypy_array_payload_t *payload = __tinypy_array_payload(array);
    size_t item_size = payload->descriptor->item_size;
    for (size_t index = 0U; index < needed; ++index) {
        size_t position = (size_t)(slice->start + (int64_t)index * slice->step);

        (void)memcpy(payload->items + position * item_size, items + index * item_size, item_size);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* array_ass_subscr: value NULL deletes. */
static tinypy_bool_t __tinypy_array_assign_subscript(tinypy_value_t *instance, void *payload, tinypy_value_t *key, tinypy_value_t *value, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    tinypy_internal_slice_indices_t slice;

    (void)payload;
    (void)user_data;
    if (tinypy_internal_index_check(key) != TINYPY_FALSE) {
        size_t index;

        if (__tinypy_array_index(instance, key, "array assignment index out of range", &index, out_error) == TINYPY_FALSE) {
            return TINYPY_FALSE;
        }
        if (value != NULL) {
            tinypy_bool_t stored = __tinypy_array_store(instance, index, value, out_error);
            return stored;
        }
        slice.start = (int64_t)index;
        slice.stop = (int64_t)index + 1;
        slice.step = 1;
        slice.length = 1U;
    }
    else if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
        if (tinypy_internal_slice_unpack(key, &slice, out_error) == TINYPY_FALSE) {
            return TINYPY_FALSE;
        }
        if (tinypy_internal_slice_adjust_indices(vm, __tinypy_array_payload(instance)->size, &slice, out_error) == TINYPY_FALSE) {
            return TINYPY_FALSE;
        }
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "array indices must be integer", out_error);
        return TINYPY_FALSE;
    }
    /* array_ass_subscr assigns a copy of the array assigned to a slice of
       itself through a call of its own, which reads the slice again. */
    if (value == instance) {
        tinypy_value_t *copy = __tinypy_array_copy(instance, out_error);
        if (copy == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t assigned = __tinypy_array_assign_subscript(instance, payload, key, copy, user_data, out_error);
        TINYPY_DECREF(copy);
        return assigned;
    }
    if (__tinypy_array_replacement_check(instance, value, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t assigned = __tinypy_array_assign_slice(instance, &slice, value, out_error);
    return assigned;
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __tinypy_array_length(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)instance;
    (void)user_data;
    TINYPY_CLEAR_ERROR(out_error);
    ptrdiff_t length = (ptrdiff_t)((tinypy_array_payload_t *)payload)->size;
    return length;
}
//////////////////////////////////////////////////////////////////////////
/* array_item, the sq_item that the sequence protocol calls with an index. */
static tinypy_value_t *__tinypy_array_sequence_item(tinypy_value_t *instance, void *payload, tinypy_value_t *key, void *user_data, tinypy_error_t **out_error) {
    size_t index;

    (void)payload;
    (void)user_data;
    if (__tinypy_array_index(instance, key, "array index out of range", &index, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_array_item(instance, index, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The first item from *in_out_index on that compares equal to value: 1 with
   its index in *in_out_index, 0 when none does and -1 on error. The size is
   read again after each comparison, which may run Python code. */
static int32_t __tinypy_array_find(tinypy_value_t *array, tinypy_value_t *value, size_t *in_out_index, tinypy_error_t **out_error) {
    size_t index;

    for (index = *in_out_index; index < __tinypy_array_payload(array)->size; ++index) {
        tinypy_value_t *item = __tinypy_array_item(array, index, out_error);
        if (item == NULL) {
            return -INT32_C(1);
        }
        int32_t equal = tinypy_compare_bool(item, value, TINYPY_COMPARE_EQUAL, out_error);
        TINYPY_DECREF(item);
        if (equal != 0) {
            *in_out_index = index;
            return equal;
        }
    }
    *in_out_index = index;
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_array_contains(tinypy_value_t *instance, void *payload, tinypy_value_t *item, void *user_data, tinypy_error_t **out_error) {
    size_t index = 0U;

    (void)payload;
    (void)user_data;
    int32_t found = __tinypy_array_find(instance, item, &index, out_error);
    return found;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_iter(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)payload;
    (void)user_data;
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *result = tinypy_internal_array_iterator_new(instance);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The item an arrayiterator reads at index, NULL past the end without an
   error: the iterator keeps the array, which may grow again. */
tinypy_value_t *tinypy_internal_array_iterator_item(tinypy_value_t *array, size_t index, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    if (index >= __tinypy_array_payload(array)->size) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_array_item(array, index, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* An array has the tp_richcompare of array.array without a tp_hash: the
   array is unhashable, while a Python subclass takes the __hash__ of object
   that update_one_slot finds. */
static tinypy_hash_t __tinypy_array_hash(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)payload;
    (void)user_data;
    if (instance->type == TINYPY_VALUE_VM(instance)->array_type) {
        tinypy_internal_hash_unhashable_error(instance, out_error);
        return (tinypy_hash_t)0;
    }
    tinypy_hash_t hash = (tinypy_hash_t)((uintptr_t)instance >> 4U);
    return hash;
}
//////////////////////////////////////////////////////////////////////////
/* array_richcompare: the items compare in order, whatever the typecodes,
   and the first pair that differs or the sizes decide. */
static tinypy_value_t *__tinypy_array_compare(tinypy_value_t *instance, void *payload, tinypy_value_t *other, tinypy_compare_operation_e operation, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    tinypy_value_t *left = NULL;
    tinypy_value_t *right = NULL;
    int32_t equal = INT32_C(1);

    (void)payload;
    (void)user_data;
    TINYPY_CLEAR_ERROR(out_error);
    if (tinypy_internal_array_check(other) == TINYPY_FALSE) {
        tinypy_value_t *result = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return result;
    }
    if (__tinypy_array_payload(instance)->size != __tinypy_array_payload(other)->size && (operation == TINYPY_COMPARE_EQUAL || operation == TINYPY_COMPARE_NOT_EQUAL)) {
        tinypy_value_t *result = tinypy_bool_from_i32(vm, operation == TINYPY_COMPARE_NOT_EQUAL);
        return result;
    }
    for (size_t index = 0U; index < __tinypy_array_payload(instance)->size && index < __tinypy_array_payload(other)->size; ++index) {
        left = __tinypy_array_item(instance, index, out_error);
        if (left == NULL) {
            return NULL;
        }
        right = __tinypy_array_item(other, index, out_error);
        if (right == NULL) {
            TINYPY_DECREF(left);
            return NULL;
        }
        equal = tinypy_compare_bool(left, right, TINYPY_COMPARE_EQUAL, out_error);
        if (equal == 0) {
            break;
        }
        TINYPY_DECREF(left);
        TINYPY_DECREF(right);
        if (equal < 0) {
            return NULL;
        }
    }
    if (equal != 0) {
        size_t left_size = __tinypy_array_payload(instance)->size;
        size_t right_size = __tinypy_array_payload(other)->size;
        int32_t order = left_size < right_size ? -1 : (left_size > right_size ? 1 : 0);
        int32_t holds = tinypy_internal_comparison_order_holds(operation, order);
        tinypy_value_t *result = tinypy_bool_from_i32(vm, holds);
        return result;
    }
    tinypy_value_t *result;
    if (operation == TINYPY_COMPARE_EQUAL || operation == TINYPY_COMPARE_NOT_EQUAL) {
        result = tinypy_bool_from_i32(vm, operation == TINYPY_COMPARE_NOT_EQUAL);
    }
    else {
        result = tinypy_compare_value(left, right, operation, out_error);
    }
    TINYPY_DECREF(left);
    TINYPY_DECREF(right);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_list(tinypy_value_t *array, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(array);
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);

    if (tinypy_internal_list_reserve_checked(vm, result, __tinypy_array_payload(array)->size, out_error) == TINYPY_FALSE) {
        TINYPY_DECREF(result);
        return NULL;
    }
    for (size_t index = 0U; index < __tinypy_array_payload(array)->size; ++index) {
        tinypy_value_t *item = __tinypy_array_item(array, index, out_error);
        if (item == NULL) {
            TINYPY_DECREF(result);
            return NULL;
        }
        tinypy_bool_t appended = tinypy_internal_list_append_checked(result, item, out_error);
        TINYPY_DECREF(item);
        if (appended == TINYPY_FALSE) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_string(tinypy_value_t *array, tinypy_error_t **out_error) {
    size_t size;
    const uint8_t *bytes = tinypy_internal_array_view(array, &size);
    tinypy_value_t *result = tinypy_internal_string_from_bytes_checked(TINYPY_VALUE_VM(array), bytes, size, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* array_repr: the typecode, then the repr of the str of a 'c' array, of the
   unicode of a 'u' array or of the list of any other one. */
static tinypy_value_t *__tinypy_array_repr(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    const tinypy_array_payload_t *array = (const tinypy_array_payload_t *)payload;
    uint8_t typecode = array->descriptor->typecode;
    char prefix[] = "array('?', ";

    (void)user_data;
    TINYPY_CLEAR_ERROR(out_error);
    prefix[7] = (char)typecode;
    if (array->size == 0U) {
        char empty[] = "array('?')";

        empty[7] = (char)typecode;
        tinypy_value_t *result = tinypy_internal_string_from_bytes_checked(vm, empty, sizeof(empty) - 1U, out_error);
        return result;
    }
    tinypy_value_t *items;
    if (typecode == (uint8_t)'c') {
        items = __tinypy_array_string(instance, out_error);
    }
    else if (typecode == (uint8_t)'u') {
        items = __tinypy_array_unicode(vm, array->items, array->size, out_error);
    }
    else {
        items = __tinypy_array_list(instance, out_error);
    }
    if (items == NULL) {
        return NULL;
    }
    tinypy_value_t *text = tinypy_object_repr(items, out_error);
    TINYPY_DECREF(items);
    if (text == NULL) {
        return NULL;
    }
    size_t text_size = TINYPY_TEXT_BYTE_SIZE(text);
    size_t size = sizeof(prefix) - 1U + text_size + 1U;
    uint8_t *bytes;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, size, size, &bytes, out_error);
    if (result != NULL) {
        (void)memcpy(bytes, prefix, sizeof(prefix) - 1U);
        (void)memcpy(bytes + sizeof(prefix) - 1U, TINYPY_TEXT_BYTES(text), text_size);
        bytes[size - 1U] = (uint8_t)')';
    }
    TINYPY_DECREF(text);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* array_concat and array_inplace_concat, the sq_concat and
   sq_inplace_concat of an array: the result of a concatenation is an
   array.array. */
tinypy_value_t *tinypy_internal_array_concat(tinypy_value_t *array, tinypy_value_t *other, tinypy_bool_t inplace, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(array);

    TINYPY_CLEAR_ERROR(out_error);
    if (tinypy_internal_array_check(other) == TINYPY_FALSE) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("can only append array (not \""),
            TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(other, 200U),
            TINYPY_MESSAGE_PART_LITERAL("\") to array")
        };

        if (inplace != TINYPY_FALSE) {
            parts[0] = (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("can only extend array with array (not \"");
            parts[2] = (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("\")");
        }
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (inplace != TINYPY_FALSE) {
        if (__tinypy_array_extend(array, other, out_error) == TINYPY_FALSE) {
            return NULL;
        }
        return TINYPY_RET(array);
    }
    const tinypy_array_payload_t *payload = __tinypy_array_payload(array);
    const tinypy_array_payload_t *other_payload = __tinypy_array_payload(other);
    if (other_payload->descriptor != payload->descriptor) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bad argument type for built-in operation", out_error);
        return NULL;
    }
    size_t item_size = payload->descriptor->item_size;
    if (other_payload->size > (size_t)PTRDIFF_MAX / item_size - payload->size) {
        tinypy_internal_vm_allocation_error(vm, out_error);
        return NULL;
    }
    tinypy_value_t *result = __tinypy_array_new(vm->array_type, payload->descriptor, payload->size + other_payload->size, out_error);
    if (result == NULL) {
        return NULL;
    }
    uint8_t *items = __tinypy_array_payload(result)->items;
    if (payload->size != 0U) {
        (void)memcpy(items, payload->items, payload->size * item_size);
    }
    if (other_payload->size != 0U) {
        (void)memcpy(items + payload->size * item_size, other_payload->items, other_payload->size * item_size);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* array_repeat and array_inplace_repeat, which sequence_repeat calls with a
   count it converted; a negative count repeats nothing. */
tinypy_value_t *tinypy_internal_array_repeat(tinypy_value_t *array, int64_t count, tinypy_bool_t inplace, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(array);
    tinypy_array_payload_t *payload = __tinypy_array_payload(array);
    size_t item_size = payload->descriptor->item_size;
    size_t size = payload->size;
    size_t repeat = 0U;

    TINYPY_CLEAR_ERROR(out_error);
    if (count > 0 && size != 0U) {
        if ((uint64_t)count > (uint64_t)((size_t)PTRDIFF_MAX / item_size / size)) {
            tinypy_internal_vm_allocation_error(vm, out_error);
            return NULL;
        }
        repeat = (size_t)count;
    }
    if (inplace == TINYPY_FALSE) {
        tinypy_value_t *result = __tinypy_array_new(vm->array_type, payload->descriptor, size * repeat, out_error);
        if (result == NULL) {
            return NULL;
        }
        uint8_t *items = __tinypy_array_payload(result)->items;
        for (size_t index = 0U; index < repeat; ++index) {
            (void)memcpy(items + index * size * item_size, payload->items, size * item_size);
        }
        return result;
    }
    /* array_inplace_repeat frees the items of an array repeated at most no
       times. */
    if (size != 0U && repeat == 0U) {
        if (__tinypy_array_resize_allowed(array, 0U, out_error) == TINYPY_FALSE) {
            return NULL;
        }
        __tinypy_array_release_items(array);
    }
    else if (size != 0U) {
        if (__tinypy_array_resize(array, size * repeat, out_error) == TINYPY_FALSE) {
            return NULL;
        }
        for (size_t index = 1U; index < repeat; ++index) {
            (void)memcpy(payload->items + index * size * item_size, payload->items, size * item_size);
        }
    }
    return TINYPY_RET(array);
}
//////////////////////////////////////////////////////////////////////////
/* The sequence of an array initializer: a list or a tuple, read with
   PySequence_Size and PySequence_GetItem, which a subclass may override. */
static tinypy_bool_t __tinypy_array_sequence_size(tinypy_vm_t *vm, tinypy_value_t *sequence, size_t *out_size, tinypy_error_t **out_error) {
    if (tinypy_internal_object_has_special_override_key(sequence, vm->internal_special_length_key) == TINYPY_FALSE) {
        *out_size = TINYPY_SIZED_SIZE(sequence);
        return TINYPY_TRUE;
    }
    tinypy_value_t *method = tinypy_internal_object_get_special_key(sequence, vm->internal_special_length_key, out_error);
    if (method == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_value_t *empty = TINYPY_RET_EMPTY_TUPLE(vm);
    tinypy_value_t *length_value = tinypy_call(method, empty, NULL, out_error);
    TINYPY_DECREF(empty);
    TINYPY_DECREF(method);
    if (length_value == NULL) {
        return TINYPY_FALSE;
    }
    int64_t length;
    tinypy_bool_t converted = tinypy_internal_number_as_ssize(length_value, &length, out_error);
    TINYPY_DECREF(length_value);
    if (converted == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    if (length < 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__() should return >= 0", out_error);
        return TINYPY_FALSE;
    }
    /* A length beyond the address space of a 32-bit host is as unallocatable
       as one beyond the storage limit. */
    if ((uint64_t)length > (uint64_t)PTRDIFF_MAX) {
        tinypy_internal_vm_allocation_error(vm, out_error);
        return TINYPY_FALSE;
    }
    *out_size = (size_t)length;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_sequence_get(tinypy_vm_t *vm, tinypy_value_t *sequence, size_t index, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(sequence);

    if (sequence->type == &vm->types[kind]) {
        if (index >= TINYPY_SIZED_SIZE(sequence)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, kind == TINYPY_VALUE_LIST ? "list index out of range" : "tuple index out of range", out_error);
            return NULL;
        }
        tinypy_value_t *item = kind == TINYPY_VALUE_LIST ? TINYPY_LIST_GET(sequence, index) : TINYPY_TUPLE_GET(sequence, index);
        return TINYPY_RET(item);
    }
    tinypy_value_t *key = tinypy_integer_from_i64(vm, (int64_t)index);
    tinypy_value_t *result = tinypy_get_item(sequence, key, out_error);
    TINYPY_DECREF(key);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_array_initialize_sequence(tinypy_value_t *array, tinypy_value_t *sequence, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(array);
    size_t size;

    if (__tinypy_array_sequence_size(vm, sequence, &size, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    if (__tinypy_array_resize(array, size, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    for (size_t index = 0U; index < size; ++index) {
        tinypy_value_t *item = __tinypy_array_sequence_get(vm, sequence, index, out_error);
        if (item == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t stored = __tinypy_array_store(array, index, item, out_error);
        TINYPY_DECREF(item);
        if (stored == TINYPY_FALSE) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Appends the code points of a unicode to a 'u' array. */
static tinypy_bool_t __tinypy_array_append_unicode(tinypy_value_t *array, tinypy_value_t *text, tinypy_error_t **out_error) {
    tinypy_array_payload_t *payload = __tinypy_array_payload(array);
    size_t size = payload->size;
    size_t count = TINYPY_SIZED_SIZE(text);
    const uint8_t *bytes = TINYPY_TEXT_BYTES(text);
    size_t byte_size = TINYPY_TEXT_BYTE_SIZE(text);

    if (count == 0U) {
        return TINYPY_TRUE;
    }
    if (count > (size_t)PTRDIFF_MAX / sizeof(uint32_t) - size) {
        tinypy_internal_vm_allocation_error(TINYPY_VALUE_VM(array), out_error);
        return TINYPY_FALSE;
    }
    if (__tinypy_array_resize(array, size + count, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    size_t offset = 0U;
    for (size_t index = 0U; index < count; ++index) {
        uint32_t code_point;

        offset += tinypy_internal_utf8_decode(bytes + offset, byte_size - offset, &code_point);
        (void)memcpy(payload->items + (size + index) * sizeof(code_point), &code_point, sizeof(code_point));
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* The 's#' format of PyArg_ParseTuple for fromstring: a str, a unicode in
   the default encoding, or an object with a read-only old buffer, which a
   buffer or an array has and a bytearray or a memoryview, which release
   their buffers, have not. *out_owner keeps the bytes alive; for a buffer of
   an array it is that array. */
static tinypy_bool_t __tinypy_array_string_argument(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t **out_owner, const uint8_t **out_bytes, size_t *out_size, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    *out_owner = NULL;
    if (kind == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *encoded = tinypy_internal_text_codec(vm, value, NULL, NULL, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);
        if (encoded == NULL) {
            return TINYPY_FALSE;
        }
        *out_owner = encoded;
        *out_bytes = (const uint8_t *)tinypy_string_view(encoded, out_size);
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_BUFFER) {
        *out_owner = TINYPY_RET(kind == TINYPY_VALUE_BUFFER ? TINYPY_BUFFER_OBJECT(value)->owner : value);
        *out_bytes = kind == TINYPY_VALUE_STRING ? (const uint8_t *)tinypy_string_view(value, out_size) : (const uint8_t *)tinypy_buffer_view(value, out_size);
        return TINYPY_TRUE;
    }
    if (tinypy_internal_array_check(value) != TINYPY_FALSE) {
        *out_owner = TINYPY_RET(value);
        *out_bytes = tinypy_internal_array_view(value, out_size);
        return TINYPY_TRUE;
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("fromstring() argument 1 must be string or read-only buffer, not "),
        TINYPY_MESSAGE_PART_ARGUMENT_TYPE_NAME(value)
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* array_fromstring. A view of the items of the array itself is located
   again after the resize that may move them, where CPython 2.7 reads the
   released storage. */
static tinypy_bool_t __tinypy_array_from_string(tinypy_value_t *array, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(array);
    tinypy_array_payload_t *payload = __tinypy_array_payload(array);
    size_t item_size = payload->descriptor->item_size;
    tinypy_value_t *owner;
    const uint8_t *bytes;
    size_t size;

    if (__tinypy_array_string_argument(vm, value, &owner, &bytes, &size, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t appended = TINYPY_FALSE;
    if (owner == array && payload->items != NULL && bytes == payload->items) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "array.fromstring(x): x cannot be self", out_error);
    }
    else if (size % item_size != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "string length not a multiple of item size", out_error);
    }
    else if (owner == array && size != 0U) {
        size_t offset = (size_t)(bytes - payload->items);
        size_t old_size = payload->size;

        appended = __tinypy_array_resize(array, old_size + size / item_size, out_error);
        if (appended != TINYPY_FALSE) {
            (void)memcpy(payload->items + old_size * item_size, payload->items + offset, size);
        }
    }
    else {
        appended = __tinypy_array_append_items(array, bytes, size / item_size, out_error);
    }
    TINYPY_DECREF(owner);
    return appended;
}
//////////////////////////////////////////////////////////////////////////
/* Fills a new array from its initializer: the items of an iterator taken
   from it, a str as fromstring, a unicode of a 'u' array, or a list or a
   tuple item by item. */
static tinypy_bool_t __tinypy_array_initialize(tinypy_value_t *array, tinypy_value_t *initializer, tinypy_value_t *iterator, tinypy_error_t **out_error) {
    if (iterator != NULL) {
        tinypy_bool_t extended = __tinypy_array_extend_iterator(array, iterator, out_error);
        return extended;
    }
    if (initializer == NULL) {
        return TINYPY_TRUE;
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(initializer);
    if (kind == TINYPY_VALUE_STRING) {
        tinypy_bool_t appended = __tinypy_array_from_string(array, initializer, out_error);
        return appended;
    }
    if (kind == TINYPY_VALUE_UNICODE) {
        tinypy_bool_t appended = __tinypy_array_append_unicode(array, initializer, out_error);
        return appended;
    }
    tinypy_bool_t initialized = __tinypy_array_initialize_sequence(array, initializer, out_error);
    return initialized;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_array_typecode_error(tinypy_vm_t *vm, tinypy_value_t *typecode, tinypy_error_t **out_error) {
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("array() argument 1 or typecode must be char (string or ascii-unicode with length 1), not "),
        TINYPY_MESSAGE_PART_TYPE_NAME(typecode)
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
/* array_new: array.array takes no keywords, which a subclass passes to its
   __init__. A list or a tuple fills the array item by item, a str as
   fromstring, a unicode a 'u' array; any other initializer is iterated, an
   iterator being taken before the typecode is looked up. */
static tinypy_value_t *__tinypy_array_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);
    uint32_t typecode;

    if (type == vm->array_type && kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "array.array() does not take keyword arguments", out_error);
        return NULL;
    }
    if (count < 1U || count > 2U) {
        tinypy_internal_make_arity_error(vm, "array", 5U, count, 1U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }
    tinypy_value_t *typecode_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *initializer = count == 2U ? TINYPY_TUPLE_GET(args, 1U) : NULL;
    tinypy_value_type_e typecode_kind = TINYPY_VALUE_KIND(typecode_value);
    if (typecode_kind == TINYPY_VALUE_STRING && TINYPY_SIZED_SIZE(typecode_value) == 1U) {
        typecode = (uint32_t)TINYPY_STRING_OBJECT(typecode_value)->bytes[0];
    }
    else if (typecode_kind == TINYPY_VALUE_UNICODE && TINYPY_SIZED_SIZE(typecode_value) == 1U) {
        (void)tinypy_internal_utf8_decode(TINYPY_TEXT_BYTES(typecode_value), TINYPY_TEXT_BYTE_SIZE(typecode_value), &typecode);
    }
    else {
        __tinypy_array_typecode_error(vm, typecode_value, out_error);
        return NULL;
    }
    tinypy_value_t *iterator = NULL;
    tinypy_value_type_e initializer_kind = initializer != NULL ? TINYPY_VALUE_KIND(initializer) : TINYPY_VALUE_NONE;
    if (initializer != NULL && initializer_kind != TINYPY_VALUE_LIST && initializer_kind != TINYPY_VALUE_TUPLE && initializer_kind != TINYPY_VALUE_STRING && (typecode != (uint32_t)'u' || initializer_kind != TINYPY_VALUE_UNICODE)) {
        iterator = tinypy_iter(initializer, out_error);
        if (iterator == NULL) {
            return NULL;
        }
        initializer = NULL;
    }
    const tinypy_array_descriptor_t *descriptor = __tinypy_array_descriptor(typecode);
    if (descriptor == NULL) {
        if (iterator != NULL) {
            TINYPY_DECREF(iterator);
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "bad typecode (must be c, b, B, u, h, H, i, I, l, L, f or d)", out_error);
        return NULL;
    }
    tinypy_value_t *result = __tinypy_array_new(type, descriptor, 0U, out_error);
    tinypy_bool_t initialized = TINYPY_FALSE;
    if (result != NULL) {
        initialized = __tinypy_array_initialize(result, initializer, iterator, out_error);
    }
    if (iterator != NULL) {
        TINYPY_DECREF(iterator);
    }
    if (initialized == TINYPY_FALSE && result != NULL) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_none(tinypy_vm_t *vm, tinypy_bool_t succeeded) {
    if (succeeded == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_append_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)count;
    (void)kwargs;
    tinypy_bool_t appended = __tinypy_array_insert(self, (int64_t)__tinypy_array_payload(self)->size, items[0U], out_error);
    tinypy_value_t *result = __tinypy_array_none(TINYPY_VALUE_VM(function), appended);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* buffer_info: the address of the items, PyLong_FromVoidPtr's int, and the
   size. */
static tinypy_value_t *__tinypy_array_buffer_info_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const tinypy_array_payload_t *payload = __tinypy_array_payload(self);

    (void)items;
    (void)count;
    (void)kwargs;
    tinypy_value_t *fields[2] = {
        tinypy_integer_from_i64(vm, (int64_t)(uintptr_t)payload->items),
        tinypy_integer_from_i64(vm, (int64_t)payload->size)
    };
    tinypy_value_t *result = tinypy_internal_tuple_from_items_checked(vm, fields, 2U, out_error);
    TINYPY_DECREF(fields[1]);
    TINYPY_DECREF(fields[0]);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* array_byteswap: the bytes of each item in reverse order. */
static tinypy_value_t *__tinypy_array_byteswap_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_array_payload_t *payload = __tinypy_array_payload(self);
    size_t item_size = payload->descriptor->item_size;

    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    for (size_t index = 0U; index < payload->size; ++index) {
        uint8_t *item = payload->items + index * item_size;

        for (size_t low = 0U; low < item_size / 2U; ++low) {
            uint8_t byte = item[low];

            item[low] = item[item_size - 1U - low];
            item[item_size - 1U - low] = byte;
        }
    }
    tinypy_value_t *result = TINYPY_RET_NONE(TINYPY_VALUE_VM(function));
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* __copy__ and __deepcopy__, which ignores its memo. */
static tinypy_value_t *__tinypy_array_copy_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)function;
    (void)items;
    (void)count;
    (void)kwargs;
    tinypy_value_t *result = __tinypy_array_copy(self, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_count_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    size_t found = 0U;
    size_t index = 0U;

    (void)count;
    (void)kwargs;
    for (;;) {
        int32_t equal = __tinypy_array_find(self, items[0U], &index, out_error);
        if (equal < 0) {
            return NULL;
        }
        if (equal == 0) {
            break;
        }
        found += 1U;
        index += 1U;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(TINYPY_VALUE_VM(function), (int64_t)found);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_extend_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)count;
    (void)kwargs;
    tinypy_bool_t extended = __tinypy_array_extend(self, items[0U], out_error);
    tinypy_value_t *result = __tinypy_array_none(TINYPY_VALUE_VM(function), extended);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* fromfile and its alias read parse their arguments as "On:fromfile";
   a memory-only runtime has no file objects, which PyFile_AsFile asks for.
   The arguments of read are counted under the name fromfile, its keywords
   under its own. */
static tinypy_value_t *__tinypy_array_fromfile_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t item_count;

    (void)self;
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_value_t *name = tinypy_native_function_name(function);
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_TEXT(name),
            TINYPY_MESSAGE_PART_LITERAL("() takes no keyword arguments")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (count != 2U) {
        tinypy_internal_make_arity_error(vm, "fromfile", 8U, count, 2U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }
    if (tinypy_internal_integer_as_ssize(items[1U], &item_count, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "arg1 must be open file", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* tofile and its alias write require a file object, which a memory-only
   runtime has not. */
static tinypy_value_t *__tinypy_array_tofile_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)self;
    (void)items;
    (void)count;
    (void)kwargs;
    tinypy_internal_make_vm_error(TINYPY_VALUE_VM(function), TINYPY_ERROR_TYPE, "arg must be open file", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* array_fromlist converts every item of the list before the array grows;
   the list must keep its size through the conversions. */
static tinypy_value_t *__tinypy_array_fromlist_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const tinypy_array_descriptor_t *descriptor = __tinypy_array_payload(self)->descriptor;
    tinypy_value_t *list = items[0U];

    (void)count;
    (void)kwargs;
    if (TINYPY_VALUE_KIND(list) != TINYPY_VALUE_LIST) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "arg must be list", out_error);
        return NULL;
    }
    size_t size = TINYPY_LIST_SIZE(list);
    if (size == 0U) {
        tinypy_value_t *result = TINYPY_RET_NONE(vm);
        return result;
    }
    if (size > (size_t)PTRDIFF_MAX / descriptor->item_size) {
        tinypy_internal_vm_allocation_error(vm, out_error);
        return NULL;
    }
    size_t byte_size = size * descriptor->item_size;
    uint8_t *converted = (uint8_t *)__tinypy_internal_vm_allocate_checked(vm, byte_size, out_error);
    if (converted == NULL) {
        return NULL;
    }
    tinypy_bool_t appended = TINYPY_TRUE;
    for (size_t index = 0U; index < size && appended != TINYPY_FALSE; ++index) {
        tinypy_value_t *item = TINYPY_RET(TINYPY_LIST_GET(list, index));

        appended = descriptor->convert(vm, item, converted + index * descriptor->item_size, out_error);
        TINYPY_DECREF(item);
        if (appended != TINYPY_FALSE && TINYPY_LIST_SIZE(list) != size) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "list changed size during iteration", out_error);
            appended = TINYPY_FALSE;
        }
    }
    if (appended != TINYPY_FALSE) {
        appended = __tinypy_array_append_items(self, converted, size, out_error);
    }
    tinypy_internal_vm_deallocate(vm, converted, byte_size);
    tinypy_value_t *result = __tinypy_array_none(vm, appended);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_fromstring_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)count;
    (void)kwargs;
    tinypy_bool_t appended = __tinypy_array_from_string(self, items[0U], out_error);
    tinypy_value_t *result = __tinypy_array_none(TINYPY_VALUE_VM(function), appended);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The 'u#' format of fromunicode takes a unicode only, before the typecode
   is checked. */
static tinypy_value_t *__tinypy_array_fromunicode_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *text = items[0U];

    (void)count;
    (void)kwargs;
    if (TINYPY_VALUE_KIND(text) != TINYPY_VALUE_UNICODE) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("fromunicode() argument 1 must be cannot convert raw buffers, not "),
            TINYPY_MESSAGE_PART_ARGUMENT_TYPE_NAME(text)
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (__tinypy_array_payload(self)->descriptor->typecode != (uint8_t)'u') {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "fromunicode() may only be called on type 'u' arrays", out_error);
        return NULL;
    }
    tinypy_bool_t appended = __tinypy_array_append_unicode(self, text, out_error);
    tinypy_value_t *result = __tinypy_array_none(vm, appended);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_index_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t index = 0U;

    (void)count;
    (void)kwargs;
    int32_t found = __tinypy_array_find(self, items[0U], &index, out_error);
    if (found < 0) {
        return NULL;
    }
    if (found == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "array.index(x): x not in list", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)index);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* array_insert parses its arguments as "nO:insert". */
static tinypy_value_t *__tinypy_array_insert_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    int64_t where;

    (void)count;
    (void)kwargs;
    if (tinypy_internal_integer_as_ssize(items[0U], &where, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_bool_t inserted = __tinypy_array_insert(self, where, items[1U], out_error);
    tinypy_value_t *result = __tinypy_array_none(TINYPY_VALUE_VM(function), inserted);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* array_pop parses its index as "|n:pop" before it looks at the size. */
static tinypy_value_t *__tinypy_array_pop_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t index = -1;

    (void)kwargs;
    if (count != 0U && tinypy_internal_integer_as_ssize(items[0U], &index, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    size_t size = __tinypy_array_payload(self)->size;
    if (size == 0U) {
        __tinypy_array_index_error(vm, "pop from empty array", out_error);
        return NULL;
    }
    if (index < 0) {
        index += (int64_t)size;
    }
    if (index < 0 || (uint64_t)index >= (uint64_t)size) {
        __tinypy_array_index_error(vm, "pop index out of range", out_error);
        return NULL;
    }
    tinypy_value_t *result = __tinypy_array_item(self, (size_t)index, out_error);
    if (result == NULL) {
        return NULL;
    }
    if (__tinypy_array_replace(self, (size_t)index, (size_t)index + 1U, NULL, 0U, out_error) == TINYPY_FALSE) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* array_reduce: the type, the typecode with the list of the items, and the
   __dict__ of the array or None; the items are never pickled as a str, so
   that Python 3 can read the pickle. */
static tinypy_value_t *__tinypy_array_reduce_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *state;

    (void)items;
    (void)count;
    (void)kwargs;
    int32_t found = tinypy_internal_object_get_optional_attr_key(self, vm->internal_special_dict_key, &state, out_error);
    if (found < 0) {
        return NULL;
    }
    if (found == 0) {
        state = TINYPY_RET_NONE(vm);
    }
    tinypy_value_t *list = __tinypy_array_list(self, out_error);
    if (list == NULL) {
        TINYPY_DECREF(state);
        return NULL;
    }
    uint8_t typecode = __tinypy_array_payload(self)->descriptor->typecode;
    tinypy_value_t *typecode_value = tinypy_string_from_bytes(vm, &typecode, 1U);
    tinypy_value_t *arguments_items[2] = {typecode_value, list};
    tinypy_value_t *arguments = tinypy_tuple_from_items(vm, arguments_items, 2U);
    tinypy_value_t *result_items[3] = {&self->type->base.base, arguments, state};
    tinypy_value_t *result = tinypy_tuple_from_items(vm, result_items, 3U);

    TINYPY_DECREF(arguments);
    TINYPY_DECREF(typecode_value);
    TINYPY_DECREF(list);
    TINYPY_DECREF(state);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_remove_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t index = 0U;

    (void)count;
    (void)kwargs;
    int32_t found = __tinypy_array_find(self, items[0U], &index, out_error);
    if (found < 0) {
        return NULL;
    }
    if (found == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "array.remove(x): x not in list", out_error);
        return NULL;
    }
    /* array_ass_slice bounds the item by the size the comparison left. */
    size_t size = __tinypy_array_payload(self)->size;
    size_t start = index < size ? index : size;
    size_t stop = index + 1U < size ? index + 1U : size;
    tinypy_bool_t removed = __tinypy_array_replace(self, start, stop, NULL, 0U, out_error);
    tinypy_value_t *result = __tinypy_array_none(vm, removed);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_reverse_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_array_payload_t *payload = __tinypy_array_payload(self);
    size_t item_size = payload->descriptor->item_size;
    uint8_t item[TINYPY_ARRAY_ITEM_SIZE_MAX];

    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    for (size_t low = 0U; low < payload->size / 2U; ++low) {
        uint8_t *front = payload->items + low * item_size;
        uint8_t *back = payload->items + (payload->size - 1U - low) * item_size;

        (void)memcpy(item, front, item_size);
        (void)memcpy(front, back, item_size);
        (void)memcpy(back, item, item_size);
    }
    tinypy_value_t *result = TINYPY_RET_NONE(TINYPY_VALUE_VM(function));
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* __sizeof__: the instance and the storage of the items, as a long. */
static tinypy_value_t *__tinypy_array_sizeof_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    const tinypy_array_payload_t *payload = __tinypy_array_payload(self);

    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    tinypy_value_t *result = tinypy_long_from_i64(TINYPY_VALUE_VM(function), (int64_t)(self->type->basic_size + payload->capacity * payload->descriptor->item_size));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_tolist_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)function;
    (void)items;
    (void)count;
    (void)kwargs;
    tinypy_value_t *result = __tinypy_array_list(self, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_tostring_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)function;
    (void)items;
    (void)count;
    (void)kwargs;
    tinypy_value_t *result = __tinypy_array_string(self, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_tounicode_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const tinypy_array_payload_t *payload = __tinypy_array_payload(self);

    (void)items;
    (void)count;
    (void)kwargs;
    if (payload->descriptor->typecode != (uint8_t)'u') {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "tounicode() may only be called on type 'u' arrays", out_error);
        return NULL;
    }
    tinypy_value_t *result = __tinypy_array_unicode(vm, payload->items, payload->size, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The slot wrappers of the array slots that a native type spec has no
   callback for: sq_concat, sq_repeat and their in-place forms, which the
   wrappers call directly, and the legacy slices. */
static tinypy_value_t *__tinypy_array_concat_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_bool_t inplace = tinypy_native_function_user_data(function) != NULL ? TINYPY_TRUE : TINYPY_FALSE;

    (void)count;
    (void)kwargs;
    tinypy_value_t *result = tinypy_internal_array_concat(self, items[0U], inplace, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* wrap_indexargfunc converts the count with PyNumber_AsSsize_t, OverflowError
   for an overflow. */
static tinypy_value_t *__tinypy_array_repeat_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_bool_t inplace = tinypy_native_function_user_data(function) != NULL ? TINYPY_TRUE : TINYPY_FALSE;
    int64_t repeat;

    (void)count;
    (void)kwargs;
    if (tinypy_internal_number_as_index(items[0U], TINYPY_ERROR_OVERFLOW, &repeat, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_array_repeat(self, repeat, inplace, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* wrap_ssizessizeargfunc passes the offsets to array_slice as they are. */
static tinypy_value_t *__tinypy_array_getslice_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    int64_t offsets[2];

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 2U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (tinypy_internal_slice_offset_arguments(args, offsets, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_array_slice(TINYPY_TUPLE_GET(args, 0U), offsets[0], offsets[1], out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* __setslice__ and __delslice__, whose user data tells them apart, pass the
   offsets to array_ass_slice as they are. */
static tinypy_value_t *__tinypy_array_setslice_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    size_t arguments = user_data != NULL ? 3U : 2U;
    int64_t offsets[2];

    if (tinypy_internal_native_method_arguments(function, args, kwargs, arguments, arguments, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (tinypy_internal_slice_offset_arguments(args, offsets, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *value = user_data != NULL ? TINYPY_TUPLE_GET(args, 3U) : NULL;
    tinypy_bool_t assigned = tinypy_internal_array_assign_slice(TINYPY_TUPLE_GET(args, 0U), offsets[0], offsets[1], value, out_error);
    tinypy_value_t *result = __tinypy_array_none(TINYPY_VALUE_VM(function), assigned);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_length_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    tinypy_value_t *result = tinypy_integer_from_i64(TINYPY_VALUE_VM(function), (int64_t)__tinypy_array_payload(self)->size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_getitem_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)function;
    (void)count;
    (void)kwargs;
    tinypy_value_t *result = __tinypy_array_subscript(self, __tinypy_array_payload(self), items[0U], NULL, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* __setitem__ and __delitem__, whose item count tells them apart. */
static tinypy_value_t *__tinypy_array_setitem_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)kwargs;
    tinypy_bool_t assigned = __tinypy_array_assign_subscript(self, __tinypy_array_payload(self), items[0U], count == 2U ? items[1U] : NULL, NULL, out_error);
    tinypy_value_t *result = __tinypy_array_none(TINYPY_VALUE_VM(function), assigned);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_contains_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)count;
    (void)kwargs;
    int32_t found = __tinypy_array_contains(self, __tinypy_array_payload(self), items[0U], NULL, out_error);
    if (found < 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_bool_from_i32(TINYPY_VALUE_VM(function), found);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_iter_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)function;
    (void)items;
    (void)count;
    (void)kwargs;
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *result = tinypy_internal_array_iterator_new(self);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_repr_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)function;
    (void)items;
    (void)count;
    (void)kwargs;
    tinypy_value_t *result = __tinypy_array_repr(self, __tinypy_array_payload(self), NULL, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_array_compare_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_compare_operation_e operation = (tinypy_compare_operation_e)(intptr_t)tinypy_native_function_user_data(function);

    (void)count;
    (void)kwargs;
    tinypy_value_t *result = __tinypy_array_compare(self, __tinypy_array_payload(self), items[0U], operation, NULL, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The getsets typecode and itemsize, by index. */
tinypy_value_t *tinypy_internal_array_field(tinypy_value_t *value, size_t index) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    const tinypy_array_descriptor_t *descriptor = __tinypy_array_payload(value)->descriptor;

    if (index == 0U) {
        tinypy_value_t *result = tinypy_string_from_bytes(vm, &descriptor->typecode, 1U);
        return result;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)descriptor->item_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_array_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_internal_instance_release_references(value, visit, user_data);
}
//////////////////////////////////////////////////////////////////////////
/* The items live outside the instance, so the shutdown sweep of
   tinypy_vm_destroy releases them too. */
static void __tinypy_array_destroy(tinypy_value_t *value) {
    tinypy_internal_native_instance_destroy(value);
    __tinypy_array_release_items(value);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_array_module(tinypy_vm_t *vm) {
    static const char module_doc[] =
        "This module defines an object type which can efficiently represent\n"
        "an array of basic values: characters, integers, floating point\n"
        "numbers.  Arrays are sequence types and behave very much like lists,\n"
        "except that the type of objects stored in them is constrained.  The\n"
        "type is specified at object creation time by using a type code, which\n"
        "is a single character.  The following type codes are defined:\n"
        "\n"
        "    Type code   C Type             Minimum size in bytes \n"
        "    'c'         character          1 \n"
        "    'b'         signed integer     1 \n"
        "    'B'         unsigned integer   1 \n"
        "    'u'         Unicode character  2 \n"
        "    'h'         signed integer     2 \n"
        "    'H'         unsigned integer   2 \n"
        "    'i'         signed integer     2 \n"
        "    'I'         unsigned integer   2 \n"
        "    'l'         signed integer     4 \n"
        "    'L'         unsigned integer   4 \n"
        "    'f'         floating point     4 \n"
        "    'd'         floating point     8 \n"
        "\n"
        "The constructor is:\n"
        "\n"
        "array(typecode [, initializer]) -- create a new array\n";
    static const char type_doc[] =
        "array(typecode [, initializer]) -> array\n"
        "\n"
        "Return a new array whose items are restricted by typecode, and\n"
        "initialized from the optional initializer value, which must be a list,\n"
        "string or iterable over elements of the appropriate type.\n"
        "\n"
        "Arrays represent basic values and behave very much like lists, except\n"
        "the type of objects stored in them is constrained.\n"
        "\n"
        "Methods:\n"
        "\n"
        "append() -- append a new item to the end of the array\n"
        "buffer_info() -- return information giving the current memory info\n"
        "byteswap() -- byteswap all the items of the array\n"
        "count() -- return number of occurrences of an object\n"
        "extend() -- extend array by appending multiple elements from an iterable\n"
        "fromfile() -- read items from a file object\n"
        "fromlist() -- append items from the list\n"
        "fromstring() -- append items from the string\n"
        "index() -- return index of first occurrence of an object\n"
        "insert() -- insert a new item into the array at a provided position\n"
        "pop() -- remove and return item (default last)\n"
        "read() -- DEPRECATED, use fromfile()\n"
        "remove() -- remove first occurrence of an object\n"
        "reverse() -- reverse the order of the items in the array\n"
        "tofile() -- write all items to a file object\n"
        "tolist() -- return the array converted to an ordinary list\n"
        "tostring() -- return the array converted to a string\n"
        "write() -- DEPRECATED, use tofile()\n"
        "\n"
        "Attributes:\n"
        "\n"
        "typecode -- the typecode character used to create the array\n"
        "itemsize -- the length in bytes of one array item\n";
    tinypy_value_t *const comparison_names[] = {vm->internal_special_lt_key, vm->internal_special_le_key, vm->internal_special_eq_key, vm->internal_special_ne_key, vm->internal_special_gt_key, vm->internal_special_ge_key};
    tinypy_native_type_spec_t spec;

    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(tinypy_array_payload_t);
    spec.repr = __tinypy_array_repr;
    spec.hash = __tinypy_array_hash;
    spec.compare = __tinypy_array_compare;
    spec.mapping_get = __tinypy_array_subscript;
    spec.mapping_set = __tinypy_array_assign_subscript;
    spec.mapping_length = __tinypy_array_length;
    spec.sequence_get = __tinypy_array_sequence_item;
    spec.sequence_length = __tinypy_array_length;
    spec.contains = __tinypy_array_contains;
    spec.iter = __tinypy_array_iter;
    spec.has_instance_dict = TINYPY_FALSE;
    spec.has_weakrefs = TINYPY_TRUE;
    tinypy_type_t *type = tinypy_internal_native_type_new_key(vm->internal_array_dot_array_key, NULL, 0U, NULL, &spec, NULL);
    tinypy_value_t *type_doc_value = tinypy_string_from_bytes(vm, type_doc, sizeof(type_doc) - 1U);

    type->release_references = __tinypy_array_release_references;
    type->traverse_references = __tinypy_array_release_references;
    type->destroy = __tinypy_array_destroy;
    type->create = __tinypy_array_create;
    tinypy_dict_delete(type->dict, vm->internal_special_weakref_key);
    /* A type created while code runs takes the __module__ of that code;
       array.array finds its module in its name. */
    tinypy_dict_delete(type->dict, vm->internal_special_module_key);
    tinypy_type_set_attr_key(type, vm->internal_special_doc_key, type_doc_value);
    TINYPY_DECREF(type_doc_value);
    tinypy_internal_constructor_add_builtin_new(type);
    tinypy_internal_type_add_object_getattribute_method(type);
    tinypy_internal_initialize_array_descriptors(type);
    tinypy_internal_type_add_items_method(type, vm->internal_append_key, __tinypy_array_append_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(type, vm->internal_buffer_info_key, __tinypy_array_buffer_info_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(type, vm->internal_byteswap_key, __tinypy_array_byteswap_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(type, vm->internal_special_copy_key, __tinypy_array_copy_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(type, vm->internal_count_key, __tinypy_array_count_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(type, vm->internal_special_deepcopy_key, __tinypy_array_copy_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(type, vm->internal_extend_key, __tinypy_array_extend_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(type, vm->internal_fromfile_key, __tinypy_array_fromfile_method, NULL, 0U, SIZE_MAX, TINYPY_ARITY_STYLE_UNCHECKED);
    tinypy_internal_type_add_items_method(type, vm->internal_fromlist_key, __tinypy_array_fromlist_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(type, vm->internal_fromstring_key, __tinypy_array_fromstring_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(type, vm->internal_fromunicode_key, __tinypy_array_fromunicode_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(type, vm->internal_index_key, __tinypy_array_index_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(type, vm->internal_insert_key, __tinypy_array_insert_method, NULL, 2U, 2U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(type, vm->internal_pop_key, __tinypy_array_pop_method, NULL, 0U, 1U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(type, vm->internal_read_key, __tinypy_array_fromfile_method, NULL, 0U, SIZE_MAX, TINYPY_ARITY_STYLE_UNCHECKED);
    tinypy_internal_type_add_items_method(type, vm->internal_special_reduce_key, __tinypy_array_reduce_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(type, vm->internal_remove_key, __tinypy_array_remove_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(type, vm->internal_reverse_key, __tinypy_array_reverse_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(type, vm->internal_tofile_key, __tinypy_array_tofile_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(type, vm->internal_tolist_key, __tinypy_array_tolist_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(type, vm->internal_tostring_key, __tinypy_array_tostring_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(type, vm->internal_tounicode_key, __tinypy_array_tounicode_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(type, vm->internal_write_key, __tinypy_array_tofile_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(type, vm->internal_special_sizeof_key, __tinypy_array_sizeof_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(type, vm->internal_special_length_key, __tinypy_array_length_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER);
    tinypy_internal_type_add_items_method(type, vm->internal_special_getitem_key, __tinypy_array_getitem_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER);
    tinypy_internal_type_add_items_method(type, vm->internal_special_setitem_key, __tinypy_array_setitem_method, NULL, 2U, 2U, TINYPY_ARITY_STYLE_UNPACK);
    tinypy_internal_type_add_items_method(type, vm->internal_special_delitem_key, __tinypy_array_setitem_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER);
    tinypy_internal_type_add_items_method(type, vm->internal_special_contains_key, __tinypy_array_contains_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER);
    tinypy_internal_type_add_items_method(type, vm->internal_special_iter_key, __tinypy_array_iter_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER);
    tinypy_internal_type_add_items_method(type, vm->internal_special_repr_key, __tinypy_array_repr_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER);
    tinypy_internal_type_add_items_method(type, vm->internal_special_add_key, __tinypy_array_concat_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER);
    tinypy_internal_type_add_items_method(type, vm->internal_special_iadd_key, __tinypy_array_concat_method, (void *)(intptr_t)1, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER);
    tinypy_internal_type_add_items_method(type, vm->internal_special_mul_key, __tinypy_array_repeat_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_UNPACK);
    tinypy_internal_type_add_items_method(type, vm->internal_special_rmul_key, __tinypy_array_repeat_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_UNPACK);
    tinypy_internal_type_add_items_method(type, vm->internal_special_imul_key, __tinypy_array_repeat_method, (void *)(intptr_t)1, 1U, 1U, TINYPY_ARITY_STYLE_UNPACK);
    for (size_t index = 0U; index < sizeof(comparison_names) / sizeof(comparison_names[0]); ++index) {
        tinypy_internal_type_add_items_method(type, comparison_names[index], __tinypy_array_compare_method, (void *)(intptr_t)index, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER);
    }
    tinypy_internal_type_add_method(type, vm->internal_special_getslice_key, __tinypy_array_getslice_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_setslice_key, __tinypy_array_setslice_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_delslice_key, __tinypy_array_setslice_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    type->flags |= TINYPY_TYPE_FLAG_IMMUTABLE;
    vm->array_type = type;

    tinypy_value_t *doc = tinypy_string_from_bytes(vm, module_doc, sizeof(module_doc) - 1U);
    tinypy_value_t *const namespace_items[] = {
        vm->internal_special_name_key, vm->internal_array_module_name,
        vm->internal_special_doc_key, doc,
        vm->internal_array_type_key, &type->base.base,
        vm->internal_array_module_name, &type->base.base
    };
    tinypy_value_t *items = tinypy_tuple_from_items(vm, namespace_items, sizeof(namespace_items) / sizeof(namespace_items[0]));

    tinypy_dict_set(vm->builtin_module_items, vm->internal_array_module_name, items);
    TINYPY_DECREF(items);
    TINYPY_DECREF(doc);
}
