/*
 * This file compiles an abstract syntax tree (AST) into Python bytecode.
 *
 * The primary entry point is __tinypy_ast_compile(), which returns a
 * tinypy_code_object_t.  The compiler makes several passes to build the code
 * object:
 *   1. Checks for future statements.  See future.c
 *   2. Builds a symbol table. See symbol_table.c.
 *   3. Generates code for basic blocks. See __tinypy_codegen_mod() in this file.
 *   4. Assembles the basic blocks into final code. See __tinypy_assembler_build() in
 *      this file.
 *   5. Optimizes the bytecode. See optimizer.c.
 *
 * Note that __tinypy_codegen_mod() suggests module, but the module ast type
 * (tinypy_ast_module_t) has cases for expressions and interactive statements.
 *
 * CAUTION: The visitor macros return early from the current function when they
 * encounter a problem. So don't invoke them when there is memory
 * which needs to be released. Code blocks are OK, as the compiler
 * structure takes care of releasing those.  Use the arena to manage
 * objects.
 */

#include "value_ops.h"

#include "ast_nodes.h"
#include "cst.h"
#include "ast_builder.h"
#include "bytecode_builder.h"
#include "codegen.h"
#include "symbol_table.h"
#include "../../bytecode/opcode.h"

#define TINYPY_CODEGEN_DEFAULT_BLOCK_SIZE 16
#define TINYPY_CODEGEN_DEFAULT_BLOCK_COUNT 8
#define TINYPY_CODEGEN_DEFAULT_CODE_SIZE 128
#define TINYPY_CODEGEN_DEFAULT_LINE_TABLE_SIZE 16

#define TINYPY_CODEGEN_COMPREHENSION_GENERATOR 0
#define TINYPY_CODEGEN_COMPREHENSION_SET 1
#define TINYPY_CODEGEN_COMPREHENSION_DICT 2

typedef struct tinypy_codegen_block_t tinypy_codegen_block_t;
typedef struct tinypy_codegen_instruction_t {
    unsigned i_jabs : 1;
    unsigned i_jrel : 1;
    unsigned i_hasarg : 1;
    uint8_t i_opcode;
    int32_t i_oparg;
    tinypy_codegen_block_t *i_target; /* target block (if jump instruction) */
    int32_t i_lineno;
} tinypy_codegen_instruction_t;
struct tinypy_codegen_block_t {
    /* Each tinypy_codegen_block_t in a compilation unit is linked via b_list in the
       reverse order that the block are allocated.  b_list points to the next
       block, not to be confused with b_next, which is next by control flow. */
    tinypy_codegen_block_t *b_list;
    /* number of instructions used */
    int32_t b_iused;
    /* length of instruction array (b_instr) */
    int32_t b_ialloc;
    /* pointer to an array of instructions, initially NULL */
    tinypy_codegen_instruction_t *b_instr;
    /* If b_next is non-NULL, it is a pointer to the next
       block reached by normal control flow. */
    tinypy_codegen_block_t *b_next;
    /* b_seen is used to perform a DFS of basicblocks. */
    unsigned b_seen : 1;
    /* b_return is TINYPY_COMPILER_TRUE if a TINYPY_OP_RETURN_VALUE opcode is inserted. */
    unsigned b_return : 1;
    /* depth of stack upon entry of block, computed by __tinypy_codegen_stack_depth() */
    int32_t b_startdepth;
    /* instruction offset for block, computed by __tinypy_assembler_resolve_jumps() */
    int32_t b_offset;
};
/* fblockinfo tracks the current frame block.

A frame block is used to handle loops, try/except, and try/finally.
It's called a frame block to distinguish it from a basic block in the
compiler IR.
*/

typedef enum tinypy_codegen_frame_block_e {
    TINYPY_CODEGEN_FRAME_BLOCK_LOOP,
    TINYPY_CODEGEN_FRAME_BLOCK_EXCEPT,
    TINYPY_CODEGEN_FRAME_BLOCK_FINALLY_TRY,
    TINYPY_CODEGEN_FRAME_BLOCK_FINALLY_END
} tinypy_codegen_frame_block_e;
typedef struct tinypy_codegen_frame_block_t {
    tinypy_codegen_frame_block_e fb_type;
    tinypy_codegen_block_t *fb_block;
} tinypy_codegen_frame_block_t;
/* The following items change on entry and exit of code blocks.
   They must be saved and restored when returning to a block.
*/
typedef struct tinypy_codegen_unit_t {
    tinypy_symbol_entry_t *u_ste;

    tinypy_value_t *u_name;
    /* The following fields are dicts that map objects to
       the index of them in co_XXX.      The index is used as
       the argument for opcodes that refer to those collections.
    */
    tinypy_value_t *u_consts;   /* all constants */
    tinypy_value_t *u_names;    /* all names */
    tinypy_value_t *u_varnames; /* local variables */
    tinypy_value_t *u_cellvars; /* cell variables */
    tinypy_value_t *u_freevars; /* free variables */

    tinypy_value_t *u_private; /* for private name mangling */

    int32_t u_argcount; /* number of arguments for block */
    /* Pointer to the most recently allocated block.  By following b_list
       members, you can reach all early allocated blocks. */
    tinypy_codegen_block_t *u_blocks;
    tinypy_codegen_block_t *u_curblock; /* pointer to current block */

    int32_t u_nfblocks;
    tinypy_codegen_frame_block_t u_fblock[TINYPY_COMPILER_MAX_BLOCKS];

    int32_t u_firstlineno;                      /* the first lineno of the block */
    int32_t u_lineno;                           /* the lineno for the current stmt */
    tinypy_compiler_boolean_e u_lineno_set; /* boolean to indicate whether instr
                          has been generated with current lineno */
} tinypy_codegen_unit_t;
/* This struct captures the global state of a compilation.

The u pointer points to the current compilation unit, while units
for enclosing blocks are stored in c_stack.     The u and c_stack are
managed by __tinypy_codegen_enter_scope() and __tinypy_codegen_exit_scope().
*/

typedef struct tinypy_codegen_t {
    const char *c_filename;
    tinypy_symbol_table_t *c_st;
    tinypy_future_features_t *c_future; /* pointer to module's __future__ */
    tinypy_compiler_flags_t *c_flags;

    tinypy_bool_t c_interactive; /* TINYPY_COMPILER_TRUE if in interactive mode */
    int32_t c_nestlevel;

    tinypy_codegen_unit_t *u;      /* compiler state for current block */
    tinypy_value_t *c_stack;       /* tinypy list holding codegen-unit handles */
    tinypy_compile_ctx_t *c_arena; /* pointer to memory allocation arena */
    tinypy_value_t *c_none;
    tinypy_value_t *c_ellipsis;
    tinypy_value_t *c_doc_name;
    tinypy_value_t *c_module_name;
    tinypy_value_t *c_lambda_name;
    tinypy_value_t *c_genexpr_name;
    tinypy_value_t *c_setcomp_name;
    tinypy_value_t *c_dictcomp_name;
    tinypy_value_t *c_empty_string;
    tinypy_value_t *c_assertion_error;
    int32_t c_optimize;
} tinypy_codegen_t;

static tinypy_bool_t __tinypy_codegen_enter_scope(tinypy_codegen_t *, tinypy_ast_identifier_t, void *, int32_t);
static void __tinypy_codegen_exit_scope(tinypy_codegen_t *);
static void __tinypy_codegen_free(tinypy_codegen_t *);
static tinypy_codegen_block_t *__tinypy_codegen_new_block(tinypy_codegen_t *);
static int32_t __tinypy_codegen_next_instr(tinypy_codegen_t *, tinypy_codegen_block_t *);
static tinypy_bool_t __tinypy_codegen_addop(tinypy_codegen_t *, int32_t);
static tinypy_bool_t __tinypy_codegen_addop_o(tinypy_codegen_t *, int32_t, tinypy_value_t *, tinypy_value_t *);
static tinypy_bool_t __tinypy_codegen_addop_i(tinypy_codegen_t *, int32_t, int32_t);
static tinypy_bool_t __tinypy_codegen_addop_j(tinypy_codegen_t *, int32_t, tinypy_codegen_block_t *, int32_t);
static tinypy_codegen_block_t *__tinypy_codegen_use_new_block(tinypy_codegen_t *);
static tinypy_bool_t __tinypy_codegen_error(tinypy_codegen_t *, const char *);
static tinypy_bool_t __tinypy_codegen_nameop(tinypy_codegen_t *, tinypy_ast_identifier_t, tinypy_ast_expression_context_e);

static tinypy_code_object_t *__tinypy_codegen_mod(tinypy_codegen_t *, tinypy_ast_module_t);
static tinypy_bool_t __tinypy_codegen_visit_stmt(tinypy_codegen_t *, tinypy_ast_statement_t);
static tinypy_bool_t __tinypy_codegen_visit_keyword(tinypy_codegen_t *, tinypy_ast_keyword_t);
static tinypy_bool_t __tinypy_codegen_visit_expr(tinypy_codegen_t *, tinypy_ast_expression_t);
static tinypy_bool_t __tinypy_codegen_augassign(tinypy_codegen_t *, tinypy_ast_statement_t);
static tinypy_bool_t __tinypy_codegen_visit_slice(tinypy_codegen_t *, tinypy_ast_slice_t, tinypy_ast_expression_context_e);

static tinypy_bool_t __tinypy_codegen_push_fblock(tinypy_codegen_t *, tinypy_codegen_frame_block_e, tinypy_codegen_block_t *);
static void __tinypy_codegen_pop_fblock(tinypy_codegen_t *, tinypy_codegen_frame_block_e, tinypy_codegen_block_t *);
/* Returns TINYPY_COMPILER_TRUE if there is a loop on the fblock stack. */
static tinypy_bool_t __tinypy_codegen_in_loop(tinypy_codegen_t *);

static int32_t __tinypy_codegen_inplace_binary_operator(tinypy_codegen_t *, tinypy_ast_binary_operator_e);
static int32_t __tinypy_codegen_expression_constant(tinypy_codegen_t *c, tinypy_ast_expression_t e);

static tinypy_bool_t __tinypy_codegen_with(tinypy_codegen_t *, tinypy_ast_statement_t);

static tinypy_code_object_t *__tinypy_assembler_build(tinypy_codegen_t *, int32_t addNone);
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_init(tinypy_codegen_t *c) {
    tinypy_vm_t *vm = c->c_arena->vm;

    c->c_stack = tinypy_list_from_items(vm, NULL, 0U);
    c->c_none = tinypy_none_get(vm);
    c->c_ellipsis = tinypy_ellipsis_get(vm);
    c->c_doc_name = tinypy_string_from_bytes(vm, "__doc__", 7U);
    c->c_module_name = tinypy_string_from_bytes(vm, "<module>", 8U);
    c->c_lambda_name = tinypy_string_from_bytes(vm, "<lambda>", 8U);
    c->c_genexpr_name = tinypy_string_from_bytes(vm, "<genexpr>", 9U);
    c->c_setcomp_name = tinypy_string_from_bytes(vm, "<setcomp>", 9U);
    c->c_dictcomp_name = tinypy_string_from_bytes(vm, "<dictcomp>", 10U);
    c->c_empty_string = tinypy_string_from_bytes(vm, NULL, 0U);
    c->c_assertion_error = tinypy_string_from_bytes(vm, "AssertionError", 14U);
    tinypy_internal_string_set_interned(c->c_doc_name, 1);
    tinypy_internal_string_set_interned(c->c_module_name, 1);
    tinypy_internal_string_set_interned(c->c_lambda_name, 1);
    tinypy_internal_string_set_interned(c->c_assertion_error, 1);
    c->c_optimize = c->c_arena->options.optimize_level;

    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_code_object_t *__tinypy_ast_compile(tinypy_compile_ctx_t *arena, tinypy_ast_module_t mod, const char *filename, tinypy_compiler_flags_t *flags, tinypy_future_features_t *future, tinypy_symbol_table_t *symbols) {
    tinypy_codegen_t c;
    tinypy_code_object_t *co = NULL;
    tinypy_compiler_flags_t local_flags;
    int32_t merged;

    memset(&c, 0, sizeof(c));
    c.c_arena = arena;
    if (!__tinypy_codegen_init(&c)) {
        return NULL;
    }
    c.c_filename = filename;
    c.c_future = future;
    if (!flags) {
        local_flags.flags = 0;
        flags = &local_flags;
    }
    merged = c.c_future->features | flags->flags;
    c.c_future->features = merged;
    flags->flags = merged;
    c.c_flags = flags;
    c.c_nestlevel = 0;

    c.c_st = symbols;

    co = __tinypy_codegen_mod(&c, mod);

    __tinypy_codegen_free(&c);
    return co;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_codegen_free(tinypy_codegen_t *c) {
    TINYPY_COMPILER_XDECREF(c->c_stack);
    TINYPY_COMPILER_XDECREF(c->c_none);
    TINYPY_COMPILER_XDECREF(c->c_ellipsis);
    TINYPY_COMPILER_XDECREF(c->c_doc_name);
    TINYPY_COMPILER_XDECREF(c->c_module_name);
    TINYPY_COMPILER_XDECREF(c->c_lambda_name);
    TINYPY_COMPILER_XDECREF(c->c_genexpr_name);
    TINYPY_COMPILER_XDECREF(c->c_setcomp_name);
    TINYPY_COMPILER_XDECREF(c->c_dictcomp_name);
    TINYPY_COMPILER_XDECREF(c->c_empty_string);
    TINYPY_COMPILER_XDECREF(c->c_assertion_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codegen_list_to_dict(tinypy_value_t *list) {
    tinypy_compiler_size_t i, n;
    tinypy_value_t *v, *k;
    tinypy_value_t *dict = __tinypy_frontend_dict_new_from_owner(list);
    if (!dict) {
        return NULL;
    }

    n = TINYPY_COMPILER_LIST_SIZE(list);
    for (i = 0; i < n; i++) {
        v = __tinypy_frontend_integer_from_owner(list, i);
        if (!v) {
            TINYPY_COMPILER_DECREF(dict);
            return NULL;
        }
        k = TINYPY_COMPILER_LIST_GET_ITEM(list, i);
        k = __tinypy_bytecode_constant_key(k);
        if (k == NULL || TINYPY_COMPILER_DICT_SET_ITEM(dict, k, v) < 0) {
            TINYPY_COMPILER_XDECREF(k);
            TINYPY_COMPILER_DECREF(v);
            TINYPY_COMPILER_DECREF(dict);
            return NULL;
        }
        TINYPY_COMPILER_DECREF(k);
        TINYPY_COMPILER_DECREF(v);
    }
    return dict;
}
/* Return new dict containing names from src that match scope(s).

src is a symbol table dictionary.  If the scope of a name matches
either scope_type or flag is set, insert it into the new dict.  The
values are integers, starting at offset and increasing by one for
each key.
*/

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codegen_dict_by_type(tinypy_value_t *src, int32_t scope_type, int32_t flag, int32_t offset) {
    tinypy_compiler_size_t i = offset, scope, num_keys, key_i;
    tinypy_value_t *k, *v, *dest = __tinypy_frontend_dict_new_from_owner(src);

    if (dest == NULL) {
        return NULL;
    }

    /* Sort the keys so that we have a deterministic order on the indexes
       saved in the returned dictionary.  These indexes are used as indexes
       into the free and cell var storage.  Therefore if they aren't
       deterministic, then the generated bytecode is not deterministic.
    */
    tinypy_value_t *sorted_keys = TINYPY_COMPILER_DICT_KEYS(src);
    if (sorted_keys == NULL) {
        return NULL;
    }
    if (TINYPY_COMPILER_LIST_SORT(sorted_keys) != 0) {
        TINYPY_COMPILER_DECREF(sorted_keys);
        return NULL;
    }
    num_keys = TINYPY_COMPILER_LIST_GET_SIZE(sorted_keys);

    for (key_i = 0; key_i < num_keys; key_i++) {
        k = TINYPY_COMPILER_LIST_GET_ITEM(sorted_keys, key_i);
        v = TINYPY_COMPILER_DICT_GET_ITEM(src, k);
        /* Scope values are encoded by symbol_table.h. */
        scope = (TINYPY_COMPILER_INT_AS_LONG(v) >> TINYPY_SYMBOL_SCOPE_OFFSET) & TINYPY_SYMBOL_SCOPE_MASK;

        if (scope == scope_type || TINYPY_COMPILER_INT_AS_LONG(v) & flag) {
            tinypy_value_t *tuple, *item = __tinypy_frontend_integer_from_owner(src, i);
            if (item == NULL) {
                TINYPY_COMPILER_DECREF(sorted_keys);
                TINYPY_COMPILER_DECREF(dest);
                return NULL;
            }
            i++;
            tuple = __tinypy_bytecode_constant_key(k);
            if (!tuple || TINYPY_COMPILER_DICT_SET_ITEM(dest, tuple, item) < 0) {
                TINYPY_COMPILER_DECREF(sorted_keys);
                TINYPY_COMPILER_DECREF(item);
                TINYPY_COMPILER_DECREF(dest);
                TINYPY_COMPILER_XDECREF(tuple);
                return NULL;
            }
            TINYPY_COMPILER_DECREF(item);
            TINYPY_COMPILER_DECREF(tuple);
        }
    }
    TINYPY_COMPILER_DECREF(sorted_keys);
    return dest;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_codegen_unit_free(tinypy_codegen_unit_t *u) {
    TINYPY_COMPILER_CLEAR(u->u_name);
    TINYPY_COMPILER_CLEAR(u->u_consts);
    TINYPY_COMPILER_CLEAR(u->u_names);
    TINYPY_COMPILER_CLEAR(u->u_varnames);
    TINYPY_COMPILER_CLEAR(u->u_freevars);
    TINYPY_COMPILER_CLEAR(u->u_cellvars);
    TINYPY_COMPILER_CLEAR(u->u_private);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_enter_scope(tinypy_codegen_t *c, tinypy_ast_identifier_t name, void *key, int32_t lineno) {

    tinypy_codegen_unit_t *u = (tinypy_codegen_unit_t *)TINYPY_COMPILER_ARENA_MALLOC(c->c_arena, sizeof(tinypy_codegen_unit_t));
    if (!u) {
        return TINYPY_FALSE;
    }
    memset(u, 0, sizeof(tinypy_codegen_unit_t));
    u->u_argcount = 0;
    u->u_ste = __tinypy_symbol_table_lookup(c->c_st, key);
    if (!u->u_ste) {
        __tinypy_codegen_unit_free(u);
        return TINYPY_FALSE;
    }
    TINYPY_COMPILER_INCREF(name);
    u->u_name = name;
    u->u_varnames = __tinypy_codegen_list_to_dict(u->u_ste->variable_names);
    u->u_cellvars = __tinypy_codegen_dict_by_type(u->u_ste->symbols, TINYPY_SYMBOL_SCOPE_CELL, 0, 0);
    if (!u->u_varnames || !u->u_cellvars) {
        __tinypy_codegen_unit_free(u);
        return TINYPY_FALSE;
    }

    u->u_freevars = __tinypy_codegen_dict_by_type(u->u_ste->symbols, TINYPY_SYMBOL_SCOPE_FREE, TINYPY_SYMBOL_DEFINITION_FREE_CLASS,
                                                  (int32_t)TINYPY_COMPILER_DICT_SIZE(u->u_cellvars));
    if (!u->u_freevars) {
        __tinypy_codegen_unit_free(u);
        return TINYPY_FALSE;
    }

    u->u_blocks = NULL;
    u->u_nfblocks = 0;
    u->u_firstlineno = lineno;
    u->u_lineno = 0;
    u->u_lineno_set = TINYPY_COMPILER_FALSE;
    u->u_consts = __tinypy_frontend_dict_new_from_owner(name);
    if (!u->u_consts) {
        __tinypy_codegen_unit_free(u);
        return TINYPY_FALSE;
    }
    u->u_names = __tinypy_frontend_dict_new_from_owner(name);
    if (!u->u_names) {
        __tinypy_codegen_unit_free(u);
        return TINYPY_FALSE;
    }

    u->u_private = NULL;

    /* Push the old compiler_unit on the stack. */
    if (c->u) {
        tinypy_value_t *handle = __tinypy_frontend_pointer_handle(c->u->u_name, c->u);
        if (!handle || TINYPY_COMPILER_LIST_APPEND(c->c_stack, handle) < 0) {
            TINYPY_COMPILER_XDECREF(handle);
            __tinypy_codegen_unit_free(u);
            return TINYPY_FALSE;
        }
        TINYPY_COMPILER_DECREF(handle);
        u->u_private = c->u->u_private;
        TINYPY_COMPILER_XINCREF(u->u_private);
    }
    c->u = u;

    c->c_nestlevel++;
    if (__tinypy_codegen_use_new_block(c) == NULL) {
        __tinypy_codegen_exit_scope(c);
        return TINYPY_FALSE;
    }

    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_codegen_exit_scope(tinypy_codegen_t *c) {
    int32_t n;
    tinypy_value_t *handle;

    c->c_nestlevel--;
    __tinypy_codegen_unit_free(c->u);
    /* Restore c->u to the parent unit. */
    n = (int32_t)TINYPY_COMPILER_LIST_GET_SIZE(c->c_stack) - 1;
    if (n >= 0) {
        handle = TINYPY_COMPILER_LIST_GET_ITEM(c->c_stack, n);
        c->u = (tinypy_codegen_unit_t *)__tinypy_frontend_pointer_from_handle(handle);
        /* we are deleting from a list so this really shouldn't fail */
        (void)TINYPY_COMPILER_SEQUENCE_DEL_ITEM(c->c_stack, n);
    }
    else {
        c->u = NULL;
    }
}
/* Allocate a new block and return a pointer to it.
   Returns NULL on error.
*/

//////////////////////////////////////////////////////////////////////////
static tinypy_codegen_block_t *__tinypy_codegen_new_block(tinypy_codegen_t *c) {

    tinypy_codegen_unit_t *u = c->u;
    if (c->c_arena->limits.max_blocks != 0U && c->c_arena->block_count >= c->c_arena->limits.max_blocks) {
        tinypy_internal_compiler_error(c->c_arena, TINYPY_ERROR_COMPILER_LIMIT, "basic block limit exceeded", c->u->u_lineno, 1, c->c_arena->out_error);
        return NULL;
    }
    c->c_arena->block_count += 1U;
    tinypy_codegen_block_t *b = (tinypy_codegen_block_t *)TINYPY_COMPILER_ARENA_MALLOC(c->c_arena, sizeof(tinypy_codegen_block_t));
    if (b == NULL) {
        return NULL;
    }
    memset((void *)b, 0, sizeof(tinypy_codegen_block_t));
    /* Extend the singly linked list of blocks with new block. */
    b->b_list = u->u_blocks;
    u->u_blocks = b;
    return b;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_codegen_block_t *__tinypy_codegen_use_new_block(tinypy_codegen_t *c) {
    tinypy_codegen_block_t *block = __tinypy_codegen_new_block(c);
    if (block == NULL) {
        return NULL;
    }
    c->u->u_curblock = block;
    return block;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_codegen_block_t *__tinypy_codegen_next_block(tinypy_codegen_t *c) {
    tinypy_codegen_block_t *block = __tinypy_codegen_new_block(c);
    if (block == NULL) {
        return NULL;
    }
    c->u->u_curblock->b_next = block;
    c->u->u_curblock = block;
    return block;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_codegen_block_t *__tinypy_codegen_use_next_block(tinypy_codegen_t *c, tinypy_codegen_block_t *block) {
    c->u->u_curblock->b_next = block;
    c->u->u_curblock = block;
    return block;
}
/* Returns the offset of the next instruction in the current block's
   b_instr array.  Resizes the b_instr as necessary.
   Returns -1 on failure.
*/

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codegen_next_instr(tinypy_codegen_t *c, tinypy_codegen_block_t *b) {
    if (c->c_arena->limits.max_instructions != 0U && c->c_arena->instruction_count >= c->c_arena->limits.max_instructions) {
        tinypy_internal_compiler_error(c->c_arena, TINYPY_ERROR_COMPILER_LIMIT, "instruction limit exceeded", c->u->u_lineno, 1, c->c_arena->out_error);
        return -1;
    }
    c->c_arena->instruction_count += 1U;
    if (b->b_instr == NULL) {
        b->b_instr = (tinypy_codegen_instruction_t *)TINYPY_COMPILER_ARENA_MALLOC(c->c_arena,
                                                                                  sizeof(tinypy_codegen_instruction_t) * TINYPY_CODEGEN_DEFAULT_BLOCK_SIZE);
        if (b->b_instr == NULL) {
            TINYPY_COMPILER_ERR_NO_MEMORY();
            return -1;
        }
        b->b_ialloc = TINYPY_CODEGEN_DEFAULT_BLOCK_SIZE;
        memset((char *)b->b_instr, 0,
               sizeof(tinypy_codegen_instruction_t) * TINYPY_CODEGEN_DEFAULT_BLOCK_SIZE);
    }
    else if (b->b_iused == b->b_ialloc) {
        tinypy_codegen_instruction_t *tmp;
        size_t oldsize, newsize;
        oldsize = b->b_ialloc * sizeof(tinypy_codegen_instruction_t);
        newsize = oldsize << 1;

        if (oldsize > (SIZE_MAX >> 1)) {
            TINYPY_COMPILER_ERR_NO_MEMORY();
            return -1;
        }

        if (newsize == 0) {
            TINYPY_COMPILER_ERR_NO_MEMORY();
            return -1;
        }
        b->b_ialloc <<= 1;
        tmp = (tinypy_codegen_instruction_t *)TINYPY_COMPILER_ARENA_MALLOC(c->c_arena, newsize);
        if (tmp == NULL) {
            return -1;
        }
        (void)memcpy(tmp, b->b_instr, oldsize);
        b->b_instr = tmp;
        memset((char *)b->b_instr + oldsize, 0, newsize - oldsize);
    }
    return b->b_iused++;
}
/* Set the i_lineno member of the instruction at offset off if the
   line number for the current expression/statement has not
   already been set.  If it has been set, the call has no effect.

   The line number is reset in the following cases:
   - when entering a new scope
   - on each statement
   - on each expression that start a new line
   - before the "except" clause
   - before the "for" and "while" expressions
*/

//////////////////////////////////////////////////////////////////////////
static void __tinypy_codegen_set_lineno(tinypy_codegen_t *c, int32_t off) {
    if (c->u->u_lineno_set) {
        return;
    }
    c->u->u_lineno_set = TINYPY_COMPILER_TRUE;
    tinypy_codegen_block_t *b = c->u->u_curblock;
    b->b_instr[off].i_lineno = c->u->u_lineno;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_opcode_stack_effect(int32_t opcode, int32_t oparg) {
    int32_t function_result;
    switch (opcode) {
    case TINYPY_OP_POP_TOP:
        return -1;
    case TINYPY_OP_ROT_TWO:
    case TINYPY_OP_ROT_THREE:
        return 0;
    case TINYPY_OP_DUP_TOP:
        return 1;
    case TINYPY_OP_ROT_FOUR:
        return 0;

    case TINYPY_OP_UNARY_POSITIVE:
    case TINYPY_OP_UNARY_NEGATIVE:
    case TINYPY_OP_UNARY_NOT:
    case TINYPY_OP_UNARY_CONVERT:
    case TINYPY_OP_UNARY_INVERT:
        return 0;

    case TINYPY_OP_SET_ADD:
    case TINYPY_OP_LIST_APPEND:
        return -1;

    case TINYPY_OP_MAP_ADD:
        return -2;

    case TINYPY_OP_BINARY_POWER:
    case TINYPY_OP_BINARY_MULTIPLY:
    case TINYPY_OP_BINARY_DIVIDE:
    case TINYPY_OP_BINARY_MODULO:
    case TINYPY_OP_BINARY_ADD:
    case TINYPY_OP_BINARY_SUBTRACT:
    case TINYPY_OP_BINARY_SUBSCR:
    case TINYPY_OP_BINARY_FLOOR_DIVIDE:
    case TINYPY_OP_BINARY_TRUE_DIVIDE:
        return -1;
    case TINYPY_OP_INPLACE_FLOOR_DIVIDE:
    case TINYPY_OP_INPLACE_TRUE_DIVIDE:
        return -1;

    case TINYPY_OP_SLICE_0 + 0:
        return 0;
    case TINYPY_OP_SLICE_0 + 1:
        return -1;
    case TINYPY_OP_SLICE_0 + 2:
        return -1;
    case TINYPY_OP_SLICE_0 + 3:
        return -2;

    case TINYPY_OP_STORE_SLICE_0 + 0:
        return -2;
    case TINYPY_OP_STORE_SLICE_0 + 1:
        return -3;
    case TINYPY_OP_STORE_SLICE_0 + 2:
        return -3;
    case TINYPY_OP_STORE_SLICE_0 + 3:
        return -4;

    case TINYPY_OP_DELETE_SLICE_0 + 0:
        return -1;
    case TINYPY_OP_DELETE_SLICE_0 + 1:
        return -2;
    case TINYPY_OP_DELETE_SLICE_0 + 2:
        return -2;
    case TINYPY_OP_DELETE_SLICE_0 + 3:
        return -3;

    case TINYPY_OP_INPLACE_ADD:
    case TINYPY_OP_INPLACE_SUBTRACT:
    case TINYPY_OP_INPLACE_MULTIPLY:
    case TINYPY_OP_INPLACE_DIVIDE:
    case TINYPY_OP_INPLACE_MODULO:
        return -1;
    case TINYPY_OP_STORE_SUBSCR:
        return -3;
    case TINYPY_OP_STORE_MAP:
        return -2;
    case TINYPY_OP_DELETE_SUBSCR:
        return -2;

    case TINYPY_OP_BINARY_LSHIFT:
    case TINYPY_OP_BINARY_RSHIFT:
    case TINYPY_OP_BINARY_AND:
    case TINYPY_OP_BINARY_XOR:
    case TINYPY_OP_BINARY_OR:
        return -1;
    case TINYPY_OP_INPLACE_POWER:
        return -1;
    case TINYPY_OP_GET_ITER:
        return 0;

    case TINYPY_OP_PRINT_EXPR:
        return -1;
    case TINYPY_OP_PRINT_ITEM:
        return -1;
    case TINYPY_OP_PRINT_NEWLINE:
        return 0;
    case TINYPY_OP_PRINT_ITEM_TO:
        return -2;
    case TINYPY_OP_PRINT_NEWLINE_TO:
        return -1;
    case TINYPY_OP_INPLACE_LSHIFT:
    case TINYPY_OP_INPLACE_RSHIFT:
    case TINYPY_OP_INPLACE_AND:
    case TINYPY_OP_INPLACE_XOR:
    case TINYPY_OP_INPLACE_OR:
        return -1;
    case TINYPY_OP_BREAK_LOOP:
        return 0;
    case TINYPY_OP_SETUP_WITH:
        return 4;
    case TINYPY_OP_WITH_CLEANUP:
        return -1; /* XXX Sometimes more */
    case TINYPY_OP_LOAD_LOCALS:
        return 1;
    case TINYPY_OP_RETURN_VALUE:
        return -1;
    case TINYPY_OP_IMPORT_STAR:
        return -1;
    case TINYPY_OP_EXEC_STMT:
        return -3;
    case TINYPY_OP_YIELD_VALUE:
        return 0;

    case TINYPY_OP_POP_BLOCK:
        return 0;
    case TINYPY_OP_END_FINALLY:
        return -3; /* or -1 or -2 if no exception occurred or
                      return/break/continue */
    case TINYPY_OP_BUILD_CLASS:
        return -2;

    case TINYPY_OP_STORE_NAME:
        return -1;
    case TINYPY_OP_DELETE_NAME:
        return 0;
    case TINYPY_OP_UNPACK_SEQUENCE:
        return oparg - 1;
    case TINYPY_OP_FOR_ITER:
        return 1; /* or -1, at end of iterator */

    case TINYPY_OP_STORE_ATTR:
        return -2;
    case TINYPY_OP_DELETE_ATTR:
        return -1;
    case TINYPY_OP_STORE_GLOBAL:
        return -1;
    case TINYPY_OP_DELETE_GLOBAL:
        return 0;
    case TINYPY_OP_DUP_TOPX:
        return oparg;
    case TINYPY_OP_LOAD_CONST:
        return 1;
    case TINYPY_OP_LOAD_NAME:
        return 1;
    case TINYPY_OP_BUILD_TUPLE:
    case TINYPY_OP_BUILD_LIST:
    case TINYPY_OP_BUILD_SET:
        return 1 - oparg;
    case TINYPY_OP_BUILD_MAP:
        return 1;
    case TINYPY_OP_LOAD_ATTR:
        return 0;
    case TINYPY_OP_COMPARE_OP:
        return -1;
    case TINYPY_OP_IMPORT_NAME:
        return -1;
    case TINYPY_OP_IMPORT_FROM:
        return 1;

    case TINYPY_OP_JUMP_FORWARD:
    case TINYPY_OP_JUMP_IF_TRUE_OR_POP:  /* -1 if jump not taken */
    case TINYPY_OP_JUMP_IF_FALSE_OR_POP: /*  "" */
    case TINYPY_OP_JUMP_ABSOLUTE:
        return 0;

    case TINYPY_OP_POP_JUMP_IF_FALSE:
    case TINYPY_OP_POP_JUMP_IF_TRUE:
        return -1;

    case TINYPY_OP_LOAD_GLOBAL:
        return 1;

    case TINYPY_OP_CONTINUE_LOOP:
        return 0;
    case TINYPY_OP_SETUP_LOOP:
    case TINYPY_OP_SETUP_EXCEPT:
    case TINYPY_OP_SETUP_FINALLY:
        return 0;

    case TINYPY_OP_LOAD_FAST:
        return 1;
    case TINYPY_OP_STORE_FAST:
        return -1;
    case TINYPY_OP_DELETE_FAST:
        return 0;

    case TINYPY_OP_RAISE_VARARGS:
        return -oparg;
#define TINYPY_CODEGEN_ARGUMENT_COUNT(o) (((o) % 256) + 2 * ((o) / 256))
    case TINYPY_OP_CALL_FUNCTION:
        function_result = -TINYPY_CODEGEN_ARGUMENT_COUNT(oparg);
        return function_result;
    case TINYPY_OP_CALL_FUNCTION_VAR:
    case TINYPY_OP_CALL_FUNCTION_KW:
        function_result = -TINYPY_CODEGEN_ARGUMENT_COUNT(oparg) - 1;
        return function_result;
    case TINYPY_OP_CALL_FUNCTION_VAR_KW:
        function_result = -TINYPY_CODEGEN_ARGUMENT_COUNT(oparg) - 2;
        return function_result;
#undef NARGS
    case TINYPY_OP_MAKE_FUNCTION:
        return -oparg;
    case TINYPY_OP_BUILD_SLICE:
        if (oparg == 3) {
            return -2;
        }
        else {
            return -1;
        }

    case TINYPY_OP_MAKE_CLOSURE:
        return -oparg - 1;
    case TINYPY_OP_LOAD_CLOSURE:
        return 1;
    case TINYPY_OP_LOAD_DEREF:
        return 1;
    case TINYPY_OP_STORE_DEREF:
        return -1;
    }
    return 0; /* not reachable */
}
/* Add an opcode with no argument.
   Returns 0 on failure, 1 on success.
*/

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_addop(tinypy_codegen_t *c, int32_t opcode) {
    int32_t off = __tinypy_codegen_next_instr(c, c->u->u_curblock);
    if (off < 0) {
        return TINYPY_FALSE;
    }
    tinypy_codegen_block_t *b = c->u->u_curblock;
    tinypy_codegen_instruction_t *i = &b->b_instr[off];
    i->i_opcode = opcode;
    i->i_hasarg = 0;
    if (opcode == TINYPY_OP_RETURN_VALUE) {
        b->b_return = 1;
    }
    __tinypy_codegen_set_lineno(c, off);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codegen_add_o(tinypy_codegen_t *c, tinypy_value_t *dict, tinypy_value_t *o) {
    tinypy_value_t *t, *v;
    tinypy_compiler_size_t arg;

    (void)c;
    t = __tinypy_bytecode_constant_key(o);
    if (t == NULL) {
        return -1;
    }

    v = TINYPY_COMPILER_DICT_GET_ITEM(dict, t);
    if (!v) {
        size_t constant_bytes = __tinypy_frontend_constant_size(o);

        if (c->c_arena->limits.max_constants != 0U && c->c_arena->constant_count >= c->c_arena->limits.max_constants) {
            TINYPY_COMPILER_DECREF(t);
            tinypy_internal_compiler_error(c->c_arena, TINYPY_ERROR_COMPILER_LIMIT, "constant limit exceeded", c->u->u_lineno, 1, c->c_arena->out_error);
            return -1;
        }
        if (c->c_arena->limits.max_constant_bytes != 0U && (c->c_arena->constant_bytes > c->c_arena->limits.max_constant_bytes || constant_bytes > c->c_arena->limits.max_constant_bytes - c->c_arena->constant_bytes)) {
            TINYPY_COMPILER_DECREF(t);
            tinypy_internal_compiler_error(c->c_arena, TINYPY_ERROR_COMPILER_LIMIT, "constant byte limit exceeded", c->u->u_lineno, 1, c->c_arena->out_error);
            return -1;
        }
        c->c_arena->constant_count += 1U;
        c->c_arena->constant_bytes += constant_bytes;
        arg = TINYPY_COMPILER_DICT_SIZE(dict);
        v = __tinypy_frontend_integer_from_owner(dict, arg);
        if (!v) {
            TINYPY_COMPILER_DECREF(t);
            return -1;
        }
        if (TINYPY_COMPILER_DICT_SET_ITEM(dict, t, v) < 0) {
            TINYPY_COMPILER_DECREF(t);
            TINYPY_COMPILER_DECREF(v);
            return -1;
        }
        TINYPY_COMPILER_DECREF(v);
    }
    else {
        arg = TINYPY_COMPILER_INT_AS_LONG(v);
    }
    TINYPY_COMPILER_DECREF(t);
    return (int32_t)arg;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_addop_o(tinypy_codegen_t *c, int32_t opcode, tinypy_value_t *dict, tinypy_value_t *o) {
    int32_t arg = __tinypy_codegen_add_o(c, dict, o);
    if (arg < 0) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = __tinypy_codegen_addop_i(c, opcode, arg);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_addop_name(tinypy_codegen_t *c, int32_t opcode, tinypy_value_t *dict, tinypy_value_t *o) {
    int32_t arg;
    tinypy_value_t *mangled = __tinypy_frontend_mangle(c->c_arena, c->u->u_private, o);
    if (!mangled) {
        return TINYPY_FALSE;
    }
    arg = __tinypy_codegen_add_o(c, dict, mangled);
    TINYPY_COMPILER_DECREF(mangled);
    if (arg < 0) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = __tinypy_codegen_addop_i(c, opcode, arg);
    return return_value_1;
}
/* Add an opcode with an integer argument.
   Returns 0 on failure, 1 on success.
*/

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_addop_i(tinypy_codegen_t *c, int32_t opcode, int32_t oparg) {
    int32_t off = __tinypy_codegen_next_instr(c, c->u->u_curblock);
    if (off < 0) {
        return TINYPY_FALSE;
    }
    tinypy_codegen_instruction_t *i = &c->u->u_curblock->b_instr[off];
    i->i_opcode = opcode;
    i->i_oparg = oparg;
    i->i_hasarg = 1;
    __tinypy_codegen_set_lineno(c, off);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_addop_j(tinypy_codegen_t *c, int32_t opcode, tinypy_codegen_block_t *b, int32_t absolute) {
    int32_t off;

    off = __tinypy_codegen_next_instr(c, c->u->u_curblock);
    if (off < 0) {
        return TINYPY_FALSE;
    }
    tinypy_codegen_instruction_t *i = &c->u->u_curblock->b_instr[off];
    i->i_opcode = opcode;
    i->i_target = b;
    i->i_hasarg = 1;
    if (absolute) {
        i->i_jabs = 1;
    }
    else {
        i->i_jrel = 1;
    }
    __tinypy_codegen_set_lineno(c, off);
    return TINYPY_TRUE;
}

/* The distinction between NEW_BLOCK and NEXT_BLOCK is subtle.  (I'd
   like to find better names.)  TINYPY_CODEGEN_NEW_BLOCK() creates a new block and sets
   it as the current block.  TINYPY_CODEGEN_NEXT_BLOCK() also creates an implicit jump
   from the current block to the new block.
*/

/* The returns inside these macros make it impossible to decref objects
   created in the local function.  Local objects should use the arena.
*/

#define TINYPY_CODEGEN_NEW_BLOCK(C)                      \
    {                                                    \
        if (__tinypy_codegen_use_new_block((C)) == NULL) \
            return 0;                \
    }

#define TINYPY_CODEGEN_NEXT_BLOCK(C)                  \
    {                                                 \
        if (__tinypy_codegen_next_block((C)) == NULL) \
            return 0;             \
    }

#define TINYPY_CODEGEN_ADD_OPCODE(C, OP)        \
    {                                           \
        if (!__tinypy_codegen_addop((C), (OP))) \
            return 0;       \
    }

#define TINYPY_CODEGEN_ADD_OPCODE_IN_SCOPE(C, OP) \
    {                                             \
        if (!__tinypy_codegen_addop((C), (OP))) { \
            __tinypy_codegen_exit_scope(c);       \
            return 0;         \
        }                                         \
    }

#define TINYPY_CODEGEN_ADD_OBJECT_OPCODE(C, OP, O, TYPE)                 \
    {                                                                    \
        if (!__tinypy_codegen_addop_o((C), (OP), (C)->u->u_##TYPE, (O))) \
            return 0;                                \
    }

/* Same as ADDOP_O, but steals a reference. */
#define TINYPY_CODEGEN_ADD_NEW_OBJECT_OPCODE(C, OP, O, TYPE)             \
    {                                                                    \
        if (!__tinypy_codegen_addop_o((C), (OP), (C)->u->u_##TYPE, (O))) { \
            TINYPY_COMPILER_DECREF((O));                                 \
            return 0;                               \
        }                                                                \
        TINYPY_COMPILER_DECREF((O));                                     \
    }

#define TINYPY_CODEGEN_ADD_NAME_OPCODE(C, OP, O, TYPE)                      \
    {                                                                       \
        if (!__tinypy_codegen_addop_name((C), (OP), (C)->u->u_##TYPE, (O))) \
            return 0;                                   \
    }

#define TINYPY_CODEGEN_ADD_INTEGER_OPCODE(C, OP, O)    \
    {                                                  \
        if (!__tinypy_codegen_addop_i((C), (OP), (O))) \
            return 0;              \
    }

#define TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(C, OP, O)        \
    {                                                     \
        if (!__tinypy_codegen_addop_j((C), (OP), (O), 1)) \
            return 0;                 \
    }

#define TINYPY_CODEGEN_ADD_RELATIVE_JUMP(C, OP, O)        \
    {                                                     \
        if (!__tinypy_codegen_addop_j((C), (OP), (O), 0)) \
            return 0;                 \
    }

/* AST visitor helpers dispatch to the matching tinypy node handler. */

#define TINYPY_CODEGEN_VISIT(C, TYPE, V)              \
    {                                                 \
        if (!__tinypy_codegen_visit_##TYPE((C), (V))) \
            return 0;             \
    }

#define TINYPY_CODEGEN_VISIT_IN_SCOPE(C, TYPE, V)     \
    {                                                 \
        if (!__tinypy_codegen_visit_##TYPE((C), (V))) { \
            __tinypy_codegen_exit_scope(c);           \
            return 0;             \
        }                                             \
    }

#define TINYPY_CODEGEN_VISIT_SLICE(C, V, CTX)               \
    {                                                       \
        if (!__tinypy_codegen_visit_slice((C), (V), (CTX))) \
            return 0;                   \
    }

#define TINYPY_CODEGEN_VISIT_SEQUENCE(C, TYPE, SEQ)                                 \
    {                                                                               \
        int32_t _i;                                                                     \
        tinypy_ast_sequence_t *seq = (SEQ); /* avoid variable capture */            \
        for (_i = 0; _i < TINYPY_AST_SEQUENCE_LENGTH(seq); _i++) { \
            TINYPY_AST_SEQUENCE_TYPE(TYPE)                                          \
            elt = (TINYPY_AST_SEQUENCE_TYPE(TYPE))TINYPY_AST_SEQUENCE_GET(seq, _i); \
            if (!__tinypy_codegen_visit_##TYPE((C), elt))                           \
                return 0;                                       \
        }                                                                           \
    }

#define TINYPY_CODEGEN_VISIT_SEQUENCE_IN_SCOPE(C, TYPE, SEQ)                        \
    {                                                                               \
        int32_t _i;                                                                     \
        tinypy_ast_sequence_t *seq = (SEQ); /* avoid variable capture */            \
        for (_i = 0; _i < TINYPY_AST_SEQUENCE_LENGTH(seq); _i++) { \
            TINYPY_AST_SEQUENCE_TYPE(TYPE)                                          \
            elt = (TINYPY_AST_SEQUENCE_TYPE(TYPE))TINYPY_AST_SEQUENCE_GET(seq, _i); \
            if (!__tinypy_codegen_visit_##TYPE((C), elt)) { \
                __tinypy_codegen_exit_scope(c);                                     \
                return 0;                                       \
            }                                                                       \
        }                                                                           \
    }
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_isdocstring(tinypy_ast_statement_t s) {
    if (s->kind != TINYPY_AST_KIND_EXPR) {
        return TINYPY_FALSE;
    }
    return s->v.Expr.value->kind == TINYPY_AST_KIND_STR;
}
/* Compile a sequence of statements, checking for a docstring. */

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_body(tinypy_codegen_t *c, tinypy_ast_sequence_t *stmts) {
    int32_t i = 0;
    tinypy_ast_statement_t st;

    if (!TINYPY_AST_SEQUENCE_LENGTH(stmts)) {
        return TINYPY_TRUE;
    }
    st = (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(stmts, 0);
    if (__tinypy_codegen_isdocstring(st) && c->c_optimize < 2) {
        /* don't generate docstrings if -OO */
        i = 1;
        TINYPY_CODEGEN_VISIT(c, expr, st->v.Expr.value);
        if (!__tinypy_codegen_nameop(c, c->c_doc_name, TINYPY_AST_CONTEXT_STORE)) {
            return TINYPY_FALSE;
        }
    }
    for (; i < TINYPY_AST_SEQUENCE_LENGTH(stmts); i++) {
        TINYPY_CODEGEN_VISIT(c, stmt, (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(stmts, i));
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_code_object_t *__tinypy_codegen_mod(tinypy_codegen_t *c, tinypy_ast_module_t mod) {
    int32_t addNone = 1;
    /* Use 0 for firstlineno initially, will fixup in __tinypy_assembler_build(). */
    if (!__tinypy_codegen_enter_scope(c, c->c_module_name, mod, 0)) {
        return NULL;
    }
    switch (mod->kind) {
    case TINYPY_AST_KIND_MODULE:
        if (!__tinypy_codegen_body(c, mod->v.Module.body)) {
            __tinypy_codegen_exit_scope(c);
            return 0;
        }
        break;
    case TINYPY_AST_KIND_INTERACTIVE:
        c->c_interactive = 1;
        TINYPY_CODEGEN_VISIT_SEQUENCE_IN_SCOPE(c, stmt,
                                               mod->v.Interactive.body);
        break;
    case TINYPY_AST_KIND_EXPRESSION:
        TINYPY_CODEGEN_VISIT_IN_SCOPE(c, expr, mod->v.Expression.body);
        addNone = 0;
        break;
    case TINYPY_AST_KIND_SUITE:
        TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                       "suite should not be possible");
        __tinypy_codegen_exit_scope(c);
        return 0;
    default:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "module kind %d should not be possible",
                                   mod->kind);
        __tinypy_codegen_exit_scope(c);
        return 0;
    }
    tinypy_code_object_t *co = __tinypy_assembler_build(c, addNone);
    __tinypy_codegen_exit_scope(c);
    return co;
}
/* The test for TINYPY_SYMBOL_SCOPE_LOCAL must come before the test for TINYPY_SYMBOL_SCOPE_FREE in order to
   handle classes where name is both local and free.  The local var is
   a method and the free var is a free var referenced within a method.
*/

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codegen_reference_type(tinypy_codegen_t *c, tinypy_value_t *name) {
    int32_t return_value_1 = __tinypy_symbol_table_scope(c->u->u_ste, name);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codegen_lookup_arg(tinypy_value_t *dict, tinypy_value_t *name) {
    tinypy_value_t *k, *v;
    k = __tinypy_bytecode_constant_key(name);
    if (k == NULL) {
        return -1;
    }
    v = TINYPY_COMPILER_DICT_GET_ITEM(dict, k);
    TINYPY_COMPILER_DECREF(k);
    if (v == NULL) {
        return -1;
    }
    int32_t return_value_1 = (int32_t)TINYPY_COMPILER_INT_AS_LONG(v);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_make_closure(tinypy_codegen_t *c, tinypy_code_object_t *co, int32_t args) {
    int32_t i, free = __tinypy_bytecode_free_variable_count(co);
    if (free == 0) {
        TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, (tinypy_value_t *)co, consts);
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_MAKE_FUNCTION, args);
        return TINYPY_TRUE;
    }
    for (i = 0; i < free; ++i) {
        /* Bypass com_addop_varname because it will generate
           TINYPY_OP_LOAD_DEREF but TINYPY_OP_LOAD_CLOSURE is needed.
        */
        tinypy_value_t *name = TINYPY_COMPILER_TUPLE_GET_ITEM(co->freevars, i);
        int32_t arg, reftype;

        /* Special case: If a class contains a method with a
           free variable that has the same name as a method,
           the name will be considered free *and* local in the
           class.  It should be handled by the closure, as
           well as by the normal name loookup logic.
        */
        reftype = __tinypy_codegen_reference_type(c, name);
        if (reftype == TINYPY_SYMBOL_SCOPE_CELL) {
            arg = __tinypy_codegen_lookup_arg(c->u->u_cellvars, name);
        }
        else /* (reftype == TINYPY_SYMBOL_SCOPE_FREE) */ {
            arg = __tinypy_codegen_lookup_arg(c->u->u_freevars, name);
        }
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_LOAD_CLOSURE, arg);
    }
    TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_BUILD_TUPLE, free);
    TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, (tinypy_value_t *)co, consts);
    TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_MAKE_CLOSURE, args);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_decorators(tinypy_codegen_t *c, tinypy_ast_sequence_t *decos) {
    int32_t i;

    if (!decos) {
        return TINYPY_TRUE;
    }

    for (i = 0; i < TINYPY_AST_SEQUENCE_LENGTH(decos); i++) {
        TINYPY_CODEGEN_VISIT(c, expr, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(decos, i));
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_arguments(tinypy_codegen_t *c, tinypy_ast_arguments_t args) {
    int32_t i;
    int32_t n = TINYPY_AST_SEQUENCE_LENGTH(args->args);
    /* Correctly handle nested argument lists */
    for (i = 0; i < n; i++) {
        tinypy_ast_expression_t arg = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(args->args, i);
        if (arg->kind == TINYPY_AST_KIND_TUPLE) {
            tinypy_value_t *id = __tinypy_frontend_format_identifier(c->c_module_name, ".", i, "");
            if (id == NULL) {
                return TINYPY_FALSE;
            }
            if (!__tinypy_codegen_nameop(c, id, TINYPY_AST_CONTEXT_LOAD)) {
                TINYPY_COMPILER_DECREF(id);
                return TINYPY_FALSE;
            }
            TINYPY_COMPILER_DECREF(id);
            TINYPY_CODEGEN_VISIT(c, expr, arg);
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_function(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    tinypy_value_t *first_const = c->c_none;
    tinypy_ast_arguments_t args = s->v.FunctionDef.args;
    tinypy_ast_sequence_t *decos = s->v.FunctionDef.decorator_list;
    tinypy_ast_statement_t st;
    int32_t i, n, docstring;

    if (!__tinypy_codegen_decorators(c, decos)) {
        return TINYPY_FALSE;
    }
    if (args->defaults) {
        TINYPY_CODEGEN_VISIT_SEQUENCE(c, expr, args->defaults);
    }
    if (!__tinypy_codegen_enter_scope(c, s->v.FunctionDef.name, (void *)s,
                                      s->lineno)) {
        return TINYPY_FALSE;
    }

    st = (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(s->v.FunctionDef.body, 0);
    docstring = __tinypy_codegen_isdocstring(st);
    if (docstring && c->c_optimize < 2) {
        first_const = st->v.Expr.value->v.Str.s;
    }
    if (__tinypy_codegen_add_o(c, c->u->u_consts, first_const) < 0) {
        __tinypy_codegen_exit_scope(c);
        return TINYPY_FALSE;
    }

    /* unpack nested arguments */
    __tinypy_codegen_arguments(c, args);

    c->u->u_argcount = TINYPY_AST_SEQUENCE_LENGTH(args->args);
    n = TINYPY_AST_SEQUENCE_LENGTH(s->v.FunctionDef.body);
    /* if there was a docstring, we need to skip the first statement */
    for (i = docstring; i < n; i++) {
        st = (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(s->v.FunctionDef.body, i);
        TINYPY_CODEGEN_VISIT_IN_SCOPE(c, stmt, st);
    }
    tinypy_code_object_t *co = __tinypy_assembler_build(c, 1);
    __tinypy_codegen_exit_scope(c);
    if (co == NULL) {
        return TINYPY_FALSE;
    }

    __tinypy_codegen_make_closure(c, co, TINYPY_AST_SEQUENCE_LENGTH(args->defaults));
    TINYPY_COMPILER_DECREF(co);

    for (i = 0; i < TINYPY_AST_SEQUENCE_LENGTH(decos); i++) {
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_CALL_FUNCTION, 1);
    }

    tinypy_bool_t return_value_1 = __tinypy_codegen_nameop(c, s->v.FunctionDef.name, TINYPY_AST_CONTEXT_STORE);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_class(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    int32_t n, i;
    tinypy_ast_sequence_t *decos = s->v.ClassDef.decorator_list;

    if (!__tinypy_codegen_decorators(c, decos)) {
        return TINYPY_FALSE;
    }

    /* push class name on stack, needed by TINYPY_OP_BUILD_CLASS */
    TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, s->v.ClassDef.name, consts);
    /* push the tuple of base classes on the stack */
    n = TINYPY_AST_SEQUENCE_LENGTH(s->v.ClassDef.bases);
    if (n > 0) {
        TINYPY_CODEGEN_VISIT_SEQUENCE(c, expr, s->v.ClassDef.bases);
    }
    TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_BUILD_TUPLE, n);
    if (!__tinypy_codegen_enter_scope(c, s->v.ClassDef.name, (void *)s,
                                      s->lineno)) {
        return TINYPY_FALSE;
    }
    TINYPY_COMPILER_INCREF(s->v.ClassDef.name);
    TINYPY_COMPILER_XSETREF(c->u->u_private, s->v.ClassDef.name);
    tinypy_value_t *str = __tinypy_frontend_string_from_owner(c->c_module_name, "__name__", 8U);
    if (!str || !__tinypy_codegen_nameop(c, str, TINYPY_AST_CONTEXT_LOAD)) {
        TINYPY_COMPILER_XDECREF(str);
        __tinypy_codegen_exit_scope(c);
        return TINYPY_FALSE;
    }

    TINYPY_COMPILER_DECREF(str);
    str = __tinypy_frontend_string_from_owner(c->c_module_name, "__module__", 10U);
    if (!str || !__tinypy_codegen_nameop(c, str, TINYPY_AST_CONTEXT_STORE)) {
        TINYPY_COMPILER_XDECREF(str);
        __tinypy_codegen_exit_scope(c);
        return TINYPY_FALSE;
    }
    TINYPY_COMPILER_DECREF(str);

    if (!__tinypy_codegen_body(c, s->v.ClassDef.body)) {
        __tinypy_codegen_exit_scope(c);
        return TINYPY_FALSE;
    }

    TINYPY_CODEGEN_ADD_OPCODE_IN_SCOPE(c, TINYPY_OP_LOAD_LOCALS);
    TINYPY_CODEGEN_ADD_OPCODE_IN_SCOPE(c, TINYPY_OP_RETURN_VALUE);
    tinypy_code_object_t *co = __tinypy_assembler_build(c, 1);
    __tinypy_codegen_exit_scope(c);
    if (co == NULL) {
        return TINYPY_FALSE;
    }

    __tinypy_codegen_make_closure(c, co, 0);
    TINYPY_COMPILER_DECREF(co);

    TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_CALL_FUNCTION, 0);
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_BUILD_CLASS);
    /* apply decorators */
    for (i = 0; i < TINYPY_AST_SEQUENCE_LENGTH(decos); i++) {
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_CALL_FUNCTION, 1);
    }
    if (!__tinypy_codegen_nameop(c, s->v.ClassDef.name, TINYPY_AST_CONTEXT_STORE)) {
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_ifexp(tinypy_codegen_t *c, tinypy_ast_expression_t e) {
    tinypy_codegen_block_t *end, *next;

    end = __tinypy_codegen_new_block(c);
    if (end == NULL) {
        return TINYPY_FALSE;
    }
    next = __tinypy_codegen_new_block(c);
    if (next == NULL) {
        return TINYPY_FALSE;
    }
    TINYPY_CODEGEN_VISIT(c, expr, e->v.IfExp.test);
    TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_POP_JUMP_IF_FALSE, next);
    TINYPY_CODEGEN_VISIT(c, expr, e->v.IfExp.body);
    TINYPY_CODEGEN_ADD_RELATIVE_JUMP(c, TINYPY_OP_JUMP_FORWARD, end);
    __tinypy_codegen_use_next_block(c, next);
    TINYPY_CODEGEN_VISIT(c, expr, e->v.IfExp.orelse);
    __tinypy_codegen_use_next_block(c, end);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_lambda(tinypy_codegen_t *c, tinypy_ast_expression_t e) {
    tinypy_ast_arguments_t args = e->v.Lambda.args;

    if (args->defaults) {
        TINYPY_CODEGEN_VISIT_SEQUENCE(c, expr, args->defaults);
    }
    if (!__tinypy_codegen_enter_scope(c, c->c_lambda_name, (void *)e, e->lineno)) {
        return TINYPY_FALSE;
    }

    /* unpack nested arguments */
    __tinypy_codegen_arguments(c, args);

    /* Make None the first constant, so the lambda can't have a
       docstring. */
    if (__tinypy_codegen_add_o(c, c->u->u_consts, c->c_none) < 0) {
        return TINYPY_FALSE;
    }

    c->u->u_argcount = TINYPY_AST_SEQUENCE_LENGTH(args->args);
    TINYPY_CODEGEN_VISIT_IN_SCOPE(c, expr, e->v.Lambda.body);
    if (c->u->u_ste->generator) {
        TINYPY_CODEGEN_ADD_OPCODE_IN_SCOPE(c, TINYPY_OP_POP_TOP);
    }
    else {
        TINYPY_CODEGEN_ADD_OPCODE_IN_SCOPE(c, TINYPY_OP_RETURN_VALUE);
    }
    tinypy_code_object_t *co = __tinypy_assembler_build(c, 1);
    __tinypy_codegen_exit_scope(c);
    if (co == NULL) {
        return TINYPY_FALSE;
    }

    __tinypy_codegen_make_closure(c, co, TINYPY_AST_SEQUENCE_LENGTH(args->defaults));
    TINYPY_COMPILER_DECREF(co);

    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_print(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    int32_t i, n;
    tinypy_compiler_boolean_e dest;

    n = TINYPY_AST_SEQUENCE_LENGTH(s->v.Print.values);
    dest = TINYPY_COMPILER_FALSE;
    if (s->v.Print.dest) {
        TINYPY_CODEGEN_VISIT(c, expr, s->v.Print.dest);
        dest = TINYPY_COMPILER_TRUE;
    }
    for (i = 0; i < n; i++) {
        tinypy_ast_expression_t e = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(s->v.Print.values, i);
        if (dest) {
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_DUP_TOP);
            TINYPY_CODEGEN_VISIT(c, expr, e);
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_ROT_TWO);
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_PRINT_ITEM_TO);
        }
        else {
            TINYPY_CODEGEN_VISIT(c, expr, e);
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_PRINT_ITEM);
        }
    }
    if (s->v.Print.nl) {
        if (dest) {
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_PRINT_NEWLINE_TO)
        }
        else {
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_PRINT_NEWLINE)
        }
    }
    else if (dest) {
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_TOP);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_if(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    tinypy_codegen_block_t *end, *next;
    int32_t constant;
    end = __tinypy_codegen_new_block(c);
    if (end == NULL) {
        return TINYPY_FALSE;
    }

    constant = __tinypy_codegen_expression_constant(c, s->v.If.test);
    /* constant = 0: "if 0"
     * constant = 1: "if 1", "if 2", ...
     * constant = -1: rest */
    if (constant == 0) {
        if (s->v.If.orelse) {
            TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.If.orelse);
        }
    }
    else if (constant == 1) {
        TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.If.body);
    }
    else {
        if (s->v.If.orelse) {
            next = __tinypy_codegen_new_block(c);
            if (next == NULL) {
                return TINYPY_FALSE;
            }
        }
        else {
            next = end;
        }
        TINYPY_CODEGEN_VISIT(c, expr, s->v.If.test);
        TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_POP_JUMP_IF_FALSE, next);
        TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.If.body);
        TINYPY_CODEGEN_ADD_RELATIVE_JUMP(c, TINYPY_OP_JUMP_FORWARD, end);
        if (s->v.If.orelse) {
            __tinypy_codegen_use_next_block(c, next);
            TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.If.orelse);
        }
    }
    __tinypy_codegen_use_next_block(c, end);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_for(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    tinypy_codegen_block_t *start, *cleanup, *end;

    start = __tinypy_codegen_new_block(c);
    cleanup = __tinypy_codegen_new_block(c);
    end = __tinypy_codegen_new_block(c);
    if (start == NULL || end == NULL || cleanup == NULL) {
        return TINYPY_FALSE;
    }
    TINYPY_CODEGEN_ADD_RELATIVE_JUMP(c, TINYPY_OP_SETUP_LOOP, end);
    if (!__tinypy_codegen_push_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_LOOP, start)) {
        return TINYPY_FALSE;
    }
    TINYPY_CODEGEN_VISIT(c, expr, s->v.For.iter);
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_GET_ITER);
    __tinypy_codegen_use_next_block(c, start);
    TINYPY_CODEGEN_ADD_RELATIVE_JUMP(c, TINYPY_OP_FOR_ITER, cleanup);
    TINYPY_CODEGEN_VISIT(c, expr, s->v.For.target);
    TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.For.body);
    TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_JUMP_ABSOLUTE, start);
    __tinypy_codegen_use_next_block(c, cleanup);
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_BLOCK);
    __tinypy_codegen_pop_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_LOOP, start);
    TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.For.orelse);
    __tinypy_codegen_use_next_block(c, end);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_while(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    tinypy_codegen_block_t *loop, *orelse, *end, *anchor = NULL;
    int32_t constant = __tinypy_codegen_expression_constant(c, s->v.While.test);

    if (constant == 0) {
        if (s->v.While.orelse) {
            TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.While.orelse);
        }
        return TINYPY_TRUE;
    }
    loop = __tinypy_codegen_new_block(c);
    end = __tinypy_codegen_new_block(c);
    if (constant == -1) {
        anchor = __tinypy_codegen_new_block(c);
        if (anchor == NULL) {
            return TINYPY_FALSE;
        }
    }
    if (loop == NULL || end == NULL) {
        return TINYPY_FALSE;
    }
    if (s->v.While.orelse) {
        orelse = __tinypy_codegen_new_block(c);
        if (orelse == NULL) {
            return TINYPY_FALSE;
        }
    }
    else {
        orelse = NULL;
    }

    TINYPY_CODEGEN_ADD_RELATIVE_JUMP(c, TINYPY_OP_SETUP_LOOP, end);
    __tinypy_codegen_use_next_block(c, loop);
    if (!__tinypy_codegen_push_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_LOOP, loop)) {
        return TINYPY_FALSE;
    }
    if (constant == -1) {
        TINYPY_CODEGEN_VISIT(c, expr, s->v.While.test);
        TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_POP_JUMP_IF_FALSE, anchor);
    }
    TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.While.body);
    TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_JUMP_ABSOLUTE, loop);

    /* XXX should the two POP instructions be in a separate block
       if there is no else clause ?
    */

    if (constant == -1) {
        __tinypy_codegen_use_next_block(c, anchor);
    }
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_BLOCK);
    __tinypy_codegen_pop_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_LOOP, loop);
    if (orelse != NULL) /* what if orelse is just pass? */ {
        TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.While.orelse);
    }
    __tinypy_codegen_use_next_block(c, end);

    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_continue(tinypy_codegen_t *c) {
    tinypy_bool_t function_result;
    static const char LOOP_ERROR_MSG[] = "'continue' not properly in loop";
    static const char IN_FINALLY_ERROR_MSG[] =
        "'continue' not supported inside 'finally' clause";
    int32_t i;

    if (!c->u->u_nfblocks) {
        tinypy_bool_t return_value_1 = __tinypy_codegen_error(c, LOOP_ERROR_MSG);
        return return_value_1;
    }
    i = c->u->u_nfblocks - 1;
    switch (c->u->u_fblock[i].fb_type) {
    case TINYPY_CODEGEN_FRAME_BLOCK_LOOP:
        TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_JUMP_ABSOLUTE, c->u->u_fblock[i].fb_block);
        break;
    case TINYPY_CODEGEN_FRAME_BLOCK_EXCEPT:
    case TINYPY_CODEGEN_FRAME_BLOCK_FINALLY_TRY:
        while (--i >= 0 && c->u->u_fblock[i].fb_type != TINYPY_CODEGEN_FRAME_BLOCK_LOOP) {
            /* Prevent continue anywhere under a finally
                  even if hidden in a sub-try or except. */
            if (c->u->u_fblock[i].fb_type == TINYPY_CODEGEN_FRAME_BLOCK_FINALLY_END) {
                tinypy_bool_t return_value_2 = __tinypy_codegen_error(c, IN_FINALLY_ERROR_MSG);
                return return_value_2;
            }
        }
        if (i == -1) {
            tinypy_bool_t return_value_3 = __tinypy_codegen_error(c, LOOP_ERROR_MSG);
            return return_value_3;
        }
        TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_CONTINUE_LOOP, c->u->u_fblock[i].fb_block);
        break;
    case TINYPY_CODEGEN_FRAME_BLOCK_FINALLY_END:
        function_result = __tinypy_codegen_error(c, IN_FINALLY_ERROR_MSG);
        return function_result;
    }

    return TINYPY_TRUE;
}
/* Code generated for "try: <body> finally: <finalbody>" is as follows:

        TINYPY_OP_SETUP_FINALLY           L
        <code for body>
        TINYPY_OP_POP_BLOCK
        TINYPY_OP_LOAD_CONST              <None>
    L:          <code for finalbody>
        TINYPY_OP_END_FINALLY

   The special instructions use the block stack.  Each block
   stack entry contains the instruction that created it (here
   TINYPY_OP_SETUP_FINALLY), the level of the value stack at the time the
   block stack entry was created, and a label (here L).

   TINYPY_OP_SETUP_FINALLY:
    Pushes the current value stack level and the label
    onto the block stack.
   TINYPY_OP_POP_BLOCK:
    Pops en entry from the block stack, and pops the value
    stack until its level is the same as indicated on the
    block stack.  (The label is ignored.)
   TINYPY_OP_END_FINALLY:
    Pops a variable number of entries from the *value* stack
    and re-raises the exception they specify.  The number of
    entries popped depends on the (pseudo) exception type.

   The block stack is unwound when an exception is raised:
   when a TINYPY_OP_SETUP_FINALLY entry is found, the exception is pushed
   onto the value stack (and the exception condition is cleared),
   and the interpreter jumps to the label gotten from the block
   stack.
*/

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_try_finally(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    tinypy_codegen_block_t *body, *end;
    body = __tinypy_codegen_new_block(c);
    end = __tinypy_codegen_new_block(c);
    if (body == NULL || end == NULL) {
        return TINYPY_FALSE;
    }

    TINYPY_CODEGEN_ADD_RELATIVE_JUMP(c, TINYPY_OP_SETUP_FINALLY, end);
    __tinypy_codegen_use_next_block(c, body);
    if (!__tinypy_codegen_push_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_FINALLY_TRY, body)) {
        return TINYPY_FALSE;
    }
    TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.TryFinally.body);
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_BLOCK);
    __tinypy_codegen_pop_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_FINALLY_TRY, body);

    TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, c->c_none, consts);
    __tinypy_codegen_use_next_block(c, end);
    if (!__tinypy_codegen_push_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_FINALLY_END, end)) {
        return TINYPY_FALSE;
    }
    TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.TryFinally.finalbody);
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_END_FINALLY);
    __tinypy_codegen_pop_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_FINALLY_END, end);

    return TINYPY_TRUE;
}
/*
   Code generated for "try: S except E1, V1: S1 except E2, V2: S2 ...":
   (The contents of the value stack is shown in [], with the top
   at the right; 'tb' is trace-back info, 'val' the exception's
   associated value, and 'exc' the exception.)

   Value stack          Label   Instruction     Argument
   []                           TINYPY_OP_SETUP_EXCEPT    L1
   []                           <code for S>
   []                           TINYPY_OP_POP_BLOCK
   []                           TINYPY_OP_JUMP_FORWARD    L0

   [tb, val, exc]       L1:     DUP                             )
   [tb, val, exc, exc]          <evaluate E1>                   )
   [tb, val, exc, exc, E1]      TINYPY_OP_COMPARE_OP      EXC_MATCH       ) only if E1
   [tb, val, exc, 1-or-0]       TINYPY_OP_POP_JUMP_IF_FALSE       L2      )
   [tb, val, exc]               POP
   [tb, val]                    <assign to V1>  (or POP if no V1)
   [tb]                         POP
   []                           <code for S1>
                                TINYPY_OP_JUMP_FORWARD    L0

   [tb, val, exc]       L2:     DUP
   .............................etc.......................

   [tb, val, exc]       Ln+1:   TINYPY_OP_END_FINALLY     # re-raise exception

   []                   L0:     <next statement>

   Of course, parts are not generated if Vi or Ei is not present.
*/
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_try_except(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    tinypy_codegen_block_t *body, *orelse, *except, *end;
    int32_t i, n;

    body = __tinypy_codegen_new_block(c);
    except = __tinypy_codegen_new_block(c);
    orelse = __tinypy_codegen_new_block(c);
    end = __tinypy_codegen_new_block(c);
    if (body == NULL || except == NULL || orelse == NULL || end == NULL) {
        return TINYPY_FALSE;
    }
    TINYPY_CODEGEN_ADD_RELATIVE_JUMP(c, TINYPY_OP_SETUP_EXCEPT, except);
    __tinypy_codegen_use_next_block(c, body);
    if (!__tinypy_codegen_push_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_EXCEPT, body)) {
        return TINYPY_FALSE;
    }
    TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.TryExcept.body);
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_BLOCK);
    __tinypy_codegen_pop_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_EXCEPT, body);
    TINYPY_CODEGEN_ADD_RELATIVE_JUMP(c, TINYPY_OP_JUMP_FORWARD, orelse);
    n = TINYPY_AST_SEQUENCE_LENGTH(s->v.TryExcept.handlers);
    __tinypy_codegen_use_next_block(c, except);
    for (i = 0; i < n; i++) {
        tinypy_ast_exception_handler_t handler = (tinypy_ast_exception_handler_t)TINYPY_AST_SEQUENCE_GET(
            s->v.TryExcept.handlers, i);
        if (!handler->v.ExceptHandler.type && i < n - 1) {
            tinypy_bool_t return_value_1 = __tinypy_codegen_error(c, "default 'except:' must be last");
            return return_value_1;
        }
        c->u->u_lineno_set = TINYPY_COMPILER_FALSE;
        c->u->u_lineno = handler->lineno;
        except = __tinypy_codegen_new_block(c);
        if (except == NULL) {
            return TINYPY_FALSE;
        }
        if (handler->v.ExceptHandler.type) {
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_DUP_TOP);
            TINYPY_CODEGEN_VISIT(c, expr, handler->v.ExceptHandler.type);
            TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_COMPARE_OP, TINYPY_COMPARE_EXCEPTION_MATCH);
            TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_POP_JUMP_IF_FALSE, except);
        }
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_TOP);
        if (handler->v.ExceptHandler.name) {
            TINYPY_CODEGEN_VISIT(c, expr, handler->v.ExceptHandler.name);
        }
        else {
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_TOP);
        }
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_TOP);
        TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, handler->v.ExceptHandler.body);
        TINYPY_CODEGEN_ADD_RELATIVE_JUMP(c, TINYPY_OP_JUMP_FORWARD, end);
        __tinypy_codegen_use_next_block(c, except);
    }
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_END_FINALLY);
    __tinypy_codegen_use_next_block(c, orelse);
    TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.TryExcept.orelse);
    __tinypy_codegen_use_next_block(c, end);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_import_as(tinypy_codegen_t *c, tinypy_ast_identifier_t name, tinypy_ast_identifier_t asname) {
    /* The TINYPY_OP_IMPORT_NAME opcode was already generated.  This function
       merely needs to bind the result to a name.

       If there is a dot in name, we need to split it and emit a
       TINYPY_OP_LOAD_ATTR for each name.
    */
    const char *src = TINYPY_COMPILER_STRING_AS_STRING(name);
    const char *dot = strchr(src, '.');
    if (dot) {
        /* Consume the base module name to get the first attribute */
        src = dot + 1;
        while (dot) {
            /* NB src is only defined when dot != NULL */
            tinypy_value_t *attr;
            size_t source_size;

            dot = strchr(src, '.');
            source_size = dot != NULL ? (size_t)(dot - src) : strlen(src);
            attr = __tinypy_frontend_string_from_owner(name, src, source_size);
            if (!attr) {
                return TINYPY_FALSE;
            }
            TINYPY_CODEGEN_ADD_NEW_OBJECT_OPCODE(c, TINYPY_OP_LOAD_ATTR, attr, names);
            if (dot == NULL) {
                break;
            }
            src = dot + 1;
        }
    }
    tinypy_bool_t return_value_1 = __tinypy_codegen_nameop(c, asname, TINYPY_AST_CONTEXT_STORE);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_import(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    /* The Import tinypy_cst_node_t stores a module name like a.b.c as a single
       string.  This is convenient for all cases except
         import a.b.c as d
       where we need to parse that string to extract the individual
       module names.
       XXX Perhaps change the representation to make this case simpler?
     */
    int32_t i, n = TINYPY_AST_SEQUENCE_LENGTH(s->v.Import.names);

    for (i = 0; i < n; i++) {
        tinypy_ast_alias_t alias = (tinypy_ast_alias_t)TINYPY_AST_SEQUENCE_GET(s->v.Import.names, i);
        tinypy_bool_t r;
        tinypy_value_t *level;

        if (c->c_flags && (c->c_flags->flags & TINYPY_CODE_FUTURE_ABSOLUTE_IMPORT)) {
            level = __tinypy_frontend_integer_from_owner(alias->name, 0);
        }
        else {
            level = __tinypy_frontend_integer_from_owner(alias->name, -1);
        }

        if (level == NULL) {
            return TINYPY_FALSE;
        }

        TINYPY_CODEGEN_ADD_NEW_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, level, consts);
        TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, c->c_none, consts);
        TINYPY_CODEGEN_ADD_NAME_OPCODE(c, TINYPY_OP_IMPORT_NAME, alias->name, names);

        if (alias->asname) {
            r = __tinypy_codegen_import_as(c, alias->name, alias->asname);
            if (!r) {
                return r;
            }
        }
        else {
            tinypy_ast_identifier_t tmp = alias->name;
            const char *base = TINYPY_COMPILER_STRING_AS_STRING(alias->name);
            char *dot = strchr(base, '.');
            if (dot) {
                tmp = __tinypy_frontend_string_from_owner(alias->name, base,
                                                          (size_t)(dot - base));
                if (tmp == NULL) {
                    return TINYPY_FALSE;
                }
            }
            r = __tinypy_codegen_nameop(c, tmp, TINYPY_AST_CONTEXT_STORE);
            if (dot) {
                TINYPY_COMPILER_DECREF(tmp);
            }
            if (!r) {
                return r;
            }
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_from_import(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    int32_t i, n = TINYPY_AST_SEQUENCE_LENGTH(s->v.ImportFrom.names);

    tinypy_value_t *level, *names;

    if (s->v.ImportFrom.level == 0 && c->c_flags && !(c->c_flags->flags & TINYPY_CODE_FUTURE_ABSOLUTE_IMPORT)) {
        level = __tinypy_frontend_integer_from_owner(c->c_module_name, -1);
    }
    else {
        level = __tinypy_frontend_integer_from_owner(c->c_module_name, s->v.ImportFrom.level);
    }

    if (!level) {
        return TINYPY_FALSE;
    }
    TINYPY_CODEGEN_ADD_NEW_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, level, consts);

    names = __tinypy_frontend_tuple_new(c->c_module_name, n);
    if (!names) {
        return TINYPY_FALSE;
    }

    /* build up the names */
    for (i = 0; i < n; i++) {
        tinypy_ast_alias_t alias = (tinypy_ast_alias_t)TINYPY_AST_SEQUENCE_GET(s->v.ImportFrom.names, i);
        TINYPY_COMPILER_INCREF(alias->name);
        TINYPY_COMPILER_TUPLE_SET_ITEM(names, i, alias->name);
    }

    tinypy_bool_t condition_2 = s->lineno > c->c_future->line_number && s->v.ImportFrom.module;
    if (condition_2 != 0) {
        condition_2 = !strcmp(TINYPY_COMPILER_STRING_AS_STRING(s->v.ImportFrom.module), "__future__");
    }
    if (condition_2) {
        TINYPY_COMPILER_DECREF(names);
        tinypy_bool_t return_value_1 = __tinypy_codegen_error(c, "from __future__ imports must occur "
                                                 "at the beginning of the file");
        return return_value_1;
    }
    TINYPY_CODEGEN_ADD_NEW_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, names, consts);

    if (s->v.ImportFrom.module) {
        TINYPY_CODEGEN_ADD_NAME_OPCODE(c, TINYPY_OP_IMPORT_NAME, s->v.ImportFrom.module, names);
    }
    else {
        TINYPY_CODEGEN_ADD_NAME_OPCODE(c, TINYPY_OP_IMPORT_NAME, c->c_empty_string, names);
    }
    for (i = 0; i < n; i++) {
        tinypy_ast_alias_t alias = (tinypy_ast_alias_t)TINYPY_AST_SEQUENCE_GET(s->v.ImportFrom.names, i);
        tinypy_ast_identifier_t store_name;

        if (i == 0 && *TINYPY_COMPILER_STRING_AS_STRING(alias->name) == '*') {
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_IMPORT_STAR);
            return TINYPY_TRUE;
        }

        TINYPY_CODEGEN_ADD_NAME_OPCODE(c, TINYPY_OP_IMPORT_FROM, alias->name, names);
        store_name = alias->name;
        if (alias->asname) {
            store_name = alias->asname;
        }

        if (!__tinypy_codegen_nameop(c, store_name, TINYPY_AST_CONTEXT_STORE)) {
            return TINYPY_FALSE;
        }
    }
    /* remove imported module */
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_TOP);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_assert(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    if (c->c_optimize != 0) {
        return TINYPY_TRUE;
    }
    if (s->v.Assert.test->kind == TINYPY_AST_KIND_TUPLE && TINYPY_AST_SEQUENCE_LENGTH(s->v.Assert.test->v.Tuple.elts) > 0) {
        const char *msg =
            "assertion is always TINYPY_COMPILER_TRUE, perhaps remove parentheses?";
        if (TINYPY_COMPILER_ERR_WARN_EXPLICIT(TINYPY_COMPILER_EXC_SYNTAX_WARNING, msg, c->c_filename,
                                              c->u->u_lineno, NULL, NULL) == -1) {
            return TINYPY_FALSE;
        }
    }
    TINYPY_CODEGEN_VISIT(c, expr, s->v.Assert.test);
    tinypy_codegen_block_t *end = __tinypy_codegen_new_block(c);
    if (end == NULL) {
        return TINYPY_FALSE;
    }
    TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_POP_JUMP_IF_TRUE, end);
    TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_GLOBAL, c->c_assertion_error, names);
    if (s->v.Assert.msg) {
        TINYPY_CODEGEN_VISIT(c, expr, s->v.Assert.msg);
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_CALL_FUNCTION, 1);
    }
    TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_RAISE_VARARGS, 1);
    __tinypy_codegen_use_next_block(c, end);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_visit_stmt(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    tinypy_bool_t function_result;
    int32_t i, n;

    /* Always assign a lineno to the next instruction for a stmt. */
    c->u->u_lineno = s->lineno;
    c->u->u_lineno_set = TINYPY_COMPILER_FALSE;

    switch (s->kind) {
    case TINYPY_AST_KIND_FUNCTION_DEF:
        function_result = __tinypy_codegen_function(c, s);
        return function_result;
    case TINYPY_AST_KIND_CLASS_DEF:
        function_result = __tinypy_codegen_class(c, s);
        return function_result;
    case TINYPY_AST_KIND_RETURN:
        if (c->u->u_ste->block_type != TINYPY_SYMBOL_BLOCK_FUNCTION) {
            tinypy_bool_t return_value_1 = __tinypy_codegen_error(c, "'return' outside function");
            return return_value_1;
        }
        if (s->v.Return.value) {
            TINYPY_CODEGEN_VISIT(c, expr, s->v.Return.value);
        }
        else {
            TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, c->c_none, consts);
        }
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_RETURN_VALUE);
        break;
    case TINYPY_AST_KIND_DELETE:
        TINYPY_CODEGEN_VISIT_SEQUENCE(c, expr, s->v.Delete.targets)
        break;
    case TINYPY_AST_KIND_ASSIGN:
        n = TINYPY_AST_SEQUENCE_LENGTH(s->v.Assign.targets);
        TINYPY_CODEGEN_VISIT(c, expr, s->v.Assign.value);
        for (i = 0; i < n; i++) {
            if (i < n - 1) {
                TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_DUP_TOP);
            }
            TINYPY_CODEGEN_VISIT(c, expr,
                                 (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(s->v.Assign.targets, i));
        }
        break;
    case TINYPY_AST_KIND_AUG_ASSIGN:
        function_result = __tinypy_codegen_augassign(c, s);
        return function_result;
    case TINYPY_AST_KIND_PRINT:
        function_result = __tinypy_codegen_print(c, s);
        return function_result;
    case TINYPY_AST_KIND_FOR:
        function_result = __tinypy_codegen_for(c, s);
        return function_result;
    case TINYPY_AST_KIND_WHILE:
        function_result = __tinypy_codegen_while(c, s);
        return function_result;
    case TINYPY_AST_KIND_IF:
        function_result = __tinypy_codegen_if(c, s);
        return function_result;
    case TINYPY_AST_KIND_RAISE:
        n = 0;
        if (s->v.Raise.type) {
            TINYPY_CODEGEN_VISIT(c, expr, s->v.Raise.type);
            n++;
            if (s->v.Raise.inst) {
                TINYPY_CODEGEN_VISIT(c, expr, s->v.Raise.inst);
                n++;
                if (s->v.Raise.tback) {
                    TINYPY_CODEGEN_VISIT(c, expr, s->v.Raise.tback);
                    n++;
                }
            }
        }
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_RAISE_VARARGS, n);
        break;
    case TINYPY_AST_KIND_TRY_EXCEPT:
        function_result = __tinypy_codegen_try_except(c, s);
        return function_result;
    case TINYPY_AST_KIND_TRY_FINALLY:
        function_result = __tinypy_codegen_try_finally(c, s);
        return function_result;
    case TINYPY_AST_KIND_ASSERT:
        function_result = __tinypy_codegen_assert(c, s);
        return function_result;
    case TINYPY_AST_KIND_IMPORT:
        function_result = __tinypy_codegen_import(c, s);
        return function_result;
    case TINYPY_AST_KIND_IMPORT_FROM:
        function_result = __tinypy_codegen_from_import(c, s);
        return function_result;
    case TINYPY_AST_KIND_EXEC:
        TINYPY_CODEGEN_VISIT(c, expr, s->v.Exec.body);
        if (s->v.Exec.globals) {
            TINYPY_CODEGEN_VISIT(c, expr, s->v.Exec.globals);
            if (s->v.Exec.locals) {
                TINYPY_CODEGEN_VISIT(c, expr, s->v.Exec.locals);
            }
            else {
                TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_DUP_TOP);
            }
        }
        else {
            TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, c->c_none, consts);
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_DUP_TOP);
        }
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_EXEC_STMT);
        break;
    case TINYPY_AST_KIND_GLOBAL:
        break;
    case TINYPY_AST_KIND_EXPR:
        if (c->c_interactive && c->c_nestlevel <= 1) {
            TINYPY_CODEGEN_VISIT(c, expr, s->v.Expr.value);
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_PRINT_EXPR);
        }
        else if (s->v.Expr.value->kind != TINYPY_AST_KIND_STR && s->v.Expr.value->kind != TINYPY_AST_KIND_NUM) {
            TINYPY_CODEGEN_VISIT(c, expr, s->v.Expr.value);
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_TOP);
        }
        break;
    case TINYPY_AST_KIND_PASS:
        break;
    case TINYPY_AST_KIND_BREAK:
        if (!__tinypy_codegen_in_loop(c)) {
            tinypy_bool_t return_value_2 = __tinypy_codegen_error(c, "'break' outside loop");
            return return_value_2;
        }
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_BREAK_LOOP);
        break;
    case TINYPY_AST_KIND_CONTINUE:
        function_result = __tinypy_codegen_continue(c);
        return function_result;
    case TINYPY_AST_KIND_WITH:
        function_result = __tinypy_codegen_with(c, s);
        return function_result;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codegen_unary_operator(tinypy_ast_unary_operator_e op) {
    switch (op) {
    case TINYPY_AST_UNARY_INVERT:
        return TINYPY_OP_UNARY_INVERT;
    case TINYPY_AST_UNARY_NOT:
        return TINYPY_OP_UNARY_NOT;
    case TINYPY_AST_UNARY_ADD:
        return TINYPY_OP_UNARY_POSITIVE;
    case TINYPY_AST_UNARY_SUBTRACT:
        return TINYPY_OP_UNARY_NEGATIVE;
    default:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "unary op %d should not be possible", op);
        return 0;
    }
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codegen_binary_operator(tinypy_codegen_t *c, tinypy_ast_binary_operator_e op) {
    switch (op) {
    case TINYPY_AST_BINARY_ADD:
        return TINYPY_OP_BINARY_ADD;
    case TINYPY_AST_BINARY_SUBTRACT:
        return TINYPY_OP_BINARY_SUBTRACT;
    case TINYPY_AST_BINARY_MULTIPLY:
        return TINYPY_OP_BINARY_MULTIPLY;
    case TINYPY_AST_BINARY_DIVIDE:
        if (c->c_flags && c->c_flags->flags & TINYPY_CODE_FUTURE_DIVISION) {
            return TINYPY_OP_BINARY_TRUE_DIVIDE;
        }
        else {
            return TINYPY_OP_BINARY_DIVIDE;
        }
    case TINYPY_AST_BINARY_MODULO:
        return TINYPY_OP_BINARY_MODULO;
    case TINYPY_AST_BINARY_POWER:
        return TINYPY_OP_BINARY_POWER;
    case TINYPY_AST_BINARY_LEFT_SHIFT:
        return TINYPY_OP_BINARY_LSHIFT;
    case TINYPY_AST_BINARY_RIGHT_SHIFT:
        return TINYPY_OP_BINARY_RSHIFT;
    case TINYPY_AST_BINARY_BIT_OR:
        return TINYPY_OP_BINARY_OR;
    case TINYPY_AST_BINARY_BIT_XOR:
        return TINYPY_OP_BINARY_XOR;
    case TINYPY_AST_BINARY_BIT_AND:
        return TINYPY_OP_BINARY_AND;
    case TINYPY_AST_BINARY_FLOOR_DIVIDE:
        return TINYPY_OP_BINARY_FLOOR_DIVIDE;
    default:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "binary op %d should not be possible", op);
        return 0;
    }
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codegen_compare_operator(tinypy_ast_compare_operator_e op) {
    switch (op) {
    case TINYPY_AST_COMPARE_EQUAL:
        return TINYPY_COMPARE_EQUAL;
    case TINYPY_AST_COMPARE_NOT_EQUAL:
        return TINYPY_COMPARE_NOT_EQUAL;
    case TINYPY_AST_COMPARE_LESS:
        return TINYPY_COMPARE_LESS;
    case TINYPY_AST_COMPARE_LESS_EQUAL:
        return TINYPY_COMPARE_LESS_EQUAL;
    case TINYPY_AST_COMPARE_GREATER:
        return TINYPY_COMPARE_GREATER;
    case TINYPY_AST_COMPARE_GREATER_EQUAL:
        return TINYPY_COMPARE_GREATER_EQUAL;
    case TINYPY_AST_COMPARE_IS:
        return TINYPY_COMPARE_IS;
    case TINYPY_AST_COMPARE_IS_NOT:
        return TINYPY_COMPARE_IS_NOT;
    case TINYPY_AST_COMPARE_IN:
        return TINYPY_COMPARE_IN;
    case TINYPY_AST_COMPARE_NOT_IN:
        return TINYPY_COMPARE_NOT_IN;
    default:
        return TINYPY_COMPILER_COMPARE_INVALID;
    }
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codegen_inplace_binary_operator(tinypy_codegen_t *c, tinypy_ast_binary_operator_e op) {
    switch (op) {
    case TINYPY_AST_BINARY_ADD:
        return TINYPY_OP_INPLACE_ADD;
    case TINYPY_AST_BINARY_SUBTRACT:
        return TINYPY_OP_INPLACE_SUBTRACT;
    case TINYPY_AST_BINARY_MULTIPLY:
        return TINYPY_OP_INPLACE_MULTIPLY;
    case TINYPY_AST_BINARY_DIVIDE:
        if (c->c_flags && c->c_flags->flags & TINYPY_CODE_FUTURE_DIVISION) {
            return TINYPY_OP_INPLACE_TRUE_DIVIDE;
        }
        else {
            return TINYPY_OP_INPLACE_DIVIDE;
        }
    case TINYPY_AST_BINARY_MODULO:
        return TINYPY_OP_INPLACE_MODULO;
    case TINYPY_AST_BINARY_POWER:
        return TINYPY_OP_INPLACE_POWER;
    case TINYPY_AST_BINARY_LEFT_SHIFT:
        return TINYPY_OP_INPLACE_LSHIFT;
    case TINYPY_AST_BINARY_RIGHT_SHIFT:
        return TINYPY_OP_INPLACE_RSHIFT;
    case TINYPY_AST_BINARY_BIT_OR:
        return TINYPY_OP_INPLACE_OR;
    case TINYPY_AST_BINARY_BIT_XOR:
        return TINYPY_OP_INPLACE_XOR;
    case TINYPY_AST_BINARY_BIT_AND:
        return TINYPY_OP_INPLACE_AND;
    case TINYPY_AST_BINARY_FLOOR_DIVIDE:
        return TINYPY_OP_INPLACE_FLOOR_DIVIDE;
    default:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "inplace binary op %d should not be possible", op);
        return 0;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_nameop(tinypy_codegen_t *c, tinypy_ast_identifier_t name, tinypy_ast_expression_context_e ctx) {
    int32_t op, scope, arg;
    enum {
        OP_FAST,
        OP_GLOBAL,
        OP_DEREF,
        OP_NAME
    } optype;

    tinypy_value_t *dict = c->u->u_names;
    /* XXX TINYPY_AST_CONTEXT_AUGMENTED_STORE isn't used anywhere! */

    tinypy_value_t *mangled = __tinypy_frontend_mangle(c->c_arena, c->u->u_private, name);
    if (!mangled) {
        return TINYPY_FALSE;
    }

    op = 0;
    optype = OP_NAME;
    scope = __tinypy_symbol_table_scope(c->u->u_ste, mangled);
    switch (scope) {
    case TINYPY_SYMBOL_SCOPE_FREE:
        dict = c->u->u_freevars;
        optype = OP_DEREF;
        break;
    case TINYPY_SYMBOL_SCOPE_CELL:
        dict = c->u->u_cellvars;
        optype = OP_DEREF;
        break;
    case TINYPY_SYMBOL_SCOPE_LOCAL:
        if (c->u->u_ste->block_type == TINYPY_SYMBOL_BLOCK_FUNCTION) {
            optype = OP_FAST;
        }
        break;
    case TINYPY_SYMBOL_SCOPE_GLOBAL_IMPLICIT:
        if (c->u->u_ste->block_type == TINYPY_SYMBOL_BLOCK_FUNCTION && !c->u->u_ste->unoptimized) {
            optype = OP_GLOBAL;
        }
        break;
    case TINYPY_SYMBOL_SCOPE_GLOBAL_EXPLICIT:
        optype = OP_GLOBAL;
        break;
    default:
        /* scope can be 0 */
        break;
    }

    /* XXX Leave assert here, but handle __doc__ and the like better */

    switch (optype) {
    case OP_DEREF:
        switch (ctx) {
        case TINYPY_AST_CONTEXT_LOAD:
            op = TINYPY_OP_LOAD_DEREF;
            break;
        case TINYPY_AST_CONTEXT_STORE:
            op = TINYPY_OP_STORE_DEREF;
            break;
        case TINYPY_AST_CONTEXT_AUGMENTED_LOAD:
        case TINYPY_AST_CONTEXT_AUGMENTED_STORE:
            break;
        case TINYPY_AST_CONTEXT_DELETE: {
            static const char prefix[] = "can not delete variable '";
            static const char suffix[] = "' referenced in nested scope";
            size_t name_size = (size_t)TINYPY_COMPILER_STRING_GET_SIZE(name);
            size_t message_size;
            char *message;

            message_size = sizeof(prefix) - 1U + name_size + sizeof(suffix);
            message = (char *)TINYPY_COMPILER_ARENA_MALLOC(c->c_arena, message_size);
            if (message == NULL) {
                tinypy_internal_compiler_error(c->c_arena, TINYPY_ERROR_COMPILER_LIMIT,
                                               "compiler diagnostic exceeds arena limit",
                                               c->u->u_lineno, 1,
                                               c->c_arena->out_error);
            }
            else {
                (void)memcpy(message, prefix, sizeof(prefix) - 1U);
                (void)memcpy(message + sizeof(prefix) - 1U,
                             TINYPY_COMPILER_STRING_AS_STRING(name), name_size);
                (void)memcpy(message + sizeof(prefix) - 1U + name_size,
                             suffix, sizeof(suffix));
                tinypy_internal_compiler_error(c->c_arena, TINYPY_ERROR_SYNTAX,
                                               message, c->u->u_lineno, 1,
                                               c->c_arena->out_error);
            }
            TINYPY_COMPILER_DECREF(mangled);
            return TINYPY_FALSE;
        }
        case TINYPY_AST_CONTEXT_PARAMETER:
        default:
            TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                           "param invalid for deref variable");
            return TINYPY_FALSE;
        }
        break;
    case OP_FAST:
        switch (ctx) {
        case TINYPY_AST_CONTEXT_LOAD:
            op = TINYPY_OP_LOAD_FAST;
            break;
        case TINYPY_AST_CONTEXT_STORE:
            op = TINYPY_OP_STORE_FAST;
            break;
        case TINYPY_AST_CONTEXT_DELETE:
            op = TINYPY_OP_DELETE_FAST;
            break;
        case TINYPY_AST_CONTEXT_AUGMENTED_LOAD:
        case TINYPY_AST_CONTEXT_AUGMENTED_STORE:
            break;
        case TINYPY_AST_CONTEXT_PARAMETER:
        default:
            TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                           "param invalid for local variable");
            return TINYPY_FALSE;
        }
        TINYPY_CODEGEN_ADD_NEW_OBJECT_OPCODE(c, op, mangled, varnames);
        return TINYPY_TRUE;
    case OP_GLOBAL:
        switch (ctx) {
        case TINYPY_AST_CONTEXT_LOAD:
            op = TINYPY_OP_LOAD_GLOBAL;
            break;
        case TINYPY_AST_CONTEXT_STORE:
            op = TINYPY_OP_STORE_GLOBAL;
            break;
        case TINYPY_AST_CONTEXT_DELETE:
            op = TINYPY_OP_DELETE_GLOBAL;
            break;
        case TINYPY_AST_CONTEXT_AUGMENTED_LOAD:
        case TINYPY_AST_CONTEXT_AUGMENTED_STORE:
            break;
        case TINYPY_AST_CONTEXT_PARAMETER:
        default:
            TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                           "param invalid for global variable");
            return TINYPY_FALSE;
        }
        break;
    case OP_NAME:
        switch (ctx) {
        case TINYPY_AST_CONTEXT_LOAD:
            op = TINYPY_OP_LOAD_NAME;
            break;
        case TINYPY_AST_CONTEXT_STORE:
            op = TINYPY_OP_STORE_NAME;
            break;
        case TINYPY_AST_CONTEXT_DELETE:
            op = TINYPY_OP_DELETE_NAME;
            break;
        case TINYPY_AST_CONTEXT_AUGMENTED_LOAD:
        case TINYPY_AST_CONTEXT_AUGMENTED_STORE:
            break;
        case TINYPY_AST_CONTEXT_PARAMETER:
        default:
            TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                           "param invalid for name variable");
            return TINYPY_FALSE;
        }
        break;
    }

    arg = __tinypy_codegen_add_o(c, dict, mangled);
    TINYPY_COMPILER_DECREF(mangled);
    if (arg < 0) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = __tinypy_codegen_addop_i(c, op, arg);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_boolop(tinypy_codegen_t *c, tinypy_ast_expression_t e) {
    int32_t jumpi, i, n;

    if (e->v.BoolOp.op == TINYPY_AST_BOOLEAN_AND) {
        jumpi = TINYPY_OP_JUMP_IF_FALSE_OR_POP;
    }
    else {
        jumpi = TINYPY_OP_JUMP_IF_TRUE_OR_POP;
    }
    tinypy_codegen_block_t *end = __tinypy_codegen_new_block(c);
    if (end == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_ast_sequence_t *s = e->v.BoolOp.values;
    n = TINYPY_AST_SEQUENCE_LENGTH(s) - 1;
    for (i = 0; i < n; ++i) {
        TINYPY_CODEGEN_VISIT(c, expr, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(s, i));
        TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, jumpi, end);
    }
    TINYPY_CODEGEN_VISIT(c, expr, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(s, n));
    __tinypy_codegen_use_next_block(c, end);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_list(tinypy_codegen_t *c, tinypy_ast_expression_t e) {
    int32_t n = TINYPY_AST_SEQUENCE_LENGTH(e->v.List.elts);
    if (e->v.List.ctx == TINYPY_AST_CONTEXT_STORE) {
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_UNPACK_SEQUENCE, n);
    }
    TINYPY_CODEGEN_VISIT_SEQUENCE(c, expr, e->v.List.elts);
    if (e->v.List.ctx == TINYPY_AST_CONTEXT_LOAD) {
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_BUILD_LIST, n);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_tuple(tinypy_codegen_t *c, tinypy_ast_expression_t e) {
    int32_t n = TINYPY_AST_SEQUENCE_LENGTH(e->v.Tuple.elts);
    if (e->v.Tuple.ctx == TINYPY_AST_CONTEXT_STORE) {
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_UNPACK_SEQUENCE, n);
    }
    TINYPY_CODEGEN_VISIT_SEQUENCE(c, expr, e->v.Tuple.elts);
    if (e->v.Tuple.ctx == TINYPY_AST_CONTEXT_LOAD) {
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_BUILD_TUPLE, n);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_compare(tinypy_codegen_t *c, tinypy_ast_expression_t e) {
    int32_t i, n;
    tinypy_codegen_block_t *cleanup = NULL;

    /* XXX the logic can be cleaned up for 1 or multiple comparisons */
    TINYPY_CODEGEN_VISIT(c, expr, e->v.Compare.left);
    n = TINYPY_AST_SEQUENCE_LENGTH(e->v.Compare.ops);
    if (n > 1) {
        cleanup = __tinypy_codegen_new_block(c);
        if (cleanup == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_CODEGEN_VISIT(c, expr,
                             (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(e->v.Compare.comparators, 0));
    }
    for (i = 1; i < n; i++) {
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_DUP_TOP);
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_ROT_THREE);
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_COMPARE_OP,
                                          __tinypy_codegen_compare_operator((tinypy_ast_compare_operator_e)(TINYPY_AST_SEQUENCE_GET(
                                              e->v.Compare.ops, i - 1))));
        TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_JUMP_IF_FALSE_OR_POP, cleanup);
        TINYPY_CODEGEN_NEXT_BLOCK(c);
        if (i < (n - 1)) {
            TINYPY_CODEGEN_VISIT(c, expr,
                                 (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(e->v.Compare.comparators, i));
        }
    }
    TINYPY_CODEGEN_VISIT(c, expr, (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(e->v.Compare.comparators, n - 1));
    TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_COMPARE_OP,
                                      __tinypy_codegen_compare_operator((tinypy_ast_compare_operator_e)(TINYPY_AST_SEQUENCE_GET(e->v.Compare.ops, n - 1))));
    if (n > 1) {
        tinypy_codegen_block_t *end = __tinypy_codegen_new_block(c);
        if (end == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_CODEGEN_ADD_RELATIVE_JUMP(c, TINYPY_OP_JUMP_FORWARD, end);
        __tinypy_codegen_use_next_block(c, cleanup);
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_ROT_TWO);
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_TOP);
        __tinypy_codegen_use_next_block(c, end);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_call(tinypy_codegen_t *c, tinypy_ast_expression_t e) {
    int32_t n, code = 0;

    TINYPY_CODEGEN_VISIT(c, expr, e->v.Call.func);
    n = TINYPY_AST_SEQUENCE_LENGTH(e->v.Call.args);
    TINYPY_CODEGEN_VISIT_SEQUENCE(c, expr, e->v.Call.args);
    if (e->v.Call.keywords) {
        TINYPY_CODEGEN_VISIT_SEQUENCE(c, keyword, e->v.Call.keywords);
        n |= TINYPY_AST_SEQUENCE_LENGTH(e->v.Call.keywords) << 8;
    }
    if (e->v.Call.starargs) {
        TINYPY_CODEGEN_VISIT(c, expr, e->v.Call.starargs);
        code |= 1;
    }
    if (e->v.Call.kwargs) {
        TINYPY_CODEGEN_VISIT(c, expr, e->v.Call.kwargs);
        code |= 2;
    }
    switch (code) {
    case 0:
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_CALL_FUNCTION, n);
        break;
    case 1:
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_CALL_FUNCTION_VAR, n);
        break;
    case 2:
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_CALL_FUNCTION_KW, n);
        break;
    case 3:
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_CALL_FUNCTION_VAR_KW, n);
        break;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_listcomp_generator(tinypy_codegen_t *c, tinypy_ast_sequence_t *generators, int32_t gen_index, tinypy_ast_expression_t elt) {
    /* generate code for the iterator, then each of the ifs,
       and then write to the element */

    tinypy_ast_comprehension_t l;
    tinypy_codegen_block_t *start, *anchor, *skip, *if_cleanup;
    int32_t i, n;

    start = __tinypy_codegen_new_block(c);
    skip = __tinypy_codegen_new_block(c);
    if_cleanup = __tinypy_codegen_new_block(c);
    anchor = __tinypy_codegen_new_block(c);

    if (start == NULL || skip == NULL || if_cleanup == NULL || anchor == NULL) {
        return TINYPY_FALSE;
    }

    l = (tinypy_ast_comprehension_t)TINYPY_AST_SEQUENCE_GET(generators, gen_index);
    TINYPY_CODEGEN_VISIT(c, expr, l->iter);
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_GET_ITER);
    __tinypy_codegen_use_next_block(c, start);
    TINYPY_CODEGEN_ADD_RELATIVE_JUMP(c, TINYPY_OP_FOR_ITER, anchor);
    TINYPY_CODEGEN_NEXT_BLOCK(c);
    TINYPY_CODEGEN_VISIT(c, expr, l->target);

    /* XXX this needs to be cleaned up...a lot! */
    n = TINYPY_AST_SEQUENCE_LENGTH(l->ifs);
    for (i = 0; i < n; i++) {
        tinypy_ast_expression_t e = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(l->ifs, i);
        TINYPY_CODEGEN_VISIT(c, expr, e);
        TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_POP_JUMP_IF_FALSE, if_cleanup);
        TINYPY_CODEGEN_NEXT_BLOCK(c);
    }

    if (++gen_index < TINYPY_AST_SEQUENCE_LENGTH(generators)) {
        if (!__tinypy_codegen_listcomp_generator(c, generators, gen_index, elt)) {
            return TINYPY_FALSE;
        }
    }

    /* only append after the last for generator */
    if (gen_index >= TINYPY_AST_SEQUENCE_LENGTH(generators)) {
        TINYPY_CODEGEN_VISIT(c, expr, elt);
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_LIST_APPEND, gen_index + 1);

        __tinypy_codegen_use_next_block(c, skip);
    }
    __tinypy_codegen_use_next_block(c, if_cleanup);
    TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_JUMP_ABSOLUTE, start);
    __tinypy_codegen_use_next_block(c, anchor);

    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_listcomp(tinypy_codegen_t *c, tinypy_ast_expression_t e) {
    TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_BUILD_LIST, 0);
    tinypy_bool_t return_value_1 = __tinypy_codegen_listcomp_generator(c, e->v.ListComp.generators, 0,
                                                   e->v.ListComp.elt);
    return return_value_1;
}
/* Dict and set comprehensions and generator expressions work by creating a
   nested function to perform the actual iteration. This means that the
   iteration variables don't leak into the current scope.
   The defined function is called immediately following its definition, with the
   result of that call being the result of the expression.
   The LC/SC version returns the populated container, while the GE version is
   flagged in symbol_table.c as a generator, so it returns the generator object
   when the function is called.
   This code *knows* that the loop cannot contain break, continue, or return,
   so it cheats and skips the TINYPY_OP_SETUP_LOOP/TINYPY_OP_POP_BLOCK steps used in normal loops.

   Possible cleanups:
    - iterate over the generator sequence instead of using recursion
*/

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_comprehension_generator(tinypy_codegen_t *c, tinypy_ast_sequence_t *generators, int32_t gen_index, tinypy_ast_expression_t elt, tinypy_ast_expression_t val, int32_t type) {
    /* generate code for the iterator, then each of the ifs,
       and then write to the element */

    tinypy_ast_comprehension_t gen;
    tinypy_codegen_block_t *start, *anchor, *skip, *if_cleanup;
    int32_t i, n;

    start = __tinypy_codegen_new_block(c);
    skip = __tinypy_codegen_new_block(c);
    if_cleanup = __tinypy_codegen_new_block(c);
    anchor = __tinypy_codegen_new_block(c);

    if (start == NULL || skip == NULL || if_cleanup == NULL || anchor == NULL) {
        return TINYPY_FALSE;
    }

    gen = (tinypy_ast_comprehension_t)TINYPY_AST_SEQUENCE_GET(generators, gen_index);

    if (gen_index == 0) {
        /* Receive outermost iter as an implicit argument */
        c->u->u_argcount = 1;
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_LOAD_FAST, 0);
    }
    else {
        /* Sub-iterator: calculate on the fly. */
        TINYPY_CODEGEN_VISIT(c, expr, gen->iter);
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_GET_ITER);
    }
    __tinypy_codegen_use_next_block(c, start);
    TINYPY_CODEGEN_ADD_RELATIVE_JUMP(c, TINYPY_OP_FOR_ITER, anchor);
    TINYPY_CODEGEN_NEXT_BLOCK(c);
    TINYPY_CODEGEN_VISIT(c, expr, gen->target);

    /* XXX this needs to be cleaned up...a lot! */
    n = TINYPY_AST_SEQUENCE_LENGTH(gen->ifs);
    for (i = 0; i < n; i++) {
        tinypy_ast_expression_t e = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(gen->ifs, i);
        TINYPY_CODEGEN_VISIT(c, expr, e);
        TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_POP_JUMP_IF_FALSE, if_cleanup);
        TINYPY_CODEGEN_NEXT_BLOCK(c);
    }

    if (++gen_index < TINYPY_AST_SEQUENCE_LENGTH(generators)) {
        if (!__tinypy_codegen_comprehension_generator(c,
                                                      generators, gen_index,
                                                      elt, val, type)) {
            return TINYPY_FALSE;
        }
    }

    /* only append after the last for generator */
    if (gen_index >= TINYPY_AST_SEQUENCE_LENGTH(generators)) {
        /* comprehension specific code */
        switch (type) {
        case TINYPY_CODEGEN_COMPREHENSION_GENERATOR:
            TINYPY_CODEGEN_VISIT(c, expr, elt);
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_YIELD_VALUE);
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_TOP);
            break;
        case TINYPY_CODEGEN_COMPREHENSION_SET:
            TINYPY_CODEGEN_VISIT(c, expr, elt);
            TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_SET_ADD, gen_index + 1);
            break;
        case TINYPY_CODEGEN_COMPREHENSION_DICT:
            /* With 'd[k] = v', v is evaluated before k, so we do
               the same. */
            TINYPY_CODEGEN_VISIT(c, expr, val);
            TINYPY_CODEGEN_VISIT(c, expr, elt);
            TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_MAP_ADD, gen_index + 1);
            break;
        default:
            return TINYPY_FALSE;
        }

        __tinypy_codegen_use_next_block(c, skip);
    }
    __tinypy_codegen_use_next_block(c, if_cleanup);
    TINYPY_CODEGEN_ADD_ABSOLUTE_JUMP(c, TINYPY_OP_JUMP_ABSOLUTE, start);
    __tinypy_codegen_use_next_block(c, anchor);

    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_comprehension(tinypy_codegen_t *c, tinypy_ast_expression_t e, int32_t type, tinypy_ast_identifier_t name, tinypy_ast_sequence_t *generators, tinypy_ast_expression_t elt, tinypy_ast_expression_t val) {
    tinypy_code_object_t *co = NULL;
    tinypy_ast_expression_t outermost_iter;

    outermost_iter = ((tinypy_ast_comprehension_t)
                          TINYPY_AST_SEQUENCE_GET(generators, 0))
                         ->iter;

    if (!__tinypy_codegen_enter_scope(c, name, (void *)e, e->lineno)) {
        goto error;
    }

    if (type != TINYPY_CODEGEN_COMPREHENSION_GENERATOR) {
        int32_t op;
        switch (type) {
        case TINYPY_CODEGEN_COMPREHENSION_SET:
            op = TINYPY_OP_BUILD_SET;
            break;
        case TINYPY_CODEGEN_COMPREHENSION_DICT:
            op = TINYPY_OP_BUILD_MAP;
            break;
        default:
            TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                       "unknown comprehension type %d", type);
            goto error_in_scope;
        }

        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, op, 0);
    }

    if (!__tinypy_codegen_comprehension_generator(c, generators, 0, elt,
                                                  val, type)) {
        goto error_in_scope;
    }

    if (type != TINYPY_CODEGEN_COMPREHENSION_GENERATOR) {
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_RETURN_VALUE);
    }

    co = __tinypy_assembler_build(c, 1);
    __tinypy_codegen_exit_scope(c);
    if (co == NULL) {
        goto error;
    }

    if (!__tinypy_codegen_make_closure(c, co, 0)) {
        goto error;
    }
    TINYPY_COMPILER_DECREF(co);

    TINYPY_CODEGEN_VISIT(c, expr, outermost_iter);
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_GET_ITER);
    TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_CALL_FUNCTION, 1);
    return TINYPY_TRUE;
error_in_scope:
    __tinypy_codegen_exit_scope(c);
error:
    TINYPY_COMPILER_XDECREF(co);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_genexp(tinypy_codegen_t *c, tinypy_ast_expression_t e) {
    tinypy_bool_t return_value_1 = __tinypy_codegen_comprehension(c, e, TINYPY_CODEGEN_COMPREHENSION_GENERATOR, c->c_genexpr_name,
                                              e->v.GeneratorExp.generators,
                                              e->v.GeneratorExp.elt, NULL);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_setcomp(tinypy_codegen_t *c, tinypy_ast_expression_t e) {
    tinypy_bool_t return_value_1 = __tinypy_codegen_comprehension(c, e, TINYPY_CODEGEN_COMPREHENSION_SET, c->c_setcomp_name,
                                              e->v.SetComp.generators,
                                              e->v.SetComp.elt, NULL);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_dictcomp(tinypy_codegen_t *c, tinypy_ast_expression_t e) {
    tinypy_bool_t return_value_1 = __tinypy_codegen_comprehension(c, e, TINYPY_CODEGEN_COMPREHENSION_DICT, c->c_dictcomp_name,
                                              e->v.DictComp.generators,
                                              e->v.DictComp.key, e->v.DictComp.value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_visit_keyword(tinypy_codegen_t *c, tinypy_ast_keyword_t k) {
    TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, k->arg, consts);
    TINYPY_CODEGEN_VISIT(c, expr, k->value);
    return TINYPY_TRUE;
}
/* Test whether expression is constant.  For constants, report
   whether they are TINYPY_COMPILER_TRUE or TINYPY_COMPILER_FALSE.

   Return values: 1 for TINYPY_COMPILER_TRUE, 0 for TINYPY_COMPILER_FALSE, -1 for non-constant.
 */

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codegen_expression_constant(tinypy_codegen_t *c, tinypy_ast_expression_t e) {
    int32_t function_result;
    switch (e->kind) {
    case TINYPY_AST_KIND_NUM:
        function_result = TINYPY_COMPILER_OBJECT_IS_TRUE(e->v.Num.n);
        return function_result;
    case TINYPY_AST_KIND_STR:
        function_result = TINYPY_COMPILER_OBJECT_IS_TRUE(e->v.Str.s);
        return function_result;
    case TINYPY_AST_KIND_NAME:
        /* __debug__ is not assignable, so we can optimize
         * it away in if and while statements */
        if (strcmp(TINYPY_COMPILER_STRING_AS_STRING(e->v.Name.id),
                   "__debug__") == 0) {
            return c->c_optimize == 0;
        }
        /* fall through */
    default:
        return -1;
    }
}
/*
   Implements the with statement from PEP 343.

   The semantics outlined in that PEP are as follows:

   with EXPR as VAR:
       BLOCK

   It is implemented roughly as:

   context = EXPR
   exit = context.__exit__  # not calling it
   value = context.__enter__()
   try:
       VAR = value  # if VAR present in the syntax
       BLOCK
   finally:
       if an exception was raised:
           exc = copy of (exception, instance, traceback)
       else:
           exc = (None, None, None)
       exit(*exc)
 */
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_with(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    tinypy_codegen_block_t *block, *finally;

    block = __tinypy_codegen_new_block(c);
    finally = __tinypy_codegen_new_block(c);
    if (!block || !finally) {
        return TINYPY_FALSE;
    }

    /* Evaluate EXPR */
    TINYPY_CODEGEN_VISIT(c, expr, s->v.With.context_expr);
    TINYPY_CODEGEN_ADD_RELATIVE_JUMP(c, TINYPY_OP_SETUP_WITH, finally);

    /* TINYPY_OP_SETUP_WITH pushes a finally block. */
    __tinypy_codegen_use_next_block(c, block);
    /* Note that the block is actually called TINYPY_OP_SETUP_WITH in ceval.c, but
       functions the same as TINYPY_OP_SETUP_FINALLY except that exceptions are
       normalized. */
    if (!__tinypy_codegen_push_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_FINALLY_TRY, block)) {
        return TINYPY_FALSE;
    }

    if (s->v.With.optional_vars) {
        TINYPY_CODEGEN_VISIT(c, expr, s->v.With.optional_vars);
    }
    else {
        /* Discard result from context.__enter__() */
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_TOP);
    }

    /* BLOCK code */
    TINYPY_CODEGEN_VISIT_SEQUENCE(c, stmt, s->v.With.body);

    /* End of try block; start the finally block */
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_POP_BLOCK);
    __tinypy_codegen_pop_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_FINALLY_TRY, block);

    TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, c->c_none, consts);
    __tinypy_codegen_use_next_block(c, finally);
    if (!__tinypy_codegen_push_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_FINALLY_END, finally)) {
        return TINYPY_FALSE;
    }

    /* Finally block starts; context.__exit__ is on the stack under
       the exception or return information. Just issue our magic
       opcode. */
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_WITH_CLEANUP);

    /* Finally block ends. */
    TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_END_FINALLY);
    __tinypy_codegen_pop_fblock(c, TINYPY_CODEGEN_FRAME_BLOCK_FINALLY_END, finally);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_visit_expr(tinypy_codegen_t *c, tinypy_ast_expression_t e) {
    tinypy_bool_t function_result;
    int32_t i, n;

    /* If expr e has a different line number than the last expr/stmt,
       set a new line number for the next instruction.
    */
    if (e->lineno > c->u->u_lineno) {
        c->u->u_lineno = e->lineno;
        c->u->u_lineno_set = TINYPY_COMPILER_FALSE;
    }
    switch (e->kind) {
    case TINYPY_AST_KIND_BOOL_OP:
        function_result = __tinypy_codegen_boolop(c, e);
        return function_result;
    case TINYPY_AST_KIND_BIN_OP:
        TINYPY_CODEGEN_VISIT(c, expr, e->v.BinOp.left);
        TINYPY_CODEGEN_VISIT(c, expr, e->v.BinOp.right);
        TINYPY_CODEGEN_ADD_OPCODE(c, __tinypy_codegen_binary_operator(c, e->v.BinOp.op));
        break;
    case TINYPY_AST_KIND_UNARY_OP:
        TINYPY_CODEGEN_VISIT(c, expr, e->v.UnaryOp.operand);
        TINYPY_CODEGEN_ADD_OPCODE(c, __tinypy_codegen_unary_operator(e->v.UnaryOp.op));
        break;
    case TINYPY_AST_KIND_LAMBDA:
        function_result = __tinypy_codegen_lambda(c, e);
        return function_result;
    case TINYPY_AST_KIND_IF_EXP:
        function_result = __tinypy_codegen_ifexp(c, e);
        return function_result;
    case TINYPY_AST_KIND_DICT:
        n = TINYPY_AST_SEQUENCE_LENGTH(e->v.Dict.values);
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_BUILD_MAP, (n > 0xFFFF ? 0xFFFF : n));
        for (i = 0; i < n; i++) {
            TINYPY_CODEGEN_VISIT(c, expr,
                                 (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(e->v.Dict.values, i));
            TINYPY_CODEGEN_VISIT(c, expr,
                                 (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(e->v.Dict.keys, i));
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_STORE_MAP);
        }
        break;
    case TINYPY_AST_KIND_SET:
        n = TINYPY_AST_SEQUENCE_LENGTH(e->v.Set.elts);
        TINYPY_CODEGEN_VISIT_SEQUENCE(c, expr, e->v.Set.elts);
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_BUILD_SET, n);
        break;
    case TINYPY_AST_KIND_LIST_COMP:
        function_result = __tinypy_codegen_listcomp(c, e);
        return function_result;
    case TINYPY_AST_KIND_SET_COMP:
        function_result = __tinypy_codegen_setcomp(c, e);
        return function_result;
    case TINYPY_AST_KIND_DICT_COMP:
        function_result = __tinypy_codegen_dictcomp(c, e);
        return function_result;
    case TINYPY_AST_KIND_GENERATOR_EXP:
        function_result = __tinypy_codegen_genexp(c, e);
        return function_result;
    case TINYPY_AST_KIND_YIELD:
        if (c->u->u_ste->block_type != TINYPY_SYMBOL_BLOCK_FUNCTION) {
            tinypy_bool_t return_value_1 = __tinypy_codegen_error(c, "'yield' outside function");
            return return_value_1;
        }
        if (e->v.Yield.value) {
            TINYPY_CODEGEN_VISIT(c, expr, e->v.Yield.value);
        }
        else {
            TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, c->c_none, consts);
        }
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_YIELD_VALUE);
        break;
    case TINYPY_AST_KIND_COMPARE:
        function_result = __tinypy_codegen_compare(c, e);
        return function_result;
    case TINYPY_AST_KIND_CALL:
        function_result = __tinypy_codegen_call(c, e);
        return function_result;
    case TINYPY_AST_KIND_REPR:
        TINYPY_CODEGEN_VISIT(c, expr, e->v.Repr.value);
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_UNARY_CONVERT);
        break;
    case TINYPY_AST_KIND_NUM:
        TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, e->v.Num.n, consts);
        break;
    case TINYPY_AST_KIND_STR:
        TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, e->v.Str.s, consts);
        break;
    /* The following exprs can be assignment targets. */
    case TINYPY_AST_KIND_ATTRIBUTE:
        if (e->v.Attribute.ctx != TINYPY_AST_CONTEXT_AUGMENTED_STORE) {
            TINYPY_CODEGEN_VISIT(c, expr, e->v.Attribute.value);
        }
        switch (e->v.Attribute.ctx) {
        case TINYPY_AST_CONTEXT_AUGMENTED_LOAD:
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_DUP_TOP);
            /* Fall through to load */
        case TINYPY_AST_CONTEXT_LOAD:
            TINYPY_CODEGEN_ADD_NAME_OPCODE(c, TINYPY_OP_LOAD_ATTR, e->v.Attribute.attr, names);
            break;
        case TINYPY_AST_CONTEXT_AUGMENTED_STORE:
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_ROT_TWO);
            /* Fall through to save */
        case TINYPY_AST_CONTEXT_STORE:
            TINYPY_CODEGEN_ADD_NAME_OPCODE(c, TINYPY_OP_STORE_ATTR, e->v.Attribute.attr, names);
            break;
        case TINYPY_AST_CONTEXT_DELETE:
            TINYPY_CODEGEN_ADD_NAME_OPCODE(c, TINYPY_OP_DELETE_ATTR, e->v.Attribute.attr, names);
            break;
        case TINYPY_AST_CONTEXT_PARAMETER:
        default:
            TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                           "param invalid in attribute expression");
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_AST_KIND_SUBSCRIPT:
        switch (e->v.Subscript.ctx) {
        case TINYPY_AST_CONTEXT_AUGMENTED_LOAD:
            TINYPY_CODEGEN_VISIT(c, expr, e->v.Subscript.value);
            TINYPY_CODEGEN_VISIT_SLICE(c, e->v.Subscript.slice, TINYPY_AST_CONTEXT_AUGMENTED_LOAD);
            break;
        case TINYPY_AST_CONTEXT_LOAD:
            TINYPY_CODEGEN_VISIT(c, expr, e->v.Subscript.value);
            TINYPY_CODEGEN_VISIT_SLICE(c, e->v.Subscript.slice, TINYPY_AST_CONTEXT_LOAD);
            break;
        case TINYPY_AST_CONTEXT_AUGMENTED_STORE:
            TINYPY_CODEGEN_VISIT_SLICE(c, e->v.Subscript.slice, TINYPY_AST_CONTEXT_AUGMENTED_STORE);
            break;
        case TINYPY_AST_CONTEXT_STORE:
            TINYPY_CODEGEN_VISIT(c, expr, e->v.Subscript.value);
            TINYPY_CODEGEN_VISIT_SLICE(c, e->v.Subscript.slice, TINYPY_AST_CONTEXT_STORE);
            break;
        case TINYPY_AST_CONTEXT_DELETE:
            TINYPY_CODEGEN_VISIT(c, expr, e->v.Subscript.value);
            TINYPY_CODEGEN_VISIT_SLICE(c, e->v.Subscript.slice, TINYPY_AST_CONTEXT_DELETE);
            break;
        case TINYPY_AST_CONTEXT_PARAMETER:
        default:
            TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                           "param invalid in subscript expression");
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_AST_KIND_NAME:
        function_result = __tinypy_codegen_nameop(c, e->v.Name.id, e->v.Name.ctx);
        return function_result;
    /* child nodes of List and Tuple will have expr_context set */
    case TINYPY_AST_KIND_LIST:
        function_result = __tinypy_codegen_list(c, e);
        return function_result;
    case TINYPY_AST_KIND_TUPLE:
        function_result = __tinypy_codegen_tuple(c, e);
        return function_result;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_augassign(tinypy_codegen_t *c, tinypy_ast_statement_t s) {
    tinypy_ast_expression_t e = s->v.AugAssign.target;
    tinypy_ast_expression_t auge;

    switch (e->kind) {
    case TINYPY_AST_KIND_ATTRIBUTE:
        auge = __tinypy_ast_attribute(e->v.Attribute.value, e->v.Attribute.attr,
                                      TINYPY_AST_CONTEXT_AUGMENTED_LOAD, e->lineno, e->col_offset, c->c_arena);
        if (auge == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_CODEGEN_VISIT(c, expr, auge);
        TINYPY_CODEGEN_VISIT(c, expr, s->v.AugAssign.value);
        TINYPY_CODEGEN_ADD_OPCODE(c, __tinypy_codegen_inplace_binary_operator(c, s->v.AugAssign.op));
        auge->v.Attribute.ctx = TINYPY_AST_CONTEXT_AUGMENTED_STORE;
        TINYPY_CODEGEN_VISIT(c, expr, auge);
        break;
    case TINYPY_AST_KIND_SUBSCRIPT:
        auge = __tinypy_ast_subscript(e->v.Subscript.value, e->v.Subscript.slice,
                                      TINYPY_AST_CONTEXT_AUGMENTED_LOAD, e->lineno, e->col_offset, c->c_arena);
        if (auge == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_CODEGEN_VISIT(c, expr, auge);
        TINYPY_CODEGEN_VISIT(c, expr, s->v.AugAssign.value);
        TINYPY_CODEGEN_ADD_OPCODE(c, __tinypy_codegen_inplace_binary_operator(c, s->v.AugAssign.op));
        auge->v.Subscript.ctx = TINYPY_AST_CONTEXT_AUGMENTED_STORE;
        TINYPY_CODEGEN_VISIT(c, expr, auge);
        break;
    case TINYPY_AST_KIND_NAME:
        if (!__tinypy_codegen_nameop(c, e->v.Name.id, TINYPY_AST_CONTEXT_LOAD)) {
            return TINYPY_FALSE;
        }
        TINYPY_CODEGEN_VISIT(c, expr, s->v.AugAssign.value);
        TINYPY_CODEGEN_ADD_OPCODE(c, __tinypy_codegen_inplace_binary_operator(c, s->v.AugAssign.op));
        tinypy_bool_t return_value_1 = __tinypy_codegen_nameop(c, e->v.Name.id, TINYPY_AST_CONTEXT_STORE);
        return return_value_1;
    default:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "invalid tinypy_cst_node_t type (%d) for augmented assignment",
                                   e->kind);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_push_fblock(tinypy_codegen_t *c, tinypy_codegen_frame_block_e t, tinypy_codegen_block_t *b) {
    if (c->u->u_nfblocks >= TINYPY_COMPILER_MAX_BLOCKS) {
        tinypy_internal_compiler_error(c->c_arena, TINYPY_ERROR_SYNTAX,
                                       "too many statically nested blocks",
                                       c->u->u_lineno, 1,
                                       c->c_arena->out_error);
        return TINYPY_FALSE;
    }
    tinypy_codegen_frame_block_t *f = &c->u->u_fblock[c->u->u_nfblocks++];
    f->fb_type = t;
    f->fb_block = b;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_codegen_pop_fblock(tinypy_codegen_t *c, tinypy_codegen_frame_block_e t, tinypy_codegen_block_t *b) {
    tinypy_codegen_unit_t *u = c->u;
    u->u_nfblocks--;
    (void)t;
    (void)b;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_in_loop(tinypy_codegen_t *c) {
    int32_t i;
    tinypy_codegen_unit_t *u = c->u;
    for (i = 0; i < u->u_nfblocks; ++i) {
        if (u->u_fblock[i].fb_type == TINYPY_CODEGEN_FRAME_BLOCK_LOOP) {
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
/* Raises a SyntaxError and returns 0.
   If something goes wrong, a different exception may be raised.
*/

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_error(tinypy_codegen_t *c, const char *errstr) {
    tinypy_internal_compiler_error(c->c_arena, TINYPY_ERROR_SYNTAX, errstr,
                                   c->u->u_lineno, 1, c->c_arena->out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_handle_subscr(tinypy_codegen_t *c, const char *kind, tinypy_ast_expression_context_e ctx) {
    int32_t op = 0;

    (void)kind;

    /* XXX this code is duplicated */
    switch (ctx) {
    case TINYPY_AST_CONTEXT_AUGMENTED_LOAD: /* fall through to TINYPY_AST_CONTEXT_LOAD */
    case TINYPY_AST_CONTEXT_LOAD:
        op = TINYPY_OP_BINARY_SUBSCR;
        break;
    case TINYPY_AST_CONTEXT_AUGMENTED_STORE: /* fall through to TINYPY_AST_CONTEXT_STORE */
    case TINYPY_AST_CONTEXT_STORE:
        op = TINYPY_OP_STORE_SUBSCR;
        break;
    case TINYPY_AST_CONTEXT_DELETE:
        op = TINYPY_OP_DELETE_SUBSCR;
        break;
    case TINYPY_AST_CONTEXT_PARAMETER:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "invalid %s kind %d in subscript\n",
                                   kind, ctx);
        return TINYPY_FALSE;
    }
    if (ctx == TINYPY_AST_CONTEXT_AUGMENTED_LOAD) {
        TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_DUP_TOPX, 2);
    }
    else if (ctx == TINYPY_AST_CONTEXT_AUGMENTED_STORE) {
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_ROT_THREE);
    }
    TINYPY_CODEGEN_ADD_OPCODE(c, op);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_slice(tinypy_codegen_t *c, tinypy_ast_slice_t s, tinypy_ast_expression_context_e ctx) {
    int32_t n = 2;
    (void)ctx;

    /* only handles the cases where TINYPY_OP_BUILD_SLICE is emitted */
    if (s->v.Slice.lower) {
        TINYPY_CODEGEN_VISIT(c, expr, s->v.Slice.lower);
    }
    else {
        TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, c->c_none, consts);
    }

    if (s->v.Slice.upper) {
        TINYPY_CODEGEN_VISIT(c, expr, s->v.Slice.upper);
    }
    else {
        TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, c->c_none, consts);
    }

    if (s->v.Slice.step) {
        n++;
        TINYPY_CODEGEN_VISIT(c, expr, s->v.Slice.step);
    }
    TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_BUILD_SLICE, n);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_simple_slice(tinypy_codegen_t *c, tinypy_ast_slice_t s, tinypy_ast_expression_context_e ctx) {
    int32_t op = 0, slice_offset = 0, stack_count = 0;

    if (s->v.Slice.lower) {
        slice_offset++;
        stack_count++;
        if (ctx != TINYPY_AST_CONTEXT_AUGMENTED_STORE) {
            TINYPY_CODEGEN_VISIT(c, expr, s->v.Slice.lower);
        }
    }
    if (s->v.Slice.upper) {
        slice_offset += 2;
        stack_count++;
        if (ctx != TINYPY_AST_CONTEXT_AUGMENTED_STORE) {
            TINYPY_CODEGEN_VISIT(c, expr, s->v.Slice.upper);
        }
    }

    if (ctx == TINYPY_AST_CONTEXT_AUGMENTED_LOAD) {
        switch (stack_count) {
        case 0:
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_DUP_TOP);
            break;
        case 1:
            TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_DUP_TOPX, 2);
            break;
        case 2:
            TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_DUP_TOPX, 3);
            break;
        }
    }
    else if (ctx == TINYPY_AST_CONTEXT_AUGMENTED_STORE) {
        switch (stack_count) {
        case 0:
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_ROT_TWO);
            break;
        case 1:
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_ROT_THREE);
            break;
        case 2:
            TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_ROT_FOUR);
            break;
        }
    }

    switch (ctx) {
    case TINYPY_AST_CONTEXT_AUGMENTED_LOAD: /* fall through to TINYPY_AST_CONTEXT_LOAD */
    case TINYPY_AST_CONTEXT_LOAD:
        op = TINYPY_OP_SLICE_0;
        break;
    case TINYPY_AST_CONTEXT_AUGMENTED_STORE: /* fall through to TINYPY_AST_CONTEXT_STORE */
    case TINYPY_AST_CONTEXT_STORE:
        op = TINYPY_OP_STORE_SLICE_0;
        break;
    case TINYPY_AST_CONTEXT_DELETE:
        op = TINYPY_OP_DELETE_SLICE_0;
        break;
    case TINYPY_AST_CONTEXT_PARAMETER:
    default:
        TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                       "param invalid in simple slice");
        return TINYPY_FALSE;
    }

    TINYPY_CODEGEN_ADD_OPCODE(c, op + slice_offset);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_visit_nested_slice(tinypy_codegen_t *c, tinypy_ast_slice_t s, tinypy_ast_expression_context_e ctx) {
    tinypy_bool_t function_result;
    switch (s->kind) {
    case TINYPY_AST_KIND_ELLIPSIS:
        TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, c->c_ellipsis, consts);
        break;
    case TINYPY_AST_KIND_SLICE:
        function_result = __tinypy_codegen_slice(c, s, ctx);
        return function_result;
    case TINYPY_AST_KIND_INDEX:
        TINYPY_CODEGEN_VISIT(c, expr, s->v.Index.value);
        break;
    case TINYPY_AST_KIND_EXT_SLICE:
    default:
        TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                       "extended slice invalid in nested slice");
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codegen_visit_slice(tinypy_codegen_t *c, tinypy_ast_slice_t s, tinypy_ast_expression_context_e ctx) {
    char *kindname = NULL;
    switch (s->kind) {
    case TINYPY_AST_KIND_INDEX:
        kindname = "index";
        if (ctx != TINYPY_AST_CONTEXT_AUGMENTED_STORE) {
            TINYPY_CODEGEN_VISIT(c, expr, s->v.Index.value);
        }
        break;
    case TINYPY_AST_KIND_ELLIPSIS:
        kindname = "ellipsis";
        if (ctx != TINYPY_AST_CONTEXT_AUGMENTED_STORE) {
            TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, c->c_ellipsis, consts);
        }
        break;
    case TINYPY_AST_KIND_SLICE:
        kindname = "slice";
        if (!s->v.Slice.step) {
            tinypy_bool_t return_value_1 = __tinypy_codegen_simple_slice(c, s, ctx);
            return return_value_1;
        }
        if (ctx != TINYPY_AST_CONTEXT_AUGMENTED_STORE) {
            if (!__tinypy_codegen_slice(c, s, ctx)) {
                return TINYPY_FALSE;
            }
        }
        break;
    case TINYPY_AST_KIND_EXT_SLICE:
        kindname = "extended slice";
        if (ctx != TINYPY_AST_CONTEXT_AUGMENTED_STORE) {
            int32_t i, n = TINYPY_AST_SEQUENCE_LENGTH(s->v.ExtSlice.dims);
            for (i = 0; i < n; i++) {
                tinypy_ast_slice_t sub = (tinypy_ast_slice_t)TINYPY_AST_SEQUENCE_GET(
                    s->v.ExtSlice.dims, i);
                if (!__tinypy_codegen_visit_nested_slice(c, sub, ctx)) {
                    return TINYPY_FALSE;
                }
            }
            TINYPY_CODEGEN_ADD_INTEGER_OPCODE(c, TINYPY_OP_BUILD_TUPLE, n);
        }
        break;
    default:
        TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR,
                                   "invalid subscript kind %d", s->kind);
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_2 = __tinypy_codegen_handle_subscr(c, kindname, ctx);
    return return_value_2;
}

/* End of the compiler section, beginning of the assembler section */
/* do depth-first search of basic block graph, starting with block.
   post records the block indices in post-order.

   XXX must handle implicit jumps from one block to next
*/

typedef struct tinypy_assembler_t {
    tinypy_value_t *a_bytecode;           /* string containing bytecode */
    int32_t a_offset;                         /* offset into bytecode */
    int32_t a_nblocks;                        /* number of reachable blocks */
    tinypy_codegen_block_t **a_postorder; /* list of blocks in __tinypy_codegen_depth_first postorder */
    tinypy_value_t *a_lnotab;             /* string containing lnotab */
    int32_t a_lnotab_off;                     /* offset into lnotab */
    int32_t a_lineno;                         /* last lineno of emitted instruction */
    int32_t a_lineno_off;                     /* bytecode offset of last lineno */
} tinypy_assembler_t;
//////////////////////////////////////////////////////////////////////////
static void __tinypy_codegen_depth_first(tinypy_codegen_t *c, tinypy_codegen_block_t *b, tinypy_assembler_t *a) {
    int32_t i;
    tinypy_codegen_instruction_t *instr = NULL;

    if (b->b_seen) {
        return;
    }
    b->b_seen = 1;
    if (b->b_next != NULL) {
        __tinypy_codegen_depth_first(c, b->b_next, a);
    }
    for (i = 0; i < b->b_iused; i++) {
        instr = &b->b_instr[i];
        if (instr->i_jrel || instr->i_jabs) {
            __tinypy_codegen_depth_first(c, instr->i_target, a);
        }
    }
    a->a_postorder[a->a_nblocks++] = b;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codegen_stack_depth_walk(tinypy_codegen_t *c, tinypy_codegen_block_t *b, int32_t depth, int32_t maxdepth) {
    int32_t i, target_depth;
    tinypy_codegen_instruction_t *instr;
    if (b->b_seen || b->b_startdepth >= depth) {
        return maxdepth;
    }
    b->b_seen = 1;
    b->b_startdepth = depth;
    for (i = 0; i < b->b_iused; i++) {
        instr = &b->b_instr[i];
        depth += __tinypy_opcode_stack_effect(instr->i_opcode, instr->i_oparg);
        if (depth > maxdepth) {
            maxdepth = depth;
        }
         /* invalid code or bug in __tinypy_codegen_stack_depth() */
        if (instr->i_jrel || instr->i_jabs) {
            target_depth = depth;
            if (instr->i_opcode == TINYPY_OP_FOR_ITER) {
                target_depth = depth - 2;
            }
            else if (instr->i_opcode == TINYPY_OP_SETUP_FINALLY || instr->i_opcode == TINYPY_OP_SETUP_EXCEPT) {
                target_depth = depth + 3;
                if (target_depth > maxdepth) {
                    maxdepth = target_depth;
                }
            }
            else if (instr->i_opcode == TINYPY_OP_JUMP_IF_TRUE_OR_POP || instr->i_opcode == TINYPY_OP_JUMP_IF_FALSE_OR_POP) {
                depth = depth - 1;
            }
            maxdepth = __tinypy_codegen_stack_depth_walk(c, instr->i_target,
                                                         target_depth, maxdepth);
            if (instr->i_opcode == TINYPY_OP_JUMP_ABSOLUTE || instr->i_opcode == TINYPY_OP_JUMP_FORWARD) {
                goto out; /* remaining code is dead */
            }
        }
    }
    if (b->b_next) {
        maxdepth = __tinypy_codegen_stack_depth_walk(c, b->b_next, depth, maxdepth);
    }
out:
    b->b_seen = 0;
    return maxdepth;
}
/* Find the flow path that needs the largest stack.  We assume that
 * cycles in the flow graph have no net effect on the stack depth.
 */
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codegen_stack_depth(tinypy_codegen_t *c) {
    tinypy_codegen_block_t *b, *entryblock;
    entryblock = NULL;
    for (b = c->u->u_blocks; b != NULL; b = b->b_list) {
        b->b_seen = 0;
        b->b_startdepth = INT_MIN;
        entryblock = b;
    }
    if (!entryblock) {
        return 0;
    }
    int32_t return_value_1 = __tinypy_codegen_stack_depth_walk(c, entryblock, 0, 0);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_assembler_init(tinypy_codegen_t *c, tinypy_assembler_t *a, int32_t nblocks, int32_t firstlineno) {
    memset(a, 0, sizeof(tinypy_assembler_t));
    a->a_lineno = firstlineno;
    a->a_bytecode = __tinypy_frontend_string_uninitialized(c->c_module_name, TINYPY_CODEGEN_DEFAULT_CODE_SIZE);
    if (!a->a_bytecode) {
        return TINYPY_FALSE;
    }
    a->a_lnotab = __tinypy_frontend_string_uninitialized(c->c_module_name, TINYPY_CODEGEN_DEFAULT_LINE_TABLE_SIZE);
    if (!a->a_lnotab) {
        return TINYPY_FALSE;
    }
    if ((size_t)nblocks > SIZE_MAX / sizeof(tinypy_codegen_block_t *)) {
        TINYPY_COMPILER_ERR_NO_MEMORY();
        return TINYPY_FALSE;
    }
    a->a_postorder = (tinypy_codegen_block_t **)TINYPY_COMPILER_ARENA_MALLOC(c->c_arena,
                                                                             sizeof(tinypy_codegen_block_t *) * (size_t)nblocks);
    if (!a->a_postorder) {
        TINYPY_COMPILER_ERR_NO_MEMORY();
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_assembler_free(tinypy_assembler_t *a) {
    TINYPY_COMPILER_XDECREF(a->a_bytecode);
    TINYPY_COMPILER_XDECREF(a->a_lnotab);
}
/* Return the size of a basic block in bytes. */

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_instruction_size(tinypy_codegen_instruction_t *instr) {
    if (!instr->i_hasarg) {
        return 1;
    } /* 1 byte for the opcode*/
    if (instr->i_oparg > 0xffff) {
        return 6;
    } /* 1 (opcode) + 1 (TINYPY_OP_EXTENDED_ARG opcode) + 2 (oparg) + 2(oparg extended) */
    return 3; /* 1 (opcode) + 2 (oparg) */
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codegen_block_size(tinypy_codegen_block_t *b) {
    int32_t i;
    int32_t size = 0;

    for (i = 0; i < b->b_iused; i++) {
        size += __tinypy_instruction_size(&b->b_instr[i]);
    }
    return size;
}
/* Appends a pair to the end of the line number table, a_lnotab, representing
   the instruction's bytecode offset and line number.  See
   Objects/lnotab_notes.txt for the description of the line number table. */

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_assembler_add_line(tinypy_assembler_t *a, tinypy_codegen_instruction_t *i) {
    int32_t d_bytecode, d_lineno;
    int32_t len;
    uint8_t *lnotab;

    d_bytecode = a->a_offset - a->a_lineno_off;
    d_lineno = i->i_lineno - a->a_lineno;

    if (d_bytecode == 0 && d_lineno == 0) {
        return TINYPY_TRUE;
    }

    if (d_bytecode > 255) {
        int32_t j, nbytes, ncodes = d_bytecode / 255;
        nbytes = a->a_lnotab_off + 2 * ncodes;
        len = (int32_t)TINYPY_COMPILER_STRING_GET_SIZE(a->a_lnotab);
        if (nbytes >= len) {
            if ((len <= INT_MAX / 2) && (len * 2 < nbytes)) {
                len = nbytes;
            }
            else if (len <= INT_MAX / 2) {
                len *= 2;
            }
            else {
                TINYPY_COMPILER_ERR_NO_MEMORY();
                return TINYPY_FALSE;
            }
            if (TINYPY_COMPILER_STRING_RESIZE(&a->a_lnotab, len) < 0) {
                return TINYPY_FALSE;
            }
        }
        lnotab = (uint8_t *)
                     TINYPY_COMPILER_STRING_AS_STRING(a->a_lnotab) + a->a_lnotab_off;
        for (j = 0; j < ncodes; j++) {
            *lnotab++ = 255;
            *lnotab++ = 0;
        }
        d_bytecode -= ncodes * 255;
        a->a_lnotab_off += ncodes * 2;
    }
    if (d_lineno > 255) {
        int32_t j, nbytes, ncodes = d_lineno / 255;
        nbytes = a->a_lnotab_off + 2 * ncodes;
        len = (int32_t)TINYPY_COMPILER_STRING_GET_SIZE(a->a_lnotab);
        if (nbytes >= len) {
            if ((len <= INT_MAX / 2) && len * 2 < nbytes) {
                len = nbytes;
            }
            else if (len <= INT_MAX / 2) {
                len *= 2;
            }
            else {
                TINYPY_COMPILER_ERR_NO_MEMORY();
                return TINYPY_FALSE;
            }
            if (TINYPY_COMPILER_STRING_RESIZE(&a->a_lnotab, len) < 0) {
                return TINYPY_FALSE;
            }
        }
        lnotab = (uint8_t *)
                     TINYPY_COMPILER_STRING_AS_STRING(a->a_lnotab) + a->a_lnotab_off;
        *lnotab++ = d_bytecode;
        *lnotab++ = 255;
        d_bytecode = 0;
        for (j = 1; j < ncodes; j++) {
            *lnotab++ = 0;
            *lnotab++ = 255;
        }
        d_lineno -= ncodes * 255;
        a->a_lnotab_off += ncodes * 2;
    }

    len = (int32_t)TINYPY_COMPILER_STRING_GET_SIZE(a->a_lnotab);
    if (a->a_lnotab_off + 2 >= len) {
        if (TINYPY_COMPILER_STRING_RESIZE(&a->a_lnotab, len * 2) < 0) {
            return TINYPY_FALSE;
        }
    }
    lnotab = (uint8_t *)
                 TINYPY_COMPILER_STRING_AS_STRING(a->a_lnotab) + a->a_lnotab_off;

    a->a_lnotab_off += 2;
    if (d_bytecode) {
        *lnotab++ = d_bytecode;
        *lnotab++ = d_lineno;
    }
    else { /* First line of a block; def stmt, etc. */
        *lnotab++ = 0;
        *lnotab++ = d_lineno;
    }
    a->a_lineno = i->i_lineno;
    a->a_lineno_off = a->a_offset;
    return TINYPY_TRUE;
}
/* __tinypy_assembler_emit()
   Extend the bytecode with a new instruction.
   Update lnotab if necessary.
*/

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_assembler_emit(tinypy_assembler_t *a, tinypy_codegen_instruction_t *i) {
    int32_t size, arg = 0, ext = 0;
    tinypy_compiler_size_t len = TINYPY_COMPILER_STRING_GET_SIZE(a->a_bytecode);
    char *code;

    size = __tinypy_instruction_size(i);
    if (i->i_hasarg) {
        arg = i->i_oparg;
        ext = arg >> 16;
    }
    if (i->i_lineno && !__tinypy_assembler_add_line(a, i)) {
        return TINYPY_FALSE;
    }
    if (a->a_offset + size >= len) {
        if (len > PTRDIFF_MAX / 2) {
            return TINYPY_FALSE;
        }
        if (TINYPY_COMPILER_STRING_RESIZE(&a->a_bytecode, len * 2) < 0) {
            return TINYPY_FALSE;
        }
    }
    code = TINYPY_COMPILER_STRING_AS_STRING(a->a_bytecode) + a->a_offset;
    a->a_offset += size;
    if (size == 6) {
        *code++ = (char)TINYPY_OP_EXTENDED_ARG;
        *code++ = ext & 0xff;
        *code++ = ext >> 8;
        arg &= 0xffff;
    }
    *code++ = i->i_opcode;
    if (i->i_hasarg) {
        *code++ = arg & 0xff;
        *code++ = arg >> 8;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_assembler_resolve_jumps(tinypy_assembler_t *a, tinypy_codegen_t *c) {
    tinypy_codegen_block_t *b;
    int32_t bsize, totsize, extended_arg_count = 0, last_extended_arg_count;
    int32_t i;

    /* Compute the size of each block and fixup jump args.
       Replace block pointer with position in bytecode. */
    do {
        totsize = 0;
        for (i = a->a_nblocks - 1; i >= 0; i--) {
            b = a->a_postorder[i];
            bsize = __tinypy_codegen_block_size(b);
            b->b_offset = totsize;
            totsize += bsize;
        }
        last_extended_arg_count = extended_arg_count;
        extended_arg_count = 0;
        for (b = c->u->u_blocks; b != NULL; b = b->b_list) {
            bsize = b->b_offset;
            for (i = 0; i < b->b_iused; i++) {
                tinypy_codegen_instruction_t *instr = &b->b_instr[i];
                /* Relative jumps are computed relative to
                   the instruction pointer after fetching
                   the jump instruction.
                */
                bsize += __tinypy_instruction_size(instr);
                if (instr->i_jabs) {
                    instr->i_oparg = instr->i_target->b_offset;
                }
                else if (instr->i_jrel) {
                    int32_t delta = instr->i_target->b_offset - bsize;
                    instr->i_oparg = delta;
                }
                else {
                    continue;
                }
                if (instr->i_oparg > 0xffff) {
                    extended_arg_count++;
                }
            }
        }

        /* XXX: This is an awful hack that could hurt performance, but
            on the bright side it should work until we come up
            with a better solution.

            The issue is that in the first loop __tinypy_codegen_block_size() is called
            which calls __tinypy_instruction_size() which requires i_oparg be set
            appropriately.          There is a bootstrap problem because
            i_oparg is calculated in the second loop above.

            So we loop until we stop seeing new EXTENDED_ARGs.
            The only EXTENDED_ARGs that could be popping up are
            ones in jump instructions.  So this should converge
            fairly quickly.
        */
    } while (last_extended_arg_count != extended_arg_count);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codegen_ordered_keys(tinypy_value_t *dict, int32_t offset) {
    tinypy_value_t *tuple, *k, *v;
    tinypy_compiler_size_t i, pos = 0, size = TINYPY_COMPILER_DICT_SIZE(dict);

    tuple = __tinypy_frontend_tuple_new(dict, size);
    if (tuple == NULL) {
        return NULL;
    }
    while (TINYPY_COMPILER_DICT_NEXT(dict, &pos, &k, &v)) {
        i = TINYPY_COMPILER_INT_AS_LONG(v);
        /* The keys of the dictionary are tuples. (see __tinypy_codegen_add_o)
           The object we want is always first, though. */
        k = TINYPY_COMPILER_TUPLE_GET_ITEM(k, 1);
        TINYPY_COMPILER_INCREF(k);
        TINYPY_COMPILER_TUPLE_SET_ITEM(tuple, i - offset, k);
    }
    return tuple;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codegen_flags(tinypy_codegen_t *c) {
    tinypy_symbol_entry_t *ste = c->u->u_ste;
    int32_t flags = 0, n;
    if (ste->block_type != TINYPY_SYMBOL_BLOCK_MODULE) {
        flags |= TINYPY_CODE_NEW_LOCALS;
    }
    if (ste->block_type == TINYPY_SYMBOL_BLOCK_FUNCTION) {
        if (!ste->unoptimized) {
            flags |= TINYPY_CODE_OPTIMIZED;
        }
        if (ste->nested) {
            flags |= TINYPY_CODE_NESTED;
        }
        if (ste->generator) {
            flags |= TINYPY_CODE_GENERATOR;
        }
        if (ste->variable_arguments) {
            flags |= TINYPY_CODE_VARARGS;
        }
        if (ste->variable_keywords) {
            flags |= TINYPY_CODE_VAR_KEYWORDS;
        }
    }

    /* (Only) inherit compilerflags in TINYPY_COMPILER_FUTURE_MASK */
    flags |= (c->c_flags->flags & TINYPY_COMPILER_FUTURE_MASK);

    n = (int32_t)TINYPY_COMPILER_DICT_SIZE(c->u->u_freevars);
    if (n < 0) {
        return -1;
    }
    if (n == 0) {
        n = (int32_t)TINYPY_COMPILER_DICT_SIZE(c->u->u_cellvars);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            flags |= TINYPY_CODE_NO_FREE;
        }
    }

    return flags;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_code_object_t *__tinypy_codegen_make_code(tinypy_codegen_t *c, tinypy_assembler_t *a) {
    tinypy_code_object_t *co = NULL;
    tinypy_value_t *consts = NULL;
    tinypy_value_t *names = NULL;
    tinypy_value_t *varnames = NULL;
    tinypy_value_t *filename = NULL;
    tinypy_value_t *name = NULL;
    tinypy_value_t *freevars = NULL;
    tinypy_value_t *cellvars = NULL;
    tinypy_value_t *bytecode = NULL;
    int32_t nlocals, flags;

    tinypy_value_t *tmp = __tinypy_codegen_ordered_keys(c->u->u_consts, 0);
    if (!tmp) {
        goto error;
    }
    consts = TINYPY_COMPILER_SEQUENCE_LIST(tmp); /* optimize_code requires a list */
    TINYPY_COMPILER_DECREF(tmp);

    names = __tinypy_codegen_ordered_keys(c->u->u_names, 0);
    varnames = __tinypy_codegen_ordered_keys(c->u->u_varnames, 0);
    if (!consts || !names || !varnames) {
        goto error;
    }

    cellvars = __tinypy_codegen_ordered_keys(c->u->u_cellvars, 0);
    if (!cellvars) {
        goto error;
    }
    freevars = __tinypy_codegen_ordered_keys(c->u->u_freevars, (int32_t)TINYPY_COMPILER_TUPLE_SIZE(cellvars));
    if (!freevars) {
        goto error;
    }
    filename = __tinypy_frontend_string_from_owner(c->c_module_name, c->c_filename,
                                                   c->c_arena->filename_size);
    if (!filename) {
        goto error;
    }

    nlocals = (int32_t)TINYPY_COMPILER_DICT_SIZE(c->u->u_varnames);
    flags = __tinypy_codegen_flags(c);
    if (flags < 0) {
        goto error;
    }

    bytecode = __tinypy_bytecode_optimize(c->c_arena, a->a_bytecode, consts, names, a->a_lnotab);
    if (!bytecode) {
        goto error;
    }

    tmp = TINYPY_COMPILER_LIST_AS_TUPLE(consts); /* __tinypy_bytecode_new requires a tuple */
    if (!tmp) {
        goto error;
    }
    TINYPY_COMPILER_DECREF(consts);
    consts = tmp;

    int32_t codegen_stack_depth = __tinypy_codegen_stack_depth(c);
    co = __tinypy_bytecode_new(c->u->u_argcount, nlocals, codegen_stack_depth, flags,
                               bytecode, consts, names, varnames,
                               freevars, cellvars,
                               filename, c->u->u_name,
                               c->u->u_firstlineno,
                               a->a_lnotab);
error:
    TINYPY_COMPILER_XDECREF(consts);
    TINYPY_COMPILER_XDECREF(names);
    TINYPY_COMPILER_XDECREF(varnames);
    TINYPY_COMPILER_XDECREF(filename);
    TINYPY_COMPILER_XDECREF(name);
    TINYPY_COMPILER_XDECREF(freevars);
    TINYPY_COMPILER_XDECREF(cellvars);
    TINYPY_COMPILER_XDECREF(bytecode);
    return co;
}
/* For debugging purposes only */

//////////////////////////////////////////////////////////////////////////
static tinypy_code_object_t *__tinypy_assembler_build(tinypy_codegen_t *c, int32_t addNone) {
    tinypy_codegen_block_t *b, *entryblock;
    tinypy_assembler_t a;
    int32_t i, j, nblocks;
    tinypy_code_object_t *co = NULL;

    /* Make sure every block that falls off the end returns None.
       XXX TINYPY_CODEGEN_NEXT_BLOCK() isn't quite right, because if the last
       block ends with a jump or return b_next shouldn't set.
    */
    if (!c->u->u_curblock->b_return) {
        TINYPY_CODEGEN_NEXT_BLOCK(c);
        if (addNone) {
            TINYPY_CODEGEN_ADD_OBJECT_OPCODE(c, TINYPY_OP_LOAD_CONST, c->c_none, consts);
        }
        TINYPY_CODEGEN_ADD_OPCODE(c, TINYPY_OP_RETURN_VALUE);
    }

    nblocks = 0;
    entryblock = NULL;
    for (b = c->u->u_blocks; b != NULL; b = b->b_list) {
        nblocks++;
        entryblock = b;
    }

    /* Set firstlineno if it wasn't explicitly set. */
    if (!c->u->u_firstlineno) {
        if (entryblock && entryblock->b_instr) {
            c->u->u_firstlineno = entryblock->b_instr->i_lineno;
        }
        else {
            c->u->u_firstlineno = 1;
        }
    }
    if (!__tinypy_assembler_init(c, &a, nblocks, c->u->u_firstlineno)) {
        goto error;
    }
    __tinypy_codegen_depth_first(c, entryblock, &a);

    /* Can't modify the bytecode after computing jump offsets. */
    __tinypy_assembler_resolve_jumps(&a, c);

    /* Emit code in reverse postorder from __tinypy_codegen_depth_first. */
    for (i = a.a_nblocks - 1; i >= 0; i--) {
        b = a.a_postorder[i];
        for (j = 0; j < b->b_iused; j++) {
            if (!__tinypy_assembler_emit(&a, &b->b_instr[j])) {
                goto error;
            }
        }
    }

    if (TINYPY_COMPILER_STRING_RESIZE(&a.a_lnotab, a.a_lnotab_off) < 0) {
        goto error;
    }
    if (TINYPY_COMPILER_STRING_RESIZE(&a.a_bytecode, a.a_offset) < 0) {
        goto error;
    }

    co = __tinypy_codegen_make_code(c, &a);
error:
    __tinypy_assembler_free(&a);
    return co;
}
