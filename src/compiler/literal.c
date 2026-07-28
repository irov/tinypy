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
static tinypy_value_t *__tinypy_compiler_parse_integer(tinypy_compile_ctx_t *ctx, const char *text, size_t size, int32_t force_long, int32_t line_number, int32_t column_offset) {
    size_t position = 0U;
    int32_t sign = 1;
    uint32_t base = 10U;
    size_t capacity;
    uint16_t *digits;
    size_t digit_count = 0U;
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
    else if (position + 1U < size && text[position] == '0') {
        base = 8U;
        position += 1U;
    }
    capacity = size + 2U;
    digits = (uint16_t *)tinypy_internal_compiler_arena_allocate(ctx, capacity * sizeof(*digits));
    if (digits == NULL) {
        __tinypy_compiler_literal_error(ctx, "integer literal exceeds compiler arena limit", line_number, column_offset);
        return NULL;
    }
    while (position < size) {
        int32_t digit = __tinypy_compiler_integer_digit((uint8_t)text[position], base);
        uint32_t carry;
        size_t index;

        if (digit < 0) {
            __tinypy_compiler_literal_error(ctx, "invalid integer literal", line_number, column_offset);
            return NULL;
        }
        carry = (uint32_t)digit;
        for (index = 0U; index < digit_count; ++index) {
            uint32_t product = (uint32_t)digits[index] * base + carry;

            digits[index] = (uint16_t)(product & 0x7fffU);
            carry = product >> 15U;
        }
        while (carry != 0U) {
            digits[digit_count] = (uint16_t)(carry & 0x7fffU);
            digit_count += 1U;
            carry >>= 15U;
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
    while (digit_count != 0U && digits[digit_count - 1U] == 0U) {
        digit_count -= 1U;
    }
    if (digit_count == 0U) {
        sign = 0;
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
    tinypy_value_t *return_value_2 = tinypy_long_from_base15_digits(ctx->vm, sign, digits, digit_count);
    return return_value_2;
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
    if (numeric_start + 2U <= size && text[numeric_start] == '0' && (text[numeric_start + 1U] == 'x' || text[numeric_start + 1U] == 'X' || text[numeric_start + 1U] == 'b' || text[numeric_start + 1U] == 'B')) {
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

    if (code_point >= UINT32_C(0xd800) && code_point <= UINT32_C(0xdfff)) {
        return TINYPY_FALSE;
    }
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
tinypy_value_t *tinypy_internal_compiler_parse_string(tinypy_compile_ctx_t *ctx, const char *text, tinypy_bool_t future_unicode, int32_t line_number, int32_t column_offset) {
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
    capacity = (content_end - position) * 4U + 1U;
    output = (uint8_t *)tinypy_internal_compiler_arena_allocate(ctx, capacity);
    if (output == NULL) {
        __tinypy_compiler_literal_error(ctx, "string literal exceeds compiler arena limit", line_number, column_offset);
        return NULL;
    }
    while (position < content_end) {
        uint8_t byte = source[position++];

        if (byte != '\\') {
            output[output_size++] = byte;
            continue;
        }
        if (position == content_end) {
            output[output_size++] = '\\';
            break;
        }
        byte = source[position++];
        if (byte == '\n') {
            continue;
        }
        if (raw != 0 && !(unicode != 0 && (byte == 'u' || byte == 'U'))) {
            output[output_size++] = '\\';
            output[output_size++] = byte;
            continue;
        }
        decoded_escape = 1; {
            int32_t escaped = __tinypy_compiler_escape_value(byte);

            if (escaped >= 0) {
                output[output_size++] = (uint8_t)escaped;
                continue;
            }
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

            if (count > content_end - position) {
                __tinypy_compiler_literal_error(ctx, "truncated escape sequence", line_number, column_offset);
                return NULL;
            }
            for (index = 0U; index < count; ++index) {
                int32_t digit = __tinypy_compiler_hex_value(source[position + index]);

                if (digit < 0) {
                    __tinypy_compiler_literal_error(ctx, "invalid hexadecimal escape sequence", line_number, column_offset);
                    return NULL;
                }
                value = (value << 4U) | (uint32_t)digit;
            }
            position += count;
            if (unicode != 0) {
                if (__tinypy_compiler_utf8_append(output, capacity, &output_size, value) == 0) {
                    __tinypy_compiler_literal_error(ctx, "invalid Unicode escape", line_number, column_offset);
                    return NULL;
                }
            }
            else {
                output[output_size++] = (uint8_t)value;
            }
            continue;
        }
        if (unicode != 0 && byte == 'N') {
            size_t name_start;
            uint32_t code_point;

            if (position == content_end || source[position] != '{') {
                __tinypy_compiler_literal_error(ctx, "malformed named Unicode escape", line_number, column_offset);
                return NULL;
            }
            position += 1U;
            name_start = position;
            while (position < content_end && source[position] != '}') {
                position += 1U;
            }
            if (position == content_end || tinypy_internal_compiler_unicode_name((const char *)source + name_start, position - name_start, &code_point) == 0) {
                __tinypy_compiler_literal_error(ctx, "unknown Unicode character name", line_number, column_offset);
                return NULL;
            }
            position += 1U;
            if (__tinypy_compiler_utf8_append(output, capacity, &output_size, code_point) == 0) {
                __tinypy_compiler_literal_error(ctx, "invalid named Unicode escape", line_number, column_offset);
                return NULL;
            }
            continue;
        }
        output[output_size++] = '\\';
        output[output_size++] = byte;
    }
    if (unicode != 0) {
        tinypy_value_t *return_value_1 = tinypy_unicode_from_utf8(ctx->vm, (const char *)output, output_size);
        return return_value_1;
    }
    tinypy_value_t *result = tinypy_string_from_bytes(ctx->vm, output, output_size);

    if (decoded_escape != 0 && output_size != 0U) {
        tinypy_internal_string_set_interned(result, 0);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_compiler_concat_strings(tinypy_compile_ctx_t *ctx, tinypy_value_t *left, tinypy_value_t *right, int32_t line_number, int32_t column_offset) {
    tinypy_value_type_e left_type;
    tinypy_value_type_e right_type;
    const uint8_t *left_bytes;
    const uint8_t *right_bytes;
    size_t left_size;
    size_t right_size;
    uint8_t *joined;
    tinypy_bool_t unicode;
    size_t index;

    left_type = tinypy_typeof(left);
    right_type = tinypy_typeof(right);
    unicode = left_type == TINYPY_VALUE_UNICODE || right_type == TINYPY_VALUE_UNICODE;
    if (left_type == TINYPY_VALUE_UNICODE) {
        size_t code_point_count;

        left_bytes = (const uint8_t *)tinypy_unicode_utf8_view(left, &left_size, &code_point_count);
    }
    else {
        left_bytes = (const uint8_t *)tinypy_string_view(left, &left_size);
    }
    if (right_type == TINYPY_VALUE_UNICODE) {
        size_t code_point_count;

        right_bytes = (const uint8_t *)tinypy_unicode_utf8_view(right, &right_size, &code_point_count);
    }
    else {
        right_bytes = (const uint8_t *)tinypy_string_view(right, &right_size);
    }
    if (unicode != 0) {
        if (left_type == TINYPY_VALUE_STRING) {
            for (index = 0U; index < left_size; ++index) {
                if (left_bytes[index] >= 0x80U) {
                    goto non_ascii;
                }
            }
        }
        if (right_type == TINYPY_VALUE_STRING) {
            for (index = 0U; index < right_size; ++index) {
                if (right_bytes[index] >= 0x80U) {
                    goto non_ascii;
                }
            }
        }
    }
    joined = (uint8_t *)tinypy_internal_compiler_arena_allocate(ctx, left_size + right_size + 1U);
    if (joined == NULL) {
        __tinypy_compiler_literal_error(ctx, "concatenated string exceeds compiler arena limit", line_number, column_offset);
        return NULL;
    }
    (void)memcpy(joined, left_bytes, left_size);
    (void)memcpy(joined + left_size, right_bytes, right_size);
    if (unicode != 0) {
        tinypy_value_t *return_value_1 = tinypy_unicode_from_utf8(ctx->vm, (const char *)joined, left_size + right_size);
        return return_value_1;
    }
    tinypy_value_t *return_value_2 = tinypy_string_from_bytes(ctx->vm, joined, left_size + right_size);
    return return_value_2;

non_ascii:
    __tinypy_compiler_literal_error(ctx, "non-ASCII byte string cannot be combined with unicode literal", line_number, column_offset);
    return NULL;
}
