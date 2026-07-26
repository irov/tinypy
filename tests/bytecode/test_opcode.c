#include "opcode.h"

#include <stdio.h>
#include <string.h>

#define TEST_CHECK(condition)                 \
    do { \
        if (!(condition)) { \
            (void)fprintf(                    \
                stderr,                       \
                "%s:%d: check failed: %s\\n", \
                __FILE__,                     \
                __LINE__,                     \
                #condition);                  \
            return 1;                         \
        }                                     \
    } while (0)

typedef struct expected_opcode_t {
    uint8_t code;
    const char *name;
    uint32_t categories;
} expected_opcode_t;

static const expected_opcode_t expected_opcodes[] = { {0U, "STOP_CODE", TINYPY_OPCODE_CATEGORY_NONE},
    {1U, "POP_TOP", TINYPY_OPCODE_CATEGORY_NONE}, {2U, "ROT_TWO", TINYPY_OPCODE_CATEGORY_NONE},
    {3U, "ROT_THREE", TINYPY_OPCODE_CATEGORY_NONE}, {4U, "DUP_TOP", TINYPY_OPCODE_CATEGORY_NONE},
    {5U, "ROT_FOUR", TINYPY_OPCODE_CATEGORY_NONE}, {9U, "NOP", TINYPY_OPCODE_CATEGORY_NONE},
    {10U, "UNARY_POSITIVE", TINYPY_OPCODE_CATEGORY_NONE}, {11U, "UNARY_NEGATIVE", TINYPY_OPCODE_CATEGORY_NONE},
    {12U, "UNARY_NOT", TINYPY_OPCODE_CATEGORY_NONE}, {13U, "UNARY_CONVERT", TINYPY_OPCODE_CATEGORY_NONE},
    {15U, "UNARY_INVERT", TINYPY_OPCODE_CATEGORY_NONE}, {19U, "BINARY_POWER", TINYPY_OPCODE_CATEGORY_NONE},
    {20U, "BINARY_MULTIPLY", TINYPY_OPCODE_CATEGORY_NONE}, {21U, "BINARY_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE},
    {22U, "BINARY_MODULO", TINYPY_OPCODE_CATEGORY_NONE}, {23U, "BINARY_ADD", TINYPY_OPCODE_CATEGORY_NONE},
    {24U, "BINARY_SUBTRACT", TINYPY_OPCODE_CATEGORY_NONE}, {25U, "BINARY_SUBSCR", TINYPY_OPCODE_CATEGORY_NONE},
    {26U, "BINARY_FLOOR_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE}, {27U, "BINARY_TRUE_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE},
    {28U, "INPLACE_FLOOR_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE}, {29U, "INPLACE_TRUE_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE},
    {30U, "SLICE+0", TINYPY_OPCODE_CATEGORY_NONE}, {31U, "SLICE+1", TINYPY_OPCODE_CATEGORY_NONE},
    {32U, "SLICE+2", TINYPY_OPCODE_CATEGORY_NONE}, {33U, "SLICE+3", TINYPY_OPCODE_CATEGORY_NONE},
    {40U, "STORE_SLICE+0", TINYPY_OPCODE_CATEGORY_NONE}, {41U, "STORE_SLICE+1", TINYPY_OPCODE_CATEGORY_NONE},
    {42U, "STORE_SLICE+2", TINYPY_OPCODE_CATEGORY_NONE}, {43U, "STORE_SLICE+3", TINYPY_OPCODE_CATEGORY_NONE},
    {50U, "DELETE_SLICE+0", TINYPY_OPCODE_CATEGORY_NONE}, {51U, "DELETE_SLICE+1", TINYPY_OPCODE_CATEGORY_NONE},
    {52U, "DELETE_SLICE+2", TINYPY_OPCODE_CATEGORY_NONE}, {53U, "DELETE_SLICE+3", TINYPY_OPCODE_CATEGORY_NONE},
    {54U, "STORE_MAP", TINYPY_OPCODE_CATEGORY_NONE}, {55U, "INPLACE_ADD", TINYPY_OPCODE_CATEGORY_NONE},
    {56U, "INPLACE_SUBTRACT", TINYPY_OPCODE_CATEGORY_NONE}, {57U, "INPLACE_MULTIPLY", TINYPY_OPCODE_CATEGORY_NONE},
    {58U, "INPLACE_DIVIDE", TINYPY_OPCODE_CATEGORY_NONE}, {59U, "INPLACE_MODULO", TINYPY_OPCODE_CATEGORY_NONE},
    {60U, "STORE_SUBSCR", TINYPY_OPCODE_CATEGORY_NONE}, {61U, "DELETE_SUBSCR", TINYPY_OPCODE_CATEGORY_NONE},
    {62U, "BINARY_LSHIFT", TINYPY_OPCODE_CATEGORY_NONE}, {63U, "BINARY_RSHIFT", TINYPY_OPCODE_CATEGORY_NONE},
    {64U, "BINARY_AND", TINYPY_OPCODE_CATEGORY_NONE}, {65U, "BINARY_XOR", TINYPY_OPCODE_CATEGORY_NONE},
    {66U, "BINARY_OR", TINYPY_OPCODE_CATEGORY_NONE}, {67U, "INPLACE_POWER", TINYPY_OPCODE_CATEGORY_NONE},
    {68U, "GET_ITER", TINYPY_OPCODE_CATEGORY_NONE}, {70U, "PRINT_EXPR", TINYPY_OPCODE_CATEGORY_NONE},
    {71U, "PRINT_ITEM", TINYPY_OPCODE_CATEGORY_NONE}, {72U, "PRINT_NEWLINE", TINYPY_OPCODE_CATEGORY_NONE},
    {73U, "PRINT_ITEM_TO", TINYPY_OPCODE_CATEGORY_NONE}, {74U, "PRINT_NEWLINE_TO", TINYPY_OPCODE_CATEGORY_NONE},
    {75U, "INPLACE_LSHIFT", TINYPY_OPCODE_CATEGORY_NONE}, {76U, "INPLACE_RSHIFT", TINYPY_OPCODE_CATEGORY_NONE},
    {77U, "INPLACE_AND", TINYPY_OPCODE_CATEGORY_NONE}, {78U, "INPLACE_XOR", TINYPY_OPCODE_CATEGORY_NONE},
    {79U, "INPLACE_OR", TINYPY_OPCODE_CATEGORY_NONE}, {80U, "BREAK_LOOP", TINYPY_OPCODE_CATEGORY_NONE},
    {81U, "WITH_CLEANUP", TINYPY_OPCODE_CATEGORY_NONE}, {82U, "LOAD_LOCALS", TINYPY_OPCODE_CATEGORY_NONE},
    {83U, "RETURN_VALUE", TINYPY_OPCODE_CATEGORY_NONE}, {84U, "IMPORT_STAR", TINYPY_OPCODE_CATEGORY_NONE},
    {85U, "EXEC_STMT", TINYPY_OPCODE_CATEGORY_NONE}, {86U, "YIELD_VALUE", TINYPY_OPCODE_CATEGORY_NONE},
    {87U, "POP_BLOCK", TINYPY_OPCODE_CATEGORY_NONE}, {88U, "END_FINALLY", TINYPY_OPCODE_CATEGORY_NONE},
    {89U, "BUILD_CLASS", TINYPY_OPCODE_CATEGORY_NONE}, {90U, "STORE_NAME", TINYPY_OPCODE_CATEGORY_NAME},
    {91U, "DELETE_NAME", TINYPY_OPCODE_CATEGORY_NAME}, {92U, "UNPACK_SEQUENCE", TINYPY_OPCODE_CATEGORY_NONE},
    {93U, "FOR_ITER", TINYPY_OPCODE_CATEGORY_JREL}, {94U, "LIST_APPEND", TINYPY_OPCODE_CATEGORY_NONE},
    {95U, "STORE_ATTR", TINYPY_OPCODE_CATEGORY_NAME}, {96U, "DELETE_ATTR", TINYPY_OPCODE_CATEGORY_NAME},
    {97U, "STORE_GLOBAL", TINYPY_OPCODE_CATEGORY_NAME}, {98U, "DELETE_GLOBAL", TINYPY_OPCODE_CATEGORY_NAME},
    {99U, "DUP_TOPX", TINYPY_OPCODE_CATEGORY_NONE}, {100U, "LOAD_CONST", TINYPY_OPCODE_CATEGORY_CONST},
    {101U, "LOAD_NAME", TINYPY_OPCODE_CATEGORY_NAME}, {102U, "BUILD_TUPLE", TINYPY_OPCODE_CATEGORY_NONE},
    {103U, "BUILD_LIST", TINYPY_OPCODE_CATEGORY_NONE}, {104U, "BUILD_SET", TINYPY_OPCODE_CATEGORY_NONE},
    {105U, "BUILD_MAP", TINYPY_OPCODE_CATEGORY_NONE}, {106U, "LOAD_ATTR", TINYPY_OPCODE_CATEGORY_NAME},
    {107U, "COMPARE_OP", TINYPY_OPCODE_CATEGORY_COMPARE}, {108U, "IMPORT_NAME", TINYPY_OPCODE_CATEGORY_NAME},
    {109U, "IMPORT_FROM", TINYPY_OPCODE_CATEGORY_NAME}, {110U, "JUMP_FORWARD", TINYPY_OPCODE_CATEGORY_JREL},
    {111U, "JUMP_IF_FALSE_OR_POP", TINYPY_OPCODE_CATEGORY_JABS}, {112U, "JUMP_IF_TRUE_OR_POP", TINYPY_OPCODE_CATEGORY_JABS},
    {113U, "JUMP_ABSOLUTE", TINYPY_OPCODE_CATEGORY_JABS}, {114U, "POP_JUMP_IF_FALSE", TINYPY_OPCODE_CATEGORY_JABS},
    {115U, "POP_JUMP_IF_TRUE", TINYPY_OPCODE_CATEGORY_JABS}, {116U, "LOAD_GLOBAL", TINYPY_OPCODE_CATEGORY_NAME},
    {119U, "CONTINUE_LOOP", TINYPY_OPCODE_CATEGORY_JABS}, {120U, "SETUP_LOOP", TINYPY_OPCODE_CATEGORY_JREL},
    {121U, "SETUP_EXCEPT", TINYPY_OPCODE_CATEGORY_JREL}, {122U, "SETUP_FINALLY", TINYPY_OPCODE_CATEGORY_JREL},
    {124U, "LOAD_FAST", TINYPY_OPCODE_CATEGORY_LOCAL}, {125U, "STORE_FAST", TINYPY_OPCODE_CATEGORY_LOCAL},
    {126U, "DELETE_FAST", TINYPY_OPCODE_CATEGORY_LOCAL}, {130U, "RAISE_VARARGS", TINYPY_OPCODE_CATEGORY_NONE},
    {131U, "CALL_FUNCTION", TINYPY_OPCODE_CATEGORY_NONE}, {132U, "MAKE_FUNCTION", TINYPY_OPCODE_CATEGORY_NONE},
    {133U, "BUILD_SLICE", TINYPY_OPCODE_CATEGORY_NONE}, {134U, "MAKE_CLOSURE", TINYPY_OPCODE_CATEGORY_NONE},
    {135U, "LOAD_CLOSURE", TINYPY_OPCODE_CATEGORY_FREE}, {136U, "LOAD_DEREF", TINYPY_OPCODE_CATEGORY_FREE},
    {137U, "STORE_DEREF", TINYPY_OPCODE_CATEGORY_FREE}, {140U, "CALL_FUNCTION_VAR", TINYPY_OPCODE_CATEGORY_NONE},
    {141U, "CALL_FUNCTION_KW", TINYPY_OPCODE_CATEGORY_NONE}, {142U, "CALL_FUNCTION_VAR_KW", TINYPY_OPCODE_CATEGORY_NONE},
    {143U, "SETUP_WITH", TINYPY_OPCODE_CATEGORY_JREL}, {145U, "EXTENDED_ARG", TINYPY_OPCODE_CATEGORY_NONE},
    {146U, "SET_ADD", TINYPY_OPCODE_CATEGORY_NONE}, {147U, "MAP_ADD", TINYPY_OPCODE_CATEGORY_NONE},
};

static const expected_opcode_t *__find_expected(uint8_t code) {
    size_t index;

    for (index = 0U;
         index != sizeof(expected_opcodes) / sizeof(expected_opcodes[0]);
         ++index) {
        if (expected_opcodes[index].code == code) {
            return &expected_opcodes[index];
        }
    }

    return NULL;
}

static int32_t __test_exact_metadata(void) {
    size_t code;
    size_t defined_count = 0U;
    size_t const_count = 0U;
    size_t name_count = 0U;
    size_t jrel_count = 0U;
    size_t jabs_count = 0U;
    size_t local_count = 0U;
    size_t compare_count = 0U;
    size_t free_count = 0U;

    TEST_CHECK(TINYPY_OPCODE_COUNT == 256U);
    TEST_CHECK(TINYPY_OPCODE_HAVE_ARGUMENT == 90U);
    TEST_CHECK(TINYPY_OPCODE_EXTENDED_ARG == 145U);

    for (code = 0U; code != TINYPY_OPCODE_COUNT; ++code) {
        uint8_t opcode = (uint8_t)code;
        const expected_opcode_t *expected = __find_expected(opcode);
        const tinypy_opcode_info_t *info = tinypy_opcode_get_info(opcode);
        const char *name = tinypy_opcode_name(opcode);
        uint32_t categories = tinypy_opcode_categories(opcode);
        uint8_t looked_up = 255U;

        TEST_CHECK(tinypy_opcode_has_argument(opcode) == (code >= TINYPY_OPCODE_HAVE_ARGUMENT));

        if (expected != NULL) {
            size_t name_size = strlen(expected->name);

            defined_count += 1U;
            TEST_CHECK(info != NULL);
            TEST_CHECK(tinypy_opcode_is_defined(opcode));
            TEST_CHECK(name == info->name);
            TEST_CHECK(strcmp(name, expected->name) == 0);
            TEST_CHECK(categories == expected->categories);
            TEST_CHECK(tinypy_opcode_lookup(expected->name, name_size, &looked_up));
            TEST_CHECK(looked_up == opcode);
        }
        else {
            TEST_CHECK(info == NULL);
            TEST_CHECK(name == NULL);
            TEST_CHECK(!tinypy_opcode_is_defined(opcode));
            TEST_CHECK(categories == TINYPY_OPCODE_CATEGORY_NONE);
        }

        if ((categories & TINYPY_OPCODE_CATEGORY_CONST) != 0U) {
            const_count += 1U;
        }
        if ((categories & TINYPY_OPCODE_CATEGORY_NAME) != 0U) {
            name_count += 1U;
        }
        if ((categories & TINYPY_OPCODE_CATEGORY_JREL) != 0U) {
            jrel_count += 1U;
        }
        if ((categories & TINYPY_OPCODE_CATEGORY_JABS) != 0U) {
            jabs_count += 1U;
        }
        if ((categories & TINYPY_OPCODE_CATEGORY_LOCAL) != 0U) {
            local_count += 1U;
        }
        if ((categories & TINYPY_OPCODE_CATEGORY_COMPARE) != 0U) {
            compare_count += 1U;
        }
        if ((categories & TINYPY_OPCODE_CATEGORY_FREE) != 0U) {
            free_count += 1U;
        }
    }

    TEST_CHECK(defined_count == 119U);
    TEST_CHECK(const_count == 1U);
    TEST_CHECK(name_count == 11U);
    TEST_CHECK(jrel_count == 6U);
    TEST_CHECK(jabs_count == 6U);
    TEST_CHECK(local_count == 3U);
    TEST_CHECK(compare_count == 1U);
    TEST_CHECK(free_count == 3U);
    return 0;
}

static int32_t __test_lookup_and_categories(void) {
    uint8_t opcode = 255U;

    TEST_CHECK(tinypy_opcode_lookup("LOAD_CONST", 10U, &opcode));
    TEST_CHECK(opcode == (uint8_t)TINYPY_OP_LOAD_CONST);
    TEST_CHECK(tinypy_opcode_lookup("SLICE+0", 7U, &opcode));
    TEST_CHECK(opcode == (uint8_t)TINYPY_OP_SLICE_0);

    opcode = 255U;
    TEST_CHECK(!tinypy_opcode_lookup("LOAD_CONST", 0U, &opcode));
    TEST_CHECK(opcode == 0U);
    opcode = 255U;
    TEST_CHECK(!tinypy_opcode_lookup("LOAD_CONST", 9U, &opcode));
    TEST_CHECK(opcode == 0U);
    opcode = 255U;
    TEST_CHECK(!tinypy_opcode_lookup("load_const", 10U, &opcode));
    TEST_CHECK(opcode == 0U);
    TEST_CHECK(tinypy_opcode_has_category(
        (uint8_t)TINYPY_OP_LOAD_CONST,
        TINYPY_OPCODE_CATEGORY_CONST));
    TEST_CHECK(!tinypy_opcode_has_category(
        (uint8_t)TINYPY_OP_LOAD_CONST,
        TINYPY_OPCODE_CATEGORY_NAME));
    TEST_CHECK(!tinypy_opcode_has_category(
        (uint8_t)TINYPY_OP_LOAD_CONST,
        (tinypy_opcode_category_e)(TINYPY_OPCODE_CATEGORY_CONST | TINYPY_OPCODE_CATEGORY_NAME)));
    TEST_CHECK(tinypy_opcode_categories(117U) == TINYPY_OPCODE_CATEGORY_NONE);
    return 0;
}

static int32_t __instruction_is_clear(const tinypy_decoded_instruction_t *instruction) {
    return instruction->offset == 0U && instruction->next_offset == 0U && instruction->encoded_size == 0U && instruction->extended_arg_count == 0U && instruction->argument == UINT64_C(0) && instruction->opcode == 0U && instruction->defined == 0U && instruction->has_argument == 0U && instruction->reserved == 0U;
}

static int32_t __test_decode_basic_and_sequential(void) {
    static const uint8_t code[] = {
        TINYPY_OP_POP_TOP,
        TINYPY_OP_LOAD_CONST, 0x34U, 0x12U,
        TINYPY_OP_RETURN_VALUE};
    tinypy_decoded_instruction_t instruction;
    tinypy_opcode_decode_status_e status;

    (void)memset(&instruction, 0xA5, sizeof(instruction));
    status = tinypy_opcode_decode(code, sizeof(code), 0U, &instruction);
    TEST_CHECK(status == TINYPY_OPCODE_DECODE_OK);
    TEST_CHECK(instruction.offset == 0U);
    TEST_CHECK(instruction.next_offset == 1U);
    TEST_CHECK(instruction.encoded_size == 1U);
    TEST_CHECK(instruction.opcode == TINYPY_OP_POP_TOP);
    TEST_CHECK(instruction.defined == 1U);
    TEST_CHECK(instruction.has_argument == 0U);
    TEST_CHECK(instruction.argument == UINT64_C(0));
    TEST_CHECK(instruction.extended_arg_count == 0U);

    status = tinypy_opcode_decode(
        code,
        sizeof(code),
        instruction.next_offset,
        &instruction);
    TEST_CHECK(status == TINYPY_OPCODE_DECODE_OK);
    TEST_CHECK(instruction.offset == 1U);
    TEST_CHECK(instruction.next_offset == 4U);
    TEST_CHECK(instruction.encoded_size == 3U);
    TEST_CHECK(instruction.opcode == TINYPY_OP_LOAD_CONST);
    TEST_CHECK(instruction.has_argument == 1U);
    TEST_CHECK(instruction.argument == UINT64_C(0x1234));
    TEST_CHECK(instruction.extended_arg_count == 0U);

    status = tinypy_opcode_decode(
        code,
        sizeof(code),
        instruction.next_offset,
        &instruction);
    TEST_CHECK(status == TINYPY_OPCODE_DECODE_OK);
    TEST_CHECK(instruction.opcode == TINYPY_OP_RETURN_VALUE);
    TEST_CHECK(instruction.next_offset == sizeof(code));

    status = tinypy_opcode_decode(
        code,
        sizeof(code),
        instruction.next_offset,
        &instruction);
    TEST_CHECK(status == TINYPY_OPCODE_DECODE_EOF);
    TEST_CHECK(__instruction_is_clear(&instruction));
    return 0;
}

static int32_t __test_decode_extended_argument(void) {
    static const uint8_t one_prefix[] = {
        TINYPY_OP_EXTENDED_ARG, 0x34U, 0x12U,
        TINYPY_OP_LOAD_CONST, 0x78U, 0x56U};
    static const uint8_t two_prefixes[] = {
        TINYPY_OP_EXTENDED_ARG, 0x01U, 0x00U,
        TINYPY_OP_EXTENDED_ARG, 0x02U, 0x00U,
        TINYPY_OP_LOAD_CONST, 0x03U, 0x00U};
    tinypy_decoded_instruction_t instruction;

    TEST_CHECK(tinypy_opcode_decode(
                   one_prefix,
                   sizeof(one_prefix),
                   0U,
                   &instruction) == TINYPY_OPCODE_DECODE_OK);
    TEST_CHECK(instruction.opcode == TINYPY_OP_LOAD_CONST);
    TEST_CHECK(instruction.argument == UINT64_C(0x12345678));
    TEST_CHECK(instruction.extended_arg_count == 1U);
    TEST_CHECK(instruction.encoded_size == 6U);
    TEST_CHECK(instruction.next_offset == sizeof(one_prefix));

    TEST_CHECK(tinypy_opcode_decode(
                   two_prefixes,
                   sizeof(two_prefixes),
                   0U,
                   &instruction) == TINYPY_OPCODE_DECODE_OK);
    TEST_CHECK(instruction.opcode == TINYPY_OP_LOAD_CONST);
    TEST_CHECK(instruction.argument == UINT64_C(0x000100020003));
    TEST_CHECK(instruction.extended_arg_count == 2U);
    TEST_CHECK(instruction.encoded_size == 9U);
    TEST_CHECK(instruction.next_offset == sizeof(two_prefixes));
    return 0;
}

static int32_t __test_decode_unknown_opcodes(void) {
    static const uint8_t no_argument[] = {6U};
    static const uint8_t with_argument[] = {148U, 0xAAU, 0x55U};
    tinypy_decoded_instruction_t instruction;

    TEST_CHECK(tinypy_opcode_decode(
                   no_argument,
                   sizeof(no_argument),
                   0U,
                   &instruction) == TINYPY_OPCODE_DECODE_OK);
    TEST_CHECK(instruction.opcode == 6U);
    TEST_CHECK(instruction.defined == 0U);
    TEST_CHECK(instruction.has_argument == 0U);
    TEST_CHECK(instruction.encoded_size == 1U);

    TEST_CHECK(tinypy_opcode_decode(
                   with_argument,
                   sizeof(with_argument),
                   0U,
                   &instruction) == TINYPY_OPCODE_DECODE_OK);
    TEST_CHECK(instruction.opcode == 148U);
    TEST_CHECK(instruction.defined == 0U);
    TEST_CHECK(instruction.has_argument == 1U);
    TEST_CHECK(instruction.argument == UINT64_C(0x55AA));
    TEST_CHECK(instruction.encoded_size == 3U);
    return 0;
}

static int32_t __expect_decode_error(const uint8_t *code, size_t code_size, size_t offset, tinypy_opcode_decode_status_e expected) {
    tinypy_decoded_instruction_t instruction;
    tinypy_opcode_decode_status_e actual;

    (void)memset(&instruction, 0xA5, sizeof(instruction));
    actual = tinypy_opcode_decode(code, code_size, offset, &instruction);
    TEST_CHECK(actual == expected);
    TEST_CHECK(__instruction_is_clear(&instruction));
    return 0;
}

static int32_t __test_decode_errors(void) {
    static const uint8_t truncated_zero[] = {TINYPY_OP_LOAD_CONST};
    static const uint8_t truncated_one[] = {TINYPY_OP_LOAD_CONST, 0x01U};
    static const uint8_t extended_zero[] = {TINYPY_OP_EXTENDED_ARG};
    static const uint8_t extended_one[] = {TINYPY_OP_EXTENDED_ARG, 0x01U};
    static const uint8_t extended_no_target[] = {
        TINYPY_OP_EXTENDED_ARG, 0x01U, 0x00U};
    static const uint8_t extended_truncated_target[] = {
        TINYPY_OP_EXTENDED_ARG, 0x01U, 0x00U,
        TINYPY_OP_LOAD_CONST, 0x02U};
    static const uint8_t extended_invalid_target[] = {
        TINYPY_OP_EXTENDED_ARG, 0x01U, 0x00U,
        TINYPY_OP_POP_TOP};
    static const uint8_t overflow[] = {
        TINYPY_OP_EXTENDED_ARG, 0xFFU, 0xFFU,
        TINYPY_OP_EXTENDED_ARG, 0xFFU, 0xFFU,
        TINYPY_OP_EXTENDED_ARG, 0xFFU, 0xFFU,
        TINYPY_OP_EXTENDED_ARG, 0xFFU, 0xFFU,
        TINYPY_OP_LOAD_CONST, 0xFFU, 0xFFU};
    static const uint8_t one_byte[] = {TINYPY_OP_POP_TOP};
    tinypy_decoded_instruction_t instruction;

    TEST_CHECK(__expect_decode_error(
                   truncated_zero,
                   sizeof(truncated_zero),
                   0U,
                   TINYPY_OPCODE_DECODE_TRUNCATED) == 0);
    TEST_CHECK(__expect_decode_error(
                   truncated_one,
                   sizeof(truncated_one),
                   0U,
                   TINYPY_OPCODE_DECODE_TRUNCATED) == 0);
    TEST_CHECK(__expect_decode_error(
                   extended_zero,
                   sizeof(extended_zero),
                   0U,
                   TINYPY_OPCODE_DECODE_TRUNCATED) == 0);
    TEST_CHECK(__expect_decode_error(
                   extended_one,
                   sizeof(extended_one),
                   0U,
                   TINYPY_OPCODE_DECODE_TRUNCATED) == 0);
    TEST_CHECK(__expect_decode_error(
                   extended_no_target,
                   sizeof(extended_no_target),
                   0U,
                   TINYPY_OPCODE_DECODE_TRUNCATED) == 0);
    TEST_CHECK(__expect_decode_error(
                   extended_truncated_target,
                   sizeof(extended_truncated_target),
                   0U,
                   TINYPY_OPCODE_DECODE_TRUNCATED) == 0);
    TEST_CHECK(__expect_decode_error(
                   extended_invalid_target,
                   sizeof(extended_invalid_target),
                   0U,
                   TINYPY_OPCODE_DECODE_INVALID_EXTENDED_ARG) == 0);
    TEST_CHECK(__expect_decode_error(
                   overflow,
                   sizeof(overflow),
                   0U,
                   TINYPY_OPCODE_DECODE_ARGUMENT_OVERFLOW) == 0);
    TEST_CHECK(__expect_decode_error(
                   one_byte,
                   sizeof(one_byte),
                   2U,
                   TINYPY_OPCODE_DECODE_INVALID_OFFSET) == 0);
    TEST_CHECK(__expect_decode_error(
                   NULL,
                   0U,
                   0U,
                   TINYPY_OPCODE_DECODE_EOF) == 0);

    (void)memset(&instruction, 0xA5, sizeof(instruction));
    return 0;
}

static int32_t __test_decode_status_names(void) {
    TEST_CHECK(strcmp(tinypy_opcode_decode_status_name(TINYPY_OPCODE_DECODE_OK), "ok") == 0);
    TEST_CHECK(strcmp(
                   tinypy_opcode_decode_status_name(TINYPY_OPCODE_DECODE_TRUNCATED),
                   "truncated instruction") == 0);
    TEST_CHECK(strcmp(
                   tinypy_opcode_decode_status_name((tinypy_opcode_decode_status_e)999),
                   "unknown decode status") == 0);
    return 0;
}

int main(void) {
    if (__test_exact_metadata() != 0) {
        return 1;
    }
    if (__test_lookup_and_categories() != 0) {
        return 1;
    }
    if (__test_decode_basic_and_sequential() != 0) {
        return 1;
    }
    if (__test_decode_extended_argument() != 0) {
        return 1;
    }
    if (__test_decode_unknown_opcodes() != 0) {
        return 1;
    }
    if (__test_decode_errors() != 0) {
        return 1;
    }
    if (__test_decode_status_names() != 0) {
        return 1;
    }

    return 0;
}
