#include "tinypy/object.h"
#include "tinypy/representation.h"

#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_object_name_equal(const char *name, size_t name_size, const char *expected, size_t expected_size) {
    tinypy_bool_t return_value_1 = name_size == expected_size && (name_size == 0U || memcmp(name, expected, name_size) == 0) ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_object_key_text(tinypy_value_t *key, const char **out_name, size_t *out_name_size) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(key);

    if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
        return TINYPY_FALSE;
    }
    *out_name = (const char *)TINYPY_TEXT_BYTES(key);
    *out_name_size = TINYPY_TEXT_BYTE_SIZE(key);
    return TINYPY_TRUE;
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

    message_size = 1U + type_name_size + (sizeof(separator) - 1U) + name_size + (sizeof(suffix) - 1U);
    message = (char *)tinypy_internal_vm_allocate(vm, message_size + 1U);
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
    tinypy_internal_vm_deallocate(vm, message, message_size + 1U);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_object_make_attribute_error_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    const char *name;
    size_t name_size;

    if (__tinypy_object_key_text(key, &name, &name_size) != 0) {
        __tinypy_object_make_attribute_error(value, name, name_size, out_error);
        return;
    }

    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_error_t *representation_error = NULL;
    tinypy_value_t *representation = tinypy_object_repr(key, &representation_error);

    if (representation != NULL) {
        name = (const char *)TINYPY_TEXT_BYTES(representation);
        name_size = TINYPY_TEXT_BYTE_SIZE(representation);
        __tinypy_object_make_attribute_error(value, name, name_size, out_error);
        TINYPY_DECREF(representation);
        return;
    }
    if (representation_error != NULL) {
        tinypy_error_release(representation_error);
    }
    tinypy_internal_exception_clear_raised(vm);
    __tinypy_object_make_attribute_error(value, "<native>", 8U, out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_owned(tinypy_value_t *value) {
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_optional(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_value_t *return_value_1 = value != NULL ? __tinypy_object_owned(value) : tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_type_tuple(tinypy_vm_t *vm, tinypy_type_t *type, int32_t mro) {
    size_t size = mro != 0 ? tinypy_type_mro_size(type) : tinypy_type_bases_size(type);
    size_t index;

    if (size == 0U) {
        tinypy_value_t *return_value_1 = tinypy_tuple_from_items(vm, NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t **items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, size * sizeof(*items));
    for (index = 0U; index < size; ++index) {
        tinypy_type_t *item = (tinypy_type_t *)(mro != 0 ? tinypy_type_mro_at(type, index) : tinypy_type_base_at(type, index));

        items[index] = &item->base.base;
    }
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, size);
    tinypy_internal_vm_deallocate(vm, items, size * sizeof(*items));
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
                tinypy_value_t *return_value_1 = __tinypy_object_owned(TINYPY_OLD_INSTANCE_OBJECT(value)->class_object);
                return return_value_1;
            }
            tinypy_value_t *return_value_2 = __tinypy_object_owned(&value->type->base.base);
            return return_value_2;
        }
        if (__tinypy_object_name_equal(name, name_size, "__call__", 8U) != 0 && value->type->call != NULL) {
            tinypy_value_t *return_value_3 = __tinypy_object_owned(value);
            return return_value_3;
        }
        if (value->type->dict_offset != 0U && __tinypy_object_name_equal(name, name_size, "__dict__", 8U) != 0) {
            tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);

            if (value->type->has_instance_dict == 0) {
                return NULL;
            }
            if (*dict_slot == NULL) {
                *dict_slot = tinypy_dict_new(vm);
            }
            tinypy_value_t *return_value_4 = __tinypy_object_owned(*dict_slot);
            return return_value_4;
        }
    }
    if (kind == TINYPY_VALUE_TYPE && dunder_name != 0) {
        tinypy_type_t *type = (tinypy_type_t *)value;

        if (__tinypy_object_name_equal(name, name_size, "__name__", 8U) != 0) {
            tinypy_value_t *return_value_5 = type->name_object != NULL ? __tinypy_object_owned(type->name_object) : tinypy_string_from_bytes(vm, type->name, type->name_size);
            return return_value_5;
        }
        if (__tinypy_object_name_equal(name, name_size, "__dict__", 8U) != 0) {
            tinypy_value_t *return_value_6 = __tinypy_object_owned(type->dict);
            return return_value_6;
        }
        if (__tinypy_object_name_equal(name, name_size, "__bases__", 9U) != 0) {
            tinypy_value_t *return_value_7 = type->bases != NULL ? __tinypy_object_owned(type->bases) : __tinypy_object_type_tuple(vm, type, INT32_C(0));
            return return_value_7;
        }
        if (__tinypy_object_name_equal(name, name_size, "__mro__", 7U) != 0) {
            tinypy_value_t *return_value_8 = type->mro != NULL ? __tinypy_object_owned(type->mro) : __tinypy_object_type_tuple(vm, type, INT32_C(1));
            return return_value_8;
        }
        if (__tinypy_object_name_equal(name, name_size, "__base__", 8U) != 0) {
            tinypy_value_t *return_value_9 = type->base_type != NULL ? __tinypy_object_owned(&type->base_type->base.base) : tinypy_none_get(vm);
            return return_value_9;
        }
        if (__tinypy_object_name_equal(name, name_size, "__flags__", 9U) != 0) {
            tinypy_value_t *return_value_10 = tinypy_integer_from_i64(vm, (type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U ? INT64_C(512) : INT64_C(0));
            return return_value_10;
        }
    }
    if (kind == TINYPY_VALUE_FUNCTION) {
        tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "func_code", 9U) != 0 || __tinypy_object_name_equal(name, name_size, "__code__", 8U) != 0) {
            tinypy_value_t *return_value_11 = __tinypy_object_owned(function->code);
            return return_value_11;
        }
        if (__tinypy_object_name_equal(name, name_size, "func_globals", 12U) != 0 || __tinypy_object_name_equal(name, name_size, "__globals__", 11U) != 0) {
            tinypy_value_t *return_value_12 = __tinypy_object_owned(function->globals);
            return return_value_12;
        }
        if (__tinypy_object_name_equal(name, name_size, "func_defaults", 13U) != 0 || __tinypy_object_name_equal(name, name_size, "__defaults__", 12U) != 0) {
            tinypy_value_t *return_value_13 = __tinypy_object_optional(vm, function->defaults);
            return return_value_13;
        }
        if (__tinypy_object_name_equal(name, name_size, "func_closure", 12U) != 0 || __tinypy_object_name_equal(name, name_size, "__closure__", 11U) != 0) {
            tinypy_value_t *return_value_14 = __tinypy_object_optional(vm, function->closure);
            return return_value_14;
        }
        if (__tinypy_object_name_equal(name, name_size, "func_name", 9U) != 0 || __tinypy_object_name_equal(name, name_size, "__name__", 8U) != 0) {
            tinypy_value_t *return_value_15 = __tinypy_object_owned(function->name);
            return return_value_15;
        }
        if (__tinypy_object_name_equal(name, name_size, "func_doc", 8U) != 0 || __tinypy_object_name_equal(name, name_size, "__doc__", 7U) != 0) {
            tinypy_value_t *return_value_16 = __tinypy_object_optional(vm, function->doc);
            return return_value_16;
        }
        if (__tinypy_object_name_equal(name, name_size, "__module__", 10U) != 0) {
            tinypy_value_t *return_value_17 = __tinypy_object_optional(vm, function->module);
            return return_value_17;
        }
        if (__tinypy_object_name_equal(name, name_size, "func_dict", 9U) != 0 || __tinypy_object_name_equal(name, name_size, "__dict__", 8U) != 0) {
            if (function->dict == NULL) {
                function->dict = tinypy_dict_new(vm);
            }
            tinypy_value_t *return_value_18 = __tinypy_object_owned(function->dict);
            return return_value_18;
        }
    }
    if (kind == TINYPY_VALUE_METHOD) {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "im_func", 7U) != 0) {
            tinypy_value_t *return_value_19 = __tinypy_object_owned(method->function);
            return return_value_19;
        }
        if (__tinypy_object_name_equal(name, name_size, "im_self", 7U) != 0) {
            tinypy_value_t *return_value_20 = __tinypy_object_optional(vm, method->self);
            return return_value_20;
        }
        if (__tinypy_object_name_equal(name, name_size, "im_class", 8U) != 0) {
            tinypy_value_t *return_value_21 = __tinypy_object_owned(method->owner);
            return return_value_21;
        }
    }
    if (kind == TINYPY_VALUE_PARTIAL) {
        tinypy_partial_object_t *partial = TINYPY_PARTIAL_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "func", 4U) != 0) {
            tinypy_value_t *return_value_22 = __tinypy_object_owned(partial->callable);
            return return_value_22;
        }
        if (__tinypy_object_name_equal(name, name_size, "args", 4U) != 0) {
            tinypy_value_t *return_value_23 = __tinypy_object_owned(partial->args);
            return return_value_23;
        }
        if (__tinypy_object_name_equal(name, name_size, "keywords", 8U) != 0) {
            tinypy_value_t *return_value_24 = __tinypy_object_owned(partial->keywords);
            return return_value_24;
        }
    }
    if (kind == TINYPY_VALUE_SRE_PATTERN) {
        tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "pattern", 7U) != 0) {
            tinypy_value_t *return_value_25 = __tinypy_object_owned(pattern->pattern);
            return return_value_25;
        }
        if (__tinypy_object_name_equal(name, name_size, "flags", 5U) != 0) {
            tinypy_value_t *return_value_26 = tinypy_integer_from_i64(vm, pattern->flags);
            return return_value_26;
        }
        if (__tinypy_object_name_equal(name, name_size, "groups", 6U) != 0) {
            tinypy_value_t *return_value_27 = tinypy_integer_from_i64(vm, (int64_t)pattern->groups);
            return return_value_27;
        }
        if (__tinypy_object_name_equal(name, name_size, "groupindex", 10U) != 0) {
            tinypy_value_t *return_value_28 = __tinypy_object_owned(pattern->groupindex);
            return return_value_28;
        }
    }
    if (kind == TINYPY_VALUE_SRE_MATCH) {
        tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(value);
        tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(match->pattern);

        if (__tinypy_object_name_equal(name, name_size, "re", 2U) != 0) {
            tinypy_value_t *return_value_29 = __tinypy_object_owned(match->pattern);
            return return_value_29;
        }
        if (__tinypy_object_name_equal(name, name_size, "string", 6U) != 0) {
            tinypy_value_t *return_value_30 = __tinypy_object_owned(match->string);
            return return_value_30;
        }
        if (__tinypy_object_name_equal(name, name_size, "pos", 3U) != 0) {
            tinypy_value_t *return_value_31 = tinypy_integer_from_i64(vm, (int64_t)match->pos);
            return return_value_31;
        }
        if (__tinypy_object_name_equal(name, name_size, "endpos", 6U) != 0) {
            tinypy_value_t *return_value_32 = tinypy_integer_from_i64(vm, (int64_t)match->endpos);
            return return_value_32;
        }
        if (__tinypy_object_name_equal(name, name_size, "lastindex", 9U) != 0) {
            tinypy_value_t *return_value_33 = match->lastindex >= 0 ? tinypy_integer_from_i64(vm, (int64_t)match->lastindex) : tinypy_none_get(vm);
            return return_value_33;
        }
        if (__tinypy_object_name_equal(name, name_size, "lastgroup", 9U) != 0) {
            if (match->lastindex < 0 || (size_t)match->lastindex >= TINYPY_LIST_SIZE(pattern->indexgroup)) {
                tinypy_value_t *return_value_34 = tinypy_none_get(vm);
                return return_value_34;
            }
            tinypy_value_t *item = TINYPY_LIST_GET(pattern->indexgroup, (size_t)match->lastindex);
            tinypy_value_t *return_value_35 = __tinypy_object_owned(item);
            return return_value_35;
        }
    }
    if (kind == TINYPY_VALUE_CODE) {
        tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "co_argcount", 11U) != 0) {
            tinypy_value_t *return_value_36 = tinypy_integer_from_i64(vm, code->arg_count);
            return return_value_36;
        }
        if (__tinypy_object_name_equal(name, name_size, "co_nlocals", 10U) != 0) {
            tinypy_value_t *return_value_37 = tinypy_integer_from_i64(vm, code->local_count);
            return return_value_37;
        }
        if (__tinypy_object_name_equal(name, name_size, "co_stacksize", 12U) != 0) {
            tinypy_value_t *return_value_38 = tinypy_integer_from_i64(vm, code->stack_size);
            return return_value_38;
        }
        if (__tinypy_object_name_equal(name, name_size, "co_flags", 8U) != 0) {
            tinypy_value_t *return_value_39 = tinypy_integer_from_i64(vm, code->flags);
            return return_value_39;
        }
        if (__tinypy_object_name_equal(name, name_size, "co_code", 7U) != 0) {
            tinypy_value_t *return_value_40 = __tinypy_object_owned(code->bytecode);
            return return_value_40;
        }
        if (__tinypy_object_name_equal(name, name_size, "co_consts", 9U) != 0) {
            tinypy_value_t *return_value_41 = __tinypy_object_owned(code->consts);
            return return_value_41;
        }
        if (__tinypy_object_name_equal(name, name_size, "co_names", 8U) != 0) {
            tinypy_value_t *return_value_42 = __tinypy_object_owned(code->names);
            return return_value_42;
        }
        if (__tinypy_object_name_equal(name, name_size, "co_varnames", 11U) != 0) {
            tinypy_value_t *return_value_43 = __tinypy_object_owned(code->varnames);
            return return_value_43;
        }
        if (__tinypy_object_name_equal(name, name_size, "co_freevars", 11U) != 0) {
            tinypy_value_t *return_value_44 = __tinypy_object_owned(code->freevars);
            return return_value_44;
        }
        if (__tinypy_object_name_equal(name, name_size, "co_cellvars", 11U) != 0) {
            tinypy_value_t *return_value_45 = __tinypy_object_owned(code->cellvars);
            return return_value_45;
        }
        if (__tinypy_object_name_equal(name, name_size, "co_filename", 11U) != 0) {
            tinypy_value_t *return_value_46 = __tinypy_object_owned(code->filename);
            return return_value_46;
        }
        if (__tinypy_object_name_equal(name, name_size, "co_name", 7U) != 0) {
            tinypy_value_t *return_value_47 = __tinypy_object_owned(code->name);
            return return_value_47;
        }
        if (__tinypy_object_name_equal(name, name_size, "co_firstlineno", 14U) != 0) {
            tinypy_value_t *return_value_48 = tinypy_integer_from_i64(vm, code->first_line_number);
            return return_value_48;
        }
        if (__tinypy_object_name_equal(name, name_size, "co_lnotab", 9U) != 0) {
            tinypy_value_t *return_value_49 = __tinypy_object_owned(code->lnotab);
            return return_value_49;
        }
    }
    if (kind == TINYPY_VALUE_FRAME) {
        tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "f_back", 6U) != 0) {
            tinypy_value_t *return_value_50 = __tinypy_object_optional(vm, frame->back);
            return return_value_50;
        }
        if (__tinypy_object_name_equal(name, name_size, "f_code", 6U) != 0) {
            tinypy_value_t *return_value_51 = __tinypy_object_owned(frame->code);
            return return_value_51;
        }
        if (__tinypy_object_name_equal(name, name_size, "f_builtins", 10U) != 0) {
            tinypy_value_t *return_value_52 = __tinypy_object_owned(frame->builtins);
            return return_value_52;
        }
        if (__tinypy_object_name_equal(name, name_size, "f_globals", 9U) != 0) {
            tinypy_value_t *return_value_53 = __tinypy_object_owned(frame->globals);
            return return_value_53;
        }
        if (__tinypy_object_name_equal(name, name_size, "f_locals", 8U) != 0) {
            tinypy_value_t *return_value_54 = __tinypy_object_owned(tinypy_internal_frame_locals(frame));
            return return_value_54;
        }
        if (__tinypy_object_name_equal(name, name_size, "f_lasti", 7U) != 0) {
            tinypy_value_t *return_value_55 = tinypy_integer_from_i64(vm, frame->last_instruction);
            return return_value_55;
        }
        if (__tinypy_object_name_equal(name, name_size, "f_lineno", 8U) != 0) {
            tinypy_value_t *return_value_56 = tinypy_integer_from_i64(vm, tinypy_frame_line_number(value));
            return return_value_56;
        }
    }
    if (kind == TINYPY_VALUE_MODULE && __tinypy_object_name_equal(name, name_size, "__dict__", 8U) != 0) {
        tinypy_value_t *module_dict = tinypy_module_dict(value);
        tinypy_value_t *return_value_57 = __tinypy_object_owned(module_dict);
        return return_value_57;
    }
    if (kind == TINYPY_VALUE_GENERATOR) {
        tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "gi_frame", 8U) != 0) {
            tinypy_value_t *return_value_58 = __tinypy_object_optional(vm, generator->frame);
            return return_value_58;
        }
        if (__tinypy_object_name_equal(name, name_size, "gi_code", 7U) != 0) {
            tinypy_value_t *return_value_59 = generator->frame != NULL ? __tinypy_object_owned(TINYPY_FRAME_OBJECT(generator->frame)->code) : tinypy_none_get(vm);
            return return_value_59;
        }
        if (__tinypy_object_name_equal(name, name_size, "gi_running", 10U) != 0) {
            tinypy_value_t *return_value_60 = tinypy_bool_from_i32(vm, generator->running);
            return return_value_60;
        }
    }
    if (kind == TINYPY_VALUE_TRACEBACK) {
        tinypy_traceback_object_t *traceback = TINYPY_TRACEBACK_OBJECT(value);

        if (__tinypy_object_name_equal(name, name_size, "tb_next", 7U) != 0) {
            tinypy_value_t *return_value_61 = __tinypy_object_optional(vm, traceback->next);
            return return_value_61;
        }
        if (__tinypy_object_name_equal(name, name_size, "tb_frame", 8U) != 0) {
            tinypy_value_t *return_value_62 = __tinypy_object_owned(traceback->frame);
            return return_value_62;
        }
        if (__tinypy_object_name_equal(name, name_size, "tb_lasti", 8U) != 0) {
            tinypy_value_t *return_value_63 = tinypy_integer_from_i64(vm, traceback->last_instruction);
            return return_value_63;
        }
        if (__tinypy_object_name_equal(name, name_size, "tb_lineno", 9U) != 0) {
            tinypy_value_t *return_value_64 = tinypy_integer_from_i64(vm, traceback->line_number);
            return return_value_64;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_call_attribute_hook(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *hook_key, tinypy_value_t *name_key, tinypy_error_t **out_error) {
    tinypy_value_t *attribute = tinypy_internal_type_lookup_key(vm, value->type, hook_key);
    tinypy_value_t *result;

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
tinypy_bool_t tinypy_internal_object_has_special(tinypy_value_t *value, const char *name, size_t name_size) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_bool_t return_value_1 = tinypy_internal_old_instance_has_special(value, name, name_size);
        return return_value_1;
    }
    tinypy_bool_t return_value_2 = tinypy_type_get_attr(value->type, name, name_size) != NULL ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_descriptor_is_data(tinypy_vm_t *vm, tinypy_value_t *attribute) {
    if (attribute->type->descriptor_set != NULL) {
        return TINYPY_TRUE;
    }
    if ((attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = tinypy_internal_type_lookup_key(vm, attribute->type, vm->special_set_key) != NULL || tinypy_internal_type_lookup_key(vm, attribute->type, vm->special_delete_key) != NULL ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_descriptor_get_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    if (attribute->type->descriptor_get != NULL) {
        tinypy_value_t *return_value_1 = attribute->type->descriptor_get(attribute, instance, owner, out_error);
        return return_value_1;
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
tinypy_bool_t tinypy_internal_descriptor_set_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (attribute->type->descriptor_set != NULL) {
        tinypy_bool_t return_value_1 = attribute->type->descriptor_set(attribute, instance, value, out_error);
        return return_value_1;
    }
    if ((attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_type_lookup_key(vm, attribute->type, vm->special_set_key) != NULL) {
        tinypy_value_t *method = tinypy_internal_object_get_attr_key(attribute, vm->special_set_key, out_error);
        tinypy_value_t *items[2] = {instance, value};
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) {
            return TINYPY_FALSE;
        }
        args = tinypy_tuple_from_items(vm, items, 2U);
        result = tinypy_call(method, args, NULL, out_error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not support assignment", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_descriptor_delete_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_error_t **out_error) {
    if (attribute->type->descriptor_set != NULL) {
        tinypy_bool_t return_value_1 = attribute->type->descriptor_set(attribute, instance, NULL, out_error);
        return return_value_1;
    }
    if ((attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_type_lookup_key(vm, attribute->type, vm->special_delete_key) != NULL) {
        tinypy_value_t *method = tinypy_internal_object_get_attr_key(attribute, vm->special_delete_key, out_error);
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) {
            return TINYPY_FALSE;
        }
        args = tinypy_tuple_from_items(vm, &instance, 1U);
        result = tinypy_call(method, args, NULL, out_error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not support deletion", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_instance_attribute(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);
    tinypy_type_t *type = value->type;
    tinypy_value_t *attribute = tinypy_internal_type_lookup_key(vm, type, key);

    if (attribute != NULL && tinypy_internal_descriptor_is_data(vm, attribute) != 0) {
        tinypy_value_t *return_value_1 = tinypy_internal_descriptor_get_value(vm, attribute, value, type, out_error);
        return return_value_1;
    }
    if (dict_slot != NULL && *dict_slot != NULL) {
        tinypy_value_t *stored = tinypy_internal_dict_get_optional(vm, *dict_slot, key);

        if (stored != NULL) {
            TINYPY_INCREF(stored);
            return stored;
        }
    }
    if (attribute != NULL) {
        tinypy_value_t *return_value_2 = tinypy_internal_descriptor_get_value(vm, attribute, value, type, out_error);
        return return_value_2;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_type_attribute(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_type_t *type = (tinypy_type_t *)value;
    tinypy_type_t *metaclass = value->type;
    tinypy_value_t *metaclass_attribute = tinypy_internal_type_lookup_key(vm, metaclass, key);

    if (metaclass_attribute != NULL && tinypy_internal_descriptor_is_data(vm, metaclass_attribute) != 0) {
        tinypy_value_t *return_value_1 = tinypy_internal_descriptor_get_value(vm, metaclass_attribute, value, metaclass, out_error);
        return return_value_1;
    }
    tinypy_value_t *attribute = tinypy_internal_type_lookup_key(vm, type, key);
    if (attribute != NULL) {
        tinypy_value_t *return_value_2 = tinypy_internal_descriptor_get_value(vm, attribute, NULL, type, out_error);
        return return_value_2;
    }
    if (metaclass_attribute != NULL) {
        tinypy_value_t *return_value_3 = tinypy_internal_descriptor_get_value(vm, metaclass_attribute, value, metaclass, out_error);
        return return_value_3;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_object_attribute_error(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    if (vm->raised_type != NULL) {
        tinypy_bool_t return_value_1 = tinypy_type_is_subtype((tinypy_type_t *)vm->raised_type, vm->exception_types[TINYPY_EXCEPTION_ATTRIBUTE_ERROR]);
        return return_value_1;
    }
    tinypy_bool_t return_value_2 = out_error != NULL && *out_error != NULL && tinypy_error_kind(*out_error) == TINYPY_ERROR_ATTRIBUTE ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_object_error_present(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    return vm->raised_type != NULL || (out_error != NULL && *out_error != NULL) ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_object_clear_attribute_error(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
    }
    tinypy_internal_exception_clear_raised(vm);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_getattr_fallback(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *result, tinypy_bool_t suppress_missing, tinypy_bool_t *out_missing, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    const char *name = NULL;
    size_t name_size = 0U;
    tinypy_bool_t text_key = __tinypy_object_key_text(key, &name, &name_size);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_bool_t has_getattr;

    if (result != NULL) {
        return result;
    }
    has_getattr = (value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && kind != TINYPY_VALUE_TYPE && (text_key == 0 || __tinypy_object_name_equal(name, name_size, "__getattr__", 11U) == 0) && tinypy_internal_type_lookup_key(vm, value->type, vm->special_getattr_key) != NULL ? INT32_C(1) : INT32_C(0);
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
        __tinypy_object_make_attribute_error_key(value, key, out_error);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_bool_t suppress_missing, tinypy_bool_t *out_missing, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    const char *name = NULL;
    size_t name_size = 0U;
    tinypy_bool_t text_key = __tinypy_object_key_text(key, &name, &name_size);
    tinypy_value_t *result = NULL;
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    TINYPY_CLEAR_ERROR(out_error);
    *out_missing = INT32_C(0);
    if (text_key != 0) {
        result = __tinypy_object_builtin_attribute(value, name, name_size);
        if (result != NULL) {
            return result;
        }
    }
    if ((value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && kind != TINYPY_VALUE_TYPE && (text_key == 0 || __tinypy_object_name_equal(name, name_size, "__getattribute__", 16U) == 0) && tinypy_internal_type_lookup_key(vm, value->type, vm->special_getattribute_key) != NULL) {
        result = __tinypy_object_call_attribute_hook(vm, value, vm->special_getattribute_key, key, out_error);
        tinypy_value_t *return_value_1 = __tinypy_object_getattr_fallback(value, key, result, suppress_missing, out_missing, out_error);
        return return_value_1;
    }
    if (value->type->get_attribute != NULL) {
        result = value->type->get_attribute(value, key, out_error);
        tinypy_value_t *return_value_2 = __tinypy_object_getattr_fallback(value, key, result, suppress_missing, out_missing, out_error);
        return return_value_2;
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

        tinypy_value_t *return_value_3 = __tinypy_object_get_attr_key(method->function, key, suppress_missing, out_missing, out_error);
        return return_value_3;
    }
    tinypy_value_t *return_value_4 = __tinypy_object_getattr_fallback(value, key, result, suppress_missing, out_missing, out_error);
    return return_value_4;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_get_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_bool_t missing;

    tinypy_value_t *return_value_1 = __tinypy_object_get_attr_key(value, key, INT32_C(0), &missing, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_object_get_optional_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    tinypy_bool_t missing;

    *out_value = __tinypy_object_get_attr_key(value, key, INT32_C(1), &missing, out_error);
    if (*out_value != NULL) {
        return INT32_C(1);
    }
    return missing != 0 ? INT32_C(0) : -INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_object_get_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *result = tinypy_object_get_attr_value(value, key, out_error);
    TINYPY_DECREF(key);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_object_get_attr_value(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = tinypy_internal_object_get_attr_key(value, name, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_object_has_attr(tinypy_value_t *value, const char *name, size_t name_size) {
    tinypy_bool_t result;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    result = tinypy_object_has_attr_value(value, key);
    TINYPY_DECREF(key);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_object_has_attr_value(tinypy_value_t *value, tinypy_value_t *name) {
    tinypy_value_t *result;
    int32_t status;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    status = tinypy_internal_object_get_optional_attr_key(value, name, &result, NULL);
    if (result != NULL) {
        TINYPY_DECREF(result);
    }
    if (status < 0) {
        tinypy_internal_exception_clear_raised(vm);
        return TINYPY_FALSE;
    }
    return status != 0 ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_object_set_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *descriptor = tinypy_internal_type_lookup_key(vm, value->type, key);
    if (descriptor != NULL && tinypy_internal_descriptor_is_data(vm, descriptor) != 0) {
        tinypy_bool_t return_value_1 = tinypy_internal_descriptor_set_value(vm, descriptor, value, attribute_value, out_error);
        return return_value_1;
    }
    if (value->type->set_attribute != NULL) {
        tinypy_bool_t return_value_2 = value->type->set_attribute(value, key, attribute_value, out_error);
        return return_value_2;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_INSTANCE || value->type->dict_offset != 0U) {
        tinypy_value_t **dict_slot;

        if (value->type->has_instance_dict == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "instance has no dictionary for this attribute", out_error);
            return TINYPY_FALSE;
        }
        dict_slot = tinypy_internal_object_dict_slot(value);
        if (*dict_slot == NULL) {
            *dict_slot = tinypy_dict_new(vm);
        }
        tinypy_dict_set(*dict_slot, key, attribute_value);
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE) {
        tinypy_internal_type_set_attr_key((tinypy_type_t *)value, key, attribute_value);
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FUNCTION) {
        if (TINYPY_FUNCTION_OBJECT(value)->dict == NULL) {
            TINYPY_FUNCTION_OBJECT(value)->dict = tinypy_dict_new(vm);
        }
        tinypy_dict_set(TINYPY_FUNCTION_OBJECT(value)->dict, key, attribute_value);
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_MODULE) {
        tinypy_dict_set(TINYPY_MODULE_OBJECT(value)->dict, key, attribute_value);
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object attributes are read-only", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_object_set_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    tinypy_bool_t result;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    result = tinypy_object_set_attr_value(value, key, attribute_value, out_error);
    TINYPY_DECREF(key);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_object_set_attr_value(tinypy_value_t *value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    tinypy_bool_t return_value_1 = tinypy_internal_object_set_attr_key(value, name, attribute_value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_object_delete_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *dict = NULL;

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *descriptor = tinypy_internal_type_lookup_key(vm, value->type, key);
    if (descriptor != NULL && tinypy_internal_descriptor_is_data(vm, descriptor) != 0) {
        tinypy_bool_t return_value_1 = tinypy_internal_descriptor_delete_value(vm, descriptor, value, out_error);
        return return_value_1;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_CLASS) {
        tinypy_bool_t return_value_2 = tinypy_internal_class_delete_attribute(value, key, out_error);
        return return_value_2;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_bool_t return_value_3 = tinypy_internal_old_instance_delete_attribute(value, key, out_error);
        return return_value_3;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_INSTANCE || value->type->dict_offset != 0U) {
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);

        dict = dict_slot != NULL ? *dict_slot : NULL;
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE) {
        if ((((tinypy_type_t *)value)->flags & TINYPY_TYPE_FLAG_IMMUTABLE) != 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type attributes are read-only", out_error);
            return TINYPY_FALSE;
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
        return TINYPY_FALSE;
    }
    if (tinypy_internal_dict_delete_optional(vm, dict, key) == 0) {
        __tinypy_object_make_attribute_error_key(value, key, out_error);
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE) {
        ((tinypy_type_t *)value)->version_tag += UINT64_C(1);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_object_delete_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_bool_t result;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    result = tinypy_internal_object_delete_attr_key(value, key, out_error);
    TINYPY_DECREF(key);
    return result;
}
