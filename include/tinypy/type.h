#ifndef TINYPY_TYPE_H
#define TINYPY_TYPE_H

#include "tinypy/types.h"

/* Every value starts with a reference count and a type pointer internally.
 * The returned type is VM-owned and borrowed. */
const tinypy_type_t *tinypy_object_type(const tinypy_value_t *value);
const char *tinypy_type_name(const tinypy_type_t *type, size_t *out_size);
const tinypy_type_t *tinypy_type_metaclass(const tinypy_type_t *type);
const tinypy_type_t *tinypy_type_base(const tinypy_type_t *type);
const tinypy_value_t *tinypy_type_dict(const tinypy_type_t *type);
int32_t tinypy_type_is_subtype(const tinypy_type_t *type, const tinypy_type_t *candidate_base);

/* A type object is also a regular tinypy_value_t. tinypy_type_new creates a Python 2
 * new-style heap type and returns one owned reference. With no bases, object
 * is used. explicit_metaclass may be NULL; in that case the most-derived
 * metaclass of the bases is selected and conflicts are rejected. namespace
 * may be NULL (an empty dict is created) or an existing dictionary. */
tinypy_value_t *tinypy_type_as_value(tinypy_type_t *type);
const tinypy_value_t *tinypy_type_as_const_value(const tinypy_type_t *type);
tinypy_type_t *tinypy_value_as_type(tinypy_value_t *value);
const tinypy_type_t *tinypy_value_as_const_type(const tinypy_value_t *value);

/* Returns one owned type reference. Semantic construction failures return
 * NULL and, when requested, place their diagnostic in out_error. */
tinypy_type_t *tinypy_type_new(tinypy_vm_t *vm, const char *name, size_t name_size, const tinypy_type_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_value_t *namespace_dict, tinypy_error_t **out_error);
size_t tinypy_type_bases_size(const tinypy_type_t *type);
const tinypy_type_t *tinypy_type_base_at(const tinypy_type_t *type, size_t index);
size_t tinypy_type_mro_size(const tinypy_type_t *type);
const tinypy_type_t *tinypy_type_mro_at(const tinypy_type_t *type, size_t index);

/* Attribute lookup returns a borrowed value or NULL when the name is absent. */
tinypy_value_t *tinypy_type_get_attr(const tinypy_type_t *type, const char *name, size_t name_size);
void tinypy_type_set_attr(tinypy_type_t *type, const char *name, size_t name_size, tinypy_value_t *value);

/* Generic instances currently provide Python instance-dict storage and MRO
 * class-attribute lookup. Descriptor invocation and __slots__ lowering are
 * layered on the same type slots but are not implied by these functions. */
tinypy_value_t *tinypy_instance_new(tinypy_type_t *type);
const tinypy_value_t *tinypy_instance_dict(const tinypy_value_t *instance);
tinypy_value_t *tinypy_instance_get_attr(tinypy_value_t *instance, const char *name, size_t name_size);
void tinypy_instance_set_attr(tinypy_value_t *instance, const char *name, size_t name_size, tinypy_value_t *value);

#endif
