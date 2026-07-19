#ifndef TINYPY_BYTECODE_BYTECODE_VERIFY_H
#define TINYPY_BYTECODE_BYTECODE_VERIFY_H

#include "opcode.h"

#define TINYPY_BYTECODE_VERIFY_DEFAULT_MAX_BLOCK_DEPTH 20U
#define TINYPY_BYTECODE_VERIFY_CONTEXTS_PER_BYTE 4U

typedef struct tinypy_bytecode_metadata_t {
    size_t const_count;
    size_t name_count;
    size_t varname_count;
    size_t freevar_count;
    size_t cellvar_count;
    size_t declared_stack_size;
} tinypy_bytecode_metadata_t;

typedef struct tinypy_bytecode_verify_limits_t {
    /* Zero means no additional limit for these three fields. */
    size_t max_bytecode_size;
    size_t max_instruction_count;
    size_t max_stack_depth;

    /* Zero selects CPython 2.7's CO_MAXBLOCKS-compatible default of 20. */
    size_t max_block_depth;
} tinypy_bytecode_verify_limits_t;

typedef enum tinypy_bytecode_verify_status_e {
    TINYPY_BYTECODE_VERIFY_OK = 0,
    TINYPY_BYTECODE_VERIFY_SIZE_OVERFLOW = 1,
    TINYPY_BYTECODE_VERIFY_LIMIT_EXCEEDED = 2,
    TINYPY_BYTECODE_VERIFY_SCRATCH_TOO_SMALL = 3,
    TINYPY_BYTECODE_VERIFY_SCRATCH_MISALIGNED = 4,
    TINYPY_BYTECODE_VERIFY_EMPTY = 5,
    TINYPY_BYTECODE_VERIFY_DECODE_ERROR = 6,
    TINYPY_BYTECODE_VERIFY_UNKNOWN_OPCODE = 7,
    TINYPY_BYTECODE_VERIFY_OPERAND_OUT_OF_RANGE = 8,
    TINYPY_BYTECODE_VERIFY_INVALID_OPERAND = 9,
    TINYPY_BYTECODE_VERIFY_JUMP_OUT_OF_RANGE = 10,
    TINYPY_BYTECODE_VERIFY_JUMP_NOT_INSTRUCTION = 11,
    TINYPY_BYTECODE_VERIFY_FALLTHROUGH_PAST_END = 12,
    TINYPY_BYTECODE_VERIFY_STACK_UNDERFLOW = 13,
    TINYPY_BYTECODE_VERIFY_STACK_DEPTH_MISMATCH = 14,
    TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW = 15,
    TINYPY_BYTECODE_VERIFY_DECLARED_STACK_TOO_SMALL = 16,
    TINYPY_BYTECODE_VERIFY_BLOCK_STACK_UNDERFLOW = 17,
    TINYPY_BYTECODE_VERIFY_BLOCK_STACK_OVERFLOW = 18,
    TINYPY_BYTECODE_VERIFY_BLOCK_STACK_MISMATCH = 19,
    TINYPY_BYTECODE_VERIFY_BREAK_OUTSIDE_LOOP = 20,
    TINYPY_BYTECODE_VERIFY_CONTINUE_OUTSIDE_LOOP = 21,
    TINYPY_BYTECODE_VERIFY_INVALID_FINALLY_STATE = 22,
    TINYPY_BYTECODE_VERIFY_INTERNAL_ERROR = 23
} tinypy_bytecode_verify_status_e;

typedef struct tinypy_bytecode_verify_result_t {
    tinypy_bytecode_verify_status_e status;
    tinypy_opcode_decode_status_e decode_status;
    size_t error_offset;
    size_t instruction_count;
    size_t computed_max_stack;
    size_t required_scratch_size;
    uint64_t error_argument;
    uint8_t error_opcode;
    uint8_t reserved[7];
} tinypy_bytecode_verify_result_t;

/*
 * Scratch is linear in bytecode_size and contains no owning pointers after
 * the call.  It reserves TINYPY_BYTECODE_VERIFY_CONTEXTS_PER_BYTE abstract CFG
 * contexts per encoded byte; adversarial state expansion beyond that budget
 * reports TINYPY_BYTECODE_VERIFY_LIMIT_EXCEEDED.  The caller retains ownership
 * and may reuse the buffer immediately.
 */
size_t tinypy_bytecode_verify_scratch_alignment(void);

tinypy_bytecode_verify_status_e tinypy_bytecode_verify_scratch_size(size_t bytecode_size, size_t *out_size);

/*
 * Verification models normal edges, both branches of conditional opcodes,
 * loop exits, and one conservative exception entry for every SETUP_EXCEPT,
 * SETUP_FINALLY, and SETUP_WITH.  YIELD_VALUE is modeled as a resumed
 * fallthrough with unchanged depth.  Runtime type/value behavior and the
 * exact instruction that raises an exception are intentionally out of scope.
 * Decode, operand, and jump-boundary checks cover every encoded instruction;
 * stack, block, marker, and fallthrough checks cover reachable CFG states.
 */
tinypy_bytecode_verify_status_e tinypy_bytecode_verify(const uint8_t *bytecode, size_t bytecode_size, const tinypy_bytecode_metadata_t *metadata, const tinypy_bytecode_verify_limits_t *limits, void *scratch, size_t scratch_size, tinypy_bytecode_verify_result_t *out_result);

const char *tinypy_bytecode_verify_status_name(tinypy_bytecode_verify_status_e status);

#endif
