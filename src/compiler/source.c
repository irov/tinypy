#include "internal.h"

#include <string.h>

/* The bytes a codec StreamReader reads from a file at a time. */
#define TINYPY_COMPILER_STREAM_READ_SIZE ((size_t)72U)

//////////////////////////////////////////////////////////////////////////
/* get_normal_name: the first twelve characters of a cookie, lowered with
   '_' read as '-', name UTF-8 or Latin-1 directly; any other spelling goes
   through the codec registry. */
static const char *__tinypy_compiler_cookie_normal_name(const uint8_t *name, size_t size) {
    char buffer[13];
    size_t index;

    for (index = 0U; index < 12U && index < size; ++index) {
        uint8_t byte = name[index];

        if (byte == '_') {
            byte = '-';
        }
        else if (byte >= 'A' && byte <= 'Z') {
            byte = (uint8_t)(byte + ('a' - 'A'));
        }
        buffer[index] = (char)byte;
    }
    buffer[index] = '\0';
    if (strcmp(buffer, "utf-8") == 0 || strncmp(buffer, "utf-8-", 6U) == 0) {
        return "utf-8";
    }
    if (strcmp(buffer, "latin-1") == 0 || strcmp(buffer, "iso-8859-1") == 0 || strcmp(buffer, "iso-latin-1") == 0 || strncmp(buffer, "latin-1-", 8U) == 0 || strncmp(buffer, "iso-8859-1-", 11U) == 0 || strncmp(buffer, "iso-latin-1-", 12U) == 0) {
        return "iso-8859-1";
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_internal_builtin_codec_e __tinypy_compiler_cookie_codec(tinypy_compile_ctx_t *ctx, const uint8_t *name, size_t size) {
    tinypy_value_t *encoding = tinypy_string_from_bytes(ctx->vm, name, size);

    if (encoding == NULL) {
        return TINYPY_INTERNAL_BUILTIN_CODEC_NONE;
    }
    tinypy_internal_builtin_codec_e codec = tinypy_internal_codecs_builtin(ctx->vm, encoding);
    TINYPY_DECREF(encoding);
    return codec;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_compiler_encoding_cookie(const uint8_t *source, size_t size, const uint8_t **out_name, size_t *out_size, int32_t *out_line, size_t *out_line_end) {
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
                size_t cookie_line_end = line_end;

                if (cookie_line_end < size && source[cookie_line_end] == '\r') {
                    cookie_line_end += 1U;
                }
                if (cookie_line_end < size && source[cookie_line_end] == '\n') {
                    cookie_line_end += 1U;
                }
                *out_name = source + name_start;
                *out_size = name_end - name_start;
                *out_line = (int32_t)line + 1;
                *out_line_end = cookie_line_end;
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
static char *__tinypy_compiler_join_parts(tinypy_compile_ctx_t *ctx, const char *const *parts, const size_t *part_sizes, size_t part_count) {
    size_t message_size = 0U;
    size_t index;
    size_t offset = 0U;
    char *message;

    for (index = 0U; index != part_count; ++index) {
        if (part_sizes[index] >= SIZE_MAX - message_size) {
            return NULL;
        }
        message_size += part_sizes[index];
    }
    message = (char *)tinypy_internal_compiler_arena_allocate(ctx, message_size + 1U);
    if (message == NULL) {
        return NULL;
    }
    for (index = 0U; index != part_count; ++index) {
        if (part_sizes[index] != 0U) {
            (void)memcpy(message + offset, parts[index], part_sizes[index]);
        }
        offset += part_sizes[index];
    }
    message[offset] = '\0';
    return message;
}
//////////////////////////////////////////////////////////////////////////
/* dec_utf8 of PyTokenizer_RestoreEncoding: the text decodes from UTF-8 and
   re-encodes to the declared encoding, each invalid sequence replaced by
   U+FFFD and each character the encoding lacks by '?'. Returns the encoded
   size; output is optional. */
static size_t __tinypy_compiler_restore_text(const tinypy_compile_ctx_t *ctx, const uint8_t *bytes, size_t size, uint8_t *output) {
    size_t offset = 0U;
    size_t output_size = 0U;

    while (offset < size) {
        uint32_t code_point;
        size_t width = tinypy_internal_utf8_decode(bytes + offset, size - offset, &code_point);

        if (width == 0U) {
            width = tinypy_internal_utf8_invalid_span(bytes + offset, size - offset);
            code_point = 0xfffdU;
        }
        if (ctx->source_diagnostic_utf8 != 0) {
            size_t output_width = code_point == 0xfffdU ? 3U : width;

            if (output != NULL) {
                if (code_point == 0xfffdU) {
                    (void)memcpy(output + output_size, "\xef\xbf\xbd", 3U);
                }
                else {
                    (void)memcpy(output + output_size, bytes + offset, width);
                }
            }
            output_size += output_width;
        }
        else {
            if (output != NULL) {
                output[output_size] = code_point <= 0xffU ? (uint8_t)code_point : (uint8_t)'?';
            }
            output_size += 1U;
        }
        offset += width;
    }
    return output_size;
}
//////////////////////////////////////////////////////////////////////////
/* Reports a tokenizer or parser diagnostic with the text of its logical
   line and the offset of the scan position in that text, both translated
   to the units of Python's tokenizer buffer and then restored to the
   declared encoding the way parsetok does. */
static void __tinypy_compiler_report(tinypy_compile_ctx_t *ctx, tinypy_error_kind_e error_kind, const char *message, int32_t line_number, int32_t column_offset, const char *line_bytes, size_t line_size, tinypy_error_t **out_error) {
    if (ctx->failed != 0) {
        return;
    }
    ctx->failed = 1;
    if (line_bytes != NULL && line_size != 0U && ctx->source_is_latin1 != 0 && ctx->source_diagnostic_transcoded == 0) {
        /* Python tokenized the host bytes themselves. */
        uint8_t *original = (uint8_t *)tinypy_internal_compiler_arena_allocate(ctx, line_size + 1U);
        size_t offset = 0U;
        size_t original_size = 0U;
        size_t original_column = 0U;

        if (original == NULL) {
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
            if (column_offset > 0 && offset < (size_t)column_offset) {
                original_column += 1U;
            }
            original[original_size++] = code_point <= 0xffU ? (uint8_t)code_point : (uint8_t)'?';
            offset += width;
        }
        line_bytes = (const char *)original;
        line_size = original_size;
        if (column_offset > 0) {
            column_offset = (int32_t)original_column;
        }
    }
    if (line_bytes != NULL && line_size != 0U && (ctx->source_diagnostic_latin1 != 0 || ctx->source_diagnostic_utf8 != 0)) {
        size_t restored_size = __tinypy_compiler_restore_text(ctx, (const uint8_t *)line_bytes, line_size, NULL);
        uint8_t *restored = (uint8_t *)tinypy_internal_compiler_arena_allocate(ctx, restored_size + 1U);

        if (restored == NULL) {
            tinypy_internal_make_vm_error(ctx->vm, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", out_error);
            return;
        }
        (void)__tinypy_compiler_restore_text(ctx, (const uint8_t *)line_bytes, line_size, restored);
        if (column_offset > 1) {
            size_t prefix_size = (size_t)column_offset - 1U < line_size ? (size_t)column_offset - 1U : line_size;

            column_offset = (int32_t)__tinypy_compiler_restore_text(ctx, (const uint8_t *)line_bytes, prefix_size, NULL) + 1;
        }
        line_bytes = (const char *)restored;
        line_size = restored_size;
    }
    tinypy_internal_make_vm_error_location(ctx->vm, error_kind, message, ctx->logical_filename, ctx->filename_size, line_number, column_offset, line_bytes, line_size, TINYPY_TRUE, out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_compiler_error(tinypy_compile_ctx_t *ctx, tinypy_error_kind_e error_kind, const char *message, int32_t line_number, int32_t column_offset, tinypy_error_t **out_error) {
    const char *line_bytes;
    size_t line_size;

    __tinypy_compiler_source_line(&ctx->source, line_number, &line_bytes, &line_size);
    if (line_number == 0 && ctx->source.size == 0U) {
        line_bytes = "";
    }
    __tinypy_compiler_report(ctx, error_kind, message, line_number, column_offset, line_bytes, line_size, out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_compiler_error_text(tinypy_compile_ctx_t *ctx, tinypy_error_kind_e error_kind, const char *message, int32_t line_number, int32_t column_offset, const char *text, size_t text_size, tinypy_error_t **out_error) {
    __tinypy_compiler_report(ctx, error_kind, message, line_number, column_offset, text, text_size, out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_compiler_error_parts(tinypy_compile_ctx_t *ctx, tinypy_error_kind_e error_kind, const char *const *parts, const size_t *part_sizes, size_t part_count, int32_t line_number, int32_t column_offset) {
    char *message = __tinypy_compiler_join_parts(ctx, parts, part_sizes, part_count);

    if (message == NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", line_number, column_offset, ctx->out_error);
        return;
    }
    tinypy_internal_compiler_error(ctx, error_kind, message, line_number, column_offset, ctx->out_error);
}
//////////////////////////////////////////////////////////////////////////
/* PyErr_SetString(PyExc_SyntaxError, message): a SyntaxError without any
   location. */
void tinypy_internal_compiler_plain_error(tinypy_compile_ctx_t *ctx, const char *message) {
    if (ctx->failed != 0) {
        return;
    }
    ctx->failed = 1;
    tinypy_internal_make_vm_error_location(ctx->vm, TINYPY_ERROR_SYNTAX, message, NULL, 0U, -1, -1, NULL, 0U, TINYPY_FALSE, ctx->out_error);
    /* The C diagnostic reports an unknown location as zero. */
    if (ctx->out_error != NULL && *ctx->out_error != NULL) {
        (*ctx->out_error)->line_number = 0;
        (*ctx->out_error)->column_offset = 0;
    }
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
    char *message = __tinypy_compiler_join_parts(ctx, parts, part_sizes, part_count);

    if (message == NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", line_number, 0, ctx->out_error);
        return;
    }
    tinypy_internal_compiler_semantic_error(ctx, message, line_number, include_location);
}
//////////////////////////////////////////////////////////////////////////
/* The str() of an exception, copied into the arena. */
static const char *__tinypy_compiler_exception_message(tinypy_compile_ctx_t *ctx, tinypy_value_t *exception, size_t *out_size) {
    TINYPY_INCREF(exception);
    tinypy_vm_clear_error(ctx->vm);
    tinypy_value_t *rendered = tinypy_object_str(exception, ctx->out_error);
    TINYPY_DECREF(exception);
    if (rendered == NULL) {
        ctx->failed = TINYPY_TRUE;
        return NULL;
    }
    size_t message_size;
    const char *message = tinypy_string_view(rendered, &message_size);
    char *copy = __tinypy_compiler_join_parts(ctx, &message, &message_size, 1U);
    TINYPY_DECREF(rendered);
    if (copy == NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", 0, 0, ctx->out_error);
        return NULL;
    }
    *out_size = message_size;
    return copy;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_compiler_decode_error(tinypy_compile_ctx_t *ctx, tinypy_value_t *exception, int32_t line_number, tinypy_bool_t literal) {
    size_t message_size;
    const char *message = __tinypy_compiler_exception_message(ctx, exception, &message_size);

    if (message == NULL) {
        return;
    }
    if (literal != 0) {
        const char *parts[] = {"(unicode error) ", message};
        size_t sizes[] = {16U, message_size};

        /* Python 2 bounds its AST decoder diagnostic to a 128-byte buffer. */
        if (sizes[1] > 127U - sizes[0]) {
            sizes[1] = 127U - sizes[0];
        }
        tinypy_internal_compiler_semantic_error_parts(ctx, parts, sizes, 2U, line_number, TINYPY_TRUE);
        return;
    }
    tinypy_internal_compiler_error_text(ctx, TINYPY_ERROR_SOURCE_DECODING, message, line_number, 0, NULL, 0U, ctx->out_error);
}
//////////////////////////////////////////////////////////////////////////
/* Whether bytes decode in a codec; a failure leaves its message in the
   arena, or no message once a limit diagnostic was reported. The exception
   the codec raised is cleared. */
static tinypy_bool_t __tinypy_compiler_decode(tinypy_compile_ctx_t *ctx, const uint8_t *bytes, size_t size, tinypy_value_t *encoding, const char **out_message, size_t *out_message_size) {
    tinypy_vm_t *vm = ctx->vm;
    tinypy_value_t *text = tinypy_string_from_bytes(vm, bytes, size);
    tinypy_error_t *decode_error = NULL;

    *out_message = NULL;
    *out_message_size = 0U;
    if (text == NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "source exceeds compiler byte limit", 0, 0, ctx->out_error);
        return TINYPY_FALSE;
    }
    tinypy_value_t *decoded = tinypy_internal_text_codec(vm, text, encoding, NULL, TINYPY_TRUE, TINYPY_TRUE, NULL, &decode_error);
    TINYPY_DECREF(text);
    if (decode_error != NULL) {
        tinypy_error_release(decode_error);
    }
    if (decoded != NULL) {
        TINYPY_DECREF(decoded);
        return TINYPY_TRUE;
    }
    if (vm->raised_value == NULL) {
        ctx->failed = TINYPY_TRUE;
        return TINYPY_FALSE;
    }
    *out_message = __tinypy_compiler_exception_message(ctx, vm->raised_value, out_message_size);
    tinypy_vm_clear_error(vm);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_compiler_is_line_break(uint8_t byte) {
    tinypy_bool_t result = byte == '\n' || byte == '\r' || byte == 0x0bU || byte == 0x0cU || byte == 0x1cU || byte == 0x1dU || byte == 0x1eU ? TINYPY_TRUE : TINYPY_FALSE;
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The number of lines the tokenizer counts in bytes: newlines and lone
   carriage returns. */
static size_t __tinypy_compiler_count_lines(const uint8_t *bytes, size_t size) {
    size_t count = 0U;
    size_t index;

    for (index = 0U; index < size; ++index) {
        if (bytes[index] == '\n' || (bytes[index] == '\r' && (index + 1U == size || bytes[index + 1U] != '\n'))) {
            count += 1U;
        }
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
/* The lines of a decoded prefix as unicode.splitlines counts them. */
static size_t __tinypy_compiler_count_split_lines(const uint8_t *bytes, size_t size) {
    size_t count = 0U;
    size_t index;

    for (index = 0U; index < size; ++index) {
        if (__tinypy_compiler_is_line_break(bytes[index]) != 0 && (bytes[index] != '\r' || index + 1U == size || bytes[index + 1U] != '\n')) {
            count += 1U;
        }
    }
    if (size != 0U && __tinypy_compiler_is_line_break(bytes[size - 1U]) == 0) {
        count += 1U;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
/* A file whose cookie names a codec other than the fast paths is read after
   the cookie line through the codec's StreamReader: 72-byte reads decoded
   together with the undecoded tail of the previous read. A read that fails
   returns the lines decoded before the failing byte when there are at least
   two of them and raises on the next read, at position 0; otherwise it
   raises at once with the position in its data. The tokenizer sees the
   failure on the line whose read raised. */
static tinypy_bool_t __tinypy_compiler_file_decoding(tinypy_compile_ctx_t *ctx, const uint8_t *rest, size_t size, int32_t cookie_line, tinypy_bool_t utf8, tinypy_value_t *encoding) {
    size_t failure;

    if (utf8 != 0) {
        if (__tinypy_compiler_utf8_valid(rest, size, &failure) != 0) {
            return TINYPY_TRUE;
        }
    }
    else {
        failure = 0U;
        while (failure < size && rest[failure] < 0x80U) {
            failure += 1U;
        }
        if (failure == size) {
            return TINYPY_TRUE;
        }
    }
    size_t chunk = failure - failure % TINYPY_COMPILER_STREAM_READ_SIZE;
    size_t carry = 0U;

    if (utf8 != 0) {
        /* A sequence cut by the previous read waits for this one. */
        size_t lead;

        for (lead = 1U; lead <= 3U && lead <= chunk; ++lead) {
            uint8_t byte = rest[chunk - lead];

            if ((byte & 0xc0U) == 0x80U) {
                continue;
            }
            if (byte >= 0xc2U && byte <= 0xf4U && (byte < 0xe0U ? 2U : (byte < 0xf0U ? 3U : 4U)) > lead) {
                carry = lead;
            }
            break;
        }
    }
    size_t data_begin = chunk - carry;
    size_t data_end = chunk + TINYPY_COMPILER_STREAM_READ_SIZE < size ? chunk + TINYPY_COMPILER_STREAM_READ_SIZE : size;
    size_t prefix_lines = __tinypy_compiler_count_split_lines(rest + data_begin, failure - data_begin);
    size_t returned_end = data_begin;
    const uint8_t *data = rest + data_begin;
    size_t data_size = data_end - data_begin;

    if (prefix_lines > 1U) {
        size_t pending = failure - data_begin;
        size_t line_start = data_begin;

        while (line_start != 0U && rest[line_start - 1U] != '\n' && rest[line_start - 1U] != '\r') {
            line_start -= 1U;
        }
        pending += data_begin - line_start;
        if (pending >= TINYPY_COMPILER_STREAM_READ_SIZE) {
            returned_end = failure;
        }
        data = rest + failure;
        data_size = (chunk + 2U * TINYPY_COMPILER_STREAM_READ_SIZE < size ? chunk + 2U * TINYPY_COMPILER_STREAM_READ_SIZE : size) - failure;
    }
    const char *message;
    size_t message_size;
    if (__tinypy_compiler_decode(ctx, data, data_size, encoding, &message, &message_size) != 0) {
        return TINYPY_TRUE;
    }
    if (message == NULL) {
        return TINYPY_FALSE;
    }
    ctx->source_decode_line = cookie_line + 1 + (int32_t)__tinypy_compiler_count_lines(rest, returned_end);
    ctx->source_decode_message = message;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_compiler_stderr(tinypy_compile_ctx_t *ctx) {
    tinypy_vm_t *vm = ctx->vm;
    tinypy_value_t *sys_module = tinypy_dict_get_optional(vm->modules, vm->internal_sys_key);

    if (sys_module == NULL) {
        return NULL;
    }
    tinypy_value_t *stderr_value = tinypy_module_get_value_key(sys_module, vm->internal_stderr_key);
    return stderr_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_compiler_write_stderr(tinypy_compile_ctx_t *ctx, tinypy_value_t *stderr_value, const char *const *parts, const size_t *part_sizes, size_t part_count) {
    tinypy_error_t *write_error = NULL;
    size_t index;

    for (index = 0U; index < part_count; ++index) {
        if (tinypy_internal_output_write(stderr_value, parts[index], part_sizes[index], &write_error) == 0) {
            ctx->failed = 1;
            if (ctx->out_error != NULL) {
                *ctx->out_error = write_error;
            }
            else if (write_error != NULL) {
                tinypy_error_release(write_error);
            }
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_compiler_syntax_warning(tinypy_compile_ctx_t *ctx, const char *message, int32_t line_number) {
    static const char label[] = ": SyntaxWarning: ";
    tinypy_value_t *stderr_value = __tinypy_compiler_stderr(ctx);
    char line_buffer[16];
    size_t line_size = 0U;
    uint32_t line = line_number > 0 ? (uint32_t)line_number : UINT32_C(1);
    const char *source_bytes;
    size_t source_size;

    if (stderr_value == NULL) {
        return TINYPY_TRUE;
    }
    do {
        line_buffer[sizeof(line_buffer) - 1U - line_size] = (char)('0' + line % UINT32_C(10));
        line /= UINT32_C(10);
        line_size += 1U;
    } while (line != 0U);
    const char *parts[] = {ctx->logical_filename, ":", line_buffer + sizeof(line_buffer) - line_size, label, message, "\n"};
    size_t sizes[] = {ctx->filename_size, 1U, line_size, sizeof(label) - 1U, strlen(message), 1U};
    if (__tinypy_compiler_write_stderr(ctx, stderr_value, parts, sizes, 6U) == 0) {
        return TINYPY_FALSE;
    }
    /* _Py_DisplaySourceLine: the line of the file without its indentation. */
    __tinypy_compiler_source_line(&ctx->program_text, line_number, &source_bytes, &source_size);
    if (source_bytes == NULL) {
        return TINYPY_TRUE;
    }
    while (source_size != 0U && (*source_bytes == ' ' || *source_bytes == '\t' || *source_bytes == '\f')) {
        source_bytes += 1;
        source_size -= 1U;
    }
    if (source_size >= 2U && source_bytes[source_size - 2U] == '\r' && source_bytes[source_size - 1U] == '\n') {
        source_size -= 2U;
    }
    else if (source_size != 0U && (source_bytes[source_size - 1U] == '\n' || source_bytes[source_size - 1U] == '\r')) {
        source_size -= 1U;
    }
    const char *line_parts[] = {"  ", source_bytes, "\n"};
    size_t line_sizes[] = {2U, source_size, 1U};
    tinypy_bool_t written = __tinypy_compiler_write_stderr(ctx, stderr_value, line_parts, line_sizes, 3U);
    return written;
}
//////////////////////////////////////////////////////////////////////////
/* The -t warning of the tokenizer, written once per file. */
tinypy_bool_t tinypy_internal_compiler_tab_warning(tinypy_compile_ctx_t *ctx) {
    static const char label[] = ": inconsistent use of tabs and spaces in indentation\n";
    tinypy_value_t *stderr_value = __tinypy_compiler_stderr(ctx);
    const char *parts[] = {ctx->logical_filename, label};
    size_t sizes[] = {ctx->filename_size, sizeof(label) - 1U};

    if (stderr_value == NULL) {
        return TINYPY_TRUE;
    }
    char *message = __tinypy_compiler_join_parts(ctx, parts, sizes, 2U);
    if (message == NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", 0, 0, ctx->out_error);
        return TINYPY_FALSE;
    }
    size_t message_size = ctx->filename_size + sizeof(label) - 1U;
    const char *message_parts[] = {message};
    tinypy_bool_t written = __tinypy_compiler_write_stderr(ctx, stderr_value, message_parts, &message_size, 1U);
    return written;
}
//////////////////////////////////////////////////////////////////////////
/* A cookie the compiler cannot honour: the file tokenizer fails when it
   reads the cookie line, after the lines before it, the string tokenizer
   at once with the codec lookup failure. */
static tinypy_bool_t __tinypy_compiler_cookie_error(tinypy_compile_ctx_t *ctx, const char *prefix, size_t prefix_size, const char *name, size_t name_size, const char *suffix, size_t suffix_size, int32_t cookie_line, tinypy_error_kind_e error_kind, tinypy_error_t **out_error) {
    const char *parts[] = {prefix, name, suffix};
    size_t sizes[] = {prefix_size, name_size, suffix_size};
    char *message = __tinypy_compiler_join_parts(ctx, parts, sizes, 3U);

    if (message == NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", 0, 0, out_error);
        return TINYPY_FALSE;
    }
    if (ctx->source_is_file != 0) {
        ctx->source_decode_line = cookie_line;
        ctx->source_decode_message = message;
        return TINYPY_TRUE;
    }
    tinypy_internal_compiler_error_text(ctx, error_kind, message, 0, 0, NULL, 0U, out_error);
    /* Python's tokenizer reports no location; the C diagnostic retains the cookie line. */
    if (out_error != NULL && *out_error != NULL) {
        (*out_error)->line_number = cookie_line;
        (*out_error)->column_offset = 1;
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* check_coding_spec: a cookie selects the fast path of its normal name or
   a codec of the registry, and conflicts with a BOM unless it names UTF-8. */
static tinypy_bool_t __tinypy_compiler_apply_cookie(tinypy_compile_ctx_t *ctx, const uint8_t *cookie, size_t cookie_size, int32_t cookie_line, int32_t bom, tinypy_internal_builtin_codec_e *out_codec, tinypy_bool_t *out_fast_path, int32_t *out_latin1, tinypy_error_t **out_error) {
    const char *normal_name = __tinypy_compiler_cookie_normal_name(cookie, cookie_size);

    ctx->source_encoding_declared = TINYPY_TRUE;
    if (bom == 0) {
        ctx->source_ascii_lines = cookie_line - 1;
    }
    if (bom != 0 && (normal_name == NULL || strcmp(normal_name, "utf-8") != 0)) {
        const char *name = normal_name != NULL ? normal_name : (const char *)cookie;
        size_t name_size = normal_name != NULL ? strlen(normal_name) : cookie_size;

        *out_latin1 = 0;
        tinypy_bool_t deferred = __tinypy_compiler_cookie_error(ctx, "encoding problem: ", 18U, name, name_size, " with BOM", 9U, cookie_line, TINYPY_ERROR_SOURCE_DECODING, out_error);
        return deferred;
    }
    if (normal_name != NULL) {
        *out_fast_path = TINYPY_TRUE;
        *out_codec = strcmp(normal_name, "utf-8") == 0 ? TINYPY_INTERNAL_BUILTIN_CODEC_UTF8 : TINYPY_INTERNAL_BUILTIN_CODEC_LATIN1;
    }
    else {
        *out_codec = __tinypy_compiler_cookie_codec(ctx, cookie, cookie_size);
    }
    if (*out_codec == TINYPY_INTERNAL_BUILTIN_CODEC_LATIN1) {
        ctx->source_diagnostic_latin1 = TINYPY_TRUE;
        /* The fast path tokenizes Latin-1 bytes themselves; the codec
           decodes any source, a Unicode one twice. */
        if (*out_fast_path == 0) {
            ctx->source_diagnostic_transcoded = TINYPY_TRUE;
            *out_latin1 = 1;
        }
        else if (ctx->source_is_unicode == 0) {
            *out_latin1 = 1;
        }
        return TINYPY_TRUE;
    }
    *out_latin1 = 0;
    if (*out_codec == TINYPY_INTERNAL_BUILTIN_CODEC_UTF8) {
        ctx->source_diagnostic_utf8 = TINYPY_TRUE;
        return TINYPY_TRUE;
    }
    if (*out_codec == TINYPY_INTERNAL_BUILTIN_CODEC_ASCII) {
        return TINYPY_TRUE;
    }
    *out_codec = TINYPY_INTERNAL_BUILTIN_CODEC_NONE;
    if (ctx->source_is_file != 0) {
        tinypy_bool_t deferred = __tinypy_compiler_cookie_error(ctx, "encoding problem: ", 18U, (const char *)cookie, cookie_size, "", 0U, cookie_line, TINYPY_ERROR_SYNTAX, out_error);
        return deferred;
    }
    (void)__tinypy_compiler_cookie_error(ctx, "unknown encoding: ", 18U, (const char *)cookie, cookie_size, "", 0U, cookie_line, TINYPY_ERROR_SYNTAX, out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_compiler_source_prepare(tinypy_compile_ctx_t *ctx, const void *source, size_t source_size, tinypy_error_t **out_error) {
    const uint8_t *input = (const uint8_t *)source;
    const uint8_t *cookie = NULL;
    size_t cookie_size = 0U;
    size_t cookie_line_end = 0U;
    size_t input_offset = 0U;
    int32_t cookie_line = 0;
    int32_t bom = 0;
    tinypy_internal_builtin_codec_e codec = TINYPY_INTERNAL_BUILTIN_CODEC_NONE;
    tinypy_bool_t fast_path = TINYPY_FALSE;
    int32_t latin1 = ctx->source_default_latin1 != 0 ? 1 : 0;
    size_t output_capacity;
    uint8_t *output;
    size_t input_index;
    size_t output_size = 0U;
    tinypy_bool_t trailing_crlf = TINYPY_FALSE;

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
        ctx->source_encoding_declared = TINYPY_TRUE;
        ctx->source_diagnostic_utf8 = TINYPY_TRUE;
    }
    if (__tinypy_compiler_encoding_cookie(input + input_offset, source_size - input_offset, &cookie, &cookie_size, &cookie_line, &cookie_line_end) != 0) {
        if (__tinypy_compiler_apply_cookie(ctx, cookie, cookie_size, cookie_line, bom, &codec, &fast_path, &latin1, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    else if (bom == 0) {
        /* decoding_fgets: a file without a declared encoding is ASCII. */
        ctx->source_ascii_lines = INT32_MAX;
    }
    ctx->source_is_latin1 = latin1 != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    output_capacity = latin1 != 0 ? (source_size - input_offset) * 2U + 3U : source_size - input_offset + 3U;
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
                trailing_crlf = input_index + 1U == source_size ? TINYPY_TRUE : TINYPY_FALSE;
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
    if (ctx->options.mode == TINYPY_COMPILE_EXEC) {
        if (output_size == 0U || output[output_size - 1U] != '\n') {
            output[output_size] = '\n';
            output_size += 1U;
            ctx->source_final_newline_added = TINYPY_TRUE;
        }
        else if (trailing_crlf != 0 && ctx->source_is_file == 0) {
            /* translate_newlines drops the newline of a final CRLF pair,
               then supplies the newline a string must end with. */
            output[output_size] = '\n';
            output_size += 1U;
        }
    }
    output[output_size] = '\0';
    ctx->source.bytes = output;
    ctx->source.size = output_size;
    if (codec != TINYPY_INTERNAL_BUILTIN_CODEC_ASCII && (codec != TINYPY_INTERNAL_BUILTIN_CODEC_UTF8 || fast_path != 0)) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t utf8 = codec == TINYPY_INTERNAL_BUILTIN_CODEC_UTF8 ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_value_t *encoding = TINYPY_RET(utf8 != 0 ? ctx->vm->internal_codec_utf8_name : ctx->vm->internal_codec_ascii_name);
    tinypy_bool_t prepared;
    if (ctx->source_is_file != 0) {
        prepared = __tinypy_compiler_file_decoding(ctx, input + cookie_line_end, source_size - cookie_line_end, cookie_line, utf8, encoding);
    }
    else {
        /* translate_into_utf8 decodes the whole string before tokenizing. */
        const char *message;
        size_t message_size;
        size_t decode_offset = 0U;

        prepared = __tinypy_compiler_decode(ctx, output, output_size, encoding, &message, &message_size);
        if (prepared == 0 && message != NULL) {
            tinypy_internal_compiler_error_text(ctx, TINYPY_ERROR_SOURCE_DECODING, message, 0, 0, NULL, 0U, out_error);
            if (utf8 != 0) {
                (void)__tinypy_compiler_utf8_valid(output, output_size, &decode_offset);
            }
            else {
                while (decode_offset < output_size && output[decode_offset] < 0x80U) {
                    decode_offset += 1U;
                }
            }
            if (out_error != NULL && *out_error != NULL) {
                __tinypy_compiler_byte_position(output, decode_offset, &(*out_error)->line_number, &(*out_error)->column_offset);
            }
        }
    }
    TINYPY_DECREF(encoding);
    return prepared;
}
