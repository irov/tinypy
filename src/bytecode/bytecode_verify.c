#include "bytecode_verify.h"

#include <assert.h>
#include <string.h>

#define TINYPY_VERIFY_NO_BLOCK SIZE_MAX
#define TINYPY_VERIFY_NO_MARKER SIZE_MAX
#define TINYPY_VERIFY_NO_STATE SIZE_MAX
#define TINYPY_VERIFY_NO_TARGET SIZE_MAX

typedef enum tinypy_verify_reason_e {
    TINYPY_VERIFY_REASON_NORMAL = 0,
    TINYPY_VERIFY_REASON_EXCEPTION_CATCHABLE = 1,
    TINYPY_VERIFY_REASON_EXCEPTION_FINAL = 2,
    TINYPY_VERIFY_REASON_RETURN = 3,
    TINYPY_VERIFY_REASON_BREAK = 4,
    TINYPY_VERIFY_REASON_CONTINUE = 5
} tinypy_verify_reason_e;

typedef struct tinypy_verify_state_t {
    size_t offset;
    size_t stack_depth;
    size_t block_index;
    size_t marker_index;
    size_t next_at_offset;
} tinypy_verify_state_t;

typedef struct tinypy_verify_block_t {
    size_t parent;
    size_t handler;
    size_t level;
    size_t depth;
    uint8_t type;
    uint8_t reserved[7];
} tinypy_verify_block_t;

typedef struct tinypy_verify_marker_t {
    size_t parent;
    size_t resume_depth;
    size_t continue_target;
    uint8_t reason;
    uint8_t needs_with_cleanup;
    uint8_t reserved[6];
} tinypy_verify_marker_t;

typedef struct tinypy_verify_effect_t {
    size_t required;
    size_t pop_count;
    size_t push_count;
} tinypy_verify_effect_t;

typedef union tinypy_verify_max_align_t {
    void *pointer_value;
    void (*function_value)(void);
    size_t size_value;
    uint64_t integer_value;
    long double floating_value;
} tinypy_verify_max_align_t;

typedef struct tinypy_verify_alignment_probe_t {
    char prefix;
    tinypy_verify_max_align_t value;
} tinypy_verify_alignment_probe_t;

#define TINYPY_VERIFY_ALIGNMENT \
    ((size_t)offsetof(tinypy_verify_alignment_probe_t, value))

typedef struct tinypy_verify_context_t {
    const uint8_t *bytecode;
    size_t bytecode_size;
    const tinypy_bytecode_metadata_t *metadata;
    tinypy_bytecode_verify_limits_t limits;
    uint8_t *boundaries;
    size_t *state_heads;
    tinypy_verify_state_t *states;
    size_t state_capacity;
    size_t state_count;
    size_t *worklist;
    size_t worklist_head;
    size_t worklist_tail;
    tinypy_verify_block_t *blocks;
    size_t block_capacity;
    size_t block_count;
    tinypy_verify_marker_t *markers;
    size_t marker_capacity;
    size_t marker_count;
    tinypy_bytecode_verify_result_t *result;
} tinypy_verify_context_t;

//////////////////////////////////////////////////////////////////////////
static int __tinypy_verify_add_size(size_t left, size_t right, size_t *out_value) {
    assert(out_value != NULL);
    if (left > SIZE_MAX - right) {
        return 0;
    }

    *out_value = left + right;
    return 1;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_verify_multiply_size(size_t left, size_t right, size_t *out_value) {
    assert(out_value != NULL);
    if (left != 0U && right > SIZE_MAX / left) {
        return 0;
    }

    *out_value = left * right;
    return 1;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_verify_align_size(size_t value, size_t *out_value) {
    size_t remainder;
    size_t padding;

    assert(out_value != NULL);
    if (TINYPY_VERIFY_ALIGNMENT == 0U) {
        return 0;
    }

    remainder = value % TINYPY_VERIFY_ALIGNMENT;
    if (remainder == 0U) {
        *out_value = value;
        return 1;
    }

    padding = TINYPY_VERIFY_ALIGNMENT - remainder;
    return __tinypy_verify_add_size(value, padding, out_value);
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_verify_clear_result(tinypy_bytecode_verify_result_t *result) {
    size_t index;

    result->status = TINYPY_BYTECODE_VERIFY_OK;
    result->decode_status = TINYPY_OPCODE_DECODE_OK;
    result->error_offset = SIZE_MAX;
    result->instruction_count = 0U;
    result->computed_max_stack = 0U;
    result->required_scratch_size = 0U;
    result->error_argument = UINT64_C(0);
    result->error_opcode = 0U;

    for (index = 0U; index != sizeof(result->reserved); ++index) {
        result->reserved[index] = 0U;
    }
}

//////////////////////////////////////////////////////////////////////////
static tinypy_bytecode_verify_status_e __tinypy_verify_fail(tinypy_verify_context_t *context, tinypy_bytecode_verify_status_e status, size_t offset, uint8_t opcode, uint64_t argument, tinypy_opcode_decode_status_e decode_status) {
    context->result->status = status;
    context->result->decode_status = decode_status;
    context->result->error_offset = offset;
    context->result->error_opcode = opcode;
    context->result->error_argument = argument;
    return status;
}

//////////////////////////////////////////////////////////////////////////
size_t tinypy_bytecode_verify_scratch_alignment(void) {
    return TINYPY_VERIFY_ALIGNMENT;
}

//////////////////////////////////////////////////////////////////////////
tinypy_bytecode_verify_status_e tinypy_bytecode_verify_scratch_size(size_t bytecode_size, size_t *out_size) {
    size_t state_count;
    size_t state_heads_size;
    size_t states_size;
    size_t worklist_size;
    size_t blocks_size;
    size_t markers_size;
    size_t total;

    assert(out_size != NULL);
    *out_size = 0U;

    if (!__tinypy_verify_multiply_size(
            bytecode_size,
            (size_t)TINYPY_BYTECODE_VERIFY_CONTEXTS_PER_BYTE,
            &state_count) || !__tinypy_verify_multiply_size(
            bytecode_size,
            sizeof(size_t),
            &state_heads_size) || !__tinypy_verify_multiply_size(
            state_count,
            sizeof(tinypy_verify_state_t),
            &states_size) || !__tinypy_verify_multiply_size(
            state_count,
            sizeof(size_t),
            &worklist_size) || !__tinypy_verify_multiply_size(
            state_count,
            sizeof(tinypy_verify_block_t),
            &blocks_size) || !__tinypy_verify_multiply_size(
            state_count,
            sizeof(tinypy_verify_marker_t),
            &markers_size)) {
        return TINYPY_BYTECODE_VERIFY_SIZE_OVERFLOW;
    }

    total = bytecode_size;
    if (!__tinypy_verify_align_size(total, &total) || !__tinypy_verify_add_size(total, state_heads_size, &total)) {
        return TINYPY_BYTECODE_VERIFY_SIZE_OVERFLOW;
    }
    if (!__tinypy_verify_align_size(total, &total) || !__tinypy_verify_add_size(total, states_size, &total) || !__tinypy_verify_align_size(total, &total) || !__tinypy_verify_add_size(total, worklist_size, &total) || !__tinypy_verify_align_size(total, &total) || !__tinypy_verify_add_size(total, blocks_size, &total) || !__tinypy_verify_align_size(total, &total) || !__tinypy_verify_add_size(total, markers_size, &total)) {
        return TINYPY_BYTECODE_VERIFY_SIZE_OVERFLOW;
    }

    *out_size = total;
    return TINYPY_BYTECODE_VERIFY_OK;
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_verify_apply_default_limits(const tinypy_bytecode_verify_limits_t *source, tinypy_bytecode_verify_limits_t *destination) {
    if (source == NULL) {
        destination->max_bytecode_size = 0U;
        destination->max_instruction_count = 0U;
        destination->max_stack_depth = 0U;
        destination->max_block_depth =
            TINYPY_BYTECODE_VERIFY_DEFAULT_MAX_BLOCK_DEPTH;
        return;
    }

    *destination = *source;
    if (destination->max_block_depth == 0U) {
        destination->max_block_depth =
            TINYPY_BYTECODE_VERIFY_DEFAULT_MAX_BLOCK_DEPTH;
    }
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_verify_bind_scratch(tinypy_verify_context_t *context, void *scratch) {
    uint8_t *base = (uint8_t *)scratch;
    size_t offset = context->bytecode_size;
    size_t state_count = context->bytecode_size * (size_t)TINYPY_BYTECODE_VERIFY_CONTEXTS_PER_BYTE;

    context->boundaries = base;
    (void)__tinypy_verify_align_size(offset, &offset);
    context->state_heads = (size_t *)(void *)(base + offset);
    offset += context->bytecode_size * sizeof(size_t);
    (void)__tinypy_verify_align_size(offset, &offset);
    context->states = (tinypy_verify_state_t *)(void *)(base + offset);
    offset += state_count * sizeof(tinypy_verify_state_t);
    (void)__tinypy_verify_align_size(offset, &offset);
    context->worklist = (size_t *)(void *)(base + offset);
    offset += state_count * sizeof(size_t);
    (void)__tinypy_verify_align_size(offset, &offset);
    context->blocks = (tinypy_verify_block_t *)(void *)(base + offset);
    offset += state_count * sizeof(tinypy_verify_block_t);
    (void)__tinypy_verify_align_size(offset, &offset);
    context->markers = (tinypy_verify_marker_t *)(void *)(base + offset);
    context->state_capacity = state_count;
    context->block_capacity = state_count;
    context->marker_capacity = state_count;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_verify_argument_to_size(uint64_t argument, size_t *out_value) {
    assert(out_value != NULL);
    if (argument > (uint64_t)SIZE_MAX) {
        return 0;
    }

    *out_value = (size_t)argument;
    return 1;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_bytecode_verify_status_e __tinypy_verify_operand(tinypy_verify_context_t *context, const tinypy_decoded_instruction_t *instruction) {
    uint32_t categories = tinypy_opcode_categories(instruction->opcode);
    uint64_t argument = instruction->argument;
    size_t free_count;

    if (!__tinypy_verify_add_size(
            context->metadata->freevar_count,
            context->metadata->cellvar_count,
            &free_count)) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_SIZE_OVERFLOW,
            instruction->offset,
            instruction->opcode,
            argument,
            TINYPY_OPCODE_DECODE_OK);
    }

    if ((categories & TINYPY_OPCODE_CATEGORY_CONST) != 0U && argument >= (uint64_t)context->metadata->const_count) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_OPERAND_OUT_OF_RANGE,
            instruction->offset,
            instruction->opcode,
            argument,
            TINYPY_OPCODE_DECODE_OK);
    }
    if ((categories & TINYPY_OPCODE_CATEGORY_NAME) != 0U && argument >= (uint64_t)context->metadata->name_count) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_OPERAND_OUT_OF_RANGE,
            instruction->offset,
            instruction->opcode,
            argument,
            TINYPY_OPCODE_DECODE_OK);
    }
    if ((categories & TINYPY_OPCODE_CATEGORY_LOCAL) != 0U && argument >= (uint64_t)context->metadata->varname_count) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_OPERAND_OUT_OF_RANGE,
            instruction->offset,
            instruction->opcode,
            argument,
            TINYPY_OPCODE_DECODE_OK);
    }
    if ((categories & TINYPY_OPCODE_CATEGORY_FREE) != 0U && argument >= (uint64_t)free_count) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_OPERAND_OUT_OF_RANGE,
            instruction->offset,
            instruction->opcode,
            argument,
            TINYPY_OPCODE_DECODE_OK);
    }
    if ((categories & TINYPY_OPCODE_CATEGORY_COMPARE) != 0U && argument >= UINT64_C(12)) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_OPERAND_OUT_OF_RANGE,
            instruction->offset,
            instruction->opcode,
            argument,
            TINYPY_OPCODE_DECODE_OK);
    }

    switch (instruction->opcode) {
    case TINYPY_OP_DUP_TOPX:
        if (argument != UINT64_C(2) && argument != UINT64_C(3)) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_INVALID_OPERAND,
                instruction->offset,
                instruction->opcode,
                argument,
                TINYPY_OPCODE_DECODE_OK);
        }
        break;
    case TINYPY_OP_RAISE_VARARGS:
        if (argument > UINT64_C(3)) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_INVALID_OPERAND,
                instruction->offset,
                instruction->opcode,
                argument,
                TINYPY_OPCODE_DECODE_OK);
        }
        break;
    case TINYPY_OP_BUILD_SLICE:
        if (argument != UINT64_C(2) && argument != UINT64_C(3)) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_INVALID_OPERAND,
                instruction->offset,
                instruction->opcode,
                argument,
                TINYPY_OPCODE_DECODE_OK);
        }
        break;
    case TINYPY_OP_LIST_APPEND:
    case TINYPY_OP_SET_ADD:
    case TINYPY_OP_MAP_ADD:
        if (argument == UINT64_C(0)) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_INVALID_OPERAND,
                instruction->offset,
                instruction->opcode,
                argument,
                TINYPY_OPCODE_DECODE_OK);
        }
        break;
    case TINYPY_OP_CALL_FUNCTION:
    case TINYPY_OP_CALL_FUNCTION_VAR:
    case TINYPY_OP_CALL_FUNCTION_KW:
    case TINYPY_OP_CALL_FUNCTION_VAR_KW:
        /* CPython 2.7 encodes positional/keyword counts in two bytes. */
        if (argument > UINT64_C(0xffff)) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_INVALID_OPERAND,
                instruction->offset,
                instruction->opcode,
                argument,
                TINYPY_OPCODE_DECODE_OK);
        }
        break;
    default:
        break;
    }

    return TINYPY_BYTECODE_VERIFY_OK;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_bytecode_verify_status_e __tinypy_verify_first_pass(tinypy_verify_context_t *context) {
    size_t offset = 0U;

    while (offset != context->bytecode_size) {
        tinypy_decoded_instruction_t instruction;
        tinypy_opcode_decode_status_e decode_status = tinypy_opcode_decode(
            context->bytecode,
            context->bytecode_size,
            offset,
            &instruction);
        tinypy_bytecode_verify_status_e status;

        if (decode_status != TINYPY_OPCODE_DECODE_OK) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_DECODE_ERROR,
                offset,
                context->bytecode[offset],
                UINT64_C(0),
                decode_status);
        }
        if (instruction.defined == 0U) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_UNKNOWN_OPCODE,
                offset,
                instruction.opcode,
                instruction.argument,
                TINYPY_OPCODE_DECODE_OK);
        }

        context->boundaries[offset] = 1U;
        context->result->instruction_count += 1U;

        if (context->limits.max_instruction_count != 0U && context->result->instruction_count >
                context->limits.max_instruction_count) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_LIMIT_EXCEEDED,
                offset,
                instruction.opcode,
                instruction.argument,
                TINYPY_OPCODE_DECODE_OK);
        }

        status = __tinypy_verify_operand(context, &instruction);
        if (status != TINYPY_BYTECODE_VERIFY_OK) {
            return status;
        }

        offset = instruction.next_offset;
    }

    return TINYPY_BYTECODE_VERIFY_OK;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_verify_jump_target(const tinypy_decoded_instruction_t *instruction, size_t *out_target) {
    size_t argument;
    uint32_t categories = tinypy_opcode_categories(instruction->opcode);

    if (!__tinypy_verify_argument_to_size(instruction->argument, &argument)) {
        return 0;
    }

    if ((categories & TINYPY_OPCODE_CATEGORY_JREL) != 0U) {
        return __tinypy_verify_add_size(
            instruction->next_offset,
            argument,
            out_target);
    }

    *out_target = argument;
    return 1;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_bytecode_verify_status_e __tinypy_verify_jumps(tinypy_verify_context_t *context) {
    size_t offset = 0U;

    while (offset != context->bytecode_size) {
        tinypy_decoded_instruction_t instruction;
        uint32_t categories;

        (void)tinypy_opcode_decode(
            context->bytecode,
            context->bytecode_size,
            offset,
            &instruction);
        categories = tinypy_opcode_categories(instruction.opcode);

        if ((categories & (TINYPY_OPCODE_CATEGORY_JREL | TINYPY_OPCODE_CATEGORY_JABS)) != 0U) {
            size_t target;

            if (!__tinypy_verify_jump_target(&instruction, &target) || target >= context->bytecode_size) {
                return __tinypy_verify_fail(
                    context,
                    TINYPY_BYTECODE_VERIFY_JUMP_OUT_OF_RANGE,
                    offset,
                    instruction.opcode,
                    instruction.argument,
                    TINYPY_OPCODE_DECODE_OK);
            }
            if (context->boundaries[target] == 0U) {
                return __tinypy_verify_fail(
                    context,
                    TINYPY_BYTECODE_VERIFY_JUMP_NOT_INSTRUCTION,
                    offset,
                    instruction.opcode,
                    (uint64_t)target,
                    TINYPY_OPCODE_DECODE_OK);
            }
        }

        offset = instruction.next_offset;
    }

    return TINYPY_BYTECODE_VERIFY_OK;
}

//////////////////////////////////////////////////////////////////////////
static uint8_t __tinypy_verify_opcode_at(const tinypy_verify_context_t *context, size_t offset) {
    tinypy_decoded_instruction_t instruction;

    if (offset >= context->bytecode_size || tinypy_opcode_decode(
            context->bytecode,
            context->bytecode_size,
            offset,
            &instruction) != TINYPY_OPCODE_DECODE_OK) {
        return 0U;
    }

    return instruction.opcode;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_verify_blocks_equal(const tinypy_verify_context_t *context, size_t left, size_t right) {
    while (left != TINYPY_VERIFY_NO_BLOCK && right != TINYPY_VERIFY_NO_BLOCK) {
        const tinypy_verify_block_t *left_block = &context->blocks[left];
        const tinypy_verify_block_t *right_block = &context->blocks[right];

        if (left_block->handler != right_block->handler || left_block->level != right_block->level || left_block->depth != right_block->depth || left_block->type != right_block->type) {
            return 0;
        }

        left = left_block->parent;
        right = right_block->parent;
    }

    return left == TINYPY_VERIFY_NO_BLOCK && right == TINYPY_VERIFY_NO_BLOCK;
}

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_verify_reason_item_count(tinypy_verify_reason_e reason) {
    switch (reason) {
    case TINYPY_VERIFY_REASON_EXCEPTION_CATCHABLE:
    case TINYPY_VERIFY_REASON_EXCEPTION_FINAL:
        return 3U;
    case TINYPY_VERIFY_REASON_RETURN:
    case TINYPY_VERIFY_REASON_CONTINUE:
        return 2U;
    case TINYPY_VERIFY_REASON_BREAK:
        return 1U;
    case TINYPY_VERIFY_REASON_NORMAL:
    default:
        return 0U;
    }
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_verify_markers_equal(const tinypy_verify_context_t *context, size_t left, size_t right) {
    while (left != TINYPY_VERIFY_NO_MARKER && right != TINYPY_VERIFY_NO_MARKER) {
        const tinypy_verify_marker_t *left_marker = &context->markers[left];
        const tinypy_verify_marker_t *right_marker = &context->markers[right];

        if (left_marker->resume_depth != right_marker->resume_depth || left_marker->continue_target != right_marker->continue_target || left_marker->reason != right_marker->reason || left_marker->needs_with_cleanup !=
                right_marker->needs_with_cleanup) {
            return 0;
        }

        left = left_marker->parent;
        right = right_marker->parent;
    }

    return left == TINYPY_VERIFY_NO_MARKER && right == TINYPY_VERIFY_NO_MARKER;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_verify_marker_expected_depth(const tinypy_verify_marker_t *marker, size_t *out_depth) {
    size_t verify_reason_item_count = __tinypy_verify_reason_item_count(
        (tinypy_verify_reason_e)marker->reason);
    return __tinypy_verify_add_size(
        marker->resume_depth,
        verify_reason_item_count,
        out_depth);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_bytecode_verify_status_e __tinypy_verify_push_marker(tinypy_verify_context_t *context, const tinypy_decoded_instruction_t *instruction, size_t parent, tinypy_verify_reason_e reason, size_t resume_depth, size_t continue_target, uint8_t needs_with_cleanup, size_t *out_index) {
    tinypy_verify_marker_t *marker;

    if (context->marker_count == context->marker_capacity) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_LIMIT_EXCEEDED,
            instruction->offset,
            instruction->opcode,
            (uint64_t)context->marker_capacity,
            TINYPY_OPCODE_DECODE_OK);
    }

    marker = &context->markers[context->marker_count];
    marker->parent = parent;
    marker->resume_depth = resume_depth;
    marker->continue_target = continue_target;
    marker->reason = (uint8_t)reason;
    marker->needs_with_cleanup = needs_with_cleanup;
    (void)memset(marker->reserved, 0, sizeof(marker->reserved));
    *out_index = context->marker_count;
    context->marker_count += 1U;
    return TINYPY_BYTECODE_VERIFY_OK;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_bytecode_verify_status_e __tinypy_verify_enqueue(tinypy_verify_context_t *context, size_t offset, size_t stack_depth, size_t block_index, size_t marker_index, size_t source_offset, uint8_t source_opcode) {
    size_t state_index;
    tinypy_verify_state_t *state;

    while (marker_index != TINYPY_VERIFY_NO_MARKER) {
        const tinypy_verify_marker_t *marker =
            &context->markers[marker_index];

        if (marker->reason !=
                (uint8_t)TINYPY_VERIFY_REASON_EXCEPTION_CATCHABLE || stack_depth != marker->resume_depth) {
            break;
        }

        marker_index = marker->parent;
    }

    if (offset >= context->bytecode_size || context->boundaries[offset] == 0U) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_INTERNAL_ERROR,
            source_offset,
            source_opcode,
            (uint64_t)offset,
            TINYPY_OPCODE_DECODE_OK);
    }

    if (context->limits.max_stack_depth != 0U && stack_depth > context->limits.max_stack_depth) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW,
            source_offset,
            source_opcode,
            (uint64_t)stack_depth,
            TINYPY_OPCODE_DECODE_OK);
    }

    if (stack_depth > context->result->computed_max_stack) {
        context->result->computed_max_stack = stack_depth;
    }
    if (stack_depth > context->metadata->declared_stack_size) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_DECLARED_STACK_TOO_SMALL,
            source_offset,
            source_opcode,
            (uint64_t)stack_depth,
            TINYPY_OPCODE_DECODE_OK);
    }

    if (marker_index != TINYPY_VERIFY_NO_MARKER) {
        const tinypy_verify_marker_t *marker =
            &context->markers[marker_index];
        size_t minimum_depth;

        if (!__tinypy_verify_marker_expected_depth(marker, &minimum_depth)) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW,
                source_offset,
                source_opcode,
                UINT64_C(0),
                TINYPY_OPCODE_DECODE_OK);
        }
        if (marker->reason ==
            (uint8_t)TINYPY_VERIFY_REASON_EXCEPTION_CATCHABLE) {
            minimum_depth = marker->resume_depth;
        }
        else if (marker->needs_with_cleanup != 0U && !__tinypy_verify_add_size(
                     minimum_depth,
                     1U,
                     &minimum_depth)) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW,
                source_offset,
                source_opcode,
                UINT64_C(0),
                TINYPY_OPCODE_DECODE_OK);
        }
        if (stack_depth < minimum_depth) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_INVALID_FINALLY_STATE,
                source_offset,
                source_opcode,
                (uint64_t)stack_depth,
                TINYPY_OPCODE_DECODE_OK);
        }
    }

    state_index = context->state_heads[offset];
    while (state_index != TINYPY_VERIFY_NO_STATE) {
        state = &context->states[state_index];

        if (__tinypy_verify_markers_equal(
                context,
                state->marker_index,
                marker_index)) {
            if (!__tinypy_verify_blocks_equal(
                    context,
                    state->block_index,
                    block_index)) {
                uint8_t verify_opcode_at = __tinypy_verify_opcode_at(context, offset);
                return __tinypy_verify_fail(
                    context,
                    TINYPY_BYTECODE_VERIFY_BLOCK_STACK_MISMATCH,
                    offset,
                    verify_opcode_at,
                    (uint64_t)stack_depth,
                    TINYPY_OPCODE_DECODE_OK);
            }
            if (state->stack_depth != stack_depth) {
                uint8_t verify_opcode_at = __tinypy_verify_opcode_at(context, offset);
                return __tinypy_verify_fail(
                    context,
                    TINYPY_BYTECODE_VERIFY_STACK_DEPTH_MISMATCH,
                    offset,
                    verify_opcode_at,
                    (uint64_t)stack_depth,
                    TINYPY_OPCODE_DECODE_OK);
            }

            return TINYPY_BYTECODE_VERIFY_OK;
        }

        state_index = state->next_at_offset;
    }

    if (context->state_count == context->state_capacity || context->worklist_tail == context->state_capacity) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_LIMIT_EXCEEDED,
            source_offset,
            source_opcode,
            (uint64_t)context->state_capacity,
            TINYPY_OPCODE_DECODE_OK);
    }

    state_index = context->state_count;
    context->state_count += 1U;
    state = &context->states[state_index];
    state->offset = offset;
    state->stack_depth = stack_depth;
    state->block_index = block_index;
    state->marker_index = marker_index;
    state->next_at_offset = context->state_heads[offset];
    context->state_heads[offset] = state_index;

    context->worklist[context->worklist_tail] = state_index;
    context->worklist_tail += 1U;
    return TINYPY_BYTECODE_VERIFY_OK;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_verify_effect_set(tinypy_verify_effect_t *effect, size_t required, size_t pop_count, size_t push_count) {
    effect->required = required;
    effect->pop_count = pop_count;
    effect->push_count = push_count;
    return 1;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_verify_effect_from_argument(uint64_t argument, size_t extra, size_t *out_value) {
    size_t value;

    return __tinypy_verify_argument_to_size(argument, &value) && __tinypy_verify_add_size(value, extra, out_value);
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_verify_get_effect(uint8_t opcode, uint64_t argument, tinypy_verify_effect_t *effect) {
    size_t count;

    switch (opcode) {
    case TINYPY_OP_STOP_CODE:
    case TINYPY_OP_NOP:
    case TINYPY_OP_BREAK_LOOP:
    case TINYPY_OP_PRINT_NEWLINE:
    case TINYPY_OP_DELETE_NAME:
    case TINYPY_OP_DELETE_GLOBAL:
    case TINYPY_OP_JUMP_FORWARD:
    case TINYPY_OP_JUMP_ABSOLUTE:
    case TINYPY_OP_CONTINUE_LOOP:
    case TINYPY_OP_SETUP_LOOP:
    case TINYPY_OP_SETUP_EXCEPT:
    case TINYPY_OP_SETUP_FINALLY:
    case TINYPY_OP_DELETE_FAST:
    case TINYPY_OP_POP_BLOCK:
        return __tinypy_verify_effect_set(effect, 0U, 0U, 0U);

    case TINYPY_OP_POP_TOP:
    case TINYPY_OP_PRINT_EXPR:
    case TINYPY_OP_PRINT_ITEM:
    case TINYPY_OP_PRINT_NEWLINE_TO:
    case TINYPY_OP_IMPORT_STAR:
    case TINYPY_OP_STORE_NAME:
    case TINYPY_OP_STORE_GLOBAL:
    case TINYPY_OP_STORE_FAST:
    case TINYPY_OP_STORE_DEREF:
    case TINYPY_OP_RETURN_VALUE:
        return __tinypy_verify_effect_set(effect, 1U, 1U, 0U);

    case TINYPY_OP_ROT_TWO:
        return __tinypy_verify_effect_set(effect, 2U, 0U, 0U);
    case TINYPY_OP_ROT_THREE:
        return __tinypy_verify_effect_set(effect, 3U, 0U, 0U);
    case TINYPY_OP_ROT_FOUR:
        return __tinypy_verify_effect_set(effect, 4U, 0U, 0U);
    case TINYPY_OP_DUP_TOP:
        return __tinypy_verify_effect_set(effect, 1U, 0U, 1U);

    case TINYPY_OP_UNARY_POSITIVE:
    case TINYPY_OP_UNARY_NEGATIVE:
    case TINYPY_OP_UNARY_NOT:
    case TINYPY_OP_UNARY_CONVERT:
    case TINYPY_OP_UNARY_INVERT:
    case TINYPY_OP_GET_ITER:
    case TINYPY_OP_LOAD_ATTR:
        return __tinypy_verify_effect_set(effect, 1U, 1U, 1U);

    case TINYPY_OP_BINARY_POWER:
    case TINYPY_OP_BINARY_MULTIPLY:
    case TINYPY_OP_BINARY_DIVIDE:
    case TINYPY_OP_BINARY_MODULO:
    case TINYPY_OP_BINARY_ADD:
    case TINYPY_OP_BINARY_SUBTRACT:
    case TINYPY_OP_BINARY_SUBSCR:
    case TINYPY_OP_BINARY_FLOOR_DIVIDE:
    case TINYPY_OP_BINARY_TRUE_DIVIDE:
    case TINYPY_OP_INPLACE_FLOOR_DIVIDE:
    case TINYPY_OP_INPLACE_TRUE_DIVIDE:
    case TINYPY_OP_INPLACE_ADD:
    case TINYPY_OP_INPLACE_SUBTRACT:
    case TINYPY_OP_INPLACE_MULTIPLY:
    case TINYPY_OP_INPLACE_DIVIDE:
    case TINYPY_OP_INPLACE_MODULO:
    case TINYPY_OP_BINARY_LSHIFT:
    case TINYPY_OP_BINARY_RSHIFT:
    case TINYPY_OP_BINARY_AND:
    case TINYPY_OP_BINARY_XOR:
    case TINYPY_OP_BINARY_OR:
    case TINYPY_OP_INPLACE_POWER:
    case TINYPY_OP_INPLACE_LSHIFT:
    case TINYPY_OP_INPLACE_RSHIFT:
    case TINYPY_OP_INPLACE_AND:
    case TINYPY_OP_INPLACE_XOR:
    case TINYPY_OP_INPLACE_OR:
    case TINYPY_OP_COMPARE_OP:
        return __tinypy_verify_effect_set(effect, 2U, 2U, 1U);

    case TINYPY_OP_SLICE_0:
        return __tinypy_verify_effect_set(effect, 1U, 1U, 1U);
    case TINYPY_OP_SLICE_1:
    case TINYPY_OP_SLICE_2:
        return __tinypy_verify_effect_set(effect, 2U, 2U, 1U);
    case TINYPY_OP_SLICE_3:
        return __tinypy_verify_effect_set(effect, 3U, 3U, 1U);

    case TINYPY_OP_STORE_SLICE_0:
        return __tinypy_verify_effect_set(effect, 2U, 2U, 0U);
    case TINYPY_OP_STORE_SLICE_1:
    case TINYPY_OP_STORE_SLICE_2:
        return __tinypy_verify_effect_set(effect, 3U, 3U, 0U);
    case TINYPY_OP_STORE_SLICE_3:
        return __tinypy_verify_effect_set(effect, 4U, 4U, 0U);
    case TINYPY_OP_DELETE_SLICE_0:
        return __tinypy_verify_effect_set(effect, 1U, 1U, 0U);
    case TINYPY_OP_DELETE_SLICE_1:
    case TINYPY_OP_DELETE_SLICE_2:
        return __tinypy_verify_effect_set(effect, 2U, 2U, 0U);
    case TINYPY_OP_DELETE_SLICE_3:
        return __tinypy_verify_effect_set(effect, 3U, 3U, 0U);

    case TINYPY_OP_STORE_MAP:
        return __tinypy_verify_effect_set(effect, 3U, 2U, 0U);
    case TINYPY_OP_STORE_SUBSCR:
        return __tinypy_verify_effect_set(effect, 3U, 3U, 0U);
    case TINYPY_OP_DELETE_SUBSCR:
    case TINYPY_OP_PRINT_ITEM_TO:
        return __tinypy_verify_effect_set(effect, 2U, 2U, 0U);
    case TINYPY_OP_EXEC_STMT:
        return __tinypy_verify_effect_set(effect, 3U, 3U, 0U);
    case TINYPY_OP_BUILD_CLASS:
        return __tinypy_verify_effect_set(effect, 3U, 3U, 1U);

    case TINYPY_OP_LOAD_LOCALS:
    case TINYPY_OP_LOAD_CONST:
    case TINYPY_OP_LOAD_NAME:
    case TINYPY_OP_LOAD_GLOBAL:
    case TINYPY_OP_LOAD_FAST:
    case TINYPY_OP_LOAD_CLOSURE:
    case TINYPY_OP_LOAD_DEREF:
    case TINYPY_OP_BUILD_MAP:
        return __tinypy_verify_effect_set(effect, 0U, 0U, 1U);

    case TINYPY_OP_YIELD_VALUE:
        /* POP at yield, PUSH of the sent value on generator resume. */
        return __tinypy_verify_effect_set(effect, 1U, 1U, 1U);

    case TINYPY_OP_UNPACK_SEQUENCE:
        if (!__tinypy_verify_argument_to_size(argument, &count)) {
            return 0;
        }
        return __tinypy_verify_effect_set(effect, 1U, 1U, count);

    case TINYPY_OP_FOR_ITER:
        return __tinypy_verify_effect_set(effect, 1U, 0U, 1U);

    case TINYPY_OP_LIST_APPEND:
    case TINYPY_OP_SET_ADD:
        if (!__tinypy_verify_effect_from_argument(argument, 1U, &count)) {
            return 0;
        }
        return __tinypy_verify_effect_set(effect, count, 1U, 0U);

    case TINYPY_OP_MAP_ADD:
        if (!__tinypy_verify_effect_from_argument(argument, 2U, &count)) {
            return 0;
        }
        return __tinypy_verify_effect_set(effect, count, 2U, 0U);

    case TINYPY_OP_STORE_ATTR:
        return __tinypy_verify_effect_set(effect, 2U, 2U, 0U);
    case TINYPY_OP_DELETE_ATTR:
        return __tinypy_verify_effect_set(effect, 1U, 1U, 0U);

    case TINYPY_OP_DUP_TOPX:
        if (!__tinypy_verify_argument_to_size(argument, &count)) {
            return 0;
        }
        return __tinypy_verify_effect_set(effect, count, 0U, count);

    case TINYPY_OP_BUILD_TUPLE:
    case TINYPY_OP_BUILD_LIST:
    case TINYPY_OP_BUILD_SET:
        if (!__tinypy_verify_argument_to_size(argument, &count)) {
            return 0;
        }
        return __tinypy_verify_effect_set(effect, count, count, 1U);

    case TINYPY_OP_IMPORT_NAME:
        return __tinypy_verify_effect_set(effect, 2U, 2U, 1U);
    case TINYPY_OP_IMPORT_FROM:
        return __tinypy_verify_effect_set(effect, 1U, 0U, 1U);

    case TINYPY_OP_JUMP_IF_FALSE_OR_POP:
    case TINYPY_OP_JUMP_IF_TRUE_OR_POP:
        return __tinypy_verify_effect_set(effect, 1U, 1U, 0U);
    case TINYPY_OP_POP_JUMP_IF_FALSE:
    case TINYPY_OP_POP_JUMP_IF_TRUE:
        return __tinypy_verify_effect_set(effect, 1U, 1U, 0U);

    case TINYPY_OP_RAISE_VARARGS:
        if (!__tinypy_verify_argument_to_size(argument, &count)) {
            return 0;
        }
        return __tinypy_verify_effect_set(effect, count, count, 0U);

    case TINYPY_OP_CALL_FUNCTION:
    case TINYPY_OP_CALL_FUNCTION_VAR:
    case TINYPY_OP_CALL_FUNCTION_KW:
    case TINYPY_OP_CALL_FUNCTION_VAR_KW: {
        size_t positional = (size_t)(argument & UINT64_C(0xff));
        size_t keyword = (size_t)((argument >> 8U) & UINT64_C(0xff));
        size_t extra = 0U;

        if (opcode == TINYPY_OP_CALL_FUNCTION_VAR || opcode == TINYPY_OP_CALL_FUNCTION_KW) {
            extra = 1U;
        }
        else if (opcode == TINYPY_OP_CALL_FUNCTION_VAR_KW) {
            extra = 2U;
        }
        if (!__tinypy_verify_multiply_size(keyword, 2U, &count) || !__tinypy_verify_add_size(count, positional, &count) || !__tinypy_verify_add_size(count, extra, &count) || !__tinypy_verify_add_size(count, 1U, &count)) {
            return 0;
        }
        return __tinypy_verify_effect_set(effect, count, count, 1U);
    }

    case TINYPY_OP_MAKE_FUNCTION:
        if (!__tinypy_verify_effect_from_argument(argument, 1U, &count)) {
            return 0;
        }
        return __tinypy_verify_effect_set(effect, count, count, 1U);
    case TINYPY_OP_MAKE_CLOSURE:
        if (!__tinypy_verify_effect_from_argument(argument, 2U, &count)) {
            return 0;
        }
        return __tinypy_verify_effect_set(effect, count, count, 1U);
    case TINYPY_OP_BUILD_SLICE:
        if (!__tinypy_verify_argument_to_size(argument, &count)) {
            return 0;
        }
        return __tinypy_verify_effect_set(effect, count, count, 1U);

    case TINYPY_OP_SETUP_WITH:
        return __tinypy_verify_effect_set(effect, 1U, 1U, 2U);

    case TINYPY_OP_WITH_CLEANUP:
    case TINYPY_OP_END_FINALLY:
        return __tinypy_verify_effect_set(effect, 1U, 1U, 0U);

    case TINYPY_OP_EXTENDED_ARG:
    default:
        return 0;
    }
}

//////////////////////////////////////////////////////////////////////////
static tinypy_bytecode_verify_status_e __tinypy_verify_apply_effect(tinypy_verify_context_t *context, const tinypy_decoded_instruction_t *instruction, size_t stack_depth, const tinypy_verify_effect_t *effect, size_t *out_depth) {
    size_t depth;

    if (stack_depth < effect->required || stack_depth < effect->pop_count) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_STACK_UNDERFLOW,
            instruction->offset,
            instruction->opcode,
            (uint64_t)stack_depth,
            TINYPY_OPCODE_DECODE_OK);
    }

    depth = stack_depth - effect->pop_count;
    if (!__tinypy_verify_add_size(depth, effect->push_count, &depth)) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW,
            instruction->offset,
            instruction->opcode,
            instruction->argument,
            TINYPY_OPCODE_DECODE_OK);
    }

    *out_depth = depth;
    return TINYPY_BYTECODE_VERIFY_OK;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_bytecode_verify_status_e __tinypy_verify_push_block(tinypy_verify_context_t *context, const tinypy_decoded_instruction_t *instruction, size_t parent, size_t level, uint8_t type, size_t *out_index) {
    size_t parent_depth = 0U;
    size_t depth;
    tinypy_verify_block_t *block;

    if (parent != TINYPY_VERIFY_NO_BLOCK) {
        parent_depth = context->blocks[parent].depth;
    }
    if (!__tinypy_verify_add_size(parent_depth, 1U, &depth)) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_BLOCK_STACK_OVERFLOW,
            instruction->offset,
            instruction->opcode,
            instruction->argument,
            TINYPY_OPCODE_DECODE_OK);
    }
    if (depth > context->limits.max_block_depth) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_BLOCK_STACK_OVERFLOW,
            instruction->offset,
            instruction->opcode,
            (uint64_t)depth,
            TINYPY_OPCODE_DECODE_OK);
    }
    if (context->block_count == context->block_capacity) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_INTERNAL_ERROR,
            instruction->offset,
            instruction->opcode,
            UINT64_C(0),
            TINYPY_OPCODE_DECODE_OK);
    }

    block = &context->blocks[context->block_count];
    block->parent = parent;
    (void)__tinypy_verify_jump_target(instruction, &block->handler);
    block->level = level;
    block->depth = depth;
    block->type = type;
    (void)memset(block->reserved, 0, sizeof(block->reserved));
    *out_index = context->block_count;
    context->block_count += 1U;
    return TINYPY_BYTECODE_VERIFY_OK;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_bytecode_verify_status_e __tinypy_verify_unwind_reason(tinypy_verify_context_t *context, const tinypy_decoded_instruction_t *instruction, size_t stack_depth, size_t block_index, tinypy_verify_reason_e reason, size_t continue_target) {
    while (block_index != TINYPY_VERIFY_NO_BLOCK) {
        const tinypy_verify_block_t *block = &context->blocks[block_index];
        size_t parent = block->parent;

        if (stack_depth < block->level) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_STACK_UNDERFLOW,
                instruction->offset,
                instruction->opcode,
                (uint64_t)stack_depth,
                TINYPY_OPCODE_DECODE_OK);
        }

        if (block->type == TINYPY_OP_SETUP_LOOP && reason == TINYPY_VERIFY_REASON_CONTINUE) {
            if (stack_depth != block->level) {
                return __tinypy_verify_fail(
                    context,
                    TINYPY_BYTECODE_VERIFY_STACK_DEPTH_MISMATCH,
                    instruction->offset,
                    instruction->opcode,
                    (uint64_t)stack_depth,
                    TINYPY_OPCODE_DECODE_OK);
            }

            return __tinypy_verify_enqueue(
                context,
                continue_target,
                stack_depth,
                block_index,
                TINYPY_VERIFY_NO_MARKER,
                instruction->offset,
                instruction->opcode);
        }

        stack_depth = block->level;
        block_index = parent;

        if (block->type == TINYPY_OP_SETUP_LOOP) {
            if (reason == TINYPY_VERIFY_REASON_BREAK) {
                return __tinypy_verify_enqueue(
                    context,
                    block->handler,
                    stack_depth,
                    block_index,
                    TINYPY_VERIFY_NO_MARKER,
                    instruction->offset,
                    instruction->opcode);
            }

            continue;
        }

        if (block->type == TINYPY_OP_SETUP_FINALLY || block->type == TINYPY_OP_SETUP_WITH) {
            size_t resume_depth = block->level;
            size_t handler_depth;
            size_t marker_index;
            uint8_t needs_cleanup = 0U;
            tinypy_bytecode_verify_status_e status;

            if (block->type == TINYPY_OP_SETUP_WITH) {
                if (resume_depth == 0U) {
                    return __tinypy_verify_fail(
                        context,
                        TINYPY_BYTECODE_VERIFY_INVALID_FINALLY_STATE,
                        instruction->offset,
                        instruction->opcode,
                        UINT64_C(0),
                        TINYPY_OPCODE_DECODE_OK);
                }
                resume_depth -= 1U;
                needs_cleanup = 1U;
            }

            size_t verify_reason_item_count = __tinypy_verify_reason_item_count(reason);
            if (!__tinypy_verify_add_size(
                    block->level,
                    verify_reason_item_count,
                    &handler_depth)) {
                return __tinypy_verify_fail(
                    context,
                    TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW,
                    instruction->offset,
                    instruction->opcode,
                    UINT64_C(0),
                    TINYPY_OPCODE_DECODE_OK);
            }

            status = __tinypy_verify_push_marker(
                context,
                instruction,
                TINYPY_VERIFY_NO_MARKER,
                reason,
                resume_depth,
                continue_target,
                needs_cleanup,
                &marker_index);
            if (status != TINYPY_BYTECODE_VERIFY_OK) {
                return status;
            }

            return __tinypy_verify_enqueue(
                context,
                block->handler,
                handler_depth,
                block_index,
                marker_index,
                instruction->offset,
                instruction->opcode);
        }

        /* SETUP_EXCEPT only handles exceptions, not non-local gotos. */
    }

    if (reason == TINYPY_VERIFY_REASON_BREAK) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_BREAK_OUTSIDE_LOOP,
            instruction->offset,
            instruction->opcode,
            UINT64_C(0),
            TINYPY_OPCODE_DECODE_OK);
    }
    if (reason == TINYPY_VERIFY_REASON_CONTINUE) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_CONTINUE_OUTSIDE_LOOP,
            instruction->offset,
            instruction->opcode,
            (uint64_t)continue_target,
            TINYPY_OPCODE_DECODE_OK);
    }

    return TINYPY_BYTECODE_VERIFY_OK;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_bytecode_verify_status_e __tinypy_verify_fallthrough(tinypy_verify_context_t *context, const tinypy_decoded_instruction_t *instruction, size_t stack_depth, size_t block_index, size_t marker_index) {
    if (instruction->next_offset == context->bytecode_size) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_FALLTHROUGH_PAST_END,
            instruction->offset,
            instruction->opcode,
            instruction->argument,
            TINYPY_OPCODE_DECODE_OK);
    }

    return __tinypy_verify_enqueue(
        context,
        instruction->next_offset,
        stack_depth,
        block_index,
        marker_index,
        instruction->offset,
        instruction->opcode);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_bytecode_verify_status_e __tinypy_verify_process_state(tinypy_verify_context_t *context, size_t state_index) {
    tinypy_verify_state_t state = context->states[state_index];
    size_t offset = state.offset;
    tinypy_decoded_instruction_t instruction;
    tinypy_verify_effect_t effect;
    tinypy_bytecode_verify_status_e status;
    size_t depth;
    size_t target = TINYPY_VERIFY_NO_TARGET;

    (void)tinypy_opcode_decode(
        context->bytecode,
        context->bytecode_size,
        offset,
        &instruction);

    if (!__tinypy_verify_get_effect(
            instruction.opcode,
            instruction.argument,
            &effect)) {
        return __tinypy_verify_fail(
            context,
            TINYPY_BYTECODE_VERIFY_INTERNAL_ERROR,
            offset,
            instruction.opcode,
            instruction.argument,
            TINYPY_OPCODE_DECODE_OK);
    }

    switch (instruction.opcode) {
    case TINYPY_OP_SETUP_LOOP:
    case TINYPY_OP_SETUP_EXCEPT:
    case TINYPY_OP_SETUP_FINALLY:
    case TINYPY_OP_SETUP_WITH: {
        size_t block_index;
        size_t marker_index;
        size_t level = state.stack_depth;

        status = __tinypy_verify_apply_effect(
            context,
            &instruction,
            state.stack_depth,
            &effect,
            &depth);
        if (status != TINYPY_BYTECODE_VERIFY_OK) {
            return status;
        }
        status = __tinypy_verify_push_block(
            context,
            &instruction,
            state.block_index,
            level,
            instruction.opcode,
            &block_index);
        if (status != TINYPY_BYTECODE_VERIFY_OK) {
            return status;
        }

        status = __tinypy_verify_fallthrough(
            context,
            &instruction,
            depth,
            block_index,
            state.marker_index);
        if (status != TINYPY_BYTECODE_VERIFY_OK) {
            return status;
        }

        (void)__tinypy_verify_jump_target(&instruction, &target);
        if (instruction.opcode == TINYPY_OP_SETUP_LOOP) {
            return __tinypy_verify_enqueue(
                context,
                target,
                level,
                state.block_index,
                state.marker_index,
                offset,
                instruction.opcode);
        }

        if (instruction.opcode == TINYPY_OP_SETUP_EXCEPT) {
            if (!__tinypy_verify_add_size(level, 3U, &depth)) {
                return __tinypy_verify_fail(
                    context,
                    TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW,
                    offset,
                    instruction.opcode,
                    UINT64_C(3),
                    TINYPY_OPCODE_DECODE_OK);
            }
            status = __tinypy_verify_push_marker(
                context,
                &instruction,
                state.marker_index,
                TINYPY_VERIFY_REASON_EXCEPTION_CATCHABLE,
                level,
                TINYPY_VERIFY_NO_TARGET,
                0U,
                &marker_index);
            if (status != TINYPY_BYTECODE_VERIFY_OK) {
                return status;
            }
            return __tinypy_verify_enqueue(
                context,
                target,
                depth,
                state.block_index,
                marker_index,
                offset,
                instruction.opcode);
        }

        if (instruction.opcode == TINYPY_OP_SETUP_WITH) {
            if (level == 0U || !__tinypy_verify_add_size(level, 3U, &depth)) {
                return __tinypy_verify_fail(
                    context,
                    TINYPY_BYTECODE_VERIFY_INVALID_FINALLY_STATE,
                    offset,
                    instruction.opcode,
                    (uint64_t)level,
                    TINYPY_OPCODE_DECODE_OK);
            }
            status = __tinypy_verify_push_marker(
                context,
                &instruction,
                state.marker_index,
                TINYPY_VERIFY_REASON_EXCEPTION_FINAL,
                level - 1U,
                TINYPY_VERIFY_NO_TARGET,
                1U,
                &marker_index);
            if (status != TINYPY_BYTECODE_VERIFY_OK) {
                return status;
            }
            return __tinypy_verify_enqueue(
                context,
                target,
                depth,
                state.block_index,
                marker_index,
                offset,
                instruction.opcode);
        }

        if (!__tinypy_verify_add_size(level, 3U, &depth)) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW,
                offset,
                instruction.opcode,
                UINT64_C(3),
                TINYPY_OPCODE_DECODE_OK);
        }
        status = __tinypy_verify_push_marker(
            context,
            &instruction,
            state.marker_index,
            TINYPY_VERIFY_REASON_EXCEPTION_FINAL,
            level,
            TINYPY_VERIFY_NO_TARGET,
            0U,
            &marker_index);
        if (status != TINYPY_BYTECODE_VERIFY_OK) {
            return status;
        }
        return __tinypy_verify_enqueue(
            context,
            target,
            depth,
            state.block_index,
            marker_index,
            offset,
            instruction.opcode);
    }

    case TINYPY_OP_POP_BLOCK:
        if (state.block_index == TINYPY_VERIFY_NO_BLOCK) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_BLOCK_STACK_UNDERFLOW,
                offset,
                instruction.opcode,
                UINT64_C(0),
                TINYPY_OPCODE_DECODE_OK);
        }
        if (state.stack_depth <
            context->blocks[state.block_index].level) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_STACK_UNDERFLOW,
                offset,
                instruction.opcode,
                (uint64_t)state.stack_depth,
                TINYPY_OPCODE_DECODE_OK);
        }
        return __tinypy_verify_fallthrough(
            context,
            &instruction,
            context->blocks[state.block_index].level,
            context->blocks[state.block_index].parent,
            state.marker_index);

    case TINYPY_OP_WITH_CLEANUP: {
        const tinypy_verify_marker_t *marker = NULL;
        size_t expected = 0U;
        int handles_pending_with = 0;

        if (state.marker_index != TINYPY_VERIFY_NO_MARKER) {
            marker = &context->markers[state.marker_index];
            if (!__tinypy_verify_marker_expected_depth(marker, &expected)) {
                return __tinypy_verify_fail(
                    context,
                    TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW,
                    offset,
                    instruction.opcode,
                    UINT64_C(0),
                    TINYPY_OPCODE_DECODE_OK);
            }
            if (marker->needs_with_cleanup != 0U && expected != SIZE_MAX && state.stack_depth == expected + 1U) {
                handles_pending_with = 1;
            }
        }

        if (handles_pending_with == 0) {
            size_t protected_depth = 0U;

            effect.required = 2U;
            if (marker != NULL) {
                protected_depth = expected;
                if (marker->needs_with_cleanup != 0U && !__tinypy_verify_add_size(
                        protected_depth,
                        1U,
                        &protected_depth)) {
                    return __tinypy_verify_fail(
                        context,
                        TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW,
                        offset,
                        instruction.opcode,
                        UINT64_C(0),
                        TINYPY_OPCODE_DECODE_OK);
                }
                if (state.stack_depth < protected_depth || state.stack_depth - protected_depth < 2U) {
                    return __tinypy_verify_fail(
                        context,
                        TINYPY_BYTECODE_VERIFY_INVALID_FINALLY_STATE,
                        offset,
                        instruction.opcode,
                        (uint64_t)state.stack_depth,
                        TINYPY_OPCODE_DECODE_OK);
                }
            }
            status = __tinypy_verify_apply_effect(
                context,
                &instruction,
                state.stack_depth,
                &effect,
                &depth);
            if (status != TINYPY_BYTECODE_VERIFY_OK) {
                return status;
            }
            return __tinypy_verify_fallthrough(
                context,
                &instruction,
                depth,
                state.block_index,
                state.marker_index);
        }

        status = __tinypy_verify_apply_effect(
            context,
            &instruction,
            state.stack_depth,
            &effect,
            &depth);
        if (status != TINYPY_BYTECODE_VERIFY_OK) {
            return status;
        }

        {
            size_t cleaned_marker;

            status = __tinypy_verify_push_marker(
                context,
                &instruction,
                marker->parent,
                (tinypy_verify_reason_e)marker->reason,
                marker->resume_depth,
                marker->continue_target,
                0U,
                &cleaned_marker);
            if (status != TINYPY_BYTECODE_VERIFY_OK) {
                return status;
            }

            status = __tinypy_verify_fallthrough(
                context,
                &instruction,
                depth,
                state.block_index,
                cleaned_marker);
            if (status != TINYPY_BYTECODE_VERIFY_OK) {
                return status;
            }
        }

        if (marker->reason ==
                (uint8_t)TINYPY_VERIFY_REASON_EXCEPTION_CATCHABLE || marker->reason ==
                (uint8_t)TINYPY_VERIFY_REASON_EXCEPTION_FINAL) {
            size_t suppressed_depth;

            /* A true __exit__ result replaces the triple with None. */
            if (!__tinypy_verify_add_size(
                    marker->resume_depth,
                    1U,
                    &suppressed_depth)) {
                return __tinypy_verify_fail(
                    context,
                    TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW,
                    offset,
                    instruction.opcode,
                    UINT64_C(1),
                    TINYPY_OPCODE_DECODE_OK);
            }
            return __tinypy_verify_fallthrough(
                context,
                &instruction,
                suppressed_depth,
                state.block_index,
                marker->parent);
        }

        return TINYPY_BYTECODE_VERIFY_OK;
    }

    case TINYPY_OP_END_FINALLY:
        if (state.marker_index == TINYPY_VERIFY_NO_MARKER) {
            status = __tinypy_verify_apply_effect(
                context,
                &instruction,
                state.stack_depth,
                &effect,
                &depth);
            if (status != TINYPY_BYTECODE_VERIFY_OK) {
                return status;
            }
            return __tinypy_verify_fallthrough(
                context,
                &instruction,
                depth,
                state.block_index,
                TINYPY_VERIFY_NO_MARKER);
        } {
            const tinypy_verify_marker_t *marker =
                &context->markers[state.marker_index];
            size_t expected;
            size_t protected_depth;
            tinypy_verify_reason_e marker_reason =
                (tinypy_verify_reason_e)marker->reason;

            if (!__tinypy_verify_marker_expected_depth(marker, &expected)) {
                return __tinypy_verify_fail(
                    context,
                    TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW,
                    offset,
                    instruction.opcode,
                    UINT64_C(0),
                    TINYPY_OPCODE_DECODE_OK);
            }
            protected_depth = expected;
            if (marker->needs_with_cleanup != 0U && !__tinypy_verify_add_size(
                    protected_depth,
                    1U,
                    &protected_depth)) {
                return __tinypy_verify_fail(
                    context,
                    TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW,
                    offset,
                    instruction.opcode,
                    UINT64_C(0),
                    TINYPY_OPCODE_DECODE_OK);
            }

            if (state.stack_depth > protected_depth) {
                status = __tinypy_verify_apply_effect(
                    context,
                    &instruction,
                    state.stack_depth,
                    &effect,
                    &depth);
                if (status != TINYPY_BYTECODE_VERIFY_OK) {
                    return status;
                }
                return __tinypy_verify_fallthrough(
                    context,
                    &instruction,
                    depth,
                    state.block_index,
                    state.marker_index);
            }
            if (marker->needs_with_cleanup != 0U || state.stack_depth != expected) {
                return __tinypy_verify_fail(
                    context,
                    TINYPY_BYTECODE_VERIFY_INVALID_FINALLY_STATE,
                    offset,
                    instruction.opcode,
                    (uint64_t)state.stack_depth,
                    TINYPY_OPCODE_DECODE_OK);
            }
            if (marker_reason ==
                    TINYPY_VERIFY_REASON_EXCEPTION_CATCHABLE || marker_reason == TINYPY_VERIFY_REASON_EXCEPTION_FINAL) {
                return TINYPY_BYTECODE_VERIFY_OK;
            }
            return __tinypy_verify_unwind_reason(
                context,
                &instruction,
                marker->resume_depth,
                state.block_index,
                marker_reason,
                marker->continue_target);
        }

    case TINYPY_OP_RETURN_VALUE:
        status = __tinypy_verify_apply_effect(
            context,
            &instruction,
            state.stack_depth,
            &effect,
            &depth);
        if (status != TINYPY_BYTECODE_VERIFY_OK) {
            return status;
        }
        return __tinypy_verify_unwind_reason(
            context,
            &instruction,
            depth,
            state.block_index,
            TINYPY_VERIFY_REASON_RETURN,
            TINYPY_VERIFY_NO_TARGET);

    case TINYPY_OP_BREAK_LOOP:
        return __tinypy_verify_unwind_reason(
            context,
            &instruction,
            state.stack_depth,
            state.block_index,
            TINYPY_VERIFY_REASON_BREAK,
            TINYPY_VERIFY_NO_TARGET);

    case TINYPY_OP_CONTINUE_LOOP:
        (void)__tinypy_verify_jump_target(&instruction, &target);
        return __tinypy_verify_unwind_reason(
            context,
            &instruction,
            state.stack_depth,
            state.block_index,
            TINYPY_VERIFY_REASON_CONTINUE,
            target);

    case TINYPY_OP_STOP_CODE:
        return TINYPY_BYTECODE_VERIFY_OK;

    case TINYPY_OP_RAISE_VARARGS:
        status = __tinypy_verify_apply_effect(
            context,
            &instruction,
            state.stack_depth,
            &effect,
            &depth);
        return status;

    case TINYPY_OP_JUMP_FORWARD:
    case TINYPY_OP_JUMP_ABSOLUTE:
        (void)__tinypy_verify_jump_target(&instruction, &target);
        return __tinypy_verify_enqueue(
            context,
            target,
            state.stack_depth,
            state.block_index,
            state.marker_index,
            offset,
            instruction.opcode);

    case TINYPY_OP_FOR_ITER:
        (void)__tinypy_verify_jump_target(&instruction, &target);
        if (state.stack_depth < 1U) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_STACK_UNDERFLOW,
                offset,
                instruction.opcode,
                (uint64_t)state.stack_depth,
                TINYPY_OPCODE_DECODE_OK);
        }
        if (!__tinypy_verify_add_size(state.stack_depth, 1U, &depth)) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW,
                offset,
                instruction.opcode,
                instruction.argument,
                TINYPY_OPCODE_DECODE_OK);
        }
        status = __tinypy_verify_fallthrough(
            context,
            &instruction,
            depth,
            state.block_index,
            state.marker_index);
        if (status != TINYPY_BYTECODE_VERIFY_OK) {
            return status;
        }
        return __tinypy_verify_enqueue(
            context,
            target,
            state.stack_depth - 1U,
            state.block_index,
            state.marker_index,
            offset,
            instruction.opcode);

    case TINYPY_OP_JUMP_IF_FALSE_OR_POP:
    case TINYPY_OP_JUMP_IF_TRUE_OR_POP:
        if (state.stack_depth < 1U) {
            return __tinypy_verify_fail(
                context,
                TINYPY_BYTECODE_VERIFY_STACK_UNDERFLOW,
                offset,
                instruction.opcode,
                (uint64_t)state.stack_depth,
                TINYPY_OPCODE_DECODE_OK);
        }
        (void)__tinypy_verify_jump_target(&instruction, &target);
        status = __tinypy_verify_enqueue(
            context,
            target,
            state.stack_depth,
            state.block_index,
            state.marker_index,
            offset,
            instruction.opcode);
        if (status != TINYPY_BYTECODE_VERIFY_OK) {
            return status;
        }
        return __tinypy_verify_fallthrough(
            context,
            &instruction,
            state.stack_depth - 1U,
            state.block_index,
            state.marker_index);

    case TINYPY_OP_POP_JUMP_IF_FALSE:
    case TINYPY_OP_POP_JUMP_IF_TRUE:
        status = __tinypy_verify_apply_effect(
            context,
            &instruction,
            state.stack_depth,
            &effect,
            &depth);
        if (status != TINYPY_BYTECODE_VERIFY_OK) {
            return status;
        }
        (void)__tinypy_verify_jump_target(&instruction, &target);
        status = __tinypy_verify_enqueue(
            context,
            target,
            depth,
            state.block_index,
            state.marker_index,
            offset,
            instruction.opcode);
        if (status != TINYPY_BYTECODE_VERIFY_OK) {
            return status;
        }
        return __tinypy_verify_fallthrough(
            context,
            &instruction,
            depth,
            state.block_index,
            state.marker_index);

    default:
        status = __tinypy_verify_apply_effect(
            context,
            &instruction,
            state.stack_depth,
            &effect,
            &depth);
        if (status != TINYPY_BYTECODE_VERIFY_OK) {
            return status;
        }
        return __tinypy_verify_fallthrough(
            context,
            &instruction,
            depth,
            state.block_index,
            state.marker_index);
    }
}

//////////////////////////////////////////////////////////////////////////
static tinypy_bytecode_verify_status_e __tinypy_verify_control_flow(tinypy_verify_context_t *context) {
    tinypy_bytecode_verify_status_e status;

    status = __tinypy_verify_enqueue(
        context,
        0U,
        0U,
        TINYPY_VERIFY_NO_BLOCK,
        TINYPY_VERIFY_NO_MARKER,
        0U,
        context->bytecode[0]);
    if (status != TINYPY_BYTECODE_VERIFY_OK) {
        return status;
    }

    while (context->worklist_head != context->worklist_tail) {
        size_t state_index = context->worklist[context->worklist_head];

        context->worklist_head += 1U;
        status = __tinypy_verify_process_state(context, state_index);
        if (status != TINYPY_BYTECODE_VERIFY_OK) {
            return status;
        }
    }

    return TINYPY_BYTECODE_VERIFY_OK;
}

//////////////////////////////////////////////////////////////////////////
tinypy_bytecode_verify_status_e tinypy_bytecode_verify(const uint8_t *bytecode, size_t bytecode_size, const tinypy_bytecode_metadata_t *metadata, const tinypy_bytecode_verify_limits_t *limits, void *scratch, size_t scratch_size, tinypy_bytecode_verify_result_t *out_result) {
    tinypy_verify_context_t context;
    tinypy_bytecode_verify_status_e status;
    size_t required_scratch_size = 0U;
    size_t index;

    assert(out_result != NULL);
    assert(metadata != NULL);
    assert(bytecode != NULL || bytecode_size == 0U);
    __tinypy_verify_clear_result(out_result);
    status = tinypy_bytecode_verify_scratch_size(
        bytecode_size,
        &required_scratch_size);
    out_result->required_scratch_size = required_scratch_size;

    if (status != TINYPY_BYTECODE_VERIFY_OK) {
        out_result->status = status;
        return status;
    }
    if (bytecode_size == 0U) {
        out_result->status = TINYPY_BYTECODE_VERIFY_EMPTY;
        return out_result->status;
    }

    (void)memset(&context, 0, sizeof(context));
    context.bytecode = bytecode;
    context.bytecode_size = bytecode_size;
    context.metadata = metadata;
    context.result = out_result;
    __tinypy_verify_apply_default_limits(limits, &context.limits);

    if (context.limits.max_bytecode_size != 0U && bytecode_size > context.limits.max_bytecode_size) {
        return __tinypy_verify_fail(
            &context,
            TINYPY_BYTECODE_VERIFY_LIMIT_EXCEEDED,
            0U,
            bytecode[0],
            (uint64_t)bytecode_size,
            TINYPY_OPCODE_DECODE_OK);
    }
    if (scratch == NULL || scratch_size < required_scratch_size) {
        return __tinypy_verify_fail(
            &context,
            TINYPY_BYTECODE_VERIFY_SCRATCH_TOO_SMALL,
            0U,
            bytecode[0],
            (uint64_t)required_scratch_size,
            TINYPY_OPCODE_DECODE_OK);
    }
    if (((uintptr_t)scratch % (uintptr_t)TINYPY_VERIFY_ALIGNMENT) != 0U) {
        return __tinypy_verify_fail(
            &context,
            TINYPY_BYTECODE_VERIFY_SCRATCH_MISALIGNED,
            0U,
            bytecode[0],
            (uint64_t)TINYPY_VERIFY_ALIGNMENT,
            TINYPY_OPCODE_DECODE_OK);
    }

    __tinypy_verify_bind_scratch(&context, scratch);
    (void)memset(context.boundaries, 0, bytecode_size);

    for (index = 0U; index != bytecode_size; ++index) {
        context.state_heads[index] = TINYPY_VERIFY_NO_STATE;
    }

    status = __tinypy_verify_first_pass(&context);
    if (status != TINYPY_BYTECODE_VERIFY_OK) {
        return status;
    }
    status = __tinypy_verify_jumps(&context);
    if (status != TINYPY_BYTECODE_VERIFY_OK) {
        return status;
    }
    status = __tinypy_verify_control_flow(&context);
    if (status != TINYPY_BYTECODE_VERIFY_OK) {
        return status;
    }

    out_result->status = TINYPY_BYTECODE_VERIFY_OK;
    out_result->error_offset = SIZE_MAX;
    out_result->error_opcode = 0U;
    out_result->error_argument = UINT64_C(0);
    return TINYPY_BYTECODE_VERIFY_OK;
}

//////////////////////////////////////////////////////////////////////////
const char *tinypy_bytecode_verify_status_name(tinypy_bytecode_verify_status_e status) {
    switch (status) {
    case TINYPY_BYTECODE_VERIFY_OK:
        return "ok";
    case TINYPY_BYTECODE_VERIFY_SIZE_OVERFLOW:
        return "size overflow";
    case TINYPY_BYTECODE_VERIFY_LIMIT_EXCEEDED:
        return "verification limit exceeded";
    case TINYPY_BYTECODE_VERIFY_SCRATCH_TOO_SMALL:
        return "scratch buffer too small";
    case TINYPY_BYTECODE_VERIFY_SCRATCH_MISALIGNED:
        return "scratch buffer is misaligned";
    case TINYPY_BYTECODE_VERIFY_EMPTY:
        return "empty bytecode";
    case TINYPY_BYTECODE_VERIFY_DECODE_ERROR:
        return "bytecode decode error";
    case TINYPY_BYTECODE_VERIFY_UNKNOWN_OPCODE:
        return "unknown opcode";
    case TINYPY_BYTECODE_VERIFY_OPERAND_OUT_OF_RANGE:
        return "operand index out of range";
    case TINYPY_BYTECODE_VERIFY_INVALID_OPERAND:
        return "invalid opcode operand";
    case TINYPY_BYTECODE_VERIFY_JUMP_OUT_OF_RANGE:
        return "jump target out of range";
    case TINYPY_BYTECODE_VERIFY_JUMP_NOT_INSTRUCTION:
        return "jump target is not a logical instruction boundary";
    case TINYPY_BYTECODE_VERIFY_FALLTHROUGH_PAST_END:
        return "instruction falls through past bytecode end";
    case TINYPY_BYTECODE_VERIFY_STACK_UNDERFLOW:
        return "value stack underflow";
    case TINYPY_BYTECODE_VERIFY_STACK_DEPTH_MISMATCH:
        return "inconsistent value stack depth at merge";
    case TINYPY_BYTECODE_VERIFY_STACK_OVERFLOW:
        return "value stack overflow";
    case TINYPY_BYTECODE_VERIFY_DECLARED_STACK_TOO_SMALL:
        return "computed stack exceeds declared stack size";
    case TINYPY_BYTECODE_VERIFY_BLOCK_STACK_UNDERFLOW:
        return "block stack underflow";
    case TINYPY_BYTECODE_VERIFY_BLOCK_STACK_OVERFLOW:
        return "block stack limit exceeded";
    case TINYPY_BYTECODE_VERIFY_BLOCK_STACK_MISMATCH:
        return "inconsistent block stack at merge";
    case TINYPY_BYTECODE_VERIFY_BREAK_OUTSIDE_LOOP:
        return "BREAK_LOOP outside loop";
    case TINYPY_BYTECODE_VERIFY_CONTINUE_OUTSIDE_LOOP:
        return "CONTINUE_LOOP outside loop";
    case TINYPY_BYTECODE_VERIFY_INVALID_FINALLY_STATE:
        return "invalid finally state";
    case TINYPY_BYTECODE_VERIFY_INTERNAL_ERROR:
        return "internal verifier error";
    default:
        return "unknown verification status";
    }
}
