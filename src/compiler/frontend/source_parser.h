#ifndef TINYPY_COMPILER_SOURCE_PARSER_H
#define TINYPY_COMPILER_SOURCE_PARSER_H

#include "parser_error.h"

typedef struct tinypy_parser_error_detail_t {
    tinypy_parser_result_e result;
    const char *filename;
    int32_t line_number;
    int32_t offset;
    char *text;
    int32_t token;
    int32_t expected;
} tinypy_parser_error_detail_t;

typedef enum tinypy_parser_flag_e {
    TINYPY_PARSER_FLAG_DONT_IMPLY_DEDENT = 0x0002,
    TINYPY_PARSER_FLAG_PRINT_IS_FUNCTION = 0x0004,
    TINYPY_PARSER_FLAG_UNICODE_LITERALS = 0x0008
} tinypy_parser_flag_e;

tinypy_cst_node_t *tinypy_internal_parse_source(tinypy_compile_ctx_t *ctx, const char *source, size_t source_size, const char *filename, const tinypy_parser_grammar_t *grammar, int32_t start_symbol, tinypy_parser_error_detail_t *error, int32_t *flags);

#endif
