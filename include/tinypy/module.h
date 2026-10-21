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
/* Fills the module of an import and returns TRUE, or FALSE with an exception
 * raised or an error in out_error. As _PyImport_LoadDynamicModule checks
 * PyErr_Occurred after it, an exception left raised fails the import even
 * when the initializer returns TRUE; a failed import leaves no module in
 * sys.modules. */
typedef tinypy_bool_t (*tinypy_native_module_initialize_t)(tinypy_value_t *module, void *user_data, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
struct tinypy_module_request_t {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *canonical_name;
    size_t canonical_name_size;
    const char *importer_name;
    size_t importer_name_size;
    /* Borrowed: the __path__ of the package of a submodule, which
     * find_module searches, or NULL for a top-level module, which the host
     * looks for where it keeps them (sys.path for the CLI). */
    tinypy_value_t *package_path;
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
    /* The directory of a package, the one entry of its __path__; NULL
     * leaves __path__ empty. */
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
/* Borrowed byte-string name; the returned module retains it. */
tinypy_value_t *tinypy_module_new_key(tinypy_value_t *name);
/* Borrowed fields; NULL until initialized on an allocation-only Python module. */
tinypy_value_t *tinypy_module_dict(const tinypy_value_t *module);
tinypy_value_t *tinypy_module_name(const tinypy_value_t *module);
void tinypy_module_swap_dict(tinypy_value_t *left, tinypy_value_t *right);
void tinypy_module_add_value(tinypy_value_t *module, const char *name, size_t name_size, tinypy_value_t *value);
tinypy_value_t *tinypy_module_get_value(tinypy_value_t *module, const char *name, size_t name_size);
/* Borrowed same-VM keys; get returns a borrowed value. */
void tinypy_module_add_value_key(tinypy_value_t *module, tinypy_value_t *name, tinypy_value_t *value);
tinypy_value_t *tinypy_module_get_value_key(tinypy_value_t *module, tinypy_value_t *name);
tinypy_value_t *tinypy_vm_modules(const tinypy_vm_t *vm);
/* The finder of the host, retained until it is replaced or the VM is
 * destroyed, follows the PEP 302 protocol of a sys.meta_path entry and is
 * asked after sys.meta_path: find_module(name, path) returns a loader or
 * None, and an ImportError it raises means that nobody provides the module.
 * The module load_module(name) returns is the result of the import, which
 * registers it when the loader did not and removes the entry of a loader
 * that failed or returned None, reported as not found. NULL removes it. */
void tinypy_vm_set_module_finder(tinypy_vm_t *vm, tinypy_value_t *finder);
tinypy_value_t *tinypy_vm_module_finder(const tinypy_vm_t *vm);
tinypy_value_t *tinypy_import_module(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *globals, tinypy_value_t *fromlist, int32_t level, tinypy_error_t **out_error);
/* Borrows a byte-string module name; host path processing uses its bytes. */
tinypy_value_t *tinypy_import_module_key(tinypy_value_t *name, tinypy_value_t *globals, tinypy_value_t *fromlist, int32_t level, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
#endif
