#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_compiler_hex_value(uint8_t byte) {
    if (byte >= '0' && byte <= '9') {
        return (int32_t)(byte - '0');
    }
    if (byte >= 'a' && byte <= 'f') {
        return (int32_t)(byte - 'a' + 10);
    }
    if (byte >= 'A' && byte <= 'F') {
        return (int32_t)(byte - 'A' + 10);
    }
    return -1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_compiler_integer_digit(uint8_t byte, uint32_t base) {
    int32_t digit = __tinypy_compiler_hex_value(byte);

    return digit >= 0 && (uint32_t)digit < base ? digit : -1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_compiler_literal_error(tinypy_compile_ctx_t *ctx, const char *message, int32_t line_number, int32_t column_offset) {
    tinypy_internal_compiler_error(ctx, TINYPY_ERROR_SYNTAX, message, line_number, column_offset, ctx->out_error);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_compiler_byte_escape_error(tinypy_compile_ctx_t *ctx, const char *message, int32_t line_number, int32_t column_offset) {
    tinypy_internal_compiler_error(ctx, TINYPY_ERROR_VALUE, message, line_number, column_offset, ctx->out_error);
}
//////////////////////////////////////////////////////////////////////////
/* ast_for_atom turns the UnicodeError that decoding a literal raised into a
   SyntaxError; any other exception, one an error handler raised, propagates.
   Releases decode_error. */
static void __tinypy_compiler_literal_decode_failure(tinypy_compile_ctx_t *ctx, tinypy_error_t *decode_error, int32_t line_number) {
    tinypy_value_t *raised_type = ctx->vm->raised_type;

    if (raised_type != NULL && TINYPY_VALUE_KIND(raised_type) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)raised_type, ctx->vm->exception_types[TINYPY_EXCEPTION_UNICODE_ERROR]) != 0) {
        if (decode_error != NULL) {
            tinypy_error_release(decode_error);
        }
        tinypy_internal_compiler_decode_error(ctx, ctx->vm->raised_value, line_number, TINYPY_TRUE);
        return;
    }
    ctx->failed = TINYPY_TRUE;
    if (ctx->out_error != NULL) {
        *ctx->out_error = decode_error;
    }
    else if (decode_error != NULL) {
        tinypy_error_release(decode_error);
    }
}
//////////////////////////////////////////////////////////////////////////
/* Writes the escape decode_unicode gives a code point to the escape
   decoder. */
static size_t __tinypy_compiler_code_point_escape(uint8_t *output, uint32_t code_point) {
    static const char hex[] = "0123456789abcdef";

    output[0] = '\\';
    output[1] = 'U';
    for (size_t index = 0U; index != 8U; ++index) {
        output[2U + index] = (uint8_t)hex[(code_point >> ((7U - index) * 4U)) & 0xfU];
    }
    return 10U;
}
//////////////////////////////////////////////////////////////////////////
/* Moves the size bytes of an arena buffer to one with room for extra bytes
   more, width bytes for each of rest bytes of source after them and a
   terminator, when it has less; NULL when that exceeds the arena. */
static uint8_t *__tinypy_compiler_literal_reserve(tinypy_compile_ctx_t *ctx, uint8_t *buffer, size_t size, size_t *io_capacity, size_t extra, size_t rest, size_t width) {
    if (extra > SIZE_MAX - 1U - size || rest > (SIZE_MAX - 1U - size - extra) / width) {
        return NULL;
    }
    size_t needed = size + extra + rest * width + 1U;
    if (needed <= *io_capacity) {
        return buffer;
    }
    size_t capacity = needed > SIZE_MAX / 2U || needed > *io_capacity * 2U ? needed : *io_capacity * 2U;
    uint8_t *grown = (uint8_t *)tinypy_internal_compiler_arena_allocate_uninitialized(ctx, capacity);
    if (grown == NULL) {
        return NULL;
    }
    (void)memcpy(grown, buffer, size);
    *io_capacity = capacity;
    return grown;
}
//////////////////////////////////////////////////////////////////////////
/* An escape the escape decoders cannot decode goes to the strict error
   handler, which unicode_decode_call_errorhandler looks up at the first
   error of a literal and calls with the UnicodeDecodeError it creates then
   and updates for each error after it. */
typedef struct tinypy_compiler_escape_errors_t {
    const uint8_t *source;
    size_t size;
    tinypy_bool_t raw;
    int32_t line_number;
    tinypy_value_t *handler;
    tinypy_value_t *exception;
} tinypy_compiler_escape_errors_t;
//////////////////////////////////////////////////////////////////////////
static void __tinypy_compiler_escape_errors_release(tinypy_compiler_escape_errors_t *errors) {
    if (errors->handler != NULL) {
        TINYPY_DECREF(errors->handler);
        errors->handler = NULL;
    }
    if (errors->exception != NULL) {
        TINYPY_DECREF(errors->exception);
        errors->exception = NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
/* Calls the handler for the escape from start to end: the replacement it
   returns goes to the output, which keeps room for what the rest of the
   source decodes to, at most two bytes per byte, and the decoding goes on
   at the position it returns. A failure releases the errors. */
static TINYPY_NOINLINE tinypy_bool_t __tinypy_compiler_call_escape_handler(tinypy_compile_ctx_t *ctx, tinypy_compiler_escape_errors_t *errors, size_t start, size_t end, const char *reason, uint8_t **io_output, size_t *io_capacity, size_t *io_output_size, size_t *io_position) {
    tinypy_vm_t *vm = ctx->vm;
    tinypy_error_t *error = NULL;

    /* The raw decoder reports an escape from the first backslash of its run. */
    while (errors->raw != 0 && start != 0U && errors->source[start - 1U] == '\\') {
        start -= 1U;
    }
    if (errors->handler == NULL) {
        errors->handler = tinypy_internal_codecs_lookup_error(vm, vm->internal_codec_strict_name, &error);
        if (errors->handler == NULL) {
            __tinypy_compiler_literal_decode_failure(ctx, error, errors->line_number);
            return TINYPY_FALSE;
        }
    }
    if (errors->exception == NULL) {
        tinypy_value_t *encoding = TINYPY_RET(errors->raw != 0 ? vm->internal_codec_raw_unicode_escape_name : vm->internal_codec_unicode_escape_name);
        tinypy_value_t *input = tinypy_string_from_bytes(vm, errors->source, errors->size);
        tinypy_value_t *start_value = tinypy_integer_from_i64(vm, (int64_t)start);
        tinypy_value_t *end_value = tinypy_integer_from_i64(vm, (int64_t)end);
        tinypy_value_t *reason_value = tinypy_string_from_bytes(vm, reason, strlen(reason));
        tinypy_value_t *items[] = {encoding, input, start_value, end_value, reason_value};
        tinypy_value_t *args = tinypy_tuple_from_items(vm, items, 5U);

        errors->exception = tinypy_exception_new(vm->exception_types[TINYPY_EXCEPTION_UNICODE_DECODE_ERROR], args, &error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(reason_value);
        TINYPY_DECREF(end_value);
        TINYPY_DECREF(start_value);
        TINYPY_DECREF(input);
        TINYPY_DECREF(encoding);
        if (errors->exception == NULL) {
            __tinypy_compiler_escape_errors_release(errors);
            __tinypy_compiler_literal_decode_failure(ctx, error, errors->line_number);
            return TINYPY_FALSE;
        }
    }
    else {
        tinypy_internal_unicode_error_update(vm, errors->exception, reason, start, end);
    }
    size_t next;
    tinypy_value_t *replacement = tinypy_internal_codecs_call_error_handler(vm, errors->handler, errors->exception, TINYPY_TRUE, errors->size, &next, &error);
    if (replacement == NULL) {
        __tinypy_compiler_escape_errors_release(errors);
        __tinypy_compiler_literal_decode_failure(ctx, error, errors->line_number);
        return TINYPY_FALSE;
    }
    size_t replacement_size = TINYPY_TEXT_BYTE_SIZE(replacement);
    uint8_t *output = __tinypy_compiler_literal_reserve(ctx, *io_output, *io_output_size, io_capacity, replacement_size, errors->size - next, 2U);
    if (output == NULL) {
        TINYPY_DECREF(replacement);
        __tinypy_compiler_escape_errors_release(errors);
        __tinypy_compiler_literal_error(ctx, "string literal exceeds compiler arena limit", errors->line_number, 0);
        return TINYPY_FALSE;
    }
    (void)memcpy(output + *io_output_size, TINYPY_TEXT_BYTES(replacement), replacement_size);
    TINYPY_DECREF(replacement);
    *io_output = output;
    *io_output_size += replacement_size;
    *io_position = next;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* The decoding loop keeps its output and position in registers: the
   handler call works on copies of them. */
static TINYPY_ALWAYS_INLINE tinypy_bool_t __tinypy_compiler_unicode_escape_error(tinypy_compile_ctx_t *ctx, tinypy_compiler_escape_errors_t *errors, size_t start, size_t end, const char *reason, uint8_t **io_output, size_t *io_capacity, size_t *io_output_size, size_t *io_position) {
    uint8_t *output = *io_output;
    size_t capacity = *io_capacity;
    size_t output_size = *io_output_size;
    size_t position = *io_position;

    if (__tinypy_compiler_call_escape_handler(ctx, errors, start, end, reason, &output, &capacity, &output_size, &position) == 0) {
        return TINYPY_FALSE;
    }
    *io_output = output;
    *io_capacity = capacity;
    *io_output_size = output_size;
    *io_position = position;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* decode_utf8 decodes a run of non-ASCII bytes whose UTF-8 is invalid with
   the strict error handler, and decode_unicode writes the escape of each
   code point of the result, a replacement of the handler included; the
   prepared bytes keep room for six per byte of the rest of the source. */
static TINYPY_NOINLINE uint8_t *__tinypy_compiler_unicode_escape_run(tinypy_compile_ctx_t *ctx, const uint8_t *run, size_t run_size, size_t rest, uint8_t *prepared, size_t *io_capacity, size_t *io_size, int32_t line_number) {
    tinypy_value_t *input = tinypy_string_from_bytes(ctx->vm, run, run_size);
    tinypy_value_t *encoding = TINYPY_RET(ctx->vm->internal_codec_utf8_name);
    tinypy_error_t *decode_error = NULL;
    tinypy_value_t *decoded = tinypy_internal_text_codec(ctx->vm, input, encoding, NULL, TINYPY_TRUE, TINYPY_TRUE, NULL, &decode_error);

    TINYPY_DECREF(encoding);
    TINYPY_DECREF(input);
    if (decoded == NULL) {
        __tinypy_compiler_literal_decode_failure(ctx, decode_error, line_number);
        return NULL;
    }
    const uint8_t *bytes = TINYPY_TEXT_BYTES(decoded);
    size_t size = TINYPY_TEXT_BYTE_SIZE(decoded);
    uint8_t *output = size <= SIZE_MAX / 10U ? __tinypy_compiler_literal_reserve(ctx, prepared, *io_size, io_capacity, size * 10U, rest, 6U) : NULL;
    if (output == NULL) {
        TINYPY_DECREF(decoded);
        __tinypy_compiler_literal_error(ctx, "string literal exceeds compiler arena limit", line_number, 0);
        return NULL;
    }
    size_t offset = 0U;
    while (offset < size) {
        uint32_t code_point;

        offset += tinypy_internal_utf8_decode(bytes + offset, size - offset, &code_point);
        *io_size += __tinypy_compiler_code_point_escape(output + *io_size, code_point);
    }
    TINYPY_DECREF(decoded);
    return output;
}
//////////////////////////////////////////////////////////////////////////
/* Prepare the byte domain consumed by the Python 2 Unicode escape decoder.
   The Latin-1 bytes Python tokenized occupy one byte each; every other
   character occupies ten escape bytes. Like decode_unicode, a backslash
   takes the next character along: an ASCII one unchanged, a UTF-8 one as
   the escape of the backslash itself. */
static const uint8_t *__tinypy_compiler_unicode_escape_source(tinypy_compile_ctx_t *ctx, const uint8_t *source, size_t size, tinypy_bool_t latin1, size_t *out_size, int32_t line_number, int32_t column_offset) {
    size_t first_non_ascii = 0U;
    while (first_non_ascii < size && source[first_non_ascii] < 0x80U) {
        first_non_ascii += 1U;
    }
    if (first_non_ascii == size) {
        *out_size = size;
        return source;
    }
    size_t expansion = latin1 != 0 ? 1U : 6U;
    if (size > (SIZE_MAX - 1U) / expansion) {
        __tinypy_compiler_literal_error(ctx, "string literal exceeds compiler arena limit", line_number, column_offset);
        return NULL;
    }
    size_t capacity = size * expansion + 1U;
    uint8_t *prepared = (uint8_t *)tinypy_internal_compiler_arena_allocate_uninitialized(ctx, capacity);
    size_t offset = 0U;
    size_t output_size = 0U;

    if (prepared == NULL) {
        __tinypy_compiler_literal_error(ctx, "string literal exceeds compiler arena limit", line_number, column_offset);
        return NULL;
    }
    while (offset < size) {
        uint8_t byte = source[offset];
        if (byte < 0x80U) {
            prepared[output_size++] = byte;
            offset += 1U;
            if (latin1 != 0 || byte != '\\' || offset == size) {
                continue;
            }
            if (source[offset] < 0x80U) {
                prepared[output_size++] = source[offset];
                offset += 1U;
                continue;
            }
            (void)memcpy(prepared + output_size, "u005c", 5U);
            output_size += 5U;
            continue;
        }
        uint32_t code_point;
        size_t width = tinypy_internal_utf8_decode(source + offset, size - offset, &code_point);

        if (width == 0U) {
            size_t run_start = offset;
            size_t run_end = offset;
            while (run_start != 0U && source[run_start - 1U] >= 0x80U) {
                run_start -= 1U;
            }
            while (run_end < size && source[run_end] >= 0x80U) {
                run_end += 1U;
            }
            /* The run decodes at once: its characters before the invalid
               sequence give their output back. */
            for (size_t index = run_start; index != offset; ++index) {
                if ((source[index] & 0xc0U) != 0x80U) {
                    output_size -= latin1 != 0 ? 1U : 10U;
                }
            }
            prepared = __tinypy_compiler_unicode_escape_run(ctx, source + run_start, run_end - run_start, size - run_end, prepared, &capacity, &output_size, line_number);
            if (prepared == NULL) {
                return NULL;
            }
            offset = run_end;
            continue;
        }
        if (latin1 != 0) {
            prepared[output_size++] = (uint8_t)code_point;
        }
        else {
            output_size += __tinypy_compiler_code_point_escape(prepared + output_size, code_point);
        }
        offset += width;
    }
    *out_size = output_size;
    return prepared;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_compiler_parse_integer(tinypy_compile_ctx_t *ctx, const char *text, size_t size, int32_t force_long, int32_t line_number, int32_t column_offset) {
    size_t position = 0U;
    int32_t sign = 1;
    uint32_t base = 10U;
    uint64_t magnitude = 0U;
    int32_t fits_i64 = 1;

    if (position < size && (text[position] == '+' || text[position] == '-')) {
        if (text[position] == '-') {
            sign = -1;
        }
        position += 1U;
    }
    if (position + 2U <= size && text[position] == '0' && (text[position + 1U] == 'x' || text[position + 1U] == 'X')) {
        base = 16U;
        position += 2U;
    }
    else if (position + 2U <= size && text[position] == '0' && (text[position + 1U] == 'b' || text[position + 1U] == 'B')) {
        base = 2U;
        position += 2U;
    }
    else if (position + 2U <= size && text[position] == '0' && (text[position + 1U] == 'o' || text[position + 1U] == 'O')) {
        base = 8U;
        position += 2U;
    }
    else if (position + 1U < size && text[position] == '0') {
        base = 8U;
        position += 1U;
    }
    size_t digits_start = position;

    while (position < size) {
        int32_t digit = __tinypy_compiler_integer_digit((uint8_t)text[position], base);

        if (digit < 0) {
            __tinypy_compiler_literal_error(ctx, "invalid integer literal", line_number, column_offset);
            return NULL;
        }
        if (fits_i64 != 0) {
            uint64_t limit = sign < 0 ? UINT64_C(0x8000000000000000) : UINT64_C(0x7fffffffffffffff);

            if (magnitude > (limit - (uint64_t)digit) / (uint64_t)base) {
                fits_i64 = 0;
            }
            else {
                magnitude = magnitude * (uint64_t)base + (uint64_t)digit;
            }
        }
        position += 1U;
    }
    if (force_long == 0 && fits_i64 != 0) {
        int64_t value;

        if (sign < 0 && magnitude == UINT64_C(0x8000000000000000)) {
            value = INT64_MIN;
        }
        else {
            value = sign < 0 ? -(int64_t)magnitude : (int64_t)magnitude;
        }
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(ctx->vm, value);
        return return_value_1;
    }
    size_t capacity = (size - digits_start) / 2U + 2U;
    uint16_t *digits = (uint16_t *)tinypy_internal_compiler_arena_allocate(ctx, capacity * sizeof(*digits));

    if (digits == NULL) {
        __tinypy_compiler_literal_error(ctx, "integer literal exceeds compiler arena limit", line_number, column_offset);
        return NULL;
    }
    size_t digit_count = tinypy_internal_long_digits_from_text((const uint8_t *)text + digits_start, size - digits_start, base, digits);

    if (digit_count == 0U) {
        sign = 0;
    }
    tinypy_value_t *result = tinypy_internal_long_from_base15_digits_checked(ctx->vm, sign, digits, digit_count, NULL);
    if (result == NULL) {
        tinypy_internal_compiler_memory_error(ctx);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_compiler_parse_number(tinypy_compile_ctx_t *ctx, const char *text, int32_t line_number, int32_t column_offset) {
    size_t size;
    size_t numeric_start = 0U;
    int32_t imaginary = 0;
    int32_t force_long = 0;
    int32_t prefixed_integer = 0;
    size_t index;

    size = strlen(text);
    if (text[size - 1U] == 'j' || text[size - 1U] == 'J') {
        imaginary = 1;
        size -= 1U;
    }
    else if (text[size - 1U] == 'l' || text[size - 1U] == 'L') {
        force_long = 1;
        size -= 1U;
    }
    if (numeric_start < size && (text[numeric_start] == '+' || text[numeric_start] == '-')) {
        numeric_start += 1U;
    }
    if (numeric_start + 2U <= size && text[numeric_start] == '0' && (text[numeric_start + 1U] == 'x' || text[numeric_start + 1U] == 'X' || text[numeric_start + 1U] == 'b' || text[numeric_start + 1U] == 'B' || text[numeric_start + 1U] == 'o' || text[numeric_start + 1U] == 'O')) {
        prefixed_integer = 1;
    }
    for (index = 0U; index < size; ++index) {
        if (prefixed_integer == 0 && (text[index] == '.' || text[index] == 'e' || text[index] == 'E')) {
            double value;

            if (tinypy_internal_compiler_decimal_double(ctx, text, size, &value, line_number, column_offset) == 0) {
                if (ctx->failed != 0) {
                    return NULL;
                }
                __tinypy_compiler_literal_error(ctx, "invalid floating-point literal", line_number, column_offset);
                return NULL;
            }
            if (imaginary != 0) {
                tinypy_value_t *return_value_1 = tinypy_complex_from_doubles(ctx->vm, 0.0, value);
                return return_value_1;
            }
            tinypy_value_t *return_value_2 = tinypy_float_from_double(ctx->vm, value);
            return return_value_2;
        }
    }
    if (imaginary != 0) {
        double value;

        if (tinypy_internal_compiler_decimal_double(ctx, text, size, &value, line_number, column_offset) == 0) {
            if (ctx->failed != 0) {
                return NULL;
            }
            __tinypy_compiler_literal_error(ctx, "invalid complex literal", line_number, column_offset);
            return NULL;
        }
        tinypy_value_t *return_value_3 = tinypy_complex_from_doubles(ctx->vm, 0.0, value);
        return return_value_3;
    }
    tinypy_value_t *return_value_4 = __tinypy_compiler_parse_integer(ctx, text, size, force_long, line_number, column_offset);
    return return_value_4;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_compiler_utf8_append(uint8_t *output, size_t capacity, size_t *size, uint32_t code_point) {
    size_t needed;

    if (code_point <= 0x7fU) {
        needed = 1U;
    }
    else if (code_point <= 0x7ffU) {
        needed = 2U;
    }
    else if (code_point <= 0xffffU) {
        needed = 3U;
    }
    else if (code_point <= 0x10ffffU) {
        needed = 4U;
    }
    else {
        return TINYPY_FALSE;
    }
    if (needed > capacity - *size) {
        return TINYPY_FALSE;
    }
    if (needed == 1U) {
        output[(*size)++] = (uint8_t)code_point;
    }
    else if (needed == 2U) {
        output[(*size)++] = (uint8_t)(0xc0U | (code_point >> 6U));
        output[(*size)++] = (uint8_t)(0x80U | (code_point & 0x3fU));
    }
    else if (needed == 3U) {
        output[(*size)++] = (uint8_t)(0xe0U | (code_point >> 12U));
        output[(*size)++] = (uint8_t)(0x80U | ((code_point >> 6U) & 0x3fU));
        output[(*size)++] = (uint8_t)(0x80U | (code_point & 0x3fU));
    }
    else {
        output[(*size)++] = (uint8_t)(0xf0U | (code_point >> 18U));
        output[(*size)++] = (uint8_t)(0x80U | ((code_point >> 12U) & 0x3fU));
        output[(*size)++] = (uint8_t)(0x80U | ((code_point >> 6U) & 0x3fU));
        output[(*size)++] = (uint8_t)(0x80U | (code_point & 0x3fU));
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_compiler_escape_value(uint8_t byte) {
    if (byte == 'a') {
        return 7;
    }
    if (byte == 'b') {
        return 8;
    }
    if (byte == 'f') {
        return 12;
    }
    if (byte == 'n') {
        return 10;
    }
    if (byte == 'r') {
        return 13;
    }
    if (byte == 't') {
        return 9;
    }
    if (byte == 'v') {
        return 11;
    }
    if (byte == '\\' || byte == '\'' || byte == '"') {
        return (int32_t)byte;
    }
    return -1;
}
//////////////////////////////////////////////////////////////////////////
static uint8_t __tinypy_compiler_literal_source_byte(tinypy_compile_ctx_t *ctx, tinypy_bool_t unicode, const uint8_t *source, size_t content_end, size_t *position, uint8_t byte) {
    if (unicode == 0 && ctx->source_is_latin1 != 0 && (byte == 0xc2U || byte == 0xc3U) && *position < content_end && (source[*position] & 0xc0U) == 0x80U) {
        uint8_t continuation = source[*position];

        *position += 1U;
        return (uint8_t)(((byte & 0x03U) << 6U) | (continuation & 0x3fU));
    }
    return byte;
}
//////////////////////////////////////////////////////////////////////////
/* parsestr encodes a byte literal of a source with an ASCII cookie back
   with the strict ASCII encoder, which fails on the characters an error
   handler replaced invalid source bytes with: over the whole literal when
   it is raw or holds no backslash, in PyString_DecodeEscape over the first
   run of them otherwise. */
static TINYPY_NOINLINE tinypy_bool_t __tinypy_compiler_ascii_literal(tinypy_compile_ctx_t *ctx, const uint8_t *content, size_t size, tinypy_bool_t raw, int32_t line_number) {
    size_t start = tinypy_internal_ascii_prefix(content, size);

    if (start == size) {
        return TINYPY_TRUE;
    }
    size_t end = start;
    if (raw != 0 || memchr(content, '\\', size) == NULL) {
        start = 0U;
        end = size;
    }
    while (end < size && content[end] >= 0x80U) {
        end += 1U;
    }
    tinypy_error_t *error = NULL;
    tinypy_value_t *text = tinypy_internal_unicode_from_utf8_checked(ctx->vm, (const char *)content + start, end - start, &error);
    tinypy_value_t *encoded = NULL;
    if (text != NULL) {
        encoded = tinypy_internal_text_codec(ctx->vm, text, ctx->vm->internal_codec_ascii_name, NULL, TINYPY_FALSE, TINYPY_TRUE, NULL, &error);
        TINYPY_DECREF(text);
    }
    if (encoded != NULL) {
        TINYPY_DECREF(encoded);
        return TINYPY_TRUE;
    }
    __tinypy_compiler_literal_decode_failure(ctx, error, line_number);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_compiler_parse_string(tinypy_compile_ctx_t *ctx, const char *text, tinypy_bool_t future_unicode, int32_t line_number, int32_t column_offset) {
    const uint8_t *source = (const uint8_t *)text;
    size_t size = strlen(text);
    size_t position = 0U;
    tinypy_bool_t raw = TINYPY_FALSE;
    tinypy_bool_t unicode = future_unicode;
    tinypy_bool_t decoded_escape = TINYPY_FALSE;
    uint8_t quote;
    size_t quote_size = 1U;
    size_t content_end;
    size_t capacity;
    uint8_t *output;
    size_t output_size = 0U;

    while (position < size && source[position] != '\'' && source[position] != '"') {
        uint8_t prefix = source[position];

        if (prefix == 'r' || prefix == 'R') {
            raw = 1;
        }
        else if (prefix == 'u' || prefix == 'U') {
            unicode = 1;
        }
        else if (prefix == 'b' || prefix == 'B') {
            unicode = 0;
        }
        else {
            __tinypy_compiler_literal_error(ctx, "invalid string prefix", line_number, column_offset);
            return NULL;
        }
        position += 1U;
    }
    if (position >= size) {
        __tinypy_compiler_literal_error(ctx, "invalid string literal", line_number, column_offset);
        return NULL;
    }
    quote = source[position];
    if (position + 2U < size && source[position + 1U] == quote && source[position + 2U] == quote) {
        quote_size = 3U;
    }
    position += quote_size;
    if (size < position + quote_size) {
        __tinypy_compiler_literal_error(ctx, "unterminated string literal", line_number, column_offset);
        return NULL;
    }
    content_end = size - quote_size;
    /* Python tokenizes the Latin-1 bytes of a source without a cookie or
       with one whose normal name is Latin-1, not the text a Latin-1 codec
       of the registry decodes. */
    tinypy_bool_t latin1 = unicode != 0 && ctx->source_is_latin1 != 0 && ctx->source_diagnostic_transcoded == 0 ? TINYPY_TRUE : TINYPY_FALSE;
    if (unicode != 0) {
        source = __tinypy_compiler_unicode_escape_source(ctx, source + position, content_end - position, latin1, &content_end, line_number, column_offset);
        if (source == NULL) {
            return NULL;
        }
        position = 0U;
    }
    else if (ctx->source_literals_ascii != 0) {
        if (__tinypy_compiler_ascii_literal(ctx, source + position, content_end - position, raw, line_number) == 0) {
            return NULL;
        }
    }
    /* The content decodes to at most one byte for each of its bytes, two
       for a Latin-1 byte of a unicode literal, escapes included, those
       prepared for the characters and replacements of a unicode literal
       too. The strict handler reserves room for a replacement it gives an
       escape. */
    size_t content_size = content_end - position;
    size_t width = latin1 != 0 ? 2U : 1U;
    if (content_size > (SIZE_MAX - 1U) / width) {
        __tinypy_compiler_literal_error(ctx, "string literal exceeds compiler arena limit", line_number, column_offset);
        return NULL;
    }
    capacity = content_size * width + 1U;
    output = (uint8_t *)tinypy_internal_compiler_arena_allocate_uninitialized(ctx, capacity);
    if (output == NULL) {
        __tinypy_compiler_literal_error(ctx, "string literal exceeds compiler arena limit", line_number, column_offset);
        return NULL;
    }
    tinypy_compiler_escape_errors_t errors = {source, content_end, raw, line_number, NULL, NULL};
    while (position < content_end) {
        uint8_t byte = source[position++];

        if (byte != '\\') {
            if (unicode != 0 && byte >= 0x80U) {
                (void)__tinypy_compiler_utf8_append(output, capacity, &output_size, byte);
                continue;
            }
            byte = __tinypy_compiler_literal_source_byte(ctx, unicode, source, content_end, &position, byte);
            output[output_size++] = byte;
            continue;
        }
        if (position == content_end) {
            output[output_size++] = '\\';
            break;
        }
        byte = source[position++];
        if (raw != 0 && !(unicode != 0 && (byte == 'u' || byte == 'U'))) {
            output[output_size++] = '\\';
            if (unicode != 0 && byte >= 0x80U) {
                (void)__tinypy_compiler_utf8_append(output, capacity, &output_size, byte);
                continue;
            }
            byte = __tinypy_compiler_literal_source_byte(ctx, unicode, source, content_end, &position, byte);
            output[output_size++] = byte;
            continue;
        }
        /* parsestr decodes every literal holding a backslash, a line
           continuation included, into a new string. */
        decoded_escape = TINYPY_TRUE;
        if (byte == '\n') {
            continue;
        }
        int32_t escaped = __tinypy_compiler_escape_value(byte);
        if (escaped >= 0) {
            output[output_size++] = (uint8_t)escaped;
            continue;
        }
        if (byte >= '0' && byte <= '7') {
            uint32_t value = (uint32_t)(byte - '0');
            size_t count = 1U;

            while (count < 3U && position < content_end && source[position] >= '0' && source[position] <= '7') {
                value = value * 8U + (uint32_t)(source[position] - '0');
                position += 1U;
                count += 1U;
            }
            if (unicode != 0) {
                (void)__tinypy_compiler_utf8_append(output, capacity, &output_size, value);
            }
            else {
                output[output_size++] = (uint8_t)(value & 0xffU);
            }
            continue;
        }
        if (byte == 'x' || (unicode != 0 && (byte == 'u' || byte == 'U'))) {
            size_t count = byte == 'x' ? 2U : (byte == 'u' ? 4U : 8U);
            uint32_t value = 0U;
            size_t index;

            size_t valid_count = 0U;
            while (valid_count < count && valid_count < content_end - position && __tinypy_compiler_hex_value(source[position + valid_count]) >= 0) {
                valid_count += 1U;
            }
            if (unicode != 0 && valid_count != count) {
                const char *reason = raw != 0 ? "truncated \\uXXXX" : (byte == 'x' ? "truncated \\xXX escape" : (byte == 'u' ? "truncated \\uXXXX escape" : "truncated \\UXXXXXXXX escape"));
                if (__tinypy_compiler_unicode_escape_error(ctx, &errors, position - 2U, position + valid_count, reason, &output, &capacity, &output_size, &position) == 0) {
                    return NULL;
                }
                continue;
            }
            if (count > content_end - position) {
                if (byte == 'x' && unicode == 0) {
                    __tinypy_compiler_byte_escape_error(ctx, "invalid \\x escape", line_number, column_offset);
                }
                else {
                    __tinypy_compiler_literal_error(ctx, "truncated escape sequence", line_number, column_offset);
                }
                return NULL;
            }
            for (index = 0U; index < count; ++index) {
                int32_t digit = __tinypy_compiler_hex_value(source[position + index]);

                if (digit < 0) {
                    if (byte == 'x' && unicode == 0) {
                        __tinypy_compiler_byte_escape_error(ctx, "invalid \\x escape", line_number, column_offset);
                    }
                    else {
                        __tinypy_compiler_literal_error(ctx, "invalid hexadecimal escape sequence", line_number, column_offset);
                    }
                    return NULL;
                }
                value = (value << 4U) | (uint32_t)digit;
            }
            position += count;
            if (unicode != 0) {
                if (__tinypy_compiler_utf8_append(output, capacity, &output_size, value) == 0) {
                    if (__tinypy_compiler_unicode_escape_error(ctx, &errors, position - count - 2U, position, raw != 0 ? "\\Uxxxxxxxx out of range" : "illegal Unicode character", &output, &capacity, &output_size, &position) == 0) {
                        return NULL;
                    }
                }
            }
            else {
                output[output_size++] = (uint8_t)value;
            }
            continue;
        }
        if (unicode != 0 && byte == 'N') {
            size_t name_start;
            size_t escape_start = position - 2U;
            uint32_t code_point;

            if (position == content_end || source[position] != '{') {
                if (__tinypy_compiler_unicode_escape_error(ctx, &errors, escape_start, position, "malformed \\N character escape", &output, &capacity, &output_size, &position) == 0) {
                    return NULL;
                }
                continue;
            }
            position += 1U;
            name_start = position;
            while (position < content_end && source[position] != '}') {
                position += 1U;
            }
            if (position == content_end || position == name_start) {
                if (__tinypy_compiler_unicode_escape_error(ctx, &errors, escape_start, position, "malformed \\N character escape", &output, &capacity, &output_size, &position) == 0) {
                    return NULL;
                }
                continue;
            }
            if (tinypy_internal_compiler_unicode_name((const char *)source + name_start, position - name_start, &code_point) == 0) {
                if (__tinypy_compiler_unicode_escape_error(ctx, &errors, escape_start, position + 1U, "unknown Unicode character name", &output, &capacity, &output_size, &position) == 0) {
                    return NULL;
                }
                continue;
            }
            position += 1U;
            if (__tinypy_compiler_utf8_append(output, capacity, &output_size, code_point) == 0) {
                __tinypy_compiler_literal_error(ctx, "invalid named Unicode escape", line_number, column_offset);
                return NULL;
            }
            continue;
        }
        byte = __tinypy_compiler_literal_source_byte(ctx, unicode, source, content_end, &position, byte);
        output[output_size++] = '\\';
        if (unicode != 0 && byte >= 0x80U) {
            (void)__tinypy_compiler_utf8_append(output, capacity, &output_size, byte);
            continue;
        }
        output[output_size++] = byte;
    }
    if (errors.handler != NULL) {
        __tinypy_compiler_escape_errors_release(&errors);
    }
    tinypy_value_t *result;

    if (unicode != 0) {
        result = tinypy_internal_unicode_from_utf8_checked(ctx->vm, (const char *)output, output_size, NULL);
    }
    else if (decoded_escape == 0 && ctx->source_literals_encoded == 0) {
        result = tinypy_internal_string_from_bytes_uninterned_checked(ctx->vm, output, output_size, NULL);
    }
    else {
        /* An escape-decoded or encoded literal is a new string in CPython,
           not one of the shared interned one-byte strings. */
        result = tinypy_internal_string_from_bytes_distinct_checked(ctx->vm, output, output_size, NULL);
    }
    if (result == NULL) {
        tinypy_internal_compiler_memory_error(ctx);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_compiler_join_strings(tinypy_compile_ctx_t *ctx, tinypy_value_t *const *parts, size_t count, tinypy_bool_t unicode) {
    size_t size = 0U;
    size_t index;

    for (index = 0U; index != count; ++index) {
        size += TINYPY_TEXT_BYTE_SIZE(parts[index]);
    }
    uint8_t *joined = (uint8_t *)tinypy_internal_compiler_arena_allocate_uninitialized(ctx, size);
    if (joined == NULL) {
        return NULL;
    }
    size_t offset = 0U;
    for (index = 0U; index != count; ++index) {
        size_t part_size = TINYPY_TEXT_BYTE_SIZE(parts[index]);

        (void)memcpy(joined + offset, TINYPY_TEXT_BYTES(parts[index]), part_size);
        offset += part_size;
    }
    tinypy_value_t *result = unicode != 0
        ? tinypy_internal_unicode_from_utf8_checked(ctx->vm, (const char *)joined, size, NULL)
        : tinypy_internal_string_from_bytes_uninterned_checked(ctx->vm, joined, size, NULL);
    if (result == NULL) {
        tinypy_internal_compiler_memory_error(ctx);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* PyUnicode_Concat decodes the accumulated byte prefix, then each byte
   part after the first unicode. Keep the actual default-codec replacement;
   an ASCII operand, which decodes to its own bytes, stays the str the final
   join reads. */
static tinypy_value_t *__tinypy_compiler_concat_unicode(tinypy_compile_ctx_t *ctx, tinypy_value_t *const *parts, size_t count, int32_t line_number) {
    tinypy_value_t *operand = count == 1U ? TINYPY_RET(parts[0]) : __tinypy_compiler_join_strings(ctx, parts, count, TINYPY_FALSE);
    tinypy_error_t *decode_error = NULL;

    if (operand == NULL) {
        return NULL;
    }
    if (tinypy_internal_ascii_prefix(TINYPY_TEXT_BYTES(operand), TINYPY_TEXT_BYTE_SIZE(operand)) == TINYPY_TEXT_BYTE_SIZE(operand)) {
        return operand;
    }
    tinypy_value_t *decoded = tinypy_internal_text_to_unicode(ctx->vm, operand, &decode_error);

    TINYPY_DECREF(operand);
    if (decoded == NULL) {
        __tinypy_compiler_literal_decode_failure(ctx, decode_error, line_number);
    }
    return decoded;
}
//////////////////////////////////////////////////////////////////////////
/* Adjacent literals, which parsestrplus folds pairwise, joined at once with
   the results of that fold: a part among empty ones stays itself when it has
   the type of the result, and joined str parts make a new string. */
tinypy_value_t *tinypy_internal_compiler_parse_strings(tinypy_compile_ctx_t *ctx, const char *const *texts, size_t count, tinypy_bool_t future_unicode, int32_t line_number, int32_t column_offset) {
    tinypy_value_t **parts = (tinypy_value_t **)tinypy_internal_compiler_arena_allocate_uninitialized(ctx, count * sizeof(*parts));
    if (parts == NULL) {
        return NULL;
    }
    tinypy_bool_t unicode = TINYPY_FALSE;
    size_t parsed = 0U;

    size_t index;

    for (index = 0U; index < count; ++index) {
        tinypy_value_t *part = __tinypy_compiler_parse_string(ctx, texts[index], future_unicode, line_number, column_offset);
        if (part == NULL) {
            break;
        }
        tinypy_bool_t part_unicode = TINYPY_VALUE_KIND(part) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
        if (parsed != 0U && part_unicode != unicode) {
            tinypy_value_t *decoded = part_unicode != 0
                ? __tinypy_compiler_concat_unicode(ctx, parts, parsed, line_number)
                : __tinypy_compiler_concat_unicode(ctx, &part, 1U, line_number);

            if (decoded == NULL) {
                TINYPY_DECREF(part);
                break;
            }
            if (part_unicode != 0) {
                while (parsed != 0U) {
                    parsed -= 1U;
                    TINYPY_DECREF(parts[parsed]);
                }
                parts[parsed++] = decoded;
            }
            else {
                TINYPY_DECREF(part);
                part = decoded;
            }
        }
        parts[parsed++] = part;
        if (part_unicode != 0) {
            unicode = TINYPY_TRUE;
        }
    }
    tinypy_value_t *result = NULL;

    if (index == count) {
        size_t non_empty_count = 0U;
        size_t alone = 0U;

        for (size_t item = 0U; item < parsed; ++item) {
            if (TINYPY_TEXT_BYTE_SIZE(parts[item]) != 0U) {
                non_empty_count += 1U;
                alone = item;
            }
            else if (non_empty_count == 0U) {
                alone = item;
            }
        }
        tinypy_value_type_e type = unicode != 0 ? TINYPY_VALUE_UNICODE : TINYPY_VALUE_STRING;

        result = non_empty_count <= 1U && TINYPY_VALUE_KIND(parts[alone]) == type
            ? TINYPY_RET(parts[alone])
            : __tinypy_compiler_join_strings(ctx, parts, parsed, unicode);
    }
    while (parsed != 0U) {
        parsed -= 1U;
        TINYPY_DECREF(parts[parsed]);
    }
    return result;
}
