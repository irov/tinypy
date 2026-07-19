#ifndef TINYPY_COMPILER_H
#define TINYPY_COMPILER_H

#include "tinypy/vm.h"

#define TINYPY_COMPILER_ABI_VERSION UINT32_C(1)
#define TINYPY_PREPROCESSOR_ABI_VERSION UINT32_C(1)
#define TINYPY_BUILD_PROFILE_DIGEST_SIZE ((size_t)32U)

/* Required pointers and typed/indexed accessors follow the TinyPy C API
 * precondition policy from tinypy.h. Invalid profile descriptors, ABI
 * versions, limits and constant values remain recoverable result codes. */

typedef struct tinypy_build_profile_t tinypy_build_profile_t;

typedef enum tinypy_compile_mode_e {
    TINYPY_COMPILE_EXEC = 1,
    TINYPY_COMPILE_EVAL = 2,
    TINYPY_COMPILE_SINGLE = 3
} tinypy_compile_mode_e;

typedef enum tinypy_compile_flag_e {
    TINYPY_COMPILE_FLAG_DONT_IMPLY_DEDENT = 0x0200,
    TINYPY_COMPILE_FLAG_FUTURE_DIVISION = 0x2000,
    TINYPY_COMPILE_FLAG_FUTURE_ABSOLUTE_IMPORT = 0x4000,
    TINYPY_COMPILE_FLAG_FUTURE_WITH_STATEMENT = 0x8000,
    TINYPY_COMPILE_FLAG_FUTURE_PRINT_FUNCTION = 0x10000,
    TINYPY_COMPILE_FLAG_FUTURE_UNICODE_LITERALS = 0x20000
} tinypy_compile_flag_e;

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
} tinypy_compile_limits_t;

typedef struct tinypy_compile_options_t {
    uint32_t abi_version;
    uint32_t struct_size;
    tinypy_compile_mode_e mode;
    uint32_t flags;
    int32_t dont_inherit;
    int32_t optimize_level;
    uint32_t feature_flags;
    const tinypy_compile_limits_t *limits;
    const tinypy_build_profile_t *build_profile;
} tinypy_compile_options_t;

void tinypy_compile_limits_init(tinypy_compile_limits_t *limits);
void tinypy_compile_options_init(tinypy_compile_options_t *options, tinypy_compile_mode_e mode);
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
    TINYPY_BUILD_VALUE_UNICODE = 7,
    TINYPY_BUILD_VALUE_TUPLE = 8
} tinypy_build_value_type_e;

/* One flat, ABI-extensible descriptor is used recursively for tuple items.
 * Only fields belonging to type are read. Buffer and item inputs are borrowed
 * for create(); the resulting profile owns deep copies. */
typedef struct tinypy_build_value_t {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t type;
    uint32_t reserved;

    int64_t integer_value;
    double float_value;

    const void *data;
    size_t data_size;

    const struct tinypy_build_value_t *items;
    size_t item_count;

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
    size_t max_value_nodes;
    size_t max_depth;
    size_t max_string_bytes;
    size_t max_tuple_items;
} tinypy_build_profile_limits_t;

typedef enum tinypy_build_profile_result_e {
    TINYPY_BUILD_PROFILE_OK = 0,
    TINYPY_BUILD_PROFILE_INVALID_ARGUMENT = 1,
    TINYPY_BUILD_PROFILE_ABI_MISMATCH = 2,
    TINYPY_BUILD_PROFILE_INVALID_OPTIMIZE = 3,
    TINYPY_BUILD_PROFILE_SIZE_OVERFLOW = 4,
    TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED = 5,
    TINYPY_BUILD_PROFILE_INVALID_NAME = 6,
    TINYPY_BUILD_PROFILE_DUPLICATE_NAME = 7,
    TINYPY_BUILD_PROFILE_NDEBUG_OVERRIDE = 8,
    TINYPY_BUILD_PROFILE_INVALID_VALUE_TYPE = 9,
    TINYPY_BUILD_PROFILE_INVALID_VALUE = 10,
    TINYPY_BUILD_PROFILE_INVALID_LONG = 11,
    TINYPY_BUILD_PROFILE_INVALID_UTF8 = 12
} tinypy_build_profile_result_e;

typedef struct tinypy_build_profile_error_t {
    uint32_t abi_version;
    uint32_t struct_size;
    tinypy_build_profile_result_e code;
    uint32_t reserved;
    size_t constant_index;
    size_t value_depth;
    const char *message;
    size_t message_size;
} tinypy_build_profile_error_t;

typedef enum tinypy_build_profile_allocation_tag_e {
    TINYPY_BUILD_PROFILE_ALLOC_TAG_PROFILE = 0x300,
    TINYPY_BUILD_PROFILE_ALLOC_TAG_DATA = 0x301
} tinypy_build_profile_allocation_tag_e;

void tinypy_build_value_init(tinypy_build_value_t *value, tinypy_build_value_type_e type);
void tinypy_build_profile_limits_init(tinypy_build_profile_limits_t *limits);

/* Exact bare-name lexical rule: ^__[A-Z][A-Z0-9_]*__$ */
int tinypy_preprocessor_name_is_reserved(const char *name, size_t name_size);

/* __NDEBUG__ is synthesized from optimize_level and must not be supplied by
 * the caller. Input order never affects the resulting sorted profile/digest.
 * limits and out_error are optional. */
tinypy_build_profile_result_e tinypy_build_profile_create(const tinypy_allocator_t *allocator, int optimize_level, const tinypy_build_constant_t *constants, size_t constant_count, const tinypy_build_profile_limits_t *limits, tinypy_build_profile_t **out_profile, tinypy_build_profile_error_t *out_error);

void tinypy_build_profile_destroy(tinypy_build_profile_t *profile);

int tinypy_build_profile_optimize_level(const tinypy_build_profile_t *profile);
size_t tinypy_build_profile_constant_count(const tinypy_build_profile_t *profile);

/* Indexed values are sorted by bytewise ASCII name. Returned pointers/views
 * are borrowed until profile destruction. */
void tinypy_build_profile_constant_at(const tinypy_build_profile_t *profile, size_t index, const char **out_name, size_t *out_name_size, const tinypy_build_value_t **out_value);

int tinypy_build_profile_find(const tinypy_build_profile_t *profile, const char *name, size_t name_size, const tinypy_build_value_t **out_value);

const uint8_t *tinypy_build_profile_digest(const tinypy_build_profile_t *profile);
const char *tinypy_build_profile_result_name(tinypy_build_profile_result_e result);

#endif
