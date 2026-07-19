#ifndef TINYPY_CLASS_H
#define TINYPY_CLASS_H

#include "tinypy/types.h"

tinypy_value_t *tinypy_class_new(const char *name, size_t name_size, tinypy_value_t *bases, tinypy_value_t *namespace_dict, tinypy_error_t **out_error);
tinypy_value_t *tinypy_class_name(const tinypy_value_t *class_value);
tinypy_value_t *tinypy_class_bases(const tinypy_value_t *class_value);
tinypy_value_t *tinypy_class_dict(const tinypy_value_t *class_value);
int32_t tinypy_class_is_subclass(const tinypy_value_t *class_value, const tinypy_value_t *candidate_base);
tinypy_value_t *tinypy_old_instance_new(tinypy_value_t *class_value);
tinypy_value_t *tinypy_old_instance_class(const tinypy_value_t *instance);
tinypy_value_t *tinypy_old_instance_dict(const tinypy_value_t *instance);

#endif
