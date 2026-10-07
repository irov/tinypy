#ifndef TINYPY_CORE_INTERNAL_H
#define TINYPY_CORE_INTERNAL_H

#if defined(_MSC_VER)
#include <intrin.h>
#define TINYPY_NATIVE_STACK_ADDRESS() ((uintptr_t)_AddressOfReturnAddress())
#elif defined(__GNUC__) || defined(__clang__)
#define TINYPY_NATIVE_STACK_ADDRESS() ((uintptr_t)__builtin_frame_address(0))
#else
#define TINYPY_NATIVE_STACK_ADDRESS() ((uintptr_t)0U)
#endif

#if defined(TINYPY_CYCLE_DIAGNOSTICS) && defined(NDEBUG)
#error "TINYPY_CYCLE_DIAGNOSTICS is Debug-only"
#endif

#if defined(TINYPY_DEBUGGER) && defined(NDEBUG)
#error "TINYPY_DEBUGGER is Debug-only"
#endif

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
#include "tinypy/debugger.h"
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

#include <string.h>
//////////////////////////////////////////////////////////////////////////
typedef union tinypy_internal_max_align_t {
    void *pointer_value;
    void (*function_value)(void);
    int64_t integer_value;
    long double floating_value;
} tinypy_internal_max_align_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_internal_alignment_probe_t {
    char prefix;
    tinypy_internal_max_align_t value;
} tinypy_internal_alignment_probe_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_internal_exception_state_t {
    tinypy_value_t *type;
    tinypy_value_t *value;
    tinypy_value_t *traceback;
} tinypy_internal_exception_state_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_intern_entry_t {
    tinypy_value_t *value;
    tinypy_hash_t hash;
} tinypy_intern_entry_t;
//////////////////////////////////////////////////////////////////////////
#define TINYPY_INTERNAL_ALIGNMENT \
    ((size_t)offsetof(tinypy_internal_alignment_probe_t, value))

#include "pool.h"

/* Operator and comparison hooks are looked up by name on every dispatch, so
   their keys are interned once per VM instead of being rebuilt each time. */
#define TINYPY_SPECIAL_OPERATOR_COUNT 24U
#define TINYPY_TYPE_FLAG_IMMUTABLE UINT64_C(1)
#define TINYPY_TYPE_FLAG_HEAP UINT64_C(2)
#define TINYPY_TYPE_FLAG_BASE_TYPE UINT64_C(4)
#define TINYPY_TYPE_FLAG_TYPE_SUBCLASS UINT64_C(8)
#define TINYPY_TYPE_FLAG_ABSTRACT UINT64_C(16)
#define TINYPY_TYPE_FLAG_PYTHON_HEAP UINT64_C(32)
#define TINYPY_VM_STATE_LIVE UINT32_C(0x5450594c)
#define TINYPY_VM_STATE_DESTROYING UINT32_C(0x54505944)
#define TINYPY_BUILTIN_TYPE_COUNT ((size_t)TINYPY_VALUE_NATIVE_INSTANCE)
#define TINYPY_FRAME_MAX_BLOCKS 20U
#define TINYPY_CODE_GLOBAL_CACHE_SIZE 16U
#define TINYPY_INTEGER_CONSTANT_MIN (-INT64_C(1023))
#define TINYPY_INTEGER_CONSTANT_MAX INT64_C(1024)
#define TINYPY_INTEGER_CONSTANT_COUNT 2048U
#define TINYPY_INTEGER_FREE_LIST_MAX 256U
#define TINYPY_FRAME_FREE_LIST_MAX 64U
#define TINYPY_METHOD_FREE_LIST_MAX 256U
#define TINYPY_NATIVE_METHOD_FREE_LIST_MAX 64U
#define TINYPY_INTERNAL_KEY_TABLE_SIZE 2048U
#define TINYPY_TYPE_LOOKUP_CACHE_SIZE 1024U
//////////////////////////////////////////////////////////////////////////
/* VM-owned names for dispatch, namespace/keyword lookup and compiler metadata.
   Each entry specifies whether to intern its initial string. The registry also
   supplies initialization and shutdown roots. Comprehension labels remain
   non-interned to preserve Python 2 marshal representation. */
#define TINYPY_INTERNAL_KEY_LIST(X) \
    X(internal_import_key, "__import__", 1) \
    X(internal_builtins_key, "__builtins__", 1) \
    X(internal_keys_key, "keys", 1) \
    X(internal_key_key, "key", 1) \
    X(internal_softspace_key, "softspace", 1) \
    X(internal_sys_key, "sys", 1) \
    X(internal_stdout_key, "stdout", 1) \
    X(internal_stderr_key, "stderr", 1) \
    X(internal_displayhook_key, "displayhook", 1) \
    X(internal_exc_type_key, "exc_type", 1) \
    X(internal_exc_value_key, "exc_value", 1) \
    X(internal_exc_traceback_key, "exc_traceback", 1) \
    X(internal_metaclass_key, "__metaclass__", 1) \
    X(internal_special_class_key, "__class__", 1) \
    X(internal_special_getattribute_key, "__getattribute__", 1) \
    X(internal_special_getattr_key, "__getattr__", 1) \
    X(internal_special_setattr_key, "__setattr__", 1) \
    X(internal_special_delattr_key, "__delattr__", 1) \
    X(internal_special_get_key, "__get__", 1) \
    X(internal_special_set_key, "__set__", 1) \
    X(internal_special_delete_key, "__delete__", 1) \
    X(internal_special_call_key, "__call__", 1) \
    X(internal_special_iter_key, "__iter__", 1) \
    X(internal_special_next_key, "next", 1) \
    X(internal_special_length_key, "__len__", 1) \
    X(internal_special_getitem_key, "__getitem__", 1) \
    X(internal_special_setitem_key, "__setitem__", 1) \
    X(internal_special_delitem_key, "__delitem__", 1) \
    X(internal_special_getslice_key, "__getslice__", 1) \
    X(internal_special_setslice_key, "__setslice__", 1) \
    X(internal_special_delslice_key, "__delslice__", 1) \
    X(internal_special_contains_key, "__contains__", 1) \
    X(internal_special_missing_key, "__missing__", 1) \
    X(internal_special_reversed_key, "__reversed__", 1) \
    X(internal_special_index_key, "__index__", 1) \
    X(internal_special_hash_key, "__hash__", 1) \
    X(internal_special_repr_key, "__repr__", 1) \
    X(internal_special_str_key, "__str__", 1) \
    X(internal_special_unicode_key, "__unicode__", 1) \
    X(internal_special_nonzero_key, "__nonzero__", 1) \
    X(internal_special_enter_key, "__enter__", 1) \
    X(internal_special_exit_key, "__exit__", 1) \
    X(internal_special_format_key, "__format__", 1) \
    X(internal_special_new_key, "__new__", 1) \
    X(internal_special_init_key, "__init__", 1) \
    X(internal_special_del_key, "__del__", 1) \
    X(internal_special_name_key, "__name__", 1) \
    X(internal_special_dir_key, "__dir__", 1) \
    X(internal_special_pow_key, "__pow__", 1) \
    X(internal_special_invert_key, "__invert__", 1) \
    X(internal_special_instancecheck_key, "__instancecheck__", 1) \
    X(internal_special_subclasscheck_key, "__subclasscheck__", 1) \
    X(internal_special_trunc_key, "__trunc__", 1) \
    X(internal_special_complex_key, "__complex__", 1) \
    X(internal_special_doc_key, "__doc__", 1) \
    X(internal_special_module_key, "__module__", 1) \
    X(internal_special_dict_key, "__dict__", 1) \
    X(internal_special_weakref_key, "__weakref__", 1) \
    X(internal_special_slots_key, "__slots__", 1) \
    X(internal_special_slotnames_key, "__slotnames__", 1) \
    X(internal_special_bases_key, "__bases__", 1) \
    X(internal_special_mro_key, "__mro__", 1) \
    X(internal_special_file_key, "__file__", 1) \
    X(internal_special_package_key, "__package__", 1) \
    X(internal_special_path_key, "__path__", 1) \
    X(internal_special_all_key, "__all__", 1) \
    X(internal_special_members_key, "__members__", 1) \
    X(internal_special_methods_key, "__methods__", 1) \
    X(internal_special_reduce_key, "__reduce__", 1) \
    X(internal_special_reduce_ex_key, "__reduce_ex__", 1) \
    X(internal_special_getnewargs_key, "__getnewargs__", 1) \
    X(internal_special_getstate_key, "__getstate__", 1) \
    X(internal_special_setstate_key, "__setstate__", 1) \
    X(internal_special_int_key, "__int__", 1) \
    X(internal_special_float_key, "__float__", 1) \
    X(internal_special_coerce_key, "__coerce__", 1) \
    X(internal_special_cmp_key, "__cmp__", 1) \
    X(internal_special_abstractmethods_key, "__abstractmethods__", 1) \
    X(internal_special_sizeof_key, "__sizeof__", 1) \
    X(internal_special_alloc_key, "__alloc__", 1) \
    X(internal_special_and_key, "__and__", 1) \
    X(internal_special_base_key, "__base__", 1) \
    X(internal_special_basicsize_key, "__basicsize__", 1) \
    X(internal_special_closure_key, "__closure__", 1) \
    X(internal_special_code_key, "__code__", 1) \
    X(internal_special_debug_key, "__debug__", 1) \
    X(internal_special_defaults_key, "__defaults__", 1) \
    X(internal_special_dictoffset_key, "__dictoffset__", 1) \
    X(internal_special_displayhook_key, "__displayhook__", 1) \
    X(internal_special_flags_key, "__flags__", 1) \
    X(internal_special_floordiv_key, "__floordiv__", 1) \
    X(internal_special_func_key, "__func__", 1) \
    X(internal_special_getformat_key, "__getformat__", 1) \
    X(internal_special_globals_key, "__globals__", 1) \
    X(internal_special_hex_key, "__hex__", 1) \
    X(internal_special_iadd_key, "__iadd__", 1) \
    X(internal_special_iand_key, "__iand__", 1) \
    X(internal_special_idiv_key, "__idiv__", 1) \
    X(internal_special_ifloordiv_key, "__ifloordiv__", 1) \
    X(internal_special_ilshift_key, "__ilshift__", 1) \
    X(internal_special_imod_key, "__imod__", 1) \
    X(internal_special_imul_key, "__imul__", 1) \
    X(internal_special_ior_key, "__ior__", 1) \
    X(internal_special_ipow_key, "__ipow__", 1) \
    X(internal_special_irshift_key, "__irshift__", 1) \
    X(internal_special_isub_key, "__isub__", 1) \
    X(internal_special_itemsize_key, "__itemsize__", 1) \
    X(internal_special_itruediv_key, "__itruediv__", 1) \
    X(internal_special_ixor_key, "__ixor__", 1) \
    X(internal_special_length_hint_key, "__length_hint__", 1) \
    X(internal_special_long_key, "__long__", 1) \
    X(internal_special_lshift_key, "__lshift__", 1) \
    X(internal_special_newobj_key, "__newobj__", 1) \
    X(internal_special_objclass_key, "__objclass__", 1) \
    X(internal_special_oct_key, "__oct__", 1) \
    X(internal_special_or_key, "__or__", 1) \
    X(internal_special_rand_key, "__rand__", 1) \
    X(internal_special_rdivmod_key, "__rdivmod__", 1) \
    X(internal_special_rfloordiv_key, "__rfloordiv__", 1) \
    X(internal_special_rlshift_key, "__rlshift__", 1) \
    X(internal_special_rmod_key, "__rmod__", 1) \
    X(internal_special_ror_key, "__ror__", 1) \
    X(internal_special_rpow_key, "__rpow__", 1) \
    X(internal_special_rrshift_key, "__rrshift__", 1) \
    X(internal_special_rshift_key, "__rshift__", 1) \
    X(internal_special_rtruediv_key, "__rtruediv__", 1) \
    X(internal_special_rxor_key, "__rxor__", 1) \
    X(internal_special_self_key, "__self__", 1) \
    X(internal_special_self_class_key, "__self_class__", 1) \
    X(internal_special_setformat_key, "__setformat__", 1) \
    X(internal_special_stderr_key, "__stderr__", 1) \
    X(internal_special_stdout_key, "__stdout__", 1) \
    X(internal_special_subclasses_key, "__subclasses__", 1) \
    X(internal_special_subclasshook_key, "__subclasshook__", 1) \
    X(internal_special_thisclass_key, "__thisclass__", 1) \
    X(internal_special_version_key, "__version__", 1) \
    X(internal_special_weakrefoffset_key, "__weakrefoffset__", 1) \
    X(internal_special_xor_key, "__xor__", 1) \
    X(internal_name_key, "name", 1) \
    X(internal_doc_key, "doc", 1) \
    X(internal_bases_key, "bases", 1) \
    X(internal_dict_key, "dict", 1) \
    X(internal_sequence_key, "sequence", 1) \
    X(internal_start_key, "start", 1) \
    X(internal_stop_key, "stop", 1) \
    X(internal_step_key, "step", 1) \
    X(internal_encoding_key, "encoding", 1) \
    X(internal_errors_key, "errors", 1) \
    X(internal_reverse_key, "reverse", 1) \
    X(internal_cmp_key, "cmp", 1) \
    X(internal_default_key, "default", 1) \
    X(internal_underscore_key, "_", 1) \
    X(internal_object_key, "object", 1) \
    X(internal_end_key, "end", 1) \
    X(internal_reason_key, "reason", 1) \
    X(internal_message_key, "message", 1) \
    X(internal_args_key, "args", 1) \
    X(internal_errno_key, "errno", 1) \
    X(internal_strerror_key, "strerror", 1) \
    X(internal_filename_key, "filename", 1) \
    X(internal_lineno_key, "lineno", 1) \
    X(internal_offset_key, "offset", 1) \
    X(internal_text_key, "text", 1) \
    X(internal_msg_key, "msg", 1) \
    X(internal_code_key, "code", 1) \
    X(internal_base_key, "base", 1) \
    X(internal_real_key, "real", 1) \
    X(internal_imag_key, "imag", 1) \
    X(internal_string_key, "string", 1) \
    X(internal_buffer_key, "buffer", 1) \
    X(internal_count_key, "count", 1) \
    X(internal_signed_key, "signed", 1) \
    X(internal_source_key, "source", 1) \
    X(internal_flags_key, "flags", 1) \
    X(internal_dont_inherit_key, "dont_inherit", 1) \
    X(internal_mode_key, "mode", 1) \
    X(internal_value_key, "value", 1) \
    X(internal_iterable_key, "iterable", 1) \
    X(internal_function_key, "function", 1) \
    X(internal_sep_key, "sep", 1) \
    X(internal_file_key, "file", 1) \
    X(internal_write_key, "write", 1) \
    X(internal_join_key, "join", 1) \
    X(internal_sort_key, "sort", 1) \
    X(internal_decode_key, "decode", 1) \
    X(internal_encode_key, "encode", 1) \
    X(internal_find_module_key, "find_module", 1) \
    X(internal_load_module_key, "load_module", 1) \
    X(internal_number_key, "number", 1) \
    X(internal_ndigits_key, "ndigits", 1) \
    X(internal_globals_key, "globals", 1) \
    X(internal_locals_key, "locals", 1) \
    X(internal_fromlist_key, "fromlist", 1) \
    X(internal_level_key, "level", 1) \
    X(internal_argdefs_key, "argdefs", 1) \
    X(internal_closure_key, "closure", 1) \
    X(internal_x_key, "x", 1) \
    X(internal_fget_key, "fget", 1) \
    X(internal_fset_key, "fset", 1) \
    X(internal_fdel_key, "fdel", 1) \
    X(internal_pos_key, "pos", 1) \
    X(internal_endpos_key, "endpos", 1) \
    X(internal_pattern_key, "pattern", 1) \
    X(internal_maxsplit_key, "maxsplit", 1) \
    X(internal_repl_key, "repl", 1) \
    X(internal_codec_cache_key, "_cache", 1) \
    X(internal_codec_search_path_key, "_search_path", 1) \
    X(internal_codec_errors_key, "_errors", 1) \
    X(internal_codec_ascii_name, "ascii", 1) \
    X(internal_codec_utf8_name, "utf8", 1) \
    X(internal_codec_latin1_name, "latin-1", 1) \
    X(internal_codec_unicode_escape_name, "unicodeescape", 1) \
    X(internal_codec_raw_unicode_escape_name, "rawunicodeescape", 1) \
    X(internal_codec_decimal_name, "decimal", 1) \
    X(internal_codec_strict_name, "strict", 1) \
    X(internal_builtin_module_name, "__builtin__", 1) \
    X(internal_future_module_name, "__future__", 1) \
    X(internal_exception_module_name, "exceptions", 1) \
    X(internal_struct_module_name, "_struct", 1) \
    X(internal_sre_module_name, "_sre", 1) \
    X(internal_codec_module_name, "_codecs", 1) \
    X(internal_copy_reg_module_name, "copy_reg", 1) \
    X(internal_partial_module_name, "_functools", 1) \
    X(internal_functools_module_name, "functools", 1) \
    X(internal_functools_dot_partial_key, "functools.partial", 1) \
    X(internal_weakref_module_name, "_weakref", 1) \
    X(internal_compiler_module_name, "<module>", 1) \
    X(internal_compiler_lambda_name, "<lambda>", 1) \
    X(internal_compiler_genexpr_name, "<genexpr>", 0) \
    X(internal_compiler_setcomp_name, "<setcomp>", 0) \
    X(internal_compiler_dictcomp_name, "<dictcomp>", 0) \
    X(internal_compiler_symbol_top_name, "top", 0) \
    X(internal_compiler_symbol_lambda_name, "lambda", 0) \
    X(internal_compiler_symbol_genexpr_name, "genexpr", 0) \
    X(internal_compiler_symbol_setcomp_name, "setcomp", 0) \
    X(internal_compiler_symbol_dictcomp_name, "dictcomp", 0) \
    X(internal_assertion_error_key, "AssertionError", 1) \
    X(internal_none_key, "None", 1) \
    X(internal_special_abs_key, "__abs__", 1) \
    X(internal_special_add_key, "__add__", 1) \
    X(internal_special_div_key, "__div__", 1) \
    X(internal_special_divmod_key, "__divmod__", 1) \
    X(internal_special_eq_key, "__eq__", 1) \
    X(internal_special_ge_key, "__ge__", 1) \
    X(internal_special_gt_key, "__gt__", 1) \
    X(internal_special_le_key, "__le__", 1) \
    X(internal_special_lt_key, "__lt__", 1) \
    X(internal_special_mod_key, "__mod__", 1) \
    X(internal_special_mul_key, "__mul__", 1) \
    X(internal_special_ne_key, "__ne__", 1) \
    X(internal_special_neg_key, "__neg__", 1) \
    X(internal_special_pos_key, "__pos__", 1) \
    X(internal_special_radd_key, "__radd__", 1) \
    X(internal_special_rdiv_key, "__rdiv__", 1) \
    X(internal_special_remove_subclass_key, "__remove_subclass", 1) \
    X(internal_special_rmul_key, "__rmul__", 1) \
    X(internal_special_rsub_key, "__rsub__", 1) \
    X(internal_special_sub_key, "__sub__", 1) \
    X(internal_special_truediv_key, "__truediv__", 1) \
    X(internal_formatter_field_name_split_key, "_formatter_field_name_split", 1) \
    X(internal_formatter_parser_key, "_formatter_parser", 1) \
    X(internal_append_key, "append", 1) \
    X(internal_as_integer_ratio_key, "as_integer_ratio", 1) \
    X(internal_bit_length_key, "bit_length", 1) \
    X(internal_capitalize_key, "capitalize", 1) \
    X(internal_center_key, "center", 1) \
    X(internal_clear_key, "clear", 1) \
    X(internal_conjugate_key, "conjugate", 1) \
    X(internal_copy_key, "copy", 1) \
    X(internal_denominator_key, "denominator", 1) \
    X(internal_endswith_key, "endswith", 1) \
    X(internal_expand_key, "expand", 1) \
    X(internal_true_key, "True", 1) \
    X(internal_false_key, "False", 1) \
    X(internal_meta_name_key, "meta", 1) \
    X(internal_meta_emit_key, "emit", 1) \
    X(internal_meta_rename_key, "rename", 1) \
    X(internal_meta_template_key, "template", 1) \
    X(internal_meta_current_class_key, "current_class", 1) \
    X(internal_meta_concat_key, "concat", 1) \
    X(internal_range_key, "range", 1) \
    X(internal_getattr_key, "getattr", 1) \
    X(internal_setattr_key, "setattr", 1) \
    X(internal_delattr_key, "delattr", 1) \
    X(internal_expandtabs_key, "expandtabs", 1) \
    X(internal_extend_key, "extend", 1) \
    X(internal_find_key, "find", 1) \
    X(internal_findall_key, "findall", 1) \
    X(internal_finditer_key, "finditer", 1) \
    X(internal_format_key, "format", 1) \
    X(internal_fromhex_key, "fromhex", 1) \
    X(internal_get_key, "get", 1) \
    X(internal_get_mandatory_release_key, "getMandatoryRelease", 1) \
    X(internal_get_optional_release_key, "getOptionalRelease", 1) \
    X(internal_group_key, "group", 1) \
    X(internal_groupdict_key, "groupdict", 1) \
    X(internal_groups_key, "groups", 1) \
    X(internal_has_key_key, "has_key", 1) \
    X(internal_hex_key, "hex", 1) \
    X(internal_hex_decode_key, "hex_decode", 1) \
    X(internal_hex_encode_key, "hex_encode", 1) \
    X(internal_index_key, "index", 1) \
    X(internal_indices_key, "indices", 1) \
    X(internal_insert_key, "insert", 1) \
    X(internal_is_integer_key, "is_integer", 1) \
    X(internal_isalnum_key, "isalnum", 1) \
    X(internal_isalpha_key, "isalpha", 1) \
    X(internal_isdecimal_key, "isdecimal", 1) \
    X(internal_isdigit_key, "isdigit", 1) \
    X(internal_islower_key, "islower", 1) \
    X(internal_isnumeric_key, "isnumeric", 1) \
    X(internal_isspace_key, "isspace", 1) \
    X(internal_istitle_key, "istitle", 1) \
    X(internal_isupper_key, "isupper", 1) \
    X(internal_items_key, "items", 1) \
    X(internal_itemsize_key, "itemsize", 1) \
    X(internal_iteritems_key, "iteritems", 1) \
    X(internal_iterkeys_key, "iterkeys", 1) \
    X(internal_itervalues_key, "itervalues", 1) \
    X(internal_ljust_key, "ljust", 1) \
    X(internal_lower_key, "lower", 1) \
    X(internal_lstrip_key, "lstrip", 1) \
    X(internal_match_key, "match", 1) \
    X(internal_mro_key, "mro", 1) \
    X(internal_ndim_key, "ndim", 1) \
    X(internal_numerator_key, "numerator", 1) \
    X(internal_partition_key, "partition", 1) \
    X(internal_pop_key, "pop", 1) \
    X(internal_popitem_key, "popitem", 1) \
    X(internal_readonly_key, "readonly", 1) \
    X(internal_reduce_key, "reduce", 1) \
    X(internal_remove_key, "remove", 1) \
    X(internal_replace_key, "replace", 1) \
    X(internal_rfind_key, "rfind", 1) \
    X(internal_rindex_key, "rindex", 1) \
    X(internal_rjust_key, "rjust", 1) \
    X(internal_rpartition_key, "rpartition", 1) \
    X(internal_rsplit_key, "rsplit", 1) \
    X(internal_rstrip_key, "rstrip", 1) \
    X(internal_scanner_key, "scanner", 1) \
    X(internal_search_key, "search", 1) \
    X(internal_setdefault_key, "setdefault", 1) \
    X(internal_shape_key, "shape", 1) \
    X(internal_span_key, "span", 1) \
    X(internal_split_key, "split", 1) \
    X(internal_splitlines_key, "splitlines", 1) \
    X(internal_startswith_key, "startswith", 1) \
    X(internal_strides_key, "strides", 1) \
    X(internal_strip_key, "strip", 1) \
    X(internal_sub_key, "sub", 1) \
    X(internal_subn_key, "subn", 1) \
    X(internal_suboffsets_key, "suboffsets", 1) \
    X(internal_swapcase_key, "swapcase", 1) \
    X(internal_title_key, "title", 1) \
    X(internal_tobytes_key, "tobytes", 1) \
    X(internal_tolist_key, "tolist", 1) \
    X(internal_translate_key, "translate", 1) \
    X(internal_update_key, "update", 1) \
    X(internal_upper_key, "upper", 1) \
    X(internal_utf_8_decode_key, "utf_8_decode", 1) \
    X(internal_values_key, "values", 1) \
    X(internal_viewitems_key, "viewitems", 1) \
    X(internal_viewkeys_key, "viewkeys", 1) \
    X(internal_viewvalues_key, "viewvalues", 1) \
    X(internal_zfill_key, "zfill", 1) \
    X(internal_codec_646_key, "646", 1) \
    X(internal_arithmetic_error_key, "ArithmeticError", 1) \
    X(internal_attribute_error_key, "AttributeError", 1) \
    X(internal_base_exception_key, "BaseException", 1) \
    X(internal_buffer_error_key, "BufferError", 1) \
    X(internal_bytes_warning_key, "BytesWarning", 1) \
    X(internal_codesize_key, "CODESIZE", 1) \
    X(internal_co_future_absolute_import_key, "CO_FUTURE_ABSOLUTE_IMPORT", 1) \
    X(internal_co_future_division_key, "CO_FUTURE_DIVISION", 1) \
    X(internal_co_future_print_function_key, "CO_FUTURE_PRINT_FUNCTION", 1) \
    X(internal_co_future_unicode_literals_key, "CO_FUTURE_UNICODE_LITERALS", 1) \
    X(internal_co_future_with_statement_key, "CO_FUTURE_WITH_STATEMENT", 1) \
    X(internal_co_generator_allowed_key, "CO_GENERATOR_ALLOWED", 1) \
    X(internal_co_nested_key, "CO_NESTED", 1) \
    X(internal_callable_proxy_type_key, "CallableProxyType", 1) \
    X(internal_deprecation_warning_key, "DeprecationWarning", 1) \
    X(internal_eof_error_key, "EOFError", 1) \
    X(internal_ellipsis_key, "Ellipsis", 1) \
    X(internal_environment_error_key, "EnvironmentError", 1) \
    X(internal_exception_key, "Exception", 1) \
    X(internal_floating_point_error_key, "FloatingPointError", 1) \
    X(internal_future_warning_key, "FutureWarning", 1) \
    X(internal_generator_exit_key, "GeneratorExit", 1) \
    X(internal_io_error_key, "IOError", 1) \
    X(internal_import_error_key, "ImportError", 1) \
    X(internal_import_warning_key, "ImportWarning", 1) \
    X(internal_indentation_error_key, "IndentationError", 1) \
    X(internal_index_error_key, "IndexError", 1) \
    X(internal_key_error_key, "KeyError", 1) \
    X(internal_keyboard_interrupt_key, "KeyboardInterrupt", 1) \
    X(internal_lookup_error_key, "LookupError", 1) \
    X(internal_magic_key, "MAGIC", 1) \
    X(internal_maxrepeat_key, "MAXREPEAT", 1) \
    X(internal_memory_error_key, "MemoryError", 1) \
    X(internal_name_error_key, "NameError", 1) \
    X(internal_none_type_key, "NoneType", 1) \
    X(internal_not_implemented_key, "NotImplemented", 1) \
    X(internal_not_implemented_error_key, "NotImplementedError", 1) \
    X(internal_not_implemented_type_key, "NotImplementedType", 1) \
    X(internal_os_error_key, "OSError", 1) \
    X(internal_overflow_error_key, "OverflowError", 1) \
    X(internal_pending_deprecation_warning_key, "PendingDeprecationWarning", 1) \
    X(internal_proxy_type_key, "ProxyType", 1) \
    X(internal_reference_error_key, "ReferenceError", 1) \
    X(internal_reference_type_key, "ReferenceType", 1) \
    X(internal_runtime_error_key, "RuntimeError", 1) \
    X(internal_runtime_warning_key, "RuntimeWarning", 1) \
    X(internal_sre_scanner_key, "SRE_Scanner", 1) \
    X(internal_sre_pattern_key, "SRE_Pattern", 1) \
    X(internal_sre_match_key, "SRE_Match", 1) \
    X(internal_standard_error_key, "StandardError", 1) \
    X(internal_stop_iteration_key, "StopIteration", 1) \
    X(internal_struct_key, "Struct", 1) \
    X(internal_syntax_error_key, "SyntaxError", 1) \
    X(internal_syntax_warning_key, "SyntaxWarning", 1) \
    X(internal_system_error_key, "SystemError", 1) \
    X(internal_system_exit_key, "SystemExit", 1) \
    X(internal_tab_error_key, "TabError", 1) \
    X(internal_type_error_key, "TypeError", 1) \
    X(internal_unbound_local_error_key, "UnboundLocalError", 1) \
    X(internal_unicode_decode_error_key, "UnicodeDecodeError", 1) \
    X(internal_unicode_encode_error_key, "UnicodeEncodeError", 1) \
    X(internal_unicode_error_key, "UnicodeError", 1) \
    X(internal_unicode_translate_error_key, "UnicodeTranslateError", 1) \
    X(internal_unicode_warning_key, "UnicodeWarning", 1) \
    X(internal_user_warning_key, "UserWarning", 1) \
    X(internal_value_error_key, "ValueError", 1) \
    X(internal_warning_key, "Warning", 1) \
    X(internal_windows_error_key, "WindowsError", 1) \
    X(internal_zero_division_error_key, "ZeroDivisionError", 1) \
    X(internal_class_type_key, "_ClassType", 1) \
    X(internal_feature_key, "_Feature", 1) \
    X(internal_heaptype_key, "_HEAPTYPE", 1) \
    X(internal_py_struct_float_coerce_key, "_PY_STRUCT_FLOAT_COERCE", 1) \
    X(internal_py_struct_range_checking_key, "_PY_STRUCT_RANGE_CHECKING", 1) \
    X(internal_clearcache_key, "_clearcache", 1) \
    X(internal_sre_expand_key, "_expand", 1) \
    X(internal_extension_cache_key, "_extension_cache", 1) \
    X(internal_extension_registry_key, "_extension_registry", 1) \
    X(internal_struct_private_format_key, "_format", 1) \
    X(internal_getframe_key, "_getframe", 1) \
    X(internal_inverted_registry_key, "_inverted_registry", 1) \
    X(internal_reconstructor_key, "_reconstructor", 1) \
    X(internal_reduce_ex_key, "_reduce_ex", 1) \
    X(internal_remove_dead_weakref_key, "_remove_dead_weakref", 1) \
    X(internal_struct_private_size_key, "_size", 1) \
    X(internal_slotnames_key, "_slotnames", 1) \
    X(internal_sre_dot_sre_match_key, "_sre.SRE_Match", 1) \
    X(internal_sre_dot_sre_pattern_key, "_sre.SRE_Pattern", 1) \
    X(internal_sre_dot_sre_scanner_key, "_sre.SRE_Scanner", 1) \
    X(internal_subx_key, "_subx", 1) \
    X(internal_abs_key, "abs", 1) \
    X(internal_future_absolute_import_key, "absolute_import", 1) \
    X(internal_add_key, "add", 1) \
    X(internal_add_extension_key, "add_extension", 1) \
    X(internal_all_key, "all", 1) \
    X(internal_all_feature_names_key, "all_feature_names", 1) \
    X(internal_ansi_hyphen_x3_dot_4_hyphen_1968_key, "ansi-x3.4-1968", 1) \
    X(internal_ansi_x3_dot_4_1968_key, "ansi_x3.4_1968", 1) \
    X(internal_any_key, "any", 1) \
    X(internal_api_version_key, "api_version", 1) \
    X(internal_apply_key, "apply", 1) \
    X(internal_ascii_decode_key, "ascii_decode", 1) \
    X(internal_ascii_encode_key, "ascii_encode", 1) \
    X(internal_backslashreplace_key, "backslashreplace", 1) \
    X(internal_basestring_key, "basestring", 1) \
    X(internal_bin_key, "bin", 1) \
    X(internal_bool_key, "bool", 1) \
    X(internal_builtin_function_or_method_key, "builtin_function_or_method", 1) \
    X(internal_builtin_module_names_key, "builtin_module_names", 1) \
    X(internal_bytearray_key, "bytearray", 1) \
    X(internal_byteorder_key, "byteorder", 1) \
    X(internal_bytes_key, "bytes", 1) \
    X(internal_calcsize_key, "calcsize", 1) \
    X(internal_callable_key, "callable", 1) \
    X(internal_callable_hyphen_iterator_key, "callable-iterator", 1) \
    X(internal_cell_key, "cell", 1) \
    X(internal_cell_contents_key, "cell_contents", 1) \
    X(internal_chr_key, "chr", 1) \
    X(internal_classmethod_key, "classmethod", 1) \
    X(internal_classobj_key, "classobj", 1) \
    X(internal_clear_extension_cache_key, "clear_extension_cache", 1) \
    X(internal_close_key, "close", 1) \
    X(internal_co_argcount_key, "co_argcount", 1) \
    X(internal_co_cellvars_key, "co_cellvars", 1) \
    X(internal_co_code_key, "co_code", 1) \
    X(internal_co_consts_key, "co_consts", 1) \
    X(internal_co_filename_key, "co_filename", 1) \
    X(internal_co_firstlineno_key, "co_firstlineno", 1) \
    X(internal_co_flags_key, "co_flags", 1) \
    X(internal_co_freevars_key, "co_freevars", 1) \
    X(internal_co_lnotab_key, "co_lnotab", 1) \
    X(internal_co_name_key, "co_name", 1) \
    X(internal_co_names_key, "co_names", 1) \
    X(internal_co_nlocals_key, "co_nlocals", 1) \
    X(internal_co_stacksize_key, "co_stacksize", 1) \
    X(internal_co_varnames_key, "co_varnames", 1) \
    X(internal_coerce_key, "coerce", 1) \
    X(internal_compile_key, "compile", 1) \
    X(internal_compiler_flag_key, "compiler_flag", 1) \
    X(internal_complex_key, "complex", 1) \
    X(internal_constructor_key, "constructor", 1) \
    X(internal_copyright_key, "copyright", 1) \
    X(internal_cp819_key, "cp819", 1) \
    X(internal_deleter_key, "deleter", 1) \
    X(internal_dict_items_key, "dict_items", 1) \
    X(internal_dict_keys_key, "dict_keys", 1) \
    X(internal_dict_values_key, "dict_values", 1) \
    X(internal_dictionary_hyphen_itemiterator_key, "dictionary-itemiterator", 1) \
    X(internal_dictionary_hyphen_keyiterator_key, "dictionary-keyiterator", 1) \
    X(internal_dictionary_hyphen_valueiterator_key, "dictionary-valueiterator", 1) \
    X(internal_dictproxy_key, "dictproxy", 1) \
    X(internal_difference_key, "difference", 1) \
    X(internal_difference_update_key, "difference_update", 1) \
    X(internal_dig_key, "dig", 1) \
    X(internal_dir_key, "dir", 1) \
    X(internal_discard_key, "discard", 1) \
    X(internal_dispatch_table_key, "dispatch_table", 1) \
    X(internal_future_division_key, "division", 1) \
    X(internal_divmod_key, "divmod", 1) \
    X(internal_dont_write_bytecode_key, "dont_write_bytecode", 1) \
    X(internal_ellipsis_type_name, "ellipsis", 1) \
    X(internal_enumerate_key, "enumerate", 1) \
    X(internal_epsilon_key, "epsilon", 1) \
    X(internal_error_key, "error", 1) \
    X(internal_eval_key, "eval", 1) \
    X(internal_exc_clear_key, "exc_clear", 1) \
    X(internal_exc_info_key, "exc_info", 1) \
    X(internal_exit_key, "exit", 1) \
    X(internal_f_back_key, "f_back", 1) \
    X(internal_f_builtins_key, "f_builtins", 1) \
    X(internal_f_code_key, "f_code", 1) \
    X(internal_f_exc_traceback_key, "f_exc_traceback", 1) \
    X(internal_f_exc_type_key, "f_exc_type", 1) \
    X(internal_f_exc_value_key, "f_exc_value", 1) \
    X(internal_f_globals_key, "f_globals", 1) \
    X(internal_f_lasti_key, "f_lasti", 1) \
    X(internal_f_lineno_key, "f_lineno", 1) \
    X(internal_f_locals_key, "f_locals", 1) \
    X(internal_f_restricted_key, "f_restricted", 1) \
    X(internal_f_trace_key, "f_trace", 1) \
    X(internal_filter_key, "filter", 1) \
    X(internal_float_key, "float", 1) \
    X(internal_float_info_key, "float_info", 1) \
    X(internal_float_repr_style_key, "float_repr_style", 1) \
    X(internal_flush_key, "flush", 1) \
    X(internal_frame_key, "frame", 1) \
    X(internal_fromkeys_key, "fromkeys", 1) \
    X(internal_frozenset_key, "frozenset", 1) \
    X(internal_func_key, "func", 1) \
    X(internal_func_closure_key, "func_closure", 1) \
    X(internal_func_code_key, "func_code", 1) \
    X(internal_func_defaults_key, "func_defaults", 1) \
    X(internal_func_dict_key, "func_dict", 1) \
    X(internal_func_doc_key, "func_doc", 1) \
    X(internal_func_globals_key, "func_globals", 1) \
    X(internal_func_name_key, "func_name", 1) \
    X(internal_generator_key, "generator", 1) \
    X(internal_future_generators_key, "generators", 1) \
    X(internal_getcodesize_key, "getcodesize", 1) \
    X(internal_getdefaultencoding_key, "getdefaultencoding", 1) \
    X(internal_getlower_key, "getlower", 1) \
    X(internal_getrecursionlimit_key, "getrecursionlimit", 1) \
    X(internal_getset_descriptor_key, "getset_descriptor", 1) \
    X(internal_getsizeof_key, "getsizeof", 1) \
    X(internal_getter_key, "getter", 1) \
    X(internal_getweakrefcount_key, "getweakrefcount", 1) \
    X(internal_getweakrefs_key, "getweakrefs", 1) \
    X(internal_gi_code_key, "gi_code", 1) \
    X(internal_gi_frame_key, "gi_frame", 1) \
    X(internal_gi_running_key, "gi_running", 1) \
    X(internal_groupindex_key, "groupindex", 1) \
    X(internal_hasattr_key, "hasattr", 1) \
    X(internal_hash_key, "hash", 1) \
    X(internal_hex_hyphen_codec_key, "hex-codec", 1) \
    X(internal_hex_codec_key, "hex_codec", 1) \
    X(internal_hexversion_key, "hexversion", 1) \
    X(internal_id_key, "id", 1) \
    X(internal_ignore_key, "ignore", 1) \
    X(internal_im_class_key, "im_class", 1) \
    X(internal_im_func_key, "im_func", 1) \
    X(internal_im_self_key, "im_self", 1) \
    X(internal_instance_key, "instance", 1) \
    X(internal_instancemethod_key, "instancemethod", 1) \
    X(internal_int_key, "int", 1) \
    X(internal_intern_key, "intern", 1) \
    X(internal_intersection_key, "intersection", 1) \
    X(internal_intersection_update_key, "intersection_update", 1) \
    X(internal_isatty_key, "isatty", 1) \
    X(internal_isdisjoint_key, "isdisjoint", 1) \
    X(internal_isinstance_key, "isinstance", 1) \
    X(internal_iso_hyphen_8859_hyphen_1_key, "iso-8859-1", 1) \
    X(internal_iso646_hyphen_us_key, "iso646-us", 1) \
    X(internal_iso8859_hyphen_1_key, "iso8859-1", 1) \
    X(internal_iso_8859_hyphen_1_key, "iso_8859-1", 1) \
    X(internal_issubclass_key, "issubclass", 1) \
    X(internal_issubset_key, "issubset", 1) \
    X(internal_issuperset_key, "issuperset", 1) \
    X(internal_iter_key, "iter", 1) \
    X(internal_iterator_key, "iterator", 1) \
    X(internal_keywords_key, "keywords", 1) \
    X(internal_l1_key, "l1", 1) \
    X(internal_lastgroup_key, "lastgroup", 1) \
    X(internal_lastindex_key, "lastindex", 1) \
    X(internal_latin1_key, "latin1", 1) \
    X(internal_latin_1_key, "latin_1", 1) \
    X(internal_latin_1_decode_key, "latin_1_decode", 1) \
    X(internal_latin_1_encode_key, "latin_1_encode", 1) \
    X(internal_len_key, "len", 1) \
    X(internal_list_key, "list", 1) \
    X(internal_listiterator_key, "listiterator", 1) \
    X(internal_listreverseiterator_key, "listreverseiterator", 1) \
    X(internal_long_key, "long", 1) \
    X(internal_lookup_key, "lookup", 1) \
    X(internal_codec_lookup_error_key, "lookup_error", 1) \
    X(internal_major_key, "major", 1) \
    X(internal_mandatory_key, "mandatory", 1) \
    X(internal_mant_dig_key, "mant_dig", 1) \
    X(internal_map_key, "map", 1) \
    X(internal_max_key, "max", 1) \
    X(internal_max_10_exp_key, "max_10_exp", 1) \
    X(internal_max_exp_key, "max_exp", 1) \
    X(internal_maxint_key, "maxint", 1) \
    X(internal_maxsize_key, "maxsize", 1) \
    X(internal_maxunicode_key, "maxunicode", 1) \
    X(internal_member_descriptor_key, "member_descriptor", 1) \
    X(internal_memoryview_key, "memoryview", 1) \
    X(internal_meta_path_key, "meta_path", 1) \
    X(internal_method_descriptor_key, "method_descriptor", 1) \
    X(internal_micro_key, "micro", 1) \
    X(internal_min_key, "min", 1) \
    X(internal_min_10_exp_key, "min_10_exp", 1) \
    X(internal_min_exp_key, "min_exp", 1) \
    X(internal_minor_key, "minor", 1) \
    X(internal_module_key, "module", 1) \
    X(internal_modules_key, "modules", 1) \
    X(internal_future_nested_scopes_key, "nested_scopes", 1) \
    X(internal_oct_key, "oct", 1) \
    X(internal_optional_key, "optional", 1) \
    X(internal_ord_key, "ord", 1) \
    X(internal_pack_key, "pack", 1) \
    X(internal_pack_into_key, "pack_into", 1) \
    X(internal_partial_key, "partial", 1) \
    X(internal_path_hooks_key, "path_hooks", 1) \
    X(internal_path_importer_cache_key, "path_importer_cache", 1) \
    X(internal_pickle_key, "pickle", 1) \
    X(internal_pickle_complex_key, "pickle_complex", 1) \
    X(internal_platform_key, "platform", 1) \
    X(internal_pow_key, "pow", 1) \
    X(internal_print_key, "print", 1) \
    X(internal_print_file_and_line_key, "print_file_and_line", 1) \
    X(internal_future_print_function_key, "print_function", 1) \
    X(internal_property_key, "property", 1) \
    X(internal_proxy_key, "proxy", 1) \
    X(internal_py3kwarning_key, "py3kwarning", 1) \
    X(internal_radix_key, "radix", 1) \
    X(internal_rangeiterator_key, "rangeiterator", 1) \
    X(internal_re_key, "re", 1) \
    X(internal_ref_key, "ref", 1) \
    X(internal_register_key, "register", 1) \
    X(internal_register_error_key, "register_error", 1) \
    X(internal_regs_key, "regs", 1) \
    X(internal_releaselevel_key, "releaselevel", 1) \
    X(internal_reload_key, "reload", 1) \
    X(internal_remove_extension_key, "remove_extension", 1) \
    X(internal_repr_key, "repr", 1) \
    X(internal_reversed_key, "reversed", 1) \
    X(internal_round_key, "round", 1) \
    X(internal_rounds_key, "rounds", 1) \
    X(internal_send_key, "send", 1) \
    X(internal_serial_key, "serial", 1) \
    X(internal_set_key, "set", 1) \
    X(internal_setiterator_key, "setiterator", 1) \
    X(internal_setrecursionlimit_key, "setrecursionlimit", 1) \
    X(internal_setter_key, "setter", 1) \
    X(internal_size_key, "size", 1) \
    X(internal_slice_key, "slice", 1) \
    X(internal_sorted_key, "sorted", 1) \
    X(internal_staticmethod_key, "staticmethod", 1) \
    X(internal_str_key, "str", 1) \
    X(internal_sum_key, "sum", 1) \
    X(internal_super_key, "super", 1) \
    X(internal_symmetric_difference_key, "symmetric_difference", 1) \
    X(internal_symmetric_difference_update_key, "symmetric_difference_update", 1) \
    X(internal_sys_dot_floatinfo_key, "sys.floatinfo", 1) \
    X(internal_sys_dot_version_info_key, "sys.version_info", 1) \
    X(internal_tb_frame_key, "tb_frame", 1) \
    X(internal_tb_lasti_key, "tb_lasti", 1) \
    X(internal_tb_lineno_key, "tb_lineno", 1) \
    X(internal_tb_next_key, "tb_next", 1) \
    X(internal_throw_key, "throw", 1) \
    X(internal_tinypy_dot_output_key, "tinypy.output", 1) \
    X(internal_traceback_key, "traceback", 1) \
    X(internal_tuple_key, "tuple", 1) \
    X(internal_tupleiterator_key, "tupleiterator", 1) \
    X(internal_type_key, "type", 1) \
    X(internal_u8_key, "u8", 1) \
    X(internal_unichr_key, "unichr", 1) \
    X(internal_unicode_key, "unicode", 1) \
    X(internal_future_unicode_literals_key, "unicode_literals", 1) \
    X(internal_union_key, "union", 1) \
    X(internal_unpack_key, "unpack", 1) \
    X(internal_unpack_from_key, "unpack_from", 1) \
    X(internal_us_hyphen_ascii_key, "us-ascii", 1) \
    X(internal_utf_key, "utf", 1) \
    X(internal_utf_hyphen_8_key, "utf-8", 1) \
    X(internal_utf_8_key, "utf_8", 1) \
    X(internal_utf_8_encode_key, "utf_8_encode", 1) \
    X(internal_vars_key, "vars", 1) \
    X(internal_version_key, "version", 1) \
    X(internal_version_info_key, "version_info", 1) \
    X(internal_warnoptions_key, "warnoptions", 1) \
    X(internal_weakcallableproxy_key, "weakcallableproxy", 1) \
    X(internal_weakproxy_key, "weakproxy", 1) \
    X(internal_weakref_key, "weakref", 1) \
    X(internal_future_with_statement_key, "with_statement", 1) \
    X(internal_wrapper_descriptor_key, "wrapper_descriptor", 1) \
    X(internal_method_wrapper_key, "method-wrapper", 1) \
    X(internal_bytearray_iterator_key, "bytearray_iterator", 1) \
    X(internal_writelines_key, "writelines", 1) \
    X(internal_xmlcharrefreplace_key, "xmlcharrefreplace", 1) \
    X(internal_xrange_key, "xrange", 1) \
    X(internal_zip_key, "zip", 1) \
    X(internal_exec_mode_key, "exec", 1) \
    X(internal_single_mode_key, "single", 1) \
    X(internal_double_key, "double", 1) \
    X(internal_unknown_key, "unknown", 1) \
    X(internal_ieee_little_endian_key, "IEEE, little-endian", 1) \
    X(internal_ieee_big_endian_key, "IEEE, big-endian", 1) \
    X(internal_future_braces_key, "braces", 1) \
    X(internal_compiler_star_name, "*", 1)
#define TINYPY_INTERNAL_KEY_COUNT_ENTRY(field, name, intern_name) + 1U
enum {
    TINYPY_INTERNAL_KEY_COUNT = 0U TINYPY_INTERNAL_KEY_LIST(TINYPY_INTERNAL_KEY_COUNT_ENTRY)
};
#undef TINYPY_INTERNAL_KEY_COUNT_ENTRY
typedef char tinypy_name_cache_must_have_free_slots_t[
    TINYPY_INTERNAL_KEY_COUNT <= TINYPY_INTERNAL_KEY_TABLE_SIZE / 2U ? 1 : -1];
//////////////////////////////////////////////////////////////////////////
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
    TINYPY_EXCEPTION_BUFFER_ERROR,
    TINYPY_EXCEPTION_TYPE_COUNT
} tinypy_exception_type_index_e;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_compile_environment_t tinypy_compile_environment_t;
typedef struct tinypy_internal_slice_indices_t {
    int64_t start;
    int64_t stop;
    int64_t step;
    size_t length;
} tinypy_internal_slice_indices_t;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
typedef struct tinypy_cycle_diagnostics_state_t tinypy_cycle_diagnostics_state_t;
#endif
//////////////////////////////////////////////////////////////////////////
/* Complete universal tinypy object header. */
struct tinypy_value_t {
    tinypy_ref_t ref;
    tinypy_type_t *type;
};
//////////////////////////////////////////////////////////////////////////
typedef char tinypy_object_ref_must_be_first_t[offsetof(tinypy_value_t, ref) == 0U ? 1 : -1];
typedef char tinypy_object_type_must_follow_refcount_t[offsetof(tinypy_value_t, type) == sizeof(tinypy_ref_t) ? 1 : -1];
typedef char tinypy_object_header_has_only_two_fields_t[sizeof(tinypy_value_t) == sizeof(tinypy_ref_t) + sizeof(tinypy_type_t *) ? 1 : -1];
//////////////////////////////////////////////////////////////////////////
/* Header for tinypy objects with a non-negative logical size. */
typedef struct tinypy_sized_object_t {
    tinypy_value_t base;
    size_t size;
} tinypy_sized_object_t;
//////////////////////////////////////////////////////////////////////////
typedef char tinypy_sized_size_must_follow_header_t[offsetof(tinypy_sized_object_t, size) == sizeof(tinypy_value_t) ? 1 : -1];
//////////////////////////////////////////////////////////////////////////
typedef void (*tinypy_release_callback_t)(tinypy_value_t *value, void *user_data);
typedef void (*tinypy_release_references_slot_t)(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
typedef void (*tinypy_destroy_slot_t)(tinypy_value_t *value);
typedef tinypy_value_t *(*tinypy_unary_slot_t)(tinypy_value_t *value, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_binary_slot_t)(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_ternary_slot_t)(tinypy_value_t *first, tinypy_value_t *second, tinypy_value_t *third, tinypy_error_t **out_error);
typedef int32_t (*tinypy_inquiry_slot_t)(tinypy_value_t *value, tinypy_error_t **out_error);
typedef ptrdiff_t (*tinypy_length_slot_t)(tinypy_value_t *value, tinypy_error_t **out_error);
typedef tinypy_hash_t (*tinypy_hash_slot_t)(tinypy_value_t *value, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_call_slot_t)(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_get_attribute_slot_t)(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error);
typedef tinypy_bool_t (*tinypy_set_attribute_slot_t)(tinypy_value_t *value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_get_item_slot_t)(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error);
typedef tinypy_bool_t (*tinypy_set_item_slot_t)(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *item, tinypy_error_t **out_error);
typedef int32_t (*tinypy_contains_slot_t)(tinypy_value_t *value, tinypy_value_t *item, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_compare_slot_t)(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_iter_slot_t)(tinypy_value_t *value, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_next_slot_t)(tinypy_value_t *iterator, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_descriptor_get_slot_t)(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
typedef tinypy_bool_t (*tinypy_descriptor_set_slot_t)(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error);
typedef tinypy_bool_t (*tinypy_init_slot_t)(tinypy_value_t *instance, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
typedef tinypy_value_t *(*tinypy_new_slot_t)(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
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
    tinypy_binary_slot_t inplace_add;
    tinypy_binary_slot_t inplace_subtract;
    tinypy_binary_slot_t inplace_multiply;
    tinypy_binary_slot_t inplace_divide;
    tinypy_binary_slot_t reflected_add;
    tinypy_binary_slot_t reflected_subtract;
    tinypy_binary_slot_t reflected_multiply;
    tinypy_binary_slot_t reflected_divide;
} tinypy_number_slots_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_sequence_slots_t {
    tinypy_length_slot_t length;
    tinypy_binary_slot_t concat;
    tinypy_get_item_slot_t get_item;
    tinypy_set_item_slot_t set_item;
    tinypy_contains_slot_t contains;
} tinypy_sequence_slots_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_mapping_slots_t {
    tinypy_length_slot_t length;
    tinypy_get_item_slot_t get_item;
    tinypy_set_item_slot_t set_item;
} tinypy_mapping_slots_t;
//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_dict_entry_state_e {
    TINYPY_DICT_ENTRY_EMPTY = 0,
    TINYPY_DICT_ENTRY_ACTIVE = 1,
    TINYPY_DICT_ENTRY_DUMMY = 2
} tinypy_dict_entry_state_e;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_dict_entry_t {
    tinypy_hash_t hash;
    tinypy_value_t *key;
    tinypy_value_t *value;
} tinypy_dict_entry_t;
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(tinypy_dict_entry_t) == 24U, "dict entry must stay compact on 64-bit targets");
#endif
//////////////////////////////////////////////////////////////////////////
#define TINYPY_DICT_ENTRY_STATE(entry) ((entry)->key != NULL ? TINYPY_DICT_ENTRY_ACTIVE : ((entry)->value != NULL ? TINYPY_DICT_ENTRY_DUMMY : TINYPY_DICT_ENTRY_EMPTY))
#define TINYPY_DICT_ENTRY_IS_ACTIVE(entry) ((entry)->key != NULL)
#define TINYPY_DICT_ENTRY_IS_EMPTY(entry) ((entry)->key == NULL && (entry)->value == NULL)
#define TINYPY_DICT_ENTRY_IS_DUMMY(entry) ((entry)->key == NULL && (entry)->value != NULL)
#define TINYPY_DICT_ENTRY_MARK_DUMMY(entry, vm) \
    do {                                           \
        (entry)->key = NULL;                       \
        (entry)->value = &(vm)->none_object.base;  \
    } while (0)
#define TINYPY_DICT_ENTRY_MARK_EMPTY(entry) \
    do {                                      \
        (entry)->hash = 0;                    \
        (entry)->key = NULL;                  \
        (entry)->value = NULL;                \
    } while (0)
//////////////////////////////////////////////////////////////////////////
#define TINYPY_DICT_MIN_SIZE 8U
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_type_lookup_cache_entry_t {
    uint64_t epoch;
    tinypy_hash_t hash;
    tinypy_type_t *type;
    tinypy_value_t *key;
    tinypy_value_t *value;
} tinypy_type_lookup_cache_entry_t;
//////////////////////////////////////////////////////////////////////////
/* The descriptor flags depend on the attribute's own type as well, so the
   entry also remembers that type and its version. */
typedef struct tinypy_attribute_lookup_cache_entry_t {
    uint64_t epoch;
    size_t name_index;
    size_t dict_index;
    tinypy_type_t *type;
    tinypy_type_t *attribute_type;
    tinypy_value_t *attribute;
    tinypy_value_t *internal_dict_key;
    tinypy_bool_t data_descriptor;
    tinypy_bool_t has_descriptor_get;
    tinypy_bool_t dict_index_valid;
} tinypy_attribute_lookup_cache_entry_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_attribute_store_cache_entry_t {
    uint64_t epoch;
    size_t name_index;
    tinypy_type_t *type;
    tinypy_type_t *descriptor_type;
    tinypy_value_t *descriptor;
    tinypy_bool_t direct_instance_dict;
    tinypy_bool_t data_descriptor;
} tinypy_attribute_store_cache_entry_t;
//////////////////////////////////////////////////////////////////////////
#define TINYPY_ATTRIBUTE_LOOKUP_CACHE_SIZE 16U
#define TINYPY_ATTRIBUTE_LOOKUP_CACHE_WAYS 2U
//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_iterator_type_index_e {
    TINYPY_ITERATOR_TYPE_LIST = 0,
    TINYPY_ITERATOR_TYPE_TUPLE,
    TINYPY_ITERATOR_TYPE_DICT_KEY,
    TINYPY_ITERATOR_TYPE_DICT_VALUE,
    TINYPY_ITERATOR_TYPE_DICT_ITEM,
    TINYPY_ITERATOR_TYPE_SET,
    TINYPY_ITERATOR_TYPE_RANGE,
    TINYPY_ITERATOR_TYPE_CALLABLE,
    TINYPY_ITERATOR_TYPE_LIST_REVERSE,
    TINYPY_ITERATOR_TYPE_BYTEARRAY,
    TINYPY_ITERATOR_TYPE_COUNT
} tinypy_iterator_type_index_e;
//////////////////////////////////////////////////////////////////////////
/* tinypy type object. Builtin and heap types share this prefix; heap
 * types later append their protocol tables and slot/member storage. */
struct tinypy_type_t {
    tinypy_sized_object_t base;
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
    tinypy_release_references_slot_t traverse_references;
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
    tinypy_value_t *own_slot_names;
    tinypy_bool_t has_instance_dict;
    tinypy_bool_t has_finalizer;
    tinypy_bool_t has_classic_mro;
    tinypy_bool_t has_custom_mro;
    tinypy_bool_t bases_updating;
    uint64_t finalizer_epoch;
    tinypy_value_t *weakrefs;
    tinypy_value_t *subclasses;
    size_t native_payload_offset;
    size_t native_payload_size;
    size_t native_payload_alignment;
    tinypy_native_type_spec_t native_spec;
    tinypy_number_slots_t native_number_slots;
    tinypy_sequence_slots_t native_sequence_slots;
    tinypy_mapping_slots_t native_mapping_slots;
};
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_instance_object_t {
    tinypy_value_t base;
    tinypy_value_t *dict;
    tinypy_value_t *slots[];
} tinypy_instance_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_native_instance_object_t {
    tinypy_value_t base;
    tinypy_value_t *dict;
    tinypy_value_t *weakrefs;
    tinypy_bool_t finalized;
    tinypy_internal_max_align_t payload;
} tinypy_native_instance_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_internal_exception_payload_t {
    tinypy_value_t *args;
    tinypy_value_t *message;
} tinypy_internal_exception_payload_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_internal_unicode_error_payload_t {
    tinypy_internal_exception_payload_t base;
    tinypy_value_t *encoding;
    tinypy_value_t *object;
    tinypy_value_t *reason;
    int64_t start;
    int64_t end;
} tinypy_internal_unicode_error_payload_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_class_object_t {
    tinypy_value_t base;
    tinypy_value_t *name;
    tinypy_value_t *bases;
    tinypy_value_t *dict;
    tinypy_value_t *weakrefs;
} tinypy_class_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_old_instance_object_t {
    tinypy_value_t base;
    tinypy_value_t *class_object;
    tinypy_value_t *dict;
    tinypy_value_t *weakrefs;
} tinypy_old_instance_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_none_object_t {
    tinypy_value_t base;
} tinypy_none_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_integer_object_t {
    tinypy_value_t base;
    int64_t integer_value;
} tinypy_integer_object_t;
//////////////////////////////////////////////////////////////////////////
/* Borrowed immutable metadata for internal names; ordinary strings use NULL. */
typedef struct tinypy_internal_string_metadata_t {
    uint8_t builtin_attribute_id;
} tinypy_internal_string_metadata_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_string_object_t {
    tinypy_sized_object_t base;
    tinypy_hash_t hash;
    const tinypy_internal_string_metadata_t *internal_metadata;
    tinypy_bool_t interned;
    tinypy_bool_t hash_computed;
    uint8_t bytes[1];
} tinypy_string_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_unicode_object_t {
    tinypy_sized_object_t base;
    size_t byte_size;
    tinypy_hash_t hash;
    tinypy_bool_t hash_computed;
    size_t *index_offsets;
    uint32_t *native_buffer;
    uint8_t utf8[];
} tinypy_unicode_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_long_object_t {
    tinypy_value_t base;
    size_t digit_count;
    int32_t sign;
    size_t digit_capacity;
    uint16_t digits[];
} tinypy_long_object_t;
//////////////////////////////////////////////////////////////////////////
typedef char tinypy_long_digit_count_must_follow_header_t[offsetof(tinypy_long_object_t, digit_count) == sizeof(tinypy_value_t) ? 1 : -1];
typedef char tinypy_long_sign_must_follow_digit_count_t[offsetof(tinypy_long_object_t, sign) == sizeof(tinypy_value_t) + sizeof(size_t) ? 1 : -1];
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_float_object_t {
    tinypy_value_t base;
    double value;
} tinypy_float_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_complex_object_t {
    tinypy_value_t base;
    double real;
    double imaginary;
} tinypy_complex_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_tuple_object_t {
    tinypy_sized_object_t base;
    tinypy_value_t *items[1];
} tinypy_tuple_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_tuple_subclass_object_t {
    tinypy_sized_object_t base;
    tinypy_value_t **items;
    tinypy_value_t *dict;
} tinypy_tuple_subclass_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_list_object_t {
    tinypy_sized_object_t base;
    tinypy_value_t **items;
    size_t allocated;
    uint64_t mutation_version;
} tinypy_list_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_dict_object_t {
    tinypy_value_t base;
    size_t fill;
    size_t used;
    size_t mask;
    tinypy_dict_entry_t *table;
    uint64_t mutation_version;
    uint64_t cache_version;
    tinypy_bool_t type_dictionary;
    tinypy_type_t *type_owner;
    size_t popitem_finger;
    tinypy_dict_entry_t small_table[TINYPY_DICT_MIN_SIZE];
} tinypy_dict_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_set_object_t {
    tinypy_value_t base;
    tinypy_value_t *dict;
    tinypy_hash_t hash;
    tinypy_bool_t hash_computed;
    size_t finger;
} tinypy_set_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_output_stream_object_t {
    tinypy_value_t base;
    tinypy_output_channel_e channel;
    tinypy_bool_t soft_space;
} tinypy_output_stream_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_buffer_object_t {
    tinypy_value_t base;
    tinypy_value_t *owner;
    size_t offset;
    size_t size;
} tinypy_buffer_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_bytearray_object_t {
    tinypy_sized_object_t base;
    size_t capacity;
    size_t exports;
    uint8_t *bytes;
} tinypy_bytearray_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_weakref_object_t {
    tinypy_value_t base;
    tinypy_value_t *object;
    tinypy_value_t *callback;
    tinypy_value_t *previous;
    tinypy_value_t *next;
    tinypy_hash_t hash;
    tinypy_bool_t hash_computed;
    tinypy_value_t *dict;
    tinypy_value_t *slots[];
} tinypy_weakref_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_partial_object_t {
    tinypy_value_t base;
    tinypy_value_t *callable;
    tinypy_value_t *args;
    tinypy_value_t *keywords;
    tinypy_value_t *dict;
    tinypy_value_t *weakrefs;
} tinypy_partial_object_t;
//////////////////////////////////////////////////////////////////////////
/* A pattern keeps the buffers of its last match so that findall, sub and
   split over one subject neither re-decode unicode nor reallocate the
   matcher stacks for every match. */
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
    tinypy_value_t *cache_string;
    uint32_t *cache_characters;
    size_t cache_size;
    void *cache_contexts;
    size_t cache_contexts_size;
    size_t *cache_marks;
    size_t cache_mark_capacity;
} tinypy_sre_pattern_object_t;
//////////////////////////////////////////////////////////////////////////
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
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_dict_view_object_t {
    tinypy_value_t base;
    tinypy_value_t *dict;
    tinypy_dict_view_kind_e kind;
} tinypy_dict_view_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_file_object_t {
    tinypy_value_t base;
    void *host_handle;
} tinypy_file_object_t;
//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_global_cache_source_e
{
    TINYPY_GLOBAL_CACHE_EMPTY = 0,
    TINYPY_GLOBAL_CACHE_GLOBALS = 1,
    TINYPY_GLOBAL_CACHE_BUILTINS = 2,
    TINYPY_GLOBAL_CACHE_COMPILE_ENVIRONMENT = 3
} tinypy_global_cache_source_e;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_global_cache_entry_t {
    size_t name_index;
    uint64_t globals_version;
    uint64_t builtins_version;
    tinypy_value_t *value;
    tinypy_global_cache_source_e source;
} tinypy_global_cache_entry_t;
//////////////////////////////////////////////////////////////////////////
/* One cleared frame allocation may be owned by its code object. */
typedef struct tinypy_code_object_t {
    tinypy_value_t base;
    int32_t arg_count;
    int32_t local_count;
    int32_t stack_size;
    int32_t flags;
    tinypy_bool_t bytecode_verified;
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
    tinypy_value_t *parameter_indices;
    tinypy_compile_environment_t *compile_environment;
    tinypy_value_t *cached_frame;
    tinypy_global_cache_entry_t global_cache[TINYPY_CODE_GLOBAL_CACHE_SIZE];
    tinypy_attribute_lookup_cache_entry_t attribute_cache[TINYPY_ATTRIBUTE_LOOKUP_CACHE_SIZE];
    tinypy_attribute_store_cache_entry_t attribute_store_cache[TINYPY_ATTRIBUTE_LOOKUP_CACHE_SIZE];
} tinypy_code_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_frame_block_t {
    int32_t type;
    uint32_t handler;
    uint32_t stack_level;
} tinypy_frame_block_t;
//////////////////////////////////////////////////////////////////////////
/* tinypy frame object. Trace and exception-state fields are added when
 * their Python-visible objects are introduced; the execution layout already
 * matches CPython's inline f_localsplus storage. */
typedef struct tinypy_frame_object_t {
    tinypy_sized_object_t base;
    tinypy_value_t *back;
    tinypy_value_t *code;
    tinypy_value_t *builtins;
    tinypy_value_t *globals;
    tinypy_value_t *locals;
    tinypy_value_t *trace;
    uint64_t handled_clear_epoch;
    tinypy_bool_t handled_state_saved;
    tinypy_value_t *previous_handled_type;
    tinypy_value_t *previous_handled_value;
    tinypy_value_t *previous_handled_traceback;
    tinypy_value_t **value_stack;
    tinypy_value_t **stack_top;
    int32_t last_instruction;
#if defined(TINYPY_DEBUGGER)
    int32_t debugger_line;
#endif
    uint32_t block_count;
    tinypy_frame_block_t blocks[TINYPY_FRAME_MAX_BLOCKS];
    tinypy_value_t *locals_plus[];
} tinypy_frame_object_t;
//////////////////////////////////////////////////////////////////////////
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
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_iterator_object_t {
    tinypy_value_t base;
    tinypy_value_t *iterable;
    tinypy_value_t *sentinel;
    size_t index;
    size_t table_position;
    uint64_t expected_state;
    int32_t mode;
    int64_t current;
    int64_t step;
    size_t remaining;
} tinypy_iterator_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_xrange_object_t {
    tinypy_value_t base;
    int64_t start;
    int64_t step;
    size_t length;
} tinypy_xrange_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_enumerate_object_t {
    tinypy_value_t base;
    tinypy_value_t *iterator;
    tinypy_value_t *index;
} tinypy_enumerate_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_reversed_object_t {
    tinypy_value_t base;
    tinypy_value_t *sequence;
    size_t index;
} tinypy_reversed_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_method_object_t {
    tinypy_value_t base;
    tinypy_value_t *function;
    tinypy_value_t *self;
    tinypy_value_t *owner;
} tinypy_method_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_cell_object_t {
    tinypy_value_t base;
    tinypy_value_t *content;
} tinypy_cell_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_slice_object_t {
    tinypy_value_t base;
    tinypy_value_t *start;
    tinypy_value_t *stop;
    tinypy_value_t *step;
} tinypy_slice_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_module_object_t {
    tinypy_value_t base;
    tinypy_value_t *dict;
    tinypy_value_t *name;
} tinypy_module_object_t;
//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_native_descriptor_kind_e {
    TINYPY_NATIVE_DESCRIPTOR_AUTO = 0,
    TINYPY_NATIVE_DESCRIPTOR_METHOD = 1,
    TINYPY_NATIVE_DESCRIPTOR_WRAPPER = 2
} tinypy_native_descriptor_kind_e;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_native_function_object_t {
    tinypy_value_t base;
    tinypy_value_t *name;
    tinypy_value_t *module;
    tinypy_value_t *function;
    tinypy_value_t *self;
    tinypy_type_t *owner;
    tinypy_native_function_callback_t callback;
    void *user_data;
    tinypy_native_function_finalize_t finalize;
    tinypy_bool_t owner_retained;
    tinypy_native_descriptor_kind_e descriptor_kind;
} tinypy_native_function_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_callable_descriptor_object_t {
    tinypy_value_t base;
    tinypy_value_t *callable;
} tinypy_callable_descriptor_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_property_object_t {
    tinypy_value_t base;
    tinypy_value_t *getter;
    tinypy_value_t *setter;
    tinypy_value_t *deleter;
    tinypy_value_t *doc;
    tinypy_bool_t getter_doc;
} tinypy_property_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_c_descriptor_object_t {
    tinypy_value_t base;
    tinypy_type_t *owner;
    tinypy_value_t *owner_reference;
    tinypy_value_t *name;
    size_t index;
    int32_t field;
    tinypy_bool_t writable;
    tinypy_bool_t owner_retained;
} tinypy_c_descriptor_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_super_object_t {
    tinypy_value_t base;
    tinypy_type_t *type;
    tinypy_value_t *object;
    tinypy_type_t *object_type;
} tinypy_super_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_traceback_object_t {
    tinypy_value_t base;
    tinypy_value_t *next;
    tinypy_value_t *frame;
    int32_t last_instruction;
    int32_t line_number;
} tinypy_traceback_object_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_generator_object_t {
    tinypy_value_t base;
    tinypy_value_t *frame;
    tinypy_value_t *code;
    size_t instruction_offset;
    tinypy_bool_t running;
    tinypy_bool_t finished;
    tinypy_bool_t started;
} tinypy_generator_object_t;
//////////////////////////////////////////////////////////////////////////
#define TINYPY_INTEGER_VALUE(value) \
    (((tinypy_integer_object_t *)(value))->integer_value)
#define TINYPY_INTEGER_OBJECT(value) ((tinypy_integer_object_t *)(value))
#define TINYPY_INSTANCE_OBJECT(value) ((tinypy_instance_object_t *)(value))
#define TINYPY_NATIVE_INSTANCE_OBJECT(value) ((tinypy_native_instance_object_t *)(value))
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
#define TINYPY_VALUE_VM(value) ((value)->type->vm)
#define TINYPY_VALUE_KIND(value) ((value)->type->layout_kind)
#define TINYPY_REFCNT(value) ((value)->ref)
#define TINYPY_SIZED_SIZE(value) (((tinypy_sized_object_t *)(value))->size)
#define TINYPY_CLEAR_ERROR(out_error) \
    do { \
        tinypy_error_t **__tinypy_out_error = (out_error); \
        if (__tinypy_out_error != NULL) { \
            *__tinypy_out_error = NULL; \
        } \
    } while (0)
#define TINYPY_INCREF(value) \
    do { \
        tinypy_value_t *__tinypy_incref_value = (value); \
        TINYPY_REFCNT(__tinypy_incref_value) += 1; \
    } while (0)
/* Return an owned reference; evaluate the argument exactly once. */
static inline tinypy_value_t *__tinypy_internal_value_ret(tinypy_value_t *value) {
    TINYPY_INCREF(value);
    return value;
}
#define TINYPY_RET(value) __tinypy_internal_value_ret(value)
#define TINYPY_RET_NONE(vm) TINYPY_RET(&(vm)->none_object.base)
#define TINYPY_RET_TRUE(vm) TINYPY_RET(&(vm)->true_object.base)
#define TINYPY_RET_FALSE(vm) TINYPY_RET(&(vm)->false_object.base)
#define TINYPY_RET_NOT_IMPLEMENTED(vm) TINYPY_RET(&(vm)->not_implemented_object.base)
#define TINYPY_RET_ELLIPSIS(vm) TINYPY_RET(&(vm)->ellipsis_object.base)
#define TINYPY_RET_EMPTY_TUPLE(vm) TINYPY_RET(&(vm)->empty_tuple_object.base.base)
#define TINYPY_RET_EMPTY_STRING(vm) TINYPY_RET(&(vm)->empty_string_object.base.base)
#define TINYPY_RET_EMPTY_UNICODE(vm) TINYPY_RET((vm)->empty_unicode)
#define TINYPY_DECREF(value) \
    do { \
        tinypy_value_t *__tinypy_decref_value = (value); \
        TINYPY_REFCNT(__tinypy_decref_value) -= 1; \
        if (TINYPY_REFCNT(__tinypy_decref_value) == 0) { \
            __tinypy_internal_value_release_zero_fast(__tinypy_decref_value); \
        } \
    } while (0)
#define TINYPY_TUPLE_ITEMS(value) ((value)->type == &TINYPY_VALUE_VM(value)->types[TINYPY_VALUE_TUPLE] ? TINYPY_TUPLE_OBJECT(value)->items : TINYPY_TUPLE_SUBCLASS_OBJECT(value)->items)
#define TINYPY_TUPLE_SIZE(value) TINYPY_SIZED_SIZE(value)
#define TINYPY_TUPLE_GET(value, index) (TINYPY_TUPLE_ITEMS(value)[(index)])
#define TINYPY_TUPLE_ITERATOR_BEGIN(value) (TINYPY_TUPLE_ITEMS(value))
#define TINYPY_TUPLE_ITERATOR_END(value) (TINYPY_TUPLE_SIZE(value) != 0U ? TINYPY_TUPLE_ITEMS(value) + TINYPY_TUPLE_SIZE(value) : TINYPY_TUPLE_ITEMS(value))
#define TINYPY_LIST_SIZE(value) TINYPY_SIZED_SIZE(value)
#define TINYPY_LIST_GET(value, index) (TINYPY_LIST_OBJECT(value)->items[(index)])
#define TINYPY_LIST_ITERATOR_BEGIN(value) (TINYPY_LIST_OBJECT(value)->items)
#define TINYPY_LIST_ITERATOR_END(value) (TINYPY_LIST_SIZE(value) != 0U ? TINYPY_LIST_OBJECT(value)->items + TINYPY_LIST_SIZE(value) : TINYPY_LIST_OBJECT(value)->items)
#define TINYPY_STRING_SIZE(value) TINYPY_SIZED_SIZE(value)
#define TINYPY_DICT_SIZE(value) (TINYPY_DICT_OBJECT(value)->used)
#define TINYPY_DICT_ITERATOR_BEGIN(value) (TINYPY_DICT_OBJECT(value)->table)
#define TINYPY_DICT_ITERATOR_END(value) (TINYPY_DICT_OBJECT(value)->table + TINYPY_DICT_OBJECT(value)->mask + 1U)
#define TINYPY_CODE_ARG_COUNT(value) (TINYPY_CODE_OBJECT(value)->arg_count)
#define TINYPY_CODE_LOCAL_COUNT(value) (TINYPY_CODE_OBJECT(value)->local_count)
#define TINYPY_CODE_STACK_SIZE(value) (TINYPY_CODE_OBJECT(value)->stack_size)
#define TINYPY_CODE_FLAGS(value) (TINYPY_CODE_OBJECT(value)->flags)
#define TINYPY_CODE_BYTECODE(value) (TINYPY_CODE_OBJECT(value)->bytecode)
#define TINYPY_CODE_CONSTS(value) (TINYPY_CODE_OBJECT(value)->consts)
#define TINYPY_CODE_NAMES(value) (TINYPY_CODE_OBJECT(value)->names)
#define TINYPY_CODE_VARNAMES(value) (TINYPY_CODE_OBJECT(value)->varnames)
#define TINYPY_CODE_FREEVARS(value) (TINYPY_CODE_OBJECT(value)->freevars)
#define TINYPY_CODE_CELLVARS(value) (TINYPY_CODE_OBJECT(value)->cellvars)
#define TINYPY_CODE_FILENAME(value) (TINYPY_CODE_OBJECT(value)->filename)
#define TINYPY_CODE_NAME(value) (TINYPY_CODE_OBJECT(value)->name)
#define TINYPY_CODE_FIRST_LINE_NUMBER(value) (TINYPY_CODE_OBJECT(value)->first_line_number)
#define TINYPY_CODE_LNOTAB(value) (TINYPY_CODE_OBJECT(value)->lnotab)
#define TINYPY_TEXT_BYTES(value) (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING ? TINYPY_STRING_OBJECT(value)->bytes : TINYPY_UNICODE_OBJECT(value)->utf8)
#define TINYPY_TEXT_BYTE_SIZE(value) (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING ? TINYPY_SIZED_SIZE(value) : TINYPY_UNICODE_OBJECT(value)->byte_size)
/* Protocol names compare their stored text, bypassing subtype callbacks. */
static inline tinypy_bool_t __tinypy_internal_name_equal(const tinypy_value_t *name, const tinypy_value_t *preset) {
    if (name == preset) {
        return TINYPY_TRUE;
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(name);
    if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
        return TINYPY_FALSE;
    }
    size_t size = TINYPY_TEXT_BYTE_SIZE(name);
    tinypy_bool_t equal = size == TINYPY_TEXT_BYTE_SIZE(preset)
        && (size == 0U || memcmp(TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTES(preset), size) == 0);
    return equal;
}
#define TINYPY_NAME_EQ(name, preset) __tinypy_internal_name_equal(name, preset)
#define TINYPY_LONG_DIGIT_COUNT(value) (TINYPY_LONG_OBJECT(value)->digit_count)
#define TINYPY_LONG_SIGN(value) (TINYPY_LONG_OBJECT(value)->sign)

typedef char tinypy_integer_body_must_follow_header_t[offsetof(tinypy_integer_object_t, integer_value) == sizeof(tinypy_value_t) ? 1 : -1];
typedef char tinypy_float_body_must_follow_header_t[offsetof(tinypy_float_object_t, value) == sizeof(tinypy_value_t) ? 1 : -1];
typedef char tinypy_complex_body_must_follow_header_t[offsetof(tinypy_complex_object_t, real) == sizeof(tinypy_value_t) ? 1 : -1];
typedef char tinypy_dict_body_must_follow_header_t[offsetof(tinypy_dict_object_t, fill) == sizeof(tinypy_value_t) ? 1 : -1];
//////////////////////////////////////////////////////////////////////////
struct tinypy_vm_t {
    uint32_t state;
    tinypy_allocator_t allocator;
    tinypy_host_t host;
    tinypy_bool_t has_host;
    size_t max_heap_bytes;
    size_t allocated_bytes;
    uint64_t hash_secret_prefix;
    uint64_t hash_secret_suffix;
    int32_t optimize_level;
    tinypy_bool_t float_format_unknown;
    tinypy_bool_t double_format_unknown;
    tinypy_pool_allocator_t pool_allocator;
    tinypy_integer_object_t *integer_free_list;
    size_t integer_free_count;
    tinypy_frame_object_t *frame_free_list;
    size_t frame_free_count;
    tinypy_method_object_t *method_free_list;
    size_t method_free_count;
    tinypy_native_function_object_t *native_method_free_list;
    size_t native_method_free_count;
    uint64_t dict_cache_epoch;
    tinypy_bool_t dict_cache_exhausted;
    uint64_t type_lookup_cache_epoch;
    tinypy_type_lookup_cache_entry_t type_lookup_cache[TINYPY_TYPE_LOOKUP_CACHE_SIZE];

    tinypy_type_t types[TINYPY_BUILTIN_TYPE_COUNT];
    tinypy_type_t *dictproxy_type;
    tinypy_type_t *iterator_types[TINYPY_ITERATOR_TYPE_COUNT];
    tinypy_type_t *memoryview_type;
    tinypy_type_t *sre_scanner_type;
    tinypy_type_t *weak_proxy_type;
    tinypy_type_t *callable_weak_proxy_type;
    tinypy_type_t *native_method_descriptor_type;
    tinypy_type_t *native_wrapper_descriptor_type;
    tinypy_type_t *native_method_wrapper_type;

    tinypy_sequence_slots_t buffer_sequence_slots;
    tinypy_mapping_slots_t buffer_mapping_slots;
    tinypy_sequence_slots_t bytearray_sequence_slots;
    tinypy_mapping_slots_t bytearray_mapping_slots;
    tinypy_sequence_slots_t dict_view_sequence_slots;
    tinypy_number_slots_t weak_proxy_number_slots;
    tinypy_sequence_slots_t weak_proxy_sequence_slots;
    tinypy_mapping_slots_t weak_proxy_mapping_slots;

    tinypy_type_t *exception_types[TINYPY_EXCEPTION_TYPE_COUNT];
    tinypy_value_t *raised_type;
    tinypy_value_t *raised_value;
    tinypy_value_t *emergency_memory_error;
    tinypy_value_t *raised_traceback;
    uint64_t handled_clear_epoch;
    tinypy_value_t *handled_type;
    tinypy_value_t *handled_value;
    tinypy_value_t *handled_traceback;

    tinypy_frame_object_t *current_frame;
    size_t release_depth;
    tinypy_value_t *pending_releases;
    size_t evaluation_depth;
    size_t recursion_limit;
    uintptr_t native_stack_origin;
    size_t max_stack_bytes;
    tinypy_bool_t recursion_error;
#if defined(TINYPY_DEBUGGER)
    tinypy_debugger_t debugger;
    tinypy_bool_t has_debugger;
#endif
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    tinypy_cycle_diagnostics_state_t *cycle_diagnostics;
#endif
    tinypy_value_t *builtins;
    tinypy_value_t *codec_module;
    tinypy_value_t *codec_search_path;
    tinypy_value_t *codec_cache;
    tinypy_value_t *codec_errors;
    /* Owned keys for internal C literals outside the fixed preset registry. */
    tinypy_value_t *internal_strings;
    /* Borrowed entries: a string removes itself before its storage is freed. */
    tinypy_intern_entry_t *intern_entries;
    size_t intern_capacity;
    size_t intern_used;
    size_t intern_fill;
    size_t intern_max_size;
#define TINYPY_INTERNAL_KEY_FIELD(field, name, intern_name) tinypy_value_t *field;
    TINYPY_INTERNAL_KEY_LIST(TINYPY_INTERNAL_KEY_FIELD)
#undef TINYPY_INTERNAL_KEY_FIELD
    tinypy_value_t *internal_key_table[TINYPY_INTERNAL_KEY_TABLE_SIZE];
    tinypy_value_t *modules;
    /* The sys module is reached through this owned reference, as CPython reads
       its own sysdict, so rebinding sys.modules['sys'] cannot break the VM. */
    tinypy_value_t *sys_module;
    tinypy_value_t *module_finder;

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
    tinypy_value_t *empty_unicode;
    tinypy_tuple_object_t empty_tuple_object;
    tinypy_value_t *string_char_cache[256];
    tinypy_value_t *unicode_char_cache[256];
};
//////////////////////////////////////////////////////////////////////////
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
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_host_valid(const tinypy_host_t *host);
tinypy_bool_t tinypy_internal_vm_valid(const tinypy_vm_t *vm);
tinypy_bool_t tinypy_internal_recursion_check(tinypy_vm_t *vm, uintptr_t stack_address, const char *message, tinypy_error_t **out_error);

void *tinypy_internal_vm_allocate(tinypy_vm_t *vm, size_t size);
void *tinypy_internal_vm_allocate_checked(tinypy_vm_t *vm, size_t size, tinypy_error_t **out_error);
void *tinypy_internal_vm_reallocate(tinypy_vm_t *vm, void *memory, size_t old_size, size_t new_size);
void *tinypy_internal_vm_reallocate_checked(tinypy_vm_t *vm, void *memory, size_t old_size, size_t new_size, tinypy_error_t **out_error);
void tinypy_internal_vm_deallocate(tinypy_vm_t *vm, void *memory, size_t size);
tinypy_bool_t tinypy_internal_decimal_double(tinypy_vm_t *vm, const char *text, size_t size, double *out_value);
tinypy_value_t *tinypy_internal_long_from_double(tinypy_vm_t *vm, double value);

void tinypy_internal_make_error(const tinypy_allocator_t *allocator, tinypy_error_kind_e error_kind, const char *message, tinypy_error_t **out_error);
void tinypy_internal_make_vm_error(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const char *message, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
/* Message fragments joined into one exception message without formatting. */
typedef struct tinypy_message_part_t {
    const char *bytes;
    size_t size;
} tinypy_message_part_t;
#define TINYPY_MESSAGE_PART_LITERAL(text) {text, sizeof(text) - 1U}
#define TINYPY_MESSAGE_PART_TEXT(value) {(const char *)TINYPY_TEXT_BYTES(value), TINYPY_TEXT_BYTE_SIZE(value)}
#define TINYPY_MESSAGE_PART_TYPE_NAME(value) {(value)->type->name, (value)->type->name_size}
#define TINYPY_MESSAGE_SIZE_BUFFER 21U
void tinypy_internal_make_vm_error_parts(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const tinypy_message_part_t *parts, size_t part_count, tinypy_error_t **out_error);
size_t tinypy_internal_format_size(char *buffer, size_t value);
/* The three ways CPython 2.7 reports a C-level argument count mismatch:
   PyArg_ParseTuple, PyArg_UnpackTuple and METH_O. */
typedef enum tinypy_arity_style_e {
    TINYPY_ARITY_STYLE_PARSED = 0,
    TINYPY_ARITY_STYLE_UNPACK = 1,
    TINYPY_ARITY_STYLE_SINGLE = 2,
    TINYPY_ARITY_STYLE_WRAPPER = 3
} tinypy_arity_style_e;
void tinypy_internal_make_arity_error(tinypy_vm_t *vm, const char *name, size_t name_size, size_t count, size_t minimum, size_t maximum, tinypy_arity_style_e style, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_native_method_arguments(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_arity_style_e style, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_legacy_slice_new(tinypy_value_t *args, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_iterator_length_hint_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error);
void tinypy_internal_initialize_native_function_descriptors(tinypy_vm_t *vm);
tinypy_bool_t tinypy_internal_native_function_compare_three_way(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_error_t **out_error);
void tinypy_internal_initialize_struct_descriptors(tinypy_type_t *type);
void tinypy_internal_type_add_object_attribute_methods(tinypy_type_t *type);
tinypy_value_t *tinypy_internal_struct_get_field(tinypy_value_t *instance, tinypy_value_t *name, tinypy_error_t **out_error);
void tinypy_internal_make_vm_error_location(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const char *message, const char *logical_filename, size_t filename_size, int32_t line_number, int32_t column_offset, const char *source_line, size_t source_line_size, tinypy_bool_t include_location, tinypy_error_t **out_error);

tinypy_bool_t tinypy_internal_value_belongs_to(const tinypy_vm_t *vm, const tinypy_value_t *value);
tinypy_bool_t tinypy_internal_value_is_vm_embedded(const tinypy_vm_t *vm, const tinypy_value_t *value);
size_t tinypy_internal_utf8_decode(const uint8_t *bytes, size_t size, uint32_t *out_code_point);
size_t tinypy_internal_utf8_invalid_span(const uint8_t *bytes, size_t size);
size_t tinypy_internal_utf8_encode(uint32_t code_point, uint8_t bytes[4]);
size_t tinypy_internal_unicode_byte_offset(tinypy_value_t *value, size_t character_index);
size_t tinypy_internal_unicode_character_index(tinypy_value_t *value, size_t byte_offset);
const uint8_t *tinypy_internal_unicode_native_buffer(tinypy_value_t *value, tinypy_bool_t checked, size_t *out_size, tinypy_error_t **out_error);
void tinypy_internal_unicode_destroy(tinypy_value_t *value);
uint32_t tinypy_internal_unicode_lower(uint32_t code_point);
uint32_t tinypy_internal_unicode_upper(uint32_t code_point);
uint32_t tinypy_internal_unicode_title(uint32_t code_point);
tinypy_bool_t tinypy_internal_unicode_is_alpha(uint32_t code_point);
tinypy_bool_t tinypy_internal_unicode_is_digit(uint32_t code_point);
tinypy_bool_t tinypy_internal_unicode_is_alnum(uint32_t code_point);
tinypy_bool_t tinypy_internal_unicode_is_space(uint32_t code_point);
tinypy_bool_t tinypy_internal_unicode_is_lower(uint32_t code_point);
tinypy_bool_t tinypy_internal_unicode_is_upper(uint32_t code_point);
tinypy_bool_t tinypy_internal_unicode_is_title(uint32_t code_point);
tinypy_bool_t tinypy_internal_unicode_is_cased(uint32_t code_point);
tinypy_bool_t tinypy_internal_unicode_is_linebreak(uint32_t code_point);
tinypy_bool_t tinypy_internal_unicode_is_decimal(uint32_t code_point);
tinypy_bool_t tinypy_internal_unicode_is_numeric(uint32_t code_point);
tinypy_bool_t tinypy_internal_unicode_decimal_digit(uint32_t code_point, uint8_t *out_digit);
tinypy_bool_t tinypy_internal_text_ascii_compatible(tinypy_vm_t *vm, const tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_raise_ascii_decode_error(tinypy_vm_t *vm, const tinypy_value_t *text, size_t start, size_t end, tinypy_error_t **out_error);
ptrdiff_t tinypy_internal_find_bytes(const uint8_t *haystack, size_t haystack_size, const uint8_t *needle, size_t needle_size, tinypy_bool_t reverse);
tinypy_vm_t *tinypy_internal_value_vm(const tinypy_value_t *value);
tinypy_value_type_e tinypy_internal_value_kind(const tinypy_value_t *value);
tinypy_value_t *tinypy_internal_value_allocate(tinypy_vm_t *vm, tinypy_value_type_e type, size_t allocation_size);
tinypy_value_t *tinypy_internal_object_allocate(tinypy_vm_t *vm, tinypy_type_t *object_type, size_t allocation_size);
tinypy_value_t *tinypy_internal_object_allocate_checked(tinypy_vm_t *vm, tinypy_type_t *object_type, size_t allocation_size, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_long_allocate_digits(tinypy_vm_t *vm, int32_t sign, size_t digit_count, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_long_from_base15_digits_checked(tinypy_vm_t *vm, int32_t sign, const uint16_t *digits, size_t digit_count, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_text_allocate_uninitialized_checked(tinypy_vm_t *vm, tinypy_value_type_e type, size_t byte_size, size_t code_point_count, uint8_t **out_bytes, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_string_from_bytes_checked(tinypy_vm_t *vm, const void *bytes, size_t size, tinypy_error_t **out_error);
/* Bypass the intern table for compiler/wire bytes; keep the empty/char caches. */
tinypy_value_t *tinypy_internal_string_from_bytes_uninterned(tinypy_vm_t *vm, const void *bytes, size_t size);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
void tinypy_internal_cycle_diagnostics_initialize(tinypy_vm_t *vm);
void tinypy_internal_cycle_diagnostics_finalize(tinypy_vm_t *vm);
void tinypy_internal_cycle_diagnostics_value_register_enabled(tinypy_vm_t *vm, tinypy_value_t *value);
void tinypy_internal_cycle_diagnostics_value_reuse_enabled(tinypy_vm_t *vm, tinypy_value_t *value);
void tinypy_internal_cycle_diagnostics_value_unregister_enabled(tinypy_vm_t *vm, tinypy_value_t *value);
void tinypy_internal_cycle_diagnostics_list_extend_enabled(tinypy_vm_t *vm, tinypy_value_t *list, size_t index, tinypy_value_t *const *items, size_t item_count);
void tinypy_internal_cycle_diagnostics_list_insert_enabled(tinypy_vm_t *vm, tinypy_value_t *list, size_t index, tinypy_value_t *item);
void tinypy_internal_cycle_diagnostics_list_set_enabled(tinypy_vm_t *vm, tinypy_value_t *list, size_t index, tinypy_value_t *item);
void tinypy_internal_cycle_diagnostics_list_remove_enabled(tinypy_vm_t *vm, tinypy_value_t *list, size_t index);
void tinypy_internal_cycle_diagnostics_list_clear_enabled(tinypy_vm_t *vm, tinypy_value_t *list);
void tinypy_internal_cycle_diagnostics_list_reindex_enabled(tinypy_vm_t *vm, tinypy_value_t *list);
void tinypy_internal_cycle_diagnostics_dict_set_enabled(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value, tinypy_bool_t inserted);
void tinypy_internal_cycle_diagnostics_dict_delete_enabled(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key);
void tinypy_internal_cycle_diagnostics_dict_clear_enabled(tinypy_vm_t *vm, tinypy_value_t *dict);
void tinypy_internal_cycle_diagnostics_cell_set_enabled(tinypy_vm_t *vm, tinypy_value_t *cell, tinypy_value_t *content);
void tinypy_internal_vm_visit_reachable_values(tinypy_vm_t *vm, tinypy_release_callback_t visit, void *user_data);

static inline void __tinypy_internal_cycle_diagnostics_value_register(tinypy_vm_t *vm, tinypy_value_t *value) {
    if (vm->cycle_diagnostics != NULL) {
        tinypy_internal_cycle_diagnostics_value_register_enabled(vm, value);
    }
}
static inline void __tinypy_internal_cycle_diagnostics_value_reuse(tinypy_vm_t *vm, tinypy_value_t *value) {
    if (vm->cycle_diagnostics != NULL) {
        tinypy_internal_cycle_diagnostics_value_reuse_enabled(vm, value);
    }
}
static inline void __tinypy_internal_cycle_diagnostics_value_unregister(tinypy_vm_t *vm, tinypy_value_t *value) {
    if (vm->cycle_diagnostics != NULL) {
        tinypy_internal_cycle_diagnostics_value_unregister_enabled(vm, value);
    }
}
static inline void __tinypy_internal_cycle_diagnostics_list_extend(tinypy_vm_t *vm, tinypy_value_t *list, size_t index, tinypy_value_t *const *items, size_t item_count) {
    if (vm->cycle_diagnostics != NULL) {
        tinypy_internal_cycle_diagnostics_list_extend_enabled(vm, list, index, items, item_count);
    }
}
static inline void __tinypy_internal_cycle_diagnostics_list_insert(tinypy_vm_t *vm, tinypy_value_t *list, size_t index, tinypy_value_t *item) {
    if (vm->cycle_diagnostics != NULL) {
        tinypy_internal_cycle_diagnostics_list_insert_enabled(vm, list, index, item);
    }
}
static inline void __tinypy_internal_cycle_diagnostics_list_set(tinypy_vm_t *vm, tinypy_value_t *list, size_t index, tinypy_value_t *item) {
    if (vm->cycle_diagnostics != NULL) {
        tinypy_internal_cycle_diagnostics_list_set_enabled(vm, list, index, item);
    }
}
static inline void __tinypy_internal_cycle_diagnostics_list_remove(tinypy_vm_t *vm, tinypy_value_t *list, size_t index) {
    if (vm->cycle_diagnostics != NULL) {
        tinypy_internal_cycle_diagnostics_list_remove_enabled(vm, list, index);
    }
}
static inline void __tinypy_internal_cycle_diagnostics_list_clear(tinypy_vm_t *vm, tinypy_value_t *list) {
    if (vm->cycle_diagnostics != NULL) {
        tinypy_internal_cycle_diagnostics_list_clear_enabled(vm, list);
    }
}
static inline void __tinypy_internal_cycle_diagnostics_list_reindex(tinypy_vm_t *vm, tinypy_value_t *list) {
    if (vm->cycle_diagnostics != NULL) {
        tinypy_internal_cycle_diagnostics_list_reindex_enabled(vm, list);
    }
}
static inline void __tinypy_internal_cycle_diagnostics_dict_set(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value, tinypy_bool_t inserted) {
    if (vm->cycle_diagnostics != NULL) {
        tinypy_internal_cycle_diagnostics_dict_set_enabled(vm, dict, key, value, inserted);
    }
}
static inline void __tinypy_internal_cycle_diagnostics_dict_delete(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key) {
    if (vm->cycle_diagnostics != NULL) {
        tinypy_internal_cycle_diagnostics_dict_delete_enabled(vm, dict, key);
    }
}
static inline void __tinypy_internal_cycle_diagnostics_dict_clear(tinypy_vm_t *vm, tinypy_value_t *dict) {
    if (vm->cycle_diagnostics != NULL) {
        tinypy_internal_cycle_diagnostics_dict_clear_enabled(vm, dict);
    }
}
static inline void __tinypy_internal_cycle_diagnostics_cell_set(tinypy_vm_t *vm, tinypy_value_t *cell, tinypy_value_t *content) {
    if (vm->cycle_diagnostics != NULL) {
        tinypy_internal_cycle_diagnostics_cell_set_enabled(vm, cell, content);
    }
}
#endif
size_t tinypy_internal_value_allocation_size(const tinypy_value_t *value);
size_t tinypy_internal_variable_builtin_payload_size(const tinypy_value_t *value);
size_t tinypy_internal_builtin_subclass_allocation_size(const tinypy_type_t *type, size_t payload_size);
tinypy_value_t *tinypy_internal_immutable_subclass_copy(tinypy_type_t *type, tinypy_value_t *value, tinypy_error_t **out_error);
void tinypy_internal_value_destroy(tinypy_value_t *value);
void tinypy_internal_value_release_zero(tinypy_value_t *value);
void tinypy_internal_integer_free_list_finalize(tinypy_vm_t *vm);
const uint8_t *tinypy_internal_text_bytes(const tinypy_value_t *value);
size_t tinypy_internal_text_byte_size(const tinypy_value_t *value);
tinypy_value_t *tinypy_internal_text_allocate_uninitialized(tinypy_vm_t *vm, tinypy_value_type_e type, size_t byte_size, size_t code_point_count, uint8_t **out_bytes);
tinypy_value_t *tinypy_internal_string_concat_in_place(tinypy_vm_t *vm, tinypy_value_t *left, const uint8_t *bytes, size_t size);
tinypy_bool_t tinypy_internal_string_is_interned(const tinypy_value_t *value);
void tinypy_internal_string_set_interned(tinypy_value_t *value, tinypy_bool_t interned);
tinypy_bool_t tinypy_internal_string_intern(tinypy_value_t **owned_value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_string_find(const tinypy_vm_t *vm, const void *bytes, size_t size);
void tinypy_internal_string_unintern(tinypy_value_t *value);
void tinypy_internal_intern_finalize(tinypy_vm_t *vm);

void tinypy_internal_tuple_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_tuple_subclass_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_tuple_subclass_destroy(tinypy_value_t *value);
tinypy_value_t *tinypy_internal_tuple_from_borrowed_items(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size);
tinypy_value_t *tinypy_internal_tuple_subclass_from_items(tinypy_type_t *type, tinypy_value_t *const *items, size_t size, tinypy_error_t **out_error);
tinypy_value_t *const *tinypy_internal_tuple_items(const tinypy_value_t *value);
void tinypy_internal_list_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_list_destroy(tinypy_value_t *value);
void tinypy_internal_list_swap_contents(tinypy_value_t *left, tinypy_value_t *right);
void tinypy_internal_list_reserve(tinypy_vm_t *vm, tinypy_value_t *list, size_t minimum_capacity);
tinypy_value_t *tinypy_internal_list_from_items_checked(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_dict_new_checked(tinypy_vm_t *vm, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_list_reserve_checked(tinypy_vm_t *vm, tinypy_value_t *list, size_t minimum_capacity, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_list_extend_checked(tinypy_value_t *list, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_list_extend_iterable(tinypy_value_t *list, tinypy_value_t *iterable, const char *negative_hint_message, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_list_append_checked(tinypy_value_t *list, tinypy_value_t *item, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_list_insert_checked(tinypy_value_t *list, size_t index, tinypy_value_t *item, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_list_replace_range_checked(tinypy_value_t *list, size_t start, size_t count, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_list_replace_strided_checked(tinypy_value_t *list, size_t start, int64_t step, size_t count, tinypy_value_t *const *items, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_list_delete_strided_checked(tinypy_value_t *list, size_t start, int64_t step, size_t count, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_tuple_new_checked(tinypy_vm_t *vm, size_t size, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_tuple_from_items_checked(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_tuple_prepend_checked(tinypy_vm_t *vm, tinypy_value_t *first, const tinypy_value_t *tail, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_tuple_join_items_checked(tinypy_vm_t *vm, tinypy_value_t *first, tinypy_value_t *const *left, size_t left_size, tinypy_value_t *const *right, size_t right_size, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_tuple_concat_checked(tinypy_vm_t *vm, const tinypy_value_t *left, const tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_tuple_tail_checked(tinypy_vm_t *vm, const tinypy_value_t *tuple, size_t start, tinypy_error_t **out_error);
void tinypy_internal_list_shrink_to_fit(tinypy_vm_t *vm, tinypy_value_t *list);
void tinypy_internal_dict_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_dict_destroy(tinypy_value_t *value);
void tinypy_internal_dict_initialize_empty(tinypy_value_t *dict);
tinypy_value_t *tinypy_internal_dict_copy(tinypy_value_t *source, tinypy_error_t **out_error);
void tinypy_internal_dict_swap_contents(tinypy_value_t *left, tinypy_value_t *right);
void tinypy_internal_dict_reserve(tinypy_vm_t *vm, tinypy_value_t *dict, size_t minimum_used);
tinypy_bool_t tinypy_internal_dict_reserve_checked(tinypy_vm_t *vm, tinypy_value_t *dict, size_t minimum_used, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_dict_get_optional(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key);
tinypy_value_t *tinypy_internal_dict_get_optional_suppressed(tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key);
tinypy_value_t *tinypy_internal_dict_get_optional_suppressed_status(tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_bool_t *out_succeeded);
tinypy_bool_t tinypy_internal_dict_get_global(tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_value_t **out_value, tinypy_bool_t *out_cacheable, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_dict_get_optional_checked(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_value_t **out_value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_dict_get_optional_index_checked(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, size_t *out_index, tinypy_value_t **out_value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_dict_lookup_hash_checked(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_hash_t hash, size_t *out_index, tinypy_bool_t *out_found, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_dict_contains_checked(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, tinypy_bool_t *out_contains, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_dict_set_checked(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_dict_set_hash_checked(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *value, tinypy_hash_t hash, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_dict_update_from(tinypy_value_t *target, tinypy_value_t *source, const char *negative_hint_message, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_dict_update_mapping(tinypy_value_t *target, tinypy_value_t *source, tinypy_value_t *keys_method, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_dict_delete_optional_checked(tinypy_vm_t *vm, tinypy_value_t *dict, const tinypy_value_t *key, tinypy_bool_t *out_deleted, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_compare_builtin_value(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_error_t **out_error);
int32_t tinypy_internal_comparison_fallback_order(tinypy_value_t *left, tinypy_value_t *right);
tinypy_bool_t tinypy_internal_compare_three_way(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_comparison_is_exact_builtin(const tinypy_value_t *value);
int32_t tinypy_internal_truth_builtin(tinypy_value_t *value, tinypy_error_t **out_error);
int32_t tinypy_internal_contains_builtin(tinypy_value_t *container, tinypy_value_t *item, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_iter_builtin(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_object_repr_builtin(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_object_str_builtin(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_operator_builtin(tinypy_value_t *left, tinypy_value_t *right, int32_t mode, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_unary_builtin(tinypy_value_t *value, int32_t mode, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_get_slice(tinypy_value_t *container, tinypy_value_t *start, tinypy_value_t *stop, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_set_slice(tinypy_value_t *container, tinypy_value_t *start, tinypy_value_t *stop, tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_delete_slice(tinypy_value_t *container, tinypy_value_t *start, tinypy_value_t *stop, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_get_item_builtin(tinypy_value_t *container, tinypy_value_t *key, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_set_item_builtin(tinypy_value_t *container, tinypy_value_t *key, tinypy_value_t *value, tinypy_bool_t legacy_slice, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_delete_item_builtin(tinypy_value_t *container, tinypy_value_t *key, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_dict_delete_index(tinypy_vm_t *vm, tinypy_value_t *dict, size_t index, tinypy_value_t **out_key, tinypy_value_t **out_value);
tinypy_value_t *tinypy_internal_dict_get_optional_index(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, size_t *out_index, tinypy_value_t **out_stored_key);
tinypy_value_t *tinypy_internal_dict_get_index_hint(const tinypy_vm_t *vm, const tinypy_value_t *dict, const tinypy_value_t *key, size_t index);
tinypy_bool_t tinypy_internal_dict_delete_optional(tinypy_vm_t *vm, tinypy_value_t *dict, const tinypy_value_t *key);
tinypy_bool_t tinypy_internal_dict_equal(const tinypy_value_t *left, const tinypy_value_t *right);
tinypy_bool_t tinypy_internal_dict_equal_checked(const tinypy_value_t *left, const tinypy_value_t *right, tinypy_bool_t *out_equal, tinypy_error_t **out_error);
void tinypy_internal_set_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_set_initialize_empty(tinypy_value_t *value);
void tinypy_internal_set_swap_contents(tinypy_value_t *left, tinypy_value_t *right);
tinypy_value_t *tinypy_internal_set_iter(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_set_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_frozenset_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_set_update_iterable(tinypy_value_t *set, tinypy_value_t *iterable, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_power_modulo(tinypy_value_t *base, tinypy_value_t *exponent, tinypy_value_t *modulus, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_power_modulo_builtin(tinypy_value_t *base, tinypy_value_t *exponent, tinypy_value_t *modulus, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_index_as_i64(tinypy_value_t *value, int64_t *out_value, tinypy_bool_t clamp_overflow, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_index_value(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_integer_as_ssize(tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_number_as_ssize(tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_number_as_i64(tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_text_codec(tinypy_vm_t *vm, tinypy_value_t *text, tinypy_value_t *encoding, tinypy_value_t *errors, tinypy_bool_t decode, tinypy_bool_t final, size_t *out_consumed, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_object_encode_attribute_name(tinypy_value_t *key, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_codecs_validate_name(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_dict_setdefault_checked(tinypy_value_t *dict, tinypy_value_t *key, tinypy_value_t *default_value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_slice_unpack(tinypy_value_t *slice_value, tinypy_internal_slice_indices_t *indices, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_slice_adjust_indices(tinypy_vm_t *vm, size_t size, tinypy_internal_slice_indices_t *indices, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_slice_indices(tinypy_value_t *slice_value, size_t size, tinypy_internal_slice_indices_t *out_indices, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_string_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_object_unicode(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_bytearray_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_bytearray_destroy(tinypy_value_t *value);
void tinypy_internal_bytearray_swap_contents(tinypy_value_t *left, tinypy_value_t *right);
tinypy_bool_t tinypy_internal_bytearray_resize_allowed(tinypy_value_t *value, size_t size, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_bytearray_initialize(tinypy_value_t *value, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
ptrdiff_t tinypy_internal_bytearray_length(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_bytearray_get_item(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_bytearray_set_item(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *item, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_bytearray_repr(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_bytearray_string(tinypy_value_t *value, tinypy_error_t **out_error);
void tinypy_internal_initialize_bytearray_methods(tinypy_vm_t *vm);
tinypy_bool_t tinypy_internal_bytes_view(const tinypy_value_t *value, const uint8_t **out_bytes, size_t *out_size);
tinypy_value_t *tinypy_internal_bytearray_concat_bytes(tinypy_vm_t *vm, const uint8_t *left_bytes, size_t left_size, const uint8_t *right_bytes, size_t right_size, tinypy_error_t **out_error);
tinypy_type_t *tinypy_internal_type_new_configured(tinypy_value_t *name, const tinypy_type_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_value_t *namespace_dict, tinypy_bool_t has_instance_dict, tinypy_bool_t has_weakrefs, tinypy_error_t **out_error);
tinypy_value_t **tinypy_internal_object_dict_slot(tinypy_value_t *value);
tinypy_value_t **tinypy_internal_object_member_slot(tinypy_value_t *value, size_t index);
tinypy_value_t **tinypy_internal_weakref_head_slot(tinypy_value_t *value);
void tinypy_internal_weakref_clear(tinypy_value_t *value);
void tinypy_internal_weakref_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_weakref_destroy(tinypy_value_t *value);
tinypy_value_t *tinypy_internal_weakref_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_weakref_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_hash_t tinypy_internal_weakref_hash(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_weakref_compare(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error);
void tinypy_internal_initialize_weakref_type(tinypy_vm_t *vm);
void tinypy_internal_initialize_weakref_module(tinypy_vm_t *vm);
void tinypy_internal_initialize_codecs_module(tinypy_vm_t *vm);
tinypy_value_t *tinypy_internal_codecs_transform_registered(tinypy_vm_t *vm, tinypy_value_t *input, tinypy_value_t *encoding, tinypy_value_t *errors, tinypy_bool_t decode, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_codecs_lookup_error(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_error_t **out_error);
void tinypy_internal_initialize_functools_module(tinypy_vm_t *vm);
void tinypy_internal_initialize_struct_module(tinypy_vm_t *vm);
void tinypy_internal_initialize_copy_reg_module(tinypy_vm_t *vm);
void tinypy_internal_initialize_representation_types(tinypy_vm_t *vm);
tinypy_value_t *tinypy_internal_sre_match_regs(tinypy_value_t *match_value);
void tinypy_internal_initialize_sre_module(tinypy_vm_t *vm);
void tinypy_internal_sre_pattern_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_sre_pattern_destroy(tinypy_value_t *value);
void tinypy_internal_sre_match_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_sre_match_destroy(tinypy_value_t *value);
void tinypy_internal_partial_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_partial_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_partial_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_initialize_partial_type(tinypy_vm_t *vm);
void tinypy_internal_initialize_exceptions_module(tinypy_vm_t *vm);
void tinypy_internal_register_module(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_value_t *module);
tinypy_value_t *tinypy_internal_reload_module(tinypy_value_t *module, tinypy_error_t **out_error);
void tinypy_internal_dict_view_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
ptrdiff_t tinypy_internal_dict_view_length(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_dict_view_iter(tinypy_value_t *value, tinypy_error_t **out_error);
int32_t tinypy_internal_dict_view_contains(tinypy_value_t *value, tinypy_value_t *item, tinypy_error_t **out_error);
void tinypy_internal_initialize_dict_view_types(tinypy_vm_t *vm);
void tinypy_internal_initialize_dictproxy_type(tinypy_vm_t *vm);
tinypy_value_t *tinypy_internal_dictproxy_new(tinypy_vm_t *vm, tinypy_value_t *dict);
tinypy_bool_t tinypy_internal_dictproxy_check(const tinypy_value_t *value);
tinypy_value_t *tinypy_internal_dictproxy_dict(const tinypy_value_t *value);
tinypy_value_t *tinypy_internal_type_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_type_subclasses(tinypy_type_t *type, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_type_layout_compatible(const tinypy_type_t *old_type, const tinypy_type_t *new_type, const char *attribute, size_t attribute_size, tinypy_error_t **out_error);
tinypy_type_t *tinypy_internal_type_new_from_values(tinypy_value_t *name, tinypy_value_t *const *bases, size_t base_count, const tinypy_type_t *explicit_metaclass, tinypy_value_t *namespace_dict, tinypy_error_t **out_error);
tinypy_type_t *tinypy_internal_type_new_from_tuple(tinypy_value_t *name, tinypy_value_t *bases, const tinypy_type_t *explicit_metaclass, tinypy_value_t *namespace_dict, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_type_mro_value_at(const tinypy_type_t *type, size_t index);
tinypy_value_t *tinypy_internal_object_builtin_attribute(tinypy_value_t *value, tinypy_value_t *key);
tinypy_value_t *tinypy_internal_type_compute_mro(tinypy_type_t *type, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_type_base_value_at(const tinypy_type_t *type, size_t index);
tinypy_value_t *tinypy_internal_type_mro_entry_dict(tinypy_value_t *entry);
tinypy_bool_t tinypy_internal_type_set_bases(tinypy_type_t *type, tinypy_value_t *bases, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_object_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_bool_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_integer_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_long_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_float_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_complex_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_unicode_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_constructor_optional_arguments(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_value_t *const *names, size_t maximum, uint32_t text_arguments, tinypy_value_t **outputs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_constructor_keyword_optional(tinypy_value_t *kwargs, tinypy_value_t *name);
int32_t tinypy_internal_d2s_buffered_n(double value, char *result);
tinypy_value_t *tinypy_internal_tuple_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_list_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_dict_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_set_equal(const tinypy_value_t *left, const tinypy_value_t *right);
tinypy_bool_t tinypy_internal_set_equal_checked(const tinypy_value_t *left, const tinypy_value_t *right, tinypy_bool_t *out_equal, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_set_is_subset_checked(const tinypy_value_t *left, const tinypy_value_t *right, tinypy_bool_t *out_subset, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_set_like_compare_checked(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_bool_t *out_result, tinypy_error_t **out_error);
tinypy_hash_t tinypy_internal_frozenset_hash(const tinypy_value_t *value);
tinypy_value_t *tinypy_internal_set_binary(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error);
void tinypy_internal_initialize_set_types(tinypy_vm_t *vm);
tinypy_value_t *tinypy_internal_output_stream_new(tinypy_vm_t *vm, tinypy_output_channel_e channel);
void tinypy_internal_initialize_output_type(tinypy_vm_t *vm);
tinypy_bool_t tinypy_internal_output_write(tinypy_value_t *target, const void *bytes, size_t size, tinypy_error_t **out_error);
void tinypy_internal_output_unraisable(tinypy_vm_t *vm, tinypy_value_t *object);
tinypy_bool_t tinypy_internal_output_write_value(tinypy_value_t *target, tinypy_value_t *text, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_output_soft_space(tinypy_value_t *target, tinypy_bool_t new_flag);
void tinypy_internal_type_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_type_destroy(tinypy_value_t *value);
void tinypy_internal_type_lookup_cache_invalidate(tinypy_vm_t *vm);
void tinypy_internal_type_detach(tinypy_type_t *type);
void tinypy_internal_type_modified(tinypy_type_t *type);
void tinypy_internal_type_lookup_cache_finalize(tinypy_vm_t *vm);
tinypy_value_t *tinypy_internal_type_lookup_key(tinypy_vm_t *vm, const tinypy_type_t *type, tinypy_value_t *key);
void tinypy_internal_type_set_attr_key(tinypy_type_t *type, tinypy_value_t *key, tinypy_value_t *value);
tinypy_bool_t tinypy_internal_type_set_name(tinypy_type_t *type, tinypy_value_t *value, tinypy_error_t **out_error);
void tinypy_internal_instance_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_type_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_class_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_old_instance_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_class_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_class_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_class_get_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_old_instance_get_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_class_set_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_old_instance_set_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_class_delete_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_old_instance_set_class(tinypy_value_t *instance_value, tinypy_value_t *class_value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_old_instance_set_dict(tinypy_value_t *instance_value, tinypy_value_t *dict_value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_old_instance_delete_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_class_lookup_key(tinypy_vm_t *vm, tinypy_value_t *class_value, tinypy_value_t *key);
tinypy_bool_t tinypy_internal_old_instance_has_special_key(tinypy_value_t *value, tinypy_value_t *key);
tinypy_bool_t tinypy_internal_object_has_special_key(tinypy_value_t *value, tinypy_value_t *key);
tinypy_bool_t tinypy_internal_object_has_special_override_key(tinypy_value_t *value, tinypy_value_t *key);
void tinypy_internal_object_initialize_special_keys(tinypy_vm_t *vm);
/* Owned preset name, or an ordinary owned string for names outside the registry. */
tinypy_value_t *tinypy_internal_name_from_bytes(tinypy_vm_t *vm, const char *name, size_t name_size);
/* Borrowed, interned and retained by the VM; this API is for C literals only. */
tinypy_value_t *tinypy_internal_string_from_literal(tinypy_vm_t *vm, const char *bytes, size_t size);
#define TINYPY_INTERNAL_STRING(vm, literal) tinypy_internal_string_from_literal((vm), "" literal, sizeof("" literal) - 1U)
/* Registration borrows same-VM names and transfers user_data to the function's
 * finalizer. Descriptor binding and module metadata follow the public setters. */
void tinypy_internal_module_add_function(tinypy_value_t *module, tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize);
void tinypy_internal_type_add_method(tinypy_type_t *type, tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize, tinypy_native_descriptor_kind_e descriptor_kind);
void tinypy_internal_type_add_property(tinypy_type_t *type, tinypy_value_t *name, tinypy_native_function_callback_t getter, void *user_data, tinypy_native_function_finalize_t finalize);
void tinypy_internal_type_add_class_method(tinypy_type_t *type, tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize);
void tinypy_internal_type_add_static_method(tinypy_type_t *type, tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize);
/* Borrows an eager VM preset; index must be less than TINYPY_SPECIAL_OPERATOR_COUNT. */
tinypy_value_t *tinypy_internal_object_special_operator_key(tinypy_vm_t *vm, size_t index);
tinypy_value_t *tinypy_internal_type_mro_tuple(tinypy_type_t *type);
tinypy_value_t *tinypy_internal_call_conversion(tinypy_value_t *value, tinypy_value_t *name, tinypy_bool_t *out_handled, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_object_get_special_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error);
void tinypy_internal_code_destroy(tinypy_value_t *value);
void tinypy_internal_code_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_code_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_code_compare(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error);
tinypy_hash_t tinypy_internal_code_hash(tinypy_value_t *value, tinypy_error_t **out_error);
void tinypy_internal_initialize_code_type(tinypy_vm_t *vm);
void tinypy_internal_frame_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_frame_free_list_push(tinypy_vm_t *vm, tinypy_value_t *frame);
void tinypy_internal_frame_free_list_finalize(tinypy_vm_t *vm);
void tinypy_internal_frame_save_handled(tinypy_vm_t *vm);
void tinypy_internal_frame_release_fast(tinypy_frame_object_t *frame);
tinypy_value_t *tinypy_internal_frame_new_function(tinypy_value_t *code, tinypy_value_t *globals);
tinypy_value_t *tinypy_internal_frame_locals(tinypy_frame_object_t *frame);
void tinypy_internal_frame_locals_to_fast(tinypy_frame_object_t *frame);
tinypy_value_t *tinypy_internal_frame_locals_get(tinypy_vm_t *vm, tinypy_value_t *mapping, tinypy_value_t *name, tinypy_error_t **out_error);
void tinypy_internal_function_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_function_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_function_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_initialize_function_type(tinypy_vm_t *vm);
tinypy_value_t *tinypy_internal_eval_function(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_eval_function_items(tinypy_value_t *function, tinypy_value_t *const *items, size_t item_count, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_eval_generator_resume(tinypy_generator_object_t *generator, tinypy_value_t *send_value, tinypy_value_t *throw_type, tinypy_value_t *throw_value, tinypy_value_t *throw_traceback, tinypy_bool_t *out_yielded, tinypy_error_t **out_error);
void tinypy_internal_iterator_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_iterator_iter(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_iterator_next(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_next_raw(tinypy_value_t *iterator, tinypy_error_t **out_error);
void tinypy_internal_iterator_clear(tinypy_iterator_object_t *iterator);
tinypy_value_t *tinypy_internal_dict_iterator_new(tinypy_value_t *dict, int32_t mode);
tinypy_value_t *tinypy_internal_call_iterator_new(tinypy_value_t *callable, tinypy_value_t *sentinel, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_formatter_iterator_new(tinypy_value_t *text, int32_t mode);
tinypy_value_t *tinypy_internal_string_formatter_parser_next(tinypy_iterator_object_t *iterator, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_string_formatter_field_next(tinypy_iterator_object_t *iterator, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_function_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_native_function_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
void tinypy_internal_native_function_set_descriptor_kind(tinypy_value_t *function, tinypy_native_descriptor_kind_e descriptor_kind);
tinypy_value_t *tinypy_internal_method_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
void tinypy_internal_method_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_method_free_list_push(tinypy_vm_t *vm, tinypy_value_t *value);
void tinypy_internal_method_free_list_finalize(tinypy_vm_t *vm);
tinypy_value_t *tinypy_internal_method_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_method_compare(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_method_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_cell_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_cell_compare(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error);
tinypy_hash_t tinypy_internal_cell_hash(tinypy_value_t *value, tinypy_error_t **out_error);
void tinypy_internal_initialize_cell_type(tinypy_vm_t *vm);
void tinypy_internal_slice_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_slice_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_module_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_module_traverse_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_module_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_initialize_module_type(tinypy_vm_t *vm);
tinypy_value_t *tinypy_internal_module_from_dict_key(tinypy_value_t *name, tinypy_value_t *dict);
tinypy_value_t *tinypy_internal_import_from(tinypy_value_t *module, tinypy_value_t *name, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_import_star(tinypy_value_t *module, tinypy_value_t *locals, tinypy_error_t **out_error);
void tinypy_internal_native_function_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_native_function_destroy(tinypy_value_t *value);
void tinypy_internal_native_function_finalize(tinypy_value_t *value);
tinypy_value_t *tinypy_internal_native_function_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_native_function_call_items(tinypy_value_t *callable, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_native_method_free_list_push(tinypy_vm_t *vm, tinypy_value_t *value);
void tinypy_internal_native_method_free_list_finalize(tinypy_vm_t *vm);
void tinypy_internal_initialize_native_function_type(tinypy_vm_t *vm);
void tinypy_internal_initialize_native_descriptor_types(tinypy_vm_t *vm);
void tinypy_internal_native_instance_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_native_instance_destroy(tinypy_value_t *value);
void tinypy_internal_native_instance_finalize(tinypy_value_t *value);
void tinypy_internal_callable_descriptor_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_static_method_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_class_method_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_static_method_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_class_method_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_property_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_c_descriptor_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_c_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_c_descriptor_set(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_member_descriptor_new(tinypy_type_t *owner, tinypy_value_t *name, size_t index);
tinypy_value_t *tinypy_internal_instance_dict_descriptor_new(tinypy_type_t *owner);
tinypy_value_t *tinypy_internal_instance_weakref_descriptor_new(tinypy_type_t *owner);
tinypy_value_t *tinypy_internal_property_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_property_set(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_property_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
void tinypy_internal_super_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_super_get_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_super_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_super_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
void tinypy_internal_initialize_super_type(tinypy_vm_t *vm);
void tinypy_internal_initialize_descriptor_types(tinypy_vm_t *vm);
void tinypy_internal_initialize_exception_descriptors(tinypy_type_t *type);
tinypy_bool_t tinypy_internal_descriptor_is_data(tinypy_vm_t *vm, tinypy_value_t *attribute);
tinypy_bool_t tinypy_internal_descriptor_has_get(tinypy_vm_t *vm, tinypy_value_t *attribute);
tinypy_value_t *tinypy_internal_object_get_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_object_get_base_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error);
int32_t tinypy_internal_object_get_optional_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t **out_value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_object_set_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *attribute_value, tinypy_error_t **out_error);
void tinypy_internal_object_make_attribute_error_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_object_set_attr_protocol_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *attribute_value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_object_delete_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_object_delete_attr_protocol_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_descriptor_get_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_descriptor_set_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_descriptor_delete_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_error_t **out_error);
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
tinypy_bool_t tinypy_internal_compile_options_inherit_frame(tinypy_vm_t *vm, tinypy_compile_options_t *options);
void tinypy_internal_exception_preserve_begin(tinypy_vm_t *vm, tinypy_internal_exception_state_t *state);
void tinypy_internal_exception_preserve_end(tinypy_vm_t *vm, tinypy_internal_exception_state_t *state);
void tinypy_internal_exception_set_raised(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *traceback);
void tinypy_internal_exception_set_raised_type(tinypy_vm_t *vm, tinypy_value_t *type, tinypy_value_t *value, tinypy_value_t *traceback);
void tinypy_internal_sys_publish_handled_exception(tinypy_vm_t *vm);
void tinypy_internal_exception_set_handled_from_raised(tinypy_vm_t *vm);
void tinypy_internal_exception_restore_handled(tinypy_vm_t *vm, tinypy_value_t *type, tinypy_value_t *value, tinypy_value_t *traceback);
void tinypy_internal_exception_restore_raised_from_handled(tinypy_vm_t *vm);
void tinypy_internal_exception_make_diagnostic(tinypy_vm_t *vm, tinypy_error_t **out_error);
void tinypy_internal_exception_raise_key_error(tinypy_vm_t *vm, tinypy_value_t *key, tinypy_error_t **out_error);
void tinypy_internal_exception_raise_stop_iteration(tinypy_vm_t *vm, tinypy_error_t **out_error);
void tinypy_internal_exception_raise_system_error(tinypy_vm_t *vm, const char *message, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_exception_consume_stop_iteration(tinypy_vm_t *vm, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_exception_consume_kind(tinypy_vm_t *vm, tinypy_exception_type_index_e index, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_exception_prefix_raised(tinypy_vm_t *vm, tinypy_exception_type_index_e index, const char *prefix, size_t prefix_size, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_object_is_mapping(tinypy_vm_t *vm, tinypy_value_t *value);
int32_t tinypy_internal_object_is_instance(tinypy_value_t *object, tinypy_value_t *classinfo, tinypy_error_t **out_error);
void tinypy_internal_traceback_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_traceback_new(tinypy_value_t *frame, tinypy_value_t *next);
void tinypy_internal_traceback_here(tinypy_vm_t *vm, tinypy_frame_object_t *frame);
void tinypy_internal_generator_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_generator_iter(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_generator_next(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_generator_from_frame(tinypy_value_t *frame);
void tinypy_internal_initialize_generator_types(tinypy_vm_t *vm);
void tinypy_internal_initialize_builtin_functions(tinypy_vm_t *vm);
tinypy_value_t *tinypy_internal_functools_reduce(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error);
void tinypy_internal_initialize_container_types(tinypy_vm_t *vm);
void tinypy_internal_initialize_slice_type(tinypy_vm_t *vm);
void tinypy_internal_initialize_numeric_types(tinypy_vm_t *vm);
void tinypy_internal_initialize_string_types(tinypy_vm_t *vm);
void tinypy_internal_initialize_constructor_types(tinypy_vm_t *vm);
void tinypy_internal_constructor_add_builtin_new(tinypy_type_t *type);
tinypy_value_t *tinypy_internal_string_percent(tinypy_value_t *format, tinypy_value_t *arguments, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_string_format_value(tinypy_vm_t *vm, tinypy_value_t *value, int32_t conversion, const uint8_t *spec, size_t spec_size, tinypy_bool_t spec_unicode, tinypy_bool_t *out_unicode, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_string_format_object(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *format_spec, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_string_format_builtin_value(tinypy_vm_t *vm, tinypy_value_t *value, int32_t conversion, const uint8_t *spec, size_t spec_size, tinypy_bool_t spec_unicode, tinypy_bool_t *out_unicode, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_xrange_new(tinypy_vm_t *vm, int64_t start, int64_t step, size_t length);
int64_t tinypy_internal_xrange_item_value(const tinypy_xrange_object_t *range, size_t index);
int64_t tinypy_internal_xrange_stop_value(const tinypy_xrange_object_t *range);
size_t tinypy_internal_iterable_size_hint(const tinypy_value_t *value);
tinypy_bool_t tinypy_internal_length_hint(tinypy_value_t *value, int64_t default_hint, int64_t *out_hint, tinypy_error_t **out_error);
size_t tinypy_internal_iterator_size_hint(const tinypy_iterator_object_t *iterator);
size_t tinypy_internal_reversed_size_hint(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_xrange_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_enumerate_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_reversed_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_enumerate_new(tinypy_value_t *iterable, tinypy_value_t *start, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_reversed_new(tinypy_value_t *sequence, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_xrange_iter(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_set_iterator_new(tinypy_value_t *value);
tinypy_value_t *tinypy_internal_enumerate_iter(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_enumerate_next(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_reversed_iter(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_reversed_next(tinypy_value_t *value, tinypy_error_t **out_error);
void tinypy_internal_enumerate_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_reversed_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
void tinypy_internal_initialize_iterator_types(tinypy_vm_t *vm);
void tinypy_internal_buffer_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data);
tinypy_value_t *tinypy_internal_buffer_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_buffer_get_item(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error);
ptrdiff_t tinypy_internal_buffer_length(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_buffer_repr(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_buffer_string(tinypy_value_t *value, tinypy_error_t **out_error);
void tinypy_internal_initialize_buffer_type(tinypy_vm_t *vm);
void tinypy_internal_initialize_memoryview_type(tinypy_vm_t *vm);
tinypy_bool_t tinypy_internal_memoryview_check(const tinypy_value_t *value);
const uint8_t *tinypy_internal_memoryview_view(const tinypy_value_t *value, size_t *out_size);
tinypy_bool_t tinypy_internal_memoryview_is_readonly(const tinypy_value_t *value);

tinypy_hash_t tinypy_internal_hash_value(const tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_hash_t tinypy_internal_hash_bytes(const tinypy_vm_t *vm, const uint8_t *bytes, size_t size);
tinypy_hash_t tinypy_internal_hash_builtin_value(const tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_internal_equal_value(const tinypy_value_t *left, const tinypy_value_t *right, tinypy_bool_t identity_implies_equal);
tinypy_bool_t tinypy_internal_numeric_order(const tinypy_value_t *left, const tinypy_value_t *right, int32_t *out_order);
int32_t tinypy_internal_text_order(const tinypy_value_t *left, const tinypy_value_t *right);
//////////////////////////////////////////////////////////////////////////
static inline tinypy_integer_object_t *__tinypy_internal_integer_free_next(const tinypy_integer_object_t *value)
{
    tinypy_integer_object_t *next;

    (void)memcpy(&next, &value->integer_value, sizeof(next));
    return next;
}
//////////////////////////////////////////////////////////////////////////
static inline void __tinypy_internal_integer_set_free_next(tinypy_integer_object_t *value, tinypy_integer_object_t *next)
{
    (void)memcpy(&value->integer_value, &next, sizeof(next));
}
//////////////////////////////////////////////////////////////////////////
static inline tinypy_value_t *__tinypy_internal_integer_from_i64_fast(tinypy_vm_t *vm, int64_t value)
{
    tinypy_value_t *result;

    if (value >= TINYPY_INTEGER_CONSTANT_MIN && value <= TINYPY_INTEGER_CONSTANT_MAX) {
        size_t index = (size_t)(value - TINYPY_INTEGER_CONSTANT_MIN);

        result = TINYPY_RET(&vm->integer_constants[index].base);
        return result;
    }
    if (vm->integer_free_list != NULL) {
        tinypy_integer_object_t *integer = vm->integer_free_list;

        vm->integer_free_list = __tinypy_internal_integer_free_next(integer);
        vm->integer_free_count -= 1U;
        result = &integer->base;
        result->ref = 1;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_value_reuse(vm, result);
#endif
        TINYPY_INTEGER_VALUE(result) = value;
        return result;
    }
    result = tinypy_internal_value_allocate(vm, TINYPY_VALUE_INTEGER, sizeof(tinypy_integer_object_t));
    TINYPY_INTEGER_VALUE(result) = value;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static inline void __tinypy_internal_value_release_zero_fast(tinypy_value_t *value)
{
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    if (value->type == &vm->types[TINYPY_VALUE_INTEGER] && vm->state == TINYPY_VM_STATE_LIVE && vm->integer_free_count < TINYPY_INTEGER_FREE_LIST_MAX) {
        tinypy_integer_object_t *integer = TINYPY_INTEGER_OBJECT(value);

        __tinypy_internal_integer_set_free_next(integer, vm->integer_free_list);
        vm->integer_free_list = integer;
        vm->integer_free_count += 1U;
        return;
    }
    tinypy_internal_value_release_zero(value);
}
//////////////////////////////////////////////////////////////////////////
#endif
