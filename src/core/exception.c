#include "tinypy/exception.h"

#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct tinypy_exception_definition_t {
    const char *name;
    size_t name_size;
    int32_t parent;
} tinypy_exception_definition_t;
static const tinypy_exception_definition_t __tinypy_exception_definitions[TINYPY_EXCEPTION_TYPE_COUNT] = { {"BaseException", 13U, -1},
    {"Exception", 9U, TINYPY_EXCEPTION_BASE}, {"StandardError", 13U, TINYPY_EXCEPTION_EXCEPTION},
    {"ArithmeticError", 15U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"FloatingPointError", 18U, TINYPY_EXCEPTION_ARITHMETIC_ERROR},
    {"OverflowError", 13U, TINYPY_EXCEPTION_ARITHMETIC_ERROR}, {"ZeroDivisionError", 17U, TINYPY_EXCEPTION_ARITHMETIC_ERROR},
    {"AssertionError", 14U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"AttributeError", 14U, TINYPY_EXCEPTION_STANDARD_ERROR},
    {"EnvironmentError", 16U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"IOError", 7U, TINYPY_EXCEPTION_ENVIRONMENT_ERROR},
    {"OSError", 7U, TINYPY_EXCEPTION_ENVIRONMENT_ERROR}, {"WindowsError", 12U, TINYPY_EXCEPTION_OS_ERROR},
    {"EOFError", 8U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"ImportError", 11U, TINYPY_EXCEPTION_STANDARD_ERROR},
    {"LookupError", 11U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"IndexError", 10U, TINYPY_EXCEPTION_LOOKUP_ERROR},
    {"KeyError", 8U, TINYPY_EXCEPTION_LOOKUP_ERROR}, {"MemoryError", 11U, TINYPY_EXCEPTION_STANDARD_ERROR},
    {"NameError", 9U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"UnboundLocalError", 17U, TINYPY_EXCEPTION_NAME_ERROR},
    {"ReferenceError", 14U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"RuntimeError", 12U, TINYPY_EXCEPTION_STANDARD_ERROR},
    {"NotImplementedError", 19U, TINYPY_EXCEPTION_RUNTIME_ERROR}, {"SyntaxError", 11U, TINYPY_EXCEPTION_STANDARD_ERROR},
    {"IndentationError", 16U, TINYPY_EXCEPTION_SYNTAX_ERROR}, {"TabError", 8U, TINYPY_EXCEPTION_INDENTATION_ERROR},
    {"SystemError", 11U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"TypeError", 9U, TINYPY_EXCEPTION_STANDARD_ERROR},
    {"ValueError", 10U, TINYPY_EXCEPTION_STANDARD_ERROR}, {"UnicodeError", 12U, TINYPY_EXCEPTION_VALUE_ERROR},
    {"UnicodeDecodeError", 18U, TINYPY_EXCEPTION_UNICODE_ERROR}, {"UnicodeEncodeError", 18U, TINYPY_EXCEPTION_UNICODE_ERROR},
    {"UnicodeTranslateError", 21U, TINYPY_EXCEPTION_UNICODE_ERROR}, {"StopIteration", 13U, TINYPY_EXCEPTION_EXCEPTION},
    {"Warning", 7U, TINYPY_EXCEPTION_EXCEPTION}, {"UserWarning", 11U, TINYPY_EXCEPTION_WARNING},
    {"DeprecationWarning", 18U, TINYPY_EXCEPTION_WARNING}, {"PendingDeprecationWarning", 25U, TINYPY_EXCEPTION_WARNING},
    {"SyntaxWarning", 13U, TINYPY_EXCEPTION_WARNING}, {"RuntimeWarning", 14U, TINYPY_EXCEPTION_WARNING},
    {"FutureWarning", 13U, TINYPY_EXCEPTION_WARNING}, {"ImportWarning", 13U, TINYPY_EXCEPTION_WARNING},
    {"UnicodeWarning", 14U, TINYPY_EXCEPTION_WARNING}, {"BytesWarning", 12U, TINYPY_EXCEPTION_WARNING},
    {"SystemExit", 10U, TINYPY_EXCEPTION_BASE}, {"KeyboardInterrupt", 17U, TINYPY_EXCEPTION_BASE},
    {"GeneratorExit", 13U, TINYPY_EXCEPTION_BASE}, {"BufferError", 11U, TINYPY_EXCEPTION_STANDARD_ERROR}};
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_exception_string_length(const char *text) {
    size_t size = 0U;

    while (text[size] != '\0') {
        size += 1U;
    }
    return size;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_exception_builtin_set(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *value) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(vm->builtins, key, value);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_exception_type_set_none(tinypy_type_t *type, const char *name, size_t name_size) {
    tinypy_value_t *none = tinypy_none_get(type->vm);

    tinypy_type_set_attr(type, name, name_size, none);
    TINYPY_DECREF(none);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_is_class(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_bool_t return_value_1 = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)value, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_is_instance(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_bool_t return_value_1 = tinypy_type_is_subtype(value->type, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_internal_exception_payload_t *__tinypy_exception_payload(tinypy_value_t *value) {
    tinypy_internal_exception_payload_t *payload = (tinypy_internal_exception_payload_t *)tinypy_native_instance_payload(value);

    return payload;
}
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_exception_text_part_t {
    const void *bytes;
    size_t size;
} tinypy_exception_text_part_t;

static tinypy_value_t *__tinypy_exception_join(tinypy_vm_t *vm, const tinypy_exception_text_part_t *parts, size_t part_count, tinypy_error_t **out_error) {
    size_t total = 0U;
    size_t index;
    uint8_t *bytes;

    for (index = 0U; index != part_count; ++index) {
        if (parts[index].size > SIZE_MAX - total) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "exception string is too large", out_error);
            return NULL;
        }
        total += parts[index].size;
    }
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, total, total, &bytes, out_error);

    if (result == NULL) {
        return NULL;
    }
    total = 0U;
    for (index = 0U; index != part_count; ++index) {
        if (parts[index].size != 0U) {
            (void)memcpy(bytes + total, parts[index].bytes, parts[index].size);
            total += parts[index].size;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_attr_or_none(tinypy_value_t *value, const char *name, size_t name_size) {
    tinypy_value_t *attribute = tinypy_instance_get_attr(value, name, name_size);

    if (attribute != NULL) {
        return attribute;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(TINYPY_VALUE_VM(value));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_is_subtype(tinypy_value_t *value, tinypy_exception_type_index_e index) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    tinypy_bool_t return_value_1 = tinypy_type_is_subtype(value->type, vm->exception_types[index]) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_key_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *args = __tinypy_exception_payload(value)->args;

    if (args != NULL && TINYPY_TUPLE_SIZE(args) == 1U) {
        tinypy_value_t *return_value_1 = tinypy_object_repr(TINYPY_TUPLE_GET(args, 0U), out_error);
        return return_value_1;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_environment_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *error_number = __tinypy_exception_attr_or_none(value, "errno", 5U);
    tinypy_value_t *error_text = __tinypy_exception_attr_or_none(value, "strerror", 8U);
    tinypy_value_t *filename = __tinypy_exception_attr_or_none(value, "filename", 8U);
    tinypy_value_t *number_string;
    tinypy_value_t *text_string;
    tinypy_value_t *filename_string = NULL;
    tinypy_exception_text_part_t parts[7];
    size_t part_count = 5U;

    if (TINYPY_VALUE_KIND(error_number) == TINYPY_VALUE_NONE || TINYPY_VALUE_KIND(error_text) == TINYPY_VALUE_NONE) {
        return NULL;
    }
    number_string = tinypy_object_str(error_number, out_error);
    if (number_string == NULL) {
        return NULL;
    }
    text_string = tinypy_object_str(error_text, out_error);
    if (text_string == NULL) {
        TINYPY_DECREF(number_string);
        return NULL;
    }
    parts[0] = (tinypy_exception_text_part_t){"[Errno ", 7U};
    parts[1] = (tinypy_exception_text_part_t){TINYPY_TEXT_BYTES(number_string), TINYPY_TEXT_BYTE_SIZE(number_string)};
    parts[2] = (tinypy_exception_text_part_t){"] ", 2U};
    parts[3] = (tinypy_exception_text_part_t){TINYPY_TEXT_BYTES(text_string), TINYPY_TEXT_BYTE_SIZE(text_string)};
    parts[4] = (tinypy_exception_text_part_t){NULL, 0U};
    if (TINYPY_VALUE_KIND(filename) != TINYPY_VALUE_NONE) {
        filename_string = tinypy_object_repr(filename, out_error);
        if (filename_string == NULL) {
            TINYPY_DECREF(text_string);
            TINYPY_DECREF(number_string);
            return NULL;
        }
        parts[4] = (tinypy_exception_text_part_t){": ", 2U};
        parts[5] = (tinypy_exception_text_part_t){TINYPY_TEXT_BYTES(filename_string), TINYPY_TEXT_BYTE_SIZE(filename_string)};
        part_count = 6U;
    }
    tinypy_value_t *result = __tinypy_exception_join(vm, parts, part_count, out_error);

    if (filename_string != NULL) {
        TINYPY_DECREF(filename_string);
    }
    TINYPY_DECREF(text_string);
    TINYPY_DECREF(number_string);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_syntax_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *message = __tinypy_exception_attr_or_none(value, "msg", 3U);
    tinypy_value_t *filename = __tinypy_exception_attr_or_none(value, "filename", 8U);
    tinypy_value_t *line = __tinypy_exception_attr_or_none(value, "lineno", 6U);
    tinypy_value_t *message_string = tinypy_object_str(message, out_error);
    tinypy_value_t *filename_string;
    tinypy_value_t *line_string;
    tinypy_exception_text_part_t parts[6];

    if (message_string == NULL) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(filename) == TINYPY_VALUE_NONE || TINYPY_VALUE_KIND(line) == TINYPY_VALUE_NONE) {
        return message_string;
    }
    filename_string = tinypy_object_str(filename, out_error);
    if (filename_string == NULL) {
        TINYPY_DECREF(message_string);
        return NULL;
    }
    line_string = tinypy_object_str(line, out_error);
    if (line_string == NULL) {
        TINYPY_DECREF(filename_string);
        TINYPY_DECREF(message_string);
        return NULL;
    }
    parts[0] = (tinypy_exception_text_part_t){TINYPY_TEXT_BYTES(message_string), TINYPY_TEXT_BYTE_SIZE(message_string)};
    parts[1] = (tinypy_exception_text_part_t){" (", 2U};
    parts[2] = (tinypy_exception_text_part_t){TINYPY_TEXT_BYTES(filename_string), TINYPY_TEXT_BYTE_SIZE(filename_string)};
    parts[3] = (tinypy_exception_text_part_t){", line ", 7U};
    parts[4] = (tinypy_exception_text_part_t){TINYPY_TEXT_BYTES(line_string), TINYPY_TEXT_BYTE_SIZE(line_string)};
    parts[5] = (tinypy_exception_text_part_t){")", 1U};
    tinypy_value_t *result = __tinypy_exception_join(vm, parts, 6U, out_error);

    TINYPY_DECREF(line_string);
    TINYPY_DECREF(filename_string);
    TINYPY_DECREF(message_string);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_unicode_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_bool_t decode = __tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_UNICODE_DECODE_ERROR);
    tinypy_bool_t translate = __tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR);
    tinypy_value_t *encoding = translate != 0 ? NULL : __tinypy_exception_attr_or_none(value, "encoding", 8U);
    tinypy_value_t *object = __tinypy_exception_attr_or_none(value, "object", 6U);
    tinypy_value_t *start_value = __tinypy_exception_attr_or_none(value, "start", 5U);
    tinypy_value_t *end_value = __tinypy_exception_attr_or_none(value, "end", 3U);
    tinypy_value_t *reason = __tinypy_exception_attr_or_none(value, "reason", 6U);
    int64_t start;
    int64_t end;
    char positions[96];
    int position_size;
    tinypy_value_t *character_repr = NULL;
    tinypy_exception_text_part_t parts[12];
    size_t part_count = 0U;

    if ((translate == 0 && TINYPY_VALUE_KIND(encoding) != TINYPY_VALUE_STRING) ||
        TINYPY_VALUE_KIND(object) != (decode != 0 ? TINYPY_VALUE_STRING : TINYPY_VALUE_UNICODE) ||
        TINYPY_VALUE_KIND(reason) != TINYPY_VALUE_STRING) {
        return NULL;
    }
    if ((TINYPY_VALUE_KIND(start_value) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(start_value) != TINYPY_VALUE_INTEGER && TINYPY_VALUE_KIND(start_value) != TINYPY_VALUE_LONG) ||
        (TINYPY_VALUE_KIND(end_value) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(end_value) != TINYPY_VALUE_INTEGER && TINYPY_VALUE_KIND(end_value) != TINYPY_VALUE_LONG) ||
        tinypy_internal_index_as_i64(start_value, &start, TINYPY_FALSE, out_error) == 0 || tinypy_internal_index_as_i64(end_value, &end, TINYPY_FALSE, out_error) == 0) {
        return NULL;
    }
    if (translate == 0) {
        parts[part_count++] = (tinypy_exception_text_part_t){"'", 1U};
        parts[part_count++] = (tinypy_exception_text_part_t){TINYPY_TEXT_BYTES(encoding), TINYPY_TEXT_BYTE_SIZE(encoding)};
        parts[part_count++] = (tinypy_exception_text_part_t){decode != 0 ? "' codec can't decode " : "' codec can't encode ", 21U};
    }
    else {
        parts[part_count++] = (tinypy_exception_text_part_t){"can't translate ", 16U};
    }
    if (end == start + 1 && start >= 0 && (uint64_t)start < (uint64_t)TINYPY_SIZED_SIZE(object)) {
        if (decode != 0) {
            const uint8_t byte = TINYPY_TEXT_BYTES(object)[(size_t)start];

            position_size = snprintf(positions, sizeof(positions), "byte 0x%02x in position %" PRId64, (unsigned int)byte, start);
            parts[part_count++] = (tinypy_exception_text_part_t){positions, (size_t)position_size};
        }
        else {
            tinypy_value_t *index = tinypy_integer_from_i64(vm, start);
            tinypy_value_t *character = tinypy_internal_get_item_builtin(object, index, out_error);

            TINYPY_DECREF(index);
            if (character == NULL) {
                return NULL;
            }
            character_repr = tinypy_object_repr(character, out_error);
            TINYPY_DECREF(character);
            if (character_repr == NULL) {
                return NULL;
            }
            parts[part_count++] = (tinypy_exception_text_part_t){"character ", 10U};
            parts[part_count++] = (tinypy_exception_text_part_t){TINYPY_TEXT_BYTES(character_repr), TINYPY_TEXT_BYTE_SIZE(character_repr)};
            position_size = snprintf(positions, sizeof(positions), " in position %" PRId64, start);
            parts[part_count++] = (tinypy_exception_text_part_t){positions, (size_t)position_size};
        }
    }
    else {
        position_size = snprintf(positions, sizeof(positions), "%s in position %" PRId64 "-%" PRId64, decode != 0 ? "bytes" : "characters", start, end - 1);
        parts[part_count++] = (tinypy_exception_text_part_t){positions, (size_t)position_size};
    }
    parts[part_count++] = (tinypy_exception_text_part_t){": ", 2U};
    parts[part_count++] = (tinypy_exception_text_part_t){TINYPY_TEXT_BYTES(reason), TINYPY_TEXT_BYTE_SIZE(reason)};
    tinypy_value_t *result = __tinypy_exception_join(vm, parts, part_count, out_error);

    if (character_repr != NULL) {
        TINYPY_DECREF(character_repr);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_exception_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_internal_exception_payload_t *payload = __tinypy_exception_payload(value);

    tinypy_internal_instance_release_references(value, visit, user_data);
    if (payload->args != NULL) {
        visit(payload->args, user_data);
    }
    if (payload->message != NULL) {
        visit(payload->message, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_allocate_blank(tinypy_type_t *type) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *instance = tinypy_internal_object_allocate(vm, type, type->basic_size);
    tinypy_internal_exception_payload_t *payload = __tinypy_exception_payload(instance);

    payload->args = tinypy_tuple_from_items(vm, NULL, 0U);
    payload->message = tinypy_string_from_bytes(vm, NULL, 0U);
    return instance;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *args = __tinypy_exception_payload(value)->args;

    if (__tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_KEY_ERROR) != 0 && args != NULL && TINYPY_TUPLE_SIZE(args) == 1U) {
        tinypy_value_t *return_value_1 = __tinypy_exception_key_string(value, out_error);
        return return_value_1;
    }
    if (__tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_ENVIRONMENT_ERROR) != 0) {
        tinypy_value_t *result = __tinypy_exception_environment_string(value, out_error);

        if (result != NULL || (out_error != NULL && *out_error != NULL)) {
            return result;
        }
    }
    if (__tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_SYNTAX_ERROR) != 0) {
        tinypy_value_t *return_value_2 = __tinypy_exception_syntax_string(value, out_error);
        return return_value_2;
    }
    if (__tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR) != 0 || __tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_UNICODE_DECODE_ERROR) != 0 || __tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR) != 0) {
        tinypy_value_t *result = __tinypy_exception_unicode_string(value, out_error);

        if (result != NULL || (out_error != NULL && *out_error != NULL)) {
            return result;
        }
    }

    if (args == NULL || TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(TINYPY_VALUE_VM(value), NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t *display = TINYPY_TUPLE_SIZE(args) == 1U ? TINYPY_TUPLE_GET(args, 0U) : args;
    tinypy_value_t *return_value_2 = tinypy_object_str(display, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_getitem_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *exception_args = __tinypy_exception_payload(TINYPY_TUPLE_GET(args, 0U))->args;
    tinypy_value_t *result = tinypy_internal_get_item_builtin(exception_args, TINYPY_TUPLE_GET(args, 1U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_getslice_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *exception_args = __tinypy_exception_payload(TINYPY_TUPLE_GET(args, 0U))->args;
    tinypy_value_t *slice = tinypy_slice_new(vm, TINYPY_TUPLE_GET(args, 1U), TINYPY_TUPLE_GET(args, 2U), NULL);
    tinypy_value_t *result = tinypy_internal_get_item_builtin(exception_args, slice, out_error);

    TINYPY_DECREF(slice);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_str_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_exception_string(TINYPY_TUPLE_GET(args, 0U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_unicode_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *self;

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *string_method = tinypy_type_get_attr(self->type, "__str__", 7U);
    tinypy_value_t *base_string_method = tinypy_type_get_attr(vm->exception_types[TINYPY_EXCEPTION_BASE], "__str__", 7U);

    if (string_method != base_string_method) {
        tinypy_value_t *text = tinypy_object_str(self, out_error);
        tinypy_value_t *result;

        if (text == NULL) {
            return NULL;
        }
        result = tinypy_internal_object_unicode(text, out_error);
        TINYPY_DECREF(text);
        return result;
    }
    tinypy_value_t *exception_args = __tinypy_exception_payload(self)->args;
    if (TINYPY_TUPLE_SIZE(exception_args) == 0U) {
        tinypy_value_t *return_value_1 = tinypy_unicode_from_utf8(vm, NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t *display = TINYPY_TUPLE_SIZE(exception_args) == 1U ? TINYPY_TUPLE_GET(exception_args, 0U) : exception_args;
    tinypy_value_t *return_value_2 = tinypy_internal_object_unicode(display, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *self;
    tinypy_value_t *exception_args;
    tinypy_value_t *args_repr;
    size_t args_size;
    const char *args_bytes;
    uint8_t *result_bytes;

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    exception_args = __tinypy_exception_payload(self)->args;
    args_repr = tinypy_object_repr(exception_args, out_error);
    if (args_repr == NULL) {
        return NULL;
    }
    args_bytes = tinypy_string_view(args_repr, &args_size);
    if (args_size > SIZE_MAX - self->type->name_size) {
        TINYPY_DECREF(args_repr);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "exception representation is too large", out_error);
        return NULL;
    }
    size_t result_size = self->type->name_size + args_size;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, result_size, result_size, &result_bytes, out_error);

    if (result == NULL) {
        TINYPY_DECREF(args_repr);
        return NULL;
    }

    (void)memcpy(result_bytes, self->type->name, self->type->name_size);
    (void)memcpy(result_bytes + self->type->name_size, args_bytes, args_size);
    TINYPY_DECREF(args_repr);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_reduce_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *self;
    tinypy_value_t *exception_args;
    const tinypy_value_t *instance_dict;
    tinypy_value_t *result;
    tinypy_value_t *items[3];
    size_t item_count = 2U;

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    exception_args = __tinypy_exception_payload(self)->args;
    items[0] = &self->type->base.base;
    items[1] = exception_args;
    instance_dict = tinypy_instance_dict(self);
    if (instance_dict != NULL) {
        items[2] = (tinypy_value_t *)instance_dict;
        item_count = 3U;
    }
    result = tinypy_tuple_from_items(vm, items, item_count);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_setstate_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *self;
    tinypy_value_t *state;
    tinypy_dict_entry_t *iterator;
    tinypy_dict_entry_t *iterator_end;

    (void)user_data;
    if (__tinypy_exception_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    state = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(state) == TINYPY_VALUE_NONE) {
        tinypy_value_t *result = tinypy_none_get(vm);

        return result;
    }
    if (TINYPY_VALUE_KIND(state) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception state must be a dictionary", out_error);
        return NULL;
    }
    iterator = TINYPY_DICT_ITERATOR_BEGIN(state);
    iterator_end = TINYPY_DICT_ITERATOR_END(state);
    for (; iterator != iterator_end; ++iterator) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
            tinypy_value_type_e key_kind = TINYPY_VALUE_KIND(iterator->key);

            if (key_kind != TINYPY_VALUE_STRING && key_kind != TINYPY_VALUE_UNICODE) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "attribute name must be string", out_error);
                return NULL;
            }
            if (tinypy_internal_object_set_attr_protocol_key(self, iterator->key, iterator->value, out_error) == 0) {
                return NULL;
            }
        }
    }
    tinypy_value_t *result = tinypy_none_get(vm);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_exception_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, NULL, NULL);

    tinypy_type_set_attr(type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_exception_set_none_attr(tinypy_value_t *self, const char *name, size_t name_size) {
    tinypy_value_t *none = tinypy_none_get(TINYPY_VALUE_VM(self));

    tinypy_instance_set_attr(self, name, name_size, none);
    TINYPY_DECREF(none);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_exception_replace_args(tinypy_internal_exception_payload_t *payload, tinypy_value_t *args) {
    tinypy_value_t *previous = payload->args;

    payload->args = args;
    if (previous != NULL) {
        TINYPY_DECREF(previous);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_init_environment(tinypy_value_t *self, tinypy_value_t *const *items, size_t argument_count, tinypy_internal_exception_payload_t *payload) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);

    __tinypy_exception_set_none_attr(self, "errno", 5U);
    __tinypy_exception_set_none_attr(self, "strerror", 8U);
    __tinypy_exception_set_none_attr(self, "filename", 8U);
    if (argument_count == 2U || argument_count == 3U) {
        tinypy_instance_set_attr(self, "errno", 5U, items[1]);
        tinypy_instance_set_attr(self, "strerror", 8U, items[2]);
        if (argument_count == 3U) {
            tinypy_value_t *normalized = tinypy_tuple_from_items(vm, items + 1U, 2U);

            tinypy_instance_set_attr(self, "filename", 8U, items[3]);
            __tinypy_exception_replace_args(payload, normalized);
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_init_system_exit(tinypy_value_t *self, tinypy_value_t *const *items, size_t argument_count, tinypy_internal_exception_payload_t *payload) {
    tinypy_value_t *code;

    (void)items;
    if (argument_count == 0U) {
        __tinypy_exception_set_none_attr(self, "code", 4U);
        return TINYPY_TRUE;
    }
    code = argument_count == 1U ? TINYPY_TUPLE_GET(payload->args, 0U) : payload->args;
    tinypy_instance_set_attr(self, "code", 4U, code);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_init_syntax(tinypy_value_t *self, tinypy_value_t *const *items, size_t argument_count, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
    static const char *location_names[4] = {"filename", "lineno", "offset", "text"};
    static const size_t location_name_sizes[4] = {8U, 6U, 6U, 4U};
    size_t index;

    if (argument_count != 0U) {
        tinypy_instance_set_attr(self, "msg", 3U, items[1]);
    }
    else {
        __tinypy_exception_set_none_attr(self, "msg", 3U);
    }
    for (index = 0U; index != 4U; ++index) {
        __tinypy_exception_set_none_attr(self, location_names[index], location_name_sizes[index]);
    }
    __tinypy_exception_set_none_attr(self, "print_file_and_line", 19U);
    if (argument_count == 2U) {
        tinypy_value_t *conversion_args = tinypy_tuple_from_items(vm, &items[2], 1U);
        tinypy_value_t *location = tinypy_internal_tuple_create(&vm->types[TINYPY_VALUE_TUPLE], conversion_args, NULL, out_error);

        TINYPY_DECREF(conversion_args);
        if (location == NULL) {
            return TINYPY_FALSE;
        }
        if (TINYPY_TUPLE_SIZE(location) != 4U) {
            TINYPY_DECREF(location);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "tuple index out of range", out_error);
            return TINYPY_FALSE;
        }
        for (index = 0U; index != 4U; ++index) {
            tinypy_instance_set_attr(self, location_names[index], location_name_sizes[index], TINYPY_TUPLE_GET(location, index));
        }
        TINYPY_DECREF(location);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_require_unicode_argument(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_type_e expected, const char *message, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(value) == expected) {
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, message, out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_require_position(tinypy_vm_t *vm, tinypy_value_t *value, const char *message, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG) {
        int64_t ignored;

        tinypy_bool_t return_value_1 = tinypy_internal_index_as_i64(value, &ignored, TINYPY_FALSE, out_error);
        return return_value_1;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, message, out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_init_unicode(tinypy_value_t *self, tinypy_value_t *const *items, size_t argument_count, tinypy_bool_t decode, tinypy_bool_t translate, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
    size_t expected = translate != 0 ? 4U : 5U;
    size_t object_index = translate != 0 ? 1U : 2U;
    size_t start_index = translate != 0 ? 2U : 3U;
    size_t end_index = translate != 0 ? 3U : 4U;
    size_t reason_index = translate != 0 ? 4U : 5U;

    if (argument_count != expected) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unicode error constructor received the wrong number of arguments", out_error);
        return TINYPY_FALSE;
    }
    if (translate == 0 && __tinypy_exception_require_unicode_argument(vm, items[1], TINYPY_VALUE_STRING, "encoding must be a string", out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (__tinypy_exception_require_unicode_argument(vm, items[object_index], decode != 0 ? TINYPY_VALUE_STRING : TINYPY_VALUE_UNICODE, decode != 0 ? "object must be a string" : "object must be unicode", out_error) == 0 ||
        __tinypy_exception_require_position(vm, items[start_index], "start must be an integer", out_error) == 0 ||
        __tinypy_exception_require_position(vm, items[end_index], "end must be an integer", out_error) == 0 ||
        __tinypy_exception_require_unicode_argument(vm, items[reason_index], TINYPY_VALUE_STRING, "reason must be a string", out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (translate == 0) {
        tinypy_instance_set_attr(self, "encoding", 8U, items[1]);
    }
    tinypy_instance_set_attr(self, "object", 6U, items[object_index]);
    tinypy_instance_set_attr(self, "start", 5U, items[start_index]);
    tinypy_instance_set_attr(self, "end", 3U, items[end_index]);
    tinypy_instance_set_attr(self, "reason", 6U, items[reason_index]);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_init(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count = TINYPY_TUPLE_SIZE(args);

    (void)user_data;
    if (count == 0U || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception constructor does not accept keyword arguments", out_error);
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_internal_exception_payload_t *payload = __tinypy_exception_payload(self);
    tinypy_value_t *const *items = tinypy_internal_tuple_items(args);
    tinypy_value_t *exception_args = tinypy_tuple_from_items(vm, items + 1U, count - 1U);

    __tinypy_exception_replace_args(payload, exception_args);
    tinypy_value_t *message = count == 2U ? items[1] : tinypy_string_from_bytes(vm, NULL, 0U);

    if (count == 2U) {
        TINYPY_INCREF(message);
    }
    if (payload->message != NULL) {
        TINYPY_DECREF(payload->message);
    }
    payload->message = message;
    size_t argument_count = count - 1U;
    tinypy_bool_t initialized = TINYPY_TRUE;

    if (__tinypy_exception_is_subtype(self, TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR) != 0) {
        initialized = __tinypy_exception_init_unicode(self, items, argument_count, TINYPY_FALSE, TINYPY_FALSE, out_error);
    }
    else if (__tinypy_exception_is_subtype(self, TINYPY_EXCEPTION_UNICODE_DECODE_ERROR) != 0) {
        initialized = __tinypy_exception_init_unicode(self, items, argument_count, TINYPY_TRUE, TINYPY_FALSE, out_error);
    }
    else if (__tinypy_exception_is_subtype(self, TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR) != 0) {
        initialized = __tinypy_exception_init_unicode(self, items, argument_count, TINYPY_FALSE, TINYPY_TRUE, out_error);
    }
    else if (__tinypy_exception_is_subtype(self, TINYPY_EXCEPTION_SYNTAX_ERROR) != 0) {
        initialized = __tinypy_exception_init_syntax(self, items, argument_count, out_error);
    }
    else if (__tinypy_exception_is_subtype(self, TINYPY_EXCEPTION_ENVIRONMENT_ERROR) != 0) {
        initialized = __tinypy_exception_init_environment(self, items, argument_count, payload);
    }
    else if (__tinypy_exception_is_subtype(self, TINYPY_EXCEPTION_SYSTEM_EXIT) != 0) {
        initialized = __tinypy_exception_init_system_exit(self, items, argument_count, payload);
    }
    if (initialized == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "BaseException.__new__ requires an exception type", out_error);
        return NULL;
    }
    tinypy_value_t *type_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(type_value) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype((tinypy_type_t *)type_value, vm->exception_types[TINYPY_EXCEPTION_BASE]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "BaseException.__new__ requires an exception subtype", out_error);
        return NULL;
    }
    tinypy_value_t *result = __tinypy_exception_allocate_blank((tinypy_type_t *)type_value);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_exception_type_index_e __tinypy_exception_index_from_error(tinypy_error_kind_e kind) {
    switch (kind) {
    case TINYPY_ERROR_TYPE:
        return TINYPY_EXCEPTION_TYPE_ERROR;
    case TINYPY_ERROR_NAME:
        return TINYPY_EXCEPTION_NAME_ERROR;
    case TINYPY_ERROR_UNBOUND_LOCAL:
        return TINYPY_EXCEPTION_UNBOUND_LOCAL_ERROR;
    case TINYPY_ERROR_INTERRUPT:
        return TINYPY_EXCEPTION_KEYBOARD_INTERRUPT;
    case TINYPY_ERROR_ZERO_DIVISION:
        return TINYPY_EXCEPTION_ZERO_DIVISION_ERROR;
    case TINYPY_ERROR_VALUE:
        return TINYPY_EXCEPTION_VALUE_ERROR;
    case TINYPY_ERROR_INDEX:
        return TINYPY_EXCEPTION_INDEX_ERROR;
    case TINYPY_ERROR_KEY:
        return TINYPY_EXCEPTION_KEY_ERROR;
    case TINYPY_ERROR_OVERFLOW:
        return TINYPY_EXCEPTION_OVERFLOW_ERROR;
    case TINYPY_ERROR_IMPORT:
        return TINYPY_EXCEPTION_IMPORT_ERROR;
    case TINYPY_ERROR_ATTRIBUTE:
        return TINYPY_EXCEPTION_ATTRIBUTE_ERROR;
    case TINYPY_ERROR_LOOKUP:
        return TINYPY_EXCEPTION_LOOKUP_ERROR;
    case TINYPY_ERROR_UNICODE_DECODE:
        return TINYPY_EXCEPTION_UNICODE_DECODE_ERROR;
    case TINYPY_ERROR_UNICODE_ENCODE:
        return TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR;
    case TINYPY_ERROR_BUFFER:
        return TINYPY_EXCEPTION_BUFFER_ERROR;
    case TINYPY_ERROR_MEMORY:
        return TINYPY_EXCEPTION_MEMORY_ERROR;
    case TINYPY_ERROR_SYNTAX:
    case TINYPY_ERROR_PREPROCESSOR:
    case TINYPY_ERROR_META:
        return TINYPY_EXCEPTION_SYNTAX_ERROR;
    case TINYPY_ERROR_INDENTATION:
        return TINYPY_EXCEPTION_INDENTATION_ERROR;
    case TINYPY_ERROR_TAB:
        return TINYPY_EXCEPTION_TAB_ERROR;
    case TINYPY_ERROR_SOURCE_DECODING:
        return TINYPY_EXCEPTION_SYNTAX_ERROR;
    case TINYPY_ERROR_COMPILER_LIMIT:
        return TINYPY_EXCEPTION_RUNTIME_ERROR;
    case TINYPY_ERROR_RUNTIME:
    default:
        return TINYPY_EXCEPTION_RUNTIME_ERROR;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_error_kind_e __tinypy_exception_error_from_type(tinypy_vm_t *vm, tinypy_type_t *type) {
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_TYPE_ERROR]) != 0) {
        return TINYPY_ERROR_TYPE;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_UNBOUND_LOCAL_ERROR]) != 0) {
        return TINYPY_ERROR_UNBOUND_LOCAL;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_NAME_ERROR]) != 0) {
        return TINYPY_ERROR_NAME;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_KEYBOARD_INTERRUPT]) != 0) {
        return TINYPY_ERROR_INTERRUPT;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_ZERO_DIVISION_ERROR]) != 0) {
        return TINYPY_ERROR_ZERO_DIVISION;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_INDEX_ERROR]) != 0) {
        return TINYPY_ERROR_INDEX;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_KEY_ERROR]) != 0) {
        return TINYPY_ERROR_KEY;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_OVERFLOW_ERROR]) != 0) {
        return TINYPY_ERROR_OVERFLOW;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_IMPORT_ERROR]) != 0) {
        return TINYPY_ERROR_IMPORT;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_LOOKUP_ERROR]) != 0) {
        return TINYPY_ERROR_LOOKUP;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_UNICODE_DECODE_ERROR]) != 0) {
        return TINYPY_ERROR_UNICODE_DECODE;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR]) != 0) {
        return TINYPY_ERROR_UNICODE_ENCODE;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_TAB_ERROR]) != 0) {
        return TINYPY_ERROR_TAB;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_INDENTATION_ERROR]) != 0) {
        return TINYPY_ERROR_INDENTATION;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR]) != 0) {
        return TINYPY_ERROR_SYNTAX;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_VALUE_ERROR]) != 0) {
        return TINYPY_ERROR_VALUE;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_BUFFER_ERROR]) != 0) {
        return TINYPY_ERROR_BUFFER;
    }
    if (tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_MEMORY_ERROR]) != 0) {
        return TINYPY_ERROR_MEMORY;
    }
    return TINYPY_ERROR_RUNTIME;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_exceptions(tinypy_vm_t *vm) {
    size_t index;

    for (index = 0U; index < (size_t)TINYPY_EXCEPTION_TYPE_COUNT; ++index) {
        const tinypy_exception_definition_t *definition = &__tinypy_exception_definitions[index];
        tinypy_type_t *type;

        if (index == (size_t)TINYPY_EXCEPTION_BASE) {
            tinypy_native_type_spec_t spec;

            tinypy_native_type_spec_init(&spec);
            spec.payload_size = sizeof(tinypy_internal_exception_payload_t);
            spec.has_instance_dict = TINYPY_TRUE;
            spec.has_weakrefs = TINYPY_FALSE;
            type = tinypy_native_type_new(vm, definition->name, definition->name_size, NULL, 0U, NULL, &spec, NULL);
            type->release_references = __tinypy_exception_release_references;
            type->traverse_references = __tinypy_exception_release_references;
        }
        else {
            const tinypy_type_t *base = vm->exception_types[(size_t)definition->parent];

            type = tinypy_internal_type_new_configured(vm, definition->name, definition->name_size, &base, 1U, NULL, NULL, TINYPY_TRUE, TINYPY_FALSE, NULL);
        }

        if (index == (size_t)TINYPY_EXCEPTION_BASE) {
            tinypy_value_t *initializer = tinypy_native_function_new(vm, "__init__", 8U, __tinypy_exception_init, NULL, NULL);
            tinypy_value_t *new_function = tinypy_native_function_new(vm, "__new__", 7U, __tinypy_exception_new_method, NULL, NULL);
            tinypy_value_t *new_descriptor = tinypy_static_method_new(new_function);

            tinypy_type_set_attr(type, "__init__", 8U, initializer);
            tinypy_type_set_attr(type, "__new__", 7U, new_descriptor);
            TINYPY_DECREF(new_descriptor);
            TINYPY_DECREF(new_function);
            TINYPY_DECREF(initializer);
            tinypy_internal_initialize_exception_descriptors(type);
            __tinypy_exception_add_method(type, "__getitem__", 11U, __tinypy_exception_getitem_method);
            __tinypy_exception_add_method(type, "__getslice__", 12U, __tinypy_exception_getslice_method);
            __tinypy_exception_add_method(type, "__str__", 7U, __tinypy_exception_str_method);
            __tinypy_exception_add_method(type, "__unicode__", 11U, __tinypy_exception_unicode_method);
            __tinypy_exception_add_method(type, "__repr__", 8U, __tinypy_exception_repr_method);
            __tinypy_exception_add_method(type, "__reduce__", 10U, __tinypy_exception_reduce_method);
            __tinypy_exception_add_method(type, "__setstate__", 12U, __tinypy_exception_setstate_method);
            type->string = &__tinypy_exception_string;
        }
        vm->exception_types[index] = type;
        __tinypy_exception_builtin_set(vm, definition->name, definition->name_size, &type->base.base);
        TINYPY_DECREF(&type->base.base);
    }
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_ENVIRONMENT_ERROR], "errno", 5U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_ENVIRONMENT_ERROR], "strerror", 8U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_ENVIRONMENT_ERROR], "filename", 8U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYSTEM_EXIT], "code", 4U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR], "msg", 3U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR], "filename", 8U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR], "lineno", 6U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR], "offset", 6U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR], "text", 4U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR], "print_file_and_line", 19U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR], "encoding", 8U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR], "object", 6U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR], "start", 5U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR], "end", 3U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR], "reason", 6U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_DECODE_ERROR], "encoding", 8U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_DECODE_ERROR], "object", 6U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_DECODE_ERROR], "start", 5U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_DECODE_ERROR], "end", 3U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_DECODE_ERROR], "reason", 6U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR], "object", 6U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR], "start", 5U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR], "end", 3U);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR], "reason", 6U);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_exceptions_module(tinypy_vm_t *vm) {
    tinypy_value_t *module = tinypy_module_new(vm, "exceptions", 10U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "exceptions", 10U);
    size_t index;

    tinypy_module_add_value(module, "__name__", 8U, name);
    TINYPY_DECREF(name);
    for (index = 0U; index < (size_t)TINYPY_EXCEPTION_TYPE_COUNT; ++index) {
        const tinypy_exception_definition_t *definition = &__tinypy_exception_definitions[index];

        tinypy_module_add_value(module, definition->name, definition->name_size, &vm->exception_types[index]->base.base);
    }
    tinypy_internal_register_module(vm, "exceptions", 10U, module);
    TINYPY_DECREF(module);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_exception_instantiate(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *initializer_attribute;

    tinypy_value_t *instance = __tinypy_exception_allocate_blank(type);

    initializer_attribute = tinypy_type_get_attr(type, "__init__", 8U);
    if (initializer_attribute != NULL) {
        tinypy_value_t *initializer = tinypy_internal_descriptor_get_value(vm, initializer_attribute, instance, type, out_error);
        tinypy_value_t *result;

        if (initializer == NULL) {
            TINYPY_DECREF(instance);
            return NULL;
        }
        result = tinypy_call(initializer, args, kwargs, out_error);
        TINYPY_DECREF(initializer);
        if (result == NULL) {
            TINYPY_DECREF(instance);
            return NULL;
        }
        if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_NONE) {
            TINYPY_DECREF(result);
            TINYPY_DECREF(instance);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception __init__ must return None", out_error);
            return NULL;
        }
        TINYPY_DECREF(result);
    }
    else if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        TINYPY_DECREF(instance);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception constructor does not accept keyword arguments", out_error);
        return NULL;
    }
    return instance;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_exception_new(tinypy_type_t *type, tinypy_value_t *args, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    if (tinypy_type_is_subtype(type, type->vm->exception_types[TINYPY_EXCEPTION_BASE]) == 0) {
        tinypy_internal_make_vm_error(type->vm, TINYPY_ERROR_TYPE, "exception type must derive from BaseException", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_exception_instantiate(type, args, NULL, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_clear_raised(tinypy_vm_t *vm) {
    if (vm->raised_type != NULL) {
        TINYPY_DECREF(vm->raised_type);
    }
    if (vm->raised_value != NULL) {
        TINYPY_DECREF(vm->raised_value);
    }
    if (vm->raised_traceback != NULL) {
        TINYPY_DECREF(vm->raised_traceback);
    }
    vm->raised_type = NULL;
    vm->raised_value = NULL;
    vm->raised_traceback = NULL;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_clear_handled(tinypy_vm_t *vm) {
    if (vm->handled_type != NULL) {
        TINYPY_DECREF(vm->handled_type);
    }
    if (vm->handled_value != NULL) {
        TINYPY_DECREF(vm->handled_value);
    }
    if (vm->handled_traceback != NULL) {
        TINYPY_DECREF(vm->handled_traceback);
    }
    vm->handled_type = NULL;
    vm->handled_value = NULL;
    vm->handled_traceback = NULL;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_preserve_begin(tinypy_vm_t *vm, tinypy_internal_exception_state_t *state) {
    state->type = vm->raised_type;
    state->value = vm->raised_value;
    state->traceback = vm->raised_traceback;
    vm->raised_type = NULL;
    vm->raised_value = NULL;
    vm->raised_traceback = NULL;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_preserve_end(tinypy_vm_t *vm, tinypy_internal_exception_state_t *state) {
    tinypy_internal_exception_clear_raised(vm);
    vm->raised_type = state->type;
    vm->raised_value = state->value;
    vm->raised_traceback = state->traceback;
    state->type = NULL;
    state->value = NULL;
    state->traceback = NULL;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_set_raised(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *traceback) {
    TINYPY_INCREF(&value->type->base.base);
    TINYPY_INCREF(value);
    if (traceback != NULL) {
        TINYPY_INCREF(traceback);
    }
    tinypy_internal_exception_clear_raised(vm);
    vm->raised_type = &value->type->base.base;
    vm->raised_value = value;
    vm->raised_traceback = traceback;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_set_handled_from_raised(tinypy_vm_t *vm) {
    tinypy_internal_exception_clear_handled(vm);
    vm->handled_type = vm->raised_type;
    vm->handled_value = vm->raised_value;
    vm->handled_traceback = vm->raised_traceback;
    vm->raised_type = NULL;
    vm->raised_value = NULL;
    vm->raised_traceback = NULL;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_restore_raised_from_handled(tinypy_vm_t *vm) {
    tinypy_internal_exception_set_raised(vm, vm->handled_value, vm->handled_traceback);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_raise_kind(tinypy_vm_t *vm, tinypy_error_kind_e kind, const char *message) {
    tinypy_value_t *value;

    if (vm->exception_types[TINYPY_EXCEPTION_BASE] == NULL) {
        return;
    }
    size_t exception_string_length = __tinypy_exception_string_length(message);
    tinypy_value_t *text = tinypy_string_from_bytes(vm, message, exception_string_length);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &text, 1U);
    tinypy_exception_type_index_e exception_index_from_error = __tinypy_exception_index_from_error(kind);
    value = tinypy_internal_exception_instantiate(vm->exception_types[exception_index_from_error], args, NULL, NULL);
    if (value == NULL) {
        tinypy_internal_exception_clear_raised(vm);
        value = __tinypy_exception_allocate_blank(vm->exception_types[exception_index_from_error]);
        tinypy_internal_exception_payload_t *payload = __tinypy_exception_payload(value);

        __tinypy_exception_replace_args(payload, args);
        TINYPY_INCREF(args);
        TINYPY_DECREF(payload->message);
        payload->message = text;
        TINYPY_INCREF(text);
    }
    TINYPY_DECREF(args);
    TINYPY_DECREF(text);
    tinypy_internal_exception_set_raised(vm, value, NULL);
    TINYPY_DECREF(value);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_make_diagnostic(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    const char *message = "Python exception";
    tinypy_error_kind_e kind = TINYPY_ERROR_RUNTIME;
    tinypy_value_t *rendered = NULL;

    if (out_error == NULL || vm->raised_value == NULL) {
        return;
    }
    kind = __tinypy_exception_error_from_type(vm, vm->raised_value->type); {
        tinypy_value_t *message_value = __tinypy_exception_payload(vm->raised_value)->message;

        if (message_value != NULL && TINYPY_VALUE_KIND(message_value) == TINYPY_VALUE_STRING && TINYPY_TEXT_BYTE_SIZE(message_value) != 0U) {
            message = (const char *)TINYPY_STRING_OBJECT(message_value)->bytes;
        }
        else {
            tinypy_internal_exception_state_t state;

            tinypy_internal_exception_preserve_begin(vm, &state);
            rendered = __tinypy_exception_string(state.value, NULL);
            tinypy_internal_exception_preserve_end(vm, &state);
        }
    }
    if (rendered != NULL && TINYPY_VALUE_KIND(rendered) == TINYPY_VALUE_STRING) {
        size_t size = TINYPY_TEXT_BYTE_SIZE(rendered);
        char *terminated = (char *)tinypy_internal_vm_allocate(vm, size + 1U);

        if (size != 0U) {
            (void)memcpy(terminated, TINYPY_TEXT_BYTES(rendered), size);
        }
        terminated[size] = '\0';
        tinypy_internal_make_error(&vm->allocator, kind, terminated, out_error);
        tinypy_internal_vm_deallocate(vm, terminated, size + 1U);
        TINYPY_DECREF(rendered);
        return;
    }
    if (rendered != NULL) {
        TINYPY_DECREF(rendered);
    }
    tinypy_internal_make_error(&vm->allocator, kind, message, out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_raise_stop_iteration(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_value_t *empty = tinypy_tuple_from_items(vm, NULL, 0U);
    tinypy_value_t *exception = tinypy_exception_new(vm->exception_types[TINYPY_EXCEPTION_STOP_ITERATION], empty, out_error);

    TINYPY_DECREF(empty);
    if (exception != NULL) {
        (void)tinypy_exception_raise(exception, NULL, out_error);
        TINYPY_DECREF(exception);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_exception_consume_stop_iteration(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_value_t *raised_type = vm->raised_type;

    if (raised_type == NULL || tinypy_type_is_subtype((tinypy_type_t *)raised_type, vm->exception_types[TINYPY_EXCEPTION_STOP_ITERATION]) == 0) {
        return TINYPY_FALSE;
    }
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
    }
    tinypy_internal_exception_clear_raised(vm);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_exception_matches(tinypy_value_t *exception, tinypy_value_t *candidate, tinypy_error_t **out_error) {
    tinypy_type_t *exception_type;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(exception);
    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(candidate) == TINYPY_VALUE_TUPLE) {
        tinypy_value_t *const *iterator = TINYPY_TUPLE_ITERATOR_BEGIN(candidate);
        tinypy_value_t *const *iterator_end = TINYPY_TUPLE_ITERATOR_END(candidate);

        for (; iterator != iterator_end; ++iterator) {
            tinypy_value_t *item = *iterator;
            int32_t matched = tinypy_exception_matches(exception, item, out_error);

            if (matched != 0) {
                return matched;
            }
        }
        return 0;
    }
    if (__tinypy_exception_is_class(vm, candidate) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "catching classes that do not inherit from BaseException is not allowed", out_error);
        return -1;
    }
    if (__tinypy_exception_is_class(vm, exception) != 0) {
        exception_type = (tinypy_type_t *)exception;
    }
    else if (__tinypy_exception_is_instance(vm, exception) != 0) {
        exception_type = exception->type;
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception match operand is not an exception", out_error);
        return -1;
    }
    int32_t return_value_1 = tinypy_type_is_subtype(exception_type, (tinypy_type_t *)candidate) != 0 ? 1 : 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_exception_raise(tinypy_value_t *exception, tinypy_value_t *traceback, tinypy_error_t **out_error) {
    tinypy_value_t *value = exception;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(exception);
    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_exception_is_class(vm, exception) != 0) {
        tinypy_value_t *args = tinypy_tuple_from_items(vm, NULL, 0U);

        value = tinypy_internal_exception_instantiate((tinypy_type_t *)exception, args, NULL, out_error);
        TINYPY_DECREF(args);
        if (value == NULL) {
            return TINYPY_FALSE;
        }
    }
    else if (__tinypy_exception_is_instance(vm, exception) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exceptions must derive from BaseException", out_error);
        return TINYPY_FALSE;
    }
    if (traceback != NULL && TINYPY_VALUE_KIND(traceback) != TINYPY_VALUE_NONE && TINYPY_VALUE_KIND(traceback) != TINYPY_VALUE_TRACEBACK) {
        if (value != exception) {
            TINYPY_DECREF(value);
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "raise traceback must be a traceback or None", out_error);
        return TINYPY_FALSE;
    }
    tinypy_value_t *raised_traceback = NULL;
    if (traceback != NULL && TINYPY_VALUE_KIND(traceback) == TINYPY_VALUE_TRACEBACK) {
        raised_traceback = traceback;
    }
    tinypy_internal_exception_set_raised(vm, value, raised_traceback);
    if (value != exception) {
        TINYPY_DECREF(value);
    }
    tinypy_internal_exception_make_diagnostic(vm, out_error);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_raised_exception(const tinypy_vm_t *vm) {
    return vm->raised_value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_raised_exception_type(const tinypy_vm_t *vm) {
    return vm->raised_type;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_raised_traceback(const tinypy_vm_t *vm) {
    return vm->raised_traceback;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_handled_exception(const tinypy_vm_t *vm) {
    return vm->handled_value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_vm_has_error(const tinypy_vm_t *vm) {
    return vm->raised_value != NULL ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_vm_clear_error(tinypy_vm_t *vm) {
    tinypy_internal_exception_clear_raised(vm);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_vm_raise_error(tinypy_vm_t *vm, tinypy_error_kind_e kind, const char *message) {
    tinypy_internal_exception_raise_kind(vm, kind, message);
}
