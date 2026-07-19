#ifndef TINYPY_CODE_H
#define TINYPY_CODE_H

#include "tinypy/types.h"

typedef enum tinypy_code_flag_e {
    TINYPY_CODE_OPTIMIZED = 0x0001,
    TINYPY_CODE_NEW_LOCALS = 0x0002,
    TINYPY_CODE_VARARGS = 0x0004,
    TINYPY_CODE_VAR_KEYWORDS = 0x0008,
    TINYPY_CODE_NESTED = 0x0010,
    TINYPY_CODE_GENERATOR = 0x0020,
    TINYPY_CODE_NO_FREE = 0x0040,
    TINYPY_CODE_FUTURE_DIVISION = 0x2000,
    TINYPY_CODE_FUTURE_ABSOLUTE_IMPORT = 0x4000,
    TINYPY_CODE_FUTURE_WITH_STATEMENT = 0x8000,
    TINYPY_CODE_FUTURE_PRINT_FUNCTION = 0x10000,
    TINYPY_CODE_FUTURE_UNICODE_LITERALS = 0x20000
} tinypy_code_flag_e;

/* Python 2.7 code object. Every object field is retained by the code object.
 * bytecode and lnotab are byte strings; consts, names, varnames, freevars and
 * cellvars are tuples; filename and name are byte strings. Returned field
 * values are borrowed. */
tinypy_value_t *tinypy_code_new(int32_t arg_count, int32_t local_count, int32_t stack_size, int32_t flags, tinypy_value_t *bytecode, tinypy_value_t *consts, tinypy_value_t *names, tinypy_value_t *varnames, tinypy_value_t *freevars, tinypy_value_t *cellvars, tinypy_value_t *filename, tinypy_value_t *name, int32_t first_line_number, tinypy_value_t *lnotab);
int32_t tinypy_code_arg_count(const tinypy_value_t *code);
int32_t tinypy_code_local_count(const tinypy_value_t *code);
int32_t tinypy_code_stack_size(const tinypy_value_t *code);
int32_t tinypy_code_flags(const tinypy_value_t *code);
tinypy_value_t *tinypy_code_bytecode(const tinypy_value_t *code);
tinypy_value_t *tinypy_code_consts(const tinypy_value_t *code);
tinypy_value_t *tinypy_code_names(const tinypy_value_t *code);
tinypy_value_t *tinypy_code_varnames(const tinypy_value_t *code);
tinypy_value_t *tinypy_code_freevars(const tinypy_value_t *code);
tinypy_value_t *tinypy_code_cellvars(const tinypy_value_t *code);
tinypy_value_t *tinypy_code_filename(const tinypy_value_t *code);
tinypy_value_t *tinypy_code_name(const tinypy_value_t *code);
int32_t tinypy_code_first_line_number(const tinypy_value_t *code);
tinypy_value_t *tinypy_code_lnotab(const tinypy_value_t *code);

#endif
