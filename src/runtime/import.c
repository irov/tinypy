#include "tinypy/module.h"

#include "tinypy/eval.h"
#include "tinypy/compiler.h"
#include "tinypy/marshal.h"
#include "internal.h"

#include <string.h>
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_import_dict_value(tinypy_vm_t *vm, tinypy_value_t *dict, const char *name, size_t name_size) {
    if (dict == NULL) {
        return NULL;
    }
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *value = tinypy_dict_get_optional(dict, key);
    TINYPY_DECREF(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_import_text_view(tinypy_value_t *value, const char **out_bytes, size_t *out_size) {
    if (value == NULL) {
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        *out_bytes = (const char *)tinypy_string_view(value, out_size);
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        size_t code_points;

        *out_bytes = tinypy_unicode_utf8_view(value, out_size, &code_points);
        return TINYPY_TRUE;
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* Follows get_parent in Python 2.7: an empty result means absolute imports
   only. Relative-import failures raise ValueError as CPython does. */
static tinypy_bool_t __tinypy_import_parent_package(tinypy_vm_t *vm, tinypy_value_t *globals, int32_t level, const char **out_bytes, size_t *out_size, tinypy_error_t **out_error) {
    const char *package_bytes = NULL;
    size_t package_size = 0U;
    tinypy_value_t *package;
    int32_t ascent;

    *out_bytes = NULL;
    *out_size = 0U;
    if (globals == NULL || TINYPY_VALUE_KIND(globals) != TINYPY_VALUE_DICT) {
        if (level > 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Attempted relative import in non-package", out_error);
            return TINYPY_FALSE;
        }
        return TINYPY_TRUE;
    }
    package = __tinypy_import_dict_value(vm, globals, "__package__", 11U);
    if (package != NULL && TINYPY_VALUE_KIND(package) != TINYPY_VALUE_NONE) {
        if (__tinypy_import_text_view(package, &package_bytes, &package_size) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__package__ set to non-string", out_error);
            return TINYPY_FALSE;
        }
    }
    else {
        tinypy_value_t *module_name = __tinypy_import_dict_value(vm, globals, "__name__", 8U);

        if (__tinypy_import_text_view(module_name, &package_bytes, &package_size) != 0 && __tinypy_import_dict_value(vm, globals, "__path__", 8U) == NULL) {
            while (package_size != 0U && package_bytes[package_size - 1U] != '.') {
                package_size -= 1U;
            }
            if (package_size != 0U) {
                package_size -= 1U;
            }
        }
    }
    if (package == NULL || TINYPY_VALUE_KIND(package) == TINYPY_VALUE_NONE) {
        tinypy_value_t *module_name = __tinypy_import_dict_value(vm, globals, "__name__", 8U);

        if (module_name != NULL && TINYPY_VALUE_KIND(module_name) == TINYPY_VALUE_STRING) {
            tinypy_value_t *key = tinypy_string_from_bytes(vm, "__package__", 11U);
            tinypy_value_t *inferred = package_size != 0U ? tinypy_string_from_bytes(vm, package_bytes, package_size) : tinypy_none_get(vm);
            tinypy_bool_t stored = tinypy_internal_dict_set_checked(vm, globals, key, inferred, out_error);

            TINYPY_DECREF(key);
            TINYPY_DECREF(inferred);
            if (stored == 0) {
                return TINYPY_FALSE;
            }
        }
    }
    if (package_size == 0U) {
        if (level > 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Attempted relative import in non-package", out_error);
            return TINYPY_FALSE;
        }
        return TINYPY_TRUE;
    }
    for (ascent = 1; ascent < level; ++ascent) {
        while (package_size != 0U && package_bytes[package_size - 1U] != '.') {
            package_size -= 1U;
        }
        if (package_size == 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Attempted relative import beyond toplevel package", out_error);
            return TINYPY_FALSE;
        }
        package_size -= 1U;
    }
    *out_bytes = package_bytes;
    *out_size = package_size;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_import_discard_error(tinypy_vm_t *vm, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
static void __tinypy_import_set_metadata(tinypy_vm_t *vm, tinypy_value_t *module, const char *name, size_t name_size, const tinypy_module_artifact_t *artifact) {
    tinypy_value_t *value = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_module_add_value(module, "__name__", 8U, value);
    TINYPY_DECREF(value);
    if (tinypy_module_get_value(module, "__package__", 11U) == NULL) {
        value = tinypy_none_get(vm);
        tinypy_module_add_value(module, "__package__", 11U, value);
        TINYPY_DECREF(value);
    }
    if (artifact->logical_filename != NULL || artifact->logical_filename_size != 0U) {
        value = tinypy_string_from_bytes(vm, artifact->logical_filename, artifact->logical_filename_size);
        tinypy_module_add_value(module, "__file__", 8U, value);
        TINYPY_DECREF(value);
    }
    tinypy_module_add_value(module, "__builtins__", 12U, vm->builtins);
    if ((artifact->flags & TINYPY_MODULE_ARTIFACT_PACKAGE) != 0U) {
        value = tinypy_list_from_items(vm, NULL, 0U);
        tinypy_module_add_value(module, "__path__", 8U, value);
        TINYPY_DECREF(value);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_import_artifact_valid(const tinypy_module_artifact_t *artifact, const char *name, size_t name_size) {
    static const uint32_t compile_features = (uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR | (uint32_t)TINYPY_COMPILE_FEATURE_META;

    if (artifact == NULL || artifact->abi_version != TINYPY_ABI_VERSION || artifact->struct_size < (uint32_t)offsetof(tinypy_module_artifact_t, compile_feature_flags)) {
        return TINYPY_FALSE;
    }
    if (artifact->content_kind < TINYPY_MODULE_CONTENT_SOURCE || artifact->content_kind > TINYPY_MODULE_CONTENT_NATIVE) {
        return TINYPY_FALSE;
    }
    if (artifact->canonical_name != NULL || artifact->canonical_name_size != 0U) {
        if (artifact->canonical_name == NULL || artifact->canonical_name_size != name_size || memcmp(artifact->canonical_name, name, name_size) != 0) {
            return TINYPY_FALSE;
        }
    }
    if ((artifact->content_kind == TINYPY_MODULE_CONTENT_SOURCE || artifact->content_kind == TINYPY_MODULE_CONTENT_MARSHAL_V2) && artifact->data == NULL && artifact->data_size != 0U) {
        return TINYPY_FALSE;
    }
    if (artifact->content_kind == TINYPY_MODULE_CONTENT_NATIVE && artifact->native_initialize == NULL) {
        return TINYPY_FALSE;
    }
    if ((size_t)artifact->struct_size >= offsetof(tinypy_module_artifact_t, build_profile) + sizeof(artifact->build_profile)) {
        if (artifact->compile_optimize_level < 0 || artifact->compile_optimize_level > 2 || (artifact->compile_feature_flags & ~compile_features) != 0U) {
            return TINYPY_FALSE;
        }
        if (((artifact->compile_feature_flags & (uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR) != 0U) != (artifact->build_profile != NULL)) {
            return TINYPY_FALSE;
        }
        if (artifact->build_profile != NULL && tinypy_build_profile_optimize_level(artifact->build_profile) != artifact->compile_optimize_level) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_import_has_compile_environment(const tinypy_module_artifact_t *artifact) {
    return (size_t)artifact->struct_size >= offsetof(tinypy_module_artifact_t, build_profile) + sizeof(artifact->build_profile);
}
//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_import_compile_feature_flags(const tinypy_module_artifact_t *artifact) {
    if (__tinypy_import_has_compile_environment(artifact) == 0) {
        return 0U;
    }
    return artifact->compile_feature_flags;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_build_profile_t *__tinypy_import_build_profile(const tinypy_module_artifact_t *artifact) {
    if (__tinypy_import_has_compile_environment(artifact) == 0) {
        return NULL;
    }
    return artifact->build_profile;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_import_compile_optimize_level(const tinypy_module_artifact_t *artifact, int32_t default_level) {
    const tinypy_build_profile_t *profile = __tinypy_import_build_profile(artifact);

    if (profile != NULL) {
        int32_t return_value_1 = (int32_t)tinypy_build_profile_optimize_level(profile);
        return return_value_1;
    }
    if (__tinypy_import_has_compile_environment(artifact) == 0) {
        return default_level;
    }
    return artifact->compile_optimize_level;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_import_make_not_found_error(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_error_t **out_error) {
    static const char prefix[] = "No module named ";
    size_t message_size;
    char *message;
    size_t component_start = name_size;

    while (component_start != 0U && name[component_start - 1U] != '.') {
        component_start -= 1U;
    }
    name += component_start;
    name_size -= component_start;
    message_size = (sizeof(prefix) - 1U) + name_size;
    message = (char *)tinypy_internal_vm_allocate(vm, message_size + 1U);
    (void)memcpy(message, prefix, sizeof(prefix) - 1U);
    if (name_size != 0U) {
        (void)memcpy(message + sizeof(prefix) - 1U, name, name_size);
    }
    message[message_size] = '\0';
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, message, out_error);
    tinypy_internal_vm_deallocate(vm, message, message_size + 1U);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_import_finder_path(tinypy_vm_t *vm, const char *name, size_t name_size) {
    size_t parent_size = name_size;
    tinypy_value_t *path = tinypy_none_get(vm);

    while (parent_size != 0U && name[parent_size - 1U] != '.') {
        parent_size -= 1U;
    }
    if (parent_size != 0U) {
        tinypy_value_t *parent_key;
        tinypy_value_t *parent;

        parent_size -= 1U;
        parent_key = tinypy_string_from_bytes(vm, name, parent_size);
        parent = tinypy_dict_get_optional(vm->modules, parent_key);
        if (parent != NULL && TINYPY_VALUE_KIND(parent) == TINYPY_VALUE_MODULE) {
            tinypy_value_t *parent_path = tinypy_module_get_value(parent, "__path__", 8U);

            if (parent_path != NULL) {
                TINYPY_INCREF(parent_path);
                TINYPY_DECREF(path);
                path = parent_path;
            }
        }
        TINYPY_DECREF(parent_key);
    }
    return path;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_import_load_finder(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *key, tinypy_bool_t preserve_registered, tinypy_bool_t *out_not_found, tinypy_error_t **out_error) {
    tinypy_value_t *path;
    tinypy_value_t *find_items[2];
    tinypy_value_t *find_args;
    tinypy_value_t *loader;
    tinypy_value_t *load_method;
    tinypy_value_t *load_args;
    tinypy_value_t *module;

    tinypy_value_t *find_method = tinypy_object_get_attr(vm->module_finder, "find_module", 11U, out_error);
    if (find_method == NULL) {
        return NULL;
    }
    tinypy_value_t *name_value = tinypy_string_from_bytes(vm, name, name_size);
    path = __tinypy_import_finder_path(vm, name, name_size);
    find_items[0] = name_value;
    find_items[1] = path;
    find_args = tinypy_tuple_from_items(vm, find_items, 2U);
    TINYPY_DECREF(path);
    loader = tinypy_call(find_method, find_args, NULL, out_error);
    TINYPY_DECREF(find_args);
    TINYPY_DECREF(find_method);
    if (loader == NULL) {
        TINYPY_DECREF(name_value);
        return NULL;
    }
    if (tinypy_typeof(loader) == TINYPY_VALUE_NONE) {
        *out_not_found = 1;
        TINYPY_DECREF(loader);
        TINYPY_DECREF(name_value);
        return NULL;
    }
    load_method = tinypy_object_get_attr(loader, "load_module", 11U, out_error);
    TINYPY_DECREF(loader);
    if (load_method == NULL) {
        TINYPY_DECREF(name_value);
        return NULL;
    }
    load_args = tinypy_tuple_from_items(vm, &name_value, 1U);
    TINYPY_DECREF(name_value);
    module = tinypy_call(load_method, load_args, NULL, out_error);
    TINYPY_DECREF(load_args);
    TINYPY_DECREF(load_method);
    if (module == NULL) {
        if (preserve_registered == 0 && tinypy_dict_contains(vm->modules, key) != 0) {
            tinypy_dict_delete(vm->modules, key);
        }
        return NULL;
    }
    if (tinypy_typeof(module) == TINYPY_VALUE_NONE) {
        if (preserve_registered == 0 && tinypy_dict_contains(vm->modules, key) != 0) {
            tinypy_dict_delete(vm->modules, key);
        }
        TINYPY_DECREF(module);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "module loader returned None", out_error);
        return NULL;
    }
    tinypy_value_t *registered = tinypy_dict_get_optional(vm->modules, key);

    if (registered == NULL) {
        tinypy_dict_set(vm->modules, key, module);
    }
    else {
        if (registered != module) {
            TINYPY_INCREF(registered);
            TINYPY_DECREF(module);
            module = registered;
        }
    }
    return module;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_import_load_one(tinypy_vm_t *vm, const char *name, size_t name_size, const char *importer, size_t importer_size, tinypy_value_t *reload_module, tinypy_bool_t *out_fresh, tinypy_bool_t *out_not_found, tinypy_error_t **out_error) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_module_request_t request;
    int32_t loaded = 0;

    *out_not_found = 0;
    *out_fresh = TINYPY_TRUE;

    tinypy_value_t *module = tinypy_dict_get_optional(vm->modules, key);
    /* A None entry is the Python 2.7 negative marker: the name is known to
       be missing and the resolver must not be asked again. */
    if (module != NULL && TINYPY_VALUE_KIND(module) == TINYPY_VALUE_NONE && reload_module == NULL) {
        *out_not_found = 1;
        TINYPY_DECREF(key);
        __tinypy_import_make_not_found_error(vm, name, name_size, out_error);
        return NULL;
    }
    if (module != NULL && reload_module == NULL) {
        TINYPY_INCREF(module);
        TINYPY_DECREF(key);
        *out_fresh = TINYPY_FALSE;
        return module;
    }
    if (vm->module_finder != NULL) {
        module = __tinypy_import_load_finder(vm, name, name_size, key, reload_module != NULL ? TINYPY_TRUE : TINYPY_FALSE, out_not_found, out_error);
        if (module != NULL || *out_not_found == 0) {
            TINYPY_DECREF(key);
            return module;
        }
        *out_not_found = 0;
    }
    if (vm->has_host == 0 || vm->host.resolve_module == NULL) {
        *out_not_found = 1;
        TINYPY_DECREF(key);
        __tinypy_import_make_not_found_error(vm, name, name_size, out_error);
        return NULL;
    }
    (void)memset(&request, 0, sizeof(request));
    request.abi_version = TINYPY_ABI_VERSION;
    request.struct_size = (uint32_t)sizeof(request);
    request.canonical_name = name;
    request.canonical_name_size = name_size;
    request.importer_name = importer;
    request.importer_name_size = importer_size;
    const tinypy_module_artifact_t *artifact = vm->host.resolve_module(vm->host.user_data, &request);
    if (artifact == NULL) {
        *out_not_found = 1;
        TINYPY_DECREF(key);
        __tinypy_import_make_not_found_error(vm, name, name_size, out_error);
        return NULL;
    }
    if (__tinypy_import_artifact_valid(artifact, name, name_size) == 0) {
        vm->host.release_module_artifact(vm->host.user_data, artifact);
        TINYPY_DECREF(key);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "host resolver returned an invalid module artifact", out_error);
        return NULL;
    }
    module = reload_module != NULL ? reload_module : tinypy_module_new(vm, name, name_size);
    if (reload_module != NULL) {
        TINYPY_INCREF(module);
    }
    __tinypy_import_set_metadata(vm, module, name, name_size, artifact);
    if (reload_module == NULL) {
        tinypy_dict_set(vm->modules, key, module);
    }
    if (artifact->content_kind == TINYPY_MODULE_CONTENT_MARSHAL_V2) {
        tinypy_marshal_error_t marshal_error;
        tinypy_value_t *code = NULL;
        tinypy_marshal_result_e marshal_result = tinypy_marshal_load_code_v2(vm, artifact->data, artifact->data_size, NULL, &code, &marshal_error);

        if (marshal_result == TINYPY_MARSHAL_OK) {
            uint32_t feature_flags = __tinypy_import_compile_feature_flags(artifact);
            const tinypy_build_profile_t *build_profile = __tinypy_import_build_profile(artifact);
            int32_t optimize_level = __tinypy_import_compile_optimize_level(artifact, vm->optimize_level);
            tinypy_value_t *eval_result;

            if (__tinypy_import_has_compile_environment(artifact) != 0) {
                tinypy_internal_code_attach_compile_options(code, feature_flags, optimize_level, build_profile);
            }
            tinypy_value_t *module_dict = tinypy_module_dict(module);
            eval_result = tinypy_eval_code(code, module_dict, NULL, out_error);
            TINYPY_DECREF(code);
            if (eval_result != NULL) {
                TINYPY_DECREF(eval_result);
                loaded = 1;
            }
        }
        else {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "module marshal artifact is invalid", out_error);
        }
    }
    else if (artifact->content_kind == TINYPY_MODULE_CONTENT_NATIVE) {
        loaded = artifact->native_initialize(module, artifact->native_user_data, out_error) != 0;
        if (loaded == 0 && vm->raised_value == NULL && (out_error == NULL || *out_error == NULL)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "native module initialization failed", out_error);
        }
    }
    else {
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
            tinypy_value_t *module_dict = tinypy_module_dict(module);
            tinypy_value_t *exec_result = tinypy_exec_code(code, module_dict, NULL, out_error);

            TINYPY_DECREF(code);
            if (exec_result != NULL) {
                TINYPY_DECREF(exec_result);
                loaded = 1;
            }
        }
    }
    vm->host.release_module_artifact(vm->host.user_data, artifact);
    if (loaded == 0) {
        if (reload_module == NULL) {
            tinypy_dict_delete(vm->modules, key);
            tinypy_value_t *module_dict = tinypy_module_dict(module);
            tinypy_dict_clear(module_dict);
        }
        TINYPY_DECREF(module);
        TINYPY_DECREF(key);
        return NULL;
    }
    tinypy_value_t *registered = tinypy_internal_dict_get_optional(vm, vm->modules, key);
    if (registered == NULL) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("Loaded module "),
            TINYPY_MESSAGE_PART_TEXT(key),
            TINYPY_MESSAGE_PART_LITERAL(" not found in sys.modules")
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_IMPORT, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        TINYPY_DECREF(module);
        TINYPY_DECREF(key);
        return NULL;
    }
    TINYPY_INCREF(registered);
    TINYPY_DECREF(module);
    TINYPY_DECREF(key);
    return registered;
}
//////////////////////////////////////////////////////////////////////////
/* Objects other than packages have no submodules, as import_submodule
   decides from __path__. */
static tinypy_bool_t __tinypy_import_is_package(tinypy_value_t *module) {
    tinypy_bool_t package = TINYPY_VALUE_KIND(module) == TINYPY_VALUE_MODULE && tinypy_module_get_value(module, "__path__", 8U) != NULL ? TINYPY_TRUE : TINYPY_FALSE;

    return package;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_import_bind_submodule(tinypy_vm_t *vm, tinypy_value_t *parent, const char *name, size_t name_size, tinypy_value_t *module) {
    tinypy_error_t *binding_error = NULL;

    /* Parents may be arbitrary objects placed into sys.modules. */
    (void)tinypy_object_set_attr(parent, name, name_size, module, &binding_error);
    if (binding_error != NULL) {
        tinypy_error_release(binding_error);
        tinypy_vm_clear_error(vm);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_import_load_path(tinypy_vm_t *vm, const char *name, size_t name_size, const char *importer, size_t importer_size, size_t return_name_size, tinypy_bool_t *out_not_found, tinypy_error_t **out_error) {
    tinypy_value_t *selected = NULL;
    tinypy_value_t *parent = NULL;
    size_t component_start = 0U;
    size_t offset;

    *out_not_found = 0;

    for (offset = 0U; offset <= name_size; ++offset) {
        if (offset != name_size && name[offset] != '.') {
            continue;
        }
        if (offset == component_start && offset == name_size && parent != NULL) {
            if (return_name_size == name_size) {
                selected = parent;
                TINYPY_INCREF(selected);
            }
            break;
        }
        if (offset == component_start) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Empty module name", out_error);
            goto failure;
        }
        tinypy_bool_t not_found = TINYPY_FALSE;
        tinypy_bool_t fresh = TINYPY_FALSE;
        tinypy_value_t *module;

        if (parent != NULL && __tinypy_import_is_package(parent) == 0) {
            *out_not_found = 1;
            __tinypy_import_make_not_found_error(vm, name, offset, out_error);
            goto failure;
        }
        module = __tinypy_import_load_one(vm, name, offset, importer, importer_size, NULL, &fresh, &not_found, out_error);
        if (module == NULL) {
            *out_not_found = not_found;
            goto failure;
        }
        if (offset == return_name_size) {
            selected = module;
            TINYPY_INCREF(selected);
        }
        if (parent != NULL) {
            if (fresh != 0) {
                __tinypy_import_bind_submodule(vm, parent, name + component_start, offset - component_start, module);
            }
            TINYPY_DECREF(parent);
        }
        parent = module;
        component_start = offset + 1U;
    }
    TINYPY_DECREF(parent);
    return selected;
failure:
    if (parent != NULL) {
        TINYPY_DECREF(parent);
    }
    if (selected != NULL) {
        TINYPY_DECREF(selected);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* Follows ensure_fromlist: names listed in a from-import that are not yet
   attributes of a package are imported as its submodules. */
static tinypy_bool_t __tinypy_import_ensure_fromlist(tinypy_vm_t *vm, tinypy_value_t *module, const char *module_name, size_t module_name_size, tinypy_value_t *fromlist, tinypy_bool_t recursive, tinypy_error_t **out_error);
static tinypy_bool_t __tinypy_import_ensure_fromlist_impl(tinypy_vm_t *vm, tinypy_value_t *module, const char *module_name, size_t module_name_size, tinypy_value_t *fromlist, tinypy_bool_t recursive, tinypy_error_t **out_error) {
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *iterator;
    tinypy_bool_t success = TINYPY_TRUE;

    if (__tinypy_import_is_package(module) == 0) {
        return TINYPY_TRUE;
    }
    iterator = tinypy_iter(fromlist, out_error);
    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        const char *item_bytes;
        size_t item_size;
        tinypy_value_t *existing = NULL;

        if (item == NULL) {
            break;
        }
        if (__tinypy_import_text_view(item, &item_bytes, &item_size) == 0) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("Item in ``from list'' must be str, not "),
                TINYPY_MESSAGE_PART_TYPE_NAME(item),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
            TINYPY_DECREF(item);
            success = TINYPY_FALSE;
            break;
        }
        if (item_size == 1U && item_bytes[0] == '*') {
            tinypy_value_t *all = recursive == 0 ? tinypy_module_get_value(module, "__all__", 7U) : NULL;

            if (all != NULL && __tinypy_import_ensure_fromlist(vm, module, module_name, module_name_size, all, TINYPY_TRUE, out_error) == 0) {
                TINYPY_DECREF(item);
                success = TINYPY_FALSE;
                break;
            }
            TINYPY_DECREF(item);
            continue;
        }
        if (tinypy_internal_object_get_optional_attr_key(module, item, &existing, out_error) < 0) {
            TINYPY_DECREF(item);
            success = TINYPY_FALSE;
            break;
        }
        if (existing != NULL) {
            TINYPY_DECREF(existing);
            TINYPY_DECREF(item);
            continue;
        }
        size_t full_size = module_name_size + 1U + item_size;
        char *full_name = (char *)tinypy_internal_vm_allocate(vm, full_size);
        tinypy_bool_t fresh = TINYPY_FALSE;
        tinypy_bool_t not_found = TINYPY_FALSE;
        tinypy_value_t *submodule;

        (void)memcpy(full_name, module_name, module_name_size);
        full_name[module_name_size] = '.';
        (void)memcpy(full_name + module_name_size + 1U, item_bytes, item_size);
        submodule = __tinypy_import_load_one(vm, full_name, full_size, module_name, module_name_size, NULL, &fresh, &not_found, out_error);
        tinypy_internal_vm_deallocate(vm, full_name, full_size);
        if (submodule != NULL) {
            if (fresh != 0) {
                __tinypy_import_bind_submodule(vm, module, item_bytes, item_size, submodule);
            }
            TINYPY_DECREF(submodule);
        }
        else if (not_found != 0) {
            __tinypy_import_discard_error(vm, out_error);
        }
        else {
            TINYPY_DECREF(item);
            success = TINYPY_FALSE;
            break;
        }
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL && *out_error == NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return TINYPY_FALSE;
    }
    return success;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_import_ensure_fromlist(tinypy_vm_t *vm, tinypy_value_t *module, const char *module_name, size_t module_name_size, tinypy_value_t *fromlist, tinypy_bool_t recursive, tinypy_error_t **out_error) {
    TINYPY_INCREF(fromlist);
    tinypy_bool_t result = __tinypy_import_ensure_fromlist_impl(vm, module, module_name, module_name_size, fromlist, recursive, out_error);
    TINYPY_DECREF(fromlist);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_import_first_component_size(const char *name, size_t name_size) {
    size_t size = 0U;

    while (size != name_size && name[size] != '.') {
        size += 1U;
    }
    return size;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_import_discard_error(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
    }
    tinypy_internal_exception_clear_raised(vm);
}
//////////////////////////////////////////////////////////////////////////
/* Marks a name that an implicit relative import found missing, so later
   imports skip straight to the absolute name, as mark_miss does. */
static void __tinypy_import_mark_missing(tinypy_vm_t *vm, const char *name, size_t name_size) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *none = tinypy_none_get(vm);

    if (tinypy_dict_get_optional(vm->modules, key) == NULL) {
        tinypy_dict_set(vm->modules, key, none);
    }
    TINYPY_DECREF(none);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_import_finish(tinypy_vm_t *vm, tinypy_value_t *result, const char *name, size_t name_size, tinypy_value_t *fromlist, tinypy_bool_t has_fromlist, tinypy_error_t **out_error) {
    if (result != NULL && has_fromlist != 0 && __tinypy_import_ensure_fromlist(vm, result, name, name_size, fromlist, TINYPY_FALSE, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Follows import_module_level in Python 2.7: an implicit relative import
   tries only its first component under the package before falling back to
   the absolute name, and the from-list is resolved against the tail. */
static tinypy_value_t *__tinypy_import_module(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *globals, tinypy_value_t *fromlist, int32_t level, tinypy_error_t **out_error) {
    const char *importer = NULL;
    size_t importer_size = 0U;
    const char *package_bytes = NULL;
    size_t package_size = 0U;
    size_t head_size = __tinypy_import_first_component_size(name, name_size);
    tinypy_bool_t has_fromlist = TINYPY_FALSE;
    tinypy_bool_t not_found = TINYPY_FALSE;
    tinypy_value_t *result;

    TINYPY_CLEAR_ERROR(out_error);
    if (globals != NULL && TINYPY_VALUE_KIND(globals) == TINYPY_VALUE_DICT) {
        tinypy_value_t *import_dict_value = __tinypy_import_dict_value(vm, globals, "__name__", 8U);
        (void)__tinypy_import_text_view(import_dict_value, &importer, &importer_size);
    }
    if (fromlist != NULL && TINYPY_VALUE_KIND(fromlist) != TINYPY_VALUE_NONE) {
        int32_t truth = tinypy_truth(fromlist, out_error);

        if (truth < 0) {
            return NULL;
        }
        has_fromlist = truth != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    }
    if (level != 0 && __tinypy_import_parent_package(vm, globals, level, &package_bytes, &package_size, out_error) == 0) {
        return NULL;
    }
    if (level != 0 && package_size != 0U) {
        tinypy_value_t *parent_key = tinypy_string_from_bytes(vm, package_bytes, package_size);
        tinypy_value_t *parent = tinypy_dict_get_optional(vm->modules, parent_key);

        TINYPY_DECREF(parent_key);
        if (parent == NULL || TINYPY_VALUE_KIND(parent) == TINYPY_VALUE_NONE) {
            if (level > 0) {
                tinypy_value_t *text = tinypy_string_from_bytes(vm, "Parent module not loaded", 24U);
                tinypy_value_t *args = tinypy_tuple_from_items(vm, &text, 1U);
                tinypy_value_t *exception = tinypy_exception_new(vm->exception_types[TINYPY_EXCEPTION_SYSTEM_ERROR], args, out_error);

                TINYPY_DECREF(args);
                TINYPY_DECREF(text);
                if (exception != NULL) {
                    (void)tinypy_exception_raise(exception, NULL, out_error);
                    TINYPY_DECREF(exception);
                }
                return NULL;
            }
            package_size = 0U;
        }
    }
    if (level > 0 || (level < 0 && package_size != 0U)) {
        size_t canonical_size = package_size + (name_size != 0U ? 1U + name_size : 0U);
        char *canonical = (char *)tinypy_internal_vm_allocate(vm, canonical_size);
        size_t return_name_size = has_fromlist != 0 ? canonical_size : package_size + (name_size != 0U ? 1U + head_size : 0U);

        (void)memcpy(canonical, package_bytes, package_size);
        if (name_size != 0U) {
            canonical[package_size] = '.';
            (void)memcpy(canonical + package_size + 1U, name, name_size);
        }
        if (level < 0 && name_size != 0U) {
            size_t canonical_head_size = package_size + 1U + head_size;
            tinypy_value_t *relative_key = tinypy_string_from_bytes(vm, canonical, canonical_head_size);
            tinypy_value_t *marker = tinypy_internal_dict_get_optional(vm, vm->modules, relative_key);
            TINYPY_DECREF(relative_key);
            tinypy_value_t *head;
            if (marker == &vm->none_object.base) {
                not_found = TINYPY_TRUE;
                head = NULL;
            }
            else {
                head = __tinypy_import_load_path(vm, canonical, canonical_head_size, importer, importer_size, canonical_head_size, &not_found, out_error);
            }

            if (head == NULL) {
                if (not_found == 0) {
                    tinypy_internal_vm_deallocate(vm, canonical, canonical_size);
                    return NULL;
                }
                __tinypy_import_discard_error(vm, out_error);
                __tinypy_import_mark_missing(vm, canonical, package_size + 1U + head_size);
                tinypy_internal_vm_deallocate(vm, canonical, canonical_size);
                level = 0;
            }
            else {
                TINYPY_DECREF(head);
            }
        }
        if (level != 0) {
            result = __tinypy_import_load_path(vm, canonical, canonical_size, importer, importer_size, return_name_size, &not_found, out_error);
            tinypy_value_t *finished = __tinypy_import_finish(vm, result, canonical, canonical_size, fromlist, has_fromlist, out_error);
            tinypy_internal_vm_deallocate(vm, canonical, canonical_size);
            return finished;
        }
    }
    if (name_size == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Empty module name", out_error);
        return NULL;
    }
    result = __tinypy_import_load_path(vm, name, name_size, importer, importer_size, has_fromlist != 0 ? name_size : head_size, &not_found, out_error);
    tinypy_value_t *finished = __tinypy_import_finish(vm, result, name, name_size, fromlist, has_fromlist, out_error);
    return finished;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_import_module(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *globals, tinypy_value_t *fromlist, int32_t level, tinypy_error_t **out_error) {
    tinypy_value_t *importer_name = globals != NULL && TINYPY_VALUE_KIND(globals) == TINYPY_VALUE_DICT
        ? __tinypy_import_dict_value(vm, globals, "__name__", 8U) : NULL;
    tinypy_value_t *result;

    if (importer_name != NULL) {
        TINYPY_INCREF(importer_name);
    }
    result = __tinypy_import_module(vm, name, name_size, globals, fromlist, level, out_error);
    if (importer_name != NULL) {
        TINYPY_DECREF(importer_name);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_reload_module(tinypy_value_t *module, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(module);
    tinypy_value_t *name_value;
    const char *name;
    size_t name_size;
    tinypy_value_t *key;
    tinypy_value_t *registered;
    tinypy_value_t *loaded;
    tinypy_bool_t not_found = TINYPY_FALSE;
    tinypy_bool_t fresh = TINYPY_FALSE;

    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(module) != TINYPY_VALUE_MODULE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reload() argument must be a module", out_error);
        return NULL;
    }
    name_value = tinypy_module_get_value(module, "__name__", 8U);
    if (__tinypy_import_text_view(name_value, &name, &name_size) == 0 || name_size == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reload() module has no valid __name__", out_error);
        return NULL;
    }
    TINYPY_INCREF(name_value);
    key = tinypy_string_from_bytes(vm, name, name_size);
    registered = tinypy_dict_get_optional(vm->modules, key);
    if (registered != module) {
        TINYPY_DECREF(key);
        TINYPY_DECREF(name_value);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "reload() module is not registered in sys.modules", out_error);
        return NULL;
    }
    size_t parent_size = name_size;

    while (parent_size != 0U && name[parent_size - 1U] != '.') {
        parent_size -= 1U;
    }
    if (parent_size != 0U) {
        tinypy_value_t *parent_key = tinypy_string_from_bytes(vm, name, parent_size - 1U);
        tinypy_value_t *parent = tinypy_dict_get_optional(vm->modules, parent_key);

        TINYPY_DECREF(parent_key);
        if (parent == NULL) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("reload(): parent "),
                {name, parent_size - 1U},
                TINYPY_MESSAGE_PART_LITERAL(" not in sys.modules"),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_IMPORT, parts, 3U, out_error);
            TINYPY_DECREF(key);
            TINYPY_DECREF(name_value);
            return NULL;
        }
    }
    loaded = __tinypy_import_load_one(vm, name, name_size, name, name_size, module, &fresh, &not_found, out_error);
    if (loaded == NULL && not_found != 0 && tinypy_module_get_value(module, "__file__", 8U) == NULL) {
        /* Modules without a source artifact are the VM's built-in modules,
           which reload() leaves as they are. */
        __tinypy_import_discard_error(vm, out_error);
        TINYPY_INCREF(module);
        loaded = module;
    }
    if (loaded == NULL) {
        tinypy_dict_set(vm->modules, key, module);
        TINYPY_DECREF(key);
        TINYPY_DECREF(name_value);
        return NULL;
    }
    tinypy_dict_set(vm->modules, key, loaded);
    TINYPY_DECREF(key);
    TINYPY_DECREF(name_value);
    return loaded;
}
//////////////////////////////////////////////////////////////////////////
/* IMPORT_FROM reads an attribute; submodules were imported by the from-list
   of IMPORT_NAME, so a miss is "cannot import name". */
tinypy_value_t *tinypy_internal_import_from(tinypy_value_t *module, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(module);
    tinypy_value_t *value = tinypy_object_get_attr(module, name, name_size, out_error);

    if (value == NULL && tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_ATTRIBUTE_ERROR, out_error) != 0) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("cannot import name "),
            {name, name_size},
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_IMPORT, parts, 2U, out_error);
    }
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_import_store_name(tinypy_vm_t *vm, tinypy_value_t *locals, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (locals->type == &vm->types[TINYPY_VALUE_DICT]) {
        tinypy_bool_t stored = tinypy_internal_dict_set_checked(vm, locals, key, value, out_error);
        return stored;
    }
    tinypy_bool_t stored = tinypy_set_item(locals, key, value, out_error);

    return stored;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_import_star(tinypy_value_t *module, tinypy_value_t *locals, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(module);
    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(module) != TINYPY_VALUE_MODULE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "from-import-* object has no __dict__ and no __all__", out_error);
        return TINYPY_FALSE;
    }
    tinypy_value_t *all = tinypy_module_get_value(module, "__all__", 7U);
    tinypy_bool_t skip_private = all == NULL;
    if (all != NULL) {
        TINYPY_INCREF(all);
    }
    else {
        tinypy_value_t *dict = tinypy_module_dict(module);
        if (dict == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "'NoneType' object has no attribute 'keys'", out_error);
            return TINYPY_FALSE;
        }
        all = tinypy_list_from_items(vm, NULL, 0U);
        size_t position = 0U;
        tinypy_value_t *key;
        tinypy_value_t *value;
        while (tinypy_dict_next(dict, &position, &key, &value) != 0) {
            if (tinypy_internal_list_append_checked(all, key, out_error) == 0) {
                TINYPY_DECREF(all);
                return TINYPY_FALSE;
            }
        }
    }
    tinypy_value_t *iterator = tinypy_iter(all, out_error);
    TINYPY_DECREF(all);
    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t success = TINYPY_TRUE;
    tinypy_error_t *iteration_error = NULL;
    for (;;) {
        tinypy_value_t *key = tinypy_next(iterator, &iteration_error);
        if (key == NULL) {
            break;
        }
        if (TINYPY_VALUE_KIND(key) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(key) != TINYPY_VALUE_UNICODE) {
            TINYPY_DECREF(key);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "attribute name must be string", out_error);
            success = TINYPY_FALSE;
            break;
        }
        if (skip_private != 0 && TINYPY_TEXT_BYTE_SIZE(key) != 0U && TINYPY_TEXT_BYTES(key)[0] == '_') {
            TINYPY_DECREF(key);
            continue;
        }
        tinypy_value_t *value = tinypy_internal_object_get_attr_key(module, key, out_error);
        tinypy_bool_t stored = value != NULL ? __tinypy_import_store_name(vm, locals, key, value, out_error) : TINYPY_FALSE;
        if (value != NULL) {
            TINYPY_DECREF(value);
        }
        TINYPY_DECREF(key);
        if (stored == 0) {
            success = TINYPY_FALSE;
            break;
        }
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL && *out_error == NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        success = TINYPY_FALSE;
    }
    return success;
}
