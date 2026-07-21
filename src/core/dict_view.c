#include "tinypy/dict_view.h"

#include "internal.h"

#include <assert.h>

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_dict_view_new(tinypy_value_t *dict, tinypy_dict_view_kind_e kind) {
    tinypy_vm_t *vm;
    tinypy_value_type_e value_kind;
    tinypy_dict_view_object_t *view;

    assert(dict != NULL);
    vm = TINYPY_VALUE_VM(dict);
    assert(tinypy_internal_vm_valid(vm));
    assert(TINYPY_VALUE_KIND(dict) == TINYPY_VALUE_DICT);
    assert(kind >= TINYPY_DICT_VIEW_KEYS && kind <= TINYPY_DICT_VIEW_ITEMS);
    value_kind = kind == TINYPY_DICT_VIEW_KEYS ? TINYPY_VALUE_DICT_KEYS : (kind == TINYPY_DICT_VIEW_VALUES ? TINYPY_VALUE_DICT_VALUES : TINYPY_VALUE_DICT_ITEMS);
    view = (tinypy_dict_view_object_t *)tinypy_internal_value_allocate(vm, value_kind, sizeof(*view));
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
    return (ptrdiff_t)TINYPY_DICT_SIZE(TINYPY_DICT_VIEW_OBJECT(value)->dict);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_view_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    return tinypy_internal_dict_iterator_new(TINYPY_DICT_VIEW_OBJECT(value)->dict, (int32_t)TINYPY_DICT_VIEW_OBJECT(value)->kind);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_dict_view_contains(tinypy_value_t *value, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_dict_view_object_t *view = TINYPY_DICT_VIEW_OBJECT(value);
    tinypy_dict_entry_t *iterator;
    tinypy_dict_entry_t *iterator_end;

    TINYPY_CLEAR_ERROR(out_error);
    if (view->kind == TINYPY_DICT_VIEW_KEYS) {
        return tinypy_dict_contains(view->dict, item);
    }
    if (view->kind == TINYPY_DICT_VIEW_ITEMS) {
        tinypy_value_t *key;
        tinypy_value_t *dict_value;
        tinypy_value_t *item_value;

        if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(item) != 2U) {
            return INT32_C(0);
        }
        key = TINYPY_TUPLE_GET(item, 0U);
        if (tinypy_dict_contains(view->dict, key) == 0) {
            return INT32_C(0);
        }
        dict_value = tinypy_dict_get(view->dict, key);
        item_value = TINYPY_TUPLE_GET(item, 1U);
        return tinypy_equal(dict_value, item_value) != 0 ? INT32_C(1) : INT32_C(0);
    }
    iterator = TINYPY_DICT_ITERATOR_BEGIN(view->dict);
    iterator_end = TINYPY_DICT_ITERATOR_END(view->dict);
    for (; iterator != iterator_end; ++iterator) {
        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE && tinypy_equal(iterator->value, item) != 0) {
            return INT32_C(1);
        }
    }
    return INT32_C(0);
}
