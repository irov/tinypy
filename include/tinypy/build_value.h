#ifndef TINYPY_BUILD_VALUE_H
#define TINYPY_BUILD_VALUE_H

#include "tinypy/types.h"

#include <stdarg.h>
//////////////////////////////////////////////////////////////////////////
/* Converters return one owned reference, or NULL with an exception set in
 * the VM. Any values they use must belong to the surrounding operation's VM. */
typedef tinypy_value_t *(*tinypy_build_value_converter_t)(void *argument);

/* Builds values from the supported Python 2.7 Py_BuildValue units below.
 * An empty or NULL format returns None, one item returns that item, and
 * multiple items return a tuple. Parentheses, brackets and braces build
 * tuples, lists and dicts. Spaces, tabs, commas and colons separate items
 * without consuming arguments.
 *
 * Supported units and their C argument types:
 *   b B h i: int; H I: unsigned int; n: ptrdiff_t; l: long;
 *   k: unsigned long; L: long long; K: unsigned long long;
 *   f d: double; D: pointer to two adjacent doubles (real, imaginary);
 *   c: int; s z: const char *; u: const wchar_t *;
 *   O S N: tinypy_value_t *; O& S& N&: converter, void *argument.
 * L and K always produce long values. s, z and u accept NULL as None.
 * Only s, z and u support a # suffix; it consumes an int length, matching
 * the legacy Py_BuildValue ABI: bytes for s/z, wchar_t elements for u.
 * A negative or omitted length uses the terminating NUL.
 * Unicode input must contain valid scalars, with surrogate pairs on a host
 * whose wchar_t is 16 bits. D's pointer must address two readable doubles.
 *
 * O and S retain their argument; N transfers an owned reference. With a
 * well-formed format, conversion failure still consumes later arguments and
 * releases transferred N references and owned converter results. The first
 * exception is saved during cleanup and restored before returning NULL.
 * Malformed formats do not guarantee consumption of remaining arguments.
 * Argument types, lifetimes and VM ownership are C API preconditions.
 *
 * Returns one owned reference on success, or NULL with a pending VM exception
 * on failure. Format errors raise SystemError unless an earlier exception
 * takes precedence. Optional out_error receives an owned diagnostic on
 * failure or NULL on success. The VM exception is left pending, not printed.
 * The va_list variant copies args and does not advance the caller's list. */
tinypy_value_t *tinypy_build_value(tinypy_vm_t *vm, tinypy_error_t **out_error, const char *format, ...);
tinypy_value_t *tinypy_build_value_va(tinypy_vm_t *vm, const char *format, va_list args, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
#endif
