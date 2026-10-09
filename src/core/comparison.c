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
    tinypy_value_t *converted = tinypy_internal_call_int_conversion(value, &handled, out_error);

    if (converted == NULL) {
        if (handled == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "an integer is required", out_error);
        }
        return TINYPY_FALSE;
    }
    kind = TINYPY_VALUE_KIND(converted);
    if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG) {
        TINYPY_DECREF(converted);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__int__ method should return an integer", out_error);
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
    tinypy_value_t *function = tinypy_internal_type_function_key(value, name);

    if (function != NULL) {
        tinypy_value_t *direct = tinypy_internal_call_type_function(function, value, NULL, 0U, out_error);
        return direct;
    }
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

//////////////////////////////////////////////////////////////////////////
/* Calls an optional special method without arguments, reaching a classic
   instance's __getattr__ as well. Returns 1 with the result, 0 when the
   method is missing and -1 on error. */
static int32_t __tinypy_comparison_call_optional(tinypy_value_t *value, tinypy_value_t *name, tinypy_value_t **out_result, tinypy_error_t **out_error) {
    tinypy_value_t *method;
    int32_t found = tinypy_internal_object_lookup_special_key(value, name, &method, out_error);

    *out_result = NULL;
    if (found <= 0) {
        return found;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *args = TINYPY_RET_EMPTY_TUPLE(vm);
    *out_result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return *out_result != NULL ? INT32_C(1) : -INT32_C(1);
}

static tinypy_value_t *__tinypy_comparison_call_binary(tinypy_value_t *receiver, tinypy_value_t *name, tinypy_value_t *argument, tinypy_bool_t missing_is_not_implemented, tinypy_error_t **out_error);
static tinypy_bool_t __tinypy_comparison_equal_checked(tinypy_value_t *left, tinypy_value_t *right, tinypy_bool_t identity_implies_equal, tinypy_bool_t *out_equal, tinypy_error_t **out_error);
static tinypy_bool_t __tinypy_comparison_try_coerced(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_bool_t *out_ordered, tinypy_error_t **out_error);

//////////////////////////////////////////////////////////////////////////
/* instance_nonzero takes any int from __nonzero__ or __len__, while
   slot_nb_nonzero takes only an exact int or a bool. Consumes the result. */
static int32_t __tinypy_truth_nonzero_result(tinypy_value_t *value, tinypy_value_t *result, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e result_kind = TINYPY_VALUE_KIND(result);

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        if (result_kind != TINYPY_VALUE_BOOL && result_kind != TINYPY_VALUE_INTEGER) {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__nonzero__ should return an int", out_error);
            return INT32_C(-1);
        }
        if (TINYPY_INTEGER_VALUE(result) < 0) {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__nonzero__ should return >= 0", out_error);
            return INT32_C(-1);
        }
    }
    else if (result_kind != TINYPY_VALUE_BOOL && result->type != &vm->types[TINYPY_VALUE_INTEGER]) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("__nonzero__ should return bool or int, returned "),
            TINYPY_MESSAGE_PART_TYPE_NAME(result),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        TINYPY_DECREF(result);
        return INT32_C(-1);
    }
    int32_t truth = TINYPY_INTEGER_VALUE(result) != 0 ? INT32_C(1) : INT32_C(0);

    TINYPY_DECREF(result);
    return truth;
}
//////////////////////////////////////////////////////////////////////////
/* A classic __len__ result is checked like __nonzero__; slot_sq_length
   converts it with PyInt_AsSsize_t. Consumes the result. */
static int32_t __tinypy_truth_length_result(tinypy_value_t *value, tinypy_value_t *result, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        int32_t classic_truth = __tinypy_truth_nonzero_result(value, result, out_error);
        return classic_truth;
    }
    int64_t length;
    tinypy_bool_t converted = tinypy_internal_number_as_ssize(result, &length, out_error);

    TINYPY_DECREF(result);
    if (converted == 0) {
        return INT32_C(-1);
    }
    if (length < 0) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_VALUE, "__len__() should return >= 0", out_error);
        return INT32_C(-1);
    }
    int32_t truth = length != 0 ? INT32_C(1) : INT32_C(0);
    return truth;
}
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
    if (dispatch_special != 0 && __tinypy_internal_object_overrides_dispatch(value, TINYPY_INTERNAL_DISPATCH_BIT(NONZERO)) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_no_args(value, vm->internal_special_nonzero_key, out_error);

        if (result == NULL) {
            return INT32_C(-1);
        }
        int32_t truth = __tinypy_truth_nonzero_result(value, result, out_error);
        return truth;
    }
    /* An inherited numeric nonzero slot wins over an added length method. */
    if (__tinypy_comparison_is_numeric(kind) != 0) {
        goto builtin_truth;
    }
    if (dispatch_special != 0 && __tinypy_internal_object_overrides_dispatch(value, TINYPY_INTERNAL_DISPATCH_BIT(LENGTH)) != 0) {
        tinypy_value_t *result = __tinypy_comparison_call_no_args(value, vm->internal_special_length_key, out_error);

        if (result == NULL) {
            return INT32_C(-1);
        }
        int32_t truth = __tinypy_truth_length_result(value, result, out_error);
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
    if (exact_builtin != 0 || dispatch_special == 0) {
        return INT32_C(1);
    }
    tinypy_value_t *result;
    int32_t found = __tinypy_comparison_call_optional(value, vm->internal_special_nonzero_key, &result, out_error);
    if (found < 0) {
        return INT32_C(-1);
    }
    if (found > 0) {
        int32_t truth = __tinypy_truth_nonzero_result(value, result, out_error);
        return truth;
    }
    found = __tinypy_comparison_call_optional(value, vm->internal_special_length_key, &result, out_error);
    if (found < 0) {
        return INT32_C(-1);
    }
    if (found > 0) {
        int32_t truth = __tinypy_truth_length_result(value, result, out_error);
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
/* The C name of a type as default_3way_compare sees it: a built-in type of
   another module than __builtin__ carries its module prefix, a class only
   its own name. */
static size_t __tinypy_comparison_type_name(const tinypy_type_t *type, const char **out_module, size_t *out_module_size) {
    tinypy_vm_t *vm = type->vm;

    *out_module = NULL;
    *out_module_size = 0U;
    if ((type->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) == 0U && memchr(type->name, '.', type->name_size) == NULL) {
        tinypy_value_t *module = tinypy_internal_dict_get_optional_suppressed(vm, type->dict, vm->internal_special_module_key);

        if (module != NULL && TINYPY_VALUE_KIND(module) == TINYPY_VALUE_STRING && TINYPY_NAME_EQ(module, vm->internal_builtin_module_name) == 0) {
            *out_module = (const char *)TINYPY_TEXT_BYTES(module);
            *out_module_size = TINYPY_TEXT_BYTE_SIZE(module);
            return *out_module_size + 1U + type->name_size;
        }
    }
    return type->name_size;
}
//////////////////////////////////////////////////////////////////////////
static uint8_t __tinypy_comparison_type_name_byte(const tinypy_type_t *type, const char *module, size_t module_size, size_t index) {
    if (module == NULL) {
        return (uint8_t)type->name[index];
    }
    if (index < module_size) {
        return (uint8_t)module[index];
    }
    uint8_t byte = index == module_size ? (uint8_t)'.' : (uint8_t)type->name[index - module_size - 1U];

    return byte;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_comparison_type_name_order(const tinypy_type_t *left, const tinypy_type_t *right) {
    const char *left_module;
    size_t left_module_size;
    size_t left_size = __tinypy_comparison_type_name(left, &left_module, &left_module_size);
    const char *right_module;
    size_t right_module_size;
    size_t right_size = __tinypy_comparison_type_name(right, &right_module, &right_module_size);
    size_t common_size = left_size < right_size ? left_size : right_size;

    for (size_t index = 0U; index < common_size; ++index) {
        uint8_t left_byte = __tinypy_comparison_type_name_byte(left, left_module, left_module_size, index);
        uint8_t right_byte = __tinypy_comparison_type_name_byte(right, right_module, right_module_size, index);

        if (left_byte != right_byte) {
            return left_byte < right_byte ? -1 : 1;
        }
    }
    if (left_size != right_size) {
        return left_size < right_size ? -1 : 1;
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* PyNumber_Check: a classic instance always has nb_int; other values are
   numbers when their type converts with __int__ or __float__. */
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
/* default_3way_compare breaks a tie of type names by the addresses of the
   type objects: CPython's static number types lie in object-file order
   (bool, classic instance, complex, float, int, long) before its other
   static types, and classes created at run time come last. */
static int32_t __tinypy_comparison_type_rank(const tinypy_type_t *type) {
    tinypy_value_type_e kind = type->layout_kind;

    if ((type->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) != 0U) {
        return 7;
    }
    if ((size_t)kind >= TINYPY_BUILTIN_TYPE_COUNT || type != &type->vm->types[kind]) {
        return 6;
    }
    switch (kind) {
    case TINYPY_VALUE_BOOL:
        return 0;
    case TINYPY_VALUE_OLD_INSTANCE:
        return 1;
    case TINYPY_VALUE_COMPLEX:
        return 2;
    case TINYPY_VALUE_FLOAT:
        return 3;
    case TINYPY_VALUE_INTEGER:
        return 4;
    case TINYPY_VALUE_LONG:
        return 5;
    default:
        return 6;
    }
}
//////////////////////////////////////////////////////////////////////////
/* default_3way_compare: values of one type order by address, None comes
   first, other types order by type name with numbers having the empty name,
   and equally named types by their type objects. */
int32_t tinypy_internal_comparison_fallback_order(tinypy_value_t *left, tinypy_value_t *right) {
    if (left->type == right->type) {
        return left == right ? 0 : ((uintptr_t)left < (uintptr_t)right ? -1 : 1);
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_NONE || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_NONE) {
        int32_t result = TINYPY_VALUE_KIND(left) == TINYPY_VALUE_NONE ? -1 : 1;

        return result;
    }
    tinypy_bool_t left_numeric = __tinypy_comparison_number_check(left);
    tinypy_bool_t right_numeric = __tinypy_comparison_number_check(right);
    if (left_numeric != right_numeric) {
        return left_numeric != 0 ? -1 : 1;
    }
    if (left_numeric == 0) {
        int32_t named = __tinypy_comparison_type_name_order(left->type, right->type);

        if (named != 0) {
            return named;
        }
    }
    int32_t left_rank = __tinypy_comparison_type_rank(left->type);
    int32_t right_rank = __tinypy_comparison_type_rank(right->type);
    if (left_rank != right_rank) {
        return left_rank < right_rank ? -1 : 1;
    }
    return (uintptr_t)left->type < (uintptr_t)right->type ? -1 : 1;
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
            break;
        }
        index += 1U;
    }
    /* list_richcompare reads the sizes again: an item comparison may have
       shrunk either list below the differing position. */
    if (index < TINYPY_SIZED_SIZE(left) && index < TINYPY_SIZED_SIZE(right)) {
        *out_equal = TINYPY_FALSE;
        return TINYPY_TRUE;
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
        tinypy_bool_t subset = TINYPY_FALSE;
        tinypy_bool_t reversed = operation == TINYPY_COMPARE_GREATER || operation == TINYPY_COMPARE_GREATER_EQUAL;
        tinypy_value_t *smaller = reversed != 0 ? right : left;
        tinypy_value_t *larger = reversed != 0 ? left : right;
        tinypy_bool_t strict = operation == TINYPY_COMPARE_LESS || operation == TINYPY_COMPARE_GREATER;

        if ((strict == 0 || tinypy_set_size(smaller) < tinypy_set_size(larger)) &&
            tinypy_internal_set_is_subset_checked(smaller, larger, &subset, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (operation == TINYPY_COMPARE_LESS) {
            *out_order = subset != 0 ? -1 : 0;
        }
        else if (operation == TINYPY_COMPARE_LESS_EQUAL) {
            *out_order = subset != 0 ? 0 : 1;
        }
        else if (operation == TINYPY_COMPARE_GREATER) {
            *out_order = subset != 0 ? 1 : 0;
        }
        else {
            *out_order = subset != 0 ? 0 : -1;
        }
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
    *out_order = tinypy_internal_comparison_fallback_order(left, right);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_comparison_text_contains(tinypy_value_t *container, tinypy_value_t *item, tinypy_error_t **out_error) {
    const uint8_t *container_bytes;
    const uint8_t *item_bytes;
    size_t container_size;
    size_t item_size;
    size_t index;

    /* string_contains names the operand; PyUnicode_Contains coerces it the
       way unicode() does. */
    if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(item) != TINYPY_VALUE_UNICODE && TINYPY_VALUE_KIND(container) == TINYPY_VALUE_STRING) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("'in <string>' requires string as left operand, not "),
            TINYPY_MESSAGE_PART_TYPE_NAME(item)
        };

        tinypy_internal_make_vm_error_parts(TINYPY_VALUE_VM(container), TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return -1;
    }
    if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(item) != TINYPY_VALUE_UNICODE) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("coercing to Unicode: need string or buffer, "),
            TINYPY_MESSAGE_PART_TYPE_NAME(item),
            TINYPY_MESSAGE_PART_LITERAL(" found")
        };

        tinypy_internal_make_vm_error_parts(TINYPY_VALUE_VM(container), TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
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
    if (dispatch_special != 0 && __tinypy_internal_object_overrides_dispatch(container, TINYPY_INTERNAL_DISPATCH_BIT(CONTAINS)) != 0) {
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
        /* bytearray_contains reads a byte through PyNumber_AsSsize_t and
           takes any failure for an operand to read as a buffer. */
        if (tinypy_internal_number_as_index(item, TINYPY_ERROR_VALUE, &integer, out_error) == 0) {
            const tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("Type "),
                TINYPY_MESSAGE_PART_TYPE_NAME(item),
                TINYPY_MESSAGE_PART_LITERAL(" doesn't support the buffer API")
            };

            if (out_error != NULL && *out_error != NULL) {
                tinypy_error_release(*out_error);
                *out_error = NULL;
            }
            tinypy_vm_clear_error(vm);
            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            return -1;
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
            /* list_contains and tuplecontains compare the probe first. */
            TINYPY_INCREF(candidate);
            equal = tinypy_compare_bool(item, candidate, TINYPY_COMPARE_EQUAL, out_error);
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
    if (dispatch_special == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object does not support containment", out_error);
        return -1;
    }
    tinypy_value_t *method;
    int32_t found = tinypy_internal_object_lookup_special_key(container, vm->internal_special_contains_key, &method, out_error);
    if (found < 0) {
        return -1;
    }
    if (found > 0) {
        tinypy_value_t *args = tinypy_tuple_from_items(vm, &item, 1U);
        tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);

        TINYPY_DECREF(args);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return -1;
        }
        int32_t truth = tinypy_truth(result, out_error);
        TINYPY_DECREF(result);
        return truth;
    }
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *iterator = tinypy_iter(container, &iteration_error);

    if (iterator == NULL) {
        /* _PySequence_IterSearch reports any failure to iterate the same way. */
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("argument of type '"),
            TINYPY_MESSAGE_PART_TYPE_NAME(container),
            TINYPY_MESSAGE_PART_LITERAL("' is not iterable"),
        };

        if (iteration_error != NULL) {
            tinypy_error_release(iteration_error);
        }
        tinypy_internal_exception_clear_raised(vm);
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return -1;
    }
    for (;;) {
        tinypy_value_t *candidate = tinypy_next(iterator, &iteration_error);

        if (candidate == NULL) {
            break;
        }
        int32_t equal = candidate == item ? 1 : tinypy_compare_bool(item, candidate, TINYPY_COMPARE_EQUAL, out_error);

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
    tinypy_value_t *function = tinypy_internal_type_function_key(receiver, name);

    if (function != NULL) {
        tinypy_value_t *direct = tinypy_internal_call_type_function(function, receiver, &argument, 1U, out_error);
        return direct;
    }
    tinypy_value_t *method;
    int32_t found = tinypy_internal_object_lookup_special_key(receiver, name, &method, out_error);

    /* half_richcompare and half_compare treat any failed lookup on a
       new-style object as NotImplemented; a classic instance only ignores
       AttributeError. */
    if (found < 0) {
        if (missing_is_not_implemented == 0 || TINYPY_VALUE_KIND(receiver) == TINYPY_VALUE_OLD_INSTANCE) {
            return NULL;
        }
        if (out_error != NULL && *out_error != NULL) {
            tinypy_error_release(*out_error);
            *out_error = NULL;
        }
        tinypy_vm_clear_error(vm);
        found = 0;
    }
    if (found == 0) {
        if (missing_is_not_implemented != 0) {
            tinypy_value_t *result = TINYPY_RET_NOT_IMPLEMENTED(vm);
            return result;
        }
        tinypy_internal_object_make_attribute_error_key(receiver, name, out_error);
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
    const uint64_t rich_names = TINYPY_INTERNAL_DISPATCH_BIT(LT) | TINYPY_INTERNAL_DISPATCH_BIT(LE) | TINYPY_INTERNAL_DISPATCH_BIT(EQ) | TINYPY_INTERNAL_DISPATCH_BIT(NE) | TINYPY_INTERNAL_DISPATCH_BIT(GT) | TINYPY_INTERNAL_DISPATCH_BIT(GE);

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t result = __tinypy_internal_object_overrides_dispatch(value, rich_names);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The rich comparison specials follow the order of the operations. */
typedef char tinypy_comparison_dispatch_follows_operations_t[
    TINYPY_INTERNAL_DISPATCH_LE - TINYPY_INTERNAL_DISPATCH_LT == (int32_t)TINYPY_COMPARE_LESS_EQUAL
    && TINYPY_INTERNAL_DISPATCH_EQ - TINYPY_INTERNAL_DISPATCH_LT == (int32_t)TINYPY_COMPARE_EQUAL
    && TINYPY_INTERNAL_DISPATCH_NE - TINYPY_INTERNAL_DISPATCH_LT == (int32_t)TINYPY_COMPARE_NOT_EQUAL
    && TINYPY_INTERNAL_DISPATCH_GT - TINYPY_INTERNAL_DISPATCH_LT == (int32_t)TINYPY_COMPARE_GREATER
    && TINYPY_INTERNAL_DISPATCH_GE - TINYPY_INTERNAL_DISPATCH_LT == (int32_t)TINYPY_COMPARE_GREATER_EQUAL ? 1 : -1];
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_try_half_rich(tinypy_value_t *receiver, tinypy_value_t *argument, tinypy_compare_operation_e operation, tinypy_bool_t *out_handled, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(receiver);
    tinypy_internal_dispatch_e special = (tinypy_internal_dispatch_e)(TINYPY_INTERNAL_DISPATCH_LT + (int32_t)operation);
    tinypy_value_t *name = tinypy_internal_dispatch_key(vm, special);
    tinypy_bool_t not_implemented;

    /* half_richcompare reaches a classic __getattr__; a missing method is
       NotImplemented. */
    if (TINYPY_VALUE_KIND(receiver) != TINYPY_VALUE_OLD_INSTANCE && __tinypy_internal_object_defines_dispatch(receiver, TINYPY_INTERNAL_DISPATCH_MASK(special)) == 0) {
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
    if (TINYPY_VALUE_KIND(left) != TINYPY_VALUE_OLD_INSTANCE && __tinypy_internal_object_overrides_dispatch(left, TINYPY_INTERNAL_DISPATCH_BIT(CMP)) == 0) {
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
static tinypy_bool_t __tinypy_comparison_try_three_way_pair(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_bool_t *out_ordered, tinypy_error_t **out_error) {
    int64_t order = 0;
    tinypy_bool_t handled = TINYPY_FALSE;

    *out_ordered = TINYPY_FALSE;
    /* instance_compare coerces the pair the way PyNumber_CoerceEx does. */
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_OLD_INSTANCE || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *first = left;
        tinypy_value_t *second = right;
        int32_t status = tinypy_internal_number_coerce(&first, &second, out_error);
        tinypy_bool_t success = TINYPY_TRUE;

        if (status < 0) {
            return TINYPY_FALSE;
        }
        if (status > 0) {
            first = TINYPY_RET(left);
            second = TINYPY_RET(right);
        }
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
        TINYPY_DECREF(second);
        TINYPY_DECREF(first);
        return success;
    }
    if (__tinypy_comparison_try_three_way(left, right, TINYPY_FALSE, &order, out_ordered, &handled, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (*out_ordered == 0 && __tinypy_comparison_try_three_way(right, left, TINYPY_TRUE, &order, out_ordered, &handled, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (*out_ordered == 0 && (__tinypy_internal_object_overrides_dispatch(left, TINYPY_INTERNAL_DISPATCH_BIT(CMP)) != 0 || __tinypy_internal_object_overrides_dispatch(right, TINYPY_INTERNAL_DISPATCH_BIT(CMP)) != 0)) {
        order = left == right ? 0 : ((uintptr_t)left < (uintptr_t)right ? -1 : 1);
        *out_ordered = TINYPY_TRUE;
    }
    *out_order = (int32_t)order;
    if (*out_ordered == 0) {
        tinypy_bool_t coerced = __tinypy_comparison_try_coerced(left, right, out_order, out_ordered, out_error);
        return coerced;
    }
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
static int32_t __tinypy_comparison_order_holds(tinypy_compare_operation_e operation, int32_t order) {
    switch (operation) {
    case TINYPY_COMPARE_LESS:
        return order < 0 ? 1 : 0;
    case TINYPY_COMPARE_LESS_EQUAL:
        return order <= 0 ? 1 : 0;
    case TINYPY_COMPARE_EQUAL:
        return order == 0 ? 1 : 0;
    case TINYPY_COMPARE_NOT_EQUAL:
        return order != 0 ? 1 : 0;
    case TINYPY_COMPARE_GREATER:
        return order > 0 ? 1 : 0;
    default:
        return order >= 0 ? 1 : 0;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_comparison_order_value(tinypy_vm_t *vm, tinypy_compare_operation_e operation, int32_t order) {
    int32_t comparison = __tinypy_comparison_order_holds(operation, order);
    tinypy_value_t *result = tinypy_bool_from_i32(vm, comparison);

    return result;
}
//////////////////////////////////////////////////////////////////////////
typedef char tinypy_comparison_scalars_precede_float_t[TINYPY_VALUE_INTEGER < TINYPY_VALUE_FLOAT && TINYPY_VALUE_STRING < TINYPY_VALUE_FLOAT ? 1 : -1];
//////////////////////////////////////////////////////////////////////////
/* int_richcompare, float_richcompare and string_richcompare decide a rich
   comparison of exact ints, floats and strs, ints and floats also mixed,
   without any dispatch; -1 leaves every other pair to the general protocol.
   Their kinds all precede TINYPY_VALUE_FLOAT's, which passes over containers
   and objects at once. */
static int32_t __tinypy_comparison_exact_scalar(const tinypy_value_t *left, const tinypy_value_t *right, tinypy_compare_operation_e operation) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    const tinypy_type_t *integer_type = &vm->types[TINYPY_VALUE_INTEGER];
    const tinypy_type_t *float_type = &vm->types[TINYPY_VALUE_FLOAT];
    double left_number;
    double right_number;
    int32_t order;

    if (operation > TINYPY_COMPARE_GREATER_EQUAL || TINYPY_VALUE_KIND(left) > TINYPY_VALUE_FLOAT || TINYPY_VALUE_KIND(right) > TINYPY_VALUE_FLOAT) {
        return -1;
    }
    if (left->type == integer_type && right->type == integer_type) {
        int64_t left_integer = TINYPY_INTEGER_VALUE(left);
        int64_t right_integer = TINYPY_INTEGER_VALUE(right);

        order = left_integer < right_integer ? -1 : (left_integer > right_integer ? 1 : 0);
    }
    else if (__tinypy_internal_exact_double(left, &left_number) != 0 && __tinypy_internal_exact_double(right, &right_number) != 0) {
        int32_t holds = __tinypy_internal_compare_doubles(operation, left_number, right_number);
        return holds;
    }
    else if ((left->type == integer_type || left->type == float_type) && (right->type == integer_type || right->type == float_type)) {
        /* An int beyond 2**53 compares with a float through its digits. */
        if (tinypy_internal_numeric_order(left, right, &order) == 0) {
            return operation == TINYPY_COMPARE_NOT_EQUAL ? 1 : 0;
        }
    }
    else if (left->type == &vm->types[TINYPY_VALUE_STRING] && right->type == left->type) {
        if (operation == TINYPY_COMPARE_EQUAL || operation == TINYPY_COMPARE_NOT_EQUAL) {
            size_t size = TINYPY_STRING_SIZE(left);
            tinypy_bool_t equal = size == TINYPY_STRING_SIZE(right) && memcmp(TINYPY_STRING_OBJECT(left)->bytes, TINYPY_STRING_OBJECT(right)->bytes, size) == 0 ? TINYPY_TRUE : TINYPY_FALSE;

            return (equal != 0) == (operation == TINYPY_COMPARE_EQUAL) ? 1 : 0;
        }
        order = tinypy_internal_text_order(left, right);
    }
    else {
        return -1;
    }
    int32_t result = __tinypy_comparison_order_holds(operation, order);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The built-in types with a tp_compare of their own: int and long (one
   family through coercion), dict, set and frozenset, slice and buffer. */
static int32_t __tinypy_comparison_three_way_family(const tinypy_value_t *value) {
    switch (TINYPY_VALUE_KIND(value)) {
    case TINYPY_VALUE_BOOL:
    case TINYPY_VALUE_INTEGER:
    case TINYPY_VALUE_LONG:
        return 1;
    case TINYPY_VALUE_DICT:
        return 2;
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
        return 3;
    case TINYPY_VALUE_SLICE:
        return 4;
    case TINYPY_VALUE_BUFFER:
        return 5;
    default:
        return 0;
    }
}
//////////////////////////////////////////////////////////////////////////
/* try_3way_compare and default_3way_compare once the rich comparison
   declined: only operands sharing a built-in tp_compare compare their values
   (set_nocmp refuses), everything else takes the default ordering. */
static tinypy_bool_t __tinypy_comparison_three_way_fallback(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_error_t **out_error) {
    int32_t family = __tinypy_comparison_three_way_family(left);

    if (family == 0 || family != __tinypy_comparison_three_way_family(right)) {
        *out_order = tinypy_internal_comparison_fallback_order(left, right);
        return TINYPY_TRUE;
    }
    if (family == 3) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(left), TINYPY_ERROR_TYPE, "cannot compare sets using cmp()", out_error);
        return TINYPY_FALSE;
    }
    tinypy_bool_t unordered;
    tinypy_bool_t result = __tinypy_comparison_order(left, right, TINYPY_COMPARE_LESS, out_order, &unordered, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Whether one built-in tp_compare serves both operands: int and long share a
   family through coercion only, as int_compare and long_compare differ. */
static tinypy_bool_t __tinypy_comparison_shares_three_way(const tinypy_value_t *left, const tinypy_value_t *right) {
    int32_t family = __tinypy_comparison_three_way_family(left);
    tinypy_bool_t shared = family != 0 && family == __tinypy_comparison_three_way_family(right) && (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_LONG) == (TINYPY_VALUE_KIND(right) == TINYPY_VALUE_LONG) ? TINYPY_TRUE : TINYPY_FALSE;

    return shared;
}
//////////////////////////////////////////////////////////////////////////
/* try_3way_compare coerces a pair that no tp_compare of its own types
   orders once a __coerce__ takes part, and compares the coerced pair only
   through a tp_compare both of its types share. */
static tinypy_bool_t __tinypy_comparison_try_coerced(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_bool_t *out_ordered, tinypy_error_t **out_error) {
    const uint64_t coerce = TINYPY_INTERNAL_DISPATCH_BIT(COERCE);
    const uint64_t compare = TINYPY_INTERNAL_DISPATCH_BIT(CMP);

    if (__tinypy_comparison_shares_three_way(left, right) != 0 || (__tinypy_internal_object_overrides_dispatch(left, coerce) == 0 && __tinypy_internal_object_overrides_dispatch(right, coerce) == 0)) {
        return TINYPY_TRUE;
    }
    tinypy_value_t *first = left;
    tinypy_value_t *second = right;
    int32_t status = tinypy_internal_number_coerce(&first, &second, out_error);

    if (status != 0) {
        return status > 0 ? TINYPY_TRUE : TINYPY_FALSE;
    }
    tinypy_bool_t success = TINYPY_TRUE;
    tinypy_bool_t classic = TINYPY_VALUE_KIND(first) == TINYPY_VALUE_OLD_INSTANCE && TINYPY_VALUE_KIND(second) == TINYPY_VALUE_OLD_INSTANCE ? TINYPY_TRUE : TINYPY_FALSE;

    if (__tinypy_comparison_shares_three_way(first, second) != 0) {
        success = __tinypy_comparison_three_way_fallback(first, second, out_order, out_error);
        *out_ordered = success;
    }
    else if (classic != 0 || (__tinypy_internal_object_overrides_dispatch(first, compare) != 0 && __tinypy_internal_object_overrides_dispatch(second, compare) != 0)) {
        success = __tinypy_comparison_try_three_way_pair(first, second, out_order, out_ordered, out_error);
    }
    TINYPY_DECREF(second);
    TINYPY_DECREF(first);
    return success;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_comparison_try_special(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_bool_t *out_handled, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    int32_t order = 0;
    tinypy_bool_t ordered = TINYPY_FALSE;

    *out_handled = TINYPY_FALSE;
    /* Operands of one type ask its rich comparison and then its tp_compare
       only, the cheap path of PyObject_RichCompare. */
    if (left->type == right->type && TINYPY_VALUE_KIND(left) != TINYPY_VALUE_OLD_INSTANCE) {
        if (__tinypy_comparison_try_rich_slot(left, right, operation, out_handled, out_value, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (*out_handled != 0) {
            return TINYPY_TRUE;
        }
        if (__tinypy_internal_object_overrides_dispatch(left, TINYPY_INTERNAL_DISPATCH_BIT(CMP)) != 0) {
            if (__tinypy_comparison_try_three_way_pair(left, right, &order, &ordered, out_error) == 0) {
                return TINYPY_FALSE;
            }
            *out_handled = ordered;
        }
        else if (__tinypy_comparison_three_way_family(left) != 0) {
            if (__tinypy_comparison_three_way_fallback(left, right, &order, out_error) == 0) {
                return TINYPY_FALSE;
            }
            ordered = TINYPY_TRUE;
            *out_handled = TINYPY_TRUE;
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
        /* Rich comparisons in Python declined: no built-in value ordering
           applies but the shared tp_compare of try_3way_compare. */
        if (ordered == 0 && (__tinypy_comparison_has_python_rich_slot(left) != 0 || __tinypy_comparison_has_python_rich_slot(right) != 0)) {
            if (__tinypy_comparison_three_way_fallback(left, right, &order, out_error) == 0) {
                return TINYPY_FALSE;
            }
            ordered = TINYPY_TRUE;
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
    int32_t exact = __tinypy_comparison_exact_scalar(left, right, operation);
    if (exact >= 0) {
        return exact;
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
    int32_t exact = __tinypy_comparison_exact_scalar(left, right, operation);
    if (exact >= 0) {
        tinypy_value_t *outcome = tinypy_bool_from_i32(TINYPY_VALUE_VM(left), exact);
        return outcome;
    }
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
    if (left->type == right->type && (left->type == &vm->types[TINYPY_VALUE_NATIVE_FUNCTION] || left->type == vm->native_method_wrapper_type)) {
        tinypy_bool_t result = tinypy_internal_native_function_compare_three_way(left, right, out_order, out_error);

        return result;
    }
    if (left->type == right->type) {
        if (kind == TINYPY_VALUE_OLD_INSTANCE || __tinypy_internal_object_overrides_dispatch(left, TINYPY_INTERNAL_DISPATCH_BIT(CMP)) != 0) {
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
    tinypy_bool_t result = __tinypy_comparison_three_way_fallback(left, right, out_order, out_error);
    return result;
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
