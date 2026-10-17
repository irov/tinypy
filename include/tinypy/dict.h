#ifndef TINYPY_DICT_H
#define TINYPY_DICT_H

#include "tinypy/types.h"
//////////////////////////////////////////////////////////////////////////
/* Mutable Python 2 dictionary. get returns a borrowed value; set retains key
 * and value. Keys belong to the same VM. A key that is unhashable or whose
 * __hash__ or __eq__ raises leaves that exception raised: get and
 * get_optional return NULL as for a missing key, contains returns false, set
 * stores nothing and delete removes nothing, so a caller that may pass such a
 * key checks tinypy_vm_has_error and handles or clears the exception. */
tinypy_value_t *tinypy_dict_new(tinypy_vm_t *vm);
size_t tinypy_dict_size(const tinypy_value_t *dict);
tinypy_value_t *tinypy_dict_get(const tinypy_value_t *dict, const tinypy_value_t *key);
tinypy_value_t *tinypy_dict_get_optional(const tinypy_value_t *dict, const tinypy_value_t *key);
tinypy_bool_t tinypy_dict_contains(const tinypy_value_t *dict, const tinypy_value_t *key);
void tinypy_dict_set(tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value);
void tinypy_dict_delete(tinypy_value_t *dict, const tinypy_value_t *key);
void tinypy_dict_clear(tinypy_value_t *dict);
uint64_t tinypy_dict_version(const tinypy_value_t *dict);
tinypy_bool_t tinypy_dict_next(const tinypy_value_t *dict, size_t *position, tinypy_value_t **out_key, tinypy_value_t **out_value);
//////////////////////////////////////////////////////////////////////////
#endif
