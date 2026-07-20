#include "tinypy/module.h"

#include "tinypy/eval.h"
#include "tinypy/compiler.h"
#include "tinypy/marshal.h"
#include "internal.h"

#include <assert.h>
#include <string.h>

static tinypy_value_t *__tinypy_import_dict_value(tinypy_vm_t *vm, tinypy_value_t *dict, const char *name, size_t name_size)
{
    tinypy_value_t *key;
    tinypy_value_t *value;

    if (dict == NULL) return NULL;
    key = tinypy_string_from_bytes(vm, name, name_size);
    value = tinypy_dict_contains(dict, key) != 0 ? tinypy_dict_get(dict, key) : NULL;
    tinypy_release(key);
    return value;
}

static int __tinypy_import_text_view(tinypy_value_t *value, const char **out_bytes, size_t *out_size)
{
    if (value == NULL) return 0;
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING) {
        *out_bytes = (const char *)tinypy_string_view(value, out_size);
        return 1;
    }
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_UNICODE) {
        size_t code_points;

        *out_bytes = tinypy_unicode_utf8_view(value, out_size, &code_points);
        return 1;
    }
    return 0;
}

static int __tinypy_import_package_view(tinypy_value_t *globals, const char **out_bytes, size_t *out_size)
{
    const char *package_bytes = NULL;
    size_t package_size = 0U;

    assert(out_bytes != NULL);
    assert(out_size != NULL);
    if (globals != NULL) {
        tinypy_value_t *package = __tinypy_import_dict_value(
            tinypy_internal_value_vm(globals), globals, "__package__", 11U);

        if (__tinypy_import_text_view(package, &package_bytes, &package_size) == 0 || package_size == 0U) {
            tinypy_vm_t *vm = tinypy_internal_value_vm(globals);
            tinypy_value_t *module_name = __tinypy_import_dict_value(vm, globals, "__name__", 8U);
            tinypy_value_t *path = __tinypy_import_dict_value(vm, globals, "__path__", 8U);

            if (__tinypy_import_text_view(module_name, &package_bytes, &package_size) != 0 && path == NULL) {
                while (package_size != 0U && package_bytes[package_size - 1U] != '.') package_size -= 1U;
                if (package_size != 0U) package_size -= 1U;
            }
        }
    }
    *out_bytes = package_bytes;
    *out_size = package_size;
    return package_bytes != NULL && package_size != 0U;
}

static char *__tinypy_import_canonical_name(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *globals, int32_t level, size_t *out_size, tinypy_error_t **out_error)
{
    const char *package_bytes = NULL;
    size_t package_size = 0U;
    char *canonical;
    size_t base_size;
    int32_t ascent;

    assert(name != NULL || name_size == 0U);
    if (level <= 0) {
        if (name_size == 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "absolute import name is empty", out_error);
            return NULL;
        }
        canonical = (char *)tinypy_internal_vm_allocate(vm, name_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        (void)memcpy(canonical, name, name_size);
        *out_size = name_size;
        return canonical;
    }
    if (__tinypy_import_package_view(globals, &package_bytes, &package_size) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "relative import has no known parent package", out_error);
        return NULL;
    }
    base_size = package_size;
    for (ascent = 1; ascent < level; ascent += 1) {
        while (base_size != 0U && package_bytes[base_size - 1U] != '.') base_size -= 1U;
        if (base_size == 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "relative import goes beyond the top-level package", out_error);
            return NULL;
        }
        base_size -= 1U;
    }
    assert(base_size != 0U);
    assert(base_size <= SIZE_MAX - name_size - (name_size != 0U ? 1U : 0U));
    *out_size = base_size + name_size + (name_size != 0U ? 1U : 0U);
    canonical = (char *)tinypy_internal_vm_allocate(vm, *out_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    (void)memcpy(canonical, package_bytes, base_size);
    if (name_size != 0U) {
        canonical[base_size] = '.';
        (void)memcpy(canonical + base_size + 1U, name, name_size);
    }
    return canonical;
}

static void __tinypy_import_set_metadata(tinypy_vm_t *vm, tinypy_value_t *module, const char *name, size_t name_size, const tinypy_module_artifact_t *artifact)
{
    tinypy_value_t *value = tinypy_string_from_bytes(vm, name, name_size);
    size_t package_size = name_size;

    tinypy_module_add_value(module, "__name__", 8U, value);
    tinypy_release(value);
    if ((artifact->flags & TINYPY_MODULE_ARTIFACT_PACKAGE) == 0U) {
        while (package_size != 0U && name[package_size - 1U] != '.') package_size -= 1U;
        if (package_size != 0U) package_size -= 1U;
    }
    value = tinypy_string_from_bytes(vm, name, package_size);
    tinypy_module_add_value(module, "__package__", 11U, value);
    tinypy_release(value);
    if (artifact->logical_filename != NULL || artifact->logical_filename_size != 0U) {
        value = tinypy_string_from_bytes(vm, artifact->logical_filename, artifact->logical_filename_size);
        tinypy_module_add_value(module, "__file__", 8U, value);
        tinypy_release(value);
    }
    tinypy_module_add_value(module, "__builtins__", 12U, vm->builtins);
    if ((artifact->flags & TINYPY_MODULE_ARTIFACT_PACKAGE) != 0U) {
        value = tinypy_list_from_items(vm, NULL, 0U);
        tinypy_module_add_value(module, "__path__", 8U, value);
        tinypy_release(value);
    }
}

static int __tinypy_import_artifact_valid(const tinypy_module_artifact_t *artifact, const char *name, size_t name_size)
{
    static const uint32_t compile_features = (uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR | (uint32_t)TINYPY_COMPILE_FEATURE_META;

    if (artifact == NULL || artifact->abi_version != TINYPY_ABI_VERSION || artifact->struct_size < (uint32_t)offsetof(tinypy_module_artifact_t, compile_feature_flags)) return 0;
    if (artifact->content_kind < TINYPY_MODULE_CONTENT_SOURCE || artifact->content_kind > TINYPY_MODULE_CONTENT_NATIVE) return 0;
    if (artifact->canonical_name != NULL || artifact->canonical_name_size != 0U) {
        if (artifact->canonical_name == NULL || artifact->canonical_name_size != name_size || memcmp(artifact->canonical_name, name, name_size) != 0) return 0;
    }
    if ((artifact->content_kind == TINYPY_MODULE_CONTENT_SOURCE || artifact->content_kind == TINYPY_MODULE_CONTENT_MARSHAL_V2) && artifact->data == NULL && artifact->data_size != 0U) return 0;
    if (artifact->content_kind == TINYPY_MODULE_CONTENT_NATIVE && artifact->native_initialize == NULL) return 0;
    if ((size_t)artifact->struct_size >= offsetof(tinypy_module_artifact_t, build_profile) + sizeof(artifact->build_profile)) {
        if (artifact->compile_optimize_level < 0 || artifact->compile_optimize_level > 2 || (artifact->compile_feature_flags & ~compile_features) != 0U) return 0;
        if (((artifact->compile_feature_flags & (uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR) != 0U) != (artifact->build_profile != NULL)) return 0;
        if (artifact->build_profile != NULL && tinypy_build_profile_optimize_level(artifact->build_profile) != artifact->compile_optimize_level) return 0;
    }
    return 1;
}

static int __tinypy_import_has_compile_environment(const tinypy_module_artifact_t *artifact)
{
    return (size_t)artifact->struct_size >= offsetof(tinypy_module_artifact_t, build_profile) + sizeof(artifact->build_profile);
}

static uint32_t __tinypy_import_compile_feature_flags(const tinypy_module_artifact_t *artifact)
{
    if (__tinypy_import_has_compile_environment(artifact) == 0) return 0U;
    return artifact->compile_feature_flags;
}

static const tinypy_build_profile_t *__tinypy_import_build_profile(const tinypy_module_artifact_t *artifact)
{
    if (__tinypy_import_has_compile_environment(artifact) == 0) return NULL;
    return artifact->build_profile;
}

static int32_t __tinypy_import_compile_optimize_level(const tinypy_module_artifact_t *artifact, int32_t default_level)
{
    const tinypy_build_profile_t *profile = __tinypy_import_build_profile(artifact);

    if (profile != NULL) return (int32_t)tinypy_build_profile_optimize_level(profile);
    if (__tinypy_import_has_compile_environment(artifact) == 0) return default_level;
    assert(artifact->compile_optimize_level >= 0 && artifact->compile_optimize_level <= 2);
    return artifact->compile_optimize_level;
}

static void __tinypy_import_make_not_found_error(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_error_t **out_error)
{
    static const char prefix[] = "No module named ";
    size_t message_size;
    char *message;

    assert(name != NULL);
    assert(name_size <= SIZE_MAX - (sizeof(prefix) - 1U) - 1U);
    message_size = (sizeof(prefix) - 1U) + name_size;
    message = (char *)tinypy_internal_vm_allocate(vm, message_size + 1U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    (void)memcpy(message, prefix, sizeof(prefix) - 1U);
    if (name_size != 0U) (void)memcpy(message + sizeof(prefix) - 1U, name, name_size);
    message[message_size] = '\0';
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, message, out_error);
    tinypy_internal_vm_deallocate(vm, message, message_size + 1U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
}

static tinypy_value_t *__tinypy_import_load_one(tinypy_vm_t *vm, const char *name, size_t name_size, const char *importer, size_t importer_size, int *out_not_found, tinypy_error_t **out_error)
{
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *module;
    const tinypy_module_artifact_t *artifact;
    tinypy_module_request_t request;
    int loaded = 0;

    assert(out_not_found != NULL);
    *out_not_found = 0;

    if (tinypy_dict_contains(vm->modules, key) != 0) {
        module = tinypy_dict_get(vm->modules, key);
        tinypy_retain(module);
        tinypy_release(key);
        return module;
    }
    if (vm->has_host == 0 || vm->host.resolve_module == NULL) {
        tinypy_release(key);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "no host module resolver is installed", out_error);
        return NULL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.abi_version = TINYPY_ABI_VERSION;
    request.struct_size = (uint32_t)sizeof(request);
    request.canonical_name = name;
    request.canonical_name_size = name_size;
    request.importer_name = importer;
    request.importer_name_size = importer_size;
    artifact = vm->host.resolve_module(vm->host.user_data, &request);
    if (artifact == NULL) {
        *out_not_found = 1;
        tinypy_release(key);
        __tinypy_import_make_not_found_error(vm, name, name_size, out_error);
        return NULL;
    }
    if (__tinypy_import_artifact_valid(artifact, name, name_size) == 0) {
        vm->host.release_module_artifact(vm->host.user_data, artifact);
        tinypy_release(key);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "host resolver returned an invalid module artifact", out_error);
        return NULL;
    }
    module = tinypy_module_new(vm, name, name_size);
    __tinypy_import_set_metadata(vm, module, name, name_size, artifact);
    tinypy_dict_set(vm->modules, key, module);
    if (artifact->content_kind == TINYPY_MODULE_CONTENT_MARSHAL_V2) {
        tinypy_marshal_error_t marshal_error;
        tinypy_value_t *code = NULL;
        tinypy_marshal_result_e marshal_result = tinypy_marshal_load_code_v2(vm, artifact->data, artifact->data_size, NULL, &code, &marshal_error);

        if (marshal_result == TINYPY_MARSHAL_OK) {
            uint32_t feature_flags = __tinypy_import_compile_feature_flags(artifact);
            const tinypy_build_profile_t *build_profile = __tinypy_import_build_profile(artifact);
            int32_t optimize_level = __tinypy_import_compile_optimize_level(artifact, vm->optimize_level);
            tinypy_value_t *eval_result;

            if (__tinypy_import_has_compile_environment(artifact) != 0) tinypy_internal_code_attach_compile_options(code, feature_flags, optimize_level, build_profile);
            eval_result = tinypy_eval_code(code, tinypy_module_dict(module), NULL, out_error);
            tinypy_release(code);
            if (eval_result != NULL) {
                tinypy_release(eval_result);
                loaded = 1;
            }
        } else {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "module marshal artifact is invalid", out_error);
        }
    } else if (artifact->content_kind == TINYPY_MODULE_CONTENT_NATIVE) {
        loaded = artifact->native_initialize(module, artifact->native_user_data, out_error) != 0;
        if (loaded == 0 && (out_error == NULL || *out_error == NULL)) tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "native module initialization failed", out_error);
    } else {
        tinypy_compile_options_t options;
        tinypy_value_t *code;
        const char *logical_filename = artifact->logical_filename != NULL ? artifact->logical_filename : name;
        size_t logical_filename_size = artifact->logical_filename != NULL ? artifact->logical_filename_size : name_size;

        tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
        options.dont_inherit = 1;
        options.optimize_level = __tinypy_import_compile_optimize_level(artifact, vm->optimize_level);
        options.feature_flags = __tinypy_import_compile_feature_flags(artifact);
        options.build_profile = __tinypy_import_build_profile(artifact);
        code = tinypy_compile_source(vm, artifact->data, artifact->data_size, logical_filename, logical_filename_size, &options, out_error);
        if (code != NULL) {
            tinypy_value_t *exec_result = tinypy_exec_code(code, tinypy_module_dict(module), NULL, out_error);

            tinypy_release(code);
            if (exec_result != NULL) {
                tinypy_release(exec_result);
                loaded = 1;
            }
        }
    }
    vm->host.release_module_artifact(vm->host.user_data, artifact);
    if (loaded == 0) {
        tinypy_dict_delete(vm->modules, key);
        tinypy_dict_clear(tinypy_module_dict(module));
        tinypy_release(module);
        tinypy_release(key);
        return NULL;
    }
    tinypy_release(key);
    return module;
}

static tinypy_value_t *__tinypy_import_load_path(tinypy_vm_t *vm, const char *name, size_t name_size, const char *importer, size_t importer_size, size_t return_name_size, int *out_not_found, tinypy_error_t **out_error)
{
    tinypy_value_t *selected = NULL;
    tinypy_value_t *parent = NULL;
    size_t component_start = 0U;
    size_t offset;

    assert(return_name_size != 0U);
    assert(return_name_size <= name_size);
    assert(out_not_found != NULL);
    *out_not_found = 0;

    for (offset = 0U; offset <= name_size; offset += 1U) {
        if (offset != name_size && name[offset] != '.') continue;
        if (offset == component_start) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "module name contains an empty component", out_error);
            goto failure;
        }
        {
            int not_found = 0;
            tinypy_value_t *module = __tinypy_import_load_one(vm, name, offset, importer, importer_size, &not_found, out_error);

            if (module == NULL) {
                *out_not_found = not_found;
                goto failure;
            }
            if (offset == return_name_size) {
                selected = module;
                tinypy_retain(selected);
            }
            if (parent != NULL) {
                tinypy_module_add_value(parent, name + component_start, offset - component_start, module);
                tinypy_release(parent);
            }
            parent = module;
        }
        component_start = offset + 1U;
    }
    tinypy_release(parent);
    assert(selected != NULL);
    return selected;
failure:
    if (parent != NULL) tinypy_release(parent);
    if (selected != NULL) tinypy_release(selected);
    return NULL;
}

static size_t __tinypy_import_first_component_size(const char *name, size_t name_size)
{
    size_t size = 0U;

    while (size != name_size && name[size] != '.') size += 1U;
    return size;
}

static void __tinypy_import_discard_error(tinypy_vm_t *vm, tinypy_error_t **out_error)
{
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
    }
    tinypy_internal_exception_clear_raised(vm);
}

tinypy_value_t *tinypy_import_module(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *globals, tinypy_value_t *fromlist, int32_t level, tinypy_error_t **out_error)
{
    const char *importer = NULL;
    size_t importer_size = 0U;
    size_t canonical_size;
    size_t return_name_size;
    char *canonical;
    int return_full;
    int not_found = 0;
    tinypy_value_t *result;

    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    assert(globals == NULL || (tinypy_internal_value_belongs_to(vm, globals) && tinypy_internal_value_kind(globals) == TINYPY_VALUE_DICT));
    assert(fromlist == NULL || tinypy_internal_value_belongs_to(vm, fromlist));
    tinypy_internal_clear_error(out_error);
    if (globals != NULL) (void)__tinypy_import_text_view(__tinypy_import_dict_value(vm, globals, "__name__", 8U), &importer, &importer_size);
    return_full = fromlist != NULL && tinypy_internal_value_kind(fromlist) != TINYPY_VALUE_NONE && (!(tinypy_internal_value_kind(fromlist) == TINYPY_VALUE_TUPLE) || tinypy_tuple_size(fromlist) != 0U);
    if (level < 0) {
        const char *package_bytes;
        size_t package_size;

        if (__tinypy_import_package_view(globals, &package_bytes, &package_size) != 0) {
            assert(name_size != 0U);
            assert(package_size <= SIZE_MAX - name_size - 1U);
            canonical_size = package_size + name_size + 1U;
            canonical = (char *)tinypy_internal_vm_allocate(vm, canonical_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            (void)memcpy(canonical, package_bytes, package_size);
            canonical[package_size] = '.';
            (void)memcpy(canonical + package_size + 1U, name, name_size);
            return_name_size = return_full != 0
                ? canonical_size
                : package_size + 1U + __tinypy_import_first_component_size(name, name_size);
            result = __tinypy_import_load_path(vm, canonical, canonical_size, importer, importer_size, return_name_size, &not_found, out_error);
            tinypy_internal_vm_deallocate(vm, canonical, canonical_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            if (result != NULL || not_found == 0) return result;
            __tinypy_import_discard_error(vm, out_error);
        }
        level = 0;
    }
    canonical = __tinypy_import_canonical_name(vm, name, name_size, globals, level, &canonical_size, out_error);
    if (canonical == NULL) return NULL;
    return_name_size = return_full != 0 || level > 0
        ? canonical_size
        : __tinypy_import_first_component_size(canonical, canonical_size);
    result = __tinypy_import_load_path(vm, canonical, canonical_size, importer, importer_size, return_name_size, &not_found, out_error);
    tinypy_internal_vm_deallocate(vm, canonical, canonical_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}

tinypy_value_t *tinypy_internal_import_from(tinypy_value_t *module, const char *name, size_t name_size, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(module);
    tinypy_value_t *value;
    const char *module_name;
    size_t module_name_size;
    char *full_name;
    int not_found;

    assert(tinypy_internal_value_kind(module) == TINYPY_VALUE_MODULE);
    value = tinypy_module_get_value(module, name, name_size);
    if (value != NULL) {
        tinypy_retain(value);
        return value;
    }
    module_name = (const char *)tinypy_string_view(tinypy_module_name(module), &module_name_size);
    assert(module_name_size <= SIZE_MAX - name_size - 1U);
    full_name = (char *)tinypy_internal_vm_allocate(vm, module_name_size + name_size + 1U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    (void)memcpy(full_name, module_name, module_name_size);
    full_name[module_name_size] = '.';
    (void)memcpy(full_name + module_name_size + 1U, name, name_size);
    value = __tinypy_import_load_path(vm, full_name, module_name_size + name_size + 1U, module_name, module_name_size, module_name_size + name_size + 1U, &not_found, out_error);
    tinypy_internal_vm_deallocate(vm, full_name, module_name_size + name_size + 1U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return value;
}

int32_t tinypy_internal_import_star(tinypy_value_t *module, tinypy_value_t *locals, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(module);
    tinypy_value_t *dict;
    tinypy_value_t *all;
    size_t index;

    assert(tinypy_internal_value_kind(module) == TINYPY_VALUE_MODULE);
    assert(tinypy_internal_value_kind(locals) == TINYPY_VALUE_DICT);
    tinypy_internal_clear_error(out_error);
    dict = tinypy_module_dict(module);
    all = tinypy_module_get_value(module, "__all__", 7U);
    if (all != NULL) {
        size_t size;

        if (tinypy_internal_value_kind(all) == TINYPY_VALUE_TUPLE) size = tinypy_tuple_size(all);
        else if (tinypy_internal_value_kind(all) == TINYPY_VALUE_LIST) size = tinypy_list_size(all);
        else {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "module __all__ is not a sequence", out_error);
            return 0;
        }
        for (index = 0U; index < size; index += 1U) {
            tinypy_value_t *key = tinypy_internal_value_kind(all) == TINYPY_VALUE_TUPLE ? tinypy_tuple_get(all, index) : tinypy_list_get(all, index);

            if (tinypy_internal_value_kind(key) != TINYPY_VALUE_STRING || tinypy_dict_contains(dict, key) == 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "module __all__ names a missing attribute", out_error);
                return 0;
            }
            tinypy_dict_set(locals, key, tinypy_dict_get(dict, key));
        }
        return 1;
    }
    {
        tinypy_dict_object_t *module_dict = TINYPY_DICT_OBJECT(dict);

        for (index = 0U; index <= module_dict->mask; index += 1U) {
            tinypy_dict_entry_t *entry = &module_dict->table[index];
            const unsigned char *bytes;
            size_t size;

            if (entry->state != TINYPY_DICT_ENTRY_ACTIVE || tinypy_internal_value_kind(entry->key) != TINYPY_VALUE_STRING) continue;
            bytes = (const unsigned char *)tinypy_string_view(entry->key, &size);
            if (size != 0U && bytes[0] == '_') continue;
            tinypy_dict_set(locals, entry->key, entry->value);
        }
    }
    return 1;
}
