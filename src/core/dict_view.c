#include "tinypy/dict_view.h"

#include "internal.h"

#include <assert.h>

tinypy_value_t *tinypy_dict_view_new(tinypy_value_t *dict, tinypy_dict_view_kind_e kind)
{
    tinypy_vm_t *vm;
    tinypy_value_type_e value_kind;
    tinypy_dict_view_object_t *view;

    assert(dict != NULL);
    vm = tinypy_internal_value_vm(dict);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_kind(dict) == TINYPY_VALUE_DICT);
    assert(kind >= TINYPY_DICT_VIEW_KEYS && kind <= TINYPY_DICT_VIEW_ITEMS);
    value_kind = kind == TINYPY_DICT_VIEW_KEYS ? TINYPY_VALUE_DICT_KEYS : (kind == TINYPY_DICT_VIEW_VALUES ? TINYPY_VALUE_DICT_VALUES : TINYPY_VALUE_DICT_ITEMS);
    view = (tinypy_dict_view_object_t *)tinypy_internal_value_allocate(vm, value_kind, sizeof(*view));
    view->dict = dict;
    view->kind = kind;
    tinypy_retain(dict);
    return &view->base;
}

void tinypy_internal_dict_view_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data)
{
    visit(TINYPY_DICT_VIEW_OBJECT(value)->dict, user_data);
}

ptrdiff_t tinypy_internal_dict_view_length(tinypy_value_t *value, tinypy_error_t **out_error)
{
    tinypy_internal_clear_error(out_error);
    return (ptrdiff_t)tinypy_dict_size(TINYPY_DICT_VIEW_OBJECT(value)->dict);
}

tinypy_value_t *tinypy_internal_dict_view_iter(tinypy_value_t *value, tinypy_error_t **out_error)
{
    tinypy_internal_clear_error(out_error);
    return tinypy_internal_dict_iterator_new(TINYPY_DICT_VIEW_OBJECT(value)->dict, (int32_t)TINYPY_DICT_VIEW_OBJECT(value)->kind);
}

int32_t tinypy_internal_dict_view_contains(tinypy_value_t *value, tinypy_value_t *item, tinypy_error_t **out_error)
{
    tinypy_dict_view_object_t *view = TINYPY_DICT_VIEW_OBJECT(value);
    tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(view->dict);
    size_t index;

    tinypy_internal_clear_error(out_error);
    if (view->kind == TINYPY_DICT_VIEW_KEYS) return tinypy_dict_contains(view->dict, item);
    if (view->kind == TINYPY_DICT_VIEW_ITEMS) {
        tinypy_value_t *key;

        if (tinypy_internal_value_kind(item) != TINYPY_VALUE_TUPLE || tinypy_tuple_size(item) != 2U) return INT32_C(0);
        key = tinypy_tuple_get(item, 0U);
        return tinypy_dict_contains(view->dict, key) != 0 && tinypy_equal(tinypy_dict_get(view->dict, key), tinypy_tuple_get(item, 1U)) != 0 ? INT32_C(1) : INT32_C(0);
    }
    for (index = 0U; index <= dict->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &dict->table[index];

        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE && tinypy_equal(entry->value, item) != 0) return INT32_C(1);
    }
    return INT32_C(0);
}
