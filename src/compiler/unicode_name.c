#include "internal.h"

#include "unicode_name_db.h"

#include <string.h>

#define TINYPY_UNICODE_NAME_S_BASE UINT32_C(0xac00)
#define TINYPY_UNICODE_NAME_L_COUNT 19U
#define TINYPY_UNICODE_NAME_V_COUNT 21U
#define TINYPY_UNICODE_NAME_T_COUNT 28U
#define TINYPY_UNICODE_NAME_N_COUNT (TINYPY_UNICODE_NAME_V_COUNT * TINYPY_UNICODE_NAME_T_COUNT)
#define TINYPY_UNICODE_NAME_S_COUNT (TINYPY_UNICODE_NAME_L_COUNT * TINYPY_UNICODE_NAME_N_COUNT)

static const char *const __tinypy_unicode_name_hangul_syllables[TINYPY_UNICODE_NAME_T_COUNT][3] = { {"G", "A", ""},
    {"GG", "AE", "G"}, {"N", "YA", "GG"},
    {"D", "YAE", "GS"}, {"DD", "EO", "N"},
    {"R", "E", "NJ"}, {"M", "YEO", "NH"},
    {"B", "YE", "D"}, {"BB", "O", "L"},
    {"S", "WA", "LG"}, {"SS", "WAE", "LM"},
    {"", "OE", "LB"}, {"J", "YO", "LS"},
    {"JJ", "U", "LT"}, {"C", "WEO", "LP"},
    {"K", "WE", "LH"}, {"T", "WI", "M"},
    {"P", "YU", "B"}, {"H", "EU", "BS"},
    {NULL, "YI", "S"}, {NULL, "I", "SS"},
    {NULL, NULL, "NG"}, {NULL, NULL, "J"},
    {NULL, NULL, "C"}, {NULL, NULL, "K"},
    {NULL, NULL, "T"}, {NULL, NULL, "P"},
    {NULL, NULL, "H"}};

//////////////////////////////////////////////////////////////////////////
static unsigned char __tinypy_unicode_name_upper(unsigned char byte) {
    if (byte >= 'a' && byte <= 'z') {
        return (unsigned char)(byte - ('a' - 'A'));
    }
    return byte;
}
//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_unicode_name_hash(const char *name, size_t name_size) {
    uint32_t hash = 0U;
    size_t index;

    for (index = 0U; index < name_size; ++index) {
        uint32_t overflow;

        hash = hash * (uint32_t)code_magic + (uint32_t)__tinypy_unicode_name_upper((unsigned char)name[index]);
        overflow = hash & UINT32_C(0xff000000);
        if (overflow != 0U) {
            hash = (hash ^ ((overflow >> 24U) & UINT32_C(0xff))) & UINT32_C(0x00ffffff);
        }
    }
    return hash;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_unicode_name_is_unified_ideograph(uint32_t code_point) {
    return (code_point >= UINT32_C(0x3400) && code_point <= UINT32_C(0x4db5)) || (code_point >= UINT32_C(0x4e00) && code_point <= UINT32_C(0x9fcb)) || (code_point >= UINT32_C(0x20000) && code_point <= UINT32_C(0x2a6d6)) || (code_point >= UINT32_C(0x2a700) && code_point <= UINT32_C(0x2b734));
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_unicode_name_append(char *buffer, size_t capacity, size_t *size, const char *text) {
    size_t text_size = strlen(text);

    if (text_size > capacity - *size) {
        return INT32_C(0);
    }
    (void)memcpy(buffer + *size, text, text_size);
    *size += text_size;
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_unicode_name_code_to_name(uint32_t code_point, char *buffer, size_t capacity) {
    size_t size = 0U;

    if (code_point >= UINT32_C(0x110000) || capacity == 0U) {
        return INT32_C(0);
    }
    if (code_point >= TINYPY_UNICODE_NAME_S_BASE && code_point < TINYPY_UNICODE_NAME_S_BASE + TINYPY_UNICODE_NAME_S_COUNT) {
        uint32_t syllable = code_point - TINYPY_UNICODE_NAME_S_BASE;
        uint32_t leading = syllable / TINYPY_UNICODE_NAME_N_COUNT;
        uint32_t vowel = (syllable % TINYPY_UNICODE_NAME_N_COUNT) / TINYPY_UNICODE_NAME_T_COUNT;
        uint32_t trailing = syllable % TINYPY_UNICODE_NAME_T_COUNT;

        if (__tinypy_unicode_name_append(buffer, capacity, &size, "HANGUL SYLLABLE ") == 0 || __tinypy_unicode_name_append(buffer, capacity, &size, __tinypy_unicode_name_hangul_syllables[leading][0]) == 0 || __tinypy_unicode_name_append(buffer, capacity, &size, __tinypy_unicode_name_hangul_syllables[vowel][1]) == 0 || __tinypy_unicode_name_append(buffer, capacity, &size, __tinypy_unicode_name_hangul_syllables[trailing][2]) == 0 || size == capacity) {
            return INT32_C(0);
        }
        buffer[size] = '\0';
        return INT32_C(1);
    }
    if (__tinypy_unicode_name_is_unified_ideograph(code_point) != 0) {
        static const char prefix[] = "CJK UNIFIED IDEOGRAPH-";
        char reversed[8];
        size_t digit_count = 0U;

        if (__tinypy_unicode_name_append(buffer, capacity, &size, prefix) == 0) {
            return INT32_C(0);
        }
        do {
            uint32_t digit = code_point & UINT32_C(0x0f);

            reversed[digit_count++] = (char)(digit < 10U ? (uint32_t)'0' + digit : (uint32_t)'A' + digit - 10U);
            code_point >>= 4U;
        } while (code_point != 0U);
        if (digit_count >= capacity - size) {
            return INT32_C(0);
        }
        while (digit_count != 0U) {
            buffer[size++] = reversed[--digit_count];
        }
        buffer[size] = '\0';
        return INT32_C(1);
    } {
        uint32_t offset = phrasebook_offset1[code_point >> phrasebook_shift];
        int32_t output_started = 0;

        offset = phrasebook_offset2[(offset << phrasebook_shift) + (code_point & ((UINT32_C(1) << phrasebook_shift) - 1U))];
        if (offset == 0U) {
            return INT32_C(0);
        }
        for (;;) {
            int32_t word = (int32_t)phrasebook[offset] - phrasebook_short;
            const unsigned char *text;

            if (word >= 0) {
                word = (word << 8) + (int32_t)phrasebook[offset + 1U];
                offset += 2U;
            }
            else {
                word = (int32_t)phrasebook[offset];
                offset += 1U;
            }
            if (output_started != 0) {
                if (size >= capacity) {
                    return INT32_C(0);
                }
                buffer[size++] = ' ';
            }
            text = lexicon + lexicon_offset[(size_t)word];
            while (*text < 128U) {
                if (size >= capacity) {
                    return INT32_C(0);
                }
                buffer[size++] = (char)*text++;
            }
            if (size >= capacity) {
                return INT32_C(0);
            }
            buffer[size++] = (char)(*text & 127U);
            if (*text == 128U) {
                break;
            }
            output_started = 1;
        }
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_unicode_name_equal(uint32_t code_point, const char *name, size_t name_size) {
    char buffer[NAME_MAXLEN];
    size_t index;

    if (name_size >= sizeof(buffer) || __tinypy_unicode_name_code_to_name(code_point, buffer, sizeof(buffer)) == 0) {
        return INT32_C(0);
    }
    for (index = 0U; index < name_size; ++index) {
        if (__tinypy_unicode_name_upper((unsigned char)name[index]) != (unsigned char)buffer[index]) {
            return INT32_C(0);
        }
    }
    return buffer[name_size] == '\0' ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_unicode_name_find_syllable(const char *text, size_t text_size, size_t count, size_t column, size_t *out_length, int32_t *out_position) {
    size_t index;

    *out_length = 0U;
    *out_position = -1;
    for (index = 0U; index < count; ++index) {
        const char *syllable = __tinypy_unicode_name_hangul_syllables[index][column];
        size_t syllable_size;

        if (syllable == NULL) {
            continue;
        }
        syllable_size = strlen(syllable);
        if ((*out_position >= 0 && syllable_size <= *out_length) || syllable_size > text_size || memcmp(text, syllable, syllable_size) != 0) {
            continue;
        }
        *out_length = syllable_size;
        *out_position = (int32_t)index;
    }
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_compiler_unicode_name(const char *name, size_t name_size, uint32_t *out_code_point) {
    static const char hangul_prefix[] = "HANGUL SYLLABLE ";
    static const char ideograph_prefix[] = "CJK UNIFIED IDEOGRAPH-";
    uint32_t hash;
    uint32_t mask = (uint32_t)code_size - 1U;
    uint32_t index;
    uint32_t code_point;
    uint32_t increment;

    assert(name != NULL);
    assert(out_code_point != NULL);
    if (name_size >= sizeof(hangul_prefix) - 1U && memcmp(name, hangul_prefix, sizeof(hangul_prefix) - 1U) == 0) {
        const char *position = name + sizeof(hangul_prefix) - 1U;
        size_t remaining = name_size - (sizeof(hangul_prefix) - 1U);
        size_t length;
        int32_t leading;
        int32_t vowel;
        int32_t trailing;

        __tinypy_unicode_name_find_syllable(position, remaining, TINYPY_UNICODE_NAME_L_COUNT, 0U, &length, &leading);
        position += length;
        remaining -= length;
        __tinypy_unicode_name_find_syllable(position, remaining, TINYPY_UNICODE_NAME_V_COUNT, 1U, &length, &vowel);
        position += length;
        remaining -= length;
        __tinypy_unicode_name_find_syllable(position, remaining, TINYPY_UNICODE_NAME_T_COUNT, 2U, &length, &trailing);
        position += length;
        remaining -= length;
        if (leading < 0 || vowel < 0 || trailing < 0 || remaining != 0U) {
            return INT32_C(0);
        }
        *out_code_point = TINYPY_UNICODE_NAME_S_BASE + ((uint32_t)leading * TINYPY_UNICODE_NAME_V_COUNT + (uint32_t)vowel) * TINYPY_UNICODE_NAME_T_COUNT + (uint32_t)trailing;
        return INT32_C(1);
    }
    if (name_size >= sizeof(ideograph_prefix) - 1U && memcmp(name, ideograph_prefix, sizeof(ideograph_prefix) - 1U) == 0) {
        size_t position = sizeof(ideograph_prefix) - 1U;

        if (name_size - position != 4U && name_size - position != 5U) {
            return INT32_C(0);
        }
        code_point = 0U;
        while (position < name_size) {
            unsigned char byte = (unsigned char)name[position++];
            uint32_t digit;

            if (byte >= '0' && byte <= '9') {
                digit = (uint32_t)(byte - '0');
            }
            else if (byte >= 'A' && byte <= 'F') {
                digit = (uint32_t)(byte - 'A') + 10U;
            }
            else {
                return INT32_C(0);
            }
            code_point = code_point * 16U + digit;
        }
        if (__tinypy_unicode_name_is_unified_ideograph(code_point) == 0) {
            return INT32_C(0);
        }
        *out_code_point = code_point;
        return INT32_C(1);
    }
    if (name_size == 0U || name_size >= NAME_MAXLEN) {
        return INT32_C(0);
    }
    hash = __tinypy_unicode_name_hash(name, name_size);
    index = (~hash) & mask;
    code_point = code_hash[index];
    if (code_point == 0U) {
        return INT32_C(0);
    }
    if (__tinypy_unicode_name_equal(code_point, name, name_size) != 0) {
        *out_code_point = code_point;
        return INT32_C(1);
    }
    increment = (hash ^ (hash >> 3U)) & mask;
    if (increment == 0U) {
        increment = mask;
    }
    for (;;) {
        index = (index + increment) & mask;
        code_point = code_hash[index];
        if (code_point == 0U) {
            return INT32_C(0);
        }
        if (__tinypy_unicode_name_equal(code_point, name, name_size) != 0) {
            *out_code_point = code_point;
            return INT32_C(1);
        }
        increment <<= 1U;
        if (increment > mask) {
            increment ^= (uint32_t)code_poly;
        }
    }
}
