#ifndef TINYPY_BYTECODE_OPCODE_H
#define TINYPY_BYTECODE_OPCODE_H

#include "tinypy/types.h"

#define TINYPY_OPCODE_COUNT 256U
#define TINYPY_OPCODE_HAVE_ARGUMENT 90U
#define TINYPY_OPCODE_EXTENDED_ARG 145U

typedef enum tinypy_opcode_value_e {
    TINYPY_OP_STOP_CODE = 0,
    TINYPY_OP_POP_TOP = 1,
    TINYPY_OP_ROT_TWO = 2,
    TINYPY_OP_ROT_THREE = 3,
    TINYPY_OP_DUP_TOP = 4,
    TINYPY_OP_ROT_FOUR = 5,
    TINYPY_OP_NOP = 9,
    TINYPY_OP_UNARY_POSITIVE = 10,
    TINYPY_OP_UNARY_NEGATIVE = 11,
    TINYPY_OP_UNARY_NOT = 12,
    TINYPY_OP_UNARY_CONVERT = 13,
    TINYPY_OP_UNARY_INVERT = 15,
    TINYPY_OP_BINARY_POWER = 19,
    TINYPY_OP_BINARY_MULTIPLY = 20,
    TINYPY_OP_BINARY_DIVIDE = 21,
    TINYPY_OP_BINARY_MODULO = 22,
    TINYPY_OP_BINARY_ADD = 23,
    TINYPY_OP_BINARY_SUBTRACT = 24,
    TINYPY_OP_BINARY_SUBSCR = 25,
    TINYPY_OP_BINARY_FLOOR_DIVIDE = 26,
    TINYPY_OP_BINARY_TRUE_DIVIDE = 27,
    TINYPY_OP_INPLACE_FLOOR_DIVIDE = 28,
    TINYPY_OP_INPLACE_TRUE_DIVIDE = 29,
    TINYPY_OP_SLICE_0 = 30,
    TINYPY_OP_SLICE_1 = 31,
    TINYPY_OP_SLICE_2 = 32,
    TINYPY_OP_SLICE_3 = 33,
    TINYPY_OP_STORE_SLICE_0 = 40,
    TINYPY_OP_STORE_SLICE_1 = 41,
    TINYPY_OP_STORE_SLICE_2 = 42,
    TINYPY_OP_STORE_SLICE_3 = 43,
    TINYPY_OP_DELETE_SLICE_0 = 50,
    TINYPY_OP_DELETE_SLICE_1 = 51,
    TINYPY_OP_DELETE_SLICE_2 = 52,
    TINYPY_OP_DELETE_SLICE_3 = 53,
    TINYPY_OP_STORE_MAP = 54,
    TINYPY_OP_INPLACE_ADD = 55,
    TINYPY_OP_INPLACE_SUBTRACT = 56,
    TINYPY_OP_INPLACE_MULTIPLY = 57,
    TINYPY_OP_INPLACE_DIVIDE = 58,
    TINYPY_OP_INPLACE_MODULO = 59,
    TINYPY_OP_STORE_SUBSCR = 60,
    TINYPY_OP_DELETE_SUBSCR = 61,
    TINYPY_OP_BINARY_LSHIFT = 62,
    TINYPY_OP_BINARY_RSHIFT = 63,
    TINYPY_OP_BINARY_AND = 64,
    TINYPY_OP_BINARY_XOR = 65,
    TINYPY_OP_BINARY_OR = 66,
    TINYPY_OP_INPLACE_POWER = 67,
    TINYPY_OP_GET_ITER = 68,
    TINYPY_OP_PRINT_EXPR = 70,
    TINYPY_OP_PRINT_ITEM = 71,
    TINYPY_OP_PRINT_NEWLINE = 72,
    TINYPY_OP_PRINT_ITEM_TO = 73,
    TINYPY_OP_PRINT_NEWLINE_TO = 74,
    TINYPY_OP_INPLACE_LSHIFT = 75,
    TINYPY_OP_INPLACE_RSHIFT = 76,
    TINYPY_OP_INPLACE_AND = 77,
    TINYPY_OP_INPLACE_XOR = 78,
    TINYPY_OP_INPLACE_OR = 79,
    TINYPY_OP_BREAK_LOOP = 80,
    TINYPY_OP_WITH_CLEANUP = 81,
    TINYPY_OP_LOAD_LOCALS = 82,
    TINYPY_OP_RETURN_VALUE = 83,
    TINYPY_OP_IMPORT_STAR = 84,
    TINYPY_OP_EXEC_STMT = 85,
    TINYPY_OP_YIELD_VALUE = 86,
    TINYPY_OP_POP_BLOCK = 87,
    TINYPY_OP_END_FINALLY = 88,
    TINYPY_OP_BUILD_CLASS = 89,
    TINYPY_OP_STORE_NAME = 90,
    TINYPY_OP_DELETE_NAME = 91,
    TINYPY_OP_UNPACK_SEQUENCE = 92,
    TINYPY_OP_FOR_ITER = 93,
    TINYPY_OP_LIST_APPEND = 94,
    TINYPY_OP_STORE_ATTR = 95,
    TINYPY_OP_DELETE_ATTR = 96,
    TINYPY_OP_STORE_GLOBAL = 97,
    TINYPY_OP_DELETE_GLOBAL = 98,
    TINYPY_OP_DUP_TOPX = 99,
    TINYPY_OP_LOAD_CONST = 100,
    TINYPY_OP_LOAD_NAME = 101,
    TINYPY_OP_BUILD_TUPLE = 102,
    TINYPY_OP_BUILD_LIST = 103,
    TINYPY_OP_BUILD_SET = 104,
    TINYPY_OP_BUILD_MAP = 105,
    TINYPY_OP_LOAD_ATTR = 106,
    TINYPY_OP_COMPARE_OP = 107,
    TINYPY_OP_IMPORT_NAME = 108,
    TINYPY_OP_IMPORT_FROM = 109,
    TINYPY_OP_JUMP_FORWARD = 110,
    TINYPY_OP_JUMP_IF_FALSE_OR_POP = 111,
    TINYPY_OP_JUMP_IF_TRUE_OR_POP = 112,
    TINYPY_OP_JUMP_ABSOLUTE = 113,
    TINYPY_OP_POP_JUMP_IF_FALSE = 114,
    TINYPY_OP_POP_JUMP_IF_TRUE = 115,
    TINYPY_OP_LOAD_GLOBAL = 116,
    TINYPY_OP_CONTINUE_LOOP = 119,
    TINYPY_OP_SETUP_LOOP = 120,
    TINYPY_OP_SETUP_EXCEPT = 121,
    TINYPY_OP_SETUP_FINALLY = 122,
    TINYPY_OP_LOAD_FAST = 124,
    TINYPY_OP_STORE_FAST = 125,
    TINYPY_OP_DELETE_FAST = 126,
    TINYPY_OP_RAISE_VARARGS = 130,
    TINYPY_OP_CALL_FUNCTION = 131,
    TINYPY_OP_MAKE_FUNCTION = 132,
    TINYPY_OP_BUILD_SLICE = 133,
    TINYPY_OP_MAKE_CLOSURE = 134,
    TINYPY_OP_LOAD_CLOSURE = 135,
    TINYPY_OP_LOAD_DEREF = 136,
    TINYPY_OP_STORE_DEREF = 137,
    TINYPY_OP_CALL_FUNCTION_VAR = 140,
    TINYPY_OP_CALL_FUNCTION_KW = 141,
    TINYPY_OP_CALL_FUNCTION_VAR_KW = 142,
    TINYPY_OP_SETUP_WITH = 143,
    TINYPY_OP_EXTENDED_ARG = 145,
    TINYPY_OP_SET_ADD = 146,
    TINYPY_OP_MAP_ADD = 147
} tinypy_opcode_value_e;

typedef enum tinypy_opcode_category_e {
    TINYPY_OPCODE_CATEGORY_NONE = 0,
    TINYPY_OPCODE_CATEGORY_CONST = 1 << 0,
    TINYPY_OPCODE_CATEGORY_NAME = 1 << 1,
    TINYPY_OPCODE_CATEGORY_JREL = 1 << 2,
    TINYPY_OPCODE_CATEGORY_JABS = 1 << 3,
    TINYPY_OPCODE_CATEGORY_LOCAL = 1 << 4,
    TINYPY_OPCODE_CATEGORY_COMPARE = 1 << 5,
    TINYPY_OPCODE_CATEGORY_FREE = 1 << 6
} tinypy_opcode_category_e;

typedef struct tinypy_opcode_info_t {
    const char *name;
    uint32_t categories;
} tinypy_opcode_info_t;

typedef enum tinypy_opcode_decode_status_e {
    TINYPY_OPCODE_DECODE_OK = 0,
    TINYPY_OPCODE_DECODE_EOF = 1,
    TINYPY_OPCODE_DECODE_INVALID_OFFSET = 2,
    TINYPY_OPCODE_DECODE_TRUNCATED = 3,
    TINYPY_OPCODE_DECODE_INVALID_EXTENDED_ARG = 4,
    TINYPY_OPCODE_DECODE_ARGUMENT_OVERFLOW = 5
} tinypy_opcode_decode_status_e;

typedef struct tinypy_decoded_instruction_t {
    size_t offset;
    size_t next_offset;
    size_t encoded_size;
    size_t extended_arg_count;
    uint64_t argument;
    uint8_t opcode;
    uint8_t defined;
    uint8_t has_argument;
    uint8_t reserved;
} tinypy_decoded_instruction_t;

const tinypy_opcode_info_t *tinypy_opcode_get_info(uint8_t opcode);
/* Returns NULL for an unassigned opcode value. */
const char *tinypy_opcode_name(uint8_t opcode);
int32_t tinypy_opcode_is_defined(uint8_t opcode);
int32_t tinypy_opcode_has_argument(uint8_t opcode);
uint32_t tinypy_opcode_categories(uint8_t opcode);
int32_t tinypy_opcode_has_category(uint8_t opcode, tinypy_opcode_category_e category);

/* Exact, case-sensitive lookup of a CPython opmap name. */
int32_t tinypy_opcode_lookup(const char *name, size_t name_size, uint8_t *out_opcode);

/*
 * Decode one logical instruction at offset.  One or more EXTENDED_ARG
 * prefixes are folded into argument and counted in extended_arg_count.
 * The output is cleared on every non-OK result.
 */
tinypy_opcode_decode_status_e tinypy_opcode_decode(const uint8_t *bytecode, size_t bytecode_size, size_t offset, tinypy_decoded_instruction_t *out_instruction);

const char *tinypy_opcode_decode_status_name(tinypy_opcode_decode_status_e status);

#endif
