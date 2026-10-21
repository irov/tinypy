#include "internal.h"

enum {
    TINYPY_UNICODE_ALPHA = 1 << 0,
    TINYPY_UNICODE_DIGIT = 1 << 1,
    TINYPY_UNICODE_ALNUM = 1 << 2,
    TINYPY_UNICODE_SPACE = 1 << 3,
    TINYPY_UNICODE_LOWER = 1 << 4,
    TINYPY_UNICODE_UPPER = 1 << 5,
    TINYPY_UNICODE_TITLE = 1 << 6
};

typedef struct tinypy_unicode_property_range_t {
    uint32_t begin;
    uint32_t end;
    uint8_t flags;
} tinypy_unicode_property_range_t;

typedef struct tinypy_unicode_mapping_t {
    uint32_t code_point;
    uint32_t lower;
    uint32_t upper;
    uint32_t title;
} tinypy_unicode_mapping_t;

typedef struct tinypy_unicode_decimal_range_t {
    uint32_t begin;
    uint32_t end;
    uint8_t digit;
} tinypy_unicode_decimal_range_t;

typedef struct tinypy_unicode_numeric_range_t {
    uint32_t begin;
    uint32_t end;
} tinypy_unicode_numeric_range_t;

#include "generated/unicode_db.h"

enum {
    TINYPY_UNICODE_INDEX_STRIDE = 64
};

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_unicode_utf8_width(uint8_t lead) {
    return lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
}
//////////////////////////////////////////////////////////////////////////
/* The index only speeds offsets up: when the heap budget or the host has no
   room for it, callers scan the text instead. */
static tinypy_bool_t __tinypy_unicode_build_index(tinypy_value_t *value) {
    tinypy_unicode_object_t *unicode = TINYPY_UNICODE_OBJECT(value);
    size_t character_count = TINYPY_SIZED_SIZE(value);
    size_t table_count;
    size_t table_index = 0U;
    size_t byte_offset = 0U;
    size_t scalar_index;

    table_count = (character_count + (size_t)TINYPY_UNICODE_INDEX_STRIDE - 1U) / (size_t)TINYPY_UNICODE_INDEX_STRIDE;
    size_t *index_offsets = (size_t *)tinypy_internal_pool_allocate_checked(TINYPY_VALUE_VM(value), table_count * sizeof(*index_offsets));
    if (index_offsets == NULL) {
        return TINYPY_FALSE;
    }
    for (scalar_index = 0U; scalar_index < character_count; ++scalar_index) {
        if (scalar_index % (size_t)TINYPY_UNICODE_INDEX_STRIDE == 0U) {
            index_offsets[table_index++] = byte_offset;
        }
        byte_offset += __tinypy_unicode_utf8_width(unicode->utf8[byte_offset]);
    }
    unicode->index_offsets = index_offsets;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_unicode_byte_offset(tinypy_value_t *value, size_t character_index) {
    tinypy_unicode_object_t *unicode = TINYPY_UNICODE_OBJECT(value);
    size_t character_count = TINYPY_SIZED_SIZE(value);
    size_t byte_offset;
    size_t scalar_index;

    if (character_index >= character_count) {
        return unicode->byte_size;
    }
    if (unicode->byte_size == character_count) {
        return character_index;
    }
    tinypy_bool_t indexed = unicode->index_offsets != NULL;
    if (indexed == 0 && character_index > (size_t)TINYPY_UNICODE_INDEX_STRIDE && character_count - character_index > (size_t)TINYPY_UNICODE_INDEX_STRIDE) {
        indexed = __tinypy_unicode_build_index(value);
    }
    if (indexed == 0) {
        if (character_index <= character_count - character_index) {
            byte_offset = 0U;
            scalar_index = 0U;
            while (scalar_index < character_index) {
                byte_offset += __tinypy_unicode_utf8_width(unicode->utf8[byte_offset]);
                scalar_index += 1U;
            }
            return byte_offset;
        }
        byte_offset = unicode->byte_size;
        scalar_index = character_count;
        while (scalar_index > character_index) {
            byte_offset -= 1U;
            while (byte_offset != 0U && (unicode->utf8[byte_offset] & 0xc0U) == 0x80U) {
                byte_offset -= 1U;
            }
            scalar_index -= 1U;
        }
        return byte_offset;
    }
    scalar_index = character_index - character_index % (size_t)TINYPY_UNICODE_INDEX_STRIDE;
    byte_offset = unicode->index_offsets[character_index / (size_t)TINYPY_UNICODE_INDEX_STRIDE];
    while (scalar_index < character_index) {
        byte_offset += __tinypy_unicode_utf8_width(unicode->utf8[byte_offset]);
        scalar_index += 1U;
    }
    return byte_offset;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_unicode_character_index(tinypy_value_t *value, size_t byte_offset) {
    tinypy_unicode_object_t *unicode = TINYPY_UNICODE_OBJECT(value);
    size_t character_count = TINYPY_SIZED_SIZE(value);
    size_t offset;
    size_t index;

    if (byte_offset >= unicode->byte_size) {
        return character_count;
    }
    if (unicode->byte_size == character_count) {
        return byte_offset;
    }
    if (unicode->index_offsets == NULL) {
        offset = 0U;
        index = 0U;
        while (offset < byte_offset && index < (size_t)TINYPY_UNICODE_INDEX_STRIDE) {
            offset += __tinypy_unicode_utf8_width(unicode->utf8[offset]);
            index += 1U;
        }
        if (offset == byte_offset) {
            return index;
        }
        offset = unicode->byte_size;
        index = character_count;
        while (offset > byte_offset && character_count - index < (size_t)TINYPY_UNICODE_INDEX_STRIDE) {
            offset -= 1U;
            while (offset != 0U && (unicode->utf8[offset] & 0xc0U) == 0x80U) {
                offset -= 1U;
            }
            index -= 1U;
        }
        if (offset == byte_offset) {
            return index;
        }
        if (__tinypy_unicode_build_index(value) == 0) {
            offset = 0U;
            index = 0U;
            while (offset < byte_offset) {
                offset += __tinypy_unicode_utf8_width(unicode->utf8[offset]);
                index += 1U;
            }
            return index;
        }
    }
    size_t table_count = (character_count + (size_t)TINYPY_UNICODE_INDEX_STRIDE - 1U) / (size_t)TINYPY_UNICODE_INDEX_STRIDE;
    size_t low = 0U;
    size_t high = table_count;

    while (low + 1U < high) {
        size_t middle = low + (high - low) / 2U;

        if (unicode->index_offsets[middle] <= byte_offset) {
            low = middle;
        }
        else {
            high = middle;
        }
    }
    index = low * (size_t)TINYPY_UNICODE_INDEX_STRIDE;
    offset = unicode->index_offsets[low];
    while (offset < byte_offset) {
        offset += __tinypy_unicode_utf8_width(unicode->utf8[offset]);
        index += 1U;
    }
    return index;
}
//////////////////////////////////////////////////////////////////////////
/* The wchar_t code units of value without the terminator: where wchar_t has
   16 bits a code point above the BMP, which canonical UTF-8 encodes from a
   lead byte of 0xf0, takes a surrogate pair. */
static size_t __tinypy_unicode_wchar_size(tinypy_value_t *value) {
    size_t size = TINYPY_SIZED_SIZE(value);
#if WCHAR_MAX <= 0xffff
    tinypy_unicode_object_t *unicode = TINYPY_UNICODE_OBJECT(value);
    size_t byte_offset;

    for (byte_offset = 0U; byte_offset < unicode->byte_size; ++byte_offset) {
        if (unicode->utf8[byte_offset] >= 0xf0U) {
            size += 1U;
        }
    }
#endif
    return size;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_unicode_destroy(tinypy_value_t *value) {
    tinypy_unicode_object_t *unicode = TINYPY_UNICODE_OBJECT(value);

    if (unicode->index_offsets != NULL) {
        size_t table_count = (TINYPY_SIZED_SIZE(value) + (size_t)TINYPY_UNICODE_INDEX_STRIDE - 1U) / (size_t)TINYPY_UNICODE_INDEX_STRIDE;

        tinypy_internal_vm_deallocate(TINYPY_VALUE_VM(value), unicode->index_offsets, table_count * sizeof(*unicode->index_offsets));
        unicode->index_offsets = NULL;
    }
    if (unicode->native_buffer != NULL) {
        tinypy_internal_vm_deallocate(TINYPY_VALUE_VM(value), unicode->native_buffer, TINYPY_SIZED_SIZE(value) * sizeof(*unicode->native_buffer));
        unicode->native_buffer = NULL;
    }
    if (unicode->wchar_buffer != NULL) {
        size_t wchar_size = __tinypy_unicode_wchar_size(value);

        tinypy_internal_vm_deallocate(TINYPY_VALUE_VM(value), unicode->wchar_buffer, (wchar_size + 1U) * sizeof(*unicode->wchar_buffer));
        unicode->wchar_buffer = NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
const uint8_t *tinypy_internal_unicode_native_buffer(tinypy_value_t *value, size_t *out_size, tinypy_error_t **out_error) {
    tinypy_unicode_object_t *unicode = TINYPY_UNICODE_OBJECT(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    size_t count = TINYPY_SIZED_SIZE(value);

    if (count > (size_t)PTRDIFF_MAX / sizeof(*unicode->native_buffer)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "Unicode buffer is too large", out_error);
        return NULL;
    }
    *out_size = count * sizeof(*unicode->native_buffer);
    if (count == 0U) {
        return unicode->utf8;
    }
    if (unicode->native_buffer == NULL) {
        uint32_t *native = (uint32_t *)__tinypy_internal_vm_allocate_checked(vm, *out_size, out_error);
        if (native == NULL) {
            return NULL;
        }
        size_t byte_offset = 0U;
        size_t index;

        for (index = 0U; index < count; ++index) {
            byte_offset += tinypy_internal_utf8_decode(unicode->utf8 + byte_offset, unicode->byte_size - byte_offset, &native[index]);
        }
        unicode->native_buffer = native;
    }
    return (const uint8_t *)unicode->native_buffer;
}
//////////////////////////////////////////////////////////////////////////
const wchar_t *tinypy_unicode_wchar_view(tinypy_value_t *value, size_t *out_size, tinypy_error_t **out_error) {
    static const wchar_t empty_view[1] = {L'\0'};
    tinypy_unicode_object_t *unicode = TINYPY_UNICODE_OBJECT(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    size_t count = TINYPY_SIZED_SIZE(value);
    size_t size = __tinypy_unicode_wchar_size(value);

    TINYPY_CLEAR_ERROR(out_error);
    *out_size = size;
    if (count == 0U) {
        return empty_view;
    }
    if (unicode->wchar_buffer != NULL) {
        return unicode->wchar_buffer;
    }
    if (size > (size_t)PTRDIFF_MAX / sizeof(*unicode->wchar_buffer) - 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "Unicode buffer is too large", out_error);
        return NULL;
    }
    wchar_t *buffer = (wchar_t *)__tinypy_internal_vm_allocate_checked(vm, (size + 1U) * sizeof(*buffer), out_error);
    if (buffer == NULL) {
        return NULL;
    }
    size_t byte_offset = 0U;
    size_t unit = 0U;
    size_t index;

    for (index = 0U; index < count; ++index) {
        uint32_t code_point;

        byte_offset += tinypy_internal_utf8_decode(unicode->utf8 + byte_offset, unicode->byte_size - byte_offset, &code_point);
#if WCHAR_MAX <= 0xffff
        if (code_point > 0xffffU) {
            code_point -= 0x10000U;
            buffer[unit++] = (wchar_t)(0xd800U + (code_point >> 10));
            buffer[unit++] = (wchar_t)(0xdc00U + (code_point & 0x3ffU));
            continue;
        }
#endif
        buffer[unit++] = (wchar_t)code_point;
    }
    buffer[unit] = L'\0';
    unicode->wchar_buffer = buffer;
    return buffer;
}

//////////////////////////////////////////////////////////////////////////
static uint8_t __tinypy_unicode_flags(uint32_t code_point) {
    size_t index;
    size_t end;
    size_t count = sizeof(__tinypy_unicode_property_ranges) / sizeof(__tinypy_unicode_property_ranges[0]);

    if (code_point < UINT32_C(0x100)) {
        return __tinypy_unicode_latin1_flags[code_point];
    }
    if (code_point > UINT32_C(0x10ffff)) {
        return 0U;
    }
    index = __tinypy_unicode_property_page_index[code_point >> 8U];
    end = __tinypy_unicode_property_page_index[(code_point >> 8U) + 1U];
    if (end < count) {
        end += 1U;
    }
    size_t low = index;
    size_t high = end;

    while (low < high) {
        size_t middle = low + (high - low) / 2U;

        if (__tinypy_unicode_property_ranges[middle].begin <= code_point) {
            low = middle + 1U;
        }
        else {
            high = middle;
        }
    }
    if (low != index) {
        const tinypy_unicode_property_range_t *range = &__tinypy_unicode_property_ranges[low - 1U];

        return code_point <= range->end ? range->flags : 0U;
    }
    return 0U;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_unicode_mapping_t *__tinypy_unicode_mapping(uint32_t code_point) {
    size_t index;
    size_t end;

    if (code_point < UINT32_C(0x100)) {
        return &__tinypy_unicode_latin1_mappings[code_point];
    }
    if (code_point > UINT32_C(0x10ffff)) {
        return NULL;
    }
    index = __tinypy_unicode_mapping_page_index[code_point >> 8U];
    end = __tinypy_unicode_mapping_page_index[(code_point >> 8U) + 1U];
    while (index < end) {
        size_t middle = index + (end - index) / 2U;
        const tinypy_unicode_mapping_t *mapping = &__tinypy_unicode_mappings[middle];

        if (code_point < mapping->code_point) {
            end = middle;
        }
        else if (code_point > mapping->code_point) {
            index = middle + 1U;
        }
        else {
            return mapping;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_utf8_decode(const uint8_t *bytes, size_t size, uint32_t *out_code_point) {
    uint8_t first;
    size_t width;
    uint32_t code_point;
    size_t index;

    if (size == 0U) {
        return 0U;
    }
    first = bytes[0];
    if (first < 0x80U) {
        *out_code_point = first;
        return 1U;
    }
    width = first < 0xe0U ? 2U : (first < 0xf0U ? 3U : 4U);
    if (width > size || (width == 2U && first < 0xc2U) || (width == 4U && first > 0xf4U)) {
        return 0U;
    }
    code_point = first & (uint32_t)(0x7fU >> width);
    for (index = 1U; index < width; ++index) {
        if ((bytes[index] & 0xc0U) != 0x80U) {
            return 0U;
        }
        code_point = (code_point << 6U) | (bytes[index] & 0x3fU);
    }
    if ((width == 3U && code_point < 0x800U) || (width == 4U && code_point < 0x10000U) || code_point > 0x10ffffU) {
        return 0U;
    }
    *out_code_point = code_point;
    return width;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_utf8_invalid_span(const uint8_t *bytes, size_t size) {
    uint8_t first;
    size_t expected;
    size_t index;

    if (size == 0U) {
        return 0U;
    }
    first = bytes[0];
    if (first >= 0x80U && first < 0xc0U) {
        return 1U;
    }
    if (first < 0x80U || first < 0xc2U || first > 0xf4U) {
        return 1U;
    }
    expected = first < 0xe0U ? 2U : (first < 0xf0U ? 3U : 4U);
    for (index = 1U; index < expected && index < size; ++index) {
        if ((bytes[index] & 0xc0U) != 0x80U) {
            return index;
        }
    }
    return size < expected ? size : expected - 1U;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_utf8_encode(uint32_t code_point, uint8_t bytes[4]) {
    if (code_point <= 0x7fU) {
        bytes[0] = (uint8_t)code_point;
        return 1U;
    }
    if (code_point <= 0x7ffU) {
        bytes[0] = (uint8_t)(0xc0U | (code_point >> 6U));
        bytes[1] = (uint8_t)(0x80U | (code_point & 0x3fU));
        return 2U;
    }
    if (code_point <= 0xffffU) {
        bytes[0] = (uint8_t)(0xe0U | (code_point >> 12U));
        bytes[1] = (uint8_t)(0x80U | ((code_point >> 6U) & 0x3fU));
        bytes[2] = (uint8_t)(0x80U | (code_point & 0x3fU));
        return 3U;
    }
    bytes[0] = (uint8_t)(0xf0U | (code_point >> 18U));
    bytes[1] = (uint8_t)(0x80U | ((code_point >> 12U) & 0x3fU));
    bytes[2] = (uint8_t)(0x80U | ((code_point >> 6U) & 0x3fU));
    bytes[3] = (uint8_t)(0x80U | (code_point & 0x3fU));
    return 4U;
}
//////////////////////////////////////////////////////////////////////////
/* The size of the ASCII run the bytes start with, read a word at a time. */
size_t tinypy_internal_ascii_prefix(const uint8_t *bytes, size_t size) {
    size_t high_bits = SIZE_MAX / 0xffU * 0x80U;
    size_t offset = 0U;

    while (size - offset >= sizeof(size_t)) {
        size_t word;

        (void)memcpy(&word, bytes + offset, sizeof(word));
        if ((word & high_bits) != 0U) {
            break;
        }
        offset += sizeof(word);
    }
    while (offset < size && bytes[offset] < 0x80U) {
        offset += 1U;
    }
    return offset;
}
//////////////////////////////////////////////////////////////////////////
uint32_t tinypy_internal_unicode_lower(uint32_t code_point) {
    if (code_point >= (uint32_t)'A' && code_point <= (uint32_t)'Z') {
        return code_point + ((uint32_t)'a' - (uint32_t)'A');
    }
    const tinypy_unicode_mapping_t *mapping = __tinypy_unicode_mapping(code_point);
    return mapping != NULL ? mapping->lower : code_point;
}
//////////////////////////////////////////////////////////////////////////
uint32_t tinypy_internal_unicode_upper(uint32_t code_point) {
    if (code_point >= (uint32_t)'a' && code_point <= (uint32_t)'z') {
        return code_point - ((uint32_t)'a' - (uint32_t)'A');
    }
    const tinypy_unicode_mapping_t *mapping = __tinypy_unicode_mapping(code_point);
    return mapping != NULL ? mapping->upper : code_point;
}
//////////////////////////////////////////////////////////////////////////
uint32_t tinypy_internal_unicode_title(uint32_t code_point) {
    if (code_point >= (uint32_t)'a' && code_point <= (uint32_t)'z') {
        return code_point - ((uint32_t)'a' - (uint32_t)'A');
    }
    const tinypy_unicode_mapping_t *mapping = __tinypy_unicode_mapping(code_point);
    return mapping != NULL ? mapping->title : code_point;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_unicode_is_alpha(uint32_t code_point) {
    uint8_t flags = __tinypy_unicode_flags(code_point);
    return (flags & TINYPY_UNICODE_ALPHA) != 0U;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_unicode_is_digit(uint32_t code_point) {
    uint8_t flags = __tinypy_unicode_flags(code_point);
    return (flags & TINYPY_UNICODE_DIGIT) != 0U;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_unicode_is_alnum(uint32_t code_point) {
    uint8_t flags = __tinypy_unicode_flags(code_point);
    return (flags & TINYPY_UNICODE_ALNUM) != 0U;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_unicode_is_space(uint32_t code_point) {
    uint8_t flags = __tinypy_unicode_flags(code_point);
    return (flags & TINYPY_UNICODE_SPACE) != 0U;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_unicode_is_lower(uint32_t code_point) {
    uint8_t flags = __tinypy_unicode_flags(code_point);
    return (flags & TINYPY_UNICODE_LOWER) != 0U;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_unicode_is_upper(uint32_t code_point) {
    uint8_t flags = __tinypy_unicode_flags(code_point);
    return (flags & TINYPY_UNICODE_UPPER) != 0U;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_unicode_is_title(uint32_t code_point) {
    uint8_t flags = __tinypy_unicode_flags(code_point);
    return (flags & TINYPY_UNICODE_TITLE) != 0U;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_unicode_is_cased(uint32_t code_point) {
    uint8_t flags = __tinypy_unicode_flags(code_point);
    return (flags & (TINYPY_UNICODE_LOWER | TINYPY_UNICODE_UPPER | TINYPY_UNICODE_TITLE)) != 0U;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_unicode_is_linebreak(uint32_t code_point) {
    return code_point == UINT32_C(0x000a) || code_point == UINT32_C(0x000b) || code_point == UINT32_C(0x000c) || code_point == UINT32_C(0x000d) || (code_point >= UINT32_C(0x001c) && code_point <= UINT32_C(0x001e)) || code_point == UINT32_C(0x0085) || code_point == UINT32_C(0x2028) || code_point == UINT32_C(0x2029);
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_unicode_decimal_digit(uint32_t code_point, uint8_t *out_digit) {
    size_t begin = 0U;
    size_t end = sizeof(__tinypy_unicode_decimal_ranges) / sizeof(__tinypy_unicode_decimal_ranges[0]);

    if (code_point >= (uint32_t)'0' && code_point <= (uint32_t)'9') {
        *out_digit = (uint8_t)(code_point - (uint32_t)'0');
        return TINYPY_TRUE;
    }
    while (begin < end) {
        size_t middle = begin + (end - begin) / 2U;
        const tinypy_unicode_decimal_range_t *range = &__tinypy_unicode_decimal_ranges[middle];

        if (code_point < range->begin) {
            end = middle;
        }
        else if (code_point > range->end) {
            begin = middle + 1U;
        }
        else {
            *out_digit = (uint8_t)(range->digit + (code_point - range->begin));
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_unicode_is_decimal(uint32_t code_point) {
    uint8_t digit;

    tinypy_bool_t return_value_1 = tinypy_internal_unicode_decimal_digit(code_point, &digit);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_unicode_is_numeric(uint32_t code_point) {
    size_t begin = 0U;
    size_t end = sizeof(__tinypy_unicode_numeric_ranges) / sizeof(__tinypy_unicode_numeric_ranges[0]);

    while (begin < end) {
        size_t middle = begin + (end - begin) / 2U;
        const tinypy_unicode_numeric_range_t *range = &__tinypy_unicode_numeric_ranges[middle];

        if (code_point < range->begin) {
            end = middle;
        }
        else if (code_point > range->end) {
            begin = middle + 1U;
        }
        else {
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_text_to_unicode(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *result = tinypy_internal_immutable_subclass_copy(&vm->types[TINYPY_VALUE_UNICODE], TINYPY_RET(value), out_error);

        return result;
    }
    tinypy_value_t *result = tinypy_internal_text_codec_builtin(vm, value, TINYPY_INTERNAL_BUILTIN_CODEC_ASCII, NULL, NULL, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
