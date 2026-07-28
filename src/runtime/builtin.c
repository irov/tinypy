#include "tinypy/native.h"
#include "tinypy/compiler.h"
#include "tinypy/eval.h"

#include "internal.h"
#include "api_internal.h"

#include <math.h>
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
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_LONG) {
        const uint16_t *digits = TINYPY_LONG_OBJECT(value)->digits;
        size_t count = TINYPY_LONG_DIGIT_COUNT(value);
        uint64_t magnitude = 0U;
        size_t index;
        uint64_t negative_limit = (uint64_t)INT64_MAX + UINT64_C(1);

        if (count > 5U) {
            goto overflow;
        }
        for (index = count; index != 0U; index -= 1U) {
            if (magnitude > (UINT64_MAX >> 15U)) {
                goto overflow;
            }
            magnitude = (magnitude << 15U) | digits[index - 1U];
        }
        if (TINYPY_LONG_SIGN(value) >= 0) {
            if (magnitude > (uint64_t)INT64_MAX) {
                goto overflow;
            }
            *out_value = (int64_t)magnitude;
        }
        else {
            if (magnitude > negative_limit) {
                goto overflow;
            }
            *out_value = magnitude == negative_limit ? INT64_MIN : -(int64_t)magnitude;
        }
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument required", out_error);
    return TINYPY_FALSE;
overflow:
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "integer argument does not fit in int64", out_error);
    return TINYPY_FALSE;
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
        tinypy_value_t *method = tinypy_object_get_attr(value, "__len__", 7U, &attribute_error);
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
        return result;
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
    tinypy_value_t *method = tinypy_object_get_attr(classinfo, name, name_size, out_error);
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
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = tinypy_iter(item, out_error);
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
        tinypy_bool_t condition_2 = __tinypy_builtin_integer_as_i64(vm, item_2, &start, out_error) == 0;
        if (condition_2 == 0) {
            tinypy_value_t *item_3 = TINYPY_TUPLE_GET(args, 1U);
            condition_2 = __tinypy_builtin_integer_as_i64(vm, item_3, &stop, out_error) == 0;
        }
        if (condition_2) {
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
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    current = start;
    while ((step > 0 && current < stop) || (step < 0 && current > stop)) {
        tinypy_value_t *item = tinypy_integer_from_i64(vm, current);

        tinypy_list_append(result, item);
        TINYPY_DECREF(item);
        if ((step > 0 && current > INT64_MAX - step) || (step < 0 && current < INT64_MIN - step)) {
            break;
        }
        current += step;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_xrange(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t argument_count;
    int64_t start;
    int64_t stop;
    int64_t step;
    uint64_t distance;
    uint64_t step_magnitude;
    uint64_t length;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 3U, out_error) == 0) {
        return NULL;
    }
    argument_count = TINYPY_TUPLE_SIZE(args);
    if (argument_count == 1U) {
        start = 0;
        tinypy_value_t *item_3 = TINYPY_TUPLE_GET(args, 0U);
        if (__tinypy_builtin_integer_as_i64(vm, item_3, &stop, out_error) == 0) {
            return NULL;
        }
        step = 1;
    }
    else {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        tinypy_bool_t condition_3 = __tinypy_builtin_integer_as_i64(vm, item, &start, out_error) == 0;
        if (condition_3 == 0) {
            tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
            condition_3 = __tinypy_builtin_integer_as_i64(vm, item_2, &stop, out_error) == 0;
        }
        if (condition_3) {
            return NULL;
        }
        if (argument_count == 3U) {
            tinypy_value_t *item_3 = TINYPY_TUPLE_GET(args, 2U);
            if (__tinypy_builtin_integer_as_i64(vm, item_3, &step, out_error) == 0) {
                return NULL;
            }
        }
        else {
            step = 1;
        }
    }
    if (step == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "xrange step cannot be zero", out_error);
        return NULL;
    }
    if ((step > 0 && start >= stop) || (step < 0 && start <= stop)) {
        tinypy_value_t *return_value_1 = tinypy_internal_xrange_new(vm, start, step, 0U);
        return return_value_1;
    }
    if (step > 0) {
        distance = (uint64_t)stop - (uint64_t)start;
        step_magnitude = (uint64_t)step;
    }
    else {
        distance = (uint64_t)start - (uint64_t)stop;
        step_magnitude = (uint64_t)(-(step + INT64_C(1))) + UINT64_C(1);
    }
    length = (distance - UINT64_C(1)) / step_magnitude + UINT64_C(1);
    if (length > (uint64_t)SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "xrange has too many items", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_2 = tinypy_internal_xrange_new(vm, start, step, (size_t)length);
    return return_value_2;
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
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        tinypy_list_append(list, item);
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
static tinypy_value_t *__tinypy_builtin_collect(tinypy_vm_t *vm, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        tinypy_list_append(result, item);
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        TINYPY_DECREF(result);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    return result;
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
static tinypy_value_t *__tinypy_builtin_enumerate(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t counter = 0;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_bool_t condition_4 = TINYPY_TUPLE_SIZE(args) == 2U;
    if (condition_4 != 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
        condition_4 = __tinypy_builtin_integer_as_i64(vm, item_2, &counter, out_error) == 0;
    }
    if (condition_4) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = tinypy_internal_enumerate_new(item, counter, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_filter(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *items;
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
    items = __tinypy_builtin_collect(vm, input, out_error);
    if (items == NULL) {
        return NULL;
    }
    selected = tinypy_list_from_items(vm, NULL, 0U);
    iterator = TINYPY_LIST_ITERATOR_BEGIN(items);
    iterator_end = TINYPY_LIST_ITERATOR_END(items);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        int32_t truth;

        if (TINYPY_VALUE_KIND(predicate) == TINYPY_VALUE_NONE) {
            truth = tinypy_truth(item, out_error);
        }
        else {
            tinypy_value_t *call_args = tinypy_tuple_from_items(vm, &item, 1U);
            tinypy_value_t *call_result = tinypy_call(predicate, call_args, NULL, out_error);

            TINYPY_DECREF(call_args);
            if (call_result == NULL) {
                TINYPY_DECREF(selected);
                TINYPY_DECREF(items);
                return NULL;
            }
            truth = tinypy_truth(call_result, out_error);
            TINYPY_DECREF(call_result);
        }
        if (truth < 0) {
            TINYPY_DECREF(selected);
            TINYPY_DECREF(items);
            return NULL;
        }
        if (truth != 0) {
            tinypy_list_append(selected, item);
        }
    }
    if (TINYPY_VALUE_KIND(input) == TINYPY_VALUE_TUPLE) {
        size_t list_size = TINYPY_LIST_SIZE(selected);
        result = tinypy_tuple_from_items(vm, TINYPY_LIST_OBJECT(selected)->items, list_size);
    }
    else if (TINYPY_VALUE_KIND(input) == TINYPY_VALUE_STRING) {
        size_t total = 0U;
        uint8_t *buffer;
        size_t output = 0U;

        iterator = TINYPY_LIST_ITERATOR_BEGIN(selected);
        iterator_end = TINYPY_LIST_ITERATOR_END(selected);
        for (; iterator != iterator_end; ++iterator) {
            total += TINYPY_TEXT_BYTE_SIZE(*iterator);
        }
        buffer = total != 0U ? (uint8_t *)tinypy_internal_vm_allocate(vm, total, TINYPY_ALLOC_TAG_TEMPORARY) : NULL;
        iterator = TINYPY_LIST_ITERATOR_BEGIN(selected);
        iterator_end = TINYPY_LIST_ITERATOR_END(selected);
        for (; iterator != iterator_end; ++iterator) {
            tinypy_value_t *item = *iterator;
            size_t size = TINYPY_TEXT_BYTE_SIZE(item);

            (void)memcpy(buffer + output, TINYPY_TEXT_BYTES(item), size);
            output += size;
        }
        result = tinypy_string_from_bytes(vm, buffer, total);
        if (buffer != NULL) {
            tinypy_internal_vm_deallocate(vm, buffer, total, TINYPY_ALLOC_TAG_TEMPORARY);
        }
    }
    else {
        result = selected;
        TINYPY_INCREF(result);
    }
    TINYPY_DECREF(selected);
    TINYPY_DECREF(items);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_map(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t iterable_count;
    size_t maximum = 0U;
    size_t iterable_index;
    size_t index;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *callable = TINYPY_TUPLE_GET(args, 0U);
    iterable_count = TINYPY_TUPLE_SIZE(args) - 1U;
    tinypy_value_t **lists = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, iterable_count * sizeof(*lists), TINYPY_ALLOC_TAG_TEMPORARY);
    for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, iterable_index + 1U);
        lists[iterable_index] = __tinypy_builtin_collect(vm, item, out_error);
        if (lists[iterable_index] == NULL) {
            while (iterable_index != 0U) {
                TINYPY_DECREF(lists[--iterable_index]);
            }
            tinypy_internal_vm_deallocate(vm, lists, iterable_count * sizeof(*lists), TINYPY_ALLOC_TAG_TEMPORARY);
            return NULL;
        }
        if (TINYPY_LIST_SIZE(lists[iterable_index]) > maximum) {
            maximum = TINYPY_LIST_SIZE(lists[iterable_index]);
        }
    }
    result = tinypy_list_from_items(vm, NULL, 0U);
    for (index = 0U; index < maximum; ++index) {
        tinypy_value_t **call_items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, iterable_count * sizeof(*call_items), TINYPY_ALLOC_TAG_TEMPORARY);
        tinypy_value_t *mapped;

        for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
            call_items[iterable_index] = index < TINYPY_LIST_SIZE(lists[iterable_index]) ? TINYPY_LIST_GET(lists[iterable_index], index) : &vm->none_object.base;
        }
        if (TINYPY_VALUE_KIND(callable) == TINYPY_VALUE_NONE) {
            mapped = iterable_count == 1U ? call_items[0] : NULL;
            if (mapped != NULL) {
                TINYPY_INCREF(mapped);
            }
            else {
                mapped = tinypy_tuple_from_items(vm, call_items, iterable_count);
            }
        }
        else {
            tinypy_value_t *call_args = tinypy_tuple_from_items(vm, call_items, iterable_count);

            mapped = tinypy_call(callable, call_args, NULL, out_error);
            TINYPY_DECREF(call_args);
        }
        tinypy_internal_vm_deallocate(vm, call_items, iterable_count * sizeof(*call_items), TINYPY_ALLOC_TAG_TEMPORARY);
        if (mapped == NULL) {
            TINYPY_DECREF(result);
            result = NULL;
            break;
        }
        tinypy_list_append(result, mapped);
        TINYPY_DECREF(mapped);
    }
    for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
        TINYPY_DECREF(lists[iterable_index]);
    }
    tinypy_internal_vm_deallocate(vm, lists, iterable_count * sizeof(*lists), TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_zip(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t iterable_count = TINYPY_TUPLE_SIZE(args);
    size_t minimum = SIZE_MAX;
    size_t iterable_index;
    size_t index;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0) {
        return NULL;
    }
    if (iterable_count == 0U) {
        tinypy_value_t *return_value_1 = tinypy_list_from_items(vm, NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t **lists = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, iterable_count * sizeof(*lists), TINYPY_ALLOC_TAG_TEMPORARY);
    for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, iterable_index);
        lists[iterable_index] = __tinypy_builtin_collect(vm, item, out_error);
        if (lists[iterable_index] == NULL) {
            while (iterable_index != 0U) {
                TINYPY_DECREF(lists[--iterable_index]);
            }
            tinypy_internal_vm_deallocate(vm, lists, iterable_count * sizeof(*lists), TINYPY_ALLOC_TAG_TEMPORARY);
            return NULL;
        }
        if (TINYPY_LIST_SIZE(lists[iterable_index]) < minimum) {
            minimum = TINYPY_LIST_SIZE(lists[iterable_index]);
        }
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    for (index = 0U; index < minimum; ++index) {
        tinypy_value_t **tuple_items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, iterable_count * sizeof(*tuple_items), TINYPY_ALLOC_TAG_TEMPORARY);
        tinypy_value_t *tuple;

        for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
            tuple_items[iterable_index] = TINYPY_LIST_GET(lists[iterable_index], index);
        }
        tuple = tinypy_tuple_from_items(vm, tuple_items, iterable_count);
        tinypy_internal_vm_deallocate(vm, tuple_items, iterable_count * sizeof(*tuple_items), TINYPY_ALLOC_TAG_TEMPORARY);
        tinypy_list_append(result, tuple);
        TINYPY_DECREF(tuple);
    }
    for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
        TINYPY_DECREF(lists[iterable_index]);
    }
    tinypy_internal_vm_deallocate(vm, lists, iterable_count * sizeof(*lists), TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_sum(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *total;
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *items = __tinypy_builtin_collect(vm, item, out_error);
    if (items == NULL) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        total = TINYPY_TUPLE_GET(args, 1U);
        TINYPY_INCREF(total);
    }
    else {
        total = tinypy_integer_from_i64(vm, INT64_C(0));
    }
    iterator = TINYPY_LIST_ITERATOR_BEGIN(items);
    iterator_end = TINYPY_LIST_ITERATOR_END(items);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *next_total = tinypy_add(total, *iterator, out_error);

        TINYPY_DECREF(total);
        if (next_total == NULL) {
            TINYPY_DECREF(items);
            return NULL;
        }
        total = next_total;
    }
    TINYPY_DECREF(items);
    return total;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_min_max(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t want_max = user_data != NULL ? INT32_C(1) : INT32_C(0);
    tinypy_value_t *items;
    tinypy_value_t *key_function = NULL;
    tinypy_value_t *best_key;
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "min/max expected at least one argument", out_error);
        return NULL;
    }
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "key", 3U);

        key_function = TINYPY_DICT_SIZE(kwargs) == 1U ? tinypy_dict_get_optional(kwargs, key) : NULL;
        if (key_function == NULL) {
            TINYPY_DECREF(key);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "min/max received an unexpected keyword", out_error);
            return NULL;
        }
        TINYPY_DECREF(key);
    }
    if (TINYPY_TUPLE_SIZE(args) == 1U) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        items = __tinypy_builtin_collect(vm, item, out_error);
    }
    else {
        tinypy_value_t *const *tuple_items = tinypy_internal_tuple_items(args);
        size_t tuple_size = TINYPY_TUPLE_SIZE(args);
        items = tinypy_list_from_items(vm, tuple_items, tuple_size);
    }
    if (items == NULL) {
        return NULL;
    }
    if (TINYPY_LIST_SIZE(items) == 0U) {
        TINYPY_DECREF(items);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "min/max argument is an empty sequence", out_error);
        return NULL;
    }
    tinypy_value_t *best = TINYPY_LIST_GET(items, 0U);
    TINYPY_INCREF(best);
    if (key_function != NULL) {
        tinypy_value_t *key_args = tinypy_tuple_from_items(vm, &best, 1U);

        best_key = tinypy_call(key_function, key_args, NULL, out_error);
        TINYPY_DECREF(key_args);
        if (best_key == NULL) {
            TINYPY_DECREF(best);
            TINYPY_DECREF(items);
            return NULL;
        }
    }
    else {
        best_key = best;
        TINYPY_INCREF(best_key);
    }
    iterator = TINYPY_LIST_ITERATOR_BEGIN(items) + 1;
    iterator_end = TINYPY_LIST_ITERATOR_END(items);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *candidate = *iterator;
        tinypy_value_t *candidate_key;
        int32_t better;

        if (key_function != NULL) {
            tinypy_value_t *key_args = tinypy_tuple_from_items(vm, &candidate, 1U);

            candidate_key = tinypy_call(key_function, key_args, NULL, out_error);
            TINYPY_DECREF(key_args);
            if (candidate_key == NULL) {
                TINYPY_DECREF(best_key);
                TINYPY_DECREF(best);
                TINYPY_DECREF(items);
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
            TINYPY_DECREF(best_key);
            TINYPY_DECREF(best);
            TINYPY_DECREF(items);
            return NULL;
        }
        if (better != 0) {
            TINYPY_DECREF(best_key);
            TINYPY_DECREF(best);
            best_key = candidate_key;
            best = candidate;
            TINYPY_INCREF(best);
        }
        else {
            TINYPY_DECREF(candidate_key);
        }
    }
    TINYPY_DECREF(best_key);
    TINYPY_DECREF(items);
    return best;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_reversed(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = tinypy_internal_reversed_new(item, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_chr_common(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t value;

    tinypy_bool_t condition_5 = __tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0;
    if (condition_5 == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        condition_5 = __tinypy_builtin_integer_as_i64(vm, item, &value, out_error) == 0;
    }
    if (condition_5) {
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
    if (value < 0 || value > INT64_C(0x10ffff) || (value >= INT64_C(0xd800) && value <= INT64_C(0xdfff))) {
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
static tinypy_value_t *__tinypy_builtin_cmp(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t less;
    int32_t greater;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    less = tinypy_compare_bool(left, right, TINYPY_COMPARE_LESS, out_error);
    if (less < 0) {
        return NULL;
    }
    if (less != 0) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, INT64_C(-1));
        return return_value_1;
    }
    greater = tinypy_compare_bool(left, right, TINYPY_COMPARE_GREATER, out_error);
    if (greater < 0) {
        return NULL;
    }
    tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, greater != 0 ? INT64_C(1) : INT64_C(0));
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_hash(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_internal_object_has_special(value, "__hash__", 8U) != 0) {
        tinypy_value_t *method = tinypy_object_get_attr(value, "__hash__", 8U, out_error);
        tinypy_value_t *empty;
        tinypy_value_t *result;
        tinypy_hash_t hash;

        if (method == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(method) == TINYPY_VALUE_NONE) {
            TINYPY_DECREF(method);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unhashable type", out_error);
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
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__hash__ returned a non-integer", out_error);
            return NULL;
        }
        hash = tinypy_hash(result);
        TINYPY_DECREF(result);
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)hash);
        return return_value_1;
    }
    tinypy_hash_t hash_2 = tinypy_internal_hash_value(value, out_error);
    if (tinypy_vm_has_error(vm) != 0) {
        return NULL;
    }
    tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, (int64_t)hash_2);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_pow(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *return_value_1 = tinypy_power(item, item_2, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_round(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    double number;
    int64_t digits = 0;
    double scale;
    double result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FLOAT) {
        number = TINYPY_FLOAT_OBJECT(value)->value;
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_INTEGER) {
        number = (double)TINYPY_INTEGER_VALUE(value);
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG) {
        size_t index = TINYPY_LONG_DIGIT_COUNT(value);

        number = 0.0;
        while (index != 0U) {
            index -= 1U;
            number = number * 32768.0 + TINYPY_LONG_OBJECT(value)->digits[index];
        }
        if (TINYPY_LONG_SIGN(value) < 0) {
            number = -number;
        }
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "round() argument must be a number", out_error);
        return NULL;
    }
    tinypy_bool_t condition_6 = TINYPY_TUPLE_SIZE(args) == 2U;
    if (condition_6 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        condition_6 = __tinypy_builtin_integer_as_i64(vm, item, &digits, out_error) == 0;
    }
    if (condition_6) {
        return NULL;
    }
    if (digits > 308) {
        tinypy_value_t *return_value_1 = tinypy_float_from_double(vm, number);
        return return_value_1;
    }
    if (digits < -308) {
        double signed_value = copysign(0.0, number);
        tinypy_value_t *return_value_2 = tinypy_float_from_double(vm, signed_value);
        return return_value_2;
    }
    scale = pow(10.0, (double)(digits < 0 ? -digits : digits));
    result = digits < 0 ? round(number / scale) * scale : round(number * scale) / scale;
    tinypy_value_t *return_value_3 = tinypy_float_from_double(vm, result);
    return return_value_3;
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
        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE && (TINYPY_VALUE_KIND(iterator->key) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(iterator->key) == TINYPY_VALUE_UNICODE)) {
            tinypy_dict_set(names, iterator->key, &vm->none_object.base);
        }
    }
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
    tinypy_value_t *names = tinypy_dict_new(vm);
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        __tinypy_builtin_dir_add_dict(names, vm->current_frame != NULL ? tinypy_internal_frame_locals(vm->current_frame) : vm->builtins);
    }
    else {
        tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
        tinypy_type_t *type = value->type;
        size_t mro_index;

        if (kind == TINYPY_VALUE_INSTANCE && TINYPY_INSTANCE_OBJECT(value)->dict != NULL) {
            __tinypy_builtin_dir_add_dict(names, TINYPY_INSTANCE_OBJECT(value)->dict);
        }
        else if (kind == TINYPY_VALUE_MODULE) {
            tinypy_value_t *module_dict = tinypy_module_dict(value);
            __tinypy_builtin_dir_add_dict(names, module_dict);
        }
        else if (kind == TINYPY_VALUE_TYPE) {
            __tinypy_builtin_dir_add_dict(names, ((tinypy_type_t *)value)->dict);
        }
        else if (kind == TINYPY_VALUE_FUNCTION && TINYPY_FUNCTION_OBJECT(value)->dict != NULL) {
            __tinypy_builtin_dir_add_dict(names, TINYPY_FUNCTION_OBJECT(value)->dict);
        }
        for (mro_index = 0U; mro_index < tinypy_type_mro_size(type); ++mro_index) {
            const tinypy_type_t *mro_type = tinypy_type_mro_at(type, mro_index);

            const tinypy_value_t *type_dict = tinypy_type_dict(mro_type);
            __tinypy_builtin_dir_add_dict(names, (tinypy_value_t *)type_dict);
        }
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    iterator = TINYPY_DICT_ITERATOR_BEGIN(names);
    iterator_end = TINYPY_DICT_ITERATOR_END(names);
    for (; iterator != iterator_end; ++iterator) {
        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE) {
            tinypy_list_append(result, iterator->key);
        }
    }
    TINYPY_DECREF(names); {
        tinypy_value_t *sort_method = tinypy_object_get_attr(result, "sort", 4U, out_error);
        tinypy_value_t *empty = tinypy_tuple_from_items(vm, NULL, 0U);
        tinypy_value_t *sort_result;

        if (sort_method == NULL) {
            TINYPY_DECREF(empty);
            TINYPY_DECREF(result);
            return NULL;
        }
        sort_result = tinypy_call(sort_method, empty, NULL, out_error);
        TINYPY_DECREF(empty);
        TINYPY_DECREF(sort_method);
        if (sort_result == NULL) {
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(sort_result);
    }
    return result;
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
static tinypy_value_t *__tinypy_builtin_abs(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_type_e kind;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
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
        tinypy_value_t *method = tinypy_object_get_attr(value, "__abs__", 7U, out_error);
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
    tinypy_value_t *return_value_1 = tinypy_internal_compiler_compile_source(vm, source, source_size, source_is_unicode, filename, filename_size, &options, out_error);
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
    code = tinypy_internal_compiler_compile_source(vm, source_bytes, source_size, source_is_unicode, "<string>", 8U, &options, out_error);
    if (code == NULL) {
        return NULL;
    }
    result = tinypy_eval_code(code, globals, locals, out_error);
    TINYPY_DECREF(code);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_builtin_register(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(vm->builtins, key, function);
    TINYPY_DECREF(key);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_builtin_functions(tinypy_vm_t *vm) {
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
    __tinypy_builtin_register(vm, "all", 3U, __tinypy_builtin_all_any); {
        tinypy_value_t *all_function = tinypy_native_function_new(vm, "all", 3U, __tinypy_builtin_all_any, (void *)(intptr_t)1, NULL);
        tinypy_value_t *all_key = tinypy_string_from_bytes(vm, "all", 3U);

        tinypy_dict_set(vm->builtins, all_key, all_function);
        TINYPY_DECREF(all_key);
        TINYPY_DECREF(all_function);
    }
    __tinypy_builtin_register(vm, "any", 3U, __tinypy_builtin_all_any);
    __tinypy_builtin_register(vm, "enumerate", 9U, __tinypy_builtin_enumerate);
    __tinypy_builtin_register(vm, "filter", 6U, __tinypy_builtin_filter);
    __tinypy_builtin_register(vm, "map", 3U, __tinypy_builtin_map);
    __tinypy_builtin_register(vm, "zip", 3U, __tinypy_builtin_zip);
    __tinypy_builtin_register(vm, "sum", 3U, __tinypy_builtin_sum); {
        tinypy_value_t *maximum = tinypy_native_function_new(vm, "max", 3U, __tinypy_builtin_min_max, (void *)(intptr_t)1, NULL);
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "max", 3U);

        tinypy_dict_set(vm->builtins, key, maximum);
        TINYPY_DECREF(key);
        TINYPY_DECREF(maximum);
    }
    __tinypy_builtin_register(vm, "min", 3U, __tinypy_builtin_min_max);
    __tinypy_builtin_register(vm, "reversed", 8U, __tinypy_builtin_reversed);
    __tinypy_builtin_register(vm, "chr", 3U, __tinypy_builtin_chr_common); {
        tinypy_value_t *unichr_function = tinypy_native_function_new(vm, "unichr", 6U, __tinypy_builtin_chr_common, (void *)(intptr_t)1, NULL);
        tinypy_value_t *unichr_key = tinypy_string_from_bytes(vm, "unichr", 6U);

        tinypy_dict_set(vm->builtins, unichr_key, unichr_function);
        TINYPY_DECREF(unichr_key);
        TINYPY_DECREF(unichr_function);
    }
    __tinypy_builtin_register(vm, "cmp", 3U, __tinypy_builtin_cmp);
    __tinypy_builtin_register(vm, "hash", 4U, __tinypy_builtin_hash);
    __tinypy_builtin_register(vm, "pow", 3U, __tinypy_builtin_pow);
    __tinypy_builtin_register(vm, "round", 5U, __tinypy_builtin_round); {
        tinypy_value_t *globals_function = tinypy_native_function_new(vm, "globals", 7U, __tinypy_builtin_frame_dict, (void *)(intptr_t)1, NULL);
        tinypy_value_t *globals_key = tinypy_string_from_bytes(vm, "globals", 7U);

        tinypy_dict_set(vm->builtins, globals_key, globals_function);
        TINYPY_DECREF(globals_key);
        TINYPY_DECREF(globals_function);
    }
    __tinypy_builtin_register(vm, "locals", 6U, __tinypy_builtin_frame_dict);
    __tinypy_builtin_register(vm, "xrange", 6U, __tinypy_builtin_xrange);
    __tinypy_builtin_register(vm, "dir", 3U, __tinypy_builtin_dir);
    __tinypy_builtin_register(vm, "__import__", 10U, __tinypy_builtin_import);
    __tinypy_builtin_register(vm, "abs", 3U, __tinypy_builtin_abs);
    __tinypy_builtin_register(vm, "ord", 3U, __tinypy_builtin_ord);
    __tinypy_builtin_register(vm, "repr", 4U, __tinypy_builtin_repr);
    __tinypy_builtin_register(vm, "compile", 7U, __tinypy_builtin_compile);
    __tinypy_builtin_register(vm, "eval", 4U, __tinypy_builtin_eval);
}
