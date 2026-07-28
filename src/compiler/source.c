#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_compiler_is_utf8_cookie(const uint8_t *name, size_t size) {
    char normalized[16];
    size_t index;
    size_t output_size = 0U;

    if (size >= sizeof(normalized)) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index < size; ++index) {
        uint8_t byte = name[index];

        if (byte == '-' || byte == '_' || byte == ' ') {
            continue;
        }
        if (byte >= 'A' && byte <= 'Z') {
            byte = (uint8_t)(byte + ('a' - 'A'));
        }
        normalized[output_size] = (char)byte;
        output_size += 1U;
    }
    tinypy_bool_t return_value_1 = output_size == 4U && memcmp(normalized, "utf8", 4U) == 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_compiler_is_ascii_cookie(const uint8_t *name, size_t size) {
    char normalized[16];
    size_t index;
    size_t output_size = 0U;

    if (size >= sizeof(normalized)) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index < size; ++index) {
        uint8_t byte = name[index];

        if (byte == '-' || byte == '_' || byte == ' ') {
            continue;
        }
        if (byte >= 'A' && byte <= 'Z') {
            byte = (uint8_t)(byte + ('a' - 'A'));
        }
        normalized[output_size] = (char)byte;
        output_size += 1U;
    }
    tinypy_bool_t return_value_1 = (output_size == 5U && memcmp(normalized, "ascii", 5U) == 0) || (output_size == 7U && memcmp(normalized, "usascii", 7U) == 0);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_compiler_is_latin1_cookie(const uint8_t *name, size_t size) {
    static const char latin1[] = "latin1";
    static const char iso88591[] = "iso88591";
    char normalized[24];
    size_t index;
    size_t output_size = 0U;

    if (size >= sizeof(normalized)) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index < size; ++index) {
        uint8_t byte = name[index];

        if (byte == '-' || byte == '_' || byte == ' ') {
            continue;
        }
        if (byte >= 'A' && byte <= 'Z') {
            byte = (uint8_t)(byte + ('a' - 'A'));
        }
        normalized[output_size] = (char)byte;
        output_size += 1U;
    }
    tinypy_bool_t return_value_1 = (output_size == sizeof(latin1) - 1U && memcmp(normalized, latin1, sizeof(latin1) - 1U) == 0) || (output_size == sizeof(iso88591) - 1U && memcmp(normalized, iso88591, sizeof(iso88591) - 1U) == 0) || (output_size == 9U && memcmp(normalized, "isolatin1", 9U) == 0);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_compiler_encoding_cookie(const uint8_t *source, size_t size, const uint8_t **out_name, size_t *out_size, int32_t *out_line) {
    size_t line = 0U;
    size_t position = 0U;

    while (line < 2U && position < size) {
        size_t line_end = position;
        size_t marker;

        while (line_end < size && source[line_end] != '\n' && source[line_end] != '\r') {
            line_end += 1U;
        }
        marker = position;
        while (marker < line_end && (source[marker] == ' ' || source[marker] == '\t' || source[marker] == '\f')) {
            marker += 1U;
        }
        if (marker < line_end && source[marker] != '#') {
            return TINYPY_FALSE;
        }
        for (; marker + 6U <= line_end; ++marker) {
            size_t name_start;
            size_t name_end;

            if (source[marker] != 'c' || source[marker + 1U] != 'o' || source[marker + 2U] != 'd' || source[marker + 3U] != 'i' || source[marker + 4U] != 'n' || source[marker + 5U] != 'g') {
                continue;
            }
            name_start = marker + 6U;
            if (name_start == line_end || (source[name_start] != ':' && source[name_start] != '=')) {
                continue;
            }
            name_start += 1U;
            while (name_start < line_end && (source[name_start] == ' ' || source[name_start] == '\t')) {
                name_start += 1U;
            }
            name_end = name_start;
            while (name_end < line_end && ((source[name_end] >= 'a' && source[name_end] <= 'z') || (source[name_end] >= 'A' && source[name_end] <= 'Z') || (source[name_end] >= '0' && source[name_end] <= '9') || source[name_end] == '-' || source[name_end] == '_' || source[name_end] == '.')) {
                name_end += 1U;
            }
            if (name_end != name_start) {
                *out_name = source + name_start;
                *out_size = name_end - name_start;
                *out_line = (int32_t)line + 1;
                return TINYPY_TRUE;
            }
        }
        position = line_end;
        if (position < size && source[position] == '\r') {
            position += 1U;
        }
        if (position < size && source[position] == '\n') {
            position += 1U;
        }
        line += 1U;
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_compiler_ascii_valid(const uint8_t *bytes, size_t size) {
    size_t index;

    for (index = 0U; index < size; ++index) {
        if (bytes[index] >= 0x80U) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_compiler_utf8_valid(const uint8_t *bytes, size_t size) {
    size_t index = 0U;

    while (index < size) {
        uint8_t first = bytes[index];
        size_t length;
        uint32_t code_point;
        size_t continuation;

        if (first < 0x80U) {
            index += 1U;
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            length = 2U;
            code_point = (uint32_t)(first & 0x1fU);
        }
        else if (first >= 0xe0U && first <= 0xefU) {
            length = 3U;
            code_point = (uint32_t)(first & 0x0fU);
        }
        else if (first >= 0xf0U && first <= 0xf4U) {
            length = 4U;
            code_point = (uint32_t)(first & 0x07U);
        }
        else {
            return TINYPY_FALSE;
        }
        if (length > size - index) {
            return TINYPY_FALSE;
        }
        for (continuation = 1U; continuation < length; ++continuation) {
            uint8_t byte = bytes[index + continuation];

            if ((byte & 0xc0U) != 0x80U) {
                return TINYPY_FALSE;
            }
            code_point = (code_point << 6U) | (uint32_t)(byte & 0x3fU);
        }
        if ((length == 3U && code_point < 0x800U) || (length == 4U && code_point < 0x10000U) || code_point > 0x10ffffU || (code_point >= 0xd800U && code_point <= 0xdfffU)) {
            return TINYPY_FALSE;
        }
        index += length;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_compiler_error(tinypy_compile_ctx_t *ctx, tinypy_error_kind_e error_kind, const char *message, int32_t line_number, int32_t column_offset, tinypy_error_t **out_error) {
    const char *line_bytes = NULL;
    size_t line_size = 0U;

    if (ctx->failed != 0) {
        return;
    }
    ctx->failed = 1;
    if (line_number > 0 && ctx->source.bytes != NULL) {
        size_t position = 0U;
        int32_t line = 1;

        while (position < ctx->source.size && line < line_number) {
            if (ctx->source.bytes[position] == '\n') {
                line += 1;
            }
            position += 1U;
        }
        if (line == line_number) {
            size_t end = position;

            while (end < ctx->source.size && ctx->source.bytes[end] != '\n') {
                end += 1U;
            }
            line_bytes = (const char *)(ctx->source.bytes + position);
            line_size = end - position;
            if (end < ctx->source.size) {
                line_size += 1U;
            }
        }
    }
    tinypy_internal_make_vm_error_location(ctx->vm, error_kind, message, ctx->logical_filename, ctx->filename_size, line_number, column_offset, line_bytes, line_size, out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_compiler_error_parts(tinypy_compile_ctx_t *ctx, tinypy_error_kind_e error_kind, const char *const *parts, const size_t *part_sizes, size_t part_count, int32_t line_number, int32_t column_offset) {
    size_t message_size = 0U;
    size_t index;
    char *message;
    size_t offset = 0U;

    for (index = 0U; index < part_count; ++index) {
        message_size += part_sizes[index];
    }
    message = (char *)tinypy_internal_compiler_arena_allocate(ctx, message_size + 1U);
    if (message == NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", line_number, column_offset, ctx->out_error);
        return;
    }
    for (index = 0U; index < part_count; ++index) {
        if (part_sizes[index] != 0U) {
            (void)memcpy(message + offset, parts[index], part_sizes[index]);
        }
        offset += part_sizes[index];
    }
    message[offset] = '\0';
    tinypy_internal_compiler_error(ctx, error_kind, message, line_number, column_offset, ctx->out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_compiler_source_prepare(tinypy_compile_ctx_t *ctx, const void *source, size_t source_size, tinypy_error_t **out_error) {
    const uint8_t *input = (const uint8_t *)source;
    const uint8_t *cookie = NULL;
    size_t cookie_size = 0U;
    size_t input_offset = 0U;
    int32_t cookie_line = 0;
    int32_t bom = 0;
    int32_t ascii = 0;
    int32_t latin1 = 0;
    size_t output_capacity;
    uint8_t *output;
    size_t input_index;
    size_t output_size = 0U;

    ctx->source.bytes = input;
    ctx->source.size = source_size;
    if (ctx->limits.max_source_bytes != 0U && source_size > ctx->limits.max_source_bytes) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "source exceeds compiler byte limit", 0, 0, out_error);
        return TINYPY_FALSE;
    }
    if (source_size != 0U && memchr(source, '\0', source_size) != NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_SYNTAX, "source code string cannot contain null bytes", 1, 1, out_error);
        return TINYPY_FALSE;
    }
    if (source_size >= 3U && input[0] == 0xefU && input[1] == 0xbbU && input[2] == 0xbfU) {
        input_offset = 3U;
        bom = 1;
    }
    if (__tinypy_compiler_encoding_cookie(input + input_offset, source_size - input_offset, &cookie, &cookie_size, &cookie_line) != 0) {
        if (ctx->source_is_unicode != 0) {
            tinypy_internal_compiler_error(ctx, TINYPY_ERROR_SYNTAX, "encoding declaration in Unicode string", cookie_line, 1, out_error);
            return TINYPY_FALSE;
        }
        if (__tinypy_compiler_is_latin1_cookie(cookie, cookie_size) != 0) {
            latin1 = 1;
        }
        else if (__tinypy_compiler_is_ascii_cookie(cookie, cookie_size) != 0) {
            ascii = 1;
        }
        else if (__tinypy_compiler_is_utf8_cookie(cookie, cookie_size) == 0) {
            tinypy_internal_compiler_error(ctx, TINYPY_ERROR_SYNTAX, "unknown source encoding", cookie_line, 1, out_error);
            return TINYPY_FALSE;
        }
    }
    if (bom != 0 && (latin1 != 0 || ascii != 0)) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_SOURCE_DECODING, "source encoding conflicts with UTF-8 BOM", cookie_line, 1, out_error);
        return TINYPY_FALSE;
    }
    if (ascii != 0 && __tinypy_compiler_ascii_valid(input + input_offset, source_size - input_offset) == 0) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_SOURCE_DECODING, "source is not valid ASCII", cookie_line, 1, out_error);
        return TINYPY_FALSE;
    }
    if (latin1 == 0 && ascii == 0 && __tinypy_compiler_utf8_valid(input + input_offset, source_size - input_offset) == 0) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_SOURCE_DECODING, "source is not valid UTF-8", 1, 1, out_error);
        return TINYPY_FALSE;
    }
    output_capacity = latin1 != 0 ? (source_size - input_offset) * 2U + 2U : source_size - input_offset + 2U;
    output = (uint8_t *)tinypy_internal_compiler_arena_allocate(ctx, output_capacity);
    if (output == NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "source normalization exceeds compiler arena limit", 0, 0, out_error);
        return TINYPY_FALSE;
    }
    for (input_index = input_offset; input_index < source_size; ++input_index) {
        uint8_t byte = input[input_index];

        if (byte == '\r') {
            if (input_index + 1U < source_size && input[input_index + 1U] == '\n') {
                ++input_index;
            }
            output[output_size] = '\n';
            output_size += 1U;
        }
        else if (latin1 != 0 && byte >= 0x80U) {
            output[output_size] = (uint8_t)(0xc0U | (byte >> 6U));
            output[output_size + 1U] = (uint8_t)(0x80U | (byte & 0x3fU));
            output_size += 2U;
        }
        else {
            output[output_size] = byte;
            output_size += 1U;
        }
    }
    if ((ctx->options.flags & (uint32_t)TINYPY_COMPILE_FLAG_DONT_IMPLY_DEDENT) == 0U && (output_size == 0U || output[output_size - 1U] != '\n')) {
        output[output_size] = '\n';
        output_size += 1U;
    }
    output[output_size] = '\0';
    ctx->source.bytes = output;
    ctx->source.size = output_size;
    return TINYPY_TRUE;
}
