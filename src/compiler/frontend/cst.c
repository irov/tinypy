/* Parse tree tinypy_cst_node_t implementation */

#include "value_ops.h"
#include "cst.h"
#include "parser_error.h"

//////////////////////////////////////////////////////////////////////////
tinypy_cst_node_t *tinypy_internal_compiler_cst_new(tinypy_compile_ctx_t *ctx, int type) {
    tinypy_cst_node_t *n = (tinypy_cst_node_t *)tinypy_internal_compiler_arena_allocate(ctx, sizeof(tinypy_cst_node_t));
    if (n == NULL) {
        return NULL;
    }
    if (ctx->limits.max_cst_nodes != 0U && ctx->cst_node_count >= ctx->limits.max_cst_nodes) {
        return NULL;
    }
    ctx->cst_node_count += 1U;
    n->context = ctx;
    n->type = type;
    n->text = NULL;
    n->line_number = 0;
    n->child_count = 0;
    n->children = NULL;
    return n;
}

/* See comments at XXXROUNDUP below.  Returns -1 on overflow. */
//////////////////////////////////////////////////////////////////////////
static int __fancy_roundup(int n) {
    /* Round up to the closest power of 2 >= n. */
    int result = 256;
    assert(n > 128);
    while (result < n) {
        result <<= 1;
        if (result <= 0) {
            return -1;
        }
    }
    return result;
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
 * Note that this would be straightforward if a tinypy_cst_node_t stored its current
 * capacity.  The code is tricky to avoid that.
 */
#define TINYPY_CST_ROUND_UP(n) ((n) <= 1 ? (n) : (n) <= 128 ? (((n) + 3) & ~3) \
                                                            : __fancy_roundup(n))

//////////////////////////////////////////////////////////////////////////
int tinypy_internal_compiler_cst_add_child(register tinypy_cst_node_t *n1, int type, char *str, int lineno, int col_offset) {
    const int nch = n1->child_count;
    int current_capacity;
    int required_capacity;
    tinypy_cst_node_t *n;

    if (n1->context->limits.max_cst_nodes != 0U && n1->context->cst_node_count >= n1->context->limits.max_cst_nodes) {
        return TINYPY_PARSER_OUT_OF_MEMORY;
    }
    if (nch == INT_MAX || nch < 0) {
        return TINYPY_PARSER_OVERFLOW;
    }

    current_capacity = TINYPY_CST_ROUND_UP(nch);
    required_capacity = TINYPY_CST_ROUND_UP(nch + 1);
    if (current_capacity < 0 || required_capacity < 0) {
        return TINYPY_PARSER_OVERFLOW;
    }
    if (current_capacity < required_capacity) {
        if ((size_t)required_capacity > SIZE_MAX / sizeof(tinypy_cst_node_t)) {
            return TINYPY_PARSER_OUT_OF_MEMORY;
        }
        n = (tinypy_cst_node_t *)tinypy_internal_compiler_arena_allocate(n1->context,
                                                                         (size_t)required_capacity * sizeof(tinypy_cst_node_t));
        if (n == NULL) {
            return TINYPY_PARSER_OUT_OF_MEMORY;
        }
        if (n1->children != NULL) {
            memcpy(n, n1->children, (size_t)nch * sizeof(tinypy_cst_node_t));
        }
        n1->children = n;
    }

    n = &n1->children[n1->child_count++];
    n1->context->cst_node_count += 1U;
    n->context = n1->context;
    n->type = type;
    n->text = str;
    n->line_number = lineno;
    n->column_offset = col_offset;
    n->child_count = 0;
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
    int i;
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
