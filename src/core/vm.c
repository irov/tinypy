#include "tinypy/vm.h"

#include "internal.h"

#include <float.h>
#include <string.h>
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_type(tinypy_vm_t *vm, tinypy_type_t *type, tinypy_type_t *metaclass, const char *name, size_t name_size, size_t basic_size, size_t item_size, uint64_t flags, tinypy_type_t *base_type, tinypy_release_references_slot_t release_references, tinypy_destroy_slot_t destroy) {
    (void)memset(type, 0, sizeof(*type));
    type->base.base.ref = 1U;
    type->base.base.type = metaclass;
    type->vm = vm;
    type->name = name;
    type->name_size = name_size;
    type->basic_size = basic_size;
    type->item_size = item_size;
    type->flags = flags;
    type->base_type = base_type;
    type->release_references = release_references;
    type->traverse_references = release_references;
    type->destroy = destroy;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_types(tinypy_vm_t *vm) {
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_TYPE], &vm->types[TINYPY_VALUE_TYPE], "type", 4U,
        sizeof(tinypy_type_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE | TINYPY_TYPE_FLAG_TYPE_SUBCLASS,
        &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_type_release_references,
        tinypy_internal_type_destroy);
    vm->types[TINYPY_VALUE_TYPE].call = tinypy_internal_type_call;
    vm->types[TINYPY_VALUE_TYPE].create = tinypy_internal_type_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_INSTANCE], &vm->types[TINYPY_VALUE_TYPE], "object", 6U,
        sizeof(tinypy_value_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        NULL, NULL, NULL);
    vm->types[TINYPY_VALUE_INSTANCE].create = tinypy_internal_object_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_NONE], &vm->types[TINYPY_VALUE_TYPE], "NoneType", 8U,
        sizeof(tinypy_none_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE], NULL, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_NOT_IMPLEMENTED], &vm->types[TINYPY_VALUE_TYPE], "NotImplementedType", 18U,
        sizeof(tinypy_none_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE], NULL, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_INVALID], &vm->types[TINYPY_VALUE_TYPE], "basestring", 10U,
        sizeof(tinypy_value_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->types[TINYPY_VALUE_INSTANCE], NULL, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_BOOL], &vm->types[TINYPY_VALUE_TYPE], "bool", 4U,
        sizeof(tinypy_integer_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INTEGER], NULL, NULL);
    vm->types[TINYPY_VALUE_BOOL].create = tinypy_internal_bool_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_INTEGER], &vm->types[TINYPY_VALUE_TYPE], "int", 3U,
        sizeof(tinypy_integer_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->types[TINYPY_VALUE_INSTANCE], NULL, NULL);
    vm->types[TINYPY_VALUE_INTEGER].create = tinypy_internal_integer_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_STRING], &vm->types[TINYPY_VALUE_TYPE], "str", 3U,
        offsetof(tinypy_string_object_t, bytes), 1U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->types[TINYPY_VALUE_INVALID], NULL, NULL);
    vm->types[TINYPY_VALUE_STRING].create = tinypy_internal_string_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_UNICODE], &vm->types[TINYPY_VALUE_TYPE], "unicode", 7U,
        offsetof(tinypy_unicode_object_t, utf8), 1U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->types[TINYPY_VALUE_INVALID], NULL, tinypy_internal_unicode_destroy);
    vm->types[TINYPY_VALUE_UNICODE].create = tinypy_internal_unicode_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_LONG], &vm->types[TINYPY_VALUE_TYPE], "long", 4U,
        offsetof(tinypy_long_object_t, digits), sizeof(uint16_t),
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->types[TINYPY_VALUE_INSTANCE], NULL, NULL);
    vm->types[TINYPY_VALUE_LONG].create = tinypy_internal_long_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_FLOAT], &vm->types[TINYPY_VALUE_TYPE], "float", 5U,
        sizeof(tinypy_float_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->types[TINYPY_VALUE_INSTANCE], NULL, NULL);
    vm->types[TINYPY_VALUE_FLOAT].create = tinypy_internal_float_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_COMPLEX], &vm->types[TINYPY_VALUE_TYPE], "complex", 7U,
        sizeof(tinypy_complex_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->types[TINYPY_VALUE_INSTANCE], NULL, NULL);
    vm->types[TINYPY_VALUE_COMPLEX].create = tinypy_internal_complex_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_TUPLE], &vm->types[TINYPY_VALUE_TYPE], "tuple", 5U,
        offsetof(tinypy_tuple_object_t, items),
        sizeof(tinypy_value_t *),
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->types[TINYPY_VALUE_INSTANCE], tinypy_internal_tuple_release_references, NULL);
    vm->types[TINYPY_VALUE_TUPLE].create = tinypy_internal_tuple_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_LIST], &vm->types[TINYPY_VALUE_TYPE], "list", 4U,
        sizeof(tinypy_list_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_list_release_references, tinypy_internal_list_destroy);
    vm->types[TINYPY_VALUE_LIST].create = tinypy_internal_list_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_DICT], &vm->types[TINYPY_VALUE_TYPE], "dict", 4U,
        sizeof(tinypy_dict_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_dict_release_references, tinypy_internal_dict_destroy);
    vm->types[TINYPY_VALUE_DICT].create = tinypy_internal_dict_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_SET], &vm->types[TINYPY_VALUE_TYPE], "set", 3U,
        sizeof(tinypy_set_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_set_release_references, NULL);
    vm->types[TINYPY_VALUE_SET].iter = tinypy_internal_set_iter;
    vm->types[TINYPY_VALUE_SET].create = tinypy_internal_set_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_FROZENSET], &vm->types[TINYPY_VALUE_TYPE], "frozenset", 9U,
        sizeof(tinypy_set_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_set_release_references, NULL);
    vm->types[TINYPY_VALUE_FROZENSET].iter = tinypy_internal_set_iter;
    vm->types[TINYPY_VALUE_FROZENSET].create = tinypy_internal_frozenset_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_OUTPUT_STREAM], &vm->types[TINYPY_VALUE_TYPE], "tinypy.output", 13U,
        sizeof(tinypy_output_stream_object_t), 0U,
        0U, &vm->types[TINYPY_VALUE_INSTANCE], NULL, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_CODE], &vm->types[TINYPY_VALUE_TYPE], "code", 4U,
        sizeof(tinypy_code_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_code_release_references, tinypy_internal_code_destroy);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_FRAME], &vm->types[TINYPY_VALUE_TYPE], "frame", 5U,
        offsetof(tinypy_frame_object_t, locals_plus), sizeof(tinypy_value_t *),
        0U, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_frame_release_references, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_FUNCTION], &vm->types[TINYPY_VALUE_TYPE], "function", 8U,
        sizeof(tinypy_function_object_t), 0U,
        0U, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_function_release_references, NULL);
    vm->types[TINYPY_VALUE_FUNCTION].call = tinypy_internal_function_call;
    vm->types[TINYPY_VALUE_FUNCTION].descriptor_get = tinypy_internal_function_descriptor_get;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_ITERATOR], &vm->types[TINYPY_VALUE_TYPE], "iterator", 8U,
        sizeof(tinypy_iterator_object_t), 0U,
        0U, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_iterator_release_references, NULL);
    vm->types[TINYPY_VALUE_ITERATOR].iter = tinypy_internal_iterator_iter;
    vm->types[TINYPY_VALUE_ITERATOR].next = tinypy_internal_iterator_next;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_METHOD], &vm->types[TINYPY_VALUE_TYPE], "instancemethod", 14U,
        sizeof(tinypy_method_object_t), 0U,
        0U, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_method_release_references, NULL);
    vm->types[TINYPY_VALUE_METHOD].call = tinypy_internal_method_call;
    vm->types[TINYPY_VALUE_METHOD].descriptor_get = tinypy_internal_method_descriptor_get;
    vm->types[TINYPY_VALUE_METHOD].rich_compare = tinypy_internal_method_compare;
    vm->types[TINYPY_VALUE_METHOD].create = tinypy_internal_method_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_CELL], &vm->types[TINYPY_VALUE_TYPE], "cell", 4U,
        sizeof(tinypy_cell_object_t), 0U,
        0U, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_cell_release_references, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_SLICE], &vm->types[TINYPY_VALUE_TYPE], "slice", 5U,
        sizeof(tinypy_slice_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_slice_release_references, NULL);
    vm->types[TINYPY_VALUE_SLICE].create = tinypy_internal_slice_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_MODULE], &vm->types[TINYPY_VALUE_TYPE], "module", 6U,
        sizeof(tinypy_module_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_module_release_references, NULL);
    vm->types[TINYPY_VALUE_MODULE].traverse_references = tinypy_internal_module_traverse_references;
    vm->types[TINYPY_VALUE_MODULE].has_instance_dict = INT32_C(1);
    vm->types[TINYPY_VALUE_MODULE].dict_offset = offsetof(tinypy_module_object_t, dict);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_NATIVE_FUNCTION], &vm->types[TINYPY_VALUE_TYPE], "builtin_function_or_method", 26U,
        sizeof(tinypy_native_function_object_t), 0U,
        0U, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_native_function_release_references,
        tinypy_internal_native_function_destroy);
    vm->types[TINYPY_VALUE_NATIVE_FUNCTION].call = tinypy_internal_native_function_call;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_STATIC_METHOD], &vm->types[TINYPY_VALUE_TYPE], "staticmethod", 12U,
        sizeof(tinypy_callable_descriptor_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_callable_descriptor_release_references, NULL);
    vm->types[TINYPY_VALUE_STATIC_METHOD].descriptor_get = tinypy_internal_static_method_get;
    vm->types[TINYPY_VALUE_STATIC_METHOD].create = tinypy_internal_static_method_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_CLASS_METHOD], &vm->types[TINYPY_VALUE_TYPE], "classmethod", 11U,
        sizeof(tinypy_callable_descriptor_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_callable_descriptor_release_references, NULL);
    vm->types[TINYPY_VALUE_CLASS_METHOD].descriptor_get = tinypy_internal_class_method_get;
    vm->types[TINYPY_VALUE_CLASS_METHOD].create = tinypy_internal_class_method_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_PROPERTY], &vm->types[TINYPY_VALUE_TYPE], "property", 8U,
        sizeof(tinypy_property_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_property_release_references, NULL);
    vm->types[TINYPY_VALUE_PROPERTY].descriptor_get = tinypy_internal_property_get;
    vm->types[TINYPY_VALUE_PROPERTY].descriptor_set = tinypy_internal_property_set;
    vm->types[TINYPY_VALUE_PROPERTY].create = tinypy_internal_property_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_SUPER], &vm->types[TINYPY_VALUE_TYPE], "super", 5U,
        sizeof(tinypy_super_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_super_release_references, NULL);
    vm->types[TINYPY_VALUE_SUPER].get_attribute = tinypy_internal_super_get_attribute;
    vm->types[TINYPY_VALUE_SUPER].create = tinypy_internal_super_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_TRACEBACK], &vm->types[TINYPY_VALUE_TYPE], "traceback", 9U,
        sizeof(tinypy_traceback_object_t), 0U,
        0U, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_traceback_release_references, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_GENERATOR], &vm->types[TINYPY_VALUE_TYPE], "generator", 9U,
        sizeof(tinypy_generator_object_t), 0U,
        0U, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_generator_release_references, NULL);
    vm->types[TINYPY_VALUE_GENERATOR].iter = tinypy_internal_generator_iter;
    vm->types[TINYPY_VALUE_GENERATOR].next = tinypy_internal_generator_next;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_XRANGE], &vm->types[TINYPY_VALUE_TYPE], "xrange", 6U,
        sizeof(tinypy_xrange_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE], NULL, NULL);
    vm->types[TINYPY_VALUE_XRANGE].iter = tinypy_internal_xrange_iter;
    vm->types[TINYPY_VALUE_XRANGE].create = tinypy_internal_xrange_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_ENUMERATE], &vm->types[TINYPY_VALUE_TYPE], "enumerate", 9U,
        sizeof(tinypy_enumerate_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_enumerate_release_references, NULL);
    vm->types[TINYPY_VALUE_ENUMERATE].iter = tinypy_internal_enumerate_iter;
    vm->types[TINYPY_VALUE_ENUMERATE].next = tinypy_internal_enumerate_next;
    vm->types[TINYPY_VALUE_ENUMERATE].create = tinypy_internal_enumerate_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_REVERSED], &vm->types[TINYPY_VALUE_TYPE], "reversed", 8U,
        sizeof(tinypy_reversed_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_reversed_release_references, NULL);
    vm->types[TINYPY_VALUE_REVERSED].iter = tinypy_internal_reversed_iter;
    vm->types[TINYPY_VALUE_REVERSED].next = tinypy_internal_reversed_next;
    vm->types[TINYPY_VALUE_REVERSED].create = tinypy_internal_reversed_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_BUFFER], &vm->types[TINYPY_VALUE_TYPE], "buffer", 6U,
        sizeof(tinypy_buffer_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_buffer_release_references, NULL);
    (void)memset(&vm->buffer_sequence_slots, 0, sizeof(vm->buffer_sequence_slots));
    vm->buffer_sequence_slots.length = tinypy_internal_buffer_length;
    (void)memset(&vm->buffer_mapping_slots, 0, sizeof(vm->buffer_mapping_slots));
    vm->buffer_mapping_slots.length = tinypy_internal_buffer_length;
    vm->buffer_mapping_slots.get_item = tinypy_internal_buffer_get_item;
    vm->types[TINYPY_VALUE_BUFFER].sequence_slots = &vm->buffer_sequence_slots;
    vm->types[TINYPY_VALUE_BUFFER].mapping_slots = &vm->buffer_mapping_slots;
    vm->types[TINYPY_VALUE_BUFFER].repr = tinypy_internal_buffer_repr;
    vm->types[TINYPY_VALUE_BUFFER].string = tinypy_internal_buffer_string;
    vm->types[TINYPY_VALUE_BUFFER].create = tinypy_internal_buffer_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_BYTEARRAY], &vm->types[TINYPY_VALUE_TYPE], "bytearray", 9U,
        sizeof(tinypy_bytearray_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        NULL, tinypy_internal_bytearray_destroy);
    (void)memset(&vm->bytearray_sequence_slots, 0, sizeof(vm->bytearray_sequence_slots));
    vm->bytearray_sequence_slots.length = tinypy_internal_bytearray_length;
    (void)memset(&vm->bytearray_mapping_slots, 0, sizeof(vm->bytearray_mapping_slots));
    vm->bytearray_mapping_slots.length = tinypy_internal_bytearray_length;
    vm->bytearray_mapping_slots.get_item = tinypy_internal_bytearray_get_item;
    vm->bytearray_mapping_slots.set_item = tinypy_internal_bytearray_set_item;
    vm->types[TINYPY_VALUE_BYTEARRAY].sequence_slots = &vm->bytearray_sequence_slots;
    vm->types[TINYPY_VALUE_BYTEARRAY].mapping_slots = &vm->bytearray_mapping_slots;
    vm->types[TINYPY_VALUE_BYTEARRAY].repr = tinypy_internal_bytearray_repr;
    vm->types[TINYPY_VALUE_BYTEARRAY].string = tinypy_internal_bytearray_string;
    vm->types[TINYPY_VALUE_BYTEARRAY].create = tinypy_internal_bytearray_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_WEAKREF], &vm->types[TINYPY_VALUE_TYPE], "weakref", 7U,
        sizeof(tinypy_weakref_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_weakref_release_references, tinypy_internal_weakref_destroy);
    vm->types[TINYPY_VALUE_WEAKREF].layout_kind = TINYPY_VALUE_WEAKREF;
    vm->types[TINYPY_VALUE_WEAKREF].call = tinypy_internal_weakref_call;
    vm->types[TINYPY_VALUE_WEAKREF].hash = tinypy_internal_weakref_hash;
    vm->types[TINYPY_VALUE_WEAKREF].rich_compare = tinypy_internal_weakref_compare;
    vm->types[TINYPY_VALUE_WEAKREF].create = tinypy_internal_weakref_create;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_DICT_KEYS], &vm->types[TINYPY_VALUE_TYPE], "dict_keys", 9U,
        sizeof(tinypy_dict_view_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_dict_view_release_references, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_DICT_VALUES], &vm->types[TINYPY_VALUE_TYPE], "dict_values", 11U,
        sizeof(tinypy_dict_view_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_dict_view_release_references, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_DICT_ITEMS], &vm->types[TINYPY_VALUE_TYPE], "dict_items", 10U,
        sizeof(tinypy_dict_view_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_dict_view_release_references, NULL);
    (void)memset(&vm->dict_view_sequence_slots, 0, sizeof(vm->dict_view_sequence_slots));
    vm->dict_view_sequence_slots.length = tinypy_internal_dict_view_length;
    vm->dict_view_sequence_slots.contains = tinypy_internal_dict_view_contains;
    vm->types[TINYPY_VALUE_DICT_KEYS].sequence_slots = &vm->dict_view_sequence_slots;
    vm->types[TINYPY_VALUE_DICT_VALUES].sequence_slots = &vm->dict_view_sequence_slots;
    vm->types[TINYPY_VALUE_DICT_ITEMS].sequence_slots = &vm->dict_view_sequence_slots;
    vm->types[TINYPY_VALUE_DICT_KEYS].iter = tinypy_internal_dict_view_iter;
    vm->types[TINYPY_VALUE_DICT_VALUES].iter = tinypy_internal_dict_view_iter;
    vm->types[TINYPY_VALUE_DICT_ITEMS].iter = tinypy_internal_dict_view_iter;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_ELLIPSIS], &vm->types[TINYPY_VALUE_TYPE], "ellipsis", 8U,
        sizeof(tinypy_none_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE], NULL, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_FILE], &vm->types[TINYPY_VALUE_TYPE], "file", 4U,
        sizeof(tinypy_file_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->types[TINYPY_VALUE_INSTANCE], NULL, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR], &vm->types[TINYPY_VALUE_TYPE], "getset_descriptor", 17U,
        sizeof(tinypy_c_descriptor_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_c_descriptor_release_references, NULL);
    vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR].descriptor_get = tinypy_internal_c_descriptor_get;
    vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR].descriptor_set = tinypy_internal_c_descriptor_set;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR], &vm->types[TINYPY_VALUE_TYPE], "member_descriptor", 17U,
        sizeof(tinypy_c_descriptor_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_c_descriptor_release_references, NULL);
    vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR].descriptor_get = tinypy_internal_c_descriptor_get;
    vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR].descriptor_set = tinypy_internal_c_descriptor_set;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_CLASS], &vm->types[TINYPY_VALUE_TYPE], "classobj", 8U,
        sizeof(tinypy_class_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_class_release_references, NULL);
    vm->types[TINYPY_VALUE_CLASS].call = tinypy_internal_class_call;
    vm->types[TINYPY_VALUE_CLASS].create = tinypy_internal_class_create;
    vm->types[TINYPY_VALUE_CLASS].get_attribute = tinypy_internal_class_get_attribute;
    vm->types[TINYPY_VALUE_CLASS].set_attribute = tinypy_internal_class_set_attribute;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_OLD_INSTANCE], &vm->types[TINYPY_VALUE_TYPE], "instance", 8U,
        sizeof(tinypy_old_instance_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_old_instance_release_references, NULL);
    vm->types[TINYPY_VALUE_OLD_INSTANCE].get_attribute = tinypy_internal_old_instance_get_attribute;
    vm->types[TINYPY_VALUE_OLD_INSTANCE].set_attribute = tinypy_internal_old_instance_set_attribute;
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_PARTIAL], &vm->types[TINYPY_VALUE_TYPE], "functools.partial", 17U,
        sizeof(tinypy_partial_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_partial_release_references, NULL);
    vm->types[TINYPY_VALUE_PARTIAL].call = tinypy_internal_partial_call;
    vm->types[TINYPY_VALUE_PARTIAL].create = tinypy_internal_partial_create;
    vm->types[TINYPY_VALUE_PARTIAL].has_instance_dict = INT32_C(1);
    vm->types[TINYPY_VALUE_PARTIAL].dict_offset = offsetof(tinypy_partial_object_t, dict);
    vm->types[TINYPY_VALUE_PARTIAL].weakref_offset = offsetof(tinypy_partial_object_t, weakrefs);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_SRE_PATTERN], &vm->types[TINYPY_VALUE_TYPE], "_sre.SRE_Pattern", 16U,
        sizeof(tinypy_sre_pattern_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_sre_pattern_release_references, tinypy_internal_sre_pattern_destroy);
    vm->types[TINYPY_VALUE_SRE_PATTERN].weakref_offset = offsetof(tinypy_sre_pattern_object_t, weakrefs);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_SRE_MATCH], &vm->types[TINYPY_VALUE_TYPE], "_sre.SRE_Match", 14U,
        sizeof(tinypy_sre_match_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_sre_match_release_references, tinypy_internal_sre_match_destroy);
    vm->types[TINYPY_VALUE_TYPE].layout_kind = TINYPY_VALUE_TYPE;
    vm->types[TINYPY_VALUE_TYPE].weakref_offset = offsetof(tinypy_type_t, weakrefs);
    vm->types[TINYPY_VALUE_INSTANCE].layout_kind = TINYPY_VALUE_INSTANCE;
    vm->types[TINYPY_VALUE_NONE].layout_kind = TINYPY_VALUE_NONE;
    vm->types[TINYPY_VALUE_NOT_IMPLEMENTED].layout_kind = TINYPY_VALUE_NOT_IMPLEMENTED;
    vm->types[TINYPY_VALUE_BOOL].layout_kind = TINYPY_VALUE_BOOL;
    vm->types[TINYPY_VALUE_INTEGER].layout_kind = TINYPY_VALUE_INTEGER;
    vm->types[TINYPY_VALUE_STRING].layout_kind = TINYPY_VALUE_STRING;
    vm->types[TINYPY_VALUE_UNICODE].layout_kind = TINYPY_VALUE_UNICODE;
    vm->types[TINYPY_VALUE_LONG].layout_kind = TINYPY_VALUE_LONG;
    vm->types[TINYPY_VALUE_FLOAT].layout_kind = TINYPY_VALUE_FLOAT;
    vm->types[TINYPY_VALUE_COMPLEX].layout_kind = TINYPY_VALUE_COMPLEX;
    vm->types[TINYPY_VALUE_TUPLE].layout_kind = TINYPY_VALUE_TUPLE;
    vm->types[TINYPY_VALUE_LIST].layout_kind = TINYPY_VALUE_LIST;
    vm->types[TINYPY_VALUE_DICT].layout_kind = TINYPY_VALUE_DICT;
    vm->types[TINYPY_VALUE_SET].layout_kind = TINYPY_VALUE_SET;
    vm->types[TINYPY_VALUE_FROZENSET].layout_kind = TINYPY_VALUE_FROZENSET;
    vm->types[TINYPY_VALUE_OUTPUT_STREAM].layout_kind = TINYPY_VALUE_OUTPUT_STREAM;
    vm->types[TINYPY_VALUE_CODE].layout_kind = TINYPY_VALUE_CODE;
    vm->types[TINYPY_VALUE_FRAME].layout_kind = TINYPY_VALUE_FRAME;
    vm->types[TINYPY_VALUE_FUNCTION].layout_kind = TINYPY_VALUE_FUNCTION;
    vm->types[TINYPY_VALUE_ITERATOR].layout_kind = TINYPY_VALUE_ITERATOR;
    vm->types[TINYPY_VALUE_METHOD].layout_kind = TINYPY_VALUE_METHOD;
    vm->types[TINYPY_VALUE_CELL].layout_kind = TINYPY_VALUE_CELL;
    vm->types[TINYPY_VALUE_SLICE].layout_kind = TINYPY_VALUE_SLICE;
    vm->types[TINYPY_VALUE_MODULE].layout_kind = TINYPY_VALUE_MODULE;
    vm->types[TINYPY_VALUE_NATIVE_FUNCTION].layout_kind = TINYPY_VALUE_NATIVE_FUNCTION;
    vm->types[TINYPY_VALUE_STATIC_METHOD].layout_kind = TINYPY_VALUE_STATIC_METHOD;
    vm->types[TINYPY_VALUE_CLASS_METHOD].layout_kind = TINYPY_VALUE_CLASS_METHOD;
    vm->types[TINYPY_VALUE_PROPERTY].layout_kind = TINYPY_VALUE_PROPERTY;
    vm->types[TINYPY_VALUE_SUPER].layout_kind = TINYPY_VALUE_SUPER;
    vm->types[TINYPY_VALUE_TRACEBACK].layout_kind = TINYPY_VALUE_TRACEBACK;
    vm->types[TINYPY_VALUE_GENERATOR].layout_kind = TINYPY_VALUE_GENERATOR;
    vm->types[TINYPY_VALUE_XRANGE].layout_kind = TINYPY_VALUE_XRANGE;
    vm->types[TINYPY_VALUE_ENUMERATE].layout_kind = TINYPY_VALUE_ENUMERATE;
    vm->types[TINYPY_VALUE_REVERSED].layout_kind = TINYPY_VALUE_REVERSED;
    vm->types[TINYPY_VALUE_BUFFER].layout_kind = TINYPY_VALUE_BUFFER;
    vm->types[TINYPY_VALUE_BYTEARRAY].layout_kind = TINYPY_VALUE_BYTEARRAY;
    vm->types[TINYPY_VALUE_WEAKREF].layout_kind = TINYPY_VALUE_WEAKREF;
    vm->types[TINYPY_VALUE_DICT_KEYS].layout_kind = TINYPY_VALUE_DICT_KEYS;
    vm->types[TINYPY_VALUE_DICT_VALUES].layout_kind = TINYPY_VALUE_DICT_VALUES;
    vm->types[TINYPY_VALUE_DICT_ITEMS].layout_kind = TINYPY_VALUE_DICT_ITEMS;
    vm->types[TINYPY_VALUE_ELLIPSIS].layout_kind = TINYPY_VALUE_ELLIPSIS;
    vm->types[TINYPY_VALUE_FILE].layout_kind = TINYPY_VALUE_FILE;
    vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR].layout_kind = TINYPY_VALUE_GETSET_DESCRIPTOR;
    vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR].layout_kind = TINYPY_VALUE_MEMBER_DESCRIPTOR;
    vm->types[TINYPY_VALUE_CLASS].layout_kind = TINYPY_VALUE_CLASS;
    vm->types[TINYPY_VALUE_CLASS].weakref_offset = offsetof(tinypy_class_object_t, weakrefs);
    vm->types[TINYPY_VALUE_OLD_INSTANCE].layout_kind = TINYPY_VALUE_OLD_INSTANCE;
    vm->types[TINYPY_VALUE_OLD_INSTANCE].weakref_offset = offsetof(tinypy_old_instance_object_t, weakrefs);
    vm->types[TINYPY_VALUE_PARTIAL].layout_kind = TINYPY_VALUE_PARTIAL;
    vm->types[TINYPY_VALUE_SRE_PATTERN].layout_kind = TINYPY_VALUE_SRE_PATTERN;
    vm->types[TINYPY_VALUE_SRE_MATCH].layout_kind = TINYPY_VALUE_SRE_MATCH;
    vm->types[TINYPY_VALUE_INSTANCE].slots_offset = offsetof(tinypy_instance_object_t, slots);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_type_dicts(tinypy_vm_t *vm) {
    size_t index;

    for (index = 0U; index < TINYPY_BUILTIN_TYPE_COUNT; ++index) {
        tinypy_dict_object_t *dict = &vm->builtin_type_dicts[index];

        dict->base.ref = 1U;
        dict->base.type = &vm->types[TINYPY_VALUE_DICT];
        dict->mask = TINYPY_DICT_MIN_SIZE - 1U;
        dict->table = dict->small_table;
        dict->type_dictionary = INT32_C(1);
        dict->type_owner = &vm->types[index];
        vm->types[index].dict = &dict->base;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_type_docs(tinypy_vm_t *vm) {
    static const struct {
        tinypy_value_type_e kind;
        const char *doc;
    } docs[] = {
        {TINYPY_VALUE_INSTANCE, "The most base type"},
        {TINYPY_VALUE_TYPE, "type(object) -> the object's type; type(name, bases, dict) -> a new type"},
        {TINYPY_VALUE_BOOL, "bool(x) -> bool"},
        {TINYPY_VALUE_INTEGER, "int(x=0) -> int or long"},
        {TINYPY_VALUE_LONG, "long(x=0) -> long"},
        {TINYPY_VALUE_FLOAT, "float(x) -> floating point number"},
        {TINYPY_VALUE_COMPLEX, "complex(real[, imag]) -> complex number"},
        {TINYPY_VALUE_STRING, "str(object='') -> string"},
        {TINYPY_VALUE_UNICODE, "unicode(string[, encoding[, errors]]) -> object"},
        {TINYPY_VALUE_INVALID, "Type basestring cannot be instantiated; it is the base for str and unicode."},
        {TINYPY_VALUE_TUPLE, "tuple() -> empty tuple; tuple(iterable) -> tuple initialized from iterable"},
        {TINYPY_VALUE_LIST, "list() -> new empty list; list(iterable) -> new list initialized from iterable"},
        {TINYPY_VALUE_DICT, "dict() -> new empty dictionary"},
        {TINYPY_VALUE_SET, "set() -> new empty set object"},
        {TINYPY_VALUE_FROZENSET, "frozenset() -> empty frozenset object"},
        {TINYPY_VALUE_BUFFER, "buffer(object[, offset[, size]]) -> read-only buffer"},
        {TINYPY_VALUE_BYTEARRAY, "bytearray(iterable_of_ints) -> bytearray"},
        {TINYPY_VALUE_XRANGE, "xrange(stop) -> xrange object"},
        {TINYPY_VALUE_ENUMERATE, "enumerate(sequence[, start=0]) -> iterator for index, value pairs"},
        {TINYPY_VALUE_REVERSED, "reversed(sequence) -> reverse iterator"},
        {TINYPY_VALUE_SLICE, "slice(stop); slice(start, stop[, step])"}
    };
    size_t index;

    for (index = 0U; index < TINYPY_BUILTIN_TYPE_COUNT; ++index) {
        tinypy_type_set_attr_key(&vm->types[index], vm->internal_special_doc_key, &vm->none_object.base);
    }
    for (index = 0U; index < sizeof(docs) / sizeof(docs[0]); ++index) {
        size_t doc_size = strlen(docs[index].doc);
        tinypy_value_t *doc = tinypy_string_from_bytes(vm, docs[index].doc, doc_size);

        tinypy_type_set_attr_key(&vm->types[docs[index].kind], vm->internal_special_doc_key, doc);
        TINYPY_DECREF(doc);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_builtins(tinypy_vm_t *vm) {
    tinypy_value_t *false_value;

    vm->builtins = tinypy_dict_new(vm);
    tinypy_value_t *none_value = TINYPY_RET_NONE(vm);
    tinypy_value_t *true_value = TINYPY_RET_TRUE(vm);
    false_value = TINYPY_RET_FALSE(vm);
    tinypy_dict_set(vm->builtins, vm->internal_none_key, none_value);
    tinypy_dict_set(vm->builtins, vm->internal_not_implemented_key, &vm->not_implemented_object.base);
    tinypy_dict_set(vm->builtins, vm->internal_true_key, true_value);
    tinypy_dict_set(vm->builtins, vm->internal_false_key, false_value);
    tinypy_dict_set(vm->builtins, vm->internal_special_debug_key, vm->optimize_level == 0 ? true_value : false_value);
    tinypy_dict_set(vm->builtins, vm->internal_ellipsis_key, &vm->ellipsis_object.base);
    tinypy_dict_set(vm->builtins, vm->internal_object_key, &vm->types[TINYPY_VALUE_INSTANCE].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_type_key, &vm->types[TINYPY_VALUE_TYPE].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_bool_key, &vm->types[TINYPY_VALUE_BOOL].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_int_key, &vm->types[TINYPY_VALUE_INTEGER].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_long_key, &vm->types[TINYPY_VALUE_LONG].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_float_key, &vm->types[TINYPY_VALUE_FLOAT].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_complex_key, &vm->types[TINYPY_VALUE_COMPLEX].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_str_key, &vm->types[TINYPY_VALUE_STRING].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_bytes_key, &vm->types[TINYPY_VALUE_STRING].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_basestring_key, &vm->types[TINYPY_VALUE_INVALID].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_unicode_key, &vm->types[TINYPY_VALUE_UNICODE].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_tuple_key, &vm->types[TINYPY_VALUE_TUPLE].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_list_key, &vm->types[TINYPY_VALUE_LIST].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_dict_key, &vm->types[TINYPY_VALUE_DICT].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_set_key, &vm->types[TINYPY_VALUE_SET].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_frozenset_key, &vm->types[TINYPY_VALUE_FROZENSET].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_slice_key, &vm->types[TINYPY_VALUE_SLICE].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_staticmethod_key, &vm->types[TINYPY_VALUE_STATIC_METHOD].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_classmethod_key, &vm->types[TINYPY_VALUE_CLASS_METHOD].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_property_key, &vm->types[TINYPY_VALUE_PROPERTY].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_super_key, &vm->types[TINYPY_VALUE_SUPER].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_buffer_key, &vm->types[TINYPY_VALUE_BUFFER].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_memoryview_key, &vm->memoryview_type->base.base);
    tinypy_dict_set(vm->builtins, vm->internal_bytearray_key, &vm->types[TINYPY_VALUE_BYTEARRAY].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_file_key, &vm->types[TINYPY_VALUE_FILE].base.base);
    TINYPY_DECREF(false_value);
    TINYPY_DECREF(true_value);
    TINYPY_DECREF(none_value);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_register_module(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_value_t *module) {
    tinypy_dict_set(vm->modules, name, module);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_sys_arguments(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_arity_style_e style, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *name = TINYPY_NATIVE_FUNCTION_OBJECT(function)->name;
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_TEXT(name),
            TINYPY_MESSAGE_PART_LITERAL("() takes no keyword arguments")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
        return TINYPY_FALSE;
    }
    if (count < minimum || count > maximum) {
        tinypy_internal_make_arity_error(vm, (const char *)TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name), count, minimum, maximum, style, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_sys_integer(tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    if (tinypy_internal_integer_as_ssize(value, out_value, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (*out_value < INT32_MIN || *out_value > INT32_MAX) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_OVERFLOW, *out_value > INT32_MAX ? "signed integer is greater than maximum" : "signed integer is less than minimum", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_exc_info(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *none_values[3] = {NULL, NULL, NULL};
    tinypy_value_t *items[3];
    size_t index;

    (void)user_data;
    if (__tinypy_internal_sys_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    if (vm->handled_type != NULL) {
        items[0] = vm->handled_type;
        items[1] = vm->handled_value;
        items[2] = vm->handled_traceback;
        tinypy_value_t *return_value_1 = tinypy_tuple_from_items(vm, items, 3U);
        return return_value_1;
    }
    for (index = 0U; index < 3U; ++index) {
        none_values[index] = TINYPY_RET_NONE(vm);
    }
    tinypy_value_t *result = tinypy_tuple_from_items(vm, none_values, 3U);
    for (index = 0U; index < 3U; ++index) {
        TINYPY_DECREF(none_values[index]);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Python 2.7 mirrors the exception being handled into sys.exc_type,
   sys.exc_value and sys.exc_traceback for backwards compatibility. */
void tinypy_internal_sys_publish_handled_exception(tinypy_vm_t *vm) {
    tinypy_value_t *sys_dict;
    tinypy_value_t *type_value;
    tinypy_value_t *value_value;
    tinypy_value_t *traceback_value;

    if (vm->state != TINYPY_VM_STATE_LIVE || vm->sys_module == NULL) {
        return;
    }
    sys_dict = TINYPY_MODULE_OBJECT(vm->sys_module)->dict;
    type_value = vm->handled_type != NULL ? vm->handled_type : &vm->none_object.base;
    value_value = vm->handled_value != NULL ? vm->handled_value : &vm->none_object.base;
    traceback_value = vm->handled_traceback != NULL ? vm->handled_traceback : &vm->none_object.base;
    tinypy_dict_set(sys_dict, vm->internal_exc_type_key, type_value);
    tinypy_dict_set(sys_dict, vm->internal_exc_value_key, value_value);
    tinypy_dict_set(sys_dict, vm->internal_exc_traceback_key, traceback_value);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_exc_clear(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_internal_sys_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    vm->handled_clear_epoch += UINT64_C(1);
    tinypy_internal_exception_clear_handled(vm);
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_getframe(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_frame_object_t *frame = vm->current_frame;
    int64_t depth = INT64_C(0);

    (void)user_data;
    if (__tinypy_internal_sys_arguments(function, args, kwargs, 0U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) != 0U) {
        tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);

        if (__tinypy_internal_sys_integer(value, &depth, out_error) == 0) {
            return NULL;
        }
    }
    while (depth > 0 && frame != NULL) {
        frame = frame->back != NULL ? TINYPY_FRAME_OBJECT(frame->back) : NULL;
        depth -= 1;
    }
    if (frame == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "call stack is not deep enough", out_error);
        return NULL;
    }
    return TINYPY_RET(&frame->base.base);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_getrecursionlimit(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_internal_sys_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)vm->recursion_limit);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_setrecursionlimit(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *value;
    int64_t limit;

    (void)user_data;
    if (__tinypy_internal_sys_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    value = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_internal_sys_integer(value, &limit, out_error) == 0) {
        return NULL;
    }
    if (limit <= 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "recursion limit must be positive", out_error);
        return NULL;
    }
    vm->recursion_limit = (size_t)limit;
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_getdefaultencoding(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_internal_sys_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    result = TINYPY_RET(vm->internal_codec_ascii_name);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_exit(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *exception;

    (void)user_data;
    if (__tinypy_internal_sys_arguments(function, args, kwargs, 0U, 1U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    exception = tinypy_exception_new(vm->exception_types[TINYPY_EXCEPTION_SYSTEM_EXIT], args, out_error);
    if (exception == NULL) {
        return NULL;
    }
    (void)tinypy_exception_raise(exception, NULL, out_error);
    TINYPY_DECREF(exception);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_sys_flush_line(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_value_t *stream = tinypy_internal_dict_get_optional_suppressed(vm, TINYPY_MODULE_OBJECT(vm->sys_module)->dict, vm->internal_stdout_key);

    if (stream == NULL) {
        return TINYPY_TRUE;
    }
    TINYPY_INCREF(stream);
    tinypy_bool_t success = TINYPY_TRUE;
    if (tinypy_internal_output_soft_space(stream, TINYPY_FALSE) != TINYPY_FALSE) {
        success = tinypy_internal_output_write(stream, "\n", 1U, out_error);
    }
    TINYPY_DECREF(stream);
    return success;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_displayhook(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *builtins_module = NULL;
    tinypy_value_t *stream = NULL;
    tinypy_value_t *writer = NULL;
    tinypy_value_t *representation = NULL;
    tinypy_value_t *result = NULL;

    (void)user_data;
    if (__tinypy_internal_sys_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    builtins_module = tinypy_internal_dict_get_optional_suppressed(vm, vm->modules, vm->internal_builtin_module_name);
    if (builtins_module == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "lost __builtin__", out_error);
        return NULL;
    }
    TINYPY_INCREF(builtins_module);
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE) {
        result = TINYPY_RET_NONE(vm);
        goto cleanup;
    }
    if (tinypy_object_set_attr_value(builtins_module, vm->internal_underscore_key, &vm->none_object.base, out_error) == TINYPY_FALSE || __tinypy_internal_sys_flush_line(vm, out_error) == TINYPY_FALSE) {
        goto cleanup;
    }
    stream = tinypy_internal_dict_get_optional_suppressed(vm, TINYPY_MODULE_OBJECT(vm->sys_module)->dict, vm->internal_stdout_key);
    if (stream == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "lost sys.stdout", out_error);
        goto cleanup;
    }
    TINYPY_INCREF(stream);
    if (TINYPY_VALUE_KIND(stream) != TINYPY_VALUE_OUTPUT_STREAM) {
        writer = tinypy_object_get_attr_value(stream, vm->internal_write_key, out_error);
        if (writer == NULL) {
            goto cleanup;
        }
    }
    representation = tinypy_object_repr(value, out_error);
    if (representation == NULL) {
        goto cleanup;
    }
    if (writer != NULL) {
        tinypy_value_t *write_args = tinypy_tuple_from_items(vm, &representation, 1U);
        tinypy_value_t *written = tinypy_call(writer, write_args, NULL, out_error);

        TINYPY_DECREF(write_args);
        if (written == NULL) {
            goto cleanup;
        }
        TINYPY_DECREF(written);
    }
    else if (tinypy_internal_output_write_value(stream, representation, out_error) == TINYPY_FALSE) {
        goto cleanup;
    }
    (void)tinypy_internal_output_soft_space(stream, TINYPY_TRUE);
    if (__tinypy_internal_sys_flush_line(vm, out_error) == TINYPY_FALSE || tinypy_object_set_attr_value(builtins_module, vm->internal_underscore_key, value, out_error) == TINYPY_FALSE) {
        goto cleanup;
    }
    result = TINYPY_RET_NONE(vm);

cleanup:
    if (representation != NULL) {
        TINYPY_DECREF(representation);
    }
    if (writer != NULL) {
        TINYPY_DECREF(writer);
    }
    if (stream != NULL) {
        TINYPY_DECREF(stream);
    }
    TINYPY_DECREF(builtins_module);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_future_feature_init(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_internal_sys_arguments(function, args, kwargs, 4U, 4U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_instance_set_attr_key(self, vm->internal_optional_key, TINYPY_TUPLE_GET(args, 1U));
    tinypy_instance_set_attr_key(self, vm->internal_mandatory_key, TINYPY_TUPLE_GET(args, 2U));
    tinypy_instance_set_attr_key(self, vm->internal_compiler_flag_key, TINYPY_TUPLE_GET(args, 3U));
    result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_future_feature_release(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *name = (intptr_t)user_data == 0 ? vm->internal_optional_key : vm->internal_mandatory_key;

    if (__tinypy_internal_sys_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = tinypy_object_get_attr_value(TINYPY_TUPLE_GET(args, 0U), name, out_error);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_future_feature_repr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *optional = NULL;
    tinypy_value_t *mandatory = NULL;
    tinypy_value_t *flag = NULL;
    tinypy_value_t *optional_repr = NULL;
    tinypy_value_t *mandatory_repr = NULL;
    tinypy_value_t *flag_repr = NULL;
    tinypy_value_t *result = NULL;

    (void)user_data;
    if (__tinypy_internal_sys_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    optional = tinypy_object_get_attr_value(self, vm->internal_optional_key, out_error);
    mandatory = optional != NULL ? tinypy_object_get_attr_value(self, vm->internal_mandatory_key, out_error) : NULL;
    flag = mandatory != NULL ? tinypy_object_get_attr_value(self, vm->internal_compiler_flag_key, out_error) : NULL;
    if (flag == NULL) {
        goto cleanup;
    }
    optional_repr = tinypy_object_repr(optional, out_error);
    mandatory_repr = optional_repr != NULL ? tinypy_object_repr(mandatory, out_error) : NULL;
    flag_repr = mandatory_repr != NULL ? tinypy_object_repr(flag, out_error) : NULL;
    if (flag_repr != NULL) {
        size_t optional_size = TINYPY_TEXT_BYTE_SIZE(optional_repr);
        size_t mandatory_size = TINYPY_TEXT_BYTE_SIZE(mandatory_repr);
        size_t flag_size = TINYPY_TEXT_BYTE_SIZE(flag_repr);
        size_t result_size = optional_size + mandatory_size + flag_size + 14U;
        uint8_t *output;

        result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, result_size, result_size, &output, out_error);
        if (result != NULL) {
            size_t position = 0U;

            (void)memcpy(output + position, "_Feature(", 9U);
            position += 9U;
            (void)memcpy(output + position, TINYPY_TEXT_BYTES(optional_repr), optional_size);
            position += optional_size;
            (void)memcpy(output + position, ", ", 2U);
            position += 2U;
            (void)memcpy(output + position, TINYPY_TEXT_BYTES(mandatory_repr), mandatory_size);
            position += mandatory_size;
            (void)memcpy(output + position, ", ", 2U);
            position += 2U;
            (void)memcpy(output + position, TINYPY_TEXT_BYTES(flag_repr), flag_size);
            position += flag_size;
            output[position] = (uint8_t)')';
        }
    }

cleanup:
    if (flag_repr != NULL) {
        TINYPY_DECREF(flag_repr);
    }
    if (mandatory_repr != NULL) {
        TINYPY_DECREF(mandatory_repr);
    }
    if (optional_repr != NULL) {
        TINYPY_DECREF(optional_repr);
    }
    if (flag != NULL) {
        TINYPY_DECREF(flag);
    }
    if (mandatory != NULL) {
        TINYPY_DECREF(mandatory);
    }
    if (optional != NULL) {
        TINYPY_DECREF(optional);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_future_release_tuple(tinypy_vm_t *vm, int64_t major, int64_t minor, int64_t micro, const char *level, size_t level_size, int64_t serial) {
    tinypy_value_t *values[5];
    tinypy_value_t *result;
    size_t index;

    values[0] = tinypy_integer_from_i64(vm, major);
    values[1] = tinypy_integer_from_i64(vm, minor);
    values[2] = tinypy_integer_from_i64(vm, micro);
    values[3] = tinypy_string_from_bytes(vm, level, level_size);
    values[4] = tinypy_integer_from_i64(vm, serial);
    result = tinypy_tuple_from_items(vm, values, 5U);
    for (index = 0U; index != 5U; ++index) {
        TINYPY_DECREF(values[index]);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_future_add_feature(tinypy_vm_t *vm, tinypy_value_t *module, tinypy_type_t *feature_type, tinypy_value_t *names, tinypy_value_t *name, tinypy_value_t *optional, tinypy_value_t *mandatory, int64_t flag) {
    tinypy_value_t *instance = tinypy_instance_new(feature_type);
    tinypy_value_t *flag_value = tinypy_integer_from_i64(vm, flag);

    tinypy_instance_set_attr_key(instance, vm->internal_optional_key, optional);
    tinypy_instance_set_attr_key(instance, vm->internal_mandatory_key, mandatory);
    tinypy_instance_set_attr_key(instance, vm->internal_compiler_flag_key, flag_value);
    tinypy_module_add_value_key(module, name, instance);
    (void)tinypy_internal_list_append_checked(names, name, NULL);
    TINYPY_DECREF(flag_value);
    TINYPY_DECREF(instance);
}
//////////////////////////////////////////////////////////////////////////
/* sys exposes several tuples whose members are also reachable by name, the
   way CPython struct sequences are. */
static tinypy_value_t *__tinypy_internal_sys_named_tuple(tinypy_value_t *type_name, tinypy_value_t *const *field_names, tinypy_value_t *const *values, size_t count) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(type_name);
    const tinypy_type_t *bases[1] = {&vm->types[TINYPY_VALUE_TUPLE]};
    tinypy_type_t *type = tinypy_type_new_key(type_name, bases, 1U, NULL, NULL, NULL);
    tinypy_value_t *members = tinypy_tuple_from_items(vm, values, count);
    tinypy_value_t *arguments = tinypy_tuple_from_items(vm, &members, 1U);
    tinypy_value_t *instance = tinypy_call(&type->base.base, arguments, NULL, NULL);
    size_t index;

    TINYPY_DECREF(arguments);
    TINYPY_DECREF(members);
    TINYPY_DECREF(&type->base.base);
    if (instance == NULL) {
        return NULL;
    }
    for (index = 0U; index < count; ++index) {
        (void)tinypy_object_set_attr_value(instance, field_names[index], values[index], NULL);
    }
    return instance;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_float_info(tinypy_vm_t *vm) {
    tinypy_value_t *const field_names[] = {vm->internal_max_key, vm->internal_max_exp_key, vm->internal_max_10_exp_key, vm->internal_min_key, vm->internal_min_exp_key, vm->internal_min_10_exp_key, vm->internal_dig_key, vm->internal_mant_dig_key, vm->internal_epsilon_key, vm->internal_radix_key, vm->internal_rounds_key};
    tinypy_value_t *values[11];
    tinypy_value_t *result;
    size_t index;

    values[0] = tinypy_float_from_double(vm, DBL_MAX);
    values[1] = tinypy_integer_from_i64(vm, (int64_t)DBL_MAX_EXP);
    values[2] = tinypy_integer_from_i64(vm, (int64_t)DBL_MAX_10_EXP);
    values[3] = tinypy_float_from_double(vm, DBL_MIN);
    values[4] = tinypy_integer_from_i64(vm, (int64_t)DBL_MIN_EXP);
    values[5] = tinypy_integer_from_i64(vm, (int64_t)DBL_MIN_10_EXP);
    values[6] = tinypy_integer_from_i64(vm, (int64_t)DBL_DIG);
    values[7] = tinypy_integer_from_i64(vm, (int64_t)DBL_MANT_DIG);
    values[8] = tinypy_float_from_double(vm, DBL_EPSILON);
    values[9] = tinypy_integer_from_i64(vm, (int64_t)FLT_RADIX);
    values[10] = tinypy_integer_from_i64(vm, (int64_t)FLT_ROUNDS);
    result = __tinypy_internal_sys_named_tuple(vm->internal_sys_dot_floatinfo_key, field_names, values, 11U);
    for (index = 0U; index < 11U; ++index) {
        TINYPY_DECREF(values[index]);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_getsizeof(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *const names[] = {vm->internal_object_key, vm->internal_default_key};
    tinypy_value_t *values[2] = {NULL, NULL};

    (void)user_data;
    tinypy_bool_t parsed = tinypy_internal_constructor_optional_arguments(vm, "getsizeof", 9U, args, kwargs, names, 2U, 0U, values, out_error);
    size_t count = TINYPY_TUPLE_SIZE(args) + (kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U);
    if (values[0] == NULL && count <= 2U) {
        (void)tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_TYPE_ERROR, out_error);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "Required argument 'object' (pos 1) not found", out_error);
        return NULL;
    }
    if (parsed == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *object = values[0];
    if (TINYPY_VALUE_KIND(object) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)tinypy_internal_value_allocation_size(object));

        return result;
    }
    tinypy_value_t *method = tinypy_internal_object_get_special_key(object, vm->internal_special_sizeof_key, out_error);
    tinypy_value_t *reported = NULL;
    tinypy_value_t *result = NULL;
    int64_t size;

    if (method == NULL) {
        if (vm->raised_type == NULL) {
            size_t name_size = object->type->name_size < 100U ? object->type->name_size : 100U;
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("Type "),
                {object->type->name, name_size},
                TINYPY_MESSAGE_PART_LITERAL(" doesn't define __sizeof__")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
        }
        goto fallback;
    }
    tinypy_value_t *empty = TINYPY_RET_EMPTY_TUPLE(vm);
    reported = tinypy_call(method, empty, NULL, out_error);
    TINYPY_DECREF(empty);
    TINYPY_DECREF(method);
    if (reported == NULL || tinypy_internal_number_as_ssize(reported, &size, out_error) == TINYPY_FALSE) {
        goto fallback;
    }
    if (size < 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__sizeof__() should return >= 0", out_error);
        goto fallback;
    }
    result = tinypy_integer_from_i64(vm, size);

fallback:
    if (reported != NULL) {
        TINYPY_DECREF(reported);
    }
    if (result == NULL && values[1] != NULL && tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_TYPE_ERROR, out_error) != TINYPY_FALSE) {
        result = TINYPY_RET(values[1]);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_initialize_future_module(tinypy_vm_t *vm) {
    tinypy_value_t *const constant_names[] = {vm->internal_co_nested_key, vm->internal_co_generator_allowed_key, vm->internal_co_future_division_key, vm->internal_co_future_absolute_import_key, vm->internal_co_future_with_statement_key, vm->internal_co_future_print_function_key, vm->internal_co_future_unicode_literals_key};
    static const int64_t constant_values[] = {16, 0, 8192, 16384, 32768, 65536, 131072};
    tinypy_value_t *module = tinypy_module_new_key(vm->internal_future_module_name);
    tinypy_value_t *name = TINYPY_RET(vm->internal_future_module_name);
    tinypy_type_t *feature_type = tinypy_type_new_key(vm->internal_feature_key, NULL, 0U, NULL, NULL, NULL);
    tinypy_value_t *module_name = TINYPY_RET(vm->internal_future_module_name);
    tinypy_value_t *feature_names = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *optional;
    tinypy_value_t *mandatory;
    size_t index;

    tinypy_module_add_value_key(module, vm->internal_special_name_key, name);
    tinypy_type_set_attr_key(feature_type, feature_type->vm->internal_special_module_key, module_name);
    tinypy_internal_type_add_method(feature_type, vm->internal_special_init_key, __tinypy_future_feature_init, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(feature_type, vm->internal_get_optional_release_key, __tinypy_future_feature_release, (void *)(intptr_t)0, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(feature_type, vm->internal_get_mandatory_release_key, __tinypy_future_feature_release, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(feature_type, vm->internal_special_repr_key, __tinypy_future_feature_repr, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_module_add_value_key(module, vm->internal_feature_key, &feature_type->base.base);
    for (index = 0U; index != sizeof(constant_values) / sizeof(constant_values[0]); ++index) {
        tinypy_value_t *value = tinypy_integer_from_i64(vm, constant_values[index]);

        tinypy_module_add_value_key(module, constant_names[index], value);
        TINYPY_DECREF(value);
    }

#define TINYPY_FUTURE_FEATURE(feature_key, optional_major, optional_minor, optional_level, optional_serial, mandatory_major, mandatory_minor, mandatory_level, flag_value) \
    do {                                                                                                                                                                                   \
        optional = __tinypy_future_release_tuple(vm, optional_major, optional_minor, 0, optional_level, sizeof(optional_level) - 1U, optional_serial);                                      \
        mandatory = __tinypy_future_release_tuple(vm, mandatory_major, mandatory_minor, 0, mandatory_level, sizeof(mandatory_level) - 1U, 0);                                               \
        __tinypy_future_add_feature(vm, module, feature_type, feature_names, feature_key, optional, mandatory, flag_value);                                     \
        TINYPY_DECREF(mandatory);                                                                                                                                                           \
        TINYPY_DECREF(optional);                                                                                                                                                            \
    } while (0)

    TINYPY_FUTURE_FEATURE(vm->internal_future_nested_scopes_key, 2, 1, "beta", 1, 2, 2, "alpha", 16);
    TINYPY_FUTURE_FEATURE(vm->internal_future_generators_key, 2, 2, "alpha", 1, 2, 3, "final", 0);
    TINYPY_FUTURE_FEATURE(vm->internal_future_division_key, 2, 2, "alpha", 2, 3, 0, "alpha", 8192);
    TINYPY_FUTURE_FEATURE(vm->internal_future_absolute_import_key, 2, 5, "alpha", 1, 3, 0, "alpha", 16384);
    TINYPY_FUTURE_FEATURE(vm->internal_future_with_statement_key, 2, 5, "alpha", 1, 2, 6, "alpha", 32768);
    TINYPY_FUTURE_FEATURE(vm->internal_future_print_function_key, 2, 6, "alpha", 2, 3, 0, "alpha", 65536);
    TINYPY_FUTURE_FEATURE(vm->internal_future_unicode_literals_key, 2, 6, "alpha", 2, 3, 0, "alpha", 131072);
#undef TINYPY_FUTURE_FEATURE

    tinypy_module_add_value_key(module, vm->internal_all_feature_names_key, feature_names);
    TINYPY_DECREF(feature_names);
    TINYPY_DECREF(module_name);
    TINYPY_DECREF(&feature_type->base.base);
    TINYPY_DECREF(name);
    return module;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_modules(tinypy_vm_t *vm) {
    tinypy_value_t *name;
    tinypy_value_t *stdout_value;
    tinypy_value_t *stderr_value;
    tinypy_value_t *future_module;
    uint16_t byteorder_probe = UINT16_C(1);

    vm->modules = tinypy_dict_new(vm);
    tinypy_value_t *builtin_module = tinypy_internal_module_from_dict_key(vm->internal_builtin_module_name, vm->builtins);
    name = TINYPY_RET(vm->internal_builtin_module_name);
    tinypy_module_add_value_key(builtin_module, vm->internal_special_name_key, name);
    TINYPY_DECREF(name);
    tinypy_internal_register_module(vm, vm->internal_builtin_module_name, builtin_module);

    tinypy_value_t *sys_module = tinypy_module_new_key(vm->internal_sys_key);
    name = TINYPY_RET(vm->internal_sys_key);
    tinypy_module_add_value_key(sys_module, vm->internal_special_name_key, name);
    TINYPY_DECREF(name);
    tinypy_module_add_value_key(sys_module, vm->internal_modules_key, vm->modules);
    stdout_value = tinypy_internal_output_stream_new(vm, TINYPY_OUTPUT_STDOUT);
    stderr_value = tinypy_internal_output_stream_new(vm, TINYPY_OUTPUT_STDERR);
    tinypy_module_add_value_key(sys_module, vm->internal_stdout_key, stdout_value);
    tinypy_module_add_value_key(sys_module, vm->internal_special_stdout_key, stdout_value);
    tinypy_module_add_value_key(sys_module, vm->internal_stderr_key, stderr_value);
    tinypy_module_add_value_key(sys_module, vm->internal_special_stderr_key, stderr_value);
    name = tinypy_string_from_bytes(vm, *((const uint8_t *)&byteorder_probe) == 1U ? "little" : "big", *((const uint8_t *)&byteorder_probe) == 1U ? 6U : 3U);
    tinypy_module_add_value_key(sys_module, vm->internal_byteorder_key, name);
    TINYPY_DECREF(name);
    name = tinypy_integer_from_i64(vm, INT64_C(0x020712f0));
    tinypy_module_add_value_key(sys_module, vm->internal_hexversion_key, name);
    TINYPY_DECREF(name);
    name = tinypy_integer_from_i64(vm, INT64_MAX);
    tinypy_module_add_value_key(sys_module, vm->internal_maxint_key, name);
    tinypy_module_add_value_key(sys_module, vm->internal_maxsize_key, name);
    TINYPY_DECREF(name);
    name = tinypy_integer_from_i64(vm, INT64_C(0x10ffff));
    tinypy_module_add_value_key(sys_module, vm->internal_maxunicode_key, name);
    TINYPY_DECREF(name);
    tinypy_module_add_value_key(sys_module, vm->internal_py3kwarning_key, &vm->false_object.base);
    tinypy_module_add_value_key(sys_module, vm->internal_dont_write_bytecode_key, &vm->false_object.base);
    name = tinypy_string_from_bytes(vm, "2.7.18 (tinypy)", 15U);
    tinypy_module_add_value_key(sys_module, vm->internal_version_key, name);
    TINYPY_DECREF(name);
    tinypy_value_t *version_items[5];
    version_items[0] = tinypy_integer_from_i64(vm, INT64_C(2));
    version_items[1] = tinypy_integer_from_i64(vm, INT64_C(7));
    version_items[2] = tinypy_integer_from_i64(vm, INT64_C(18));
    version_items[3] = tinypy_string_from_bytes(vm, "final", 5U);
    version_items[4] = tinypy_integer_from_i64(vm, INT64_C(0));
    tinypy_value_t *const version_fields[] = {vm->internal_major_key, vm->internal_minor_key, vm->internal_micro_key, vm->internal_releaselevel_key, vm->internal_serial_key};
    tinypy_value_t *version_info = __tinypy_internal_sys_named_tuple(vm->internal_sys_dot_version_info_key, version_fields, version_items, 5U);
    for (size_t version_index = 0U; version_index != 5U; ++version_index) {
        TINYPY_DECREF(version_items[version_index]);
    }
    tinypy_module_add_value_key(sys_module, vm->internal_version_info_key, version_info);
    TINYPY_DECREF(version_info);
    tinypy_value_t *float_info = __tinypy_internal_sys_float_info(vm);

    tinypy_module_add_value_key(sys_module, vm->internal_float_info_key, float_info);
    TINYPY_DECREF(float_info);
    tinypy_module_add_value_key(sys_module, vm->internal_exc_type_key, &vm->none_object.base);
    tinypy_module_add_value_key(sys_module, vm->internal_exc_value_key, &vm->none_object.base);
    tinypy_module_add_value_key(sys_module, vm->internal_exc_traceback_key, &vm->none_object.base);
    name = tinypy_integer_from_i64(vm, INT64_C(1013));
    tinypy_module_add_value_key(sys_module, vm->internal_api_version_key, name);
    TINYPY_DECREF(name);
#if defined(_WIN32)
    name = tinypy_string_from_bytes(vm, "win32", 5U);
#elif defined(__APPLE__)
    name = tinypy_string_from_bytes(vm, "darwin", 6U);
#elif defined(__linux__)
    name = tinypy_string_from_bytes(vm, "linux2", 6U);
#else
    name = tinypy_string_from_bytes(vm, "unknown", 7U);
#endif
    tinypy_module_add_value_key(sys_module, vm->internal_platform_key, name);
    TINYPY_DECREF(name);
    name = tinypy_string_from_bytes(vm, "short", 5U);
    tinypy_module_add_value_key(sys_module, vm->internal_float_repr_style_key, name);
    TINYPY_DECREF(name);
    tinypy_value_t *warn_options = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *meta_path = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *path_hooks = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *path_importer_cache = tinypy_dict_new(vm);
    tinypy_module_add_value_key(sys_module, vm->internal_warnoptions_key, warn_options);
    tinypy_module_add_value_key(sys_module, vm->internal_meta_path_key, meta_path);
    tinypy_module_add_value_key(sys_module, vm->internal_path_hooks_key, path_hooks);
    tinypy_module_add_value_key(sys_module, vm->internal_path_importer_cache_key, path_importer_cache);
    TINYPY_DECREF(path_importer_cache);
    TINYPY_DECREF(path_hooks);
    TINYPY_DECREF(meta_path);
    TINYPY_DECREF(warn_options);
    tinypy_value_t *const builtin_module_name_values[] = {vm->internal_builtin_module_name, vm->internal_future_module_name, vm->internal_codec_module_name, vm->internal_partial_module_name, vm->internal_sre_module_name, vm->internal_struct_module_name, vm->internal_weakref_module_name, vm->internal_copy_reg_module_name, vm->internal_exception_module_name, vm->internal_sys_key};
    tinypy_value_t *builtin_module_names = tinypy_tuple_from_items(vm, builtin_module_name_values, 10U);
    tinypy_module_add_value_key(sys_module, vm->internal_builtin_module_names_key, builtin_module_names);
    TINYPY_DECREF(builtin_module_names);
    TINYPY_DECREF(stderr_value);
    TINYPY_DECREF(stdout_value);
    tinypy_internal_module_add_function(sys_module, vm->internal_exc_info_key, __tinypy_internal_sys_exc_info, NULL, NULL);
    tinypy_internal_module_add_function(sys_module, vm->internal_exc_clear_key, __tinypy_internal_sys_exc_clear, NULL, NULL);
    tinypy_internal_module_add_function(sys_module, vm->internal_getframe_key, __tinypy_internal_sys_getframe, NULL, NULL);
    tinypy_internal_module_add_function(sys_module, vm->internal_getrecursionlimit_key, __tinypy_internal_sys_getrecursionlimit, NULL, NULL);
    tinypy_internal_module_add_function(sys_module, vm->internal_getsizeof_key, __tinypy_internal_sys_getsizeof, NULL, NULL);
    tinypy_internal_module_add_function(sys_module, vm->internal_setrecursionlimit_key, __tinypy_internal_sys_setrecursionlimit, NULL, NULL);
    tinypy_internal_module_add_function(sys_module, vm->internal_getdefaultencoding_key, __tinypy_internal_sys_getdefaultencoding, NULL, NULL);
    tinypy_internal_module_add_function(sys_module, vm->internal_exit_key, __tinypy_internal_sys_exit, NULL, NULL);
    tinypy_value_t *displayhook = tinypy_native_function_new_key(vm->internal_displayhook_key, __tinypy_internal_sys_displayhook, NULL, NULL);
    tinypy_module_add_value_key(sys_module, vm->internal_displayhook_key, displayhook);
    tinypy_module_add_value_key(sys_module, vm->internal_special_displayhook_key, displayhook);
    TINYPY_DECREF(displayhook);
    tinypy_internal_register_module(vm, vm->internal_sys_key, sys_module);
    future_module = __tinypy_internal_initialize_future_module(vm);
    tinypy_internal_register_module(vm, vm->internal_future_module_name, future_module);
    tinypy_internal_initialize_weakref_module(vm);
    tinypy_internal_initialize_codecs_module(vm);
    tinypy_internal_initialize_functools_module(vm);
    tinypy_internal_initialize_struct_module(vm);
    tinypy_internal_initialize_copy_reg_module(vm);
    tinypy_internal_initialize_sre_module(vm);
    tinypy_internal_initialize_exceptions_module(vm);
    vm->sys_module = sys_module;
    TINYPY_DECREF(future_module);
    TINYPY_DECREF(builtin_module);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_none(tinypy_none_object_t *value, tinypy_type_t *type) {
    (void)memset(value, 0, sizeof(*value));
    value->base.ref = 1U;
    value->base.type = type;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_integer(tinypy_integer_object_t *value, tinypy_type_t *type, int64_t integer_value) {
    (void)memset(value, 0, sizeof(*value));
    value->base.ref = 1U;
    value->base.type = type;
    TINYPY_INTEGER_VALUE(value) = integer_value;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_float(tinypy_float_object_t *value, tinypy_type_t *type, double float_value) {
    (void)memset(value, 0, sizeof(*value));
    value->base.ref = 1U;
    value->base.type = type;
    value->value = float_value;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_empty_string(tinypy_string_object_t *value, tinypy_type_t *type) {
    (void)memset(value, 0, sizeof(*value));
    value->base.base.ref = 1U;
    value->base.base.type = type;
    value->base.size = 0;
    value->hash = 0;
    value->interned = 1;
    value->hash_computed = 1;
    value->bytes[0] = 0U;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_empty_tuple(tinypy_tuple_object_t *value, tinypy_type_t *type) {
    (void)memset(value, 0, sizeof(*value));
    value->base.base.ref = 1U;
    value->base.base.type = type;
    value->base.size = 0;
    value->items[0] = NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_host_valid(const tinypy_host_t *host) {
    if (host == NULL) {
        return TINYPY_TRUE;
    }

    return host->abi_version == TINYPY_ABI_VERSION && host->struct_size >= (uint32_t)sizeof(*host) && (host->resolve_module == NULL || host->release_module_artifact != NULL);
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_vm_valid(const tinypy_vm_t *vm) {
    return vm != NULL && vm->state == TINYPY_VM_STATE_LIVE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_recursion_check(tinypy_vm_t *vm, uintptr_t stack_address, const char *message, tinypy_error_t **out_error) {
    size_t stack_usage = 0U;

    if (vm->evaluation_depth == 0U) {
        vm->native_stack_origin = stack_address;
    }
    else if (stack_address != 0U && vm->native_stack_origin != 0U) {
        stack_usage = (size_t)(stack_address > vm->native_stack_origin
            ? stack_address - vm->native_stack_origin
            : vm->native_stack_origin - stack_address);
    }
    if (vm->evaluation_depth >= vm->recursion_limit || stack_usage >= vm->max_stack_bytes) {
        tinypy_bool_t previous = vm->recursion_error;

        vm->recursion_error = TINYPY_TRUE;
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, message, out_error);
        vm->recursion_error = previous;
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void *tinypy_internal_vm_allocate(tinypy_vm_t *vm, size_t size) {
    void *return_value_1 = tinypy_internal_pool_allocate(vm, size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void *tinypy_internal_vm_allocate_checked(tinypy_vm_t *vm, size_t size, tinypy_error_t **out_error) {
    void *memory = tinypy_internal_pool_allocate_checked(vm, size);

    if (memory == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "memory allocation failed", out_error);
    }
    return memory;
}
//////////////////////////////////////////////////////////////////////////
void *tinypy_internal_vm_reallocate(tinypy_vm_t *vm, void *memory, size_t old_size, size_t new_size) {
    void *return_value_1 = tinypy_internal_pool_reallocate(vm, memory, old_size, new_size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void *tinypy_internal_vm_reallocate_checked(tinypy_vm_t *vm, void *memory, size_t old_size, size_t new_size, tinypy_error_t **out_error) {
    void *resized = tinypy_internal_pool_reallocate_checked(vm, memory, old_size, new_size);

    if (resized == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "memory allocation failed", out_error);
    }
    return resized;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_vm_deallocate(tinypy_vm_t *vm, void *memory, size_t size) {
    tinypy_internal_pool_deallocate(vm, memory, size);
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
tinypy_vm_t *tinypy_vm_create(const tinypy_vm_config_t *config) {

    const tinypy_allocator_t *allocator = config->allocator;

    tinypy_vm_t *vm = (tinypy_vm_t *)allocator->allocate(
        allocator->user_data,
        sizeof(*vm),
        TINYPY_INTERNAL_ALIGNMENT);

    (void)memset(vm, 0, sizeof(*vm));
    vm->state = TINYPY_VM_STATE_LIVE;
    vm->allocator = *allocator;
    vm->max_heap_bytes =
        config->struct_size >= (uint32_t)(offsetof(tinypy_vm_config_t, max_heap_bytes) + sizeof(config->max_heap_bytes))
            ? config->max_heap_bytes
            : 0U;
    vm->allocated_bytes = sizeof(*vm);
    vm->type_lookup_cache_epoch = UINT64_C(1);
    vm->recursion_limit = 1000U;
    vm->max_stack_bytes =
        config->struct_size >= (uint32_t)(offsetof(tinypy_vm_config_t, max_stack_bytes) + sizeof(config->max_stack_bytes))
            && config->max_stack_bytes != 0U
            ? config->max_stack_bytes
            : 1024U * 1024U;
    vm->optimize_level =
        config->struct_size >= (uint32_t)(offsetof(tinypy_vm_config_t, optimize_level) + sizeof(config->optimize_level))
            ? config->optimize_level
            : 0;
    tinypy_internal_pool_initialize(vm);

    if (config->host != NULL) {
        size_t host_size = config->host->struct_size < (uint32_t)sizeof(vm->host) ? (size_t)config->host->struct_size : sizeof(vm->host);

        (void)memcpy(&vm->host, config->host, host_size);
        vm->has_host = 1;
    }
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    if (config->struct_size >= (uint32_t)(offsetof(tinypy_vm_config_t, cycle_diagnostics) + sizeof(config->cycle_diagnostics)) &&
        config->cycle_diagnostics != 0) {
        tinypy_internal_cycle_diagnostics_initialize(vm);
    }
#endif

    __tinypy_internal_initialize_types(vm);
    __tinypy_internal_initialize_none(
        &vm->none_object,
        &vm->types[TINYPY_VALUE_NONE]);
    __tinypy_internal_initialize_none(
        &vm->not_implemented_object,
        &vm->types[TINYPY_VALUE_NOT_IMPLEMENTED]);
    __tinypy_internal_initialize_none(
        &vm->ellipsis_object,
        &vm->types[TINYPY_VALUE_ELLIPSIS]);
    __tinypy_internal_initialize_integer(
        &vm->false_object,
        &vm->types[TINYPY_VALUE_BOOL],
        INT64_C(0));
    __tinypy_internal_initialize_integer(
        &vm->true_object,
        &vm->types[TINYPY_VALUE_BOOL],
        INT64_C(1));
    for (size_t integer_index = 0U;
         integer_index < TINYPY_INTEGER_CONSTANT_COUNT;
         ++integer_index) {
        __tinypy_internal_initialize_integer(
            &vm->integer_constants[integer_index],
            &vm->types[TINYPY_VALUE_INTEGER],
            TINYPY_INTEGER_CONSTANT_MIN + (int64_t)integer_index);
    }
    __tinypy_internal_initialize_float(
        &vm->float_zero_object,
        &vm->types[TINYPY_VALUE_FLOAT],
        0.0);
    __tinypy_internal_initialize_empty_string(
        &vm->empty_string_object,
        &vm->types[TINYPY_VALUE_STRING]);
    vm->empty_unicode = tinypy_unicode_from_utf8(vm, "", 0U);
    __tinypy_internal_initialize_empty_tuple(
        &vm->empty_tuple_object,
        &vm->types[TINYPY_VALUE_TUPLE]);
#define TINYPY_INTERNAL_KEY_CREATE(field, name, intern_name) \
    vm->field = tinypy_string_from_bytes(vm, name, sizeof(name) - 1U); \
    if (intern_name != 0) { \
        (void)tinypy_internal_string_intern(&vm->field, NULL); \
    }
    TINYPY_INTERNAL_KEY_LIST(TINYPY_INTERNAL_KEY_CREATE)
#undef TINYPY_INTERNAL_KEY_CREATE

    tinypy_internal_object_initialize_special_keys(vm);
    __tinypy_internal_initialize_type_dicts(vm);
    __tinypy_internal_initialize_type_docs(vm);
    tinypy_internal_initialize_native_descriptor_types(vm);
    tinypy_internal_initialize_container_types(vm);
    tinypy_internal_initialize_slice_type(vm);
    tinypy_internal_initialize_numeric_types(vm);
    tinypy_internal_initialize_string_types(vm);
    tinypy_internal_initialize_representation_types(vm);
    tinypy_internal_initialize_bytearray_methods(vm);
    tinypy_internal_initialize_weakref_type(vm);
    tinypy_internal_initialize_constructor_types(vm);
    tinypy_internal_initialize_descriptor_types(vm);
    tinypy_internal_initialize_native_function_type(vm);
    tinypy_internal_initialize_dictproxy_type(vm);
    tinypy_internal_initialize_cell_type(vm);
    tinypy_internal_initialize_code_type(vm);
    tinypy_internal_initialize_function_type(vm);
    tinypy_internal_initialize_module_type(vm);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_CLASS]);
    tinypy_internal_initialize_super_type(vm);
    tinypy_internal_initialize_partial_type(vm);
    tinypy_internal_initialize_iterator_types(vm);
    tinypy_internal_initialize_buffer_type(vm);
    tinypy_internal_initialize_memoryview_type(vm);
    tinypy_internal_initialize_generator_types(vm);
    tinypy_internal_initialize_set_types(vm);
    tinypy_internal_initialize_dict_view_types(vm);
    tinypy_internal_initialize_output_type(vm);
    __tinypy_internal_initialize_builtins(vm);
    tinypy_internal_initialize_exceptions(vm);
    tinypy_internal_initialize_builtin_functions(vm);
    tinypy_dict_set(vm->builtins, vm->internal_xrange_key, &vm->types[TINYPY_VALUE_XRANGE].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_enumerate_key, &vm->types[TINYPY_VALUE_ENUMERATE].base.base);
    tinypy_dict_set(vm->builtins, vm->internal_reversed_key, &vm->types[TINYPY_VALUE_REVERSED].base.base);
    __tinypy_internal_initialize_modules(vm);

    return vm;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_builtins(const tinypy_vm_t *vm) {
    return vm->builtins;
}
typedef struct tinypy_shutdown_entry_t {
    tinypy_value_t *value;
    size_t allocation_size;
    tinypy_destroy_slot_t destroy;
    tinypy_value_type_e kind;
    int32_t auxiliary;
} tinypy_shutdown_entry_t;
typedef struct tinypy_shutdown_graph_t {
    tinypy_vm_t *vm;
    tinypy_shutdown_entry_t *entries;
    size_t entry_count;
    size_t entry_capacity;
    size_t *slots;
    size_t slot_count;
    size_t slot_capacity;
} tinypy_shutdown_graph_t;
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_shutdown_pointer_hash(const tinypy_value_t *value) {
    uintptr_t bits = (uintptr_t)value;

    bits >>= 3U;
    bits ^= bits >> 17U;
    bits *= (uintptr_t)UINT64_C(0xed5ad4bb);
    bits ^= bits >> 11U;
    return (size_t)bits;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_shutdown_entries_reserve(tinypy_shutdown_graph_t *graph) {
    size_t old_size;
    size_t new_capacity;
    size_t new_size;

    if (graph->entry_count < graph->entry_capacity) {
        return;
    }
    new_capacity = graph->entry_capacity == 0U ? 256U : graph->entry_capacity * 2U;
    old_size = graph->entry_capacity * sizeof(*graph->entries);
    new_size = new_capacity * sizeof(*graph->entries);
    tinypy_shutdown_entry_t *new_entries = (tinypy_shutdown_entry_t *)tinypy_internal_vm_allocate(graph->vm, new_size);
    if (graph->entries != NULL) {
        (void)memcpy(new_entries, graph->entries, graph->entry_count * sizeof(*new_entries));
        tinypy_internal_vm_deallocate(graph->vm, graph->entries, old_size);
    }
    graph->entries = new_entries;
    graph->entry_capacity = new_capacity;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_shutdown_slots_rebuild(tinypy_shutdown_graph_t *graph, size_t new_capacity) {
    size_t *new_slots;
    size_t new_size;
    size_t index;

    new_size = new_capacity * sizeof(*new_slots);
    new_slots = (size_t *)tinypy_internal_vm_allocate(graph->vm, new_size);
    (void)memset(new_slots, 0, new_size);
    for (index = 0U; index < graph->entry_count; ++index) {
        size_t slot = __tinypy_shutdown_pointer_hash(graph->entries[index].value) & (new_capacity - 1U);

        while (new_slots[slot] != 0U) {
            slot = (slot + 1U) & (new_capacity - 1U);
        }
        new_slots[slot] = index + 1U;
    }
    if (graph->slots != NULL) {
        tinypy_internal_vm_deallocate(graph->vm, graph->slots, graph->slot_capacity * sizeof(*graph->slots));
    }
    graph->slots = new_slots;
    graph->slot_count = graph->entry_count;
    graph->slot_capacity = new_capacity;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_shutdown_find(const tinypy_shutdown_graph_t *graph, const tinypy_value_t *value) {
    size_t slot;

    if (graph->slot_capacity == 0U) {
        return SIZE_MAX;
    }
    slot = __tinypy_shutdown_pointer_hash(value) & (graph->slot_capacity - 1U);
    while (graph->slots[slot] != 0U) {
        size_t index = graph->slots[slot] - 1U;

        if (graph->entries[index].value == value) {
            return index;
        }
        slot = (slot + 1U) & (graph->slot_capacity - 1U);
    }
    return SIZE_MAX;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_shutdown_add(tinypy_shutdown_graph_t *graph, tinypy_value_t *value) {
    size_t slot;

    if (value == NULL || tinypy_internal_value_is_vm_embedded(graph->vm, value) != 0) {
        return;
    }
    if (graph->slot_capacity == 0U) {
        __tinypy_shutdown_slots_rebuild(graph, 256U);
    }
    if (__tinypy_shutdown_find(graph, value) != SIZE_MAX) {
        return;
    }
    if ((graph->slot_count + 1U) * 3U >= graph->slot_capacity * 2U) {
        __tinypy_shutdown_slots_rebuild(graph, graph->slot_capacity * 2U);
    }
    __tinypy_shutdown_entries_reserve(graph);
    tinypy_shutdown_entry_t *entry = &graph->entries[graph->entry_count];
    entry->value = value;
    entry->allocation_size = tinypy_internal_value_allocation_size(value);
    entry->destroy = value->type->destroy;
    entry->kind = TINYPY_VALUE_KIND(value);
    entry->auxiliary = INT32_C(0);
    slot = __tinypy_shutdown_pointer_hash(value) & (graph->slot_capacity - 1U);
    while (graph->slots[slot] != 0U) {
        slot = (slot + 1U) & (graph->slot_capacity - 1U);
    }
    graph->slots[slot] = graph->entry_count + 1U;
    graph->entry_count += 1U;
    graph->slot_count += 1U;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_shutdown_visit(tinypy_value_t *value, void *user_data) {
    __tinypy_shutdown_add((tinypy_shutdown_graph_t *)user_data, value);
}
//////////////////////////////////////////////////////////////////////////
/* follow_weak adds weak referents too, so that the final sweep also frees
   objects kept alive only by leaked owning cycles. Reachability queries must
   not do that. */
static void __tinypy_shutdown_collect(tinypy_shutdown_graph_t *graph, tinypy_bool_t follow_weak) {
    tinypy_vm_t *vm = graph->vm;
    size_t index;

    __tinypy_shutdown_add(graph, vm->empty_unicode);
    __tinypy_shutdown_add(graph, vm->modules);
    __tinypy_shutdown_add(graph, vm->builtins);
    __tinypy_shutdown_add(graph, vm->codec_module);
    __tinypy_shutdown_add(graph, vm->codec_search_path);
    __tinypy_shutdown_add(graph, vm->codec_cache);
    __tinypy_shutdown_add(graph, vm->codec_errors);
    __tinypy_shutdown_add(graph, vm->internal_strings);
#define TINYPY_INTERNAL_KEY_ROOT(field, name, intern_name) __tinypy_shutdown_add(graph, vm->field);
    TINYPY_INTERNAL_KEY_LIST(TINYPY_INTERNAL_KEY_ROOT)
#undef TINYPY_INTERNAL_KEY_ROOT
    __tinypy_shutdown_add(graph, vm->sys_module);
    __tinypy_shutdown_add(graph, vm->module_finder);
    if (vm->memoryview_type != NULL) {
        __tinypy_shutdown_add(graph, &vm->memoryview_type->base.base);
    }
    if (vm->sre_scanner_type != NULL) {
        __tinypy_shutdown_add(graph, &vm->sre_scanner_type->base.base);
    }
    __tinypy_shutdown_add(graph, vm->raised_type);
    __tinypy_shutdown_add(graph, vm->raised_value);
    __tinypy_shutdown_add(graph, vm->emergency_memory_error);
    __tinypy_shutdown_add(graph, vm->raised_traceback);
    __tinypy_shutdown_add(graph, vm->handled_type);
    __tinypy_shutdown_add(graph, vm->handled_value);
    __tinypy_shutdown_add(graph, vm->handled_traceback);
    if (vm->dictproxy_type != NULL) {
        __tinypy_shutdown_add(graph, &vm->dictproxy_type->base.base);
    }
    for (index = 0U; index < TINYPY_ITERATOR_TYPE_COUNT; ++index) {
        if (vm->iterator_types[index] != NULL) {
            __tinypy_shutdown_add(graph, &vm->iterator_types[index]->base.base);
        }
    }
    if (vm->weak_proxy_type != NULL) {
        __tinypy_shutdown_add(graph, &vm->weak_proxy_type->base.base);
    }
    if (vm->callable_weak_proxy_type != NULL) {
        __tinypy_shutdown_add(graph, &vm->callable_weak_proxy_type->base.base);
    }
    if (vm->native_method_descriptor_type != NULL) {
        __tinypy_shutdown_add(graph, &vm->native_method_descriptor_type->base.base);
    }
    if (vm->native_wrapper_descriptor_type != NULL) {
        __tinypy_shutdown_add(graph, &vm->native_wrapper_descriptor_type->base.base);
    }
    if (vm->native_method_wrapper_type != NULL) {
        __tinypy_shutdown_add(graph, &vm->native_method_wrapper_type->base.base);
    }
    for (index = 0U; index < 256U; ++index) {
        __tinypy_shutdown_add(graph, vm->string_char_cache[index]);
        __tinypy_shutdown_add(graph, vm->unicode_char_cache[index]);
    }
    if (vm->current_frame != NULL) {
        __tinypy_shutdown_add(graph, &vm->current_frame->base.base);
    }
    for (index = 0U; index < TINYPY_EXCEPTION_TYPE_COUNT; ++index) {
        if (vm->exception_types[index] != NULL) {
            __tinypy_shutdown_add(graph, &vm->exception_types[index]->base.base);
        }
    }
    for (index = 0U; index < TINYPY_BUILTIN_TYPE_COUNT; ++index) {
        tinypy_internal_dict_release_references(&vm->builtin_type_dicts[index].base, __tinypy_shutdown_visit, graph);
        __tinypy_shutdown_add(graph, vm->types[index].name_object);
        __tinypy_shutdown_add(graph, vm->types[index].bases);
        __tinypy_shutdown_add(graph, vm->types[index].mro);
        __tinypy_shutdown_add(graph, vm->types[index].subclasses);
    }
    for (index = 0U; index < graph->entry_count; ++index) {
        tinypy_value_t *value = graph->entries[index].value;
        tinypy_type_t *type = value->type;

        __tinypy_shutdown_add(graph, &type->base.base);
        if (type->traverse_references != NULL) {
            type->traverse_references(value, __tinypy_shutdown_visit, graph);
        }
        if (follow_weak != 0 && graph->entries[index].kind == TINYPY_VALUE_WEAKREF) {
            __tinypy_shutdown_add(graph, TINYPY_WEAKREF_OBJECT(value)->object);
        }
    }
    for (index = 0U; index < graph->entry_count; ++index) {
        if (graph->entries[index].kind == TINYPY_VALUE_TYPE) {
            tinypy_value_t *mro = ((tinypy_type_t *)graph->entries[index].value)->mro;
            size_t mro_index = mro != NULL ? __tinypy_shutdown_find(graph, mro) : SIZE_MAX;

            if (mro_index != SIZE_MAX) {
                graph->entries[mro_index].auxiliary = INT32_C(1);
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_shutdown_destroy_entry(tinypy_shutdown_graph_t *graph, tinypy_shutdown_entry_t *entry) {
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_value_unregister(graph->vm, entry->value);
#endif
    if (entry->destroy != NULL) {
        entry->destroy(entry->value);
    }
    tinypy_internal_vm_deallocate(graph->vm, entry->value, entry->allocation_size);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_shutdown_destroy_graph(tinypy_shutdown_graph_t *graph) {
    size_t index;

    for (index = 0U; index < graph->entry_count; ++index) {
        tinypy_shutdown_entry_t *entry = &graph->entries[index];

        if (entry->auxiliary == 0 && entry->kind == TINYPY_VALUE_WEAKREF) {
            __tinypy_shutdown_destroy_entry(graph, entry);
        }
    }
    for (index = 0U; index < graph->entry_count; ++index) {
        tinypy_shutdown_entry_t *entry = &graph->entries[index];

        if (entry->auxiliary == 0 && entry->kind != TINYPY_VALUE_WEAKREF && entry->kind != TINYPY_VALUE_TYPE) {
            __tinypy_shutdown_destroy_entry(graph, entry);
        }
    }
    for (index = 0U; index < graph->entry_count; ++index) {
        tinypy_shutdown_entry_t *entry = &graph->entries[index];

        if (entry->auxiliary == 0 && entry->kind == TINYPY_VALUE_TYPE) {
            __tinypy_shutdown_destroy_entry(graph, entry);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_shutdown_graph_destroy(tinypy_shutdown_graph_t *graph) {
    if (graph->slots != NULL) {
        tinypy_internal_vm_deallocate(graph->vm, graph->slots, graph->slot_capacity * sizeof(*graph->slots));
    }
    if (graph->entries != NULL) {
        tinypy_internal_vm_deallocate(graph->vm, graph->entries, graph->entry_capacity * sizeof(*graph->entries));
    }
}
//////////////////////////////////////////////////////////////////////////
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
void tinypy_internal_vm_visit_reachable_values(tinypy_vm_t *vm, tinypy_release_callback_t visit, void *user_data) {
    tinypy_shutdown_graph_t graph;
    size_t index;

    (void)memset(&graph, 0, sizeof(graph));
    graph.vm = vm;
    __tinypy_shutdown_collect(&graph, TINYPY_FALSE);
    for (index = 0U; index < graph.entry_count; ++index) {
        visit(graph.entries[index].value, user_data);
    }
    __tinypy_shutdown_graph_destroy(&graph);
}
//////////////////////////////////////////////////////////////////////////
#endif
/* Native finalizers may touch other values, so they run while the VM is still
   live and every reachable object is intact. The sweep below then frees
   memory without calling them again. */
static void __tinypy_shutdown_finalize_natives(tinypy_vm_t *vm) {
    tinypy_shutdown_graph_t graph;
    size_t index;

    (void)memset(&graph, 0, sizeof(graph));
    graph.vm = vm;
    __tinypy_shutdown_collect(&graph, TINYPY_TRUE);
    for (index = 0U; index < graph.entry_count; ++index) {
        tinypy_shutdown_entry_t *entry = &graph.entries[index];

        if (entry->kind == TINYPY_VALUE_NATIVE_FUNCTION || entry->kind == TINYPY_VALUE_NATIVE_INSTANCE) {
            TINYPY_INCREF(entry->value);
        }
    }
    for (index = 0U; index < graph.entry_count; ++index) {
        tinypy_shutdown_entry_t *entry = &graph.entries[index];

        if (entry->kind == TINYPY_VALUE_NATIVE_FUNCTION) {
            tinypy_internal_native_function_finalize(entry->value);
        }
        else if (entry->kind == TINYPY_VALUE_NATIVE_INSTANCE) {
            tinypy_internal_native_instance_finalize(entry->value);
        }
    }
    for (index = 0U; index < graph.entry_count; ++index) {
        tinypy_shutdown_entry_t *entry = &graph.entries[index];

        if (entry->kind == TINYPY_VALUE_NATIVE_FUNCTION || entry->kind == TINYPY_VALUE_NATIVE_INSTANCE) {
            TINYPY_DECREF(entry->value);
        }
    }
    __tinypy_shutdown_graph_destroy(&graph);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_vm_destroy(tinypy_vm_t *vm) {
    tinypy_allocator_t allocator;
    tinypy_shutdown_graph_t graph;
    size_t type_index;

    __tinypy_shutdown_finalize_natives(vm);
    vm->state = TINYPY_VM_STATE_DESTROYING;
    tinypy_internal_intern_finalize(vm);
    tinypy_internal_type_lookup_cache_finalize(vm);
    tinypy_internal_integer_free_list_finalize(vm);
    tinypy_internal_frame_free_list_finalize(vm);
    tinypy_internal_method_free_list_finalize(vm);
    tinypy_internal_native_method_free_list_finalize(vm);
    (void)memset(&graph, 0, sizeof(graph));
    graph.vm = vm;
    __tinypy_shutdown_collect(&graph, TINYPY_TRUE);
    __tinypy_shutdown_destroy_graph(&graph);
    for (type_index = 0U;
         type_index < TINYPY_BUILTIN_TYPE_COUNT;
         ++type_index) {
        tinypy_internal_dict_destroy(&vm->builtin_type_dicts[type_index].base);
    }
    __tinypy_shutdown_graph_destroy(&graph);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    tinypy_internal_cycle_diagnostics_finalize(vm);
#endif
    tinypy_internal_pool_finalize(vm);

    allocator = vm->allocator;
    allocator.deallocate(
        allocator.user_data,
        vm,
        sizeof(*vm),
        TINYPY_INTERNAL_ALIGNMENT);
}
