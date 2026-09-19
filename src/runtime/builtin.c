#include "tinypy/native.h"
#include "tinypy/compiler.h"
#include "tinypy/eval.h"

#include "internal.h"
#include "api_internal.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_no_keywords(tinypy_vm_t *vm, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin function does not accept keyword arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_argument_count(tinypy_vm_t *vm, tinypy_value_t *args, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin function received the wrong number of arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_named_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, const char *const *names, const size_t *name_sizes, size_t parameter_count, size_t required_count, tinypy_value_t **out_values, tinypy_error_t **out_error) {
    size_t positional_count = TINYPY_TUPLE_SIZE(args);
    size_t keyword_count = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    size_t recognized_keyword_count = 0U;
    size_t index;

    if (positional_count > parameter_count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin function received too many positional arguments", out_error);
        return TINYPY_FALSE;
    }
    for (index = 0U; index < parameter_count; ++index) {
        out_values[index] = index < positional_count ? TINYPY_TUPLE_GET(args, index) : NULL;
    }
    for (index = 0U; index < parameter_count; ++index) {
        tinypy_value_t *key;
        tinypy_value_t *keyword_value;

        if (keyword_count == 0U) {
            break;
        }
        key = tinypy_string_from_bytes(vm, names[index], name_sizes[index]);
        keyword_value = tinypy_dict_get_optional(kwargs, key);
        TINYPY_DECREF(key);
        if (keyword_value == NULL) {
            continue;
        }
        recognized_keyword_count += 1U;
        if (out_values[index] != NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin function received multiple values for one argument", out_error);
            return TINYPY_FALSE;
        }
        out_values[index] = keyword_value;
    }
    if (recognized_keyword_count != keyword_count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin function received an unexpected keyword argument", out_error);
        return TINYPY_FALSE;
    }
    for (index = 0U; index < required_count; ++index) {
        if (out_values[index] == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin function is missing a required argument", out_error);
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_text_view(tinypy_vm_t *vm, tinypy_value_t *value, const char **out_bytes, size_t *out_size, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        *out_bytes = (const char *)tinypy_string_view(value, out_size);
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        size_t code_points;

        *out_bytes = tinypy_unicode_utf8_view(value, out_size, &code_points);
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "attribute name must be a string", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_integer_as_i64(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    (void)vm;
    tinypy_bool_t return_value_1 = tinypy_internal_index_as_i64(value, out_value, TINYPY_FALSE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_call_items(tinypy_vm_t *vm, tinypy_value_t *callable, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error) {
    if (callable->type == &vm->types[TINYPY_VALUE_FUNCTION]) {
        tinypy_value_t *return_value_1 = tinypy_internal_eval_function_items(callable, items, item_count, NULL, out_error);
        return return_value_1;
    }
    if (callable->type == &vm->types[TINYPY_VALUE_METHOD]) {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(callable);

        if (method->self != NULL && method->function->type == &vm->types[TINYPY_VALUE_FUNCTION] && item_count < SIZE_MAX / sizeof(tinypy_value_t *)) {
            tinypy_value_t *local_items[4];
            tinypy_value_t **bound_items = local_items;
            size_t bound_count = item_count + 1U;
            size_t index;

            if (bound_count > sizeof(local_items) / sizeof(local_items[0])) {
                bound_items = (tinypy_value_t **)tinypy_internal_vm_allocate_checked(vm, bound_count * sizeof(*bound_items), out_error);
                if (bound_items == NULL) {
                    return NULL;
                }
            }
            bound_items[0] = method->self;
            for (index = 0U; index < item_count; ++index) {
                bound_items[index + 1U] = items[index];
            }
            tinypy_value_t *result = tinypy_internal_eval_function_items(method->function, bound_items, bound_count, NULL, out_error);

            if (bound_items != local_items) {
                tinypy_internal_vm_deallocate(vm, bound_items, bound_count * sizeof(*bound_items));
            }
            return result;
        }
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, items, item_count);
    tinypy_value_t *result = tinypy_call(callable, args, NULL, out_error);

    TINYPY_DECREF(args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_apply_sequence(tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE || kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_BUFFER || kind == TINYPY_VALUE_BYTEARRAY || kind == TINYPY_VALUE_XRANGE) {
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_DICT || kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t result = tinypy_internal_object_has_special(value, "__getitem__", 11U);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_apply(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *call_args = NULL;
    tinypy_value_t *call_kwargs = NULL;
    tinypy_value_t *owned_args = NULL;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 3U, out_error) == 0) {
        return NULL;
    }
    if (count >= 2U) {
        call_args = TINYPY_TUPLE_GET(args, 1U);
        if (TINYPY_VALUE_KIND(call_args) != TINYPY_VALUE_TUPLE) {
            tinypy_value_t *constructor_args;

            if (__tinypy_builtin_apply_sequence(call_args) == 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "apply() arg 2 must be a sequence", out_error);
                return NULL;
            }
            constructor_args = tinypy_tuple_from_items(vm, &call_args, 1U);
            owned_args = tinypy_internal_tuple_create(&vm->types[TINYPY_VALUE_TUPLE], constructor_args, NULL, out_error);
            TINYPY_DECREF(constructor_args);
            if (owned_args == NULL) {
                return NULL;
            }
            call_args = owned_args;
        }
    }
    else {
        owned_args = tinypy_tuple_from_items(vm, NULL, 0U);
        call_args = owned_args;
    }
    if (count == 3U) {
        call_kwargs = TINYPY_TUPLE_GET(args, 2U);
        if (TINYPY_VALUE_KIND(call_kwargs) != TINYPY_VALUE_DICT) {
            if (owned_args != NULL) {
                TINYPY_DECREF(owned_args);
            }
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "apply() arg 3 must be a dictionary", out_error);
            return NULL;
        }
    }
    tinypy_value_t *result = tinypy_call(TINYPY_TUPLE_GET(args, 0U), call_args, call_kwargs, out_error);

    if (owned_args != NULL) {
        TINYPY_DECREF(owned_args);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_len(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t size;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_internal_object_has_special_override(value, "__len__", 7U) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special(value, "__len__", 7U, out_error);
        tinypy_value_t *empty;
        tinypy_value_t *result;
        int64_t length;

        if (method == NULL) {
            return NULL;
        }
        empty = tinypy_tuple_from_items(vm, NULL, 0U);
        result = tinypy_call(method, empty, NULL, out_error);
        TINYPY_DECREF(empty);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return NULL;
        }
        if (__tinypy_builtin_integer_as_i64(vm, result, &length, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(result);
        if (length < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__ returned a negative value", out_error);
            return NULL;
        }
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, length);
        return return_value_1;
    }
    switch (TINYPY_VALUE_KIND(value)) {
    case TINYPY_VALUE_STRING:
        (void)tinypy_string_view(value, &size);
        break;
    case TINYPY_VALUE_UNICODE: {
        size_t byte_size;
        (void)tinypy_unicode_utf8_view(value, &byte_size, &size);
        break;
    }
    case TINYPY_VALUE_TUPLE:
        size = TINYPY_TUPLE_SIZE(value);
        break;
    case TINYPY_VALUE_LIST:
        size = TINYPY_LIST_SIZE(value);
        break;
    case TINYPY_VALUE_DICT:
        size = TINYPY_DICT_SIZE(value);
        break;
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
        size = tinypy_set_size(value);
        break;
    case TINYPY_VALUE_XRANGE:
        size = TINYPY_XRANGE_OBJECT(value)->length;
        break;
    default: {
        tinypy_length_slot_t length_slot = value->type->mapping_slots != NULL && value->type->mapping_slots->length != NULL
                                               ? value->type->mapping_slots->length
                                               : (value->type->sequence_slots != NULL ? value->type->sequence_slots->length : NULL);
        if (length_slot != NULL) {
            ptrdiff_t length = length_slot(value, out_error);

            if (length < 0) {
                if (out_error == NULL || *out_error == NULL) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "length slot returned a negative value", out_error);
                }
                return NULL;
            }
            tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)length);
            return return_value_1;
        }
        tinypy_error_t *attribute_error = NULL;
        tinypy_value_t *method = tinypy_internal_object_get_special(value, "__len__", 7U, &attribute_error);
        tinypy_value_t *empty;
        tinypy_value_t *result;

        if (method == NULL) {
            if (attribute_error != NULL) {
                tinypy_error_release(attribute_error);
            }
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object has no length", out_error);
            return NULL;
        }
        empty = tinypy_tuple_from_items(vm, NULL, 0U);
        result = tinypy_call(method, empty, NULL, out_error);
        TINYPY_DECREF(empty);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_INTEGER && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_LONG) {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__len__ returned a non-integer", out_error);
            return NULL;
        }
        int64_t length;
        if (__tinypy_builtin_integer_as_i64(vm, result, &length, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(result);
        if (length < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__ returned a negative value", out_error);
            return NULL;
        }
        tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, length);
        return return_value_2;
    }
    }
    tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, (int64_t)size);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_id(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    uintptr_t identity;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    identity = (uintptr_t)TINYPY_TUPLE_GET(args, 0U);
    if ((uint64_t)identity <= (uint64_t)INT64_MAX) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)identity);
        return return_value_1;
    }
    uint16_t digits[5];
    size_t count = 0U;
    uint64_t magnitude = (uint64_t)identity;

    while (magnitude != 0U) {
        digits[count] = (uint16_t)(magnitude & UINT64_C(0x7fff));
        count += 1U;
        magnitude >>= 15U;
    }
    tinypy_value_t *return_value_2 = tinypy_long_from_base15_digits(vm, 1, digits, count);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_builtin_metaclass_check(tinypy_value_t *object, tinypy_value_t *classinfo, tinypy_bool_t subclass, tinypy_bool_t *out_result, tinypy_error_t **out_error) {
    const char *name = subclass != 0 ? "__subclasscheck__" : "__instancecheck__";
    size_t name_size = subclass != 0 ? 17U : 17U;
    tinypy_value_t *value;
    int32_t truth;

    if (TINYPY_VALUE_KIND(classinfo) != TINYPY_VALUE_TYPE || tinypy_internal_object_has_special(classinfo, name, name_size) == 0) {
        return 0;
    }
    tinypy_value_t *method = tinypy_internal_object_get_special(classinfo, name, name_size, out_error);
    if (method == NULL) {
        return -1;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(object);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &object, 1U);
    value = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    if (value == NULL) {
        return -1;
    }
    truth = tinypy_truth(value, out_error);
    TINYPY_DECREF(value);
    if (truth < 0) {
        return -1;
    }
    *out_result = truth != 0 ? 1 : 0;
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_builtin_instance_check(tinypy_value_t *object, tinypy_value_t *classinfo, tinypy_bool_t subclass, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(object);
    tinypy_bool_t metaclass_result;
    int32_t metaclass_handled;

    if (TINYPY_VALUE_KIND(classinfo) == TINYPY_VALUE_TUPLE) {
        tinypy_value_t *const *iterator = TINYPY_TUPLE_ITERATOR_BEGIN(classinfo);
        tinypy_value_t *const *iterator_end = TINYPY_TUPLE_ITERATOR_END(classinfo);

        for (; iterator != iterator_end; ++iterator) {
            tinypy_value_t *item = *iterator;
            int32_t result = __tinypy_builtin_instance_check(object, item, subclass, out_error);

            if (result != 0) {
                return result;
            }
        }
        return 0;
    }
    metaclass_handled = __tinypy_builtin_metaclass_check(object, classinfo, subclass, &metaclass_result, out_error);
    if (metaclass_handled < 0) {
        return -1;
    }
    if (metaclass_handled != 0) {
        return metaclass_result;
    }
    if (TINYPY_VALUE_KIND(classinfo) == TINYPY_VALUE_CLASS) {
        if (subclass != 0) {
            if (TINYPY_VALUE_KIND(object) == TINYPY_VALUE_TYPE) {
                return 0;
            }
            if (TINYPY_VALUE_KIND(object) != TINYPY_VALUE_CLASS) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "issubclass first argument is not a class", out_error);
                return -1;
            }
            int32_t return_value_1 = tinypy_class_is_subclass(object, classinfo);
            return return_value_1;
        }
        if (TINYPY_VALUE_KIND(object) != TINYPY_VALUE_OLD_INSTANCE) {
            return 0;
        }
        tinypy_value_t *old_instance_class = tinypy_old_instance_class(object);
        int32_t return_value_2 = tinypy_class_is_subclass(old_instance_class, classinfo);
        return return_value_2;
    }
    if (TINYPY_VALUE_KIND(classinfo) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "classinfo is not a type or tuple of types", out_error);
        return -1;
    }
    if (subclass != 0) {
        if (TINYPY_VALUE_KIND(object) == TINYPY_VALUE_CLASS) {
            int32_t return_value_3 = tinypy_type_is_subtype(object->type, (tinypy_type_t *)classinfo);
            return return_value_3;
        }
        if (TINYPY_VALUE_KIND(object) != TINYPY_VALUE_TYPE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "issubclass first argument is not a type", out_error);
            return -1;
        }
        int32_t return_value_4 = tinypy_type_is_subtype((tinypy_type_t *)object, (tinypy_type_t *)classinfo);
        return return_value_4;
    }
    int32_t return_value_5 = tinypy_type_is_subtype(object->type, (tinypy_type_t *)classinfo);
    return return_value_5;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_isinstance(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    result = __tinypy_builtin_instance_check(item, item_2, 0, out_error);
    tinypy_value_t *return_value_1 = result < 0 ? NULL : tinypy_bool_from_i32(vm, result);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_issubclass(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    result = __tinypy_builtin_instance_check(item, item_2, 1, out_error);
    tinypy_value_t *return_value_1 = result < 0 ? NULL : tinypy_bool_from_i32(vm, result);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_callable(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_bool_t condition = TINYPY_TUPLE_GET(args, 0U)->type->call != NULL;
    if (condition == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        condition = tinypy_internal_object_has_special(item, "__call__", 8U) != 0;
    }
    tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, condition);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_getattr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const char *name;
    size_t name_size;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *object = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *name_value = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_builtin_text_view(vm, name_value, &name, &name_size, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U) {
        int32_t status = tinypy_internal_object_get_optional_attr_key(object, name_value, &result, out_error);

        if (status > 0) {
            return result;
        }
        if (status < 0) {
            return NULL;
        }
        result = TINYPY_TUPLE_GET(args, 2U);
        TINYPY_INCREF(result);
        return result;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_object_get_attr_key(object, name_value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_hasattr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const char *name;
    size_t name_size;
    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_builtin_text_view(vm, item_2, &name, &name_size, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, tinypy_object_has_attr_value(item, item_2));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_setattr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const char *name;
    size_t name_size;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 3U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_builtin_text_view(vm, item, &name, &name_size, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_3 = TINYPY_TUPLE_GET(args, 2U);
    if (tinypy_object_set_attr(item_2, name, name_size, item_3, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_delattr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const char *name;
    size_t name_size;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_builtin_text_view(vm, item, &name, &name_size, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_object_delete_attr(item_2, name, name_size, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_iter(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = TINYPY_TUPLE_SIZE(args) == 2U
                                        ? tinypy_internal_call_iterator_new(item, TINYPY_TUPLE_GET(args, 1U), out_error)
                                        : tinypy_iter(item, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_next(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_error_t *iteration_error = NULL;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *result = tinypy_next(item, &iteration_error);
    if (result != NULL) {
        return result;
    }
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        result = TINYPY_TUPLE_GET(args, 1U);
        TINYPY_INCREF(result);
        return result;
    }
    tinypy_internal_exception_raise_stop_iteration(vm, out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_range(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count;
    int64_t start;
    int64_t stop;
    int64_t step;
    int64_t current;
    uint64_t distance;
    uint64_t step_magnitude;
    uint64_t length;
    size_t index;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 3U, out_error) == 0) {
        return NULL;
    }
    count = TINYPY_TUPLE_SIZE(args);
    if (count == 1U) {
        start = 0;
        tinypy_value_t *item_4 = TINYPY_TUPLE_GET(args, 0U);
        if (__tinypy_builtin_integer_as_i64(vm, item_4, &stop, out_error) == 0) {
            return NULL;
        }
        step = 1;
    }
    else {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
        if (__tinypy_builtin_integer_as_i64(vm, item_2, &start, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *item_3 = TINYPY_TUPLE_GET(args, 1U);
        if (__tinypy_builtin_integer_as_i64(vm, item_3, &stop, out_error) == 0) {
            return NULL;
        }
        if (count == 3U) {
            tinypy_value_t *item_4 = TINYPY_TUPLE_GET(args, 2U);
            if (__tinypy_builtin_integer_as_i64(vm, item_4, &step, out_error) == 0) {
                return NULL;
            }
        }
        else {
            step = 1;
        }
    }
    if (step == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "range step cannot be zero", out_error);
        return NULL;
    }
    if ((step > 0 && start >= stop) || (step < 0 && start <= stop)) {
        length = UINT64_C(0);
    }
    else {
        if (step > 0) {
            distance = (uint64_t)stop - (uint64_t)start;
            step_magnitude = (uint64_t)step;
        }
        else {
            distance = (uint64_t)start - (uint64_t)stop;
            step_magnitude = (uint64_t)(-(step + INT64_C(1))) + UINT64_C(1);
        }
        length = (distance - UINT64_C(1)) / step_magnitude + UINT64_C(1);
    }
    if (length > (uint64_t)PTRDIFF_MAX || length > (uint64_t)SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "range has too many items", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);

    if (tinypy_internal_list_reserve_checked(vm, result, (size_t)length, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    current = start;
    for (index = 0U; index < (size_t)length; ++index) {
        tinypy_value_t *item = tinypy_integer_from_i64(vm, current);

        if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(item);
        if (index + 1U < (size_t)length) {
            current += step;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_sorted(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *sort_method;
    tinypy_value_t *sort_args;
    tinypy_value_t *sort_result;
    size_t argument_count;

    (void)user_data;
    if (__tinypy_builtin_argument_count(vm, args, 1U, 4U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *iterator = tinypy_iter(item_2, out_error);
    if (iterator == NULL) {
        return NULL;
    }
    tinypy_value_t *list = tinypy_list_from_items(vm, NULL, 0U);
    if (tinypy_internal_list_reserve_checked(vm, list, tinypy_internal_iterable_size_hint(item_2), out_error) == 0) {
        TINYPY_DECREF(list);
        TINYPY_DECREF(iterator);
        return NULL;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        if (tinypy_internal_list_append_checked(list, item, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(list);
            TINYPY_DECREF(iterator);
            return NULL;
        }
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        TINYPY_DECREF(list);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    sort_method = tinypy_object_get_attr(list, "sort", 4U, out_error);
    if (sort_method == NULL) {
        TINYPY_DECREF(list);
        return NULL;
    }
    argument_count = TINYPY_TUPLE_SIZE(args) - 1U;
    tinypy_value_t *const *items = argument_count != 0U ? &tinypy_internal_tuple_items(args)[1] : NULL;
    sort_args = tinypy_tuple_from_items(vm, items, argument_count);
    sort_result = tinypy_call(sort_method, sort_args, kwargs, out_error);
    TINYPY_DECREF(sort_args);
    TINYPY_DECREF(sort_method);
    if (sort_result == NULL) {
        TINYPY_DECREF(list);
        return NULL;
    }
    TINYPY_DECREF(sort_result);
    return list;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_all_any(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t want_all = user_data != NULL ? INT32_C(1) : INT32_C(0);
    tinypy_error_t *iteration_error = NULL;

    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *iterator = tinypy_iter(item_2, out_error);
    if (iterator == NULL) {
        return NULL;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        int32_t truth;

        if (item == NULL) {
            break;
        }
        truth = tinypy_truth(item, out_error);
        TINYPY_DECREF(item);
        if (truth < 0) {
            TINYPY_DECREF(iterator);
            return NULL;
        }
        if ((want_all != 0 && truth == 0) || (want_all == 0 && truth != 0)) {
            TINYPY_DECREF(iterator);
            tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, want_all == 0);
            return return_value_1;
        }
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    tinypy_value_t *return_value_2 = tinypy_bool_from_i32(vm, want_all != 0);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_divmod(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value = tinypy_divmod(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_filter_text(tinypy_vm_t *vm, tinypy_value_t *predicate, tinypy_value_t *input, tinypy_error_t **out_error) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(input);
    size_t byte_size = TINYPY_TEXT_BYTE_SIZE(input);
    tinypy_bool_t unicode = TINYPY_VALUE_KIND(input) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;

    if (TINYPY_VALUE_KIND(predicate) == TINYPY_VALUE_NONE) {
        TINYPY_INCREF(input);
        return input;
    }

    uint8_t *output = byte_size != 0U ? (uint8_t *)tinypy_internal_vm_allocate_checked(vm, byte_size, out_error) : NULL;
    size_t input_offset = 0U;
    size_t output_size = 0U;
    size_t output_code_points = 0U;

    if (byte_size != 0U && output == NULL) {
        return NULL;
    }

    while (input_offset < byte_size) {
        size_t character_size;
        tinypy_value_t *character;
        tinypy_value_t *call_result;
        int32_t truth;

        if (unicode != 0) {
            uint8_t lead = bytes[input_offset];

            character_size = lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
            character = tinypy_unicode_from_utf8(vm, (const char *)bytes + input_offset, character_size);
        }
        else {
            character_size = 1U;
            character = tinypy_string_from_bytes(vm, bytes + input_offset, 1U);
        }
        call_result = __tinypy_builtin_call_items(vm, predicate, &character, 1U, out_error);
        TINYPY_DECREF(character);
        if (call_result == NULL) {
            if (output != NULL) {
                tinypy_internal_vm_deallocate(vm, output, byte_size);
            }
            return NULL;
        }
        truth = tinypy_truth(call_result, out_error);
        TINYPY_DECREF(call_result);
        if (truth < 0) {
            if (output != NULL) {
                tinypy_internal_vm_deallocate(vm, output, byte_size);
            }
            return NULL;
        }
        if (truth != 0) {
            (void)memcpy(output + output_size, bytes + input_offset, character_size);
            output_size += character_size;
            output_code_points += 1U;
        }
        input_offset += character_size;
    }
    tinypy_value_t *result;

    if (unicode != 0) {
        uint8_t *result_bytes;

        result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_UNICODE, output_size, output_code_points, &result_bytes, out_error);
        if (result != NULL && output_size != 0U) {
            (void)memcpy(result_bytes, output, output_size);
        }
    }
    else {
        result = tinypy_internal_string_from_bytes_checked(vm, output, output_size, out_error);
    }
    if (output != NULL) {
        tinypy_internal_vm_deallocate(vm, output, byte_size);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_filter(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *input_iterator;
    tinypy_value_t *selected;
    tinypy_value_t *result;
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *predicate = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *input = TINYPY_TUPLE_GET(args, 1U);
    if ((input->type == &vm->types[TINYPY_VALUE_STRING] || input->type == &vm->types[TINYPY_VALUE_UNICODE])) {
        tinypy_value_t *return_value_1 = __tinypy_builtin_filter_text(vm, predicate, input, out_error);
        return return_value_1;
    }
    input_iterator = tinypy_iter(input, out_error);
    if (input_iterator == NULL) {
        return NULL;
    }
    selected = tinypy_list_from_items(vm, NULL, 0U);
    if (tinypy_internal_list_reserve_checked(vm, selected, tinypy_internal_iterable_size_hint(input), out_error) == 0) {
        TINYPY_DECREF(selected);
        TINYPY_DECREF(input_iterator);
        return NULL;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(input_iterator, &iteration_error);
        int32_t truth;

        if (item == NULL) {
            break;
        }
        if (TINYPY_VALUE_KIND(predicate) == TINYPY_VALUE_NONE) {
            truth = tinypy_truth(item, out_error);
        }
        else {
            tinypy_value_t *call_result = __tinypy_builtin_call_items(vm, predicate, &item, 1U, out_error);

            if (call_result == NULL) {
                TINYPY_DECREF(item);
                TINYPY_DECREF(selected);
                TINYPY_DECREF(input_iterator);
                return NULL;
            }
            truth = tinypy_truth(call_result, out_error);
            TINYPY_DECREF(call_result);
        }
        if (truth < 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(selected);
            TINYPY_DECREF(input_iterator);
            return NULL;
        }
        if (truth != 0) {
            if (tinypy_internal_list_append_checked(selected, item, out_error) == 0) {
                TINYPY_DECREF(item);
                TINYPY_DECREF(selected);
                TINYPY_DECREF(input_iterator);
                return NULL;
            }
        }
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(input_iterator);
    if (iteration_error != NULL) {
        TINYPY_DECREF(selected);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    if (TINYPY_VALUE_KIND(input) == TINYPY_VALUE_TUPLE) {
        size_t list_size = TINYPY_LIST_SIZE(selected);
        result = tinypy_tuple_from_items(vm, TINYPY_LIST_OBJECT(selected)->items, list_size);
    }
    else if (TINYPY_VALUE_KIND(input) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(input) == TINYPY_VALUE_UNICODE) {
        size_t total = 0U;
        uint8_t empty_buffer = 0U;
        uint8_t *buffer = &empty_buffer;
        size_t output = 0U;
        tinypy_bool_t overflow = TINYPY_FALSE;

        iterator = TINYPY_LIST_ITERATOR_BEGIN(selected);
        iterator_end = TINYPY_LIST_ITERATOR_END(selected);
        for (; iterator != iterator_end; ++iterator) {
            size_t item_size = TINYPY_TEXT_BYTE_SIZE(*iterator);

            if (item_size > SIZE_MAX - total) {
                overflow = TINYPY_TRUE;
                break;
            }
            total += item_size;
        }
        if (overflow != 0 || total >= (size_t)PTRDIFF_MAX) {
            TINYPY_DECREF(selected);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "filtered string is too large", out_error);
            return NULL;
        }
        if (total != 0U) {
            buffer = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, total, out_error);
            if (buffer == NULL) {
                TINYPY_DECREF(selected);
                return NULL;
            }
        }
        iterator = TINYPY_LIST_ITERATOR_BEGIN(selected);
        iterator_end = TINYPY_LIST_ITERATOR_END(selected);
        for (; iterator != iterator_end; ++iterator) {
            tinypy_value_t *item = *iterator;
            size_t size = TINYPY_TEXT_BYTE_SIZE(item);

            if (size != 0U) {
                (void)memcpy(buffer + output, TINYPY_TEXT_BYTES(item), size);
            }
            output += size;
        }
        if (TINYPY_VALUE_KIND(input) == TINYPY_VALUE_UNICODE) {
            uint8_t *result_bytes;

            result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_UNICODE, total, TINYPY_LIST_SIZE(selected), &result_bytes, out_error);
            if (result != NULL && total != 0U) {
                (void)memcpy(result_bytes, buffer, total);
            }
        }
        else {
            result = tinypy_internal_string_from_bytes_checked(vm, buffer, total, out_error);
        }
        if (total != 0U) {
            tinypy_internal_vm_deallocate(vm, buffer, total);
        }
    }
    else {
        size_t selected_size = TINYPY_LIST_SIZE(selected);
        size_t selected_capacity = TINYPY_LIST_OBJECT(selected)->allocated;

        if (selected_capacity - selected_size > selected_size / 4U + 8U) {
            tinypy_internal_list_shrink_to_fit(vm, selected);
        }
        result = selected;
        TINYPY_INCREF(result);
    }
    TINYPY_DECREF(selected);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_map(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t iterable_count;
    size_t iterable_index;
    size_t result_hint = 0U;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *callable = TINYPY_TUPLE_GET(args, 0U);
    iterable_count = TINYPY_TUPLE_SIZE(args) - 1U;
    if (iterable_count > SIZE_MAX / (2U * sizeof(tinypy_value_t *) + 2U * sizeof(uint8_t))) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "map temporary storage is too large", out_error);
        return NULL;
    }
    size_t scratch_size = iterable_count * (2U * sizeof(tinypy_value_t *) + 2U * sizeof(uint8_t));
    uint8_t *scratch = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, scratch_size, out_error);
    if (scratch == NULL) {
        return NULL;
    }
    tinypy_value_t **iterators = (tinypy_value_t **)scratch;
    tinypy_value_t **call_items = iterators + iterable_count;
    uint8_t *exhausted = (uint8_t *)(call_items + iterable_count);
    uint8_t *yielded = exhausted + iterable_count;

    (void)memset(exhausted, 0, iterable_count * sizeof(*exhausted));
    for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, iterable_index + 1U);
        size_t hint = tinypy_internal_iterable_size_hint(item);

        if (hint > result_hint) {
            result_hint = hint;
        }
        iterators[iterable_index] = tinypy_iter(item, out_error);
        if (iterators[iterable_index] == NULL) {
            while (iterable_index != 0U) {
                TINYPY_DECREF(iterators[--iterable_index]);
            }
            tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
            return NULL;
        }
    }
    result = tinypy_list_from_items(vm, NULL, 0U);
    if (tinypy_internal_list_reserve_checked(vm, result, result_hint, out_error) == 0) {
        TINYPY_DECREF(result);
        result = NULL;
        goto map_cleanup;
    }
    for (;;) {
        tinypy_bool_t have_item = TINYPY_FALSE;
        tinypy_value_t *mapped;

        (void)memset(yielded, 0, iterable_count * sizeof(*yielded));
        for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
            tinypy_error_t *iteration_error = NULL;

            if (exhausted[iterable_index] != 0U) {
                call_items[iterable_index] = &vm->none_object.base;
                continue;
            }
            call_items[iterable_index] = tinypy_next(iterators[iterable_index], &iteration_error);
            if (call_items[iterable_index] == NULL) {
                if (iteration_error != NULL) {
                    size_t release_index;

                    for (release_index = 0U; release_index < iterable_index; ++release_index) {
                        if (yielded[release_index] != 0U) {
                            TINYPY_DECREF(call_items[release_index]);
                        }
                    }
                    if (out_error != NULL) {
                        *out_error = iteration_error;
                    }
                    else {
                        tinypy_error_release(iteration_error);
                    }
                    TINYPY_DECREF(result);
                    result = NULL;
                    goto map_cleanup;
                }
                exhausted[iterable_index] = 1U;
                call_items[iterable_index] = &vm->none_object.base;
                continue;
            }
            yielded[iterable_index] = 1U;
            have_item = TINYPY_TRUE;
        }
        if (have_item == 0) {
            break;
        }
        if (TINYPY_VALUE_KIND(callable) == TINYPY_VALUE_NONE) {
            mapped = iterable_count == 1U ? call_items[0] : NULL;
            if (mapped != NULL) {
                TINYPY_INCREF(mapped);
            }
            else {
                mapped = tinypy_internal_tuple_from_items_checked(vm, call_items, iterable_count, out_error);
            }
        }
        else {
            mapped = __tinypy_builtin_call_items(vm, callable, call_items, iterable_count, out_error);
        }
        for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
            if (yielded[iterable_index] != 0U) {
                TINYPY_DECREF(call_items[iterable_index]);
            }
        }
        if (mapped == NULL) {
            TINYPY_DECREF(result);
            result = NULL;
            break;
        }
        if (tinypy_internal_list_append_checked(result, mapped, out_error) == 0) {
            TINYPY_DECREF(mapped);
            TINYPY_DECREF(result);
            result = NULL;
            break;
        }
        TINYPY_DECREF(mapped);
    }
map_cleanup:
    for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
        TINYPY_DECREF(iterators[iterable_index]);
    }
    tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_zip(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t iterable_count = TINYPY_TUPLE_SIZE(args);
    size_t iterable_index;
    size_t result_hint = SIZE_MAX;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0) {
        return NULL;
    }
    if (iterable_count == 0U) {
        tinypy_value_t *return_value_1 = tinypy_list_from_items(vm, NULL, 0U);
        return return_value_1;
    }
    if (iterable_count > SIZE_MAX / (2U * sizeof(tinypy_value_t *))) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "zip temporary storage is too large", out_error);
        return NULL;
    }
    size_t scratch_size = iterable_count * 2U * sizeof(tinypy_value_t *);
    tinypy_value_t **scratch = (tinypy_value_t **)tinypy_internal_vm_allocate_checked(vm, scratch_size, out_error);
    if (scratch == NULL) {
        return NULL;
    }
    tinypy_value_t **iterators = scratch;
    tinypy_value_t **tuple_items = iterators + iterable_count;
    for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, iterable_index);
        size_t hint = tinypy_internal_iterable_size_hint(item);

        if (hint < result_hint) {
            result_hint = hint;
        }
        iterators[iterable_index] = tinypy_iter(item, out_error);
        if (iterators[iterable_index] == NULL) {
            while (iterable_index != 0U) {
                TINYPY_DECREF(iterators[--iterable_index]);
            }
            tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
            return NULL;
        }
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    if (tinypy_internal_list_reserve_checked(vm, result, result_hint, out_error) == 0) {
        TINYPY_DECREF(result);
        result = NULL;
        goto zip_cleanup;
    }
    for (;;) {
        tinypy_value_t *tuple;

        for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
            tinypy_error_t *iteration_error = NULL;

            tuple_items[iterable_index] = tinypy_next(iterators[iterable_index], &iteration_error);
            if (tuple_items[iterable_index] == NULL) {
                size_t release_index;

                for (release_index = 0U; release_index < iterable_index; ++release_index) {
                    TINYPY_DECREF(tuple_items[release_index]);
                }
                if (iteration_error != NULL) {
                    if (out_error != NULL) {
                        *out_error = iteration_error;
                    }
                    else {
                        tinypy_error_release(iteration_error);
                    }
                    TINYPY_DECREF(result);
                    result = NULL;
                }
                goto zip_cleanup;
            }
        }
        tuple = tinypy_internal_tuple_from_items_checked(vm, tuple_items, iterable_count, out_error);
        for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
            TINYPY_DECREF(tuple_items[iterable_index]);
        }
        if (tuple == NULL) {
            TINYPY_DECREF(result);
            result = NULL;
            goto zip_cleanup;
        }
        if (tinypy_internal_list_append_checked(result, tuple, out_error) == 0) {
            TINYPY_DECREF(tuple);
            TINYPY_DECREF(result);
            result = NULL;
            goto zip_cleanup;
        }
        TINYPY_DECREF(tuple);
    }
zip_cleanup:
    for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
        TINYPY_DECREF(iterators[iterable_index]);
    }
    tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_sum(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *iterator;
    tinypy_value_t *total;
    int64_t integer_total = 0;
    tinypy_bool_t integer_fast_path;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *iterable = TINYPY_TUPLE_GET(args, 0U);
    iterator = tinypy_iter(iterable, out_error);
    if (iterator == NULL) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        total = TINYPY_TUPLE_GET(args, 1U);
        TINYPY_INCREF(total);
    }
    else {
        total = tinypy_integer_from_i64(vm, INT64_C(0));
    }
    if (TINYPY_VALUE_KIND(total) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(total) == TINYPY_VALUE_UNICODE) {
        TINYPY_DECREF(total);
        TINYPY_DECREF(iterator);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "sum() can't sum strings", out_error);
        return NULL;
    }
    integer_fast_path = total->type == &vm->types[TINYPY_VALUE_INTEGER] || total->type == &vm->types[TINYPY_VALUE_BOOL];
    if (integer_fast_path != 0) {
        integer_total = TINYPY_INTEGER_VALUE(total);
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        tinypy_value_t *next_total;

        if (item == NULL) {
            break;
        }
        if (integer_fast_path != 0 && (item->type == &vm->types[TINYPY_VALUE_INTEGER] || item->type == &vm->types[TINYPY_VALUE_BOOL])) {
            int64_t item_value = TINYPY_INTEGER_VALUE(item);

            if (!((item_value > 0 && integer_total > INT64_MAX - item_value) || (item_value < 0 && integer_total < INT64_MIN - item_value))) {
                integer_total += item_value;
                TINYPY_DECREF(item);
                if (total != NULL) {
                    TINYPY_DECREF(total);
                    total = NULL;
                }
                continue;
            }
        }
        if (total == NULL) {
            total = tinypy_integer_from_i64(vm, integer_total);
        }
        integer_fast_path = TINYPY_FALSE;
        next_total = tinypy_add(total, item, out_error);

        TINYPY_DECREF(item);
        TINYPY_DECREF(total);
        if (next_total == NULL) {
            TINYPY_DECREF(iterator);
            return NULL;
        }
        total = next_total;
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (total != NULL) {
            TINYPY_DECREF(total);
        }
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    if (total == NULL) {
        total = tinypy_integer_from_i64(vm, integer_total);
    }
    return total;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_min_max(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t want_max = user_data != NULL ? INT32_C(1) : INT32_C(0);
    tinypy_value_t *key_function = NULL;
    tinypy_value_t *iterator;
    tinypy_value_t *best;
    tinypy_value_t *best_key;
    tinypy_error_t *iteration_error = NULL;

    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "min/max expected at least one argument", out_error);
        return NULL;
    }
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        key_function = TINYPY_DICT_SIZE(kwargs) == 1U ? tinypy_dict_get_optional(kwargs, vm->key_key) : NULL;
        if (key_function == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "min/max received an unexpected keyword", out_error);
            return NULL;
        }
    }
    tinypy_value_t *source = TINYPY_TUPLE_SIZE(args) == 1U ? TINYPY_TUPLE_GET(args, 0U) : args;
    iterator = tinypy_iter(source, out_error);
    if (iterator == NULL) {
        return NULL;
    }
    best = tinypy_next(iterator, &iteration_error);
    if (best == NULL) {
        TINYPY_DECREF(iterator);
        if (iteration_error != NULL) {
            if (out_error != NULL) {
                *out_error = iteration_error;
            }
            else {
                tinypy_error_release(iteration_error);
            }
            return NULL;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "min/max argument is an empty sequence", out_error);
        return NULL;
    }
    if (key_function != NULL) {
        best_key = __tinypy_builtin_call_items(vm, key_function, &best, 1U, out_error);
        if (best_key == NULL) {
            TINYPY_DECREF(best);
            TINYPY_DECREF(iterator);
            return NULL;
        }
    }
    else {
        best_key = best;
        TINYPY_INCREF(best_key);
    }
    for (;;) {
        tinypy_value_t *candidate = tinypy_next(iterator, &iteration_error);
        tinypy_value_t *candidate_key;
        int32_t better;

        if (candidate == NULL) {
            break;
        }
        if (key_function != NULL) {
            candidate_key = __tinypy_builtin_call_items(vm, key_function, &candidate, 1U, out_error);
            if (candidate_key == NULL) {
                TINYPY_DECREF(candidate);
                TINYPY_DECREF(best_key);
                TINYPY_DECREF(best);
                TINYPY_DECREF(iterator);
                return NULL;
            }
        }
        else {
            candidate_key = candidate;
            TINYPY_INCREF(candidate_key);
        }
        better = tinypy_compare_bool(candidate_key, best_key, want_max != 0 ? TINYPY_COMPARE_GREATER : TINYPY_COMPARE_LESS, out_error);
        if (better < 0) {
            TINYPY_DECREF(candidate_key);
            TINYPY_DECREF(candidate);
            TINYPY_DECREF(best_key);
            TINYPY_DECREF(best);
            TINYPY_DECREF(iterator);
            return NULL;
        }
        if (better != 0) {
            TINYPY_DECREF(best_key);
            TINYPY_DECREF(best);
            best_key = candidate_key;
            best = candidate;
        }
        else {
            TINYPY_DECREF(candidate_key);
            TINYPY_DECREF(candidate);
        }
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        TINYPY_DECREF(best_key);
        TINYPY_DECREF(best);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    TINYPY_DECREF(best_key);
    return best;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_chr_common(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t value;

    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 ||
        __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_builtin_integer_as_i64(vm, item, &value, out_error) == 0) {
        return NULL;
    }
    if (user_data == NULL) {
        uint8_t byte;

        if (value < 0 || value > 255) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "chr() argument not in range(256)", out_error);
            return NULL;
        }
        byte = (uint8_t)value;
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, &byte, 1U);
        return return_value_1;
    }
    if (value < 0 || value > INT64_C(0x10ffff)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unichr() argument out of range", out_error);
        return NULL;
    }
    char utf8[4];
    size_t size;

    if (value < 0x80) {
        utf8[0] = (char)value;
        size = 1U;
    }
    else if (value < 0x800) {
        utf8[0] = (char)(0xc0 | (value >> 6));
        utf8[1] = (char)(0x80 | (value & 0x3f));
        size = 2U;
    }
    else if (value < 0x10000) {
        utf8[0] = (char)(0xe0 | (value >> 12));
        utf8[1] = (char)(0x80 | ((value >> 6) & 0x3f));
        utf8[2] = (char)(0x80 | (value & 0x3f));
        size = 3U;
    }
    else {
        utf8[0] = (char)(0xf0 | (value >> 18));
        utf8[1] = (char)(0x80 | ((value >> 12) & 0x3f));
        utf8[2] = (char)(0x80 | ((value >> 6) & 0x3f));
        utf8[3] = (char)(0x80 | (value & 0x3f));
        size = 4U;
    }
    tinypy_value_t *return_value_2 = tinypy_unicode_from_utf8(vm, utf8, size);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_builtin_coerce_one(tinypy_value_t *left, tinypy_value_t *right, tinypy_bool_t reverse, tinypy_value_t **out_result, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *method;
    tinypy_value_t *arguments;
    tinypy_value_t *result;

    if (tinypy_internal_object_has_special(left, "__coerce__", 10U) == 0) {
        return INT32_C(0);
    }
    method = tinypy_internal_object_get_special(left, "__coerce__", 10U, out_error);
    if (method == NULL) {
        return -INT32_C(1);
    }
    arguments = tinypy_tuple_from_items(vm, &right, 1U);
    result = tinypy_call(method, arguments, NULL, out_error);
    TINYPY_DECREF(arguments);
    TINYPY_DECREF(method);
    if (result == NULL) {
        return -INT32_C(1);
    }
    if (result == &vm->not_implemented_object.base || (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_OLD_INSTANCE && TINYPY_VALUE_KIND(result) == TINYPY_VALUE_NONE)) {
        TINYPY_DECREF(result);
        return INT32_C(0);
    }
    if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(result) != 2U) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__coerce__ must return a 2-tuple or NotImplemented", out_error);
        return -INT32_C(1);
    }
    if (reverse != 0) {
        tinypy_value_t *items[2] = {TINYPY_TUPLE_GET(result, 1U), TINYPY_TUPLE_GET(result, 0U)};
        tinypy_value_t *ordered = tinypy_tuple_from_items(vm, items, 2U);

        TINYPY_DECREF(result);
        result = ordered;
    }
    *out_result = result;
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_coerce(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *left;
    tinypy_value_t *right;
    tinypy_value_t *result;
    int32_t status;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    left = TINYPY_TUPLE_GET(args, 0U);
    right = TINYPY_TUPLE_GET(args, 1U);
    status = __tinypy_builtin_coerce_one(left, right, TINYPY_FALSE, &result, out_error);
    if (status > 0) {
        return result;
    }
    if (status < 0) {
        return NULL;
    }
    status = __tinypy_builtin_coerce_one(right, left, TINYPY_TRUE, &result, out_error);
    if (status > 0) {
        return result;
    }
    if (status < 0) {
        return NULL;
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    if (left->type == right->type && (left->type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U && kind != TINYPY_VALUE_OLD_INSTANCE && kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE && kind != TINYPY_VALUE_SET && kind != TINYPY_VALUE_FROZENSET) {
        tinypy_value_t *items[2] = {left, right};
        tinypy_value_t *return_value_1 = tinypy_tuple_from_items(vm, items, 2U);
        return return_value_1;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "number coercion failed", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_cmp(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t equal;
    int32_t less;
    int32_t greater;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    if ((TINYPY_VALUE_KIND(left) == TINYPY_VALUE_SET || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_FROZENSET) &&
        (TINYPY_VALUE_KIND(right) == TINYPY_VALUE_SET || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_FROZENSET)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot compare sets using cmp()", out_error);
        return NULL;
    }
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
    if (less != 0) {
        tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, INT64_C(-1));
        return return_value_2;
    }
    greater = tinypy_compare_bool(left, right, TINYPY_COMPARE_GREATER, out_error);
    if (greater < 0) {
        return NULL;
    }
    tinypy_value_t *return_value_3 = tinypy_integer_from_i64(vm, greater != 0 ? INT64_C(1) : INT64_C(0));
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_hash(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_hash_t hash = tinypy_internal_hash_value(value, out_error);
    if (tinypy_vm_has_error(vm) != 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)hash);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_pow(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *return_value_1 = TINYPY_TUPLE_SIZE(args) == 3U
                                        ? tinypy_internal_power_modulo(item, item_2, TINYPY_TUPLE_GET(args, 2U), out_error)
                                        : tinypy_power(item, item_2, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
#define TINYPY_ROUND_BIGINT_WORDS ((size_t)80U)

typedef struct tinypy_round_bigint_t {
    uint32_t words[TINYPY_ROUND_BIGINT_WORDS];
    size_t count;
} tinypy_round_bigint_t;

//////////////////////////////////////////////////////////////////////////
static void __tinypy_round_bigint_normalize(tinypy_round_bigint_t *value) {
    while (value->count != 0U && value->words[value->count - 1U] == 0U) {
        value->count -= 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_round_bigint_set_u64(tinypy_round_bigint_t *value, uint64_t integer) {
    (void)memset(value, 0, sizeof(*value));
    if (integer != 0U) {
        value->words[0] = (uint32_t)integer;
        value->words[1] = (uint32_t)(integer >> 32U);
        value->count = value->words[1] == 0U ? 1U : 2U;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_round_bigint_multiply_u32(tinypy_round_bigint_t *value, uint32_t multiplier) {
    uint64_t carry = 0U;
    size_t index;

    for (index = 0U; index < value->count; ++index) {
        uint64_t product = (uint64_t)value->words[index] * multiplier + carry;

        value->words[index] = (uint32_t)product;
        carry = product >> 32U;
    }
    if (carry != 0U) {
        if (value->count == TINYPY_ROUND_BIGINT_WORDS) {
            return TINYPY_FALSE;
        }
        value->words[value->count++] = (uint32_t)carry;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_round_bigint_shift_left(tinypy_round_bigint_t *value, size_t shift) {
    size_t word_shift;
    uint32_t bit_shift;
    size_t old_count;
    size_t new_count;
    size_t index;

    if (value->count == 0U || shift == 0U) {
        return TINYPY_TRUE;
    }
    word_shift = shift / 32U;
    bit_shift = (uint32_t)(shift % 32U);
    old_count = value->count;
    new_count = old_count + word_shift + (bit_shift != 0U ? 1U : 0U);
    if (new_count > TINYPY_ROUND_BIGINT_WORDS) {
        return TINYPY_FALSE;
    }
    for (index = new_count; index != 0U; --index) {
        size_t output_index = index - 1U;
        uint64_t word = 0U;

        if (output_index >= word_shift) {
            size_t source_index = output_index - word_shift;

            if (source_index < old_count) {
                word |= (uint64_t)value->words[source_index] << bit_shift;
            }
            if (bit_shift != 0U && source_index != 0U && source_index - 1U < old_count) {
                word |= (uint64_t)value->words[source_index - 1U] >> (32U - bit_shift);
            }
        }
        value->words[output_index] = (uint32_t)word;
    }
    value->count = new_count;
    __tinypy_round_bigint_normalize(value);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_round_bigint_bit_length(const tinypy_round_bigint_t *value) {
    size_t bits;
    uint32_t high;

    if (value->count == 0U) {
        return 0U;
    }
    bits = (value->count - 1U) * 32U;
    high = value->words[value->count - 1U];
    while (high != 0U) {
        bits += 1U;
        high >>= 1U;
    }
    return bits;
}
//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_round_bigint_shifted_word(const tinypy_round_bigint_t *value, size_t output_index, size_t shift) {
    size_t word_shift = shift / 32U;
    uint32_t bit_shift = (uint32_t)(shift % 32U);
    uint64_t word = 0U;

    if (output_index >= word_shift) {
        size_t source_index = output_index - word_shift;

        if (source_index < value->count) {
            word |= (uint64_t)value->words[source_index] << bit_shift;
        }
        if (bit_shift != 0U && source_index != 0U && source_index - 1U < value->count) {
            word |= (uint64_t)value->words[source_index - 1U] >> (32U - bit_shift);
        }
    }
    return (uint32_t)word;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_round_bigint_shifted_count(const tinypy_round_bigint_t *value, size_t shift) {
    size_t count;

    if (value->count == 0U) {
        return 0U;
    }
    count = value->count + shift / 32U + (shift % 32U != 0U ? 1U : 0U);
    while (count != 0U && __tinypy_round_bigint_shifted_word(value, count - 1U, shift) == 0U) {
        count -= 1U;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_round_bigint_compare_shifted(const tinypy_round_bigint_t *left, const tinypy_round_bigint_t *right, size_t right_shift) {
    size_t right_count = __tinypy_round_bigint_shifted_count(right, right_shift);
    size_t index;

    if (left->count != right_count) {
        return left->count < right_count ? -1 : 1;
    }
    index = left->count;
    while (index != 0U) {
        uint32_t left_word;
        uint32_t right_word;

        index -= 1U;
        left_word = left->words[index];
        right_word = __tinypy_round_bigint_shifted_word(right, index, right_shift);
        if (left_word != right_word) {
            return left_word < right_word ? -1 : 1;
        }
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_round_bigint_subtract_shifted(tinypy_round_bigint_t *left, const tinypy_round_bigint_t *right, size_t right_shift) {
    size_t right_count = __tinypy_round_bigint_shifted_count(right, right_shift);
    uint64_t borrow = 0U;
    size_t index;

    for (index = 0U; index < left->count; ++index) {
        uint64_t subtrahend = (index < right_count ? (uint64_t)__tinypy_round_bigint_shifted_word(right, index, right_shift) : 0U) + borrow;
        uint64_t minuend = left->words[index];

        left->words[index] = (uint32_t)(minuend - subtrahend);
        borrow = minuend < subtrahend ? 1U : 0U;
    }
    __tinypy_round_bigint_normalize(left);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_round_bigint_set_bit(tinypy_round_bigint_t *value, size_t bit) {
    size_t word = bit / 32U;

    while (value->count <= word) {
        value->words[value->count++] = 0U;
    }
    value->words[word] |= UINT32_C(1) << (bit % 32U);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_round_bigint_increment(tinypy_round_bigint_t *value) {
    size_t index = 0U;

    while (index < value->count && value->words[index] == UINT32_MAX) {
        value->words[index++] = 0U;
    }
    if (index == value->count) {
        if (value->count == TINYPY_ROUND_BIGINT_WORDS) {
            return TINYPY_FALSE;
        }
        value->words[value->count++] = 1U;
    }
    else {
        value->words[index] += 1U;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_round_bigint_divide(const tinypy_round_bigint_t *numerator, const tinypy_round_bigint_t *denominator, tinypy_round_bigint_t *quotient, tinypy_round_bigint_t *remainder) {
    size_t numerator_bits;
    size_t denominator_bits;
    size_t shift;

    (void)memset(quotient, 0, sizeof(*quotient));
    *remainder = *numerator;
    numerator_bits = __tinypy_round_bigint_bit_length(remainder);
    denominator_bits = __tinypy_round_bigint_bit_length(denominator);
    if (denominator_bits == 0U) {
        return TINYPY_FALSE;
    }
    if (numerator_bits < denominator_bits) {
        return TINYPY_TRUE;
    }
    shift = numerator_bits - denominator_bits;
    for (;;) {
        if (__tinypy_round_bigint_compare_shifted(remainder, denominator, shift) >= 0) {
            __tinypy_round_bigint_subtract_shifted(remainder, denominator, shift);
            __tinypy_round_bigint_set_bit(quotient, shift);
        }
        if (shift == 0U) {
            break;
        }
        shift -= 1U;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_round_bigint_divide_u32(tinypy_round_bigint_t *value, uint32_t divisor) {
    uint64_t remainder = 0U;
    size_t index = value->count;

    while (index != 0U) {
        uint64_t dividend;

        index -= 1U;
        dividend = (remainder << 32U) | value->words[index];
        value->words[index] = (uint32_t)(dividend / divisor);
        remainder = dividend % divisor;
    }
    __tinypy_round_bigint_normalize(value);
    return (uint32_t)remainder;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_round_bigint_decimal(const tinypy_round_bigint_t *value, char *digits, size_t capacity) {
    tinypy_round_bigint_t copy = *value;
    size_t position = capacity;

    if (copy.count == 0U) {
        digits[0] = '0';
        return 1U;
    }
    while (copy.count != 0U) {
        if (position == 0U) {
            return 0U;
        }
        digits[--position] = (char)('0' + __tinypy_round_bigint_divide_u32(&copy, 10U));
    }
    (void)memmove(digits, digits + position, capacity - position);
    return capacity - position;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_round_double(tinypy_vm_t *vm, double number, int32_t digits, double *out_result) {
    tinypy_round_bigint_t numerator;
    tinypy_round_bigint_t denominator;
    tinypy_round_bigint_t quotient;
    tinypy_round_bigint_t remainder;
    tinypy_round_bigint_t doubled_remainder;
    char decimal_digits[704];
    char decimal[704];
    double fraction;
    double result;
    uint64_t mantissa;
    size_t decimal_digit_count;
    size_t position = 0U;
    size_t index;
    int exponent;
    int32_t binary_shift;

    fraction = frexp(fabs(number), &exponent);
    mantissa = (uint64_t)ldexp(fraction, DBL_MANT_DIG);
    __tinypy_round_bigint_set_u64(&numerator, mantissa);
    __tinypy_round_bigint_set_u64(&denominator, UINT64_C(1));
    if (digits >= 0) {
        for (index = 0U; index < (size_t)digits; ++index) {
            if (__tinypy_round_bigint_multiply_u32(&numerator, 5U) == 0) {
                return TINYPY_FALSE;
            }
        }
        binary_shift = (int32_t)(exponent - DBL_MANT_DIG) + digits;
    }
    else {
        for (index = 0U; index < (size_t)(-digits); ++index) {
            if (__tinypy_round_bigint_multiply_u32(&denominator, 5U) == 0) {
                return TINYPY_FALSE;
            }
        }
        binary_shift = (int32_t)(exponent - DBL_MANT_DIG) + digits;
    }
    if (binary_shift >= 0) {
        if (__tinypy_round_bigint_shift_left(&numerator, (size_t)binary_shift) == 0) {
            return TINYPY_FALSE;
        }
    }
    else if (__tinypy_round_bigint_shift_left(&denominator, (size_t)(-binary_shift)) == 0) {
        return TINYPY_FALSE;
    }
    if (__tinypy_round_bigint_divide(&numerator, &denominator, &quotient, &remainder) == 0) {
        return TINYPY_FALSE;
    }
    doubled_remainder = remainder;
    if (__tinypy_round_bigint_shift_left(&doubled_remainder, 1U) == 0) {
        return TINYPY_FALSE;
    }
    if (__tinypy_round_bigint_compare_shifted(&doubled_remainder, &denominator, 0U) >= 0 && __tinypy_round_bigint_increment(&quotient) == 0) {
        return TINYPY_FALSE;
    }
    decimal_digit_count = __tinypy_round_bigint_decimal(&quotient, decimal_digits, sizeof(decimal_digits));
    if (decimal_digit_count == 0U) {
        return TINYPY_FALSE;
    }
    if (signbit(number) != 0) {
        decimal[position++] = '-';
    }
    if (digits > 0) {
        size_t fractional_digits = (size_t)digits;

        if (decimal_digit_count > fractional_digits) {
            size_t integer_digits = decimal_digit_count - fractional_digits;

            (void)memcpy(decimal + position, decimal_digits, integer_digits);
            position += integer_digits;
            decimal[position++] = '.';
            (void)memcpy(decimal + position, decimal_digits + integer_digits, fractional_digits);
            position += fractional_digits;
        }
        else {
            decimal[position++] = '0';
            decimal[position++] = '.';
            (void)memset(decimal + position, '0', fractional_digits - decimal_digit_count);
            position += fractional_digits - decimal_digit_count;
            (void)memcpy(decimal + position, decimal_digits, decimal_digit_count);
            position += decimal_digit_count;
        }
    }
    else {
        (void)memcpy(decimal + position, decimal_digits, decimal_digit_count);
        position += decimal_digit_count;
        if (digits < 0) {
            (void)memset(decimal + position, '0', (size_t)(-digits));
            position += (size_t)(-digits);
        }
    }
    if (position >= sizeof(decimal)) {
        return TINYPY_FALSE;
    }
    if (tinypy_internal_decimal_double(vm, decimal, position, &result) == 0 || isfinite(result) == 0) {
        return TINYPY_FALSE;
    }
    *out_result = result;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_round(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    static const char *const parameter_names[] = {"number", "ndigits"};
    static const size_t parameter_name_sizes[] = {6U, 7U};
    tinypy_value_t *arguments[2];
    tinypy_value_t *value;
    double number;
    int64_t digits = 0;
    double result;

    (void)user_data;
    if (__tinypy_builtin_named_arguments(vm, args, kwargs, parameter_names, parameter_name_sizes, 2U, 1U, arguments, out_error) == 0) {
        return NULL;
    }
    value = arguments[0];
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FLOAT) {
        number = TINYPY_FLOAT_OBJECT(value)->value;
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_INTEGER) {
        number = (double)TINYPY_INTEGER_VALUE(value);
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG) {
        if (tinypy_long_as_double(value, &number, out_error) == 0) {
            return NULL;
        }
    }
    else if (tinypy_internal_object_has_special(value, "__float__", 9U) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special(value, "__float__", 9U, out_error);
        tinypy_value_t *empty_args;
        tinypy_value_t *converted;

        if (method == NULL) {
            return NULL;
        }
        empty_args = tinypy_tuple_from_items(vm, NULL, 0U);
        converted = tinypy_call(method, empty_args, NULL, out_error);
        TINYPY_DECREF(empty_args);
        TINYPY_DECREF(method);
        if (converted == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(converted) != TINYPY_VALUE_FLOAT) {
            TINYPY_DECREF(converted);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__float__ returned a non-float", out_error);
            return NULL;
        }
        number = TINYPY_FLOAT_OBJECT(converted)->value;
        TINYPY_DECREF(converted);
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "round() argument must be a number", out_error);
        return NULL;
    }
    if (arguments[1] != NULL && tinypy_internal_index_as_i64(arguments[1], &digits, TINYPY_TRUE, out_error) == 0) {
        return NULL;
    }
    if (isfinite(number) == 0 || number == 0.0) {
        tinypy_value_t *return_value_1 = tinypy_float_from_double(vm, number);
        return return_value_1;
    }
    if (digits > (int64_t)((DBL_MANT_DIG - DBL_MIN_EXP) * 0.30103)) {
        tinypy_value_t *return_value_2 = tinypy_float_from_double(vm, number);
        return return_value_2;
    }
    if (digits < -(int64_t)((DBL_MAX_EXP + 1) * 0.30103)) {
        double signed_value = copysign(0.0, number);
        tinypy_value_t *return_value_3 = tinypy_float_from_double(vm, signed_value);
        return return_value_3;
    }
    if (digits == 0) {
        tinypy_value_t *return_value_4 = tinypy_float_from_double(vm, round(number));
        return return_value_4;
    }
    if (__tinypy_builtin_round_double(vm, number, (int32_t)digits, &result) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "rounded value is too large to represent", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_5 = tinypy_float_from_double(vm, result);
    return return_value_5;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_frame_dict(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *value;

    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 0U, 0U, out_error) == 0) {
        return NULL;
    }
    if (vm->current_frame == NULL) {
        value = vm->builtins;
    }
    else {
        value = user_data != NULL ? vm->current_frame->globals : tinypy_internal_frame_locals(vm->current_frame);
    }
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_builtin_dir_add_dict(tinypy_value_t *names, tinypy_value_t *dict) {
    tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(dict);
    tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(dict);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(names);

    for (; iterator != iterator_end; ++iterator) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator) && (TINYPY_VALUE_KIND(iterator->key) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(iterator->key) == TINYPY_VALUE_UNICODE)) {
            tinypy_dict_set(names, iterator->key, &vm->none_object.base);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_builtin_dir_add_name(tinypy_vm_t *vm, tinypy_value_t *names, const char *name, size_t name_size) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(names, key, &vm->none_object.base);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_builtin_dir_add_class(tinypy_value_t *names, tinypy_value_t *class_value) {
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT(class_value);
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    __tinypy_builtin_dir_add_dict(names, class_object->dict);
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(class_object->bases);
    iterator_end = TINYPY_TUPLE_ITERATOR_END(class_object->bases);
    for (; iterator != iterator_end; ++iterator) {
        __tinypy_builtin_dir_add_class(names, *iterator);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_dir_sort(tinypy_vm_t *vm, tinypy_value_t *result, tinypy_error_t **out_error) {
    tinypy_value_t *sort_function = tinypy_type_get_attr(&vm->types[TINYPY_VALUE_LIST], "sort", 4U);
    tinypy_value_t *arguments = tinypy_tuple_from_items(vm, &result, 1U);
    tinypy_value_t *sort_result = tinypy_call(sort_function, arguments, NULL, out_error);

    TINYPY_DECREF(arguments);
    if (sort_result == NULL) {
        TINYPY_DECREF(result);
        return NULL;
    }
    TINYPY_DECREF(sort_result);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_print_keyword(const tinypy_value_t *key, const char *name, size_t name_size) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(key);
    tinypy_bool_t result = (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE)
                           && TINYPY_TEXT_BYTE_SIZE(key) == name_size
                           && memcmp(TINYPY_TEXT_BYTES(key), name, name_size) == 0
                               ? TINYPY_TRUE
                               : TINYPY_FALSE;

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_print_affix(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_NONE || kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "sep and end must be strings or None", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_print_affix_value(tinypy_vm_t *vm, tinypy_value_t *value, const char *default_bytes, size_t default_size, tinypy_bool_t unicode_default) {
    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_NONE) {
        TINYPY_INCREF(value);
        return value;
    }
    if (unicode_default != 0) {
        tinypy_value_t *return_value_1 = tinypy_unicode_from_utf8(vm, default_bytes, default_size);
        return return_value_1;
    }
    tinypy_value_t *return_value_2 = tinypy_string_from_bytes(vm, default_bytes, default_size);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_print(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *separator = &vm->none_object.base;
    tinypy_value_t *ending = &vm->none_object.base;
    tinypy_value_t *target = &vm->none_object.base;
    tinypy_value_t *separator_text = NULL;
    tinypy_value_t *ending_text = NULL;
    tinypy_bool_t unicode_default = TINYPY_FALSE;
    tinypy_value_t *result = NULL;
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    size_t index;

    (void)user_data;
    if (kwargs != NULL) {
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(kwargs);
        tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(kwargs);

        for (; iterator != iterator_end; ++iterator) {
            if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator) == 0) {
                continue;
            }
            if (__tinypy_builtin_print_keyword(iterator->key, "sep", 3U) != 0) {
                separator = iterator->value;
            }
            else if (__tinypy_builtin_print_keyword(iterator->key, "end", 3U) != 0) {
                ending = iterator->value;
            }
            else if (__tinypy_builtin_print_keyword(iterator->key, "file", 4U) != 0) {
                target = iterator->value;
            }
            else {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "print() received an unexpected keyword argument", out_error);
                return NULL;
            }
        }
    }
    if (__tinypy_builtin_print_affix(vm, separator, out_error) == 0
        || __tinypy_builtin_print_affix(vm, ending, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(target) == TINYPY_VALUE_NONE) {
        tinypy_value_t *module = tinypy_dict_get(vm->modules, vm->sys_key);

        target = tinypy_module_get_value(module, "stdout", 6U);
        if (target == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "lost sys.stdout", out_error);
            return NULL;
        }
    }
    unicode_default = TINYPY_VALUE_KIND(separator) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(ending) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
    for (index = 0U; unicode_default == 0 && index < argument_count; ++index) {
        if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, index)) == TINYPY_VALUE_UNICODE) {
            unicode_default = TINYPY_TRUE;
        }
    }
    if (argument_count > 1U) {
        separator_text = __tinypy_builtin_print_affix_value(vm, separator, " ", 1U, unicode_default);
    }
    ending_text = __tinypy_builtin_print_affix_value(vm, ending, "\n", 1U, unicode_default);
    for (index = 0U; index < argument_count; ++index) {
        tinypy_value_t *text;
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, index);
        tinypy_value_type_e item_kind = TINYPY_VALUE_KIND(item);

        if (index != 0U && tinypy_internal_output_write_value(target, separator_text, out_error) == 0) {
            goto cleanup;
        }
        if (item_kind == TINYPY_VALUE_STRING || item_kind == TINYPY_VALUE_UNICODE) {
            text = item;
            TINYPY_INCREF(text);
        }
        else {
            text = tinypy_object_str(item, out_error);
            if (text == NULL) {
                goto cleanup;
            }
        }
        if (tinypy_internal_output_write_value(target, text, out_error) == 0) {
            TINYPY_DECREF(text);
            goto cleanup;
        }
        TINYPY_DECREF(text);
    }
    if (tinypy_internal_output_write_value(target, ending_text, out_error) != 0) {
        result = tinypy_none_get(vm);
    }
cleanup:
    if (ending_text != NULL) {
        TINYPY_DECREF(ending_text);
    }
    if (separator_text != NULL) {
        TINYPY_DECREF(separator_text);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_dir(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_dict_entry_t *iterator;
    tinypy_dict_entry_t *iterator_end;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 0U, 1U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 1U) {
        tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);

        if (tinypy_internal_object_has_special(value, "__dir__", 7U) != 0) {
            tinypy_value_t *method = tinypy_internal_object_get_special(value, "__dir__", 7U, out_error);
            tinypy_value_t *empty;
            tinypy_value_t *result;

            if (method == NULL) {
                return NULL;
            }
            empty = tinypy_tuple_from_items(vm, NULL, 0U);
            result = tinypy_call(method, empty, NULL, out_error);
            TINYPY_DECREF(empty);
            TINYPY_DECREF(method);
            if (result == NULL) {
                return NULL;
            }
            if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_LIST) {
                TINYPY_DECREF(result);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__dir__() must return a list", out_error);
                return NULL;
            }
            tinypy_value_t *sorted = __tinypy_builtin_dir_sort(vm, result, out_error);

            return sorted;
        }
    }
    tinypy_value_t *names = tinypy_dict_new(vm);
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        __tinypy_builtin_dir_add_dict(names, vm->current_frame != NULL ? tinypy_internal_frame_locals(vm->current_frame) : vm->builtins);
    }
    else {
        tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
        tinypy_type_t *type = value->type;
        size_t mro_index;

        if (kind != TINYPY_VALUE_MODULE && kind != TINYPY_VALUE_TYPE) {
            tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);

            if (dict_slot != NULL && *dict_slot != NULL) {
                __tinypy_builtin_dir_add_dict(names, *dict_slot);
            }
        }
        if (kind == TINYPY_VALUE_MODULE) {
            tinypy_value_t *module_dict = tinypy_module_dict(value);
            __tinypy_builtin_dir_add_dict(names, module_dict);
        }
        else if (kind == TINYPY_VALUE_CLASS) {
            __tinypy_builtin_dir_add_class(names, value);
        }
        else if (kind == TINYPY_VALUE_OLD_INSTANCE) {
            tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(value);

            __tinypy_builtin_dir_add_dict(names, instance->dict);
            __tinypy_builtin_dir_add_class(names, instance->class_object);
        }
        else if (kind == TINYPY_VALUE_TYPE) {
            tinypy_type_t *represented_type = (tinypy_type_t *)value;

            for (mro_index = 0U; mro_index < tinypy_type_mro_size(represented_type); ++mro_index) {
                const tinypy_type_t *mro_type = tinypy_type_mro_at(represented_type, mro_index);
                const tinypy_value_t *type_dict = tinypy_type_dict(mro_type);

                __tinypy_builtin_dir_add_dict(names, (tinypy_value_t *)type_dict);
            }
            if (represented_type == &vm->types[TINYPY_VALUE_TYPE]) {
                static const struct {
                    const char *name;
                    size_t size;
                } type_metadata[] = {
                    {"__abstractmethods__", 19U}, {"__base__", 8U}, {"__bases__", 9U},
                    {"__basicsize__", 13U}, {"__dict__", 8U}, {"__dictoffset__", 14U},
                    {"__flags__", 9U}, {"__itemsize__", 12U}, {"__module__", 10U},
                    {"__mro__", 7U}, {"__name__", 8U}, {"__weakrefoffset__", 17U}
                };
                size_t metadata_index;

                for (metadata_index = 0U; metadata_index < sizeof(type_metadata) / sizeof(type_metadata[0]); ++metadata_index) {
                    __tinypy_builtin_dir_add_name(vm, names, type_metadata[metadata_index].name, type_metadata[metadata_index].size);
                }
            }
        }
        else if (kind == TINYPY_VALUE_FUNCTION && TINYPY_FUNCTION_OBJECT(value)->dict != NULL) {
            __tinypy_builtin_dir_add_dict(names, TINYPY_FUNCTION_OBJECT(value)->dict);
        }
        if (kind != TINYPY_VALUE_MODULE && kind != TINYPY_VALUE_TYPE && kind != TINYPY_VALUE_CLASS && kind != TINYPY_VALUE_OLD_INSTANCE) {
            for (mro_index = 0U; mro_index < tinypy_type_mro_size(type); ++mro_index) {
                const tinypy_type_t *mro_type = tinypy_type_mro_at(type, mro_index);

                const tinypy_value_t *type_dict = tinypy_type_dict(mro_type);
                __tinypy_builtin_dir_add_dict(names, (tinypy_value_t *)type_dict);
            }
        }
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    if (tinypy_internal_list_reserve_checked(vm, result, TINYPY_DICT_SIZE(names), out_error) == 0) {
        TINYPY_DECREF(result);
        TINYPY_DECREF(names);
        return NULL;
    }
    iterator = TINYPY_DICT_ITERATOR_BEGIN(names);
    iterator_end = TINYPY_DICT_ITERATOR_END(names);
    for (; iterator != iterator_end; ++iterator) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
            if (tinypy_internal_list_append_checked(result, iterator->key, out_error) == 0) {
                TINYPY_DECREF(result);
                TINYPY_DECREF(names);
                return NULL;
            }
        }
    }
    TINYPY_DECREF(names);
    tinypy_value_t *sorted = __tinypy_builtin_dir_sort(vm, result, out_error);

    return sorted;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_import(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    static const char *const parameter_names[] = {"name", "globals", "locals", "fromlist", "level"};
    static const size_t parameter_name_sizes[] = {4U, 7U, 6U, 8U, 5U};
    tinypy_value_t *arguments[5] = {NULL, NULL, NULL, NULL, NULL};
    size_t positional_count = TINYPY_TUPLE_SIZE(args);
    size_t keyword_count = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    size_t recognized_keyword_count = 0U;
    size_t index;
    const char *name_bytes;
    size_t name_size;
    tinypy_value_t *globals = vm->current_frame != NULL ? vm->current_frame->globals : NULL;
    tinypy_value_t *fromlist = NULL;
    int64_t level = -1;

    (void)user_data;
    if (positional_count > 5U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__import__ received too many positional arguments", out_error);
        return NULL;
    }
    for (index = 0U; index < positional_count; index += 1U) {
        arguments[index] = TINYPY_TUPLE_GET(args, index);
    }
    for (index = 0U; index < 5U; index += 1U) {
        tinypy_value_t *key;
        tinypy_value_t *keyword_value;

        if (kwargs == NULL || keyword_count == 0U) {
            break;
        }
        key = tinypy_string_from_bytes(vm, parameter_names[index], parameter_name_sizes[index]);
        keyword_value = tinypy_dict_get_optional(kwargs, key);
        TINYPY_DECREF(key);
        if (keyword_value == NULL) {
            continue;
        }
        recognized_keyword_count += 1U;
        if (arguments[index] != NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__import__ received multiple values for one argument", out_error);
            return NULL;
        }
        arguments[index] = keyword_value;
    }
    if (recognized_keyword_count != keyword_count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__import__ received an unexpected keyword argument", out_error);
        return NULL;
    }
    if (arguments[0] == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__import__ missing required argument 'name'", out_error);
        return NULL;
    }
    tinypy_value_t *name = arguments[0];
    if (__tinypy_builtin_text_view(vm, name, &name_bytes, &name_size, out_error) == 0) {
        return NULL;
    }
    tinypy_bool_t condition_7 = arguments[1] != NULL;
    if (condition_7 != 0) {
        tinypy_value_t *item = arguments[1];
        condition_7 = TINYPY_VALUE_KIND(item) != TINYPY_VALUE_NONE;
    }
    if (condition_7) {
        globals = arguments[1];
        if (TINYPY_VALUE_KIND(globals) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__import__ globals must be a dictionary", out_error);
            return NULL;
        }
    }
    if (arguments[3] != NULL) {
        fromlist = arguments[3];
    }
    tinypy_bool_t condition_8 = arguments[4] != NULL;
    if (condition_8 != 0) {
        tinypy_value_t *item = arguments[4];
        condition_8 = __tinypy_builtin_integer_as_i64(vm, item, &level, out_error) == 0;
    }
    if (condition_8) {
        return NULL;
    }
    if (level < INT32_MIN || level > INT32_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__import__ level is out of range", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_import_module(vm, name_bytes, name_size, globals, fromlist, (int32_t)level, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_reload(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_reload_module(TINYPY_TUPLE_GET(args, 0U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_abs(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_type_e kind;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_internal_object_has_special_override(value, "__abs__", 7U) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special(value, "__abs__", 7U, out_error);
        tinypy_value_t *empty;
        tinypy_value_t *result;

        if (method == NULL) {
            return NULL;
        }
        empty = tinypy_tuple_from_items(vm, NULL, 0U);
        result = tinypy_call(method, empty, NULL, out_error);
        TINYPY_DECREF(empty);
        TINYPY_DECREF(method);
        return result;
    }
    kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_BOOL) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));
        return return_value_1;
    }
    if (kind == TINYPY_VALUE_INTEGER) {
        if (TINYPY_INTEGER_VALUE(value) < 0) {
            tinypy_value_t *return_value_2 = tinypy_negative(value, out_error);
            return return_value_2;
        }
        TINYPY_INCREF(value);
        return value;
    }
    if (kind == TINYPY_VALUE_LONG) {
        if (TINYPY_LONG_SIGN(value) < 0) {
            tinypy_value_t *return_value_3 = tinypy_negative(value, out_error);
            return return_value_3;
        }
        TINYPY_INCREF(value);
        return value;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        double absolute_value = fabs(TINYPY_FLOAT_OBJECT(value)->value);
        tinypy_value_t *return_value_4 = tinypy_float_from_double(vm, absolute_value);
        return return_value_4;
    }
    if (kind == TINYPY_VALUE_COMPLEX) {
        double magnitude = hypot(TINYPY_COMPLEX_OBJECT(value)->real, TINYPY_COMPLEX_OBJECT(value)->imaginary);
        tinypy_value_t *return_value_5 = tinypy_float_from_double(vm, magnitude);
        return return_value_5;
    }
    if (value->type->number_slots != NULL && value->type->number_slots->absolute != NULL) {
        tinypy_value_t *return_value_6 = value->type->number_slots->absolute(value, out_error);
        return return_value_6;
    }
    if (tinypy_internal_object_has_special(value, "__abs__", 7U) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special(value, "__abs__", 7U, out_error);
        tinypy_value_t *empty;
        tinypy_value_t *result;

        if (method == NULL) {
            return NULL;
        }
        empty = tinypy_tuple_from_items(vm, NULL, 0U);
        result = tinypy_call(method, empty, NULL, out_error);
        TINYPY_DECREF(empty);
        TINYPY_DECREF(method);
        return result;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bad operand for abs", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_ord(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const uint8_t *bytes;
    size_t byte_size;
    uint32_t code_point;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        bytes = (const uint8_t *)tinypy_string_view(value, &byte_size);
        if (byte_size != 1U) {
            goto wrong_length;
        }
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, bytes[0]);
        return return_value_1;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        size_t code_points;

        bytes = (const uint8_t *)tinypy_unicode_utf8_view(value, &byte_size, &code_points);
        if (code_points != 1U) {
            goto wrong_length;
        }
        if (bytes[0] < 0x80U) {
            code_point = bytes[0];
        }
        else if (bytes[0] < 0xe0U) {
            code_point = ((uint32_t)(bytes[0] & 0x1fU) << 6U) | (uint32_t)(bytes[1] & 0x3fU);
        }
        else if (bytes[0] < 0xf0U) {
            code_point = ((uint32_t)(bytes[0] & 0x0fU) << 12U) | ((uint32_t)(bytes[1] & 0x3fU) << 6U) | (uint32_t)(bytes[2] & 0x3fU);
        }
        else {
            code_point = ((uint32_t)(bytes[0] & 0x07U) << 18U) | ((uint32_t)(bytes[1] & 0x3fU) << 12U) | ((uint32_t)(bytes[2] & 0x3fU) << 6U) | (uint32_t)(bytes[3] & 0x3fU);
        }
        tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, (int64_t)code_point);
        return return_value_2;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "ord expects a string", out_error);
    return NULL;
wrong_length:
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "ord expects a character", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_format(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const uint8_t *spec = (const uint8_t *)"";
    size_t spec_size = 0U;
    tinypy_bool_t spec_unicode = TINYPY_FALSE;
    tinypy_bool_t result_unicode;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        tinypy_value_t *format_spec = TINYPY_TUPLE_GET(args, 1U);

        if (TINYPY_VALUE_KIND(format_spec) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(format_spec) != TINYPY_VALUE_UNICODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "format() argument 2 must be string or unicode", out_error);
            return NULL;
        }
        spec = TINYPY_TEXT_BYTES(format_spec);
        spec_size = TINYPY_TEXT_BYTE_SIZE(format_spec);
        spec_unicode = TINYPY_VALUE_KIND(format_spec) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_string_format_value(vm, TINYPY_TUPLE_GET(args, 0U), 0, spec, spec_size, spec_unicode, &result_unicode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_call_no_arguments(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *method = tinypy_internal_object_get_special(value, name, name_size, out_error);

    if (method == NULL) {
        return NULL;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, NULL, 0U);
    tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_base_repr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t base = (intptr_t)user_data;
    const char *method_name = base == 16 ? "__hex__" : "__oct__";
    size_t method_size = 7U;
    tinypy_value_t *value;
    tinypy_value_t *integer;
    tinypy_value_t *formatted;
    tinypy_bool_t result_unicode;
    const uint8_t *spec = base == 2 ? (const uint8_t *)"#b" : (base == 8 ? (const uint8_t *)"#o" : (const uint8_t *)"#x");

    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    value = TINYPY_TUPLE_GET(args, 0U);
    if (base != 2 && value->type != &vm->types[TINYPY_VALUE_BOOL] && value->type != &vm->types[TINYPY_VALUE_INTEGER] && value->type != &vm->types[TINYPY_VALUE_LONG] && tinypy_internal_object_has_special(value, method_name, method_size) != 0) {
        tinypy_value_t *special = __tinypy_builtin_call_no_arguments(value, method_name, method_size, out_error);

        if (special == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(special) != TINYPY_VALUE_STRING) {
            TINYPY_DECREF(special);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, base == 16 ? "__hex__ returned a non-string" : "__oct__ returned a non-string", out_error);
            return NULL;
        }
        return special;
    }
    tinypy_value_type_e value_kind = TINYPY_VALUE_KIND(value);
    if (base != 2 && value_kind != TINYPY_VALUE_BOOL && value_kind != TINYPY_VALUE_INTEGER && value_kind != TINYPY_VALUE_LONG) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, base == 16 ? "hex() argument can't be converted to hex" : "oct() argument can't be converted to oct", out_error);
        return NULL;
    }
    integer = tinypy_internal_index_value(value, out_error);
    if (integer == NULL) {
        return NULL;
    }
    formatted = tinypy_internal_string_format_value(vm, integer, 0, spec, 2U, TINYPY_FALSE, &result_unicode, out_error);
    if (formatted == NULL) {
        TINYPY_DECREF(integer);
        return NULL;
    }
    tinypy_bool_t long_suffix = base != 2 && TINYPY_VALUE_KIND(integer) == TINYPY_VALUE_LONG ? TINYPY_TRUE : TINYPY_FALSE;
    TINYPY_DECREF(integer);
    if (base != 8 && long_suffix == 0) {
        return formatted;
    }
    const uint8_t *bytes = TINYPY_TEXT_BYTES(formatted);
    size_t size = TINYPY_TEXT_BYTE_SIZE(formatted);
    size_t prefix = size >= 3U && bytes[0] == (uint8_t)'-' ? 1U : 0U;
    tinypy_bool_t octal_zero = base == 8 && size - prefix == 3U && bytes[prefix] == (uint8_t)'0' && bytes[prefix + 1U] == (uint8_t)'o' && bytes[prefix + 2U] == (uint8_t)'0' ? TINYPY_TRUE : TINYPY_FALSE;
    size_t output_size = size + (long_suffix != 0 ? 1U : 0U) - (base == 8 ? 1U : 0U) - (octal_zero != 0 ? 1U : 0U);
    uint8_t *output;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, output_size, output_size, &output, out_error);
    size_t output_index = 0U;

    if (result == NULL) {
        TINYPY_DECREF(formatted);
        return NULL;
    }

    if (prefix != 0U) {
        output[output_index++] = (uint8_t)'-';
    }
    if (base == 8) {
        output[output_index++] = (uint8_t)'0';
        if (octal_zero == 0) {
            (void)memcpy(output + output_index, bytes + prefix + 2U, size - prefix - 2U);
            output_index += size - prefix - 2U;
        }
    }
    else {
        (void)memcpy(output + output_index, bytes + prefix, size - prefix);
        output_index += size - prefix;
    }
    if (long_suffix != 0) {
        output[output_index++] = (uint8_t)'L';
    }
    TINYPY_DECREF(formatted);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_vars(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 0U, 1U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_value_t *value = vm->current_frame != NULL ? tinypy_internal_frame_locals(vm->current_frame) : vm->builtins;

        TINYPY_INCREF(value);
        return value;
    }
    tinypy_value_t *target = TINYPY_TUPLE_GET(args, 0U);

    if (tinypy_object_has_attr(target, "__dict__", 8U) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "vars() argument must have __dict__ attribute", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_object_get_attr(target, "__dict__", 8U, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_intern(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (value->type != &vm->types[TINYPY_VALUE_STRING]) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "intern() argument must be string", out_error);
        return NULL;
    }
    tinypy_value_t *existing = tinypy_internal_dict_get_optional(vm, vm->interned_strings, value);
    if (existing != NULL) {
        TINYPY_INCREF(existing);
        return existing;
    }
    tinypy_internal_string_set_interned(value, TINYPY_TRUE);
    tinypy_dict_set(vm->interned_strings, value, value);
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_repr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = tinypy_object_repr(item, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_compile_mode(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_compile_mode_e *out_mode, tinypy_error_t **out_error) {
    const char *bytes;
    size_t size;

    if (__tinypy_builtin_text_view(vm, value, &bytes, &size, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (size == 4U && memcmp(bytes, "exec", 4U) == 0) {
        *out_mode = TINYPY_COMPILE_EXEC;
    }
    else if (size == 4U && memcmp(bytes, "eval", 4U) == 0) {
        *out_mode = TINYPY_COMPILE_EVAL;
    }
    else if (size == 6U && memcmp(bytes, "single", 6U) == 0) {
        *out_mode = TINYPY_COMPILE_SINGLE;
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "compile() mode must be 'exec', 'eval' or 'single'", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_source_view(tinypy_vm_t *vm, tinypy_value_t *value, const void **out_source, size_t *out_size, tinypy_bool_t *out_unicode, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        *out_source = tinypy_string_view(value, out_size);
        *out_unicode = 0;
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        size_t code_points;

        *out_source = tinypy_unicode_utf8_view(value, out_size, &code_points);
        *out_unicode = 1;
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "compile() source must be a string", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_compile(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const void *source;
    size_t source_size;
    const char *filename;
    size_t filename_size;
    tinypy_compile_mode_e mode;
    tinypy_compile_options_t options;
    int64_t flags = 0;
    int64_t dont_inherit = 0;
    tinypy_bool_t source_is_unicode;
    const uint32_t supported_flags = (uint32_t)(TINYPY_COMPILE_FLAG_DONT_IMPLY_DEDENT | TINYPY_COMPILE_FLAG_FUTURE_DIVISION | TINYPY_COMPILE_FLAG_FUTURE_ABSOLUTE_IMPORT | TINYPY_COMPILE_FLAG_FUTURE_WITH_STATEMENT | TINYPY_COMPILE_FLAG_FUTURE_PRINT_FUNCTION | TINYPY_COMPILE_FLAG_FUTURE_UNICODE_LITERALS);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 3U, 5U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_builtin_source_view(vm, item_2, &source, &source_size, &source_is_unicode, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item_3 = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_builtin_text_view(vm, item_3, &filename, &filename_size, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item_4 = TINYPY_TUPLE_GET(args, 2U);
    if (__tinypy_builtin_compile_mode(vm, item_4, &mode, out_error) == 0) {
        return NULL;
    }
    tinypy_bool_t condition_9 = TINYPY_TUPLE_SIZE(args) >= 4U;
    if (condition_9 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 3U);
        condition_9 = __tinypy_builtin_integer_as_i64(vm, item, &flags, out_error) == 0;
    }
    if (condition_9) {
        return NULL;
    }
    tinypy_bool_t condition_10 = TINYPY_TUPLE_SIZE(args) >= 5U;
    if (condition_10 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 4U);
        condition_10 = __tinypy_builtin_integer_as_i64(vm, item, &dont_inherit, out_error) == 0;
    }
    if (condition_10) {
        return NULL;
    }
    if (flags < 0 || (uint64_t)flags > UINT32_MAX || ((uint32_t)flags & ~supported_flags) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "compile(): unrecognised flags", out_error);
        return NULL;
    }
    tinypy_compile_options_init(&options, mode);
    if (tinypy_internal_compile_options_inherit_frame(vm, &options) == 0) {
        options.optimize_level = vm->optimize_level;
    }
    options.flags = (uint32_t)flags;
    options.dont_inherit = dont_inherit != 0 ? 1 : 0;
    tinypy_value_t *return_value_1 = tinypy_internal_compiler_compile_source(vm, source, source_size, source_is_unicode, source_is_unicode == 0 ? TINYPY_TRUE : TINYPY_FALSE, filename, filename_size, &options, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_builtin_ensure_builtins(tinypy_vm_t *vm, tinypy_value_t *globals) {
    if (tinypy_dict_contains(globals, vm->builtins_key) == 0) {
        tinypy_dict_set(globals, vm->builtins_key, vm->builtins);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_eval(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *locals;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *source = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *globals = vm->current_frame != NULL ? vm->current_frame->globals : vm->builtins;
    locals = vm->current_frame != NULL ? tinypy_internal_frame_locals(vm->current_frame) : globals;
    tinypy_bool_t condition_11 = TINYPY_TUPLE_SIZE(args) >= 2U;
    if (condition_11 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        condition_11 = TINYPY_VALUE_KIND(item) != TINYPY_VALUE_NONE;
    }
    if (condition_11) {
        globals = TINYPY_TUPLE_GET(args, 1U);
        if (TINYPY_VALUE_KIND(globals) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "eval() globals must be a dictionary", out_error);
            return NULL;
        }
        locals = globals;
    }
    tinypy_bool_t condition_12 = TINYPY_TUPLE_SIZE(args) >= 3U;
    if (condition_12 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 2U);
        condition_12 = TINYPY_VALUE_KIND(item) != TINYPY_VALUE_NONE;
    }
    if (condition_12) {
        locals = TINYPY_TUPLE_GET(args, 2U);
        if (TINYPY_VALUE_KIND(locals) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "eval() locals must be a dictionary", out_error);
            return NULL;
        }
    }
    if (globals != vm->builtins) {
        __tinypy_builtin_ensure_builtins(vm, globals);
    }
    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_CODE) {
        tinypy_value_t *return_value_1 = tinypy_eval_code(source, globals, locals, out_error);
        return return_value_1;
    }
    const void *source_bytes;
    size_t source_size;
    tinypy_bool_t source_is_unicode;
    tinypy_compile_options_t options;
    tinypy_value_t *code;
    tinypy_value_t *result;

    if (__tinypy_builtin_source_view(vm, source, &source_bytes, &source_size, &source_is_unicode, out_error) == 0) {
        return NULL;
    }
    while (source_size != 0U && (*(const uint8_t *)source_bytes == (uint8_t)' ' || *(const uint8_t *)source_bytes == (uint8_t)'\t')) {
        source_bytes = (const uint8_t *)source_bytes + 1U;
        source_size -= 1U;
    }
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EVAL);
    if (tinypy_internal_compile_options_inherit_frame(vm, &options) == 0) {
        options.optimize_level = vm->optimize_level;
    }
    options.dont_inherit = 0;
    code = tinypy_internal_compiler_compile_source(vm, source_bytes, source_size, source_is_unicode, source_is_unicode == 0 ? TINYPY_TRUE : TINYPY_FALSE, "<string>", 8U, &options, out_error);
    if (code == NULL) {
        return NULL;
    }
    result = tinypy_eval_code(code, globals, locals, out_error);
    TINYPY_DECREF(code);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_builtin_register_with_user_data(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, user_data, NULL);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(vm->builtins, key, function);
    TINYPY_DECREF(key);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_builtin_register(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    __tinypy_builtin_register_with_user_data(vm, name, name_size, callback, NULL);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_builtin_functions(tinypy_vm_t *vm) {
    __tinypy_builtin_register(vm, "apply", 5U, __tinypy_builtin_apply);
    __tinypy_builtin_register(vm, "print", 5U, __tinypy_builtin_print);
    __tinypy_builtin_register(vm, "len", 3U, __tinypy_builtin_len);
    __tinypy_builtin_register(vm, "id", 2U, __tinypy_builtin_id);
    __tinypy_builtin_register(vm, "isinstance", 10U, __tinypy_builtin_isinstance);
    __tinypy_builtin_register(vm, "issubclass", 10U, __tinypy_builtin_issubclass);
    __tinypy_builtin_register(vm, "callable", 8U, __tinypy_builtin_callable);
    __tinypy_builtin_register(vm, "getattr", 7U, __tinypy_builtin_getattr);
    __tinypy_builtin_register(vm, "hasattr", 7U, __tinypy_builtin_hasattr);
    __tinypy_builtin_register(vm, "setattr", 7U, __tinypy_builtin_setattr);
    __tinypy_builtin_register(vm, "delattr", 7U, __tinypy_builtin_delattr);
    __tinypy_builtin_register(vm, "iter", 4U, __tinypy_builtin_iter);
    __tinypy_builtin_register(vm, "next", 4U, __tinypy_builtin_next);
    __tinypy_builtin_register(vm, "range", 5U, __tinypy_builtin_range);
    __tinypy_builtin_register(vm, "sorted", 6U, __tinypy_builtin_sorted);
    __tinypy_builtin_register_with_user_data(vm, "all", 3U, __tinypy_builtin_all_any, (void *)(intptr_t)1);
    __tinypy_builtin_register(vm, "any", 3U, __tinypy_builtin_all_any);
    __tinypy_builtin_register(vm, "divmod", 6U, __tinypy_builtin_divmod);
    __tinypy_builtin_register(vm, "filter", 6U, __tinypy_builtin_filter);
    __tinypy_builtin_register(vm, "map", 3U, __tinypy_builtin_map);
    __tinypy_builtin_register(vm, "zip", 3U, __tinypy_builtin_zip);
    __tinypy_builtin_register(vm, "sum", 3U, __tinypy_builtin_sum);
    __tinypy_builtin_register_with_user_data(vm, "max", 3U, __tinypy_builtin_min_max, (void *)(intptr_t)1);
    __tinypy_builtin_register(vm, "min", 3U, __tinypy_builtin_min_max);
    __tinypy_builtin_register(vm, "chr", 3U, __tinypy_builtin_chr_common);
    __tinypy_builtin_register_with_user_data(vm, "unichr", 6U, __tinypy_builtin_chr_common, (void *)(intptr_t)1);
    __tinypy_builtin_register(vm, "cmp", 3U, __tinypy_builtin_cmp);
    __tinypy_builtin_register(vm, "coerce", 6U, __tinypy_builtin_coerce);
    __tinypy_builtin_register(vm, "hash", 4U, __tinypy_builtin_hash);
    __tinypy_builtin_register(vm, "pow", 3U, __tinypy_builtin_pow);
    __tinypy_builtin_register(vm, "round", 5U, __tinypy_builtin_round);
    __tinypy_builtin_register_with_user_data(vm, "globals", 7U, __tinypy_builtin_frame_dict, (void *)(intptr_t)1);
    __tinypy_builtin_register(vm, "locals", 6U, __tinypy_builtin_frame_dict);
    __tinypy_builtin_register(vm, "dir", 3U, __tinypy_builtin_dir);
    __tinypy_builtin_register(vm, "__import__", 10U, __tinypy_builtin_import);
    __tinypy_builtin_register(vm, "reload", 6U, __tinypy_builtin_reload);
    __tinypy_builtin_register(vm, "abs", 3U, __tinypy_builtin_abs);
    __tinypy_builtin_register(vm, "ord", 3U, __tinypy_builtin_ord);
    __tinypy_builtin_register(vm, "format", 6U, __tinypy_builtin_format);
    __tinypy_builtin_register_with_user_data(vm, "bin", 3U, __tinypy_builtin_base_repr, (void *)(intptr_t)2);
    __tinypy_builtin_register_with_user_data(vm, "oct", 3U, __tinypy_builtin_base_repr, (void *)(intptr_t)8);
    __tinypy_builtin_register_with_user_data(vm, "hex", 3U, __tinypy_builtin_base_repr, (void *)(intptr_t)16);
    __tinypy_builtin_register(vm, "vars", 4U, __tinypy_builtin_vars);
    __tinypy_builtin_register(vm, "intern", 6U, __tinypy_builtin_intern);
    __tinypy_builtin_register(vm, "reduce", 6U, tinypy_internal_functools_reduce);
    __tinypy_builtin_register(vm, "repr", 4U, __tinypy_builtin_repr);
    __tinypy_builtin_register(vm, "compile", 7U, __tinypy_builtin_compile);
    __tinypy_builtin_register(vm, "eval", 4U, __tinypy_builtin_eval);
}
