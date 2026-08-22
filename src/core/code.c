#include "tinypy/code.h"
#include "tinypy/compiler.h"

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
            if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
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

        tinypy_internal_string_set_interned(value, 1);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_code_new(int32_t arg_count, int32_t local_count, int32_t stack_size, int32_t flags, tinypy_value_t *bytecode, tinypy_value_t *consts, tinypy_value_t *names, tinypy_value_t *varnames, tinypy_value_t *freevars, tinypy_value_t *cellvars, tinypy_value_t *filename, tinypy_value_t *name, int32_t first_line_number, tinypy_value_t *lnotab) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(bytecode);

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
    if (code->parameter_indices != NULL) {
        visit(code->parameter_indices, user_data);
    }
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
static tinypy_bool_t __tinypy_code_identifier_tuple(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *const *item = TINYPY_TUPLE_ITERATOR_BEGIN(value);
    tinypy_value_t *const *end = TINYPY_TUPLE_ITERATOR_END(value);

    for (; item != end; ++item) {
        if (TINYPY_VALUE_KIND(*item) != TINYPY_VALUE_STRING) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "code name tuple contains a non-string", out_error);
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_code_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);
    int32_t integers[5];
    tinypy_value_t *empty = NULL;
    tinypy_value_t *freevars;
    tinypy_value_t *cellvars;
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
    if (integers[0] < 0 || integers[1] < 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "code() argcount and nlocals must not be negative", out_error);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 4U)) != TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 5U)) != TINYPY_VALUE_TUPLE || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 6U)) != TINYPY_VALUE_TUPLE || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 7U)) != TINYPY_VALUE_TUPLE || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 8U)) != TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 9U)) != TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 11U)) != TINYPY_VALUE_STRING) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "code() received an invalid string or tuple field", out_error);
        return NULL;
    }
    if (__tinypy_code_identifier_tuple(vm, TINYPY_TUPLE_GET(args, 6U), out_error) == 0 || __tinypy_code_identifier_tuple(vm, TINYPY_TUPLE_GET(args, 7U), out_error) == 0) {
        return NULL;
    }
    if (count < 14U) {
        empty = tinypy_tuple_from_items(vm, NULL, 0U);
    }
    freevars = count >= 13U ? TINYPY_TUPLE_GET(args, 12U) : empty;
    cellvars = count >= 14U ? TINYPY_TUPLE_GET(args, 13U) : empty;
    if (TINYPY_VALUE_KIND(freevars) != TINYPY_VALUE_TUPLE || TINYPY_VALUE_KIND(cellvars) != TINYPY_VALUE_TUPLE || __tinypy_code_identifier_tuple(vm, freevars, out_error) == 0 || __tinypy_code_identifier_tuple(vm, cellvars, out_error) == 0) {
        if (empty != NULL) {
            TINYPY_DECREF(empty);
        }
        if (out_error == NULL || *out_error == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "code free variables must be tuples of strings", out_error);
        }
        return NULL;
    }
    tinypy_value_t *result = tinypy_code_new(integers[0], integers[1], integers[2], integers[3], TINYPY_TUPLE_GET(args, 4U), TINYPY_TUPLE_GET(args, 5U), TINYPY_TUPLE_GET(args, 6U), TINYPY_TUPLE_GET(args, 7U), freevars, cellvars, TINYPY_TUPLE_GET(args, 8U), TINYPY_TUPLE_GET(args, 9U), integers[4], TINYPY_TUPLE_GET(args, 11U));
    if (empty != NULL) {
        TINYPY_DECREF(empty);
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
        tinypy_value_t *return_value_1 = tinypy_not_implemented_get(vm);
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
        return tinypy_not_implemented_get(vm);
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
    static const char *const names[] = {"__lt__", "__le__", "__eq__", "__ne__", "__gt__", "__ge__"};
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_CODE];
    size_t index;

    type->create = tinypy_internal_code_create;
    type->rich_compare = tinypy_internal_code_compare;
    type->hash = tinypy_internal_code_hash;
    tinypy_internal_constructor_add_builtin_new(type);
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        tinypy_value_t *method = tinypy_native_function_new(vm, names[index], 6U, __tinypy_code_compare_method, (void *)(intptr_t)index, NULL);

        tinypy_type_set_attr(type, names[index], 6U, method);
        TINYPY_DECREF(method);
    }
    tinypy_value_t *cmp = tinypy_native_function_new(vm, "__cmp__", 7U, __tinypy_code_cmp_method, NULL, NULL);
    tinypy_value_t *hash = tinypy_native_function_new(vm, "__hash__", 8U, __tinypy_code_hash_method, NULL, NULL);

    tinypy_type_set_attr(type, "__cmp__", 7U, cmp);
    tinypy_type_set_attr(type, "__hash__", 8U, hash);
    TINYPY_DECREF(hash);
    TINYPY_DECREF(cmp);
}
