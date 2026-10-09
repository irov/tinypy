#include "tinypy/representation.h"

#include "internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct tinypy_representation_builder_t {
    tinypy_vm_t *vm;
    tinypy_value_t *root;
    uint8_t *bytes;
    size_t size;
    size_t capacity;
    tinypy_bool_t failed;
    tinypy_bool_t memory_failed;
    tinypy_bool_t skip_root_special;
    uint8_t inline_bytes[128];
} tinypy_representation_builder_t;

//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_initialize(tinypy_representation_builder_t *builder, tinypy_vm_t *vm) {
    builder->vm = vm;
    builder->root = NULL;
    builder->bytes = builder->inline_bytes;
    builder->size = 0U;
    builder->capacity = sizeof(builder->inline_bytes);
    builder->failed = TINYPY_FALSE;
    builder->memory_failed = TINYPY_FALSE;
    builder->skip_root_special = TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_destroy(tinypy_representation_builder_t *builder) {
    if (builder->bytes != builder->inline_bytes) {
        tinypy_internal_vm_deallocate(builder->vm, builder->bytes, builder->capacity);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_reserve(tinypy_representation_builder_t *builder, size_t additional) {
    size_t required;
    size_t capacity;

    if (builder->failed != 0 || additional > SIZE_MAX - builder->size) {
        builder->failed = TINYPY_TRUE;
        return;
    }
    required = builder->size + additional;
    if (required <= builder->capacity) {
        return;
    }
    capacity = builder->capacity == 0U ? 64U : builder->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }
    uint8_t *resized;

    if (builder->bytes == builder->inline_bytes) {
        resized = (uint8_t *)tinypy_internal_pool_allocate_checked(builder->vm, capacity);
        if (resized != NULL) {
            (void)memcpy(resized, builder->inline_bytes, builder->size);
        }
    }
    else {
        resized = (uint8_t *)tinypy_internal_pool_reallocate_checked(builder->vm, builder->bytes, builder->capacity, capacity);
    }
    if (resized == NULL) {
        builder->failed = TINYPY_TRUE;
        builder->memory_failed = TINYPY_TRUE;
        return;
    }
    builder->bytes = resized;
    builder->capacity = capacity;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_append(tinypy_representation_builder_t *builder, const void *bytes, size_t size) {
    if (size == 0U) {
        return;
    }
    __tinypy_representation_reserve(builder, size);
    if (builder->failed != 0) {
        return;
    }
    (void)memcpy(builder->bytes + builder->size, bytes, size);
    builder->size += size;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_append_character(tinypy_representation_builder_t *builder, uint8_t character) {
    __tinypy_representation_append(builder, &character, 1U);
}
//////////////////////////////////////////////////////////////////////////
/* Py_ReprEnter: the containers being represented are tracked across nested
   repr calls, so one met again inside its own representation is shown
   abbreviated even when a user __repr__ asked for it. */
static tinypy_bool_t __tinypy_representation_enter(tinypy_representation_builder_t *builder, tinypy_representation_frame_t *frame, tinypy_value_t *value) {
    tinypy_vm_t *vm = builder->vm;

    for (const tinypy_representation_frame_t *active = vm->representation_frames; active != NULL; active = active->previous) {
        if (active->value == value) {
            return TINYPY_FALSE;
        }
    }
    frame->value = value;
    frame->previous = vm->representation_frames;
    vm->representation_frames = frame;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_leave(tinypy_representation_builder_t *builder, const tinypy_representation_frame_t *frame) {
    builder->vm->representation_frames = frame->previous;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_unsigned_decimal(tinypy_representation_builder_t *builder, uint64_t value, size_t minimum_digits) {
    uint8_t digits[32];
    size_t offset = sizeof(digits);

    do {
        offset -= 1U;
        digits[offset] = (uint8_t)('0' + value % UINT64_C(10));
        value /= UINT64_C(10);
    } while (value != UINT64_C(0));
    while (sizeof(digits) - offset < minimum_digits) {
        offset -= 1U;
        digits[offset] = (uint8_t)'0';
    }
    __tinypy_representation_append(builder, digits + offset, sizeof(digits) - offset);
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
static void __tinypy_representation_long(tinypy_representation_builder_t *builder, const tinypy_value_t *value, tinypy_bool_t raw) {
    size_t digit_count = TINYPY_LONG_DIGIT_COUNT(value);
    size_t word_count;
    size_t chunk_capacity;
    uint32_t *scratch;
    uint32_t *work;
    uint32_t *chunks;
    size_t scratch_size;
    size_t chunk_count = 0U;
    size_t active_digits;
    size_t index;

    if (TINYPY_LONG_SIGN(value) < 0) {
        __tinypy_representation_append_character(builder, (uint8_t)'-');
    }
    if (digit_count == 0U) {
        __tinypy_representation_append(builder, raw != 0 ? "0" : "0L", raw != 0 ? 1U : 2U);
        return;
    }
    if (digit_count == SIZE_MAX || digit_count / 29U > SIZE_MAX / 15U) {
        builder->failed = TINYPY_TRUE;
        return;
    }
    word_count = (digit_count + 1U) / 2U;
    chunk_capacity = (digit_count / 29U) * 15U + ((digit_count % 29U) * 15U + 28U) / 29U;
    if (word_count > SIZE_MAX - chunk_capacity || word_count + chunk_capacity > SIZE_MAX / sizeof(*scratch)) {
        builder->failed = TINYPY_TRUE;
        return;
    }
    scratch_size = (word_count + chunk_capacity) * sizeof(*scratch);
    scratch = (uint32_t *)tinypy_internal_pool_allocate_checked(builder->vm, scratch_size);
    if (scratch == NULL) {
        builder->failed = TINYPY_TRUE;
        builder->memory_failed = TINYPY_TRUE;
        return;
    }
    work = scratch;
    chunks = scratch + word_count;
    for (index = 0U; index < word_count; ++index) {
        size_t digit_index = index * 2U;

        work[index] = (uint32_t)TINYPY_LONG_OBJECT(value)->digits[digit_index];
        if (digit_index + 1U < digit_count) {
            work[index] |= (uint32_t)TINYPY_LONG_OBJECT(value)->digits[digit_index + 1U] << 15U;
        }
    }
    active_digits = word_count;
    while (active_digits != 0U) {
        uint64_t remainder = UINT64_C(0);
        size_t division_index = active_digits;

        while (division_index != 0U) {
            uint64_t current;

            division_index -= 1U;
            current = (remainder << 30U) | (uint64_t)work[division_index];
            work[division_index] = (uint32_t)(current / UINT64_C(1000000000));
            remainder = current % UINT64_C(1000000000);
        }
        chunks[chunk_count] = (uint32_t)remainder;
        chunk_count += 1U;
        while (active_digits != 0U && work[active_digits - 1U] == 0U) {
            active_digits -= 1U;
        }
    }
    uint32_t leading = chunks[chunk_count - 1U];
    size_t output_digits = 1U + (chunk_count - 1U) * 9U;

    while (leading >= 10U) {
        leading /= 10U;
        output_digits += 1U;
    }
    __tinypy_representation_reserve(builder, output_digits + (raw == 0 ? 1U : 0U));
    __tinypy_representation_unsigned_decimal(builder, chunks[chunk_count - 1U], 1U);
    while (chunk_count > 1U) {
        chunk_count -= 1U;
        __tinypy_representation_unsigned_decimal(builder, chunks[chunk_count - 1U], 9U);
    }
    if (raw == 0) {
        __tinypy_representation_append_character(builder, (uint8_t)'L');
    }
    tinypy_internal_vm_deallocate(builder->vm, scratch, scratch_size);
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_representation_normalize_decimal(char *buffer, size_t size) {
    size_t position = 0U;
    size_t decimal_end;

    if (position < size && (buffer[position] == '+' || buffer[position] == '-')) {
        position += 1U;
    }
    while (position < size && buffer[position] >= '0' && buffer[position] <= '9') {
        position += 1U;
    }
    if (position == size || buffer[position] == 'e' || buffer[position] == 'E') {
        return size;
    }
    decimal_end = position;
    while (decimal_end < size && (buffer[decimal_end] < '0' || buffer[decimal_end] > '9')) {
        if (buffer[decimal_end] == 'e' || buffer[decimal_end] == 'E') {
            return size;
        }
        decimal_end += 1U;
    }
    if (decimal_end == size) {
        return size;
    }
    if (decimal_end > position + 1U) {
        (void)memmove(buffer + position + 1U, buffer + decimal_end, size - decimal_end);
        size -= decimal_end - position - 1U;
    }
    buffer[position] = '.';
    return size;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_representation_double_candidate(double value, tinypy_bool_t raw, char *buffer, size_t capacity) {
    int32_t written;
    size_t size;

    if (raw != 0) {
        size = tinypy_internal_double_format_text(value, (uint8_t)'g', TINYPY_FALSE, 12U, (uint8_t *)buffer);
        if (size != 0U) {
            return size;
        }
        written = snprintf(buffer, capacity, "%.12g", value);
        if (written < 0 || (size_t)written >= capacity) {
            return 0U;
        }
        size = (size_t)written;
    }
    else {
        written = tinypy_internal_d2s_buffered_n(value, buffer);
        if (written <= 0 || (size_t)written >= capacity) {
            return 0U;
        }
        size = (size_t)written;
    }
    size_t return_value = __tinypy_representation_normalize_decimal(buffer, size);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_double(tinypy_representation_builder_t *builder, double value, tinypy_bool_t raw, tinypy_bool_t add_dot_zero) {
    char candidate[64];
    uint8_t digits[24];
    size_t candidate_size;
    size_t digit_count = 0U;
    size_t first_significant = SIZE_MAX;
    size_t decimal_position = SIZE_MAX;
    size_t exponent_position = SIZE_MAX;
    int32_t exponent = 0;
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
        value = -value;
    }
    if (value == 0.0) {
        __tinypy_representation_append(builder, add_dot_zero != 0 ? "0.0" : "0", add_dot_zero != 0 ? 3U : 1U);
        return;
    }
    candidate_size = __tinypy_representation_double_candidate(value, raw, candidate, sizeof(candidate));
    if (candidate_size == 0U) {
        __tinypy_representation_append(builder, add_dot_zero != 0 ? "0.0" : "0", add_dot_zero != 0 ? 3U : 1U);
        return;
    }
    for (index = 0U; index < candidate_size; ++index) {
        uint8_t character = (uint8_t)candidate[index];

        if (character >= (uint8_t)'0' && character <= (uint8_t)'9') {
            if (first_significant == SIZE_MAX && character != (uint8_t)'0') {
                first_significant = digit_count;
            }
            digits[digit_count++] = (uint8_t)(character - (uint8_t)'0');
        }
        else if (character == (uint8_t)'.') {
            decimal_position = digit_count;
        }
        else if (character == (uint8_t)'e' || character == (uint8_t)'E') {
            exponent_position = index;
            break;
        }
    }
    if (digit_count == 0U) {
        __tinypy_representation_append(builder, add_dot_zero != 0 ? "0.0" : "0", add_dot_zero != 0 ? 3U : 1U);
        return;
    }
    if (exponent_position != SIZE_MAX) {
        int32_t sign = 1;
        size_t cursor = exponent_position + 1U;

        if (cursor < candidate_size && (candidate[cursor] == '+' || candidate[cursor] == '-')) {
            sign = candidate[cursor++] == '-' ? -1 : 1;
        }
        while (cursor < candidate_size) {
            exponent = exponent * 10 + (int32_t)(candidate[cursor++] - '0');
        }
        exponent *= sign;
    }
    else if (decimal_position == SIZE_MAX) {
        exponent = (int32_t)digit_count - 1;
    }
    else if (first_significant < decimal_position) {
        exponent = (int32_t)(decimal_position - first_significant - 1U);
    }
    else {
        exponent = -(int32_t)(first_significant - decimal_position + 1U);
    }
    if (first_significant != 0U && first_significant != SIZE_MAX) {
        digit_count -= first_significant;
        (void)memmove(digits, digits + first_significant, digit_count * sizeof(*digits));
    }
    /* %.12g with ADD_DOT_0 moves to the exponent form once the integer
       part would need all twelve digits; repr keeps seventeen. */
    scientific = exponent < -4 || exponent >= (raw != 0 ? 11 : 16);
    if (scientific != 0) {
        while (digit_count > 1U && digits[digit_count - 1U] == 0U) {
            digit_count -= 1U;
        }
        __tinypy_representation_append_character(builder, (uint8_t)('0' + (char)digits[0]));
        if (digit_count > 1U) {
            __tinypy_representation_append_character(builder, (uint8_t)'.');
            for (index = 1U; index < digit_count; ++index) {
                __tinypy_representation_append_character(builder, (uint8_t)('0' + (char)digits[index]));
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
            __tinypy_representation_append_character(builder, (uint8_t)('0' + (char)digits[index]));
        }
        return;
    }
    for (index = 0U; index <= (size_t)exponent; ++index) {
        uint8_t digit = index < digit_count ? digits[index] : 0U;

        __tinypy_representation_append_character(builder, (uint8_t)('0' + digit));
    }
    if ((size_t)exponent + 1U < digit_count) {
        __tinypy_representation_append_character(builder, (uint8_t)'.');
        for (index = (size_t)exponent + 1U; index < digit_count; ++index) {
            __tinypy_representation_append_character(builder, (uint8_t)('0' + (char)digits[index]));
        }
    }
    else {
        if (add_dot_zero != 0) {
            __tinypy_representation_append(builder, ".0", 2U);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_hexadecimal_escape(tinypy_representation_builder_t *builder, uint8_t marker, uint32_t value, size_t digits) {
    static const uint8_t hexadecimal[] = "0123456789abcdef";
    uint8_t escaped[10];
    size_t index;

    escaped[0] = (uint8_t)'\\';
    escaped[1] = marker;
    for (index = 0U; index < digits; ++index) {
        size_t shift = (digits - index - 1U) * 4U;

        escaped[index + 2U] = hexadecimal[(value >> shift) & 0x0fU];
    }
    __tinypy_representation_append(builder, escaped, digits + 2U);
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_representation_utf8_code_point(const uint8_t *bytes, size_t size, uint32_t *out_code_point) {
    uint8_t first = bytes[0];

    if (first < 0x80U) {
        *out_code_point = first;
        return 1U;
    }
    if (first < 0xe0U && size >= 2U) {
        *out_code_point = ((uint32_t)(first & 0x1fU) << 6U) | (uint32_t)(bytes[1] & 0x3fU);
        return 2U;
    }
    if (first < 0xf0U && size >= 3U) {
        *out_code_point = ((uint32_t)(first & 0x0fU) << 12U) |
            ((uint32_t)(bytes[1] & 0x3fU) << 6U) |
            (uint32_t)(bytes[2] & 0x3fU);
        return 3U;
    }
    if (size >= 4U) {
        *out_code_point = ((uint32_t)(first & 0x07U) << 18U) |
            ((uint32_t)(bytes[1] & 0x3fU) << 12U) |
            ((uint32_t)(bytes[2] & 0x3fU) << 6U) |
            (uint32_t)(bytes[3] & 0x3fU);
        return 4U;
    }
    *out_code_point = first;
    return 1U;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_quoted(tinypy_representation_builder_t *builder, const tinypy_value_t *value) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(value);
    size_t size = TINYPY_TEXT_BYTE_SIZE(value);
    size_t index;
    tinypy_bool_t unicode = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE;
    tinypy_bool_t has_single_quote = TINYPY_FALSE;
    tinypy_bool_t has_double_quote = TINYPY_FALSE;
    uint8_t quote;

    for (index = 0U; index < size; ++index) {
        has_single_quote = has_single_quote != 0 || bytes[index] == (uint8_t)'\'';
        has_double_quote = has_double_quote != 0 || bytes[index] == (uint8_t)'"';
    }
    quote = has_single_quote != 0 && has_double_quote == 0 ? (uint8_t)'"' : (uint8_t)'\'';

    if (unicode != 0) {
        __tinypy_representation_append_character(builder, (uint8_t)'u');
    }
    __tinypy_representation_append_character(builder, quote);
    for (index = 0U; index < size; ++index) {
        uint8_t byte = bytes[index];

        if (byte == '\\' || byte == quote) {
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
        else if (unicode != 0 && byte >= 0x80U) {
            uint32_t code_point;
            size_t width = __tinypy_representation_utf8_code_point(bytes + index, size - index, &code_point);

            index += width - 1U;
            if (code_point <= UINT32_C(0xff)) {
                __tinypy_representation_hexadecimal_escape(builder, (uint8_t)'x', code_point, 2U);
            }
            else if (code_point <= UINT32_C(0xffff)) {
                __tinypy_representation_hexadecimal_escape(builder, (uint8_t)'u', code_point, 4U);
            }
            else {
                __tinypy_representation_hexadecimal_escape(builder, (uint8_t)'U', code_point, 8U);
            }
        }
        else {
            __tinypy_representation_hexadecimal_escape(builder, (uint8_t)'x', byte, 2U);
        }
    }
    __tinypy_representation_append_character(builder, quote);
}

static tinypy_bool_t __tinypy_representation_value(tinypy_representation_builder_t *builder, tinypy_value_t *value, tinypy_bool_t raw, tinypy_error_t **out_error);

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_representation_module_attribute(tinypy_value_t *module, tinypy_value_t *name) {
    tinypy_value_t *dict = tinypy_module_dict(module);
    tinypy_dict_entry_t *iterator;
    tinypy_dict_entry_t *end;

    if (dict == NULL) {
        return NULL;
    }
    iterator = TINYPY_DICT_ITERATOR_BEGIN(dict);
    end = TINYPY_DICT_ITERATOR_END(dict);

    for (; iterator != end; ++iterator) {
        tinypy_value_t *key;

        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator) == 0) {
            continue;
        }
        key = iterator->key;
        if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_STRING && TINYPY_NAME_EQ(key, name) != 0) {
            return iterator->value;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* list_repr and tuplerepr read the size again after every item, since a
   representation may change the list. */
static tinypy_bool_t __tinypy_representation_sequence(tinypy_representation_builder_t *builder, tinypy_value_t *value, uint8_t open, uint8_t close, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_representation_frame_t frame;

    if (__tinypy_representation_enter(builder, &frame, value) == 0) {
        __tinypy_representation_append(builder, kind == TINYPY_VALUE_TUPLE ? "(...)" : "[...]", 5U);
        return TINYPY_TRUE;
    }
    __tinypy_representation_append_character(builder, open);
    for (size_t index = 0U; index < TINYPY_SIZED_SIZE(value); ++index) {
        tinypy_value_t *item = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(value, index) : TINYPY_LIST_GET(value, index);

        TINYPY_INCREF(item);
        if (index != 0U) {
            __tinypy_representation_append(builder, ", ", 2U);
        }
        tinypy_bool_t represented = __tinypy_representation_value(builder, item, INT32_C(0), out_error);
        TINYPY_DECREF(item);
        if (represented == 0) {
            __tinypy_representation_leave(builder, &frame);
            return TINYPY_FALSE;
        }
    }
    if (kind == TINYPY_VALUE_TUPLE && TINYPY_SIZED_SIZE(value) == 1U) {
        __tinypy_representation_append_character(builder, (uint8_t)',');
    }
    __tinypy_representation_append_character(builder, close);
    __tinypy_representation_leave(builder, &frame);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* dict_repr walks the table by position and holds each pair while it is
   represented. */
static tinypy_bool_t __tinypy_representation_dict(tinypy_representation_builder_t *builder, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_representation_frame_t frame;
    size_t position = 0U;
    tinypy_value_t *key;
    tinypy_value_t *item;
    size_t emitted = 0U;

    if (__tinypy_representation_enter(builder, &frame, value) == 0) {
        __tinypy_representation_append(builder, "{...}", 5U);
        return TINYPY_TRUE;
    }
    __tinypy_representation_append_character(builder, (uint8_t)'{');
    while (tinypy_dict_next(value, &position, &key, &item) != 0) {
        TINYPY_INCREF(key);
        TINYPY_INCREF(item);
        if (emitted != 0U) {
            __tinypy_representation_append(builder, ", ", 2U);
        }
        tinypy_bool_t represented = __tinypy_representation_value(builder, key, INT32_C(0), out_error);
        if (represented != 0) {
            __tinypy_representation_append(builder, ": ", 2U);
            represented = __tinypy_representation_value(builder, item, INT32_C(0), out_error);
        }
        TINYPY_DECREF(item);
        TINYPY_DECREF(key);
        if (represented == 0) {
            __tinypy_representation_leave(builder, &frame);
            return TINYPY_FALSE;
        }
        emitted += 1U;
    }
    __tinypy_representation_append_character(builder, (uint8_t)'}');
    __tinypy_representation_leave(builder, &frame);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* set_repr and dictview_repr represent a list of the elements, taken by
   iteration first, in the name of the type; a dictionary view met again
   inside its own representation is shown as "...". */
static tinypy_bool_t __tinypy_representation_elements(tinypy_representation_builder_t *builder, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_representation_frame_t frame;

    if (__tinypy_representation_enter(builder, &frame, value) == 0) {
        if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
            __tinypy_representation_append(builder, value->type->name, value->type->name_size);
            __tinypy_representation_append(builder, "(...)", 5U);
        }
        else {
            __tinypy_representation_append(builder, "...", 3U);
        }
        return TINYPY_TRUE;
    }
    tinypy_value_t *keys = tinypy_internal_list_from_items_checked(vm, NULL, 0U, out_error);
    if (keys == NULL) {
        __tinypy_representation_leave(builder, &frame);
        return TINYPY_FALSE;
    }
    tinypy_bool_t represented = tinypy_internal_list_extend_iterable(keys, value, "error return without exception set", out_error);
    if (represented != 0) {
        __tinypy_representation_append(builder, value->type->name, value->type->name_size);
        __tinypy_representation_append_character(builder, (uint8_t)'(');
        represented = __tinypy_representation_sequence(builder, keys, (uint8_t)'[', (uint8_t)']', out_error);
        __tinypy_representation_append_character(builder, (uint8_t)')');
    }
    TINYPY_DECREF(keys);
    __tinypy_representation_leave(builder, &frame);
    return represented;
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
/* reported names the method in the non-string error: object.__str__ calls
   __repr__ but PyObject_Str reports the result as __str__'s. */
static tinypy_value_t *__tinypy_representation_custom_reported(tinypy_value_t *value, tinypy_value_t *name, tinypy_value_t *reported, tinypy_error_t **out_error) {
    tinypy_value_t *args;
    tinypy_value_t *result;

    tinypy_value_t *method;
    if (tinypy_internal_object_lookup_special_key(value, name, &method, out_error) <= 0) {
        return NULL;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    args = TINYPY_RET_EMPTY_TUPLE(vm);
    result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    if (result == NULL) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_UNICODE) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_TEXT(reported),
            TINYPY_MESSAGE_PART_LITERAL(" returned non-string (type "),
            TINYPY_MESSAGE_PART_TYPE_NAME(result),
            TINYPY_MESSAGE_PART_LITERAL(")")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_representation_custom(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_representation_custom_reported(value, name, name, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_representation_append_text(tinypy_representation_builder_t *builder, tinypy_value_t *text, tinypy_bool_t raw, tinypy_error_t **out_error) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(text);
    size_t byte_size = TINYPY_TEXT_BYTE_SIZE(text);
    size_t index;

    (void)raw;
    if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) {
        for (index = 0U; index < byte_size; ++index) {
            if (bytes[index] >= 0x80U) {
                /* PyObject_Repr encodes a unicode result with the default
                   encoding, whose error carries the usual arguments. */
                tinypy_value_t *encoded = tinypy_internal_text_codec(builder->vm, text, NULL, NULL, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);

                if (encoded == NULL) {
                    return TINYPY_FALSE;
                }
                __tinypy_representation_append(builder, TINYPY_TEXT_BYTES(encoded), TINYPY_TEXT_BYTE_SIZE(encoded));
                TINYPY_DECREF(encoded);
                return TINYPY_TRUE;
            }
        }
    }
    __tinypy_representation_append(builder, bytes, byte_size);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_representation_dict_module(tinypy_vm_t *vm, tinypy_value_t *dict) {
    tinypy_value_t *key = vm->internal_special_module_key;
    tinypy_value_t *module = tinypy_internal_dict_get_optional(vm, dict, key);

    if (module == NULL || TINYPY_VALUE_KIND(module) != TINYPY_VALUE_STRING) {
        return NULL;
    }
    return module;
}
//////////////////////////////////////////////////////////////////////////
/* Old-style classes and their instances always show the defining module,
   falling back to "?" the way Python 2.7 does when it is missing. */
static void __tinypy_representation_class_qualified_name(tinypy_representation_builder_t *builder, tinypy_value_t *class_value) {
    tinypy_value_t *module = __tinypy_representation_dict_module(builder->vm, tinypy_class_dict(class_value));
    tinypy_value_t *name = tinypy_class_name(class_value);

    if (module != NULL) {
        __tinypy_representation_append(builder, TINYPY_TEXT_BYTES(module), TINYPY_TEXT_BYTE_SIZE(module));
    }
    else {
        __tinypy_representation_append_character(builder, (uint8_t)'?');
    }
    __tinypy_representation_append_character(builder, (uint8_t)'.');
    __tinypy_representation_append(builder, TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name));
}
//////////////////////////////////////////////////////////////////////////
/* Instances of types defined in Python show the defining module, the way
   object.__repr__ does in Python 2.7; built-in types stay unqualified. */
static void __tinypy_representation_type_qualified_name(tinypy_representation_builder_t *builder, const tinypy_type_t *type) {
    tinypy_value_t *module = tinypy_type_get_attr_key(type, type->vm->internal_special_module_key);

    if (module != NULL && TINYPY_VALUE_KIND(module) == TINYPY_VALUE_STRING && TINYPY_NAME_EQ(module, type->vm->internal_builtin_module_name) == 0 && ((type->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) != 0U || memchr(type->name, '.', type->name_size) == NULL)) {
        __tinypy_representation_append(builder, TINYPY_TEXT_BYTES(module), TINYPY_TEXT_BYTE_SIZE(module));
        __tinypy_representation_append_character(builder, (uint8_t)'.');
    }
    __tinypy_representation_append(builder, type->name, type->name_size);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_representation_qualifier(tinypy_representation_builder_t *builder, tinypy_value_t *value) {
    const uint8_t *bytes = (const uint8_t *)"?";
    size_t size = 1U;
    tinypy_value_t *name = NULL;

    if (value == NULL) {
        __tinypy_representation_append(builder, bytes, size);
        return;
    }
    switch (TINYPY_VALUE_KIND(value)) {
    case TINYPY_VALUE_TYPE:
        bytes = (const uint8_t *)((tinypy_type_t *)value)->name;
        size = ((tinypy_type_t *)value)->name_size;
        break;
    case TINYPY_VALUE_CLASS:
        name = tinypy_class_name(value);
        break;
    case TINYPY_VALUE_FUNCTION:
        name = tinypy_function_name(value);
        break;
    case TINYPY_VALUE_NATIVE_FUNCTION:
        name = tinypy_native_function_name(value);
        break;
    default:
        break;
    }
    if (name != NULL && (TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(name) == TINYPY_VALUE_UNICODE)) {
        bytes = TINYPY_TEXT_BYTES(name);
        size = TINYPY_TEXT_BYTE_SIZE(name);
    }
    __tinypy_representation_append(builder, bytes, size);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_representation_value_impl(tinypy_representation_builder_t *builder, tinypy_value_t *value, tinypy_bool_t raw, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
/* PyObject_Repr and _PyObject_Str guard against runaway recursion. */
static tinypy_bool_t __tinypy_representation_value(tinypy_representation_builder_t *builder, tinypy_value_t *value, tinypy_bool_t raw, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    const char *message = raw != 0 ? "maximum recursion depth exceeded while getting the str of an object" : "maximum recursion depth exceeded while getting the repr of an object";
    tinypy_bool_t result;

    if (tinypy_internal_recursion_check(vm, TINYPY_NATIVE_STACK_ADDRESS(), message, out_error) == 0) {
        return TINYPY_FALSE;
    }
    vm->evaluation_depth += 1U;
    result = __tinypy_representation_value_impl(builder, value, raw, out_error);
    vm->evaluation_depth -= 1U;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_representation_value_impl(tinypy_representation_builder_t *builder, tinypy_value_t *value, tinypy_bool_t raw, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_bool_t function_result;
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_value_t *special_name = raw != 0 ? vm->internal_special_str_key : vm->internal_special_repr_key;
    uint64_t special = raw != 0 ? TINYPY_INTERNAL_DISPATCH_BIT(STR) : TINYPY_INTERNAL_DISPATCH_BIT(REPR);
    tinypy_unary_slot_t representation_slot = raw != 0 ? value->type->string : value->type->repr;

    if ((builder->skip_root_special == 0 || value != builder->root) && (value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && __tinypy_internal_object_overrides_dispatch(value, special) != 0) {
        tinypy_value_t *custom = __tinypy_representation_custom(value, special_name, out_error);

        if (custom == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t appended = __tinypy_representation_append_text(builder, custom, raw, out_error);
        TINYPY_DECREF(custom);
        return appended;
    }
    if (raw != 0 && (builder->skip_root_special == 0 || value != builder->root) && (value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U &&
        (kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_DICT || kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) &&
        __tinypy_internal_object_overrides_dispatch(value, TINYPY_INTERNAL_DISPATCH_BIT(REPR)) != 0) {
        tinypy_value_t *custom = __tinypy_representation_custom(value, vm->internal_special_repr_key, out_error);

        if (custom == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t appended = __tinypy_representation_append_text(builder, custom, raw, out_error);
        TINYPY_DECREF(custom);
        return appended;
    }
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
        tinypy_bool_t appended = __tinypy_representation_append_text(builder, representation, raw, out_error);
        TINYPY_DECREF(representation);
        return appended;
    }

    if (raw != 0 && (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE)) {
        tinypy_bool_t return_value = __tinypy_representation_append_text(builder, value, raw, out_error);
        return return_value;
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
        __tinypy_representation_long(builder, value, raw);
        return TINYPY_TRUE;
    case TINYPY_VALUE_FLOAT:
        __tinypy_representation_double(builder, TINYPY_FLOAT_OBJECT(value)->value, raw, TINYPY_TRUE);
        return TINYPY_TRUE;
    case TINYPY_VALUE_COMPLEX: {
        double real = TINYPY_COMPLEX_OBJECT(value)->real;
        double imaginary = TINYPY_COMPLEX_OBJECT(value)->imaginary;

        if (real == 0.0 && signbit(real) == 0) {
            __tinypy_representation_double(builder, imaginary, raw, TINYPY_FALSE);
            __tinypy_representation_append_character(builder, (uint8_t)'j');
            return TINYPY_TRUE;
        }
        __tinypy_representation_append_character(builder, (uint8_t)'(');
        __tinypy_representation_double(builder, real, raw, TINYPY_FALSE);
        if (isnan(imaginary) != 0 || signbit(imaginary) == 0) {
            __tinypy_representation_append_character(builder, (uint8_t)'+');
        }
        __tinypy_representation_double(builder, imaginary, raw, TINYPY_FALSE);
        __tinypy_representation_append(builder, "j)", 2U);
        return TINYPY_TRUE;
    }
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
    case TINYPY_VALUE_DICT_KEYS:
    case TINYPY_VALUE_DICT_VALUES:
    case TINYPY_VALUE_DICT_ITEMS:
        function_result = __tinypy_representation_elements(builder, value, out_error);
        return function_result;
    case TINYPY_VALUE_TYPE: {
        tinypy_type_t *type_value = (tinypy_type_t *)value;
        tinypy_bool_t heap_type = (type_value->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) != 0U ? TINYPY_TRUE : TINYPY_FALSE;
        tinypy_value_t *module = tinypy_internal_dict_get_optional_suppressed(builder->vm, type_value->dict, builder->vm->internal_special_module_key);

        /* Python classes use their own module; native types retain their
           qualified C name, including module prefixes. */
        __tinypy_representation_append(builder, heap_type != 0 ? "<class '" : "<type '", heap_type != 0 ? 8U : 7U);
        if (module != NULL && TINYPY_VALUE_KIND(module) == TINYPY_VALUE_STRING && TINYPY_NAME_EQ(module, type_value->vm->internal_builtin_module_name) == 0 && (heap_type != TINYPY_FALSE || memchr(type_value->name, '.', type_value->name_size) == NULL)) {
            __tinypy_representation_append(builder, TINYPY_TEXT_BYTES(module), TINYPY_TEXT_BYTE_SIZE(module));
            __tinypy_representation_append_character(builder, (uint8_t)'.');
        }
        __tinypy_representation_append(builder, type_value->name, type_value->name_size);
        __tinypy_representation_append(builder, "'>", 2U);
        return TINYPY_TRUE;
    }
    case TINYPY_VALUE_CLASS:
        if (raw != 0 && __tinypy_representation_dict_module(builder->vm, tinypy_class_dict(value)) == NULL) {
            tinypy_value_t *class_name = tinypy_class_name(value);

            __tinypy_representation_append(builder, TINYPY_TEXT_BYTES(class_name), TINYPY_TEXT_BYTE_SIZE(class_name));
            return TINYPY_TRUE;
        }
        if (raw != 0) {
            __tinypy_representation_class_qualified_name(builder, value);
            return TINYPY_TRUE;
        }
        __tinypy_representation_append(builder, "<class ", 7U);
        __tinypy_representation_class_qualified_name(builder, value);
        __tinypy_representation_append(builder, " at ", 4U);
        __tinypy_representation_pointer(builder, value);
        __tinypy_representation_append_character(builder, (uint8_t)'>');
        return TINYPY_TRUE;
    case TINYPY_VALUE_METHOD: {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(value);
        tinypy_bool_t bound = method->self != NULL ? TINYPY_TRUE : TINYPY_FALSE;

        __tinypy_representation_append(builder, bound != 0 ? "<bound method " : "<unbound method ", bound != 0 ? 14U : 16U);
        __tinypy_representation_qualifier(builder, method->owner);
        __tinypy_representation_append_character(builder, (uint8_t)'.');
        __tinypy_representation_qualifier(builder, method->function);
        if (bound == 0) {
            __tinypy_representation_append_character(builder, (uint8_t)'>');
            return TINYPY_TRUE;
        }
        __tinypy_representation_append(builder, " of ", 4U);
        if (__tinypy_representation_value(builder, method->self, INT32_C(0), out_error) == 0) {
            return TINYPY_FALSE;
        }
        __tinypy_representation_append_character(builder, (uint8_t)'>');
        return TINYPY_TRUE;
    }
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
    case TINYPY_VALUE_CODE: {
        tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);

        __tinypy_representation_append(builder, "<code object ", 13U);
        __tinypy_representation_append(builder, TINYPY_TEXT_BYTES(code->name), TINYPY_TEXT_BYTE_SIZE(code->name));
        __tinypy_representation_append(builder, " at ", 4U);
        __tinypy_representation_pointer(builder, value);
        __tinypy_representation_append(builder, ", file \"", 8U);
        __tinypy_representation_append(builder, TINYPY_TEXT_BYTES(code->filename), TINYPY_TEXT_BYTE_SIZE(code->filename));
        __tinypy_representation_append(builder, "\", line ", 8U);
        __tinypy_representation_integer(builder, code->first_line_number);
        __tinypy_representation_append_character(builder, (uint8_t)'>');
        return TINYPY_TRUE;
    }
    case TINYPY_VALUE_GENERATOR: {
        tinypy_value_t *code_name = TINYPY_CODE_OBJECT(TINYPY_GENERATOR_OBJECT(value)->code)->name;

        __tinypy_representation_append(builder, "<generator object ", 18U);
        __tinypy_representation_append(builder, TINYPY_TEXT_BYTES(code_name), TINYPY_TEXT_BYTE_SIZE(code_name));
        __tinypy_representation_append(builder, " at ", 4U);
        __tinypy_representation_pointer(builder, value);
        __tinypy_representation_append_character(builder, (uint8_t)'>');
        return TINYPY_TRUE;
    }
    case TINYPY_VALUE_CELL: {
        tinypy_cell_object_t *cell = TINYPY_CELL_OBJECT(value);

        __tinypy_representation_append(builder, "<cell at ", 9U);
        __tinypy_representation_pointer(builder, value);
        if (cell->content == NULL) {
            __tinypy_representation_append(builder, ": empty>", 8U);
            return TINYPY_TRUE;
        }
        __tinypy_representation_append(builder, ": ", 2U);
        __tinypy_representation_append(builder, cell->content->type->name, cell->content->type->name_size);
        __tinypy_representation_append(builder, " object at ", 11U);
        __tinypy_representation_pointer(builder, cell->content);
        __tinypy_representation_append_character(builder, (uint8_t)'>');
        return TINYPY_TRUE;
    }
    case TINYPY_VALUE_SUPER: {
        tinypy_super_object_t *super_value = TINYPY_SUPER_OBJECT(value);

        __tinypy_representation_append(builder, "<super: <class '", 16U);
        if (super_value->type == NULL) {
            __tinypy_representation_append(builder, "NULL", 4U);
        }
        else {
            __tinypy_representation_append(builder, super_value->type->name, super_value->type->name_size);
        }
        if (super_value->object == NULL) {
            __tinypy_representation_append(builder, "'>, NULL>", 9U);
            return TINYPY_TRUE;
        }
        __tinypy_representation_append(builder, "'>, <", 5U);
        __tinypy_representation_append(builder, super_value->object_type->name, super_value->object_type->name_size);
        __tinypy_representation_append(builder, " object>>", 9U);
        return TINYPY_TRUE;
    }
    case TINYPY_VALUE_MODULE: {
        __tinypy_representation_append(builder, "<module '", 9U);
        tinypy_value_t *module_name = __tinypy_representation_module_attribute(value, vm->internal_special_name_key);
        tinypy_value_t *module_file = __tinypy_representation_module_attribute(value, vm->internal_special_file_key);

        if (module_name == NULL || TINYPY_VALUE_KIND(module_name) != TINYPY_VALUE_STRING) {
            __tinypy_representation_append_character(builder, (uint8_t)'?');
        }
        else {
            const uint8_t *bytes_3 = TINYPY_STRING_OBJECT(module_name)->bytes;
            size_t byte_size_3 = TINYPY_STRING_SIZE(module_name);

            __tinypy_representation_append(builder, bytes_3, byte_size_3 < 80U ? byte_size_3 : 80U);
        }
        if (module_file != NULL && TINYPY_VALUE_KIND(module_file) == TINYPY_VALUE_STRING) {
            size_t file_size = TINYPY_STRING_SIZE(module_file);

            __tinypy_representation_append(builder, "' from '", 8U);
            __tinypy_representation_append(builder, TINYPY_STRING_OBJECT(module_file)->bytes, file_size < 300U ? file_size : 300U);
            __tinypy_representation_append(builder, "'>", 2U);
        }
        else {
            __tinypy_representation_append(builder, "' (built-in)>", 13U);
        }
        return TINYPY_TRUE;
    }
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
        int64_t stop = tinypy_internal_xrange_stop_value(range);

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
        tinypy_value_t *custom = builder->skip_root_special != 0 && value == builder->root
                                     ? NULL
                                     : __tinypy_representation_custom(value, special_name, out_error);

        if (custom != NULL) {
            tinypy_bool_t appended = __tinypy_representation_append_text(builder, custom, raw, out_error);
            TINYPY_DECREF(custom);
            return appended;
        }
        if (out_error != NULL && *out_error != NULL) {
            return TINYPY_FALSE;
        }
        /* str() of a classic instance without __str__ falls back to __repr__,
           the way instance_str does in Python 2.7. */
        if (raw != 0 && kind == TINYPY_VALUE_OLD_INSTANCE) {
            tinypy_value_t *fallback = __tinypy_representation_custom(value, vm->internal_special_repr_key, out_error);

            if (fallback != NULL) {
                tinypy_bool_t appended = __tinypy_representation_append_text(builder, fallback, raw, out_error);
                TINYPY_DECREF(fallback);
                return appended;
            }
            if (out_error != NULL && *out_error != NULL) {
                return TINYPY_FALSE;
            }
        }
        __tinypy_representation_append_character(builder, (uint8_t)'<');
        if (kind == TINYPY_VALUE_OLD_INSTANCE) {
            __tinypy_representation_class_qualified_name(builder, tinypy_old_instance_class(value));
            __tinypy_representation_append(builder, " instance at ", 13U);
        }
        else {
            __tinypy_representation_type_qualified_name(builder, value->type);
            __tinypy_representation_append(builder, " object at ", 11U);
        }
        __tinypy_representation_pointer(builder, value);
        __tinypy_representation_append_character(builder, (uint8_t)'>');
        return TINYPY_TRUE;
    }
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_representation_build(tinypy_value_t *value, tinypy_bool_t raw, tinypy_bool_t skip_root_special, tinypy_error_t **out_error) {
    tinypy_representation_builder_t builder;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    /* _PyObject_Str returns an exact str itself. */
    if (raw != 0 && value->type == &vm->types[TINYPY_VALUE_STRING]) {
        return TINYPY_RET(value);
    }
    if (skip_root_special == 0 && __tinypy_internal_object_overrides_dispatch(value, raw != 0 ? TINYPY_INTERNAL_DISPATCH_BIT(STR) : TINYPY_INTERNAL_DISPATCH_BIT(REPR)) != 0) {
        tinypy_value_t *custom = __tinypy_representation_custom(value, raw != 0 ? vm->internal_special_str_key : vm->internal_special_repr_key, out_error);

        if (custom == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(custom) == TINYPY_VALUE_UNICODE) {
            tinypy_value_t *encoded = tinypy_internal_text_codec(vm, custom, NULL, NULL, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);

            TINYPY_DECREF(custom);
            return encoded;
        }
        return custom;
    }
    if (raw != 0 && TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *encoded = tinypy_internal_text_codec(vm, value, NULL, NULL, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);

        return encoded;
    }

    __tinypy_representation_initialize(&builder, vm);
    builder.root = value;
    builder.skip_root_special = skip_root_special;
    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_representation_value(&builder, value, raw, out_error) == 0 || builder.failed != 0) {
        if (builder.failed != 0 && (out_error == NULL || *out_error == NULL)) {
            tinypy_internal_make_vm_error(builder.vm, builder.memory_failed != 0 ? TINYPY_ERROR_MEMORY : TINYPY_ERROR_OVERFLOW, builder.memory_failed != 0 ? "memory allocation failed" : "representation is too large", out_error);
        }
        __tinypy_representation_destroy(&builder);
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_string_from_bytes_uninterned_checked(builder.vm, builder.bytes, builder.size, out_error);
    __tinypy_representation_destroy(&builder);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_representation_default_object(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_representation_builder_t builder;

    __tinypy_representation_initialize(&builder, TINYPY_VALUE_VM(value));
    TINYPY_CLEAR_ERROR(out_error);
    __tinypy_representation_append_character(&builder, (uint8_t)'<');
    __tinypy_representation_type_qualified_name(&builder, value->type);
    __tinypy_representation_append(&builder, " object at ", 11U);
    __tinypy_representation_pointer(&builder, value);
    __tinypy_representation_append_character(&builder, (uint8_t)'>');
    if (builder.failed != 0) {
        __tinypy_representation_destroy(&builder);
        tinypy_internal_make_vm_error(builder.vm, builder.memory_failed != 0 ? TINYPY_ERROR_MEMORY : TINYPY_ERROR_OVERFLOW, builder.memory_failed != 0 ? "memory allocation failed" : "representation is too large", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_string_from_bytes_uninterned_checked(builder.vm, builder.bytes, builder.size, out_error);
    __tinypy_representation_destroy(&builder);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_object_repr(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_representation_build(value, INT32_C(0), TINYPY_FALSE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_object_str(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_representation_build(value, INT32_C(1), TINYPY_FALSE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_repr_builtin(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_representation_build(value, TINYPY_FALSE, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_str_builtin(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_representation_build(value, TINYPY_TRUE, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_representation_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *result = user_data != NULL ? tinypy_internal_object_str_builtin(self, out_error) : tinypy_internal_object_repr_builtin(self, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_representation_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    /* object_str returns what tp_repr returns, so an overriding __repr__
       keeps a unicode result. */
    if (user_data != NULL && __tinypy_internal_object_overrides_dispatch(self, TINYPY_INTERNAL_DISPATCH_BIT(REPR)) != 0) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
        tinypy_value_t *custom = __tinypy_representation_custom_reported(self, vm->internal_special_repr_key, vm->internal_special_str_key, out_error);
        return custom;
    }
    if (user_data != NULL) {
        tinypy_value_t *return_value_1 = tinypy_object_repr(self, out_error);
        return return_value_1;
    }
    tinypy_value_t *return_value_2 = __tinypy_representation_default_object(self, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_type_representation_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 1U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__repr__ requires a type object", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_object_repr_builtin(TINYPY_TUPLE_GET(args, 0U), out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_representation_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_type_e kind = (tinypy_value_type_e)(intptr_t)user_data;

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 1U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != kind) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "representation method received an incompatible object", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_object_repr_builtin(TINYPY_TUPLE_GET(args, 0U), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_class_property(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__class__ descriptor received invalid arguments", out_error);
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *result = TINYPY_RET(&self->type->base.base);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* object_set_class refuses to delete __class__. */
static tinypy_value_t *__tinypy_object_class_delete(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)args;
    (void)kwargs;
    (void)user_data;
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "can't delete __class__ attribute", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* __class__ may be reassigned between Python-defined classes whose instances
   have the same layout, matching object_set_class in Python 2.7. */
static tinypy_value_t *__tinypy_object_class_assign(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 2U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__class__ descriptor received invalid arguments", out_error);
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *replacement = TINYPY_TUPLE_GET(args, 1U);

    if (TINYPY_VALUE_KIND(replacement) != TINYPY_VALUE_TYPE) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("__class__ must be set to new-style class, not '"),
            TINYPY_MESSAGE_PART_TYPE_NAME(replacement),
            TINYPY_MESSAGE_PART_LITERAL("' object")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
        return NULL;
    }
    tinypy_type_t *current = self->type;
    tinypy_type_t *target = (tinypy_type_t *)replacement;

    if ((current->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) == 0U || (target->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__class__ assignment: only for heap types", out_error);
        return NULL;
    }
    if (tinypy_internal_type_layout_compatible(target, current, "__class__", 9U, out_error) == 0) {
        return NULL;
    }
    TINYPY_INCREF(replacement);
    self->type = target;
    TINYPY_DECREF(&current->base.base);
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_sizeof_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    size_t size = tinypy_internal_value_allocation_size(self);
    size_t extra = 0U;

    switch (TINYPY_VALUE_KIND(self)) {
        case TINYPY_VALUE_LIST:
            extra = TINYPY_LIST_OBJECT(self)->allocated * sizeof(tinypy_value_t *);
            break;
        case TINYPY_VALUE_DICT:
            if (TINYPY_DICT_OBJECT(self)->table != TINYPY_DICT_OBJECT(self)->small_table) {
                extra = (TINYPY_DICT_OBJECT(self)->mask + 1U) * sizeof(tinypy_dict_entry_t);
            }
            break;
        case TINYPY_VALUE_SET:
        case TINYPY_VALUE_FROZENSET: {
            tinypy_value_t *dict = TINYPY_SET_OBJECT(self)->dict;

            extra = tinypy_internal_value_allocation_size(dict);
            if (TINYPY_DICT_OBJECT(dict)->table != TINYPY_DICT_OBJECT(dict)->small_table) {
                size_t table_size = (TINYPY_DICT_OBJECT(dict)->mask + 1U) * sizeof(tinypy_dict_entry_t);

                if (table_size > SIZE_MAX - extra) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "object size is too large", out_error);
                    return NULL;
                }
                extra += table_size;
            }
            break;
        }
        case TINYPY_VALUE_BYTEARRAY:
            extra = TINYPY_BYTEARRAY_OBJECT(self)->capacity;
            break;
        default:
            break;
    }
    if (extra > SIZE_MAX - size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "object size is too large", out_error);
        return NULL;
    }
    size += extra;
#if SIZE_MAX > INT64_MAX
    if (size > (size_t)INT64_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "object size is too large", out_error);
        return NULL;
    }
#endif
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)size);

    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_representation_types(tinypy_vm_t *vm) {
    static const tinypy_value_type_e semantic_repr_types[] = {
        TINYPY_VALUE_CODE,
        TINYPY_VALUE_FUNCTION,
        TINYPY_VALUE_CELL,
        TINYPY_VALUE_MODULE,
        TINYPY_VALUE_SUPER
    };
    static const tinypy_value_type_e direct_sizeof_types[] = {
        TINYPY_VALUE_LONG,
        TINYPY_VALUE_STRING,
        TINYPY_VALUE_UNICODE,
        TINYPY_VALUE_LIST,
        TINYPY_VALUE_DICT,
        TINYPY_VALUE_SET,
        TINYPY_VALUE_FROZENSET,
        TINYPY_VALUE_BYTEARRAY
    };
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_FLOAT], vm->internal_special_repr_key, __tinypy_representation_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_FLOAT], vm->internal_special_str_key, __tinypy_representation_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_INSTANCE], vm->internal_special_repr_key, __tinypy_object_representation_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_INSTANCE], vm->internal_special_str_key, __tinypy_object_representation_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);

    tinypy_value_t *class_getter = tinypy_native_function_new_key(vm->internal_special_class_key, __tinypy_object_class_property, NULL, NULL);
    tinypy_value_t *class_setter = tinypy_native_function_new_key(vm->internal_special_class_key, __tinypy_object_class_assign, NULL, NULL);
    tinypy_value_t *class_deleter = tinypy_native_function_new_key(vm->internal_special_class_key, __tinypy_object_class_delete, NULL, NULL);
    tinypy_value_t *class_property = tinypy_property_new(vm, class_getter, class_setter, class_deleter, NULL);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_INSTANCE], vm->internal_special_sizeof_key, __tinypy_object_sizeof_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);

    tinypy_type_set_attr_key(&vm->types[TINYPY_VALUE_INSTANCE], vm->internal_special_class_key, class_property);
    for (size_t index = 0U; index < sizeof(direct_sizeof_types) / sizeof(direct_sizeof_types[0]); ++index) {
        tinypy_internal_type_add_method(&vm->types[direct_sizeof_types[index]], vm->internal_special_sizeof_key, __tinypy_object_sizeof_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TYPE], vm->internal_special_repr_key, __tinypy_type_representation_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    for (size_t index = 0U; index < sizeof(semantic_repr_types) / sizeof(semantic_repr_types[0]); ++index) {
        tinypy_value_type_e kind = semantic_repr_types[index];
        tinypy_internal_type_add_method(&vm->types[kind], vm->internal_special_repr_key, __tinypy_builtin_representation_method, (void *)(intptr_t)kind, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    TINYPY_DECREF(class_property);
    TINYPY_DECREF(class_deleter);
    TINYPY_DECREF(class_setter);
    TINYPY_DECREF(class_getter);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *const names[1] = {vm->internal_object_key};
    tinypy_value_t *values[1];

    if (tinypy_internal_constructor_optional_arguments(vm, "str", 3U, args, kwargs, names, 1U, 0U, values, out_error) == 0) {
        return NULL;
    }
    if (values[0] == NULL) {
        tinypy_value_t *value = TINYPY_RET_EMPTY_STRING(vm);
        tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(type, value, out_error);
        return return_value_1;
    }
    tinypy_value_t *item = values[0];
    if (TINYPY_VALUE_KIND(item) == TINYPY_VALUE_BYTEARRAY && tinypy_internal_object_has_special_override_key(item, vm->internal_special_str_key) == 0) {
        tinypy_value_t *value = tinypy_internal_bytearray_string(item, out_error);
        tinypy_value_t *return_value_2 = tinypy_internal_immutable_subclass_copy(type, value, out_error);
        return return_value_2;
    }
    tinypy_value_t *value = tinypy_object_str(item, out_error);
    if (type == &vm->types[TINYPY_VALUE_STRING]) {
        return value;
    }
    tinypy_value_t *return_value_3 = tinypy_internal_immutable_subclass_copy(type, value, out_error);
    return return_value_3;
}
