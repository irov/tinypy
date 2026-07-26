#include "tinypy/module.h"

#include "internal.h"

#include "assertion.h"
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_module_from_dict(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *dict) {
    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    TINYPY_ASSERT(name != NULL || name_size == 0U);
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, dict));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(dict) == TINYPY_VALUE_DICT);
    tinypy_module_object_t *module = (tinypy_module_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_MODULE, sizeof(*module));
    module->name = tinypy_string_from_bytes(vm, name, name_size);
    module->dict = dict;
    TINYPY_INCREF(dict);
    return &module->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_module_new(tinypy_vm_t *vm, const char *name, size_t name_size) {
    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    TINYPY_ASSERT(name != NULL || name_size == 0U);
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
    TINYPY_ASSERT(module != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(module)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(module) == TINYPY_VALUE_MODULE);
    return TINYPY_MODULE_OBJECT((tinypy_value_t *)module)->dict;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_module_name(const tinypy_value_t *module) {
    TINYPY_ASSERT(module != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(module)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(module) == TINYPY_VALUE_MODULE);
    return TINYPY_MODULE_OBJECT((tinypy_value_t *)module)->name;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_module_add_value(tinypy_value_t *module_value, const char *name, size_t name_size, tinypy_value_t *value) {
    TINYPY_ASSERT(module_value != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(module_value);
    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(module_value) == TINYPY_VALUE_MODULE);
    TINYPY_ASSERT(name != NULL || name_size == 0U);
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, value));
    tinypy_module_object_t *module = TINYPY_MODULE_OBJECT(module_value);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_dict_set(module->dict, key, value);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_module_get_value(tinypy_value_t *module_value, const char *name, size_t name_size) {
    tinypy_value_t *value;

    TINYPY_ASSERT(module_value != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(module_value);
    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(module_value) == TINYPY_VALUE_MODULE);
    TINYPY_ASSERT(name != NULL || name_size == 0U);
    tinypy_module_object_t *module = TINYPY_MODULE_OBJECT(module_value);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    value = tinypy_dict_get_optional(module->dict, key);
    TINYPY_DECREF(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_modules(const tinypy_vm_t *vm) {
    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    return vm->modules;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_vm_set_module_finder(tinypy_vm_t *vm, tinypy_value_t *finder) {
    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    TINYPY_ASSERT(finder == NULL || tinypy_internal_value_belongs_to(vm, finder));
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
    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    return vm->module_finder;
}
