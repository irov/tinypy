#ifndef TINYPY_COMPILER_AST_H
#define TINYPY_COMPILER_AST_H
tinypy_ast_module_t __tinypy_ast_build(const tinypy_cst_node_t *, tinypy_compiler_flags_t *flags, const char *, tinypy_compile_ctx_t *);
tinypy_ast_expression_t *tinypy_internal_ast_operator_chain(tinypy_compile_ctx_t *arena, tinypy_ast_expression_t expression, size_t *out_count);
#endif /* !TINYPY_COMPILER_AST_H */
