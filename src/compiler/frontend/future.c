#include "value_ops.h"
#include "ast_nodes.h"
#include "codegen.h"

#define TINYPY_FUTURE_UNDEFINED_FEATURE_FORMAT "future feature %.100s is not defined"
#define TINYPY_FUTURE_LATE_IMPORT_MESSAGE \
"from __future__ imports must occur at the beginning of the file"

static int
__tinypy_frontend_future_check_features(tinypy_compile_ctx_t *arena, tinypy_future_features_t *ff, tinypy_ast_statement_t s, const char *filename)
{
    int i;
    tinypy_ast_sequence_t *names;

    assert(s->kind == TINYPY_AST_KIND_IMPORT_FROM);

    names = s->v.ImportFrom.names;
    for (i = 0; i < TINYPY_AST_SEQUENCE_LENGTH(names); i++) {
        tinypy_ast_alias_t name = (tinypy_ast_alias_t)TINYPY_AST_SEQUENCE_GET(names, i);
        const char *feature = TINYPY_COMPILER_STRING_AS_STRING(name->name);
        if (!feature)
            return 0;
        if (strcmp(feature, TINYPY_FUTURE_FEATURE_NESTED_SCOPES) == 0) {
            continue;
        } else if (strcmp(feature, TINYPY_FUTURE_FEATURE_GENERATORS) == 0) {
            continue;
        } else if (strcmp(feature, TINYPY_FUTURE_FEATURE_DIVISION) == 0) {
            ff->features |= TINYPY_CODE_FUTURE_DIVISION;
        } else if (strcmp(feature, TINYPY_FUTURE_FEATURE_ABSOLUTE_IMPORT) == 0) {
            ff->features |= TINYPY_CODE_FUTURE_ABSOLUTE_IMPORT;
        } else if (strcmp(feature, TINYPY_FUTURE_FEATURE_WITH_STATEMENT) == 0) {
            ff->features |= TINYPY_CODE_FUTURE_WITH_STATEMENT;
        } else if (strcmp(feature, TINYPY_FUTURE_FEATURE_PRINT_FUNCTION) == 0) {
            ff->features |= TINYPY_CODE_FUTURE_PRINT_FUNCTION;
        } else if (strcmp(feature, TINYPY_FUTURE_FEATURE_UNICODE_LITERALS) == 0) {
            ff->features |= TINYPY_CODE_FUTURE_UNICODE_LITERALS;
        } else if (strcmp(feature, "braces") == 0) {
            tinypy_internal_compiler_error(arena, TINYPY_ERROR_SYNTAX, "not a chance", s->lineno, 1, arena->out_error);
            return 0;
        } else {
            static const char prefix[] = "future feature ";
            static const char suffix[] = " is not defined";
            const char *parts[] = {prefix, feature, suffix};
            size_t feature_size = strlen(feature);
            size_t part_sizes[] = {sizeof(prefix) - 1U, feature_size < 100U ? feature_size : 100U, sizeof(suffix) - 1U};

            (void)filename;
            tinypy_internal_compiler_error_parts(arena, TINYPY_ERROR_SYNTAX, parts, part_sizes, sizeof(parts) / sizeof(parts[0]), s->lineno, 1);
            return 0;
        }
    }
    return 1;
}

static int
__tinypy_frontend_future_parse(tinypy_compile_ctx_t *arena, tinypy_future_features_t *ff, tinypy_ast_module_t mod, const char *filename)
{
    int i, found_docstring = 0, done = 0, prev_line = 0;

    if (!(mod->kind == TINYPY_AST_KIND_MODULE || mod->kind == TINYPY_AST_KIND_INTERACTIVE))
        return 1;

    /* A subsequent pass will detect future imports that don't
       appear at the beginning of the file.  There's one case,
       however, that is easier to handle here: A series of imports
       joined by semi-colons, where the first import is a future
       statement but some subsequent import has the future form
       but is preceded by a regular import.
    */


    for (i = 0; i < TINYPY_AST_SEQUENCE_LENGTH(mod->v.Module.body); i++) {
        tinypy_ast_statement_t s = (tinypy_ast_statement_t)TINYPY_AST_SEQUENCE_GET(mod->v.Module.body, i);

        if (done && s->lineno > prev_line)
            return 1;
        prev_line = s->lineno;

        /* The tests below will return from this function unless it is
           still possible to find a future statement.  The only things
           that can precede a future statement are another future
           statement and a doc string.
        */

        if (s->kind == TINYPY_AST_KIND_IMPORT_FROM) {
            tinypy_ast_identifier_t modname = s->v.ImportFrom.module;
            if (modname && TINYPY_COMPILER_STRING_GET_SIZE(modname) == 10 &&
                !strcmp(TINYPY_COMPILER_STRING_AS_STRING(modname), "__future__")) {
                if (done) {
                    tinypy_internal_compiler_error(arena, TINYPY_ERROR_SYNTAX, TINYPY_FUTURE_LATE_IMPORT_MESSAGE, s->lineno, 1, arena->out_error);
                    return 0;
                }
                if (!__tinypy_frontend_future_check_features(arena, ff, s, filename))
                    return 0;
                ff->line_number = s->lineno;
            }
            else
                done = 1;
        }
        else if (s->kind == TINYPY_AST_KIND_EXPR && !found_docstring) {
            tinypy_ast_expression_t e = s->v.Expr.value;
            if (e->kind != TINYPY_AST_KIND_STR)
                done = 1;
            else
                found_docstring = 1;
        }
        else
            done = 1;
    }
    return 1;
}


tinypy_future_features_t *
__tinypy_future_scan(tinypy_compile_ctx_t *arena, tinypy_ast_module_t mod, const char *filename)
{
    tinypy_future_features_t *ff;

    ff = (tinypy_future_features_t *)TINYPY_COMPILER_ARENA_MALLOC(arena, sizeof(tinypy_future_features_t));
    if (ff == NULL) return NULL;
    ff->features = 0;
    ff->line_number = -1;

    if (!__tinypy_frontend_future_parse(arena, ff, mod, filename)) return NULL;
    return ff;
}
