#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codecs_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec function received the wrong number of arguments", out_error);
        return TINYPY_FALSE;
    }
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec function does not accept keyword arguments", out_error);
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
static tinypy_value_t *__tinypy_codecs_module_value(tinypy_value_t *module, const char *name, size_t name_size) {
    tinypy_value_t *value = tinypy_module_get_value(module, name, name_size);

    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_normalize(tinypy_vm_t *vm, tinypy_value_t *name) {
    const uint8_t *source = TINYPY_TEXT_BYTES(name);
    size_t source_size = TINYPY_TEXT_BYTE_SIZE(name);
    uint8_t *normalized;
    size_t source_index;

    if (source_size == 0U) {
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, NULL, 0U);
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
    tinypy_value_t *module = (tinypy_value_t *)user_data;

    if (__tinypy_codecs_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *search = TINYPY_TUPLE_GET(args, 0U);
    if (search->type->call == NULL && tinypy_internal_object_has_special(search, "__call__", 8U) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "argument must be callable", out_error);
        return NULL;
    }
    tinypy_value_t *codecs_module_value = __tinypy_codecs_module_value(module, "_search_path", 12U);
    if (tinypy_internal_list_append_checked(codecs_module_value, search, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_lookup(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *module = (tinypy_value_t *)user_data;
    tinypy_value_t *cache;
    tinypy_value_t *search_path;
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    if (__tinypy_codecs_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *name = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_codecs_require_text(vm, name, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *normalized = __tinypy_codecs_normalize(vm, name);
    cache = __tinypy_codecs_module_value(module, "_cache", 6U);
    tinypy_value_t *cached = tinypy_dict_get_optional(cache, normalized);

    if (cached != NULL) {
        TINYPY_INCREF(cached);
        TINYPY_DECREF(normalized);
        return cached;
    }
    search_path = __tinypy_codecs_module_value(module, "_search_path", 12U);
    iterator = TINYPY_LIST_ITERATOR_BEGIN(search_path);
    iterator_end = TINYPY_LIST_ITERATOR_END(search_path);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *search_args = tinypy_tuple_from_items(vm, &normalized, 1U);
        tinypy_value_t *result = tinypy_call(*iterator, search_args, NULL, out_error);

        TINYPY_DECREF(search_args);
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
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_LOOKUP, "unknown encoding", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_call_text(tinypy_vm_t *vm, tinypy_value_t *input, const char *method_name, tinypy_value_t *encoding, tinypy_value_t *errors, tinypy_error_t **out_error) {
    tinypy_value_t *items[2];
    tinypy_value_t *result;
    size_t argument_count = 1U;

    if (__tinypy_codecs_require_text(vm, input, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *method = tinypy_object_get_attr(input, method_name, 6U, out_error);
    if (method == NULL) {
        return NULL;
    }
    items[0] = encoding;
    if (errors != NULL) {
        items[1] = errors;
        argument_count = 2U;
    }
    tinypy_value_t *method_args = tinypy_tuple_from_items(vm, items, argument_count);
    result = tinypy_call(method, method_args, NULL, out_error);
    TINYPY_DECREF(method_args);
    TINYPY_DECREF(method);
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
static tinypy_value_t *__tinypy_codecs_specific(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t operation = (intptr_t)user_data;
    tinypy_bool_t decode = (int32_t)(operation & (intptr_t)1);
    const char *encoding_name = operation < (intptr_t)2 ? "utf-8" : (operation < (intptr_t)4 ? "ascii" : "latin-1");
    size_t encoding_size = operation < (intptr_t)2 ? 5U : (operation < (intptr_t)4 ? 5U : 7U);
    size_t maximum = decode != 0 ? 3U : 2U;
    tinypy_value_t *errors = NULL;
    tinypy_value_t *converted;

    if (__tinypy_codecs_arguments(vm, args, kwargs, 1U, maximum, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *input = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_TUPLE_SIZE(args) >= 2U) {
        errors = TINYPY_TUPLE_GET(args, 1U);
    }
    tinypy_value_t *encoding = tinypy_string_from_bytes(vm, encoding_name, encoding_size);
    converted = __tinypy_codecs_call_text(vm, input, decode != 0 ? "decode" : "encode", encoding, errors, out_error);
    TINYPY_DECREF(encoding);
    if (converted == NULL) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_codecs_codec_result(vm, input, converted);
    return return_value_1;
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

    if (__tinypy_codecs_arguments(vm, args, kwargs, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    input = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_codecs_require_text(vm, input, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(input) == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *encoding = tinypy_string_from_bytes(vm, "ascii", 5U);

        bytes_value = __tinypy_codecs_call_text(vm, input, "encode", encoding, NULL, out_error);
        TINYPY_DECREF(encoding);
        if (bytes_value == NULL) {
            return NULL;
        }
    }
    else {
        bytes_value = input;
        TINYPY_INCREF(bytes_value);
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
    tinypy_value_t *module_key = tinypy_string_from_bytes(vm, "_codecs", 7U);
    tinypy_value_t *module = tinypy_dict_get_optional(vm->modules, module_key);
    tinypy_value_t *lookup_args;
    tinypy_value_t *codec;
    tinypy_value_t *transform;
    tinypy_value_t *call_items[2];
    tinypy_value_t *call_args;
    tinypy_value_t *result;

    TINYPY_DECREF(module_key);
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
        transform = TINYPY_TUPLE_GET(codec, decode != 0 ? 1U : 0U);
        TINYPY_INCREF(transform);
    }
    else if (TINYPY_VALUE_KIND(codec) == TINYPY_VALUE_LIST && TINYPY_LIST_SIZE(codec) >= 2U) {
        transform = TINYPY_LIST_GET(codec, decode != 0 ? 1U : 0U);
        TINYPY_INCREF(transform);
    }
    else {
        transform = tinypy_object_get_attr(codec, decode != 0 ? "decode" : "encode", 6U, out_error);
    }
    TINYPY_DECREF(codec);
    if (transform == NULL) {
        return NULL;
    }
    call_items[0] = input;
    call_items[1] = errors;
    if (call_items[1] == NULL) {
        call_items[1] = tinypy_string_from_bytes(vm, "strict", 6U);
    }
    call_args = tinypy_tuple_from_items(vm, call_items, 2U);
    result = tinypy_call(transform, call_args, NULL, out_error);
    TINYPY_DECREF(call_args);
    if (errors == NULL) {
        TINYPY_DECREF(call_items[1]);
    }
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
    tinypy_value_t *errors = NULL;
    tinypy_value_t *function_name = tinypy_native_function_name(function);
    tinypy_bool_t condition = TINYPY_TEXT_BYTE_SIZE(function_name) == 6U;
    if (condition != 0) {
        const uint8_t *bytes = TINYPY_TEXT_BYTES(function_name);
        condition = memcmp(bytes, "decode", 6U) == 0;
    }
    tinypy_bool_t decode = condition;

    (void)user_data;

    if (__tinypy_codecs_arguments(vm, args, kwargs, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *input = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *encoding = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_codecs_require_text(vm, encoding, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U) {
        errors = TINYPY_TUPLE_GET(args, 2U);
    }
    tinypy_value_t *return_value_1 = tinypy_internal_codecs_transform_registered(vm, input, encoding, errors, decode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_register_error(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *module = (tinypy_value_t *)user_data;

    if (__tinypy_codecs_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *name = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *handler = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_codecs_require_text(vm, name, out_error) == 0) {
        return NULL;
    }
    if (handler->type->call == NULL && tinypy_internal_object_has_special(handler, "__call__", 8U) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "error handler must be callable", out_error);
        return NULL;
    }
    tinypy_value_t *codecs_module_value = __tinypy_codecs_module_value(module, "_errors", 7U);
    tinypy_dict_set(codecs_module_value, name, handler);
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_lookup_error(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *module = (tinypy_value_t *)user_data;
    tinypy_value_t *handler;

    if (__tinypy_codecs_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *name = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_codecs_require_text(vm, name, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *errors = __tinypy_codecs_module_value(module, "_errors", 7U);
    handler = tinypy_dict_get_optional(errors, name);
    if (handler == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_LOOKUP, "unknown error handler name", out_error);
        return NULL;
    }
    TINYPY_INCREF(handler);
    return handler;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_codecs_lookup_error(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_value_t *module_key = tinypy_string_from_bytes(vm, "_codecs", 7U);
    tinypy_value_t *module = tinypy_dict_get_optional(vm->modules, module_key);
    tinypy_value_t *errors;
    tinypy_value_t *handler;

    TINYPY_DECREF(module_key);
    if (module == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_LOOKUP, "codec registry is unavailable", out_error);
        return NULL;
    }
    errors = __tinypy_codecs_module_value(module, "_errors", 7U);
    handler = tinypy_dict_get_optional(errors, name);
    if (handler == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_LOOKUP, "unknown error handler name", out_error);
        return NULL;
    }
    TINYPY_INCREF(handler);
    return handler;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_error_handler(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t mode = (intptr_t)user_data;
    tinypy_value_t *item;
    tinypy_value_t *end;
    tinypy_value_t *replacement = NULL;
    tinypy_value_t *result;

    if (__tinypy_codecs_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    item = TINYPY_TUPLE_GET(args, 0U);
    if (mode == 0) {
        (void)tinypy_exception_raise(item, NULL, out_error);
        return NULL;
    }
    if (tinypy_type_is_subtype(item->type, vm->exception_types[TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR]) == 0 &&
        tinypy_type_is_subtype(item->type, vm->exception_types[TINYPY_EXCEPTION_UNICODE_DECODE_ERROR]) == 0 &&
        tinypy_type_is_subtype(item->type, vm->exception_types[TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec error handler requires a UnicodeError", out_error);
        return NULL;
    }
    end = tinypy_object_get_attr(item, "end", 3U, out_error);
    if (end == NULL) {
        return NULL;
    }
    if (mode == 1) {
        replacement = tinypy_unicode_from_utf8(vm, NULL, 0U);
    }
    else {
        tinypy_bool_t encode = tinypy_type_is_subtype(item->type, vm->exception_types[TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR]) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
        tinypy_bool_t translate = tinypy_type_is_subtype(item->type, vm->exception_types[TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR]) != 0 ? TINYPY_TRUE : TINYPY_FALSE;

        if ((mode == 3 || mode == 4) && encode == 0) {
            TINYPY_DECREF(end);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "error handler does not support this UnicodeError", out_error);
            return NULL;
        }
        if (mode == 2 && encode == 0 && translate == 0) {
            replacement = tinypy_unicode_from_utf8(vm, "\xef\xbf\xbd", 3U);
        }
        else {
            tinypy_value_t *object = tinypy_object_get_attr(item, "object", 6U, out_error);
            tinypy_value_t *start = tinypy_object_get_attr(item, "start", 5U, out_error);

            if (object == NULL || start == NULL) {
                if (start != NULL) {
                    TINYPY_DECREF(start);
                }
                if (object != NULL) {
                    TINYPY_DECREF(object);
                }
                TINYPY_DECREF(end);
                return NULL;
            }
            if (mode == 2 && translate != 0) {
                int64_t start_index;
                int64_t end_index;

                if (tinypy_internal_index_as_i64(start, &start_index, TINYPY_FALSE, out_error) != 0 && tinypy_internal_index_as_i64(end, &end_index, TINYPY_FALSE, out_error) != 0 && end_index >= start_index && (uint64_t)(end_index - start_index) <= SIZE_MAX / 3U) {
                    size_t count = (size_t)(end_index - start_index);
                    uint8_t *bytes;

                    replacement = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_UNICODE, count * 3U, count, &bytes, out_error);
                    if (replacement != NULL) {
                        size_t index;

                        for (index = 0U; index != count; ++index) {
                            (void)memcpy(bytes + index * 3U, "\xef\xbf\xbd", 3U);
                        }
                    }
                }
            }
            else {
                tinypy_value_t *slice = tinypy_slice_new(vm, start, end, NULL);
                tinypy_value_t *span = tinypy_internal_get_item_builtin(object, slice, out_error);

                TINYPY_DECREF(slice);
                if (span != NULL) {
                    tinypy_value_t *encoding = tinypy_string_from_bytes(vm, "ascii", 5U);
                    const char *error_name = mode == 2 ? "replace" : (mode == 3 ? "xmlcharrefreplace" : "backslashreplace");
                    size_t error_name_size = mode == 2 ? 7U : (mode == 3 ? 17U : 16U);
                    tinypy_value_t *errors = tinypy_string_from_bytes(vm, error_name, error_name_size);
                    tinypy_value_t *encoded = __tinypy_codecs_call_text(vm, span, "encode", encoding, errors, out_error);

                    TINYPY_DECREF(errors);
                    TINYPY_DECREF(encoding);
                    TINYPY_DECREF(span);
                    if (encoded != NULL) {
                        replacement = tinypy_unicode_from_utf8(vm, (const char *)TINYPY_TEXT_BYTES(encoded), TINYPY_TEXT_BYTE_SIZE(encoded));
                        TINYPY_DECREF(encoded);
                    }
                }
            }
            TINYPY_DECREF(start);
            TINYPY_DECREF(object);
        }
    }
    if (replacement == NULL) {
        TINYPY_DECREF(end);
        if (out_error == NULL || *out_error == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid UnicodeError range", out_error);
        }
        return NULL;
    }
    tinypy_value_t *items[2] = {replacement, end};

    result = tinypy_tuple_from_items(vm, items, 2U);
    TINYPY_DECREF(replacement);
    TINYPY_DECREF(end);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_add_function(tinypy_vm_t *vm, tinypy_value_t *module, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, user_data, NULL);

    tinypy_module_add_value(module, name, name_size, function);
    return function;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_codecs_cache_builtin(tinypy_vm_t *vm, tinypy_value_t *module, tinypy_value_t *encode, tinypy_value_t *decode, const char *const *aliases, const size_t *alias_sizes, size_t alias_count) {
    tinypy_value_t *none = tinypy_none_get(vm);
    tinypy_value_t *items[4] = {encode, decode, none, none};
    tinypy_value_t *codec = tinypy_tuple_from_items(vm, items, 4U);
    tinypy_value_t *cache = __tinypy_codecs_module_value(module, "_cache", 6U);
    size_t index;

    for (index = 0U; index != alias_count; ++index) {
        tinypy_value_t *key = tinypy_string_from_bytes(vm, aliases[index], alias_sizes[index]);

        tinypy_dict_set(cache, key, codec);
        TINYPY_DECREF(key);
    }
    TINYPY_DECREF(codec);
    TINYPY_DECREF(none);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_codecs_module(tinypy_vm_t *vm) {
    static const char *error_names[5] = {"strict", "ignore", "replace", "xmlcharrefreplace", "backslashreplace"};
    static const size_t error_name_sizes[5] = {6U, 6U, 7U, 17U, 16U};
    static const char *const utf8_aliases[] = {"utf-8", "utf8", "utf_8", "u8", "utf"};
    static const size_t utf8_alias_sizes[] = {5U, 4U, 5U, 2U, 3U};
    static const char *const ascii_aliases[] = {"ascii", "646", "us-ascii", "iso646-us", "ansi_x3.4_1968", "ansi-x3.4-1968"};
    static const size_t ascii_alias_sizes[] = {5U, 3U, 8U, 9U, 14U, 14U};
    static const char *const latin1_aliases[] = {"latin-1", "latin1", "latin_1", "iso-8859-1", "iso8859-1", "iso_8859-1", "cp819", "l1"};
    static const size_t latin1_alias_sizes[] = {7U, 6U, 7U, 10U, 9U, 10U, 5U, 2U};
    static const char *const hex_aliases[] = {"hex", "hex-codec", "hex_codec"};
    static const size_t hex_alias_sizes[] = {3U, 9U, 9U};
    tinypy_value_t *module = tinypy_module_new(vm, "_codecs", 7U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "_codecs", 7U);
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

    tinypy_module_add_value(module, "__name__", 8U, name);
    tinypy_module_add_value(module, "_search_path", 12U, search_path);
    tinypy_module_add_value(module, "_cache", 6U, cache);
    tinypy_module_add_value(module, "_errors", 7U, errors);
    TINYPY_DECREF(errors);
    TINYPY_DECREF(cache);
    TINYPY_DECREF(search_path);
    TINYPY_DECREF(name);

    tinypy_value_t *function = __tinypy_codecs_add_function(vm, module, "register", 8U, __tinypy_codecs_register, module);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "lookup", 6U, __tinypy_codecs_lookup, module);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "encode", 6U, __tinypy_codecs_transform, module);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "decode", 6U, __tinypy_codecs_transform, module);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "register_error", 14U, __tinypy_codecs_register_error, module);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "lookup_error", 12U, __tinypy_codecs_lookup_error, module);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "utf_8_encode", 12U, __tinypy_codecs_specific, (void *)(intptr_t)0);
    utf8_encode = function;
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "utf_8_decode", 12U, __tinypy_codecs_specific, (void *)(intptr_t)1);
    utf8_decode = function;
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "ascii_encode", 12U, __tinypy_codecs_specific, (void *)(intptr_t)2);
    ascii_encode = function;
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "ascii_decode", 12U, __tinypy_codecs_specific, (void *)(intptr_t)3);
    ascii_decode = function;
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "latin_1_encode", 14U, __tinypy_codecs_specific, (void *)(intptr_t)4);
    latin1_encode = function;
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "latin_1_decode", 14U, __tinypy_codecs_specific, (void *)(intptr_t)5);
    latin1_decode = function;
    TINYPY_DECREF(function);
    hex_encode = tinypy_native_function_new(vm, "hex_encode", 10U, __tinypy_codecs_hex, (void *)(intptr_t)0, NULL);
    hex_decode = tinypy_native_function_new(vm, "hex_decode", 10U, __tinypy_codecs_hex, (void *)(intptr_t)1, NULL);

    __tinypy_codecs_cache_builtin(vm, module, utf8_encode, utf8_decode, utf8_aliases, utf8_alias_sizes, sizeof(utf8_aliases) / sizeof(utf8_aliases[0]));
    __tinypy_codecs_cache_builtin(vm, module, ascii_encode, ascii_decode, ascii_aliases, ascii_alias_sizes, sizeof(ascii_aliases) / sizeof(ascii_aliases[0]));
    __tinypy_codecs_cache_builtin(vm, module, latin1_encode, latin1_decode, latin1_aliases, latin1_alias_sizes, sizeof(latin1_aliases) / sizeof(latin1_aliases[0]));
    __tinypy_codecs_cache_builtin(vm, module, hex_encode, hex_decode, hex_aliases, hex_alias_sizes, sizeof(hex_aliases) / sizeof(hex_aliases[0]));
    TINYPY_DECREF(hex_decode);
    TINYPY_DECREF(hex_encode);

    for (index = 0U; index < 5U; ++index) {
        tinypy_value_t *key;

        function = tinypy_native_function_new(vm, error_names[index], error_name_sizes[index], __tinypy_codecs_error_handler, (void *)(intptr_t)index, NULL);
        key = tinypy_string_from_bytes(vm, error_names[index], error_name_sizes[index]);
        tinypy_value_t *codecs_module_value = __tinypy_codecs_module_value(module, "_errors", 7U);
        tinypy_dict_set(codecs_module_value, key, function);
        TINYPY_DECREF(key);
        TINYPY_DECREF(function);
    }
    tinypy_internal_register_module(vm, "_codecs", 7U, module);
    TINYPY_DECREF(module);
}
