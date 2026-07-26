#include "bytecode_verify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CHECK(condition)                \
    do { \
        if (!(condition)) { \
            (void)fprintf(                   \
                stderr,                      \
                "%s:%d: check failed: %s\n", \
                __FILE__,                    \
                __LINE__,                    \
                #condition);                 \
            return 1;                        \
        }                                    \
    } while (0)

static tinypy_bytecode_metadata_t __test_metadata(size_t stack_size) {
    tinypy_bytecode_metadata_t metadata;

    metadata.const_count = 64U;
    metadata.name_count = 64U;
    metadata.varname_count = 64U;
    metadata.freevar_count = 32U;
    metadata.cellvar_count = 32U;
    metadata.declared_stack_size = stack_size;
    return metadata;
}

static void *__test_align_pointer(void *memory, size_t alignment) {
    uintptr_t address = (uintptr_t)memory;
    uintptr_t remainder = address % (uintptr_t)alignment;

    if (remainder != 0U) {
        address += (uintptr_t)alignment - remainder;
    }

    return (void *)address;
}

static tinypy_bytecode_verify_status_e __test_verify(const uint8_t *code, size_t code_size, const tinypy_bytecode_metadata_t *metadata, const tinypy_bytecode_verify_limits_t *limits, tinypy_bytecode_verify_result_t *result) {
    size_t required = 0U;
    size_t alignment = tinypy_bytecode_verify_scratch_alignment();
    void *allocation;
    void *scratch;
    tinypy_bytecode_verify_status_e status;

    if (tinypy_bytecode_verify_scratch_size(code_size, &required) !=
        TINYPY_BYTECODE_VERIFY_OK) {
        return TINYPY_BYTECODE_VERIFY_SIZE_OVERFLOW;
    }

    allocation = malloc(required + alignment);
    if (allocation == NULL) {
        return TINYPY_BYTECODE_VERIFY_INTERNAL_ERROR;
    }

    scratch = __test_align_pointer(allocation, alignment);
    status = tinypy_bytecode_verify(
        code,
        code_size,
        metadata,
        limits,
        scratch,
        required,
        result);
    free(allocation);
    return status;
}

static size_t __test_emit_instruction(uint8_t *code, size_t offset, uint8_t opcode, uint16_t argument) {
    code[offset] = opcode;
    offset += 1U;

    if (tinypy_opcode_has_argument(opcode)) {
        code[offset] = (uint8_t)(argument & UINT16_C(0xff));
        code[offset + 1U] = (uint8_t)(argument >> 8U);
        offset += 2U;
    }

    return offset;
}

static void __test_patch_argument(uint8_t *code, size_t instruction_offset, uint16_t argument) {
    code[instruction_offset + 1U] =
        (uint8_t)(argument & UINT16_C(0xff));
    code[instruction_offset + 2U] = (uint8_t)(argument >> 8U);
}

static int32_t __test_simple_and_terminators(void) {
    static const uint8_t simple[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_RETURN_VALUE};
    static const uint8_t stop[] = {TINYPY_OP_STOP_CODE};
    static const uint8_t reraise[] = {
        TINYPY_OP_RAISE_VARARGS, 0U, 0U};
    tinypy_bytecode_metadata_t metadata = __test_metadata(1U);
    tinypy_bytecode_verify_result_t result;

    TEST_CHECK(__test_verify(
                   simple,
                   sizeof(simple),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.status == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.error_offset == SIZE_MAX);
    TEST_CHECK(result.instruction_count == 2U);
    TEST_CHECK(result.computed_max_stack == 1U);
    TEST_CHECK(result.required_scratch_size != 0U);

    metadata.declared_stack_size = 0U;
    TEST_CHECK(__test_verify(
                   stop,
                   sizeof(stop),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 0U);
    TEST_CHECK(__test_verify(
                   reraise,
                   sizeof(reraise),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_OK);
    return 0;
}

static int32_t __test_valid_loop(void) {
    static const uint8_t code[] = {
        TINYPY_OP_SETUP_LOOP, 14U, 0U,   /* 0 -> 17 */
        TINYPY_OP_LOAD_CONST, 0U, 0U,    /* 3 */
        TINYPY_OP_GET_ITER,              /* 6 */
        TINYPY_OP_FOR_ITER, 6U, 0U,      /* 7 -> 16 */
        TINYPY_OP_STORE_FAST, 0U, 0U,    /* 10 */
        TINYPY_OP_JUMP_ABSOLUTE, 7U, 0U, /* 13 */
        TINYPY_OP_POP_BLOCK,             /* 16 */
        TINYPY_OP_LOAD_CONST, 1U, 0U,    /* 17 */
        TINYPY_OP_RETURN_VALUE           /* 20 */
    };
    tinypy_bytecode_metadata_t metadata = __test_metadata(2U);
    tinypy_bytecode_verify_result_t result;

    TEST_CHECK(__test_verify(
                   code,
                   sizeof(code),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.instruction_count == 9U);
    TEST_CHECK(result.computed_max_stack == 2U);
    return 0;
}

static int32_t __test_valid_try_except(void) {
    static const uint8_t code[] = {
        TINYPY_OP_SETUP_EXCEPT, 8U, 0U,       /* 0 -> 11 */
        TINYPY_OP_LOAD_CONST, 0U, 0U,         /* 3 */
        TINYPY_OP_POP_TOP,                    /* 6 */
        TINYPY_OP_POP_BLOCK,                  /* 7 */
        TINYPY_OP_JUMP_FORWARD, 18U, 0U,      /* 8 -> 29 */
        TINYPY_OP_DUP_TOP,                    /* 11 */
        TINYPY_OP_LOAD_CONST, 1U, 0U,         /* 12 */
        TINYPY_OP_COMPARE_OP, 10U, 0U,        /* 15 */
        TINYPY_OP_POP_JUMP_IF_FALSE, 28U, 0U, /* 18 */
        TINYPY_OP_POP_TOP,                    /* 21 */
        TINYPY_OP_POP_TOP,                    /* 22 */
        TINYPY_OP_POP_TOP,                    /* 23 */
        TINYPY_OP_LOAD_CONST, 2U, 0U,         /* 24 */
        TINYPY_OP_RETURN_VALUE,               /* 27 */
        TINYPY_OP_END_FINALLY,                /* 28 */
        TINYPY_OP_LOAD_CONST, 3U, 0U,         /* 29 */
        TINYPY_OP_RETURN_VALUE                /* 32 */
    };
    tinypy_bytecode_metadata_t metadata = __test_metadata(5U);
    tinypy_bytecode_verify_result_t result;

    TEST_CHECK(__test_verify(
                   code,
                   sizeof(code),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 5U);
    return 0;
}

static int32_t __test_valid_try_finally_and_return(void) {
    static const uint8_t normal_code[] = {
        TINYPY_OP_SETUP_FINALLY, 8U, 0U, /* 0 -> 11 */
        TINYPY_OP_LOAD_CONST, 0U, 0U,    /* 3 */
        TINYPY_OP_POP_TOP,               /* 6 */
        TINYPY_OP_POP_BLOCK,             /* 7 */
        TINYPY_OP_LOAD_CONST, 1U, 0U,    /* 8: None marker */
        TINYPY_OP_LOAD_CONST, 2U, 0U,    /* 11: final body */
        TINYPY_OP_POP_TOP,               /* 14 */
        TINYPY_OP_END_FINALLY,           /* 15 */
        TINYPY_OP_LOAD_CONST, 3U, 0U,    /* 16 */
        TINYPY_OP_RETURN_VALUE           /* 19 */
    };
    static const uint8_t return_code[] = {
        TINYPY_OP_SETUP_FINALLY, 8U, 0U, /* 0 -> 11 */
        TINYPY_OP_LOAD_CONST, 0U, 0U,    /* 3 */
        TINYPY_OP_RETURN_VALUE,          /* 6 */
        TINYPY_OP_POP_BLOCK,             /* 7: unreachable */
        TINYPY_OP_LOAD_CONST, 1U, 0U,    /* 8: unreachable */
        TINYPY_OP_LOAD_CONST, 2U, 0U,    /* 11: final body */
        TINYPY_OP_POP_TOP,               /* 14 */
        TINYPY_OP_END_FINALLY,           /* 15 */
        TINYPY_OP_LOAD_CONST, 3U, 0U,    /* 16: unreachable */
        TINYPY_OP_RETURN_VALUE           /* 19 */
    };
    tinypy_bytecode_metadata_t metadata = __test_metadata(4U);
    tinypy_bytecode_verify_result_t result;

    TEST_CHECK(__test_verify(
                   normal_code,
                   sizeof(normal_code),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 4U);
    TEST_CHECK(__test_verify(
                   return_code,
                   sizeof(return_code),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 4U);
    return 0;
}

static int32_t __test_valid_with_and_generator(void) {
    static const uint8_t with_code[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U, /* 0: context */
        TINYPY_OP_SETUP_WITH, 5U, 0U, /* 3 -> 11 */
        TINYPY_OP_POP_TOP,            /* 6: enter value */
        TINYPY_OP_POP_BLOCK,          /* 7 */
        TINYPY_OP_LOAD_CONST, 1U, 0U, /* 8: None marker */
        TINYPY_OP_WITH_CLEANUP,       /* 11 */
        TINYPY_OP_END_FINALLY,        /* 12 */
        TINYPY_OP_LOAD_CONST, 2U, 0U, /* 13 */
        TINYPY_OP_RETURN_VALUE        /* 16 */
    };
    static const uint8_t generator_code[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_YIELD_VALUE,
        TINYPY_OP_POP_TOP,
        TINYPY_OP_LOAD_CONST, 1U, 0U,
        TINYPY_OP_RETURN_VALUE};
    tinypy_bytecode_metadata_t metadata = __test_metadata(4U);
    tinypy_bytecode_verify_result_t result;

    TEST_CHECK(__test_verify(
                   with_code,
                   sizeof(with_code),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 4U);

    metadata.declared_stack_size = 1U;
    TEST_CHECK(__test_verify(
                   generator_code,
                   sizeof(generator_code),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 1U);
    return 0;
}

static int32_t __test_nested_pending_reasons(void) {
    /* Emitted by the local CPython 2.7.18 reference compiler. */
    static const uint8_t nested_finally[] = {
        TINYPY_OP_SETUP_FINALLY, 8U, 0U,
        TINYPY_OP_LOAD_FAST, 0U, 0U,
        TINYPY_OP_RETURN_VALUE,
        TINYPY_OP_POP_BLOCK,
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_SETUP_FINALLY, 14U, 0U,
        TINYPY_OP_LOAD_FAST, 0U, 0U,
        TINYPY_OP_LOAD_CONST, 1U, 0U,
        TINYPY_OP_INPLACE_ADD,
        TINYPY_OP_STORE_FAST, 0U, 0U,
        TINYPY_OP_POP_BLOCK,
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_LOAD_FAST, 0U, 0U,
        TINYPY_OP_LOAD_CONST, 2U, 0U,
        TINYPY_OP_INPLACE_ADD,
        TINYPY_OP_STORE_FAST, 0U, 0U,
        TINYPY_OP_END_FINALLY,
        TINYPY_OP_END_FINALLY,
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_RETURN_VALUE};
    static const uint8_t nested_with[] = {
        TINYPY_OP_SETUP_FINALLY, 8U, 0U,
        TINYPY_OP_LOAD_FAST, 0U, 0U,
        TINYPY_OP_RETURN_VALUE,
        TINYPY_OP_POP_BLOCK,
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_LOAD_FAST, 1U, 0U,
        TINYPY_OP_SETUP_WITH, 15U, 0U,
        TINYPY_OP_POP_TOP,
        TINYPY_OP_LOAD_FAST, 0U, 0U,
        TINYPY_OP_LOAD_CONST, 1U, 0U,
        TINYPY_OP_INPLACE_ADD,
        TINYPY_OP_STORE_FAST, 0U, 0U,
        TINYPY_OP_POP_BLOCK,
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_WITH_CLEANUP,
        TINYPY_OP_END_FINALLY,
        TINYPY_OP_END_FINALLY,
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_RETURN_VALUE};
    static const uint8_t nested_except[] = {
        TINYPY_OP_SETUP_FINALLY, 8U, 0U,
        TINYPY_OP_LOAD_FAST, 0U, 0U,
        TINYPY_OP_RETURN_VALUE,
        TINYPY_OP_POP_BLOCK,
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_SETUP_EXCEPT, 14U, 0U,
        TINYPY_OP_LOAD_FAST, 0U, 0U,
        TINYPY_OP_LOAD_CONST, 1U, 0U,
        TINYPY_OP_INPLACE_ADD,
        TINYPY_OP_STORE_FAST, 0U, 0U,
        TINYPY_OP_POP_BLOCK,
        TINYPY_OP_JUMP_FORWARD, 23U, 0U,
        TINYPY_OP_DUP_TOP,
        TINYPY_OP_LOAD_GLOBAL, 0U, 0U,
        TINYPY_OP_COMPARE_OP, 10U, 0U,
        TINYPY_OP_POP_JUMP_IF_FALSE, 50U, 0U,
        TINYPY_OP_POP_TOP,
        TINYPY_OP_POP_TOP,
        TINYPY_OP_POP_TOP,
        TINYPY_OP_LOAD_CONST, 2U, 0U,
        TINYPY_OP_STORE_FAST, 0U, 0U,
        TINYPY_OP_JUMP_FORWARD, 1U, 0U,
        TINYPY_OP_END_FINALLY,
        TINYPY_OP_END_FINALLY,
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_RETURN_VALUE};
    tinypy_bytecode_metadata_t metadata = __test_metadata(8U);
    tinypy_bytecode_verify_result_t result;

    TEST_CHECK(__test_verify(
                   nested_finally,
                   sizeof(nested_finally),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 8U);

    metadata.declared_stack_size = 9U;
    TEST_CHECK(__test_verify(
                   nested_with,
                   sizeof(nested_with),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 7U);

    metadata.declared_stack_size = 8U;
    TEST_CHECK(__test_verify(
                   nested_except,
                   sizeof(nested_except),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 8U);
    return 0;
}

static int32_t __test_variable_stack_effects(void) {
    static const uint8_t unpack_build[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_UNPACK_SEQUENCE, 3U, 0U,
        TINYPY_OP_BUILD_TUPLE, 3U, 0U,
        TINYPY_OP_RETURN_VALUE};
    static const uint8_t duplicate[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_LOAD_CONST, 1U, 0U,
        TINYPY_OP_DUP_TOPX, 2U, 0U,
        TINYPY_OP_POP_TOP,
        TINYPY_OP_POP_TOP,
        TINYPY_OP_POP_TOP,
        TINYPY_OP_RETURN_VALUE};
    static const uint8_t call[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U, /* function */
        TINYPY_OP_LOAD_CONST, 1U, 0U, /* positional */
        TINYPY_OP_LOAD_CONST, 2U, 0U, /* positional */
        TINYPY_OP_LOAD_CONST, 3U, 0U, /* keyword name */
        TINYPY_OP_LOAD_CONST, 4U, 0U, /* keyword value */
        TINYPY_OP_CALL_FUNCTION, 2U, 1U,
        TINYPY_OP_RETURN_VALUE};
    static const uint8_t call_var_kw[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U, /* function */
        TINYPY_OP_LOAD_CONST, 1U, 0U, /* positional */
        TINYPY_OP_LOAD_CONST, 2U, 0U, /* keyword name */
        TINYPY_OP_LOAD_CONST, 3U, 0U, /* keyword value */
        TINYPY_OP_LOAD_CONST, 4U, 0U, /* *args */
        TINYPY_OP_LOAD_CONST, 5U, 0U, /* **kwargs */
        TINYPY_OP_CALL_FUNCTION_VAR_KW, 1U, 1U,
        TINYPY_OP_RETURN_VALUE};
    static const uint8_t make_functions[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U, /* default */
        TINYPY_OP_LOAD_CONST, 1U, 0U, /* default */
        TINYPY_OP_LOAD_CONST, 2U, 0U, /* code */
        TINYPY_OP_MAKE_FUNCTION, 2U, 0U,
        TINYPY_OP_POP_TOP,
        TINYPY_OP_LOAD_CONST, 3U, 0U, /* default */
        TINYPY_OP_LOAD_CONST, 4U, 0U, /* closure */
        TINYPY_OP_LOAD_CONST, 5U, 0U, /* code */
        TINYPY_OP_MAKE_CLOSURE, 1U, 0U,
        TINYPY_OP_RETURN_VALUE};
    static const uint8_t collection_ops[] = {
        TINYPY_OP_BUILD_MAP, 0U, 0U,
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_LOAD_CONST, 1U, 0U,
        TINYPY_OP_STORE_MAP,
        TINYPY_OP_LOAD_CONST, 2U, 0U,
        TINYPY_OP_LOAD_CONST, 3U, 0U,
        TINYPY_OP_MAP_ADD, 1U, 0U,
        TINYPY_OP_RETURN_VALUE};
    static const uint8_t slice_raise[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_LOAD_CONST, 1U, 0U,
        TINYPY_OP_LOAD_CONST, 2U, 0U,
        TINYPY_OP_BUILD_SLICE, 3U, 0U,
        TINYPY_OP_POP_TOP,
        TINYPY_OP_LOAD_CONST, 3U, 0U,
        TINYPY_OP_LOAD_CONST, 4U, 0U,
        TINYPY_OP_LOAD_CONST, 5U, 0U,
        TINYPY_OP_RAISE_VARARGS, 3U, 0U};
    tinypy_bytecode_metadata_t metadata = __test_metadata(6U);
    tinypy_bytecode_verify_result_t result;

    TEST_CHECK(__test_verify(unpack_build, sizeof(unpack_build), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 3U);
    TEST_CHECK(__test_verify(duplicate, sizeof(duplicate), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 4U);
    TEST_CHECK(__test_verify(call, sizeof(call), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 5U);
    TEST_CHECK(__test_verify(call_var_kw, sizeof(call_var_kw), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 6U);
    TEST_CHECK(__test_verify(make_functions, sizeof(make_functions), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 3U);
    TEST_CHECK(__test_verify(collection_ops, sizeof(collection_ops), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 3U);
    TEST_CHECK(__test_verify(slice_raise, sizeof(slice_raise), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(result.computed_max_stack == 3U);
    return 0;
}

static int32_t __test_all_defined_opcodes_reach_effect_model(void) {
    uint8_t code[128];
    tinypy_bytecode_metadata_t metadata = __test_metadata(16U);
    tinypy_bytecode_verify_result_t result;
    size_t opcode_value;
    size_t covered = 0U;

    for (opcode_value = 0U;
         opcode_value != (size_t)TINYPY_OPCODE_COUNT;
         ++opcode_value) {
        uint8_t opcode = (uint8_t)opcode_value;
        size_t offset = 0U;
        size_t prefix_setup = SIZE_MAX;
        size_t instruction_offset;
        size_t instruction_next;
        size_t fallthrough_terminal;
        size_t jump_terminal;
        uint16_t argument = UINT16_C(0);
        size_t index;

        if (!tinypy_opcode_is_defined(opcode)) {
            continue;
        }
        covered += 1U;

        if (opcode == TINYPY_OP_EXTENDED_ARG) {
            offset = __test_emit_instruction(
                code,
                offset,
                TINYPY_OP_EXTENDED_ARG,
                UINT16_C(0));
            offset = __test_emit_instruction(
                code,
                offset,
                TINYPY_OP_LOAD_CONST,
                UINT16_C(0));
            offset = __test_emit_instruction(
                code,
                offset,
                TINYPY_OP_STOP_CODE,
                UINT16_C(0));
            TEST_CHECK(__test_verify(
                           code,
                           offset,
                           &metadata,
                           NULL,
                           &result) == TINYPY_BYTECODE_VERIFY_OK);
            TEST_CHECK(result.instruction_count == 2U);
            continue;
        }

        for (index = 0U; index != 8U; ++index) {
            offset = __test_emit_instruction(
                code,
                offset,
                TINYPY_OP_LOAD_CONST,
                UINT16_C(0));
        }

        if (opcode == TINYPY_OP_POP_BLOCK || opcode == TINYPY_OP_BREAK_LOOP || opcode == TINYPY_OP_CONTINUE_LOOP) {
            prefix_setup = offset;
            offset = __test_emit_instruction(
                code,
                offset,
                TINYPY_OP_SETUP_LOOP,
                UINT16_C(0));
        }

        instruction_offset = offset;
        switch (opcode) {
        case TINYPY_OP_DUP_TOPX:
        case TINYPY_OP_UNPACK_SEQUENCE:
        case TINYPY_OP_BUILD_TUPLE:
        case TINYPY_OP_BUILD_LIST:
        case TINYPY_OP_BUILD_SET:
        case TINYPY_OP_BUILD_SLICE:
            argument = UINT16_C(2);
            break;
        case TINYPY_OP_LIST_APPEND:
        case TINYPY_OP_SET_ADD:
        case TINYPY_OP_MAP_ADD:
            argument = UINT16_C(1);
            break;
        default:
            argument = UINT16_C(0);
            break;
        }
        offset = __test_emit_instruction(
            code,
            offset,
            opcode,
            argument);
        instruction_next = offset;
        fallthrough_terminal = offset;
        offset = __test_emit_instruction(
            code,
            offset,
            TINYPY_OP_STOP_CODE,
            UINT16_C(0));
        jump_terminal = offset;
        offset = __test_emit_instruction(
            code,
            offset,
            TINYPY_OP_STOP_CODE,
            UINT16_C(0));

        if (prefix_setup != SIZE_MAX) {
            size_t setup_next = prefix_setup + 3U;

            TEST_CHECK(jump_terminal >= setup_next);
            TEST_CHECK(jump_terminal - setup_next <= UINT16_MAX);
            __test_patch_argument(
                code,
                prefix_setup,
                (uint16_t)(jump_terminal - setup_next));
        }

        if (opcode == TINYPY_OP_CONTINUE_LOOP) {
            TEST_CHECK(instruction_offset <= UINT16_MAX);
            __test_patch_argument(
                code,
                instruction_offset,
                (uint16_t)instruction_offset);
        }
        else if (tinypy_opcode_has_category(
                     opcode,
                     TINYPY_OPCODE_CATEGORY_JREL)) {
            TEST_CHECK(jump_terminal >= instruction_next);
            TEST_CHECK(jump_terminal - instruction_next <= UINT16_MAX);
            __test_patch_argument(
                code,
                instruction_offset,
                (uint16_t)(jump_terminal - instruction_next));
        }
        else if (tinypy_opcode_has_category(
                     opcode,
                     TINYPY_OPCODE_CATEGORY_JABS)) {
            TEST_CHECK(jump_terminal <= UINT16_MAX);
            __test_patch_argument(
                code,
                instruction_offset,
                (uint16_t)jump_terminal);
        }

        TEST_CHECK(fallthrough_terminal < jump_terminal);
        TEST_CHECK(__test_verify(
                       code,
                       offset,
                       &metadata,
                       NULL,
                       &result) == TINYPY_BYTECODE_VERIFY_OK);
    }

    TEST_CHECK(covered == 119U);
    return 0;
}

static int32_t __test_decode_and_opcode_errors(void) {
    static const uint8_t unknown[] = {6U};
    static const uint8_t unknown_with_argument[] = {200U, 0U, 0U};
    static const uint8_t truncated[] = {TINYPY_OP_LOAD_CONST, 0U};
    static const uint8_t truncated_extended[] = {
        TINYPY_OP_EXTENDED_ARG, 0U, 0U};
    static const uint8_t extended_before_noarg[] = {
        TINYPY_OP_EXTENDED_ARG, 0U, 0U,
        TINYPY_OP_NOP};
    tinypy_bytecode_metadata_t metadata = __test_metadata(8U);
    tinypy_bytecode_verify_result_t result;

    TEST_CHECK(__test_verify(unknown, sizeof(unknown), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_UNKNOWN_OPCODE);
    TEST_CHECK(result.error_offset == 0U);
    TEST_CHECK(result.error_opcode == 6U);
    TEST_CHECK(__test_verify(
                   unknown_with_argument,
                   sizeof(unknown_with_argument),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_UNKNOWN_OPCODE);
    TEST_CHECK(result.error_opcode == 200U);

    TEST_CHECK(__test_verify(truncated, sizeof(truncated), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_DECODE_ERROR);
    TEST_CHECK(result.decode_status == TINYPY_OPCODE_DECODE_TRUNCATED);
    TEST_CHECK(result.error_offset == 0U);
    TEST_CHECK(__test_verify(
                   truncated_extended,
                   sizeof(truncated_extended),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_DECODE_ERROR);
    TEST_CHECK(result.decode_status == TINYPY_OPCODE_DECODE_TRUNCATED);
    TEST_CHECK(__test_verify(
                   extended_before_noarg,
                   sizeof(extended_before_noarg),
                   &metadata,
                   NULL,
                   &result) == TINYPY_BYTECODE_VERIFY_DECODE_ERROR);
    TEST_CHECK(
        result.decode_status == TINYPY_OPCODE_DECODE_INVALID_EXTENDED_ARG);
    return 0;
}

static int32_t __test_operand_bounds(void) {
    static const uint8_t const_code[] = {TINYPY_OP_LOAD_CONST, 1U, 0U, TINYPY_OP_RETURN_VALUE};
    static const uint8_t name_code[] = {TINYPY_OP_LOAD_NAME, 1U, 0U, TINYPY_OP_RETURN_VALUE};
    static const uint8_t local_code[] = {TINYPY_OP_LOAD_FAST, 1U, 0U, TINYPY_OP_RETURN_VALUE};
    static const uint8_t free_code[] = {TINYPY_OP_LOAD_DEREF, 2U, 0U, TINYPY_OP_RETURN_VALUE};
    static const uint8_t compare_code[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_COMPARE_OP, 12U, 0U,
        TINYPY_OP_RETURN_VALUE};
    tinypy_bytecode_metadata_t metadata = __test_metadata(8U);
    tinypy_bytecode_verify_result_t result;

    metadata.const_count = 1U;
    metadata.name_count = 1U;
    metadata.varname_count = 1U;
    metadata.cellvar_count = 1U;
    metadata.freevar_count = 1U;

    TEST_CHECK(__test_verify(const_code, sizeof(const_code), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_OPERAND_OUT_OF_RANGE);
    TEST_CHECK(result.error_opcode == TINYPY_OP_LOAD_CONST);
    TEST_CHECK(__test_verify(name_code, sizeof(name_code), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_OPERAND_OUT_OF_RANGE);
    TEST_CHECK(result.error_opcode == TINYPY_OP_LOAD_NAME);
    TEST_CHECK(__test_verify(local_code, sizeof(local_code), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_OPERAND_OUT_OF_RANGE);
    TEST_CHECK(result.error_opcode == TINYPY_OP_LOAD_FAST);
    TEST_CHECK(__test_verify(free_code, sizeof(free_code), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_OPERAND_OUT_OF_RANGE);
    TEST_CHECK(result.error_opcode == TINYPY_OP_LOAD_DEREF);
    TEST_CHECK(__test_verify(compare_code, sizeof(compare_code), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_OPERAND_OUT_OF_RANGE);
    TEST_CHECK(result.error_opcode == TINYPY_OP_COMPARE_OP);
    return 0;
}

static int32_t __test_invalid_special_operands(void) {
    static const uint8_t dup[] = {TINYPY_OP_DUP_TOPX, 1U, 0U};
    static const uint8_t raise[] = {TINYPY_OP_RAISE_VARARGS, 4U, 0U};
    static const uint8_t slice[] = {TINYPY_OP_BUILD_SLICE, 1U, 0U};
    static const uint8_t append[] = {TINYPY_OP_LIST_APPEND, 0U, 0U};
    static const uint8_t extended_call[] = {
        TINYPY_OP_EXTENDED_ARG, 1U, 0U,
        TINYPY_OP_CALL_FUNCTION, 0U, 0U};
    const uint8_t *codes[] = {dup, raise, slice, append, extended_call};
    const size_t sizes[] = {
        sizeof(dup), sizeof(raise), sizeof(slice), sizeof(append), sizeof(extended_call)};
    size_t index;
    tinypy_bytecode_metadata_t metadata = __test_metadata(8U);
    tinypy_bytecode_verify_result_t result;

    for (index = 0U; index != sizeof(codes) / sizeof(codes[0]); ++index) {
        TEST_CHECK(__test_verify(codes[index], sizes[index], &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_INVALID_OPERAND);
    }
    return 0;
}

static int32_t __test_jump_validation(void) {
    static const uint8_t out_of_range[] = {
        TINYPY_OP_JUMP_ABSOLUTE, 10U, 0U};
    static const uint8_t into_argument[] = {
        TINYPY_OP_JUMP_ABSOLUTE, 1U, 0U,
        TINYPY_OP_STOP_CODE};
    static const uint8_t into_extended_payload[] = {
        TINYPY_OP_JUMP_ABSOLUTE, 6U, 0U,
        TINYPY_OP_EXTENDED_ARG, 0U, 0U,
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_RETURN_VALUE};
    static const uint8_t to_extended_boundary[] = {
        TINYPY_OP_JUMP_ABSOLUTE, 3U, 0U,
        TINYPY_OP_EXTENDED_ARG, 0U, 0U,
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_RETURN_VALUE};
    static const uint8_t relative_out_of_range[] = {
        TINYPY_OP_JUMP_FORWARD, 255U, 255U};
    tinypy_bytecode_metadata_t metadata = __test_metadata(8U);
    tinypy_bytecode_verify_result_t result;

    TEST_CHECK(__test_verify(out_of_range, sizeof(out_of_range), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_JUMP_OUT_OF_RANGE);
    TEST_CHECK(__test_verify(relative_out_of_range, sizeof(relative_out_of_range), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_JUMP_OUT_OF_RANGE);
    TEST_CHECK(__test_verify(into_argument, sizeof(into_argument), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_JUMP_NOT_INSTRUCTION);
    TEST_CHECK(__test_verify(into_extended_payload, sizeof(into_extended_payload), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_JUMP_NOT_INSTRUCTION);
    TEST_CHECK(__test_verify(to_extended_boundary, sizeof(to_extended_boundary), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_OK);
    return 0;
}

static int32_t __test_control_flow_errors(void) {
    static const uint8_t fallthrough[] = {TINYPY_OP_NOP};
    static const uint8_t underflow[] = {TINYPY_OP_POP_TOP};
    static const uint8_t inconsistent_depth[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U,          /* 0 */
        TINYPY_OP_JUMP_IF_TRUE_OR_POP, 7U, 0U, /* 3 */
        TINYPY_OP_NOP,                         /* 6 */
        TINYPY_OP_NOP,                         /* 7 */
        TINYPY_OP_STOP_CODE                    /* 8 */
    };
    static const uint8_t pop_block[] = {TINYPY_OP_POP_BLOCK};
    static const uint8_t break_outside[] = {TINYPY_OP_BREAK_LOOP};
    static const uint8_t continue_outside[] = {
        TINYPY_OP_CONTINUE_LOOP, 0U, 0U};
    static const uint8_t end_finally[] = {TINYPY_OP_END_FINALLY};
    static const uint8_t block_mismatch[] = {
        TINYPY_OP_SETUP_LOOP, 14U, 0U,        /* 0 -> 17 */
        TINYPY_OP_LOAD_CONST, 0U, 0U,         /* 3 */
        TINYPY_OP_POP_JUMP_IF_FALSE, 13U, 0U, /* 6 */
        TINYPY_OP_POP_BLOCK,                  /* 9 */
        TINYPY_OP_JUMP_FORWARD, 3U, 0U,       /* 10 -> 16 */
        TINYPY_OP_JUMP_ABSOLUTE, 16U, 0U,     /* 13 -> 16 */
        TINYPY_OP_STOP_CODE,                  /* 16 */
        TINYPY_OP_STOP_CODE                   /* 17 */
    };
    static const uint8_t missing_with_cleanup[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U, /* 0 */
        TINYPY_OP_SETUP_WITH, 1U, 0U, /* 3 -> 7 */
        TINYPY_OP_STOP_CODE,          /* 6 */
        TINYPY_OP_END_FINALLY         /* 7 */
    };
    tinypy_bytecode_metadata_t metadata = __test_metadata(8U);
    tinypy_bytecode_verify_result_t result;

    TEST_CHECK(__test_verify(fallthrough, sizeof(fallthrough), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_FALLTHROUGH_PAST_END);
    TEST_CHECK(__test_verify(underflow, sizeof(underflow), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_STACK_UNDERFLOW);
    TEST_CHECK(__test_verify(inconsistent_depth, sizeof(inconsistent_depth), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_STACK_DEPTH_MISMATCH);
    TEST_CHECK(result.error_offset == 7U);
    TEST_CHECK(__test_verify(pop_block, sizeof(pop_block), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_BLOCK_STACK_UNDERFLOW);
    TEST_CHECK(__test_verify(break_outside, sizeof(break_outside), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_BREAK_OUTSIDE_LOOP);
    TEST_CHECK(__test_verify(continue_outside, sizeof(continue_outside), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_CONTINUE_OUTSIDE_LOOP);
    TEST_CHECK(__test_verify(end_finally, sizeof(end_finally), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_STACK_UNDERFLOW);
    TEST_CHECK(__test_verify(block_mismatch, sizeof(block_mismatch), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_BLOCK_STACK_MISMATCH);
    TEST_CHECK(result.error_offset == 16U);
    TEST_CHECK(__test_verify(missing_with_cleanup, sizeof(missing_with_cleanup), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_INVALID_FINALLY_STATE);
    return 0;
}

static int32_t __test_declared_stack_limits_and_block_limits(void) {
    static const uint8_t stack_two[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_LOAD_CONST, 1U, 0U,
        TINYPY_OP_POP_TOP,
        TINYPY_OP_RETURN_VALUE};
    static const uint8_t nested_blocks[] = {
        TINYPY_OP_SETUP_LOOP, 3U, 0U, /* 0 -> 6 */
        TINYPY_OP_SETUP_LOOP, 0U, 0U, /* 3 -> 6 */
        TINYPY_OP_BREAK_LOOP,         /* 6 */
        TINYPY_OP_STOP_CODE           /* 7 */
    };
    tinypy_bytecode_metadata_t metadata = __test_metadata(1U);
    tinypy_bytecode_verify_limits_t limits;
    tinypy_bytecode_verify_result_t result;

    TEST_CHECK(__test_verify(stack_two, sizeof(stack_two), &metadata, NULL, &result) == TINYPY_BYTECODE_VERIFY_DECLARED_STACK_TOO_SMALL);
    TEST_CHECK(result.computed_max_stack == 2U);

    metadata.declared_stack_size = 8U;
    (void)memset(&limits, 0, sizeof(limits));
    limits.max_stack_depth = 1U;
    TEST_CHECK(__test_verify(stack_two, sizeof(stack_two), &metadata, &limits, &result) == TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW);

    (void)memset(&limits, 0, sizeof(limits));
    limits.max_bytecode_size = sizeof(stack_two) - 1U;
    TEST_CHECK(__test_verify(stack_two, sizeof(stack_two), &metadata, &limits, &result) == TINYPY_BYTECODE_VERIFY_LIMIT_EXCEEDED);

    (void)memset(&limits, 0, sizeof(limits));
    limits.max_instruction_count = 1U;
    TEST_CHECK(__test_verify(stack_two, sizeof(stack_two), &metadata, &limits, &result) == TINYPY_BYTECODE_VERIFY_LIMIT_EXCEEDED);

    (void)memset(&limits, 0, sizeof(limits));
    limits.max_block_depth = 1U;
    TEST_CHECK(__test_verify(nested_blocks, sizeof(nested_blocks), &metadata, &limits, &result) == TINYPY_BYTECODE_VERIFY_BLOCK_STACK_OVERFLOW);
    return 0;
}

static int32_t __test_scratch_contract(void) {
    static const uint8_t code[] = {
        TINYPY_OP_LOAD_CONST, 0U, 0U,
        TINYPY_OP_RETURN_VALUE};
    tinypy_bytecode_metadata_t metadata = __test_metadata(1U);
    tinypy_bytecode_verify_result_t result;
    size_t required = 0U;
    size_t alignment = tinypy_bytecode_verify_scratch_alignment();
    void *allocation;
    void *aligned;

    TEST_CHECK(alignment != 0U);
    TEST_CHECK(tinypy_bytecode_verify_scratch_size(sizeof(code), &required) == TINYPY_BYTECODE_VERIFY_OK);
    TEST_CHECK(required != 0U);
    TEST_CHECK(tinypy_bytecode_verify_scratch_size(SIZE_MAX, &required) == TINYPY_BYTECODE_VERIFY_SIZE_OVERFLOW);

    TEST_CHECK(tinypy_bytecode_verify(code, sizeof(code), &metadata, NULL, NULL, 0U, &result) == TINYPY_BYTECODE_VERIFY_SCRATCH_TOO_SMALL);
    TEST_CHECK(result.required_scratch_size != 0U);

    allocation = malloc(result.required_scratch_size + alignment + 1U);
    TEST_CHECK(allocation != NULL);
    aligned = __test_align_pointer(allocation, alignment);
    TEST_CHECK(tinypy_bytecode_verify(code, sizeof(code), &metadata, NULL, aligned, result.required_scratch_size - 1U, &result) == TINYPY_BYTECODE_VERIFY_SCRATCH_TOO_SMALL);
    TEST_CHECK(tinypy_bytecode_verify(code, sizeof(code), &metadata, NULL, (uint8_t *)aligned + 1U, result.required_scratch_size, &result) == TINYPY_BYTECODE_VERIFY_SCRATCH_MISALIGNED);
    free(allocation);

    TEST_CHECK(tinypy_bytecode_verify(NULL, 0U, &metadata, NULL, NULL, 0U, &result) == TINYPY_BYTECODE_VERIFY_EMPTY);
    return 0;
}

static int32_t __test_status_names(void) {
    tinypy_bytecode_verify_status_e status;

    TEST_CHECK(strcmp(tinypy_bytecode_verify_status_name(TINYPY_BYTECODE_VERIFY_OK), "ok") == 0);
    TEST_CHECK(strcmp(tinypy_bytecode_verify_status_name(TINYPY_BYTECODE_VERIFY_STACK_UNDERFLOW), "value stack underflow") == 0);
    for (status = TINYPY_BYTECODE_VERIFY_OK;
         status <= TINYPY_BYTECODE_VERIFY_INTERNAL_ERROR;
         status = (tinypy_bytecode_verify_status_e)((int32_t)status + 1)) {
        TEST_CHECK(strcmp(
                       tinypy_bytecode_verify_status_name(status),
                       "unknown verification status") != 0);
    }
    TEST_CHECK(strcmp(tinypy_bytecode_verify_status_name((tinypy_bytecode_verify_status_e)999), "unknown verification status") == 0);
    return 0;
}

int main(void) {
    if (__test_simple_and_terminators() != 0) {
        return 1;
    }
    if (__test_valid_loop() != 0) {
        return 1;
    }
    if (__test_valid_try_except() != 0) {
        return 1;
    }
    if (__test_valid_try_finally_and_return() != 0) {
        return 1;
    }
    if (__test_valid_with_and_generator() != 0) {
        return 1;
    }
    if (__test_nested_pending_reasons() != 0) {
        return 1;
    }
    if (__test_variable_stack_effects() != 0) {
        return 1;
    }
    if (__test_all_defined_opcodes_reach_effect_model() != 0) {
        return 1;
    }
    if (__test_decode_and_opcode_errors() != 0) {
        return 1;
    }
    if (__test_operand_bounds() != 0) {
        return 1;
    }
    if (__test_invalid_special_operands() != 0) {
        return 1;
    }
    if (__test_jump_validation() != 0) {
        return 1;
    }
    if (__test_control_flow_errors() != 0) {
        return 1;
    }
    if (__test_declared_stack_limits_and_block_limits() != 0) {
        return 1;
    }
    if (__test_scratch_contract() != 0) {
        return 1;
    }
    if (__test_status_names() != 0) {
        return 1;
    }
    return 0;
}
