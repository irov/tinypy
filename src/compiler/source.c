#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_compiler_cookie_prefix(const uint8_t *name, size_t size, const char *canonical, size_t canonical_size) {
    if (size < canonical_size || (size > canonical_size && name[canonical_size] != '-' && name[canonical_size] != '_')) {
        return TINYPY_FALSE;
    }
    for (size_t index = 0U; index < canonical_size; ++index) {
        uint8_t byte = name[index];

        if (byte == '_') {
            byte = '-';
        }
        else if (byte >= 'A' && byte <= 'Z') {
            byte = (uint8_t)(byte + ('a' - 'A'));
        }
        if (byte != (uint8_t)canonical[index]) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
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
    tinypy_bool_t return_value_1 = (output_size == 4U && memcmp(normalized, "utf8", 4U) == 0)
        || __tinypy_compiler_cookie_prefix(name, size, "utf-8", 5U) != 0;
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
static tinypy_bool_t __tinypy_compiler_utf8_valid(const uint8_t *bytes, size_t size, size_t *out_invalid_offset) {
    size_t index = 0U;

    *out_invalid_offset = 0U;
    while (index < size) {
        uint8_t first = bytes[index];
        size_t length;
        uint32_t code_point;
        size_t continuation;

        if (first < 0x80U) {
            index += 1U;
            continue;
        }
        *out_invalid_offset = index;
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
static void __tinypy_compiler_byte_position(const uint8_t *bytes, size_t offset, int32_t *out_line, int32_t *out_column) {
    size_t index;
    size_t line_start = 0U;
    int32_t line = 1;

    for (index = 0U; index < offset; ++index) {
        if (bytes[index] == '\n') {
            line += 1;
            line_start = index + 1U;
        }
    }
    *out_line = line;
    *out_column = (int32_t)(offset - line_start) + 1;
}
//////////////////////////////////////////////////////////////////////////
/* Locates the text of a source line, including its newline, for diagnostics. */
static void __tinypy_compiler_source_line(const tinypy_source_view_t *source, int32_t line_number, const char **out_bytes, size_t *out_size) {
    size_t position = 0U;
    int32_t line = 1;
    size_t end;

    *out_bytes = NULL;
    *out_size = 0U;
    if (line_number <= 0 || source->bytes == NULL) {
        return;
    }
    while (position < source->size && line < line_number) {
        if (source->bytes[position] == '\n') {
            line += 1;
        }
        position += 1U;
    }
    if (line != line_number) {
        return;
    }
    end = position;
    while (end < source->size && source->bytes[end] != '\n') {
        end += 1U;
    }
    *out_bytes = (const char *)(source->bytes + position);
    *out_size = end - position + (end < source->size ? 1U : 0U);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_compiler_error(tinypy_compile_ctx_t *ctx, tinypy_error_kind_e error_kind, const char *message, int32_t line_number, int32_t column_offset, tinypy_error_t **out_error) {
    const char *line_bytes = NULL;
    size_t line_size = 0U;

    if (ctx->failed != 0) {
        return;
    }
    ctx->failed = 1;
    __tinypy_compiler_source_line(&ctx->source, line_number, &line_bytes, &line_size);
    if (line_bytes != NULL && (ctx->source_is_latin1 != 0 || ctx->source_diagnostic_latin1 != 0)) {
        uint8_t *original = (uint8_t *)tinypy_internal_compiler_arena_allocate(ctx, line_size + 1U);
        uint8_t *restored = (uint8_t *)tinypy_internal_compiler_arena_allocate(ctx, line_size + 1U);
        size_t offset = 0U;
        size_t original_size = 0U;
        size_t original_column = 0U;
        size_t restored_size = 0U;

        if (original == NULL || restored == NULL) {
            tinypy_internal_make_vm_error(ctx->vm, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", out_error);
            return;
        }
        while (offset < line_size) {
            uint32_t code_point;
            size_t width = tinypy_internal_utf8_decode((const uint8_t *)line_bytes + offset, line_size - offset, &code_point);
            if (width == 0U) {
                width = 1U;
                code_point = (uint8_t)line_bytes[offset];
            }
            if (column_offset > 0 && offset < (size_t)column_offset - 1U) {
                original_column += 1U;
            }
            original[original_size++] = code_point <= 0xffU ? (uint8_t)code_point : '?';
            offset += width;
        }
        offset = 0U;
        size_t restored_column = 0U;
        while (offset < original_size) {
            uint32_t code_point = original[offset];
            size_t width = 1U;
            if (ctx->source_is_latin1 != 0 && ctx->source_diagnostic_latin1 != 0) {
                width = tinypy_internal_utf8_decode(original + offset, original_size - offset, &code_point);
                if (width == 0U) {
                    width = tinypy_internal_utf8_invalid_span(original + offset, original_size - offset);
                    code_point = '?';
                }
            }
            if (offset < original_column) {
                restored_column += 1U;
            }
            restored[restored_size++] = code_point <= 0xffU ? (uint8_t)code_point : '?';
            offset += width;
        }
        line_bytes = (const char *)restored;
        line_size = restored_size;
        if (column_offset > 0) {
            column_offset = (int32_t)restored_column + 1;
        }
    }
    else if (line_bytes != NULL && ctx->source_diagnostic_utf8 != 0) {
        if (line_size > (SIZE_MAX - 1U) / 3U) {
            tinypy_internal_make_vm_error(ctx->vm, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", out_error);
            return;
        }
        uint8_t *restored = (uint8_t *)tinypy_internal_compiler_arena_allocate(ctx, line_size * 3U + 1U);
        size_t offset = 0U;
        size_t restored_size = 0U;
        size_t restored_column = 0U;
        if (restored == NULL) {
            tinypy_internal_make_vm_error(ctx->vm, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", out_error);
            return;
        }
        while (offset < line_size) {
            uint32_t code_point;
            size_t width = tinypy_internal_utf8_decode((const uint8_t *)line_bytes + offset, line_size - offset, &code_point);
            size_t output_width = width;
            if (width == 0U) {
                width = tinypy_internal_utf8_invalid_span((const uint8_t *)line_bytes + offset, line_size - offset);
                (void)memcpy(restored + restored_size, "\xef\xbf\xbd", 3U);
                output_width = 3U;
            }
            else {
                (void)memcpy(restored + restored_size, line_bytes + offset, width);
            }
            if (column_offset > 0 && offset < (size_t)column_offset - 1U) {
                restored_column += output_width;
            }
            restored_size += output_width;
            offset += width;
        }
        line_bytes = (const char *)restored;
        line_size = restored_size;
        if (column_offset > 0) {
            column_offset = (int32_t)restored_column + 1;
        }
    }
    if (line_number == 0 && ctx->source.size == 0U) {
        line_bytes = "";
    }
    tinypy_internal_make_vm_error_location(ctx->vm, error_kind, message, ctx->logical_filename, ctx->filename_size, line_number, column_offset, line_bytes, line_size, TINYPY_TRUE, out_error);
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
void tinypy_internal_compiler_semantic_error(tinypy_compile_ctx_t *ctx, const char *message, int32_t line_number, tinypy_bool_t include_location) {
    if (ctx->failed != 0) {
        return;
    }
    ctx->failed = 1;
    const char *line_bytes;
    size_t line_size;
    __tinypy_compiler_source_line(&ctx->program_text, line_number, &line_bytes, &line_size);
    /* PyErr_ProgramText quotes the line without its indentation. */
    while (line_size != 0U && (*line_bytes == ' ' || *line_bytes == '\t' || *line_bytes == '\f')) {
        line_bytes += 1;
        line_size -= 1U;
    }
    tinypy_internal_make_vm_error_location(ctx->vm, TINYPY_ERROR_SYNTAX, message, ctx->logical_filename, ctx->filename_size, line_number, -1, line_bytes, line_size, include_location, ctx->out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_compiler_semantic_error_parts(tinypy_compile_ctx_t *ctx, const char *const *parts, const size_t *part_sizes, size_t part_count, int32_t line_number, tinypy_bool_t include_location) {
    size_t message_size = 0U;
    size_t index;
    size_t offset = 0U;
    char *message;

    for (index = 0U; index != part_count; ++index) {
        if (part_sizes[index] > SIZE_MAX - message_size) {
            tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", line_number, 0, ctx->out_error);
            return;
        }
        message_size += part_sizes[index];
    }
    if (message_size == SIZE_MAX) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", line_number, 0, ctx->out_error);
        return;
    }
    message = (char *)tinypy_internal_compiler_arena_allocate(ctx, message_size + 1U);
    if (message == NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", line_number, 0, ctx->out_error);
        return;
    }
    for (index = 0U; index != part_count; ++index) {
        if (part_sizes[index] != 0U) {
            (void)memcpy(message + offset, parts[index], part_sizes[index]);
        }
        offset += part_sizes[index];
    }
    message[offset] = '\0';
    tinypy_internal_compiler_semantic_error(ctx, message, line_number, include_location);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_compiler_decode_error(tinypy_compile_ctx_t *ctx, tinypy_value_t *exception, int32_t line_number, tinypy_bool_t literal) {
    TINYPY_INCREF(exception);
    tinypy_vm_clear_error(ctx->vm);
    tinypy_value_t *rendered = tinypy_object_str(exception, ctx->out_error);
    TINYPY_DECREF(exception);
    if (rendered == NULL) {
        ctx->failed = TINYPY_TRUE;
        return;
    }
    size_t message_size;
    const char *message = tinypy_string_view(rendered, &message_size);
    const char *parts[] = {literal != 0 ? "(unicode error) " : "", message};
    size_t sizes[] = {literal != 0 ? 16U : 0U, message_size};

    if (literal != 0) {
        /* Python 2 bounds its AST decoder diagnostic to a 128-byte buffer. */
        if (sizes[1] > 127U - sizes[0]) {
            sizes[1] = 127U - sizes[0];
        }
        tinypy_internal_compiler_semantic_error_parts(ctx, parts, sizes, 2U, line_number, TINYPY_TRUE);
    }
    else {
        tinypy_internal_compiler_error_parts(ctx, TINYPY_ERROR_SOURCE_DECODING, parts, sizes, 2U, 0, 0);
    }
    TINYPY_DECREF(rendered);
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_compiler_syntax_warning(tinypy_compile_ctx_t *ctx, const char *message, int32_t line_number) {
    static const char label[] = ": SyntaxWarning: ";
    tinypy_vm_t *vm = ctx->vm;
    tinypy_value_t *sys_module = tinypy_dict_get_optional(vm->modules, vm->internal_sys_key);
    tinypy_value_t *stderr_value;
    tinypy_error_t *write_error = NULL;
    char line_buffer[16];
    size_t line_size = 0U;
    uint32_t line = line_number > 0 ? (uint32_t)line_number : UINT32_C(1);

    if (sys_module == NULL) {
        return TINYPY_TRUE;
    }
    stderr_value = tinypy_module_get_value_key(sys_module, vm->internal_stderr_key);
    if (stderr_value == NULL) {
        return TINYPY_TRUE;
    }
    do {
        line_buffer[sizeof(line_buffer) - 1U - line_size] = (char)('0' + line % UINT32_C(10));
        line /= UINT32_C(10);
        line_size += 1U;
    } while (line != 0U);
    if (tinypy_internal_output_write(stderr_value, ctx->logical_filename, ctx->filename_size, &write_error) == 0 ||
        tinypy_internal_output_write(stderr_value, ":", 1U, &write_error) == 0 ||
        tinypy_internal_output_write(stderr_value, line_buffer + sizeof(line_buffer) - line_size, line_size, &write_error) == 0 ||
        tinypy_internal_output_write(stderr_value, label, sizeof(label) - 1U, &write_error) == 0 ||
        tinypy_internal_output_write(stderr_value, message, strlen(message), &write_error) == 0 ||
        tinypy_internal_output_write(stderr_value, "\n", 1U, &write_error) == 0) {
        ctx->failed = 1;
        if (ctx->out_error != NULL) {
            *ctx->out_error = write_error;
        }
        else if (write_error != NULL) {
            tinypy_error_release(write_error);
        }
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
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
    int32_t latin1 = ctx->source_default_latin1 != 0 ? 1 : 0;
    size_t output_capacity;
    uint8_t *output;
    size_t input_index;
    size_t output_size = 0U;
    size_t invalid_offset = 0U;
    int32_t invalid_line;
    int32_t invalid_column;

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
        latin1 = 0;
        ctx->source_diagnostic_utf8 = TINYPY_TRUE;
    }
    if (__tinypy_compiler_encoding_cookie(input + input_offset, source_size - input_offset, &cookie, &cookie_size, &cookie_line) != 0) {
        ctx->source_encoding_declared = TINYPY_TRUE;
        if (__tinypy_compiler_is_latin1_cookie(cookie, cookie_size) != 0) {
            ctx->source_diagnostic_latin1 = TINYPY_TRUE;
            if (ctx->source_is_unicode == 0) {
                latin1 = 1;
            }
        }
        else if (__tinypy_compiler_is_ascii_cookie(cookie, cookie_size) != 0) {
            ascii = 1;
            latin1 = 0;
        }
        else {
            if (__tinypy_compiler_is_utf8_cookie(cookie, cookie_size) == 0) {
                const char *parts[] = {"unknown encoding: ", (const char *)cookie};
                size_t sizes[] = {18U, cookie_size};
                tinypy_internal_compiler_error_parts(ctx, TINYPY_ERROR_SYNTAX, parts, sizes, 2U, 0, 0);
                /* Python's tokenizer reports no location; the C diagnostic retains the cookie line. */
                if (out_error != NULL && *out_error != NULL) {
                    (*out_error)->line_number = cookie_line;
                    (*out_error)->column_offset = 1;
                }
                return TINYPY_FALSE;
            }
            latin1 = 0;
            ctx->source_diagnostic_utf8 = TINYPY_TRUE;
        }
    }
    if (bom != 0 && (latin1 != 0 || ascii != 0 || (cookie != NULL && __tinypy_compiler_cookie_prefix(cookie, cookie_size, "utf-8", 5U) == 0))) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_SOURCE_DECODING, "source encoding conflicts with UTF-8 BOM", cookie_line, 1, out_error);
        return TINYPY_FALSE;
    }
    if (ctx->source_default_latin1 == 0 && latin1 == 0 && ascii == 0 && __tinypy_compiler_utf8_valid(input + input_offset, source_size - input_offset, &invalid_offset) == 0) {
        __tinypy_compiler_byte_position(input + input_offset, invalid_offset, &invalid_line, &invalid_column);
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_SOURCE_DECODING, "source is not valid UTF-8", invalid_line, invalid_column, out_error);
        return TINYPY_FALSE;
    }
    ctx->source_is_latin1 = latin1 != 0 ? TINYPY_TRUE : TINYPY_FALSE;
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
    if (ctx->options.mode == TINYPY_COMPILE_EXEC && (output_size == 0U || output[output_size - 1U] != '\n')) {
        output[output_size] = '\n';
        output_size += 1U;
    }
    output[output_size] = '\0';
    ctx->source.bytes = output;
    ctx->source.size = output_size;
    size_t ascii_offset = 0U;
    if (ascii != 0) {
        while (ascii_offset < output_size && output[ascii_offset] < 0x80U) {
            ascii_offset += 1U;
        }
    }
    if (ascii != 0 && ascii_offset < output_size) {
        tinypy_value_t *text = tinypy_string_from_bytes(ctx->vm, output, output_size);
        tinypy_value_t *encoding = TINYPY_RET(ctx->vm->internal_codec_ascii_name);
        tinypy_error_t *decode_error = NULL;
        tinypy_value_t *decoded = tinypy_internal_text_codec(ctx->vm, text, encoding, NULL, TINYPY_TRUE, TINYPY_TRUE, NULL, &decode_error);

        if (decoded == NULL && ctx->vm->raised_value != NULL) {
            tinypy_internal_compiler_decode_error(ctx, ctx->vm->raised_value, 0, TINYPY_FALSE);
            if (out_error != NULL && *out_error != NULL) {
                __tinypy_compiler_byte_position(output, ascii_offset, &(*out_error)->line_number, &(*out_error)->column_offset);
            }
        }
        if (decode_error != NULL) {
            tinypy_error_release(decode_error);
        }
        if (decoded != NULL) {
            TINYPY_DECREF(decoded);
        }
        TINYPY_DECREF(encoding);
        TINYPY_DECREF(text);
        if (decoded == NULL) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
