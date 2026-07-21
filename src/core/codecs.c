#include "internal.h"

#include <assert.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codecs_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = tinypy_tuple_size(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec function received the wrong number of arguments", out_error);
        return INT32_C(0);
    }
    if (kwargs != NULL && tinypy_dict_size(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec function does not accept keyword arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codecs_require_text(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);

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
    const unsigned char *source = tinypy_internal_text_bytes(name);
    size_t source_size = tinypy_internal_text_byte_size(name);
    unsigned char *normalized;
    size_t source_index;
    size_t normalized_size = 0U;
    int32_t separator = INT32_C(0);
    tinypy_value_t *result;

    if (source_size == 0U) {
        return tinypy_string_from_bytes(vm, NULL, 0U);
    }
    normalized = (unsigned char *)tinypy_internal_vm_allocate(vm, source_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (source_index = 0U; source_index < source_size; source_index += 1U) {
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
    result = tinypy_string_from_bytes(vm, normalized, normalized_size);
    tinypy_internal_vm_deallocate(vm, normalized, source_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_register(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *module = (tinypy_value_t *)user_data;
    tinypy_value_t *search;

    if (__tinypy_codecs_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    search = tinypy_tuple_get(args, 0U);
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
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *module = (tinypy_value_t *)user_data;
    tinypy_value_t *name;
    tinypy_value_t *normalized;
    tinypy_value_t *cache;
    tinypy_value_t *search_path;
    size_t index;

    if (__tinypy_codecs_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    name = tinypy_tuple_get(args, 0U);
    if (__tinypy_codecs_require_text(vm, name, out_error) == 0) {
        return NULL;
    }
    normalized = __tinypy_codecs_normalize(vm, name);
    cache = __tinypy_codecs_module_value(module, "_cache", 6U);
    if (tinypy_dict_contains(cache, normalized) != 0) {
        tinypy_value_t *cached = tinypy_dict_get(cache, normalized);

        tinypy_retain(cached);
        tinypy_release(normalized);
        return cached;
    }
    search_path = __tinypy_codecs_module_value(module, "_search_path", 12U);
    for (index = 0U; index < tinypy_list_size(search_path); index += 1U) {
        tinypy_value_t *search_args = tinypy_tuple_from_items(vm, &normalized, 1U);
        tinypy_value_t *item = tinypy_list_get(search_path, index);
        tinypy_value_t *result = tinypy_call(item, search_args, NULL, out_error);

        tinypy_release(search_args);
        if (result == NULL) {
            tinypy_release(normalized);
            return NULL;
        }
        if (tinypy_internal_value_kind(result) != TINYPY_VALUE_NONE) {
            tinypy_dict_set(cache, normalized, result);
            tinypy_release(normalized);
            return result;
        }
        tinypy_release(result);
    }
    tinypy_release(normalized);
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_LOOKUP, "unknown encoding", out_error);
    return NULL;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_call_text(tinypy_vm_t *vm, tinypy_value_t *input, const char *method_name, tinypy_value_t *encoding, tinypy_value_t *errors, tinypy_error_t **out_error) {
    tinypy_value_t *method;
    tinypy_value_t *items[2];
    tinypy_value_t *method_args;
    tinypy_value_t *result;
    size_t argument_count = 1U;

    if (__tinypy_codecs_require_text(vm, input, out_error) == 0) {
        return NULL;
    }
    method = tinypy_object_get_attr(input, method_name, 6U, out_error);
    if (method == NULL) {
        return NULL;
    }
    items[0] = encoding;
    if (errors != NULL) {
        items[1] = errors;
        argument_count = 2U;
    }
    method_args = tinypy_tuple_from_items(vm, items, argument_count);
    result = tinypy_call(method, method_args, NULL, out_error);
    tinypy_release(method_args);
    tinypy_release(method);
    return result;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_codec_result(tinypy_vm_t *vm, tinypy_value_t *input, tinypy_value_t *converted) {
    size_t input_size = tinypy_internal_value_kind(input) == TINYPY_VALUE_UNICODE ? (size_t)TINYPY_SIZE(input) : tinypy_internal_text_byte_size(input);
    tinypy_value_t *consumed = tinypy_integer_from_i64(vm, (int64_t)input_size);
    tinypy_value_t *items[2] = {converted, consumed};
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);

    tinypy_release(consumed);
    tinypy_release(converted);
    return result;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_specific(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    intptr_t operation = (intptr_t)user_data;
    int32_t decode = (int32_t)(operation & (intptr_t)1);
    const char *encoding_name = operation < (intptr_t)2 ? "utf-8" : (operation < (intptr_t)4 ? "ascii" : "latin-1");
    size_t encoding_size = operation < (intptr_t)2 ? 5U : (operation < (intptr_t)4 ? 5U : 7U);
    size_t maximum = decode != 0 ? 3U : 2U;
    tinypy_value_t *input;
    tinypy_value_t *encoding;
    tinypy_value_t *errors = NULL;
    tinypy_value_t *converted;

    if (__tinypy_codecs_arguments(vm, args, kwargs, 1U, maximum, out_error) == 0) {
        return NULL;
    }
    input = tinypy_tuple_get(args, 0U);
    if (tinypy_tuple_size(args) >= 2U) {
        errors = tinypy_tuple_get(args, 1U);
    }
    encoding = tinypy_string_from_bytes(vm, encoding_name, encoding_size);
    converted = __tinypy_codecs_call_text(vm, input, decode != 0 ? "decode" : "encode", encoding, errors, out_error);
    tinypy_release(encoding);
    if (converted == NULL) {
        return NULL;
    }
    return __tinypy_codecs_codec_result(vm, input, converted);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_transform(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *module = (tinypy_value_t *)user_data;
    tinypy_value_t *input;
    tinypy_value_t *encoding;
    tinypy_value_t *errors = NULL;
    tinypy_value_t *lookup_args;
    tinypy_value_t *codec;
    tinypy_value_t *transform;
    tinypy_value_t *call_items[2];
    tinypy_value_t *call_args;
    tinypy_value_t *result;
    tinypy_value_t *function_name = tinypy_native_function_name(function);
    int condition = tinypy_internal_text_byte_size(function_name) == 6U;
    if (condition != 0) {
        const unsigned char *bytes = tinypy_internal_text_bytes(function_name);
        condition = memcmp(bytes, "decode", 6U) == 0;
    }
    int32_t decode = condition;

    if (__tinypy_codecs_arguments(vm, args, kwargs, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    input = tinypy_tuple_get(args, 0U);
    encoding = tinypy_tuple_get(args, 1U);
    if (__tinypy_codecs_require_text(vm, encoding, out_error) == 0) {
        return NULL;
    }
    if (tinypy_tuple_size(args) == 3U) {
        errors = tinypy_tuple_get(args, 2U);
    }
    lookup_args = tinypy_tuple_from_items(vm, &encoding, 1U);
    codec = __tinypy_codecs_lookup(function, lookup_args, NULL, module, out_error);
    tinypy_release(lookup_args);
    if (codec == NULL) {
        return NULL;
    }
    transform = tinypy_object_get_attr(codec, decode != 0 ? "decode" : "encode", 6U, out_error);
    tinypy_release(codec);
    if (transform == NULL) {
        return NULL;
    }
    call_items[0] = input;
    call_items[1] = errors != NULL ? errors : tinypy_string_from_bytes(vm, "strict", 6U);
    call_args = tinypy_tuple_from_items(vm, call_items, 2U);
    result = tinypy_call(transform, call_args, NULL, out_error);
    tinypy_release(call_args);
    if (errors == NULL) {
        tinypy_release(call_items[1]);
    }
    tinypy_release(transform);
    if (result == NULL) {
        return NULL;
    }
    if (tinypy_internal_value_kind(result) != TINYPY_VALUE_TUPLE || tinypy_tuple_size(result) < 1U) {
        tinypy_release(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "codec must return a tuple", out_error);
        return NULL;
    }
    input = tinypy_tuple_get(result, 0U);
    tinypy_retain(input);
    tinypy_release(result);
    return input;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_register_error(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *module = (tinypy_value_t *)user_data;
    tinypy_value_t *name;
    tinypy_value_t *handler;

    if (__tinypy_codecs_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    name = tinypy_tuple_get(args, 0U);
    handler = tinypy_tuple_get(args, 1U);
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
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *module = (tinypy_value_t *)user_data;
    tinypy_value_t *name;
    tinypy_value_t *errors;
    tinypy_value_t *handler;

    if (__tinypy_codecs_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    name = tinypy_tuple_get(args, 0U);
    if (__tinypy_codecs_require_text(vm, name, out_error) == 0) {
        return NULL;
    }
    errors = __tinypy_codecs_module_value(module, "_errors", 7U);
    if (tinypy_dict_contains(errors, name) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_LOOKUP, "unknown error handler name", out_error);
        return NULL;
    }
    handler = tinypy_dict_get(errors, name);
    tinypy_retain(handler);
    return handler;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codecs_error_handler(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_codecs_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = tinypy_tuple_get(args, 0U);
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
    tinypy_value_t *function;
    size_t index;

    tinypy_module_add_value(module, "__name__", 8U, name);
    tinypy_module_add_value(module, "_search_path", 12U, search_path);
    tinypy_module_add_value(module, "_cache", 6U, cache);
    tinypy_module_add_value(module, "_errors", 7U, errors);
    tinypy_release(errors);
    tinypy_release(cache);
    tinypy_release(search_path);
    tinypy_release(name);

    function = __tinypy_codecs_add_function(vm, module, "register", 8U, __tinypy_codecs_register, module);
    tinypy_release(function);
    function = __tinypy_codecs_add_function(vm, module, "lookup", 6U, __tinypy_codecs_lookup, module);
    tinypy_release(function);
    function = __tinypy_codecs_add_function(vm, module, "encode", 6U, __tinypy_codecs_transform, module);
    tinypy_release(function);
    function = __tinypy_codecs_add_function(vm, module, "decode", 6U, __tinypy_codecs_transform, module);
    tinypy_release(function);
    function = __tinypy_codecs_add_function(vm, module, "register_error", 14U, __tinypy_codecs_register_error, module);
    tinypy_release(function);
    function = __tinypy_codecs_add_function(vm, module, "lookup_error", 12U, __tinypy_codecs_lookup_error, module);
    tinypy_release(function);
    function = __tinypy_codecs_add_function(vm, module, "utf_8_encode", 12U, __tinypy_codecs_specific, (void *)(intptr_t)0);
    tinypy_release(function);
    function = __tinypy_codecs_add_function(vm, module, "utf_8_decode", 12U, __tinypy_codecs_specific, (void *)(intptr_t)1);
    tinypy_release(function);
    function = __tinypy_codecs_add_function(vm, module, "ascii_encode", 12U, __tinypy_codecs_specific, (void *)(intptr_t)2);
    tinypy_release(function);
    function = __tinypy_codecs_add_function(vm, module, "ascii_decode", 12U, __tinypy_codecs_specific, (void *)(intptr_t)3);
    tinypy_release(function);
    function = __tinypy_codecs_add_function(vm, module, "latin_1_encode", 14U, __tinypy_codecs_specific, (void *)(intptr_t)4);
    tinypy_release(function);
    function = __tinypy_codecs_add_function(vm, module, "latin_1_decode", 14U, __tinypy_codecs_specific, (void *)(intptr_t)5);
    tinypy_release(function);

    for (index = 0U; index < 5U; index += 1U) {
        tinypy_value_t *key;

        function = tinypy_native_function_new(vm, error_names[index], error_name_sizes[index], __tinypy_codecs_error_handler, (void *)(intptr_t)index, NULL);
        key = tinypy_string_from_bytes(vm, error_names[index], error_name_sizes[index]);
        tinypy_value_t *codecs_module_value = __tinypy_codecs_module_value(module, "_errors", 7U);
        tinypy_dict_set(codecs_module_value, key, function);
        tinypy_release(key);
        tinypy_release(function);
    }
    tinypy_internal_register_module(vm, "_codecs", 7U, module);
    tinypy_release(module);
}
