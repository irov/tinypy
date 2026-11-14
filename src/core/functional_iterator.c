#include "internal.h"

//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_functional_iterator_kind_e {
    TINYPY_FUNCTIONAL_ITERATOR_ZIP,
    TINYPY_FUNCTIONAL_ITERATOR_MAP,
    TINYPY_FUNCTIONAL_ITERATOR_FILTER
} tinypy_functional_iterator_kind_e;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_functional_iterator_payload_t {
    tinypy_value_t *iterators;
    tinypy_value_t *callable;
    tinypy_value_t *exhausted;
} tinypy_functional_iterator_payload_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_chain_payload_t {
    tinypy_value_t *source;
    tinypy_value_t *active;
} tinypy_chain_payload_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_iterator_slice_payload_t {
    tinypy_value_t *source;
    int64_t next;
    int64_t stop;
    int64_t step;
    uint64_t consumed;
} tinypy_iterator_slice_payload_t;
//////////////////////////////////////////////////////////////////////////
static void __tinypy_functional_iterator_clear(tinypy_value_t **slot) {
    tinypy_value_t *value = *slot;

    if (value == NULL) {
        return;
    }
    *slot = NULL;
    TINYPY_DECREF(value);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_functional_iterator_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_functional_iterator_payload_t *payload = (tinypy_functional_iterator_payload_t *)tinypy_native_instance_payload(value);

    tinypy_internal_native_instance_release_references(value, visit, user_data);
    if (payload->iterators != NULL) {
        visit(payload->iterators, user_data);
    }
    if (payload->callable != NULL) {
        visit(payload->callable, user_data);
    }
    if (payload->exhausted != NULL) {
        visit(payload->exhausted, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_functional_iterator_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_functional_iterator_kind_e kind = (tinypy_functional_iterator_kind_e)(intptr_t)type->native_spec.user_data;
    tinypy_value_t *name = kind == TINYPY_FUNCTIONAL_ITERATOR_ZIP ? vm->internal_xzip_key : (kind == TINYPY_FUNCTIONAL_ITERATOR_MAP ? vm->internal_xmap_key : vm->internal_xfilter_key);
    size_t argument_count = TINYPY_TUPLE_SIZE(args);

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_TEXT(name),
            TINYPY_MESSAGE_PART_LITERAL("() does not take keyword arguments")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (kind == TINYPY_FUNCTIONAL_ITERATOR_MAP && argument_count < 2U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "xmap() must have at least two arguments.", out_error);
        return NULL;
    }
    if (kind == TINYPY_FUNCTIONAL_ITERATOR_FILTER && argument_count != 2U) {
        char count_text[TINYPY_MESSAGE_SIZE_BUFFER];
        size_t count_size = tinypy_internal_format_size(count_text, argument_count);
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("xfilter expected 2 arguments, got "),
            {count_text, count_size}
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    size_t offset = kind == TINYPY_FUNCTIONAL_ITERATOR_ZIP ? 0U : 1U;
    size_t iterator_count = argument_count - offset;
    tinypy_value_t *iterators = tinypy_internal_tuple_new_checked(vm, iterator_count, out_error);
    if (iterators == NULL) {
        return NULL;
    }
    for (size_t index = 0U; index < iterator_count; ++index) {
        tinypy_value_t *source = TINYPY_TUPLE_GET(args, index + offset);
        tinypy_value_t *iterator = tinypy_iter(source, out_error);
        if (iterator == NULL) {
            TINYPY_DECREF(iterators);
            return NULL;
        }
        tinypy_tuple_set(iterators, index, iterator);
        TINYPY_DECREF(iterator);
    }
    tinypy_value_t *instance = tinypy_internal_object_allocate_checked(vm, type, type->basic_size, out_error);
    if (instance == NULL) {
        TINYPY_DECREF(iterators);
        return NULL;
    }
    tinypy_functional_iterator_payload_t *payload = (tinypy_functional_iterator_payload_t *)tinypy_native_instance_payload(instance);

    payload->iterators = iterators;
    if (offset != 0U) {
        tinypy_value_t *callable = TINYPY_TUPLE_GET(args, 0U);

        payload->callable = TINYPY_RET(callable);
    }
    if (kind == TINYPY_FUNCTIONAL_ITERATOR_MAP) {
        payload->exhausted = tinypy_internal_tuple_new_checked(vm, iterator_count, out_error);
        if (payload->exhausted == NULL) {
            TINYPY_DECREF(instance);
            return NULL;
        }
    }
    return instance;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_functional_iterator_iter(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)payload;
    (void)user_data;
    (void)out_error;
    return TINYPY_RET(instance);
}
//////////////////////////////////////////////////////////////////////////
/* Each call owns its row, including during callbacks that advance the same
   iterator again. No partially filled tuple is shared with another call. */
static tinypy_value_t *__tinypy_functional_iterator_row(tinypy_vm_t *vm, tinypy_value_t *iterators, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(iterators);

    if (count == 0U) {
        return NULL;
    }
    tinypy_value_t *row = tinypy_internal_tuple_new_checked(vm, count, out_error);
    if (row == NULL) {
        return NULL;
    }
    for (size_t index = 0U; index < count; ++index) {
        tinypy_value_t *iterator = TINYPY_TUPLE_GET(iterators, index);
        tinypy_value_t *item = tinypy_next(iterator, out_error);
        if (item == NULL) {
            TINYPY_DECREF(row);
            return NULL;
        }
        tinypy_tuple_set(row, index, item);
        TINYPY_DECREF(item);
    }
    return row;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xzip_next(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    tinypy_functional_iterator_payload_t *iterator = (tinypy_functional_iterator_payload_t *)payload;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);

    (void)user_data;
    tinypy_value_t *row = __tinypy_functional_iterator_row(vm, iterator->iterators, out_error);
    return row;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xmap_next(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    tinypy_functional_iterator_payload_t *iterator = (tinypy_functional_iterator_payload_t *)payload;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);

    (void)user_data;
    size_t count = TINYPY_TUPLE_SIZE(iterator->iterators);
    tinypy_value_t *local_items[8];
    tinypy_value_t **items = local_items;

    if (count > sizeof(local_items) / sizeof(local_items[0])) {
        items = (tinypy_value_t **)__tinypy_internal_vm_allocate_checked(vm, count * sizeof(*items), out_error);
        if (items == NULL) {
            return NULL;
        }
    }
    size_t index = 0U;
    tinypy_value_t *result = NULL;
    tinypy_bool_t have_item = TINYPY_FALSE;

    for (; index < count; ++index) {
        if (TINYPY_TUPLE_GET(iterator->exhausted, index) == &vm->true_object.base) {
            items[index] = TINYPY_RET_NONE(vm);
            continue;
        }
        tinypy_value_t *source = TINYPY_TUPLE_GET(iterator->iterators, index);
        tinypy_error_t *iteration_error = NULL;
        items[index] = tinypy_next(source, &iteration_error);
        if (items[index] == NULL) {
            if (tinypy_internal_iteration_failed(vm, iteration_error, out_error) != TINYPY_FALSE) {
                goto cleanup;
            }
            tinypy_tuple_set(iterator->exhausted, index, &vm->true_object.base);
            items[index] = TINYPY_RET_NONE(vm);
            continue;
        }
        have_item = TINYPY_TRUE;
    }
    if (have_item == TINYPY_FALSE) {
        goto cleanup;
    }
    if (TINYPY_VALUE_KIND(iterator->callable) == TINYPY_VALUE_NONE) {
        result = count == 1U ? TINYPY_RET(items[0U]) : tinypy_internal_tuple_from_items_checked(vm, items, count, out_error);
        goto cleanup;
    }
    result = tinypy_internal_object_call_items(iterator->callable, items, count, NULL, out_error);

cleanup:
    while (index != 0U) {
        TINYPY_DECREF(items[--index]);
    }
    if (items != local_items) {
        tinypy_internal_vm_deallocate(vm, items, count * sizeof(*items));
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xfilter_next(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    tinypy_functional_iterator_payload_t *iterator = (tinypy_functional_iterator_payload_t *)payload;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    tinypy_value_t *source = TINYPY_TUPLE_GET(iterator->iterators, 0U);
    tinypy_bool_t call_predicate = TINYPY_VALUE_KIND(iterator->callable) != TINYPY_VALUE_NONE ? TINYPY_TRUE : TINYPY_FALSE;

    (void)user_data;
    for (;;) {
        if (tinypy_internal_eval_poll_interrupt(vm, out_error) == TINYPY_FALSE) {
            return NULL;
        }
        tinypy_value_t *item = tinypy_next(source, out_error);
        if (item == NULL) {
            return NULL;
        }
        tinypy_value_t *selected = item;
        if (call_predicate != TINYPY_FALSE) {
            selected = tinypy_internal_object_call_items(iterator->callable, &item, 1U, NULL, out_error);
            if (selected == NULL) {
                TINYPY_DECREF(item);
                return NULL;
            }
        }
        int32_t truth = tinypy_truth(selected, out_error);

        if (call_predicate != TINYPY_FALSE) {
            TINYPY_DECREF(selected);
        }
        if (truth < 0) {
            TINYPY_DECREF(item);
            return NULL;
        }
        if (truth != 0) {
            return item;
        }
        TINYPY_DECREF(item);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_xchain_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_chain_payload_t *payload = (tinypy_chain_payload_t *)tinypy_native_instance_payload(value);

    tinypy_internal_native_instance_release_references(value, visit, user_data);
    if (payload->active != NULL) {
        visit(payload->active, user_data);
    }
    if (payload->source != NULL) {
        visit(payload->source, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xchain_from_source(tinypy_type_t *type, tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_value_t *iterator = tinypy_iter(source, out_error);
    if (iterator == NULL) {
        return NULL;
    }
    tinypy_value_t *instance = tinypy_internal_object_allocate_checked(type->vm, type, type->basic_size, out_error);
    if (instance == NULL) {
        TINYPY_DECREF(iterator);
        return NULL;
    }
    tinypy_chain_payload_t *payload = (tinypy_chain_payload_t *)tinypy_native_instance_payload(instance);

    payload->source = iterator;
    return instance;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xchain_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(type->vm, TINYPY_ERROR_TYPE, "xchain() does not take keyword arguments", out_error);
        return NULL;
    }
    tinypy_value_t *result = __tinypy_xchain_from_source(type, args, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xchain_from_iterable(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_type_t *owner = (tinypy_type_t *)user_data;
    tinypy_value_t *receiver = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(receiver) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(owner->vm, TINYPY_ERROR_TYPE, "xchain.from_iterable() requires an xchain type", out_error);
        return NULL;
    }
    tinypy_type_t *type = (tinypy_type_t *)receiver;
    if (tinypy_type_is_subtype(type, owner) == TINYPY_FALSE) {
        tinypy_internal_make_vm_error(owner->vm, TINYPY_ERROR_TYPE, "xchain.from_iterable() requires an xchain type", out_error);
        return NULL;
    }
    tinypy_value_t *source = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *result = __tinypy_xchain_from_source(type, source, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xchain_next(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    tinypy_chain_payload_t *chain = (tinypy_chain_payload_t *)payload;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);

    (void)user_data;
    while (chain->source != NULL) {
        if (tinypy_internal_eval_poll_interrupt(vm, out_error) == TINYPY_FALSE) {
            return NULL;
        }
        if (chain->source == NULL) {
            return NULL;
        }
        tinypy_value_t *active;
        if (chain->active == NULL) {
            tinypy_value_t *source = TINYPY_RET(chain->source);
            tinypy_value_t *iterable = tinypy_next(source, out_error);

            TINYPY_DECREF(source);
            if (iterable == NULL) {
                __tinypy_functional_iterator_clear(&chain->source);
                return NULL;
            }
            active = tinypy_iter(iterable, out_error);

            TINYPY_DECREF(iterable);
            if (active == NULL) {
                __tinypy_functional_iterator_clear(&chain->source);
                return NULL;
            }
            tinypy_value_t *previous = chain->active;

            chain->active = active;
            TINYPY_INCREF(active);
            if (previous != NULL) {
                TINYPY_DECREF(previous);
            }
        }
        else {
            active = TINYPY_RET(chain->active);
        }
        /* A callback may exhaust the chain while this call is suspended. */
        tinypy_value_t *item = tinypy_next(active, out_error);

        if (item != NULL) {
            TINYPY_DECREF(active);
            return item;
        }
        if (vm->raised_value != NULL || (out_error != NULL && *out_error != NULL)) {
            TINYPY_DECREF(active);
            return NULL;
        }
        if (chain->active == active) {
            __tinypy_functional_iterator_clear(&chain->active);
        }
        TINYPY_DECREF(active);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_xslice_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_iterator_slice_payload_t *payload = (tinypy_iterator_slice_payload_t *)tinypy_native_instance_payload(value);

    tinypy_internal_native_instance_release_references(value, visit, user_data);
    if (payload->source != NULL) {
        visit(payload->source, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
/* Python 2 islice uses PyInt_AsSsize_t, including __int__ and float
   truncation. Conversion failures become the argument's ValueError. */
static int64_t __tinypy_xslice_argument(tinypy_value_t *value, int64_t default_value) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE) {
        return default_value;
    }
    int64_t result;
    tinypy_error_t *error = NULL;
    if (tinypy_internal_number_as_ssize(value, &result, &error) == TINYPY_FALSE) {
        if (error != NULL) {
            tinypy_error_release(error);
        }
        tinypy_vm_clear_error(TINYPY_VALUE_VM(value));
        return INT64_C(-1);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xslice_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "xslice() does not take keyword arguments", out_error);
        return NULL;
    }
    if (count < 2U || count > 4U) {
        tinypy_value_t *name = vm->internal_xslice_key;

        tinypy_internal_make_arity_error(vm, (const char *)TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name), count, 2U, 4U, TINYPY_ARITY_STYLE_UNPACK, out_error);
        return NULL;
    }
    int64_t start = INT64_C(0);
    if (count > 2U) {
        tinypy_value_t *argument = TINYPY_TUPLE_GET(args, 1U);

        start = __tinypy_xslice_argument(argument, INT64_C(0));
    }
    tinypy_value_t *stop_argument = TINYPY_TUPLE_GET(args, count == 2U ? 1U : 2U);
    int64_t stop = __tinypy_xslice_argument(stop_argument, INT64_C(-1));
    if (stop == INT64_C(-1) && TINYPY_VALUE_KIND(stop_argument) != TINYPY_VALUE_NONE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Stop argument for xslice() must be None or an integer: 0 <= x <= maxint.", out_error);
        return NULL;
    }
    if (start < 0 || stop < INT64_C(-1)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Indices for xslice() must be None or an integer: 0 <= x <= maxint.", out_error);
        return NULL;
    }
    int64_t step = INT64_C(1);
    if (count == 4U) {
        tinypy_value_t *argument = TINYPY_TUPLE_GET(args, 3U);

        step = __tinypy_xslice_argument(argument, INT64_C(1));
    }
    if (step < 1) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Step for xslice() must be a positive integer or None.", out_error);
        return NULL;
    }
    tinypy_value_t *iterable = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *source = tinypy_iter(iterable, out_error);
    if (source == NULL) {
        return NULL;
    }
    tinypy_value_t *instance = tinypy_internal_object_allocate_checked(vm, type, type->basic_size, out_error);
    if (instance == NULL) {
        TINYPY_DECREF(source);
        return NULL;
    }
    tinypy_iterator_slice_payload_t *payload = (tinypy_iterator_slice_payload_t *)tinypy_native_instance_payload(instance);

    payload->source = source;
    payload->next = start;
    payload->stop = stop;
    payload->step = step;
    return instance;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xslice_next(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    tinypy_iterator_slice_payload_t *slice = (tinypy_iterator_slice_payload_t *)payload;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);

    (void)user_data;
    if (slice->source == NULL) {
        return NULL;
    }
    tinypy_value_t *source = TINYPY_RET(slice->source);
    tinypy_value_t *item;

    while (slice->next >= 0 && slice->consumed < (uint64_t)slice->next) {
        if (tinypy_internal_eval_poll_interrupt(vm, out_error) == TINYPY_FALSE) {
            TINYPY_DECREF(source);
            return NULL;
        }
        item = tinypy_next(source, out_error);
        if (item == NULL) {
            goto empty;
        }
        TINYPY_DECREF(item);
        slice->consumed += 1U;
    }
    if (slice->stop >= 0 && slice->consumed >= (uint64_t)slice->stop) {
        goto empty;
    }
    item = tinypy_next(source, out_error);
    if (item == NULL) {
        goto empty;
    }
    slice->consumed += 1U;
    slice->next = slice->next > INT64_MAX - slice->step ? slice->stop : slice->next + slice->step;
    if (slice->stop >= 0 && slice->next > slice->stop) {
        slice->next = slice->stop;
    }
    TINYPY_DECREF(source);
    return item;

empty:
    __tinypy_functional_iterator_clear(&slice->source);
    TINYPY_DECREF(source);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_functional_iterator_iter_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)function;
    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    return TINYPY_RET(self);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_functional_iterator_next_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_type_t *owner = (tinypy_type_t *)tinypy_native_function_user_data(function);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
    void *payload = tinypy_native_instance_payload(self);

    (void)items;
    (void)count;
    (void)kwargs;
    tinypy_value_t *result = owner->native_spec.next(self, payload, owner->native_spec.user_data, out_error);
    if (result == NULL && vm->raised_value == NULL && (out_error == NULL || *out_error == NULL)) {
        tinypy_internal_exception_raise_stop_iteration(vm, out_error);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_functional_iterators(tinypy_vm_t *vm) {
    tinypy_value_t *const names[] = {vm->internal_xzip_key, vm->internal_xmap_key, vm->internal_xfilter_key, vm->internal_xchain_key, vm->internal_xslice_key};
    static const struct {
        size_t payload_size;
        tinypy_new_slot_t create;
        tinypy_release_references_slot_t release_references;
        tinypy_native_unary_t next;
    } definitions[] = {
        {sizeof(tinypy_functional_iterator_payload_t), __tinypy_functional_iterator_create, __tinypy_functional_iterator_release_references, __tinypy_xzip_next},
        {sizeof(tinypy_functional_iterator_payload_t), __tinypy_functional_iterator_create, __tinypy_functional_iterator_release_references, __tinypy_xmap_next},
        {sizeof(tinypy_functional_iterator_payload_t), __tinypy_functional_iterator_create, __tinypy_functional_iterator_release_references, __tinypy_xfilter_next},
        {sizeof(tinypy_chain_payload_t), __tinypy_xchain_create, __tinypy_xchain_release_references, __tinypy_xchain_next},
        {sizeof(tinypy_iterator_slice_payload_t), __tinypy_xslice_create, __tinypy_xslice_release_references, __tinypy_xslice_next}
    };

    for (size_t index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        tinypy_native_type_spec_t spec;

        tinypy_native_type_spec_init(&spec);
        spec.payload_size = definitions[index].payload_size;
        spec.user_data = (void *)(intptr_t)index;
        spec.iter = __tinypy_functional_iterator_iter;
        spec.next = definitions[index].next;
        spec.has_instance_dict = TINYPY_FALSE;
        spec.has_weakrefs = TINYPY_FALSE;
        tinypy_type_t *type = tinypy_internal_native_type_new_key(names[index], NULL, 0U, NULL, &spec, NULL);

        type->create = definitions[index].create;
        type->release_references = definitions[index].release_references;
        type->traverse_references = definitions[index].release_references;
        tinypy_internal_constructor_add_builtin_new(type);
        tinypy_internal_type_add_object_getattribute_method(type);
        tinypy_internal_type_add_items_method(type, vm->internal_special_iter_key, __tinypy_functional_iterator_iter_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER);
        tinypy_internal_type_add_items_method(type, vm->internal_special_next_key, __tinypy_functional_iterator_next_method, type, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER);
        if (names[index] == vm->internal_xchain_key) {
            tinypy_internal_type_add_class_method(type, vm->internal_from_iterable_key, __tinypy_xchain_from_iterable, type, NULL);
        }
        type->flags |= TINYPY_TYPE_FLAG_IMMUTABLE;
        tinypy_value_t *type_value = &type->base.base;

        tinypy_dict_set(vm->builtins, names[index], type_value);
        TINYPY_DECREF(type_value);
    }
}
