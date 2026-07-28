#ifndef TINYPY_NATIVE_H
#define TINYPY_NATIVE_H

#include "tinypy/comparison.h"
//////////////////////////////////////////////////////////////////////////
#define TINYPY_NATIVE_TYPE_ABI_VERSION UINT32_C(1)

typedef tinypy_value_t *(*tinypy_native_function_callback_t)(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error);
typedef void (*tinypy_native_function_finalize_t)(void *user_data);

typedef tinypy_bool_t (*tinypy_native_construct_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error);
typedef void (*tinypy_native_finalize_t)(tinypy_value_t *instance, void *payload, void *user_data);
typedef tinypy_value_t *(*tinypy_native_call_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_native_unary_t)(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_native_binary_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *other, void *user_data, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_native_get_attribute_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *name, void *user_data, tinypy_error_t **out_error);
typedef tinypy_bool_t (*tinypy_native_set_attribute_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *name, tinypy_value_t *value, void *user_data, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_native_get_item_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *key, void *user_data, tinypy_error_t **out_error);
typedef tinypy_bool_t (*tinypy_native_set_item_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *key, tinypy_value_t *value, void *user_data, tinypy_error_t **out_error);
typedef ptrdiff_t (*tinypy_native_length_t)(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error);
typedef int32_t (*tinypy_native_contains_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *item, void *user_data, tinypy_error_t **out_error);
typedef tinypy_hash_t (*tinypy_native_hash_t)(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_native_compare_t)(tinypy_value_t *instance, void *payload, tinypy_value_t *other, tinypy_compare_operation_e operation, void *user_data, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
struct tinypy_native_type_spec_t {
    uint32_t abi_version;
    uint32_t struct_size;
    size_t payload_size;
    size_t payload_alignment;
    void *user_data;
    tinypy_native_construct_t construct;
    tinypy_native_finalize_t finalize;
    tinypy_native_call_t call;
    tinypy_native_unary_t repr;
    tinypy_native_hash_t hash;
    tinypy_native_compare_t compare;
    tinypy_native_get_attribute_t get_attribute;
    tinypy_native_set_attribute_t set_attribute;
    tinypy_native_get_item_t mapping_get;
    tinypy_native_set_item_t mapping_set;
    tinypy_native_length_t mapping_length;
    tinypy_native_get_item_t sequence_get;
    tinypy_native_set_item_t sequence_set;
    tinypy_native_length_t sequence_length;
    tinypy_native_contains_t contains;
    tinypy_native_unary_t iter;
    tinypy_native_unary_t next;
    tinypy_native_unary_t negative;
    tinypy_native_unary_t absolute;
    tinypy_native_binary_t add;
    tinypy_native_binary_t subtract;
    tinypy_native_binary_t multiply;
    tinypy_native_binary_t divide;
    tinypy_native_binary_t inplace_add;
    tinypy_native_binary_t inplace_subtract;
    tinypy_native_binary_t inplace_multiply;
    tinypy_native_binary_t inplace_divide;
    tinypy_native_binary_t reflected_add;
    tinypy_native_binary_t reflected_subtract;
    tinypy_native_binary_t reflected_multiply;
    tinypy_native_binary_t reflected_divide;
};
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_native_function_new(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize);
tinypy_value_t *tinypy_native_function_name(const tinypy_value_t *function);
void *tinypy_native_function_user_data(const tinypy_value_t *function);

void tinypy_native_type_spec_init(tinypy_native_type_spec_t *spec);
tinypy_type_t *tinypy_native_type_new(tinypy_vm_t *vm, const char *name, size_t name_size, const tinypy_type_t *const *bases, size_t base_count, tinypy_value_t *namespace_dict, const tinypy_native_type_spec_t *spec, tinypy_error_t **out_error);
tinypy_bool_t tinypy_native_type_update_spec(tinypy_type_t *type, const tinypy_native_type_spec_t *spec, tinypy_error_t **out_error);
tinypy_value_t *tinypy_native_instance_new(tinypy_type_t *type);
tinypy_bool_t tinypy_native_instance_construct(tinypy_value_t *instance, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void *tinypy_native_instance_payload(tinypy_value_t *instance);
const void *tinypy_native_instance_const_payload(const tinypy_value_t *instance);
const tinypy_native_type_spec_t *tinypy_native_type_spec(const tinypy_type_t *type);
//////////////////////////////////////////////////////////////////////////
#endif
