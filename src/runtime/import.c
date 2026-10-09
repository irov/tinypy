#include "tinypy/module.h"

#include "tinypy/eval.h"
#include "tinypy/compiler.h"
#include "tinypy/marshal.h"
#include "internal.h"

#include <string.h>
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_import_dict_value(tinypy_value_t *dict, tinypy_value_t *name) {
    if (dict == NULL) {
        return NULL;
    }
    tinypy_value_t *value = tinypy_internal_dict_get_optional_suppressed(TINYPY_VALUE_VM(dict), dict, name);
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
/* PyObject_HasAttr and ensure_fromlist suppress every lookup exception. */
static tinypy_value_t *__tinypy_import_optional_attribute(tinypy_value_t *value, tinypy_value_t *name) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_exception_state_t state;
    tinypy_error_t *error = NULL;
    tinypy_value_t *result;

    tinypy_internal_exception_preserve_begin(vm, &state);
    result = tinypy_object_get_attr_value(value, name, &error);
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_internal_exception_preserve_end(vm, &state);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_import_sequence_item(tinypy_value_t *sequence, int64_t index, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(sequence);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(sequence);
    tinypy_bool_t indexed = kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_STRING
        || kind == TINYPY_VALUE_UNICODE || kind == TINYPY_VALUE_XRANGE || kind == TINYPY_VALUE_BYTEARRAY
        || kind == TINYPY_VALUE_BUFFER;

    if (indexed == TINYPY_FALSE) {
        indexed = (sequence->type->sequence_slots != NULL && sequence->type->sequence_slots->get_item != NULL)
            || tinypy_internal_object_has_special_override_key(sequence, vm->internal_special_getitem_key) != TINYPY_FALSE;
    }
    if (indexed == TINYPY_FALSE) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("'"),
            TINYPY_MESSAGE_PART_TYPE_NAME(sequence),
            TINYPY_MESSAGE_PART_LITERAL("' object does not support indexing"),
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
        return NULL;
    }
    tinypy_value_t *key = tinypy_integer_from_i64(vm, index);
    tinypy_value_t *item = tinypy_get_item(sequence, key, out_error);

    TINYPY_DECREF(key);
    return item;
}
//////////////////////////////////////////////////////////////////////////
/* Follows get_parent in Python 2.7: an empty result means absolute imports
   only. Relative-import failures raise ValueError as CPython does. */
static tinypy_bool_t __tinypy_import_parent_package(tinypy_vm_t *vm, tinypy_value_t *globals, int32_t level, tinypy_value_t **out_package, tinypy_error_t **out_error) {
    const char *package_bytes = NULL;
    size_t package_size = 0U;
    tinypy_value_t *package;
    int32_t ascent;

    *out_package = NULL;
    if (globals == NULL || TINYPY_VALUE_KIND(globals) != TINYPY_VALUE_DICT) {
        return TINYPY_TRUE;
    }
    package = __tinypy_import_dict_value(globals, vm->internal_special_package_key);
    if (package != NULL && TINYPY_VALUE_KIND(package) != TINYPY_VALUE_NONE) {
        if (TINYPY_VALUE_KIND(package) != TINYPY_VALUE_STRING) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__package__ set to non-string", out_error);
            return TINYPY_FALSE;
        }
        package_bytes = (const char *)TINYPY_TEXT_BYTES(package);
        package_size = TINYPY_TEXT_BYTE_SIZE(package);
        const char *nul = (const char *)memchr(package_bytes, 0, package_size);
        if (nul != NULL) {
            package_size = (size_t)(nul - package_bytes);
        }
        *out_package = tinypy_string_from_bytes(vm, package_bytes, package_size);
    }
    else {
        tinypy_value_t *module_name = __tinypy_import_dict_value(globals, vm->internal_special_name_key);

        if (module_name == NULL || TINYPY_VALUE_KIND(module_name) != TINYPY_VALUE_STRING) {
            return TINYPY_TRUE;
        }
        TINYPY_INCREF(module_name);
        package_bytes = (const char *)TINYPY_TEXT_BYTES(module_name);
        package_size = TINYPY_TEXT_BYTE_SIZE(module_name);
        const char *nul = (const char *)memchr(package_bytes, 0, package_size);
        if (nul != NULL) {
            package_size = (size_t)(nul - package_bytes);
        }
        tinypy_bool_t is_package = __tinypy_import_dict_value(globals, vm->internal_special_path_key) != NULL;
        if (is_package == TINYPY_FALSE) {
            while (package_size != 0U && package_bytes[package_size - 1U] != '.') {
                package_size -= 1U;
            }
            if (package_size != 0U) {
                package_size -= 1U;
            }
        }
        if (package_size == 0U && level > 0 && is_package == TINYPY_FALSE) {
            TINYPY_DECREF(module_name);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Attempted relative import in non-package", out_error);
            return TINYPY_FALSE;
        }
        *out_package = tinypy_string_from_bytes(vm, package_bytes, package_size);
        {
            tinypy_value_t *key = vm->internal_special_package_key;
            tinypy_value_t *inferred = is_package != TINYPY_FALSE ? TINYPY_RET(module_name)
                : (package_size != 0U ? TINYPY_RET(*out_package) : TINYPY_RET_NONE(vm));
            tinypy_bool_t stored = tinypy_internal_dict_set_checked(vm, globals, key, inferred, out_error);

            TINYPY_DECREF(inferred);
            TINYPY_DECREF(module_name);
            if (stored == 0) {
                TINYPY_DECREF(*out_package);
                *out_package = NULL;
                return TINYPY_FALSE;
            }
        }
    }
    package_bytes = (const char *)TINYPY_TEXT_BYTES(*out_package);
    if (package_size == 0U) {
        if (level > 0) {
            TINYPY_DECREF(*out_package);
            *out_package = NULL;
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
            TINYPY_DECREF(*out_package);
            *out_package = NULL;
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Attempted relative import beyond toplevel package", out_error);
            return TINYPY_FALSE;
        }
        package_size -= 1U;
    }
    if (package_size != TINYPY_TEXT_BYTE_SIZE(*out_package)) {
        tinypy_value_t *ascended = tinypy_string_from_bytes(vm, package_bytes, package_size);
        TINYPY_DECREF(*out_package);
        *out_package = ascended;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_import_discard_error(tinypy_vm_t *vm, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
static void __tinypy_import_set_metadata(tinypy_vm_t *vm, tinypy_value_t *module, const char *name, size_t name_size, const tinypy_module_artifact_t *artifact) {
    tinypy_value_t *value = tinypy_internal_name_from_bytes(vm, name, name_size);
    tinypy_module_add_value_key(module, vm->internal_special_name_key, value);
    TINYPY_DECREF(value);
    if (tinypy_module_get_value_key(module, vm->internal_special_package_key) == NULL) {
        value = TINYPY_RET_NONE(vm);
        tinypy_module_add_value_key(module, vm->internal_special_package_key, value);
        TINYPY_DECREF(value);
    }
    if (artifact->logical_filename != NULL || artifact->logical_filename_size != 0U) {
        value = tinypy_string_from_bytes(vm, artifact->logical_filename, artifact->logical_filename_size);
        tinypy_module_add_value_key(module, vm->internal_special_file_key, value);
        TINYPY_DECREF(value);
    }
    tinypy_module_add_value_key(module, vm->internal_builtins_key, vm->builtins);
    if ((artifact->flags & TINYPY_MODULE_ARTIFACT_PACKAGE) != 0U) {
        tinypy_value_t *directory = artifact->package_token != NULL ? tinypy_string_from_bytes(vm, artifact->package_token, artifact->package_token_size) : NULL;

        value = tinypy_list_from_items(vm, &directory, directory != NULL ? 1U : 0U);
        tinypy_module_add_value_key(module, vm->internal_special_path_key, value);
        TINYPY_DECREF(value);
        if (directory != NULL) {
            TINYPY_DECREF(directory);
        }
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
    tinypy_value_t *path = TINYPY_RET_NONE(vm);

    while (parent_size != 0U && name[parent_size - 1U] != '.') {
        parent_size -= 1U;
    }
    if (parent_size != 0U) {
        tinypy_value_t *parent_key;
        tinypy_value_t *parent;

        parent_size -= 1U;
        parent_key = tinypy_internal_name_from_bytes(vm, name, parent_size);
        parent = tinypy_dict_get_optional(vm->modules, parent_key);
        if (parent != NULL) {
            TINYPY_INCREF(parent);
            tinypy_value_t *parent_path = __tinypy_import_optional_attribute(parent, vm->internal_special_path_key);
            TINYPY_DECREF(parent);

            if (parent_path != NULL) {
                TINYPY_DECREF(path);
                path = parent_path;
            }
        }
        TINYPY_DECREF(parent_key);
    }
    return path;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_import_load_finder(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *key, tinypy_value_t *path, tinypy_bool_t preserve_registered, tinypy_bool_t *out_not_found, tinypy_error_t **out_error) {
    tinypy_value_t *find_items[2];
    tinypy_value_t *find_args;
    tinypy_value_t *loader;
    tinypy_value_t *load_method;
    tinypy_value_t *load_args;
    tinypy_value_t *module;

    tinypy_value_t *find_method = tinypy_object_get_attr_value(vm->module_finder, vm->internal_find_module_key, out_error);
    if (find_method == NULL) {
        return NULL;
    }
    tinypy_value_t *name_value = tinypy_internal_name_from_bytes(vm, name, name_size);
    find_items[0] = name_value;
    find_items[1] = path;
    find_args = tinypy_tuple_from_items(vm, find_items, 2U);
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
    load_method = tinypy_object_get_attr_value(loader, vm->internal_load_module_key, out_error);
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
static tinypy_value_t *__tinypy_import_load_one(tinypy_vm_t *vm, const char *name, size_t name_size, const char *importer, size_t importer_size, tinypy_value_t *globals, tinypy_value_t *parent, tinypy_value_t *reload_module, tinypy_bool_t *out_fresh, tinypy_bool_t *out_not_found, tinypy_error_t **out_error) {
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
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
    tinypy_value_t *path = parent != NULL ? __tinypy_import_optional_attribute(parent, vm->internal_special_path_key)
        : __tinypy_import_finder_path(vm, name, name_size);
    if (path == NULL) {
        *out_not_found = TINYPY_TRUE;
        TINYPY_DECREF(key);
        __tinypy_import_make_not_found_error(vm, name, name_size, out_error);
        return NULL;
    }
    if (vm->module_finder != NULL) {
        module = __tinypy_import_load_finder(vm, name, name_size, key, path, reload_module != NULL ? TINYPY_TRUE : TINYPY_FALSE, out_not_found, out_error);
        if (module != NULL || *out_not_found == 0) {
            TINYPY_DECREF(path);
            TINYPY_DECREF(key);
            return module;
        }
        *out_not_found = 0;
    }
    TINYPY_DECREF(path);
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
    tinypy_value_t *importer_name = globals != NULL && TINYPY_VALUE_KIND(globals) == TINYPY_VALUE_DICT
        ? __tinypy_import_dict_value(globals, vm->internal_special_name_key) : NULL;
    if (importer_name != NULL) {
        TINYPY_INCREF(importer_name);
        (void)__tinypy_import_text_view(importer_name, &importer, &importer_size);
    }
    request.importer_name = importer;
    request.importer_name_size = importer_size;
    const tinypy_module_artifact_t *artifact = vm->host.resolve_module(vm->host.user_data, &request);
    if (importer_name != NULL) {
        TINYPY_DECREF(importer_name);
    }
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
    module = reload_module != NULL ? reload_module : tinypy_module_new_key(key);
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
    /* A failed module leaves sys.modules; releasing it sets its globals to
       None the way _PyModule_Clear does, for the functions that outlive it. */
    if (loaded == 0) {
        if (reload_module == NULL) {
            tinypy_dict_delete(vm->modules, key);
        }
        TINYPY_DECREF(module);
        TINYPY_DECREF(key);
        return NULL;
    }
    /* A module that leaves None for itself in sys.modules was not found. */
    tinypy_value_t *registered = tinypy_internal_dict_get_optional(vm, vm->modules, key);
    if (registered != NULL && TINYPY_VALUE_KIND(registered) == TINYPY_VALUE_NONE && reload_module == NULL) {
        *out_not_found = 1;
        __tinypy_import_make_not_found_error(vm, name, name_size, out_error);
        TINYPY_DECREF(module);
        TINYPY_DECREF(key);
        return NULL;
    }
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
    tinypy_vm_t *vm = TINYPY_VALUE_VM(module);
    tinypy_value_t *path = __tinypy_import_optional_attribute(module, vm->internal_special_path_key);
    tinypy_bool_t package = path != NULL;

    if (path != NULL) {
        TINYPY_DECREF(path);
    }
    return package;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_import_bind_submodule(tinypy_vm_t *vm, tinypy_value_t *parent, const char *name, size_t name_size, tinypy_value_t *module) {
    tinypy_error_t *binding_error = NULL;

    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);

    /* Parents may be arbitrary objects placed into sys.modules. */
    (void)tinypy_object_set_attr_value(parent, key, module, &binding_error);
    TINYPY_DECREF(key);
    if (binding_error != NULL) {
        tinypy_error_release(binding_error);
        tinypy_vm_clear_error(vm);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_import_load_path(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *globals, size_t return_name_size, tinypy_value_t **out_tail, tinypy_bool_t *out_not_found, tinypy_error_t **out_error) {
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
                selected = TINYPY_RET(parent);
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

        module = __tinypy_import_load_one(vm, name, offset, NULL, 0U, globals, parent, NULL, &fresh, &not_found, out_error);
        if (module == NULL) {
            *out_not_found = not_found;
            goto failure;
        }
        if (offset == return_name_size) {
            selected = TINYPY_RET(module);
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
    if (out_tail != NULL) {
        *out_tail = parent;
    }
    else {
        TINYPY_DECREF(parent);
    }
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
    tinypy_bool_t success = TINYPY_TRUE;
    int64_t position;

    if (__tinypy_import_is_package(module) == 0) {
        return TINYPY_TRUE;
    }
    for (position = 0; ; position += 1) {
        tinypy_value_t *item = __tinypy_import_sequence_item(fromlist, position, out_error);
        const char *item_bytes;
        size_t item_size;

        if (item == NULL) {
            if (vm->raised_value != NULL && tinypy_type_is_subtype(vm->raised_value->type, vm->exception_types[TINYPY_EXCEPTION_INDEX_ERROR]) != TINYPY_FALSE) {
                __tinypy_import_discard_error(vm, out_error);
            }
            else {
                success = TINYPY_FALSE;
            }
            break;
        }
        if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_STRING) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("Item in ``from list'' must be str, not "),
                TINYPY_MESSAGE_PART_TYPE_NAME(item),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
            TINYPY_DECREF(item);
            success = TINYPY_FALSE;
            break;
        }
        item_bytes = (const char *)TINYPY_TEXT_BYTES(item);
        item_size = TINYPY_TEXT_BYTE_SIZE(item);
        if (item_size != 0U && item_bytes[0] == '*') {
            tinypy_value_t *all = recursive == 0 ? __tinypy_import_optional_attribute(module, vm->internal_special_all_key) : NULL;

            if (all != NULL && __tinypy_import_ensure_fromlist(vm, module, module_name, module_name_size, all, TINYPY_TRUE, out_error) == 0) {
                TINYPY_DECREF(all);
                TINYPY_DECREF(item);
                success = TINYPY_FALSE;
                break;
            }
            if (all != NULL) {
                TINYPY_DECREF(all);
            }
            TINYPY_DECREF(item);
            continue;
        }
        tinypy_value_t *existing = __tinypy_import_optional_attribute(module, item);
        if (existing != NULL) {
            TINYPY_DECREF(existing);
            TINYPY_DECREF(item);
            continue;
        }
        const char *nul = (const char *)memchr(item_bytes, 0, item_size);
        if (nul != NULL) {
            item_size = (size_t)(nul - item_bytes);
        }
        size_t full_size = module_name_size + 1U + item_size;
        char *full_name = (char *)tinypy_internal_vm_allocate(vm, full_size);
        tinypy_bool_t fresh = TINYPY_FALSE;
        tinypy_bool_t not_found = TINYPY_FALSE;
        tinypy_value_t *submodule;

        (void)memcpy(full_name, module_name, module_name_size);
        full_name[module_name_size] = '.';
        (void)memcpy(full_name + module_name_size + 1U, item_bytes, item_size);
        submodule = __tinypy_import_load_one(vm, full_name, full_size, module_name, module_name_size, NULL, module, NULL, &fresh, &not_found, out_error);
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
static void __tinypy_import_mark_missing(tinypy_vm_t *vm, tinypy_value_t *key) {
    tinypy_value_t *none = TINYPY_RET_NONE(vm);

    if (tinypy_dict_get_optional(vm->modules, key) == NULL) {
        tinypy_dict_set(vm->modules, key, none);
    }
    TINYPY_DECREF(none);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_import_finish(tinypy_vm_t *vm, tinypy_value_t *result, tinypy_value_t *tail, const char *name, size_t name_size, tinypy_value_t *fromlist, tinypy_error_t **out_error) {
    if (result == NULL) {
        return NULL;
    }
    int32_t truth = fromlist != NULL ? tinypy_truth(fromlist, out_error) : 0;
    if (truth < 0 || (truth != 0 && __tinypy_import_ensure_fromlist(vm, tail, name, name_size, fromlist, TINYPY_FALSE, out_error) == 0)) {
        TINYPY_DECREF(result);
        TINYPY_DECREF(tail);
        return NULL;
    }
    if (truth != 0) {
        TINYPY_DECREF(result);
        return tail;
    }
    TINYPY_DECREF(tail);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Follows import_module_level in Python 2.7: an implicit relative import
   tries only its first component under the package before falling back to
   the absolute name, and the from-list is resolved against the tail. */
static tinypy_value_t *__tinypy_import_module(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *globals, tinypy_value_t *fromlist, int32_t level, tinypy_value_t *package, tinypy_error_t **out_error) {
    const char *package_bytes = package != NULL ? (const char *)TINYPY_TEXT_BYTES(package) : NULL;
    size_t package_size = package != NULL ? TINYPY_TEXT_BYTE_SIZE(package) : 0U;
    size_t head_size = __tinypy_import_first_component_size(name, name_size);
    tinypy_bool_t not_found = TINYPY_FALSE;
    tinypy_value_t *result;
    tinypy_value_t *tail = NULL;
    tinypy_value_t *relative_miss = NULL;

    TINYPY_CLEAR_ERROR(out_error);
    if (level != 0 && package_size != 0U) {
        tinypy_value_t *parent_key = tinypy_string_from_bytes(vm, package_bytes, package_size);
        tinypy_value_t *parent = tinypy_dict_get_optional(vm->modules, parent_key);

        TINYPY_DECREF(parent_key);
        /* A None parent makes the import absolute, as get_parent's Py_None. */
        if (parent == NULL || TINYPY_VALUE_KIND(parent) == TINYPY_VALUE_NONE) {
            if (level > 0 && parent == NULL) {
                static const char prefix[] = "Parent module '";
                static const char suffix[] = "' not loaded, cannot perform relative import";
                char message[sizeof(prefix) + 200U + sizeof(suffix) - 1U];
                size_t displayed_size = package_size < 200U ? package_size : 200U;

                (void)memcpy(message, prefix, sizeof(prefix) - 1U);
                (void)memcpy(message + sizeof(prefix) - 1U, package_bytes, displayed_size);
                (void)memcpy(message + sizeof(prefix) - 1U + displayed_size, suffix, sizeof(suffix));
                tinypy_internal_exception_raise_system_error(vm, message, out_error);
                return NULL;
            }
            package_size = 0U;
        }
    }
    if (level != 0 && package_size != 0U) {
        size_t canonical_size = package_size + (name_size != 0U ? 1U + name_size : 0U);
        char *canonical = (char *)tinypy_internal_vm_allocate(vm, canonical_size);
        size_t return_name_size = package_size + (name_size != 0U ? 1U + head_size : 0U);

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
                head = __tinypy_import_load_path(vm, canonical, canonical_head_size, globals, canonical_head_size, NULL, &not_found, out_error);
            }

            if (head == NULL) {
                if (not_found == 0) {
                    tinypy_internal_vm_deallocate(vm, canonical, canonical_size);
                    return NULL;
                }
                __tinypy_import_discard_error(vm, out_error);
                relative_miss = tinypy_internal_name_from_bytes(vm, canonical, package_size + 1U + head_size);
                tinypy_internal_vm_deallocate(vm, canonical, canonical_size);
                level = 0;
            }
            else {
                TINYPY_DECREF(head);
            }
        }
        if (level != 0) {
            result = __tinypy_import_load_path(vm, canonical, canonical_size, globals, return_name_size, &tail, &not_found, out_error);
            tinypy_value_t *finished = __tinypy_import_finish(vm, result, tail, canonical, canonical_size, fromlist, out_error);
            tinypy_internal_vm_deallocate(vm, canonical, canonical_size);
            return finished;
        }
    }
    if (name_size == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Empty module name", out_error);
        return NULL;
    }
    result = __tinypy_import_load_path(vm, name, name_size, globals, head_size, &tail, &not_found, out_error);
    /* mark_miss: the relative name is known missing once the absolute head
       imported. */
    if (relative_miss != NULL) {
        tinypy_value_t *head_key = tinypy_internal_name_from_bytes(vm, name, head_size);
        tinypy_value_t *head = tinypy_dict_get_optional(vm->modules, head_key);

        if (head != NULL && TINYPY_VALUE_KIND(head) != TINYPY_VALUE_NONE) {
            __tinypy_import_mark_missing(vm, relative_miss);
        }
        TINYPY_DECREF(head_key);
        TINYPY_DECREF(relative_miss);
    }
    tinypy_value_t *finished = __tinypy_import_finish(vm, result, tail, name, name_size, fromlist, out_error);
    return finished;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_import_module(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *globals, tinypy_value_t *fromlist, int32_t level, tinypy_error_t **out_error) {
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
    tinypy_value_t *result = tinypy_import_module_key(key, globals, fromlist, level, out_error);

    TINYPY_DECREF(key);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_import_module_key(tinypy_value_t *internal_name_key, tinypy_value_t *globals, tinypy_value_t *fromlist, int32_t level, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(internal_name_key);
    const char *name = (const char *)TINYPY_TEXT_BYTES(internal_name_key);
    size_t name_size = TINYPY_TEXT_BYTE_SIZE(internal_name_key);
    tinypy_value_t *package = NULL;
    tinypy_value_t *result;

    TINYPY_CLEAR_ERROR(out_error);
    if (memchr(name, '/', name_size) != NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "Import by filename is not supported.", out_error);
        return NULL;
    }
    if (level != 0 && __tinypy_import_parent_package(vm, globals, level, &package, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    result = __tinypy_import_module(vm, name, name_size, globals, fromlist, level, package, out_error);
    if (package != NULL) {
        TINYPY_DECREF(package);
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
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reload() argument must be module", out_error);
        return NULL;
    }
    name_value = tinypy_module_get_value_key(module, vm->internal_special_name_key);
    if (name_value == NULL || TINYPY_VALUE_KIND(name_value) != TINYPY_VALUE_STRING) {
        tinypy_internal_exception_raise_system_error(vm, "nameless module", out_error);
        return NULL;
    }
    (void)__tinypy_import_text_view(name_value, &name, &name_size);
    TINYPY_INCREF(name_value);
    key = tinypy_internal_name_from_bytes(vm, name, name_size);
    registered = tinypy_dict_get_optional(vm->modules, key);
    if (registered != module) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("reload(): module "),
            {name, name_size},
            TINYPY_MESSAGE_PART_LITERAL(" not in sys.modules")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_IMPORT, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        TINYPY_DECREF(key);
        TINYPY_DECREF(name_value);
        return NULL;
    }
    size_t parent_size = name_size;

    while (parent_size != 0U && name[parent_size - 1U] != '.') {
        parent_size -= 1U;
    }
    if (parent_size != 0U) {
        tinypy_value_t *parent_key = tinypy_internal_name_from_bytes(vm, name, parent_size - 1U);
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
    loaded = __tinypy_import_load_one(vm, name, name_size, name, name_size, NULL, NULL, module, &fresh, &not_found, out_error);
    if (loaded == NULL && not_found != 0 && tinypy_module_get_value_key(module, vm->internal_special_file_key) == NULL) {
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
tinypy_value_t *tinypy_internal_import_from(tinypy_value_t *module, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(module);
    tinypy_value_t *value = tinypy_object_get_attr_value(module, name, out_error);

    if (value == NULL && tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_ATTRIBUTE_ERROR, out_error) != 0) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("cannot import name "),
            TINYPY_MESSAGE_PART_TEXT(name),
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
    tinypy_value_t *all = NULL;
    tinypy_bool_t skip_private = TINYPY_FALSE;
    tinypy_bool_t success = TINYPY_TRUE;
    int64_t position;

    TINYPY_CLEAR_ERROR(out_error);
    int32_t status = tinypy_internal_object_get_optional_attr_key(module, vm->internal_special_all_key, &all, out_error);
    if (status < 0) {
        return TINYPY_FALSE;
    }
    if (status == 0) {
        tinypy_value_t *dict = NULL;
        status = tinypy_internal_object_get_optional_attr_key(module, vm->internal_special_dict_key, &dict, out_error);
        if (status < 0) {
            return TINYPY_FALSE;
        }
        if (status == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "from-import-* object has no __dict__ and no __all__", out_error);
            return TINYPY_FALSE;
        }
        tinypy_value_t *keys = tinypy_object_get_attr_value(dict, vm->internal_keys_key, out_error);
        TINYPY_DECREF(dict);
        if (keys == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_value_t *empty = TINYPY_RET_EMPTY_TUPLE(vm);
        all = tinypy_call(keys, empty, NULL, out_error);
        TINYPY_DECREF(empty);
        TINYPY_DECREF(keys);
        if (all == NULL) {
            return TINYPY_FALSE;
        }
        skip_private = TINYPY_TRUE;
    }
    for (position = 0; ; position += 1) {
        tinypy_value_t *key = __tinypy_import_sequence_item(all, position, out_error);
        if (key == NULL) {
            if (vm->raised_value != NULL && tinypy_type_is_subtype(vm->raised_value->type, vm->exception_types[TINYPY_EXCEPTION_INDEX_ERROR]) != TINYPY_FALSE) {
                __tinypy_import_discard_error(vm, out_error);
            }
            else {
                success = TINYPY_FALSE;
            }
            break;
        }
        if (skip_private != 0 && TINYPY_VALUE_KIND(key) == TINYPY_VALUE_STRING && TINYPY_TEXT_BYTE_SIZE(key) != 0U && TINYPY_TEXT_BYTES(key)[0] == '_') {
            TINYPY_DECREF(key);
            continue;
        }
        /* PyObject_GetAttr reads each listed name, which must be a string. */
        if (TINYPY_VALUE_KIND(key) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(key) != TINYPY_VALUE_UNICODE) {
            const tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("attribute name must be string, not '"),
                TINYPY_MESSAGE_PART_TYPE_NAME(key),
                TINYPY_MESSAGE_PART_LITERAL("'")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            TINYPY_DECREF(key);
            success = TINYPY_FALSE;
            break;
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
    TINYPY_DECREF(all);
    return success;
}
