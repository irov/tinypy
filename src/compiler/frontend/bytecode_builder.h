#ifndef TINYPY_COMPILER_BYTECODE_BUILDER_H
#define TINYPY_COMPILER_BYTECODE_BUILDER_H

#define TINYPY_COMPILER_MAX_BLOCKS 20
#define TINYPY_PARSER_REQUIRES_FUTURE_KEYWORD 1
#define TINYPY_COMPILER_FUTURE_MASK (TINYPY_CODE_FUTURE_DIVISION | TINYPY_CODE_FUTURE_ABSOLUTE_IMPORT | TINYPY_CODE_FUTURE_WITH_STATEMENT | TINYPY_CODE_FUTURE_PRINT_FUNCTION | TINYPY_CODE_FUTURE_UNICODE_LITERALS)
#define TINYPY_COMPILER_COMPARE_INVALID 11
#define TINYPY_COMPILER_OPCODE_HAS_ARGUMENT(opcode) ((opcode) >= (int)TINYPY_OPCODE_HAVE_ARGUMENT)

#define __tinypy_bytecode_free_variable_count(code) ((int)tinypy_tuple_size((code)->freevars))

tinypy_code_object_t *__tinypy_frontend_code_new(int arg_count, int local_count, int stack_size, int flags, tinypy_value_t *bytecode, tinypy_value_t *consts, tinypy_value_t *names, tinypy_value_t *varnames, tinypy_value_t *freevars, tinypy_value_t *cellvars, tinypy_value_t *filename, tinypy_value_t *name, int first_line_number, tinypy_value_t *lnotab);
tinypy_value_t *__tinypy_bytecode_constant_key(tinypy_value_t *object);

#define __tinypy_bytecode_new __tinypy_frontend_code_new

#endif
