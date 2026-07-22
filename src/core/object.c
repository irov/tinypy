#include "tinypy/object.h"

#include "internal.h"

#include <assert.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_object_name_equal(const char *name, size_t name_size, const char *expected, size_t expected_size) {
    return name_size == expected_size && (name_size == 0U || memcmp(name, expected, name_size) == 0) ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_object_make_attribute_error(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    static const char separator[] = "' object has no attribute '";
    static const char suffix[] = "'";
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    const char *type_name = value->type->name;
    size_t type_name_size = value->type->name_size;
    size_t message_size;
    char *message;

    assert(name != NULL || name_size == 0U);
    assert(type_name != NULL || type_name_size == 0U);
    assert(type_name_size <= SIZE_MAX - 1U - (sizeof(separator) - 1U) - (sizeof(suffix) - 1U) - 1U);
    assert(name_size <= SIZE_MAX - 1U - type_name_size - (sizeof(separator) - 1U) - (sizeof(suffix) - 1U) - 1U);
    message_size = 1U + type_name_size + (sizeof(separator) - 1U) + name_size + (sizeof(suffix) - 1U);
    message = (char *)tinypy_internal_vm_allocate(vm, message_size + 1U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    message[0] = '\'';
    if (type_name_size != 0U) {
        (void)memcpy(message + 1U, type_name, type_name_size);
    }
    (void)memcpy(message + 1U + type_name_size, separator, sizeof(separator) - 1U);
    if (name_size != 0U) {
        (void)memcpy(message + 1U + type_name_size + sizeof(separator) - 1U, name, name_size);
    }
    (void)memcpy(message + 1U + type_name_size + sizeof(separator) - 1U + name_size, suffix, sizeof(suffix));
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, message, out_error);
    tinypy_internal_vm_deallocate(vm, message, message_size + 1U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_object_make_attribute_error_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    const char *name = (const char *)TINYPY_TEXT_BYTES(key);
    size_t name_size = TINYPY_TEXT_BYTE_SIZE(key);

    __tinypy_object_make_attribute_error(value, name, name_size, out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_owned(tinypy_value_t *value) {
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_optional(tinypy_vm_t *vm, tinypy_value_t *value) {
    return value != NULL ? __tinypy_object_owned(value) : tinypy_none_get(vm);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_type_tuple(tinypy_vm_t *vm, tinypy_type_t *type, int32_t mro) {
    size_t size = mro != 0 ? tinypy_type_mro_size(type) : tinypy_type_bases_size(type);
    size_t index;

    if (size == 0U) {
        return tinypy_tuple_from_items(vm, NULL, 0U);
    }
    assert(size <= SIZE_MAX / sizeof(tinypy_value_t *));
    tinypy_value_t **items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, size * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (index = 0U; index < size; ++index) {
        tinypy_type_t *item = (tinypy_type_t *)(mro != 0 ? tinypy_type_mro_at(type, index) : tinypy_type_base_at(type, index));

        items[index] = &item->base.base;
    }
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, size);
    tinypy_internal_vm_deallocate(vm, items, size * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_builtin_attribute(tinypy_value_t *value, const char *name, size_t name_size) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    int32_t dunder_name = name_size >= 2U && name[0] == '_' && name[1] == '_' ? INT32_C(1) : INT32_C(0);

    if (dunder_name != 0) {
        if (__tinypy_object_name_equal(name, name_size, "__class__", 9U) != 0) {
            if (kind == TINYPY_VALUE_OLD_INSTANCE) {
                return __tinypy_object_owned(TINYPY_OLD_INSTANCE_OBJECT(value)->class_object);
            }
            return __tinypy_object_owned(&value->type->base.base);
        }
        if (__tinypy_object_name_equal(name, name_size, "__call__", 8U) != 0 && value->type->call != NULL) {
            return __tinypy_object_owned(value);
        }
        if (value->type->dict_offset != 0U && __tinypy_object_name_equal(name, name_size, "__dict__", 8U) != 0) {
            tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);

            if (value->type->has_instance_dict == 0) {
                return NULL;
            }
            if (*dict_slot == NULL) {
                *dict_slot = tinypy_dict_new(vm);
            }
            return __tinypy_object_owned(*dict_slot);
        }
    }
    if (kind == TINYPY_VALUE_TYPE && dunder_name != 0) {
        tinypy_type_t *type = (tinypy_type_t *)value;

        if (__tinypy_object_name_equal(name, name_size, "__name__", 8U) != 0) {
            return type->name_object != NULL ? __tinypy_object_owned(type->name_object) : tinypy_string_from_bytes(vm, type->name, type->name_size);
        }
        if (__tinypy_object_name_equal(name, name_size, "__dict__", 8U) != 0) {
            return __tinypy_object_owned(type->dict);
        }
        if (__tinypy_object_name_equal(name, name_size, "__bases__", 9U) != 0) {
            return type->bases != NULL ? __tinypy_object_owned(type->bases) : __tinypy_object_type_tuple(vm, type, INT32_C(0));
        }
        if (__tinypy_object_name_equal(name, name_size, "__mro__", 7U) != 0) {
            return type->mro != NULL ? __tinypy_object_owned(type->mro) : __tinypy_object_type_tuple(vm, type, INT32_C(1));
        }
        if (__tinypy_object_name_equal(name, name_size, "__base__", 8U) != 0) {
            return type->base_type != NULL ? __tinypy_object_owned(&type->base_type->base.base) : tinypy_none_get(vm);
        }
        if (__tinypy_object_name_equal(name, name_size, "__flags__", 9U) != 0) {
            return tinypy_integer_from_i64(vm, (type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U ? INT64_C(512) : INT64_C(0));
        }
    }
    if (kind == TINYPY_VALUE_FUNCTION) {
        tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "func_code", 9U) != 0 || __tinypy_object_name_equal(name, name_size, "__code__", 8U) != 0) {
            return __tinypy_object_owned(function->code);
        }
        if (__tinypy_object_name_equal(name, name_size, "func_globals", 12U) != 0 || __tinypy_object_name_equal(name, name_size, "__globals__", 11U) != 0) {
            return __tinypy_object_owned(function->globals);
        }
        if (__tinypy_object_name_equal(name, name_size, "func_defaults", 13U) != 0 || __tinypy_object_name_equal(name, name_size, "__defaults__", 12U) != 0) {
            return __tinypy_object_optional(vm, function->defaults);
        }
        if (__tinypy_object_name_equal(name, name_size, "func_closure", 12U) != 0 || __tinypy_object_name_equal(name, name_size, "__closure__", 11U) != 0) {
            return __tinypy_object_optional(vm, function->closure);
        }
        if (__tinypy_object_name_equal(name, name_size, "func_name", 9U) != 0 || __tinypy_object_name_equal(name, name_size, "__name__", 8U) != 0) {
            return __tinypy_object_owned(function->name);
        }
        if (__tinypy_object_name_equal(name, name_size, "func_doc", 8U) != 0 || __tinypy_object_name_equal(name, name_size, "__doc__", 7U) != 0) {
            return __tinypy_object_optional(vm, function->doc);
        }
        if (__tinypy_object_name_equal(name, name_size, "__module__", 10U) != 0) {
            return __tinypy_object_optional(vm, function->module);
        }
        if (__tinypy_object_name_equal(name, name_size, "func_dict", 9U) != 0 || __tinypy_object_name_equal(name, name_size, "__dict__", 8U) != 0) {
            if (function->dict == NULL) {
                function->dict = tinypy_dict_new(vm);
            }
            return __tinypy_object_owned(function->dict);
        }
    }
    if (kind == TINYPY_VALUE_METHOD) {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "im_func", 7U) != 0) {
            return __tinypy_object_owned(method->function);
        }
        if (__tinypy_object_name_equal(name, name_size, "im_self", 7U) != 0) {
            return __tinypy_object_optional(vm, method->self);
        }
        if (__tinypy_object_name_equal(name, name_size, "im_class", 8U) != 0) {
            return __tinypy_object_owned(method->owner);
        }
    }
    if (kind == TINYPY_VALUE_PARTIAL) {
        tinypy_partial_object_t *partial = TINYPY_PARTIAL_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "func", 4U) != 0) {
            return __tinypy_object_owned(partial->callable);
        }
        if (__tinypy_object_name_equal(name, name_size, "args", 4U) != 0) {
            return __tinypy_object_owned(partial->args);
        }
        if (__tinypy_object_name_equal(name, name_size, "keywords", 8U) != 0) {
            return __tinypy_object_owned(partial->keywords);
        }
    }
    if (kind == TINYPY_VALUE_SRE_PATTERN) {
        tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "pattern", 7U) != 0) {
            return __tinypy_object_owned(pattern->pattern);
        }
        if (__tinypy_object_name_equal(name, name_size, "flags", 5U) != 0) {
            return tinypy_integer_from_i64(vm, pattern->flags);
        }
        if (__tinypy_object_name_equal(name, name_size, "groups", 6U) != 0) {
            return tinypy_integer_from_i64(vm, (int64_t)pattern->groups);
        }
        if (__tinypy_object_name_equal(name, name_size, "groupindex", 10U) != 0) {
            return __tinypy_object_owned(pattern->groupindex);
        }
    }
    if (kind == TINYPY_VALUE_SRE_MATCH) {
        tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(value);
        tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(match->pattern);

        if (__tinypy_object_name_equal(name, name_size, "re", 2U) != 0) {
            return __tinypy_object_owned(match->pattern);
        }
        if (__tinypy_object_name_equal(name, name_size, "string", 6U) != 0) {
            return __tinypy_object_owned(match->string);
        }
        if (__tinypy_object_name_equal(name, name_size, "pos", 3U) != 0) {
            return tinypy_integer_from_i64(vm, (int64_t)match->pos);
        }
        if (__tinypy_object_name_equal(name, name_size, "endpos", 6U) != 0) {
            return tinypy_integer_from_i64(vm, (int64_t)match->endpos);
        }
        if (__tinypy_object_name_equal(name, name_size, "lastindex", 9U) != 0) {
            return match->lastindex >= 0 ? tinypy_integer_from_i64(vm, (int64_t)match->lastindex) : tinypy_none_get(vm);
        }
        if (__tinypy_object_name_equal(name, name_size, "lastgroup", 9U) != 0) {
            if (match->lastindex < 0 || (size_t)match->lastindex >= TINYPY_LIST_SIZE(pattern->indexgroup)) {
                return tinypy_none_get(vm);
            }
            tinypy_value_t *item = TINYPY_LIST_GET(pattern->indexgroup, (size_t)match->lastindex);
            return __tinypy_object_owned(item);
        }
    }
    if (kind == TINYPY_VALUE_CODE) {
        tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "co_argcount", 11U) != 0) {
            return tinypy_integer_from_i64(vm, code->arg_count);
        }
        if (__tinypy_object_name_equal(name, name_size, "co_nlocals", 10U) != 0) {
            return tinypy_integer_from_i64(vm, code->local_count);
        }
        if (__tinypy_object_name_equal(name, name_size, "co_stacksize", 12U) != 0) {
            return tinypy_integer_from_i64(vm, code->stack_size);
        }
        if (__tinypy_object_name_equal(name, name_size, "co_flags", 8U) != 0) {
            return tinypy_integer_from_i64(vm, code->flags);
        }
        if (__tinypy_object_name_equal(name, name_size, "co_code", 7U) != 0) {
            return __tinypy_object_owned(code->bytecode);
        }
        if (__tinypy_object_name_equal(name, name_size, "co_consts", 9U) != 0) {
            return __tinypy_object_owned(code->consts);
        }
        if (__tinypy_object_name_equal(name, name_size, "co_names", 8U) != 0) {
            return __tinypy_object_owned(code->names);
        }
        if (__tinypy_object_name_equal(name, name_size, "co_varnames", 11U) != 0) {
            return __tinypy_object_owned(code->varnames);
        }
        if (__tinypy_object_name_equal(name, name_size, "co_freevars", 11U) != 0) {
            return __tinypy_object_owned(code->freevars);
        }
        if (__tinypy_object_name_equal(name, name_size, "co_cellvars", 11U) != 0) {
            return __tinypy_object_owned(code->cellvars);
        }
        if (__tinypy_object_name_equal(name, name_size, "co_filename", 11U) != 0) {
            return __tinypy_object_owned(code->filename);
        }
        if (__tinypy_object_name_equal(name, name_size, "co_name", 7U) != 0) {
            return __tinypy_object_owned(code->name);
        }
        if (__tinypy_object_name_equal(name, name_size, "co_firstlineno", 14U) != 0) {
            return tinypy_integer_from_i64(vm, code->first_line_number);
        }
        if (__tinypy_object_name_equal(name, name_size, "co_lnotab", 9U) != 0) {
            return __tinypy_object_owned(code->lnotab);
        }
    }
    if (kind == TINYPY_VALUE_FRAME) {
        tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "f_back", 6U) != 0) {
            return __tinypy_object_optional(vm, frame->back);
        }
        if (__tinypy_object_name_equal(name, name_size, "f_code", 6U) != 0) {
            return __tinypy_object_owned(frame->code);
        }
        if (__tinypy_object_name_equal(name, name_size, "f_builtins", 10U) != 0) {
            return __tinypy_object_owned(frame->builtins);
        }
        if (__tinypy_object_name_equal(name, name_size, "f_globals", 9U) != 0) {
            return __tinypy_object_owned(frame->globals);
        }
        if (__tinypy_object_name_equal(name, name_size, "f_locals", 8U) != 0) {
            return __tinypy_object_owned(tinypy_internal_frame_locals(frame));
        }
        if (__tinypy_object_name_equal(name, name_size, "f_lasti", 7U) != 0) {
            return tinypy_integer_from_i64(vm, frame->last_instruction);
        }
        if (__tinypy_object_name_equal(name, name_size, "f_lineno", 8U) != 0) {
            return tinypy_integer_from_i64(vm, tinypy_frame_line_number(value));
        }
    }
    if (kind == TINYPY_VALUE_MODULE && __tinypy_object_name_equal(name, name_size, "__dict__", 8U) != 0) {
        tinypy_value_t *module_dict = tinypy_module_dict(value);
        return __tinypy_object_owned(module_dict);
    }
    if (kind == TINYPY_VALUE_GENERATOR) {
        tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "gi_frame", 8U) != 0) {
            return __tinypy_object_optional(vm, generator->frame);
        }
        if (__tinypy_object_name_equal(name, name_size, "gi_code", 7U) != 0) {
            return generator->frame != NULL ? __tinypy_object_owned(TINYPY_FRAME_OBJECT(generator->frame)->code) : tinypy_none_get(vm);
        }
        if (__tinypy_object_name_equal(name, name_size, "gi_running", 10U) != 0) {
            return tinypy_bool_from_i32(vm, generator->running);
        }
    }
    if (kind == TINYPY_VALUE_TRACEBACK) {
        tinypy_traceback_object_t *traceback = TINYPY_TRACEBACK_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "tb_next", 7U) != 0) {
            return __tinypy_object_optional(vm, traceback->next);
        }
        if (__tinypy_object_name_equal(name, name_size, "tb_frame", 8U) != 0) {
            return __tinypy_object_owned(traceback->frame);
        }
        if (__tinypy_object_name_equal(name, name_size, "tb_lasti", 8U) != 0) {
            return tinypy_integer_from_i64(vm, traceback->last_instruction);
        }
        if (__tinypy_object_name_equal(name, name_size, "tb_lineno", 9U) != 0) {
            return tinypy_integer_from_i64(vm, traceback->line_number);
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_call_attribute_hook(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *hook_key, tinypy_value_t *name_key, tinypy_error_t **out_error) {
    tinypy_value_t *attribute = tinypy_internal_type_lookup_key(vm, value->type, hook_key);
    tinypy_value_t *result;

    assert(attribute != NULL);
    tinypy_value_t *method = tinypy_internal_descriptor_get_value(vm, attribute, value, value->type, out_error);
    if (method == NULL) {
        return NULL;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &name_key, 1U);
    result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_object_has_special(tinypy_value_t *value, const char *name, size_t name_size) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        return tinypy_internal_old_instance_has_special(value, name, name_size);
    }
    return tinypy_type_get_attr(value->type, name, name_size) != NULL ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_descriptor_is_data(tinypy_vm_t *vm, tinypy_value_t *attribute) {
    if (attribute->type->descriptor_set != NULL) {
        return INT32_C(1);
    }
    if ((attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U) {
        return INT32_C(0);
    }
    return tinypy_internal_type_lookup_key(vm, attribute->type, vm->special_set_key) != NULL || tinypy_internal_type_lookup_key(vm, attribute->type, vm->special_delete_key) != NULL ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_descriptor_get_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    if (attribute->type->descriptor_get != NULL) {
        return attribute->type->descriptor_get(attribute, instance, owner, out_error);
    }
    if ((attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_type_lookup_key(vm, attribute->type, vm->special_get_key) != NULL) {
        tinypy_value_t *method = tinypy_internal_object_get_attr_key(attribute, vm->special_get_key, out_error);
        tinypy_value_t *none = NULL;
        tinypy_value_t *items[2];
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) {
            return NULL;
        }
        if (instance == NULL) {
            none = tinypy_none_get(vm);
        }
        items[0] = instance != NULL ? instance : none;
        items[1] = &owner->base.base;
        args = tinypy_tuple_from_items(vm, items, 2U);
        result = tinypy_call(method, args, NULL, out_error);
        TINYPY_DECREF(args);
        if (none != NULL) {
            TINYPY_DECREF(none);
        }
        TINYPY_DECREF(method);
        return result;
    }
    TINYPY_INCREF(attribute);
    return attribute;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_descriptor_set_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (attribute->type->descriptor_set != NULL) {
        return attribute->type->descriptor_set(attribute, instance, value, out_error);
    }
    if ((attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_type_lookup_key(vm, attribute->type, vm->special_set_key) != NULL) {
        tinypy_value_t *method = tinypy_internal_object_get_attr_key(attribute, vm->special_set_key, out_error);
        tinypy_value_t *items[2] = {instance, value};
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) {
            return INT32_C(0);
        }
        args = tinypy_tuple_from_items(vm, items, 2U);
        result = tinypy_call(method, args, NULL, out_error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return INT32_C(0);
        }
        TINYPY_DECREF(result);
        return INT32_C(1);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not support assignment", out_error);
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_descriptor_delete_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_error_t **out_error) {
    if (attribute->type->descriptor_set != NULL) {
        return attribute->type->descriptor_set(attribute, instance, NULL, out_error);
    }
    if ((attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_type_lookup_key(vm, attribute->type, vm->special_delete_key) != NULL) {
        tinypy_value_t *method = tinypy_internal_object_get_attr_key(attribute, vm->special_delete_key, out_error);
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) {
            return INT32_C(0);
        }
        args = tinypy_tuple_from_items(vm, &instance, 1U);
        result = tinypy_call(method, args, NULL, out_error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return INT32_C(0);
        }
        TINYPY_DECREF(result);
        return INT32_C(1);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not support deletion", out_error);
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_instance_attribute(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);
    tinypy_type_t *type = value->type;
    tinypy_value_t *attribute = tinypy_internal_type_lookup_key(vm, type, key);

    if (attribute != NULL && tinypy_internal_descriptor_is_data(vm, attribute) != 0) {
        return tinypy_internal_descriptor_get_value(vm, attribute, value, type, out_error);
    }
    if (dict_slot != NULL && *dict_slot != NULL) {
        tinypy_value_t *stored = tinypy_internal_dict_get_optional(vm, *dict_slot, key);

        if (stored != NULL) {
            TINYPY_INCREF(stored);
            return stored;
        }
    }
    if (attribute != NULL) {
        return tinypy_internal_descriptor_get_value(vm, attribute, value, type, out_error);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_type_attribute(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_type_t *type = (tinypy_type_t *)value;
    tinypy_type_t *metaclass = value->type;
    tinypy_value_t *metaclass_attribute = tinypy_internal_type_lookup_key(vm, metaclass, key);

    if (metaclass_attribute != NULL && tinypy_internal_descriptor_is_data(vm, metaclass_attribute) != 0) {
        return tinypy_internal_descriptor_get_value(vm, metaclass_attribute, value, metaclass, out_error);
    }
    tinypy_value_t *attribute = tinypy_internal_type_lookup_key(vm, type, key);
    if (attribute != NULL) {
        return tinypy_internal_descriptor_get_value(vm, attribute, NULL, type, out_error);
    }
    if (metaclass_attribute != NULL) {
        return tinypy_internal_descriptor_get_value(vm, metaclass_attribute, value, metaclass, out_error);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_object_attribute_error(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    if (vm->raised_type != NULL) {
        return tinypy_type_is_subtype((tinypy_type_t *)vm->raised_type, vm->exception_types[TINYPY_EXCEPTION_ATTRIBUTE_ERROR]);
    }
    return out_error != NULL && *out_error != NULL && tinypy_error_kind(*out_error) == TINYPY_ERROR_ATTRIBUTE ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_object_error_present(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    return vm->raised_type != NULL || (out_error != NULL && *out_error != NULL) ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_object_clear_attribute_error(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    assert(__tinypy_object_attribute_error(vm, out_error) != 0);
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
    }
    tinypy_internal_exception_clear_raised(vm);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_getattr_fallback(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *result, int32_t suppress_missing, int32_t *out_missing, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    const char *name = (const char *)TINYPY_TEXT_BYTES(key);
    size_t name_size = TINYPY_TEXT_BYTE_SIZE(key);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    int32_t has_getattr;

    if (result != NULL) {
        return result;
    }
    has_getattr = (value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && kind != TINYPY_VALUE_TYPE && __tinypy_object_name_equal(name, name_size, "__getattr__", 11U) == 0 && tinypy_internal_type_lookup_key(vm, value->type, vm->special_getattr_key) != NULL ? INT32_C(1) : INT32_C(0);
    if (has_getattr != 0) {
        if (__tinypy_object_error_present(vm, out_error) != 0) {
            if (__tinypy_object_attribute_error(vm, out_error) == 0) {
                return NULL;
            }
            __tinypy_object_clear_attribute_error(vm, out_error);
        }
        result = __tinypy_object_call_attribute_hook(vm, value, vm->special_getattr_key, key, out_error);
        if (result != NULL) {
            return result;
        }
    }
    if (suppress_missing != 0) {
        if (__tinypy_object_error_present(vm, out_error) != 0) {
            if (__tinypy_object_attribute_error(vm, out_error) == 0) {
                return NULL;
            }
            __tinypy_object_clear_attribute_error(vm, out_error);
        }
        *out_missing = INT32_C(1);
        return NULL;
    }
    if (vm->raised_type == NULL && (out_error == NULL || *out_error == NULL)) {
        __tinypy_object_make_attribute_error(value, name, name_size, out_error);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_attr_key(tinypy_value_t *value, tinypy_value_t *key, int32_t suppress_missing, int32_t *out_missing, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    const char *name = (const char *)TINYPY_TEXT_BYTES(key);
    size_t name_size = TINYPY_TEXT_BYTE_SIZE(key);
    tinypy_value_t *result = NULL;
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    TINYPY_CLEAR_ERROR(out_error);
    *out_missing = INT32_C(0);
    result = __tinypy_object_builtin_attribute(value, name, name_size);
    if (result != NULL) {
        return result;
    }
    if ((value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && kind != TINYPY_VALUE_TYPE && __tinypy_object_name_equal(name, name_size, "__getattribute__", 16U) == 0 && tinypy_internal_type_lookup_key(vm, value->type, vm->special_getattribute_key) != NULL) {
        result = __tinypy_object_call_attribute_hook(vm, value, vm->special_getattribute_key, key, out_error);
        return __tinypy_object_getattr_fallback(value, key, result, suppress_missing, out_missing, out_error);
    }
    if (value->type->get_attribute != NULL) {
        result = value->type->get_attribute(value, key, out_error);
        return __tinypy_object_getattr_fallback(value, key, result, suppress_missing, out_missing, out_error);
    }
    if (kind == TINYPY_VALUE_INSTANCE || value->type->dict_offset != 0U) {
        result = __tinypy_internal_instance_attribute(vm, value, key, out_error);
    }
    else if (kind == TINYPY_VALUE_TYPE) {
        result = __tinypy_internal_type_attribute(vm, value, key, out_error);
    }
    else if (kind == TINYPY_VALUE_MODULE) {
        tinypy_value_t *dict = tinypy_module_dict(value);

        result = tinypy_internal_dict_get_optional(vm, dict, key);
        if (result != NULL) {
            TINYPY_INCREF(result);
        }
    }
    else if (kind == TINYPY_VALUE_FUNCTION && TINYPY_FUNCTION_OBJECT(value)->dict != NULL) {
        result = tinypy_internal_dict_get_optional(vm, TINYPY_FUNCTION_OBJECT(value)->dict, key);
        if (result != NULL) {
            TINYPY_INCREF(result);
        }
    }
    else {
        tinypy_value_t *attribute = tinypy_internal_type_lookup_key(vm, value->type, key);

        if (attribute != NULL) {
            result = tinypy_internal_descriptor_get_value(vm, attribute, value, value->type, out_error);
        }
    }
    if (result == NULL && kind == TINYPY_VALUE_METHOD) {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(value);

        return __tinypy_object_get_attr_key(method->function, key, suppress_missing, out_missing, out_error);
    }
    return __tinypy_object_getattr_fallback(value, key, result, suppress_missing, out_missing, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_get_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    int32_t missing;

    return __tinypy_object_get_attr_key(value, key, INT32_C(0), &missing, out_error);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_object_get_optional_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    int32_t missing;

    assert(out_value != NULL);
    *out_value = __tinypy_object_get_attr_key(value, key, INT32_C(1), &missing, out_error);
    if (*out_value != NULL) {
        return INT32_C(1);
    }
    return missing != 0 ? INT32_C(0) : -INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_object_get_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    assert(value != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *result = tinypy_internal_object_get_attr_key(value, key, out_error);
    TINYPY_DECREF(key);
    return result;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_object_has_attr(tinypy_value_t *value, const char *name, size_t name_size) {
    int32_t result;

    assert(value != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    result = tinypy_object_has_attr_value(value, key);
    TINYPY_DECREF(key);
    return result;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_object_has_attr_value(tinypy_value_t *value, tinypy_value_t *name) {
    tinypy_value_t *result;
    int32_t status;

    assert(value != NULL && name != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, name));
    assert(TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(name) == TINYPY_VALUE_UNICODE);
    status = tinypy_internal_object_get_optional_attr_key(value, name, &result, NULL);
    if (result != NULL) {
        TINYPY_DECREF(result);
    }
    if (status < 0) {
        tinypy_internal_exception_clear_raised(vm);
        return INT32_C(0);
    }
    return status;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_object_set_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    assert(tinypy_internal_value_belongs_to(vm, key));
    assert(TINYPY_VALUE_KIND(key) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(key) == TINYPY_VALUE_UNICODE);
    assert(tinypy_internal_value_belongs_to(vm, attribute_value));
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *descriptor = tinypy_internal_type_lookup_key(vm, value->type, key);
    if (descriptor != NULL && tinypy_internal_descriptor_is_data(vm, descriptor) != 0) {
        return tinypy_internal_descriptor_set_value(vm, descriptor, value, attribute_value, out_error);
    }
    if (value->type->set_attribute != NULL) {
        return value->type->set_attribute(value, key, attribute_value, out_error);
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_INSTANCE || value->type->dict_offset != 0U) {
        tinypy_value_t **dict_slot;

        if (value->type->has_instance_dict == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "instance has no dictionary for this attribute", out_error);
            return 0;
        }
        dict_slot = tinypy_internal_object_dict_slot(value);
        assert(dict_slot != NULL);
        if (*dict_slot == NULL) {
            *dict_slot = tinypy_dict_new(vm);
        }
        tinypy_dict_set(*dict_slot, key, attribute_value);
        return 1;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE) {
        tinypy_internal_type_set_attr_key((tinypy_type_t *)value, key, attribute_value);
        return 1;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FUNCTION) {
        if (TINYPY_FUNCTION_OBJECT(value)->dict == NULL) {
            TINYPY_FUNCTION_OBJECT(value)->dict = tinypy_dict_new(vm);
        }
        tinypy_dict_set(TINYPY_FUNCTION_OBJECT(value)->dict, key, attribute_value);
        return 1;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_MODULE) {
        tinypy_dict_set(TINYPY_MODULE_OBJECT(value)->dict, key, attribute_value);
        return 1;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object attributes are read-only", out_error);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_object_set_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    int32_t result;

    assert(value != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    assert(attribute_value != NULL);
    assert(tinypy_internal_value_belongs_to(vm, attribute_value));
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    result = tinypy_internal_object_set_attr_key(value, key, attribute_value, out_error);
    TINYPY_DECREF(key);
    return result;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_object_delete_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *dict = NULL;

    assert(tinypy_internal_value_belongs_to(vm, key));
    assert(TINYPY_VALUE_KIND(key) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(key) == TINYPY_VALUE_UNICODE);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *descriptor = tinypy_internal_type_lookup_key(vm, value->type, key);
    if (descriptor != NULL && tinypy_internal_descriptor_is_data(vm, descriptor) != 0) {
        return tinypy_internal_descriptor_delete_value(vm, descriptor, value, out_error);
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_CLASS) {
        return tinypy_internal_class_delete_attribute(value, key, out_error);
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        return tinypy_internal_old_instance_delete_attribute(value, key, out_error);
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_INSTANCE || value->type->dict_offset != 0U) {
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);

        dict = dict_slot != NULL ? *dict_slot : NULL;
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE) {
        if ((((tinypy_type_t *)value)->flags & TINYPY_TYPE_FLAG_IMMUTABLE) != 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type attributes are read-only", out_error);
            return INT32_C(0);
        }
        dict = ((tinypy_type_t *)value)->dict;
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_MODULE) {
        dict = tinypy_module_dict(value);
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FUNCTION) {
        dict = TINYPY_FUNCTION_OBJECT(value)->dict;
    }
    if (dict == NULL) {
        __tinypy_object_make_attribute_error_key(value, key, out_error);
        return INT32_C(0);
    }
    if (tinypy_internal_dict_delete_optional(vm, dict, key) == 0) {
        __tinypy_object_make_attribute_error_key(value, key, out_error);
        return INT32_C(0);
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE) {
        assert(((tinypy_type_t *)value)->version_tag != UINT64_MAX);
        ((tinypy_type_t *)value)->version_tag += UINT64_C(1);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_object_delete_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    int32_t result;

    assert(value != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    result = tinypy_internal_object_delete_attr_key(value, key, out_error);
    TINYPY_DECREF(key);
    return result;
}
