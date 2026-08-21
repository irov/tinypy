#include "tinypy/cell.h"

#include "internal.h"

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_cell_new(tinypy_vm_t *vm, tinypy_value_t *content) {
    tinypy_cell_object_t *cell = (tinypy_cell_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_CELL, sizeof(*cell));
    cell->content = content;
    if (content != NULL) {
        TINYPY_INCREF(content);
    }
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_cell_set(vm, &cell->base, content);
#endif
    return &cell->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cell_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_cell_object_t *cell = TINYPY_CELL_OBJECT(value);

    if (cell->content != NULL) {
        visit(cell->content, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_cell_get(const tinypy_value_t *cell) {
    tinypy_value_t *return_value_1 = TINYPY_CELL_OBJECT((tinypy_value_t *)cell)->content;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_cell_set(tinypy_value_t *cell_value, tinypy_value_t *content) {
    tinypy_cell_object_t *cell = TINYPY_CELL_OBJECT(cell_value);
    tinypy_value_t *previous = cell->content;
    if (content != NULL) {
        TINYPY_INCREF(content);
    }
    cell->content = content;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_cell_set(TINYPY_VALUE_VM(cell_value), cell_value, content);
#endif
    if (previous != NULL) {
        TINYPY_DECREF(previous);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cell_order(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_error_t **out_error) {
    tinypy_value_t *left_content = TINYPY_CELL_OBJECT(left)->content;
    tinypy_value_t *right_content = TINYPY_CELL_OBJECT(right)->content;
    int32_t equal;
    int32_t less;

    if (left_content == NULL || right_content == NULL) {
        *out_order = left_content == right_content ? INT32_C(0) : (left_content == NULL ? -INT32_C(1) : INT32_C(1));
        return TINYPY_TRUE;
    }
    equal = tinypy_compare_bool(left_content, right_content, TINYPY_COMPARE_EQUAL, out_error);
    if (equal < 0) {
        return TINYPY_FALSE;
    }
    if (equal != 0) {
        *out_order = INT32_C(0);
        return TINYPY_TRUE;
    }
    less = tinypy_compare_bool(left_content, right_content, TINYPY_COMPARE_LESS, out_error);
    if (less < 0) {
        return TINYPY_FALSE;
    }
    *out_order = less != 0 ? -INT32_C(1) : INT32_C(1);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_cell_compare(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    int32_t order;
    tinypy_bool_t result;

    if (TINYPY_VALUE_KIND(right) != TINYPY_VALUE_CELL) {
        tinypy_value_t *return_value_1 = tinypy_not_implemented_get(vm);
        return return_value_1;
    }
    if (__tinypy_cell_order(left, right, &order, out_error) == 0) {
        return NULL;
    }
    switch (operation) {
    case TINYPY_COMPARE_LESS:
        result = order < 0;
        break;
    case TINYPY_COMPARE_LESS_EQUAL:
        result = order <= 0;
        break;
    case TINYPY_COMPARE_EQUAL:
        result = order == 0;
        break;
    case TINYPY_COMPARE_NOT_EQUAL:
        result = order != 0;
        break;
    case TINYPY_COMPARE_GREATER:
        result = order > 0;
        break;
    case TINYPY_COMPARE_GREATER_EQUAL:
        result = order >= 0;
        break;
    default:
        return tinypy_not_implemented_get(vm);
    }
    tinypy_value_t *return_value_3 = tinypy_bool_from_i32(vm, result);
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
tinypy_hash_t tinypy_internal_cell_hash(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_TYPE, "unhashable type: 'cell'", out_error);
    return (tinypy_hash_t)0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_cell_cmp_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t order;

    (void)user_data;
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 2U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_CELL || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 1U)) != TINYPY_VALUE_CELL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cell.__cmp__ requires two cells", out_error);
        return NULL;
    }
    if (__tinypy_cell_order(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), &order, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, order);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_cell_type(tinypy_vm_t *vm) {
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_CELL];
    tinypy_value_t *cmp = tinypy_native_function_new(vm, "__cmp__", 7U, __tinypy_cell_cmp_method, NULL, NULL);

    type->rich_compare = tinypy_internal_cell_compare;
    type->hash = tinypy_internal_cell_hash;
    tinypy_type_set_attr(type, "__cmp__", 7U, cmp);
    tinypy_type_set_attr(type, "__hash__", 8U, &vm->none_object.base);
    TINYPY_DECREF(cmp);
}
