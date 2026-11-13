#ifndef TINYPY_COMPILER_H
#define TINYPY_COMPILER_H

#include "tinypy/vm.h"

#define TINYPY_COMPILER_ABI_VERSION UINT32_C(2)
#define TINYPY_PREPROCESSOR_ABI_VERSION UINT32_C(5)
#define TINYPY_META_ABI_VERSION UINT32_C(2)
#define TINYPY_BUILD_PROFILE_DIGEST_SIZE ((size_t)32U)

/* Required pointers and typed/indexed accessors follow the tinypy C API
 * precondition policy from tinypy.h. Invalid profile descriptors, ABI
 * versions, limits and constant values remain recoverable result codes. */

typedef enum tinypy_compile_mode_e {
    TINYPY_COMPILE_EXEC = 1,
    TINYPY_COMPILE_EVAL = 2,
    TINYPY_COMPILE_SINGLE = 3
} tinypy_compile_mode_e;

typedef enum tinypy_compile_feature_e {
    TINYPY_COMPILE_FEATURE_PREPROCESSOR = UINT32_C(1) << 0,
    TINYPY_COMPILE_FEATURE_META = UINT32_C(1) << 1
} tinypy_compile_feature_e;

/* A host source is the text of a file named by its logical filename and
 * decodes like Python's file tokenizer: ASCII unless a PEP 263 cookie or a
 * UTF-8 BOM declares its encoding. TINYPY_COMPILE_FLAG_STRING_SOURCE compiles
 * it as the byte string of compile(), exec and eval instead. Python checks
 * the consistency of tabs and spaces in indentation only with -t (a warning
 * on sys.stderr) or -tt (TabError); the two tab flags select those checks. */
typedef enum tinypy_compile_flag_e {
    TINYPY_COMPILE_FLAG_DONT_IMPLY_DEDENT = 0x0200,
    TINYPY_COMPILE_FLAG_STRING_SOURCE = 0x0400,
    TINYPY_COMPILE_FLAG_TAB_WARNING = 0x0800,
    TINYPY_COMPILE_FLAG_TAB_ERROR = 0x1000,
    TINYPY_COMPILE_FLAG_FUTURE_DIVISION = 0x2000,
    TINYPY_COMPILE_FLAG_FUTURE_ABSOLUTE_IMPORT = 0x4000,
    TINYPY_COMPILE_FLAG_FUTURE_WITH_STATEMENT = 0x8000,
    TINYPY_COMPILE_FLAG_FUTURE_PRINT_FUNCTION = 0x10000,
    TINYPY_COMPILE_FLAG_FUTURE_UNICODE_LITERALS = 0x20000
} tinypy_compile_flag_e;

/* The compile functions read the options and the limits within their
 * struct_size: the fields that a smaller size predates keep the values of
 * tinypy_compile_options_init and tinypy_compile_limits_init. The first
 * layout of tinypy_compile_limits_t ended after max_arena_bytes; another
 * abi_version or a struct_size below the first layout fails the call with
 * TINYPY_ERROR_TYPE. */
typedef struct tinypy_compile_limits_t {
    uint32_t abi_version;
    uint32_t struct_size;
    size_t max_source_bytes;
    size_t max_tokens;
    size_t max_cst_nodes;
    size_t max_ast_nodes;
    size_t max_nesting;
    size_t max_symbols;
    size_t max_blocks;
    size_t max_instructions;
    size_t max_constants;
    size_t max_constant_bytes;
    size_t max_arena_bytes;
    size_t max_preprocessor_operations;
    size_t max_preprocessor_value_nodes;
    size_t max_preprocessor_bytes;
    size_t max_template_expansions;
    size_t max_template_depth;
    size_t max_generated_ast_nodes;
    size_t max_generated_source_bytes;
    size_t max_source_map_entries;
} tinypy_compile_limits_t;

struct tinypy_compile_options_t {
    uint32_t abi_version;
    uint32_t struct_size;
    tinypy_compile_mode_e mode;
    uint32_t flags;
    tinypy_bool_t dont_inherit;
    int32_t optimize_level;
    uint32_t feature_flags;
    const tinypy_compile_limits_t *limits;
    /* The compiled code keeps a copy of the profile in the VM heap, which
     * counts against max_heap_bytes; a copy that the host or the budget
     * refuses fails the compile with MemoryError. */
    const tinypy_build_profile_t *build_profile;
};

void tinypy_compile_limits_init(tinypy_compile_limits_t *limits);
void tinypy_compile_options_init(tinypy_compile_options_t *options, tinypy_compile_mode_e mode);
/* Copy a compile environment onto code and its nested code constants, e.g.
 * after marshal loading. This controls subsequent dynamic compilation; it
 * does not rewrite existing bytecode. optimize_level, which also selects
 * __debug__, is independent of profile. Invalid settings fail with TypeError;
 * allocation refusal fails with MemoryError, leaving the old environment.
 * The caller may destroy profile after this call. out_error is optional. */
tinypy_bool_t tinypy_code_set_compile_options(tinypy_value_t *code, uint32_t feature_flags, int32_t optimize_level, const tinypy_build_profile_t *profile, tinypy_error_t **out_error);
tinypy_value_t *tinypy_compile_source(tinypy_vm_t *vm, const void *source, size_t source_size, const char *logical_filename, size_t filename_size, const tinypy_compile_options_t *options, tinypy_error_t **out_error);
tinypy_value_t *tinypy_eval_source(tinypy_vm_t *vm, const void *source, size_t source_size, const char *logical_filename, size_t filename_size, tinypy_value_t *globals, tinypy_value_t *locals, const tinypy_compile_options_t *options, tinypy_error_t **out_error);
tinypy_value_t *tinypy_exec_source(tinypy_vm_t *vm, const void *source, size_t source_size, const char *logical_filename, size_t filename_size, tinypy_value_t *globals, tinypy_value_t *locals, const tinypy_compile_options_t *options, tinypy_error_t **out_error);

typedef enum tinypy_build_value_type_e {
    TINYPY_BUILD_VALUE_NONE = 1,
    TINYPY_BUILD_VALUE_BOOL = 2,
    TINYPY_BUILD_VALUE_INTEGER = 3,
    TINYPY_BUILD_VALUE_LONG = 4,
    TINYPY_BUILD_VALUE_FLOAT = 5,
    TINYPY_BUILD_VALUE_STRING = 6,
    TINYPY_BUILD_VALUE_UNICODE = 7
} tinypy_build_value_type_e;

/* One flat, ABI-extensible descriptor holds a scalar constant.
 * Only fields belonging to type are read. Buffer inputs are borrowed for
 * create(); the resulting profile owns deep copies. Containers are unsupported. */
typedef struct tinypy_build_value_t {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t type;
    uint32_t reserved;

    int64_t integer_value;
    double float_value;

    const void *data;
    size_t data_size;

    const uint16_t *long_digits;
    size_t long_digit_count;
    int32_t long_sign;
    uint32_t reserved2;
} tinypy_build_value_t;

typedef struct tinypy_build_constant_t {
    uint32_t abi_version;
    uint32_t struct_size;
    const char *name;
    size_t name_size;
    tinypy_build_value_t value;
} tinypy_build_constant_t;

typedef struct tinypy_build_profile_limits_t {
    uint32_t abi_version;
    uint32_t struct_size;
    size_t max_allocated_bytes;
    size_t max_constants;
    /* Source nesting limit while parsing NAME=literal definitions. */
    size_t max_depth;
    size_t max_string_bytes;
} tinypy_build_profile_limits_t;

typedef enum tinypy_build_profile_result_e {
    TINYPY_BUILD_PROFILE_OK = 0,
    TINYPY_BUILD_PROFILE_INVALID_ARGUMENT = 1,
    TINYPY_BUILD_PROFILE_ABI_MISMATCH = 2,
    TINYPY_BUILD_PROFILE_SIZE_OVERFLOW = 4,
    TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED = 5,
    TINYPY_BUILD_PROFILE_INVALID_NAME = 6,
    TINYPY_BUILD_PROFILE_DUPLICATE_NAME = 7,
    TINYPY_BUILD_PROFILE_INVALID_VALUE_TYPE = 9,
    TINYPY_BUILD_PROFILE_INVALID_VALUE = 10,
    TINYPY_BUILD_PROFILE_INVALID_LONG = 11,
    TINYPY_BUILD_PROFILE_INVALID_UTF8 = 12,
    TINYPY_BUILD_PROFILE_INVALID_DEFINITION = 13
} tinypy_build_profile_result_e;

typedef struct tinypy_build_profile_error_t {
    uint32_t abi_version;
    uint32_t struct_size;
    tinypy_build_profile_result_e code;
    uint32_t reserved;
    size_t constant_index;
    const char *message;
    size_t message_size;
} tinypy_build_profile_error_t;

void tinypy_build_value_init(tinypy_build_value_t *value, tinypy_build_value_type_e type);
/* Borrow an exact builtin None/bool/int/long/float/str/unicode as a descriptor.
 * No conversion or user code runs. Subclasses and all other types fail.
 * Buffer/digit views remain valid while object is alive; create() copies them.
 * NULL arguments fail, and out_value is unchanged on failure. */
tinypy_bool_t tinypy_build_value_from_object(const tinypy_value_t *object, tinypy_build_value_t *out_value);
void tinypy_build_profile_limits_init(tinypy_build_profile_limits_t *limits);

/* A profile maps supplied names to typed constants, independent of any
 * optimize level. Names accept any ASCII Python identifier except keywords,
 * None, True, False and __debug__; no naming pattern is reserved. With
 * PREPROCESSOR, each bare-name read of a supplied name becomes its constant
 * before scope analysis and name mangling, so the match is lexical: supplied
 * names shadow builtins, and binding one in any scope is a compile error.
 * Unregistered names, misspelled ones included, remain runtime names.
 * Input order never affects the resulting sorted profile/digest.
 * A request that allocator refuses fails the call with
 * TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED, everything it allocated released.
 * limits and out_error are optional. */
tinypy_build_profile_result_e tinypy_build_profile_create(const tinypy_allocator_t *allocator, const tinypy_build_constant_t *constants, size_t constant_count, const tinypy_build_profile_limits_t *limits, tinypy_build_profile_t **out_profile, tinypy_build_profile_error_t *out_error);

/* Like create, from NAME=literal definitions whose values use Python 2.7
 * literal syntax: None, bool, signed integer/long/float, byte/Unicode
 * strings. Container literals, including tuples, are rejected. The compiler
 * of vm parses each value without executing code, only during the call;
 * the profile belongs to allocator and outlives vm. Literal decoding does not
 * use VM codecs or error handlers:
 * invalid Unicode escapes/UTF-8 and concatenation of non-ASCII byte literals
 * with Unicode fail as invalid definitions. Whitespace before a value is
 * ignored. A missing '=' or unsupported literal fails with
 * TINYPY_BUILD_PROFILE_INVALID_DEFINITION. Every error names the failed
 * definition in constant_index. */
tinypy_build_profile_result_e tinypy_build_profile_parse(tinypy_vm_t *vm, const tinypy_allocator_t *allocator, const char *const *definitions, size_t definition_count, const tinypy_build_profile_limits_t *limits, tinypy_build_profile_t **out_profile, tinypy_build_profile_error_t *out_error);

void tinypy_build_profile_destroy(tinypy_build_profile_t *profile);

size_t tinypy_build_profile_constant_count(const tinypy_build_profile_t *profile);

/* Indexed values are sorted by bytewise ASCII name. Returned pointers/views
 * are borrowed until profile destruction. */
void tinypy_build_profile_constant_at(const tinypy_build_profile_t *profile, size_t index, const char **out_name, size_t *out_name_size, const tinypy_build_value_t **out_value);

tinypy_bool_t tinypy_build_profile_find(const tinypy_build_profile_t *profile, const char *name, size_t name_size, const tinypy_build_value_t **out_value);

const uint8_t *tinypy_build_profile_digest(const tinypy_build_profile_t *profile);
const char *tinypy_build_profile_result_name(tinypy_build_profile_result_e result);

#endif
