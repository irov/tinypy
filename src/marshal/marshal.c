#include "tinypy/marshal.h"

#include <float.h>
#include <limits.h>
#include <string.h>

#define TINYPY_MARSHAL_DEFAULT_INPUT_BYTES ((size_t)64U * 1024U * 1024U)
#define TINYPY_MARSHAL_DEFAULT_ALLOCATED_BYTES ((size_t)256U * 1024U * 1024U)
#define TINYPY_MARSHAL_DEFAULT_DEPTH ((size_t)2000U)
#define TINYPY_MARSHAL_DEFAULT_OBJECTS ((size_t)1000000U)
#define TINYPY_MARSHAL_DEFAULT_STRING_BYTES ((size_t)64U * 1024U * 1024U)
#define TINYPY_MARSHAL_DEFAULT_CONTAINER_ITEMS ((size_t)1000000U)
#define TINYPY_MARSHAL_INTERN_CHUNK_SIZE ((size_t)64U)

#define TYPE_NULL ((uint8_t)'0')
#define TYPE_NONE ((uint8_t)'N')
#define TYPE_FALSE ((uint8_t)'F')
#define TYPE_TRUE ((uint8_t)'T')
#define TYPE_STOPITER ((uint8_t)'S')
#define TYPE_ELLIPSIS ((uint8_t)'.')
#define TYPE_INTEGER ((uint8_t)'i')
#define TYPE_INTEGER64 ((uint8_t)'I')
#define TYPE_FLOAT ((uint8_t)'f')
#define TYPE_BINARY_FLOAT ((uint8_t)'g')
#define TYPE_COMPLEX ((uint8_t)'x')
#define TYPE_BINARY_COMPLEX ((uint8_t)'y')
#define TYPE_LONG ((uint8_t)'l')
#define TYPE_STRING ((uint8_t)'s')
#define TYPE_INTERNED ((uint8_t)'t')
#define TYPE_STRINGREF ((uint8_t)'R')
#define TYPE_TUPLE ((uint8_t)'(')
#define TYPE_LIST ((uint8_t)'[')
#define TYPE_DICT ((uint8_t)'{')
#define TYPE_CODE ((uint8_t)'c')
#define TYPE_UNICODE ((uint8_t)'u')
#define TYPE_SET ((uint8_t)'<')
#define TYPE_FROZENSET ((uint8_t)'>')

typedef union tinypy_marshal_max_align_t {
    void *pointer_value;
    void (*function_value)(void);
    int64_t integer_value;
    long double floating_value;
} tinypy_marshal_max_align_t;

typedef struct tinypy_marshal_alignment_probe_t {
    char prefix;
    tinypy_marshal_max_align_t value;
} tinypy_marshal_alignment_probe_t;

#define TINYPY_MARSHAL_ALIGNMENT \
    ((size_t)offsetof(tinypy_marshal_alignment_probe_t, value))

typedef struct tinypy_marshal_allocation_t {
    struct tinypy_marshal_allocation_t *next;
    size_t total_size;
    tinypy_marshal_max_align_t alignment;
} tinypy_marshal_allocation_t;

typedef struct tinypy_marshal_dict_entry_t {
    struct tinypy_marshal_dict_entry_t *next;
    tinypy_marshal_object_t *key;
    tinypy_marshal_object_t *value;
} tinypy_marshal_dict_entry_t;

typedef struct tinypy_marshal_intern_chunk_t {
    struct tinypy_marshal_intern_chunk_t *next;
    size_t count;
    tinypy_marshal_object_t *items[TINYPY_MARSHAL_INTERN_CHUNK_SIZE];
} tinypy_marshal_intern_chunk_t;

struct tinypy_marshal_object_t {
    tinypy_marshal_type_e type;
    uint8_t wire_type;
    uint8_t reserved[3];
    union {
        tinypy_bool_t boolean_value;
        int64_t integer_value;
        struct {
            int32_t sign;
            size_t count;
            uint16_t *digits;
        } long_value;
        struct {
            double value;
            uint8_t *text;
            size_t text_size;
        } float_value;
        struct {
            double real;
            double imaginary;
            uint8_t *real_text;
            size_t real_text_size;
            uint8_t *imaginary_text;
            size_t imaginary_text_size;
        } complex_value;
        struct {
            uint8_t *bytes;
            size_t size;
            size_t code_points;
            tinypy_bool_t interned;
        } string_value;
        struct {
            size_t count;
            tinypy_marshal_object_t **items;
        } sequence_value;
        struct {
            size_t count;
            tinypy_marshal_dict_entry_t *first;
            tinypy_marshal_dict_entry_t *last;
        } dict_value;
        tinypy_marshal_code_t code_value;
    } as;
};

struct tinypy_marshal_document_t {
    uint32_t state;
    tinypy_allocator_t allocator;
    tinypy_marshal_limits_t limits;
    size_t input_size;
    size_t allocated_bytes;
    size_t object_count;
    tinypy_marshal_allocation_t *allocations;
    tinypy_marshal_intern_chunk_t *intern_first;
    tinypy_marshal_intern_chunk_t *intern_last;
    size_t intern_count;
    tinypy_marshal_object_t *root;
};

typedef struct tinypy_marshal_parser_t {
    const uint8_t *bytes;
    size_t size;
    size_t offset;
    size_t depth;
    size_t objects_seen;
    uint8_t current_wire_type;
    tinypy_marshal_result_e result;
    tinypy_marshal_document_t *document;
    tinypy_marshal_error_t *error;
} tinypy_marshal_parser_t;

typedef struct tinypy_marshal_writer_t {
    const tinypy_marshal_document_t *document;
    uint8_t *buffer;
    size_t capacity;
    size_t offset;
    size_t depth;
    size_t object_count;
    size_t emitted_intern_count;
    size_t max_output_bytes;
    size_t max_depth;
    tinypy_marshal_result_e result;
    tinypy_marshal_error_t *error;
    const tinypy_marshal_object_t *current_object;
    size_t current_object_index;
    uint8_t current_wire_type;
} tinypy_marshal_writer_t;

#define TINYPY_MARSHAL_DOCUMENT_LIVE UINT32_C(0x4d415253)

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_marshal_cstring_size(const char *text) {
    size_t size = 0U;

    while (text[size] != '\0') {
        size += 1U;
    }

    return size;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_marshal_set_error_direct(tinypy_marshal_error_t *error, tinypy_marshal_result_e code, size_t offset, uint8_t wire_type, const char *message) {
    if (error == NULL) {
        return;
    }

    (void)memset(error, 0, sizeof(*error));
    error->abi_version = TINYPY_MARSHAL_ABI_VERSION;
    error->struct_size = (uint32_t)sizeof(*error);
    error->code = code;
    error->offset = offset;
    error->wire_type = wire_type;
    error->object = NULL;
    error->object_index = SIZE_MAX;
    error->message = message;
    error->message_size = __tinypy_marshal_cstring_size(message);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_marshal_parser_fail(tinypy_marshal_parser_t *parser, tinypy_marshal_result_e code, size_t offset, const char *message) {
    if (parser->result != TINYPY_MARSHAL_OK) {
        return;
    }

    parser->result = code;
    __tinypy_marshal_set_error_direct(
        parser->error,
        code,
        offset,
        parser->current_wire_type,
        message);
    if (parser->error != NULL && parser->objects_seen != 0U) {
        parser->error->object_index = parser->objects_seen - 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_limits_valid(const tinypy_marshal_limits_t *limits) {
    return limits->abi_version == TINYPY_MARSHAL_ABI_VERSION && limits->struct_size >= (uint32_t)sizeof(*limits) && limits->max_input_bytes != 0U && limits->max_allocated_bytes != 0U && limits->max_depth != 0U && limits->max_objects != 0U && limits->max_string_bytes != 0U && limits->max_container_items != 0U;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_marshal_limits_init(tinypy_marshal_limits_t *limits) {

    (void)memset(limits, 0, sizeof(*limits));
    limits->abi_version = TINYPY_MARSHAL_ABI_VERSION;
    limits->struct_size = (uint32_t)sizeof(*limits);
    limits->max_input_bytes = TINYPY_MARSHAL_DEFAULT_INPUT_BYTES;
    limits->max_allocated_bytes = TINYPY_MARSHAL_DEFAULT_ALLOCATED_BYTES;
    limits->max_depth = TINYPY_MARSHAL_DEFAULT_DEPTH;
    limits->max_objects = TINYPY_MARSHAL_DEFAULT_OBJECTS;
    limits->max_string_bytes = TINYPY_MARSHAL_DEFAULT_STRING_BYTES;
    limits->max_container_items = TINYPY_MARSHAL_DEFAULT_CONTAINER_ITEMS;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_marshal_write_options_init(tinypy_marshal_write_options_t *options) {

    (void)memset(options, 0, sizeof(*options));
    options->abi_version = TINYPY_MARSHAL_ABI_VERSION;
    options->struct_size = (uint32_t)sizeof(*options);
    options->max_output_bytes = TINYPY_MARSHAL_DEFAULT_INPUT_BYTES;
    options->max_depth = TINYPY_MARSHAL_DEFAULT_DEPTH;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_multiply_size(size_t left, size_t right, size_t *out) {
    if (left != 0U && right > SIZE_MAX / left) {
        return TINYPY_FALSE;
    }
    *out = left * right;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void *__tinypy_marshal_graph_allocate(tinypy_marshal_parser_t *parser, size_t payload_size) {
    tinypy_marshal_document_t *document = parser->document;
    size_t total_size;

    if (payload_size > SIZE_MAX - sizeof(tinypy_marshal_allocation_t)) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_BYTE_LIMIT,
            parser->offset,
            "marshal allocation size overflow");
        return NULL;
    }
    total_size = sizeof(tinypy_marshal_allocation_t) + payload_size;
    if (total_size > document->limits.max_allocated_bytes - document->allocated_bytes) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_BYTE_LIMIT,
            parser->offset,
            "marshal allocated-byte limit exceeded");
        return NULL;
    }

    tinypy_marshal_allocation_t *allocation = (tinypy_marshal_allocation_t *)document->allocator.allocate(
        document->allocator.user_data,
        total_size,
        TINYPY_MARSHAL_ALIGNMENT,
        TINYPY_MARSHAL_ALLOC_TAG_GRAPH);

    allocation->next = document->allocations;
    allocation->total_size = total_size;
    document->allocations = allocation;
    document->allocated_bytes += total_size;
    (void)memset((void *)(allocation + 1), 0, payload_size);
    return (void *)(allocation + 1);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_object_t *__tinypy_marshal_object_allocate(tinypy_marshal_parser_t *parser, tinypy_marshal_type_e type, uint8_t wire_type) {
    tinypy_marshal_object_t *object =
        (tinypy_marshal_object_t *)__tinypy_marshal_graph_allocate(parser, sizeof(*object));

    if (object != NULL) {
        object->type = type;
        object->wire_type = wire_type;
    }
    return object;
}
//////////////////////////////////////////////////////////////////////////
static uint8_t *__tinypy_marshal_graph_copy(tinypy_marshal_parser_t *parser, const uint8_t *source, size_t size) {
    uint8_t *copy;

    if (size == 0U) {
        return NULL;
    }
    copy = (uint8_t *)__tinypy_marshal_graph_allocate(parser, size);
    if (copy != NULL) {
        (void)memcpy(copy, source, size);
    }
    return copy;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_take(tinypy_marshal_parser_t *parser, size_t count, const uint8_t **out_bytes) {
    if (count > parser->size - parser->offset) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_TRUNCATED,
            parser->size,
            "marshal input is truncated");
        return TINYPY_FALSE;
    }

    *out_bytes = parser->bytes + parser->offset;
    parser->offset += count;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_read_u8(tinypy_marshal_parser_t *parser, uint8_t *out_value) {
    const uint8_t *bytes;
    if (!__tinypy_marshal_take(parser, 1U, &bytes)) {
        return TINYPY_FALSE;
    }
    *out_value = bytes[0];
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_read_u16(tinypy_marshal_parser_t *parser, uint16_t *out_value) {
    const uint8_t *bytes;
    if (!__tinypy_marshal_take(parser, 2U, &bytes)) {
        return TINYPY_FALSE;
    }
    *out_value = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_read_u32(tinypy_marshal_parser_t *parser, uint32_t *out_value) {
    const uint8_t *bytes;
    if (!__tinypy_marshal_take(parser, 4U, &bytes)) {
        return TINYPY_FALSE;
    }
    *out_value =
        (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) | ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_marshal_u32_as_i32(uint32_t value) {
    if (value <= (uint32_t)INT32_MAX) {
        return (int32_t)value;
    }
    return INT32_MIN + (int32_t)(value - UINT32_C(0x80000000));
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_read_i32(tinypy_marshal_parser_t *parser, int32_t *out_value) {
    uint32_t value;
    if (!__tinypy_marshal_read_u32(parser, &value)) {
        return TINYPY_FALSE;
    }
    *out_value = __tinypy_marshal_u32_as_i32(value);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_read_i64(tinypy_marshal_parser_t *parser, int64_t *out_value) {
    uint32_t low;
    uint32_t high;
    uint64_t value;

    if (!__tinypy_marshal_read_u32(parser, &low) || !__tinypy_marshal_read_u32(parser, &high)) {
        return TINYPY_FALSE;
    }
    value = (uint64_t)low | ((uint64_t)high << 32U);
    if (value <= (uint64_t)INT64_MAX) {
        *out_value = (int64_t)value;
    }
    else {
        *out_value = INT64_MIN + (int64_t)(value - (UINT64_C(1) << 63U));
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_read_size32(tinypy_marshal_parser_t *parser, size_t *out_size, const char *description) {
    int32_t value;
    size_t size_offset = parser->offset;

    if (!__tinypy_marshal_read_i32(parser, &value)) {
        return TINYPY_FALSE;
    }
    if (value < 0) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_INVALID_SIZE,
            size_offset,
            description);
        return TINYPY_FALSE;
    }
    *out_size = (size_t)value;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_utf8_validate(const uint8_t *bytes, size_t size, size_t *out_code_points) {
    size_t index = 0U;
    size_t count = 0U;

    while (index != size) {
        uint32_t code_point;
        size_t continuation_count;
        uint8_t first = bytes[index];

        if (first <= 0x7fU) {
            code_point = first;
            continuation_count = 0U;
        }
        else if (first >= 0xc2U && first <= 0xdfU) {
            code_point = (uint32_t)(first & 0x1fU);
            continuation_count = 1U;
        }
        else if (first >= 0xe0U && first <= 0xefU) {
            code_point = (uint32_t)(first & 0x0fU);
            continuation_count = 2U;
        }
        else if (first >= 0xf0U && first <= 0xf4U) {
            code_point = (uint32_t)(first & 0x07U);
            continuation_count = 3U;
        }
        else {
            return TINYPY_FALSE;
        }

        if (continuation_count > size - index - 1U) {
            return TINYPY_FALSE;
        }
        if (continuation_count != 0U) {
            size_t part;
            for (part = 1U; part <= continuation_count; ++part) {
                uint8_t continuation = bytes[index + part];
                if ((continuation & 0xc0U) != 0x80U) {
                    return TINYPY_FALSE;
                }
                code_point = (code_point << 6U) | (uint32_t)(continuation & 0x3fU);
            }
        }

        if ((continuation_count == 2U && code_point < UINT32_C(0x800)) || (continuation_count == 3U && code_point < UINT32_C(0x10000)) || (code_point >= UINT32_C(0xd800) && code_point <= UINT32_C(0xdfff)) || code_point > UINT32_C(0x10ffff)) {
            return TINYPY_FALSE;
        }

        index += continuation_count + 1U;
        count += 1U;
    }

    *out_code_points = count;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static double __tinypy_marshal_double_from_bits(uint64_t bits) {
    double result = 0.0;
    (void)memcpy(&result, &bits, sizeof(result));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_supports_binary64(void) {
    return sizeof(double) == 8U && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024
               ? TINYPY_TRUE
               : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_binary_double(const uint8_t *bytes, double *out_value) {
    uint64_t bits = 0U;
    size_t index;

    if (__tinypy_marshal_supports_binary64() == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index != 8U; ++index) {
        bits |= (uint64_t)bytes[index] << (index * 8U);
    }
    *out_value = __tinypy_marshal_double_from_bits(bits);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_ascii_equal(const uint8_t *bytes, size_t size, const char *text) {
    size_t index;
    size_t text_size = __tinypy_marshal_cstring_size(text);
    if (size != text_size) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index != size; ++index) {
        uint8_t current = bytes[index];
        uint8_t expected = (uint8_t)text[index];
        if (current >= (uint8_t)'A' && current <= (uint8_t)'Z') {
            current = (uint8_t)(current + ((uint8_t)'a' - (uint8_t)'A'));
        }
        if (current != expected) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_text_double(const uint8_t *bytes, size_t size, double *out_value) {
    size_t index = 0U;
    int32_t negative = 0;
    int32_t saw_digit = 0;
    int32_t exponent_negative = 0;
    int32_t exponent_value = 0;
    long double value = 0.0L;
    long double fraction = 0.1L;
    int32_t in_fraction = 0;

    if (size == 0U) {
        return TINYPY_FALSE;
    }
    if (bytes[index] == (uint8_t)'+' || bytes[index] == (uint8_t)'-') {
        negative = bytes[index] == (uint8_t)'-';
        index += 1U;
        if (index == size) {
            return TINYPY_FALSE;
        }
    }

    if (__tinypy_marshal_ascii_equal(bytes + index, size - index, "inf") || __tinypy_marshal_ascii_equal(bytes + index, size - index, "infinity")) {
        uint64_t bits = UINT64_C(0x7ff0000000000000);
        if (negative) {
            bits |= UINT64_C(0x8000000000000000);
        }
        *out_value = __tinypy_marshal_double_from_bits(bits);
        return TINYPY_TRUE;
    }
    if (__tinypy_marshal_ascii_equal(bytes + index, size - index, "nan")) {
        uint64_t bits = UINT64_C(0x7ff8000000000000);
        if (negative) {
            bits |= UINT64_C(0x8000000000000000);
        }
        *out_value = __tinypy_marshal_double_from_bits(bits);
        return TINYPY_TRUE;
    }

    while (index != size) {
        uint8_t current = bytes[index];
        if (current >= (uint8_t)'0' && current <= (uint8_t)'9') {
            int32_t digit = (int32_t)(current - (uint8_t)'0');
            saw_digit = 1;
            if (in_fraction) {
                value += (long double)digit * fraction;
                fraction *= 0.1L;
            }
            else {
                value = value * 10.0L + (long double)digit;
            }
            index += 1U;
            continue;
        }
        if (current == (uint8_t)'.' && !in_fraction) {
            in_fraction = 1;
            index += 1U;
            continue;
        }
        break;
    }
    if (!saw_digit) {
        return TINYPY_FALSE;
    }

    if (index != size && (bytes[index] == (uint8_t)'e' || bytes[index] == (uint8_t)'E')) {
        int32_t saw_exponent_digit = 0;
        index += 1U;
        if (index != size && (bytes[index] == (uint8_t)'+' || bytes[index] == (uint8_t)'-')) {
            exponent_negative = bytes[index] == (uint8_t)'-';
            index += 1U;
        }
        while (index != size && bytes[index] >= (uint8_t)'0' && bytes[index] <= (uint8_t)'9') {
            int32_t digit = (int32_t)(bytes[index] - (uint8_t)'0');
            saw_exponent_digit = 1;
            if (exponent_value < 10000) {
                exponent_value = exponent_value * 10 + digit;
            }
            index += 1U;
        }
        if (!saw_exponent_digit) {
            return TINYPY_FALSE;
        }
    }
    if (index != size) {
        return TINYPY_FALSE;
    }

    if (exponent_value > 4096) {
        value = exponent_negative ? 0.0L : (long double)DBL_MAX * 2.0L;
    }
    else {
        while (exponent_value != 0) {
            value = exponent_negative ? value * 0.1L : value * 10.0L;
            exponent_value -= 1;
        }
    }
    *out_value = (double)(negative ? -value : value);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_intern_append(tinypy_marshal_parser_t *parser, tinypy_marshal_object_t *object) {
    tinypy_marshal_document_t *document = parser->document;
    tinypy_marshal_intern_chunk_t *chunk = document->intern_last;

    if (chunk == NULL || chunk->count == TINYPY_MARSHAL_INTERN_CHUNK_SIZE) {
        tinypy_marshal_intern_chunk_t *new_chunk =
            (tinypy_marshal_intern_chunk_t *)__tinypy_marshal_graph_allocate(
                parser,
                sizeof(*new_chunk));
        if (new_chunk == NULL) {
            return TINYPY_FALSE;
        }
        if (chunk == NULL) {
            document->intern_first = new_chunk;
        }
        else {
            chunk->next = new_chunk;
        }
        document->intern_last = new_chunk;
        chunk = new_chunk;
    }

    chunk->items[chunk->count] = object;
    chunk->count += 1U;
    document->intern_count += 1U;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_object_t *__tinypy_marshal_intern_get(const tinypy_marshal_document_t *document, size_t index) {
    const tinypy_marshal_intern_chunk_t *chunk = document->intern_first;

    while (chunk != NULL) {
        if (index < chunk->count) {
            return chunk->items[index];
        }
        index -= chunk->count;
        chunk = chunk->next;
    }
    return NULL;
}

static tinypy_marshal_object_t *__tinypy_marshal_parse_object(tinypy_marshal_parser_t *parser, tinypy_bool_t *out_is_null);

//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_object_t *__tinypy_marshal_parse_string(tinypy_marshal_parser_t *parser, uint8_t wire_type, tinypy_bool_t unicode_value) {
    size_t size;
    size_t size_offset = parser->offset;
    const uint8_t *source;
    uint8_t *copy;
    size_t code_points = 0U;

    if (!__tinypy_marshal_read_size32(
            parser,
            &size,
            unicode_value ? "negative unicode byte size" : "negative string byte size")) {
        return NULL;
    }
    if (size > parser->document->limits.max_string_bytes) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_STRING_LIMIT,
            size_offset,
            "marshal string-byte limit exceeded");
        return NULL;
    }
    if (!__tinypy_marshal_take(parser, size, &source)) {
        return NULL;
    }
    if (unicode_value && !__tinypy_marshal_utf8_validate(source, size, &code_points)) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_INVALID_UTF8,
            size_offset + 4U,
            "invalid UTF-8 in marshal unicode object");
        return NULL;
    }

    tinypy_marshal_object_t *object = __tinypy_marshal_object_allocate(
        parser,
        unicode_value ? TINYPY_MARSHAL_TYPE_UNICODE : TINYPY_MARSHAL_TYPE_BYTES,
        wire_type);
    if (object == NULL) {
        return NULL;
    }
    if (size == SIZE_MAX) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_BYTE_LIMIT,
            size_offset,
            "marshal string allocation size overflow");
        return NULL;
    }
    copy = (uint8_t *)__tinypy_marshal_graph_allocate(parser, size + 1U);
    if (copy == NULL) {
        return NULL;
    }
    if (size != 0U) {
        (void)memcpy(copy, source, size);
    }
    copy[size] = 0U;
    object->as.string_value.bytes = copy;
    object->as.string_value.size = size;
    object->as.string_value.code_points = code_points;
    object->as.string_value.interned = wire_type == TYPE_INTERNED;

    if (wire_type == TYPE_INTERNED && !__tinypy_marshal_intern_append(parser, object)) {
        return NULL;
    }
    return object;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_object_t *__tinypy_marshal_parse_long(tinypy_marshal_parser_t *parser, uint8_t wire_type) {
    int32_t signed_count;
    size_t count;
    size_t bytes_size;
    size_t size_offset = parser->offset;
    uint16_t *digits = NULL;
    size_t index;

    if (!__tinypy_marshal_read_i32(parser, &signed_count)) {
        return NULL;
    }
    if (signed_count == INT32_MIN) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_INVALID_LONG,
            size_offset,
            "marshal long digit count is out of range");
        return NULL;
    }
    count = (size_t)(signed_count < 0 ? -(int64_t)signed_count : signed_count);
    if (count > parser->document->limits.max_container_items) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_CONTAINER_LIMIT,
            size_offset,
            "marshal long digit-count limit exceeded");
        return NULL;
    }
    if (!__tinypy_marshal_multiply_size(count, sizeof(*digits), &bytes_size)) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_BYTE_LIMIT,
            size_offset,
            "marshal long allocation size overflow");
        return NULL;
    }

    tinypy_marshal_object_t *object = __tinypy_marshal_object_allocate(parser, TINYPY_MARSHAL_TYPE_LONG, wire_type);
    if (object == NULL) {
        return NULL;
    }
    if (bytes_size != 0U) {
        digits = (uint16_t *)__tinypy_marshal_graph_allocate(parser, bytes_size);
        if (digits == NULL) {
            return NULL;
        }
    }
    for (index = 0U; index != count; ++index) {
        if (!__tinypy_marshal_read_u16(parser, &digits[index])) {
            return NULL;
        }
        if (digits[index] >= UINT16_C(32768)) {
            __tinypy_marshal_parser_fail(
                parser,
                TINYPY_MARSHAL_INVALID_LONG,
                parser->offset - 2U,
                "marshal long digit is outside base 2^15");
            return NULL;
        }
    }
    if (count != 0U && digits[count - 1U] == 0U) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_INVALID_LONG,
            parser->offset - 2U,
            "marshal long has an unnormalized top digit");
        return NULL;
    }
    object->as.long_value.sign = signed_count < 0 ? -1 : (signed_count > 0 ? 1 : 0);
    object->as.long_value.count = count;
    object->as.long_value.digits = digits;
    return object;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_object_t *__tinypy_marshal_parse_sequence(tinypy_marshal_parser_t *parser, uint8_t wire_type, tinypy_marshal_type_e type) {
    size_t count;
    size_t bytes_size;
    size_t size_offset = parser->offset;
    size_t index;

    if (!__tinypy_marshal_read_size32(parser, &count, "negative marshal container size")) {
        return NULL;
    }
    if (count > parser->document->limits.max_container_items) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_CONTAINER_LIMIT,
            size_offset,
            "marshal container-item limit exceeded");
        return NULL;
    }
    if (!__tinypy_marshal_multiply_size(count, sizeof(tinypy_marshal_object_t *), &bytes_size)) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_BYTE_LIMIT,
            size_offset,
            "marshal container allocation size overflow");
        return NULL;
    }

    tinypy_marshal_object_t *object = __tinypy_marshal_object_allocate(parser, type, wire_type);
    if (object == NULL) {
        return NULL;
    }
    object->as.sequence_value.count = count;
    if (bytes_size != 0U) {
        object->as.sequence_value.items =
            (tinypy_marshal_object_t **)__tinypy_marshal_graph_allocate(parser, bytes_size);
        if (object->as.sequence_value.items == NULL) {
            return NULL;
        }
    }

    for (index = 0U; index != count; ++index) {
        tinypy_bool_t is_null = TINYPY_FALSE;
        tinypy_marshal_object_t *item = __tinypy_marshal_parse_object(parser, &is_null);
        if (parser->result != TINYPY_MARSHAL_OK) {
            return NULL;
        }
        if (is_null) {
            __tinypy_marshal_parser_fail(
                parser,
                TINYPY_MARSHAL_NULL_OUTSIDE_DICT,
                parser->offset - 1U,
                "TYPE_NULL is valid only as a dictionary terminator");
            return NULL;
        }
        object->as.sequence_value.items[index] = item;
    }
    return object;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_object_t *__tinypy_marshal_parse_dict(tinypy_marshal_parser_t *parser, uint8_t wire_type) {
    tinypy_marshal_object_t *object =
        __tinypy_marshal_object_allocate(parser, TINYPY_MARSHAL_TYPE_DICT, wire_type);

    if (object == NULL) {
        return NULL;
    }
    for (;;) {
        tinypy_bool_t key_is_null = TINYPY_FALSE;
        tinypy_bool_t value_is_null = TINYPY_FALSE;
        tinypy_marshal_object_t *key = __tinypy_marshal_parse_object(parser, &key_is_null);
        tinypy_marshal_object_t *value;
        tinypy_marshal_dict_entry_t *entry;

        if (parser->result != TINYPY_MARSHAL_OK) {
            return NULL;
        }
        if (key_is_null) {
            return object;
        }
        if (object->as.dict_value.count >=
            parser->document->limits.max_container_items) {
            __tinypy_marshal_parser_fail(
                parser,
                TINYPY_MARSHAL_CONTAINER_LIMIT,
                parser->offset,
                "marshal dictionary-item limit exceeded");
            return NULL;
        }
        value = __tinypy_marshal_parse_object(parser, &value_is_null);
        if (parser->result != TINYPY_MARSHAL_OK) {
            return NULL;
        }
        if (value_is_null) {
            __tinypy_marshal_parser_fail(
                parser,
                TINYPY_MARSHAL_NULL_OUTSIDE_DICT,
                parser->offset - 1U,
                "TYPE_NULL is only a dictionary key terminator");
            return NULL;
        }

        entry = (tinypy_marshal_dict_entry_t *)__tinypy_marshal_graph_allocate(
            parser,
            sizeof(*entry));
        if (entry == NULL) {
            return NULL;
        }
        entry->key = key;
        entry->value = value;
        if (object->as.dict_value.last == NULL) {
            object->as.dict_value.first = entry;
        }
        else {
            object->as.dict_value.last->next = entry;
        }
        object->as.dict_value.last = entry;
        object->as.dict_value.count += 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_tuple_of_bytes(const tinypy_marshal_object_t *object) {
    size_t index;
    if (object == NULL || object->type != TINYPY_MARSHAL_TYPE_TUPLE) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index != object->as.sequence_value.count; ++index) {
        const tinypy_marshal_object_t *item = object->as.sequence_value.items[index];
        if (item == NULL || item->type != TINYPY_MARSHAL_TYPE_BYTES) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_code_fields_valid(const tinypy_marshal_code_t *code) {
    tinypy_bool_t return_value_1 = code->argcount >= 0 && code->nlocals >= 0 && code->stacksize >= 0 && code->firstlineno >= 0 && code->bytecode != NULL && code->bytecode->type == TINYPY_MARSHAL_TYPE_BYTES && code->consts != NULL && code->consts->type == TINYPY_MARSHAL_TYPE_TUPLE && __tinypy_marshal_tuple_of_bytes(code->names) && __tinypy_marshal_tuple_of_bytes(code->varnames) && __tinypy_marshal_tuple_of_bytes(code->freevars) && __tinypy_marshal_tuple_of_bytes(code->cellvars) && code->filename != NULL && code->filename->type == TINYPY_MARSHAL_TYPE_BYTES && code->name != NULL && code->name->type == TINYPY_MARSHAL_TYPE_BYTES && code->lnotab != NULL && code->lnotab->type == TINYPY_MARSHAL_TYPE_BYTES;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_object_t *__tinypy_marshal_parse_code(tinypy_marshal_parser_t *parser, uint8_t wire_type, size_t code_offset) {
    tinypy_marshal_object_t *object =
        __tinypy_marshal_object_allocate(parser, TINYPY_MARSHAL_TYPE_CODE, wire_type);
    tinypy_bool_t is_null = TINYPY_FALSE;

    if (object == NULL) {
        return NULL;
    }
    tinypy_marshal_code_t *code = &object->as.code_value;
    code->abi_version = TINYPY_MARSHAL_ABI_VERSION;
    code->struct_size = (uint32_t)sizeof(*code);

    if (!__tinypy_marshal_read_i32(parser, &code->argcount) || !__tinypy_marshal_read_i32(parser, &code->nlocals) || !__tinypy_marshal_read_i32(parser, &code->stacksize) || !__tinypy_marshal_read_i32(parser, &code->flags)) {
        return NULL;
    }

#define TINYPY_MARSHAL_PARSE_CODE_OBJECT(field)                        \
    do { \
        code->field = __tinypy_marshal_parse_object(parser, &is_null); \
        if (parser->result != TINYPY_MARSHAL_OK) { \
            return NULL;                                               \
        }                                                              \
        if (is_null) { \
            __tinypy_marshal_parser_fail(                              \
                parser,                                                \
                TINYPY_MARSHAL_NULL_OUTSIDE_DICT,                      \
                parser->offset - 1U,                                   \
                "TYPE_NULL is invalid in a code object");              \
            return NULL;                                               \
        }                                                              \
    } while (0)

    TINYPY_MARSHAL_PARSE_CODE_OBJECT(bytecode);
    TINYPY_MARSHAL_PARSE_CODE_OBJECT(consts);
    TINYPY_MARSHAL_PARSE_CODE_OBJECT(names);
    TINYPY_MARSHAL_PARSE_CODE_OBJECT(varnames);
    TINYPY_MARSHAL_PARSE_CODE_OBJECT(freevars);
    TINYPY_MARSHAL_PARSE_CODE_OBJECT(cellvars);
    TINYPY_MARSHAL_PARSE_CODE_OBJECT(filename);
    TINYPY_MARSHAL_PARSE_CODE_OBJECT(name);
    if (!__tinypy_marshal_read_i32(parser, &code->firstlineno)) {
        return NULL;
    }
    TINYPY_MARSHAL_PARSE_CODE_OBJECT(lnotab);

#undef TINYPY_MARSHAL_PARSE_CODE_OBJECT

    if (!__tinypy_marshal_code_fields_valid(code)) {
        parser->current_wire_type = wire_type;
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_INVALID_CODE,
            code_offset,
            "marshal code object has invalid field types or counts");
        return NULL;
    }
    return object;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_object_t *__tinypy_marshal_parse_object(tinypy_marshal_parser_t *parser, tinypy_bool_t *out_is_null) {
    size_t object_offset = parser->offset;
    uint8_t wire_type;
    tinypy_marshal_object_t *object = NULL;

    *out_is_null = 0;
    parser->depth += 1U;
    if (parser->depth > parser->document->limits.max_depth) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_DEPTH_LIMIT,
            object_offset,
            "marshal nesting-depth limit exceeded");
        parser->depth -= 1U;
        return NULL;
    }
    if (!__tinypy_marshal_read_u8(parser, &wire_type)) {
        parser->depth -= 1U;
        return NULL;
    }
    parser->current_wire_type = wire_type;
    if (parser->objects_seen >= parser->document->limits.max_objects) {
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_OBJECT_LIMIT,
            object_offset,
            "marshal object-count limit exceeded");
        parser->depth -= 1U;
        return NULL;
    }
    parser->objects_seen += 1U;

    switch (wire_type) {
    case TYPE_NULL:
        *out_is_null = 1;
        break;
    case TYPE_NONE:
        object = __tinypy_marshal_object_allocate(parser, TINYPY_MARSHAL_TYPE_NONE, wire_type);
        break;
    case TYPE_FALSE:
    case TYPE_TRUE:
        object = __tinypy_marshal_object_allocate(parser, TINYPY_MARSHAL_TYPE_BOOL, wire_type);
        if (object != NULL) {
            object->as.boolean_value = wire_type == TYPE_TRUE;
        }
        break;
    case TYPE_STOPITER:
        object = __tinypy_marshal_object_allocate(
            parser,
            TINYPY_MARSHAL_TYPE_STOP_ITERATION,
            wire_type);
        break;
    case TYPE_ELLIPSIS:
        object = __tinypy_marshal_object_allocate(parser, TINYPY_MARSHAL_TYPE_ELLIPSIS, wire_type);
        break;
    case TYPE_INTEGER: {
        int32_t value;
        if (__tinypy_marshal_read_i32(parser, &value)) {
            object = __tinypy_marshal_object_allocate(parser, TINYPY_MARSHAL_TYPE_INTEGER, wire_type);
            if (object != NULL) {
                object->as.integer_value = (int64_t)value;
            }
        }
    }
    break;
    case TYPE_INTEGER64: {
        int64_t value;
        if (__tinypy_marshal_read_i64(parser, &value)) {
            object = __tinypy_marshal_object_allocate(parser, TINYPY_MARSHAL_TYPE_INTEGER, wire_type);
            if (object != NULL) {
                object->as.integer_value = value;
            }
        }
    }
    break;
    case TYPE_LONG:
        object = __tinypy_marshal_parse_long(parser, wire_type);
        break;
    case TYPE_FLOAT:
    case TYPE_COMPLEX: {
        uint8_t text_size;
        const uint8_t *text;
        const uint8_t *real_text;
        const uint8_t *imaginary_text = NULL;
        size_t real_text_size;
        size_t imaginary_text_size = 0U;
        double real;
        double imaginary = 0.0;
        if (!__tinypy_marshal_read_u8(parser, &text_size) || !__tinypy_marshal_take(parser, (size_t)text_size, &text) || !__tinypy_marshal_text_double(text, (size_t)text_size, &real)) {
            if (parser->result == TINYPY_MARSHAL_OK) {
                __tinypy_marshal_parser_fail(
                    parser,
                    TINYPY_MARSHAL_INVALID_FLOAT,
                    object_offset,
                    "invalid textual marshal float");
            }
            break;
        }
        real_text = text;
        real_text_size = (size_t)text_size;
        if (wire_type == TYPE_COMPLEX) {
            if (!__tinypy_marshal_read_u8(parser, &text_size) || !__tinypy_marshal_take(parser, (size_t)text_size, &text) || !__tinypy_marshal_text_double(text, (size_t)text_size, &imaginary)) {
                if (parser->result == TINYPY_MARSHAL_OK) {
                    __tinypy_marshal_parser_fail(
                        parser,
                        TINYPY_MARSHAL_INVALID_FLOAT,
                        object_offset,
                        "invalid textual marshal complex value");
                }
                break;
            }
            imaginary_text = text;
            imaginary_text_size = (size_t)text_size;
        }
        object = __tinypy_marshal_object_allocate(
            parser,
            wire_type == TYPE_FLOAT ? TINYPY_MARSHAL_TYPE_FLOAT : TINYPY_MARSHAL_TYPE_COMPLEX,
            wire_type);
        if (object != NULL) {
            if (wire_type == TYPE_FLOAT) {
                object->as.float_value.value = real;
                object->as.float_value.text = __tinypy_marshal_graph_copy(
                    parser,
                    real_text,
                    real_text_size);
                object->as.float_value.text_size = real_text_size;
            }
            else {
                object->as.complex_value.real = real;
                object->as.complex_value.imaginary = imaginary;
                object->as.complex_value.real_text = __tinypy_marshal_graph_copy(
                    parser,
                    real_text,
                    real_text_size);
                object->as.complex_value.real_text_size = real_text_size;
                if (parser->result == TINYPY_MARSHAL_OK) {
                    object->as.complex_value.imaginary_text =
                        __tinypy_marshal_graph_copy(
                            parser,
                            imaginary_text,
                            imaginary_text_size);
                    object->as.complex_value.imaginary_text_size =
                        imaginary_text_size;
                }
            }
        }
    }
    break;
    case TYPE_BINARY_FLOAT:
    case TYPE_BINARY_COMPLEX: {
        const uint8_t *binary;
        double real;
        double imaginary = 0.0;
        if (!__tinypy_marshal_take(parser, 8U, &binary)) {
            break;
        }
        if (!__tinypy_marshal_binary_double(binary, &real)) {
            __tinypy_marshal_parser_fail(
                parser,
                TINYPY_MARSHAL_INVALID_FLOAT,
                object_offset,
                "host does not support IEEE-754 binary64 marshal values");
            break;
        }
        if (wire_type == TYPE_BINARY_COMPLEX) {
            if (!__tinypy_marshal_take(parser, 8U, &binary)) {
                break;
            }
            if (!__tinypy_marshal_binary_double(binary, &imaginary)) {
                __tinypy_marshal_parser_fail(
                    parser,
                    TINYPY_MARSHAL_INVALID_FLOAT,
                    object_offset,
                    "host does not support IEEE-754 binary64 marshal values");
                break;
            }
        }
        object = __tinypy_marshal_object_allocate(
            parser,
            wire_type == TYPE_BINARY_FLOAT ? TINYPY_MARSHAL_TYPE_FLOAT : TINYPY_MARSHAL_TYPE_COMPLEX,
            wire_type);
        if (object != NULL) {
            if (wire_type == TYPE_BINARY_FLOAT) {
                object->as.float_value.value = real;
            }
            else {
                object->as.complex_value.real = real;
                object->as.complex_value.imaginary = imaginary;
            }
        }
    }
    break;
    case TYPE_STRING:
    case TYPE_INTERNED:
        object = __tinypy_marshal_parse_string(parser, wire_type, 0);
        break;
    case TYPE_UNICODE:
        object = __tinypy_marshal_parse_string(parser, wire_type, 1);
        break;
    case TYPE_STRINGREF: {
        int32_t index;
        size_t ref_offset = parser->offset;
        if (!__tinypy_marshal_read_i32(parser, &index)) {
            break;
        }
        if (index < 0 || (size_t)index >= parser->document->intern_count) {
            __tinypy_marshal_parser_fail(
                parser,
                TINYPY_MARSHAL_INVALID_STRING_REF,
                ref_offset,
                "marshal interned-string reference is out of range");
            break;
        }
        object = __tinypy_marshal_intern_get(parser->document, (size_t)index);
    }
    break;
    case TYPE_TUPLE:
        object = __tinypy_marshal_parse_sequence(parser, wire_type, TINYPY_MARSHAL_TYPE_TUPLE);
        break;
    case TYPE_LIST:
        object = __tinypy_marshal_parse_sequence(parser, wire_type, TINYPY_MARSHAL_TYPE_LIST);
        break;
    case TYPE_SET:
        object = __tinypy_marshal_parse_sequence(parser, wire_type, TINYPY_MARSHAL_TYPE_SET);
        break;
    case TYPE_FROZENSET:
        object = __tinypy_marshal_parse_sequence(parser, wire_type, TINYPY_MARSHAL_TYPE_FROZENSET);
        break;
    case TYPE_DICT:
        object = __tinypy_marshal_parse_dict(parser, wire_type);
        break;
    case TYPE_CODE:
        object = __tinypy_marshal_parse_code(parser, wire_type, object_offset);
        break;
    default:
        __tinypy_marshal_parser_fail(
            parser,
            TINYPY_MARSHAL_UNKNOWN_TYPE,
            object_offset,
            "unknown CPython 2.7 marshal type code");
        break;
    }

    parser->depth -= 1U;
    return object;
}
//////////////////////////////////////////////////////////////////////////
tinypy_marshal_result_e tinypy_marshal_read_v2(const void *bytes, size_t size, const tinypy_allocator_t *allocator, const tinypy_marshal_limits_t *limits, tinypy_marshal_document_t **out_document, tinypy_marshal_error_t *out_error) {
    tinypy_marshal_limits_t effective_limits;
    tinypy_marshal_parser_t parser;
    tinypy_bool_t root_is_null = TINYPY_FALSE;

    __tinypy_marshal_set_error_direct(
        out_error,
        TINYPY_MARSHAL_OK,
        0U,
        0U,
        "ok");
    *out_document = NULL;
    if (allocator->abi_version != TINYPY_ABI_VERSION || (size_t)allocator->struct_size <
            offsetof(tinypy_allocator_t, deallocate) + sizeof(allocator->deallocate)) {
        __tinypy_marshal_set_error_direct(
            out_error,
            TINYPY_MARSHAL_ABI_MISMATCH,
            0U,
            0U,
            "marshal allocator ABI mismatch");
        return TINYPY_MARSHAL_ABI_MISMATCH;
    }

    if (limits == NULL) {
        tinypy_marshal_limits_init(&effective_limits);
    }
    else {
        if (!__tinypy_marshal_limits_valid(limits)) {
            __tinypy_marshal_set_error_direct(
                out_error,
                limits->abi_version == TINYPY_MARSHAL_ABI_VERSION
                    ? TINYPY_MARSHAL_INVALID_ARGUMENT
                    : TINYPY_MARSHAL_ABI_MISMATCH,
                0U,
                0U,
                "invalid marshal limits ABI or zero limit");
            return limits->abi_version == TINYPY_MARSHAL_ABI_VERSION
                       ? TINYPY_MARSHAL_INVALID_ARGUMENT
                       : TINYPY_MARSHAL_ABI_MISMATCH;
        }
        effective_limits = *limits;
    }
    if (size > effective_limits.max_input_bytes) {
        __tinypy_marshal_set_error_direct(
            out_error,
            TINYPY_MARSHAL_BYTE_LIMIT,
            0U,
            0U,
            "marshal input-byte limit exceeded");
        return TINYPY_MARSHAL_BYTE_LIMIT;
    }
    if (sizeof(tinypy_marshal_document_t) > effective_limits.max_allocated_bytes) {
        __tinypy_marshal_set_error_direct(
            out_error,
            TINYPY_MARSHAL_BYTE_LIMIT,
            0U,
            0U,
            "marshal allocated-byte limit is smaller than the document");
        return TINYPY_MARSHAL_BYTE_LIMIT;
    }

    tinypy_marshal_document_t *document = (tinypy_marshal_document_t *)allocator->allocate(
        allocator->user_data,
        sizeof(*document),
        TINYPY_MARSHAL_ALIGNMENT,
        TINYPY_MARSHAL_ALLOC_TAG_DOCUMENT);

    (void)memset(document, 0, sizeof(*document));
    document->state = TINYPY_MARSHAL_DOCUMENT_LIVE;
    document->allocator = *allocator;
    document->limits = effective_limits;
    document->input_size = size;
    document->allocated_bytes = sizeof(*document);

    (void)memset(&parser, 0, sizeof(parser));
    parser.bytes = (const uint8_t *)bytes;
    parser.size = size;
    parser.document = document;
    parser.error = out_error;
    parser.result = TINYPY_MARSHAL_OK;
    document->root = __tinypy_marshal_parse_object(&parser, &root_is_null);

    if (parser.result == TINYPY_MARSHAL_OK && root_is_null) {
        __tinypy_marshal_parser_fail(
            &parser,
            TINYPY_MARSHAL_NULL_OUTSIDE_DICT,
            0U,
            "TYPE_NULL cannot be a top-level marshal object");
    }
    if (parser.result == TINYPY_MARSHAL_OK && parser.offset != size) {
        __tinypy_marshal_parser_fail(
            &parser,
            TINYPY_MARSHAL_TRAILING_DATA,
            parser.offset,
            "trailing bytes follow the top-level marshal object");
    }
    if (parser.result != TINYPY_MARSHAL_OK) {
        tinypy_marshal_document_destroy(document);
        return parser.result;
    }

    document->object_count = parser.objects_seen;
    *out_document = document;
    return TINYPY_MARSHAL_OK;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_marshal_writer_fail(tinypy_marshal_writer_t *writer, tinypy_marshal_result_e result, const char *message) {
    if (writer->result != TINYPY_MARSHAL_OK) {
        return;
    }
    writer->result = result;
    __tinypy_marshal_set_error_direct(
        writer->error,
        result,
        writer->offset,
        writer->current_wire_type,
        message);
    if (writer->error != NULL) {
        writer->error->object = writer->current_object;
        writer->error->object_index = writer->current_object_index;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_writer_put(tinypy_marshal_writer_t *writer, const uint8_t *bytes, size_t size) {
    if (size > writer->max_output_bytes - writer->offset) {
        __tinypy_marshal_writer_fail(
            writer,
            TINYPY_MARSHAL_OUTPUT_LIMIT,
            "marshal output-byte limit exceeded");
        return TINYPY_FALSE;
    }
    if (writer->buffer != NULL) {
        if (size > writer->capacity - writer->offset) {
            __tinypy_marshal_writer_fail(
                writer,
                TINYPY_MARSHAL_BUFFER_TOO_SMALL,
                "marshal output buffer is too small");
            return TINYPY_FALSE;
        }
        if (size != 0U) {
            (void)memcpy(writer->buffer + writer->offset, bytes, size);
        }
    }
    writer->offset += size;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_writer_byte(tinypy_marshal_writer_t *writer, uint8_t value) {
    tinypy_bool_t return_value_1 = __tinypy_marshal_writer_put(writer, &value, 1U);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_writer_u16(tinypy_marshal_writer_t *writer, uint16_t value) {
    uint8_t bytes[2];
    bytes[0] = (uint8_t)(value & UINT16_C(0xff));
    bytes[1] = (uint8_t)((value >> 8U) & UINT16_C(0xff));
    tinypy_bool_t return_value_1 = __tinypy_marshal_writer_put(writer, bytes, sizeof(bytes));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_marshal_i32_as_u32(int32_t value) {
    if (value >= 0) {
        return (uint32_t)value;
    }
    return UINT32_MAX - (uint32_t)(-(int64_t)(value + 1));
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_writer_i32(tinypy_marshal_writer_t *writer, int32_t value) {
    uint32_t bits = __tinypy_marshal_i32_as_u32(value);
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(bits & UINT32_C(0xff));
    bytes[1] = (uint8_t)((bits >> 8U) & UINT32_C(0xff));
    bytes[2] = (uint8_t)((bits >> 16U) & UINT32_C(0xff));
    bytes[3] = (uint8_t)((bits >> 24U) & UINT32_C(0xff));
    tinypy_bool_t return_value_1 = __tinypy_marshal_writer_put(writer, bytes, sizeof(bytes));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static uint64_t __tinypy_marshal_i64_as_u64(int64_t value) {
    if (value >= 0) {
        return (uint64_t)value;
    }
    return UINT64_MAX - (uint64_t)(-(value + 1));
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_writer_i64(tinypy_marshal_writer_t *writer, int64_t value) {
    uint64_t bits = __tinypy_marshal_i64_as_u64(value);
    uint8_t bytes[8];
    size_t index;
    for (index = 0U; index != sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)((bits >> (index * 8U)) & UINT64_C(0xff));
    }
    tinypy_bool_t return_value_1 = __tinypy_marshal_writer_put(writer, bytes, sizeof(bytes));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_writer_size32(tinypy_marshal_writer_t *writer, size_t size) {
    if (size > (size_t)INT32_MAX) {
        __tinypy_marshal_writer_fail(
            writer,
            TINYPY_MARSHAL_INVALID_GRAPH,
            "marshal graph contains a size above the v2 signed-32-bit limit");
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = __tinypy_marshal_writer_i32(writer, (int32_t)size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_writer_binary_double(tinypy_marshal_writer_t *writer, double value) {
    uint64_t bits;
    uint8_t bytes[8];
    size_t index;

    if (__tinypy_marshal_supports_binary64() == TINYPY_FALSE) {
        __tinypy_marshal_writer_fail(
            writer,
            TINYPY_MARSHAL_INVALID_FLOAT,
            "host does not support IEEE-754 binary64 marshal values");
        return TINYPY_FALSE;
    }
    (void)memcpy(&bits, &value, sizeof(bits));
    for (index = 0U; index != sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)((bits >> (index * 8U)) & UINT64_C(0xff));
    }
    tinypy_bool_t return_value_1 = __tinypy_marshal_writer_put(writer, bytes, sizeof(bytes));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_intern_index(const tinypy_marshal_document_t *document, const tinypy_marshal_object_t *object, size_t *out_index) {
    const tinypy_marshal_intern_chunk_t *chunk = document->intern_first;
    size_t base = 0U;

    while (chunk != NULL) {
        size_t index;
        for (index = 0U; index != chunk->count; ++index) {
            if (chunk->items[index] == object) {
                *out_index = base + index;
                return TINYPY_TRUE;
            }
        }
        base += chunk->count;
        chunk = chunk->next;
    }
    return TINYPY_FALSE;
}

static tinypy_bool_t __tinypy_marshal_write_object(tinypy_marshal_writer_t *writer, const tinypy_marshal_object_t *object);

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_write_bytes_object(tinypy_marshal_writer_t *writer, const tinypy_marshal_object_t *object) {
    const uint8_t *bytes = object->as.string_value.bytes;
    size_t size = object->as.string_value.size;

    if (object->as.string_value.interned) {
        size_t index;
        if (!__tinypy_marshal_intern_index(writer->document, object, &index) || index > (size_t)INT32_MAX) {
            __tinypy_marshal_writer_fail(
                writer,
                TINYPY_MARSHAL_INVALID_GRAPH,
                "interned marshal string is absent from the document table");
            return TINYPY_FALSE;
        }
        if (index < writer->emitted_intern_count) {
            tinypy_bool_t return_value_1 = __tinypy_marshal_writer_byte(writer, TYPE_STRINGREF) && __tinypy_marshal_writer_i32(writer, (int32_t)index);
            return return_value_1;
        }
        if (index != writer->emitted_intern_count) {
            __tinypy_marshal_writer_fail(
                writer,
                TINYPY_MARSHAL_INVALID_GRAPH,
                "marshal interned strings are encountered out of table order");
            return TINYPY_FALSE;
        }
        writer->emitted_intern_count += 1U;
        if (!__tinypy_marshal_writer_byte(writer, TYPE_INTERNED)) {
            return TINYPY_FALSE;
        }
    }
    else if (!__tinypy_marshal_writer_byte(writer, TYPE_STRING)) {
        return TINYPY_FALSE;
    }

    tinypy_bool_t return_value_2 = __tinypy_marshal_writer_size32(writer, size) && __tinypy_marshal_writer_put(writer, bytes, size);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_write_sequence(tinypy_marshal_writer_t *writer, const tinypy_marshal_object_t *object, uint8_t wire_type) {
    size_t index;
    if (!__tinypy_marshal_writer_byte(writer, wire_type) || !__tinypy_marshal_writer_size32(writer, object->as.sequence_value.count)) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index != object->as.sequence_value.count; ++index) {
        if (!__tinypy_marshal_write_object(
                writer,
                object->as.sequence_value.items[index])) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_write_dict(tinypy_marshal_writer_t *writer, const tinypy_marshal_object_t *object) {
    const tinypy_marshal_dict_entry_t *entry = object->as.dict_value.first;
    size_t count = 0U;

    if (!__tinypy_marshal_writer_byte(writer, TYPE_DICT)) {
        return TINYPY_FALSE;
    }
    while (entry != NULL) {
        if (!__tinypy_marshal_write_object(writer, entry->key) || !__tinypy_marshal_write_object(writer, entry->value)) {
            return TINYPY_FALSE;
        }
        count += 1U;
        entry = entry->next;
    }
    if (count != object->as.dict_value.count) {
        __tinypy_marshal_writer_fail(
            writer,
            TINYPY_MARSHAL_INVALID_GRAPH,
            "marshal dictionary linked-list count is inconsistent");
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = __tinypy_marshal_writer_byte(writer, TYPE_NULL);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_write_code(tinypy_marshal_writer_t *writer, const tinypy_marshal_object_t *object) {
    const tinypy_marshal_code_t *code = &object->as.code_value;

    if (!__tinypy_marshal_code_fields_valid(code)) {
        __tinypy_marshal_writer_fail(
            writer,
            TINYPY_MARSHAL_INVALID_GRAPH,
            "marshal code object graph has invalid field types or counts");
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = __tinypy_marshal_writer_byte(writer, TYPE_CODE) && __tinypy_marshal_writer_i32(writer, code->argcount) && __tinypy_marshal_writer_i32(writer, code->nlocals) && __tinypy_marshal_writer_i32(writer, code->stacksize) && __tinypy_marshal_writer_i32(writer, code->flags) && __tinypy_marshal_write_object(writer, code->bytecode) && __tinypy_marshal_write_object(writer, code->consts) && __tinypy_marshal_write_object(writer, code->names) && __tinypy_marshal_write_object(writer, code->varnames) && __tinypy_marshal_write_object(writer, code->freevars) && __tinypy_marshal_write_object(writer, code->cellvars) && __tinypy_marshal_write_object(writer, code->filename) && __tinypy_marshal_write_object(writer, code->name) && __tinypy_marshal_writer_i32(writer, code->firstlineno) && __tinypy_marshal_write_object(writer, code->lnotab);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_write_object(tinypy_marshal_writer_t *writer, const tinypy_marshal_object_t *object) {
    size_t object_index = writer->object_count;
    tinypy_bool_t result = TINYPY_FALSE;

    writer->current_object = object;
    writer->current_object_index = object_index;
    writer->current_wire_type = object != NULL ? object->wire_type : 0U;
    writer->object_count += 1U;
    writer->depth += 1U;
    if (writer->depth > writer->max_depth) {
        __tinypy_marshal_writer_fail(
            writer,
            TINYPY_MARSHAL_DEPTH_LIMIT,
            "marshal writer nesting-depth limit exceeded");
        writer->depth -= 1U;
        return TINYPY_FALSE;
    }
    if (object == NULL) {
        __tinypy_marshal_writer_fail(
            writer,
            TINYPY_MARSHAL_INVALID_GRAPH,
            "marshal graph contains a NULL object edge");
        writer->depth -= 1U;
        return TINYPY_FALSE;
    }

    switch (object->type) {
    case TINYPY_MARSHAL_TYPE_NONE:
        result = __tinypy_marshal_writer_byte(writer, TYPE_NONE);
        break;
    case TINYPY_MARSHAL_TYPE_BOOL:
        result = __tinypy_marshal_writer_byte(
            writer,
            object->as.boolean_value ? TYPE_TRUE : TYPE_FALSE);
        break;
    case TINYPY_MARSHAL_TYPE_STOP_ITERATION:
        result = __tinypy_marshal_writer_byte(writer, TYPE_STOPITER);
        break;
    case TINYPY_MARSHAL_TYPE_ELLIPSIS:
        result = __tinypy_marshal_writer_byte(writer, TYPE_ELLIPSIS);
        break;
    case TINYPY_MARSHAL_TYPE_INTEGER:
        if (object->wire_type == TYPE_INTEGER) {
            if (object->as.integer_value < (int64_t)INT32_MIN || object->as.integer_value > (int64_t)INT32_MAX) {
                __tinypy_marshal_writer_fail(
                    writer,
                    TINYPY_MARSHAL_INVALID_GRAPH,
                    "TYPE_INTEGER graph value is outside signed 32-bit range");
                break;
            }
            result = __tinypy_marshal_writer_byte(writer, TYPE_INTEGER) && __tinypy_marshal_writer_i32(
                         writer,
                         (int32_t)object->as.integer_value);
        }
        else if (object->wire_type == TYPE_INTEGER64) {
            result = __tinypy_marshal_writer_byte(writer, TYPE_INTEGER64) && __tinypy_marshal_writer_i64(writer, object->as.integer_value);
        }
        else {
            __tinypy_marshal_writer_fail(
                writer,
                TINYPY_MARSHAL_INVALID_GRAPH,
                "marshal integer has an invalid wire type");
        }
        break;
    case TINYPY_MARSHAL_TYPE_LONG: {
        size_t index;
        size_t count = object->as.long_value.count;
        int64_t signed_count;
        if (count > (size_t)INT32_MAX || (count != 0U && object->as.long_value.digits == NULL) || object->as.long_value.sign < -1 || object->as.long_value.sign > 1 || (count == 0U && object->as.long_value.sign != 0) || (count != 0U && object->as.long_value.sign == 0)) {
            __tinypy_marshal_writer_fail(
                writer,
                TINYPY_MARSHAL_INVALID_GRAPH,
                "marshal long graph is not canonical");
            break;
        }
        signed_count = (int64_t)count * (int64_t)object->as.long_value.sign;
        if (!__tinypy_marshal_writer_byte(writer, TYPE_LONG) || !__tinypy_marshal_writer_i32(writer, (int32_t)signed_count)) {
            break;
        }
        result = 1;
        for (index = 0U; index != count; ++index) {
            uint16_t digit = object->as.long_value.digits[index];
            if (digit >= UINT16_C(32768) || (index + 1U == count && digit == 0U)) {
                __tinypy_marshal_writer_fail(
                    writer,
                    TINYPY_MARSHAL_INVALID_GRAPH,
                    "marshal long graph contains a non-canonical digit");
                result = 0;
                break;
            }
            if (!__tinypy_marshal_writer_u16(writer, digit)) {
                result = 0;
                break;
            }
        }
    }
    break;
    case TINYPY_MARSHAL_TYPE_FLOAT:
        if (object->wire_type == TYPE_FLOAT) {
            if (object->as.float_value.text == NULL || object->as.float_value.text_size > UINT8_MAX) {
                __tinypy_marshal_writer_fail(
                    writer,
                    TINYPY_MARSHAL_INVALID_GRAPH,
                    "text marshal float has no canonical source spelling");
                break;
            }
            result = __tinypy_marshal_writer_byte(writer, TYPE_FLOAT) && __tinypy_marshal_writer_byte(
                         writer,
                         (uint8_t)object->as.float_value.text_size) && __tinypy_marshal_writer_put(
                         writer,
                         object->as.float_value.text,
                         object->as.float_value.text_size);
        }
        else if (object->wire_type == TYPE_BINARY_FLOAT) {
            result = __tinypy_marshal_writer_byte(writer, TYPE_BINARY_FLOAT) && __tinypy_marshal_writer_binary_double(
                         writer,
                         object->as.float_value.value);
        }
        else {
            __tinypy_marshal_writer_fail(
                writer,
                TINYPY_MARSHAL_INVALID_GRAPH,
                "marshal float has an invalid wire type");
        }
        break;
    case TINYPY_MARSHAL_TYPE_COMPLEX:
        if (object->wire_type == TYPE_COMPLEX) {
            if (object->as.complex_value.real_text == NULL || object->as.complex_value.imaginary_text == NULL || object->as.complex_value.real_text_size > UINT8_MAX || object->as.complex_value.imaginary_text_size > UINT8_MAX) {
                __tinypy_marshal_writer_fail(
                    writer,
                    TINYPY_MARSHAL_INVALID_GRAPH,
                    "text marshal complex has no canonical source spelling");
                break;
            }
            result = __tinypy_marshal_writer_byte(writer, TYPE_COMPLEX) && __tinypy_marshal_writer_byte(
                         writer,
                         (uint8_t)object->as.complex_value.real_text_size) && __tinypy_marshal_writer_put(
                         writer,
                         object->as.complex_value.real_text,
                         object->as.complex_value.real_text_size) && __tinypy_marshal_writer_byte(
                         writer,
                         (uint8_t)object->as.complex_value.imaginary_text_size) && __tinypy_marshal_writer_put(
                         writer,
                         object->as.complex_value.imaginary_text,
                         object->as.complex_value.imaginary_text_size);
        }
        else if (object->wire_type == TYPE_BINARY_COMPLEX) {
            result = __tinypy_marshal_writer_byte(writer, TYPE_BINARY_COMPLEX) && __tinypy_marshal_writer_binary_double(
                         writer,
                         object->as.complex_value.real) && __tinypy_marshal_writer_binary_double(
                         writer,
                         object->as.complex_value.imaginary);
        }
        else {
            __tinypy_marshal_writer_fail(
                writer,
                TINYPY_MARSHAL_INVALID_GRAPH,
                "marshal complex has an invalid wire type");
        }
        break;
    case TINYPY_MARSHAL_TYPE_BYTES:
        result = __tinypy_marshal_write_bytes_object(writer, object);
        break;
    case TINYPY_MARSHAL_TYPE_UNICODE:
        result = __tinypy_marshal_writer_byte(writer, TYPE_UNICODE) && __tinypy_marshal_writer_size32(writer, object->as.string_value.size) && __tinypy_marshal_writer_put(
                     writer,
                     object->as.string_value.bytes,
                     object->as.string_value.size);
        break;
    case TINYPY_MARSHAL_TYPE_TUPLE:
        result = __tinypy_marshal_write_sequence(writer, object, TYPE_TUPLE);
        break;
    case TINYPY_MARSHAL_TYPE_LIST:
        result = __tinypy_marshal_write_sequence(writer, object, TYPE_LIST);
        break;
    case TINYPY_MARSHAL_TYPE_DICT:
        result = __tinypy_marshal_write_dict(writer, object);
        break;
    case TINYPY_MARSHAL_TYPE_SET:
        result = __tinypy_marshal_write_sequence(writer, object, TYPE_SET);
        break;
    case TINYPY_MARSHAL_TYPE_FROZENSET:
        result = __tinypy_marshal_write_sequence(writer, object, TYPE_FROZENSET);
        break;
    case TINYPY_MARSHAL_TYPE_CODE:
        result = __tinypy_marshal_write_code(writer, object);
        break;
    default:
        __tinypy_marshal_writer_fail(
            writer,
            TINYPY_MARSHAL_INVALID_GRAPH,
            "marshal graph contains an unknown object type");
        break;
    }

    writer->depth -= 1U;
    return result && writer->result == TINYPY_MARSHAL_OK;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_result_e __tinypy_marshal_writer_run(const tinypy_marshal_document_t *document, uint8_t *buffer, size_t capacity, const tinypy_marshal_write_options_t *options, size_t *out_size, tinypy_marshal_error_t *out_error) {
    tinypy_marshal_writer_t writer;

    (void)memset(&writer, 0, sizeof(writer));
    writer.document = document;
    writer.buffer = buffer;
    writer.capacity = capacity;
    writer.max_output_bytes = options->max_output_bytes;
    writer.max_depth = options->max_depth;
    writer.result = TINYPY_MARSHAL_OK;
    writer.error = out_error;
    writer.current_object_index = SIZE_MAX;
    if (!__tinypy_marshal_write_object(&writer, document->root)) {
        return writer.result;
    }
    if (writer.emitted_intern_count != document->intern_count) {
        __tinypy_marshal_writer_fail(
            &writer,
            TINYPY_MARSHAL_INVALID_GRAPH,
            "marshal document contains unreachable interned strings");
        return writer.result;
    }
    *out_size = writer.offset;
    return TINYPY_MARSHAL_OK;
}
//////////////////////////////////////////////////////////////////////////
tinypy_marshal_result_e tinypy_marshal_write_v2(const tinypy_marshal_document_t *document, void *buffer, size_t capacity, size_t *out_size, const tinypy_marshal_write_options_t *options, tinypy_marshal_error_t *out_error) {
    tinypy_marshal_write_options_t effective_options;
    size_t required_size = 0U;
    tinypy_marshal_result_e result;

    __tinypy_marshal_set_error_direct(out_error, TINYPY_MARSHAL_OK, 0U, 0U, "ok");
    *out_size = 0U;
    if (options == NULL) {
        tinypy_marshal_write_options_init(&effective_options);
    }
    else {
        if (options->abi_version != TINYPY_MARSHAL_ABI_VERSION || options->struct_size < (uint32_t)sizeof(*options)) {
            __tinypy_marshal_set_error_direct(
                out_error,
                TINYPY_MARSHAL_ABI_MISMATCH,
                0U,
                0U,
                "marshal writer options ABI mismatch");
            return TINYPY_MARSHAL_ABI_MISMATCH;
        }
        if (options->max_output_bytes == 0U || options->max_depth == 0U) {
            __tinypy_marshal_set_error_direct(
                out_error,
                TINYPY_MARSHAL_INVALID_ARGUMENT,
                0U,
                0U,
                "marshal writer options contain a zero limit");
            return TINYPY_MARSHAL_INVALID_ARGUMENT;
        }
        effective_options = *options;
    }

    result = __tinypy_marshal_writer_run(
        document,
        NULL,
        0U,
        &effective_options,
        &required_size,
        out_error);
    if (result != TINYPY_MARSHAL_OK) {
        return result;
    }
    *out_size = required_size;
    if (buffer == NULL) {
        return TINYPY_MARSHAL_OK;
    }
    if (capacity < required_size) {
        __tinypy_marshal_set_error_direct(
            out_error,
            TINYPY_MARSHAL_BUFFER_TOO_SMALL,
            capacity,
            0U,
            "marshal output buffer is too small");
        return TINYPY_MARSHAL_BUFFER_TOO_SMALL;
    }

    tinypy_marshal_result_e return_value_1 = __tinypy_marshal_writer_run(
        document,
        (uint8_t *)buffer,
        capacity,
        &effective_options,
        out_size,
        out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_marshal_document_destroy(tinypy_marshal_document_t *document) {
    tinypy_allocator_t allocator;

    document->state = 0U;
    allocator = document->allocator;
    tinypy_marshal_allocation_t *allocation = document->allocations;
    while (allocation != NULL) {
        tinypy_marshal_allocation_t *next = allocation->next;
        allocator.deallocate(
            allocator.user_data,
            allocation,
            allocation->total_size,
            TINYPY_MARSHAL_ALIGNMENT,
            TINYPY_MARSHAL_ALLOC_TAG_GRAPH);
        allocation = next;
    }
    allocator.deallocate(
        allocator.user_data,
        document,
        sizeof(*document),
        TINYPY_MARSHAL_ALIGNMENT,
        TINYPY_MARSHAL_ALLOC_TAG_DOCUMENT);
}
//////////////////////////////////////////////////////////////////////////
const tinypy_marshal_object_t *tinypy_marshal_document_root(const tinypy_marshal_document_t *document) {
    return document->root;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_marshal_document_input_size(const tinypy_marshal_document_t *document) {
    return document->input_size;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_marshal_document_object_count(const tinypy_marshal_document_t *document) {
    return document->object_count;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_marshal_document_allocated_bytes(const tinypy_marshal_document_t *document) {
    return document->allocated_bytes;
}
//////////////////////////////////////////////////////////////////////////
tinypy_marshal_type_e tinypy_marshal_object_type(const tinypy_marshal_object_t *object) {
    return object->type;
}
//////////////////////////////////////////////////////////////////////////
uint8_t tinypy_marshal_object_wire_type(const tinypy_marshal_object_t *object) {
    return object->wire_type;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_marshal_bool_value(const tinypy_marshal_object_t *object) {
    return object->as.boolean_value;
}
//////////////////////////////////////////////////////////////////////////
int64_t tinypy_marshal_integer_value(const tinypy_marshal_object_t *object) {
    return object->as.integer_value;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_marshal_long_view(const tinypy_marshal_object_t *object, int32_t *out_sign, const uint16_t **out_digits, size_t *out_digit_count) {
    *out_sign = object->as.long_value.sign;
    *out_digits = object->as.long_value.digits;
    *out_digit_count = object->as.long_value.count;
}
//////////////////////////////////////////////////////////////////////////
double tinypy_marshal_float_value(const tinypy_marshal_object_t *object) {
    return object->as.float_value.value;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_marshal_complex_value(const tinypy_marshal_object_t *object, double *out_real, double *out_imaginary) {
    *out_real = object->as.complex_value.real;
    *out_imaginary = object->as.complex_value.imaginary;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_marshal_bytes_view(const tinypy_marshal_object_t *object, const void **out_bytes, size_t *out_size, tinypy_bool_t *out_interned) {
    *out_bytes = object->as.string_value.bytes;
    *out_size = object->as.string_value.size;
    *out_interned = object->as.string_value.interned;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_marshal_unicode_view(const tinypy_marshal_object_t *object, const char **out_utf8, size_t *out_size, size_t *out_code_point_count) {
    *out_utf8 = (const char *)object->as.string_value.bytes;
    *out_size = object->as.string_value.size;
    *out_code_point_count = object->as.string_value.code_points;
}

//////////////////////////////////////////////////////////////////////////
size_t tinypy_marshal_sequence_size(const tinypy_marshal_object_t *object) {
    return object->as.sequence_value.count;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_marshal_object_t *tinypy_marshal_sequence_item(const tinypy_marshal_object_t *object, size_t index) {
    return object->as.sequence_value.items[index];
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_marshal_dict_size(const tinypy_marshal_object_t *object) {
    return object->as.dict_value.count;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_marshal_dict_entry_t *__tinypy_marshal_dict_entry_at(const tinypy_marshal_object_t *object, size_t index) {
    const tinypy_marshal_dict_entry_t *entry = object->as.dict_value.first;
    while (index != 0U) {
        entry = entry->next;
        index -= 1U;
    }
    return entry;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_marshal_object_t *tinypy_marshal_dict_key(const tinypy_marshal_object_t *object, size_t index) {
    const tinypy_marshal_dict_entry_t *entry = __tinypy_marshal_dict_entry_at(object, index);
    return entry->key;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_marshal_object_t *tinypy_marshal_dict_value(const tinypy_marshal_object_t *object, size_t index) {
    const tinypy_marshal_dict_entry_t *entry = __tinypy_marshal_dict_entry_at(object, index);
    return entry->value;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_marshal_code_t *tinypy_marshal_code_view(const tinypy_marshal_object_t *object) {
    return &object->as.code_value;
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_marshal_result_name(tinypy_marshal_result_e result) {
    switch (result) {
    case TINYPY_MARSHAL_OK:
        return "ok";
    case TINYPY_MARSHAL_INVALID_ARGUMENT:
        return "invalid argument";
    case TINYPY_MARSHAL_ABI_MISMATCH:
        return "ABI mismatch";
    case TINYPY_MARSHAL_TRUNCATED:
        return "truncated input";
    case TINYPY_MARSHAL_UNKNOWN_TYPE:
        return "unknown type code";
    case TINYPY_MARSHAL_NULL_OUTSIDE_DICT:
        return "NULL outside dictionary terminator";
    case TINYPY_MARSHAL_INVALID_SIZE:
        return "invalid size";
    case TINYPY_MARSHAL_INVALID_LONG:
        return "invalid long";
    case TINYPY_MARSHAL_INVALID_STRING_REF:
        return "invalid string reference";
    case TINYPY_MARSHAL_INVALID_UTF8:
        return "invalid UTF-8";
    case TINYPY_MARSHAL_INVALID_FLOAT:
        return "invalid float";
    case TINYPY_MARSHAL_INVALID_CODE:
        return "invalid code object";
    case TINYPY_MARSHAL_DEPTH_LIMIT:
        return "depth limit";
    case TINYPY_MARSHAL_OBJECT_LIMIT:
        return "object limit";
    case TINYPY_MARSHAL_STRING_LIMIT:
        return "string limit";
    case TINYPY_MARSHAL_CONTAINER_LIMIT:
        return "container limit";
    case TINYPY_MARSHAL_BYTE_LIMIT:
        return "byte limit";
    case TINYPY_MARSHAL_TRAILING_DATA:
        return "trailing data";
    case TINYPY_MARSHAL_OUTPUT_LIMIT:
        return "output limit";
    case TINYPY_MARSHAL_BUFFER_TOO_SMALL:
        return "buffer too small";
    case TINYPY_MARSHAL_INVALID_GRAPH:
        return "invalid graph";
    case TINYPY_MARSHAL_UNSUPPORTED_RUNTIME_TYPE:
        return "unsupported runtime type";
    case TINYPY_MARSHAL_ROOT_NOT_CODE:
        return "root is not code";
    default:
        return "unknown marshal result";
    }
}
