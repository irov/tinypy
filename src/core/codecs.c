#include "internal.h"

#include <assert.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codecs_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec function received the wrong number of arguments", out_error);
        return INT32_C(0);
    }
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec function does not accept keyword arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codecs_require_text(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        return INT32_C(1);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec name must be a string", out_error);
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_module_value(tinypy_value_t *module, const char *name, size_t name_size) {
    tinypy_value_t *value = tinypy_module_get_value(module, name, name_size);

    assert(value != NULL);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_normalize(tinypy_vm_t *vm, tinypy_value_t *name) {
    const unsigned char *source = TINYPY_TEXT_BYTES(name);
    size_t source_size = TINYPY_TEXT_BYTE_SIZE(name);
    unsigned char *normalized;
    size_t source_index;
    size_t normalized_size = 0U;
    int32_t separator = INT32_C(0);

    if (source_size == 0U) {
        return tinypy_string_from_bytes(vm, NULL, 0U);
    }
    normalized = (unsigned char *)tinypy_internal_vm_allocate(vm, source_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (source_index = 0U; source_index < source_size; ++source_index) {
        unsigned char character = source[source_index];

        if ((character >= (unsigned char)'a' && character <= (unsigned char)'z') || (character >= (unsigned char)'0' && character <= (unsigned char)'9') || character == (unsigned char)'.') {
            if (separator != 0 && normalized_size != 0U) {
                normalized[normalized_size++] = (unsigned char)'_';
            }
            normalized[normalized_size++] = character;
            separator = INT32_C(0);
        }
        else if (character >= (unsigned char)'A' && character <= (unsigned char)'Z') {
            if (separator != 0 && normalized_size != 0U) {
                normalized[normalized_size++] = (unsigned char)'_';
            }
            normalized[normalized_size++] = (unsigned char)(character + ('a' - 'A'));
            separator = INT32_C(0);
        }
        else {
            separator = INT32_C(1);
        }
    }
    tinypy_value_t *result = tinypy_string_from_bytes(vm, normalized, normalized_size);
    tinypy_internal_vm_deallocate(vm, normalized, source_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
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
    tinypy_list_append(codecs_module_value, search);
    return tinypy_none_get(vm);
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
    int32_t decode = (int32_t)(operation & (intptr_t)1);
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
    return __tinypy_codecs_codec_result(vm, input, converted);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_transform(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *module = (tinypy_value_t *)user_data;
    tinypy_value_t *errors = NULL;
    tinypy_value_t *lookup_args;
    tinypy_value_t *codec;
    tinypy_value_t *transform;
    tinypy_value_t *call_items[2];
    tinypy_value_t *call_args;
    tinypy_value_t *result;
    tinypy_value_t *function_name = tinypy_native_function_name(function);
    int condition = TINYPY_TEXT_BYTE_SIZE(function_name) == 6U;
    if (condition != 0) {
        const unsigned char *bytes = TINYPY_TEXT_BYTES(function_name);
        condition = memcmp(bytes, "decode", 6U) == 0;
    }
    int32_t decode = condition;

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
    lookup_args = tinypy_tuple_from_items(vm, &encoding, 1U);
    codec = __tinypy_codecs_lookup(function, lookup_args, NULL, module, out_error);
    TINYPY_DECREF(lookup_args);
    if (codec == NULL) {
        return NULL;
    }
    transform = tinypy_object_get_attr(codec, decode != 0 ? "decode" : "encode", 6U, out_error);
    TINYPY_DECREF(codec);
    if (transform == NULL) {
        return NULL;
    }
    call_items[0] = input;
    call_items[1] = errors != NULL ? errors : tinypy_string_from_bytes(vm, "strict", 6U);
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
    if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(result) < 1U) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec must return a tuple", out_error);
        return NULL;
    }
    input = TINYPY_TUPLE_GET(result, 0U);
    TINYPY_INCREF(input);
    TINYPY_DECREF(result);
    return input;
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
    return tinypy_none_get(vm);
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
static tinypy_value_t *__tinypy_codecs_error_handler(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_codecs_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    (void)tinypy_exception_raise(item, NULL, out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_add_function(tinypy_vm_t *vm, tinypy_value_t *module, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, user_data, NULL);

    tinypy_module_add_value(module, name, name_size, function);
    return function;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_codecs_module(tinypy_vm_t *vm) {
    static const char *error_names[5] = {"strict", "ignore", "replace", "xmlcharrefreplace", "backslashreplace"};
    static const size_t error_name_sizes[5] = {6U, 6U, 7U, 17U, 16U};
    tinypy_value_t *module = tinypy_module_new(vm, "_codecs", 7U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "_codecs", 7U);
    tinypy_value_t *search_path = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *cache = tinypy_dict_new(vm);
    tinypy_value_t *errors = tinypy_dict_new(vm);
    size_t index;

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
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "utf_8_decode", 12U, __tinypy_codecs_specific, (void *)(intptr_t)1);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "ascii_encode", 12U, __tinypy_codecs_specific, (void *)(intptr_t)2);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "ascii_decode", 12U, __tinypy_codecs_specific, (void *)(intptr_t)3);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "latin_1_encode", 14U, __tinypy_codecs_specific, (void *)(intptr_t)4);
    TINYPY_DECREF(function);
    function = __tinypy_codecs_add_function(vm, module, "latin_1_decode", 14U, __tinypy_codecs_specific, (void *)(intptr_t)5);
    TINYPY_DECREF(function);

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
