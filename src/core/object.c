#include "tinypy/object.h"

#include "internal.h"

#include <assert.h>
#include <string.h>

static int32_t __tinypy_object_name_equal(const char *name, size_t name_size, const char *expected, size_t expected_size)
{
    return name_size == expected_size && (name_size == 0U || memcmp(name, expected, name_size) == 0) ? INT32_C(1) : INT32_C(0);
}

static void __tinypy_object_make_attribute_error(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_error_t **out_error)
{
    static const char prefix[] = "object has no attribute '";
    static const char suffix[] = "'";
    size_t message_size;
    char *message;

    assert(name != NULL || name_size == 0U);
    assert(name_size <= SIZE_MAX - (sizeof(prefix) - 1U) - (sizeof(suffix) - 1U) - 1U);
    message_size = (sizeof(prefix) - 1U) + name_size + (sizeof(suffix) - 1U);
    message = (char *)tinypy_internal_vm_allocate(vm, message_size + 1U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    (void)memcpy(message, prefix, sizeof(prefix) - 1U);
    if (name_size != 0U) (void)memcpy(message + sizeof(prefix) - 1U, name, name_size);
    (void)memcpy(message + sizeof(prefix) - 1U + name_size, suffix, sizeof(suffix));
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, message, out_error);
    tinypy_internal_vm_deallocate(vm, message, message_size + 1U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
}

static tinypy_value_t *__tinypy_object_owned(tinypy_value_t *value)
{
    tinypy_retain(value);
    return value;
}

static tinypy_value_t *__tinypy_object_optional(tinypy_vm_t *vm, tinypy_value_t *value)
{
    return value != NULL ? __tinypy_object_owned(value) : tinypy_none_get(vm);
}

static tinypy_value_t *__tinypy_object_type_tuple(tinypy_vm_t *vm, tinypy_type_t *type, int32_t mro)
{
    size_t size = mro != 0 ? tinypy_type_mro_size(type) : tinypy_type_bases_size(type);
    tinypy_value_t **items;
    tinypy_value_t *result;
    size_t index;

    if (size == 0U) return tinypy_tuple_from_items(vm, NULL, 0U);
    assert(size <= SIZE_MAX / sizeof(*items));
    items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, size * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (index = 0U; index < size; index += 1U) {
        tinypy_type_t *item = (tinypy_type_t *)(mro != 0 ? tinypy_type_mro_at(type, index) : tinypy_type_base_at(type, index));

        items[index] = &item->base.base;
    }
    result = tinypy_tuple_from_items(vm, items, size);
    tinypy_internal_vm_deallocate(vm, items, size * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}

static tinypy_value_t *__tinypy_object_builtin_attribute(tinypy_value_t *value, const char *name, size_t name_size)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(value);
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);

    if (__tinypy_object_name_equal(name, name_size, "__class__", 9U) != 0) {
        if (kind == TINYPY_VALUE_OLD_INSTANCE) return __tinypy_object_owned(TINYPY_OLD_INSTANCE_OBJECT(value)->class_object);
        return __tinypy_object_owned(&value->type->base.base);
    }
    if (__tinypy_object_name_equal(name, name_size, "__call__", 8U) != 0 && value->type->call != NULL) return __tinypy_object_owned(value);
    if (value->type->dict_offset != 0U && __tinypy_object_name_equal(name, name_size, "__dict__", 8U) != 0) {
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);

        if (value->type->has_instance_dict == 0) return NULL;
        if (*dict_slot == NULL) *dict_slot = tinypy_dict_new(vm);
        return __tinypy_object_owned(*dict_slot);
    }
    if (kind == TINYPY_VALUE_TYPE) {
        tinypy_type_t *type = (tinypy_type_t *)value;

        if (__tinypy_object_name_equal(name, name_size, "__name__", 8U) != 0) return type->name_object != NULL ? __tinypy_object_owned(type->name_object) : tinypy_string_from_bytes(vm, type->name, type->name_size);
        if (__tinypy_object_name_equal(name, name_size, "__dict__", 8U) != 0) return __tinypy_object_owned(type->dict);
        if (__tinypy_object_name_equal(name, name_size, "__bases__", 9U) != 0) return type->bases != NULL ? __tinypy_object_owned(type->bases) : __tinypy_object_type_tuple(vm, type, INT32_C(0));
        if (__tinypy_object_name_equal(name, name_size, "__mro__", 7U) != 0) return type->mro != NULL ? __tinypy_object_owned(type->mro) : __tinypy_object_type_tuple(vm, type, INT32_C(1));
        if (__tinypy_object_name_equal(name, name_size, "__base__", 8U) != 0) return type->base_type != NULL ? __tinypy_object_owned(&type->base_type->base.base) : tinypy_none_get(vm);
        if (__tinypy_object_name_equal(name, name_size, "__flags__", 9U) != 0) return tinypy_integer_from_i64(vm, (type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U ? INT64_C(512) : INT64_C(0));
    }
    if (kind == TINYPY_VALUE_FUNCTION) {
        tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "func_code", 9U) != 0 || __tinypy_object_name_equal(name, name_size, "__code__", 8U) != 0) return __tinypy_object_owned(function->code);
        if (__tinypy_object_name_equal(name, name_size, "func_globals", 12U) != 0 || __tinypy_object_name_equal(name, name_size, "__globals__", 11U) != 0) return __tinypy_object_owned(function->globals);
        if (__tinypy_object_name_equal(name, name_size, "func_defaults", 13U) != 0 || __tinypy_object_name_equal(name, name_size, "__defaults__", 12U) != 0) return __tinypy_object_optional(vm, function->defaults);
        if (__tinypy_object_name_equal(name, name_size, "func_closure", 12U) != 0 || __tinypy_object_name_equal(name, name_size, "__closure__", 11U) != 0) return __tinypy_object_optional(vm, function->closure);
        if (__tinypy_object_name_equal(name, name_size, "func_name", 9U) != 0 || __tinypy_object_name_equal(name, name_size, "__name__", 8U) != 0) return __tinypy_object_owned(function->name);
        if (__tinypy_object_name_equal(name, name_size, "func_doc", 8U) != 0 || __tinypy_object_name_equal(name, name_size, "__doc__", 7U) != 0) return __tinypy_object_optional(vm, function->doc);
        if (__tinypy_object_name_equal(name, name_size, "__module__", 10U) != 0) return __tinypy_object_optional(vm, function->module);
        if (__tinypy_object_name_equal(name, name_size, "func_dict", 9U) != 0 || __tinypy_object_name_equal(name, name_size, "__dict__", 8U) != 0) {
            if (function->dict == NULL) function->dict = tinypy_dict_new(vm);
            return __tinypy_object_owned(function->dict);
        }
    }
    if (kind == TINYPY_VALUE_METHOD) {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "im_func", 7U) != 0) return __tinypy_object_owned(method->function);
        if (__tinypy_object_name_equal(name, name_size, "im_self", 7U) != 0) return __tinypy_object_optional(vm, method->self);
        if (__tinypy_object_name_equal(name, name_size, "im_class", 8U) != 0) return __tinypy_object_owned(method->owner);
    }
    if (kind == TINYPY_VALUE_PARTIAL) {
        tinypy_partial_object_t *partial = TINYPY_PARTIAL_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "func", 4U) != 0) return __tinypy_object_owned(partial->callable);
        if (__tinypy_object_name_equal(name, name_size, "args", 4U) != 0) return __tinypy_object_owned(partial->args);
        if (__tinypy_object_name_equal(name, name_size, "keywords", 8U) != 0) return __tinypy_object_owned(partial->keywords);
    }
    if (kind == TINYPY_VALUE_SRE_PATTERN) {
        tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "pattern", 7U) != 0) return __tinypy_object_owned(pattern->pattern);
        if (__tinypy_object_name_equal(name, name_size, "flags", 5U) != 0) return tinypy_integer_from_i64(vm, pattern->flags);
        if (__tinypy_object_name_equal(name, name_size, "groups", 6U) != 0) return tinypy_integer_from_i64(vm, (int64_t)pattern->groups);
        if (__tinypy_object_name_equal(name, name_size, "groupindex", 10U) != 0) return __tinypy_object_owned(pattern->groupindex);
    }
    if (kind == TINYPY_VALUE_SRE_MATCH) {
        tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(value);
        tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(match->pattern);

        if (__tinypy_object_name_equal(name, name_size, "re", 2U) != 0) return __tinypy_object_owned(match->pattern);
        if (__tinypy_object_name_equal(name, name_size, "string", 6U) != 0) return __tinypy_object_owned(match->string);
        if (__tinypy_object_name_equal(name, name_size, "pos", 3U) != 0) return tinypy_integer_from_i64(vm, (int64_t)match->pos);
        if (__tinypy_object_name_equal(name, name_size, "endpos", 6U) != 0) return tinypy_integer_from_i64(vm, (int64_t)match->endpos);
        if (__tinypy_object_name_equal(name, name_size, "lastindex", 9U) != 0) return match->lastindex >= 0 ? tinypy_integer_from_i64(vm, (int64_t)match->lastindex) : tinypy_none_get(vm);
        if (__tinypy_object_name_equal(name, name_size, "lastgroup", 9U) != 0) {
            if (match->lastindex < 0 || (size_t)match->lastindex >= tinypy_list_size(pattern->indexgroup)) return tinypy_none_get(vm);
            return __tinypy_object_owned(tinypy_list_get(pattern->indexgroup, (size_t)match->lastindex));
        }
    }
    if (kind == TINYPY_VALUE_CODE) {
        tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "co_argcount", 11U) != 0) return tinypy_integer_from_i64(vm, code->arg_count);
        if (__tinypy_object_name_equal(name, name_size, "co_nlocals", 10U) != 0) return tinypy_integer_from_i64(vm, code->local_count);
        if (__tinypy_object_name_equal(name, name_size, "co_stacksize", 12U) != 0) return tinypy_integer_from_i64(vm, code->stack_size);
        if (__tinypy_object_name_equal(name, name_size, "co_flags", 8U) != 0) return tinypy_integer_from_i64(vm, code->flags);
        if (__tinypy_object_name_equal(name, name_size, "co_code", 7U) != 0) return __tinypy_object_owned(code->bytecode);
        if (__tinypy_object_name_equal(name, name_size, "co_consts", 9U) != 0) return __tinypy_object_owned(code->consts);
        if (__tinypy_object_name_equal(name, name_size, "co_names", 8U) != 0) return __tinypy_object_owned(code->names);
        if (__tinypy_object_name_equal(name, name_size, "co_varnames", 11U) != 0) return __tinypy_object_owned(code->varnames);
        if (__tinypy_object_name_equal(name, name_size, "co_freevars", 11U) != 0) return __tinypy_object_owned(code->freevars);
        if (__tinypy_object_name_equal(name, name_size, "co_cellvars", 11U) != 0) return __tinypy_object_owned(code->cellvars);
        if (__tinypy_object_name_equal(name, name_size, "co_filename", 11U) != 0) return __tinypy_object_owned(code->filename);
        if (__tinypy_object_name_equal(name, name_size, "co_name", 7U) != 0) return __tinypy_object_owned(code->name);
        if (__tinypy_object_name_equal(name, name_size, "co_firstlineno", 14U) != 0) return tinypy_integer_from_i64(vm, code->first_line_number);
        if (__tinypy_object_name_equal(name, name_size, "co_lnotab", 9U) != 0) return __tinypy_object_owned(code->lnotab);
    }
    if (kind == TINYPY_VALUE_FRAME) {
        tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "f_back", 6U) != 0) return __tinypy_object_optional(vm, frame->back);
        if (__tinypy_object_name_equal(name, name_size, "f_code", 6U) != 0) return __tinypy_object_owned(frame->code);
        if (__tinypy_object_name_equal(name, name_size, "f_builtins", 10U) != 0) return __tinypy_object_owned(frame->builtins);
        if (__tinypy_object_name_equal(name, name_size, "f_globals", 9U) != 0) return __tinypy_object_owned(frame->globals);
        if (__tinypy_object_name_equal(name, name_size, "f_locals", 8U) != 0) return __tinypy_object_owned(frame->locals);
        if (__tinypy_object_name_equal(name, name_size, "f_lasti", 7U) != 0) return tinypy_integer_from_i64(vm, frame->last_instruction);
        if (__tinypy_object_name_equal(name, name_size, "f_lineno", 8U) != 0) return tinypy_integer_from_i64(vm, frame->line_number);
    }
    if (kind == TINYPY_VALUE_MODULE && __tinypy_object_name_equal(name, name_size, "__dict__", 8U) != 0) return __tinypy_object_owned(tinypy_module_dict(value));
    if (kind == TINYPY_VALUE_GENERATOR) {
        tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "gi_frame", 8U) != 0) return __tinypy_object_optional(vm, generator->frame);
        if (__tinypy_object_name_equal(name, name_size, "gi_code", 7U) != 0) return generator->frame != NULL ? __tinypy_object_owned(TINYPY_FRAME_OBJECT(generator->frame)->code) : tinypy_none_get(vm);
        if (__tinypy_object_name_equal(name, name_size, "gi_running", 10U) != 0) return tinypy_bool_from_i32(vm, generator->running);
    }
    if (kind == TINYPY_VALUE_TRACEBACK) {
        tinypy_traceback_object_t *traceback = TINYPY_TRACEBACK_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "tb_next", 7U) != 0) return __tinypy_object_optional(vm, traceback->next);
        if (__tinypy_object_name_equal(name, name_size, "tb_frame", 8U) != 0) return __tinypy_object_owned(traceback->frame);
        if (__tinypy_object_name_equal(name, name_size, "tb_lasti", 8U) != 0) return tinypy_integer_from_i64(vm, traceback->last_instruction);
        if (__tinypy_object_name_equal(name, name_size, "tb_lineno", 9U) != 0) return tinypy_integer_from_i64(vm, traceback->line_number);
    }
    return NULL;
}

static tinypy_value_t *__tinypy_object_call_attribute_hook(tinypy_value_t *value, const char *hook_name, size_t hook_name_size, const char *name, size_t name_size, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(value);
    tinypy_value_t *method = tinypy_object_get_attr(value, hook_name, hook_name_size, out_error);
    tinypy_value_t *name_value;
    tinypy_value_t *args;
    tinypy_value_t *result;

    if (method == NULL) return NULL;
    name_value = tinypy_string_from_bytes(vm, name, name_size);
    args = tinypy_tuple_from_items(vm, &name_value, 1U);
    result = tinypy_call(method, args, NULL, out_error);
    tinypy_release(args);
    tinypy_release(name_value);
    tinypy_release(method);
    return result;
}

int32_t tinypy_internal_object_has_special(tinypy_value_t *value, const char *name, size_t name_size)
{
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_OLD_INSTANCE) return tinypy_internal_old_instance_has_special(value, name, name_size);
    return tinypy_type_get_attr(value->type, name, name_size) != NULL ? INT32_C(1) : INT32_C(0);
}

int32_t tinypy_internal_descriptor_is_data(tinypy_value_t *attribute)
{
    return attribute->type->descriptor_set != NULL || tinypy_type_get_attr(attribute->type, "__set__", 7U) != NULL || tinypy_type_get_attr(attribute->type, "__delete__", 10U) != NULL;
}

tinypy_value_t *tinypy_internal_descriptor_get_value(tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error)
{
    if (attribute->type->descriptor_get != NULL) {
        return attribute->type->descriptor_get(attribute, instance, owner, out_error);
    }
    if (tinypy_type_get_attr(attribute->type, "__get__", 7U) != NULL) {
        tinypy_vm_t *vm = tinypy_internal_value_vm(attribute);
        tinypy_value_t *method = tinypy_object_get_attr(attribute, "__get__", 7U, out_error);
        tinypy_value_t *none = NULL;
        tinypy_value_t *items[2];
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) return NULL;
        if (instance == NULL) none = tinypy_none_get(vm);
        items[0] = instance != NULL ? instance : none;
        items[1] = &owner->base.base;
        args = tinypy_tuple_from_items(vm, items, 2U);
        result = tinypy_call(method, args, NULL, out_error);
        tinypy_release(args);
        if (none != NULL) tinypy_release(none);
        tinypy_release(method);
        return result;
    }
    tinypy_retain(attribute);
    return attribute;
}

int32_t tinypy_internal_descriptor_set_value(tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error)
{
    if (attribute->type->descriptor_set != NULL) return attribute->type->descriptor_set(attribute, instance, value, out_error);
    if (tinypy_type_get_attr(attribute->type, "__set__", 7U) != NULL) {
        tinypy_vm_t *vm = tinypy_internal_value_vm(attribute);
        tinypy_value_t *method = tinypy_object_get_attr(attribute, "__set__", 7U, out_error);
        tinypy_value_t *items[2] = {instance, value};
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) return INT32_C(0);
        args = tinypy_tuple_from_items(vm, items, 2U);
        result = tinypy_call(method, args, NULL, out_error);
        tinypy_release(args);
        tinypy_release(method);
        if (result == NULL) return INT32_C(0);
        tinypy_release(result);
        return INT32_C(1);
    }
    tinypy_internal_make_vm_error(tinypy_internal_value_vm(attribute), TINYPY_ERROR_TYPE, "descriptor does not support assignment", out_error);
    return INT32_C(0);
}

int32_t tinypy_internal_descriptor_delete_value(tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_error_t **out_error)
{
    if (attribute->type->descriptor_set != NULL) return attribute->type->descriptor_set(attribute, instance, NULL, out_error);
    if (tinypy_type_get_attr(attribute->type, "__delete__", 10U) != NULL) {
        tinypy_vm_t *vm = tinypy_internal_value_vm(attribute);
        tinypy_value_t *method = tinypy_object_get_attr(attribute, "__delete__", 10U, out_error);
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) return INT32_C(0);
        args = tinypy_tuple_from_items(vm, &instance, 1U);
        result = tinypy_call(method, args, NULL, out_error);
        tinypy_release(args);
        tinypy_release(method);
        if (result == NULL) return INT32_C(0);
        tinypy_release(result);
        return INT32_C(1);
    }
    tinypy_internal_make_vm_error(tinypy_internal_value_vm(attribute), TINYPY_ERROR_TYPE, "descriptor does not support deletion", out_error);
    return INT32_C(0);
}

static tinypy_value_t *__tinypy_internal_instance_attribute(tinypy_value_t *value, const char *name, size_t name_size, tinypy_value_t *key, tinypy_error_t **out_error)
{
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);
    tinypy_type_t *type = value->type;
    tinypy_value_t *attribute = tinypy_type_get_attr(type, name, name_size);

    if (attribute != NULL && tinypy_internal_descriptor_is_data(attribute) != 0) {
        return tinypy_internal_descriptor_get_value(attribute, value, type, out_error);
    }
    if (dict_slot != NULL && *dict_slot != NULL && tinypy_dict_contains(*dict_slot, key) != 0) {
        tinypy_value_t *stored = tinypy_dict_get(*dict_slot, key);

        tinypy_retain(stored);
        return stored;
    }
    if (attribute != NULL) {
        return tinypy_internal_descriptor_get_value(attribute, value, type, out_error);
    }
    return NULL;
}

static tinypy_value_t *__tinypy_internal_type_attribute(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error)
{
    tinypy_type_t *type = (tinypy_type_t *)value;
    tinypy_type_t *metaclass = value->type;
    tinypy_value_t *metaclass_attribute = tinypy_type_get_attr(metaclass, name, name_size);
    tinypy_value_t *attribute;

    if (metaclass_attribute != NULL && tinypy_internal_descriptor_is_data(metaclass_attribute) != 0) {
        return tinypy_internal_descriptor_get_value(metaclass_attribute, value, metaclass, out_error);
    }
    attribute = tinypy_type_get_attr(type, name, name_size);
    if (attribute != NULL) {
        return tinypy_internal_descriptor_get_value(attribute, NULL, type, out_error);
    }
    if (metaclass_attribute != NULL) {
        return tinypy_internal_descriptor_get_value(metaclass_attribute, value, metaclass, out_error);
    }
    return NULL;
}

tinypy_value_t *tinypy_object_get_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm;
    tinypy_value_t *key;
    tinypy_value_t *result = NULL;
    tinypy_value_type_e kind;

    assert(value != NULL);
    vm = tinypy_internal_value_vm(value);
    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    tinypy_internal_clear_error(out_error);
    kind = tinypy_internal_value_kind(value);
    result = __tinypy_object_builtin_attribute(value, name, name_size);
    if (result != NULL) return result;
    if ((value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && kind != TINYPY_VALUE_TYPE && __tinypy_object_name_equal(name, name_size, "__getattribute__", 16U) == 0 && tinypy_type_get_attr(value->type, "__getattribute__", 16U) != NULL) {
        return __tinypy_object_call_attribute_hook(value, "__getattribute__", 16U, name, name_size, out_error);
    }
    key = tinypy_string_from_bytes(vm, name, name_size);
    if (value->type->get_attribute != NULL) {
        result = value->type->get_attribute(value, key, out_error);
        tinypy_release(key);
        if (result == NULL && (out_error == NULL || *out_error == NULL)) {
            __tinypy_object_make_attribute_error(vm, name, name_size, out_error);
        }
        return result;
    }
    if (kind == TINYPY_VALUE_INSTANCE || value->type->dict_offset != 0U) {
        result = __tinypy_internal_instance_attribute(value, name, name_size, key, out_error);
    } else if (kind == TINYPY_VALUE_TYPE) {
        result = __tinypy_internal_type_attribute(value, name, name_size, out_error);
    } else if (kind == TINYPY_VALUE_MODULE) {
        tinypy_value_t *dict = tinypy_module_dict(value);

        if (tinypy_dict_contains(dict, key) != 0) {
            result = tinypy_dict_get(dict, key);
            tinypy_retain(result);
        }
    } else if (kind == TINYPY_VALUE_FUNCTION && TINYPY_FUNCTION_OBJECT(value)->dict != NULL && tinypy_dict_contains(TINYPY_FUNCTION_OBJECT(value)->dict, key) != 0) {
        result = tinypy_dict_get(TINYPY_FUNCTION_OBJECT(value)->dict, key);
        tinypy_retain(result);
    } else {
        tinypy_value_t *attribute = tinypy_type_get_attr(value->type, name, name_size);

        if (attribute != NULL) {
            result = tinypy_internal_descriptor_get_value(attribute, value, value->type, out_error);
        }
    }
    tinypy_release(key);
    if (result == NULL && (value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && kind != TINYPY_VALUE_TYPE && __tinypy_object_name_equal(name, name_size, "__getattr__", 11U) == 0 && tinypy_type_get_attr(value->type, "__getattr__", 11U) != NULL) {
        return __tinypy_object_call_attribute_hook(value, "__getattr__", 11U, name, name_size, out_error);
    }
    if (result == NULL && (out_error == NULL || *out_error == NULL)) {
        __tinypy_object_make_attribute_error(vm, name, name_size, out_error);
    }
    return result;
}

int32_t tinypy_object_set_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_value_t *attribute_value, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm;
    tinypy_value_t *descriptor;

    assert(value != NULL);
    vm = tinypy_internal_value_vm(value);
    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    assert(attribute_value != NULL);
    assert(tinypy_internal_value_belongs_to(vm, attribute_value));
    tinypy_internal_clear_error(out_error);
    descriptor = tinypy_type_get_attr(value->type, name, name_size);
    if (descriptor != NULL && tinypy_internal_descriptor_is_data(descriptor) != 0) {
        return tinypy_internal_descriptor_set_value(descriptor, value, attribute_value, out_error);
    }
    if (value->type->set_attribute != NULL) {
        tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
        int32_t result = value->type->set_attribute(value, key, attribute_value, out_error);

        tinypy_release(key);
        return result;
    }
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_INSTANCE || value->type->dict_offset != 0U) {
        tinypy_value_t **dict_slot;
        tinypy_value_t *key;

        if (value->type->has_instance_dict == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "instance has no dictionary for this attribute", out_error);
            return 0;
        }
        dict_slot = tinypy_internal_object_dict_slot(value);
        assert(dict_slot != NULL);
        if (*dict_slot == NULL) *dict_slot = tinypy_dict_new(vm);
        key = tinypy_string_from_bytes(vm, name, name_size);
        tinypy_dict_set(*dict_slot, key, attribute_value);
        tinypy_release(key);
        return 1;
    }
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_TYPE) {
        tinypy_type_set_attr((tinypy_type_t *)value, name, name_size, attribute_value);
        return 1;
    }
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_FUNCTION) {
        tinypy_value_t *key;

        if (TINYPY_FUNCTION_OBJECT(value)->dict == NULL) TINYPY_FUNCTION_OBJECT(value)->dict = tinypy_dict_new(vm);
        key = tinypy_string_from_bytes(vm, name, name_size);
        tinypy_dict_set(TINYPY_FUNCTION_OBJECT(value)->dict, key, attribute_value);
        tinypy_release(key);
        return 1;
    }
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_MODULE) {
        tinypy_module_add_value(value, name, name_size, attribute_value);
        return 1;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object attributes are read-only", out_error);
    return 0;
}

int32_t tinypy_object_delete_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm;
    tinypy_value_t *descriptor;
    tinypy_value_t *dict = NULL;
    tinypy_value_t *key;

    assert(value != NULL);
    vm = tinypy_internal_value_vm(value);
    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    tinypy_internal_clear_error(out_error);
    descriptor = tinypy_type_get_attr(value->type, name, name_size);
    if (descriptor != NULL && tinypy_internal_descriptor_is_data(descriptor) != 0) return tinypy_internal_descriptor_delete_value(descriptor, value, out_error);
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_CLASS) return tinypy_internal_class_delete_attribute(value, name, name_size, out_error);
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_OLD_INSTANCE) return tinypy_internal_old_instance_delete_attribute(value, name, name_size, out_error);
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_INSTANCE || value->type->dict_offset != 0U) {
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);

        dict = dict_slot != NULL ? *dict_slot : NULL;
    }
    else if (tinypy_internal_value_kind(value) == TINYPY_VALUE_TYPE) {
        if ((((tinypy_type_t *)value)->flags & TINYPY_TYPE_FLAG_IMMUTABLE) != 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type attributes are read-only", out_error);
            return INT32_C(0);
        }
        dict = ((tinypy_type_t *)value)->dict;
    } else if (tinypy_internal_value_kind(value) == TINYPY_VALUE_MODULE) dict = tinypy_module_dict(value);
    else if (tinypy_internal_value_kind(value) == TINYPY_VALUE_FUNCTION) dict = TINYPY_FUNCTION_OBJECT(value)->dict;
    if (dict == NULL) {
        __tinypy_object_make_attribute_error(vm, name, name_size, out_error);
        return INT32_C(0);
    }
    key = tinypy_string_from_bytes(vm, name, name_size);
    if (tinypy_dict_contains(dict, key) == 0) {
        tinypy_release(key);
        __tinypy_object_make_attribute_error(vm, name, name_size, out_error);
        return INT32_C(0);
    }
    tinypy_dict_delete(dict, key);
    tinypy_release(key);
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_TYPE) {
        assert(((tinypy_type_t *)value)->version_tag != UINT64_MAX);
        ((tinypy_type_t *)value)->version_tag += UINT64_C(1);
    }
    return INT32_C(1);
}
