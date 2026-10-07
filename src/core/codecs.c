#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codecs_arguments(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_arity_style_e style, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *name = tinypy_native_function_name(function);
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_TEXT(name),
            TINYPY_MESSAGE_PART_LITERAL("() takes no keyword arguments"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    if (count < minimum || count > maximum) {
        tinypy_internal_make_arity_error(vm, (const char *)TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name), count, minimum, maximum, style, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codecs_require_text(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec name must be a string", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_codecs_validate_name(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (__tinypy_codecs_require_text(vm, value, out_error) == 0) {
        return TINYPY_FALSE;
    }
    const uint8_t *bytes = TINYPY_TEXT_BYTES(value);
    size_t size = TINYPY_TEXT_BYTE_SIZE(value);

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        for (size_t index = 0U; index < size; ++index) {
            if (bytes[index] >= 0x80U) {
                tinypy_value_t *converted = tinypy_internal_text_codec(vm, value, NULL, NULL, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);

                if (converted != NULL) {
                    TINYPY_DECREF(converted);
                }
                return TINYPY_FALSE;
            }
        }
    }
    if (memchr(bytes, 0, size) != NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec name contains a null character", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_normalize(tinypy_vm_t *vm, tinypy_value_t *name) {
    const uint8_t *source = TINYPY_TEXT_BYTES(name);
    size_t source_size = TINYPY_TEXT_BYTE_SIZE(name);
    uint8_t *normalized;
    size_t source_index;

    if (source_size == 0U) {
        tinypy_value_t *return_value_1 = TINYPY_RET_EMPTY_STRING(vm);
        return return_value_1;
    }
    normalized = (uint8_t *)tinypy_internal_vm_allocate(vm, source_size);
    for (source_index = 0U; source_index < source_size; ++source_index) {
        uint8_t character = source[source_index];

        if (character >= (uint8_t)'A' && character <= (uint8_t)'Z') {
            character = (uint8_t)(character + ('a' - 'A'));
        }
        normalized[source_index] = character == (uint8_t)' ' ? (uint8_t)'-' : character;
    }
    tinypy_value_t *result = tinypy_string_from_bytes(vm, normalized, source_size);
    tinypy_internal_vm_deallocate(vm, normalized, source_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_register(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    (void)user_data;

    if (__tinypy_codecs_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *search = TINYPY_TUPLE_GET(args, 0U);
    if (search->type->call == NULL && tinypy_internal_object_has_special_key(search, vm->internal_special_call_key) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "argument must be callable", out_error);
        return NULL;
    }
    tinypy_value_t *codecs_module_value = vm->codec_search_path;
    if (tinypy_internal_list_append_checked(codecs_module_value, search, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_lookup(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    (void)user_data;
    tinypy_value_t *cache;
    tinypy_value_t *search_path;

    if (__tinypy_codecs_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *argument_names[] = {vm->internal_encoding_key};
    tinypy_value_t *values[1];
    if (tinypy_internal_constructor_optional_arguments(vm, "lookup", 6U, args, NULL, argument_names, 1U, UINT32_C(1), values, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *name = values[0];
    tinypy_value_t *normalized = __tinypy_codecs_normalize(vm, name);
    cache = vm->codec_cache;
    tinypy_value_t *cached = tinypy_dict_get_optional(cache, normalized);

    if (cached != NULL) {
        TINYPY_INCREF(cached);
        TINYPY_DECREF(normalized);
        return cached;
    }
    search_path = vm->codec_search_path;
    for (size_t index = 0U; index < TINYPY_LIST_SIZE(search_path); ++index) {
        tinypy_value_t *search = TINYPY_RET(TINYPY_LIST_GET(search_path, index));
        tinypy_value_t *search_args = tinypy_tuple_from_items(vm, &normalized, 1U);
        tinypy_value_t *result = tinypy_call(search, search_args, NULL, out_error);

        TINYPY_DECREF(search_args);
        TINYPY_DECREF(search);
        if (result == NULL) {
            TINYPY_DECREF(normalized);
            return NULL;
        }
        if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_NONE) {
            if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(result) != 4U) {
                TINYPY_DECREF(result);
                TINYPY_DECREF(normalized);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec search functions must return 4-tuples", out_error);
                return NULL;
            }
            tinypy_dict_set(cache, normalized, result);
            TINYPY_DECREF(normalized);
            return result;
        }
        TINYPY_DECREF(result);
    }
    TINYPY_DECREF(normalized);
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("unknown encoding: "), TINYPY_MESSAGE_PART_TEXT(name),
    };
    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_LOOKUP, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_call_text(tinypy_vm_t *vm, tinypy_value_t *input, tinypy_bool_t decode, tinypy_value_t *encoding, tinypy_value_t *errors, tinypy_error_t **out_error) {
    if (__tinypy_codecs_require_text(vm, input, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_text_codec(vm, input, encoding, errors, decode, TINYPY_TRUE, NULL, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_codec_result(tinypy_vm_t *vm, tinypy_value_t *input, tinypy_value_t *converted) {
    size_t input_size = TINYPY_VALUE_KIND(input) == TINYPY_VALUE_UNICODE ? TINYPY_SIZED_SIZE(input) : TINYPY_TEXT_BYTE_SIZE(input);
    tinypy_value_t *consumed = tinypy_integer_from_i64(vm, (int64_t)input_size);
    tinypy_value_t *items[2] = {converted, consumed};
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);

    TINYPY_DECREF(consumed);
    TINYPY_DECREF(converted);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_text_input(tinypy_vm_t *vm, tinypy_value_t *input, tinypy_bool_t decode, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(input);
    const uint8_t *bytes;
    size_t size;

    if (decode == TINYPY_FALSE) {
        tinypy_value_t *result = tinypy_internal_string_argument_text(vm, input, TINYPY_TRUE, out_error);
        return result;
    }
    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        return TINYPY_RET(input);
    }
    if ((decode != 0 || kind == TINYPY_VALUE_BUFFER) && tinypy_internal_bytes_view(input, &bytes, &size) != 0) {
        tinypy_value_t *result = tinypy_internal_string_from_bytes_checked(vm, bytes, size, out_error);
        return result;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, decode != 0 ? "decoder requires a string or buffer" : "encoder requires a string or unicode", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_specific(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t operation = (intptr_t)user_data;
    tinypy_bool_t decode = (int32_t)(operation & (intptr_t)1);
    tinypy_value_t *encoding = operation < (intptr_t)2 || operation == (intptr_t)7 ? vm->internal_utf_hyphen_8_key : (operation < (intptr_t)4 ? vm->internal_codec_ascii_name : vm->internal_codec_latin1_name);
    size_t maximum = operation == (intptr_t)1 ? 3U : 2U;
    tinypy_value_t *errors = NULL;
    tinypy_value_t *converted;
    tinypy_bool_t final = TINYPY_FALSE;
    size_t consumed;

    if (__tinypy_codecs_arguments(function, args, kwargs, 1U, maximum, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *source = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *input;
    tinypy_value_type_e source_kind = TINYPY_VALUE_KIND(source);
    const uint8_t *source_bytes;
    size_t source_size;

    if (decode != 0 && source_kind == TINYPY_VALUE_UNICODE) {
        input = tinypy_internal_text_codec(vm, source, NULL, NULL, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);
        if (input == NULL) {
            return NULL;
        }
    }
    else {
        if (decode != TINYPY_FALSE && source_kind != TINYPY_VALUE_STRING && source_kind != TINYPY_VALUE_UNICODE && tinypy_internal_bytes_view(source, &source_bytes, &source_size) == TINYPY_FALSE) {
            tinypy_bool_t heap_type = (source->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U ? TINYPY_TRUE : TINYPY_FALSE;
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_TEXT(tinypy_native_function_name(function)),
                {heap_type != TINYPY_FALSE ? "() argument 1 must be convertible to a buffer, not " : "() argument 1 must be string or buffer, not ", heap_type != TINYPY_FALSE ? 51U : 44U},
                {source_kind == TINYPY_VALUE_NONE ? "None" : source->type->name, source_kind == TINYPY_VALUE_NONE ? 4U : source->type->name_size},
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            return NULL;
        }
        input = TINYPY_RET(source);
    }
    if (TINYPY_TUPLE_SIZE(args) >= 2U) {
        errors = TINYPY_TUPLE_GET(args, 1U);
        tinypy_value_type_e errors_kind = TINYPY_VALUE_KIND(errors);
        if (errors_kind == TINYPY_VALUE_NONE) {
            errors = NULL;
        }
        else {
            tinypy_bool_t is_text = errors_kind == TINYPY_VALUE_STRING || errors_kind == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
            tinypy_bool_t contains_null = is_text != TINYPY_FALSE && memchr(TINYPY_TEXT_BYTES(errors), 0, TINYPY_TEXT_BYTE_SIZE(errors)) != NULL ? TINYPY_TRUE : TINYPY_FALSE;

            if (is_text == TINYPY_FALSE || contains_null != TINYPY_FALSE) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_TEXT(tinypy_native_function_name(function)),
                    TINYPY_MESSAGE_PART_LITERAL("() argument 2 must be string"),
                    {contains_null != TINYPY_FALSE ? " without null bytes or None, not " : " or None, not ", contains_null != TINYPY_FALSE ? 33U : 14U},
                    TINYPY_MESSAGE_PART_TYPE_NAME(errors),
                };
                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
                goto cleanup_input;
            }
            if (tinypy_internal_codecs_validate_name(vm, errors, out_error) == TINYPY_FALSE) {
                goto cleanup_input;
            }
        }
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U) {
        int64_t integer;

        if (tinypy_internal_integer_as_ssize(TINYPY_TUPLE_GET(args, 2U), &integer, out_error) == 0) {
            goto cleanup_input;
        }
        if (integer < INT32_MIN || integer > INT32_MAX) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "Python int too large to convert to C int", out_error);
            goto cleanup_input;
        }
        final = integer != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    }
    tinypy_value_t *text_input = __tinypy_codecs_text_input(vm, input, decode, out_error);
    if (text_input == NULL) {
        goto cleanup_input;
    }
    converted = tinypy_internal_text_codec(vm, text_input, encoding, errors, decode, operation == (intptr_t)1 ? final : TINYPY_TRUE, &consumed, out_error);
    TINYPY_DECREF(text_input);
    TINYPY_DECREF(input);
    if (converted == NULL) {
        return NULL;
    }
    tinypy_value_t *consumed_value = tinypy_integer_from_i64(vm, (int64_t)consumed);
    tinypy_value_t *items[2] = {converted, consumed_value};
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);

    TINYPY_DECREF(consumed_value);
    TINYPY_DECREF(converted);
    return result;
cleanup_input:
    TINYPY_DECREF(input);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codecs_hex_digit(uint8_t character) {
    if (character >= (uint8_t)'0' && character <= (uint8_t)'9') {
        return (int32_t)(character - (uint8_t)'0');
    }
    if (character >= (uint8_t)'a' && character <= (uint8_t)'f') {
        return (int32_t)(character - (uint8_t)'a') + 10;
    }
    if (character >= (uint8_t)'A' && character <= (uint8_t)'F') {
        return (int32_t)(character - (uint8_t)'A') + 10;
    }
    return -1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_hex(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    static const uint8_t hexadecimal[] = "0123456789abcdef";
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_bool_t decode = (intptr_t)user_data != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_value_t *input;
    tinypy_value_t *bytes_value;
    tinypy_value_t *converted;
    tinypy_value_t *result;
    const uint8_t *source;
    size_t source_size;
    uint8_t *output;
    size_t index;

    if (__tinypy_codecs_arguments(function, args, kwargs, 1U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        int32_t strict = tinypy_compare_bool(TINYPY_TUPLE_GET(args, 1U), vm->internal_codec_strict_name, TINYPY_COMPARE_EQUAL, out_error);

        if (strict < 0) {
            return NULL;
        }
        if (strict == 0) {
            tinypy_value_t *empty = TINYPY_RET_EMPTY_TUPLE(vm);
            tinypy_value_t *exception = tinypy_exception_new(vm->exception_types[TINYPY_EXCEPTION_ASSERTION_ERROR], empty, out_error);

            TINYPY_DECREF(empty);
            if (exception != NULL) {
                (void)tinypy_exception_raise(exception, NULL, out_error);
                TINYPY_DECREF(exception);
            }
            return NULL;
        }
    }
    input = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_codecs_require_text(vm, input, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(input) == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *encoding = TINYPY_RET(vm->internal_codec_ascii_name);

        bytes_value = __tinypy_codecs_call_text(vm, input, TINYPY_FALSE, encoding, NULL, out_error);
        TINYPY_DECREF(encoding);
        if (bytes_value == NULL) {
            return NULL;
        }
    }
    else {
        bytes_value = TINYPY_RET(input);
    }
    if (TINYPY_VALUE_KIND(bytes_value) != TINYPY_VALUE_STRING) {
        TINYPY_DECREF(bytes_value);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "encoder did not return a string", out_error);
        return NULL;
    }
    source = TINYPY_TEXT_BYTES(bytes_value);
    source_size = TINYPY_TEXT_BYTE_SIZE(bytes_value);
    if (decode != 0) {
        if ((source_size & 1U) != 0U) {
            TINYPY_DECREF(bytes_value);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "Odd-length string", out_error);
            return NULL;
        }
        converted = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, source_size / 2U, source_size / 2U, &output, out_error);
        if (converted == NULL) {
            TINYPY_DECREF(bytes_value);
            return NULL;
        }
        for (index = 0U; index != source_size; index += 2U) {
            int32_t high = __tinypy_codecs_hex_digit(source[index]);
            int32_t low = __tinypy_codecs_hex_digit(source[index + 1U]);

            if (high < 0 || low < 0) {
                TINYPY_DECREF(converted);
                TINYPY_DECREF(bytes_value);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "Non-hexadecimal digit found", out_error);
                return NULL;
            }
            output[index / 2U] = (uint8_t)((high << 4) | low);
        }
    }
    else {
        if (source_size > SIZE_MAX / 2U) {
            TINYPY_DECREF(bytes_value);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "hex encoding is too large", out_error);
            return NULL;
        }
        converted = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, source_size * 2U, source_size * 2U, &output, out_error);
        if (converted == NULL) {
            TINYPY_DECREF(bytes_value);
            return NULL;
        }
        for (index = 0U; index != source_size; ++index) {
            output[index * 2U] = hexadecimal[source[index] >> 4];
            output[index * 2U + 1U] = hexadecimal[source[index] & 0x0fU];
        }
    }
    TINYPY_DECREF(bytes_value);
    result = __tinypy_codecs_codec_result(vm, input, converted);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_codecs_transform_registered(tinypy_vm_t *vm, tinypy_value_t *input, tinypy_value_t *encoding, tinypy_value_t *errors, tinypy_bool_t decode, tinypy_error_t **out_error) {
    tinypy_value_t *module = vm->codec_module;
    tinypy_value_t *lookup_args;
    tinypy_value_t *codec;
    tinypy_value_t *transform;
    tinypy_value_t *call_items[2];
    tinypy_value_t *call_args;
    tinypy_value_t *result;

    if (module == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_LOOKUP, "codec registry is unavailable", out_error);
        return NULL;
    }
    lookup_args = tinypy_tuple_from_items(vm, &encoding, 1U);
    codec = __tinypy_codecs_lookup(module, lookup_args, NULL, module, out_error);
    TINYPY_DECREF(lookup_args);
    if (codec == NULL) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(codec) == TINYPY_VALUE_TUPLE && TINYPY_TUPLE_SIZE(codec) >= 2U) {
        transform = TINYPY_RET(TINYPY_TUPLE_GET(codec, decode != 0 ? 1U : 0U));
    }
    else if (TINYPY_VALUE_KIND(codec) == TINYPY_VALUE_LIST && TINYPY_LIST_SIZE(codec) >= 2U) {
        transform = TINYPY_RET(TINYPY_LIST_GET(codec, decode != 0 ? 1U : 0U));
    }
    else {
        transform = tinypy_object_get_attr_value(codec, decode != 0 ? vm->internal_decode_key : vm->internal_encode_key, out_error);
    }
    TINYPY_DECREF(codec);
    if (transform == NULL) {
        return NULL;
    }
    call_items[0] = input;
    call_items[1] = errors;
    call_args = tinypy_tuple_from_items(vm, call_items, errors != NULL ? 2U : 1U);
    result = tinypy_call(transform, call_args, NULL, out_error);
    TINYPY_DECREF(call_args);
    TINYPY_DECREF(transform);
    if (result == NULL) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(result) != 2U) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, decode != 0 ? "decoder must return a tuple (object,integer)" : "encoder must return a tuple (object,integer)", out_error);
        return NULL;
    }
    tinypy_value_t *converted = TINYPY_TUPLE_GET(result, 0U);

    TINYPY_INCREF(converted);
    TINYPY_DECREF(result);
    return converted;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_transform(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *function_name = tinypy_native_function_name(function);
    tinypy_bool_t decode = TINYPY_NAME_EQ(function_name, vm->internal_decode_key) != TINYPY_FALSE ? TINYPY_TRUE : TINYPY_FALSE;
    const char *name = decode != 0 ? "decode" : "encode";
    tinypy_value_t *names[3] = {vm->internal_object_key, vm->internal_encoding_key, vm->internal_errors_key};
    tinypy_value_t *values[3];
    tinypy_value_t *encoding;
    tinypy_value_t *errors = NULL;
    tinypy_value_t *result;

    (void)user_data;

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_message_part_t parts[] = {
            {name, 6U},
            TINYPY_MESSAGE_PART_LITERAL("() takes no keyword arguments")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
        return NULL;
    }
    size_t count = TINYPY_TUPLE_SIZE(args);
    if (count < 1U || count > 3U) {
        tinypy_internal_make_arity_error(vm, name, 6U, count, 1U, 3U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }
    if (tinypy_internal_constructor_optional_arguments(vm, name, 6U, args, NULL, names, 3U, UINT32_C(6), values, out_error) == 0) {
        return NULL;
    }
    encoding = values[1] != NULL ? tinypy_internal_string_from_bytes_checked(vm, TINYPY_TEXT_BYTES(values[1]), TINYPY_TEXT_BYTE_SIZE(values[1]), out_error) : TINYPY_RET(vm->internal_codec_ascii_name);
    if (encoding == NULL) {
        return NULL;
    }
    if (values[2] != NULL) {
        errors = tinypy_internal_string_from_bytes_checked(vm, TINYPY_TEXT_BYTES(values[2]), TINYPY_TEXT_BYTE_SIZE(values[2]), out_error);
        if (errors == NULL) {
            TINYPY_DECREF(encoding);
            return NULL;
        }
    }
    result = tinypy_internal_codecs_transform_registered(vm, values[0], encoding, errors, decode, out_error);
    if (errors != NULL) {
        TINYPY_DECREF(errors);
    }
    TINYPY_DECREF(encoding);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_register_error(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    (void)user_data;

    if (__tinypy_codecs_arguments(function, args, kwargs, 2U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *argument_names[] = {vm->internal_encoding_key, vm->internal_object_key};
    tinypy_value_t *values[2];
    if (tinypy_internal_constructor_optional_arguments(vm, "register_error", 14U, args, NULL, argument_names, 2U, UINT32_C(1), values, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *name = values[0];
    tinypy_value_t *handler = values[1];
    if (handler->type->call == NULL && tinypy_internal_object_has_special_key(handler, vm->internal_special_call_key) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "error handler must be callable", out_error);
        return NULL;
    }
    tinypy_value_t *codecs_module_value = vm->codec_errors;
    tinypy_value_t *key = name->type == &vm->types[TINYPY_VALUE_STRING]
        ? TINYPY_RET(name)
        : tinypy_internal_string_from_bytes_checked(vm, TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name), out_error);
    if (key == NULL) {
        return NULL;
    }
    tinypy_bool_t registered = tinypy_internal_dict_set_checked(vm, codecs_module_value, key, handler, out_error);

    TINYPY_DECREF(key);
    if (registered == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_lookup_error(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    (void)user_data;

    if (__tinypy_codecs_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *argument_names[] = {vm->internal_encoding_key};
    tinypy_value_t *values[1];
    if (tinypy_internal_constructor_optional_arguments(vm, "lookup_error", 12U, args, NULL, argument_names, 1U, UINT32_C(1), values, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *name = values[0];
    tinypy_value_t *result = tinypy_internal_codecs_lookup_error(vm, name, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_codecs_lookup_error(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_value_t *module = vm->codec_module;
    tinypy_value_t *errors;
    tinypy_value_t *handler;

    if (module == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_LOOKUP, "codec registry is unavailable", out_error);
        return NULL;
    }
    errors = vm->codec_errors;
    tinypy_value_t *key = name->type == &vm->types[TINYPY_VALUE_STRING]
        ? TINYPY_RET(name)
        : tinypy_internal_string_from_bytes_checked(vm, TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name), out_error);
    if (key == NULL) {
        return NULL;
    }
    handler = tinypy_dict_get_optional(errors, key);
    TINYPY_DECREF(key);
    if (handler == NULL) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("unknown error handler name '"),
            TINYPY_MESSAGE_PART_TEXT(name),
            TINYPY_MESSAGE_PART_LITERAL("'"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_LOOKUP, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    return TINYPY_RET(handler);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_codecs_wrong_exception_type(tinypy_vm_t *vm, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_value_t *reported_class = tinypy_object_get_attr_value(item, vm->internal_special_class_key, out_error);

    if (reported_class == NULL) {
        return;
    }
    tinypy_value_t *name = tinypy_object_get_attr_value(reported_class, vm->internal_special_name_key, out_error);

    TINYPY_DECREF(reported_class);
    if (name == NULL) {
        return;
    }
    tinypy_value_t *text = tinypy_object_str(name, out_error);

    TINYPY_DECREF(name);
    if (text == NULL) {
        return;
    }
    size_t size = TINYPY_TEXT_BYTE_SIZE(text);
    const uint8_t *bytes = TINYPY_TEXT_BYTES(text);

    if (size > 400U) {
        size = 400U;
    }
    const uint8_t *nul = (const uint8_t *)memchr(bytes, 0, size);

    if (nul != NULL) {
        size = (size_t)(nul - bytes);
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("don't know how to handle "),
        {(const char *)bytes, size},
        TINYPY_MESSAGE_PART_LITERAL(" in error callback"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    TINYPY_DECREF(text);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_error_handler(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t mode = (intptr_t)user_data;
    tinypy_value_t *object = NULL;
    tinypy_value_t *end = NULL;
    tinypy_value_t *replacement = NULL;
    tinypy_value_t *result = NULL;
    int64_t first;
    int64_t last;

    if (__tinypy_codecs_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    if (mode == 0) {
        if (tinypy_type_is_subtype(item->type, vm->exception_types[TINYPY_EXCEPTION_BASE]) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec must pass exception instance", out_error);
            return NULL;
        }
        (void)tinypy_exception_raise(item, NULL, out_error);
        return NULL;
    }
    tinypy_bool_t encode = tinypy_type_is_subtype(item->type, vm->exception_types[TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR]);
    tinypy_bool_t decode = tinypy_type_is_subtype(item->type, vm->exception_types[TINYPY_EXCEPTION_UNICODE_DECODE_ERROR]);
    tinypy_bool_t translate = tinypy_type_is_subtype(item->type, vm->exception_types[TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR]);
    if ((encode == 0 && decode == 0 && translate == 0) || ((mode == 3 || mode == 4) && encode == 0)) {
        __tinypy_codecs_wrong_exception_type(vm, item, out_error);
        return NULL;
    }
    tinypy_internal_unicode_error_payload_t *payload = (tinypy_internal_unicode_error_payload_t *)tinypy_native_instance_payload(item);
    if (payload->object != NULL) {
        object = TINYPY_RET(payload->object);
    }
    if (object == NULL || TINYPY_VALUE_KIND(object) != (decode != 0 ? TINYPY_VALUE_STRING : TINYPY_VALUE_UNICODE)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, decode != 0 ? "object attribute must be str" : "object attribute must be unicode", out_error);
        goto cleanup;
    }
    first = mode == 1 || (mode == 2 && decode != 0) ? INT64_C(0) : payload->start;
    last = payload->end;
    /* Match the clamped UnicodeError accessors, before sizing any output. */
    int64_t length = (int64_t)TINYPY_SIZED_SIZE(object);
    if (first < INT64_C(0)) {
        first = INT64_C(0);
    }
    if (first >= length) {
        first = length > INT64_C(0) ? length - INT64_C(1) : INT64_C(0);
    }
    if (last < INT64_C(1)) {
        last = INT64_C(1);
    }
    if (last > length) {
        last = length;
    }
    if (last < first) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid UnicodeError range", out_error);
        goto cleanup;
    }
    end = tinypy_integer_from_i64(vm, last);
    size_t count = (size_t)(last - first);
    if (mode == 1) {
        replacement = tinypy_unicode_from_utf8(vm, NULL, 0U);
    }
    else if (mode == 2) {
        size_t width = encode != 0 ? 1U : 3U;
        if (decode != 0) {
            count = 1U;
        }
        if (count > SIZE_MAX / width) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "codec replacement is too large", out_error);
            goto cleanup;
        }
        uint8_t *bytes;
        replacement = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_UNICODE, count * width, count, &bytes, out_error);
        if (replacement != NULL) {
            for (size_t index = 0U; index < count; ++index) {
                (void)memcpy(bytes + index * width, encode != 0 ? "?" : "\xef\xbf\xbd", width);
            }
        }
    }
    else {
        tinypy_value_t *first_value = tinypy_integer_from_i64(vm, first);
        tinypy_value_t *slice = tinypy_slice_new(vm, first_value, end, NULL);
        tinypy_value_t *span = tinypy_internal_get_item_builtin(object, slice, out_error);
        TINYPY_DECREF(slice);
        TINYPY_DECREF(first_value);
        if (span != NULL) {
            if (count > (SIZE_MAX - 1U) / 12U) {
                TINYPY_DECREF(span);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "codec replacement is too large", out_error);
                goto cleanup;
            }
            size_t capacity = count * 12U;
            uint8_t *bytes = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, capacity + 1U, out_error);
            if (bytes != NULL) {
                size_t input = 0U;
                size_t output = 0U;
                size_t byte_size = TINYPY_TEXT_BYTE_SIZE(span);
                while (input < byte_size) {
                    uint32_t scalar;
                    size_t consumed = tinypy_internal_utf8_decode(TINYPY_TEXT_BYTES(span) + input, byte_size - input, &scalar);
                    if (consumed == 0U) {
                        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid internal Unicode data", out_error);
                        break;
                    }
                    int written;
                    if (mode == 3) {
                        written = snprintf((char *)bytes + output, capacity + 1U - output, "&#%" PRIu32 ";", scalar);
                    }
                    else if (scalar <= UINT32_C(0xff)) {
                        written = snprintf((char *)bytes + output, capacity + 1U - output, "\\x%02" PRIx32, scalar);
                    }
                    else if (scalar <= UINT32_C(0xffff)) {
                        written = snprintf((char *)bytes + output, capacity + 1U - output, "\\u%04" PRIx32, scalar);
                    }
                    else {
                        written = snprintf((char *)bytes + output, capacity + 1U - output, "\\U%08" PRIx32, scalar);
                    }
                    output += (size_t)written;
                    input += consumed;
                }
                if (input == byte_size) {
                    replacement = tinypy_unicode_from_utf8(vm, (const char *)bytes, output);
                }
                tinypy_internal_vm_deallocate(vm, bytes, capacity + 1U);
            }
            TINYPY_DECREF(span);
        }
    }
    if (replacement != NULL) {
        tinypy_value_t *items[2] = {replacement, end};
        result = tinypy_tuple_from_items(vm, items, 2U);
    }
cleanup:
    if (replacement != NULL) {
        TINYPY_DECREF(replacement);
    }
    if (end != NULL) {
        TINYPY_DECREF(end);
    }
    if (object != NULL) {
        TINYPY_DECREF(object);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_add_function(tinypy_value_t *module, tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new_key(name, callback, user_data, NULL);

    tinypy_module_add_value_key(module, name, function);
    return function;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_codecs_cache_builtin(tinypy_vm_t *vm, tinypy_value_t *module, tinypy_value_t *encode, tinypy_value_t *decode, tinypy_value_t *const *aliases, size_t alias_count) {
    (void)module;
    tinypy_value_t *none = TINYPY_RET_NONE(vm);
    tinypy_value_t *items[4] = {encode, decode, none, none};
    tinypy_value_t *codec = tinypy_tuple_from_items(vm, items, 4U);
    tinypy_value_t *cache = vm->codec_cache;
    size_t index;

    for (index = 0U; index != alias_count; ++index) {
        tinypy_dict_set(cache, aliases[index], codec);
    }
    TINYPY_DECREF(codec);
    TINYPY_DECREF(none);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_codecs_module(tinypy_vm_t *vm) {
    tinypy_value_t *const error_names[5] = {vm->internal_codec_strict_name, vm->internal_ignore_key, vm->internal_replace_key, vm->internal_xmlcharrefreplace_key, vm->internal_backslashreplace_key};
    tinypy_value_t *const function_names[5] = {vm->internal_strict_errors_key, vm->internal_ignore_errors_key, vm->internal_replace_errors_key, vm->internal_xmlcharrefreplace_errors_key, vm->internal_backslashreplace_errors_key};
    tinypy_value_t *const utf8_aliases[] = {vm->internal_utf_hyphen_8_key, vm->internal_codec_utf8_name, vm->internal_utf_8_key, vm->internal_u8_key, vm->internal_utf_key};
    tinypy_value_t *const ascii_aliases[] = {vm->internal_codec_ascii_name, vm->internal_codec_646_key, vm->internal_us_hyphen_ascii_key, vm->internal_iso646_hyphen_us_key, vm->internal_ansi_x3_dot_4_1968_key, vm->internal_ansi_hyphen_x3_dot_4_hyphen_1968_key};
    tinypy_value_t *const latin1_aliases[] = {vm->internal_codec_latin1_name, vm->internal_latin1_key, vm->internal_latin_1_key, vm->internal_iso_hyphen_8859_hyphen_1_key, vm->internal_iso8859_hyphen_1_key, vm->internal_iso_8859_hyphen_1_key, vm->internal_cp819_key, vm->internal_l1_key};
    tinypy_value_t *const hex_aliases[] = {vm->internal_hex_key, vm->internal_hex_hyphen_codec_key, vm->internal_hex_codec_key};
    tinypy_value_t *module = tinypy_module_new_key(vm->internal_codec_module_name);
    tinypy_value_t *name = TINYPY_RET(vm->internal_codec_module_name);
    tinypy_value_t *search_path = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *cache = tinypy_dict_new(vm);
    tinypy_value_t *errors = tinypy_dict_new(vm);
    size_t index;
    tinypy_value_t *utf8_encode;
    tinypy_value_t *utf8_decode;
    tinypy_value_t *ascii_encode;
    tinypy_value_t *ascii_decode;
    tinypy_value_t *latin1_encode;
    tinypy_value_t *latin1_decode;
    tinypy_value_t *hex_encode;
    tinypy_value_t *hex_decode;

    vm->codec_module = module;
    vm->codec_search_path = search_path;
    vm->codec_cache = cache;
    vm->codec_errors = errors;
    TINYPY_INCREF(module);
    TINYPY_INCREF(search_path);
    TINYPY_INCREF(cache);
    TINYPY_INCREF(errors);
    tinypy_module_add_value_key(module, vm->internal_special_name_key, name);
    tinypy_module_add_value_key(module, vm->internal_codec_search_path_key, search_path);
    tinypy_module_add_value_key(module, vm->internal_codec_cache_key, cache);
    tinypy_module_add_value_key(module, vm->internal_codec_errors_key, errors);
    TINYPY_DECREF(errors);
    TINYPY_DECREF(cache);
    TINYPY_DECREF(search_path);
    TINYPY_DECREF(name);

    tinypy_value_t *function = __tinypy_codecs_add_function(module, vm->internal_register_key, __tinypy_codecs_register, module);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(module, vm->internal_lookup_key, __tinypy_codecs_lookup, module);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(module, vm->internal_encode_key, __tinypy_codecs_transform, module);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(module, vm->internal_decode_key, __tinypy_codecs_transform, module);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(module, vm->internal_register_error_key, __tinypy_codecs_register_error, module);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(module, vm->internal_codec_lookup_error_key, __tinypy_codecs_lookup_error, module);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(module, vm->internal_utf_8_encode_key, __tinypy_codecs_specific, (void *)(intptr_t)0);
    utf8_encode = function;
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(module, vm->internal_utf_8_decode_key, __tinypy_codecs_specific, (void *)(intptr_t)1);
    TINYPY_DECREF(function);
    utf8_decode = tinypy_native_function_new_key(vm->internal_utf_8_decode_key, __tinypy_codecs_specific, (void *)(intptr_t)7, NULL);
    function = __tinypy_codecs_add_function(module, vm->internal_ascii_encode_key, __tinypy_codecs_specific, (void *)(intptr_t)2);
    ascii_encode = function;
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(module, vm->internal_ascii_decode_key, __tinypy_codecs_specific, (void *)(intptr_t)3);
    ascii_decode = function;
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(module, vm->internal_latin_1_encode_key, __tinypy_codecs_specific, (void *)(intptr_t)4);
    latin1_encode = function;
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(module, vm->internal_latin_1_decode_key, __tinypy_codecs_specific, (void *)(intptr_t)5);
    latin1_decode = function;
    TINYPY_DECREF(function);
    hex_encode = tinypy_native_function_new_key(vm->internal_hex_encode_key, __tinypy_codecs_hex, (void *)(intptr_t)0, NULL);
    hex_decode = tinypy_native_function_new_key(vm->internal_hex_decode_key, __tinypy_codecs_hex, (void *)(intptr_t)1, NULL);

    __tinypy_codecs_cache_builtin(vm, module, utf8_encode, utf8_decode, utf8_aliases, sizeof(utf8_aliases) / sizeof(utf8_aliases[0]));
    __tinypy_codecs_cache_builtin(vm, module, ascii_encode, ascii_decode, ascii_aliases, sizeof(ascii_aliases) / sizeof(ascii_aliases[0]));
    __tinypy_codecs_cache_builtin(vm, module, latin1_encode, latin1_decode, latin1_aliases, sizeof(latin1_aliases) / sizeof(latin1_aliases[0]));
    __tinypy_codecs_cache_builtin(vm, module, hex_encode, hex_decode, hex_aliases, sizeof(hex_aliases) / sizeof(hex_aliases[0]));
    TINYPY_DECREF(utf8_decode);
    TINYPY_DECREF(hex_decode);
    TINYPY_DECREF(hex_encode);

    for (index = 0U; index < 5U; ++index) {
        tinypy_value_t *key;

        function = tinypy_native_function_new_key(function_names[index], __tinypy_codecs_error_handler, (void *)(intptr_t)index, NULL);
        key = TINYPY_RET(error_names[index]);
        tinypy_value_t *codecs_module_value = vm->codec_errors;
        tinypy_dict_set(codecs_module_value, key, function);
        TINYPY_DECREF(key);
        TINYPY_DECREF(function);
    }
    tinypy_internal_register_module(vm, vm->internal_codec_module_name, module);
    TINYPY_DECREF(module);
}
