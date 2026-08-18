#include "tinypy/dict_view.h"

#include "internal.h"

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_dict_view_new(tinypy_value_t *dict, tinypy_dict_view_kind_e kind) {
    tinypy_value_type_e value_kind;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    value_kind = kind == TINYPY_DICT_VIEW_KEYS ? TINYPY_VALUE_DICT_KEYS : (kind == TINYPY_DICT_VIEW_VALUES ? TINYPY_VALUE_DICT_VALUES : TINYPY_VALUE_DICT_ITEMS);
    tinypy_dict_view_object_t *view = (tinypy_dict_view_object_t *)tinypy_internal_value_allocate(vm, value_kind, sizeof(*view));
    view->dict = dict;
    view->kind = kind;
    TINYPY_INCREF(dict);
    return &view->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_dict_view_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    visit(TINYPY_DICT_VIEW_OBJECT(value)->dict, user_data);
}
//////////////////////////////////////////////////////////////////////////
ptrdiff_t tinypy_internal_dict_view_length(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    ptrdiff_t return_value_1 = (ptrdiff_t)TINYPY_DICT_SIZE(TINYPY_DICT_VIEW_OBJECT(value)->dict);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_view_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *return_value_1 = tinypy_internal_dict_iterator_new(TINYPY_DICT_VIEW_OBJECT(value)->dict, (int32_t)TINYPY_DICT_VIEW_OBJECT(value)->kind);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_dict_view_contains(tinypy_value_t *value, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_dict_view_object_t *view = TINYPY_DICT_VIEW_OBJECT(value);

    TINYPY_CLEAR_ERROR(out_error);
    if (view->kind == TINYPY_DICT_VIEW_KEYS) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_bool_t contains;

        if (tinypy_internal_dict_contains_checked(vm, view->dict, item, &contains, out_error) == 0) {
            return INT32_C(-1);
        }
        return contains != 0 ? INT32_C(1) : INT32_C(0);
    }
    if (view->kind == TINYPY_DICT_VIEW_ITEMS) {
        tinypy_value_t *key;
        tinypy_value_t *dict_value;
        tinypy_value_t *item_value;

        if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(item) != 2U) {
            return INT32_C(0);
        }
        key = TINYPY_TUPLE_GET(item, 0U);
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        if (tinypy_internal_dict_get_optional_checked(vm, view->dict, key, &dict_value, out_error) == 0) {
            return INT32_C(-1);
        }
        if (dict_value == NULL) {
            return INT32_C(0);
        }
        item_value = TINYPY_TUPLE_GET(item, 1U);
        TINYPY_INCREF(dict_value);
        int32_t equal = dict_value == item_value ? 1 : tinypy_compare_bool(dict_value, item_value, TINYPY_COMPARE_EQUAL, out_error);
        TINYPY_DECREF(dict_value);
        return equal;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_dict_entry_t *entries = TINYPY_DICT_ITERATOR_BEGIN(view->dict);
    size_t capacity = TINYPY_DICT_OBJECT(view->dict)->mask + 1U;
    uint64_t version = TINYPY_DICT_OBJECT(view->dict)->mutation_version;
    size_t index;

    for (index = 0U; index < capacity; ++index) {
        if (entries[index].state == TINYPY_DICT_ENTRY_ACTIVE) {
            tinypy_value_t *dict_value = entries[index].value;
            int32_t equal;

            TINYPY_INCREF(dict_value);
            equal = dict_value == item ? 1 : tinypy_compare_bool(dict_value, item, TINYPY_COMPARE_EQUAL, out_error);
            TINYPY_DECREF(dict_value);
            if (equal < 0) {
                return INT32_C(-1);
            }
            if (TINYPY_DICT_OBJECT(view->dict)->mutation_version != version || TINYPY_DICT_OBJECT(view->dict)->table != entries) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "dictionary changed size during iteration", out_error);
                return INT32_C(-1);
            }
            if (equal != 0) {
                return INT32_C(1);
            }
        }
    }
    return INT32_C(0);
}
