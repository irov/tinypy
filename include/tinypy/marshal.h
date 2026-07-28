#ifndef TINYPY_MARSHAL_H
#define TINYPY_MARSHAL_H

#include "tinypy/vm.h"

#define TINYPY_MARSHAL_ABI_VERSION UINT32_C(1)

/* Required pointers and typed/indexed accessors follow the tinypy C API
 * precondition policy from tinypy.h. Malformed wire data, ABI mismatches,
 * and configured resource limits remain recoverable result codes. */

typedef struct tinypy_marshal_document_t tinypy_marshal_document_t;
typedef struct tinypy_marshal_object_t tinypy_marshal_object_t;

typedef enum tinypy_marshal_result_e {
    TINYPY_MARSHAL_OK = 0,
    TINYPY_MARSHAL_INVALID_ARGUMENT = 1,
    TINYPY_MARSHAL_ABI_MISMATCH = 2,
    TINYPY_MARSHAL_TRUNCATED = 3,
    TINYPY_MARSHAL_UNKNOWN_TYPE = 4,
    TINYPY_MARSHAL_NULL_OUTSIDE_DICT = 5,
    TINYPY_MARSHAL_INVALID_SIZE = 6,
    TINYPY_MARSHAL_INVALID_LONG = 7,
    TINYPY_MARSHAL_INVALID_STRING_REF = 8,
    TINYPY_MARSHAL_INVALID_UTF8 = 9,
    TINYPY_MARSHAL_INVALID_FLOAT = 10,
    TINYPY_MARSHAL_INVALID_CODE = 11,
    TINYPY_MARSHAL_DEPTH_LIMIT = 12,
    TINYPY_MARSHAL_OBJECT_LIMIT = 13,
    TINYPY_MARSHAL_STRING_LIMIT = 14,
    TINYPY_MARSHAL_CONTAINER_LIMIT = 15,
    TINYPY_MARSHAL_BYTE_LIMIT = 16,
    TINYPY_MARSHAL_TRAILING_DATA = 17,
    TINYPY_MARSHAL_OUTPUT_LIMIT = 18,
    TINYPY_MARSHAL_BUFFER_TOO_SMALL = 19,
    TINYPY_MARSHAL_INVALID_GRAPH = 20,
    TINYPY_MARSHAL_UNSUPPORTED_RUNTIME_TYPE = 21,
    TINYPY_MARSHAL_ROOT_NOT_CODE = 22
} tinypy_marshal_result_e;

typedef enum tinypy_marshal_type_e {
    TINYPY_MARSHAL_TYPE_NONE = 1,
    TINYPY_MARSHAL_TYPE_BOOL = 2,
    TINYPY_MARSHAL_TYPE_STOP_ITERATION = 3,
    TINYPY_MARSHAL_TYPE_ELLIPSIS = 4,
    TINYPY_MARSHAL_TYPE_INTEGER = 5,
    TINYPY_MARSHAL_TYPE_LONG = 6,
    TINYPY_MARSHAL_TYPE_FLOAT = 7,
    TINYPY_MARSHAL_TYPE_COMPLEX = 8,
    TINYPY_MARSHAL_TYPE_BYTES = 9,
    TINYPY_MARSHAL_TYPE_UNICODE = 10,
    TINYPY_MARSHAL_TYPE_TUPLE = 11,
    TINYPY_MARSHAL_TYPE_LIST = 12,
    TINYPY_MARSHAL_TYPE_DICT = 13,
    TINYPY_MARSHAL_TYPE_SET = 14,
    TINYPY_MARSHAL_TYPE_FROZENSET = 15,
    TINYPY_MARSHAL_TYPE_CODE = 16
} tinypy_marshal_type_e;

typedef struct tinypy_marshal_limits_t {
    uint32_t abi_version;
    uint32_t struct_size;
    size_t max_input_bytes;
    size_t max_allocated_bytes;
    size_t max_depth;
    size_t max_objects;
    size_t max_string_bytes;
    size_t max_container_items;
} tinypy_marshal_limits_t;

typedef struct tinypy_marshal_error_t {
    uint32_t abi_version;
    uint32_t struct_size;
    tinypy_marshal_result_e code;
    size_t offset;
    uint8_t wire_type;
    uint8_t reserved[7];
    const tinypy_marshal_object_t *object;
    size_t object_index;
    const char *message;
    size_t message_size;
} tinypy_marshal_error_t;

typedef struct tinypy_marshal_write_options_t {
    uint32_t abi_version;
    uint32_t struct_size;
    size_t max_output_bytes;
    size_t max_depth;
} tinypy_marshal_write_options_t;

typedef struct tinypy_marshal_code_t {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t argcount;
    int32_t nlocals;
    int32_t stacksize;
    int32_t flags;
    const tinypy_marshal_object_t *bytecode;
    const tinypy_marshal_object_t *consts;
    const tinypy_marshal_object_t *names;
    const tinypy_marshal_object_t *varnames;
    const tinypy_marshal_object_t *freevars;
    const tinypy_marshal_object_t *cellvars;
    const tinypy_marshal_object_t *filename;
    const tinypy_marshal_object_t *name;
    int32_t firstlineno;
    const tinypy_marshal_object_t *lnotab;
} tinypy_marshal_code_t;

/* Fill limits with conservative defaults. A limits object passed to the
 * reader must retain these ABI fields even when individual limits change. */
void tinypy_marshal_limits_init(tinypy_marshal_limits_t *limits);
void tinypy_marshal_write_options_init(tinypy_marshal_write_options_t *options);

/* Read exactly one raw CPython 2.7 marshal-v2 object. There is no .pyc header.
 * The input is borrowed only for this call; strings and graph data are copied
 * with allocator. allocator and limits are copied into the document. limits
 * and out_error are optional. */
tinypy_marshal_result_e tinypy_marshal_read_v2(const void *bytes, size_t size, const tinypy_allocator_t *allocator, const tinypy_marshal_limits_t *limits, tinypy_marshal_document_t **out_document, tinypy_marshal_error_t *out_error);

/* Read one raw CPython 2.7 marshal-v2 code object and materialize the complete
 * code/constant graph as VM-owned values. Temporary parser allocations use
 * the VM allocator and are released before this function returns. out_code
 * receives one owned reference on success. */
tinypy_marshal_result_e tinypy_marshal_load_code_v2(tinypy_vm_t *vm, const void *bytes, size_t size, const tinypy_marshal_limits_t *limits, tinypy_value_t **out_code, tinypy_marshal_error_t *out_error);

/* Serialize exactly one immutable graph in CPython 2.7 marshal-v2 encoding.
 * A NULL buffer with zero capacity performs a size query. For a non-NULL
 * buffer, a short capacity returns TINYPY_MARSHAL_BUFFER_TOO_SMALL without
 * changing any byte in the caller buffer. out_size always receives the exact
 * required size after a successful sizing pass. The writer allocates nothing.
 * options and out_error are optional. */
tinypy_marshal_result_e tinypy_marshal_write_v2(const tinypy_marshal_document_t *document, void *buffer, size_t capacity, size_t *out_size, const tinypy_marshal_write_options_t *options, tinypy_marshal_error_t *out_error);
/* Direct code dumping uses the owning VM allocator for a transient intern
 * table and releases it before returning. The output buffer remains owned by
 * the caller. */
tinypy_marshal_result_e tinypy_marshal_dump_code_v2(const tinypy_value_t *code, void *buffer, size_t capacity, size_t *out_size, const tinypy_marshal_write_options_t *options, tinypy_marshal_error_t *out_error);

void tinypy_marshal_document_destroy(tinypy_marshal_document_t *document);

const tinypy_marshal_object_t *tinypy_marshal_document_root(const tinypy_marshal_document_t *document);
size_t tinypy_marshal_document_input_size(const tinypy_marshal_document_t *document);
size_t tinypy_marshal_document_object_count(const tinypy_marshal_document_t *document);
size_t tinypy_marshal_document_allocated_bytes(const tinypy_marshal_document_t *document);

tinypy_marshal_type_e tinypy_marshal_object_type(const tinypy_marshal_object_t *object);
uint8_t tinypy_marshal_object_wire_type(const tinypy_marshal_object_t *object);
tinypy_bool_t tinypy_marshal_bool_value(const tinypy_marshal_object_t *object);
int64_t tinypy_marshal_integer_value(const tinypy_marshal_object_t *object);
void tinypy_marshal_long_view(const tinypy_marshal_object_t *object, int32_t *out_sign, const uint16_t **out_digits, size_t *out_digit_count);
double tinypy_marshal_float_value(const tinypy_marshal_object_t *object);
void tinypy_marshal_complex_value(const tinypy_marshal_object_t *object, double *out_real, double *out_imaginary);
void tinypy_marshal_bytes_view(const tinypy_marshal_object_t *object, const void **out_bytes, size_t *out_size, tinypy_bool_t *out_interned);
void tinypy_marshal_unicode_view(const tinypy_marshal_object_t *object, const char **out_utf8, size_t *out_size, size_t *out_code_point_count);
size_t tinypy_marshal_sequence_size(const tinypy_marshal_object_t *object);
const tinypy_marshal_object_t *tinypy_marshal_sequence_item(const tinypy_marshal_object_t *object, size_t index);
size_t tinypy_marshal_dict_size(const tinypy_marshal_object_t *object);
const tinypy_marshal_object_t *tinypy_marshal_dict_key(const tinypy_marshal_object_t *object, size_t index);
const tinypy_marshal_object_t *tinypy_marshal_dict_value(const tinypy_marshal_object_t *object, size_t index);
const tinypy_marshal_code_t *tinypy_marshal_code_view(const tinypy_marshal_object_t *object);

const char *tinypy_marshal_result_name(tinypy_marshal_result_e result);

#endif
