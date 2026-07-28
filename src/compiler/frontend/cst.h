#ifndef TINYPY_COMPILER_CST_H
#define TINYPY_COMPILER_CST_H

typedef struct tinypy_cst_node_t {
    tinypy_compile_ctx_t *context;
    short type;
    char *text;
    int32_t line_number;
    int32_t column_offset;
    int32_t child_count;
    struct tinypy_cst_node_t *children;
} tinypy_cst_node_t;

tinypy_cst_node_t *tinypy_internal_compiler_cst_new(tinypy_compile_ctx_t *ctx, int32_t type);
int32_t tinypy_internal_compiler_cst_add_child(tinypy_cst_node_t *node, int32_t type, char *text, int32_t line_number, int32_t column_offset);
void tinypy_internal_compiler_cst_release(tinypy_cst_node_t *node);
tinypy_compiler_size_t tinypy_internal_compiler_cst_size(tinypy_cst_node_t *node);

#define TINYPY_CST_CHILD_COUNT(node) ((node)->child_count)
#define TINYPY_CST_CHILD(node, index) (&(node)->children[(index)])
#define TINYPY_CST_REVERSE_CHILD(node, index) TINYPY_CST_CHILD((node), TINYPY_CST_CHILD_COUNT(node) + (index))
#define TINYPY_CST_TYPE(node) ((node)->type)
#define TINYPY_CST_TEXT(node) ((node)->text)

#endif
