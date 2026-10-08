#include "tinypy/code.h"
#include "tinypy/compiler.h"

#include "bytecode_verify.h"
#include "internal.h"

#include <limits.h>
#include <math.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_code_all_name_chars(const tinypy_value_t *value) {
    size_t size;
    const uint8_t *bytes = (const uint8_t *)tinypy_string_view(value, &size);
    size_t index;

    for (index = 0U; index < size; ++index) {
        uint8_t byte = bytes[index];

        if ((byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') || byte == '_' || (byte >= 'a' && byte <= 'z')) {
            continue;
        }
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_code_intern_constants(tinypy_value_t **owned_value) {
    tinypy_value_t *value = *owned_value;
    tinypy_value_type_e type = TINYPY_VALUE_KIND(value);

    if (type == TINYPY_VALUE_STRING) {
        if (__tinypy_internal_code_all_name_chars(value) != 0) {
            (void)tinypy_internal_string_intern(owned_value, NULL);
        }
        return;
    }
    if (type == TINYPY_VALUE_TUPLE) {
        tinypy_value_t **iterator = TINYPY_TUPLE_ITERATOR_BEGIN(value);
        tinypy_value_t **iterator_end = TINYPY_TUPLE_ITERATOR_END(value);

        for (; iterator != iterator_end; ++iterator) {
            __tinypy_internal_code_intern_constants(iterator);
        }
        return;
    }
    if (type == TINYPY_VALUE_FROZENSET) {
        tinypy_value_t *dict = TINYPY_SET_OBJECT(value)->dict;
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(dict);
        tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(dict);

        for (; iterator != iterator_end; ++iterator) {
            if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
                __tinypy_internal_code_intern_constants(&iterator->key);
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_code_intern_identifiers(tinypy_value_t *tuple) {
    tinypy_value_t **iterator = TINYPY_TUPLE_ITERATOR_BEGIN(tuple);
    tinypy_value_t **iterator_end = TINYPY_TUPLE_ITERATOR_END(tuple);

    for (; iterator != iterator_end; ++iterator) {
        (void)tinypy_internal_string_intern(iterator, NULL);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_new(int32_t arg_count, int32_t local_count, int32_t stack_size, int32_t flags, tinypy_value_t *bytecode, tinypy_value_t *consts, tinypy_value_t *names, tinypy_value_t *varnames, tinypy_value_t *freevars, tinypy_value_t *cellvars, tinypy_value_t *filename, tinypy_value_t *name, int32_t first_line_number, tinypy_value_t *lnotab) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(bytecode);

    __tinypy_internal_code_intern_identifiers(names);
    __tinypy_internal_code_intern_identifiers(varnames);
    __tinypy_internal_code_intern_identifiers(freevars);
    __tinypy_internal_code_intern_identifiers(cellvars);
    __tinypy_internal_code_intern_constants(&consts);

    tinypy_code_object_t *code = (tinypy_code_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_CODE, sizeof(*code));
    code->arg_count = arg_count;
    code->local_count = local_count;
    code->stack_size = stack_size;
    code->flags = flags;
    code->bytecode = bytecode;
    code->consts = consts;
    code->names = names;
    code->varnames = varnames;
    code->freevars = freevars;
    code->cellvars = cellvars;
    code->filename = filename;
    code->name = name;
    code->first_line_number = first_line_number;
    code->lnotab = lnotab;
    code->parameter_indices = NULL;
    code->compile_environment = NULL;

    TINYPY_INCREF(bytecode);
    TINYPY_INCREF(consts);
    TINYPY_INCREF(names);
    TINYPY_INCREF(varnames);
    TINYPY_INCREF(freevars);
    TINYPY_INCREF(cellvars);
    TINYPY_INCREF(filename);
    TINYPY_INCREF(name);
    TINYPY_INCREF(lnotab);

    return &code->base;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_code_caches_size(size_t slot_count) {
    size_t size = slot_count * (sizeof(tinypy_global_cache_entry_t) + TINYPY_ATTRIBUTE_LOOKUP_CACHE_WAYS * sizeof(tinypy_attribute_lookup_cache_entry_t) + sizeof(tinypy_attribute_store_cache_entry_t));
    return size;
}
//////////////////////////////////////////////////////////////////////////
/* Verified bytecode with method calls is evaluated from a copy marking them. */
static tinypy_bool_t __tinypy_internal_code_find_method_calls(tinypy_code_object_t *code, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(&code->base);
    const uint8_t *bytecode = TINYPY_STRING_OBJECT(code->bytecode)->bytes;
    size_t bytecode_size = TINYPY_SIZED_SIZE(code->bytecode);
    size_t load_limit = bytecode_size / 3U;
    size_t pending_capacity = (size_t)code->stack_size < load_limit ? (size_t)code->stack_size : load_limit;
    size_t scratch_size = pending_capacity * sizeof(tinypy_bytecode_method_load_t) + bytecode_size * sizeof(tinypy_bytecode_method_target_t);
    uint8_t *scratch = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, scratch_size, out_error);

    if (scratch == NULL) {
        return TINYPY_FALSE;
    }
    uint8_t *method_bytecode = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, bytecode_size, out_error);
    if (method_bytecode == NULL) {
        tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
        return TINYPY_FALSE;
    }
    tinypy_bytecode_method_load_t *pending = (tinypy_bytecode_method_load_t *)scratch;
    tinypy_bytecode_method_target_t *targets = (tinypy_bytecode_method_target_t *)(scratch + pending_capacity * sizeof(tinypy_bytecode_method_load_t));
    size_t slots = tinypy_bytecode_find_method_calls(bytecode, bytecode_size, method_bytecode, targets, pending, pending_capacity);

    tinypy_internal_vm_deallocate(vm, scratch, scratch_size);
    if (slots == 0U) {
        tinypy_internal_vm_deallocate(vm, method_bytecode, bytecode_size);
        return TINYPY_TRUE;
    }
    code->method_bytecode = method_bytecode;
    code->method_bytecode_size = bytecode_size;
    code->method_call_slots = slots;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* The caches share one zeroed allocation. */
tinypy_bool_t tinypy_internal_code_allocate_caches(tinypy_code_object_t *code, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(&code->base);
    size_t name_count = TINYPY_TUPLE_SIZE(code->names);
    size_t slot_count = 1U;

    while (slot_count < name_count && slot_count < TINYPY_CODE_CACHE_SLOTS_MAX) {
        slot_count *= 2U;
    }
    size_t size = __tinypy_internal_code_caches_size(slot_count);
    uint8_t *caches = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, size, out_error);
    if (caches == NULL) {
        return TINYPY_FALSE;
    }
    (void)memset(caches, 0, size);
    code->cache_slot_count = slot_count;
    code->global_cache = (tinypy_global_cache_entry_t *)caches;
    code->attribute_cache = (tinypy_attribute_lookup_cache_entry_t *)(code->global_cache + slot_count);
    code->attribute_store_cache = (tinypy_attribute_store_cache_entry_t *)(code->attribute_cache + slot_count * TINYPY_ATTRIBUTE_LOOKUP_CACHE_WAYS);
    tinypy_bool_t found = __tinypy_internal_code_find_method_calls(code, out_error);
    return found;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_code_destroy(tinypy_value_t *value) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    if (code->cached_frame != NULL) {
        tinypy_internal_value_destroy(code->cached_frame);
        TINYPY_DECREF(&vm->types[TINYPY_VALUE_FRAME].base.base);
        code->cached_frame = NULL;
    }
    if (code->global_cache != NULL) {
        size_t caches_size = __tinypy_internal_code_caches_size(code->cache_slot_count);

        tinypy_internal_vm_deallocate(vm, code->global_cache, caches_size);
        code->global_cache = NULL;
        code->attribute_cache = NULL;
        code->attribute_store_cache = NULL;
    }
    if (code->method_bytecode != NULL) {
        tinypy_internal_vm_deallocate(vm, code->method_bytecode, code->method_bytecode_size);
        code->method_bytecode = NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_code_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);

    visit(code->bytecode, user_data);
    visit(code->consts, user_data);
    visit(code->names, user_data);
    visit(code->varnames, user_data);
    visit(code->freevars, user_data);
    visit(code->cellvars, user_data);
    visit(code->filename, user_data);
    visit(code->name, user_data);
    visit(code->lnotab, user_data);
    if (code->parameter_indices != NULL) {
        visit(code->parameter_indices, user_data);
    }
    if (code->compile_environment != NULL) {
        tinypy_internal_compile_environment_release(code->compile_environment);
        code->compile_environment = NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_code_attach_compile_environment(tinypy_value_t *code_value, tinypy_compile_environment_t *environment) {
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(code_value);
    if (code->compile_environment != environment) {
        if (environment != NULL) {
            tinypy_internal_compile_environment_retain(environment);
        }
        if (code->compile_environment != NULL) {
            tinypy_internal_compile_environment_release(code->compile_environment);
        }
        code->compile_environment = environment;
    }
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(code->consts);
    iterator_end = TINYPY_TUPLE_ITERATOR_END(code->consts);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *constant = *iterator;

        if (TINYPY_VALUE_KIND(constant) == TINYPY_VALUE_CODE) {
            tinypy_internal_code_attach_compile_environment(constant, environment);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_code_attach_compile_options(tinypy_value_t *code, uint32_t feature_flags, int32_t optimize_level, const tinypy_build_profile_t *profile) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(code);
    tinypy_compile_environment_t *environment = tinypy_internal_compile_environment_create(vm, feature_flags, optimize_level, profile);
    tinypy_internal_code_attach_compile_environment(code, environment);
    tinypy_internal_compile_environment_release(environment);
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_compile_options_inherit_frame(tinypy_vm_t *vm, tinypy_compile_options_t *options) {
    if (vm->current_frame == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_compile_environment_t *environment = TINYPY_CODE_OBJECT(vm->current_frame->code)->compile_environment;
    if (environment == NULL) {
        return TINYPY_FALSE;
    }
    options->feature_flags = tinypy_internal_compile_environment_feature_flags(environment);
    options->optimize_level = tinypy_internal_compile_environment_optimize_level(environment);
    options->build_profile = tinypy_internal_compile_environment_build_profile(environment);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_arg_count(const tinypy_value_t *code) {
    int32_t return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->arg_count;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_local_count(const tinypy_value_t *code) {
    int32_t return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->local_count;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_stack_size(const tinypy_value_t *code) {
    int32_t return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->stack_size;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_flags(const tinypy_value_t *code) {
    int32_t return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->flags;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_bytecode(const tinypy_value_t *code) {
    tinypy_value_t *return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->bytecode;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_consts(const tinypy_value_t *code) {
    tinypy_value_t *return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->consts;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_names(const tinypy_value_t *code) {
    tinypy_value_t *return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->names;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_varnames(const tinypy_value_t *code) {
    tinypy_value_t *return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->varnames;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_freevars(const tinypy_value_t *code) {
    tinypy_value_t *return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->freevars;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_cellvars(const tinypy_value_t *code) {
    tinypy_value_t *return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->cellvars;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_filename(const tinypy_value_t *code) {
    tinypy_value_t *return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->filename;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_name(const tinypy_value_t *code) {
    tinypy_value_t *return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->name;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_first_line_number(const tinypy_value_t *code) {
    int32_t return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->first_line_number;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_lnotab(const tinypy_value_t *code) {
    tinypy_value_t *return_value_1 = TINYPY_CODE_OBJECT((tinypy_value_t *)code)->lnotab;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_code_integer(tinypy_vm_t *vm, tinypy_value_t *value, int32_t *out_value, tinypy_error_t **out_error) {
    int64_t integer;

    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_INTEGER) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "code integer field is not an integer", out_error);
        return TINYPY_FALSE;
    }
    integer = TINYPY_INTEGER_VALUE(value);
    if (integer < INT32_MIN || integer > INT32_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "code integer field is out of range", out_error);
        return TINYPY_FALSE;
    }
    *out_value = (int32_t)integer;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* validate_and_copy_tuple: identifiers are copied into exact str objects so
   later name lookups never run str subclass methods, and interning them
   leaves the caller's tuple untouched. */
static tinypy_value_t *__tinypy_code_copy_identifiers(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_TUPLE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "code free variables must be tuples of strings", out_error);
        return NULL;
    }
    tinypy_value_t *const *items = tinypy_internal_tuple_items(value);
    size_t size = TINYPY_TUPLE_SIZE(value);
    for (size_t index = 0U; index < size; ++index) {
        if (TINYPY_VALUE_KIND(items[index]) != TINYPY_VALUE_STRING) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("name tuples must contain only strings, not '"),
                TINYPY_MESSAGE_PART_TYPE_NAME(items[index]),
                TINYPY_MESSAGE_PART_LITERAL("'"),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            return NULL;
        }
    }
    tinypy_value_t *copy = tinypy_tuple_from_items(vm, items, size);
    tinypy_value_t **copy_items = TINYPY_TUPLE_ITEMS(copy);
    for (size_t index = 0U; index < size; ++index) {
        tinypy_value_t *item = copy_items[index];

        if (item->type != &vm->types[TINYPY_VALUE_STRING]) {
            copy_items[index] = tinypy_string_from_bytes(vm, TINYPY_TEXT_BYTES(item), TINYPY_TEXT_BYTE_SIZE(item));
            TINYPY_DECREF(item);
        }
    }
    return copy;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_code_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);
    int32_t integers[5];
    size_t index;

    if (type != &vm->types[TINYPY_VALUE_CODE] || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < 12U || count > 14U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "code() requires 12 to 14 positional arguments", out_error);
        return NULL;
    }
    for (index = 0U; index < 4U; ++index) {
        if (__tinypy_code_integer(vm, TINYPY_TUPLE_GET(args, index), &integers[index], out_error) == 0) {
            return NULL;
        }
    }
    if (__tinypy_code_integer(vm, TINYPY_TUPLE_GET(args, 10U), &integers[4], out_error) == 0) {
        return NULL;
    }
    if (integers[0] < 0 || integers[1] < 0 || integers[2] < 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "code() argcount, nlocals and stacksize must not be negative", out_error);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 4U)) != TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 5U)) != TINYPY_VALUE_TUPLE || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 6U)) != TINYPY_VALUE_TUPLE || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 7U)) != TINYPY_VALUE_TUPLE || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 8U)) != TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 9U)) != TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 11U)) != TINYPY_VALUE_STRING) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "code() received an invalid string or tuple field", out_error);
        return NULL;
    }
    tinypy_value_t *empty = &vm->empty_tuple_object.base.base;
    tinypy_value_t *const sources[] = {TINYPY_TUPLE_GET(args, 6U), TINYPY_TUPLE_GET(args, 7U), count >= 13U ? TINYPY_TUPLE_GET(args, 12U) : empty, count >= 14U ? TINYPY_TUPLE_GET(args, 13U) : empty};
    tinypy_value_t *identifiers[] = {NULL, NULL, NULL, NULL};
    tinypy_value_t *result = NULL;

    for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        identifiers[index] = __tinypy_code_copy_identifiers(vm, sources[index], out_error);
        if (identifiers[index] == NULL) {
            goto cleanup;
        }
    }
    if ((size_t)integers[1] != TINYPY_TUPLE_SIZE(identifiers[1])) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "code() nlocals must match the number of variable names", out_error);
        goto cleanup;
    }
    size_t required_locals = (size_t)integers[0] + ((integers[3] & TINYPY_CODE_VARARGS) != 0 ? 1U : 0U) + ((integers[3] & TINYPY_CODE_VAR_KEYWORDS) != 0 ? 1U : 0U);
    if (required_locals > (size_t)integers[1]) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "code() argcount exceeds nlocals", out_error);
        goto cleanup;
    }
    result = tinypy_code_new(integers[0], integers[1], integers[2], integers[3], TINYPY_TUPLE_GET(args, 4U), TINYPY_TUPLE_GET(args, 5U), identifiers[0], identifiers[1], identifiers[2], identifiers[3], TINYPY_TUPLE_GET(args, 8U), TINYPY_TUPLE_GET(args, 9U), integers[4], TINYPY_TUPLE_GET(args, 11U));
cleanup:
    for (index = 0U; index < sizeof(identifiers) / sizeof(identifiers[0]); ++index) {
        if (identifiers[index] != NULL) {
            TINYPY_DECREF(identifiers[index]);
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_code_value_order(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_error_t **out_error) {
    int32_t equal = tinypy_compare_bool(left, right, TINYPY_COMPARE_EQUAL, out_error);

    if (equal < 0) {
        return TINYPY_FALSE;
    }
    if (equal != 0) {
        *out_order = INT32_C(0);
        return TINYPY_TRUE;
    }
    int32_t less = tinypy_compare_bool(left, right, TINYPY_COMPARE_LESS, out_error);
    if (less < 0) {
        return TINYPY_FALSE;
    }
    *out_order = less != 0 ? -INT32_C(1) : INT32_C(1);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_code_equal(tinypy_value_t *left_value, tinypy_value_t *right_value, tinypy_bool_t *out_equal, tinypy_error_t **out_error);

static tinypy_bool_t __tinypy_code_constant_equal(tinypy_value_t *left, tinypy_value_t *right, tinypy_bool_t *out_equal, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;

    if (left == right) {
        *out_equal = TINYPY_TRUE;
        return TINYPY_TRUE;
    }
    if (left->type != right->type) {
        *out_equal = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    kind = TINYPY_VALUE_KIND(left);
    if (kind == TINYPY_VALUE_TUPLE) {
        size_t size = TINYPY_TUPLE_SIZE(left);
        size_t index;

        if (size != TINYPY_TUPLE_SIZE(right)) {
            *out_equal = TINYPY_FALSE;
            return TINYPY_TRUE;
        }
        for (index = 0U; index < size; ++index) {
            tinypy_bool_t equal;

            if (__tinypy_code_constant_equal(TINYPY_TUPLE_GET(left, index), TINYPY_TUPLE_GET(right, index), &equal, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (equal == 0) {
                *out_equal = TINYPY_FALSE;
                return TINYPY_TRUE;
            }
        }
        *out_equal = TINYPY_TRUE;
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_FROZENSET) {
        tinypy_value_t *left_dict = TINYPY_SET_OBJECT(left)->dict;
        tinypy_value_t *right_dict = TINYPY_SET_OBJECT(right)->dict;
        tinypy_dict_entry_t *left_iterator;
        tinypy_dict_entry_t *left_end;

        if (TINYPY_DICT_SIZE(left_dict) != TINYPY_DICT_SIZE(right_dict)) {
            *out_equal = TINYPY_FALSE;
            return TINYPY_TRUE;
        }
        left_iterator = TINYPY_DICT_ITERATOR_BEGIN(left_dict);
        left_end = TINYPY_DICT_ITERATOR_END(left_dict);
        for (; left_iterator != left_end; ++left_iterator) {
            tinypy_dict_entry_t *right_iterator;
            tinypy_dict_entry_t *right_end;
            tinypy_bool_t found = TINYPY_FALSE;

            if (TINYPY_DICT_ENTRY_IS_ACTIVE(left_iterator) == 0) {
                continue;
            }
            right_iterator = TINYPY_DICT_ITERATOR_BEGIN(right_dict);
            right_end = TINYPY_DICT_ITERATOR_END(right_dict);
            for (; right_iterator != right_end; ++right_iterator) {
                tinypy_bool_t equal;

                if (TINYPY_DICT_ENTRY_IS_ACTIVE(right_iterator) == 0) {
                    continue;
                }
                if (__tinypy_code_constant_equal(left_iterator->key, right_iterator->key, &equal, out_error) == 0) {
                    return TINYPY_FALSE;
                }
                if (equal != 0) {
                    found = TINYPY_TRUE;
                    break;
                }
            }
            if (found == 0) {
                *out_equal = TINYPY_FALSE;
                return TINYPY_TRUE;
            }
        }
        *out_equal = TINYPY_TRUE;
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        double left_number = TINYPY_FLOAT_OBJECT(left)->value;
        double right_number = TINYPY_FLOAT_OBJECT(right)->value;

        *out_equal = left_number == right_number && (left_number != 0.0 || signbit(left_number) == signbit(right_number));
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_COMPLEX) {
        double left_real = TINYPY_COMPLEX_OBJECT(left)->real;
        double right_real = TINYPY_COMPLEX_OBJECT(right)->real;
        double left_imaginary = TINYPY_COMPLEX_OBJECT(left)->imaginary;
        double right_imaginary = TINYPY_COMPLEX_OBJECT(right)->imaginary;
        tinypy_bool_t real_equal = left_real == right_real && (left_real != 0.0 || signbit(left_real) == signbit(right_real));
        tinypy_bool_t imaginary_equal = left_imaginary == right_imaginary && (left_imaginary != 0.0 || signbit(left_imaginary) == signbit(right_imaginary));

        *out_equal = real_equal != 0 && imaginary_equal != 0;
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_CODE) {
        tinypy_bool_t return_value_1 = __tinypy_code_equal(left, right, out_equal, out_error);
        return return_value_1;
    }
    int32_t equal = tinypy_compare_bool(left, right, TINYPY_COMPARE_EQUAL, out_error);

    if (equal < 0) {
        return TINYPY_FALSE;
    }
    *out_equal = equal != 0;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_code_equal(tinypy_value_t *left_value, tinypy_value_t *right_value, tinypy_bool_t *out_equal, tinypy_error_t **out_error) {
    tinypy_code_object_t *left = TINYPY_CODE_OBJECT(left_value);
    tinypy_code_object_t *right = TINYPY_CODE_OBJECT(right_value);
    tinypy_value_t *left_values[] = {left->name, left->bytecode, left->names, left->varnames, left->freevars, left->cellvars};
    tinypy_value_t *right_values[] = {right->name, right->bytecode, right->names, right->varnames, right->freevars, right->cellvars};
    size_t index;

    if (left->arg_count != right->arg_count || left->local_count != right->local_count || left->flags != right->flags || left->first_line_number != right->first_line_number) {
        *out_equal = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    for (index = 0U; index < sizeof(left_values) / sizeof(left_values[0]); ++index) {
        int32_t equal = tinypy_compare_bool(left_values[index], right_values[index], TINYPY_COMPARE_EQUAL, out_error);

        if (equal < 0) {
            return TINYPY_FALSE;
        }
        if (equal == 0) {
            *out_equal = TINYPY_FALSE;
            return TINYPY_TRUE;
        }
    }
    tinypy_bool_t return_value_1 = __tinypy_code_constant_equal(left->consts, right->consts, out_equal, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_code_order(tinypy_value_t *left_value, tinypy_value_t *right_value, int32_t *out_order, tinypy_error_t **out_error) {
    tinypy_code_object_t *left = TINYPY_CODE_OBJECT(left_value);
    tinypy_code_object_t *right = TINYPY_CODE_OBJECT(right_value);
    const int32_t left_integers[] = {left->arg_count, left->local_count, left->flags, left->first_line_number};
    const int32_t right_integers[] = {right->arg_count, right->local_count, right->flags, right->first_line_number};
    tinypy_value_t *left_values[] = {left->bytecode, left->consts, left->names, left->varnames, left->freevars, left->cellvars};
    tinypy_value_t *right_values[] = {right->bytecode, right->consts, right->names, right->varnames, right->freevars, right->cellvars};
    size_t index;

    if (__tinypy_code_value_order(left->name, right->name, out_order, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (*out_order != 0) {
        return TINYPY_TRUE;
    }
    for (index = 0U; index < sizeof(left_integers) / sizeof(left_integers[0]); ++index) {
        if (left_integers[index] != right_integers[index]) {
            *out_order = left_integers[index] < right_integers[index] ? -INT32_C(1) : INT32_C(1);
            return TINYPY_TRUE;
        }
    }
    for (index = 0U; index < sizeof(left_values) / sizeof(left_values[0]); ++index) {
        if (__tinypy_code_value_order(left_values[index], right_values[index], out_order, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (*out_order != 0) {
            return TINYPY_TRUE;
        }
    }
    *out_order = INT32_C(0);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_code_compare(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    int32_t order;
    tinypy_bool_t result;

    if (TINYPY_VALUE_KIND(right) != TINYPY_VALUE_CODE) {
        tinypy_value_t *return_value_1 = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return return_value_1;
    }
    if (operation == TINYPY_COMPARE_EQUAL || operation == TINYPY_COMPARE_NOT_EQUAL) {
        if (__tinypy_code_equal(left, right, &result, out_error) == 0) {
            return NULL;
        }
        if (operation == TINYPY_COMPARE_NOT_EQUAL) {
            result = result == 0;
        }
        tinypy_value_t *return_value_2 = tinypy_bool_from_i32(vm, result);
        return return_value_2;
    }
    if (__tinypy_code_order(left, right, &order, out_error) == 0) {
        return NULL;
    }
    switch (operation) {
    case TINYPY_COMPARE_LESS:
        result = order < 0;
        break;
    case TINYPY_COMPARE_LESS_EQUAL:
        result = order <= 0;
        break;
    case TINYPY_COMPARE_GREATER:
        result = order > 0;
        break;
    case TINYPY_COMPARE_GREATER_EQUAL:
        result = order >= 0;
        break;
    default:
        return TINYPY_RET_NOT_IMPLEMENTED(vm);
    }
    tinypy_value_t *return_value_4 = tinypy_bool_from_i32(vm, result);
    return return_value_4;
}
//////////////////////////////////////////////////////////////////////////
tinypy_hash_t tinypy_internal_code_hash(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *fields[] = {code->name, code->bytecode, code->consts, code->names, code->varnames, code->freevars, code->cellvars};
    tinypy_hash_t hash = (tinypy_hash_t)code->arg_count ^ (tinypy_hash_t)code->local_count ^ (tinypy_hash_t)code->flags;
    size_t index;

    for (index = 0U; index < sizeof(fields) / sizeof(fields[0]); ++index) {
        tinypy_value_t *previous_raised = vm->raised_value;
        tinypy_hash_t field_hash = tinypy_internal_hash_value(fields[index], out_error);

        if ((out_error != NULL && *out_error != NULL) || vm->raised_value != previous_raised) {
            return (tinypy_hash_t)0;
        }
        hash ^= field_hash;
    }
    if (hash == (tinypy_hash_t)-1) {
        hash = (tinypy_hash_t)-2;
    }
    return hash;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_code_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_CODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "code method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_code_compare_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_code_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_code_compare(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), (int32_t)(intptr_t)user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_code_cmp_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t order;

    (void)user_data;
    if (__tinypy_code_method_arguments(vm, args, kwargs, 2U, out_error) == 0 || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 1U)) != TINYPY_VALUE_CODE) {
        if (out_error == NULL || *out_error == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "code.__cmp__ requires two code objects", out_error);
        }
        return NULL;
    }
    if (__tinypy_code_order(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), &order, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, order);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_code_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_hash_t hash;

    (void)user_data;
    if (__tinypy_code_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    hash = tinypy_internal_code_hash(TINYPY_TUPLE_GET(args, 0U), out_error);
    if (out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)hash);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_code_type(tinypy_vm_t *vm) {
    tinypy_value_t *const names[] = {vm->internal_special_lt_key, vm->internal_special_le_key, vm->internal_special_eq_key, vm->internal_special_ne_key, vm->internal_special_gt_key, vm->internal_special_ge_key};
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_CODE];
    size_t index;

    type->create = tinypy_internal_code_create;
    type->rich_compare = tinypy_internal_code_compare;
    type->hash = tinypy_internal_code_hash;
    tinypy_internal_constructor_add_builtin_new(type);
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        tinypy_internal_type_add_method(type, names[index], __tinypy_code_compare_method, (void *)(intptr_t)index, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    tinypy_internal_type_add_method(type, vm->internal_special_cmp_key, __tinypy_code_cmp_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_value_t *hash = tinypy_native_function_new_key(vm->internal_special_hash_key, __tinypy_code_hash_method, NULL, NULL);

    tinypy_type_set_attr_key(type, type->vm->internal_special_hash_key, hash);
    TINYPY_DECREF(hash);
}
