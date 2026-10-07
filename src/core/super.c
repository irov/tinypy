#include "tinypy/super.h"

#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_super_new(tinypy_type_t *instance_type, tinypy_type_t *type, tinypy_value_t *object, tinypy_error_t **out_error) {
    tinypy_type_t *object_type = NULL;
    tinypy_value_t *reported_type = NULL;

    tinypy_vm_t *vm = type->vm;
    TINYPY_CLEAR_ERROR(out_error);
    if (object != NULL && TINYPY_VALUE_KIND(object) == TINYPY_VALUE_NONE) {
        object = NULL;
    }
    if (object != NULL) {
        if (TINYPY_VALUE_KIND(object) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)object, type) != 0) {
            object_type = (tinypy_type_t *)object;
        }
        else if (tinypy_type_is_subtype(object->type, type) != 0) {
            object_type = object->type;
        }
        else {
            tinypy_error_t *lookup_error = NULL;

            reported_type = tinypy_object_get_attr(object, "__class__", 9U, &lookup_error);
            if (reported_type != NULL && TINYPY_VALUE_KIND(reported_type) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)reported_type, type) != 0) {
                object_type = (tinypy_type_t *)reported_type;
            }
            else {
                if (reported_type != NULL) {
                    TINYPY_DECREF(reported_type);
                }
                if (lookup_error != NULL) {
                    tinypy_error_release(lookup_error);
                }
                tinypy_internal_exception_clear_raised(vm);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "super(type, obj): obj must be an instance or subtype of type", out_error);
                return NULL;
            }
        }
    }
    tinypy_super_object_t *super_value = (tinypy_super_object_t *)tinypy_internal_object_allocate(vm, instance_type, instance_type->basic_size);
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
    if (reported_type != NULL) {
        TINYPY_DECREF(reported_type);
    }
    return &super_value->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_super_new(tinypy_type_t *type, tinypy_value_t *object, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_super_new(&type->vm->types[TINYPY_VALUE_SUPER], type, object, out_error);
    return return_value_1;
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
    tinypy_bool_t found_type = TINYPY_FALSE;
    tinypy_value_type_e name_kind = TINYPY_VALUE_KIND(name);

    if (name_kind == TINYPY_VALUE_STRING || name_kind == TINYPY_VALUE_UNICODE) {
        name_bytes = (const char *)TINYPY_TEXT_BYTES(name);
        name_size = TINYPY_TEXT_BYTE_SIZE(name);
        if (name_size == 13U && memcmp(name_bytes, "__thisclass__", 13U) == 0) {
            TINYPY_INCREF(&super_value->type->base.base);
            return &super_value->type->base.base;
        }
        if (name_size == 8U && memcmp(name_bytes, "__self__", 8U) == 0) {
            if (super_value->object != NULL) {
                TINYPY_INCREF(super_value->object);
                return super_value->object;
            }
            tinypy_value_t *return_value_1 = tinypy_none_get(vm);
            return return_value_1;
        }
        if (name_size == 14U && memcmp(name_bytes, "__self_class__", 14U) == 0) {
            if (super_value->object_type != NULL) {
                TINYPY_INCREF(&super_value->object_type->base.base);
                return &super_value->object_type->base.base;
            }
            tinypy_value_t *return_value_2 = tinypy_none_get(vm);
            return return_value_2;
        }
    }
    if (super_value->object_type == NULL) {
        tinypy_value_t *super_attribute = tinypy_internal_type_lookup_key(vm, value->type, name);

        if (super_attribute != NULL) {
            tinypy_value_t *return_value_3 = tinypy_internal_descriptor_get_value(vm, super_attribute, value, value->type, out_error);
            return return_value_3;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "unbound super has no requested attribute", out_error);
        return NULL;
    }
    mro_size = tinypy_type_mro_size(super_value->object_type);
    for (index = 0U; index < mro_size; ++index) {
        tinypy_value_t *entry = tinypy_internal_type_mro_value_at(super_value->object_type, index);

        if (found_type == 0) {
            if (entry == &super_value->type->base.base) {
                found_type = 1;
            }
            continue;
        }
        tinypy_value_t *attribute = tinypy_dict_get_optional(tinypy_internal_type_mro_entry_dict(entry), name);

        if (attribute != NULL) {
            tinypy_value_t *instance = super_value->object == &super_value->object_type->base.base ? NULL : super_value->object;
            tinypy_value_t *return_value_2 = tinypy_internal_descriptor_get_value(vm, attribute, instance, super_value->object_type, out_error);
            return return_value_2;
        }
    }
    tinypy_value_t *super_attribute = tinypy_internal_type_lookup_key(vm, value->type, name);
    if (super_attribute != NULL) {
        tinypy_value_t *return_value_4 = tinypy_internal_descriptor_get_value(vm, super_attribute, value, value->type, out_error);
        return return_value_4;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "super object has no requested attribute", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_super_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < 1U || count > 2U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "super requires one or two arguments", out_error);
        return NULL;
    }
    tinypy_value_t *requested_type = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(requested_type) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "super first argument is not a type", out_error);
        return NULL;
    }
    tinypy_value_t *object = count == 2U ? TINYPY_TUPLE_GET(args, 1U) : NULL;
    tinypy_value_t *return_value_1 = __tinypy_super_new(type, (tinypy_type_t *)requested_type, object, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_super_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    tinypy_super_object_t *super_value = TINYPY_SUPER_OBJECT(descriptor);

    (void)owner;
    TINYPY_CLEAR_ERROR(out_error);
    if (instance == NULL || super_value->object != NULL) {
        TINYPY_INCREF(descriptor);
        return descriptor;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor);
    if (descriptor->type != &vm->types[TINYPY_VALUE_SUPER]) {
        tinypy_value_t *items[2] = {&super_value->type->base.base, instance};
        tinypy_value_t *args = tinypy_tuple_from_items(vm, items, 2U);
        tinypy_value_t *result = tinypy_call(&descriptor->type->base.base, args, NULL, out_error);

        TINYPY_DECREF(args);
        return result;
    }
    tinypy_value_t *return_value_1 = __tinypy_super_new(descriptor->type, super_value->type, instance, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_super_get_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *instance;

    (void)user_data;
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < 2U || count > 3U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_SUPER) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "super.__get__ received invalid arguments", out_error);
        return NULL;
    }
    instance = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(instance) == TINYPY_VALUE_NONE) {
        instance = NULL;
    }
    if (instance == NULL && (count == 2U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 2U)) == TINYPY_VALUE_NONE)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__get__(None, None) is invalid", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_super_descriptor_get(TINYPY_TUPLE_GET(args, 0U), instance, instance != NULL ? instance->type : NULL, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_super_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count = TINYPY_TUPLE_SIZE(args);

    (void)user_data;
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < 2U || count > 3U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_SUPER || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 1U)) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "super.__init__ received invalid arguments", out_error);
        return NULL;
    }
    tinypy_super_object_t *self = TINYPY_SUPER_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    tinypy_value_t *replacement = __tinypy_super_new(self->base.type, (tinypy_type_t *)TINYPY_TUPLE_GET(args, 1U), count == 3U ? TINYPY_TUPLE_GET(args, 2U) : NULL, out_error);

    if (replacement == NULL) {
        return NULL;
    }
    tinypy_super_object_t *updated = TINYPY_SUPER_OBJECT(replacement);
    tinypy_type_t *previous_type = self->type;
    tinypy_value_t *previous_object = self->object;
    tinypy_type_t *previous_object_type = self->object_type;

    self->type = updated->type;
    self->object = updated->object;
    self->object_type = updated->object_type;
    updated->type = previous_type;
    updated->object = previous_object;
    updated->object_type = previous_object_type;
    TINYPY_DECREF(replacement);
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_super_type(tinypy_vm_t *vm) {
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_SUPER];
    tinypy_value_t *get = tinypy_native_function_new(vm, "__get__", 7U, __tinypy_super_get_method, NULL, NULL);
    tinypy_value_t *init = tinypy_native_function_new(vm, "__init__", 8U, __tinypy_super_init_method, NULL, NULL);

    type->descriptor_get = tinypy_internal_super_descriptor_get;
    tinypy_internal_constructor_add_builtin_new(type);
    tinypy_type_set_attr(type, "__get__", 7U, get);
    tinypy_type_set_attr(type, "__init__", 8U, init);
    TINYPY_DECREF(get);
    TINYPY_DECREF(init);
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_super_type(const tinypy_value_t *super_value) {
    const tinypy_type_t *return_value_1 = TINYPY_SUPER_OBJECT((tinypy_value_t *)super_value)->type;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_super_object(const tinypy_value_t *super_value) {
    tinypy_value_t *return_value_1 = TINYPY_SUPER_OBJECT((tinypy_value_t *)super_value)->object;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_super_object_type(const tinypy_value_t *super_value) {
    const tinypy_type_t *return_value_1 = TINYPY_SUPER_OBJECT((tinypy_value_t *)super_value)->object_type;
    return return_value_1;
}
