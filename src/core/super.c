#include "tinypy/super.h"

#include "internal.h"

#include <assert.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_super_new(tinypy_type_t *type, tinypy_value_t *object, tinypy_error_t **out_error) {
    tinypy_vm_t *vm;
    tinypy_type_t *object_type = NULL;
    tinypy_super_object_t *super_value;

    assert(type != NULL);
    vm = type->vm;
    assert(tinypy_internal_vm_valid(vm));
    assert(object == NULL || tinypy_internal_value_belongs_to(vm, object));
    TINYPY_CLEAR_ERROR(out_error);
    if (object != NULL) {
        if (TINYPY_VALUE_KIND(object) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)object, type) != 0) {
            object_type = (tinypy_type_t *)object;
        }
        else if (tinypy_type_is_subtype(object->type, type) != 0) {
            object_type = object->type;
        }
        else {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "super object is not an instance or subtype of the requested type", out_error);
            return NULL;
        }
    }
    super_value = (tinypy_super_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_SUPER, sizeof(*super_value));
    super_value->type = type;
    super_value->object = object;
    super_value->object_type = object_type;
    TINYPY_INCREF(&type->base.base);
    if (object != NULL) {
        TINYPY_INCREF(object);
    }
    if (object_type != NULL) {
        TINYPY_INCREF(&object_type->base.base);
    }
    return &super_value->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_super_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_super_object_t *super_value = TINYPY_SUPER_OBJECT(value);

    visit(&super_value->type->base.base, user_data);
    if (super_value->object != NULL) {
        visit(super_value->object, user_data);
    }
    if (super_value->object_type != NULL) {
        visit(&super_value->object_type->base.base, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_super_get_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_super_object_t *super_value = TINYPY_SUPER_OBJECT(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    const char *name_bytes;
    size_t name_size;
    size_t mro_size;
    size_t index;
    int found_type = 0;

    assert(TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING);
    name_bytes = (const char *)tinypy_string_view(name, &name_size);
    if (name_size == 13U && memcmp(name_bytes, "__thisclass__", 13U) == 0) {
        TINYPY_INCREF(&super_value->type->base.base);
        return &super_value->type->base.base;
    }
    if (name_size == 8U && memcmp(name_bytes, "__self__", 8U) == 0) {
        if (super_value->object != NULL) {
            TINYPY_INCREF(super_value->object);
            return super_value->object;
        }
        return tinypy_none_get(vm);
    }
    if (super_value->object_type == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "unbound super has no requested attribute", out_error);
        return NULL;
    }
    mro_size = tinypy_type_mro_size(super_value->object_type);
    for (index = 0U; index < mro_size; ++index) {
        tinypy_type_t *mro_type = (tinypy_type_t *)tinypy_type_mro_at(super_value->object_type, index);

        if (found_type == 0) {
            if (mro_type == super_value->type) {
                found_type = 1;
            }
            continue;
        }
        if (tinypy_dict_contains(mro_type->dict, name) != 0) {
            tinypy_value_t *attribute = tinypy_dict_get(mro_type->dict, name);

            return tinypy_internal_descriptor_get_value(vm, attribute, super_value->object, super_value->object_type, out_error);
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "super object has no requested attribute", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_super_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *requested_type;

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < 1U || count > 2U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "super requires one or two arguments", out_error);
        return NULL;
    }
    requested_type = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(requested_type) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "super first argument is not a type", out_error);
        return NULL;
    }
    tinypy_value_t *object = count == 2U ? TINYPY_TUPLE_GET(args, 1U) : NULL;
    return tinypy_super_new((tinypy_type_t *)requested_type, object, out_error);
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_super_type(const tinypy_value_t *super_value) {
    assert(super_value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(super_value)));
    assert(TINYPY_VALUE_KIND(super_value) == TINYPY_VALUE_SUPER);
    return TINYPY_SUPER_OBJECT((tinypy_value_t *)super_value)->type;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_super_object(const tinypy_value_t *super_value) {
    assert(super_value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(super_value)));
    assert(TINYPY_VALUE_KIND(super_value) == TINYPY_VALUE_SUPER);
    return TINYPY_SUPER_OBJECT((tinypy_value_t *)super_value)->object;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_super_object_type(const tinypy_value_t *super_value) {
    assert(super_value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(super_value)));
    assert(TINYPY_VALUE_KIND(super_value) == TINYPY_VALUE_SUPER);
    return TINYPY_SUPER_OBJECT((tinypy_value_t *)super_value)->object_type;
}
