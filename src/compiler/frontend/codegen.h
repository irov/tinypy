#ifndef TINYPY_COMPILER_CODEGEN_H
#define TINYPY_COMPILER_CODEGEN_H

#include "bytecode_builder.h"

typedef struct tinypy_future_features_t {
    int features;
    int line_number;
} tinypy_future_features_t;

struct tinypy_symbol_table_t;

#define TINYPY_FUTURE_FEATURE_NESTED_SCOPES "nested_scopes"
#define TINYPY_FUTURE_FEATURE_GENERATORS "generators"
#define TINYPY_FUTURE_FEATURE_DIVISION "division"
#define TINYPY_FUTURE_FEATURE_ABSOLUTE_IMPORT "absolute_import"
#define TINYPY_FUTURE_FEATURE_WITH_STATEMENT "with_statement"
#define TINYPY_FUTURE_FEATURE_PRINT_FUNCTION "print_function"
#define TINYPY_FUTURE_FEATURE_UNICODE_LITERALS "unicode_literals"

tinypy_future_features_t *__tinypy_future_scan(tinypy_compile_ctx_t *arena, tinypy_ast_module_t module, const char *filename);
tinypy_code_object_t *__tinypy_ast_compile(tinypy_compile_ctx_t *arena, tinypy_ast_module_t module, const char *filename, tinypy_compiler_flags_t *flags, tinypy_future_features_t *future, struct tinypy_symbol_table_t *symbols);
tinypy_value_t *__tinypy_bytecode_optimize(tinypy_compile_ctx_t *arena, tinypy_value_t *code, tinypy_value_t *consts, tinypy_value_t *names, tinypy_value_t *lineno);

#endif
