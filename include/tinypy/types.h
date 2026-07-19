#ifndef TINYPY_TYPES_H
#define TINYPY_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define TINYPY_ABI_VERSION UINT32_C(1)

/* Hashes deliberately emulate the 64-bit CPython 2.7 `long` policy on every
 * host, including 32-bit hosts. This gives bytecode/cache users one stable
 * result width instead of inheriting the embedding process word size. */
#define TINYPY_HASH_BITS 64
typedef int64_t tinypy_hash_t;

typedef struct tinypy_vm_t tinypy_vm_t;
typedef struct tinypy_value_t tinypy_value_t;
typedef struct tinypy_type_t tinypy_type_t;
typedef struct tinypy_error_t tinypy_error_t;
typedef struct tinypy_module_request_t tinypy_module_request_t;
typedef struct tinypy_module_artifact_t tinypy_module_artifact_t;

typedef enum tinypy_error_kind_e {
    TINYPY_ERROR_TYPE = 1,
    TINYPY_ERROR_RUNTIME = 2,
    TINYPY_ERROR_NAME = 3,
    TINYPY_ERROR_UNBOUND_LOCAL = 4,
    TINYPY_ERROR_INTERRUPT = 5,
    TINYPY_ERROR_ZERO_DIVISION = 6,
    TINYPY_ERROR_VALUE = 7,
    TINYPY_ERROR_INDEX = 8,
    TINYPY_ERROR_KEY = 9,
    TINYPY_ERROR_OVERFLOW = 10,
    TINYPY_ERROR_IMPORT = 11,
    TINYPY_ERROR_ATTRIBUTE = 12,
    TINYPY_ERROR_LOOKUP = 13,
    TINYPY_ERROR_SYNTAX = 14,
    TINYPY_ERROR_INDENTATION = 15,
    TINYPY_ERROR_TAB = 16,
    TINYPY_ERROR_SOURCE_DECODING = 17,
    TINYPY_ERROR_COMPILER_LIMIT = 18
} tinypy_error_kind_e;

/* Unless a parameter is explicitly documented as optional, pointer validity
 * and object ownership are C API preconditions. Direct typed/indexed accessors
 * additionally require the documented kind and bounds. These contracts are
 * asserted in debug builds and are undefined behavior when NDEBUG is defined.
 * Direct typed accessors never convert a wrong kind, index or scalar range
 * into a status. Pointer-returning operations use NULL for semantic failure
 * and may provide an explicit tinypy_error_t. A host allocator must return
 * non-NULL for every valid non-zero allocation request; exhaustion is a
 * contract violation, not a recoverable status. */

typedef enum tinypy_value_type_e {
    TINYPY_VALUE_INVALID = 0,
    TINYPY_VALUE_NONE = 1,
    TINYPY_VALUE_BOOL = 2,
    TINYPY_VALUE_INTEGER = 3,
    TINYPY_VALUE_STRING = 4,
    TINYPY_VALUE_UNICODE = 5,
    TINYPY_VALUE_LONG = 6,
    TINYPY_VALUE_FLOAT = 7,
    TINYPY_VALUE_COMPLEX = 8,
    TINYPY_VALUE_TUPLE = 9,
    TINYPY_VALUE_LIST = 10,
    TINYPY_VALUE_DICT = 11,
    TINYPY_VALUE_TYPE = 12,
    TINYPY_VALUE_INSTANCE = 13,
    TINYPY_VALUE_CODE = 14,
    TINYPY_VALUE_FRAME = 15,
    TINYPY_VALUE_FUNCTION = 16,
    TINYPY_VALUE_ITERATOR = 17,
    TINYPY_VALUE_METHOD = 18,
    TINYPY_VALUE_CELL = 19,
    TINYPY_VALUE_SLICE = 20,
    TINYPY_VALUE_MODULE = 21,
    TINYPY_VALUE_NATIVE_FUNCTION = 22,
    TINYPY_VALUE_STATIC_METHOD = 23,
    TINYPY_VALUE_CLASS_METHOD = 24,
    TINYPY_VALUE_PROPERTY = 25,
    TINYPY_VALUE_SUPER = 26,
    TINYPY_VALUE_TRACEBACK = 27,
    TINYPY_VALUE_GENERATOR = 28,
    TINYPY_VALUE_SET = 29,
    TINYPY_VALUE_FROZENSET = 30,
    TINYPY_VALUE_OUTPUT_STREAM = 31,
    TINYPY_VALUE_NOT_IMPLEMENTED = 32,
    TINYPY_VALUE_XRANGE = 33,
    TINYPY_VALUE_ENUMERATE = 34,
    TINYPY_VALUE_REVERSED = 35,
    TINYPY_VALUE_BUFFER = 36,
    TINYPY_VALUE_ELLIPSIS = 37,
    TINYPY_VALUE_FILE = 38,
    TINYPY_VALUE_GETSET_DESCRIPTOR = 39,
    TINYPY_VALUE_MEMBER_DESCRIPTOR = 40,
    TINYPY_VALUE_CLASS = 41,
    TINYPY_VALUE_OLD_INSTANCE = 42,
    TINYPY_VALUE_BYTEARRAY = 43,
    TINYPY_VALUE_WEAKREF = 44,
    TINYPY_VALUE_DICT_KEYS = 45,
    TINYPY_VALUE_DICT_VALUES = 46,
    TINYPY_VALUE_DICT_ITEMS = 47,
    TINYPY_VALUE_PARTIAL = 48,
    TINYPY_VALUE_SRE_PATTERN = 49,
    TINYPY_VALUE_SRE_MATCH = 50
} tinypy_value_type_e;

typedef enum tinypy_allocation_tag_e {
    TINYPY_ALLOC_TAG_VM = 1,
    TINYPY_ALLOC_TAG_VALUE = 2,
    TINYPY_ALLOC_TAG_ERROR = 3,
    TINYPY_ALLOC_TAG_LIST_ITEMS = 4,
    TINYPY_ALLOC_TAG_DICT_TABLE = 5,
    TINYPY_ALLOC_TAG_TYPE_MRO = 6,
    TINYPY_ALLOC_TAG_MARSHAL_CACHE = 7,
    TINYPY_ALLOC_TAG_TEMPORARY = 8,
    TINYPY_ALLOC_TAG_BYTEARRAY_DATA = 9,
    TINYPY_ALLOC_TAG_TUPLE_ITEMS = 10,
    TINYPY_ALLOC_TAG_SRE_DATA = 11,
    TINYPY_ALLOC_TAG_COMPILER_ARENA = 12,
    TINYPY_ALLOC_TAG_COMPILER_DATA = 13,
    TINYPY_ALLOC_TAG_MARSHAL_WRITE = 14
} tinypy_allocation_tag_e;

typedef enum tinypy_output_channel_e {
    TINYPY_OUTPUT_STDOUT = 1,
    TINYPY_OUTPUT_STDERR = 2,
    TINYPY_OUTPUT_UNRAISABLE = 3
} tinypy_output_channel_e;

#endif
