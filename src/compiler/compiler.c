#include "internal.h"
#include "api_internal.h"

#include "tinypy/code.h"
#include "tinypy/eval.h"
#include "tinypy/preprocessor.h"
#include "opcode.h"
#include "tinypy/tuple.h"
#include "tinypy/value.h"

#include "value_ops.h"
#include "ast_nodes.h"
#include "parser_error.h"
#include "grammar.h"
#include "grammar_symbols.h"
#include "cst.h"
#include "ast_builder.h"
#include "codegen.h"
#include "source_parser.h"
#include "symbol_table.h"
#include "token.h"

#include <string.h>

#define TINYPY_COMPILE_DEFAULT_SOURCE_BYTES ((size_t)16777216U)
#define TINYPY_COMPILE_DEFAULT_ITEMS ((size_t)4194304U)
#define TINYPY_COMPILE_DEFAULT_NESTING ((size_t)1000U)
#define TINYPY_COMPILE_DEFAULT_INSTRUCTIONS ((size_t)16777216U)
#define TINYPY_COMPILE_DEFAULT_CONSTANT_BYTES ((size_t)67108864U)
#define TINYPY_COMPILE_DEFAULT_ARENA_BYTES ((size_t)268435456U)
#define TINYPY_COMPILE_DEFAULT_PREPROCESSOR_OPERATIONS ((size_t)4194304U)
#define TINYPY_COMPILE_DEFAULT_PREPROCESSOR_VALUE_NODES ((size_t)1048576U)
#define TINYPY_COMPILE_DEFAULT_PREPROCESSOR_BYTES ((size_t)67108864U)
#define TINYPY_COMPILE_DEFAULT_TEMPLATE_EXPANSIONS ((size_t)65536U)
#define TINYPY_COMPILE_DEFAULT_TEMPLATE_DEPTH ((size_t)128U)
#define TINYPY_COMPILE_DEFAULT_GENERATED_AST_NODES ((size_t)4194304U)
#define TINYPY_COMPILE_DEFAULT_GENERATED_SOURCE_BYTES ((size_t)67108864U)
#define TINYPY_COMPILE_DEFAULT_SOURCE_MAP_ENTRIES ((size_t)1048576U)

//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_compiler_inherited_flags(const tinypy_compile_ctx_t *ctx) {
    static const uint32_t future_mask = (uint32_t)(TINYPY_CODE_FUTURE_DIVISION | TINYPY_CODE_FUTURE_ABSOLUTE_IMPORT | TINYPY_CODE_FUTURE_WITH_STATEMENT | TINYPY_CODE_FUTURE_PRINT_FUNCTION | TINYPY_CODE_FUTURE_UNICODE_LITERALS);
    tinypy_value_t *frame;

    if (ctx->options.dont_inherit != 0) {
        return ctx->options.flags;
    }
    frame = tinypy_vm_current_frame(ctx->vm);
    if (frame == NULL) {
        return ctx->options.flags;
    }
    tinypy_value_t *frame_code = tinypy_frame_code(frame);
    return ctx->options.flags | ((uint32_t)tinypy_code_flags(frame_code) & future_mask);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_compiler_source_is_empty_suite(const tinypy_compile_ctx_t *ctx) {
    size_t position = 0U;

    while (position < ctx->source.size) {
        unsigned char byte = ctx->source.bytes[position];

        if (byte == ' ' || byte == '\t' || byte == '\f' || byte == '\n') {
            position += 1U;
            continue;
        }
        if (byte == '#') {
            while (position < ctx->source.size && ctx->source.bytes[position] != '\n') {
                position += 1U;
            }
            continue;
        }
        if (position + 4U <= ctx->source.size && memcmp(ctx->source.bytes + position, "pass", 4U) == 0) {
            position += 4U;
            continue;
        }
        return 0;
    }
    return 1;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_compiler_empty_code(tinypy_compile_ctx_t *ctx) {
    unsigned char instructions[4];
    tinypy_value_t *none;
    tinypy_value_t *bytecode;
    tinypy_value_t *consts;
    tinypy_value_t *empty;
    tinypy_value_t *filename;
    tinypy_value_t *name;
    tinypy_value_t *lnotab;
    tinypy_value_t *code;
    uint32_t flags = __tinypy_compiler_inherited_flags(ctx) | (uint32_t)TINYPY_CODE_NO_FREE;

    instructions[0] = (unsigned char)TINYPY_OP_LOAD_CONST;
    instructions[1] = 0U;
    instructions[2] = 0U;
    instructions[3] = (unsigned char)TINYPY_OP_RETURN_VALUE;
    none = tinypy_none_get(ctx->vm);
    bytecode = tinypy_string_from_bytes(ctx->vm, instructions, sizeof(instructions));
    consts = tinypy_tuple_from_items(ctx->vm, &none, 1U);
    empty = tinypy_tuple_from_items(ctx->vm, NULL, 0U);
    filename = tinypy_string_from_bytes(ctx->vm, ctx->logical_filename, ctx->filename_size);
    name = tinypy_string_from_bytes(ctx->vm, "<module>", 8U);
    tinypy_internal_string_set_interned(name, 1);
    lnotab = tinypy_string_from_bytes(ctx->vm, NULL, 0U);
    code = tinypy_code_new(0, 0, 1, (int32_t)flags, bytecode, consts, empty, empty, empty, empty, filename, name, 1, lnotab);
    tinypy_release(lnotab);
    tinypy_release(name);
    tinypy_release(filename);
    tinypy_release(empty);
    tinypy_release(consts);
    tinypy_release(bytecode);
    tinypy_release(none);
    return code;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_compiler_parser_start(tinypy_compile_mode_e mode) {
    if (mode == TINYPY_COMPILE_EXEC) {
        return TINYPY_GRAMMAR_FILE_INPUT;
    }
    if (mode == TINYPY_COMPILE_EVAL) {
        return TINYPY_GRAMMAR_EVAL_INPUT;
    }
    assert(mode == TINYPY_COMPILE_SINGLE);
    return TINYPY_GRAMMAR_SINGLE_INPUT;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_compiler_parser_flags(uint32_t compile_flags) {
    int flags = 0;

    if ((compile_flags & (uint32_t)TINYPY_CODE_FUTURE_PRINT_FUNCTION) != 0U) {
        flags |= TINYPY_PARSER_FLAG_PRINT_IS_FUNCTION;
    }
    if ((compile_flags & (uint32_t)TINYPY_CODE_FUTURE_UNICODE_LITERALS) != 0U) {
        flags |= TINYPY_PARSER_FLAG_UNICODE_LITERALS;
    }
    if ((compile_flags & (uint32_t)TINYPY_COMPILE_FLAG_DONT_IMPLY_DEDENT) != 0U) {
        flags |= TINYPY_PARSER_FLAG_DONT_IMPLY_DEDENT;
    }
    return flags;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_error_kind_e __tinypy_compiler_parser_error_kind(int error, int token, int expected) {
    if (error == TINYPY_PARSER_TAB_SPACE_ERROR) {
        return TINYPY_ERROR_TAB;
    }
    if (error == TINYPY_PARSER_DEDENT_ERROR || error == TINYPY_PARSER_TOO_DEEP) {
        return TINYPY_ERROR_INDENTATION;
    }
    if (error == TINYPY_PARSER_SYNTAX_ERROR && (token == TINYPY_TOKEN_INDENT || expected == TINYPY_TOKEN_INDENT)) {
        return TINYPY_ERROR_INDENTATION;
    }
    if (error == TINYPY_PARSER_DECODE_ERROR) {
        return TINYPY_ERROR_SOURCE_DECODING;
    }
    if (error == TINYPY_PARSER_OUT_OF_MEMORY || error == TINYPY_PARSER_OVERFLOW) {
        return TINYPY_ERROR_COMPILER_LIMIT;
    }
    return TINYPY_ERROR_SYNTAX;
}

//////////////////////////////////////////////////////////////////////////
static const char *__tinypy_compiler_parser_error_message(int error, int token, int expected) {
    if (error == TINYPY_PARSER_EOF) {
        return "unexpected EOF while parsing";
    }
    if (error == TINYPY_PARSER_BAD_TOKEN) {
        return "invalid token";
    }
    if (error == TINYPY_PARSER_TAB_SPACE_ERROR) {
        return "inconsistent use of tabs and spaces in indentation";
    }
    if (error == TINYPY_PARSER_TOO_DEEP) {
        return "too many levels of indentation";
    }
    if (error == TINYPY_PARSER_DEDENT_ERROR) {
        return "unindent does not match any outer indentation level";
    }
    if (error == TINYPY_PARSER_DECODE_ERROR) {
        return "source decoding failed";
    }
    if (error == TINYPY_PARSER_EOF_TRIPLE_STRING) {
        return "EOF while scanning triple-quoted string literal";
    }
    if (error == TINYPY_PARSER_EOL_STRING) {
        return "EOL while scanning string literal";
    }
    if (error == TINYPY_PARSER_LINE_CONTINUATION_ERROR) {
        return "unexpected character after line continuation character";
    }
    if (error == TINYPY_PARSER_OUT_OF_MEMORY || error == TINYPY_PARSER_OVERFLOW) {
        return "parser exceeds compiler limits";
    }
    if (error == TINYPY_PARSER_SYNTAX_ERROR && token == TINYPY_TOKEN_INDENT) {
        return "unexpected indent";
    }
    if (error == TINYPY_PARSER_SYNTAX_ERROR && expected == TINYPY_TOKEN_INDENT) {
        return "expected an indented block";
    }
    return "invalid syntax";
}

//////////////////////////////////////////////////////////////////////////
static tinypy_cst_node_t *__tinypy_compiler_parse(tinypy_compile_ctx_t *ctx, tinypy_error_t **out_error) {
    tinypy_parser_error_detail_t detail;
    uint32_t compiler_inherited_flags = __tinypy_compiler_inherited_flags(ctx);
    int flags = __tinypy_compiler_parser_flags(compiler_inherited_flags);
    tinypy_cst_node_t *tree;

    int compiler_parser_start = __tinypy_compiler_parser_start(ctx->options.mode);
    tree = tinypy_internal_parse_source(ctx, (const char *)ctx->source.bytes, ctx->source.size, ctx->logical_filename, &__tinypy_parser_grammar, compiler_parser_start, &detail, &flags);
    if (tree != NULL) {
        return tree;
    }
    tinypy_error_kind_e compiler_parser_error_kind = __tinypy_compiler_parser_error_kind(detail.result, detail.token, detail.expected);
    const char *compiler_parser_error_message = __tinypy_compiler_parser_error_message(detail.result, detail.token, detail.expected);
    tinypy_internal_compiler_error(ctx, compiler_parser_error_kind, compiler_parser_error_message, detail.line_number > 0 ? detail.line_number : 1, detail.offset >= 0 ? detail.offset : 1, out_error);
    return NULL;
}

//////////////////////////////////////////////////////////////////////////
void tinypy_compile_limits_init(tinypy_compile_limits_t *limits) {
    assert(limits != NULL);
    (void)memset(limits, 0, sizeof(*limits));
    limits->abi_version = TINYPY_COMPILER_ABI_VERSION;
    limits->struct_size = (uint32_t)sizeof(*limits);
    limits->max_source_bytes = TINYPY_COMPILE_DEFAULT_SOURCE_BYTES;
    limits->max_tokens = TINYPY_COMPILE_DEFAULT_ITEMS;
    limits->max_cst_nodes = TINYPY_COMPILE_DEFAULT_ITEMS;
    limits->max_ast_nodes = TINYPY_COMPILE_DEFAULT_ITEMS;
    limits->max_nesting = TINYPY_COMPILE_DEFAULT_NESTING;
    limits->max_symbols = TINYPY_COMPILE_DEFAULT_ITEMS;
    limits->max_blocks = TINYPY_COMPILE_DEFAULT_ITEMS;
    limits->max_instructions = TINYPY_COMPILE_DEFAULT_INSTRUCTIONS;
    limits->max_constants = TINYPY_COMPILE_DEFAULT_ITEMS;
    limits->max_constant_bytes = TINYPY_COMPILE_DEFAULT_CONSTANT_BYTES;
    limits->max_arena_bytes = TINYPY_COMPILE_DEFAULT_ARENA_BYTES;
    limits->max_preprocessor_operations = TINYPY_COMPILE_DEFAULT_PREPROCESSOR_OPERATIONS;
    limits->max_preprocessor_value_nodes = TINYPY_COMPILE_DEFAULT_PREPROCESSOR_VALUE_NODES;
    limits->max_preprocessor_bytes = TINYPY_COMPILE_DEFAULT_PREPROCESSOR_BYTES;
    limits->max_template_expansions = TINYPY_COMPILE_DEFAULT_TEMPLATE_EXPANSIONS;
    limits->max_template_depth = TINYPY_COMPILE_DEFAULT_TEMPLATE_DEPTH;
    limits->max_generated_ast_nodes = TINYPY_COMPILE_DEFAULT_GENERATED_AST_NODES;
    limits->max_generated_source_bytes = TINYPY_COMPILE_DEFAULT_GENERATED_SOURCE_BYTES;
    limits->max_source_map_entries = TINYPY_COMPILE_DEFAULT_SOURCE_MAP_ENTRIES;
}

//////////////////////////////////////////////////////////////////////////
void tinypy_compile_options_init(tinypy_compile_options_t *options, tinypy_compile_mode_e mode) {
    assert(options != NULL);
    assert(mode >= TINYPY_COMPILE_EXEC && mode <= TINYPY_COMPILE_SINGLE);
    (void)memset(options, 0, sizeof(*options));
    options->abi_version = TINYPY_COMPILER_ABI_VERSION;
    options->struct_size = (uint32_t)sizeof(*options);
    options->mode = mode;
    options->dont_inherit = 0;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_compiler_compile(tinypy_compile_ctx_t *ctx, tinypy_error_t **out_error) {
    tinypy_cst_node_t *tree;
    tinypy_compiler_flags_t flags;
    tinypy_ast_module_t module;
    tinypy_future_features_t *future;
    tinypy_symbol_table_t *symbols;
    tinypy_code_object_t *code;

    if (ctx->options.mode == TINYPY_COMPILE_EXEC && __tinypy_compiler_source_is_empty_suite(ctx) != 0) {
        return __tinypy_compiler_empty_code(ctx);
    }
    if (ctx->options.mode == TINYPY_COMPILE_SINGLE && __tinypy_compiler_source_is_empty_suite(ctx) != 0 && ctx->source.size > 1U) {
        return __tinypy_compiler_empty_code(ctx);
    }
    tree = __tinypy_compiler_parse(ctx, out_error);
    if (tree == NULL) {
        return NULL;
    }
    flags.flags = (int)__tinypy_compiler_inherited_flags(ctx) | TINYPY_COMPILER_FLAG_SOURCE_IS_UTF8;
    module = __tinypy_ast_build(tree, &flags, ctx->logical_filename, ctx);
    if (module == NULL) {
        if (ctx->failed == 0) {
            tinypy_internal_compiler_error(ctx, TINYPY_ERROR_SYNTAX, "unable to construct compiler AST", 1, 1, out_error);
        }
        return NULL;
    }
    future = __tinypy_future_scan(ctx, module, ctx->logical_filename);
    if (future == NULL) {
        if (ctx->failed == 0) {
            tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "future scan exceeds compiler arena limit", 1, 1, out_error);
        }
        return NULL;
    }
    if ((ctx->options.feature_flags & (uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR) != 0U && tinypy_internal_preprocessor_transform(ctx, module, (uint32_t)future->features | (uint32_t)flags.flags) == 0) {
        if (ctx->failed == 0) {
            tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "preprocessor exceeds compiler limits", 1, 1, out_error);
        }
        return NULL;
    }
    if ((ctx->options.feature_flags & (uint32_t)TINYPY_COMPILE_FEATURE_META) != 0U && tinypy_internal_meta_transform(ctx, module) == 0) {
        if (ctx->failed == 0) {
            tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "meta expansion exceeds compiler limits", 1, 1, out_error);
        }
        return NULL;
    }
    symbols = __tinypy_symbol_table_build(ctx, module, ctx->logical_filename, future);
    if (symbols == NULL) {
        if (ctx->failed == 0) {
            tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "symbol table exceeds compiler limits", 1, 1, out_error);
        }
        return NULL;
    }
    code = __tinypy_ast_compile(ctx, module, ctx->logical_filename, &flags, future, symbols);
    if (code == NULL && ctx->failed == 0) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "code generation exceeds compiler limits", 1, 1, out_error);
    }
    return (tinypy_value_t *)code;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_compiler_compile_source(tinypy_vm_t *vm, const void *source, size_t source_size, int32_t source_is_unicode, const char *logical_filename, size_t filename_size, const tinypy_compile_options_t *options, tinypy_error_t **out_error) {
    tinypy_compile_ctx_t ctx;
    tinypy_value_t *code = NULL;

    assert(tinypy_internal_vm_valid(vm));
    assert(source != NULL || source_size == 0U);
    assert(logical_filename != NULL || filename_size == 0U);
    assert(options != NULL);
    assert(options->abi_version == TINYPY_COMPILER_ABI_VERSION);
    assert(options->struct_size >= (uint32_t)sizeof(*options));
    assert(options->mode >= TINYPY_COMPILE_EXEC && options->mode <= TINYPY_COMPILE_SINGLE);
    assert(options->optimize_level >= 0 && options->optimize_level <= 2);
    assert((options->feature_flags & ~((uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR | (uint32_t)TINYPY_COMPILE_FEATURE_META)) == 0U);
    assert((options->feature_flags & (uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR) == 0U ? options->build_profile == NULL : options->build_profile != NULL);
    assert(options->build_profile == NULL || tinypy_build_profile_optimize_level(options->build_profile) == options->optimize_level);
    tinypy_internal_clear_error(out_error);
    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.vm = vm;
    ctx.options = *options;
    ctx.logical_filename = logical_filename;
    ctx.filename_size = filename_size;
    ctx.source_is_unicode = source_is_unicode;
    ctx.out_error = out_error;
    if (options->limits != NULL) {
        assert(options->limits->abi_version == TINYPY_COMPILER_ABI_VERSION);
        assert(options->limits->struct_size >= (uint32_t)sizeof(*options->limits));
        ctx.limits = *options->limits;
    }
    else {
        tinypy_compile_limits_init(&ctx.limits);
    }
    if (tinypy_internal_compiler_source_prepare(&ctx, source, source_size, out_error) != 0) {
        code = tinypy_internal_compiler_compile(&ctx, out_error);
    }
    if (code != NULL) {
        tinypy_internal_code_attach_compile_options(code, options->feature_flags, options->optimize_level, options->build_profile);
    }
    tinypy_internal_compiler_arena_destroy(&ctx);
    return code;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_compile_source(tinypy_vm_t *vm, const void *source, size_t source_size, const char *logical_filename, size_t filename_size, const tinypy_compile_options_t *options, tinypy_error_t **out_error) {
    return tinypy_internal_compiler_compile_source(vm, source, source_size, 0, logical_filename, filename_size, options, out_error);
}

//////////////////////////////////////////////////////////////////////////
tinypy_preprocess_result_t *tinypy_preprocess_source(tinypy_vm_t *vm, const void *source, size_t source_size, const char *logical_filename, size_t filename_size, const tinypy_compile_options_t *options, tinypy_error_t **out_error) {
    tinypy_compile_ctx_t ctx;
    tinypy_preprocess_result_t *result = NULL;
    tinypy_cst_node_t *tree;
    tinypy_compiler_flags_t flags;
    tinypy_ast_module_t module;
    tinypy_future_features_t *future;

    assert(tinypy_internal_vm_valid(vm));
    assert(source != NULL || source_size == 0U);
    assert(logical_filename != NULL || filename_size == 0U);
    assert(options != NULL);
    assert(options->abi_version == TINYPY_COMPILER_ABI_VERSION);
    assert(options->struct_size >= (uint32_t)sizeof(*options));
    assert(options->mode >= TINYPY_COMPILE_EXEC && options->mode <= TINYPY_COMPILE_SINGLE);
    assert(options->optimize_level >= 0 && options->optimize_level <= 2);
    assert((options->feature_flags & ~((uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR | (uint32_t)TINYPY_COMPILE_FEATURE_META)) == 0U);
    assert((options->feature_flags & (uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR) == 0U ? options->build_profile == NULL : options->build_profile != NULL);
    assert(options->build_profile == NULL || tinypy_build_profile_optimize_level(options->build_profile) == options->optimize_level);
    tinypy_internal_clear_error(out_error);
    (void)memset(&ctx, 0, sizeof(ctx));
    ctx.vm = vm;
    ctx.options = *options;
    ctx.logical_filename = logical_filename;
    ctx.filename_size = filename_size;
    ctx.out_error = out_error;
    if (options->limits != NULL) {
        assert(options->limits->abi_version == TINYPY_COMPILER_ABI_VERSION);
        assert(options->limits->struct_size >= (uint32_t)sizeof(*options->limits));
        ctx.limits = *options->limits;
    }
    else {
        tinypy_compile_limits_init(&ctx.limits);
    }
    if (tinypy_internal_compiler_source_prepare(&ctx, source, source_size, out_error) == 0) {
        goto complete;
    }
    tree = __tinypy_compiler_parse(&ctx, out_error);
    if (tree == NULL) {
        goto complete;
    }
    flags.flags = (int)__tinypy_compiler_inherited_flags(&ctx) | TINYPY_COMPILER_FLAG_SOURCE_IS_UTF8;
    module = __tinypy_ast_build(tree, &flags, ctx.logical_filename, &ctx);
    if (module == NULL) {
        if (ctx.failed == 0) {
            tinypy_internal_compiler_error(&ctx, TINYPY_ERROR_SYNTAX, "unable to construct compiler AST", 1, 1, out_error);
        }
        goto complete;
    }
    future = __tinypy_future_scan(&ctx, module, ctx.logical_filename);
    if (future == NULL) {
        if (ctx.failed == 0) {
            tinypy_internal_compiler_error(&ctx, TINYPY_ERROR_COMPILER_LIMIT, "future scan exceeds compiler arena limit", 1, 1, out_error);
        }
        goto complete;
    }
    if ((ctx.options.feature_flags & (uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR) != 0U && tinypy_internal_preprocessor_transform(&ctx, module, (uint32_t)future->features | (uint32_t)flags.flags) == 0) {
        if (ctx.failed == 0) {
            tinypy_internal_compiler_error(&ctx, TINYPY_ERROR_COMPILER_LIMIT, "preprocessor exceeds compiler limits", 1, 1, out_error);
        }
        goto complete;
    }
    if ((ctx.options.feature_flags & (uint32_t)TINYPY_COMPILE_FEATURE_META) != 0U && tinypy_internal_meta_transform(&ctx, module) == 0) {
        if (ctx.failed == 0) {
            tinypy_internal_compiler_error(&ctx, TINYPY_ERROR_COMPILER_LIMIT, "meta expansion exceeds compiler limits", 1, 1, out_error);
        }
        goto complete;
    }
    result = tinypy_internal_preprocessor_render(&ctx, module);

complete:
    tinypy_internal_compiler_arena_destroy(&ctx);
    return result;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_compiler_run_source(tinypy_vm_t *vm, const void *source, size_t source_size, const char *logical_filename, size_t filename_size, tinypy_value_t *globals, tinypy_value_t *locals, const tinypy_compile_options_t *options, tinypy_compile_mode_e mode, int32_t discard_result, tinypy_error_t **out_error) {
    tinypy_compile_options_t local_options = *options;
    tinypy_value_t *code;
    tinypy_value_t *result;

    local_options.mode = mode;
    code = tinypy_compile_source(vm, source, source_size, logical_filename, filename_size, &local_options, out_error);
    if (code == NULL) {
        return NULL;
    }
    result = tinypy_eval_code(code, globals, locals, out_error);
    tinypy_release(code);
    if (result == NULL) {
        return NULL;
    }
    if (discard_result != 0) {
        tinypy_release(result);
        result = tinypy_none_get(vm);
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_eval_source(tinypy_vm_t *vm, const void *source, size_t source_size, const char *logical_filename, size_t filename_size, tinypy_value_t *globals, tinypy_value_t *locals, const tinypy_compile_options_t *options, tinypy_error_t **out_error) {
    assert(tinypy_internal_vm_valid(vm));
    assert(globals != NULL);
    assert(tinypy_internal_value_belongs_to(vm, globals));
    assert(locals == NULL || tinypy_internal_value_belongs_to(vm, locals));
    assert(options != NULL);
    return __tinypy_compiler_run_source(vm, source, source_size, logical_filename, filename_size, globals, locals, options, TINYPY_COMPILE_EVAL, 0, out_error);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_exec_source(tinypy_vm_t *vm, const void *source, size_t source_size, const char *logical_filename, size_t filename_size, tinypy_value_t *globals, tinypy_value_t *locals, const tinypy_compile_options_t *options, tinypy_error_t **out_error) {
    assert(tinypy_internal_vm_valid(vm));
    assert(globals != NULL);
    assert(tinypy_internal_value_belongs_to(vm, globals));
    assert(locals == NULL || tinypy_internal_value_belongs_to(vm, locals));
    assert(options != NULL);
    return __tinypy_compiler_run_source(vm, source, source_size, logical_filename, filename_size, globals, locals, options, TINYPY_COMPILE_EXEC, 1, out_error);
}
