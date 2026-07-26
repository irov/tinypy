#include "tinypy/code.h"
#include "tinypy/compiler.h"

#include "internal.h"

#include "assertion.h"

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_code_all_name_chars(const tinypy_value_t *value) {
    size_t size;
    const uint8_t *bytes = (const uint8_t *)tinypy_string_view(value, &size);
    size_t index;

    for (index = 0U; index < size; ++index) {
        uint8_t byte = bytes[index];

        if ((byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') || byte == '_' || (byte >= 'a' && byte <= 'z')) {
            continue;
        }
        return INT32_C(0);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_code_intern_constants(tinypy_value_t *value) {
    tinypy_value_type_e type = TINYPY_VALUE_KIND(value);

    if (type == TINYPY_VALUE_STRING) {
        if (__tinypy_internal_code_all_name_chars(value) != 0) {
            tinypy_internal_string_set_interned(value, 1);
        }
        return;
    }
    if (type == TINYPY_VALUE_TUPLE) {
        tinypy_value_t *const *iterator = TINYPY_TUPLE_ITERATOR_BEGIN(value);
        tinypy_value_t *const *iterator_end = TINYPY_TUPLE_ITERATOR_END(value);

        for (; iterator != iterator_end; ++iterator) {
            tinypy_value_t *item = *iterator;
            __tinypy_internal_code_intern_constants(item);
        }
        return;
    }
    if (type == TINYPY_VALUE_FROZENSET) {
        tinypy_value_t *dict = TINYPY_SET_OBJECT(value)->dict;
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(dict);
        tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(dict);

        for (; iterator != iterator_end; ++iterator) {
            if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE) {
                __tinypy_internal_code_intern_constants(iterator->key);
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_code_intern_identifiers(tinypy_value_t *tuple) {
    tinypy_value_t *const *iterator = TINYPY_TUPLE_ITERATOR_BEGIN(tuple);
    tinypy_value_t *const *iterator_end = TINYPY_TUPLE_ITERATOR_END(tuple);

    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *value = *iterator;

        TINYPY_ASSERT(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING);
        tinypy_internal_string_set_interned(value, 1);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_new(int32_t arg_count, int32_t local_count, int32_t stack_size, int32_t flags, tinypy_value_t *bytecode, tinypy_value_t *consts, tinypy_value_t *names, tinypy_value_t *varnames, tinypy_value_t *freevars, tinypy_value_t *cellvars, tinypy_value_t *filename, tinypy_value_t *name, int32_t first_line_number, tinypy_value_t *lnotab) {
    TINYPY_ASSERT(bytecode != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(bytecode);
    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    TINYPY_ASSERT(arg_count >= 0);
    TINYPY_ASSERT(local_count >= 0);
    TINYPY_ASSERT(stack_size >= 0);
    TINYPY_ASSERT(first_line_number >= 0);
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, bytecode) && TINYPY_VALUE_KIND(bytecode) == TINYPY_VALUE_STRING);
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, consts) && TINYPY_VALUE_KIND(consts) == TINYPY_VALUE_TUPLE);
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, names) && TINYPY_VALUE_KIND(names) == TINYPY_VALUE_TUPLE);
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, varnames) && TINYPY_VALUE_KIND(varnames) == TINYPY_VALUE_TUPLE);
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, freevars) && TINYPY_VALUE_KIND(freevars) == TINYPY_VALUE_TUPLE);
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, cellvars) && TINYPY_VALUE_KIND(cellvars) == TINYPY_VALUE_TUPLE);
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, filename) && TINYPY_VALUE_KIND(filename) == TINYPY_VALUE_STRING);
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, name) && TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING);
    TINYPY_ASSERT(tinypy_internal_value_belongs_to(vm, lnotab) && TINYPY_VALUE_KIND(lnotab) == TINYPY_VALUE_STRING);

    __tinypy_internal_code_intern_identifiers(names);
    __tinypy_internal_code_intern_identifiers(varnames);
    __tinypy_internal_code_intern_identifiers(freevars);
    __tinypy_internal_code_intern_identifiers(cellvars);
    __tinypy_internal_code_intern_constants(consts);

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
void tinypy_internal_code_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(value);
    size_t index;

    visit(code->bytecode, user_data);
    visit(code->consts, user_data);
    visit(code->names, user_data);
    visit(code->varnames, user_data);
    visit(code->freevars, user_data);
    visit(code->cellvars, user_data);
    visit(code->filename, user_data);
    visit(code->name, user_data);
    visit(code->lnotab, user_data);
    for (index = 0U; index < TINYPY_ATTRIBUTE_LOOKUP_CACHE_SIZE; ++index) {
        if (code->attribute_cache[index].dict_key != NULL) {
            visit(code->attribute_cache[index].dict_key, user_data);
        }
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

    TINYPY_ASSERT(code_value != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code_value)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code_value) == TINYPY_VALUE_CODE);
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
    TINYPY_ASSERT(code != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(code);
    tinypy_compile_environment_t *environment = tinypy_internal_compile_environment_create(vm, feature_flags, optimize_level, profile);
    tinypy_internal_code_attach_compile_environment(code, environment);
    tinypy_internal_compile_environment_release(environment);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_compile_options_inherit_frame(tinypy_vm_t *vm, tinypy_compile_options_t *options) {
    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    TINYPY_ASSERT(options != NULL);
    if (vm->current_frame == NULL) {
        return 0;
    }
    tinypy_compile_environment_t *environment = TINYPY_CODE_OBJECT(vm->current_frame->code)->compile_environment;
    if (environment == NULL) {
        return 0;
    }
    options->feature_flags = tinypy_internal_compile_environment_feature_flags(environment);
    options->optimize_level = tinypy_internal_compile_environment_optimize_level(environment);
    options->build_profile = tinypy_internal_compile_environment_build_profile(environment);
    return 1;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_arg_count(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->arg_count;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_local_count(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->local_count;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_stack_size(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->stack_size;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_flags(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->flags;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_bytecode(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->bytecode;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_consts(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->consts;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_names(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->names;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_varnames(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->varnames;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_freevars(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->freevars;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_cellvars(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->cellvars;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_filename(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->filename;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_name(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->name;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_first_line_number(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->first_line_number;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_lnotab(const tinypy_value_t *code) {
    TINYPY_ASSERT(code != NULL);
    TINYPY_ASSERT(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    TINYPY_ASSERT(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    return TINYPY_CODE_OBJECT((tinypy_value_t *)code)->lnotab;
}
