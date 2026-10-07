#include "tinypy/module.h"

#include "internal.h"

typedef struct tinypy_module_clear_entry_t {
    tinypy_value_t *key;
    tinypy_hash_t hash;
} tinypy_module_clear_entry_t;

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_module_from_dict_key(tinypy_value_t *name, tinypy_value_t *dict) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(name);
    tinypy_module_object_t *module = (tinypy_module_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_MODULE, sizeof(*module));
    module->name = TINYPY_RET(name);
    module->dict = dict;
    TINYPY_INCREF(dict);
    return &module->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_module_new(tinypy_vm_t *vm, const char *name, size_t name_size) {
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
    tinypy_value_t *module = tinypy_module_new_key(key);
    TINYPY_DECREF(key);
    return module;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_module_new_key(tinypy_value_t *name) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(name);
    tinypy_value_t *dict = tinypy_dict_new(vm);
    tinypy_value_t *module = tinypy_internal_module_from_dict_key(name, dict);
    TINYPY_DECREF(dict);

    tinypy_module_add_value_key(module, vm->internal_special_name_key, TINYPY_MODULE_OBJECT(module)->name);
    tinypy_module_add_value_key(module, vm->internal_special_doc_key, &vm->none_object.base);
    tinypy_module_add_value_key(module, vm->internal_special_package_key, &vm->none_object.base);
    return module;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_module_traverse_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_module_object_t *module = TINYPY_MODULE_OBJECT(value);

    if (module->name != NULL) {
        visit(module->name, user_data);
    }
    if (module->dict != NULL) {
        visit(module->dict, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_module_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_module_object_t *module = TINYPY_MODULE_OBJECT(value);
    tinypy_value_t *dict = module->dict;
    tinypy_value_t *name = module->name;

    if (dict != NULL) {
        tinypy_internal_exception_state_t state;
        tinypy_error_t *error = NULL;

        tinypy_internal_exception_preserve_begin(vm, &state);
        /* Pin names before replacing values: finalizers may mutate the
           namespace, so its hash table cannot remain borrowed across them. */
        for (size_t pass = 0U; pass < 2U; ++pass) {
            size_t capacity = TINYPY_DICT_SIZE(dict);
            size_t count = 0U;
            tinypy_module_clear_entry_t *keys;
            tinypy_dict_entry_t *entry = TINYPY_DICT_ITERATOR_BEGIN(dict);
            tinypy_dict_entry_t *end = TINYPY_DICT_ITERATOR_END(dict);

            if (error != NULL) {
                tinypy_error_release(error);
                error = NULL;
                tinypy_vm_clear_error(vm);
            }
            if (capacity == 0U || capacity > SIZE_MAX / sizeof(*keys)) {
                continue;
            }
            keys = (tinypy_module_clear_entry_t *)tinypy_internal_vm_allocate_checked(vm, capacity * sizeof(*keys), &error);
            if (keys == NULL) {
                continue;
            }
            for (; entry != end; ++entry) {
                const char *bytes;
                size_t size;

                if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry) == 0 || TINYPY_VALUE_KIND(entry->key) != TINYPY_VALUE_STRING || entry->value == &vm->none_object.base) {
                    continue;
                }
                bytes = (const char *)TINYPY_STRING_OBJECT(entry->key)->bytes;
                size = TINYPY_STRING_SIZE(entry->key);
                if ((pass == 0U && (size == 0U || bytes[0] != '_' || (size > 1U && bytes[1] == '_'))) || (pass == 1U && (entry->key == vm->internal_builtins_key || strcmp(bytes, (const char *)TINYPY_TEXT_BYTES(vm->internal_builtins_key)) == 0))) {
                    continue;
                }
                keys[count].key = entry->key;
                keys[count].hash = entry->hash;
                TINYPY_INCREF(entry->key);
                count += 1U;
            }
            for (size_t index = 0U; index < count; ++index) {
                tinypy_value_t *key = keys[index].key;
                tinypy_hash_t hash = keys[index].hash;
                size_t slot;
                tinypy_bool_t found;

                if (error != NULL) {
                    tinypy_error_release(error);
                    error = NULL;
                    tinypy_vm_clear_error(vm);
                }
                if (tinypy_internal_dict_lookup_hash_checked(vm, dict, key, hash, &slot, &found, &error) != 0
                    && found != 0 && TINYPY_DICT_OBJECT(dict)->table[slot].value != &vm->none_object.base) {
                    (void)tinypy_internal_dict_set_checked(vm, dict, key, &vm->none_object.base, &error);
                }
                TINYPY_DECREF(key);
            }
            tinypy_internal_vm_deallocate(vm, keys, capacity * sizeof(*keys));
        }
        if (error != NULL) {
            tinypy_error_release(error);
        }
        tinypy_internal_exception_preserve_end(vm, &state);
    }
    module->name = NULL;
    module->dict = NULL;
    if (name != NULL) {
        visit(name, user_data);
    }
    if (dict != NULL) {
        visit(dict, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_module_dict(const tinypy_value_t *module) {
    tinypy_value_t *return_value_1 = TINYPY_MODULE_OBJECT((tinypy_value_t *)module)->dict;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_module_name(const tinypy_value_t *module) {
    tinypy_value_t *return_value_1 = TINYPY_MODULE_OBJECT((tinypy_value_t *)module)->name;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_module_swap_dict(tinypy_value_t *left_value, tinypy_value_t *right_value) {
    tinypy_module_object_t *left = TINYPY_MODULE_OBJECT(left_value);
    tinypy_module_object_t *right = TINYPY_MODULE_OBJECT(right_value);
    tinypy_value_t *temporary = left->dict;
    left->dict = right->dict;
    right->dict = temporary;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_module_add_value(tinypy_value_t *module_value, const char *name, size_t name_size, tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(module_value);
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);

    tinypy_module_add_value_key(module_value, key, value);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_module_add_value_key(tinypy_value_t *module_value, tinypy_value_t *key, tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(module_value);
    tinypy_module_object_t *module = TINYPY_MODULE_OBJECT(module_value);

    if (module->dict == NULL) {
        module->dict = tinypy_dict_new(vm);
    }
    tinypy_dict_set(module->dict, key, value);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NATIVE_FUNCTION) {
        tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(value);

        if (function->module == NULL && module->name != NULL) {
            function->module = TINYPY_RET(module->name);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_module_get_value(tinypy_value_t *module_value, const char *name, size_t name_size) {
    if (TINYPY_MODULE_OBJECT(module_value)->dict == NULL) {
        return NULL;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(module_value);
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
    tinypy_value_t *value = tinypy_module_get_value_key(module_value, key);
    TINYPY_DECREF(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_module_get_value_key(tinypy_value_t *module_value, tinypy_value_t *key) {
    tinypy_module_object_t *module = TINYPY_MODULE_OBJECT(module_value);

    if (module->dict == NULL) {
        return NULL;
    }
    tinypy_value_t *value = tinypy_dict_get_optional(module->dict, key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_modules(const tinypy_vm_t *vm) {
    return vm->modules;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_vm_set_module_finder(tinypy_vm_t *vm, tinypy_value_t *finder) {
    if (finder != NULL) {
        TINYPY_INCREF(finder);
    }
    tinypy_value_t *previous = vm->module_finder;
    vm->module_finder = finder;
    if (previous != NULL) {
        TINYPY_DECREF(previous);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_module_finder(const tinypy_vm_t *vm) {
    return vm->module_finder;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_module_initialize(tinypy_value_t *value, tinypy_value_t *name, tinypy_value_t *doc, tinypy_error_t **out_error) {
    tinypy_module_object_t *module = TINYPY_MODULE_OBJECT(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *internal_name_key = vm->internal_special_name_key;
    tinypy_value_t *internal_doc_key = vm->internal_special_doc_key;

    if (module->dict == NULL) {
        module->dict = tinypy_dict_new(vm);
    }
    TINYPY_INCREF(name);
    tinypy_value_t *previous_name = module->name;
    module->name = name;
    if (previous_name != NULL) {
        TINYPY_DECREF(previous_name);
    }
    if (tinypy_internal_dict_set_checked(vm, module->dict, internal_name_key, name, out_error) == 0 || tinypy_internal_dict_set_checked(vm, module->dict, internal_doc_key, doc, out_error) == 0) {
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_module_arguments(tinypy_vm_t *vm, tinypy_value_t *args, size_t offset, tinypy_value_t *kwargs, tinypy_value_t **out_name, tinypy_value_t **out_doc, tinypy_error_t **out_error) {
    tinypy_value_t *const names[] = {vm->internal_name_key, vm->internal_doc_key};
    size_t count = TINYPY_TUPLE_SIZE(args) - offset;
    size_t keywords = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    size_t recognized = 0U;
    tinypy_value_t *values[2] = {NULL, NULL};

    *out_name = NULL;
    *out_doc = NULL;
    if (count > 2U || keywords > 2U - count) {
        tinypy_internal_make_arity_error(vm, "module.__init__", 15U, count + keywords, 0U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return TINYPY_FALSE;
    }
    for (size_t index = 0U; index < 2U; ++index) {
        tinypy_value_t *keyword = recognized < keywords ? tinypy_internal_constructor_keyword_optional(kwargs, names[index]) : NULL;

        values[index] = index < count ? TINYPY_TUPLE_GET(args, offset + index) : NULL;
        if (keyword != NULL) {
            recognized += 1U;
            if (values[index] != NULL) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("Argument given by name ('"),
                    TINYPY_MESSAGE_PART_TEXT(names[index]),
                    TINYPY_MESSAGE_PART_LITERAL("') and position ("),
                    {index == 0U ? "1" : "2", 1U},
                    TINYPY_MESSAGE_PART_LITERAL(")")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 5U, out_error);
                goto failure;
            }
            values[index] = keyword;
        }
        if (index == 0U && values[index] == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "Required argument 'name' (pos 1) not found", out_error);
            goto failure;
        }
        if (index == 0U && TINYPY_VALUE_KIND(values[index]) != TINYPY_VALUE_STRING) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("module.__init__() argument 1 must be string, not "),
                {TINYPY_VALUE_KIND(values[index]) == TINYPY_VALUE_NONE ? "None" : values[index]->type->name,
                 TINYPY_VALUE_KIND(values[index]) == TINYPY_VALUE_NONE ? 4U : values[index]->type->name_size}
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
            goto failure;
        }
        if (values[index] == NULL) {
            values[index] = &vm->none_object.base;
        }
        TINYPY_INCREF(values[index]);
        if (index == 0U) {
            *out_name = values[index];
        }
        else {
            *out_doc = values[index];
        }
    }
    if (recognized != keywords) {
        tinypy_dict_entry_t *entry = TINYPY_DICT_ITERATOR_BEGIN(kwargs);
        tinypy_dict_entry_t *end = TINYPY_DICT_ITERATOR_END(kwargs);

        for (; entry != end; ++entry) {
            if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry) != 0
                && !((TINYPY_NAME_EQ(entry->key, vm->internal_name_key) != TINYPY_FALSE)
                     || (TINYPY_NAME_EQ(entry->key, vm->internal_doc_key) != TINYPY_FALSE))) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("'"),
                    {(const char *)TINYPY_TEXT_BYTES(entry->key), TINYPY_TEXT_BYTE_SIZE(entry->key)},
                    TINYPY_MESSAGE_PART_LITERAL("' is an invalid keyword argument for this function")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
                goto failure;
            }
        }
    }
    return TINYPY_TRUE;
failure:
    if (*out_name != NULL) {
        TINYPY_DECREF(*out_name);
        *out_name = NULL;
    }
    if (*out_doc != NULL) {
        TINYPY_DECREF(*out_doc);
        *out_doc = NULL;
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_module_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_module_object_t *module;
    tinypy_value_t *name;
    tinypy_value_t *doc;

    if (__tinypy_module_arguments(vm, args, 0U, kwargs, &name, &doc, out_error) == 0) {
        return NULL;
    }
    module = (tinypy_module_object_t *)tinypy_internal_object_allocate_checked(vm, type, type->basic_size, out_error);
    if (module == NULL) {
        TINYPY_DECREF(name);
        TINYPY_DECREF(doc);
        return NULL;
    }
    tinypy_bool_t initialized = __tinypy_module_initialize(&module->base, name, doc, out_error);
    TINYPY_DECREF(name);
    TINYPY_DECREF(doc);
    if (initialized == 0) {
        TINYPY_DECREF(&module->base);
        return NULL;
    }
    return &module->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_module_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *self;
    tinypy_value_t *name;
    tinypy_value_t *doc;

    (void)user_data;
    if (count == 0U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_MODULE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "module.__init__ received invalid arguments", out_error);
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_module_arguments(vm, args, 1U, kwargs, &name, &doc, out_error) == 0) {
        return NULL;
    }
    tinypy_bool_t initialized = __tinypy_module_initialize(self, name, doc, out_error);
    TINYPY_DECREF(name);
    TINYPY_DECREF(doc);
    if (initialized == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_module_type(tinypy_vm_t *vm) {
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_MODULE];
    tinypy_internal_type_add_method(type, vm->internal_special_init_key, __tinypy_module_init_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);

    type->create = tinypy_internal_module_create;
    tinypy_internal_constructor_add_builtin_new(type);
}
