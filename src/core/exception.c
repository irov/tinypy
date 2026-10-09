#include "tinypy/exception.h"

#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct tinypy_exception_definition_t {
    size_t name_offset;
    int32_t parent;
} tinypy_exception_definition_t;
static const tinypy_exception_definition_t __tinypy_exception_definitions[TINYPY_EXCEPTION_TYPE_COUNT] = { {offsetof(tinypy_vm_t, internal_base_exception_key), -1},
    {offsetof(tinypy_vm_t, internal_exception_key), TINYPY_EXCEPTION_BASE}, {offsetof(tinypy_vm_t, internal_standard_error_key), TINYPY_EXCEPTION_EXCEPTION},
    {offsetof(tinypy_vm_t, internal_arithmetic_error_key), TINYPY_EXCEPTION_STANDARD_ERROR}, {offsetof(tinypy_vm_t, internal_floating_point_error_key), TINYPY_EXCEPTION_ARITHMETIC_ERROR},
    {offsetof(tinypy_vm_t, internal_overflow_error_key), TINYPY_EXCEPTION_ARITHMETIC_ERROR}, {offsetof(tinypy_vm_t, internal_zero_division_error_key), TINYPY_EXCEPTION_ARITHMETIC_ERROR},
    {offsetof(tinypy_vm_t, internal_assertion_error_key), TINYPY_EXCEPTION_STANDARD_ERROR}, {offsetof(tinypy_vm_t, internal_attribute_error_key), TINYPY_EXCEPTION_STANDARD_ERROR},
    {offsetof(tinypy_vm_t, internal_environment_error_key), TINYPY_EXCEPTION_STANDARD_ERROR}, {offsetof(tinypy_vm_t, internal_io_error_key), TINYPY_EXCEPTION_ENVIRONMENT_ERROR},
    {offsetof(tinypy_vm_t, internal_os_error_key), TINYPY_EXCEPTION_ENVIRONMENT_ERROR}, {offsetof(tinypy_vm_t, internal_windows_error_key), TINYPY_EXCEPTION_OS_ERROR},
    {offsetof(tinypy_vm_t, internal_eof_error_key), TINYPY_EXCEPTION_STANDARD_ERROR}, {offsetof(tinypy_vm_t, internal_import_error_key), TINYPY_EXCEPTION_STANDARD_ERROR},
    {offsetof(tinypy_vm_t, internal_lookup_error_key), TINYPY_EXCEPTION_STANDARD_ERROR}, {offsetof(tinypy_vm_t, internal_index_error_key), TINYPY_EXCEPTION_LOOKUP_ERROR},
    {offsetof(tinypy_vm_t, internal_key_error_key), TINYPY_EXCEPTION_LOOKUP_ERROR}, {offsetof(tinypy_vm_t, internal_memory_error_key), TINYPY_EXCEPTION_STANDARD_ERROR},
    {offsetof(tinypy_vm_t, internal_name_error_key), TINYPY_EXCEPTION_STANDARD_ERROR}, {offsetof(tinypy_vm_t, internal_unbound_local_error_key), TINYPY_EXCEPTION_NAME_ERROR},
    {offsetof(tinypy_vm_t, internal_reference_error_key), TINYPY_EXCEPTION_STANDARD_ERROR}, {offsetof(tinypy_vm_t, internal_runtime_error_key), TINYPY_EXCEPTION_STANDARD_ERROR},
    {offsetof(tinypy_vm_t, internal_not_implemented_error_key), TINYPY_EXCEPTION_RUNTIME_ERROR}, {offsetof(tinypy_vm_t, internal_syntax_error_key), TINYPY_EXCEPTION_STANDARD_ERROR},
    {offsetof(tinypy_vm_t, internal_indentation_error_key), TINYPY_EXCEPTION_SYNTAX_ERROR}, {offsetof(tinypy_vm_t, internal_tab_error_key), TINYPY_EXCEPTION_INDENTATION_ERROR},
    {offsetof(tinypy_vm_t, internal_system_error_key), TINYPY_EXCEPTION_STANDARD_ERROR}, {offsetof(tinypy_vm_t, internal_type_error_key), TINYPY_EXCEPTION_STANDARD_ERROR},
    {offsetof(tinypy_vm_t, internal_value_error_key), TINYPY_EXCEPTION_STANDARD_ERROR}, {offsetof(tinypy_vm_t, internal_unicode_error_key), TINYPY_EXCEPTION_VALUE_ERROR},
    {offsetof(tinypy_vm_t, internal_unicode_decode_error_key), TINYPY_EXCEPTION_UNICODE_ERROR}, {offsetof(tinypy_vm_t, internal_unicode_encode_error_key), TINYPY_EXCEPTION_UNICODE_ERROR},
    {offsetof(tinypy_vm_t, internal_unicode_translate_error_key), TINYPY_EXCEPTION_UNICODE_ERROR}, {offsetof(tinypy_vm_t, internal_stop_iteration_key), TINYPY_EXCEPTION_EXCEPTION},
    {offsetof(tinypy_vm_t, internal_warning_key), TINYPY_EXCEPTION_EXCEPTION}, {offsetof(tinypy_vm_t, internal_user_warning_key), TINYPY_EXCEPTION_WARNING},
    {offsetof(tinypy_vm_t, internal_deprecation_warning_key), TINYPY_EXCEPTION_WARNING}, {offsetof(tinypy_vm_t, internal_pending_deprecation_warning_key), TINYPY_EXCEPTION_WARNING},
    {offsetof(tinypy_vm_t, internal_syntax_warning_key), TINYPY_EXCEPTION_WARNING}, {offsetof(tinypy_vm_t, internal_runtime_warning_key), TINYPY_EXCEPTION_WARNING},
    {offsetof(tinypy_vm_t, internal_future_warning_key), TINYPY_EXCEPTION_WARNING}, {offsetof(tinypy_vm_t, internal_import_warning_key), TINYPY_EXCEPTION_WARNING},
    {offsetof(tinypy_vm_t, internal_unicode_warning_key), TINYPY_EXCEPTION_WARNING}, {offsetof(tinypy_vm_t, internal_bytes_warning_key), TINYPY_EXCEPTION_WARNING},
    {offsetof(tinypy_vm_t, internal_system_exit_key), TINYPY_EXCEPTION_BASE}, {offsetof(tinypy_vm_t, internal_keyboard_interrupt_key), TINYPY_EXCEPTION_BASE},
    {offsetof(tinypy_vm_t, internal_generator_exit_key), TINYPY_EXCEPTION_BASE}, {offsetof(tinypy_vm_t, internal_buffer_error_key), TINYPY_EXCEPTION_STANDARD_ERROR}};
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_exception_string_length(const char *text) {
    size_t size = 0U;

    while (text[size] != '\0') {
        size += 1U;
    }
    return size;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_exception_type_set_none(tinypy_type_t *type, tinypy_value_t *name) {
    tinypy_value_t *none = TINYPY_RET_NONE(type->vm);

    tinypy_type_set_attr_key(type, name, none);
    TINYPY_DECREF(none);
}
//////////////////////////////////////////////////////////////////////////
/* Builtin exception types are named "exceptions.<name>", as their tp_name in
   Python 2.7; __name__ and __module__ are the parts of that dotted name. */
static void __tinypy_exception_type_qualify_name(tinypy_type_t *type, tinypy_value_t *module_name) {
    size_t module_size = TINYPY_TEXT_BYTE_SIZE(module_name);
    size_t name_size = module_size + 1U + type->name_size;
    uint8_t *name;
    tinypy_value_t *name_object = tinypy_internal_text_allocate_uninitialized(type->vm, TINYPY_VALUE_STRING, name_size, name_size, &name);

    (void)memcpy(name, TINYPY_TEXT_BYTES(module_name), module_size);
    name[module_size] = (uint8_t)'.';
    (void)memcpy(name + module_size + 1U, type->name, type->name_size);
    tinypy_value_t *previous = type->name_object;
    type->name_object = name_object;
    type->name = (const char *)name;
    type->name_size = name_size;
    TINYPY_DECREF(previous);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_is_class(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_bool_t return_value_1 = kind == TINYPY_VALUE_CLASS || (kind == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)value, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0) ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_is_instance(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_bool_t return_value_1 = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE || tinypy_type_is_subtype(value->type, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
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
/* Returns an owned reference to a field stored on the exception itself, or
   NULL while it is unset: the class-level None default does not count, the
   way the NULL members of the C exception objects do not. */
static tinypy_value_t *__tinypy_exception_field(tinypy_value_t *value, tinypy_value_t *name) {
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);

    if (dict_slot == NULL || *dict_slot == NULL) {
        return NULL;
    }
    tinypy_value_t *field = tinypy_internal_dict_get_optional(TINYPY_VALUE_VM(value), *dict_slot, name);
    if (field == NULL) {
        return NULL;
    }
    return TINYPY_RET(field);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_is_subtype(tinypy_value_t *value, tinypy_exception_type_index_e index) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    tinypy_bool_t return_value_1 = tinypy_type_is_subtype(value->type, vm->exception_types[index]) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_has_unicode_payload(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type;

    if (value->type->native_payload_size < sizeof(tinypy_internal_unicode_error_payload_t)) {
        return TINYPY_FALSE;
    }
    /* Reference ownership follows physical storage, even with a custom MRO. */
    for (type = value->type; type != NULL; type = type->base_type) {
        if (type == vm->exception_types[TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR] || type == vm->exception_types[TINYPY_EXCEPTION_UNICODE_DECODE_ERROR] || type == vm->exception_types[TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR]) {
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_exception_unicode_replace(tinypy_value_t **slot, tinypy_value_t *value) {
    tinypy_value_t *previous = *slot;

    if (value != NULL) {
        TINYPY_INCREF(value);
    }
    *slot = value;
    if (previous != NULL) {
        TINYPY_DECREF(previous);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_exception_unicode_clear(tinypy_internal_unicode_error_payload_t *payload) {
    __tinypy_exception_unicode_replace(&payload->encoding, NULL);
    __tinypy_exception_unicode_replace(&payload->object, NULL);
    __tinypy_exception_unicode_replace(&payload->reason, NULL);
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
/* EnvironmentError_str: a filename selects the three-field form; otherwise
   errno and strerror must both be set, even to None. */
static tinypy_value_t *__tinypy_exception_environment_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *fields[] = {
        __tinypy_exception_field(value, vm->internal_errno_key),
        __tinypy_exception_field(value, vm->internal_strerror_key),
        __tinypy_exception_field(value, vm->internal_filename_key),
    };
    tinypy_value_t *strings[] = {NULL, NULL, NULL};
    size_t field_count = fields[2] != NULL ? 3U : 2U;
    tinypy_value_t *result = NULL;
    size_t index;

    if (fields[2] == NULL && (fields[0] == NULL || fields[1] == NULL)) {
        goto cleanup;
    }
    for (index = 0U; index < field_count; ++index) {
        tinypy_value_t *field = fields[index] != NULL ? fields[index] : &vm->none_object.base;

        strings[index] = index == 2U ? tinypy_object_repr(field, out_error) : tinypy_object_str(field, out_error);
        if (strings[index] == NULL) {
            goto cleanup;
        }
    }
    tinypy_exception_text_part_t parts[] = {
        {"[Errno ", 7U},
        {TINYPY_TEXT_BYTES(strings[0]), TINYPY_TEXT_BYTE_SIZE(strings[0])},
        {"] ", 2U},
        {TINYPY_TEXT_BYTES(strings[1]), TINYPY_TEXT_BYTE_SIZE(strings[1])},
        {": ", 2U},
        {NULL, 0U},
    };

    if (field_count == 3U) {
        parts[5].bytes = TINYPY_TEXT_BYTES(strings[2]);
        parts[5].size = TINYPY_TEXT_BYTE_SIZE(strings[2]);
    }
    result = __tinypy_exception_join(vm, parts, field_count == 3U ? 6U : 4U, out_error);
cleanup:
    for (index = 0U; index < sizeof(fields) / sizeof(fields[0]); ++index) {
        if (strings[index] != NULL) {
            TINYPY_DECREF(strings[index]);
        }
        if (fields[index] != NULL) {
            TINYPY_DECREF(fields[index]);
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* SyntaxError_str: the message is followed by the base name of a str
   filename and an int line number, formatted as C strings. */
static tinypy_value_t *__tinypy_exception_syntax_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *message = __tinypy_exception_field(value, vm->internal_msg_key);
    tinypy_value_t *message_string = tinypy_object_str(message != NULL ? message : &vm->none_object.base, out_error);

    if (message != NULL) {
        TINYPY_DECREF(message);
    }
    if (message_string == NULL || TINYPY_VALUE_KIND(message_string) != TINYPY_VALUE_STRING) {
        return message_string;
    }
    tinypy_value_t *filename = __tinypy_exception_field(value, vm->internal_filename_key);
    tinypy_value_t *line = __tinypy_exception_field(value, vm->internal_lineno_key);
    tinypy_bool_t has_filename = filename != NULL && TINYPY_VALUE_KIND(filename) == TINYPY_VALUE_STRING ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_bool_t has_line = line != NULL && (TINYPY_VALUE_KIND(line) == TINYPY_VALUE_INTEGER || TINYPY_VALUE_KIND(line) == TINYPY_VALUE_BOOL) ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_value_t *result = message_string;

    if (has_filename != 0 || has_line != 0) {
        const char *message_bytes = (const char *)TINYPY_TEXT_BYTES(message_string);
        const char *message_end = (const char *)memchr(message_bytes, 0, TINYPY_TEXT_BYTE_SIZE(message_string));
        char line_buffer[32];
        int line_size = has_line != 0 ? snprintf(line_buffer, sizeof(line_buffer), "%" PRId64, TINYPY_INTEGER_VALUE(line)) : 0;
        tinypy_exception_text_part_t parts[] = {
            {message_bytes, message_end != NULL ? (size_t)(message_end - message_bytes) : TINYPY_TEXT_BYTE_SIZE(message_string)},
            {" (", 2U},
            {NULL, 0U},
            {", ", has_filename != 0 && has_line != 0 ? 2U : 0U},
            {"line ", has_line != 0 ? 5U : 0U},
            {line_buffer, line_size > 0 ? (size_t)line_size : 0U},
            {")", 1U},
        };

        if (has_filename != 0) {
            const char *name = (const char *)TINYPY_TEXT_BYTES(filename);
            const char *name_end = (const char *)memchr(name, 0, TINYPY_TEXT_BYTE_SIZE(filename));
            size_t name_size = name_end != NULL ? (size_t)(name_end - name) : TINYPY_TEXT_BYTE_SIZE(filename);
            size_t base = name_size;

            while (base != 0U && name[base - 1U] != '/') {
                base -= 1U;
            }
            parts[2].bytes = name + base;
            parts[2].size = name_size - base;
        }
        result = __tinypy_exception_join(vm, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        TINYPY_DECREF(message_string);
    }
    if (filename != NULL) {
        TINYPY_DECREF(filename);
    }
    if (line != NULL) {
        TINYPY_DECREF(line);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_unicode_field_string(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (value == NULL) {
        tinypy_value_t *result = tinypy_string_from_bytes(vm, "<NULL>", 6U);
        return result;
    }
    TINYPY_INCREF(value);
    tinypy_value_t *result = tinypy_object_str(value, out_error);
    TINYPY_DECREF(value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_exception_unicode_field_size(tinypy_value_t *value) {
    size_t size = TINYPY_TEXT_BYTE_SIZE(value);
    const uint8_t *bytes = TINYPY_TEXT_BYTES(value);
    size_t index;

    if (size > 400U) {
        size = 400U;
    }
    for (index = 0U; index < size; ++index) {
        if (bytes[index] == 0U) {
            break;
        }
    }
    return index;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_unicode_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_unicode_error_payload_t *payload = (tinypy_internal_unicode_error_payload_t *)tinypy_native_instance_payload(value);
    tinypy_bool_t decode = __tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_UNICODE_DECODE_ERROR);
    tinypy_bool_t translate = __tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR);
    tinypy_value_t *encoding = NULL;
    tinypy_value_t *object = NULL;
    tinypy_value_t *reason;
    int64_t start;
    int64_t end;
    char positions[96];
    char character[16];
    int position_size;
    tinypy_exception_text_part_t parts[12];
    size_t part_count = 0U;
    tinypy_value_t *result = NULL;

    if (payload->object == NULL) {
        tinypy_value_t *empty = tinypy_unicode_from_utf8(vm, NULL, 0U);
        return empty;
    }
    reason = __tinypy_exception_unicode_field_string(vm, payload->reason, out_error);
    if (reason == NULL) {
        return NULL;
    }
    if (translate == 0) {
        encoding = __tinypy_exception_unicode_field_string(vm, payload->encoding, out_error);
        if (encoding == NULL) {
            goto cleanup;
        }
        parts[part_count++] = (tinypy_exception_text_part_t){"'", 1U};
        parts[part_count++] = (tinypy_exception_text_part_t){TINYPY_TEXT_BYTES(encoding), __tinypy_exception_unicode_field_size(encoding)};
        parts[part_count++] = (tinypy_exception_text_part_t){decode != 0 ? "' codec can't decode " : "' codec can't encode ", 21U};
    }
    else {
        parts[part_count++] = (tinypy_exception_text_part_t){"can't translate ", 16U};
    }
    object = payload->object;
    if (object == NULL || TINYPY_VALUE_KIND(object) != (decode != 0 ? TINYPY_VALUE_STRING : TINYPY_VALUE_UNICODE)) {
        object = NULL;
        goto cleanup;
    }
    TINYPY_INCREF(object);
    start = payload->start;
    end = payload->end;
    if (start < INT64_MAX && end == start + 1 && start >= 0 && (uint64_t)start < (uint64_t)TINYPY_SIZED_SIZE(object)) {
        if (decode != 0) {
            const uint8_t byte = TINYPY_TEXT_BYTES(object)[(size_t)start];

            position_size = snprintf(positions, sizeof(positions), "byte 0x%02x in position %" PRId64, (unsigned int)byte, start);
            parts[part_count++] = (tinypy_exception_text_part_t){positions, (size_t)position_size};
        }
        else {
            size_t byte_offset = tinypy_internal_unicode_byte_offset(object, (size_t)start);
            uint32_t code_point = 0U;
            int character_size;

            (void)tinypy_internal_utf8_decode(TINYPY_TEXT_BYTES(object) + byte_offset, TINYPY_TEXT_BYTE_SIZE(object) - byte_offset, &code_point);
            character_size = snprintf(character, sizeof(character), code_point <= UINT32_C(255) ? "u'\\x%02x'" : (code_point <= UINT32_C(65535) ? "u'\\u%04x'" : "u'\\U%08x'"), (unsigned int)code_point);
            parts[part_count++] = (tinypy_exception_text_part_t){"character ", 10U};
            parts[part_count++] = (tinypy_exception_text_part_t){character, (size_t)character_size};
            position_size = snprintf(positions, sizeof(positions), " in position %" PRId64, start);
            parts[part_count++] = (tinypy_exception_text_part_t){positions, (size_t)position_size};
        }
    }
    else {
        int64_t last = end != INT64_MIN ? end - 1 : INT64_MAX;

        position_size = snprintf(positions, sizeof(positions), "%s in position %" PRId64 "-%" PRId64, decode != 0 ? "bytes" : "characters", start, last);
        parts[part_count++] = (tinypy_exception_text_part_t){positions, (size_t)position_size};
    }
    parts[part_count++] = (tinypy_exception_text_part_t){": ", 2U};
    parts[part_count++] = (tinypy_exception_text_part_t){TINYPY_TEXT_BYTES(reason), __tinypy_exception_unicode_field_size(reason)};
    result = __tinypy_exception_join(vm, parts, part_count, out_error);
cleanup:
    if (object != NULL) {
        TINYPY_DECREF(object);
    }
    if (encoding != NULL) {
        TINYPY_DECREF(encoding);
    }
    TINYPY_DECREF(reason);
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
    if (__tinypy_exception_has_unicode_payload(value) != 0) {
        tinypy_internal_unicode_error_payload_t *unicode = (tinypy_internal_unicode_error_payload_t *)payload;

        if (unicode->encoding != NULL) {
            visit(unicode->encoding, user_data);
        }
        if (unicode->object != NULL) {
            visit(unicode->object, user_data);
        }
        if (unicode->reason != NULL) {
            visit(unicode->reason, user_data);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_allocate_blank(tinypy_type_t *type) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *instance = tinypy_internal_object_allocate(vm, type, type->basic_size);
    tinypy_internal_exception_payload_t *payload = __tinypy_exception_payload(instance);

    payload->args = TINYPY_RET_EMPTY_TUPLE(vm);
    payload->message = TINYPY_RET_EMPTY_STRING(vm);
    return instance;
}
//////////////////////////////////////////////////////////////////////////
/* The families whose tp_str is not BaseException_str in Python 2.7. */
static tinypy_bool_t __tinypy_exception_has_own_string(tinypy_value_t *value) {
    tinypy_bool_t result = __tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_KEY_ERROR) != 0
        || __tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_ENVIRONMENT_ERROR) != 0
        || __tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_SYNTAX_ERROR) != 0
        || __tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR) != 0
        || __tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_UNICODE_DECODE_ERROR) != 0
        || __tinypy_exception_is_subtype(value, TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR) != 0;

    return result;
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
        tinypy_value_t *return_value_1 = TINYPY_RET_EMPTY_STRING(TINYPY_VALUE_VM(value));
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
    /* wrap_sq_item of BaseException_getitem: the argument is an index into
       the arguments. */
    int64_t index;
    if (tinypy_internal_number_as_index(TINYPY_TUPLE_GET(args, 1U), TINYPY_ERROR_OVERFLOW, &index, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *exception_args = __tinypy_exception_payload(TINYPY_TUPLE_GET(args, 0U))->args;
    tinypy_value_t *position = tinypy_integer_from_i64(vm, index);
    tinypy_value_t *result = tinypy_internal_get_item_builtin(exception_args, position, out_error);

    TINYPY_DECREF(position);
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
    tinypy_value_t *string_method = tinypy_type_get_attr_key(self->type, vm->internal_special_str_key);
    tinypy_value_t *base_string_method = tinypy_type_get_attr_key(vm->exception_types[TINYPY_EXCEPTION_BASE], vm->internal_special_str_key);

    /* BaseException_unicode converts what an overridden __str__ returns,
       which may be unicode, and the text of the families with their own str. */
    if (string_method != base_string_method || __tinypy_exception_has_own_string(self) != 0) {
        tinypy_bool_t handled;
        tinypy_value_t *text = string_method != base_string_method
                                   ? tinypy_internal_call_conversion(self, vm->internal_special_str_key, &handled, out_error)
                                   : __tinypy_exception_string(self, out_error);

        if (text == NULL) {
            return NULL;
        }
        tinypy_value_t *result = tinypy_internal_object_unicode(text, out_error);
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
    size_t name_size;
    const char *name = tinypy_internal_type_short_name(self->type, &name_size);
    if (args_size > SIZE_MAX - name_size) {
        TINYPY_DECREF(args_repr);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "exception representation is too large", out_error);
        return NULL;
    }
    size_t result_size = name_size + args_size;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, result_size, result_size, &result_bytes, out_error);

    if (result == NULL) {
        TINYPY_DECREF(args_repr);
        return NULL;
    }

    (void)memcpy(result_bytes, name, name_size);
    (void)memcpy(result_bytes + name_size, args_bytes, args_size);
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
        tinypy_value_t *result = TINYPY_RET_NONE(vm);

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
    tinypy_value_t *result = TINYPY_RET_NONE(vm);

    return result;
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

    if (argument_count == 2U || argument_count == 3U) {
        tinypy_instance_set_attr_key(self, vm->internal_errno_key, items[0]);
        tinypy_instance_set_attr_key(self, vm->internal_strerror_key, items[1]);
        if (argument_count == 3U) {
            tinypy_value_t *normalized = tinypy_tuple_from_items(vm, items, 2U);

            tinypy_instance_set_attr_key(self, vm->internal_filename_key, items[2]);
            __tinypy_exception_replace_args(payload, normalized);
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_init_system_exit(tinypy_value_t *self, tinypy_value_t *const *items, size_t argument_count, tinypy_internal_exception_payload_t *payload) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
    tinypy_value_t *code;

    (void)items;
    if (argument_count == 0U) {
        return TINYPY_TRUE;
    }
    code = argument_count == 1U ? TINYPY_TUPLE_GET(payload->args, 0U) : payload->args;
    tinypy_instance_set_attr_key(self, vm->internal_code_key, code);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_init_syntax(tinypy_value_t *self, tinypy_value_t *const *items, size_t argument_count, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
    tinypy_value_t *const location_names[4] = {vm->internal_filename_key, vm->internal_lineno_key, vm->internal_offset_key, vm->internal_text_key};
    size_t index;

    if (argument_count != 0U) {
        tinypy_instance_set_attr_key(self, vm->internal_msg_key, items[0]);
    }
    if (argument_count == 2U) {
        tinypy_value_t *conversion_args = tinypy_tuple_from_items(vm, &items[1], 1U);
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
            tinypy_instance_set_attr_key(self, location_names[index], TINYPY_TUPLE_GET(location, index));
        }
        TINYPY_DECREF(location);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* The O! conversion of PyArg_ParseTuple for the argument at index. */
static tinypy_bool_t __tinypy_exception_require_unicode_argument(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t index, tinypy_value_type_e expected, tinypy_error_t **out_error) {
    tinypy_value_t *value = items[index];

    if (TINYPY_VALUE_KIND(value) == expected) {
        return TINYPY_TRUE;
    }
    char position = (char)('1' + index);
    tinypy_message_part_t type_name = TINYPY_MESSAGE_PART_TYPE_NAME(value);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE) {
        type_name = (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("None");
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("argument "),
        {&position, 1U},
        TINYPY_MESSAGE_PART_LITERAL(" must be "),
        {vm->types[expected].name, vm->types[expected].name_size},
        TINYPY_MESSAGE_PART_LITERAL(", not "),
        type_name
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_exception_init_unicode(tinypy_value_t *self, tinypy_value_t *const *items, size_t argument_count, tinypy_bool_t decode, tinypy_bool_t translate, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
    tinypy_internal_unicode_error_payload_t *payload = (tinypy_internal_unicode_error_payload_t *)tinypy_native_instance_payload(self);
    size_t expected = translate != 0 ? 4U : 5U;
    size_t object_index = translate != 0 ? 0U : 1U;
    size_t start_index = translate != 0 ? 1U : 2U;
    size_t end_index = translate != 0 ? 2U : 3U;
    size_t reason_index = translate != 0 ? 3U : 4U;
    int64_t position;

    __tinypy_exception_unicode_clear(payload);

    if (argument_count != expected) {
        tinypy_internal_make_arity_error(vm, NULL, 0U, argument_count, expected, expected, TINYPY_ARITY_STYLE_PARSED, out_error);
        goto failure;
    }
    if (translate == 0 && __tinypy_exception_require_unicode_argument(vm, items, 0U, TINYPY_VALUE_STRING, out_error) == 0) {
        goto failure;
    }
    if (translate == 0) {
        __tinypy_exception_unicode_replace(&payload->encoding, items[0]);
    }
    if (__tinypy_exception_require_unicode_argument(vm, items, object_index, decode != 0 ? TINYPY_VALUE_STRING : TINYPY_VALUE_UNICODE, out_error) == 0) {
        goto failure;
    }
    __tinypy_exception_unicode_replace(&payload->object, items[object_index]);
    if (tinypy_internal_integer_as_ssize(items[start_index], &position, out_error) == 0) {
        goto failure;
    }
    payload->start = position;
    if (tinypy_internal_integer_as_ssize(items[end_index], &position, out_error) == 0) {
        goto failure;
    }
    payload->end = position;
    if (__tinypy_exception_require_unicode_argument(vm, items, reason_index, TINYPY_VALUE_STRING, out_error) == 0) {
        goto failure;
    }
    __tinypy_exception_unicode_replace(&payload->reason, items[reason_index]);
    return TINYPY_TRUE;
failure:
    __tinypy_exception_unicode_clear(payload);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* BaseException.__init__ of an instance: the arguments become its args and
   fill the extra fields of the built-in exception families. */
static tinypy_bool_t __tinypy_exception_initialize(tinypy_value_t *self, tinypy_value_t *arguments, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_message_part_t name[3];

        tinypy_internal_type_message_name(self->type, name);
        tinypy_message_part_t parts[] = {
            name[0],
            name[1],
            name[2],
            TINYPY_MESSAGE_PART_LITERAL(" does not take keyword arguments"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    tinypy_internal_exception_payload_t *payload = __tinypy_exception_payload(self);
    tinypy_value_t *const *items = tinypy_internal_tuple_items(arguments);
    size_t argument_count = TINYPY_TUPLE_SIZE(arguments);

    TINYPY_INCREF(arguments);
    __tinypy_exception_replace_args(payload, arguments);
    if (argument_count == 1U) {
        TINYPY_INCREF(items[0]);
        if (payload->message != NULL) {
            TINYPY_DECREF(payload->message);
        }
        payload->message = items[0];
    }
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
    return initialized;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_init(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count = TINYPY_TUPLE_SIZE(args);

    (void)user_data;
    if (count == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception constructor does not accept keyword arguments", out_error);
        return NULL;
    }
    tinypy_value_t *const *items = tinypy_internal_tuple_items(args);
    tinypy_value_t *arguments = tinypy_tuple_from_items(vm, items + 1U, count - 1U);
    tinypy_bool_t initialized = __tinypy_exception_initialize(items[0], arguments, kwargs, out_error);

    TINYPY_DECREF(arguments);
    if (initialized == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_exception_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    (void)kwargs;
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
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
    tinypy_value_t *module_name = TINYPY_RET(vm->internal_exception_module_name);
    size_t index;

    for (index = 0U; index < (size_t)TINYPY_EXCEPTION_TYPE_COUNT; ++index) {
        const tinypy_exception_definition_t *definition = &__tinypy_exception_definitions[index];
        tinypy_type_t *type;
        tinypy_value_t *type_name = *(tinypy_value_t **)((uint8_t *)vm + definition->name_offset);

        if (index == (size_t)TINYPY_EXCEPTION_BASE) {
            tinypy_native_type_spec_t spec;

            tinypy_native_type_spec_init(&spec);
            spec.payload_size = sizeof(tinypy_internal_exception_payload_t);
            spec.has_instance_dict = TINYPY_TRUE;
            spec.has_weakrefs = TINYPY_FALSE;
            type = tinypy_native_type_new_key(type_name, NULL, 0U, NULL, &spec, NULL);
            type->release_references = __tinypy_exception_release_references;
            type->traverse_references = __tinypy_exception_release_references;
        }
        else {
            const tinypy_type_t *base = vm->exception_types[(size_t)definition->parent];

            type = tinypy_internal_type_new_configured(type_name, &base, 1U, NULL, NULL, TINYPY_TRUE, TINYPY_FALSE, NULL);
        }
        if (index == (size_t)TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR || index == (size_t)TINYPY_EXCEPTION_UNICODE_DECODE_ERROR || index == (size_t)TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR) {
            type->native_payload_size = sizeof(tinypy_internal_unicode_error_payload_t);
            type->native_spec.payload_size = type->native_payload_size;
            type->basic_size = type->native_payload_offset + type->native_payload_size;
            type->basic_size = (type->basic_size + sizeof(void *) - 1U) & ~(sizeof(void *) - 1U);
            type->slots_offset = type->basic_size;
        }

        if (index == (size_t)TINYPY_EXCEPTION_BASE) {
            tinypy_internal_type_add_method(type, vm->internal_special_init_key, __tinypy_exception_init, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
            tinypy_value_t *new_function = tinypy_native_function_new_key(vm->internal_special_new_key, __tinypy_exception_new_method, NULL, NULL);
            tinypy_value_t *new_descriptor = tinypy_static_method_new(new_function);

            tinypy_type_set_attr_key(type, type->vm->internal_special_new_key, new_descriptor);
            TINYPY_DECREF(new_descriptor);
            TINYPY_DECREF(new_function);
            tinypy_internal_initialize_exception_descriptors(type);
            tinypy_internal_type_add_method(type, vm->internal_special_getitem_key, __tinypy_exception_getitem_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
            tinypy_internal_type_add_method(type, vm->internal_special_getslice_key, __tinypy_exception_getslice_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
            tinypy_internal_type_add_method(type, vm->internal_special_str_key, __tinypy_exception_str_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
            tinypy_internal_type_add_method(type, vm->internal_special_unicode_key, __tinypy_exception_unicode_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
            tinypy_internal_type_add_method(type, vm->internal_special_repr_key, __tinypy_exception_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
            tinypy_internal_type_add_method(type, vm->internal_special_reduce_key, __tinypy_exception_reduce_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
            tinypy_internal_type_add_method(type, vm->internal_special_setstate_key, __tinypy_exception_setstate_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
            type->string = &__tinypy_exception_string;
        }
        __tinypy_exception_type_qualify_name(type, module_name);
        type->flags |= TINYPY_TYPE_FLAG_IMMUTABLE;
        vm->exception_types[index] = type;
#if defined(_WIN32)
        tinypy_dict_set(vm->builtins, type_name, &type->base.base);
#else
        if (index != (size_t)TINYPY_EXCEPTION_WINDOWS_ERROR) {
            tinypy_dict_set(vm->builtins, type_name, &type->base.base);
        }
#endif
#if !defined(_WIN32)
        if (index == (size_t)TINYPY_EXCEPTION_WINDOWS_ERROR) {
            /* The VM exception table is the sole owner of this unexported type. */
            continue;
        }
#endif
        TINYPY_DECREF(&type->base.base);
    }
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_ENVIRONMENT_ERROR], vm->internal_errno_key);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_ENVIRONMENT_ERROR], vm->internal_strerror_key);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_ENVIRONMENT_ERROR], vm->internal_filename_key);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYSTEM_EXIT], vm->internal_code_key);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR], vm->internal_msg_key);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR], vm->internal_filename_key);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR], vm->internal_lineno_key);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR], vm->internal_offset_key);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR], vm->internal_text_key);
    __tinypy_exception_type_set_none(vm->exception_types[TINYPY_EXCEPTION_SYNTAX_ERROR], vm->internal_print_file_and_line_key);
    tinypy_internal_initialize_exception_descriptors(vm->exception_types[TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR]);
    tinypy_internal_initialize_exception_descriptors(vm->exception_types[TINYPY_EXCEPTION_UNICODE_DECODE_ERROR]);
    tinypy_internal_initialize_exception_descriptors(vm->exception_types[TINYPY_EXCEPTION_UNICODE_TRANSLATE_ERROR]);
    vm->emergency_memory_error = __tinypy_exception_allocate_blank(vm->exception_types[TINYPY_EXCEPTION_MEMORY_ERROR]);
    TINYPY_DECREF(module_name);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_exceptions_module(tinypy_vm_t *vm) {
    tinypy_value_t *module = tinypy_module_new_key(vm->internal_exception_module_name);
    tinypy_value_t *name = TINYPY_RET(vm->internal_exception_module_name);
    size_t index;

    tinypy_module_add_value_key(module, vm->internal_special_name_key, name);
    TINYPY_DECREF(name);
    for (index = 0U; index < (size_t)TINYPY_EXCEPTION_TYPE_COUNT; ++index) {
        const tinypy_exception_definition_t *definition = &__tinypy_exception_definitions[index];

#if !defined(_WIN32)
        if (index == (size_t)TINYPY_EXCEPTION_WINDOWS_ERROR) {
            continue;
        }
#endif
        tinypy_value_t *internal_name_key = *(tinypy_value_t **)((uint8_t *)vm + definition->name_offset);
        tinypy_module_add_value_key(module, internal_name_key, &vm->exception_types[index]->base.base);
    }
    tinypy_internal_register_module(vm, vm->internal_exception_module_name, module);
    TINYPY_DECREF(module);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_exception_instantiate(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *initializer_attribute;

    tinypy_value_t *instance;
    tinypy_value_t *new_attribute = tinypy_internal_type_constructor_new(type);
    tinypy_value_t *base_new = tinypy_internal_type_constructor_new(vm->exception_types[TINYPY_EXCEPTION_BASE]);
    if (new_attribute != NULL && new_attribute != base_new) {
        tinypy_value_t *constructor = tinypy_internal_descriptor_get_value(vm, new_attribute, NULL, type, out_error);
        if (constructor == NULL) {
            return NULL;
        }
        tinypy_value_t *new_args = tinypy_internal_tuple_prepend_checked(vm, &type->base.base, args, out_error);
        instance = new_args != NULL ? tinypy_call(constructor, new_args, kwargs, out_error) : NULL;
        if (new_args != NULL) {
            TINYPY_DECREF(new_args);
        }
        TINYPY_DECREF(constructor);
        if (instance == NULL || tinypy_type_is_subtype(instance->type, type) == 0) {
            return instance;
        }
    }
    else {
        instance = __tinypy_exception_allocate_blank(type);
    }

    initializer_attribute = tinypy_internal_type_constructor_init(instance->type);
    tinypy_value_t *base_init = tinypy_internal_type_constructor_init(vm->exception_types[TINYPY_EXCEPTION_BASE]);
    if (initializer_attribute != NULL && initializer_attribute == base_init) {
        /* The built-in __init__ runs under the call guard of tinypy_call but
           without a bound method, and keeps an exact args tuple as it is. */
        if (__tinypy_internal_call_enter(vm, out_error) == 0) {
            TINYPY_DECREF(instance);
            return NULL;
        }
        tinypy_value_t *const *items = tinypy_internal_tuple_items(args);
        tinypy_value_t *arguments = args->type == &vm->types[TINYPY_VALUE_TUPLE] ? TINYPY_RET(args) : tinypy_tuple_from_items(vm, items, TINYPY_TUPLE_SIZE(args));
        tinypy_bool_t initialized = __tinypy_exception_initialize(instance, arguments, kwargs, out_error);

        __tinypy_internal_call_leave(vm);
        TINYPY_DECREF(arguments);
        if (initialized == 0) {
            TINYPY_DECREF(instance);
            return NULL;
        }
    }
    else if (initializer_attribute != NULL) {
        tinypy_value_t *initializer = tinypy_internal_descriptor_get_value(vm, initializer_attribute, instance, instance->type, out_error);
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
/* do_raise rejects an exception class whose call returned something other
   than an exception instance; calling a classic class always yields one. */
tinypy_bool_t tinypy_internal_exception_check_normalized(tinypy_value_t *exception_class, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    if (__tinypy_exception_is_instance(vm, value) != 0) {
        return TINYPY_TRUE;
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("calling "),
        {((tinypy_type_t *)exception_class)->name, ((tinypy_type_t *)exception_class)->name_size},
        TINYPY_MESSAGE_PART_LITERAL("() should have returned an instance of BaseException, not '"),
        TINYPY_MESSAGE_PART_TYPE_NAME(value),
        TINYPY_MESSAGE_PART_LITERAL("'"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return TINYPY_FALSE;
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
    tinypy_value_t *type = vm->raised_type;
    tinypy_value_t *value = vm->raised_value;
    tinypy_value_t *traceback = vm->raised_traceback;

    vm->raised_type = NULL;
    vm->raised_value = NULL;
    vm->raised_traceback = NULL;
    if (type != NULL) {
        TINYPY_DECREF(type);
    }
    if (value != NULL) {
        TINYPY_DECREF(value);
    }
    if (traceback != NULL) {
        TINYPY_DECREF(traceback);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_clear_handled(tinypy_vm_t *vm) {
    tinypy_value_t *type = vm->handled_type;
    tinypy_value_t *value = vm->handled_value;
    tinypy_value_t *traceback = vm->handled_traceback;

    vm->handled_type = NULL;
    vm->handled_value = NULL;
    vm->handled_traceback = NULL;
    if (type != NULL) {
        TINYPY_DECREF(type);
    }
    if (value != NULL) {
        TINYPY_DECREF(value);
    }
    if (traceback != NULL) {
        TINYPY_DECREF(traceback);
    }
    tinypy_internal_sys_publish_handled_exception(vm, &vm->none_object.base, &vm->none_object.base, &vm->none_object.base);
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
void tinypy_internal_exception_set_raised_type(tinypy_vm_t *vm, tinypy_value_t *type_value, tinypy_value_t *value, tinypy_value_t *traceback) {
    TINYPY_INCREF(type_value);
    TINYPY_INCREF(value);
    if (traceback != NULL) {
        TINYPY_INCREF(traceback);
    }
    tinypy_internal_exception_clear_raised(vm);
    vm->raised_type = type_value;
    vm->raised_value = value;
    vm->raised_traceback = traceback;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_set_raised(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *traceback) {
    tinypy_value_t *type_value = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE ? TINYPY_OLD_INSTANCE_OBJECT(value)->class_object : &value->type->base.base;

    tinypy_internal_exception_set_raised_type(vm, type_value, value, traceback);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_set_handled_from_raised(tinypy_vm_t *vm) {
    tinypy_internal_frame_save_handled(vm);
    tinypy_value_t *type = vm->raised_type;
    tinypy_value_t *value = vm->raised_value;
    tinypy_value_t *traceback = vm->raised_traceback;

    vm->raised_type = NULL;
    vm->raised_value = NULL;
    vm->raised_traceback = NULL;
    tinypy_internal_exception_restore_handled(vm, type, value, traceback);
}
//////////////////////////////////////////////////////////////////////////
/* Takes over the three references and publishes sys.exc_* the way
   set_exc_info and reset_exc_info do in CPython 2.7. */
void tinypy_internal_exception_restore_handled(tinypy_vm_t *vm, tinypy_value_t *type, tinypy_value_t *value, tinypy_value_t *traceback) {
    tinypy_value_t *previous_type = vm->handled_type;
    tinypy_value_t *previous_value = vm->handled_value;
    tinypy_value_t *previous_traceback = vm->handled_traceback;

    vm->handled_type = type;
    vm->handled_value = value;
    vm->handled_traceback = traceback;
    if (previous_type != NULL) {
        TINYPY_DECREF(previous_type);
    }
    if (previous_value != NULL) {
        TINYPY_DECREF(previous_value);
    }
    if (previous_traceback != NULL) {
        TINYPY_DECREF(previous_traceback);
    }
    tinypy_internal_sys_publish_handled_exception(vm, type != NULL ? type : &vm->none_object.base, value, traceback);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_restore_raised_from_handled(tinypy_vm_t *vm) {
    tinypy_internal_exception_set_raised_type(vm, vm->handled_type, vm->handled_value, vm->handled_traceback);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_raise_kind(tinypy_vm_t *vm, tinypy_error_kind_e kind, const char *message) {
    tinypy_value_t *value;

    if (vm->exception_types[TINYPY_EXCEPTION_BASE] == NULL) {
        return;
    }
    /* Reporting exhausted heap must not enter constructors or checked
       allocations. This VM-owned instance is reserved during initialization. */
    if (kind == TINYPY_ERROR_MEMORY && vm->emergency_memory_error != NULL) {
        tinypy_internal_exception_set_raised(vm, vm->emergency_memory_error, NULL);
        return;
    }
    size_t exception_string_length = __tinypy_exception_string_length(message);
    tinypy_value_t *text = tinypy_string_from_bytes(vm, message, exception_string_length);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &text, 1U);
    tinypy_exception_type_index_e exception_index_from_error = __tinypy_exception_index_from_error(kind);
    /* At the recursion limit, calling even the built-in __init__ would enter
       the call guard again while constructing its own RuntimeError. */
    value = vm->recursion_error == 0 && vm->evaluation_depth < vm->recursion_limit
        ? tinypy_internal_exception_instantiate(vm->exception_types[exception_index_from_error], args, NULL, NULL)
        : NULL;
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
    tinypy_error_kind_e kind = TINYPY_ERROR_RUNTIME;

    if (out_error == NULL || vm->raised_value == NULL) {
        return;
    }
    if (vm->raised_type != NULL && TINYPY_VALUE_KIND(vm->raised_type) == TINYPY_VALUE_TYPE) {
        kind = __tinypy_exception_error_from_type(vm, (tinypy_type_t *)vm->raised_type);
    }
    tinypy_internal_make_exception_error(vm, kind, vm->raised_value, out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_raise_key_error(tinypy_vm_t *vm, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &key, 1U);
    tinypy_value_t *exception = tinypy_exception_new(vm->exception_types[TINYPY_EXCEPTION_KEY_ERROR], args, out_error);

    TINYPY_DECREF(args);
    if (exception == NULL) {
        return;
    }
    (void)tinypy_exception_raise(exception, NULL, out_error);
    TINYPY_DECREF(exception);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_raise_stop_iteration(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_value_t *empty = TINYPY_RET_EMPTY_TUPLE(vm);
    tinypy_value_t *exception = tinypy_exception_new(vm->exception_types[TINYPY_EXCEPTION_STOP_ITERATION], empty, out_error);

    TINYPY_DECREF(empty);
    if (exception != NULL) {
        (void)tinypy_exception_raise(exception, NULL, out_error);
        TINYPY_DECREF(exception);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_exception_raise_system_error(tinypy_vm_t *vm, const char *message, tinypy_error_t **out_error) {
    tinypy_value_t *text = tinypy_string_from_bytes(vm, message, strlen(message));
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &text, 1U);
    tinypy_value_t *exception = tinypy_exception_new(vm->exception_types[TINYPY_EXCEPTION_SYSTEM_ERROR], args, out_error);

    TINYPY_DECREF(args);
    TINYPY_DECREF(text);
    if (exception != NULL) {
        (void)tinypy_exception_raise(exception, NULL, out_error);
        TINYPY_DECREF(exception);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_exception_consume_kind(tinypy_vm_t *vm, tinypy_exception_type_index_e index, tinypy_error_t **out_error) {
    tinypy_value_t *raised_type = vm->raised_type;

    if (raised_type == NULL || TINYPY_VALUE_KIND(raised_type) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype((tinypy_type_t *)raised_type, vm->exception_types[index]) == 0) {
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
tinypy_bool_t tinypy_internal_exception_consume_stop_iteration(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_bool_t consumed = tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_STOP_ITERATION, out_error);

    return consumed;
}
//////////////////////////////////////////////////////////////////////////
/* Prepends text to the message of the raised exception when it is an instance
   of the given type carrying a single string argument, the way build_class in
   CPython annotates errors raised while calling the metaclass. */
tinypy_bool_t tinypy_internal_exception_prefix_raised(tinypy_vm_t *vm, tinypy_exception_type_index_e index, const char *prefix, size_t prefix_size, tinypy_error_t **out_error) {
    tinypy_value_t *value = vm->raised_value;
    tinypy_internal_exception_payload_t *payload;
    tinypy_value_t *text;
    tinypy_value_t *joined;
    tinypy_value_t *args;
    size_t text_size;
    char *buffer;

    if (value == NULL || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE || tinypy_type_is_subtype(value->type, vm->exception_types[index]) == 0) {
        return TINYPY_FALSE;
    }
    payload = __tinypy_exception_payload(value);
    if (payload->args == NULL || TINYPY_VALUE_KIND(payload->args) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(payload->args) != 1U) {
        return TINYPY_FALSE;
    }
    text = TINYPY_TUPLE_GET(payload->args, 0U);
    if (TINYPY_VALUE_KIND(text) != TINYPY_VALUE_STRING) {
        return TINYPY_FALSE;
    }
    text_size = TINYPY_TEXT_BYTE_SIZE(text);
    buffer = (char *)tinypy_internal_vm_allocate(vm, prefix_size + text_size);
    (void)memcpy(buffer, prefix, prefix_size);
    if (text_size != 0U) {
        (void)memcpy(buffer + prefix_size, TINYPY_TEXT_BYTES(text), text_size);
    }
    joined = tinypy_string_from_bytes(vm, buffer, prefix_size + text_size);
    tinypy_internal_vm_deallocate(vm, buffer, prefix_size + text_size);
    args = tinypy_tuple_from_items(vm, &joined, 1U);
    __tinypy_exception_replace_args(payload, args);
    if (payload->message != NULL) {
        TINYPY_DECREF(payload->message);
    }
    payload->message = joined;
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
        tinypy_internal_exception_make_diagnostic(vm, out_error);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_exception_matches_tuple(tinypy_value_t *exception, tinypy_value_t *candidate, tinypy_error_t **out_error) {
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
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_exception_matches(tinypy_value_t *exception, tinypy_value_t *candidate, tinypy_error_t **out_error) {
    tinypy_type_t *exception_type = NULL;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(exception);
    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(candidate) == TINYPY_VALUE_TUPLE) {
        /* Nested tuples recurse on the C stack; CPython has no guard here. */
        if (tinypy_internal_recursion_check(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded in __subclasscheck__", out_error) == 0) {
            return -1;
        }
        vm->evaluation_depth += 1U;
        int32_t matched = __tinypy_exception_matches_tuple(exception, candidate, out_error);
        vm->evaluation_depth -= 1U;
        return matched;
    }
    if (__tinypy_exception_is_class(vm, candidate) == 0) {
        int32_t identical = exception == candidate ? 1 : 0;

        return identical;
    }
    if (TINYPY_VALUE_KIND(candidate) == TINYPY_VALUE_CLASS) {
        tinypy_value_t *exception_class = NULL;

        if (TINYPY_VALUE_KIND(exception) == TINYPY_VALUE_CLASS) {
            exception_class = exception;
        }
        else if (TINYPY_VALUE_KIND(exception) == TINYPY_VALUE_OLD_INSTANCE) {
            exception_class = TINYPY_OLD_INSTANCE_OBJECT(exception)->class_object;
        }
        if (exception_class == NULL) {
            return 0;
        }
        int32_t return_value_1 = tinypy_class_is_subclass(exception_class, candidate) != 0 ? 1 : 0;
        return return_value_1;
    }
    if (__tinypy_exception_is_class(vm, exception) != 0) {
        if (TINYPY_VALUE_KIND(exception) != TINYPY_VALUE_TYPE) {
            return 0;
        }
        exception_type = (tinypy_type_t *)exception;
    }
    else if (__tinypy_exception_is_instance(vm, exception) != 0) {
        if (TINYPY_VALUE_KIND(exception) == TINYPY_VALUE_OLD_INSTANCE) {
            return 0;
        }
        exception_type = exception->type;
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exception match operand is not an exception", out_error);
        return -1;
    }
    if (candidate->type != &vm->types[TINYPY_VALUE_TYPE] && tinypy_internal_type_lookup_key(vm, candidate->type, vm->internal_special_subclasscheck_key) != NULL) {
        tinypy_internal_exception_state_t state;
        tinypy_error_t *callback_error = NULL;
        tinypy_value_t *exception_class = TINYPY_RET(tinypy_type_as_value(exception_type));
        TINYPY_INCREF(candidate);
        tinypy_internal_exception_preserve_begin(vm, &state);
        tinypy_value_t *method = tinypy_internal_object_get_special_key(candidate, vm->internal_special_subclasscheck_key, &callback_error);
        tinypy_value_t *call_args = method != NULL ? tinypy_tuple_from_items(vm, &exception_class, 1U) : NULL;
        tinypy_value_t *checked = call_args != NULL ? tinypy_call(method, call_args, NULL, &callback_error) : NULL;
        int32_t truth = checked != NULL ? tinypy_truth(checked, &callback_error) : -1;
        if (checked != NULL) {
            TINYPY_DECREF(checked);
        }
        if (call_args != NULL) {
            TINYPY_DECREF(call_args);
        }
        if (method != NULL) {
            TINYPY_DECREF(method);
        }
        if (callback_error != NULL) {
            tinypy_internal_output_unraisable(vm, exception_class);
            tinypy_error_release(callback_error);
        }
        TINYPY_DECREF(candidate);
        TINYPY_DECREF(exception_class);
        tinypy_internal_exception_preserve_end(vm, &state);
        /* Exception matching cannot replace the pending exception with a
           failure raised by a metaclass check. */
        int32_t matched = truth > 0 ? 1 : 0;
        return matched;
    }
    int32_t return_value_2 = tinypy_type_is_subtype(exception_type, (tinypy_type_t *)candidate) != 0 ? 1 : 0;
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_exception_raise(tinypy_value_t *exception, tinypy_value_t *traceback, tinypy_error_t **out_error) {
    tinypy_value_t *value = exception;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(exception);
    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_exception_is_class(vm, exception) != 0) {
        tinypy_value_t *args = TINYPY_RET_EMPTY_TUPLE(vm);

        value = tinypy_call(exception, args, NULL, out_error);
        TINYPY_DECREF(args);
        if (value == NULL) {
            return TINYPY_FALSE;
        }
        if (tinypy_internal_exception_check_normalized(exception, value, out_error) == 0) {
            TINYPY_DECREF(value);
            return TINYPY_FALSE;
        }
    }
    else if (__tinypy_exception_is_instance(vm, exception) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exceptions must be old-style classes or derived from BaseException", out_error);
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
