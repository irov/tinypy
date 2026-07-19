#ifndef TINYPY_COMPILER_GRAMMAR_H
#define TINYPY_COMPILER_GRAMMAR_H

#include "bitset.h"

typedef struct tinypy_parser_label_t {
    int token_type;
    const char *text;
} tinypy_parser_label_t;

typedef struct tinypy_parser_label_list_t {
    int count;
    const tinypy_parser_label_t *items;
} tinypy_parser_label_list_t;

typedef struct tinypy_parser_arc_t {
    short label_index;
    short target_state;
} tinypy_parser_arc_t;

typedef struct tinypy_parser_dfa_state_t {
    int transition_count;
    const tinypy_parser_arc_t *transitions;
} tinypy_parser_dfa_state_t;

typedef struct tinypy_parser_dfa_t {
    int symbol;
    const char *name;
    int initial_state;
    int state_count;
    const tinypy_parser_dfa_state_t *states;
    const tinypy_bitset_byte_t *first_set;
} tinypy_parser_dfa_t;

typedef struct tinypy_parser_grammar_t {
    int rule_count;
    const tinypy_parser_dfa_t *rules;
    tinypy_parser_label_list_t labels;
    int start_symbol;
} tinypy_parser_grammar_t;

#define TINYPY_GRAMMAR_EMPTY_LABEL 0

extern const tinypy_parser_grammar_t __tinypy_parser_grammar;

#endif
