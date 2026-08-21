#include "tinypy/exception.h"

#include "internal.h"

typedef struct tinypy_exception_definition_t {
    const char *name;
    size_t name_size;
    int32_t parent;
} tinypy_exception_definition_t;
static const tinypy_exception_definition_t __tinypy_exception_definitions[TINYPY_EXCEPTION_TYPE_COUNT] = { {"BaseException", 13U, -1},
    {"Exception", 9U, TINYPY_EXCEPTION_BASE}, {"StandardError", 13U, TINYPY_EXCEPTION_EXCEPTION},
    {"ArithmeticError", 15U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"FloatingPointError", 18U, TINYPY_EXCEPTION_ARITHMETIC_ERROR},
    {"OverflowError", 13U, TINYPY_EXCEPTION_ARITHMETIC_ERROR}, {"ZeroDivisionError", 17U, TINYPY_EXCEPTION_ARITHMETIC_ERROR},
    {"AssertionError", 14U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"AttributeError", 14U, TINYPY_EXCEPTION_STANDARD_ERROR},
    {"EnvironmentError", 16U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"IOError", 7U, TINYPY_EXCEPTION_ENVIRONMENT_ERROR},
    {"OSError", 7U, TINYPY_EXCEPTION_ENVIRONMENT_ERROR}, {"WindowsError", 12U, TINYPY_EXCEPTION_OS_ERROR},
    {"EOFError", 8U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"ImportError", 11U, TINYPY_EXCEPTION_STANDARD_ERROR},
    {"LookupError", 11U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"IndexError", 10U, TINYPY_EXCEPTION_LOOKUP_ERROR},
    {"KeyError", 8U, TINYPY_EXCEPTION_LOOKUP_ERROR}, {"MemoryError", 11U, TINYPY_EXCEPTION_STANDARD_ERROR},
    {"NameError", 9U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"UnboundLocalError", 17U, TINYPY_EXCEPTION_NAME_ERROR},
    {"ReferenceError", 14U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"RuntimeError", 12U, TINYPY_EXCEPTION_STANDARD_ERROR},
    {"NotImplementedError", 19U, TINYPY_EXCEPTION_RUNTIME_ERROR}, {"SyntaxError", 11U, TINYPY_EXCEPTION_STANDARD_ERROR},
    {"IndentationError", 16U, TINYPY_EXCEPTION_SYNTAX_ERROR}, {"TabError", 8U, TINYPY_EXCEPTION_INDENTATION_ERROR},
    {"SystemError", 11U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"TypeError", 9U, TINYPY_EXCEPTION_STANDARD_ERROR},
    {"ValueError", 10U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"UnicodeError", 12U, TINYPY_EXCEPTION_VALUE_ERROR},
    {"UnicodeDecodeError", 18U, TINYPY_EXCEPTION_UNICODE_ERROR}, {"UnicodeEncodeError", 18U, TINYPY_EXCEPTION_UNICODE_ERROR},
    {"UnicodeTranslateError", 21U, TINYPY_EXCEPTION_UNICODE_ERROR}, {"StopIteration", 13U, TINYPY_EXCEPTION_EXCEPTION},
    {"Warning", 7U, TINYPY_EXCEPTION_EXCEPTION}, {"UserWarning", 11U, TINYPY_EXCEPTION_WARNING},
    {"DeprecationWarning", 18U, TINYPY_EXCEPTION_WARNING}, {"PendingDeprecationWarning", 25U, TINYPY_EXCEPTION_WARNING},
    {"SyntaxWarning", 13U, TINYPY_EXCEPTION_WARNING}, {"RuntimeWarning", 14U, TINYPY_EXCEPTION_WARNING},
    {"FutureWarning", 13U, TINYPY_EXCEPTION_WARNING}, {"ImportWarning", 13U, TINYPY_EXCEPTION_WARNING},
    {"UnicodeWarning", 14U, TINYPY_EXCEPTION_WARNING}, {"BytesWarning", 12U, TINYPY_EXCEPTION_WARNING},
    {"SystemExit", 10U, TINYPY_EXCEPTION_BASE}, {"KeyboardInterrupt", 17U, TINYPY_EXCEPTION_BASE},
    {"GeneratorExit", 13U, TINYPY_EXCEPTION_BASE}, {"BufferError", 11U, TINYPY_EXCEPTION_STANDARD_ERROR}};
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_exception_string_length(const char *text) {
    size_t size = 0U;

    while (text[size] != '\0') {
        size += 1U;
    }
    return size;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_exception_builtin_set(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *value) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(vm->builtins, key, value);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_is_class(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_bool_t return_value_1 = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)value, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_is_instance(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_bool_t return_value_1 = tinypy_type_is_subtype(value->type, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_internal_exception_payload_t *__tinypy_exception_payload(tinypy_value_t *value) {
    tinypy_internal_exception_payload_t *payload = (tinypy_internal_exception_payload_t *)tinypy_native_instance_payload(value);

    return payload;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_exception_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_internal_exception_payload_t *payload = __tinypy_exception_payload(value);

    tinypy_internal_instance_release_references(value, visit, user_data);
    if (payload->args != NULL) {
        visit(payload->args, user_data);
    }
    if (payload->message != NULL) {
        visit(payload->message, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_allocate_blank(tinypy_type_t *type) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *instance = tinypy_internal_object_allocate(vm, type, type->basic_size);
    tinypy_internal_exception_payload_t *payload = __tinypy_exception_payload(instance);

    payload->args = tinypy_tuple_from_items(vm, NULL, 0U);
    payload->message = tinypy_string_from_bytes(vm, NULL, 0U);
    return instance;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *args = __tinypy_exception_payload(value)->args;

    if (args == NULL || TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(TINYPY_VALUE_VM(value), NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t *display = TINYPY_TUPLE_SIZE(args) == 1U ? TINYPY_TUPLE_GET(args, 0U) : args;
    tinypy_value_t *return_value_2 = tinypy_object_str(display, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_getitem_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *exception_args = __tinypy_exception_payload(TINYPY_TUPLE_GET(args, 0U))->args;
    tinypy_value_t *result = tinypy_internal_get_item_builtin(exception_args, TINYPY_TUPLE_GET(args, 1U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_getslice_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *exception_args = __tinypy_exception_payload(TINYPY_TUPLE_GET(args, 0U))->args;
    tinypy_value_t *slice = tinypy_slice_new(vm, TINYPY_TUPLE_GET(args, 1U), TINYPY_TUPLE_GET(args, 2U), NULL);
    tinypy_value_t *result = tinypy_internal_get_item_builtin(exception_args, slice, out_error);

    TINYPY_DECREF(slice);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_str_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_exception_string(TINYPY_TUPLE_GET(args, 0U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_unicode_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *self;

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *string_method = tinypy_type_get_attr(self->type, "__str__", 7U);
    tinypy_value_t *base_string_method = tinypy_type_get_attr(vm->exception_types[TINYPY_EXCEPTION_BASE], "__str__", 7U);

    if (string_method != base_string_method) {
        tinypy_value_t *text = tinypy_object_str(self, out_error);
        tinypy_value_t *result;

        if (text == NULL) {
            return NULL;
        }
        result = tinypy_internal_object_unicode(text, out_error);
        TINYPY_DECREF(text);
        return result;
    }
    tinypy_value_t *exception_args = __tinypy_exception_payload(self)->args;
    if (TINYPY_TUPLE_SIZE(exception_args) == 0U) {
        tinypy_value_t *return_value_1 = tinypy_unicode_from_utf8(vm, NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t *display = TINYPY_TUPLE_SIZE(exception_args) == 1U ? TINYPY_TUPLE_GET(exception_args, 0U) : exception_args;
    tinypy_value_t *return_value_2 = tinypy_internal_object_unicode(display, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *self;
    tinypy_value_t *exception_args;
    tinypy_value_t *args_repr;
    size_t args_size;
    const char *args_bytes;
    uint8_t *result_bytes;

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    exception_args = __tinypy_exception_payload(self)->args;
    args_repr = tinypy_object_repr(exception_args, out_error);
    if (args_repr == NULL) {
        return NULL;
    }
    args_bytes = tinypy_string_view(args_repr, &args_size);
    if (args_size > SIZE_MAX - self->type->name_size) {
        TINYPY_DECREF(args_repr);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "exception representation is too large", out_error);
        return NULL;
    }
    size_t result_size = self->type->name_size + args_size;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, result_size, result_size, &result_bytes, out_error);

    if (result == NULL) {
        TINYPY_DECREF(args_repr);
        return NULL;
    }

    (void)memcpy(result_bytes, self->type->name, self->type->name_size);
    (void)memcpy(result_bytes + self->type->name_size, args_bytes, args_size);
    TINYPY_DECREF(args_repr);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_reduce_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *self;
    tinypy_value_t *exception_args;
    const tinypy_value_t *instance_dict;
    tinypy_value_t *result;
    tinypy_value_t *items[3];
    size_t item_count = 2U;

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    exception_args = __tinypy_exception_payload(self)->args;
    items[0] = &self->type->base.base;
    items[1] = exception_args;
    instance_dict = tinypy_instance_dict(self);
    if (instance_dict != NULL) {
        items[2] = (tinypy_value_t *)instance_dict;
        item_count = 3U;
    }
    result = tinypy_tuple_from_items(vm, items, item_count);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_setstate_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *self;
    tinypy_value_t *state;
    tinypy_dict_entry_t *iterator;
    tinypy_dict_entry_t *iterator_end;

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    state = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(state) == TINYPY_VALUE_NONE) {
        tinypy_value_t *result = tinypy_none_get(vm);

        return result;
    }
    if (TINYPY_VALUE_KIND(state) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception state must be a dictionary", out_error);
        return NULL;
    }
    iterator = TINYPY_DICT_ITERATOR_BEGIN(state);
    iterator_end = TINYPY_DICT_ITERATOR_END(state);
    for (; iterator != iterator_end; ++iterator) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
            tinypy_value_type_e key_kind = TINYPY_VALUE_KIND(iterator->key);

            if (key_kind != TINYPY_VALUE_STRING && key_kind != TINYPY_VALUE_UNICODE) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "attribute name must be string", out_error);
                return NULL;
            }
            if (tinypy_internal_object_set_attr_protocol_key(self, iterator->key, iterator->value, out_error) == 0) {
                return NULL;
            }
        }
    }
    tinypy_value_t *result = tinypy_none_get(vm);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_exception_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, NULL, NULL);

    tinypy_type_set_attr(type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_init(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count = TINYPY_TUPLE_SIZE(args);

    (void)user_data;
    if (count == 0U || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception constructor does not accept keyword arguments", out_error);
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_internal_exception_payload_t *payload = __tinypy_exception_payload(self);
    tinypy_value_t *const *items = tinypy_internal_tuple_items(args);
    tinypy_value_t *exception_args = tinypy_tuple_from_items(vm, items + 1U, count - 1U);

    tinypy_value_t *previous_args = payload->args;
    payload->args = exception_args;
    if (previous_args != NULL) {
        TINYPY_DECREF(previous_args);
    }
    if (count == 2U) {
        tinypy_value_t *message = items[1];

        TINYPY_INCREF(message);
        if (payload->message != NULL) {
            TINYPY_DECREF(payload->message);
        }
        payload->message = message;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "BaseException.__new__ requires an exception type", out_error);
        return NULL;
    }
    tinypy_value_t *type_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(type_value) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype((tinypy_type_t *)type_value, vm->exception_types[TINYPY_EXCEPTION_BASE]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "BaseException.__new__ requires an exception subtype", out_error);
        return NULL;
    }
    tinypy_value_t *result = __tinypy_exception_allocate_blank((tinypy_type_t *)type_value);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_exception_type_index_e __tinypy_exception_index_from_error(tinypy_error_kind_e kind) {
    switch (kind) {
    case TINYPY_ERROR_TYPE:
        return TINYPY_EXCEPTION_TYPE_ERROR;
    case TINYPY_ERROR_NAME:
        return TINYPY_EXCEPTION_NAME_ERROR;
    case TINYPY_ERROR_UNBOUND_LOCAL:
        return TINYPY_EXCEPTION_UNBOUND_LOCAL_ERROR;
    case TINYPY_ERROR_INTERRUPT:
        return TINYPY_EXCEPTION_KEYBOARD_INTERRUPT;
    case TINYPY_ERROR_ZERO_DIVISION:
        return TINYPY_EXCEPTION_ZERO_DIVISION_ERROR;
    case TINYPY_ERROR_VALUE:
        return TINYPY_EXCEPTION_VALUE_ERROR;
    case TINYPY_ERROR_INDEX:
        return TINYPY_EXCEPTION_INDEX_ERROR;
    case TINYPY_ERROR_KEY:
        return TINYPY_EXCEPTION_KEY_ERROR;
    case TINYPY_ERROR_OVERFLOW:
        return TINYPY_EXCEPTION_OVERFLOW_ERROR;
    case TINYPY_ERROR_IMPORT:
        return TINYPY_EXCEPTION_IMPORT_ERROR;
    case TINYPY_ERROR_ATTRIBUTE:
        return TINYPY_EXCEPTION_ATTRIBUTE_ERROR;
    case TINYPY_ERROR_LOOKUP:
        return TINYPY_EXCEPTION_LOOKUP_ERROR;
    case TINYPY_ERROR_UNICODE_DECODE:
        return TINYPY_EXCEPTION_UNICODE_DECODE_ERROR;
    case TINYPY_ERROR_UNICODE_ENCODE:
        return TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR;
    case TINYPY_ERROR_BUFFER:
        return TINYPY_EXCEPTION_BUFFER_ERROR;
    case TINYPY_ERROR_MEMORY:
        return TINYPY_EXCEPTION_MEMORY_ERROR;
    case TINYPY_ERROR_SYNTAX:
    case TINYPY_ERROR_PREPROCESSOR:
    case TINYPY_ERROR_META:
        return TINYPY_EXCEPTION_SYNTAX_ERROR;
    case TINYPY_ERROR_INDENTATION:
        return TINYPY_EXCEPTION_INDENTATION_ERROR;
    case TINYPY_ERROR_TAB:
        return TINYPY_EXCEPTION_TAB_ERROR;
    case TINYPY_ERROR_SOURCE_DECODING:
        return TINYPY_EXCEPTION_SYNTAX_ERROR;
    case TINYPY_ERROR_COMPILER_LIMIT:
        return TINYPY_EXCEPTION_RUNTIME_ERROR;
    case TINYPY_ERROR_RUNTIME:
    default:
        return TINYPY_EXCEPTION_RUNTIME_ERROR;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_error_kind_e __tinypy_exception_error_from_type(tinypy_vm_t *vm, tinypy_type_t *type) {
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_TYPE_ERROR]) != 0) {
        return TINYPY_ERROR_TYPE;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_UNBOUND_LOCAL_ERROR]) != 0) {
        return TINYPY_ERROR_UNBOUND_LOCAL;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_NAME_ERROR]) != 0) {
        return TINYPY_ERROR_NAME;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_KEYBOARD_INTERRUPT]) != 0) {
        return TINYPY_ERROR_INTERRUPT;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_ZERO_DIVISION_ERROR]) != 0) {
        return TINYPY_ERROR_ZERO_DIVISION;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_INDEX_ERROR]) != 0) {
        return TINYPY_ERROR_INDEX;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_KEY_ERROR]) != 0) {
        return TINYPY_ERROR_KEY;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_OVERFLOW_ERROR]) != 0) {
        return TINYPY_ERROR_OVERFLOW;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_IMPORT_ERROR]) != 0) {
        return TINYPY_ERROR_IMPORT;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_LOOKUP_ERROR]) != 0) {
        return TINYPY_ERROR_LOOKUP;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_UNICODE_DECODE_ERROR]) != 0) {
        return TINYPY_ERROR_UNICODE_DECODE;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR]) != 0) {
        return TINYPY_ERROR_UNICODE_ENCODE;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_TAB_ERROR]) != 0) {
        return TINYPY_ERROR_TAB;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_INDENTATION_ERROR]) != 0) {
        return TINYPY_ERROR_INDENTATION;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR]) != 0) {
        return TINYPY_ERROR_SYNTAX;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_VALUE_ERROR]) != 0) {
        return TINYPY_ERROR_VALUE;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_BUFFER_ERROR]) != 0) {
        return TINYPY_ERROR_BUFFER;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_MEMORY_ERROR]) != 0) {
        return TINYPY_ERROR_MEMORY;
    }
    return TINYPY_ERROR_RUNTIME;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_exceptions(tinypy_vm_t *vm) {
    size_t index;

    for (index = 0U; index < (size_t)TINYPY_EXCEPTION_TYPE_COUNT; ++index) {
        const tinypy_exception_definition_t *definition = &__tinypy_exception_definitions[index];
        tinypy_type_t *type;

        if (index == (size_t)TINYPY_EXCEPTION_BASE) {
            tinypy_native_type_spec_t spec;

            tinypy_native_type_spec_init(&spec);
            spec.payload_size = sizeof(tinypy_internal_exception_payload_t);
            spec.has_instance_dict = TINYPY_TRUE;
            spec.has_weakrefs = TINYPY_FALSE;
            type = tinypy_native_type_new(vm, definition->name, definition->name_size, NULL, 0U, NULL, &spec, NULL);
            type->release_references = __tinypy_exception_release_references;
            type->traverse_references = __tinypy_exception_release_references;
        }
        else {
            const tinypy_type_t *base = vm->exception_types[(size_t)definition->parent];

            type = tinypy_internal_type_new_configured(vm, definition->name, definition->name_size, &base, 1U, NULL, NULL, TINYPY_TRUE, TINYPY_FALSE, NULL);
        }

        if (index == (size_t)TINYPY_EXCEPTION_BASE) {
            tinypy_value_t *initializer = tinypy_native_function_new(vm, "__init__", 8U, __tinypy_exception_init, NULL, NULL);
            tinypy_value_t *new_function = tinypy_native_function_new(vm, "__new__", 7U, __tinypy_exception_new_method, NULL, NULL);
            tinypy_value_t *new_descriptor = tinypy_static_method_new(new_function);

            tinypy_type_set_attr(type, "__init__", 8U, initializer);
            tinypy_type_set_attr(type, "__new__", 7U, new_descriptor);
            TINYPY_DECREF(new_descriptor);
            TINYPY_DECREF(new_function);
            TINYPY_DECREF(initializer);
            tinypy_internal_initialize_exception_descriptors(type);
            __tinypy_exception_add_method(type, "__getitem__", 11U, __tinypy_exception_getitem_method);
            __tinypy_exception_add_method(type, "__getslice__", 12U, __tinypy_exception_getslice_method);
            __tinypy_exception_add_method(type, "__str__", 7U, __tinypy_exception_str_method);
            __tinypy_exception_add_method(type, "__unicode__", 11U, __tinypy_exception_unicode_method);
            __tinypy_exception_add_method(type, "__repr__", 8U, __tinypy_exception_repr_method);
            __tinypy_exception_add_method(type, "__reduce__", 10U, __tinypy_exception_reduce_method);
            __tinypy_exception_add_method(type, "__setstate__", 12U, __tinypy_exception_setstate_method);
            type->string = &__tinypy_exception_string;
        }
        vm->exception_types[index] = type;
        __tinypy_exception_builtin_set(vm, definition->name, definition->name_size, &type->base.base);
        TINYPY_DECREF(&type->base.base);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_exceptions_module(tinypy_vm_t *vm) {
    tinypy_value_t *module = tinypy_module_new(vm, "exceptions", 10U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "exceptions", 10U);
    size_t index;

    tinypy_module_add_value(module, "__name__", 8U, name);
    TINYPY_DECREF(name);
    for (index = 0U; index < (size_t)TINYPY_EXCEPTION_TYPE_COUNT; ++index) {
        const tinypy_exception_definition_t *definition = &__tinypy_exception_definitions[index];

        tinypy_module_add_value(module, definition->name, definition->name_size, &vm->exception_types[index]->base.base);
    }
    tinypy_internal_register_module(vm, "exceptions", 10U, module);
    TINYPY_DECREF(module);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_exception_instantiate(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *initializer_attribute;

    tinypy_value_t *instance = __tinypy_exception_allocate_blank(type);

    initializer_attribute = tinypy_type_get_attr(type, "__init__", 8U);
    if (initializer_attribute != NULL) {
        tinypy_value_t *initializer = tinypy_internal_descriptor_get_value(vm, initializer_attribute, instance, type, out_error);
        tinypy_value_t *result;

        if (initializer == NULL) {
            TINYPY_DECREF(instance);
            return NULL;
        }
        result = tinypy_call(initializer, args, kwargs, out_error);
        TINYPY_DECREF(initializer);
        if (result == NULL) {
            TINYPY_DECREF(instance);
            return NULL;
        }
        if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_NONE) {
            TINYPY_DECREF(result);
            TINYPY_DECREF(instance);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception __init__ must return None", out_error);
            return NULL;
        }
        TINYPY_DECREF(result);
    }
    else if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        TINYPY_DECREF(instance);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception constructor does not accept keyword arguments", out_error);
        return NULL;
    }
    return instance;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_exception_new(tinypy_type_t *type, tinypy_value_t *args, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    if (tinypy_type_is_subtype(type, type->vm->exception_types[TINYPY_EXCEPTION_BASE]) == 0) {
        tinypy_internal_make_vm_error(type->vm, TINYPY_ERROR_TYPE, "exception type must derive from BaseException", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_exception_instantiate(type, args, NULL, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_clear_raised(tinypy_vm_t *vm) {
    if (vm->raised_type != NULL) {
        TINYPY_DECREF(vm->raised_type);
    }
    if (vm->raised_value != NULL) {
        TINYPY_DECREF(vm->raised_value);
    }
    if (vm->raised_traceback != NULL) {
        TINYPY_DECREF(vm->raised_traceback);
    }
    vm->raised_type = NULL;
    vm->raised_value = NULL;
    vm->raised_traceback = NULL;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_clear_handled(tinypy_vm_t *vm) {
    if (vm->handled_type != NULL) {
        TINYPY_DECREF(vm->handled_type);
    }
    if (vm->handled_value != NULL) {
        TINYPY_DECREF(vm->handled_value);
    }
    if (vm->handled_traceback != NULL) {
        TINYPY_DECREF(vm->handled_traceback);
    }
    vm->handled_type = NULL;
    vm->handled_value = NULL;
    vm->handled_traceback = NULL;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_preserve_begin(tinypy_vm_t *vm, tinypy_internal_exception_state_t *state) {
    state->type = vm->raised_type;
    state->value = vm->raised_value;
    state->traceback = vm->raised_traceback;
    vm->raised_type = NULL;
    vm->raised_value = NULL;
    vm->raised_traceback = NULL;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_preserve_end(tinypy_vm_t *vm, tinypy_internal_exception_state_t *state) {
    tinypy_internal_exception_clear_raised(vm);
    vm->raised_type = state->type;
    vm->raised_value = state->value;
    vm->raised_traceback = state->traceback;
    state->type = NULL;
    state->value = NULL;
    state->traceback = NULL;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_set_raised(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *traceback) {
    TINYPY_INCREF(&value->type->base.base);
    TINYPY_INCREF(value);
    if (traceback != NULL) {
        TINYPY_INCREF(traceback);
    }
    tinypy_internal_exception_clear_raised(vm);
    vm->raised_type = &value->type->base.base;
    vm->raised_value = value;
    vm->raised_traceback = traceback;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_set_handled_from_raised(tinypy_vm_t *vm) {
    tinypy_internal_exception_clear_handled(vm);
    vm->handled_type = vm->raised_type;
    vm->handled_value = vm->raised_value;
    vm->handled_traceback = vm->raised_traceback;
    vm->raised_type = NULL;
    vm->raised_value = NULL;
    vm->raised_traceback = NULL;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_restore_raised_from_handled(tinypy_vm_t *vm) {
    tinypy_internal_exception_set_raised(vm, vm->handled_value, vm->handled_traceback);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_raise_kind(tinypy_vm_t *vm, tinypy_error_kind_e kind, const char *message) {
    tinypy_value_t *value;

    if (vm->exception_types[TINYPY_EXCEPTION_BASE] == NULL) {
        return;
    }
    size_t exception_string_length = __tinypy_exception_string_length(message);
    tinypy_value_t *text = tinypy_string_from_bytes(vm, message, exception_string_length);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &text, 1U);
    tinypy_exception_type_index_e exception_index_from_error = __tinypy_exception_index_from_error(kind);
    value = tinypy_internal_exception_instantiate(vm->exception_types[exception_index_from_error], args, NULL, NULL);
    TINYPY_DECREF(args);
    TINYPY_DECREF(text);
    tinypy_internal_exception_set_raised(vm, value, NULL);
    TINYPY_DECREF(value);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_make_diagnostic(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    const char *message = "Python exception";
    tinypy_error_kind_e kind = TINYPY_ERROR_RUNTIME;

    if (out_error == NULL || vm->raised_value == NULL) {
        return;
    }
    kind = __tinypy_exception_error_from_type(vm, vm->raised_value->type); {
        tinypy_value_t *message_value = __tinypy_exception_payload(vm->raised_value)->message;

        if (message_value != NULL && TINYPY_VALUE_KIND(message_value) == TINYPY_VALUE_STRING) {
            message = (const char *)TINYPY_STRING_OBJECT(message_value)->bytes;
        }
    }
    tinypy_internal_make_error(&vm->allocator, kind, message, out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_raise_stop_iteration(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_value_t *empty = tinypy_tuple_from_items(vm, NULL, 0U);
    tinypy_value_t *exception = tinypy_exception_new(vm->exception_types[TINYPY_EXCEPTION_STOP_ITERATION], empty, out_error);

    TINYPY_DECREF(empty);
    if (exception != NULL) {
        (void)tinypy_exception_raise(exception, NULL, out_error);
        TINYPY_DECREF(exception);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_exception_consume_stop_iteration(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_value_t *raised_type = vm->raised_type;

    if (raised_type == NULL || tinypy_type_is_subtype((tinypy_type_t *)raised_type, vm->exception_types[TINYPY_EXCEPTION_STOP_ITERATION]) == 0) {
        return TINYPY_FALSE;
    }
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
    }
    tinypy_internal_exception_clear_raised(vm);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_exception_matches(tinypy_value_t *exception, tinypy_value_t *candidate, tinypy_error_t **out_error) {
    tinypy_type_t *exception_type;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(exception);
    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(candidate) == TINYPY_VALUE_TUPLE) {
        tinypy_value_t *const *iterator = TINYPY_TUPLE_ITERATOR_BEGIN(candidate);
        tinypy_value_t *const *iterator_end = TINYPY_TUPLE_ITERATOR_END(candidate);

        for (; iterator != iterator_end; ++iterator) {
            tinypy_value_t *item = *iterator;
            int32_t matched = tinypy_exception_matches(exception, item, out_error);

            if (matched != 0) {
                return matched;
            }
        }
        return 0;
    }
    if (__tinypy_exception_is_class(vm, candidate) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "catching classes that do not inherit from BaseException is not allowed", out_error);
        return -1;
    }
    if (__tinypy_exception_is_class(vm, exception) != 0) {
        exception_type = (tinypy_type_t *)exception;
    }
    else if (__tinypy_exception_is_instance(vm, exception) != 0) {
        exception_type = exception->type;
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception match operand is not an exception", out_error);
        return -1;
    }
    int32_t return_value_1 = tinypy_type_is_subtype(exception_type, (tinypy_type_t *)candidate) != 0 ? 1 : 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_exception_raise(tinypy_value_t *exception, tinypy_value_t *traceback, tinypy_error_t **out_error) {
    tinypy_value_t *value = exception;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(exception);
    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_exception_is_class(vm, exception) != 0) {
        tinypy_value_t *args = tinypy_tuple_from_items(vm, NULL, 0U);

        value = tinypy_internal_exception_instantiate((tinypy_type_t *)exception, args, NULL, out_error);
        TINYPY_DECREF(args);
        if (value == NULL) {
            return TINYPY_FALSE;
        }
    }
    else if (__tinypy_exception_is_instance(vm, exception) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exceptions must derive from BaseException", out_error);
        return TINYPY_FALSE;
    }
    if (traceback != NULL && TINYPY_VALUE_KIND(traceback) != TINYPY_VALUE_NONE && TINYPY_VALUE_KIND(traceback) != TINYPY_VALUE_TRACEBACK) {
        if (value != exception) {
            TINYPY_DECREF(value);
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "raise traceback must be a traceback or None", out_error);
        return TINYPY_FALSE;
    }
    tinypy_value_t *raised_traceback = NULL;
    if (traceback != NULL && TINYPY_VALUE_KIND(traceback) == TINYPY_VALUE_TRACEBACK) {
        raised_traceback = traceback;
    }
    tinypy_internal_exception_set_raised(vm, value, raised_traceback);
    if (value != exception) {
        TINYPY_DECREF(value);
    }
    tinypy_internal_exception_make_diagnostic(vm, out_error);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_raised_exception(const tinypy_vm_t *vm) {
    return vm->raised_value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_raised_exception_type(const tinypy_vm_t *vm) {
    return vm->raised_type;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_raised_traceback(const tinypy_vm_t *vm) {
    return vm->raised_traceback;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_handled_exception(const tinypy_vm_t *vm) {
    return vm->handled_value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_vm_has_error(const tinypy_vm_t *vm) {
    return vm->raised_value != NULL ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_vm_clear_error(tinypy_vm_t *vm) {
    tinypy_internal_exception_clear_raised(vm);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_vm_raise_error(tinypy_vm_t *vm, tinypy_error_kind_e kind, const char *message) {
    tinypy_internal_exception_raise_kind(vm, kind, message);
}
