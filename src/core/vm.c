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
    type->destroy = destroy;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_types(tinypy_vm_t *vm) {
    __tinypy_internal_initialize_type(
        vm, &vm->type_type, &vm->type_type, "type", 4U,
        sizeof(tinypy_type_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE | TINYPY_TYPE_FLAG_TYPE_SUBCLASS,
        &vm->object_type,
        tinypy_internal_type_release_references,
        tinypy_internal_type_destroy);
    vm->type_type.call = tinypy_internal_type_call;
    vm->type_type.create = tinypy_internal_type_create;
    __tinypy_internal_initialize_type(
        vm, &vm->object_type, &vm->type_type, "object", 6U,
        sizeof(tinypy_value_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        NULL, NULL, NULL);
    vm->object_type.create = tinypy_internal_object_create;
    __tinypy_internal_initialize_type(
        vm, &vm->none_type, &vm->type_type, "NoneType", 8U,
        sizeof(tinypy_none_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type, NULL, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->not_implemented_type, &vm->type_type, "NotImplementedType", 18U,
        sizeof(tinypy_none_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type, NULL, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->basestring_type, &vm->type_type, "basestring", 10U,
        sizeof(tinypy_value_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->object_type, NULL, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->bool_type, &vm->type_type, "bool", 4U,
        sizeof(tinypy_integer_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->integer_type, NULL, NULL);
    vm->bool_type.create = tinypy_internal_bool_create;
    __tinypy_internal_initialize_type(
        vm, &vm->integer_type, &vm->type_type, "int", 3U,
        sizeof(tinypy_integer_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->object_type, NULL, NULL);
    vm->integer_type.create = tinypy_internal_integer_create;
    __tinypy_internal_initialize_type(
        vm, &vm->string_type, &vm->type_type, "str", 3U,
        offsetof(tinypy_string_object_t, bytes), 1U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->basestring_type, NULL, NULL);
    vm->string_type.create = tinypy_internal_string_create;
    __tinypy_internal_initialize_type(
        vm, &vm->unicode_type, &vm->type_type, "unicode", 7U,
        offsetof(tinypy_unicode_object_t, utf8), 1U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->basestring_type, NULL, NULL);
    vm->unicode_type.create = tinypy_internal_unicode_create;
    __tinypy_internal_initialize_type(
        vm, &vm->long_type, &vm->type_type, "long", 4U,
        offsetof(tinypy_long_object_t, digits), sizeof(uint16_t),
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->object_type, NULL, NULL);
    vm->long_type.create = tinypy_internal_long_create;
    __tinypy_internal_initialize_type(
        vm, &vm->float_type, &vm->type_type, "float", 5U,
        sizeof(tinypy_float_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->object_type, NULL, NULL);
    vm->float_type.create = tinypy_internal_float_create;
    __tinypy_internal_initialize_type(
        vm, &vm->complex_type, &vm->type_type, "complex", 7U,
        sizeof(tinypy_complex_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->object_type, NULL, NULL);
    vm->complex_type.create = tinypy_internal_complex_create;
    __tinypy_internal_initialize_type(
        vm, &vm->tuple_type, &vm->type_type, "tuple", 5U,
        offsetof(tinypy_tuple_object_t, items),
        sizeof(tinypy_value_t *),
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->object_type, tinypy_internal_tuple_release_references, NULL);
    vm->tuple_type.create = tinypy_internal_tuple_create;
    __tinypy_internal_initialize_type(
        vm, &vm->list_type, &vm->type_type, "list", 4U,
        sizeof(tinypy_list_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->object_type,
        tinypy_internal_list_release_references, tinypy_internal_list_destroy);
    vm->list_type.create = tinypy_internal_list_create;
    __tinypy_internal_initialize_type(
        vm, &vm->dict_type, &vm->type_type, "dict", 4U,
        sizeof(tinypy_dict_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->object_type,
        tinypy_internal_dict_release_references, tinypy_internal_dict_destroy);
    vm->dict_type.create = tinypy_internal_dict_create;
    __tinypy_internal_initialize_type(
        vm, &vm->set_type, &vm->type_type, "set", 3U,
        sizeof(tinypy_set_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->object_type,
        tinypy_internal_set_release_references, NULL);
    vm->set_type.iter = tinypy_internal_set_iter;
    vm->set_type.create = tinypy_internal_set_create;
    __tinypy_internal_initialize_type(
        vm, &vm->frozenset_type, &vm->type_type, "frozenset", 9U,
        sizeof(tinypy_set_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE, &vm->object_type,
        tinypy_internal_set_release_references, NULL);
    vm->frozenset_type.iter = tinypy_internal_set_iter;
    vm->frozenset_type.create = tinypy_internal_frozenset_create;
    __tinypy_internal_initialize_type(
        vm, &vm->output_stream_type, &vm->type_type, "tinypy.output", 13U,
        sizeof(tinypy_output_stream_object_t), 0U,
        0U, &vm->object_type, NULL, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->code_type, &vm->type_type, "code", 4U,
        sizeof(tinypy_code_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type,
        tinypy_internal_code_release_references, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->frame_type, &vm->type_type, "frame", 5U,
        offsetof(tinypy_frame_object_t, locals_plus), sizeof(tinypy_value_t *),
        0U, &vm->object_type,
        tinypy_internal_frame_release_references, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->function_type, &vm->type_type, "function", 8U,
        sizeof(tinypy_function_object_t), 0U,
        0U, &vm->object_type,
        tinypy_internal_function_release_references, NULL);
    vm->function_type.call = tinypy_internal_function_call;
    vm->function_type.descriptor_get = tinypy_internal_function_descriptor_get;
    __tinypy_internal_initialize_type(
        vm, &vm->iterator_type, &vm->type_type, "iterator", 8U,
        sizeof(tinypy_iterator_object_t), 0U,
        0U, &vm->object_type,
        tinypy_internal_iterator_release_references, NULL);
    vm->iterator_type.iter = tinypy_internal_iterator_iter;
    vm->iterator_type.next = tinypy_internal_iterator_next;
    __tinypy_internal_initialize_type(
        vm, &vm->method_type, &vm->type_type, "instancemethod", 14U,
        sizeof(tinypy_method_object_t), 0U,
        0U, &vm->object_type,
        tinypy_internal_method_release_references, NULL);
    vm->method_type.call = tinypy_internal_method_call;
    __tinypy_internal_initialize_type(
        vm, &vm->cell_type, &vm->type_type, "cell", 4U,
        sizeof(tinypy_cell_object_t), 0U,
        0U, &vm->object_type,
        tinypy_internal_cell_release_references, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->slice_type, &vm->type_type, "slice", 5U,
        sizeof(tinypy_slice_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type,
        tinypy_internal_slice_release_references, NULL);
    vm->slice_type.create = tinypy_internal_slice_create;
    __tinypy_internal_initialize_type(
        vm, &vm->module_type, &vm->type_type, "module", 6U,
        sizeof(tinypy_module_object_t), 0U,
        0U, &vm->object_type,
        tinypy_internal_module_release_references, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->native_function_type, &vm->type_type, "builtin_function_or_method", 26U,
        sizeof(tinypy_native_function_object_t), 0U,
        0U, &vm->object_type,
        tinypy_internal_native_function_release_references,
        tinypy_internal_native_function_destroy);
    vm->native_function_type.call = tinypy_internal_native_function_call;
    vm->native_function_type.descriptor_get = tinypy_internal_function_descriptor_get;
    __tinypy_internal_initialize_type(
        vm, &vm->static_method_type, &vm->type_type, "staticmethod", 12U,
        sizeof(tinypy_callable_descriptor_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->object_type,
        tinypy_internal_callable_descriptor_release_references, NULL);
    vm->static_method_type.descriptor_get = tinypy_internal_static_method_get;
    vm->static_method_type.create = tinypy_internal_static_method_create;
    __tinypy_internal_initialize_type(
        vm, &vm->class_method_type, &vm->type_type, "classmethod", 11U,
        sizeof(tinypy_callable_descriptor_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->object_type,
        tinypy_internal_callable_descriptor_release_references, NULL);
    vm->class_method_type.descriptor_get = tinypy_internal_class_method_get;
    vm->class_method_type.create = tinypy_internal_class_method_create;
    __tinypy_internal_initialize_type(
        vm, &vm->property_type, &vm->type_type, "property", 8U,
        sizeof(tinypy_property_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->object_type,
        tinypy_internal_property_release_references, NULL);
    vm->property_type.descriptor_get = tinypy_internal_property_get;
    vm->property_type.descriptor_set = tinypy_internal_property_set;
    vm->property_type.create = tinypy_internal_property_create;
    __tinypy_internal_initialize_type(
        vm, &vm->super_type, &vm->type_type, "super", 5U,
        sizeof(tinypy_super_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->object_type,
        tinypy_internal_super_release_references, NULL);
    vm->super_type.get_attribute = tinypy_internal_super_get_attribute;
    vm->super_type.create = tinypy_internal_super_create;
    __tinypy_internal_initialize_type(
        vm, &vm->traceback_type, &vm->type_type, "traceback", 9U,
        sizeof(tinypy_traceback_object_t), 0U,
        0U, &vm->object_type,
        tinypy_internal_traceback_release_references, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->generator_type, &vm->type_type, "generator", 9U,
        sizeof(tinypy_generator_object_t), 0U,
        0U, &vm->object_type,
        tinypy_internal_generator_release_references, NULL);
    vm->generator_type.iter = tinypy_internal_generator_iter;
    vm->generator_type.next = tinypy_internal_generator_next;
    __tinypy_internal_initialize_type(
        vm, &vm->xrange_type, &vm->type_type, "xrange", 6U,
        sizeof(tinypy_xrange_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type, NULL, NULL);
    vm->xrange_type.iter = tinypy_internal_xrange_iter;
    vm->xrange_type.create = tinypy_internal_xrange_create;
    __tinypy_internal_initialize_type(
        vm, &vm->enumerate_type, &vm->type_type, "enumerate", 9U,
        sizeof(tinypy_enumerate_object_t), 0U,
        0U, &vm->object_type,
        tinypy_internal_enumerate_release_references, NULL);
    vm->enumerate_type.iter = tinypy_internal_enumerate_iter;
    vm->enumerate_type.next = tinypy_internal_enumerate_next;
    __tinypy_internal_initialize_type(
        vm, &vm->reversed_type, &vm->type_type, "reversed", 8U,
        sizeof(tinypy_reversed_object_t), 0U,
        0U, &vm->object_type,
        tinypy_internal_reversed_release_references, NULL);
    vm->reversed_type.iter = tinypy_internal_reversed_iter;
    vm->reversed_type.next = tinypy_internal_reversed_next;
    __tinypy_internal_initialize_type(
        vm, &vm->buffer_type, &vm->type_type, "buffer", 6U,
        sizeof(tinypy_buffer_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type,
        tinypy_internal_buffer_release_references, NULL);
    (void)memset(&vm->buffer_sequence_slots, 0, sizeof(vm->buffer_sequence_slots));
    vm->buffer_sequence_slots.length = tinypy_internal_buffer_length;
    (void)memset(&vm->buffer_mapping_slots, 0, sizeof(vm->buffer_mapping_slots));
    vm->buffer_mapping_slots.length = tinypy_internal_buffer_length;
    vm->buffer_mapping_slots.get_item = tinypy_internal_buffer_get_item;
    vm->buffer_type.sequence_slots = &vm->buffer_sequence_slots;
    vm->buffer_type.mapping_slots = &vm->buffer_mapping_slots;
    vm->buffer_type.repr = tinypy_internal_buffer_repr;
    vm->buffer_type.string = tinypy_internal_buffer_string;
    vm->buffer_type.create = tinypy_internal_buffer_create;
    __tinypy_internal_initialize_type(
        vm, &vm->bytearray_type, &vm->type_type, "bytearray", 9U,
        sizeof(tinypy_bytearray_object_t), 0U,
        TINYPY_TYPE_FLAG_BASE_TYPE, &vm->object_type,
        NULL, tinypy_internal_bytearray_destroy);
    (void)memset(&vm->bytearray_sequence_slots, 0, sizeof(vm->bytearray_sequence_slots));
    vm->bytearray_sequence_slots.length = tinypy_internal_bytearray_length;
    (void)memset(&vm->bytearray_mapping_slots, 0, sizeof(vm->bytearray_mapping_slots));
    vm->bytearray_mapping_slots.length = tinypy_internal_bytearray_length;
    vm->bytearray_mapping_slots.get_item = tinypy_internal_bytearray_get_item;
    vm->bytearray_mapping_slots.set_item = tinypy_internal_bytearray_set_item;
    vm->bytearray_type.sequence_slots = &vm->bytearray_sequence_slots;
    vm->bytearray_type.mapping_slots = &vm->bytearray_mapping_slots;
    vm->bytearray_type.repr = tinypy_internal_bytearray_repr;
    vm->bytearray_type.string = tinypy_internal_bytearray_string;
    vm->bytearray_type.create = tinypy_internal_bytearray_create;
    __tinypy_internal_initialize_type(
        vm, &vm->weakref_type, &vm->type_type, "weakref", 7U,
        sizeof(tinypy_weakref_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE, &vm->object_type,
        tinypy_internal_weakref_release_references, tinypy_internal_weakref_destroy);
    vm->weakref_type.layout_kind = TINYPY_VALUE_WEAKREF;
    vm->weakref_type.call = tinypy_internal_weakref_call;
    vm->weakref_type.create = tinypy_internal_weakref_create;
    __tinypy_internal_initialize_type(
        vm, &vm->dict_keys_type, &vm->type_type, "dict_keys", 9U,
        sizeof(tinypy_dict_view_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type,
        tinypy_internal_dict_view_release_references, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->dict_values_type, &vm->type_type, "dict_values", 11U,
        sizeof(tinypy_dict_view_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type,
        tinypy_internal_dict_view_release_references, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->dict_items_type, &vm->type_type, "dict_items", 10U,
        sizeof(tinypy_dict_view_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type,
        tinypy_internal_dict_view_release_references, NULL);
    (void)memset(&vm->dict_view_sequence_slots, 0, sizeof(vm->dict_view_sequence_slots));
    vm->dict_view_sequence_slots.length = tinypy_internal_dict_view_length;
    vm->dict_view_sequence_slots.contains = tinypy_internal_dict_view_contains;
    vm->dict_keys_type.sequence_slots = &vm->dict_view_sequence_slots;
    vm->dict_values_type.sequence_slots = &vm->dict_view_sequence_slots;
    vm->dict_items_type.sequence_slots = &vm->dict_view_sequence_slots;
    vm->dict_keys_type.iter = tinypy_internal_dict_view_iter;
    vm->dict_values_type.iter = tinypy_internal_dict_view_iter;
    vm->dict_items_type.iter = tinypy_internal_dict_view_iter;
    __tinypy_internal_initialize_type(
        vm, &vm->ellipsis_type, &vm->type_type, "ellipsis", 8U,
        sizeof(tinypy_none_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type, NULL, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->file_type, &vm->type_type, "file", 4U,
        sizeof(tinypy_file_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE | TINYPY_TYPE_FLAG_BASE_TYPE,
        &vm->object_type, NULL, NULL);
    __tinypy_internal_initialize_type(
        vm, &vm->getset_descriptor_type, &vm->type_type, "getset_descriptor", 17U,
        sizeof(tinypy_c_descriptor_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type,
        tinypy_internal_c_descriptor_release_references, NULL);
    vm->getset_descriptor_type.descriptor_get = tinypy_internal_c_descriptor_get;
    vm->getset_descriptor_type.descriptor_set = tinypy_internal_c_descriptor_set;
    __tinypy_internal_initialize_type(
        vm, &vm->member_descriptor_type, &vm->type_type, "member_descriptor", 17U,
        sizeof(tinypy_c_descriptor_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type,
        tinypy_internal_c_descriptor_release_references, NULL);
    vm->member_descriptor_type.descriptor_get = tinypy_internal_c_descriptor_get;
    vm->member_descriptor_type.descriptor_set = tinypy_internal_c_descriptor_set;
    __tinypy_internal_initialize_type(
        vm, &vm->class_type, &vm->type_type, "classobj", 8U,
        sizeof(tinypy_class_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type,
        tinypy_internal_class_release_references, NULL);
    vm->class_type.call = tinypy_internal_class_call;
    vm->class_type.get_attribute = tinypy_internal_class_get_attribute;
    vm->class_type.set_attribute = tinypy_internal_class_set_attribute;
    __tinypy_internal_initialize_type(
        vm, &vm->old_instance_type, &vm->type_type, "instance", 8U,
        sizeof(tinypy_old_instance_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type,
        tinypy_internal_old_instance_release_references, NULL);
    vm->old_instance_type.get_attribute = tinypy_internal_old_instance_get_attribute;
    vm->old_instance_type.set_attribute = tinypy_internal_old_instance_set_attribute;
    __tinypy_internal_initialize_type(
        vm, &vm->partial_type, &vm->type_type, "functools.partial", 17U,
        sizeof(tinypy_partial_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type,
        tinypy_internal_partial_release_references, NULL);
    vm->partial_type.call = tinypy_internal_partial_call;
    vm->partial_type.create = tinypy_internal_partial_create;
    vm->partial_type.has_instance_dict = INT32_C(1);
    vm->partial_type.dict_offset = offsetof(tinypy_partial_object_t, dict);
    vm->partial_type.weakref_offset = offsetof(tinypy_partial_object_t, weakrefs);
    __tinypy_internal_initialize_type(
        vm, &vm->sre_pattern_type, &vm->type_type, "_sre.SRE_Pattern", 16U,
        sizeof(tinypy_sre_pattern_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type,
        tinypy_internal_sre_pattern_release_references, tinypy_internal_sre_pattern_destroy);
    vm->sre_pattern_type.weakref_offset = offsetof(tinypy_sre_pattern_object_t, weakrefs);
    __tinypy_internal_initialize_type(
        vm, &vm->sre_match_type, &vm->type_type, "_sre.SRE_Match", 14U,
        sizeof(tinypy_sre_match_object_t), 0U,
        TINYPY_TYPE_FLAG_IMMUTABLE, &vm->object_type,
        tinypy_internal_sre_match_release_references, tinypy_internal_sre_match_destroy);
    vm->type_type.layout_kind = TINYPY_VALUE_TYPE;
    vm->type_type.weakref_offset = offsetof(tinypy_type_t, weakrefs);
    vm->object_type.layout_kind = TINYPY_VALUE_INSTANCE;
    vm->none_type.layout_kind = TINYPY_VALUE_NONE;
    vm->not_implemented_type.layout_kind = TINYPY_VALUE_NOT_IMPLEMENTED;
    vm->bool_type.layout_kind = TINYPY_VALUE_BOOL;
    vm->integer_type.layout_kind = TINYPY_VALUE_INTEGER;
    vm->string_type.layout_kind = TINYPY_VALUE_STRING;
    vm->unicode_type.layout_kind = TINYPY_VALUE_UNICODE;
    vm->long_type.layout_kind = TINYPY_VALUE_LONG;
    vm->float_type.layout_kind = TINYPY_VALUE_FLOAT;
    vm->complex_type.layout_kind = TINYPY_VALUE_COMPLEX;
    vm->tuple_type.layout_kind = TINYPY_VALUE_TUPLE;
    vm->list_type.layout_kind = TINYPY_VALUE_LIST;
    vm->dict_type.layout_kind = TINYPY_VALUE_DICT;
    vm->set_type.layout_kind = TINYPY_VALUE_SET;
    vm->frozenset_type.layout_kind = TINYPY_VALUE_FROZENSET;
    vm->output_stream_type.layout_kind = TINYPY_VALUE_OUTPUT_STREAM;
    vm->code_type.layout_kind = TINYPY_VALUE_CODE;
    vm->frame_type.layout_kind = TINYPY_VALUE_FRAME;
    vm->function_type.layout_kind = TINYPY_VALUE_FUNCTION;
    vm->iterator_type.layout_kind = TINYPY_VALUE_ITERATOR;
    vm->method_type.layout_kind = TINYPY_VALUE_METHOD;
    vm->cell_type.layout_kind = TINYPY_VALUE_CELL;
    vm->slice_type.layout_kind = TINYPY_VALUE_SLICE;
    vm->module_type.layout_kind = TINYPY_VALUE_MODULE;
    vm->native_function_type.layout_kind = TINYPY_VALUE_NATIVE_FUNCTION;
    vm->static_method_type.layout_kind = TINYPY_VALUE_STATIC_METHOD;
    vm->class_method_type.layout_kind = TINYPY_VALUE_CLASS_METHOD;
    vm->property_type.layout_kind = TINYPY_VALUE_PROPERTY;
    vm->super_type.layout_kind = TINYPY_VALUE_SUPER;
    vm->traceback_type.layout_kind = TINYPY_VALUE_TRACEBACK;
    vm->generator_type.layout_kind = TINYPY_VALUE_GENERATOR;
    vm->xrange_type.layout_kind = TINYPY_VALUE_XRANGE;
    vm->enumerate_type.layout_kind = TINYPY_VALUE_ENUMERATE;
    vm->reversed_type.layout_kind = TINYPY_VALUE_REVERSED;
    vm->buffer_type.layout_kind = TINYPY_VALUE_BUFFER;
    vm->bytearray_type.layout_kind = TINYPY_VALUE_BYTEARRAY;
    vm->weakref_type.layout_kind = TINYPY_VALUE_WEAKREF;
    vm->dict_keys_type.layout_kind = TINYPY_VALUE_DICT_KEYS;
    vm->dict_values_type.layout_kind = TINYPY_VALUE_DICT_VALUES;
    vm->dict_items_type.layout_kind = TINYPY_VALUE_DICT_ITEMS;
    vm->ellipsis_type.layout_kind = TINYPY_VALUE_ELLIPSIS;
    vm->file_type.layout_kind = TINYPY_VALUE_FILE;
    vm->getset_descriptor_type.layout_kind = TINYPY_VALUE_GETSET_DESCRIPTOR;
    vm->member_descriptor_type.layout_kind = TINYPY_VALUE_MEMBER_DESCRIPTOR;
    vm->class_type.layout_kind = TINYPY_VALUE_CLASS;
    vm->class_type.weakref_offset = offsetof(tinypy_class_object_t, weakrefs);
    vm->old_instance_type.layout_kind = TINYPY_VALUE_OLD_INSTANCE;
    vm->old_instance_type.weakref_offset = offsetof(tinypy_old_instance_object_t, weakrefs);
    vm->partial_type.layout_kind = TINYPY_VALUE_PARTIAL;
    vm->sre_pattern_type.layout_kind = TINYPY_VALUE_SRE_PATTERN;
    vm->sre_match_type.layout_kind = TINYPY_VALUE_SRE_MATCH;
    vm->object_type.slots_offset = offsetof(tinypy_instance_object_t, slots);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_initialize_type_dicts(tinypy_vm_t *vm) {
    tinypy_type_t *types[] = {
        &vm->type_type,
        &vm->object_type,
        &vm->none_type,
        &vm->not_implemented_type,
        &vm->basestring_type,
        &vm->bool_type,
        &vm->integer_type,
        &vm->string_type,
        &vm->unicode_type,
        &vm->long_type,
        &vm->float_type,
        &vm->complex_type,
        &vm->tuple_type,
        &vm->list_type,
        &vm->dict_type,
        &vm->set_type,
        &vm->frozenset_type,
        &vm->output_stream_type,
        &vm->code_type,
        &vm->frame_type,
        &vm->function_type,
        &vm->iterator_type,
        &vm->method_type,
        &vm->cell_type,
        &vm->slice_type,
        &vm->module_type,
        &vm->native_function_type,
        &vm->static_method_type,
        &vm->class_method_type,
        &vm->property_type,
        &vm->super_type,
        &vm->traceback_type,
        &vm->generator_type,
        &vm->xrange_type,
        &vm->enumerate_type,
        &vm->reversed_type,
        &vm->buffer_type,
        &vm->bytearray_type,
        &vm->weakref_type,
        &vm->dict_keys_type,
        &vm->dict_values_type,
        &vm->dict_items_type,
        &vm->ellipsis_type,
        &vm->file_type,
        &vm->getset_descriptor_type,
        &vm->member_descriptor_type,
        &vm->class_type,
        &vm->old_instance_type,
        &vm->partial_type,
        &vm->sre_pattern_type,
        &vm->sre_match_type};
    size_t index;

    for (index = 0U; index < sizeof(types) / sizeof(types[0]); ++index) {
        tinypy_dict_object_t *dict = &vm->builtin_type_dicts[index];

        dict->base.ref = 1U;
        dict->base.type = &vm->dict_type;
        dict->mask = TINYPY_DICT_MIN_SIZE - 1U;
        dict->table = dict->small_table;
        dict->type_dictionary = INT32_C(1);
        types[index]->dict = &dict->base;
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
    __tinypy_internal_builtin_set(vm, "object", 6U, &vm->object_type.base.base);
    __tinypy_internal_builtin_set(vm, "type", 4U, &vm->type_type.base.base);
    __tinypy_internal_builtin_set(vm, "bool", 4U, &vm->bool_type.base.base);
    __tinypy_internal_builtin_set(vm, "int", 3U, &vm->integer_type.base.base);
    __tinypy_internal_builtin_set(vm, "long", 4U, &vm->long_type.base.base);
    __tinypy_internal_builtin_set(vm, "float", 5U, &vm->float_type.base.base);
    __tinypy_internal_builtin_set(vm, "complex", 7U, &vm->complex_type.base.base);
    __tinypy_internal_builtin_set(vm, "str", 3U, &vm->string_type.base.base);
    __tinypy_internal_builtin_set(vm, "bytes", 5U, &vm->string_type.base.base);
    __tinypy_internal_builtin_set(vm, "basestring", 10U, &vm->basestring_type.base.base);
    __tinypy_internal_builtin_set(vm, "unicode", 7U, &vm->unicode_type.base.base);
    __tinypy_internal_builtin_set(vm, "tuple", 5U, &vm->tuple_type.base.base);
    __tinypy_internal_builtin_set(vm, "list", 4U, &vm->list_type.base.base);
    __tinypy_internal_builtin_set(vm, "dict", 4U, &vm->dict_type.base.base);
    __tinypy_internal_builtin_set(vm, "set", 3U, &vm->set_type.base.base);
    __tinypy_internal_builtin_set(vm, "frozenset", 9U, &vm->frozenset_type.base.base);
    __tinypy_internal_builtin_set(vm, "slice", 5U, &vm->slice_type.base.base);
    __tinypy_internal_builtin_set(vm, "staticmethod", 12U, &vm->static_method_type.base.base);
    __tinypy_internal_builtin_set(vm, "classmethod", 11U, &vm->class_method_type.base.base);
    __tinypy_internal_builtin_set(vm, "property", 8U, &vm->property_type.base.base);
    __tinypy_internal_builtin_set(vm, "super", 5U, &vm->super_type.base.base);
    __tinypy_internal_builtin_set(vm, "buffer", 6U, &vm->buffer_type.base.base);
    __tinypy_internal_builtin_set(vm, "bytearray", 9U, &vm->bytearray_type.base.base);
    __tinypy_internal_builtin_set(vm, "file", 4U, &vm->file_type.base.base);
    TINYPY_DECREF(false_value);
    TINYPY_DECREF(true_value);
    TINYPY_DECREF(none_value);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_register_module(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *module) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(vm->modules, key, module);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_sys_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "sys function received invalid arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
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
        return tinypy_tuple_from_items(vm, items, 3U);
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
    return tinypy_none_get(vm);
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
    assert(vm->recursion_limit <= (size_t)INT64_MAX);
    return tinypy_integer_from_i64(vm, (int64_t)vm->recursion_limit);
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
    return tinypy_none_get(vm);
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
    name = tinypy_string_from_bytes(vm, *((const unsigned char *)&byteorder_probe) == 1U ? "little" : "big", *((const unsigned char *)&byteorder_probe) == 1U ? 6U : 3U);
    tinypy_module_add_value(sys_module, "byteorder", 9U, name);
    TINYPY_DECREF(name);
    name = tinypy_integer_from_i64(vm, INT64_C(0x020712f0));
    tinypy_module_add_value(sys_module, "hexversion", 10U, name);
    TINYPY_DECREF(name);
    name = tinypy_integer_from_i64(vm, INT64_MAX);
    tinypy_module_add_value(sys_module, "maxint", 6U, name);
    TINYPY_DECREF(name);
    tinypy_module_add_value(sys_module, "py3kwarning", 11U, &vm->false_object.base);
    TINYPY_DECREF(stderr_value);
    TINYPY_DECREF(stdout_value);
    __tinypy_internal_sys_add_function(vm, sys_module, "exc_info", 8U, __tinypy_internal_sys_exc_info);
    __tinypy_internal_sys_add_function(vm, sys_module, "exc_clear", 9U, __tinypy_internal_sys_exc_clear);
    __tinypy_internal_sys_add_function(vm, sys_module, "_getframe", 9U, __tinypy_internal_sys_getframe);
    __tinypy_internal_sys_add_function(vm, sys_module, "getrecursionlimit", 17U, __tinypy_internal_sys_getrecursionlimit);
    __tinypy_internal_sys_add_function(vm, sys_module, "setrecursionlimit", 17U, __tinypy_internal_sys_setrecursionlimit);
    tinypy_internal_register_module(vm, "sys", 3U, sys_module);
    future_module = tinypy_module_new(vm, "__future__", 10U);
    name = tinypy_string_from_bytes(vm, "__future__", 10U);
    tinypy_module_add_value(future_module, "__name__", 8U, name);
    TINYPY_DECREF(name);
    tinypy_module_add_value(future_module, "nested_scopes", 13U, &vm->none_object.base);
    tinypy_module_add_value(future_module, "generators", 10U, &vm->none_object.base);
    tinypy_module_add_value(future_module, "division", 8U, &vm->none_object.base);
    tinypy_module_add_value(future_module, "absolute_import", 15U, &vm->none_object.base);
    tinypy_module_add_value(future_module, "with_statement", 14U, &vm->none_object.base);
    tinypy_module_add_value(future_module, "print_function", 14U, &vm->none_object.base);
    tinypy_module_add_value(future_module, "unicode_literals", 16U, &vm->none_object.base);
    tinypy_internal_register_module(vm, "__future__", 10U, future_module);
    tinypy_internal_initialize_weakref_module(vm);
    tinypy_internal_initialize_codecs_module(vm);
    tinypy_internal_initialize_functools_module(vm);
    tinypy_internal_initialize_struct_module(vm);
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
int tinypy_internal_host_valid(const tinypy_host_t *host) {
    if (host == NULL) {
        return 1;
    }

    return host->abi_version == TINYPY_ABI_VERSION && host->struct_size >= (uint32_t)sizeof(*host) && (host->resolve_module == NULL || host->release_module_artifact != NULL);
}
//////////////////////////////////////////////////////////////////////////
int tinypy_internal_vm_valid(const tinypy_vm_t *vm) {
    return vm != NULL && vm->state == TINYPY_VM_STATE_LIVE;
}
//////////////////////////////////////////////////////////////////////////
void *tinypy_internal_vm_allocate(tinypy_vm_t *vm, size_t size, uint32_t tag) {
    return tinypy_internal_pool_allocate(vm, size, tag);
}
//////////////////////////////////////////////////////////////////////////
void *tinypy_internal_vm_reallocate(tinypy_vm_t *vm, void *memory, size_t old_size, size_t new_size, uint32_t tag) {
    return tinypy_internal_pool_reallocate(vm, memory, old_size, new_size, tag);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_vm_deallocate(tinypy_vm_t *vm, void *memory, size_t size, uint32_t tag) {
    tinypy_internal_pool_deallocate(vm, memory, size, tag);
}
//////////////////////////////////////////////////////////////////////////
tinypy_vm_t *tinypy_vm_create(const tinypy_vm_config_t *config) {
    assert(config != NULL);
    assert(config->abi_version == TINYPY_ABI_VERSION);
    assert(config->struct_size >= (uint32_t)offsetof(tinypy_vm_config_t, optimize_level));

    const tinypy_allocator_t *allocator = config->allocator;
    assert(allocator != NULL);
    assert(allocator->abi_version == TINYPY_ABI_VERSION);
    assert(allocator->struct_size >= (uint32_t)sizeof(*allocator));
    assert(allocator->allocate != NULL);
    assert(allocator->reallocate != NULL);
    assert(allocator->deallocate != NULL);

    assert(tinypy_internal_host_valid(config->host));

    assert(config->max_heap_bytes == 0U || config->max_heap_bytes >= sizeof(tinypy_vm_t));

    tinypy_vm_t *vm = (tinypy_vm_t *)allocator->allocate(
        allocator->user_data,
        sizeof(*vm),
        TINYPY_INTERNAL_ALIGNMENT,
        (uint32_t)TINYPY_ALLOC_TAG_VM);

    assert(vm != NULL);

    (void)memset(vm, 0, sizeof(*vm));
    vm->state = TINYPY_VM_STATE_LIVE;
    vm->allocator = *allocator;
    vm->max_heap_bytes = config->max_heap_bytes;
    vm->allocated_bytes = sizeof(*vm);
    vm->type_lookup_cache_epoch = UINT64_C(1);
    vm->recursion_limit = 1000U;
    vm->optimize_level = config->struct_size >= (uint32_t)sizeof(*config) ? config->optimize_level : 0;
    assert(vm->optimize_level >= 0 && vm->optimize_level <= 2);
    tinypy_internal_pool_initialize(vm);

    if (config->host != NULL) {
        vm->host = *config->host;
        vm->has_host = 1;
    }

    __tinypy_internal_initialize_types(vm);
    __tinypy_internal_initialize_none(
        &vm->none_object,
        &vm->none_type);
    __tinypy_internal_initialize_none(
        &vm->not_implemented_object,
        &vm->not_implemented_type);
    __tinypy_internal_initialize_none(
        &vm->ellipsis_object,
        &vm->ellipsis_type);
    __tinypy_internal_initialize_integer(
        &vm->false_object,
        &vm->bool_type,
        INT64_C(0));
    __tinypy_internal_initialize_integer(
        &vm->true_object,
        &vm->bool_type,
        INT64_C(1));
    for (size_t integer_index = 0U;
         integer_index < TINYPY_INTEGER_CONSTANT_COUNT;
         ++integer_index) {
        __tinypy_internal_initialize_integer(
            &vm->integer_constants[integer_index],
            &vm->integer_type,
            TINYPY_INTEGER_CONSTANT_MIN + (int64_t)integer_index);
    }
    __tinypy_internal_initialize_float(
        &vm->float_zero_object,
        &vm->float_type,
        0.0);
    __tinypy_internal_initialize_empty_string(
        &vm->empty_string_object,
        &vm->string_type);
    __tinypy_internal_initialize_empty_tuple(
        &vm->empty_tuple_object,
        &vm->tuple_type);
    vm->builtins_key = tinypy_string_from_bytes(vm, "__builtins__", 12U);
    vm->special_getattribute_key = tinypy_string_from_bytes(vm, "__getattribute__", 16U);
    vm->special_getattr_key = tinypy_string_from_bytes(vm, "__getattr__", 11U);
    vm->special_get_key = tinypy_string_from_bytes(vm, "__get__", 7U);
    vm->special_set_key = tinypy_string_from_bytes(vm, "__set__", 7U);
    vm->special_delete_key = tinypy_string_from_bytes(vm, "__delete__", 10U);
    tinypy_internal_string_set_interned(vm->special_getattribute_key, 1);
    tinypy_internal_string_set_interned(vm->special_getattr_key, 1);
    tinypy_internal_string_set_interned(vm->special_get_key, 1);
    tinypy_internal_string_set_interned(vm->special_set_key, 1);
    tinypy_internal_string_set_interned(vm->special_delete_key, 1);

    __tinypy_internal_initialize_type_dicts(vm);
    tinypy_internal_initialize_container_types(vm);
    tinypy_internal_initialize_string_types(vm);
    tinypy_internal_initialize_representation_types(vm);
    tinypy_internal_initialize_bytearray_methods(vm);
    tinypy_internal_initialize_weakref_type(vm);
    tinypy_internal_initialize_constructor_types(vm);
    tinypy_internal_initialize_descriptor_types(vm);
    tinypy_internal_initialize_generator_types(vm);
    tinypy_internal_initialize_set_types(vm);
    tinypy_internal_initialize_output_type(vm);
    __tinypy_internal_initialize_builtins(vm);
    tinypy_internal_initialize_exceptions(vm);
    tinypy_internal_initialize_builtin_functions(vm);
    __tinypy_internal_builtin_set(vm, "xrange", 6U, &vm->xrange_type.base.base);
    __tinypy_internal_initialize_modules(vm);

    return vm;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_builtins(const tinypy_vm_t *vm) {
    assert(tinypy_internal_vm_valid(vm));
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
    assert(new_capacity > graph->entry_capacity);
    assert(new_capacity <= SIZE_MAX / sizeof(*graph->entries));
    old_size = graph->entry_capacity * sizeof(*graph->entries);
    new_size = new_capacity * sizeof(*graph->entries);
    tinypy_shutdown_entry_t *new_entries = (tinypy_shutdown_entry_t *)tinypy_internal_vm_allocate(graph->vm, new_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    if (graph->entries != NULL) {
        (void)memcpy(new_entries, graph->entries, graph->entry_count * sizeof(*new_entries));
        tinypy_internal_vm_deallocate(graph->vm, graph->entries, old_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    graph->entries = new_entries;
    graph->entry_capacity = new_capacity;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_shutdown_slots_rebuild(tinypy_shutdown_graph_t *graph, size_t new_capacity) {
    size_t *new_slots;
    size_t new_size;
    size_t index;

    assert(new_capacity >= 256U);
    assert((new_capacity & (new_capacity - 1U)) == 0U);
    assert(new_capacity <= SIZE_MAX / sizeof(*new_slots));
    new_size = new_capacity * sizeof(*new_slots);
    new_slots = (size_t *)tinypy_internal_vm_allocate(graph->vm, new_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    (void)memset(new_slots, 0, new_size);
    for (index = 0U; index < graph->entry_count; ++index) {
        size_t slot = __tinypy_shutdown_pointer_hash(graph->entries[index].value) & (new_capacity - 1U);

        while (new_slots[slot] != 0U) {
            slot = (slot + 1U) & (new_capacity - 1U);
        }
        new_slots[slot] = index + 1U;
    }
    if (graph->slots != NULL) {
        tinypy_internal_vm_deallocate(graph->vm, graph->slots, graph->slot_capacity * sizeof(*graph->slots), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
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
    assert(tinypy_internal_value_belongs_to(graph->vm, value));
    if (graph->slot_capacity == 0U) {
        __tinypy_shutdown_slots_rebuild(graph, 256U);
    }
    if (__tinypy_shutdown_find(graph, value) != SIZE_MAX) {
        return;
    }
    if ((graph->slot_count + 1U) * 3U >= graph->slot_capacity * 2U) {
        assert(graph->slot_capacity <= SIZE_MAX / 2U);
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
    tinypy_type_t *types[TINYPY_BUILTIN_TYPE_COUNT] = {
        &vm->type_type, &vm->object_type, &vm->none_type,
        &vm->not_implemented_type, &vm->basestring_type, &vm->bool_type,
        &vm->integer_type, &vm->string_type, &vm->unicode_type,
        &vm->long_type, &vm->float_type, &vm->complex_type,
        &vm->tuple_type, &vm->list_type, &vm->dict_type, &vm->set_type,
        &vm->frozenset_type, &vm->output_stream_type, &vm->code_type,
        &vm->frame_type, &vm->function_type, &vm->iterator_type,
        &vm->method_type, &vm->cell_type, &vm->slice_type, &vm->module_type,
        &vm->native_function_type, &vm->static_method_type,
        &vm->class_method_type, &vm->property_type, &vm->super_type,
        &vm->traceback_type, &vm->generator_type, &vm->xrange_type,
        &vm->enumerate_type, &vm->reversed_type, &vm->buffer_type,
        &vm->bytearray_type, &vm->weakref_type, &vm->dict_keys_type,
        &vm->dict_values_type, &vm->dict_items_type, &vm->ellipsis_type,
        &vm->file_type, &vm->getset_descriptor_type,
        &vm->member_descriptor_type, &vm->class_type,
        &vm->old_instance_type, &vm->partial_type,
        &vm->sre_pattern_type, &vm->sre_match_type};
    size_t index;

    __tinypy_shutdown_add(graph, vm->modules);
    __tinypy_shutdown_add(graph, vm->builtins);
    __tinypy_shutdown_add(graph, vm->builtins_key);
    __tinypy_shutdown_add(graph, vm->special_getattribute_key);
    __tinypy_shutdown_add(graph, vm->special_getattr_key);
    __tinypy_shutdown_add(graph, vm->special_get_key);
    __tinypy_shutdown_add(graph, vm->special_set_key);
    __tinypy_shutdown_add(graph, vm->special_delete_key);
    __tinypy_shutdown_add(graph, vm->module_finder);
    __tinypy_shutdown_add(graph, vm->raised_type);
    __tinypy_shutdown_add(graph, vm->raised_value);
    __tinypy_shutdown_add(graph, vm->raised_traceback);
    __tinypy_shutdown_add(graph, vm->handled_type);
    __tinypy_shutdown_add(graph, vm->handled_value);
    __tinypy_shutdown_add(graph, vm->handled_traceback);
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
        __tinypy_shutdown_add(graph, types[index]->name_object);
        __tinypy_shutdown_add(graph, types[index]->bases);
        __tinypy_shutdown_add(graph, types[index]->mro);
        __tinypy_shutdown_add(graph, types[index]->subclasses);
    }
    for (index = 0U; index < graph->entry_count; ++index) {
        tinypy_value_t *value = graph->entries[index].value;
        tinypy_type_t *type = value->type;

        __tinypy_shutdown_add(graph, &type->base.base);
        if (type->release_references != NULL) {
            type->release_references(value, __tinypy_shutdown_visit, graph);
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
    if (entry->destroy != NULL) {
        entry->destroy(entry->value);
    }
    tinypy_internal_vm_deallocate(graph->vm, entry->value, entry->allocation_size, (uint32_t)TINYPY_ALLOC_TAG_VALUE);
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
        tinypy_internal_vm_deallocate(graph->vm, graph->slots, graph->slot_capacity * sizeof(*graph->slots), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    if (graph->entries != NULL) {
        tinypy_internal_vm_deallocate(graph->vm, graph->entries, graph->entry_capacity * sizeof(*graph->entries), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_vm_destroy(tinypy_vm_t *vm) {
    tinypy_allocator_t allocator;
    tinypy_shutdown_graph_t graph;
    size_t type_index;

    assert(tinypy_internal_vm_valid(vm));

    tinypy_internal_type_lookup_cache_finalize(vm);
    tinypy_internal_integer_free_list_finalize(vm);
    tinypy_internal_frame_free_list_finalize(vm);
    tinypy_internal_method_free_list_finalize(vm);
    (void)memset(&graph, 0, sizeof(graph));
    graph.vm = vm;
    __tinypy_shutdown_collect(&graph);
    __tinypy_shutdown_destroy_graph(&graph);
    vm->state = TINYPY_VM_STATE_DESTROYING;
    for (type_index = 0U;
         type_index < TINYPY_BUILTIN_TYPE_COUNT;
         ++type_index) {
        tinypy_internal_dict_destroy(&vm->builtin_type_dicts[type_index].base);
    }
    __tinypy_shutdown_graph_destroy(&graph);
    tinypy_internal_pool_finalize(vm);
    assert(vm->allocated_bytes == sizeof(*vm));

    allocator = vm->allocator;
    allocator.deallocate(
        allocator.user_data,
        vm,
        sizeof(*vm),
        TINYPY_INTERNAL_ALIGNMENT,
        (uint32_t)TINYPY_ALLOC_TAG_VM);
}
