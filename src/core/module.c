#include "tinypy/module.h"

#include "internal.h"

#include <assert.h>

static tinypy_module_object_t *__tinypy_internal_module_validate(const tinypy_value_t *value)
{
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_MODULE);
    return TINYPY_MODULE_OBJECT((tinypy_value_t *)value);
}

tinypy_value_t *tinypy_internal_module_from_dict(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *dict)
{
    tinypy_module_object_t *module;

    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    assert(tinypy_internal_value_belongs_to(vm, dict));
    assert(tinypy_internal_value_kind(dict) == TINYPY_VALUE_DICT);
    module = (tinypy_module_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_MODULE, sizeof(*module));
    module->name = tinypy_string_from_bytes(vm, name, name_size);
    module->dict = dict;
    tinypy_retain(dict);
    return &module->base;
}

tinypy_value_t *tinypy_module_new(tinypy_vm_t *vm, const char *name, size_t name_size)
{
    tinypy_value_t *dict;
    tinypy_value_t *module;

    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    dict = tinypy_dict_new(vm);
    module = tinypy_internal_module_from_dict(vm, name, name_size, dict);
    tinypy_release(dict);
    return module;
}

void tinypy_internal_module_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data)
{
    tinypy_module_object_t *module = TINYPY_MODULE_OBJECT(value);

    visit(module->name, user_data);
    visit(module->dict, user_data);
}

tinypy_value_t *tinypy_module_dict(const tinypy_value_t *module)
{
    return __tinypy_internal_module_validate(module)->dict;
}

tinypy_value_t *tinypy_module_name(const tinypy_value_t *module)
{
    return __tinypy_internal_module_validate(module)->name;
}

void tinypy_module_add_value(tinypy_value_t *module_value, const char *name, size_t name_size, tinypy_value_t *value)
{
    tinypy_module_object_t *module = __tinypy_internal_module_validate(module_value);
    tinypy_vm_t *vm = tinypy_internal_value_vm(module_value);
    tinypy_value_t *key;

    assert(name != NULL || name_size == 0U);
    assert(tinypy_internal_value_belongs_to(vm, value));
    key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_dict_set(module->dict, key, value);
    tinypy_release(key);
}

tinypy_value_t *tinypy_module_get_value(tinypy_value_t *module_value, const char *name, size_t name_size)
{
    tinypy_module_object_t *module = __tinypy_internal_module_validate(module_value);
    tinypy_value_t *key;
    tinypy_value_t *value;

    assert(name != NULL || name_size == 0U);
    key = tinypy_string_from_bytes(tinypy_internal_value_vm(module_value), name, name_size);
    value = tinypy_dict_contains(module->dict, key) != 0 ? tinypy_dict_get(module->dict, key) : NULL;
    tinypy_release(key);
    return value;
}

tinypy_value_t *tinypy_vm_modules(const tinypy_vm_t *vm)
{
    assert(tinypy_internal_vm_valid(vm));
    return vm->modules;
}
