#include "tinypy/vm.h"

#include "internal.h"

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
        tinypy_internal_code_release_references, NULL);
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
    vm->types[TINYPY_VALUE_MODULE].has_instance_dict = INT32_C(1);
    vm->types[TINYPY_VALUE_MODULE].dict_offset = offsetof(tinypy_module_object_t, dict);
    __tinypy_internal_initialize_type(
        vm, &vm->types[TINYPY_VALUE_NATIVE_FUNCTION], &vm->types[TINYPY_VALUE_TYPE], "builtin_function_or_method", 26U,
        sizeof(tinypy_native_function_object_t), 0U,
        0U, &vm->types[TINYPY_VALUE_INSTANCE],
        tinypy_internal_native_function_release_references,
        tinypy_internal_native_function_destroy);
    vm->types[TINYPY_VALUE_NATIVE_FUNCTION].call = tinypy_internal_native_function_call;
    vm->types[TINYPY_VALUE_NATIVE_FUNCTION].descriptor_get = tinypy_internal_native_function_descriptor_get;
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
        vm, &vm->types[TINYPY_VALUE_PARTIAL], &vm->types[TINYPY_VALUE_TYPE], "partial", 7U,
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
        tinypy_type_set_attr(&vm->types[index], "__doc__", 7U, &vm->none_object.base);
    }
    for (index = 0U; index < sizeof(docs) / sizeof(docs[0]); ++index) {
        size_t doc_size = strlen(docs[index].doc);
        tinypy_value_t *doc = tinypy_string_from_bytes(vm, docs[index].doc, doc_size);

        tinypy_type_set_attr(&vm->types[docs[index].kind], "__doc__", 7U, doc);
        TINYPY_DECREF(doc);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_builtin_set(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *value) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(vm->builtins, key, value);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_builtins(tinypy_vm_t *vm) {
    tinypy_value_t *false_value;

    vm->builtins = tinypy_dict_new(vm);
    tinypy_value_t *none_value = tinypy_none_get(vm);
    tinypy_value_t *true_value = tinypy_bool_from_i32(vm, 1);
    false_value = tinypy_bool_from_i32(vm, 0);
    __tinypy_internal_builtin_set(vm, "None", 4U, none_value);
    __tinypy_internal_builtin_set(vm, "NotImplemented", 14U, &vm->not_implemented_object.base);
    __tinypy_internal_builtin_set(vm, "True", 4U, true_value);
    __tinypy_internal_builtin_set(vm, "False", 5U, false_value);
    __tinypy_internal_builtin_set(vm, "__debug__", 9U, vm->optimize_level == 0 ? true_value : false_value);
    __tinypy_internal_builtin_set(vm, "Ellipsis", 8U, &vm->ellipsis_object.base);
    __tinypy_internal_builtin_set(vm, "object", 6U, &vm->types[TINYPY_VALUE_INSTANCE].base.base);
    __tinypy_internal_builtin_set(vm, "type", 4U, &vm->types[TINYPY_VALUE_TYPE].base.base);
    __tinypy_internal_builtin_set(vm, "bool", 4U, &vm->types[TINYPY_VALUE_BOOL].base.base);
    __tinypy_internal_builtin_set(vm, "int", 3U, &vm->types[TINYPY_VALUE_INTEGER].base.base);
    __tinypy_internal_builtin_set(vm, "long", 4U, &vm->types[TINYPY_VALUE_LONG].base.base);
    __tinypy_internal_builtin_set(vm, "float", 5U, &vm->types[TINYPY_VALUE_FLOAT].base.base);
    __tinypy_internal_builtin_set(vm, "complex", 7U, &vm->types[TINYPY_VALUE_COMPLEX].base.base);
    __tinypy_internal_builtin_set(vm, "str", 3U, &vm->types[TINYPY_VALUE_STRING].base.base);
    __tinypy_internal_builtin_set(vm, "bytes", 5U, &vm->types[TINYPY_VALUE_STRING].base.base);
    __tinypy_internal_builtin_set(vm, "basestring", 10U, &vm->types[TINYPY_VALUE_INVALID].base.base);
    __tinypy_internal_builtin_set(vm, "unicode", 7U, &vm->types[TINYPY_VALUE_UNICODE].base.base);
    __tinypy_internal_builtin_set(vm, "tuple", 5U, &vm->types[TINYPY_VALUE_TUPLE].base.base);
    __tinypy_internal_builtin_set(vm, "list", 4U, &vm->types[TINYPY_VALUE_LIST].base.base);
    __tinypy_internal_builtin_set(vm, "dict", 4U, &vm->types[TINYPY_VALUE_DICT].base.base);
    __tinypy_internal_builtin_set(vm, "set", 3U, &vm->types[TINYPY_VALUE_SET].base.base);
    __tinypy_internal_builtin_set(vm, "frozenset", 9U, &vm->types[TINYPY_VALUE_FROZENSET].base.base);
    __tinypy_internal_builtin_set(vm, "slice", 5U, &vm->types[TINYPY_VALUE_SLICE].base.base);
    __tinypy_internal_builtin_set(vm, "staticmethod", 12U, &vm->types[TINYPY_VALUE_STATIC_METHOD].base.base);
    __tinypy_internal_builtin_set(vm, "classmethod", 11U, &vm->types[TINYPY_VALUE_CLASS_METHOD].base.base);
    __tinypy_internal_builtin_set(vm, "property", 8U, &vm->types[TINYPY_VALUE_PROPERTY].base.base);
    __tinypy_internal_builtin_set(vm, "super", 5U, &vm->types[TINYPY_VALUE_SUPER].base.base);
    __tinypy_internal_builtin_set(vm, "buffer", 6U, &vm->types[TINYPY_VALUE_BUFFER].base.base);
    __tinypy_internal_builtin_set(vm, "memoryview", 10U, &vm->memoryview_type->base.base);
    __tinypy_internal_builtin_set(vm, "bytearray", 9U, &vm->types[TINYPY_VALUE_BYTEARRAY].base.base);
    __tinypy_internal_builtin_set(vm, "file", 4U, &vm->types[TINYPY_VALUE_FILE].base.base);
    TINYPY_DECREF(false_value);
    TINYPY_DECREF(true_value);
    TINYPY_DECREF(none_value);
    TINYPY_DECREF(&vm->memoryview_type->base.base);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_register_module(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *module) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(vm->modules, key, module);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_sys_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "sys function received invalid arguments", out_error);
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
    if (__tinypy_internal_sys_arguments(vm, args, kwargs, 0U, 0U, out_error) == 0) {
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
        none_values[index] = tinypy_none_get(vm);
    }
    tinypy_value_t *result = tinypy_tuple_from_items(vm, none_values, 3U);
    for (index = 0U; index < 3U; ++index) {
        TINYPY_DECREF(none_values[index]);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_exc_clear(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_internal_sys_arguments(vm, args, kwargs, 0U, 0U, out_error) == 0) {
        return NULL;
    }
    tinypy_internal_exception_clear_handled(vm);
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_getframe(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_frame_object_t *frame = vm->current_frame;
    int64_t depth = INT64_C(0);

    (void)user_data;
    if (__tinypy_internal_sys_arguments(vm, args, kwargs, 0U, 1U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) != 0U) {
        tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);

        if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_INTEGER) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "frame depth must be an integer", out_error);
            return NULL;
        }
        depth = TINYPY_INTEGER_VALUE(value);
        if (depth < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "frame depth must not be negative", out_error);
            return NULL;
        }
    }
    while (depth != 0 && frame != NULL) {
        frame = frame->back != NULL ? TINYPY_FRAME_OBJECT(frame->back) : NULL;
        depth -= 1;
    }
    if (frame == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "call stack is not deep enough", out_error);
        return NULL;
    }
    TINYPY_INCREF(&frame->base.base);
    return &frame->base.base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_getrecursionlimit(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_internal_sys_arguments(vm, args, kwargs, 0U, 0U, out_error) == 0) {
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
    if (__tinypy_internal_sys_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_INTEGER) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "recursion limit must be an integer", out_error);
        return NULL;
    }
    limit = TINYPY_INTEGER_VALUE(value);
    if (limit <= 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "recursion limit must be positive", out_error);
        return NULL;
    }
    if (limit > INT32_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "recursion limit is too large", out_error);
        return NULL;
    }
    vm->recursion_limit = (size_t)limit;
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_getdefaultencoding(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_internal_sys_arguments(vm, args, kwargs, 0U, 0U, out_error) == 0) {
        return NULL;
    }
    result = tinypy_string_from_bytes(vm, "ascii", 5U);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_sys_exit(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *exception;

    (void)user_data;
    if (__tinypy_internal_sys_arguments(vm, args, kwargs, 0U, 1U, out_error) == 0) {
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
static tinypy_value_t *__tinypy_internal_sys_displayhook(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *value;
    tinypy_value_t *underscore;
    tinypy_value_t *representation;
    tinypy_value_t *sys_module;
    tinypy_value_t *stdout_value;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_internal_sys_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE) {
        tinypy_value_t *return_value_1 = tinypy_none_get(vm);
        return return_value_1;
    }
    underscore = tinypy_internal_string_from_bytes_checked(vm, "_", 1U, out_error);
    if (underscore == NULL) {
        return NULL;
    }
    if (tinypy_internal_dict_set_checked(vm, vm->builtins, underscore, &vm->none_object.base, out_error) == 0) {
        TINYPY_DECREF(underscore);
        return NULL;
    }
    representation = tinypy_object_repr(value, out_error);
    if (representation == NULL) {
        TINYPY_DECREF(underscore);
        return NULL;
    }
    sys_module = tinypy_dict_get(vm->modules, vm->sys_key);
    stdout_value = tinypy_module_get_value(sys_module, "stdout", 6U);
    if (stdout_value == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "lost sys.stdout", out_error);
        TINYPY_DECREF(representation);
        TINYPY_DECREF(underscore);
        return NULL;
    }
    TINYPY_INCREF(stdout_value);
    if (tinypy_internal_output_write(stdout_value, TINYPY_TEXT_BYTES(representation), TINYPY_TEXT_BYTE_SIZE(representation), out_error) == 0
        || tinypy_internal_output_write(stdout_value, "\n", 1U, out_error) == 0) {
        TINYPY_DECREF(stdout_value);
        TINYPY_DECREF(representation);
        TINYPY_DECREF(underscore);
        return NULL;
    }
    TINYPY_DECREF(stdout_value);
    TINYPY_DECREF(representation);
    if (tinypy_internal_dict_set_checked(vm, vm->builtins, underscore, value, out_error) == 0) {
        TINYPY_DECREF(underscore);
        return NULL;
    }
    TINYPY_DECREF(underscore);
    result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_future_feature_init(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_internal_sys_arguments(vm, args, kwargs, 4U, 4U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_instance_set_attr(self, "optional", 8U, TINYPY_TUPLE_GET(args, 1U));
    tinypy_instance_set_attr(self, "mandatory", 9U, TINYPY_TUPLE_GET(args, 2U));
    tinypy_instance_set_attr(self, "compiler_flag", 13U, TINYPY_TUPLE_GET(args, 3U));
    result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_future_feature_release(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const char *name = (intptr_t)user_data == 0 ? "optional" : "mandatory";
    size_t name_size = (intptr_t)user_data == 0 ? 8U : 9U;

    if (__tinypy_internal_sys_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = tinypy_object_get_attr(TINYPY_TUPLE_GET(args, 0U), name, name_size, out_error);
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
    if (__tinypy_internal_sys_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    optional = tinypy_object_get_attr(self, "optional", 8U, out_error);
    mandatory = optional != NULL ? tinypy_object_get_attr(self, "mandatory", 9U, out_error) : NULL;
    flag = mandatory != NULL ? tinypy_object_get_attr(self, "compiler_flag", 13U, out_error) : NULL;
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
static void __tinypy_future_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, user_data, NULL);

    tinypy_type_set_attr(type, name, name_size, function);
    TINYPY_DECREF(function);
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
static void __tinypy_future_add_feature(tinypy_vm_t *vm, tinypy_value_t *module, tinypy_type_t *feature_type, tinypy_value_t *names, const char *name, size_t name_size, tinypy_value_t *optional, tinypy_value_t *mandatory, int64_t flag) {
    tinypy_value_t *instance = tinypy_instance_new(feature_type);
    tinypy_value_t *flag_value = tinypy_integer_from_i64(vm, flag);
    tinypy_value_t *name_value = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_instance_set_attr(instance, "optional", 8U, optional);
    tinypy_instance_set_attr(instance, "mandatory", 9U, mandatory);
    tinypy_instance_set_attr(instance, "compiler_flag", 13U, flag_value);
    tinypy_module_add_value(module, name, name_size, instance);
    (void)tinypy_internal_list_append_checked(names, name_value, NULL);
    TINYPY_DECREF(name_value);
    TINYPY_DECREF(flag_value);
    TINYPY_DECREF(instance);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_initialize_future_module(tinypy_vm_t *vm) {
    static const char *const constant_names[] = {"CO_NESTED", "CO_GENERATOR_ALLOWED", "CO_FUTURE_DIVISION", "CO_FUTURE_ABSOLUTE_IMPORT", "CO_FUTURE_WITH_STATEMENT", "CO_FUTURE_PRINT_FUNCTION", "CO_FUTURE_UNICODE_LITERALS"};
    static const size_t constant_name_sizes[] = {9U, 20U, 18U, 25U, 24U, 24U, 26U};
    static const int64_t constant_values[] = {16, 0, 8192, 16384, 32768, 65536, 131072};
    tinypy_value_t *module = tinypy_module_new(vm, "__future__", 10U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "__future__", 10U);
    tinypy_type_t *feature_type = tinypy_type_new(vm, "_Feature", 8U, NULL, 0U, NULL, NULL, NULL);
    tinypy_value_t *module_name = tinypy_string_from_bytes(vm, "__future__", 10U);
    tinypy_value_t *feature_names = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *optional;
    tinypy_value_t *mandatory;
    size_t index;

    tinypy_module_add_value(module, "__name__", 8U, name);
    tinypy_type_set_attr(feature_type, "__module__", 10U, module_name);
    __tinypy_future_add_method(feature_type, "__init__", 8U, __tinypy_future_feature_init, NULL);
    __tinypy_future_add_method(feature_type, "getOptionalRelease", 18U, __tinypy_future_feature_release, (void *)(intptr_t)0);
    __tinypy_future_add_method(feature_type, "getMandatoryRelease", 19U, __tinypy_future_feature_release, (void *)(intptr_t)1);
    __tinypy_future_add_method(feature_type, "__repr__", 8U, __tinypy_future_feature_repr, NULL);
    tinypy_module_add_value(module, "_Feature", 8U, &feature_type->base.base);
    for (index = 0U; index != sizeof(constant_values) / sizeof(constant_values[0]); ++index) {
        tinypy_value_t *value = tinypy_integer_from_i64(vm, constant_values[index]);

        tinypy_module_add_value(module, constant_names[index], constant_name_sizes[index], value);
        TINYPY_DECREF(value);
    }

#define TINYPY_FUTURE_FEATURE(feature_name, optional_major, optional_minor, optional_level, optional_serial, mandatory_major, mandatory_minor, mandatory_level, flag_value) \
    do {                                                                                                                                                                                   \
        optional = __tinypy_future_release_tuple(vm, optional_major, optional_minor, 0, optional_level, sizeof(optional_level) - 1U, optional_serial);                                      \
        mandatory = __tinypy_future_release_tuple(vm, mandatory_major, mandatory_minor, 0, mandatory_level, sizeof(mandatory_level) - 1U, 0);                                               \
        __tinypy_future_add_feature(vm, module, feature_type, feature_names, #feature_name, sizeof(#feature_name) - 1U, optional, mandatory, flag_value);                                     \
        TINYPY_DECREF(mandatory);                                                                                                                                                           \
        TINYPY_DECREF(optional);                                                                                                                                                            \
    } while (0)

    TINYPY_FUTURE_FEATURE(nested_scopes, 2, 1, "beta", 1, 2, 2, "alpha", 16);
    TINYPY_FUTURE_FEATURE(generators, 2, 2, "alpha", 1, 2, 3, "final", 0);
    TINYPY_FUTURE_FEATURE(division, 2, 2, "alpha", 2, 3, 0, "alpha", 8192);
    TINYPY_FUTURE_FEATURE(absolute_import, 2, 5, "alpha", 1, 3, 0, "alpha", 16384);
    TINYPY_FUTURE_FEATURE(with_statement, 2, 5, "alpha", 1, 2, 6, "alpha", 32768);
    TINYPY_FUTURE_FEATURE(print_function, 2, 6, "alpha", 2, 3, 0, "alpha", 65536);
    TINYPY_FUTURE_FEATURE(unicode_literals, 2, 6, "alpha", 2, 3, 0, "alpha", 131072);
#undef TINYPY_FUTURE_FEATURE

    tinypy_module_add_value(module, "all_feature_names", 17U, feature_names);
    TINYPY_DECREF(feature_names);
    TINYPY_DECREF(module_name);
    TINYPY_DECREF(&feature_type->base.base);
    TINYPY_DECREF(name);
    return module;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_sys_add_function(tinypy_vm_t *vm, tinypy_value_t *module, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    tinypy_module_add_value(module, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_modules(tinypy_vm_t *vm) {
    tinypy_value_t *name;
    tinypy_value_t *stdout_value;
    tinypy_value_t *stderr_value;
    tinypy_value_t *future_module;
    uint16_t byteorder_probe = UINT16_C(1);

    vm->modules = tinypy_dict_new(vm);
    tinypy_value_t *builtin_module = tinypy_internal_module_from_dict(vm, "__builtin__", 11U, vm->builtins);
    name = tinypy_string_from_bytes(vm, "__builtin__", 11U);
    tinypy_module_add_value(builtin_module, "__name__", 8U, name);
    TINYPY_DECREF(name);
    tinypy_internal_register_module(vm, "__builtin__", 11U, builtin_module);

    tinypy_value_t *sys_module = tinypy_module_new(vm, "sys", 3U);
    name = tinypy_string_from_bytes(vm, "sys", 3U);
    tinypy_module_add_value(sys_module, "__name__", 8U, name);
    TINYPY_DECREF(name);
    tinypy_module_add_value(sys_module, "modules", 7U, vm->modules);
    stdout_value = tinypy_internal_output_stream_new(vm, TINYPY_OUTPUT_STDOUT);
    stderr_value = tinypy_internal_output_stream_new(vm, TINYPY_OUTPUT_STDERR);
    tinypy_module_add_value(sys_module, "stdout", 6U, stdout_value);
    tinypy_module_add_value(sys_module, "__stdout__", 10U, stdout_value);
    tinypy_module_add_value(sys_module, "stderr", 6U, stderr_value);
    tinypy_module_add_value(sys_module, "__stderr__", 10U, stderr_value);
    name = tinypy_string_from_bytes(vm, *((const uint8_t *)&byteorder_probe) == 1U ? "little" : "big", *((const uint8_t *)&byteorder_probe) == 1U ? 6U : 3U);
    tinypy_module_add_value(sys_module, "byteorder", 9U, name);
    TINYPY_DECREF(name);
    name = tinypy_integer_from_i64(vm, INT64_C(0x020712f0));
    tinypy_module_add_value(sys_module, "hexversion", 10U, name);
    TINYPY_DECREF(name);
    name = tinypy_integer_from_i64(vm, INT64_MAX);
    tinypy_module_add_value(sys_module, "maxint", 6U, name);
    tinypy_module_add_value(sys_module, "maxsize", 7U, name);
    TINYPY_DECREF(name);
    name = tinypy_integer_from_i64(vm, INT64_C(0x10ffff));
    tinypy_module_add_value(sys_module, "maxunicode", 10U, name);
    TINYPY_DECREF(name);
    tinypy_module_add_value(sys_module, "py3kwarning", 11U, &vm->false_object.base);
    tinypy_module_add_value(sys_module, "dont_write_bytecode", 19U, &vm->false_object.base);
    name = tinypy_string_from_bytes(vm, "2.7.18 (tinypy)", 15U);
    tinypy_module_add_value(sys_module, "version", 7U, name);
    TINYPY_DECREF(name);
    tinypy_value_t *version_items[5];
    version_items[0] = tinypy_integer_from_i64(vm, INT64_C(2));
    version_items[1] = tinypy_integer_from_i64(vm, INT64_C(7));
    version_items[2] = tinypy_integer_from_i64(vm, INT64_C(18));
    version_items[3] = tinypy_string_from_bytes(vm, "final", 5U);
    version_items[4] = tinypy_integer_from_i64(vm, INT64_C(0));
    tinypy_value_t *version_info = tinypy_tuple_from_items(vm, version_items, 5U);
    for (size_t version_index = 0U; version_index != 5U; ++version_index) {
        TINYPY_DECREF(version_items[version_index]);
    }
    tinypy_module_add_value(sys_module, "version_info", 12U, version_info);
    TINYPY_DECREF(version_info);
    name = tinypy_integer_from_i64(vm, INT64_C(1013));
    tinypy_module_add_value(sys_module, "api_version", 11U, name);
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
    tinypy_module_add_value(sys_module, "platform", 8U, name);
    TINYPY_DECREF(name);
    name = tinypy_string_from_bytes(vm, "short", 5U);
    tinypy_module_add_value(sys_module, "float_repr_style", 16U, name);
    TINYPY_DECREF(name);
    tinypy_value_t *warn_options = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *meta_path = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *path_hooks = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *path_importer_cache = tinypy_dict_new(vm);
    tinypy_module_add_value(sys_module, "warnoptions", 11U, warn_options);
    tinypy_module_add_value(sys_module, "meta_path", 9U, meta_path);
    tinypy_module_add_value(sys_module, "path_hooks", 10U, path_hooks);
    tinypy_module_add_value(sys_module, "path_importer_cache", 19U, path_importer_cache);
    TINYPY_DECREF(path_importer_cache);
    TINYPY_DECREF(path_hooks);
    TINYPY_DECREF(meta_path);
    TINYPY_DECREF(warn_options);
    static const char *const builtin_module_name_bytes[] = {"__builtin__", "__future__", "_codecs", "_functools", "_sre", "_struct", "_weakref", "copy_reg", "exceptions", "sys"};
    static const size_t builtin_module_name_sizes[] = {11U, 10U, 7U, 10U, 4U, 7U, 8U, 8U, 10U, 3U};
    tinypy_value_t *builtin_module_name_values[10];
    for (size_t module_index = 0U; module_index != 10U; ++module_index) {
        builtin_module_name_values[module_index] = tinypy_string_from_bytes(vm, builtin_module_name_bytes[module_index], builtin_module_name_sizes[module_index]);
    }
    tinypy_value_t *builtin_module_names = tinypy_tuple_from_items(vm, builtin_module_name_values, 10U);
    for (size_t module_index = 0U; module_index != 10U; ++module_index) {
        TINYPY_DECREF(builtin_module_name_values[module_index]);
    }
    tinypy_module_add_value(sys_module, "builtin_module_names", 20U, builtin_module_names);
    TINYPY_DECREF(builtin_module_names);
    TINYPY_DECREF(stderr_value);
    TINYPY_DECREF(stdout_value);
    __tinypy_internal_sys_add_function(vm, sys_module, "exc_info", 8U, __tinypy_internal_sys_exc_info);
    __tinypy_internal_sys_add_function(vm, sys_module, "exc_clear", 9U, __tinypy_internal_sys_exc_clear);
    __tinypy_internal_sys_add_function(vm, sys_module, "_getframe", 9U, __tinypy_internal_sys_getframe);
    __tinypy_internal_sys_add_function(vm, sys_module, "getrecursionlimit", 17U, __tinypy_internal_sys_getrecursionlimit);
    __tinypy_internal_sys_add_function(vm, sys_module, "setrecursionlimit", 17U, __tinypy_internal_sys_setrecursionlimit);
    __tinypy_internal_sys_add_function(vm, sys_module, "getdefaultencoding", 18U, __tinypy_internal_sys_getdefaultencoding);
    __tinypy_internal_sys_add_function(vm, sys_module, "exit", 4U, __tinypy_internal_sys_exit);
    tinypy_value_t *displayhook = tinypy_native_function_new(vm, "displayhook", 11U, __tinypy_internal_sys_displayhook, NULL, NULL);
    tinypy_module_add_value(sys_module, "displayhook", 11U, displayhook);
    tinypy_module_add_value(sys_module, "__displayhook__", 15U, displayhook);
    TINYPY_DECREF(displayhook);
    tinypy_internal_register_module(vm, "sys", 3U, sys_module);
    future_module = __tinypy_internal_initialize_future_module(vm);
    tinypy_internal_register_module(vm, "__future__", 10U, future_module);
    tinypy_internal_initialize_weakref_module(vm);
    tinypy_internal_initialize_codecs_module(vm);
    tinypy_internal_initialize_functools_module(vm);
    tinypy_internal_initialize_struct_module(vm);
    tinypy_internal_initialize_copy_reg_module(vm);
    tinypy_internal_initialize_sre_module(vm);
    tinypy_internal_initialize_exceptions_module(vm);
    TINYPY_DECREF(sys_module);
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
    vm->max_heap_bytes = config->max_heap_bytes;
    vm->allocated_bytes = sizeof(*vm);
    vm->type_lookup_cache_epoch = UINT64_C(1);
    vm->recursion_limit = 1000U;
    vm->optimize_level =
        config->struct_size >= (uint32_t)(offsetof(tinypy_vm_config_t, optimize_level) + sizeof(config->optimize_level))
            ? config->optimize_level
            : 0;
    tinypy_internal_pool_initialize(vm);

    if (config->host != NULL) {
        vm->host = *config->host;
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
    __tinypy_internal_initialize_empty_tuple(
        &vm->empty_tuple_object,
        &vm->types[TINYPY_VALUE_TUPLE]);
    vm->builtins_key = tinypy_string_from_bytes(vm, "__builtins__", 12U);
    vm->keys_key = tinypy_string_from_bytes(vm, "keys", 4U);
    vm->key_key = tinypy_string_from_bytes(vm, "key", 3U);
    vm->softspace_key = tinypy_string_from_bytes(vm, "softspace", 9U);
    vm->sys_key = tinypy_string_from_bytes(vm, "sys", 3U);
    vm->metaclass_key = tinypy_string_from_bytes(vm, "__metaclass__", 13U);
    vm->special_getattribute_key = tinypy_string_from_bytes(vm, "__getattribute__", 16U);
    vm->special_getattr_key = tinypy_string_from_bytes(vm, "__getattr__", 11U);
    vm->special_get_key = tinypy_string_from_bytes(vm, "__get__", 7U);
    vm->special_set_key = tinypy_string_from_bytes(vm, "__set__", 7U);
    vm->special_delete_key = tinypy_string_from_bytes(vm, "__delete__", 10U);
    vm->special_call_key = tinypy_string_from_bytes(vm, "__call__", 8U);
    vm->special_iter_key = tinypy_string_from_bytes(vm, "__iter__", 8U);
    vm->special_length_key = tinypy_string_from_bytes(vm, "__len__", 7U);
    vm->special_getitem_key = tinypy_string_from_bytes(vm, "__getitem__", 11U);
    vm->special_setitem_key = tinypy_string_from_bytes(vm, "__setitem__", 11U);
    vm->special_delitem_key = tinypy_string_from_bytes(vm, "__delitem__", 11U);
    vm->special_contains_key = tinypy_string_from_bytes(vm, "__contains__", 12U);
    vm->special_index_key = tinypy_string_from_bytes(vm, "__index__", 9U);
    vm->special_hash_key = tinypy_string_from_bytes(vm, "__hash__", 8U);
    vm->special_repr_key = tinypy_string_from_bytes(vm, "__repr__", 8U);
    vm->special_str_key = tinypy_string_from_bytes(vm, "__str__", 7U);
    vm->special_nonzero_key = tinypy_string_from_bytes(vm, "__nonzero__", 11U);
    vm->special_enter_key = tinypy_string_from_bytes(vm, "__enter__", 9U);
    vm->special_exit_key = tinypy_string_from_bytes(vm, "__exit__", 8U);
    vm->special_format_key = tinypy_string_from_bytes(vm, "__format__", 10U);
    vm->special_new_key = tinypy_string_from_bytes(vm, "__new__", 7U);
    vm->special_init_key = tinypy_string_from_bytes(vm, "__init__", 8U);
    vm->special_name_key = tinypy_string_from_bytes(vm, "__name__", 8U);
    tinypy_internal_string_set_interned(vm->keys_key, 1);
    tinypy_internal_string_set_interned(vm->key_key, 1);
    tinypy_internal_string_set_interned(vm->softspace_key, 1);
    tinypy_internal_string_set_interned(vm->sys_key, 1);
    tinypy_internal_string_set_interned(vm->metaclass_key, 1);
    tinypy_internal_string_set_interned(vm->special_getattribute_key, 1);
    tinypy_internal_string_set_interned(vm->special_getattr_key, 1);
    tinypy_internal_string_set_interned(vm->special_get_key, 1);
    tinypy_internal_string_set_interned(vm->special_set_key, 1);
    tinypy_internal_string_set_interned(vm->special_delete_key, 1);
    tinypy_internal_string_set_interned(vm->special_call_key, 1);
    tinypy_internal_string_set_interned(vm->special_iter_key, 1);
    tinypy_internal_string_set_interned(vm->special_length_key, 1);
    tinypy_internal_string_set_interned(vm->special_getitem_key, 1);
    tinypy_internal_string_set_interned(vm->special_setitem_key, 1);
    tinypy_internal_string_set_interned(vm->special_delitem_key, 1);
    tinypy_internal_string_set_interned(vm->special_contains_key, 1);
    tinypy_internal_string_set_interned(vm->special_index_key, 1);
    tinypy_internal_string_set_interned(vm->special_hash_key, 1);
    tinypy_internal_string_set_interned(vm->special_repr_key, 1);
    tinypy_internal_string_set_interned(vm->special_str_key, 1);
    tinypy_internal_string_set_interned(vm->special_nonzero_key, 1);
    tinypy_internal_string_set_interned(vm->special_enter_key, 1);
    tinypy_internal_string_set_interned(vm->special_exit_key, 1);
    tinypy_internal_string_set_interned(vm->special_format_key, 1);
    tinypy_internal_string_set_interned(vm->special_new_key, 1);
    tinypy_internal_string_set_interned(vm->special_init_key, 1);
    tinypy_internal_string_set_interned(vm->special_name_key, 1);

    __tinypy_internal_initialize_type_dicts(vm);
    __tinypy_internal_initialize_type_docs(vm);
    vm->interned_strings = tinypy_dict_new(vm);
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
    __tinypy_internal_builtin_set(vm, "xrange", 6U, &vm->types[TINYPY_VALUE_XRANGE].base.base);
    __tinypy_internal_builtin_set(vm, "enumerate", 9U, &vm->types[TINYPY_VALUE_ENUMERATE].base.base);
    __tinypy_internal_builtin_set(vm, "reversed", 8U, &vm->types[TINYPY_VALUE_REVERSED].base.base);
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
static void __tinypy_shutdown_collect(tinypy_shutdown_graph_t *graph) {
    tinypy_vm_t *vm = graph->vm;
    size_t index;

    __tinypy_shutdown_add(graph, vm->modules);
    __tinypy_shutdown_add(graph, vm->builtins);
    __tinypy_shutdown_add(graph, vm->interned_strings);
    __tinypy_shutdown_add(graph, vm->builtins_key);
    __tinypy_shutdown_add(graph, vm->keys_key);
    __tinypy_shutdown_add(graph, vm->key_key);
    __tinypy_shutdown_add(graph, vm->softspace_key);
    __tinypy_shutdown_add(graph, vm->sys_key);
    __tinypy_shutdown_add(graph, vm->metaclass_key);
    __tinypy_shutdown_add(graph, vm->special_getattribute_key);
    __tinypy_shutdown_add(graph, vm->special_getattr_key);
    __tinypy_shutdown_add(graph, vm->special_get_key);
    __tinypy_shutdown_add(graph, vm->special_set_key);
    __tinypy_shutdown_add(graph, vm->special_delete_key);
    __tinypy_shutdown_add(graph, vm->special_call_key);
    __tinypy_shutdown_add(graph, vm->special_iter_key);
    __tinypy_shutdown_add(graph, vm->special_length_key);
    __tinypy_shutdown_add(graph, vm->special_getitem_key);
    __tinypy_shutdown_add(graph, vm->special_setitem_key);
    __tinypy_shutdown_add(graph, vm->special_delitem_key);
    __tinypy_shutdown_add(graph, vm->special_contains_key);
    __tinypy_shutdown_add(graph, vm->special_index_key);
    __tinypy_shutdown_add(graph, vm->special_hash_key);
    __tinypy_shutdown_add(graph, vm->special_repr_key);
    __tinypy_shutdown_add(graph, vm->special_str_key);
    __tinypy_shutdown_add(graph, vm->special_nonzero_key);
    __tinypy_shutdown_add(graph, vm->special_enter_key);
    __tinypy_shutdown_add(graph, vm->special_exit_key);
    __tinypy_shutdown_add(graph, vm->special_format_key);
    __tinypy_shutdown_add(graph, vm->special_new_key);
    __tinypy_shutdown_add(graph, vm->special_init_key);
    __tinypy_shutdown_add(graph, vm->special_name_key);
    __tinypy_shutdown_add(graph, vm->module_finder);
    __tinypy_shutdown_add(graph, vm->raised_type);
    __tinypy_shutdown_add(graph, vm->raised_value);
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
        if (graph->entries[index].kind == TINYPY_VALUE_WEAKREF) {
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
    __tinypy_shutdown_collect(&graph);
    for (index = 0U; index < graph.entry_count; ++index) {
        visit(graph.entries[index].value, user_data);
    }
    __tinypy_shutdown_graph_destroy(&graph);
}
//////////////////////////////////////////////////////////////////////////
#endif
void tinypy_vm_destroy(tinypy_vm_t *vm) {
    tinypy_allocator_t allocator;
    tinypy_shutdown_graph_t graph;
    size_t type_index;

    vm->state = TINYPY_VM_STATE_DESTROYING;
    tinypy_internal_type_lookup_cache_finalize(vm);
    tinypy_internal_integer_free_list_finalize(vm);
    tinypy_internal_frame_free_list_finalize(vm);
    tinypy_internal_method_free_list_finalize(vm);
    (void)memset(&graph, 0, sizeof(graph));
    graph.vm = vm;
    __tinypy_shutdown_collect(&graph);
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
