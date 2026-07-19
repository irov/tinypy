#include "tinypy/slice.h"

#include "internal.h"

#include <assert.h>

static tinypy_slice_object_t *__tinypy_internal_slice_validate(const tinypy_value_t *value)
{
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_SLICE);
    return TINYPY_SLICE_OBJECT((tinypy_value_t *)value);
}

tinypy_value_t *tinypy_slice_new(tinypy_vm_t *vm, tinypy_value_t *start, tinypy_value_t *stop, tinypy_value_t *step)
{
    tinypy_slice_object_t *slice;

    assert(tinypy_internal_vm_valid(vm));
    assert(start == NULL || tinypy_internal_value_belongs_to(vm, start));
    assert(stop == NULL || tinypy_internal_value_belongs_to(vm, stop));
    assert(step == NULL || tinypy_internal_value_belongs_to(vm, step));
    slice = (tinypy_slice_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_SLICE, sizeof(*slice));
    slice->start = start != NULL ? start : &vm->none_object.base;
    slice->stop = stop != NULL ? stop : &vm->none_object.base;
    slice->step = step != NULL ? step : &vm->none_object.base;
    tinypy_retain(slice->start);
    tinypy_retain(slice->stop);
    tinypy_retain(slice->step);
    return &slice->base;
}

void tinypy_internal_slice_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data)
{
    tinypy_slice_object_t *slice = TINYPY_SLICE_OBJECT(value);

    visit(slice->start, user_data);
    visit(slice->stop, user_data);
    visit(slice->step, user_data);
}

tinypy_value_t *tinypy_slice_start(const tinypy_value_t *slice)
{
    return __tinypy_internal_slice_validate(slice)->start;
}

tinypy_value_t *tinypy_slice_stop(const tinypy_value_t *slice)
{
    return __tinypy_internal_slice_validate(slice)->stop;
}

tinypy_value_t *tinypy_slice_step(const tinypy_value_t *slice)
{
    return __tinypy_internal_slice_validate(slice)->step;
}

tinypy_value_t *tinypy_internal_slice_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    size_t argument_count = tinypy_tuple_size(args);

    tinypy_internal_clear_error(out_error);
    if (kwargs != NULL && tinypy_dict_size(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice does not accept keyword arguments", out_error);
        return NULL;
    }
    if (argument_count == 1U) return tinypy_slice_new(vm, NULL, tinypy_tuple_get(args, 0U), NULL);
    if (argument_count == 2U) return tinypy_slice_new(vm, tinypy_tuple_get(args, 0U), tinypy_tuple_get(args, 1U), NULL);
    if (argument_count == 3U) return tinypy_slice_new(vm, tinypy_tuple_get(args, 0U), tinypy_tuple_get(args, 1U), tinypy_tuple_get(args, 2U));
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice requires one to three arguments", out_error);
    return NULL;
}
