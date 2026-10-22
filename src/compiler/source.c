#include "internal.h"

#include <string.h>

/* The bytes a codec StreamReader reads from a file at a time for a line,
   doubled while the line does not end until the reads reach the limit. */
#define TINYPY_COMPILER_STREAM_READ_SIZE ((size_t)72U)
#define TINYPY_COMPILER_STREAM_READ_LIMIT ((size_t)8000U)
/* The characters PyErr_ProgramText reads into its line buffer at a time. */
#define TINYPY_COMPILER_PROGRAM_TEXT_READ_SIZE ((size_t)999U)

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
/* get_coding_spec over the first two lines. decode_str reads a string only
   in the lines a newline ends, which the string of an expression or an
   interactive statement does not get at its end. */
static tinypy_bool_t __tinypy_compiler_encoding_cookie(const uint8_t *source, size_t size, tinypy_bool_t newline_required, const uint8_t **out_name, size_t *out_size, int32_t *out_line, size_t *out_line_end) {
    size_t line = 0U;
    size_t position = 0U;

    while (line < 2U && position < size) {
        size_t line_end = position;
        size_t marker;

        while (line_end < size && source[line_end] != '\n' && source[line_end] != '\r') {
            line_end += 1U;
        }
        if (line_end == size && newline_required != 0) {
            return TINYPY_FALSE;
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
/* Locates a line of the host file read with universal newlines, as Python
   reads a file again for its diagnostics: the bounds of its characters and
   whether a newline ends it. A line past the end of the file is empty. The
   search starts at the line located last unless the line is before it, so
   the diagnostics of a pass read the file once, as linecache does. */
static tinypy_bool_t __tinypy_compiler_program_line(tinypy_compile_ctx_t *ctx, int32_t line_number, size_t *out_start, size_t *out_end, tinypy_bool_t *out_newline) {
    const tinypy_source_view_t *source = &ctx->program_text;
    size_t position = 0U;
    int32_t line = 1;

    *out_start = 0U;
    *out_end = 0U;
    *out_newline = TINYPY_FALSE;
    if (line_number <= 0 || source->bytes == NULL) {
        return TINYPY_FALSE;
    }
    if (ctx->program_line_number != 0 && ctx->program_line_number <= line_number) {
        position = ctx->program_line_start;
        line = ctx->program_line_number;
    }
    for (;;) {
        size_t end = position;
        while (end < source->size && source->bytes[end] != '\n' && source->bytes[end] != '\r') {
            end += 1U;
        }
        if (line == line_number || end == source->size) {
            ctx->program_line_number = line;
            ctx->program_line_start = position;
            *out_start = line == line_number ? position : end;
            *out_end = end;
            *out_newline = end < source->size ? TINYPY_TRUE : TINYPY_FALSE;
            return TINYPY_TRUE;
        }
        position = end + (source->bytes[end] == '\r' && end + 1U < source->size && source->bytes[end + 1U] == '\n' ? 2U : 1U);
        line += 1;
    }
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
/* The code point of a surrogate pair, which the UTF-8 of a unicode object
   holds as two three-byte sequences, at bytes; zero when none starts there. */
static uint32_t __tinypy_compiler_surrogate_pair(const uint8_t *bytes, size_t size) {
    if (size < 6U || bytes[0] != 0xedU || (bytes[1] & 0xf0U) != 0xa0U || (bytes[2] & 0xc0U) != 0x80U || bytes[3] != 0xedU || (bytes[4] & 0xf0U) != 0xb0U || (bytes[5] & 0xc0U) != 0x80U) {
        return 0U;
    }
    uint32_t high = ((uint32_t)(bytes[1] & 0x0fU) << 6U) | (uint32_t)(bytes[2] & 0x3fU);
    uint32_t low = ((uint32_t)(bytes[4] & 0x0fU) << 6U) | (uint32_t)(bytes[5] & 0x3fU);
    uint32_t code_point = UINT32_C(0x10000) + (high << 10U) + low;
    return code_point;
}
//////////////////////////////////////////////////////////////////////////
/* dec_utf8 of PyTokenizer_RestoreEncoding: the text decodes from UTF-8 and
   re-encodes to the declared encoding, each invalid sequence replaced by
   U+FFFD and each character the encoding lacks by '?'; the UTF-8 encoder
   joins a surrogate pair into the code point it stands for. Returns the
   encoded size; output is optional. */
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
            uint32_t pair = __tinypy_compiler_surrogate_pair(bytes + offset, size - offset);

            if (pair != 0U) {
                code_point = pair;
                width = 6U;
            }
            uint8_t encoded[4];
            size_t encoded_size = tinypy_internal_utf8_encode(code_point, encoded);
            if (output != NULL) {
                (void)memcpy(output + output_size, encoded, encoded_size);
            }
            output_size += encoded_size;
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
    /* err_input quotes the text as a C string. */
    const char *terminator = line_bytes != NULL ? (const char *)memchr(line_bytes, '\0', line_size) : NULL;
    if (terminator != NULL) {
        line_size = (size_t)(terminator - line_bytes);
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
void tinypy_internal_compiler_memory_error(tinypy_compile_ctx_t *ctx) {
    tinypy_internal_exception_clear_raised(ctx->vm);
    tinypy_internal_compiler_error(ctx, TINYPY_ERROR_MEMORY, "memory allocation failed", 0, 0, ctx->out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_compiler_error_text(tinypy_compile_ctx_t *ctx, tinypy_error_kind_e error_kind, const char *message, int32_t line_number, int32_t column_offset, const char *text, size_t text_size, tinypy_error_t **out_error) {
    __tinypy_compiler_report(ctx, error_kind, message, line_number, column_offset, text, text_size, out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_compiler_operation_error(tinypy_compile_ctx_t *ctx, tinypy_error_kind_e error_kind, tinypy_error_t *error, const char *message, int32_t line_number, int32_t column_offset) {
    tinypy_error_kind_e reported_kind = error_kind;
    const char *reported_message = message;

    if (error != NULL) {
        reported_message = tinypy_error_message(error, NULL);
        if (tinypy_error_kind(error) == TINYPY_ERROR_MEMORY) {
            reported_kind = TINYPY_ERROR_MEMORY;
        }
    }
    tinypy_internal_exception_clear_raised(ctx->vm);
    tinypy_internal_compiler_error(ctx, reported_kind, reported_message, line_number, column_offset, ctx->out_error);
    if (error != NULL) {
        tinypy_error_release(error);
    }
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
    const char *text = NULL;
    size_t text_size = 0U;

    if (ctx->failed != 0) {
        return;
    }
    size_t start;
    size_t end;
    tinypy_bool_t newline;
    if (__tinypy_compiler_program_line(ctx, line_number, &start, &end, &newline) != 0) {
        /* PyErr_ProgramText reads the line through a 1000-byte buffer until
           a read ends the line: a longer line leaves its last part, and one
           that fills the buffer up to the end of the file nothing. */
        size_t count = end - start + (newline != 0 ? 1U : 0U);
        size_t offset = 0U;
        while (count - offset > TINYPY_COMPILER_PROGRAM_TEXT_READ_SIZE || (count - offset == TINYPY_COMPILER_PROGRAM_TEXT_READ_SIZE && newline == 0)) {
            offset += TINYPY_COMPILER_PROGRAM_TEXT_READ_SIZE;
        }
        start += offset < end - start ? offset : end - start;
        /* It quotes the line without its indentation. */
        while (start != end && (ctx->program_text.bytes[start] == ' ' || ctx->program_text.bytes[start] == '\t' || ctx->program_text.bytes[start] == '\f')) {
            start += 1U;
        }
        const char *parts[] = {(const char *)ctx->program_text.bytes + start, "\n"};
        size_t sizes[] = {end - start, offset != count && newline != 0 ? 1U : 0U};
        text = __tinypy_compiler_join_parts(ctx, parts, sizes, 2U);
        if (text == NULL) {
            tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", line_number, 0, ctx->out_error);
            return;
        }
        text_size = sizes[0] + sizes[1];
    }
    ctx->failed = 1;
    tinypy_internal_make_vm_error_location(ctx->vm, TINYPY_ERROR_SYNTAX, message, ctx->logical_filename, ctx->filename_size, line_number, -1, text, text_size, include_location, ctx->out_error);
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
/* The str() of an exception, copied into the arena, or the fixed message
   of the decoder when str() raises anything but a MemoryError; no message
   once an error was reported, which the compile keeps. */
static const char *__tinypy_compiler_exception_message(tinypy_compile_ctx_t *ctx, tinypy_value_t *exception, const char *unknown, size_t *out_size) {
    TINYPY_INCREF(exception);
    tinypy_vm_clear_error(ctx->vm);
    if (ctx->failed != 0) {
        TINYPY_DECREF(exception);
        return NULL;
    }
    tinypy_error_t *render_error = NULL;
    tinypy_value_t *rendered = tinypy_object_str(exception, &render_error);
    TINYPY_DECREF(exception);
    if (rendered == NULL && render_error != NULL && tinypy_error_kind(render_error) == TINYPY_ERROR_MEMORY) {
        tinypy_internal_compiler_operation_error(ctx, TINYPY_ERROR_MEMORY, render_error, "memory allocation failed", 0, 0);
        return NULL;
    }
    if (rendered == NULL) {
        if (render_error != NULL) {
            tinypy_error_release(render_error);
        }
        tinypy_internal_exception_clear_raised(ctx->vm);
        *out_size = strlen(unknown);
        return unknown;
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
    const char *message = __tinypy_compiler_exception_message(ctx, exception, literal != 0 ? "unknown error" : "unknown decode error", &message_size);

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
/* The text bytes decode to in a codec with the strict error handler, the
   replacements of a handler registered for it included; the decoder keeps
   a sequence the bytes cut unless final. A failure leaves its message in
   the arena, or no message once a limit diagnostic was reported. The
   exception the codec raised is cleared. */
static tinypy_value_t *__tinypy_compiler_decode(tinypy_compile_ctx_t *ctx, const uint8_t *bytes, size_t size, tinypy_value_t *encoding, tinypy_bool_t final, size_t *out_consumed, const char **out_message, size_t *out_message_size) {
    tinypy_vm_t *vm = ctx->vm;
    tinypy_value_t *text = tinypy_string_from_bytes(vm, bytes, size);
    tinypy_error_t *decode_error = NULL;

    *out_message = NULL;
    *out_message_size = 0U;
    if (text == NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "source exceeds compiler byte limit", 0, 0, ctx->out_error);
        return NULL;
    }
    tinypy_value_t *decoded = tinypy_internal_text_codec(vm, text, encoding, NULL, TINYPY_TRUE, final, out_consumed, &decode_error);
    TINYPY_DECREF(text);
    if (decode_error != NULL) {
        tinypy_error_release(decode_error);
    }
    if (decoded != NULL) {
        return decoded;
    }
    if (vm->raised_value == NULL) {
        ctx->failed = TINYPY_TRUE;
        return NULL;
    }
    *out_message = __tinypy_compiler_exception_message(ctx, vm->raised_value, "unknown decode error", out_message_size);
    tinypy_vm_clear_error(vm);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* The StreamReader of the UTF-8 or ASCII codec of Python 2, which reads a
   file after its cookie line when the cookie names the codec without a fast
   path. Its buffers are ranges of the bytes it reads: stream.read consumed
   those before position, the ones from decoded on wait in bytebuffer for
   the rest of their sequence, and the characters from buffered to decoded
   are charbuffer, or linebuffer while lines of them are cached. */
typedef struct tinypy_compiler_stream_reader_t {
    const uint8_t *bytes;
    size_t size;
    tinypy_bool_t utf8;
    size_t position;
    size_t decoded;
    size_t buffered;
    size_t buffered_characters;
    size_t cached_lines;
    /* The data of the read that raised. */
    size_t error_begin;
    size_t error_end;
} tinypy_compiler_stream_reader_t;
//////////////////////////////////////////////////////////////////////////
/* The stateful decoder: the end of the characters the bytes from begin to
   end decode to, which stops before a sequence the data cuts and at the
   first invalid one. The UTF-8 codec of Python 2 accepts surrogates. */
static size_t __tinypy_compiler_stream_decode(const tinypy_compiler_stream_reader_t *reader, size_t begin, size_t end, tinypy_bool_t *out_invalid) {
    const uint8_t *bytes = reader->bytes;
    size_t offset = begin;

    *out_invalid = TINYPY_FALSE;
    while (offset < end) {
        uint8_t first = bytes[offset];

        if (first < 0x80U) {
            offset += 1U;
            continue;
        }
        size_t length = reader->utf8 == 0 ? 0U : (first >= 0xc2U && first <= 0xdfU ? 2U : (first >= 0xe0U && first <= 0xefU ? 3U : (first >= 0xf0U && first <= 0xf4U ? 4U : 0U)));
        if (length > end - offset) {
            return offset;
        }
        tinypy_bool_t valid = length != 0U ? TINYPY_TRUE : TINYPY_FALSE;
        for (size_t index = 1U; index < length; ++index) {
            if ((bytes[offset + index] & 0xc0U) != 0x80U) {
                valid = TINYPY_FALSE;
            }
        }
        if (valid == 0 || (first == 0xe0U && bytes[offset + 1U] < 0xa0U) || (first == 0xf0U && bytes[offset + 1U] < 0x90U) || (first == 0xf4U && bytes[offset + 1U] > 0x8fU)) {
            *out_invalid = TINYPY_TRUE;
            return offset;
        }
        offset += length;
    }
    return offset;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_compiler_stream_characters(const tinypy_compiler_stream_reader_t *reader, size_t begin, size_t end) {
    size_t count = 0U;

    for (size_t offset = begin; offset < end; ++offset) {
        if ((reader->bytes[offset] & 0xc0U) != 0x80U) {
            count += 1U;
        }
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
/* The end of the first line unicode.splitlines finds in the characters from
   begin to end, after its line break; zero when they hold no line break. */
static size_t __tinypy_compiler_stream_line_end(const tinypy_compiler_stream_reader_t *reader, size_t begin, size_t end) {
    const uint8_t *bytes = reader->bytes;

    for (size_t offset = begin; offset < end; ++offset) {
        uint8_t byte = bytes[offset];

        if (byte == '\r') {
            size_t line_end = offset + 1U < end && bytes[offset + 1U] == '\n' ? offset + 2U : offset + 1U;
            return line_end;
        }
        if (byte == '\n' || byte == 0x0bU || byte == 0x0cU || (byte >= 0x1cU && byte <= 0x1eU)) {
            return offset + 1U;
        }
        if (byte == 0xc2U && offset + 1U < end && bytes[offset + 1U] == 0x85U) {
            return offset + 2U;
        }
        if (byte == 0xe2U && offset + 2U < end && bytes[offset + 1U] == 0x80U && (bytes[offset + 2U] == 0xa8U || bytes[offset + 2U] == 0xa9U)) {
            return offset + 3U;
        }
    }
    return 0U;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_compiler_stream_line_count(const tinypy_compiler_stream_reader_t *reader, size_t begin, size_t end) {
    size_t count = 0U;

    while (begin != end) {
        size_t line_end = __tinypy_compiler_stream_line_end(reader, begin, end);

        count += 1U;
        if (line_end == 0U) {
            break;
        }
        begin = line_end;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_compiler_stream_take(tinypy_compiler_stream_reader_t *reader, size_t end) {
    reader->buffered_characters -= __tinypy_compiler_stream_characters(reader, reader->buffered, end);
    reader->buffered = end;
}
//////////////////////////////////////////////////////////////////////////
/* StreamReader.read(size, chars=size): stream reads of read_size bytes
   until that many characters are buffered, then the first read_size of
   them. A read whose data holds an invalid sequence raises unless, read for
   a first line, the characters before the sequence make more than one line;
   those are buffered, the rest is left for the next read. FALSE when a read
   raises. */
static tinypy_bool_t __tinypy_compiler_stream_read(tinypy_compiler_stream_reader_t *reader, size_t read_size, tinypy_bool_t first_line, size_t *out_end) {
    reader->cached_lines = 0U;
    while (reader->buffered_characters < read_size) {
        size_t data_begin = reader->decoded;
        size_t read_end = read_size < reader->size - reader->position ? reader->position + read_size : reader->size;
        tinypy_bool_t eof = read_end == reader->position ? TINYPY_TRUE : TINYPY_FALSE;

        reader->position = read_end;
        tinypy_bool_t invalid;
        size_t decoded = __tinypy_compiler_stream_decode(reader, data_begin, read_end, &invalid);
        if (invalid != 0 && (first_line == 0 || __tinypy_compiler_stream_line_count(reader, data_begin, decoded) <= 1U)) {
            reader->error_begin = data_begin;
            reader->error_end = read_end;
            return TINYPY_FALSE;
        }
        reader->buffered_characters += __tinypy_compiler_stream_characters(reader, data_begin, decoded);
        reader->decoded = decoded;
        if (eof != 0) {
            break;
        }
    }
    size_t end = reader->buffered;
    for (size_t count = 0U; count != read_size && end != reader->decoded; ++count) {
        end += 1U;
        while (end != reader->decoded && (reader->bytes[end] & 0xc0U) == 0x80U) {
            end += 1U;
        }
    }
    __tinypy_compiler_stream_take(reader, end);
    *out_end = end;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* StreamReader.readline: the first cached line, or the first line of reads
   of 72 bytes, doubled while no line ends, which caches the further lines
   when there are at least two of them. FALSE when a read raises. */
static tinypy_bool_t __tinypy_compiler_stream_readline(tinypy_compiler_stream_reader_t *reader, size_t *out_begin, size_t *out_end) {
    size_t line_begin = reader->buffered;
    size_t scanned = line_begin;
    size_t read_size = TINYPY_COMPILER_STREAM_READ_SIZE;

    *out_begin = line_begin;
    if (reader->cached_lines != 0U) {
        size_t line_end = __tinypy_compiler_stream_line_end(reader, line_begin, reader->decoded);

        reader->cached_lines = reader->cached_lines > 2U ? reader->cached_lines - 1U : 0U;
        __tinypy_compiler_stream_take(reader, line_end);
        *out_end = line_end;
        return TINYPY_TRUE;
    }
    for (;;) {
        size_t data_begin = reader->buffered;
        size_t data_end;
        if (__tinypy_compiler_stream_read(reader, read_size, TINYPY_TRUE, &data_end) == 0) {
            return TINYPY_FALSE;
        }
        /* A line that may end with "\r\n" reads one more character. */
        if (data_end != data_begin && reader->bytes[data_end - 1U] == '\r') {
            if (__tinypy_compiler_stream_read(reader, 1U, TINYPY_FALSE, &data_end) == 0) {
                return TINYPY_FALSE;
            }
        }
        size_t line_end = __tinypy_compiler_stream_line_end(reader, scanned, data_end);
        if (line_end != 0U) {
            size_t further_lines = __tinypy_compiler_stream_line_count(reader, line_end, data_end);

            reader->cached_lines = further_lines > 1U ? further_lines : 0U;
            reader->buffered_characters += __tinypy_compiler_stream_characters(reader, line_end, data_end);
            reader->buffered = line_end;
            *out_end = line_end;
            return TINYPY_TRUE;
        }
        if (data_end == data_begin) {
            *out_end = data_end;
            return TINYPY_TRUE;
        }
        scanned = data_end;
        if (read_size < TINYPY_COMPILER_STREAM_READ_LIMIT) {
            read_size *= 2U;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
/* decoding_fgets reads a line of the tokenizer through readline calls until
   one returns a newline or nothing, the first as decoding_feof checks for
   the end of the file, and the line count tells the line a read that
   raises fails. At the end of the file a sequence the file cuts is left in
   bytebuffer, unread. */
static tinypy_bool_t __tinypy_compiler_stream_lines(tinypy_compiler_stream_reader_t *reader, size_t *out_line_count) {
    size_t line_count = 0U;

    for (;;) {
        *out_line_count = line_count;
        size_t begin;
        size_t end;
        if (__tinypy_compiler_stream_readline(reader, &begin, &end) == 0) {
            return TINYPY_FALSE;
        }
        if (begin == end) {
            return TINYPY_TRUE;
        }
        while (reader->bytes[end - 1U] != '\n') {
            if (__tinypy_compiler_stream_readline(reader, &begin, &end) == 0) {
                return TINYPY_FALSE;
            }
            if (begin == end) {
                break;
            }
        }
        line_count += 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
/* A file whose cookie names the UTF-8 or ASCII codec without a fast path is
   read after the cookie line through the codec's StreamReader. The
   tokenizer sees a read that raises on the line it reads then; without
   one, the text the reads decode, which leaves out a sequence the end of
   the file cuts. The source becomes that text when an error handler
   registered for strict replaces the errors: the read its first error
   fails stands for each read from there on, and the rest of the file for
   the reads after it. */
static tinypy_bool_t __tinypy_compiler_file_decoding(tinypy_compile_ctx_t *ctx, const uint8_t **io_input, size_t *io_size, size_t rest_offset, int32_t cookie_line, tinypy_bool_t utf8) {
    const uint8_t *rest = *io_input + rest_offset;
    size_t size = *io_size - rest_offset;
    tinypy_compiler_stream_reader_t reader = {rest, size, utf8, 0U, 0U, 0U, 0U, 0U, 0U, 0U};

    tinypy_bool_t invalid;
    if (__tinypy_compiler_stream_decode(&reader, 0U, size, &invalid) == size) {
        return TINYPY_TRUE;
    }
    size_t line_count;
    if (__tinypy_compiler_stream_lines(&reader, &line_count) != 0) {
        *io_size = rest_offset + reader.decoded;
        return TINYPY_TRUE;
    }
    tinypy_value_t *encoding = TINYPY_RET(utf8 != 0 ? ctx->vm->internal_codec_utf8_name : ctx->vm->internal_codec_ascii_name);
    const char *message;
    size_t message_size;
    size_t consumed;
    tinypy_value_t *decoded = __tinypy_compiler_decode(ctx, rest + reader.error_begin, reader.error_end - reader.error_begin, encoding, TINYPY_FALSE, &consumed, &message, &message_size);
    tinypy_value_t *remainder = NULL;
    if (decoded != NULL) {
        size_t remainder_begin = reader.error_begin + consumed;

        remainder = __tinypy_compiler_decode(ctx, rest + remainder_begin, size - remainder_begin, encoding, TINYPY_FALSE, &consumed, &message, &message_size);
    }
    TINYPY_DECREF(encoding);
    if (remainder != NULL) {
        const char *parts[] = {(const char *)*io_input, (const char *)TINYPY_TEXT_BYTES(decoded), (const char *)TINYPY_TEXT_BYTES(remainder)};
        size_t sizes[] = {rest_offset + reader.error_begin, TINYPY_TEXT_BYTE_SIZE(decoded), TINYPY_TEXT_BYTE_SIZE(remainder)};
        const char *text = __tinypy_compiler_join_parts(ctx, parts, sizes, 3U);

        TINYPY_DECREF(remainder);
        TINYPY_DECREF(decoded);
        if (text == NULL) {
            tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "source normalization exceeds compiler arena limit", 0, 0, ctx->out_error);
            return TINYPY_FALSE;
        }
        *io_input = (const uint8_t *)text;
        *io_size = sizes[0] + sizes[1] + sizes[2];
        return TINYPY_TRUE;
    }
    if (decoded != NULL) {
        TINYPY_DECREF(decoded);
    }
    if (message == NULL) {
        return TINYPY_FALSE;
    }
    ctx->source_decode_line = cookie_line + 1 + (int32_t)line_count;
    ctx->source_decode_message = message;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Fails the compile with the error of a warning that could not be shown. */
static tinypy_bool_t __tinypy_compiler_warning_error(tinypy_compile_ctx_t *ctx, tinypy_error_t *error) {
    if (ctx->failed != 0 || ctx->out_error == NULL) {
        if (error != NULL) {
            tinypy_error_release(error);
        }
        ctx->failed = 1;
        return TINYPY_FALSE;
    }
    ctx->failed = 1;
    *ctx->out_error = error;
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_compiler_is_space(uint8_t byte) {
    tinypy_bool_t result = byte == ' ' || byte == '\t' || byte == 0x0bU || byte == 0x0cU ? TINYPY_TRUE : TINYPY_FALSE;
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* warnings.showwarning writes the warning and the stripped line linecache
   reads from the file to sys.stderr in one call. A None sys.stderr or a
   write raising IOError or UnicodeError loses the warning; a missing
   sys.stderr or any other failure fails the compile. */
tinypy_bool_t tinypy_internal_compiler_syntax_warning(tinypy_compile_ctx_t *ctx, const char *message, int32_t line_number) {
    static const char label[] = ": SyntaxWarning: ";
    tinypy_vm_t *vm = ctx->vm;
    char line_buffer[16];
    size_t line_size = 0U;
    uint32_t line = line_number > 0 ? (uint32_t)line_number : UINT32_C(1);

    if (vm->sys_module == NULL) {
        return TINYPY_TRUE;
    }
    tinypy_error_t *error = NULL;
    tinypy_value_t *stderr_value = tinypy_object_get_attr_value(vm->sys_module, vm->internal_stderr_key, &error);
    if (stderr_value == NULL) {
        tinypy_bool_t reported = __tinypy_compiler_warning_error(ctx, error);
        return reported;
    }
    if (TINYPY_VALUE_KIND(stderr_value) == TINYPY_VALUE_NONE) {
        TINYPY_DECREF(stderr_value);
        return TINYPY_TRUE;
    }
    do {
        line_buffer[sizeof(line_buffer) - 1U - line_size] = (char)('0' + line % UINT32_C(10));
        line /= UINT32_C(10);
        line_size += 1U;
    } while (line != 0U);
    const char *parts[] = {ctx->logical_filename, ":", line_buffer + sizeof(line_buffer) - line_size, label, message, "\n", "  ", NULL, "\n"};
    size_t sizes[] = {ctx->filename_size, 1U, line_size, sizeof(label) - 1U, strlen(message), 1U, 2U, 0U, 1U};
    size_t part_count = 6U;
    size_t start;
    size_t end;
    tinypy_bool_t newline;
    tinypy_bool_t found = __tinypy_compiler_program_line(ctx, line_number, &start, &end, &newline);
    if (found != 0 && (start != end || newline != 0)) {
        while (start != end && __tinypy_compiler_is_space(ctx->program_text.bytes[start]) != 0) {
            start += 1U;
        }
        while (end != start && __tinypy_compiler_is_space(ctx->program_text.bytes[end - 1U]) != 0) {
            end -= 1U;
        }
        parts[7] = (const char *)ctx->program_text.bytes + start;
        sizes[7] = end - start;
        part_count = 9U;
    }
    char *text = __tinypy_compiler_join_parts(ctx, parts, sizes, part_count);
    if (text == NULL) {
        TINYPY_DECREF(stderr_value);
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", 0, 0, ctx->out_error);
        return TINYPY_FALSE;
    }
    size_t text_size = strlen(text);
    tinypy_bool_t written = tinypy_internal_output_write(stderr_value, text, text_size, &error);
    TINYPY_DECREF(stderr_value);
    if (written != 0) {
        return TINYPY_TRUE;
    }
    if (tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_IO_ERROR, &error) != 0) {
        return TINYPY_TRUE;
    }
    if (tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_UNICODE_ERROR, &error) != 0) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t reported = __tinypy_compiler_warning_error(ctx, error);
    return reported;
}
//////////////////////////////////////////////////////////////////////////
/* The -t warning of the tokenizer, written once per file like
   PySys_WriteStderr: to the host stderr when sys.stderr is missing or its
   write fails. */
tinypy_bool_t tinypy_internal_compiler_tab_warning(tinypy_compile_ctx_t *ctx) {
    static const char label[] = ": inconsistent use of tabs and spaces in indentation\n";
    tinypy_vm_t *vm = ctx->vm;
    const char *parts[] = {ctx->logical_filename, label};
    size_t sizes[] = {ctx->filename_size, sizeof(label) - 1U};
    char *message = __tinypy_compiler_join_parts(ctx, parts, sizes, 2U);

    if (message == NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "compiler diagnostic exceeds arena limit", 0, 0, ctx->out_error);
        return TINYPY_FALSE;
    }
    size_t message_size = sizes[0] + sizes[1];
    tinypy_value_t *stderr_value = vm->sys_module != NULL ? tinypy_module_get_value_key(vm->sys_module, vm->internal_stderr_key) : NULL;
    if (stderr_value != NULL) {
        TINYPY_INCREF(stderr_value);
        tinypy_error_t *error = NULL;
        tinypy_bool_t written = tinypy_internal_output_write(stderr_value, message, message_size, &error);
        TINYPY_DECREF(stderr_value);
        if (written != 0) {
            return TINYPY_TRUE;
        }
        if (error != NULL) {
            tinypy_error_release(error);
        }
        tinypy_internal_exception_clear_raised(vm);
    }
    tinypy_output_emit(vm, TINYPY_OUTPUT_STDERR, message, message_size);
    return TINYPY_TRUE;
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
        ctx->source_literals_encoded = *out_codec == TINYPY_INTERNAL_BUILTIN_CODEC_ASCII || *out_codec == TINYPY_INTERNAL_BUILTIN_CODEC_LATIN1 ? TINYPY_TRUE : TINYPY_FALSE;
        ctx->source_literals_ascii = *out_codec == TINYPY_INTERNAL_BUILTIN_CODEC_ASCII ? TINYPY_TRUE : TINYPY_FALSE;
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
/* PyUnicode_AsUTF8String encodes a surrogate pair as the code point it
   stands for: in the source of a unicode object, in a string the codec of
   its cookie decodes, and in the lines of a file the codec decodes, from
   begin on. */
static const uint8_t *__tinypy_compiler_join_surrogates(tinypy_compile_ctx_t *ctx, const uint8_t *source, size_t size, size_t begin, size_t *out_size) {
    size_t index = begin;

    *out_size = size;
    while (index < size && __tinypy_compiler_surrogate_pair(source + index, size - index) == 0U) {
        index += 1U;
    }
    if (index == size) {
        return source;
    }
    uint8_t *joined = (uint8_t *)tinypy_internal_compiler_arena_allocate_uninitialized(ctx, size);
    if (joined == NULL) {
        return NULL;
    }
    (void)memcpy(joined, source, index);
    size_t joined_size = index;
    while (index < size) {
        uint32_t code_point = __tinypy_compiler_surrogate_pair(source + index, size - index);

        if (code_point != 0U) {
            joined_size += tinypy_internal_utf8_encode(code_point, joined + joined_size);
            index += 6U;
            continue;
        }
        joined[joined_size++] = source[index++];
    }
    *out_size = joined_size;
    return joined;
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
    size_t universal_newlines_end = source_size;

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
    if (ctx->source_is_unicode != 0) {
        input = __tinypy_compiler_join_surrogates(ctx, input, source_size, 0U, &source_size);
        if (input == NULL) {
            return TINYPY_FALSE;
        }
    }
    if (source_size >= 3U && input[0] == 0xefU && input[1] == 0xbbU && input[2] == 0xbfU) {
        input_offset = 3U;
        bom = 1;
        latin1 = 0;
        ctx->source_encoding_declared = TINYPY_TRUE;
        ctx->source_diagnostic_utf8 = TINYPY_TRUE;
    }
    tinypy_bool_t newline_required = ctx->source_is_file == 0 && ctx->options.mode != TINYPY_COMPILE_EXEC ? TINYPY_TRUE : TINYPY_FALSE;
    if (__tinypy_compiler_encoding_cookie(input + input_offset, source_size - input_offset, newline_required, &cookie, &cookie_size, &cookie_line, &cookie_line_end) != 0) {
        if (__tinypy_compiler_apply_cookie(ctx, cookie, cookie_size, cookie_line, bom, &codec, &fast_path, &latin1, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    else if (bom == 0) {
        /* decoding_fgets: a file without a declared encoding is ASCII. */
        ctx->source_ascii_lines = INT32_MAX;
    }
    ctx->source_is_latin1 = latin1 != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    if (ctx->source_is_file != 0 && cookie != NULL && fast_path == 0) {
        size_t rest_offset = input_offset + cookie_line_end;

        /* decoding_feof asks the StreamReader for the next line, which ends
           the file at once: an open token reads no further newline. The
           tokenizer turns only the "\r\n" ending a line it reads, a newline
           supplied at the end of the file included, to "\n". */
        ctx->source_final_newline_added = TINYPY_TRUE;
        universal_newlines_end = rest_offset;
        if (codec == TINYPY_INTERNAL_BUILTIN_CODEC_ASCII || codec == TINYPY_INTERNAL_BUILTIN_CODEC_UTF8) {
            if (__tinypy_compiler_file_decoding(ctx, &input, &source_size, rest_offset, cookie_line, codec == TINYPY_INTERNAL_BUILTIN_CODEC_UTF8 ? TINYPY_TRUE : TINYPY_FALSE) == 0) {
                return TINYPY_FALSE;
            }
            input = __tinypy_compiler_join_surrogates(ctx, input, source_size, rest_offset, &source_size);
            if (input == NULL) {
                return TINYPY_FALSE;
            }
        }
    }
    /* Latin-1 bytes transcode to two UTF-8 bytes; a newline and the
       terminator may follow. */
    size_t expansion = latin1 != 0 ? 2U : 1U;
    if (source_size - input_offset > (SIZE_MAX - 3U) / expansion) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_MEMORY, "source is too large", 0, 0, out_error);
        return TINYPY_FALSE;
    }
    output_capacity = (source_size - input_offset) * expansion + 3U;
    output = (uint8_t *)tinypy_internal_compiler_arena_allocate(ctx, output_capacity);
    if (output == NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "source normalization exceeds compiler arena limit", 0, 0, out_error);
        return TINYPY_FALSE;
    }
    for (input_index = input_offset; input_index < source_size; ++input_index) {
        uint8_t byte = input[input_index];

        if (byte == '\r' && (input_index < universal_newlines_end || input_index + 1U == source_size || input[input_index + 1U] == '\n')) {
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
        else if (ctx->source_is_file != 0 && input[source_size - 1U] == '\r') {
            /* Py_UniversalNewlineFgets reads ahead after a CR, which at the
               end of the file sets the end-of-file flag decoding_feof
               checks: an open token reads no further newline. */
            ctx->source_final_newline_added = TINYPY_TRUE;
        }
    }
    output[output_size] = '\0';
    ctx->source.bytes = output;
    ctx->source.size = output_size;
    if (ctx->source_is_file != 0 || (codec != TINYPY_INTERNAL_BUILTIN_CODEC_ASCII && (codec != TINYPY_INTERNAL_BUILTIN_CODEC_UTF8 || fast_path != 0))) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t utf8 = codec == TINYPY_INTERNAL_BUILTIN_CODEC_UTF8 ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_value_t *encoding = TINYPY_RET(utf8 != 0 ? ctx->vm->internal_codec_utf8_name : ctx->vm->internal_codec_ascii_name);
    /* translate_into_utf8 decodes the whole string before tokenizing and
       encodes it again, with the replacements of an error handler
       registered for strict. */
    const char *message;
    size_t message_size;
    tinypy_value_t *decoded = __tinypy_compiler_decode(ctx, output, output_size, encoding, TINYPY_TRUE, NULL, &message, &message_size);
    TINYPY_DECREF(encoding);
    if (decoded == NULL) {
        if (message == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_compiler_stream_reader_t reader = {output, output_size, utf8, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
        tinypy_bool_t invalid;
        size_t decode_offset = __tinypy_compiler_stream_decode(&reader, 0U, output_size, &invalid);

        tinypy_internal_compiler_error_text(ctx, TINYPY_ERROR_SOURCE_DECODING, message, 0, 0, NULL, 0U, out_error);
        if (out_error != NULL && *out_error != NULL) {
            __tinypy_compiler_byte_position(output, decode_offset, &(*out_error)->line_number, &(*out_error)->column_offset);
        }
        return TINYPY_FALSE;
    }
    const char *text = (const char *)output;
    size_t text_size = TINYPY_TEXT_BYTE_SIZE(decoded);
    if (text_size != output_size || memcmp(TINYPY_TEXT_BYTES(decoded), output, output_size) != 0) {
        const char *decoded_text = (const char *)TINYPY_TEXT_BYTES(decoded);

        text = __tinypy_compiler_join_parts(ctx, &decoded_text, &text_size, 1U);
    }
    TINYPY_DECREF(decoded);
    if (text == NULL) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "source normalization exceeds compiler arena limit", 0, 0, out_error);
        return TINYPY_FALSE;
    }
    ctx->source.bytes = __tinypy_compiler_join_surrogates(ctx, (const uint8_t *)text, text_size, 0U, &ctx->source.size);
    tinypy_bool_t prepared = ctx->source.bytes != NULL ? TINYPY_TRUE : TINYPY_FALSE;
    return prepared;
}
