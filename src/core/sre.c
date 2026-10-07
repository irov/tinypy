#include "internal.h"

#include <string.h>

#define TINYPY_SRE_MAGIC UINT32_C(20031017)
#define TINYPY_SRE_MAXREPEAT UINT32_MAX
#define TINYPY_SRE_MAX_MARKS 200U
#define TINYPY_SRE_FLAG_LOCALE INT64_C(4)
#define TINYPY_SRE_FLAG_UNICODE INT64_C(32)

typedef enum tinypy_sre_opcode_e {
    TINYPY_SRE_OP_FAILURE = 0,
    TINYPY_SRE_OP_SUCCESS = 1,
    TINYPY_SRE_OP_ANY = 2,
    TINYPY_SRE_OP_ANY_ALL = 3,
    TINYPY_SRE_OP_ASSERT = 4,
    TINYPY_SRE_OP_ASSERT_NOT = 5,
    TINYPY_SRE_OP_AT = 6,
    TINYPY_SRE_OP_BRANCH = 7,
    TINYPY_SRE_OP_CALL = 8,
    TINYPY_SRE_OP_CATEGORY = 9,
    TINYPY_SRE_OP_CHARSET = 10,
    TINYPY_SRE_OP_BIGCHARSET = 11,
    TINYPY_SRE_OP_GROUPREF = 12,
    TINYPY_SRE_OP_GROUPREF_EXISTS = 13,
    TINYPY_SRE_OP_GROUPREF_IGNORE = 14,
    TINYPY_SRE_OP_IN = 15,
    TINYPY_SRE_OP_IN_IGNORE = 16,
    TINYPY_SRE_OP_INFO = 17,
    TINYPY_SRE_OP_JUMP = 18,
    TINYPY_SRE_OP_LITERAL = 19,
    TINYPY_SRE_OP_LITERAL_IGNORE = 20,
    TINYPY_SRE_OP_MARK = 21,
    TINYPY_SRE_OP_MAX_UNTIL = 22,
    TINYPY_SRE_OP_MIN_UNTIL = 23,
    TINYPY_SRE_OP_NOT_LITERAL = 24,
    TINYPY_SRE_OP_NOT_LITERAL_IGNORE = 25,
    TINYPY_SRE_OP_NEGATE = 26,
    TINYPY_SRE_OP_RANGE = 27,
    TINYPY_SRE_OP_REPEAT = 28,
    TINYPY_SRE_OP_REPEAT_ONE = 29,
    TINYPY_SRE_OP_SUBPATTERN = 30,
    TINYPY_SRE_OP_MIN_REPEAT_ONE = 31
} tinypy_sre_opcode_e;

typedef enum tinypy_sre_at_e {
    TINYPY_SRE_AT_BEGINNING = 0,
    TINYPY_SRE_AT_BEGINNING_LINE = 1,
    TINYPY_SRE_AT_BEGINNING_STRING = 2,
    TINYPY_SRE_AT_BOUNDARY = 3,
    TINYPY_SRE_AT_NON_BOUNDARY = 4,
    TINYPY_SRE_AT_END = 5,
    TINYPY_SRE_AT_END_LINE = 6,
    TINYPY_SRE_AT_END_STRING = 7,
    TINYPY_SRE_AT_LOC_BOUNDARY = 8,
    TINYPY_SRE_AT_LOC_NON_BOUNDARY = 9,
    TINYPY_SRE_AT_UNI_BOUNDARY = 10,
    TINYPY_SRE_AT_UNI_NON_BOUNDARY = 11
} tinypy_sre_at_e;

typedef enum tinypy_sre_category_e {
    TINYPY_SRE_CATEGORY_DIGIT = 0,
    TINYPY_SRE_CATEGORY_NOT_DIGIT = 1,
    TINYPY_SRE_CATEGORY_SPACE = 2,
    TINYPY_SRE_CATEGORY_NOT_SPACE = 3,
    TINYPY_SRE_CATEGORY_WORD = 4,
    TINYPY_SRE_CATEGORY_NOT_WORD = 5,
    TINYPY_SRE_CATEGORY_LINEBREAK = 6,
    TINYPY_SRE_CATEGORY_NOT_LINEBREAK = 7,
    TINYPY_SRE_CATEGORY_LOC_WORD = 8,
    TINYPY_SRE_CATEGORY_LOC_NOT_WORD = 9,
    TINYPY_SRE_CATEGORY_UNI_DIGIT = 10,
    TINYPY_SRE_CATEGORY_UNI_NOT_DIGIT = 11,
    TINYPY_SRE_CATEGORY_UNI_SPACE = 12,
    TINYPY_SRE_CATEGORY_UNI_NOT_SPACE = 13,
    TINYPY_SRE_CATEGORY_UNI_WORD = 14,
    TINYPY_SRE_CATEGORY_UNI_NOT_WORD = 15,
    TINYPY_SRE_CATEGORY_UNI_LINEBREAK = 16,
    TINYPY_SRE_CATEGORY_UNI_NOT_LINEBREAK = 17
} tinypy_sre_category_e;

/* The matcher follows sre_lib.h from Python 2.7: every backtracking point
   is a context on a heap-allocated stack, so pattern depth and repeat counts
   are bounded by memory rather than by the native stack. */
typedef enum tinypy_sre_jump_e {
    TINYPY_SRE_JUMP_NONE = 0,
    TINYPY_SRE_JUMP_MAX_UNTIL_1 = 1,
    TINYPY_SRE_JUMP_MAX_UNTIL_2 = 2,
    TINYPY_SRE_JUMP_MAX_UNTIL_3 = 3,
    TINYPY_SRE_JUMP_MIN_UNTIL_1 = 4,
    TINYPY_SRE_JUMP_MIN_UNTIL_2 = 5,
    TINYPY_SRE_JUMP_MIN_UNTIL_3 = 6,
    TINYPY_SRE_JUMP_REPEAT = 7,
    TINYPY_SRE_JUMP_REPEAT_ONE = 8,
    TINYPY_SRE_JUMP_MIN_REPEAT_ONE = 9,
    TINYPY_SRE_JUMP_BRANCH = 10,
    TINYPY_SRE_JUMP_ASSERT = 11,
    TINYPY_SRE_JUMP_ASSERT_NOT = 12
} tinypy_sre_jump_e;

typedef struct tinypy_sre_repeat_t {
    size_t previous;
    size_t pc;
    size_t count;
    size_t last_position;
} tinypy_sre_repeat_t;

typedef struct tinypy_sre_context_t {
    size_t pc;
    size_t position;
    size_t count;
    size_t saved_last_position;
    size_t mark_offset;
    size_t repeat_index;
    ptrdiff_t lastmark;
    ptrdiff_t lastindex;
    tinypy_sre_repeat_t repeat;
    uint32_t jump;
    uint32_t literal;
    tinypy_bool_t literal_tail;
} tinypy_sre_context_t;

typedef struct tinypy_sre_state_t {
    tinypy_vm_t *vm;
    tinypy_sre_pattern_object_t *pattern;
    tinypy_value_t *string;
    const uint8_t *bytes;
    uint32_t *characters;
    size_t size;
    size_t beginning;
    size_t end;
    size_t position;
    size_t *marks;
    ptrdiff_t lastmark;
    ptrdiff_t lastindex;
    size_t repeat;
    tinypy_sre_context_t *contexts;
    size_t context_count;
    size_t context_capacity;
    size_t *mark_stack;
    size_t mark_top;
    size_t mark_capacity;
    tinypy_bool_t invalid_code;
    tinypy_bool_t memory_failed;
} tinypy_sre_state_t;

static tinypy_bool_t __tinypy_sre_match(tinypy_sre_state_t *state, size_t start_pc, size_t *inout_position, size_t *marks, ptrdiff_t *inout_lastindex);
static void __tinypy_sre_pattern_cache_release(tinypy_sre_pattern_object_t *pattern);

//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_sre_ascii_lower(uint32_t character) {
    if (character >= (uint32_t)'A' && character <= (uint32_t)'Z') {
        return character + (uint32_t)('a' - 'A');
    }
    return character;
}
//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_sre_lower(uint32_t character, int64_t flags) {
    if ((flags & TINYPY_SRE_FLAG_UNICODE) != 0 && (flags & TINYPY_SRE_FLAG_LOCALE) == 0) {
        uint32_t return_value_1 = tinypy_internal_unicode_lower(character);
        return return_value_1;
    }
    uint32_t return_value_2 = __tinypy_sre_ascii_lower(character);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_is_digit(uint32_t character) {
    return character >= (uint32_t)'0' && character <= (uint32_t)'9' ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_is_space(uint32_t character) {
    return character == (uint32_t)' ' || character == (uint32_t)'\t' || character == (uint32_t)'\n' || character == (uint32_t)'\r' || character == (uint32_t)'\v' || character == (uint32_t)'\f' ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_is_word(uint32_t character) {
    tinypy_bool_t return_value_1 = __tinypy_sre_is_digit(character) != 0 || (character >= (uint32_t)'a' && character <= (uint32_t)'z') || (character >= (uint32_t)'A' && character <= (uint32_t)'Z') || character == (uint32_t)'_' ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_is_unicode_word(uint32_t character) {
    tinypy_bool_t return_value_1 = tinypy_internal_unicode_is_alnum(character) != 0 || character == (uint32_t)'_' ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_is_linebreak(uint32_t character) {
    return character == (uint32_t)'\n' ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_category(uint32_t category, uint32_t character) {
    tinypy_bool_t function_result;
    switch ((tinypy_sre_category_e)category) {
    case TINYPY_SRE_CATEGORY_DIGIT:
        function_result = __tinypy_sre_is_digit(character);
        return function_result;
    case TINYPY_SRE_CATEGORY_NOT_DIGIT:
        function_result = __tinypy_sre_is_digit(character) == 0;
        return function_result;
    case TINYPY_SRE_CATEGORY_SPACE:
        function_result = __tinypy_sre_is_space(character);
        return function_result;
    case TINYPY_SRE_CATEGORY_NOT_SPACE:
        function_result = __tinypy_sre_is_space(character) == 0;
        return function_result;
    case TINYPY_SRE_CATEGORY_WORD:
    case TINYPY_SRE_CATEGORY_LOC_WORD:
        function_result = __tinypy_sre_is_word(character);
        return function_result;
    case TINYPY_SRE_CATEGORY_NOT_WORD:
    case TINYPY_SRE_CATEGORY_LOC_NOT_WORD:
        function_result = __tinypy_sre_is_word(character) == 0;
        return function_result;
    case TINYPY_SRE_CATEGORY_LINEBREAK:
        function_result = __tinypy_sre_is_linebreak(character);
        return function_result;
    case TINYPY_SRE_CATEGORY_NOT_LINEBREAK:
        function_result = __tinypy_sre_is_linebreak(character) == 0;
        return function_result;
    case TINYPY_SRE_CATEGORY_UNI_DIGIT:
        function_result = tinypy_internal_unicode_is_decimal(character);
        return function_result;
    case TINYPY_SRE_CATEGORY_UNI_NOT_DIGIT:
        function_result = tinypy_internal_unicode_is_decimal(character) == 0;
        return function_result;
    case TINYPY_SRE_CATEGORY_UNI_SPACE:
        function_result = tinypy_internal_unicode_is_space(character);
        return function_result;
    case TINYPY_SRE_CATEGORY_UNI_NOT_SPACE:
        function_result = tinypy_internal_unicode_is_space(character) == 0;
        return function_result;
    case TINYPY_SRE_CATEGORY_UNI_WORD:
        function_result = __tinypy_sre_is_unicode_word(character);
        return function_result;
    case TINYPY_SRE_CATEGORY_UNI_NOT_WORD:
        function_result = __tinypy_sre_is_unicode_word(character) == 0;
        return function_result;
    case TINYPY_SRE_CATEGORY_UNI_LINEBREAK:
        function_result = tinypy_internal_unicode_is_linebreak(character);
        return function_result;
    case TINYPY_SRE_CATEGORY_UNI_NOT_LINEBREAK:
        function_result = tinypy_internal_unicode_is_linebreak(character) == 0;
        return function_result;
    default:
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_sre_character_at(const tinypy_sre_state_t *state, size_t position) {
    if (state->characters != NULL) {
        return state->characters[position];
    }
    return state->bytes[position];
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_at(const tinypy_sre_state_t *state, size_t position, uint32_t at) {
    tinypy_bool_t function_result;
    tinypy_bool_t previous_word;
    tinypy_bool_t current_word;

    switch ((tinypy_sre_at_e)at) {
    case TINYPY_SRE_AT_BEGINNING:
    case TINYPY_SRE_AT_BEGINNING_STRING:
        return position == state->beginning;
    case TINYPY_SRE_AT_BEGINNING_LINE:
        function_result = position == state->beginning || __tinypy_sre_is_linebreak(__tinypy_sre_character_at(state, position - 1U)) != 0;
        return function_result;
    case TINYPY_SRE_AT_END:
        function_result = position == state->end || (position + 1U == state->end && __tinypy_sre_is_linebreak(__tinypy_sre_character_at(state, position)) != 0);
        return function_result;
    case TINYPY_SRE_AT_END_LINE:
        function_result = position == state->end || __tinypy_sre_is_linebreak(__tinypy_sre_character_at(state, position)) != 0;
        return function_result;
    case TINYPY_SRE_AT_END_STRING:
        return position == state->end;
    case TINYPY_SRE_AT_BOUNDARY:
    case TINYPY_SRE_AT_LOC_BOUNDARY:
    case TINYPY_SRE_AT_UNI_BOUNDARY:
    case TINYPY_SRE_AT_NON_BOUNDARY:
    case TINYPY_SRE_AT_LOC_NON_BOUNDARY:
    case TINYPY_SRE_AT_UNI_NON_BOUNDARY:
        /* An empty subject has no word boundaries and no non-boundaries. */
        if (state->beginning == state->end) {
            return TINYPY_FALSE;
        }
        if (at == TINYPY_SRE_AT_UNI_BOUNDARY || at == TINYPY_SRE_AT_UNI_NON_BOUNDARY) {
            previous_word = position > state->beginning ? __tinypy_sre_is_unicode_word(__tinypy_sre_character_at(state, position - 1U)) : TINYPY_FALSE;
            current_word = position < state->end ? __tinypy_sre_is_unicode_word(__tinypy_sre_character_at(state, position)) : TINYPY_FALSE;
        } else {
            previous_word = position > state->beginning ? __tinypy_sre_is_word(__tinypy_sre_character_at(state, position - 1U)) : TINYPY_FALSE;
            current_word = position < state->end ? __tinypy_sre_is_word(__tinypy_sre_character_at(state, position)) : TINYPY_FALSE;
        }
        if (at == TINYPY_SRE_AT_BOUNDARY || at == TINYPY_SRE_AT_LOC_BOUNDARY || at == TINYPY_SRE_AT_UNI_BOUNDARY) {
            return previous_word != current_word;
        }
        return previous_word == current_word;
    default:
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_charset(const tinypy_sre_pattern_object_t *pattern, size_t pc, uint32_t character) {
    tinypy_bool_t accepted = TINYPY_TRUE;

    while (pc < pattern->code_size) {
        uint32_t opcode = pattern->code[pc++];

        switch ((tinypy_sre_opcode_e)opcode) {
        case TINYPY_SRE_OP_FAILURE:
            return accepted == 0;
        case TINYPY_SRE_OP_LITERAL:
            if (pc >= pattern->code_size) {
                return TINYPY_FALSE;
            }
            if (character == pattern->code[pc]) {
                return accepted;
            }
            pc += 1U;
            break;
        case TINYPY_SRE_OP_CATEGORY:
            if (pc >= pattern->code_size) {
                return TINYPY_FALSE;
            }
            if (__tinypy_sre_category(pattern->code[pc], character) != 0) {
                return accepted;
            }
            pc += 1U;
            break;
        case TINYPY_SRE_OP_CHARSET:
            if (pc + 8U > pattern->code_size) {
                return TINYPY_FALSE;
            }
            if (character < UINT32_C(256) && (pattern->code[pc + (character >> 5U)] & (UINT32_C(1) << (character & UINT32_C(31)))) != 0U) {
                return accepted;
            }
            pc += 8U;
            break;
        case TINYPY_SRE_OP_RANGE:
            if (pc + 2U > pattern->code_size) {
                return TINYPY_FALSE;
            }
            if (pattern->code[pc] <= character && character <= pattern->code[pc + 1U]) {
                return accepted;
            }
            pc += 2U;
            break;
        case TINYPY_SRE_OP_NEGATE:
            accepted = accepted == 0 ? INT32_C(1) : INT32_C(0);
            break;
        case TINYPY_SRE_OP_BIGCHARSET: {
            size_t block_count;
            const uint8_t *block_indices;
            size_t block;

            if (pc >= pattern->code_size) {
                return TINYPY_FALSE;
            }
            block_count = pattern->code[pc++];
            if (pc + 64U + block_count * 8U > pattern->code_size) {
                return TINYPY_FALSE;
            }
            block_indices = (const uint8_t *)(pattern->code + pc);
            block = character <= UINT32_C(65535) ? block_indices[character >> 8U] : SIZE_MAX;
            pc += 64U;
            if (block != SIZE_MAX && block < block_count && (pattern->code[pc + block * 8U + ((character & UINT32_C(255)) >> 5U)] & (UINT32_C(1) << (character & UINT32_C(31)))) != 0U) {
                return accepted;
            }
            pc += block_count * 8U;
        }
        break;
        default:
            return TINYPY_FALSE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_sre_copy_marks(size_t *target, const size_t *source, size_t count) {
    if (count != 0U) {
        (void)memcpy(target, source, count * sizeof(*target));
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_match_one(tinypy_sre_state_t *state, size_t pc, size_t position, size_t *out_position) {
    uint32_t opcode;
    uint32_t character;

    if (position >= state->end || pc >= state->pattern->code_size) {
        return TINYPY_FALSE;
    }
    opcode = state->pattern->code[pc];
    character = __tinypy_sre_character_at(state, position);
    switch ((tinypy_sre_opcode_e)opcode) {
    case TINYPY_SRE_OP_LITERAL:
        if (pc + 1U >= state->pattern->code_size || character != state->pattern->code[pc + 1U]) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_SRE_OP_LITERAL_IGNORE:
        if (pc + 1U >= state->pattern->code_size || __tinypy_sre_lower(character, state->pattern->flags) != __tinypy_sre_lower(state->pattern->code[pc + 1U], state->pattern->flags)) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_SRE_OP_NOT_LITERAL:
        if (pc + 1U >= state->pattern->code_size || character == state->pattern->code[pc + 1U]) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_SRE_OP_NOT_LITERAL_IGNORE:
        if (pc + 1U >= state->pattern->code_size || __tinypy_sre_lower(character, state->pattern->flags) == __tinypy_sre_lower(state->pattern->code[pc + 1U], state->pattern->flags)) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_SRE_OP_ANY:
        if (__tinypy_sre_is_linebreak(character) != 0) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_SRE_OP_ANY_ALL:
        break;
    case TINYPY_SRE_OP_IN:
        if (pc + 1U >= state->pattern->code_size || __tinypy_sre_charset(state->pattern, pc + 2U, character) == 0) {
            return TINYPY_FALSE;
        }
        break;
    case TINYPY_SRE_OP_IN_IGNORE: {
        uint32_t lowered_character;

        if (pc + 1U >= state->pattern->code_size) {
            return TINYPY_FALSE;
        }
        lowered_character = __tinypy_sre_lower(character, state->pattern->flags);
        if (__tinypy_sre_charset(state->pattern, pc + 2U, lowered_character) == 0) {
            return TINYPY_FALSE;
        }
        break;
    }
    case TINYPY_SRE_OP_CATEGORY:
        if (pc + 1U >= state->pattern->code_size || __tinypy_sre_category(state->pattern->code[pc + 1U], character) == 0) {
            return TINYPY_FALSE;
        }
        break;
    default:
        return TINYPY_FALSE;
    }
    *out_position = position + 1U;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* The pool reallocator needs a live block, so an empty buffer is allocated. */
static void *__tinypy_sre_grow(tinypy_sre_state_t *state, void *memory, size_t old_size, size_t new_size) {
    void *grown;

    if (memory == NULL) {
        grown = tinypy_internal_vm_allocate_checked(state->vm, new_size, NULL);
    }
    else {
        grown = tinypy_internal_vm_reallocate_checked(state->vm, memory, old_size, new_size, NULL);
    }
    if (grown == NULL) {
        state->memory_failed = TINYPY_TRUE;
    }
    return grown;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_sre_marks_save(tinypy_sre_state_t *state) {
    size_t count = state->pattern->groups * 2U;
    size_t offset = state->mark_top;

    if (count == 0U) {
        return offset;
    }
    if (count > state->mark_capacity - offset) {
        size_t new_capacity = state->mark_capacity == 0U ? count * 4U : state->mark_capacity * 2U;
        size_t *grown;

        while (new_capacity - offset < count) {
            new_capacity *= 2U;
        }
        grown = (size_t *)__tinypy_sre_grow(state, state->mark_stack, state->mark_capacity * sizeof(*grown), new_capacity * sizeof(*grown));
        if (grown == NULL) {
            return SIZE_MAX;
        }
        state->mark_stack = grown;
        state->mark_capacity = new_capacity;
    }
    (void)memcpy(state->mark_stack + offset, state->marks, count * sizeof(*state->marks));
    state->mark_top = offset + count;
    return offset;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_sre_marks_restore(tinypy_sre_state_t *state, size_t offset, tinypy_bool_t keep) {
    size_t count = state->pattern->groups * 2U;

    if (count != 0U) {
        (void)memcpy(state->marks, state->mark_stack + offset, count * sizeof(*state->marks));
    }
    if (keep == 0) {
        state->mark_top = offset;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_sre_marks_drop(tinypy_sre_state_t *state, size_t offset) {
    state->mark_top = offset;
}
//////////////////////////////////////////////////////////////////////////
/* Marks above lastmark count as unset, so only the two fields are restored. */
static void __tinypy_sre_lastmark_restore(tinypy_sre_state_t *state, const tinypy_sre_context_t *context) {
    state->lastmark = context->lastmark;
    state->lastindex = context->lastindex;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_sre_context_t *__tinypy_sre_context_push(tinypy_sre_state_t *state, size_t pc) {
    tinypy_sre_context_t *context;

    if (state->context_count == state->context_capacity) {
        size_t new_capacity = state->context_capacity == 0U ? 16U : state->context_capacity * 2U;
        tinypy_sre_context_t *grown;

        if (new_capacity > SIZE_MAX / sizeof(*grown)) {
            state->memory_failed = TINYPY_TRUE;
            return NULL;
        }
        grown = (tinypy_sre_context_t *)__tinypy_sre_grow(state, state->contexts, state->context_capacity * sizeof(*grown), new_capacity * sizeof(*grown));
        if (grown == NULL) {
            return NULL;
        }
        state->contexts = grown;
        state->context_capacity = new_capacity;
    }
    context = &state->contexts[state->context_count];
    state->context_count += 1U;
    (void)memset(context, 0, sizeof(*context));
    context->pc = pc;
    context->position = state->position;
    context->mark_offset = SIZE_MAX;
    context->repeat_index = SIZE_MAX;
    return context;
}
//////////////////////////////////////////////////////////////////////////
/* Counts how often the single-character item at item_pc matches, up to limit. */
static size_t __tinypy_sre_count(tinypy_sre_state_t *state, size_t item_pc, size_t position, size_t limit) {
    const uint32_t *code = state->pattern->code;
    size_t code_size = state->pattern->code_size;
    size_t start = position;
    size_t end = state->end;

    if (item_pc >= code_size || position >= end) {
        return 0U;
    }
    if (limit < end - position) {
        end = position + limit;
    }
    switch ((tinypy_sre_opcode_e)code[item_pc]) {
    case TINYPY_SRE_OP_ANY_ALL:
        position = end;
        break;
    case TINYPY_SRE_OP_ANY:
        while (position < end && __tinypy_sre_is_linebreak(__tinypy_sre_character_at(state, position)) == 0) {
            position += 1U;
        }
        break;
    case TINYPY_SRE_OP_LITERAL:
        if (item_pc + 1U >= code_size) {
            return 0U;
        }
        while (position < end && __tinypy_sre_character_at(state, position) == code[item_pc + 1U]) {
            position += 1U;
        }
        break;
    case TINYPY_SRE_OP_NOT_LITERAL:
        if (item_pc + 1U >= code_size) {
            return 0U;
        }
        while (position < end && __tinypy_sre_character_at(state, position) != code[item_pc + 1U]) {
            position += 1U;
        }
        break;
    case TINYPY_SRE_OP_IN:
        if (item_pc + 1U >= code_size) {
            return 0U;
        }
        while (position < end && __tinypy_sre_charset(state->pattern, item_pc + 2U, __tinypy_sre_character_at(state, position)) != 0) {
            position += 1U;
        }
        break;
    default:
        while (position < end) {
            size_t next_position;

            if (__tinypy_sre_match_one(state, item_pc, position, &next_position) == 0) {
                break;
            }
            position = next_position;
        }
        break;
    }
    return position - start;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_match(tinypy_sre_state_t *state, size_t start_pc, size_t *inout_position, size_t *marks, ptrdiff_t *inout_lastindex) {
    tinypy_sre_pattern_object_t *pattern = state->pattern;
    const uint32_t *code = pattern->code;
    size_t code_size = pattern->code_size;
    size_t mark_count = pattern->groups * 2U;
    tinypy_sre_context_t *ctx;
    tinypy_sre_repeat_t *rep;
    tinypy_bool_t ret = TINYPY_FALSE;
    size_t pc;
    size_t index;
    size_t skip;
    size_t minimum;
    size_t maximum;
    size_t count;
    size_t next_position;

    state->marks = marks;
    state->lastindex = *inout_lastindex;
    state->lastmark = -1;
    for (index = 0U; index < mark_count; ++index) {
        if (marks[index] != SIZE_MAX) {
            state->lastmark = (ptrdiff_t)index;
        }
    }
    state->position = *inout_position;
    state->context_count = 0U;
    state->repeat = SIZE_MAX;
    state->mark_top = 0U;
    if (__tinypy_sre_context_push(state, start_pc) == NULL) {
        return TINYPY_FALSE;
    }

entrance:
    ctx = &state->contexts[state->context_count - 1U];
    ctx->position = state->position;
    pc = ctx->pc;
    if (pc < code_size && code[pc] == TINYPY_SRE_OP_INFO) {
        if (pc + 3U >= code_size || code[pc + 1U] > code_size - pc - 1U) {
            goto invalid;
        }
        if (code[pc + 3U] != 0U && code[pc + 3U] > state->end - ctx->position) {
            ret = TINYPY_FALSE;
            goto exit;
        }
        pc += 1U + code[pc + 1U];
    }

dispatch:
    if (pc >= code_size) {
        goto invalid;
    }
    switch ((tinypy_sre_opcode_e)code[pc]) {
    case TINYPY_SRE_OP_SUCCESS:
        state->position = ctx->position;
        ret = TINYPY_TRUE;
        goto exit;
    case TINYPY_SRE_OP_FAILURE:
        ret = TINYPY_FALSE;
        goto exit;
    case TINYPY_SRE_OP_INFO:
    case TINYPY_SRE_OP_JUMP:
        if (pc + 1U >= code_size || code[pc + 1U] > code_size - pc - 1U) {
            goto invalid;
        }
        pc += 1U + code[pc + 1U];
        goto dispatch;
    case TINYPY_SRE_OP_MARK: {
        size_t mark;

        if (pc + 1U >= code_size) {
            goto invalid;
        }
        mark = code[pc + 1U];
        if (mark >= mark_count) {
            goto invalid;
        }
        if ((mark & 1U) != 0U) {
            state->lastindex = (ptrdiff_t)(mark / 2U + 1U);
        }
        if ((ptrdiff_t)mark > state->lastmark) {
            for (index = (size_t)(state->lastmark + 1); index < mark; ++index) {
                marks[index] = SIZE_MAX;
            }
            state->lastmark = (ptrdiff_t)mark;
        }
        marks[mark] = ctx->position;
        pc += 2U;
        goto dispatch;
    }
    case TINYPY_SRE_OP_LITERAL:
    case TINYPY_SRE_OP_LITERAL_IGNORE:
    case TINYPY_SRE_OP_NOT_LITERAL:
    case TINYPY_SRE_OP_NOT_LITERAL_IGNORE:
    case TINYPY_SRE_OP_ANY:
    case TINYPY_SRE_OP_ANY_ALL:
    case TINYPY_SRE_OP_CATEGORY:
        if (__tinypy_sre_match_one(state, pc, ctx->position, &next_position) == 0) {
            ret = TINYPY_FALSE;
            goto exit;
        }
        ctx->position = next_position;
        pc += code[pc] == TINYPY_SRE_OP_ANY || code[pc] == TINYPY_SRE_OP_ANY_ALL ? 1U : 2U;
        goto dispatch;
    case TINYPY_SRE_OP_IN:
    case TINYPY_SRE_OP_IN_IGNORE:
        if (pc + 1U >= code_size || code[pc + 1U] > code_size - pc - 1U) {
            goto invalid;
        }
        if (__tinypy_sre_match_one(state, pc, ctx->position, &next_position) == 0) {
            ret = TINYPY_FALSE;
            goto exit;
        }
        ctx->position = next_position;
        pc += 1U + code[pc + 1U];
        goto dispatch;
    case TINYPY_SRE_OP_AT:
        if (pc + 1U >= code_size) {
            goto invalid;
        }
        if (__tinypy_sre_at(state, ctx->position, code[pc + 1U]) == 0) {
            ret = TINYPY_FALSE;
            goto exit;
        }
        pc += 2U;
        goto dispatch;
    case TINYPY_SRE_OP_GROUPREF:
    case TINYPY_SRE_OP_GROUPREF_IGNORE: {
        size_t mark;
        size_t source;
        size_t source_end;

        if (pc + 1U >= code_size) {
            goto invalid;
        }
        mark = code[pc + 1U] * 2U;
        if (mark + 1U >= mark_count || (ptrdiff_t)(mark + 1U) > state->lastmark || marks[mark] == SIZE_MAX || marks[mark + 1U] == SIZE_MAX) {
            ret = TINYPY_FALSE;
            goto exit;
        }
        source = marks[mark];
        source_end = marks[mark + 1U];
        if (source_end < source) {
            ret = TINYPY_FALSE;
            goto exit;
        }
        while (source < source_end) {
            uint32_t left;
            uint32_t right;

            if (ctx->position >= state->end) {
                ret = TINYPY_FALSE;
                goto exit;
            }
            left = __tinypy_sre_character_at(state, source);
            right = __tinypy_sre_character_at(state, ctx->position);
            if (code[pc] == TINYPY_SRE_OP_GROUPREF_IGNORE) {
                left = __tinypy_sre_lower(left, pattern->flags);
                right = __tinypy_sre_lower(right, pattern->flags);
            }
            if (left != right) {
                ret = TINYPY_FALSE;
                goto exit;
            }
            source += 1U;
            ctx->position += 1U;
        }
        pc += 2U;
        goto dispatch;
    }
    case TINYPY_SRE_OP_GROUPREF_EXISTS: {
        size_t mark;

        if (pc + 2U >= code_size) {
            goto invalid;
        }
        mark = code[pc + 1U] * 2U;
        if (mark + 1U >= mark_count || (ptrdiff_t)(mark + 1U) > state->lastmark || marks[mark] == SIZE_MAX || marks[mark + 1U] == SIZE_MAX || marks[mark + 1U] < marks[mark]) {
            if (code[pc + 2U] > code_size - pc - 1U) {
                goto invalid;
            }
            pc += 1U + code[pc + 2U];
        }
        else {
            pc += 3U;
        }
        goto dispatch;
    }
    case TINYPY_SRE_OP_ASSERT:
    case TINYPY_SRE_OP_ASSERT_NOT: {
        size_t back;

        if (pc + 2U >= code_size || code[pc + 1U] > code_size - pc - 1U) {
            goto invalid;
        }
        back = code[pc + 2U];
        if (back > ctx->position - state->beginning) {
            if (code[pc] == TINYPY_SRE_OP_ASSERT) {
                ret = TINYPY_FALSE;
                goto exit;
            }
            pc += 1U + code[pc + 1U];
            goto dispatch;
        }
        state->position = ctx->position - back;
        ctx->pc = pc;
        ctx->jump = code[pc] == TINYPY_SRE_OP_ASSERT ? TINYPY_SRE_JUMP_ASSERT : TINYPY_SRE_JUMP_ASSERT_NOT;
        if (__tinypy_sre_context_push(state, pc + 3U) == NULL) {
            goto failure;
        }
        goto entrance;
    }
    case TINYPY_SRE_OP_BRANCH:
        ctx->lastmark = state->lastmark;
        ctx->lastindex = state->lastindex;
        ctx->repeat_index = state->repeat;
        if (state->repeat != SIZE_MAX) {
            ctx->mark_offset = __tinypy_sre_marks_save(state);
            if (ctx->mark_offset == SIZE_MAX) {
                goto failure;
            }
        }
        ctx->pc = pc + 1U;
        goto branch_next;
    case TINYPY_SRE_OP_REPEAT_ONE:
    case TINYPY_SRE_OP_MIN_REPEAT_ONE:
        if (pc + 3U >= code_size || code[pc + 1U] >= code_size - pc - 1U) {
            goto invalid;
        }
        skip = code[pc + 1U];
        minimum = code[pc + 2U];
        maximum = code[pc + 3U] == TINYPY_SRE_MAXREPEAT ? SIZE_MAX : code[pc + 3U];
        if (ctx->position > state->end || minimum > state->end - ctx->position) {
            ret = TINYPY_FALSE;
            goto exit;
        }
        count = __tinypy_sre_count(state, pc + 4U, ctx->position, code[pc] == TINYPY_SRE_OP_REPEAT_ONE ? maximum : minimum);
        if (count < minimum) {
            ret = TINYPY_FALSE;
            goto exit;
        }
        ctx->position += count;
        if (code[pc + 1U + skip] == TINYPY_SRE_OP_SUCCESS) {
            state->position = ctx->position;
            ret = TINYPY_TRUE;
            goto exit;
        }
        ctx->lastmark = state->lastmark;
        ctx->lastindex = state->lastindex;
        ctx->count = count;
        ctx->pc = pc;
        if (code[pc] == TINYPY_SRE_OP_REPEAT_ONE) {
            ctx->literal_tail = code[pc + 1U + skip] == TINYPY_SRE_OP_LITERAL && pc + 2U + skip < code_size ? TINYPY_TRUE : TINYPY_FALSE;
            ctx->literal = ctx->literal_tail != 0 ? code[pc + 2U + skip] : 0U;
            goto repeat_one_next;
        }
        goto min_repeat_one_next;
    case TINYPY_SRE_OP_REPEAT:
        if (pc + 3U >= code_size || code[pc + 1U] > code_size - pc - 1U) {
            goto invalid;
        }
        ctx->repeat.previous = state->repeat;
        ctx->repeat.pc = pc;
        ctx->repeat.count = SIZE_MAX;
        ctx->repeat.last_position = SIZE_MAX;
        state->repeat = state->context_count - 1U;
        state->position = ctx->position;
        ctx->pc = pc;
        ctx->jump = TINYPY_SRE_JUMP_REPEAT;
        if (__tinypy_sre_context_push(state, pc + 1U + code[pc + 1U]) == NULL) {
            goto failure;
        }
        goto entrance;
    case TINYPY_SRE_OP_MAX_UNTIL:
        if (state->repeat == SIZE_MAX) {
            goto invalid;
        }
        rep = &state->contexts[state->repeat].repeat;
        if (rep->pc + 3U >= code_size) {
            goto invalid;
        }
        ctx->repeat_index = state->repeat;
        ctx->pc = pc;
        minimum = code[rep->pc + 2U];
        maximum = code[rep->pc + 3U] == TINYPY_SRE_MAXREPEAT ? SIZE_MAX : code[rep->pc + 3U];
        count = rep->count + 1U;
        ctx->count = count;
        if (count < minimum) {
            rep->count = count;
            state->position = ctx->position;
            ctx->jump = TINYPY_SRE_JUMP_MAX_UNTIL_1;
            if (__tinypy_sre_context_push(state, rep->pc + 4U) == NULL) {
                goto failure;
            }
            goto entrance;
        }
        if (count < maximum && ctx->position != rep->last_position) {
            rep->count = count;
            ctx->lastmark = state->lastmark;
            ctx->lastindex = state->lastindex;
            ctx->mark_offset = __tinypy_sre_marks_save(state);
            if (ctx->mark_offset == SIZE_MAX) {
                goto failure;
            }
            ctx->saved_last_position = rep->last_position;
            rep->last_position = ctx->position;
            state->position = ctx->position;
            ctx->jump = TINYPY_SRE_JUMP_MAX_UNTIL_2;
            if (__tinypy_sre_context_push(state, rep->pc + 4U) == NULL) {
                goto failure;
            }
            goto entrance;
        }
        goto max_until_tail;
    case TINYPY_SRE_OP_MIN_UNTIL:
        if (state->repeat == SIZE_MAX) {
            goto invalid;
        }
        rep = &state->contexts[state->repeat].repeat;
        if (rep->pc + 3U >= code_size) {
            goto invalid;
        }
        ctx->repeat_index = state->repeat;
        ctx->pc = pc;
        minimum = code[rep->pc + 2U];
        count = rep->count + 1U;
        ctx->count = count;
        if (count < minimum) {
            rep->count = count;
            state->position = ctx->position;
            ctx->jump = TINYPY_SRE_JUMP_MIN_UNTIL_1;
            if (__tinypy_sre_context_push(state, rep->pc + 4U) == NULL) {
                goto failure;
            }
            goto entrance;
        }
        ctx->lastmark = state->lastmark;
        ctx->lastindex = state->lastindex;
        ctx->mark_offset = __tinypy_sre_marks_save(state);
        if (ctx->mark_offset == SIZE_MAX) {
            goto failure;
        }
        state->repeat = rep->previous;
        state->position = ctx->position;
        ctx->jump = TINYPY_SRE_JUMP_MIN_UNTIL_2;
        if (__tinypy_sre_context_push(state, pc + 1U) == NULL) {
            goto failure;
        }
        goto entrance;
    default:
        goto invalid;
    }

branch_next:
    /* ctx->pc is the header of the alternative to try; a zero header ends
       the list. */
    if (ctx->pc >= code_size) {
        goto invalid;
    }
    if (code[ctx->pc] == 0U) {
        if (ctx->mark_offset != SIZE_MAX) {
            __tinypy_sre_marks_drop(state, ctx->mark_offset);
        }
        ret = TINYPY_FALSE;
        goto exit;
    }
    if (code[ctx->pc] > code_size - ctx->pc) {
        goto invalid;
    }
    if (ctx->pc + 2U < code_size && (code[ctx->pc + 1U] == TINYPY_SRE_OP_LITERAL || code[ctx->pc + 1U] == TINYPY_SRE_OP_IN)) {
        tinypy_bool_t possible = TINYPY_FALSE;

        if (ctx->position < state->end) {
            uint32_t character = __tinypy_sre_character_at(state, ctx->position);

            if (code[ctx->pc + 1U] == TINYPY_SRE_OP_LITERAL) {
                possible = character == code[ctx->pc + 2U] ? TINYPY_TRUE : TINYPY_FALSE;
            }
            else {
                possible = __tinypy_sre_charset(pattern, ctx->pc + 3U, character);
            }
        }
        if (possible == 0) {
            ctx->pc += code[ctx->pc];
            goto branch_next;
        }
    }
    state->position = ctx->position;
    ctx->jump = TINYPY_SRE_JUMP_BRANCH;
    if (__tinypy_sre_context_push(state, ctx->pc + 1U) == NULL) {
        goto failure;
    }
    goto entrance;

repeat_one_next:
    minimum = code[ctx->pc + 2U];
    if (ctx->literal_tail != 0) {
        while (ctx->count >= minimum && (ctx->position >= state->end || __tinypy_sre_character_at(state, ctx->position) != ctx->literal)) {
            if (ctx->count == 0U) {
                break;
            }
            ctx->position -= 1U;
            ctx->count -= 1U;
        }
        if (ctx->count < minimum || ctx->position >= state->end || __tinypy_sre_character_at(state, ctx->position) != ctx->literal) {
            ret = TINYPY_FALSE;
            goto exit;
        }
    }
    state->position = ctx->position;
    ctx->jump = TINYPY_SRE_JUMP_REPEAT_ONE;
    if (__tinypy_sre_context_push(state, ctx->pc + 1U + code[ctx->pc + 1U]) == NULL) {
        goto failure;
    }
    goto entrance;

min_repeat_one_next:
    state->position = ctx->position;
    ctx->jump = TINYPY_SRE_JUMP_MIN_REPEAT_ONE;
    if (__tinypy_sre_context_push(state, ctx->pc + 1U + code[ctx->pc + 1U]) == NULL) {
        goto failure;
    }
    goto entrance;

max_until_tail:
    rep = &state->contexts[ctx->repeat_index].repeat;
    state->repeat = rep->previous;
    state->position = ctx->position;
    ctx->jump = TINYPY_SRE_JUMP_MAX_UNTIL_3;
    if (__tinypy_sre_context_push(state, ctx->pc + 1U) == NULL) {
        goto failure;
    }
    goto entrance;

invalid:
    state->invalid_code = TINYPY_TRUE;
failure:
    ret = TINYPY_FALSE;

exit:
    state->context_count -= 1U;
    if (state->context_count == 0U) {
        goto finished;
    }
    ctx = &state->contexts[state->context_count - 1U];
    if (state->invalid_code != 0 || state->memory_failed != 0) {
        ret = TINYPY_FALSE;
        goto exit;
    }
    switch ((tinypy_sre_jump_e)ctx->jump) {
    case TINYPY_SRE_JUMP_BRANCH:
        if (ret != 0) {
            if (ctx->mark_offset != SIZE_MAX) {
                __tinypy_sre_marks_drop(state, ctx->mark_offset);
            }
            goto exit;
        }
        if (ctx->mark_offset != SIZE_MAX) {
            __tinypy_sre_marks_restore(state, ctx->mark_offset, TINYPY_TRUE);
        }
        __tinypy_sre_lastmark_restore(state, ctx);
        ctx->pc += code[ctx->pc];
        goto branch_next;
    case TINYPY_SRE_JUMP_REPEAT_ONE:
        if (ret != 0) {
            goto exit;
        }
        __tinypy_sre_lastmark_restore(state, ctx);
        if (ctx->count <= code[ctx->pc + 2U]) {
            ret = TINYPY_FALSE;
            goto exit;
        }
        ctx->position -= 1U;
        ctx->count -= 1U;
        goto repeat_one_next;
    case TINYPY_SRE_JUMP_MIN_REPEAT_ONE:
        if (ret != 0) {
            goto exit;
        }
        maximum = code[ctx->pc + 3U] == TINYPY_SRE_MAXREPEAT ? SIZE_MAX : code[ctx->pc + 3U];
        if (ctx->count >= maximum || __tinypy_sre_match_one(state, ctx->pc + 4U, ctx->position, &next_position) == 0) {
            ret = TINYPY_FALSE;
            goto exit;
        }
        ctx->position = next_position;
        ctx->count += 1U;
        __tinypy_sre_lastmark_restore(state, ctx);
        goto min_repeat_one_next;
    case TINYPY_SRE_JUMP_REPEAT:
        state->repeat = ctx->repeat.previous;
        goto exit;
    case TINYPY_SRE_JUMP_MAX_UNTIL_1:
        if (ret != 0) {
            goto exit;
        }
        rep = &state->contexts[ctx->repeat_index].repeat;
        rep->count = ctx->count - 1U;
        ret = TINYPY_FALSE;
        goto exit;
    case TINYPY_SRE_JUMP_MAX_UNTIL_2:
        rep = &state->contexts[ctx->repeat_index].repeat;
        rep->last_position = ctx->saved_last_position;
        if (ret != 0) {
            __tinypy_sre_marks_drop(state, ctx->mark_offset);
            goto exit;
        }
        __tinypy_sre_marks_restore(state, ctx->mark_offset, TINYPY_FALSE);
        __tinypy_sre_lastmark_restore(state, ctx);
        rep->count = ctx->count - 1U;
        goto max_until_tail;
    case TINYPY_SRE_JUMP_MAX_UNTIL_3:
        if (ret != 0) {
            goto exit;
        }
        state->repeat = ctx->repeat_index;
        ret = TINYPY_FALSE;
        goto exit;
    case TINYPY_SRE_JUMP_MIN_UNTIL_1:
        if (ret != 0) {
            goto exit;
        }
        rep = &state->contexts[ctx->repeat_index].repeat;
        rep->count = ctx->count - 1U;
        ret = TINYPY_FALSE;
        goto exit;
    case TINYPY_SRE_JUMP_MIN_UNTIL_2:
        if (ret != 0) {
            __tinypy_sre_marks_drop(state, ctx->mark_offset);
            goto exit;
        }
        rep = &state->contexts[ctx->repeat_index].repeat;
        __tinypy_sre_marks_restore(state, ctx->mark_offset, TINYPY_FALSE);
        state->repeat = ctx->repeat_index;
        __tinypy_sre_lastmark_restore(state, ctx);
        maximum = code[rep->pc + 3U] == TINYPY_SRE_MAXREPEAT ? SIZE_MAX : code[rep->pc + 3U];
        if (ctx->count >= maximum || ctx->position == rep->last_position) {
            ret = TINYPY_FALSE;
            goto exit;
        }
        rep->count = ctx->count;
        ctx->saved_last_position = rep->last_position;
        rep->last_position = ctx->position;
        state->position = ctx->position;
        ctx->jump = TINYPY_SRE_JUMP_MIN_UNTIL_3;
        if (__tinypy_sre_context_push(state, rep->pc + 4U) == NULL) {
            goto failure;
        }
        goto entrance;
    case TINYPY_SRE_JUMP_MIN_UNTIL_3:
        rep = &state->contexts[ctx->repeat_index].repeat;
        rep->last_position = ctx->saved_last_position;
        if (ret != 0) {
            goto exit;
        }
        rep->count = ctx->count - 1U;
        ret = TINYPY_FALSE;
        goto exit;
    case TINYPY_SRE_JUMP_ASSERT:
        if (ret == 0) {
            goto exit;
        }
        pc = ctx->pc + 1U + code[ctx->pc + 1U];
        goto dispatch;
    case TINYPY_SRE_JUMP_ASSERT_NOT:
        if (ret != 0) {
            ret = TINYPY_FALSE;
            goto exit;
        }
        pc = ctx->pc + 1U + code[ctx->pc + 1U];
        goto dispatch;
    default:
        goto invalid;
    }

finished:
    if (ret != 0) {
        *inout_position = state->position;
        *inout_lastindex = state->lastindex;
        for (index = (size_t)(state->lastmark + 1); index < mark_count; ++index) {
            marks[index] = SIZE_MAX;
        }
    }
    return ret;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_integer(tinypy_value_t *value, int64_t *out_value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_LONG && TINYPY_LONG_DIGIT_COUNT(value) <= 4U) {
        *out_value = tinypy_long_as_i64(value);
        return TINYPY_TRUE;
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_sre_text_size(const tinypy_value_t *text) {
    size_t return_value;
    if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) {
        return_value = TINYPY_SIZED_SIZE(text);
    } else {
        const uint8_t *bytes;
        (void)tinypy_internal_bytes_view((tinypy_value_t *)text, &bytes, &return_value);
    }
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_subject_supported(tinypy_value_t *text) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(text);
    tinypy_bool_t result = kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE || kind == TINYPY_VALUE_BYTEARRAY || kind == TINYPY_VALUE_BUFFER;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_subject_length(tinypy_vm_t *vm, tinypy_value_t *string, size_t *out_size, tinypy_error_t **out_error) {
    if (__tinypy_sre_subject_supported(string) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected string or buffer", out_error);
        return TINYPY_FALSE;
    }
    size_t size = __tinypy_sre_text_size(string);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(string);

    if (kind != TINYPY_VALUE_UNICODE && tinypy_internal_object_has_special_override_key(string, vm->internal_special_length_key) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special_key(string, vm->internal_special_length_key, out_error);
        tinypy_value_t *arguments;
        tinypy_value_t *result;
        int64_t reported_size;

        if (method == NULL) {
            return TINYPY_FALSE;
        }
        arguments = TINYPY_RET_EMPTY_TUPLE(vm);
        result = tinypy_call(method, arguments, NULL, out_error);
        TINYPY_DECREF(arguments);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t converted = tinypy_internal_number_as_ssize(result, &reported_size, out_error);

        TINYPY_DECREF(result);
        if (converted == 0) {
            return TINYPY_FALSE;
        }
        if (reported_size < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__ returned a negative value", out_error);
            return TINYPY_FALSE;
        }
        if (kind != TINYPY_VALUE_STRING && size != (uint64_t)reported_size) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer size mismatch", out_error);
            return TINYPY_FALSE;
        }
        size = (size_t)reported_size;
    }
    *out_size = size;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_argument_integer(tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FLOAT) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_TYPE, "integer argument expected, got float", out_error);
        return TINYPY_FALSE;
    }
    tinypy_bool_t result = tinypy_internal_number_as_i64(value, out_value, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_text_slice(tinypy_vm_t *vm, tinypy_value_t *text, size_t start, size_t end, tinypy_error_t **out_error) {
    const uint8_t *bytes;
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(text);

    if (tinypy_internal_object_has_special_override_key(text, vm->internal_special_getslice_key) != 0 || (kind == TINYPY_VALUE_BYTEARRAY && tinypy_internal_object_has_special_override_key(text, vm->internal_special_getitem_key) != 0)) {
        tinypy_value_t *start_value = tinypy_integer_from_i64(vm, (int64_t)start);
        tinypy_value_t *end_value = tinypy_integer_from_i64(vm, (int64_t)end);
        tinypy_value_t *result = tinypy_internal_get_slice(text, start_value, end_value, out_error);
        TINYPY_DECREF(end_value);
        TINYPY_DECREF(start_value);
        return result;
    }

    if (kind == TINYPY_VALUE_UNICODE) {
        bytes = TINYPY_TEXT_BYTES(text);
        size_t byte_start = tinypy_internal_unicode_byte_offset(text, start);
        size_t byte_end = tinypy_internal_unicode_byte_offset(text, end);
        tinypy_value_t *return_value_1 = tinypy_unicode_from_utf8(vm, (const char *)bytes + byte_start, byte_end - byte_start);
        return return_value_1;
    }
    size_t size;
    (void)tinypy_internal_bytes_view(text, &bytes, &size);
    if (start > size) {
        start = size;
    }
    if (end > size) {
        end = size;
    }
    if (end < start) {
        end = start;
    }
    if (kind == TINYPY_VALUE_BYTEARRAY) {
        tinypy_value_t *result = tinypy_bytearray_from_bytes(vm, bytes != NULL ? bytes + start : NULL, end - start);
        return result;
    }
    tinypy_value_t *return_value_2 = tinypy_string_from_bytes(vm, bytes + start, end - start);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_empty_like(tinypy_vm_t *vm, tinypy_value_t *text, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_sre_text_slice(vm, text, 0U, 0U, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_sre_pattern_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(value);

    visit(pattern->pattern, user_data);
    visit(pattern->groupindex, user_data);
    visit(pattern->indexgroup, user_data);
    if (pattern->cache_string != NULL) {
        visit(pattern->cache_string, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_sre_pattern_destroy(tinypy_value_t *value) {
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(value);

    __tinypy_sre_pattern_cache_release(pattern);
    if (pattern->code != NULL) {
        tinypy_internal_vm_deallocate(TINYPY_VALUE_VM(value), pattern->code, pattern->code_size * sizeof(*pattern->code));
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_sre_match_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(value);

    visit(match->pattern, user_data);
    visit(match->string, user_data);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_sre_match_destroy(tinypy_value_t *value) {
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(value);

    if (match->marks != NULL) {
        tinypy_internal_vm_deallocate(TINYPY_VALUE_VM(value), match->marks, match->mark_count * sizeof(*match->marks));
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_match_new(tinypy_sre_state_t *state, tinypy_value_t *string, size_t pos, size_t endpos, size_t start, size_t end, const size_t *marks, ptrdiff_t lastindex) {
    tinypy_sre_match_object_t *match = (tinypy_sre_match_object_t *)tinypy_internal_value_allocate(state->vm, TINYPY_VALUE_SRE_MATCH, sizeof(*match));

    match->pattern = &state->pattern->base;
    match->string = string;
    match->pos = pos;
    match->endpos = endpos;
    match->start = start;
    match->end = end;
    match->mark_count = state->pattern->groups * 2U;
    match->lastindex = lastindex;
    TINYPY_INCREF(match->pattern);
    TINYPY_INCREF(string);
    if (match->mark_count != 0U) {
        match->marks = (size_t *)tinypy_internal_vm_allocate(state->vm, match->mark_count * sizeof(*match->marks));
        __tinypy_sre_copy_marks(match->marks, marks, match->mark_count);
    }
    return &match->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_state_initialize(tinypy_sre_state_t *state, tinypy_sre_pattern_object_t *pattern, tinypy_value_t *string, size_t endpos, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(&pattern->base);

    (void)memset(state, 0, sizeof(*state));
    state->vm = vm;
    state->pattern = pattern;
    state->string = string;
    if (TINYPY_VALUE_KIND(string) == TINYPY_VALUE_UNICODE) {
        state->bytes = TINYPY_TEXT_BYTES(string);
    }
    else {
        size_t byte_size;
        (void)tinypy_internal_bytes_view(string, &state->bytes, &byte_size);
    }
    state->size = __tinypy_sre_text_size(string);
    state->beginning = 0U;
    state->end = endpos;
    /* The matcher stacks and a decoded subject are handed over from the
       pattern's cache and returned on finalisation. */
    state->contexts = (tinypy_sre_context_t *)pattern->cache_contexts;
    state->context_capacity = pattern->cache_contexts_size / sizeof(*state->contexts);
    pattern->cache_contexts = NULL;
    pattern->cache_contexts_size = 0U;
    state->mark_stack = pattern->cache_marks;
    state->mark_capacity = pattern->cache_mark_capacity;
    pattern->cache_marks = NULL;
    pattern->cache_mark_capacity = 0U;
    if (TINYPY_VALUE_KIND(string) != TINYPY_VALUE_UNICODE || TINYPY_TEXT_BYTE_SIZE(string) == state->size) {
        return TINYPY_TRUE;
    }
    if (pattern->cache_string == string && pattern->cache_characters != NULL) {
        state->characters = pattern->cache_characters;
        pattern->cache_characters = NULL;
        return TINYPY_TRUE;
    }
    if (state->size > SIZE_MAX / sizeof(*state->characters)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "regular expression input is too large", out_error);
        return TINYPY_FALSE;
    }
    state->characters = (uint32_t *)tinypy_internal_vm_allocate_checked(vm, state->size * sizeof(*state->characters), out_error);
    if (state->characters == NULL) {
        return TINYPY_FALSE;
    }
    size_t byte_size = TINYPY_TEXT_BYTE_SIZE(string);
    size_t byte_offset = 0U;
    size_t character_index;

    for (character_index = 0U; character_index != state->size; ++character_index) {
        size_t width = tinypy_internal_utf8_decode(state->bytes + byte_offset, byte_size - byte_offset, &state->characters[character_index]);

        if (width == 0U) {
            tinypy_internal_vm_deallocate(vm, state->characters, state->size * sizeof(*state->characters));
            state->characters = NULL;
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "invalid internal Unicode data", out_error);
            return TINYPY_FALSE;
        }
        byte_offset += width;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_sre_state_finalize(tinypy_sre_state_t *state) {
    tinypy_sre_pattern_object_t *pattern = state->pattern;

    if (state->characters != NULL) {
        if (pattern->cache_characters != NULL) {
            tinypy_internal_vm_deallocate(state->vm, pattern->cache_characters, pattern->cache_size * sizeof(*pattern->cache_characters));
        }
        if (pattern->cache_string != state->string) {
            if (pattern->cache_string != NULL) {
                TINYPY_DECREF(pattern->cache_string);
            }
            pattern->cache_string = state->string;
            TINYPY_INCREF(state->string);
        }
        pattern->cache_characters = state->characters;
        pattern->cache_size = state->size;
    }
    if (state->mark_stack != NULL) {
        if (pattern->cache_marks != NULL) {
            tinypy_internal_vm_deallocate(state->vm, pattern->cache_marks, pattern->cache_mark_capacity * sizeof(*pattern->cache_marks));
        }
        pattern->cache_marks = state->mark_stack;
        pattern->cache_mark_capacity = state->mark_capacity;
    }
    if (state->contexts != NULL) {
        if (pattern->cache_contexts != NULL) {
            tinypy_internal_vm_deallocate(state->vm, pattern->cache_contexts, pattern->cache_contexts_size);
        }
        pattern->cache_contexts = state->contexts;
        pattern->cache_contexts_size = state->context_capacity * sizeof(*state->contexts);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_sre_pattern_cache_release(tinypy_sre_pattern_object_t *pattern) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(&pattern->base);

    if (pattern->cache_characters != NULL) {
        tinypy_internal_vm_deallocate(vm, pattern->cache_characters, pattern->cache_size * sizeof(*pattern->cache_characters));
        pattern->cache_characters = NULL;
    }
    if (pattern->cache_marks != NULL) {
        tinypy_internal_vm_deallocate(vm, pattern->cache_marks, pattern->cache_mark_capacity * sizeof(*pattern->cache_marks));
        pattern->cache_marks = NULL;
    }
    if (pattern->cache_contexts != NULL) {
        tinypy_internal_vm_deallocate(vm, pattern->cache_contexts, pattern->cache_contexts_size);
        pattern->cache_contexts = NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
/* Reports a matcher failure that is an error rather than a non-match. */
static tinypy_bool_t __tinypy_sre_state_check(tinypy_sre_state_t *state, tinypy_error_t **out_error) {
    if (state->invalid_code != 0) {
        tinypy_internal_make_vm_error(state->vm, TINYPY_ERROR_RUNTIME, "invalid SRE bytecode", out_error);
        return TINYPY_FALSE;
    }
    if (state->memory_failed != 0) {
        tinypy_internal_make_vm_error(state->vm, TINYPY_ERROR_MEMORY, "memory allocation failed", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_execute(tinypy_sre_pattern_object_t *pattern, tinypy_value_t *string, size_t pos, size_t endpos, int32_t search, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(&pattern->base);
    tinypy_sre_state_t state;
    size_t string_size;
    size_t candidate;
    tinypy_value_t *result = NULL;

    if (__tinypy_sre_subject_supported(string) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected string or buffer", out_error);
        return NULL;
    }
    string_size = __tinypy_sre_text_size(string);
    if (pos > string_size) {
        pos = string_size;
    }
    if (endpos > string_size) {
        endpos = string_size;
    }
    if (__tinypy_sre_state_initialize(&state, pattern, string, endpos, out_error) == 0) {
        return NULL;
    }
    /* The INFO block names a literal prefix or a leading character class;
       sre_search skips candidate positions that cannot start a match. The
       compiler only records them for patterns that never match empty. */
    uint32_t prefix_character = 0U;
    size_t charset_pc = 0U;
    tinypy_bool_t has_prefix = TINYPY_FALSE;

    if (search != 0 && pattern->code_size > 7U && pattern->code[0] == TINYPY_SRE_OP_INFO && pattern->code[1] <= pattern->code_size - 1U) {
        uint32_t info_flags = pattern->code[2];

        if ((info_flags & 1U) != 0U && pattern->code[5] != 0U && 7U < pattern->code_size) {
            has_prefix = TINYPY_TRUE;
            prefix_character = pattern->code[7];
        }
        else if ((info_flags & 4U) != 0U) {
            charset_pc = 5U;
        }
    }
    for (candidate = pos; candidate <= endpos || search == 0; ++candidate) {
        size_t marks[TINYPY_SRE_MAX_MARKS];
        size_t matched_end;
        ptrdiff_t lastindex = -1;
        size_t index;

        if (has_prefix != 0) {
            while (candidate < endpos && __tinypy_sre_character_at(&state, candidate) != prefix_character) {
                candidate += 1U;
            }
            if (candidate >= endpos) {
                break;
            }
        }
        else if (charset_pc != 0U) {
            while (candidate < endpos && __tinypy_sre_charset(pattern, charset_pc, __tinypy_sre_character_at(&state, candidate)) == 0) {
                candidate += 1U;
            }
            if (candidate >= endpos) {
                break;
            }
        }
        for (index = 0U; index < pattern->groups * 2U; ++index) {
            marks[index] = SIZE_MAX;
        }
        matched_end = candidate;
        state.invalid_code = TINYPY_FALSE;
        state.memory_failed = TINYPY_FALSE;
        if (__tinypy_sre_match(&state, 0U, &matched_end, marks, &lastindex) != 0) {
            result = __tinypy_sre_match_new(&state, string, pos, endpos, candidate, matched_end, marks, lastindex);
            break;
        }
        if (__tinypy_sre_state_check(&state, out_error) == 0) {
            break;
        }
        if (search == 0 || candidate == endpos) {
            break;
        }
    }
    __tinypy_sre_state_finalize(&state);
    if (result != NULL || tinypy_vm_has_error(vm) != 0) {
        return result;
    }
    result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "SRE method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_bounds(tinypy_sre_pattern_object_t *pattern, tinypy_value_t *args, size_t string_index, size_t *out_pos, size_t *out_endpos, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(&pattern->base);
    tinypy_value_t *string = TINYPY_TUPLE_GET(args, string_index);
    size_t size;
    int64_t pos = 0;
    int64_t endpos = INT64_MAX;
    if (TINYPY_TUPLE_SIZE(args) > string_index + 1U) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, string_index + 1U);
        if (__tinypy_sre_argument_integer(item, &pos, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    if (TINYPY_TUPLE_SIZE(args) > string_index + 2U) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, string_index + 2U);
        if (__tinypy_sre_argument_integer(item, &endpos, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    if (__tinypy_sre_subject_supported(string) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected string or buffer", out_error);
        return TINYPY_FALSE;
    }
    if (__tinypy_sre_subject_length(vm, string, &size, out_error) == 0) {
        return TINYPY_FALSE;
    }
    *out_pos = pos < 0 ? 0U : ((uint64_t)pos > size ? size : (size_t)pos);
    *out_endpos = endpos < 0 ? 0U : ((uint64_t)endpos > size ? size : (size_t)endpos);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_pattern_match_or_search(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t pos;
    size_t endpos;

    tinypy_bool_t condition = __tinypy_sre_method_arguments(vm, args, kwargs, 2U, 4U, out_error) == 0;
    if (condition == 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
        condition = TINYPY_VALUE_KIND(item_2) != TINYPY_VALUE_SRE_PATTERN;
    }
    if (condition) {
        return NULL;
    }
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    if (__tinypy_sre_bounds(pattern, args, 1U, &pos, &endpos, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *return_value_1 = __tinypy_sre_execute(pattern, item, pos, endpos, user_data != NULL ? INT32_C(1) : INT32_C(0), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_match_group_index(tinypy_sre_match_object_t *match, tinypy_value_t *index_value, size_t *out_index, tinypy_error_t **out_error) {
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(match->pattern);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(&match->base);
    int64_t index;

    if (__tinypy_sre_integer(index_value, &index) == 0) {
        tinypy_value_t *value = tinypy_dict_get_optional(pattern->groupindex, index_value);
        tinypy_bool_t condition_2 = value == NULL;

        if (condition_2 == 0) {
            condition_2 = __tinypy_sre_integer(value, &index) == 0;
        }
        if (condition_2) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "no such group", out_error);
            return TINYPY_FALSE;
        }
    }
    if (index < 0 || (uint64_t)index > pattern->groups) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "no such group", out_error);
        return TINYPY_FALSE;
    }
    *out_index = (size_t)index;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_match_group_value(tinypy_sre_match_object_t *match, size_t index, tinypy_value_t *default_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(&match->base);
    size_t start;
    size_t end;

    if (index == 0U) {
        tinypy_value_t *return_value_1 = __tinypy_sre_text_slice(vm, match->string, match->start, match->end, out_error);
        return return_value_1;
    }
    start = match->marks[(index - 1U) * 2U];
    end = match->marks[(index - 1U) * 2U + 1U];
    if (start == SIZE_MAX || end == SIZE_MAX) {
        if (default_value == NULL) {
            tinypy_value_t *empty = __tinypy_sre_empty_like(vm, match->string, out_error);
            return empty;
        }
        return TINYPY_RET(default_value);
    }
    if (end < start) {
        end = start;
    }
    tinypy_value_t *return_value_2 = __tinypy_sre_text_slice(vm, match->string, start, end, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_match_group(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count;

    (void)user_data;
    tinypy_bool_t condition_3 = __tinypy_sre_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, out_error) == 0;
    if (condition_3 == 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
        condition_3 = TINYPY_VALUE_KIND(item_2) != TINYPY_VALUE_SRE_MATCH;
    }
    if (condition_3) {
        return NULL;
    }
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    count = TINYPY_TUPLE_SIZE(args) - 1U;
    tinypy_value_t *none = TINYPY_RET_NONE(vm);
    if (count <= 1U) {
        size_t index = 0U;
        tinypy_value_t *result;

        tinypy_bool_t condition_4 = count == 1U;
        if (condition_4 != 0) {
            tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
            condition_4 = __tinypy_sre_match_group_index(match, item_2, &index, out_error) == 0;
        }
        if (condition_4) {
            TINYPY_DECREF(none);
            return NULL;
        }
        result = __tinypy_sre_match_group_value(match, index, none, out_error);
        TINYPY_DECREF(none);
        return result;
    }
    tinypy_value_t **items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, count * sizeof(*items));
    tinypy_value_t *result;
    size_t item_index;

    for (item_index = 0U; item_index < count; ++item_index) {
        size_t group;

        tinypy_value_t *item = TINYPY_TUPLE_GET(args, item_index + 1U);
        if (__tinypy_sre_match_group_index(match, item, &group, out_error) == 0) {
            while (item_index != 0U) {
                TINYPY_DECREF(items[--item_index]);
            }
            tinypy_internal_vm_deallocate(vm, items, count * sizeof(*items));
            TINYPY_DECREF(none);
            return NULL;
        }
        items[item_index] = __tinypy_sre_match_group_value(match, group, none, out_error);
        if (items[item_index] == NULL) {
            while (item_index != 0U) {
                TINYPY_DECREF(items[--item_index]);
            }
            tinypy_internal_vm_deallocate(vm, items, count * sizeof(*items));
            TINYPY_DECREF(none);
            return NULL;
        }
    }
    result = tinypy_tuple_from_items(vm, items, count);
    for (item_index = 0U; item_index < count; ++item_index) {
        TINYPY_DECREF(items[item_index]);
    }
    tinypy_internal_vm_deallocate(vm, items, count * sizeof(*items));
    TINYPY_DECREF(none);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_match_groups(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *default_value;
    tinypy_value_t **items;
    tinypy_value_t *result;
    size_t index;

    (void)user_data;
    tinypy_bool_t condition_5 = __tinypy_sre_method_arguments(vm, args, kwargs, 1U, 2U, out_error) == 0;
    if (condition_5 == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        condition_5 = TINYPY_VALUE_KIND(item) != TINYPY_VALUE_SRE_MATCH;
    }
    if (condition_5) {
        return NULL;
    }
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(match->pattern);
    default_value = TINYPY_TUPLE_SIZE(args) == 2U ? TINYPY_TUPLE_GET(args, 1U) : &vm->none_object.base;
    if (pattern->groups == 0U) {
        tinypy_value_t *return_value_1 = TINYPY_RET_EMPTY_TUPLE(vm);
        return return_value_1;
    }
    items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, pattern->groups * sizeof(*items));
    for (index = 0U; index < pattern->groups; ++index) {
        items[index] = __tinypy_sre_match_group_value(match, index + 1U, default_value, out_error);
        if (items[index] == NULL) {
            while (index != 0U) {
                TINYPY_DECREF(items[--index]);
            }
            tinypy_internal_vm_deallocate(vm, items, pattern->groups * sizeof(*items));
            return NULL;
        }
    }
    result = tinypy_tuple_from_items(vm, items, pattern->groups);
    for (index = 0U; index < pattern->groups; ++index) {
        TINYPY_DECREF(items[index]);
    }
    tinypy_internal_vm_deallocate(vm, items, pattern->groups * sizeof(*items));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_match_span_value(tinypy_sre_match_object_t *match, tinypy_value_t *args, size_t *out_start, size_t *out_end, tinypy_error_t **out_error) {
    size_t group = 0U;

    tinypy_bool_t condition_6 = TINYPY_TUPLE_SIZE(args) == 2U;
    if (condition_6 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        condition_6 = __tinypy_sre_match_group_index(match, item, &group, out_error) == 0;
    }
    if (condition_6) {
        return TINYPY_FALSE;
    }
    if (group == 0U) {
        *out_start = match->start;
        *out_end = match->end;
    }
    else {
        *out_start = match->marks[(group - 1U) * 2U];
        *out_end = match->marks[(group - 1U) * 2U + 1U];
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_match_span_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t start;
    size_t end;
    intptr_t operation = (intptr_t)user_data;

    tinypy_bool_t condition_7 = __tinypy_sre_method_arguments(vm, args, kwargs, 1U, 2U, out_error) == 0;
    if (condition_7 == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        condition_7 = TINYPY_VALUE_KIND(item) != TINYPY_VALUE_SRE_MATCH;
    }
    if (condition_7) {
        return NULL;
    }
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    if (__tinypy_sre_match_span_value(match, args, &start, &end, out_error) == 0) {
        return NULL;
    }
    if (operation == 0) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, start == SIZE_MAX ? INT64_C(-1) : (int64_t)start);
        return return_value_1;
    }
    if (operation == 1) {
        tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, end == SIZE_MAX ? INT64_C(-1) : (int64_t)end);
        return return_value_2;
    }
    tinypy_value_t *start_value = tinypy_integer_from_i64(vm, start == SIZE_MAX ? INT64_C(-1) : (int64_t)start);
    tinypy_value_t *end_value = tinypy_integer_from_i64(vm, end == SIZE_MAX ? INT64_C(-1) : (int64_t)end);
    tinypy_value_t *items[2] = {start_value, end_value};
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);

    TINYPY_DECREF(end_value);
    TINYPY_DECREF(start_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_pattern_findall(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;
    size_t pos;
    size_t endpos;

    (void)user_data;
    tinypy_bool_t condition_8 = __tinypy_sre_method_arguments(vm, args, kwargs, 2U, 4U, out_error) == 0;
    if (condition_8 == 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
        condition_8 = TINYPY_VALUE_KIND(item_2) != TINYPY_VALUE_SRE_PATTERN;
    }
    if (condition_8) {
        return NULL;
    }
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    tinypy_value_t *string = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_sre_bounds(pattern, args, 1U, &pos, &endpos, out_error) == 0) {
        return NULL;
    }
    result = tinypy_list_from_items(vm, NULL, 0U);
    while (pos <= endpos) {
        tinypy_value_t *match_value = __tinypy_sre_execute(pattern, string, pos, endpos, INT32_C(1), out_error);
        tinypy_sre_match_object_t *match;
        tinypy_value_t *item;

        if (match_value == NULL) {
            TINYPY_DECREF(result);
            return NULL;
        }
        if (TINYPY_VALUE_KIND(match_value) == TINYPY_VALUE_NONE) {
            TINYPY_DECREF(match_value);
            break;
        }
        match = TINYPY_SRE_MATCH_OBJECT(match_value);
        if (pattern->groups == 0U) {
            item = __tinypy_sre_text_slice(vm, string, match->start, match->end, out_error);
        }
        else if (pattern->groups == 1U) {
            item = __tinypy_sre_match_group_value(match, 1U, NULL, out_error);
        }
        else {
            tinypy_value_t **items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, pattern->groups * sizeof(*items));
            size_t index;

            for (index = 0U; index < pattern->groups; ++index) {
                items[index] = __tinypy_sre_match_group_value(match, index + 1U, NULL, out_error);
                if (items[index] == NULL) {
                    while (index != 0U) {
                        TINYPY_DECREF(items[--index]);
                    }
                    tinypy_internal_vm_deallocate(vm, items, pattern->groups * sizeof(*items));
                    TINYPY_DECREF(match_value);
                    TINYPY_DECREF(result);
                    return NULL;
                }
            }
            item = tinypy_tuple_from_items(vm, items, pattern->groups);
            for (index = 0U; index < pattern->groups; ++index) {
                TINYPY_DECREF(items[index]);
            }
            tinypy_internal_vm_deallocate(vm, items, pattern->groups * sizeof(*items));
        }
        if (item == NULL) {
            TINYPY_DECREF(match_value);
            TINYPY_DECREF(result);
            return NULL;
        }
        if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(match_value);
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(item);
        pos = match->end == match->start ? match->end + 1U : match->end;
        TINYPY_DECREF(match_value);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_join(tinypy_vm_t *vm, tinypy_value_t *pieces, tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_value_t *separator = __tinypy_sre_empty_like(vm, source, out_error);
    if (separator == NULL) {
        return NULL;
    }
    if (TINYPY_LIST_SIZE(pieces) == 0U) {
        return separator;
    }
    tinypy_value_t *join = tinypy_object_get_attr_value(separator, vm->internal_join_key, out_error);
    TINYPY_DECREF(separator);
    if (join == NULL) {
        return NULL;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &pieces, 1U);
    tinypy_value_t *result = tinypy_call(join, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(join);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_pattern_sub_expanded(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *string;
    tinypy_value_t *pieces;
    tinypy_value_t *joined;
    size_t string_size;
    size_t pos = 0U;
    size_t copied = 0U;
    size_t substitutions = 0U;
    int64_t limit = 0;
    tinypy_bool_t callable;

    tinypy_bool_t condition_9 = __tinypy_sre_method_arguments(vm, args, kwargs, 3U, 4U, out_error) == 0;
    if (condition_9 == 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
        condition_9 = TINYPY_VALUE_KIND(item_2) != TINYPY_VALUE_SRE_PATTERN;
    }
    if (condition_9) {
        return NULL;
    }
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    tinypy_value_t *replacement = TINYPY_TUPLE_GET(args, 1U);
    string = TINYPY_TUPLE_GET(args, 2U);
    if (TINYPY_TUPLE_SIZE(args) == 4U) {
        int64_t count;

        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 3U);
        if (__tinypy_sre_argument_integer(item, &count, out_error) == 0) {
            return NULL;
        }
        limit = count;
    }
    if (__tinypy_sre_subject_supported(string) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected string or buffer", out_error);
        return NULL;
    }
    callable = replacement->type->call != NULL || tinypy_internal_object_has_special_key(replacement, vm->internal_special_call_key) != 0;
    if (callable == 0 && __tinypy_sre_subject_supported(replacement) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "replacement must be a string or callable", out_error);
        return NULL;
    }
    if (__tinypy_sre_subject_length(vm, string, &string_size, out_error) == 0) {
        return NULL;
    }
    pieces = tinypy_list_from_items(vm, NULL, 0U);
    while (pos <= string_size && (limit == 0 || (limit > 0 && substitutions < (uint64_t)limit))) {
        tinypy_value_t *match_value = __tinypy_sre_execute(pattern, string, pos, string_size, INT32_C(1), out_error);
        tinypy_sre_match_object_t *match;
        tinypy_value_t *piece;

        if (match_value == NULL) {
            TINYPY_DECREF(pieces);
            return NULL;
        }
        if (TINYPY_VALUE_KIND(match_value) == TINYPY_VALUE_NONE) {
            TINYPY_DECREF(match_value);
            break;
        }
        match = TINYPY_SRE_MATCH_OBJECT(match_value);
        match->pos = 0U;
        if (copied < match->start) {
            piece = __tinypy_sre_text_slice(vm, string, copied, match->start, out_error);
            if (piece == NULL) {
                TINYPY_DECREF(match_value);
                TINYPY_DECREF(pieces);
                return NULL;
            }
            if (tinypy_internal_list_append_checked(pieces, piece, out_error) == 0) {
                TINYPY_DECREF(piece);
                TINYPY_DECREF(match_value);
                TINYPY_DECREF(pieces);
                return NULL;
            }
            TINYPY_DECREF(piece);
        }
        else if (copied == match->start && copied == match->end && substitutions != 0U) {
            pos = match->end < string_size ? match->end + 1U : string_size + 1U;
            TINYPY_DECREF(match_value);
            continue;
        }
        if (callable != 0) {
            tinypy_value_t *call_args = tinypy_tuple_from_items(vm, &match_value, 1U);

            piece = tinypy_call(replacement, call_args, NULL, out_error);
            TINYPY_DECREF(call_args);
            if (piece == NULL) {
                TINYPY_DECREF(match_value);
                TINYPY_DECREF(pieces);
                return NULL;
            }
        }
        else {
            piece = TINYPY_RET(replacement);
        }
        if (TINYPY_VALUE_KIND(piece) != TINYPY_VALUE_NONE) {
            if (tinypy_internal_list_append_checked(pieces, piece, out_error) == 0) {
                TINYPY_DECREF(piece);
                TINYPY_DECREF(match_value);
                TINYPY_DECREF(pieces);
                return NULL;
            }
        }
        TINYPY_DECREF(piece);
        copied = match->end;
        substitutions += 1U;
        pos = match->end == match->start ? match->end + 1U : match->end;
        TINYPY_DECREF(match_value);
    }
    if (copied < string_size) {
        tinypy_value_t *tail = __tinypy_sre_text_slice(vm, string, copied, string_size, out_error);
        if (tail == NULL) {
            TINYPY_DECREF(pieces);
            return NULL;
        }

        if (tinypy_internal_list_append_checked(pieces, tail, out_error) == 0) {
            TINYPY_DECREF(tail);
            TINYPY_DECREF(pieces);
            return NULL;
        }
        TINYPY_DECREF(tail);
    }
    joined = __tinypy_sre_join(vm, pieces, string, out_error);
    TINYPY_DECREF(pieces);
    if (joined == NULL) {
        return NULL;
    }
    if (user_data == NULL) {
        return joined;
    }
    tinypy_value_t *count = tinypy_integer_from_i64(vm, (int64_t)substitutions);
    tinypy_value_t *items[2] = {joined, count};
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);

    TINYPY_DECREF(count);
    TINYPY_DECREF(joined);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_compile(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *groupindex;
    tinypy_value_t *indexgroup;
    tinypy_sre_pattern_object_t *pattern;
    int64_t flags;
    int64_t groups;
    size_t code_size;
    size_t index;
    tinypy_value_t *flags_value;
    tinypy_value_t *groups_value;

    (void)user_data;
    if (__tinypy_sre_method_arguments(vm, args, kwargs, 6U, 6U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *source = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *code_value = TINYPY_TUPLE_GET(args, 2U);
    groupindex = TINYPY_TUPLE_GET(args, 4U);
    indexgroup = TINYPY_TUPLE_GET(args, 5U);
    flags_value = TINYPY_TUPLE_GET(args, 1U);
    groups_value = TINYPY_TUPLE_GET(args, 3U);
    if (TINYPY_VALUE_KIND(source) != TINYPY_VALUE_NONE &&
        TINYPY_VALUE_KIND(source) != TINYPY_VALUE_STRING &&
        TINYPY_VALUE_KIND(source) != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "invalid SRE compile arguments", out_error);
        return NULL;
    }
    if (__tinypy_sre_integer(flags_value, &flags) == 0 ||
        __tinypy_sre_integer(groups_value, &groups) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "invalid SRE compile arguments", out_error);
        return NULL;
    }
    if (groups < 0 || groups > 100 ||
        TINYPY_VALUE_KIND(groupindex) != TINYPY_VALUE_DICT ||
        TINYPY_VALUE_KIND(indexgroup) != TINYPY_VALUE_LIST ||
        (TINYPY_VALUE_KIND(code_value) != TINYPY_VALUE_LIST &&
         TINYPY_VALUE_KIND(code_value) != TINYPY_VALUE_TUPLE)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "invalid SRE compile arguments", out_error);
        return NULL;
    }
    code_size = TINYPY_VALUE_KIND(code_value) == TINYPY_VALUE_LIST ? TINYPY_LIST_SIZE(code_value) : TINYPY_TUPLE_SIZE(code_value);
    if (code_size == 0U || code_size > SIZE_MAX / sizeof(uint32_t)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid SRE code", out_error);
        return NULL;
    }
    pattern = (tinypy_sre_pattern_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_SRE_PATTERN, sizeof(*pattern));
    pattern->pattern = source;
    pattern->groupindex = groupindex;
    pattern->indexgroup = indexgroup;
    pattern->code_size = code_size;
    pattern->groups = (size_t)groups;
    pattern->flags = flags;
    TINYPY_INCREF(source);
    TINYPY_INCREF(groupindex);
    TINYPY_INCREF(indexgroup);
    pattern->code = (uint32_t *)tinypy_internal_vm_allocate(vm, code_size * sizeof(*pattern->code));
    for (index = 0U; index < code_size; ++index) {
        tinypy_value_t *item = TINYPY_VALUE_KIND(code_value) == TINYPY_VALUE_LIST ? TINYPY_LIST_GET(code_value, index) : TINYPY_TUPLE_GET(code_value, index);
        int64_t value;

        if (__tinypy_sre_integer(item, &value) == 0 || value < 0 || (uint64_t)value > UINT32_MAX) {
            TINYPY_DECREF(&pattern->base);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "SRE code must contain unsigned integers", out_error);
            return NULL;
        }
        pattern->code[index] = (uint32_t)value;
    }
    return &pattern->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_getlower(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t character;
    int64_t flags;

    (void)user_data;
    if (__tinypy_sre_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "getlower arguments are invalid", out_error);
        return NULL;
    }
    tinypy_value_t *character_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *flags_value = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_sre_argument_integer(character_value, &character, out_error) == 0) {
        return NULL;
    }
    if (character < INT32_MIN || character > INT32_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, character < INT32_MIN ? "signed integer is less than minimum" : "signed integer is greater than maximum", out_error);
        return NULL;
    }
    if (__tinypy_sre_argument_integer(flags_value, &flags, out_error) == 0) {
        return NULL;
    }
    if (flags < INT32_MIN || flags > INT32_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, flags < INT32_MIN ? "signed integer is less than minimum" : "signed integer is greater than maximum", out_error);
        return NULL;
    }
    uint32_t sre_lower = __tinypy_sre_lower((uint32_t)character, flags);
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)(int32_t)sre_lower);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_getcodesize(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_sre_method_arguments(vm, args, kwargs, 0U, 0U, out_error) == 0) {
        return NULL;
    }
    result = tinypy_integer_from_i64(vm, INT64_C(4));
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Template expansion and the scanner protocol live in the re module, exactly
   as they do in Python 2.7, so the engine calls back into it. */
static tinypy_value_t *__tinypy_sre_call_re_helper(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error) {
    tinypy_value_t *module = tinypy_import_module_key(vm->internal_re_key, NULL, NULL, INT32_C(0), out_error);
    tinypy_value_t *helper;
    tinypy_value_t *call_args;
    tinypy_value_t *result;

    if (module == NULL) {
        return NULL;
    }
    helper = tinypy_object_get_attr_value(module, name, out_error);
    TINYPY_DECREF(module);
    if (helper == NULL) {
        return NULL;
    }
    call_args = tinypy_tuple_from_items(vm, items, item_count);
    result = tinypy_call(helper, call_args, NULL, out_error);
    TINYPY_DECREF(call_args);
    TINYPY_DECREF(helper);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_sre_template_is_literal(tinypy_value_t *replacement, size_t size) {
    const uint8_t *bytes;
    size_t index;

    if (TINYPY_VALUE_KIND(replacement) == TINYPY_VALUE_UNICODE) {
        bytes = TINYPY_TEXT_BYTES(replacement);
        size = TINYPY_TEXT_BYTE_SIZE(replacement);
    }
    else {
        size_t byte_size;

        (void)tinypy_internal_bytes_view(replacement, &bytes, &byte_size);
        if (size > byte_size) {
            size = byte_size;
        }
    }

    for (index = 0U; index < size; ++index) {
        if (bytes[index] == (uint8_t)'\\') {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_sre_match_regs(tinypy_value_t *match_value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(match_value);
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(match_value);
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(match->pattern);
    size_t count = pattern->groups + 1U;
    tinypy_value_t **items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, count * sizeof(*items));
    tinypy_value_t *result;
    size_t index;

    for (index = 0U; index < count; ++index) {
        size_t start = index == 0U ? match->start : match->marks[(index - 1U) * 2U];
        size_t end = index == 0U ? match->end : match->marks[(index - 1U) * 2U + 1U];
        tinypy_value_t *span[2];

        if (start == SIZE_MAX || end == SIZE_MAX) {
            span[0] = tinypy_integer_from_i64(vm, INT64_C(-1));
            span[1] = tinypy_integer_from_i64(vm, INT64_C(-1));
        }
        else {
            span[0] = tinypy_integer_from_i64(vm, (int64_t)start);
            span[1] = tinypy_integer_from_i64(vm, (int64_t)end);
        }
        items[index] = tinypy_tuple_from_items(vm, span, 2U);
        TINYPY_DECREF(span[1]);
        TINYPY_DECREF(span[0]);
    }
    result = tinypy_tuple_from_items(vm, items, count);
    for (index = 0U; index < count; ++index) {
        TINYPY_DECREF(items[index]);
    }
    tinypy_internal_vm_deallocate(vm, items, count * sizeof(*items));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_match_groupdict(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *default_value;
    tinypy_value_t *result;
    size_t position = 0U;
    tinypy_value_t *name;
    tinypy_value_t *index_value;

    (void)user_data;
    tinypy_bool_t condition = __tinypy_sre_method_arguments(vm, args, kwargs, 1U, 2U, out_error) == 0;
    if (condition == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        condition = TINYPY_VALUE_KIND(item) != TINYPY_VALUE_SRE_MATCH;
    }
    if (condition) {
        return NULL;
    }
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(match->pattern);
    default_value = TINYPY_TUPLE_SIZE(args) == 2U ? TINYPY_TUPLE_GET(args, 1U) : TINYPY_RET_NONE(vm);
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        TINYPY_INCREF(default_value);
    }
    result = tinypy_dict_new(vm);
    while (tinypy_dict_next(pattern->groupindex, &position, &name, &index_value) != 0) {
        size_t index;
        tinypy_value_t *value;

        if (__tinypy_sre_match_group_index(match, name, &index, out_error) == 0) {
            TINYPY_DECREF(result);
            TINYPY_DECREF(default_value);
            return NULL;
        }
        value = __tinypy_sre_match_group_value(match, index, default_value, out_error);
        if (value == NULL) {
            TINYPY_DECREF(result);
            TINYPY_DECREF(default_value);
            return NULL;
        }
        tinypy_dict_set(result, name, value);
        TINYPY_DECREF(value);
    }
    TINYPY_DECREF(default_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_match_expand(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    tinypy_bool_t condition = __tinypy_sre_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0;
    if (condition == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        condition = TINYPY_VALUE_KIND(item) != TINYPY_VALUE_SRE_MATCH;
    }
    if (condition) {
        return NULL;
    }
    tinypy_value_t *match_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(match_value);
    tinypy_value_t *items[3] = {match->pattern, match_value, TINYPY_TUPLE_GET(args, 1U)};
    tinypy_value_t *return_value_1 = __tinypy_sre_call_re_helper(vm, vm->internal_sre_expand_key, items, 3U, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_pattern_split(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *string;
    tinypy_value_t *result;
    size_t string_size;
    size_t pos = 0U;
    size_t last = 0U;
    size_t splits = 0U;
    int64_t limit = 0;

    (void)user_data;
    tinypy_bool_t condition = __tinypy_sre_method_arguments(vm, args, kwargs, 2U, 3U, out_error) == 0;
    if (condition == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        condition = TINYPY_VALUE_KIND(item) != TINYPY_VALUE_SRE_PATTERN;
    }
    if (condition) {
        return NULL;
    }
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    string = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_TUPLE_SIZE(args) == 3U) {
        int64_t count;

        if (__tinypy_sre_argument_integer(TINYPY_TUPLE_GET(args, 2U), &count, out_error) == 0) {
            return NULL;
        }
        limit = count;
    }
    if (__tinypy_sre_subject_supported(string) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected string or buffer", out_error);
        return NULL;
    }
    if (__tinypy_sre_subject_length(vm, string, &string_size, out_error) == 0) {
        return NULL;
    }
    result = tinypy_list_from_items(vm, NULL, 0U);
    while (pos <= string_size && (limit == 0 || (limit > 0 && splits < (uint64_t)limit))) {
        tinypy_value_t *match_value = __tinypy_sre_execute(pattern, string, pos, string_size, INT32_C(1), out_error);
        tinypy_sre_match_object_t *match;
        tinypy_value_t *piece;
        size_t group;

        if (match_value == NULL) {
            TINYPY_DECREF(result);
            return NULL;
        }
        if (TINYPY_VALUE_KIND(match_value) == TINYPY_VALUE_NONE) {
            TINYPY_DECREF(match_value);
            break;
        }
        match = TINYPY_SRE_MATCH_OBJECT(match_value);
        /* Python 2.7 never splits on an empty match. */
        if (match->start == match->end) {
            TINYPY_DECREF(match_value);
            pos += 1U;
            continue;
        }
        piece = __tinypy_sre_text_slice(vm, string, last, match->start, out_error);
        if (piece == NULL) {
            TINYPY_DECREF(match_value);
            TINYPY_DECREF(result);
            return NULL;
        }
        if (tinypy_internal_list_append_checked(result, piece, out_error) == 0) {
            TINYPY_DECREF(piece);
            TINYPY_DECREF(match_value);
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(piece);
        for (group = 1U; group <= pattern->groups; ++group) {
            tinypy_value_t *none = TINYPY_RET_NONE(vm);
            tinypy_value_t *value = __tinypy_sre_match_group_value(match, group, none, out_error);

            TINYPY_DECREF(none);
            if (value == NULL) {
                TINYPY_DECREF(match_value);
                TINYPY_DECREF(result);
                return NULL;
            }
            if (tinypy_internal_list_append_checked(result, value, out_error) == 0) {
                TINYPY_DECREF(value);
                TINYPY_DECREF(match_value);
                TINYPY_DECREF(result);
                return NULL;
            }
            TINYPY_DECREF(value);
        }
        last = match->end;
        pos = match->end;
        splits += 1U;
        TINYPY_DECREF(match_value);
    }
    tinypy_value_t *tail = __tinypy_sre_text_slice(vm, string, last, string_size, out_error);
    if (tail == NULL) {
        TINYPY_DECREF(result);
        return NULL;
    }

    if (tinypy_internal_list_append_checked(result, tail, out_error) == 0) {
        TINYPY_DECREF(tail);
        TINYPY_DECREF(result);
        return NULL;
    }
    TINYPY_DECREF(tail);
    return result;
}
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_internal_sre_scanner_payload_t {
    tinypy_value_t *pattern;
    tinypy_value_t *string;
    size_t pos;
    size_t initial_pos;
    size_t endpos;
} tinypy_internal_sre_scanner_payload_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_internal_sre_scanner_payload_t *__tinypy_sre_scanner_payload(tinypy_value_t *value) {
    tinypy_internal_sre_scanner_payload_t *payload = (tinypy_internal_sre_scanner_payload_t *)tinypy_native_instance_payload(value);
    return payload;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_sre_scanner_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_internal_sre_scanner_payload_t *payload = __tinypy_sre_scanner_payload(value);
    tinypy_value_t *pattern = payload->pattern;
    tinypy_value_t *string = payload->string;

    tinypy_internal_instance_release_references(value, visit, user_data);
    payload->pattern = NULL;
    payload->string = NULL;
    if (pattern != NULL) {
        visit(pattern, user_data);
    }
    if (string != NULL) {
        visit(string, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_scanner_step(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    tinypy_bool_t condition = __tinypy_sre_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0;
    if (condition == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        condition = item->type != vm->sre_scanner_type;
    }
    if (condition) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected an SRE scanner", out_error);
        return NULL;
    }
    tinypy_internal_sre_scanner_payload_t *payload = __tinypy_sre_scanner_payload(TINYPY_TUPLE_GET(args, 0U));
    if (payload->pattern == NULL || payload->pos > payload->endpos) {
        tinypy_value_t *finished = TINYPY_RET_NONE(vm);
        return finished;
    }
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(payload->pattern);
    tinypy_value_t *match_value = __tinypy_sre_execute(pattern, payload->string, payload->pos, payload->endpos, user_data != NULL ? INT32_C(1) : INT32_C(0), out_error);

    if (match_value == NULL) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(match_value) == TINYPY_VALUE_NONE) {
        payload->pos = payload->endpos + 1U;
        return match_value;
    }
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(match_value);
    match->pos = payload->initial_pos;
    payload->pos = match->end == match->start ? match->end + 1U : match->end;
    return match_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_scanner_new(tinypy_vm_t *vm, tinypy_value_t *pattern_value, tinypy_value_t *string, size_t pos, size_t endpos) {
    tinypy_value_t *scanner = tinypy_native_instance_new(vm->sre_scanner_type);
    tinypy_internal_sre_scanner_payload_t *payload;

    if (scanner == NULL) {
        return NULL;
    }
    payload = __tinypy_sre_scanner_payload(scanner);
    TINYPY_INCREF(pattern_value);
    TINYPY_INCREF(string);
    payload->pattern = pattern_value;
    payload->string = string;
    payload->pos = pos;
    payload->initial_pos = pos;
    payload->endpos = endpos;
    return scanner;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_pattern_scanner(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t pos;
    size_t endpos;

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_message_part_t parts[] = {
            {user_data != NULL ? "finditer" : "scanner", user_data != NULL ? 8U : 7U},
            TINYPY_MESSAGE_PART_LITERAL("() takes no keyword arguments")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
        return NULL;
    }
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    if (argument_count < 2U || argument_count > 4U) {
        tinypy_internal_make_arity_error(vm, "scanner", 7U, argument_count != 0U ? argument_count - 1U : 0U, 1U, 3U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }

    tinypy_bool_t condition = __tinypy_sre_method_arguments(vm, args, kwargs, 2U, 4U, out_error) == 0;
    if (condition == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        condition = TINYPY_VALUE_KIND(item) != TINYPY_VALUE_SRE_PATTERN;
    }
    if (condition) {
        return NULL;
    }
    tinypy_value_t *pattern_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(pattern_value);
    if (__tinypy_sre_bounds(pattern, args, 1U, &pos, &endpos, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *scanner = __tinypy_sre_scanner_new(vm, pattern_value, TINYPY_TUPLE_GET(args, 1U), pos, endpos);

    if (scanner == NULL) {
        return NULL;
    }
    if (user_data == NULL) {
        return scanner;
    }
    tinypy_value_t *search = tinypy_object_get_attr_value(scanner, vm->internal_search_key, out_error);

    TINYPY_DECREF(scanner);
    if (search == NULL) {
        return NULL;
    }
    tinypy_value_t *sentinel = TINYPY_RET_NONE(vm);
    tinypy_value_t *iterator = tinypy_internal_call_iterator_new(search, sentinel, out_error);

    TINYPY_DECREF(sentinel);
    TINYPY_DECREF(search);
    return iterator;
}
//////////////////////////////////////////////////////////////////////////
/* A replacement holding a backslash is a template, not a literal: re._subx
   turns it into the finished string or into a filter over the match. */
static tinypy_value_t *__tinypy_sre_pattern_sub(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *replacement;
    tinypy_value_t *expanded;
    tinypy_value_t *rewritten;
    tinypy_value_t *result;
    tinypy_value_t *items[4];
    size_t count;
    size_t index;

    if (TINYPY_VALUE_KIND(args) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(args) < 2U || TINYPY_TUPLE_SIZE(args) > 4U) {
        tinypy_value_t *return_value_1 = __tinypy_sre_pattern_sub_expanded(function, args, kwargs, user_data, out_error);
        return return_value_1;
    }
    replacement = TINYPY_TUPLE_GET(args, 1U);
    tinypy_bool_t callable = replacement->type->call != NULL || tinypy_internal_object_has_special_key(replacement, vm->internal_special_call_key) != 0;
    if (callable != 0 || __tinypy_sre_subject_supported(replacement) == 0 || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_SRE_PATTERN) {
        tinypy_value_t *return_value_2 = __tinypy_sre_pattern_sub_expanded(function, args, kwargs, user_data, out_error);
        return return_value_2;
    }
    size_t template_size;

    if (__tinypy_sre_subject_length(vm, replacement, &template_size, out_error) == 0) {
        return NULL;
    }
    if (__tinypy_sre_template_is_literal(replacement, template_size) != 0) {
        tinypy_value_t *literal = __tinypy_sre_pattern_sub_expanded(function, args, kwargs, user_data, out_error);
        return literal;
    }
    tinypy_value_t *helper_items[2] = {TINYPY_TUPLE_GET(args, 0U), replacement};

    expanded = __tinypy_sre_call_re_helper(vm, vm->internal_subx_key, helper_items, 2U, out_error);
    if (expanded == NULL) {
        return NULL;
    }
    count = TINYPY_TUPLE_SIZE(args);
    for (index = 0U; index < count; ++index) {
        items[index] = index == 1U ? expanded : TINYPY_TUPLE_GET(args, index);
    }
    rewritten = tinypy_tuple_from_items(vm, items, count);
    result = __tinypy_sre_pattern_sub_expanded(function, rewritten, kwargs, user_data, out_error);
    TINYPY_DECREF(rewritten);
    TINYPY_DECREF(expanded);
    return result;
}
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_sre_keyword_spec_t {
    tinypy_native_function_callback_t callback;
    intptr_t mode;
    size_t parameter_offsets[4];
    size_t count;
    size_t required;
    unsigned int integers;
    tinypy_bool_t has_alias;
} tinypy_sre_keyword_spec_t;
//////////////////////////////////////////////////////////////////////////
static const tinypy_sre_keyword_spec_t __tinypy_sre_keyword_specs[] = {
    {__tinypy_sre_pattern_match_or_search, 0, {offsetof(tinypy_vm_t, internal_string_key), offsetof(tinypy_vm_t, internal_pos_key), offsetof(tinypy_vm_t, internal_endpos_key), offsetof(tinypy_vm_t, internal_pattern_key)}, 3U, 0U, 6U, TINYPY_TRUE},
    {__tinypy_sre_pattern_match_or_search, 1, {offsetof(tinypy_vm_t, internal_string_key), offsetof(tinypy_vm_t, internal_pos_key), offsetof(tinypy_vm_t, internal_endpos_key), offsetof(tinypy_vm_t, internal_pattern_key)}, 3U, 0U, 6U, TINYPY_TRUE},
    {__tinypy_sre_pattern_findall, 0, {offsetof(tinypy_vm_t, internal_string_key), offsetof(tinypy_vm_t, internal_pos_key), offsetof(tinypy_vm_t, internal_endpos_key), offsetof(tinypy_vm_t, internal_source_key)}, 3U, 0U, 6U, TINYPY_TRUE},
    {__tinypy_sre_pattern_split, 0, {offsetof(tinypy_vm_t, internal_string_key), offsetof(tinypy_vm_t, internal_maxsplit_key), offsetof(tinypy_vm_t, internal_source_key), 0U}, 2U, 0U, 2U, TINYPY_TRUE},
    {__tinypy_sre_pattern_sub, 0, {offsetof(tinypy_vm_t, internal_repl_key), offsetof(tinypy_vm_t, internal_string_key), offsetof(tinypy_vm_t, internal_count_key), 0U}, 3U, 2U, 4U, TINYPY_FALSE},
    {__tinypy_sre_pattern_sub, 1, {offsetof(tinypy_vm_t, internal_repl_key), offsetof(tinypy_vm_t, internal_string_key), offsetof(tinypy_vm_t, internal_count_key), 0U}, 3U, 2U, 4U, TINYPY_FALSE},
    {__tinypy_sre_match_groups, 0, {offsetof(tinypy_vm_t, internal_default_key), 0U, 0U, 0U}, 1U, 0U, 0U, TINYPY_FALSE},
    {__tinypy_sre_match_groupdict, 0, {offsetof(tinypy_vm_t, internal_default_key), 0U, 0U, 0U}, 1U, 0U, 0U, TINYPY_FALSE},
};
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_keyword_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    const tinypy_sre_keyword_spec_t *spec = (const tinypy_sre_keyword_spec_t *)user_data;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *items[5] = {NULL, NULL, NULL, NULL, NULL};
    tinypy_value_t *parameters[4] = {NULL, NULL, NULL, NULL};
    tinypy_value_t *alias = NULL;
    tinypy_value_t *bound;
    tinypy_value_t *result = NULL;
    size_t positional_count = TINYPY_TUPLE_SIZE(args) != 0U ? TINYPY_TUPLE_SIZE(args) - 1U : 0U;
    size_t keyword_count = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    size_t field_count = spec->count + (spec->has_alias != 0 ? 1U : 0U);
    size_t remaining_keywords = keyword_count;
    size_t index;

    if (TINYPY_TUPLE_SIZE(args) == 0U || positional_count + keyword_count > spec->count) {
        tinypy_value_t *function_name = tinypy_native_function_name(function);
        size_t count = positional_count + keyword_count;

        if (spec->has_alias != 0) {
            tinypy_value_t *actual = tinypy_integer_from_i64(vm, (int64_t)count);
            tinypy_value_t *actual_text = tinypy_object_str(actual, out_error);
            char maximum = (char)('0' + spec->count);

            TINYPY_DECREF(actual);
            if (actual_text == NULL) {
                return NULL;
            }
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_TEXT(function_name),
                TINYPY_MESSAGE_PART_LITERAL("() takes at most "),
                {&maximum, 1U},
                TINYPY_MESSAGE_PART_LITERAL(" positional arguments ("),
                TINYPY_MESSAGE_PART_TEXT(actual_text),
                TINYPY_MESSAGE_PART_LITERAL(" given)")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 6U, out_error);
            TINYPY_DECREF(actual_text);
        }
        else {
            tinypy_internal_make_arity_error(vm, (const char *)TINYPY_TEXT_BYTES(function_name), TINYPY_TEXT_BYTE_SIZE(function_name), count, spec->required, spec->count, TINYPY_ARITY_STYLE_PARSED, out_error);
        }
        return NULL;
    }
    for (index = 0U; index < field_count; ++index) {
        parameters[index] = *(tinypy_value_t **)((uint8_t *)vm + spec->parameter_offsets[index]);
    }
    items[0] = TINYPY_RET(TINYPY_TUPLE_GET(args, 0U));
    for (index = 0U; index < field_count; ++index) {
        tinypy_value_t *value = NULL;

        if (kwargs != NULL) {
            value = remaining_keywords != 0U ? tinypy_internal_constructor_keyword_optional(kwargs, parameters[index]) : NULL;
            if (value != NULL) {
                remaining_keywords -= 1U;
            }
        }
        if (index < positional_count) {
            if (value != NULL) {
                char position = (char)('1' + index);
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("Argument given by name ('"),
                    TINYPY_MESSAGE_PART_TEXT(parameters[index]),
                    TINYPY_MESSAGE_PART_LITERAL("') and position ("),
                    {&position, 1U},
                    TINYPY_MESSAGE_PART_LITERAL(")")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 5U, out_error);
                goto cleanup;
            }
            value = TINYPY_TUPLE_GET(args, index + 1U);
        }
        if (value == NULL && index < spec->required) {
            char position = (char)('1' + index);
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("Required argument '"),
                TINYPY_MESSAGE_PART_TEXT(parameters[index]),
                TINYPY_MESSAGE_PART_LITERAL("' (pos "),
                {&position, 1U},
                TINYPY_MESSAGE_PART_LITERAL(") not found")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 5U, out_error);
            goto cleanup;
        }
        if (value != NULL) {
            TINYPY_INCREF(value);
        }
        if (index < spec->count && (spec->integers & (1U << index)) != 0U) {
            int64_t integer = 0;

            if (value != NULL) {
                tinypy_bool_t converted = __tinypy_sre_argument_integer(value, &integer, out_error);

                TINYPY_DECREF(value);
                if (converted == 0) {
                    goto cleanup;
                }
            }
            else if (index == 2U && spec->integers == 6U) {
                integer = INT64_MAX;
            }
            value = tinypy_integer_from_i64(vm, integer);
        }
        if (index == spec->count) {
            alias = value;
        }
        else {
            items[index + 1U] = value;
        }
    }
    if (remaining_keywords != 0U) {
        size_t position = 0U;
        tinypy_value_t *key;
        tinypy_value_t *value;

        while (tinypy_dict_next(kwargs, &position, &key, &value) != 0) {
            tinypy_bool_t recognized = TINYPY_FALSE;

            if (TINYPY_VALUE_KIND(key) != TINYPY_VALUE_STRING) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "keywords must be strings", out_error);
                goto cleanup;
            }
            for (index = 0U; index < field_count; ++index) {
                if (TINYPY_NAME_EQ(key, parameters[index]) != 0) {
                    recognized = TINYPY_TRUE;
                    break;
                }
            }
            if (recognized == 0) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("'"),
                    TINYPY_MESSAGE_PART_TEXT(key),
                    TINYPY_MESSAGE_PART_LITERAL("' is an invalid keyword argument for this function")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
                goto cleanup;
            }
        }
    }
    if (spec->has_alias != 0) {
        if (alias != NULL) {
            if (items[1] != NULL) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("Argument given by name ('"),
                    TINYPY_MESSAGE_PART_TEXT(parameters[spec->count]),
                    TINYPY_MESSAGE_PART_LITERAL("') and position (1)")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
                goto cleanup;
            }
            items[1] = alias;
            alias = NULL;
        }
        if (items[1] == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "Required argument 'string' (pos 1) not found", out_error);
            goto cleanup;
        }
    }
    for (index = 1U; index <= spec->count; ++index) {
        if (items[index] == NULL) {
            items[index] = TINYPY_RET_NONE(vm);
        }
    }
    bound = tinypy_tuple_from_items(vm, items, spec->count + 1U);
    result = spec->callback(function, bound, NULL, (void *)spec->mode, out_error);
    TINYPY_DECREF(bound);
cleanup:
    if (alias != NULL) {
        TINYPY_DECREF(alias);
    }
    for (index = 0U; index <= spec->count; ++index) {
        if (items[index] != NULL) {
            TINYPY_DECREF(items[index]);
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static void __tinypy_sre_initialize_scanner_type(tinypy_vm_t *vm) {
    tinypy_native_type_spec_t spec;
    tinypy_value_t *const step_names[] = {vm->internal_match_key, vm->internal_search_key};
    size_t index;

    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(tinypy_internal_sre_scanner_payload_t);
    spec.has_instance_dict = TINYPY_FALSE;
    spec.has_weakrefs = TINYPY_FALSE;
    vm->sre_scanner_type = tinypy_native_type_new_key(vm->internal_sre_dot_sre_scanner_key, NULL, 0U, NULL, &spec, NULL);
    vm->sre_scanner_type->release_references = __tinypy_sre_scanner_release_references;
    vm->sre_scanner_type->traverse_references = __tinypy_sre_scanner_release_references;
    for (index = 0U; index < sizeof(step_names) / sizeof(step_names[0]); ++index) {
        tinypy_internal_type_add_method(vm->sre_scanner_type, step_names[index], __tinypy_sre_scanner_step, index == 0U ? NULL : (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_sre_module(tinypy_vm_t *vm) {
    tinypy_value_t *module = tinypy_module_new_key(vm->internal_sre_module_name);
    tinypy_value_t *name = TINYPY_RET(vm->internal_sre_module_name);
    tinypy_value_t *magic = tinypy_integer_from_i64(vm, TINYPY_SRE_MAGIC);
    tinypy_value_t *code_size = tinypy_integer_from_i64(vm, INT64_C(4));
    tinypy_value_t *max_repeat = tinypy_long_from_i64(vm, UINT32_MAX);
    tinypy_value_t *copyright = tinypy_string_from_bytes(vm, " SRE 2.2.2 Copyright (c) 1997-2002 by Secret Labs AB ", 53U);

    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_PATTERN], vm->internal_match_key, __tinypy_sre_keyword_method, (void *)&__tinypy_sre_keyword_specs[0], NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_PATTERN], vm->internal_search_key, __tinypy_sre_keyword_method, (void *)&__tinypy_sre_keyword_specs[1], NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_PATTERN], vm->internal_findall_key, __tinypy_sre_keyword_method, (void *)&__tinypy_sre_keyword_specs[2], NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_PATTERN], vm->internal_sub_key, __tinypy_sre_keyword_method, (void *)&__tinypy_sre_keyword_specs[4], NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_PATTERN], vm->internal_subn_key, __tinypy_sre_keyword_method, (void *)&__tinypy_sre_keyword_specs[5], NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_PATTERN], vm->internal_split_key, __tinypy_sre_keyword_method, (void *)&__tinypy_sre_keyword_specs[3], NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_PATTERN], vm->internal_scanner_key, __tinypy_sre_pattern_scanner, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_PATTERN], vm->internal_finditer_key, __tinypy_sre_pattern_scanner, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_MATCH], vm->internal_group_key, __tinypy_sre_match_group, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_MATCH], vm->internal_groups_key, __tinypy_sre_keyword_method, (void *)&__tinypy_sre_keyword_specs[6], NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_MATCH], vm->internal_start_key, __tinypy_sre_match_span_method, (void *)(intptr_t)0, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_MATCH], vm->internal_end_key, __tinypy_sre_match_span_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_MATCH], vm->internal_span_key, __tinypy_sre_match_span_method, (void *)(intptr_t)2, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_MATCH], vm->internal_groupdict_key, __tinypy_sre_keyword_method, (void *)&__tinypy_sre_keyword_specs[7], NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SRE_MATCH], vm->internal_expand_key, __tinypy_sre_match_expand, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    __tinypy_sre_initialize_scanner_type(vm);
    tinypy_module_add_value_key(module, vm->internal_special_name_key, name);
    tinypy_module_add_value_key(module, vm->internal_magic_key, magic);
    tinypy_module_add_value_key(module, vm->internal_codesize_key, code_size);
    tinypy_module_add_value_key(module, vm->internal_maxrepeat_key, max_repeat);
    tinypy_module_add_value_key(module, vm->internal_copyright_key, copyright);
    tinypy_internal_module_add_function(module, vm->internal_compile_key, __tinypy_sre_compile, NULL, NULL);
    tinypy_internal_module_add_function(module, vm->internal_getlower_key, __tinypy_sre_getlower, NULL, NULL);
    tinypy_internal_module_add_function(module, vm->internal_getcodesize_key, __tinypy_sre_getcodesize, NULL, NULL);
    TINYPY_DECREF(copyright);
    TINYPY_DECREF(max_repeat);
    TINYPY_DECREF(code_size);
    TINYPY_DECREF(magic);
    TINYPY_DECREF(name);
    tinypy_internal_register_module(vm, vm->internal_sre_module_name, module);
    TINYPY_DECREF(module);
}
