#ifndef TINYPY_CORE_INTERNAL_H
#define TINYPY_CORE_INTERNAL_H

#include "tinypy/dict.h"
#include "tinypy/dict_view.h"
#include "tinypy/buffer.h"
#include "tinypy/bytearray.h"
#include "tinypy/weakref.h"
#include "tinypy/set.h"
#include "tinypy/code.h"
#include "tinypy/cell.h"
#include "tinypy/class.h"
#include "tinypy/descriptor.h"
#include "tinypy/error.h"
#include "tinypy/exception.h"
#include "tinypy/frame.h"
#include "tinypy/function.h"
#include "tinypy/generator.h"
#include "tinypy/iterator.h"
#include "tinypy/item.h"
#include "tinypy/module.h"
#include "tinypy/native.h"
#include "tinypy/method.h"
#include "tinypy/object.h"
#include "tinypy/output.h"
#include "tinypy/representation.h"
#include "tinypy/operator.h"
#include "tinypy/comparison.h"
#include "tinypy/slice.h"
#include "tinypy/super.h"
#include "tinypy/traceback.h"
#include "tinypy/hash.h"
#include "tinypy/list.h"
#include "tinypy/long.h"
#include "tinypy/numeric.h"
#include "tinypy/tuple.h"
#include "tinypy/type.h"
#include "tinypy/value.h"
#include "tinypy/vm.h"

#include <assert.h>

typedef union tinypy_internal_max_align_t {
    void *pointer_value;
    void (*function_value)(void);
    int64_t integer_value;
    long double floating_value;
} tinypy_internal_max_align_t;

typedef struct tinypy_internal_alignment_probe_t {
    char prefix;
    tinypy_internal_max_align_t value;
} tinypy_internal_alignment_probe_t;

typedef struct tinypy_internal_exception_state_t {
    tinypy_value_t *type;
    tinypy_value_t *value;
    tinypy_value_t *traceback;
} tinypy_internal_exception_state_t;

#define TINYPY_INTERNAL_ALIGNMENT \
    ((size_t)offsetof(tinypy_internal_alignment_probe_t, value))

#define TINYPY_TYPE_FLAG_IMMUTABLE UINT64_C(1)
#define TINYPY_TYPE_FLAG_HEAP UINT64_C(2)
#define TINYPY_TYPE_FLAG_BASE_TYPE UINT64_C(4)
#define TINYPY_TYPE_FLAG_TYPE_SUBCLASS UINT64_C(8)
#define TINYPY_VM_STATE_LIVE UINT32_C(0x5450594c)
#define TINYPY_VM_STATE_DESTROYING UINT32_C(0x54505944)
#define TINYPY_BUILTIN_TYPE_COUNT 51U
#define TINYPY_FRAME_MAX_BLOCKS 20U
#define TINYPY_INTEGER_CONSTANT_MIN (-INT64_C(1023))
#define TINYPY_INTEGER_CONSTANT_MAX INT64_C(1024)
#define TINYPY_INTEGER_CONSTANT_COUNT 2048U

typedef enum tinypy_exception_type_index_e {
    TINYPY_EXCEPTION_BASE = 0,
    TINYPY_EXCEPTION_EXCEPTION,
    TINYPY_EXCEPTION_STANDARD_ERROR,
    TINYPY_EXCEPTION_ARITHMETIC_ERROR,
    TINYPY_EXCEPTION_FLOATING_POINT_ERROR,
    TINYPY_EXCEPTION_OVERFLOW_ERROR,
    TINYPY_EXCEPTION_ZERO_DIVISION_ERROR,
    TINYPY_EXCEPTION_ASSERTION_ERROR,
    TINYPY_EXCEPTION_ATTRIBUTE_ERROR,
    TINYPY_EXCEPTION_ENVIRONMENT_ERROR,
    TINYPY_EXCEPTION_IO_ERROR,
    TINYPY_EXCEPTION_OS_ERROR,
    TINYPY_EXCEPTION_WINDOWS_ERROR,
    TINYPY_EXCEPTION_EOF_ERROR,
    TINYPY_EXCEPTION_IMPORT_ERROR,
    TINYPY_EXCEPTION_LOOKUP_ERROR,
    TINYPY_EXCEPTION_INDEX_ERROR,
    TINYPY_EXCEPTION_KEY_ERROR,
    TINYPY_EXCEPTION_MEMORY_ERROR,
    TINYPY_EXCEPTION_NAME_ERROR,
    TINYPY_EXCEPTION_UNBOUND_LOCAL_ERROR,
    TINYPY_EXCEPTION_REFERENCE_ERROR,
    TINYPY_EXCEPTION_RUNTIME_ERROR,
    TINYPY_EXCEPTION_NOT_IMPLEMENTED_ERROR,
    TINYPY_EXCEPTION_SYNTAX_ERROR,
    TINYPY_EXCEPTION_INDENTATION_ERROR,
    TINYPY_EXCEPTION_TAB_ERROR,
    TINYPY_EXCEPTION_SYSTEM_ERROR,
    TINYPY_EXCEPTION_TYPE_ERROR,
    TINYPY_EXCEPTION_VALUE_ERROR,
    TINYPY_EXCEPTION_UNICODE_ERROR,
    TINYPY_EXCEPTION_UNICODE_DECODE_ERROR,
    TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR,
    TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR,
    TINYPY_EXCEPTION_STOP_ITERATION,
    TINYPY_EXCEPTION_WARNING,
    TINYPY_EXCEPTION_USER_WARNING,
    TINYPY_EXCEPTION_DEPRECATION_WARNING,
    TINYPY_EXCEPTION_PENDING_DEPRECATION_WARNING,
    TINYPY_EXCEPTION_SYNTAX_WARNING,
    TINYPY_EXCEPTION_RUNTIME_WARNING,
    TINYPY_EXCEPTION_FUTURE_WARNING,
    TINYPY_EXCEPTION_IMPORT_WARNING,
    TINYPY_EXCEPTION_UNICODE_WARNING,
    TINYPY_EXCEPTION_BYTES_WARNING,
    TINYPY_EXCEPTION_SYSTEM_EXIT,
    TINYPY_EXCEPTION_KEYBOARD_INTERRUPT,
    TINYPY_EXCEPTION_GENERATOR_EXIT,
    TINYPY_EXCEPTION_TYPE_COUNT
} tinypy_exception_type_index_e;

typedef ptrdiff_t tinypy_ref_t;
typedef struct tinypy_compile_environment_t tinypy_compile_environment_t;

/* Complete universal TinyPy object header. */
struct tinypy_value_t {
    tinypy_ref_t ref;
    tinypy_type_t *type;
};

typedef char tinypy_object_ref_must_be_first_t[
    offsetof(tinypy_value_t, ref) == 0U ? 1 : -1];
typedef char tinypy_object_type_must_follow_refcount_t[
    offsetof(tinypy_value_t, type) == sizeof(tinypy_ref_t) ? 1 : -1];
typedef char tinypy_object_header_has_only_two_fields_t[
    sizeof(tinypy_value_t) == sizeof(tinypy_ref_t) + sizeof(tinypy_type_t *) ? 1 : -1];

/* Header for TinyPy layouts with an inline variable-size tail. */
typedef struct tinypy_var_object_t {
    tinypy_value_t base;
    ptrdiff_t size;
} tinypy_var_object_t;

typedef void (*tinypy_release_callback_t)(tinypy_value_t *value, void *user_data);
typedef void (*tinypy_release_references_slot_t)(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
typedef void (*tinypy_destroy_slot_t)(tinypy_vm_t *vm, tinypy_value_t *value);
typedef tinypy_value_t *(*tinypy_unary_slot_t)(tinypy_value_t *value, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_binary_slot_t)(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_ternary_slot_t)(tinypy_value_t *first, tinypy_value_t *second, tinypy_value_t *third, tinypy_error_t **out_error);
typedef int32_t (*tinypy_inquiry_slot_t)(tinypy_value_t *value, tinypy_error_t **out_error);
typedef ptrdiff_t (*tinypy_length_slot_t)(tinypy_value_t *value, tinypy_error_t **out_error);
typedef tinypy_hash_t (*tinypy_hash_slot_t)(tinypy_value_t *value, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_call_slot_t)(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_get_attribute_slot_t)(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error);
typedef int32_t (*tinypy_set_attribute_slot_t)(tinypy_value_t *value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_get_item_slot_t)(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error);
typedef int32_t (*tinypy_set_item_slot_t)(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *item, tinypy_error_t **out_error);
typedef int32_t (*tinypy_contains_slot_t)(tinypy_value_t *value, tinypy_value_t *item, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_compare_slot_t)(tinypy_value_t *left, tinypy_value_t *right, int operation, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_iter_slot_t)(tinypy_value_t *value, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_next_slot_t)(tinypy_value_t *iterator, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_descriptor_get_slot_t)(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
typedef int32_t (*tinypy_descriptor_set_slot_t)(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error);
typedef int32_t (*tinypy_init_slot_t)(tinypy_value_t *instance, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_new_slot_t)(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);

typedef struct tinypy_number_slots_t {
    tinypy_binary_slot_t add;
    tinypy_binary_slot_t subtract;
    tinypy_binary_slot_t multiply;
    tinypy_binary_slot_t divide;
    tinypy_binary_slot_t remainder;
    tinypy_ternary_slot_t power;
    tinypy_unary_slot_t negative;
    tinypy_unary_slot_t positive;
    tinypy_unary_slot_t absolute;
    tinypy_inquiry_slot_t nonzero;
    tinypy_unary_slot_t invert;
    tinypy_binary_slot_t left_shift;
    tinypy_binary_slot_t right_shift;
    tinypy_binary_slot_t bit_and;
    tinypy_binary_slot_t bit_xor;
    tinypy_binary_slot_t bit_or;
    tinypy_binary_slot_t floor_divide;
    tinypy_binary_slot_t true_divide;
    tinypy_unary_slot_t index;
} tinypy_number_slots_t;

typedef struct tinypy_sequence_slots_t {
    tinypy_length_slot_t length;
    tinypy_binary_slot_t concat;
    tinypy_get_item_slot_t get_item;
    tinypy_set_item_slot_t set_item;
    tinypy_contains_slot_t contains;
} tinypy_sequence_slots_t;

typedef struct tinypy_mapping_slots_t {
    tinypy_length_slot_t length;
    tinypy_get_item_slot_t get_item;
    tinypy_set_item_slot_t set_item;
} tinypy_mapping_slots_t;

typedef enum tinypy_dict_entry_state_e {
    TINYPY_DICT_ENTRY_EMPTY = 0,
    TINYPY_DICT_ENTRY_ACTIVE = 1,
    TINYPY_DICT_ENTRY_DUMMY = 2
} tinypy_dict_entry_state_e;

typedef struct tinypy_dict_entry_t {
    tinypy_hash_t hash;
    tinypy_value_t *key;
    tinypy_value_t *value;
    tinypy_dict_entry_state_e state;
} tinypy_dict_entry_t;

#define TINYPY_DICT_MIN_SIZE 8U

/* TinyPy type object. Builtin and heap types share this prefix; heap
 * types later append their protocol tables and slot/member storage. */
struct tinypy_type_t {
    tinypy_var_object_t base;
    tinypy_vm_t *vm;
    const char *name;
    size_t name_size;
    size_t basic_size;
    size_t item_size;
    uint64_t flags;
    tinypy_type_t *base_type;
    tinypy_value_type_e layout_kind;
    tinypy_value_t *name_object;
    tinypy_value_t *dict;
    tinypy_value_t *bases;
    tinypy_value_t *mro;
    tinypy_number_slots_t *number_slots;
    tinypy_sequence_slots_t *sequence_slots;
    tinypy_mapping_slots_t *mapping_slots;
    tinypy_unary_slot_t repr;
    tinypy_unary_slot_t string;
    tinypy_hash_slot_t hash;
    tinypy_call_slot_t call;
    tinypy_get_attribute_slot_t get_attribute;
    tinypy_set_attribute_slot_t set_attribute;
    tinypy_compare_slot_t rich_compare;
    tinypy_release_references_slot_t release_references;
    tinypy_destroy_slot_t destroy;
    tinypy_iter_slot_t iter;
    tinypy_next_slot_t next;
    tinypy_descriptor_get_slot_t descriptor_get;
    tinypy_descriptor_set_slot_t descriptor_set;
    tinypy_init_slot_t initialize;
    tinypy_new_slot_t create;
    size_t weakref_offset;
    size_t dict_offset;
    size_t slots_offset;
    size_t slot_count;
    int32_t has_instance_dict;
    int32_t has_finalizer;
    uint64_t version_tag;
    tinypy_value_t *weakrefs;
    tinypy_value_t *subclasses;
};

typedef struct tinypy_instance_object_t {
    tinypy_value_t base;
    tinypy_value_t *dict;
    tinypy_value_t *slots[];
} tinypy_instance_object_t;

typedef struct tinypy_class_object_t {
    tinypy_value_t base;
    tinypy_value_t *name;
    tinypy_value_t *bases;
    tinypy_value_t *dict;
    tinypy_value_t *weakrefs;
} tinypy_class_object_t;

typedef struct tinypy_old_instance_object_t {
    tinypy_value_t base;
    tinypy_value_t *class_object;
    tinypy_value_t *dict;
    tinypy_value_t *weakrefs;
} tinypy_old_instance_object_t;

typedef struct tinypy_none_object_t {
    tinypy_value_t base;
} tinypy_none_object_t;

typedef struct tinypy_integer_object_t {
    tinypy_value_t base;
    int64_t integer_value;
} tinypy_integer_object_t;

typedef struct tinypy_string_object_t {
    tinypy_var_object_t base;
    int32_t interned;
    unsigned char bytes[1];
} tinypy_string_object_t;

typedef struct tinypy_unicode_object_t {
    tinypy_var_object_t base;
    size_t byte_size;
    unsigned char utf8[];
} tinypy_unicode_object_t;

typedef struct tinypy_long_object_t {
    tinypy_var_object_t base;
    uint16_t digits[];
} tinypy_long_object_t;

typedef struct tinypy_float_object_t {
    tinypy_value_t base;
    double value;
} tinypy_float_object_t;

typedef struct tinypy_complex_object_t {
    tinypy_value_t base;
    double real;
    double imaginary;
} tinypy_complex_object_t;

typedef struct tinypy_tuple_object_t {
    tinypy_var_object_t base;
    tinypy_value_t *items[1];
} tinypy_tuple_object_t;

typedef struct tinypy_tuple_subclass_object_t {
    tinypy_var_object_t base;
    tinypy_value_t **items;
    tinypy_value_t *dict;
} tinypy_tuple_subclass_object_t;

typedef struct tinypy_list_object_t {
    tinypy_var_object_t base;
    tinypy_value_t **items;
    size_t allocated;
    uint64_t mutation_version;
} tinypy_list_object_t;

typedef struct tinypy_dict_object_t {
    tinypy_value_t base;
    size_t fill;
    size_t used;
    size_t mask;
    tinypy_dict_entry_t *table;
    uint64_t mutation_version;
    tinypy_dict_entry_t small_table[TINYPY_DICT_MIN_SIZE];
} tinypy_dict_object_t;

typedef struct tinypy_set_object_t {
    tinypy_value_t base;
    tinypy_value_t *dict;
    tinypy_hash_t hash;
    int32_t hash_computed;
} tinypy_set_object_t;

typedef struct tinypy_output_stream_object_t {
    tinypy_value_t base;
    tinypy_output_channel_e channel;
    int32_t soft_space;
} tinypy_output_stream_object_t;

typedef struct tinypy_buffer_object_t {
    tinypy_value_t base;
    tinypy_value_t *owner;
    size_t offset;
    size_t size;
} tinypy_buffer_object_t;

typedef struct tinypy_bytearray_object_t {
    tinypy_var_object_t base;
    size_t capacity;
    unsigned char *bytes;
} tinypy_bytearray_object_t;

typedef struct tinypy_weakref_object_t {
    tinypy_value_t base;
    tinypy_value_t *object;
    tinypy_value_t *callback;
    tinypy_value_t *previous;
    tinypy_value_t *next;
    tinypy_hash_t hash;
    int32_t hash_computed;
    tinypy_value_t *dict;
    tinypy_value_t *slots[];
} tinypy_weakref_object_t;

typedef struct tinypy_partial_object_t {
    tinypy_value_t base;
    tinypy_value_t *callable;
    tinypy_value_t *args;
    tinypy_value_t *keywords;
    tinypy_value_t *dict;
    tinypy_value_t *weakrefs;
} tinypy_partial_object_t;

typedef struct tinypy_sre_pattern_object_t {
    tinypy_value_t base;
    tinypy_value_t *pattern;
    tinypy_value_t *groupindex;
    tinypy_value_t *indexgroup;
    uint32_t *code;
    size_t code_size;
    size_t groups;
    int64_t flags;
    tinypy_value_t *weakrefs;
} tinypy_sre_pattern_object_t;

typedef struct tinypy_sre_match_object_t {
    tinypy_value_t base;
    tinypy_value_t *pattern;
    tinypy_value_t *string;
    size_t pos;
    size_t endpos;
    size_t start;
    size_t end;
    size_t *marks;
    size_t mark_count;
    ptrdiff_t lastindex;
} tinypy_sre_match_object_t;

typedef struct tinypy_dict_view_object_t {
    tinypy_value_t base;
    tinypy_value_t *dict;
    tinypy_dict_view_kind_e kind;
} tinypy_dict_view_object_t;

typedef struct tinypy_file_object_t {
    tinypy_value_t base;
    void *host_handle;
} tinypy_file_object_t;

/* TinyPy code object. The runtime omits the reference implementation's frame cache and
 * weakref list because neither changes Python code semantics. */
typedef struct tinypy_code_object_t {
    tinypy_value_t base;
    int32_t arg_count;
    int32_t local_count;
    int32_t stack_size;
    int32_t flags;
    tinypy_value_t *bytecode;
    tinypy_value_t *consts;
    tinypy_value_t *names;
    tinypy_value_t *varnames;
    tinypy_value_t *freevars;
    tinypy_value_t *cellvars;
    tinypy_value_t *filename;
    tinypy_value_t *name;
    int32_t first_line_number;
    tinypy_value_t *lnotab;
    tinypy_compile_environment_t *compile_environment;
} tinypy_code_object_t;

typedef struct tinypy_frame_block_t {
    int32_t type;
    size_t handler;
    size_t stack_level;
} tinypy_frame_block_t;

/* TinyPy frame object. Trace and exception-state fields are added when
 * their Python-visible objects are introduced; the execution layout already
 * matches CPython's inline f_localsplus storage. */
typedef struct tinypy_frame_object_t {
    tinypy_var_object_t base;
    tinypy_value_t *back;
    tinypy_value_t *code;
    tinypy_value_t *builtins;
    tinypy_value_t *globals;
    tinypy_value_t *locals;
    tinypy_value_t *previous_handled_type;
    tinypy_value_t *previous_handled_value;
    tinypy_value_t *previous_handled_traceback;
    tinypy_value_t **value_stack;
    tinypy_value_t **stack_top;
    int32_t last_instruction;
    int32_t line_number;
    size_t block_count;
    tinypy_frame_block_t blocks[TINYPY_FRAME_MAX_BLOCKS];
    tinypy_value_t *locals_plus[];
} tinypy_frame_object_t;

typedef struct tinypy_function_object_t {
    tinypy_value_t base;
    tinypy_value_t *code;
    tinypy_value_t *globals;
    tinypy_value_t *defaults;
    tinypy_value_t *closure;
    tinypy_value_t *doc;
    tinypy_value_t *name;
    tinypy_value_t *dict;
    tinypy_value_t *module;
} tinypy_function_object_t;

typedef struct tinypy_iterator_object_t {
    tinypy_value_t base;
    tinypy_value_t *iterable;
    size_t index;
    size_t table_position;
    uint64_t expected_version;
    int32_t mode;
    int64_t current;
    int64_t step;
    size_t remaining;
} tinypy_iterator_object_t;

typedef struct tinypy_xrange_object_t {
    tinypy_value_t base;
    int64_t start;
    int64_t step;
    size_t length;
} tinypy_xrange_object_t;

typedef struct tinypy_enumerate_object_t {
    tinypy_value_t base;
    tinypy_value_t *iterator;
    int64_t index;
} tinypy_enumerate_object_t;

typedef struct tinypy_reversed_object_t {
    tinypy_value_t base;
    tinypy_value_t *sequence;
    size_t index;
} tinypy_reversed_object_t;

typedef struct tinypy_method_object_t {
    tinypy_value_t base;
    tinypy_value_t *function;
    tinypy_value_t *self;
    tinypy_value_t *owner;
} tinypy_method_object_t;

typedef struct tinypy_cell_object_t {
    tinypy_value_t base;
    tinypy_value_t *content;
} tinypy_cell_object_t;

typedef struct tinypy_slice_object_t {
    tinypy_value_t base;
    tinypy_value_t *start;
    tinypy_value_t *stop;
    tinypy_value_t *step;
} tinypy_slice_object_t;

typedef struct tinypy_module_object_t {
    tinypy_value_t base;
    tinypy_value_t *dict;
    tinypy_value_t *name;
} tinypy_module_object_t;

typedef struct tinypy_native_function_object_t {
    tinypy_value_t base;
    tinypy_value_t *name;
    tinypy_native_function_callback_t callback;
    void *user_data;
    tinypy_native_function_finalize_t finalize;
} tinypy_native_function_object_t;

typedef struct tinypy_callable_descriptor_object_t {
    tinypy_value_t base;
    tinypy_value_t *callable;
} tinypy_callable_descriptor_object_t;

typedef struct tinypy_property_object_t {
    tinypy_value_t base;
    tinypy_value_t *getter;
    tinypy_value_t *setter;
    tinypy_value_t *deleter;
    tinypy_value_t *doc;
} tinypy_property_object_t;

typedef struct tinypy_c_descriptor_object_t {
    tinypy_value_t base;
    tinypy_type_t *owner;
    tinypy_value_t *name;
    size_t index;
    int32_t field;
    int32_t writable;
    int32_t owner_retained;
} tinypy_c_descriptor_object_t;

typedef struct tinypy_super_object_t {
    tinypy_value_t base;
    tinypy_type_t *type;
    tinypy_value_t *object;
    tinypy_type_t *object_type;
} tinypy_super_object_t;

typedef struct tinypy_traceback_object_t {
    tinypy_value_t base;
    tinypy_value_t *next;
    tinypy_value_t *frame;
    int32_t last_instruction;
    int32_t line_number;
} tinypy_traceback_object_t;

typedef struct tinypy_generator_object_t {
    tinypy_value_t base;
    tinypy_value_t *frame;
    size_t instruction_offset;
    int running;
    int started;
    int finished;
    tinypy_value_t *handled_type;
    tinypy_value_t *handled_value;
    tinypy_value_t *handled_traceback;
} tinypy_generator_object_t;

#define TINYPY_INTEGER_VALUE(value) \
    (((tinypy_integer_object_t *)(value))->integer_value)
#define TINYPY_INSTANCE_OBJECT(value) ((tinypy_instance_object_t *)(value))
#define TINYPY_CLASS_OBJECT(value) ((tinypy_class_object_t *)(value))
#define TINYPY_OLD_INSTANCE_OBJECT(value) ((tinypy_old_instance_object_t *)(value))
#define TINYPY_STRING_OBJECT(value) ((tinypy_string_object_t *)(value))
#define TINYPY_UNICODE_OBJECT(value) ((tinypy_unicode_object_t *)(value))
#define TINYPY_LONG_OBJECT(value) ((tinypy_long_object_t *)(value))
#define TINYPY_FLOAT_OBJECT(value) ((tinypy_float_object_t *)(value))
#define TINYPY_COMPLEX_OBJECT(value) ((tinypy_complex_object_t *)(value))
#define TINYPY_TUPLE_OBJECT(value) ((tinypy_tuple_object_t *)(value))
#define TINYPY_TUPLE_SUBCLASS_OBJECT(value) ((tinypy_tuple_subclass_object_t *)(value))
#define TINYPY_LIST_OBJECT(value) ((tinypy_list_object_t *)(value))
#define TINYPY_DICT_OBJECT(value) ((tinypy_dict_object_t *)(value))
#define TINYPY_SET_OBJECT(value) ((tinypy_set_object_t *)(value))
#define TINYPY_OUTPUT_STREAM_OBJECT(value) ((tinypy_output_stream_object_t *)(value))
#define TINYPY_BUFFER_OBJECT(value) ((tinypy_buffer_object_t *)(value))
#define TINYPY_BYTEARRAY_OBJECT(value) ((tinypy_bytearray_object_t *)(value))
#define TINYPY_WEAKREF_OBJECT(value) ((tinypy_weakref_object_t *)(value))
#define TINYPY_DICT_VIEW_OBJECT(value) ((tinypy_dict_view_object_t *)(value))
#define TINYPY_PARTIAL_OBJECT(value) ((tinypy_partial_object_t *)(value))
#define TINYPY_SRE_PATTERN_OBJECT(value) ((tinypy_sre_pattern_object_t *)(value))
#define TINYPY_SRE_MATCH_OBJECT(value) ((tinypy_sre_match_object_t *)(value))
#define TINYPY_CODE_OBJECT(value) ((tinypy_code_object_t *)(value))
#define TINYPY_FRAME_OBJECT(value) ((tinypy_frame_object_t *)(value))
#define TINYPY_FUNCTION_OBJECT(value) ((tinypy_function_object_t *)(value))
#define TINYPY_ITERATOR_OBJECT(value) ((tinypy_iterator_object_t *)(value))
#define TINYPY_XRANGE_OBJECT(value) ((tinypy_xrange_object_t *)(value))
#define TINYPY_ENUMERATE_OBJECT(value) ((tinypy_enumerate_object_t *)(value))
#define TINYPY_REVERSED_OBJECT(value) ((tinypy_reversed_object_t *)(value))
#define TINYPY_METHOD_OBJECT(value) ((tinypy_method_object_t *)(value))
#define TINYPY_CELL_OBJECT(value) ((tinypy_cell_object_t *)(value))
#define TINYPY_SLICE_OBJECT(value) ((tinypy_slice_object_t *)(value))
#define TINYPY_MODULE_OBJECT(value) ((tinypy_module_object_t *)(value))
#define TINYPY_NATIVE_FUNCTION_OBJECT(value) ((tinypy_native_function_object_t *)(value))
#define TINYPY_CALLABLE_DESCRIPTOR_OBJECT(value) ((tinypy_callable_descriptor_object_t *)(value))
#define TINYPY_PROPERTY_OBJECT(value) ((tinypy_property_object_t *)(value))
#define TINYPY_C_DESCRIPTOR_OBJECT(value) ((tinypy_c_descriptor_object_t *)(value))
#define TINYPY_SUPER_OBJECT(value) ((tinypy_super_object_t *)(value))
#define TINYPY_TRACEBACK_OBJECT(value) ((tinypy_traceback_object_t *)(value))
#define TINYPY_GENERATOR_OBJECT(value) ((tinypy_generator_object_t *)(value))
#define TINYPY_SIZE(value) (((tinypy_var_object_t *)(value))->size)
#define TINYPY_LONG_DIGIT_COUNT(value) \
    ((size_t)(TINYPY_SIZE(value) < 0 ? -TINYPY_SIZE(value) : TINYPY_SIZE(value)))
#define TINYPY_LONG_SIGN(value) \
    (TINYPY_SIZE(value) < 0 ? -1 : (TINYPY_SIZE(value) > 0 ? 1 : 0))

typedef char tinypy_integer_body_must_follow_header_t[
    offsetof(tinypy_integer_object_t, integer_value) == sizeof(tinypy_value_t) ? 1 : -1];
typedef char tinypy_float_body_must_follow_header_t[
    offsetof(tinypy_float_object_t, value) == sizeof(tinypy_value_t) ? 1 : -1];
typedef char tinypy_complex_body_must_follow_header_t[
    offsetof(tinypy_complex_object_t, real) == sizeof(tinypy_value_t) ? 1 : -1];
typedef char tinypy_dict_body_must_follow_header_t[
    offsetof(tinypy_dict_object_t, fill) == sizeof(tinypy_value_t) ? 1 : -1];

struct tinypy_vm_t {
    uint32_t state;
    tinypy_allocator_t allocator;
    tinypy_host_t host;
    int has_host;
    size_t max_heap_bytes;
    size_t allocated_bytes;
    uint64_t hash_secret_prefix;
    uint64_t hash_secret_suffix;
    size_t hash_depth;
    size_t equality_depth;
    int32_t optimize_level;

    tinypy_type_t type_type;
    tinypy_type_t object_type;
    tinypy_type_t none_type;
    tinypy_type_t not_implemented_type;
    tinypy_type_t basestring_type;
    tinypy_type_t bool_type;
    tinypy_type_t integer_type;
    tinypy_type_t string_type;
    tinypy_type_t unicode_type;
    tinypy_type_t long_type;
    tinypy_type_t float_type;
    tinypy_type_t complex_type;
    tinypy_type_t tuple_type;
    tinypy_type_t list_type;
    tinypy_type_t dict_type;
    tinypy_type_t code_type;
    tinypy_type_t frame_type;
    tinypy_type_t function_type;
    tinypy_type_t iterator_type;
    tinypy_type_t method_type;
    tinypy_type_t cell_type;
    tinypy_type_t slice_type;
    tinypy_type_t module_type;
    tinypy_type_t native_function_type;
    tinypy_type_t static_method_type;
    tinypy_type_t class_method_type;
    tinypy_type_t property_type;
    tinypy_type_t super_type;
    tinypy_type_t traceback_type;
    tinypy_type_t generator_type;
    tinypy_type_t xrange_type;
    tinypy_type_t enumerate_type;
    tinypy_type_t reversed_type;
    tinypy_type_t buffer_type;
    tinypy_type_t bytearray_type;
    tinypy_type_t weakref_type;
    tinypy_type_t dict_keys_type;
    tinypy_type_t dict_values_type;
    tinypy_type_t dict_items_type;
    tinypy_type_t ellipsis_type;
    tinypy_type_t file_type;
    tinypy_type_t getset_descriptor_type;
    tinypy_type_t member_descriptor_type;
    tinypy_type_t class_type;
    tinypy_type_t old_instance_type;
    tinypy_type_t partial_type;
    tinypy_type_t sre_pattern_type;
    tinypy_type_t sre_match_type;
    tinypy_type_t set_type;
    tinypy_type_t frozenset_type;
    tinypy_type_t output_stream_type;

    tinypy_sequence_slots_t buffer_sequence_slots;
    tinypy_mapping_slots_t buffer_mapping_slots;
    tinypy_sequence_slots_t bytearray_sequence_slots;
    tinypy_mapping_slots_t bytearray_mapping_slots;
    tinypy_sequence_slots_t dict_view_sequence_slots;

    tinypy_type_t *exception_types[TINYPY_EXCEPTION_TYPE_COUNT];
    tinypy_value_t *raised_type;
    tinypy_value_t *raised_value;
    tinypy_value_t *raised_traceback;
    tinypy_value_t *handled_type;
    tinypy_value_t *handled_value;
    tinypy_value_t *handled_traceback;

    tinypy_frame_object_t *current_frame;
    size_t evaluation_depth;
    tinypy_value_t *builtins;
    tinypy_value_t *modules;

    /* Builtin type dictionaries are real dict objects, but their empty object
     * bodies are VM-owned just like the builtin types and singletons. This
     * keeps VM creation allocation-atomic; dictionary tables still use the
     * host allocator when attributes are inserted. */
    tinypy_dict_object_t builtin_type_dicts[TINYPY_BUILTIN_TYPE_COUNT];

    tinypy_none_object_t none_object;
    tinypy_none_object_t not_implemented_object;
    tinypy_none_object_t ellipsis_object;
    tinypy_integer_object_t false_object;
    tinypy_integer_object_t true_object;
    tinypy_integer_object_t integer_constants[TINYPY_INTEGER_CONSTANT_COUNT];
    tinypy_float_object_t float_zero_object;
    tinypy_string_object_t empty_string_object;
    tinypy_tuple_object_t empty_tuple_object;
};

struct tinypy_error_t {
    tinypy_allocator_t allocator;
    tinypy_error_kind_e kind;
    size_t allocation_size;
    size_t message_size;
    size_t filename_size;
    size_t source_line_size;
    int32_t line_number;
    int32_t column_offset;
    char data[];
};

int tinypy_internal_host_valid(const tinypy_host_t *host);
int tinypy_internal_vm_valid(const tinypy_vm_t *vm);

void *tinypy_internal_vm_allocate(tinypy_vm_t *vm, size_t size, uint32_t tag);
void *tinypy_internal_vm_reallocate(tinypy_vm_t *vm, void *memory, size_t old_size, size_t new_size, uint32_t tag);
void tinypy_internal_vm_deallocate(tinypy_vm_t *vm, void *memory, size_t size, uint32_t tag);

void tinypy_internal_clear_error(tinypy_error_t **out_error);
void tinypy_internal_make_error(const tinypy_allocator_t *allocator, tinypy_error_kind_e error_kind, const char *message, tinypy_error_t **out_error);
void tinypy_internal_make_vm_error(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const char *message, tinypy_error_t **out_error);
void tinypy_internal_make_vm_error_location(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const char *message, const char *logical_filename, size_t filename_size, int32_t line_number, int32_t column_offset, const char *source_line, size_t source_line_size, tinypy_error_t **out_error);

int tinypy_internal_value_belongs_to(const tinypy_vm_t *vm, const tinypy_value_t *value);
int tinypy_internal_value_is_vm_embedded(const tinypy_vm_t *vm, const tinypy_value_t *value);
tinypy_vm_t *tinypy_internal_value_vm(const tinypy_value_t *value);
tinypy_value_type_e tinypy_internal_value_kind(const tinypy_value_t *value);
tinypy_type_t *tinypy_internal_type_for_kind(tinypy_vm_t *vm, tinypy_value_type_e kind);
tinypy_value_t *tinypy_internal_value_allocate(tinypy_vm_t *vm, tinypy_value_type_e type, size_t allocation_size);
tinypy_value_t *tinypy_internal_object_allocate(tinypy_vm_t *vm, tinypy_type_t *object_type, size_t allocation_size);
size_t tinypy_internal_value_allocation_size(const tinypy_value_t *value);
void tinypy_internal_value_destroy(tinypy_vm_t *vm, tinypy_value_t *value);
const unsigned char *tinypy_internal_text_bytes(const tinypy_value_t *value);
size_t tinypy_internal_text_byte_size(const tinypy_value_t *value);
int32_t tinypy_internal_string_is_interned(const tinypy_value_t *value);
void tinypy_internal_string_set_interned(tinypy_value_t *value, int32_t interned);

void tinypy_internal_tuple_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_tuple_subclass_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_tuple_subclass_destroy(tinypy_vm_t *vm, tinypy_value_t *value);
tinypy_value_t *tinypy_internal_tuple_from_borrowed_items(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size);
tinypy_value_t *tinypy_internal_tuple_subclass_from_items(tinypy_type_t *type, tinypy_value_t *const *items, size_t size);
tinypy_value_t *const *tinypy_internal_tuple_items(const tinypy_value_t *value);
void tinypy_internal_list_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_list_destroy(tinypy_vm_t *vm, tinypy_value_t *value);
void tinypy_internal_dict_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_dict_destroy(tinypy_vm_t *vm, tinypy_value_t *value);
int32_t tinypy_internal_dict_equal(const tinypy_value_t *left, const tinypy_value_t *right);
void tinypy_internal_set_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_set_iter(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_set_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_frozenset_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_string_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_bytearray_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_bytearray_destroy(tinypy_vm_t *vm, tinypy_value_t *value);
ptrdiff_t tinypy_internal_bytearray_length(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_bytearray_get_item(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error);
int32_t tinypy_internal_bytearray_set_item(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *item, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_bytearray_repr(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_bytearray_string(tinypy_value_t *value, tinypy_error_t **out_error);
void tinypy_internal_initialize_bytearray_methods(tinypy_vm_t *vm);
int32_t tinypy_internal_bytes_view(const tinypy_value_t *value, const unsigned char **out_bytes, size_t *out_size);
tinypy_value_t **tinypy_internal_object_dict_slot(tinypy_value_t *value);
tinypy_value_t **tinypy_internal_object_member_slot(tinypy_value_t *value, size_t index);
tinypy_value_t **tinypy_internal_weakref_head_slot(tinypy_value_t *value);
void tinypy_internal_weakref_clear(tinypy_value_t *value);
void tinypy_internal_weakref_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_weakref_destroy(tinypy_vm_t *vm, tinypy_value_t *value);
tinypy_value_t *tinypy_internal_weakref_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_weakref_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_initialize_weakref_type(tinypy_vm_t *vm);
void tinypy_internal_initialize_weakref_module(tinypy_vm_t *vm);
void tinypy_internal_initialize_codecs_module(tinypy_vm_t *vm);
void tinypy_internal_initialize_functools_module(tinypy_vm_t *vm);
void tinypy_internal_initialize_struct_module(tinypy_vm_t *vm);
void tinypy_internal_initialize_representation_types(tinypy_vm_t *vm);
void tinypy_internal_initialize_sre_module(tinypy_vm_t *vm);
void tinypy_internal_sre_pattern_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_sre_pattern_destroy(tinypy_vm_t *vm, tinypy_value_t *value);
void tinypy_internal_sre_match_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_sre_match_destroy(tinypy_vm_t *vm, tinypy_value_t *value);
void tinypy_internal_partial_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_partial_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_partial_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_initialize_exceptions_module(tinypy_vm_t *vm);
void tinypy_internal_register_module(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *module);
void tinypy_internal_dict_view_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
ptrdiff_t tinypy_internal_dict_view_length(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_dict_view_iter(tinypy_value_t *value, tinypy_error_t **out_error);
int32_t tinypy_internal_dict_view_contains(tinypy_value_t *value, tinypy_value_t *item, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_type_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_type_subclasses(tinypy_type_t *type);
tinypy_value_t *tinypy_internal_object_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_bool_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_integer_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_long_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_float_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_complex_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_unicode_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_tuple_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_list_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_dict_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
int32_t tinypy_internal_set_equal(const tinypy_value_t *left, const tinypy_value_t *right);
tinypy_hash_t tinypy_internal_frozenset_hash(const tinypy_value_t *value);
tinypy_value_t *tinypy_internal_set_binary(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error);
void tinypy_internal_initialize_set_types(tinypy_vm_t *vm);
tinypy_value_t *tinypy_internal_output_stream_new(tinypy_vm_t *vm, tinypy_output_channel_e channel);
void tinypy_internal_initialize_output_type(tinypy_vm_t *vm);
int32_t tinypy_internal_output_write(tinypy_value_t *target, const void *bytes, size_t size, tinypy_error_t **out_error);
int32_t tinypy_internal_output_soft_space(tinypy_value_t *target);
void tinypy_internal_output_set_soft_space(tinypy_value_t *target, int32_t soft_space);
void tinypy_internal_type_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_type_destroy(tinypy_vm_t *vm, tinypy_value_t *value);
void tinypy_internal_instance_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_type_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_class_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_old_instance_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_class_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_class_get_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_old_instance_get_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error);
int32_t tinypy_internal_class_set_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error);
int32_t tinypy_internal_old_instance_set_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error);
int32_t tinypy_internal_class_delete_attribute(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error);
int32_t tinypy_internal_old_instance_delete_attribute(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_class_lookup(tinypy_value_t *class_value, const char *name, size_t name_size);
int32_t tinypy_internal_old_instance_has_special(tinypy_value_t *value, const char *name, size_t name_size);
int32_t tinypy_internal_object_has_special(tinypy_value_t *value, const char *name, size_t name_size);
void tinypy_internal_code_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_frame_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_function_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_function_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_eval_function(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_eval_generator_resume(tinypy_generator_object_t *generator, tinypy_value_t *send_value, tinypy_value_t *throw_value, tinypy_value_t *throw_traceback, int *out_yielded, tinypy_error_t **out_error);
void tinypy_internal_iterator_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_iterator_iter(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_iterator_next(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_dict_iterator_new(tinypy_value_t *dict, int32_t mode);
tinypy_value_t *tinypy_internal_function_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
void tinypy_internal_method_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_method_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_cell_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_slice_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_slice_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_module_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_module_from_dict(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *dict);
tinypy_value_t *tinypy_internal_import_from(tinypy_value_t *module, const char *name, size_t name_size, tinypy_error_t **out_error);
int32_t tinypy_internal_import_star(tinypy_value_t *module, tinypy_value_t *locals, tinypy_error_t **out_error);
void tinypy_internal_native_function_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_native_function_destroy(tinypy_vm_t *vm, tinypy_value_t *value);
tinypy_value_t *tinypy_internal_native_function_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_callable_descriptor_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_static_method_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_class_method_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_static_method_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_class_method_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_property_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_c_descriptor_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_c_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
int32_t tinypy_internal_c_descriptor_set(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_member_descriptor_new(tinypy_type_t *owner, tinypy_value_t *name, size_t index);
tinypy_value_t *tinypy_internal_property_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
int32_t tinypy_internal_property_set(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_property_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_super_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_super_get_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_super_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_initialize_descriptor_types(tinypy_vm_t *vm);
int32_t tinypy_internal_descriptor_is_data(tinypy_value_t *attribute);
tinypy_value_t *tinypy_internal_descriptor_get_value(tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
int32_t tinypy_internal_descriptor_set_value(tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error);
int32_t tinypy_internal_descriptor_delete_value(tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_error_t **out_error);
void tinypy_internal_initialize_exceptions(tinypy_vm_t *vm);
tinypy_value_t *tinypy_internal_exception_instantiate(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_exception_raise_kind(tinypy_vm_t *vm, tinypy_error_kind_e kind, const char *message);
void tinypy_internal_exception_clear_raised(tinypy_vm_t *vm);
void tinypy_internal_exception_clear_handled(tinypy_vm_t *vm);
tinypy_compile_environment_t *tinypy_internal_compile_environment_create(tinypy_vm_t *vm, uint32_t feature_flags, int32_t optimize_level, const tinypy_build_profile_t *profile);
void tinypy_internal_compile_environment_retain(tinypy_compile_environment_t *environment);
void tinypy_internal_compile_environment_release(tinypy_compile_environment_t *environment);
uint32_t tinypy_internal_compile_environment_feature_flags(const tinypy_compile_environment_t *environment);
int32_t tinypy_internal_compile_environment_optimize_level(const tinypy_compile_environment_t *environment);
const tinypy_build_profile_t *tinypy_internal_compile_environment_build_profile(const tinypy_compile_environment_t *environment);
void tinypy_internal_code_attach_compile_environment(tinypy_value_t *code, tinypy_compile_environment_t *environment);
void tinypy_internal_code_attach_compile_options(tinypy_value_t *code, uint32_t feature_flags, int32_t optimize_level, const tinypy_build_profile_t *profile);
int32_t tinypy_internal_compile_options_inherit_frame(tinypy_vm_t *vm, tinypy_compile_options_t *options);
void tinypy_internal_exception_preserve_begin(tinypy_vm_t *vm, tinypy_internal_exception_state_t *state);
void tinypy_internal_exception_preserve_end(tinypy_vm_t *vm, tinypy_internal_exception_state_t *state);
void tinypy_internal_exception_set_raised(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *traceback);
void tinypy_internal_exception_set_handled_from_raised(tinypy_vm_t *vm);
void tinypy_internal_exception_restore_raised_from_handled(tinypy_vm_t *vm);
void tinypy_internal_exception_make_diagnostic(tinypy_vm_t *vm, tinypy_error_t **out_error);
void tinypy_internal_exception_raise_stop_iteration(tinypy_vm_t *vm, tinypy_error_t **out_error);
int32_t tinypy_internal_exception_consume_stop_iteration(tinypy_vm_t *vm, tinypy_error_t **out_error);
void tinypy_internal_traceback_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_traceback_new(tinypy_value_t *frame, tinypy_value_t *next);
void tinypy_internal_traceback_here(tinypy_vm_t *vm, tinypy_frame_object_t *frame);
void tinypy_internal_generator_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_generator_iter(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_generator_next(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_generator_from_frame(tinypy_value_t *frame);
void tinypy_internal_initialize_generator_types(tinypy_vm_t *vm);
void tinypy_internal_initialize_builtin_functions(tinypy_vm_t *vm);
void tinypy_internal_initialize_container_types(tinypy_vm_t *vm);
void tinypy_internal_initialize_string_types(tinypy_vm_t *vm);
void tinypy_internal_initialize_constructor_types(tinypy_vm_t *vm);
tinypy_value_t *tinypy_internal_string_percent(tinypy_value_t *format, tinypy_value_t *arguments, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_xrange_new(tinypy_vm_t *vm, int64_t start, int64_t step, size_t length);
tinypy_value_t *tinypy_internal_xrange_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_enumerate_new(tinypy_value_t *iterable, int64_t start, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_reversed_new(tinypy_value_t *sequence, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_xrange_iter(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_enumerate_iter(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_enumerate_next(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_reversed_iter(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_reversed_next(tinypy_value_t *value, tinypy_error_t **out_error);
void tinypy_internal_enumerate_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_reversed_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_buffer_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_buffer_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_buffer_get_item(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error);
ptrdiff_t tinypy_internal_buffer_length(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_buffer_repr(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_buffer_string(tinypy_value_t *value, tinypy_error_t **out_error);

tinypy_hash_t tinypy_internal_hash_value(const tinypy_value_t *value);
int32_t tinypy_internal_equal_value(const tinypy_value_t *left, const tinypy_value_t *right, int identity_implies_equal);
int tinypy_internal_numeric_order(const tinypy_value_t *left, const tinypy_value_t *right, int32_t *out_order);
int32_t tinypy_internal_text_order(const tinypy_value_t *left, const tinypy_value_t *right);

#endif
