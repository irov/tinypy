#include "tinypy/object.h"
#include "tinypy/representation.h"

#include "internal.h"

#include <string.h>

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
tinypy_value_t *tinypy_internal_object_encode_attribute_name(tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(key);

    if (TINYPY_TEXT_BYTE_SIZE(key) == TINYPY_SIZED_SIZE(key)) {
        tinypy_value_t *result = tinypy_internal_name_from_bytes(vm, (const char *)TINYPY_TEXT_BYTES(key), TINYPY_TEXT_BYTE_SIZE(key));
        return result;
    }
    tinypy_value_t *encode = tinypy_type_get_attr_key(&vm->types[TINYPY_VALUE_UNICODE], vm->internal_encode_key);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &key, 1U);
    tinypy_value_t *result = tinypy_call(encode, args, NULL, out_error);

    TINYPY_DECREF(args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_object_type_metadata_read_only(tinypy_value_t *key) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(key);
    tinypy_value_t *const metadata[] = {vm->internal_special_bases_key, vm->internal_special_mro_key, vm->internal_special_base_key, vm->internal_special_flags_key, vm->internal_special_basicsize_key, vm->internal_special_itemsize_key, vm->internal_special_dictoffset_key, vm->internal_special_weakrefoffset_key, vm->internal_special_dict_key, vm->internal_special_doc_key, vm->internal_special_class_key};

    for (size_t index = 0U; index < sizeof(metadata) / sizeof(metadata[0]); ++index) {
        if (TINYPY_NAME_EQ(key, metadata[index]) != 0) {
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* Classic classes and their instances name the class itself, the way Python
   2.7 reports a missing attribute on them. */
static void __tinypy_object_make_attribute_error(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    static const char object_prefix[] = "'";
    static const char type_prefix[] = "type object '";
    static const char type_separator[] = "' has no attribute '";
    static const char object_separator[] = "' object has no attribute '";
    static const char class_prefix[] = "class ";
    static const char class_separator[] = " has no attribute '";
    static const char instance_prefix[] = "";
    static const char instance_separator[] = " instance has no attribute '";
    static const char suffix[] = "'";
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    const char *prefix = object_prefix;
    size_t prefix_size = sizeof(object_prefix) - 1U;
    const char *separator = object_separator;
    size_t separator_size = sizeof(object_separator) - 1U;
    const char *owner_name = value->type->name;
    size_t owner_name_size = value->type->name_size;
    size_t message_size;
    size_t offset = 0U;
    char *message;

    const char *name_nul = (const char *)memchr(name, 0, name_size);
    if (name_nul != NULL) {
        name_size = (size_t)(name_nul - name);
    }
    if (kind == TINYPY_VALUE_PARTIAL && value->type == &vm->types[TINYPY_VALUE_PARTIAL]) {
        owner_name = "functools.partial";
        owner_name_size = sizeof("functools.partial") - 1U;
    }
    if (kind == TINYPY_VALUE_TYPE) {
        prefix = type_prefix;
        prefix_size = sizeof(type_prefix) - 1U;
        separator = type_separator;
        separator_size = sizeof(type_separator) - 1U;
        owner_name = ((tinypy_type_t *)value)->name;
        owner_name_size = ((tinypy_type_t *)value)->name_size;
    }
    if (kind == TINYPY_VALUE_CLASS || kind == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *class_value = kind == TINYPY_VALUE_CLASS ? value : tinypy_old_instance_class(value);
        tinypy_value_t *class_name = tinypy_class_name(class_value);

        owner_name = (const char *)TINYPY_TEXT_BYTES(class_name);
        owner_name_size = TINYPY_TEXT_BYTE_SIZE(class_name);
        prefix = kind == TINYPY_VALUE_CLASS ? class_prefix : instance_prefix;
        prefix_size = kind == TINYPY_VALUE_CLASS ? sizeof(class_prefix) - 1U : sizeof(instance_prefix) - 1U;
        separator = kind == TINYPY_VALUE_CLASS ? class_separator : instance_separator;
        separator_size = kind == TINYPY_VALUE_CLASS ? sizeof(class_separator) - 1U : sizeof(instance_separator) - 1U;
    }
    message_size = prefix_size + owner_name_size + separator_size + name_size + (sizeof(suffix) - 1U);
    message = (char *)tinypy_internal_vm_allocate(vm, message_size + 1U);
    if (prefix_size != 0U) {
        (void)memcpy(message + offset, prefix, prefix_size);
        offset += prefix_size;
    }
    if (owner_name_size != 0U) {
        (void)memcpy(message + offset, owner_name, owner_name_size);
        offset += owner_name_size;
    }
    (void)memcpy(message + offset, separator, separator_size);
    offset += separator_size;
    if (name_size != 0U) {
        (void)memcpy(message + offset, name, name_size);
        offset += name_size;
    }
    (void)memcpy(message + offset, suffix, sizeof(suffix));
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, message, out_error);
    tinypy_internal_vm_deallocate(vm, message, message_size + 1U);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_object_make_attribute_error_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
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
static tinypy_value_t *__tinypy_object_optional(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_value_t *return_value_1 = value != NULL ? TINYPY_RET(value) : TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static uint64_t __tinypy_object_type_flags(tinypy_vm_t *vm, const tinypy_type_t *type) {
    uint64_t flags = UINT64_C(0x1eb) | (UINT64_C(1) << 12U) | (UINT64_C(1) << 17U) | (UINT64_C(1) << 18U) | (UINT64_C(1) << 19U);

    if ((type->flags & TINYPY_TYPE_FLAG_NEEDS_ATTRIBUTE_READY) != 0U) {
        flags &= ~(UINT64_C(1) << 12U);
    }
    if ((type->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) != 0U) {
        flags |= UINT64_C(1) << 9U;
    }
    if ((type->flags & TINYPY_TYPE_FLAG_BASE_TYPE) != 0U) {
        flags |= UINT64_C(1) << 10U;
    }
    if ((type->flags & TINYPY_TYPE_FLAG_ABSTRACT) != 0U) {
        flags |= UINT64_C(1) << 20U;
    }
    if (type->release_references != NULL) {
        flags |= UINT64_C(1) << 14U;
    }
    if (type->weakref_offset != 0U) {
        flags |= UINT64_C(1) << 6U;
    }
    if (type->iter != NULL || type->next != NULL) {
        flags |= UINT64_C(1) << 7U;
    }
    if (type->number_slots != NULL || type->layout_kind == TINYPY_VALUE_BOOL || type->layout_kind == TINYPY_VALUE_INTEGER || type->layout_kind == TINYPY_VALUE_LONG || type->layout_kind == TINYPY_VALUE_FLOAT || type->layout_kind == TINYPY_VALUE_COMPLEX || tinypy_type_is_subtype(type, &vm->types[TINYPY_VALUE_STRING]) != 0 || tinypy_type_is_subtype(type, &vm->types[TINYPY_VALUE_UNICODE]) != 0 || tinypy_type_is_subtype(type, &vm->types[TINYPY_VALUE_SET]) != 0 || tinypy_type_is_subtype(type, &vm->types[TINYPY_VALUE_FROZENSET]) != 0) {
        flags |= UINT64_C(1) << 4U;
    }
    if (type->layout_kind == TINYPY_VALUE_STRING || type->layout_kind == TINYPY_VALUE_BYTEARRAY || type->layout_kind == TINYPY_VALUE_BUFFER || type == vm->memoryview_type) {
        flags |= UINT64_C(1) << 21U;
    }
    if (tinypy_type_is_subtype(type, &vm->types[TINYPY_VALUE_INTEGER]) != 0) {
        flags |= UINT64_C(1) << 23U;
    }
    if (tinypy_type_is_subtype(type, &vm->types[TINYPY_VALUE_LONG]) != 0) {
        flags |= UINT64_C(1) << 24U;
    }
    if (tinypy_type_is_subtype(type, &vm->types[TINYPY_VALUE_LIST]) != 0) {
        flags |= UINT64_C(1) << 25U;
    }
    if (tinypy_type_is_subtype(type, &vm->types[TINYPY_VALUE_TUPLE]) != 0) {
        flags |= UINT64_C(1) << 26U;
    }
    if (tinypy_type_is_subtype(type, &vm->types[TINYPY_VALUE_STRING]) != 0) {
        flags |= UINT64_C(1) << 27U;
    }
    if (tinypy_type_is_subtype(type, &vm->types[TINYPY_VALUE_UNICODE]) != 0) {
        flags |= UINT64_C(1) << 28U;
    }
    if (tinypy_type_is_subtype(type, &vm->types[TINYPY_VALUE_DICT]) != 0) {
        flags |= UINT64_C(1) << 29U;
    }
    if (vm->exception_types[TINYPY_EXCEPTION_BASE] != NULL && tinypy_type_is_subtype(type, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0) {
        flags |= UINT64_C(1) << 30U;
    }
    if (tinypy_type_is_subtype(type, &vm->types[TINYPY_VALUE_TYPE]) != 0) {
        flags |= UINT64_C(1) << 31U;
    }
    return flags;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_type_tuple(tinypy_vm_t *vm, tinypy_type_t *type, int32_t mro) {
    size_t size = mro != 0 ? tinypy_type_mro_size(type) : tinypy_type_bases_size(type);
    size_t index;

    if (size == 0U) {
        tinypy_value_t *return_value_1 = TINYPY_RET_EMPTY_TUPLE(vm);
        return return_value_1;
    }
    tinypy_value_t **items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, size * sizeof(*items));
    for (index = 0U; index < size; ++index) {
        items[index] = mro != 0 ? tinypy_internal_type_mro_value_at(type, index) : tinypy_internal_type_base_value_at(type, index);
    }
    tinypy_value_t *result = mro != 0 ? tinypy_internal_tuple_from_borrowed_items(vm, items, size) : tinypy_tuple_from_items(vm, items, size);
    tinypy_internal_vm_deallocate(vm, items, size * sizeof(*items));
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Only VM preset names participate; keys borrow the registry's lifetime. */
#define TINYPY_BUILTIN_ATTRIBUTE_LIST(X) \
    X(internal_special_dict_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_DICT) \
    X(internal_special_name_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_NAME) \
    X(internal_special_bases_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_BASES) \
    X(internal_special_mro_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_MRO) \
    X(internal_special_base_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_BASE) \
    X(internal_special_flags_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_FLAGS) \
    X(internal_special_basicsize_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_BASICSIZE) \
    X(internal_special_itemsize_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_ITEMSIZE) \
    X(internal_special_dictoffset_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_DICTOFFSET) \
    X(internal_special_weakrefoffset_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_WEAKREFOFFSET) \
    X(internal_special_module_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_MODULE) \
    X(internal_special_doc_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_DOC) \
    X(internal_func_globals_key, TINYPY_BUILTIN_ATTRIBUTE_FUNC_GLOBALS) \
    X(internal_special_globals_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_GLOBALS) \
    X(internal_func_defaults_key, TINYPY_BUILTIN_ATTRIBUTE_FUNC_DEFAULTS) \
    X(internal_special_defaults_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_DEFAULTS) \
    X(internal_func_closure_key, TINYPY_BUILTIN_ATTRIBUTE_FUNC_CLOSURE) \
    X(internal_special_closure_key, TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_CLOSURE) \
    X(internal_func_name_key, TINYPY_BUILTIN_ATTRIBUTE_FUNC_NAME) \
    X(internal_func_doc_key, TINYPY_BUILTIN_ATTRIBUTE_FUNC_DOC) \
    X(internal_func_dict_key, TINYPY_BUILTIN_ATTRIBUTE_FUNC_DICT) \
    X(internal_im_func_key, TINYPY_BUILTIN_ATTRIBUTE_IM_FUNC) \
    X(internal_im_self_key, TINYPY_BUILTIN_ATTRIBUTE_IM_SELF) \
    X(internal_im_class_key, TINYPY_BUILTIN_ATTRIBUTE_IM_CLASS) \
    X(internal_func_key, TINYPY_BUILTIN_ATTRIBUTE_FUNC) \
    X(internal_args_key, TINYPY_BUILTIN_ATTRIBUTE_ARGS) \
    X(internal_keywords_key, TINYPY_BUILTIN_ATTRIBUTE_KEYWORDS) \
    X(internal_pattern_key, TINYPY_BUILTIN_ATTRIBUTE_PATTERN) \
    X(internal_flags_key, TINYPY_BUILTIN_ATTRIBUTE_FLAGS) \
    X(internal_groups_key, TINYPY_BUILTIN_ATTRIBUTE_GROUPS) \
    X(internal_groupindex_key, TINYPY_BUILTIN_ATTRIBUTE_GROUPINDEX) \
    X(internal_re_key, TINYPY_BUILTIN_ATTRIBUTE_RE) \
    X(internal_string_key, TINYPY_BUILTIN_ATTRIBUTE_STRING) \
    X(internal_pos_key, TINYPY_BUILTIN_ATTRIBUTE_POS) \
    X(internal_endpos_key, TINYPY_BUILTIN_ATTRIBUTE_ENDPOS) \
    X(internal_lastindex_key, TINYPY_BUILTIN_ATTRIBUTE_LASTINDEX) \
    X(internal_regs_key, TINYPY_BUILTIN_ATTRIBUTE_REGS) \
    X(internal_lastgroup_key, TINYPY_BUILTIN_ATTRIBUTE_LASTGROUP) \
    X(internal_co_argcount_key, TINYPY_BUILTIN_ATTRIBUTE_CO_ARGCOUNT) \
    X(internal_co_nlocals_key, TINYPY_BUILTIN_ATTRIBUTE_CO_NLOCALS) \
    X(internal_co_stacksize_key, TINYPY_BUILTIN_ATTRIBUTE_CO_STACKSIZE) \
    X(internal_co_flags_key, TINYPY_BUILTIN_ATTRIBUTE_CO_FLAGS) \
    X(internal_co_code_key, TINYPY_BUILTIN_ATTRIBUTE_CO_CODE) \
    X(internal_co_consts_key, TINYPY_BUILTIN_ATTRIBUTE_CO_CONSTS) \
    X(internal_co_names_key, TINYPY_BUILTIN_ATTRIBUTE_CO_NAMES) \
    X(internal_co_varnames_key, TINYPY_BUILTIN_ATTRIBUTE_CO_VARNAMES) \
    X(internal_co_freevars_key, TINYPY_BUILTIN_ATTRIBUTE_CO_FREEVARS) \
    X(internal_co_cellvars_key, TINYPY_BUILTIN_ATTRIBUTE_CO_CELLVARS) \
    X(internal_co_filename_key, TINYPY_BUILTIN_ATTRIBUTE_CO_FILENAME) \
    X(internal_co_name_key, TINYPY_BUILTIN_ATTRIBUTE_CO_NAME) \
    X(internal_co_firstlineno_key, TINYPY_BUILTIN_ATTRIBUTE_CO_FIRSTLINENO) \
    X(internal_co_lnotab_key, TINYPY_BUILTIN_ATTRIBUTE_CO_LNOTAB) \
    X(internal_f_back_key, TINYPY_BUILTIN_ATTRIBUTE_F_BACK) \
    X(internal_f_code_key, TINYPY_BUILTIN_ATTRIBUTE_F_CODE) \
    X(internal_f_builtins_key, TINYPY_BUILTIN_ATTRIBUTE_F_BUILTINS) \
    X(internal_f_globals_key, TINYPY_BUILTIN_ATTRIBUTE_F_GLOBALS) \
    X(internal_f_locals_key, TINYPY_BUILTIN_ATTRIBUTE_F_LOCALS) \
    X(internal_f_lasti_key, TINYPY_BUILTIN_ATTRIBUTE_F_LASTI) \
    X(internal_f_lineno_key, TINYPY_BUILTIN_ATTRIBUTE_F_LINENO) \
    X(internal_gi_frame_key, TINYPY_BUILTIN_ATTRIBUTE_GI_FRAME) \
    X(internal_gi_code_key, TINYPY_BUILTIN_ATTRIBUTE_GI_CODE) \
    X(internal_gi_running_key, TINYPY_BUILTIN_ATTRIBUTE_GI_RUNNING) \
    X(internal_tb_next_key, TINYPY_BUILTIN_ATTRIBUTE_TB_NEXT) \
    X(internal_tb_frame_key, TINYPY_BUILTIN_ATTRIBUTE_TB_FRAME) \
    X(internal_tb_lasti_key, TINYPY_BUILTIN_ATTRIBUTE_TB_LASTI) \
    X(internal_tb_lineno_key, TINYPY_BUILTIN_ATTRIBUTE_TB_LINENO)
#define TINYPY_BUILTIN_ATTRIBUTE_ENUM(field, attribute) attribute,
typedef enum tinypy_builtin_attribute_e {
    TINYPY_BUILTIN_ATTRIBUTE_NONE = 0,
    TINYPY_BUILTIN_ATTRIBUTE_LIST(TINYPY_BUILTIN_ATTRIBUTE_ENUM)
    TINYPY_BUILTIN_ATTRIBUTE_COUNT
} tinypy_builtin_attribute_e;
#undef TINYPY_BUILTIN_ATTRIBUTE_ENUM

typedef struct tinypy_builtin_attribute_spec_t {
    size_t name_offset;
    uint8_t attribute;
} tinypy_builtin_attribute_spec_t;
#define TINYPY_BUILTIN_ATTRIBUTE_SPEC(field, attribute) {offsetof(tinypy_vm_t, field), attribute},
static const tinypy_builtin_attribute_spec_t __tinypy_builtin_attribute_specs[] = {
    TINYPY_BUILTIN_ATTRIBUTE_LIST(TINYPY_BUILTIN_ATTRIBUTE_SPEC)
};
#undef TINYPY_BUILTIN_ATTRIBUTE_SPEC
static const tinypy_internal_string_metadata_t __tinypy_builtin_attribute_metadata[TINYPY_BUILTIN_ATTRIBUTE_COUNT] = {
#define TINYPY_BUILTIN_ATTRIBUTE_METADATA(field, attribute) [attribute] = {attribute},
    TINYPY_BUILTIN_ATTRIBUTE_LIST(TINYPY_BUILTIN_ATTRIBUTE_METADATA)
#undef TINYPY_BUILTIN_ATTRIBUTE_METADATA
};
#undef TINYPY_BUILTIN_ATTRIBUTE_LIST
typedef char tinypy_builtin_attribute_id_must_fit_metadata_t[
    TINYPY_BUILTIN_ATTRIBUTE_COUNT <= UINT8_MAX ? 1 : -1];

static tinypy_value_t *__tinypy_object_cached_special_key(tinypy_vm_t *vm, const char *name, size_t name_size);
//////////////////////////////////////////////////////////////////////////
static tinypy_builtin_attribute_e __tinypy_object_builtin_attribute_id(tinypy_vm_t *vm, tinypy_value_t *key) {
    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_STRING) {
        const tinypy_internal_string_metadata_t *metadata = TINYPY_STRING_OBJECT(key)->internal_metadata;
        if (metadata != NULL) {
            return (tinypy_builtin_attribute_e)metadata->builtin_attribute_id;
        }
    }
    /* Raw compiler/marshal strings, immutable subtype copies and Unicode keys
       borrow their preset from the shared name cache without changing the key,
       allocating or invoking subtype __hash__/__eq__ callbacks. */
    tinypy_value_t *preset = __tinypy_object_cached_special_key(vm, (const char *)TINYPY_TEXT_BYTES(key), TINYPY_TEXT_BYTE_SIZE(key));
    if (preset == NULL) {
        return TINYPY_BUILTIN_ATTRIBUTE_NONE;
    }
    const tinypy_internal_string_metadata_t *metadata = TINYPY_STRING_OBJECT(preset)->internal_metadata;
    return (tinypy_builtin_attribute_e)metadata->builtin_attribute_id;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_type_special_name(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = (tinypy_type_t *)value;
    if ((type->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) != 0U) {
        return TINYPY_RET(type->name_object);
    }
    size_t offset = 0U;
    for (size_t index = 0U; index < type->name_size; ++index) {
        if (type->name[index] == '.') {
            offset = index + 1U;
        }
    }
    tinypy_value_t *result = tinypy_string_from_bytes(vm, type->name + offset, type->name_size - offset);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_type_special_dict(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = (tinypy_type_t *)value;
    if ((type->flags & TINYPY_TYPE_FLAG_NEEDS_ATTRIBUTE_READY) != 0U) {
        return TINYPY_RET_NONE(vm);
    }
    tinypy_value_t *result = tinypy_internal_dictproxy_new(vm, type->dict);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_object_immutable_type_error(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *name = __tinypy_object_get_type_special_name(value);
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("can't set attributes of built-in/extension type '"),
        TINYPY_MESSAGE_PART_TEXT(name), TINYPY_MESSAGE_PART_LITERAL("'")
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
    TINYPY_DECREF(name);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_type_special_bases(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = (tinypy_type_t *)value;
    if (type->bases == NULL) {
        type->bases = __tinypy_object_type_tuple(vm, type, INT32_C(0));
    }
    tinypy_value_t *result = TINYPY_RET(type->bases);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_type_special_mro(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = (tinypy_type_t *)value;
    if ((type->flags & TINYPY_TYPE_FLAG_NEEDS_ATTRIBUTE_READY) != 0U) {
        return TINYPY_RET_NONE(vm);
    }
    if (type->mro == NULL && (type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U) {
        tinypy_value_t *result = TINYPY_RET_NONE(vm);

        return result;
    }
    /* The internal tuple borrows self; expose an owning copy. */
    tinypy_value_t *mro = tinypy_internal_type_mro_tuple(type);
    tinypy_value_t *const *mro_items = tinypy_internal_tuple_items(mro);
    tinypy_value_t *result = tinypy_tuple_from_items(vm, mro_items, TINYPY_TUPLE_SIZE(mro));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_type_special_base(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = (tinypy_type_t *)value;
    if ((type->flags & TINYPY_TYPE_FLAG_NEEDS_ATTRIBUTE_READY) != 0U) {
        return TINYPY_RET_NONE(vm);
    }
    tinypy_value_t *result = type->base_type != NULL ? TINYPY_RET(&type->base_type->base.base) : TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_type_special_flags(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = (tinypy_type_t *)value;
    uint64_t flags = __tinypy_object_type_flags(vm, type);
    tinypy_value_t *result = tinypy_long_from_i64(vm, (int64_t)flags);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_type_special_basicsize(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = (tinypy_type_t *)value;
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)type->basic_size);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_type_special_itemsize(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = (tinypy_type_t *)value;
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)type->item_size);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_type_special_dictoffset(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = (tinypy_type_t *)value;
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)type->dict_offset);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_type_special_weakrefoffset(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = (tinypy_type_t *)value;
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)type->weakref_offset);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_type_special_module(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = (tinypy_type_t *)value;
    if ((type->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) != 0U) {
        tinypy_value_t *module = tinypy_internal_dict_get_optional_suppressed(vm, type->dict, vm->internal_special_module_key);
        if (module == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "__module__", NULL);
            return NULL;
        }
        return TINYPY_RET(module);
    }
    for (size_t index = type->name_size; index != 0U; --index) {
        if (type->name[index - 1U] == '.') {
            tinypy_value_t *result = tinypy_string_from_bytes(vm, type->name, index - 1U);
            return result;
        }
    }
    /* Builtin exception types keep a short C name and a declared module.
       Instance descriptors such as function.__module__ are not type metadata. */
    tinypy_value_t *module = tinypy_internal_dict_get_optional_suppressed(vm, type->dict, vm->internal_special_module_key);
    if (module != NULL && TINYPY_VALUE_KIND(module) == TINYPY_VALUE_STRING) {
        return TINYPY_RET(module);
    }
    return TINYPY_RET(vm->internal_builtin_module_name);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_type_special_doc(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = (tinypy_type_t *)value;
    tinypy_value_t *doc = tinypy_internal_dict_get_optional(vm, type->dict, vm->internal_special_doc_key);

    tinypy_value_t *result = __tinypy_object_optional(vm, doc);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_function_func_globals(tinypy_value_t *value) {
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(function->globals);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_function_func_defaults(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(value);
    tinypy_value_t *result = __tinypy_object_optional(vm, function->defaults);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_function_func_closure(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(value);
    tinypy_value_t *result = __tinypy_object_optional(vm, function->closure);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_function_func_name(tinypy_value_t *value) {
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(function->name);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_function_func_doc(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(value);
    tinypy_value_t *result = __tinypy_object_optional(vm, function->doc);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_function_special_module(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(value);
    tinypy_value_t *result = __tinypy_object_optional(vm, function->module);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_function_func_dict(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(value);
    if (function->dict == NULL) {
        function->dict = tinypy_dict_new(vm);
    }
    tinypy_value_t *result = TINYPY_RET(function->dict);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_method_im_func(tinypy_value_t *value) {
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(method->function);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_method_im_self(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(value);
    tinypy_value_t *result = __tinypy_object_optional(vm, method->self);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_method_im_class(tinypy_value_t *value) {
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(method->owner);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_partial_func(tinypy_value_t *value) {
    tinypy_partial_object_t *partial = TINYPY_PARTIAL_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(partial->callable);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_partial_args(tinypy_value_t *value) {
    tinypy_partial_object_t *partial = TINYPY_PARTIAL_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(partial->args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_partial_keywords(tinypy_value_t *value) {
    tinypy_partial_object_t *partial = TINYPY_PARTIAL_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(partial->keywords);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_sre_pattern_pattern(tinypy_value_t *value) {
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(pattern->pattern);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_sre_pattern_flags(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, pattern->flags);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_sre_pattern_groups(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)pattern->groups);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_sre_pattern_groupindex(tinypy_value_t *value) {
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(pattern->groupindex);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_sre_match_re(tinypy_value_t *value) {
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(match->pattern);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_sre_match_string(tinypy_value_t *value) {
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(match->string);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_sre_match_pos(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)match->pos);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_sre_match_endpos(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)match->endpos);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_sre_match_lastindex(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(value);
    tinypy_value_t *result = match->lastindex >= 0 ? tinypy_integer_from_i64(vm, (int64_t)match->lastindex) : TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_sre_match_regs(tinypy_value_t *value) {

    tinypy_value_t *result = tinypy_internal_sre_match_regs(value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_sre_match_lastgroup(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_sre_match_object_t *match = TINYPY_SRE_MATCH_OBJECT(value);
    tinypy_sre_pattern_object_t *pattern = TINYPY_SRE_PATTERN_OBJECT(match->pattern);
    if (match->lastindex < 0 || (size_t)match->lastindex >= TINYPY_LIST_SIZE(pattern->indexgroup)) {
        tinypy_value_t *result = TINYPY_RET_NONE(vm);
        return result;
    }
    tinypy_value_t *item = TINYPY_LIST_GET(pattern->indexgroup, (size_t)match->lastindex);
    tinypy_value_t *result = TINYPY_RET(item);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_argcount(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, code->arg_count);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_nlocals(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, code->local_count);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_stacksize(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, code->stack_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_flags(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, code->flags);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_code(tinypy_value_t *value) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(code->bytecode);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_consts(tinypy_value_t *value) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(code->consts);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_names(tinypy_value_t *value) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(code->names);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_varnames(tinypy_value_t *value) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(code->varnames);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_freevars(tinypy_value_t *value) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(code->freevars);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_cellvars(tinypy_value_t *value) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(code->cellvars);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_filename(tinypy_value_t *value) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(code->filename);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_name(tinypy_value_t *value) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(code->name);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_firstlineno(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, code->first_line_number);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_code_co_lnotab(tinypy_value_t *value) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(code->lnotab);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_frame_f_back(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(value);
    tinypy_value_t *result = __tinypy_object_optional(vm, frame->back);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_frame_f_code(tinypy_value_t *value) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(frame->code);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_frame_f_builtins(tinypy_value_t *value) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(frame->builtins);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_frame_f_globals(tinypy_value_t *value) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(frame->globals);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_frame_f_locals(tinypy_value_t *value) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(tinypy_internal_frame_locals(frame));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_frame_f_lasti(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, frame->last_instruction);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_frame_f_lineno(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, tinypy_frame_line_number(value));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_generator_gi_frame(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(value);
    tinypy_value_t *result = __tinypy_object_optional(vm, generator->frame);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_generator_gi_code(tinypy_value_t *value) {
    tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(generator->code);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_generator_gi_running(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, generator->running != 0 ? INT64_C(1) : INT64_C(0));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_generator_special_name(tinypy_value_t *value) {
    tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(TINYPY_CODE_OBJECT(generator->code)->name);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_traceback_tb_next(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_traceback_object_t *traceback = TINYPY_TRACEBACK_OBJECT(value);
    tinypy_value_t *result = __tinypy_object_optional(vm, traceback->next);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_traceback_tb_frame(tinypy_value_t *value) {
    tinypy_traceback_object_t *traceback = TINYPY_TRACEBACK_OBJECT(value);
    tinypy_value_t *result = TINYPY_RET(traceback->frame);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_traceback_tb_lasti(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_traceback_object_t *traceback = TINYPY_TRACEBACK_OBJECT(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, traceback->last_instruction);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_traceback_tb_lineno(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_traceback_object_t *traceback = TINYPY_TRACEBACK_OBJECT(value);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, traceback->line_number);
    return result;
}
//////////////////////////////////////////////////////////////////////////
typedef tinypy_value_t *(*tinypy_builtin_attribute_getter_t)(tinypy_value_t *value);
static const tinypy_builtin_attribute_getter_t __tinypy_object_type_getters[TINYPY_BUILTIN_ATTRIBUTE_COUNT] = {
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_NAME] = __tinypy_object_get_type_special_name,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_DICT] = __tinypy_object_get_type_special_dict,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_BASES] = __tinypy_object_get_type_special_bases,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_MRO] = __tinypy_object_get_type_special_mro,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_BASE] = __tinypy_object_get_type_special_base,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_FLAGS] = __tinypy_object_get_type_special_flags,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_BASICSIZE] = __tinypy_object_get_type_special_basicsize,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_ITEMSIZE] = __tinypy_object_get_type_special_itemsize,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_DICTOFFSET] = __tinypy_object_get_type_special_dictoffset,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_WEAKREFOFFSET] = __tinypy_object_get_type_special_weakrefoffset,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_MODULE] = __tinypy_object_get_type_special_module,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_DOC] = __tinypy_object_get_type_special_doc,
};
static const tinypy_builtin_attribute_getter_t __tinypy_object_function_getters[TINYPY_BUILTIN_ATTRIBUTE_COUNT] = {
    [TINYPY_BUILTIN_ATTRIBUTE_FUNC_GLOBALS] = __tinypy_object_get_function_func_globals,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_GLOBALS] = __tinypy_object_get_function_func_globals,
    [TINYPY_BUILTIN_ATTRIBUTE_FUNC_DEFAULTS] = __tinypy_object_get_function_func_defaults,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_DEFAULTS] = __tinypy_object_get_function_func_defaults,
    [TINYPY_BUILTIN_ATTRIBUTE_FUNC_CLOSURE] = __tinypy_object_get_function_func_closure,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_CLOSURE] = __tinypy_object_get_function_func_closure,
    [TINYPY_BUILTIN_ATTRIBUTE_FUNC_NAME] = __tinypy_object_get_function_func_name,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_NAME] = __tinypy_object_get_function_func_name,
    [TINYPY_BUILTIN_ATTRIBUTE_FUNC_DOC] = __tinypy_object_get_function_func_doc,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_DOC] = __tinypy_object_get_function_func_doc,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_MODULE] = __tinypy_object_get_function_special_module,
    [TINYPY_BUILTIN_ATTRIBUTE_FUNC_DICT] = __tinypy_object_get_function_func_dict,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_DICT] = __tinypy_object_get_function_func_dict,
};
static const tinypy_builtin_attribute_getter_t __tinypy_object_method_getters[TINYPY_BUILTIN_ATTRIBUTE_COUNT] = {
    [TINYPY_BUILTIN_ATTRIBUTE_IM_FUNC] = __tinypy_object_get_method_im_func,
    [TINYPY_BUILTIN_ATTRIBUTE_IM_SELF] = __tinypy_object_get_method_im_self,
    [TINYPY_BUILTIN_ATTRIBUTE_IM_CLASS] = __tinypy_object_get_method_im_class,
};
static const tinypy_builtin_attribute_getter_t __tinypy_object_partial_getters[TINYPY_BUILTIN_ATTRIBUTE_COUNT] = {
    [TINYPY_BUILTIN_ATTRIBUTE_FUNC] = __tinypy_object_get_partial_func,
    [TINYPY_BUILTIN_ATTRIBUTE_ARGS] = __tinypy_object_get_partial_args,
    [TINYPY_BUILTIN_ATTRIBUTE_KEYWORDS] = __tinypy_object_get_partial_keywords,
};
static const tinypy_builtin_attribute_getter_t __tinypy_object_sre_pattern_getters[TINYPY_BUILTIN_ATTRIBUTE_COUNT] = {
    [TINYPY_BUILTIN_ATTRIBUTE_PATTERN] = __tinypy_object_get_sre_pattern_pattern,
    [TINYPY_BUILTIN_ATTRIBUTE_FLAGS] = __tinypy_object_get_sre_pattern_flags,
    [TINYPY_BUILTIN_ATTRIBUTE_GROUPS] = __tinypy_object_get_sre_pattern_groups,
    [TINYPY_BUILTIN_ATTRIBUTE_GROUPINDEX] = __tinypy_object_get_sre_pattern_groupindex,
};
static const tinypy_builtin_attribute_getter_t __tinypy_object_sre_match_getters[TINYPY_BUILTIN_ATTRIBUTE_COUNT] = {
    [TINYPY_BUILTIN_ATTRIBUTE_RE] = __tinypy_object_get_sre_match_re,
    [TINYPY_BUILTIN_ATTRIBUTE_STRING] = __tinypy_object_get_sre_match_string,
    [TINYPY_BUILTIN_ATTRIBUTE_POS] = __tinypy_object_get_sre_match_pos,
    [TINYPY_BUILTIN_ATTRIBUTE_ENDPOS] = __tinypy_object_get_sre_match_endpos,
    [TINYPY_BUILTIN_ATTRIBUTE_LASTINDEX] = __tinypy_object_get_sre_match_lastindex,
    [TINYPY_BUILTIN_ATTRIBUTE_REGS] = __tinypy_object_get_sre_match_regs,
    [TINYPY_BUILTIN_ATTRIBUTE_LASTGROUP] = __tinypy_object_get_sre_match_lastgroup,
};
static const tinypy_builtin_attribute_getter_t __tinypy_object_code_getters[TINYPY_BUILTIN_ATTRIBUTE_COUNT] = {
    [TINYPY_BUILTIN_ATTRIBUTE_CO_ARGCOUNT] = __tinypy_object_get_code_co_argcount,
    [TINYPY_BUILTIN_ATTRIBUTE_CO_NLOCALS] = __tinypy_object_get_code_co_nlocals,
    [TINYPY_BUILTIN_ATTRIBUTE_CO_STACKSIZE] = __tinypy_object_get_code_co_stacksize,
    [TINYPY_BUILTIN_ATTRIBUTE_CO_FLAGS] = __tinypy_object_get_code_co_flags,
    [TINYPY_BUILTIN_ATTRIBUTE_CO_CODE] = __tinypy_object_get_code_co_code,
    [TINYPY_BUILTIN_ATTRIBUTE_CO_CONSTS] = __tinypy_object_get_code_co_consts,
    [TINYPY_BUILTIN_ATTRIBUTE_CO_NAMES] = __tinypy_object_get_code_co_names,
    [TINYPY_BUILTIN_ATTRIBUTE_CO_VARNAMES] = __tinypy_object_get_code_co_varnames,
    [TINYPY_BUILTIN_ATTRIBUTE_CO_FREEVARS] = __tinypy_object_get_code_co_freevars,
    [TINYPY_BUILTIN_ATTRIBUTE_CO_CELLVARS] = __tinypy_object_get_code_co_cellvars,
    [TINYPY_BUILTIN_ATTRIBUTE_CO_FILENAME] = __tinypy_object_get_code_co_filename,
    [TINYPY_BUILTIN_ATTRIBUTE_CO_NAME] = __tinypy_object_get_code_co_name,
    [TINYPY_BUILTIN_ATTRIBUTE_CO_FIRSTLINENO] = __tinypy_object_get_code_co_firstlineno,
    [TINYPY_BUILTIN_ATTRIBUTE_CO_LNOTAB] = __tinypy_object_get_code_co_lnotab,
};
static const tinypy_builtin_attribute_getter_t __tinypy_object_frame_getters[TINYPY_BUILTIN_ATTRIBUTE_COUNT] = {
    [TINYPY_BUILTIN_ATTRIBUTE_F_BACK] = __tinypy_object_get_frame_f_back,
    [TINYPY_BUILTIN_ATTRIBUTE_F_CODE] = __tinypy_object_get_frame_f_code,
    [TINYPY_BUILTIN_ATTRIBUTE_F_BUILTINS] = __tinypy_object_get_frame_f_builtins,
    [TINYPY_BUILTIN_ATTRIBUTE_F_GLOBALS] = __tinypy_object_get_frame_f_globals,
    [TINYPY_BUILTIN_ATTRIBUTE_F_LOCALS] = __tinypy_object_get_frame_f_locals,
    [TINYPY_BUILTIN_ATTRIBUTE_F_LASTI] = __tinypy_object_get_frame_f_lasti,
    [TINYPY_BUILTIN_ATTRIBUTE_F_LINENO] = __tinypy_object_get_frame_f_lineno,
};
static const tinypy_builtin_attribute_getter_t __tinypy_object_generator_getters[TINYPY_BUILTIN_ATTRIBUTE_COUNT] = {
    [TINYPY_BUILTIN_ATTRIBUTE_GI_FRAME] = __tinypy_object_get_generator_gi_frame,
    [TINYPY_BUILTIN_ATTRIBUTE_GI_CODE] = __tinypy_object_get_generator_gi_code,
    [TINYPY_BUILTIN_ATTRIBUTE_GI_RUNNING] = __tinypy_object_get_generator_gi_running,
    [TINYPY_BUILTIN_ATTRIBUTE_SPECIAL_NAME] = __tinypy_object_get_generator_special_name,
};
static const tinypy_builtin_attribute_getter_t __tinypy_object_traceback_getters[TINYPY_BUILTIN_ATTRIBUTE_COUNT] = {
    [TINYPY_BUILTIN_ATTRIBUTE_TB_NEXT] = __tinypy_object_get_traceback_tb_next,
    [TINYPY_BUILTIN_ATTRIBUTE_TB_FRAME] = __tinypy_object_get_traceback_tb_frame,
    [TINYPY_BUILTIN_ATTRIBUTE_TB_LASTI] = __tinypy_object_get_traceback_tb_lasti,
    [TINYPY_BUILTIN_ATTRIBUTE_TB_LINENO] = __tinypy_object_get_traceback_tb_lineno,
};
static const tinypy_builtin_attribute_getter_t *const __tinypy_object_builtin_getters[TINYPY_VALUE_NATIVE_INSTANCE + 1U] = {
    [TINYPY_VALUE_TYPE] = __tinypy_object_type_getters,
    [TINYPY_VALUE_FUNCTION] = __tinypy_object_function_getters,
    [TINYPY_VALUE_METHOD] = __tinypy_object_method_getters,
    [TINYPY_VALUE_PARTIAL] = __tinypy_object_partial_getters,
    [TINYPY_VALUE_SRE_PATTERN] = __tinypy_object_sre_pattern_getters,
    [TINYPY_VALUE_SRE_MATCH] = __tinypy_object_sre_match_getters,
    [TINYPY_VALUE_CODE] = __tinypy_object_code_getters,
    [TINYPY_VALUE_FRAME] = __tinypy_object_frame_getters,
    [TINYPY_VALUE_GENERATOR] = __tinypy_object_generator_getters,
    [TINYPY_VALUE_TRACEBACK] = __tinypy_object_traceback_getters,
};
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_special_class(tinypy_value_t *value) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(value);
        return TINYPY_RET(instance->class_object);
    }
    return TINYPY_RET(&value->type->base.base);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_builtin_attribute(tinypy_value_t *value, tinypy_value_t *key) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    if (key == vm->internal_special_class_key) {
        tinypy_value_t *result = __tinypy_object_get_special_class(value);
        return result;
    }
    const char *name = (const char *)TINYPY_TEXT_BYTES(key);
    size_t name_size = TINYPY_TEXT_BYTE_SIZE(key);
    tinypy_bool_t dunder_name = name_size >= 2U && name[0] == '_' && name[1] == '_' ? TINYPY_TRUE : TINYPY_FALSE;

    if (dunder_name != TINYPY_FALSE) {
        if (TINYPY_NAME_EQ(key, vm->internal_special_class_key) != TINYPY_FALSE) {
            tinypy_value_t *result = __tinypy_object_get_special_class(value);
            return result;
        }
        if (kind == TINYPY_VALUE_NATIVE_INSTANCE && value->type->call != NULL && TINYPY_NAME_EQ(key, vm->internal_special_call_key) != TINYPY_FALSE) {
            tinypy_value_t *return_value_3 = TINYPY_RET(value);
            return return_value_3;
        }
        if (kind != TINYPY_VALUE_MODULE && value->type->dict_offset != 0U && TINYPY_NAME_EQ(key, vm->internal_special_dict_key) != TINYPY_FALSE) {
            tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);

            if (value->type->has_instance_dict == TINYPY_FALSE) {
                return NULL;
            }
            if (*dict_slot == NULL) {
                *dict_slot = tinypy_dict_new(vm);
            }
            tinypy_value_t *return_value_4 = TINYPY_RET(*dict_slot);
            return return_value_4;
        }
    }
    if (kind == TINYPY_VALUE_FUNCTION && (TINYPY_NAME_EQ(key, vm->internal_func_code_key) != TINYPY_FALSE || TINYPY_NAME_EQ(key, vm->internal_special_code_key) != TINYPY_FALSE)) {
        tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(value);
        return TINYPY_RET(function->code);
    }
    if (kind == TINYPY_VALUE_MODULE) {
        if (TINYPY_NAME_EQ(key, vm->internal_special_dict_key) != TINYPY_FALSE) {
            tinypy_value_t *module_dict = tinypy_module_dict(value);
            tinypy_value_t *result = __tinypy_object_optional(vm, module_dict);
            return result;
        }
        return NULL;
    }
    const tinypy_builtin_attribute_getter_t *getters = __tinypy_object_builtin_getters[kind];
    if (getters == NULL) {
        return NULL;
    }
    tinypy_builtin_attribute_e attribute_id = __tinypy_object_builtin_attribute_id(vm, key);
    tinypy_builtin_attribute_getter_t getter = getters[attribute_id];
    if (getter == NULL) {
        return NULL;
    }
    tinypy_value_t *result = getter(value);
    return result;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_call_attribute_hook(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *hook_key, tinypy_value_t *internal_name_key, tinypy_error_t **out_error) {
    tinypy_value_t *attribute = tinypy_internal_type_lookup_key(vm, value->type, hook_key);
    tinypy_value_t *result;

    tinypy_value_t *method = tinypy_internal_descriptor_get_value(vm, attribute, value, value->type, out_error);
    if (method == NULL) {
        return NULL;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &internal_name_key, 1U);
    result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static const size_t __tinypy_object_operator_name_offsets[TINYPY_SPECIAL_OPERATOR_COUNT] = {
    offsetof(tinypy_vm_t, internal_special_eq_key),
    offsetof(tinypy_vm_t, internal_special_ne_key),
    offsetof(tinypy_vm_t, internal_special_lt_key),
    offsetof(tinypy_vm_t, internal_special_le_key),
    offsetof(tinypy_vm_t, internal_special_gt_key),
    offsetof(tinypy_vm_t, internal_special_ge_key),
    offsetof(tinypy_vm_t, internal_special_cmp_key),
    offsetof(tinypy_vm_t, internal_special_add_key),
    offsetof(tinypy_vm_t, internal_special_sub_key),
    offsetof(tinypy_vm_t, internal_special_mul_key),
    offsetof(tinypy_vm_t, internal_special_div_key),
    offsetof(tinypy_vm_t, internal_special_mod_key),
    offsetof(tinypy_vm_t, internal_special_neg_key),
    offsetof(tinypy_vm_t, internal_special_pos_key),
    offsetof(tinypy_vm_t, internal_special_abs_key),
    offsetof(tinypy_vm_t, internal_special_int_key),
    offsetof(tinypy_vm_t, internal_special_radd_key),
    offsetof(tinypy_vm_t, internal_special_rsub_key),
    offsetof(tinypy_vm_t, internal_special_rmul_key),
    offsetof(tinypy_vm_t, internal_special_rdiv_key),
    offsetof(tinypy_vm_t, internal_special_float_key),
    offsetof(tinypy_vm_t, internal_special_divmod_key),
    offsetof(tinypy_vm_t, internal_special_coerce_key),
    offsetof(tinypy_vm_t, internal_special_truediv_key),
};
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_special_operator_key(tinypy_vm_t *vm, size_t index) {
    tinypy_value_t *key = *(tinypy_value_t **)((uint8_t *)vm + __tinypy_object_operator_name_offsets[index]);
    return key;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_object_special_key_hash(const char *name, size_t size) {
    uint32_t hash = UINT32_C(2166136261);
    for (size_t index = 0U; index < size; ++index) {
        hash = (hash ^ (uint8_t)name[index]) * UINT32_C(16777619);
    }
    return (size_t)hash & (TINYPY_INTERNAL_KEY_TABLE_SIZE - 1U);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_object_special_key_insert(tinypy_vm_t *vm, tinypy_value_t *key) {
    size_t size = TINYPY_TEXT_BYTE_SIZE(key);
    const char *bytes = (const char *)TINYPY_TEXT_BYTES(key);
    size_t index = __tinypy_object_special_key_hash(bytes, size);

    for (;;) {
        tinypy_value_t *stored = vm->internal_key_table[index];
        if (stored == NULL) {
            vm->internal_key_table[index] = key;
            return;
        }
        if (TINYPY_TEXT_BYTE_SIZE(stored) == size && memcmp(TINYPY_TEXT_BYTES(stored), bytes, size) == 0) {
            return;
        }
        index = (index + 1U) & (TINYPY_INTERNAL_KEY_TABLE_SIZE - 1U);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_object_initialize_special_keys(tinypy_vm_t *vm) {
#define TINYPY_INTERNAL_KEY_INSERT(field, name, intern_name) \
    TINYPY_STRING_OBJECT(vm->field)->internal_metadata = &__tinypy_builtin_attribute_metadata[TINYPY_BUILTIN_ATTRIBUTE_NONE]; \
    __tinypy_object_special_key_insert(vm, vm->field);
    TINYPY_INTERNAL_KEY_LIST(TINYPY_INTERNAL_KEY_INSERT)
#undef TINYPY_INTERNAL_KEY_INSERT
    for (size_t index = 0U; index < sizeof(__tinypy_builtin_attribute_specs) / sizeof(__tinypy_builtin_attribute_specs[0]); ++index) {
        const tinypy_builtin_attribute_spec_t *spec = &__tinypy_builtin_attribute_specs[index];
        tinypy_value_t *key = *(tinypy_value_t **)((uint8_t *)vm + spec->name_offset);
        TINYPY_STRING_OBJECT(key)->internal_metadata = &__tinypy_builtin_attribute_metadata[spec->attribute];
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_cached_special_key(tinypy_vm_t *vm, const char *name, size_t name_size) {
    size_t index = __tinypy_object_special_key_hash(name, name_size);

    for (;;) {
        tinypy_value_t *key = vm->internal_key_table[index];
        if (key == NULL) {
            return NULL;
        }
        if (TINYPY_TEXT_BYTE_SIZE(key) == name_size && memcmp(TINYPY_TEXT_BYTES(key), name, name_size) == 0) {
            return key;
        }
        index = (index + 1U) & (TINYPY_INTERNAL_KEY_TABLE_SIZE - 1U);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_name_from_bytes(tinypy_vm_t *vm, const char *name, size_t name_size) {
    tinypy_value_t *key = __tinypy_object_cached_special_key(vm, name, name_size);

    if (key != NULL) {
        return TINYPY_RET(key);
    }
    tinypy_value_t *result = tinypy_string_from_bytes(vm, name, name_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_from_literal(tinypy_vm_t *vm, const char *bytes, size_t size) {
    tinypy_value_t *preset = __tinypy_object_cached_special_key(vm, bytes, size);
    if (preset != NULL && tinypy_internal_string_is_interned(preset) != 0) {
        return preset;
    }
    tinypy_value_t *key = tinypy_string_from_bytes(vm, bytes, size);
    (void)tinypy_internal_string_intern(&key, NULL);
    if (vm->internal_strings == NULL) {
        vm->internal_strings = tinypy_dict_new(vm);
    }
    if (tinypy_dict_get_optional(vm->internal_strings, key) == NULL) {
        tinypy_dict_set(vm->internal_strings, key, &vm->none_object.base);
    }
    TINYPY_DECREF(key);
    return key;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_object_has_special_key(tinypy_value_t *value, tinypy_value_t *key) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_bool_t result = tinypy_internal_old_instance_has_special_key(value, key);
        return result;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_bool_t result = tinypy_internal_type_lookup_key(vm, value->type, key) != NULL ? TINYPY_TRUE : TINYPY_FALSE;
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_object_has_special_override_key(tinypy_value_t *value, tinypy_value_t *key) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_value_t *attribute;
    tinypy_value_t *builtin_attribute = NULL;

    if (kind == TINYPY_VALUE_OLD_INSTANCE) {
        if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_STRING) {
            tinypy_bool_t result = tinypy_internal_old_instance_has_special_key(value, key);

            return result;
        }
        return TINYPY_FALSE;
    }
    if ((size_t)kind < TINYPY_BUILTIN_TYPE_COUNT && value->type == &vm->types[kind]) {
        return TINYPY_FALSE;
    }
    /* Immutable VM-native descriptor types share the native-function payload,
       but their own slots are built-ins rather than Python overrides. */
    if (value->type == vm->native_method_descriptor_type || value->type == vm->native_wrapper_descriptor_type || value->type == vm->native_method_wrapper_type) {
        return TINYPY_FALSE;
    }
    attribute = tinypy_internal_type_lookup_key(vm, value->type, key);
    if (attribute == NULL) {
        return TINYPY_FALSE;
    }
    if ((size_t)kind < TINYPY_BUILTIN_TYPE_COUNT) {
        builtin_attribute = tinypy_internal_type_lookup_key(vm, &vm->types[kind], key);
    }
    else if (kind == TINYPY_VALUE_NATIVE_INSTANCE) {
        builtin_attribute = tinypy_internal_type_lookup_key(vm, &vm->types[TINYPY_VALUE_INSTANCE], key);
    }
    return attribute != builtin_attribute ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_descriptor_has_get(tinypy_vm_t *vm, tinypy_value_t *attribute) {
    if (attribute->type->descriptor_get != NULL) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t has_get = (attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_type_lookup_key(vm, attribute->type, vm->internal_special_get_key) != NULL ? TINYPY_TRUE : TINYPY_FALSE;

    return has_get;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_descriptor_is_data(tinypy_vm_t *vm, tinypy_value_t *attribute) {
    if (attribute->type->descriptor_set != NULL) {
        return TINYPY_TRUE;
    }
    if ((attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = tinypy_internal_type_lookup_key(vm, attribute->type, vm->internal_special_set_key) != NULL || tinypy_internal_type_lookup_key(vm, attribute->type, vm->internal_special_delete_key) != NULL ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_descriptor_get_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    if (attribute->type->descriptor_get != NULL) {
        tinypy_value_t *return_value_1 = attribute->type->descriptor_get(attribute, instance, owner, out_error);
        return return_value_1;
    }
    if ((attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_type_lookup_key(vm, attribute->type, vm->internal_special_get_key) != NULL) {
        tinypy_value_t *method = tinypy_internal_object_get_special_key(attribute, vm->internal_special_get_key, out_error);
        tinypy_value_t *none = NULL;
        tinypy_value_t *items[2];
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) {
            return NULL;
        }
        if (instance == NULL) {
            none = TINYPY_RET_NONE(vm);
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
    return TINYPY_RET(attribute);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_descriptor_get_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    TINYPY_INCREF(attribute);
    TINYPY_INCREF(&owner->base.base);
    tinypy_value_t *result = __tinypy_descriptor_get_value(vm, attribute, instance, owner, out_error);
    TINYPY_DECREF(&owner->base.base);
    TINYPY_DECREF(attribute);
    return result;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_get_special_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *result = tinypy_internal_object_get_attr_key(value, key, out_error);

        return result;
    }
    tinypy_value_t *attribute = tinypy_internal_type_lookup_key(vm, value->type, key);

    if (attribute == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_descriptor_get_value(vm, attribute, value, value->type, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Static built-in types are created without an mro tuple; it is materialised
   on first use so that both __mro__ and type.mro() see it. */
tinypy_value_t *tinypy_internal_type_mro_tuple(tinypy_type_t *type) {
    if (type->mro == NULL) {
        type->mro = __tinypy_object_type_tuple(type->vm, type, INT32_C(1));
    }
    return type->mro;
}
//////////////////////////////////////////////////////////////////////////
/* Mirrors PyMapping_Check: subscriptable objects other than the built-in
   sequences count as mappings. */
tinypy_bool_t tinypy_internal_object_is_mapping(tinypy_vm_t *vm, tinypy_value_t *value) {
    switch (TINYPY_VALUE_KIND(value)) {
    case TINYPY_VALUE_DICT:
    case TINYPY_VALUE_BYTEARRAY:
        return TINYPY_TRUE;
    case TINYPY_VALUE_OLD_INSTANCE: {
        tinypy_internal_exception_state_t state;
        tinypy_value_t *attribute;
        tinypy_bool_t result;

        tinypy_internal_exception_preserve_begin(vm, &state);
        attribute = tinypy_internal_object_get_attr_key(value, vm->internal_special_getitem_key, NULL);
        result = attribute != NULL ? TINYPY_TRUE : TINYPY_FALSE;
        if (attribute != NULL) {
            TINYPY_DECREF(attribute);
        }
        tinypy_internal_exception_preserve_end(vm, &state);
        return result;
    }
    case TINYPY_VALUE_LIST:
    case TINYPY_VALUE_TUPLE:
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_UNICODE:
    case TINYPY_VALUE_BUFFER:
        return TINYPY_FALSE;
    default:
        break;
    }
    tinypy_bool_t subscriptable = tinypy_internal_type_lookup_key(vm, value->type, vm->internal_special_getitem_key) != NULL ? TINYPY_TRUE : TINYPY_FALSE;

    return subscriptable;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_descriptor_set_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (attribute->type->descriptor_set != NULL) {
        tinypy_bool_t return_value_1 = attribute->type->descriptor_set(attribute, instance, value, out_error);
        return return_value_1;
    }
    if ((attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_type_lookup_key(vm, attribute->type, vm->internal_special_set_key) != NULL) {
        tinypy_value_t *method = tinypy_internal_object_get_special_key(attribute, vm->internal_special_set_key, out_error);
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
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "__set__", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_descriptor_set_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_INCREF(attribute);
    tinypy_bool_t result = __tinypy_descriptor_set_value(vm, attribute, instance, value, out_error);
    TINYPY_DECREF(attribute);
    return result;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_descriptor_delete_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_error_t **out_error) {
    if (attribute->type->descriptor_set != NULL) {
        tinypy_bool_t return_value_1 = attribute->type->descriptor_set(attribute, instance, NULL, out_error);
        return return_value_1;
    }
    if ((attribute->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_type_lookup_key(vm, attribute->type, vm->internal_special_delete_key) != NULL) {
        tinypy_value_t *method = tinypy_internal_object_get_special_key(attribute, vm->internal_special_delete_key, out_error);
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
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "__delete__", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_descriptor_delete_value(tinypy_vm_t *vm, tinypy_value_t *attribute, tinypy_value_t *instance, tinypy_error_t **out_error) {
    TINYPY_INCREF(attribute);
    tinypy_bool_t result = __tinypy_descriptor_delete_value(vm, attribute, instance, out_error);
    TINYPY_DECREF(attribute);
    return result;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
/* PyObject_GenericGetAttr: a data descriptor wins over the instance dict
   only when it also defines __get__. */
static tinypy_value_t *__tinypy_internal_instance_attribute(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_type_t *type = value->type;
    tinypy_value_t *attribute;
    tinypy_value_t *result = NULL;

    TINYPY_INCREF(&type->base.base);
    attribute = tinypy_internal_type_lookup_key(vm, type, key);
    if (attribute != NULL) {
        TINYPY_INCREF(attribute);
        if (tinypy_internal_descriptor_has_get(vm, attribute) != 0 && tinypy_internal_descriptor_is_data(vm, attribute) != 0) {
            result = tinypy_internal_descriptor_get_value(vm, attribute, value, type, out_error);
            goto done;
        }
    }
    tinypy_value_t **dict_slot = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FUNCTION ? &TINYPY_FUNCTION_OBJECT(value)->dict : tinypy_internal_object_dict_slot(value);
    if (dict_slot != NULL && *dict_slot != NULL) {
        tinypy_value_t *dict = *dict_slot;

        TINYPY_INCREF(dict);
        result = tinypy_internal_dict_get_optional_suppressed(vm, dict, key);
        if (result != NULL) {
            TINYPY_INCREF(result);
            TINYPY_DECREF(dict);
            goto done;
        }
        TINYPY_DECREF(dict);
    }
    if (attribute != NULL) {
        result = tinypy_internal_descriptor_get_value(vm, attribute, value, type, out_error);
    }
done:
    if (attribute != NULL) {
        TINYPY_DECREF(attribute);
    }
    TINYPY_DECREF(&type->base.base);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_type_attribute(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_type_t *type = (tinypy_type_t *)value;
    tinypy_type_t *metaclass = value->type;
    tinypy_value_t *metaclass_attribute;
    tinypy_value_t *result = NULL;

    TINYPY_INCREF(&metaclass->base.base);
    metaclass_attribute = tinypy_internal_type_lookup_key(vm, metaclass, key);
    if (metaclass_attribute != NULL) {
        TINYPY_INCREF(metaclass_attribute);
        if (tinypy_internal_descriptor_has_get(vm, metaclass_attribute) != 0 && tinypy_internal_descriptor_is_data(vm, metaclass_attribute) != 0) {
            result = tinypy_internal_descriptor_get_value(vm, metaclass_attribute, value, metaclass, out_error);
            goto done;
        }
    }
    tinypy_value_t *attribute = tinypy_internal_type_lookup_key(vm, type, key);
    if (attribute != NULL) {
        result = tinypy_internal_descriptor_get_value(vm, attribute, NULL, type, out_error);
    }
    else if (metaclass_attribute != NULL) {
        result = tinypy_internal_descriptor_get_value(vm, metaclass_attribute, value, metaclass, out_error);
    }
done:
    if (metaclass_attribute != NULL) {
        TINYPY_DECREF(metaclass_attribute);
    }
    TINYPY_DECREF(&metaclass->base.base);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_object_attribute_error(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    if (vm->raised_type != NULL && TINYPY_VALUE_KIND(vm->raised_type) == TINYPY_VALUE_TYPE) {
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
static tinypy_value_t *__tinypy_object_getattr_fallback(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *result, tinypy_bool_t suppress_missing, tinypy_bool_t allow_getattr, tinypy_bool_t *out_missing, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_bool_t has_getattr;

    if (result != NULL) {
        return result;
    }
    has_getattr = allow_getattr != TINYPY_FALSE && (value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_type_lookup_key(vm, value->type, vm->internal_special_getattr_key) != NULL ? TINYPY_TRUE : TINYPY_FALSE;
    if (has_getattr != TINYPY_FALSE) {
        if (__tinypy_object_error_present(vm, out_error) != 0) {
            if (__tinypy_object_attribute_error(vm, out_error) == 0) {
                return NULL;
            }
            __tinypy_object_clear_attribute_error(vm, out_error);
        }
        result = __tinypy_object_call_attribute_hook(vm, value, vm->internal_special_getattr_key, key, out_error);
        if (result != NULL) {
            return result;
        }
    }
    if (suppress_missing != TINYPY_FALSE) {
        if (__tinypy_object_error_present(vm, out_error) != 0) {
            if (__tinypy_object_attribute_error(vm, out_error) == 0) {
                return NULL;
            }
            __tinypy_object_clear_attribute_error(vm, out_error);
        }
        *out_missing = TINYPY_TRUE;
        return NULL;
    }
    if (vm->raised_type == NULL && (out_error == NULL || *out_error == NULL)) {
        tinypy_internal_object_make_attribute_error_key(value, key, out_error);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_object_get_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_bool_t suppress_missing, tinypy_bool_t skip_custom, tinypy_bool_t *out_missing, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    const char *name = NULL;
    size_t name_size = 0U;
    tinypy_bool_t internal_text_key = __tinypy_object_key_text(key, &name, &name_size);
    tinypy_value_t *result = NULL;
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    TINYPY_CLEAR_ERROR(out_error);
    *out_missing = TINYPY_FALSE;
    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *encoded_key = tinypy_internal_object_encode_attribute_name(key, out_error);

        if (encoded_key == NULL) {
            return NULL;
        }
        result = __tinypy_object_get_attr_key(value, encoded_key, suppress_missing, skip_custom, out_missing, out_error);
        TINYPY_DECREF(encoded_key);
        return result;
    }
    if (internal_text_key != TINYPY_FALSE) {
        value->type->flags &= ~TINYPY_TYPE_FLAG_NEEDS_ATTRIBUTE_READY;
        if (kind == TINYPY_VALUE_TYPE) {
            ((tinypy_type_t *)value)->flags &= ~TINYPY_TYPE_FLAG_NEEDS_ATTRIBUTE_READY;
        }
    }
    if (skip_custom == TINYPY_FALSE && (value->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_object_has_special_override_key(value, vm->internal_special_getattribute_key) != 0) {
        result = __tinypy_object_call_attribute_hook(vm, value, vm->internal_special_getattribute_key, key, out_error);
        tinypy_value_t *return_value_1 = __tinypy_object_getattr_fallback(value, key, result, suppress_missing, TINYPY_TRUE, out_missing, out_error);
        return return_value_1;
    }
    tinypy_bool_t canonical_key = key->type == &vm->types[TINYPY_VALUE_STRING] ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_bool_t builtin_attribute = canonical_key != TINYPY_FALSE && internal_text_key != TINYPY_FALSE && ((kind != TINYPY_VALUE_INSTANCE && (value->type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U) || name_size < 2U || name[0] != '_') ? TINYPY_TRUE : TINYPY_FALSE;
    if (canonical_key != TINYPY_FALSE && internal_text_key != TINYPY_FALSE && builtin_attribute == TINYPY_FALSE) {
        tinypy_value_t *attribute = tinypy_internal_type_lookup_key(vm, value->type, key);
        builtin_attribute = attribute == NULL || TINYPY_VALUE_KIND(attribute) == TINYPY_VALUE_GETSET_DESCRIPTOR ? TINYPY_TRUE : TINYPY_FALSE;
    }
    if (builtin_attribute != TINYPY_FALSE) {
        result = tinypy_internal_object_builtin_attribute(value, key);
        if (result != NULL) {
            return result;
        }
    }
    if (value->type->get_attribute != NULL) {
        result = value->type->get_attribute(value, key, out_error);
        tinypy_value_t *return_value_2 = __tinypy_object_getattr_fallback(value, key, result, suppress_missing, skip_custom == TINYPY_FALSE ? TINYPY_TRUE : TINYPY_FALSE, out_missing, out_error);
        return return_value_2;
    }
    if (kind == TINYPY_VALUE_INSTANCE || kind == TINYPY_VALUE_FUNCTION || value->type->dict_offset != 0U) {
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
    else {
        tinypy_value_t *attribute = tinypy_internal_type_lookup_key(vm, value->type, key);

        if (attribute != NULL) {
            result = tinypy_internal_descriptor_get_value(vm, attribute, value, value->type, out_error);
        }
    }
    if (result == NULL && kind == TINYPY_VALUE_METHOD) {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(value);

        tinypy_value_t *return_value_3 = __tinypy_object_get_attr_key(method->function, key, suppress_missing, skip_custom, out_missing, out_error);
        return return_value_3;
    }
    tinypy_value_t *return_value_4 = __tinypy_object_getattr_fallback(value, key, result, suppress_missing, skip_custom == TINYPY_FALSE ? TINYPY_TRUE : TINYPY_FALSE, out_missing, out_error);
    return return_value_4;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_get_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_bool_t missing;

    tinypy_value_t *return_value_1 = __tinypy_object_get_attr_key(value, key, TINYPY_FALSE, TINYPY_FALSE, &missing, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_get_base_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_bool_t missing;

    tinypy_value_t *return_value_1 = __tinypy_object_get_attr_key(value, key, TINYPY_FALSE, TINYPY_TRUE, &missing, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_object_get_optional_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t **out_value, tinypy_error_t **out_error) {
    tinypy_bool_t missing;

    *out_value = __tinypy_object_get_attr_key(value, key, TINYPY_TRUE, TINYPY_FALSE, &missing, out_error);
    if (*out_value != NULL) {
        return INT32_C(1);
    }
    return missing != TINYPY_FALSE ? INT32_C(0) : -INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_object_get_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
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
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
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
    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *encoded_key = tinypy_internal_object_encode_attribute_name(key, out_error);

        if (encoded_key == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t stored = tinypy_internal_object_set_attr_key(value, encoded_key, attribute_value, out_error);
        TINYPY_DECREF(encoded_key);
        return stored;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE && TINYPY_VALUE_KIND(key) == TINYPY_VALUE_STRING && key->type != &vm->types[TINYPY_VALUE_STRING]) {
        tinypy_value_t *name = tinypy_internal_name_from_bytes(vm, (const char *)TINYPY_TEXT_BYTES(key), TINYPY_TEXT_BYTE_SIZE(key));
        tinypy_bool_t stored = tinypy_internal_object_set_attr_key(value, name, attribute_value, out_error);

        TINYPY_DECREF(name);
        return stored;
    }
    tinypy_value_t *descriptor = tinypy_internal_type_lookup_key(vm, value->type, key);
    if (descriptor != NULL) {
        TINYPY_INCREF(descriptor);
        tinypy_bool_t data = tinypy_internal_descriptor_is_data(vm, descriptor);
        if (data != TINYPY_FALSE) {
            tinypy_bool_t stored = tinypy_internal_descriptor_set_value(vm, descriptor, value, attribute_value, out_error);
            TINYPY_DECREF(descriptor);
            return stored;
        }
        TINYPY_DECREF(descriptor);
    }
    if (value->type->set_attribute != NULL) {
        tinypy_bool_t return_value_2 = value->type->set_attribute(value, key, attribute_value, out_error);
        return return_value_2;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_INSTANCE || value->type->dict_offset != 0U) {
        tinypy_value_t **dict_slot;

        if (value->type->has_instance_dict == TINYPY_FALSE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "instance has no dictionary for this attribute", out_error);
            return TINYPY_FALSE;
        }
        dict_slot = tinypy_internal_object_dict_slot(value);
        if (*dict_slot == NULL) {
            *dict_slot = tinypy_dict_new(vm);
        }
        tinypy_bool_t inserted = tinypy_internal_dict_set_checked(vm, *dict_slot, key, attribute_value, out_error);
        return inserted;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE) {
        tinypy_type_t *type = (tinypy_type_t *)value;
        const char *name = NULL;
        size_t name_size = 0U;

        if ((type->flags & TINYPY_TYPE_FLAG_IMMUTABLE) != 0U) {
            __tinypy_object_immutable_type_error(value, out_error);
            return TINYPY_FALSE;
        }
        if (__tinypy_object_key_text(key, &name, &name_size) != 0) {
            if (TINYPY_NAME_EQ(key, vm->internal_special_bases_key) != 0) {
                tinypy_bool_t return_value_3 = tinypy_internal_type_set_bases(type, attribute_value, out_error);
                return return_value_3;
            }
            if (TINYPY_NAME_EQ(key, vm->internal_special_name_key) != 0) {
                tinypy_bool_t return_value_4 = tinypy_internal_type_set_name(type, attribute_value, out_error);
                return return_value_4;
            }
            if (__tinypy_object_type_metadata_read_only(key) != 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type metadata is read-only", out_error);
                return TINYPY_FALSE;
            }
        }
        tinypy_bool_t stored = tinypy_internal_dict_set_checked(vm, type->dict, key, attribute_value, out_error);
        return stored;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FUNCTION) {
        if (TINYPY_FUNCTION_OBJECT(value)->dict == NULL) {
            TINYPY_FUNCTION_OBJECT(value)->dict = tinypy_dict_new(vm);
        }
        tinypy_bool_t stored = tinypy_internal_dict_set_checked(vm, TINYPY_FUNCTION_OBJECT(value)->dict, key, attribute_value, out_error);
        return stored;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_MODULE) {
        tinypy_bool_t stored = tinypy_internal_dict_set_checked(vm, TINYPY_MODULE_OBJECT(value)->dict, key, attribute_value, out_error);
        return stored;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NATIVE_FUNCTION || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_GETSET_DESCRIPTOR || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_MEMBER_DESCRIPTOR) {
        size_t name_size = 0U;
        const char *name = NULL;

        if (__tinypy_object_key_text(key, &name, &name_size) != 0) {
            __tinypy_object_make_attribute_error(value, name, name_size, out_error);
            return TINYPY_FALSE;
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object attributes are read-only", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* Classic instances keep __setattr__ and __delattr__ in the class, and assign
   __class__ and __dict__ directly without consulting either hook. */
static tinypy_bool_t __tinypy_object_old_instance_set_attr(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *method;

    if (TINYPY_NAME_EQ(key, vm->internal_special_class_key) != 0) {
        tinypy_bool_t return_value_1 = tinypy_internal_old_instance_set_class(value, attribute_value, out_error);
        return return_value_1;
    }
    if (TINYPY_NAME_EQ(key, vm->internal_special_dict_key) != 0) {
        tinypy_bool_t return_value_2 = tinypy_internal_old_instance_set_dict(value, attribute_value, out_error);
        return return_value_2;
    }
    if (tinypy_internal_old_instance_has_special_key(value, vm->internal_special_setattr_key) != 0) {
        tinypy_value_t *items[2] = {key, attribute_value};
        tinypy_value_t *args;
        tinypy_value_t *result;

        method = tinypy_internal_object_get_special_key(value, vm->internal_special_setattr_key, out_error);
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
    tinypy_bool_t return_value_3 = tinypy_internal_old_instance_set_attribute(value, key, attribute_value, out_error);
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_object_old_instance_delete_attr(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *method;

    if (TINYPY_NAME_EQ(key, vm->internal_special_class_key) != 0) {
        tinypy_bool_t return_value_1 = tinypy_internal_old_instance_set_class(value, NULL, out_error);
        return return_value_1;
    }
    if (TINYPY_NAME_EQ(key, vm->internal_special_dict_key) != 0) {
        tinypy_bool_t return_value_2 = tinypy_internal_old_instance_set_dict(value, NULL, out_error);
        return return_value_2;
    }
    if (tinypy_internal_old_instance_has_special_key(value, vm->internal_special_delattr_key) != 0) {
        tinypy_value_t *args;
        tinypy_value_t *result;

        method = tinypy_internal_object_get_special_key(value, vm->internal_special_delattr_key, out_error);
        if (method == NULL) {
            return TINYPY_FALSE;
        }
        args = tinypy_tuple_from_items(vm, &key, 1U);
        result = tinypy_call(method, args, NULL, out_error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    tinypy_bool_t return_value_3 = tinypy_internal_old_instance_delete_attribute(value, key, out_error);
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_object_cold_attribute_error(tinypy_value_t *value, tinypy_value_t *key, tinypy_bool_t deleting, tinypy_error_t **out_error) {
    const char *name = (const char *)TINYPY_TEXT_BYTES(key);
    size_t size = TINYPY_TEXT_BYTE_SIZE(key);
    const char *terminator = (const char *)memchr(name, 0, size);

    if (terminator != NULL) {
        size = (size_t)(terminator - name);
    }
    if (size > 100U) {
        size = 100U;
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("'"),
        {value->type->name, value->type->name_size},
        TINYPY_MESSAGE_PART_LITERAL("' object has only read-only attributes ("),
        {deleting != TINYPY_FALSE ? "del" : "assign to", deleting != TINYPY_FALSE ? 3U : 9U},
        TINYPY_MESSAGE_PART_LITERAL(" ."),
        {name, size},
        TINYPY_MESSAGE_PART_LITERAL(")")
    };

    tinypy_internal_make_vm_error_parts(TINYPY_VALUE_VM(value), TINYPY_ERROR_TYPE, parts, 7U, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_object_set_attr_protocol_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *encoded_key = tinypy_internal_object_encode_attribute_name(key, out_error);

        if (encoded_key == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t stored = tinypy_internal_object_set_attr_protocol_key(value, encoded_key, attribute_value, out_error);
        TINYPY_DECREF(encoded_key);
        return stored;
    }
    if ((value->type->flags & TINYPY_TYPE_FLAG_NEEDS_ATTRIBUTE_READY) != 0U) {
        __tinypy_object_cold_attribute_error(value, key, TINYPY_FALSE, out_error);
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_bool_t return_value_0 = __tinypy_object_old_instance_set_attr(value, key, attribute_value, out_error);
        return return_value_0;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    if (tinypy_internal_object_has_special_override_key(value, vm->internal_special_setattr_key) != 0) {
        tinypy_value_t *attribute = tinypy_type_get_attr_key(value->type, vm->internal_special_setattr_key);
        tinypy_value_t *method = tinypy_internal_descriptor_get_value(vm, attribute, value, value->type, out_error);
        tinypy_value_t *items[2] = {key, attribute_value};
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
    tinypy_bool_t return_value_1 = tinypy_internal_object_set_attr_key(value, key, attribute_value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_object_set_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    tinypy_bool_t result;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
    result = tinypy_object_set_attr_value(value, key, attribute_value, out_error);
    TINYPY_DECREF(key);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_object_set_attr_value(tinypy_value_t *value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    tinypy_bool_t return_value_1 = tinypy_internal_object_set_attr_protocol_key(value, name, attribute_value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_object_delete_attr_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *dict = NULL;

    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *encoded_key = tinypy_internal_object_encode_attribute_name(key, out_error);

        if (encoded_key == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t deleted = tinypy_internal_object_delete_attr_key(value, encoded_key, out_error);
        TINYPY_DECREF(encoded_key);
        return deleted;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE && TINYPY_VALUE_KIND(key) == TINYPY_VALUE_STRING && key->type != &vm->types[TINYPY_VALUE_STRING]) {
        tinypy_value_t *name = tinypy_internal_name_from_bytes(vm, (const char *)TINYPY_TEXT_BYTES(key), TINYPY_TEXT_BYTE_SIZE(key));
        tinypy_bool_t deleted = tinypy_internal_object_delete_attr_key(value, name, out_error);

        TINYPY_DECREF(name);
        return deleted;
    }
    tinypy_value_t *descriptor = tinypy_internal_type_lookup_key(vm, value->type, key);
    if (descriptor != NULL) {
        TINYPY_INCREF(descriptor);
        tinypy_bool_t data = tinypy_internal_descriptor_is_data(vm, descriptor);
        if (data != TINYPY_FALSE) {
            tinypy_bool_t stored = tinypy_internal_descriptor_delete_value(vm, descriptor, value, out_error);
            TINYPY_DECREF(descriptor);
            return stored;
        }
        TINYPY_DECREF(descriptor);
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
        tinypy_type_t *type = (tinypy_type_t *)value;
        const char *name = NULL;
        size_t name_size = 0U;

        if ((type->flags & TINYPY_TYPE_FLAG_IMMUTABLE) != 0U) {
            __tinypy_object_immutable_type_error(value, out_error);
            return TINYPY_FALSE;
        }
        if (__tinypy_object_key_text(key, &name, &name_size) != 0 && (TINYPY_NAME_EQ(key, vm->internal_special_name_key) != 0 || TINYPY_NAME_EQ(key, vm->internal_special_module_key) != 0 || __tinypy_object_type_metadata_read_only(key) != 0)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type metadata cannot be deleted", out_error);
            return TINYPY_FALSE;
        }
        dict = type->dict;
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_MODULE) {
        dict = tinypy_module_dict(value);
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FUNCTION) {
        dict = TINYPY_FUNCTION_OBJECT(value)->dict;
    }
    if (dict == NULL) {
        tinypy_internal_object_make_attribute_error_key(value, key, out_error);
        return TINYPY_FALSE;
    }
    if (tinypy_internal_dict_delete_optional(vm, dict, key) == 0) {
        tinypy_internal_object_make_attribute_error_key(value, key, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_object_delete_attr_protocol_key(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *encoded_key = tinypy_internal_object_encode_attribute_name(key, out_error);

        if (encoded_key == NULL) {
            return TINYPY_FALSE;
        }
        tinypy_bool_t deleted = tinypy_internal_object_delete_attr_protocol_key(value, encoded_key, out_error);
        TINYPY_DECREF(encoded_key);
        return deleted;
    }
    if ((value->type->flags & TINYPY_TYPE_FLAG_NEEDS_ATTRIBUTE_READY) != 0U) {
        __tinypy_object_cold_attribute_error(value, key, TINYPY_TRUE, out_error);
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_bool_t return_value_0 = __tinypy_object_old_instance_delete_attr(value, key, out_error);
        return return_value_0;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    if (tinypy_internal_object_has_special_override_key(value, vm->internal_special_delattr_key) != 0) {
        tinypy_value_t *attribute = tinypy_type_get_attr_key(value->type, vm->internal_special_delattr_key);
        tinypy_value_t *method = tinypy_internal_descriptor_get_value(vm, attribute, value, value->type, out_error);
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) {
            return TINYPY_FALSE;
        }
        args = tinypy_tuple_from_items(vm, &key, 1U);
        result = tinypy_call(method, args, NULL, out_error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    tinypy_bool_t return_value_1 = tinypy_internal_object_delete_attr_key(value, key, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_object_delete_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_bool_t result;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
    result = tinypy_internal_object_delete_attr_protocol_key(value, key, out_error);
    TINYPY_DECREF(key);
    return result;
}
