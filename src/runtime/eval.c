#include "tinypy/eval.h"
#include "tinypy/compiler.h"

#include "bytecode_verify.h"
#include "internal.h"
#include "api_internal.h"

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
    TINYPY_EVAL_INTEGER_BINARY_OR,
    TINYPY_EVAL_INTEGER_BINARY_DIVIDE
} tinypy_eval_integer_binary_e;

static tinypy_bool_t __tinypy_eval_push_block(tinypy_frame_object_t *frame, int32_t type, size_t handler);
static tinypy_value_t *__tinypy_eval_function_items_keywords(tinypy_value_t *function_value, tinypy_value_t *const *items, size_t item_count, tinypy_value_t *kwargs, tinypy_value_t *const *keyword_items, size_t keyword_count, tinypy_error_t **out_error);

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_eval_stack_depth(const tinypy_frame_object_t *frame) {
    return (size_t)(frame->stack_top - frame->value_stack);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_push_owned(tinypy_frame_object_t *frame, tinypy_value_t *value) {
    *frame->stack_top = value;
    frame->stack_top += 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_pop_owned(tinypy_frame_object_t *frame) {
    frame->stack_top -= 1;
    return *frame->stack_top;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_peek(const tinypy_frame_object_t *frame, size_t depth) {
    return frame->stack_top[-(ptrdiff_t)depth];
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_next(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *item;
    size_t size;

    if (value->type != &vm->types[TINYPY_VALUE_ITERATOR]
        && value->type != vm->iterator_types[TINYPY_ITERATOR_TYPE_LIST]
        && value->type != vm->iterator_types[TINYPY_ITERATOR_TYPE_TUPLE]
        && value->type != vm->iterator_types[TINYPY_ITERATOR_TYPE_RANGE]) {
        tinypy_value_t *return_value_1 = tinypy_next(value, out_error);
        return return_value_1;
    }
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(value);
    if (iterator->mode == INT32_C(3)) {
        if (iterator->remaining == 0U) {
            return NULL;
        }
        item = __tinypy_internal_integer_from_i64_fast(vm, iterator->current);
        iterator->remaining -= 1U;
        if (iterator->remaining != 0U) {
            iterator->current += iterator->step;
        }
        return item;
    }
    if (iterator->mode != INT32_C(0)) {
        tinypy_value_t *return_value_2 = tinypy_next(value, out_error);
        return return_value_2;
    }
    tinypy_value_t *iterable = iterator->iterable;
    if (iterable == NULL) {
        return NULL;
    }
    if (iterable->type == &vm->types[TINYPY_VALUE_TUPLE]) {
        size = TINYPY_TUPLE_SIZE(iterable);
        if (iterator->index >= size) {
            tinypy_internal_iterator_clear(iterator);
            return NULL;
        }
        item = TINYPY_TUPLE_GET(iterable, iterator->index);
    }
    else if (iterable->type == &vm->types[TINYPY_VALUE_LIST] && TINYPY_LIST_OBJECT(iterable)->mutation_version == iterator->expected_state) {
        size = TINYPY_LIST_SIZE(iterable);
        if (iterator->index >= size) {
            tinypy_internal_iterator_clear(iterator);
            return NULL;
        }
        item = TINYPY_LIST_GET(iterable, iterator->index);
    }
    else {
        tinypy_value_t *return_value_3 = tinypy_next(value, out_error);
        return return_value_3;
    }
    iterator->index += 1U;
    return TINYPY_RET(item);
}
//////////////////////////////////////////////////////////////////////////
static inline int32_t __tinypy_eval_truth(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (value == &vm->true_object.base) {
        return 1;
    }
    if (value == &vm->false_object.base || value == &vm->none_object.base) {
        return 0;
    }
    if (value->type == &vm->types[TINYPY_VALUE_INTEGER]) {
        int32_t truth = TINYPY_INTEGER_VALUE(value) != 0 ? 1 : 0;
        return truth;
    }
    int32_t truth = tinypy_truth(value, out_error);
    return truth;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_sequence_index(tinypy_vm_t *vm, tinypy_value_t *key, size_t size, size_t *out_index) {
    int64_t index;

    if (key->type != &vm->types[TINYPY_VALUE_INTEGER]) {
        return TINYPY_FALSE;
    }
    index = TINYPY_INTEGER_VALUE(key);
    if (index < 0) {
        uint64_t distance = (uint64_t)(-(index + INT64_C(1))) + UINT64_C(1);

        if (distance > (uint64_t)size) {
            return TINYPY_FALSE;
        }
        *out_index = size - (size_t)distance;
        return TINYPY_TRUE;
    }
    if ((uint64_t)index >= (uint64_t)size) {
        return TINYPY_FALSE;
    }
    *out_index = (size_t)index;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_get_item(tinypy_vm_t *vm, tinypy_value_t *container, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_value_t *item;
    size_t index;

    if (container->type == &vm->types[TINYPY_VALUE_DICT]) {
        if (tinypy_internal_dict_get_optional_checked(vm, container, key, &item, out_error) == 0) {
            return NULL;
        }
        if (item != NULL) {
            return TINYPY_RET(item);
        }
    }
    else if (container->type == &vm->types[TINYPY_VALUE_LIST] && __tinypy_eval_sequence_index(vm, key, TINYPY_LIST_SIZE(container), &index) != 0) {
        item = TINYPY_RET(TINYPY_LIST_GET(container, index));
        return item;
    }
    else if (container->type == &vm->types[TINYPY_VALUE_TUPLE] && __tinypy_eval_sequence_index(vm, key, TINYPY_TUPLE_SIZE(container), &index) != 0) {
        item = TINYPY_RET(TINYPY_TUPLE_GET(container, index));
        return item;
    }
    tinypy_value_t *return_value_1 = tinypy_get_item(container, key, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_set_item(tinypy_vm_t *vm, tinypy_value_t *container, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error) {
    size_t index;

    if (container->type == &vm->types[TINYPY_VALUE_DICT]) {
        tinypy_bool_t return_value_1 = tinypy_internal_dict_set_checked(vm, container, key, value, out_error);
        return return_value_1;
    }
    if (container->type == &vm->types[TINYPY_VALUE_LIST] && __tinypy_eval_sequence_index(vm, key, TINYPY_LIST_SIZE(container), &index) != 0) {
        tinypy_list_set(container, index, value);
        return TINYPY_TRUE;
    }
    tinypy_bool_t return_value_1 = tinypy_set_item(container, key, value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static inline tinypy_bool_t __tinypy_eval_attribute_cache_valid(const tinypy_attribute_lookup_cache_entry_t *cache, tinypy_value_t *object, size_t name_index) {
    tinypy_bool_t valid = TINYPY_VALUE_VM(object)->type_lookup_cache_epoch != 0U && cache->epoch == TINYPY_VALUE_VM(object)->type_lookup_cache_epoch && cache->name_index == name_index && cache->type == object->type
        && (cache->attribute == NULL || (cache->attribute->type == cache->attribute_type));

    return valid;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_load_attr(tinypy_vm_t *vm, tinypy_value_t *code, tinypy_value_t *object, tinypy_value_t *name, size_t name_index, tinypy_error_t **out_error) {
    const uint8_t *name_bytes = TINYPY_TEXT_BYTES(name);
    size_t name_size = TINYPY_TEXT_BYTE_SIZE(name);
    if (TINYPY_VALUE_KIND(object) != TINYPY_VALUE_INSTANCE || object->type->get_attribute != NULL || (object->type->has_classic_mro != 0 || object->type->has_custom_mro != 0)
        || (name_size >= 2U && name_bytes[0] == (uint8_t)'_' && name_bytes[1] == (uint8_t)'_')) {
        tinypy_value_t *result = tinypy_internal_object_get_attr_key(object, name, out_error);
        return result;
    }
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_attribute_lookup_cache_entry_t *set = &TINYPY_CODE_OBJECT(code)->attribute_cache[(name_index * TINYPY_ATTRIBUTE_LOOKUP_CACHE_WAYS) & (TINYPY_ATTRIBUTE_LOOKUP_CACHE_SIZE - 1U)];
    tinypy_attribute_lookup_cache_entry_t *cache = NULL;
    for (size_t way = 0U; way < TINYPY_ATTRIBUTE_LOOKUP_CACHE_WAYS; ++way) {
        if (__tinypy_eval_attribute_cache_valid(&set[way], object, name_index) != 0) {
            cache = &set[way];
            break;
        }
    }
    tinypy_type_t *type = object->type;
    tinypy_value_t *attribute;
    tinypy_bool_t data_descriptor;
    tinypy_bool_t has_get;
    if (cache == NULL) {
        if (tinypy_internal_object_has_special_override_key(object, vm->internal_special_getattribute_key) != 0
            || tinypy_internal_object_has_special_override_key(object, vm->internal_special_getattr_key) != 0) {
            tinypy_value_t *result = tinypy_internal_object_get_attr_key(object, name, out_error);
            return result;
        }
        if (set[1].internal_dict_key != NULL) {
            TINYPY_DECREF(set[1].internal_dict_key);
        }
        set[1] = set[0];
        cache = set;
        (void)memset(cache, 0, sizeof(*cache));
        attribute = tinypy_internal_type_lookup_key(vm, type, name);
        if (attribute != NULL) {
            TINYPY_INCREF(attribute);
        }
        TINYPY_INCREF(&type->base.base);
        uint64_t epoch = vm->type_lookup_cache_epoch;
        has_get = attribute != NULL ? tinypy_internal_descriptor_has_get(vm, attribute) : TINYPY_FALSE;
        data_descriptor = attribute != NULL && has_get != 0 ? tinypy_internal_descriptor_is_data(vm, attribute) : TINYPY_FALSE;
        cache->epoch = epoch;
        cache->name_index = name_index;
        cache->type = type;
        cache->attribute = attribute;
        cache->attribute_type = attribute != NULL ? attribute->type : NULL;
        cache->data_descriptor = data_descriptor;
        cache->has_descriptor_get = has_get;
    }
    else {
        attribute = cache->attribute;
        data_descriptor = cache->data_descriptor;
        has_get = cache->has_descriptor_get;
        if (attribute != NULL) {
            TINYPY_INCREF(attribute);
        }
        TINYPY_INCREF(&type->base.base);
    }
    tinypy_value_t *result = NULL;
    if (data_descriptor != 0) {
        result = tinypy_internal_descriptor_get_value(vm, attribute, object, type, out_error);
        goto done;
    }
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(object);
    if (dict_slot != NULL && *dict_slot != NULL) {
        size_t dict_index = cache->dict_index;
        result = cache->dict_index_valid != 0 ? tinypy_internal_dict_get_index_hint(vm, *dict_slot, name, dict_index) : NULL;
        if (result == NULL) {
            result = tinypy_internal_dict_get_optional_index(vm, *dict_slot, name, &dict_index, NULL);
            /* No borrowed key survives a callback or a cache eviction. */
            cache->dict_index = dict_index;
            cache->dict_index_valid = result != NULL;
        }
        if (result != NULL) {
            TINYPY_INCREF(result);
            goto done;
        }
    }
    if (attribute != NULL) {
        if (has_get == 0) {
            result = TINYPY_RET(attribute);
        }
        else {
            result = tinypy_internal_descriptor_get_value(vm, attribute, object, type, out_error);
        }
    }
    else {
        result = tinypy_internal_object_get_attr_key(object, name, out_error);
    }
done:
    if (attribute != NULL) {
        TINYPY_DECREF(attribute);
    }
    TINYPY_DECREF(&type->base.base);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_store_attr(tinypy_vm_t *vm, tinypy_value_t *code, tinypy_value_t *object, tinypy_value_t *name, size_t name_index, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(object) != TINYPY_VALUE_INSTANCE || object->type->set_attribute != NULL || (object->type->has_classic_mro != 0 || object->type->has_custom_mro != 0)) {
        tinypy_bool_t result = tinypy_internal_object_set_attr_protocol_key(object, name, value, out_error);
        return result;
    }
    tinypy_attribute_store_cache_entry_t *cache = &TINYPY_CODE_OBJECT(code)->attribute_store_cache[name_index & (TINYPY_ATTRIBUTE_LOOKUP_CACHE_SIZE - 1U)];
    if (TINYPY_VALUE_VM(object)->type_lookup_cache_epoch != 0U && cache->epoch == TINYPY_VALUE_VM(object)->type_lookup_cache_epoch && cache->name_index == name_index && cache->type == object->type
        && (cache->direct_instance_dict != 0 || cache->data_descriptor != 0)
        && (cache->descriptor == NULL || (cache->descriptor->type == cache->descriptor_type))) {
        if (cache->data_descriptor != 0) {
            tinypy_bool_t result = tinypy_internal_descriptor_set_value(vm, cache->descriptor, object, value, out_error);
            return result;
        }
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(object);
        if (*dict_slot == NULL) {
            *dict_slot = tinypy_dict_new(vm);
        }
        tinypy_bool_t stored = tinypy_internal_dict_set_checked(vm, *dict_slot, name, value, out_error);
        return stored;
    }
    (void)memset(cache, 0, sizeof(*cache));
    if (tinypy_internal_object_has_special_override_key(object, vm->internal_special_setattr_key) == 0) {
        tinypy_type_t *type = object->type;
        tinypy_value_t *descriptor = tinypy_internal_type_lookup_key(vm, type, name);
        if (descriptor != NULL) {
            TINYPY_INCREF(descriptor);
        }
        TINYPY_INCREF(&type->base.base);
        uint64_t epoch = vm->type_lookup_cache_epoch;
        tinypy_bool_t data = descriptor != NULL ? tinypy_internal_descriptor_is_data(vm, descriptor) : TINYPY_FALSE;
        cache->epoch = epoch;
        cache->name_index = name_index;
        cache->type = type;
        cache->descriptor = descriptor;
        cache->descriptor_type = descriptor != NULL ? descriptor->type : NULL;
        cache->data_descriptor = data;
        cache->direct_instance_dict = data == 0 && type->has_instance_dict != 0;
        tinypy_bool_t stored;
        if (data != 0) {
            stored = tinypy_internal_descriptor_set_value(vm, descriptor, object, value, out_error);
        }
        else {
            stored = tinypy_internal_object_set_attr_key(object, name, value, out_error);
        }
        if (descriptor != NULL) {
            TINYPY_DECREF(descriptor);
        }
        TINYPY_DECREF(&type->base.base);
        return stored;
    }
    tinypy_bool_t stored = tinypy_internal_object_set_attr_protocol_key(object, name, value, out_error);
    return stored;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_unwind_stack(tinypy_frame_object_t *frame, size_t depth) {
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
static tinypy_value_t *__tinypy_eval_lookup_name(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_value_t *name, size_t name_index, int32_t include_locals, tinypy_error_t **out_error) {
    tinypy_global_cache_entry_t *cache = NULL;

    if (include_locals != 0) {
        tinypy_value_t *value = tinypy_internal_frame_locals_get(vm, tinypy_internal_frame_locals(frame), name, out_error);
        if (value != NULL || tinypy_vm_has_error(vm) != 0) {
            return value;
        }
    }
    else if (vm->dict_cache_exhausted == 0) {
        uint64_t globals_version = TINYPY_DICT_OBJECT(frame->globals)->cache_version;

        cache = &TINYPY_CODE_OBJECT(frame->code)->global_cache[name_index & (TINYPY_CODE_GLOBAL_CACHE_SIZE - 1U)];
        if (cache->source != TINYPY_GLOBAL_CACHE_EMPTY && cache->name_index == name_index && cache->globals_version == globals_version && (cache->source != TINYPY_GLOBAL_CACHE_BUILTINS || cache->builtins_version == TINYPY_DICT_OBJECT(frame->builtins)->cache_version)) {
            return TINYPY_RET(cache->value);
        }
    }
    tinypy_value_t *value;
    tinypy_bool_t cacheable = TINYPY_TRUE;

    if (include_locals != 0) {
        value = tinypy_internal_dict_get_optional_suppressed(vm, frame->globals, name);
    }
    else if (tinypy_internal_dict_get_global(vm, frame->globals, name, &value, &cacheable, out_error) == 0) {
        return NULL;
    }
    if (cacheable == 0) {
        cache = NULL;
    }
    if (value != NULL) {
        if (cache != NULL) {
            cache->name_index = name_index;
            cache->globals_version = TINYPY_DICT_OBJECT(frame->globals)->cache_version;
            cache->builtins_version = 0U;
            cache->value = value;
            cache->source = TINYPY_GLOBAL_CACHE_GLOBALS;
        }
        return TINYPY_RET(value);
    }
    if (TINYPY_CODE_OBJECT(frame->code)->compile_environment != NULL) {
        if (TINYPY_NAME_EQ(name, vm->internal_special_debug_key) != TINYPY_FALSE) {
            int32_t optimize_level = tinypy_internal_compile_environment_optimize_level(TINYPY_CODE_OBJECT(frame->code)->compile_environment);
            int32_t debug = optimize_level == 0 ? INT32_C(1) : INT32_C(0);
            value = tinypy_bool_from_i32(vm, debug);
            if (cache != NULL) {
                cache->name_index = name_index;
                cache->globals_version = TINYPY_DICT_OBJECT(frame->globals)->cache_version;
                cache->builtins_version = 0U;
                cache->value = value;
                cache->source = TINYPY_GLOBAL_CACHE_COMPILE_ENVIRONMENT;
            }
            return value;
        }
    }
    if (include_locals != 0) {
        value = tinypy_internal_dict_get_optional_suppressed(vm, frame->builtins, name);
    }
    else if (tinypy_internal_dict_get_global(vm, frame->builtins, name, &value, &cacheable, out_error) == 0) {
        return NULL;
    }
    if (cacheable == 0) {
        cache = NULL;
    }
    if (value != NULL) {
        if (cache != NULL) {
            cache->name_index = name_index;
            cache->globals_version = TINYPY_DICT_OBJECT(frame->globals)->cache_version;
            cache->builtins_version = TINYPY_DICT_OBJECT(frame->builtins)->cache_version;
            cache->value = value;
            cache->source = TINYPY_GLOBAL_CACHE_BUILTINS;
        }
        return TINYPY_RET(value);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_default_output(tinypy_vm_t *vm, tinypy_value_t *internal_name_key, tinypy_error_t **out_error) {
    tinypy_value_t *sys_dict = TINYPY_MODULE_OBJECT(vm->sys_module)->dict;
    tinypy_value_t *target = tinypy_internal_dict_get_optional(vm, sys_dict, internal_name_key);

    if (target == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "lost sys.stdout", out_error);
        return NULL;
    }
    return TINYPY_RET(target);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_print_whitespace(uint8_t character) {
    return character == (uint8_t)' ' || character == (uint8_t)'\t' || character == (uint8_t)'\n' || character == (uint8_t)'\r' || character == (uint8_t)'\v' || character == (uint8_t)'\f';
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_print_item(tinypy_vm_t *vm, tinypy_value_t *target, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_value_type_e item_kind = TINYPY_VALUE_KIND(item);
    tinypy_value_t *text;
    tinypy_value_t *writer = NULL;
    const uint8_t *bytes;
    size_t size;

    if (tinypy_internal_output_soft_space(target, TINYPY_FALSE) != TINYPY_FALSE
        && tinypy_internal_output_write(target, " ", 1U, out_error) == TINYPY_FALSE) {
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(target) != TINYPY_VALUE_OUTPUT_STREAM) {
        writer = tinypy_object_get_attr_value(target, vm->internal_write_key, out_error);
        if (writer == NULL) {
            return TINYPY_FALSE;
        }
    }
    if (item->type == &vm->types[TINYPY_VALUE_STRING] || item_kind == TINYPY_VALUE_UNICODE) {
        text = TINYPY_RET(item);
    }
    else {
        text = tinypy_object_str(item, out_error);
        if (text == NULL) {
            if (writer != NULL) {
                TINYPY_DECREF(writer);
            }
            return TINYPY_FALSE;
        }
    }
    bytes = TINYPY_TEXT_BYTES(text);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    tinypy_bool_t written;
    if (writer != NULL) {
        tinypy_value_t *args = tinypy_tuple_from_items(vm, &text, 1U);
        tinypy_value_t *result = tinypy_call(writer, args, NULL, out_error);

        TINYPY_DECREF(args);
        TINYPY_DECREF(writer);
        written = result != NULL;
        if (result != NULL) {
            TINYPY_DECREF(result);
        }
    }
    else {
        written = tinypy_internal_output_write_value(target, text, out_error);
    }
    if (written == TINYPY_FALSE) {
        TINYPY_DECREF(text);
        return TINYPY_FALSE;
    }
    if (item_kind == TINYPY_VALUE_STRING || item_kind == TINYPY_VALUE_UNICODE) {
        bytes = TINYPY_TEXT_BYTES(item);
        size = TINYPY_TEXT_BYTE_SIZE(item);
    }
    /* PRINT_ITEM keeps the soft space unless a text item ends in whitespace
       other than a plain space; other objects always set it. */
    tinypy_bool_t soft_space = TINYPY_TRUE;
    if (item_kind == TINYPY_VALUE_UNICODE && size != 0U) {
        size_t last = size - 1U;
        uint32_t code_point;
        while (last != 0U && (bytes[last] & UINT8_C(0xc0)) == UINT8_C(0x80)) {
            last -= 1U;
        }
        if (tinypy_internal_utf8_decode(bytes + last, size - last, &code_point) != 0U && code_point != UINT32_C(32) && tinypy_internal_unicode_is_space(code_point) != 0) {
            soft_space = TINYPY_FALSE;
        }
    }
    else if (item_kind == TINYPY_VALUE_STRING && size != 0U && bytes[size - 1U] != (uint8_t)' ' && __tinypy_eval_print_whitespace(bytes[size - 1U]) != 0) {
        soft_space = TINYPY_FALSE;
    }
    if (soft_space != TINYPY_FALSE) {
        (void)tinypy_internal_output_soft_space(target, TINYPY_TRUE);
    }
    TINYPY_DECREF(text);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_print_newline(tinypy_value_t *target, tinypy_error_t **out_error) {
    if (tinypy_internal_output_write(target, "\n", 1U, out_error) == 0) {
        return TINYPY_FALSE;
    }
    (void)tinypy_internal_output_soft_space(target, TINYPY_FALSE);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_make_unbound_local_error(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("local variable '"),
        TINYPY_MESSAGE_PART_TEXT(name),
        TINYPY_MESSAGE_PART_LITERAL("' referenced before assignment"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_UNBOUND_LOCAL, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_make_unpack_error(tinypy_vm_t *vm, size_t obtained, tinypy_error_t **out_error) {
    char count_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
    size_t count_size = tinypy_internal_format_size(count_buffer, obtained);
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("need more than "),
        {count_buffer, count_size},
        TINYPY_MESSAGE_PART_LITERAL(" value"),
        TINYPY_MESSAGE_PART_LITERAL("s"),
        TINYPY_MESSAGE_PART_LITERAL(" to unpack"),
    };

    if (obtained == 1U) {
        parts[3].size = 0U;
    }
    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_make_name_error(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_bool_t global, tinypy_error_t **out_error) {
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("global name '"),
        TINYPY_MESSAGE_PART_TEXT(name),
        TINYPY_MESSAGE_PART_LITERAL("' is not defined"),
    };

    if (global == 0) {
        parts[0].bytes += 7;
        parts[0].size -= 7U;
    }
    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_NAME, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_build_sequence(tinypy_vm_t *vm, tinypy_frame_object_t *frame, size_t count, int32_t as_list) {
    size_t index;

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

    if (left->type == &vm->types[TINYPY_VALUE_INTEGER] && right->type == &vm->types[TINYPY_VALUE_INTEGER] && operation <= (size_t)TINYPY_COMPARE_GREATER_EQUAL) {
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
            result = INT32_C(0);
            break;
        }
        tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, result);
        return return_value_1;
    }
    tinypy_value_t *return_value_2 = tinypy_compare_value(left, right, (tinypy_compare_operation_e)operation, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_exception_class(tinypy_vm_t *vm, tinypy_value_t *value) {
    if (value == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_bool_t return_value_1 = kind == TINYPY_VALUE_CLASS || (kind == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)value, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0) ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_exception_instance(tinypy_vm_t *vm, tinypy_value_t *value) {
    if (value == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE || tinypy_type_is_subtype(value->type, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_exception_instance_of(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *candidate) {
    if (TINYPY_VALUE_KIND(candidate) == TINYPY_VALUE_CLASS) {
        tinypy_bool_t return_value_1 = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE && tinypy_class_is_subclass(TINYPY_OLD_INSTANCE_OBJECT(value)->class_object, candidate) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
        return return_value_1;
    }
    tinypy_bool_t return_value_2 = TINYPY_VALUE_KIND(candidate) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype(value->type, (tinypy_type_t *)candidate) != 0 ? TINYPY_TRUE : TINYPY_FALSE;

    (void)vm;
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_code_bound(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals, tinypy_function_object_t *function, tinypy_value_t *const *items, size_t item_count, tinypy_value_t *kwargs, tinypy_value_t *const *keyword_items, size_t keyword_count, tinypy_generator_object_t *generator, tinypy_value_t *send_value, tinypy_value_t *throw_type, tinypy_value_t *throw_value, tinypy_value_t *throw_traceback, tinypy_bool_t *out_yielded, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
/* Follows exec_statement in CPython 2.7, including the tuple form and the
   write-back of the frame's locals after a plain exec. */
static tinypy_bool_t __tinypy_eval_exec_statement(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_value_t *source, tinypy_value_t *globals_value, tinypy_value_t *locals_value, tinypy_error_t **out_error) {
    tinypy_value_t *execution_globals = globals_value;
    tinypy_value_t *execution_locals = locals_value;
    tinypy_value_t *execution_result = NULL;
    tinypy_bool_t plain = TINYPY_FALSE;

    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_TUPLE && TINYPY_VALUE_KIND(globals_value) == TINYPY_VALUE_NONE && TINYPY_VALUE_KIND(locals_value) == TINYPY_VALUE_NONE && (TINYPY_TUPLE_SIZE(source) == 2U || TINYPY_TUPLE_SIZE(source) == 3U)) {
        execution_globals = TINYPY_TUPLE_GET(source, 1U);
        if (TINYPY_TUPLE_SIZE(source) == 3U) {
            execution_locals = TINYPY_TUPLE_GET(source, 2U);
        }
        source = TINYPY_TUPLE_GET(source, 0U);
    }
    if (TINYPY_VALUE_KIND(execution_globals) == TINYPY_VALUE_NONE) {
        execution_globals = frame->globals;
        if (TINYPY_VALUE_KIND(execution_locals) == TINYPY_VALUE_NONE) {
            execution_locals = tinypy_internal_frame_locals(frame);
            plain = TINYPY_TRUE;
        }
    }
    else if (TINYPY_VALUE_KIND(execution_locals) == TINYPY_VALUE_NONE) {
        execution_locals = execution_globals;
    }
    if (TINYPY_VALUE_KIND(source) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(source) != TINYPY_VALUE_UNICODE && TINYPY_VALUE_KIND(source) != TINYPY_VALUE_CODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exec: arg 1 must be a string, file, or code object", out_error);
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(execution_globals) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exec: arg 2 must be a dictionary or None", out_error);
        return TINYPY_FALSE;
    }
    if (tinypy_internal_object_is_mapping(vm, execution_locals) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exec: arg 3 must be a mapping or None", out_error);
        return TINYPY_FALSE;
    }
    if (tinypy_internal_dict_get_optional_suppressed(vm, execution_globals, vm->internal_builtins_key) == NULL) {
        tinypy_dict_set(execution_globals, vm->internal_builtins_key, frame->builtins);
    }
    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_CODE) {
        if (TINYPY_TUPLE_SIZE(TINYPY_CODE_FREEVARS(source)) != 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "code object passed to exec may not contain free variables", out_error);
            return TINYPY_FALSE;
        }
        execution_result = __tinypy_eval_code_bound(source, execution_globals, execution_locals, NULL, NULL, 0U, NULL, NULL, 0U, NULL, NULL, NULL, NULL, NULL, NULL, out_error);
    }
    else {
        tinypy_compile_options_t options;
        tinypy_value_t *execution_code;
        const void *source_bytes;
        size_t source_size;
        tinypy_bool_t source_is_unicode = TINYPY_VALUE_KIND(source) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;

        if (source_is_unicode == 0) {
            source_bytes = tinypy_string_view(source, &source_size);
        }
        else {
            size_t code_points;

            source_bytes = tinypy_unicode_utf8_view(source, &source_size, &code_points);
        }
        if (memchr(source_bytes, 0, source_size) != NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected string without null bytes", out_error);
            return TINYPY_FALSE;
        }
        tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
        if (tinypy_internal_compile_options_inherit_frame(vm, &options) == 0) {
            options.optimize_level = vm->optimize_level;
        }
        options.dont_inherit = 0;
        execution_code = tinypy_internal_compiler_compile_source(vm, source_bytes, source_size, source_is_unicode, source_is_unicode == 0 ? TINYPY_TRUE : TINYPY_FALSE, "<string>", 8U, &options, out_error);
        if (execution_code != NULL) {
            execution_result = tinypy_exec_code(execution_code, execution_globals, execution_locals, out_error);
            TINYPY_DECREF(execution_code);
        }
    }
    if (plain != 0) {
        tinypy_internal_frame_locals_to_fast(frame);
    }
    if (execution_result == NULL) {
        return TINYPY_FALSE;
    }
    TINYPY_DECREF(execution_result);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Follows do_raise in CPython 2.7: the traceback is validated first, a tuple
   stands for its first item, and an explicit traceback re-raises without
   adding an entry for this frame. */
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
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exceptions must be old-style classes or derived from BaseException, not NoneType", out_error);
            goto cleanup;
        }
        if (__tinypy_eval_exception_instance_of(vm, vm->handled_value, vm->handled_type) != 0) {
            tinypy_internal_exception_restore_raised_from_handled(vm);
            reason = vm->raised_traceback != NULL ? TINYPY_EVAL_REASON_RERAISE : TINYPY_EVAL_REASON_EXCEPTION;
            goto cleanup;
        }
        raise_type = vm->handled_type;
        raise_value = vm->handled_value;
        traceback = vm->handled_traceback;
        TINYPY_INCREF(raise_type);
        TINYPY_INCREF(raise_value);
        if (traceback != NULL) {
            TINYPY_INCREF(traceback);
        }
    }
    if (traceback != NULL && TINYPY_VALUE_KIND(traceback) == TINYPY_VALUE_NONE) {
        TINYPY_DECREF(traceback);
        traceback = NULL;
    }
    if (traceback != NULL && TINYPY_VALUE_KIND(traceback) != TINYPY_VALUE_TRACEBACK) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "raise: arg 3 must be a traceback or None", out_error);
        goto cleanup;
    }
    while (TINYPY_VALUE_KIND(raise_type) == TINYPY_VALUE_TUPLE && TINYPY_TUPLE_SIZE(raise_type) != 0U) {
        tinypy_value_t *first = TINYPY_TUPLE_GET(raise_type, 0U);

        TINYPY_INCREF(first);
        TINYPY_DECREF(raise_type);
        raise_type = first;
    }
    if (__tinypy_eval_exception_class(vm, raise_type) != 0) {
        if (raise_value != NULL && __tinypy_eval_exception_instance_of(vm, raise_value, raise_type) != 0) {
            exception = TINYPY_RET(raise_value);
        }
        else {
            tinypy_value_t *args;

            if (raise_value == NULL || TINYPY_VALUE_KIND(raise_value) == TINYPY_VALUE_NONE) {
                args = TINYPY_RET_EMPTY_TUPLE(vm);
            }
            else if (TINYPY_VALUE_KIND(raise_value) == TINYPY_VALUE_TUPLE) {
                args = TINYPY_RET(raise_value);
            }
            else {
                args = tinypy_tuple_from_items(vm, &raise_value, 1U);
            }
            if (TINYPY_VALUE_KIND(raise_type) == TINYPY_VALUE_CLASS) {
                exception = tinypy_call(raise_type, args, NULL, out_error);
            }
            else {
                exception = tinypy_internal_exception_instantiate((tinypy_type_t *)raise_type, args, NULL, out_error);
            }
            TINYPY_DECREF(args);
            if (exception == NULL) {
                goto cleanup;
            }
            if (__tinypy_eval_exception_instance(vm, exception) == 0) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("calling "),
                    {((tinypy_type_t *)raise_type)->name, ((tinypy_type_t *)raise_type)->name_size},
                    TINYPY_MESSAGE_PART_LITERAL("() should have returned an instance of BaseException, not '"),
                    TINYPY_MESSAGE_PART_TYPE_NAME(exception),
                    TINYPY_MESSAGE_PART_LITERAL("'"),
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
                goto cleanup;
            }
        }
    }
    else if (__tinypy_eval_exception_instance(vm, raise_type) != 0) {
        if (raise_value != NULL && TINYPY_VALUE_KIND(raise_value) != TINYPY_VALUE_NONE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "instance exception may not have a separate value", out_error);
            goto cleanup;
        }
        exception = TINYPY_RET(raise_type);
    }
    else {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("exceptions must be old-style classes or derived from BaseException, not "),
            TINYPY_MESSAGE_PART_TYPE_NAME(raise_type),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        goto cleanup;
    }
    if (__tinypy_eval_exception_class(vm, raise_type) != 0 && __tinypy_eval_exception_instance_of(vm, exception, raise_type) == 0) {
        tinypy_internal_exception_set_raised_type(vm, raise_type, exception, traceback);
    }
    else {
        tinypy_internal_exception_set_raised(vm, exception, traceback);
    }
    if (traceback != NULL) {
        reason = TINYPY_EVAL_REASON_RERAISE;
    }
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
#if !defined(NDEBUG)
    /* Stack shape is established by the one-time bytecode verifier. */
    size_t depth = __tinypy_eval_stack_depth(frame);
    if (depth < 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "invalid value stack during exception cleanup", out_error);
        return TINYPY_EVAL_REASON_EXCEPTION;
    }
    tinypy_value_t *marker = __tinypy_eval_peek(frame, 1U);
    size_t required = 1U;
    if (TINYPY_VALUE_KIND(marker) == TINYPY_VALUE_INTEGER) {
        int64_t encoded = TINYPY_INTEGER_VALUE(marker);
        if (encoded == TINYPY_EVAL_REASON_RETURN || encoded == TINYPY_EVAL_REASON_CONTINUE) {
            required += 1U;
        }
    }
    else if (TINYPY_VALUE_KIND(marker) != TINYPY_VALUE_NONE) {
        required += 2U;
    }
    if (depth < required) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "invalid value stack during exception cleanup", out_error);
        return TINYPY_EVAL_REASON_EXCEPTION;
    }
#endif
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
    else if (__tinypy_eval_exception_class(vm, top) != 0) {
        tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
        tinypy_value_t *traceback = __tinypy_eval_pop_owned(frame);

        if (TINYPY_VALUE_KIND(traceback) != TINYPY_VALUE_NONE && TINYPY_VALUE_KIND(traceback) != TINYPY_VALUE_TRACEBACK) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "END_FINALLY received invalid exception state", out_error);
            reason = TINYPY_EVAL_REASON_EXCEPTION;
        }
        else {
            tinypy_value_t *raised_traceback = NULL;
            if (TINYPY_VALUE_KIND(traceback) == TINYPY_VALUE_TRACEBACK) {
                raised_traceback = traceback;
            }
            tinypy_internal_exception_set_raised_type(vm, top, value, raised_traceback);
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
static tinypy_bool_t __tinypy_eval_setup_with(tinypy_vm_t *vm, tinypy_frame_object_t *frame, size_t handler, tinypy_error_t **out_error) {
    tinypy_value_t *context = __tinypy_eval_pop_owned(frame);
    tinypy_value_t *exit_method = tinypy_internal_object_get_special_key(context, vm->internal_special_exit_key, out_error);
    tinypy_value_t *enter_result;

    if (exit_method == NULL) {
        if (tinypy_vm_has_error(vm) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "__exit__", out_error);
        }
        TINYPY_DECREF(context);
        return TINYPY_FALSE;
    }
    tinypy_value_t *enter_method = tinypy_internal_object_get_special_key(context, vm->internal_special_enter_key, out_error);
    TINYPY_DECREF(context);
    if (enter_method == NULL) {
        if (tinypy_vm_has_error(vm) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "__enter__", out_error);
        }
        TINYPY_DECREF(exit_method);
        return TINYPY_FALSE;
    }
    tinypy_value_t *args = TINYPY_RET_EMPTY_TUPLE(vm);
    enter_result = tinypy_call(enter_method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(enter_method);
    if (enter_result == NULL) {
        TINYPY_DECREF(exit_method);
        return TINYPY_FALSE;
    }
    __tinypy_eval_push_owned(frame, exit_method);
    (void)__tinypy_eval_push_block(frame, TINYPY_OP_SETUP_WITH, handler);
    __tinypy_eval_push_owned(frame, enter_result);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_eval_reason_e __tinypy_eval_with_cleanup(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_error_t **out_error) {
#if !defined(NDEBUG)
    /* Stack shape is established by the one-time bytecode verifier. */
    size_t depth = __tinypy_eval_stack_depth(frame);
    if (depth < 2U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "invalid value stack during exception cleanup", out_error);
        return TINYPY_EVAL_REASON_EXCEPTION;
    }
    tinypy_value_t *marker = __tinypy_eval_peek(frame, 1U);
    size_t required = 2U;
    if (TINYPY_VALUE_KIND(marker) == TINYPY_VALUE_INTEGER) {
        int64_t encoded = TINYPY_INTEGER_VALUE(marker);
        if (encoded == TINYPY_EVAL_REASON_RETURN || encoded == TINYPY_EVAL_REASON_CONTINUE) {
            required += 1U;
        }
    }
    else if (TINYPY_VALUE_KIND(marker) != TINYPY_VALUE_NONE) {
        required += 2U;
    }
    if (depth < required) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "invalid value stack during exception cleanup", out_error);
        return TINYPY_EVAL_REASON_EXCEPTION;
    }
#endif
    tinypy_value_t *top = __tinypy_eval_pop_owned(frame);
    tinypy_value_t *type = NULL;
    tinypy_value_t *value = NULL;
    tinypy_value_t *traceback = NULL;
    tinypy_value_t *payload = NULL;
    tinypy_value_t *exit_method;
    tinypy_value_t *arguments[3];
    tinypy_bool_t is_exception = TINYPY_FALSE;

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
        arguments[0] = TINYPY_RET_NONE(vm);
        arguments[1] = TINYPY_RET_NONE(vm);
        arguments[2] = TINYPY_RET_NONE(vm);
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

    int32_t call_truth = is_exception != 0 ? __tinypy_eval_truth(vm, call_result, out_error) : INT32_C(0);

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
        tinypy_value_t *none = TINYPY_RET_NONE(vm);
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
    TINYPY_DECREF(call_result);
    return TINYPY_EVAL_REASON_NOT;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_add_overflow(int64_t left, int64_t right, int64_t *out_result) {
    if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right)) {
        return TINYPY_TRUE;
    }
    *out_result = left + right;
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_subtract_overflow(int64_t left, int64_t right, int64_t *out_result) {
    if ((right < 0 && left > INT64_MAX + right) || (right > 0 && left < INT64_MIN + right)) {
        return TINYPY_TRUE;
    }
    *out_result = left - right;
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_multiply_overflow(int64_t left, int64_t right, int64_t *out_result) {
    if (left == 0 || right == 0) {
        *out_result = 0;
        return TINYPY_FALSE;
    }
    if ((left == -1 && right == INT64_MIN) || (right == -1 && left == INT64_MIN)) {
        return TINYPY_TRUE;
    }
    if (left > 0) {
        if ((right > 0 && left > INT64_MAX / right) || (right < 0 && right < INT64_MIN / left)) {
            return TINYPY_TRUE;
        }
    }
    else if ((right > 0 && left < INT64_MIN / right) || (right < 0 && left < INT64_MAX / right)) {
        return TINYPY_TRUE;
    }
    *out_result = left * right;
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* Float arithmetic gets the same inline treatment as integers, including
   reusing an operand that nothing else holds. */
static tinypy_value_t *__tinypy_eval_exact_float_binary(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, tinypy_eval_integer_binary_e operation, tinypy_bool_t *out_handled, tinypy_bool_t *out_reused) {
    double result;

    *out_handled = 0;
    *out_reused = 0;
    tinypy_bool_t left_float = left->type == &vm->types[TINYPY_VALUE_FLOAT];
    tinypy_bool_t right_float = right->type == &vm->types[TINYPY_VALUE_FLOAT];
    if ((left_float == 0 && left->type != &vm->types[TINYPY_VALUE_INTEGER])
        || (right_float == 0 && right->type != &vm->types[TINYPY_VALUE_INTEGER])
        || (left_float == 0 && right_float == 0)) {
        return NULL;
    }
    double left_number = left_float != 0 ? TINYPY_FLOAT_OBJECT(left)->value : (double)TINYPY_INTEGER_VALUE(left);
    double right_number = right_float != 0 ? TINYPY_FLOAT_OBJECT(right)->value : (double)TINYPY_INTEGER_VALUE(right);

    if (operation == TINYPY_EVAL_INTEGER_BINARY_ADD) {
        result = left_number + right_number;
    }
    else if (operation == TINYPY_EVAL_INTEGER_BINARY_SUBTRACT) {
        result = left_number - right_number;
    }
    else if (operation == TINYPY_EVAL_INTEGER_BINARY_MULTIPLY) {
        result = left_number * right_number;
    }
    else if (operation == TINYPY_EVAL_INTEGER_BINARY_DIVIDE && right_number != 0.0) {
        result = left_number / right_number;
    }
    else {
        return NULL;
    }
    *out_handled = 1;
    if (left_float != 0 && TINYPY_REFCNT(left) == 1) {
        TINYPY_FLOAT_OBJECT(left)->value = result;
        *out_reused = 1;
        return left;
    }
    if (right_float != 0 && TINYPY_REFCNT(right) == 1) {
        TINYPY_FLOAT_OBJECT(right)->value = result;
        *out_reused = 1;
        return right;
    }
    tinypy_value_t *return_value_1 = tinypy_float_from_double(vm, result);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static inline tinypy_value_t *__tinypy_eval_exact_integer_binary(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, tinypy_eval_integer_binary_e operation, tinypy_bool_t *out_handled, tinypy_bool_t *out_reused) {
    int64_t left_integer;
    int64_t right_integer;
    int64_t result;

    *out_handled = 0;
    *out_reused = 0;
    if (left->type != &vm->types[TINYPY_VALUE_INTEGER] || right->type != &vm->types[TINYPY_VALUE_INTEGER]) {
        tinypy_value_t *return_value_float = __tinypy_eval_exact_float_binary(vm, left, right, operation, out_handled, out_reused);
        return return_value_float;
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
    tinypy_value_t *return_value_1 = __tinypy_internal_integer_from_i64_fast(vm, result);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static inline tinypy_bool_t __tinypy_eval_binary(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_binary_slot_t operation, tinypy_eval_integer_binary_e integer_operation, tinypy_error_t **out_error) {
    tinypy_value_t *right = __tinypy_eval_pop_owned(frame);
    tinypy_value_t *left = __tinypy_eval_pop_owned(frame);
    tinypy_bool_t handled = TINYPY_FALSE;
    tinypy_bool_t reused = TINYPY_FALSE;
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
        return TINYPY_FALSE;
    }
    __tinypy_eval_push_owned(frame, result);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_decode_trusted(const uint8_t *bytecode, size_t instruction_offset, tinypy_decoded_instruction_t *instruction);
//////////////////////////////////////////////////////////////////////////
/* string_concatenate in ceval: when `s += t` is immediately followed by a
   store back into the variable that holds s, and nothing else references s,
   the byte string is grown in place instead of being copied. */
static tinypy_bool_t __tinypy_eval_inplace_concat(tinypy_vm_t *vm, tinypy_frame_object_t *frame, const uint8_t *bytecode, size_t bytecode_size, size_t next_offset) {
    tinypy_value_t *left;
    tinypy_value_t *right;
    tinypy_decoded_instruction_t next;
    tinypy_value_t **slot = NULL;
    tinypy_value_t *mapping = NULL;
    tinypy_value_t *name = NULL;
    tinypy_value_t *grown;

    if (__tinypy_eval_stack_depth(frame) < 2U || next_offset >= bytecode_size) {
        return TINYPY_FALSE;
    }
    left = frame->stack_top[-2];
    right = frame->stack_top[-1];
    if (left->type != &vm->types[TINYPY_VALUE_STRING] || right->type != &vm->types[TINYPY_VALUE_STRING] || TINYPY_REFCNT(left) != 2U || TINYPY_STRING_OBJECT(left)->interned != 0 || TINYPY_SIZED_SIZE(right) == 0U) {
        return TINYPY_FALSE;
    }
    __tinypy_eval_decode_trusted(bytecode, next_offset, &next);
    if (next.opcode == TINYPY_OP_STORE_FAST) {
        if (next.argument >= (size_t)TINYPY_CODE_LOCAL_COUNT(frame->code) || frame->locals_plus[next.argument] != left) {
            return TINYPY_FALSE;
        }
        slot = &frame->locals_plus[next.argument];
    }
    else if (next.opcode == TINYPY_OP_STORE_NAME) {
        mapping = frame->locals;
        if (mapping == NULL || mapping->type != &vm->types[TINYPY_VALUE_DICT] || next.argument >= TINYPY_TUPLE_SIZE(TINYPY_CODE_NAMES(frame->code))) {
            return TINYPY_FALSE;
        }
        name = TINYPY_TUPLE_GET(TINYPY_CODE_NAMES(frame->code), next.argument);
        if (tinypy_internal_dict_get_optional(vm, mapping, name) != left) {
            return TINYPY_FALSE;
        }
    }
    else {
        return TINYPY_FALSE;
    }
    /* Drop the variable's reference so that the stack holds the only one. */
    if (slot != NULL) {
        *slot = NULL;
        TINYPY_DECREF(left);
    }
    else {
        TINYPY_INCREF(left);
        tinypy_dict_delete(mapping, name);
        TINYPY_DECREF(left);
    }
    grown = tinypy_internal_string_concat_in_place(vm, left, TINYPY_TEXT_BYTES(right), TINYPY_SIZED_SIZE(right));
    if (grown == NULL) {
        if (slot != NULL) {
            *slot = left;
        }
        else {
            tinypy_dict_set(mapping, name, left);
        }
        TINYPY_INCREF(left);
        return TINYPY_FALSE;
    }
    frame->stack_top[-2] = grown;
    frame->stack_top -= 1;
    TINYPY_DECREF(right);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_pop_slice_bounds(tinypy_frame_object_t *frame, size_t variant, tinypy_value_t **out_start, tinypy_value_t **out_stop) {
    *out_stop = (variant & 2U) != 0U ? __tinypy_eval_pop_owned(frame) : NULL;
    *out_start = (variant & 1U) != 0U ? __tinypy_eval_pop_owned(frame) : NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_release_slice_bounds(tinypy_value_t *start, tinypy_value_t *stop) {
    if (start != NULL) {
        TINYPY_DECREF(start);
    }
    if (stop != NULL) {
        TINYPY_DECREF(stop);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_push_block(tinypy_frame_object_t *frame, int32_t type, size_t handler) {
    size_t stack_depth = __tinypy_eval_stack_depth(frame);

    tinypy_frame_block_t *block = &frame->blocks[frame->block_count];
    frame->block_count += 1U;
    block->type = type;
    block->handler = (uint32_t)handler;
    block->stack_level = (uint32_t)stack_depth;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_clear_diagnostic(tinypy_error_t **out_error) {
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_poll_interrupt(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    if (vm->has_host != 0 && vm->host.poll_interrupt != NULL && vm->host.poll_interrupt(vm->host.user_data) != 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INTERRUPT, "execution interrupted by host", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
#if defined(TINYPY_DEBUGGER)
static void __tinypy_eval_debugger_event(tinypy_vm_t *vm, tinypy_debugger_event_e kind, tinypy_frame_object_t *frame) {
    if (vm->has_debugger == 0) {
        return;
    }

    tinypy_debugger_event_t event;
    event.abi_version = TINYPY_ABI_VERSION;
    event.struct_size = (uint32_t)sizeof(event);
    event.event = kind;
    event.frame = &frame->base.base;
    event.exception = kind == TINYPY_DEBUGGER_EVENT_EXCEPTION ? vm->raised_value : NULL;
    event.traceback = kind == TINYPY_DEBUGGER_EVENT_EXCEPTION ? vm->raised_traceback : NULL;
    tinypy_internal_exception_state_t state;
    tinypy_internal_exception_preserve_begin(vm, &state);
    vm->debugger.callback(vm->debugger.user_data, &event);
    tinypy_internal_exception_preserve_end(vm, &state);
}
#endif
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_push_exception_triple(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_value_t *type, tinypy_value_t *value, tinypy_value_t *traceback) {
    tinypy_value_t *stack_traceback;

    if (traceback != NULL) {
        stack_traceback = TINYPY_RET(traceback);
    }
    else {
        stack_traceback = TINYPY_RET_NONE(vm);
    }
    TINYPY_INCREF(value);
    TINYPY_INCREF(type);
    __tinypy_eval_push_owned(frame, stack_traceback);
    __tinypy_eval_push_owned(frame, value);
    __tinypy_eval_push_owned(frame, type);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_raise_lost_exception(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_error_kind_e kind = TINYPY_ERROR_RUNTIME;
    const char *message = "unwinding without a pending exception";

    if (out_error != NULL && *out_error != NULL) {
        kind = tinypy_error_kind(*out_error);
        message = tinypy_error_message(*out_error, NULL);
    }
    tinypy_internal_exception_raise_kind(vm, kind, message);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_eval_normalize_raised(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_internal_exception_state_t state;
    tinypy_value_t *args;

    if (__tinypy_eval_exception_instance_of(vm, vm->raised_value, vm->raised_type) != 0) {
        return;
    }
    tinypy_internal_exception_preserve_begin(vm, &state);
    __tinypy_eval_clear_diagnostic(out_error);
    if (TINYPY_VALUE_KIND(state.value) == TINYPY_VALUE_NONE) {
        args = TINYPY_RET_EMPTY_TUPLE(vm);
    }
    else if (TINYPY_VALUE_KIND(state.value) == TINYPY_VALUE_TUPLE) {
        args = TINYPY_RET(state.value);
    }
    else {
        args = tinypy_tuple_from_items(vm, &state.value, 1U);
    }
    tinypy_value_t *value = tinypy_call(state.type, args, NULL, out_error);

    TINYPY_DECREF(args);
    if (value != NULL) {
        tinypy_internal_exception_set_raised_type(vm, state.type, value, state.traceback);
        TINYPY_DECREF(value);
    }
    else {
        if (vm->raised_value == NULL) {
            __tinypy_eval_raise_lost_exception(vm, out_error);
        }
        if (vm->raised_traceback == NULL && state.traceback != NULL) {
            vm->raised_traceback = state.traceback;
            TINYPY_INCREF(state.traceback);
        }
    }
    TINYPY_DECREF(state.type);
    TINYPY_DECREF(state.value);
    if (state.traceback != NULL) {
        TINYPY_DECREF(state.traceback);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_unwind_reason(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_eval_reason_e *reason, size_t *out_instruction_offset, tinypy_value_t **in_out_result, tinypy_error_t **out_error) {
    if (*reason == TINYPY_EVAL_REASON_EXCEPTION) {
        if (vm->raised_value == NULL) {
            __tinypy_eval_raise_lost_exception(vm, out_error);
        }
        tinypy_internal_traceback_here(vm, frame);
    }
    else if (*reason == TINYPY_EVAL_REASON_RERAISE) {
        *reason = TINYPY_EVAL_REASON_EXCEPTION;
    }

    while (*reason != TINYPY_EVAL_REASON_NOT && frame->block_count != 0U) {
        tinypy_frame_block_t block = frame->blocks[frame->block_count - 1U];

        if (block.type == TINYPY_OP_SETUP_LOOP && *reason == TINYPY_EVAL_REASON_CONTINUE) {
            *out_instruction_offset = (size_t)tinypy_integer_as_i64(*in_out_result);
            TINYPY_DECREF(*in_out_result);
            *in_out_result = NULL;
            *reason = TINYPY_EVAL_REASON_NOT;
            return TINYPY_TRUE;
        }

        frame->block_count -= 1U;
        __tinypy_eval_unwind_stack(frame, block.stack_level);
        if (block.type == TINYPY_OP_SETUP_LOOP && *reason == TINYPY_EVAL_REASON_BREAK) {
            *out_instruction_offset = block.handler;
            *reason = TINYPY_EVAL_REASON_NOT;
            return TINYPY_TRUE;
        }
        if (block.type == TINYPY_OP_SETUP_FINALLY || (block.type == TINYPY_OP_SETUP_EXCEPT && *reason == TINYPY_EVAL_REASON_EXCEPTION) || block.type == TINYPY_OP_SETUP_WITH) {
            if (*reason == TINYPY_EVAL_REASON_EXCEPTION) {
                if (block.type == TINYPY_OP_SETUP_EXCEPT || block.type == TINYPY_OP_SETUP_WITH) {
                    __tinypy_eval_normalize_raised(vm, out_error);
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
                    __tinypy_eval_push_owned(frame, *in_out_result);
                    *in_out_result = NULL;
                }
                why_value = __tinypy_internal_integer_from_i64_fast(vm, (int64_t)*reason);
                __tinypy_eval_push_owned(frame, why_value);
            }
            __tinypy_eval_clear_diagnostic(out_error);
            *out_instruction_offset = block.handler;
            *reason = TINYPY_EVAL_REASON_NOT;
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_constant_is_none(const void *user_data, size_t index) {
    const tinypy_code_object_t *code = (const tinypy_code_object_t *)user_data;
    tinypy_bool_t result = TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(code->consts, index)) == TINYPY_VALUE_NONE ? TINYPY_TRUE : TINYPY_FALSE;

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_verify_code(tinypy_vm_t *vm, tinypy_code_object_t *code, tinypy_error_t **out_error) {
    tinypy_bytecode_metadata_t metadata;
    tinypy_bytecode_verify_result_t result;
    tinypy_bytecode_verify_status_e status;
    const uint8_t *bytecode;
    size_t bytecode_size;
    size_t scratch_size;
    void *scratch;

    if (code->bytecode_verified != 0) {
        return TINYPY_TRUE;
    }
    bytecode = TINYPY_STRING_OBJECT(code->bytecode)->bytes;
    bytecode_size = TINYPY_SIZED_SIZE(code->bytecode);
    metadata.is_none_constant = __tinypy_eval_constant_is_none;
    metadata.constant_user_data = code;
    metadata.const_count = TINYPY_TUPLE_SIZE(code->consts);
    metadata.name_count = TINYPY_TUPLE_SIZE(code->names);
    metadata.varname_count = TINYPY_TUPLE_SIZE(code->varnames);
    metadata.freevar_count = TINYPY_TUPLE_SIZE(code->freevars);
    metadata.cellvar_count = TINYPY_TUPLE_SIZE(code->cellvars);
    metadata.declared_stack_size = (size_t)code->stack_size;
    status = tinypy_bytecode_verify_scratch_size(bytecode_size, &scratch_size);
    if (status != TINYPY_BYTECODE_VERIFY_OK) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, tinypy_bytecode_verify_status_name(status), out_error);
        return TINYPY_FALSE;
    }
    scratch = tinypy_internal_vm_allocate_checked(vm, scratch_size, out_error);
    if (scratch == NULL) {
        return TINYPY_FALSE;
    }
    status = tinypy_bytecode_verify(bytecode, bytecode_size, &metadata, NULL, scratch, scratch_size, &result);
    tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
    if (status != TINYPY_BYTECODE_VERIFY_OK) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, tinypy_bytecode_verify_status_name(status), out_error);
        return TINYPY_FALSE;
    }
    code->bytecode_verified = 1;
    return TINYPY_TRUE;
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
/* Names a callable for messages the way PyEval_GetFuncName and
   PyEval_GetFuncDesc do. */
static void __tinypy_eval_callable_name(tinypy_value_t *callable, tinypy_message_part_t *out_name, tinypy_message_part_t *out_description) {
    static const char call_description[] = "()";
    static const char constructor_description[] = " constructor";
    static const char instance_description[] = " instance";
    static const char object_description[] = " object";
    tinypy_value_t *name = NULL;
    const char *description = object_description;
    size_t description_size = sizeof(object_description) - 1U;

    switch (TINYPY_VALUE_KIND(callable)) {
    case TINYPY_VALUE_METHOD:
        __tinypy_eval_callable_name(TINYPY_METHOD_OBJECT(callable)->function, out_name, out_description);
        return;
    case TINYPY_VALUE_FUNCTION:
        name = TINYPY_FUNCTION_OBJECT(callable)->name;
        description = call_description;
        description_size = sizeof(call_description) - 1U;
        break;
    case TINYPY_VALUE_NATIVE_FUNCTION:
        name = TINYPY_NATIVE_FUNCTION_OBJECT(callable)->name;
        description = call_description;
        description_size = sizeof(call_description) - 1U;
        break;
    case TINYPY_VALUE_CLASS:
        name = TINYPY_CLASS_OBJECT(callable)->name;
        description = constructor_description;
        description_size = sizeof(constructor_description) - 1U;
        break;
    case TINYPY_VALUE_OLD_INSTANCE:
        name = TINYPY_CLASS_OBJECT(TINYPY_OLD_INSTANCE_OBJECT(callable)->class_object)->name;
        description = instance_description;
        description_size = sizeof(instance_description) - 1U;
        break;
    default:
        break;
    }
    if (name != NULL && TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING) {
        out_name->bytes = (const char *)TINYPY_TEXT_BYTES(name);
        out_name->size = TINYPY_TEXT_BYTE_SIZE(name);
    }
    else {
        out_name->bytes = callable->type->name;
        out_name->size = callable->type->name_size;
    }
    out_description->bytes = description;
    out_description->size = description_size;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_keyword_name_valid(tinypy_value_t *key) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(key);
    tinypy_bool_t valid = kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;

    return valid;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_call_keyword_mapping(tinypy_vm_t *vm, tinypy_value_t *callable, tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_message_part_t name;
    tinypy_message_part_t description;
    tinypy_value_t *mapping;
    tinypy_value_t *keys_method = NULL;
    tinypy_error_t *mapping_error = NULL;
    int32_t mapping_status;
    tinypy_bool_t attribute_error;

    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_DICT) {
        tinypy_value_t *result = tinypy_internal_dict_copy(source, out_error);
        return result;
    }
    mapping_status = tinypy_internal_object_get_optional_attr_key(source, vm->internal_keys_key, &keys_method, &mapping_error);
    if (mapping_status > 0) {
        mapping = tinypy_dict_new(vm);
        tinypy_bool_t updated = tinypy_internal_dict_update_mapping(mapping, source, keys_method, &mapping_error);

        TINYPY_DECREF(keys_method);
        if (updated != 0) {
            return mapping;
        }
        TINYPY_DECREF(mapping);
    }
    attribute_error = vm->raised_type != NULL && TINYPY_VALUE_KIND(vm->raised_type) == TINYPY_VALUE_TYPE
        ? tinypy_type_is_subtype((tinypy_type_t *)vm->raised_type, vm->exception_types[TINYPY_EXCEPTION_ATTRIBUTE_ERROR])
        : (mapping_error != NULL && tinypy_error_kind(mapping_error) == TINYPY_ERROR_ATTRIBUTE);
    if (mapping_status == 0 || attribute_error != 0) {
        if (mapping_error != NULL) {
            tinypy_error_release(mapping_error);
        }
        tinypy_vm_clear_error(vm);
        __tinypy_eval_callable_name(callable, &name, &description);
        tinypy_message_part_t parts[] = {
            name,
            description,
            TINYPY_MESSAGE_PART_LITERAL(" argument after ** must be a mapping, not "),
            TINYPY_MESSAGE_PART_TYPE_NAME(source),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    }
    else if (out_error != NULL) {
        *out_error = mapping_error;
    }
    else if (mapping_error != NULL) {
        tinypy_error_release(mapping_error);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_call_stack(tinypy_vm_t *vm, tinypy_frame_object_t *frame, size_t argument, tinypy_bool_t has_varargs, tinypy_bool_t has_var_keywords, tinypy_error_t **out_error) {
    size_t positional_count = argument & 0xffU;
    size_t keyword_count = (argument >> 8U) & 0xffU;
    size_t consumed = 1U + positional_count + keyword_count * 2U + (has_varargs != 0 ? 1U : 0U) + (has_var_keywords != 0 ? 1U : 0U);
    tinypy_value_t *args = NULL;
    tinypy_value_t *kwargs = NULL;
    tinypy_value_t *result;
    tinypy_bool_t direct_function;
    tinypy_bool_t direct_bound_function;
    tinypy_bool_t direct_native;
    size_t index;

    tinypy_value_t **first = frame->stack_top - consumed;
    direct_function = has_varargs == 0 && first[0]->type == &vm->types[TINYPY_VALUE_FUNCTION];
    direct_bound_function = 0;
    direct_native = has_varargs == 0 && first[0]->type == &vm->types[TINYPY_VALUE_NATIVE_FUNCTION]
        && TINYPY_NATIVE_FUNCTION_OBJECT(first[0])->self != NULL;
    if (has_varargs == 0 && first[0]->type == &vm->types[TINYPY_VALUE_METHOD]) {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(first[0]);

        direct_bound_function = method->self != NULL && method->function->type == &vm->types[TINYPY_VALUE_FUNCTION];
    }
    if (has_var_keywords != 0) {
        size_t mapping_offset = 1U + positional_count + keyword_count * 2U + (has_varargs != 0 ? 1U : 0U);

        kwargs = __tinypy_eval_call_keyword_mapping(vm, first[0], first[mapping_offset], out_error);
        if (kwargs == NULL) {
            result = NULL;
            goto cleanup;
        }
    }
    if (has_varargs != 0) {
        tinypy_value_t *iterable = first[1U + positional_count + keyword_count * 2U];
        tinypy_value_t *star = iterable;

        if (TINYPY_VALUE_KIND(iterable) != TINYPY_VALUE_TUPLE) {
            tinypy_error_t *conversion_error = NULL;
            tinypy_value_t *constructor_args = tinypy_tuple_from_items(vm, &iterable, 1U);

            star = tinypy_internal_tuple_create(&vm->types[TINYPY_VALUE_TUPLE], constructor_args, NULL, &conversion_error);
            TINYPY_DECREF(constructor_args);
            if (star == NULL) {
                if (conversion_error != NULL && tinypy_error_kind(conversion_error) == TINYPY_ERROR_TYPE && TINYPY_VALUE_KIND(iterable) != TINYPY_VALUE_GENERATOR) {
                    tinypy_message_part_t name;
                    tinypy_message_part_t description;

                    tinypy_error_release(conversion_error);
                    tinypy_vm_clear_error(vm);
                    __tinypy_eval_callable_name(first[0], &name, &description);
                    tinypy_message_part_t parts[] = {
                        name,
                        description,
                        TINYPY_MESSAGE_PART_LITERAL(" argument after * must be an iterable, not "),
                        TINYPY_MESSAGE_PART_TYPE_NAME(iterable),
                    };

                    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
                }
                else if (out_error != NULL) {
                    *out_error = conversion_error;
                }
                else if (conversion_error != NULL) {
                    tinypy_error_release(conversion_error);
                }
                result = NULL;
                goto cleanup;
            }
        }
        args = tinypy_internal_tuple_join_items_checked(vm, NULL, first + 1U, positional_count, tinypy_internal_tuple_items(star), TINYPY_TUPLE_SIZE(star), out_error);
        if (star != iterable) {
            TINYPY_DECREF(star);
        }
        if (args == NULL) {
            result = NULL;
            goto cleanup;
        }
    }
    else if (direct_function == 0 && direct_bound_function == 0 && direct_native == 0) {
        args = tinypy_tuple_from_items(vm, first + 1U, positional_count);
    }
    if ((keyword_count != 0U || has_var_keywords != 0)
        && ((direct_function == 0 && direct_bound_function == 0) || has_var_keywords != 0)) {
        if (kwargs == NULL) {
            kwargs = tinypy_dict_new(vm);
        }
        for (index = keyword_count; index != 0U; --index) {
            tinypy_value_t *key = first[1U + positional_count + (index - 1U) * 2U];
            tinypy_value_t *value = first[2U + positional_count + (index - 1U) * 2U];

            if (__tinypy_eval_keyword_name_valid(key) == 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "keywords must be strings", out_error);
                result = NULL;
                goto cleanup;
            }
            tinypy_bool_t contains;
            if (tinypy_internal_dict_contains_checked(vm, kwargs, key, &contains, out_error) == 0) {
                result = NULL;
                goto cleanup;
            }
            if (contains != 0) {
                tinypy_message_part_t name;
                tinypy_message_part_t description;

                __tinypy_eval_callable_name(first[0], &name, &description);
                tinypy_message_part_t parts[] = {
                    name,
                    description,
                    TINYPY_MESSAGE_PART_LITERAL(" got multiple values for keyword argument '"),
                    TINYPY_MESSAGE_PART_TEXT(key),
                    TINYPY_MESSAGE_PART_LITERAL("'"),
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
                result = NULL;
                goto cleanup;
            }
            if (tinypy_internal_dict_set_checked(vm, kwargs, key, value, out_error) == 0) {
                result = NULL;
                goto cleanup;
            }
        }
    }
    if (direct_function != 0) {
        tinypy_value_t *const *keyword_items = first + 1U + positional_count;

        result = __tinypy_eval_function_items_keywords(first[0], first + 1U, positional_count, kwargs, kwargs == NULL ? keyword_items : NULL, kwargs == NULL ? keyword_count : 0U, out_error);
    }
    else if (direct_bound_function != 0) {
        tinypy_value_t *bound_method = first[0];
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(first[0]);
        tinypy_value_t *const *keyword_items = first + 1U + positional_count;

        first[0] = method->self;
        result = __tinypy_eval_function_items_keywords(method->function, first, positional_count + 1U, kwargs, kwargs == NULL ? keyword_items : NULL, kwargs == NULL ? keyword_count : 0U, out_error);
        first[0] = bound_method;
    }
    else if (direct_native != 0) {
        result = tinypy_internal_native_function_call_items(first[0], first + 1U, positional_count, kwargs, out_error);
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

/* Follows build_class in CPython 2.7: the metaclass comes from the namespace,
   then from the first base, and the global __metaclass__ applies only to
   classes without bases. */
static tinypy_value_t *__tinypy_eval_build_class(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_value_t *namespace_dict, tinypy_value_t *bases, tinypy_value_t *name, tinypy_error_t **out_error) {
    static const char metaclass_prefix[] = "Error when calling the metaclass bases\n    ";
    tinypy_value_t *classic_metaclass = &vm->types[TINYPY_VALUE_CLASS].base.base;
    tinypy_value_t *metaclass;
    tinypy_value_t *class_value;

    if (TINYPY_VALUE_KIND(namespace_dict) != TINYPY_VALUE_DICT || TINYPY_VALUE_KIND(bases) != TINYPY_VALUE_TUPLE || TINYPY_VALUE_KIND(name) != TINYPY_VALUE_STRING) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "BUILD_CLASS received invalid operands", out_error);
        return NULL;
    }
    metaclass = tinypy_dict_get_optional(namespace_dict, vm->internal_metaclass_key);
    if (metaclass != NULL) {
        TINYPY_INCREF(metaclass);
    }
    else if (TINYPY_TUPLE_SIZE(bases) != 0U) {
        tinypy_value_t *base = TINYPY_TUPLE_GET(bases, 0U);
        tinypy_error_t *lookup_error = NULL;

        metaclass = tinypy_internal_object_get_attr_key(base, vm->internal_special_class_key, &lookup_error);
        if (metaclass == NULL) {
            if (lookup_error != NULL) {
                tinypy_error_release(lookup_error);
            }
            tinypy_vm_clear_error(vm);
            metaclass = TINYPY_RET(&base->type->base.base);
        }
    }
    else {
        metaclass = tinypy_dict_get_optional(frame->globals, vm->internal_metaclass_key);
        if (metaclass == NULL) {
            metaclass = classic_metaclass;
        }
        TINYPY_INCREF(metaclass);
    }
    if (metaclass == classic_metaclass) {
        size_t name_size;
        const char *name_bytes = (const char *)tinypy_string_view(name, &name_size);

        class_value = tinypy_class_new(name_bytes, name_size, bases, namespace_dict, out_error);
    }
    else {
        tinypy_value_t *class_argument_items[3] = {name, bases, namespace_dict};
        tinypy_value_t *class_arguments = tinypy_tuple_from_items(vm, class_argument_items, 3U);

        class_value = tinypy_call(metaclass, class_arguments, NULL, out_error);
        TINYPY_DECREF(class_arguments);
    }
    if (class_value == NULL && TINYPY_VALUE_KIND(metaclass) == TINYPY_VALUE_TYPE && (((tinypy_type_t *)metaclass)->flags & TINYPY_TYPE_FLAG_HEAP) == 0U) {
        (void)tinypy_internal_exception_prefix_raised(vm, TINYPY_EXCEPTION_TYPE_ERROR, metaclass_prefix, sizeof(metaclass_prefix) - 1U, out_error);
    }
    TINYPY_DECREF(metaclass);
    return class_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_parameter_indices(tinypy_vm_t *vm, tinypy_value_t *code) {
    tinypy_code_object_t *code_object = TINYPY_CODE_OBJECT(code);
    size_t arg_count = (size_t)TINYPY_CODE_ARG_COUNT(code);
    size_t index;

    if (code_object->parameter_indices != NULL) {
        return code_object->parameter_indices;
    }
    code_object->parameter_indices = tinypy_dict_new(vm);
    for (index = 0U; index < arg_count; ++index) {
        tinypy_value_t *parameter_index = tinypy_integer_from_i64(vm, (int64_t)index);

        tinypy_dict_set(code_object->parameter_indices, TINYPY_TUPLE_GET(TINYPY_CODE_VARNAMES(code), index), parameter_index);
        TINYPY_DECREF(parameter_index);
    }
    return code_object->parameter_indices;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_bind_keyword(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_value_t *extra_keywords, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *code = frame->code;
    size_t parameter_index = SIZE_MAX;


    if (__tinypy_eval_keyword_name_valid(key) == 0) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_TEXT(TINYPY_CODE_NAME(code)),
            TINYPY_MESSAGE_PART_LITERAL("() keywords must be strings"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    if (key->type == &vm->types[TINYPY_VALUE_STRING] || key->type == &vm->types[TINYPY_VALUE_UNICODE]) {
        tinypy_value_t *parameter_index_value = tinypy_dict_get_optional(__tinypy_eval_parameter_indices(vm, code), key);

        if (parameter_index_value != NULL) {
            parameter_index = (size_t)TINYPY_INTEGER_VALUE(parameter_index_value);
        }
    }
    else {
        size_t arg_count = (size_t)TINYPY_CODE_ARG_COUNT(code);
        tinypy_value_t *names = TINYPY_CODE_VARNAMES(code);

        /* Keyword subtypes compare to every formal name without hashing. */
        for (size_t index = 0U; index < arg_count; ++index) {
            int32_t comparison = tinypy_compare_bool(key, TINYPY_TUPLE_GET(names, index), TINYPY_COMPARE_EQUAL, out_error);

            if (comparison < 0) {
                return TINYPY_FALSE;
            }
            if (comparison != 0) {
                parameter_index = index;
                break;
            }
        }
    }
    if (parameter_index != SIZE_MAX) {
        if (frame->locals_plus[parameter_index] != NULL) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_TEXT(TINYPY_CODE_NAME(code)),
                TINYPY_MESSAGE_PART_LITERAL("() got multiple values for keyword argument '"),
                TINYPY_MESSAGE_PART_TEXT(key),
                TINYPY_MESSAGE_PART_LITERAL("'"),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            return TINYPY_FALSE;
        }
        frame->locals_plus[parameter_index] = value;
        TINYPY_INCREF(value);
        return TINYPY_TRUE;
    }
    if (extra_keywords != NULL) {
        tinypy_bool_t result = tinypy_internal_dict_set_checked(vm, extra_keywords, key, value, out_error);
        return result;
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_TEXT(TINYPY_CODE_NAME(code)),
        TINYPY_MESSAGE_PART_LITERAL("() got an unexpected keyword argument '"),
        TINYPY_MESSAGE_PART_TEXT(key),
        TINYPY_MESSAGE_PART_LITERAL("'"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* "<name>() takes <qualifier> N argument(s) (M given)", as PyEval_EvalCodeEx
   reports arity mismatches. */
static void __tinypy_eval_make_arity_error(tinypy_vm_t *vm, tinypy_value_t *code, const char *qualifier, size_t qualifier_size, size_t expected, size_t given, tinypy_error_t **out_error) {
    char expected_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
    char given_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
    size_t expected_size = tinypy_internal_format_size(expected_buffer, expected);
    size_t given_size = tinypy_internal_format_size(given_buffer, given);
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_TEXT(TINYPY_CODE_NAME(code)),
        TINYPY_MESSAGE_PART_LITERAL("() takes "),
        {qualifier, qualifier_size},
        {expected_buffer, expected_size},
        TINYPY_MESSAGE_PART_LITERAL(" argument"),
        TINYPY_MESSAGE_PART_LITERAL("s"),
        TINYPY_MESSAGE_PART_LITERAL(" ("),
        {given_buffer, given_size},
        TINYPY_MESSAGE_PART_LITERAL(" given)"),
    };

    if (expected == 1U) {
        parts[5].size = 0U;
    }
    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_bind_arguments(tinypy_vm_t *vm, tinypy_frame_object_t *frame, tinypy_function_object_t *function, tinypy_value_t *const *items, size_t item_count, tinypy_value_t *kwargs, tinypy_value_t *const *keyword_items, size_t keyword_count, tinypy_error_t **out_error) {
    tinypy_value_t *code = frame->code;
    tinypy_value_t *defaults = function->defaults;
    size_t arg_count = (size_t)TINYPY_CODE_ARG_COUNT(code);
    size_t positional_count = item_count;
    size_t default_count = defaults != NULL ? TINYPY_TUPLE_SIZE(defaults) : 0U;
    size_t first_default;
    size_t index;
    tinypy_bool_t has_varargs = (TINYPY_CODE_FLAGS(code) & TINYPY_CODE_VARARGS) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_bool_t has_var_keywords = (TINYPY_CODE_FLAGS(code) & TINYPY_CODE_VAR_KEYWORDS) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_value_t *extra_keywords = NULL;
    tinypy_bool_t result = TINYPY_FALSE;

    size_t default_offset = default_count > arg_count ? default_count - arg_count : 0U;
    default_count -= default_offset;
    first_default = arg_count - default_count;
    if (arg_count == 0U && has_varargs == 0 && has_var_keywords == 0 && (positional_count != 0U || kwargs != NULL || keyword_count != 0U)) {
        size_t given = positional_count + keyword_count + (kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U);

        if (given != 0U) {
            char given_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
            size_t given_size = tinypy_internal_format_size(given_buffer, given);
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_TEXT(TINYPY_CODE_NAME(code)),
                TINYPY_MESSAGE_PART_LITERAL("() takes no arguments ("),
                {given_buffer, given_size},
                TINYPY_MESSAGE_PART_LITERAL(" given)"),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            return TINYPY_FALSE;
        }
    }
    if (positional_count > arg_count && has_varargs == 0) {
        size_t given = positional_count + keyword_count + (kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U);

        if (default_count != 0U) {
            __tinypy_eval_make_arity_error(vm, code, "at most ", 8U, arg_count, given, out_error);
        }
        else {
            __tinypy_eval_make_arity_error(vm, code, "exactly ", 8U, arg_count, given, out_error);
        }
        return TINYPY_FALSE;
    }
    if (defaults != NULL) {
        TINYPY_INCREF(defaults);
    }
    for (index = 0U; index < positional_count && index < arg_count; ++index) {
        frame->locals_plus[index] = TINYPY_RET(items[index]);
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
        tinypy_value_t *snapshot = tinypy_internal_dict_copy(kwargs, out_error);
        if (snapshot == NULL) {
            goto cleanup;
        }
        tinypy_bool_t bound = TINYPY_TRUE;
        for (size_t position = 0U; position <= TINYPY_DICT_OBJECT(snapshot)->mask; ++position) {
            tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(snapshot)->table[position];
            if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry)
                && __tinypy_eval_bind_keyword(vm, frame, extra_keywords, entry->key, entry->value, out_error) == 0) {
                bound = TINYPY_FALSE;
                break;
            }
        }
        TINYPY_DECREF(snapshot);
        if (bound == 0) {
            goto cleanup;
        }
    }
    for (index = 0U; index < keyword_count; ++index) {
        if (__tinypy_eval_bind_keyword(vm, frame, extra_keywords, keyword_items[index * 2U], keyword_items[index * 2U + 1U], out_error) == 0) {
            goto cleanup;
        }
    }

    for (index = 0U; index < arg_count; ++index) {
        if (frame->locals_plus[index] != NULL) {
            continue;
        }
        if (index < first_default) {
            size_t given = 0U;
            size_t bound_index;

            for (bound_index = 0U; bound_index < arg_count; ++bound_index) {
                if (frame->locals_plus[bound_index] != NULL) {
                    given += 1U;
                }
            }
            if (has_varargs != 0 || default_count != 0U) {
                __tinypy_eval_make_arity_error(vm, code, "at least ", 9U, first_default, given, out_error);
            }
            else {
                __tinypy_eval_make_arity_error(vm, code, "exactly ", 8U, first_default, given, out_error);
            }
            goto cleanup;
        }
        frame->locals_plus[index] = TINYPY_RET(TINYPY_TUPLE_GET(defaults, default_offset + index - first_default));
    }
    result = TINYPY_TRUE;
cleanup:
    if (defaults != NULL) {
        TINYPY_DECREF(defaults);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_eval_bind_exact_positional(tinypy_frame_object_t *frame, tinypy_function_object_t *function, tinypy_value_t *const *items, size_t item_count, tinypy_value_t *kwargs, size_t keyword_count) {
    size_t arg_count = (size_t)TINYPY_CODE_ARG_COUNT(function->code);
    size_t index;

    if (item_count != arg_count || kwargs != NULL || keyword_count != 0U || (TINYPY_CODE_FLAGS(function->code) & (TINYPY_CODE_VARARGS | TINYPY_CODE_VAR_KEYWORDS)) != 0) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index < arg_count; ++index) {
        frame->locals_plus[index] = items[index];
        TINYPY_INCREF(items[index]);
    }
    return TINYPY_TRUE;
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
    if (free_count != 0U && function != NULL) {
        for (cell_index = 0U; cell_index < free_count; ++cell_index) {
            tinypy_value_t *cell = TINYPY_TUPLE_GET(function->closure, cell_index);

            frame->locals_plus[local_count + cell_count + cell_index] = cell;
            TINYPY_INCREF(cell);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_code_bound(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals, tinypy_function_object_t *function, tinypy_value_t *const *items, size_t item_count, tinypy_value_t *kwargs, tinypy_value_t *const *keyword_items, size_t keyword_count, tinypy_generator_object_t *generator, tinypy_value_t *send_value, tinypy_value_t *throw_type, tinypy_value_t *throw_value, tinypy_value_t *throw_traceback, tinypy_bool_t *out_yielded, tinypy_error_t **out_error) {
    tinypy_vm_t *vm;
    tinypy_value_t *frame_value;
    tinypy_frame_object_t *frame;
    const uint8_t *bytecode;
    size_t bytecode_size;
    size_t instruction_offset;
    tinypy_value_t *result = NULL;
    tinypy_eval_reason_e reason = TINYPY_EVAL_REASON_NOT;
    int32_t generator_execution = generator != NULL;

    if (out_yielded != NULL) {
        *out_yielded = 0;
    }
    if (generator_execution != 0) {
        frame_value = generator->frame;
        frame = TINYPY_FRAME_OBJECT(frame_value);
        code = frame->code;
        vm = TINYPY_VALUE_VM(code);
        if (tinypy_internal_recursion_check(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded", out_error) == 0) {
            return NULL;
        }
        if (TINYPY_CODE_OBJECT(code)->bytecode_verified == 0 && __tinypy_eval_verify_code(vm, TINYPY_CODE_OBJECT(code), out_error) == 0) {
            return NULL;
        }
        instruction_offset = generator->instruction_offset;
        frame->back = vm->current_frame != NULL ? &vm->current_frame->base.base : NULL;
        if (frame->back != NULL) {
            TINYPY_INCREF(frame->back);
        }
        frame->handled_clear_epoch = vm->handled_clear_epoch;
        frame->handled_state_saved = TINYPY_FALSE;
        frame->previous_handled_type = NULL;
        frame->previous_handled_value = NULL;
        frame->previous_handled_traceback = NULL;
        if (generator->started != 0 && throw_value == NULL) {
            TINYPY_INCREF(send_value);
            __tinypy_eval_push_owned(frame, send_value);
        }
    }
    else {
        vm = TINYPY_VALUE_VM(code);
        if (tinypy_internal_recursion_check(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded", out_error) == 0) {
            return NULL;
        }
        if (TINYPY_CODE_OBJECT(code)->bytecode_verified == 0 && __tinypy_eval_verify_code(vm, TINYPY_CODE_OBJECT(code), out_error) == 0) {
            return NULL;
        }
        if (function == NULL && TINYPY_TUPLE_SIZE(TINYPY_CODE_FREEVARS(code)) != 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "code object passed to exec may not contain free variables", out_error);
            return NULL;
        }
        if (function == NULL && (TINYPY_CODE_FLAGS(code) & TINYPY_CODE_GENERATOR) != 0) {
            tinypy_value_t *callable = tinypy_function_new(code, globals, NULL, NULL);
            result = __tinypy_eval_function_items_keywords(callable, NULL, 0U, NULL, NULL, 0U, out_error);
            TINYPY_DECREF(callable);
            return result;
        }
        instruction_offset = 0U;
        frame_value = function != NULL ? tinypy_internal_frame_new_function(code, globals) : tinypy_frame_new(code, globals, locals);
        if (frame_value == NULL) {
            tinypy_internal_exception_make_diagnostic(vm, out_error);
            return NULL;
        }
        frame = TINYPY_FRAME_OBJECT(frame_value);
        if (function != NULL) {
            if (__tinypy_eval_bind_exact_positional(frame, function, items, item_count, kwargs, keyword_count) == 0 && __tinypy_eval_bind_arguments(vm, frame, function, items, item_count, kwargs, keyword_items, keyword_count, out_error) == 0) {
                TINYPY_DECREF(frame_value);
                return NULL;
            }
        }
        if (TINYPY_TUPLE_SIZE(TINYPY_CODE_CELLVARS(code)) != 0U || TINYPY_TUPLE_SIZE(TINYPY_CODE_FREEVARS(code)) != 0U) {
            __tinypy_eval_initialize_cells(vm, frame, function);
        }
    }
    TINYPY_CLEAR_ERROR(out_error);

    bytecode = TINYPY_STRING_OBJECT(TINYPY_CODE_BYTECODE(code))->bytes;
    bytecode_size = TINYPY_SIZED_SIZE(TINYPY_CODE_BYTECODE(code));
    vm->current_frame = frame;
    vm->evaluation_depth += 1U;
#if defined(TINYPY_DEBUGGER)
    __tinypy_eval_debugger_event(vm, TINYPY_DEBUGGER_EVENT_CALL, frame);
#endif

    if (throw_value != NULL) {
        if (throw_type != NULL) {
            tinypy_internal_exception_set_raised_type(vm, throw_type, throw_value, throw_traceback);
        }
        else {
            tinypy_internal_exception_set_raised(vm, throw_value, throw_traceback);
        }
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
        frame->last_instruction = (int32_t)instruction.offset;
#if defined(TINYPY_DEBUGGER)
        int32_t debugger_line = tinypy_frame_line_number(&frame->base.base);
        if (debugger_line != frame->debugger_line) {
            frame->debugger_line = debugger_line;
            __tinypy_eval_debugger_event(vm, TINYPY_DEBUGGER_EVENT_LINE, frame);
        }
#endif
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
        case TINYPY_OP_PRINT_EXPR: {
            tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *sys_dict = TINYPY_MODULE_OBJECT(vm->sys_module)->dict;
            tinypy_value_t *displayhook = tinypy_internal_dict_get_optional(vm, sys_dict, vm->internal_displayhook_key);
            tinypy_value_t *display_args = NULL;
            tinypy_value_t *display_result = NULL;

            if (displayhook == NULL) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "lost sys.displayhook", out_error);
            }
            else {
                TINYPY_INCREF(displayhook);
                display_args = tinypy_internal_tuple_from_items_checked(vm, &item, 1U, out_error);
                if (display_args != NULL) {
                    display_result = tinypy_call(displayhook, display_args, NULL, out_error);
                    TINYPY_DECREF(display_args);
                }
                TINYPY_DECREF(displayhook);
            }
            TINYPY_DECREF(item);
            if (display_result == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            else {
                TINYPY_DECREF(display_result);
            }
        }
        break;
        case TINYPY_OP_PRINT_ITEM: {
            tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *target = __tinypy_eval_default_output(vm, vm->internal_stdout_key, out_error);
            tinypy_bool_t printed = target != NULL ? __tinypy_eval_print_item(vm, target, item, out_error) : TINYPY_FALSE;

            TINYPY_DECREF(target);
            TINYPY_DECREF(item);
            if (printed == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_PRINT_NEWLINE: {
            tinypy_value_t *target = __tinypy_eval_default_output(vm, vm->internal_stdout_key, out_error);
            tinypy_bool_t printed = target != NULL ? __tinypy_eval_print_newline(target, out_error) : TINYPY_FALSE;

            TINYPY_DECREF(target);
            if (printed == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_PRINT_ITEM_TO: {
            tinypy_value_t *target = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
            if (TINYPY_VALUE_KIND(target) == TINYPY_VALUE_NONE) {
                TINYPY_DECREF(target);
                target = __tinypy_eval_default_output(vm, vm->internal_stdout_key, out_error);
            }
            tinypy_bool_t printed = target != NULL ? __tinypy_eval_print_item(vm, target, item, out_error) : TINYPY_FALSE;

            TINYPY_DECREF(item);
            TINYPY_DECREF(target);
            if (printed == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_PRINT_NEWLINE_TO: {
            tinypy_value_t *target = __tinypy_eval_pop_owned(frame);
            if (TINYPY_VALUE_KIND(target) == TINYPY_VALUE_NONE) {
                TINYPY_DECREF(target);
                target = __tinypy_eval_default_output(vm, vm->internal_stdout_key, out_error);
            }
            tinypy_bool_t printed = target != NULL ? __tinypy_eval_print_newline(target, out_error) : TINYPY_FALSE;

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

            if (__tinypy_eval_exec_statement(vm, frame, source, globals_value, locals_value, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            TINYPY_DECREF(source);
            TINYPY_DECREF(globals_value);
            TINYPY_DECREF(locals_value);
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
            tinypy_value_t *value = TINYPY_RET(__tinypy_eval_peek(frame, 1U));
            __tinypy_eval_push_owned(frame, value);
        }
        break;
        case TINYPY_OP_DUP_TOPX: {
            tinypy_value_t **first;
            size_t index;

            first = frame->stack_top - argument;
            for (index = 0U; index < argument; ++index) {
                tinypy_value_t *value = TINYPY_RET(first[index]);
                __tinypy_eval_push_owned(frame, value);
            }
        }
        break;
        case TINYPY_OP_UNARY_NOT: {
            tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
            int32_t truth = __tinypy_eval_truth(vm, value, out_error);
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
        case TINYPY_OP_UNARY_CONVERT: {
            tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *representation = tinypy_object_repr(value, out_error);

            TINYPY_DECREF(value);
            if (representation == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            else {
                __tinypy_eval_push_owned(frame, representation);
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
            if (__tinypy_eval_binary(vm, frame, tinypy_add, TINYPY_EVAL_INTEGER_BINARY_ADD, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_INPLACE_ADD:
            if (__tinypy_eval_inplace_concat(vm, frame, bytecode, bytecode_size, instruction.next_offset) != 0) {
                break;
            }
            if (__tinypy_eval_binary(vm, frame, tinypy_inplace_add, TINYPY_EVAL_INTEGER_BINARY_ADD, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_SUBTRACT:
            if (__tinypy_eval_binary(vm, frame, tinypy_subtract, TINYPY_EVAL_INTEGER_BINARY_SUBTRACT, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_INPLACE_SUBTRACT:
            if (__tinypy_eval_binary(vm, frame, tinypy_inplace_subtract, TINYPY_EVAL_INTEGER_BINARY_SUBTRACT, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_MULTIPLY:
            if (__tinypy_eval_binary(vm, frame, tinypy_multiply, TINYPY_EVAL_INTEGER_BINARY_MULTIPLY, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_INPLACE_MULTIPLY:
            if (__tinypy_eval_binary(vm, frame, tinypy_inplace_multiply, TINYPY_EVAL_INTEGER_BINARY_MULTIPLY, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_POWER:
            if (__tinypy_eval_binary(vm, frame, tinypy_power, TINYPY_EVAL_INTEGER_BINARY_NONE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_INPLACE_POWER:
            if (__tinypy_eval_binary(vm, frame, tinypy_inplace_power, TINYPY_EVAL_INTEGER_BINARY_NONE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_DIVIDE:
            if (__tinypy_eval_binary(vm, frame, tinypy_divide, TINYPY_EVAL_INTEGER_BINARY_DIVIDE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_INPLACE_DIVIDE:
            if (__tinypy_eval_binary(vm, frame, tinypy_inplace_divide, TINYPY_EVAL_INTEGER_BINARY_DIVIDE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_MODULO:
            if (__tinypy_eval_binary(vm, frame, tinypy_remainder, TINYPY_EVAL_INTEGER_BINARY_NONE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_INPLACE_MODULO:
            if (__tinypy_eval_binary(vm, frame, tinypy_inplace_remainder, TINYPY_EVAL_INTEGER_BINARY_NONE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_FLOOR_DIVIDE:
            if (__tinypy_eval_binary(vm, frame, tinypy_floor_divide, TINYPY_EVAL_INTEGER_BINARY_NONE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_INPLACE_FLOOR_DIVIDE:
            if (__tinypy_eval_binary(vm, frame, tinypy_inplace_floor_divide, TINYPY_EVAL_INTEGER_BINARY_NONE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_TRUE_DIVIDE:
            if (__tinypy_eval_binary(vm, frame, tinypy_true_divide, TINYPY_EVAL_INTEGER_BINARY_DIVIDE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_INPLACE_TRUE_DIVIDE:
            if (__tinypy_eval_binary(vm, frame, tinypy_inplace_true_divide, TINYPY_EVAL_INTEGER_BINARY_DIVIDE, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_LSHIFT:
            if (__tinypy_eval_binary(vm, frame, tinypy_left_shift, TINYPY_EVAL_INTEGER_BINARY_LEFT_SHIFT, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_INPLACE_LSHIFT:
            if (__tinypy_eval_binary(vm, frame, tinypy_inplace_left_shift, TINYPY_EVAL_INTEGER_BINARY_LEFT_SHIFT, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_RSHIFT:
            if (__tinypy_eval_binary(vm, frame, tinypy_right_shift, TINYPY_EVAL_INTEGER_BINARY_RIGHT_SHIFT, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_INPLACE_RSHIFT:
            if (__tinypy_eval_binary(vm, frame, tinypy_inplace_right_shift, TINYPY_EVAL_INTEGER_BINARY_RIGHT_SHIFT, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_AND:
            if (__tinypy_eval_binary(vm, frame, tinypy_bit_and, TINYPY_EVAL_INTEGER_BINARY_AND, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_INPLACE_AND:
            if (__tinypy_eval_binary(vm, frame, tinypy_inplace_bit_and, TINYPY_EVAL_INTEGER_BINARY_AND, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_XOR:
            if (__tinypy_eval_binary(vm, frame, tinypy_bit_xor, TINYPY_EVAL_INTEGER_BINARY_XOR, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_INPLACE_XOR:
            if (__tinypy_eval_binary(vm, frame, tinypy_inplace_bit_xor, TINYPY_EVAL_INTEGER_BINARY_XOR, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BINARY_OR:
            if (__tinypy_eval_binary(vm, frame, tinypy_bit_or, TINYPY_EVAL_INTEGER_BINARY_OR, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_INPLACE_OR:
            if (__tinypy_eval_binary(vm, frame, tinypy_inplace_bit_or, TINYPY_EVAL_INTEGER_BINARY_OR, out_error) == 0) {
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
            tinypy_value_t *start;
            tinypy_value_t *stop;

            __tinypy_eval_pop_slice_bounds(frame, variant, &start, &stop);
            tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *item = tinypy_internal_get_slice(container, start, stop, out_error);

            __tinypy_eval_release_slice_bounds(start, stop);
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
            tinypy_value_t *start;
            tinypy_value_t *stop;

            __tinypy_eval_pop_slice_bounds(frame, variant, &start, &stop);
            tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
            tinypy_bool_t stored = tinypy_internal_set_slice(container, start, stop, value, out_error);

            __tinypy_eval_release_slice_bounds(start, stop);
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
            tinypy_value_t *start;
            tinypy_value_t *stop;

            __tinypy_eval_pop_slice_bounds(frame, variant, &start, &stop);
            tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
            tinypy_bool_t deleted = tinypy_internal_delete_slice(container, start, stop, out_error);

            __tinypy_eval_release_slice_bounds(start, stop);
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
            tinypy_bool_t stored = __tinypy_eval_set_item(vm, container, key, value, out_error);

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
            tinypy_bool_t deleted = tinypy_delete_item(container, key, out_error);

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
            tinypy_value_t *value = TINYPY_RET(TINYPY_TUPLE_GET(code_consts, argument));
            __tinypy_eval_push_owned(frame, value);
        }
        break;
        case TINYPY_OP_LOAD_NAME:
        case TINYPY_OP_LOAD_GLOBAL: {
            tinypy_value_t *code_names = TINYPY_CODE_NAMES(code);
            tinypy_value_t *name = TINYPY_TUPLE_GET(code_names, argument);
            tinypy_value_t *value = __tinypy_eval_lookup_name(vm, frame, name, argument, instruction.opcode == TINYPY_OP_LOAD_NAME, out_error);
            if (value == NULL) {
                if (tinypy_vm_has_error(vm) == 0) {
                    __tinypy_eval_make_name_error(vm, name, instruction.opcode == TINYPY_OP_LOAD_GLOBAL ? TINYPY_TRUE : TINYPY_FALSE, out_error);
                }
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

            if (instruction.opcode == TINYPY_OP_STORE_GLOBAL || mapping->type == &vm->types[TINYPY_VALUE_DICT]) {
                if (tinypy_internal_dict_set_checked(vm, mapping, name, value, out_error) == 0) {
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                }
            }
            else if (tinypy_set_item(mapping, name, value, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            TINYPY_DECREF(value);
        }
        break;
        case TINYPY_OP_DELETE_NAME:
        case TINYPY_OP_DELETE_GLOBAL: {
            tinypy_value_t *code_names = TINYPY_CODE_NAMES(code);
            tinypy_value_t *name = TINYPY_TUPLE_GET(code_names, argument);
            tinypy_value_t *mapping = instruction.opcode == TINYPY_OP_DELETE_NAME ? tinypy_internal_frame_locals(frame) : frame->globals;
            tinypy_bool_t deleted;

            if (instruction.opcode == TINYPY_OP_DELETE_GLOBAL || mapping->type == &vm->types[TINYPY_VALUE_DICT]) {
                tinypy_error_t *delete_error = NULL;

                if (tinypy_internal_dict_delete_optional_checked(vm, mapping, name, &deleted, &delete_error) == 0) {
                    deleted = TINYPY_FALSE;
                }
                if (delete_error != NULL) {
                    tinypy_error_release(delete_error);
                    tinypy_vm_clear_error(vm);
                }
            }
            else {
                tinypy_error_t *delete_error = NULL;

                deleted = tinypy_delete_item(mapping, name, &delete_error);
                if (delete_error != NULL) {
                    tinypy_error_release(delete_error);
                }
                if (deleted == 0) {
                    tinypy_vm_clear_error(vm);
                }
            }
            if (deleted == 0) {
                __tinypy_eval_make_name_error(vm, name, instruction.opcode == TINYPY_OP_DELETE_GLOBAL ? TINYPY_TRUE : TINYPY_FALSE, out_error);
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
        }
        break;
        case TINYPY_OP_LOAD_FAST:
            if (frame->locals_plus[argument] == NULL) {
                __tinypy_eval_make_unbound_local_error(vm, TINYPY_TUPLE_GET(TINYPY_CODE_VARNAMES(code), argument), out_error);
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

            TINYPY_INCREF(cell);
            __tinypy_eval_push_owned(frame, cell);
        }
        break;
        case TINYPY_OP_LOAD_DEREF: {
            size_t local_count = (size_t)TINYPY_CODE_LOCAL_COUNT(code);
            tinypy_value_t *cell = frame->locals_plus[local_count + argument];

            tinypy_value_t *content = tinypy_cell_get(cell);
            if (content == NULL) {
                tinypy_value_t *cellvars = TINYPY_CODE_CELLVARS(code);

                if (argument < TINYPY_TUPLE_SIZE(cellvars)) {
                    __tinypy_eval_make_unbound_local_error(vm, TINYPY_TUPLE_GET(cellvars, argument), out_error);
                }
                else {
                    tinypy_value_t *free_name = TINYPY_TUPLE_GET(TINYPY_CODE_FREEVARS(code), argument - TINYPY_TUPLE_SIZE(cellvars));
                    tinypy_message_part_t parts[] = {
                        TINYPY_MESSAGE_PART_LITERAL("free variable '"),
                        TINYPY_MESSAGE_PART_TEXT(free_name),
                        TINYPY_MESSAGE_PART_LITERAL("' referenced before assignment in enclosing scope"),
                    };

                    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_NAME, parts, sizeof(parts) / sizeof(parts[0]), out_error);
                }
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
                __tinypy_eval_make_unbound_local_error(vm, TINYPY_TUPLE_GET(TINYPY_CODE_VARNAMES(code), argument), out_error);
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
            tinypy_bool_t stored;

            stored = __tinypy_eval_store_attr(vm, code, object, name, argument, attribute_value, out_error);
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
            tinypy_bool_t deleted;

            deleted = tinypy_internal_object_delete_attr_protocol_key(object, name, out_error);
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
            tinypy_value_t *importer = tinypy_internal_dict_get_optional(vm, frame->builtins, vm->internal_import_key);
            tinypy_value_t *module = NULL;
            if (importer == NULL) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_IMPORT, "__import__ not found", out_error);
            }
            else {
                TINYPY_INCREF(importer);
                tinypy_value_t *import_items[5] = {name, frame->globals, tinypy_frame_locals(&frame->base.base), fromlist, level_value};
                tinypy_value_t *import_args = tinypy_internal_tuple_from_items_checked(vm, import_items, TINYPY_INTEGER_VALUE(level_value) == -1 ? 4U : 5U, out_error);
                if (import_args != NULL) {
                    module = tinypy_call(importer, import_args, NULL, out_error);
                    TINYPY_DECREF(import_args);
                }
                TINYPY_DECREF(importer);
            }
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
            tinypy_value_t *imported;

            imported = tinypy_internal_import_from(module, name, out_error);
            if (imported == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, imported);
        }
        break;
        case TINYPY_OP_IMPORT_STAR: {
            tinypy_value_t *module = __tinypy_eval_pop_owned(frame);
            tinypy_bool_t imported = tinypy_internal_import_star(module, tinypy_internal_frame_locals(frame), out_error);

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
                tinypy_bool_t added = tinypy_set_add(set, item, out_error);

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

            if (tinypy_internal_dict_set_checked(vm, dict, key, value, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            TINYPY_DECREF(value);
            TINYPY_DECREF(key);
        }
        break;
        case TINYPY_OP_LIST_APPEND: {
            tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *list = __tinypy_eval_peek(frame, argument);

            if (tinypy_internal_list_append_checked(list, item, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            TINYPY_DECREF(item);
        }
        break;
        case TINYPY_OP_SET_ADD: {
            tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *set = __tinypy_eval_peek(frame, argument);
            tinypy_bool_t added = tinypy_set_add(set, item, out_error);

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

            if (tinypy_internal_dict_set_checked(vm, dict, key, value, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            TINYPY_DECREF(value);
            TINYPY_DECREF(key);
        }
        break;
        case TINYPY_OP_UNPACK_SEQUENCE: {
            tinypy_value_t *sequence = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *iterator;
            tinypy_value_t **unpacked_items = NULL;
            tinypy_error_t *iteration_error = NULL;
            size_t index;

            if (sequence->type == &vm->types[TINYPY_VALUE_TUPLE] || sequence->type == &vm->types[TINYPY_VALUE_LIST]) {
                tinypy_value_t *const *sequence_items = sequence->type == &vm->types[TINYPY_VALUE_TUPLE] ? TINYPY_TUPLE_ITERATOR_BEGIN(sequence) : TINYPY_LIST_OBJECT(sequence)->items;
                size_t sequence_size = sequence->type == &vm->types[TINYPY_VALUE_TUPLE] ? TINYPY_TUPLE_SIZE(sequence) : TINYPY_LIST_SIZE(sequence);

                if (sequence_size == argument) {
                    for (index = argument; index != 0U; index -= 1U) {
                        TINYPY_INCREF(sequence_items[index - 1U]);
                        __tinypy_eval_push_owned(frame, sequence_items[index - 1U]);
                    }
                }
                else if (sequence_size < argument) {
                    __tinypy_eval_make_unpack_error(vm, sequence_size, out_error);
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                }
                else {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "too many values to unpack", out_error);
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                }
                TINYPY_DECREF(sequence);
                break;
            }
            iterator = tinypy_iter(sequence, out_error);
            TINYPY_DECREF(sequence);
            if (iterator == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            if (argument != 0U) {
                if (argument > SIZE_MAX / sizeof(*unpacked_items)) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "unpack temporary storage is too large", out_error);
                    TINYPY_DECREF(iterator);
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                    break;
                }
                unpacked_items = (tinypy_value_t **)tinypy_internal_vm_allocate_checked(vm, (size_t)argument * sizeof(*unpacked_items), out_error);
                if (unpacked_items == NULL) {
                    TINYPY_DECREF(iterator);
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                    break;
                }
            }
            for (index = 0U; index < argument; ++index) {
                unpacked_items[index] = tinypy_next(iterator, &iteration_error);
                if (unpacked_items[index] == NULL) {
                    break;
                }
            }
            if (index != argument) {
                size_t obtained = index;

                while (index != 0U) {
                    TINYPY_DECREF(unpacked_items[--index]);
                }
                if (argument != 0U) {
                    tinypy_internal_vm_deallocate(vm, unpacked_items, argument * sizeof(*unpacked_items));
                }
                TINYPY_DECREF(iterator);
                if (iteration_error != NULL) {
                    if (out_error != NULL) {
                        *out_error = iteration_error;
                    }
                    else {
                        tinypy_error_release(iteration_error);
                    }
                }
                else {
                    __tinypy_eval_make_unpack_error(vm, obtained, out_error);
                }
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            tinypy_value_t *extra = tinypy_next(iterator, &iteration_error);
            TINYPY_DECREF(iterator);
            if (extra != NULL || iteration_error != NULL) {
                if (extra != NULL) {
                    TINYPY_DECREF(extra);
                }
                while (index != 0U) {
                    TINYPY_DECREF(unpacked_items[--index]);
                }
                if (argument != 0U) {
                    tinypy_internal_vm_deallocate(vm, unpacked_items, argument * sizeof(*unpacked_items));
                }
                if (iteration_error != NULL) {
                    if (out_error != NULL) {
                        *out_error = iteration_error;
                    }
                    else {
                        tinypy_error_release(iteration_error);
                    }
                }
                else {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "too many values to unpack", out_error);
                }
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            for (index = argument; index != 0U; index -= 1U) {
                __tinypy_eval_push_owned(frame, unpacked_items[index - 1U]);
            }
            if (argument != 0U) {
                tinypy_internal_vm_deallocate(vm, unpacked_items, argument * sizeof(*unpacked_items));
            }
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
#if !defined(TINYPY_DEBUGGER)
            if (comparison->type == &vm->types[TINYPY_VALUE_BOOL] && instruction_offset < bytecode_size) {
                tinypy_decoded_instruction_t next;

                __tinypy_eval_decode_trusted(bytecode, instruction_offset, &next);
                if (next.opcode == TINYPY_OP_POP_JUMP_IF_FALSE || next.opcode == TINYPY_OP_POP_JUMP_IF_TRUE) {
                    tinypy_bool_t truth = tinypy_bool_as_i32(comparison) != 0;
                    tinypy_bool_t jump = next.opcode == TINYPY_OP_POP_JUMP_IF_TRUE ? truth : truth == 0;

                    frame->last_instruction = (int32_t)next.offset;
                    instruction_offset = jump != 0 ? (size_t)next.argument : next.next_offset;
                    TINYPY_DECREF(comparison);
                    break;
                }
            }
#endif
            __tinypy_eval_push_owned(frame, comparison);
        }
        break;
        case TINYPY_OP_MAKE_FUNCTION: {
            tinypy_value_t *function_code = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *defaults = argument != 0U ? __tinypy_eval_build_sequence(vm, frame, argument, 0) : NULL;

            tinypy_bool_t valid_function = TINYPY_VALUE_KIND(function_code) == TINYPY_VALUE_CODE;
            tinypy_value_t *created_function = NULL;
            if (valid_function != 0) {
                created_function = tinypy_function_new(function_code, frame->globals, defaults, NULL);
            }
            else {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "invalid function operands", out_error);
            }
            if (defaults != NULL) {
                TINYPY_DECREF(defaults);
            }
            TINYPY_DECREF(function_code);
            if (created_function == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, created_function);
        }
        break;
        case TINYPY_OP_MAKE_CLOSURE: {
            tinypy_value_t *function_code = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *closure = __tinypy_eval_pop_owned(frame);
            tinypy_value_t *defaults = argument != 0U ? __tinypy_eval_build_sequence(vm, frame, argument, 0) : NULL;

            tinypy_bool_t valid_function = TINYPY_VALUE_KIND(function_code) == TINYPY_VALUE_CODE && TINYPY_VALUE_KIND(closure) == TINYPY_VALUE_TUPLE && TINYPY_TUPLE_SIZE(closure) == TINYPY_TUPLE_SIZE(TINYPY_CODE_FREEVARS(function_code));
            if (valid_function != 0) {
                for (size_t cell_index = 0U; cell_index < TINYPY_TUPLE_SIZE(closure); ++cell_index) {
                    if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(closure, cell_index)) != TINYPY_VALUE_CELL) {
                        valid_function = TINYPY_FALSE;
                        break;
                    }
                }
            }
            tinypy_value_t *created_function = NULL;
            if (valid_function != 0) {
                created_function = tinypy_function_new(function_code, frame->globals, defaults, closure);
            }
            else {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "invalid function operands", out_error);
            }
            if (defaults != NULL) {
                TINYPY_DECREF(defaults);
            }
            TINYPY_DECREF(closure);
            TINYPY_DECREF(function_code);
            if (created_function == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, created_function);
        }
        break;
        case TINYPY_OP_CALL_FUNCTION:
        case TINYPY_OP_CALL_FUNCTION_VAR:
        case TINYPY_OP_CALL_FUNCTION_KW:
        case TINYPY_OP_CALL_FUNCTION_VAR_KW: {
            tinypy_bool_t has_varargs = instruction.opcode == TINYPY_OP_CALL_FUNCTION_VAR || instruction.opcode == TINYPY_OP_CALL_FUNCTION_VAR_KW ? TINYPY_TRUE : TINYPY_FALSE;
            tinypy_bool_t has_var_keywords = instruction.opcode == TINYPY_OP_CALL_FUNCTION_KW || instruction.opcode == TINYPY_OP_CALL_FUNCTION_VAR_KW ? TINYPY_TRUE : TINYPY_FALSE;
            tinypy_value_t *call_result = __tinypy_eval_call_stack(vm, frame, argument, has_varargs, has_var_keywords, out_error);

            if (call_result == NULL) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
                break;
            }
            __tinypy_eval_push_owned(frame, call_result);
        }
        break;
        case TINYPY_OP_JUMP_FORWARD:
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
            int32_t truth = __tinypy_eval_truth(vm, value, out_error);
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
            int32_t truth = __tinypy_eval_truth(vm, value, out_error);
            tinypy_bool_t jump = instruction.opcode == TINYPY_OP_JUMP_IF_TRUE_OR_POP ? truth != 0 : truth == 0;
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
            (void)__tinypy_eval_push_block(frame, TINYPY_OP_SETUP_LOOP, instruction_offset + argument);
            break;
        case TINYPY_OP_SETUP_EXCEPT:
        case TINYPY_OP_SETUP_FINALLY:
            (void)__tinypy_eval_push_block(frame, (int32_t)instruction.opcode, instruction_offset + argument);
            break;
        case TINYPY_OP_SETUP_WITH:
            if (__tinypy_eval_setup_with(vm, frame, instruction_offset + argument, out_error) == 0) {
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_WITH_CLEANUP:
            reason = __tinypy_eval_with_cleanup(vm, frame, out_error);
            break;
        case TINYPY_OP_POP_BLOCK:
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

        if (reason == TINYPY_EVAL_REASON_EXCEPTION) {
#if defined(TINYPY_DEBUGGER)
            __tinypy_eval_debugger_event(vm, TINYPY_DEBUGGER_EVENT_EXCEPTION, frame);
#endif
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
    else if (reason == TINYPY_EVAL_REASON_RETURN) {
#if defined(TINYPY_DEBUGGER)
        __tinypy_eval_debugger_event(vm, TINYPY_DEBUGGER_EVENT_RETURN, frame);
#endif
    }
    if (generator_execution != 0 && reason == TINYPY_EVAL_REASON_YIELD) {
        generator->instruction_offset = instruction_offset;
        if (out_yielded != NULL) {
            *out_yielded = 1;
        }
    }
    vm->evaluation_depth -= 1U;
    vm->current_frame = frame->back != NULL ? TINYPY_FRAME_OBJECT(frame->back) : NULL;
    if (frame->handled_state_saved != 0 && vm->handled_clear_epoch == frame->handled_clear_epoch) {
        tinypy_internal_exception_restore_handled(vm, frame->previous_handled_type, frame->previous_handled_value, frame->previous_handled_traceback);
    }
    else {
        if (frame->previous_handled_type != NULL) {
            TINYPY_DECREF(frame->previous_handled_type);
        }
        if (frame->previous_handled_value != NULL) {
            TINYPY_DECREF(frame->previous_handled_value);
        }
        if (frame->previous_handled_traceback != NULL) {
            TINYPY_DECREF(frame->previous_handled_traceback);
        }
    }
    frame->handled_state_saved = TINYPY_FALSE;
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
    tinypy_value_t *return_value_1 = __tinypy_eval_code_bound(code, globals, locals, NULL, NULL, 0U, NULL, NULL, 0U, NULL, NULL, NULL, NULL, NULL, NULL, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_exec_code(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(code);
    tinypy_value_t *result = tinypy_eval_code(code, globals, locals, out_error);
    if (result == NULL) {
        return NULL;
    }
    TINYPY_DECREF(result);
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_eval_function(tinypy_value_t *function_value, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = tinypy_internal_eval_function_items(function_value, TINYPY_TUPLE_ITEMS(args), TINYPY_TUPLE_SIZE(args), kwargs, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_eval_function_items(tinypy_value_t *function_value, tinypy_value_t *const *items, size_t item_count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_eval_function_items_keywords(function_value, items, item_count, kwargs, NULL, 0U, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_eval_function_items_keywords(tinypy_value_t *function_value, tinypy_value_t *const *items, size_t item_count, tinypy_value_t *kwargs, tinypy_value_t *const *keyword_items, size_t keyword_count, tinypy_error_t **out_error) {
    tinypy_value_t *result;

    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(function_value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function->code);
    if ((TINYPY_CODE_FLAGS(function->code) & TINYPY_CODE_GENERATOR) != 0) {
        tinypy_value_t *frame_value = tinypy_internal_frame_new_function(function->code, function->globals);
        if (frame_value == NULL) {
            tinypy_internal_exception_make_diagnostic(vm, out_error);
            return NULL;
        }
        tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(frame_value);

        if (__tinypy_eval_bind_exact_positional(frame, function, items, item_count, kwargs, keyword_count) == 0 && __tinypy_eval_bind_arguments(vm, frame, function, items, item_count, kwargs, keyword_items, keyword_count, out_error) == 0) {
            TINYPY_DECREF(frame_value);
            return NULL;
        }
        if (TINYPY_TUPLE_SIZE(TINYPY_CODE_CELLVARS(frame->code)) != 0U || TINYPY_TUPLE_SIZE(TINYPY_CODE_FREEVARS(frame->code)) != 0U) {
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
        result = __tinypy_eval_code_bound(function->code, function->globals, NULL, function, items, item_count, kwargs, keyword_items, keyword_count, NULL, NULL, NULL, NULL, NULL, NULL, out_error);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_eval_generator_resume(tinypy_generator_object_t *generator, tinypy_value_t *send_value, tinypy_value_t *throw_type, tinypy_value_t *throw_value, tinypy_value_t *throw_traceback, tinypy_bool_t *out_yielded, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_eval_code_bound(NULL, NULL, NULL, NULL, NULL, 0U, NULL, NULL, 0U, generator, send_value, throw_type, throw_value, throw_traceback, out_yielded, out_error);
    return return_value_1;
}
