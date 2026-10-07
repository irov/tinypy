#include "tinypy/class.h"

#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_class_lookup_key(tinypy_vm_t *vm, tinypy_value_t *class_value, tinypy_value_t *key) {
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT(class_value);
    for (;;) {
        tinypy_value_t *dict = TINYPY_RET(class_object->dict);
        tinypy_value_t *attribute = tinypy_internal_dict_get_optional(vm, dict, key);
        if (class_object->dict != dict) {
            TINYPY_DECREF(dict);
            if (tinypy_vm_has_error(vm) != 0) {
                return NULL;
            }
            continue;
        }
        TINYPY_DECREF(dict);
        if (attribute != NULL || tinypy_vm_has_error(vm) != 0) {
            return attribute;
        }
        tinypy_value_t *bases = TINYPY_RET(class_object->bases);
        tinypy_bool_t changed = TINYPY_FALSE;
        for (size_t index = 0U; index < TINYPY_TUPLE_SIZE(bases); ++index) {
            attribute = tinypy_internal_class_lookup_key(vm, TINYPY_TUPLE_GET(bases, index), key);
            if (class_object->bases != bases) {
                changed = TINYPY_TRUE;
                break;
            }
            if (attribute != NULL || tinypy_vm_has_error(vm) != 0) {
                break;
            }
        }
        TINYPY_DECREF(bases);
        if (changed == 0 || tinypy_vm_has_error(vm) != 0) {
            return attribute;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_lookup_key(tinypy_vm_t *vm, tinypy_value_t *class_value, tinypy_value_t *key) {
    if (tinypy_internal_recursion_check(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded in class lookup", NULL) == 0) {
        return NULL;
    }
    vm->evaluation_depth += 1U;
    tinypy_value_t *result = __tinypy_class_lookup_key(vm, class_value, key);
    vm->evaluation_depth -= 1U;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_class_is_subclass(const tinypy_value_t *class_value, const tinypy_value_t *candidate_base) {
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT((tinypy_value_t *)class_value);
    if (class_value == candidate_base) {
        return TINYPY_TRUE;
    }
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(class_object->bases);
    iterator_end = TINYPY_TUPLE_ITERATOR_END(class_object->bases);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        if (tinypy_class_is_subclass(item, candidate_base) != 0) {
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_class_is_subclass(const tinypy_value_t *class_value, const tinypy_value_t *candidate_base) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_value);
    if (tinypy_internal_recursion_check(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded in subclass check", NULL) == 0) {
        return TINYPY_FALSE;
    }
    vm->evaluation_depth += 1U;
    tinypy_bool_t result = __tinypy_class_is_subclass(class_value, candidate_base);
    vm->evaluation_depth -= 1U;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_class_bind_impl(tinypy_value_t *class_value, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(attribute);

    if (kind == TINYPY_VALUE_FUNCTION) {
        tinypy_value_t *return_value_1 = tinypy_method_new(attribute, instance, class_value);
        return return_value_1;
    }
    if (kind == TINYPY_VALUE_STATIC_METHOD) {
        tinypy_value_t *callable = tinypy_static_method_callable(attribute);

        return TINYPY_RET(callable);
    }
    if (kind == TINYPY_VALUE_CLASS_METHOD) {
        tinypy_value_t *class_method_callable = tinypy_class_method_callable(attribute);
        tinypy_value_t *return_value_2 = tinypy_method_new(class_method_callable, class_value, class_value);
        return return_value_2;
    }
    if ((attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_type_lookup_key(vm, attribute->type, vm->internal_special_get_key) != NULL) {
        tinypy_value_t *method = tinypy_internal_object_get_special_key(attribute, vm->internal_special_get_key, out_error);
        tinypy_value_t *none = NULL;
        tinypy_value_t *items[2];
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) {
            return NULL;
        }
        if (instance == NULL) {
            none = TINYPY_RET_NONE(vm);
        }
        items[0] = instance != NULL ? instance : none;
        items[1] = class_value;
        args = tinypy_tuple_from_items(vm, items, 2U);
        result = tinypy_call(method, args, NULL, out_error);
        TINYPY_DECREF(args);
        if (none != NULL) {
            TINYPY_DECREF(none);
        }
        TINYPY_DECREF(method);
        return result;
    }
    if (kind == TINYPY_VALUE_PROPERTY && instance != NULL) {
        tinypy_value_t *return_value_3 = attribute->type->descriptor_get(attribute, instance, NULL, out_error);
        return return_value_3;
    }
    return TINYPY_RET(attribute);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_class_bind(tinypy_value_t *class_value, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_error_t **out_error) {
    TINYPY_INCREF(class_value);
    TINYPY_INCREF(attribute);
    tinypy_value_t *result = __tinypy_class_bind_impl(class_value, attribute, instance, out_error);
    TINYPY_DECREF(attribute);
    TINYPY_DECREF(class_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_new(const char *name, size_t name_size, tinypy_value_t *bases, tinypy_value_t *namespace_dict, tinypy_error_t **out_error) {
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(bases);
    TINYPY_CLEAR_ERROR(out_error);
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(bases);
    iterator_end = TINYPY_TUPLE_ITERATOR_END(bases);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *base = *iterator;

        if (TINYPY_VALUE_KIND(base) != TINYPY_VALUE_CLASS) {
            /* PyClass_New lets the type of the first non-class base build the
               class, which is how class C(Old, object) becomes new-style. */
            tinypy_value_t *metaclass = &base->type->base.base;

            if (base->type->call == NULL && TINYPY_VALUE_KIND(metaclass) != TINYPY_VALUE_TYPE) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "PyClass_New: base must be a class", out_error);
                return NULL;
            }
            tinypy_value_t *name_value = tinypy_internal_name_from_bytes(vm, name, name_size);
            tinypy_value_t *items[3] = {name_value, bases, namespace_dict};
            tinypy_value_t *args = tinypy_tuple_from_items(vm, items, 3U);
            tinypy_value_t *result = tinypy_call(metaclass, args, NULL, out_error);

            TINYPY_DECREF(args);
            TINYPY_DECREF(name_value);
            return result;
        }
    }
    tinypy_class_object_t *class_object = (tinypy_class_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_CLASS, sizeof(*class_object));
    class_object->name = tinypy_internal_name_from_bytes(vm, name, name_size);
    class_object->bases = bases;
    class_object->dict = namespace_dict;
    TINYPY_INCREF(bases);
    TINYPY_INCREF(namespace_dict);
    return &class_object->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *const names[] = {vm->internal_name_key, vm->internal_bases_key, vm->internal_dict_key};
    size_t count = TINYPY_TUPLE_SIZE(args);
    size_t keywords = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    size_t recognized = 0U;
    tinypy_value_t *values[3] = {NULL, NULL, NULL};
    tinypy_value_t *result = NULL;

    if (count > 3U || keywords > 3U - count) {
        tinypy_value_t *number = tinypy_integer_from_i64(vm, (int64_t)(count + keywords));
        tinypy_value_t *text = tinypy_object_str(number, out_error);

        TINYPY_DECREF(number);
        if (text != NULL) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("function takes at most 3 arguments ("),
                {(const char *)TINYPY_TEXT_BYTES(text), TINYPY_TEXT_BYTE_SIZE(text)},
                TINYPY_MESSAGE_PART_LITERAL(" given)")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
            TINYPY_DECREF(text);
        }
        return NULL;
    }
    for (size_t index = 0U; index < 3U; ++index) {
        tinypy_value_t *value = index < count ? TINYPY_TUPLE_GET(args, index) : NULL;
        tinypy_value_t *keyword = recognized < keywords ? tinypy_internal_constructor_keyword_optional(kwargs, names[index]) : NULL;

        if (keyword != NULL) {
            recognized += 1U;
            if (value != NULL) {
                char position = (char)('1' + index);
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("Argument given by name ('"),
                    TINYPY_MESSAGE_PART_TEXT(names[index]),
                    TINYPY_MESSAGE_PART_LITERAL("') and position ("),
                    {&position, 1U},
                    TINYPY_MESSAGE_PART_LITERAL(")")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 5U, out_error);
                goto cleanup;
            }
            value = keyword;
        }
        if (value == NULL) {
            char position = (char)('1' + index);
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("Required argument '"),
                TINYPY_MESSAGE_PART_TEXT(names[index]),
                TINYPY_MESSAGE_PART_LITERAL("' (pos "),
                {&position, 1U},
                TINYPY_MESSAGE_PART_LITERAL(") not found")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 5U, out_error);
            goto cleanup;
        }
        if (index == 0U && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_STRING) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("argument 1 must be string, not "),
                {TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE ? "None" : value->type->name,
                 TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE ? 4U : value->type->name_size}
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
            goto cleanup;
        }
        values[index] = value;
        TINYPY_INCREF(value);
    }
    if (TINYPY_VALUE_KIND(values[2]) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "PyClass_New: dict must be a dictionary", out_error);
        goto cleanup;
    }
    tinypy_value_t *internal_doc_key = vm->internal_special_doc_key;
    tinypy_value_t *module_key = vm->internal_special_module_key;

    if (tinypy_internal_dict_get_optional_suppressed(vm, values[2], internal_doc_key) == NULL
        && tinypy_internal_dict_set_checked(vm, values[2], internal_doc_key, &vm->none_object.base, out_error) == 0) {
        goto cleanup;
    }
    if (tinypy_internal_dict_get_optional_suppressed(vm, values[2], module_key) == NULL && vm->current_frame != NULL) {
        tinypy_value_t *internal_name_key = vm->internal_special_name_key;
        tinypy_value_t *module_name = tinypy_internal_dict_get_optional_suppressed(vm, vm->current_frame->globals, internal_name_key);

        if (module_name != NULL) {
            TINYPY_INCREF(module_name);
            tinypy_bool_t stored = tinypy_internal_dict_set_checked(vm, values[2], module_key, module_name, out_error);

            TINYPY_DECREF(module_name);
            if (stored == 0) {
                goto cleanup;
            }
        }
    }
    if (TINYPY_VALUE_KIND(values[1]) != TINYPY_VALUE_TUPLE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "PyClass_New: bases must be a tuple", out_error);
        goto cleanup;
    }
    for (size_t index = 0U; index < TINYPY_TUPLE_SIZE(values[1]); ++index) {
        tinypy_value_t *base = TINYPY_TUPLE_GET(values[1], index);

        if (TINYPY_VALUE_KIND(base) != TINYPY_VALUE_CLASS) {
            tinypy_value_t *metaclass = &base->type->base.base;
            tinypy_value_t *arguments = tinypy_tuple_from_items(vm, values, 3U);

            result = tinypy_call(metaclass, arguments, NULL, out_error);
            TINYPY_DECREF(arguments);
            goto cleanup;
        }
    }
    result = tinypy_class_new((const char *)TINYPY_STRING_OBJECT(values[0])->bytes, TINYPY_STRING_SIZE(values[0]), values[1], values[2], out_error);
    if (result != NULL && TINYPY_VALUE_KIND(result) == TINYPY_VALUE_CLASS) {
        tinypy_value_t *previous = TINYPY_CLASS_OBJECT(result)->name;

        TINYPY_INCREF(values[0]);
        TINYPY_CLASS_OBJECT(result)->name = values[0];
        TINYPY_DECREF(previous);
    }
cleanup:
    for (size_t index = 0U; index < 3U; ++index) {
        if (values[index] != NULL) {
            TINYPY_DECREF(values[index]);
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_class_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT(value);
    visit(class_object->name, user_data);
    visit(class_object->bases, user_data);
    visit(class_object->dict, user_data);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_old_instance_new(tinypy_value_t *class_value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_value);
    tinypy_old_instance_object_t *instance = (tinypy_old_instance_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_OLD_INSTANCE, sizeof(*instance));
    instance->class_object = class_value;
    instance->dict = tinypy_dict_new(vm);
    TINYPY_INCREF(class_value);
    return &instance->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_old_instance_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(value);
    visit(instance->class_object, user_data);
    visit(instance->dict, user_data);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_class_special_attribute(tinypy_value_t *class_value, tinypy_value_t *name) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_value);
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT(class_value);
    tinypy_value_t *result = NULL;

    if (TINYPY_NAME_EQ(name, vm->internal_special_name_key) != 0) {
        result = class_object->name;
    }
    else if (TINYPY_NAME_EQ(name, vm->internal_special_bases_key) != 0) {
        result = class_object->bases;
    }
    else if (TINYPY_NAME_EQ(name, vm->internal_special_dict_key) != 0) {
        result = class_object->dict;
    }
    if (result != NULL) {
        TINYPY_INCREF(result);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_get_attribute(tinypy_value_t *class_value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_value);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *special = __tinypy_class_special_attribute(class_value, name);
    if (special != NULL) {
        return special;
    }
    tinypy_value_t *attribute = tinypy_internal_class_lookup_key(vm, class_value, name);
    tinypy_value_t *return_value_1 = attribute != NULL ? __tinypy_class_bind(class_value, attribute, NULL, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_old_instance_get_direct(tinypy_vm_t *vm, tinypy_value_t *instance_value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(instance_value);

    if (TINYPY_NAME_EQ(name, vm->internal_special_class_key) != 0) {
        return TINYPY_RET(instance->class_object);
    }
    if (TINYPY_NAME_EQ(name, vm->internal_special_dict_key) != 0) {
        return TINYPY_RET(instance->dict);
    }
    tinypy_value_t *attribute = tinypy_internal_dict_get_optional(vm, instance->dict, name);
    if (attribute != NULL) {
        return TINYPY_RET(attribute);
    }
    attribute = tinypy_internal_class_lookup_key(vm, instance->class_object, name);
    tinypy_value_t *return_value_1 = attribute != NULL ? __tinypy_class_bind(instance->class_object, attribute, instance_value, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_old_instance_get_attribute(tinypy_value_t *instance_value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance_value);
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(instance_value);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *result = __tinypy_old_instance_get_direct(vm, instance_value, name, out_error);
    if (result != NULL || TINYPY_NAME_EQ(name, vm->internal_special_getattr_key) != 0) {
        return result;
    }
    tinypy_value_t *hook_attribute = tinypy_internal_class_lookup_key(vm, instance->class_object, vm->internal_special_getattr_key);
    tinypy_value_t *hook;
    tinypy_value_t *args;

    if (hook_attribute == NULL) {
        return NULL;
    }
    if (vm->raised_value != NULL && tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_ATTRIBUTE_ERROR, out_error) == 0) {
        return NULL;
    }
    hook = __tinypy_class_bind(instance->class_object, hook_attribute, instance_value, out_error);
    if (hook == NULL) {
        return NULL;
    }
    args = tinypy_tuple_from_items(vm, &name, 1U);
    result = tinypy_call(hook, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(hook);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_class_set_attribute(tinypy_value_t *class_value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT(class_value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_value);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t **field = NULL;
    if (TINYPY_NAME_EQ(name, vm->internal_special_name_key) != 0) {
        if (attribute_value == NULL || TINYPY_VALUE_KIND(attribute_value) != TINYPY_VALUE_STRING) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__name__ must be a string object", out_error);
            return TINYPY_FALSE;
        }
        if (memchr(TINYPY_TEXT_BYTES(attribute_value), '\0', TINYPY_TEXT_BYTE_SIZE(attribute_value)) != NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__name__ must not contain null bytes", out_error);
            return TINYPY_FALSE;
        }
        field = &class_object->name;
    }
    else if (TINYPY_NAME_EQ(name, vm->internal_special_dict_key) != 0) {
        if (attribute_value == NULL || TINYPY_VALUE_KIND(attribute_value) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__dict__ must be a dictionary object", out_error);
            return TINYPY_FALSE;
        }
        field = &class_object->dict;
    }
    else if (TINYPY_NAME_EQ(name, vm->internal_special_bases_key) != 0) {
        if (attribute_value == NULL || TINYPY_VALUE_KIND(attribute_value) != TINYPY_VALUE_TUPLE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__bases__ must be a tuple object", out_error);
            return TINYPY_FALSE;
        }
        for (size_t index = 0U; index < TINYPY_TUPLE_SIZE(attribute_value); ++index) {
            tinypy_value_t *base = TINYPY_TUPLE_GET(attribute_value, index);
            if (TINYPY_VALUE_KIND(base) != TINYPY_VALUE_CLASS) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__bases__ items must be classes", out_error);
                return TINYPY_FALSE;
            }
            if (tinypy_class_is_subclass(base, class_value) != 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "a __bases__ item causes an inheritance cycle", out_error);
                return TINYPY_FALSE;
            }
            if (tinypy_vm_has_error(vm) != 0) {
                return TINYPY_FALSE;
            }
        }
        field = &class_object->bases;
    }
    if (field != NULL) {
        tinypy_value_t *previous = *field;
        TINYPY_INCREF(attribute_value);
        *field = attribute_value;
        TINYPY_DECREF(previous);
        return TINYPY_TRUE;
    }
    tinypy_bool_t stored = tinypy_internal_dict_set_checked(TINYPY_VALUE_VM(class_value), class_object->dict, name, attribute_value, out_error);
    return stored;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_old_instance_set_attribute(tinypy_value_t *instance_value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(instance_value);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_bool_t stored = tinypy_internal_dict_set_checked(TINYPY_VALUE_VM(instance_value), instance->dict, name, attribute_value, out_error);
    return stored;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_old_instance_set_class(tinypy_value_t *instance_value, tinypy_value_t *class_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance_value);
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(instance_value);
    tinypy_value_t *previous;

    TINYPY_CLEAR_ERROR(out_error);
    if (class_value == NULL || TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_CLASS) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__class__ must be set to a class", out_error);
        return TINYPY_FALSE;
    }
    previous = instance->class_object;
    TINYPY_INCREF(class_value);
    instance->class_object = class_value;
    TINYPY_DECREF(previous);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_old_instance_set_dict(tinypy_value_t *instance_value, tinypy_value_t *dict_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance_value);
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(instance_value);
    tinypy_value_t *previous;

    TINYPY_CLEAR_ERROR(out_error);
    if (dict_value == NULL || TINYPY_VALUE_KIND(dict_value) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__dict__ must be set to a dictionary", out_error);
        return TINYPY_FALSE;
    }
    previous = instance->dict;
    TINYPY_INCREF(dict_value);
    instance->dict = dict_value;
    TINYPY_DECREF(previous);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_class_delete_from_dict(tinypy_value_t *owner, tinypy_value_t *dict, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(owner);

    if (tinypy_internal_dict_delete_optional(vm, dict, name) == 0) {
        tinypy_internal_object_make_attribute_error_key(owner, name, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_class_delete_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    if (TINYPY_NAME_EQ(name, vm->internal_special_name_key) != TINYPY_FALSE ||
        TINYPY_NAME_EQ(name, vm->internal_special_dict_key) != TINYPY_FALSE ||
        TINYPY_NAME_EQ(name, vm->internal_special_bases_key) != TINYPY_FALSE) {
        tinypy_bool_t stored = tinypy_internal_class_set_attribute(value, name, NULL, out_error);
        return stored;
    }
    tinypy_bool_t return_value_1 = __tinypy_class_delete_from_dict(value, TINYPY_CLASS_OBJECT(value)->dict, name, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_old_instance_delete_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_bool_t return_value_1 = __tinypy_class_delete_from_dict(value, TINYPY_OLD_INSTANCE_OBJECT(value)->dict, name, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    tinypy_value_t *instance = tinypy_old_instance_new(callable);
    tinypy_value_t *initializer_attribute = tinypy_internal_class_lookup_key(vm, callable, vm->internal_special_init_key);

    if (initializer_attribute == NULL) {
        if (TINYPY_TUPLE_SIZE(args) != 0U || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U)) {
            TINYPY_DECREF(instance);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "class constructor takes no arguments", out_error);
            return NULL;
        }
        return instance;
    }
    tinypy_value_t *initializer = __tinypy_class_bind(callable, initializer_attribute, instance, out_error);
    if (initializer == NULL) {
        TINYPY_DECREF(instance);
        return NULL;
    }
    tinypy_value_t *result = tinypy_call(initializer, args, kwargs, out_error);
    TINYPY_DECREF(initializer);
    if (result == NULL) {
        TINYPY_DECREF(instance);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_NONE) {
        TINYPY_DECREF(result);
        TINYPY_DECREF(instance);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__init__ must return None", out_error);
        return NULL;
    }
    TINYPY_DECREF(result);
    return instance;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_old_instance_has_special_key(tinypy_value_t *value, tinypy_value_t *key) {
    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_OLD_INSTANCE) {
        return TINYPY_FALSE;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(value);
    tinypy_bool_t result = tinypy_internal_dict_get_optional(vm, instance->dict, key) != NULL
        || tinypy_internal_class_lookup_key(vm, instance->class_object, key) != NULL;
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_name(const tinypy_value_t *class_value) {
    tinypy_value_t *return_value_1 = TINYPY_CLASS_OBJECT((tinypy_value_t *)class_value)->name;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_bases(const tinypy_value_t *class_value) {
    tinypy_value_t *return_value_1 = TINYPY_CLASS_OBJECT((tinypy_value_t *)class_value)->bases;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_dict(const tinypy_value_t *class_value) {
    tinypy_value_t *return_value_1 = TINYPY_CLASS_OBJECT((tinypy_value_t *)class_value)->dict;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_old_instance_class(const tinypy_value_t *instance) {
    tinypy_value_t *return_value_1 = TINYPY_OLD_INSTANCE_OBJECT((tinypy_value_t *)instance)->class_object;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_old_instance_dict(const tinypy_value_t *instance) {
    tinypy_value_t *return_value_1 = TINYPY_OLD_INSTANCE_OBJECT((tinypy_value_t *)instance)->dict;
    return return_value_1;
}
