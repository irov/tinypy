#ifndef TINYPY_COMPILER_SYMBOL_TABLE_H
#define TINYPY_COMPILER_SYMBOL_TABLE_H

typedef enum tinypy_symbol_block_e { TINYPY_SYMBOL_BLOCK_FUNCTION, TINYPY_SYMBOL_BLOCK_CLASS, TINYPY_SYMBOL_BLOCK_MODULE } tinypy_symbol_block_e;

typedef struct tinypy_symbol_table_t tinypy_symbol_table_t;

typedef struct tinypy_symbol_entry_t {
    tinypy_value_t *id;
    tinypy_value_t *handle;
    tinypy_value_t *symbols;
    tinypy_value_t *name;
    tinypy_value_t *variable_names;
    tinypy_value_t *children;
    tinypy_symbol_block_e block_type;
    int unoptimized;
    int nested;
    unsigned has_free_variables : 1;
    unsigned child_has_free_variables : 1;
    unsigned generator : 1;
    unsigned variable_arguments : 1;
    unsigned variable_keywords : 1;
    unsigned returns_value : 1;
    int line_number;
    int optimization_line_number;
    int temporary_name_index;
    tinypy_symbol_table_t *table;
} tinypy_symbol_entry_t;

struct tinypy_symbol_table_t {
    tinypy_compile_ctx_t *arena;
    const char *filename;
    tinypy_symbol_entry_t *current;
    tinypy_symbol_entry_t *top;
    tinypy_value_t *symbols;
    tinypy_value_t *stack;
    tinypy_value_t *global_symbols;
    int block_count;
    tinypy_value_t *private_name;
    tinypy_future_features_t *future;
    tinypy_value_t *top_name;
    tinypy_value_t *lambda_name;
    tinypy_value_t *generator_expression_name;
    tinypy_value_t *set_comprehension_name;
    tinypy_value_t *dict_comprehension_name;
};

#define TINYPY_SYMBOL_DEFINITION_GLOBAL 1
#define TINYPY_SYMBOL_DEFINITION_LOCAL 2
#define TINYPY_SYMBOL_DEFINITION_PARAMETER (2 << 1)
#define TINYPY_SYMBOL_USE (2 << 2)
#define TINYPY_SYMBOL_DEFINITION_FREE (2 << 3)
#define TINYPY_SYMBOL_DEFINITION_FREE_CLASS (2 << 4)
#define TINYPY_SYMBOL_DEFINITION_IMPORT (2 << 5)
#define TINYPY_SYMBOL_DEFINITION_BOUND (TINYPY_SYMBOL_DEFINITION_LOCAL | TINYPY_SYMBOL_DEFINITION_PARAMETER | TINYPY_SYMBOL_DEFINITION_IMPORT)
#define TINYPY_SYMBOL_SCOPE_OFFSET 11
#define TINYPY_SYMBOL_SCOPE_MASK 7
#define TINYPY_SYMBOL_SCOPE_LOCAL 1
#define TINYPY_SYMBOL_SCOPE_GLOBAL_EXPLICIT 2
#define TINYPY_SYMBOL_SCOPE_GLOBAL_IMPLICIT 3
#define TINYPY_SYMBOL_SCOPE_FREE 4
#define TINYPY_SYMBOL_SCOPE_CELL 5
#define TINYPY_SYMBOL_OPTIMIZATION_IMPORT_STAR 1
#define TINYPY_SYMBOL_OPTIMIZATION_EXEC 2
#define TINYPY_SYMBOL_OPTIMIZATION_BARE_EXEC 4
#define TINYPY_SYMBOL_OPTIMIZATION_TOP_LEVEL 8
#define TINYPY_SYMBOL_GENERATOR 1
#define TINYPY_SYMBOL_GENERATOR_EXPRESSION 2

int __tinypy_symbol_table_scope(tinypy_symbol_entry_t *entry, tinypy_value_t *name);
tinypy_symbol_table_t *__tinypy_symbol_table_build(tinypy_compile_ctx_t *arena, tinypy_ast_module_t module, const char *filename, tinypy_future_features_t *future);
tinypy_symbol_entry_t *__tinypy_symbol_table_lookup(tinypy_symbol_table_t *table, void *key);
void __tinypy_symbol_table_free(tinypy_symbol_table_t *table);

#endif
