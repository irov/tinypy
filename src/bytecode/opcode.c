#include "opcode.h"

#include "assertion.h"

#define TINYPY_OPCODE_ALL_CATEGORIES            \
    ((uint32_t)TINYPY_OPCODE_CATEGORY_CONST |   \
     (uint32_t)TINYPY_OPCODE_CATEGORY_NAME |    \
     (uint32_t)TINYPY_OPCODE_CATEGORY_JREL |    \
     (uint32_t)TINYPY_OPCODE_CATEGORY_JABS |    \
     (uint32_t)TINYPY_OPCODE_CATEGORY_LOCAL |   \
     (uint32_t)TINYPY_OPCODE_CATEGORY_COMPARE | \
     (uint32_t)TINYPY_OPCODE_CATEGORY_FREE)

static const tinypy_opcode_info_t __tinypy_opcode_table[TINYPY_OPCODE_COUNT] = {
    [TINYPY_OP_STOP_CODE] = {"STOP_CODE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_POP_TOP] = {"POP_TOP", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_ROT_TWO] = {"ROT_TWO", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_ROT_THREE] = {"ROT_THREE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_DUP_TOP] = {"DUP_TOP", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_ROT_FOUR] = {"ROT_FOUR", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_NOP] = {"NOP", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_UNARY_POSITIVE] = {"UNARY_POSITIVE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_UNARY_NEGATIVE] = {"UNARY_NEGATIVE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_UNARY_NOT] = {"UNARY_NOT", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_UNARY_CONVERT] = {"UNARY_CONVERT", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_UNARY_INVERT] = {"UNARY_INVERT", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_POWER] = {"BINARY_POWER", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_MULTIPLY] = {"BINARY_MULTIPLY", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_DIVIDE] = {"BINARY_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_MODULO] = {"BINARY_MODULO", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_ADD] = {"BINARY_ADD", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_SUBTRACT] = {"BINARY_SUBTRACT", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_SUBSCR] = {"BINARY_SUBSCR", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_FLOOR_DIVIDE] = {"BINARY_FLOOR_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_TRUE_DIVIDE] = {"BINARY_TRUE_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_INPLACE_FLOOR_DIVIDE] = {"INPLACE_FLOOR_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_INPLACE_TRUE_DIVIDE] = {"INPLACE_TRUE_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_SLICE_0] = {"SLICE+0", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_SLICE_1] = {"SLICE+1", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_SLICE_2] = {"SLICE+2", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_SLICE_3] = {"SLICE+3", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_STORE_SLICE_0] = {"STORE_SLICE+0", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_STORE_SLICE_1] = {"STORE_SLICE+1", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_STORE_SLICE_2] = {"STORE_SLICE+2", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_STORE_SLICE_3] = {"STORE_SLICE+3", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_DELETE_SLICE_0] = {"DELETE_SLICE+0", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_DELETE_SLICE_1] = {"DELETE_SLICE+1", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_DELETE_SLICE_2] = {"DELETE_SLICE+2", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_DELETE_SLICE_3] = {"DELETE_SLICE+3", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_STORE_MAP] = {"STORE_MAP", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_INPLACE_ADD] = {"INPLACE_ADD", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_INPLACE_SUBTRACT] = {"INPLACE_SUBTRACT", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_INPLACE_MULTIPLY] = {"INPLACE_MULTIPLY", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_INPLACE_DIVIDE] = {"INPLACE_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_INPLACE_MODULO] = {"INPLACE_MODULO", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_STORE_SUBSCR] = {"STORE_SUBSCR", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_DELETE_SUBSCR] = {"DELETE_SUBSCR", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_LSHIFT] = {"BINARY_LSHIFT", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_RSHIFT] = {"BINARY_RSHIFT", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_AND] = {"BINARY_AND", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_XOR] = {"BINARY_XOR", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BINARY_OR] = {"BINARY_OR", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_INPLACE_POWER] = {"INPLACE_POWER", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_GET_ITER] = {"GET_ITER", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_PRINT_EXPR] = {"PRINT_EXPR", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_PRINT_ITEM] = {"PRINT_ITEM", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_PRINT_NEWLINE] = {"PRINT_NEWLINE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_PRINT_ITEM_TO] = {"PRINT_ITEM_TO", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_PRINT_NEWLINE_TO] = {"PRINT_NEWLINE_TO", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_INPLACE_LSHIFT] = {"INPLACE_LSHIFT", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_INPLACE_RSHIFT] = {"INPLACE_RSHIFT", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_INPLACE_AND] = {"INPLACE_AND", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_INPLACE_XOR] = {"INPLACE_XOR", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_INPLACE_OR] = {"INPLACE_OR", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BREAK_LOOP] = {"BREAK_LOOP", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_WITH_CLEANUP] = {"WITH_CLEANUP", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_LOAD_LOCALS] = {"LOAD_LOCALS", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_RETURN_VALUE] = {"RETURN_VALUE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_IMPORT_STAR] = {"IMPORT_STAR", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_EXEC_STMT] = {"EXEC_STMT", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_YIELD_VALUE] = {"YIELD_VALUE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_POP_BLOCK] = {"POP_BLOCK", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_END_FINALLY] = {"END_FINALLY", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BUILD_CLASS] = {"BUILD_CLASS", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_STORE_NAME] = {"STORE_NAME", TINYPY_OPCODE_CATEGORY_NAME},
    [TINYPY_OP_DELETE_NAME] = {"DELETE_NAME", TINYPY_OPCODE_CATEGORY_NAME},
    [TINYPY_OP_UNPACK_SEQUENCE] = {"UNPACK_SEQUENCE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_FOR_ITER] = {"FOR_ITER", TINYPY_OPCODE_CATEGORY_JREL},
    [TINYPY_OP_LIST_APPEND] = {"LIST_APPEND", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_STORE_ATTR] = {"STORE_ATTR", TINYPY_OPCODE_CATEGORY_NAME},
    [TINYPY_OP_DELETE_ATTR] = {"DELETE_ATTR", TINYPY_OPCODE_CATEGORY_NAME},
    [TINYPY_OP_STORE_GLOBAL] = {"STORE_GLOBAL", TINYPY_OPCODE_CATEGORY_NAME},
    [TINYPY_OP_DELETE_GLOBAL] = {"DELETE_GLOBAL", TINYPY_OPCODE_CATEGORY_NAME},
    [TINYPY_OP_DUP_TOPX] = {"DUP_TOPX", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_LOAD_CONST] = {"LOAD_CONST", TINYPY_OPCODE_CATEGORY_CONST},
    [TINYPY_OP_LOAD_NAME] = {"LOAD_NAME", TINYPY_OPCODE_CATEGORY_NAME},
    [TINYPY_OP_BUILD_TUPLE] = {"BUILD_TUPLE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BUILD_LIST] = {"BUILD_LIST", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BUILD_SET] = {"BUILD_SET", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BUILD_MAP] = {"BUILD_MAP", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_LOAD_ATTR] = {"LOAD_ATTR", TINYPY_OPCODE_CATEGORY_NAME},
    [TINYPY_OP_COMPARE_OP] = {"COMPARE_OP", TINYPY_OPCODE_CATEGORY_COMPARE},
    [TINYPY_OP_IMPORT_NAME] = {"IMPORT_NAME", TINYPY_OPCODE_CATEGORY_NAME},
    [TINYPY_OP_IMPORT_FROM] = {"IMPORT_FROM", TINYPY_OPCODE_CATEGORY_NAME},
    [TINYPY_OP_JUMP_FORWARD] = {"JUMP_FORWARD", TINYPY_OPCODE_CATEGORY_JREL},
    [TINYPY_OP_JUMP_IF_FALSE_OR_POP] = {"JUMP_IF_FALSE_OR_POP", TINYPY_OPCODE_CATEGORY_JABS},
    [TINYPY_OP_JUMP_IF_TRUE_OR_POP] = {"JUMP_IF_TRUE_OR_POP", TINYPY_OPCODE_CATEGORY_JABS},
    [TINYPY_OP_JUMP_ABSOLUTE] = {"JUMP_ABSOLUTE", TINYPY_OPCODE_CATEGORY_JABS},
    [TINYPY_OP_POP_JUMP_IF_FALSE] = {"POP_JUMP_IF_FALSE", TINYPY_OPCODE_CATEGORY_JABS},
    [TINYPY_OP_POP_JUMP_IF_TRUE] = {"POP_JUMP_IF_TRUE", TINYPY_OPCODE_CATEGORY_JABS},
    [TINYPY_OP_LOAD_GLOBAL] = {"LOAD_GLOBAL", TINYPY_OPCODE_CATEGORY_NAME},
    [TINYPY_OP_CONTINUE_LOOP] = {"CONTINUE_LOOP", TINYPY_OPCODE_CATEGORY_JABS},
    [TINYPY_OP_SETUP_LOOP] = {"SETUP_LOOP", TINYPY_OPCODE_CATEGORY_JREL},
    [TINYPY_OP_SETUP_EXCEPT] = {"SETUP_EXCEPT", TINYPY_OPCODE_CATEGORY_JREL},
    [TINYPY_OP_SETUP_FINALLY] = {"SETUP_FINALLY", TINYPY_OPCODE_CATEGORY_JREL},
    [TINYPY_OP_LOAD_FAST] = {"LOAD_FAST", TINYPY_OPCODE_CATEGORY_LOCAL},
    [TINYPY_OP_STORE_FAST] = {"STORE_FAST", TINYPY_OPCODE_CATEGORY_LOCAL},
    [TINYPY_OP_DELETE_FAST] = {"DELETE_FAST", TINYPY_OPCODE_CATEGORY_LOCAL},
    [TINYPY_OP_RAISE_VARARGS] = {"RAISE_VARARGS", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_CALL_FUNCTION] = {"CALL_FUNCTION", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_MAKE_FUNCTION] = {"MAKE_FUNCTION", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_BUILD_SLICE] = {"BUILD_SLICE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_MAKE_CLOSURE] = {"MAKE_CLOSURE", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_LOAD_CLOSURE] = {"LOAD_CLOSURE", TINYPY_OPCODE_CATEGORY_FREE},
    [TINYPY_OP_LOAD_DEREF] = {"LOAD_DEREF", TINYPY_OPCODE_CATEGORY_FREE},
    [TINYPY_OP_STORE_DEREF] = {"STORE_DEREF", TINYPY_OPCODE_CATEGORY_FREE},
    [TINYPY_OP_CALL_FUNCTION_VAR] = {"CALL_FUNCTION_VAR", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_CALL_FUNCTION_KW] = {"CALL_FUNCTION_KW", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_CALL_FUNCTION_VAR_KW] = {"CALL_FUNCTION_VAR_KW", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_SETUP_WITH] = {"SETUP_WITH", TINYPY_OPCODE_CATEGORY_JREL},
    [TINYPY_OP_EXTENDED_ARG] = {"EXTENDED_ARG", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_SET_ADD] = {"SET_ADD", TINYPY_OPCODE_CATEGORY_NONE},
    [TINYPY_OP_MAP_ADD] = {"MAP_ADD", TINYPY_OPCODE_CATEGORY_NONE},
};

//////////////////////////////////////////////////////////////////////////
static void __tinypy_opcode_clear_instruction(tinypy_decoded_instruction_t *instruction) {
    instruction->offset = 0U;
    instruction->next_offset = 0U;
    instruction->encoded_size = 0U;
    instruction->extended_arg_count = 0U;
    instruction->argument = UINT64_C(0);
    instruction->opcode = 0U;
    instruction->defined = 0U;
    instruction->has_argument = 0U;
    instruction->reserved = 0U;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_opcode_name_equal(const char *candidate, const char *name, size_t name_size) {
    size_t index;

    for (index = 0U; index != name_size; ++index) {
        if (candidate[index] == '\0' || candidate[index] != name[index]) {
            return 0;
        }
    }

    return candidate[name_size] == '\0';
}
//////////////////////////////////////////////////////////////////////////
const tinypy_opcode_info_t *tinypy_opcode_get_info(uint8_t opcode) {
    const tinypy_opcode_info_t *info = &__tinypy_opcode_table[opcode];

    return info->name != NULL ? info : NULL;
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_opcode_name(uint8_t opcode) {
    return __tinypy_opcode_table[opcode].name;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_opcode_is_defined(uint8_t opcode) {
    return __tinypy_opcode_table[opcode].name != NULL;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_opcode_has_argument(uint8_t opcode) {
    return opcode >= (uint8_t)TINYPY_OPCODE_HAVE_ARGUMENT;
}
//////////////////////////////////////////////////////////////////////////
uint32_t tinypy_opcode_categories(uint8_t opcode) {
    return __tinypy_opcode_table[opcode].categories;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_opcode_has_category(uint8_t opcode, tinypy_opcode_category_e category) {
    uint32_t requested = (uint32_t)category;

    TINYPY_ASSERT(requested != 0U);
    TINYPY_ASSERT((requested & ~TINYPY_OPCODE_ALL_CATEGORIES) == 0U);

    return (__tinypy_opcode_table[opcode].categories & requested) == requested;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_opcode_lookup(const char *name, size_t name_size, uint8_t *out_opcode) {
    size_t opcode;

    TINYPY_ASSERT(out_opcode != NULL);
    TINYPY_ASSERT(name != NULL || name_size == 0U);
    *out_opcode = 0U;

    if (name_size == 0U) {
        return 0;
    }

    for (opcode = 0U; opcode != (size_t)TINYPY_OPCODE_COUNT; ++opcode) {
        const tinypy_opcode_info_t *info = &__tinypy_opcode_table[opcode];

        if (info->name != NULL && __tinypy_opcode_name_equal(info->name, name, name_size)) {
            *out_opcode = (uint8_t)opcode;
            return 1;
        }
    }

    return 0;
}
//////////////////////////////////////////////////////////////////////////
tinypy_opcode_decode_status_e tinypy_opcode_decode(const uint8_t *bytecode, size_t bytecode_size, size_t offset, tinypy_decoded_instruction_t *out_instruction) {
    size_t cursor;
    size_t extended_arg_count = 0U;
    uint64_t argument = UINT64_C(0);

    TINYPY_ASSERT(out_instruction != NULL);
    TINYPY_ASSERT(bytecode != NULL || bytecode_size == 0U);
    __tinypy_opcode_clear_instruction(out_instruction);

    if (offset > bytecode_size) {
        return TINYPY_OPCODE_DECODE_INVALID_OFFSET;
    }

    if (offset == bytecode_size) {
        return TINYPY_OPCODE_DECODE_EOF;
    }

    cursor = offset;

    for (;;) {
        uint8_t opcode = bytecode[cursor];
        uint16_t argument_word;
        const tinypy_opcode_info_t *info;

        cursor += 1U;

        if (opcode < (uint8_t)TINYPY_OPCODE_HAVE_ARGUMENT) {
            if (extended_arg_count != 0U) {
                return TINYPY_OPCODE_DECODE_INVALID_EXTENDED_ARG;
            }

            info = &__tinypy_opcode_table[opcode];
            out_instruction->offset = offset;
            out_instruction->next_offset = cursor;
            out_instruction->encoded_size = cursor - offset;
            out_instruction->opcode = opcode;
            out_instruction->defined = info->name != NULL ? 1U : 0U;
            out_instruction->has_argument = 0U;
            return TINYPY_OPCODE_DECODE_OK;
        }

        if (bytecode_size - cursor < 2U) {
            return TINYPY_OPCODE_DECODE_TRUNCATED;
        }

        argument_word = (uint16_t)bytecode[cursor];
        argument_word |= (uint16_t)((uint16_t)bytecode[cursor + 1U] << 8U);
        cursor += 2U;

        if (argument > (UINT64_MAX >> 16U)) {
            return TINYPY_OPCODE_DECODE_ARGUMENT_OVERFLOW;
        }

        argument = (argument << 16U) | (uint64_t)argument_word;

        if (opcode == (uint8_t)TINYPY_OPCODE_EXTENDED_ARG) {
            extended_arg_count += 1U;

            if (cursor == bytecode_size) {
                return TINYPY_OPCODE_DECODE_TRUNCATED;
            }

            continue;
        }

        info = &__tinypy_opcode_table[opcode];
        out_instruction->offset = offset;
        out_instruction->next_offset = cursor;
        out_instruction->encoded_size = cursor - offset;
        out_instruction->extended_arg_count = extended_arg_count;
        out_instruction->argument = argument;
        out_instruction->opcode = opcode;
        out_instruction->defined = info->name != NULL ? 1U : 0U;
        out_instruction->has_argument = 1U;
        return TINYPY_OPCODE_DECODE_OK;
    }
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_opcode_decode_status_name(tinypy_opcode_decode_status_e status) {
    switch (status) {
    case TINYPY_OPCODE_DECODE_OK:
        return "ok";
    case TINYPY_OPCODE_DECODE_EOF:
        return "end of bytecode";
    case TINYPY_OPCODE_DECODE_INVALID_OFFSET:
        return "invalid offset";
    case TINYPY_OPCODE_DECODE_TRUNCATED:
        return "truncated instruction";
    case TINYPY_OPCODE_DECODE_INVALID_EXTENDED_ARG:
        return "EXTENDED_ARG must prefix an opcode with an argument";
    case TINYPY_OPCODE_DECODE_ARGUMENT_OVERFLOW:
        return "extended argument overflow";
    default:
        return "unknown decode status";
    }
}
