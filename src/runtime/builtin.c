#include "tinypy/native.h"
#include "tinypy/compiler.h"
#include "tinypy/eval.h"

#include "internal.h"
#include "api_internal.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static int __tinypy_builtin_no_keywords(tinypy_vm_t *vm, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    if (kwargs != NULL && tinypy_dict_size(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin function does not accept keyword arguments", out_error);
        return 0;
    }
    return 1;
}

static int __tinypy_builtin_argument_count(tinypy_vm_t *vm, tinypy_value_t *args, size_t minimum, size_t maximum, tinypy_error_t **out_error)
{
    size_t count = tinypy_tuple_size(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin function received the wrong number of arguments", out_error);
        return 0;
    }
    return 1;
}

static int __tinypy_builtin_text_view(tinypy_vm_t *vm, tinypy_value_t *value, const char **out_bytes, size_t *out_size, tinypy_error_t **out_error)
{
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING) {
        *out_bytes = (const char *)tinypy_string_view(value, out_size);
        return 1;
    }
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_UNICODE) {
        size_t code_points;

        *out_bytes = tinypy_unicode_utf8_view(value, out_size, &code_points);
        return 1;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "attribute name must be a string", out_error);
    return 0;
}

static int __tinypy_builtin_integer_as_i64(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error)
{
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return 1;
    }
    if (kind == TINYPY_VALUE_LONG) {
        const uint16_t *digits = TINYPY_LONG_OBJECT(value)->digits;
        size_t count = TINYPY_LONG_DIGIT_COUNT(value);
        uint64_t magnitude = 0U;
        size_t index;
        uint64_t negative_limit = (uint64_t)INT64_MAX + UINT64_C(1);

        if (count > 5U) goto overflow;
        for (index = count; index != 0U; index -= 1U) {
            if (magnitude > (UINT64_MAX >> 15U)) goto overflow;
            magnitude = (magnitude << 15U) | digits[index - 1U];
        }
        if (TINYPY_LONG_SIGN(value) >= 0) {
            if (magnitude > (uint64_t)INT64_MAX) goto overflow;
            *out_value = (int64_t)magnitude;
        } else {
            if (magnitude > negative_limit) goto overflow;
            *out_value = magnitude == negative_limit ? INT64_MIN : -(int64_t)magnitude;
        }
        return 1;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument required", out_error);
    return 0;
overflow:
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "integer argument does not fit in int64", out_error);
    return 0;
}

static tinypy_value_t *__tinypy_builtin_len(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *value;
    size_t size;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    value = tinypy_tuple_get(args, 0U);
    switch (tinypy_internal_value_kind(value)) {
    case TINYPY_VALUE_STRING:
        (void)tinypy_string_view(value, &size);
        break;
    case TINYPY_VALUE_UNICODE: {
        size_t byte_size;
        (void)tinypy_unicode_utf8_view(value, &byte_size, &size);
        break;
    }
    case TINYPY_VALUE_TUPLE: size = tinypy_tuple_size(value); break;
    case TINYPY_VALUE_LIST: size = tinypy_list_size(value); break;
    case TINYPY_VALUE_DICT: size = tinypy_dict_size(value); break;
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET: size = tinypy_set_size(value); break;
    case TINYPY_VALUE_XRANGE: size = TINYPY_XRANGE_OBJECT(value)->length; break;
    default: {
        tinypy_length_slot_t length_slot = value->type->mapping_slots != NULL && value->type->mapping_slots->length != NULL
            ? value->type->mapping_slots->length
            : (value->type->sequence_slots != NULL ? value->type->sequence_slots->length : NULL);
        if (length_slot != NULL) {
            ptrdiff_t length = length_slot(value, out_error);

            if (length < 0) {
                if (out_error == NULL || *out_error == NULL) tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "length slot returned a negative value", out_error);
                return NULL;
            }
            return tinypy_integer_from_i64(vm, (int64_t)length);
        }
        {
        tinypy_error_t *attribute_error = NULL;
        tinypy_value_t *method = tinypy_object_get_attr(value, "__len__", 7U, &attribute_error);
        tinypy_value_t *empty;
        tinypy_value_t *result;

        if (method == NULL) {
            if (attribute_error != NULL) tinypy_error_release(attribute_error);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object has no length", out_error);
            return NULL;
        }
        empty = tinypy_tuple_from_items(vm, NULL, 0U);
        result = tinypy_call(method, empty, NULL, out_error);
        tinypy_release(empty);
        tinypy_release(method);
        if (result == NULL) return NULL;
        if (tinypy_internal_value_kind(result) != TINYPY_VALUE_BOOL && tinypy_internal_value_kind(result) != TINYPY_VALUE_INTEGER && tinypy_internal_value_kind(result) != TINYPY_VALUE_LONG) {
            tinypy_release(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__len__ returned a non-integer", out_error);
            return NULL;
        }
        return result;
        }
    }
    }
    assert(size <= (size_t)INT64_MAX);
    return tinypy_integer_from_i64(vm, (int64_t)size);
}

static tinypy_value_t *__tinypy_builtin_id(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    uintptr_t identity;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    identity = (uintptr_t)tinypy_tuple_get(args, 0U);
    if ((uint64_t)identity <= (uint64_t)INT64_MAX) return tinypy_integer_from_i64(vm, (int64_t)identity);
    {
        uint16_t digits[5];
        size_t count = 0U;
        uint64_t magnitude = (uint64_t)identity;

        while (magnitude != 0U) {
            digits[count] = (uint16_t)(magnitude & UINT64_C(0x7fff));
            count += 1U;
            magnitude >>= 15U;
        }
        return tinypy_long_from_base15_digits(vm, 1, digits, count);
    }
}

static int __tinypy_builtin_metaclass_check(tinypy_value_t *object, tinypy_value_t *classinfo, int subclass, int *out_result, tinypy_error_t **out_error)
{
    const char *name = subclass != 0 ? "__subclasscheck__" : "__instancecheck__";
    size_t name_size = subclass != 0 ? 17U : 17U;
    tinypy_value_t *method;
    tinypy_value_t *args;
    tinypy_value_t *value;
    int32_t truth;

    if (tinypy_internal_value_kind(classinfo) != TINYPY_VALUE_TYPE || tinypy_internal_object_has_special(classinfo, name, name_size) == 0) return 0;
    method = tinypy_object_get_attr(classinfo, name, name_size, out_error);
    if (method == NULL) return -1;
    args = tinypy_tuple_from_items(tinypy_internal_value_vm(object), &object, 1U);
    value = tinypy_call(method, args, NULL, out_error);
    tinypy_release(args);
    tinypy_release(method);
    if (value == NULL) return -1;
    truth = tinypy_truth(value, out_error);
    tinypy_release(value);
    if (truth < 0) return -1;
    *out_result = truth != 0 ? 1 : 0;
    return 1;
}

static int __tinypy_builtin_instance_check(tinypy_value_t *object, tinypy_value_t *classinfo, int subclass, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(object);
    int metaclass_result;
    int metaclass_handled;

    if (tinypy_internal_value_kind(classinfo) == TINYPY_VALUE_TUPLE) {
        size_t index;

        for (index = 0U; index < tinypy_tuple_size(classinfo); index += 1U) {
            int result = __tinypy_builtin_instance_check(object, tinypy_tuple_get(classinfo, index), subclass, out_error);

            if (result != 0) return result;
        }
        return 0;
    }
    metaclass_handled = __tinypy_builtin_metaclass_check(object, classinfo, subclass, &metaclass_result, out_error);
    if (metaclass_handled < 0) return -1;
    if (metaclass_handled != 0) return metaclass_result;
    if (tinypy_internal_value_kind(classinfo) == TINYPY_VALUE_CLASS) {
        if (subclass != 0) {
            if (tinypy_internal_value_kind(object) == TINYPY_VALUE_TYPE) return 0;
            if (tinypy_internal_value_kind(object) != TINYPY_VALUE_CLASS) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "issubclass first argument is not a class", out_error);
                return -1;
            }
            return tinypy_class_is_subclass(object, classinfo);
        }
        if (tinypy_internal_value_kind(object) != TINYPY_VALUE_OLD_INSTANCE) return 0;
        return tinypy_class_is_subclass(tinypy_old_instance_class(object), classinfo);
    }
    if (tinypy_internal_value_kind(classinfo) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "classinfo is not a type or tuple of types", out_error);
        return -1;
    }
    if (subclass != 0) {
        if (tinypy_internal_value_kind(object) == TINYPY_VALUE_CLASS) return tinypy_type_is_subtype(object->type, (tinypy_type_t *)classinfo);
        if (tinypy_internal_value_kind(object) != TINYPY_VALUE_TYPE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "issubclass first argument is not a type", out_error);
            return -1;
        }
        return tinypy_type_is_subtype((tinypy_type_t *)object, (tinypy_type_t *)classinfo);
    }
    return tinypy_type_is_subtype(object->type, (tinypy_type_t *)classinfo);
}

static tinypy_value_t *__tinypy_builtin_isinstance(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    int result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) return NULL;
    result = __tinypy_builtin_instance_check(tinypy_tuple_get(args, 0U), tinypy_tuple_get(args, 1U), 0, out_error);
    return result < 0 ? NULL : tinypy_bool_from_i32(vm, result);
}

static tinypy_value_t *__tinypy_builtin_issubclass(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    int result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) return NULL;
    result = __tinypy_builtin_instance_check(tinypy_tuple_get(args, 0U), tinypy_tuple_get(args, 1U), 1, out_error);
    return result < 0 ? NULL : tinypy_bool_from_i32(vm, result);
}

static tinypy_value_t *__tinypy_builtin_callable(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    return tinypy_bool_from_i32(vm, tinypy_tuple_get(args, 0U)->type->call != NULL || tinypy_internal_object_has_special(tinypy_tuple_get(args, 0U), "__call__", 8U) != 0);
}

static tinypy_value_t *__tinypy_builtin_getattr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *object;
    const char *name;
    size_t name_size;
    tinypy_error_t *attribute_error = NULL;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 3U, out_error) == 0) return NULL;
    object = tinypy_tuple_get(args, 0U);
    if (__tinypy_builtin_text_view(vm, tinypy_tuple_get(args, 1U), &name, &name_size, out_error) == 0) return NULL;
    result = tinypy_object_get_attr(object, name, name_size, &attribute_error);
    if (result != NULL) return result;
    if (tinypy_tuple_size(args) == 3U) {
        if (attribute_error != NULL) tinypy_error_release(attribute_error);
        tinypy_internal_exception_clear_raised(vm);
        result = tinypy_tuple_get(args, 2U);
        tinypy_retain(result);
        return result;
    }
    if (out_error != NULL) *out_error = attribute_error;
    else if (attribute_error != NULL) tinypy_error_release(attribute_error);
    return NULL;
}

static tinypy_value_t *__tinypy_builtin_hasattr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    const char *name;
    size_t name_size;
    tinypy_error_t *attribute_error = NULL;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) return NULL;
    if (__tinypy_builtin_text_view(vm, tinypy_tuple_get(args, 1U), &name, &name_size, out_error) == 0) return NULL;
    result = tinypy_object_get_attr(tinypy_tuple_get(args, 0U), name, name_size, &attribute_error);
    if (result != NULL) tinypy_release(result);
    if (attribute_error != NULL) {
        tinypy_error_release(attribute_error);
        tinypy_internal_exception_clear_raised(vm);
    }
    return tinypy_bool_from_i32(vm, result != NULL);
}

static tinypy_value_t *__tinypy_builtin_setattr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    const char *name;
    size_t name_size;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 3U, 3U, out_error) == 0) return NULL;
    if (__tinypy_builtin_text_view(vm, tinypy_tuple_get(args, 1U), &name, &name_size, out_error) == 0) return NULL;
    if (tinypy_object_set_attr(tinypy_tuple_get(args, 0U), name, name_size, tinypy_tuple_get(args, 2U), out_error) == 0) return NULL;
    return tinypy_none_get(vm);
}

static tinypy_value_t *__tinypy_builtin_delattr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    const char *name;
    size_t name_size;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) return NULL;
    if (__tinypy_builtin_text_view(vm, tinypy_tuple_get(args, 1U), &name, &name_size, out_error) == 0) return NULL;
    if (tinypy_object_delete_attr(tinypy_tuple_get(args, 0U), name, name_size, out_error) == 0) return NULL;
    return tinypy_none_get(vm);
}

static tinypy_value_t *__tinypy_builtin_iter(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    return tinypy_iter(tinypy_tuple_get(args, 0U), out_error);
}

static tinypy_value_t *__tinypy_builtin_next(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 2U, out_error) == 0) return NULL;
    result = tinypy_next(tinypy_tuple_get(args, 0U), &iteration_error);
    if (result != NULL) return result;
    if (iteration_error != NULL) {
        if (out_error != NULL) *out_error = iteration_error;
        else tinypy_error_release(iteration_error);
        return NULL;
    }
    if (tinypy_tuple_size(args) == 2U) {
        result = tinypy_tuple_get(args, 1U);
        tinypy_retain(result);
        return result;
    }
    tinypy_internal_exception_raise_stop_iteration(vm, out_error);
    return NULL;
}

static tinypy_value_t *__tinypy_builtin_range(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    size_t count;
    int64_t start;
    int64_t stop;
    int64_t step;
    int64_t current;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 3U, out_error) == 0) return NULL;
    count = tinypy_tuple_size(args);
    if (count == 1U) {
        start = 0;
        if (__tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 0U), &stop, out_error) == 0) return NULL;
        step = 1;
    } else {
        if (__tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 0U), &start, out_error) == 0 || __tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 1U), &stop, out_error) == 0) return NULL;
        if (count == 3U) {
            if (__tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 2U), &step, out_error) == 0) return NULL;
        } else {
            step = 1;
        }
    }
    if (step == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "range step cannot be zero", out_error);
        return NULL;
    }
    result = tinypy_list_from_items(vm, NULL, 0U);
    current = start;
    while ((step > 0 && current < stop) || (step < 0 && current > stop)) {
        tinypy_value_t *item = tinypy_integer_from_i64(vm, current);

        tinypy_list_append(result, item);
        tinypy_release(item);
        if ((step > 0 && current > INT64_MAX - step) || (step < 0 && current < INT64_MIN - step)) break;
        current += step;
    }
    return result;
}

static tinypy_value_t *__tinypy_builtin_xrange(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    size_t argument_count;
    int64_t start;
    int64_t stop;
    int64_t step;
    uint64_t distance;
    uint64_t step_magnitude;
    uint64_t length;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 3U, out_error) == 0) return NULL;
    argument_count = tinypy_tuple_size(args);
    if (argument_count == 1U) {
        start = 0;
        if (__tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 0U), &stop, out_error) == 0) return NULL;
        step = 1;
    } else {
        if (__tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 0U), &start, out_error) == 0 || __tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 1U), &stop, out_error) == 0) return NULL;
        if (argument_count == 3U) {
            if (__tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 2U), &step, out_error) == 0) return NULL;
        } else step = 1;
    }
    if (step == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "xrange step cannot be zero", out_error);
        return NULL;
    }
    if ((step > 0 && start >= stop) || (step < 0 && start <= stop)) return tinypy_internal_xrange_new(vm, start, step, 0U);
    if (step > 0) {
        distance = (uint64_t)stop - (uint64_t)start;
        step_magnitude = (uint64_t)step;
    } else {
        distance = (uint64_t)start - (uint64_t)stop;
        step_magnitude = (uint64_t)(-(step + INT64_C(1))) + UINT64_C(1);
    }
    length = (distance - UINT64_C(1)) / step_magnitude + UINT64_C(1);
    if (length > (uint64_t)SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "xrange has too many items", out_error);
        return NULL;
    }
    return tinypy_internal_xrange_new(vm, start, step, (size_t)length);
}

static tinypy_value_t *__tinypy_builtin_sorted(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *iterator;
    tinypy_value_t *list;
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *sort_method;
    tinypy_value_t *sort_args;
    tinypy_value_t *sort_result;
    size_t argument_count;

    (void)user_data;
    if (__tinypy_builtin_argument_count(vm, args, 1U, 4U, out_error) == 0) return NULL;
    iterator = tinypy_iter(tinypy_tuple_get(args, 0U), out_error);
    if (iterator == NULL) return NULL;
    list = tinypy_list_from_items(vm, NULL, 0U);
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) break;
        tinypy_list_append(list, item);
        tinypy_release(item);
    }
    tinypy_release(iterator);
    if (iteration_error != NULL) {
        tinypy_release(list);
        if (out_error != NULL) *out_error = iteration_error;
        else tinypy_error_release(iteration_error);
        return NULL;
    }
    sort_method = tinypy_object_get_attr(list, "sort", 4U, out_error);
    if (sort_method == NULL) {
        tinypy_release(list);
        return NULL;
    }
    argument_count = tinypy_tuple_size(args) - 1U;
    sort_args = tinypy_tuple_from_items(vm, argument_count != 0U ? &tinypy_internal_tuple_items(args)[1] : NULL, argument_count);
    sort_result = tinypy_call(sort_method, sort_args, kwargs, out_error);
    tinypy_release(sort_args);
    tinypy_release(sort_method);
    if (sort_result == NULL) {
        tinypy_release(list);
        return NULL;
    }
    tinypy_release(sort_result);
    return list;
}

static tinypy_value_t *__tinypy_builtin_collect(tinypy_vm_t *vm, tinypy_value_t *iterable, tinypy_error_t **out_error)
{
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
    tinypy_value_t *result;
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) return NULL;
    result = tinypy_list_from_items(vm, NULL, 0U);
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) break;
        tinypy_list_append(result, item);
        tinypy_release(item);
    }
    tinypy_release(iterator);
    if (iteration_error != NULL) {
        tinypy_release(result);
        if (out_error != NULL) *out_error = iteration_error;
        else tinypy_error_release(iteration_error);
        return NULL;
    }
    return result;
}

static tinypy_value_t *__tinypy_builtin_all_any(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    int32_t want_all = user_data != NULL ? INT32_C(1) : INT32_C(0);
    tinypy_value_t *iterator;
    tinypy_error_t *iteration_error = NULL;

    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    iterator = tinypy_iter(tinypy_tuple_get(args, 0U), out_error);
    if (iterator == NULL) return NULL;
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        int32_t truth;

        if (item == NULL) break;
        truth = tinypy_truth(item, out_error);
        tinypy_release(item);
        if (truth < 0) {
            tinypy_release(iterator);
            return NULL;
        }
        if ((want_all != 0 && truth == 0) || (want_all == 0 && truth != 0)) {
            tinypy_release(iterator);
            return tinypy_bool_from_i32(vm, want_all == 0);
        }
    }
    tinypy_release(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL) *out_error = iteration_error;
        else tinypy_error_release(iteration_error);
        return NULL;
    }
    return tinypy_bool_from_i32(vm, want_all != 0);
}

static tinypy_value_t *__tinypy_builtin_enumerate(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    int64_t counter = 0;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 2U, out_error) == 0) return NULL;
    if (tinypy_tuple_size(args) == 2U && __tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 1U), &counter, out_error) == 0) return NULL;
    return tinypy_internal_enumerate_new(tinypy_tuple_get(args, 0U), counter, out_error);
}

static tinypy_value_t *__tinypy_builtin_filter(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *predicate;
    tinypy_value_t *input;
    tinypy_value_t *items;
    tinypy_value_t *selected;
    tinypy_value_t *result;
    size_t index;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) return NULL;
    predicate = tinypy_tuple_get(args, 0U);
    input = tinypy_tuple_get(args, 1U);
    items = __tinypy_builtin_collect(vm, input, out_error);
    if (items == NULL) return NULL;
    selected = tinypy_list_from_items(vm, NULL, 0U);
    for (index = 0U; index < tinypy_list_size(items); index += 1U) {
        tinypy_value_t *item = tinypy_list_get(items, index);
        int32_t truth;

        if (tinypy_internal_value_kind(predicate) == TINYPY_VALUE_NONE) truth = tinypy_truth(item, out_error);
        else {
            tinypy_value_t *call_args = tinypy_tuple_from_items(vm, &item, 1U);
            tinypy_value_t *call_result = tinypy_call(predicate, call_args, NULL, out_error);

            tinypy_release(call_args);
            if (call_result == NULL) {
                tinypy_release(selected);
                tinypy_release(items);
                return NULL;
            }
            truth = tinypy_truth(call_result, out_error);
            tinypy_release(call_result);
        }
        if (truth < 0) {
            tinypy_release(selected);
            tinypy_release(items);
            return NULL;
        }
        if (truth != 0) tinypy_list_append(selected, item);
    }
    if (tinypy_internal_value_kind(input) == TINYPY_VALUE_TUPLE) result = tinypy_tuple_from_items(vm, TINYPY_LIST_OBJECT(selected)->items, tinypy_list_size(selected));
    else if (tinypy_internal_value_kind(input) == TINYPY_VALUE_STRING) {
        size_t total = 0U;
        unsigned char *buffer;
        size_t output = 0U;

        for (index = 0U; index < tinypy_list_size(selected); index += 1U) total += tinypy_internal_text_byte_size(tinypy_list_get(selected, index));
        buffer = total != 0U ? (unsigned char *)tinypy_internal_vm_allocate(vm, total, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY) : NULL;
        for (index = 0U; index < tinypy_list_size(selected); index += 1U) {
            tinypy_value_t *item = tinypy_list_get(selected, index);
            size_t size = tinypy_internal_text_byte_size(item);

            (void)memcpy(buffer + output, tinypy_internal_text_bytes(item), size);
            output += size;
        }
        result = tinypy_string_from_bytes(vm, buffer, total);
        if (buffer != NULL) tinypy_internal_vm_deallocate(vm, buffer, total, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    } else {
        result = selected;
        tinypy_retain(result);
    }
    tinypy_release(selected);
    tinypy_release(items);
    return result;
}

static tinypy_value_t *__tinypy_builtin_map(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *callable;
    size_t iterable_count;
    tinypy_value_t **lists;
    size_t maximum = 0U;
    size_t iterable_index;
    size_t index;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, SIZE_MAX, out_error) == 0) return NULL;
    callable = tinypy_tuple_get(args, 0U);
    iterable_count = tinypy_tuple_size(args) - 1U;
    lists = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, iterable_count * sizeof(*lists), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (iterable_index = 0U; iterable_index < iterable_count; iterable_index += 1U) {
        lists[iterable_index] = __tinypy_builtin_collect(vm, tinypy_tuple_get(args, iterable_index + 1U), out_error);
        if (lists[iterable_index] == NULL) {
            while (iterable_index != 0U) tinypy_release(lists[--iterable_index]);
            tinypy_internal_vm_deallocate(vm, lists, iterable_count * sizeof(*lists), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            return NULL;
        }
        if (tinypy_list_size(lists[iterable_index]) > maximum) maximum = tinypy_list_size(lists[iterable_index]);
    }
    result = tinypy_list_from_items(vm, NULL, 0U);
    for (index = 0U; index < maximum; index += 1U) {
        tinypy_value_t **call_items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, iterable_count * sizeof(*call_items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        tinypy_value_t *mapped;

        for (iterable_index = 0U; iterable_index < iterable_count; iterable_index += 1U) call_items[iterable_index] = index < tinypy_list_size(lists[iterable_index]) ? tinypy_list_get(lists[iterable_index], index) : &vm->none_object.base;
        if (tinypy_internal_value_kind(callable) == TINYPY_VALUE_NONE) {
            mapped = iterable_count == 1U ? call_items[0] : NULL;
            if (mapped != NULL) tinypy_retain(mapped);
            else mapped = tinypy_tuple_from_items(vm, call_items, iterable_count);
        } else {
            tinypy_value_t *call_args = tinypy_tuple_from_items(vm, call_items, iterable_count);

            mapped = tinypy_call(callable, call_args, NULL, out_error);
            tinypy_release(call_args);
        }
        tinypy_internal_vm_deallocate(vm, call_items, iterable_count * sizeof(*call_items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        if (mapped == NULL) {
            tinypy_release(result);
            result = NULL;
            break;
        }
        tinypy_list_append(result, mapped);
        tinypy_release(mapped);
    }
    for (iterable_index = 0U; iterable_index < iterable_count; iterable_index += 1U) tinypy_release(lists[iterable_index]);
    tinypy_internal_vm_deallocate(vm, lists, iterable_count * sizeof(*lists), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}

static tinypy_value_t *__tinypy_builtin_zip(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    size_t iterable_count = tinypy_tuple_size(args);
    tinypy_value_t **lists;
    size_t minimum = SIZE_MAX;
    size_t iterable_index;
    size_t index;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0) return NULL;
    if (iterable_count == 0U) return tinypy_list_from_items(vm, NULL, 0U);
    lists = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, iterable_count * sizeof(*lists), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (iterable_index = 0U; iterable_index < iterable_count; iterable_index += 1U) {
        lists[iterable_index] = __tinypy_builtin_collect(vm, tinypy_tuple_get(args, iterable_index), out_error);
        if (lists[iterable_index] == NULL) {
            while (iterable_index != 0U) tinypy_release(lists[--iterable_index]);
            tinypy_internal_vm_deallocate(vm, lists, iterable_count * sizeof(*lists), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            return NULL;
        }
        if (tinypy_list_size(lists[iterable_index]) < minimum) minimum = tinypy_list_size(lists[iterable_index]);
    }
    result = tinypy_list_from_items(vm, NULL, 0U);
    for (index = 0U; index < minimum; index += 1U) {
        tinypy_value_t **tuple_items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, iterable_count * sizeof(*tuple_items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        tinypy_value_t *tuple;

        for (iterable_index = 0U; iterable_index < iterable_count; iterable_index += 1U) tuple_items[iterable_index] = tinypy_list_get(lists[iterable_index], index);
        tuple = tinypy_tuple_from_items(vm, tuple_items, iterable_count);
        tinypy_internal_vm_deallocate(vm, tuple_items, iterable_count * sizeof(*tuple_items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        tinypy_list_append(result, tuple);
        tinypy_release(tuple);
    }
    for (iterable_index = 0U; iterable_index < iterable_count; iterable_index += 1U) tinypy_release(lists[iterable_index]);
    tinypy_internal_vm_deallocate(vm, lists, iterable_count * sizeof(*lists), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}

static tinypy_value_t *__tinypy_builtin_sum(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *items;
    tinypy_value_t *total;
    size_t index;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 2U, out_error) == 0) return NULL;
    items = __tinypy_builtin_collect(vm, tinypy_tuple_get(args, 0U), out_error);
    if (items == NULL) return NULL;
    if (tinypy_tuple_size(args) == 2U) {
        total = tinypy_tuple_get(args, 1U);
        tinypy_retain(total);
    } else total = tinypy_integer_from_i64(vm, INT64_C(0));
    for (index = 0U; index < tinypy_list_size(items); index += 1U) {
        tinypy_value_t *next_total = tinypy_add(total, tinypy_list_get(items, index), out_error);

        tinypy_release(total);
        if (next_total == NULL) {
            tinypy_release(items);
            return NULL;
        }
        total = next_total;
    }
    tinypy_release(items);
    return total;
}

static tinypy_value_t *__tinypy_builtin_min_max(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    int32_t want_max = user_data != NULL ? INT32_C(1) : INT32_C(0);
    tinypy_value_t *items;
    tinypy_value_t *key_function = NULL;
    tinypy_value_t *best;
    tinypy_value_t *best_key;
    size_t index;

    if (tinypy_tuple_size(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "min/max expected at least one argument", out_error);
        return NULL;
    }
    if (kwargs != NULL && tinypy_dict_size(kwargs) != 0U) {
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "key", 3U);

        if (tinypy_dict_size(kwargs) != 1U || tinypy_dict_contains(kwargs, key) == 0) {
            tinypy_release(key);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "min/max received an unexpected keyword", out_error);
            return NULL;
        }
        key_function = tinypy_dict_get(kwargs, key);
        tinypy_release(key);
    }
    if (tinypy_tuple_size(args) == 1U) items = __tinypy_builtin_collect(vm, tinypy_tuple_get(args, 0U), out_error);
    else items = tinypy_list_from_items(vm, tinypy_internal_tuple_items(args), tinypy_tuple_size(args));
    if (items == NULL) return NULL;
    if (tinypy_list_size(items) == 0U) {
        tinypy_release(items);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "min/max argument is an empty sequence", out_error);
        return NULL;
    }
    best = tinypy_list_get(items, 0U);
    tinypy_retain(best);
    if (key_function != NULL) {
        tinypy_value_t *key_args = tinypy_tuple_from_items(vm, &best, 1U);

        best_key = tinypy_call(key_function, key_args, NULL, out_error);
        tinypy_release(key_args);
        if (best_key == NULL) {
            tinypy_release(best);
            tinypy_release(items);
            return NULL;
        }
    } else {
        best_key = best;
        tinypy_retain(best_key);
    }
    for (index = 1U; index < tinypy_list_size(items); index += 1U) {
        tinypy_value_t *candidate = tinypy_list_get(items, index);
        tinypy_value_t *candidate_key;
        int32_t better;

        if (key_function != NULL) {
            tinypy_value_t *key_args = tinypy_tuple_from_items(vm, &candidate, 1U);

            candidate_key = tinypy_call(key_function, key_args, NULL, out_error);
            tinypy_release(key_args);
            if (candidate_key == NULL) {
                tinypy_release(best_key);
                tinypy_release(best);
                tinypy_release(items);
                return NULL;
            }
        } else {
            candidate_key = candidate;
            tinypy_retain(candidate_key);
        }
        better = tinypy_compare_bool(candidate_key, best_key, want_max != 0 ? TINYPY_COMPARE_GREATER : TINYPY_COMPARE_LESS, out_error);
        if (better < 0) {
            tinypy_release(candidate_key);
            tinypy_release(best_key);
            tinypy_release(best);
            tinypy_release(items);
            return NULL;
        }
        if (better != 0) {
            tinypy_release(best_key);
            tinypy_release(best);
            best_key = candidate_key;
            best = candidate;
            tinypy_retain(best);
        } else tinypy_release(candidate_key);
    }
    tinypy_release(best_key);
    tinypy_release(items);
    return best;
}

static tinypy_value_t *__tinypy_builtin_reversed(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    return tinypy_internal_reversed_new(tinypy_tuple_get(args, 0U), out_error);
}

static tinypy_value_t *__tinypy_builtin_chr_common(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    int64_t value;

    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0 || __tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 0U), &value, out_error) == 0) return NULL;
    if (user_data == NULL) {
        unsigned char byte;

        if (value < 0 || value > 255) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "chr() argument not in range(256)", out_error);
            return NULL;
        }
        byte = (unsigned char)value;
        return tinypy_string_from_bytes(vm, &byte, 1U);
    }
    if (value < 0 || value > INT64_C(0x10ffff) || (value >= INT64_C(0xd800) && value <= INT64_C(0xdfff))) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unichr() argument out of range", out_error);
        return NULL;
    }
    {
        char utf8[4];
        size_t size;

        if (value < 0x80) {
            utf8[0] = (char)value;
            size = 1U;
        } else if (value < 0x800) {
            utf8[0] = (char)(0xc0 | (value >> 6));
            utf8[1] = (char)(0x80 | (value & 0x3f));
            size = 2U;
        } else if (value < 0x10000) {
            utf8[0] = (char)(0xe0 | (value >> 12));
            utf8[1] = (char)(0x80 | ((value >> 6) & 0x3f));
            utf8[2] = (char)(0x80 | (value & 0x3f));
            size = 3U;
        } else {
            utf8[0] = (char)(0xf0 | (value >> 18));
            utf8[1] = (char)(0x80 | ((value >> 12) & 0x3f));
            utf8[2] = (char)(0x80 | ((value >> 6) & 0x3f));
            utf8[3] = (char)(0x80 | (value & 0x3f));
            size = 4U;
        }
        return tinypy_unicode_from_utf8(vm, utf8, size);
    }
}

static tinypy_value_t *__tinypy_builtin_cmp(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *left;
    tinypy_value_t *right;
    int32_t less;
    int32_t greater;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) return NULL;
    left = tinypy_tuple_get(args, 0U);
    right = tinypy_tuple_get(args, 1U);
    less = tinypy_compare_bool(left, right, TINYPY_COMPARE_LESS, out_error);
    if (less < 0) return NULL;
    if (less != 0) return tinypy_integer_from_i64(vm, INT64_C(-1));
    greater = tinypy_compare_bool(left, right, TINYPY_COMPARE_GREATER, out_error);
    if (greater < 0) return NULL;
    return tinypy_integer_from_i64(vm, greater != 0 ? INT64_C(1) : INT64_C(0));
}

static tinypy_value_t *__tinypy_builtin_hash(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *value;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    value = tinypy_tuple_get(args, 0U);
    if (tinypy_internal_object_has_special(value, "__hash__", 8U) != 0) {
        tinypy_value_t *method = tinypy_object_get_attr(value, "__hash__", 8U, out_error);
        tinypy_value_t *empty;
        tinypy_value_t *result;
        tinypy_hash_t hash;

        if (method == NULL) return NULL;
        if (tinypy_internal_value_kind(method) == TINYPY_VALUE_NONE) {
            tinypy_release(method);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unhashable type", out_error);
            return NULL;
        }
        empty = tinypy_tuple_from_items(vm, NULL, 0U);
        result = tinypy_call(method, empty, NULL, out_error);
        tinypy_release(empty);
        tinypy_release(method);
        if (result == NULL) return NULL;
        if (tinypy_internal_value_kind(result) != TINYPY_VALUE_BOOL && tinypy_internal_value_kind(result) != TINYPY_VALUE_INTEGER && tinypy_internal_value_kind(result) != TINYPY_VALUE_LONG) {
            tinypy_release(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__hash__ returned a non-integer", out_error);
            return NULL;
        }
        hash = tinypy_hash(result);
        tinypy_release(result);
        return tinypy_integer_from_i64(vm, (int64_t)hash);
    }
    return tinypy_integer_from_i64(vm, (int64_t)tinypy_hash(value));
}

static tinypy_value_t *__tinypy_builtin_pow(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 2U, 2U, out_error) == 0) return NULL;
    return tinypy_power(tinypy_tuple_get(args, 0U), tinypy_tuple_get(args, 1U), out_error);
}

static tinypy_value_t *__tinypy_builtin_round(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *value;
    double number;
    int64_t digits = 0;
    double scale;
    double result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 2U, out_error) == 0) return NULL;
    value = tinypy_tuple_get(args, 0U);
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_FLOAT) number = TINYPY_FLOAT_OBJECT(value)->value;
    else if (tinypy_internal_value_kind(value) == TINYPY_VALUE_BOOL || tinypy_internal_value_kind(value) == TINYPY_VALUE_INTEGER) number = (double)TINYPY_INTEGER_VALUE(value);
    else if (tinypy_internal_value_kind(value) == TINYPY_VALUE_LONG) {
        size_t index = TINYPY_LONG_DIGIT_COUNT(value);

        number = 0.0;
        while (index != 0U) {
            index -= 1U;
            number = number * 32768.0 + TINYPY_LONG_OBJECT(value)->digits[index];
        }
        if (TINYPY_LONG_SIGN(value) < 0) number = -number;
    } else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "round() argument must be a number", out_error);
        return NULL;
    }
    if (tinypy_tuple_size(args) == 2U && __tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 1U), &digits, out_error) == 0) return NULL;
    if (digits > 308) return tinypy_float_from_double(vm, number);
    if (digits < -308) return tinypy_float_from_double(vm, copysign(0.0, number));
    scale = pow(10.0, (double)(digits < 0 ? -digits : digits));
    result = digits < 0 ? round(number / scale) * scale : round(number * scale) / scale;
    return tinypy_float_from_double(vm, result);
}

static tinypy_value_t *__tinypy_builtin_frame_dict(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *value;

    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 0U, 0U, out_error) == 0) return NULL;
    if (vm->current_frame == NULL) value = vm->builtins;
    else value = user_data != NULL ? vm->current_frame->globals : vm->current_frame->locals;
    tinypy_retain(value);
    return value;
}

static void __tinypy_builtin_dir_add_dict(tinypy_value_t *names, tinypy_value_t *dict)
{
    tinypy_dict_object_t *source = TINYPY_DICT_OBJECT(dict);
    size_t index;
    tinypy_vm_t *vm = tinypy_internal_value_vm(names);

    for (index = 0U; index <= source->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &source->table[index];

        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE && (tinypy_internal_value_kind(entry->key) == TINYPY_VALUE_STRING || tinypy_internal_value_kind(entry->key) == TINYPY_VALUE_UNICODE)) tinypy_dict_set(names, entry->key, &vm->none_object.base);
    }
}

static tinypy_value_t *__tinypy_builtin_dir(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *names;
    tinypy_value_t *result;
    tinypy_dict_object_t *name_dict;
    size_t index;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 0U, 1U, out_error) == 0) return NULL;
    names = tinypy_dict_new(vm);
    if (tinypy_tuple_size(args) == 0U) {
        __tinypy_builtin_dir_add_dict(names, vm->current_frame != NULL ? vm->current_frame->locals : vm->builtins);
    } else {
        tinypy_value_t *value = tinypy_tuple_get(args, 0U);
        tinypy_value_type_e kind = tinypy_internal_value_kind(value);
        tinypy_type_t *type = value->type;
        size_t mro_index;

        if (kind == TINYPY_VALUE_INSTANCE && TINYPY_INSTANCE_OBJECT(value)->dict != NULL) __tinypy_builtin_dir_add_dict(names, TINYPY_INSTANCE_OBJECT(value)->dict);
        else if (kind == TINYPY_VALUE_MODULE) __tinypy_builtin_dir_add_dict(names, tinypy_module_dict(value));
        else if (kind == TINYPY_VALUE_TYPE) __tinypy_builtin_dir_add_dict(names, ((tinypy_type_t *)value)->dict);
        else if (kind == TINYPY_VALUE_FUNCTION && TINYPY_FUNCTION_OBJECT(value)->dict != NULL) __tinypy_builtin_dir_add_dict(names, TINYPY_FUNCTION_OBJECT(value)->dict);
        for (mro_index = 0U; mro_index < tinypy_type_mro_size(type); mro_index += 1U) {
            const tinypy_type_t *mro_type = tinypy_type_mro_at(type, mro_index);

            __tinypy_builtin_dir_add_dict(names, (tinypy_value_t *)tinypy_type_dict(mro_type));
        }
    }
    result = tinypy_list_from_items(vm, NULL, 0U);
    name_dict = TINYPY_DICT_OBJECT(names);
    for (index = 0U; index <= name_dict->mask; index += 1U) if (name_dict->table[index].state == TINYPY_DICT_ENTRY_ACTIVE) tinypy_list_append(result, name_dict->table[index].key);
    tinypy_release(names);
    {
        tinypy_value_t *sort_method = tinypy_object_get_attr(result, "sort", 4U, out_error);
        tinypy_value_t *empty = tinypy_tuple_from_items(vm, NULL, 0U);
        tinypy_value_t *sort_result;

        if (sort_method == NULL) {
            tinypy_release(empty);
            tinypy_release(result);
            return NULL;
        }
        sort_result = tinypy_call(sort_method, empty, NULL, out_error);
        tinypy_release(empty);
        tinypy_release(sort_method);
        if (sort_result == NULL) {
            tinypy_release(result);
            return NULL;
        }
        tinypy_release(sort_result);
    }
    return result;
}

static tinypy_value_t *__tinypy_builtin_import(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *name;
    const char *name_bytes;
    size_t name_size;
    tinypy_value_t *globals = vm->current_frame != NULL ? vm->current_frame->globals : NULL;
    tinypy_value_t *fromlist = NULL;
    int64_t level = -1;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 5U, out_error) == 0) return NULL;
    name = tinypy_tuple_get(args, 0U);
    if (__tinypy_builtin_text_view(vm, name, &name_bytes, &name_size, out_error) == 0) return NULL;
    if (tinypy_tuple_size(args) >= 2U && tinypy_internal_value_kind(tinypy_tuple_get(args, 1U)) != TINYPY_VALUE_NONE) {
        globals = tinypy_tuple_get(args, 1U);
        if (tinypy_internal_value_kind(globals) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__import__ globals must be a dictionary", out_error);
            return NULL;
        }
    }
    if (tinypy_tuple_size(args) >= 4U) fromlist = tinypy_tuple_get(args, 3U);
    if (tinypy_tuple_size(args) == 5U && __tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 4U), &level, out_error) == 0) return NULL;
    if (level < INT32_MIN || level > INT32_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__import__ level is out of range", out_error);
        return NULL;
    }
    return tinypy_import_module(vm, name_bytes, name_size, globals, fromlist, (int32_t)level, out_error);
}

static tinypy_value_t *__tinypy_builtin_abs(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *value;
    tinypy_value_type_e kind;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    value = tinypy_tuple_get(args, 0U);
    kind = tinypy_internal_value_kind(value);
    if (kind == TINYPY_VALUE_BOOL) return tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));
    if (kind == TINYPY_VALUE_INTEGER) {
        if (TINYPY_INTEGER_VALUE(value) < 0) return tinypy_negative(value, out_error);
        tinypy_retain(value);
        return value;
    }
    if (kind == TINYPY_VALUE_LONG) {
        if (TINYPY_LONG_SIGN(value) < 0) return tinypy_negative(value, out_error);
        tinypy_retain(value);
        return value;
    }
    if (kind == TINYPY_VALUE_FLOAT) return tinypy_float_from_double(vm, fabs(TINYPY_FLOAT_OBJECT(value)->value));
    if (kind == TINYPY_VALUE_COMPLEX) return tinypy_float_from_double(vm, hypot(TINYPY_COMPLEX_OBJECT(value)->real, TINYPY_COMPLEX_OBJECT(value)->imaginary));
    if (value->type->number_slots != NULL && value->type->number_slots->absolute != NULL) return value->type->number_slots->absolute(value, out_error);
    if (tinypy_internal_object_has_special(value, "__abs__", 7U) != 0) {
        tinypy_value_t *method = tinypy_object_get_attr(value, "__abs__", 7U, out_error);
        tinypy_value_t *empty;
        tinypy_value_t *result;

        if (method == NULL) return NULL;
        empty = tinypy_tuple_from_items(vm, NULL, 0U);
        result = tinypy_call(method, empty, NULL, out_error);
        tinypy_release(empty);
        tinypy_release(method);
        return result;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bad operand for abs", out_error);
    return NULL;
}

static tinypy_value_t *__tinypy_builtin_ord(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *value;
    const unsigned char *bytes;
    size_t byte_size;
    uint32_t code_point;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    value = tinypy_tuple_get(args, 0U);
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING) {
        bytes = (const unsigned char *)tinypy_string_view(value, &byte_size);
        if (byte_size != 1U) goto wrong_length;
        return tinypy_integer_from_i64(vm, bytes[0]);
    }
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_UNICODE) {
        size_t code_points;

        bytes = (const unsigned char *)tinypy_unicode_utf8_view(value, &byte_size, &code_points);
        if (code_points != 1U) goto wrong_length;
        if (bytes[0] < 0x80U) code_point = bytes[0];
        else if (bytes[0] < 0xe0U) code_point = ((uint32_t)(bytes[0] & 0x1fU) << 6U) | (uint32_t)(bytes[1] & 0x3fU);
        else if (bytes[0] < 0xf0U) code_point = ((uint32_t)(bytes[0] & 0x0fU) << 12U) | ((uint32_t)(bytes[1] & 0x3fU) << 6U) | (uint32_t)(bytes[2] & 0x3fU);
        else code_point = ((uint32_t)(bytes[0] & 0x07U) << 18U) | ((uint32_t)(bytes[1] & 0x3fU) << 12U) | ((uint32_t)(bytes[2] & 0x3fU) << 6U) | (uint32_t)(bytes[3] & 0x3fU);
        return tinypy_integer_from_i64(vm, (int64_t)code_point);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "ord expects a string", out_error);
    return NULL;
wrong_length:
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "ord expects a character", out_error);
    return NULL;
}

static tinypy_value_t *__tinypy_builtin_repr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    return tinypy_object_repr(tinypy_tuple_get(args, 0U), out_error);
}

static int32_t __tinypy_builtin_compile_mode(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_compile_mode_e *out_mode, tinypy_error_t **out_error)
{
    const char *bytes;
    size_t size;

    if (__tinypy_builtin_text_view(vm, value, &bytes, &size, out_error) == 0) return INT32_C(0);
    if (size == 4U && memcmp(bytes, "exec", 4U) == 0) *out_mode = TINYPY_COMPILE_EXEC;
    else if (size == 4U && memcmp(bytes, "eval", 4U) == 0) *out_mode = TINYPY_COMPILE_EVAL;
    else if (size == 6U && memcmp(bytes, "single", 6U) == 0) *out_mode = TINYPY_COMPILE_SINGLE;
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "compile() mode must be 'exec', 'eval' or 'single'", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}

static int32_t __tinypy_builtin_source_view(tinypy_vm_t *vm, tinypy_value_t *value, const void **out_source, size_t *out_size, int32_t *out_unicode, tinypy_error_t **out_error)
{
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING) {
        *out_source = tinypy_string_view(value, out_size);
        *out_unicode = 0;
        return INT32_C(1);
    }
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_UNICODE) {
        size_t code_points;

        *out_source = tinypy_unicode_utf8_view(value, out_size, &code_points);
        *out_unicode = 1;
        return INT32_C(1);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "compile() source must be a string", out_error);
    return INT32_C(0);
}

static tinypy_value_t *__tinypy_builtin_compile(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    const void *source;
    size_t source_size;
    const char *filename;
    size_t filename_size;
    tinypy_compile_mode_e mode;
    tinypy_compile_options_t options;
    int64_t flags = 0;
    int64_t dont_inherit = 0;
    int32_t source_is_unicode;
    const uint32_t supported_flags = (uint32_t)(TINYPY_COMPILE_FLAG_DONT_IMPLY_DEDENT | TINYPY_COMPILE_FLAG_FUTURE_DIVISION | TINYPY_COMPILE_FLAG_FUTURE_ABSOLUTE_IMPORT | TINYPY_COMPILE_FLAG_FUTURE_WITH_STATEMENT | TINYPY_COMPILE_FLAG_FUTURE_PRINT_FUNCTION | TINYPY_COMPILE_FLAG_FUTURE_UNICODE_LITERALS);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 3U, 5U, out_error) == 0) return NULL;
    if (__tinypy_builtin_source_view(vm, tinypy_tuple_get(args, 0U), &source, &source_size, &source_is_unicode, out_error) == 0) return NULL;
    if (__tinypy_builtin_text_view(vm, tinypy_tuple_get(args, 1U), &filename, &filename_size, out_error) == 0) return NULL;
    if (__tinypy_builtin_compile_mode(vm, tinypy_tuple_get(args, 2U), &mode, out_error) == 0) return NULL;
    if (tinypy_tuple_size(args) >= 4U && __tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 3U), &flags, out_error) == 0) return NULL;
    if (tinypy_tuple_size(args) >= 5U && __tinypy_builtin_integer_as_i64(vm, tinypy_tuple_get(args, 4U), &dont_inherit, out_error) == 0) return NULL;
    if (flags < 0 || (uint64_t)flags > UINT32_MAX || ((uint32_t)flags & ~supported_flags) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "compile(): unrecognised flags", out_error);
        return NULL;
    }
    tinypy_compile_options_init(&options, mode);
    if (tinypy_internal_compile_options_inherit_frame(vm, &options) == 0) options.optimize_level = vm->optimize_level;
    options.flags = (uint32_t)flags;
    options.dont_inherit = dont_inherit != 0 ? 1 : 0;
    return tinypy_internal_compiler_compile_source(vm, source, source_size, source_is_unicode, filename, filename_size, &options, out_error);
}

static void __tinypy_builtin_ensure_builtins(tinypy_vm_t *vm, tinypy_value_t *globals)
{
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "__builtins__", 12U);

    if (tinypy_dict_contains(globals, key) == 0) tinypy_dict_set(globals, key, vm->builtins);
    tinypy_release(key);
}

static tinypy_value_t *__tinypy_builtin_eval(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *source;
    tinypy_value_t *globals;
    tinypy_value_t *locals;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(vm, args, 1U, 3U, out_error) == 0) return NULL;
    source = tinypy_tuple_get(args, 0U);
    globals = vm->current_frame != NULL ? vm->current_frame->globals : vm->builtins;
    locals = vm->current_frame != NULL ? vm->current_frame->locals : globals;
    if (tinypy_tuple_size(args) >= 2U && tinypy_internal_value_kind(tinypy_tuple_get(args, 1U)) != TINYPY_VALUE_NONE) {
        globals = tinypy_tuple_get(args, 1U);
        if (tinypy_internal_value_kind(globals) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "eval() globals must be a dictionary", out_error);
            return NULL;
        }
        locals = globals;
    }
    if (tinypy_tuple_size(args) >= 3U && tinypy_internal_value_kind(tinypy_tuple_get(args, 2U)) != TINYPY_VALUE_NONE) {
        locals = tinypy_tuple_get(args, 2U);
        if (tinypy_internal_value_kind(locals) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "eval() locals must be a dictionary", out_error);
            return NULL;
        }
    }
    if (globals != vm->builtins) __tinypy_builtin_ensure_builtins(vm, globals);
    if (tinypy_internal_value_kind(source) == TINYPY_VALUE_CODE) return tinypy_eval_code(source, globals, locals, out_error);
    {
        const void *source_bytes;
        size_t source_size;
        int32_t source_is_unicode;
        tinypy_compile_options_t options;
        tinypy_value_t *code;
        tinypy_value_t *result;

        if (__tinypy_builtin_source_view(vm, source, &source_bytes, &source_size, &source_is_unicode, out_error) == 0) return NULL;
        while (source_size != 0U && (*(const unsigned char *)source_bytes == (unsigned char)' ' || *(const unsigned char *)source_bytes == (unsigned char)'\t')) {
            source_bytes = (const unsigned char *)source_bytes + 1U;
            source_size -= 1U;
        }
        tinypy_compile_options_init(&options, TINYPY_COMPILE_EVAL);
        if (tinypy_internal_compile_options_inherit_frame(vm, &options) == 0) options.optimize_level = vm->optimize_level;
        options.dont_inherit = 0;
        code = tinypy_internal_compiler_compile_source(vm, source_bytes, source_size, source_is_unicode, "<string>", 8U, &options, out_error);
        if (code == NULL) return NULL;
        result = tinypy_eval_code(code, globals, locals, out_error);
        tinypy_release(code);
        return result;
    }
}

static void __tinypy_builtin_register(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback)
{
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(vm->builtins, key, function);
    tinypy_release(key);
    tinypy_release(function);
}

void tinypy_internal_initialize_builtin_functions(tinypy_vm_t *vm)
{
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
    __tinypy_builtin_register(vm, "all", 3U, __tinypy_builtin_all_any);
    {
        tinypy_value_t *all_function = tinypy_native_function_new(vm, "all", 3U, __tinypy_builtin_all_any, (void *)(intptr_t)1, NULL);
        tinypy_value_t *all_key = tinypy_string_from_bytes(vm, "all", 3U);

        tinypy_dict_set(vm->builtins, all_key, all_function);
        tinypy_release(all_key);
        tinypy_release(all_function);
    }
    __tinypy_builtin_register(vm, "any", 3U, __tinypy_builtin_all_any);
    __tinypy_builtin_register(vm, "enumerate", 9U, __tinypy_builtin_enumerate);
    __tinypy_builtin_register(vm, "filter", 6U, __tinypy_builtin_filter);
    __tinypy_builtin_register(vm, "map", 3U, __tinypy_builtin_map);
    __tinypy_builtin_register(vm, "zip", 3U, __tinypy_builtin_zip);
    __tinypy_builtin_register(vm, "sum", 3U, __tinypy_builtin_sum);
    {
        tinypy_value_t *maximum = tinypy_native_function_new(vm, "max", 3U, __tinypy_builtin_min_max, (void *)(intptr_t)1, NULL);
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "max", 3U);

        tinypy_dict_set(vm->builtins, key, maximum);
        tinypy_release(key);
        tinypy_release(maximum);
    }
    __tinypy_builtin_register(vm, "min", 3U, __tinypy_builtin_min_max);
    __tinypy_builtin_register(vm, "reversed", 8U, __tinypy_builtin_reversed);
    __tinypy_builtin_register(vm, "chr", 3U, __tinypy_builtin_chr_common);
    {
        tinypy_value_t *unichr_function = tinypy_native_function_new(vm, "unichr", 6U, __tinypy_builtin_chr_common, (void *)(intptr_t)1, NULL);
        tinypy_value_t *unichr_key = tinypy_string_from_bytes(vm, "unichr", 6U);

        tinypy_dict_set(vm->builtins, unichr_key, unichr_function);
        tinypy_release(unichr_key);
        tinypy_release(unichr_function);
    }
    __tinypy_builtin_register(vm, "cmp", 3U, __tinypy_builtin_cmp);
    __tinypy_builtin_register(vm, "hash", 4U, __tinypy_builtin_hash);
    __tinypy_builtin_register(vm, "pow", 3U, __tinypy_builtin_pow);
    __tinypy_builtin_register(vm, "round", 5U, __tinypy_builtin_round);
    {
        tinypy_value_t *globals_function = tinypy_native_function_new(vm, "globals", 7U, __tinypy_builtin_frame_dict, (void *)(intptr_t)1, NULL);
        tinypy_value_t *globals_key = tinypy_string_from_bytes(vm, "globals", 7U);

        tinypy_dict_set(vm->builtins, globals_key, globals_function);
        tinypy_release(globals_key);
        tinypy_release(globals_function);
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
