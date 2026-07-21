#include "tinypy/eval.h"
#include "tinypy/compiler.h"

#include "bytecode_verify.h"
#include "internal.h"
#include "api_internal.h"

#include <assert.h>
#include <string.h>

typedef enum tinypy_eval_reason_e {
    TINYPY_EVAL_REASON_NOT = 0x0001,
    TINYPY_EVAL_REASON_EXCEPTION = 0x0002,
    TINYPY_EVAL_REASON_RERAISE = 0x0004,
    TINYPY_EVAL_REASON_RETURN = 0x0008,
    TINYPY_EVAL_REASON_BREAK = 0x0010,
    TINYPY_EVAL_REASON_CONTINUE = 0x0020,
    TINYPY_EVAL_REASON_YIELD = 0x0040
} tinypy_eval_reason_e;
//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_eval_integer_binary_e
{
    TINYPY_EVAL_INTEGER_BINARY_NONE = 0,
    TINYPY_EVAL_INTEGER_BINARY_ADD,
    TINYPY_EVAL_INTEGER_BINARY_SUBTRACT,
    TINYPY_EVAL_INTEGER_BINARY_MULTIPLY,
    TINYPY_EVAL_INTEGER_BINARY_LEFT_SHIFT,
    TINYPY_EVAL_INTEGER_BINARY_RIGHT_SHIFT,
    TINYPY_EVAL_INTEGER_BINARY_AND,
    TINYPY_EVAL_INTEGER_BINARY_XOR,
    TINYPY_EVAL_INTEGER_BINARY_OR
} tinypy_eval_integer_binary_e;

static int __tinypy_eval_push_block(tinypy_frame_object_t *frame, int32_t type, size_t handler);

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_eval_stack_depth(const tinypy_frame_object_t *frame) {
    return (size_t)(frame->stack_top - frame->value_stack);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_push_owned(tinypy_frame_object_t *frame, tinypy_value_t *value) {
    assert(value != NULL);
    assert(__tinypy_eval_stack_depth(frame) < (size_t)TINYPY_CODE_STACK_SIZE(frame->code));
    *frame->stack_top = value;
    frame->stack_top += 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_pop_owned(tinypy_frame_object_t *frame) {
    assert(frame->stack_top > frame->value_stack);
    frame->stack_top -= 1;
    return *frame->stack_top;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_peek(const tinypy_frame_object_t *frame, size_t depth) {
    assert(depth != 0U);
    assert(depth <= __tinypy_eval_stack_depth(frame));
    return frame->stack_top[-(ptrdiff_t)depth];
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_next(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *item;
    size_t size;

    if (value->type != &vm->iterator_type) {
        return tinypy_next(value, out_error);
    }
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(value);
    if (iterator->mode == INT32_C(3)) {
        if (iterator->remaining == 0U) {
            return NULL;
        }
        item = __tinypy_internal_integer_from_i64_fast(vm, iterator->current);
        iterator->remaining -= 1U;
        if (iterator->remaining != 0U) {
            assert((iterator->step >= 0 && iterator->current <= INT64_MAX - iterator->step) || (iterator->step < 0 && iterator->current >= INT64_MIN - iterator->step));
            iterator->current += iterator->step;
        }
        return item;
    }
    if (iterator->mode != INT32_C(0)) {
        return tinypy_next(value, out_error);
    }
    tinypy_value_t *iterable = iterator->iterable;
    if (iterable->type == &vm->tuple_type) {
        size = TINYPY_TUPLE_SIZE(iterable);
        if (iterator->index == size) {
            return NULL;
        }
        item = TINYPY_TUPLE_GET(iterable, iterator->index);
    }
    else if (iterable->type == &vm->list_type && TINYPY_LIST_OBJECT(iterable)->mutation_version == iterator->expected_version) {
        size = TINYPY_LIST_SIZE(iterable);
        if (iterator->index == size) {
            return NULL;
        }
        item = TINYPY_LIST_GET(iterable, iterator->index);
    }
    else {
        return tinypy_next(value, out_error);
    }
    iterator->index += 1U;
    TINYPY_INCREF(item);
    return item;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_eval_sequence_index(tinypy_vm_t *vm, tinypy_value_t *key, size_t size, size_t *out_index) {
    int64_t index;

    if (key->type != &vm->integer_type) {
        return INT32_C(0);
    }
    index = TINYPY_INTEGER_VALUE(key);
    if (index < 0) {
        uint64_t distance = (uint64_t)(-(index + INT64_C(1))) + UINT64_C(1);

        if (distance > (uint64_t)size) {
            return INT32_C(0);
        }
        *out_index = size - (size_t)distance;
        return INT32_C(1);
    }
    if ((uint64_t)index >= (uint64_t)size) {
        return INT32_C(0);
    }
    *out_index = (size_t)index;
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_get_item(tinypy_vm_t *vm, tinypy_value_t *container, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_value_t *item;
    size_t index;

    if (container->type == &vm->dict_type) {
        item = tinypy_internal_dict_get_optional(vm, container, key);
        if (item != NULL) {
            TINYPY_INCREF(item);
            return item;
        }
    }
    else if (container->type == &vm->list_type && __tinypy_eval_sequence_index(vm, key, TINYPY_LIST_SIZE(container), &index) != 0) {
        item = TINYPY_LIST_GET(container, index);
        TINYPY_INCREF(item);
        return item;
    }
    else if (container->type == &vm->tuple_type && __tinypy_eval_sequence_index(vm, key, TINYPY_TUPLE_SIZE(container), &index) != 0) {
        item = TINYPY_TUPLE_GET(container, index);
        TINYPY_INCREF(item);
        return item;
    }
    return tinypy_get_item(container, key, out_error);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_eval_set_item(tinypy_vm_t *vm, tinypy_value_t *container, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error) {
    size_t index;

    if (container->type == &vm->dict_type) {
        tinypy_dict_set(container, key, value);
        return INT32_C(1);
    }
    if (container->type == &vm->list_type && __tinypy_eval_sequence_index(vm, key, TINYPY_LIST_SIZE(container), &index) != 0) {
        tinypy_list_set(container, index, value);
        return INT32_C(1);
    }
    return tinypy_set_item(container, key, value, out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_load_attr(tinypy_vm_t *vm, tinypy_value_t *code, tinypy_value_t *object, tinypy_value_t *name, size_t name_index, tinypy_error_t **out_error) {
    tinypy_value_t *attribute;
    tinypy_value_t *stored;
    const unsigned char *name_bytes;
    size_t name_size;

    if (TINYPY_VALUE_KIND(object) != TINYPY_VALUE_INSTANCE || object->type->get_attribute != NULL) {
        return tinypy_internal_object_get_attr_key(object, name, out_error);
    }
    name_bytes = TINYPY_TEXT_BYTES(name);
    name_size = TINYPY_TEXT_BYTE_SIZE(name);
    if (name_size >= 2U && name_bytes[0] == (unsigned char)'_' && name_bytes[1] == (unsigned char)'_') {
        return tinypy_internal_object_get_attr_key(object, name, out_error);
    }
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_attribute_lookup_cache_entry_t *cache = &TINYPY_CODE_OBJECT(code)->attribute_cache[name_index & (TINYPY_ATTRIBUTE_LOOKUP_CACHE_SIZE - 1U)];
    if (cache->epoch == vm->type_lookup_cache_epoch && cache->name_index == name_index && cache->type == object->type) {
        if (cache->data_descriptor != 0) {
            return tinypy_internal_descriptor_get_value(vm, cache->attribute, object, object->type, out_error);
        }
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(object);
        if (dict_slot != NULL && *dict_slot != NULL) {
            stored = cache->dict_index_valid != 0 ? tinypy_internal_dict_get_index_hint(vm, *dict_slot, cache->dict_key, cache->dict_index) : NULL;
            if (stored == NULL) {
                tinypy_value_t *stored_key = NULL;

                stored = tinypy_internal_dict_get_optional_index(vm, *dict_slot, name, &cache->dict_index, &stored_key);
                if (stored_key != cache->dict_key) {
                    if (stored_key != NULL) {
                        TINYPY_INCREF(stored_key);
                    }
                    if (cache->dict_key != NULL) {
                        TINYPY_DECREF(cache->dict_key);
                    }
                    cache->dict_key = stored_key;
                }
                cache->dict_index_valid = stored != NULL ? INT32_C(1) : INT32_C(0);
            }
            if (stored != NULL) {
                TINYPY_INCREF(stored);
                return stored;
            }
        }
        if (cache->attribute != NULL) {
            if (cache->has_descriptor_get == 0) {
                TINYPY_INCREF(cache->attribute);
                return cache->attribute;
            }
            return tinypy_internal_descriptor_get_value(vm, cache->attribute, object, object->type, out_error);
        }
        return tinypy_internal_object_get_attr_key(object, name, out_error);
    }
    if (tinypy_internal_type_lookup_key(vm, object->type, vm->special_getattribute_key) != NULL || tinypy_internal_type_lookup_key(vm, object->type, vm->special_getattr_key) != NULL) {
        return tinypy_internal_object_get_attr_key(object, name, out_error);
    }
    if (cache->dict_key != NULL) {
        TINYPY_DECREF(cache->dict_key);
        cache->dict_key = NULL;
    }
    attribute = tinypy_internal_type_lookup_key(vm, object->type, name);
    cache->epoch = vm->type_lookup_cache_epoch;
    cache->name_index = name_index;
    cache->type = object->type;
    cache->attribute = attribute;
    cache->data_descriptor = attribute != NULL ? tinypy_internal_descriptor_is_data(vm, attribute) : INT32_C(0);
    cache->has_descriptor_get = attribute != NULL && (attribute->type->descriptor_get != NULL || ((attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_type_lookup_key(vm, attribute->type, vm->special_get_key) != NULL)) ? INT32_C(1) : INT32_C(0);
    cache->dict_index_valid = INT32_C(0);
    if (cache->data_descriptor != 0) {
        return tinypy_internal_descriptor_get_value(vm, attribute, object, object->type, out_error);
    }
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(object);
    if (dict_slot != NULL && *dict_slot != NULL) {
        stored = tinypy_internal_dict_get_optional_index(vm, *dict_slot, name, &cache->dict_index, &cache->dict_key);
        if (stored != NULL) {
            TINYPY_INCREF(cache->dict_key);
            cache->dict_index_valid = INT32_C(1);
            TINYPY_INCREF(stored);
            return stored;
        }
    }
    if (attribute != NULL) {
        if (cache->has_descriptor_get == 0) {
            TINYPY_INCREF(attribute);
            return attribute;
        }
        return tinypy_internal_descriptor_get_value(vm, attribute, object, object->type, out_error);
    }
    return tinypy_internal_object_get_attr_key(object, name, out_error);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_unwind_stack(tinypy_frame_object_t *frame, size_t depth) {
    assert(depth <= __tinypy_eval_stack_depth(frame));
    while (__tinypy_eval_stack_depth(frame) > depth) {
        tinypy_value_t *eval_pop_owned = __tinypy_eval_pop_owned(frame);
        TINYPY_DECREF(eval_pop_owned);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_clear_local_slots(tinypy_frame_object_t *frame) {
    tinypy_value_t *cellvars = TINYPY_CODE_CELLVARS(frame->code);
    tinypy_value_t *freevars = TINYPY_CODE_FREEVARS(frame->code);
    size_t count = (size_t)TINYPY_CODE_LOCAL_COUNT(frame->code) + TINYPY_TUPLE_SIZE(cellvars) + TINYPY_TUPLE_SIZE(freevars);
    size_t index;

    for (index = 0U; index < count; ++index) {
        if (frame->locals_plus[index] != NULL) {
            TINYPY_DECREF(frame->locals_plus[index]);
            frame->locals_plus[index] = NULL;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_lookup_name(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_value_t *name, size_t name_index, int include_locals) {
    tinypy_global_cache_entry_t *cache = NULL;

    if (include_locals != 0) {
        tinypy_value_t *value = tinypy_internal_dict_get_optional(vm, tinypy_internal_frame_locals(frame), name);
        if (value != NULL) {
            TINYPY_INCREF(value);
            return value;
        }
    }
    else {
        uint64_t globals_version = TINYPY_DICT_OBJECT(frame->globals)->mutation_version;

        cache = &frame->global_cache[name_index & (TINYPY_FRAME_GLOBAL_CACHE_SIZE - 1U)];
        if (cache->source != TINYPY_GLOBAL_CACHE_EMPTY && cache->name_index == name_index && cache->globals_version == globals_version && (cache->source != TINYPY_GLOBAL_CACHE_BUILTINS || cache->builtins_version == TINYPY_DICT_OBJECT(frame->builtins)->mutation_version)) {
            TINYPY_INCREF(cache->value);
            return cache->value;
        }
    }
    tinypy_value_t *value = tinypy_internal_dict_get_optional(vm, frame->globals, name);
    if (value != NULL) {
        if (cache != NULL) {
            cache->name_index = name_index;
            cache->globals_version = TINYPY_DICT_OBJECT(frame->globals)->mutation_version;
            cache->builtins_version = 0U;
            cache->value = value;
            cache->source = TINYPY_GLOBAL_CACHE_GLOBALS;
        }
        TINYPY_INCREF(value);
        return value;
    }
    if (TINYPY_CODE_OBJECT(frame->code)->compile_environment != NULL) {
        size_t name_size;
        const char *name_bytes = (const char *)tinypy_string_view(name, &name_size);

        if (name_size == 9U && memcmp(name_bytes, "__debug__", 9U) == 0) {
            int32_t optimize_level = tinypy_internal_compile_environment_optimize_level(TINYPY_CODE_OBJECT(frame->code)->compile_environment);
            int32_t debug = optimize_level == 0 ? INT32_C(1) : INT32_C(0);
            value = tinypy_bool_from_i32(vm, debug);
            if (cache != NULL) {
                cache->name_index = name_index;
                cache->globals_version = TINYPY_DICT_OBJECT(frame->globals)->mutation_version;
                cache->builtins_version = 0U;
                cache->value = value;
                cache->source = TINYPY_GLOBAL_CACHE_COMPILE_ENVIRONMENT;
            }
            return value;
        }
    }
    value = tinypy_internal_dict_get_optional(vm, frame->builtins, name);
    if (value != NULL) {
        if (cache != NULL) {
            cache->name_index = name_index;
            cache->globals_version = TINYPY_DICT_OBJECT(frame->globals)->mutation_version;
            cache->builtins_version = TINYPY_DICT_OBJECT(frame->builtins)->mutation_version;
            cache->value = value;
            cache->source = TINYPY_GLOBAL_CACHE_BUILTINS;
        }
        TINYPY_INCREF(value);
        return value;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_default_output(tinypy_vm_t *vm, const char *name, size_t name_size) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "sys", 3U);

    assert(tinypy_dict_contains(vm->modules, key) != 0);
    tinypy_value_t *module = tinypy_dict_get(vm->modules, key);
    TINYPY_DECREF(key);
    tinypy_value_t *target = tinypy_module_get_value(module, name, name_size);
    assert(target != NULL);
    TINYPY_INCREF(target);
    return target;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_eval_print_whitespace(unsigned char character) {
    return character == (unsigned char)' ' || character == (unsigned char)'\t' || character == (unsigned char)'\n' || character == (unsigned char)'\r' || character == (unsigned char)'\v' || character == (unsigned char)'\f';
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_eval_print_item(tinypy_value_t *target, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_value_t *text = tinypy_object_str(item, out_error);
    const unsigned char *bytes;
    size_t size;

    if (text == NULL) {
        return INT32_C(0);
    }
    bytes = TINYPY_TEXT_BYTES(text);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    if (tinypy_internal_output_soft_space(target) != 0 && (size == 0U || bytes[0] != (unsigned char)'\n')) {
        if (tinypy_internal_output_write(target, " ", 1U, out_error) == 0) {
            TINYPY_DECREF(text);
            return INT32_C(0);
        }
    }
    if (tinypy_internal_output_write(target, bytes, size, out_error) == 0) {
        TINYPY_DECREF(text);
        return INT32_C(0);
    }
    int32_t soft_space = INT32_C(1);
    if (size != 0U) {
        soft_space = __tinypy_eval_print_whitespace(bytes[size - 1U]) == 0 ? INT32_C(1) : INT32_C(0);
    }
    tinypy_internal_output_set_soft_space(target, soft_space);
    TINYPY_DECREF(text);
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_eval_print_newline(tinypy_value_t *target, tinypy_error_t **out_error) {
    if (tinypy_internal_output_write(target, "\n", 1U, out_error) == 0) {
        return INT32_C(0);
    }
    tinypy_internal_output_set_soft_space(target, INT32_C(0));
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_make_name_error(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_error_t **out_error) {
    static const char prefix[] = "name '";
    static const char suffix[] = "' is not defined";
    const unsigned char *name_bytes;
    size_t name_size;
    size_t message_size;
    char *message;

    assert(TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(name) == TINYPY_VALUE_UNICODE);
    name_bytes = TINYPY_TEXT_BYTES(name);
    name_size = TINYPY_TEXT_BYTE_SIZE(name);
    assert(name_size <= SIZE_MAX - (sizeof(prefix) - 1U) - (sizeof(suffix) - 1U) - 1U);
    message_size = (sizeof(prefix) - 1U) + name_size + (sizeof(suffix) - 1U);
    message = (char *)tinypy_internal_vm_allocate(vm, message_size + 1U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    (void)memcpy(message, prefix, sizeof(prefix) - 1U);
    if (name_size != 0U) {
        (void)memcpy(message + sizeof(prefix) - 1U, name_bytes, name_size);
    }
    (void)memcpy(message + sizeof(prefix) - 1U + name_size, suffix, sizeof(suffix));
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_NAME, message, out_error);
    tinypy_internal_vm_deallocate(vm, message, message_size + 1U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_build_sequence(tinypy_vm_t *vm, tinypy_frame_object_t *frame, size_t count, int as_list) {
    size_t index;

    assert(count <= __tinypy_eval_stack_depth(frame));
    tinypy_value_t **items = frame->stack_top - count;
    tinypy_value_t *selected_value;
    if (as_list != 0) {
        selected_value = tinypy_list_from_items(vm, items, count);
    }
    else {
        selected_value = tinypy_tuple_from_items(vm, items, count);
    }
    tinypy_value_t *result = selected_value;
    for (index = 0U; index < count; ++index) {
        TINYPY_DECREF(items[index]);
    }
    frame->stack_top -= count;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_compare(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, size_t operation, tinypy_error_t **out_error) {
    int32_t result;

    assert(operation <= (size_t)TINYPY_COMPARE_EXCEPTION_MATCH);
    if (left->type == &vm->integer_type && right->type == &vm->integer_type && operation <= (size_t)TINYPY_COMPARE_GREATER_EQUAL) {
        int64_t left_integer = TINYPY_INTEGER_VALUE(left);
        int64_t right_integer = TINYPY_INTEGER_VALUE(right);

        switch ((tinypy_compare_operation_e)operation) {
        case TINYPY_COMPARE_LESS:
            result = left_integer < right_integer;
            break;
        case TINYPY_COMPARE_LESS_EQUAL:
            result = left_integer <= right_integer;
            break;
        case TINYPY_COMPARE_EQUAL:
            result = left_integer == right_integer;
            break;
        case TINYPY_COMPARE_NOT_EQUAL:
            result = left_integer != right_integer;
            break;
        case TINYPY_COMPARE_GREATER:
            result = left_integer > right_integer;
            break;
        case TINYPY_COMPARE_GREATER_EQUAL:
            result = left_integer >= right_integer;
            break;
        default:
            assert(0);
            result = INT32_C(0);
            break;
        }
        return tinypy_bool_from_i32(vm, result);
    }
    result = tinypy_compare_bool(left, right, (tinypy_compare_operation_e)operation, out_error);
    if (result < 0) {
        return NULL;
    }
    return tinypy_bool_from_i32(vm, result);
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_exception_class(tinypy_vm_t *vm, tinypy_value_t *value) {
    return TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)value, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_exception_instance(tinypy_vm_t *vm, tinypy_value_t *value) {
    return tinypy_type_is_subtype(value->type, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_eval_reason_e __tinypy_eval_raise(tinypy_vm_t *vm, tinypy_frame_object_t *frame, size_t argument, tinypy_error_t **out_error) {
    tinypy_value_t *traceback = argument == 3U ? __tinypy_eval_pop_owned(frame) : NULL;
    tinypy_value_t *raise_value = argument >= 2U ? __tinypy_eval_pop_owned(frame) : NULL;
    tinypy_value_t *raise_type = argument >= 1U ? __tinypy_eval_pop_owned(frame) : NULL;
    tinypy_value_t *exception = NULL;
    tinypy_eval_reason_e reason = TINYPY_EVAL_REASON_EXCEPTION;

    if (argument > 3U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "RAISE_VARARGS received an invalid argument", out_error);
        goto cleanup;
    }
    if (argument == 0U) {
        if (vm->handled_value == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exceptions must be old-style classes or derived from BaseException", out_error);
            goto cleanup;
        }
        tinypy_internal_exception_restore_raised_from_handled(vm);
        tinypy_internal_exception_make_diagnostic(vm, out_error);
        return TINYPY_EVAL_REASON_RERAISE;
    }
    if (__tinypy_eval_exception_instance(vm, raise_type) != 0) {
        if (argument != 1U && !(argument == 2U && TINYPY_VALUE_KIND(raise_value) == TINYPY_VALUE_NONE)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "instance exception may not have a separate value", out_error);
            goto cleanup;
        }
        exception = raise_type;
        TINYPY_INCREF(exception);
    }
    else if (__tinypy_eval_exception_class(vm, raise_type) != 0) {
        if (raise_value != NULL && __tinypy_eval_exception_instance(vm, raise_value) != 0 && tinypy_type_is_subtype(raise_value->type, (tinypy_type_t *)raise_type) != 0) {
            exception = raise_value;
            TINYPY_INCREF(exception);
        }
        else {
            tinypy_value_t *args;

            if (raise_value == NULL || TINYPY_VALUE_KIND(raise_value) == TINYPY_VALUE_NONE) {
                args = tinypy_tuple_from_items(vm, NULL, 0U);
            }
            else if (TINYPY_VALUE_KIND(raise_value) == TINYPY_VALUE_TUPLE) {
                args = raise_value;
                TINYPY_INCREF(args);
            }
            else {
                args = tinypy_tuple_from_items(vm, &raise_value, 1U);
            }
            exception = tinypy_internal_exception_instantiate((tinypy_type_t *)raise_type, args, NULL, out_error);
            TINYPY_DECREF(args);
            if (exception == NULL) {
                goto cleanup;
            }
        }
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exceptions must derive from BaseException", out_error);
        goto cleanup;
    }
    if (traceback != NULL && TINYPY_VALUE_KIND(traceback) != TINYPY_VALUE_NONE && TINYPY_VALUE_KIND(traceback) != TINYPY_VALUE_TRACEBACK) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "raise traceback must be a traceback or None", out_error);
        goto cleanup;
    }
    tinypy_value_t *raised_traceback = NULL;
    if (traceback != NULL && TINYPY_VALUE_KIND(traceback) == TINYPY_VALUE_TRACEBACK) {
        raised_traceback = traceback;
    }
    tinypy_internal_exception_set_raised(vm, exception, raised_traceback);
    tinypy_internal_exception_make_diagnostic(vm, out_error);
cleanup:
    if (exception != NULL) {
        TINYPY_DECREF(exception);
    }
    if (raise_type != NULL) {
        TINYPY_DECREF(raise_type);
    }
    if (raise_value != NULL) {
        TINYPY_DECREF(raise_value);
    }
    if (traceback != NULL) {
        TINYPY_DECREF(traceback);
    }
    return reason;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_eval_reason_e __tinypy_eval_end_finally(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_value_t **out_result, tinypy_error_t **out_error) {
    tinypy_value_t *top = __tinypy_eval_pop_owned(frame);
    tinypy_eval_reason_e reason = TINYPY_EVAL_REASON_NOT;

    if (TINYPY_VALUE_KIND(top) == TINYPY_VALUE_INTEGER) {
        int64_t encoded = tinypy_integer_as_i64(top);

        if (encoded == TINYPY_EVAL_REASON_RETURN || encoded == TINYPY_EVAL_REASON_CONTINUE) {
            *out_result = __tinypy_eval_pop_owned(frame);
        }
        if (encoded == TINYPY_EVAL_REASON_EXCEPTION || encoded == TINYPY_EVAL_REASON_RERAISE || encoded == TINYPY_EVAL_REASON_RETURN || encoded == TINYPY_EVAL_REASON_BREAK || encoded == TINYPY_EVAL_REASON_CONTINUE) {
            reason = (tinypy_eval_reason_e)encoded;
        }
        else {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "END_FINALLY received an invalid unwind reason", out_error), reason = TINYPY_EVAL_REASON_EXCEPTION;
        }
    }
    else if (TINYPY_VALUE_KIND(top) == TINYPY_VALUE_TYPE && __tinypy_eval_exception_class(vm, top) != 0) {
        tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
        tinypy_value_t *traceback = __tinypy_eval_pop_owned(frame);

        if (__tinypy_eval_exception_instance(vm, value) == 0 || (TINYPY_VALUE_KIND(traceback) != TINYPY_VALUE_NONE && TINYPY_VALUE_KIND(traceback) != TINYPY_VALUE_TRACEBACK)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "END_FINALLY received invalid exception state", out_error);
            reason = TINYPY_EVAL_REASON_EXCEPTION;
        }
        else {
            tinypy_value_t *raised_traceback = NULL;
            if (TINYPY_VALUE_KIND(traceback) == TINYPY_VALUE_TRACEBACK) {
                raised_traceback = traceback;
            }
            tinypy_internal_exception_set_raised(vm, value, raised_traceback);
            tinypy_internal_exception_make_diagnostic(vm, out_error);
            reason = TINYPY_EVAL_REASON_RERAISE;
        }
        TINYPY_DECREF(traceback);
        TINYPY_DECREF(value);
    }
    else if (TINYPY_VALUE_KIND(top) != TINYPY_VALUE_NONE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "END_FINALLY popped invalid exception state", out_error);
        reason = TINYPY_EVAL_REASON_EXCEPTION;
    }
    TINYPY_DECREF(top);
    return reason;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_setup_with(tinypy_vm_t *vm, tinypy_frame_object_t *frame, size_t handler, tinypy_error_t **out_error) {
    tinypy_value_t *context = __tinypy_eval_pop_owned(frame);
    tinypy_value_t *exit_method = tinypy_object_get_attr(context, "__exit__", 8U, out_error);
    tinypy_value_t *enter_result;

    if (exit_method == NULL) {
        TINYPY_DECREF(context);
        return 0;
    }
    tinypy_value_t *enter_method = tinypy_object_get_attr(context, "__enter__", 9U, out_error);
    TINYPY_DECREF(context);
    if (enter_method == NULL) {
        TINYPY_DECREF(exit_method);
        return 0;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, NULL, 0U);
    enter_result = tinypy_call(enter_method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(enter_method);
    if (enter_result == NULL) {
        TINYPY_DECREF(exit_method);
        return 0;
    }
    __tinypy_eval_push_owned(frame, exit_method);
    (void)__tinypy_eval_push_block(frame, TINYPY_OP_SETUP_WITH, handler);
    __tinypy_eval_push_owned(frame, enter_result);
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_eval_reason_e __tinypy_eval_with_cleanup(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_error_t **out_error) {
    tinypy_value_t *top = __tinypy_eval_pop_owned(frame);
    tinypy_value_t *type = NULL;
    tinypy_value_t *value = NULL;
    tinypy_value_t *traceback = NULL;
    tinypy_value_t *payload = NULL;
    tinypy_value_t *exit_method;
    tinypy_value_t *arguments[3];
    int is_exception = 0;

    if (TINYPY_VALUE_KIND(top) == TINYPY_VALUE_NONE) {
        exit_method = __tinypy_eval_pop_owned(frame);
    }
    else if (TINYPY_VALUE_KIND(top) == TINYPY_VALUE_INTEGER) {
        int64_t encoded = tinypy_integer_as_i64(top);

        if (encoded == TINYPY_EVAL_REASON_RETURN || encoded == TINYPY_EVAL_REASON_CONTINUE) {
            payload = __tinypy_eval_pop_owned(frame);
        }
        exit_method = __tinypy_eval_pop_owned(frame);
    }
    else {
        type = top;
        value = __tinypy_eval_pop_owned(frame);
        traceback = __tinypy_eval_pop_owned(frame);
        exit_method = __tinypy_eval_pop_owned(frame);
        is_exception = 1;
    }

    if (is_exception != 0) {
        arguments[0] = type;
        arguments[1] = value;
        arguments[2] = traceback;
    }
    else {
        arguments[0] = tinypy_none_get(vm);
        arguments[1] = tinypy_none_get(vm);
        arguments[2] = tinypy_none_get(vm);
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, arguments, 3U);
    if (is_exception == 0) {
        TINYPY_DECREF(arguments[2]);
        TINYPY_DECREF(arguments[1]);
        TINYPY_DECREF(arguments[0]);
    }
    tinypy_value_t *call_result = tinypy_call(exit_method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(exit_method);
    if (call_result == NULL) {
        if (payload != NULL) {
            TINYPY_DECREF(payload);
        }
        if (traceback != NULL) {
            TINYPY_DECREF(traceback);
        }
        if (value != NULL) {
            TINYPY_DECREF(value);
        }
        TINYPY_DECREF(top);
        return TINYPY_EVAL_REASON_EXCEPTION;
    }

    {
        int32_t call_truth = is_exception != 0 ? tinypy_truth(call_result, out_error) : INT32_C(0);

        if (call_truth < 0) {
            if (payload != NULL) {
                TINYPY_DECREF(payload);
            }
            if (traceback != NULL) {
                TINYPY_DECREF(traceback);
            }
            if (value != NULL) {
                TINYPY_DECREF(value);
            }
            TINYPY_DECREF(top);
            TINYPY_DECREF(call_result);
            return TINYPY_EVAL_REASON_EXCEPTION;
        }
        if (is_exception != 0 && call_truth != 0) {
            TINYPY_DECREF(traceback);
            TINYPY_DECREF(value);
            TINYPY_DECREF(type);
            tinypy_value_t *none = tinypy_none_get(vm);
            __tinypy_eval_push_owned(frame, none);
        }
        else {
            if (payload != NULL) {
                __tinypy_eval_push_owned(frame, payload);
            }
            if (is_exception != 0) {
                __tinypy_eval_push_owned(frame, traceback);
                __tinypy_eval_push_owned(frame, value);
            }
            __tinypy_eval_push_owned(frame, top);
        }
    }
    TINYPY_DECREF(call_result);
    return TINYPY_EVAL_REASON_NOT;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_add_overflow(int64_t left, int64_t right, int64_t *out_result) {
    if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right)) {
        return 1;
    }
    *out_result = left + right;
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_subtract_overflow(int64_t left, int64_t right, int64_t *out_result) {
    if ((right < 0 && left > INT64_MAX + right) || (right > 0 && left < INT64_MIN + right)) {
        return 1;
    }
    *out_result = left - right;
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_multiply_overflow(int64_t left, int64_t right, int64_t *out_result) {
    if (left == 0 || right == 0) {
        *out_result = 0;
        return 0;
    }
    if ((left == -1 && right == INT64_MIN) || (right == -1 && left == INT64_MIN)) {
        return 1;
    }
    if (left > 0) {
        if ((right > 0 && left > INT64_MAX / right) || (right < 0 && right < INT64_MIN / left)) {
            return 1;
        }
    }
    else if ((right > 0 && left < INT64_MIN / right) || (right < 0 && left < INT64_MAX / right)) {
        return 1;
    }
    *out_result = left * right;
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static inline tinypy_value_t *__tinypy_eval_exact_integer_binary(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, tinypy_eval_integer_binary_e operation, int *out_handled, int *out_reused) {
    int64_t left_integer;
    int64_t right_integer;
    int64_t result;

    *out_handled = 0;
    *out_reused = 0;
    if (left->type != &vm->integer_type || right->type != &vm->integer_type) {
        return NULL;
    }
    left_integer = TINYPY_INTEGER_VALUE(left);
    right_integer = TINYPY_INTEGER_VALUE(right);
    if (operation == TINYPY_EVAL_INTEGER_BINARY_ADD) {
        if (__tinypy_eval_add_overflow(left_integer, right_integer, &result) != 0) {
            return NULL;
        }
    }
    else if (operation == TINYPY_EVAL_INTEGER_BINARY_SUBTRACT) {
        if (__tinypy_eval_subtract_overflow(left_integer, right_integer, &result) != 0) {
            return NULL;
        }
    }
    else if (operation == TINYPY_EVAL_INTEGER_BINARY_MULTIPLY) {
        if (__tinypy_eval_multiply_overflow(left_integer, right_integer, &result) != 0) {
            return NULL;
        }
    }
    else if (operation == TINYPY_EVAL_INTEGER_BINARY_AND) {
        result = left_integer & right_integer;
    }
    else if (operation == TINYPY_EVAL_INTEGER_BINARY_XOR) {
        result = left_integer ^ right_integer;
    }
    else if (operation == TINYPY_EVAL_INTEGER_BINARY_OR) {
        result = left_integer | right_integer;
    }
    else if (operation == TINYPY_EVAL_INTEGER_BINARY_RIGHT_SHIFT) {
        if (right_integer < 0 || (uint64_t)right_integer > (uint64_t)PTRDIFF_MAX) {
            return NULL;
        }
        if (right_integer >= 63) {
            result = left_integer < 0 ? -1 : 0;
        }
        else if (left_integer < 0) {
            result = -1 - ((-1 - left_integer) >> (uint32_t)right_integer);
        }
        else {
            result = left_integer >> (uint32_t)right_integer;
        }
    }
    else if (operation == TINYPY_EVAL_INTEGER_BINARY_LEFT_SHIFT) {
        int64_t factor;

        if (right_integer < 0 || (uint64_t)right_integer > (uint64_t)PTRDIFF_MAX) {
            return NULL;
        }
        if (left_integer == 0) {
            result = 0;
        }
        else if (right_integer == 63 && left_integer == -1) {
            result = INT64_MIN;
        }
        else if (right_integer >= 63) {
            return NULL;
        }
        else {
            factor = INT64_C(1) << (uint32_t)right_integer;
            if (left_integer > INT64_MAX / factor || left_integer < INT64_MIN / factor) {
                return NULL;
            }
            result = left_integer * factor;
        }
    }
    else {
        return NULL;
    }
    *out_handled = 1;
    if (result < TINYPY_INTEGER_CONSTANT_MIN || result > TINYPY_INTEGER_CONSTANT_MAX) {
        if (TINYPY_REFCNT(left) == 1) {
            TINYPY_INTEGER_VALUE(left) = result;
            *out_reused = 1;
            return left;
        }
        if (TINYPY_REFCNT(right) == 1) {
            TINYPY_INTEGER_VALUE(right) = result;
            *out_reused = 1;
            return right;
        }
    }
    return __tinypy_internal_integer_from_i64_fast(vm, result);
}
//////////////////////////////////////////////////////////////////////////
static inline int __tinypy_eval_binary(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_binary_slot_t operation, tinypy_eval_integer_binary_e integer_operation, tinypy_error_t **out_error) {
    tinypy_value_t *right = __tinypy_eval_pop_owned(frame);
    tinypy_value_t *left = __tinypy_eval_pop_owned(frame);
    int handled = 0;
    int reused = 0;
    tinypy_value_t *result = NULL;

    if (integer_operation != TINYPY_EVAL_INTEGER_BINARY_NONE) {
        result = __tinypy_eval_exact_integer_binary(vm, left, right, integer_operation, &handled, &reused);
    }
    if (handled == 0) {
        result = operation(left, right, out_error);
    }

    if (reused == 0 || result != right) {
        TINYPY_DECREF(right);
    }
    if (reused == 0 || result != left) {
        TINYPY_DECREF(left);
    }
    if (result == NULL) {
        return 0;
    }
    __tinypy_eval_push_owned(frame, result);
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_pop_slice_key(tinypy_vm_t *vm, tinypy_frame_object_t *frame, size_t variant) {
    tinypy_value_t *stop = (variant & 2U) != 0U ? __tinypy_eval_pop_owned(frame) : NULL;
    tinypy_value_t *start = (variant & 1U) != 0U ? __tinypy_eval_pop_owned(frame) : NULL;
    tinypy_value_t *slice = tinypy_slice_new(vm, start, stop, NULL);

    if (start != NULL) {
        TINYPY_DECREF(start);
    }
    if (stop != NULL) {
        TINYPY_DECREF(stop);
    }
    return slice;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_push_block(tinypy_frame_object_t *frame, int32_t type, size_t handler) {
    size_t stack_depth = __tinypy_eval_stack_depth(frame);

    assert(frame->block_count < TINYPY_FRAME_MAX_BLOCKS);
    assert(handler <= (size_t)UINT32_MAX);
    assert(stack_depth <= (size_t)UINT32_MAX);
    tinypy_frame_block_t *block = &frame->blocks[frame->block_count];
    frame->block_count += 1U;
    block->type = type;
    block->handler = (uint32_t)handler;
    block->stack_level = (uint32_t)stack_depth;
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_clear_diagnostic(tinypy_error_t **out_error) {
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_poll_interrupt(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    if (vm->has_host != 0 && vm->host.poll_interrupt != NULL && vm->host.poll_interrupt(vm->host.user_data) != 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INTERRUPT, "execution interrupted by host", out_error);
        return 0;
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_push_exception_triple(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_value_t *type, tinypy_value_t *value, tinypy_value_t *traceback) {
    tinypy_value_t *stack_traceback;

    assert(type != NULL && value != NULL);
    if (traceback != NULL) {
        stack_traceback = traceback;
        TINYPY_INCREF(stack_traceback);
    }
    else {
        stack_traceback = tinypy_none_get(vm);
    }
    TINYPY_INCREF(value);
    TINYPY_INCREF(type);
    __tinypy_eval_push_owned(frame, stack_traceback);
    __tinypy_eval_push_owned(frame, value);
    __tinypy_eval_push_owned(frame, type);
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_unwind_reason(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_eval_reason_e *reason, size_t *out_instruction_offset, tinypy_value_t **in_out_result, tinypy_error_t **out_error) {
    if (*reason == TINYPY_EVAL_REASON_EXCEPTION) {
        assert(vm->raised_type != NULL && vm->raised_value != NULL);
        tinypy_internal_traceback_here(vm, frame);
    }
    else if (*reason == TINYPY_EVAL_REASON_RERAISE) {
        assert(vm->raised_type != NULL && vm->raised_value != NULL);
        *reason = TINYPY_EVAL_REASON_EXCEPTION;
    }

    while (*reason != TINYPY_EVAL_REASON_NOT && frame->block_count != 0U) {
        tinypy_frame_block_t block = frame->blocks[frame->block_count - 1U];

        if (block.type == TINYPY_OP_SETUP_LOOP && *reason == TINYPY_EVAL_REASON_CONTINUE) {
            assert(*in_out_result != NULL);
            assert(TINYPY_VALUE_KIND(*in_out_result) == TINYPY_VALUE_INTEGER);
            assert(tinypy_integer_as_i64(*in_out_result) >= 0);
            *out_instruction_offset = (size_t)tinypy_integer_as_i64(*in_out_result);
            TINYPY_DECREF(*in_out_result);
            *in_out_result = NULL;
            *reason = TINYPY_EVAL_REASON_NOT;
            return 1;
        }

        frame->block_count -= 1U;
        __tinypy_eval_unwind_stack(frame, block.stack_level);
        if (block.type == TINYPY_OP_SETUP_LOOP && *reason == TINYPY_EVAL_REASON_BREAK) {
            *out_instruction_offset = block.handler;
            *reason = TINYPY_EVAL_REASON_NOT;
            return 1;
        }
        if (block.type == TINYPY_OP_SETUP_FINALLY || (block.type == TINYPY_OP_SETUP_EXCEPT && *reason == TINYPY_EVAL_REASON_EXCEPTION) || block.type == TINYPY_OP_SETUP_WITH) {
            if (*reason == TINYPY_EVAL_REASON_EXCEPTION) {
                if (block.type == TINYPY_OP_SETUP_EXCEPT || block.type == TINYPY_OP_SETUP_WITH) {
                    tinypy_internal_exception_set_handled_from_raised(vm);
                    __tinypy_eval_push_exception_triple(vm, frame, vm->handled_type, vm->handled_value, vm->handled_traceback);
                }
                else {
                    __tinypy_eval_push_exception_triple(vm, frame, vm->raised_type, vm->raised_value, vm->raised_traceback);
                    tinypy_internal_exception_clear_raised(vm);
                }
            }
            else {
                tinypy_value_t *why_value;

                if (*reason == TINYPY_EVAL_REASON_RETURN || *reason == TINYPY_EVAL_REASON_CONTINUE) {
                    assert(*in_out_result != NULL);
                    __tinypy_eval_push_owned(frame, *in_out_result);
                    *in_out_result = NULL;
                }
                why_value = __tinypy_internal_integer_from_i64_fast(vm, (int64_t)*reason);
                __tinypy_eval_push_owned(frame, why_value);
            }
            __tinypy_eval_clear_diagnostic(out_error);
            *out_instruction_offset = block.handler;
            *reason = TINYPY_EVAL_REASON_NOT;
            return 1;
        }
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_verify_code(tinypy_vm_t *vm, tinypy_code_object_t *code, tinypy_error_t **out_error) {
    tinypy_bytecode_metadata_t metadata;
    tinypy_bytecode_verify_result_t result;
    tinypy_bytecode_verify_status_e status;
    const uint8_t *bytecode;
    size_t bytecode_size;
    size_t scratch_size;
    void *scratch;

    if (code->bytecode_verified != 0) {
        return 1;
    }
    bytecode = TINYPY_STRING_OBJECT(code->bytecode)->bytes;
    bytecode_size = TINYPY_SIZED_SIZE(code->bytecode);
    metadata.const_count = TINYPY_TUPLE_SIZE(code->consts);
    metadata.name_count = TINYPY_TUPLE_SIZE(code->names);
    metadata.varname_count = TINYPY_TUPLE_SIZE(code->varnames);
    metadata.freevar_count = TINYPY_TUPLE_SIZE(code->freevars);
    metadata.cellvar_count = TINYPY_TUPLE_SIZE(code->cellvars);
    metadata.declared_stack_size = (size_t)code->stack_size;
    status = tinypy_bytecode_verify_scratch_size(bytecode_size, &scratch_size);
    if (status != TINYPY_BYTECODE_VERIFY_OK) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, tinypy_bytecode_verify_status_name(status), out_error);
        return 0;
    }
    scratch = tinypy_internal_vm_allocate(vm, scratch_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    status = tinypy_bytecode_verify(bytecode, bytecode_size, &metadata, NULL, scratch, scratch_size, &result);
    tinypy_internal_vm_deallocate(vm, scratch, scratch_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    if (status != TINYPY_BYTECODE_VERIFY_OK) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, tinypy_bytecode_verify_status_name(status), out_error);
        return 0;
    }
    code->bytecode_verified = 1;
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_decode_trusted(const uint8_t *bytecode, size_t instruction_offset, tinypy_decoded_instruction_t *instruction) {
    size_t cursor = instruction_offset;
    uint64_t argument = UINT64_C(0);

    for (;;) {
        uint8_t opcode = bytecode[cursor++];

        if (opcode < (uint8_t)TINYPY_OPCODE_HAVE_ARGUMENT) {
            instruction->offset = instruction_offset;
            instruction->next_offset = cursor;
            instruction->opcode = opcode;
            instruction->argument = UINT64_C(0);
            return;
        }
        argument = (argument << 16U) | (uint64_t)((uint16_t)bytecode[cursor] | (uint16_t)((uint16_t)bytecode[cursor + 1U] << 8U));
        cursor += 2U;
        if (opcode == (uint8_t)TINYPY_OPCODE_EXTENDED_ARG) {
            continue;
        }
        instruction->offset = instruction_offset;
        instruction->next_offset = cursor;
        instruction->opcode = opcode;
        instruction->argument = argument;
        return;
    }
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_call_append_iterable(tinypy_value_t *arguments, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *iterator = tinypy_iter(iterable, &iteration_error);

    if (iterator == NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else if (iteration_error != NULL) {
            tinypy_error_release(iteration_error);
        }
        return 0;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        tinypy_list_append(arguments, item);
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return 0;
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_call_merge_keywords(tinypy_vm_t *vm, tinypy_value_t *target, tinypy_value_t *source, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(source) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "double-star argument is not a dictionary", out_error);
        return 0;
    }
    tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(source);
    tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(source);
    for (; iterator != iterator_end; ++iterator) {
        if (iterator->state != TINYPY_DICT_ENTRY_ACTIVE) {
            continue;
        }
        if (TINYPY_VALUE_KIND(iterator->key) != TINYPY_VALUE_STRING) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "keyword name is not a string", out_error);
            return 0;
        }
        if (tinypy_dict_contains(target, iterator->key) != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function received duplicate keyword arguments", out_error);
            return 0;
        }
        tinypy_dict_set(target, iterator->key, iterator->value);
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_call_stack(tinypy_vm_t *vm, tinypy_frame_object_t *frame, size_t argument, int has_varargs, int has_var_keywords, tinypy_error_t **out_error) {
    size_t positional_count = argument & 0xffU;
    size_t keyword_count = (argument >> 8U) & 0xffU;
    size_t consumed = 1U + positional_count + keyword_count * 2U + (has_varargs != 0 ? 1U : 0U) + (has_var_keywords != 0 ? 1U : 0U);
    tinypy_value_t *args = NULL;
    tinypy_value_t *kwargs = NULL;
    tinypy_value_t *result;
    int direct_function;
    int direct_bound_function;
    size_t index;

    assert(consumed <= __tinypy_eval_stack_depth(frame));
    tinypy_value_t **first = frame->stack_top - consumed;
    direct_function = has_varargs == 0 && first[0]->type == &vm->function_type;
    direct_bound_function = 0;
    if (has_varargs == 0 && first[0]->type == &vm->method_type) {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(first[0]);

        direct_bound_function = method->self != NULL && method->function->type == &vm->function_type;
    }
    if (has_varargs != 0) {
        tinypy_value_t *arguments = tinypy_list_from_items(vm, first + 1U, positional_count);
        tinypy_value_t *iterable = first[1U + positional_count + keyword_count * 2U];

        if (__tinypy_eval_call_append_iterable(arguments, iterable, out_error) == 0) {
            TINYPY_DECREF(arguments);
            result = NULL;
            goto cleanup;
        }
        size_t list_size = TINYPY_LIST_SIZE(arguments);
        args = tinypy_tuple_from_items(vm, TINYPY_LIST_OBJECT(arguments)->items, list_size);
        TINYPY_DECREF(arguments);
    }
    else if (direct_function == 0 && direct_bound_function == 0) {
        args = tinypy_tuple_from_items(vm, first + 1U, positional_count);
    }
    if (keyword_count != 0U || has_var_keywords != 0) {
        kwargs = tinypy_dict_new(vm);
        for (index = 0U; index < keyword_count; ++index) {
            tinypy_value_t *key = first[1U + positional_count + index * 2U];
            tinypy_value_t *value = first[2U + positional_count + index * 2U];

            if (TINYPY_VALUE_KIND(key) != TINYPY_VALUE_STRING) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "keyword name is not a string", out_error);
                result = NULL;
                goto cleanup;
            }
            tinypy_dict_set(kwargs, key, value);
        }
        if (has_var_keywords != 0) {
            size_t mapping_offset = 1U + positional_count + keyword_count * 2U + (has_varargs != 0 ? 1U : 0U);

            if (__tinypy_eval_call_merge_keywords(vm, kwargs, first[mapping_offset], out_error) == 0) {
                result = NULL;
                goto cleanup;
            }
        }
    }
    if (direct_function != 0) {
        result = tinypy_internal_eval_function_items(first[0], first + 1U, positional_count, kwargs, out_error);
    }
    else if (direct_bound_function != 0) {
        tinypy_value_t *bound_method = first[0];
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(first[0]);

        first[0] = method->self;
        result = tinypy_internal_eval_function_items(method->function, first, positional_count + 1U, kwargs, out_error);
        first[0] = bound_method;
    }
    else {
        result = tinypy_call(first[0], args, kwargs, out_error);
    }
cleanup:
    if (kwargs != NULL) {
        TINYPY_DECREF(kwargs);
    }
    if (args != NULL) {
        TINYPY_DECREF(args);
    }
    for (index = 0U; index < consumed; ++index) {
        TINYPY_DECREF(first[index]);
    }
    frame->stack_top = first;
    return result;
}

static tinypy_value_t *__tinypy_eval_build_class(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_value_t *namespace_dict, tinypy_value_t *bases, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_value_t *metaclass = NULL;
    tinypy_value_t *class_argument_items[3];
    size_t base_count;
    size_t index;
    tinypy_value_t *metaclass_key;
    size_t name_size;
    const char *name_bytes;

    if (TINYPY_VALUE_KIND(namespace_dict) != TINYPY_VALUE_DICT || TINYPY_VALUE_KIND(bases) != TINYPY_VALUE_TUPLE || TINYPY_VALUE_KIND(name) != TINYPY_VALUE_STRING) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "BUILD_CLASS received invalid operands", out_error);
        return NULL;
    }
    base_count = TINYPY_TUPLE_SIZE(bases);
    metaclass_key = tinypy_string_from_bytes(vm, "__metaclass__", 13U);
    metaclass = tinypy_dict_get_optional(namespace_dict, metaclass_key);
    if (metaclass == NULL) {
        metaclass = tinypy_dict_get_optional(frame->globals, metaclass_key);
    }
    if (metaclass != NULL) {
        TINYPY_INCREF(metaclass);
    }
    TINYPY_DECREF(metaclass_key);
    int condition = metaclass == NULL;
    if (condition != 0) {
        int condition_2 = base_count == 0U;
        if (condition_2 == 0) {
            tinypy_value_t *item_2 = TINYPY_TUPLE_GET(bases, 0U);
            condition_2 = TINYPY_VALUE_KIND(item_2) == TINYPY_VALUE_CLASS;
        }
        condition = (condition_2);
    }
    if (condition) {
        for (index = 0U; index < base_count; ++index) {
            tinypy_value_t *item = TINYPY_TUPLE_GET(bases, index);
            if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_CLASS) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "a classic class cannot inherit from a new-style type", out_error);
                return NULL;
            }
        }
        name_bytes = (const char *)tinypy_string_view(name, &name_size);
        return tinypy_class_new(name_bytes, name_size, bases, namespace_dict, out_error);
    }
    if (metaclass == NULL && base_count != 0U) {
        tinypy_value_t *base = TINYPY_TUPLE_GET(bases, 0U);

        if (TINYPY_VALUE_KIND(base) != TINYPY_VALUE_TYPE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "class base is not a type", out_error);
            return NULL;
        }
        metaclass = &base->type->base.base;
        TINYPY_INCREF(metaclass);
    }
    else if (metaclass == NULL) {
        metaclass = &vm->type_type.base.base;
        TINYPY_INCREF(metaclass);
    }
    class_argument_items[0] = name;
    class_argument_items[1] = bases;
    class_argument_items[2] = namespace_dict;
    tinypy_value_t *class_arguments = tinypy_tuple_from_items(vm, class_argument_items, 3U);
    tinypy_value_t *class_value = tinypy_call(metaclass, class_arguments, NULL, out_error);
    TINYPY_DECREF(class_arguments);
    TINYPY_DECREF(metaclass);
    return class_value;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_bind_arguments(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_function_object_t *function, tinypy_value_t *const *items, size_t item_count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    size_t arg_count = (size_t)TINYPY_CODE_ARG_COUNT(function->code);
    size_t positional_count = item_count;
    size_t default_count = function->defaults != NULL ? TINYPY_TUPLE_SIZE(function->defaults) : 0U;
    size_t first_default;
    size_t index;
    int has_varargs = (TINYPY_CODE_FLAGS(function->code) & TINYPY_CODE_VARARGS) != 0;
    int has_var_keywords = (TINYPY_CODE_FLAGS(function->code) & TINYPY_CODE_VAR_KEYWORDS) != 0;
    tinypy_value_t *extra_keywords = NULL;

    assert(default_count <= arg_count);
    first_default = arg_count - default_count;
    if (positional_count > arg_count && has_varargs == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function received too many positional arguments", out_error);
        return 0;
    }
    for (index = 0U; index < positional_count && index < arg_count; ++index) {
        frame->locals_plus[index] = items[index];
        TINYPY_INCREF(frame->locals_plus[index]);
    }
    if (has_varargs != 0) {
        size_t extra_count = positional_count > arg_count ? positional_count - arg_count : 0U;
        tinypy_value_t *const *extra_items = extra_count != 0U ? &items[arg_count] : NULL;

        frame->locals_plus[arg_count] = tinypy_tuple_from_items(vm, (tinypy_value_t *const *)extra_items, extra_count);
    }
    if (has_var_keywords != 0) {
        extra_keywords = tinypy_dict_new(vm);
        frame->locals_plus[arg_count + (has_varargs != 0 ? 1U : 0U)] = extra_keywords;
    }

    if (kwargs != NULL) {
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(kwargs);
        tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(kwargs);

        for (; iterator != iterator_end; ++iterator) {
            size_t parameter_index;
            int found = 0;

            if (iterator->state != TINYPY_DICT_ENTRY_ACTIVE) {
                continue;
            }
            if (TINYPY_VALUE_KIND(iterator->key) != TINYPY_VALUE_STRING) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "keyword name is not a string", out_error);
                return 0;
            }
            for (parameter_index = 0U; parameter_index < arg_count; ++parameter_index) {
                tinypy_value_t *code_varnames = TINYPY_CODE_VARNAMES(function->code);
                tinypy_value_t *item = TINYPY_TUPLE_GET(code_varnames, parameter_index);
                if (tinypy_equal(iterator->key, item) != 0) {
                    found = 1;
                    break;
                }
            }
            if (found != 0) {
                if (frame->locals_plus[parameter_index] != NULL) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function received multiple values for one argument", out_error);
                    return 0;
                }
                frame->locals_plus[parameter_index] = iterator->value;
                TINYPY_INCREF(iterator->value);
            }
            else if (extra_keywords != NULL) {
                tinypy_dict_set(extra_keywords, iterator->key, iterator->value);
            }
            else {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function received an unexpected keyword argument", out_error);
                return 0;
            }
        }
    }

    for (index = 0U; index < arg_count; ++index) {
        if (frame->locals_plus[index] != NULL) {
            continue;
        }
        if (index < first_default) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function is missing a required argument", out_error);
            return 0;
        }
        frame->locals_plus[index] = TINYPY_TUPLE_GET(function->defaults, index - first_default);
        TINYPY_INCREF(frame->locals_plus[index]);
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_eval_bind_exact_positional(tinypy_frame_object_t *frame, tinypy_function_object_t *function, tinypy_value_t *const *items, size_t item_count, tinypy_value_t *kwargs) {
    size_t arg_count = (size_t)TINYPY_CODE_ARG_COUNT(function->code);
    size_t index;

    if (item_count != arg_count || kwargs != NULL || (TINYPY_CODE_FLAGS(function->code) & (TINYPY_CODE_VARARGS | TINYPY_CODE_VAR_KEYWORDS)) != 0) {
        return 0;
    }
    for (index = 0U; index < arg_count; ++index) {
        frame->locals_plus[index] = items[index];
        TINYPY_INCREF(items[index]);
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_initialize_cells(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_function_object_t *function) {
    tinypy_value_t *code = frame->code;
    tinypy_value_t *cellvars = TINYPY_CODE_CELLVARS(code);
    size_t cell_count = TINYPY_TUPLE_SIZE(cellvars);
    tinypy_value_t *freevars = TINYPY_CODE_FREEVARS(code);
    size_t free_count = TINYPY_TUPLE_SIZE(freevars);
    size_t local_count;
    size_t named_argument_count = (size_t)TINYPY_CODE_ARG_COUNT(code);
    size_t cell_index;

    if (cell_count == 0U && free_count == 0U) {
        return;
    }
    local_count = (size_t)TINYPY_CODE_LOCAL_COUNT(code);
    if ((TINYPY_CODE_FLAGS(code) & TINYPY_CODE_VARARGS) != 0) {
        named_argument_count += 1U;
    }
    if ((TINYPY_CODE_FLAGS(code) & TINYPY_CODE_VAR_KEYWORDS) != 0) {
        named_argument_count += 1U;
    }
    for (cell_index = 0U; cell_index < cell_count; ++cell_index) {
        tinypy_value_t *cell_name = TINYPY_TUPLE_GET(cellvars, cell_index);
        tinypy_value_t *content = NULL;
        size_t argument_index;

        for (argument_index = 0U; argument_index < named_argument_count; ++argument_index) {
            tinypy_value_t *code_varnames = TINYPY_CODE_VARNAMES(code);
            tinypy_value_t *item = TINYPY_TUPLE_GET(code_varnames, argument_index);
            if (tinypy_equal(cell_name, item) != 0) {
                content = frame->locals_plus[argument_index];
                break;
            }
        }
        frame->locals_plus[local_count + cell_index] = tinypy_cell_new(vm, content);
    }
    if (free_count != 0U) {
        assert(function != NULL);
        assert(function->closure != NULL);
        assert(TINYPY_TUPLE_SIZE(function->closure) == free_count);
        for (cell_index = 0U; cell_index < free_count; ++cell_index) {
            tinypy_value_t *cell = TINYPY_TUPLE_GET(function->closure, cell_index);

            frame->locals_plus[local_count + cell_count + cell_index] = cell;
            TINYPY_INCREF(cell);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_code_bound(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals, tinypy_function_object_t *function, tinypy_value_t *const *items, size_t item_count, tinypy_value_t *kwargs, tinypy_generator_object_t *generator, tinypy_value_t *send_value, tinypy_value_t *throw_value, tinypy_value_t *throw_traceback, int *out_yielded, tinypy_error_t **out_error) {
    tinypy_vm_t *vm;
    tinypy_value_t *frame_value;
    tinypy_frame_object_t *frame;
    const uint8_t *bytecode;
    size_t bytecode_size;
    size_t instruction_offset;
    tinypy_value_t *result = NULL;
    tinypy_eval_reason_e reason = TINYPY_EVAL_REASON_NOT;
    int generator_execution = generator != NULL;

    if (out_yielded != NULL) {
        *out_yielded = 0;
    }
    if (generator_execution != 0) {
        frame_value = generator->frame;
        assert(frame_value != NULL);
        frame = TINYPY_FRAME_OBJECT(frame_value);
        code = frame->code;
        vm = TINYPY_VALUE_VM(code);
        if (TINYPY_CODE_OBJECT(code)->bytecode_verified == 0 && __tinypy_eval_verify_code(vm, TINYPY_CODE_OBJECT(code), out_error) == 0) {
            return NULL;
        }
        instruction_offset = generator->instruction_offset;
        assert(frame->back == NULL);
        assert(frame->previous_handled_type == NULL && frame->previous_handled_value == NULL && frame->previous_handled_traceback == NULL);
        frame->back = vm->current_frame != NULL ? &vm->current_frame->base.base : NULL;
        if (frame->back != NULL) {
            TINYPY_INCREF(frame->back);
        }
        frame->previous_handled_type = vm->handled_type;
        frame->previous_handled_value = vm->handled_value;
        frame->previous_handled_traceback = vm->handled_traceback;
        if (frame->previous_handled_type != NULL) {
            TINYPY_INCREF(frame->previous_handled_type);
        }
        if (frame->previous_handled_value != NULL) {
            TINYPY_INCREF(frame->previous_handled_value);
        }
        if (frame->previous_handled_traceback != NULL) {
            TINYPY_INCREF(frame->previous_handled_traceback);
        }
        if (vm->handled_type != NULL || vm->handled_value != NULL || vm->handled_traceback != NULL) {
            tinypy_internal_exception_clear_handled(vm);
        }
        vm->handled_type = generator->handled_type;
        vm->handled_value = generator->handled_value;
        vm->handled_traceback = generator->handled_traceback;
        generator->handled_type = NULL;
        generator->handled_value = NULL;
        generator->handled_traceback = NULL;
        if (generator->started != 0 && throw_value == NULL) {
            TINYPY_INCREF(send_value);
            __tinypy_eval_push_owned(frame, send_value);
        }
    }
    else {
        assert(code != NULL);
        vm = TINYPY_VALUE_VM(code);
        assert(tinypy_internal_vm_valid(vm));
        assert(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
        assert(globals != NULL);
        assert(tinypy_internal_value_belongs_to(vm, globals));
        assert(TINYPY_VALUE_KIND(globals) == TINYPY_VALUE_DICT);
        assert(locals == NULL || tinypy_internal_value_belongs_to(vm, locals));
        assert(locals == NULL || TINYPY_VALUE_KIND(locals) == TINYPY_VALUE_DICT);
        if (TINYPY_CODE_OBJECT(code)->bytecode_verified == 0 && __tinypy_eval_verify_code(vm, TINYPY_CODE_OBJECT(code), out_error) == 0) {
            return NULL;
        }
        instruction_offset = 0U;
        frame_value = function != NULL ? tinypy_internal_frame_new_function(code, globals) : tinypy_frame_new(code, globals, locals);
        frame = TINYPY_FRAME_OBJECT(frame_value);
        if (function != NULL) {
            if (__tinypy_eval_bind_exact_positional(frame, function, items, item_count, kwargs) == 0 && __tinypy_eval_bind_arguments(vm, frame, function, items, item_count, kwargs, out_error) == 0) {
                TINYPY_DECREF(frame_value);
                return NULL;
            }
        }
        if (TINYPY_TUPLE_SIZE(TINYPY_CODE_CELLVARS(code)) != 0U || TINYPY_TUPLE_SIZE(TINYPY_CODE_FREEVARS(code)) != 0U) {
            __tinypy_eval_initialize_cells(vm, frame, function);
        }
    }
    assert(vm->evaluation_depth < 1000U);
    TINYPY_CLEAR_ERROR(out_error);

    bytecode = TINYPY_STRING_OBJECT(TINYPY_CODE_BYTECODE(code))->bytes;
    bytecode_size = TINYPY_SIZED_SIZE(TINYPY_CODE_BYTECODE(code));
    vm->current_frame = frame;
    vm->evaluation_depth += 1U;

    if (throw_value != NULL) {
        tinypy_internal_exception_set_raised(vm, throw_value, throw_traceback);
        reason = TINYPY_EVAL_REASON_EXCEPTION;
        (void)__tinypy_eval_unwind_reason(vm, frame, &reason, &instruction_offset, &result, out_error);
    }
    else if (__tinypy_eval_poll_interrupt(vm, out_error) == 0) {
        reason = TINYPY_EVAL_REASON_EXCEPTION;
        (void)__tinypy_eval_unwind_reason(vm, frame, &reason, &instruction_offset, &result, out_error);
    }

    while (reason == TINYPY_EVAL_REASON_NOT && instruction_offset < bytecode_size) {
        tinypy_decoded_instruction_t instruction;
        size_t argument;

        __tinypy_eval_decode_trusted(bytecode, instruction_offset, &instruction);
        assert(instruction.offset <= (size_t)INT32_MAX);
        assert(instruction.argument <= (uint64_t)SIZE_MAX);
        frame->last_instruction = (int32_t)instruction.offset;
        instruction_offset = instruction.next_offset;
        argument = (size_t)instruction.argument;

        switch (instruction.opcode) {
        case TINYPY_OP_NOP:
            break;
        case TINYPY_OP_POP_TOP: {
            tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
            TINYPY_DECREF(value);
            break;
        }
        case TINYPY_OP_PRINT_ITEM: {
            tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *target = __tinypy_eval_default_output(vm, "stdout", 6U);
            int32_t printed = __tinypy_eval_print_item(target, item, out_error);

            TINYPY_DECREF(target);
            TINYPY_DECREF(item);
            if (printed == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_PRINT_NEWLINE: {
            tinypy_value_t *target = __tinypy_eval_default_output(vm, "stdout", 6U);
            int32_t printed = __tinypy_eval_print_newline(target, out_error);

            TINYPY_DECREF(target);
            if (printed == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_PRINT_ITEM_TO: {
            tinypy_value_t *target = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
            int32_t printed = __tinypy_eval_print_item(target, item, out_error);

            TINYPY_DECREF(item);
            TINYPY_DECREF(target);
            if (printed == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_PRINT_NEWLINE_TO: {
            tinypy_value_t *target = __tinypy_eval_pop_owned(frame);
            int32_t printed = __tinypy_eval_print_newline(target, out_error);

            TINYPY_DECREF(target);
            if (printed == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_EXEC_STMT: {
            tinypy_value_t *locals_value = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *globals_value = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *source = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *execution_globals = TINYPY_VALUE_KIND(globals_value) == TINYPY_VALUE_NONE ? frame->globals : globals_value;
            tinypy_value_t *execution_locals = TINYPY_VALUE_KIND(locals_value) == TINYPY_VALUE_NONE ? (TINYPY_VALUE_KIND(globals_value) == TINYPY_VALUE_NONE ? tinypy_internal_frame_locals(frame) : execution_globals) : locals_value;
            tinypy_value_t *execution_result = NULL;

            if (TINYPY_VALUE_KIND(execution_globals) != TINYPY_VALUE_DICT || TINYPY_VALUE_KIND(execution_locals) != TINYPY_VALUE_DICT) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exec globals and locals must be dictionaries", out_error);
            }
            else if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_CODE) {
                execution_result = __tinypy_eval_code_bound(source, execution_globals, execution_locals, NULL, NULL, 0U, NULL, NULL, NULL, NULL, NULL, NULL, out_error);
            }
            else if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(source) == TINYPY_VALUE_UNICODE) {
                tinypy_compile_options_t options;
                tinypy_value_t *execution_code;
                const void *source_bytes;
                size_t source_size;
                const char *filename;
                size_t filename_size;
                int32_t source_is_unicode;

                if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_STRING) {
                    source_bytes = tinypy_string_view(source, &source_size);
                    source_is_unicode = 0;
                }
                else {
                    size_t code_points;

                    source_bytes = tinypy_unicode_utf8_view(source, &source_size, &code_points);
                    source_is_unicode = 1;
                }
                tinypy_value_t *code_filename = TINYPY_CODE_FILENAME(frame->code);
                filename = (const char *)tinypy_string_view(code_filename, &filename_size);
                tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
                if (tinypy_internal_compile_options_inherit_frame(vm, &options) == 0) {
                    options.optimize_level = vm->optimize_level;
                }
                options.dont_inherit = 0;
                execution_code = tinypy_internal_compiler_compile_source(vm, source_bytes, source_size, source_is_unicode, filename, filename_size, &options, out_error);
                if (execution_code != NULL) {
                    execution_result = tinypy_exec_code(execution_code, execution_globals, execution_locals, out_error);
                    TINYPY_DECREF(execution_code);
                }
            }
            else {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exec requires a string or code object", out_error);
            }
            TINYPY_DECREF(source);
            TINYPY_DECREF(globals_value);
            TINYPY_DECREF(locals_value);
            if (execution_result == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            else {
                TINYPY_DECREF(execution_result);
            }
        }
        break;
        case TINYPY_OP_ROT_TWO: {
            tinypy_value_t *top = __tinypy_eval_peek(frame, 1U);
            frame->stack_top[-1] = __tinypy_eval_peek(frame, 2U);
            frame->stack_top[-2] = top;
        }
        break;
        case TINYPY_OP_ROT_THREE: {
            tinypy_value_t *top = __tinypy_eval_peek(frame, 1U);
            frame->stack_top[-1] = __tinypy_eval_peek(frame, 2U);
            frame->stack_top[-2] = __tinypy_eval_peek(frame, 3U);
            frame->stack_top[-3] = top;
        }
        break;
        case TINYPY_OP_ROT_FOUR: {
            tinypy_value_t *top = __tinypy_eval_peek(frame, 1U);
            frame->stack_top[-1] = __tinypy_eval_peek(frame, 2U);
            frame->stack_top[-2] = __tinypy_eval_peek(frame, 3U);
            frame->stack_top[-3] = __tinypy_eval_peek(frame, 4U);
            frame->stack_top[-4] = top;
        }
        break;
        case TINYPY_OP_DUP_TOP: {
            tinypy_value_t *value = __tinypy_eval_peek(frame, 1U);
            TINYPY_INCREF(value);
            __tinypy_eval_push_owned(frame, value);
        }
        break;
        case TINYPY_OP_DUP_TOPX: {
            tinypy_value_t **first;
            size_t index;

            assert(argument == 2U || argument == 3U);
            assert(argument <= __tinypy_eval_stack_depth(frame));
            first = frame->stack_top - argument;
            for (index = 0U; index < argument; ++index) {
                tinypy_value_t *value = first[index];
                TINYPY_INCREF(value);
                __tinypy_eval_push_owned(frame, value);
            }
        }
        break;
        case TINYPY_OP_UNARY_NOT: {
            tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
            int32_t truth = tinypy_truth(value, out_error);
            TINYPY_DECREF(value);
            if (truth < 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            else {
                tinypy_value_t *bool_from_i32 = tinypy_bool_from_i32(vm, truth == 0);
                __tinypy_eval_push_owned(frame, bool_from_i32);
            }
        }
        break;
        case TINYPY_OP_UNARY_POSITIVE:
        case TINYPY_OP_UNARY_NEGATIVE:
        case TINYPY_OP_UNARY_INVERT: {
            tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *unary_result;

            if (instruction.opcode == TINYPY_OP_UNARY_POSITIVE) {
                unary_result = tinypy_positive(value, out_error);
            }
            else if (instruction.opcode == TINYPY_OP_UNARY_NEGATIVE) {
                unary_result = tinypy_negative(value, out_error);
            }
            else {
                unary_result = tinypy_invert(value, out_error);
            }
            TINYPY_DECREF(value);
            if (unary_result == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, unary_result);
        }
        break;
        case TINYPY_OP_BINARY_ADD:
        case TINYPY_OP_INPLACE_ADD:
            if (__tinypy_eval_binary(vm, frame, tinypy_add, TINYPY_EVAL_INTEGER_BINARY_ADD, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_SUBTRACT:
        case TINYPY_OP_INPLACE_SUBTRACT:
            if (__tinypy_eval_binary(vm, frame, tinypy_subtract, TINYPY_EVAL_INTEGER_BINARY_SUBTRACT, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_MULTIPLY:
        case TINYPY_OP_INPLACE_MULTIPLY:
            if (__tinypy_eval_binary(vm, frame, tinypy_multiply, TINYPY_EVAL_INTEGER_BINARY_MULTIPLY, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_POWER:
        case TINYPY_OP_INPLACE_POWER:
            if (__tinypy_eval_binary(vm, frame, tinypy_power, TINYPY_EVAL_INTEGER_BINARY_NONE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_DIVIDE:
        case TINYPY_OP_INPLACE_DIVIDE:
            if (__tinypy_eval_binary(vm, frame, tinypy_divide, TINYPY_EVAL_INTEGER_BINARY_NONE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_MODULO:
        case TINYPY_OP_INPLACE_MODULO:
            if (__tinypy_eval_binary(vm, frame, tinypy_remainder, TINYPY_EVAL_INTEGER_BINARY_NONE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_FLOOR_DIVIDE:
        case TINYPY_OP_INPLACE_FLOOR_DIVIDE:
            if (__tinypy_eval_binary(vm, frame, tinypy_floor_divide, TINYPY_EVAL_INTEGER_BINARY_NONE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_TRUE_DIVIDE:
        case TINYPY_OP_INPLACE_TRUE_DIVIDE:
            if (__tinypy_eval_binary(vm, frame, tinypy_true_divide, TINYPY_EVAL_INTEGER_BINARY_NONE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_LSHIFT:
        case TINYPY_OP_INPLACE_LSHIFT:
            if (__tinypy_eval_binary(vm, frame, tinypy_left_shift, TINYPY_EVAL_INTEGER_BINARY_LEFT_SHIFT, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_RSHIFT:
        case TINYPY_OP_INPLACE_RSHIFT:
            if (__tinypy_eval_binary(vm, frame, tinypy_right_shift, TINYPY_EVAL_INTEGER_BINARY_RIGHT_SHIFT, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_AND:
        case TINYPY_OP_INPLACE_AND:
            if (__tinypy_eval_binary(vm, frame, tinypy_bit_and, TINYPY_EVAL_INTEGER_BINARY_AND, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_XOR:
        case TINYPY_OP_INPLACE_XOR:
            if (__tinypy_eval_binary(vm, frame, tinypy_bit_xor, TINYPY_EVAL_INTEGER_BINARY_XOR, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_OR:
        case TINYPY_OP_INPLACE_OR:
            if (__tinypy_eval_binary(vm, frame, tinypy_bit_or, TINYPY_EVAL_INTEGER_BINARY_OR, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_SUBSCR: {
            tinypy_value_t *key = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *item = __tinypy_eval_get_item(vm, container, key, out_error);

            TINYPY_DECREF(key);
            TINYPY_DECREF(container);
            if (item == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, item);
        }
        break;
        case TINYPY_OP_SLICE_0:
        case TINYPY_OP_SLICE_1:
        case TINYPY_OP_SLICE_2:
        case TINYPY_OP_SLICE_3: {
            size_t variant = (size_t)(instruction.opcode - TINYPY_OP_SLICE_0);
            tinypy_value_t *slice = __tinypy_eval_pop_slice_key(vm, frame, variant);
            tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *item = tinypy_get_item(container, slice, out_error);

            TINYPY_DECREF(slice);
            TINYPY_DECREF(container);
            if (item == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, item);
        }
        break;
        case TINYPY_OP_STORE_SLICE_0:
        case TINYPY_OP_STORE_SLICE_1:
        case TINYPY_OP_STORE_SLICE_2:
        case TINYPY_OP_STORE_SLICE_3: {
            size_t variant = (size_t)(instruction.opcode - TINYPY_OP_STORE_SLICE_0);
            tinypy_value_t *slice = __tinypy_eval_pop_slice_key(vm, frame, variant);
            tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
            int32_t stored = tinypy_set_item(container, slice, value, out_error);

            TINYPY_DECREF(slice);
            TINYPY_DECREF(container);
            TINYPY_DECREF(value);
            if (stored == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_DELETE_SLICE_0:
        case TINYPY_OP_DELETE_SLICE_1:
        case TINYPY_OP_DELETE_SLICE_2:
        case TINYPY_OP_DELETE_SLICE_3: {
            size_t variant = (size_t)(instruction.opcode - TINYPY_OP_DELETE_SLICE_0);
            tinypy_value_t *slice = __tinypy_eval_pop_slice_key(vm, frame, variant);
            tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
            int32_t deleted = tinypy_delete_item(container, slice, out_error);

            TINYPY_DECREF(slice);
            TINYPY_DECREF(container);
            if (deleted == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_STORE_SUBSCR: {
            tinypy_value_t *key = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
            int32_t stored = __tinypy_eval_set_item(vm, container, key, value, out_error);

            TINYPY_DECREF(key);
            TINYPY_DECREF(container);
            TINYPY_DECREF(value);
            if (stored == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_DELETE_SUBSCR: {
            tinypy_value_t *key = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
            int32_t deleted = tinypy_delete_item(container, key, out_error);

            TINYPY_DECREF(key);
            TINYPY_DECREF(container);
            if (deleted == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_GET_ITER: {
            tinypy_value_t *iterable = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *iterator = tinypy_iter(iterable, out_error);

            TINYPY_DECREF(iterable);
            if (iterator == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, iterator);
        }
        break;
        case TINYPY_OP_LOAD_CONST: {
            tinypy_value_t *code_consts = TINYPY_CODE_CONSTS(code);
            tinypy_value_t *value = TINYPY_TUPLE_GET(code_consts, argument);
            TINYPY_INCREF(value);
            __tinypy_eval_push_owned(frame, value);
        }
        break;
        case TINYPY_OP_LOAD_NAME:
        case TINYPY_OP_LOAD_GLOBAL: {
            tinypy_value_t *code_names = TINYPY_CODE_NAMES(code);
            tinypy_value_t *name = TINYPY_TUPLE_GET(code_names, argument);
            tinypy_value_t *value = __tinypy_eval_lookup_name(vm, frame, name, argument, instruction.opcode == TINYPY_OP_LOAD_NAME);
            if (value == NULL) {
                __tinypy_eval_make_name_error(vm, name, out_error);
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, value);
        }
        break;
        case TINYPY_OP_STORE_NAME:
        case TINYPY_OP_STORE_GLOBAL: {
            tinypy_value_t *code_names = TINYPY_CODE_NAMES(code);
            tinypy_value_t *name = TINYPY_TUPLE_GET(code_names, argument);
            tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *mapping = instruction.opcode == TINYPY_OP_STORE_NAME ? tinypy_internal_frame_locals(frame) : frame->globals;

            tinypy_dict_set(mapping, name, value);
            TINYPY_DECREF(value);
        }
        break;
        case TINYPY_OP_DELETE_NAME:
        case TINYPY_OP_DELETE_GLOBAL: {
            tinypy_value_t *code_names = TINYPY_CODE_NAMES(code);
            tinypy_value_t *name = TINYPY_TUPLE_GET(code_names, argument);
            tinypy_value_t *mapping = instruction.opcode == TINYPY_OP_DELETE_NAME ? tinypy_internal_frame_locals(frame) : frame->globals;
            if (tinypy_dict_contains(mapping, name) == 0) {
                __tinypy_eval_make_name_error(vm, name, out_error);
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            tinypy_dict_delete(mapping, name);
        }
        break;
        case TINYPY_OP_LOAD_FAST:
            if (frame->locals_plus[argument] == NULL) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_UNBOUND_LOCAL, "local variable referenced before assignment", out_error);
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            else {
                TINYPY_INCREF(frame->locals_plus[argument]);
                __tinypy_eval_push_owned(frame, frame->locals_plus[argument]);
            }
            break;
        case TINYPY_OP_LOAD_CLOSURE: {
            size_t local_count = (size_t)TINYPY_CODE_LOCAL_COUNT(code);
            tinypy_value_t *cell = frame->locals_plus[local_count + argument];

            assert(cell != NULL);
            assert(TINYPY_VALUE_KIND(cell) == TINYPY_VALUE_CELL);
            TINYPY_INCREF(cell);
            __tinypy_eval_push_owned(frame, cell);
        }
        break;
        case TINYPY_OP_LOAD_DEREF: {
            size_t local_count = (size_t)TINYPY_CODE_LOCAL_COUNT(code);
            tinypy_value_t *cell = frame->locals_plus[local_count + argument];

            assert(cell != NULL);
            assert(TINYPY_VALUE_KIND(cell) == TINYPY_VALUE_CELL);
            tinypy_value_t *content = tinypy_cell_get(cell);
            if (content == NULL) {
                int selected_value_2;
                tinypy_value_t *cellvars = TINYPY_CODE_CELLVARS(code);
                if (argument < TINYPY_TUPLE_SIZE(cellvars)) {
                    selected_value_2 = TINYPY_ERROR_UNBOUND_LOCAL;
                }
                else {
                    selected_value_2 = TINYPY_ERROR_NAME;
                }
                tinypy_internal_make_vm_error(vm, selected_value_2, "free variable referenced before assignment", out_error);
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            TINYPY_INCREF(content);
            __tinypy_eval_push_owned(frame, content);
        }
        break;
        case TINYPY_OP_STORE_DEREF: {
            size_t local_count = (size_t)TINYPY_CODE_LOCAL_COUNT(code);
            tinypy_value_t *cell = frame->locals_plus[local_count + argument];
            tinypy_value_t *content = __tinypy_eval_pop_owned(frame);

            assert(cell != NULL);
            assert(TINYPY_VALUE_KIND(cell) == TINYPY_VALUE_CELL);
            tinypy_cell_set(cell, content);
            TINYPY_DECREF(content);
        }
        break;
        case TINYPY_OP_STORE_FAST: {
            tinypy_value_t *previous = frame->locals_plus[argument];
            frame->locals_plus[argument] = __tinypy_eval_pop_owned(frame);
            if (previous != NULL) {
                TINYPY_DECREF(previous);
            }
        }
        break;
        case TINYPY_OP_DELETE_FAST:
            if (frame->locals_plus[argument] == NULL) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_UNBOUND_LOCAL, "local variable referenced before assignment", out_error);
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            else {
                TINYPY_DECREF(frame->locals_plus[argument]);
                frame->locals_plus[argument] = NULL;
            }
            break;
        case TINYPY_OP_LOAD_LOCALS: {
            tinypy_value_t *local_mapping = tinypy_internal_frame_locals(frame);

            TINYPY_INCREF(local_mapping);
            __tinypy_eval_push_owned(frame, local_mapping);
        }
        break;
        case TINYPY_OP_LOAD_ATTR: {
            tinypy_value_t *code_names = TINYPY_CODE_NAMES(code);
            tinypy_value_t *name = TINYPY_TUPLE_GET(code_names, argument);
            tinypy_value_t *object = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *attribute;

            attribute = __tinypy_eval_load_attr(vm, code, object, name, argument, out_error);
            TINYPY_DECREF(object);
            if (attribute == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, attribute);
        }
        break;
        case TINYPY_OP_STORE_ATTR: {
            tinypy_value_t *code_names = TINYPY_CODE_NAMES(code);
            tinypy_value_t *name = TINYPY_TUPLE_GET(code_names, argument);
            tinypy_value_t *object = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *attribute_value = __tinypy_eval_pop_owned(frame);
            int32_t stored;

            stored = tinypy_internal_object_set_attr_key(object, name, attribute_value, out_error);
            TINYPY_DECREF(attribute_value);
            TINYPY_DECREF(object);
            if (stored == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_DELETE_ATTR: {
            tinypy_value_t *code_names = TINYPY_CODE_NAMES(code);
            tinypy_value_t *name = TINYPY_TUPLE_GET(code_names, argument);
            tinypy_value_t *object = __tinypy_eval_pop_owned(frame);
            int32_t deleted;

            deleted = tinypy_internal_object_delete_attr_key(object, name, out_error);
            TINYPY_DECREF(object);
            if (deleted == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_IMPORT_NAME: {
            tinypy_value_t *code_names = TINYPY_CODE_NAMES(code);
            tinypy_value_t *name = TINYPY_TUPLE_GET(code_names, argument);
            tinypy_value_t *fromlist = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *level_value = __tinypy_eval_pop_owned(frame);
            const char *name_bytes;
            size_t name_size;
            int64_t level;
            tinypy_value_t *module;

            assert(TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING);
            assert(TINYPY_VALUE_KIND(level_value) == TINYPY_VALUE_INTEGER || TINYPY_VALUE_KIND(level_value) == TINYPY_VALUE_LONG);
            name_bytes = (const char *)tinypy_string_view(name, &name_size);
            level = TINYPY_VALUE_KIND(level_value) == TINYPY_VALUE_LONG ? tinypy_long_as_i64(level_value) : tinypy_integer_as_i64(level_value);
            assert(level >= INT32_MIN && level <= INT32_MAX);
            module = tinypy_import_module(vm, name_bytes, name_size, frame->globals, fromlist, (int32_t)level, out_error);
            TINYPY_DECREF(level_value);
            TINYPY_DECREF(fromlist);
            if (module == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, module);
        }
        break;
        case TINYPY_OP_IMPORT_FROM: {
            tinypy_value_t *code_names = TINYPY_CODE_NAMES(code);
            tinypy_value_t *name = TINYPY_TUPLE_GET(code_names, argument);
            tinypy_value_t *module = __tinypy_eval_peek(frame, 1U);
            const char *name_bytes;
            size_t name_size;
            tinypy_value_t *imported;

            assert(TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING);
            name_bytes = (const char *)tinypy_string_view(name, &name_size);
            imported = tinypy_internal_import_from(module, name_bytes, name_size, out_error);
            if (imported == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, imported);
        }
        break;
        case TINYPY_OP_IMPORT_STAR: {
            tinypy_value_t *module = __tinypy_eval_pop_owned(frame);
            int32_t imported = tinypy_internal_import_star(module, tinypy_internal_frame_locals(frame), out_error);

            TINYPY_DECREF(module);
            if (imported == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_BUILD_TUPLE: {
            tinypy_value_t *tuple = __tinypy_eval_build_sequence(vm, frame, argument, 0);
            __tinypy_eval_push_owned(frame, tuple);
            break;
        }
        case TINYPY_OP_BUILD_LIST: {
            tinypy_value_t *list = __tinypy_eval_build_sequence(vm, frame, argument, 1);
            __tinypy_eval_push_owned(frame, list);
            break;
        }
        case TINYPY_OP_BUILD_SET: {
            tinypy_value_t *set = tinypy_set_new(vm);
            size_t index;

            for (index = 0U; index < argument; ++index) {
                tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
                int32_t added = tinypy_set_add(set, item, out_error);

                TINYPY_DECREF(item);
                if (added == 0) {
                    TINYPY_DECREF(set);
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                    break;
                }
            }
            if (reason == TINYPY_EVAL_REASON_NOT) {
                __tinypy_eval_push_owned(frame, set);
            }
        }
        break;
        case TINYPY_OP_BUILD_MAP: {
            tinypy_value_t *dict = tinypy_dict_new(vm);
            __tinypy_eval_push_owned(frame, dict);
            break;
        }
        case TINYPY_OP_BUILD_SLICE: {
            tinypy_value_t *step;
            tinypy_value_t *stop;
            tinypy_value_t *start;
            tinypy_value_t *slice;

            assert(argument == 2U || argument == 3U);
            step = argument == 3U ? __tinypy_eval_pop_owned(frame) : NULL;
            stop = __tinypy_eval_pop_owned(frame);
            start = __tinypy_eval_pop_owned(frame);
            slice = tinypy_slice_new(vm, start, stop, step);
            if (step != NULL) {
                TINYPY_DECREF(step);
            }
            TINYPY_DECREF(stop);
            TINYPY_DECREF(start);
            __tinypy_eval_push_owned(frame, slice);
        }
        break;
        case TINYPY_OP_STORE_MAP: {
            tinypy_value_t *key = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *dict = __tinypy_eval_peek(frame, 1U);
            tinypy_dict_set(dict, key, value);
            TINYPY_DECREF(value);
            TINYPY_DECREF(key);
        }
        break;
        case TINYPY_OP_LIST_APPEND: {
            tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *list = __tinypy_eval_peek(frame, argument);

            tinypy_list_append(list, item);
            TINYPY_DECREF(item);
        }
        break;
        case TINYPY_OP_SET_ADD: {
            tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *set = __tinypy_eval_peek(frame, argument);
            int32_t added = tinypy_set_add(set, item, out_error);

            TINYPY_DECREF(item);
            if (added == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_MAP_ADD: {
            tinypy_value_t *key = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *dict = __tinypy_eval_peek(frame, argument);

            tinypy_dict_set(dict, key, value);
            TINYPY_DECREF(value);
            TINYPY_DECREF(key);
        }
        break;
        case TINYPY_OP_UNPACK_SEQUENCE: {
            tinypy_value_t *sequence = __tinypy_eval_pop_owned(frame);
            size_t count;
            size_t index;

            if (TINYPY_VALUE_KIND(sequence) == TINYPY_VALUE_TUPLE) {
                count = TINYPY_TUPLE_SIZE(sequence);
            }
            else if (TINYPY_VALUE_KIND(sequence) == TINYPY_VALUE_LIST) {
                count = TINYPY_LIST_SIZE(sequence);
            }
            else {
                count = SIZE_MAX;
            }
            if (count != argument) {
                TINYPY_DECREF(sequence);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "unpack sequence has the wrong size", out_error);
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            for (index = count; index != 0U; index -= 1U) {
                tinypy_value_t *item = TINYPY_VALUE_KIND(sequence) == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(sequence, index - 1U) : TINYPY_LIST_GET(sequence, index - 1U);
                TINYPY_INCREF(item);
                __tinypy_eval_push_owned(frame, item);
            }
            TINYPY_DECREF(sequence);
        }
        break;
        case TINYPY_OP_BUILD_CLASS: {
            tinypy_value_t *namespace_dict = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *bases = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *name = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *class_value = __tinypy_eval_build_class(vm, frame, namespace_dict, bases, name, out_error);

            TINYPY_DECREF(name);
            TINYPY_DECREF(bases);
            TINYPY_DECREF(namespace_dict);
            if (class_value == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, class_value);
        }
        break;
        case TINYPY_OP_COMPARE_OP: {
            tinypy_value_t *right = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *left = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *comparison = __tinypy_eval_compare(vm, left, right, argument, out_error);
            TINYPY_DECREF(right);
            TINYPY_DECREF(left);
            if (comparison == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, comparison);
        }
        break;
        case TINYPY_OP_MAKE_FUNCTION: {
            tinypy_value_t *function_code = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *defaults = argument != 0U ? __tinypy_eval_build_sequence(vm, frame, argument, 0) : NULL;

            assert(TINYPY_VALUE_KIND(function_code) == TINYPY_VALUE_CODE);
            tinypy_value_t *created_function = tinypy_function_new(function_code, frame->globals, defaults, NULL);
            if (defaults != NULL) {
                TINYPY_DECREF(defaults);
            }
            TINYPY_DECREF(function_code);
            __tinypy_eval_push_owned(frame, created_function);
        }
        break;
        case TINYPY_OP_MAKE_CLOSURE: {
            tinypy_value_t *function_code = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *closure = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *defaults = argument != 0U ? __tinypy_eval_build_sequence(vm, frame, argument, 0) : NULL;

            assert(TINYPY_VALUE_KIND(function_code) == TINYPY_VALUE_CODE);
            assert(TINYPY_VALUE_KIND(closure) == TINYPY_VALUE_TUPLE);
            tinypy_value_t *created_function = tinypy_function_new(function_code, frame->globals, defaults, closure);
            if (defaults != NULL) {
                TINYPY_DECREF(defaults);
            }
            TINYPY_DECREF(closure);
            TINYPY_DECREF(function_code);
            __tinypy_eval_push_owned(frame, created_function);
        }
        break;
        case TINYPY_OP_CALL_FUNCTION:
        case TINYPY_OP_CALL_FUNCTION_VAR:
        case TINYPY_OP_CALL_FUNCTION_KW:
        case TINYPY_OP_CALL_FUNCTION_VAR_KW: {
            int has_varargs = instruction.opcode == TINYPY_OP_CALL_FUNCTION_VAR || instruction.opcode == TINYPY_OP_CALL_FUNCTION_VAR_KW;
            int has_var_keywords = instruction.opcode == TINYPY_OP_CALL_FUNCTION_KW || instruction.opcode == TINYPY_OP_CALL_FUNCTION_VAR_KW;
            tinypy_value_t *call_result = __tinypy_eval_call_stack(vm, frame, argument, has_varargs, has_var_keywords, out_error);

            if (call_result == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, call_result);
        }
        break;
        case TINYPY_OP_JUMP_FORWARD:
            assert(instruction_offset <= SIZE_MAX - argument);
            instruction_offset += argument;
            break;
        case TINYPY_OP_FOR_ITER: {
            tinypy_error_t *iteration_error = NULL;
            tinypy_value_t *iterator = __tinypy_eval_peek(frame, 1U);
            tinypy_value_t *item = __tinypy_eval_next(vm, iterator, &iteration_error);

            if (item != NULL) {
                __tinypy_eval_push_owned(frame, item);
            }
            else if (iteration_error != NULL) {
                if (out_error != NULL) {
                    *out_error = iteration_error;
                }
                else {
                    tinypy_error_release(iteration_error);
                }
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            else {
                tinypy_value_t *eval_pop_owned = __tinypy_eval_pop_owned(frame);
                TINYPY_DECREF(eval_pop_owned);
                assert(instruction_offset <= SIZE_MAX - argument);
                instruction_offset += argument;
            }
        }
        break;
        case TINYPY_OP_JUMP_ABSOLUTE:
            if (argument <= instruction.offset && __tinypy_eval_poll_interrupt(vm, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            else {
                instruction_offset = argument;
            }
            break;
        case TINYPY_OP_POP_JUMP_IF_FALSE:
        case TINYPY_OP_POP_JUMP_IF_TRUE: {
            tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
            int32_t truth = tinypy_truth(value, out_error);
            TINYPY_DECREF(value);
            if (truth < 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            else if ((instruction.opcode == TINYPY_OP_POP_JUMP_IF_TRUE && truth != 0) || (instruction.opcode == TINYPY_OP_POP_JUMP_IF_FALSE && truth == 0)) {
                instruction_offset = argument;
            }
        }
        break;
        case TINYPY_OP_JUMP_IF_FALSE_OR_POP:
        case TINYPY_OP_JUMP_IF_TRUE_OR_POP: {
            tinypy_value_t *value = __tinypy_eval_peek(frame, 1U);
            int32_t truth = tinypy_truth(value, out_error);
            int jump = instruction.opcode == TINYPY_OP_JUMP_IF_TRUE_OR_POP ? truth != 0 : truth == 0;
            if (truth < 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            else if (jump != 0) {
                instruction_offset = argument;
            }
            else {
                tinypy_value_t *eval_pop_owned = __tinypy_eval_pop_owned(frame);
                TINYPY_DECREF(eval_pop_owned);
            }
        }
        break;
        case TINYPY_OP_SETUP_LOOP:
            assert(instruction_offset <= SIZE_MAX - argument);
            (void)__tinypy_eval_push_block(frame, TINYPY_OP_SETUP_LOOP, instruction_offset + argument);
            break;
        case TINYPY_OP_SETUP_EXCEPT:
        case TINYPY_OP_SETUP_FINALLY:
            assert(instruction_offset <= SIZE_MAX - argument);
            (void)__tinypy_eval_push_block(frame, (int32_t)instruction.opcode, instruction_offset + argument);
            break;
        case TINYPY_OP_SETUP_WITH:
            assert(instruction_offset <= SIZE_MAX - argument);
            if (__tinypy_eval_setup_with(vm, frame, instruction_offset + argument, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_WITH_CLEANUP:
            reason = __tinypy_eval_with_cleanup(vm, frame, out_error);
            break;
        case TINYPY_OP_POP_BLOCK:
            assert(frame->block_count != 0U);
            frame->block_count -= 1U;
            __tinypy_eval_unwind_stack(frame, frame->blocks[frame->block_count].stack_level);
            break;
        case TINYPY_OP_BREAK_LOOP:
            reason = TINYPY_EVAL_REASON_BREAK;
            break;
        case TINYPY_OP_CONTINUE_LOOP:
            if (__tinypy_eval_poll_interrupt(vm, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            else {
                result = __tinypy_internal_integer_from_i64_fast(vm, (int64_t)argument);
                reason = TINYPY_EVAL_REASON_CONTINUE;
            }
            break;
        case TINYPY_OP_RAISE_VARARGS:
            reason = __tinypy_eval_raise(vm, frame, argument, out_error);
            break;
        case TINYPY_OP_END_FINALLY:
            reason = __tinypy_eval_end_finally(vm, frame, &result, out_error);
            break;
        case TINYPY_OP_RETURN_VALUE:
            result = __tinypy_eval_pop_owned(frame);
            reason = TINYPY_EVAL_REASON_RETURN;
            break;
        case TINYPY_OP_YIELD_VALUE:
            result = __tinypy_eval_pop_owned(frame);
            reason = TINYPY_EVAL_REASON_YIELD;
            break;
        default:
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "bytecode opcode is not implemented by the evaluator", out_error);
            reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        }

        if (reason != TINYPY_EVAL_REASON_NOT && reason != TINYPY_EVAL_REASON_YIELD && (reason != TINYPY_EVAL_REASON_RETURN || frame->block_count != 0U) && __tinypy_eval_unwind_reason(vm, frame, &reason, &instruction_offset, &result, out_error) != 0) {
            continue;
        }
    }

    if (reason == TINYPY_EVAL_REASON_NOT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "bytecode ended without RETURN_VALUE", out_error);
        reason = TINYPY_EVAL_REASON_EXCEPTION;
        tinypy_internal_traceback_here(vm, frame);
    }
    if (reason == TINYPY_EVAL_REASON_EXCEPTION) {
        if (result != NULL) {
            TINYPY_DECREF(result);
            result = NULL;
        }
        if (out_error != NULL && *out_error == NULL) {
            tinypy_internal_exception_make_diagnostic(vm, out_error);
        }
    }
    if (generator_execution != 0 && reason == TINYPY_EVAL_REASON_YIELD) {
        generator->instruction_offset = instruction_offset;
        generator->handled_type = vm->handled_type;
        generator->handled_value = vm->handled_value;
        generator->handled_traceback = vm->handled_traceback;
        vm->handled_type = NULL;
        vm->handled_value = NULL;
        vm->handled_traceback = NULL;
        if (out_yielded != NULL) {
            *out_yielded = 1;
        }
    }
    vm->evaluation_depth -= 1U;
    vm->current_frame = frame->back != NULL ? TINYPY_FRAME_OBJECT(frame->back) : NULL;
    if ((reason != TINYPY_EVAL_REASON_YIELD || generator_execution == 0) && (vm->handled_type != NULL || vm->handled_value != NULL || vm->handled_traceback != NULL)) {
        tinypy_internal_exception_clear_handled(vm);
    }
    vm->handled_type = frame->previous_handled_type;
    vm->handled_value = frame->previous_handled_value;
    vm->handled_traceback = frame->previous_handled_traceback;
    frame->previous_handled_type = NULL;
    frame->previous_handled_value = NULL;
    frame->previous_handled_traceback = NULL;
    if (generator_execution != 0) {
        if (frame->back != NULL) {
            TINYPY_DECREF(frame->back);
            frame->back = NULL;
        }
        if (reason != TINYPY_EVAL_REASON_YIELD) {
            __tinypy_eval_unwind_stack(frame, 0U);
            __tinypy_eval_clear_local_slots(frame);
        }
    }
    else {
        __tinypy_eval_unwind_stack(frame, 0U);
        __tinypy_eval_clear_local_slots(frame);
        if (TINYPY_REFCNT(frame_value) == 1U) {
            tinypy_internal_frame_release_fast(frame);
        }
        else {
            TINYPY_DECREF(frame_value);
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_eval_code(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(code);
    tinypy_internal_exception_clear_raised(vm);
    return __tinypy_eval_code_bound(code, globals, locals, NULL, NULL, 0U, NULL, NULL, NULL, NULL, NULL, NULL, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_exec_code(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals, tinypy_error_t **out_error) {
    assert(code != NULL);
    assert(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(code);
    assert(tinypy_internal_vm_valid(vm));
    assert(globals != NULL);
    assert(tinypy_internal_value_belongs_to(vm, globals));
    assert(locals == NULL || tinypy_internal_value_belongs_to(vm, locals));
    tinypy_value_t *result = tinypy_eval_code(code, globals, locals, out_error);
    if (result == NULL) {
        return NULL;
    }
    TINYPY_DECREF(result);
    return tinypy_none_get(vm);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_eval_function(tinypy_value_t *function_value, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    return tinypy_internal_eval_function_items(function_value, TINYPY_TUPLE_ITEMS(args), TINYPY_TUPLE_SIZE(args), kwargs, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_eval_function_items(tinypy_value_t *function_value, tinypy_value_t *const *items, size_t item_count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *result;

    assert(function_value != NULL);
    assert(TINYPY_VALUE_KIND(function_value) == TINYPY_VALUE_FUNCTION);
    assert(items != NULL || item_count == 0U);
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(function_value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function->code);
    if ((TINYPY_CODE_FLAGS(function->code) & TINYPY_CODE_GENERATOR) != 0) {
        tinypy_value_t *frame_value = tinypy_internal_frame_new_function(function->code, function->globals);
        tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(frame_value);

        if (__tinypy_eval_bind_exact_positional(frame, function, items, item_count, kwargs) == 0 && __tinypy_eval_bind_arguments(vm, frame, function, items, item_count, kwargs, out_error) == 0) {
            TINYPY_DECREF(frame_value);
            return NULL;
        }
        if (TINYPY_TUPLE_SIZE(TINYPY_CODE_CELLVARS(function->code)) != 0U || TINYPY_TUPLE_SIZE(TINYPY_CODE_FREEVARS(function->code)) != 0U) {
            __tinypy_eval_initialize_cells(vm, frame, function);
        }
        if (frame->back != NULL) {
            TINYPY_DECREF(frame->back);
            frame->back = NULL;
        }
        if (frame->previous_handled_type != NULL) {
            TINYPY_DECREF(frame->previous_handled_type);
        }
        if (frame->previous_handled_value != NULL) {
            TINYPY_DECREF(frame->previous_handled_value);
        }
        if (frame->previous_handled_traceback != NULL) {
            TINYPY_DECREF(frame->previous_handled_traceback);
        }
        frame->previous_handled_type = NULL;
        frame->previous_handled_value = NULL;
        frame->previous_handled_traceback = NULL;
        result = tinypy_internal_generator_from_frame(frame_value);
        TINYPY_DECREF(frame_value);
    }
    else {
        result = __tinypy_eval_code_bound(function->code, function->globals, NULL, function, items, item_count, kwargs, NULL, NULL, NULL, NULL, NULL, out_error);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_eval_generator_resume(tinypy_generator_object_t *generator, tinypy_value_t *send_value, tinypy_value_t *throw_value, tinypy_value_t *throw_traceback, int *out_yielded, tinypy_error_t **out_error) {
    assert(generator != NULL);
    assert(send_value != NULL);
    assert(throw_value == NULL || tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(&generator->base), throw_value));
    assert(throw_traceback == NULL || tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(&generator->base), throw_traceback));
    return __tinypy_eval_code_bound(NULL, NULL, NULL, NULL, NULL, 0U, NULL, generator, send_value, throw_value, throw_traceback, out_yielded, out_error);
}
