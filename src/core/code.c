#include "tinypy/code.h"
#include "tinypy/compiler.h"

#include "internal.h"

#include <assert.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_code_object_t *__tinypy_internal_code_validate(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_CODE);

    return TINYPY_CODE_OBJECT((tinypy_value_t *)value);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_code_all_name_chars(const tinypy_value_t *value) {
    size_t size;
    const unsigned char *bytes = (const unsigned char *)tinypy_string_view(value, &size);
    size_t index;

    for (index = 0U; index < size; index += 1U) {
        unsigned char byte = bytes[index];

        if ((byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') || byte == '_' || (byte >= 'a' && byte <= 'z')) {
            continue;
        }
        return INT32_C(0);
    }
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_code_intern_constants(tinypy_value_t *value) {
    tinypy_value_type_e type = tinypy_internal_value_kind(value);
    size_t index;

    if (type == TINYPY_VALUE_STRING) {
        if (__tinypy_internal_code_all_name_chars(value) != 0) {
            tinypy_internal_string_set_interned(value, 1);
        }
        return;
    }
    if (type == TINYPY_VALUE_TUPLE) {
        for (index = 0U; index < tinypy_tuple_size(value); index += 1U) {
            tinypy_value_t *item = tinypy_tuple_get(value, index);
            __tinypy_internal_code_intern_constants(item);
        }
        return;
    }
    if (type == TINYPY_VALUE_FROZENSET) {
        tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(TINYPY_SET_OBJECT(value)->dict);

        for (index = 0U; index <= dict->mask; index += 1U) {
            if (dict->table[index].state == TINYPY_DICT_ENTRY_ACTIVE) {
                __tinypy_internal_code_intern_constants(dict->table[index].key);
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_code_intern_identifiers(tinypy_value_t *tuple) {
    size_t index;

    for (index = 0U; index < tinypy_tuple_size(tuple); index += 1U) {
        tinypy_value_t *value = tinypy_tuple_get(tuple, index);

        assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING);
        tinypy_internal_string_set_interned(value, 1);
    }
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_new(int32_t arg_count, int32_t local_count, int32_t stack_size, int32_t flags, tinypy_value_t *bytecode, tinypy_value_t *consts, tinypy_value_t *names, tinypy_value_t *varnames, tinypy_value_t *freevars, tinypy_value_t *cellvars, tinypy_value_t *filename, tinypy_value_t *name, int32_t first_line_number, tinypy_value_t *lnotab) {
    tinypy_vm_t *vm;
    tinypy_code_object_t *code;

    assert(bytecode != NULL);
    vm = tinypy_internal_value_vm(bytecode);
    assert(tinypy_internal_vm_valid(vm));
    assert(arg_count >= 0);
    assert(local_count >= 0);
    assert(stack_size >= 0);
    assert(first_line_number >= 0);
    assert(tinypy_internal_value_belongs_to(vm, bytecode) && tinypy_internal_value_kind(bytecode) == TINYPY_VALUE_STRING);
    assert(tinypy_internal_value_belongs_to(vm, consts) && tinypy_internal_value_kind(consts) == TINYPY_VALUE_TUPLE);
    assert(tinypy_internal_value_belongs_to(vm, names) && tinypy_internal_value_kind(names) == TINYPY_VALUE_TUPLE);
    assert(tinypy_internal_value_belongs_to(vm, varnames) && tinypy_internal_value_kind(varnames) == TINYPY_VALUE_TUPLE);
    assert(tinypy_internal_value_belongs_to(vm, freevars) && tinypy_internal_value_kind(freevars) == TINYPY_VALUE_TUPLE);
    assert(tinypy_internal_value_belongs_to(vm, cellvars) && tinypy_internal_value_kind(cellvars) == TINYPY_VALUE_TUPLE);
    assert(tinypy_internal_value_belongs_to(vm, filename) && tinypy_internal_value_kind(filename) == TINYPY_VALUE_STRING);
    assert(tinypy_internal_value_belongs_to(vm, name) && tinypy_internal_value_kind(name) == TINYPY_VALUE_STRING);
    assert(tinypy_internal_value_belongs_to(vm, lnotab) && tinypy_internal_value_kind(lnotab) == TINYPY_VALUE_STRING);

    __tinypy_internal_code_intern_identifiers(names);
    __tinypy_internal_code_intern_identifiers(varnames);
    __tinypy_internal_code_intern_identifiers(freevars);
    __tinypy_internal_code_intern_identifiers(cellvars);
    __tinypy_internal_code_intern_constants(consts);

    code = (tinypy_code_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_CODE, sizeof(*code));
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

    tinypy_retain(bytecode);
    tinypy_retain(consts);
    tinypy_retain(names);
    tinypy_retain(varnames);
    tinypy_retain(freevars);
    tinypy_retain(cellvars);
    tinypy_retain(filename);
    tinypy_retain(name);
    tinypy_retain(lnotab);

    return &code->base;
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
    if (code->compile_environment != NULL) {
        tinypy_internal_compile_environment_release(code->compile_environment);
        code->compile_environment = NULL;
    }
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_code_attach_compile_environment(tinypy_value_t *code_value, tinypy_compile_environment_t *environment) {
    tinypy_code_object_t *code = __tinypy_internal_code_validate(code_value);
    size_t index;

    if (code->compile_environment != environment) {
        if (environment != NULL) {
            tinypy_internal_compile_environment_retain(environment);
        }
        if (code->compile_environment != NULL) {
            tinypy_internal_compile_environment_release(code->compile_environment);
        }
        code->compile_environment = environment;
    }
    for (index = 0U; index < tinypy_tuple_size(code->consts); index += 1U) {
        tinypy_value_t *constant = tinypy_tuple_get(code->consts, index);

        if (tinypy_internal_value_kind(constant) == TINYPY_VALUE_CODE) {
            tinypy_internal_code_attach_compile_environment(constant, environment);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_code_attach_compile_options(tinypy_value_t *code, uint32_t feature_flags, int32_t optimize_level, const tinypy_build_profile_t *profile) {
    tinypy_compile_environment_t *environment;

    assert(code != NULL);
    tinypy_vm_t *vm = tinypy_internal_value_vm(code);
    environment = tinypy_internal_compile_environment_create(vm, feature_flags, optimize_level, profile);
    tinypy_internal_code_attach_compile_environment(code, environment);
    tinypy_internal_compile_environment_release(environment);
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_compile_options_inherit_frame(tinypy_vm_t *vm, tinypy_compile_options_t *options) {
    tinypy_compile_environment_t *environment;

    assert(tinypy_internal_vm_valid(vm));
    assert(options != NULL);
    if (vm->current_frame == NULL) {
        return 0;
    }
    environment = TINYPY_CODE_OBJECT(vm->current_frame->code)->compile_environment;
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
    return __tinypy_internal_code_validate(code)->arg_count;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_local_count(const tinypy_value_t *code) {
    return __tinypy_internal_code_validate(code)->local_count;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_stack_size(const tinypy_value_t *code) {
    return __tinypy_internal_code_validate(code)->stack_size;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_flags(const tinypy_value_t *code) {
    return __tinypy_internal_code_validate(code)->flags;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_bytecode(const tinypy_value_t *code) {
    return __tinypy_internal_code_validate(code)->bytecode;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_consts(const tinypy_value_t *code) {
    return __tinypy_internal_code_validate(code)->consts;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_names(const tinypy_value_t *code) {
    return __tinypy_internal_code_validate(code)->names;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_varnames(const tinypy_value_t *code) {
    return __tinypy_internal_code_validate(code)->varnames;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_freevars(const tinypy_value_t *code) {
    return __tinypy_internal_code_validate(code)->freevars;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_cellvars(const tinypy_value_t *code) {
    return __tinypy_internal_code_validate(code)->cellvars;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_filename(const tinypy_value_t *code) {
    return __tinypy_internal_code_validate(code)->filename;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_name(const tinypy_value_t *code) {
    return __tinypy_internal_code_validate(code)->name;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_code_first_line_number(const tinypy_value_t *code) {
    return __tinypy_internal_code_validate(code)->first_line_number;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_lnotab(const tinypy_value_t *code) {
    return __tinypy_internal_code_validate(code)->lnotab;
}
