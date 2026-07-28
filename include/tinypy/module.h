#ifndef TINYPY_MODULE_H
#define TINYPY_MODULE_H

#include "tinypy/types.h"
//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_module_content_kind_e {
    TINYPY_MODULE_CONTENT_SOURCE = 1,
    TINYPY_MODULE_CONTENT_MARSHAL_V2 = 2,
    TINYPY_MODULE_CONTENT_NATIVE = 3
} tinypy_module_content_kind_e;
//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_module_artifact_flag_e {
    TINYPY_MODULE_ARTIFACT_PACKAGE = 1 << 0
} tinypy_module_artifact_flag_e;
//////////////////////////////////////////////////////////////////////////
typedef tinypy_bool_t (*tinypy_native_module_initialize_t)(tinypy_value_t *module, void *user_data, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
struct tinypy_module_request_t {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *canonical_name;
    size_t canonical_name_size;
    const char *importer_name;
    size_t importer_name_size;
};
//////////////////////////////////////////////////////////////////////////
struct tinypy_module_artifact_t {
    uint32_t abi_version;
    uint32_t struct_size;
    tinypy_module_content_kind_e content_kind;
    uint32_t flags;
    const void *data;
    size_t data_size;
    const char *canonical_name;
    size_t canonical_name_size;
    const char *logical_filename;
    size_t logical_filename_size;
    const void *package_token;
    size_t package_token_size;
    tinypy_native_module_initialize_t native_initialize;
    void *native_user_data;
    uint32_t compile_feature_flags;
    int32_t compile_optimize_level;
    const tinypy_build_profile_t *build_profile;
};
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_module_new(tinypy_vm_t *vm, const char *name, size_t name_size);
tinypy_value_t *tinypy_module_dict(const tinypy_value_t *module);
tinypy_value_t *tinypy_module_name(const tinypy_value_t *module);
void tinypy_module_add_value(tinypy_value_t *module, const char *name, size_t name_size, tinypy_value_t *value);
tinypy_value_t *tinypy_module_get_value(tinypy_value_t *module, const char *name, size_t name_size);
tinypy_value_t *tinypy_vm_modules(const tinypy_vm_t *vm);
void tinypy_vm_set_module_finder(tinypy_vm_t *vm, tinypy_value_t *finder);
tinypy_value_t *tinypy_vm_module_finder(const tinypy_vm_t *vm);
tinypy_value_t *tinypy_import_module(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *globals, tinypy_value_t *fromlist, int32_t level, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
#endif
