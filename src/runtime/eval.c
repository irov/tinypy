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

static int __tinypy_eval_push_block(tinypy_frame_object_t *frame, int32_t type, size_t handler);

static size_t __tinypy_eval_stack_depth(const tinypy_frame_object_t *frame)
{
    return (size_t)(frame->stack_top - frame->value_stack);
}

static void __tinypy_eval_push_owned(tinypy_frame_object_t *frame, tinypy_value_t *value)
{
    assert(value != NULL);
    assert(__tinypy_eval_stack_depth(frame) < (size_t)tinypy_code_stack_size(frame->code));
    *frame->stack_top = value;
    frame->stack_top += 1;
}

static tinypy_value_t *__tinypy_eval_pop_owned(tinypy_frame_object_t *frame)
{
    assert(frame->stack_top > frame->value_stack);
    frame->stack_top -= 1;
    return *frame->stack_top;
}

static tinypy_value_t *__tinypy_eval_peek(const tinypy_frame_object_t *frame, size_t depth)
{
    assert(depth != 0U);
    assert(depth <= __tinypy_eval_stack_depth(frame));
    return frame->stack_top[-(ptrdiff_t)depth];
}

static void __tinypy_eval_unwind_stack(tinypy_frame_object_t *frame, size_t depth)
{
    assert(depth <= __tinypy_eval_stack_depth(frame));
    while (__tinypy_eval_stack_depth(frame) > depth) {
        tinypy_release(__tinypy_eval_pop_owned(frame));
    }
}

static void __tinypy_eval_clear_local_slots(tinypy_frame_object_t *frame)
{
    size_t count = (size_t)tinypy_code_local_count(frame->code) + tinypy_tuple_size(tinypy_code_cellvars(frame->code)) + tinypy_tuple_size(tinypy_code_freevars(frame->code));
    size_t index;

    for (index = 0U; index < count; index += 1U) {
        if (frame->locals_plus[index] != NULL) {
            tinypy_release(frame->locals_plus[index]);
            frame->locals_plus[index] = NULL;
        }
    }
}

static int32_t __tinypy_eval_line_number(tinypy_value_t *code, size_t instruction_offset)
{
    const unsigned char *bytes;
    size_t size;
    size_t index;
    size_t address = 0U;
    int32_t line = tinypy_code_first_line_number(code);

    bytes = (const unsigned char *)tinypy_string_view(tinypy_code_lnotab(code), &size);
    for (index = 0U; index + 1U < size; index += 2U) {
        address += bytes[index];
        if (address > instruction_offset) {
            break;
        }
        line += (int32_t)bytes[index + 1U];
    }
    return line;
}

static tinypy_value_t *__tinypy_eval_lookup_name(tinypy_frame_object_t *frame, tinypy_value_t *name, int include_locals)
{
    tinypy_value_t *value;

    if (include_locals != 0 && tinypy_dict_contains(frame->locals, name) != 0) {
        value = tinypy_dict_get(frame->locals, name);
        tinypy_retain(value);
        return value;
    }
    if (tinypy_dict_contains(frame->globals, name) != 0) {
        value = tinypy_dict_get(frame->globals, name);
        tinypy_retain(value);
        return value;
    }
    if (tinypy_dict_contains(frame->builtins, name) != 0) {
        value = tinypy_dict_get(frame->builtins, name);
        tinypy_retain(value);
        return value;
    }
    return NULL;
}

static tinypy_value_t *__tinypy_eval_default_output(tinypy_vm_t *vm, const char *name, size_t name_size)
{
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "sys", 3U);
    tinypy_value_t *module;
    tinypy_value_t *target;

    assert(tinypy_dict_contains(vm->modules, key) != 0);
    module = tinypy_dict_get(vm->modules, key);
    tinypy_release(key);
    target = tinypy_module_get_value(module, name, name_size);
    assert(target != NULL);
    tinypy_retain(target);
    return target;
}

static int32_t __tinypy_eval_print_whitespace(unsigned char character)
{
    return character == (unsigned char)' ' || character == (unsigned char)'\t' || character == (unsigned char)'\n' || character == (unsigned char)'\r' || character == (unsigned char)'\v' || character == (unsigned char)'\f';
}

static int32_t __tinypy_eval_print_item(tinypy_value_t *target, tinypy_value_t *item, tinypy_error_t **out_error)
{
    tinypy_value_t *text = tinypy_object_str(item, out_error);
    const unsigned char *bytes;
    size_t size;

    if (text == NULL) return INT32_C(0);
    bytes = tinypy_internal_text_bytes(text);
    size = tinypy_internal_text_byte_size(text);
    if (tinypy_internal_output_soft_space(target) != 0 && (size == 0U || bytes[0] != (unsigned char)'\n')) {
        if (tinypy_internal_output_write(target, " ", 1U, out_error) == 0) {
            tinypy_release(text);
            return INT32_C(0);
        }
    }
    if (tinypy_internal_output_write(target, bytes, size, out_error) == 0) {
        tinypy_release(text);
        return INT32_C(0);
    }
    tinypy_internal_output_set_soft_space(target, size == 0U || __tinypy_eval_print_whitespace(bytes[size - 1U]) == 0 ? INT32_C(1) : INT32_C(0));
    tinypy_release(text);
    return INT32_C(1);
}

static int32_t __tinypy_eval_print_newline(tinypy_value_t *target, tinypy_error_t **out_error)
{
    if (tinypy_internal_output_write(target, "\n", 1U, out_error) == 0) return INT32_C(0);
    tinypy_internal_output_set_soft_space(target, INT32_C(0));
    return INT32_C(1);
}

static void __tinypy_eval_make_name_error(tinypy_vm_t *vm, tinypy_value_t *name, tinypy_error_t **out_error)
{
    static const char prefix[] = "name '";
    static const char suffix[] = "' is not defined";
    const unsigned char *name_bytes;
    size_t name_size;
    size_t message_size;
    char *message;

    assert(tinypy_internal_value_kind(name) == TINYPY_VALUE_STRING || tinypy_internal_value_kind(name) == TINYPY_VALUE_UNICODE);
    name_bytes = tinypy_internal_text_bytes(name);
    name_size = tinypy_internal_text_byte_size(name);
    assert(name_size <= SIZE_MAX - (sizeof(prefix) - 1U) - (sizeof(suffix) - 1U) - 1U);
    message_size = (sizeof(prefix) - 1U) + name_size + (sizeof(suffix) - 1U);
    message = (char *)tinypy_internal_vm_allocate(vm, message_size + 1U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    (void)memcpy(message, prefix, sizeof(prefix) - 1U);
    if (name_size != 0U) (void)memcpy(message + sizeof(prefix) - 1U, name_bytes, name_size);
    (void)memcpy(message + sizeof(prefix) - 1U + name_size, suffix, sizeof(suffix));
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_NAME, message, out_error);
    tinypy_internal_vm_deallocate(vm, message, message_size + 1U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
}

static tinypy_value_t *__tinypy_eval_build_sequence(tinypy_frame_object_t *frame, size_t count, int as_list)
{
    tinypy_value_t *result;
    tinypy_value_t **items;
    size_t index;

    assert(count <= __tinypy_eval_stack_depth(frame));
    items = frame->stack_top - count;
    result = as_list != 0 ? tinypy_list_from_items(tinypy_internal_value_vm(frame->code), items, count) : tinypy_tuple_from_items(tinypy_internal_value_vm(frame->code), items, count);
    for (index = 0U; index < count; index += 1U) {
        tinypy_release(items[index]);
    }
    frame->stack_top -= count;
    return result;
}

static tinypy_value_t *__tinypy_eval_compare(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, size_t operation, tinypy_error_t **out_error)
{
    int32_t result;

    assert(operation <= (size_t)TINYPY_COMPARE_EXCEPTION_MATCH);
    result = tinypy_compare_bool(left, right, (tinypy_compare_operation_e)operation, out_error);
    if (result < 0) return NULL;
    return tinypy_bool_from_i32(vm, result);
}

static int __tinypy_eval_exception_class(tinypy_vm_t *vm, tinypy_value_t *value)
{
    return tinypy_internal_value_kind(value) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)value, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0;
}

static int __tinypy_eval_exception_instance(tinypy_vm_t *vm, tinypy_value_t *value)
{
    return tinypy_type_is_subtype(value->type, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0;
}

static tinypy_eval_reason_e __tinypy_eval_raise(tinypy_frame_object_t *frame, size_t argument, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(frame->code);
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
        if (argument != 1U && !(argument == 2U && tinypy_internal_value_kind(raise_value) == TINYPY_VALUE_NONE)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "instance exception may not have a separate value", out_error);
            goto cleanup;
        }
        exception = raise_type;
        tinypy_retain(exception);
    } else if (__tinypy_eval_exception_class(vm, raise_type) != 0) {
        if (raise_value != NULL && __tinypy_eval_exception_instance(vm, raise_value) != 0 && tinypy_type_is_subtype(raise_value->type, (tinypy_type_t *)raise_type) != 0) {
            exception = raise_value;
            tinypy_retain(exception);
        } else {
            tinypy_value_t *args;

            if (raise_value == NULL || tinypy_internal_value_kind(raise_value) == TINYPY_VALUE_NONE) args = tinypy_tuple_from_items(vm, NULL, 0U);
            else if (tinypy_internal_value_kind(raise_value) == TINYPY_VALUE_TUPLE) {
                args = raise_value;
                tinypy_retain(args);
            } else args = tinypy_tuple_from_items(vm, &raise_value, 1U);
            exception = tinypy_internal_exception_instantiate((tinypy_type_t *)raise_type, args, NULL, out_error);
            tinypy_release(args);
            if (exception == NULL) goto cleanup;
        }
    } else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exceptions must derive from BaseException", out_error);
        goto cleanup;
    }
    if (traceback != NULL && tinypy_internal_value_kind(traceback) != TINYPY_VALUE_NONE && tinypy_internal_value_kind(traceback) != TINYPY_VALUE_TRACEBACK) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "raise traceback must be a traceback or None", out_error);
        goto cleanup;
    }
    tinypy_internal_exception_set_raised(vm, exception, traceback != NULL && tinypy_internal_value_kind(traceback) == TINYPY_VALUE_TRACEBACK ? traceback : NULL);
    tinypy_internal_exception_make_diagnostic(vm, out_error);
cleanup:
    if (exception != NULL) tinypy_release(exception);
    if (raise_type != NULL) tinypy_release(raise_type);
    if (raise_value != NULL) tinypy_release(raise_value);
    if (traceback != NULL) tinypy_release(traceback);
    return reason;
}

static tinypy_eval_reason_e __tinypy_eval_end_finally(tinypy_frame_object_t *frame, tinypy_value_t **out_result, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(frame->code);
    tinypy_value_t *top = __tinypy_eval_pop_owned(frame);
    tinypy_eval_reason_e reason = TINYPY_EVAL_REASON_NOT;

    if (tinypy_internal_value_kind(top) == TINYPY_VALUE_INTEGER) {
        int64_t encoded = tinypy_integer_as_i64(top);

        if (encoded == TINYPY_EVAL_REASON_RETURN || encoded == TINYPY_EVAL_REASON_CONTINUE) *out_result = __tinypy_eval_pop_owned(frame);
        if (encoded == TINYPY_EVAL_REASON_EXCEPTION || encoded == TINYPY_EVAL_REASON_RERAISE || encoded == TINYPY_EVAL_REASON_RETURN || encoded == TINYPY_EVAL_REASON_BREAK || encoded == TINYPY_EVAL_REASON_CONTINUE) reason = (tinypy_eval_reason_e)encoded;
        else tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "END_FINALLY received an invalid unwind reason", out_error), reason = TINYPY_EVAL_REASON_EXCEPTION;
    } else if (tinypy_internal_value_kind(top) == TINYPY_VALUE_TYPE && __tinypy_eval_exception_class(vm, top) != 0) {
        tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
        tinypy_value_t *traceback = __tinypy_eval_pop_owned(frame);

        if (__tinypy_eval_exception_instance(vm, value) == 0 || (tinypy_internal_value_kind(traceback) != TINYPY_VALUE_NONE && tinypy_internal_value_kind(traceback) != TINYPY_VALUE_TRACEBACK)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "END_FINALLY received invalid exception state", out_error);
            reason = TINYPY_EVAL_REASON_EXCEPTION;
        } else {
            tinypy_internal_exception_set_raised(vm, value, tinypy_internal_value_kind(traceback) == TINYPY_VALUE_TRACEBACK ? traceback : NULL);
            tinypy_internal_exception_make_diagnostic(vm, out_error);
            reason = TINYPY_EVAL_REASON_RERAISE;
        }
        tinypy_release(traceback);
        tinypy_release(value);
    } else if (tinypy_internal_value_kind(top) != TINYPY_VALUE_NONE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "END_FINALLY popped invalid exception state", out_error);
        reason = TINYPY_EVAL_REASON_EXCEPTION;
    }
    tinypy_release(top);
    return reason;
}

static int __tinypy_eval_setup_with(tinypy_frame_object_t *frame, size_t handler, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(frame->code);
    tinypy_value_t *context = __tinypy_eval_pop_owned(frame);
    tinypy_value_t *exit_method = tinypy_object_get_attr(context, "__exit__", 8U, out_error);
    tinypy_value_t *enter_method;
    tinypy_value_t *args;
    tinypy_value_t *enter_result;

    if (exit_method == NULL) {
        tinypy_release(context);
        return 0;
    }
    enter_method = tinypy_object_get_attr(context, "__enter__", 9U, out_error);
    tinypy_release(context);
    if (enter_method == NULL) {
        tinypy_release(exit_method);
        return 0;
    }
    args = tinypy_tuple_from_items(vm, NULL, 0U);
    enter_result = tinypy_call(enter_method, args, NULL, out_error);
    tinypy_release(args);
    tinypy_release(enter_method);
    if (enter_result == NULL) {
        tinypy_release(exit_method);
        return 0;
    }
    __tinypy_eval_push_owned(frame, exit_method);
    (void)__tinypy_eval_push_block(frame, TINYPY_OP_SETUP_WITH, handler);
    __tinypy_eval_push_owned(frame, enter_result);
    return 1;
}

static tinypy_eval_reason_e __tinypy_eval_with_cleanup(tinypy_frame_object_t *frame, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(frame->code);
    tinypy_value_t *top = __tinypy_eval_pop_owned(frame);
    tinypy_value_t *type = NULL;
    tinypy_value_t *value = NULL;
    tinypy_value_t *traceback = NULL;
    tinypy_value_t *payload = NULL;
    tinypy_value_t *exit_method;
    tinypy_value_t *arguments[3];
    tinypy_value_t *args;
    tinypy_value_t *call_result;
    int is_exception = 0;

    if (tinypy_internal_value_kind(top) == TINYPY_VALUE_NONE) {
        exit_method = __tinypy_eval_pop_owned(frame);
    } else if (tinypy_internal_value_kind(top) == TINYPY_VALUE_INTEGER) {
        int64_t encoded = tinypy_integer_as_i64(top);

        if (encoded == TINYPY_EVAL_REASON_RETURN || encoded == TINYPY_EVAL_REASON_CONTINUE) payload = __tinypy_eval_pop_owned(frame);
        exit_method = __tinypy_eval_pop_owned(frame);
    } else {
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
    } else {
        arguments[0] = tinypy_none_get(vm);
        arguments[1] = tinypy_none_get(vm);
        arguments[2] = tinypy_none_get(vm);
    }
    args = tinypy_tuple_from_items(vm, arguments, 3U);
    if (is_exception == 0) {
        tinypy_release(arguments[2]);
        tinypy_release(arguments[1]);
        tinypy_release(arguments[0]);
    }
    call_result = tinypy_call(exit_method, args, NULL, out_error);
    tinypy_release(args);
    tinypy_release(exit_method);
    if (call_result == NULL) {
        if (payload != NULL) tinypy_release(payload);
        if (traceback != NULL) tinypy_release(traceback);
        if (value != NULL) tinypy_release(value);
        tinypy_release(top);
        return TINYPY_EVAL_REASON_EXCEPTION;
    }

    {
        int32_t call_truth = is_exception != 0 ? tinypy_truth(call_result, out_error) : INT32_C(0);

        if (call_truth < 0) {
            if (payload != NULL) tinypy_release(payload);
            if (traceback != NULL) tinypy_release(traceback);
            if (value != NULL) tinypy_release(value);
            tinypy_release(top);
            tinypy_release(call_result);
            return TINYPY_EVAL_REASON_EXCEPTION;
        }
    if (is_exception != 0 && call_truth != 0) {
        tinypy_release(traceback);
        tinypy_release(value);
        tinypy_release(type);
        __tinypy_eval_push_owned(frame, tinypy_none_get(vm));
    } else {
        if (payload != NULL) __tinypy_eval_push_owned(frame, payload);
        if (is_exception != 0) {
            __tinypy_eval_push_owned(frame, traceback);
            __tinypy_eval_push_owned(frame, value);
        }
        __tinypy_eval_push_owned(frame, top);
    }
    }
    tinypy_release(call_result);
    return TINYPY_EVAL_REASON_NOT;
}

static int __tinypy_eval_binary(tinypy_frame_object_t *frame, tinypy_binary_slot_t operation, tinypy_error_t **out_error)
{
    tinypy_value_t *right = __tinypy_eval_pop_owned(frame);
    tinypy_value_t *left = __tinypy_eval_pop_owned(frame);
    tinypy_value_t *result = operation(left, right, out_error);

    tinypy_release(right);
    tinypy_release(left);
    if (result == NULL) {
        return 0;
    }
    __tinypy_eval_push_owned(frame, result);
    return 1;
}

static tinypy_value_t *__tinypy_eval_pop_slice_key(tinypy_frame_object_t *frame, size_t variant)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(frame->code);
    tinypy_value_t *stop = (variant & 2U) != 0U ? __tinypy_eval_pop_owned(frame) : NULL;
    tinypy_value_t *start = (variant & 1U) != 0U ? __tinypy_eval_pop_owned(frame) : NULL;
    tinypy_value_t *slice = tinypy_slice_new(vm, start, stop, NULL);

    if (start != NULL) tinypy_release(start);
    if (stop != NULL) tinypy_release(stop);
    return slice;
}

static int __tinypy_eval_push_block(tinypy_frame_object_t *frame, int32_t type, size_t handler)
{
    tinypy_frame_block_t *block;

    assert(frame->block_count < TINYPY_FRAME_MAX_BLOCKS);
    block = &frame->blocks[frame->block_count];
    frame->block_count += 1U;
    block->type = type;
    block->handler = handler;
    block->stack_level = __tinypy_eval_stack_depth(frame);
    return 1;
}

static void __tinypy_eval_clear_diagnostic(tinypy_error_t **out_error)
{
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
    }
}

static void __tinypy_eval_push_exception_triple(tinypy_frame_object_t *frame, tinypy_value_t *type, tinypy_value_t *value, tinypy_value_t *traceback)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(frame->code);
    tinypy_value_t *stack_traceback;

    assert(type != NULL && value != NULL);
    if (traceback != NULL) {
        stack_traceback = traceback;
        tinypy_retain(stack_traceback);
    } else {
        stack_traceback = tinypy_none_get(vm);
    }
    tinypy_retain(value);
    tinypy_retain(type);
    __tinypy_eval_push_owned(frame, stack_traceback);
    __tinypy_eval_push_owned(frame, value);
    __tinypy_eval_push_owned(frame, type);
}

static int __tinypy_eval_unwind_reason(tinypy_frame_object_t *frame, tinypy_eval_reason_e *reason, size_t *out_instruction_offset, tinypy_value_t **in_out_result, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(frame->code);

    if (*reason == TINYPY_EVAL_REASON_EXCEPTION) {
        assert(vm->raised_type != NULL && vm->raised_value != NULL);
        tinypy_internal_traceback_here(vm, frame);
    } else if (*reason == TINYPY_EVAL_REASON_RERAISE) {
        assert(vm->raised_type != NULL && vm->raised_value != NULL);
        *reason = TINYPY_EVAL_REASON_EXCEPTION;
    }

    while (*reason != TINYPY_EVAL_REASON_NOT && frame->block_count != 0U) {
        tinypy_frame_block_t block = frame->blocks[frame->block_count - 1U];

        if (block.type == TINYPY_OP_SETUP_LOOP && *reason == TINYPY_EVAL_REASON_CONTINUE) {
            assert(*in_out_result != NULL);
            assert(tinypy_internal_value_kind(*in_out_result) == TINYPY_VALUE_INTEGER);
            assert(tinypy_integer_as_i64(*in_out_result) >= 0);
            *out_instruction_offset = (size_t)tinypy_integer_as_i64(*in_out_result);
            tinypy_release(*in_out_result);
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
                    __tinypy_eval_push_exception_triple(frame, vm->handled_type, vm->handled_value, vm->handled_traceback);
                } else {
                    __tinypy_eval_push_exception_triple(frame, vm->raised_type, vm->raised_value, vm->raised_traceback);
                    tinypy_internal_exception_clear_raised(vm);
                }
            } else {
                tinypy_value_t *why_value;

                if (*reason == TINYPY_EVAL_REASON_RETURN || *reason == TINYPY_EVAL_REASON_CONTINUE) {
                    assert(*in_out_result != NULL);
                    __tinypy_eval_push_owned(frame, *in_out_result);
                    *in_out_result = NULL;
                }
                why_value = tinypy_integer_from_i64(vm, (int64_t)*reason);
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

static int __tinypy_eval_decode(tinypy_frame_object_t *frame, const uint8_t *bytecode, size_t bytecode_size, size_t instruction_offset, tinypy_decoded_instruction_t *instruction, tinypy_error_t **out_error)
{
    tinypy_opcode_decode_status_e status = tinypy_opcode_decode(bytecode, bytecode_size, instruction_offset, instruction);

    if (status != TINYPY_OPCODE_DECODE_OK || instruction->defined == 0U) {
        tinypy_internal_make_vm_error(tinypy_internal_value_vm(frame->code), TINYPY_ERROR_RUNTIME, "invalid CPython 2.7 bytecode instruction", out_error);
        return 0;
    }
    return 1;
}

static int __tinypy_eval_call_append_iterable(tinypy_value_t *arguments, tinypy_value_t *iterable, tinypy_error_t **out_error)
{
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *iterator = tinypy_iter(iterable, &iteration_error);

    if (iterator == NULL) {
        if (out_error != NULL) *out_error = iteration_error;
        else if (iteration_error != NULL) tinypy_error_release(iteration_error);
        return 0;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) break;
        tinypy_list_append(arguments, item);
        tinypy_release(item);
    }
    tinypy_release(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL) *out_error = iteration_error;
        else tinypy_error_release(iteration_error);
        return 0;
    }
    return 1;
}

static int __tinypy_eval_call_merge_keywords(tinypy_vm_t *vm, tinypy_value_t *target, tinypy_value_t *source, tinypy_error_t **out_error)
{
    tinypy_dict_object_t *dict;
    size_t index;

    if (tinypy_internal_value_kind(source) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "double-star argument is not a dictionary", out_error);
        return 0;
    }
    dict = TINYPY_DICT_OBJECT(source);
    for (index = 0U; index <= dict->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &dict->table[index];

        if (entry->state != TINYPY_DICT_ENTRY_ACTIVE) continue;
        if (tinypy_internal_value_kind(entry->key) != TINYPY_VALUE_STRING) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "keyword name is not a string", out_error);
            return 0;
        }
        if (tinypy_dict_contains(target, entry->key) != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function received duplicate keyword arguments", out_error);
            return 0;
        }
        tinypy_dict_set(target, entry->key, entry->value);
    }
    return 1;
}

static tinypy_value_t *__tinypy_eval_call_stack(tinypy_frame_object_t *frame, size_t argument, int has_varargs, int has_var_keywords, tinypy_error_t **out_error)
{
    size_t positional_count = argument & 0xffU;
    size_t keyword_count = (argument >> 8U) & 0xffU;
    size_t consumed = 1U + positional_count + keyword_count * 2U + (has_varargs != 0 ? 1U : 0U) + (has_var_keywords != 0 ? 1U : 0U);
    tinypy_vm_t *vm = tinypy_internal_value_vm(frame->code);
    tinypy_value_t **first;
    tinypy_value_t *args = NULL;
    tinypy_value_t *kwargs = NULL;
    tinypy_value_t *result;
    size_t index;

    assert(consumed <= __tinypy_eval_stack_depth(frame));
    first = frame->stack_top - consumed;
    if (has_varargs != 0) {
        tinypy_value_t *arguments = tinypy_list_from_items(vm, first + 1U, positional_count);
        tinypy_value_t *iterable = first[1U + positional_count + keyword_count * 2U];

        if (__tinypy_eval_call_append_iterable(arguments, iterable, out_error) == 0) {
            tinypy_release(arguments);
            result = NULL;
            goto cleanup;
        }
        args = tinypy_tuple_from_items(vm, TINYPY_LIST_OBJECT(arguments)->items, tinypy_list_size(arguments));
        tinypy_release(arguments);
    } else {
        args = tinypy_tuple_from_items(vm, first + 1U, positional_count);
    }
    if (keyword_count != 0U || has_var_keywords != 0) {
        kwargs = tinypy_dict_new(vm);
        for (index = 0U; index < keyword_count; index += 1U) {
            tinypy_value_t *key = first[1U + positional_count + index * 2U];
            tinypy_value_t *value = first[2U + positional_count + index * 2U];

            if (tinypy_internal_value_kind(key) != TINYPY_VALUE_STRING) {
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
    result = tinypy_call(first[0], args, kwargs, out_error);
cleanup:
    if (kwargs != NULL) {
        tinypy_release(kwargs);
    }
    if (args != NULL) tinypy_release(args);
    for (index = 0U; index < consumed; index += 1U) {
        tinypy_release(first[index]);
    }
    frame->stack_top = first;
    return result;
}

static tinypy_value_t *__tinypy_eval_build_class(tinypy_frame_object_t *frame, tinypy_value_t *namespace_dict, tinypy_value_t *bases, tinypy_value_t *name, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(frame->code);
    tinypy_value_t *metaclass = NULL;
    tinypy_value_t *class_value;
    tinypy_value_t *class_arguments;
    tinypy_value_t *class_argument_items[3];
    size_t base_count;
    size_t index;
    tinypy_value_t *metaclass_key;
    size_t name_size;
    const char *name_bytes;

    if (tinypy_internal_value_kind(namespace_dict) != TINYPY_VALUE_DICT || tinypy_internal_value_kind(bases) != TINYPY_VALUE_TUPLE || tinypy_internal_value_kind(name) != TINYPY_VALUE_STRING) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "BUILD_CLASS received invalid operands", out_error);
        return NULL;
    }
    base_count = tinypy_tuple_size(bases);
    metaclass_key = tinypy_string_from_bytes(vm, "__metaclass__", 13U);
    if (tinypy_dict_contains(namespace_dict, metaclass_key) != 0) {
        metaclass = tinypy_dict_get(namespace_dict, metaclass_key);
        tinypy_retain(metaclass);
    } else if (tinypy_dict_contains(frame->globals, metaclass_key) != 0) {
        metaclass = tinypy_dict_get(frame->globals, metaclass_key);
        tinypy_retain(metaclass);
    }
    tinypy_release(metaclass_key);
    if (metaclass == NULL && (base_count == 0U || tinypy_internal_value_kind(tinypy_tuple_get(bases, 0U)) == TINYPY_VALUE_CLASS)) {
        for (index = 0U; index < base_count; index += 1U) {
            if (tinypy_internal_value_kind(tinypy_tuple_get(bases, index)) != TINYPY_VALUE_CLASS) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "a classic class cannot inherit from a new-style type", out_error);
                return NULL;
            }
        }
        name_bytes = (const char *)tinypy_string_view(name, &name_size);
        return tinypy_class_new(name_bytes, name_size, bases, namespace_dict, out_error);
    }
    if (metaclass == NULL && base_count != 0U) {
        tinypy_value_t *base = tinypy_tuple_get(bases, 0U);

        if (tinypy_internal_value_kind(base) != TINYPY_VALUE_TYPE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "class base is not a type", out_error);
            return NULL;
        }
        metaclass = &base->type->base.base;
        tinypy_retain(metaclass);
    } else if (metaclass == NULL) {
        metaclass = &vm->type_type.base.base;
        tinypy_retain(metaclass);
    }
    class_argument_items[0] = name;
    class_argument_items[1] = bases;
    class_argument_items[2] = namespace_dict;
    class_arguments = tinypy_tuple_from_items(vm, class_argument_items, 3U);
    class_value = tinypy_call(metaclass, class_arguments, NULL, out_error);
    tinypy_release(class_arguments);
    tinypy_release(metaclass);
    return class_value;
}

static int __tinypy_eval_bind_arguments(tinypy_frame_object_t *frame, tinypy_function_object_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function->code);
    size_t arg_count = (size_t)tinypy_code_arg_count(function->code);
    size_t positional_count = tinypy_tuple_size(args);
    size_t default_count = function->defaults != NULL ? tinypy_tuple_size(function->defaults) : 0U;
    size_t first_default;
    size_t index;
    int has_varargs = (tinypy_code_flags(function->code) & TINYPY_CODE_VARARGS) != 0;
    int has_var_keywords = (tinypy_code_flags(function->code) & TINYPY_CODE_VAR_KEYWORDS) != 0;
    tinypy_value_t *extra_keywords = NULL;

    assert(default_count <= arg_count);
    first_default = arg_count - default_count;
    if (positional_count > arg_count && has_varargs == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function received too many positional arguments", out_error);
        return 0;
    }
    for (index = 0U; index < positional_count && index < arg_count; index += 1U) {
        frame->locals_plus[index] = tinypy_tuple_get(args, index);
        tinypy_retain(frame->locals_plus[index]);
    }
    if (has_varargs != 0) {
        size_t extra_count = positional_count > arg_count ? positional_count - arg_count : 0U;
        tinypy_value_t *const *extra_items = extra_count != 0U ? &tinypy_internal_tuple_items(args)[arg_count] : NULL;

        frame->locals_plus[arg_count] = tinypy_tuple_from_items(vm, (tinypy_value_t *const *)extra_items, extra_count);
    }
    if (has_var_keywords != 0) {
        extra_keywords = tinypy_dict_new(vm);
        frame->locals_plus[arg_count + (has_varargs != 0 ? 1U : 0U)] = extra_keywords;
    }

    if (kwargs != NULL) {
        tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(kwargs);

        for (index = 0U; index <= dict->mask; index += 1U) {
            tinypy_dict_entry_t *entry = &dict->table[index];
            size_t parameter_index;
            int found = 0;

            if (entry->state != TINYPY_DICT_ENTRY_ACTIVE) {
                continue;
            }
            if (tinypy_internal_value_kind(entry->key) != TINYPY_VALUE_STRING) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "keyword name is not a string", out_error);
                return 0;
            }
            for (parameter_index = 0U; parameter_index < arg_count; parameter_index += 1U) {
                if (tinypy_equal(entry->key, tinypy_tuple_get(tinypy_code_varnames(function->code), parameter_index)) != 0) {
                    found = 1;
                    break;
                }
            }
            if (found != 0) {
                if (frame->locals_plus[parameter_index] != NULL) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function received multiple values for one argument", out_error);
                    return 0;
                }
                frame->locals_plus[parameter_index] = entry->value;
                tinypy_retain(entry->value);
            } else if (extra_keywords != NULL) {
                tinypy_dict_set(extra_keywords, entry->key, entry->value);
            } else {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function received an unexpected keyword argument", out_error);
                return 0;
            }
        }
    }

    for (index = 0U; index < arg_count; index += 1U) {
        if (frame->locals_plus[index] != NULL) {
            continue;
        }
        if (index < first_default) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function is missing a required argument", out_error);
            return 0;
        }
        frame->locals_plus[index] = tinypy_tuple_get(function->defaults, index - first_default);
        tinypy_retain(frame->locals_plus[index]);
    }
    return 1;
}

static void __tinypy_eval_initialize_cells(tinypy_frame_object_t *frame, tinypy_function_object_t *function)
{
    tinypy_value_t *code = frame->code;
    tinypy_vm_t *vm = tinypy_internal_value_vm(code);
    size_t local_count = (size_t)tinypy_code_local_count(code);
    size_t cell_count = tinypy_tuple_size(tinypy_code_cellvars(code));
    size_t free_count = tinypy_tuple_size(tinypy_code_freevars(code));
    size_t named_argument_count = (size_t)tinypy_code_arg_count(code);
    size_t cell_index;

    if ((tinypy_code_flags(code) & TINYPY_CODE_VARARGS) != 0) {
        named_argument_count += 1U;
    }
    if ((tinypy_code_flags(code) & TINYPY_CODE_VAR_KEYWORDS) != 0) {
        named_argument_count += 1U;
    }
    for (cell_index = 0U; cell_index < cell_count; cell_index += 1U) {
        tinypy_value_t *cell_name = tinypy_tuple_get(tinypy_code_cellvars(code), cell_index);
        tinypy_value_t *content = NULL;
        size_t argument_index;

        for (argument_index = 0U; argument_index < named_argument_count; argument_index += 1U) {
            if (tinypy_equal(cell_name, tinypy_tuple_get(tinypy_code_varnames(code), argument_index)) != 0) {
                content = frame->locals_plus[argument_index];
                break;
            }
        }
        frame->locals_plus[local_count + cell_index] = tinypy_cell_new(vm, content);
    }
    if (free_count != 0U) {
        assert(function != NULL);
        assert(function->closure != NULL);
        assert(tinypy_tuple_size(function->closure) == free_count);
        for (cell_index = 0U; cell_index < free_count; cell_index += 1U) {
            tinypy_value_t *cell = tinypy_tuple_get(function->closure, cell_index);

            frame->locals_plus[local_count + cell_count + cell_index] = cell;
            tinypy_retain(cell);
        }
    }
}

static tinypy_value_t *__tinypy_eval_code_bound(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals, tinypy_function_object_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_generator_object_t *generator, tinypy_value_t *throw_value, tinypy_value_t *throw_traceback, int *out_yielded, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm;
    tinypy_value_t *frame_value;
    tinypy_frame_object_t *frame;
    const uint8_t *bytecode;
    size_t bytecode_size;
    size_t instruction_offset;
    tinypy_value_t *result = NULL;
    tinypy_eval_reason_e reason = TINYPY_EVAL_REASON_NOT;
    int generator_execution = generator != NULL;

    if (out_yielded != NULL) *out_yielded = 0;
    if (generator_execution != 0) {
        frame_value = generator->frame;
        assert(frame_value != NULL);
        frame = TINYPY_FRAME_OBJECT(frame_value);
        code = frame->code;
        vm = tinypy_internal_value_vm(code);
        instruction_offset = generator->instruction_offset;
        assert(frame->back == NULL);
        assert(frame->previous_handled_type == NULL && frame->previous_handled_value == NULL && frame->previous_handled_traceback == NULL);
        frame->back = vm->current_frame != NULL ? &vm->current_frame->base.base : NULL;
        if (frame->back != NULL) tinypy_retain(frame->back);
        frame->previous_handled_type = vm->handled_type;
        frame->previous_handled_value = vm->handled_value;
        frame->previous_handled_traceback = vm->handled_traceback;
        if (frame->previous_handled_type != NULL) tinypy_retain(frame->previous_handled_type);
        if (frame->previous_handled_value != NULL) tinypy_retain(frame->previous_handled_value);
        if (frame->previous_handled_traceback != NULL) tinypy_retain(frame->previous_handled_traceback);
        tinypy_internal_exception_clear_handled(vm);
        vm->handled_type = generator->handled_type;
        vm->handled_value = generator->handled_value;
        vm->handled_traceback = generator->handled_traceback;
        generator->handled_type = NULL;
        generator->handled_value = NULL;
        generator->handled_traceback = NULL;
        if (generator->started != 0 && throw_value == NULL) {
            tinypy_retain(args);
            __tinypy_eval_push_owned(frame, args);
        }
    } else {
        assert(code != NULL);
        vm = tinypy_internal_value_vm(code);
        assert(tinypy_internal_vm_valid(vm));
        assert(tinypy_internal_value_kind(code) == TINYPY_VALUE_CODE);
        assert(globals != NULL);
        assert(tinypy_internal_value_belongs_to(vm, globals));
        assert(tinypy_internal_value_kind(globals) == TINYPY_VALUE_DICT);
        assert(locals == NULL || tinypy_internal_value_belongs_to(vm, locals));
        assert(locals == NULL || tinypy_internal_value_kind(locals) == TINYPY_VALUE_DICT);
        instruction_offset = 0U;
        frame_value = tinypy_frame_new(code, globals, locals);
        frame = TINYPY_FRAME_OBJECT(frame_value);
        if (function != NULL && __tinypy_eval_bind_arguments(frame, function, args, kwargs, out_error) == 0) {
            tinypy_release(frame_value);
            return NULL;
        }
        __tinypy_eval_initialize_cells(frame, function);
    }
    assert(vm->evaluation_depth < 1000U);
    tinypy_internal_clear_error(out_error);

    bytecode = (const uint8_t *)tinypy_string_view(tinypy_code_bytecode(code), &bytecode_size);
    vm->current_frame = frame;
    vm->evaluation_depth += 1U;

    if (throw_value != NULL) {
        tinypy_internal_exception_set_raised(vm, throw_value, throw_traceback);
        reason = TINYPY_EVAL_REASON_EXCEPTION;
        (void)__tinypy_eval_unwind_reason(frame, &reason, &instruction_offset, &result, out_error);
    }

    while (reason == TINYPY_EVAL_REASON_NOT && instruction_offset < bytecode_size) {
        tinypy_decoded_instruction_t instruction;
        size_t argument;

        if (vm->has_host != 0 && vm->host.poll_interrupt != NULL && vm->host.poll_interrupt(vm->host.user_data) != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INTERRUPT, "execution interrupted by host", out_error);
            reason = TINYPY_EVAL_REASON_EXCEPTION;
            goto unwind_reason;
        }
        if (__tinypy_eval_decode(frame, bytecode, bytecode_size, instruction_offset, &instruction, out_error) == 0) {
            reason = TINYPY_EVAL_REASON_EXCEPTION;
            goto unwind_reason;
        }
        assert(instruction.offset <= (size_t)INT32_MAX);
        assert(instruction.argument <= (uint64_t)SIZE_MAX);
        frame->last_instruction = (int32_t)instruction.offset;
        frame->line_number = __tinypy_eval_line_number(code, instruction.offset);
        instruction_offset = instruction.next_offset;
        argument = (size_t)instruction.argument;

        switch (instruction.opcode) {
        case TINYPY_OP_NOP:
            break;
        case TINYPY_OP_POP_TOP:
            tinypy_release(__tinypy_eval_pop_owned(frame));
            break;
        case TINYPY_OP_PRINT_ITEM:
            {
                tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *target = __tinypy_eval_default_output(vm, "stdout", 6U);
                int32_t printed = __tinypy_eval_print_item(target, item, out_error);

                tinypy_release(target);
                tinypy_release(item);
                if (printed == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_PRINT_NEWLINE:
            {
                tinypy_value_t *target = __tinypy_eval_default_output(vm, "stdout", 6U);
                int32_t printed = __tinypy_eval_print_newline(target, out_error);

                tinypy_release(target);
                if (printed == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_PRINT_ITEM_TO:
            {
                tinypy_value_t *target = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
                int32_t printed = __tinypy_eval_print_item(target, item, out_error);

                tinypy_release(item);
                tinypy_release(target);
                if (printed == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_PRINT_NEWLINE_TO:
            {
                tinypy_value_t *target = __tinypy_eval_pop_owned(frame);
                int32_t printed = __tinypy_eval_print_newline(target, out_error);

                tinypy_release(target);
                if (printed == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_EXEC_STMT:
            {
                tinypy_value_t *locals_value = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *globals_value = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *source = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *execution_globals = tinypy_internal_value_kind(globals_value) == TINYPY_VALUE_NONE ? frame->globals : globals_value;
                tinypy_value_t *execution_locals = tinypy_internal_value_kind(locals_value) == TINYPY_VALUE_NONE ? (tinypy_internal_value_kind(globals_value) == TINYPY_VALUE_NONE ? frame->locals : execution_globals) : locals_value;
                tinypy_value_t *execution_result = NULL;

                if (tinypy_internal_value_kind(execution_globals) != TINYPY_VALUE_DICT || tinypy_internal_value_kind(execution_locals) != TINYPY_VALUE_DICT) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exec globals and locals must be dictionaries", out_error);
                } else if (tinypy_internal_value_kind(source) == TINYPY_VALUE_CODE) {
                    execution_result = __tinypy_eval_code_bound(source, execution_globals, execution_locals, NULL, NULL, NULL, NULL, NULL, NULL, NULL, out_error);
                } else if (tinypy_internal_value_kind(source) == TINYPY_VALUE_STRING || tinypy_internal_value_kind(source) == TINYPY_VALUE_UNICODE) {
                    tinypy_compile_options_t options;
                    tinypy_value_t *execution_code;
                    const void *source_bytes;
                    size_t source_size;
                    const char *filename;
                    size_t filename_size;
                    int32_t source_is_unicode;

                    if (tinypy_internal_value_kind(source) == TINYPY_VALUE_STRING) {
                        source_bytes = tinypy_string_view(source, &source_size);
                        source_is_unicode = 0;
                    }
                    else {
                        size_t code_points;

                        source_bytes = tinypy_unicode_utf8_view(source, &source_size, &code_points);
                        source_is_unicode = 1;
                    }
                    filename = (const char *)tinypy_string_view(tinypy_code_filename(frame->code), &filename_size);
                    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
                    options.dont_inherit = 0;
                    options.optimize_level = vm->optimize_level;
                    execution_code = tinypy_internal_compiler_compile_source(vm, source_bytes, source_size, source_is_unicode, filename, filename_size, &options, out_error);
                    if (execution_code != NULL) {
                        execution_result = tinypy_exec_code(execution_code, execution_globals, execution_locals, out_error);
                        tinypy_release(execution_code);
                    }
                } else {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exec requires a string or code object", out_error);
                }
                tinypy_release(source);
                tinypy_release(globals_value);
                tinypy_release(locals_value);
                if (execution_result == NULL) reason = TINYPY_EVAL_REASON_EXCEPTION;
                else tinypy_release(execution_result);
            }
            break;
        case TINYPY_OP_ROT_TWO:
            {
                tinypy_value_t *top = __tinypy_eval_peek(frame, 1U);
                frame->stack_top[-1] = __tinypy_eval_peek(frame, 2U);
                frame->stack_top[-2] = top;
            }
            break;
        case TINYPY_OP_ROT_THREE:
            {
                tinypy_value_t *top = __tinypy_eval_peek(frame, 1U);
                frame->stack_top[-1] = __tinypy_eval_peek(frame, 2U);
                frame->stack_top[-2] = __tinypy_eval_peek(frame, 3U);
                frame->stack_top[-3] = top;
            }
            break;
        case TINYPY_OP_ROT_FOUR:
            {
                tinypy_value_t *top = __tinypy_eval_peek(frame, 1U);
                frame->stack_top[-1] = __tinypy_eval_peek(frame, 2U);
                frame->stack_top[-2] = __tinypy_eval_peek(frame, 3U);
                frame->stack_top[-3] = __tinypy_eval_peek(frame, 4U);
                frame->stack_top[-4] = top;
            }
            break;
        case TINYPY_OP_DUP_TOP:
            {
                tinypy_value_t *value = __tinypy_eval_peek(frame, 1U);
                tinypy_retain(value);
                __tinypy_eval_push_owned(frame, value);
            }
            break;
        case TINYPY_OP_DUP_TOPX:
            {
                tinypy_value_t **first;
                size_t index;

                assert(argument == 2U || argument == 3U);
                assert(argument <= __tinypy_eval_stack_depth(frame));
                first = frame->stack_top - argument;
                for (index = 0U; index < argument; index += 1U) {
                    tinypy_value_t *value = first[index];
                    tinypy_retain(value);
                    __tinypy_eval_push_owned(frame, value);
                }
            }
            break;
        case TINYPY_OP_UNARY_NOT:
            {
                tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
                int32_t truth = tinypy_truth(value, out_error);
                tinypy_release(value);
                if (truth < 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
                else __tinypy_eval_push_owned(frame, tinypy_bool_from_i32(vm, truth == 0));
            }
            break;
        case TINYPY_OP_UNARY_POSITIVE:
        case TINYPY_OP_UNARY_NEGATIVE:
        case TINYPY_OP_UNARY_INVERT:
            {
                tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *unary_result;

                if (instruction.opcode == TINYPY_OP_UNARY_POSITIVE) {
                    unary_result = tinypy_positive(value, out_error);
                } else if (instruction.opcode == TINYPY_OP_UNARY_NEGATIVE) {
                    unary_result = tinypy_negative(value, out_error);
                } else {
                    unary_result = tinypy_invert(value, out_error);
                }
                tinypy_release(value);
                if (unary_result == NULL) {
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                    break;
                }
                __tinypy_eval_push_owned(frame, unary_result);
            }
            break;
        case TINYPY_OP_BINARY_ADD:
        case TINYPY_OP_INPLACE_ADD:
            if (__tinypy_eval_binary(frame, tinypy_add, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_BINARY_SUBTRACT:
        case TINYPY_OP_INPLACE_SUBTRACT:
            if (__tinypy_eval_binary(frame, tinypy_subtract, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_BINARY_MULTIPLY:
        case TINYPY_OP_INPLACE_MULTIPLY:
            if (__tinypy_eval_binary(frame, tinypy_multiply, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_BINARY_POWER:
        case TINYPY_OP_INPLACE_POWER:
            if (__tinypy_eval_binary(frame, tinypy_power, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_BINARY_DIVIDE:
        case TINYPY_OP_INPLACE_DIVIDE:
            if (__tinypy_eval_binary(frame, tinypy_divide, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_BINARY_MODULO:
        case TINYPY_OP_INPLACE_MODULO:
            if (__tinypy_eval_binary(frame, tinypy_remainder, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_BINARY_FLOOR_DIVIDE:
        case TINYPY_OP_INPLACE_FLOOR_DIVIDE:
            if (__tinypy_eval_binary(frame, tinypy_floor_divide, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_BINARY_TRUE_DIVIDE:
        case TINYPY_OP_INPLACE_TRUE_DIVIDE:
            if (__tinypy_eval_binary(frame, tinypy_true_divide, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_BINARY_LSHIFT:
        case TINYPY_OP_INPLACE_LSHIFT:
            if (__tinypy_eval_binary(frame, tinypy_left_shift, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_BINARY_RSHIFT:
        case TINYPY_OP_INPLACE_RSHIFT:
            if (__tinypy_eval_binary(frame, tinypy_right_shift, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_BINARY_AND:
        case TINYPY_OP_INPLACE_AND:
            if (__tinypy_eval_binary(frame, tinypy_bit_and, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_BINARY_XOR:
        case TINYPY_OP_INPLACE_XOR:
            if (__tinypy_eval_binary(frame, tinypy_bit_xor, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_BINARY_OR:
        case TINYPY_OP_INPLACE_OR:
            if (__tinypy_eval_binary(frame, tinypy_bit_or, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_BINARY_SUBSCR:
            {
                tinypy_value_t *key = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *item = tinypy_get_item(container, key, out_error);

                tinypy_release(key);
                tinypy_release(container);
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
        case TINYPY_OP_SLICE_3:
            {
                size_t variant = (size_t)(instruction.opcode - TINYPY_OP_SLICE_0);
                tinypy_value_t *slice = __tinypy_eval_pop_slice_key(frame, variant);
                tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *item = tinypy_get_item(container, slice, out_error);

                tinypy_release(slice);
                tinypy_release(container);
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
        case TINYPY_OP_STORE_SLICE_3:
            {
                size_t variant = (size_t)(instruction.opcode - TINYPY_OP_STORE_SLICE_0);
                tinypy_value_t *slice = __tinypy_eval_pop_slice_key(frame, variant);
                tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
                int32_t stored = tinypy_set_item(container, slice, value, out_error);

                tinypy_release(slice);
                tinypy_release(container);
                tinypy_release(value);
                if (stored == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_DELETE_SLICE_0:
        case TINYPY_OP_DELETE_SLICE_1:
        case TINYPY_OP_DELETE_SLICE_2:
        case TINYPY_OP_DELETE_SLICE_3:
            {
                size_t variant = (size_t)(instruction.opcode - TINYPY_OP_DELETE_SLICE_0);
                tinypy_value_t *slice = __tinypy_eval_pop_slice_key(frame, variant);
                tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
                int32_t deleted = tinypy_delete_item(container, slice, out_error);

                tinypy_release(slice);
                tinypy_release(container);
                if (deleted == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_STORE_SUBSCR:
            {
                tinypy_value_t *key = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
                int32_t stored = tinypy_set_item(container, key, value, out_error);

                tinypy_release(key);
                tinypy_release(container);
                tinypy_release(value);
                if (stored == 0) {
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                }
            }
            break;
        case TINYPY_OP_DELETE_SUBSCR:
            {
                tinypy_value_t *key = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *container = __tinypy_eval_pop_owned(frame);
                int32_t deleted = tinypy_delete_item(container, key, out_error);

                tinypy_release(key);
                tinypy_release(container);
                if (deleted == 0) {
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                }
            }
            break;
        case TINYPY_OP_GET_ITER:
            {
                tinypy_value_t *iterable = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *iterator = tinypy_iter(iterable, out_error);

                tinypy_release(iterable);
                if (iterator == NULL) {
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                    break;
                }
                __tinypy_eval_push_owned(frame, iterator);
            }
            break;
        case TINYPY_OP_LOAD_CONST:
            {
                tinypy_value_t *value = tinypy_tuple_get(tinypy_code_consts(code), argument);
                tinypy_retain(value);
                __tinypy_eval_push_owned(frame, value);
            }
            break;
        case TINYPY_OP_LOAD_NAME:
        case TINYPY_OP_LOAD_GLOBAL:
            {
                tinypy_value_t *name = tinypy_tuple_get(tinypy_code_names(code), argument);
                tinypy_value_t *value = __tinypy_eval_lookup_name(frame, name, instruction.opcode == TINYPY_OP_LOAD_NAME);
                if (value == NULL) {
                    __tinypy_eval_make_name_error(vm, name, out_error);
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                    break;
                }
                __tinypy_eval_push_owned(frame, value);
            }
            break;
        case TINYPY_OP_STORE_NAME:
        case TINYPY_OP_STORE_GLOBAL:
            {
                tinypy_value_t *name = tinypy_tuple_get(tinypy_code_names(code), argument);
                tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
                tinypy_dict_set(instruction.opcode == TINYPY_OP_STORE_NAME ? frame->locals : frame->globals, name, value);
                tinypy_release(value);
            }
            break;
        case TINYPY_OP_DELETE_NAME:
        case TINYPY_OP_DELETE_GLOBAL:
            {
                tinypy_value_t *name = tinypy_tuple_get(tinypy_code_names(code), argument);
                tinypy_value_t *mapping = instruction.opcode == TINYPY_OP_DELETE_NAME ? frame->locals : frame->globals;
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
            } else {
                tinypy_retain(frame->locals_plus[argument]);
                __tinypy_eval_push_owned(frame, frame->locals_plus[argument]);
            }
            break;
        case TINYPY_OP_LOAD_CLOSURE:
            {
                size_t local_count = (size_t)tinypy_code_local_count(code);
                tinypy_value_t *cell = frame->locals_plus[local_count + argument];

                assert(cell != NULL);
                assert(tinypy_internal_value_kind(cell) == TINYPY_VALUE_CELL);
                tinypy_retain(cell);
                __tinypy_eval_push_owned(frame, cell);
            }
            break;
        case TINYPY_OP_LOAD_DEREF:
            {
                size_t local_count = (size_t)tinypy_code_local_count(code);
                tinypy_value_t *cell = frame->locals_plus[local_count + argument];
                tinypy_value_t *content;

                assert(cell != NULL);
                assert(tinypy_internal_value_kind(cell) == TINYPY_VALUE_CELL);
                content = tinypy_cell_get(cell);
                if (content == NULL) {
                    tinypy_internal_make_vm_error(vm, argument < tinypy_tuple_size(tinypy_code_cellvars(code)) ? TINYPY_ERROR_UNBOUND_LOCAL : TINYPY_ERROR_NAME, "free variable referenced before assignment", out_error);
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                    break;
                }
                tinypy_retain(content);
                __tinypy_eval_push_owned(frame, content);
            }
            break;
        case TINYPY_OP_STORE_DEREF:
            {
                size_t local_count = (size_t)tinypy_code_local_count(code);
                tinypy_value_t *cell = frame->locals_plus[local_count + argument];
                tinypy_value_t *content = __tinypy_eval_pop_owned(frame);

                assert(cell != NULL);
                assert(tinypy_internal_value_kind(cell) == TINYPY_VALUE_CELL);
                tinypy_cell_set(cell, content);
                tinypy_release(content);
            }
            break;
        case TINYPY_OP_STORE_FAST:
            {
                tinypy_value_t *previous = frame->locals_plus[argument];
                frame->locals_plus[argument] = __tinypy_eval_pop_owned(frame);
                if (previous != NULL) {
                    tinypy_release(previous);
                }
            }
            break;
        case TINYPY_OP_DELETE_FAST:
            if (frame->locals_plus[argument] == NULL) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_UNBOUND_LOCAL, "local variable referenced before assignment", out_error);
                reason = TINYPY_EVAL_REASON_EXCEPTION;
            } else {
                tinypy_release(frame->locals_plus[argument]);
                frame->locals_plus[argument] = NULL;
            }
            break;
        case TINYPY_OP_LOAD_LOCALS:
            tinypy_retain(frame->locals);
            __tinypy_eval_push_owned(frame, frame->locals);
            break;
        case TINYPY_OP_LOAD_ATTR:
            {
                tinypy_value_t *name = tinypy_tuple_get(tinypy_code_names(code), argument);
                tinypy_value_t *object = __tinypy_eval_pop_owned(frame);
                const char *name_bytes;
                size_t name_size;
                tinypy_value_t *attribute;

                name_bytes = (const char *)tinypy_string_view(name, &name_size);
                attribute = tinypy_object_get_attr(object, name_bytes, name_size, out_error);
                tinypy_release(object);
                if (attribute == NULL) {
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                    break;
                }
                __tinypy_eval_push_owned(frame, attribute);
            }
            break;
        case TINYPY_OP_STORE_ATTR:
            {
                tinypy_value_t *name = tinypy_tuple_get(tinypy_code_names(code), argument);
                tinypy_value_t *object = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *attribute_value = __tinypy_eval_pop_owned(frame);
                const char *name_bytes;
                size_t name_size;
                int32_t stored;

                name_bytes = (const char *)tinypy_string_view(name, &name_size);
                stored = tinypy_object_set_attr(object, name_bytes, name_size, attribute_value, out_error);
                tinypy_release(attribute_value);
                tinypy_release(object);
                if (stored == 0) {
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                }
            }
            break;
        case TINYPY_OP_DELETE_ATTR:
            {
                tinypy_value_t *name = tinypy_tuple_get(tinypy_code_names(code), argument);
                tinypy_value_t *object = __tinypy_eval_pop_owned(frame);
                const char *name_bytes;
                size_t name_size;
                int32_t deleted;

                name_bytes = (const char *)tinypy_string_view(name, &name_size);
                deleted = tinypy_object_delete_attr(object, name_bytes, name_size, out_error);
                tinypy_release(object);
                if (deleted == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_IMPORT_NAME:
            {
                tinypy_value_t *name = tinypy_tuple_get(tinypy_code_names(code), argument);
                tinypy_value_t *fromlist = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *level_value = __tinypy_eval_pop_owned(frame);
                const char *name_bytes;
                size_t name_size;
                int64_t level;
                tinypy_value_t *module;

                assert(tinypy_internal_value_kind(name) == TINYPY_VALUE_STRING);
                assert(tinypy_internal_value_kind(level_value) == TINYPY_VALUE_INTEGER || tinypy_internal_value_kind(level_value) == TINYPY_VALUE_LONG);
                name_bytes = (const char *)tinypy_string_view(name, &name_size);
                level = tinypy_internal_value_kind(level_value) == TINYPY_VALUE_LONG ? tinypy_long_as_i64(level_value) : tinypy_integer_as_i64(level_value);
                assert(level >= INT32_MIN && level <= INT32_MAX);
                module = tinypy_import_module(vm, name_bytes, name_size, frame->globals, fromlist, (int32_t)level, out_error);
                tinypy_release(level_value);
                tinypy_release(fromlist);
                if (module == NULL) {
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                    break;
                }
                __tinypy_eval_push_owned(frame, module);
            }
            break;
        case TINYPY_OP_IMPORT_FROM:
            {
                tinypy_value_t *name = tinypy_tuple_get(tinypy_code_names(code), argument);
                tinypy_value_t *module = __tinypy_eval_peek(frame, 1U);
                const char *name_bytes;
                size_t name_size;
                tinypy_value_t *imported;

                assert(tinypy_internal_value_kind(name) == TINYPY_VALUE_STRING);
                name_bytes = (const char *)tinypy_string_view(name, &name_size);
                imported = tinypy_internal_import_from(module, name_bytes, name_size, out_error);
                if (imported == NULL) {
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                    break;
                }
                __tinypy_eval_push_owned(frame, imported);
            }
            break;
        case TINYPY_OP_IMPORT_STAR:
            {
                tinypy_value_t *module = __tinypy_eval_pop_owned(frame);
                int32_t imported = tinypy_internal_import_star(module, frame->locals, out_error);

                tinypy_release(module);
                if (imported == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_BUILD_TUPLE:
            __tinypy_eval_push_owned(frame, __tinypy_eval_build_sequence(frame, argument, 0));
            break;
        case TINYPY_OP_BUILD_LIST:
            __tinypy_eval_push_owned(frame, __tinypy_eval_build_sequence(frame, argument, 1));
            break;
        case TINYPY_OP_BUILD_SET:
            {
                tinypy_value_t *set = tinypy_set_new(vm);
                size_t index;

                for (index = 0U; index < argument; index += 1U) {
                    tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
                    int32_t added = tinypy_set_add(set, item, out_error);

                    tinypy_release(item);
                    if (added == 0) {
                        tinypy_release(set);
                        reason = TINYPY_EVAL_REASON_EXCEPTION;
                        break;
                    }
                }
                if (reason == TINYPY_EVAL_REASON_NOT) __tinypy_eval_push_owned(frame, set);
            }
            break;
        case TINYPY_OP_BUILD_MAP:
            __tinypy_eval_push_owned(frame, tinypy_dict_new(vm));
            break;
        case TINYPY_OP_BUILD_SLICE:
            {
                tinypy_value_t *step;
                tinypy_value_t *stop;
                tinypy_value_t *start;
                tinypy_value_t *slice;

                assert(argument == 2U || argument == 3U);
                step = argument == 3U ? __tinypy_eval_pop_owned(frame) : NULL;
                stop = __tinypy_eval_pop_owned(frame);
                start = __tinypy_eval_pop_owned(frame);
                slice = tinypy_slice_new(vm, start, stop, step);
                if (step != NULL) tinypy_release(step);
                tinypy_release(stop);
                tinypy_release(start);
                __tinypy_eval_push_owned(frame, slice);
            }
            break;
        case TINYPY_OP_STORE_MAP:
            {
                tinypy_value_t *key = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *dict = __tinypy_eval_peek(frame, 1U);
                tinypy_dict_set(dict, key, value);
                tinypy_release(value);
                tinypy_release(key);
            }
            break;
        case TINYPY_OP_LIST_APPEND:
            {
                tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *list = __tinypy_eval_peek(frame, argument);

                tinypy_list_append(list, item);
                tinypy_release(item);
            }
            break;
        case TINYPY_OP_SET_ADD:
            {
                tinypy_value_t *item = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *set = __tinypy_eval_peek(frame, argument);
                int32_t added = tinypy_set_add(set, item, out_error);

                tinypy_release(item);
                if (added == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            }
            break;
        case TINYPY_OP_MAP_ADD:
            {
                tinypy_value_t *key = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *dict = __tinypy_eval_peek(frame, argument);

                tinypy_dict_set(dict, key, value);
                tinypy_release(value);
                tinypy_release(key);
            }
            break;
        case TINYPY_OP_UNPACK_SEQUENCE:
            {
                tinypy_value_t *sequence = __tinypy_eval_pop_owned(frame);
                size_t count;
                size_t index;

                if (tinypy_internal_value_kind(sequence) == TINYPY_VALUE_TUPLE) {
                    count = tinypy_tuple_size(sequence);
                } else if (tinypy_internal_value_kind(sequence) == TINYPY_VALUE_LIST) {
                    count = tinypy_list_size(sequence);
                } else {
                    count = SIZE_MAX;
                }
                if (count != argument) {
                    tinypy_release(sequence);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "unpack sequence has the wrong size", out_error);
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                    break;
                }
                for (index = count; index != 0U; index -= 1U) {
                    tinypy_value_t *item = tinypy_internal_value_kind(sequence) == TINYPY_VALUE_TUPLE ? tinypy_tuple_get(sequence, index - 1U) : tinypy_list_get(sequence, index - 1U);
                    tinypy_retain(item);
                    __tinypy_eval_push_owned(frame, item);
                }
                tinypy_release(sequence);
            }
            break;
        case TINYPY_OP_BUILD_CLASS:
            {
                tinypy_value_t *namespace_dict = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *bases = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *name = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *class_value = __tinypy_eval_build_class(frame, namespace_dict, bases, name, out_error);

                tinypy_release(name);
                tinypy_release(bases);
                tinypy_release(namespace_dict);
                if (class_value == NULL) {
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                    break;
                }
                __tinypy_eval_push_owned(frame, class_value);
            }
            break;
        case TINYPY_OP_COMPARE_OP:
            {
                tinypy_value_t *right = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *left = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *comparison = __tinypy_eval_compare(vm, left, right, argument, out_error);
                tinypy_release(right);
                tinypy_release(left);
                if (comparison == NULL) {
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                    break;
                }
                __tinypy_eval_push_owned(frame, comparison);
            }
            break;
        case TINYPY_OP_MAKE_FUNCTION:
            {
                tinypy_value_t *function_code = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *defaults = argument != 0U ? __tinypy_eval_build_sequence(frame, argument, 0) : NULL;
                tinypy_value_t *created_function;

                assert(tinypy_internal_value_kind(function_code) == TINYPY_VALUE_CODE);
                created_function = tinypy_function_new(function_code, frame->globals, defaults, NULL);
                if (defaults != NULL) {
                    tinypy_release(defaults);
                }
                tinypy_release(function_code);
                __tinypy_eval_push_owned(frame, created_function);
            }
            break;
        case TINYPY_OP_MAKE_CLOSURE:
            {
                tinypy_value_t *function_code = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *closure = __tinypy_eval_pop_owned(frame);
                tinypy_value_t *defaults = argument != 0U ? __tinypy_eval_build_sequence(frame, argument, 0) : NULL;
                tinypy_value_t *created_function;

                assert(tinypy_internal_value_kind(function_code) == TINYPY_VALUE_CODE);
                assert(tinypy_internal_value_kind(closure) == TINYPY_VALUE_TUPLE);
                created_function = tinypy_function_new(function_code, frame->globals, defaults, closure);
                if (defaults != NULL) {
                    tinypy_release(defaults);
                }
                tinypy_release(closure);
                tinypy_release(function_code);
                __tinypy_eval_push_owned(frame, created_function);
            }
            break;
        case TINYPY_OP_CALL_FUNCTION:
        case TINYPY_OP_CALL_FUNCTION_VAR:
        case TINYPY_OP_CALL_FUNCTION_KW:
        case TINYPY_OP_CALL_FUNCTION_VAR_KW:
            {
                int has_varargs = instruction.opcode == TINYPY_OP_CALL_FUNCTION_VAR || instruction.opcode == TINYPY_OP_CALL_FUNCTION_VAR_KW;
                int has_var_keywords = instruction.opcode == TINYPY_OP_CALL_FUNCTION_KW || instruction.opcode == TINYPY_OP_CALL_FUNCTION_VAR_KW;
                tinypy_value_t *call_result = __tinypy_eval_call_stack(frame, argument, has_varargs, has_var_keywords, out_error);

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
        case TINYPY_OP_FOR_ITER:
            {
                tinypy_error_t *iteration_error = NULL;
                tinypy_value_t *iterator = __tinypy_eval_peek(frame, 1U);
                tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

                if (item != NULL) {
                    __tinypy_eval_push_owned(frame, item);
                } else if (iteration_error != NULL) {
                    if (out_error != NULL) {
                        *out_error = iteration_error;
                    } else {
                        tinypy_error_release(iteration_error);
                    }
                    reason = TINYPY_EVAL_REASON_EXCEPTION;
                } else {
                    tinypy_release(__tinypy_eval_pop_owned(frame));
                    assert(instruction_offset <= SIZE_MAX - argument);
                    instruction_offset += argument;
                }
            }
            break;
        case TINYPY_OP_JUMP_ABSOLUTE:
            instruction_offset = argument;
            break;
        case TINYPY_OP_POP_JUMP_IF_FALSE:
        case TINYPY_OP_POP_JUMP_IF_TRUE:
            {
                tinypy_value_t *value = __tinypy_eval_pop_owned(frame);
                int32_t truth = tinypy_truth(value, out_error);
                tinypy_release(value);
                if (truth < 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
                else if ((instruction.opcode == TINYPY_OP_POP_JUMP_IF_TRUE && truth != 0) || (instruction.opcode == TINYPY_OP_POP_JUMP_IF_FALSE && truth == 0)) {
                    instruction_offset = argument;
                }
            }
            break;
        case TINYPY_OP_JUMP_IF_FALSE_OR_POP:
        case TINYPY_OP_JUMP_IF_TRUE_OR_POP:
            {
                tinypy_value_t *value = __tinypy_eval_peek(frame, 1U);
                int32_t truth = tinypy_truth(value, out_error);
                int jump = instruction.opcode == TINYPY_OP_JUMP_IF_TRUE_OR_POP ? truth != 0 : truth == 0;
                if (truth < 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
                else if (jump != 0) {
                    instruction_offset = argument;
                } else {
                    tinypy_release(__tinypy_eval_pop_owned(frame));
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
            if (__tinypy_eval_setup_with(frame, instruction_offset + argument, out_error) == 0) reason = TINYPY_EVAL_REASON_EXCEPTION;
            break;
        case TINYPY_OP_WITH_CLEANUP:
            reason = __tinypy_eval_with_cleanup(frame, out_error);
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
            result = tinypy_integer_from_i64(vm, (int64_t)argument);
            reason = TINYPY_EVAL_REASON_CONTINUE;
            break;
        case TINYPY_OP_RAISE_VARARGS:
            reason = __tinypy_eval_raise(frame, argument, out_error);
            break;
        case TINYPY_OP_END_FINALLY:
            reason = __tinypy_eval_end_finally(frame, &result, out_error);
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

unwind_reason:
        if (reason != TINYPY_EVAL_REASON_NOT && reason != TINYPY_EVAL_REASON_YIELD && __tinypy_eval_unwind_reason(frame, &reason, &instruction_offset, &result, out_error) != 0) continue;
    }

    if (reason == TINYPY_EVAL_REASON_NOT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "bytecode ended without RETURN_VALUE", out_error);
        reason = TINYPY_EVAL_REASON_EXCEPTION;
        tinypy_internal_traceback_here(vm, frame);
    }
    if (reason == TINYPY_EVAL_REASON_EXCEPTION) {
        if (result != NULL) {
            tinypy_release(result);
            result = NULL;
        }
        if (out_error != NULL && *out_error == NULL) tinypy_internal_exception_make_diagnostic(vm, out_error);
    }
    if (generator_execution != 0 && reason == TINYPY_EVAL_REASON_YIELD) {
        generator->instruction_offset = instruction_offset;
        generator->handled_type = vm->handled_type;
        generator->handled_value = vm->handled_value;
        generator->handled_traceback = vm->handled_traceback;
        vm->handled_type = NULL;
        vm->handled_value = NULL;
        vm->handled_traceback = NULL;
        if (out_yielded != NULL) *out_yielded = 1;
    }
    vm->evaluation_depth -= 1U;
    vm->current_frame = frame->back != NULL ? TINYPY_FRAME_OBJECT(frame->back) : NULL;
    if (reason != TINYPY_EVAL_REASON_YIELD || generator_execution == 0) tinypy_internal_exception_clear_handled(vm);
    vm->handled_type = frame->previous_handled_type;
    vm->handled_value = frame->previous_handled_value;
    vm->handled_traceback = frame->previous_handled_traceback;
    frame->previous_handled_type = NULL;
    frame->previous_handled_value = NULL;
    frame->previous_handled_traceback = NULL;
    if (generator_execution != 0) {
        if (frame->back != NULL) {
            tinypy_release(frame->back);
            frame->back = NULL;
        }
        if (reason != TINYPY_EVAL_REASON_YIELD) {
            __tinypy_eval_unwind_stack(frame, 0U);
            __tinypy_eval_clear_local_slots(frame);
        }
    } else {
        __tinypy_eval_unwind_stack(frame, 0U);
        __tinypy_eval_clear_local_slots(frame);
        tinypy_release(frame_value);
    }
    return result;
}

tinypy_value_t *tinypy_eval_code(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals, tinypy_error_t **out_error)
{
    tinypy_internal_exception_clear_raised(tinypy_internal_value_vm(code));
    return __tinypy_eval_code_bound(code, globals, locals, NULL, NULL, NULL, NULL, NULL, NULL, NULL, out_error);
}

tinypy_value_t *tinypy_exec_code(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm;
    tinypy_value_t *result;

    assert(code != NULL);
    assert(tinypy_internal_value_kind(code) == TINYPY_VALUE_CODE);
    vm = tinypy_internal_value_vm(code);
    assert(tinypy_internal_vm_valid(vm));
    assert(globals != NULL);
    assert(tinypy_internal_value_belongs_to(vm, globals));
    assert(locals == NULL || tinypy_internal_value_belongs_to(vm, locals));
    result = tinypy_eval_code(code, globals, locals, out_error);
    if (result == NULL) return NULL;
    tinypy_release(result);
    return tinypy_none_get(vm);
}

tinypy_value_t *tinypy_internal_eval_function(tinypy_value_t *function_value, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_function_object_t *function;
    tinypy_value_t *locals;
    tinypy_value_t *result;

    assert(function_value != NULL);
    assert(tinypy_internal_value_kind(function_value) == TINYPY_VALUE_FUNCTION);
    function = TINYPY_FUNCTION_OBJECT(function_value);
    locals = tinypy_dict_new(tinypy_internal_value_vm(function_value));
    if ((tinypy_code_flags(function->code) & TINYPY_CODE_GENERATOR) != 0) {
        tinypy_value_t *frame_value = tinypy_frame_new(function->code, function->globals, locals);
        tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(frame_value);

        if (__tinypy_eval_bind_arguments(frame, function, args, kwargs, out_error) == 0) {
            tinypy_release(frame_value);
            tinypy_release(locals);
            return NULL;
        }
        __tinypy_eval_initialize_cells(frame, function);
        if (frame->back != NULL) {
            tinypy_release(frame->back);
            frame->back = NULL;
        }
        if (frame->previous_handled_type != NULL) tinypy_release(frame->previous_handled_type);
        if (frame->previous_handled_value != NULL) tinypy_release(frame->previous_handled_value);
        if (frame->previous_handled_traceback != NULL) tinypy_release(frame->previous_handled_traceback);
        frame->previous_handled_type = NULL;
        frame->previous_handled_value = NULL;
        frame->previous_handled_traceback = NULL;
        result = tinypy_internal_generator_from_frame(frame_value);
        tinypy_release(frame_value);
    } else {
        result = __tinypy_eval_code_bound(function->code, function->globals, locals, function, args, kwargs, NULL, NULL, NULL, NULL, out_error);
    }
    tinypy_release(locals);
    return result;
}

tinypy_value_t *tinypy_internal_eval_generator_resume(tinypy_generator_object_t *generator, tinypy_value_t *send_value, tinypy_value_t *throw_value, tinypy_value_t *throw_traceback, int *out_yielded, tinypy_error_t **out_error)
{
    assert(generator != NULL);
    assert(send_value != NULL);
    assert(throw_value == NULL || tinypy_internal_value_belongs_to(tinypy_internal_value_vm(&generator->base), throw_value));
    assert(throw_traceback == NULL || tinypy_internal_value_belongs_to(tinypy_internal_value_vm(&generator->base), throw_traceback));
    return __tinypy_eval_code_bound(NULL, NULL, NULL, NULL, send_value, NULL, generator, throw_value, throw_traceback, out_yielded, out_error);
}
