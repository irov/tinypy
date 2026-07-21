#include "value_ops.h"
#include "ast_nodes.h"
#include "codegen.h"
#include "symbol_table.h"

/* error strings used for warnings */
#define TINYPY_SYMBOL_GLOBAL_AFTER_ASSIGN \
    "name '%.400s' is assigned to before global declaration"

#define TINYPY_SYMBOL_GLOBAL_AFTER_USE \
    "name '%.400s' is used prior to global declaration"

#define TINYPY_SYMBOL_IMPORT_STAR_WARNING "import * only allowed at module level"

#define TINYPY_SYMBOL_RETURN_VALUE_IN_GENERATOR \
    "'return' with argument inside generator"

//////////////////////////////////////////////////////////////////////////
static tinypy_symbol_entry_t *__tinypy_symbol_entry_new(tinypy_symbol_table_t *st, tinypy_ast_identifier_t name, tinypy_symbol_block_e block, void *key, int lineno) {
    tinypy_symbol_entry_t *ste;
    tinypy_value_t *handle;

    ste = (tinypy_symbol_entry_t *)TINYPY_COMPILER_ARENA_MALLOC(st->arena, sizeof(*ste));
    if (ste == NULL) {
        return NULL;
    }
    ste->table = st;
    ste->id = tinypy_string_from_bytes(st->arena->vm, &key, sizeof(key));
    if (TINYPY_COMPILER_ARENA_ADD_VALUE(st->arena, ste->id) != 0) {
        return NULL;
    }
    handle = tinypy_string_from_bytes(st->arena->vm, &ste, sizeof(ste));
    if (TINYPY_COMPILER_ARENA_ADD_VALUE(st->arena, handle) != 0) {
        return NULL;
    }
    ste->handle = handle;
    ste->name = name;
    ste->symbols = tinypy_dict_new(st->arena->vm);
    if (TINYPY_COMPILER_ARENA_ADD_VALUE(st->arena, ste->symbols) != 0) {
        return NULL;
    }
    ste->variable_names = tinypy_list_from_items(st->arena->vm, NULL, 0U);
    if (TINYPY_COMPILER_ARENA_ADD_VALUE(st->arena, ste->variable_names) != 0) {
        return NULL;
    }
    ste->children = tinypy_list_from_items(st->arena->vm, NULL, 0U);
    if (TINYPY_COMPILER_ARENA_ADD_VALUE(st->arena, ste->children) != 0) {
        return NULL;
    }

    ste->block_type = block;
    ste->unoptimized = 0;
    ste->nested = 0;
    ste->has_free_variables = 0;
    ste->variable_arguments = 0;
    ste->variable_keywords = 0;
    ste->optimization_line_number = 0;
    ste->temporary_name_index = 0;
    ste->line_number = lineno;

    if (st->current != NULL && (st->current->nested || st->current->block_type == TINYPY_SYMBOL_BLOCK_FUNCTION)) {
        ste->nested = 1;
    }
    ste->child_has_free_variables = 0;
    ste->generator = 0;
    ste->returns_value = 0;

    if (TINYPY_COMPILER_DICT_SET_ITEM(st->symbols, ste->id, handle) < 0) {
        return NULL;
    }

    return ste;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_symbol_entry_t *__tinypy_symbol_entry_from_handle(tinypy_value_t *handle) {
    size_t size;
    const void *bytes = tinypy_string_view(handle, &size);
    tinypy_symbol_entry_t *entry;

    assert(size == sizeof(entry));
    (void)memcpy(&entry, bytes, sizeof(entry));
    return entry;
}

static int __tinypy_symbol_analyze(tinypy_symbol_table_t *st);
static int __tinypy_symbol_warn(tinypy_symbol_table_t *st, tinypy_value_t *warn, const char *msg, int lineno);
static int __tinypy_symbol_enter_block(tinypy_symbol_table_t *st, tinypy_ast_identifier_t name, tinypy_symbol_block_e block, void *ast, int lineno);
static int __tinypy_symbol_exit_block(tinypy_symbol_table_t *st, void *ast);
static int __tinypy_symbol_visit_stmt(tinypy_symbol_table_t *st, tinypy_ast_statement_t s);
static int __tinypy_symbol_visit_expr(tinypy_symbol_table_t *st, tinypy_ast_expression_t s);
static int __tinypy_symbol_visit_listcomp(tinypy_symbol_table_t *st, tinypy_ast_expression_t e);
static int __tinypy_symbol_visit_genexp(tinypy_symbol_table_t *st, tinypy_ast_expression_t s);
static int __tinypy_symbol_visit_setcomp(tinypy_symbol_table_t *st, tinypy_ast_expression_t e);
static int __tinypy_symbol_visit_dictcomp(tinypy_symbol_table_t *st, tinypy_ast_expression_t e);
static int __tinypy_symbol_visit_arguments(tinypy_symbol_table_t *st, tinypy_ast_arguments_t);
static int __tinypy_symbol_visit_excepthandler(tinypy_symbol_table_t *st, tinypy_ast_exception_handler_t);
static int __tinypy_symbol_visit_alias(tinypy_symbol_table_t *st, tinypy_ast_alias_t);
static int __tinypy_symbol_visit_comprehension(tinypy_symbol_table_t *st, tinypy_ast_comprehension_t);
static int __tinypy_symbol_visit_keyword(tinypy_symbol_table_t *st, tinypy_ast_keyword_t);
static int __tinypy_symbol_visit_slice(tinypy_symbol_table_t *st, tinypy_ast_slice_t);
static int __tinypy_symbol_visit_params(tinypy_symbol_table_t *st, tinypy_ast_sequence_t *args, int top);
static int __tinypy_symbol_visit_params_nested(tinypy_symbol_table_t *st, tinypy_ast_sequence_t *args);
static int __tinypy_symbol_implicit_arg(tinypy_symbol_table_t *st, int pos);

#define TINYPY_SYMBOL_DUPLICATE_ARGUMENT \
    "duplicate argument '%s' in function definition"

//////////////////////////////////////////////////////////////////////////
static tinypy_symbol_table_t *__tinypy_symbol_table_new(tinypy_compile_ctx_t *arena) {
    tinypy_symbol_table_t *st;

    st = (tinypy_symbol_table_t *)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(tinypy_symbol_table_t));
    if (st == NULL) {
        return NULL;
    }

    st->arena = arena;
    st->filename = NULL;
    st->symbols = NULL;
    st->stack = tinypy_list_from_items(arena->vm, NULL, 0U);
    if (TINYPY_COMPILER_ARENA_ADD_VALUE(arena, st->stack) != 0) {
        return NULL;
    }
    st->symbols = tinypy_dict_new(arena->vm);
    if (TINYPY_COMPILER_ARENA_ADD_VALUE(arena, st->symbols) != 0) {
        return NULL;
    }
    st->current = NULL;
    st->private_name = NULL;
    st->block_count = 0;
    st->top_name = NULL;
    st->lambda_name = NULL;
    st->generator_expression_name = NULL;
    st->set_comprehension_name = NULL;
    st->dict_comprehension_name = NULL;
    return st;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_symbol_identifier(tinypy_symbol_table_t *st, tinypy_value_t **slot, const char *name) {
    if (*slot == NULL) {
        unsigned long size = strlen(name);
        *slot = tinypy_string_from_bytes(st->arena->vm, name, size);
        if (TINYPY_COMPILER_ARENA_ADD_VALUE(st->arena, *slot) != 0) {
            return NULL;
        }
    }
    return *slot;
}
//////////////////////////////////////////////////////////////////////////
tinypy_symbol_table_t *__tinypy_symbol_table_build(tinypy_compile_ctx_t *arena, tinypy_ast_module_t mod, const char *filename, tinypy_future_features_t *future) {
    tinypy_symbol_table_t *st = __tinypy_symbol_table_new(arena);
    tinypy_ast_sequence_t *seq;
    int i;

    if (st == NULL) {
        return st;
    }
    st->filename = filename;
    st->future = future;
    if (!__tinypy_symbol_identifier(st, &st->top_name, "top") || !__tinypy_symbol_enter_block(st, st->top_name, TINYPY_SYMBOL_BLOCK_MODULE, (void *)mod, 0)) {
        __tinypy_symbol_table_free(st);
        return NULL;
    }

    st->top = st->current;
    st->current->unoptimized = TINYPY_SYMBOL_OPTIMIZATION_TOP_LEVEL;
    /* Any other top-level initialization? */
    switch (mod->kind) {
    case TINYPY_AST_KIND_MODULE:
        seq = mod->v.Module.body;
        for (i = 0; i < TINYPY_AST_SEQUENCE_LENGTH(seq); i++) {
            if (!__tinypy_symbol_visit_stmt(st,
                                            (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(seq, i))) {
                goto error;
            }
        }
        break;
    case TINYPY_AST_KIND_EXPRESSION:
        if (!__tinypy_symbol_visit_expr(st, mod->v.Expression.body)) {
            goto error;
        }
        break;
    case TINYPY_AST_KIND_INTERACTIVE:
        seq = mod->v.Interactive.body;
        for (i = 0; i < TINYPY_AST_SEQUENCE_LENGTH(seq); i++) {
            if (!__tinypy_symbol_visit_stmt(st,
                                            (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(seq, i))) {
                goto error;
            }
        }
        break;
    case TINYPY_AST_KIND_SUITE:
        TINYPY_COMPILER_ERR_SET_STRING(TINYPY_COMPILER_EXC_RUNTIME_ERROR,
                                       "this compiler does not handle Suites");
        goto error;
    }
    if (!__tinypy_symbol_exit_block(st, (void *)mod)) {
        __tinypy_symbol_table_free(st);
        return NULL;
    }
    if (__tinypy_symbol_analyze(st)) {
        return st;
    }
    __tinypy_symbol_table_free(st);
    return NULL;
error:
    (void)__tinypy_symbol_exit_block(st, (void *)mod);
    __tinypy_symbol_table_free(st);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
void __tinypy_symbol_table_free(tinypy_symbol_table_t *st) {
    (void)st;
}
//////////////////////////////////////////////////////////////////////////
tinypy_symbol_entry_t *__tinypy_symbol_table_lookup(tinypy_symbol_table_t *st, void *key) {
    tinypy_value_t *k, *v;

    k = tinypy_string_from_bytes(st->arena->vm, &key, sizeof(key));
    v = TINYPY_COMPILER_DICT_GET_ITEM(st->symbols, k);
    TINYPY_COMPILER_DECREF(k);
    if (v == NULL) {
        return NULL;
    }
    return __tinypy_symbol_entry_from_handle(v);
}
//////////////////////////////////////////////////////////////////////////
int __tinypy_symbol_table_scope(tinypy_symbol_entry_t *ste, tinypy_value_t *name) {
    tinypy_value_t *v = TINYPY_COMPILER_DICT_GET_ITEM(ste->symbols, name);
    if (!v) {
        return 0;
    }
    assert(TINYPY_COMPILER_INT_CHECK(v));
    return (TINYPY_COMPILER_INT_AS_LONG(v) >> TINYPY_SYMBOL_SCOPE_OFFSET) & TINYPY_SYMBOL_SCOPE_MASK;
}

/* Analyze raw symbol information to determine scope of each name.

   The next several functions are helpers for __tinypy_symbol_table_analyze(),
   which determines whether a name is local, global, or free. In addition,
   it determines which local variables are cell variables; they provide
   bindings that are used for free variables in enclosed blocks.

   There are also two kinds of free variables, implicit and explicit.  An
   explicit global is declared with the global statement.  An implicit
   global is a free variable for which the compiler has found no binding
   in an enclosing function scope.  The implicit global is either a global
   or a builtin.  Python's module and class blocks use the xxx_NAME opcodes
   to handle these names to implement slightly odd semantics. In such a
   block, the name is treated as global until it is assigned to; then it
   is treated as a local.

   The symbol table requires two passes to determine the scope of each name.
   The first pass collects raw facts from the AST: the name is a parameter
   here, the name is used by not defined here, etc.  The second pass analyzes
   these facts during a pass over the TINYPY_COMPILER_ST_ENTRY_OBJECTS created during pass 1.

   When a function is entered during the second pass, the parent passes
   the set of all name bindings visible to its children.  These bindings
   are used to determine if the variable is free or an implicit global.
   After doing the local analysis, it analyzes each of its child blocks
   using an updated set of name bindings.

   The children update the free variable set.  If a local variable is free
   in a child, the variable is marked as a cell.  The current function must
   provide runtime storage for the variable that may outlive the function's
   frame.  Cell variables are removed from the free set before the analyze
   function returns to its parent.

   The sets of bound and free variables are implemented as dictionaries
   mapping strings to None.
*/

#define TINYPY_SYMBOL_SET_SCOPE(DICT, NAME, I)                                 \
    {                                                                          \
        tinypy_value_t *o = __tinypy_frontend_integer_from_owner((DICT), (I)); \
        if (!o)                                                                \
            return 0;                                                          \
        if (TINYPY_COMPILER_DICT_SET_ITEM((DICT), (NAME), o) < 0) { \
            TINYPY_COMPILER_DECREF(o);                                         \
            return 0;                                                          \
        }                                                                      \
        TINYPY_COMPILER_DECREF(o);                                             \
    }

/* Decide on scope of name, given flags.

   The namespace dictionaries may be modified to record information
   about the new name.  For example, a new global will add an entry to
   global.  A name that was global can be changed to local.
*/

//////////////////////////////////////////////////////////////////////////
static int __analyze_name(tinypy_symbol_entry_t *ste, tinypy_value_t *dict, tinypy_value_t *name, long flags, tinypy_value_t *bound, tinypy_value_t *local, tinypy_value_t *free, tinypy_value_t *global) {
    if (flags & TINYPY_SYMBOL_DEFINITION_GLOBAL) {
        if (flags & TINYPY_SYMBOL_DEFINITION_PARAMETER) {
            static const char prefix[] = "name '";
            static const char suffix[] = "' is local and global";
            const char *parts[] = {prefix, TINYPY_COMPILER_STRING_AS_STRING(name), suffix};
            size_t part_sizes[] = {sizeof(prefix) - 1U, (size_t)TINYPY_COMPILER_STRING_GET_SIZE(name), sizeof(suffix) - 1U};

            tinypy_internal_compiler_error_parts(ste->table->arena, TINYPY_ERROR_SYNTAX, parts, part_sizes, sizeof(parts) / sizeof(parts[0]), ste->line_number, 1);
            return 0;
        }
        TINYPY_SYMBOL_SET_SCOPE(dict, name, TINYPY_SYMBOL_SCOPE_GLOBAL_EXPLICIT);
        if (__tinypy_frontend_dict_set_none(global, name) < 0) {
            return 0;
        }
        if (bound && TINYPY_COMPILER_DICT_GET_ITEM(bound, name)) {
            if (TINYPY_COMPILER_DICT_DEL_ITEM(bound, name) < 0) {
                return 0;
            }
        }
        return 1;
    }
    if (flags & TINYPY_SYMBOL_DEFINITION_BOUND) {
        TINYPY_SYMBOL_SET_SCOPE(dict, name, TINYPY_SYMBOL_SCOPE_LOCAL);
        if (__tinypy_frontend_dict_set_none(local, name) < 0) {
            return 0;
        }
        if (TINYPY_COMPILER_DICT_GET_ITEM(global, name)) {
            if (TINYPY_COMPILER_DICT_DEL_ITEM(global, name) < 0) {
                return 0;
            }
        }
        return 1;
    }
    /* If an enclosing block has a binding for this name, it
       is a free variable rather than a global variable.
       Note that having a non-NULL bound implies that the block
       is nested.
    */
    if (bound && TINYPY_COMPILER_DICT_GET_ITEM(bound, name)) {
        TINYPY_SYMBOL_SET_SCOPE(dict, name, TINYPY_SYMBOL_SCOPE_FREE);
        ste->has_free_variables = 1;
        if (__tinypy_frontend_dict_set_none(free, name) < 0) {
            return 0;
        }
        return 1;
    }
    /* If a parent has a global statement, then call it global
       explicit?  It could also be global implicit.
     */
    else if (global && TINYPY_COMPILER_DICT_GET_ITEM(global, name)) {
        TINYPY_SYMBOL_SET_SCOPE(dict, name, TINYPY_SYMBOL_SCOPE_GLOBAL_IMPLICIT);
        return 1;
    }
    else {
        if (ste->nested) {
            ste->has_free_variables = 1;
        }
        TINYPY_SYMBOL_SET_SCOPE(dict, name, TINYPY_SYMBOL_SCOPE_GLOBAL_IMPLICIT);
        return 1;
    }
    /* Should never get here. */
    TINYPY_COMPILER_ERR_FORMAT(TINYPY_COMPILER_EXC_SYSTEM_ERROR, "failed to set scope for %s",
                               TINYPY_COMPILER_STRING_AS_STRING(name));
    return 0;
}

#undef SET_SCOPE

/* If a name is defined in free and also in locals, then this block
   provides the binding for the free variable.  The name should be
   marked TINYPY_SYMBOL_SCOPE_CELL in this block and removed from the free list.

   Note that the current block's free variables are included in free.
   That's safe because no name can be free and local in the same scope.
*/

//////////////////////////////////////////////////////////////////////////
static int __analyze_cells(tinypy_value_t *scope, tinypy_value_t *free) {
    tinypy_value_t *name, *v, *w;
    int success = 0;
    tinypy_compiler_size_t pos = 0;

    w = __tinypy_frontend_integer_from_owner(scope, TINYPY_SYMBOL_SCOPE_CELL);
    if (!w) {
        return 0;
    }
    while (TINYPY_COMPILER_DICT_NEXT(scope, &pos, &name, &v)) {
        long flags;
        assert(TINYPY_COMPILER_INT_CHECK(v));
        flags = TINYPY_COMPILER_INT_AS_LONG(v);
        if (flags != TINYPY_SYMBOL_SCOPE_LOCAL) {
            continue;
        }
        if (!TINYPY_COMPILER_DICT_GET_ITEM(free, name)) {
            continue;
        }
        /* Replace TINYPY_SYMBOL_SCOPE_LOCAL with TINYPY_SYMBOL_SCOPE_CELL for this name, and remove
           from free. It is safe to replace the value of name
           in the dict, because it will not cause a resize.
         */
        if (TINYPY_COMPILER_DICT_SET_ITEM(scope, name, w) < 0) {
            goto error;
        }
        if (TINYPY_COMPILER_DICT_DEL_ITEM(free, name) < 0) {
            goto error;
        }
    }
    success = 1;
error:
    TINYPY_COMPILER_DECREF(w);
    return success;
}

/* Check for illegal statements in unoptimized namespaces */
//////////////////////////////////////////////////////////////////////////
static int __check_unoptimized(const tinypy_symbol_entry_t *ste) {
    const char *name;
    size_t name_size;
    const char *trailer;
    size_t trailer_size;

    if (ste->block_type != TINYPY_SYMBOL_BLOCK_FUNCTION || !ste->unoptimized || !(ste->has_free_variables || ste->child_has_free_variables)) {
        return 1;
    }

    name = TINYPY_COMPILER_STRING_AS_STRING(ste->name);
    name_size = (size_t)TINYPY_COMPILER_STRING_GET_SIZE(ste->name);
    if (name_size > 100U) {
        name_size = 100U;
    }
    trailer = ste->child_has_free_variables != 0
                  ? "contains a nested function with free variables"
                  : "is a nested function";
    trailer_size = strlen(trailer);

    switch (ste->unoptimized) {
    case TINYPY_SYMBOL_OPTIMIZATION_TOP_LEVEL: /* exec / import * at top-level is fine */
    case TINYPY_SYMBOL_OPTIMIZATION_EXEC:      /* qualified exec is fine */
        return 1;
    case TINYPY_SYMBOL_OPTIMIZATION_IMPORT_STAR: {
        static const char prefix[] = "import * is not allowed in function '";
        static const char infix[] = "' because it ";
        const char *parts[] = {prefix, name, infix, trailer};
        size_t part_sizes[] = {sizeof(prefix) - 1U, name_size, sizeof(infix) - 1U, trailer_size};

        tinypy_internal_compiler_error_parts(ste->table->arena, TINYPY_ERROR_SYNTAX, parts, part_sizes, sizeof(parts) / sizeof(parts[0]), ste->optimization_line_number, 1);
        break;
    }
    case TINYPY_SYMBOL_OPTIMIZATION_BARE_EXEC: {
        static const char prefix[] = "unqualified exec is not allowed in function '";
        static const char infix[] = "' because it ";
        const char *parts[] = {prefix, name, infix, trailer};
        size_t part_sizes[] = {sizeof(prefix) - 1U, name_size, sizeof(infix) - 1U, trailer_size};

        tinypy_internal_compiler_error_parts(ste->table->arena, TINYPY_ERROR_SYNTAX, parts, part_sizes, sizeof(parts) / sizeof(parts[0]), ste->optimization_line_number, 1);
        break;
    }
    default: {
        static const char prefix[] = "function '";
        static const char infix[] = "' uses import * and bare exec, which are illegal because it ";
        const char *parts[] = {prefix, name, infix, trailer};
        size_t part_sizes[] = {sizeof(prefix) - 1U, name_size, sizeof(infix) - 1U, trailer_size};

        tinypy_internal_compiler_error_parts(ste->table->arena, TINYPY_ERROR_SYNTAX, parts, part_sizes, sizeof(parts) / sizeof(parts[0]), ste->optimization_line_number, 1);
        break;
    }
    }
    return 0;
}

/* Enter the final scope information into the symbols dict.
 *
 * All arguments are dicts.  Modifies symbols, others are read-only.
 */
//////////////////////////////////////////////////////////////////////////
static int __update_symbols(tinypy_value_t *symbols, tinypy_value_t *scope, tinypy_value_t *bound, tinypy_value_t *free, int classflag) {
    tinypy_value_t *name, *v, *u, *w, *free_value = NULL;
    tinypy_compiler_size_t pos = 0;

    while (TINYPY_COMPILER_DICT_NEXT(symbols, &pos, &name, &v)) {
        long i, flags;
        assert(TINYPY_COMPILER_INT_CHECK(v));
        flags = TINYPY_COMPILER_INT_AS_LONG(v);
        w = TINYPY_COMPILER_DICT_GET_ITEM(scope, name);
        assert(w && TINYPY_COMPILER_INT_CHECK(w));
        i = TINYPY_COMPILER_INT_AS_LONG(w);
        flags |= (i << TINYPY_SYMBOL_SCOPE_OFFSET);
        u = __tinypy_frontend_integer_from_owner(symbols, flags);
        if (!u) {
            return 0;
        }
        if (TINYPY_COMPILER_DICT_SET_ITEM(symbols, name, u) < 0) {
            TINYPY_COMPILER_DECREF(u);
            return 0;
        }
        TINYPY_COMPILER_DECREF(u);
    }

    free_value = __tinypy_frontend_integer_from_owner(symbols, TINYPY_SYMBOL_SCOPE_FREE << TINYPY_SYMBOL_SCOPE_OFFSET);
    if (!free_value) {
        return 0;
    }

    /* add a free variable when it's only use is for creating a closure */
    pos = 0;
    while (TINYPY_COMPILER_DICT_NEXT(free, &pos, &name, &v)) {
        tinypy_value_t *o = TINYPY_COMPILER_DICT_GET_ITEM(symbols, name);

        if (o) {
            /* It could be a free variable in a method of
               the class that has the same name as a local
               or global in the class scope.
            */
            if (classflag && TINYPY_COMPILER_INT_AS_LONG(o) & (TINYPY_SYMBOL_DEFINITION_BOUND | TINYPY_SYMBOL_DEFINITION_GLOBAL)) {
                long i = TINYPY_COMPILER_INT_AS_LONG(o) | TINYPY_SYMBOL_DEFINITION_FREE_CLASS;
                o = __tinypy_frontend_integer_from_owner(symbols, i);
                if (!o) {
                    TINYPY_COMPILER_DECREF(free_value);
                    return 0;
                }
                if (TINYPY_COMPILER_DICT_SET_ITEM(symbols, name, o) < 0) {
                    TINYPY_COMPILER_DECREF(o);
                    TINYPY_COMPILER_DECREF(free_value);
                    return 0;
                }
                TINYPY_COMPILER_DECREF(o);
            }
            /* else it's not free, probably a cell */
            continue;
        }
        if (!TINYPY_COMPILER_DICT_GET_ITEM(bound, name)) {
            continue;
        } /* it's a global */

        if (TINYPY_COMPILER_DICT_SET_ITEM(symbols, name, free_value) < 0) {
            TINYPY_COMPILER_DECREF(free_value);
            return 0;
        }
    }
    TINYPY_COMPILER_DECREF(free_value);
    return 1;
}

/* Make final symbol table decisions for block of ste.

   Arguments:
   ste -- current symtable entry (input/output)
   bound -- set of variables bound in enclosing scopes (input).  bound
       is NULL for module blocks.
   free -- set of free variables in enclosed scopes (output)
   globals -- set of declared global variables in enclosing scopes (input)

   The implementation uses two mutually recursive functions,
   __analyze_block() and __analyze_child_block().  __analyze_block() is
   responsible for analyzing the individual names defined in a block.
   __analyze_child_block() prepares temporary namespace dictionaries
   used to evaluated nested blocks.

   The two functions exist because a child block should see the name
   bindings of its enclosing blocks, but those bindings should not
   propagate back to a parent block.
*/

static int __analyze_child_block(tinypy_symbol_entry_t *entry, tinypy_value_t *bound, tinypy_value_t *free, tinypy_value_t *global, tinypy_value_t *child_free);

//////////////////////////////////////////////////////////////////////////
static int __analyze_block(tinypy_symbol_entry_t *ste, tinypy_value_t *bound, tinypy_value_t *free, tinypy_value_t *global) {
    tinypy_value_t *name, *v, *local = NULL, *scope = NULL;
    tinypy_value_t *newbound = NULL, *newglobal = NULL;
    tinypy_value_t *newfree = NULL, *allfree = NULL;
    int i, success = 0;
    tinypy_compiler_size_t pos = 0;

    local = __tinypy_frontend_dict_new_from_owner(ste->symbols); /* collect new names bound in block */
    if (!local) {
        goto error;
    }
    scope = __tinypy_frontend_dict_new_from_owner(ste->symbols); /* collect scopes defined for each name */
    if (!scope) {
        goto error;
    }

    /* Allocate new global and bound variable dictionaries.  These
       dictionaries hold the names visible in nested blocks.  For
       ClassBlocks, the bound and global names are initialized
       before analyzing names, because class bindings aren't
       visible in methods.  For other blocks, they are initialized
       after names are analyzed.
     */

    /* TODO(jhylton): Package these dicts in a struct so that we
       can write reasonable helper functions?
    */
    newglobal = __tinypy_frontend_dict_new_from_owner(ste->symbols);
    if (!newglobal) {
        goto error;
    }
    newbound = __tinypy_frontend_dict_new_from_owner(ste->symbols);
    if (!newbound) {
        goto error;
    }
    newfree = __tinypy_frontend_dict_new_from_owner(ste->symbols);
    if (!newfree) {
        goto error;
    }

    if (ste->block_type == TINYPY_SYMBOL_BLOCK_CLASS) {
        if (TINYPY_COMPILER_DICT_UPDATE(newglobal, global) < 0) {
            goto error;
        }
        if (bound) {
            if (TINYPY_COMPILER_DICT_UPDATE(newbound, bound) < 0) {
                goto error;
            }
        }
    }

    while (TINYPY_COMPILER_DICT_NEXT(ste->symbols, &pos, &name, &v)) {
        long flags = TINYPY_COMPILER_INT_AS_LONG(v);
        if (!__analyze_name(ste, scope, name, flags,
                            bound, local, free, global)) {
            goto error;
        }
    }

    if (ste->block_type != TINYPY_SYMBOL_BLOCK_CLASS) {
        if (ste->block_type == TINYPY_SYMBOL_BLOCK_FUNCTION) {
            if (TINYPY_COMPILER_DICT_UPDATE(newbound, local) < 0) {
                goto error;
            }
        }
        if (bound) {
            if (TINYPY_COMPILER_DICT_UPDATE(newbound, bound) < 0) {
                goto error;
            }
        }
        if (TINYPY_COMPILER_DICT_UPDATE(newglobal, global) < 0) {
            goto error;
        }
    }

    /* Recursively call __analyze_block() on each child block.

       newbound, newglobal now contain the names visible in
       nested blocks.  The free variables in the children will
       be collected in allfree.
    */
    allfree = __tinypy_frontend_dict_new_from_owner(ste->symbols);
    if (!allfree) {
        goto error;
    }
    for (i = 0; i < TINYPY_COMPILER_LIST_GET_SIZE(ste->children); ++i) {
        tinypy_value_t *c = TINYPY_COMPILER_LIST_GET_ITEM(ste->children, i);
        tinypy_symbol_entry_t *entry;
        assert(c != NULL);
        entry = __tinypy_symbol_entry_from_handle(c);
        if (!__analyze_child_block(entry, newbound, newfree, newglobal,
                                   allfree)) {
            goto error;
        }
        if (entry->has_free_variables || entry->child_has_free_variables) {
            ste->child_has_free_variables = 1;
        }
    }

    if (TINYPY_COMPILER_DICT_UPDATE(newfree, allfree) < 0) {
        goto error;
    }
    if (ste->block_type == TINYPY_SYMBOL_BLOCK_FUNCTION && !__analyze_cells(scope, newfree)) {
        goto error;
    }
    if (!__update_symbols(ste->symbols, scope, bound, newfree,
                          ste->block_type == TINYPY_SYMBOL_BLOCK_CLASS)) {
        goto error;
    }
    if (!__check_unoptimized(ste)) {
        goto error;
    }

    if (TINYPY_COMPILER_DICT_UPDATE(free, newfree) < 0) {
        goto error;
    }
    success = 1;
error:
    TINYPY_COMPILER_XDECREF(local);
    TINYPY_COMPILER_XDECREF(scope);
    TINYPY_COMPILER_XDECREF(newbound);
    TINYPY_COMPILER_XDECREF(newglobal);
    TINYPY_COMPILER_XDECREF(newfree);
    TINYPY_COMPILER_XDECREF(allfree);
    if (!success) {
        assert(TINYPY_COMPILER_ERR_OCCURRED());
    }
    return success;
}
//////////////////////////////////////////////////////////////////////////
static int __analyze_child_block(tinypy_symbol_entry_t *entry, tinypy_value_t *bound, tinypy_value_t *free, tinypy_value_t *global, tinypy_value_t *child_free) {
    tinypy_value_t *temp_bound = NULL, *temp_global = NULL, *temp_free = NULL;

    /* Copy the bound and global dictionaries.

       These dictionaries are used by all blocks enclosed by the
       current block.  The __analyze_block() call modifies these
       dictionaries.

    */
    temp_bound = __tinypy_frontend_dict_new_from_owner(entry->symbols);
    if (!temp_bound) {
        goto error;
    }
    if (TINYPY_COMPILER_DICT_UPDATE(temp_bound, bound) < 0) {
        goto error;
    }
    temp_free = __tinypy_frontend_dict_new_from_owner(entry->symbols);
    if (!temp_free) {
        goto error;
    }
    if (TINYPY_COMPILER_DICT_UPDATE(temp_free, free) < 0) {
        goto error;
    }
    temp_global = __tinypy_frontend_dict_new_from_owner(entry->symbols);
    if (!temp_global) {
        goto error;
    }
    if (TINYPY_COMPILER_DICT_UPDATE(temp_global, global) < 0) {
        goto error;
    }

    if (!__analyze_block(entry, temp_bound, temp_free, temp_global)) {
        goto error;
    }
    if (TINYPY_COMPILER_DICT_UPDATE(child_free, temp_free) < 0) {
        goto error;
    }
    TINYPY_COMPILER_DECREF(temp_bound);
    TINYPY_COMPILER_DECREF(temp_free);
    TINYPY_COMPILER_DECREF(temp_global);
    return 1;
error:
    TINYPY_COMPILER_XDECREF(temp_bound);
    TINYPY_COMPILER_XDECREF(temp_free);
    TINYPY_COMPILER_XDECREF(temp_global);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_analyze(tinypy_symbol_table_t *st) {
    tinypy_value_t *free, *global;
    int r;

    free = __tinypy_frontend_dict_new_from_owner(st->symbols);
    if (!free) {
        return 0;
    }
    global = __tinypy_frontend_dict_new_from_owner(st->symbols);
    if (!global) {
        TINYPY_COMPILER_DECREF(free);
        return 0;
    }
    r = __analyze_block(st->top, NULL, free, global);
    TINYPY_COMPILER_DECREF(free);
    TINYPY_COMPILER_DECREF(global);
    return r;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_warn(tinypy_symbol_table_t *st, tinypy_value_t *warn, const char *msg, int lineno) {
    (void)st;
    (void)warn;
    (void)msg;
    (void)lineno;
    return 1;
}

/* __tinypy_symbol_enter_block() gets a reference via ste_new.
   This reference is released when the block is exited, via the DECREF
   in __tinypy_symbol_exit_block().
*/

//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_exit_block(tinypy_symbol_table_t *st, void *ast) {
    tinypy_compiler_size_t end;
    tinypy_value_t *handle;

    (void)ast;
    st->current = NULL;
    end = TINYPY_COMPILER_LIST_GET_SIZE(st->stack) - 1;
    if (end >= 0) {
        handle = TINYPY_COMPILER_LIST_GET_ITEM(st->stack, end);
        if (handle == NULL) {
            return 0;
        }
        st->current = __tinypy_symbol_entry_from_handle(handle);
        if (TINYPY_COMPILER_SEQUENCE_DEL_ITEM(st->stack, end) < 0) {
            return 0;
        }
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_enter_block(tinypy_symbol_table_t *st, tinypy_ast_identifier_t name, tinypy_symbol_block_e block, void *ast, int lineno) {
    tinypy_symbol_entry_t *prev = NULL;

    if (st->current) {
        prev = st->current;
        if (TINYPY_COMPILER_LIST_APPEND(st->stack, st->current->handle) < 0) {
            return 0;
        }
    }
    st->current = __tinypy_symbol_entry_new(st, name, block, ast, lineno);
    if (st->current == NULL) {
        return 0;
    }
    if (block == TINYPY_SYMBOL_BLOCK_MODULE) {
        st->global_symbols = st->current->symbols;
    }
    if (prev) {
        if (TINYPY_COMPILER_LIST_APPEND(prev->children, st->current->handle) < 0) {
            return 0;
        }
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static long __tinypy_symbol_lookup(tinypy_symbol_table_t *st, tinypy_value_t *name) {
    tinypy_value_t *o;
    tinypy_value_t *mangled = __tinypy_frontend_mangle(st->arena, st->private_name, name);
    if (!mangled) {
        return 0;
    }
    o = TINYPY_COMPILER_DICT_GET_ITEM(st->current->symbols, mangled);
    TINYPY_COMPILER_DECREF(mangled);
    if (!o) {
        return 0;
    }
    return TINYPY_COMPILER_INT_AS_LONG(o);
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_add_def(tinypy_symbol_table_t *st, tinypy_value_t *name, int flag) {
    tinypy_value_t *o;
    tinypy_value_t *dict;
    long val;
    tinypy_value_t *mangled = __tinypy_frontend_mangle(st->arena, st->private_name, name);

    if (!mangled) {
        return 0;
    }
    dict = st->current->symbols;
    if ((o = TINYPY_COMPILER_DICT_GET_ITEM(dict, mangled))) {
        val = TINYPY_COMPILER_INT_AS_LONG(o);
        if ((flag & TINYPY_SYMBOL_DEFINITION_PARAMETER) && (val & TINYPY_SYMBOL_DEFINITION_PARAMETER)) {
            /* Is it better to use 'mangled' or 'name' here? */
            static const char prefix[] = "duplicate argument '";
            static const char suffix[] = "' in function definition";
            const char *parts[] = {prefix, TINYPY_COMPILER_STRING_AS_STRING(name), suffix};
            size_t part_sizes[] = {sizeof(prefix) - 1U, (size_t)TINYPY_COMPILER_STRING_GET_SIZE(name), sizeof(suffix) - 1U};

            tinypy_internal_compiler_error_parts(st->arena, TINYPY_ERROR_SYNTAX, parts, part_sizes, sizeof(parts) / sizeof(parts[0]), st->current->line_number, 1);
            goto error;
        }
        val |= flag;
    }
    else {
        if (st->arena->limits.max_symbols != 0U && st->arena->symbol_count >= st->arena->limits.max_symbols) {
            tinypy_internal_compiler_error(st->arena, TINYPY_ERROR_COMPILER_LIMIT, "symbol limit exceeded", st->current->line_number, 1, st->arena->out_error);
            goto error;
        }
        st->arena->symbol_count += 1U;
        val = flag;
    }
    o = __tinypy_frontend_integer_from_owner(dict, val);
    if (o == NULL) {
        goto error;
    }
    if (TINYPY_COMPILER_DICT_SET_ITEM(dict, mangled, o) < 0) {
        TINYPY_COMPILER_DECREF(o);
        goto error;
    }
    TINYPY_COMPILER_DECREF(o);

    if (flag & TINYPY_SYMBOL_DEFINITION_PARAMETER) {
        if (TINYPY_COMPILER_LIST_APPEND(st->current->variable_names, mangled) < 0) {
            goto error;
        }
    }
    else if (flag & TINYPY_SYMBOL_DEFINITION_GLOBAL) {
        /* XXX need to update TINYPY_SYMBOL_DEFINITION_GLOBAL for other flags too;
           perhaps only TINYPY_SYMBOL_DEFINITION_FREE_GLOBAL */
        val = flag;
        if ((o = TINYPY_COMPILER_DICT_GET_ITEM(st->global_symbols, mangled))) {
            val |= TINYPY_COMPILER_INT_AS_LONG(o);
        }
        o = __tinypy_frontend_integer_from_owner(st->global_symbols, val);
        if (o == NULL) {
            goto error;
        }
        if (TINYPY_COMPILER_DICT_SET_ITEM(st->global_symbols, mangled, o) < 0) {
            TINYPY_COMPILER_DECREF(o);
            goto error;
        }
        TINYPY_COMPILER_DECREF(o);
    }
    TINYPY_COMPILER_DECREF(mangled);
    return 1;

error:
    TINYPY_COMPILER_DECREF(mangled);
    return 0;
}

/* Symbol visitors dispatch to the matching tinypy AST node handler.
   The tail variants skip an initial portion of a node sequence. */

#define TINYPY_SYMBOL_VISIT(ST, TYPE, V)          \
    if (!__tinypy_symbol_visit_##TYPE((ST), (V))) \
        return 0;

#define TINYPY_SYMBOL_VISIT_IN_BLOCK(ST, TYPE, V, S) \
    if (!__tinypy_symbol_visit_##TYPE((ST), (V))) { \
        __tinypy_symbol_exit_block((ST), (S));       \
        return 0;                                    \
    }

#define TINYPY_SYMBOL_VISIT_SEQUENCE(ST, TYPE, SEQ)                                                                                          \
    {                                                                                                                                        \
        int __tinypy_visit_index;                                                                                                            \
        tinypy_ast_sequence_t *__tinypy_visit_sequence = (SEQ);                                                                              \
        for (__tinypy_visit_index = 0; __tinypy_visit_index < TINYPY_AST_SEQUENCE_LENGTH(__tinypy_visit_sequence); __tinypy_visit_index++) { \
            TINYPY_AST_SEQUENCE_TYPE(TYPE)                                                                                                   \
            __tinypy_visit_element = (TINYPY_AST_SEQUENCE_TYPE(TYPE))TINYPY_AST_SEQUENCE_GET(__tinypy_visit_sequence, __tinypy_visit_index); \
            if (!__tinypy_symbol_visit_##TYPE((ST), __tinypy_visit_element))                                                                 \
                return 0;                                                                                                                    \
        }                                                                                                                                    \
    }

#define TINYPY_SYMBOL_VISIT_SEQUENCE_IN_BLOCK(ST, TYPE, SEQ, S)                                                                              \
    {                                                                                                                                        \
        int __tinypy_visit_index;                                                                                                            \
        tinypy_ast_sequence_t *__tinypy_visit_sequence = (SEQ);                                                                              \
        for (__tinypy_visit_index = 0; __tinypy_visit_index < TINYPY_AST_SEQUENCE_LENGTH(__tinypy_visit_sequence); __tinypy_visit_index++) { \
            TINYPY_AST_SEQUENCE_TYPE(TYPE)                                                                                                   \
            __tinypy_visit_element = (TINYPY_AST_SEQUENCE_TYPE(TYPE))TINYPY_AST_SEQUENCE_GET(__tinypy_visit_sequence, __tinypy_visit_index); \
            if (!__tinypy_symbol_visit_##TYPE((ST), __tinypy_visit_element)) { \
                __tinypy_symbol_exit_block((ST), (S));                                                                                       \
                return 0;                                                                                                                    \
            }                                                                                                                                \
        }                                                                                                                                    \
    }

#define TINYPY_SYMBOL_VISIT_SEQUENCE_TAIL(ST, TYPE, SEQ, START)                                                                                  \
    {                                                                                                                                            \
        int __tinypy_visit_index;                                                                                                                \
        tinypy_ast_sequence_t *__tinypy_visit_sequence = (SEQ);                                                                                  \
        for (__tinypy_visit_index = (START); __tinypy_visit_index < TINYPY_AST_SEQUENCE_LENGTH(__tinypy_visit_sequence); __tinypy_visit_index++) { \
            TINYPY_AST_SEQUENCE_TYPE(TYPE)                                                                                                       \
            __tinypy_visit_element = (TINYPY_AST_SEQUENCE_TYPE(TYPE))TINYPY_AST_SEQUENCE_GET(__tinypy_visit_sequence, __tinypy_visit_index);     \
            if (!__tinypy_symbol_visit_##TYPE((ST), __tinypy_visit_element))                                                                     \
                return 0;                                                                                                                        \
        }                                                                                                                                        \
    }

#define TINYPY_SYMBOL_VISIT_SEQUENCE_TAIL_IN_BLOCK(ST, TYPE, SEQ, START, S)                                                                      \
    {                                                                                                                                            \
        int __tinypy_visit_index;                                                                                                                \
        tinypy_ast_sequence_t *__tinypy_visit_sequence = (SEQ);                                                                                  \
        for (__tinypy_visit_index = (START); __tinypy_visit_index < TINYPY_AST_SEQUENCE_LENGTH(__tinypy_visit_sequence); __tinypy_visit_index++) { \
            TINYPY_AST_SEQUENCE_TYPE(TYPE)                                                                                                       \
            __tinypy_visit_element = (TINYPY_AST_SEQUENCE_TYPE(TYPE))TINYPY_AST_SEQUENCE_GET(__tinypy_visit_sequence, __tinypy_visit_index);     \
            if (!__tinypy_symbol_visit_##TYPE((ST), __tinypy_visit_element)) { \
                __tinypy_symbol_exit_block((ST), (S));                                                                                           \
                return 0;                                                                                                                        \
            }                                                                                                                                    \
        }                                                                                                                                        \
    }
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_stmt(tinypy_symbol_table_t *st, tinypy_ast_statement_t s) {
    switch (s->kind) {
    case TINYPY_AST_KIND_FUNCTION_DEF:
        if (!__tinypy_symbol_add_def(st, s->v.FunctionDef.name, TINYPY_SYMBOL_DEFINITION_LOCAL)) {
            return 0;
        }
        if (s->v.FunctionDef.args->defaults) {
            TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, s->v.FunctionDef.args->defaults);
        }
        if (s->v.FunctionDef.decorator_list) {
            TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, s->v.FunctionDef.decorator_list);
        }
        if (!__tinypy_symbol_enter_block(st, s->v.FunctionDef.name,
                                         TINYPY_SYMBOL_BLOCK_FUNCTION, (void *)s, s->lineno)) {
            return 0;
        }
        TINYPY_SYMBOL_VISIT_IN_BLOCK(st, arguments, s->v.FunctionDef.args, s);
        TINYPY_SYMBOL_VISIT_SEQUENCE_IN_BLOCK(st, stmt, s->v.FunctionDef.body, s);
        if (!__tinypy_symbol_exit_block(st, s)) {
            return 0;
        }
        break;
    case TINYPY_AST_KIND_CLASS_DEF: {
        tinypy_value_t *tmp;
        if (!__tinypy_symbol_add_def(st, s->v.ClassDef.name, TINYPY_SYMBOL_DEFINITION_LOCAL)) {
            return 0;
        }
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, s->v.ClassDef.bases);
        if (s->v.ClassDef.decorator_list) {
            TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, s->v.ClassDef.decorator_list);
        }
        if (!__tinypy_symbol_enter_block(st, s->v.ClassDef.name, TINYPY_SYMBOL_BLOCK_CLASS,
                                         (void *)s, s->lineno)) {
            return 0;
        }
        tmp = st->private_name;
        st->private_name = s->v.ClassDef.name;
        TINYPY_SYMBOL_VISIT_SEQUENCE_IN_BLOCK(st, stmt, s->v.ClassDef.body, s);
        st->private_name = tmp;
        if (!__tinypy_symbol_exit_block(st, s)) {
            return 0;
        }
        break;
    }
    case TINYPY_AST_KIND_RETURN:
        if (s->v.Return.value) {
            TINYPY_SYMBOL_VISIT(st, expr, s->v.Return.value);
            st->current->returns_value = 1;
            if (st->current->generator) {
                tinypy_internal_compiler_error(st->arena, TINYPY_ERROR_SYNTAX, TINYPY_SYMBOL_RETURN_VALUE_IN_GENERATOR, s->lineno, 1, st->arena->out_error);
                return 0;
            }
        }
        break;
    case TINYPY_AST_KIND_DELETE:
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, s->v.Delete.targets);
        break;
    case TINYPY_AST_KIND_ASSIGN:
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, s->v.Assign.targets);
        TINYPY_SYMBOL_VISIT(st, expr, s->v.Assign.value);
        break;
    case TINYPY_AST_KIND_AUG_ASSIGN:
        TINYPY_SYMBOL_VISIT(st, expr, s->v.AugAssign.target);
        TINYPY_SYMBOL_VISIT(st, expr, s->v.AugAssign.value);
        break;
    case TINYPY_AST_KIND_PRINT:
        if (s->v.Print.dest) {
            TINYPY_SYMBOL_VISIT(st, expr, s->v.Print.dest);
        }
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, s->v.Print.values);
        break;
    case TINYPY_AST_KIND_FOR:
        TINYPY_SYMBOL_VISIT(st, expr, s->v.For.target);
        TINYPY_SYMBOL_VISIT(st, expr, s->v.For.iter);
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, stmt, s->v.For.body);
        if (s->v.For.orelse) {
            TINYPY_SYMBOL_VISIT_SEQUENCE(st, stmt, s->v.For.orelse);
        }
        break;
    case TINYPY_AST_KIND_WHILE:
        TINYPY_SYMBOL_VISIT(st, expr, s->v.While.test);
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, stmt, s->v.While.body);
        if (s->v.While.orelse) {
            TINYPY_SYMBOL_VISIT_SEQUENCE(st, stmt, s->v.While.orelse);
        }
        break;
    case TINYPY_AST_KIND_IF:
        /* XXX if 0: and lookup_yield() hacks */
        TINYPY_SYMBOL_VISIT(st, expr, s->v.If.test);
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, stmt, s->v.If.body);
        if (s->v.If.orelse) {
            TINYPY_SYMBOL_VISIT_SEQUENCE(st, stmt, s->v.If.orelse);
        }
        break;
    case TINYPY_AST_KIND_RAISE:
        if (s->v.Raise.type) {
            TINYPY_SYMBOL_VISIT(st, expr, s->v.Raise.type);
            if (s->v.Raise.inst) {
                TINYPY_SYMBOL_VISIT(st, expr, s->v.Raise.inst);
                if (s->v.Raise.tback) {
                    TINYPY_SYMBOL_VISIT(st, expr, s->v.Raise.tback);
                }
            }
        }
        break;
    case TINYPY_AST_KIND_TRY_EXCEPT:
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, stmt, s->v.TryExcept.body);
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, stmt, s->v.TryExcept.orelse);
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, excepthandler, s->v.TryExcept.handlers);
        break;
    case TINYPY_AST_KIND_TRY_FINALLY:
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, stmt, s->v.TryFinally.body);
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, stmt, s->v.TryFinally.finalbody);
        break;
    case TINYPY_AST_KIND_ASSERT:
        TINYPY_SYMBOL_VISIT(st, expr, s->v.Assert.test);
        if (s->v.Assert.msg) {
            TINYPY_SYMBOL_VISIT(st, expr, s->v.Assert.msg);
        }
        break;
    case TINYPY_AST_KIND_IMPORT:
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, alias, s->v.Import.names);
        /* XXX Don't have the lineno available inside
           visit_alias */
        if (st->current->unoptimized && !st->current->optimization_line_number) {
            st->current->optimization_line_number = s->lineno;
        }
        break;
    case TINYPY_AST_KIND_IMPORT_FROM:
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, alias, s->v.ImportFrom.names);
        /* XXX Don't have the lineno available inside
           visit_alias */
        if (st->current->unoptimized && !st->current->optimization_line_number) {
            st->current->optimization_line_number = s->lineno;
        }
        break;
    case TINYPY_AST_KIND_EXEC:
        TINYPY_SYMBOL_VISIT(st, expr, s->v.Exec.body);
        if (!st->current->optimization_line_number) {
            st->current->optimization_line_number = s->lineno;
        }
        if (s->v.Exec.globals) {
            st->current->unoptimized |= TINYPY_SYMBOL_OPTIMIZATION_EXEC;
            TINYPY_SYMBOL_VISIT(st, expr, s->v.Exec.globals);
            if (s->v.Exec.locals) {
                TINYPY_SYMBOL_VISIT(st, expr, s->v.Exec.locals);
            }
        }
        else {
            st->current->unoptimized |= TINYPY_SYMBOL_OPTIMIZATION_BARE_EXEC;
        }
        break;
    case TINYPY_AST_KIND_GLOBAL: {
        int i;
        tinypy_ast_sequence_t *seq = s->v.Global.names;
        for (i = 0; i < TINYPY_AST_SEQUENCE_LENGTH(seq); i++) {
            tinypy_ast_identifier_t name = (tinypy_ast_identifier_t)TINYPY_AST_SEQUENCE_GET(seq, i);
            long cur = __tinypy_symbol_lookup(st, name);
            if (cur < 0) {
                return 0;
            }
            if (cur & (TINYPY_SYMBOL_DEFINITION_LOCAL | TINYPY_SYMBOL_USE)) {
                const char *message = (cur & TINYPY_SYMBOL_DEFINITION_LOCAL) ? "name is assigned to before global declaration" : "name is used prior to global declaration";
                if (!__tinypy_symbol_warn(st, NULL, message, s->lineno)) {
                    return 0;
                }
            }
            if (!__tinypy_symbol_add_def(st, name, TINYPY_SYMBOL_DEFINITION_GLOBAL)) {
                return 0;
            }
        }
        break;
    }
    case TINYPY_AST_KIND_EXPR:
        TINYPY_SYMBOL_VISIT(st, expr, s->v.Expr.value);
        break;
    case TINYPY_AST_KIND_PASS:
    case TINYPY_AST_KIND_BREAK:
    case TINYPY_AST_KIND_CONTINUE:
        /* nothing to do here */
        break;
    case TINYPY_AST_KIND_WITH:
        TINYPY_SYMBOL_VISIT(st, expr, s->v.With.context_expr);
        if (s->v.With.optional_vars) {
            TINYPY_SYMBOL_VISIT(st, expr, s->v.With.optional_vars);
        }
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, stmt, s->v.With.body);
        break;
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_expr(tinypy_symbol_table_t *st, tinypy_ast_expression_t e) {
    switch (e->kind) {
    case TINYPY_AST_KIND_BOOL_OP:
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, e->v.BoolOp.values);
        break;
    case TINYPY_AST_KIND_BIN_OP:
        TINYPY_SYMBOL_VISIT(st, expr, e->v.BinOp.left);
        TINYPY_SYMBOL_VISIT(st, expr, e->v.BinOp.right);
        break;
    case TINYPY_AST_KIND_UNARY_OP:
        TINYPY_SYMBOL_VISIT(st, expr, e->v.UnaryOp.operand);
        break;
    case TINYPY_AST_KIND_LAMBDA: {
        if (!__tinypy_symbol_identifier(st, &st->lambda_name, "lambda")) {
            return 0;
        }
        if (e->v.Lambda.args->defaults) {
            TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, e->v.Lambda.args->defaults);
        }
        if (!__tinypy_symbol_enter_block(st, st->lambda_name,
                                         TINYPY_SYMBOL_BLOCK_FUNCTION, (void *)e, e->lineno)) {
            return 0;
        }
        TINYPY_SYMBOL_VISIT_IN_BLOCK(st, arguments, e->v.Lambda.args, (void *)e);
        TINYPY_SYMBOL_VISIT_IN_BLOCK(st, expr, e->v.Lambda.body, (void *)e);
        if (!__tinypy_symbol_exit_block(st, (void *)e)) {
            return 0;
        }
        break;
    }
    case TINYPY_AST_KIND_IF_EXP:
        TINYPY_SYMBOL_VISIT(st, expr, e->v.IfExp.test);
        TINYPY_SYMBOL_VISIT(st, expr, e->v.IfExp.body);
        TINYPY_SYMBOL_VISIT(st, expr, e->v.IfExp.orelse);
        break;
    case TINYPY_AST_KIND_DICT:
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, e->v.Dict.keys);
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, e->v.Dict.values);
        break;
    case TINYPY_AST_KIND_SET:
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, e->v.Set.elts);
        break;
    case TINYPY_AST_KIND_LIST_COMP:
        if (!__tinypy_symbol_visit_listcomp(st, e)) {
            return 0;
        }
        break;
    case TINYPY_AST_KIND_GENERATOR_EXP:
        if (!__tinypy_symbol_visit_genexp(st, e)) {
            return 0;
        }
        break;
    case TINYPY_AST_KIND_SET_COMP:
        if (!__tinypy_symbol_visit_setcomp(st, e)) {
            return 0;
        }
        break;
    case TINYPY_AST_KIND_DICT_COMP:
        if (!__tinypy_symbol_visit_dictcomp(st, e)) {
            return 0;
        }
        break;
    case TINYPY_AST_KIND_YIELD:
        if (e->v.Yield.value) {
            TINYPY_SYMBOL_VISIT(st, expr, e->v.Yield.value);
        }
        st->current->generator = 1;
        if (st->current->returns_value) {
            tinypy_internal_compiler_error(st->arena, TINYPY_ERROR_SYNTAX, TINYPY_SYMBOL_RETURN_VALUE_IN_GENERATOR, e->lineno, 1, st->arena->out_error);
            return 0;
        }
        break;
    case TINYPY_AST_KIND_COMPARE:
        TINYPY_SYMBOL_VISIT(st, expr, e->v.Compare.left);
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, e->v.Compare.comparators);
        break;
    case TINYPY_AST_KIND_CALL:
        TINYPY_SYMBOL_VISIT(st, expr, e->v.Call.func);
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, e->v.Call.args);
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, keyword, e->v.Call.keywords);
        if (e->v.Call.starargs) {
            TINYPY_SYMBOL_VISIT(st, expr, e->v.Call.starargs);
        }
        if (e->v.Call.kwargs) {
            TINYPY_SYMBOL_VISIT(st, expr, e->v.Call.kwargs);
        }
        break;
    case TINYPY_AST_KIND_REPR:
        TINYPY_SYMBOL_VISIT(st, expr, e->v.Repr.value);
        break;
    case TINYPY_AST_KIND_NUM:
    case TINYPY_AST_KIND_STR:
        /* Nothing to do here. */
        break;
    /* The following exprs can be assignment targets. */
    case TINYPY_AST_KIND_ATTRIBUTE:
        TINYPY_SYMBOL_VISIT(st, expr, e->v.Attribute.value);
        break;
    case TINYPY_AST_KIND_SUBSCRIPT:
        TINYPY_SYMBOL_VISIT(st, expr, e->v.Subscript.value);
        TINYPY_SYMBOL_VISIT(st, slice, e->v.Subscript.slice);
        break;
    case TINYPY_AST_KIND_NAME:
        if (!__tinypy_symbol_add_def(st, e->v.Name.id,
                                     e->v.Name.ctx == TINYPY_AST_CONTEXT_LOAD ? TINYPY_SYMBOL_USE : TINYPY_SYMBOL_DEFINITION_LOCAL)) {
            return 0;
        }
        break;
    /* child nodes of List and Tuple will have expr_context set */
    case TINYPY_AST_KIND_LIST:
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, e->v.List.elts);
        break;
    case TINYPY_AST_KIND_TUPLE:
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, e->v.Tuple.elts);
        break;
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_implicit_arg(tinypy_symbol_table_t *st, int pos) {
    tinypy_value_t *id = __tinypy_frontend_format_identifier(st->symbols, ".", pos, "");
    if (id == NULL) {
        return 0;
    }
    if (!__tinypy_symbol_add_def(st, id, TINYPY_SYMBOL_DEFINITION_PARAMETER)) {
        TINYPY_COMPILER_DECREF(id);
        return 0;
    }
    TINYPY_COMPILER_DECREF(id);
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_params(tinypy_symbol_table_t *st, tinypy_ast_sequence_t *args, int toplevel) {
    int i;

    /* go through all the toplevel arguments first */
    for (i = 0; i < TINYPY_AST_SEQUENCE_LENGTH(args); i++) {
        tinypy_ast_expression_t arg = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(args, i);
        if (arg->kind == TINYPY_AST_KIND_NAME) {
            assert(arg->v.Name.ctx == TINYPY_AST_CONTEXT_PARAMETER || (arg->v.Name.ctx == TINYPY_AST_CONTEXT_STORE && !toplevel));
            if (!__tinypy_symbol_add_def(st, arg->v.Name.id, TINYPY_SYMBOL_DEFINITION_PARAMETER)) {
                return 0;
            }
        }
        else if (arg->kind == TINYPY_AST_KIND_TUPLE) {
            assert(arg->v.Tuple.ctx == TINYPY_AST_CONTEXT_STORE);
            if (toplevel) {
                if (!__tinypy_symbol_implicit_arg(st, i)) {
                    return 0;
                }
            }
        }
        else {
            tinypy_internal_compiler_error(st->arena, TINYPY_ERROR_SYNTAX, "invalid expression in parameter list", st->current->line_number, 1, st->arena->out_error);
            return 0;
        }
    }

    if (!toplevel) {
        if (!__tinypy_symbol_visit_params_nested(st, args)) {
            return 0;
        }
    }

    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_params_nested(tinypy_symbol_table_t *st, tinypy_ast_sequence_t *args) {
    int i;
    for (i = 0; i < TINYPY_AST_SEQUENCE_LENGTH(args); i++) {
        tinypy_ast_expression_t arg = (tinypy_ast_expression_t)TINYPY_AST_SEQUENCE_GET(args, i);
        if (arg->kind == TINYPY_AST_KIND_TUPLE && !__tinypy_symbol_visit_params(st, arg->v.Tuple.elts, 0)) {
            return 0;
        }
    }

    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_arguments(tinypy_symbol_table_t *st, tinypy_ast_arguments_t a) {
    /* skip default arguments inside function block
       XXX should ast be different?
    */
    if (a->args && !__tinypy_symbol_visit_params(st, a->args, 1)) {
        return 0;
    }
    if (a->vararg) {
        if (!__tinypy_symbol_add_def(st, a->vararg, TINYPY_SYMBOL_DEFINITION_PARAMETER)) {
            return 0;
        }
        st->current->variable_arguments = 1;
    }
    if (a->kwarg) {
        if (!__tinypy_symbol_add_def(st, a->kwarg, TINYPY_SYMBOL_DEFINITION_PARAMETER)) {
            return 0;
        }
        st->current->variable_keywords = 1;
    }
    if (a->args && !__tinypy_symbol_visit_params_nested(st, a->args)) {
        return 0;
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_excepthandler(tinypy_symbol_table_t *st, tinypy_ast_exception_handler_t eh) {
    if (eh->v.ExceptHandler.type) {
        TINYPY_SYMBOL_VISIT(st, expr, eh->v.ExceptHandler.type);
    }
    if (eh->v.ExceptHandler.name) {
        TINYPY_SYMBOL_VISIT(st, expr, eh->v.ExceptHandler.name);
    }
    TINYPY_SYMBOL_VISIT_SEQUENCE(st, stmt, eh->v.ExceptHandler.body);
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_alias(tinypy_symbol_table_t *st, tinypy_ast_alias_t a) {
    /* Compute store_name, the name actually bound by the import
       operation.  It is different than a->name when a->name is a
       dotted package name (e.g. spam.eggs)
    */
    tinypy_value_t *store_name;
    tinypy_value_t *name = (a->asname == NULL) ? a->name : a->asname;
    const char *base = TINYPY_COMPILER_STRING_AS_STRING(name);
    char *dot = strchr(base, '.');
    if (dot) {
        store_name = __tinypy_frontend_string_from_owner(name, base, (size_t)(dot - base));
        if (!store_name) {
            return 0;
        }
    }
    else {
        store_name = name;
        TINYPY_COMPILER_INCREF(store_name);
    }
    if (strcmp(TINYPY_COMPILER_STRING_AS_STRING(name), "*")) {
        int r = __tinypy_symbol_add_def(st, store_name, TINYPY_SYMBOL_DEFINITION_IMPORT);
        TINYPY_COMPILER_DECREF(store_name);
        return r;
    }
    else {
        if (st->current->block_type != TINYPY_SYMBOL_BLOCK_MODULE && !__tinypy_symbol_warn(st, TINYPY_COMPILER_EXC_SYNTAX_WARNING, TINYPY_SYMBOL_IMPORT_STAR_WARNING, -1)) {
            TINYPY_COMPILER_DECREF(store_name);
            return 0;
        }
        st->current->unoptimized |= TINYPY_SYMBOL_OPTIMIZATION_IMPORT_STAR;
        TINYPY_COMPILER_DECREF(store_name);
        return 1;
    }
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_comprehension(tinypy_symbol_table_t *st, tinypy_ast_comprehension_t lc) {
    TINYPY_SYMBOL_VISIT(st, expr, lc->target);
    TINYPY_SYMBOL_VISIT(st, expr, lc->iter);
    TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, lc->ifs);
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_keyword(tinypy_symbol_table_t *st, tinypy_ast_keyword_t k) {
    TINYPY_SYMBOL_VISIT(st, expr, k->value);
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_slice(tinypy_symbol_table_t *st, tinypy_ast_slice_t s) {
    switch (s->kind) {
    case TINYPY_AST_KIND_SLICE:
        if (s->v.Slice.lower) {
            TINYPY_SYMBOL_VISIT(st, expr, s->v.Slice.lower)
        }
        if (s->v.Slice.upper) {
            TINYPY_SYMBOL_VISIT(st, expr, s->v.Slice.upper)
        }
        if (s->v.Slice.step) {
            TINYPY_SYMBOL_VISIT(st, expr, s->v.Slice.step)
        }
        break;
    case TINYPY_AST_KIND_EXT_SLICE:
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, slice, s->v.ExtSlice.dims)
        break;
    case TINYPY_AST_KIND_INDEX:
        TINYPY_SYMBOL_VISIT(st, expr, s->v.Index.value)
        break;
    case TINYPY_AST_KIND_ELLIPSIS:
        break;
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_new_tmpname(tinypy_symbol_table_t *st) {
    tinypy_ast_identifier_t tmp;

    tmp = __tinypy_frontend_format_identifier(st->symbols, "_[", ++st->current->temporary_name_index, "]");
    if (!tmp) {
        return 0;
    }
    if (!__tinypy_symbol_add_def(st, tmp, TINYPY_SYMBOL_DEFINITION_LOCAL)) {
        return 0;
    }
    TINYPY_COMPILER_DECREF(tmp);
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_handle_comprehension(tinypy_symbol_table_t *st, tinypy_ast_expression_t e, tinypy_ast_identifier_t scope_name, tinypy_ast_sequence_t *generators, tinypy_ast_expression_t elt, tinypy_ast_expression_t value) {
    int is_generator = (e->kind == TINYPY_AST_KIND_GENERATOR_EXP);
    int needs_tmp = !is_generator;
    tinypy_ast_comprehension_t outermost = ((tinypy_ast_comprehension_t)
                                                TINYPY_AST_SEQUENCE_GET(generators, 0));
    /* Outermost iterator is evaluated in current scope */
    TINYPY_SYMBOL_VISIT(st, expr, outermost->iter);
    /* Create comprehension scope for the rest */
    if (!scope_name || !__tinypy_symbol_enter_block(st, scope_name, TINYPY_SYMBOL_BLOCK_FUNCTION, (void *)e, 0)) {
        return 0;
    }
    /* To inspect yield expressions independently, clear
       the generator flag, and restore it at the end */
    is_generator |= st->current->generator;
    st->current->generator = 0;
    /* Outermost iter is received as an argument */
    if (!__tinypy_symbol_implicit_arg(st, 0)) {
        __tinypy_symbol_exit_block(st, (void *)e);
        return 0;
    }
    /* Allocate temporary name if needed */
    if (needs_tmp && !__tinypy_symbol_new_tmpname(st)) {
        __tinypy_symbol_exit_block(st, (void *)e);
        return 0;
    }
    TINYPY_SYMBOL_VISIT_IN_BLOCK(st, expr, outermost->target, (void *)e);
    TINYPY_SYMBOL_VISIT_SEQUENCE_IN_BLOCK(st, expr, outermost->ifs, (void *)e);
    TINYPY_SYMBOL_VISIT_SEQUENCE_TAIL_IN_BLOCK(st, comprehension,
                                               generators, 1, (void *)e);
    if (value) {
        TINYPY_SYMBOL_VISIT_IN_BLOCK(st, expr, value, (void *)e);
    }
    TINYPY_SYMBOL_VISIT_IN_BLOCK(st, expr, elt, (void *)e);
    st->current->generator |= is_generator;
    return __tinypy_symbol_exit_block(st, (void *)e);
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_listcomp(tinypy_symbol_table_t *st, tinypy_ast_expression_t e) {
    tinypy_ast_sequence_t *generators = e->v.ListComp.generators;
    int i, is_generator;
    /* To inspect yield expressions independently, clear
       the generator flag, and restore it at the end */
    is_generator = st->current->generator;
    st->current->generator = 0;
    TINYPY_SYMBOL_VISIT(st, expr, e->v.ListComp.elt);
    for (i = 0; i < TINYPY_AST_SEQUENCE_LENGTH(generators); i++) {
        tinypy_ast_comprehension_t lc = (tinypy_ast_comprehension_t)TINYPY_AST_SEQUENCE_GET(generators, i);
        TINYPY_SYMBOL_VISIT(st, expr, lc->target);
        if (i == 0 && !st->current->generator) {
            /* 'yield' in the outermost iterator doesn't cause a warning */
            TINYPY_SYMBOL_VISIT(st, expr, lc->iter);
            is_generator |= st->current->generator;
            st->current->generator = 0;
        }
        else {
            TINYPY_SYMBOL_VISIT(st, expr, lc->iter);
        }
        TINYPY_SYMBOL_VISIT_SEQUENCE(st, expr, lc->ifs);
    }

    st->current->generator |= is_generator;
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_genexp(tinypy_symbol_table_t *st, tinypy_ast_expression_t e) {
    tinypy_value_t *symbol_identifier = __tinypy_symbol_identifier(st, &st->generator_expression_name, "genexpr");
    return __tinypy_symbol_handle_comprehension(st, e, symbol_identifier,
                                                e->v.GeneratorExp.generators,
                                                e->v.GeneratorExp.elt, NULL);
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_setcomp(tinypy_symbol_table_t *st, tinypy_ast_expression_t e) {
    tinypy_value_t *symbol_identifier = __tinypy_symbol_identifier(st, &st->set_comprehension_name, "setcomp");
    return __tinypy_symbol_handle_comprehension(st, e, symbol_identifier,
                                                e->v.SetComp.generators,
                                                e->v.SetComp.elt, NULL);
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_symbol_visit_dictcomp(tinypy_symbol_table_t *st, tinypy_ast_expression_t e) {
    tinypy_value_t *symbol_identifier = __tinypy_symbol_identifier(st, &st->dict_comprehension_name, "dictcomp");
    return __tinypy_symbol_handle_comprehension(st, e, symbol_identifier,
                                                e->v.DictComp.generators,
                                                e->v.DictComp.key,
                                                e->v.DictComp.value);
}
