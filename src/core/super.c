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

            reported_type = tinypy_object_get_attr_value(object, vm->internal_special_class_key, &lookup_error);
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
/* super_getattro: search the MRO after the starting type, except for
   __class__ and unbound super, then fall back to the super object itself. */
tinypy_value_t *tinypy_internal_super_get_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_super_object_t *super_value = TINYPY_SUPER_OBJECT(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    if (super_value->object_type != NULL && TINYPY_NAME_EQ(name, vm->internal_special_class_key) == TINYPY_FALSE) {
        size_t mro_size = tinypy_type_mro_size(super_value->object_type);
        tinypy_bool_t found_type = TINYPY_FALSE;

        for (size_t index = 0U; index < mro_size; ++index) {
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
                tinypy_value_t *return_value_1 = tinypy_internal_descriptor_get_value(vm, attribute, instance, super_value->object_type, out_error);
                return return_value_1;
            }
        }
    }
    tinypy_value_t *super_attribute = tinypy_internal_type_lookup_key(vm, value->type, name);
    if (super_attribute != NULL) {
        tinypy_value_t *return_value_2 = tinypy_internal_descriptor_get_value(vm, super_attribute, value, value->type, out_error);
        return return_value_2;
    }
    tinypy_internal_object_make_attribute_error_key(value, name, out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* super_init parses "O!|O:super" after rejecting keyword arguments. */
static tinypy_bool_t __tinypy_super_arguments(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "super does not take keyword arguments", out_error);
        return TINYPY_FALSE;
    }
    if (count < 1U || count > 2U) {
        tinypy_internal_make_arity_error(vm, "super", 5U, count, 1U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(items[0]) != TINYPY_VALUE_TYPE) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("super() argument 1 must be type, not "),
            TINYPY_MESSAGE_PART_TYPE_NAME(items[0]),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_super_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *const *items = tinypy_internal_tuple_items(args);

    if (__tinypy_super_arguments(vm, items, count, kwargs, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_super_new(type, (tinypy_type_t *)items[0], count == 2U ? items[1] : NULL, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_super_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    tinypy_super_object_t *super_value = TINYPY_SUPER_OBJECT(descriptor);

    (void)owner;
    TINYPY_CLEAR_ERROR(out_error);
    if (instance == NULL || super_value->object != NULL) {
        return TINYPY_RET(descriptor);
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == TINYPY_FALSE) {
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
    if (count == 0U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_SUPER) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "super.__init__ requires a super object", out_error);
        return NULL;
    }
    tinypy_value_t *const *items = tinypy_internal_tuple_items(args) + 1U;
    if (__tinypy_super_arguments(vm, items, count - 1U, kwargs, out_error) == 0) {
        return NULL;
    }
    tinypy_super_object_t *self = TINYPY_SUPER_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    tinypy_value_t *replacement = __tinypy_super_new(self->base.type, (tinypy_type_t *)items[0], count == 3U ? items[1] : NULL, out_error);

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
    tinypy_value_t *result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_super_type(tinypy_vm_t *vm) {
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_SUPER];
    tinypy_internal_type_add_method(type, vm->internal_special_get_key, __tinypy_super_get_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_init_key, __tinypy_super_init_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);

    type->descriptor_get = tinypy_internal_super_descriptor_get;
    tinypy_internal_constructor_add_builtin_new(type);
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
