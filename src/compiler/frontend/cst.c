/* Parse tree tinypy_cst_node_t implementation */

#include "value_ops.h"
#include "cst.h"
#include "parser_error.h"

//////////////////////////////////////////////////////////////////////////
tinypy_cst_node_t *tinypy_internal_compiler_cst_new(tinypy_compile_ctx_t *ctx, int32_t type) {
    if (ctx->limits.max_cst_nodes != 0U && ctx->cst_node_count >= ctx->limits.max_cst_nodes) {
        return NULL;
    }
    tinypy_cst_node_t *n = (tinypy_cst_node_t *)tinypy_internal_compiler_arena_allocate(ctx, sizeof(tinypy_cst_node_t));
    if (n == NULL) {
        return NULL;
    }
    ctx->cst_node_count += 1U;
    n->context = ctx;
    n->type = type;
    n->text = NULL;
    n->line_number = 0;
    n->child_count = 0;
    n->child_capacity = 0;
    n->children = NULL;
    return n;
}

/* See comments at XXXROUNDUP below.  Returns -1 on overflow. */
//////////////////////////////////////////////////////////////////////////
static int32_t __fancy_roundup(int32_t n) {
    /* Round up to the closest power of 2 >= n. */
    uint32_t result = UINT32_C(256);
    uint32_t required = (uint32_t)n;

    while (result < required) {
        if (result > (uint32_t)INT32_MAX / UINT32_C(2)) {
            return -1;
        }
        result <<= 1U;
    }
    return (int32_t)result;
}

/* A gimmick to make massive numbers of reallocs quicker.  The result is
 * a number >= the input. In tinypy_internal_compiler_cst_add_child it is used when
 * we're about to add child number current_size + 1:
 *
 *     if TINYPY_CST_ROUND_UP(current_size) < TINYPY_CST_ROUND_UP(current_size + 1):
 *         allocate space for TINYPY_CST_ROUND_UP(current_size + 1) total children
 *     else:
 *         we already have enough space
 *
 * Since a tinypy_cst_node_t starts out empty, we must have
 *
 *     TINYPY_CST_ROUND_UP(0) < TINYPY_CST_ROUND_UP(1)
 *
 * so that we allocate space for the first child.  One-child nodes are very
 * common (presumably that would change if we used a more abstract form
 * of syntax tree), so to avoid wasting memory it's desirable that
 * TINYPY_CST_ROUND_UP(1) == 1.  That in turn forces TINYPY_CST_ROUND_UP(0) == 0.
 *
 * Else for 2 <= n <= 128, we round up to the closest multiple of 4.  Why 4?
 * Rounding up to a multiple of an exact power of 2 is very efficient, and
 * most nodes with more than one child have <= 4 kids.
 *
 * Larger arrays grow proportionally. This keeps the number of arena copies
 * bounded while preserving the compact representation of common small nodes.
 *
 * tinypy_cst_node_t stores the rounded capacity so repeated additions do not
 * recompute it or copy until the current allocation is full.
 */
#define TINYPY_CST_ROUND_UP(n) ((n) <= 1 ? (n) : (n) <= 128 ? (((n) + 3) & ~3) \
                                                            : __fancy_roundup(n))

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_compiler_cst_add_child(register tinypy_cst_node_t *n1, int32_t type, char *str, int32_t lineno, int32_t col_offset) {
    const int32_t nch = n1->child_count;
    int32_t required_capacity;

    if (n1->context->limits.max_cst_nodes != 0U && n1->context->cst_node_count >= n1->context->limits.max_cst_nodes) {
        return TINYPY_PARSER_OUT_OF_MEMORY;
    }
    if (nch == INT_MAX || nch < 0) {
        return TINYPY_PARSER_OVERFLOW;
    }

    required_capacity = TINYPY_CST_ROUND_UP(nch + 1);
    if (required_capacity < 0) {
        return TINYPY_PARSER_OVERFLOW;
    }
    if (n1->child_capacity < required_capacity) {
        if ((size_t)required_capacity > SIZE_MAX / sizeof(tinypy_cst_node_t)) {
            return TINYPY_PARSER_OUT_OF_MEMORY;
        }
        tinypy_cst_node_t *new_children = (tinypy_cst_node_t *)tinypy_internal_compiler_arena_allocate_uninitialized(n1->context,
                                                                                                                     (size_t)required_capacity * sizeof(tinypy_cst_node_t));
        if (new_children == NULL) {
            return TINYPY_PARSER_OUT_OF_MEMORY;
        }
        if (n1->children != NULL) {
            memcpy(new_children, n1->children, (size_t)nch * sizeof(tinypy_cst_node_t));
        }
        n1->children = new_children;
        n1->child_capacity = required_capacity;
    }

    tinypy_cst_node_t *n = &n1->children[n1->child_count++];
    n1->context->cst_node_count += 1U;
    n->context = n1->context;
    n->type = type;
    n->text = str;
    n->line_number = lineno;
    n->column_offset = col_offset;
    n->child_count = 0;
    n->child_capacity = 0;
    n->children = NULL;
    return 0;
}

/* Forward */
static tinypy_compiler_size_t __tinypy_frontend_size_of_children(tinypy_cst_node_t *n);

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_compiler_cst_release(tinypy_cst_node_t *n) {
    (void)n;
}
//////////////////////////////////////////////////////////////////////////
tinypy_compiler_size_t tinypy_internal_compiler_cst_size(tinypy_cst_node_t *n) {
    tinypy_compiler_size_t res = 0;

    if (n != NULL) {
        res = sizeof(tinypy_cst_node_t) + __tinypy_frontend_size_of_children(n);
    }
    return res;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_compiler_size_t __tinypy_frontend_size_of_children(tinypy_cst_node_t *n) {
    tinypy_compiler_size_t res = 0;
    int32_t i;
    for (i = TINYPY_CST_CHILD_COUNT(n); --i >= 0;) {
        res += __tinypy_frontend_size_of_children(TINYPY_CST_CHILD(n, i));
    }
    if (n->children != NULL) {
        /* allocated size of n->children array */
        res += TINYPY_CST_ROUND_UP(TINYPY_CST_CHILD_COUNT(n)) * sizeof(tinypy_cst_node_t);
    }
    if (TINYPY_CST_TEXT(n) != NULL) {
        res += strlen(TINYPY_CST_TEXT(n)) + 1;
    }
    return res;
}
