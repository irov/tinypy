#include "tinypy/class.h"

#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
/* PyDict_GetItem of the classic lookups: the error of a key comparison is
   dropped, and an exception pending before is set aside only when there is
   one. */
static tinypy_value_t *__tinypy_class_dict_probe(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key) {
    if (vm->raised_type != NULL) {
        tinypy_value_t *kept = tinypy_internal_dict_get_optional_suppressed(vm, dict, key);
        return kept;
    }
    tinypy_value_t *value = tinypy_internal_dict_get_optional(vm, dict, key);
    if (value == NULL && vm->raised_type != NULL) {
        tinypy_internal_exception_clear_raised(vm);
    }
    return value;
}
//////////////////////////////////////////////////////////////////////////
/* class_lookup recurses into a base without a recursion check, so only the
   native stack guard fails it. */
static tinypy_value_t *__tinypy_class_lookup_base(tinypy_vm_t *vm, tinypy_value_t *base, tinypy_value_t *key) {
    if (__tinypy_internal_native_recursion_enter(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded in class lookup", NULL) == 0) {
        return NULL;
    }
    tinypy_value_t *attribute = tinypy_internal_class_lookup_key(vm, base, key);
    __tinypy_internal_native_recursion_leave(vm);
    return attribute;
}
//////////////////////////////////////////////////////////////////////////
/* class_lookup. A key comparison that replaced the dictionary or the bases
   holding the attribute found may have freed it, so that attribute is looked
   up again. */
tinypy_value_t *tinypy_internal_class_lookup_key(tinypy_vm_t *vm, tinypy_value_t *class_value, tinypy_value_t *key) {
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT(class_value);
    for (;;) {
        tinypy_value_t *dict = TINYPY_RET(class_object->dict);
        tinypy_value_t *attribute = __tinypy_class_dict_probe(vm, dict, key);
        tinypy_bool_t replaced = class_object->dict != dict ? TINYPY_TRUE : TINYPY_FALSE;

        TINYPY_DECREF(dict);
        if (attribute != NULL) {
            if (replaced != 0) {
                continue;
            }
            return attribute;
        }
        tinypy_value_t *bases = TINYPY_RET(class_object->bases);
        for (size_t index = 0U; attribute == NULL && index < TINYPY_TUPLE_SIZE(bases); ++index) {
            attribute = __tinypy_class_lookup_base(vm, TINYPY_TUPLE_GET(bases, index), key);
            if (attribute == NULL && vm->raised_type != NULL) {
                break;
            }
        }
        replaced = class_object->bases != bases ? TINYPY_TRUE : TINYPY_FALSE;
        TINYPY_DECREF(bases);
        if (attribute == NULL || replaced == 0) {
            return attribute;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
/* A lookup that fails with a pending error, such as the stack guard,
   reports it through out_error too, unless that already holds one. */
static tinypy_value_t *__tinypy_class_lookup_key_checked(tinypy_vm_t *vm, tinypy_value_t *class_value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_value_t *attribute = tinypy_internal_class_lookup_key(vm, class_value, key);
    if (attribute == NULL && out_error != NULL && *out_error == NULL) {
        tinypy_internal_exception_make_diagnostic(vm, out_error);
    }
    return attribute;
}
//////////////////////////////////////////////////////////////////////////
/* PyClass_IsSubclass, which takes no recursion level: the walk, which runs
   no Python code, follows the last base in a loop and recurses into the
   others under the native stack guard alone. */
int32_t tinypy_internal_class_is_subclass(const tinypy_value_t *class_value, const tinypy_value_t *candidate_base, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_value);

    while (class_value != candidate_base) {
        tinypy_value_t *bases = TINYPY_CLASS_OBJECT((tinypy_value_t *)class_value)->bases;
        size_t base_count = TINYPY_TUPLE_SIZE(bases);

        if (base_count == 0U) {
            return 0;
        }
        if (base_count > 1U) {
            if (__tinypy_internal_native_recursion_enter(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded in subclass check", out_error) == 0) {
                return -1;
            }
            int32_t result = 0;
            for (size_t index = 0U; result == 0 && index + 1U < base_count; ++index) {
                result = tinypy_internal_class_is_subclass(TINYPY_TUPLE_GET(bases, index), candidate_base, out_error);
            }
            __tinypy_internal_native_recursion_leave(vm);
            if (result != 0) {
                return result;
            }
        }
        class_value = TINYPY_TUPLE_GET(bases, base_count - 1U);
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_class_is_subclass(const tinypy_value_t *class_value, const tinypy_value_t *candidate_base) {
    int32_t subclass = tinypy_internal_class_is_subclass(class_value, candidate_base, NULL);
    return subclass > 0 ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* class_getattr and instance_getattr2 call the tp_descr_get of whatever
   they find, with the class as its owner; a function and an initialised
   static method are bound directly. */
static tinypy_value_t *__tinypy_class_bind(tinypy_value_t *class_value, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_value);

    if (TINYPY_VALUE_KIND(attribute) == TINYPY_VALUE_FUNCTION) {
        tinypy_value_t *method = tinypy_method_new(attribute, instance, class_value);
        return method;
    }
    tinypy_value_t *callable = attribute->type == &vm->types[TINYPY_VALUE_STATIC_METHOD] ? tinypy_static_method_callable(attribute) : NULL;
    if (callable != NULL) {
        return TINYPY_RET(callable);
    }
    tinypy_value_t *result = tinypy_internal_descriptor_get_class_value(vm, attribute, instance, class_value, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* PyClass_New fills in __doc__ and the __module__ of the calling frame's
   globals before it looks at the bases. */
static tinypy_bool_t __tinypy_class_namespace_defaults(tinypy_vm_t *vm, tinypy_value_t *namespace_dict, tinypy_error_t **out_error) {
    tinypy_value_t *doc_key = vm->internal_special_doc_key;
    tinypy_value_t *module_key = vm->internal_special_module_key;

    if (tinypy_internal_dict_get_optional_suppressed(vm, namespace_dict, doc_key) == NULL) {
        if (tinypy_internal_dict_set_checked(vm, namespace_dict, doc_key, &vm->none_object.base, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    if (vm->current_frame == NULL || tinypy_internal_dict_get_optional_suppressed(vm, namespace_dict, module_key) != NULL) {
        return TINYPY_TRUE;
    }
    tinypy_value_t *module_name = tinypy_internal_dict_get_optional_suppressed(vm, vm->current_frame->globals, vm->internal_special_name_key);
    if (module_name == NULL) {
        return TINYPY_TRUE;
    }
    TINYPY_INCREF(module_name);
    tinypy_bool_t stored = tinypy_internal_dict_set_checked(vm, namespace_dict, module_key, module_name, out_error);
    TINYPY_DECREF(module_name);
    return stored;
}
//////////////////////////////////////////////////////////////////////////
/* set_slot: the hooks are owned, and a lookup result is borrowed. */
static void __tinypy_class_set_hook(tinypy_value_t **slot, tinypy_value_t *hook) {
    tinypy_value_t *previous = *slot;

    if (hook != NULL) {
        TINYPY_INCREF(hook);
    }
    *slot = hook;
    if (previous != NULL) {
        TINYPY_DECREF(previous);
    }
}
//////////////////////////////////////////////////////////////////////////
/* class_lookup of set_attr_slots for each hook not found yet; the caller
   sets aside the exception pending before. A hook found is stored owned and
   counted off the hooks still missing. The bases are walked under the
   native stack guard of class_lookup, whose failure fails the walk. */
static tinypy_bool_t __tinypy_class_find_hooks(tinypy_vm_t *vm, tinypy_value_t *class_value, tinypy_value_t *const *names, tinypy_value_t **hooks, size_t count, size_t *in_out_missing) {
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT(class_value);
    tinypy_value_t *dict = TINYPY_RET(class_object->dict);

    for (size_t index = 0U; index < count; ++index) {
        if (hooks[index] != NULL) {
            continue;
        }
        tinypy_value_t *hook = __tinypy_class_dict_probe(vm, dict, names[index]);
        if (hook != NULL) {
            hooks[index] = TINYPY_RET(hook);
            *in_out_missing -= 1U;
        }
    }
    TINYPY_DECREF(dict);
    tinypy_value_t *bases = TINYPY_RET(class_object->bases);
    tinypy_bool_t walked = TINYPY_TRUE;

    for (size_t index = 0U; walked != 0 && *in_out_missing != 0U && index < TINYPY_TUPLE_SIZE(bases); ++index) {
        walked = __tinypy_internal_native_recursion_enter(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded in class lookup", NULL);
        if (walked != 0) {
            walked = __tinypy_class_find_hooks(vm, TINYPY_TUPLE_GET(bases, index), names, hooks, count, in_out_missing);
            __tinypy_internal_native_recursion_leave(vm);
        }
    }
    TINYPY_DECREF(bases);
    return walked;
}
//////////////////////////////////////////////////////////////////////////
/* set_attr_slots: the hooks a class resolves through its bases, found in
   one walk. No error of their lookups outlives it; a hierarchy too deep for
   the native stack guard keeps the hooks the class had and raises
   RuntimeError instead. */
static tinypy_bool_t __tinypy_class_refresh_hooks(tinypy_value_t *class_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_value);
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT(class_value);
    tinypy_value_t *const names[] = {vm->internal_special_getattr_key, vm->internal_special_setattr_key, vm->internal_special_delattr_key};
    tinypy_value_t **const slots[] = {&class_object->getattr_hook, &class_object->setattr_hook, &class_object->delattr_hook};
    tinypy_value_t *hooks[] = {NULL, NULL, NULL};
    size_t count = sizeof(names) / sizeof(names[0]);
    size_t missing = count;
    tinypy_internal_exception_state_t state;

    tinypy_internal_exception_preserve_begin(vm, &state);
    tinypy_bool_t found = __tinypy_class_find_hooks(vm, class_value, names, hooks, count, &missing);
    for (size_t index = 0U; index < count; ++index) {
        if (found != 0) {
            __tinypy_class_set_hook(slots[index], hooks[index]);
        }
        if (hooks[index] != NULL) {
            TINYPY_DECREF(hooks[index]);
        }
    }
    tinypy_internal_exception_preserve_end(vm, &state);
    if (found == 0) {
        tinypy_internal_recursion_error(vm, "maximum recursion depth exceeded in class lookup", out_error);
    }
    return found;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_hook(tinypy_value_t *class_value, tinypy_value_t *name) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_value);
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT(class_value);

    if (TINYPY_NAME_EQ(name, vm->internal_special_getattr_key) != 0) {
        return class_object->getattr_hook;
    }
    if (TINYPY_NAME_EQ(name, vm->internal_special_setattr_key) != 0) {
        return class_object->setattr_hook;
    }
    return class_object->delattr_hook;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_new(const char *name, size_t name_size, tinypy_value_t *bases, tinypy_value_t *namespace_dict, tinypy_error_t **out_error) {
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(bases);
    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_class_namespace_defaults(vm, namespace_dict, out_error) == 0) {
        return NULL;
    }
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
    TINYPY_DICT_OBJECT(namespace_dict)->type_dictionary = TINYPY_TRUE;
    TINYPY_INCREF(bases);
    TINYPY_INCREF(namespace_dict);
    if (__tinypy_class_refresh_hooks(&class_object->base, out_error) == 0) {
        TINYPY_DECREF(&class_object->base);
        return NULL;
    }
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
                TINYPY_MESSAGE_PART_ARGUMENT_TYPE_NAME(value)
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
    if (__tinypy_class_namespace_defaults(vm, values[2], out_error) == 0) {
        goto cleanup;
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
    if (class_object->getattr_hook != NULL) {
        visit(class_object->getattr_hook, user_data);
    }
    if (class_object->setattr_hook != NULL) {
        visit(class_object->setattr_hook, user_data);
    }
    if (class_object->delattr_hook != NULL) {
        visit(class_object->delattr_hook, user_data);
    }
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
/* instance_new: instance(class[, dict]) makes a classic instance without
   calling __init__, using the given dictionary itself. */
tinypy_value_t *tinypy_internal_old_instance_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);

    (void)kwargs;
    TINYPY_CLEAR_ERROR(out_error);
    if (count < 1U || count > 2U) {
        tinypy_internal_make_arity_error(vm, "instance", 8U, count, 1U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_CLASS) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("instance() argument 1 must be classobj, not "),
            TINYPY_MESSAGE_PART_TYPE_NAME_PRECISION(class_value, 50U),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    tinypy_value_t *dict = count == 2U ? TINYPY_TUPLE_GET(args, 1U) : NULL;
    if (dict != NULL && TINYPY_VALUE_KIND(dict) != TINYPY_VALUE_NONE && TINYPY_VALUE_KIND(dict) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "instance() second arg must be dictionary or None", out_error);
        return NULL;
    }
    tinypy_value_t *instance = tinypy_old_instance_new(class_value);
    if (dict != NULL && TINYPY_VALUE_KIND(dict) == TINYPY_VALUE_DICT) {
        (void)tinypy_internal_old_instance_set_dict(instance, dict, NULL);
    }
    return instance;
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
    tinypy_value_t *attribute = __tinypy_class_lookup_key_checked(vm, class_value, name, out_error);
    tinypy_value_t *return_value_1 = attribute != NULL ? __tinypy_class_bind(class_value, attribute, NULL, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* The owned value of key in the dictionary of a class or an instance, which
   stays referenced while a key comparison may replace it. */
static inline tinypy_value_t *__tinypy_class_dict_value(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key) {
    TINYPY_INCREF(dict);
    tinypy_value_t *value = __tinypy_class_dict_probe(vm, dict, key);

    if (value != NULL) {
        TINYPY_INCREF(value);
    }
    TINYPY_DECREF(dict);
    return value;
}
//////////////////////////////////////////////////////////////////////////
/* The attribute the class of an instance provides. The caller keeps
   class_value referenced, since a key comparison of the lookup may replace
   the class of the instance. */
static tinypy_value_t *__tinypy_old_instance_class_attribute(tinypy_vm_t *vm, tinypy_value_t *instance_value, tinypy_value_t *class_value, tinypy_value_t *name, tinypy_bool_t *out_unbound, tinypy_error_t **out_error) {
    tinypy_value_t *attribute = __tinypy_class_lookup_key_checked(vm, class_value, name, out_error);

    if (attribute == NULL) {
        return NULL;
    }
    if (out_unbound != NULL && attribute->type == &vm->types[TINYPY_VALUE_FUNCTION]) {
        *out_unbound = TINYPY_TRUE;
        return TINYPY_RET(attribute);
    }
    tinypy_value_t *result = __tinypy_class_bind(TINYPY_OLD_INSTANCE_OBJECT(instance_value)->class_object, attribute, instance_value, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* With out_unbound, a function found on the class is returned as it is, for
   a method call to pass the instance as its first argument. */
static tinypy_value_t *__tinypy_old_instance_get_direct(tinypy_vm_t *vm, tinypy_value_t *instance_value, tinypy_value_t *name, tinypy_bool_t *out_unbound, tinypy_error_t **out_error) {
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(instance_value);

    if (TINYPY_NAME_EQ(name, vm->internal_special_class_key) != 0) {
        return TINYPY_RET(instance->class_object);
    }
    if (TINYPY_NAME_EQ(name, vm->internal_special_dict_key) != 0) {
        return TINYPY_RET(instance->dict);
    }
    /* An interned name decides the lookup in the instance dictionary by
       identity, without a key comparison. */
    tinypy_value_t *attribute = NULL;
    tinypy_bool_t decided = tinypy_internal_dict_get_identity(vm, instance->dict, name, &attribute);
    if (decided == 0) {
        attribute = __tinypy_class_dict_value(vm, instance->dict, name);
    }
    else if (attribute != NULL) {
        TINYPY_INCREF(attribute);
    }
    if (attribute != NULL) {
        return attribute;
    }
    tinypy_value_t *class_value = TINYPY_RET(instance->class_object);
    tinypy_value_t *result = __tinypy_old_instance_class_attribute(vm, instance_value, class_value, name, out_unbound, out_error);

    TINYPY_DECREF(class_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_old_instance_get_attribute(tinypy_value_t *instance_value, tinypy_value_t *name, tinypy_bool_t *out_unbound, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance_value);
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(instance_value);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *result = __tinypy_old_instance_get_direct(vm, instance_value, name, out_unbound, out_error);
    if (result != NULL || TINYPY_NAME_EQ(name, vm->internal_special_getattr_key) != 0) {
        return result;
    }
    tinypy_value_t *hook = TINYPY_CLASS_OBJECT(instance->class_object)->getattr_hook;

    if (hook == NULL) {
        return NULL;
    }
    /* Releasing the AttributeError may run a __del__ that replaces the hook. */
    TINYPY_INCREF(hook);
    if (vm->raised_value != NULL && tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_ATTRIBUTE_ERROR, out_error) == 0) {
        TINYPY_DECREF(hook);
        return NULL;
    }
    result = tinypy_internal_old_instance_call_hook(instance_value, hook, name, NULL, out_error);
    TINYPY_DECREF(hook);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_old_instance_get_attribute(tinypy_value_t *instance_value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_old_instance_get_attribute(instance_value, name, NULL, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The attribute an attribute load or a method call finds on a classic
   instance, raising AttributeError as getattr does when there is none; with
   out_unbound, a function of the class comes unbound and sets it. */
tinypy_value_t *tinypy_internal_old_instance_get_method(tinypy_value_t *instance_value, tinypy_value_t *name, tinypy_bool_t *out_unbound, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance_value);
    tinypy_value_t *result = __tinypy_old_instance_get_attribute(instance_value, name, out_unbound, out_error);

    if (result == NULL && vm->raised_type == NULL && (out_error == NULL || *out_error == NULL)) {
        tinypy_internal_object_make_attribute_error_key(instance_value, name, out_error);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* cl_getattr, cl_setattr and cl_delattr: the hook found in the class is
   called unbound, with the instance first and without __get__. */
tinypy_value_t *tinypy_internal_old_instance_call_hook(tinypy_value_t *instance_value, tinypy_value_t *hook, tinypy_value_t *name, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance_value);
    tinypy_value_t *items[] = {instance_value, name, value};

    TINYPY_INCREF(hook);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, items, value != NULL ? 3U : 2U);
    tinypy_value_t *result = tinypy_call(hook, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(hook);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The dictionary of the owner stays referenced while a key comparison of the
   store may replace it. */
static tinypy_bool_t __tinypy_class_store_in_dict(tinypy_value_t *owner, tinypy_value_t *dict, tinypy_value_t *name, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(owner);

    TINYPY_INCREF(dict);
    tinypy_bool_t stored = tinypy_internal_dict_set_checked(vm, dict, name, value, out_error);
    TINYPY_DECREF(dict);
    return stored;
}
//////////////////////////////////////////////////////////////////////////
/* The dictionary of the owner stays referenced while a key comparison of the
   deletion may replace it. */
static tinypy_bool_t __tinypy_class_delete_from_dict(tinypy_value_t *owner, tinypy_value_t *dict, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(owner);

    TINYPY_INCREF(dict);
    tinypy_bool_t deleted = tinypy_internal_dict_delete_optional(vm, dict, name);
    TINYPY_DECREF(dict);
    if (deleted == 0) {
        tinypy_internal_object_make_attribute_error_key(owner, name, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
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
            int32_t cycle = tinypy_internal_class_is_subclass(base, class_value, out_error);
            if (cycle < 0) {
                return TINYPY_FALSE;
            }
            if (cycle > 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "a __bases__ item causes an inheritance cycle", out_error);
                return TINYPY_FALSE;
            }
        }
        field = &class_object->bases;
    }
    if (field != NULL) {
        tinypy_value_t *previous = *field;
        TINYPY_INCREF(attribute_value);
        *field = attribute_value;
        /* A new-style class keeps the MRO it linearized from the class, so
           of these fields only a new dictionary changes its lookups. */
        if (field == &class_object->dict) {
            TINYPY_DICT_OBJECT(attribute_value)->type_dictionary = TINYPY_TRUE;
            __tinypy_internal_type_classic_modified(vm);
        }
        TINYPY_DECREF(previous);
        /* set_dict and set_bases resolve the hooks again; a hierarchy too
           deep for the native stack guard keeps the value assigned and
           raises. */
        if (field == &class_object->name) {
            return TINYPY_TRUE;
        }
        tinypy_bool_t refreshed = __tinypy_class_refresh_hooks(class_value, out_error);
        return refreshed;
    }
    /* class_setattr replaces the hook of this class alone, and then updates
       the dictionary as well. */
    if (TINYPY_NAME_EQ(name, vm->internal_special_getattr_key) != 0) {
        __tinypy_class_set_hook(&class_object->getattr_hook, attribute_value);
    }
    else if (TINYPY_NAME_EQ(name, vm->internal_special_setattr_key) != 0) {
        __tinypy_class_set_hook(&class_object->setattr_hook, attribute_value);
    }
    else if (TINYPY_NAME_EQ(name, vm->internal_special_delattr_key) != 0) {
        __tinypy_class_set_hook(&class_object->delattr_hook, attribute_value);
    }
    if (attribute_value == NULL) {
        tinypy_bool_t deleted = __tinypy_class_delete_from_dict(class_value, class_object->dict, name, out_error);
        return deleted;
    }
    tinypy_bool_t stored = __tinypy_class_store_in_dict(class_value, class_object->dict, name, attribute_value, out_error);
    return stored;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_old_instance_set_attribute(tinypy_value_t *instance_value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(instance_value);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_bool_t stored = __tinypy_class_store_in_dict(instance_value, instance->dict, name, attribute_value, out_error);
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
tinypy_bool_t tinypy_internal_class_delete_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_bool_t deleted = tinypy_internal_class_set_attribute(value, name, NULL, out_error);
    return deleted;
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
    tinypy_value_t *initializer_attribute = __tinypy_class_lookup_key_checked(vm, callable, vm->internal_special_init_key, out_error);

    if (initializer_attribute == NULL) {
        if (tinypy_vm_has_error(vm) != 0) {
            TINYPY_DECREF(instance);
            return NULL;
        }
        if (TINYPY_TUPLE_SIZE(args) != 0U || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U)) {
            TINYPY_DECREF(instance);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "this constructor takes no arguments", out_error);
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
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__init__() should return None", out_error);
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
    tinypy_value_t *attribute = __tinypy_class_dict_value(vm, instance->dict, key);

    if (attribute != NULL) {
        TINYPY_DECREF(attribute);
        return TINYPY_TRUE;
    }
    /* A key comparison of the lookup may replace the class of the instance. */
    tinypy_value_t *class_value = TINYPY_RET(instance->class_object);
    tinypy_bool_t found = tinypy_internal_class_lookup_key(vm, class_value, key) != NULL ? TINYPY_TRUE : TINYPY_FALSE;

    TINYPY_DECREF(class_value);
    return found;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_class_representation_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (user_data != NULL) {
        tinypy_value_t *text = tinypy_internal_object_str_builtin(self, out_error);
        return text;
    }
    tinypy_value_t *representation = tinypy_internal_object_repr_builtin(self, out_error);
    return representation;
}
//////////////////////////////////////////////////////////////////////////
/* The __call__ of classobj makes an instance, as calling the class does. */
static tinypy_value_t *__tinypy_class_call_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *const *items = tinypy_internal_tuple_items(args);
    size_t count = TINYPY_TUPLE_SIZE(args);

    (void)user_data;
    if (count == 0U || TINYPY_VALUE_KIND(items[0]) != TINYPY_VALUE_CLASS) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "classobj.__call__ requires a classic class", out_error);
        return NULL;
    }
    tinypy_value_t *call_args = tinypy_tuple_from_items(vm, items + 1U, count - 1U);
    tinypy_value_t *result = tinypy_internal_class_call(items[0], call_args, kwargs, out_error);

    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Native instance slots are also callable through type(instance). Their
   implementations keep using the classic class and __getattr__ protocols. */
static tinypy_value_t *__tinypy_classic_unary_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *name = tinypy_native_function_name(function);
    tinypy_value_t *result;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);

    if (name == vm->internal_special_repr_key || name == vm->internal_special_str_key) {
        result = tinypy_internal_object_call_representation(self, name, out_error);
        return result;
    }
    if (name == vm->internal_special_iter_key) {
        result = tinypy_iter(self, out_error);
        return result;
    }
    if (name == vm->internal_special_next_key) {
        result = tinypy_next(self, out_error);
        if (result == NULL && tinypy_vm_has_error(vm) == 0 && (out_error == NULL || *out_error == NULL)) {
            tinypy_internal_exception_raise_stop_iteration(vm, out_error);
        }
        return result;
    }
    if (name == vm->internal_special_nonzero_key) {
        int32_t truth = tinypy_truth(self, out_error);

        result = truth >= 0 ? tinypy_bool_from_i32(vm, truth) : NULL;
        return result;
    }
    tinypy_bool_t handled;
    if (name == vm->internal_special_int_key) {
        result = tinypy_internal_call_int_conversion(self, &handled, out_error);
        return result;
    }
    if (name == vm->internal_special_long_key) {
        result = tinypy_internal_call_long_conversion(self, &handled, out_error);
        return result;
    }
    result = tinypy_internal_call_classic_conversion(self, name, out_error);
    if (result == NULL || name != vm->internal_special_length_key) {
        return result;
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(result);
    if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__len__() should return an int", out_error);
        return NULL;
    }
    int64_t length;

    if (tinypy_internal_number_as_ssize(result, &length, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    TINYPY_DECREF(result);
    if (length < 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__() should return >= 0", out_error);
        return NULL;
    }
    result = tinypy_integer_from_i64(vm, length);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_classic_item_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *name = tinypy_native_function_name(function);
    size_t count = (size_t)(uintptr_t)user_data;
    tinypy_arity_style_e style = count == 2U ? TINYPY_ARITY_STYLE_UNPACK : TINYPY_ARITY_STYLE_WRAPPER;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, count, count, style, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *key = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *result;
    tinypy_bool_t assigned;

    if (name == vm->internal_special_getitem_key) {
        result = tinypy_get_item(self, key, out_error);
        return result;
    }
    if (name == vm->internal_special_contains_key) {
        int32_t contained = tinypy_contains(self, key, out_error);

        result = contained >= 0 ? tinypy_bool_from_i32(vm, contained) : NULL;
        return result;
    }
    /* instance_setattr and class_setattr take a str name alone. */
    if (name == vm->internal_special_setattr_key || name == vm->internal_special_delattr_key) {
        if (TINYPY_VALUE_KIND(key) != TINYPY_VALUE_STRING) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "attribute name must be a string", out_error);
            return NULL;
        }
        assigned = name == vm->internal_special_setattr_key
            ? tinypy_object_set_attr_value(self, key, TINYPY_TUPLE_GET(args, 2U), out_error)
            : tinypy_internal_object_delete_attr_protocol_key(self, key, out_error);
    }
    else {
        assigned = name == vm->internal_special_setitem_key
            ? tinypy_set_item(self, key, TINYPY_TUPLE_GET(args, 2U), out_error)
            : tinypy_delete_item(self, key, out_error);
    }
    result = assigned != 0 ? TINYPY_RET_NONE(vm) : NULL;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_classic_call_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, NULL, 0U, SIZE_MAX, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *const *items = tinypy_internal_tuple_items(args);
    tinypy_value_t *arguments = tinypy_tuple_from_items(vm, items + 1U, TINYPY_TUPLE_SIZE(args) - 1U);
    tinypy_value_t *result = tinypy_call(items[0], arguments, kwargs, out_error);

    TINYPY_DECREF(arguments);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_classic_add_methods(tinypy_vm_t *vm) {
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_OLD_INSTANCE];
    tinypy_value_t *unary[] = {
        vm->internal_special_pos_key, vm->internal_special_neg_key, vm->internal_special_abs_key, vm->internal_special_invert_key,
        vm->internal_special_int_key, vm->internal_special_long_key, vm->internal_special_float_key,
        vm->internal_special_oct_key, vm->internal_special_hex_key, vm->internal_special_index_key,
        vm->internal_special_nonzero_key, vm->internal_special_length_key, vm->internal_special_iter_key, vm->internal_special_next_key,
        vm->internal_special_repr_key, vm->internal_special_str_key
    };
    struct {
        tinypy_value_t *name;
        size_t count;
    } items[] = {
        {vm->internal_special_getitem_key, 1U}, {vm->internal_special_setitem_key, 2U}, {vm->internal_special_delitem_key, 1U},
        {vm->internal_special_contains_key, 1U}, {vm->internal_special_setattr_key, 2U}, {vm->internal_special_delattr_key, 1U}
    };
    tinypy_value_t *slices[] = {vm->internal_special_getslice_key, vm->internal_special_setslice_key, vm->internal_special_delslice_key};

    for (size_t index = 0U; index < sizeof(unary) / sizeof(unary[0]); ++index) {
        tinypy_internal_type_add_method(type, unary[index], __tinypy_classic_unary_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_WRAPPER);
    }
    for (size_t index = 0U; index < sizeof(items) / sizeof(items[0]); ++index) {
        tinypy_internal_type_add_method(type, items[index].name, __tinypy_classic_item_method, (void *)(uintptr_t)items[index].count, NULL, TINYPY_NATIVE_DESCRIPTOR_WRAPPER);
    }
    for (size_t index = 0U; index < sizeof(slices) / sizeof(slices[0]); ++index) {
        tinypy_internal_type_add_method(type, slices[index], tinypy_internal_classic_slice_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_WRAPPER);
    }
    tinypy_internal_type_add_method(type, vm->internal_special_call_key, __tinypy_classic_call_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_WRAPPER);
    tinypy_internal_type_add_method(type, vm->internal_special_hash_key, tinypy_internal_hash_builtin_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_WRAPPER);
    tinypy_internal_initialize_classic_operator_methods(vm);
    tinypy_internal_initialize_classic_comparison_methods(vm);
}
//////////////////////////////////////////////////////////////////////////
/* Classic classes expose class_repr, class_str, PyInstance_New and
   class_setattr; the instance type exposes its protocol slots as native
   wrappers. */
void tinypy_internal_initialize_class_type(tinypy_vm_t *vm) {
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_CLASS];
    tinypy_type_t *instance_type = &vm->types[TINYPY_VALUE_OLD_INSTANCE];

    instance_type->create = tinypy_internal_old_instance_create;
    tinypy_internal_constructor_add_builtin_new(instance_type);
    __tinypy_classic_add_methods(vm);

    tinypy_internal_constructor_add_builtin_new(type);
    tinypy_internal_type_add_method(type, vm->internal_special_repr_key, __tinypy_class_representation_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_str_key, __tinypy_class_representation_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_call_key, __tinypy_class_call_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_setattr_key, __tinypy_classic_item_method, (void *)(uintptr_t)2U, NULL, TINYPY_NATIVE_DESCRIPTOR_WRAPPER);
    tinypy_internal_type_add_method(type, vm->internal_special_delattr_key, __tinypy_classic_item_method, (void *)(uintptr_t)1U, NULL, TINYPY_NATIVE_DESCRIPTOR_WRAPPER);
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
