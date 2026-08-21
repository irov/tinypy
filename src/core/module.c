#include "tinypy/module.h"

#include "internal.h"

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_module_from_dict(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *dict) {
    tinypy_module_object_t *module = (tinypy_module_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_MODULE, sizeof(*module));
    module->name = tinypy_string_from_bytes(vm, name, name_size);
    module->dict = dict;
    TINYPY_INCREF(dict);
    return &module->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_module_new(tinypy_vm_t *vm, const char *name, size_t name_size) {
    tinypy_value_t *dict = tinypy_dict_new(vm);
    tinypy_value_t *module = tinypy_internal_module_from_dict(vm, name, name_size, dict);
    TINYPY_DECREF(dict);
    return module;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_module_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_module_object_t *module = TINYPY_MODULE_OBJECT(value);

    visit(module->name, user_data);
    visit(module->dict, user_data);
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
    tinypy_module_object_t *module = TINYPY_MODULE_OBJECT(module_value);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_dict_set(module->dict, key, value);
    TINYPY_DECREF(key);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NATIVE_FUNCTION) {
        tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(value);

        if (function->module == NULL) {
            function->module = module->name;
            TINYPY_INCREF(function->module);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_module_get_value(tinypy_value_t *module_value, const char *name, size_t name_size) {
    tinypy_value_t *value;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(module_value);
    tinypy_module_object_t *module = TINYPY_MODULE_OBJECT(module_value);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    value = tinypy_dict_get_optional(module->dict, key);
    TINYPY_DECREF(key);
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
    tinypy_value_t *name_key = tinypy_string_from_bytes(vm, "__name__", 8U);
    tinypy_value_t *doc_key = tinypy_string_from_bytes(vm, "__doc__", 7U);

    if (TINYPY_VALUE_KIND(name) != TINYPY_VALUE_STRING) {
        TINYPY_DECREF(doc_key);
        TINYPY_DECREF(name_key);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "module name must be a string", out_error);
        return TINYPY_FALSE;
    }
    TINYPY_INCREF(name);
    if (module->name != NULL) {
        TINYPY_DECREF(module->name);
    }
    module->name = name;
    if (tinypy_internal_dict_set_checked(vm, module->dict, name_key, name, out_error) == 0 || tinypy_internal_dict_set_checked(vm, module->dict, doc_key, doc, out_error) == 0) {
        TINYPY_DECREF(doc_key);
        TINYPY_DECREF(name_key);
        return TINYPY_FALSE;
    }
    TINYPY_DECREF(doc_key);
    TINYPY_DECREF(name_key);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_module_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_module_object_t *module;
    tinypy_value_t *doc;

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < 1U || count > 2U || tinypy_type_is_subtype(type, &vm->types[TINYPY_VALUE_MODULE]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "module() requires a name and optional doc string", out_error);
        return NULL;
    }
    module = (tinypy_module_object_t *)tinypy_internal_object_allocate(vm, type, type->basic_size);
    module->dict = tinypy_dict_new(vm);
    doc = count == 2U ? TINYPY_TUPLE_GET(args, 1U) : &vm->none_object.base;
    if (__tinypy_module_initialize(&module->base, TINYPY_TUPLE_GET(args, 0U), doc, out_error) == 0) {
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
    tinypy_value_t *doc;

    (void)user_data;
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < 2U || count > 3U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_MODULE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "module.__init__ received invalid arguments", out_error);
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    doc = count == 3U ? TINYPY_TUPLE_GET(args, 2U) : &vm->none_object.base;
    if (__tinypy_module_initialize(self, TINYPY_TUPLE_GET(args, 1U), doc, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_module_type(tinypy_vm_t *vm) {
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_MODULE];
    tinypy_value_t *init = tinypy_native_function_new(vm, "__init__", 8U, __tinypy_module_init_method, NULL, NULL);

    type->create = tinypy_internal_module_create;
    tinypy_internal_constructor_add_builtin_new(type);
    tinypy_type_set_attr(type, "__init__", 8U, init);
    TINYPY_DECREF(init);
}
