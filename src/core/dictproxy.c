#include "internal.h"

#include <string.h>

typedef struct tinypy_internal_dictproxy_payload_t {
    tinypy_value_t *dict;
} tinypy_internal_dictproxy_payload_t;

typedef enum tinypy_internal_dictproxy_method_e {
    TINYPY_INTERNAL_DICTPROXY_GET = 0,
    TINYPY_INTERNAL_DICTPROXY_HAS_KEY = 1,
    TINYPY_INTERNAL_DICTPROXY_KEYS = 2,
    TINYPY_INTERNAL_DICTPROXY_VALUES = 3,
    TINYPY_INTERNAL_DICTPROXY_ITEMS = 4,
    TINYPY_INTERNAL_DICTPROXY_ITERKEYS = 5,
    TINYPY_INTERNAL_DICTPROXY_ITERVALUES = 6,
    TINYPY_INTERNAL_DICTPROXY_ITERITEMS = 7,
    TINYPY_INTERNAL_DICTPROXY_COPY = 8
} tinypy_internal_dictproxy_method_e;

//////////////////////////////////////////////////////////////////////////
static tinypy_internal_dictproxy_payload_t *__tinypy_dictproxy_payload(tinypy_value_t *value) {
    tinypy_internal_dictproxy_payload_t *return_value_1 = (tinypy_internal_dictproxy_payload_t *)tinypy_native_instance_payload(value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_dictproxy_check(const tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    return value->type == vm->dictproxy_type ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dictproxy_dict(const tinypy_value_t *value) {
    const tinypy_internal_dictproxy_payload_t *payload = (const tinypy_internal_dictproxy_payload_t *)tinypy_native_instance_const_payload(value);

    return payload->dict;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dictproxy_new(tinypy_vm_t *vm, tinypy_value_t *dict) {
    tinypy_value_t *result = tinypy_native_instance_new(vm->dictproxy_type);
    tinypy_internal_dictproxy_payload_t *payload = __tinypy_dictproxy_payload(result);

    payload->dict = dict;
    TINYPY_INCREF(dict);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_dictproxy_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_internal_dictproxy_payload_t *payload = __tinypy_dictproxy_payload(value);
    tinypy_value_t *dict = payload->dict;

    tinypy_internal_instance_release_references(value, visit, user_data);
    payload->dict = NULL;
    if (dict != NULL) {
        visit(dict, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_dictproxy_traverse_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_internal_dictproxy_payload_t *payload = __tinypy_dictproxy_payload(value);

    tinypy_internal_instance_release_references(value, visit, user_data);
    if (payload->dict != NULL) {
        visit(payload->dict, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dictproxy_get_item(tinypy_value_t *instance, void *payload_value, tinypy_value_t *key, void *user_data, tinypy_error_t **out_error) {
    tinypy_internal_dictproxy_payload_t *payload = (tinypy_internal_dictproxy_payload_t *)payload_value;

    (void)instance;
    (void)user_data;
    tinypy_value_t *return_value_1 = tinypy_get_item(payload->dict, key, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __tinypy_dictproxy_length(tinypy_value_t *instance, void *payload_value, void *user_data, tinypy_error_t **out_error) {
    tinypy_internal_dictproxy_payload_t *payload = (tinypy_internal_dictproxy_payload_t *)payload_value;
    size_t size = TINYPY_DICT_SIZE(payload->dict);

    (void)instance;
    (void)user_data;
    TINYPY_CLEAR_ERROR(out_error);
#if SIZE_MAX > PTRDIFF_MAX
    if (size > (size_t)PTRDIFF_MAX) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(payload->dict), TINYPY_ERROR_OVERFLOW, "dictproxy is too large", out_error);
        return -1;
    }
#endif
    return (ptrdiff_t)size;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_dictproxy_contains(tinypy_value_t *instance, void *payload_value, tinypy_value_t *item, void *user_data, tinypy_error_t **out_error) {
    tinypy_internal_dictproxy_payload_t *payload = (tinypy_internal_dictproxy_payload_t *)payload_value;
    tinypy_bool_t contains;

    (void)instance;
    (void)user_data;
    if (tinypy_internal_dict_contains_checked(TINYPY_VALUE_VM(payload->dict), payload->dict, item, &contains, out_error) == 0) {
        return -1;
    }
    return contains != 0 ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dictproxy_iter(tinypy_value_t *instance, void *payload_value, void *user_data, tinypy_error_t **out_error) {
    tinypy_internal_dictproxy_payload_t *payload = (tinypy_internal_dictproxy_payload_t *)payload_value;

    (void)instance;
    (void)user_data;
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *return_value_1 = tinypy_internal_dict_iterator_new(payload->dict, INT32_C(0));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dictproxy_repr(tinypy_value_t *instance, void *payload_value, void *user_data, tinypy_error_t **out_error) {
    static const char prefix[] = "dict_proxy(";
    tinypy_internal_dictproxy_payload_t *payload = (tinypy_internal_dictproxy_payload_t *)payload_value;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    tinypy_value_t *dict_repr;
    size_t dict_size;
    size_t total_size;
    uint8_t *bytes;

    (void)user_data;
    dict_repr = tinypy_object_repr(payload->dict, out_error);
    if (dict_repr == NULL) {
        return NULL;
    }
    dict_size = TINYPY_TEXT_BYTE_SIZE(dict_repr);
    if (dict_size > SIZE_MAX - (sizeof(prefix) - 1U) - 1U) {
        TINYPY_DECREF(dict_repr);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "dictproxy representation is too large", out_error);
        return NULL;
    }
    total_size = (sizeof(prefix) - 1U) + dict_size + 1U;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, total_size, total_size, &bytes, out_error);
    if (result == NULL) {
        TINYPY_DECREF(dict_repr);
        return NULL;
    }
    (void)memcpy(bytes, prefix, sizeof(prefix) - 1U);
    (void)memcpy(bytes + sizeof(prefix) - 1U, TINYPY_TEXT_BYTES(dict_repr), dict_size);
    bytes[total_size - 1U] = (uint8_t)')';
    TINYPY_DECREF(dict_repr);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_dictproxy_hash(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)payload;
    (void)user_data;
    tinypy_internal_make_vm_error(TINYPY_VALUE_VM(instance), TINYPY_ERROR_TYPE, "unhashable type: 'dictproxy'", out_error);
    return (tinypy_hash_t)0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dictproxy_compare(tinypy_value_t *instance, void *payload_value, tinypy_value_t *other, tinypy_compare_operation_e operation, void *user_data, tinypy_error_t **out_error) {
    tinypy_internal_dictproxy_payload_t *payload = (tinypy_internal_dictproxy_payload_t *)payload_value;
    tinypy_value_t *right;

    (void)user_data;
    if (tinypy_internal_dictproxy_check(other) != 0) {
        right = tinypy_internal_dictproxy_dict(other);
    }
    else if (TINYPY_VALUE_KIND(other) == TINYPY_VALUE_DICT) {
        right = other;
    }
    else {
        tinypy_value_t *return_value_1 = tinypy_not_implemented_get(TINYPY_VALUE_VM(instance));
        return return_value_1;
    }
    tinypy_value_t *return_value_2 = tinypy_compare_value(payload->dict, right, operation, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_dictproxy_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < minimum || count > maximum || tinypy_internal_dictproxy_check(TINYPY_TUPLE_GET(args, 0U)) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "dictproxy method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dictproxy_snapshot(tinypy_value_t *dict, int32_t mode, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_dict_entry_t *entry = TINYPY_DICT_ITERATOR_BEGIN(dict);
    tinypy_dict_entry_t *end = TINYPY_DICT_ITERATOR_END(dict);

    if (tinypy_internal_list_reserve_checked(vm, result, TINYPY_DICT_SIZE(dict), out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    for (; entry != end; ++entry) {
        tinypy_value_t *item;

        if (!TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
            continue;
        }
        if (mode == 0) {
            item = entry->key;
        }
        else if (mode == 1) {
            item = entry->value;
        }
        else {
            tinypy_value_t *items[2] = {entry->key, entry->value};

            item = tinypy_tuple_from_items(vm, items, 2U);
            if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
                TINYPY_DECREF(item);
                TINYPY_DECREF(result);
                return NULL;
            }
            TINYPY_DECREF(item);
            continue;
        }
        if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dictproxy_copy(tinypy_value_t *dict, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    tinypy_value_t *result = tinypy_dict_new(vm);
    tinypy_dict_entry_t *entry = TINYPY_DICT_ITERATOR_BEGIN(dict);
    tinypy_dict_entry_t *end = TINYPY_DICT_ITERATOR_END(dict);

    if (tinypy_internal_dict_reserve_checked(vm, result, TINYPY_DICT_SIZE(dict), out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    for (; entry != end; ++entry) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry) && tinypy_internal_dict_set_checked(vm, result, entry->key, entry->value, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dictproxy_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_internal_dictproxy_method_e method = (tinypy_internal_dictproxy_method_e)(intptr_t)user_data;
    size_t minimum = method == TINYPY_INTERNAL_DICTPROXY_GET ? 2U : (method == TINYPY_INTERNAL_DICTPROXY_HAS_KEY ? 2U : 1U);
    size_t maximum = method == TINYPY_INTERNAL_DICTPROXY_GET ? 3U : minimum;
    tinypy_value_t *self;
    tinypy_value_t *dict;

    if (__tinypy_dictproxy_arguments(vm, args, kwargs, minimum, maximum, out_error) == 0) {
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    dict = tinypy_internal_dictproxy_dict(self);
    if (method == TINYPY_INTERNAL_DICTPROXY_GET) {
        tinypy_value_t *value;

        if (tinypy_internal_dict_get_optional_checked(vm, dict, TINYPY_TUPLE_GET(args, 1U), &value, out_error) == 0) {
            return NULL;
        }
        if (value == NULL && TINYPY_TUPLE_SIZE(args) == 3U) {
            value = TINYPY_TUPLE_GET(args, 2U);
        }
        if (value == NULL) {
            tinypy_value_t *return_value_1 = tinypy_none_get(vm);
            return return_value_1;
        }
        TINYPY_INCREF(value);
        return value;
    }
    if (method == TINYPY_INTERNAL_DICTPROXY_HAS_KEY) {
        tinypy_bool_t contains;

        if (tinypy_internal_dict_contains_checked(vm, dict, TINYPY_TUPLE_GET(args, 1U), &contains, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *return_value_2 = tinypy_bool_from_i32(vm, contains);
        return return_value_2;
    }
    if (method >= TINYPY_INTERNAL_DICTPROXY_KEYS && method <= TINYPY_INTERNAL_DICTPROXY_ITEMS) {
        tinypy_value_t *return_value_3 = __tinypy_dictproxy_snapshot(dict, (int32_t)method - (int32_t)TINYPY_INTERNAL_DICTPROXY_KEYS, out_error);
        return return_value_3;
    }
    if (method >= TINYPY_INTERNAL_DICTPROXY_ITERKEYS && method <= TINYPY_INTERNAL_DICTPROXY_ITERITEMS) {
        tinypy_value_t *return_value_4 = tinypy_internal_dict_iterator_new(dict, (int32_t)method - (int32_t)TINYPY_INTERNAL_DICTPROXY_ITERKEYS);
        return return_value_4;
    }
    tinypy_value_t *return_value_5 = __tinypy_dictproxy_copy(dict, out_error);
    return return_value_5;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dictproxy_protocol_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t operation = (intptr_t)user_data;
    size_t count = operation == 1 || operation == 2 ? 2U : 1U;
    tinypy_value_t *self;

    if (__tinypy_dictproxy_arguments(vm, args, kwargs, count, count, out_error) == 0) {
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    if (operation == 0) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)TINYPY_DICT_SIZE(tinypy_internal_dictproxy_dict(self)));
        return return_value_1;
    }
    if (operation == 1) {
        tinypy_value_t *return_value_2 = tinypy_get_item(tinypy_internal_dictproxy_dict(self), TINYPY_TUPLE_GET(args, 1U), out_error);
        return return_value_2;
    }
    if (operation == 2) {
        tinypy_bool_t contains;

        if (tinypy_internal_dict_contains_checked(vm, tinypy_internal_dictproxy_dict(self), TINYPY_TUPLE_GET(args, 1U), &contains, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *return_value_3 = tinypy_bool_from_i32(vm, contains);
        return return_value_3;
    }
    if (operation == 3) {
        tinypy_value_t *return_value_4 = tinypy_internal_dict_iterator_new(tinypy_internal_dictproxy_dict(self), INT32_C(0));
        return return_value_4;
    }
    tinypy_value_t *return_value_5 = __tinypy_dictproxy_repr(self, __tinypy_dictproxy_payload(self), NULL, out_error);
    return return_value_5;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dictproxy_compare_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_compare_operation_e operation = (tinypy_compare_operation_e)(intptr_t)user_data;
    tinypy_value_t *self;
    tinypy_value_t *other;
    tinypy_value_t *right;

    if (__tinypy_dictproxy_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    other = TINYPY_TUPLE_GET(args, 1U);
    right = tinypy_internal_dictproxy_check(other) != 0 ? tinypy_internal_dictproxy_dict(other) : other;
    tinypy_value_t *return_value_1 = tinypy_compare_value(tinypy_internal_dictproxy_dict(self), right, operation, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dictproxy_cmp_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *self;
    tinypy_value_t *other;
    tinypy_value_t *left;
    tinypy_value_t *right;
    int32_t equal;
    int32_t less;

    (void)user_data;
    if (__tinypy_dictproxy_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    other = TINYPY_TUPLE_GET(args, 1U);
    left = tinypy_internal_dictproxy_dict(self);
    right = tinypy_internal_dictproxy_check(other) != 0 ? tinypy_internal_dictproxy_dict(other) : other;
    equal = tinypy_compare_bool(left, right, TINYPY_COMPARE_EQUAL, out_error);
    if (equal < 0) {
        return NULL;
    }
    if (equal != 0) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, INT64_C(0));
        return return_value_1;
    }
    less = tinypy_compare_bool(left, right, TINYPY_COMPARE_LESS, out_error);
    if (less < 0) {
        return NULL;
    }
    tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, less != 0 ? -INT64_C(1) : INT64_C(1));
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_dictproxy_register(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, user_data, NULL);

    tinypy_type_set_attr(vm->dictproxy_type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_dictproxy_type(tinypy_vm_t *vm) {
    static const struct {
        const char *name;
        size_t name_size;
        tinypy_internal_dictproxy_method_e method;
    } methods[] = {
        {"get", 3U, TINYPY_INTERNAL_DICTPROXY_GET},
        {"has_key", 7U, TINYPY_INTERNAL_DICTPROXY_HAS_KEY},
        {"keys", 4U, TINYPY_INTERNAL_DICTPROXY_KEYS},
        {"values", 6U, TINYPY_INTERNAL_DICTPROXY_VALUES},
        {"items", 5U, TINYPY_INTERNAL_DICTPROXY_ITEMS},
        {"iterkeys", 8U, TINYPY_INTERNAL_DICTPROXY_ITERKEYS},
        {"itervalues", 10U, TINYPY_INTERNAL_DICTPROXY_ITERVALUES},
        {"iteritems", 9U, TINYPY_INTERNAL_DICTPROXY_ITERITEMS},
        {"copy", 4U, TINYPY_INTERNAL_DICTPROXY_COPY}
    };
    static const char *const comparison_names[] = {"__lt__", "__le__", "__eq__", "__ne__", "__gt__", "__ge__"};
    tinypy_native_type_spec_t spec;
    size_t index;

    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(tinypy_internal_dictproxy_payload_t);
    spec.repr = __tinypy_dictproxy_repr;
    spec.hash = __tinypy_dictproxy_hash;
    spec.compare = __tinypy_dictproxy_compare;
    spec.mapping_get = __tinypy_dictproxy_get_item;
    spec.mapping_length = __tinypy_dictproxy_length;
    spec.contains = __tinypy_dictproxy_contains;
    spec.iter = __tinypy_dictproxy_iter;
    spec.has_instance_dict = TINYPY_FALSE;
    spec.has_weakrefs = TINYPY_FALSE;
    vm->dictproxy_type = tinypy_native_type_new(vm, "dictproxy", 9U, NULL, 0U, NULL, &spec, NULL);
    vm->dictproxy_type->release_references = __tinypy_dictproxy_release_references;
    vm->dictproxy_type->traverse_references = __tinypy_dictproxy_traverse_references;
    vm->dictproxy_type->flags = (vm->dictproxy_type->flags | TINYPY_TYPE_FLAG_IMMUTABLE) & ~TINYPY_TYPE_FLAG_BASE_TYPE;
    for (index = 0U; index < sizeof(methods) / sizeof(methods[0]); ++index) {
        __tinypy_dictproxy_register(vm, methods[index].name, methods[index].name_size, __tinypy_dictproxy_method, (void *)(intptr_t)methods[index].method);
    }
    __tinypy_dictproxy_register(vm, "__len__", 7U, __tinypy_dictproxy_protocol_method, (void *)(intptr_t)0);
    __tinypy_dictproxy_register(vm, "__getitem__", 11U, __tinypy_dictproxy_protocol_method, (void *)(intptr_t)1);
    __tinypy_dictproxy_register(vm, "__contains__", 12U, __tinypy_dictproxy_protocol_method, (void *)(intptr_t)2);
    __tinypy_dictproxy_register(vm, "__iter__", 8U, __tinypy_dictproxy_protocol_method, (void *)(intptr_t)3);
    __tinypy_dictproxy_register(vm, "__repr__", 8U, __tinypy_dictproxy_protocol_method, (void *)(intptr_t)4);
    __tinypy_dictproxy_register(vm, "__str__", 7U, __tinypy_dictproxy_protocol_method, (void *)(intptr_t)4);
    for (index = 0U; index < sizeof(comparison_names) / sizeof(comparison_names[0]); ++index) {
        __tinypy_dictproxy_register(vm, comparison_names[index], 6U, __tinypy_dictproxy_compare_method, (void *)(intptr_t)index);
    }
    __tinypy_dictproxy_register(vm, "__cmp__", 7U, __tinypy_dictproxy_cmp_method, NULL);
}
