#include "tinypy/comparison.h"

#include "internal.h"

#include <math.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_is_numeric(tinypy_value_type_e kind) {
    return kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG || kind == TINYPY_VALUE_FLOAT || kind == TINYPY_VALUE_COMPLEX;
}
//////////////////////////////////////////////////////////////////////////
/* PyInt_AsLong accepts the numeric __int__ protocol, including floats. */
tinypy_bool_t tinypy_internal_number_as_i64(tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return TINYPY_TRUE;
    }
    tinypy_bool_t handled;
    tinypy_value_t *converted = tinypy_internal_call_conversion(value, vm->internal_special_int_key, &handled, out_error);

    if (converted == NULL) {
        if (handled == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "an integer is required", out_error);
        }
        return TINYPY_FALSE;
    }
    kind = TINYPY_VALUE_KIND(converted);
    if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG) {
        TINYPY_DECREF(converted);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__int__ returned a non-integer", out_error);
        return TINYPY_FALSE;
    }
    tinypy_bool_t result = tinypy_internal_index_as_i64(converted, out_value, TINYPY_FALSE, out_error);

    TINYPY_DECREF(converted);
    if (result == 0 && kind == TINYPY_VALUE_LONG) {
        if (out_error != NULL && *out_error != NULL) {
            tinypy_error_release(*out_error);
            *out_error = NULL;
        }
        tinypy_vm_clear_error(vm);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "Python int too large to convert to C long", out_error);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_comparison_is_exact_builtin(const tinypy_value_t *value) {
    if ((value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U) {
        return TINYPY_FALSE;
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
    case TINYPY_VALUE_SLICE:
        return TINYPY_TRUE;
    default:
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_comparison_call_no_args(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *method = tinypy_internal_object_get_special_key(value, name, out_error);

    if (method == NULL) {
        return NULL;
    }
    tinypy_value_t *args = TINYPY_RET_EMPTY_TUPLE(vm);
    tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}

static tinypy_value_t *__tinypy_comparison_call_binary(tinypy_value_t *receiver, tinypy_value_t *name, tinypy_value_t *argument, tinypy_bool_t missing_is_not_implemented, tinypy_error_t **out_error);
static tinypy_bool_t __tinypy_comparison_equal_checked(tinypy_value_t *left, tinypy_value_t *right, tinypy_bool_t identity_implies_equal, tinypy_bool_t *out_equal, tinypy_error_t **out_error);

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_truth(tinypy_value_t *value, tinypy_bool_t dispatch_special, tinypy_error_t **out_error) {
    int32_t function_result;
    tinypy_value_type_e kind;
    tinypy_bool_t exact_builtin;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    TINYPY_CLEAR_ERROR(out_error);
    kind = TINYPY_VALUE_KIND(value);
    /* Every classic instance shares the builtin instance type, so its
       __nonzero__ and __len__ live in the class and must still be consulted. */
    exact_builtin = kind != TINYPY_VALUE_OLD_INSTANCE && (size_t)kind < TINYPY_BUILTIN_TYPE_COUNT && value->type == &vm->types[kind] ? TINYPY_TRUE : TINYPY_FALSE;
    if (exact_builtin != 0) {
        goto builtin_truth;
    }
    if (dispatch_special != 0 && tinypy_internal_object_has_special_override_key(value, vm->internal_special_nonzero_key) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_no_args(value, vm->internal_special_nonzero_key, out_error);
        int32_t truth;

        if (result == NULL) {
            return INT32_C(-1);
        }
        if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_INTEGER) {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__nonzero__ must return bool or int", out_error);
            return INT32_C(-1);
        }
        if (kind == TINYPY_VALUE_OLD_INSTANCE && TINYPY_INTEGER_VALUE(result) < 0) {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__nonzero__ should return >= 0", out_error);
            return INT32_C(-1);
        }
        truth = TINYPY_INTEGER_VALUE(result) != 0 ? INT32_C(1) : INT32_C(0);
        TINYPY_DECREF(result);
        return truth;
    }
    /* An inherited numeric nonzero slot wins over an added length method. */
    if (__tinypy_comparison_is_numeric(kind) != 0) {
        goto builtin_truth;
    }
    if (dispatch_special != 0 && tinypy_internal_object_has_special_override_key(value, vm->internal_special_length_key) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_no_args(value, vm->internal_special_length_key, out_error);
        int32_t truth;

        if (result == NULL) {
            return INT32_C(-1);
        }
        if (kind == TINYPY_VALUE_OLD_INSTANCE && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_INTEGER) {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__nonzero__ should return an int", out_error);
            return INT32_C(-1);
        }
        int64_t length;
        if (tinypy_internal_number_as_ssize(result, &length, out_error) == 0) {
            TINYPY_DECREF(result);
            return INT32_C(-1);
        }
        TINYPY_DECREF(result);
        if (length < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__ returned a negative value", out_error);
            return INT32_C(-1);
        }
        truth = length != 0 ? INT32_C(1) : INT32_C(0);
        return truth;
    }
builtin_truth:
    switch (kind) {
    case TINYPY_VALUE_NONE:
        return INT32_C(0);
    case TINYPY_VALUE_BOOL:
    case TINYPY_VALUE_INTEGER:
        function_result = TINYPY_INTEGER_VALUE(value) != 0 ? INT32_C(1) : INT32_C(0);
        return function_result;
    case TINYPY_VALUE_LONG:
        function_result = TINYPY_LONG_SIGN(value) != 0 ? INT32_C(1) : INT32_C(0);
        return function_result;
    case TINYPY_VALUE_FLOAT:
        function_result = TINYPY_FLOAT_OBJECT(value)->value != 0.0 ? INT32_C(1) : INT32_C(0);
        return function_result;
    case TINYPY_VALUE_COMPLEX:
        function_result = TINYPY_COMPLEX_OBJECT(value)->real != 0.0 || TINYPY_COMPLEX_OBJECT(value)->imaginary != 0.0 ? INT32_C(1) : INT32_C(0);
        return function_result;
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_UNICODE:
    case TINYPY_VALUE_TUPLE:
    case TINYPY_VALUE_LIST:
        function_result = TINYPY_SIZED_SIZE(value) != 0 ? INT32_C(1) : INT32_C(0);
        return function_result;
    case TINYPY_VALUE_DICT:
        function_result = TINYPY_DICT_OBJECT(value)->used != 0U ? INT32_C(1) : INT32_C(0);
        return function_result;
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
        function_result = tinypy_set_size(value) != 0U ? INT32_C(1) : INT32_C(0);
        return function_result;
    case TINYPY_VALUE_XRANGE:
        function_result = TINYPY_XRANGE_OBJECT(value)->length != 0U ? INT32_C(1) : INT32_C(0);
        return function_result;
    default:
        break;
    }
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
    if (value->type->number_slots != NULL && value->type->number_slots->nonzero != NULL) {
        int32_t return_value_1 = value->type->number_slots->nonzero(value, out_error);
        return return_value_1;
    }
    if (exact_builtin != 0) {
        return INT32_C(1);
    }
    if (dispatch_special != 0 && tinypy_internal_object_has_special_key(value, vm->internal_special_nonzero_key) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_no_args(value, vm->internal_special_nonzero_key, out_error);
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
    if (dispatch_special != 0 && tinypy_internal_object_has_special_key(value, vm->internal_special_length_key) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_no_args(value, vm->internal_special_length_key, out_error);
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
int32_t tinypy_internal_truth_builtin(tinypy_value_t *value, tinypy_error_t **out_error) {
    int32_t result = __tinypy_truth(value, TINYPY_FALSE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_truth(tinypy_value_t *value, tinypy_error_t **out_error) {
    int32_t result = __tinypy_truth(value, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_comparison_type_name_order(const tinypy_type_t *left, const tinypy_type_t *right) {
    size_t common_size = left->name_size < right->name_size ? left->name_size : right->name_size;
    int32_t comparison = common_size != 0U ? memcmp(left->name, right->name, common_size) : 0;

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
static tinypy_bool_t __tinypy_comparison_number_check(const tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (__tinypy_comparison_is_numeric(kind) != 0 || kind == TINYPY_VALUE_OLD_INSTANCE) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t result = tinypy_internal_object_has_special_key((tinypy_value_t *)value, vm->internal_special_int_key) != 0
        || tinypy_internal_object_has_special_key((tinypy_value_t *)value, vm->internal_special_float_key) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return result;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_comparison_fallback_order(tinypy_value_t *left, tinypy_value_t *right) {
    tinypy_bool_t left_numeric;
    tinypy_bool_t right_numeric;

    if (left->type == right->type) {
        return left == right ? 0 : ((uintptr_t)left < (uintptr_t)right ? -1 : 1);
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_NONE || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_NONE) {
        int32_t result = TINYPY_VALUE_KIND(left) == TINYPY_VALUE_NONE ? -1 : 1;

        return result;
    }
    left_numeric = __tinypy_comparison_number_check(left);
    right_numeric = __tinypy_comparison_number_check(right);
    if (left_numeric != right_numeric) {
        return left_numeric != 0 ? -1 : 1;
    }
    if (left_numeric != 0) {
        return (uintptr_t)left->type < (uintptr_t)right->type ? -1 : 1;
    }
    int32_t result = __tinypy_comparison_type_name_order(left->type, right->type);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_sequence_equal_checked(tinypy_value_t *left, tinypy_value_t *right, tinypy_bool_t *out_equal, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    size_t left_size;
    size_t right_size;
    size_t index = 0U;

    if (kind != TINYPY_VALUE_KIND(right)) {
        *out_equal = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    left_size = TINYPY_SIZED_SIZE(left);
    right_size = TINYPY_SIZED_SIZE(right);
    if (left_size != right_size) {
        *out_equal = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    while (index < TINYPY_SIZED_SIZE(left) && index < TINYPY_SIZED_SIZE(right)) {
        tinypy_value_t *left_item = kind == TINYPY_VALUE_TUPLE ? tinypy_internal_tuple_items(left)[index] : TINYPY_LIST_GET(left, index);
        tinypy_value_t *right_item = kind == TINYPY_VALUE_TUPLE ? tinypy_internal_tuple_items(right)[index] : TINYPY_LIST_GET(right, index);
        int32_t equal;

        if (left_item == right_item) {
            index += 1U;
            continue;
        }
        TINYPY_INCREF(left_item);
        TINYPY_INCREF(right_item);
        equal = tinypy_compare_bool(left_item, right_item, TINYPY_COMPARE_EQUAL, out_error);
        TINYPY_DECREF(right_item);
        TINYPY_DECREF(left_item);
        if (equal < 0) {
            return TINYPY_FALSE;
        }
        if (equal == 0) {
            *out_equal = TINYPY_FALSE;
            return TINYPY_TRUE;
        }
        index += 1U;
    }
    *out_equal = TINYPY_SIZED_SIZE(left) == TINYPY_SIZED_SIZE(right) ? TINYPY_TRUE : TINYPY_FALSE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_slice_equal_checked(tinypy_value_t *left, tinypy_value_t *right, tinypy_bool_t *out_equal, tinypy_error_t **out_error) {
    tinypy_slice_object_t *left_slice = TINYPY_SLICE_OBJECT(left);
    tinypy_slice_object_t *right_slice = TINYPY_SLICE_OBJECT(right);
    tinypy_value_t *left_items[3] = {left_slice->start, left_slice->stop, left_slice->step};
    tinypy_value_t *right_items[3] = {right_slice->start, right_slice->stop, right_slice->step};
    size_t index;

    for (index = 0U; index < 3U; ++index) {
        if (left_items[index] != right_items[index]) {
            int32_t equal = tinypy_compare_bool(left_items[index], right_items[index], TINYPY_COMPARE_EQUAL, out_error);

            if (equal < 0) {
                return TINYPY_FALSE;
            }
            if (equal == 0) {
                *out_equal = TINYPY_FALSE;
                return TINYPY_TRUE;
            }
        }
    }
    *out_equal = TINYPY_TRUE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_equal_checked(tinypy_value_t *left, tinypy_value_t *right, tinypy_bool_t identity_implies_equal, tinypy_bool_t *out_equal, tinypy_error_t **out_error) {
    tinypy_value_type_e left_kind;
    tinypy_value_type_e right_kind;

    if (left == right && identity_implies_equal != 0) {
        *out_equal = TINYPY_TRUE;
        return TINYPY_TRUE;
    }
    left_kind = TINYPY_VALUE_KIND(left);
    right_kind = TINYPY_VALUE_KIND(right);
    if ((left_kind == TINYPY_VALUE_BUFFER && right_kind == TINYPY_VALUE_STRING) || (left_kind == TINYPY_VALUE_STRING && right_kind == TINYPY_VALUE_BUFFER)) {
        *out_equal = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    if ((left_kind == TINYPY_VALUE_TUPLE || left_kind == TINYPY_VALUE_LIST) && (right_kind == TINYPY_VALUE_TUPLE || right_kind == TINYPY_VALUE_LIST)) {
        tinypy_bool_t return_value_1 = __tinypy_comparison_sequence_equal_checked(left, right, out_equal, out_error);
        return return_value_1;
    }
    if (left_kind == TINYPY_VALUE_DICT && right_kind == TINYPY_VALUE_DICT) {
        tinypy_bool_t return_value_2 = tinypy_internal_dict_equal_checked(left, right, out_equal, out_error);
        return return_value_2;
    }
    if ((left_kind == TINYPY_VALUE_SET || left_kind == TINYPY_VALUE_FROZENSET) && (right_kind == TINYPY_VALUE_SET || right_kind == TINYPY_VALUE_FROZENSET)) {
        tinypy_bool_t return_value_3 = tinypy_internal_set_equal_checked(left, right, out_equal, out_error);
        return return_value_3;
    }
    if (left_kind == TINYPY_VALUE_SLICE && right_kind == TINYPY_VALUE_SLICE) {
        tinypy_bool_t return_value_4 = __tinypy_comparison_slice_equal_checked(left, right, out_equal, out_error);
        return return_value_4;
    }
    *out_equal = tinypy_internal_equal_value(left, right, identity_implies_equal);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_dict_characterize(tinypy_value_t *left, tinypy_value_t *right, tinypy_value_t **out_key, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *different_key = NULL;
    tinypy_value_t *different_value = NULL;
    size_t index = 0U;

    while (index <= TINYPY_DICT_OBJECT(left)->mask) {
        tinypy_dict_object_t *left_dict = TINYPY_DICT_OBJECT(left);
        tinypy_dict_entry_t *entry = &left_dict->table[index];
        tinypy_value_t *key;
        tinypy_value_t *left_value;
        tinypy_value_t *right_value;
        int32_t equal;

        if (!TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
            index += 1U;
            continue;
        }
        key = TINYPY_RET(entry->key);
        if (different_key != NULL) {
            int32_t current_is_smaller = tinypy_compare_bool(different_key, key, TINYPY_COMPARE_LESS, out_error);

            if (current_is_smaller < 0) {
                TINYPY_DECREF(key);
                goto fail;
            }
            left_dict = TINYPY_DICT_OBJECT(left);
            if (current_is_smaller != 0 || index > left_dict->mask || !TINYPY_DICT_ENTRY_IS_ACTIVE(&left_dict->table[index])) {
                TINYPY_DECREF(key);
                index += 1U;
                continue;
            }
            entry = &left_dict->table[index];
        }
        left_value = TINYPY_RET(entry->value);
        if (tinypy_internal_dict_get_optional_index_checked(vm, right, key, NULL, &right_value, out_error) == 0) {
            TINYPY_DECREF(left_value);
            TINYPY_DECREF(key);
            goto fail;
        }
        if (right_value == NULL) {
            equal = 0;
        }
        else {
            TINYPY_INCREF(right_value);
            equal = tinypy_compare_bool(left_value, right_value, TINYPY_COMPARE_EQUAL, out_error);
            TINYPY_DECREF(right_value);
            if (equal < 0) {
                TINYPY_DECREF(left_value);
                TINYPY_DECREF(key);
                goto fail;
            }
        }
        if (equal == 0) {
            if (different_key != NULL) {
                TINYPY_DECREF(different_key);
                TINYPY_DECREF(different_value);
            }
            different_key = key;
            different_value = left_value;
        }
        else {
            TINYPY_DECREF(left_value);
            TINYPY_DECREF(key);
        }
        index += 1U;
    }
    *out_key = different_key;
    *out_value = different_value;
    return TINYPY_TRUE;

fail:
    if (different_key != NULL) {
        TINYPY_DECREF(different_key);
        TINYPY_DECREF(different_value);
    }
    *out_key = NULL;
    *out_value = NULL;
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_three_way(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_error_t **out_error) {
    tinypy_bool_t result = tinypy_internal_compare_three_way(left, right, out_order, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_dict_order(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_error_t **out_error) {
    size_t left_size = TINYPY_DICT_OBJECT(left)->used;
    size_t right_size = TINYPY_DICT_OBJECT(right)->used;
    tinypy_value_t *left_key = NULL;
    tinypy_value_t *left_value = NULL;
    tinypy_value_t *right_key = NULL;
    tinypy_value_t *right_value = NULL;
    int32_t order = 0;

    if (left_size != right_size) {
        *out_order = left_size < right_size ? -1 : 1;
        return TINYPY_TRUE;
    }
    if (__tinypy_comparison_dict_characterize(left, right, &left_key, &left_value, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (left_key == NULL) {
        *out_order = 0;
        return TINYPY_TRUE;
    }
    if (__tinypy_comparison_dict_characterize(right, left, &right_key, &right_value, out_error) == 0) {
        TINYPY_DECREF(left_value);
        TINYPY_DECREF(left_key);
        return TINYPY_FALSE;
    }
    if (right_key != NULL && __tinypy_comparison_three_way(left_key, right_key, &order, out_error) == 0) {
        goto fail;
    }
    if (order == 0 && right_value != NULL && __tinypy_comparison_three_way(left_value, right_value, &order, out_error) == 0) {
        goto fail;
    }
    if (right_key != NULL) {
        TINYPY_DECREF(right_value);
        TINYPY_DECREF(right_key);
    }
    TINYPY_DECREF(left_value);
    TINYPY_DECREF(left_key);
    *out_order = order;
    return TINYPY_TRUE;

fail:
    if (right_key != NULL) {
        TINYPY_DECREF(right_value);
        TINYPY_DECREF(right_key);
    }
    TINYPY_DECREF(left_value);
    TINYPY_DECREF(left_key);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_order(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, int32_t *out_order, tinypy_bool_t *out_unordered, tinypy_error_t **out_error) {
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    *out_unordered = 0;
    if (__tinypy_comparison_is_numeric(left_kind) != 0 && __tinypy_comparison_is_numeric(right_kind) != 0) {
        if (left_kind == TINYPY_VALUE_COMPLEX || right_kind == TINYPY_VALUE_COMPLEX) {
            tinypy_vm_t *vm_2 = TINYPY_VALUE_VM(left);
            tinypy_internal_make_vm_error(vm_2, TINYPY_ERROR_TYPE, "no ordering relation is defined for complex numbers", out_error);
            return TINYPY_FALSE;
        }
        if (tinypy_internal_numeric_order(left, right, out_order) == 0) {
            *out_order = 0;
            *out_unordered = 1;
        }
        return TINYPY_TRUE;
    }
    if ((left_kind == TINYPY_VALUE_STRING || left_kind == TINYPY_VALUE_UNICODE) && (right_kind == TINYPY_VALUE_STRING || right_kind == TINYPY_VALUE_UNICODE)) {
        if (left_kind != right_kind) {
            /* Ordering a byte string against unicode decodes it as ASCII. */
            const tinypy_value_t *string = left_kind == TINYPY_VALUE_STRING ? left : right;
            const uint8_t *bytes = TINYPY_TEXT_BYTES(string);
            size_t size = TINYPY_TEXT_BYTE_SIZE(string);
            size_t index;

            for (index = 0U; index < size; ++index) {
                if (bytes[index] >= 0x80U) {
                    (void)tinypy_internal_raise_ascii_decode_error(TINYPY_VALUE_VM(left), string, index, index + 1U, out_error);
                    return TINYPY_FALSE;
                }
            }
        }
        *out_order = tinypy_internal_text_order(left, right);
        return TINYPY_TRUE;
    }
    if (left_kind == TINYPY_VALUE_BYTEARRAY || right_kind == TINYPY_VALUE_BYTEARRAY || (left_kind == TINYPY_VALUE_BUFFER && right_kind == TINYPY_VALUE_BUFFER)) {
        const uint8_t *left_bytes;
        const uint8_t *right_bytes;
        size_t left_size;
        size_t right_size;
        size_t common_size;
        int32_t comparison;

        if (tinypy_internal_bytes_view(left, &left_bytes, &left_size) != 0 && tinypy_internal_bytes_view(right, &right_bytes, &right_size) != 0) {
            common_size = left_size < right_size ? left_size : right_size;
            comparison = common_size != 0U ? memcmp(left_bytes, right_bytes, common_size) : 0;
            *out_order = comparison < 0 ? -1 : (comparison > 0 ? 1 : (left_size < right_size ? -1 : (left_size > right_size ? 1 : 0)));
            return TINYPY_TRUE;
        }
    }
    if (left_kind == right_kind && (left_kind == TINYPY_VALUE_TUPLE || left_kind == TINYPY_VALUE_LIST)) {
        size_t left_size = left_kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(left) : TINYPY_LIST_SIZE(left);
        size_t right_size = right_kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(right) : TINYPY_LIST_SIZE(right);
        size_t index = 0U;

        while (index < (left_kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(left) : TINYPY_LIST_SIZE(left))
            && index < (right_kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(right) : TINYPY_LIST_SIZE(right))) {
            tinypy_value_t *left_item = left_kind == TINYPY_VALUE_TUPLE ? tinypy_internal_tuple_items(left)[index] : TINYPY_LIST_GET(left, index);
            tinypy_value_t *right_item = left_kind == TINYPY_VALUE_TUPLE ? tinypy_internal_tuple_items(right)[index] : TINYPY_LIST_GET(right, index);

            int32_t equal;

            if (left_item == right_item) {
                index += 1U;
                continue;
            }
            TINYPY_INCREF(left_item);
            TINYPY_INCREF(right_item);
            equal = tinypy_compare_bool(left_item, right_item, TINYPY_COMPARE_EQUAL, out_error);
            if (equal < 0) {
                TINYPY_DECREF(right_item);
                TINYPY_DECREF(left_item);
                return TINYPY_FALSE;
            }
            if (equal == 0) {
                int32_t ordered = tinypy_compare_bool(left_item, right_item, operation, out_error);

                TINYPY_DECREF(right_item);
                TINYPY_DECREF(left_item);
                if (ordered < 0) {
                    return TINYPY_FALSE;
                }
                if (operation == TINYPY_COMPARE_LESS) {
                    *out_order = ordered != 0 ? -1 : 0;
                }
                else if (operation == TINYPY_COMPARE_LESS_EQUAL) {
                    *out_order = ordered != 0 ? 0 : 1;
                }
                else if (operation == TINYPY_COMPARE_GREATER) {
                    *out_order = ordered != 0 ? 1 : 0;
                }
                else {
                    *out_order = ordered != 0 ? 0 : -1;
                }
                return TINYPY_TRUE;
            }
            TINYPY_DECREF(right_item);
            TINYPY_DECREF(left_item);
            index += 1U;
        }
        left_size = left_kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(left) : TINYPY_LIST_SIZE(left);
        right_size = right_kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(right) : TINYPY_LIST_SIZE(right);
        *out_order = left_size < right_size ? -1 : (left_size > right_size ? 1 : 0);
        return TINYPY_TRUE;
    }
    if ((left_kind == TINYPY_VALUE_SET || left_kind == TINYPY_VALUE_FROZENSET) && (right_kind == TINYPY_VALUE_SET || right_kind == TINYPY_VALUE_FROZENSET)) {
        tinypy_bool_t equal;

        if (tinypy_internal_set_equal_checked(left, right, &equal, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (equal != 0) {
            *out_order = 0;
            return TINYPY_TRUE;
        }
        if (tinypy_set_size(left) < tinypy_set_size(right)) {
            tinypy_bool_t left_subset;

            if (tinypy_internal_set_is_subset_checked(left, right, &left_subset, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (left_subset != 0) {
                *out_order = -1;
                return TINYPY_TRUE;
            }
        }
        else if (tinypy_set_size(left) > tinypy_set_size(right)) {
            tinypy_bool_t right_subset;

            if (tinypy_internal_set_is_subset_checked(right, left, &right_subset, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (right_subset != 0) {
                *out_order = 1;
                return TINYPY_TRUE;
            }
        }
        *out_order = 0;
        *out_unordered = 1;
        return TINYPY_TRUE;
    }
    if (left_kind == TINYPY_VALUE_DICT && right_kind == TINYPY_VALUE_DICT) {
        tinypy_bool_t equal;

        if (tinypy_internal_dict_equal_checked(left, right, &equal, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (equal != 0) {
            *out_order = 0;
            return TINYPY_TRUE;
        }
        tinypy_bool_t return_value_1 = __tinypy_comparison_dict_order(left, right, out_order, out_error);
        return return_value_1;
    }
    if (left_kind == TINYPY_VALUE_SLICE && right_kind == TINYPY_VALUE_SLICE) {
        tinypy_slice_object_t *left_slice = TINYPY_SLICE_OBJECT(left);
        tinypy_slice_object_t *right_slice = TINYPY_SLICE_OBJECT(right);
        tinypy_value_t *left_items[3] = {left_slice->start, left_slice->stop, left_slice->step};
        tinypy_value_t *right_items[3] = {right_slice->start, right_slice->stop, right_slice->step};
        size_t index;

        for (index = 0U; index < 3U; ++index) {
            if (left_items[index] != right_items[index]) {
                int32_t order;

                if (__tinypy_comparison_three_way(left_items[index], right_items[index], &order, out_error) == 0) {
                    return TINYPY_FALSE;
                }
                if (order != 0) {
                    *out_order = order;
                    return TINYPY_TRUE;
                }
            }
        }
        *out_order = 0;
        return TINYPY_TRUE;
    }
    if (left_kind == TINYPY_VALUE_NONE || right_kind == TINYPY_VALUE_NONE) {
        *out_order = left_kind == right_kind ? 0 : (left_kind == TINYPY_VALUE_NONE ? -1 : 1);
        return TINYPY_TRUE;
    }
    if (__tinypy_comparison_number_check(left) != __tinypy_comparison_number_check(right)) {
        *out_order = __tinypy_comparison_number_check(left) != 0 ? -1 : 1;
        return TINYPY_TRUE;
    }
    if (left->type != right->type) {
        *out_order = __tinypy_comparison_type_name_order(left->type, right->type);
        return TINYPY_TRUE;
    }
    if (left == right) {
        *out_order = 0;
        return TINYPY_TRUE;
    }
    *out_order = (uintptr_t)left < (uintptr_t)right ? -1 : 1;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_comparison_text_contains(tinypy_value_t *container, tinypy_value_t *item, tinypy_error_t **out_error) {
    const uint8_t *container_bytes;
    const uint8_t *item_bytes;
    size_t container_size;
    size_t item_size;
    size_t index;

    if ((TINYPY_VALUE_KIND(item) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(item) != TINYPY_VALUE_UNICODE)) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "string containment requires a string operand", out_error);
        return -1;
    }
    if (TINYPY_VALUE_KIND(container) != TINYPY_VALUE_KIND(item)) {
        tinypy_value_t *bytes_value = TINYPY_VALUE_KIND(container) == TINYPY_VALUE_STRING ? container : item;
        if (tinypy_internal_text_ascii_compatible(TINYPY_VALUE_VM(container), bytes_value, out_error) == 0) {
            return -1;
        }
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
static int32_t __tinypy_contains(tinypy_value_t *container, tinypy_value_t *item, tinypy_bool_t dispatch_special, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    TINYPY_CLEAR_ERROR(out_error);
    if (dispatch_special != 0 && tinypy_internal_object_has_special_override_key(container, vm->internal_special_contains_key) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_binary(container, vm->internal_special_contains_key, item, TINYPY_FALSE, out_error);
        int32_t truth;

        if (result == NULL) {
            return -1;
        }
        truth = tinypy_truth(result, out_error);
        TINYPY_DECREF(result);
        return truth;
    }
    if (container->type->sequence_slots != NULL && container->type->sequence_slots->contains != NULL) {
        int32_t return_value_1 = container->type->sequence_slots->contains(container, item, out_error);
        return return_value_1;
    }
    kind = TINYPY_VALUE_KIND(container);
    if (kind == TINYPY_VALUE_DICT) {
        tinypy_bool_t contains;

        if (tinypy_internal_dict_contains_checked(vm, container, item, &contains, out_error) == 0) {
            return INT32_C(-1);
        }
        return contains != 0 ? INT32_C(1) : INT32_C(0);
    }
    if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        int32_t return_value_3 = tinypy_set_contains(container, item, out_error);
        return return_value_3;
    }
    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        int32_t return_value_4 = __tinypy_comparison_text_contains(container, item, out_error);
        return return_value_4;
    }
    if (kind == TINYPY_VALUE_BYTEARRAY) {
        const uint8_t *needle;
        size_t needle_size;
        int64_t integer;
        size_t size = tinypy_bytearray_size(container);
        size_t index;

        if (tinypy_internal_bytes_view(item, &needle, &needle_size) != 0) {
            ptrdiff_t found = tinypy_internal_find_bytes(TINYPY_BYTEARRAY_OBJECT(container)->bytes, size, needle, needle_size, TINYPY_FALSE);
            return found >= 0 ? 1 : 0;
        }
        tinypy_value_type_e kind_2 = TINYPY_VALUE_KIND(item);
        tinypy_bool_t condition = __tinypy_comparison_is_numeric(kind_2) == 0;
        if (condition == 0) {
            condition = (TINYPY_VALUE_KIND(item) == TINYPY_VALUE_FLOAT || TINYPY_VALUE_KIND(item) == TINYPY_VALUE_COMPLEX);
        }
        if (condition) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bytearray containment requires a byte string or integer", out_error);
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
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "byte must be in range(0, 256)", out_error);
            return -1;
        }
        for (index = 0U; index < size; ++index) {
            if (TINYPY_BYTEARRAY_OBJECT(container)->bytes[index] == (uint8_t)integer) {
                return 1;
            }
        }
        return 0;
    }
    if (kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_LIST) {
        size_t index = 0U;

        while (index < TINYPY_SIZED_SIZE(container)) {
            tinypy_value_t *candidate = kind == TINYPY_VALUE_TUPLE ? tinypy_internal_tuple_items(container)[index] : TINYPY_LIST_GET(container, index);
            int32_t equal;

            if (candidate == item) {
                return 1;
            }
            TINYPY_INCREF(candidate);
            equal = tinypy_compare_bool(candidate, item, TINYPY_COMPARE_EQUAL, out_error);
            TINYPY_DECREF(candidate);
            if (equal < 0) {
                return -1;
            }
            if (equal != 0) {
                return 1;
            }
            index += 1U;
        }
        return 0;
    }
    if (dispatch_special != 0 && tinypy_internal_object_has_special_key(container, vm->internal_special_contains_key) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_binary(container, vm->internal_special_contains_key, item, TINYPY_FALSE, out_error);
        int32_t truth;

        if (result == NULL) {
            return -1;
        }
        truth = tinypy_truth(result, out_error);
        TINYPY_DECREF(result);
        return truth;
    }
    if (dispatch_special == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object does not support containment", out_error);
        return -1;
    }
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
        int32_t equal = candidate == item ? 1 : tinypy_compare_bool(candidate, item, TINYPY_COMPARE_EQUAL, out_error);

        if (equal < 0) {
            TINYPY_DECREF(candidate);
            TINYPY_DECREF(iterator);
            return -1;
        }
        if (equal != 0) {
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
    return 0;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_contains_builtin(tinypy_value_t *container, tinypy_value_t *item, tinypy_error_t **out_error) {
    int32_t result = __tinypy_contains(container, item, TINYPY_FALSE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_contains(tinypy_value_t *container, tinypy_value_t *item, tinypy_error_t **out_error) {
    int32_t result = __tinypy_contains(container, item, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_comparison_call_binary(tinypy_value_t *receiver, tinypy_value_t *name, tinypy_value_t *argument, tinypy_bool_t missing_is_not_implemented, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(receiver);
    tinypy_value_t *method = tinypy_internal_object_get_special_key(receiver, name, out_error);

    if (method == NULL) {
        if (missing_is_not_implemented != 0 &&
            (TINYPY_VALUE_KIND(receiver) != TINYPY_VALUE_OLD_INSTANCE ||
             (out_error != NULL && *out_error != NULL && tinypy_error_kind(*out_error) == TINYPY_ERROR_ATTRIBUTE) ||
             (vm->raised_type != NULL && TINYPY_VALUE_KIND(vm->raised_type) == TINYPY_VALUE_TYPE &&
              tinypy_type_is_subtype((tinypy_type_t *)vm->raised_type, vm->exception_types[TINYPY_EXCEPTION_ATTRIBUTE_ERROR]) != 0))) {
            if (out_error != NULL && *out_error != NULL) {
                tinypy_error_release(*out_error);
                *out_error = NULL;
            }
            tinypy_vm_clear_error(vm);
            tinypy_value_t *result = TINYPY_RET_NOT_IMPLEMENTED(vm);
            return result;
        }
        return NULL;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &argument, 1U);
    tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_special_result(tinypy_vm_t *vm, tinypy_value_t *result, tinypy_value_t **out_value, tinypy_bool_t *out_not_implemented) {
    if (result == &vm->not_implemented_object.base) {
        TINYPY_DECREF(result);
        *out_not_implemented = INT32_C(1);
        return TINYPY_TRUE;
    }
    *out_value = result;
    *out_not_implemented = TINYPY_FALSE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_compare_operation_e __tinypy_comparison_reverse_operation(tinypy_compare_operation_e operation) {
    static const tinypy_compare_operation_e reversed[] = {
        TINYPY_COMPARE_GREATER,
        TINYPY_COMPARE_GREATER_EQUAL,
        TINYPY_COMPARE_EQUAL,
        TINYPY_COMPARE_NOT_EQUAL,
        TINYPY_COMPARE_LESS,
        TINYPY_COMPARE_LESS_EQUAL};

    return reversed[(size_t)operation];
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_try_native_rich_slot(tinypy_value_t *receiver, tinypy_value_t *argument, tinypy_compare_operation_e operation, tinypy_bool_t *out_handled, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    tinypy_value_t *result;
    tinypy_bool_t not_implemented;

    *out_handled = INT32_C(0);
    if (receiver->type->rich_compare == NULL) {
        return TINYPY_TRUE;
    }
    result = receiver->type->rich_compare(receiver, argument, (int32_t)operation, out_error);
    if (result == NULL) {
        *out_handled = INT32_C(1);
        return TINYPY_FALSE;
    }
    (void)__tinypy_comparison_special_result(TINYPY_VALUE_VM(receiver), result, out_value, &not_implemented);
    if (not_implemented == 0) {
        *out_handled = INT32_C(1);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_has_python_rich_slot(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *const rich_names[] = {vm->internal_special_lt_key, vm->internal_special_le_key, vm->internal_special_eq_key, vm->internal_special_ne_key, vm->internal_special_gt_key, vm->internal_special_ge_key};
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        return TINYPY_FALSE;
    }
    for (size_t index = 0U; index < 6U; ++index) {
        if (tinypy_internal_object_has_special_override_key(value, rich_names[index]) != 0) {
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_try_half_rich(tinypy_value_t *receiver, tinypy_value_t *argument, tinypy_compare_operation_e operation, tinypy_bool_t *out_handled, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(receiver);
    tinypy_value_t *const rich_names[] = {vm->internal_special_lt_key, vm->internal_special_le_key, vm->internal_special_eq_key, vm->internal_special_ne_key, vm->internal_special_gt_key, vm->internal_special_ge_key};
    tinypy_value_t *name = rich_names[(size_t)operation];
    tinypy_bool_t not_implemented;

    if (tinypy_internal_object_has_special_key(receiver, name) == 0) {
        return TINYPY_TRUE;
    }
    tinypy_value_t *result = __tinypy_comparison_call_binary(receiver, name, argument, TINYPY_TRUE, out_error);

    if (result == NULL) {
        *out_handled = TINYPY_TRUE;
        return TINYPY_FALSE;
    }
    (void)__tinypy_comparison_special_result(TINYPY_VALUE_VM(receiver), result, out_value, &not_implemented);
    *out_handled = not_implemented == 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Python 2 slot dispatch itself tries both operands. The outer rich-compare
   dispatch may then call the slot again after NotImplemented. */
static tinypy_bool_t __tinypy_comparison_try_rich_slot(tinypy_value_t *receiver, tinypy_value_t *argument, tinypy_compare_operation_e operation, tinypy_bool_t *out_handled, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    tinypy_bool_t classic = TINYPY_VALUE_KIND(receiver) == TINYPY_VALUE_OLD_INSTANCE ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_bool_t python = __tinypy_comparison_has_python_rich_slot(receiver);

    *out_handled = TINYPY_FALSE;
    if (classic != 0 || python != 0) {
        if (__tinypy_comparison_try_half_rich(receiver, argument, operation, out_handled, out_value, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (*out_handled != 0) {
            return TINYPY_TRUE;
        }
        tinypy_bool_t reflected = classic != 0 ? (TINYPY_VALUE_KIND(argument) == TINYPY_VALUE_OLD_INSTANCE ? TINYPY_TRUE : TINYPY_FALSE) : __tinypy_comparison_has_python_rich_slot(argument);
        if (reflected != 0) {
            tinypy_bool_t result = __tinypy_comparison_try_half_rich(argument, receiver, __tinypy_comparison_reverse_operation(operation), out_handled, out_value, out_error);
            return result;
        }
        return TINYPY_TRUE;
    }
    if (receiver->type->rich_compare != NULL) {
        tinypy_bool_t result = __tinypy_comparison_try_native_rich_slot(receiver, argument, operation, out_handled, out_value, out_error);
        return result;
    }
    tinypy_bool_t result = __tinypy_comparison_try_half_rich(receiver, argument, operation, out_handled, out_value, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* One direction of the Python 2 three-way protocol. A NotImplemented result
   leaves ordered clear so the caller can try the other operand. */
static tinypy_bool_t __tinypy_comparison_try_three_way(tinypy_value_t *left, tinypy_value_t *right, tinypy_bool_t swapped, int64_t *out_order, tinypy_bool_t *out_ordered, tinypy_bool_t *out_handled, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *result;
    int64_t order;

    /* A built-in's ordering is already the default ordering, and its __cmp__
       wrapper type-checks its argument the way CPython's wrap_cmpfunc does, so
       the three-way protocol only applies to types defining __cmp__ in Python. */
    if (tinypy_internal_object_has_special_override_key(left, vm->internal_special_cmp_key) == 0) {
        return TINYPY_TRUE;
    }
    result = __tinypy_comparison_call_binary(left, vm->internal_special_cmp_key, right, TINYPY_TRUE, out_error);
    if (result == NULL) {
        *out_handled = INT32_C(1);
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(result) == TINYPY_VALUE_NOT_IMPLEMENTED) {
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    if (tinypy_internal_number_as_i64(result, &order, out_error) == 0) {
        TINYPY_DECREF(result);
        if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_OLD_INSTANCE) {
            if (out_error != NULL && *out_error != NULL) {
                tinypy_error_release(*out_error);
                *out_error = NULL;
            }
            tinypy_vm_clear_error(vm);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "comparison did not return an int", out_error);
        }
        *out_handled = INT32_C(1);
        return TINYPY_FALSE;
    }
    TINYPY_DECREF(result);
    order = order < 0 ? -1 : (order > 0 ? 1 : 0);
    *out_order = swapped != 0 ? -order : order;
    *out_ordered = TINYPY_TRUE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_comparison_coerce_classic(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    for (size_t index = 0U; index < 2U; ++index) {
        tinypy_value_t *receiver = index == 0U ? left : right;
        tinypy_value_t *argument = index == 0U ? right : left;

        if (TINYPY_VALUE_KIND(receiver) != TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_object_has_special_key(receiver, vm->internal_special_coerce_key) == 0) {
            continue;
        }
        tinypy_value_t *pair = __tinypy_comparison_call_binary(receiver, vm->internal_special_coerce_key, argument, TINYPY_TRUE, out_error);

        if (pair == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(pair) == TINYPY_VALUE_NONE || TINYPY_VALUE_KIND(pair) == TINYPY_VALUE_NOT_IMPLEMENTED) {
            TINYPY_DECREF(pair);
            continue;
        }
        if (TINYPY_VALUE_KIND(pair) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(pair) != 2U) {
            TINYPY_DECREF(pair);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "coercion should return None or 2-tuple", out_error);
            return NULL;
        }
        if (index != 0U) {
            tinypy_value_t *items[] = {TINYPY_TUPLE_GET(pair, 1U), TINYPY_TUPLE_GET(pair, 0U)};
            tinypy_value_t *ordered = tinypy_tuple_from_items(vm, items, 2U);

            TINYPY_DECREF(pair);
            pair = ordered;
        }
        return pair;
    }
    tinypy_value_t *items[] = {left, right};

    tinypy_value_t *pair = tinypy_tuple_from_items(vm, items, 2U);
    return pair;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_try_three_way_pair(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_bool_t *out_ordered, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_vm_t *right_vm = TINYPY_VALUE_VM(right);
    int64_t order = 0;
    tinypy_bool_t handled = TINYPY_FALSE;

    *out_ordered = TINYPY_FALSE;
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_OLD_INSTANCE || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *pair = __tinypy_comparison_coerce_classic(left, right, out_error);
        tinypy_bool_t success = TINYPY_TRUE;

        if (pair == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_value_t *first = TINYPY_TUPLE_GET(pair, 0U);
        tinypy_value_t *second = TINYPY_TUPLE_GET(pair, 1U);

        if (TINYPY_VALUE_KIND(first) != TINYPY_VALUE_OLD_INSTANCE && TINYPY_VALUE_KIND(second) != TINYPY_VALUE_OLD_INSTANCE) {
            success = tinypy_internal_compare_three_way(first, second, out_order, out_error);
            *out_ordered = success;
        }
        else {
            if (TINYPY_VALUE_KIND(first) == TINYPY_VALUE_OLD_INSTANCE) {
                success = __tinypy_comparison_try_three_way(first, second, TINYPY_FALSE, &order, out_ordered, &handled, out_error);
            }
            if (success != 0 && *out_ordered == 0 && TINYPY_VALUE_KIND(second) == TINYPY_VALUE_OLD_INSTANCE) {
                success = __tinypy_comparison_try_three_way(second, first, TINYPY_TRUE, &order, out_ordered, &handled, out_error);
            }
            if (*out_ordered != 0) {
                *out_order = (int32_t)order;
            }
        }
        TINYPY_DECREF(pair);
        return success;
    }
    if (__tinypy_comparison_try_three_way(left, right, TINYPY_FALSE, &order, out_ordered, &handled, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (*out_ordered == 0 && __tinypy_comparison_try_three_way(right, left, TINYPY_TRUE, &order, out_ordered, &handled, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (*out_ordered == 0 && (tinypy_internal_object_has_special_override_key(left, vm->internal_special_cmp_key) != 0 || tinypy_internal_object_has_special_override_key(right, right_vm->internal_special_cmp_key) != 0)) {
        order = left == right ? 0 : ((uintptr_t)left < (uintptr_t)right ? -1 : 1);
        *out_ordered = TINYPY_TRUE;
    }
    *out_order = (int32_t)order;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_try_rich(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_bool_t *out_handled, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    tinypy_compare_operation_e reversed = __tinypy_comparison_reverse_operation(operation);

    *out_handled = TINYPY_FALSE;
    if (right->type != left->type && tinypy_type_is_subtype(right->type, left->type) != 0) {
        if (__tinypy_comparison_try_rich_slot(right, left, reversed, out_handled, out_value, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (*out_handled != 0) {
            return TINYPY_TRUE;
        }
    }
    if (__tinypy_comparison_try_rich_slot(left, right, operation, out_handled, out_value, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (*out_handled != 0) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t result = __tinypy_comparison_try_rich_slot(right, left, reversed, out_handled, out_value, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_comparison_order_value(tinypy_vm_t *vm, tinypy_compare_operation_e operation, int32_t order) {
    tinypy_bool_t comparison;

    if (operation == TINYPY_COMPARE_LESS) {
        comparison = order < 0;
    }
    else if (operation == TINYPY_COMPARE_LESS_EQUAL) {
        comparison = order <= 0;
    }
    else if (operation == TINYPY_COMPARE_EQUAL) {
        comparison = order == 0;
    }
    else if (operation == TINYPY_COMPARE_NOT_EQUAL) {
        comparison = order != 0;
    }
    else if (operation == TINYPY_COMPARE_GREATER) {
        comparison = order > 0;
    }
    else {
        comparison = order >= 0;
    }
    tinypy_value_t *result = tinypy_bool_from_i32(vm, comparison);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_try_special(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_bool_t *out_handled, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    int32_t order = 0;
    tinypy_bool_t ordered = TINYPY_FALSE;

    *out_handled = TINYPY_FALSE;
    if (left->type == right->type && TINYPY_VALUE_KIND(left) != TINYPY_VALUE_OLD_INSTANCE) {
        if (__tinypy_comparison_try_rich_slot(left, right, operation, out_handled, out_value, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (*out_handled != 0) {
            return TINYPY_TRUE;
        }
        if (tinypy_internal_object_has_special_override_key(left, vm->internal_special_cmp_key) != 0) {
            if (__tinypy_comparison_try_three_way_pair(left, right, &order, &ordered, out_error) == 0) {
                return TINYPY_FALSE;
            }
            *out_handled = ordered;
        }
    }
    if (*out_handled == 0) {
        if (__tinypy_comparison_try_rich(left, right, operation, out_handled, out_value, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (*out_handled != 0) {
            return TINYPY_TRUE;
        }
        if (__tinypy_comparison_try_three_way_pair(left, right, &order, &ordered, out_error) == 0) {
            return TINYPY_FALSE;
        }
        *out_handled = ordered;
    }
    if (ordered != 0) {
        *out_value = __tinypy_comparison_order_value(TINYPY_VALUE_VM(left), operation, order);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_comparison_default_bool(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_error_t **out_error) {
    int32_t order;
    tinypy_bool_t unordered;

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
        int32_t return_value_1 = tinypy_exception_matches(left, right, out_error);
        return return_value_1;
    }
    if (operation <= TINYPY_COMPARE_GREATER_EQUAL && (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_DICT_KEYS || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_DICT_ITEMS || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_DICT_KEYS || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_DICT_ITEMS)) {
        tinypy_bool_t result;
        tinypy_error_t *local_error = NULL;
        tinypy_error_t **comparison_error = out_error != NULL ? out_error : &local_error;

        if (tinypy_internal_set_like_compare_checked(left, right, operation, &result, comparison_error) != 0) {
            return result != 0 ? INT32_C(1) : INT32_C(0);
        }
        if (local_error != NULL) {
            tinypy_error_release(local_error);
            return INT32_C(-1);
        }
        if (out_error != NULL && *out_error != NULL) {
            return INT32_C(-1);
        }
    }
    if (operation == TINYPY_COMPARE_EQUAL) {
        tinypy_bool_t equal;

        if (__tinypy_comparison_equal_checked(left, right, TINYPY_FALSE, &equal, out_error) == 0) {
            return -1;
        }
        return equal != 0 ? 1 : 0;
    }
    if (operation == TINYPY_COMPARE_NOT_EQUAL) {
        tinypy_bool_t equal;

        if (__tinypy_comparison_equal_checked(left, right, TINYPY_FALSE, &equal, out_error) == 0) {
            return -1;
        }
        return equal == 0 ? 1 : 0;
    }
    if (__tinypy_comparison_order(left, right, operation, &order, &unordered, out_error) == 0) {
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
//////////////////////////////////////////////////////////////////////////
/* Comparing containers recurses through C, so it shares the interpreter
   recursion budget the way CPython's Py_EnterRecursiveCall does. Without this
   a self-referential or deeply nested structure overflows the C stack. */
static tinypy_bool_t __tinypy_comparison_enter(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    if (tinypy_internal_recursion_check(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded in cmp", out_error) == 0) {
        return TINYPY_FALSE;
    }
    vm->evaluation_depth += 1U;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_comparison_leave(tinypy_vm_t *vm) {
    vm->evaluation_depth -= 1U;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_comparison_sequence_order_value(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    size_t index = 0U;

    while (index < TINYPY_SIZED_SIZE(left) && index < TINYPY_SIZED_SIZE(right)) {
        tinypy_value_t *left_item = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(left, index) : TINYPY_LIST_GET(left, index);
        tinypy_value_t *right_item = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(right, index) : TINYPY_LIST_GET(right, index);

        if (left_item == right_item) {
            index += 1U;
            continue;
        }
        TINYPY_INCREF(left_item);
        TINYPY_INCREF(right_item);
        int32_t equal = tinypy_compare_bool(left_item, right_item, TINYPY_COMPARE_EQUAL, out_error);

        TINYPY_DECREF(right_item);
        TINYPY_DECREF(left_item);
        if (equal < 0) {
            return NULL;
        }
        if (equal == 0) {
            /* Equality callbacks can replace or remove list entries. */
            if (index >= TINYPY_SIZED_SIZE(left) || index >= TINYPY_SIZED_SIZE(right)) {
                break;
            }
            left_item = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(left, index) : TINYPY_LIST_GET(left, index);
            right_item = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(right, index) : TINYPY_LIST_GET(right, index);
            TINYPY_INCREF(left_item);
            TINYPY_INCREF(right_item);
            tinypy_value_t *result = tinypy_compare_value(left_item, right_item, operation, out_error);

            TINYPY_DECREF(right_item);
            TINYPY_DECREF(left_item);
            return result;
        }
        index += 1U;
    }
    size_t left_size = TINYPY_SIZED_SIZE(left);
    size_t right_size = TINYPY_SIZED_SIZE(right);
    int32_t order = left_size < right_size ? -1 : (left_size > right_size ? 1 : 0);
    tinypy_value_t *result = __tinypy_comparison_order_value(TINYPY_VALUE_VM(left), operation, order);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_comparison_builtin_value_inner(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);

    if ((kind == TINYPY_VALUE_UNICODE && right_kind == TINYPY_VALUE_BUFFER) || (kind == TINYPY_VALUE_BUFFER && right_kind == TINYPY_VALUE_UNICODE)) {
        tinypy_value_t *buffer = kind == TINYPY_VALUE_BUFFER ? left : right;
        const uint8_t *bytes;
        size_t size;

        if (tinypy_internal_bytes_view(buffer, &bytes, &size) == 0) {
            return NULL;
        }
        tinypy_value_t *text = tinypy_internal_string_from_bytes_checked(TINYPY_VALUE_VM(left), bytes, size, out_error);

        if (text == NULL) {
            return NULL;
        }
        tinypy_value_t *result = kind == TINYPY_VALUE_BUFFER
            ? __tinypy_comparison_builtin_value_inner(text, right, operation, out_error)
            : __tinypy_comparison_builtin_value_inner(left, text, operation, out_error);

        TINYPY_DECREF(text);
        return result;
    }

    if ((kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_TUPLE) && kind == TINYPY_VALUE_KIND(right) && operation <= TINYPY_COMPARE_GREATER_EQUAL && operation != TINYPY_COMPARE_EQUAL && operation != TINYPY_COMPARE_NOT_EQUAL) {
        tinypy_value_t *result = __tinypy_comparison_sequence_order_value(left, right, operation, out_error);
        return result;
    }
    int32_t comparison = __tinypy_comparison_default_bool(left, right, operation, out_error);

    if (comparison < 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_bool_from_i32(TINYPY_VALUE_VM(left), comparison);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_comparison_bool_inner(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    if (left == right && (operation == TINYPY_COMPARE_EQUAL || operation == TINYPY_COMPARE_NOT_EQUAL)) {
        return operation == TINYPY_COMPARE_EQUAL ? 1 : 0;
    }
    if (operation <= TINYPY_COMPARE_GREATER_EQUAL && (left->type->rich_compare != NULL || right->type->rich_compare != NULL || tinypy_internal_comparison_is_exact_builtin(left) == 0 || tinypy_internal_comparison_is_exact_builtin(right) == 0)) {
        tinypy_bool_t handled;
        tinypy_value_t *special_value = NULL;

        if (__tinypy_comparison_try_special(left, right, operation, &handled, &special_value, out_error) == 0) {
            return -1;
        }
        if (handled != 0) {
            int32_t truth = tinypy_truth(special_value, out_error);

            TINYPY_DECREF(special_value);
            return truth;
        }
    }
    tinypy_value_t *result = __tinypy_comparison_builtin_value_inner(left, right, operation, out_error);

    if (result == NULL) {
        return -1;
    }
    int32_t truth = tinypy_truth(result, out_error);

    TINYPY_DECREF(result);
    return truth;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_comparison_value_inner(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    if (operation <= TINYPY_COMPARE_GREATER_EQUAL && (left->type->rich_compare != NULL || right->type->rich_compare != NULL || tinypy_internal_comparison_is_exact_builtin(left) == 0 || tinypy_internal_comparison_is_exact_builtin(right) == 0)) {
        tinypy_bool_t handled;
        tinypy_value_t *special_value = NULL;

        if (__tinypy_comparison_try_special(left, right, operation, &handled, &special_value, out_error) == 0) {
            return NULL;
        }
        if (handled != 0) {
            return special_value;
        }
    }
    tinypy_value_t *return_value = __tinypy_comparison_builtin_value_inner(left, right, operation, out_error);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_compare_builtin_value(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    if (__tinypy_comparison_enter(vm, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_comparison_builtin_value_inner(left, right, operation, out_error);

    __tinypy_comparison_leave(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_compare_bool(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    if (__tinypy_comparison_enter(vm, out_error) == 0) {
        return -1;
    }
    int32_t result = __tinypy_comparison_bool_inner(left, right, operation, out_error);

    __tinypy_comparison_leave(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_compare_value(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    if (__tinypy_comparison_enter(vm, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_comparison_value_inner(left, right, operation, out_error);

    __tinypy_comparison_leave(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_three_way_inner(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    tinypy_bool_t ordered;

    if (left == right) {
        *out_order = 0;
        return TINYPY_TRUE;
    }
    if (left->type == right->type) {
        if (kind == TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_object_has_special_override_key(left, vm->internal_special_cmp_key) != 0) {
            if (__tinypy_comparison_try_three_way_pair(left, right, out_order, &ordered, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (ordered != 0) {
                return TINYPY_TRUE;
            }
        }
        else if (kind == TINYPY_VALUE_DICT) {
            tinypy_bool_t result = __tinypy_comparison_dict_order(left, right, out_order, out_error);
            return result;
        }
        else if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG || kind == TINYPY_VALUE_BUFFER || kind == TINYPY_VALUE_SLICE) {
            tinypy_bool_t unordered;
            tinypy_bool_t result = __tinypy_comparison_order(left, right, TINYPY_COMPARE_LESS, out_order, &unordered, out_error);
            return result;
        }
        else if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
            tinypy_internal_make_vm_error(TINYPY_VALUE_VM(left), TINYPY_ERROR_TYPE, "cannot compare sets using cmp()", out_error);
            return TINYPY_FALSE;
        }
    }
    static const tinypy_compare_operation_e operations[3] = {TINYPY_COMPARE_EQUAL, TINYPY_COMPARE_LESS, TINYPY_COMPARE_GREATER};
    static const int32_t outcomes[3] = {0, -1, 1};
    for (size_t index = 0U; index < 3U; ++index) {
        tinypy_bool_t handled;
        tinypy_value_t *value = NULL;

        if (__tinypy_comparison_try_rich(left, right, operations[index], &handled, &value, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (handled != 0) {
            int32_t truth = tinypy_truth(value, out_error);

            TINYPY_DECREF(value);
            if (truth < 0) {
                return TINYPY_FALSE;
            }
            if (truth != 0) {
                *out_order = outcomes[index];
                return TINYPY_TRUE;
            }
        }
    }
    if (__tinypy_comparison_try_three_way_pair(left, right, out_order, &ordered, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (ordered != 0) {
        return TINYPY_TRUE;
    }
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);
    if ((kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) && (right_kind == TINYPY_VALUE_SET || right_kind == TINYPY_VALUE_FROZENSET)) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(left), TINYPY_ERROR_TYPE, "cannot compare sets using cmp()", out_error);
        return TINYPY_FALSE;
    }
    tinypy_bool_t unordered;
    if (__tinypy_comparison_order(left, right, TINYPY_COMPARE_LESS, out_order, &unordered, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (unordered != 0) {
        *out_order = tinypy_internal_comparison_fallback_order(left, right);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_compare_three_way(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);

    if (__tinypy_comparison_enter(vm, out_error) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t result = __tinypy_comparison_three_way_inner(left, right, out_order, out_error);

    __tinypy_comparison_leave(vm);
    return result;
}
