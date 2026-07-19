#ifndef TINYPY_DESCRIPTOR_H
#define TINYPY_DESCRIPTOR_H

#include "tinypy/types.h"

tinypy_value_t *tinypy_static_method_new(tinypy_value_t *callable);
tinypy_value_t *tinypy_static_method_callable(const tinypy_value_t *descriptor);
tinypy_value_t *tinypy_class_method_new(tinypy_value_t *callable);
tinypy_value_t *tinypy_class_method_callable(const tinypy_value_t *descriptor);
tinypy_value_t *tinypy_property_new(tinypy_vm_t *vm, tinypy_value_t *getter, tinypy_value_t *setter, tinypy_value_t *deleter, tinypy_value_t *doc);
tinypy_value_t *tinypy_property_getter(const tinypy_value_t *property);
tinypy_value_t *tinypy_property_setter(const tinypy_value_t *property);
tinypy_value_t *tinypy_property_deleter(const tinypy_value_t *property);
tinypy_value_t *tinypy_property_doc(const tinypy_value_t *property);

#endif
