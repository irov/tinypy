#include "tinypy/slice.h"

#include "internal.h"

#include "assertion.h"

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_slice_new(tinypy_vm_t *vm, tinypy_value_t *start, tinypy_value_t *stop, tinypy_value_t *step) {
    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    TINYPY_ASSERT(start == NULL || tinypy_internal_value_belongs_to(vm, start));
    TINYPY_ASSERT(stop == NULL || tinypy_internal_value_belongs_to(vm, stop));
    TINYPY_ASSERT(step == NULL || tinypy_internal_value_belongs_to(vm, step));
    tinypy_slice_object_t *slice = (tinypy_slice_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_SLICE, sizeof(*slice));
    slice->start = start != NULL ? start : &vm->none_object.base;
    slice->stop = stop != NULL ? stop : &vm->none_object.base;
    slice->step = step != NULL ? step : &vm->none_object.base;
    TINYPY_INCREF(slice->start);
    TINYPY_INCREF(slice->stop);
    TINYPY_INCREF(slice->step);
    return &slice->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_slice_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_slice_object_t *slice = TINYPY_SLICE_OBJECT(value);

    visit(slice->start, user_data);
    visit(slice->stop, user_data);
    visit(slice->step, user_data);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_slice_start(const tinypy_value_t *slice) {
    TINYPY_ASSERT(slice != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(slice)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(slice) == TINYPY_VALUE_SLICE);
    return TINYPY_SLICE_OBJECT((tinypy_value_t *)slice)->start;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_slice_stop(const tinypy_value_t *slice) {
    TINYPY_ASSERT(slice != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(slice)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(slice) == TINYPY_VALUE_SLICE);
    return TINYPY_SLICE_OBJECT((tinypy_value_t *)slice)->stop;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_slice_step(const tinypy_value_t *slice) {
    TINYPY_ASSERT(slice != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(slice)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(slice) == TINYPY_VALUE_SLICE);
    return TINYPY_SLICE_OBJECT((tinypy_value_t *)slice)->step;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_slice_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t argument_count = TINYPY_TUPLE_SIZE(args);

    TINYPY_CLEAR_ERROR(out_error);
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice does not accept keyword arguments", out_error);
        return NULL;
    }
    if (argument_count == 1U) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        return tinypy_slice_new(vm, NULL, item, NULL);
    }
    if (argument_count == 2U) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
        return tinypy_slice_new(vm, item, item_2, NULL);
    }
    if (argument_count == 3U) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
        tinypy_value_t *item_3 = TINYPY_TUPLE_GET(args, 2U);
        return tinypy_slice_new(vm, item, item_2, item_3);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice requires one to three arguments", out_error);
    return NULL;
}
