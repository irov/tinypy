#include "internal.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct tinypy_constructor_text_view_t {
    const uint8_t *bytes;
    size_t size;
    uint8_t *owned;
    size_t capacity;
} tinypy_constructor_text_view_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_no_keywords(tinypy_vm_t *vm, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "constructor does not accept keyword arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_argument_count(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *args, size_t minimum, size_t maximum, tinypy_arity_style_e style, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_arity_error(vm, name, name_size, count, minimum, maximum, style, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_constructor_keyword_optional(tinypy_value_t *kwargs, tinypy_value_t *name) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(name);
    tinypy_value_t *keyword = tinypy_internal_dict_get_optional_suppressed(vm, kwargs, name);
    return keyword;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_optional_arguments(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_value_t *const *names, size_t maximum, uint32_t text_arguments, tinypy_value_t **outputs, tinypy_bool_t (*converter)(tinypy_vm_t *, size_t, tinypy_value_t *, void *, tinypy_error_t **), void *converter_data, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);
    size_t keyword_count = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    size_t recognized = 0U;

    if (count > maximum || keyword_count > maximum - count) {
        tinypy_internal_make_arity_error(vm, name, name_size, count + keyword_count, 0U, maximum, TINYPY_ARITY_STYLE_PARSED, out_error);
        return TINYPY_FALSE;
    }
    for (size_t index = 0U; index < maximum; ++index) {
        outputs[index] = NULL;
    }
    for (size_t index = 0U; index < maximum; ++index) {
        tinypy_value_t *keyword = NULL;

        outputs[index] = index < count ? TINYPY_TUPLE_GET(args, index) : NULL;
        if (recognized < keyword_count) {
            keyword = tinypy_internal_constructor_keyword_optional(kwargs, names[index]);
        }
        if (keyword != NULL) {
            recognized += 1U;
            if (outputs[index] != NULL) {
                tinypy_value_t *position = tinypy_integer_from_i64(vm, (int64_t)index + 1);
                tinypy_value_t *position_text = tinypy_object_str(position, out_error);

                if (position_text != NULL) {
                    tinypy_message_part_t parts[] = {
                        TINYPY_MESSAGE_PART_LITERAL("Argument given by name ('"),
                        TINYPY_MESSAGE_PART_TEXT(names[index]),
                        TINYPY_MESSAGE_PART_LITERAL("') and position ("),
                        {(const char *)TINYPY_TEXT_BYTES(position_text), TINYPY_TEXT_BYTE_SIZE(position_text)},
                        TINYPY_MESSAGE_PART_LITERAL(")")
                    };

                    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 5U, out_error);
                    TINYPY_DECREF(position_text);
                }
                TINYPY_DECREF(position);
                return TINYPY_FALSE;
            }
            outputs[index] = keyword;
        }
        if (outputs[index] == NULL && recognized == keyword_count) {
            return TINYPY_TRUE;
        }
        if (outputs[index] != NULL && converter != NULL && converter(vm, index, outputs[index], converter_data, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (outputs[index] != NULL && (text_arguments & (UINT32_C(1) << index)) != 0U) {
            tinypy_value_t *value = outputs[index];
            tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
            tinypy_bool_t text = kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE;
            tinypy_bool_t null_byte = TINYPY_FALSE;

            if (text != 0) {
                if (kind == TINYPY_VALUE_UNICODE) {
                    const uint8_t *bytes = TINYPY_TEXT_BYTES(value);
                    size_t size = TINYPY_TEXT_BYTE_SIZE(value);

                    for (size_t offset = 0U; offset < size; ++offset) {
                        if (bytes[offset] >= 0x80U && tinypy_internal_codecs_validate_name(vm, value, out_error) == 0) {
                            return TINYPY_FALSE;
                        }
                    }
                }
                null_byte = memchr(TINYPY_TEXT_BYTES(value), 0, TINYPY_TEXT_BYTE_SIZE(value)) != NULL;
            }
            if (text == 0 || null_byte != 0) {
                tinypy_value_t *position = tinypy_integer_from_i64(vm, (int64_t)index + 1);
                tinypy_value_t *position_text = tinypy_object_str(position, out_error);

                if (position_text != NULL) {
                    tinypy_message_part_t parts[] = {
                        {name, name_size},
                        TINYPY_MESSAGE_PART_LITERAL("() argument "),
                        {(const char *)TINYPY_TEXT_BYTES(position_text), TINYPY_TEXT_BYTE_SIZE(position_text)},
                        {null_byte != 0 ? " must be string without null bytes, not " : " must be string, not ", null_byte != 0 ? 40U : 21U},
                        {kind == TINYPY_VALUE_NONE ? "None" : value->type->name, kind == TINYPY_VALUE_NONE ? 4U : value->type->name_size}
                    };

                    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 5U, out_error);
                    TINYPY_DECREF(position_text);
                }
                TINYPY_DECREF(position);
                return TINYPY_FALSE;
            }
        }
    }
    if (recognized != keyword_count) {
        tinypy_dict_object_t *dictionary = TINYPY_DICT_OBJECT(kwargs);

        for (size_t slot = 0U; slot <= dictionary->mask; ++slot) {
            tinypy_dict_entry_t *entry = &dictionary->table[slot];
            tinypy_bool_t known = TINYPY_FALSE;

            if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry) == 0) {
                continue;
            }
            for (size_t index = 0U; index < maximum; ++index) {
                if (TINYPY_NAME_EQ(entry->key, names[index]) != 0) {
                    known = TINYPY_TRUE;
                    break;
                }
            }
            if (known == 0) {
                if (TINYPY_VALUE_KIND(entry->key) != TINYPY_VALUE_STRING) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "keywords must be strings", out_error);
                    return TINYPY_FALSE;
                }
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("'"),
                    {(const char *)TINYPY_TEXT_BYTES(entry->key), TINYPY_TEXT_BYTE_SIZE(entry->key)},
                    TINYPY_MESSAGE_PART_LITERAL("' is an invalid keyword argument for this function")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
                return TINYPY_FALSE;
            }
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_constructor_optional_arguments(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_value_t *const *names, size_t maximum, uint32_t text_arguments, tinypy_value_t **outputs, tinypy_error_t **out_error) {
    tinypy_bool_t result = __tinypy_constructor_optional_arguments(vm, name, name_size, args, kwargs, names, maximum, text_arguments, outputs, NULL, NULL, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_function_argument_count(tinypy_value_t *function, tinypy_value_t *args, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    tinypy_value_t *name = TINYPY_NATIVE_FUNCTION_OBJECT(function)->name;
    tinypy_bool_t accepted = __tinypy_constructor_argument_count(TINYPY_VALUE_VM(function), (const char *)TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name), args, minimum, maximum, TINYPY_ARITY_STYLE_PARSED, out_error);

    return accepted;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_object_has_excess_arguments(tinypy_value_t *args, tinypy_value_t *kwargs) {
    tinypy_bool_t return_value_1 = TINYPY_TUPLE_SIZE(args) > 1U || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_sequence_argument(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    tinypy_value_t *const names[1] = {vm->internal_sequence_key};
    tinypy_bool_t result = __tinypy_constructor_optional_arguments(vm, name, name_size, args, kwargs, names, 1U, 0U, out_value, NULL, NULL, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_has_mutable_builtin_layout(tinypy_value_type_e kind) {
    return kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_DICT || kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_BYTEARRAY ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_has_immutable_builtin_layout(tinypy_value_type_e kind) {
    return kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG || kind == TINYPY_VALUE_FLOAT || kind == TINYPY_VALUE_COMPLEX || kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_abstract_error(tinypy_type_t *type, tinypy_error_t **out_error) {
    static const char prefix[] = "Can't instantiate abstract class ";
    static const char suffix[] = " with abstract methods ";
    tinypy_vm_t *vm = type->vm;
    size_t prefix_size = sizeof(prefix) - 1U;
    size_t suffix_size = sizeof(suffix) - 1U;
    tinypy_value_t *abstract_methods;
    tinypy_value_t *arguments;
    tinypy_value_t *names;
    tinypy_value_t *sort_method;
    tinypy_value_t *sort_result;
    tinypy_value_t *separator;
    tinypy_value_t *join_method;
    tinypy_value_t *joined;
    const uint8_t *joined_bytes;
    size_t joined_size;
    size_t message_size;
    char *message;

    abstract_methods = tinypy_object_get_attr_value(&type->base.base, vm->internal_special_abstractmethods_key, out_error);
    if (abstract_methods == NULL) {
        return;
    }
    arguments = tinypy_tuple_from_items(vm, &abstract_methods, 1U);
    names = tinypy_internal_list_create(&vm->types[TINYPY_VALUE_LIST], arguments, NULL, out_error);
    TINYPY_DECREF(arguments);
    TINYPY_DECREF(abstract_methods);
    if (names == NULL) {
        return;
    }
    sort_method = tinypy_object_get_attr_value(names, vm->internal_sort_key, out_error);
    if (sort_method == NULL) {
        TINYPY_DECREF(names);
        return;
    }
    arguments = TINYPY_RET_EMPTY_TUPLE(vm);
    sort_result = tinypy_call(sort_method, arguments, NULL, out_error);
    TINYPY_DECREF(arguments);
    TINYPY_DECREF(sort_method);
    if (sort_result == NULL) {
        TINYPY_DECREF(names);
        return;
    }
    TINYPY_DECREF(sort_result);
    separator = tinypy_string_from_bytes(vm, ", ", 2U);
    join_method = tinypy_object_get_attr_value(separator, vm->internal_join_key, out_error);
    TINYPY_DECREF(separator);
    if (join_method == NULL) {
        TINYPY_DECREF(names);
        return;
    }
    arguments = tinypy_tuple_from_items(vm, &names, 1U);
    joined = tinypy_call(join_method, arguments, NULL, out_error);
    TINYPY_DECREF(arguments);
    TINYPY_DECREF(join_method);
    TINYPY_DECREF(names);
    if (joined == NULL) {
        return;
    }
    joined_bytes = TINYPY_TEXT_BYTES(joined);
    joined_size = TINYPY_TEXT_BYTE_SIZE(joined);
    if (type->name_size > SIZE_MAX - prefix_size - suffix_size - 1U || joined_size > SIZE_MAX - prefix_size - type->name_size - suffix_size - 1U) {
        TINYPY_DECREF(joined);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "Can't instantiate abstract class", out_error);
        return;
    }
    message_size = prefix_size + type->name_size + suffix_size + joined_size;
    message = (char *)tinypy_internal_vm_allocate(vm, message_size + 1U);
    (void)memcpy(message, prefix, prefix_size);
    if (type->name_size != 0U) {
        (void)memcpy(message + prefix_size, type->name, type->name_size);
    }
    (void)memcpy(message + prefix_size + type->name_size, suffix, suffix_size);
    if (joined_size != 0U) {
        (void)memcpy(message + prefix_size + type->name_size + suffix_size, joined_bytes, joined_size);
    }
    message[message_size] = '\0';
    TINYPY_DECREF(joined);
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, message, out_error);
    tinypy_internal_vm_deallocate(vm, message, message_size + 1U);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_tail_arguments(tinypy_vm_t *vm, tinypy_value_t *args) {
    size_t size = TINYPY_TUPLE_SIZE(args);

    if (size <= 1U) {
        tinypy_value_t *return_value_1 = TINYPY_RET_EMPTY_TUPLE(vm);
        return return_value_1;
    }
    tinypy_value_t *return_value_2 = tinypy_tuple_from_items(vm, tinypy_internal_tuple_items(args) + 1U, size - 1U);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
static void __tinypy_constructor_rebuild_container_diagnostics(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LIST) {
        __tinypy_internal_cycle_diagnostics_list_clear(vm, value);
        __tinypy_internal_cycle_diagnostics_list_extend(vm, value, 0U, TINYPY_LIST_OBJECT(value)->items, TINYPY_LIST_SIZE(value));
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_DICT) {
        tinypy_dict_entry_t *entry;
        tinypy_dict_entry_t *end;

        __tinypy_internal_cycle_diagnostics_dict_clear(vm, value);
        entry = TINYPY_DICT_ITERATOR_BEGIN(value);
        end = TINYPY_DICT_ITERATOR_END(value);
        for (; entry != end; ++entry) {
            if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
                __tinypy_internal_cycle_diagnostics_dict_set(vm, value, entry->key, entry->value, TINYPY_TRUE);
            }
        }
    }
}
#endif
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_call_conversion(tinypy_value_t *value, tinypy_value_t *name, tinypy_bool_t *out_handled, tinypy_error_t **out_error) {
    tinypy_value_t *method;
    tinypy_value_t *args;
    tinypy_value_t *result;
    int32_t found = tinypy_internal_object_lookup_special_key(value, name, &method, out_error);

    *out_handled = found != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    if (found <= 0) {
        return NULL;
    }
    args = TINYPY_RET_EMPTY_TUPLE(TINYPY_VALUE_VM(value));
    result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_unicode_from_default_encoding(tinypy_vm_t *vm, tinypy_value_t *text, tinypy_error_t **out_error) {
    const uint8_t *bytes;
    size_t size;
    size_t index;

    if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) {
        return TINYPY_RET(text);
    }
    if (TINYPY_VALUE_KIND(text) != TINYPY_VALUE_STRING) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("coercing to Unicode: need string or buffer, "),
            TINYPY_MESSAGE_PART_TYPE_NAME(text),
            TINYPY_MESSAGE_PART_LITERAL(" found")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    bytes = (const uint8_t *)tinypy_string_view(text, &size);
    for (index = 0U; index < size; ++index) {
        if (bytes[index] >= 0x80U) {
            (void)tinypy_internal_raise_ascii_decode_error(vm, text, index, index + 1U, out_error);
            return NULL;
        }
    }
    tinypy_value_t *return_value = tinypy_unicode_from_utf8(vm, (const char *)bytes, size);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_unicode(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_bool_t handled;
    tinypy_value_t *text;
    tinypy_value_t *result;

    /* PyObject_Unicode copies a unicode subtype that keeps the builtin
       conversion to an exact unicode. */
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE && tinypy_internal_object_has_special_override_key(value, vm->internal_special_unicode_key) == 0) {
        tinypy_value_t *exact = tinypy_internal_immutable_subclass_copy(&vm->types[TINYPY_VALUE_UNICODE], TINYPY_RET(value), out_error);
        return exact;
    }
    /* A str subtype that overrides __str__ is converted through it. */
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING && tinypy_internal_object_has_special_override_key(value, vm->internal_special_unicode_key) == 0 && tinypy_internal_object_has_special_override_key(value, vm->internal_special_str_key) == 0) {
        tinypy_value_t *return_value = __tinypy_constructor_unicode_from_default_encoding(vm, value, out_error);
        return return_value;
    }
    text = tinypy_internal_call_conversion(value, vm->internal_special_unicode_key, &handled, out_error);
    if (handled == 0) {
        text = tinypy_internal_call_conversion(value, vm->internal_special_str_key, &handled, out_error);
        if (handled == 0) {
            /* Only a classic instance lacks __str__; instance_str then
               falls back to its repr. */
            text = tinypy_object_repr(value, out_error);
        }
    }
    if (text == NULL) {
        return NULL;
    }
    result = __tinypy_constructor_unicode_from_default_encoding(vm, text, out_error);
    TINYPY_DECREF(text);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_ascii_space(uint8_t character) {
    return character == (uint8_t)' ' || character == (uint8_t)'\t' || character == (uint8_t)'\n' || character == (uint8_t)'\r' || character == (uint8_t)'\v' || character == (uint8_t)'\f';
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_text_view_initialize(tinypy_vm_t *vm, tinypy_value_t *text, tinypy_constructor_text_view_t *view, tinypy_error_t **out_error) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(text);
    size_t size = TINYPY_TEXT_BYTE_SIZE(text);
    size_t input = 0U;
    size_t output = 0U;
    size_t character = 0U;

    view->bytes = bytes;
    view->size = size;
    view->owned = NULL;
    view->capacity = 0U;
    if (TINYPY_VALUE_KIND(text) != TINYPY_VALUE_UNICODE || size == 0U) {
        return TINYPY_TRUE;
    }
    view->owned = (uint8_t *)tinypy_internal_vm_allocate(vm, size);
    view->capacity = size;
    while (input < size) {
        uint32_t code_point;
        uint8_t digit;
        size_t width = tinypy_internal_utf8_decode(bytes + input, size - input, &code_point);

        /* PyUnicode_EncodeDecimal turns every Unicode space into ' '. */
        if (tinypy_internal_unicode_is_space(code_point) != 0) {
            view->owned[output++] = (uint8_t)' ';
        }
        else if (tinypy_internal_unicode_decimal_digit(code_point, &digit) != 0) {
            view->owned[output++] = (uint8_t)('0' + digit);
        }
        else if (code_point != 0U && code_point < UINT32_C(0x100)) {
            view->owned[output++] = (uint8_t)code_point;
        }
        else {
            size_t end = character + 1U;
            size_t next = input + width;

            while (next < size) {
                uint32_t next_point;
                size_t next_width = tinypy_internal_utf8_decode(bytes + next, size - next, &next_point);

                if ((next_point != 0U && next_point < UINT32_C(0x100)) || tinypy_internal_unicode_decimal_digit(next_point, &digit) != 0 || tinypy_internal_unicode_is_space(next_point) != 0) {
                    break;
                }
                next += next_width;
                end += 1U;
            }
            tinypy_value_t *encoding = TINYPY_RET(vm->internal_codec_decimal_name);
            tinypy_value_t *start_value = tinypy_integer_from_i64(vm, (int64_t)character);
            tinypy_value_t *end_value = tinypy_integer_from_i64(vm, (int64_t)end);
            tinypy_value_t *reason = tinypy_string_from_bytes(vm, "invalid decimal Unicode string", 30U);
            tinypy_value_t *source = tinypy_unicode_from_utf8(vm, (const char *)bytes, size);
            tinypy_value_t *items[5] = {encoding, source, start_value, end_value, reason};
            tinypy_value_t *arguments = tinypy_tuple_from_items(vm, items, 5U);
            tinypy_value_t *exception = tinypy_exception_new(vm->exception_types[TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR], arguments, out_error);

            TINYPY_DECREF(arguments);
            TINYPY_DECREF(source);
            TINYPY_DECREF(reason);
            TINYPY_DECREF(end_value);
            TINYPY_DECREF(start_value);
            TINYPY_DECREF(encoding);
            tinypy_internal_vm_deallocate(vm, view->owned, view->capacity);
            view->owned = NULL;
            if (exception != NULL) {
                (void)tinypy_exception_raise(exception, NULL, out_error);
                TINYPY_DECREF(exception);
            }
            return TINYPY_FALSE;
        }
        input += width;
        character += 1U;
    }
    view->bytes = view->owned;
    view->size = output;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_text_view_destroy(tinypy_vm_t *vm, tinypy_constructor_text_view_t *view) {
    if (view->owned != NULL) {
        tinypy_internal_vm_deallocate(vm, view->owned, view->capacity);
    }
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_constructor_digit(uint8_t character) {
    if (character >= (uint8_t)'0' && character <= (uint8_t)'9') {
        return (int32_t)(character - (uint8_t)'0');
    }
    if (character >= (uint8_t)'a' && character <= (uint8_t)'z') {
        return (int32_t)(character - (uint8_t)'a') + INT32_C(10);
    }
    if (character >= (uint8_t)'A' && character <= (uint8_t)'Z') {
        return (int32_t)(character - (uint8_t)'A') + INT32_C(10);
    }
    return INT32_C(-1);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_base_value(tinypy_vm_t *vm, tinypy_value_t *value, int32_t *out_base, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    int64_t base;

    if (kind == TINYPY_VALUE_FLOAT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument expected, got float", out_error);
        return TINYPY_FALSE;
    }
    if (tinypy_internal_number_as_i64(value, &base, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (base > INT32_MAX || base < INT32_MIN) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, base > INT32_MAX ? "signed integer is greater than maximum" : "signed integer is less than minimum", out_error);
        return TINYPY_FALSE;
    }
    *out_base = (int32_t)base;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_parse_base(tinypy_vm_t *vm, size_t index, tinypy_value_t *value, void *user_data, tinypy_error_t **out_error) {
    if (index == 1U) {
        tinypy_bool_t result = __tinypy_constructor_base_value(vm, value, (int32_t *)user_data, out_error);

        return result;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_long_multiply_add(uint16_t *digits, size_t *count, uint32_t multiplier, uint32_t addition) {
    uint64_t carry = addition;
    size_t index;

    for (index = 0U; index < *count; ++index) {
        uint64_t current = (uint64_t)digits[index] * multiplier + carry;

        digits[index] = (uint16_t)(current & UINT64_C(0x7fff));
        carry = current >> 15U;
    }
    while (carry != 0U) {
        digits[(*count)++] = (uint16_t)(carry & UINT64_C(0x7fff));
        carry >>= 15U;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_integer_from_digits(tinypy_vm_t *vm, int32_t sign, const uint16_t *digits, size_t digit_count, int32_t force_long) {
    uint64_t magnitude = UINT64_C(0);
    uint64_t limit = sign < 0 ? (uint64_t)INT64_MAX + UINT64_C(1) : (uint64_t)INT64_MAX;
    size_t index = digit_count;
    tinypy_bool_t fits = TINYPY_TRUE;

    if (force_long != 0) {
        tinypy_value_t *return_value_1 = tinypy_long_from_base15_digits(vm, digit_count == 0U ? 0 : sign, digits, digit_count);
        return return_value_1;
    }
    while (index != 0U) {
        uint64_t digit = digits[--index];

        if (magnitude > (limit - digit) / UINT64_C(32768)) {
            fits = TINYPY_FALSE;
            break;
        }
        magnitude = magnitude * UINT64_C(32768) + digit;
    }
    if (fits != 0) {
        int64_t value;

        if (sign < 0) {
            value = magnitude == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)magnitude;
        }
        else {
            value = (int64_t)magnitude;
        }
        tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, value);
        return return_value_2;
    }
    tinypy_value_t *return_value_3 = tinypy_long_from_base15_digits(vm, sign, digits, digit_count);
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_integer_literal_error(tinypy_vm_t *vm, const uint8_t *bytes, size_t size, int32_t base, int32_t force_long, tinypy_bool_t trim_space, tinypy_error_t **out_error) {
    size_t begin = 0U;

    if (trim_space != 0) {
        while (begin < size && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
            begin += 1U;
        }
    }
    tinypy_value_t *literal = tinypy_string_from_bytes(vm, bytes + begin, size - begin);
    tinypy_value_t *representation = tinypy_object_repr(literal, out_error);
    tinypy_value_t *base_value = tinypy_integer_from_i64(vm, (int64_t)base);
    tinypy_value_t *base_text = tinypy_object_str(base_value, out_error);

    if (representation != NULL && base_text != NULL) {
        tinypy_message_part_t parts[4] = {
            {force_long != 0 ? "invalid literal for long() with base " : "invalid literal for int() with base ", force_long != 0 ? 37U : 36U},
            {(const char *)TINYPY_TEXT_BYTES(base_text), TINYPY_TEXT_BYTE_SIZE(base_text)},
            {": ", 2U},
            {(const char *)TINYPY_TEXT_BYTES(representation), TINYPY_TEXT_BYTE_SIZE(representation)}
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, 4U, out_error);
    }
    if (base_text != NULL) {
        TINYPY_DECREF(base_text);
    }
    TINYPY_DECREF(base_value);
    if (representation != NULL) {
        TINYPY_DECREF(representation);
    }
    TINYPY_DECREF(literal);
}
//////////////////////////////////////////////////////////////////////////
/* Whether the digits PyOS_strtoul reads from the front exceed a C long. */
static tinypy_bool_t __tinypy_constructor_digits_exceed_long(const uint8_t *bytes, size_t size, int32_t base) {
    uint64_t magnitude = 0U;
    size_t index;

    for (index = 0U; index < size; ++index) {
        int32_t digit = __tinypy_constructor_digit(bytes[index]);

        if (digit < 0 || digit >= base) {
            break;
        }
        if (magnitude > ((uint64_t)INT64_MAX - (uint64_t)digit) / (uint64_t)base) {
            return TINYPY_TRUE;
        }
        magnitude = magnitude * (uint64_t)base + (uint64_t)digit;
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_integer_bytes(tinypy_vm_t *vm, const uint8_t *bytes, size_t size, int32_t base, int32_t force_long, tinypy_error_t **out_error) {
    size_t begin = 0U;
    size_t end = size;
    size_t index;
    int32_t sign = INT32_C(1);
    int32_t actual_base = base;
    size_t digit_count = 0U;
    uint16_t *digits;
    size_t capacity;
    size_t output_count = 0U;
    uint32_t chunk_base;
    size_t chunk_digits;
    uint32_t chunk = 0U;
    size_t chunk_length = 0U;
    const uint8_t *null_byte = (const uint8_t *)memchr(bytes, 0, size);

    if (null_byte != NULL) {
        size = (size_t)(null_byte - bytes);
        end = size;
    }
    if (base != 0 && (base < 2 || base > 36)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, force_long != 0 ? "long() base must be >= 2 and <= 36, or 0" : "int() base must be >= 2 and <= 36, or 0", out_error);
        return NULL;
    }
    while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
        begin += 1U;
    }
    size_t literal_begin = begin;

    while (end > begin && __tinypy_constructor_ascii_space(bytes[end - 1U]) != 0) {
        end -= 1U;
    }
    if (begin < end && (bytes[begin] == (uint8_t)'+' || bytes[begin] == (uint8_t)'-')) {
        if (bytes[begin] == (uint8_t)'-') {
            sign = INT32_C(-1);
        }
        begin += 1U;
        while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
            begin += 1U;
        }
    }
    if (actual_base == 0) {
        if (end - begin >= 2U && bytes[begin] == (uint8_t)'0' && (bytes[begin + 1U] == (uint8_t)'x' || bytes[begin + 1U] == (uint8_t)'X')) {
            actual_base = 16;
            begin += 2U;
        }
        else if (end - begin >= 2U && bytes[begin] == (uint8_t)'0' && (bytes[begin + 1U] == (uint8_t)'b' || bytes[begin + 1U] == (uint8_t)'B')) {
            actual_base = 2;
            begin += 2U;
        }
        else if (end - begin >= 2U && bytes[begin] == (uint8_t)'0' && (bytes[begin + 1U] == (uint8_t)'o' || bytes[begin + 1U] == (uint8_t)'O')) {
            actual_base = 8;
            begin += 2U;
        }
        else {
            actual_base = begin < end && bytes[begin] == (uint8_t)'0' ? 8 : 10;
        }
    }
    else if (end - begin >= 2U && bytes[begin] == (uint8_t)'0') {
        uint8_t prefix = bytes[begin + 1U];

        if ((actual_base == 16 && (prefix == (uint8_t)'x' || prefix == (uint8_t)'X')) || (actual_base == 8 && (prefix == (uint8_t)'o' || prefix == (uint8_t)'O')) || (actual_base == 2 && (prefix == (uint8_t)'b' || prefix == (uint8_t)'B'))) {
            begin += 2U;
        }
    }
    /* PyInt_FromString passes an unsigned base 0 literal that exceeds a C
       long to PyLong_FromString, which also takes a trailing L. */
    if (force_long == 0 && base == 0 && literal_begin < end && bytes[literal_begin] == (uint8_t)'0' && __tinypy_constructor_digits_exceed_long(bytes + begin, end - begin, actual_base) != 0) {
        tinypy_value_t *long_result = __tinypy_constructor_integer_bytes(vm, bytes + literal_begin, size - literal_begin, 0, 1, out_error);
        return long_result;
    }
    if (force_long != 0 && actual_base <= 21 && begin < end && (bytes[end - 1U] == (uint8_t)'L' || bytes[end - 1U] == (uint8_t)'l')) {
        end -= 1U;
    }
    for (index = begin; index < end; ++index) {
        int32_t digit = __tinypy_constructor_digit(bytes[index]);

        if (digit < 0 || digit >= actual_base) {
            __tinypy_constructor_integer_literal_error(vm, bytes, size, force_long != 0 ? actual_base : base, force_long, force_long == 0, out_error);
            return NULL;
        }
        digit_count += 1U;
    }
    if (digit_count == 0U) {
        __tinypy_constructor_integer_literal_error(vm, bytes, size, force_long != 0 ? actual_base : base, force_long, force_long == 0, out_error);
        return NULL;
    }
    capacity = digit_count / 2U + 2U;
    if (capacity > SIZE_MAX / sizeof(*digits)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "integer literal is too large", out_error);
        return NULL;
    }
    digits = (uint16_t *)tinypy_internal_vm_allocate_checked(vm, capacity * sizeof(*digits), out_error);
    if (digits == NULL) {
        return NULL;
    }
    chunk_base = (uint32_t)actual_base;
    chunk_digits = 1U;
    while (chunk_base <= UINT32_MAX / (uint32_t)actual_base) {
        chunk_base *= (uint32_t)actual_base;
        chunk_digits += 1U;
    }
    for (index = begin; index < end; ++index) {
        uint32_t digit = (uint32_t)__tinypy_constructor_digit(bytes[index]);

        chunk = chunk * (uint32_t)actual_base + digit;
        chunk_length += 1U;
        if (chunk_length == chunk_digits) {
            __tinypy_constructor_long_multiply_add(digits, &output_count, chunk_base, chunk);
            chunk = 0U;
            chunk_length = 0U;
        }
    }
    if (chunk_length != 0U) {
        uint32_t multiplier = 1U;

        for (index = 0U; index < chunk_length; ++index) {
            multiplier *= (uint32_t)actual_base;
        }
        __tinypy_constructor_long_multiply_add(digits, &output_count, multiplier, chunk);
    }
    tinypy_value_t *result = __tinypy_constructor_integer_from_digits(vm, sign, digits, output_count, force_long);
    tinypy_internal_vm_deallocate(vm, digits, capacity * sizeof(*digits));
    if (null_byte != NULL) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, force_long != 0 ? "null byte in argument for long()" : "null byte in argument for int()", out_error);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_integer_text(tinypy_vm_t *vm, tinypy_value_t *text, int32_t base, int32_t force_long, tinypy_error_t **out_error) {
    tinypy_constructor_text_view_t view;
    tinypy_value_t *result;

    if (__tinypy_constructor_text_view_initialize(vm, text, &view, out_error) == 0) {
        return NULL;
    }
    result = __tinypy_constructor_integer_bytes(vm, view.bytes, view.size, base, force_long, out_error);
    __tinypy_constructor_text_view_destroy(vm, &view);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_number_as_double(tinypy_vm_t *vm, tinypy_value_t *value, double *out_value, tinypy_bool_t allow_complex, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = (double)TINYPY_INTEGER_VALUE(value);
    }
    else if (kind == TINYPY_VALUE_LONG) {
        if (tinypy_long_as_double(value, out_value, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    else if (kind == TINYPY_VALUE_FLOAT) {
        *out_value = TINYPY_FLOAT_OBJECT(value)->value;
    }
    else if (kind == TINYPY_VALUE_COMPLEX && allow_complex != 0 && TINYPY_COMPLEX_OBJECT(value)->imaginary == 0.0) {
        *out_value = TINYPY_COMPLEX_OBJECT(value)->real;
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "numeric conversion requires a number", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_long_from_double(tinypy_vm_t *vm, double value) {
    uint16_t digits[(DBL_MAX_EXP + 14) / 15];
    double magnitude = trunc(fabs(value));
    size_t count = 0U;

    while (magnitude >= 1.0) {
        double quotient = floor(magnitude / 32768.0);
        double remainder = magnitude - quotient * 32768.0;

        digits[count++] = (uint16_t)remainder;
        magnitude = quotient;
    }
    tinypy_value_t *return_value = tinypy_long_from_base15_digits(vm, value < 0.0 ? -1 : (count != 0U ? 1 : 0), digits, count);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_text_equal_ascii(const uint8_t *bytes, size_t size, const char *ascii) {
    size_t index;

    for (index = 0U; index < size; ++index) {
        uint8_t left = bytes[index];
        uint8_t right = (uint8_t)ascii[index];

        if (right == 0U) {
            return TINYPY_FALSE;
        }
        if (left >= (uint8_t)'A' && left <= (uint8_t)'Z') {
            left = (uint8_t)(left + ((uint8_t)'a' - (uint8_t)'A'));
        }
        if (left != right) {
            return TINYPY_FALSE;
        }
    }
    return ascii[size] == '\0' ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_decimal_double(tinypy_vm_t *vm, const uint8_t *bytes, size_t size, double *out_value) {
    char local[128];

    if (size < sizeof(local)) {
        char *end;
        double value;

        (void)memcpy(local, bytes, size);
        local[size] = '\0';
        value = strtod(local, &end);
        if (end == local + size) {
            *out_value = value;
            return TINYPY_TRUE;
        }
    }
    tinypy_bool_t return_value_1 = tinypy_internal_decimal_double(vm, (const char *)bytes, size, out_value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_float_literal_error(tinypy_vm_t *vm, const uint8_t *bytes, size_t size, tinypy_bool_t numeric_prefix, tinypy_error_t **out_error) {
    size_t begin = 0U;
    const uint8_t *null_byte;

    while (begin < size && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
        begin += 1U;
    }
    null_byte = (const uint8_t *)memchr(bytes + begin, 0, size - begin);
    tinypy_message_part_t parts[2] = {
        {numeric_prefix != 0 ? "invalid literal for float(): " : "could not convert string to float: ", numeric_prefix != 0 ? 29U : 35U},
        {(const char *)bytes + begin, null_byte != NULL ? (size_t)(null_byte - bytes) - begin : size - begin}
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, 2U, out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_float_bytes(tinypy_vm_t *vm, const uint8_t *bytes, size_t size, double *out_value, tinypy_bool_t report_error, tinypy_error_t **out_error) {
    size_t begin = 0U;
    size_t end = size;
    size_t index;
    int32_t sign = INT32_C(1);
    size_t digits = 0U;
    tinypy_bool_t numeric_prefix = TINYPY_FALSE;

    while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
        begin += 1U;
    }
    while (end > begin && __tinypy_constructor_ascii_space(bytes[end - 1U]) != 0) {
        end -= 1U;
    }
    if (begin < end && (bytes[begin] == (uint8_t)'+' || bytes[begin] == (uint8_t)'-')) {
        if (bytes[begin] == (uint8_t)'-') {
            sign = INT32_C(-1);
        }
        begin += 1U;
    }
    if (__tinypy_constructor_text_equal_ascii(bytes + begin, end - begin, "nan") != 0) {
        *out_value = sign < 0 ? -NAN : NAN;
        return TINYPY_TRUE;
    }
    if (__tinypy_constructor_text_equal_ascii(bytes + begin, end - begin, "inf") != 0 || __tinypy_constructor_text_equal_ascii(bytes + begin, end - begin, "infinity") != 0) {
        *out_value = sign < 0 ? -INFINITY : INFINITY;
        return TINYPY_TRUE;
    }
    if (end - begin >= 3U && (__tinypy_constructor_text_equal_ascii(bytes + begin, 3U, "nan") != 0 || __tinypy_constructor_text_equal_ascii(bytes + begin, 3U, "inf") != 0)) {
        numeric_prefix = TINYPY_TRUE;
    }
    index = begin;
    while (index < end && bytes[index] >= (uint8_t)'0' && bytes[index] <= (uint8_t)'9') {
        digits += 1U;
        index += 1U;
    }
    if (index < end && bytes[index] == (uint8_t)'.') {
        index += 1U;
        while (index < end && bytes[index] >= (uint8_t)'0' && bytes[index] <= (uint8_t)'9') {
            digits += 1U;
            index += 1U;
        }
    }
    if (digits != 0U) {
        numeric_prefix = TINYPY_TRUE;
    }
    if (index < end && (bytes[index] == (uint8_t)'e' || bytes[index] == (uint8_t)'E')) {
        size_t exponent_digits = 0U;

        index += 1U;
        if (index < end && (bytes[index] == (uint8_t)'+' || bytes[index] == (uint8_t)'-')) {
            index += 1U;
        }
        while (index < end && bytes[index] >= (uint8_t)'0' && bytes[index] <= (uint8_t)'9') {
            exponent_digits += 1U;
            index += 1U;
        }
        if (exponent_digits == 0U) {
            digits = 0U;
        }
    }
    if (digits == 0U || index != end) {
        if (report_error != 0) {
            __tinypy_constructor_float_literal_error(vm, bytes, size, numeric_prefix, out_error);
        }
        return TINYPY_FALSE;
    }
    double magnitude;

    if (__tinypy_constructor_decimal_double(vm, bytes + begin, end - begin, &magnitude) == 0) {
        if (report_error != 0) {
            __tinypy_constructor_float_literal_error(vm, bytes, size, numeric_prefix, out_error);
        }
        return TINYPY_FALSE;
    }
    *out_value = sign < 0 ? -magnitude : magnitude;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_float_text(tinypy_vm_t *vm, tinypy_value_t *text, double *out_value, tinypy_error_t **out_error) {
    tinypy_constructor_text_view_t view;
    tinypy_bool_t result;

    if (__tinypy_constructor_text_view_initialize(vm, text, &view, out_error) == 0) {
        return TINYPY_FALSE;
    }
    result = __tinypy_constructor_float_bytes(vm, view.bytes, view.size, out_value, TINYPY_TRUE, out_error);
    __tinypy_constructor_text_view_destroy(vm, &view);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_complex_text(tinypy_vm_t *vm, tinypy_value_t *text, double *out_real, double *out_imaginary, tinypy_error_t **out_error) {
    tinypy_constructor_text_view_t view;
    const uint8_t *bytes;
    size_t begin = 0U;
    size_t end;
    size_t split = SIZE_MAX;
    size_t index;
    tinypy_bool_t imaginary = TINYPY_FALSE;
    tinypy_bool_t valid = TINYPY_FALSE;

    if (__tinypy_constructor_text_view_initialize(vm, text, &view, out_error) == 0) {
        return TINYPY_FALSE;
    }
    bytes = view.bytes;
    end = view.size;
    while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
        begin += 1U;
    }
    while (end > begin && __tinypy_constructor_ascii_space(bytes[end - 1U]) != 0) {
        end -= 1U;
    }
    if (end - begin >= 2U && bytes[begin] == (uint8_t)'(' && bytes[end - 1U] == (uint8_t)')') {
        begin += 1U;
        end -= 1U;
        while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
            begin += 1U;
        }
        while (end > begin && __tinypy_constructor_ascii_space(bytes[end - 1U]) != 0) {
            end -= 1U;
        }
    }
    for (index = begin; index < end; ++index) {
        if (__tinypy_constructor_ascii_space(bytes[index]) != 0) {
            goto done;
        }
    }
    if (begin < end && (bytes[end - 1U] == (uint8_t)'j' || bytes[end - 1U] == (uint8_t)'J')) {
        imaginary = TINYPY_TRUE;
        end -= 1U;
    }
    if (imaginary == 0) {
        valid = __tinypy_constructor_float_bytes(vm, bytes + begin, end - begin, out_real, TINYPY_FALSE, out_error);
        *out_imaginary = 0.0;
        goto done;
    }
    for (index = begin + 1U; index < end; ++index) {
        if ((bytes[index] == (uint8_t)'+' || bytes[index] == (uint8_t)'-') && bytes[index - 1U] != (uint8_t)'e' && bytes[index - 1U] != (uint8_t)'E') {
            split = index;
        }
    }
    if (split != SIZE_MAX) {
        if (__tinypy_constructor_float_bytes(vm, bytes + begin, split - begin, out_real, TINYPY_FALSE, out_error) == 0) {
            goto done;
        }
        begin = split;
    }
    else {
        *out_real = 0.0;
    }
    if (end - begin == 0U || (end - begin == 1U && bytes[begin] == (uint8_t)'+')) {
        *out_imaginary = 1.0;
        valid = TINYPY_TRUE;
    }
    else if (end - begin == 1U && bytes[begin] == (uint8_t)'-') {
        *out_imaginary = -1.0;
        valid = TINYPY_TRUE;
    }
    else {
        valid = __tinypy_constructor_float_bytes(vm, bytes + begin, end - begin, out_imaginary, TINYPY_FALSE, out_error);
    }

done:
    __tinypy_constructor_text_view_destroy(vm, &view);
    if (valid == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "complex() arg is a malformed string", out_error);
    }
    return valid;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_sequence_to_list(tinypy_vm_t *vm, tinypy_value_t *iterable, int64_t default_hint, tinypy_error_t **out_error) {
    tinypy_value_t *result = tinypy_internal_list_from_items_checked(vm, NULL, 0U, out_error);
    if (result == NULL) {
        return NULL;
    }
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) {
        TINYPY_DECREF(result);
        return NULL;
    }
    int64_t hint;
    if (tinypy_internal_length_hint(iterable, default_hint, &hint, out_error) == 0) {
        TINYPY_DECREF(iterator);
        TINYPY_DECREF(result);
        return NULL;
    }
    if (hint == INT64_C(-1) || (default_hint == INT64_C(10) && hint < 0)) {
        tinypy_internal_exception_raise_system_error(vm, hint == INT64_C(-1) ? "NULL result without error in PyObject_Call" : "bad argument to internal function", out_error);
        TINYPY_DECREF(iterator);
        TINYPY_DECREF(result);
        return NULL;
    }
    size_t reserve_hint = hint < 0 ? 0U : (size_t)hint;
    if ((hint >= 0 && (uint64_t)hint > (uint64_t)SIZE_MAX) || tinypy_internal_list_reserve_checked(vm, result, reserve_hint, out_error) == 0) {
        TINYPY_DECREF(iterator);
        TINYPY_DECREF(result);
        return NULL;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        TINYPY_DECREF(result);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_new(tinypy_vm_t *vm, tinypy_type_t *requested, tinypy_value_t *name, tinypy_value_t *bases, tinypy_value_t *namespace_dict, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_type_t *winner = requested;
    size_t base_count = TINYPY_TUPLE_SIZE(bases);
    for (size_t index = 0U; index < base_count; ++index) {
        tinypy_value_t *base = TINYPY_TUPLE_GET(bases, index);
        if (TINYPY_VALUE_KIND(base) == TINYPY_VALUE_CLASS) {
            continue;
        }
        /* type_new weighs the type of every base, so an object that is not a
           class reports a metaclass conflict before best_base rejects it. */
        tinypy_type_t *candidate = base->type;
        if (tinypy_type_is_subtype(winner, candidate) != 0) {
            continue;
        }
        if (tinypy_type_is_subtype(candidate, winner) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "metaclass conflict: the metaclass of a derived class must be a (non-strict) subclass of the metaclasses of all its bases", out_error);
            return NULL;
        }
        winner = candidate;
    }
    if (winner != requested) {
        tinypy_value_t *new_method = tinypy_internal_type_lookup_key(vm, winner, vm->internal_special_new_key);
        tinypy_value_t *builtin_new = tinypy_internal_type_lookup_key(vm, &vm->types[TINYPY_VALUE_TYPE], vm->internal_special_new_key);
        if (new_method != builtin_new) {
            tinypy_value_t *winner_value = TINYPY_RET(tinypy_type_as_value(winner));
            tinypy_value_t *callable = tinypy_internal_object_get_attr_key(winner_value, vm->internal_special_new_key, out_error);
            if (callable == NULL) {
                TINYPY_DECREF(winner_value);
                return NULL;
            }
            size_t count = TINYPY_TUPLE_SIZE(args);
            tinypy_value_t *items[4] = {winner_value, NULL, NULL, NULL};
            for (size_t argument_index = 0U; argument_index < count; ++argument_index) {
                items[argument_index + 1U] = TINYPY_TUPLE_GET(args, argument_index);
            }
            tinypy_value_t *call_args = tinypy_internal_tuple_from_items_checked(vm, items, count + 1U, out_error);
            tinypy_value_t *result = call_args != NULL ? tinypy_call(callable, call_args, kwargs, out_error) : NULL;
            if (call_args != NULL) {
                TINYPY_DECREF(call_args);
            }
            TINYPY_DECREF(callable);
            TINYPY_DECREF(winner_value);
            return result;
        }
    }
    tinypy_type_t *created = tinypy_internal_type_new_from_tuple(name, bases, winner, namespace_dict, out_error);
    tinypy_value_t *result = created != NULL ? tinypy_type_as_value(created) : NULL;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_create_new(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *const names[] = {vm->internal_name_key, vm->internal_bases_key, vm->internal_dict_key};
    tinypy_value_t *const expected_names[] = {vm->internal_string_key, vm->internal_tuple_key, vm->internal_dict_key};
    static const tinypy_value_type_e expected_kinds[] = {TINYPY_VALUE_STRING, TINYPY_VALUE_TUPLE, TINYPY_VALUE_DICT};
    size_t count = TINYPY_TUPLE_SIZE(args);
    size_t keyword_count = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    tinypy_value_t *arguments[3] = {NULL, NULL, NULL};
    tinypy_value_t *result = NULL;

    if (type->base.base.type == &vm->types[TINYPY_VALUE_TYPE] && count == 1U && keyword_count == 0U) {
        result = TINYPY_RET(tinypy_type_as_value(TINYPY_TUPLE_GET(args, 0U)->type));
        return result;
    }
    if (count > 3U || keyword_count != 3U - count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type() takes 1 or 3 arguments", out_error);
        return NULL;
    }
    for (size_t index = 0U; index < 3U; ++index) {
        tinypy_value_t *keyword = NULL;
        char position = (char)('1' + index);

        if (keyword_count != 0U) {
            tinypy_bool_t found = tinypy_internal_dict_get_optional_checked(vm, kwargs, names[index], &keyword, out_error);

            if (found == 0) {
                goto cleanup;
            }
        }
        if (index < count && keyword != NULL) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("Argument given by name ('"),
                TINYPY_MESSAGE_PART_TEXT(names[index]),
                TINYPY_MESSAGE_PART_LITERAL("') and position ("),
                {&position, 1U},
                TINYPY_MESSAGE_PART_LITERAL(")"),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            goto cleanup;
        }
        arguments[index] = index < count ? TINYPY_TUPLE_GET(args, index) : keyword;
        if (arguments[index] == NULL) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("Required argument '"),
                TINYPY_MESSAGE_PART_TEXT(names[index]),
                TINYPY_MESSAGE_PART_LITERAL("' (pos "),
                {&position, 1U},
                TINYPY_MESSAGE_PART_LITERAL(") not found"),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            goto cleanup;
        }
        TINYPY_INCREF(arguments[index]);
        if (TINYPY_VALUE_KIND(arguments[index]) != expected_kinds[index]) {
            tinypy_message_part_t type_name = TINYPY_MESSAGE_PART_TYPE_NAME(arguments[index]);
            if (TINYPY_VALUE_KIND(arguments[index]) == TINYPY_VALUE_NONE) {
                type_name.bytes = "None";
                type_name.size = 4U;
            }
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("type() argument "),
                {&position, 1U},
                TINYPY_MESSAGE_PART_LITERAL(" must be "),
                TINYPY_MESSAGE_PART_TEXT(expected_names[index]),
                TINYPY_MESSAGE_PART_LITERAL(", not "),
                type_name,
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            goto cleanup;
        }
    }
    result = __tinypy_constructor_type_new(vm, type, arguments[0], arguments[1], arguments[2], args, kwargs, out_error);
cleanup:
    for (size_t index = 0U; index < 3U; ++index) {
        if (arguments[index] != NULL) {
            TINYPY_DECREF(arguments[index]);
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_type_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_constructor_type_create_new(type, args, kwargs, out_error);

    if (result == NULL || (type == &type->vm->types[TINYPY_VALUE_TYPE] && TINYPY_TUPLE_SIZE(args) == 1U && (kwargs == NULL || TINYPY_DICT_SIZE(kwargs) == 0U)) || tinypy_type_is_subtype(result->type, type) == 0) {
        return result;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(result);
    tinypy_value_t *initializer = tinypy_internal_object_get_special_key(result, vm->internal_special_init_key, out_error);
    tinypy_value_t *initialized = initializer != NULL ? tinypy_call(initializer, args, kwargs, out_error) : NULL;
    if (initializer != NULL) {
        TINYPY_DECREF(initializer);
    }
    if (initialized == NULL) {
        TINYPY_DECREF(result);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(initialized) != TINYPY_VALUE_NONE) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("__init__() should return None, not '"),
            TINYPY_MESSAGE_PART_TYPE_NAME(initialized),
            TINYPY_MESSAGE_PART_LITERAL("'"),
        };

        tinypy_internal_make_vm_error_parts(type->vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        TINYPY_DECREF(initialized);
        TINYPY_DECREF(result);
        return NULL;
    }
    TINYPY_DECREF(initialized);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    /* object_new: excess arguments of either kind take one message. */
    if (TINYPY_TUPLE_SIZE(args) != 0U || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U)) {
        tinypy_internal_make_vm_error(type->vm, TINYPY_ERROR_TYPE, "object() takes no parameters", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_object_allocate_checked(type->vm, type, type->basic_size, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bool_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *const names[1] = {vm->internal_x_key};
    tinypy_value_t *values[1];
    int32_t truth;

    if (tinypy_internal_constructor_optional_arguments(vm, "bool", 4U, args, kwargs, names, 1U, 0U, values, out_error) == 0) {
        return NULL;
    }
    if (values[0] == NULL) {
        tinypy_value_t *return_value_1 = TINYPY_RET_FALSE(vm);
        return return_value_1;
    }
    truth = tinypy_truth(values[0], out_error);
    tinypy_value_t *return_value_2 = truth < 0 ? NULL : tinypy_bool_from_i32(vm, truth);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
/* A long subclass instance converts to a plain long with the same digits. */
static tinypy_value_t *__tinypy_constructor_long_exact(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    size_t digit_count = TINYPY_LONG_DIGIT_COUNT(value);
    tinypy_value_t *result = tinypy_internal_long_allocate_digits(vm, TINYPY_LONG_SIGN(value), digit_count, out_error);

    if (result != NULL && digit_count != 0U) {
        (void)memcpy(TINYPY_LONG_OBJECT(result)->digits, TINYPY_LONG_OBJECT(value)->digits, digit_count * sizeof(TINYPY_LONG_OBJECT(result)->digits[0]));
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_conversion_error(tinypy_vm_t *vm, const char *protocol, size_t protocol_size, const char *expected, size_t expected_size, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_message_part_t parts[] = {
        {protocol, protocol_size},
        TINYPY_MESSAGE_PART_LITERAL(" returned "),
        {expected, expected_size},
        TINYPY_MESSAGE_PART_LITERAL(" (type "),
        TINYPY_MESSAGE_PART_TYPE_NAME(value),
        TINYPY_MESSAGE_PART_LITERAL(")"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
/* instance_int, instance_long and instance_float look the conversion up
   like any attribute of the classic instance, so a missing one is the
   instance's AttributeError. */
static tinypy_value_t *__tinypy_constructor_classic_conversion(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_value_t *method = tinypy_internal_object_get_attr_key(value, name, out_error);

    if (method == NULL) {
        return NULL;
    }
    tinypy_value_t *args = TINYPY_RET_EMPTY_TUPLE(TINYPY_VALUE_VM(value));
    tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* PyNumber_Int and _PyNumber_ConvertIntegralToInt call what PyObject_GetAttr
   finds and clear any failure of the lookup. Returns 1 with the call's
   result, 0 when the attribute is unavailable and -1 when the call fails. */
static int32_t __tinypy_constructor_call_found_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_value_t **out_result, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_error_t *lookup_error = NULL;
    tinypy_value_t *method;
    int32_t found = tinypy_internal_object_get_optional_attr_key(value, name, &method, &lookup_error);

    *out_result = NULL;
    if (found < 0) {
        if (lookup_error != NULL) {
            tinypy_error_release(lookup_error);
        }
        tinypy_vm_clear_error(vm);
        return INT32_C(0);
    }
    if (found == 0) {
        return INT32_C(0);
    }
    tinypy_value_t *args = TINYPY_RET_EMPTY_TUPLE(vm);
    *out_result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return *out_result != NULL ? INT32_C(1) : -INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
/* _PyNumber_ConvertIntegralToInt names a classic instance by its class. */
static void __tinypy_constructor_non_integral_error(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_OLD_INSTANCE) {
        __tinypy_constructor_conversion_error(vm, "__trunc__", 9U, "non-Integral", 12U, value, out_error);
        return;
    }
    tinypy_value_t *class_name = TINYPY_CLASS_OBJECT(TINYPY_OLD_INSTANCE_OBJECT(value)->class_object)->name;
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("__trunc__ returned non-Integral (type "),
        TINYPY_MESSAGE_PART_TEXT(class_name),
        TINYPY_MESSAGE_PART_LITERAL(")"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_is_integral(const tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    return kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG;
}
//////////////////////////////////////////////////////////////////////////
/* PyNumber_Int and PyNumber_Long check what __int__, __long__ or __trunc__
   returned and keep int and long subclasses; a truncated value that is no
   integer goes through its __int__ attribute. */
static tinypy_value_t *__tinypy_constructor_integer_result(tinypy_vm_t *vm, tinypy_value_t *conversion_result, tinypy_value_t *method_name, tinypy_bool_t truncated, int32_t force_long, tinypy_error_t **out_error) {
    if (conversion_result == NULL) {
        return NULL;
    }
    if (truncated != 0 && __tinypy_constructor_is_integral(conversion_result) == 0) {
        tinypy_value_t *truncated_result = conversion_result;
        int32_t found = __tinypy_constructor_call_found_attribute(truncated_result, vm->internal_special_int_key, &conversion_result, out_error);

        if (found == 0) {
            __tinypy_constructor_non_integral_error(vm, truncated_result, out_error);
        }
        TINYPY_DECREF(truncated_result);
        if (conversion_result == NULL) {
            return NULL;
        }
        if (__tinypy_constructor_is_integral(conversion_result) == 0) {
            __tinypy_constructor_non_integral_error(vm, conversion_result, out_error);
            TINYPY_DECREF(conversion_result);
            return NULL;
        }
    }
    else if (__tinypy_constructor_is_integral(conversion_result) == 0) {
        __tinypy_constructor_conversion_error(vm, (const char *)TINYPY_TEXT_BYTES(method_name), TINYPY_TEXT_BYTE_SIZE(method_name), force_long != 0 ? "non-long" : "non-int", force_long != 0 ? 8U : 7U, conversion_result, out_error);
        TINYPY_DECREF(conversion_result);
        return NULL;
    }
    if (force_long != 0 && TINYPY_VALUE_KIND(conversion_result) != TINYPY_VALUE_LONG) {
        tinypy_value_t *long_result = tinypy_long_from_i64(vm, TINYPY_INTEGER_VALUE(conversion_result));

        TINYPY_DECREF(conversion_result);
        return long_result;
    }
    return conversion_result;
}
//////////////////////////////////////////////////////////////////////////
/* nb_int. Every classic instance has instance_int: __int__ when the instance
   has it, otherwise its __trunc__ result made integral. */
tinypy_value_t *tinypy_internal_call_int_conversion(tinypy_value_t *value, tinypy_bool_t *out_handled, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *result = tinypy_internal_call_conversion(value, vm->internal_special_int_key, out_handled, out_error);
        return result;
    }
    *out_handled = TINYPY_TRUE;
    if (tinypy_object_has_attr_value(value, vm->internal_special_int_key) != 0) {
        tinypy_value_t *result = __tinypy_constructor_classic_conversion(value, vm->internal_special_int_key, out_error);
        return result;
    }
    tinypy_value_t *truncated = __tinypy_constructor_classic_conversion(value, vm->internal_special_trunc_key, out_error);
    tinypy_value_t *integral = __tinypy_constructor_integer_result(vm, truncated, vm->internal_special_trunc_key, TINYPY_TRUE, 0, out_error);
    return integral;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_integer_common(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, int32_t force_long, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    int32_t base = 10;
    tinypy_value_type_e kind;
    tinypy_bool_t handled;
    tinypy_bool_t truncated = TINYPY_FALSE;
    tinypy_value_t *method_name;
    tinypy_value_t *conversion_result;

    tinypy_value_t *const names[2] = {vm->internal_x_key, vm->internal_base_key};
    tinypy_value_t *values[2];
    tinypy_value_t *value;
    tinypy_value_t *base_value;

    if (__tinypy_constructor_optional_arguments(vm, force_long != 0 ? "long" : "int", force_long != 0 ? 4U : 3U, args, kwargs, names, 2U, 0U, values, __tinypy_constructor_parse_base, &base, out_error) == 0) {
        return NULL;
    }
    value = values[0];
    base_value = values[1];
    if (value == NULL && base_value != NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, force_long != 0 ? "long() missing string argument" : "int() missing string argument", out_error);
        return NULL;
    }
    if (value == NULL) {
        tinypy_value_t *result = force_long != 0 ? tinypy_long_from_i64(vm, INT64_C(0)) : tinypy_integer_from_i64(vm, INT64_C(0));

        return result;
    }
    kind = TINYPY_VALUE_KIND(value);
    if (base_value != NULL) {
        if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, force_long != 0 ? "long() can't convert non-string with explicit base" : "int() can't convert non-string with explicit base", out_error);
            return NULL;
        }
        if (kind == TINYPY_VALUE_STRING && memchr(TINYPY_TEXT_BYTES(value), 0, TINYPY_TEXT_BYTE_SIZE(value)) != NULL) {
            __tinypy_constructor_integer_literal_error(vm, TINYPY_TEXT_BYTES(value), TINYPY_TEXT_BYTE_SIZE(value), base, force_long, TINYPY_FALSE, out_error);
            return NULL;
        }
        tinypy_value_t *return_value_2 = __tinypy_constructor_integer_text(vm, value, base, force_long, out_error);
        return return_value_2;
    }
    /* instance_long falls back to instance_int. */
    if (kind == TINYPY_VALUE_OLD_INSTANCE) {
        method_name = vm->internal_special_int_key;
        if (force_long != 0 && tinypy_object_has_attr_value(value, vm->internal_special_long_key) != 0) {
            method_name = vm->internal_special_long_key;
            conversion_result = __tinypy_constructor_classic_conversion(value, method_name, out_error);
        }
        else {
            conversion_result = tinypy_internal_call_int_conversion(value, &handled, out_error);
        }
        tinypy_value_t *classic = __tinypy_constructor_integer_result(vm, conversion_result, method_name, TINYPY_FALSE, force_long, out_error);
        return classic;
    }
    if (tinypy_internal_object_has_special_override_key(value, force_long != 0 ? vm->internal_special_long_key : vm->internal_special_int_key) != 0) {
        goto integer_conversion;
    }
    /* A text subclass may still answer __trunc__ before it is parsed. */
    if ((kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) && value->type == &vm->types[kind]) {
        tinypy_value_t *result = __tinypy_constructor_integer_text(vm, value, 10, force_long, out_error);
        return result;
    }
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        tinypy_value_t *return_value_4 = force_long != 0 ? tinypy_long_from_i64(vm, TINYPY_INTEGER_VALUE(value)) : tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));
        return return_value_4;
    }
    if (kind == TINYPY_VALUE_LONG) {
        if (force_long != 0) {
            if (value->type == &vm->types[TINYPY_VALUE_LONG]) {
                return TINYPY_RET(value);
            }
            tinypy_value_t *exact_long = __tinypy_constructor_long_exact(vm, value, out_error);
            return exact_long;
        }
        int64_t converted;

        if (tinypy_internal_long_digits_as_i64(TINYPY_LONG_SIGN(value), TINYPY_LONG_OBJECT(value)->digits, TINYPY_LONG_DIGIT_COUNT(value), &converted) != 0) {
            tinypy_value_t *return_value_5 = tinypy_integer_from_i64(vm, converted);
            return return_value_5;
        }
        if (value->type == &vm->types[TINYPY_VALUE_LONG]) {
            return TINYPY_RET(value);
        }
        tinypy_value_t *exact_long = __tinypy_constructor_long_exact(vm, value, out_error);
        return exact_long;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        double number = TINYPY_FLOAT_OBJECT(value)->value;

        if (isnan(number)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "cannot convert float NaN to integer", out_error);
            return NULL;
        }
        if (isinf(number)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "cannot convert float infinity to integer", out_error);
            return NULL;
        }
        if (number >= -0x1p63 && number < 0x1p63) {
            tinypy_value_t *return_value_6 = force_long != 0 ? tinypy_long_from_i64(vm, (int64_t)number) : tinypy_integer_from_i64(vm, (int64_t)number);
            return return_value_6;
        }
        tinypy_value_t *return_value_7 = tinypy_internal_long_from_double(vm, number);
        return return_value_7;
    }
integer_conversion:
    method_name = force_long != 0 ? vm->internal_special_long_key : vm->internal_special_int_key;
    conversion_result = tinypy_internal_call_conversion(value, method_name, &handled, out_error);
    if (handled == 0) {
        truncated = __tinypy_constructor_call_found_attribute(value, vm->internal_special_trunc_key, &conversion_result, out_error) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
        handled = truncated;
    }
    if (handled != 0) {
        tinypy_value_t *integer = __tinypy_constructor_integer_result(vm, conversion_result, method_name, truncated, force_long, out_error);
        return integer;
    }
    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *text_integer = __tinypy_constructor_integer_text(vm, value, 10, force_long, out_error);
        return text_integer;
    }
    if (kind == TINYPY_VALUE_BUFFER || kind == TINYPY_VALUE_BYTEARRAY) {
        const uint8_t *bytes;
        size_t size;
        tinypy_value_t *character_string = NULL;
        tinypy_bool_t has_buffer;

        if (kind == TINYPY_VALUE_BUFFER) {
            tinypy_internal_exception_state_t state;
            tinypy_error_t *conversion_error = NULL;

            tinypy_internal_exception_preserve_begin(vm, &state);
            character_string = tinypy_internal_buffer_character_string(value, &conversion_error);
            if (conversion_error != NULL) {
                tinypy_error_release(conversion_error);
            }
            tinypy_internal_exception_preserve_end(vm, &state);
            has_buffer = character_string != NULL;
            if (has_buffer != 0) {
                bytes = (const uint8_t *)tinypy_string_view(character_string, &size);
            }
        } else {
            has_buffer = tinypy_internal_bytes_view(value, &bytes, &size);
        }
        if (has_buffer != 0) {
            tinypy_value_t *result = __tinypy_constructor_integer_bytes(vm, bytes, size, 10, force_long, out_error);

            if (character_string != NULL) {
                TINYPY_DECREF(character_string);
            }
            return result;
        }
    }
    tinypy_message_part_t parts[] = {
        {force_long != 0 ? "long" : "int", force_long != 0 ? 4U : 3U},
        TINYPY_MESSAGE_PART_LITERAL("() argument must be a string or a number, not '"),
        TINYPY_MESSAGE_PART_TYPE_NAME(value),
        TINYPY_MESSAGE_PART_LITERAL("'"),
    };
    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_integer_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *value = __tinypy_constructor_integer_common(type, args, kwargs, INT32_C(0), out_error);

    if (value == NULL || type == &vm->types[TINYPY_VALUE_INTEGER]) {
        return value;
    }
    /* int_subtype_new keeps the C long of whatever int() produced. */
    int64_t number = INT64_C(0);
    tinypy_bool_t fits = TINYPY_TRUE;

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG) {
        fits = tinypy_internal_long_digits_as_i64(TINYPY_LONG_SIGN(value), TINYPY_LONG_OBJECT(value)->digits, TINYPY_LONG_DIGIT_COUNT(value), &number);
    }
    else {
        number = TINYPY_INTEGER_VALUE(value);
    }
    TINYPY_DECREF(value);
    if (fits == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "Python int too large to convert to C long", out_error);
        return NULL;
    }
    tinypy_value_t *integer = tinypy_integer_from_i64(vm, number);
    tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(type, integer, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_long_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *value = __tinypy_constructor_integer_common(type, args, kwargs, INT32_C(1), out_error);

    if (type == &type->vm->types[TINYPY_VALUE_LONG]) {
        return value;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(type, value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* Whether the value has nb_float: a builtin number, a classic instance or a
   type with __float__. */
static tinypy_bool_t __tinypy_constructor_has_float(tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (__tinypy_constructor_is_integral(value) != 0 || kind == TINYPY_VALUE_FLOAT || kind == TINYPY_VALUE_COMPLEX || kind == TINYPY_VALUE_OLD_INSTANCE) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t has_float = tinypy_internal_object_has_special_key(value, TINYPY_VALUE_VM(value)->internal_special_float_key);
    return has_float;
}
//////////////////////////////////////////////////////////////////////////
/* nb_float of a value that has one: what __float__ returned, the exact
   float itself or a new float of another builtin number. */
static tinypy_value_t *__tinypy_constructor_call_float(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *classic = __tinypy_constructor_classic_conversion(value, vm->internal_special_float_key, out_error);
        return classic;
    }
    if (value->type == &vm->types[TINYPY_VALUE_FLOAT]) {
        return TINYPY_RET(value);
    }
    if ((__tinypy_constructor_is_integral(value) != 0 || kind == TINYPY_VALUE_FLOAT) && tinypy_internal_object_has_special_override_key(value, vm->internal_special_float_key) == 0) {
        double number;

        if (__tinypy_constructor_number_as_double(vm, value, &number, INT32_C(0), out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *builtin = tinypy_float_from_double(vm, number);
        return builtin;
    }
    tinypy_bool_t handled;
    tinypy_value_t *converted = tinypy_internal_call_conversion(value, vm->internal_special_float_key, &handled, out_error);

    if (handled == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "a float is required", out_error);
    }
    return converted;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_float_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *const names[1] = {vm->internal_x_key};
    tinypy_value_t *values[1];
    double number;

    if (tinypy_internal_constructor_optional_arguments(vm, "float", 5U, args, kwargs, names, 1U, 0U, values, out_error) == 0) {
        return NULL;
    }
    if (values[0] == NULL) {
        tinypy_value_t *value = tinypy_float_from_double(vm, 0.0);
        tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(type, value, out_error);
        return return_value_1;
    }
    tinypy_value_t *value = values[0];
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    if ((kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) && tinypy_internal_object_has_special_override_key(value, vm->internal_special_float_key) == 0) {
        if (__tinypy_constructor_float_text(vm, value, &number, out_error) == 0) {
            return NULL;
        }
    }
    else if (__tinypy_constructor_has_float(value) != 0) {
        tinypy_value_t *converted = __tinypy_constructor_call_float(vm, value, out_error);

        if (converted == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(converted) != TINYPY_VALUE_FLOAT) {
            __tinypy_constructor_conversion_error(vm, "__float__", 9U, "non-float", 9U, converted, out_error);
            TINYPY_DECREF(converted);
            return NULL;
        }
        /* PyNumber_Float keeps a float subclass that __float__ returned. */
        if (type == &vm->types[TINYPY_VALUE_FLOAT]) {
            return converted;
        }
        number = TINYPY_FLOAT_OBJECT(converted)->value;
        TINYPY_DECREF(converted);
    }
    else {
        const uint8_t *bytes;
        size_t size;
        tinypy_value_t *character_string = NULL;
        tinypy_bool_t has_buffer = TINYPY_FALSE;

        if (kind == TINYPY_VALUE_BUFFER) {
            tinypy_internal_exception_state_t state;
            tinypy_error_t *conversion_error = NULL;

            tinypy_internal_exception_preserve_begin(vm, &state);
            character_string = tinypy_internal_buffer_character_string(value, &conversion_error);
            if (conversion_error != NULL) {
                tinypy_error_release(conversion_error);
            }
            tinypy_internal_exception_preserve_end(vm, &state);
            has_buffer = character_string != NULL;
            if (has_buffer != 0) {
                bytes = (const uint8_t *)tinypy_string_view(character_string, &size);
            }
        } else if (kind == TINYPY_VALUE_BYTEARRAY) {
            has_buffer = tinypy_internal_bytes_view(value, &bytes, &size);
        }
        if (has_buffer != 0) {
            tinypy_bool_t parsed = __tinypy_constructor_float_bytes(vm, bytes, size, &number, TINYPY_TRUE, out_error);

            if (character_string != NULL) {
                TINYPY_DECREF(character_string);
            }
            if (parsed == 0) {
                return NULL;
            }
        }
        else {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "float() argument must be a string or a number", out_error);
            return NULL;
        }
    }
    tinypy_value_t *value_result = tinypy_float_from_double(vm, number);
    tinypy_value_t *return_value_2 = tinypy_internal_immutable_subclass_copy(type, value_result, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
/* try_complex_special_method: a classic instance's __complex__ attribute or
   any other value's special method. Returns 1 with the result, 0 when there
   is none and -1 on error. */
static int32_t __tinypy_constructor_complex_special(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t **out_result, tinypy_error_t **out_error) {
    tinypy_value_t *method;
    int32_t found = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE
        ? tinypy_internal_object_get_optional_attr_key(value, vm->internal_special_complex_key, &method, out_error)
        : tinypy_internal_object_lookup_special_key(value, vm->internal_special_complex_key, &method, out_error);

    *out_result = NULL;
    if (found <= 0) {
        return found;
    }
    tinypy_value_t *args = TINYPY_RET_EMPTY_TUPLE(vm);
    *out_result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return *out_result != NULL ? INT32_C(1) : -INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
/* complex_new keeps a complex operand whole and takes any other through
   nb_float: PyNumber_Float wants a float for the real part, while
   PyFloat_AsDouble also reads an integer for the imaginary one. */
static tinypy_bool_t __tinypy_constructor_complex_part(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_bool_t imaginary, double *out_parts, tinypy_error_t **out_error) {
    out_parts[1] = 0.0;
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_COMPLEX) {
        out_parts[0] = TINYPY_COMPLEX_OBJECT(value)->real;
        out_parts[1] = TINYPY_COMPLEX_OBJECT(value)->imaginary;
        return TINYPY_TRUE;
    }
    tinypy_value_t *converted = __tinypy_constructor_call_float(vm, value, out_error);

    if (converted == NULL) {
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(converted) != TINYPY_VALUE_FLOAT && (imaginary == 0 || __tinypy_constructor_is_integral(converted) == 0)) {
        __tinypy_constructor_conversion_error(vm, "__float__", 9U, "non-float", 9U, converted, out_error);
        TINYPY_DECREF(converted);
        return TINYPY_FALSE;
    }
    tinypy_bool_t result = __tinypy_constructor_number_as_double(vm, converted, &out_parts[0], INT32_C(0), out_error);

    TINYPY_DECREF(converted);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* complex_new checks that both operands have nb_float before it combines
   them into real + imaginary * 1j. */
static tinypy_bool_t __tinypy_constructor_complex_numbers(tinypy_vm_t *vm, tinypy_value_t *first, tinypy_value_t *second, double *out_real, double *out_imaginary, tinypy_error_t **out_error) {
    tinypy_value_t *special = NULL;
    double real_parts[2] = {0.0, 0.0};
    double imaginary_parts[2] = {0.0, 0.0};

    if (first != NULL) {
        int32_t found = __tinypy_constructor_complex_special(vm, first, &special, out_error);

        if (found < 0) {
            return TINYPY_FALSE;
        }
    }
    tinypy_value_t *real_value = special != NULL ? special : first;
    tinypy_bool_t real_is_complex = real_value != NULL && TINYPY_VALUE_KIND(real_value) == TINYPY_VALUE_COMPLEX ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_bool_t converted = (real_value == NULL || __tinypy_constructor_has_float(real_value) != 0) && (second == NULL || __tinypy_constructor_has_float(second) != 0) ? TINYPY_TRUE : TINYPY_FALSE;

    if (converted == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "complex() argument must be a string or a number", out_error);
    }
    else if (real_value != NULL) {
        converted = __tinypy_constructor_complex_part(vm, real_value, TINYPY_FALSE, real_parts, out_error);
    }
    if (special != NULL) {
        TINYPY_DECREF(special);
    }
    if (converted != 0 && second != NULL) {
        converted = __tinypy_constructor_complex_part(vm, second, TINYPY_TRUE, imaginary_parts, out_error);
    }
    *out_real = real_parts[0];
    *out_imaginary = real_parts[1];
    if (second != NULL) {
        *out_imaginary = imaginary_parts[0];
        if (TINYPY_VALUE_KIND(second) == TINYPY_VALUE_COMPLEX) {
            *out_real -= imaginary_parts[1];
        }
        if (real_is_complex != 0) {
            *out_imaginary += real_parts[1];
        }
    }
    return converted;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_complex_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *const names[2] = {vm->internal_real_key, vm->internal_imag_key};
    tinypy_value_t *values[2];
    double real = 0.0;
    double imaginary = 0.0;

    if (tinypy_internal_constructor_optional_arguments(vm, "complex", 7U, args, kwargs, names, 2U, 0U, values, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *first = values[0];
    tinypy_value_t *second = values[1];
    tinypy_bool_t first_is_text = first != NULL && (TINYPY_VALUE_KIND(first) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(first) == TINYPY_VALUE_UNICODE) ? TINYPY_TRUE : TINYPY_FALSE;
    if (first_is_text != 0 && second != NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "complex() can't take second arg if first is a string", out_error);
        return NULL;
    }
    if (second != NULL && (TINYPY_VALUE_KIND(second) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(second) == TINYPY_VALUE_UNICODE)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "complex() second arg can't be a string", out_error);
        return NULL;
    }
    if (first_is_text != 0) {
        if (__tinypy_constructor_complex_text(vm, first, &real, &imaginary, out_error) == 0) {
            return NULL;
        }
    }
    else if (first != NULL && second == NULL && first->type == &vm->types[TINYPY_VALUE_COMPLEX] && type == first->type) {
        return TINYPY_RET(first);
    }
    else if (__tinypy_constructor_complex_numbers(vm, first, second, &real, &imaginary, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value_result = tinypy_complex_from_doubles(vm, real, imaginary);
    tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(type, value_result, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_unicode_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *const names[3] = {vm->internal_string_key, vm->internal_encoding_key, vm->internal_errors_key};
    tinypy_value_t *values[3];

    if (tinypy_internal_constructor_optional_arguments(vm, "unicode", 7U, args, kwargs, names, 3U, UINT32_C(6), values, out_error) == 0) {
        return NULL;
    }
    if (values[0] == NULL) {
        tinypy_value_t *value = tinypy_unicode_from_utf8(vm, NULL, 0U);
        tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(type, value, out_error);
        return return_value_1;
    }
    tinypy_value_t *value = values[0];
    if (values[1] != NULL || values[2] != NULL) {
        tinypy_value_t *source = value;
        tinypy_value_t *result;
        tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

        if (kind == TINYPY_VALUE_UNICODE || kind == TINYPY_VALUE_BYTEARRAY) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, kind == TINYPY_VALUE_UNICODE ? "decoding Unicode is not supported" : "decoding bytearray is not supported", out_error);
            return NULL;
        }
        if (kind == TINYPY_VALUE_BUFFER) {
            /* PyObject_AsCharBuffer reads the character buffer of a
               legacy buffer, not its raw code units. */
            source = tinypy_internal_buffer_character_string(value, out_error);
            if (source == NULL) {
                return NULL;
            }
        }
        else if (kind != TINYPY_VALUE_STRING) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("coercing to Unicode: need string or buffer, "),
                TINYPY_MESSAGE_PART_TYPE_NAME(value),
                TINYPY_MESSAGE_PART_LITERAL(" found")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
            return NULL;
        }
        /* PyUnicode_FromEncodedObject decodes no empty input, and the
           decoder must return unicode. */
        result = TINYPY_TEXT_BYTE_SIZE(source) == 0U ? tinypy_unicode_from_utf8(vm, NULL, 0U) : tinypy_internal_text_codec(vm, source, values[1], values[2], TINYPY_TRUE, TINYPY_TRUE, NULL, out_error);
        if (source != value) {
            TINYPY_DECREF(source);
        }
        if (result != NULL && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_UNICODE) {
            const tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("decoder did not return an unicode object (type="),
                TINYPY_MESSAGE_PART_TYPE_NAME(result),
                TINYPY_MESSAGE_PART_LITERAL(")")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            TINYPY_DECREF(result);
            return NULL;
        }
        tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(type, result, out_error);
        return return_value_1;
    }
    /* unicode(x) returns what PyObject_Unicode returns, which keeps a unicode
       subtype instance that __unicode__ returned. */
    tinypy_value_t *converted = tinypy_internal_object_unicode(value, out_error);
    if (type == &vm->types[TINYPY_VALUE_UNICODE]) {
        return converted;
    }
    tinypy_value_t *return_value_2 = tinypy_internal_immutable_subclass_copy(type, converted, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_list_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *value;

    if (__tinypy_constructor_sequence_argument(vm, "list", 4U, args, kwargs, &value, out_error) == 0) {
        return NULL;
    }
    if (value == NULL) {
        tinypy_value_t *return_value_1 = tinypy_internal_list_from_items_checked(vm, NULL, 0U, out_error);
        return return_value_1;
    }
    if (value->type == &vm->types[TINYPY_VALUE_LIST]) {
        size_t list_size = TINYPY_LIST_SIZE(value);
        tinypy_value_t *return_value_2 = tinypy_internal_list_from_items_checked(vm, TINYPY_LIST_OBJECT(value)->items, list_size, out_error);
        return return_value_2;
    }
    if (value->type == &vm->types[TINYPY_VALUE_TUPLE]) {
        tinypy_value_t *const *tuple_items = tinypy_internal_tuple_items(value);
        size_t tuple_size = TINYPY_TUPLE_SIZE(value);
        tinypy_value_t *return_value_3 = tinypy_internal_list_from_items_checked(vm, tuple_items, tuple_size, out_error);
        return return_value_3;
    }
    tinypy_value_t *return_value_4 = __tinypy_constructor_sequence_to_list(vm, value, INT64_C(8), out_error);
    return return_value_4;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_tuple_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *result;
    tinypy_value_t *value;

    if (__tinypy_constructor_sequence_argument(vm, "tuple", 5U, args, kwargs, &value, out_error) == 0) {
        return NULL;
    }
    if (value == NULL) {
        tinypy_value_t *return_value_1 = type == &vm->types[TINYPY_VALUE_TUPLE] ? TINYPY_RET_EMPTY_TUPLE(vm) : tinypy_internal_tuple_subclass_from_items(type, NULL, 0U, out_error);
        return return_value_1;
    }
    if (type == &vm->types[TINYPY_VALUE_TUPLE] && value->type == &vm->types[TINYPY_VALUE_TUPLE]) {
        return TINYPY_RET(value);
    }
    if (value->type == &vm->types[TINYPY_VALUE_TUPLE]) {
        tinypy_value_t *selected_value_2;
        if (type == &vm->types[TINYPY_VALUE_TUPLE]) {
            tinypy_value_t *const *tuple_items = tinypy_internal_tuple_items(value);
            size_t tuple_size = TINYPY_TUPLE_SIZE(value);
            selected_value_2 = tinypy_tuple_from_items(vm, tuple_items, tuple_size);
        }
        else {
            tinypy_value_t *const *tuple_items = tinypy_internal_tuple_items(value);
            size_t tuple_size = TINYPY_TUPLE_SIZE(value);
            selected_value_2 = tinypy_internal_tuple_subclass_from_items(type, tuple_items, tuple_size, out_error);
        }
        return selected_value_2;
    }
    if (value->type == &vm->types[TINYPY_VALUE_LIST]) {
        tinypy_value_t *selected_value_3;
        if (type == &vm->types[TINYPY_VALUE_TUPLE]) {
            size_t list_size = TINYPY_LIST_SIZE(value);
            selected_value_3 = tinypy_internal_tuple_from_items_checked(vm, TINYPY_LIST_OBJECT(value)->items, list_size, out_error);
        }
        else {
            size_t list_size = TINYPY_LIST_SIZE(value);
            selected_value_3 = tinypy_internal_tuple_subclass_from_items(type, TINYPY_LIST_OBJECT(value)->items, list_size, out_error);
        }
        return selected_value_3;
    }
    tinypy_value_t *list = __tinypy_constructor_sequence_to_list(vm, value, INT64_C(10), out_error);
    if (list == NULL) {
        return NULL;
    }
    tinypy_value_t *selected_value;
    if (type == &vm->types[TINYPY_VALUE_TUPLE]) {
        size_t list_size = TINYPY_LIST_SIZE(list);
        selected_value = tinypy_internal_tuple_from_items_checked(vm, TINYPY_LIST_OBJECT(list)->items, list_size, out_error);
    }
    else {
        size_t list_size = TINYPY_LIST_SIZE(list);
        selected_value = tinypy_internal_tuple_subclass_from_items(type, TINYPY_LIST_OBJECT(list)->items, list_size, out_error);
    }
    result = selected_value;
    TINYPY_DECREF(list);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_dict_update(tinypy_value_t *result, tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_bool_t return_value_1 = tinypy_internal_dict_update_from(result, source, "NULL result without error in PyObject_Call", out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if (__tinypy_constructor_argument_count(vm, "dict", 4U, args, 0U, 1U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_dict_new_checked(vm, out_error);
    if (result == NULL) {
        return NULL;
    }
    tinypy_bool_t condition_2 = TINYPY_TUPLE_SIZE(args) == 1U;
    if (condition_2 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        condition_2 = __tinypy_constructor_dict_update(result, item, out_error) == 0;
    }
    if (condition_2) {
        TINYPY_DECREF(result);
        return NULL;
    }
    if (kwargs != NULL && __tinypy_constructor_dict_update(result, kwargs, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    (void)user_data;

    if (TINYPY_TUPLE_SIZE(args) == 0U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype((tinypy_type_t *)TINYPY_TUPLE_GET(args, 0U), &vm->types[TINYPY_VALUE_TYPE]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__new__ requires a subtype of type", out_error);
        return NULL;
    }
    tinypy_type_t *metaclass = (tinypy_type_t *)TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *arguments = __tinypy_constructor_tail_arguments(vm, args);
    tinypy_value_t *result = __tinypy_constructor_type_create_new(metaclass, arguments, kwargs, out_error);

    TINYPY_DECREF(arguments);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    (void)user_data;

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__init__() takes no keyword arguments", out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) != 2U && TINYPY_TUPLE_SIZE(args) != 4U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__init__() takes 1 or 3 arguments", out_error);
        return NULL;
    }
    tinypy_value_t *result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* tp_new_wrapper: the class that the __new__ of owner receives first,
   which must be a subtype of owner. */
static tinypy_type_t *__tinypy_constructor_new_receiver(tinypy_vm_t *vm, const tinypy_type_t *owner, tinypy_value_t *args, tinypy_error_t **out_error) {
    tinypy_message_part_t owner_name[3];

    tinypy_internal_type_message_name(owner, owner_name);
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_message_part_t parts[] = {
            owner_name[0],
            owner_name[1],
            owner_name[2],
            TINYPY_MESSAGE_PART_LITERAL(".__new__(): not enough arguments"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    tinypy_value_t *receiver = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(receiver) != TINYPY_VALUE_TYPE) {
        tinypy_message_part_t parts[] = {
            owner_name[0],
            owner_name[1],
            owner_name[2],
            TINYPY_MESSAGE_PART_LITERAL(".__new__(X): X is not a type object ("),
            TINYPY_MESSAGE_PART_TYPE_NAME(receiver),
            TINYPY_MESSAGE_PART_LITERAL(")"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    tinypy_type_t *type = (tinypy_type_t *)receiver;
    if (tinypy_type_is_subtype(type, owner) == 0) {
        tinypy_message_part_t type_name[3];

        tinypy_internal_type_message_name(type, type_name);
        tinypy_message_part_t parts[] = {
            owner_name[0],
            owner_name[1],
            owner_name[2],
            TINYPY_MESSAGE_PART_LITERAL(".__new__("),
            type_name[0],
            type_name[1],
            type_name[2],
            TINYPY_MESSAGE_PART_LITERAL("): "),
            type_name[0],
            type_name[1],
            type_name[2],
            TINYPY_MESSAGE_PART_LITERAL(" is not a subtype of "),
            owner_name[0],
            owner_name[1],
            owner_name[2],
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    return type;
}
//////////////////////////////////////////////////////////////////////////
/* object.__new__ with the arguments that follow the class, which class
   calls pass without building the argument tuple. */
tinypy_value_t *tinypy_internal_object_new_items(tinypy_type_t *class_type, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = class_type->vm;

    TINYPY_CLEAR_ERROR(out_error);
    /* tp_new_wrapper: the nearest base not created by Python code must
       still use object.__new__, which only object itself and a native type
       without a __new__ of its own do; the other built-in types have their
       own constructor or none at all. */
    const tinypy_type_t *static_base = class_type;
    while ((static_base->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) != 0U && static_base->base_type != NULL) {
        static_base = static_base->base_type;
    }
    tinypy_bool_t object_base = static_base == &vm->types[TINYPY_VALUE_INSTANCE] ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_value_t *object_new = object_base == 0 ? tinypy_type_get_attr_key(&vm->types[TINYPY_VALUE_INSTANCE], vm->internal_special_new_key) : NULL;
    if (object_base == 0 && (static_base->layout_kind != TINYPY_VALUE_NATIVE_INSTANCE || tinypy_type_get_attr_key(static_base, vm->internal_special_new_key) != object_new)) {
        tinypy_message_part_t class_name[3];
        tinypy_message_part_t base_name[3];

        tinypy_internal_type_message_name(class_type, class_name);
        tinypy_internal_type_message_name(static_base, base_name);
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("object.__new__("),
            class_name[0],
            class_name[1],
            class_name[2],
            TINYPY_MESSAGE_PART_LITERAL(") is not safe, use "),
            base_name[0],
            base_name[1],
            base_name[2],
            TINYPY_MESSAGE_PART_LITERAL(".__new__()"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if ((class_type->flags & TINYPY_TYPE_FLAG_ABSTRACT) != 0U) {
        __tinypy_constructor_abstract_error(class_type, out_error);
        return NULL;
    }
    tinypy_value_type_e layout_kind = class_type->layout_kind;
    if (__tinypy_constructor_has_immutable_builtin_layout(layout_kind) != 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__new__ cannot create immutable builtin instances", out_error);
        return NULL;
    }
    if (layout_kind == TINYPY_VALUE_SET || layout_kind == TINYPY_VALUE_FROZENSET) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__new__ cannot create set instances", out_error);
        return NULL;
    }
    if (__tinypy_constructor_has_mutable_builtin_layout(layout_kind) == 0 && (count != 0U || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U))) {
        tinypy_value_t *type_init = tinypy_internal_type_lookup_key(vm, class_type, vm->internal_special_init_key);
        tinypy_value_t *object_init = tinypy_internal_type_lookup_key(vm, &vm->types[TINYPY_VALUE_INSTANCE], vm->internal_special_init_key);

        /* CPython 2.7 accepts these arguments when __init__ is overridden.
         * When both __new__ and __init__ are overridden it also emits a
         * DeprecationWarning, which tinypy does not expose. */
        if (type_init == object_init) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object() takes no parameters", out_error);
            return NULL;
        }
    }
    tinypy_value_t *return_value_1;
    if (layout_kind == TINYPY_VALUE_NATIVE_INSTANCE) {
        tinypy_value_t *constructor_args = tinypy_tuple_from_items(vm, items, count);

        return_value_1 = tinypy_internal_object_allocate_checked(vm, class_type, class_type->basic_size, out_error);
        if (return_value_1 == NULL) {
            TINYPY_DECREF(constructor_args);
            return NULL;
        }
        if (tinypy_native_instance_construct(return_value_1, constructor_args, kwargs, out_error) == 0) {
            TINYPY_DECREF(return_value_1);
            return_value_1 = NULL;
        }
        TINYPY_DECREF(constructor_args);
    }
    else {
        return_value_1 = tinypy_internal_object_allocate_checked(vm, class_type, class_type->basic_size, out_error);
        if (return_value_1 == NULL) {
            return NULL;
        }
        if (layout_kind == TINYPY_VALUE_DICT) {
            tinypy_internal_dict_initialize_empty(return_value_1);
        }
        else if (layout_kind == TINYPY_VALUE_SET) {
            tinypy_internal_set_initialize_empty(return_value_1);
        }
    }
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    tinypy_type_t *class_type = __tinypy_constructor_new_receiver(vm, &vm->types[TINYPY_VALUE_INSTANCE], args, out_error);
    if (class_type == NULL) {
        return NULL;
    }
    tinypy_value_t *const *items = tinypy_internal_tuple_items(args);
    tinypy_value_t *result = tinypy_internal_object_new_items(class_type, items + 1U, TINYPY_TUPLE_SIZE(args) - 1U, kwargs, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_basestring_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)args;
    (void)kwargs;
    (void)user_data;
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "The basestring type cannot be instantiated", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_basestring_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)args;
    (void)kwargs;
    tinypy_internal_make_vm_error(type->vm, TINYPY_ERROR_TYPE, "The basestring type cannot be instantiated", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__init__ requires an instance", out_error);
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e layout_kind = self->type->layout_kind;
    /* dict.__init__ merges into the existing mapping; list, set and bytearray
       all re-initialise, matching dict_update_common versus list_init/set_init. */
    if (layout_kind == TINYPY_VALUE_DICT) {
        tinypy_value_t *constructor_args = __tinypy_constructor_tail_arguments(vm, args);
        tinypy_bool_t merged = TINYPY_TRUE;

        if (__tinypy_constructor_argument_count(vm, "dict", 4U, constructor_args, 0U, 1U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
            merged = TINYPY_FALSE;
        }
        else if (TINYPY_TUPLE_SIZE(constructor_args) == 1U) {
            merged = __tinypy_constructor_dict_update(self, TINYPY_TUPLE_GET(constructor_args, 0U), out_error);
        }
        TINYPY_DECREF(constructor_args);
        if (merged != 0 && kwargs != NULL) {
            merged = __tinypy_constructor_dict_update(self, kwargs, out_error);
        }
        if (merged == 0) {
            return NULL;
        }
        tinypy_value_t *merged_result = TINYPY_RET_NONE(vm);
        return merged_result;
    }
    if (layout_kind == TINYPY_VALUE_LIST) {
        tinypy_value_t *constructor_args = __tinypy_constructor_tail_arguments(vm, args);
        tinypy_value_t *source;

        if (__tinypy_constructor_sequence_argument(vm, "list", 4U, constructor_args, kwargs, &source, out_error) == 0) {
            TINYPY_DECREF(constructor_args);
            return NULL;
        }
        tinypy_list_clear(self);
        if (source != NULL && tinypy_internal_list_extend_iterable(self, source, "NULL result without error in PyObject_Call", out_error) == 0) {
            TINYPY_DECREF(constructor_args);
            return NULL;
        }
        TINYPY_DECREF(constructor_args);
        tinypy_value_t *initialized_result = TINYPY_RET_NONE(vm);
        return initialized_result;
    }
    if (layout_kind == TINYPY_VALUE_SET) {
        tinypy_value_t *constructor_args = __tinypy_constructor_tail_arguments(vm, args);
        tinypy_bool_t initialized = tinypy_internal_set_initialize(self, constructor_args, kwargs, out_error);

        TINYPY_DECREF(constructor_args);
        if (initialized == 0) {
            return NULL;
        }
        tinypy_value_t *initialized_result = TINYPY_RET_NONE(vm);
        return initialized_result;
    }
    if (layout_kind == TINYPY_VALUE_BYTEARRAY) {
        tinypy_value_t *constructor_args = __tinypy_constructor_tail_arguments(vm, args);
        tinypy_bool_t initialized = tinypy_internal_bytearray_initialize(self, constructor_args, kwargs, out_error);

        TINYPY_DECREF(constructor_args);
        if (initialized == 0) {
            return NULL;
        }
        tinypy_value_t *initialized_result = TINYPY_RET_NONE(vm);
        return initialized_result;
    }
    if (__tinypy_constructor_has_mutable_builtin_layout(layout_kind) != 0) {
        tinypy_value_t *constructor_args = __tinypy_constructor_tail_arguments(vm, args);
        tinypy_type_t *builtin_type = &vm->types[layout_kind];
        tinypy_value_t *initialized = builtin_type->create(builtin_type, constructor_args, kwargs, out_error);

        TINYPY_DECREF(constructor_args);
        if (initialized == NULL) {
            return NULL;
        }
        switch (layout_kind) {
        case TINYPY_VALUE_LIST:
            tinypy_internal_list_swap_contents(self, initialized);
            break;
        case TINYPY_VALUE_DICT:
            tinypy_internal_dict_swap_contents(self, initialized);
            break;
        case TINYPY_VALUE_SET:
            tinypy_internal_set_swap_contents(self, initialized);
            break;
        case TINYPY_VALUE_BYTEARRAY:
            if (tinypy_internal_bytearray_resize_allowed(self, TINYPY_SIZED_SIZE(initialized), out_error) == 0) {
                TINYPY_DECREF(initialized);
                return NULL;
            }
            tinypy_internal_bytearray_swap_contents(self, initialized);
            break;
        default:
            break;
        }
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_constructor_rebuild_container_diagnostics(self);
        __tinypy_constructor_rebuild_container_diagnostics(initialized);
#endif
        TINYPY_DECREF(initialized);
        tinypy_value_t *return_value = TINYPY_RET_NONE(vm);
        return return_value;
    }
    if (__tinypy_constructor_object_has_excess_arguments(args, kwargs) != 0) {
        tinypy_type_t *type = self->type;
        tinypy_value_t *type_new = tinypy_type_get_attr_key(type, type->vm->internal_special_new_key);
        tinypy_value_t *object_new = tinypy_type_get_attr_key(&vm->types[TINYPY_VALUE_INSTANCE], vm->internal_special_new_key);

        /* Symmetrically, CPython 2.7 accepts these arguments when __new__ is
         * overridden, with the same unexposed warning when both are custom. */
        if (type_new == object_new) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__init__() takes no parameters", out_error);
            return NULL;
        }
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_object_attribute_name(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(name) == TINYPY_VALUE_UNICODE) {
        return TINYPY_TRUE;
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("attribute name must be string, not '"),
        TINYPY_MESSAGE_PART_TYPE_NAME(name),
        TINYPY_MESSAGE_PART_LITERAL("'")
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_getattribute_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *name = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_constructor_object_attribute_name(vm, name, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_object_get_base_attr_key(TINYPY_TUPLE_GET(args, 0U), name, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* hackcheck: the __setattr__ and __delattr__ wrappers of object and type
   refuse objects whose built-in type assigns attributes another way. */
static tinypy_bool_t __tinypy_constructor_object_hackcheck(tinypy_value_t *self, void *user_data, const char *what, size_t what_size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(self);
    const tinypy_type_t *type = &vm->types[TINYPY_VALUE_INSTANCE];

    if (kind == TINYPY_VALUE_TYPE || kind == TINYPY_VALUE_CLASS || kind == TINYPY_VALUE_OLD_INSTANCE) {
        type = &vm->types[kind];
    }
    else if ((self->type->flags & TINYPY_TYPE_FLAG_NEEDS_ATTRIBUTE_READY) != 0U) {
        type = self->type;
    }
    if (type == &vm->types[(tinypy_value_type_e)(intptr_t)user_data]) {
        return TINYPY_TRUE;
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("can't apply this "),
        {what, what_size},
        TINYPY_MESSAGE_PART_LITERAL(" to "),
        {type->name, type->name_size},
        TINYPY_MESSAGE_PART_LITERAL(" object")
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_setattr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 3U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_constructor_object_hackcheck(self, user_data, "__setattr__", 11U, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *name = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_constructor_object_attribute_name(vm, name, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (tinypy_internal_object_set_attr_key(TINYPY_TUPLE_GET(args, 0U), name, TINYPY_TUPLE_GET(args, 2U), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_delattr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_constructor_object_hackcheck(self, user_data, "__delattr__", 11U, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *name = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_constructor_object_attribute_name(vm, name, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (tinypy_internal_object_delete_attr_key(TINYPY_TUPLE_GET(args, 0U), name, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* The __getattribute__ entry of a type whose tp_getattro is the generic
   attribute lookup. */
void tinypy_internal_type_add_object_getattribute_method(tinypy_type_t *type) {
    tinypy_internal_type_add_method(type, type->vm->internal_special_getattribute_key, __tinypy_constructor_object_getattribute_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_add_object_attribute_methods(tinypy_type_t *type) {
    tinypy_vm_t *vm = type->vm;

    tinypy_internal_type_add_object_getattribute_method(type);
    tinypy_internal_type_add_method(type, vm->internal_special_setattr_key, __tinypy_constructor_object_setattr_method, (void *)(intptr_t)TINYPY_VALUE_INSTANCE, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_delattr_key, __tinypy_constructor_object_delattr_method, (void *)(intptr_t)TINYPY_VALUE_INSTANCE, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_hash_t hash = (tinypy_hash_t)((uintptr_t)TINYPY_TUPLE_GET(args, 0U) >> 4U);
    if (hash == (tinypy_hash_t)-1) {
        hash = (tinypy_hash_t)-2;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)hash);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_format_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *spec = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(spec) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(spec) != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "argument to __format__ must be unicode or str", out_error);
        return NULL;
    }
    /* object.__format__ stringifies and then applies the spec to that string,
       the way object_format does in Python 2.7. */
    tinypy_bool_t spec_unicode = TINYPY_VALUE_KIND(spec) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_value_t *text = spec_unicode != 0
                               ? tinypy_internal_object_unicode(TINYPY_TUPLE_GET(args, 0U), out_error)
                               : tinypy_object_str(TINYPY_TUPLE_GET(args, 0U), out_error);

    if (text == NULL) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_string_format_object(vm, text, spec, out_error);

    TINYPY_DECREF(text);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* object_subclasshook accepts any positional arguments. */
static tinypy_value_t *__tinypy_constructor_object_subclasshook_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    (void)args;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NOT_IMPLEMENTED(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_call_with_items(tinypy_vm_t *vm, tinypy_value_t *callable, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error) {
    tinypy_value_t *call_args = tinypy_tuple_from_items(vm, items, item_count);
    tinypy_value_t *result = tinypy_call(callable, call_args, NULL, out_error);

    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_copy_reg_function(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_value_t *module = tinypy_import_module_key(vm->internal_copy_reg_module_name, NULL, NULL, INT32_C(0), out_error);

    if (module == NULL) {
        return NULL;
    }
    tinypy_value_t *function = tinypy_object_get_attr_value(module, name, out_error);
    TINYPY_DECREF(module);
    return function;
}
//////////////////////////////////////////////////////////////////////////
/* Protocol 2 carries __slots__ values beside the instance dictionary, so that
   pickle and copy can restore attributes that live outside __dict__. */
static tinypy_bool_t __tinypy_constructor_append_slot_state(tinypy_vm_t *vm, tinypy_value_t *self, tinypy_value_t *class_value, tinypy_value_t **in_out_state, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE) {
        return TINYPY_TRUE;
    }
    tinypy_value_t *class_dict = ((tinypy_type_t *)class_value)->dict;
    tinypy_value_t *names = class_dict != NULL ? tinypy_internal_dict_get_optional_suppressed(vm, class_dict, vm->internal_special_slotnames_key) : NULL;
    tinypy_value_t *slots;
    tinypy_value_t *combined;
    size_t index;

    if (names != NULL && TINYPY_VALUE_KIND(names) == TINYPY_VALUE_LIST) {
        TINYPY_INCREF(names);
    }
    else {
        tinypy_value_t *slotnames_function = __tinypy_constructor_copy_reg_function(vm, vm->internal_slotnames_key, out_error);

        if (slotnames_function == NULL) {
            return TINYPY_FALSE;
        }
        names = __tinypy_constructor_call_with_items(vm, slotnames_function, &class_value, 1U, out_error);
        TINYPY_DECREF(slotnames_function);
        if (names == NULL) {
            return TINYPY_FALSE;
        }
    }
    if (TINYPY_VALUE_KIND(names) == TINYPY_VALUE_NONE) {
        TINYPY_DECREF(names);
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(names) != TINYPY_VALUE_LIST) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "copy_reg._slotnames didn't return a list or None", out_error);
        TINYPY_DECREF(names);
        return TINYPY_FALSE;
    }
    if (TINYPY_LIST_SIZE(names) == 0U) {
        TINYPY_DECREF(names);
        return TINYPY_TRUE;
    }
    slots = tinypy_dict_new(vm);
    for (index = 0U; index < TINYPY_LIST_SIZE(names); ++index) {
        tinypy_value_t *name = TINYPY_RET(TINYPY_LIST_GET(names, index));
        tinypy_error_t *lookup_error = NULL;
        tinypy_value_t *value = tinypy_object_get_attr_value(self, name, &lookup_error);

        if (value == NULL) {
            TINYPY_DECREF(name);
            if (lookup_error != NULL) {
                tinypy_error_release(lookup_error);
            }
            tinypy_internal_exception_clear_raised(vm);
            continue;
        }
        tinypy_bool_t inserted = tinypy_internal_dict_set_checked(vm, slots, name, value, out_error);
        TINYPY_DECREF(name);
        TINYPY_DECREF(value);
        if (inserted == TINYPY_FALSE) {
            TINYPY_DECREF(names);
            TINYPY_DECREF(slots);
            return TINYPY_FALSE;
        }
    }
    TINYPY_DECREF(names);
    if (TINYPY_DICT_SIZE(slots) == 0U) {
        TINYPY_DECREF(slots);
        return TINYPY_TRUE;
    }
    tinypy_value_t *items[2] = {*in_out_state, slots};

    combined = tinypy_tuple_from_items(vm, items, 2U);
    TINYPY_DECREF(slots);
    TINYPY_DECREF(*in_out_state);
    *in_out_state = combined;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_reduce_optional_attribute(tinypy_value_t *self, tinypy_value_t *name) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
    tinypy_internal_exception_state_t state;
    tinypy_error_t *error = NULL;

    tinypy_internal_exception_preserve_begin(vm, &state);
    tinypy_value_t *value = tinypy_object_get_attr_value(self, name, &error);
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_internal_exception_preserve_end(vm, &state);
    return value;
}
//////////////////////////////////////////////////////////////////////////
/* set_reduce and bytearray_reduce pass the instance __dict__ as the state,
   or None when getting it fails. */
static tinypy_value_t *__tinypy_constructor_reduce_instance_dict(tinypy_value_t *self) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
    tinypy_value_t *dict = __tinypy_constructor_reduce_optional_attribute(self, vm->internal_special_dict_key);

    if (dict == NULL) {
        return TINYPY_RET_NONE(vm);
    }
    return dict;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_common_reduce(tinypy_value_t *self, tinypy_value_t *protocol_value, int64_t protocol, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);

    if (protocol < 2) {
        tinypy_value_t *reducer = __tinypy_constructor_copy_reg_function(vm, vm->internal_reduce_ex_key, out_error);

        if (reducer == NULL) {
            return NULL;
        }
        tinypy_value_t *items[] = {self, protocol_value};
        tinypy_value_t *result = __tinypy_constructor_call_with_items(vm, reducer, items, 2U, out_error);
        TINYPY_DECREF(reducer);
        return result;
    }

    tinypy_value_t *class_value = tinypy_object_get_attr_value(self, vm->internal_special_class_key, out_error);
    tinypy_value_t *newobj = NULL;
    tinypy_value_t *new_arguments = NULL;
    tinypy_value_t *constructor_arguments = NULL;
    tinypy_value_t *state = NULL;
    tinypy_value_t *list_items = NULL;
    tinypy_value_t *dict_items = NULL;
    tinypy_value_t *result = NULL;

    if (class_value == NULL) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(class_value) == TINYPY_VALUE_TYPE && ((tinypy_type_t *)class_value)->create == NULL
        && (((tinypy_type_t *)class_value)->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) == 0U) {
        tinypy_type_t *class_type = (tinypy_type_t *)class_value;
        size_t name_size = class_type->name_size < 200U ? class_type->name_size : 200U;
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("can't pickle "), {class_type->name, name_size}, TINYPY_MESSAGE_PART_LITERAL(" objects")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
        goto cleanup;
    }
    tinypy_value_t *getnewargs = __tinypy_constructor_reduce_optional_attribute(self, vm->internal_special_getnewargs_key);
    if (getnewargs != NULL) {
        new_arguments = __tinypy_constructor_call_with_items(vm, getnewargs, NULL, 0U, out_error);
        TINYPY_DECREF(getnewargs);
        if (new_arguments == NULL) {
            goto cleanup;
        }
        if (TINYPY_VALUE_KIND(new_arguments) != TINYPY_VALUE_TUPLE) {
            size_t name_size = new_arguments->type->name_size < 200U ? new_arguments->type->name_size : 200U;
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("__getnewargs__ should return a tuple, not '"),
                {new_arguments->type->name, name_size}, TINYPY_MESSAGE_PART_LITERAL("'")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
            goto cleanup;
        }
    }
    else {
        new_arguments = TINYPY_RET_EMPTY_TUPLE(vm);
    }
    tinypy_value_t *getstate = __tinypy_constructor_reduce_optional_attribute(self, vm->internal_special_getstate_key);
    if (getstate != NULL) {
        state = __tinypy_constructor_call_with_items(vm, getstate, NULL, 0U, out_error);
        TINYPY_DECREF(getstate);
        if (state == NULL) {
            goto cleanup;
        }
    }
    else {
        state = __tinypy_constructor_reduce_optional_attribute(self, vm->internal_special_dict_key);
        if (state == NULL) {
            state = TINYPY_RET_NONE(vm);
        }
        if (__tinypy_constructor_append_slot_state(vm, self, class_value, &state, out_error) == 0) {
            goto cleanup;
        }
    }
    if (TINYPY_VALUE_KIND(self) == TINYPY_VALUE_LIST) {
        list_items = tinypy_iter(self, out_error);
        if (list_items == NULL) {
            goto cleanup;
        }
    }
    else {
        list_items = TINYPY_RET_NONE(vm);
    }
    if (TINYPY_VALUE_KIND(self) == TINYPY_VALUE_DICT) {
        tinypy_value_t *iteritems = tinypy_object_get_attr_value(self, vm->internal_iteritems_key, out_error);

        if (iteritems == NULL) {
            goto cleanup;
        }
        dict_items = __tinypy_constructor_call_with_items(vm, iteritems, NULL, 0U, out_error);
        TINYPY_DECREF(iteritems);
        if (dict_items == NULL) {
            goto cleanup;
        }
    }
    else {
        dict_items = TINYPY_RET_NONE(vm);
    }
    newobj = __tinypy_constructor_copy_reg_function(vm, vm->internal_special_newobj_key, out_error);
    if (newobj == NULL) {
        goto cleanup;
    }
    constructor_arguments = tinypy_internal_tuple_prepend_checked(vm, class_value, new_arguments, out_error);
    if (constructor_arguments == NULL) {
        goto cleanup;
    }
    tinypy_value_t *reduce_items[] = {newobj, constructor_arguments, state, list_items, dict_items};

    result = tinypy_tuple_from_items(vm, reduce_items, 5U);

cleanup:
    if (dict_items != NULL) {
        TINYPY_DECREF(dict_items);
    }
    if (list_items != NULL) {
        TINYPY_DECREF(list_items);
    }
    if (state != NULL) {
        TINYPY_DECREF(state);
    }
    if (constructor_arguments != NULL) {
        TINYPY_DECREF(constructor_arguments);
    }
    if (new_arguments != NULL) {
        TINYPY_DECREF(new_arguments);
    }
    if (newobj != NULL) {
        TINYPY_DECREF(newobj);
    }
    TINYPY_DECREF(class_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_reduce_protocol(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, int64_t *out_protocol, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    *out_protocol = INT64_C(0);
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        tinypy_value_t *protocol_value = TINYPY_TUPLE_GET(args, 1U);

        if (TINYPY_VALUE_KIND(protocol_value) == TINYPY_VALUE_FLOAT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument expected, got float", out_error);
            return TINYPY_FALSE;
        }
        if (tinypy_internal_number_as_i64(protocol_value, out_protocol, out_error) == TINYPY_FALSE) {
            return TINYPY_FALSE;
        }
        if (*out_protocol < INT32_MIN || *out_protocol > INT32_MAX) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, *out_protocol < INT32_MIN ? "signed integer is less than minimum" : "signed integer is greater than maximum", out_error);
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_reduce_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t protocol;

    (void)user_data;
    if (__tinypy_constructor_reduce_protocol(function, args, kwargs, &protocol, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *protocol_value = tinypy_integer_from_i64(vm, protocol);
    tinypy_value_t *result = __tinypy_constructor_object_common_reduce(TINYPY_TUPLE_GET(args, 0U), protocol_value, protocol, out_error);
    TINYPY_DECREF(protocol_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_reduce_ex_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t protocol;

    (void)user_data;
    if (__tinypy_constructor_reduce_protocol(function, args, kwargs, &protocol, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *reduce = __tinypy_constructor_reduce_optional_attribute(self, vm->internal_special_reduce_key);

    if (reduce != NULL) {
        tinypy_value_t *class_value = tinypy_object_get_attr_value(self, vm->internal_special_class_key, out_error);
        tinypy_value_t *class_reduce = class_value != NULL ? tinypy_object_get_attr_value(class_value, vm->internal_special_reduce_key, out_error) : NULL;

        if (class_value != NULL) {
            TINYPY_DECREF(class_value);
        }
        if (class_reduce == NULL) {
            TINYPY_DECREF(reduce);
            return NULL;
        }
        tinypy_value_t *object_reduce = tinypy_type_get_attr_key(&vm->types[TINYPY_VALUE_INSTANCE], vm->internal_special_reduce_key);
        tinypy_bool_t overridden = class_reduce != object_reduce;

        TINYPY_DECREF(class_reduce);
        if (overridden != TINYPY_FALSE) {
            tinypy_value_t *result = __tinypy_constructor_call_with_items(vm, reduce, NULL, 0U, out_error);

            TINYPY_DECREF(reduce);
            return result;
        }
        TINYPY_DECREF(reduce);
    }
    tinypy_value_t *protocol_value = tinypy_integer_from_i64(vm, protocol);
    tinypy_value_t *result = __tinypy_constructor_object_common_reduce(self, protocol_value, protocol, out_error);

    TINYPY_DECREF(protocol_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_copy_reg_newobj(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *type_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(type_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__newobj__ requires a type", out_error);
        return NULL;
    }
    tinypy_value_t *constructor = tinypy_object_get_attr_value(type_value, vm->internal_special_new_key, out_error);
    if (constructor == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_call(constructor, args, NULL, out_error);
    TINYPY_DECREF(constructor);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_copy_reg_reconstructor(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *instance = NULL;

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 3U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *type_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *base_value = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *state = TINYPY_TUPLE_GET(args, 2U);
    if (TINYPY_VALUE_KIND(type_value) != TINYPY_VALUE_TYPE || TINYPY_VALUE_KIND(base_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "_reconstructor requires type arguments", out_error);
        return NULL;
    }
    tinypy_type_t *base = (tinypy_type_t *)base_value;
    tinypy_value_t *constructor = tinypy_internal_object_get_attr_key(base_value, vm->internal_special_new_key, out_error);
    if (constructor == NULL) {
        return NULL;
    }
    if (base == &vm->types[TINYPY_VALUE_INSTANCE]) {
        tinypy_value_t *items[] = {type_value};

        instance = __tinypy_constructor_call_with_items(vm, constructor, items, 1U, out_error);
    }
    else {
        tinypy_value_t *items[] = {type_value, state};

        instance = __tinypy_constructor_call_with_items(vm, constructor, items, 2U, out_error);
    }
    TINYPY_DECREF(constructor);
    if (instance == NULL || base == &vm->types[TINYPY_VALUE_INSTANCE]) {
        return instance;
    }
    tinypy_value_t *initializer = tinypy_internal_type_lookup_key(vm, base, vm->internal_special_init_key);
    tinypy_value_t *object_initializer = tinypy_internal_type_lookup_key(vm, &vm->types[TINYPY_VALUE_INSTANCE], vm->internal_special_init_key);
    if (initializer != NULL && initializer != object_initializer) {
        tinypy_value_t *items[] = {instance, state};
        tinypy_value_t *initialized = __tinypy_constructor_call_with_items(vm, initializer, items, 2U, out_error);

        if (initialized == NULL) {
            TINYPY_DECREF(instance);
            return NULL;
        }
        TINYPY_DECREF(initialized);
    }
    return instance;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_copy_reg_reduce_ex(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *base = NULL;
    tinypy_value_t *base_state = NULL;
    tinypy_value_t *class_value = NULL;
    tinypy_value_t *mro = NULL;
    tinypy_value_t *iterator = NULL;
    tinypy_value_t *instance_state = NULL;
    tinypy_value_t *reconstructor = NULL;
    tinypy_value_t *constructor_args = NULL;
    tinypy_value_t *result = NULL;

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *protocol = TINYPY_TUPLE_GET(args, 1U);
    if ((TINYPY_VALUE_KIND(protocol) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(protocol) != TINYPY_VALUE_INTEGER) || TINYPY_INTEGER_VALUE(protocol) >= 2) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "_reduce_ex requires a protocol below 2", out_error);
        return NULL;
    }
    class_value = tinypy_object_get_attr_value(self, vm->internal_special_class_key, out_error);
    if (class_value == NULL) {
        goto cleanup;
    }
    mro = tinypy_object_get_attr_value(class_value, vm->internal_special_mro_key, out_error);
    TINYPY_DECREF(class_value);
    class_value = NULL;
    if (mro == NULL) {
        goto cleanup;
    }
    iterator = tinypy_iter(mro, out_error);
    if (iterator == NULL) {
        goto cleanup;
    }
    for (;;) {
        tinypy_value_t *candidate = tinypy_next(iterator, out_error);

        if (candidate == NULL) {
            if (vm->raised_value != NULL) {
                goto cleanup;
            }
            break;
        }
        tinypy_value_t *present = __tinypy_constructor_reduce_optional_attribute(candidate, vm->internal_special_flags_key);

        if (present == NULL) {
            TINYPY_DECREF(candidate);
            continue;
        }
        TINYPY_DECREF(present);
        tinypy_value_t *flags = tinypy_object_get_attr_value(candidate, vm->internal_special_flags_key, out_error);
        tinypy_value_t *heap_flag = tinypy_integer_from_i64(vm, INT64_C(1) << 9U);
        tinypy_value_t *masked = flags != NULL ? tinypy_bit_and(flags, heap_flag, out_error) : NULL;

        TINYPY_DECREF(heap_flag);
        if (flags != NULL) {
            TINYPY_DECREF(flags);
        }
        int32_t is_heap = masked != NULL ? tinypy_truth(masked, out_error) : -INT32_C(1);

        if (masked != NULL) {
            TINYPY_DECREF(masked);
        }
        if (is_heap < 0) {
            TINYPY_DECREF(candidate);
            goto cleanup;
        }
        if (is_heap == 0) {
            base = candidate;
            break;
        }
        TINYPY_DECREF(candidate);
    }
    TINYPY_DECREF(iterator);
    iterator = NULL;
    TINYPY_DECREF(mro);
    mro = NULL;
    if (base == NULL) {
        base = TINYPY_RET(&vm->types[TINYPY_VALUE_INSTANCE].base.base);
    }
    if (base == &vm->types[TINYPY_VALUE_INSTANCE].base.base) {
        base_state = TINYPY_RET_NONE(vm);
    }
    else {
        class_value = tinypy_object_get_attr_value(self, vm->internal_special_class_key, out_error);
        if (class_value == NULL) {
            goto cleanup;
        }
        tinypy_bool_t same_base = base == class_value;

        TINYPY_DECREF(class_value);
        class_value = NULL;
        if (same_base != TINYPY_FALSE) {
            tinypy_value_t *name = tinypy_object_get_attr_value(base, vm->internal_special_name_key, out_error);
            tinypy_value_t *text = name != NULL ? tinypy_object_str(name, out_error) : NULL;

            if (name != NULL) {
                TINYPY_DECREF(name);
            }
            if (text != NULL) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("can't pickle "),
                    {(const char *)TINYPY_TEXT_BYTES(text), TINYPY_TEXT_BYTE_SIZE(text)},
                    TINYPY_MESSAGE_PART_LITERAL(" objects")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
                TINYPY_DECREF(text);
            }
            goto cleanup;
        }
        tinypy_value_t *base_args = tinypy_tuple_from_items(vm, &self, 1U);

        base_state = tinypy_call(base, base_args, NULL, out_error);
        TINYPY_DECREF(base_args);
        if (base_state == NULL) {
            goto cleanup;
        }
    }
    class_value = tinypy_object_get_attr_value(self, vm->internal_special_class_key, out_error);
    if (class_value == NULL) {
        goto cleanup;
    }
    tinypy_value_t *constructor_items[] = {class_value, base, base_state};
    constructor_args = tinypy_tuple_from_items(vm, constructor_items, 3U);
    tinypy_value_t *getstate;
    int32_t has_getstate = tinypy_internal_object_get_optional_attr_key(self, vm->internal_special_getstate_key, &getstate, out_error);

    if (has_getstate < 0) {
        goto cleanup;
    }
    if (has_getstate != 0) {
        instance_state = __tinypy_constructor_call_with_items(vm, getstate, NULL, 0U, out_error);
        TINYPY_DECREF(getstate);
        if (instance_state == NULL) {
            goto cleanup;
        }
    }
    else {
        tinypy_value_t *slots;
        int32_t has_slots = tinypy_internal_object_get_optional_attr_key(self, vm->internal_special_slots_key, &slots, out_error);

        if (has_slots < 0) {
            goto cleanup;
        }
        int32_t nonempty_slots = slots != NULL ? tinypy_truth(slots, out_error) : INT32_C(0);

        if (slots != NULL) {
            TINYPY_DECREF(slots);
        }
        if (nonempty_slots < 0) {
            goto cleanup;
        }
        if (nonempty_slots != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "a class that defines __slots__ without defining __getstate__ cannot be pickled", out_error);
            goto cleanup;
        }
        int32_t has_dict = tinypy_internal_object_get_optional_attr_key(self, vm->internal_special_dict_key, &instance_state, out_error);

        if (has_dict < 0) {
            goto cleanup;
        }
        if (instance_state == NULL) {
            instance_state = TINYPY_RET_NONE(vm);
        }
    }
    reconstructor = __tinypy_constructor_copy_reg_function(vm, vm->internal_reconstructor_key, out_error);
    if (reconstructor == NULL) {
        goto cleanup;
    }
    int32_t has_state = tinypy_truth(instance_state, out_error);
    if (has_state < 0) {
        goto cleanup;
    }
    tinypy_value_t *result_items[] = {reconstructor, constructor_args, instance_state};
    result = tinypy_tuple_from_items(vm, result_items, has_state != 0 ? 3U : 2U);

cleanup:
    if (reconstructor != NULL) {
        TINYPY_DECREF(reconstructor);
    }
    if (instance_state != NULL) {
        TINYPY_DECREF(instance_state);
    }
    if (constructor_args != NULL) {
        TINYPY_DECREF(constructor_args);
    }
    if (base_state != NULL) {
        TINYPY_DECREF(base_state);
    }
    if (base != NULL) {
        TINYPY_DECREF(base);
    }
    if (class_value != NULL) {
        TINYPY_DECREF(class_value);
    }
    if (iterator != NULL) {
        TINYPY_DECREF(iterator);
    }
    if (mro != NULL) {
        TINYPY_DECREF(mro);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_tuple_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;

    (void)user_data;
    /* tuple_new parses the arguments after the type as tuple() does. */
    tinypy_type_t *requested = __tinypy_constructor_new_receiver(vm, &vm->types[TINYPY_VALUE_TUPLE], args, out_error);
    if (requested == NULL) {
        return NULL;
    }
    tinypy_value_t *constructor_args = __tinypy_constructor_tail_arguments(vm, args);
    result = tinypy_internal_tuple_create(requested, constructor_args, kwargs, out_error);
    TINYPY_DECREF(constructor_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_set_new_common(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_bool_t frozen, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_type_e kind = frozen != 0 ? TINYPY_VALUE_FROZENSET : TINYPY_VALUE_SET;

    if (__tinypy_constructor_function_argument_count(function, args, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *type_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(type_value) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype((tinypy_type_t *)type_value, &vm->types[kind]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, frozen != 0 ? "frozenset.__new__ requires a frozenset subtype" : "set.__new__ requires a set subtype", out_error);
        return NULL;
    }
    tinypy_value_t *constructor_args;
    if (frozen == 0 || TINYPY_TUPLE_SIZE(args) == 1U) {
        constructor_args = TINYPY_RET_EMPTY_TUPLE(vm);
    }
    else {
        constructor_args = __tinypy_constructor_tail_arguments(vm, args);
    }
    tinypy_value_t *result = frozen != 0
                                 ? tinypy_internal_frozenset_create((tinypy_type_t *)type_value, constructor_args, kwargs, out_error)
                                 : tinypy_internal_set_create((tinypy_type_t *)type_value, constructor_args, kwargs, out_error);
    TINYPY_DECREF(constructor_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_set_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    tinypy_value_t *return_value_1 = __tinypy_constructor_set_new_common(function, args, kwargs, TINYPY_FALSE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_frozenset_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    tinypy_value_t *return_value_1 = __tinypy_constructor_set_new_common(function, args, kwargs, TINYPY_TRUE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_call_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_function_argument_count(function, args, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__call__ requires a type", out_error);
        return NULL;
    }
    tinypy_value_t *call_args = __tinypy_constructor_tail_arguments(vm, args);
    tinypy_value_t *result = tinypy_internal_type_call(class_value, call_args, kwargs, out_error);

    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_instancecheck_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__instancecheck__ requires a type", out_error);
        return NULL;
    }
    tinypy_value_t *instance = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *result = tinypy_bool_from_i32(vm, tinypy_type_is_subtype(instance->type, (tinypy_type_t *)class_value));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_subclasscheck_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *subclass_value = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__subclasscheck__ requires two types", out_error);
        return NULL;
    }
    /* A classic class only ever derives from classic classes, so it is never
       a subclass of a new-style type. */
    if (TINYPY_VALUE_KIND(subclass_value) == TINYPY_VALUE_CLASS) {
        tinypy_value_t *classic_result = TINYPY_RET_FALSE(vm);
        return classic_result;
    }
    if (TINYPY_VALUE_KIND(subclass_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__subclasscheck__ requires two types", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_bool_from_i32(vm, tinypy_type_is_subtype((tinypy_type_t *)subclass_value, (tinypy_type_t *)class_value));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_compare_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(left) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type comparison requires a type", out_error);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(right) != TINYPY_VALUE_TYPE) {
        tinypy_value_t *result = TINYPY_RET_NOT_IMPLEMENTED(vm);

        return result;
    }
    tinypy_value_t *result = tinypy_internal_compare_builtin_value(left, right, (tinypy_compare_operation_e)(intptr_t)user_data, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_mro_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "mro() requires a type", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_type_compute_mro((tinypy_type_t *)class_value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_subclasses_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__subclasses__() requires a type", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_type_subclasses((tinypy_type_t *)class_value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_immutable_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_type_e kind = (tinypy_value_type_e)(intptr_t)user_data;

    tinypy_type_t *type = __tinypy_constructor_new_receiver(vm, &vm->types[kind], args, out_error);
    if (type == NULL) {
        return NULL;
    }
    if (type->layout_kind != kind) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "immutable __new__ received an incompatible type", out_error);
        return NULL;
    }
    tinypy_value_t *constructor_args = __tinypy_constructor_tail_arguments(vm, args);
    tinypy_value_t *result = vm->types[kind].create(type, constructor_args, kwargs, out_error);
    TINYPY_DECREF(constructor_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_add_immutable_new(tinypy_vm_t *vm, tinypy_value_type_e kind) {
    tinypy_type_t *type = &vm->types[kind];
    tinypy_internal_type_add_static_method(type, vm->internal_special_new_key, __tinypy_constructor_immutable_new_method, (void *)(intptr_t)kind, NULL);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_builtin_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_type_t *base_type = (tinypy_type_t *)user_data;

    tinypy_type_t *type = __tinypy_constructor_new_receiver(vm, base_type, args, out_error);
    if (type == NULL) {
        return NULL;
    }
    tinypy_value_type_e kind = base_type->layout_kind;
    if (kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_DICT || kind == TINYPY_VALUE_BYTEARRAY) {
        tinypy_value_t *result = tinypy_internal_object_allocate_checked(vm, type, type->basic_size, out_error);

        if (result == NULL) {
            return NULL;
        }
        if (kind == TINYPY_VALUE_DICT) {
            tinypy_internal_dict_initialize_empty(result);
        }
        return result;
    }
    if (kind == TINYPY_VALUE_PROPERTY || kind == TINYPY_VALUE_CLASS_METHOD || kind == TINYPY_VALUE_STATIC_METHOD || kind == TINYPY_VALUE_MODULE || kind == TINYPY_VALUE_SUPER) {
        tinypy_value_t *result = tinypy_internal_object_allocate_checked(vm, type, type->basic_size, out_error);

        return result;
    }
    tinypy_value_t *constructor_args = __tinypy_constructor_tail_arguments(vm, args);
    tinypy_value_t *result = base_type->create(type, constructor_args, kwargs, out_error);

    TINYPY_DECREF(constructor_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_constructor_add_builtin_new(tinypy_type_t *type) {
    tinypy_internal_type_add_static_method(type, type->vm->internal_special_new_key, __tinypy_constructor_builtin_new_method, type, NULL);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_builtin_reduce_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_type_t *declaring_type = (tinypy_type_t *)user_data;
    tinypy_value_t *constructor_args = NULL;
    tinypy_value_t *state = NULL;
    tinypy_value_t *owned[4] = {NULL, NULL, NULL, NULL};
    size_t owned_count = 0U;
    size_t result_count = 2U;
    /* range_reduce is METH_VARARGS and ignores its arguments; the others are
       METH_NOARGS. */
    size_t maximum = declaring_type->layout_kind == TINYPY_VALUE_XRANGE ? SIZE_MAX : 0U;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, maximum, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_type_is_subtype(self->type, declaring_type) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__reduce__ received an incompatible object", out_error);
        return NULL;
    }
    switch (declaring_type->layout_kind) {
    case TINYPY_VALUE_SLICE: {
        tinypy_slice_object_t *slice = TINYPY_SLICE_OBJECT(self);
        tinypy_value_t *items[] = {slice->start, slice->stop, slice->step};

        constructor_args = tinypy_tuple_from_items(vm, items, 3U);
        break;
    }
    case TINYPY_VALUE_XRANGE: {
        tinypy_xrange_object_t *range = TINYPY_XRANGE_OBJECT(self);

        owned[0] = tinypy_integer_from_i64(vm, range->start);
        owned[1] = tinypy_integer_from_i64(vm, tinypy_internal_xrange_stop_value(range));
        owned[2] = tinypy_integer_from_i64(vm, range->step);
        owned_count = 3U;
        constructor_args = tinypy_tuple_from_items(vm, owned, owned_count);
        break;
    }
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET: {
        /* set_reduce takes PySequence_List(self), which iterates through an
           overridden __iter__. */
        tinypy_value_t *list = tinypy_internal_list_from_items_checked(vm, NULL, 0U, out_error);

        if (list == NULL) {
            return NULL;
        }
        if (tinypy_internal_list_extend_iterable(list, self, "error return without exception set", out_error) == TINYPY_FALSE) {
            TINYPY_DECREF(list);
            return NULL;
        }
        constructor_args = tinypy_tuple_from_items(vm, &list, 1U);
        TINYPY_DECREF(list);
        state = __tinypy_constructor_reduce_instance_dict(self);
        result_count = 3U;
        break;
    }
    case TINYPY_VALUE_BYTEARRAY: {
        tinypy_value_t *bytes = tinypy_string_from_bytes(vm, TINYPY_BYTEARRAY_OBJECT(self)->bytes, TINYPY_SIZED_SIZE(self));
        tinypy_value_t *decode = tinypy_object_get_attr_value(bytes, vm->internal_decode_key, out_error);
        tinypy_value_t *encoding = TINYPY_RET(vm->internal_codec_latin1_name);
        tinypy_value_t *unicode = NULL;

        if (decode != NULL) {
            tinypy_value_t *decode_args = tinypy_tuple_from_items(vm, &encoding, 1U);

            unicode = tinypy_call(decode, decode_args, NULL, out_error);
            TINYPY_DECREF(decode_args);
            TINYPY_DECREF(decode);
        }
        TINYPY_DECREF(bytes);
        if (unicode == NULL) {
            TINYPY_DECREF(encoding);
            return NULL;
        }
        tinypy_value_t *items[] = {unicode, encoding};
        constructor_args = tinypy_tuple_from_items(vm, items, 2U);
        TINYPY_DECREF(unicode);
        TINYPY_DECREF(encoding);
        state = __tinypy_constructor_reduce_instance_dict(self);
        result_count = 3U;
        break;
    }
    default:
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin type has no native reduction", out_error);
        return NULL;
    }
    tinypy_value_t *result_items[] = {&self->type->base.base, constructor_args, state};
    tinypy_value_t *result = tinypy_tuple_from_items(vm, result_items, result_count);

    if (state != NULL) {
        TINYPY_DECREF(state);
    }
    TINYPY_DECREF(constructor_args);
    while (owned_count != 0U) {
        owned_count -= 1U;
        TINYPY_DECREF(owned[owned_count]);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_add_builtin_reduce(tinypy_type_t *type) {
    tinypy_internal_type_add_method(type, type->vm->internal_special_reduce_key, __tinypy_constructor_builtin_reduce_method, type, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_copy_reg_callable(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_bool_t result = value->type->call != NULL || tinypy_internal_object_has_special_key(value, vm->internal_special_call_key) != 0 ? TINYPY_TRUE : TINYPY_FALSE;

    return result;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_copy_reg_slot_name(tinypy_vm_t *vm, tinypy_value_t *owner, tinypy_value_t *slot) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(slot);
    size_t size = TINYPY_TEXT_BYTE_SIZE(slot);
    const char *class_name = ((const tinypy_type_t *)owner)->name;
    size_t class_name_size = ((const tinypy_type_t *)owner)->name_size;
    size_t class_start = 0U;
    uint8_t *mangled;
    size_t mangled_size;
    tinypy_value_t *result;

    if (TINYPY_VALUE_KIND(owner) == TINYPY_VALUE_CLASS) {
        tinypy_value_t *name = TINYPY_CLASS_OBJECT(owner)->name;

        class_name = (const char *)TINYPY_TEXT_BYTES(name);
        class_name_size = TINYPY_TEXT_BYTE_SIZE(name);
    }
    if (size < 3U || bytes[0] != (uint8_t)'_' || bytes[1] != (uint8_t)'_' || (bytes[size - 2U] == (uint8_t)'_' && bytes[size - 1U] == (uint8_t)'_')) {
        return TINYPY_RET(slot);
    }
    while (class_start < class_name_size && class_name[class_start] == '_') {
        class_start += 1U;
    }
    if (class_start == class_name_size) {
        return TINYPY_RET(slot);
    }
    mangled_size = 1U + class_name_size - class_start + size;
    mangled = (uint8_t *)tinypy_internal_vm_allocate(vm, mangled_size);
    mangled[0] = (uint8_t)'_';
    (void)memcpy(mangled + 1U, class_name + class_start, class_name_size - class_start);
    (void)memcpy(mangled + 1U + class_name_size - class_start, bytes, size);
    result = TINYPY_VALUE_KIND(slot) == TINYPY_VALUE_UNICODE
                 ? tinypy_unicode_from_utf8(vm, (const char *)mangled, mangled_size)
                 : tinypy_string_from_bytes(vm, mangled, mangled_size);
    tinypy_internal_vm_deallocate(vm, mangled, mangled_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_copy_reg_constructor(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    if (__tinypy_copy_reg_callable(TINYPY_TUPLE_GET(args, 0U)) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "constructors must be callable", out_error);
        return NULL;
    }
    result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_copy_reg_pickle(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *module = (tinypy_value_t *)user_data;
    tinypy_value_t *result;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *object_type = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *reducer = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(object_type) == TINYPY_VALUE_CLASS) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "copy_reg is not intended for use with classes", out_error);
        return NULL;
    }
    if (__tinypy_copy_reg_callable(reducer) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reduction functions must be callable", out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U && TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 2U)) != TINYPY_VALUE_NONE && __tinypy_copy_reg_callable(TINYPY_TUPLE_GET(args, 2U)) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "constructors must be callable", out_error);
        return NULL;
    }
    tinypy_value_t *dispatch_table = tinypy_module_get_value_key(module, vm->internal_dispatch_table_key);
    if (tinypy_internal_dict_set_checked(vm, dispatch_table, object_type, reducer, out_error) == 0) {
        return NULL;
    }
    result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_copy_reg_pickle_complex(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_COMPLEX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "pickle_complex requires a complex number", out_error);
        return NULL;
    }
    double real;
    double imaginary;
    tinypy_complex_as_doubles(value, &real, &imaginary);
    tinypy_value_t *real_value = tinypy_float_from_double(vm, real);
    tinypy_value_t *imaginary_value = tinypy_float_from_double(vm, imaginary);
    tinypy_value_t *complex_args_items[2] = {real_value, imaginary_value};
    tinypy_value_t *complex_args = tinypy_tuple_from_items(vm, complex_args_items, 2U);
    tinypy_value_t *result_items[2] = {&vm->types[TINYPY_VALUE_COMPLEX].base.base, complex_args};
    tinypy_value_t *result = tinypy_tuple_from_items(vm, result_items, 2U);

    TINYPY_DECREF(complex_args);
    TINYPY_DECREF(imaginary_value);
    TINYPY_DECREF(real_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_copy_reg_extension(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *module = (tinypy_value_t *)user_data;
    tinypy_value_t *function_name = tinypy_native_function_name(function);
    tinypy_value_t *result;
    tinypy_bool_t remove = TINYPY_TEXT_BYTE_SIZE(function_name) == 16U ? TINYPY_TRUE : TINYPY_FALSE;
    int64_t code;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 3U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *module_name = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *object_name = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *code_value = TINYPY_TUPLE_GET(args, 2U);
    if ((TINYPY_VALUE_KIND(module_name) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(module_name) != TINYPY_VALUE_UNICODE) ||
        (TINYPY_VALUE_KIND(object_name) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(object_name) != TINYPY_VALUE_UNICODE) ||
        tinypy_internal_index_as_i64(code_value, &code, TINYPY_FALSE, out_error) == 0) {
        if (out_error == NULL || *out_error == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "extension arguments are invalid", out_error);
        }
        return NULL;
    }
    if (code <= 0 || code > INT64_C(0x7fffffff)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "code out of range", out_error);
        return NULL;
    }
    tinypy_value_t *normalized_code = tinypy_integer_from_i64(vm, code);
    tinypy_value_t *key_items[2] = {module_name, object_name};
    tinypy_value_t *key = tinypy_tuple_from_items(vm, key_items, 2U);
    tinypy_value_t *registry = tinypy_module_get_value_key(module, vm->internal_extension_registry_key);
    tinypy_value_t *inverted = tinypy_module_get_value_key(module, vm->internal_inverted_registry_key);
    tinypy_value_t *registered_code = tinypy_dict_get_optional(registry, key);
    tinypy_value_t *registered_key = tinypy_dict_get_optional(inverted, normalized_code);

    if (remove != 0) {
        if (registered_code == NULL || registered_key == NULL || TINYPY_VALUE_KIND(registered_code) != TINYPY_VALUE_INTEGER || TINYPY_INTEGER_VALUE(registered_code) != code || tinypy_internal_equal_value(registered_key, key, 1) == 0) {
            TINYPY_DECREF(key);
            TINYPY_DECREF(normalized_code);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "extension is not registered with that code", out_error);
            return NULL;
        }
        tinypy_dict_delete(registry, key);
        tinypy_dict_delete(inverted, normalized_code);
        tinypy_dict_delete(tinypy_module_get_value_key(module, vm->internal_extension_cache_key), normalized_code);
    }
    else if (registered_code != NULL || registered_key != NULL) {
        tinypy_bool_t same = registered_code != NULL && TINYPY_VALUE_KIND(registered_code) == TINYPY_VALUE_INTEGER && TINYPY_INTEGER_VALUE(registered_code) == code && registered_key != NULL && tinypy_internal_equal_value(registered_key, key, 1) != 0;

        if (same == 0) {
            TINYPY_DECREF(key);
            TINYPY_DECREF(normalized_code);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "extension code is already in use", out_error);
            return NULL;
        }
    }
    else if (tinypy_internal_dict_set_checked(vm, registry, key, normalized_code, out_error) == 0 || tinypy_internal_dict_set_checked(vm, inverted, normalized_code, key, out_error) == 0) {
        TINYPY_DECREF(key);
        TINYPY_DECREF(normalized_code);
        return NULL;
    }
    TINYPY_DECREF(key);
    TINYPY_DECREF(normalized_code);
    result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_copy_reg_clear_extension_cache(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *module = (tinypy_value_t *)user_data;
    tinypy_value_t *result;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 0U, 0U, out_error) == 0) {
        return NULL;
    }
    tinypy_dict_clear(tinypy_module_get_value_key(module, vm->internal_extension_cache_key));
    result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_copy_reg_slotnames(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *cache_key = NULL;
    tinypy_value_t *slots_key = NULL;
    tinypy_value_t *mro = NULL;
    tinypy_value_t *result = NULL;
    tinypy_value_t *iterator = NULL;
    size_t mro_index;

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_function_argument_count(function, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *type_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(type_value);
    if (kind != TINYPY_VALUE_TYPE && kind != TINYPY_VALUE_CLASS) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "argument must be a class", out_error);
        return NULL;
    }
    tinypy_value_t *namespace_dict = kind == TINYPY_VALUE_TYPE ? ((tinypy_type_t *)type_value)->dict : TINYPY_CLASS_OBJECT(type_value)->dict;
    cache_key = TINYPY_RET(vm->internal_special_slotnames_key);
    tinypy_value_t *cached = tinypy_internal_dict_get_optional(vm, namespace_dict, cache_key);
    if (cached != NULL && TINYPY_VALUE_KIND(cached) != TINYPY_VALUE_NONE) {
        TINYPY_INCREF(cached);
        TINYPY_DECREF(cache_key);
        return cached;
    }
    result = tinypy_list_from_items(vm, NULL, 0U);
    slots_key = TINYPY_RET(vm->internal_special_slots_key);
    tinypy_value_t *declared_slots = kind == TINYPY_VALUE_CLASS
                                        ? tinypy_internal_class_lookup_key(vm, type_value, slots_key)
                                        : tinypy_internal_type_lookup_key(vm, (tinypy_type_t *)type_value, slots_key);
    if (declared_slots != NULL) {
        /* The owning public MRO snapshot keeps entries alive across iterator
           callbacks that can change the class hierarchy. */
        mro = tinypy_object_get_attr_value(type_value, vm->internal_special_mro_key, out_error);
        if (mro == NULL) {
            goto error;
        }
    }
    for (mro_index = 0U; mro != NULL && mro_index != TINYPY_TUPLE_SIZE(mro); ++mro_index) {
        tinypy_value_t *candidate = TINYPY_TUPLE_GET(mro, mro_index);
        tinypy_value_t *slots = tinypy_internal_dict_get_optional(vm, tinypy_internal_type_mro_entry_dict(candidate), slots_key);

        if (slots == NULL) {
            continue;
        }
        if (TINYPY_VALUE_KIND(slots) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(slots) == TINYPY_VALUE_UNICODE) {
            tinypy_value_t *single = tinypy_tuple_from_items(vm, &slots, 1U);

            iterator = tinypy_iter(single, out_error);
            TINYPY_DECREF(single);
        }
        else {
            iterator = tinypy_iter(slots, out_error);
        }
        if (iterator == NULL) {
            goto error;
        }
        for (;;) {
            tinypy_value_t *slot = tinypy_next(iterator, out_error);
            tinypy_value_t *slot_name;

            if (slot == NULL) {
                if (out_error != NULL && *out_error != NULL) {
                    goto error;
                }
                break;
            }
            if (TINYPY_VALUE_KIND(slot) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(slot) != TINYPY_VALUE_UNICODE) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("'"),
                    TINYPY_MESSAGE_PART_TYPE_NAME(slot),
                    TINYPY_MESSAGE_PART_LITERAL("' object has no attribute 'startswith'"),
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_ATTRIBUTE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
                TINYPY_DECREF(slot);
                goto error;
            }
            if (TINYPY_NAME_EQ(slot, vm->internal_special_dict_key) != 0 || TINYPY_NAME_EQ(slot, vm->internal_special_weakref_key) != 0) {
                TINYPY_DECREF(slot);
                continue;
            }
            slot_name = __tinypy_copy_reg_slot_name(vm, candidate, slot);
            TINYPY_DECREF(slot);
            tinypy_bool_t appended = tinypy_internal_list_append_checked(result, slot_name, out_error);
            TINYPY_DECREF(slot_name);
            if (appended == 0) {
                goto error;
            }
        }
        TINYPY_DECREF(iterator);
        iterator = NULL;
    }
    if (mro != NULL) {
        TINYPY_DECREF(mro);
    }
    TINYPY_DECREF(slots_key);
    if (kind == TINYPY_VALUE_CLASS) {
        tinypy_dict_set(namespace_dict, cache_key, result);
    }
    else if ((((tinypy_type_t *)type_value)->flags & TINYPY_TYPE_FLAG_HEAP) != 0U) {
        tinypy_type_set_attr_key((tinypy_type_t *)type_value, ((tinypy_type_t *)type_value)->vm->internal_special_slotnames_key, result);
    }
    TINYPY_DECREF(cache_key);
    return result;
error:
    if (iterator != NULL) {
        TINYPY_DECREF(iterator);
    }
    if (mro != NULL) {
        TINYPY_DECREF(mro);
    }
    TINYPY_DECREF(slots_key);
    TINYPY_DECREF(cache_key);
    TINYPY_DECREF(result);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_copy_reg_module(tinypy_vm_t *vm) {
    tinypy_value_t *module = tinypy_module_new_key(vm->internal_copy_reg_module_name);
    tinypy_value_t *name = TINYPY_RET(vm->internal_copy_reg_module_name);

    tinypy_module_add_value_key(module, vm->internal_special_name_key, name);
    TINYPY_DECREF(name);
    tinypy_value_t *dispatch_table = tinypy_dict_new(vm);
    tinypy_value_t *extension_registry = tinypy_dict_new(vm);
    tinypy_value_t *inverted_registry = tinypy_dict_new(vm);
    tinypy_value_t *extension_cache = tinypy_dict_new(vm);
    tinypy_value_t *heap_type = tinypy_integer_from_i64(vm, INT64_C(1) << 9);
    tinypy_value_t *const public_name_values[] = {vm->internal_pickle_key, vm->internal_constructor_key, vm->internal_add_extension_key, vm->internal_remove_extension_key, vm->internal_clear_extension_cache_key};

    tinypy_module_add_value_key(module, vm->internal_dispatch_table_key, dispatch_table);
    tinypy_module_add_value_key(module, vm->internal_extension_registry_key, extension_registry);
    tinypy_module_add_value_key(module, vm->internal_inverted_registry_key, inverted_registry);
    tinypy_module_add_value_key(module, vm->internal_extension_cache_key, extension_cache);
    tinypy_module_add_value_key(module, vm->internal_heaptype_key, heap_type);
    tinypy_module_add_value_key(module, vm->internal_class_type_key, &vm->types[TINYPY_VALUE_CLASS].base.base);
    tinypy_value_t *public_name_list = tinypy_list_from_items(vm, public_name_values, 5U);
    tinypy_module_add_value_key(module, vm->internal_special_all_key, public_name_list);
    TINYPY_DECREF(public_name_list);
    TINYPY_DECREF(heap_type);
    TINYPY_DECREF(extension_cache);
    TINYPY_DECREF(inverted_registry);
    TINYPY_DECREF(extension_registry);
    TINYPY_DECREF(dispatch_table);
    tinypy_internal_module_add_function(module, vm->internal_special_newobj_key, __tinypy_copy_reg_newobj, module, NULL);
    tinypy_internal_module_add_function(module, vm->internal_reconstructor_key, __tinypy_copy_reg_reconstructor, module, NULL);
    tinypy_internal_module_add_function(module, vm->internal_reduce_ex_key, __tinypy_copy_reg_reduce_ex, module, NULL);
    tinypy_internal_module_add_function(module, vm->internal_constructor_key, __tinypy_copy_reg_constructor, module, NULL);
    tinypy_internal_module_add_function(module, vm->internal_pickle_key, __tinypy_copy_reg_pickle, module, NULL);
    tinypy_internal_module_add_function(module, vm->internal_pickle_complex_key, __tinypy_copy_reg_pickle_complex, module, NULL);
    tinypy_internal_module_add_function(module, vm->internal_add_extension_key, __tinypy_copy_reg_extension, module, NULL);
    tinypy_internal_module_add_function(module, vm->internal_remove_extension_key, __tinypy_copy_reg_extension, module, NULL);
    tinypy_internal_module_add_function(module, vm->internal_clear_extension_cache_key, __tinypy_copy_reg_clear_extension_cache, module, NULL);
    tinypy_internal_module_add_function(module, vm->internal_slotnames_key, __tinypy_copy_reg_slotnames, module, NULL);
    tinypy_value_t *complex_reducer = tinypy_module_get_value_key(module, vm->internal_pickle_complex_key);
    tinypy_internal_dict_set_checked(vm, dispatch_table, &vm->types[TINYPY_VALUE_COMPLEX].base.base, complex_reducer, NULL);
    tinypy_internal_register_module(vm, vm->internal_copy_reg_module_name, module);
    TINYPY_DECREF(module);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_constructor_types(tinypy_vm_t *vm) {
    vm->types[TINYPY_VALUE_INVALID].create = __tinypy_constructor_basestring_create;
    tinypy_internal_type_add_static_method((&vm->types[TINYPY_VALUE_TYPE]), vm->internal_special_new_key, __tinypy_constructor_type_new_method, NULL, NULL);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_TYPE]), vm->internal_special_init_key, __tinypy_constructor_type_init_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_TYPE]), vm->internal_special_setattr_key, __tinypy_constructor_object_setattr_method, (void *)(intptr_t)TINYPY_VALUE_TYPE, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_TYPE]), vm->internal_special_delattr_key, __tinypy_constructor_object_delattr_method, (void *)(intptr_t)TINYPY_VALUE_TYPE, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_TYPE]), vm->internal_special_call_key, __tinypy_constructor_type_call_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_TYPE]), vm->internal_special_instancecheck_key, __tinypy_constructor_type_instancecheck_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_TYPE]), vm->internal_special_subclasscheck_key, __tinypy_constructor_type_subclasscheck_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TYPE], vm->internal_special_lt_key, __tinypy_constructor_type_compare_method, (void *)(intptr_t)TINYPY_COMPARE_LESS, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TYPE], vm->internal_special_le_key, __tinypy_constructor_type_compare_method, (void *)(intptr_t)TINYPY_COMPARE_LESS_EQUAL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TYPE], vm->internal_special_eq_key, __tinypy_constructor_type_compare_method, (void *)(intptr_t)TINYPY_COMPARE_EQUAL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TYPE], vm->internal_special_ne_key, __tinypy_constructor_type_compare_method, (void *)(intptr_t)TINYPY_COMPARE_NOT_EQUAL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TYPE], vm->internal_special_gt_key, __tinypy_constructor_type_compare_method, (void *)(intptr_t)TINYPY_COMPARE_GREATER, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TYPE], vm->internal_special_ge_key, __tinypy_constructor_type_compare_method, (void *)(intptr_t)TINYPY_COMPARE_GREATER_EQUAL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_TYPE]), vm->internal_mro_key, __tinypy_constructor_type_mro_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_TYPE]), vm->internal_special_subclasses_key, __tinypy_constructor_type_subclasses_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_static_method((&vm->types[TINYPY_VALUE_INSTANCE]), vm->internal_special_new_key, __tinypy_constructor_object_new_method, NULL, NULL);
    tinypy_internal_type_add_static_method((&vm->types[TINYPY_VALUE_INVALID]), vm->internal_special_new_key, __tinypy_constructor_basestring_new_method, NULL, NULL);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_INSTANCE]), vm->internal_special_init_key, __tinypy_constructor_object_init_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_object_attribute_methods(&vm->types[TINYPY_VALUE_INSTANCE]);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_INSTANCE]), vm->internal_special_hash_key, __tinypy_constructor_object_hash_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_INSTANCE]), vm->internal_special_format_key, __tinypy_constructor_object_format_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_class_method((&vm->types[TINYPY_VALUE_INSTANCE]), vm->internal_special_subclasshook_key, __tinypy_constructor_object_subclasshook_method, NULL, NULL);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_INSTANCE]), vm->internal_special_reduce_key, __tinypy_constructor_object_reduce_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_INSTANCE]), vm->internal_special_reduce_ex_key, __tinypy_constructor_object_reduce_ex_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_LIST]), vm->internal_special_init_key, __tinypy_constructor_object_init_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_DICT]), vm->internal_special_init_key, __tinypy_constructor_object_init_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_special_init_key, __tinypy_constructor_object_init_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_BYTEARRAY]), vm->internal_special_init_key, __tinypy_constructor_object_init_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_static_method((&vm->types[TINYPY_VALUE_TUPLE]), vm->internal_special_new_key, __tinypy_constructor_tuple_new_method, NULL, NULL);
    tinypy_internal_type_add_static_method((&vm->types[TINYPY_VALUE_SET]), vm->internal_special_new_key, __tinypy_constructor_set_new_method, NULL, NULL);
    tinypy_internal_type_add_static_method((&vm->types[TINYPY_VALUE_FROZENSET]), vm->internal_special_new_key, __tinypy_constructor_frozenset_new_method, NULL, NULL);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_LIST]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_DICT]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_BYTEARRAY]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_PROPERTY]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_CLASS_METHOD]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_STATIC_METHOD]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_SLICE]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_XRANGE]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_BUFFER]);
    __tinypy_constructor_add_builtin_reduce(&vm->types[TINYPY_VALUE_SLICE]);
    __tinypy_constructor_add_builtin_reduce(&vm->types[TINYPY_VALUE_XRANGE]);
    __tinypy_constructor_add_builtin_reduce(&vm->types[TINYPY_VALUE_SET]);
    __tinypy_constructor_add_builtin_reduce(&vm->types[TINYPY_VALUE_FROZENSET]);
    __tinypy_constructor_add_builtin_reduce(&vm->types[TINYPY_VALUE_BYTEARRAY]);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_BOOL);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_INTEGER);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_LONG);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_FLOAT);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_COMPLEX);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_STRING);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_UNICODE);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_ENUMERATE);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_REVERSED);
}
