#include "tinypy/comparison.h"

#include "internal.h"

#include <assert.h>
#include <math.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
static int __tinypy_comparison_is_numeric(tinypy_value_type_e kind) {
    return kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG || kind == TINYPY_VALUE_FLOAT || kind == TINYPY_VALUE_COMPLEX;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_comparison_is_exact_builtin(const tinypy_value_t *value) {
    if ((value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U) {
        return 0;
    }
    switch (TINYPY_VALUE_KIND(value)) {
    case TINYPY_VALUE_NONE:
    case TINYPY_VALUE_BOOL:
    case TINYPY_VALUE_INTEGER:
    case TINYPY_VALUE_LONG:
    case TINYPY_VALUE_FLOAT:
    case TINYPY_VALUE_COMPLEX:
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_UNICODE:
    case TINYPY_VALUE_TUPLE:
    case TINYPY_VALUE_LIST:
    case TINYPY_VALUE_DICT:
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
        return 1;
    default:
        return 0;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_comparison_call_no_args(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *method = tinypy_object_get_attr(value, name, name_size, out_error);
    tinypy_value_t *args;
    tinypy_value_t *result;

    if (method == NULL) {
        return NULL;
    }
    args = tinypy_tuple_from_items(vm, NULL, 0U);
    result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}

static tinypy_value_t *__tinypy_comparison_call_binary(tinypy_value_t *receiver, const char *name, size_t name_size, tinypy_value_t *argument, tinypy_error_t **out_error);

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_truth(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm;
    tinypy_value_type_e kind;

    assert(value != NULL);
    vm = TINYPY_VALUE_VM(value);
    assert(tinypy_internal_vm_valid(vm));
    TINYPY_CLEAR_ERROR(out_error);
    kind = TINYPY_VALUE_KIND(value);
    switch (kind) {
    case TINYPY_VALUE_NONE:
        return INT32_C(0);
    case TINYPY_VALUE_BOOL:
    case TINYPY_VALUE_INTEGER:
        return TINYPY_INTEGER_VALUE(value) != 0 ? INT32_C(1) : INT32_C(0);
    case TINYPY_VALUE_LONG:
        return TINYPY_LONG_SIGN(value) != 0 ? INT32_C(1) : INT32_C(0);
    case TINYPY_VALUE_FLOAT:
        return TINYPY_FLOAT_OBJECT(value)->value != 0.0 ? INT32_C(1) : INT32_C(0);
    case TINYPY_VALUE_COMPLEX:
        return TINYPY_COMPLEX_OBJECT(value)->real != 0.0 || TINYPY_COMPLEX_OBJECT(value)->imaginary != 0.0 ? INT32_C(1) : INT32_C(0);
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_UNICODE:
    case TINYPY_VALUE_TUPLE:
    case TINYPY_VALUE_LIST:
        return TINYPY_SIZED_SIZE(value) != 0 ? INT32_C(1) : INT32_C(0);
    case TINYPY_VALUE_DICT:
        return TINYPY_DICT_OBJECT(value)->used != 0U ? INT32_C(1) : INT32_C(0);
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
        return tinypy_set_size(value) != 0U ? INT32_C(1) : INT32_C(0);
    case TINYPY_VALUE_XRANGE:
        return TINYPY_XRANGE_OBJECT(value)->length != 0U ? INT32_C(1) : INT32_C(0);
    default:
        break;
    } {
        tinypy_length_slot_t length_slot = value->type->mapping_slots != NULL && value->type->mapping_slots->length != NULL
                                               ? value->type->mapping_slots->length
                                               : (value->type->sequence_slots != NULL ? value->type->sequence_slots->length : NULL);

        if (length_slot != NULL) {
            ptrdiff_t length = length_slot(value, out_error);

            if (length < 0) {
                if (out_error == NULL || *out_error == NULL) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "length slot returned a negative value", out_error);
                }
                return INT32_C(-1);
            }
            return length != 0 ? INT32_C(1) : INT32_C(0);
        }
    }
    if (value->type->number_slots != NULL && value->type->number_slots->nonzero != NULL) {
        return value->type->number_slots->nonzero(value, out_error);
    }
    if (tinypy_internal_object_has_special(value, "__nonzero__", 11U) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_no_args(value, "__nonzero__", 11U, out_error);
        int32_t truth;

        if (result == NULL) {
            return INT32_C(-1);
        }
        if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_INTEGER) {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__nonzero__ must return bool or int", out_error);
            return INT32_C(-1);
        }
        truth = TINYPY_INTEGER_VALUE(result) != 0 ? INT32_C(1) : INT32_C(0);
        TINYPY_DECREF(result);
        return truth;
    }
    if (tinypy_internal_object_has_special(value, "__len__", 7U) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_no_args(value, "__len__", 7U, out_error);
        int32_t truth;

        if (result == NULL) {
            return INT32_C(-1);
        }
        if (TINYPY_VALUE_KIND(result) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(result) == TINYPY_VALUE_INTEGER) {
            if (TINYPY_INTEGER_VALUE(result) < 0) {
                TINYPY_DECREF(result);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__ returned a negative value", out_error);
                return INT32_C(-1);
            }
            truth = TINYPY_INTEGER_VALUE(result) != 0 ? INT32_C(1) : INT32_C(0);
        }
        else if (TINYPY_VALUE_KIND(result) == TINYPY_VALUE_LONG) {
            if (TINYPY_LONG_SIGN(result) < 0) {
                TINYPY_DECREF(result);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__ returned a negative value", out_error);
                return INT32_C(-1);
            }
            truth = TINYPY_LONG_SIGN(result) != 0 ? INT32_C(1) : INT32_C(0);
        }
        else {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__len__ returned a non-integer", out_error);
            return INT32_C(-1);
        }
        TINYPY_DECREF(result);
        return truth;
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_comparison_type_name_order(const tinypy_type_t *left, const tinypy_type_t *right) {
    size_t common_size = left->name_size < right->name_size ? left->name_size : right->name_size;
    int comparison = common_size != 0U ? memcmp(left->name, right->name, common_size) : 0;

    if (comparison != 0) {
        return comparison < 0 ? -1 : 1;
    }
    if (left->name_size != right->name_size) {
        return left->name_size < right->name_size ? -1 : 1;
    }
    if (left == right) {
        return 0;
    }
    return (uintptr_t)left < (uintptr_t)right ? -1 : 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_comparison_order(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, int *out_unordered, tinypy_error_t **out_error) {
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    *out_unordered = 0;
    if (__tinypy_comparison_is_numeric(left_kind) != 0 && __tinypy_comparison_is_numeric(right_kind) != 0) {
        if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
            tinypy_vm_t *vm_2 = TINYPY_VALUE_VM(left);
            tinypy_internal_make_vm_error(vm_2, TINYPY_ERROR_TYPE, "complex numbers have no ordering relation", out_error);
            return 0;
        }
        if (tinypy_internal_numeric_order(left, right, out_order) == 0) {
            *out_order = 0;
            *out_unordered = 1;
        }
        return 1;
    }
    if ((left_kind == TINYPY_VALUE_STRING || left_kind == TINYPY_VALUE_UNICODE) && (right_kind == TINYPY_VALUE_STRING || right_kind == TINYPY_VALUE_UNICODE)) {
        *out_order = tinypy_internal_text_order(left, right);
        return 1;
    }
    if ((left_kind == TINYPY_VALUE_BYTEARRAY || right_kind == TINYPY_VALUE_BYTEARRAY || left_kind == TINYPY_VALUE_BUFFER || right_kind == TINYPY_VALUE_BUFFER)) {
        const unsigned char *left_bytes;
        const unsigned char *right_bytes;
        size_t left_size;
        size_t right_size;
        size_t common_size;
        int comparison;

        if (tinypy_internal_bytes_view(left, &left_bytes, &left_size) != 0 && tinypy_internal_bytes_view(right, &right_bytes, &right_size) != 0) {
            common_size = left_size < right_size ? left_size : right_size;
            comparison = common_size != 0U ? memcmp(left_bytes, right_bytes, common_size) : 0;
            *out_order = comparison < 0 ? -1 : (comparison > 0 ? 1 : (left_size < right_size ? -1 : (left_size > right_size ? 1 : 0)));
            return 1;
        }
    }
    if (left_kind == right_kind && (left_kind == TINYPY_VALUE_TUPLE || left_kind == TINYPY_VALUE_LIST)) {
        size_t left_size = left_kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(left) : TINYPY_LIST_SIZE(left);
        size_t right_size = right_kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(right) : TINYPY_LIST_SIZE(right);
        size_t common_size = left_size < right_size ? left_size : right_size;
        tinypy_value_t *const *left_iterator = left_kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_ITERATOR_BEGIN(left) : TINYPY_LIST_ITERATOR_BEGIN(left);
        tinypy_value_t *const *left_iterator_end = common_size != 0U ? left_iterator + common_size : left_iterator;
        tinypy_value_t *const *right_iterator = right_kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_ITERATOR_BEGIN(right) : TINYPY_LIST_ITERATOR_BEGIN(right);
#ifndef NDEBUG
        tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

        assert(vm->equality_depth < 1000U);
        vm->equality_depth += 1U;
#endif
        for (; left_iterator != left_iterator_end; ++left_iterator, ++right_iterator) {
            tinypy_value_t *left_item = *left_iterator;
            tinypy_value_t *right_item = *right_iterator;

            if (tinypy_internal_equal_value(left_item, right_item, 1) == 0) {
                int ordered = __tinypy_comparison_order(left_item, right_item, out_order, out_unordered, out_error);
#ifndef NDEBUG
                vm->equality_depth -= 1U;
#endif
                return ordered;
            }
        }
#ifndef NDEBUG
        vm->equality_depth -= 1U;
#endif
        *out_order = left_size < right_size ? -1 : (left_size > right_size ? 1 : 0);
        return 1;
    }
    if ((left_kind == TINYPY_VALUE_SET || left_kind == TINYPY_VALUE_FROZENSET) && (right_kind == TINYPY_VALUE_SET || right_kind == TINYPY_VALUE_FROZENSET)) {
        int32_t left_subset = tinypy_set_size(left) <= tinypy_set_size(right) && tinypy_internal_set_equal(left, right) != 0;

        if (tinypy_internal_set_equal(left, right) != 0) {
            *out_order = 0;
            return 1;
        }
        if (tinypy_set_size(left) < tinypy_set_size(right)) {
            tinypy_value_t *dict = TINYPY_SET_OBJECT(left)->dict;
            tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(dict);
            tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(dict);

            left_subset = 1;
            for (; iterator != iterator_end; ++iterator) {
                if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE && tinypy_dict_contains(TINYPY_SET_OBJECT(right)->dict, iterator->key) == 0) {
                    left_subset = 0;
                    break;
                }
            }
            if (left_subset != 0) {
                *out_order = -1;
                return 1;
            }
        }
        else if (tinypy_set_size(left) > tinypy_set_size(right)) {
            tinypy_value_t *dict = TINYPY_SET_OBJECT(right)->dict;
            tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(dict);
            tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(dict);
            int32_t right_subset = 1;

            for (; iterator != iterator_end; ++iterator) {
                if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE && tinypy_dict_contains(TINYPY_SET_OBJECT(left)->dict, iterator->key) == 0) {
                    right_subset = 0;
                    break;
                }
            }
            if (right_subset != 0) {
                *out_order = 1;
                return 1;
            }
        }
        *out_order = 0;
        *out_unordered = 1;
        return 1;
    }
    if (left->type != right->type) {
        *out_order = __tinypy_comparison_type_name_order(left->type, right->type);
        return 1;
    }
    if (left == right) {
        *out_order = 0;
        return 1;
    }
    *out_order = (uintptr_t)left < (uintptr_t)right ? -1 : 1;
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_comparison_text_contains(tinypy_value_t *container, tinypy_value_t *item, tinypy_error_t **out_error) {
    const unsigned char *container_bytes;
    const unsigned char *item_bytes;
    size_t container_size;
    size_t item_size;
    size_t index;

    if ((TINYPY_VALUE_KIND(item) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(item) != TINYPY_VALUE_UNICODE)) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "string containment requires a string operand", out_error);
        return -1;
    }
    container_bytes = TINYPY_TEXT_BYTES(container);
    item_bytes = TINYPY_TEXT_BYTES(item);
    container_size = TINYPY_TEXT_BYTE_SIZE(container);
    item_size = TINYPY_TEXT_BYTE_SIZE(item);
    if (item_size == 0U) {
        return 1;
    }
    if (item_size > container_size) {
        return 0;
    }
    for (index = 0U; index <= container_size - item_size; ++index) {
        if (memcmp(container_bytes + index, item_bytes, item_size) == 0) {
            return 1;
        }
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_contains(tinypy_value_t *container, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_vm_t *vm;
    tinypy_value_type_e kind;

    assert(container != NULL && item != NULL);
    vm = TINYPY_VALUE_VM(container);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, item));
    TINYPY_CLEAR_ERROR(out_error);
    if (container->type->sequence_slots != NULL && container->type->sequence_slots->contains != NULL) {
        return container->type->sequence_slots->contains(container, item, out_error);
    }
    kind = TINYPY_VALUE_KIND(container);
    if (kind == TINYPY_VALUE_DICT) {
        return tinypy_dict_contains(container, item) != 0 ? 1 : 0;
    }
    if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        return tinypy_set_contains(container, item, out_error);
    }
    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        return __tinypy_comparison_text_contains(container, item, out_error);
    }
    if (kind == TINYPY_VALUE_BYTEARRAY) {
        int64_t integer;
        size_t size = tinypy_bytearray_size(container);
        size_t index;

        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(item);
        int condition = __tinypy_comparison_is_numeric(kind_2) == 0;
        if (condition == 0) {
            condition = (TINYPY_VALUE_KIND(item) == TINYPY_VALUE_FLOAT || TINYPY_VALUE_KIND(item) == TINYPY_VALUE_COMPLEX);
        }
        if (condition) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bytearray containment requires an integer", out_error);
            return -1;
        }
        if (TINYPY_VALUE_KIND(item) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(item) == TINYPY_VALUE_INTEGER) {
            integer = TINYPY_INTEGER_VALUE(item);
        }
        else {
            if (TINYPY_LONG_DIGIT_COUNT(item) > 1U || TINYPY_LONG_SIGN(item) < 0) {
                return 0;
            }
            integer = TINYPY_LONG_DIGIT_COUNT(item) == 0U ? 0 : (int64_t)TINYPY_LONG_OBJECT(item)->digits[0];
        }
        if (integer < 0 || integer > 255) {
            return 0;
        }
        for (index = 0U; index < size; ++index) {
            if (TINYPY_BYTEARRAY_OBJECT(container)->bytes[index] == (unsigned char)integer) {
                return 1;
            }
        }
        return 0;
    }
    if (kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_LIST) {
        tinypy_value_t *const *iterator = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_ITERATOR_BEGIN(container) : TINYPY_LIST_ITERATOR_BEGIN(container);
        tinypy_value_t *const *iterator_end = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_ITERATOR_END(container) : TINYPY_LIST_ITERATOR_END(container);

        for (; iterator != iterator_end; ++iterator) {
            tinypy_value_t *candidate = *iterator;

            if (tinypy_internal_equal_value(candidate, item, 1) != 0) {
                return 1;
            }
        }
        return 0;
    }
    if (tinypy_internal_object_has_special(container, "__contains__", 12U) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_binary(container, "__contains__", 12U, item, out_error);
        int32_t truth;

        if (result == NULL) {
            return -1;
        }
        truth = tinypy_truth(result, out_error);
        TINYPY_DECREF(result);
        return truth;
    } {
        tinypy_error_t *iteration_error = NULL;
        tinypy_value_t *iterator = tinypy_iter(container, &iteration_error);

        if (iterator == NULL) {
            if (out_error != NULL) {
                *out_error = iteration_error;
            }
            else if (iteration_error != NULL) {
                tinypy_error_release(iteration_error);
            }
            return -1;
        }
        for (;;) {
            tinypy_value_t *candidate = tinypy_next(iterator, &iteration_error);

            if (candidate == NULL) {
                break;
            }
            if (tinypy_internal_equal_value(candidate, item, 1) != 0) {
                TINYPY_DECREF(candidate);
                TINYPY_DECREF(iterator);
                return 1;
            }
            TINYPY_DECREF(candidate);
        }
        TINYPY_DECREF(iterator);
        if (iteration_error != NULL) {
            if (out_error != NULL) {
                *out_error = iteration_error;
            }
            else {
                tinypy_error_release(iteration_error);
            }
            return -1;
        }
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_comparison_call_binary(tinypy_value_t *receiver, const char *name, size_t name_size, tinypy_value_t *argument, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(receiver);
    tinypy_value_t *method = tinypy_object_get_attr(receiver, name, name_size, out_error);
    tinypy_value_t *args;
    tinypy_value_t *result;

    if (method == NULL) {
        return NULL;
    }
    args = tinypy_tuple_from_items(vm, &argument, 1U);
    result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_comparison_special_result(tinypy_vm_t *vm, tinypy_value_t *result, int32_t *out_value, int32_t *out_not_implemented, tinypy_error_t **out_error) {
    int32_t truth;

    if (result == &vm->not_implemented_object.base) {
        TINYPY_DECREF(result);
        *out_not_implemented = INT32_C(1);
        return INT32_C(1);
    }
    truth = tinypy_truth(result, out_error);
    TINYPY_DECREF(result);
    if (truth < 0) {
        return INT32_C(0);
    }
    *out_value = truth;
    *out_not_implemented = INT32_C(0);
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_comparison_try_special(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, int32_t *out_handled, int32_t *out_value, tinypy_error_t **out_error) {
    static const char *left_names[] = {"__lt__", "__le__", "__eq__", "__ne__", "__gt__", "__ge__"};
    static const char *right_names[] = {"__gt__", "__ge__", "__eq__", "__ne__", "__lt__", "__le__"};
    static const size_t name_sizes[] = {6U, 6U, 6U, 6U, 6U, 6U};
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    size_t index = (size_t)operation;

    *out_handled = INT32_C(0);
    if (operation > TINYPY_COMPARE_GREATER_EQUAL) {
        return INT32_C(1);
    }
    if (tinypy_internal_object_has_special(left, left_names[index], name_sizes[index]) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_binary(left, left_names[index], name_sizes[index], right, out_error);
        int32_t not_implemented;

        if (result == NULL) {
            *out_handled = INT32_C(1);
            return INT32_C(0);
        }
        if (__tinypy_comparison_special_result(vm, result, out_value, &not_implemented, out_error) == 0) {
            *out_handled = INT32_C(1);
            return INT32_C(0);
        }
        if (not_implemented == 0) {
            *out_handled = INT32_C(1);
            return INT32_C(1);
        }
    }
    if ((right->type != left->type || (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_OLD_INSTANCE && tinypy_old_instance_class(left) != tinypy_old_instance_class(right))) && tinypy_internal_object_has_special(right, right_names[index], name_sizes[index]) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_binary(right, right_names[index], name_sizes[index], left, out_error);
        int32_t not_implemented;

        if (result == NULL) {
            *out_handled = INT32_C(1);
            return INT32_C(0);
        }
        if (__tinypy_comparison_special_result(vm, result, out_value, &not_implemented, out_error) == 0) {
            *out_handled = INT32_C(1);
            return INT32_C(0);
        }
        if (not_implemented == 0) {
            *out_handled = INT32_C(1);
            return INT32_C(1);
        }
    }
    if (tinypy_internal_object_has_special(left, "__cmp__", 7U) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_binary(left, "__cmp__", 7U, right, out_error);
        int64_t order;

        if (result == NULL) {
            *out_handled = INT32_C(1);
            return INT32_C(0);
        }
        if (TINYPY_VALUE_KIND(result) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(result) == TINYPY_VALUE_INTEGER) {
            order = TINYPY_INTEGER_VALUE(result);
        }
        else {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__cmp__ returned a non-integer", out_error);
            *out_handled = INT32_C(1);
            return INT32_C(0);
        }
        TINYPY_DECREF(result);
        if (operation == TINYPY_COMPARE_LESS) {
            *out_value = order < 0;
        }
        else if (operation == TINYPY_COMPARE_LESS_EQUAL) {
            *out_value = order <= 0;
        }
        else if (operation == TINYPY_COMPARE_EQUAL) {
            *out_value = order == 0;
        }
        else if (operation == TINYPY_COMPARE_NOT_EQUAL) {
            *out_value = order != 0;
        }
        else if (operation == TINYPY_COMPARE_GREATER) {
            *out_value = order > 0;
        }
        else {
            *out_value = order >= 0;
        }
        *out_handled = INT32_C(1);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_compare_bool(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_error_t **out_error) {
    int32_t order;
    int unordered;

    assert(left != NULL && right != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(left)));
    assert(tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(left), right));
    assert(operation >= TINYPY_COMPARE_LESS && operation <= TINYPY_COMPARE_EXCEPTION_MATCH);
    TINYPY_CLEAR_ERROR(out_error);
    if (operation == TINYPY_COMPARE_IS) {
        return left == right;
    }
    if (operation == TINYPY_COMPARE_IS_NOT) {
        return left != right;
    }
    if (operation == TINYPY_COMPARE_IN || operation == TINYPY_COMPARE_NOT_IN) {
        int32_t contained = tinypy_contains(right, left, out_error);

        return contained < 0 || operation == TINYPY_COMPARE_IN ? contained : contained == 0;
    }
    if (operation == TINYPY_COMPARE_EXCEPTION_MATCH) {
        return tinypy_exception_matches(left, right, out_error);
    }
    if (operation <= TINYPY_COMPARE_GREATER_EQUAL && (__tinypy_comparison_is_exact_builtin(left) == 0 || __tinypy_comparison_is_exact_builtin(right) == 0)) {
        int32_t handled;
        int32_t special_value;

        if (__tinypy_comparison_try_special(left, right, operation, &handled, &special_value, out_error) == 0) {
            return -1;
        }
        if (handled != 0) {
            return special_value;
        }
    }
    if (operation == TINYPY_COMPARE_EQUAL) {
        return tinypy_equal(left, right);
    }
    if (operation == TINYPY_COMPARE_NOT_EQUAL) {
        return tinypy_equal(left, right) == 0;
    }
    if (__tinypy_comparison_order(left, right, &order, &unordered, out_error) == 0) {
        return -1;
    }
    if (unordered != 0) {
        return 0;
    }
    if (operation == TINYPY_COMPARE_LESS) {
        return order < 0;
    }
    if (operation == TINYPY_COMPARE_LESS_EQUAL) {
        return order <= 0;
    }
    if (operation == TINYPY_COMPARE_GREATER) {
        return order > 0;
    }
    return order >= 0;
}
