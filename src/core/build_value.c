#include "tinypy/build_value.h"

#include "internal.h"

#include <string.h>
#include <wchar.h>
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_build_value_state_t {
    tinypy_vm_t *vm;
    const char *format;
    va_list *args;
    tinypy_internal_exception_state_t error;
    tinypy_bool_t failed;
} tinypy_build_value_state_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_build_value_read(tinypy_build_value_state_t *state);
//////////////////////////////////////////////////////////////////////////
static void __tinypy_build_value_system_error(tinypy_vm_t *vm, const char *message) {
    if (vm->raised_value != NULL) {
        return;
    }

    tinypy_value_t *text = tinypy_string_from_bytes(vm, message, strlen(message));
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &text, 1U);
    tinypy_value_t *exception = tinypy_exception_new(vm->exception_types[TINYPY_EXCEPTION_SYSTEM_ERROR], args, NULL);

    TINYPY_DECREF(args);
    TINYPY_DECREF(text);
    if (exception != NULL) {
        tinypy_internal_exception_set_raised(vm, exception, NULL);
        TINYPY_DECREF(exception);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_build_value_remember_error(tinypy_build_value_state_t *state) {
    if (state->failed != 0) {
        return;
    }

    __tinypy_build_value_system_error(state->vm, "build_value returned NULL without an exception");
    tinypy_internal_exception_preserve_begin(state->vm, &state->error);
    state->failed = TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_build_value_items(tinypy_build_value_state_t *state, char end) {
    tinypy_value_t *items = tinypy_list_from_items(state->vm, NULL, 0U);

    for (;;) {
        while (*state->format == ' ' || *state->format == '\t' || *state->format == ',' || *state->format == ':') {
            ++state->format;
        }
        char token = *state->format;
        if (token == end) {
            if (end != '\0') {
                ++state->format;
            }
            break;
        }
        if (token == '\0' || token == ')' || token == ']' || token == '}') {
            __tinypy_build_value_system_error(state->vm, "unmatched delimiter in build_value format");
            __tinypy_build_value_remember_error(state);
            break;
        }

        /* While the format can still be parsed, consume arguments after a
         * failure and release transferred N references and converter results.
         * The saved first exception is restored when the build returns. */
        if (state->failed != 0) {
            tinypy_internal_exception_clear_raised(state->vm);
        }
        tinypy_value_t *item = __tinypy_build_value_read(state);
        if (item == NULL) {
            __tinypy_build_value_remember_error(state);
        }
        else {
            if (state->failed == 0 && tinypy_internal_list_append_checked(items, item, NULL) == 0) {
                __tinypy_build_value_remember_error(state);
            }
            TINYPY_DECREF(item);
        }
    }

    if (state->failed != 0) {
        TINYPY_DECREF(items);
        return NULL;
    }
    return items;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_build_value_tuple(tinypy_value_t *items) {
    tinypy_value_t *result = tinypy_internal_tuple_from_items_checked(TINYPY_VALUE_VM(items), TINYPY_LIST_OBJECT(items)->items, TINYPY_SIZED_SIZE(items), NULL);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_build_value_container(tinypy_build_value_state_t *state, char token) {
    char end = token == '(' ? ')' : token == '[' ? ']' : '}';
    tinypy_value_t *items = __tinypy_build_value_items(state, end);

    if (items == NULL || token == '[') {
        return items;
    }
    if (token == '(') {
        tinypy_value_t *tuple = __tinypy_build_value_tuple(items);
        TINYPY_DECREF(items);
        return tuple;
    }

    size_t count = TINYPY_SIZED_SIZE(items);
    if (count % 2U != 0U) {
        TINYPY_DECREF(items);
        __tinypy_build_value_system_error(state->vm, "dictionary build_value format requires key/value pairs");
        return NULL;
    }

    tinypy_value_t *dict = tinypy_dict_new(state->vm);
    for (size_t index = 0U; index < count; index += 2U) {
        tinypy_value_t *key = tinypy_list_get(items, index);
        tinypy_value_t *value = tinypy_list_get(items, index + 1U);

        if (tinypy_internal_dict_set_checked(state->vm, dict, key, value, NULL) == 0) {
            TINYPY_DECREF(dict);
            TINYPY_DECREF(items);
            return NULL;
        }
    }
    TINYPY_DECREF(items);
    return dict;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_build_value_size(tinypy_build_value_state_t *state) {
    if (*state->format != '#') {
        return -1;
    }
    ++state->format;
    int size = va_arg(*state->args, int);
    return size;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_build_value_unsigned_long(tinypy_vm_t *vm, uint64_t value) {
    uint16_t digits[5];
    size_t count = 0U;

    while (value != 0U) {
        digits[count++] = (uint16_t)(value & UINT64_C(0x7fff));
        value >>= 15U;
    }
    int32_t sign = count == 0U ? 0 : 1;
    tinypy_value_t *result = tinypy_long_from_base15_digits(vm, sign, digits, count);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_build_value_unicode(tinypy_vm_t *vm, const wchar_t *value, size_t size) {
    if (size > (SIZE_MAX - 1U) / 4U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "unicode is too large", NULL);
        return NULL;
    }

    size_t capacity = size * 4U + 1U;
    uint8_t *utf8 = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, capacity, NULL);
    if (utf8 == NULL) {
        return NULL;
    }
    size_t byte_count = 0U;
    for (size_t index = 0U; index < size; ++index) {
        uint32_t code_point = (uint32_t)value[index];
        if (sizeof(wchar_t) == 2U && code_point >= UINT32_C(0xd800) && code_point <= UINT32_C(0xdbff) && index + 1U < size) {
            uint32_t low = (uint32_t)value[index + 1U];
            if (low >= UINT32_C(0xdc00) && low <= UINT32_C(0xdfff)) {
                code_point = UINT32_C(0x10000) + ((code_point - UINT32_C(0xd800)) << 10U) + low - UINT32_C(0xdc00);
                ++index;
            }
        }
        byte_count += tinypy_internal_utf8_encode(code_point, utf8 + byte_count);
    }
    tinypy_value_t *result = tinypy_unicode_from_utf8(vm, (const char *)utf8, byte_count);
    tinypy_internal_vm_deallocate(vm, utf8, capacity);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_build_value_read(tinypy_build_value_state_t *state) {
    tinypy_vm_t *vm = state->vm;
    char token = *state->format++;

    switch (token) {
    case '(':
    case '[':
    case '{':
        return __tinypy_build_value_container(state, token);
    case 'b':
    case 'B':
    case 'h':
    case 'i': {
        int value = va_arg(*state->args, int);
        tinypy_value_t *result = tinypy_integer_from_i64(vm, value);
        return result;
    }
    case 'H':
    case 'I': {
        unsigned int value = va_arg(*state->args, unsigned int);
        tinypy_value_t *result = tinypy_integer_from_i64(vm, value);
        return result;
    }
    case 'n': {
        ptrdiff_t value = va_arg(*state->args, ptrdiff_t);
        tinypy_value_t *result = tinypy_integer_from_i64(vm, value);
        return result;
    }
    case 'l': {
        long value = va_arg(*state->args, long);
        tinypy_value_t *result = tinypy_integer_from_i64(vm, value);
        return result;
    }
    case 'k': {
        unsigned long value = va_arg(*state->args, unsigned long);
        tinypy_value_t *result = (uint64_t)value <= (uint64_t)INT64_MAX ? tinypy_integer_from_i64(vm, (int64_t)value) : __tinypy_build_value_unsigned_long(vm, value);
        return result;
    }
    case 'L': {
        long long value = va_arg(*state->args, long long);
        tinypy_value_t *result = tinypy_long_from_i64(vm, value);
        return result;
    }
    case 'K': {
        unsigned long long value = va_arg(*state->args, unsigned long long);
        tinypy_value_t *result = __tinypy_build_value_unsigned_long(vm, value);
        return result;
    }
    case 'f':
    case 'd': {
        double value = va_arg(*state->args, double);
        tinypy_value_t *result = tinypy_float_from_double(vm, value);
        return result;
    }
    case 'D': {
        const void *value = va_arg(*state->args, const void *);
        double parts[2];
        (void)memcpy(parts, value, sizeof(parts));
        tinypy_value_t *result = tinypy_complex_from_doubles(vm, parts[0], parts[1]);
        return result;
    }
    case 'c': {
        char value = (char)va_arg(*state->args, int);
        tinypy_value_t *result = tinypy_string_from_bytes(vm, &value, 1U);
        return result;
    }
    case 's':
    case 'z': {
        const char *value = va_arg(*state->args, const char *);
        int specified_size = __tinypy_build_value_size(state);
        if (value == NULL) {
            tinypy_value_t *result = tinypy_none_get(vm);
            return result;
        }
        size_t size = specified_size < 0 ? strlen(value) : (size_t)specified_size;
        tinypy_value_t *result = tinypy_string_from_bytes(vm, value, size);
        return result;
    }
    case 'u': {
        const wchar_t *value = va_arg(*state->args, const wchar_t *);
        int specified_size = __tinypy_build_value_size(state);
        if (value == NULL) {
            tinypy_value_t *result = tinypy_none_get(vm);
            return result;
        }
        size_t size = specified_size < 0 ? wcslen(value) : (size_t)specified_size;
        tinypy_value_t *result = __tinypy_build_value_unicode(vm, value, size);
        return result;
    }
    case 'O':
    case 'S':
    case 'N': {
        if (*state->format == '&') {
            ++state->format;
            tinypy_build_value_converter_t converter = va_arg(*state->args, tinypy_build_value_converter_t);
            void *argument = va_arg(*state->args, void *);
            tinypy_value_t *result = converter(argument);
            return result;
        }
        tinypy_value_t *value = va_arg(*state->args, tinypy_value_t *);
        if (value != NULL && token != 'N') {
            TINYPY_INCREF(value);
        }
        return value;
    }
    default:
        __tinypy_build_value_system_error(vm, "unsupported build_value format");
        return NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_build_value_va(tinypy_vm_t *vm, const char *format, va_list args, tinypy_error_t **out_error) {
    va_list copied_args;
    tinypy_build_value_state_t state;
    tinypy_value_t *result = NULL;

    TINYPY_CLEAR_ERROR(out_error);
    va_copy(copied_args, args);
    (void)memset(&state, 0, sizeof(state));
    state.vm = vm;
    state.format = format != NULL ? format : "";
    state.args = &copied_args;
    tinypy_value_t *items = __tinypy_build_value_items(&state, '\0');
    if (items != NULL) {
        size_t count = TINYPY_SIZED_SIZE(items);
        if (count == 0U) {
            result = tinypy_none_get(vm);
        }
        else if (count == 1U) {
            result = tinypy_list_get(items, 0U);
            TINYPY_INCREF(result);
        }
        else {
            result = __tinypy_build_value_tuple(items);
        }
        TINYPY_DECREF(items);
    }
    va_end(copied_args);
    if (result == NULL) {
        __tinypy_build_value_remember_error(&state);
        tinypy_internal_exception_preserve_end(vm, &state.error);
        tinypy_internal_exception_make_diagnostic(vm, out_error);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_build_value(tinypy_vm_t *vm, tinypy_error_t **out_error, const char *format, ...) {
    va_list args;

    va_start(args, format);
    tinypy_value_t *result = tinypy_build_value_va(vm, format, args, out_error);
    va_end(args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
