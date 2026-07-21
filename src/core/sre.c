#include "internal.h"

#include <assert.h>
#include <string.h>

#define TINYPY_SRE_MAGIC UINT32_C(20031017)
#define TINYPY_SRE_MAXREPEAT UINT32_MAX
#define TINYPY_SRE_MAX_MARKS 200U

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

typedef struct tinypy_sre_state_t {
    tinypy_vm_t *vm;
    tinypy_sre_pattern_object_t *pattern;
    const unsigned char *bytes;
    size_t size;
    size_t beginning;
    size_t end;
    size_t recursion_depth;
    int32_t invalid_code;
} tinypy_sre_state_t;

static int32_t __tinypy_sre_match_code(tinypy_sre_state_t *state, size_t pc, size_t stop, size_t *position, size_t *marks, ptrdiff_t *lastindex);

//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_sre_lower(uint32_t character) {
    if (character >= (uint32_t)'A' && character <= (uint32_t)'Z') {
        return character + (uint32_t)('a' - 'A');
    }
    return character;
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_is_digit(uint32_t character) {
    return character >= (uint32_t)'0' && character <= (uint32_t)'9' ? INT32_C(1) : INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_is_space(uint32_t character) {
    return character == (uint32_t)' ' || character == (uint32_t)'\t' || character == (uint32_t)'\n' || character == (uint32_t)'\r' || character == (uint32_t)'\v' || character == (uint32_t)'\f' ? INT32_C(1) : INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_is_word(uint32_t character) {
    return __tinypy_sre_is_digit(character) != 0 || (character >= (uint32_t)'a' && character <= (uint32_t)'z') || (character >= (uint32_t)'A' && character <= (uint32_t)'Z') || character == (uint32_t)'_' ? INT32_C(1) : INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_is_linebreak(uint32_t character) {
    return character == (uint32_t)'\n' ? INT32_C(1) : INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_category(uint32_t category, uint32_t character) {
    switch ((tinypy_sre_category_e)category) {
    case TINYPY_SRE_CATEGORY_DIGIT:
    case TINYPY_SRE_CATEGORY_UNI_DIGIT:
        return __tinypy_sre_is_digit(character);
    case TINYPY_SRE_CATEGORY_NOT_DIGIT:
    case TINYPY_SRE_CATEGORY_UNI_NOT_DIGIT:
        return __tinypy_sre_is_digit(character) == 0;
    case TINYPY_SRE_CATEGORY_SPACE:
    case TINYPY_SRE_CATEGORY_UNI_SPACE:
        return __tinypy_sre_is_space(character);
    case TINYPY_SRE_CATEGORY_NOT_SPACE:
    case TINYPY_SRE_CATEGORY_UNI_NOT_SPACE:
        return __tinypy_sre_is_space(character) == 0;
    case TINYPY_SRE_CATEGORY_WORD:
    case TINYPY_SRE_CATEGORY_LOC_WORD:
    case TINYPY_SRE_CATEGORY_UNI_WORD:
        return __tinypy_sre_is_word(character);
    case TINYPY_SRE_CATEGORY_NOT_WORD:
    case TINYPY_SRE_CATEGORY_LOC_NOT_WORD:
    case TINYPY_SRE_CATEGORY_UNI_NOT_WORD:
        return __tinypy_sre_is_word(character) == 0;
    case TINYPY_SRE_CATEGORY_LINEBREAK:
    case TINYPY_SRE_CATEGORY_UNI_LINEBREAK:
        return __tinypy_sre_is_linebreak(character);
    case TINYPY_SRE_CATEGORY_NOT_LINEBREAK:
    case TINYPY_SRE_CATEGORY_UNI_NOT_LINEBREAK:
        return __tinypy_sre_is_linebreak(character) == 0;
    default:
        return INT32_C(0);
    }
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_at(const tinypy_sre_state_t *state, size_t position, uint32_t at) {
    int32_t previous_word;
    int32_t current_word;

    switch ((tinypy_sre_at_e)at) {
    case TINYPY_SRE_AT_BEGINNING:
    case TINYPY_SRE_AT_BEGINNING_STRING:
        return position == state->beginning;
    case TINYPY_SRE_AT_BEGINNING_LINE:
        return position == state->beginning || __tinypy_sre_is_linebreak(state->bytes[position - 1U]) != 0;
    case TINYPY_SRE_AT_END:
        return position == state->end || (position + 1U == state->end && __tinypy_sre_is_linebreak(state->bytes[position]) != 0);
    case TINYPY_SRE_AT_END_LINE:
        return position == state->end || __tinypy_sre_is_linebreak(state->bytes[position]) != 0;
    case TINYPY_SRE_AT_END_STRING:
        return position == state->end;
    case TINYPY_SRE_AT_BOUNDARY:
    case TINYPY_SRE_AT_LOC_BOUNDARY:
    case TINYPY_SRE_AT_UNI_BOUNDARY:
    case TINYPY_SRE_AT_NON_BOUNDARY:
    case TINYPY_SRE_AT_LOC_NON_BOUNDARY:
    case TINYPY_SRE_AT_UNI_NON_BOUNDARY:
        previous_word = position > state->beginning ? __tinypy_sre_is_word(state->bytes[position - 1U]) : INT32_C(0);
        current_word = position < state->end ? __tinypy_sre_is_word(state->bytes[position]) : INT32_C(0);
        if (at == TINYPY_SRE_AT_BOUNDARY || at == TINYPY_SRE_AT_LOC_BOUNDARY || at == TINYPY_SRE_AT_UNI_BOUNDARY) {
            return previous_word != current_word;
        }
        return previous_word == current_word;
    default:
        return INT32_C(0);
    }
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_charset(const tinypy_sre_pattern_object_t *pattern, size_t pc, uint32_t character) {
    int32_t accepted = INT32_C(1);

    while (pc < pattern->code_size) {
        uint32_t opcode = pattern->code[pc++];

        switch ((tinypy_sre_opcode_e)opcode) {
        case TINYPY_SRE_OP_FAILURE:
            return accepted == 0;
        case TINYPY_SRE_OP_LITERAL:
            if (pc >= pattern->code_size) {
                return INT32_C(0);
            }
            if (character == pattern->code[pc]) {
                return accepted;
            }
            pc += 1U;
            break;
        case TINYPY_SRE_OP_CATEGORY:
            if (pc >= pattern->code_size) {
                return INT32_C(0);
            }
            if (__tinypy_sre_category(pattern->code[pc], character) != 0) {
                return accepted;
            }
            pc += 1U;
            break;
        case TINYPY_SRE_OP_CHARSET:
            if (pc + 8U > pattern->code_size) {
                return INT32_C(0);
            }
            if (character < UINT32_C(256) && (pattern->code[pc + (character >> 5U)] & (UINT32_C(1) << (character & UINT32_C(31)))) != 0U) {
                return accepted;
            }
            pc += 8U;
            break;
        case TINYPY_SRE_OP_RANGE:
            if (pc + 2U > pattern->code_size) {
                return INT32_C(0);
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
            const unsigned char *block_indices;
            size_t block;

            if (pc >= pattern->code_size) {
                return INT32_C(0);
            }
            block_count = pattern->code[pc++];
            if (pc + 64U + block_count * 8U > pattern->code_size) {
                return INT32_C(0);
            }
            block_indices = (const unsigned char *)(pattern->code + pc);
            block = character <= UINT32_C(65535) ? block_indices[character >> 8U] : SIZE_MAX;
            pc += 64U;
            if (block != SIZE_MAX && block < block_count && (pattern->code[pc + block * 8U + ((character & UINT32_C(255)) >> 5U)] & (UINT32_C(1) << (character & UINT32_C(31)))) != 0U) {
                return accepted;
            }
            pc += block_count * 8U;
        }
        break;
        default:
            return INT32_C(0);
        }
    }
    return INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_sre_copy_marks(size_t *target, const size_t *source, size_t count) {
    if (count != 0U) {
        (void)memcpy(target, source, count * sizeof(*target));
    }
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_match_one(tinypy_sre_state_t *state, size_t pc, size_t position, size_t *out_position) {
    uint32_t opcode;
    uint32_t character;

    if (position >= state->end || pc >= state->pattern->code_size) {
        return INT32_C(0);
    }
    opcode = state->pattern->code[pc];
    character = state->bytes[position];
    switch ((tinypy_sre_opcode_e)opcode) {
    case TINYPY_SRE_OP_LITERAL:
        if (pc + 1U >= state->pattern->code_size || character != state->pattern->code[pc + 1U]) {
            return INT32_C(0);
        }
        break;
    case TINYPY_SRE_OP_LITERAL_IGNORE:
        if (pc + 1U >= state->pattern->code_size || __tinypy_sre_lower(character) != __tinypy_sre_lower(state->pattern->code[pc + 1U])) {
            return INT32_C(0);
        }
        break;
    case TINYPY_SRE_OP_NOT_LITERAL:
        if (pc + 1U >= state->pattern->code_size || character == state->pattern->code[pc + 1U]) {
            return INT32_C(0);
        }
        break;
    case TINYPY_SRE_OP_NOT_LITERAL_IGNORE:
        if (pc + 1U >= state->pattern->code_size || __tinypy_sre_lower(character) == __tinypy_sre_lower(state->pattern->code[pc + 1U])) {
            return INT32_C(0);
        }
        break;
    case TINYPY_SRE_OP_ANY:
        if (__tinypy_sre_is_linebreak(character) != 0) {
            return INT32_C(0);
        }
        break;
    case TINYPY_SRE_OP_ANY_ALL:
        break;
    case TINYPY_SRE_OP_IN:
        if (pc + 1U >= state->pattern->code_size || __tinypy_sre_charset(state->pattern, pc + 2U, character) == 0) {
            return INT32_C(0);
        }
        break;
    case TINYPY_SRE_OP_IN_IGNORE: {
        uint32_t lowered_character;

        if (pc + 1U >= state->pattern->code_size) {
            return INT32_C(0);
        }
        lowered_character = __tinypy_sre_lower(character);
        if (__tinypy_sre_charset(state->pattern, pc + 2U, lowered_character) == 0) {
            return INT32_C(0);
        }
        break;
    }
    case TINYPY_SRE_OP_CATEGORY:
        if (pc + 1U >= state->pattern->code_size || __tinypy_sre_category(state->pattern->code[pc + 1U], character) == 0) {
            return INT32_C(0);
        }
        break;
    default:
        return INT32_C(0);
    }
    *out_position = position + 1U;
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_match_repeat(tinypy_sre_state_t *state, size_t pc, size_t stop, size_t *position, size_t *marks, ptrdiff_t *lastindex) {
    tinypy_sre_pattern_object_t *pattern = state->pattern;
    size_t skip = pattern->code[pc + 1U];
    size_t minimum = pattern->code[pc + 2U];
    size_t maximum = pattern->code[pc + 3U] == TINYPY_SRE_MAXREPEAT ? state->end - *position + 1U : pattern->code[pc + 3U];
    size_t until = pc + 1U + skip;
    size_t capacity = maximum + 1U;
    size_t *positions;
    size_t *saved_marks;
    ptrdiff_t *saved_lastindex;
    size_t count = 0U;
    size_t index;
    int32_t greedy;

    if (until >= pattern->code_size || capacity > SIZE_MAX / sizeof(*positions) || pattern->groups * 2U > TINYPY_SRE_MAX_MARKS) {
        return INT32_C(0);
    }
    greedy = pattern->code[until] == TINYPY_SRE_OP_MAX_UNTIL ? INT32_C(1) : INT32_C(0);
    if (greedy == 0 && pattern->code[until] != TINYPY_SRE_OP_MIN_UNTIL) {
        return INT32_C(0);
    }
    positions = (size_t *)tinypy_internal_vm_allocate(state->vm, capacity * sizeof(*positions), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    saved_lastindex = (ptrdiff_t *)tinypy_internal_vm_allocate(state->vm, capacity * sizeof(*saved_lastindex), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    saved_marks = pattern->groups != 0U ? (size_t *)tinypy_internal_vm_allocate(state->vm, capacity * pattern->groups * 2U * sizeof(*saved_marks), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY) : NULL;
    positions[0] = *position;
    saved_lastindex[0] = *lastindex;
    if (pattern->groups != 0U) {
        __tinypy_sre_copy_marks(saved_marks, marks, pattern->groups * 2U);
    }
    while (count < maximum) {
        size_t next_position = positions[count];
        size_t next_marks[TINYPY_SRE_MAX_MARKS];
        ptrdiff_t next_lastindex = saved_lastindex[count];

        if (pattern->groups != 0U) {
            __tinypy_sre_copy_marks(next_marks, saved_marks + count * pattern->groups * 2U, pattern->groups * 2U);
        }
        if (__tinypy_sre_match_code(state, pc + 4U, until, &next_position, next_marks, &next_lastindex) == 0 || next_position == positions[count]) {
            break;
        }
        count += 1U;
        positions[count] = next_position;
        saved_lastindex[count] = next_lastindex;
        if (pattern->groups != 0U) {
            __tinypy_sre_copy_marks(saved_marks + count * pattern->groups * 2U, next_marks, pattern->groups * 2U);
        }
    }
    if (count >= minimum) {
        size_t attempts = count - minimum + 1U;

        for (index = 0U; index < attempts; index += 1U) {
            size_t selected = greedy != 0 ? count - index : minimum + index;
            size_t trial_position = positions[selected];
            size_t trial_marks[TINYPY_SRE_MAX_MARKS];
            ptrdiff_t trial_lastindex = saved_lastindex[selected];

            if (pattern->groups != 0U) {
                __tinypy_sre_copy_marks(trial_marks, saved_marks + selected * pattern->groups * 2U, pattern->groups * 2U);
            }
            if (__tinypy_sre_match_code(state, until + 1U, stop, &trial_position, trial_marks, &trial_lastindex) != 0) {
                *position = trial_position;
                *lastindex = trial_lastindex;
                if (pattern->groups != 0U) {
                    __tinypy_sre_copy_marks(marks, trial_marks, pattern->groups * 2U);
                }
                if (saved_marks != NULL) {
                    tinypy_internal_vm_deallocate(state->vm, saved_marks, capacity * pattern->groups * 2U * sizeof(*saved_marks), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
                }
                tinypy_internal_vm_deallocate(state->vm, saved_lastindex, capacity * sizeof(*saved_lastindex), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
                tinypy_internal_vm_deallocate(state->vm, positions, capacity * sizeof(*positions), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
                return INT32_C(1);
            }
        }
    }
    if (saved_marks != NULL) {
        tinypy_internal_vm_deallocate(state->vm, saved_marks, capacity * pattern->groups * 2U * sizeof(*saved_marks), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    tinypy_internal_vm_deallocate(state->vm, saved_lastindex, capacity * sizeof(*saved_lastindex), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    tinypy_internal_vm_deallocate(state->vm, positions, capacity * sizeof(*positions), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_match_code(tinypy_sre_state_t *state, size_t pc, size_t stop, size_t *position, size_t *marks, ptrdiff_t *lastindex) {
    tinypy_sre_pattern_object_t *pattern = state->pattern;

    assert(state->recursion_depth < 1000U);
    state->recursion_depth += 1U;
    while (pc < pattern->code_size) {
        uint32_t opcode;

        if (pc == stop) {
            state->recursion_depth -= 1U;
            return INT32_C(1);
        }
        opcode = pattern->code[pc];
        switch ((tinypy_sre_opcode_e)opcode) {
        case TINYPY_SRE_OP_SUCCESS:
            state->recursion_depth -= 1U;
            return INT32_C(1);
        case TINYPY_SRE_OP_FAILURE:
            state->recursion_depth -= 1U;
            return INT32_C(0);
        case TINYPY_SRE_OP_INFO:
        case TINYPY_SRE_OP_JUMP:
            if (pc + 1U >= pattern->code_size || pattern->code[pc + 1U] > SIZE_MAX - pc - 1U) {
                goto invalid;
            }
            pc += 1U + pattern->code[pc + 1U];
            break;
        case TINYPY_SRE_OP_LITERAL:
        case TINYPY_SRE_OP_LITERAL_IGNORE:
        case TINYPY_SRE_OP_NOT_LITERAL:
        case TINYPY_SRE_OP_NOT_LITERAL_IGNORE:
        case TINYPY_SRE_OP_ANY:
        case TINYPY_SRE_OP_ANY_ALL:
        case TINYPY_SRE_OP_CATEGORY: {
            size_t next_position;

            if (__tinypy_sre_match_one(state, pc, *position, &next_position) == 0) {
                state->recursion_depth -= 1U;
                return INT32_C(0);
            }
            *position = next_position;
            pc += opcode == TINYPY_SRE_OP_ANY || opcode == TINYPY_SRE_OP_ANY_ALL ? 1U : 2U;
        }
        break;
        case TINYPY_SRE_OP_IN:
        case TINYPY_SRE_OP_IN_IGNORE: {
            size_t next_position;
            size_t skip;

            if (pc + 1U >= pattern->code_size || __tinypy_sre_match_one(state, pc, *position, &next_position) == 0) {
                state->recursion_depth -= 1U;
                return INT32_C(0);
            }
            skip = pattern->code[pc + 1U];
            if (skip > SIZE_MAX - pc - 1U) {
                goto invalid;
            }
            *position = next_position;
            pc += 1U + skip;
        }
        break;
        case TINYPY_SRE_OP_MARK: {
            size_t mark;

            if (pc + 1U >= pattern->code_size) {
                goto invalid;
            }
            mark = pattern->code[pc + 1U];
            if (mark >= pattern->groups * 2U) {
                goto invalid;
            }
            marks[mark] = *position;
            if ((mark & 1U) != 0U) {
                *lastindex = (ptrdiff_t)(mark / 2U + 1U);
            }
            pc += 2U;
        }
        break;
        case TINYPY_SRE_OP_AT:
            if (pc + 1U >= pattern->code_size) {
                goto invalid;
            }
            if (__tinypy_sre_at(state, *position, pattern->code[pc + 1U]) == 0) {
                state->recursion_depth -= 1U;
                return INT32_C(0);
            }
            pc += 2U;
            break;
        case TINYPY_SRE_OP_BRANCH: {
            size_t branch = pc + 1U;

            while (branch < pattern->code_size && pattern->code[branch] != 0U) {
                size_t trial_position = *position;
                size_t trial_marks[TINYPY_SRE_MAX_MARKS];
                ptrdiff_t trial_lastindex = *lastindex;
                size_t skip = pattern->code[branch];

                if (skip > SIZE_MAX - branch) {
                    goto invalid;
                }
                if (pattern->groups != 0U) {
                    __tinypy_sre_copy_marks(trial_marks, marks, pattern->groups * 2U);
                }
                if (__tinypy_sre_match_code(state, branch + 1U, stop, &trial_position, trial_marks, &trial_lastindex) != 0) {
                    *position = trial_position;
                    *lastindex = trial_lastindex;
                    if (pattern->groups != 0U) {
                        __tinypy_sre_copy_marks(marks, trial_marks, pattern->groups * 2U);
                    }
                    state->recursion_depth -= 1U;
                    return INT32_C(1);
                }
                branch += skip;
            }
            state->recursion_depth -= 1U;
            return INT32_C(0);
        }
        case TINYPY_SRE_OP_REPEAT_ONE:
        case TINYPY_SRE_OP_MIN_REPEAT_ONE: {
            size_t skip;
            size_t minimum;
            size_t maximum;
            size_t count = 0U;
            size_t cursor = *position;
            size_t tail;
            size_t attempts;
            size_t index;

            if (pc + 3U >= pattern->code_size) {
                goto invalid;
            }
            skip = pattern->code[pc + 1U];
            minimum = pattern->code[pc + 2U];
            maximum = pattern->code[pc + 3U] == TINYPY_SRE_MAXREPEAT ? state->end - cursor : pattern->code[pc + 3U];
            if (skip > SIZE_MAX - pc - 1U) {
                goto invalid;
            }
            tail = pc + 1U + skip;
            while (count < maximum) {
                size_t next_position;

                if (__tinypy_sre_match_one(state, pc + 4U, cursor, &next_position) == 0) {
                    break;
                }
                cursor = next_position;
                count += 1U;
            }
            if (count < minimum) {
                state->recursion_depth -= 1U;
                return INT32_C(0);
            }
            attempts = count - minimum + 1U;
            for (index = 0U; index < attempts; index += 1U) {
                size_t selected = opcode == TINYPY_SRE_OP_REPEAT_ONE ? count - index : minimum + index;
                size_t trial_position = *position + selected;
                size_t trial_marks[TINYPY_SRE_MAX_MARKS];
                ptrdiff_t trial_lastindex = *lastindex;

                if (pattern->groups != 0U) {
                    __tinypy_sre_copy_marks(trial_marks, marks, pattern->groups * 2U);
                }
                if (__tinypy_sre_match_code(state, tail, stop, &trial_position, trial_marks, &trial_lastindex) != 0) {
                    *position = trial_position;
                    *lastindex = trial_lastindex;
                    if (pattern->groups != 0U) {
                        __tinypy_sre_copy_marks(marks, trial_marks, pattern->groups * 2U);
                    }
                    state->recursion_depth -= 1U;
                    return INT32_C(1);
                }
            }
            state->recursion_depth -= 1U;
            return INT32_C(0);
        }
        case TINYPY_SRE_OP_REPEAT: {
            int32_t matched;

            if (pc + 3U >= pattern->code_size) {
                goto invalid;
            }
            matched = __tinypy_sre_match_repeat(state, pc, stop, position, marks, lastindex);
            state->recursion_depth -= 1U;
            return matched;
        }
        case TINYPY_SRE_OP_GROUPREF:
        case TINYPY_SRE_OP_GROUPREF_IGNORE: {
            size_t group;
            size_t mark;
            size_t source;
            size_t source_end;

            if (pc + 1U >= pattern->code_size) {
                goto invalid;
            }
            group = pattern->code[pc + 1U];
            mark = group * 2U;
            if (mark + 1U >= pattern->groups * 2U || marks[mark] == SIZE_MAX || marks[mark + 1U] == SIZE_MAX) {
                state->recursion_depth -= 1U;
                return INT32_C(0);
            }
            source = marks[mark];
            source_end = marks[mark + 1U];
            while (source < source_end) {
                uint32_t left;
                uint32_t right;

                if (*position >= state->end) {
                    state->recursion_depth -= 1U;
                    return INT32_C(0);
                }
                left = state->bytes[source++];
                right = state->bytes[*position];
                if (opcode == TINYPY_SRE_OP_GROUPREF_IGNORE) {
                    left = __tinypy_sre_lower(left);
                    right = __tinypy_sre_lower(right);
                }
                if (left != right) {
                    state->recursion_depth -= 1U;
                    return INT32_C(0);
                }
                *position += 1U;
            }
            pc += 2U;
        }
        break;
        case TINYPY_SRE_OP_GROUPREF_EXISTS: {
            size_t group;
            size_t mark;

            if (pc + 2U >= pattern->code_size) {
                goto invalid;
            }
            group = pattern->code[pc + 1U];
            mark = group * 2U;
            if (mark + 1U >= pattern->groups * 2U || marks[mark] == SIZE_MAX || marks[mark + 1U] == SIZE_MAX) {
                pc += 1U + pattern->code[pc + 2U];
            }
            else {
                pc += 3U;
            }
        }
        break;
        case TINYPY_SRE_OP_ASSERT:
        case TINYPY_SRE_OP_ASSERT_NOT: {
            size_t skip;
            size_t back;
            size_t trial_position;
            size_t trial_marks[TINYPY_SRE_MAX_MARKS];
            ptrdiff_t trial_lastindex = *lastindex;
            int32_t matched;

            if (pc + 2U >= pattern->code_size) {
                goto invalid;
            }
            skip = pattern->code[pc + 1U];
            back = pattern->code[pc + 2U];
            if (back > *position - state->beginning || skip > SIZE_MAX - pc - 1U) {
                matched = INT32_C(0);
            }
            else {
                trial_position = *position - back;
                if (pattern->groups != 0U) {
                    __tinypy_sre_copy_marks(trial_marks, marks, pattern->groups * 2U);
                }
                matched = __tinypy_sre_match_code(state, pc + 3U, pc + 1U + skip, &trial_position, trial_marks, &trial_lastindex);
            }
            if ((opcode == TINYPY_SRE_OP_ASSERT && matched == 0) || (opcode == TINYPY_SRE_OP_ASSERT_NOT && matched != 0)) {
                state->recursion_depth -= 1U;
                return INT32_C(0);
            }
            if (opcode == TINYPY_SRE_OP_ASSERT && matched != 0 && pattern->groups != 0U) {
                __tinypy_sre_copy_marks(marks, trial_marks, pattern->groups * 2U);
                *lastindex = trial_lastindex;
            }
            pc += 1U + skip;
        }
        break;
        default:
            goto invalid;
        }
    }
invalid:
    state->invalid_code = INT32_C(1);
    state->recursion_depth -= 1U;
    return INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_integer(tinypy_value_t *value, int64_t *out_value) {
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return INT32_C(1);
    }
    if (kind == TINYPY_VALUE_LONG && TINYPY_LONG_DIGIT_COUNT(value) <= 4U) {
        *out_value = tinypy_long_as_i64(value);
        return INT32_C(1);
    }
    return INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_text_slice(tinypy_vm_t *vm, tinypy_value_t *text, size_t start, size_t end) {
    const unsigned char *bytes = tinypy_internal_text_bytes(text);

    assert(start <= end);
    assert(end <= tinypy_internal_text_byte_size(text));
    if (tinypy_internal_value_kind(text) == TINYPY_VALUE_UNICODE) {
        return tinypy_unicode_from_utf8(vm, (const char *)bytes + start, end - start);
    }
    return tinypy_string_from_bytes(vm, bytes + start, end - start);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_empty_like(tinypy_vm_t *vm, tinypy_value_t *text) {
    return tinypy_internal_value_kind(text) == TINYPY_VALUE_UNICODE ? tinypy_unicode_from_utf8(vm, NULL, 0U) : tinypy_string_from_bytes(vm, NULL, 0U);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_sre_pattern_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(value);

    visit(pattern->pattern, user_data);
    visit(pattern->groupindex, user_data);
    visit(pattern->indexgroup, user_data);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_sre_pattern_destroy(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(value);

    if (pattern->code != NULL) {
        tinypy_internal_vm_deallocate(vm, pattern->code, pattern->code_size * sizeof(*pattern->code), (uint32_t)TINYPY_ALLOC_TAG_SRE_DATA);
    }
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_sre_match_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(value);

    visit(match->pattern, user_data);
    visit(match->string, user_data);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_sre_match_destroy(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(value);

    if (match->marks != NULL) {
        tinypy_internal_vm_deallocate(vm, match->marks, match->mark_count * sizeof(*match->marks), (uint32_t)TINYPY_ALLOC_TAG_SRE_DATA);
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
    tinypy_retain(match->pattern);
    tinypy_retain(string);
    if (match->mark_count != 0U) {
        match->marks = (size_t *)tinypy_internal_vm_allocate(state->vm, match->mark_count * sizeof(*match->marks), (uint32_t)TINYPY_ALLOC_TAG_SRE_DATA);
        __tinypy_sre_copy_marks(match->marks, marks, match->mark_count);
    }
    return &match->base;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_execute(tinypy_sre_pattern_object_t *pattern, tinypy_value_t *string, size_t pos, size_t endpos, int32_t search, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(&pattern->base);
    tinypy_sre_state_t state;
    size_t string_size;
    size_t candidate;

    if (tinypy_internal_value_kind(string) != TINYPY_VALUE_STRING && tinypy_internal_value_kind(string) != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected string or buffer", out_error);
        return NULL;
    }
    string_size = tinypy_internal_text_byte_size(string);
    if (pos > string_size) {
        pos = string_size;
    }
    if (endpos > string_size) {
        endpos = string_size;
    }
    if (endpos < pos) {
        endpos = pos;
    }
    (void)memset(&state, 0, sizeof(state));
    state.vm = vm;
    state.pattern = pattern;
    state.bytes = tinypy_internal_text_bytes(string);
    state.size = string_size;
    state.beginning = 0U;
    state.end = endpos;
    for (candidate = pos; candidate <= endpos; candidate += 1U) {
        size_t marks[TINYPY_SRE_MAX_MARKS];
        size_t matched_end = candidate;
        ptrdiff_t lastindex = -1;
        size_t index;

        for (index = 0U; index < pattern->groups * 2U; index += 1U) {
            marks[index] = SIZE_MAX;
        }
        state.invalid_code = INT32_C(0);
        if (__tinypy_sre_match_code(&state, 0U, SIZE_MAX, &matched_end, marks, &lastindex) != 0) {
            return __tinypy_sre_match_new(&state, string, pos, endpos, candidate, matched_end, marks, lastindex);
        }
        if (state.invalid_code != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "invalid SRE bytecode", out_error);
            return NULL;
        }
        if (search == 0 || candidate == endpos) {
            break;
        }
    }
    return tinypy_none_get(vm);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = tinypy_tuple_size(args);

    if ((kwargs != NULL && tinypy_dict_size(kwargs) != 0U) || count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "SRE method received invalid arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_bounds(tinypy_sre_pattern_object_t *pattern, tinypy_value_t *args, size_t string_index, size_t *out_pos, size_t *out_endpos, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(&pattern->base);
    tinypy_value_t *string = tinypy_tuple_get(args, string_index);
    size_t size;
    int64_t value;

    if (tinypy_internal_value_kind(string) != TINYPY_VALUE_STRING && tinypy_internal_value_kind(string) != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected string or buffer", out_error);
        return INT32_C(0);
    }
    size = tinypy_internal_text_byte_size(string);
    *out_pos = 0U;
    *out_endpos = size;
    if (tinypy_tuple_size(args) > string_index + 1U) {
        tinypy_value_t *item = tinypy_tuple_get(args, string_index + 1U);
        if (__tinypy_sre_integer(item, &value) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice indices must be integers", out_error);
            return INT32_C(0);
        }
        *out_pos = value < 0 ? 0U : ((uint64_t)value > size ? size : (size_t)value);
    }
    if (tinypy_tuple_size(args) > string_index + 2U) {
        tinypy_value_t *item = tinypy_tuple_get(args, string_index + 2U);
        if (__tinypy_sre_integer(item, &value) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice indices must be integers", out_error);
            return INT32_C(0);
        }
        *out_endpos = value < 0 ? 0U : ((uint64_t)value > size ? size : (size_t)value);
    }
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_pattern_match_or_search(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_sre_pattern_object_t *pattern;
    size_t pos;
    size_t endpos;

    int condition = __tinypy_sre_method_arguments(vm, args, kwargs, 2U, 4U, out_error) == 0;
    if (condition == 0) {
        tinypy_value_t *item_2 = tinypy_tuple_get(args, 0U);
        condition = tinypy_internal_value_kind(item_2) != TINYPY_VALUE_SRE_PATTERN;
    }
    if (condition) {
        return NULL;
    }
    pattern = TINYPY_SRE_PATTERN_OBJECT(tinypy_tuple_get(args, 0U));
    if (__tinypy_sre_bounds(pattern, args, 1U, &pos, &endpos, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = tinypy_tuple_get(args, 1U);
    return __tinypy_sre_execute(pattern, item, pos, endpos, user_data != NULL ? INT32_C(1) : INT32_C(0), out_error);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_match_group_index(tinypy_sre_match_object_t *match, tinypy_value_t *index_value, size_t *out_index, tinypy_error_t **out_error) {
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(match->pattern);
    tinypy_vm_t *vm = tinypy_internal_value_vm(&match->base);
    int64_t index;

    if (__tinypy_sre_integer(index_value, &index) == 0) {
        int condition_2 = tinypy_dict_contains(pattern->groupindex, index_value) == 0;
        if (condition_2 == 0) {
            tinypy_value_t *value = tinypy_dict_get(pattern->groupindex, index_value);
            condition_2 = __tinypy_sre_integer(value, &index) == 0;
        }
        if (condition_2) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "no such group", out_error);
            return INT32_C(0);
        }
    }
    if (index < 0 || (uint64_t)index > pattern->groups) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "no such group", out_error);
        return INT32_C(0);
    }
    *out_index = (size_t)index;
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_match_group_value(tinypy_sre_match_object_t *match, size_t index, tinypy_value_t *default_value) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(&match->base);
    size_t start;
    size_t end;

    if (index == 0U) {
        return __tinypy_sre_text_slice(vm, match->string, match->start, match->end);
    }
    start = match->marks[(index - 1U) * 2U];
    end = match->marks[(index - 1U) * 2U + 1U];
    if (start == SIZE_MAX || end == SIZE_MAX) {
        tinypy_retain(default_value);
        return default_value;
    }
    return __tinypy_sre_text_slice(vm, match->string, start, end);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_match_group(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_sre_match_object_t *match;
    tinypy_value_t *none;
    size_t count;

    (void)user_data;
    int condition_3 = __tinypy_sre_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, out_error) == 0;
    if (condition_3 == 0) {
        tinypy_value_t *item_2 = tinypy_tuple_get(args, 0U);
        condition_3 = tinypy_internal_value_kind(item_2) != TINYPY_VALUE_SRE_MATCH;
    }
    if (condition_3) {
        return NULL;
    }
    match = TINYPY_SRE_MATCH_OBJECT(tinypy_tuple_get(args, 0U));
    count = tinypy_tuple_size(args) - 1U;
    none = tinypy_none_get(vm);
    if (count <= 1U) {
        size_t index = 0U;
        tinypy_value_t *result;

        int condition_4 = count == 1U;
        if (condition_4 != 0) {
            tinypy_value_t *item_2 = tinypy_tuple_get(args, 1U);
            condition_4 = __tinypy_sre_match_group_index(match, item_2, &index, out_error) == 0;
        }
        if (condition_4) {
            tinypy_release(none);
            return NULL;
        }
        result = __tinypy_sre_match_group_value(match, index, none);
        tinypy_release(none);
        return result;
    } {
        tinypy_value_t **items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, count * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        tinypy_value_t *result;
        size_t item_index;

        for (item_index = 0U; item_index < count; item_index += 1U) {
            size_t group;

            tinypy_value_t *item = tinypy_tuple_get(args, item_index + 1U);
            if (__tinypy_sre_match_group_index(match, item, &group, out_error) == 0) {
                while (item_index != 0U) {
                    tinypy_release(items[--item_index]);
                }
                tinypy_internal_vm_deallocate(vm, items, count * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
                tinypy_release(none);
                return NULL;
            }
            items[item_index] = __tinypy_sre_match_group_value(match, group, none);
        }
        result = tinypy_tuple_from_items(vm, items, count);
        for (item_index = 0U; item_index < count; item_index += 1U) {
            tinypy_release(items[item_index]);
        }
        tinypy_internal_vm_deallocate(vm, items, count * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        tinypy_release(none);
        return result;
    }
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_match_groups(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_sre_match_object_t *match;
    tinypy_sre_pattern_object_t *pattern;
    tinypy_value_t *default_value;
    tinypy_value_t **items;
    tinypy_value_t *result;
    size_t index;

    (void)user_data;
    int condition_5 = __tinypy_sre_method_arguments(vm, args, kwargs, 1U, 2U, out_error) == 0;
    if (condition_5 == 0) {
        tinypy_value_t *item = tinypy_tuple_get(args, 0U);
        condition_5 = tinypy_internal_value_kind(item) != TINYPY_VALUE_SRE_MATCH;
    }
    if (condition_5) {
        return NULL;
    }
    match = TINYPY_SRE_MATCH_OBJECT(tinypy_tuple_get(args, 0U));
    pattern = TINYPY_SRE_PATTERN_OBJECT(match->pattern);
    default_value = tinypy_tuple_size(args) == 2U ? tinypy_tuple_get(args, 1U) : &vm->none_object.base;
    if (pattern->groups == 0U) {
        return tinypy_tuple_from_items(vm, NULL, 0U);
    }
    items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, pattern->groups * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (index = 0U; index < pattern->groups; index += 1U) {
        items[index] = __tinypy_sre_match_group_value(match, index + 1U, default_value);
    }
    result = tinypy_tuple_from_items(vm, items, pattern->groups);
    for (index = 0U; index < pattern->groups; index += 1U) {
        tinypy_release(items[index]);
    }
    tinypy_internal_vm_deallocate(vm, items, pattern->groups * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_sre_match_span_value(tinypy_sre_match_object_t *match, tinypy_value_t *args, size_t *out_start, size_t *out_end, tinypy_error_t **out_error) {
    size_t group = 0U;

    int condition_6 = tinypy_tuple_size(args) == 2U;
    if (condition_6 != 0) {
        tinypy_value_t *item = tinypy_tuple_get(args, 1U);
        condition_6 = __tinypy_sre_match_group_index(match, item, &group, out_error) == 0;
    }
    if (condition_6) {
        return INT32_C(0);
    }
    if (group == 0U) {
        *out_start = match->start;
        *out_end = match->end;
    }
    else {
        *out_start = match->marks[(group - 1U) * 2U];
        *out_end = match->marks[(group - 1U) * 2U + 1U];
    }
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_match_span_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_sre_match_object_t *match;
    size_t start;
    size_t end;
    intptr_t operation = (intptr_t)user_data;

    int condition_7 = __tinypy_sre_method_arguments(vm, args, kwargs, 1U, 2U, out_error) == 0;
    if (condition_7 == 0) {
        tinypy_value_t *item = tinypy_tuple_get(args, 0U);
        condition_7 = tinypy_internal_value_kind(item) != TINYPY_VALUE_SRE_MATCH;
    }
    if (condition_7) {
        return NULL;
    }
    match = TINYPY_SRE_MATCH_OBJECT(tinypy_tuple_get(args, 0U));
    if (__tinypy_sre_match_span_value(match, args, &start, &end, out_error) == 0) {
        return NULL;
    }
    if (operation == 0) {
        return tinypy_integer_from_i64(vm, start == SIZE_MAX ? INT64_C(-1) : (int64_t)start);
    }
    if (operation == 1) {
        return tinypy_integer_from_i64(vm, end == SIZE_MAX ? INT64_C(-1) : (int64_t)end);
    } {
        tinypy_value_t *start_value = tinypy_integer_from_i64(vm, start == SIZE_MAX ? INT64_C(-1) : (int64_t)start);
        tinypy_value_t *end_value = tinypy_integer_from_i64(vm, end == SIZE_MAX ? INT64_C(-1) : (int64_t)end);
        tinypy_value_t *items[2] = {start_value, end_value};
        tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);

        tinypy_release(end_value);
        tinypy_release(start_value);
        return result;
    }
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_pattern_findall(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_sre_pattern_object_t *pattern;
    tinypy_value_t *string;
    tinypy_value_t *result;
    size_t pos;
    size_t endpos;

    (void)user_data;
    int condition_8 = __tinypy_sre_method_arguments(vm, args, kwargs, 2U, 4U, out_error) == 0;
    if (condition_8 == 0) {
        tinypy_value_t *item_2 = tinypy_tuple_get(args, 0U);
        condition_8 = tinypy_internal_value_kind(item_2) != TINYPY_VALUE_SRE_PATTERN;
    }
    if (condition_8) {
        return NULL;
    }
    pattern = TINYPY_SRE_PATTERN_OBJECT(tinypy_tuple_get(args, 0U));
    string = tinypy_tuple_get(args, 1U);
    if (__tinypy_sre_bounds(pattern, args, 1U, &pos, &endpos, out_error) == 0) {
        return NULL;
    }
    result = tinypy_list_from_items(vm, NULL, 0U);
    while (pos <= endpos) {
        tinypy_value_t *match_value = __tinypy_sre_execute(pattern, string, pos, endpos, INT32_C(1), out_error);
        tinypy_sre_match_object_t *match;
        tinypy_value_t *item;

        if (match_value == NULL) {
            tinypy_release(result);
            return NULL;
        }
        if (tinypy_internal_value_kind(match_value) == TINYPY_VALUE_NONE) {
            tinypy_release(match_value);
            break;
        }
        match = TINYPY_SRE_MATCH_OBJECT(match_value);
        if (pattern->groups == 0U) {
            item = __tinypy_sre_text_slice(vm, string, match->start, match->end);
        }
        else if (pattern->groups == 1U) {
            tinypy_value_t *empty = __tinypy_sre_empty_like(vm, string);

            item = __tinypy_sre_match_group_value(match, 1U, empty);
            tinypy_release(empty);
        }
        else {
            tinypy_value_t *empty = __tinypy_sre_empty_like(vm, string);
            tinypy_value_t **items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, pattern->groups * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            size_t index;

            for (index = 0U; index < pattern->groups; index += 1U) {
                items[index] = __tinypy_sre_match_group_value(match, index + 1U, empty);
            }
            item = tinypy_tuple_from_items(vm, items, pattern->groups);
            for (index = 0U; index < pattern->groups; index += 1U) {
                tinypy_release(items[index]);
            }
            tinypy_internal_vm_deallocate(vm, items, pattern->groups * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            tinypy_release(empty);
        }
        tinypy_list_append(result, item);
        tinypy_release(item);
        pos = match->end == match->start ? match->end + 1U : match->end;
        tinypy_release(match_value);
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_join(tinypy_vm_t *vm, tinypy_value_t *pieces, tinypy_value_t *source, tinypy_error_t **out_error) {
    size_t count = tinypy_list_size(pieces);
    size_t total = 0U;
    size_t index;
    unsigned char *bytes;
    size_t offset = 0U;
    int32_t unicode = tinypy_internal_value_kind(source) == TINYPY_VALUE_UNICODE;
    tinypy_value_t *result;

    for (index = 0U; index < count; index += 1U) {
        tinypy_value_t *piece = tinypy_list_get(pieces, index);
        tinypy_value_type_e kind = tinypy_internal_value_kind(piece);
        size_t size;

        if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "replacement must be a string", out_error);
            return NULL;
        }
        if (kind == TINYPY_VALUE_UNICODE) {
            unicode = INT32_C(1);
        }
        size = tinypy_internal_text_byte_size(piece);
        assert(size <= SIZE_MAX - total);
        total += size;
    }
    if (total == 0U) {
        return unicode != 0 ? tinypy_unicode_from_utf8(vm, NULL, 0U) : tinypy_string_from_bytes(vm, NULL, 0U);
    }
    bytes = (unsigned char *)tinypy_internal_vm_allocate(vm, total, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (index = 0U; index < count; index += 1U) {
        tinypy_value_t *piece = tinypy_list_get(pieces, index);
        size_t size = tinypy_internal_text_byte_size(piece);

        if (size != 0U) {
            (void)memcpy(bytes + offset, tinypy_internal_text_bytes(piece), size);
        }
        offset += size;
    }
    result = unicode != 0 ? tinypy_unicode_from_utf8(vm, (const char *)bytes, total) : tinypy_string_from_bytes(vm, bytes, total);
    tinypy_internal_vm_deallocate(vm, bytes, total, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_pattern_sub(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_sre_pattern_object_t *pattern;
    tinypy_value_t *replacement;
    tinypy_value_t *string;
    tinypy_value_t *pieces;
    tinypy_value_t *joined;
    size_t string_size;
    size_t pos = 0U;
    size_t copied = 0U;
    size_t substitutions = 0U;
    size_t limit = 0U;
    int32_t callable;

    int condition_9 = __tinypy_sre_method_arguments(vm, args, kwargs, 3U, 4U, out_error) == 0;
    if (condition_9 == 0) {
        tinypy_value_t *item_2 = tinypy_tuple_get(args, 0U);
        condition_9 = tinypy_internal_value_kind(item_2) != TINYPY_VALUE_SRE_PATTERN;
    }
    if (condition_9) {
        return NULL;
    }
    pattern = TINYPY_SRE_PATTERN_OBJECT(tinypy_tuple_get(args, 0U));
    replacement = tinypy_tuple_get(args, 1U);
    string = tinypy_tuple_get(args, 2U);
    if (tinypy_internal_value_kind(string) != TINYPY_VALUE_STRING && tinypy_internal_value_kind(string) != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected string or buffer", out_error);
        return NULL;
    }
    if (tinypy_tuple_size(args) == 4U) {
        int64_t count;

        tinypy_value_t *item = tinypy_tuple_get(args, 3U);
        if (__tinypy_sre_integer(item, &count) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "count must be an integer", out_error);
            return NULL;
        }
        limit = count <= 0 ? 0U : (size_t)count;
    }
    callable = replacement->type->call != NULL || tinypy_internal_object_has_special(replacement, "__call__", 8U) != 0;
    if (callable == 0 && tinypy_internal_value_kind(replacement) != TINYPY_VALUE_STRING && tinypy_internal_value_kind(replacement) != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "replacement must be a string or callable", out_error);
        return NULL;
    }
    string_size = tinypy_internal_text_byte_size(string);
    pieces = tinypy_list_from_items(vm, NULL, 0U);
    while (pos <= string_size && (limit == 0U || substitutions < limit)) {
        tinypy_value_t *match_value = __tinypy_sre_execute(pattern, string, pos, string_size, INT32_C(1), out_error);
        tinypy_sre_match_object_t *match;
        tinypy_value_t *piece;

        if (match_value == NULL) {
            tinypy_release(pieces);
            return NULL;
        }
        if (tinypy_internal_value_kind(match_value) == TINYPY_VALUE_NONE) {
            tinypy_release(match_value);
            break;
        }
        match = TINYPY_SRE_MATCH_OBJECT(match_value);
        if (copied < match->start) {
            piece = __tinypy_sre_text_slice(vm, string, copied, match->start);
            tinypy_list_append(pieces, piece);
            tinypy_release(piece);
        }
        else if (copied == match->start && copied == match->end && substitutions != 0U) {
            pos = match->end < string_size ? match->end + 1U : string_size + 1U;
            tinypy_release(match_value);
            continue;
        }
        if (callable != 0) {
            tinypy_value_t *call_args = tinypy_tuple_from_items(vm, &match_value, 1U);

            piece = tinypy_call(replacement, call_args, NULL, out_error);
            tinypy_release(call_args);
            if (piece == NULL) {
                tinypy_release(match_value);
                tinypy_release(pieces);
                return NULL;
            }
        }
        else {
            piece = replacement;
            tinypy_retain(piece);
        }
        if (tinypy_internal_value_kind(piece) != TINYPY_VALUE_NONE) {
            tinypy_list_append(pieces, piece);
        }
        tinypy_release(piece);
        copied = match->end;
        substitutions += 1U;
        pos = match->end == match->start ? match->end + 1U : match->end;
        tinypy_release(match_value);
    }
    if (copied < string_size) {
        tinypy_value_t *tail = __tinypy_sre_text_slice(vm, string, copied, string_size);

        tinypy_list_append(pieces, tail);
        tinypy_release(tail);
    }
    joined = __tinypy_sre_join(vm, pieces, string, out_error);
    tinypy_release(pieces);
    if (joined == NULL) {
        return NULL;
    }
    if (user_data == NULL) {
        return joined;
    } {
        tinypy_value_t *count = tinypy_integer_from_i64(vm, (int64_t)substitutions);
        tinypy_value_t *items[2] = {joined, count};
        tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);

        tinypy_release(count);
        tinypy_release(joined);
        return result;
    }
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_compile(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *source;
    tinypy_value_t *code_value;
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
    source = tinypy_tuple_get(args, 0U);
    code_value = tinypy_tuple_get(args, 2U);
    groupindex = tinypy_tuple_get(args, 4U);
    indexgroup = tinypy_tuple_get(args, 5U);
    flags_value = tinypy_tuple_get(args, 1U);
    groups_value = tinypy_tuple_get(args, 3U);
    int condition_11 = (tinypy_internal_value_kind(source) != TINYPY_VALUE_NONE && tinypy_internal_value_kind(source) != TINYPY_VALUE_STRING && tinypy_internal_value_kind(source) != TINYPY_VALUE_UNICODE) || __tinypy_sre_integer(flags_value, &flags) == 0 || __tinypy_sre_integer(groups_value, &groups) == 0 || groups < 0 || groups > 100 || tinypy_internal_value_kind(groupindex) != TINYPY_VALUE_DICT;
    if (condition_11 == 0) {
        condition_11 = tinypy_internal_value_kind(indexgroup) != TINYPY_VALUE_LIST;
    }
    int condition_10 = condition_11;
    if (condition_10 == 0) {
        condition_10 = (tinypy_internal_value_kind(code_value) != TINYPY_VALUE_LIST && tinypy_internal_value_kind(code_value) != TINYPY_VALUE_TUPLE);
    }
    if (condition_10) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "invalid SRE compile arguments", out_error);
        return NULL;
    }
    code_size = tinypy_internal_value_kind(code_value) == TINYPY_VALUE_LIST ? tinypy_list_size(code_value) : tinypy_tuple_size(code_value);
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
    tinypy_retain(source);
    tinypy_retain(groupindex);
    tinypy_retain(indexgroup);
    pattern->code = (uint32_t *)tinypy_internal_vm_allocate(vm, code_size * sizeof(*pattern->code), (uint32_t)TINYPY_ALLOC_TAG_SRE_DATA);
    for (index = 0U; index < code_size; index += 1U) {
        tinypy_value_t *item = tinypy_internal_value_kind(code_value) == TINYPY_VALUE_LIST ? tinypy_list_get(code_value, index) : tinypy_tuple_get(code_value, index);
        int64_t value;

        if (__tinypy_sre_integer(item, &value) == 0 || value < 0 || (uint64_t)value > UINT32_MAX) {
            tinypy_release(&pattern->base);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "SRE code must contain unsigned integers", out_error);
            return NULL;
        }
        pattern->code[index] = (uint32_t)value;
    }
    return &pattern->base;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sre_getlower(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *character_value;
    int64_t character;

    (void)user_data;
    if (__tinypy_sre_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "getlower arguments are invalid", out_error);
        return NULL;
    }
    character_value = tinypy_tuple_get(args, 0U);
    if (__tinypy_sre_integer(character_value, &character) == 0 || character < 0 || (uint64_t)character > UINT32_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "getlower arguments are invalid", out_error);
        return NULL;
    }
    uint32_t sre_lower = __tinypy_sre_lower((uint32_t)character);
    return tinypy_integer_from_i64(vm, (int64_t)sre_lower);
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_sre_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, user_data, NULL);
    tinypy_value_t *key = tinypy_string_from_bytes(type->vm, name, name_size);

    tinypy_dict_set(type->dict, key, function);
    tinypy_release(key);
    tinypy_release(function);
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_sre_add_module_function(tinypy_vm_t *vm, tinypy_value_t *module, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    tinypy_module_add_value(module, name, name_size, function);
    tinypy_release(function);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_sre_module(tinypy_vm_t *vm) {
    tinypy_value_t *module = tinypy_module_new(vm, "_sre", 4U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "_sre", 4U);
    tinypy_value_t *magic = tinypy_integer_from_i64(vm, TINYPY_SRE_MAGIC);
    tinypy_value_t *code_size = tinypy_integer_from_i64(vm, INT64_C(4));
    tinypy_value_t *max_repeat = tinypy_long_from_i64(vm, UINT32_MAX);

    __tinypy_sre_add_method(&vm->sre_pattern_type, "match", 5U, __tinypy_sre_pattern_match_or_search, NULL);
    __tinypy_sre_add_method(&vm->sre_pattern_type, "search", 6U, __tinypy_sre_pattern_match_or_search, (void *)(intptr_t)1);
    __tinypy_sre_add_method(&vm->sre_pattern_type, "findall", 7U, __tinypy_sre_pattern_findall, NULL);
    __tinypy_sre_add_method(&vm->sre_pattern_type, "sub", 3U, __tinypy_sre_pattern_sub, NULL);
    __tinypy_sre_add_method(&vm->sre_pattern_type, "subn", 4U, __tinypy_sre_pattern_sub, (void *)(intptr_t)1);
    __tinypy_sre_add_method(&vm->sre_match_type, "group", 5U, __tinypy_sre_match_group, NULL);
    __tinypy_sre_add_method(&vm->sre_match_type, "groups", 6U, __tinypy_sre_match_groups, NULL);
    __tinypy_sre_add_method(&vm->sre_match_type, "start", 5U, __tinypy_sre_match_span_method, (void *)(intptr_t)0);
    __tinypy_sre_add_method(&vm->sre_match_type, "end", 3U, __tinypy_sre_match_span_method, (void *)(intptr_t)1);
    __tinypy_sre_add_method(&vm->sre_match_type, "span", 4U, __tinypy_sre_match_span_method, (void *)(intptr_t)2);
    tinypy_module_add_value(module, "__name__", 8U, name);
    tinypy_module_add_value(module, "MAGIC", 5U, magic);
    tinypy_module_add_value(module, "CODESIZE", 8U, code_size);
    tinypy_module_add_value(module, "MAXREPEAT", 9U, max_repeat);
    __tinypy_sre_add_module_function(vm, module, "compile", 7U, __tinypy_sre_compile);
    __tinypy_sre_add_module_function(vm, module, "getlower", 8U, __tinypy_sre_getlower);
    tinypy_release(max_repeat);
    tinypy_release(code_size);
    tinypy_release(magic);
    tinypy_release(name);
    tinypy_internal_register_module(vm, "_sre", 4U, module);
    tinypy_release(module);
}
