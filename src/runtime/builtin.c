#include "tinypy/native.h"
#include "tinypy/compiler.h"
#include "tinypy/eval.h"

#include "internal.h"
#include "api_internal.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_no_keywords(tinypy_value_t *function, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_TEXT(TINYPY_NATIVE_FUNCTION_OBJECT(function)->name),
            TINYPY_MESSAGE_PART_LITERAL("() takes no keyword arguments")
        };

        tinypy_internal_make_vm_error_parts(TINYPY_VALUE_VM(function), TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_argument_count(tinypy_value_t *function, tinypy_value_t *args, size_t minimum, size_t maximum, tinypy_arity_style_e style, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count < minimum || count > maximum) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
        tinypy_value_t *name = TINYPY_NATIVE_FUNCTION_OBJECT(function)->name;

        tinypy_internal_make_arity_error(vm, (const char *)TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name), count, minimum, maximum, style, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_argument_count_message(tinypy_vm_t *vm, tinypy_value_t *args, size_t minimum, size_t maximum, const char *message, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, message, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_named_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_value_t *const *names, size_t parameter_count, size_t required_count, tinypy_value_t **out_values, tinypy_error_t **out_error) {
    size_t positional_count = TINYPY_TUPLE_SIZE(args);
    size_t index;

    if (positional_count > parameter_count) {
        char expected_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
        char given_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
        size_t expected_size = tinypy_internal_format_size(expected_buffer, parameter_count);
        size_t given_size = tinypy_internal_format_size(given_buffer, positional_count);
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("function takes at most "),
            {expected_buffer, expected_size},
            TINYPY_MESSAGE_PART_LITERAL(" arguments ("),
            {given_buffer, given_size},
            TINYPY_MESSAGE_PART_LITERAL(" given)"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    for (index = 0U; index < parameter_count; ++index) {
        out_values[index] = index < positional_count ? TINYPY_TUPLE_GET(args, index) : NULL;
    }
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(kwargs);
        tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(kwargs);

        for (; iterator != iterator_end; ++iterator) {
            size_t parameter = parameter_count;

            if (!TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
                continue;
            }
            if (TINYPY_VALUE_KIND(iterator->key) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(iterator->key) == TINYPY_VALUE_UNICODE) {
                for (index = 0U; index < parameter_count; ++index) {
                    if (TINYPY_NAME_EQ(iterator->key, names[index]) != 0) {
                        parameter = index;
                        break;
                    }
                }
            }
            if (parameter == parameter_count) {
                if (TINYPY_VALUE_KIND(iterator->key) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(iterator->key) == TINYPY_VALUE_UNICODE) {
                    tinypy_message_part_t parts[] = {
                        TINYPY_MESSAGE_PART_LITERAL("'"),
                        TINYPY_MESSAGE_PART_TEXT(iterator->key),
                        TINYPY_MESSAGE_PART_LITERAL("' is an invalid keyword argument for this function"),
                    };

                    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
                }
                else {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "keywords must be strings", out_error);
                }
                return TINYPY_FALSE;
            }
            if (out_values[parameter] != NULL) {
                char position_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
                size_t position_size = tinypy_internal_format_size(position_buffer, parameter + 1U);
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("Argument given by name ('"),
                    TINYPY_MESSAGE_PART_TEXT(names[parameter]),
                    TINYPY_MESSAGE_PART_LITERAL("') and position ("),
                    {position_buffer, position_size},
                    TINYPY_MESSAGE_PART_LITERAL(")"),
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
                return TINYPY_FALSE;
            }
            out_values[parameter] = iterator->value;
        }
    }
    for (index = 0U; index < required_count; ++index) {
        if (out_values[index] == NULL) {
            char position_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
            size_t position_size = tinypy_internal_format_size(position_buffer, index + 1U);
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("Required argument '"),
                TINYPY_MESSAGE_PART_TEXT(names[index]),
                TINYPY_MESSAGE_PART_LITERAL("' (pos "),
                {position_buffer, position_size},
                TINYPY_MESSAGE_PART_LITERAL(") not found"),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_text_view(tinypy_vm_t *vm, tinypy_value_t *value, const char **out_bytes, size_t *out_size, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        *out_bytes = (const char *)tinypy_string_view(value, out_size);
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        size_t code_points;

        *out_bytes = tinypy_unicode_utf8_view(value, out_size, &code_points);
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "attribute name must be a string", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_call_items(tinypy_vm_t *vm, tinypy_value_t *callable, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error) {
    if (callable->type == &vm->types[TINYPY_VALUE_FUNCTION]) {
        tinypy_value_t *return_value_1 = tinypy_internal_eval_function_items(callable, items, item_count, NULL, out_error);
        return return_value_1;
    }
    if (callable->type == &vm->types[TINYPY_VALUE_METHOD]) {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(callable);

        if (method->self != NULL && method->function->type == &vm->types[TINYPY_VALUE_FUNCTION] && item_count < SIZE_MAX / sizeof(tinypy_value_t *)) {
            tinypy_value_t *local_items[4];
            tinypy_value_t **bound_items = local_items;
            size_t bound_count = item_count + 1U;
            size_t index;

            if (bound_count > sizeof(local_items) / sizeof(local_items[0])) {
                bound_items = (tinypy_value_t **)tinypy_internal_vm_allocate_checked(vm, bound_count * sizeof(*bound_items), out_error);
                if (bound_items == NULL) {
                    return NULL;
                }
            }
            bound_items[0] = method->self;
            for (index = 0U; index < item_count; ++index) {
                bound_items[index + 1U] = items[index];
            }
            tinypy_value_t *result = tinypy_internal_eval_function_items(method->function, bound_items, bound_count, NULL, out_error);

            if (bound_items != local_items) {
                tinypy_internal_vm_deallocate(vm, bound_items, bound_count * sizeof(*bound_items));
            }
            return result;
        }
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, items, item_count);
    tinypy_value_t *result = tinypy_call(callable, args, NULL, out_error);

    TINYPY_DECREF(args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_apply_sequence(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE || kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_BUFFER || kind == TINYPY_VALUE_BYTEARRAY || kind == TINYPY_VALUE_XRANGE) {
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_DICT || kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t result = tinypy_internal_object_has_special_key(value, vm->internal_special_getitem_key);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_apply(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *call_args = NULL;
    tinypy_value_t *call_kwargs = NULL;
    tinypy_value_t *owned_args = NULL;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 3U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    if (count >= 2U) {
        call_args = TINYPY_TUPLE_GET(args, 1U);
        if (TINYPY_VALUE_KIND(call_args) != TINYPY_VALUE_TUPLE) {
            tinypy_value_t *constructor_args;

            if (__tinypy_builtin_apply_sequence(call_args) == 0) {
                const tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("apply() arg 2 expected sequence, found "),
                    TINYPY_MESSAGE_PART_TYPE_NAME(call_args)
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
                return NULL;
            }
            constructor_args = tinypy_tuple_from_items(vm, &call_args, 1U);
            owned_args = tinypy_internal_tuple_create(&vm->types[TINYPY_VALUE_TUPLE], constructor_args, NULL, out_error);
            TINYPY_DECREF(constructor_args);
            if (owned_args == NULL) {
                return NULL;
            }
            call_args = owned_args;
        }
    }
    else {
        owned_args = TINYPY_RET_EMPTY_TUPLE(vm);
        call_args = owned_args;
    }
    if (count == 3U) {
        call_kwargs = TINYPY_TUPLE_GET(args, 2U);
        if (TINYPY_VALUE_KIND(call_kwargs) != TINYPY_VALUE_DICT) {
            if (owned_args != NULL) {
                TINYPY_DECREF(owned_args);
            }
            const tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("apply() arg 3 expected dictionary, found "),
                TINYPY_MESSAGE_PART_TYPE_NAME(call_kwargs)
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            return NULL;
        }
    }
    tinypy_value_t *result = tinypy_call(TINYPY_TUPLE_GET(args, 0U), call_args, call_kwargs, out_error);

    if (owned_args != NULL) {
        TINYPY_DECREF(owned_args);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_len(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t size;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_internal_object_overrides_dispatch(value, TINYPY_INTERNAL_DISPATCH_BIT(LENGTH)) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special_key(value, vm->internal_special_length_key, out_error);
        tinypy_value_t *empty;
        tinypy_value_t *result;
        int64_t length;

        if (method == NULL) {
            return NULL;
        }
        empty = TINYPY_RET_EMPTY_TUPLE(vm);
        result = tinypy_call(method, empty, NULL, out_error);
        TINYPY_DECREF(empty);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_INTEGER) {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__len__() should return an int", out_error);
            return NULL;
        }
        if (tinypy_internal_number_as_ssize(result, &length, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(result);
        if (length < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__() should return >= 0", out_error);
            return NULL;
        }
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, length);
        return return_value_1;
    }
    switch (TINYPY_VALUE_KIND(value)) {
    case TINYPY_VALUE_STRING:
        (void)tinypy_string_view(value, &size);
        break;
    case TINYPY_VALUE_UNICODE: {
        size_t byte_size;
        (void)tinypy_unicode_utf8_view(value, &byte_size, &size);
        break;
    }
    case TINYPY_VALUE_TUPLE:
        size = TINYPY_TUPLE_SIZE(value);
        break;
    case TINYPY_VALUE_LIST:
        size = TINYPY_LIST_SIZE(value);
        break;
    case TINYPY_VALUE_DICT:
        size = TINYPY_DICT_SIZE(value);
        break;
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
        size = tinypy_set_size(value);
        break;
    case TINYPY_VALUE_XRANGE:
        size = TINYPY_XRANGE_OBJECT(value)->length;
        break;
    default: {
        tinypy_length_slot_t length_slot = value->type->mapping_slots != NULL && value->type->mapping_slots->length != NULL
                                               ? value->type->mapping_slots->length
                                               : (value->type->sequence_slots != NULL ? value->type->sequence_slots->length : NULL);
        if (length_slot != NULL) {
            ptrdiff_t length = length_slot(value, out_error);

            if (length < 0) {
                if (out_error == NULL || *out_error == NULL) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "length slot returned a negative value", out_error);
                }
                return NULL;
            }
            tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)length);
            return return_value_1;
        }
        tinypy_value_t *method = tinypy_internal_object_get_special_key(value, vm->internal_special_length_key, out_error);
        tinypy_value_t *empty;
        tinypy_value_t *result;

        if (method == NULL) {
            if (tinypy_vm_has_error(vm) != 0) {
                return NULL;
            }
            const tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("object of type '"),
                TINYPY_MESSAGE_PART_TYPE_NAME(value),
                TINYPY_MESSAGE_PART_LITERAL("' has no len()")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            return NULL;
        }
        empty = TINYPY_RET_EMPTY_TUPLE(vm);
        result = tinypy_call(method, empty, NULL, out_error);
        TINYPY_DECREF(empty);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_INTEGER) {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__len__() should return an int", out_error);
            return NULL;
        }
        int64_t length;
        if (tinypy_internal_number_as_ssize(result, &length, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(result);
        if (length < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__() should return >= 0", out_error);
            return NULL;
        }
        tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, length);
        return return_value_2;
    }
    }
    tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, (int64_t)size);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_id(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    uintptr_t identity;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    identity = (uintptr_t)TINYPY_TUPLE_GET(args, 0U);
    if ((uint64_t)identity <= (uint64_t)INT64_MAX) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)identity);
        return return_value_1;
    }
    uint16_t digits[5];
    size_t count = 0U;
    uint64_t magnitude = (uint64_t)identity;

    while (magnitude != 0U) {
        digits[count] = (uint16_t)(magnitude & UINT64_C(0x7fff));
        count += 1U;
        magnitude >>= 15U;
    }
    tinypy_value_t *return_value_2 = tinypy_long_from_base15_digits(vm, 1, digits, count);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
/* Classinfo tuples and __instancecheck__/__subclasscheck__ calls share the
   recursion limit; the caller leaves with evaluation_depth -= 1. */
static tinypy_bool_t __tinypy_builtin_instance_check_enter(tinypy_vm_t *vm, tinypy_bool_t subclass, tinypy_error_t **out_error) {
    if (tinypy_internal_recursion_check(vm, TINYPY_NATIVE_STACK_ADDRESS(), subclass != 0 ? "maximum recursion depth exceeded in __subclasscheck__" : "maximum recursion depth exceeded in __instancecheck__", out_error) == 0) {
        return TINYPY_FALSE;
    }
    vm->evaluation_depth += 1U;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* A metaclass other than type may define __instancecheck__ or
   __subclasscheck__; plain type uses the default rules directly. */
static int32_t __tinypy_builtin_metaclass_check(tinypy_value_t *object, tinypy_value_t *classinfo, tinypy_bool_t subclass, tinypy_bool_t *out_result, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(object);
    tinypy_value_t *key = subclass != 0 ? vm->internal_special_subclasscheck_key : vm->internal_special_instancecheck_key;
    tinypy_value_t *value;
    int32_t truth;

    if (classinfo->type == &vm->types[TINYPY_VALUE_TYPE] || TINYPY_VALUE_KIND(classinfo) == TINYPY_VALUE_CLASS || TINYPY_VALUE_KIND(classinfo) == TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_type_lookup_key(vm, classinfo->type, key) == NULL) {
        return 0;
    }
    tinypy_value_t *method = tinypy_internal_object_get_special_key(classinfo, key, out_error);
    if (method == NULL) {
        return -1;
    }
    if (__tinypy_builtin_instance_check_enter(vm, subclass, out_error) == 0) {
        TINYPY_DECREF(method);
        return -1;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &object, 1U);
    value = tinypy_call(method, args, NULL, out_error);
    vm->evaluation_depth -= 1U;
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    if (value == NULL) {
        return -1;
    }
    truth = tinypy_truth(value, out_error);
    TINYPY_DECREF(value);
    if (truth < 0) {
        return -1;
    }
    *out_result = truth != 0 ? 1 : 0;
    return 1;
}
//////////////////////////////////////////////////////////////////////////
/* abstract_get_bases: any object whose __bases__ is a tuple acts as a class. */
static tinypy_value_t *__tinypy_builtin_abstract_bases(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *bases;
    tinypy_error_t *lookup_error = NULL;

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_CLASS) {
        bases = TINYPY_RET(TINYPY_CLASS_OBJECT(value)->bases);
        return bases;
    }
    bases = tinypy_object_get_attr_value(value, vm->internal_special_bases_key, &lookup_error);
    if (bases == NULL) {
        tinypy_bool_t attribute_error = vm->raised_type != NULL && TINYPY_VALUE_KIND(vm->raised_type) == TINYPY_VALUE_TYPE
            ? tinypy_type_is_subtype((tinypy_type_t *)vm->raised_type, vm->exception_types[TINYPY_EXCEPTION_ATTRIBUTE_ERROR])
            : lookup_error != NULL && tinypy_error_kind(lookup_error) == TINYPY_ERROR_ATTRIBUTE;

        if (attribute_error == 0) {
            if (out_error != NULL) {
                *out_error = lookup_error;
            }
            else if (lookup_error != NULL) {
                tinypy_error_release(lookup_error);
            }
            return NULL;
        }
        if (lookup_error != NULL) {
            tinypy_error_release(lookup_error);
        }
        tinypy_vm_clear_error(vm);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(bases) != TINYPY_VALUE_TUPLE) {
        TINYPY_DECREF(bases);
        return NULL;
    }
    return bases;
}
//////////////////////////////////////////////////////////////////////////
/* abstract_issubclass follows a single base in a loop and recurses, under
   the recursion limit, only into several bases; the walk owns each class it
   visits since __bases__ may run Python code. */
static int32_t __tinypy_builtin_abstract_issubclass(tinypy_vm_t *vm, tinypy_value_t *derived, tinypy_value_t *cls, tinypy_error_t **out_error) {
    int32_t result = 0;

    TINYPY_INCREF(derived);
    while (derived != cls) {
        tinypy_value_t *bases = __tinypy_builtin_abstract_bases(vm, derived, out_error);

        if (bases == NULL) {
            result = tinypy_vm_has_error(vm) != 0 ? -1 : 0;
            break;
        }
        if (TINYPY_TUPLE_SIZE(bases) == 1U) {
            tinypy_value_t *base = TINYPY_RET(TINYPY_TUPLE_GET(bases, 0U));

            TINYPY_DECREF(bases);
            TINYPY_DECREF(derived);
            derived = base;
            continue;
        }
        if (TINYPY_TUPLE_SIZE(bases) != 0U && __tinypy_builtin_instance_check_enter(vm, TINYPY_TRUE, out_error) == 0) {
            result = -1;
        }
        else if (TINYPY_TUPLE_SIZE(bases) != 0U) {
            for (size_t index = 0U; result == 0 && index < TINYPY_TUPLE_SIZE(bases); ++index) {
                result = __tinypy_builtin_abstract_issubclass(vm, TINYPY_TUPLE_GET(bases, index), cls, out_error);
            }
            vm->evaluation_depth -= 1U;
        }
        TINYPY_DECREF(bases);
        break;
    }
    if (derived == cls) {
        result = 1;
    }
    TINYPY_DECREF(derived);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_check_class(tinypy_vm_t *vm, tinypy_value_t *value, const char *message, tinypy_error_t **out_error) {
    tinypy_value_t *bases = __tinypy_builtin_abstract_bases(vm, value, out_error);

    if (bases == NULL) {
        if (tinypy_vm_has_error(vm) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, message, out_error);
        }
        return TINYPY_FALSE;
    }
    TINYPY_DECREF(bases);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_object_instance_check(tinypy_value_t *object, tinypy_value_t *classinfo, tinypy_bool_t subclass, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(object);
    tinypy_bool_t metaclass_result;
    int32_t metaclass_handled;

    if (subclass == 0 && TINYPY_VALUE_KIND(classinfo) == TINYPY_VALUE_TYPE && object->type == (tinypy_type_t *)classinfo) {
        return 1;
    }
    if (TINYPY_VALUE_KIND(classinfo) == TINYPY_VALUE_TUPLE) {
        int32_t result = 0;

        if (__tinypy_builtin_instance_check_enter(vm, subclass, out_error) == 0) {
            return -1;
        }
        for (size_t index = 0U; result == 0 && index < TINYPY_TUPLE_SIZE(classinfo); ++index) {
            result = tinypy_internal_object_instance_check(object, TINYPY_TUPLE_GET(classinfo, index), subclass, out_error);
        }
        vm->evaluation_depth -= 1U;
        return result;
    }
    metaclass_handled = __tinypy_builtin_metaclass_check(object, classinfo, subclass, &metaclass_result, out_error);
    if (metaclass_handled < 0) {
        return -1;
    }
    if (metaclass_handled != 0) {
        return metaclass_result;
    }
    if (subclass != 0) {
        if (TINYPY_VALUE_KIND(classinfo) == TINYPY_VALUE_TYPE && TINYPY_VALUE_KIND(object) == TINYPY_VALUE_TYPE) {
            int32_t return_value_1 = tinypy_type_is_subtype((tinypy_type_t *)object, (tinypy_type_t *)classinfo);
            return return_value_1;
        }
        if (__tinypy_builtin_check_class(vm, object, "issubclass() arg 1 must be a class", out_error) == 0 || __tinypy_builtin_check_class(vm, classinfo, "issubclass() arg 2 must be a class or tuple of classes", out_error) == 0) {
            return -1;
        }
        int32_t return_value_2 = __tinypy_builtin_abstract_issubclass(vm, object, classinfo, out_error);
        return return_value_2;
    }
    if (TINYPY_VALUE_KIND(classinfo) == TINYPY_VALUE_CLASS && TINYPY_VALUE_KIND(object) == TINYPY_VALUE_OLD_INSTANCE) {
        int32_t return_value_3 = tinypy_class_is_subclass(tinypy_old_instance_class(object), classinfo);
        return return_value_3;
    }
    if (TINYPY_VALUE_KIND(classinfo) == TINYPY_VALUE_TYPE) {
        if (tinypy_type_is_subtype(object->type, (tinypy_type_t *)classinfo) != 0) {
            return 1;
        }
        /* The __class__ attribute may name a different class, as with proxies. */
        tinypy_error_t *lookup_error = NULL;
        tinypy_value_t *reported = tinypy_object_get_attr_value(object, vm->internal_special_class_key, &lookup_error);
        int32_t result = 0;

        if (reported == NULL) {
            if (lookup_error != NULL) {
                tinypy_error_release(lookup_error);
            }
            tinypy_vm_clear_error(vm);
            return 0;
        }
        if (reported != &object->type->base.base && TINYPY_VALUE_KIND(reported) == TINYPY_VALUE_TYPE) {
            result = tinypy_type_is_subtype((tinypy_type_t *)reported, (tinypy_type_t *)classinfo);
        }
        TINYPY_DECREF(reported);
        return result;
    }
    if (__tinypy_builtin_check_class(vm, classinfo, "isinstance() arg 2 must be a class, type, or tuple of classes and types", out_error) == 0) {
        return -1;
    }
    tinypy_error_t *lookup_error = NULL;
    tinypy_value_t *reported = tinypy_object_get_attr_value(object, vm->internal_special_class_key, &lookup_error);
    int32_t result;

    if (reported == NULL) {
        if (lookup_error != NULL) {
            tinypy_error_release(lookup_error);
        }
        tinypy_vm_clear_error(vm);
        return 0;
    }
    result = __tinypy_builtin_abstract_issubclass(vm, reported, classinfo, out_error);
    TINYPY_DECREF(reported);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_isinstance(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 2U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    result = tinypy_internal_object_instance_check(item, item_2, TINYPY_FALSE, out_error);
    tinypy_value_t *return_value_1 = result < 0 ? NULL : tinypy_bool_from_i32(vm, result);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_issubclass(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 2U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    result = tinypy_internal_object_instance_check(item, item_2, TINYPY_TRUE, out_error);
    tinypy_value_t *return_value_1 = result < 0 ? NULL : tinypy_bool_from_i32(vm, result);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_callable(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    tinypy_bool_t condition = TINYPY_TUPLE_GET(args, 0U)->type->call != NULL;
    if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_internal_exception_state_t state;
        tinypy_error_t *error = NULL;
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_t *call;

        tinypy_internal_exception_preserve_begin(vm, &state);
        call = tinypy_object_get_attr_value(item, vm->internal_special_call_key, &error);
        condition = call != NULL ? TINYPY_TRUE : TINYPY_FALSE;
        if (call != NULL) {
            TINYPY_DECREF(call);
        }
        if (error != NULL) {
            tinypy_error_release(error);
        }
        tinypy_internal_exception_preserve_end(vm, &state);
    }
    else if (condition == 0) {
        condition = tinypy_internal_object_has_special_key(TINYPY_TUPLE_GET(args, 0U), vm->internal_special_call_key) != 0;
    }
    tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, condition);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* The name argument of the attribute builtins: unicode encodes with the
   default codec, anything else but a string is a TypeError; setattr and
   delattr intern the name as PyObject_SetAttr does. */
static tinypy_value_t *__tinypy_builtin_attribute_name(tinypy_value_t *function, tinypy_value_t *name, tinypy_bool_t setting, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(name);

    if ((kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) && setting != 0) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("attribute name must be string, not '"),
            TINYPY_MESSAGE_PART_TYPE_NAME(name),
            TINYPY_MESSAGE_PART_LITERAL("'")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_TEXT(TINYPY_NATIVE_FUNCTION_OBJECT(function)->name),
            TINYPY_MESSAGE_PART_LITERAL("(): attribute name must be string")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    tinypy_value_t *result = kind == TINYPY_VALUE_UNICODE ? tinypy_internal_object_encode_attribute_name(name, out_error) : TINYPY_RET(name);
    if (result != NULL && setting != 0 && tinypy_internal_string_intern(&result, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_getattr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 2U, 3U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *object = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *name = __tinypy_builtin_attribute_name(function, TINYPY_TUPLE_GET(args, 1U), TINYPY_FALSE, out_error);
    if (name == NULL) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U) {
        int32_t status = tinypy_internal_object_get_optional_attr_key(object, name, &result, out_error);

        TINYPY_DECREF(name);
        if (status > 0) {
            return result;
        }
        if (status < 0) {
            return NULL;
        }
        result = TINYPY_RET(TINYPY_TUPLE_GET(args, 2U));
        return result;
    }
    result = tinypy_internal_object_get_attr_key(object, name, out_error);
    TINYPY_DECREF(name);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_hasattr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 2U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *name = __tinypy_builtin_attribute_name(function, TINYPY_TUPLE_GET(args, 1U), TINYPY_FALSE, out_error);
    if (name == NULL) {
        return NULL;
    }
    /* A missing attribute answers False without raising an AttributeError. */
    tinypy_value_t *attribute;
    int32_t status = tinypy_internal_object_get_optional_attr_key(TINYPY_TUPLE_GET(args, 0U), name, &attribute, out_error);
    tinypy_bool_t found = status > 0 ? TINYPY_TRUE : TINYPY_FALSE;

    TINYPY_DECREF(name);
    if (status > 0) {
        TINYPY_DECREF(attribute);
    }
    else if (status < 0) {
        if (vm->raised_type != NULL && (TINYPY_VALUE_KIND(vm->raised_type) != TINYPY_VALUE_TYPE ||
            tinypy_type_is_subtype((tinypy_type_t *)vm->raised_type, vm->exception_types[TINYPY_EXCEPTION_EXCEPTION]) == 0)) {
            return NULL;
        }
        if (out_error != NULL && *out_error != NULL) {
            tinypy_error_release(*out_error);
            *out_error = NULL;
        }
        tinypy_internal_exception_clear_raised(vm);
    }
    tinypy_value_t *result = tinypy_bool_from_i32(vm, found);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_setattr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 3U, 3U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *name = __tinypy_builtin_attribute_name(function, TINYPY_TUPLE_GET(args, 1U), TINYPY_TRUE, out_error);
    if (name == NULL) {
        return NULL;
    }
    tinypy_bool_t assigned = tinypy_object_set_attr_value(TINYPY_TUPLE_GET(args, 0U), name, TINYPY_TUPLE_GET(args, 2U), out_error);

    TINYPY_DECREF(name);
    if (assigned == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_delattr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 2U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *name = __tinypy_builtin_attribute_name(function, TINYPY_TUPLE_GET(args, 1U), TINYPY_TRUE, out_error);
    if (name == NULL) {
        return NULL;
    }
    tinypy_bool_t deleted = tinypy_internal_object_delete_attr_protocol_key(TINYPY_TUPLE_GET(args, 0U), name, out_error);

    TINYPY_DECREF(name);
    if (deleted == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_iter(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = TINYPY_TUPLE_SIZE(args) == 2U
                                        ? tinypy_internal_call_iterator_new(item, TINYPY_TUPLE_GET(args, 1U), out_error)
                                        : tinypy_iter(item, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_next(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_error_t *iteration_error = NULL;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    if (item->type->next == NULL && tinypy_internal_object_has_special_key(item, vm->internal_special_next_key) == 0) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_TYPE_NAME(item),
            TINYPY_MESSAGE_PART_LITERAL(" object is not an iterator")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_next_raw(item, &iteration_error);
    if (result != NULL) {
        return result;
    }
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        (void)tinypy_internal_exception_consume_stop_iteration(vm, &iteration_error);
    }
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        result = TINYPY_RET(TINYPY_TUPLE_GET(args, 1U));
        return result;
    }
    tinypy_internal_exception_raise_stop_iteration(vm, out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* PyArg_ParseTuple("l") as range() tries it first: an int, a long within a C
   long or an __int__ result; any failure leaves the arguments to the long
   implementation. */
static tinypy_bool_t __tinypy_builtin_range_small_argument(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_value_t *integer = NULL;
    tinypy_error_t *error = NULL;

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return TINYPY_TRUE;
    }
    if (kind != TINYPY_VALUE_LONG) {
        tinypy_bool_t handled;

        if (kind == TINYPY_VALUE_FLOAT) {
            return TINYPY_FALSE;
        }
        integer = tinypy_internal_call_int_conversion(value, &handled, &error);
        if (integer == NULL || (TINYPY_VALUE_KIND(integer) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(integer) != TINYPY_VALUE_INTEGER && TINYPY_VALUE_KIND(integer) != TINYPY_VALUE_LONG)) {
            if (integer != NULL) {
                TINYPY_DECREF(integer);
            }
            if (error != NULL) {
                tinypy_error_release(error);
            }
            tinypy_vm_clear_error(vm);
            return TINYPY_FALSE;
        }
        value = integer;
    }
    tinypy_bool_t converted = tinypy_internal_index_as_i64(value, out_value, TINYPY_FALSE, &error);

    if (integer != NULL) {
        TINYPY_DECREF(integer);
    }
    if (converted == 0) {
        tinypy_error_release(error);
        tinypy_vm_clear_error(vm);
    }
    return converted;
}
//////////////////////////////////////////////////////////////////////////
/* get_range_long_argument: an int or long, or the __int__ result of any
   other object but a float. */
static tinypy_value_t *__tinypy_builtin_range_long_argument(tinypy_vm_t *vm, tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_value_t *integer;

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG) {
        integer = TINYPY_RET(value);
    }
    else {
        tinypy_bool_t handled = TINYPY_FALSE;

        integer = kind != TINYPY_VALUE_FLOAT ? tinypy_internal_call_int_conversion(value, &handled, out_error) : NULL;
        if (handled == 0) {
            const tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("range() integer "),
                {name, name_size},
                TINYPY_MESSAGE_PART_LITERAL(" argument expected, got "),
                TINYPY_MESSAGE_PART_TYPE_NAME(value),
                TINYPY_MESSAGE_PART_LITERAL(".")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            return NULL;
        }
        if (integer == NULL) {
            return NULL;
        }
        kind = TINYPY_VALUE_KIND(integer);
        if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG) {
            TINYPY_DECREF(integer);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__int__ should return int object", out_error);
            return NULL;
        }
    }
    /* The copy consumes the reference to the integer. */
    tinypy_value_t *result = tinypy_internal_immutable_subclass_copy(&vm->types[kind], integer, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* handle_range_longs: the bounds stay Python integers and every item is a
   long. */
static tinypy_value_t *__tinypy_builtin_range_large(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_error_t **out_error) {
    static const char *const names[3] = {"start", "end", "step"};
    static const size_t name_sizes[3] = {5U, 3U, 4U};
    static const size_t order[3] = {1U, 0U, 2U};
    tinypy_value_t *values[3] = {NULL, NULL, NULL};
    tinypy_value_t *zero = tinypy_integer_from_i64(vm, INT64_C(0));
    tinypy_value_t *one = tinypy_integer_from_i64(vm, INT64_C(1));
    tinypy_value_t *distance = NULL;
    tinypy_value_t *magnitude = NULL;
    tinypy_value_t *remaining = NULL;
    tinypy_value_t *quotient = NULL;
    tinypy_value_t *length_value = NULL;
    tinypy_value_t *current = NULL;
    tinypy_value_t *result = NULL;
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    int64_t length;

    for (size_t position = 0U; position < 3U; ++position) {
        size_t index = order[position];

        if (index == 0U && argument_count == 1U) {
            values[index] = TINYPY_RET(zero);
        }
        else if (index == 2U && argument_count < 3U) {
            values[index] = TINYPY_RET(one);
        }
        else {
            values[index] = __tinypy_builtin_range_long_argument(vm, TINYPY_TUPLE_GET(args, index - (argument_count == 1U ? 1U : 0U)), names[index], name_sizes[index], out_error);
            if (values[index] == NULL) {
                goto cleanup;
            }
        }
    }
    int32_t positive = tinypy_compare_bool(values[2], zero, TINYPY_COMPARE_GREATER, out_error);
    int32_t nonzero = positive >= 0 ? tinypy_compare_bool(values[2], zero, TINYPY_COMPARE_NOT_EQUAL, out_error) : -1;
    if (nonzero <= 0) {
        if (nonzero == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "range() step argument must not be zero", out_error);
        }
        goto cleanup;
    }
    int32_t nonempty = tinypy_compare_bool(values[0], values[1], positive != 0 ? TINYPY_COMPARE_LESS : TINYPY_COMPARE_GREATER, out_error);
    if (nonempty < 0) {
        goto cleanup;
    }
    if (nonempty == 0) {
        result = tinypy_list_from_items(vm, NULL, 0U);
        goto cleanup;
    }
    distance = tinypy_internal_operator_builtin(positive != 0 ? values[1] : values[0], positive != 0 ? values[0] : values[1], 1, out_error);
    magnitude = positive != 0 ? TINYPY_RET(values[2]) : tinypy_internal_operator_builtin(zero, values[2], 1, out_error);
    remaining = distance != NULL && magnitude != NULL ? tinypy_internal_operator_builtin(distance, one, 1, out_error) : NULL;
    quotient = remaining != NULL ? tinypy_internal_operator_builtin(remaining, magnitude, 4, out_error) : NULL;
    length_value = quotient != NULL ? tinypy_internal_operator_builtin(quotient, one, 0, out_error) : NULL;
    if (length_value == NULL) {
        goto cleanup;
    }
    tinypy_error_t *length_error = NULL;
    if (tinypy_internal_index_as_i64(length_value, &length, TINYPY_FALSE, &length_error) == 0) {
        tinypy_error_release(length_error);
        tinypy_vm_clear_error(vm);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "range() result has too many items", out_error);
        goto cleanup;
    }
    result = tinypy_list_from_items(vm, NULL, 0U);
    if (tinypy_internal_list_reserve_checked(vm, result, (size_t)length, out_error) == 0) {
        TINYPY_DECREF(result);
        result = NULL;
        goto cleanup;
    }
    current = TINYPY_RET(values[0]);
    for (size_t index = 0U; index < (size_t)length; ++index) {
        tinypy_value_t *item = TINYPY_VALUE_KIND(current) == TINYPY_VALUE_LONG ? TINYPY_RET(current) : tinypy_long_from_i64(vm, TINYPY_INTEGER_VALUE(current));
        tinypy_bool_t appended = tinypy_internal_list_append_checked(result, item, out_error);

        TINYPY_DECREF(item);
        if (appended == 0) {
            TINYPY_DECREF(result);
            result = NULL;
            goto cleanup;
        }
        if (index + 1U < (size_t)length) {
            tinypy_value_t *next_value = tinypy_internal_operator_builtin(current, values[2], 0, out_error);

            TINYPY_DECREF(current);
            current = next_value;
            if (current == NULL) {
                TINYPY_DECREF(result);
                result = NULL;
                goto cleanup;
            }
        }
    }
cleanup:
    {
        tinypy_value_t *owned[] = {current, length_value, quotient, remaining, magnitude, distance, values[0], values[1], values[2]};
        for (size_t index = 0U; index < sizeof(owned) / sizeof(owned[0]); ++index) {
            if (owned[index] != NULL) {
                TINYPY_DECREF(owned[index]);
            }
        }
    }
    TINYPY_DECREF(one);
    TINYPY_DECREF(zero);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_range(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t bounds[3] = {INT64_C(0), INT64_C(0), INT64_C(1)};
    uint64_t length = UINT64_C(0);
    size_t index;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 3U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    size_t count = TINYPY_TUPLE_SIZE(args);
    for (index = 0U; index < count; ++index) {
        if (__tinypy_builtin_range_small_argument(vm, TINYPY_TUPLE_GET(args, index), &bounds[index + (count == 1U ? 1U : 0U)]) == 0) {
            tinypy_value_t *result = __tinypy_builtin_range_large(vm, args, out_error);
            return result;
        }
    }
    int64_t start = bounds[0];
    int64_t stop = bounds[1];
    int64_t step = bounds[2];
    if (step == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "range() step argument must not be zero", out_error);
        return NULL;
    }
    if (step > 0 && start < stop) {
        length = ((uint64_t)stop - (uint64_t)start - UINT64_C(1)) / (uint64_t)step + UINT64_C(1);
    }
    else if (step < 0 && start > stop) {
        length = ((uint64_t)start - (uint64_t)stop - UINT64_C(1)) / ((uint64_t)(-(step + INT64_C(1))) + UINT64_C(1)) + UINT64_C(1);
    }
    if (length > (uint64_t)PTRDIFF_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "range() result has too many items", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);

    if (tinypy_internal_list_reserve_checked(vm, result, (size_t)length, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    int64_t current = start;
    for (index = 0U; index < (size_t)length; ++index) {
        tinypy_value_t *item = tinypy_integer_from_i64(vm, current);

        if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(item);
        if (index + 1U < (size_t)length) {
            current += step;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_sorted_arguments(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_value_t **out_source, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *const names[4] = {vm->internal_iterable_key, vm->internal_cmp_key, vm->internal_key_key, vm->internal_reverse_key};
    size_t positional_count = TINYPY_TUPLE_SIZE(args);
    size_t keyword_count = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    size_t matched_keywords = 0U;

    if (positional_count + keyword_count > 4U) {
        tinypy_value_t *name = TINYPY_NATIVE_FUNCTION_OBJECT(function)->name;

        tinypy_internal_make_arity_error(vm, (const char *)TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name), positional_count + keyword_count, 0U, 4U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return TINYPY_FALSE;
    }
    for (size_t parameter = 0U; parameter < 4U; ++parameter) {
        tinypy_value_t *value = parameter < positional_count ? TINYPY_TUPLE_GET(args, parameter) : NULL;
        tinypy_value_t *keyword_value = NULL;

        if (matched_keywords < keyword_count) {
            keyword_value = tinypy_internal_constructor_keyword_optional(kwargs, names[parameter]);
        }
        if (keyword_value != NULL) {
            matched_keywords += 1U;
            if (value != NULL) {
                char position_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
                size_t position_size = tinypy_internal_format_size(position_buffer, parameter + 1U);
                const tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("Argument given by name ('"),
                    TINYPY_MESSAGE_PART_TEXT(names[parameter]),
                    TINYPY_MESSAGE_PART_LITERAL("') and position ("),
                    {position_buffer, position_size},
                    TINYPY_MESSAGE_PART_LITERAL(")")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
                return TINYPY_FALSE;
            }
            value = keyword_value;
        }
        if (parameter == 0U) {
            if (value == NULL) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "Required argument 'iterable' (pos 1) not found", out_error);
                return TINYPY_FALSE;
            }
            *out_source = value;
        }
        if (parameter == 3U && value != NULL) {
            int64_t reverse;

            if (tinypy_internal_integer_as_ssize(value, &reverse, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (reverse < INT_MIN || reverse > INT_MAX) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, reverse < INT_MIN ? "signed integer is less than minimum" : "signed integer is greater than maximum", out_error);
                return TINYPY_FALSE;
            }
        }
    }
    if (matched_keywords != keyword_count) {
        for (size_t index = 0U; index <= TINYPY_DICT_OBJECT(kwargs)->mask; ++index) {
            tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(kwargs)->table[index];
            tinypy_bool_t known = TINYPY_FALSE;

            if (!TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
                continue;
            }
            tinypy_value_type_e kind = TINYPY_VALUE_KIND(entry->key);
            if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
                for (size_t parameter = 0U; parameter < 4U; ++parameter) {
                    if (TINYPY_NAME_EQ(entry->key, names[parameter]) != 0) {
                        known = TINYPY_TRUE;
                        break;
                    }
                }
            }
            if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "keywords must be strings", out_error);
                return TINYPY_FALSE;
            }
            if (known == 0) {
                const tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("'"),
                    TINYPY_MESSAGE_PART_TEXT(entry->key),
                    TINYPY_MESSAGE_PART_LITERAL("' is an invalid keyword argument for this function")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
                return TINYPY_FALSE;
            }
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_sorted(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *sort_method;
    tinypy_value_t *sort_args;
    tinypy_value_t *sort_result;
    size_t argument_count;
    tinypy_value_t *source;

    (void)user_data;
    if (__tinypy_builtin_sorted_arguments(function, args, kwargs, &source, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *iterator = tinypy_iter(source, out_error);
    if (iterator == NULL) {
        return NULL;
    }
    tinypy_value_t *list = tinypy_list_from_items(vm, NULL, 0U);
    int64_t hint;
    if (tinypy_internal_length_hint(source, INT64_C(8), &hint, out_error) == 0) {
        TINYPY_DECREF(list);
        TINYPY_DECREF(iterator);
        return NULL;
    }
    if (hint == INT64_C(-1)) {
        tinypy_internal_exception_raise_system_error(vm, "error return without exception set", out_error);
        TINYPY_DECREF(list);
        TINYPY_DECREF(iterator);
        return NULL;
    }
    size_t reserve_hint = hint < 0 ? 0U : (size_t)hint;
    if ((hint >= 0 && (uint64_t)hint > (uint64_t)SIZE_MAX) || tinypy_internal_list_reserve_checked(vm, list, reserve_hint, out_error) == 0) {
        TINYPY_DECREF(list);
        TINYPY_DECREF(iterator);
        return NULL;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        if (tinypy_internal_list_append_checked(list, item, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(list);
            TINYPY_DECREF(iterator);
            return NULL;
        }
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        TINYPY_DECREF(list);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    sort_method = tinypy_object_get_attr_value(list, vm->internal_sort_key, out_error);
    if (sort_method == NULL) {
        TINYPY_DECREF(list);
        return NULL;
    }
    argument_count = TINYPY_TUPLE_SIZE(args) != 0U ? TINYPY_TUPLE_SIZE(args) - 1U : 0U;
    tinypy_value_t *const *items = argument_count != 0U ? &tinypy_internal_tuple_items(args)[1] : NULL;
    sort_args = tinypy_tuple_from_items(vm, items, argument_count);
    sort_result = tinypy_call(sort_method, sort_args, kwargs, out_error);
    TINYPY_DECREF(sort_args);
    TINYPY_DECREF(sort_method);
    if (sort_result == NULL) {
        TINYPY_DECREF(list);
        return NULL;
    }
    TINYPY_DECREF(sort_result);
    return list;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_all_any(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t want_all = user_data != NULL ? INT32_C(1) : INT32_C(0);
    tinypy_error_t *iteration_error = NULL;

    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *iterator = tinypy_iter(item_2, out_error);
    if (iterator == NULL) {
        return NULL;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        int32_t truth;

        if (item == NULL) {
            break;
        }
        truth = tinypy_truth(item, out_error);
        TINYPY_DECREF(item);
        if (truth < 0) {
            TINYPY_DECREF(iterator);
            return NULL;
        }
        if ((want_all != 0 && truth == 0) || (want_all == 0 && truth != 0)) {
            TINYPY_DECREF(iterator);
            tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, want_all == 0);
            return return_value_1;
        }
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    tinypy_value_t *return_value_2 = tinypy_bool_from_i32(vm, want_all != 0);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_divmod(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 2U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value = tinypy_divmod(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_filter_text(tinypy_vm_t *vm, tinypy_value_t *predicate, tinypy_value_t *input, tinypy_error_t **out_error) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(input);
    size_t byte_size = TINYPY_TEXT_BYTE_SIZE(input);
    tinypy_bool_t unicode = TINYPY_VALUE_KIND(input) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;

    if (TINYPY_VALUE_KIND(predicate) == TINYPY_VALUE_NONE) {
        return TINYPY_RET(input);
    }

    uint8_t *output = byte_size != 0U ? (uint8_t *)tinypy_internal_vm_allocate_checked(vm, byte_size, out_error) : NULL;
    size_t input_offset = 0U;
    size_t output_size = 0U;
    size_t output_code_points = 0U;

    if (byte_size != 0U && output == NULL) {
        return NULL;
    }

    while (input_offset < byte_size) {
        size_t character_size;
        tinypy_value_t *character;
        tinypy_value_t *call_result;
        int32_t truth;

        if (unicode != 0) {
            uint8_t lead = bytes[input_offset];

            character_size = lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
            character = tinypy_unicode_from_utf8(vm, (const char *)bytes + input_offset, character_size);
        }
        else {
            character_size = 1U;
            character = tinypy_string_from_bytes(vm, bytes + input_offset, 1U);
        }
        call_result = __tinypy_builtin_call_items(vm, predicate, &character, 1U, out_error);
        TINYPY_DECREF(character);
        if (call_result == NULL) {
            if (output != NULL) {
                tinypy_internal_vm_deallocate(vm, output, byte_size);
            }
            return NULL;
        }
        truth = tinypy_truth(call_result, out_error);
        TINYPY_DECREF(call_result);
        if (truth < 0) {
            if (output != NULL) {
                tinypy_internal_vm_deallocate(vm, output, byte_size);
            }
            return NULL;
        }
        if (truth != 0) {
            (void)memcpy(output + output_size, bytes + input_offset, character_size);
            output_size += character_size;
            output_code_points += 1U;
        }
        input_offset += character_size;
    }
    tinypy_value_t *result;

    if (unicode != 0) {
        uint8_t *result_bytes;

        result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_UNICODE, output_size, output_code_points, &result_bytes, out_error);
        if (result != NULL && output_size != 0U) {
            (void)memcpy(result_bytes, output, output_size);
        }
    }
    else {
        result = tinypy_internal_string_from_bytes_checked(vm, output, output_size, out_error);
    }
    if (output != NULL) {
        tinypy_internal_vm_deallocate(vm, output, byte_size);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_filter(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *iteration_source;
    tinypy_value_t *selected;
    tinypy_value_t *result;
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 2U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *predicate = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *input = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_type_e input_kind = TINYPY_VALUE_KIND(input);
    tinypy_bool_t text_input = input_kind == TINYPY_VALUE_STRING || input_kind == TINYPY_VALUE_UNICODE;
    tinypy_bool_t indexed_input = text_input != 0 || input_kind == TINYPY_VALUE_TUPLE;
    tinypy_bool_t direct_truth = TINYPY_VALUE_KIND(predicate) == TINYPY_VALUE_NONE
        || (indexed_input == 0 && predicate == &vm->types[TINYPY_VALUE_BOOL].base.base);
    size_t input_index = 0U;
    if ((input->type == &vm->types[TINYPY_VALUE_STRING] || input->type == &vm->types[TINYPY_VALUE_UNICODE])) {
        tinypy_value_t *return_value_1 = __tinypy_builtin_filter_text(vm, predicate, input, out_error);
        return return_value_1;
    }
    if (indexed_input != 0) {
        /* Python 2 filters immutable sequences by their storage length and
           item protocol, ignoring overridden __iter__ and __len__. */
        iteration_source = TINYPY_RET(input);
    }
    else {
        iteration_source = tinypy_iter(input, out_error);
    }
    if (iteration_source == NULL) {
        return NULL;
    }
    selected = tinypy_list_from_items(vm, NULL, 0U);
    int64_t hint;
    if (indexed_input != 0) {
        hint = (int64_t)tinypy_internal_iterable_size_hint(input);
    }
    else if (tinypy_internal_length_hint(input, INT64_C(8), &hint, out_error) == 0) {
        TINYPY_DECREF(selected);
        TINYPY_DECREF(iteration_source);
        return NULL;
    }
    if (hint < 0) {
        tinypy_internal_exception_raise_system_error(vm, hint == INT64_C(-1) ? "error return without exception set" : "bad argument to internal function", out_error);
        TINYPY_DECREF(selected);
        TINYPY_DECREF(iteration_source);
        return NULL;
    }
    if ((uint64_t)hint > (uint64_t)SIZE_MAX || tinypy_internal_list_reserve_checked(vm, selected, (size_t)hint, out_error) == 0) {
        TINYPY_DECREF(selected);
        TINYPY_DECREF(iteration_source);
        return NULL;
    }
    for (;;) {
        tinypy_value_t *item;
        int32_t truth;

        if (indexed_input != 0) {
            if (input_index == TINYPY_SIZED_SIZE(input)) {
                break;
            }
            if (input->type == &vm->types[TINYPY_VALUE_TUPLE]) {
                item = TINYPY_RET(TINYPY_TUPLE_GET(input, input_index));
            }
            else {
                tinypy_value_t *key = tinypy_integer_from_i64(vm, (int64_t)input_index);

                item = tinypy_get_item(input, key, &iteration_error);
                TINYPY_DECREF(key);
            }
            input_index += 1U;
        }
        else {
            item = tinypy_next(iteration_source, &iteration_error);
        }
        if (item == NULL) {
            break;
        }
        if (direct_truth != 0) {
            truth = text_input != 0 ? 1 : tinypy_truth(item, out_error);
        }
        else {
            tinypy_value_t *call_result = __tinypy_builtin_call_items(vm, predicate, &item, 1U, out_error);

            if (call_result == NULL) {
                TINYPY_DECREF(item);
                TINYPY_DECREF(selected);
                TINYPY_DECREF(iteration_source);
                return NULL;
            }
            truth = tinypy_truth(call_result, out_error);
            TINYPY_DECREF(call_result);
        }
        if (truth < 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(selected);
            TINYPY_DECREF(iteration_source);
            return NULL;
        }
        if (truth != 0) {
            if (text_input != 0 && TINYPY_VALUE_KIND(item) != input_kind) {
                TINYPY_DECREF(item);
                TINYPY_DECREF(selected);
                TINYPY_DECREF(iteration_source);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, input_kind == TINYPY_VALUE_UNICODE ? "can't filter unicode to unicode: __getitem__ returned different type" : "can't filter str to str: __getitem__ returned different type", out_error);
                return NULL;
            }
            if (tinypy_internal_list_append_checked(selected, item, out_error) == 0) {
                TINYPY_DECREF(item);
                TINYPY_DECREF(selected);
                TINYPY_DECREF(iteration_source);
                return NULL;
            }
        }
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iteration_source);
    if (iteration_error != NULL) {
        TINYPY_DECREF(selected);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    if (TINYPY_VALUE_KIND(input) == TINYPY_VALUE_TUPLE) {
        size_t list_size = TINYPY_LIST_SIZE(selected);
        result = tinypy_internal_tuple_from_items_checked(vm, TINYPY_LIST_OBJECT(selected)->items, list_size, out_error);
    }
    else if (TINYPY_VALUE_KIND(input) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(input) == TINYPY_VALUE_UNICODE) {
        size_t total = 0U;
        uint8_t empty_buffer = 0U;
        uint8_t *buffer = &empty_buffer;
        size_t output = 0U;
        tinypy_bool_t overflow = TINYPY_FALSE;

        size_t code_points = 0U;

        iterator = TINYPY_LIST_ITERATOR_BEGIN(selected);
        iterator_end = TINYPY_LIST_ITERATOR_END(selected);
        for (; iterator != iterator_end; ++iterator) {
            size_t item_size;

            item_size = TINYPY_TEXT_BYTE_SIZE(*iterator);
            if (item_size > SIZE_MAX - total) {
                overflow = TINYPY_TRUE;
                break;
            }
            total += item_size;
            code_points += TINYPY_SIZED_SIZE(*iterator);
        }
        if (overflow != 0 || total >= (size_t)PTRDIFF_MAX) {
            TINYPY_DECREF(selected);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "filtered string is too large", out_error);
            return NULL;
        }
        if (total != 0U) {
            buffer = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, total, out_error);
            if (buffer == NULL) {
                TINYPY_DECREF(selected);
                return NULL;
            }
        }
        iterator = TINYPY_LIST_ITERATOR_BEGIN(selected);
        iterator_end = TINYPY_LIST_ITERATOR_END(selected);
        for (; iterator != iterator_end; ++iterator) {
            tinypy_value_t *item = *iterator;
            size_t size = TINYPY_TEXT_BYTE_SIZE(item);

            if (size != 0U) {
                (void)memcpy(buffer + output, TINYPY_TEXT_BYTES(item), size);
            }
            output += size;
        }
        if (TINYPY_VALUE_KIND(input) == TINYPY_VALUE_UNICODE) {
            uint8_t *result_bytes;

            result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_UNICODE, total, code_points, &result_bytes, out_error);
            if (result != NULL && total != 0U) {
                (void)memcpy(result_bytes, buffer, total);
            }
        }
        else {
            result = tinypy_internal_string_from_bytes_checked(vm, buffer, total, out_error);
        }
        if (total != 0U) {
            tinypy_internal_vm_deallocate(vm, buffer, total);
        }
    }
    else {
        size_t selected_size = TINYPY_LIST_SIZE(selected);
        size_t selected_capacity = TINYPY_LIST_OBJECT(selected)->allocated;

        if (selected_capacity - selected_size > selected_size / 4U + 8U) {
            tinypy_internal_list_shrink_to_fit(vm, selected);
        }
        result = TINYPY_RET(selected);
    }
    TINYPY_DECREF(selected);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_map(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t iterable_count;
    size_t iterable_index;
    size_t result_hint = 0U;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count_message(vm, args, 2U, SIZE_MAX, "map() requires at least two args", out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *callable = TINYPY_TUPLE_GET(args, 0U);
    iterable_count = TINYPY_TUPLE_SIZE(args) - 1U;
    if (iterable_count == 1U && TINYPY_VALUE_KIND(callable) == TINYPY_VALUE_NONE) {
        tinypy_value_t *input = TINYPY_TUPLE_GET(args, 1U);

        if (input->type == &vm->types[TINYPY_VALUE_LIST]) {
            result = tinypy_internal_list_from_items_checked(vm, TINYPY_LIST_OBJECT(input)->items, TINYPY_LIST_SIZE(input), out_error);
            return result;
        }
        if (input->type == &vm->types[TINYPY_VALUE_TUPLE]) {
            result = tinypy_internal_list_from_items_checked(vm, tinypy_internal_tuple_items(input), TINYPY_TUPLE_SIZE(input), out_error);
            return result;
        }
        tinypy_value_t *list_arguments = tinypy_tuple_from_items(vm, &input, 1U);

        result = tinypy_internal_list_create(&vm->types[TINYPY_VALUE_LIST], list_arguments, NULL, out_error);
        TINYPY_DECREF(list_arguments);
        return result;
    }
    if (iterable_count > SIZE_MAX / (2U * sizeof(tinypy_value_t *) + 2U * sizeof(uint8_t))) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "map temporary storage is too large", out_error);
        return NULL;
    }
    size_t scratch_size = iterable_count * (2U * sizeof(tinypy_value_t *) + 2U * sizeof(uint8_t));
    uint8_t *scratch = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, scratch_size, out_error);
    if (scratch == NULL) {
        return NULL;
    }
    tinypy_value_t **iterators = (tinypy_value_t **)scratch;
    tinypy_value_t **call_items = iterators + iterable_count;
    uint8_t *exhausted = (uint8_t *)(call_items + iterable_count);
    uint8_t *yielded = exhausted + iterable_count;

    (void)memset(exhausted, 0, iterable_count * sizeof(*exhausted));
    for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, iterable_index + 1U);
        tinypy_error_t *iterator_error = NULL;

        iterators[iterable_index] = tinypy_iter(item, &iterator_error);
        if (iterators[iterable_index] == NULL) {
            if (iterator_error != NULL) {
                tinypy_error_release(iterator_error);
            }
            char position[TINYPY_MESSAGE_SIZE_BUFFER];
            size_t position_size = tinypy_internal_format_size(position, iterable_index + 2U);
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("argument "),
                {position, position_size},
                TINYPY_MESSAGE_PART_LITERAL(" to map() must support iteration")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
            while (iterable_index != 0U) {
                TINYPY_DECREF(iterators[--iterable_index]);
            }
            tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
            return NULL;
        }
        int64_t hint;
        tinypy_error_t *hint_error = NULL;
        if (tinypy_internal_length_hint(item, INT64_C(8), &hint, &hint_error) == 0) {
            /* Python 2's builtin_map neglects to check the error from
               _PyObject_LengthHint. Preserve that observable behavior on
               the general map path; map(None, one iterable) uses list(). */
            if (hint_error != NULL) {
                tinypy_error_release(hint_error);
            }
            tinypy_vm_clear_error(vm);
            continue;
        }
        if (hint < 0) {
            continue;
        }
        if ((uint64_t)hint > (uint64_t)SIZE_MAX) {
            size_t release_count = iterable_index + 1U;

            while (release_count != 0U) {
                TINYPY_DECREF(iterators[--release_count]);
            }
            tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
            return NULL;
        }
        if ((size_t)hint > result_hint) {
            result_hint = (size_t)hint;
        }
    }
    result = tinypy_list_from_items(vm, NULL, 0U);
    if (tinypy_internal_list_reserve_checked(vm, result, result_hint, out_error) == 0) {
        TINYPY_DECREF(result);
        result = NULL;
        goto map_cleanup;
    }
    for (;;) {
        tinypy_bool_t have_item = TINYPY_FALSE;
        tinypy_value_t *mapped;

        (void)memset(yielded, 0, iterable_count * sizeof(*yielded));
        for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
            tinypy_error_t *iteration_error = NULL;

            if (exhausted[iterable_index] != 0U) {
                call_items[iterable_index] = &vm->none_object.base;
                continue;
            }
            call_items[iterable_index] = tinypy_next(iterators[iterable_index], &iteration_error);
            if (call_items[iterable_index] == NULL) {
                if (iteration_error != NULL) {
                    size_t release_index;

                    for (release_index = 0U; release_index < iterable_index; ++release_index) {
                        if (yielded[release_index] != 0U) {
                            TINYPY_DECREF(call_items[release_index]);
                        }
                    }
                    if (out_error != NULL) {
                        *out_error = iteration_error;
                    }
                    else {
                        tinypy_error_release(iteration_error);
                    }
                    TINYPY_DECREF(result);
                    result = NULL;
                    goto map_cleanup;
                }
                exhausted[iterable_index] = 1U;
                call_items[iterable_index] = &vm->none_object.base;
                continue;
            }
            yielded[iterable_index] = 1U;
            have_item = TINYPY_TRUE;
        }
        if (have_item == 0) {
            break;
        }
        if (TINYPY_VALUE_KIND(callable) == TINYPY_VALUE_NONE) {
            mapped = iterable_count == 1U ? call_items[0] : NULL;
            if (mapped != NULL) {
                TINYPY_INCREF(mapped);
            }
            else {
                mapped = tinypy_internal_tuple_from_items_checked(vm, call_items, iterable_count, out_error);
            }
        }
        else {
            mapped = __tinypy_builtin_call_items(vm, callable, call_items, iterable_count, out_error);
        }
        for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
            if (yielded[iterable_index] != 0U) {
                TINYPY_DECREF(call_items[iterable_index]);
            }
        }
        if (mapped == NULL) {
            TINYPY_DECREF(result);
            result = NULL;
            break;
        }
        if (tinypy_internal_list_append_checked(result, mapped, out_error) == 0) {
            TINYPY_DECREF(mapped);
            TINYPY_DECREF(result);
            result = NULL;
            break;
        }
        TINYPY_DECREF(mapped);
    }
map_cleanup:
    for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
        TINYPY_DECREF(iterators[iterable_index]);
    }
    tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_zip(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t iterable_count = TINYPY_TUPLE_SIZE(args);
    size_t iterable_index;
    int64_t result_hint = INT64_C(-1);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0) {
        return NULL;
    }
    if (iterable_count == 0U) {
        tinypy_value_t *return_value_1 = tinypy_list_from_items(vm, NULL, 0U);
        return return_value_1;
    }
    if (iterable_count > SIZE_MAX / (2U * sizeof(tinypy_value_t *))) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "zip temporary storage is too large", out_error);
        return NULL;
    }
    size_t scratch_size = iterable_count * 2U * sizeof(tinypy_value_t *);
    tinypy_value_t **scratch = (tinypy_value_t **)tinypy_internal_vm_allocate_checked(vm, scratch_size, out_error);
    if (scratch == NULL) {
        return NULL;
    }
    tinypy_value_t **iterators = scratch;
    tinypy_value_t **tuple_items = iterators + iterable_count;
    for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, iterable_index);
        int64_t hint;

        if (tinypy_internal_length_hint(item, INT64_C(-2), &hint, out_error) == 0) {
            tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
            return NULL;
        }
        if (hint == INT64_C(-1)) {
            tinypy_internal_exception_raise_system_error(vm, "error return without exception set", out_error);
            tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
            return NULL;
        }
        if (hint < 0) {
            result_hint = INT64_C(-1);
            break;
        }
        if (result_hint < 0 || hint < result_hint) {
            result_hint = hint;
        }
    }
    for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, iterable_index);

        iterators[iterable_index] = tinypy_iter(item, out_error);
        if (iterators[iterable_index] == NULL) {
            if (tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_TYPE_ERROR, out_error) != 0) {
                char position[TINYPY_MESSAGE_SIZE_BUFFER];
                size_t position_size = tinypy_internal_format_size(position, iterable_index + 1U);
                const tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("zip argument #"),
                    {position, position_size},
                    TINYPY_MESSAGE_PART_LITERAL(" must support iteration")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            }
            while (iterable_index != 0U) {
                TINYPY_DECREF(iterators[--iterable_index]);
            }
            tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
            return NULL;
        }
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    size_t reserve_hint = result_hint < 0 ? 10U : (size_t)result_hint;
    if (tinypy_internal_list_reserve_checked(vm, result, reserve_hint, out_error) == 0) {
        TINYPY_DECREF(result);
        result = NULL;
        goto zip_cleanup;
    }
    for (;;) {
        tinypy_value_t *tuple;

        for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
            tinypy_error_t *iteration_error = NULL;

            tuple_items[iterable_index] = tinypy_next(iterators[iterable_index], &iteration_error);
            if (tuple_items[iterable_index] == NULL) {
                size_t release_index;

                for (release_index = 0U; release_index < iterable_index; ++release_index) {
                    TINYPY_DECREF(tuple_items[release_index]);
                }
                if (iteration_error != NULL) {
                    if (out_error != NULL) {
                        *out_error = iteration_error;
                    }
                    else {
                        tinypy_error_release(iteration_error);
                    }
                    TINYPY_DECREF(result);
                    result = NULL;
                }
                goto zip_cleanup;
            }
        }
        tuple = tinypy_internal_tuple_from_items_checked(vm, tuple_items, iterable_count, out_error);
        for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
            TINYPY_DECREF(tuple_items[iterable_index]);
        }
        if (tuple == NULL) {
            TINYPY_DECREF(result);
            result = NULL;
            goto zip_cleanup;
        }
        if (tinypy_internal_list_append_checked(result, tuple, out_error) == 0) {
            TINYPY_DECREF(tuple);
            TINYPY_DECREF(result);
            result = NULL;
            goto zip_cleanup;
        }
        TINYPY_DECREF(tuple);
    }
zip_cleanup:
    for (iterable_index = 0U; iterable_index < iterable_count; ++iterable_index) {
        TINYPY_DECREF(iterators[iterable_index]);
    }
    tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_sum(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *iterator;
    tinypy_value_t *total;
    int64_t integer_total = 0;
    tinypy_bool_t integer_fast_path;
    tinypy_bool_t float_fast_path;
    double float_total = 0.0;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *iterable = TINYPY_TUPLE_GET(args, 0U);
    iterator = tinypy_iter(iterable, out_error);
    if (iterator == NULL) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        total = TINYPY_RET(TINYPY_TUPLE_GET(args, 1U));
    }
    else {
        total = tinypy_integer_from_i64(vm, INT64_C(0));
    }
    if (TINYPY_VALUE_KIND(total) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(total) == TINYPY_VALUE_UNICODE) {
        TINYPY_DECREF(total);
        TINYPY_DECREF(iterator);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "sum() can't sum strings [use ''.join(seq) instead]", out_error);
        return NULL;
    }
    integer_fast_path = total->type == &vm->types[TINYPY_VALUE_INTEGER] || total->type == &vm->types[TINYPY_VALUE_BOOL];
    if (integer_fast_path != 0) {
        integer_total = TINYPY_INTEGER_VALUE(total);
    }
    float_fast_path = total->type == &vm->types[TINYPY_VALUE_FLOAT];
    if (float_fast_path != 0) {
        float_total = TINYPY_FLOAT_OBJECT(total)->value;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        tinypy_value_t *next_total;

        if (item == NULL) {
            break;
        }
        if (integer_fast_path != 0 && (item->type == &vm->types[TINYPY_VALUE_INTEGER] || item->type == &vm->types[TINYPY_VALUE_BOOL])) {
            int64_t item_value = TINYPY_INTEGER_VALUE(item);

            if (!((item_value > 0 && integer_total > INT64_MAX - item_value) || (item_value < 0 && integer_total < INT64_MIN - item_value))) {
                integer_total += item_value;
                TINYPY_DECREF(item);
                if (total != NULL) {
                    TINYPY_DECREF(total);
                    total = NULL;
                }
                continue;
            }
        }
        if (integer_fast_path != 0 && item->type == &vm->types[TINYPY_VALUE_FLOAT]) {
            float_total = (double)integer_total;
            integer_fast_path = TINYPY_FALSE;
            float_fast_path = TINYPY_TRUE;
        }
        if (float_fast_path != 0 && (item->type == &vm->types[TINYPY_VALUE_FLOAT] || item->type == &vm->types[TINYPY_VALUE_INTEGER] || item->type == &vm->types[TINYPY_VALUE_BOOL])) {
            float_total += item->type == &vm->types[TINYPY_VALUE_FLOAT] ? TINYPY_FLOAT_OBJECT(item)->value : (double)TINYPY_INTEGER_VALUE(item);
            TINYPY_DECREF(item);
            if (total != NULL) {
                TINYPY_DECREF(total);
                total = NULL;
            }
            continue;
        }
        if (total == NULL) {
            total = float_fast_path != 0 ? tinypy_float_from_double(vm, float_total) : tinypy_integer_from_i64(vm, integer_total);
        }
        integer_fast_path = TINYPY_FALSE;
        float_fast_path = TINYPY_FALSE;
        next_total = tinypy_add(total, item, out_error);

        TINYPY_DECREF(item);
        TINYPY_DECREF(total);
        if (next_total == NULL) {
            TINYPY_DECREF(iterator);
            return NULL;
        }
        total = next_total;
        if (total->type == &vm->types[TINYPY_VALUE_FLOAT]) {
            float_fast_path = TINYPY_TRUE;
            float_total = TINYPY_FLOAT_OBJECT(total)->value;
        }
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (total != NULL) {
            TINYPY_DECREF(total);
        }
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    if (total == NULL) {
        total = float_fast_path != 0 ? tinypy_float_from_double(vm, float_total) : tinypy_integer_from_i64(vm, integer_total);
    }
    return total;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_min_max(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t want_max = user_data != NULL ? INT32_C(1) : INT32_C(0);
    tinypy_value_t *key_function = NULL;
    tinypy_value_t *iterator;
    tinypy_value_t *best;
    tinypy_value_t *best_key;
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *name = TINYPY_NATIVE_FUNCTION_OBJECT(function)->name;

    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_internal_make_arity_error(vm, (const char *)TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name), 0U, 1U, 1U, TINYPY_ARITY_STYLE_UNPACK, out_error);
        return NULL;
    }
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        key_function = tinypy_internal_dict_get_optional_suppressed(vm, kwargs, vm->internal_key_key);
        if (TINYPY_DICT_SIZE(kwargs) != 1U || key_function == NULL) {
            const tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_TEXT(name),
                TINYPY_MESSAGE_PART_LITERAL("() got an unexpected keyword argument")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            return NULL;
        }
    }
    tinypy_value_t *source = TINYPY_TUPLE_SIZE(args) == 1U ? TINYPY_TUPLE_GET(args, 0U) : args;
    iterator = tinypy_iter(source, out_error);
    if (iterator == NULL) {
        return NULL;
    }
    best = tinypy_next(iterator, &iteration_error);
    if (best == NULL) {
        TINYPY_DECREF(iterator);
        if (iteration_error != NULL) {
            if (out_error != NULL) {
                *out_error = iteration_error;
            }
            else {
                tinypy_error_release(iteration_error);
            }
            return NULL;
        }
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_TEXT(name),
            TINYPY_MESSAGE_PART_LITERAL("() arg is an empty sequence")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (key_function != NULL) {
        best_key = __tinypy_builtin_call_items(vm, key_function, &best, 1U, out_error);
        if (best_key == NULL) {
            TINYPY_DECREF(best);
            TINYPY_DECREF(iterator);
            return NULL;
        }
    }
    else {
        best_key = TINYPY_RET(best);
    }
    for (;;) {
        tinypy_value_t *candidate = tinypy_next(iterator, &iteration_error);
        tinypy_value_t *candidate_key;
        int32_t better;

        if (candidate == NULL) {
            break;
        }
        if (key_function != NULL) {
            candidate_key = __tinypy_builtin_call_items(vm, key_function, &candidate, 1U, out_error);
            if (candidate_key == NULL) {
                TINYPY_DECREF(candidate);
                TINYPY_DECREF(best_key);
                TINYPY_DECREF(best);
                TINYPY_DECREF(iterator);
                return NULL;
            }
        }
        else {
            candidate_key = TINYPY_RET(candidate);
        }
        better = tinypy_compare_bool(candidate_key, best_key, want_max != 0 ? TINYPY_COMPARE_GREATER : TINYPY_COMPARE_LESS, out_error);
        if (better < 0) {
            TINYPY_DECREF(candidate_key);
            TINYPY_DECREF(candidate);
            TINYPY_DECREF(best_key);
            TINYPY_DECREF(best);
            TINYPY_DECREF(iterator);
            return NULL;
        }
        if (better != 0) {
            TINYPY_DECREF(best_key);
            TINYPY_DECREF(best);
            best_key = candidate_key;
            best = candidate;
        }
        else {
            TINYPY_DECREF(candidate_key);
            TINYPY_DECREF(candidate);
        }
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        TINYPY_DECREF(best_key);
        TINYPY_DECREF(best);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    TINYPY_DECREF(best_key);
    return best;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_chr_common(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t value;

    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 ||
        __tinypy_builtin_argument_count(function, args, 1U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    /* chr parses its argument as a C long, unichr as a C int. */
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_internal_integer_as_ssize(item, &value, out_error) == 0) {
        return NULL;
    }
    if (user_data == NULL) {
        uint8_t byte;

        if (value < 0 || value > 255) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "chr() arg not in range(256)", out_error);
            return NULL;
        }
        byte = (uint8_t)value;
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, &byte, 1U);
        return return_value_1;
    }
    if (value < INT_MIN || value > INT_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, value < INT_MIN ? "signed integer is less than minimum" : "signed integer is greater than maximum", out_error);
        return NULL;
    }
    if (value < 0 || value > INT64_C(0x10ffff)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unichr() arg not in range(0x110000) (wide Python build)", out_error);
        return NULL;
    }
    char utf8[4];
    size_t size;

    if (value < 0x80) {
        utf8[0] = (char)value;
        size = 1U;
    }
    else if (value < 0x800) {
        utf8[0] = (char)(0xc0 | (value >> 6));
        utf8[1] = (char)(0x80 | (value & 0x3f));
        size = 2U;
    }
    else if (value < 0x10000) {
        utf8[0] = (char)(0xe0 | (value >> 12));
        utf8[1] = (char)(0x80 | ((value >> 6) & 0x3f));
        utf8[2] = (char)(0x80 | (value & 0x3f));
        size = 3U;
    }
    else {
        utf8[0] = (char)(0xf0 | (value >> 18));
        utf8[1] = (char)(0x80 | ((value >> 12) & 0x3f));
        utf8[2] = (char)(0x80 | ((value >> 6) & 0x3f));
        utf8[3] = (char)(0x80 | (value & 0x3f));
        size = 4U;
    }
    tinypy_value_t *return_value_2 = tinypy_unicode_from_utf8(vm, utf8, size);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_builtin_coerce_one(tinypy_value_t *left, tinypy_value_t *right, tinypy_bool_t reverse, tinypy_value_t **out_result, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *method;
    tinypy_value_t *arguments;
    tinypy_value_t *result;

    if (tinypy_internal_object_has_special_key(left, vm->internal_special_coerce_key) == 0) {
        return INT32_C(0);
    }
    method = tinypy_internal_object_get_special_key(left, vm->internal_special_coerce_key, out_error);
    if (method == NULL) {
        return -INT32_C(1);
    }
    arguments = tinypy_tuple_from_items(vm, &right, 1U);
    result = tinypy_call(method, arguments, NULL, out_error);
    TINYPY_DECREF(arguments);
    TINYPY_DECREF(method);
    if (result == NULL) {
        return -INT32_C(1);
    }
    if (result == &vm->not_implemented_object.base || (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_OLD_INSTANCE && TINYPY_VALUE_KIND(result) == TINYPY_VALUE_NONE)) {
        TINYPY_DECREF(result);
        return INT32_C(0);
    }
    if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(result) != 2U) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__coerce__ must return a 2-tuple or NotImplemented", out_error);
        return -INT32_C(1);
    }
    if (reverse != 0) {
        tinypy_value_t *items[2] = {TINYPY_TUPLE_GET(result, 1U), TINYPY_TUPLE_GET(result, 0U)};
        tinypy_value_t *ordered = tinypy_tuple_from_items(vm, items, 2U);

        TINYPY_DECREF(result);
        result = ordered;
    }
    *out_result = result;
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_coerce(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *left;
    tinypy_value_t *right;
    tinypy_value_t *result;
    int32_t status;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 2U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    left = TINYPY_TUPLE_GET(args, 0U);
    right = TINYPY_TUPLE_GET(args, 1U);
    status = __tinypy_builtin_coerce_one(left, right, TINYPY_FALSE, &result, out_error);
    if (status > 0) {
        return result;
    }
    if (status < 0) {
        return NULL;
    }
    status = __tinypy_builtin_coerce_one(right, left, TINYPY_TRUE, &result, out_error);
    if (status > 0) {
        return result;
    }
    if (status < 0) {
        return NULL;
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(left);
    if (left->type == right->type && (left->type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U && kind != TINYPY_VALUE_OLD_INSTANCE && kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE && kind != TINYPY_VALUE_SET && kind != TINYPY_VALUE_FROZENSET) {
        tinypy_value_t *items[2] = {left, right};
        tinypy_value_t *return_value_1 = tinypy_tuple_from_items(vm, items, 2U);
        return return_value_1;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "number coercion failed", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_cmp(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t order;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 2U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_internal_compare_three_way(left, right, &order, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)order);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_hash(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_hash_t hash = tinypy_internal_hash_value(value, out_error);
    if (tinypy_vm_has_error(vm) != 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)hash);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_pow(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 2U, 3U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *return_value_1 = TINYPY_TUPLE_SIZE(args) == 3U
                                        ? tinypy_internal_power_modulo(item, item_2, TINYPY_TUPLE_GET(args, 2U), out_error)
                                        : tinypy_power(item, item_2, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
#define TINYPY_ROUND_BIGINT_WORDS ((size_t)80U)

typedef struct tinypy_round_bigint_t {
    uint32_t words[TINYPY_ROUND_BIGINT_WORDS];
    size_t count;
} tinypy_round_bigint_t;

//////////////////////////////////////////////////////////////////////////
static void __tinypy_round_bigint_normalize(tinypy_round_bigint_t *value) {
    while (value->count != 0U && value->words[value->count - 1U] == 0U) {
        value->count -= 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_round_bigint_set_u64(tinypy_round_bigint_t *value, uint64_t integer) {
    (void)memset(value, 0, sizeof(*value));
    if (integer != 0U) {
        value->words[0] = (uint32_t)integer;
        value->words[1] = (uint32_t)(integer >> 32U);
        value->count = value->words[1] == 0U ? 1U : 2U;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_round_bigint_multiply_u32(tinypy_round_bigint_t *value, uint32_t multiplier) {
    uint64_t carry = 0U;
    size_t index;

    for (index = 0U; index < value->count; ++index) {
        uint64_t product = (uint64_t)value->words[index] * multiplier + carry;

        value->words[index] = (uint32_t)product;
        carry = product >> 32U;
    }
    if (carry != 0U) {
        if (value->count == TINYPY_ROUND_BIGINT_WORDS) {
            return TINYPY_FALSE;
        }
        value->words[value->count++] = (uint32_t)carry;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_round_bigint_shift_left(tinypy_round_bigint_t *value, size_t shift) {
    size_t word_shift;
    uint32_t bit_shift;
    size_t old_count;
    size_t new_count;
    size_t index;

    if (value->count == 0U || shift == 0U) {
        return TINYPY_TRUE;
    }
    word_shift = shift / 32U;
    bit_shift = (uint32_t)(shift % 32U);
    old_count = value->count;
    new_count = old_count + word_shift + (bit_shift != 0U ? 1U : 0U);
    if (new_count > TINYPY_ROUND_BIGINT_WORDS) {
        return TINYPY_FALSE;
    }
    for (index = new_count; index != 0U; --index) {
        size_t output_index = index - 1U;
        uint64_t word = 0U;

        if (output_index >= word_shift) {
            size_t source_index = output_index - word_shift;

            if (source_index < old_count) {
                word |= (uint64_t)value->words[source_index] << bit_shift;
            }
            if (bit_shift != 0U && source_index != 0U && source_index - 1U < old_count) {
                word |= (uint64_t)value->words[source_index - 1U] >> (32U - bit_shift);
            }
        }
        value->words[output_index] = (uint32_t)word;
    }
    value->count = new_count;
    __tinypy_round_bigint_normalize(value);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_round_bigint_bit_length(const tinypy_round_bigint_t *value) {
    size_t bits;
    uint32_t high;

    if (value->count == 0U) {
        return 0U;
    }
    bits = (value->count - 1U) * 32U;
    high = value->words[value->count - 1U];
    while (high != 0U) {
        bits += 1U;
        high >>= 1U;
    }
    return bits;
}
//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_round_bigint_shifted_word(const tinypy_round_bigint_t *value, size_t output_index, size_t shift) {
    size_t word_shift = shift / 32U;
    uint32_t bit_shift = (uint32_t)(shift % 32U);
    uint64_t word = 0U;

    if (output_index >= word_shift) {
        size_t source_index = output_index - word_shift;

        if (source_index < value->count) {
            word |= (uint64_t)value->words[source_index] << bit_shift;
        }
        if (bit_shift != 0U && source_index != 0U && source_index - 1U < value->count) {
            word |= (uint64_t)value->words[source_index - 1U] >> (32U - bit_shift);
        }
    }
    return (uint32_t)word;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_round_bigint_shifted_count(const tinypy_round_bigint_t *value, size_t shift) {
    size_t count;

    if (value->count == 0U) {
        return 0U;
    }
    count = value->count + shift / 32U + (shift % 32U != 0U ? 1U : 0U);
    while (count != 0U && __tinypy_round_bigint_shifted_word(value, count - 1U, shift) == 0U) {
        count -= 1U;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_round_bigint_compare_shifted(const tinypy_round_bigint_t *left, const tinypy_round_bigint_t *right, size_t right_shift) {
    size_t right_count = __tinypy_round_bigint_shifted_count(right, right_shift);
    size_t index;

    if (left->count != right_count) {
        return left->count < right_count ? -1 : 1;
    }
    index = left->count;
    while (index != 0U) {
        uint32_t left_word;
        uint32_t right_word;

        index -= 1U;
        left_word = left->words[index];
        right_word = __tinypy_round_bigint_shifted_word(right, index, right_shift);
        if (left_word != right_word) {
            return left_word < right_word ? -1 : 1;
        }
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_round_bigint_subtract_shifted(tinypy_round_bigint_t *left, const tinypy_round_bigint_t *right, size_t right_shift) {
    size_t right_count = __tinypy_round_bigint_shifted_count(right, right_shift);
    uint64_t borrow = 0U;
    size_t index;

    for (index = 0U; index < left->count; ++index) {
        uint64_t subtrahend = (index < right_count ? (uint64_t)__tinypy_round_bigint_shifted_word(right, index, right_shift) : 0U) + borrow;
        uint64_t minuend = left->words[index];

        left->words[index] = (uint32_t)(minuend - subtrahend);
        borrow = minuend < subtrahend ? 1U : 0U;
    }
    __tinypy_round_bigint_normalize(left);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_round_bigint_set_bit(tinypy_round_bigint_t *value, size_t bit) {
    size_t word = bit / 32U;

    while (value->count <= word) {
        value->words[value->count++] = 0U;
    }
    value->words[word] |= UINT32_C(1) << (bit % 32U);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_round_bigint_increment(tinypy_round_bigint_t *value) {
    size_t index = 0U;

    while (index < value->count && value->words[index] == UINT32_MAX) {
        value->words[index++] = 0U;
    }
    if (index == value->count) {
        if (value->count == TINYPY_ROUND_BIGINT_WORDS) {
            return TINYPY_FALSE;
        }
        value->words[value->count++] = 1U;
    }
    else {
        value->words[index] += 1U;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_round_bigint_divide(const tinypy_round_bigint_t *numerator, const tinypy_round_bigint_t *denominator, tinypy_round_bigint_t *quotient, tinypy_round_bigint_t *remainder) {
    size_t numerator_bits;
    size_t denominator_bits;
    size_t shift;

    (void)memset(quotient, 0, sizeof(*quotient));
    *remainder = *numerator;
    numerator_bits = __tinypy_round_bigint_bit_length(remainder);
    denominator_bits = __tinypy_round_bigint_bit_length(denominator);
    if (denominator_bits == 0U) {
        return TINYPY_FALSE;
    }
    if (numerator_bits < denominator_bits) {
        return TINYPY_TRUE;
    }
    shift = numerator_bits - denominator_bits;
    for (;;) {
        if (__tinypy_round_bigint_compare_shifted(remainder, denominator, shift) >= 0) {
            __tinypy_round_bigint_subtract_shifted(remainder, denominator, shift);
            __tinypy_round_bigint_set_bit(quotient, shift);
        }
        if (shift == 0U) {
            break;
        }
        shift -= 1U;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_round_bigint_divide_u32(tinypy_round_bigint_t *value, uint32_t divisor) {
    uint64_t remainder = 0U;
    size_t index = value->count;

    while (index != 0U) {
        uint64_t dividend;

        index -= 1U;
        dividend = (remainder << 32U) | value->words[index];
        value->words[index] = (uint32_t)(dividend / divisor);
        remainder = dividend % divisor;
    }
    __tinypy_round_bigint_normalize(value);
    return (uint32_t)remainder;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_round_bigint_decimal(const tinypy_round_bigint_t *value, char *digits, size_t capacity) {
    tinypy_round_bigint_t copy = *value;
    size_t position = capacity;

    if (copy.count == 0U) {
        digits[0] = '0';
        return 1U;
    }
    while (copy.count != 0U) {
        if (position == 0U) {
            return 0U;
        }
        digits[--position] = (char)('0' + __tinypy_round_bigint_divide_u32(&copy, 10U));
    }
    (void)memmove(digits, digits + position, capacity - position);
    return capacity - position;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_round_double(tinypy_vm_t *vm, double number, int32_t digits, double *out_result) {
    tinypy_round_bigint_t numerator;
    tinypy_round_bigint_t denominator;
    tinypy_round_bigint_t quotient;
    tinypy_round_bigint_t remainder;
    tinypy_round_bigint_t doubled_remainder;
    char decimal_digits[704];
    char decimal[704];
    double fraction;
    double result;
    uint64_t mantissa;
    size_t decimal_digit_count;
    size_t position = 0U;
    size_t index;
    int exponent;
    int32_t binary_shift;

    fraction = frexp(fabs(number), &exponent);
    mantissa = (uint64_t)ldexp(fraction, DBL_MANT_DIG);
    __tinypy_round_bigint_set_u64(&numerator, mantissa);
    __tinypy_round_bigint_set_u64(&denominator, UINT64_C(1));
    if (digits >= 0) {
        for (index = 0U; index < (size_t)digits; ++index) {
            if (__tinypy_round_bigint_multiply_u32(&numerator, 5U) == 0) {
                return TINYPY_FALSE;
            }
        }
        binary_shift = (int32_t)(exponent - DBL_MANT_DIG) + digits;
    }
    else {
        for (index = 0U; index < (size_t)(-digits); ++index) {
            if (__tinypy_round_bigint_multiply_u32(&denominator, 5U) == 0) {
                return TINYPY_FALSE;
            }
        }
        binary_shift = (int32_t)(exponent - DBL_MANT_DIG) + digits;
    }
    if (binary_shift >= 0) {
        if (__tinypy_round_bigint_shift_left(&numerator, (size_t)binary_shift) == 0) {
            return TINYPY_FALSE;
        }
    }
    else if (__tinypy_round_bigint_shift_left(&denominator, (size_t)(-binary_shift)) == 0) {
        return TINYPY_FALSE;
    }
    if (__tinypy_round_bigint_divide(&numerator, &denominator, &quotient, &remainder) == 0) {
        return TINYPY_FALSE;
    }
    doubled_remainder = remainder;
    if (__tinypy_round_bigint_shift_left(&doubled_remainder, 1U) == 0) {
        return TINYPY_FALSE;
    }
    if (__tinypy_round_bigint_compare_shifted(&doubled_remainder, &denominator, 0U) >= 0 && __tinypy_round_bigint_increment(&quotient) == 0) {
        return TINYPY_FALSE;
    }
    decimal_digit_count = __tinypy_round_bigint_decimal(&quotient, decimal_digits, sizeof(decimal_digits));
    if (decimal_digit_count == 0U) {
        return TINYPY_FALSE;
    }
    if (signbit(number) != 0) {
        decimal[position++] = '-';
    }
    if (digits > 0) {
        size_t fractional_digits = (size_t)digits;

        if (decimal_digit_count > fractional_digits) {
            size_t integer_digits = decimal_digit_count - fractional_digits;

            (void)memcpy(decimal + position, decimal_digits, integer_digits);
            position += integer_digits;
            decimal[position++] = '.';
            (void)memcpy(decimal + position, decimal_digits + integer_digits, fractional_digits);
            position += fractional_digits;
        }
        else {
            decimal[position++] = '0';
            decimal[position++] = '.';
            (void)memset(decimal + position, '0', fractional_digits - decimal_digit_count);
            position += fractional_digits - decimal_digit_count;
            (void)memcpy(decimal + position, decimal_digits, decimal_digit_count);
            position += decimal_digit_count;
        }
    }
    else {
        (void)memcpy(decimal + position, decimal_digits, decimal_digit_count);
        position += decimal_digit_count;
        if (digits < 0) {
            (void)memset(decimal + position, '0', (size_t)(-digits));
            position += (size_t)(-digits);
        }
    }
    if (position >= sizeof(decimal)) {
        return TINYPY_FALSE;
    }
    if (tinypy_internal_decimal_double(vm, decimal, position, &result) == 0 || isfinite(result) == 0) {
        return TINYPY_FALSE;
    }
    *out_result = result;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* The magnitude scaled by 10^digits is rounded half away from zero from its
   exact value; when that fits the 53-bit mantissa, one correctly rounded
   division by the exact power of ten gives the double of the decimal text. */
static tinypy_bool_t __tinypy_builtin_round_scaled(double number, int32_t digits, double *out_result) {
    if (digits <= 0) {
        return TINYPY_FALSE;
    }
    double magnitude = fabs(number);
    uint64_t scaled;
    int32_t half_order;
    if (tinypy_internal_double_scaled_integer(magnitude, (size_t)digits, &scaled, &half_order) == 0) {
        return TINYPY_FALSE;
    }
    if (scaled >= (UINT64_C(1) << DBL_MANT_DIG)) {
        return TINYPY_FALSE;
    }
    if (half_order >= 0) {
        scaled += 1U;
    }
    double power = 1.0;

    for (int32_t index = 0; index < digits; ++index) {
        power *= 10.0;
    }
    *out_result = copysign((double)scaled / power, number);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_round(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *const parameter_names[] = {vm->internal_number_key, vm->internal_ndigits_key};
    tinypy_value_t *arguments[2];
    tinypy_value_t *value;
    double number;
    int64_t digits = 0;
    double result;

    (void)user_data;
    if (__tinypy_builtin_named_arguments(vm, args, kwargs, parameter_names, 2U, 1U, arguments, out_error) == 0) {
        return NULL;
    }
    value = arguments[0];
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FLOAT) {
        number = TINYPY_FLOAT_OBJECT(value)->value;
    }
    else if ((TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_INTEGER) && tinypy_internal_object_has_special_override_key(value, vm->internal_special_float_key) == 0) {
        number = (double)TINYPY_INTEGER_VALUE(value);
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG && tinypy_internal_object_has_special_override_key(value, vm->internal_special_float_key) == 0) {
        if (tinypy_long_as_double(value, &number, out_error) == 0) {
            return NULL;
        }
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_object_has_special_key(value, vm->internal_special_float_key) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special_key(value, vm->internal_special_float_key, out_error);
        tinypy_value_t *empty_args;
        tinypy_value_t *converted;

        if (method == NULL) {
            return NULL;
        }
        empty_args = TINYPY_RET_EMPTY_TUPLE(vm);
        converted = tinypy_call(method, empty_args, NULL, out_error);
        TINYPY_DECREF(empty_args);
        TINYPY_DECREF(method);
        if (converted == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(converted) != TINYPY_VALUE_FLOAT) {
            TINYPY_DECREF(converted);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "nb_float should return float object", out_error);
            return NULL;
        }
        number = TINYPY_FLOAT_OBJECT(converted)->value;
        TINYPY_DECREF(converted);
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "a float is required", out_error);
        return NULL;
    }
    if (arguments[1] != NULL && tinypy_internal_index_as_i64(arguments[1], &digits, TINYPY_TRUE, out_error) == 0) {
        return NULL;
    }
    if (isfinite(number) == 0 || number == 0.0) {
        tinypy_value_t *return_value_1 = tinypy_float_from_double(vm, number);
        return return_value_1;
    }
    if (digits > (int64_t)((DBL_MANT_DIG - DBL_MIN_EXP) * 0.30103)) {
        tinypy_value_t *return_value_2 = tinypy_float_from_double(vm, number);
        return return_value_2;
    }
    if (digits < -(int64_t)((DBL_MAX_EXP + 1) * 0.30103)) {
        double signed_value = copysign(0.0, number);
        tinypy_value_t *return_value_3 = tinypy_float_from_double(vm, signed_value);
        return return_value_3;
    }
    if (digits == 0) {
        tinypy_value_t *return_value_4 = tinypy_float_from_double(vm, round(number));
        return return_value_4;
    }
    if (__tinypy_builtin_round_scaled(number, (int32_t)digits, &result) != 0) {
        tinypy_value_t *return_value_5 = tinypy_float_from_double(vm, result);
        return return_value_5;
    }
    if (__tinypy_builtin_round_double(vm, number, (int32_t)digits, &result) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "rounded value too large to represent", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_6 = tinypy_float_from_double(vm, result);
    return return_value_6;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_frame_dict(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *value;

    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    if (vm->current_frame == NULL) {
        value = vm->builtins;
    }
    else {
        value = user_data != NULL ? vm->current_frame->globals : tinypy_internal_frame_locals(vm->current_frame);
    }
    return TINYPY_RET(value);
}
//////////////////////////////////////////////////////////////////////////
/* Inserting a name may run the __eq__ of a colliding key, which can resize
   or release the dict being merged: the walk holds it and re-reads its table
   after every insertion. */
static tinypy_bool_t __tinypy_builtin_dir_add_dict(tinypy_value_t *names, tinypy_value_t *dict, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(names);
    tinypy_bool_t success = TINYPY_TRUE;

    TINYPY_INCREF(dict);
    for (size_t index = 0U; success != 0 && index <= TINYPY_DICT_OBJECT(dict)->mask; ++index) {
        tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(dict)->table[index];

        if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry) != 0) {
            tinypy_value_t *key = TINYPY_RET(entry->key);

            success = tinypy_internal_dict_set_hash_checked(vm, names, key, &vm->none_object.base, entry->hash, out_error);
            TINYPY_DECREF(key);
        }
    }
    TINYPY_DECREF(dict);
    return success;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_dir_optional(tinypy_value_t *value, tinypy_value_t *name) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_exception_state_t state;
    tinypy_error_t *error = NULL;
    tinypy_value_t *result;

    tinypy_internal_exception_preserve_begin(vm, &state);
    result = tinypy_object_get_attr_value(value, name, &error);
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_internal_exception_preserve_end(vm, &state);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_dir_add_class(tinypy_value_t *names, tinypy_value_t *class_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(names);
    tinypy_value_t *dictionary;
    tinypy_value_t *bases;
    tinypy_bool_t success = TINYPY_TRUE;

    if (tinypy_internal_recursion_check(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded while getting directory", out_error) == 0) {
        return TINYPY_FALSE;
    }
    vm->evaluation_depth += 1U;
    dictionary = __tinypy_builtin_dir_optional(class_value, vm->internal_special_dict_key);
    if (dictionary != NULL) {
        if (TINYPY_VALUE_KIND(dictionary) == TINYPY_VALUE_DICT) {
            success = __tinypy_builtin_dir_add_dict(names, dictionary, out_error);
        }
        else {
            tinypy_value_t *keys = tinypy_object_get_attr_value(dictionary, vm->internal_keys_key, out_error);

            success = keys != NULL ? tinypy_internal_dict_update_mapping(names, dictionary, keys, out_error) : TINYPY_FALSE;
            if (keys != NULL) {
                TINYPY_DECREF(keys);
            }
        }
        TINYPY_DECREF(dictionary);
        if (success == 0) {
            vm->evaluation_depth -= 1U;
            return TINYPY_FALSE;
        }
    }
    bases = __tinypy_builtin_dir_optional(class_value, vm->internal_special_bases_key);
    if (bases != NULL) {
        tinypy_internal_exception_state_t state;
        tinypy_error_t *error = NULL;
        tinypy_value_t *method;
        tinypy_value_t *length = NULL;
        int64_t size = -1;

        tinypy_internal_exception_preserve_begin(vm, &state);
        method = TINYPY_VALUE_KIND(bases) != TINYPY_VALUE_DICT ? tinypy_internal_object_get_special_key(bases, vm->internal_special_length_key, &error) : NULL;
        if (method != NULL) {
            tinypy_value_t *empty = TINYPY_RET_EMPTY_TUPLE(vm);

            length = tinypy_call(method, empty, NULL, &error);
            TINYPY_DECREF(empty);
            TINYPY_DECREF(method);
            if (length != NULL) {
                if (TINYPY_VALUE_KIND(bases) != TINYPY_VALUE_OLD_INSTANCE || TINYPY_VALUE_KIND(length) == TINYPY_VALUE_INTEGER || TINYPY_VALUE_KIND(length) == TINYPY_VALUE_BOOL) {
                    (void)tinypy_internal_number_as_ssize(length, &size, &error);
                }
                TINYPY_DECREF(length);
            }
        }
        if (error != NULL) {
            tinypy_error_release(error);
        }
        tinypy_internal_exception_preserve_end(vm, &state);
        for (int64_t index = 0; index < size; ++index) {
            tinypy_value_t *key = tinypy_integer_from_i64(vm, (int64_t)index);
            tinypy_value_t *base = tinypy_get_item(bases, key, out_error);

            TINYPY_DECREF(key);
            if (base == NULL) {
                success = TINYPY_FALSE;
                break;
            }
            success = __tinypy_builtin_dir_add_class(names, base, out_error);
            TINYPY_DECREF(base);
            if (success == 0) {
                break;
            }
        }
        TINYPY_DECREF(bases);
    }
    vm->evaluation_depth -= 1U;
    return success;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_dir_add_list(tinypy_value_t *names, tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(names);
    tinypy_value_t *list = __tinypy_builtin_dir_optional(value, name);
    tinypy_bool_t success = TINYPY_TRUE;

    if (list != NULL) {
        if (TINYPY_VALUE_KIND(list) == TINYPY_VALUE_LIST) {
            for (size_t index = 0U; success != 0 && index < TINYPY_LIST_SIZE(list); ++index) {
                tinypy_value_t *item = TINYPY_RET(TINYPY_LIST_GET(list, index));

                if (TINYPY_VALUE_KIND(item) == TINYPY_VALUE_STRING) {
                    success = tinypy_internal_dict_set_checked(vm, names, item, &vm->none_object.base, out_error);
                }
                TINYPY_DECREF(item);
            }
        }
        TINYPY_DECREF(list);
    }
    return success;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_dir_sort(tinypy_vm_t *vm, tinypy_value_t *result, tinypy_error_t **out_error) {
    tinypy_value_t *sort_function = tinypy_type_get_attr_key(&vm->types[TINYPY_VALUE_LIST], vm->internal_sort_key);
    tinypy_value_t *arguments = tinypy_tuple_from_items(vm, &result, 1U);
    tinypy_value_t *sort_result = tinypy_call(sort_function, arguments, NULL, out_error);

    TINYPY_DECREF(arguments);
    if (sort_result == NULL) {
        TINYPY_DECREF(result);
        return NULL;
    }
    TINYPY_DECREF(sort_result);
    return result;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_print_affix(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_NONE || kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "sep and end must be strings or None", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_print_affix_value(tinypy_vm_t *vm, tinypy_value_t *value, const char *default_bytes, size_t default_size, tinypy_bool_t unicode_default) {
    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_NONE) {
        return TINYPY_RET(value);
    }
    if (unicode_default != 0) {
        tinypy_value_t *return_value_1 = tinypy_unicode_from_utf8(vm, default_bytes, default_size);
        return return_value_1;
    }
    tinypy_value_t *return_value_2 = tinypy_string_from_bytes(vm, default_bytes, default_size);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_print(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *separator = &vm->none_object.base;
    tinypy_value_t *ending = &vm->none_object.base;
    tinypy_value_t *target = &vm->none_object.base;
    tinypy_value_t *separator_text = NULL;
    tinypy_value_t *ending_text = NULL;
    tinypy_bool_t unicode_default = TINYPY_FALSE;
    tinypy_value_t *result = NULL;
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    size_t index;

    (void)user_data;
    if (kwargs != NULL) {
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(kwargs);
        tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(kwargs);

        for (; iterator != iterator_end; ++iterator) {
            if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator) == 0) {
                continue;
            }
            if (TINYPY_NAME_EQ(iterator->key, vm->internal_sep_key) != 0) {
                separator = iterator->value;
            }
            else if (TINYPY_NAME_EQ(iterator->key, vm->internal_end_key) != 0) {
                ending = iterator->value;
            }
            else if (TINYPY_NAME_EQ(iterator->key, vm->internal_file_key) != 0) {
                target = iterator->value;
            }
            else {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "print() received an unexpected keyword argument", out_error);
                return NULL;
            }
        }
    }
    if (__tinypy_builtin_print_affix(vm, separator, out_error) == 0
        || __tinypy_builtin_print_affix(vm, ending, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(target) == TINYPY_VALUE_NONE) {
        tinypy_value_t *sys_dict = TINYPY_MODULE_OBJECT(vm->sys_module)->dict;

        target = tinypy_internal_dict_get_optional(vm, sys_dict, vm->internal_stdout_key);
        if (target == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "lost sys.stdout", out_error);
            return NULL;
        }
        if (TINYPY_VALUE_KIND(target) == TINYPY_VALUE_NONE) {
            tinypy_value_t *silent = TINYPY_RET_NONE(vm);
            return silent;
        }
    }
    TINYPY_INCREF(target);
    unicode_default = TINYPY_VALUE_KIND(separator) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(ending) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
    for (index = 0U; unicode_default == 0 && index < argument_count; ++index) {
        if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, index)) == TINYPY_VALUE_UNICODE) {
            unicode_default = TINYPY_TRUE;
        }
    }
    if (argument_count > 1U) {
        separator_text = __tinypy_builtin_print_affix_value(vm, separator, " ", 1U, unicode_default);
    }
    ending_text = __tinypy_builtin_print_affix_value(vm, ending, "\n", 1U, unicode_default);
    for (index = 0U; index < argument_count; ++index) {
        tinypy_value_t *text;
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, index);
        tinypy_value_type_e item_kind = TINYPY_VALUE_KIND(item);

        if (index != 0U && tinypy_internal_output_write_value(target, separator_text, out_error) == 0) {
            goto cleanup;
        }
        if (item_kind == TINYPY_VALUE_STRING || item_kind == TINYPY_VALUE_UNICODE) {
            text = TINYPY_RET(item);
        }
        else {
            text = tinypy_object_str(item, out_error);
            if (text == NULL) {
                goto cleanup;
            }
        }
        if (tinypy_internal_output_write_value(target, text, out_error) == 0) {
            TINYPY_DECREF(text);
            goto cleanup;
        }
        TINYPY_DECREF(text);
    }
    if (tinypy_internal_output_write_value(target, ending_text, out_error) != 0) {
        result = TINYPY_RET_NONE(vm);
    }
cleanup:
    if (ending_text != NULL) {
        TINYPY_DECREF(ending_text);
    }
    if (separator_text != NULL) {
        TINYPY_DECREF(separator_text);
    }
    TINYPY_DECREF(target);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_dir(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_dict_entry_t *iterator;
    tinypy_dict_entry_t *iterator_end;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 0U, 1U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 1U) {
        tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);

        tinypy_value_t *method = tinypy_internal_object_get_special_key(value, vm->internal_special_dir_key, out_error);

        if (method == NULL && TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
            if (tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_ATTRIBUTE_ERROR, out_error) == 0) {
                return NULL;
            }
        }
        else if (method == NULL && (tinypy_vm_has_error(vm) != 0 || (out_error != NULL && *out_error != NULL))) {
            return NULL;
        }
        if (method != NULL) {
            tinypy_value_t *empty;
            tinypy_value_t *result;

            empty = TINYPY_RET_EMPTY_TUPLE(vm);
            result = tinypy_call(method, empty, NULL, out_error);
            TINYPY_DECREF(empty);
            TINYPY_DECREF(method);
            if (result == NULL) {
                return NULL;
            }
            if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_LIST) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("__dir__() must return a list, not "),
                    {result->type->name, result->type->name_size}
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
                TINYPY_DECREF(result);
                return NULL;
            }
            tinypy_value_t *sorted = __tinypy_builtin_dir_sort(vm, result, out_error);

            return sorted;
        }
    }
    tinypy_value_t *names = tinypy_dict_new(vm);
    tinypy_bool_t merged = TINYPY_TRUE;
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        merged = __tinypy_builtin_dir_add_dict(names, vm->current_frame != NULL ? tinypy_internal_frame_locals(vm->current_frame) : vm->builtins, out_error);
    }
    else {
        tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
        if (kind == TINYPY_VALUE_MODULE) {
            tinypy_value_t *module_dict = tinypy_object_get_attr_value(value, vm->internal_special_dict_key, out_error);

            if (module_dict == NULL) {
                merged = TINYPY_FALSE;
            }
            else {
                if (TINYPY_VALUE_KIND(module_dict) == TINYPY_VALUE_DICT) {
                    merged = __tinypy_builtin_dir_add_dict(names, module_dict, out_error);
                }
                else {
                    tinypy_value_t *module_name = tinypy_module_dict(value) != NULL ? tinypy_module_get_value_key(value, vm->internal_special_name_key) : NULL;

                    if (module_name != NULL && TINYPY_VALUE_KIND(module_name) == TINYPY_VALUE_STRING) {
                        tinypy_message_part_t parts[] = {
                            {(const char *)TINYPY_TEXT_BYTES(module_name), TINYPY_TEXT_BYTE_SIZE(module_name)},
                            TINYPY_MESSAGE_PART_LITERAL(".__dict__ is not a dictionary")
                        };

                        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
                    }
                    else {
                        tinypy_internal_exception_raise_system_error(vm, "nameless module", out_error);
                    }
                    merged = TINYPY_FALSE;
                }
                TINYPY_DECREF(module_dict);
            }
        }
        else if (kind == TINYPY_VALUE_TYPE || kind == TINYPY_VALUE_CLASS) {
            merged = __tinypy_builtin_dir_add_class(names, value, out_error);
            if (merged != 0 && value == &vm->types[TINYPY_VALUE_TYPE].base.base) {
                tinypy_value_t *const type_metadata[] = {vm->internal_special_abstractmethods_key, vm->internal_special_base_key, vm->internal_special_bases_key, vm->internal_special_basicsize_key, vm->internal_special_dict_key, vm->internal_special_dictoffset_key, vm->internal_special_flags_key, vm->internal_special_itemsize_key, vm->internal_special_module_key, vm->internal_special_mro_key, vm->internal_special_name_key, vm->internal_special_weakrefoffset_key};
                size_t metadata_index;

                for (metadata_index = 0U; metadata_index < sizeof(type_metadata) / sizeof(type_metadata[0]); ++metadata_index) {
                    tinypy_dict_set(names, type_metadata[metadata_index], &vm->none_object.base);
                }
            }
        }
        else {
            tinypy_value_t *dictionary = __tinypy_builtin_dir_optional(value, vm->internal_special_dict_key);

            if (dictionary != NULL) {
                if (TINYPY_VALUE_KIND(dictionary) == TINYPY_VALUE_DICT) {
                    merged = __tinypy_builtin_dir_add_dict(names, dictionary, out_error);
                }
                TINYPY_DECREF(dictionary);
            }
            if (merged != 0) {
                merged = __tinypy_builtin_dir_add_list(names, value, vm->internal_special_members_key, out_error);
            }
            if (merged != 0) {
                merged = __tinypy_builtin_dir_add_list(names, value, vm->internal_special_methods_key, out_error);
            }
            if (merged != 0) {
                tinypy_value_t *reported_class = __tinypy_builtin_dir_optional(value, vm->internal_special_class_key);

                if (reported_class != NULL) {
                    merged = __tinypy_builtin_dir_add_class(names, reported_class, out_error);
                    TINYPY_DECREF(reported_class);
                }
            }
        }
    }
    if (merged == 0) {
        TINYPY_DECREF(names);
        return NULL;
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    if (tinypy_internal_list_reserve_checked(vm, result, TINYPY_DICT_SIZE(names), out_error) == 0) {
        TINYPY_DECREF(result);
        TINYPY_DECREF(names);
        return NULL;
    }
    iterator = TINYPY_DICT_ITERATOR_BEGIN(names);
    iterator_end = TINYPY_DICT_ITERATOR_END(names);
    for (; iterator != iterator_end; ++iterator) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
            if (tinypy_internal_list_append_checked(result, iterator->key, out_error) == 0) {
                TINYPY_DECREF(result);
                TINYPY_DECREF(names);
                return NULL;
            }
        }
    }
    TINYPY_DECREF(names);
    tinypy_value_t *sorted = __tinypy_builtin_dir_sort(vm, result, out_error);

    return sorted;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_import_name(tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(name);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(name);
    tinypy_bool_t text = kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE;
    tinypy_bool_t has_nul = text != TINYPY_FALSE && memchr(TINYPY_TEXT_BYTES(name), 0, TINYPY_TEXT_BYTE_SIZE(name)) != NULL;

    if (kind == TINYPY_VALUE_UNICODE && tinypy_internal_codecs_validate_name(vm, name, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    if (text == TINYPY_FALSE || has_nul != TINYPY_FALSE) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("__import__() argument 1 must be string"),
            {has_nul != TINYPY_FALSE ? " without null bytes, not " : ", not ", has_nul != TINYPY_FALSE ? 25U : 6U},
            {kind == TINYPY_VALUE_NONE ? "None" : name->type->name, kind == TINYPY_VALUE_NONE ? 4U : name->type->name_size},
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_import(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *const parameter_names[] = {vm->internal_name_key, vm->internal_globals_key, vm->internal_locals_key, vm->internal_fromlist_key, vm->internal_level_key};
    tinypy_value_t *arguments[5] = {NULL, NULL, NULL, NULL, NULL};
    size_t positional_count = TINYPY_TUPLE_SIZE(args);
    size_t keyword_count = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    size_t recognized_keyword_count = 0U;
    size_t index;
    const char *name_bytes;
    size_t name_size;
    tinypy_value_t *globals = NULL;
    tinypy_value_t *fromlist = NULL;
    int64_t level = -1;
    tinypy_value_t *result = NULL;

    (void)user_data;
    if (positional_count > 5U || keyword_count > 5U - positional_count) {
        tinypy_internal_make_arity_error(vm, "__import__", 10U, positional_count + keyword_count, 0U, 5U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }
    for (index = 0U; index < 5U; index += 1U) {
        tinypy_value_t *value = index < positional_count ? TINYPY_TUPLE_GET(args, index) : NULL;
        tinypy_value_t *keyword_value = recognized_keyword_count < keyword_count
            ? tinypy_internal_constructor_keyword_optional(kwargs, parameter_names[index]) : NULL;

        if (keyword_value != NULL) {
            recognized_keyword_count += 1U;
            if (value != NULL) {
                char position = (char)('1' + index);
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("Argument given by name ('"),
                    TINYPY_MESSAGE_PART_TEXT(parameter_names[index]),
                    TINYPY_MESSAGE_PART_LITERAL("') and position ("),
                    {&position, 1U},
                    TINYPY_MESSAGE_PART_LITERAL(")"),
                };
                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 5U, out_error);
                goto complete;
            }
            value = keyword_value;
        }
        if (value == NULL) {
            if (index == 0U) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "Required argument 'name' (pos 1) not found", out_error);
                goto complete;
            }
            if (recognized_keyword_count == keyword_count) {
                break;
            }
            continue;
        }
        arguments[index] = TINYPY_RET(value);
        if (index == 0U && __tinypy_builtin_import_name(value, out_error) == TINYPY_FALSE) {
            goto complete;
        }
        if (index == 4U) {
            if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FLOAT) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument expected, got float", out_error);
                goto complete;
            }
            if (tinypy_internal_number_as_i64(value, &level, out_error) == TINYPY_FALSE) {
                goto complete;
            }
            if (level < INT32_MIN || level > INT32_MAX) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, level < INT32_MIN ? "signed integer is less than minimum" : "signed integer is greater than maximum", out_error);
                goto complete;
            }
        }
    }
    if (recognized_keyword_count != keyword_count) {
        tinypy_dict_object_t *dictionary = TINYPY_DICT_OBJECT(kwargs);
        for (size_t slot = 0U; slot <= dictionary->mask; slot += 1U) {
            tinypy_dict_entry_t *entry = &dictionary->table[slot];
            tinypy_bool_t known = TINYPY_FALSE;

            if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry) == TINYPY_FALSE) {
                continue;
            }
            for (index = 0U; index < 5U; index += 1U) {
                if (TINYPY_NAME_EQ(entry->key, parameter_names[index]) != TINYPY_FALSE) {
                    known = TINYPY_TRUE;
                    break;
                }
            }
            if (known == TINYPY_FALSE) {
                size_t keyword_size = TINYPY_TEXT_BYTE_SIZE(entry->key);
                const char *keyword_bytes = (const char *)TINYPY_TEXT_BYTES(entry->key);
                const char *nul = (const char *)memchr(keyword_bytes, 0, keyword_size);
                if (nul != NULL) {
                    keyword_size = (size_t)(nul - keyword_bytes);
                }
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("'"),
                    {keyword_bytes, keyword_size < 200U ? keyword_size : 200U},
                    TINYPY_MESSAGE_PART_LITERAL("' is an invalid keyword argument for this function"),
                };
                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
                goto complete;
            }
        }
    }
    if (arguments[1] != NULL && TINYPY_VALUE_KIND(arguments[1]) != TINYPY_VALUE_NONE) {
        globals = arguments[1];
    }
    if (arguments[3] != NULL) {
        fromlist = arguments[3];
    }
    (void)__tinypy_builtin_text_view(vm, arguments[0], &name_bytes, &name_size, out_error);
    tinypy_value_t *module_key = tinypy_internal_name_from_bytes(vm, name_bytes, name_size);
    result = tinypy_import_module_key(module_key, globals, fromlist, (int32_t)level, out_error);

    TINYPY_DECREF(module_key);
complete:
    for (index = 0U; index < 5U; index += 1U) {
        if (arguments[index] != NULL) {
            TINYPY_DECREF(arguments[index]);
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_reload(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_reload_module(TINYPY_TUPLE_GET(args, 0U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_abs(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_number_absolute(TINYPY_TUPLE_GET(args, 0U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_ord(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const uint8_t *bytes;
    size_t byte_size;
    size_t length;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_BYTEARRAY) {
        (void)tinypy_internal_bytes_view(value, &bytes, &byte_size);
        length = byte_size;
    }
    else if (kind == TINYPY_VALUE_UNICODE) {
        bytes = (const uint8_t *)tinypy_unicode_utf8_view(value, &byte_size, &length);
    }
    else {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("ord() expected string of length 1, but "),
            TINYPY_MESSAGE_PART_TYPE_NAME(value),
            TINYPY_MESSAGE_PART_LITERAL(" found")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (length != 1U) {
        char length_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
        size_t length_digits = tinypy_internal_format_size(length_buffer, length);
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("ord() expected a character, but string of length "),
            {length_buffer, length_digits},
            TINYPY_MESSAGE_PART_LITERAL(" found")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    uint32_t code_point = bytes[0];
    if (kind == TINYPY_VALUE_UNICODE) {
        (void)tinypy_internal_utf8_decode(bytes, byte_size, &code_point);
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)code_point);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_format(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *format_spec = NULL;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        format_spec = TINYPY_TUPLE_GET(args, 1U);

        if (TINYPY_VALUE_KIND(format_spec) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(format_spec) != TINYPY_VALUE_UNICODE) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("format expects arg 2 to be string or unicode, not "),
                TINYPY_MESSAGE_PART_TYPE_NAME(format_spec)
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
            return NULL;
        }
    }
    tinypy_value_t *return_value_1 = tinypy_internal_string_format_object(vm, TINYPY_TUPLE_GET(args, 0U), format_spec, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_call_no_arguments(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *method = tinypy_internal_object_get_special_key(value, name, out_error);

    if (method == NULL) {
        return NULL;
    }
    tinypy_value_t *args = TINYPY_RET_EMPTY_TUPLE(vm);
    tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_base_repr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t base = (intptr_t)user_data;
    tinypy_value_t *method_name = base == 16 ? vm->internal_special_hex_key : vm->internal_special_oct_key;
    tinypy_value_t *value;
    tinypy_value_t *integer;
    tinypy_value_t *formatted;
    tinypy_bool_t result_unicode;
    const uint8_t *spec = base == 2 ? (const uint8_t *)"#b" : (base == 8 ? (const uint8_t *)"#o" : (const uint8_t *)"#x");

    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    value = TINYPY_TUPLE_GET(args, 0U);
    /* instance_hex and instance_oct look the method up like any attribute. */
    tinypy_bool_t classic = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE ? TINYPY_TRUE : TINYPY_FALSE;
    if (base != 2 && (classic != 0 || (value->type != &vm->types[TINYPY_VALUE_BOOL] && value->type != &vm->types[TINYPY_VALUE_INTEGER] && value->type != &vm->types[TINYPY_VALUE_LONG] && tinypy_internal_object_has_special_key(value, method_name) != 0))) {
        tinypy_value_t *special = __tinypy_builtin_call_no_arguments(value, method_name, out_error);

        if (special == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(special) != TINYPY_VALUE_STRING) {
            const tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_TEXT(method_name),
                TINYPY_MESSAGE_PART_LITERAL(" returned non-string (type "),
                TINYPY_MESSAGE_PART_TYPE_NAME(special),
                TINYPY_MESSAGE_PART_LITERAL(")"),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            TINYPY_DECREF(special);
            return NULL;
        }
        return special;
    }
    tinypy_value_type_e value_kind = TINYPY_VALUE_KIND(value);
    if (base != 2 && value_kind != TINYPY_VALUE_BOOL && value_kind != TINYPY_VALUE_INTEGER && value_kind != TINYPY_VALUE_LONG) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, base == 16 ? "hex() argument can't be converted to hex" : "oct() argument can't be converted to oct", out_error);
        return NULL;
    }
    integer = tinypy_internal_index_value(value, out_error);
    if (integer == NULL) {
        return NULL;
    }
    formatted = tinypy_internal_string_format_builtin_value(vm, integer, 0, spec, 2U, TINYPY_FALSE, &result_unicode, out_error);
    if (formatted == NULL) {
        TINYPY_DECREF(integer);
        return NULL;
    }
    tinypy_bool_t long_suffix = base != 2 && TINYPY_VALUE_KIND(integer) == TINYPY_VALUE_LONG ? TINYPY_TRUE : TINYPY_FALSE;
    TINYPY_DECREF(integer);
    if (base != 8 && long_suffix == 0) {
        return formatted;
    }
    const uint8_t *bytes = TINYPY_TEXT_BYTES(formatted);
    size_t size = TINYPY_TEXT_BYTE_SIZE(formatted);
    size_t prefix = size >= 3U && bytes[0] == (uint8_t)'-' ? 1U : 0U;
    tinypy_bool_t octal_zero = base == 8 && size - prefix == 3U && bytes[prefix] == (uint8_t)'0' && bytes[prefix + 1U] == (uint8_t)'o' && bytes[prefix + 2U] == (uint8_t)'0' ? TINYPY_TRUE : TINYPY_FALSE;
    size_t output_size = size + (long_suffix != 0 ? 1U : 0U) - (base == 8 ? 1U : 0U) - (octal_zero != 0 ? 1U : 0U);
    uint8_t *output;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, output_size, output_size, &output, out_error);
    size_t output_index = 0U;

    if (result == NULL) {
        TINYPY_DECREF(formatted);
        return NULL;
    }

    if (prefix != 0U) {
        output[output_index++] = (uint8_t)'-';
    }
    if (base == 8) {
        output[output_index++] = (uint8_t)'0';
        if (octal_zero == 0) {
            (void)memcpy(output + output_index, bytes + prefix + 2U, size - prefix - 2U);
            output_index += size - prefix - 2U;
        }
    }
    else {
        (void)memcpy(output + output_index, bytes + prefix, size - prefix);
        output_index += size - prefix;
    }
    if (long_suffix != 0) {
        output[output_index++] = (uint8_t)'L';
    }
    TINYPY_DECREF(formatted);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_vars(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 0U, 1U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_value_t *value = vm->current_frame != NULL ? tinypy_internal_frame_locals(vm->current_frame) : vm->builtins;

        return TINYPY_RET(value);
    }
    tinypy_value_t *target = TINYPY_TUPLE_GET(args, 0U);

    tinypy_value_t *result = tinypy_object_get_attr_value(target, vm->internal_special_dict_key, out_error);

    if (result == NULL) {
        if (out_error != NULL && *out_error != NULL) {
            tinypy_error_release(*out_error);
            *out_error = NULL;
        }
        tinypy_vm_clear_error(vm);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "vars() argument must have __dict__ attribute", out_error);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_intern(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_STRING) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("intern() argument 1 must be string, not "),
            TINYPY_MESSAGE_PART_TYPE_NAME(value)
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (value->type != &vm->types[TINYPY_VALUE_STRING]) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "can't intern subclass of string", out_error);
        return NULL;
    }
    TINYPY_INCREF(value);
    if (tinypy_internal_string_intern(&value, out_error) == 0) {
        TINYPY_DECREF(value);
        return NULL;
    }
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_repr(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = tinypy_object_repr(item, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_compile_string(tinypy_value_t *value, const char *argument, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_value_t *encoded;

    if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
        tinypy_message_part_t type_name = TINYPY_MESSAGE_PART_TYPE_NAME(value);
        if (kind == TINYPY_VALUE_NONE) {
            type_name.bytes = "None";
            type_name.size = 4U;
        }
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("compile() argument "),
            {argument, 1U},
            TINYPY_MESSAGE_PART_LITERAL(" must be string, not "),
            type_name,
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    encoded = tinypy_internal_object_encode_attribute_name(value, out_error);
    if (encoded == NULL) {
        return NULL;
    }
    size_t size;
    const uint8_t *bytes = tinypy_string_view(encoded, &size);
    if (size != 0U && memchr(bytes, '\0', size) != NULL) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("compile() argument "),
            {argument, 1U},
            TINYPY_MESSAGE_PART_LITERAL(" must be string without null bytes, not "),
            TINYPY_MESSAGE_PART_TYPE_NAME(value),
        };
        TINYPY_DECREF(encoded);
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    return encoded;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_compile_mode(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_compile_mode_e *out_mode, tinypy_error_t **out_error) {
    const char *bytes;
    size_t size;

    if (__tinypy_builtin_text_view(vm, value, &bytes, &size, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (TINYPY_NAME_EQ(value, vm->internal_exec_mode_key) != TINYPY_FALSE) {
        *out_mode = TINYPY_COMPILE_EXEC;
    }
    else if (TINYPY_NAME_EQ(value, vm->internal_eval_key) != TINYPY_FALSE) {
        *out_mode = TINYPY_COMPILE_EVAL;
    }
    else if (TINYPY_NAME_EQ(value, vm->internal_single_mode_key) != TINYPY_FALSE) {
        *out_mode = TINYPY_COMPILE_SINGLE;
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "compile() arg 3 must be 'exec', 'eval' or 'single'", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_compile_integer(tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_value_t *converted = value;
    int64_t integer;

    if (kind == TINYPY_VALUE_FLOAT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument expected, got float", out_error);
        return TINYPY_FALSE;
    }
    if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && value->type != &vm->types[TINYPY_VALUE_LONG]) {
        if (tinypy_internal_object_has_special_key(value, vm->internal_special_int_key) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "an integer is required", out_error);
            return TINYPY_FALSE;
        }
        tinypy_value_t *method = tinypy_internal_object_get_special_key(value, vm->internal_special_int_key, out_error);
        if (method == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_value_t *empty = TINYPY_RET_EMPTY_TUPLE(vm);
        converted = tinypy_call(method, empty, NULL, out_error);
        TINYPY_DECREF(empty);
        TINYPY_DECREF(method);
        if (converted == NULL) {
            return TINYPY_FALSE;
        }
        kind = TINYPY_VALUE_KIND(converted);
        if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__int__ method should return an integer", out_error);
            TINYPY_DECREF(converted);
            return TINYPY_FALSE;
        }
    }
    else {
        TINYPY_INCREF(converted);
    }
    if (kind == TINYPY_VALUE_LONG) {
        tinypy_error_t *conversion_error = NULL;
        if (tinypy_internal_index_as_i64(converted, &integer, TINYPY_FALSE, &conversion_error) == 0) {
            tinypy_error_release(conversion_error);
            TINYPY_DECREF(converted);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "Python int too large to convert to C long", out_error);
            return TINYPY_FALSE;
        }
    }
    if (kind != TINYPY_VALUE_LONG) {
        integer = tinypy_integer_as_i64(converted);
    }
    TINYPY_DECREF(converted);
    if (integer < INT32_MIN || integer > INT32_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, integer < INT32_MIN ? "signed integer is less than minimum" : "signed integer is greater than maximum", out_error);
        return TINYPY_FALSE;
    }
    *out_value = integer;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_source_view(tinypy_vm_t *vm, tinypy_value_t *value, const void **out_source, size_t *out_size, tinypy_bool_t *out_unicode, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        *out_source = tinypy_string_view(value, out_size);
        *out_unicode = 0;
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        size_t code_points;

        *out_source = tinypy_unicode_utf8_view(value, out_size, &code_points);
        *out_unicode = 1;
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BYTEARRAY || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BUFFER) {
        const uint8_t *bytes;
        if (tinypy_internal_bytes_view(value, &bytes, out_size) != 0) {
            *out_source = bytes;
            *out_unicode = 0;
            return TINYPY_TRUE;
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected a readable buffer object", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_compile(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *const parameter_names[] = {vm->internal_source_key, vm->internal_filename_key, vm->internal_mode_key, vm->internal_flags_key, vm->internal_dont_inherit_key};
    tinypy_value_t *arguments[5] = {NULL, NULL, NULL, NULL, NULL};
    const void *source;
    size_t source_size;
    const char *filename;
    size_t filename_size;
    tinypy_compile_mode_e mode;
    tinypy_compile_options_t options;
    int64_t flags = 0;
    int64_t dont_inherit = 0;
    tinypy_bool_t source_is_unicode;
    tinypy_value_t *filename_value;
    tinypy_value_t *mode_value;
    tinypy_value_t *result = NULL;
    const uint32_t obsolete_flags = UINT32_C(0x10);
    const uint32_t supported_flags = (uint32_t)(TINYPY_COMPILE_FLAG_DONT_IMPLY_DEDENT | TINYPY_COMPILE_FLAG_FUTURE_DIVISION | TINYPY_COMPILE_FLAG_FUTURE_ABSOLUTE_IMPORT | TINYPY_COMPILE_FLAG_FUTURE_WITH_STATEMENT | TINYPY_COMPILE_FLAG_FUTURE_PRINT_FUNCTION | TINYPY_COMPILE_FLAG_FUTURE_UNICODE_LITERALS);

    (void)user_data;
    if (__tinypy_builtin_named_arguments(vm, args, kwargs, parameter_names, 5U, 3U, arguments, out_error) == 0) {
        return NULL;
    }
    filename_value = __tinypy_builtin_compile_string(arguments[1], "2", out_error);
    if (filename_value == NULL) {
        return NULL;
    }
    mode_value = __tinypy_builtin_compile_string(arguments[2], "3", out_error);
    if (mode_value == NULL) {
        TINYPY_DECREF(filename_value);
        return NULL;
    }
    filename = (const char *)tinypy_string_view(filename_value, &filename_size);
    if (arguments[3] != NULL && __tinypy_builtin_compile_integer(arguments[3], &flags, out_error) == 0) {
        goto done;
    }
    if (arguments[4] != NULL && __tinypy_builtin_compile_integer(arguments[4], &dont_inherit, out_error) == 0) {
        goto done;
    }
    if (__tinypy_builtin_compile_mode(vm, mode_value, &mode, out_error) == 0) {
        goto done;
    }
    if (flags < 0 || (uint64_t)flags > UINT32_MAX || ((uint32_t)flags & ~(supported_flags | obsolete_flags)) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "compile(): unrecognised flags", out_error);
        goto done;
    }
    if (__tinypy_builtin_source_view(vm, arguments[0], &source, &source_size, &source_is_unicode, out_error) == 0) {
        goto done;
    }
    if (source_size != 0U && memchr(source, '\0', source_size) != NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "compile() expected string without null bytes", out_error);
        goto done;
    }
    tinypy_compile_options_init(&options, mode);
    if (tinypy_internal_compile_options_inherit_frame(vm, &options) == 0) {
        options.optimize_level = vm->optimize_level;
    }
    options.flags = (uint32_t)flags & supported_flags;
    options.dont_inherit = dont_inherit != 0 ? 1 : 0;
    result = tinypy_internal_compiler_compile_source(vm, source, source_size, source_is_unicode, source_is_unicode == 0 ? TINYPY_TRUE : TINYPY_FALSE, filename, filename_size, &options, out_error);
done:
    TINYPY_DECREF(mode_value);
    TINYPY_DECREF(filename_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_builtin_ensure_builtins(tinypy_vm_t *vm, tinypy_value_t *globals, tinypy_error_t **out_error) {
    if (tinypy_internal_dict_get_optional_suppressed(vm, globals, vm->internal_builtins_key) == NULL) {
        tinypy_value_t *builtins = vm->current_frame != NULL ? vm->current_frame->builtins : vm->builtins;

        tinypy_bool_t result = tinypy_internal_dict_set_checked(vm, globals, vm->internal_builtins_key, builtins, out_error);

        return result;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_builtin_eval(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *locals;

    (void)user_data;
    if (__tinypy_builtin_no_keywords(function, kwargs, out_error) == 0 || __tinypy_builtin_argument_count(function, args, 1U, 3U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *source = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *globals = TINYPY_TUPLE_SIZE(args) >= 2U ? TINYPY_TUPLE_GET(args, 1U) : NULL;
    locals = TINYPY_TUPLE_SIZE(args) >= 3U ? TINYPY_TUPLE_GET(args, 2U) : NULL;
    if (globals != NULL && TINYPY_VALUE_KIND(globals) == TINYPY_VALUE_NONE) {
        globals = NULL;
    }
    if (locals != NULL && TINYPY_VALUE_KIND(locals) == TINYPY_VALUE_NONE) {
        locals = NULL;
    }
    if (locals != NULL && tinypy_internal_object_is_mapping(vm, locals) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "locals must be a mapping", out_error);
        return NULL;
    }
    if (globals != NULL && TINYPY_VALUE_KIND(globals) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, tinypy_internal_object_is_mapping(vm, globals) != 0 ? "globals must be a real dict; try eval(expr, {}, mapping)" : "globals must be a dict", out_error);
        return NULL;
    }
    if (globals == NULL) {
        globals = vm->current_frame != NULL ? vm->current_frame->globals : vm->builtins;
        if (locals == NULL) {
            locals = vm->current_frame != NULL ? tinypy_internal_frame_locals(vm->current_frame) : globals;
        }
    }
    else if (locals == NULL) {
        locals = globals;
    }
    if (globals != vm->builtins) {
        if (__tinypy_builtin_ensure_builtins(vm, globals, out_error) == 0) {
            return NULL;
        }
    }
    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_CODE) {
        if (TINYPY_TUPLE_SIZE(TINYPY_CODE_FREEVARS(source)) != 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "code object passed to eval() may not contain free variables", out_error);
            return NULL;
        }
        tinypy_value_t *return_value_1 = tinypy_eval_code(source, globals, locals, out_error);
        return return_value_1;
    }
    const void *source_bytes;
    size_t source_size;
    tinypy_bool_t source_is_unicode;
    tinypy_compile_options_t options;
    tinypy_value_t *code;
    tinypy_value_t *result;

    if (TINYPY_VALUE_KIND(source) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(source) != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "eval() arg 1 must be a string or code object", out_error);
        return NULL;
    }
    if (__tinypy_builtin_source_view(vm, source, &source_bytes, &source_size, &source_is_unicode, out_error) == 0) {
        return NULL;
    }
    if (source_size != 0U && memchr(source_bytes, '\0', source_size) != NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected string without null bytes", out_error);
        return NULL;
    }
    while (source_size != 0U && (*(const uint8_t *)source_bytes == (uint8_t)' ' || *(const uint8_t *)source_bytes == (uint8_t)'\t')) {
        source_bytes = (const uint8_t *)source_bytes + 1U;
        source_size -= 1U;
    }
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EVAL);
    if (tinypy_internal_compile_options_inherit_frame(vm, &options) == 0) {
        options.optimize_level = vm->optimize_level;
    }
    options.dont_inherit = 0;
    code = tinypy_internal_compiler_compile_source(vm, source_bytes, source_size, source_is_unicode, source_is_unicode == 0 ? TINYPY_TRUE : TINYPY_FALSE, "<string>", 8U, &options, out_error);
    if (code == NULL) {
        return NULL;
    }
    result = tinypy_eval_code(code, globals, locals, out_error);
    TINYPY_DECREF(code);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_builtin_register_with_user_data(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new_key(name, callback, user_data, NULL);
    TINYPY_NATIVE_FUNCTION_OBJECT(function)->module = TINYPY_RET(vm->internal_builtin_module_name);

    tinypy_dict_set(vm->builtins, name, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_builtin_register(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_native_function_callback_t callback) {
    __tinypy_builtin_register_with_user_data(vm, name, callback, NULL);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_builtin_functions(tinypy_vm_t *vm) {
    __tinypy_builtin_register(vm, vm->internal_apply_key, __tinypy_builtin_apply);
    __tinypy_builtin_register(vm, vm->internal_print_key, __tinypy_builtin_print);
    __tinypy_builtin_register(vm, vm->internal_len_key, __tinypy_builtin_len);
    __tinypy_builtin_register(vm, vm->internal_id_key, __tinypy_builtin_id);
    __tinypy_builtin_register(vm, vm->internal_isinstance_key, __tinypy_builtin_isinstance);
    __tinypy_builtin_register(vm, vm->internal_issubclass_key, __tinypy_builtin_issubclass);
    __tinypy_builtin_register(vm, vm->internal_callable_key, __tinypy_builtin_callable);
    __tinypy_builtin_register(vm, vm->internal_getattr_key, __tinypy_builtin_getattr);
    __tinypy_builtin_register(vm, vm->internal_hasattr_key, __tinypy_builtin_hasattr);
    __tinypy_builtin_register(vm, vm->internal_setattr_key, __tinypy_builtin_setattr);
    __tinypy_builtin_register(vm, vm->internal_delattr_key, __tinypy_builtin_delattr);
    __tinypy_builtin_register(vm, vm->internal_iter_key, __tinypy_builtin_iter);
    __tinypy_builtin_register(vm, vm->internal_special_next_key, __tinypy_builtin_next);
    __tinypy_builtin_register(vm, vm->internal_range_key, __tinypy_builtin_range);
    __tinypy_builtin_register(vm, vm->internal_sorted_key, __tinypy_builtin_sorted);
    __tinypy_builtin_register_with_user_data(vm, vm->internal_all_key, __tinypy_builtin_all_any, (void *)(intptr_t)1);
    __tinypy_builtin_register(vm, vm->internal_any_key, __tinypy_builtin_all_any);
    __tinypy_builtin_register(vm, vm->internal_divmod_key, __tinypy_builtin_divmod);
    __tinypy_builtin_register(vm, vm->internal_filter_key, __tinypy_builtin_filter);
    __tinypy_builtin_register(vm, vm->internal_map_key, __tinypy_builtin_map);
    __tinypy_builtin_register(vm, vm->internal_zip_key, __tinypy_builtin_zip);
    __tinypy_builtin_register(vm, vm->internal_sum_key, __tinypy_builtin_sum);
    __tinypy_builtin_register_with_user_data(vm, vm->internal_max_key, __tinypy_builtin_min_max, (void *)(intptr_t)1);
    __tinypy_builtin_register(vm, vm->internal_min_key, __tinypy_builtin_min_max);
    __tinypy_builtin_register(vm, vm->internal_chr_key, __tinypy_builtin_chr_common);
    __tinypy_builtin_register_with_user_data(vm, vm->internal_unichr_key, __tinypy_builtin_chr_common, (void *)(intptr_t)1);
    __tinypy_builtin_register(vm, vm->internal_cmp_key, __tinypy_builtin_cmp);
    __tinypy_builtin_register(vm, vm->internal_coerce_key, __tinypy_builtin_coerce);
    __tinypy_builtin_register(vm, vm->internal_hash_key, __tinypy_builtin_hash);
    __tinypy_builtin_register(vm, vm->internal_pow_key, __tinypy_builtin_pow);
    __tinypy_builtin_register(vm, vm->internal_round_key, __tinypy_builtin_round);
    __tinypy_builtin_register_with_user_data(vm, vm->internal_globals_key, __tinypy_builtin_frame_dict, (void *)(intptr_t)1);
    __tinypy_builtin_register(vm, vm->internal_locals_key, __tinypy_builtin_frame_dict);
    __tinypy_builtin_register(vm, vm->internal_dir_key, __tinypy_builtin_dir);
    __tinypy_builtin_register(vm, vm->internal_import_key, __tinypy_builtin_import);
    __tinypy_builtin_register(vm, vm->internal_reload_key, __tinypy_builtin_reload);
    __tinypy_builtin_register(vm, vm->internal_abs_key, __tinypy_builtin_abs);
    __tinypy_builtin_register(vm, vm->internal_ord_key, __tinypy_builtin_ord);
    __tinypy_builtin_register(vm, vm->internal_format_key, __tinypy_builtin_format);
    __tinypy_builtin_register_with_user_data(vm, vm->internal_bin_key, __tinypy_builtin_base_repr, (void *)(intptr_t)2);
    __tinypy_builtin_register_with_user_data(vm, vm->internal_oct_key, __tinypy_builtin_base_repr, (void *)(intptr_t)8);
    __tinypy_builtin_register_with_user_data(vm, vm->internal_hex_key, __tinypy_builtin_base_repr, (void *)(intptr_t)16);
    __tinypy_builtin_register(vm, vm->internal_vars_key, __tinypy_builtin_vars);
    __tinypy_builtin_register(vm, vm->internal_intern_key, __tinypy_builtin_intern);
    __tinypy_builtin_register(vm, vm->internal_reduce_key, tinypy_internal_functools_reduce);
    __tinypy_builtin_register(vm, vm->internal_repr_key, __tinypy_builtin_repr);
    __tinypy_builtin_register(vm, vm->internal_compile_key, __tinypy_builtin_compile);
    __tinypy_builtin_register(vm, vm->internal_eval_key, __tinypy_builtin_eval);
}
