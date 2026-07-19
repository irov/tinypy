#ifndef TINYPY_VALUE_H
#define TINYPY_VALUE_H

#include "tinypy/types.h"

/* All returned values belong to vm and carry one owned reference. None,
 * bool, int values in [-1023, 1024], positive float 0.0, empty byte str and
 * empty tuple are VM-local constants whose base reference is owned by vm.
 * Constructors retain a cached constant for the caller like any other
 * result. The accessors require a valid live vm. A contract violation,
 * including reference-count saturation, is asserted in debug builds and is
 * undefined behavior when NDEBUG is set. */
tinypy_value_t *tinypy_none_get(tinypy_vm_t *vm);
tinypy_value_t *tinypy_ellipsis_get(tinypy_vm_t *vm);
tinypy_value_t *tinypy_bool_from_i32(tinypy_vm_t *vm, int32_t value);
tinypy_value_t *tinypy_integer_from_i64(tinypy_vm_t *vm, int64_t value);

/* Python 2 str stores arbitrary bytes. bytes may be NULL only when size is
 * zero. A trailing NUL is stored for C interoperability but is not part of
 * the value and callers must always use the explicit size. */
tinypy_value_t *tinypy_string_from_bytes(tinypy_vm_t *vm, const void *bytes, size_t size);

/* The returned view is borrowed and remains valid until value is released or
 * its VM is destroyed. Embedded NUL bytes are preserved. */
const void *tinypy_string_view(const tinypy_value_t *value, size_t *out_size);

/* Creates a Python 2 unicode value from one canonical UTF-8 byte sequence.
 * Invalid UTF-8 is a caller contract violation: it asserts in debug builds
 * and is undefined behavior when NDEBUG is set. */
tinypy_value_t *tinypy_unicode_from_utf8(tinypy_vm_t *vm, const char *utf8, size_t size);

/* Returns the canonical UTF-8 representation and both byte and Unicode scalar
 * counts. The borrowed view has the same lifetime rules as tinypy_string_view. */
const char *tinypy_unicode_utf8_view(const tinypy_value_t *value, size_t *out_size, size_t *out_code_point_count);

tinypy_value_type_e tinypy_typeof(const tinypy_value_t *value);

/* Passing a value of the wrong documented kind is a C contract violation. */
int32_t tinypy_bool_as_i32(const tinypy_value_t *value);

/* Python bool is a subtype of int, so this accepts both TINYPY_VALUE_INTEGER and
 * TINYPY_VALUE_BOOL. */
int64_t tinypy_integer_as_i64(const tinypy_value_t *value);

/* Reference operations require a live value. Its VM is recovered through the
 * value's type. Contract violations assert in debug builds and are undefined
 * in release. */
void tinypy_retain(tinypy_value_t *value);
void tinypy_release(tinypy_value_t *value);

#endif
