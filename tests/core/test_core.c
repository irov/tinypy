#include "tinypy/tinypy.h"
#include "../../src/core/internal.h"

#include <float.h>
#include <math.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef char test_double_size_must_match_uint64_t[sizeof(double) == sizeof(uint64_t) ? 1 : -1];
//////////////////////////////////////////////////////////////////////////
typedef union test_allocation_header_t {
    struct {
        size_t size;
        size_t alignment;
    } fields;
    void *pointer_alignment;
    void (*function_alignment)(void);
    int64_t integer_alignment;
    long double floating_alignment;
} test_allocation_header_t;
//////////////////////////////////////////////////////////////////////////
typedef struct test_allocator_state_t {
    size_t allocation_calls;
    size_t deallocation_calls;
    size_t outstanding_allocations;
    size_t outstanding_bytes;
    size_t last_allocation_size;
    size_t fail_allocation_above;
    /* With fail_allocation_above, only the requests below this size fail. */
    size_t fail_allocation_below;
    size_t fail_allocation_size;
    size_t fail_allocation_call;
} test_allocator_state_t;

#define TEST_CHECK(condition)                \
    do { \
        if (!(condition)) { \
            (void)fprintf(                   \
                stderr,                      \
                "%s:%d: check failed: %s\n", \
                __FILE__,                    \
                __LINE__,                    \
                #condition);                 \
            return 1;                        \
        }                                    \
    } while (0)
//////////////////////////////////////////////////////////////////////////
static void *__test_allocate(void *user_data, size_t size, size_t alignment) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;
    test_allocation_header_t *header;

    state->allocation_calls += 1U;
    state->last_allocation_size = size;

    if ((state->fail_allocation_above != 0U && size > state->fail_allocation_above && (state->fail_allocation_below == 0U || size < state->fail_allocation_below)) || (state->fail_allocation_size != 0U && size == state->fail_allocation_size) || state->allocation_calls == state->fail_allocation_call || size > SIZE_MAX - sizeof(*header)) {
        return NULL;
    }

    header = (test_allocation_header_t *)malloc(sizeof(*header) + size);
    if (header == NULL) {
        return NULL;
    }

    header->fields.size = size;
    header->fields.alignment = alignment;
    state->outstanding_allocations += 1U;
    state->outstanding_bytes += size;
    return (void *)(header + 1);
}
//////////////////////////////////////////////////////////////////////////
static void *__test_reallocate(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;
    test_allocation_header_t *header;
    test_allocation_header_t *resized;

    if (memory == NULL) {
        void *return_value_1 = __test_allocate(user_data, new_size, alignment);
        return return_value_1;
    }

    state->allocation_calls += 1U;
    header = ((test_allocation_header_t *)memory) - 1;
    state->last_allocation_size = new_size;
    if (header->fields.size != old_size || header->fields.alignment != alignment || (state->fail_allocation_above != 0U && new_size > state->fail_allocation_above && (state->fail_allocation_below == 0U || new_size < state->fail_allocation_below)) || (state->fail_allocation_size != 0U && new_size == state->fail_allocation_size) || state->allocation_calls == state->fail_allocation_call || new_size > SIZE_MAX - sizeof(*header)) {
        return NULL;
    }

    resized = (test_allocation_header_t *)realloc(
        header,
        sizeof(*header) + new_size);
    if (resized == NULL) {
        return NULL;
    }

    state->outstanding_bytes -= old_size;
    state->outstanding_bytes += new_size;
    resized->fields.size = new_size;
    resized->fields.alignment = alignment;
    return (void *)(resized + 1);
}
//////////////////////////////////////////////////////////////////////////
static void __test_deallocate(void *user_data, void *memory, size_t size, size_t alignment) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;
    test_allocation_header_t *header;

    if (memory == NULL) {
        return;
    }

    header = ((test_allocation_header_t *)memory) - 1;
    if (header->fields.size != size || header->fields.alignment != alignment) {
        (void)fprintf(stderr, "allocator size mismatch\n");
        abort();
    }

    state->deallocation_calls += 1U;
    state->outstanding_allocations -= 1U;
    state->outstanding_bytes -= size;
    free(header);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_allocator_t __test_make_allocator(test_allocator_state_t *state) {
    tinypy_allocator_t allocator;

    (void)memset(&allocator, 0, sizeof(allocator));
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.user_data = state;
    allocator.allocate = __test_allocate;
    allocator.reallocate = __test_reallocate;
    allocator.deallocate = __test_deallocate;
    return allocator;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_vm_config_t __test_make_config(const tinypy_allocator_t *allocator) {
    tinypy_vm_config_t config;

    (void)memset(&config, 0, sizeof(config));
    config.abi_version = TINYPY_ABI_VERSION;
    config.struct_size = (uint32_t)sizeof(config);
    config.allocator = allocator;
    return config;
}
//////////////////////////////////////////////////////////////////////////
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
typedef struct test_cycle_diagnostic_state_t {
    char text[32768];
    size_t size;
    size_t message_count;
} test_cycle_diagnostic_state_t;
//////////////////////////////////////////////////////////////////////////
static void __test_cycle_diagnostic(void *user_data, const tinypy_diagnostic_t *diagnostic) {
    test_cycle_diagnostic_state_t *state = (test_cycle_diagnostic_state_t *)user_data;
    size_t available;
    size_t copy_size;

    if (diagnostic == NULL) {
        return;
    }
    state->message_count += 1U;
    available = sizeof(state->text) - state->size - 1U;
    copy_size = diagnostic->message_size < available ? diagnostic->message_size : available;
    if (copy_size != 0U) {
        (void)memcpy(state->text + state->size, diagnostic->message, copy_size);
        state->size += copy_size;
    }
    if (state->size + 1U < sizeof(state->text)) {
        state->text[state->size] = '\n';
        state->size += 1U;
    }
    state->text[state->size] = '\0';
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_cycle_diagnostics(void) {
    static const char source[] =
        "list_cycle = []\n"
        "list_cycle.append(list_cycle)\n"
        "dict_cycle = {}\n"
        "dict_cycle['self'] = dict_cycle\n"
        "def make_cycle():\n"
        "    value = None\n"
        "    def closure():\n"
        "        return value\n"
        "    value = closure\n"
        "    return closure\n"
        "cell_cycle = make_cycle()\n";
    test_allocator_state_t allocator_state;
    test_cycle_diagnostic_state_t diagnostic_state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_value_t *code;
    tinypy_value_t *globals;
    tinypy_value_t *result;
    tinypy_value_t *list;
    tinypy_value_t *dict;
    tinypy_value_t *function;
    tinypy_value_t *closure;
    tinypy_value_t *cell;
    tinypy_value_t *list_key;
    tinypy_value_t *internal_dict_key;
    tinypy_value_t *internal_function_key;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&diagnostic_state, 0, sizeof(diagnostic_state));
    allocator = __test_make_allocator(&allocator_state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    TEST_CHECK(vm->cycle_diagnostics == NULL);
    list = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_list_append(list, list);
    TEST_CHECK(tinypy_vm_report_cycles(vm, __test_cycle_diagnostic, &diagnostic_state) == 0U);
    TEST_CHECK(diagnostic_state.message_count == 0U);
    tinypy_list_clear(list);
    tinypy_release(list);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&diagnostic_state, 0, sizeof(diagnostic_state));
    allocator = __test_make_allocator(&allocator_state);
    config = __test_make_config(&allocator);
    config.cycle_diagnostics = 1;
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    TEST_CHECK(vm->cycle_diagnostics != NULL);
    TEST_CHECK(tinypy_vm_report_cycles(vm, __test_cycle_diagnostic, &diagnostic_state) == 0U);
    TEST_CHECK(diagnostic_state.message_count == 0U);

    tinypy_value_t *retained_module = tinypy_module_new(vm, "retained", 8U);
    tinypy_value_t *module_marker = tinypy_integer_from_i64(vm, INT64_C(17));
    tinypy_value_t *module_namespace = tinypy_module_dict(retained_module);
    tinypy_module_add_value(retained_module, "marker", 6U, module_marker);
    TEST_CHECK(tinypy_vm_report_cycles(vm, __test_cycle_diagnostic, &diagnostic_state) == 0U);
    TEST_CHECK(tinypy_module_dict(retained_module) == module_namespace);
    TEST_CHECK(tinypy_module_get_value(retained_module, "marker", 6U) == module_marker);
    tinypy_release(module_marker);
    tinypy_release(retained_module);

    list = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_list_append(list, list);
    TEST_CHECK(tinypy_vm_report_cycles(vm, NULL, NULL) == 0U);
    TEST_CHECK(tinypy_vm_report_cycles(vm, __test_cycle_diagnostic, &diagnostic_state) == 1U);
    TEST_CHECK(diagnostic_state.message_count >= 4U);
    TEST_CHECK(strstr(diagnostic_state.text, "[tinypy cycle] cycle 1") != NULL);
    TEST_CHECK(strstr(diagnostic_state.text, "candidate break site") != NULL);
    TEST_CHECK(strstr(diagnostic_state.text, "[0] -> object #") != NULL);

    tinypy_list_clear(list);
    tinypy_release(list);

    (void)memset(&diagnostic_state, 0, sizeof(diagnostic_state));
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    code = tinypy_compile_source(
        vm,
        source,
        sizeof(source) - 1U,
        "cycle.py",
        sizeof("cycle.py") - 1U,
        &options,
        &error);
    TEST_CHECK(code != NULL);
    TEST_CHECK(error == NULL);
    globals = tinypy_dict_new(vm);
    result = tinypy_eval_code(code, globals, NULL, &error);
    TEST_CHECK(result != NULL);
    TEST_CHECK(error == NULL);

    list_key = tinypy_string_from_bytes(vm, "list_cycle", sizeof("list_cycle") - 1U);
    internal_dict_key = tinypy_string_from_bytes(vm, "dict_cycle", sizeof("dict_cycle") - 1U);
    internal_function_key = tinypy_string_from_bytes(vm, "cell_cycle", sizeof("cell_cycle") - 1U);
    list = tinypy_dict_get(globals, list_key);
    dict = tinypy_dict_get(globals, internal_dict_key);
    function = tinypy_dict_get(globals, internal_function_key);
    tinypy_retain(list);
    tinypy_retain(dict);
    tinypy_retain(function);
    closure = tinypy_function_closure(function);
    TEST_CHECK(closure != NULL);
    TEST_CHECK(tinypy_tuple_size(closure) == 1U);
    cell = tinypy_tuple_get(closure, 0U);
    TEST_CHECK(tinypy_typeof(cell) == TINYPY_VALUE_CELL);
    tinypy_dict_clear(globals);

    TEST_CHECK(tinypy_vm_report_cycles(vm, __test_cycle_diagnostic, &diagnostic_state) >= 3U);
    TEST_CHECK(strstr(diagnostic_state.text, "[0] -> object #") != NULL);
    TEST_CHECK(strstr(diagnostic_state.text, "['self'] -> object #") != NULL);
    TEST_CHECK(strstr(diagnostic_state.text, ".cell_contents -> object #") != NULL);
    TEST_CHECK(strstr(diagnostic_state.text, "candidate break site at cycle.py:2 in <module>") != NULL);
    TEST_CHECK(strstr(diagnostic_state.text, "candidate break site at cycle.py:4 in <module>") != NULL);
    TEST_CHECK(strstr(diagnostic_state.text, "candidate break site at cycle.py:9 in make_cycle") != NULL);

    tinypy_list_clear(list);
    tinypy_dict_clear(dict);
    tinypy_cell_set(cell, NULL);
    tinypy_release(function);
    tinypy_release(dict);
    tinypy_release(list);
    tinypy_release(internal_function_key);
    tinypy_release(internal_dict_key);
    tinypy_release(list_key);
    tinypy_release(result);
    tinypy_release(globals);
    tinypy_release(code);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
#endif
//////////////////////////////////////////////////////////////////////////
static double __test_double_from_bits(uint64_t bits) {
    double value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}
//////////////////////////////////////////////////////////////////////////
static uint64_t __test_double_to_bits(double value) {
    uint64_t bits = UINT64_C(0);

    (void)memcpy(&bits, &value, sizeof(value));
    return bits;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_allocator_accounting(void) {
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_value_t *none_value = NULL;
    tinypy_value_t *bool_value = NULL;
    tinypy_value_t *integer_value = NULL;
    int64_t extracted = INT64_C(0);
    size_t base_allocations;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);

    TEST_CHECK(tinypy_abi_version() == TINYPY_ABI_VERSION);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    base_allocations = state.outstanding_allocations;
    TEST_CHECK(base_allocations != 0U);

    none_value = tinypy_none_get(vm);
    bool_value = tinypy_bool_from_i32(vm, INT32_C(1));
    TEST_CHECK(none_value != NULL);
    TEST_CHECK(bool_value != NULL);
    TEST_CHECK(tinypy_typeof(none_value) == TINYPY_VALUE_NONE);
    TEST_CHECK(tinypy_typeof(bool_value) == TINYPY_VALUE_BOOL);
    TEST_CHECK(tinypy_bool_as_i32(bool_value) == INT32_C(1));
    TEST_CHECK(tinypy_integer_as_i64(bool_value) == INT64_C(1));
    TEST_CHECK(state.outstanding_allocations == base_allocations);

    integer_value = tinypy_integer_from_i64(vm, INT64_C(1234567));
    TEST_CHECK(tinypy_typeof(integer_value) == TINYPY_VALUE_INTEGER);
    extracted = tinypy_integer_as_i64(integer_value);
    TEST_CHECK(extracted == INT64_C(1234567));

    tinypy_release(integer_value);
    TEST_CHECK(state.outstanding_allocations == base_allocations);
    tinypy_release(none_value);
    tinypy_release(bool_value);

    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    TEST_CHECK(state.deallocation_calls == state.allocation_calls);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* Makes one-item tuples, which have no free list, until the pool takes
   memory from the host for a new arena, and returns how many it made. */
static size_t __test_pool_fill_arena(tinypy_vm_t *vm, const test_allocator_state_t *state, tinypy_value_t **values, size_t capacity) {
    tinypy_value_t *items[1];
    size_t allocation_calls = state->allocation_calls;
    size_t count = 0U;

    items[0] = tinypy_none_get(vm);
    while (count < capacity && state->allocation_calls == allocation_calls) {
        values[count] = tinypy_tuple_from_items(vm, items, 1U);
        count += 1U;
    }
    tinypy_release(items[0]);
    return count;
}
//////////////////////////////////////////////////////////////////////////
/* The pool keeps the last arena that becomes wholly free and returns the
   others to the host; under a heap budget it keeps none. */
static int32_t __test_pool_allocator_budget(size_t max_heap_bytes) {
    const size_t value_count = 20000U;
    const size_t pass_count = 4U;
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm;
    tinypy_value_t **values;
    tinypy_value_t *items[1];
    size_t base_allocations;
    size_t kept_allocations;
    size_t allocation_calls;
    size_t deallocation_calls;
    size_t pooled_allocation_calls;
    size_t count;
    size_t index;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    config.max_heap_bytes = max_heap_bytes;
    kept_allocations = max_heap_bytes == 0U ? 1U : 0U;
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    values = (tinypy_value_t **)malloc(value_count * sizeof(*values));
    TEST_CHECK(values != NULL);
    /* The integer free list keeps the first values released; fill it first so
       the values below return to the pool wherever the bootstrap left room. */
    for (index = 0U; index < TINYPY_INTEGER_FREE_LIST_MAX; index += 1U) {
        values[index] = tinypy_integer_from_i64(vm, INT64_C(1000000) + (int64_t)index);
    }
    for (index = 0U; index < TINYPY_INTEGER_FREE_LIST_MAX; index += 1U) {
        tinypy_release(values[index]);
    }
    base_allocations = state.outstanding_allocations;
    allocation_calls = state.allocation_calls;

    for (index = 0U; index < value_count; index += 1U) {
        values[index] = tinypy_integer_from_i64(vm, INT64_C(1000000) + (int64_t)index);
    }
    pooled_allocation_calls = state.allocation_calls - allocation_calls;
    TEST_CHECK(pooled_allocation_calls < value_count / 100U);
    for (index = 0U; index < value_count; index += 1U) {
        tinypy_release(values[index]);
    }
    TEST_CHECK(state.outstanding_allocations <= base_allocations + kept_allocations);

    /* A value that crosses into a new arena and goes again would otherwise
       take the arena from the host and return it on every pass. */
    count = __test_pool_fill_arena(vm, &state, values, value_count);
    TEST_CHECK(count < value_count);
    tinypy_release(values[count - 1U]);
    allocation_calls = state.allocation_calls;
    deallocation_calls = state.deallocation_calls;
    items[0] = tinypy_none_get(vm);
    for (index = 0U; index < pass_count; index += 1U) {
        tinypy_value_t *value = tinypy_tuple_from_items(vm, items, 1U);

        tinypy_release(value);
    }
    tinypy_release(items[0]);
    TEST_CHECK(state.allocation_calls - allocation_calls == pass_count * (1U - kept_allocations));
    TEST_CHECK(state.deallocation_calls - deallocation_calls == pass_count * (1U - kept_allocations));
    for (index = 0U; index + 1U < count; index += 1U) {
        tinypy_release(values[index]);
    }
    TEST_CHECK(state.outstanding_allocations <= base_allocations + kept_allocations);

    free(values);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_pool_allocator(void) {
    const size_t max_heap_bytes = 64U * 1024U * 1024U;

    if (__test_pool_allocator_budget(0U) != 0) {
        return 1;
    }
    int32_t result = __test_pool_allocator_budget(max_heap_bytes);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_independent_vms(void) {
    test_allocator_state_t state_a;
    test_allocator_state_t state_b;
    tinypy_allocator_t allocator_a;
    tinypy_allocator_t allocator_b;
    tinypy_vm_config_t config_a;
    tinypy_vm_config_t config_b;
    tinypy_vm_t *vm_a = NULL;
    tinypy_vm_t *vm_b = NULL;
    tinypy_value_t *none_a = NULL;
    tinypy_value_t *none_b = NULL;
    tinypy_value_t *value_a = NULL;
    int64_t extracted = INT64_C(0);

    (void)memset(&state_a, 0, sizeof(state_a));
    (void)memset(&state_b, 0, sizeof(state_b));
    allocator_a = __test_make_allocator(&state_a);
    allocator_b = __test_make_allocator(&state_b);
    config_a = __test_make_config(&allocator_a);
    config_b = __test_make_config(&allocator_b);

    vm_a = tinypy_vm_create(&config_a);
    vm_b = tinypy_vm_create(&config_b);
    TEST_CHECK(vm_a != NULL);
    TEST_CHECK(vm_b != NULL);
    none_a = tinypy_none_get(vm_a);
    none_b = tinypy_none_get(vm_b);
    TEST_CHECK(none_a != none_b);

    value_a = tinypy_integer_from_i64(vm_a, INT64_C(42));
    extracted = tinypy_integer_as_i64(value_a);
    TEST_CHECK(extracted == INT64_C(42));
    TEST_CHECK(tinypy_typeof(value_a) == TINYPY_VALUE_INTEGER);
    tinypy_release(value_a);
    tinypy_release(none_a);
    tinypy_release(none_b);

    tinypy_vm_destroy(vm_a);
    tinypy_vm_destroy(vm_b);
    TEST_CHECK(state_a.outstanding_allocations == 0U);
    TEST_CHECK(state_b.outstanding_allocations == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_value_lifetime(void) {
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_value_t *retained_value = NULL;
    tinypy_value_t *unreleased_value = NULL;
    size_t vm_allocations;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    vm_allocations = state.outstanding_allocations;

    retained_value = tinypy_integer_from_i64(vm, INT64_C(2048));
    tinypy_retain(retained_value);
    tinypy_release(retained_value);
    tinypy_release(retained_value);
    TEST_CHECK(state.outstanding_allocations == vm_allocations);

    unreleased_value = tinypy_integer_from_i64(vm, INT64_C(2049));
    tinypy_release(unreleased_value);
    TEST_CHECK(state.outstanding_allocations == vm_allocations);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_constant_cache(void) {
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_value_t *true_a;
    tinypy_value_t *true_b;
    tinypy_value_t *false_a;
    tinypy_value_t *false_b;
    tinypy_value_t *integer_min_a;
    tinypy_value_t *integer_min_b;
    tinypy_value_t *integer_max_a;
    tinypy_value_t *integer_max_b;
    tinypy_value_t *integer_one;
    tinypy_value_t *outside_low;
    tinypy_value_t *outside_high;
    tinypy_value_t *float_zero_a;
    tinypy_value_t *float_zero_b;
    tinypy_value_t *float_negative_zero;
    tinypy_value_t *empty_a;
    tinypy_value_t *empty_b;
    tinypy_value_t *empty_tuple_a;
    tinypy_value_t *empty_tuple_b;
    size_t allocation_calls;
    size_t base_allocations;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    base_allocations = state.outstanding_allocations;
    allocation_calls = state.allocation_calls;

    true_a = tinypy_bool_from_i32(vm, INT32_C(1));
    true_b = tinypy_bool_from_i32(vm, INT32_C(7));
    false_a = tinypy_bool_from_i32(vm, INT32_C(0));
    false_b = tinypy_bool_from_i32(vm, INT32_C(0));
    integer_min_a = tinypy_integer_from_i64(vm, INT64_C(-1023));
    integer_min_b = tinypy_integer_from_i64(vm, INT64_C(-1023));
    integer_max_a = tinypy_integer_from_i64(vm, INT64_C(1024));
    integer_max_b = tinypy_integer_from_i64(vm, INT64_C(1024));
    integer_one = tinypy_integer_from_i64(vm, INT64_C(1));
    float_zero_a = tinypy_float_from_double(vm, 0.0);
    float_zero_b = tinypy_float_from_double(vm, 0.0);
    empty_a = tinypy_string_from_bytes(vm, NULL, 0U);
    empty_b = tinypy_string_from_bytes(vm, "", 0U);
    empty_tuple_a = tinypy_tuple_from_items(vm, NULL, 0U);
    empty_tuple_b = tinypy_tuple_from_items(vm, NULL, 0U);

    TEST_CHECK(true_a == true_b);
    TEST_CHECK(false_a == false_b);
    TEST_CHECK(true_a != false_a);
    TEST_CHECK(true_a != integer_one);
    TEST_CHECK(integer_min_a == integer_min_b);
    TEST_CHECK(integer_max_a == integer_max_b);
    TEST_CHECK(tinypy_integer_as_i64(integer_min_a) == INT64_C(-1023));
    TEST_CHECK(tinypy_integer_as_i64(integer_max_a) == INT64_C(1024));
    TEST_CHECK(float_zero_a == float_zero_b);
    TEST_CHECK(empty_a == empty_b);
    TEST_CHECK(empty_tuple_a == empty_tuple_b);
    TEST_CHECK(tinypy_tuple_size(empty_tuple_a) == 0U);
    TEST_CHECK(state.allocation_calls == allocation_calls);

    outside_low = tinypy_integer_from_i64(vm, INT64_C(-1024));
    outside_high = tinypy_integer_from_i64(vm, INT64_C(1025));
    double double_from_bits = __test_double_from_bits(UINT64_C(0x8000000000000000));
    float_negative_zero = tinypy_float_from_double(
        vm,
        double_from_bits);
    TEST_CHECK(outside_low != integer_min_a);
    TEST_CHECK(outside_high != integer_max_a);
    TEST_CHECK(float_negative_zero != float_zero_a);

    tinypy_release(float_negative_zero);
    tinypy_release(outside_high);
    tinypy_release(outside_low);
    tinypy_release(empty_tuple_b);
    tinypy_release(empty_tuple_a);
    tinypy_release(empty_b);
    tinypy_release(empty_a);
    tinypy_release(float_zero_b);
    tinypy_release(float_zero_a);
    tinypy_release(integer_one);
    tinypy_release(integer_max_b);
    tinypy_release(integer_max_a);
    tinypy_release(integer_min_b);
    tinypy_release(integer_min_a);
    tinypy_release(false_b);
    tinypy_release(false_a);
    tinypy_release(true_b);
    tinypy_release(true_a);

    TEST_CHECK(state.outstanding_allocations == base_allocations);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_byte_strings(void) {
    //////////////////////////////////////////////////////////////////////////
    uint8_t bytes[] = {
        0x61U, 0x00U, 0x62U, 0xffU, 0x00U, 0x63U};
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_value_t *value = NULL;
    tinypy_value_t *empty = NULL;
    const void *view = NULL;
    size_t view_size = 0U;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);

    value = tinypy_string_from_bytes(vm, bytes, sizeof(bytes));
    bytes[0] = 0x7aU;
    TEST_CHECK(tinypy_typeof(value) == TINYPY_VALUE_STRING);
    view = tinypy_string_view(value, &view_size);
    TEST_CHECK(view != NULL);
    TEST_CHECK(view_size == sizeof(bytes));
    TEST_CHECK(((const uint8_t *)view)[0] == 0x61U);
    TEST_CHECK(memcmp(((const uint8_t *)view) + 1, bytes + 1, sizeof(bytes) - 1U) == 0);
    TEST_CHECK(((const uint8_t *)view)[view_size] == 0U);

    empty = tinypy_string_from_bytes(vm, NULL, 0U);
    view = tinypy_string_view(empty, &view_size);
    TEST_CHECK(view != NULL);
    TEST_CHECK(view_size == 0U);
    TEST_CHECK(((const uint8_t *)view)[0] == 0U);

    tinypy_retain(value);
    tinypy_release(value);
    tinypy_release(value);
    tinypy_release(empty);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_unicode_utf8(void) {
    //////////////////////////////////////////////////////////////////////////
    uint8_t utf8[] = {
        0x41U,
        0xc2U, 0xa2U,
        0xe2U, 0x82U, 0xacU,
        0xf0U, 0x9fU, 0x98U, 0x80U,
        0x00U,
        0xedU, 0x9fU, 0xbfU,
        0xeeU, 0x80U, 0x80U,
        0xf4U, 0x8fU, 0xbfU, 0xbfU};
#if WCHAR_MAX <= 0xffff
    static const wchar_t wide_units[] = {
        0x41, 0xa2, 0x20ac, 0xd83d, 0xde00, 0x0, 0xd7ff, 0xe000, 0xdbff, 0xdfff};
#else
    static const wchar_t wide_units[] = {
        0x41, 0xa2, 0x20ac, 0x1f600, 0x0, 0xd7ff, 0xe000, 0x10ffff};
#endif
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_value_t *value = NULL;
    static const char *const invalid_utf8[] = {
        "\xf4", "\xc0\x80", "\xe0\x80\x80", "\xf5\x80\x80\x80", "\xf4\x90\x80\x80", "a\xffz"};
    tinypy_value_t *empty = NULL;
    tinypy_value_t *decoded = NULL;
    tinypy_error_t *error = NULL;
    const char *view = NULL;
    const wchar_t *wide = NULL;
    size_t invalid_index;
    size_t view_size = 0U;
    size_t wide_size = 0U;
    size_t code_point_count = 0U;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);

    value = tinypy_unicode_from_utf8(
        vm,
        (const char *)utf8,
        sizeof(utf8));
    utf8[0] = 0x5aU;
    TEST_CHECK(tinypy_typeof(value) == TINYPY_VALUE_UNICODE);
    view = tinypy_unicode_utf8_view(value,
                                    &view_size,
                                    &code_point_count);
    TEST_CHECK(view_size == sizeof(utf8));
    TEST_CHECK(code_point_count == 8U);
    TEST_CHECK(((const uint8_t *)view)[0] == 0x41U);
    TEST_CHECK(
        memcmp(view + 1, utf8 + 1, sizeof(utf8) - 1U) == 0);
    TEST_CHECK(((const uint8_t *)view)[view_size] == 0U);

    wide = tinypy_unicode_wchar_view(value, &wide_size, &error);
    TEST_CHECK(wide != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(wide_size == sizeof(wide_units) / sizeof(wide_units[0]));
    TEST_CHECK(memcmp(wide, wide_units, sizeof(wide_units)) == 0);
    TEST_CHECK(wide[wide_size] == L'\0');
    TEST_CHECK(tinypy_unicode_wchar_view(value, &wide_size, NULL) == wide);
    TEST_CHECK(wide_size == sizeof(wide_units) / sizeof(wide_units[0]));

    empty = tinypy_unicode_from_utf8(vm, NULL, 0U);
    view = tinypy_unicode_utf8_view(empty,
                                    &view_size,
                                    &code_point_count);
    TEST_CHECK(view != NULL);
    TEST_CHECK(view_size == 0U);
    TEST_CHECK(code_point_count == 0U);
    TEST_CHECK(view[0] == '\0');
    wide = tinypy_unicode_wchar_view(empty, &wide_size, &error);
    TEST_CHECK(wide != NULL);
    TEST_CHECK(wide_size == 0U);
    TEST_CHECK(wide[0] == L'\0');

    decoded = tinypy_unicode_decode_utf8(vm, (const char *)utf8, sizeof(utf8), &error);
    TEST_CHECK(decoded != NULL);
    TEST_CHECK(error == NULL);
    view = tinypy_unicode_utf8_view(decoded, &view_size, &code_point_count);
    TEST_CHECK(view_size == sizeof(utf8));
    TEST_CHECK(code_point_count == 8U);
    TEST_CHECK(memcmp(view, utf8, sizeof(utf8)) == 0);
    tinypy_release(decoded);
    for (invalid_index = 0U; invalid_index < sizeof(invalid_utf8) / sizeof(invalid_utf8[0]); ++invalid_index) {
        size_t raised_name_size = 0U;

        decoded = tinypy_unicode_decode_utf8(vm, invalid_utf8[invalid_index], strlen(invalid_utf8[invalid_index]), &error);
        TEST_CHECK(decoded == NULL);
        TEST_CHECK(tinypy_vm_has_error(vm) != 0);
        TEST_CHECK(strcmp(tinypy_type_name(tinypy_value_as_const_type(tinypy_vm_raised_exception_type(vm)), &raised_name_size), "exceptions.UnicodeDecodeError") == 0);
        if (error != NULL) {
            tinypy_error_release(error);
            error = NULL;
        }
        tinypy_vm_clear_error(vm);
    }

    tinypy_release(value);
    tinypy_release(empty);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* A buffer over a unicode views the UCS-4 code points the first buffer
   caches on it; when the host refuses their memory, tinypy_buffer_from_object
   returns NULL with MemoryError raised rather than a buffer without bytes.
   An offset that the offset of a buffer carries past PTRDIFF_MAX returns
   NULL with ValueError raised rather than a wrapped view. */
static int32_t __test_buffer_unicode_refusal(void) {
    static const char digits[] = "0123456789abcdef";
    char utf8[512];
    test_allocator_state_t state;
    size_t size = 0U;

    for (size_t index = 0U; index < sizeof(utf8); ++index) {
        utf8[index] = digits[index % (sizeof(digits) - 1U)];
    }
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *memory_error = &vm->exception_types[TINYPY_EXCEPTION_MEMORY_ERROR]->base.base;
    tinypy_value_t *value_error = &vm->exception_types[TINYPY_EXCEPTION_VALUE_ERROR]->base.base;
    tinypy_value_t *text = tinypy_unicode_from_utf8(vm, utf8, sizeof(utf8));
    TEST_CHECK(text != NULL);
    state.fail_allocation_size = sizeof(utf8) * sizeof(uint32_t);
    tinypy_value_t *refused = tinypy_buffer_from_object(text, 0U, TINYPY_BUFFER_TO_END);
    state.fail_allocation_size = 0U;
    TEST_CHECK(refused == NULL);
    TEST_CHECK(tinypy_vm_raised_exception_type(vm) == memory_error);
    tinypy_vm_clear_error(vm);

    tinypy_value_t *buffer = tinypy_buffer_from_object(text, sizeof(uint32_t), TINYPY_BUFFER_TO_END);
    TEST_CHECK(buffer != NULL && tinypy_vm_has_error(vm) == 0);
    const uint32_t *view = (const uint32_t *)tinypy_buffer_view(buffer, &size);
    TEST_CHECK(view != NULL && size == (sizeof(utf8) - 1U) * sizeof(uint32_t));
    TEST_CHECK(view[0] == (uint32_t)'1' && view[size / sizeof(uint32_t) - 1U] == (uint32_t)'f');
    size_t overflowing_offsets[] = {(size_t)PTRDIFF_MAX - sizeof(uint32_t) + 1U, (size_t)PTRDIFF_MAX + 1U, SIZE_MAX};
    for (size_t index = 0U; index < sizeof(overflowing_offsets) / sizeof(overflowing_offsets[0]); ++index) {
        tinypy_value_t *overflowing = tinypy_buffer_from_object(buffer, overflowing_offsets[index], TINYPY_BUFFER_TO_END);
        TEST_CHECK(overflowing == NULL);
        TEST_CHECK(tinypy_vm_raised_exception_type(vm) == value_error);
        tinypy_vm_clear_error(vm);
    }
    tinypy_value_t *farthest = tinypy_buffer_from_object(buffer, (size_t)PTRDIFF_MAX - sizeof(uint32_t), TINYPY_BUFFER_TO_END);
    TEST_CHECK(farthest != NULL && tinypy_vm_has_error(vm) == 0);
    const void *farthest_view = tinypy_buffer_view(farthest, &size);
    TEST_CHECK(farthest_view != NULL && size == 0U);
    tinypy_release(farthest);
    tinypy_release(buffer);
    tinypy_release(text);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_string_release_contract(void) {
    uint8_t bytes[257];
    //////////////////////////////////////////////////////////////////////////
    static const char unicode_bytes[] = {
        (char)0x61, (char)0x00, (char)0xe2, (char)0x82, (char)0xac,
        (char)0xf0, (char)0x9f, (char)0x98, (char)0x80};
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_value_t *string_value = NULL;
    tinypy_value_t *unicode_value = NULL;
    size_t index;
    size_t base_allocations;

    for (index = 0U; index < sizeof(bytes); index += 1U) {
        bytes[index] = (uint8_t)(index & 0xffU);
    }

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    base_allocations = state.outstanding_allocations;
    string_value = tinypy_string_from_bytes(vm, bytes, sizeof(bytes));
    unicode_value = tinypy_unicode_from_utf8(
        vm,
        unicode_bytes,
        sizeof(unicode_bytes));
    TEST_CHECK(tinypy_string_from_bytes(vm, bytes, SIZE_MAX) == NULL);
    TEST_CHECK(tinypy_unicode_from_utf8(vm, (const char *)bytes, SIZE_MAX) == NULL);
    TEST_CHECK(tinypy_long_from_base15_digits(vm, 1, (const uint16_t *)bytes, SIZE_MAX) == NULL);
    TEST_CHECK(tinypy_tuple_new(vm, SIZE_MAX) == NULL);
    TEST_CHECK(tinypy_tuple_from_items(vm, NULL, SIZE_MAX) == NULL);
    TEST_CHECK(tinypy_list_from_items(vm, NULL, SIZE_MAX) == NULL);
    tinypy_retain(string_value);
    tinypy_retain(unicode_value);

    tinypy_release(string_value);
    tinypy_release(string_value);
    tinypy_release(unicode_value);
    tinypy_release(unicode_value);
    TEST_CHECK(state.outstanding_allocations == base_allocations);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    TEST_CHECK(state.deallocation_calls == state.allocation_calls);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_float_complex_bits(void) {
    //////////////////////////////////////////////////////////////////////////
    static const uint64_t patterns[] = {
        UINT64_C(0x0000000000000000),
        UINT64_C(0x8000000000000000),
        UINT64_C(0x3ff0000000000000),
        UINT64_C(0xbff0000000000000),
        UINT64_C(0x7ff0000000000000),
        UINT64_C(0x7ff0000000000001),
        UINT64_C(0x7ff8000000001234),
        UINT64_C(0xfff8000000005678)};
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_value_t *value = NULL;
    size_t index;
    double extracted;
    double extracted_real;
    double extracted_imaginary;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);

    for (index = 0U;
         index < sizeof(patterns) / sizeof(patterns[0]);
         index += 1U) {
        double input = __test_double_from_bits(patterns[index]);

        value = tinypy_float_from_double(vm, input);
        TEST_CHECK(tinypy_typeof(value) == TINYPY_VALUE_FLOAT);
        extracted = tinypy_float_as_double(value);
        TEST_CHECK(__test_double_to_bits(extracted) == patterns[index]);
        tinypy_release(value);
        value = NULL;
    }

    double double_from_bits = __test_double_from_bits(UINT64_C(0x8000000000000000));
    double double_from_bits_2 = __test_double_from_bits(UINT64_C(0x7ff8000000001234));
    value = tinypy_complex_from_doubles(
        vm,
        double_from_bits,
        double_from_bits_2);
    TEST_CHECK(tinypy_typeof(value) == TINYPY_VALUE_COMPLEX);
    tinypy_complex_as_doubles(
        value,
        &extracted_real,
        &extracted_imaginary);
    TEST_CHECK(__test_double_to_bits(extracted_real) ==
               UINT64_C(0x8000000000000000));
    TEST_CHECK(__test_double_to_bits(extracted_imaginary) ==
               UINT64_C(0x7ff8000000001234));
    tinypy_release(value);

    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* Released exact floats stay on the VM's free list up to its bound and are
   taken back before the allocator, also once the heap budget is spent; the
   shared positive zero never enters it, and VM destroy returns the list to
   the host. */
static int32_t __test_float_free_list(void) {
    tinypy_value_t *values[TINYPY_FLOAT_FREE_LIST_MAX + 8U];
    size_t count = sizeof(values) / sizeof(values[0]);
    test_allocator_state_t state;
    tinypy_error_t *error = NULL;
    size_t index;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    for (index = 0U; index < count; ++index) {
        values[index] = tinypy_float_from_double(vm, 0.5 + (double)index);
    }
    for (index = 0U; index < count; ++index) {
        tinypy_release(values[index]);
    }
    TEST_CHECK(vm->float_free_count == TINYPY_FLOAT_FREE_LIST_MAX);
    tinypy_value_t *zero = tinypy_float_from_double(vm, 0.0);
    TEST_CHECK(zero == &vm->float_zero_object.base);
    tinypy_release(zero);
    TEST_CHECK(vm->float_free_count == TINYPY_FLOAT_FREE_LIST_MAX);

    vm->max_heap_bytes = vm->allocated_bytes;
    tinypy_value_t *reused = tinypy_internal_float_from_double_checked(vm, -2.5, &error);
    TEST_CHECK(reused != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_float_as_double(reused) == -2.5);
    TEST_CHECK(vm->float_free_count == TINYPY_FLOAT_FREE_LIST_MAX - 1U);
    tinypy_release(reused);
    TEST_CHECK(vm->float_free_count == TINYPY_FLOAT_FREE_LIST_MAX);

    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_long_canonical(void) {
    //////////////////////////////////////////////////////////////////////////
    static const int64_t values[] = {
        INT64_C(0),
        INT64_C(1),
        INT64_C(-1),
        INT64_C(32767),
        INT64_C(32768),
        INT64_C(-32768),
        INT64_MAX,
        INT64_MIN};
    //////////////////////////////////////////////////////////////////////////
    static const uint16_t maximum_digits[] = {
        UINT16_C(0x7fff), UINT16_C(0x7fff), UINT16_C(0x7fff),
        UINT16_C(0x7fff), UINT16_C(0x0007)};
    //////////////////////////////////////////////////////////////////////////
    static const uint16_t minimum_digits[] = {
        UINT16_C(0), UINT16_C(0), UINT16_C(0), UINT16_C(0),
        UINT16_C(0x0008)};
    //////////////////////////////////////////////////////////////////////////
    uint16_t arbitrary_digits[] = {
        UINT16_C(1), UINT16_C(0x7fff), UINT16_C(2),
        UINT16_C(3), UINT16_C(4), UINT16_C(1)};
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_value_t *value = NULL;
    size_t value_index;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);

    for (value_index = 0U;
         value_index < sizeof(values) / sizeof(values[0]);
         ++value_index) {
        uint16_t expected_digits[5];
        uint64_t magnitude;
        size_t expected_count = 0U;
        const uint16_t *digits = NULL;
        size_t digit_count = 0U;
        int32_t sign = 0;
        int64_t extracted = INT64_C(0);
        size_t digit_index;

        if (values[value_index] < INT64_C(0)) {
            magnitude = (uint64_t)(-(values[value_index] + INT64_C(1)));
            magnitude += UINT64_C(1);
        }
        else {
            magnitude = (uint64_t)values[value_index];
        }
        while (magnitude != UINT64_C(0)) {
            expected_digits[expected_count] = (uint16_t)(magnitude & UINT64_C(0x7fff));
            expected_count += 1U;
            magnitude >>= 15U;
        }

        value = tinypy_long_from_i64(vm, values[value_index]);
        TEST_CHECK(tinypy_typeof(value) == TINYPY_VALUE_LONG);
        digits = tinypy_long_base15_view(value,
                                         &sign,
                                         &digit_count);
        TEST_CHECK(digit_count == expected_count);
        TEST_CHECK(sign == (values[value_index] > INT64_C(0) ? 1 : (values[value_index] < INT64_C(0) ? -1 : 0)));
        TEST_CHECK((digit_count == 0U) == (digits == NULL));
        for (digit_index = 0U;
             digit_index < digit_count;
             ++digit_index) {
            TEST_CHECK(digits[digit_index] == expected_digits[digit_index]);
        }
        extracted = tinypy_long_as_i64(value);
        TEST_CHECK(extracted == values[value_index]);

        double double_value = 17.0;
        tinypy_error_t *error = NULL;
        size_t allocations = state.allocation_calls;
        TEST_CHECK(tinypy_long_as_double(value, &double_value, &error) != 0);
        TEST_CHECK(double_value == (double)values[value_index]);
        TEST_CHECK(error == NULL);
        TEST_CHECK(state.allocation_calls == allocations);
        tinypy_release(value);
        value = NULL;
    }

    value = tinypy_long_from_base15_digits(
        vm,
        1,
        maximum_digits,
        sizeof(maximum_digits) / sizeof(maximum_digits[0])); {
        int64_t extracted = INT64_C(0);
        extracted = tinypy_long_as_i64(value);
        TEST_CHECK(extracted == INT64_MAX);
    }
    tinypy_release(value);

    value = tinypy_long_from_base15_digits(
        vm,
        -1,
        minimum_digits,
        sizeof(minimum_digits) / sizeof(minimum_digits[0])); {
        int64_t extracted = INT64_C(0);
        extracted = tinypy_long_as_i64(value);
        TEST_CHECK(extracted == INT64_MIN);
    }
    tinypy_release(value);

    value = tinypy_long_from_base15_digits(
        vm,
        -1,
        arbitrary_digits,
        sizeof(arbitrary_digits) / sizeof(arbitrary_digits[0]));
    arbitrary_digits[0] = UINT16_C(99); {
        int32_t sign = 0;
        const uint16_t *digits = NULL;
        size_t digit_count = 0U;
        digits = tinypy_long_base15_view(value,
                                         &sign,
                                         &digit_count);
        TEST_CHECK(sign == -1);
        TEST_CHECK(digit_count == 6U);
        TEST_CHECK(digits[0] == UINT16_C(1));
    }
    tinypy_release(value);

    /* The digit constructor drops zero digits at the top and makes zero of a
     * zero magnitude whatever its sign, so the value compares, hashes and
     * divides like any other 5; it refuses a sign or digit no long carries. */
    const uint16_t padded_digits[] = {5U, 0U, 0U};
    const uint16_t zero_digits[] = {0U, 0U};
    const uint16_t wide_digits_top[] = {1U, 0x8000U};
    int32_t padded_sign = 0;
    size_t padded_count = 0U;
    value = tinypy_long_from_base15_digits(vm, 1, padded_digits, 3U);
    const uint16_t *padded_view = tinypy_long_base15_view(value, &padded_sign, &padded_count);
    TEST_CHECK(padded_sign == 1 && padded_count == 1U && padded_view[0] == 5U);
    TEST_CHECK(tinypy_hash(value) == 5);
    tinypy_value_t *dividend = tinypy_long_from_i64(vm, INT64_C(1) << 40);
    tinypy_value_t *quotient = tinypy_floor_divide(dividend, value, NULL);
    TEST_CHECK(quotient != NULL && tinypy_long_as_i64(quotient) == (INT64_C(1) << 40) / 5);
    tinypy_release(quotient);
    tinypy_release(dividend);
    tinypy_release(value);
    for (int32_t sign = -1; sign <= 1; ++sign) {
        for (size_t count = 0U; count <= 2U; count += 2U) {
            int32_t zero_sign = 7;
            size_t zero_count = 7U;
            value = tinypy_long_from_base15_digits(vm, sign, count != 0U ? zero_digits : NULL, count);
            TEST_CHECK(tinypy_long_base15_view(value, &zero_sign, &zero_count) == NULL);
            TEST_CHECK(zero_sign == 0 && zero_count == 0U);
            tinypy_release(value);
        }
    }
    TEST_CHECK(tinypy_long_from_base15_digits(vm, 0, padded_digits, 3U) == NULL);
    TEST_CHECK(tinypy_long_from_base15_digits(vm, 2, padded_digits, 3U) == NULL);
    TEST_CHECK(tinypy_long_from_base15_digits(vm, -2, zero_digits, 2U) == NULL);
    TEST_CHECK(tinypy_long_from_base15_digits(vm, 1, wide_digits_top, 2U) == NULL);
    TEST_CHECK(tinypy_long_from_base15_digits(vm, -1, wide_digits_top, 2U) == NULL);
    TEST_CHECK(tinypy_vm_has_error(vm) == 0);

    /* Halfway values round to the even significand for either sign. */
    uint16_t rounding_digits[] = {0U, 0U, 0U, 256U};
    const uint16_t rounding_low_digits[] = {1U, 3U};
    const double rounded_values[] = {9007199254740992.0, 9007199254740996.0};
    for (size_t index = 0U; index < 2U; ++index) {
        rounding_digits[0] = rounding_low_digits[index];
        for (int32_t sign = -1; sign <= 1; sign += 2) {
            value = tinypy_long_from_base15_digits(vm, sign, rounding_digits, 4U);
            double double_value = 17.0;
            TEST_CHECK(tinypy_long_as_double(value, &double_value, NULL) != 0);
            TEST_CHECK(double_value == (double)sign * rounded_values[index]);
            tinypy_release(value);
        }
    }

    /* 2^100 + 1 exceeds int64_t but rounds to the exactly represented 2^100. */
    const uint16_t wide_digits[] = {1U, 0U, 0U, 0U, 0U, 0U, 1024U};
    value = tinypy_long_from_base15_digits(vm, 1, wide_digits, 7U);
    double double_value = 17.0;
    TEST_CHECK(tinypy_long_as_double(value, &double_value, NULL) != 0);
    TEST_CHECK(double_value == ldexp(1.0, 100));
    tinypy_release(value);

    /* DBL_MAX as an integer: bits 971 through 1023 are set. */
    uint16_t limit_digits[69] = {0};
    for (size_t bit = 971U; bit < 1024U; ++bit) {
        limit_digits[bit / 15U] |= (uint16_t)(UINT16_C(1) << (bit % 15U));
    }
    value = tinypy_long_from_base15_digits(vm, 1, limit_digits, 69U);
    TEST_CHECK(tinypy_long_as_double(value, &double_value, NULL) != 0);
    TEST_CHECK(double_value == DBL_MAX);
    tinypy_release(value);

    /* DBL_MAX + 2^970 rounds up to 2^1024, beyond the largest finite double.
     * 2^1024 itself exceeds DBL_MAX_EXP bits before rounding. Both failures
     * must preserve the output. */
    limit_digits[970U / 15U] |= (uint16_t)(UINT16_C(1) << (970U % 15U));
    for (size_t index = 0U; index < 2U; ++index) {
        value = tinypy_long_from_base15_digits(vm, 1, limit_digits, 69U);
        tinypy_error_t *error = NULL;
        double_value = 17.0;
        TEST_CHECK(tinypy_long_as_double(value, &double_value, &error) == 0);
        TEST_CHECK(double_value == 17.0);
        TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_OVERFLOW);
        TEST_CHECK(tinypy_vm_has_error(vm) != 0);
        tinypy_error_release(error);
        tinypy_vm_clear_error(vm);
        tinypy_release(value);
        (void)memset(limit_digits, 0, sizeof(limit_digits));
        limit_digits[1024U / 15U] = (uint16_t)(UINT16_C(1) << (1024U % 15U));
    }

    /* As PyNumber_AsSsize_t: an int is an index, a float is not one, and a
     * long that int64_t cannot hold raises the overflow kind. */
    int64_t index = 0;
    tinypy_error_t *error = NULL;

    value = tinypy_integer_from_i64(vm, INT64_C(-7));
    TEST_CHECK(tinypy_number_as_index(value, TINYPY_ERROR_INDEX, &index, NULL) != 0);
    TEST_CHECK(index == INT64_C(-7));
    tinypy_release(value);

    value = tinypy_float_from_double(vm, 1.5);
    TEST_CHECK(tinypy_number_as_index(value, TINYPY_ERROR_INDEX, &index, &error) == 0);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_TYPE);
    tinypy_error_release(error);
    tinypy_vm_clear_error(vm);
    tinypy_release(value);

    error = NULL;
    value = tinypy_long_from_base15_digits(vm, 1, limit_digits, 69U);
    TEST_CHECK(tinypy_number_as_index(value, TINYPY_ERROR_INDEX, &index, &error) == 0);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_INDEX);
    tinypy_error_release(error);
    tinypy_vm_clear_error(vm);
    tinypy_release(value);

    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_tuple_ownership(void) {
    static const uint8_t child_bytes[] = {0x61U, 0x00U, 0x62U};
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_value_t *none_value = NULL;
    tinypy_value_t *child = NULL;
    tinypy_value_t *tuple = NULL;
    tinypy_value_t *borrowed = NULL;
    tinypy_value_t *empty = NULL;
    tinypy_value_t *leaf = NULL;
    tinypy_value_t *left = NULL;
    tinypy_value_t *right = NULL;
    tinypy_value_t *parent = NULL;
    tinypy_value_t *items[3];
    size_t tuple_size = 0U;
    size_t calls_before;
    size_t base_allocations;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    base_allocations = state.outstanding_allocations;
    none_value = tinypy_none_get(vm);
    child = tinypy_string_from_bytes(vm, child_bytes, sizeof(child_bytes));

    items[0] = child;
    items[1] = child;
    items[2] = none_value;
    tuple = tinypy_tuple_from_items(vm, items, 3U);
    items[0] = none_value;
    items[1] = none_value;
    TEST_CHECK(tinypy_typeof(tuple) == TINYPY_VALUE_TUPLE);
    tuple_size = tinypy_tuple_size(tuple);
    TEST_CHECK(tuple_size == 3U);
    borrowed = tinypy_tuple_get(tuple, 0U);
    TEST_CHECK(borrowed == child);
    borrowed = tinypy_tuple_get(tuple, 1U);
    TEST_CHECK(borrowed == child);
    borrowed = tinypy_tuple_get(tuple, 2U);
    TEST_CHECK(borrowed == none_value);

    tinypy_release(child);
    tinypy_release(tuple);
    tinypy_release(none_value);
    TEST_CHECK(state.outstanding_allocations == base_allocations);

    empty = tinypy_tuple_from_items(vm, NULL, 0U);
    tuple_size = tinypy_tuple_size(empty);
    TEST_CHECK(tuple_size == 0U);
    tinypy_release(empty);

    child = tinypy_string_from_bytes(vm, child_bytes, sizeof(child_bytes));
    tuple = tinypy_tuple_new(vm, 2U);
    none_value = tinypy_none_get(vm);
    TEST_CHECK(tinypy_tuple_get(tuple, 0U) == none_value);
    tinypy_release(none_value);
    tinypy_tuple_set(tuple, 0U, child);
    tinypy_tuple_set(tuple, 1U, child);
    TEST_CHECK(tinypy_tuple_get(tuple, 0U) == child);
    TEST_CHECK(tinypy_tuple_get(tuple, 1U) == child);
    tinypy_release(child);
    tinypy_release(tuple);
    TEST_CHECK(state.outstanding_allocations == base_allocations);

    leaf = tinypy_long_from_i64(vm, INT64_C(7));
    items[0] = leaf;
    left = tinypy_tuple_from_items(vm, items, 1U);
    right = tinypy_tuple_from_items(vm, items, 1U);
    items[0] = left;
    items[1] = right;
    items[2] = left;
    parent = tinypy_tuple_from_items(vm, items, 3U);

    tinypy_release(leaf);
    tinypy_release(left);
    tinypy_release(right);
    calls_before = state.allocation_calls;
    tinypy_release(parent);
    TEST_CHECK(state.allocation_calls == calls_before);
    TEST_CHECK(state.outstanding_allocations == base_allocations);

    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_tuple_deep_release(void) {
    const size_t depth = 20000U;
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_value_t *current = NULL;
    size_t index;
    size_t calls_before;
    size_t base_allocations;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    base_allocations = state.outstanding_allocations;
    current = tinypy_integer_from_i64(vm, INT64_C(1));

    for (index = 0U; index < depth; index += 1U) {
        tinypy_value_t *next = NULL;
        tinypy_value_t *items[1];

        items[0] = current;
        next = tinypy_tuple_from_items(vm, items, 1U);
        tinypy_release(current);
        current = next;
    }
    calls_before = state.allocation_calls;
    tinypy_release(current);
    TEST_CHECK(state.allocation_calls == calls_before);
    /* The pool keeps the last arena that became wholly free. */
    TEST_CHECK(state.outstanding_allocations <= base_allocations + 1U);

    current = tinypy_integer_from_i64(vm, INT64_C(2));
    for (index = 0U; index < depth; index += 1U) {
        tinypy_value_t *next = NULL;
        tinypy_value_t *items[1];

        items[0] = current;
        next = tinypy_tuple_from_items(vm, items, 1U);
        tinypy_release(current);
        current = next;
    }
    tinypy_release(current);
    TEST_CHECK(state.outstanding_allocations <= base_allocations + 1U);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_tuple_set, like PyTuple_SetItem, takes an instance of a tuple
   subclass, whose items lie behind the instance layout. */
static int32_t __test_tuple_set_subclass(void) {
    static const char source[] = "class Record(tuple): pass\nrecord = Record((1, 2))\n";
    static const char check[] = "assert type(record) is Record and record == ('x', 2) and record[0] == 'x', record\n";
    test_allocator_state_t state;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "record.py", 9U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "record", 6U);
    tinypy_value_t *record = tinypy_dict_get(globals, key);
    tinypy_release(key);
    TEST_CHECK(record != NULL && tinypy_tuple_size(record) == 2U);
    tinypy_value_t *item = tinypy_string_from_bytes(vm, "x", 1U);
    tinypy_tuple_set(record, 0U, item);
    TEST_CHECK(tinypy_tuple_get(record, 0U) == item);
    tinypy_release(item);
    result = tinypy_exec_source(vm, check, sizeof(check) - 1U, "check.py", 8U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* A length hint past what a list can reserve, which on a 32-bit host lies
   past the address space as well, runs out of memory with MemoryError set,
   for every constructor that sizes its list from the hint. */
static int32_t __test_length_hint_past_reserve(void) {
    static const char source[] =
        "class Hint(object):\n"
        "    def __init__(self, size):\n"
        "        self.size = size\n"
        "    def __iter__(self):\n"
        "        return iter([1, 2])\n"
        "    def __length_hint__(self):\n"
        "        return self.size\n"
        "class Sized(Hint):\n"
        "    def __len__(self):\n"
        "        return self.size\n"
        "class Record(tuple):\n"
        "    pass\n"
        "def star(*arguments):\n"
        "    return arguments\n"
        "builds = (tuple, list, Record, lambda source: star(*source), lambda source: [].extend(source), lambda source: [0].extend(source))\n"
        "for size in sizes:\n"
        "    for source in (Hint(size), Sized(size)):\n"
        "        for build in builds:\n"
        "            try:\n"
        "                build(source)\n"
        "            except MemoryError:\n"
        "                pass\n"
        "            else:\n"
        "                raise AssertionError(size)\n";
    const uint64_t sizes[] = {(uint64_t)(SIZE_MAX / sizeof(void *)) + UINT64_C(1), UINT64_C(1) << 62U};
    test_allocator_state_t state;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_value_t *items[2];

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    for (size_t index = 0U; index < sizeof(sizes) / sizeof(sizes[0]); ++index) {
        items[index] = tinypy_integer_from_i64(vm, (int64_t)sizes[index]);
    }
    tinypy_value_t *size_tuple = tinypy_tuple_from_items(vm, items, sizeof(items) / sizeof(items[0]));
    for (size_t index = 0U; index < sizeof(sizes) / sizeof(sizes[0]); ++index) {
        tinypy_release(items[index]);
    }
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "sizes", 5U);
    tinypy_dict_set(globals, key, size_tuple);
    tinypy_release(key);
    tinypy_release(size_tuple);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "hint.py", 7U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_hash_and_equality(void) {
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_value_t *boolean = NULL;
    tinypy_value_t *integer = NULL;
    tinypy_value_t *long_one = NULL;
    tinypy_value_t *long_large = NULL;
    tinypy_value_t *floating = NULL;
    tinypy_value_t *fraction = NULL;
    tinypy_value_t *complex_one = NULL;
    tinypy_value_t *complex_pair = NULL;
    tinypy_value_t *string = NULL;
    tinypy_value_t *unicode = NULL;
    tinypy_value_t *unicode_pi = NULL;
    tinypy_value_t *tuple = NULL;
    tinypy_value_t *list_a = NULL;
    tinypy_value_t *list_b = NULL;
    tinypy_value_t *popped = NULL;
    tinypy_value_t *nan_value = NULL;
    tinypy_value_t *iter_method = NULL;
    tinypy_value_t *iter_args = NULL;
    tinypy_value_t *iterator = NULL;
    tinypy_value_t *iterated = NULL;
    tinypy_error_t *error = NULL;
    const tinypy_type_t *integer_type;
    const tinypy_type_t *boolean_type;
    const tinypy_type_t *string_type;
    const tinypy_type_t *metaclass_type;
    const tinypy_value_t *integer_type_dict;
    const char *type_name;
    size_t type_name_size;
    size_t type_dict_size;
    tinypy_value_t *items[2];
    tinypy_hash_t hash;
    uint16_t large_digits[5] = {0U, 0U, 0U, 0U, 1024U};
    uint64_t nan_bits = UINT64_C(0x7ff8000000000001);
    double nan_double;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);

    boolean = tinypy_bool_from_i32(vm, INT32_C(1));
    integer = tinypy_integer_from_i64(vm, 1);
    long_one = tinypy_long_from_i64(vm, 1);
    long_large = tinypy_long_from_base15_digits(vm, -1, large_digits, 5U);
    floating = tinypy_float_from_double(vm, 1.0);
    fraction = tinypy_float_from_double(vm, 1.5);
    complex_one = tinypy_complex_from_doubles(vm, 1.0, 0.0);
    complex_pair = tinypy_complex_from_doubles(vm, 1.0, 2.0);
    string = tinypy_string_from_bytes(vm, "abc", 3U);
    unicode = tinypy_unicode_from_utf8(vm, "abc", 3U);
    unicode_pi = tinypy_unicode_from_utf8(vm, "\xcf\x80", 2U);

    boolean_type = tinypy_object_type(boolean);
    integer_type = tinypy_object_type(integer);
    string_type = tinypy_object_type(string);
    TEST_CHECK(boolean_type != NULL);
    TEST_CHECK(integer_type != NULL);
    TEST_CHECK(string_type != NULL);
    TEST_CHECK(integer_type != string_type);
    type_name = tinypy_type_name(integer_type, &type_name_size);
    TEST_CHECK(type_name != NULL);
    TEST_CHECK(type_name_size == 3U);
    TEST_CHECK(memcmp(type_name, "int", 3U) == 0);
    metaclass_type = tinypy_type_metaclass(integer_type);
    TEST_CHECK(metaclass_type != NULL);
    TEST_CHECK(tinypy_type_metaclass(metaclass_type) == metaclass_type);
    TEST_CHECK(tinypy_type_base(boolean_type) == integer_type);
    TEST_CHECK(tinypy_type_is_subtype(boolean_type, integer_type) == 1);
    TEST_CHECK(tinypy_type_is_subtype(integer_type, boolean_type) == 0);
    integer_type_dict = tinypy_type_dict(integer_type);
    TEST_CHECK(integer_type_dict != NULL);
    TEST_CHECK(tinypy_typeof(integer_type_dict) == TINYPY_VALUE_DICT);
    type_dict_size = tinypy_dict_size(integer_type_dict);
    TEST_CHECK(type_dict_size != 0U);

    TEST_CHECK(tinypy_equal(integer, long_one) == 1);
    TEST_CHECK(tinypy_equal(integer, floating) == 1);
    TEST_CHECK(tinypy_equal(integer, complex_one) == 1);
    TEST_CHECK(tinypy_equal(integer, complex_pair) == 0);
    TEST_CHECK(tinypy_equal(string, unicode) == 1);
    TEST_CHECK(tinypy_equal(integer, fraction) == 0);

    hash = tinypy_hash(integer);
    TEST_CHECK(hash == 1);
    hash = tinypy_hash(long_one);
    TEST_CHECK(hash == 1);
    hash = tinypy_hash(floating);
    TEST_CHECK(hash == 1);
    hash = tinypy_hash(complex_one);
    TEST_CHECK(hash == 1);
    hash = tinypy_hash(complex_pair);
    TEST_CHECK(hash == INT64_C(2000007));
    hash = tinypy_hash(long_large);
    TEST_CHECK(hash == -64);
    hash = tinypy_hash(fraction);
    TEST_CHECK(hash == INT64_C(1610645504));
    hash = tinypy_hash(string);
    TEST_CHECK(hash == INT64_C(1453079729188098211));
    hash = tinypy_hash(unicode);
    TEST_CHECK(hash == INT64_C(1453079729188098211));
    hash = tinypy_hash(unicode_pi);
    TEST_CHECK(hash == INT64_C(122880369601));

    items[0] = integer;
    items[1] = string;
    tuple = tinypy_tuple_from_items(vm, items, 2U);
    hash = tinypy_hash(tuple);
    TEST_CHECK(hash == INT64_C(7932834718630705379));
    list_a = tinypy_list_from_items(vm, items, 2U);
    list_b = tinypy_list_from_items(vm, items, 2U);
    TEST_CHECK(tinypy_list_size(list_a) == 2U);
    TEST_CHECK(tinypy_list_get(list_a, 0U) == integer);
    TEST_CHECK(tinypy_list_get(list_a, 1U) == string);
    TEST_CHECK(tinypy_list_version(list_a) == UINT64_C(0));
    iter_method = tinypy_object_get_attr(list_a, "__iter__", 8U, &error);
    TEST_CHECK(iter_method != NULL && error == NULL);
    iter_args = tinypy_tuple_from_items(vm, NULL, 0U);
    iterator = tinypy_call(iter_method, iter_args, NULL, &error);
    TEST_CHECK(iterator != NULL && error == NULL);
    iterated = tinypy_next(iterator, &error);
    TEST_CHECK(iterated == integer && error == NULL);
    tinypy_release(iterated);
    iterated = tinypy_next(iterator, &error);
    TEST_CHECK(iterated == string && error == NULL);
    tinypy_release(iterated);
    TEST_CHECK(tinypy_next(iterator, &error) == NULL && error == NULL);
    TEST_CHECK(tinypy_vm_raised_exception(vm) == NULL);
    TEST_CHECK(tinypy_next(iterator, &error) == NULL && error == NULL);
    TEST_CHECK(tinypy_vm_raised_exception(vm) == NULL);
    tinypy_release(iterator);
    tinypy_release(iter_args);
    tinypy_release(iter_method);
    TEST_CHECK(tinypy_equal(list_a, list_b) == 1);
    TEST_CHECK(tinypy_equal(tuple, list_a) == 0);
    tinypy_list_append(list_a, fraction);
    TEST_CHECK(tinypy_list_size(list_a) == 3U);
    TEST_CHECK(tinypy_list_version(list_a) == UINT64_C(1));
    items[0] = fraction;
    tinypy_list_extend(list_b, items, 1U);
    TEST_CHECK(tinypy_equal(list_a, list_b) == 1);
    tinypy_list_insert(list_a, 1U, long_one);
    TEST_CHECK(tinypy_list_get(list_a, 1U) == long_one);
    tinypy_list_set(list_a, 1U, floating);
    TEST_CHECK(tinypy_list_get(list_a, 1U) == floating);
    popped = tinypy_list_pop(list_a, 1U);
    TEST_CHECK(popped == floating);
    tinypy_release(popped);
    tinypy_list_delete(list_a, 2U);
    TEST_CHECK(tinypy_list_size(list_a) == 2U);
    tinypy_list_clear(list_b);
    TEST_CHECK(tinypy_list_size(list_b) == 0U);
    TEST_CHECK(tinypy_list_version(list_b) == UINT64_C(2));
    (void)memcpy(&nan_double, &nan_bits, sizeof(nan_double));
    nan_value = tinypy_float_from_double(vm, nan_double);
    TEST_CHECK(tinypy_equal(nan_value, nan_value) == 0);
    hash = tinypy_hash(nan_value);
    TEST_CHECK(hash == 0);

    tinypy_release(nan_value);
    tinypy_release(list_b);
    tinypy_release(list_a);
    tinypy_release(tuple);
    tinypy_release(unicode_pi);
    tinypy_release(unicode);
    tinypy_release(string);
    tinypy_release(fraction);
    tinypy_release(complex_pair);
    tinypy_release(complex_one);
    tinypy_release(floating);
    tinypy_release(long_large);
    tinypy_release(long_one);
    tinypy_release(integer);
    tinypy_release(boolean);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_dictionary_runtime(void) {
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_value_t *dict = NULL;
    tinypy_value_t *dict_equal = NULL;
    tinypy_value_t *key_a = NULL;
    tinypy_value_t *key_a_equal = NULL;
    tinypy_value_t *value_one = NULL;
    tinypy_value_t *value_two = NULL;
    tinypy_value_t *unhashable = NULL;
    tinypy_value_t *borrowed = NULL;
    size_t size;
    uint64_t version;
    tinypy_bool_t contains;
    size_t index;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    dict = tinypy_dict_new(vm);
    TEST_CHECK(tinypy_typeof(dict) == TINYPY_VALUE_DICT);
    size = tinypy_dict_size(dict);
    TEST_CHECK(size == 0U);
    version = tinypy_dict_version(dict);
    TEST_CHECK(version == UINT64_C(0));

    key_a = tinypy_string_from_bytes(vm, "key", 3U);
    key_a_equal = tinypy_string_from_bytes(vm, "key", 3U);
    value_one = tinypy_integer_from_i64(vm, 1);
    value_two = tinypy_integer_from_i64(vm, 2);

    size = tinypy_dict_size(dict);
    TEST_CHECK(size == 0U);

    tinypy_dict_set(dict, key_a, value_one);
    dict_equal = tinypy_dict_new(vm);
    tinypy_dict_set(dict_equal, key_a_equal, value_one);
    TEST_CHECK(tinypy_equal(dict, dict_equal) == 1);
    contains = tinypy_dict_contains(dict, key_a_equal);
    TEST_CHECK(contains == 1);
    borrowed = tinypy_dict_get(dict, key_a_equal);
    TEST_CHECK(borrowed == value_one);
    borrowed = tinypy_dict_get_optional(dict, key_a_equal);
    TEST_CHECK(borrowed == value_one);
    tinypy_dict_set(dict, key_a_equal, value_two);
    TEST_CHECK(tinypy_equal(dict, dict_equal) == 0);
    size = tinypy_dict_size(dict);
    TEST_CHECK(size == 1U);
    borrowed = tinypy_dict_get(dict, key_a);
    TEST_CHECK(borrowed == value_two);

    for (index = 0U; index < 100U; index += 1U) {
        tinypy_value_t *key = NULL;
        tinypy_value_t *value = NULL;
        key = tinypy_integer_from_i64(vm, (int64_t)index + 100);
        value = tinypy_integer_from_i64(vm, (int64_t)index + 1000);
        tinypy_dict_set(dict, key, value);
        tinypy_release(value);
        tinypy_release(key);
    }
    size = tinypy_dict_size(dict);
    TEST_CHECK(size == 101U);

    tinypy_dict_delete(dict, key_a_equal);
    TEST_CHECK(tinypy_dict_contains(dict, key_a) == 0);
    TEST_CHECK(tinypy_dict_get_optional(dict, key_a) == NULL);
    TEST_CHECK(tinypy_dict_get(dict, key_a) == NULL);

    tinypy_dict_set(dict, key_a, dict);
    tinypy_dict_clear(dict);
    size = tinypy_dict_size(dict);
    TEST_CHECK(size == 0U);
    contains = tinypy_dict_contains(dict, key_a);
    TEST_CHECK(contains == 0);

    /* A key whose hash or comparison raises leaves the exception raised. */
    unhashable = tinypy_list_from_items(vm, NULL, 0U);
    TEST_CHECK(tinypy_dict_get(dict, unhashable) == NULL);
    TEST_CHECK(tinypy_vm_has_error(vm) != 0);
    tinypy_vm_clear_error(vm);
    TEST_CHECK(tinypy_dict_get_optional(dict, unhashable) == NULL);
    TEST_CHECK(tinypy_vm_has_error(vm) != 0);
    tinypy_vm_clear_error(vm);
    contains = tinypy_dict_contains(dict, unhashable);
    TEST_CHECK(contains == 0);
    TEST_CHECK(tinypy_vm_has_error(vm) != 0);
    tinypy_vm_clear_error(vm);
    tinypy_dict_set(dict, unhashable, value_one);
    size = tinypy_dict_size(dict);
    TEST_CHECK(size == 0U);
    TEST_CHECK(tinypy_vm_has_error(vm) != 0);
    tinypy_vm_clear_error(vm);
    tinypy_dict_delete(dict, unhashable);
    TEST_CHECK(tinypy_vm_has_error(vm) != 0);
    tinypy_vm_clear_error(vm);

    tinypy_release(unhashable);
    tinypy_release(dict_equal);
    tinypy_release(value_two);
    tinypy_release(value_one);
    tinypy_release(key_a_equal);
    tinypy_release(key_a);
    tinypy_release(dict);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_type_class_runtime(void) {
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_error_t *error = NULL;
    tinypy_value_t *integer = NULL;
    tinypy_value_t *instance = NULL;
    tinypy_value_t *attribute = NULL;
    tinypy_value_t *replacement = NULL;
    tinypy_value_t *direct_attribute = NULL;
    tinypy_value_t *type_dict = NULL;
    tinypy_value_t *key = NULL;
    tinypy_value_t *borrowed = NULL;
    tinypy_type_t *metaclass = NULL;
    tinypy_type_t *base_a = NULL;
    tinypy_type_t *base_b = NULL;
    tinypy_type_t *child = NULL;
    tinypy_type_t *left = NULL;
    tinypy_type_t *right = NULL;
    tinypy_type_t *invalid = NULL;
    const tinypy_type_t *integer_type;
    const tinypy_type_t *object_type;
    const tinypy_type_t *type_type;
    const tinypy_type_t *observed_type;
    const tinypy_type_t *bases[2];
    size_t allocation_calls;
    size_t probe;
    size_t size;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    integer = tinypy_integer_from_i64(vm, 42);
    integer_type = tinypy_object_type(integer);
    TEST_CHECK(integer_type != NULL);
    object_type = tinypy_type_base(integer_type);
    type_type = tinypy_type_metaclass(integer_type);
    TEST_CHECK(object_type != NULL);
    TEST_CHECK(type_type != NULL);
    TEST_CHECK(tinypy_type_metaclass(type_type) == type_type);

    bases[0] = type_type;
    metaclass = tinypy_type_new(
        vm, "Meta", 4U, bases, 1U, NULL, NULL, &error);
    TEST_CHECK(metaclass != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_type_is_subtype(metaclass, type_type) == 1);
    TEST_CHECK(tinypy_type_metaclass(metaclass) == type_type);

    bases[0] = object_type;
    base_a = tinypy_type_new(
        vm, "BaseA", 5U, bases, 1U, metaclass, NULL, &error);
    TEST_CHECK(base_a != NULL);
    TEST_CHECK(error == NULL);
    base_b = tinypy_type_new(
        vm, "BaseB", 5U, bases, 1U, metaclass, NULL, &error);
    TEST_CHECK(base_b != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_type_metaclass(base_a) == metaclass);
    TEST_CHECK(tinypy_object_type(tinypy_type_as_value(base_a)) == metaclass);

    bases[0] = base_a;
    bases[1] = base_b;
    child = tinypy_type_new(
        vm, "Child", 5U, bases, 2U, NULL, NULL, &error);
    TEST_CHECK(child != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_type_metaclass(child) == metaclass);
    TEST_CHECK(tinypy_type_is_subtype(child, base_a) == 1);
    TEST_CHECK(tinypy_type_is_subtype(child, base_b) == 1);
    TEST_CHECK(tinypy_type_is_subtype(child, object_type) == 1);
    size = tinypy_type_bases_size(child);
    TEST_CHECK(size == 2U);
    observed_type = tinypy_type_base_at(child, 0U);
    TEST_CHECK(observed_type == base_a);
    observed_type = tinypy_type_base_at(child, 1U);
    TEST_CHECK(observed_type == base_b);
    size = tinypy_type_mro_size(child);
    TEST_CHECK(size == 4U);
    observed_type = tinypy_type_mro_at(child, 0U);
    TEST_CHECK(observed_type == child);
    observed_type = tinypy_type_mro_at(child, 1U);
    TEST_CHECK(observed_type == base_a);
    observed_type = tinypy_type_mro_at(child, 2U);
    TEST_CHECK(observed_type == base_b);
    observed_type = tinypy_type_mro_at(child, 3U);
    TEST_CHECK(observed_type == object_type);

    attribute = tinypy_integer_from_i64(vm, 7);
    tinypy_type_set_attr(base_a, "answer", 6U, attribute);
    borrowed = tinypy_type_get_attr(child, "answer", 6U);
    TEST_CHECK(borrowed == attribute);
    TEST_CHECK(tinypy_type_get_attr(child, "missing", 7U) == NULL);
    replacement = tinypy_integer_from_i64(vm, 8);
    tinypy_type_set_attr(base_a, "answer", 6U, replacement);
    borrowed = tinypy_type_get_attr(child, "answer", 6U);
    TEST_CHECK(borrowed == replacement);

    type_dict = (tinypy_value_t *)tinypy_type_dict(base_a);
    tinypy_retain(type_dict);
    TEST_CHECK(type_dict != NULL);
    TEST_CHECK(error == NULL);
    direct_attribute = tinypy_integer_from_i64(vm, 9);
    key = tinypy_string_from_bytes(vm, "missing", 7U);
    tinypy_dict_set(type_dict, key, direct_attribute);
    borrowed = tinypy_type_get_attr(child, "missing", 7U);
    TEST_CHECK(borrowed == direct_attribute);
    tinypy_dict_delete(type_dict, key);
    TEST_CHECK(tinypy_type_get_attr(child, "missing", 7U) == NULL);
    tinypy_dict_set(type_dict, key, direct_attribute);
    TEST_CHECK(tinypy_type_get_attr(child, "missing", 7U) == direct_attribute);
    tinypy_release(key);
    key = tinypy_string_from_bytes(vm, "answer", 6U);
    tinypy_dict_set(type_dict, key, direct_attribute);
    borrowed = tinypy_type_get_attr(child, "answer", 6U);
    TEST_CHECK(borrowed == direct_attribute);
    tinypy_dict_delete(type_dict, key);
    TEST_CHECK(tinypy_type_get_attr(child, "answer", 6U) == NULL);
    tinypy_dict_set(type_dict, key, direct_attribute);
    TEST_CHECK(tinypy_type_get_attr(child, "answer", 6U) == direct_attribute);
    tinypy_release(key);
    tinypy_release(type_dict);

    instance = tinypy_instance_new(child);
    TEST_CHECK(tinypy_typeof(instance) == TINYPY_VALUE_INSTANCE);
    TEST_CHECK(tinypy_object_type(instance) == child);
    tinypy_value_t *type_value = tinypy_type_as_value(child);
    (void)tinypy_hash(type_value);
    (void)tinypy_hash(instance);
    TEST_CHECK(tinypy_instance_dict(instance) == NULL);
    borrowed = tinypy_instance_get_attr(instance, "answer", 6U);
    TEST_CHECK(borrowed == direct_attribute);
    TEST_CHECK(tinypy_instance_get_attr(instance, "missing", 7U) == direct_attribute);
    tinypy_instance_set_attr(instance, "answer", 6U, integer);
    TEST_CHECK(tinypy_instance_dict(instance) != NULL);
    TEST_CHECK(tinypy_typeof(tinypy_instance_dict(instance)) == TINYPY_VALUE_DICT);
    borrowed = tinypy_instance_get_attr(instance, "answer", 6U);
    TEST_CHECK(borrowed == integer);
    TEST_CHECK(tinypy_object_has_attr(instance, "answer", 6U) != 0);
    TEST_CHECK(tinypy_object_has_attr(instance, "absent", 6U) == 0);
    key = tinypy_string_from_bytes(vm, "answer", 6U);
    TEST_CHECK(tinypy_object_has_attr_value(instance, key) != 0);
    tinypy_release(key);
    key = NULL;
    allocation_calls = state.allocation_calls;
    for (probe = 0U; probe != 100000U; ++probe) {
        TEST_CHECK(tinypy_object_has_attr(instance, "absent", 6U) == 0);
    }
    TEST_CHECK(state.allocation_calls == allocation_calls);
    TEST_CHECK(tinypy_vm_has_error(vm) == 0);

    bases[0] = base_a;
    bases[1] = base_b;
    left = tinypy_type_new(
        vm, "Left", 4U, bases, 2U, NULL, NULL, &error);
    TEST_CHECK(left != NULL);
    TEST_CHECK(error == NULL);
    bases[0] = base_b;
    bases[1] = base_a;
    right = tinypy_type_new(
        vm, "Right", 5U, bases, 2U, NULL, NULL, &error);
    TEST_CHECK(right != NULL);
    TEST_CHECK(error == NULL);
    bases[0] = left;
    bases[1] = right;
    invalid = tinypy_type_new(
        vm, "Invalid", 7U, bases, 2U, NULL, NULL, &error);
    TEST_CHECK(invalid == NULL);
    TEST_CHECK(error != NULL);
    TEST_CHECK(tinypy_error_kind(error) == TINYPY_ERROR_TYPE);
    tinypy_error_release(error);
    error = NULL;

    tinypy_release(instance);
    tinypy_release(direct_attribute);
    tinypy_release(replacement);
    tinypy_release(attribute);
    tinypy_value_t *type_value_2 = tinypy_type_as_value(right);
    tinypy_release(type_value_2);
    tinypy_value_t *type_value_3 = tinypy_type_as_value(left);
    tinypy_release(type_value_3);
    tinypy_value_t *type_value_4 = tinypy_type_as_value(child);
    tinypy_release(type_value_4);
    tinypy_value_t *type_value_5 = tinypy_type_as_value(base_b);
    tinypy_release(type_value_5);
    tinypy_value_t *type_value_6 = tinypy_type_as_value(base_a);
    tinypy_release(type_value_6);
    tinypy_value_t *type_value_7 = tinypy_type_as_value(metaclass);
    tinypy_release(type_value_7);
    tinypy_release(integer);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_code_object_runtime(void) {
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm;
    tinypy_value_t *bytecode;
    tinypy_value_t *constant;
    tinypy_value_t *consts;
    tinypy_value_t *empty_tuple;
    tinypy_value_t *filename;
    tinypy_value_t *name;
    tinypy_value_t *lnotab;
    tinypy_value_t *code;
    tinypy_value_t *items[1];
    const void *bytes;
    const char *type_name;
    size_t size;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    bytecode = tinypy_string_from_bytes(vm, "d\0\0S", 4U);
    constant = tinypy_integer_from_i64(vm, INT64_C(123));
    items[0] = constant;
    consts = tinypy_tuple_from_items(vm, items, 1U);
    empty_tuple = tinypy_tuple_from_items(vm, NULL, 0U);
    filename = tinypy_string_from_bytes(vm, "test.py", 7U);
    name = tinypy_string_from_bytes(vm, "module", 6U);
    lnotab = tinypy_string_from_bytes(vm, "", 0U);

    code = tinypy_code_new(0, 0, 1, TINYPY_CODE_NO_FREE, bytecode, consts, empty_tuple, empty_tuple, empty_tuple, empty_tuple, filename, name, 1, lnotab);
    TEST_CHECK(tinypy_typeof(code) == TINYPY_VALUE_CODE);
    TEST_CHECK(tinypy_code_arg_count(code) == 0);
    TEST_CHECK(tinypy_code_local_count(code) == 0);
    TEST_CHECK(tinypy_code_stack_size(code) == 1);
    TEST_CHECK(tinypy_code_flags(code) == TINYPY_CODE_NO_FREE);
    TEST_CHECK(tinypy_code_first_line_number(code) == 1);
    TEST_CHECK(tinypy_code_consts(code) == consts);
    TEST_CHECK(tinypy_code_names(code) == empty_tuple);
    TEST_CHECK(tinypy_code_varnames(code) == empty_tuple);
    TEST_CHECK(tinypy_code_freevars(code) == empty_tuple);
    TEST_CHECK(tinypy_code_cellvars(code) == empty_tuple);
    TEST_CHECK(tinypy_code_filename(code) == filename);
    TEST_CHECK(tinypy_code_name(code) == name);
    TEST_CHECK(tinypy_code_lnotab(code) == lnotab);
    tinypy_value_t *code_bytecode = tinypy_code_bytecode(code);
    bytes = tinypy_string_view(code_bytecode, &size);
    TEST_CHECK(size == 4U);
    TEST_CHECK(memcmp(bytes, "d\0\0S", 4U) == 0);
    const tinypy_type_t *type = tinypy_object_type(code);
    type_name = tinypy_type_name(type, &size);
    TEST_CHECK(size == 4U);
    TEST_CHECK(memcmp(type_name, "code", 4U) == 0);

    tinypy_release(lnotab);
    tinypy_release(name);
    tinypy_release(filename);
    tinypy_release(empty_tuple);
    tinypy_release(consts);
    tinypy_release(constant);
    tinypy_release(bytecode);
    TEST_CHECK(tinypy_integer_as_i64(tinypy_tuple_get(tinypy_code_consts(code), 0U)) == INT64_C(123));
    tinypy_release(code);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* A failure while the method calls of a function's verified code are searched
   leaves the code without caches, and the next call verifies it again. The
   copy of the bytecode made for that search is the one allocation of exactly
   the bytecode's size. */
static int32_t __test_code_cache_allocation_failure(void) {
    static const char source[] =
        "line = '    value = int(abs(value)) + len(str(value)) - len(repr(value))\\n'\n"
        "exec 'def scan(value):\\n' + line * 16 + '    return value\\n'\n";
    test_allocator_state_t state;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    size_t bytecode_size;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "scan.py", 7U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "scan", 4U);
    tinypy_value_t *function = tinypy_dict_get(globals, name);
    TEST_CHECK(function != NULL);
    tinypy_value_t *code = tinypy_function_code(function);
    tinypy_value_t *code_bytecode = tinypy_code_bytecode(code);
    (void)tinypy_string_view(code_bytecode, &bytecode_size);
    tinypy_value_t *argument = tinypy_integer_from_i64(vm, 5);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &argument, 1U);

    state.fail_allocation_size = bytecode_size;
    result = tinypy_call(function, args, NULL, &error);
    state.fail_allocation_size = 0U;
    TEST_CHECK(result == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    result = tinypy_call(function, args, NULL, &error);
    TEST_CHECK(result != NULL && error == NULL);
    TEST_CHECK(tinypy_integer_as_i64(result) == 5);
    tinypy_release(result);

    tinypy_release(args);
    tinypy_release(argument);
    tinypy_release(name);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_eval_frame_runtime(void) {
    //////////////////////////////////////////////////////////////////////////
    static const uint8_t instructions[] = {
        100U, 0U, 0U,
        90U, 0U, 0U,
        101U, 0U, 0U,
        100U, 1U, 0U,
        102U, 2U, 0U,
        83U};
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm;
    tinypy_error_t *error = NULL;
    tinypy_value_t *bytecode;
    tinypy_value_t *integer;
    tinypy_value_t *none;
    tinypy_value_t *consts;
    tinypy_value_t *name;
    tinypy_value_t *names;
    tinypy_value_t *empty_tuple;
    tinypy_value_t *filename;
    tinypy_value_t *module_name;
    tinypy_value_t *lnotab;
    tinypy_value_t *code;
    tinypy_value_t *globals;
    tinypy_value_t *result;
    tinypy_value_t *const_items[2];
    tinypy_value_t *name_items[1];

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    bytecode = tinypy_string_from_bytes(vm, instructions, sizeof(instructions));
    integer = tinypy_integer_from_i64(vm, 41);
    none = tinypy_none_get(vm);
    const_items[0] = integer;
    const_items[1] = none;
    consts = tinypy_tuple_from_items(vm, const_items, 2U);
    name = tinypy_string_from_bytes(vm, "answer", 6U);
    name_items[0] = name;
    names = tinypy_tuple_from_items(vm, name_items, 1U);
    empty_tuple = tinypy_tuple_from_items(vm, NULL, 0U);
    filename = tinypy_string_from_bytes(vm, "eval.py", 7U);
    module_name = tinypy_string_from_bytes(vm, "<module>", 8U);
    lnotab = tinypy_string_from_bytes(vm, "", 0U);
    code = tinypy_code_new(0, 0, 2, TINYPY_CODE_NO_FREE, bytecode, consts, names, empty_tuple, empty_tuple, empty_tuple, filename, module_name, 1, lnotab);
    globals = tinypy_dict_new(vm);

    TEST_CHECK(tinypy_vm_current_frame(vm) == NULL);
    result = tinypy_eval_code(code, globals, NULL, &error);
    TEST_CHECK(result != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_vm_current_frame(vm) == NULL);
    TEST_CHECK(tinypy_typeof(result) == TINYPY_VALUE_TUPLE);
    TEST_CHECK(tinypy_tuple_size(result) == 2U);
    TEST_CHECK(tinypy_integer_as_i64(tinypy_tuple_get(result, 0U)) == 41);
    TEST_CHECK(tinypy_typeof(tinypy_tuple_get(result, 1U)) == TINYPY_VALUE_NONE);
    TEST_CHECK(tinypy_dict_contains(globals, name) != 0);
    TEST_CHECK(tinypy_dict_get(globals, name) == integer);

    tinypy_release(result);
    tinypy_release(globals);
    tinypy_release(code);
    tinypy_release(lnotab);
    tinypy_release(module_name);
    tinypy_release(filename);
    tinypy_release(empty_tuple);
    tinypy_release(names);
    tinypy_release(name);
    tinypy_release(consts);
    tinypy_release(none);
    tinypy_release(integer);
    tinypy_release(bytecode);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_function_call_runtime(void) {
    static const uint8_t function_instructions[] = {124U, 0U, 0U, 124U, 1U, 0U, 102U, 2U, 0U, 83U};
    static const uint8_t module_instructions[] = {100U, 0U, 0U, 100U, 1U, 0U, 132U, 1U, 0U, 90U, 0U, 0U, 101U, 0U, 0U, 100U, 2U, 0U, 131U, 1U, 0U, 83U};
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm;
    tinypy_error_t *error = NULL;
    tinypy_value_t *empty;
    tinypy_value_t *filename;
    tinypy_value_t *function_name;
    tinypy_value_t *argument_a_name;
    tinypy_value_t *argument_b_name;
    tinypy_value_t *varnames;
    tinypy_value_t *function_bytecode;
    tinypy_value_t *function_lnotab;
    tinypy_value_t *function_code;
    tinypy_value_t *default_value;
    tinypy_value_t *argument_value;
    tinypy_value_t *module_consts;
    tinypy_value_t *binding_name;
    tinypy_value_t *module_names;
    tinypy_value_t *module_name;
    tinypy_value_t *module_bytecode;
    tinypy_value_t *module_lnotab;
    tinypy_value_t *module_code;
    tinypy_value_t *globals;
    tinypy_value_t *result;
    tinypy_value_t *function_value;
    tinypy_type_t *class_type;
    tinypy_type_t *copied_method_type;
    const tinypy_type_t *object_type;
    const tinypy_type_t *class_bases[1];
    tinypy_value_t *class_value;
    tinypy_value_t *instance;
    tinypy_value_t *method;
    tinypy_value_t *method_code;
    tinypy_value_t *method_args;
    tinypy_value_t *method_result;
    tinypy_value_t *unbound_method;
    tinypy_value_t *copied_method_type_value;
    tinypy_value_t *copied_method_instance;
    tinypy_value_t *copied_method;
    tinypy_value_t *copied_method_result;
    tinypy_value_t *method_arg_items[1];
    tinypy_value_t *varname_items[2];
    tinypy_value_t *const_items[3];
    tinypy_value_t *name_items[1];

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    empty = tinypy_tuple_from_items(vm, NULL, 0U);
    filename = tinypy_string_from_bytes(vm, "function.py", 11U);
    function_name = tinypy_string_from_bytes(vm, "pair", 4U);
    argument_a_name = tinypy_string_from_bytes(vm, "a", 1U);
    argument_b_name = tinypy_string_from_bytes(vm, "b", 1U);
    varname_items[0] = argument_a_name;
    varname_items[1] = argument_b_name;
    varnames = tinypy_tuple_from_items(vm, varname_items, 2U);
    function_bytecode = tinypy_string_from_bytes(vm, function_instructions, sizeof(function_instructions));
    function_lnotab = tinypy_string_from_bytes(vm, "", 0U);
    function_code = tinypy_code_new(2, 2, 2, TINYPY_CODE_OPTIMIZED | TINYPY_CODE_NEW_LOCALS | TINYPY_CODE_NO_FREE, function_bytecode, empty, empty, varnames, empty, empty, filename, function_name, 1, function_lnotab);
    default_value = tinypy_integer_from_i64(vm, 20);
    argument_value = tinypy_integer_from_i64(vm, 10);
    const_items[0] = default_value;
    const_items[1] = function_code;
    const_items[2] = argument_value;
    module_consts = tinypy_tuple_from_items(vm, const_items, 3U);
    binding_name = tinypy_string_from_bytes(vm, "pair", 4U);
    name_items[0] = binding_name;
    module_names = tinypy_tuple_from_items(vm, name_items, 1U);
    module_name = tinypy_string_from_bytes(vm, "<module>", 8U);
    module_bytecode = tinypy_string_from_bytes(vm, module_instructions, sizeof(module_instructions));
    module_lnotab = tinypy_string_from_bytes(vm, "", 0U);
    module_code = tinypy_code_new(0, 0, 2, TINYPY_CODE_NO_FREE, module_bytecode, module_consts, module_names, empty, empty, empty, filename, module_name, 1, module_lnotab);
    globals = tinypy_dict_new(vm);

    result = tinypy_eval_code(module_code, globals, NULL, &error);
    TEST_CHECK(result != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_typeof(result) == TINYPY_VALUE_TUPLE);
    TEST_CHECK(tinypy_tuple_size(result) == 2U);
    TEST_CHECK(tinypy_integer_as_i64(tinypy_tuple_get(result, 0U)) == 10);
    TEST_CHECK(tinypy_integer_as_i64(tinypy_tuple_get(result, 1U)) == 20);
    TEST_CHECK(tinypy_dict_contains(globals, binding_name) != 0);
    function_value = tinypy_dict_get(globals, binding_name);
    TEST_CHECK(tinypy_typeof(function_value) == TINYPY_VALUE_FUNCTION);
    TEST_CHECK(tinypy_function_code(function_value) == function_code);
    TEST_CHECK(tinypy_tuple_size(tinypy_function_defaults(function_value)) == 1U);
    TEST_CHECK(tinypy_integer_as_i64(tinypy_tuple_get(tinypy_function_defaults(function_value), 0U)) == 20);

    const tinypy_type_t *type = tinypy_object_type(argument_value);
    object_type = tinypy_type_base(type);
    class_bases[0] = object_type;
    class_type = tinypy_type_new(vm, "PairOwner", 9U, class_bases, 1U, NULL, NULL, &error);
    TEST_CHECK(class_type != NULL);
    TEST_CHECK(error == NULL);
    tinypy_type_set_attr(class_type, "pair", 4U, function_value);
    class_value = tinypy_type_as_value(class_type);
    instance = tinypy_call(class_value, empty, NULL, &error);
    TEST_CHECK(instance != NULL);
    TEST_CHECK(error == NULL);
    method = tinypy_object_get_attr(instance, "pair", 4U, &error);
    TEST_CHECK(method != NULL);
    TEST_CHECK(tinypy_typeof(method) == TINYPY_VALUE_METHOD);
    TEST_CHECK(tinypy_method_self(method) == instance);
    TEST_CHECK(tinypy_method_function(method) == function_value);
    method_code = tinypy_object_get_attr(method, "func_code", 9U, &error);
    TEST_CHECK(method_code == function_code);
    TEST_CHECK(error == NULL);
    method_arg_items[0] = argument_value;
    method_args = tinypy_tuple_from_items(vm, method_arg_items, 1U);
    method_result = tinypy_call(method, method_args, NULL, &error);
    TEST_CHECK(method_result != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_tuple_size(method_result) == 2U);
    TEST_CHECK(tinypy_tuple_get(method_result, 0U) == instance);
    TEST_CHECK(tinypy_integer_as_i64(tinypy_tuple_get(method_result, 1U)) == 10);

    unbound_method = tinypy_object_get_attr(class_value, "pair", 4U, &error);
    TEST_CHECK(unbound_method != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_typeof(unbound_method) == TINYPY_VALUE_METHOD);
    TEST_CHECK(tinypy_method_self(unbound_method) == NULL);
    class_bases[0] = class_type;
    copied_method_type = tinypy_type_new(vm, "CopiedMethodOwner", 17U, class_bases, 1U, NULL, NULL, &error);
    TEST_CHECK(copied_method_type != NULL);
    TEST_CHECK(error == NULL);
    tinypy_type_set_attr(copied_method_type, "pair", 4U, unbound_method);
    copied_method_type_value = tinypy_type_as_value(copied_method_type);
    copied_method_instance = tinypy_call(copied_method_type_value, empty, NULL, &error);
    TEST_CHECK(copied_method_instance != NULL);
    TEST_CHECK(error == NULL);
    copied_method = tinypy_object_get_attr(copied_method_instance, "pair", 4U, &error);
    TEST_CHECK(copied_method != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_typeof(copied_method) == TINYPY_VALUE_METHOD);
    TEST_CHECK(tinypy_method_self(copied_method) == copied_method_instance);
    copied_method_result = tinypy_call(copied_method, method_args, NULL, &error);
    TEST_CHECK(copied_method_result != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_tuple_size(copied_method_result) == 2U);
    TEST_CHECK(tinypy_tuple_get(copied_method_result, 0U) == copied_method_instance);
    TEST_CHECK(tinypy_integer_as_i64(tinypy_tuple_get(copied_method_result, 1U)) == 10);

    tinypy_release(copied_method_result);
    tinypy_release(copied_method);
    tinypy_release(copied_method_instance);
    tinypy_release(copied_method_type_value);
    tinypy_release(unbound_method);
    tinypy_release(method_result);
    tinypy_release(method_args);
    tinypy_release(method_code);
    tinypy_release(method);
    tinypy_release(instance);
    tinypy_release(class_value);
    tinypy_release(result);
    /* A Python function owns its globals and this module dict owns the
     * function. With the configured no-GC runtime, module teardown must
     * explicitly break that ownership cycle before releasing the dict. */
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_release(module_code);
    tinypy_release(module_lnotab);
    tinypy_release(module_bytecode);
    tinypy_release(module_name);
    tinypy_release(module_names);
    tinypy_release(binding_name);
    tinypy_release(module_consts);
    tinypy_release(argument_value);
    tinypy_release(default_value);
    tinypy_release(function_code);
    tinypy_release(function_lnotab);
    tinypy_release(function_bytecode);
    tinypy_release(varnames);
    tinypy_release(argument_b_name);
    tinypy_release(argument_a_name);
    tinypy_release(function_name);
    tinypy_release(filename);
    tinypy_release(empty);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_exception_state_lost_native(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = (tinypy_vm_t *)user_data;
    tinypy_error_t *error = NULL;
    tinypy_value_t *none = tinypy_none_get(vm);
    tinypy_value_t *empty = tinypy_tuple_from_items(vm, NULL, 0U);
    tinypy_value_t *result = tinypy_call(none, empty, NULL, &error);

    (void)function;
    (void)args;
    (void)kwargs;

    if (result != NULL) {
        tinypy_release(result);
    }
    tinypy_release(empty);
    tinypy_release(none);
    tinypy_vm_clear_error(vm);
    if (out_error != NULL) {
        *out_error = error;
    }
    else if (error != NULL) {
        tinypy_error_release(error);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_exception_state_lost_runtime(void) {
    static const char source[] =
        "def run(items):\n"
        "    total = 0\n"
        "    for item in items:\n"
        "        try:\n"
        "            lost()\n"
        "        except:\n"
        "            total = total + item\n"
        "    return total\n"
        "outcome = run([1, 2, 3])\n";
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_value_t *builtins;
    tinypy_value_t *name;
    tinypy_value_t *native;
    tinypy_value_t *code;
    tinypy_value_t *globals;
    tinypy_value_t *result;
    tinypy_value_t *outcome_key;
    tinypy_value_t *outcome;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    builtins = tinypy_vm_builtins(vm);
    name = tinypy_string_from_bytes(vm, "lost", 4U);
    native = tinypy_native_function_new(vm, "lost", 4U, &__test_exception_state_lost_native, vm, NULL);
    tinypy_dict_set(builtins, name, native);

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    code = tinypy_compile_source(vm, source, sizeof(source) - 1U, "lost.py", sizeof("lost.py") - 1U, &options, &error);
    TEST_CHECK(code != NULL);
    TEST_CHECK(error == NULL);
    globals = tinypy_dict_new(vm);
    result = tinypy_eval_code(code, globals, NULL, &error);
    TEST_CHECK(result != NULL);
    TEST_CHECK(error == NULL);

    outcome_key = tinypy_string_from_bytes(vm, "outcome", 7U);
    outcome = tinypy_dict_get(globals, outcome_key);
    TEST_CHECK(outcome != NULL);
    TEST_CHECK(tinypy_integer_as_i64(outcome) == 6);

    tinypy_release(outcome_key);
    tinypy_release(result);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_release(code);
    tinypy_dict_delete(builtins, name);
    tinypy_release(native);
    tinypy_release(name);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* Runs source that raises and hands over the diagnostic it leaves. */
static tinypy_error_t *__test_exception_diagnostic(tinypy_vm_t *vm, tinypy_value_t *globals, const char *source, size_t source_size) {
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *code = tinypy_compile_source(vm, source, source_size, "diagnostic.py", sizeof("diagnostic.py") - 1U, &options, &error);
    if (code == NULL) {
        return error;
    }
    tinypy_value_t *result = tinypy_eval_code(code, globals, NULL, &error);
    if (result != NULL) {
        tinypy_release(result);
    }
    tinypy_release(code);
    tinypy_vm_clear_error(vm);
    return error;
}
//////////////////////////////////////////////////////////////////////////
/* The message of a raised exception is rendered once, when it is first read,
   and an unread one is rendered when the VM is destroyed; the kind, mapped
   from the class the exception was raised with, likewise. */
static int32_t __test_exception_diagnostics(void) {
    static const char define[] =
        "calls = []\n"
        "class Failure(Exception):\n"
        "    def __str__(self):\n"
        "        calls.append(1)\n"
        "        return 'calls=%d' % len(calls)\n"
        "class Missing(KeyError):\n"
        "    pass\n";
    static const char raise_failure[] = "raise Failure()\n";
    static const char raise_missing[] = "raise Missing('key')\n";
    test_allocator_state_t state;
    tinypy_error_t *error = NULL;
    size_t size;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_compile_options_t options;
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *code = tinypy_compile_source(vm, define, sizeof(define) - 1U, "diagnostic.py", sizeof("diagnostic.py") - 1U, &options, &error);
    TEST_CHECK(code != NULL && error == NULL);
    tinypy_value_t *result = tinypy_eval_code(code, globals, NULL, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    tinypy_release(code);

    tinypy_error_t *unread = __test_exception_diagnostic(vm, globals, raise_failure, sizeof(raise_failure) - 1U);
    TEST_CHECK(unread != NULL);
    tinypy_error_release(unread);
    tinypy_error_t *read = __test_exception_diagnostic(vm, globals, raise_failure, sizeof(raise_failure) - 1U);
    TEST_CHECK(read != NULL);
    const char *message = tinypy_error_message(read, &size);
    TEST_CHECK(size == 7U && memcmp(message, "calls=1", 7U) == 0);
    message = tinypy_error_message(read, &size);
    TEST_CHECK(size == 7U && memcmp(message, "calls=1", 7U) == 0);
    TEST_CHECK(tinypy_error_kind(read) == TINYPY_ERROR_RUNTIME);
    tinypy_error_release(read);
    tinypy_error_t *missing = __test_exception_diagnostic(vm, globals, raise_missing, sizeof(raise_missing) - 1U);
    TEST_CHECK(missing != NULL && tinypy_error_kind(missing) == TINYPY_ERROR_KEY);
    message = tinypy_error_message(missing, &size);
    TEST_CHECK(size == 5U && memcmp(message, "'key'", 5U) == 0);
    TEST_CHECK(tinypy_error_kind(missing) == TINYPY_ERROR_KEY);
    tinypy_error_release(missing);

    /* sys.exit leaves its argument unnormalized, and the message reads as
       the SystemExit it makes. */
    static const char *const exits[] = {"import sys\nsys.exit()\n", "import sys\nsys.exit(())\n", "import sys\nsys.exit((3,))\n", "import sys\nsys.exit((1, 2))\n"};
    static const char *const exit_messages[] = {"", "", "3", "(1, 2)"};
    for (size_t index = 0U; index < sizeof(exits) / sizeof(exits[0]); ++index) {
        size_t exit_size = strlen(exits[index]);
        tinypy_error_t *exit_error = __test_exception_diagnostic(vm, globals, exits[index], exit_size);
        TEST_CHECK(exit_error != NULL);
        const char *exit_message = tinypy_error_message(exit_error, &size);
        size_t expected_size = strlen(exit_messages[index]);
        TEST_CHECK(size == expected_size && memcmp(exit_message, exit_messages[index], size) == 0);
        tinypy_error_release(exit_error);
    }

    /* Like PyErr_GivenExceptionMatches, matching never raises: the
       unnormalized value matches nothing but itself, and the pending
       SystemExit stays. */
    static const char exit_code[] = "import sys\nsys.exit(3)\n";
    code = tinypy_compile_source(vm, exit_code, sizeof(exit_code) - 1U, "diagnostic.py", sizeof("diagnostic.py") - 1U, &options, &error);
    TEST_CHECK(code != NULL && error == NULL);
    result = tinypy_eval_code(code, globals, NULL, &error);
    TEST_CHECK(result == NULL && error != NULL);
    tinypy_error_release(error);
    error = NULL;
    tinypy_release(code);
    tinypy_value_t *system_exit_key = tinypy_string_from_bytes(vm, "SystemExit", 10U);
    tinypy_value_t *builtins = tinypy_vm_builtins(vm);
    tinypy_value_t *system_exit = tinypy_dict_get(builtins, system_exit_key);
    tinypy_value_t *raised_type = tinypy_vm_raised_exception_type(vm);
    tinypy_value_t *raised = tinypy_vm_raised_exception(vm);
    TEST_CHECK(system_exit != NULL && raised_type == system_exit && tinypy_typeof(raised) == TINYPY_VALUE_INTEGER);
    TEST_CHECK(tinypy_exception_matches(raised, system_exit, &error) == 0 && error == NULL);
    TEST_CHECK(tinypy_exception_matches(raised, raised, &error) == 1 && error == NULL);
    TEST_CHECK(tinypy_exception_matches(raised_type, system_exit, &error) == 1 && error == NULL);
    TEST_CHECK(tinypy_vm_raised_exception_type(vm) == system_exit && tinypy_vm_raised_exception(vm) == raised);
    tinypy_release(system_exit_key);
    tinypy_vm_clear_error(vm);

    tinypy_error_t *first = __test_exception_diagnostic(vm, globals, raise_failure, sizeof(raise_failure) - 1U);
    tinypy_error_t *second = __test_exception_diagnostic(vm, globals, raise_failure, sizeof(raise_failure) - 1U);
    missing = __test_exception_diagnostic(vm, globals, raise_missing, sizeof(raise_missing) - 1U);
    TEST_CHECK(first != NULL && second != NULL && missing != NULL);

    /* The class and its globals stay alive until the VM collects them, after
       the pending messages are rendered. */
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    const char *first_message = tinypy_error_message(first, &size);
    TEST_CHECK(size == 7U && memcmp(first_message, "calls=", 6U) == 0);
    const char *second_message = tinypy_error_message(second, &size);
    TEST_CHECK(size == 7U && memcmp(second_message, "calls=", 6U) == 0);
    TEST_CHECK(first_message[6] + second_message[6] == '2' + '3');
    TEST_CHECK(tinypy_error_kind(missing) == TINYPY_ERROR_KEY);
    const char *missing_message = tinypy_error_message(missing, &size);
    TEST_CHECK(size == 5U && memcmp(missing_message, "'key'", 5U) == 0);
    tinypy_error_release(first);
    tinypy_error_release(second);
    tinypy_error_release(missing);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_exception_missing_attribute(tinypy_value_t *instance, void *payload, tinypy_value_t *name, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(instance);

    (void)payload;
    (void)name;
    (void)user_data;
    (void)out_error;
    tinypy_vm_raise_error(vm, TINYPY_ERROR_ATTRIBUTE, "no such native attribute");
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* Diagnostics of exceptions that Python code raises have the kind of their
   class, and tinypy_vm_normalize_error normalizes the raised exception as
   PyErr_NormalizeException does. */
static int32_t __test_exception_normalization(void) {
    static const char define[] =
        "import sys\n"
        "class A(Exception):\n"
        "    def __new__(cls, *args):\n"
        "        return Exception.__new__(B, *args)\n"
        "class B(A):\n"
        "    pass\n"
        "class C(Exception):\n"
        "    pass\n"
        "calls = []\n"
        "class D(Exception):\n"
        "    def __new__(cls, *args):\n"
        "        calls.append(args)\n"
        "        if len(calls) > 1:\n"
        "            raise KeyError('again')\n"
        "        return C('c')\n"
        "class Missing(AttributeError):\n"
        "    pass\n"
        "class Dynamic(object):\n"
        "    def __getattr__(self, name):\n"
        "        raise AttributeError(name)\n";
    static const char *const attribute_sources[] = {"object().missing\n", "raise AttributeError('raised')\n", "raise Missing('subclass')\n", "Dynamic().missing\n", "Native().missing\n"};
    static const char raise_subclass[] = "raise A, 'x'\n";
    static const char raise_failing[] = "raise D, 'x'\n";
    static const char raise_exit[] = "sys.exit(5)\n";
    test_allocator_state_t state;
    tinypy_compile_options_t options;
    tinypy_native_type_spec_t spec;
    tinypy_error_t *error = NULL;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_native_type_spec_init(&spec);
    spec.get_attribute = &__test_exception_missing_attribute;
    tinypy_type_t *native = tinypy_native_type_new(vm, "Native", 6U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(native != NULL && error == NULL);
    tinypy_value_t *native_value = tinypy_type_as_value(native);
    tinypy_value_t *native_key = tinypy_string_from_bytes(vm, "Native", 6U);
    tinypy_dict_set(globals, native_key, native_value);
    tinypy_release(native_key);
    tinypy_release(native_value);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *code = tinypy_compile_source(vm, define, sizeof(define) - 1U, "normalize.py", sizeof("normalize.py") - 1U, &options, &error);
    TEST_CHECK(code != NULL && error == NULL);
    tinypy_value_t *result = tinypy_eval_code(code, globals, NULL, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    tinypy_release(code);

    for (size_t index = 0U; index < sizeof(attribute_sources) / sizeof(attribute_sources[0]); ++index) {
        size_t source_size = strlen(attribute_sources[index]);
        tinypy_error_t *attribute_error = __test_exception_diagnostic(vm, globals, attribute_sources[index], source_size);
        TEST_CHECK(attribute_error != NULL && tinypy_error_kind(attribute_error) == TINYPY_ERROR_ATTRIBUTE);
        tinypy_error_release(attribute_error);
    }

    TEST_CHECK(tinypy_vm_normalize_error(vm, &error) != 0 && error == NULL && tinypy_vm_has_error(vm) == 0);

    /* The class of an instance that the call of the raised class returned
       names the exception once it is normalized. */
    tinypy_value_t *a_key = tinypy_string_from_bytes(vm, "A", 1U);
    tinypy_value_t *b_key = tinypy_string_from_bytes(vm, "B", 1U);
    tinypy_value_t *class_a = tinypy_dict_get(globals, a_key);
    tinypy_value_t *class_b = tinypy_dict_get(globals, b_key);
    tinypy_release(a_key);
    tinypy_release(b_key);
    code = tinypy_compile_source(vm, raise_subclass, sizeof(raise_subclass) - 1U, "normalize.py", sizeof("normalize.py") - 1U, &options, &error);
    TEST_CHECK(code != NULL && error == NULL);
    TEST_CHECK(tinypy_eval_code(code, globals, NULL, &error) == NULL && error != NULL);
    tinypy_error_release(error);
    error = NULL;
    tinypy_release(code);
    tinypy_value_t *raised = tinypy_vm_raised_exception(vm);
    tinypy_value_t *traceback = tinypy_vm_raised_traceback(vm);
    TEST_CHECK(tinypy_vm_raised_exception_type(vm) == class_a && traceback != NULL);
    TEST_CHECK(tinypy_vm_normalize_error(vm, &error) != 0 && error == NULL);
    TEST_CHECK(tinypy_vm_raised_exception_type(vm) == class_b && tinypy_vm_raised_exception(vm) == raised && tinypy_vm_raised_traceback(vm) == traceback);
    TEST_CHECK(tinypy_vm_normalize_error(vm, &error) != 0 && error == NULL && tinypy_vm_raised_exception(vm) == raised);
    tinypy_vm_clear_error(vm);

    /* The code of sys.exit becomes the argument of a SystemExit. */
    code = tinypy_compile_source(vm, raise_exit, sizeof(raise_exit) - 1U, "normalize.py", sizeof("normalize.py") - 1U, &options, &error);
    TEST_CHECK(code != NULL && error == NULL);
    TEST_CHECK(tinypy_eval_code(code, globals, NULL, &error) == NULL && error != NULL);
    tinypy_error_release(error);
    error = NULL;
    tinypy_release(code);
    TEST_CHECK(tinypy_typeof(tinypy_vm_raised_exception(vm)) == TINYPY_VALUE_INTEGER);
    TEST_CHECK(tinypy_vm_normalize_error(vm, &error) != 0 && error == NULL);
    tinypy_value_t *exit_code = tinypy_object_get_attr(tinypy_vm_raised_exception(vm), "code", 4U, &error);
    TEST_CHECK(exit_code != NULL && error == NULL && tinypy_integer_as_i64(exit_code) == 5);
    tinypy_release(exit_code);
    tinypy_vm_clear_error(vm);

    /* A failed call leaves its own exception pending. */
    code = tinypy_compile_source(vm, raise_failing, sizeof(raise_failing) - 1U, "normalize.py", sizeof("normalize.py") - 1U, &options, &error);
    TEST_CHECK(code != NULL && error == NULL);
    TEST_CHECK(tinypy_eval_code(code, globals, NULL, &error) == NULL && error != NULL);
    tinypy_error_release(error);
    error = NULL;
    tinypy_release(code);
    TEST_CHECK(tinypy_vm_normalize_error(vm, &error) == 0);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_KEY);
    TEST_CHECK(tinypy_vm_has_error(vm) != 0 && tinypy_vm_raised_traceback(vm) != NULL);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);

    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_object_str and tinypy_object_repr report a failed lookup of the
   __str__ or __repr__ of a classic instance through their result, without an
   out_error as with one, and a lookup raising AttributeError falls back to
   the default text, as instance_str and instance_repr do. The built-in
   __str__ and __repr__ of an exception run as the C slots BaseException_str
   and BaseException_repr, with no method to bind, so a host refusing the
   memory of one changes neither text; the held methods empty the free list,
   and a build whose pool passes every request to the host sees the refusal. */
static int32_t __test_representation_errors(void) {
    static const char define[] =
        "class Failing:\n"
        "    def __getattr__(self, name):\n"
        "        if name in ('__str__', '__repr__'):\n"
        "            raise KeyError(name)\n"
        "        raise AttributeError(name)\n"
        "class Missing:\n"
        "    @property\n"
        "    def __repr__(self):\n"
        "        raise AttributeError('repr')\n"
        "    @property\n"
        "    def __str__(self):\n"
        "        raise AttributeError('str')\n"
        "class Raised(ValueError):\n"
        "    pass\n"
        "failing = Failing()\n"
        "missing = Missing()\n"
        "builtin = RuntimeError('cmp')\n"
        "derived = Raised('own', 2)\n"
        "held = [[].append for index in range(64)]\n";
    static const char *const exception_names[] = {"builtin", "derived"};
    static const char *const exception_texts[][2] = {{"cmp", "RuntimeError('cmp',)"}, {"('own', 2)", "Raised('own', 2)"}};
    static const char raise_failing[] = "raise failing\n";
    static const char default_prefix[] = "<__builtin__.Missing instance at 0x";
    static const char failed_message[] = "<exception str() failed>";
    test_allocator_state_t state;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    size_t size;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *code = tinypy_compile_source(vm, define, sizeof(define) - 1U, "representation.py", sizeof("representation.py") - 1U, &options, &error);
    TEST_CHECK(code != NULL && error == NULL);
    tinypy_value_t *result = tinypy_eval_code(code, globals, NULL, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    tinypy_release(code);
    tinypy_value_t *builtins = tinypy_vm_builtins(vm);
    tinypy_value_t *key_error_key = tinypy_string_from_bytes(vm, "KeyError", 8U);
    tinypy_value_t *key_error = tinypy_dict_get(builtins, key_error_key);
    tinypy_release(key_error_key);
    tinypy_value_t *failing_key = tinypy_string_from_bytes(vm, "failing", 7U);
    tinypy_value_t *failing = tinypy_dict_get(globals, failing_key);
    tinypy_release(failing_key);
    tinypy_value_t *missing_key = tinypy_string_from_bytes(vm, "missing", 7U);
    tinypy_value_t *missing = tinypy_dict_get(globals, missing_key);
    tinypy_release(missing_key);
    TEST_CHECK(key_error != NULL && failing != NULL && missing != NULL);

    TEST_CHECK(tinypy_object_str(failing, NULL) == NULL);
    TEST_CHECK(tinypy_vm_has_error(vm) != 0 && tinypy_vm_raised_exception_type(vm) == key_error);
    tinypy_vm_clear_error(vm);
    TEST_CHECK(tinypy_object_repr(failing, NULL) == NULL);
    TEST_CHECK(tinypy_vm_has_error(vm) != 0 && tinypy_vm_raised_exception_type(vm) == key_error);
    tinypy_vm_clear_error(vm);
    TEST_CHECK(tinypy_object_str(failing, &error) == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_KEY);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);

    tinypy_value_t *texts[2];
    texts[0] = tinypy_object_str(missing, NULL);
    texts[1] = tinypy_object_repr(missing, &error);
    TEST_CHECK(texts[0] != NULL && texts[1] != NULL && error == NULL && tinypy_vm_has_error(vm) == 0);
    for (size_t index = 0U; index < 2U; ++index) {
        const char *text = tinypy_string_view(texts[index], &size);
        TEST_CHECK(size > sizeof(default_prefix) - 1U && memcmp(text, default_prefix, sizeof(default_prefix) - 1U) == 0);
        tinypy_release(texts[index]);
    }

    for (size_t index = 0U; index < sizeof(exception_names) / sizeof(exception_names[0]); ++index) {
        tinypy_value_t *exception_key = tinypy_string_from_bytes(vm, exception_names[index], strlen(exception_names[index]));
        tinypy_value_t *exception = tinypy_dict_get(globals, exception_key);
        tinypy_release(exception_key);
        TEST_CHECK(exception != NULL);
        state.fail_allocation_size = sizeof(tinypy_native_function_object_t);
        texts[0] = tinypy_object_str(exception, &error);
        texts[1] = tinypy_object_repr(exception, &error);
        state.fail_allocation_size = 0U;
        TEST_CHECK(texts[0] != NULL && texts[1] != NULL && error == NULL && tinypy_vm_has_error(vm) == 0);
        for (size_t text_index = 0U; text_index < 2U; ++text_index) {
            const char *text = tinypy_string_view(texts[text_index], &size);
            const char *expected = exception_texts[index][text_index];
            TEST_CHECK(size == strlen(expected) && memcmp(text, expected, size) == 0);
            tinypy_release(texts[text_index]);
        }
    }

    /* The diagnostic of an uncaught exception renders str() without an
       out_error, as PyErr_Display does. */
    tinypy_error_t *diagnostic = __test_exception_diagnostic(vm, globals, raise_failing, sizeof(raise_failing) - 1U);
    TEST_CHECK(diagnostic != NULL);
    const char *message = tinypy_error_message(diagnostic, &size);
    TEST_CHECK(size == sizeof(failed_message) - 1U && memcmp(message, failed_message, size) == 0);
    tinypy_error_release(diagnostic);

    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_operator_numeric_runtime(void) {
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm;
    tinypy_error_t *error = NULL;
    tinypy_value_t *maximum;
    tinypy_value_t *one;
    tinypy_value_t *overflow;
    tinypy_value_t *huge_shift;
    tinypy_value_t *shifted;
    tinypy_value_t *large;
    tinypy_value_t *negative_large;
    tinypy_value_t *three;
    tinypy_value_t *quotient;
    tinypy_value_t *remainder;
    tinypy_value_t *product;
    tinypy_value_t *reconstructed;
    tinypy_value_t *negative_quotient;
    tinypy_value_t *negative_remainder;
    tinypy_value_t *negative_product;
    tinypy_value_t *negative_reconstructed;
    tinypy_value_t *minus_seven;
    tinypy_value_t *floor_result;
    tinypy_value_t *modulo_result;
    tinypy_value_t *left_text;
    tinypy_value_t *right_text;
    tinypy_value_t *joined_text;
    tinypy_value_t *repeat_count;
    tinypy_value_t *limited_result;
    tinypy_value_t *single_list;
    tinypy_value_t *medium_tuple;
    tinypy_value_t *left_unicode;
    tinypy_value_t *right_unicode;
    tinypy_value_t *joined_unicode;
    tinypy_value_t *dictionary;
    tinypy_value_t *zero;
    tinypy_value_t *none;
    tinypy_value_t *allocation_long;
    tinypy_value_t *allocation_result;
    const uint16_t *digits;
    const void *bytes;
    size_t digit_count;
    size_t byte_size;
    int32_t sign;
    uint16_t large_digits[5] = {0U, 0U, 0U, 0U, 1024U};
    uint16_t allocation_digits[600];

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    config.max_heap_bytes = 8U * 1024U * 1024U;
    vm = tinypy_vm_create(&config);
    maximum = tinypy_integer_from_i64(vm, INT64_MAX);
    one = tinypy_integer_from_i64(vm, 1);
    overflow = tinypy_add(maximum, one, &error);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_typeof(overflow) == TINYPY_VALUE_LONG);
    digits = tinypy_long_base15_view(overflow, &sign, &digit_count);
    TEST_CHECK(sign == 1);
    TEST_CHECK(digit_count == 5U);
    TEST_CHECK(digits[4] == 8U);

    huge_shift = tinypy_integer_from_i64(vm, INT64_C(134217728));
    shifted = tinypy_left_shift(one, huge_shift, &error);
    TEST_CHECK(shifted == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    tinypy_release(huge_shift);

    huge_shift = tinypy_integer_from_i64(vm, INT64_MAX);
    shifted = tinypy_left_shift(one, huge_shift, &error);
    TEST_CHECK(shifted == NULL);
    TEST_CHECK(error != NULL);
    TEST_CHECK(tinypy_error_kind(error) == ((uint64_t)SIZE_MAX >= (uint64_t)INT64_MAX ? TINYPY_ERROR_MEMORY : TINYPY_ERROR_OVERFLOW));
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    tinypy_release(huge_shift);

    large = tinypy_long_from_base15_digits(vm, 1, large_digits, 5U);
    negative_large = tinypy_long_from_base15_digits(vm, -1, large_digits, 5U);
    three = tinypy_integer_from_i64(vm, 3);
    quotient = tinypy_floor_divide(large, three, &error);
    remainder = tinypy_remainder(large, three, &error);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_typeof(quotient) == TINYPY_VALUE_LONG);
    TEST_CHECK(tinypy_long_as_i64(remainder) == 1);
    product = tinypy_multiply(quotient, three, &error);
    reconstructed = tinypy_add(product, remainder, &error);
    TEST_CHECK(tinypy_equal(reconstructed, large) != 0);

    negative_quotient = tinypy_floor_divide(negative_large, three, &error);
    negative_remainder = tinypy_remainder(negative_large, three, &error);
    TEST_CHECK(tinypy_long_as_i64(negative_remainder) == 2);
    negative_product = tinypy_multiply(negative_quotient, three, &error);
    negative_reconstructed = tinypy_add(negative_product, negative_remainder, &error);
    TEST_CHECK(tinypy_equal(negative_reconstructed, negative_large) != 0);

    minus_seven = tinypy_integer_from_i64(vm, -7);
    floor_result = tinypy_floor_divide(minus_seven, three, &error);
    modulo_result = tinypy_remainder(minus_seven, three, &error);
    TEST_CHECK(tinypy_integer_as_i64(floor_result) == -3);
    TEST_CHECK(tinypy_integer_as_i64(modulo_result) == 2);
    left_text = tinypy_string_from_bytes(vm, "tiny", 4U);
    right_text = tinypy_string_from_bytes(vm, "py", 2U);
    joined_text = tinypy_add(left_text, right_text, &error);
    bytes = tinypy_string_view(joined_text, &byte_size);
    TEST_CHECK(byte_size == 6U);
    TEST_CHECK(memcmp(bytes, "tinypy", 6U) == 0);
    repeat_count = tinypy_integer_from_i64(vm, INT64_C(3000000));
    limited_result = tinypy_multiply(left_text, repeat_count, &error);
    TEST_CHECK(limited_result == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    single_list = tinypy_list_from_items(vm, &one, 1U);
    limited_result = tinypy_multiply(single_list, repeat_count, &error);
    TEST_CHECK(limited_result == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    limited_result = tinypy_inplace_multiply(single_list, repeat_count, &error);
    TEST_CHECK(limited_result == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
    TEST_CHECK(tinypy_list_size(single_list) == 1U);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    tinypy_release(single_list);
    tinypy_release(repeat_count);
    medium_tuple = tinypy_tuple_new(vm, 32U);
    state.fail_allocation_above = 512U;
    limited_result = tinypy_add(medium_tuple, medium_tuple, &error);
    TEST_CHECK(limited_result == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    state.fail_allocation_above = 0U;
    tinypy_release(medium_tuple);
    (void)memset(allocation_digits, 0, sizeof(allocation_digits));
    allocation_digits[sizeof(allocation_digits) / sizeof(allocation_digits[0]) - 1U] = 1U;
    allocation_long = tinypy_long_from_base15_digits(vm, 1, allocation_digits, sizeof(allocation_digits) / sizeof(allocation_digits[0]));
    state.fail_allocation_above = 512U;
    allocation_result = tinypy_multiply(allocation_long, allocation_long, &error);
    TEST_CHECK(allocation_result == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    allocation_result = tinypy_object_repr(allocation_long, &error);
    TEST_CHECK(allocation_result == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    state.fail_allocation_above = 0U;
    tinypy_release(allocation_long);
    left_unicode = tinypy_unicode_from_utf8(vm, "Contract", 8U);
    right_unicode = tinypy_unicode_from_utf8(vm, "_Cooldown", 9U);
    joined_unicode = tinypy_add(left_unicode, right_text, &error);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_typeof(joined_unicode) == TINYPY_VALUE_UNICODE);
    bytes = tinypy_unicode_utf8_view(joined_unicode, &byte_size, &digit_count);
    TEST_CHECK(byte_size == 10U);
    TEST_CHECK(memcmp(bytes, "Contractpy", 10U) == 0);
    tinypy_release(joined_unicode);
    joined_unicode = tinypy_add(left_text, right_unicode, &error);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_typeof(joined_unicode) == TINYPY_VALUE_UNICODE);
    bytes = tinypy_unicode_utf8_view(joined_unicode, &byte_size, &digit_count);
    TEST_CHECK(byte_size == 13U);
    TEST_CHECK(memcmp(bytes, "tiny_Cooldown", 13U) == 0);
    dictionary = tinypy_dict_new(vm);
    zero = tinypy_integer_from_i64(vm, 0);
    none = tinypy_none_get(vm);
    TEST_CHECK(tinypy_compare_bool(none, zero, TINYPY_COMPARE_LESS, &error) == 1);
    TEST_CHECK(tinypy_compare_bool(zero, dictionary, TINYPY_COMPARE_LESS, &error) == 1);
    TEST_CHECK(tinypy_compare_bool(dictionary, zero, TINYPY_COMPARE_LESS, &error) == 0);
    TEST_CHECK(error == NULL);

    tinypy_value_t *complex_base = tinypy_complex_from_doubles(vm, 1.0, 2.0);
    tinypy_value_t *complex_exponent = tinypy_complex_from_doubles(vm, 3.0, 0.0);
    tinypy_value_t *complex_result = tinypy_power(complex_base, complex_exponent, &error);
    double complex_real;
    double complex_imaginary;
    TEST_CHECK(error == NULL);
    tinypy_complex_as_doubles(complex_result, &complex_real, &complex_imaginary);
    TEST_CHECK(fabs(complex_real + 11.0) < 1e-12);
    TEST_CHECK(fabs(complex_imaginary + 2.0) < 1e-12);

    tinypy_value_t *complex_zero = tinypy_complex_from_doubles(vm, 0.0, 0.0);
    tinypy_value_t *complex_zero_power = tinypy_power(complex_zero, complex_zero, &error);
    TEST_CHECK(error == NULL);
    tinypy_complex_as_doubles(complex_zero_power, &complex_real, &complex_imaginary);
    TEST_CHECK(complex_real == 1.0);
    TEST_CHECK(complex_imaginary == 0.0);

    tinypy_release(complex_zero_power);
    tinypy_release(complex_zero);
    tinypy_release(complex_result);
    tinypy_release(complex_exponent);
    tinypy_release(complex_base);

    tinypy_release(none);
    tinypy_release(zero);
    tinypy_release(dictionary);
    tinypy_release(joined_unicode);
    tinypy_release(right_unicode);
    tinypy_release(left_unicode);
    tinypy_release(joined_text);
    tinypy_release(right_text);
    tinypy_release(left_text);
    tinypy_release(modulo_result);
    tinypy_release(floor_result);
    tinypy_release(minus_seven);
    tinypy_release(negative_reconstructed);
    tinypy_release(negative_product);
    tinypy_release(negative_remainder);
    tinypy_release(negative_quotient);
    tinypy_release(reconstructed);
    tinypy_release(product);
    tinypy_release(remainder);
    tinypy_release(quotient);
    tinypy_release(three);
    tinypy_release(negative_large);
    tinypy_release(large);
    tinypy_release(overflow);
    tinypy_release(one);
    tinypy_release(maximum);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_native_payload_t {
    int32_t value;
} test_native_payload_t;
//////////////////////////////////////////////////////////////////////////
typedef struct test_native_state_t {
    tinypy_vm_t *vm;
    tinypy_type_t *base_type;
    tinypy_type_t *subtype;
    int32_t constructed;
    int32_t finalized;
    int32_t attribute_calls;
    int32_t hash_error;
    int32_t compare_calls;
    int32_t compare_order[8];
    size_t compare_order_count;
    int32_t binary_calls;
    int32_t reflected_calls;
    int32_t inplace_calls;
} test_native_state_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __test_native_construct(tinypy_value_t *instance, void *payload, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;
    test_native_state_t *state = (test_native_state_t *)user_data;

    (void)instance;
    (void)kwargs;
    (void)out_error;
    if (tinypy_tuple_size(args) != 0U) {
        return TINYPY_FALSE;
    }
    native_payload->value = 73;
    state->constructed += 1;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __test_native_finalize(tinypy_value_t *instance, void *payload, void *user_data) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;
    test_native_state_t *state = (test_native_state_t *)user_data;

    (void)instance;
    (void)native_payload;
    state->finalized += 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_repr(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;

    (void)user_data;
    (void)out_error;
    if (native_payload->value != 73) {
        return NULL;
    }
    tinypy_vm_t *value_vm = tinypy_value_vm(instance);
    tinypy_value_t *return_value_1 = tinypy_string_from_bytes(value_vm, "native-73", 9U);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_compare(tinypy_value_t *instance, void *payload, tinypy_value_t *other, tinypy_compare_operation_e operation, void *user_data, tinypy_error_t **out_error) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;
    test_native_state_t *state = (test_native_state_t *)user_data;
    int32_t equal;

    (void)out_error;
    state->compare_calls += 1;
    if (state->compare_order_count < sizeof(state->compare_order) / sizeof(state->compare_order[0])) {
        const tinypy_type_t *type = tinypy_object_type(instance);

        state->compare_order[state->compare_order_count] = type == state->subtype ? 2 : (type == state->base_type ? 1 : 0);
        state->compare_order_count += 1U;
    }
    if (tinypy_typeof(other) != TINYPY_VALUE_INTEGER) {
        tinypy_value_t *return_value_1 = tinypy_not_implemented_get(state->vm);
        return return_value_1;
    }
    equal = native_payload->value == tinypy_integer_as_i64(other) ? INT32_C(1) : INT32_C(0);
    if (operation == TINYPY_COMPARE_EQUAL) {
        tinypy_value_t *return_value_2 = tinypy_bool_from_i32(state->vm, equal);
        return return_value_2;
    }
    if (operation == TINYPY_COMPARE_NOT_EQUAL) {
        tinypy_value_t *return_value_3 = tinypy_bool_from_i32(state->vm, equal == 0 ? INT32_C(1) : INT32_C(0));
        return return_value_3;
    }
    tinypy_value_t *return_value_4 = tinypy_not_implemented_get(state->vm);
    return return_value_4;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __test_native_hash(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;
    test_native_state_t *state = (test_native_state_t *)user_data;

    (void)instance;
    (void)out_error;
    if (state->hash_error != 0) {
        tinypy_vm_raise_error(state->vm, TINYPY_ERROR_RUNTIME, "native hash failure");
        return (tinypy_hash_t)0;
    }
    return (tinypy_hash_t)native_payload->value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_call(tinypy_value_t *instance, void *payload, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;
    test_native_state_t *state = (test_native_state_t *)user_data;

    (void)instance;
    (void)kwargs;
    (void)out_error;
    if (tinypy_tuple_size(args) != 1U || tinypy_typeof(tinypy_tuple_get(args, 0U)) != TINYPY_VALUE_INTEGER) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(state->vm, native_payload->value + tinypy_integer_as_i64(tinypy_tuple_get(args, 0U)));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_negative(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;
    test_native_state_t *state = (test_native_state_t *)user_data;

    (void)instance;
    (void)out_error;
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(state->vm, -(int64_t)native_payload->value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_absolute(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;
    test_native_state_t *state = (test_native_state_t *)user_data;
    int64_t result = native_payload->value < 0 ? -(int64_t)native_payload->value : (int64_t)native_payload->value;

    (void)instance;
    (void)out_error;
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(state->vm, result);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
typedef enum test_native_binary_operation_e {
    TEST_NATIVE_BINARY_ADD,
    TEST_NATIVE_BINARY_SUBTRACT,
    TEST_NATIVE_BINARY_MULTIPLY,
    TEST_NATIVE_BINARY_DIVIDE
} test_native_binary_operation_e;
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_binary_result(tinypy_value_t *instance, void *payload, tinypy_value_t *other, void *user_data, tinypy_error_t **out_error, test_native_binary_operation_e operation, int32_t reflected) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;
    test_native_state_t *state = (test_native_state_t *)user_data;
    int64_t first;
    int64_t second;
    int64_t result;

    (void)instance;
    if (tinypy_typeof(other) != TINYPY_VALUE_INTEGER) {
        tinypy_value_t *return_value_1 = tinypy_not_implemented_get(state->vm);
        return return_value_1;
    }
    first = reflected != 0 ? tinypy_integer_as_i64(other) : native_payload->value;
    second = reflected != 0 ? native_payload->value : tinypy_integer_as_i64(other);
    if (reflected != 0) {
        state->reflected_calls += 1;
    }
    else {
        state->binary_calls += 1;
    }
    switch (operation) {
    case TEST_NATIVE_BINARY_ADD:
        result = first + second;
        break;
    case TEST_NATIVE_BINARY_SUBTRACT:
        result = first - second;
        break;
    case TEST_NATIVE_BINARY_MULTIPLY:
        result = first * second;
        break;
    case TEST_NATIVE_BINARY_DIVIDE:
        if (second == 0) {
            tinypy_vm_raise_error(state->vm, TINYPY_ERROR_ZERO_DIVISION, "native division by zero");
            (void)out_error;
            return NULL;
        }
        result = first / second;
        break;
    default:
        return NULL;
    }
    tinypy_value_t *return_value_2 = tinypy_integer_from_i64(state->vm, result);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
#define TEST_NATIVE_BINARY_CALLBACK(__name, operation, reflected) \
    static tinypy_value_t *__name(tinypy_value_t *instance, void *payload, tinypy_value_t *other, void *user_data, tinypy_error_t **out_error) { \
        tinypy_value_t *return_value = __test_native_binary_result(instance, payload, other, user_data, out_error, operation, reflected); \
        return return_value; \
    }

TEST_NATIVE_BINARY_CALLBACK(__test_native_add, TEST_NATIVE_BINARY_ADD, INT32_C(0))
TEST_NATIVE_BINARY_CALLBACK(__test_native_subtract, TEST_NATIVE_BINARY_SUBTRACT, INT32_C(0))
TEST_NATIVE_BINARY_CALLBACK(__test_native_multiply, TEST_NATIVE_BINARY_MULTIPLY, INT32_C(0))
TEST_NATIVE_BINARY_CALLBACK(__test_native_divide, TEST_NATIVE_BINARY_DIVIDE, INT32_C(0))
TEST_NATIVE_BINARY_CALLBACK(__test_native_reflected_add, TEST_NATIVE_BINARY_ADD, INT32_C(1))
TEST_NATIVE_BINARY_CALLBACK(__test_native_reflected_subtract, TEST_NATIVE_BINARY_SUBTRACT, INT32_C(1))
TEST_NATIVE_BINARY_CALLBACK(__test_native_reflected_multiply, TEST_NATIVE_BINARY_MULTIPLY, INT32_C(1))
TEST_NATIVE_BINARY_CALLBACK(__test_native_reflected_divide, TEST_NATIVE_BINARY_DIVIDE, INT32_C(1))

#undef TEST_NATIVE_BINARY_CALLBACK
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_inplace_result(tinypy_value_t *instance, void *payload, tinypy_value_t *other, void *user_data, tinypy_error_t **out_error, test_native_binary_operation_e operation) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;
    test_native_state_t *state = (test_native_state_t *)user_data;
    int64_t value;

    if (tinypy_typeof(other) != TINYPY_VALUE_INTEGER) {
        tinypy_value_t *return_value_1 = tinypy_not_implemented_get(state->vm);
        return return_value_1;
    }
    value = tinypy_integer_as_i64(other);
    switch (operation) {
    case TEST_NATIVE_BINARY_ADD:
        native_payload->value += (int32_t)value;
        break;
    case TEST_NATIVE_BINARY_SUBTRACT:
        native_payload->value -= (int32_t)value;
        break;
    case TEST_NATIVE_BINARY_MULTIPLY:
        native_payload->value *= (int32_t)value;
        break;
    case TEST_NATIVE_BINARY_DIVIDE:
        if (value == 0) {
            tinypy_vm_raise_error(state->vm, TINYPY_ERROR_ZERO_DIVISION, "native inplace division by zero");
            (void)out_error;
            return NULL;
        }
        native_payload->value /= (int32_t)value;
        break;
    }
    state->inplace_calls += 1;
    tinypy_retain(instance);
    return instance;
}
//////////////////////////////////////////////////////////////////////////
#define TEST_NATIVE_INPLACE_CALLBACK(__name, operation) \
    static tinypy_value_t *__name(tinypy_value_t *instance, void *payload, tinypy_value_t *other, void *user_data, tinypy_error_t **out_error) { \
        tinypy_value_t *return_value = __test_native_inplace_result(instance, payload, other, user_data, out_error, operation); \
        return return_value; \
    }

TEST_NATIVE_INPLACE_CALLBACK(__test_native_inplace_add, TEST_NATIVE_BINARY_ADD)
TEST_NATIVE_INPLACE_CALLBACK(__test_native_inplace_subtract, TEST_NATIVE_BINARY_SUBTRACT)
TEST_NATIVE_INPLACE_CALLBACK(__test_native_inplace_multiply, TEST_NATIVE_BINARY_MULTIPLY)
TEST_NATIVE_INPLACE_CALLBACK(__test_native_inplace_divide, TEST_NATIVE_BINARY_DIVIDE)

#undef TEST_NATIVE_INPLACE_CALLBACK
//////////////////////////////////////////////////////////////////////////
static int32_t __test_native_attribute_name_equal(tinypy_value_t *name, const char *expected, size_t expected_size) {
    const void *bytes;
    size_t size;

    if (tinypy_typeof(name) == TINYPY_VALUE_STRING) {
        bytes = tinypy_string_view(name, &size);
    }
    else if (tinypy_typeof(name) == TINYPY_VALUE_UNICODE) {
        size_t code_point_count;

        bytes = tinypy_unicode_utf8_view(name, &size, &code_point_count);
    }
    else {
        return INT32_C(0);
    }
    int32_t return_value_1 = size == expected_size && (size == 0U || memcmp(bytes, expected, size) == 0) ? INT32_C(1) : INT32_C(0);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_get_attribute(tinypy_value_t *instance, void *payload, tinypy_value_t *name, void *user_data, tinypy_error_t **out_error) {
    test_native_state_t *state = (test_native_state_t *)user_data;

    (void)instance;
    (void)payload;
    (void)out_error;
    state->attribute_calls += 1;
    if (__test_native_attribute_name_equal(name, "present", 7U) != 0) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(state->vm, 73);
        return return_value_1;
    }
    if (__test_native_attribute_name_equal(name, "failure", 7U) != 0) {
        tinypy_vm_raise_error(state->vm, TINYPY_ERROR_VALUE, "native attribute failure");
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_raise_error(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = (tinypy_vm_t *)user_data;

    (void)function;
    (void)args;
    (void)kwargs;
    (void)out_error;
    tinypy_vm_raise_error(vm, TINYPY_ERROR_VALUE, "native callback failure");
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_return_args(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)function;
    (void)kwargs;
    (void)user_data;
    (void)out_error;
    tinypy_retain(args);
    return args;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_native_reduce_state_t {
    tinypy_value_t *arguments[2];
    size_t calls;
} test_native_reduce_state_t;
//////////////////////////////////////////////////////////////////////////
static void __test_native_registration_finalize(void *user_data) {
    size_t *calls = (size_t *)user_data;
    *calls += 1U;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_reduce_retain_args(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    test_native_reduce_state_t *state = (test_native_reduce_state_t *)user_data;

    (void)kwargs;
    if (tinypy_tuple_size(args) != 2U || state->calls >= sizeof(state->arguments) / sizeof(state->arguments[0])) {
        tinypy_vm_raise_error(tinypy_value_vm(function), TINYPY_ERROR_RUNTIME, "unexpected native reduce arguments");
        return NULL;
    }
    tinypy_retain(args);
    state->arguments[state->calls++] = args;
    tinypy_value_t *result = tinypy_add(tinypy_tuple_get(args, 0U), tinypy_tuple_get(args, 1U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_stack_budget(void) {
    static const char source[] =
        "depth = 0\n"
        "def descend(callback):\n"
        "    global depth\n"
        "    depth += 1\n"
        "    return callback(callback)\n"
        "descend(descend)\n";
    static const char recovery[] = "answer = 6 * 7\n";
    size_t profile;

    for (profile = 0U; profile < 3U; ++profile) {
        test_allocator_state_t state;
        tinypy_allocator_t allocator;
        tinypy_vm_config_t config;
        tinypy_compile_options_t options;
        tinypy_vm_t *vm;
        tinypy_error_t *error = NULL;
        tinypy_value_t *code;
        tinypy_value_t *globals;
        tinypy_value_t *result;
        tinypy_value_t *key;

        (void)memset(&state, 0, sizeof(state));
        allocator = __test_make_allocator(&state);
        config = __test_make_config(&allocator);
        if (profile == 0U) {
            config.max_stack_bytes = 64U * 1024U;
        }
        else if (profile == 2U) {
            /* An older config must not read the appended budget field. */
            config.struct_size = (uint32_t)offsetof(tinypy_vm_config_t, max_stack_bytes);
            config.max_stack_bytes = 1U;
        }
        vm = tinypy_vm_create(&config);
        TEST_CHECK(vm != NULL);
        tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
        code = tinypy_compile_source(vm, source, sizeof(source) - 1U, "stack.py", sizeof("stack.py") - 1U, &options, &error);
        TEST_CHECK(code != NULL);
        TEST_CHECK(error == NULL);
        globals = tinypy_dict_new(vm);
        result = tinypy_eval_code(code, globals, NULL, &error);
        TEST_CHECK(result == NULL);
        TEST_CHECK(error != NULL);
        TEST_CHECK(tinypy_error_kind(error) == TINYPY_ERROR_RUNTIME);
        tinypy_error_release(error);
        error = NULL;
        tinypy_vm_clear_error(vm);
        tinypy_release(code);
#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
        if (profile == 0U) {
            key = tinypy_string_from_bytes(vm, "depth", 5U);
            /* The byte budget must stop recursion before the depth limit. */
            TEST_CHECK(tinypy_integer_as_i64(tinypy_dict_get(globals, key)) < 990);
            tinypy_release(key);
        }
#endif
        code = tinypy_compile_source(vm, recovery, sizeof(recovery) - 1U, "recover.py", sizeof("recover.py") - 1U, &options, &error);
        TEST_CHECK(code != NULL);
        result = tinypy_eval_code(code, globals, NULL, &error);
        TEST_CHECK(result != NULL);
        TEST_CHECK(error == NULL);
        key = tinypy_string_from_bytes(vm, "answer", 6U);
        TEST_CHECK(tinypy_integer_as_i64(tinypy_dict_get(globals, key)) == 42);
        tinypy_release(key);
        tinypy_release(result);
        tinypy_release(code);
        tinypy_dict_clear(globals);
        tinypy_release(globals);
        tinypy_vm_destroy(vm);
        TEST_CHECK(state.outstanding_allocations == 0U);
        TEST_CHECK(state.outstanding_bytes == 0U);
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static size_t __test_type_lookup_cache_key_count(const tinypy_vm_t *vm, const tinypy_value_t *key) {
    size_t count = 0U;

    for (size_t index = 0U; index < TINYPY_TYPE_LOOKUP_CACHE_SIZE; ++index) {
        count += vm->type_lookup_cache[index].key == key ? 1U : 0U;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_intern_lifetime(void) {
    static const char source[] =
        "raw_name = '<lambda>'\n"
        "folded_name = '<' + 'lambda>'\n"
        "formatted_name = '<%s>' % 'lambda'\n"
        "top = genexpr = setcomp = dictcomp = 1\n"
        "compiler_cache_probe = 1\n"
        "class SlotName(str):\n"
        "    def __hash__(self):\n"
        "        raise AssertionError('slot name hash callback')\n"
        "    def __eq__(self, other):\n"
        "        raise AssertionError('attribute name equality callback')\n"
        "class Slotted(object):\n"
        "    __slots__ = (SlotName('member'),)\n"
        "slotted = Slotted()\n"
        "slotted.member = 41\n"
        "assert slotted.member == 41\n"
        "assert type(Slotted.__dict__['member'].__name__) is str\n"
        "del slotted, Slotted, SlotName\n"
        "def batch(seed):\n"
        "    for index in xrange(2000):\n"
        "        intern('batch_%d_%d_' % (seed, index) + 'x' * 300)\n"
        "assert batch.func_code is batch.__code__\n"
        "assert getattr(batch, u'func_code') is batch.__code__\n"
        "assert getattr(batch, u'__code__') is batch.func_code\n";
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_value_t *code;
    tinypy_value_t *globals;
    tinypy_value_t *function;
    tinypy_value_t *result;
    size_t warm_bytes = 0U;
    size_t registration_finalizers = 0U;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);

    TEST_CHECK(vm != NULL);
    tinypy_vm_t *other_vm = tinypy_vm_create(&config);
    TEST_CHECK(other_vm != NULL);
    /* Core registration must use the eagerly initialized registry, without
       creating the extension/test-only literal dictionary. */
    TEST_CHECK(vm->internal_strings == NULL && other_vm->internal_strings == NULL);
    TEST_CHECK(vm->internal_func_code_key != vm->internal_special_code_key);
    size_t preset_allocations = state.allocation_calls;
#define TEST_PRESET_NAME(field, name, intern_name) \
    do { \
        tinypy_value_t *preset = vm->field; \
        tinypy_ref_t preset_refs = TINYPY_REFCNT(preset); \
        TEST_CHECK(preset != other_vm->field); \
        TEST_CHECK(TINYPY_STRING_OBJECT(preset)->internal_metadata != NULL); \
        TEST_CHECK(TINYPY_STRING_OBJECT(preset)->internal_metadata == TINYPY_STRING_OBJECT(other_vm->field)->internal_metadata); \
        TEST_CHECK(tinypy_internal_string_is_interned(preset) == intern_name); \
        for (size_t repeat = 0U; repeat < 2U; ++repeat) { \
            tinypy_value_t *owned_name = tinypy_internal_name_from_bytes(vm, name, sizeof(name) - 1U); \
            TEST_CHECK(owned_name == preset); \
            TEST_CHECK(TINYPY_REFCNT(preset) == preset_refs + 1); \
            TEST_CHECK(TINYPY_VALUE_VM(owned_name) == vm); \
            tinypy_release(owned_name); \
            TEST_CHECK(TINYPY_REFCNT(preset) == preset_refs); \
            if (intern_name != 0) { \
                owned_name = tinypy_string_from_bytes(vm, name, sizeof(name) - 1U); \
                TEST_CHECK(owned_name == preset && TINYPY_REFCNT(preset) == preset_refs + 1); \
                tinypy_release(owned_name); \
                owned_name = tinypy_internal_string_from_bytes_checked(vm, name, sizeof(name) - 1U, &error); \
                TEST_CHECK(owned_name == preset && error == NULL); \
                tinypy_release(owned_name); \
                TEST_CHECK(TINYPY_REFCNT(preset) == preset_refs); \
            } \
        } \
    } while (0);
    TINYPY_INTERNAL_KEY_LIST(TEST_PRESET_NAME)
#undef TEST_PRESET_NAME
#define TEST_VM_VALUE(accessor, field) \
    do { \
        tinypy_value_t *preset = &vm->field; \
        tinypy_ref_t preset_refs = TINYPY_REFCNT(preset); \
        tinypy_value_t *owned_value = accessor(vm); \
        TEST_CHECK(owned_value == preset); \
        TEST_CHECK(owned_value != &other_vm->field); \
        TEST_CHECK(TINYPY_REFCNT(preset) == preset_refs + 1); \
        TEST_CHECK(TINYPY_VALUE_VM(owned_value) == vm); \
        tinypy_release(owned_value); \
        TEST_CHECK(TINYPY_REFCNT(preset) == preset_refs); \
    } while (0)
    TEST_VM_VALUE(TINYPY_RET_NONE, none_object.base);
    TEST_VM_VALUE(TINYPY_RET_TRUE, true_object.base);
    TEST_VM_VALUE(TINYPY_RET_FALSE, false_object.base);
    TEST_VM_VALUE(TINYPY_RET_NOT_IMPLEMENTED, not_implemented_object.base);
    TEST_VM_VALUE(TINYPY_RET_ELLIPSIS, ellipsis_object.base);
    TEST_VM_VALUE(TINYPY_RET_EMPTY_TUPLE, empty_tuple_object.base.base);
    TEST_VM_VALUE(TINYPY_RET_EMPTY_STRING, empty_string_object.base.base);
#undef TEST_VM_VALUE
    tinypy_value_t *operands[] = {vm->internal_special_doc_key, vm->internal_special_module_key};
    size_t operand_index = 0U;
    tinypy_ref_t operand_refs = TINYPY_REFCNT(operands[0]);
    tinypy_value_t *owned_operand = TINYPY_RET(operands[operand_index++]);
    TEST_CHECK(operand_index == 1U && owned_operand == operands[0]);
    TEST_CHECK(TINYPY_REFCNT(operands[0]) == operand_refs + 1);
    tinypy_release(owned_operand);
    TEST_CHECK(TINYPY_REFCNT(operands[0]) == operand_refs);
    tinypy_vm_t *vms[] = {vm, other_vm};
    size_t vm_index = 0U;
    tinypy_ref_t none_refs = TINYPY_REFCNT(&vm->none_object.base);
    tinypy_value_t *owned_none = TINYPY_RET_NONE(vms[vm_index++]);
    TEST_CHECK(vm_index == 1U && owned_none == &vm->none_object.base);
    TEST_CHECK(TINYPY_REFCNT(owned_none) == none_refs + 1);
    tinypy_release(owned_none);
    TEST_CHECK(TINYPY_REFCNT(&vm->none_object.base) == none_refs);
    for (size_t index = 0U; index < TINYPY_SPECIAL_OPERATOR_COUNT; ++index) {
        tinypy_value_t *preset = tinypy_internal_object_special_operator_key(vm, index);
        size_t name_size = TINYPY_TEXT_BYTE_SIZE(preset);
        const char *name = (const char *)TINYPY_TEXT_BYTES(preset);
        tinypy_ref_t preset_refs = TINYPY_REFCNT(preset);
        tinypy_value_t *owned_name = tinypy_internal_name_from_bytes(vm, name, name_size);

        TEST_CHECK(owned_name == preset);
        TEST_CHECK(preset != tinypy_internal_object_special_operator_key(other_vm, index));
        TEST_CHECK(tinypy_internal_object_special_operator_key(vm, index) == preset);
        TEST_CHECK(TINYPY_REFCNT(preset) == preset_refs + 1);
        tinypy_release(owned_name);
        TEST_CHECK(TINYPY_REFCNT(preset) == preset_refs);
    }
    TEST_CHECK(state.allocation_calls == preset_allocations);
    size_t intern_used = vm->intern_used;
    for (size_t index = 0U; index < 32U; ++index) {
        char generated[64];
        int32_t generated_size = (int32_t)snprintf(generated, sizeof(generated), "ordinary-uncached-%zu", index);
        TEST_CHECK(generated_size > 1 && (size_t)generated_size < sizeof(generated));
        tinypy_value_t *ordinary = tinypy_string_from_bytes(vm, generated, (size_t)generated_size);
        tinypy_value_t *ordinary_again = tinypy_string_from_bytes(vm, generated, (size_t)generated_size);
        TEST_CHECK(ordinary != ordinary_again && vm->intern_used == intern_used);
        TEST_CHECK(tinypy_internal_string_find(vm, generated, (size_t)generated_size) == NULL);
        tinypy_release(ordinary_again);
        tinypy_release(ordinary);
    }
    tinypy_type_t *buffer_type = &vm->types[TINYPY_VALUE_BUFFER];
    tinypy_value_t *method_key = vm->internal_append_key;
    tinypy_ref_t method_key_refs = TINYPY_REFCNT(method_key);
    size_t method_key_cached = __test_type_lookup_cache_key_count(vm, method_key);
    tinypy_value_t *key_method = tinypy_native_function_new_key(method_key, __test_native_return_args, NULL, NULL);
    TEST_CHECK(TINYPY_NATIVE_FUNCTION_OBJECT(key_method)->name == method_key);
    TEST_CHECK(TINYPY_REFCNT(method_key) == method_key_refs + 1);
    tinypy_type_set_attr_key(buffer_type, method_key, key_method);
    TEST_CHECK(TINYPY_REFCNT(method_key) == method_key_refs + 2);
    TEST_CHECK(tinypy_type_get_attr_key(buffer_type, method_key) == key_method);
    TEST_CHECK(tinypy_type_get_attr(buffer_type, "append", 6U) == key_method);
    TEST_CHECK(key_method->type == vm->native_method_descriptor_type);
    TEST_CHECK(TINYPY_NATIVE_FUNCTION_OBJECT(key_method)->owner == buffer_type);
    tinypy_type_set_attr(buffer_type, "append", 6U, &vm->none_object.base);
    TEST_CHECK(tinypy_type_get_attr_key(buffer_type, method_key) == &vm->none_object.base);
    tinypy_release(key_method);
    /* The dictionary and the type lookup cache entries, one for each version
       of the type unless they share an index, retain the name. */
    size_t method_key_entries = __test_type_lookup_cache_key_count(vm, method_key) - method_key_cached;
    TEST_CHECK(method_key_entries != 0U && TINYPY_REFCNT(method_key) == method_key_refs + 1 + (tinypy_ref_t)method_key_entries);
    TEST_CHECK(vm->intern_used == intern_used);
    tinypy_value_t *buffer_len = tinypy_type_get_attr(&vm->types[TINYPY_VALUE_BUFFER], "__len__", 7U);
    TEST_CHECK(buffer_len != NULL && TINYPY_NATIVE_FUNCTION_OBJECT(buffer_len)->name == vm->internal_special_length_key);
    tinypy_value_t *class_key = tinypy_string_from_bytes(vm, "__class__tail", 9U);
    tinypy_value_t *raw_class_key = tinypy_internal_string_from_bytes_uninterned(vm, "__class__", 9U);
    tinypy_value_t *unicode_class_key = tinypy_unicode_from_utf8(vm, "__class__", 9U);
    TEST_CHECK(class_key == vm->internal_special_class_key && raw_class_key != class_key && unicode_class_key != class_key);
    TEST_CHECK(TINYPY_NAME_EQ(class_key, vm->internal_special_class_key) != 0);
    TEST_CHECK(TINYPY_NAME_EQ(raw_class_key, vm->internal_special_class_key) != 0);
    TEST_CHECK(TINYPY_NAME_EQ(unicode_class_key, vm->internal_special_class_key) != 0);
    TEST_CHECK(TINYPY_NAME_EQ(class_key, vm->internal_special_dict_key) == 0);
    TEST_CHECK(TINYPY_NAME_EQ(&vm->none_object.base, vm->internal_special_class_key) == 0);
    TEST_CHECK(tinypy_internal_string_is_interned(raw_class_key) == 0);
    TEST_CHECK(tinypy_internal_string_is_interned(class_key) != 0);
    tinypy_value_t *builtin_class = tinypy_internal_object_builtin_attribute(&vm->none_object.base, raw_class_key);
    TEST_CHECK(builtin_class == &vm->types[TINYPY_VALUE_NONE].base.base);
    tinypy_release(builtin_class);
    builtin_class = tinypy_internal_object_builtin_attribute(&vm->none_object.base, unicode_class_key);
    TEST_CHECK(builtin_class == &vm->types[TINYPY_VALUE_NONE].base.base);
    tinypy_release(builtin_class);
    tinypy_release(unicode_class_key);
    tinypy_release(raw_class_key);
    tinypy_release(class_key);
    static const char cached_name[] = "runtime_cached_name\0suffix";
    tinypy_value_t *cached = tinypy_string_from_bytes(vm, cached_name, sizeof(cached_name) - 1U);
    TEST_CHECK(tinypy_internal_string_intern(&cached, &error) != 0 && error == NULL);
    tinypy_ref_t cached_refs = TINYPY_REFCNT(cached);
    size_t cached_allocations = state.allocation_calls;
    tinypy_value_t *cached_again = tinypy_string_from_bytes(vm, cached_name, sizeof(cached_name) - 1U);
    TEST_CHECK(cached_again == cached && TINYPY_REFCNT(cached) == cached_refs + 1);
    TEST_CHECK(state.allocation_calls == cached_allocations);
    tinypy_release(cached_again);
    tinypy_release(cached);
    TEST_CHECK(tinypy_internal_string_find(vm, cached_name, sizeof(cached_name) - 1U) == NULL);
    size_t literal_allocations = state.allocation_calls;
    tinypy_ref_t doc_refs = TINYPY_REFCNT(vm->internal_special_doc_key);
    TEST_CHECK(TINYPY_INTERNAL_STRING(vm, "__doc__") == vm->internal_special_doc_key);
    TEST_CHECK(TINYPY_REFCNT(vm->internal_special_doc_key) == doc_refs && state.allocation_calls == literal_allocations);
    TEST_CHECK(vm->internal_strings == NULL && other_vm->internal_strings == NULL);
    tinypy_value_t *private_key = tinypy_string_from_bytes(vm, "private_literal\0tail", 20U);
    TEST_CHECK(tinypy_internal_string_intern(&private_key, &error) != 0 && error == NULL);
    tinypy_ref_t private_refs = TINYPY_REFCNT(private_key);
    TEST_CHECK(TINYPY_INTERNAL_STRING(vm, "private_literal\0tail") == private_key);
    TEST_CHECK(TINYPY_REFCNT(private_key) == private_refs + 1);
    tinypy_release(private_key);
    TEST_CHECK(tinypy_internal_string_find(vm, "private_literal\0tail", 20U) == private_key);
    literal_allocations = state.allocation_calls;
    private_refs = TINYPY_REFCNT(private_key);
    for (size_t repeat = 0U; repeat < 3U; ++repeat) {
        TEST_CHECK(TINYPY_INTERNAL_STRING(vm, "private_literal\0tail") == private_key);
    }
    TEST_CHECK(TINYPY_REFCNT(private_key) == private_refs && state.allocation_calls == literal_allocations);
    TEST_CHECK(TINYPY_INTERNAL_STRING(other_vm, "private_literal\0tail") != private_key);
    vm_index = 0U;
    tinypy_value_t *literal_once = TINYPY_INTERNAL_STRING(vms[vm_index++], "literal_once");
    TEST_CHECK(vm_index == 1U && TINYPY_VALUE_VM(literal_once) == vm);
    tinypy_value_t *literal_top = TINYPY_INTERNAL_STRING(vm, "top");
    TEST_CHECK(literal_top != vm->internal_compiler_symbol_top_name && tinypy_internal_string_is_interned(literal_top) != 0);
    TEST_CHECK(tinypy_internal_string_is_interned(vm->internal_compiler_symbol_top_name) == 0);

    tinypy_value_t *registration_name = tinypy_string_from_bytes(vm, "Registration", 12U);
    TEST_CHECK(tinypy_internal_string_is_interned(registration_name) == 0);
    tinypy_type_t *registration_type = tinypy_type_new_key(registration_name, NULL, 0U, NULL, NULL, &error);
    TEST_CHECK(registration_type != NULL && error == NULL);
    TEST_CHECK(registration_type->name_object == registration_name);
    tinypy_native_type_spec_t registration_spec;
    tinypy_native_type_spec_init(&registration_spec);
    tinypy_type_t *native_key_type = tinypy_native_type_new_key(registration_name, NULL, 0U, NULL, &registration_spec, &error);
    TEST_CHECK(native_key_type != NULL && error == NULL);
    TEST_CHECK(native_key_type->name_object == registration_name);
    tinypy_release(&native_key_type->base.base);
    TEST_CHECK(tinypy_internal_string_is_interned(registration_name) == 0);
    tinypy_release(registration_name);
    tinypy_value_t *registration_keys[] = {
        TINYPY_INTERNAL_STRING(vm, "native_auto"), TINYPY_INTERNAL_STRING(vm, "native_method"),
        TINYPY_INTERNAL_STRING(vm, "native_wrapper"), TINYPY_INTERNAL_STRING(vm, "native_class"),
        TINYPY_INTERNAL_STRING(vm, "native_static"), TINYPY_INTERNAL_STRING(vm, "native_property")};
    /* Automatic wrapper classification reuses canonical keys and also accepts
       raw/Unicode names; embedded NUL bytes prevent a false slot match. */
    static const char wrapper_name[] = "__eq__";
    tinypy_value_t *wrapper_keys[] = {
        TINYPY_RET(vm->internal_special_eq_key),
        tinypy_internal_string_from_bytes_uninterned(vm, wrapper_name, sizeof(wrapper_name) - 1U),
        tinypy_unicode_from_utf8(vm, wrapper_name, sizeof(wrapper_name) - 1U)
    };
    for (size_t index = 0U; index < sizeof(wrapper_keys) / sizeof(wrapper_keys[0]); ++index) {
        tinypy_value_t *wrapper = tinypy_native_function_new_key(vm->internal_special_eq_key, __test_native_return_args, NULL, NULL);
        tinypy_type_set_attr_key(registration_type, wrapper_keys[index], wrapper);
        TEST_CHECK(wrapper->type == vm->native_wrapper_descriptor_type);
        TEST_CHECK(TINYPY_NATIVE_FUNCTION_OBJECT(wrapper)->owner == registration_type);
        tinypy_release(wrapper);
        tinypy_release(wrapper_keys[index]);
    }
    TEST_CHECK(tinypy_internal_dict_delete_optional(vm, registration_type->dict, vm->internal_special_eq_key) != TINYPY_FALSE);
    static const char nonwrapper_name[] = "__eq__\0tail";
    tinypy_value_t *nonwrapper_key = tinypy_internal_string_from_bytes_uninterned(vm, nonwrapper_name, sizeof(nonwrapper_name) - 1U);
    tinypy_value_t *nonwrapper = tinypy_native_function_new_key(nonwrapper_key, __test_native_return_args, NULL, NULL);
    tinypy_type_set_attr_key(registration_type, nonwrapper_key, nonwrapper);
    TEST_CHECK(nonwrapper->type == vm->native_method_descriptor_type);
    TEST_CHECK(tinypy_internal_dict_delete_optional(vm, registration_type->dict, nonwrapper_key) != TINYPY_FALSE);
    tinypy_release(nonwrapper);
    tinypy_release(nonwrapper_key);
    for (size_t index = 0U; index < 3U; ++index) {
        tinypy_internal_type_add_method(registration_type, registration_keys[index], __test_native_return_args, &registration_finalizers, __test_native_registration_finalize, (tinypy_native_descriptor_kind_e)index);
        tinypy_value_t *registered = tinypy_type_get_attr_key(registration_type, registration_keys[index]);
        TEST_CHECK(TINYPY_NATIVE_FUNCTION_OBJECT(registered)->name == registration_keys[index]);
        TEST_CHECK(TINYPY_NATIVE_FUNCTION_OBJECT(registered)->owner == registration_type);
        TEST_CHECK(TINYPY_NATIVE_FUNCTION_OBJECT(registered)->descriptor_kind == (tinypy_native_descriptor_kind_e)index);
    }
    tinypy_internal_type_add_class_method(registration_type, registration_keys[3], __test_native_return_args, &registration_finalizers, __test_native_registration_finalize);
    tinypy_internal_type_add_static_method(registration_type, registration_keys[4], __test_native_return_args, &registration_finalizers, __test_native_registration_finalize);
    tinypy_internal_type_add_property(registration_type, registration_keys[5], __test_native_return_args, &registration_finalizers, __test_native_registration_finalize);
    tinypy_value_t *registration_instance = tinypy_instance_new(registration_type);
    for (size_t index = 0U; index < 6U; ++index) {
        tinypy_value_t *bound = tinypy_object_get_attr_value(registration_instance, registration_keys[index], &error);
        TEST_CHECK(bound != NULL && error == NULL);
        tinypy_value_t *arguments = index == 5U ? TINYPY_RET(bound) : tinypy_call(bound, &vm->empty_tuple_object.base.base, NULL, &error);
        TEST_CHECK(arguments != NULL && error == NULL);
        TEST_CHECK(tinypy_tuple_size(arguments) == (index == 4U ? 0U : 1U));
        if (index != 4U) {
            TEST_CHECK(tinypy_tuple_get(arguments, 0U) == (index == 3U ? &registration_type->base.base : registration_instance));
        }
        tinypy_release(arguments);
        tinypy_release(bound);
    }
    /* Direct instance access keeps raw dictionary/type values. It must not
       bind a method or invoke a property's getter/setter. */
    TEST_CHECK(tinypy_instance_get_attr_key(registration_instance, registration_keys[0]) == tinypy_type_get_attr_key(registration_type, registration_keys[0]));
    tinypy_instance_set_attr_key(registration_instance, registration_keys[5], &vm->true_object.base);
    TEST_CHECK(tinypy_instance_get_attr_key(registration_instance, registration_keys[5]) == &vm->true_object.base);
    TEST_CHECK(tinypy_instance_get_attr(registration_instance, "native_property", 15U) == &vm->true_object.base);
    tinypy_value_t *property_result = tinypy_object_get_attr_value(registration_instance, registration_keys[5], &error);
    TEST_CHECK(property_result != NULL && error == NULL && tinypy_tuple_size(property_result) == 1U);
    TEST_CHECK(tinypy_tuple_get(property_result, 0U) == registration_instance);
    tinypy_release(property_result);

    tinypy_value_t *registration_module_name = tinypy_string_from_bytes(vm, "registration_module", 19U);
    TEST_CHECK(tinypy_internal_string_is_interned(registration_module_name) == 0);
    tinypy_value_t *registration_module = tinypy_module_new_key(registration_module_name);
    TEST_CHECK(tinypy_module_name(registration_module) == registration_module_name);
    TEST_CHECK(tinypy_internal_string_is_interned(registration_module_name) == 0);
    tinypy_release(registration_module_name);
    tinypy_internal_module_add_function(registration_module, literal_once, __test_native_return_args, &registration_finalizers, __test_native_registration_finalize);
    tinypy_value_t *registered_function = tinypy_module_get_value(registration_module, "literal_once", 12U);
    TEST_CHECK(registered_function != NULL && TINYPY_NATIVE_FUNCTION_OBJECT(registered_function)->name == literal_once);
    TEST_CHECK(TINYPY_NATIVE_FUNCTION_OBJECT(registered_function)->module == tinypy_module_name(registration_module));
    TEST_CHECK(tinypy_module_get_value_key(registration_module, literal_once) == registered_function);

    /* Existing keys remain borrowed; embedded NUL bytes are part of the key.
       Replacing a populated value must neither allocate nor retain the key
       again, and the byte and key APIs must address the same entry. */
    tinypy_value_t *binary_key = tinypy_string_from_bytes(vm, "value\0tail", 10U);
    tinypy_ref_t binary_refs = TINYPY_REFCNT(binary_key);
    tinypy_module_add_value_key(registration_module, binary_key, &vm->true_object.base);
    tinypy_instance_set_attr_key(registration_instance, binary_key, &vm->true_object.base);
    TEST_CHECK(TINYPY_REFCNT(binary_key) == binary_refs + 2);
    size_t key_allocations = state.allocation_calls;
    tinypy_module_add_value_key(registration_module, binary_key, &vm->false_object.base);
    tinypy_instance_set_attr_key(registration_instance, binary_key, &vm->false_object.base);
    TEST_CHECK(tinypy_module_get_value_key(registration_module, binary_key) == &vm->false_object.base);
    TEST_CHECK(tinypy_instance_get_attr_key(registration_instance, binary_key) == &vm->false_object.base);
    TEST_CHECK(state.allocation_calls == key_allocations);
    TEST_CHECK(TINYPY_REFCNT(binary_key) == binary_refs + 2);
    TEST_CHECK(tinypy_module_get_value(registration_module, "value\0tail", 10U) == &vm->false_object.base);
    TEST_CHECK(tinypy_instance_get_attr(registration_instance, "value\0tail", 10U) == &vm->false_object.base);
    TEST_CHECK(tinypy_module_get_value_key(registration_module, TINYPY_INTERNAL_STRING(vm, "missing_module_value")) == NULL);
    tinypy_value_t *imported_sys = tinypy_import_module_key(vm->internal_sys_key, NULL, NULL, INT32_C(0), &error);
    TEST_CHECK(imported_sys == vm->sys_module && error == NULL);
    TEST_CHECK(tinypy_module_get_value_key(imported_sys, vm->internal_stdout_key) == tinypy_module_get_value(imported_sys, "stdout", 6U));
    tinypy_release(imported_sys);
    tinypy_release(registration_module);
    tinypy_release(registration_instance);
    TEST_CHECK(TINYPY_REFCNT(binary_key) == binary_refs);
    TEST_CHECK(tinypy_internal_string_is_interned(binary_key) == 0);
    tinypy_release(binary_key);
    tinypy_release(&registration_type->base.base);
    tinypy_vm_destroy(other_vm);
    tinypy_value_t *prefix = tinypy_internal_name_from_bytes(vm, "__doc__tail", 7U);
    tinypy_value_t *extended = tinypy_internal_name_from_bytes(vm, "__doc__tail", 11U);
    size_t extended_size;
    const void *extended_bytes = tinypy_string_view(extended, &extended_size);
    TEST_CHECK(prefix == vm->internal_special_doc_key);
    TEST_CHECK(extended != prefix && extended_size == 11U);
    TEST_CHECK(memcmp(extended_bytes, "__doc__tail", extended_size) == 0);
    tinypy_release(extended);
    tinypy_release(prefix);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    code = tinypy_compile_source(vm, source, sizeof(source) - 1U, "__doc__", 7U, &options, &error);
    TEST_CHECK(code != NULL && error == NULL);
    /* Hash dispatch accepts raw and Unicode names without retaining them or
       invoking Python callbacks, and collisions must verify the full span. */
    static const char varnames_name[] = "co_varnames";
    tinypy_value_t *raw_varnames = tinypy_internal_string_from_bytes_uninterned(vm, varnames_name, sizeof(varnames_name) - 1U);
    tinypy_value_t *unicode_varnames = tinypy_unicode_from_utf8(vm, varnames_name, sizeof(varnames_name) - 1U);
    TEST_CHECK(TINYPY_STRING_OBJECT(raw_varnames)->internal_metadata == NULL);
    TEST_CHECK(TINYPY_STRING_OBJECT(vm->internal_co_varnames_key)->internal_metadata->builtin_attribute_id != 0U);
    TEST_CHECK(TINYPY_STRING_OBJECT(vm->internal_stdout_key)->internal_metadata->builtin_attribute_id == 0U);
    TEST_CHECK(tinypy_internal_object_builtin_attribute(code, vm->internal_stdout_key) == NULL);
    TEST_CHECK(tinypy_internal_object_builtin_attribute(code, vm->internal_func_defaults_key) == NULL);
    tinypy_value_t *varname_keys[] = {vm->internal_co_varnames_key, raw_varnames, unicode_varnames};
    tinypy_ref_t varname_refs[] = {TINYPY_REFCNT(varname_keys[0]), TINYPY_REFCNT(varname_keys[1]), TINYPY_REFCNT(varname_keys[2])};
    size_t dispatch_allocations = state.allocation_calls;
    for (size_t index = 0U; index < sizeof(varname_keys) / sizeof(varname_keys[0]); ++index) {
        tinypy_value_t *varnames = tinypy_internal_object_builtin_attribute(code, varname_keys[index]);
        TEST_CHECK(varnames == tinypy_code_varnames(code));
        TEST_CHECK(TINYPY_REFCNT(varname_keys[index]) == varname_refs[index]);
        tinypy_release(varnames);
    }
    TEST_CHECK(state.allocation_calls == dispatch_allocations);
    tinypy_value_t *interned_varnames = tinypy_internal_string_from_bytes_uninterned(vm, varnames_name, sizeof(varnames_name) - 1U);
    TEST_CHECK(tinypy_internal_string_intern(&interned_varnames, &error) == TINYPY_TRUE && error == NULL);
    TEST_CHECK(interned_varnames == vm->internal_co_varnames_key);
    TEST_CHECK(TINYPY_STRING_OBJECT(interned_varnames)->internal_metadata == TINYPY_STRING_OBJECT(vm->internal_co_varnames_key)->internal_metadata);
    tinypy_release(interned_varnames);

    /* Immutable subtype copies and growable raw strings cannot inherit a
       preset's dispatch metadata, even when they initially share its bytes. */
    const tinypy_type_t *string_base = &vm->types[TINYPY_VALUE_STRING];
    static const char subtype_name[] = "InternalName";
    tinypy_type_t *string_subtype = tinypy_type_new(vm, subtype_name, sizeof(subtype_name) - 1U, &string_base, 1U, NULL, NULL, &error);
    TEST_CHECK(string_subtype != NULL && error == NULL);
    tinypy_value_t *owned_varnames = TINYPY_RET(vm->internal_co_varnames_key);
    tinypy_value_t *subtype_varnames = tinypy_internal_immutable_subclass_copy(string_subtype, owned_varnames, &error);
    TEST_CHECK(subtype_varnames != NULL && error == NULL);
    TEST_CHECK(subtype_varnames->type == string_subtype && TINYPY_STRING_OBJECT(subtype_varnames)->internal_metadata == NULL);
    tinypy_value_t *subtype_result = tinypy_internal_object_builtin_attribute(code, subtype_varnames);
    TEST_CHECK(subtype_result == tinypy_code_varnames(code));
    tinypy_release(subtype_result);
    tinypy_value_t *plain_varnames = tinypy_internal_immutable_subclass_copy(&vm->types[TINYPY_VALUE_STRING], subtype_varnames, &error);
    TEST_CHECK(plain_varnames != NULL && error == NULL && TINYPY_STRING_OBJECT(plain_varnames)->internal_metadata == NULL);
    plain_varnames = tinypy_internal_string_concat_in_place(vm, plain_varnames, (const uint8_t *)"_tail", 5U);
    TEST_CHECK(plain_varnames != NULL && TINYPY_STRING_OBJECT(plain_varnames)->internal_metadata == NULL);
    TEST_CHECK(tinypy_internal_object_builtin_attribute(code, plain_varnames) == NULL);
    tinypy_release(plain_varnames);
    tinypy_release(&string_subtype->base.base);
    tinypy_release(unicode_varnames);
    tinypy_release(raw_varnames);
    size_t dispatch_intern_used = vm->intern_used;
    size_t collision_probes = 0U;
    uint8_t collision_name[10] = {'c', 'o', '_', 'p', 'r', 'o', 'b', 'e', 0U, 0U};
    for (size_t index = 0U; index <= UINT8_MAX; ++index) {
        collision_name[9] = (uint8_t)index;
        uint32_t name_hash = UINT32_C(2166136261);
        for (size_t byte = 0U; byte < sizeof(collision_name); ++byte) {
            name_hash = (name_hash ^ collision_name[byte]) * UINT32_C(16777619);
        }
        size_t slot = (size_t)name_hash & (TINYPY_INTERNAL_KEY_TABLE_SIZE - 1U);
        if (vm->internal_key_table[slot] == NULL) {
            continue;
        }
        collision_probes += 1U;
        tinypy_value_t *collision_key = tinypy_internal_string_from_bytes_uninterned(vm, (const char *)collision_name, sizeof(collision_name));
        tinypy_ref_t collision_refs = TINYPY_REFCNT(collision_key);
        TEST_CHECK(tinypy_internal_object_builtin_attribute(code, collision_key) == NULL);
        TEST_CHECK(TINYPY_REFCNT(collision_key) == collision_refs && vm->intern_used == dispatch_intern_used);
        tinypy_release(collision_key);
    }
    TEST_CHECK(collision_probes != 0U);
    TEST_CHECK(tinypy_internal_string_find(vm, "compiler_cache_probe", 20U) != NULL);
    TEST_CHECK(tinypy_internal_string_is_interned(vm->internal_compiler_symbol_top_name) == 0);
    TEST_CHECK(tinypy_internal_string_is_interned(vm->internal_compiler_symbol_genexpr_name) == 0);
    TEST_CHECK(tinypy_internal_string_is_interned(vm->internal_compiler_symbol_setcomp_name) == 0);
    TEST_CHECK(tinypy_internal_string_is_interned(vm->internal_compiler_symbol_dictcomp_name) == 0);
    TEST_CHECK(tinypy_code_filename(code) != vm->internal_special_doc_key);
    TEST_CHECK(tinypy_internal_string_is_interned(tinypy_code_filename(code)) == 0);
    TEST_CHECK(tinypy_internal_string_is_interned(vm->internal_special_doc_key) != 0);
    uint8_t serialized[4096];
    uint8_t reserialized[4096];
    size_t serialized_size;
    size_t reserialized_size;
    tinypy_value_t *loaded_code = NULL;
    TEST_CHECK(tinypy_marshal_dump_code_v2(code, serialized, sizeof(serialized), &serialized_size, NULL, NULL) == TINYPY_MARSHAL_OK);
    TEST_CHECK(tinypy_marshal_load_code_v2(vm, serialized, serialized_size, NULL, &loaded_code, NULL) == TINYPY_MARSHAL_OK);
    TEST_CHECK(tinypy_internal_string_is_interned(tinypy_code_filename(loaded_code)) == 0);
    TEST_CHECK(tinypy_internal_string_is_interned(vm->internal_special_doc_key) != 0);
    TEST_CHECK(tinypy_marshal_dump_code_v2(loaded_code, reserialized, sizeof(reserialized), &reserialized_size, NULL, NULL) == TINYPY_MARSHAL_OK);
    TEST_CHECK(serialized_size == reserialized_size && memcmp(serialized, reserialized, serialized_size) == 0);
    tinypy_release(loaded_code);
    globals = tinypy_dict_new(vm);
    result = tinypy_eval_code(code, globals, NULL, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "batch", 5U);

    function = tinypy_dict_get(globals, key);
    tinypy_retain(function);
    tinypy_release(key);
    for (int64_t seed = 0; seed < 4; ++seed) {
        tinypy_value_t *argument = tinypy_integer_from_i64(vm, seed);
        tinypy_value_t *args = tinypy_tuple_from_items(vm, &argument, 1U);

        result = tinypy_call(function, args, NULL, &error);
        TEST_CHECK(result != NULL && error == NULL);
        tinypy_release(result);
        tinypy_release(args);
        tinypy_release(argument);
        if (seed == 0) {
            warm_bytes = state.outstanding_bytes;
        }
        else {
            TEST_CHECK(state.outstanding_bytes <= warm_bytes + 32768U);
        }
    }
    tinypy_release(function);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_release(code);
    tinypy_vm_destroy(vm);
    TEST_CHECK(registration_finalizers == 7U);
    TEST_CHECK(state.outstanding_bytes == 0U && state.outstanding_allocations == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* Float text converts and renders the same whatever the C locale. */
static int32_t __test_float_hex_locale(void) {
    static const char source[] =
        "assert float('1.5') == 1.5 and float(u' -2.5e-3 ') == -2.5e-3\n"
        "assert float('0.30000000000000004') == 0.30000000000000004 and float('1e300') == 1e300\n"
        "assert complex('2.5-0.5j') == 2.5 - 0.5j\n"
        "assert (str(2.5), '%.3f' % 2.5, '%g' % 1.5e20, format(1234.5, ',.1f')) == ('2.5', '2.500', '1.5e+20', '1,234.5')\n";
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    char saved_locale[128];
    const char *current_locale = setlocale(LC_NUMERIC, NULL);
    const char *const candidates[] = {"de_DE.UTF-8", "fr_FR.UTF-8", "de_DE", "fr_FR"};

    TEST_CHECK(current_locale != NULL && strlen(current_locale) < sizeof(saved_locale));
    (void)strcpy(saved_locale, current_locale);
    for (size_t index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        if (setlocale(LC_NUMERIC, candidates[index]) != NULL) {
            break;
        }
    }
    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *sample = tinypy_float_from_double(vm, 0.0);
    tinypy_value_t *method = tinypy_object_get_attr(sample, "fromhex", 7U, &error);
    tinypy_value_t *text = tinypy_string_from_bytes(vm, "0x1.8p+1", 8U);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &text, 1U);
    tinypy_value_t *result = tinypy_call(method, args, NULL, &error);

    TEST_CHECK(result != NULL && error == NULL && tinypy_float_as_double(result) == 3.0);
    tinypy_release(result);
    tinypy_release(args);
    tinypy_release(text);
    tinypy_release(method);
    tinypy_release(sample);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *checked = tinypy_exec_source(vm, source, sizeof(source) - 1U, "float_locale.py", 15U, globals, NULL, &options, &error);
    TEST_CHECK(checked != NULL && error == NULL);
    tinypy_release(checked);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(setlocale(LC_NUMERIC, saved_locale) != NULL);
    TEST_CHECK(state.outstanding_bytes == 0U && state.outstanding_allocations == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_subtype_factory_limits(void) {
    static const char source[] =
        "class Text(str): pass\n"
        "class Wide(unicode): pass\n"
        "class Number(long): pass\n"
        "class Record(tuple): pass\n"
        "text = 'x' * 65536\n"
        "wide = u'x' * 65536\n"
        "number = 1L << 131072\n"
        "record = tuple(range(2048))\n";
    static const char *const type_names[] = {"Text", "Wide", "Number", "Record"};
    static const char *const value_names[] = {"text", "wide", "number", "record"};
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *globals = tinypy_dict_new(vm);

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "factory_limits.py", 17U, globals, NULL, &options, &error);

    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    for (size_t index = 0U; index < sizeof(type_names) / sizeof(type_names[0]); ++index) {
        tinypy_value_t *type_key = tinypy_string_from_bytes(vm, type_names[index], strlen(type_names[index]));
        tinypy_value_t *internal_value_key = tinypy_string_from_bytes(vm, value_names[index], strlen(value_names[index]));
        tinypy_value_t *type = tinypy_dict_get(globals, type_key);
        tinypy_value_t *value = tinypy_dict_get(globals, internal_value_key);
        tinypy_value_t *args = tinypy_tuple_from_items(vm, &value, 1U);

        TEST_CHECK(type != NULL && value != NULL);
        state.fail_allocation_above = 4096U;
        result = tinypy_call(type, args, NULL, &error);
        state.fail_allocation_above = 0U;
        TEST_CHECK(result == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
        tinypy_error_release(error);
        error = NULL;
        tinypy_vm_clear_error(vm);
        tinypy_release(args);
        tinypy_release(type_key);
        tinypy_release(internal_value_key);
    }
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_bytes == 0U && state.outstanding_allocations == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* A table that cannot grow after an insertion keeps the dictionary or set as
   it was: repeated failures must not use up the free slots that terminate
   lookups. Only the table allocations are large enough to fail. */
static int32_t __test_container_failed_growth(void) {
    static const char setup_source[] =
        "keys = range(20000)\n"
        "def fill(container, add):\n"
        "    position = 0\n"
        "    try:\n"
        "        while position < len(keys):\n"
        "            add(container, keys[position])\n"
        "            position += 1\n"
        "    except MemoryError:\n"
        "        pass\n"
        "    size = len(container)\n"
        "    failures = 0\n"
        "    while position < len(keys):\n"
        "        try:\n"
        "            add(container, keys[position])\n"
        "        except MemoryError:\n"
        "            failures += 1\n"
        "        position += 1\n"
        "    assert 0 < size < len(keys) and len(container) == size\n"
        "    assert failures == len(keys) - size and -1 not in container\n"
        "def store(values, key):\n"
        "    values[key] = None\n"
        "def add(members, key):\n"
        "    members.add(key)\n";
    static const char fill_source[] =
        "fill({}, store)\n"
        "fill(set(), add)\n";
    test_allocator_state_t state;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *setup_code = tinypy_compile_source(vm, setup_source, sizeof(setup_source) - 1U, "failed_growth.py", sizeof("failed_growth.py") - 1U, &options, &error);
    TEST_CHECK(setup_code != NULL && error == NULL);
    tinypy_value_t *fill_code = tinypy_compile_source(vm, fill_source, sizeof(fill_source) - 1U, "failed_growth.py", sizeof("failed_growth.py") - 1U, &options, &error);
    TEST_CHECK(fill_code != NULL && error == NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *result = tinypy_eval_code(setup_code, globals, NULL, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);

    state.fail_allocation_above = 300U * 1024U;
    result = tinypy_eval_code(fill_code, globals, NULL, &error);
    state.fail_allocation_above = 0U;
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);

    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_release(fill_code);
    tinypy_release(setup_code);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* Under a heap budget that a second copy of a list's storage would exceed,
   removing its items and sorting it without a key function or merges still
   succeed: removal does not depend on the budget, and such a sort needs no
   memory beyond the list. */
static int32_t __test_container_removal_heap_budget(void) {
    static const char source[] =
        "items = [None] * 100000\n"
        "del items[10:]\n"
        "assert len(items) == 10\n"
        "items = None\n"
        "items = [None] * 100000\n"
        "items[:] = [1]\n"
        "assert items == [1]\n"
        "items = None\n"
        "items = [None] * 100000\n"
        "del items[::2]\n"
        "assert len(items) == 50000\n"
        "items = None\n"
        "items = [None] * 100000\n"
        "del items[:]\n"
        "assert items == []\n"
        "items = None\n"
        "items = [None] * 100000\n"
        "items.sort()\n"
        "items.sort(reverse=True)\n"
        "assert len(items) == 100000\n";
    test_allocator_state_t state;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *probe = tinypy_vm_create(&config);
    TEST_CHECK(probe != NULL);
    size_t bootstrap_bytes = probe->allocated_bytes;
    tinypy_vm_destroy(probe);
    size_t storage_bytes = 100000U * sizeof(tinypy_value_t *);
    config.max_heap_bytes = bootstrap_bytes + storage_bytes + storage_bytes / 2U;
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "removal.py", 10U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_container_heap_limits(void) {
    static const char source[] =
        "list_failed = False\n"
        "try:\n"
        "    range(2000000)\n"
        "except MemoryError:\n"
        "    list_failed = True\n"
        "assert list_failed\n"
        "bytearray_failed = False\n"
        "try:\n"
        "    bytearray(16000000)\n"
        "except MemoryError:\n"
        "    bytearray_failed = True\n"
        "assert bytearray_failed\n"
        "dict_failed = False\n"
        "values = {}\n"
        "try:\n"
        "    for index in xrange(2000000):\n"
        "        values[index] = index\n"
        "except MemoryError:\n"
        "    dict_failed = True\n"
        "assert dict_failed\n";
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_compile_options_t options;
    tinypy_vm_t *vm;
    tinypy_error_t *error = NULL;
    tinypy_value_t *code;
    tinypy_value_t *globals;
    tinypy_value_t *result;
    tinypy_value_t *sort_items[1024];
    tinypy_value_t *sort_list;
    tinypy_value_t *sort_method;
    tinypy_value_t *sort_args;
    tinypy_value_t *sort_result;
    size_t allocation_calls;
    size_t index;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    config.max_heap_bytes = 8U * 1024U * 1024U;
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    code = tinypy_compile_source(vm, source, sizeof(source) - 1U, "heap_limits.py", sizeof("heap_limits.py") - 1U, &options, &error);
    TEST_CHECK(code != NULL);
    TEST_CHECK(error == NULL);
    globals = tinypy_dict_new(vm);
    result = tinypy_eval_code(code, globals, NULL, &error);
    TEST_CHECK(result != NULL);
    TEST_CHECK(error == NULL);

    /* Two ascending runs: sorting them merges half the items through
       temporary storage, and sorting the result needs none. */
    size_t sort_size = sizeof(sort_items) / sizeof(sort_items[0]);
    for (index = 0U; index < sort_size; ++index) {
        sort_items[index] = tinypy_integer_from_i64(vm, (int64_t)((index + sort_size / 2U) % sort_size));
    }
    sort_list = tinypy_list_from_items(vm, sort_items, sort_size);
    sort_method = tinypy_object_get_attr(sort_list, "sort", 4U, &error);
    sort_args = tinypy_tuple_new(vm, 0U);
    TEST_CHECK(sort_method != NULL);
    TEST_CHECK(error == NULL);
    allocation_calls = state.allocation_calls;
    sort_result = tinypy_call(sort_method, sort_args, NULL, &error);
    TEST_CHECK(sort_result != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(state.allocation_calls == allocation_calls + 1U);
    tinypy_release(sort_result);
    allocation_calls = state.allocation_calls;
    sort_result = tinypy_call(sort_method, sort_args, NULL, &error);
    TEST_CHECK(sort_result != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(state.allocation_calls == allocation_calls);
    tinypy_release(sort_result);
    tinypy_release(sort_method);
    tinypy_release(sort_list);

    /* A refused merge leaves the runs as they were. */
    sort_list = tinypy_list_from_items(vm, sort_items, sort_size);
    sort_method = tinypy_object_get_attr(sort_list, "sort", 4U, &error);
    TEST_CHECK(sort_method != NULL);
    TEST_CHECK(error == NULL);
    state.fail_allocation_above = 512U;
    sort_result = tinypy_call(sort_method, sort_args, NULL, &error);
    TEST_CHECK(sort_result == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
    TEST_CHECK(tinypy_list_size(sort_list) == sort_size);
    for (index = 0U; index < sort_size; ++index) {
        TEST_CHECK(tinypy_list_get(sort_list, index) == sort_items[index]);
    }
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    state.fail_allocation_above = 0U;
    for (index = 0U; index < sort_size; ++index) {
        tinypy_release(sort_items[index]);
    }
    tinypy_release(sort_args);
    tinypy_release(sort_method);
    tinypy_release(sort_list);

    tinypy_release(result);
    tinypy_release(globals);
    tinypy_release(code);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __test_heap_budget_native_initialize(tinypy_value_t *module, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(module);
    tinypy_value_t *value = tinypy_integer_from_i64(vm, INT64_C(2));

    (void)user_data;
    (void)out_error;
    tinypy_module_add_value(module, "VALUE", 5U, value);
    tinypy_release(value);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_module_artifact_t *__test_heap_budget_resolve(void *user_data, const tinypy_module_request_t *request) {
    const tinypy_module_artifact_t *artifacts = (const tinypy_module_artifact_t *)user_data;
    size_t index;

    for (index = 0U; index < 2U; ++index) {
        if (request->canonical_name_size == artifacts[index].canonical_name_size && memcmp(request->canonical_name, artifacts[index].canonical_name, request->canonical_name_size) == 0) {
            return &artifacts[index];
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __test_heap_budget_release(void *user_data, const tinypy_module_artifact_t *artifact) {
    (void)user_data;
    (void)artifact;
}
//////////////////////////////////////////////////////////////////////////
typedef enum test_heap_budget_outcome_e {
    TEST_HEAP_BUDGET_REJECTED = 0,
    TEST_HEAP_BUDGET_MEMORY_ERROR = 1,
    TEST_HEAP_BUDGET_SUCCEEDED = 2
} test_heap_budget_outcome_e;
//////////////////////////////////////////////////////////////////////////
/* A run under a heap budget ends with success or MemoryError: the VM is
   either created whole or not at all, and nothing leaks or is miscompiled. */
static int32_t __test_heap_budget_run(tinypy_module_artifact_t *artifacts, const char *source, size_t budget, test_heap_budget_outcome_e *out_outcome) {
    test_allocator_state_t state;
    tinypy_host_t host;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = artifacts;
    host.resolve_module = __test_heap_budget_resolve;
    host.release_module_artifact = __test_heap_budget_release;
    config.host = &host;
    config.max_heap_bytes = budget;
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    if (vm == NULL) {
        TEST_CHECK(state.outstanding_allocations == 0U);
        TEST_CHECK(state.outstanding_bytes == 0U);
        *out_outcome = TEST_HEAP_BUDGET_REJECTED;
        return 0;
    }
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    size_t source_size = strlen(source);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *result = tinypy_exec_source(vm, source, source_size, "budget.py", 9U, globals, NULL, &options, &error);
    *out_outcome = result != NULL ? TEST_HEAP_BUDGET_SUCCEEDED : TEST_HEAP_BUDGET_MEMORY_ERROR;
    if (result != NULL) {
        tinypy_release(result);
    }
    else {
        TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
        tinypy_error_release(error);
        tinypy_vm_clear_error(vm);
    }
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* Budgets grow geometrically from below the VM bootstrap to well above the
   whole run; failures late in a run sit just under the smallest budget that
   succeeds, so that range is swept finely as well. */
static int32_t __test_heap_budget_sweep(void) {
    static const char module_source[] =
        "VALUE = 1\n"
        "def twice(item):\n"
        "    return [item] * 2\n";
    static const char *const sources[] = {
        "",
        "def outer(x):\n"
        "    def inner(y):\n"
        "        return x + y\n"
        "    return inner\n"
        "class Sample(object):\n"
        "    def items(self):\n"
        "        return [index * 2 for index in range(5)]\n"
        "assert outer(1)(2) == 3\n"
        "assert Sample().items() == [0, 2, 4, 6, 8]\n",
        "def numbers():\n"
        "    try:\n"
        "        yield 1\n"
        "        yield 2\n"
        "    finally:\n"
        "        log.append('closed')\n"
        "log = []\n"
        "assert sum(numbers()) == 3\n"
        "assert log == ['closed']\n",
        "import budget_source\n"
        "import budget_native\n"
        "assert budget_source.VALUE + budget_native.VALUE == 3\n"
        "assert budget_source.twice('x') == ['x', 'x']\n"};
    tinypy_module_artifact_t artifacts[2];
    size_t outcome_counts[3] = {0U, 0U, 0U};
    size_t index;

    (void)memset(artifacts, 0, sizeof(artifacts));
    for (index = 0U; index < 2U; ++index) {
        artifacts[index].abi_version = TINYPY_ABI_VERSION;
        artifacts[index].struct_size = (uint32_t)sizeof(artifacts[index]);
        artifacts[index].canonical_name_size = 13U;
    }
    artifacts[0].content_kind = TINYPY_MODULE_CONTENT_SOURCE;
    artifacts[0].data = module_source;
    artifacts[0].data_size = sizeof(module_source) - 1U;
    artifacts[0].canonical_name = "budget_source";
    artifacts[0].logical_filename = "budget_source.py";
    artifacts[0].logical_filename_size = 16U;
    artifacts[1].content_kind = TINYPY_MODULE_CONTENT_NATIVE;
    artifacts[1].canonical_name = "budget_native";
    artifacts[1].native_initialize = __test_heap_budget_native_initialize;
    for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        size_t threshold = 0U;
        size_t budget;
        size_t step;

        for (budget = 16U * 1024U; budget <= 4U * 1024U * 1024U; budget += budget / 7U) {
            test_heap_budget_outcome_e outcome;
            if (__test_heap_budget_run(artifacts, sources[index], budget, &outcome) != 0) {
                return 1;
            }
            outcome_counts[outcome] += 1U;
            if (outcome == TEST_HEAP_BUDGET_SUCCEEDED && threshold == 0U) {
                threshold = budget;
            }
        }
        TEST_CHECK(threshold != 0U);
        for (step = 1U; step <= 32U; ++step) {
            test_heap_budget_outcome_e outcome;
            if (__test_heap_budget_run(artifacts, sources[index], threshold - threshold / 256U * step, &outcome) != 0) {
                return 1;
            }
            outcome_counts[outcome] += 1U;
        }
    }
    TEST_CHECK(outcome_counts[TEST_HEAP_BUDGET_REJECTED] != 0U);
    TEST_CHECK(outcome_counts[TEST_HEAP_BUDGET_MEMORY_ERROR] != 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* Text that runs out of heap while it is formatted fails with MemoryError,
   never as too large, and a regular expression state that cannot decode its
   subject hands the matcher stacks back to the pattern. */
static int32_t __test_text_heap_budget(void) {
    static const char source[] =
        "import _sre\n"
        "for bits in (4000, 16000, 64000, 128000):\n"
        "    try:\n"
        "        big = 1 << bits\n"
        "        text = 'y' * (bits // 4)\n"
        "    except MemoryError:\n"
        "        break\n"
        "    for render in (lambda: '%x' % big, lambda: format(big, 'x'), lambda: '{0:x}'.format(big), lambda: hex(big), lambda: '%s%s' % (text, text)):\n"
        "        try:\n"
        "            render()\n"
        "        except MemoryError:\n"
        "            pass\n"
        "pattern = _sre.compile('(a|b)*c', 0, [17, 4, 0, 1, 0, 28, 13, 0, 4294967295, 21, 0, 15, 5, 27, 97, 98, 0, 21, 1, 22, 19, 99, 1], 1)\n"
        "assert pattern.match(u'ababc').span() == (0, 5)\n"
        "for size in (1000, 10000, 40000):\n"
        "    try:\n"
        "        assert pattern.search(u'\\xe9' * size) is None\n"
        "    except MemoryError:\n"
        "        pass\n";
    static const char float_source[] =
        "texts = ('2' * 900 + 'e-1124', '2' * 900 + 'e-1124j')\n"
        "for convert in (float, complex):\n"
        "    hold = []\n"
        "    for size in (16384, 2048, 256, 32, 8, 2):\n"
        "        try:\n"
        "            while True:\n"
        "                hold.append('x' * size)\n"
        "        except MemoryError:\n"
        "            pass\n"
        "    try:\n"
        "        convert(texts[convert is complex])\n"
        "    except MemoryError:\n"
        "        pass\n"
        "    hold = None\n";
    /* A string that cannot grow in place keeps its variable bound, whether
       the next instruction stores it as a local, a cell or a name, and
       nothing leaks. */
    static const char concat_source[] =
        "t = 'x' * 4096\n"
        "def local():\n"
        "    s = ''.join(['ab'] * 1000)\n"
        "    try:\n"
        "        while True:\n"
        "            s = s + t\n"
        "    except MemoryError:\n"
        "        s = not s\n"
        "    return s\n"
        "def cell():\n"
        "    s = ''.join(['ab'] * 1000)\n"
        "    def peek():\n"
        "        return s\n"
        "    try:\n"
        "        while True:\n"
        "            s += t\n"
        "    except MemoryError:\n"
        "        s = not s\n"
        "    return peek()\n"
        "s = ''.join(['ab'] * 1000)\n"
        "try:\n"
        "    while True:\n"
        "        s += t\n"
        "except MemoryError:\n"
        "    s = not s\n"
        "assert s is False and local() is False and cell() is False\n";
    tinypy_module_artifact_t artifacts[2];
    size_t outcome_counts[3] = {0U, 0U, 0U};
    size_t budget;

    /* No host module is imported; the resolver finds nothing. */
    (void)memset(artifacts, 0, sizeof(artifacts));
    for (budget = 64U * 1024U; budget <= 4U * 1024U * 1024U; budget += budget / 24U) {
        test_heap_budget_outcome_e outcome;
        if (__test_heap_budget_run(artifacts, source, budget, &outcome) != 0) {
            return 1;
        }
        outcome_counts[outcome] += 1U;
    }
    TEST_CHECK(outcome_counts[TEST_HEAP_BUDGET_MEMORY_ERROR] != 0U);
    TEST_CHECK(outcome_counts[TEST_HEAP_BUDGET_SUCCEEDED] != 0U);
    /* float() and complex() of a literal whose exact conversion runs out of
       heap raise MemoryError, as PyOS_string_to_double does, never the
       ValueError of a malformed literal. */
    for (budget = 1024U * 1024U; budget <= 4U * 1024U * 1024U; budget *= 2U) {
        test_heap_budget_outcome_e outcome;
        if (__test_heap_budget_run(artifacts, float_source, budget, &outcome) != 0) {
            return 1;
        }
        TEST_CHECK(outcome == TEST_HEAP_BUDGET_SUCCEEDED);
    }
    for (budget = 2U * 1024U * 1024U; budget <= 4U * 1024U * 1024U; budget *= 2U) {
        test_heap_budget_outcome_e outcome;
        if (__test_heap_budget_run(artifacts, concat_source, budget, &outcome) != 0) {
            return 1;
        }
        TEST_CHECK(outcome == TEST_HEAP_BUDGET_SUCCEEDED);
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* Computed-size text, buffer and tuple results raise MemoryError when they
   do not fit the rest of the heap budget, and a module released while the
   budget is exhausted still clears its namespace, so the cycle between it
   and its functions gives its memory back. */
static int32_t __test_computed_heap_budget(void) {
    static const char computed_source[] =
        "import _struct\n"
        "N = 300000\n"
        "def f(*a):\n"
        "    return a\n"
        "def outcome(make_input, operation):\n"
        "    try:\n"
        "        value = make_input()\n"
        "        kept = [operation(value), operation(value), operation(value)]\n"
        "    except MemoryError:\n"
        "        return 'MemoryError'\n"
        "    return 'kept'\n"
        "text = lambda: 'c' + 'a' * N + 'c,'\n"
        "utext = lambda: u'c' + u'a' * N + u'c,'\n"
        "cases = ((text, lambda s: s.partition(',')), (text, lambda s: s.rsplit(',')), (text, lambda s: s.strip('c,')),\n"
        "         (utext, lambda u: u[1:]), (utext, lambda u: u.strip(u'c,')), (text, unicode),\n"
        "         (lambda: [0] * (N // 8), lambda items: f(*items)), (lambda: '\\x00' * N, lambda z: _struct.unpack('%dd' % (N // 8), z)),\n"
        "         (text, lambda s: memoryview(s).tobytes()), (text, lambda s: str(buffer(s))))\n"
        "results = [outcome(make_input, operation) for make_input, operation in cases]\n"
        "assert results == ['MemoryError'] * len(cases), results\n";
    static const char module_source[] =
        "import sys\n"
        "module_type = type(sys)\n"
        "body = 'def f():\\n    return 1\\nbig = \"z\" * 150000\\n' + ''.join('v%d = %d\\n' % (index, index) for index in range(100))\n"
        "def exhaust():\n"
        "    fill = []\n"
        "    for size in (65536, 8192, 1024, 600):\n"
        "        try:\n"
        "            while len(fill) < 10000:\n"
        "                fill.append('x' * size)\n"
        "        except MemoryError:\n"
        "            pass\n"
        "    return fill\n"
        "for round in range(4):\n"
        "    victim = module_type('victim')\n"
        "    exec body in victim.__dict__\n"
        "    fill = exhaust()\n"
        "    del victim\n"
        "    del fill\n"
        "text = 'y' * 400000\n";
    /* A shift count or an exponent that fits an int64 but not the host's
       size_t asks for more digits than the host can address, a MemoryError
       on every host as with 64-bit sizes. */
    static const char huge_source[] =
        "def outcome(operation):\n"
        "    try:\n"
        "        operation()\n"
        "    except MemoryError:\n"
        "        return 'MemoryError'\n"
        "    return 'kept'\n"
        "cases = (lambda: 1 << 2 ** 40, lambda: -3L << 2 ** 62, lambda: 7 ** 2 ** 64, lambda: (-2L) ** (2 ** 64 + 1))\n"
        "results = [outcome(operation) for operation in cases]\n"
        "assert results == ['MemoryError'] * len(cases), results\n"
        "assert (1 >> 2 ** 40, -1 >> 2 ** 62, 0 << 2 ** 62, 5L >> 2 ** 40, -5L >> 2 ** 62) == (0, -1, 0, 0, -1)\n";
    test_allocator_state_t state;
    tinypy_module_artifact_t artifacts[2];
    test_heap_budget_outcome_e outcome;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *probe = tinypy_vm_create(&config);
    TEST_CHECK(probe != NULL);
    size_t bootstrap_bytes = probe->allocated_bytes;
    tinypy_vm_destroy(probe);
    /* Only built-in modules are imported; the resolver finds nothing. */
    (void)memset(artifacts, 0, sizeof(artifacts));
    if (__test_heap_budget_run(artifacts, computed_source, bootstrap_bytes + 1024U * 1024U, &outcome) != 0) {
        return 1;
    }
    TEST_CHECK(outcome == TEST_HEAP_BUDGET_SUCCEEDED);
    if (__test_heap_budget_run(artifacts, module_source, bootstrap_bytes + 1000000U, &outcome) != 0) {
        return 1;
    }
    TEST_CHECK(outcome == TEST_HEAP_BUDGET_SUCCEEDED);
    if (__test_heap_budget_run(artifacts, huge_source, bootstrap_bytes + 1024U * 1024U, &outcome) != 0) {
        return 1;
    }
    TEST_CHECK(outcome == TEST_HEAP_BUDGET_SUCCEEDED);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* A module with a function, scalars and an interned name, and with string,
   unicode, long and tuple constants larger than the pooled requests, so that
   every build hands their allocations to the host. */
static size_t __test_marshal_large_constants(tinypy_vm_t *vm, uint8_t *buffer, size_t capacity) {
    char source[8192];
    tinypy_compile_options_t options;
    size_t marshal_size = 0U;
    size_t index;

    size_t size = (size_t)snprintf(source, sizeof(source), "def f(a, b=4000000000):\n    return (a, b, 1.5, 2j, 123456, u'\\xe9\\u20ac', 'name_like')\ns = '");
    (void)memset(source + size, 's', 700U);
    size += 700U;
    size += (size_t)snprintf(source + size, sizeof(source) - size, "'\nu = u'");
    for (index = 0U; index < 300U; ++index) {
        size += (size_t)snprintf(source + size, sizeof(source) - size, "\\xe9");
    }
    size += (size_t)snprintf(source + size, sizeof(source) - size, "'\nl = 0x");
    (void)memset(source + size, 'f', 1200U);
    size += 1200U;
    size += (size_t)snprintf(source + size, sizeof(source) - size, "L\nt = (");
    for (index = 0U; index < 80U; ++index) {
        size += (size_t)snprintf(source + size, sizeof(source) - size, "%u, ", (unsigned int)index);
    }
    size += (size_t)snprintf(source + size, sizeof(source) - size, ")\n");
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *code = tinypy_compile_source(vm, source, size, "large.py", 8U, &options, NULL);
    if (code == NULL) {
        return 0U;
    }
    tinypy_marshal_result_e result = tinypy_marshal_dump_code_v2(code, buffer, capacity, &marshal_size, NULL, NULL);
    tinypy_release(code);
    return result == TINYPY_MARSHAL_OK ? marshal_size : 0U;
}
//////////////////////////////////////////////////////////////////////////
/* Each host request of a load fails in turn: the load ends with
   TINYPY_MARSHAL_BYTE_LIMIT and a pending MemoryError, leaves nothing behind
   and the VM loads the module again. The constants include a dict holding a
   list, which only crafted marshal data carries. */
static int32_t __test_marshal_load_memory_sweep(tinypy_vm_t *vm, test_allocator_state_t *state, const uint8_t *bytes, size_t size) {
    tinypy_value_t *memory_error = &vm->exception_types[TINYPY_EXCEPTION_MEMORY_ERROR]->base.base;
    tinypy_marshal_error_t marshal_error;
    tinypy_value_t *code = NULL;
    size_t unreported_count = 0U;
    size_t index;

    TEST_CHECK(tinypy_marshal_load_code_v2(vm, bytes, size, NULL, &code, &marshal_error) == TINYPY_MARSHAL_OK && code != NULL);
    tinypy_release(code);
    size_t settled_allocations = state->outstanding_allocations;
    size_t first_call = state->allocation_calls;
    TEST_CHECK(tinypy_marshal_load_code_v2(vm, bytes, size, NULL, &code, &marshal_error) == TINYPY_MARSHAL_OK && code != NULL);
    tinypy_release(code);
    size_t request_count = state->allocation_calls - first_call;
    TEST_CHECK(request_count != 0U && state->outstanding_allocations == settled_allocations);
    for (index = 1U; index <= request_count; ++index) {
        state->fail_allocation_call = state->allocation_calls + index;
        tinypy_marshal_result_e result = tinypy_marshal_load_code_v2(vm, bytes, size, NULL, &code, &marshal_error);
        tinypy_bool_t reached = state->allocation_calls >= state->fail_allocation_call ? TINYPY_TRUE : TINYPY_FALSE;
        state->fail_allocation_call = 0U;
        if (reached == 0) {
            TEST_CHECK(result == TINYPY_MARSHAL_OK && code != NULL);
            tinypy_release(code);
            continue;
        }
        TEST_CHECK(result == TINYPY_MARSHAL_BYTE_LIMIT && code == NULL && marshal_error.code == TINYPY_MARSHAL_BYTE_LIMIT);
        if (tinypy_vm_raised_exception_type(vm) != memory_error) {
            unreported_count += 1U;
        }
        tinypy_vm_clear_error(vm);
        TEST_CHECK(state->outstanding_allocations == settled_allocations);
    }
    TEST_CHECK(unreported_count == 0U);
    TEST_CHECK(tinypy_marshal_load_code_v2(vm, bytes, size, NULL, &code, &marshal_error) == TINYPY_MARSHAL_OK && code != NULL);
    tinypy_release(code);
    TEST_CHECK(state->outstanding_allocations == settled_allocations);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_module_artifact_t *__test_marshal_load_resolve(void *user_data, const tinypy_module_request_t *request) {
    const tinypy_module_artifact_t *artifact = (const tinypy_module_artifact_t *)user_data;

    (void)request;
    return artifact;
}
//////////////////////////////////////////////////////////////////////////
static void __test_marshal_load_release(void *user_data, const tinypy_module_artifact_t *artifact) {
    (void)user_data;
    (void)artifact;
}
//////////////////////////////////////////////////////////////////////////
/* Loads a module with a large string constant under a heap budget that the
   parsed document, the materialized constant or neither exhausts, directly
   and through import. */
static int32_t __test_marshal_load_budget(const uint8_t *bytes, size_t size, size_t string_size, size_t budget_extra, const char *message) {
    test_allocator_state_t state;
    tinypy_module_artifact_t artifact;
    tinypy_host_t host;
    tinypy_marshal_error_t marshal_error;
    tinypy_value_t *code = NULL;
    tinypy_error_t *error = NULL;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *probe = tinypy_vm_create(&config);
    TEST_CHECK(probe != NULL);
    size_t bootstrap_bytes = probe->allocated_bytes;
    tinypy_vm_destroy(probe);
    (void)memset(&artifact, 0, sizeof(artifact));
    artifact.abi_version = TINYPY_ABI_VERSION;
    artifact.struct_size = (uint32_t)sizeof(artifact);
    artifact.content_kind = TINYPY_MODULE_CONTENT_MARSHAL_V2;
    artifact.data = bytes;
    artifact.data_size = size;
    artifact.canonical_name = "large";
    artifact.canonical_name_size = 5U;
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = &artifact;
    host.resolve_module = __test_marshal_load_resolve;
    host.release_module_artifact = __test_marshal_load_release;
    config.host = &host;
    config.max_heap_bytes = bootstrap_bytes + string_size / 4U * budget_extra;
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_marshal_result_e result = tinypy_marshal_load_code_v2(vm, bytes, size, NULL, &code, &marshal_error);
    tinypy_value_t *module = tinypy_import_module(vm, "large", 5U, NULL, NULL, 0, &error);
    if (message == NULL) {
        TEST_CHECK(result == TINYPY_MARSHAL_OK && code != NULL && module != NULL && error == NULL);
        tinypy_release(module);
        tinypy_release(code);
    }
    else {
        TEST_CHECK(result == TINYPY_MARSHAL_BYTE_LIMIT && code == NULL && strcmp(marshal_error.message, message) == 0);
        TEST_CHECK(module == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
        tinypy_error_release(error);
        tinypy_vm_clear_error(vm);
    }
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* Loading marshal data recovers every allocation the host or the heap budget
   refuses, in the parser and in the materialized values alike. */
static int32_t __test_marshal_load_memory(void) {
    static const uint8_t dict_module[] = {
        0x63U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x40U, 0x00U, 0x00U,
        0x00U, 0x73U, 0x04U, 0x00U, 0x00U, 0x00U, 0x64U, 0x00U, 0x00U, 0x53U, 0x28U, 0x02U, 0x00U, 0x00U, 0x00U, 0x4eU,
        0x7bU, 0x73U, 0x01U, 0x00U, 0x00U, 0x00U, 0x6bU, 0x5bU, 0x02U, 0x00U, 0x00U, 0x00U, 0x69U, 0x07U, 0x00U, 0x00U,
        0x00U, 0x74U, 0x03U, 0x00U, 0x00U, 0x00U, 0x6bU, 0x65U, 0x79U, 0x30U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x28U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x73U, 0x04U,
        0x00U, 0x00U, 0x00U, 0x78U, 0x2eU, 0x70U, 0x79U, 0x73U, 0x08U, 0x00U, 0x00U, 0x00U, 0x3cU, 0x6dU, 0x6fU, 0x64U,
        0x75U, 0x6cU, 0x65U, 0x3eU, 0x01U, 0x00U, 0x00U, 0x00U, 0x73U, 0x00U, 0x00U, 0x00U, 0x00U};
    static const size_t string_size = 1024U * 1024U;
    static uint8_t bytes[8192];
    test_allocator_state_t state;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    size_t size = __test_marshal_large_constants(vm, bytes, sizeof(bytes));
    TEST_CHECK(size != 0U);
    if (__test_marshal_load_memory_sweep(vm, &state, bytes, size) != 0) {
        return 1;
    }
    if (__test_marshal_load_memory_sweep(vm, &state, dict_module, sizeof(dict_module)) != 0) {
        return 1;
    }
    char *source = (char *)malloc(string_size + 8U);
    TEST_CHECK(source != NULL);
    (void)memcpy(source, "s = '", 5U);
    (void)memset(source + 5U, 's', string_size);
    (void)memcpy(source + 5U + string_size, "'\n", 2U);
    uint8_t *large = (uint8_t *)malloc(string_size + 4096U);
    TEST_CHECK(large != NULL);
    tinypy_compile_options_t options;
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *code = tinypy_compile_source(vm, source, string_size + 7U, "large.py", 8U, &options, NULL);
    TEST_CHECK(code != NULL);
    size_t large_size = 0U;
    TEST_CHECK(tinypy_marshal_dump_code_v2(code, large, string_size + 4096U, &large_size, NULL, NULL) == TINYPY_MARSHAL_OK);
    tinypy_release(code);
    free(source);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    /* The budget is the VM after bootstrap and a number of quarters of the
       string: two leave no room for the parsed copy, six none for the
       constant beside it, sixteen are enough. */
    if (__test_marshal_load_budget(large, large_size, string_size, 2U, "marshal allocation failed") != 0) {
        return 1;
    }
    if (__test_marshal_load_budget(large, large_size, string_size, 6U, "marshal runtime value allocation failed") != 0) {
        return 1;
    }
    if (__test_marshal_load_budget(large, large_size, string_size, 16U, NULL) != 0) {
        return 1;
    }
    free(large);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* As r_object stores it with PyDict_SetItem, a constant dict with an
   unhashable key fails the load with TypeError, directly and through import,
   instead of dropping the key. */
static int32_t __test_marshal_load_unhashable_key(void) {
    static const uint8_t module[] = {
        0x63U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x40U, 0x00U, 0x00U,
        0x00U, 0x73U, 0x04U, 0x00U, 0x00U, 0x00U, 0x64U, 0x00U, 0x00U, 0x53U, 0x28U, 0x02U, 0x00U, 0x00U, 0x00U, 0x4eU,
        0x7bU, 0x5bU, 0x00U, 0x00U, 0x00U, 0x00U, 0x4eU, 0x30U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x73U, 0x04U, 0x00U, 0x00U,
        0x00U, 0x78U, 0x2eU, 0x70U, 0x79U, 0x73U, 0x08U, 0x00U, 0x00U, 0x00U, 0x3cU, 0x6dU, 0x6fU, 0x64U, 0x75U, 0x6cU,
        0x65U, 0x3eU, 0x01U, 0x00U, 0x00U, 0x00U, 0x73U, 0x00U, 0x00U, 0x00U, 0x00U};
    test_allocator_state_t state;
    tinypy_module_artifact_t artifact;
    tinypy_host_t host;
    tinypy_marshal_error_t marshal_error;
    tinypy_value_t *code = NULL;
    tinypy_error_t *error = NULL;

    (void)memset(&state, 0, sizeof(state));
    (void)memset(&artifact, 0, sizeof(artifact));
    artifact.abi_version = TINYPY_ABI_VERSION;
    artifact.struct_size = (uint32_t)sizeof(artifact);
    artifact.content_kind = TINYPY_MODULE_CONTENT_MARSHAL_V2;
    artifact.data = module;
    artifact.data_size = sizeof(module);
    artifact.canonical_name = "keyed";
    artifact.canonical_name_size = 5U;
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = &artifact;
    host.resolve_module = __test_marshal_load_resolve;
    host.release_module_artifact = __test_marshal_load_release;
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    config.host = &host;
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *type_error = &vm->exception_types[TINYPY_EXCEPTION_TYPE_ERROR]->base.base;
    TEST_CHECK(tinypy_marshal_load_code_v2(vm, module, sizeof(module), NULL, &code, &marshal_error) == TINYPY_MARSHAL_INVALID_GRAPH);
    TEST_CHECK(code == NULL && marshal_error.code == TINYPY_MARSHAL_INVALID_GRAPH && marshal_error.wire_type == (uint8_t)'[');
    TEST_CHECK(tinypy_vm_raised_exception_type(vm) == type_error);
    tinypy_vm_clear_error(vm);
    tinypy_value_t *imported = tinypy_import_module(vm, "keyed", 5U, NULL, NULL, 0, &error);
    TEST_CHECK(imported == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_TYPE);
    size_t message_size;
    const char *message = tinypy_error_message(error, &message_size);
    TEST_CHECK(message_size == 23U && memcmp(message, "unhashable type: 'list'", 23U) == 0);
    tinypy_error_release(error);
    tinypy_vm_clear_error(vm);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* The direct dump keeps its intern table in VM memory, and its write pass
   must reference the strings its size pass counted. Each host request of a
   dump into a buffer of the queried size fails in turn: the dump ends with
   TINYPY_MARSHAL_BYTE_LIMIT and a pending MemoryError, or it is complete, and
   no byte past the buffer changes. */
static int32_t __test_marshal_dump_memory(void) {
    static const char source[] = "def f(a, b):\n    return alpha + beta + gamma + delta + epsilon + zeta + eta + theta + iota + kappa\nalpha = beta = gamma = delta = epsilon = zeta = eta = theta = iota = kappa = 1\n";
    static const size_t guard_size = 16U;
    test_allocator_state_t state;
    tinypy_compile_options_t options;
    tinypy_marshal_error_t marshal_error;
    size_t size = 0U;
    size_t written = 0U;
    size_t index;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *memory_error = &vm->exception_types[TINYPY_EXCEPTION_MEMORY_ERROR]->base.base;
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *code = tinypy_compile_source(vm, source, sizeof(source) - 1U, "m.py", 4U, &options, NULL);
    TEST_CHECK(code != NULL);
    TEST_CHECK(tinypy_marshal_dump_code_v2(code, NULL, 0U, &size, NULL, &marshal_error) == TINYPY_MARSHAL_OK && size != 0U);
    uint8_t *expected = (uint8_t *)malloc(size);
    uint8_t *buffer = (uint8_t *)malloc(size + guard_size);
    TEST_CHECK(expected != NULL && buffer != NULL);
    size_t first_call = state.allocation_calls;
    TEST_CHECK(tinypy_marshal_dump_code_v2(code, expected, size, &written, NULL, &marshal_error) == TINYPY_MARSHAL_OK && written == size);
    size_t request_count = state.allocation_calls - first_call;
    TEST_CHECK(request_count != 0U);
    for (index = 1U; index <= request_count; ++index) {
        (void)memset(buffer, 0xa5, size + guard_size);
        state.fail_allocation_call = state.allocation_calls + index;
        tinypy_marshal_result_e result = tinypy_marshal_dump_code_v2(code, buffer, size, &written, NULL, &marshal_error);
        tinypy_bool_t reached = state.allocation_calls >= state.fail_allocation_call ? TINYPY_TRUE : TINYPY_FALSE;
        state.fail_allocation_call = 0U;
        for (size_t guard = 0U; guard < guard_size; ++guard) {
            TEST_CHECK(buffer[size + guard] == 0xa5U);
        }
        if (reached == 0) {
            TEST_CHECK(result == TINYPY_MARSHAL_OK && written == size && memcmp(buffer, expected, size) == 0);
            continue;
        }
        TEST_CHECK(result == TINYPY_MARSHAL_BYTE_LIMIT && marshal_error.code == TINYPY_MARSHAL_BYTE_LIMIT && written <= size);
        TEST_CHECK(tinypy_vm_raised_exception_type(vm) == memory_error);
        tinypy_vm_clear_error(vm);
    }
    free(buffer);
    free(expected);
    tinypy_release(code);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* r_object reads an 's' string into a new object, one byte long or not, so
   marshal writes it back as 's' while the interned name stays 't'. The
   module is marshal.dumps(compile("x = '\\xff'\nprint x\n", '<s>', 'exec')) of
   CPython 2.7.18. */
static int32_t __test_marshal_one_byte_strings(void) {
    static const uint8_t module[] = {
        0x63U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x40U, 0x00U, 0x00U,
        0x00U, 0x73U, 0x0fU, 0x00U, 0x00U, 0x00U, 0x64U, 0x00U, 0x00U, 0x5aU, 0x00U, 0x00U, 0x65U, 0x00U, 0x00U, 0x47U,
        0x48U, 0x64U, 0x01U, 0x00U, 0x53U, 0x28U, 0x02U, 0x00U, 0x00U, 0x00U, 0x73U, 0x01U, 0x00U, 0x00U, 0x00U, 0xffU,
        0x4eU, 0x28U, 0x01U, 0x00U, 0x00U, 0x00U, 0x74U, 0x01U, 0x00U, 0x00U, 0x00U, 0x78U, 0x28U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x73U, 0x03U, 0x00U, 0x00U, 0x00U,
        0x3cU, 0x73U, 0x3eU, 0x74U, 0x08U, 0x00U, 0x00U, 0x00U, 0x3cU, 0x6dU, 0x6fU, 0x64U, 0x75U, 0x6cU, 0x65U, 0x3eU,
        0x01U, 0x00U, 0x00U, 0x00U, 0x73U, 0x02U, 0x00U, 0x00U, 0x00U, 0x06U, 0x01U};
    test_allocator_state_t state;
    tinypy_marshal_error_t marshal_error;
    tinypy_value_t *code = NULL;
    uint8_t dumped[sizeof(module)];
    size_t size = 0U;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *shared = tinypy_string_from_bytes(vm, "\xff", 1U);
    TEST_CHECK(tinypy_marshal_load_code_v2(vm, module, sizeof(module), NULL, &code, &marshal_error) == TINYPY_MARSHAL_OK && code != NULL);
    TEST_CHECK(tinypy_tuple_get(tinypy_code_consts(code), 0U) != shared);
    TEST_CHECK(tinypy_marshal_dump_code_v2(code, dumped, sizeof(dumped), &size, NULL, &marshal_error) == TINYPY_MARSHAL_OK);
    TEST_CHECK(size == sizeof(module) && memcmp(dumped, module, size) == 0);
    tinypy_release(code);
    tinypy_release(shared);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_code_new checks the fields the marshal loader checks: frames are
   sized by local_count while the verifier bounds local operands by
   len(varnames), so a mismatch, a name that is not a string, arguments
   beyond the locals or a negative stack size fail with ValueError instead of
   making code that evaluates outside its frame. The loader rejects a name
   that is not a string too. */
static int32_t __test_code_new_validation(void) {
    static const uint8_t store_load[] = {100U, 0U, 0U, 125U, 3U, 0U, 124U, 3U, 0U, 83U};
    static const uint8_t integer_name_module[] = {
        0x63U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x40U, 0x00U, 0x00U,
        0x00U, 0x73U, 0x0fU, 0x00U, 0x00U, 0x00U, 0x64U, 0x00U, 0x00U, 0x5aU, 0x00U, 0x00U, 0x65U, 0x00U, 0x00U, 0x47U,
        0x48U, 0x64U, 0x01U, 0x00U, 0x53U, 0x28U, 0x02U, 0x00U, 0x00U, 0x00U, 0x73U, 0x01U, 0x00U, 0x00U, 0x00U, 0xffU,
        0x4eU, 0x28U, 0x01U, 0x00U, 0x00U, 0x00U, 0x69U, 0x7bU, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x73U, 0x03U, 0x00U, 0x00U, 0x00U, 0x3cU,
        0x73U, 0x3eU, 0x74U, 0x08U, 0x00U, 0x00U, 0x00U, 0x3cU, 0x6dU, 0x6fU, 0x64U, 0x75U, 0x6cU, 0x65U, 0x3eU, 0x01U,
        0x00U, 0x00U, 0x00U, 0x73U, 0x02U, 0x00U, 0x00U, 0x00U, 0x06U, 0x01U};
    static const int32_t flags = TINYPY_CODE_OPTIMIZED | TINYPY_CODE_NEW_LOCALS | TINYPY_CODE_NO_FREE;
    test_allocator_state_t state;
    tinypy_marshal_error_t marshal_error;
    tinypy_value_t *loaded = NULL;
    tinypy_error_t *error = NULL;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *value_error = &vm->exception_types[TINYPY_EXCEPTION_VALUE_ERROR]->base.base;
    tinypy_value_t *bytecode = tinypy_string_from_bytes(vm, store_load, sizeof(store_load));
    tinypy_value_t *seven = tinypy_integer_from_i64(vm, INT64_C(7));
    tinypy_value_t *consts = tinypy_tuple_from_items(vm, &seven, 1U);
    tinypy_value_t *integer_names = tinypy_tuple_from_items(vm, &seven, 1U);
    tinypy_value_t *local_names[4] = {tinypy_string_from_bytes(vm, "a", 1U), tinypy_string_from_bytes(vm, "b", 1U), tinypy_string_from_bytes(vm, "c", 1U), tinypy_string_from_bytes(vm, "d", 1U)};
    tinypy_value_t *varnames = tinypy_tuple_from_items(vm, local_names, 4U);
    tinypy_value_t *empty = tinypy_tuple_from_items(vm, NULL, 0U);
    tinypy_value_t *filename = tinypy_string_from_bytes(vm, "f.py", 4U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "f", 1U);
    tinypy_value_t *lnotab = tinypy_string_from_bytes(vm, "", 0U);
    TEST_CHECK(tinypy_code_new(0, 0, 2, flags, bytecode, consts, empty, varnames, empty, empty, filename, name, 1, lnotab) == NULL);
    TEST_CHECK(tinypy_vm_raised_exception_type(vm) == value_error);
    tinypy_vm_clear_error(vm);
    TEST_CHECK(tinypy_code_new(0, 4, 2, flags, bytecode, consts, integer_names, varnames, empty, empty, filename, name, 1, lnotab) == NULL);
    TEST_CHECK(tinypy_vm_raised_exception_type(vm) == value_error);
    tinypy_vm_clear_error(vm);
    TEST_CHECK(tinypy_code_new(4, 4, 2, flags | TINYPY_CODE_VARARGS, bytecode, consts, empty, varnames, empty, empty, filename, name, 1, lnotab) == NULL);
    TEST_CHECK(tinypy_vm_raised_exception_type(vm) == value_error);
    tinypy_vm_clear_error(vm);
    TEST_CHECK(tinypy_code_new(0, 4, -1, flags, bytecode, consts, empty, varnames, empty, empty, filename, name, 1, lnotab) == NULL);
    TEST_CHECK(tinypy_vm_raised_exception_type(vm) == value_error);
    tinypy_vm_clear_error(vm);
    tinypy_value_t *code = tinypy_code_new(0, 4, 2, flags, bytecode, consts, empty, varnames, empty, empty, filename, name, 1, lnotab);
    TEST_CHECK(code != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *result = tinypy_eval_code(code, globals, NULL, &error);
    TEST_CHECK(result != NULL && error == NULL && tinypy_integer_as_i64(result) == INT64_C(7));
    tinypy_release(result);
    TEST_CHECK(tinypy_marshal_load_code_v2(vm, integer_name_module, sizeof(integer_name_module), NULL, &loaded, &marshal_error) == TINYPY_MARSHAL_INVALID_CODE);
    TEST_CHECK(loaded == NULL && tinypy_vm_has_error(vm) == 0);
    tinypy_release(globals);
    tinypy_release(code);
    tinypy_release(lnotab);
    tinypy_release(name);
    tinypy_release(filename);
    tinypy_release(empty);
    tinypy_release(varnames);
    for (size_t index = 0U; index < 4U; ++index) {
        tinypy_release(local_names[index]);
    }
    tinypy_release(integer_names);
    tinypy_release(consts);
    tinypy_release(seven);
    tinypy_release(bytecode);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_weakref_optional_error_outputs(void) {
    test_allocator_state_t allocator_state;
    tinypy_error_t *error = NULL;
    tinypy_bool_t matching_errors = TINYPY_TRUE;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *target = tinypy_set_new(vm);
    tinypy_value_t *reference = tinypy_weakref_new(target, NULL, &error);
    tinypy_value_t *module = tinypy_import_module(vm, "_weakref", 8U, NULL, NULL, 0, &error);
    tinypy_value_t *factory = tinypy_module_get_value(module, "proxy", 5U);
    tinypy_value_t *factory_args = tinypy_tuple_from_items(vm, &target, 1U);
    tinypy_value_t *proxy = tinypy_call(factory, factory_args, NULL, &error);
    tinypy_value_t *empty = tinypy_tuple_from_items(vm, NULL, 0U);
    tinypy_value_t *keywords = tinypy_dict_new(vm);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "missing", 7U);

    TEST_CHECK(reference != NULL && module != NULL && factory != NULL && proxy != NULL && error == NULL);
    tinypy_dict_set(keywords, name, target);
    const struct {
        tinypy_value_t *receiver;
        const char *name;
        size_t name_size;
        size_t argument_count;
    } methods[] = {
        {proxy, "__or__", 6U, 1U},
        {proxy, "__nonzero__", 11U, 0U},
        {reference, "__eq__", 6U, 1U},
        {proxy, "__delattr__", 11U, 1U},
        {reference, "__call__", 8U, 0U}
    };

    /* The same ordinary descriptor call must retain its parser exception
       whether the embedding caller requests an owned diagnostic or not. */
    for (size_t index = 0U; index < sizeof(methods) / sizeof(methods[0]); ++index) {
        tinypy_value_t *descriptor = tinypy_type_get_attr(tinypy_object_type(methods[index].receiver), methods[index].name, methods[index].name_size);

        TEST_CHECK(descriptor != NULL);
        for (size_t shape = 0U; shape < 2U; ++shape) {
            tinypy_value_t *items[2] = {methods[index].receiver, name};
            size_t count = shape == 0U ? (methods[index].argument_count == 0U ? 2U : 1U) : methods[index].argument_count + 1U;
            tinypy_value_t *arguments = tinypy_tuple_from_items(vm, items, count);
            tinypy_value_t *kwargs = shape == 0U ? NULL : keywords;
            tinypy_value_t *result = tinypy_call(descriptor, arguments, kwargs, &error);

            TEST_CHECK(result == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_TYPE);
            tinypy_value_t *expected_type = tinypy_vm_raised_exception_type(vm);
            tinypy_value_t *expected_exception = tinypy_vm_raised_exception(vm);

            TEST_CHECK(expected_type != NULL && expected_exception != NULL);
            tinypy_retain(expected_exception);
            tinypy_error_release(error);
            error = NULL;
            tinypy_vm_clear_error(vm);
            tinypy_value_t *expected_args = tinypy_object_get_attr(expected_exception, "args", 4U, &error);

            TEST_CHECK(expected_args != NULL && error == NULL && tinypy_tuple_size(expected_args) == 1U);
            tinypy_release(expected_exception);
            result = tinypy_call(descriptor, arguments, kwargs, NULL);
            TEST_CHECK(result == NULL && tinypy_vm_has_error(vm) != 0);
            tinypy_value_t *actual_exception = tinypy_vm_raised_exception(vm);

            TEST_CHECK(actual_exception != NULL);
            matching_errors = tinypy_vm_raised_exception_type(vm) == expected_type && matching_errors != TINYPY_FALSE ? TINYPY_TRUE : TINYPY_FALSE;
            tinypy_retain(actual_exception);
            tinypy_vm_clear_error(vm);
            tinypy_value_t *actual_args = tinypy_object_get_attr(actual_exception, "args", 4U, &error);

            TEST_CHECK(actual_args != NULL && error == NULL && tinypy_tuple_size(actual_args) == 1U);
            size_t expected_size;
            size_t actual_size;
            const void *expected_message = tinypy_string_view(tinypy_tuple_get(expected_args, 0U), &expected_size);
            const void *actual_message = tinypy_string_view(tinypy_tuple_get(actual_args, 0U), &actual_size);
            tinypy_bool_t same_message = expected_size == actual_size && memcmp(expected_message, actual_message, expected_size) == 0 ? TINYPY_TRUE : TINYPY_FALSE;

            if (same_message == TINYPY_FALSE) {
                (void)fprintf(stderr, "weakref optional error output: %s %s changed diagnostic\n", methods[index].name, shape == 0U ? "count" : "keyword");
                matching_errors = TINYPY_FALSE;
            }
            tinypy_release(actual_args);
            tinypy_release(actual_exception);
            tinypy_release(expected_args);
            tinypy_release(arguments);
            result = tinypy_call(reference, empty, NULL, NULL);
            TEST_CHECK(result == target && tinypy_vm_has_error(vm) == 0);
            tinypy_release(result);
        }
    }
    tinypy_release(name);
    tinypy_release(keywords);
    tinypy_release(empty);
    tinypy_release(proxy);
    tinypy_release(factory_args);
    tinypy_release(module);
    tinypy_release(reference);
    tinypy_release(target);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U && allocator_state.outstanding_bytes == 0U);
    TEST_CHECK(matching_errors != TINYPY_FALSE);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_attribute_global(tinypy_value_t *globals, const char *name) {
    tinypy_value_t *key = tinypy_string_from_bytes(tinypy_value_vm(globals), name, strlen(name));
    tinypy_value_t *value = tinypy_dict_get(globals, key);

    tinypy_release(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_attribute_error_message(tinypy_vm_t *vm, tinypy_error_t *error, tinypy_error_kind_e kind, const char *expected) {
    size_t size = 0U;
    const char *message = error != NULL ? tinypy_error_message(error, &size) : "";
    int32_t matched = error != NULL && tinypy_error_kind(error) == kind && size == strlen(expected) && memcmp(message, expected, size) == 0 ? INT32_C(1) : INT32_C(0);

    if (matched == 0) {
        (void)fprintf(stderr, "attribute error: expected '%s', got '%.*s'\n", expected, (int)size, message);
    }
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_vm_clear_error(vm);
    return matched;
}
//////////////////////////////////////////////////////////////////////////
/* A host may pass any hashable value as an attribute key: the messages
   show its repr, and a deleted key that is missing becomes the argument of
   the AttributeError, as PyObject_GenericSetAttr raises it. */
static int32_t __test_attribute_native_key_errors(void) {
    static const char source[] =
        "class Slotted(object):\n"
        "    __slots__ = ()\n"
        "class Plain(object):\n"
        "    pass\n"
        "slotted = Slotted()\n"
        "plain = Plain()\n"
        "plain.present = 1\n"
        "delegating = super(Plain, plain)\n"
        "iterator = iter([])\n";
    test_allocator_state_t allocator_state;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *globals = tinypy_dict_new(vm);

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "keys.py", 7U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    tinypy_value_t *key = tinypy_integer_from_i64(vm, 123456789);
    tinypy_value_t *value = tinypy_integer_from_i64(vm, 1);

    TEST_CHECK(tinypy_object_set_attr_value(__test_attribute_global(globals, "iterator"), key, value, &error) == TINYPY_FALSE);
    TEST_CHECK(__test_attribute_error_message(vm, error, TINYPY_ERROR_TYPE, "'listiterator' object has only read-only attributes (assign to .123456789)") != 0);
    error = NULL;
    TEST_CHECK(tinypy_object_set_attr_value(__test_attribute_global(globals, "Slotted"), key, value, &error) != TINYPY_FALSE && error == NULL);
    TEST_CHECK(tinypy_object_set_attr_value(__test_attribute_global(globals, "slotted"), key, value, &error) == TINYPY_FALSE);
    TEST_CHECK(__test_attribute_error_message(vm, error, TINYPY_ERROR_ATTRIBUTE, "'Slotted' object attribute '123456789' is read-only") != 0);
    error = NULL;
    TEST_CHECK(tinypy_object_get_attr_value(__test_attribute_global(globals, "delegating"), key, &error) == NULL);
    TEST_CHECK(__test_attribute_error_message(vm, error, TINYPY_ERROR_ATTRIBUTE, "'super' object has no attribute '123456789'") != 0);
    error = NULL;
    TEST_CHECK(tinypy_internal_object_delete_attr_protocol_key(__test_attribute_global(globals, "plain"), key, &error) == TINYPY_FALSE);
    tinypy_value_t *exception = tinypy_vm_raised_exception(vm);
    TEST_CHECK(exception != NULL);
    tinypy_retain(exception);
    TEST_CHECK(__test_attribute_error_message(vm, error, TINYPY_ERROR_ATTRIBUTE, "123456789") != 0);
    error = NULL;
    tinypy_value_t *args = tinypy_object_get_attr(exception, "args", 4U, &error);
    TEST_CHECK(args != NULL && error == NULL && tinypy_tuple_size(args) == 1U && tinypy_tuple_get(args, 0U) == key);
    tinypy_release(args);
    tinypy_release(exception);
    tinypy_release(value);
    tinypy_release(key);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U && allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_class_attribute(tinypy_value_t *instance, void *payload, tinypy_value_t *name, void *user_data, tinypy_error_t **out_error) {
    int32_t *calls = (int32_t *)user_data;
    size_t size = 0U;
    const char *text = (const char *)tinypy_string_view(name, &size);

    (void)payload;
    (void)out_error;
    *calls += 1;
    if (size == 9U && memcmp(text, "__class__", 9U) == 0) {
        tinypy_value_t *marker = tinypy_string_from_bytes(tinypy_value_vm(instance), "hooked", 6U);
        return marker;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* The get_attribute callback of a host native type answers __class__, as
   tp_getattro does, though object provides it as a getset. */
static int32_t __test_native_attribute_hook_class(void) {
    static const char source[] =
        "class Derived(Native):\n"
        "    pass\n"
        "derived = Derived()\n"
        "assert (native.__class__, getattr(native, '__cl' + 'ass__'), derived.__class__) == ('hooked',) * 3\n"
        "assert isinstance(native, Native) and isinstance(derived, Native)\n";
    test_allocator_state_t allocator_state;
    tinypy_compile_options_t options;
    tinypy_native_type_spec_t spec;
    tinypy_error_t *error = NULL;
    int32_t calls = 0;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_native_type_spec_init(&spec);
    spec.get_attribute = __test_native_class_attribute;
    spec.user_data = &calls;
    tinypy_type_t *native = tinypy_native_type_new(vm, "Native", 6U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(native != NULL && error == NULL);
    tinypy_value_t *instance = tinypy_native_instance_new(native);
    tinypy_value_t *type_key = tinypy_string_from_bytes(vm, "Native", 6U);
    tinypy_value_t *instance_key = tinypy_string_from_bytes(vm, "native", 6U);
    tinypy_dict_set(globals, type_key, tinypy_type_as_value(native));
    tinypy_dict_set(globals, instance_key, instance);
    tinypy_release(instance_key);
    tinypy_release(type_key);
    tinypy_value_t *attribute = tinypy_object_get_attr(instance, "__class__", 9U, &error);
    size_t size = 0U;
    const char *text = attribute != NULL ? (const char *)tinypy_string_view(attribute, &size) : "";
    TEST_CHECK(error == NULL && size == 6U && memcmp(text, "hooked", 6U) == 0 && calls == 1);
    tinypy_release(attribute);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "hook.py", 7U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL && calls >= 4);
    tinypy_release(result);
    tinypy_release(instance);
    tinypy_release(tinypy_type_as_value(native));
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U && allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_native_embedding(void) {
    int32_t optional_error_result = __test_weakref_optional_error_outputs();

    TEST_CHECK(optional_error_result == 0);
    test_allocator_state_t allocator_state;
    test_native_state_t native_state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm;
    tinypy_native_type_spec_t spec;
    tinypy_native_type_spec_t incompatible_spec;
    const tinypy_native_type_spec_t *active_spec;
    tinypy_type_t *native_type;
    tinypy_type_t *python_base;
    tinypy_type_t *incompatible_type;
    tinypy_type_t *subtype;
    tinypy_type_t *invalid_type;
    const tinypy_type_t *subtype_bases[2];
    const tinypy_type_t *invalid_bases[2];
    tinypy_value_t *instance;
    tinypy_value_t *compact_instance;
    tinypy_value_t *weak_reference;
    tinypy_value_t *base_instance;
    tinypy_value_t *other_instance;
    tinypy_value_t *args;
    tinypy_value_t *call_args;
    tinypy_value_t *representation;
    tinypy_value_t *value;
    tinypy_value_t *native_function;
    tinypy_value_t *native_method_instance;
    tinypy_value_t *native_method;
    tinypy_value_t *native_result;
    tinypy_value_t *native_dict;
    tinypy_value_t *dict_value;
    tinypy_value_t *attribute_owner;
    tinypy_value_t *attribute_result;
    tinypy_value_t *iter_key;
    tinypy_value_t *iter_value;
    tinypy_error_t *error = NULL;
    test_native_payload_t *payload;
    const void *bytes;
    size_t byte_size;
    size_t position = 0U;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&native_state, 0, sizeof(native_state));
    allocator = __test_make_allocator(&allocator_state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    native_state.vm = vm;
    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(test_native_payload_t);
    spec.user_data = &native_state;
    spec.construct = __test_native_construct;
    spec.finalize = __test_native_finalize;
    native_type = tinypy_native_type_new(vm, "Native", 6U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(native_type != NULL);
    TEST_CHECK(error == NULL);
    native_state.base_type = native_type;

    /* Modules returned by the public Python allocation-only __new__ method
       are valid embedding values even before their namespace is created. */
    {
        tinypy_value_t *known_module = tinypy_module_new(vm, "known", 5U);
        const tinypy_type_t *module_type = tinypy_object_type(known_module);
        tinypy_value_t *type_item = (tinypy_value_t *)tinypy_type_as_const_value(module_type);
        tinypy_value_t *allocate = tinypy_object_get_attr(type_item, "__new__", 7U, &error);
        tinypy_value_t *arguments = tinypy_tuple_from_items(vm, &type_item, 1U);
        tinypy_value_t *uninitialized = tinypy_call(allocate, arguments, NULL, &error);

        TEST_CHECK(uninitialized != NULL && error == NULL);
        TEST_CHECK(tinypy_module_name(uninitialized) == NULL);
        TEST_CHECK(tinypy_module_dict(uninitialized) == NULL);
        TEST_CHECK(tinypy_module_get_value(uninitialized, "missing", 7U) == NULL);
        tinypy_value_t *marker = tinypy_integer_from_i64(vm, 17);
        tinypy_module_add_value(uninitialized, "marker", 6U, marker);
        TEST_CHECK(tinypy_module_dict(uninitialized) != NULL);
        TEST_CHECK(tinypy_module_name(uninitialized) == NULL);
        TEST_CHECK(tinypy_module_get_value(uninitialized, "marker", 6U) == marker);
        tinypy_value_t *callback = tinypy_native_function_new(vm, "callback", 8U, __test_native_return_args, NULL, NULL);
        tinypy_module_add_value(uninitialized, "callback", 8U, callback);
        TEST_CHECK(tinypy_module_get_value(uninitialized, "callback", 8U) == callback);
        tinypy_release(callback);
        tinypy_release(marker);
        tinypy_release(uninitialized);
        tinypy_release(allocate);
        tinypy_release(arguments);
        tinypy_release(known_module);
        TEST_CHECK(tinypy_vm_has_error(vm) == 0);
    }

    spec.call = __test_native_call;
    spec.repr = __test_native_repr;
    spec.hash = __test_native_hash;
    spec.compare = __test_native_compare;
    spec.get_attribute = __test_native_get_attribute;
    spec.negative = __test_native_negative;
    spec.absolute = __test_native_absolute;
    spec.add = __test_native_add;
    spec.subtract = __test_native_subtract;
    spec.multiply = __test_native_multiply;
    spec.divide = __test_native_divide;
    spec.inplace_add = __test_native_inplace_add;
    spec.inplace_subtract = __test_native_inplace_subtract;
    spec.inplace_multiply = __test_native_inplace_multiply;
    spec.inplace_divide = __test_native_inplace_divide;
    spec.reflected_add = __test_native_reflected_add;
    spec.reflected_subtract = __test_native_reflected_subtract;
    spec.reflected_multiply = __test_native_reflected_multiply;
    spec.reflected_divide = __test_native_reflected_divide;
    TEST_CHECK(tinypy_native_type_update_spec(native_type, &spec, &error) != 0);
    TEST_CHECK(error == NULL);
    active_spec = tinypy_native_type_spec(native_type);
    TEST_CHECK(active_spec->call == __test_native_call);
    TEST_CHECK(active_spec->repr == __test_native_repr);
    TEST_CHECK(active_spec->hash == __test_native_hash);
    TEST_CHECK(active_spec->compare == __test_native_compare);
    TEST_CHECK(active_spec->absolute == __test_native_absolute);
    TEST_CHECK(active_spec->add == __test_native_add);
    TEST_CHECK(active_spec->inplace_divide == __test_native_inplace_divide);
    TEST_CHECK(active_spec->reflected_subtract == __test_native_reflected_subtract);
    TEST_CHECK(active_spec->has_instance_dict != 0);
    TEST_CHECK(active_spec->has_weakrefs != 0);

    incompatible_spec = spec;
    incompatible_spec.has_instance_dict = TINYPY_FALSE;
    TEST_CHECK(tinypy_native_type_update_spec(native_type, &incompatible_spec, &error) == 0);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_TYPE);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);

    python_base = tinypy_type_new(vm, "PythonBase", 10U, NULL, 0U, NULL, NULL, &error);
    TEST_CHECK(python_base != NULL);
    subtype_bases[0] = python_base;
    subtype_bases[1] = native_type;
    subtype = tinypy_type_new(vm, "Derived", 7U, subtype_bases, 2U, NULL, NULL, &error);
    TEST_CHECK(subtype != NULL);
    TEST_CHECK(error == NULL);
    native_state.subtype = subtype;
    TEST_CHECK(tinypy_typeof(tinypy_type_as_value(subtype)) == TINYPY_VALUE_TYPE);
    instance = tinypy_native_instance_new(subtype);
    TEST_CHECK(tinypy_typeof(instance) == TINYPY_VALUE_NATIVE_INSTANCE);
    args = tinypy_tuple_from_items(vm, NULL, 0U);
    TEST_CHECK(tinypy_native_instance_construct(instance, args, NULL, &error) != 0);
    TEST_CHECK(error == NULL);
    payload = (test_native_payload_t *)tinypy_native_instance_payload(instance);
    TEST_CHECK(payload->value == 73);
    representation = tinypy_object_repr(instance, &error);
    TEST_CHECK(representation != NULL);
    bytes = tinypy_string_view(representation, &byte_size);
    TEST_CHECK(byte_size == 9U && memcmp(bytes, "native-73", 9U) == 0);
    TEST_CHECK(tinypy_object_has_attr(instance, "present", 7U) != 0);
    TEST_CHECK(tinypy_object_has_attr(instance, "missing", 7U) == 0);
    TEST_CHECK(tinypy_object_has_attr(instance, "failure", 7U) == 0);
    TEST_CHECK(tinypy_vm_has_error(vm) == 0);
    TEST_CHECK(native_state.attribute_calls == 3);
    TEST_CHECK(tinypy_hash(instance) == 73);
    value = tinypy_string_from_bytes(vm, "hash", 4U);
    native_function = tinypy_dict_get(tinypy_vm_builtins(vm), value);
    tinypy_release(value);
    call_args = tinypy_tuple_from_items(vm, &instance, 1U);
    native_state.hash_error = 1;
    native_result = tinypy_call(native_function, call_args, NULL, &error);
    TEST_CHECK(native_result == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_RUNTIME);
    bytes = tinypy_error_message(error, &byte_size);
    TEST_CHECK(byte_size == 19U && memcmp(bytes, "native hash failure", 19U) == 0);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    native_state.hash_error = 0;
    tinypy_release(call_args);

    /* Public hashing and calling permit an omitted error output. A bound
       wrapper must propagate its receiver's callback failure through the VM. */
    {
        tinypy_value_t *descriptor = tinypy_type_get_attr(python_base, "__repr__", 8U);
        tinypy_value_t *bind = tinypy_object_get_attr(descriptor, "__get__", 7U, &error);
        tinypy_value_t *arguments = tinypy_tuple_from_items(vm, &instance, 1U);
        tinypy_value_t *wrapper = tinypy_call(bind, arguments, NULL, &error);

        TEST_CHECK(wrapper != NULL && error == NULL);
        tinypy_value_t *hash_method = tinypy_object_get_attr(wrapper, "__hash__", 8U, &error);

        TEST_CHECK(hash_method != NULL && error == NULL);
        native_state.hash_error = 1;
        TEST_CHECK(tinypy_hash(wrapper) == (tinypy_hash_t)0);
        TEST_CHECK(tinypy_vm_has_error(vm) != 0);
        TEST_CHECK(tinypy_vm_raised_exception(vm) != NULL);
        tinypy_vm_clear_error(vm);
        tinypy_value_t *hash_result = tinypy_call(hash_method, args, NULL, NULL);

        TEST_CHECK(hash_result == NULL);
        TEST_CHECK(tinypy_vm_has_error(vm) != 0);
        TEST_CHECK(tinypy_vm_raised_exception(vm) != NULL);
        tinypy_vm_clear_error(vm);
        native_state.hash_error = 0;
        tinypy_hash_t expected_hash = tinypy_hash(wrapper);
        hash_result = tinypy_call(hash_method, args, NULL, NULL);
        TEST_CHECK(hash_result != NULL);
        TEST_CHECK(tinypy_integer_as_i64(hash_result) == expected_hash);
        TEST_CHECK(tinypy_vm_has_error(vm) == 0);
        tinypy_release(hash_result);
        tinypy_release(hash_method);
        tinypy_release(wrapper);
        tinypy_release(arguments);
        tinypy_release(bind);
    }

    value = tinypy_negative(instance, &error);
    TEST_CHECK(value != NULL && tinypy_integer_as_i64(value) == -73);
    TEST_CHECK(error == NULL);
    tinypy_release(value);
    value = tinypy_integer_from_i64(vm, 5);
    call_args = tinypy_tuple_from_items(vm, &value, 1U);
    tinypy_release(value);
    value = tinypy_call(instance, call_args, NULL, &error);
    TEST_CHECK(value != NULL && tinypy_integer_as_i64(value) == 78);
    TEST_CHECK(error == NULL);
    tinypy_release(value);
    tinypy_release(call_args);
    /* The type publishes the slot wrapper of its call callback, which its
       get_attribute callback, as tp_getattro, leaves to the type. */
    attribute_result = tinypy_object_get_attr(instance, "__call__", 8U, &error);
    TEST_CHECK(attribute_result == NULL && error != NULL);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    attribute_result = tinypy_type_get_attr(native_type, "__call__", 8U);
    value = tinypy_integer_from_i64(vm, 5);
    tinypy_value_t *wrapper_items[2] = {instance, value};
    call_args = tinypy_tuple_from_items(vm, wrapper_items, 2U);
    tinypy_release(value);
    value = tinypy_call(attribute_result, call_args, NULL, &error);
    TEST_CHECK(value != NULL && tinypy_integer_as_i64(value) == 78);
    TEST_CHECK(error == NULL);
    tinypy_release(value);
    tinypy_release(call_args);

    value = tinypy_integer_from_i64(vm, 73);
    TEST_CHECK(tinypy_compare_bool(instance, value, TINYPY_COMPARE_EQUAL, &error) == 1);
    TEST_CHECK(tinypy_compare_bool(value, instance, TINYPY_COMPARE_EQUAL, &error) == 1);
    TEST_CHECK(tinypy_compare_bool(instance, value, TINYPY_COMPARE_NOT_EQUAL, &error) == 0);
    TEST_CHECK(tinypy_compare_bool(value, instance, TINYPY_COMPARE_NOT_EQUAL, &error) == 0);
    TEST_CHECK(error == NULL);
    TEST_CHECK(native_state.compare_calls == 4);

    attribute_owner = tinypy_type_as_value(python_base);
    dict_value = tinypy_integer_from_i64(vm, 91);
    TEST_CHECK(tinypy_object_set_attr_value(attribute_owner, value, dict_value, &error) != 0);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_object_has_attr_value(attribute_owner, instance) != 0);
    attribute_result = tinypy_object_get_attr_value(attribute_owner, instance, &error);
    TEST_CHECK(attribute_result != NULL && tinypy_integer_as_i64(attribute_result) == 91);
    TEST_CHECK(error == NULL);
    tinypy_release(attribute_result);
    tinypy_release(dict_value);

    native_dict = tinypy_dict_new(vm);
    dict_value = tinypy_integer_from_i64(vm, 91);
    tinypy_dict_set(native_dict, value, dict_value);
    TEST_CHECK(tinypy_dict_contains(native_dict, instance) != 0);
    TEST_CHECK(tinypy_dict_get_optional(native_dict, instance) == dict_value);
    tinypy_dict_clear(native_dict);
    tinypy_dict_set(native_dict, instance, dict_value);
    TEST_CHECK(tinypy_dict_contains(native_dict, value) != 0);
    TEST_CHECK(tinypy_dict_get_optional(native_dict, value) == dict_value);
    tinypy_release(dict_value);
    tinypy_release(native_dict);

    tinypy_release(value);

    base_instance = tinypy_native_instance_new(native_type);
    other_instance = tinypy_native_instance_new(subtype);
    TEST_CHECK(tinypy_native_instance_construct(base_instance, args, NULL, &error) != 0);
    TEST_CHECK(tinypy_native_instance_construct(other_instance, args, NULL, &error) != 0);
    TEST_CHECK(error == NULL);
    native_state.compare_order_count = 0U;
    TEST_CHECK(tinypy_compare_bool(base_instance, instance, TINYPY_COMPARE_EQUAL, &error) == 0);
    TEST_CHECK(error == NULL);
    TEST_CHECK(native_state.compare_order_count == 3U);
    TEST_CHECK(native_state.compare_order[0] == 2 && native_state.compare_order[1] == 1 && native_state.compare_order[2] == 2);
    native_state.compare_order_count = 0U;
    /* PyObject_RichCompare first tries the equal-type fast slot, then
       try_rich_compare asks both operands after NotImplemented. */
    TEST_CHECK(tinypy_compare_bool(instance, other_instance, TINYPY_COMPARE_EQUAL, &error) == 0);
    TEST_CHECK(error == NULL);
    TEST_CHECK(native_state.compare_order_count == 3U);
    TEST_CHECK(native_state.compare_order[0] == 2 && native_state.compare_order[1] == 2 && native_state.compare_order[2] == 2);

    value = tinypy_integer_from_i64(vm, 7);
    native_result = tinypy_add(instance, value, &error);
    TEST_CHECK(native_result != NULL && tinypy_integer_as_i64(native_result) == 80);
    tinypy_release(native_result);
    native_result = tinypy_subtract(instance, value, &error);
    TEST_CHECK(native_result != NULL && tinypy_integer_as_i64(native_result) == 66);
    tinypy_release(native_result);
    native_result = tinypy_multiply(instance, value, &error);
    TEST_CHECK(native_result != NULL && tinypy_integer_as_i64(native_result) == 511);
    tinypy_release(native_result);
    native_result = tinypy_divide(instance, value, &error);
    TEST_CHECK(native_result != NULL && tinypy_integer_as_i64(native_result) == 10);
    tinypy_release(native_result);
    native_result = tinypy_add(value, instance, &error);
    TEST_CHECK(native_result != NULL && tinypy_integer_as_i64(native_result) == 80);
    tinypy_release(native_result);
    native_result = tinypy_subtract(value, instance, &error);
    TEST_CHECK(native_result != NULL && tinypy_integer_as_i64(native_result) == -66);
    tinypy_release(native_result);
    native_result = tinypy_multiply(value, instance, &error);
    TEST_CHECK(native_result != NULL && tinypy_integer_as_i64(native_result) == 511);
    tinypy_release(native_result);
    tinypy_release(value);
    value = tinypy_integer_from_i64(vm, 146);
    native_result = tinypy_divide(value, instance, &error);
    TEST_CHECK(native_result != NULL && tinypy_integer_as_i64(native_result) == 2);
    tinypy_release(native_result);
    tinypy_release(value);
    TEST_CHECK(error == NULL);
    TEST_CHECK(native_state.binary_calls == 4);
    TEST_CHECK(native_state.reflected_calls == 4);

    native_function = tinypy_native_function_new(vm, "collect", 7U, __test_native_return_args, NULL, NULL);
    tinypy_type_set_attr(python_base, "collect", 7U, native_function);
    value = tinypy_type_as_value(python_base);
    native_method_instance = tinypy_call(value, args, NULL, &error);
    TEST_CHECK(native_method_instance != NULL);
    TEST_CHECK(error == NULL);
    native_method = tinypy_object_get_attr(native_method_instance, "collect", 7U, &error);
    TEST_CHECK(native_method != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_typeof(native_method) == TINYPY_VALUE_NATIVE_FUNCTION);
    native_result = tinypy_call(native_method, args, NULL, &error);
    TEST_CHECK(native_result != NULL);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_tuple_size(native_result) == 1U);
    TEST_CHECK(tinypy_tuple_get(native_result, 0U) == native_method_instance);
    tinypy_release(native_result);
    tinypy_release(native_method);
    tinypy_release(native_method_instance);
    tinypy_release(native_function);

    /* A public native callback may retain its argument tuple. reduce must
       replace a shared tuple before preparing the next argument pair. */
    {
        static const char source[] = "reduce(keep_pairs, [1, 2, 3])";
        test_native_reduce_state_t reduce_state;
        tinypy_compile_options_t options;

        (void)memset(&reduce_state, 0, sizeof(reduce_state));
        tinypy_value_t *callback = tinypy_native_function_new(vm, "keep_pairs", 10U, __test_native_reduce_retain_args, &reduce_state, NULL);
        tinypy_value_t *globals = tinypy_dict_new(vm);
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "keep_pairs", 10U);

        tinypy_dict_set(globals, key, callback);
        tinypy_compile_options_init(&options, TINYPY_COMPILE_EVAL);
        tinypy_value_t *code = tinypy_compile_source(vm, source, sizeof(source) - 1U, "native_reduce.py", sizeof("native_reduce.py") - 1U, &options, &error);

        TEST_CHECK(code != NULL && error == NULL);
        tinypy_value_t *result = tinypy_eval_code(code, globals, NULL, &error);

        TEST_CHECK(result != NULL && error == NULL);
        TEST_CHECK(tinypy_integer_as_i64(result) == 6);
        tinypy_release(result);
        tinypy_release(code);
        tinypy_dict_clear(globals);
        tinypy_release(globals);
        tinypy_release(key);
        tinypy_release(callback);
        TEST_CHECK(reduce_state.calls == 2U);
        TEST_CHECK(reduce_state.arguments[0] != reduce_state.arguments[1]);
        TEST_CHECK(tinypy_tuple_size(reduce_state.arguments[0]) == 2U);
        TEST_CHECK(tinypy_tuple_size(reduce_state.arguments[1]) == 2U);
        TEST_CHECK(tinypy_integer_as_i64(tinypy_tuple_get(reduce_state.arguments[0], 0U)) == 1);
        TEST_CHECK(tinypy_integer_as_i64(tinypy_tuple_get(reduce_state.arguments[0], 1U)) == 2);
        TEST_CHECK(tinypy_integer_as_i64(tinypy_tuple_get(reduce_state.arguments[1], 0U)) == 3);
        TEST_CHECK(tinypy_integer_as_i64(tinypy_tuple_get(reduce_state.arguments[1], 1U)) == 3);
        tinypy_release(reduce_state.arguments[0]);
        tinypy_release(reduce_state.arguments[1]);
        TEST_CHECK(tinypy_vm_has_error(vm) == 0);
    }

    value = tinypy_integer_from_i64(vm, 3);
    native_result = tinypy_inplace_add(instance, value, &error);
    TEST_CHECK(native_result == instance);
    tinypy_release(native_result);
    native_result = tinypy_inplace_subtract(instance, value, &error);
    TEST_CHECK(native_result == instance);
    tinypy_release(native_result);
    native_result = tinypy_inplace_multiply(instance, value, &error);
    TEST_CHECK(native_result == instance);
    tinypy_release(native_result);
    native_result = tinypy_inplace_divide(instance, value, &error);
    TEST_CHECK(native_result == instance);
    tinypy_release(native_result);
    tinypy_release(value);
    TEST_CHECK(error == NULL);
    TEST_CHECK(((test_native_payload_t *)tinypy_native_instance_payload(instance))->value == 73);
    TEST_CHECK(native_state.inplace_calls == 4);

    native_function = tinypy_native_function_new(vm, "raise_error", 11U, __test_native_raise_error, vm, NULL);
    native_result = tinypy_call(native_function, args, NULL, &error);
    TEST_CHECK(native_result == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_VALUE);
    bytes = tinypy_error_message(error, &byte_size);
    TEST_CHECK(byte_size == 23U && memcmp(bytes, "native callback failure", 23U) == 0);
    TEST_CHECK(tinypy_vm_has_error(vm) != 0);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    tinypy_release(native_function);

    value = tinypy_integer_from_i64(vm, 11);
    TEST_CHECK(tinypy_object_set_attr(instance, "answer", 6U, value, &error) != 0);
    TEST_CHECK(error == NULL);
    TEST_CHECK(tinypy_dict_next(tinypy_instance_dict(instance), &position, &iter_key, &iter_value) != 0);
    TEST_CHECK(tinypy_integer_as_i64(iter_value) == 11);
    tinypy_release(value);

    tinypy_native_type_spec_init(&incompatible_spec);
    incompatible_spec.payload_size = sizeof(test_native_payload_t) + sizeof(void *);
    incompatible_spec.has_instance_dict = TINYPY_FALSE;
    incompatible_spec.has_weakrefs = TINYPY_FALSE;
    incompatible_type = tinypy_native_type_new(vm, "Other", 5U, NULL, 0U, NULL, &incompatible_spec, &error);
    TEST_CHECK(incompatible_type != NULL);
    active_spec = tinypy_native_type_spec(incompatible_type);
    TEST_CHECK(active_spec->has_instance_dict == 0);
    TEST_CHECK(active_spec->has_weakrefs == 0);
    compact_instance = tinypy_native_instance_new(incompatible_type);
    TEST_CHECK(tinypy_instance_dict(compact_instance) == NULL);
    weak_reference = tinypy_weakref_new(compact_instance, NULL, &error);
    TEST_CHECK(weak_reference == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_TYPE);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    tinypy_release(compact_instance);
    invalid_bases[0] = native_type;
    invalid_bases[1] = incompatible_type;
    invalid_type = tinypy_type_new(vm, "Invalid", 7U, invalid_bases, 2U, NULL, NULL, &error);
    TEST_CHECK(invalid_type == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_TYPE);
    bytes = tinypy_error_message(error, &byte_size);
    TEST_CHECK(byte_size == 56U && memcmp(bytes, "multiple bases have incompatible native instance layouts", 56U) == 0);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);

    /* Native roots laying their payload out identically combine as bases,
       the way C++ classes bound with several bases do; the first base
       keeps the layout. */
    tinypy_native_type_spec_t twin_spec;
    tinypy_native_type_spec_init(&twin_spec);
    twin_spec.payload_size = sizeof(test_native_payload_t);
    tinypy_type_t *twin_type = tinypy_native_type_new(vm, "Twin", 4U, NULL, 0U, NULL, &twin_spec, &error);
    TEST_CHECK(twin_type != NULL && error == NULL);
    const tinypy_type_t *twin_bases[2] = {native_type, twin_type};
    tinypy_type_t *combined_type = tinypy_native_type_new(vm, "Combined", 8U, twin_bases, 2U, NULL, &twin_spec, &error);
    TEST_CHECK(combined_type != NULL && error == NULL);
    TEST_CHECK(tinypy_type_is_subtype(combined_type, native_type) != 0);
    TEST_CHECK(tinypy_type_is_subtype(combined_type, twin_type) != 0);
    tinypy_value_t *combined_instance = tinypy_native_instance_new(combined_type);
    ((test_native_payload_t *)tinypy_native_instance_payload(combined_instance))->value = 91;
    TEST_CHECK(tinypy_object_type(combined_instance) == combined_type);
    TEST_CHECK(((test_native_payload_t *)tinypy_native_instance_payload(combined_instance))->value == 91);
    tinypy_release(combined_instance);
    tinypy_value_t *combined_value = tinypy_type_as_value(combined_type);
    tinypy_release(combined_value);
    tinypy_value_t *twin_value = tinypy_type_as_value(twin_type);
    tinypy_release(twin_value);

    tinypy_value_t *type_value = tinypy_type_as_value(incompatible_type);
    tinypy_release(type_value);
    tinypy_release(representation);
    tinypy_release(args);
    tinypy_release(other_instance);
    tinypy_release(base_instance);
    tinypy_release(instance);
    TEST_CHECK(native_state.constructed == 3);
    TEST_CHECK(native_state.finalized == 3);
    tinypy_value_t *type_value_2 = tinypy_type_as_value(subtype);
    tinypy_release(type_value_2);
    tinypy_value_t *type_value_3 = tinypy_type_as_value(native_type);
    tinypy_release(type_value_3);
    tinypy_value_t *type_value_4 = tinypy_type_as_value(python_base);
    tinypy_release(type_value_4);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_module_finder_state_t {
    tinypy_vm_t *vm;
    tinypy_value_t *finder;
    tinypy_value_t *replacement;
    size_t find_count;
    size_t load_count;
} test_module_finder_state_t;
//////////////////////////////////////////////////////////////////////////
static int32_t __test_module_name_is(tinypy_value_t *value, const char *expected, size_t expected_size) {
    const void *bytes;
    size_t size;

    if (tinypy_typeof(value) != TINYPY_VALUE_STRING) {
        return 0;
    }
    bytes = tinypy_string_view(value, &size);
    int32_t return_value_1 = size == expected_size && memcmp(bytes, expected, expected_size) == 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_module_finder_find(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    test_module_finder_state_t *state = (test_module_finder_state_t *)user_data;
    tinypy_value_t *name;

    (void)function;
    (void)kwargs;
    (void)out_error;
    if (tinypy_tuple_size(args) != 2U) {
        return NULL;
    }
    name = tinypy_tuple_get(args, 0U);
    state->find_count += 1U;
    if (__test_module_name_is(name, "finder_sample", 13U) == 0 && __test_module_name_is(name, "finder_broken", 13U) == 0) {
        tinypy_value_t *return_value_1 = tinypy_none_get(state->vm);
        return return_value_1;
    }
    tinypy_retain(state->finder);
    return state->finder;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_module_finder_load(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    test_module_finder_state_t *state = (test_module_finder_state_t *)user_data;
    tinypy_value_t *name;
    const char *name_bytes;
    size_t name_size;
    tinypy_value_t *module;
    tinypy_value_t *key;

    (void)function;
    (void)kwargs;
    (void)out_error;
    if (tinypy_tuple_size(args) != 1U) {
        return NULL;
    }
    name = tinypy_tuple_get(args, 0U);
    if (state->replacement != NULL) {
        tinypy_dict_set(tinypy_vm_modules(state->vm), name, state->replacement);
        state->load_count += 1U;
        tinypy_retain(state->replacement);
        return state->replacement;
    }
    name_bytes = (const char *)tinypy_string_view(name, &name_size);
    module = tinypy_module_new(state->vm, name_bytes, name_size);
    tinypy_module_add_value(module, "__name__", 8U, name);
    tinypy_value_t *vm_builtins = tinypy_vm_builtins(state->vm);
    tinypy_module_add_value(module, "__builtins__", 12U, vm_builtins);
    key = tinypy_string_from_bytes(state->vm, name_bytes, name_size);
    tinypy_value_t *vm_modules = tinypy_vm_modules(state->vm);
    tinypy_dict_set(vm_modules, key, module);
    tinypy_release(key);
    state->load_count += 1U;
    if (__test_module_name_is(name, "finder_broken", 13U) != 0) {
        tinypy_release(module);
        return NULL;
    }
    tinypy_value_t *answer = tinypy_integer_from_i64(state->vm, (int64_t)(41U + state->load_count));

    tinypy_module_add_value(module, "answer", 6U, answer);
    tinypy_release(answer);
    return module;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_module_finder(void) {
    test_allocator_state_t allocator_state;
    test_module_finder_state_t finder_state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm;
    tinypy_value_t *finder;
    tinypy_value_t *find_function;
    tinypy_value_t *load_function;
    tinypy_value_t *module;
    tinypy_value_t *answer;
    tinypy_value_t *key;
    tinypy_value_t *cached_module;
    tinypy_value_t *reload_function;
    tinypy_value_t *reload_args;
    tinypy_value_t *reload_result;
    tinypy_value_t *stale;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&finder_state, 0, sizeof(finder_state));
    allocator = __test_make_allocator(&allocator_state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    finder = tinypy_module_new(vm, "finder", 6U);
    finder_state.vm = vm;
    finder_state.finder = finder;
    find_function = tinypy_native_function_new(vm, "find_module", 11U, __test_module_finder_find, &finder_state, NULL);
    load_function = tinypy_native_function_new(vm, "load_module", 11U, __test_module_finder_load, &finder_state, NULL);
    tinypy_module_add_value(finder, "find_module", 11U, find_function);
    tinypy_module_add_value(finder, "load_module", 11U, load_function);
    tinypy_release(load_function);
    tinypy_release(find_function);
    tinypy_vm_set_module_finder(vm, finder);
    TEST_CHECK(tinypy_vm_module_finder(vm) == finder);
    tinypy_release(finder);

    module = tinypy_import_module(vm, "finder_sample", 13U, NULL, NULL, 0, &error);
    TEST_CHECK(module != NULL);
    TEST_CHECK(error == NULL);
    answer = tinypy_module_get_value(module, "answer", 6U);
    TEST_CHECK(answer != NULL && tinypy_integer_as_i64(answer) == 42);
    cached_module = tinypy_import_module(vm, "finder_sample", 13U, NULL, NULL, 0, &error);
    TEST_CHECK(cached_module == module);
    tinypy_release(cached_module);
    TEST_CHECK(finder_state.find_count == 1U && finder_state.load_count == 1U);
    stale = tinypy_integer_from_i64(vm, INT64_C(7));
    tinypy_module_add_value(module, "stale", 5U, stale);
    tinypy_release(stale);
    key = tinypy_string_from_bytes(vm, "reload", 6U);
    reload_function = tinypy_dict_get(tinypy_vm_builtins(vm), key);
    tinypy_retain(reload_function);
    tinypy_release(key);
    TEST_CHECK(reload_function != NULL && error == NULL);
    reload_args = tinypy_tuple_from_items(vm, &module, 1U);
    reload_result = tinypy_call(reload_function, reload_args, NULL, &error);
    tinypy_release(reload_args);
    TEST_CHECK(reload_result != NULL && reload_result != module && error == NULL);
    answer = tinypy_module_get_value(module, "answer", 6U);
    TEST_CHECK(answer != NULL && tinypy_integer_as_i64(answer) == 42);
    stale = tinypy_module_get_value(module, "stale", 5U);
    TEST_CHECK(stale != NULL && tinypy_integer_as_i64(stale) == 7);
    answer = tinypy_module_get_value(reload_result, "answer", 6U);
    TEST_CHECK(answer != NULL && tinypy_integer_as_i64(answer) == 43);
    TEST_CHECK(tinypy_module_get_value(reload_result, "stale", 5U) == NULL);
    key = tinypy_string_from_bytes(vm, "finder_sample", 13U);
    TEST_CHECK(tinypy_dict_get_optional(tinypy_vm_modules(vm), key) == reload_result);
    tinypy_release(key);
    TEST_CHECK(finder_state.find_count == 2U && finder_state.load_count == 2U);

    finder_state.replacement = tinypy_integer_from_i64(vm, INT64_C(17));
    reload_args = tinypy_tuple_from_items(vm, &reload_result, 1U);
    cached_module = tinypy_call(reload_function, reload_args, NULL, &error);
    tinypy_release(reload_args);
    TEST_CHECK(cached_module == finder_state.replacement && error == NULL);
    TEST_CHECK(tinypy_integer_as_i64(cached_module) == 17);
    key = tinypy_string_from_bytes(vm, "finder_sample", 13U);
    TEST_CHECK(tinypy_dict_get_optional(tinypy_vm_modules(vm), key) == cached_module);
    tinypy_release(key);
    answer = tinypy_module_get_value(reload_result, "answer", 6U);
    TEST_CHECK(answer != NULL && tinypy_integer_as_i64(answer) == 43);
    tinypy_release(cached_module);
    tinypy_release(finder_state.replacement);
    finder_state.replacement = NULL;
    tinypy_release(reload_function);
    TEST_CHECK(finder_state.find_count == 3U && finder_state.load_count == 3U);
    tinypy_release(reload_result);
    tinypy_release(module);

    module = tinypy_import_module(vm, "finder_broken", 13U, NULL, NULL, 0, &error);
    TEST_CHECK(module == NULL);
    TEST_CHECK(error != NULL);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    key = tinypy_string_from_bytes(vm, "finder_broken", 13U);
    TEST_CHECK(tinypy_dict_contains(tinypy_vm_modules(vm), key) == 0);
    tinypy_release(key);
    TEST_CHECK(finder_state.find_count == 4U && finder_state.load_count == 4U);

    tinypy_vm_set_module_finder(vm, NULL);
    TEST_CHECK(tinypy_vm_module_finder(vm) == NULL);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* Offers every module it is asked for, as source that sets maxint. */
static const tinypy_module_artifact_t *__test_module_finder_protocol_resolve(void *user_data, const tinypy_module_request_t *request) {
    static const char source[] = "maxint = 1\n";
    tinypy_module_artifact_t *artifact = (tinypy_module_artifact_t *)user_data;

    (void)memset(artifact, 0, sizeof(*artifact));
    artifact->abi_version = TINYPY_ABI_VERSION;
    artifact->struct_size = (uint32_t)sizeof(*artifact);
    artifact->content_kind = TINYPY_MODULE_CONTENT_SOURCE;
    artifact->data = source;
    artifact->data_size = sizeof(source) - 1U;
    artifact->canonical_name = request->canonical_name;
    artifact->canonical_name_size = request->canonical_name_size;
    return artifact;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __test_module_finder_protocol_run(tinypy_vm_t *vm, const char *source, size_t source_size, const char *filename, tinypy_value_t *globals) {
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *result = tinypy_exec_source(vm, source, source_size, filename, strlen(filename), globals, NULL, &options, &error);
    if (result == NULL) {
        size_t message_size = 0U;
        const char *message = error != NULL ? tinypy_error_message(error, &message_size) : "";

        (void)fprintf(stderr, "module finder protocol: %.*s\n", (int)message_size, message);
        tinypy_error_release(error);
        tinypy_vm_clear_error(vm);
        return TINYPY_FALSE;
    }
    tinypy_release(result);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_module_finder_drop(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)args;
    (void)kwargs;
    (void)user_data;
    (void)out_error;
    tinypy_vm_set_module_finder(vm, NULL);
    tinypy_value_t *none = tinypy_none_get(vm);
    return none;
}
//////////////////////////////////////////////////////////////////////////
/* The host finder follows the protocol of sys.meta_path and comes after
   it: an ImportError of find_module means that nobody provides the module,
   so the host resolver is not asked, and the modules the VM provides come
   before the resolver, which reload() of sys does not consult. The name an
   implicit relative import misses is marked once the absolute head
   imported, and a finder the host drops while it is asked is released
   before its loader runs. */
static int32_t __test_module_finder_protocol(void) {
    static const char finders[] =
        "import sys\n"
        "calls = []\n"
        "events = []\n"
        "sources = {'marked': 'import marked.sub\\n', 'marked.sub': 'import marked.other\\n', 'marked.other': ''}\n"
        "class Loader(object):\n"
        "    def __init__(self, origin):\n"
        "        self.origin = origin\n"
        "    def load_module(self, name):\n"
        "        module = type(sys)(name)\n"
        "        module.origin = self.origin\n"
        "        return module\n"
        "class SourceLoader(object):\n"
        "    def load_module(self, name):\n"
        "        module = sys.modules[name] = type(sys)(name)\n"
        "        if name == 'marked':\n"
        "            module.__path__ = []\n"
        "        exec sources[name] in vars(module)\n"
        "        return module\n"
        "class HostFinder(object):\n"
        "    def find_module(self, name, path=None):\n"
        "        calls.append(name)\n"
        "        if name == 'finder_missing' or name == 'marked.marked':\n"
        "            raise ImportError('the host finder has no ' + name)\n"
        "        if name.startswith('finder_'):\n"
        "            return Loader('host')\n"
        "        if name in sources:\n"
        "            return SourceLoader()\n"
        "        return None\n"
        "class MetaFinder(object):\n"
        "    def find_module(self, name, path=None):\n"
        "        if name == 'finder_meta':\n"
        "            return Loader('meta')\n"
        "        return None\n"
        "class EventLoader(object):\n"
        "    def load_module(self, name):\n"
        "        events.append('load')\n"
        "        return type(sys)(name)\n"
        "class DroppedFinder(object):\n"
        "    def __del__(self):\n"
        "        events.append('finder released')\n"
        "    def find_module(self, name, path=None):\n"
        "        events.append('find')\n"
        "        drop_finder()\n"
        "        return EventLoader()\n"
        "host_finder = HostFinder()\n"
        "dropped_finder = DroppedFinder()\n";
    static const char checks[] =
        "try:\n"
        "    import finder_missing\n"
        "except ImportError as error:\n"
        "    assert str(error) == 'No module named finder_missing', error\n"
        "else:\n"
        "    raise AssertionError('the resolver was asked after the finder')\n"
        "import finder_hosted\n"
        "assert finder_hosted.origin == 'host' and sys.modules['finder_hosted'] is finder_hosted\n"
        "sys.meta_path.append(MetaFinder())\n"
        "import finder_meta\n"
        "assert finder_meta.origin == 'meta' and 'finder_meta' not in calls\n"
        "assert reload(sys) is sys and sys.maxint > 1 and calls[-1] == 'sys'\n"
        "del sys.meta_path\n"
        "try:\n"
        "    import finder_unlisted\n"
        "except RuntimeError as error:\n"
        "    assert str(error) == 'sys.meta_path must be a list of import hooks', error\n"
        "else:\n"
        "    raise AssertionError('imported without sys.meta_path')\n"
        "sys.meta_path = []\n"
        "import resolved_module\n"
        "assert resolved_module.maxint == 1 and calls[-1] == 'resolved_module'\n"
        "start = len(calls)\n"
        "import marked\n"
        "assert calls[start:] == ['marked', 'marked.marked', 'marked.sub', 'marked.other'], calls[start:]\n"
        "assert sys.modules['marked.marked'] is None\n";
    static const char dropping[] =
        "del dropped_finder\n"
        "import dropped_module\n"
        "assert events == ['find', 'finder released', 'load'], events\n";
    test_allocator_state_t allocator_state;
    tinypy_module_artifact_t artifact;
    tinypy_host_t host;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = &artifact;
    host.resolve_module = __test_module_finder_protocol_resolve;
    host.release_module_artifact = __test_heap_budget_release;
    config.host = &host;
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *drop_function = tinypy_native_function_new(vm, "drop_finder", 11U, __test_module_finder_drop, NULL, NULL);
    tinypy_value_t *drop_key = tinypy_string_from_bytes(vm, "drop_finder", 11U);

    tinypy_dict_set(globals, drop_key, drop_function);
    tinypy_release(drop_key);
    tinypy_release(drop_function);
    TEST_CHECK(__test_module_finder_protocol_run(vm, finders, sizeof(finders) - 1U, "finders.py", globals) != TINYPY_FALSE);
    tinypy_value_t *finder_key = tinypy_string_from_bytes(vm, "host_finder", 11U);
    tinypy_value_t *finder = tinypy_dict_get(globals, finder_key);
    tinypy_release(finder_key);
    TEST_CHECK(finder != NULL);
    tinypy_vm_set_module_finder(vm, finder);
    TEST_CHECK(__test_module_finder_protocol_run(vm, checks, sizeof(checks) - 1U, "checks.py", globals) != TINYPY_FALSE);
    finder_key = tinypy_string_from_bytes(vm, "dropped_finder", 14U);
    finder = tinypy_dict_get(globals, finder_key);
    tinypy_release(finder_key);
    TEST_CHECK(finder != NULL);
    tinypy_vm_set_module_finder(vm, finder);
    TEST_CHECK(__test_module_finder_protocol_run(vm, dropping, sizeof(dropping) - 1U, "dropping.py", globals) != TINYPY_FALSE);
    TEST_CHECK(tinypy_vm_module_finder(vm) == NULL);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_module_resolver_state_t {
    tinypy_vm_t *vm;
    tinypy_module_artifact_t artifact;
    size_t release_count;
} test_module_resolver_state_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __test_module_request_is(const tinypy_module_request_t *request, const char *name) {
    size_t name_size = strlen(name);

    tinypy_bool_t equal = request->canonical_name_size == name_size && memcmp(request->canonical_name, name, name_size) == 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return equal;
}
//////////////////////////////////////////////////////////////////////////
/* Raises ValueError and reports the success that native_pending asks for. */
static tinypy_bool_t __test_module_resolver_contract_initialize(tinypy_value_t *module, void *user_data, tinypy_error_t **out_error) {
    (void)out_error;
    tinypy_vm_raise_error(tinypy_value_vm(module), TINYPY_ERROR_VALUE, "the initializer raised");
    return user_data != NULL ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* Offers each module as source, with the field its name tells left NULL
   but sized, or raises the error its name tells. The native modules
   initialize with an exception raised. */
static const tinypy_module_artifact_t *__test_module_resolver_contract_resolve(void *user_data, const tinypy_module_request_t *request) {
    static const char source[] = "VALUE = 1\n";
    test_module_resolver_state_t *state = (test_module_resolver_state_t *)user_data;

    if (__test_module_request_is(request, "raising_runtime") != TINYPY_FALSE) {
        tinypy_vm_raise_error(state->vm, TINYPY_ERROR_RUNTIME, "the resolver failed");
        return NULL;
    }
    if (__test_module_request_is(request, "raising_import") != TINYPY_FALSE) {
        tinypy_vm_raise_error(state->vm, TINYPY_ERROR_IMPORT, "the resolver has no module");
        return NULL;
    }
    (void)memset(&state->artifact, 0, sizeof(state->artifact));
    state->artifact.abi_version = TINYPY_ABI_VERSION;
    state->artifact.struct_size = (uint32_t)sizeof(state->artifact);
    state->artifact.content_kind = TINYPY_MODULE_CONTENT_SOURCE;
    state->artifact.data = source;
    state->artifact.data_size = sizeof(source) - 1U;
    state->artifact.canonical_name = request->canonical_name;
    state->artifact.canonical_name_size = request->canonical_name_size;
    if (__test_module_request_is(request, "null_filename") != TINYPY_FALSE) {
        state->artifact.logical_filename_size = 6U;
    }
    if (__test_module_request_is(request, "null_token") != TINYPY_FALSE) {
        state->artifact.flags = (uint32_t)TINYPY_MODULE_ARTIFACT_PACKAGE;
        state->artifact.package_token_size = 4U;
    }
    if (__test_module_request_is(request, "raising_artifact") != TINYPY_FALSE) {
        tinypy_vm_raise_error(state->vm, TINYPY_ERROR_RUNTIME, "the resolver raised");
    }
    if (__test_module_request_is(request, "native_pending") != TINYPY_FALSE || __test_module_request_is(request, "native_failing") != TINYPY_FALSE) {
        state->artifact.content_kind = TINYPY_MODULE_CONTENT_NATIVE;
        state->artifact.native_initialize = __test_module_resolver_contract_initialize;
        state->artifact.native_user_data = __test_module_request_is(request, "native_pending") != TINYPY_FALSE ? state : NULL;
    }
    return &state->artifact;
}
//////////////////////////////////////////////////////////////////////////
static void __test_module_resolver_contract_release(void *user_data, const tinypy_module_artifact_t *artifact) {
    test_module_resolver_state_t *state = (test_module_resolver_state_t *)user_data;

    (void)artifact;
    state->release_count += 1U;
}
//////////////////////////////////////////////////////////////////////////
/* Imports name, which must fail with an error of kind and message. */
static tinypy_bool_t __test_module_import_fails(tinypy_vm_t *vm, const char *name, tinypy_error_kind_e kind, const char *message) {
    tinypy_error_t *error = NULL;
    tinypy_value_t *module = tinypy_import_module(vm, name, strlen(name), NULL, NULL, 0, &error);
    size_t message_size = 0U;
    const char *text = error != NULL ? tinypy_error_message(error, &message_size) : "";
    tinypy_bool_t failed = module == NULL && error != NULL && tinypy_error_kind(error) == kind && message_size == strlen(message) && memcmp(text, message, message_size) == 0 ? TINYPY_TRUE : TINYPY_FALSE;

    if (module != NULL) {
        tinypy_release(module);
    }
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_vm_clear_error(vm);
    return failed;
}
//////////////////////////////////////////////////////////////////////////
/* An artifact with a NULL field of nonzero size is invalid, and a resolver
   that returns no artifact fails the import with the exception it raised,
   except an ImportError, which means that it has no module. An exception
   raised with an artifact, or by a native initializer whatever it reports,
   fails the import too and leaves no module in sys.modules. */
static int32_t __test_module_resolver_contract(void) {
    test_allocator_state_t allocator_state;
    test_module_resolver_state_t resolver_state;
    tinypy_host_t host;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&resolver_state, 0, sizeof(resolver_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = &resolver_state;
    host.resolve_module = __test_module_resolver_contract_resolve;
    host.release_module_artifact = __test_module_resolver_contract_release;
    config.host = &host;
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    resolver_state.vm = vm;

    TEST_CHECK(__test_module_import_fails(vm, "null_filename", TINYPY_ERROR_IMPORT, "host resolver returned an invalid module artifact") != TINYPY_FALSE);
    TEST_CHECK(__test_module_import_fails(vm, "null_token", TINYPY_ERROR_IMPORT, "host resolver returned an invalid module artifact") != TINYPY_FALSE);
    TEST_CHECK(resolver_state.release_count == 2U);
    TEST_CHECK(__test_module_import_fails(vm, "raising_runtime", TINYPY_ERROR_RUNTIME, "the resolver failed") != TINYPY_FALSE);
    TEST_CHECK(__test_module_import_fails(vm, "raising_import", TINYPY_ERROR_IMPORT, "No module named raising_import") != TINYPY_FALSE);
    TEST_CHECK(__test_module_import_fails(vm, "raising_artifact", TINYPY_ERROR_RUNTIME, "the resolver raised") != TINYPY_FALSE);
    TEST_CHECK(__test_module_import_fails(vm, "native_pending", TINYPY_ERROR_VALUE, "the initializer raised") != TINYPY_FALSE);
    TEST_CHECK(__test_module_import_fails(vm, "native_failing", TINYPY_ERROR_VALUE, "the initializer raised") != TINYPY_FALSE);
    TEST_CHECK(resolver_state.release_count == 5U);
    tinypy_value_t *modules = tinypy_vm_modules(vm);
    static const char *const failed_names[] = {"null_filename", "raising_artifact", "native_pending", "native_failing"};
    for (size_t index = 0U; index < sizeof(failed_names) / sizeof(failed_names[0]); ++index) {
        tinypy_value_t *key = tinypy_string_from_bytes(vm, failed_names[index], strlen(failed_names[index]));
        TEST_CHECK(tinypy_dict_contains(modules, key) == 0);
        tinypy_release(key);
    }
    tinypy_value_t *module = tinypy_import_module(vm, "valid", 5U, NULL, NULL, 0, &error);
    TEST_CHECK(module != NULL && error == NULL);
    tinypy_value_t *value = tinypy_module_get_value(module, "VALUE", 5U);
    TEST_CHECK(value != NULL && tinypy_integer_as_i64(value) == 1);
    TEST_CHECK(resolver_state.release_count == 6U);
    tinypy_release(module);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* Consumes the value returned by the build operation under test. */
static tinypy_bool_t __test_build_value_matches(tinypy_value_t *value, const char *expected) {
    if (value == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_value_t *repr = tinypy_object_repr(value, NULL);
    tinypy_release(value);
    if (repr == NULL) {
        return TINYPY_FALSE;
    }
    size_t size;
    const void *bytes = tinypy_string_view(repr, &size);
    tinypy_bool_t matches = size == strlen(expected) && memcmp(bytes, expected, size) == 0 ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_release(repr);
    return matches;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_build_value_va(tinypy_vm_t *vm, const char *format, ...) {
    va_list args;

    va_start(args, format);
    tinypy_value_t *first = tinypy_build_value_va(vm, format, args, NULL);
    tinypy_value_t *second = tinypy_build_value_va(vm, format, args, NULL);
    va_end(args);
    TEST_CHECK(__test_build_value_matches(first, "(7, 'copied')"));
    TEST_CHECK(__test_build_value_matches(second, "(7, 'copied')"));
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_build_value_converter_state_t {
    tinypy_value_t *exception;
    tinypy_value_t *result;
    size_t calls;
} test_build_value_converter_state_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_build_value_converter(void *argument) {
    test_build_value_converter_state_t *state = (test_build_value_converter_state_t *)argument;
    state->calls += 1U;
    if (state->exception != NULL) {
        (void)tinypy_exception_raise(state->exception, NULL, NULL);
    }
    if (state->result != NULL) {
        tinypy_retain(state->result);
    }
    return state->result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_build_value(void) {
    test_allocator_state_t allocator_state = {0};
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_error_t *error = NULL;

    TEST_CHECK(vm != NULL);
    TEST_CHECK(__test_build_value_matches(tinypy_build_value(vm, &error, ""), "None"));
    TEST_CHECK(error == NULL);
    TEST_CHECK(__test_build_value_matches(tinypy_build_value(vm, NULL, NULL), "None"));
    TEST_CHECK(__test_build_value_matches(tinypy_build_value(vm, NULL, " ,\t:"), "None"));
    TEST_CHECK(__test_build_value_matches(tinypy_build_value(vm, NULL, "i", 7), "7"));
    TEST_CHECK(__test_build_value_matches(tinypy_build_value(vm, NULL, "()[]{}"), "((), [], {})"));
    TEST_CHECK(__test_build_value_matches(tinypy_build_value(vm, NULL, "(i[sz]{s:(iL)})", 7, "bytes", (const char *)NULL, "key", 3, 9LL), "(7, ['bytes', None], {'key': (3, 9L)})"));
    TEST_CHECK(__test_build_value_matches(tinypy_build_value(vm, NULL, "bBhHInlkLKfdc", -3, 255, -300, 65535U, 4000000000U, (ptrdiff_t)-4, -5L, 6UL, -7LL, 18446744073709551615ULL, 1.5, 2.5, 'x'), "(-3, 255, -300, 65535, 4000000000, -4, -5, 6, -7L, 18446744073709551615L, 1.5, 2.5, 'x')"));
    TEST_CHECK(__test_build_value_matches(tinypy_build_value(vm, NULL, "LK", 0LL, 0ULL), "(0L, 0L)"));
    TEST_CHECK(__test_build_value_matches(tinypy_build_value(vm, NULL, "s#z#s#", "a\0b", 3, (const char *)NULL, 100, "end", -1), "('a\\x00b', None, 'end')"));
    TEST_CHECK(__test_build_value_va(vm, "is", 7, "copied") == 0);

    const double complex_parts[] = {1.5, -2.5};
    tinypy_value_t *complex_value = tinypy_build_value(vm, NULL, "D", (const void *)complex_parts);
    TEST_CHECK(complex_value != NULL && tinypy_typeof(complex_value) == TINYPY_VALUE_COMPLEX);
    double real;
    double imaginary;
    tinypy_complex_as_doubles(complex_value, &real, &imaginary);
    TEST_CHECK(real == 1.5 && imaginary == -2.5);
    tinypy_release(complex_value);

    const wchar_t wide[] = L"\u00e9\U0001f600";
    const char expected_utf8[] = "\xc3\xa9\xf0\x9f\x98\x80";
    tinypy_value_t *unicode = tinypy_build_value(vm, NULL, "u", wide);
    TEST_CHECK(unicode != NULL && tinypy_typeof(unicode) == TINYPY_VALUE_UNICODE);
    size_t byte_size;
    size_t code_points;
    const char *utf8 = tinypy_unicode_utf8_view(unicode, &byte_size, &code_points);
    TEST_CHECK(byte_size == sizeof(expected_utf8) - 1U && code_points == 2U);
    TEST_CHECK(memcmp(utf8, expected_utf8, byte_size) == 0);
    tinypy_release(unicode);
    TEST_CHECK(__test_build_value_matches(tinypy_build_value(vm, NULL, "u#u#", L"a\0b", 3, (const wchar_t *)NULL, 1), "(u'a\\x00b', None)"));

    tinypy_value_t *shared = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_ref_t references = tinypy_refcount(shared);
    tinypy_value_t *retained = tinypy_build_value(vm, NULL, "OS", shared, shared);
    TEST_CHECK(retained != NULL && tinypy_refcount(shared) == references + 2);
    tinypy_release(retained);
    TEST_CHECK(tinypy_refcount(shared) == references);
    tinypy_retain(shared);
    tinypy_value_t *transferred = tinypy_build_value(vm, NULL, "N", shared);
    TEST_CHECK(transferred == shared && tinypy_refcount(shared) == references + 1);
    tinypy_release(transferred);
    TEST_CHECK(tinypy_refcount(shared) == references);

    test_build_value_converter_state_t converter = {NULL, shared, 0U};
    tinypy_value_t *converted = tinypy_build_value(vm, NULL, "O&", __test_build_value_converter, (void *)&converter);
    TEST_CHECK(converted == shared && converter.calls == 1U && tinypy_refcount(shared) == references + 1);
    tinypy_release(converted);

    tinypy_vm_raise_error(vm, TINYPY_ERROR_VALUE, "first-converter-error");
    tinypy_value_t *first_error = tinypy_vm_raised_exception(vm);
    tinypy_retain(first_error);
    tinypy_vm_clear_error(vm);
    tinypy_vm_raise_error(vm, TINYPY_ERROR_TYPE, "second-converter-error");
    tinypy_value_t *second_error = tinypy_vm_raised_exception(vm);
    tinypy_retain(second_error);
    tinypy_vm_clear_error(vm);
    test_build_value_converter_state_t first = {first_error, NULL, 0U};
    test_build_value_converter_state_t second = {second_error, NULL, 0U};
    tinypy_retain(shared);
    tinypy_retain(shared);
    tinypy_value_t *failed = tinypy_build_value(vm, &error, "(O&[N]O&N)", __test_build_value_converter, (void *)&first, shared, __test_build_value_converter, (void *)&second, shared);
    TEST_CHECK(failed == NULL && first.calls == 1U && second.calls == 1U);
    TEST_CHECK(tinypy_refcount(shared) == references);
    TEST_CHECK(tinypy_vm_raised_exception(vm) == first_error);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_VALUE);
    TEST_CHECK(strcmp(tinypy_error_message(error, NULL), "first-converter-error") == 0);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);

    /* Format errors must use the VM's real SystemError type even if a host
     * has replaced that name in the mutable builtins dictionary. */
    tinypy_value_t *system_key = tinypy_string_from_bytes(vm, "SystemError", 11U);
    tinypy_value_t *builtins = tinypy_vm_builtins(vm);
    tinypy_value_t *system_type = tinypy_dict_get(builtins, system_key);
    tinypy_retain(system_type);
    tinypy_dict_set(builtins, system_key, shared);
    const char *invalid_formats[] = {"?", "(", "[)", ")", "}"};
    for (size_t index = 0U; index < sizeof(invalid_formats) / sizeof(invalid_formats[0]); ++index) {
        TEST_CHECK(tinypy_build_value(vm, &error, invalid_formats[index]) == NULL);
        TEST_CHECK(error != NULL && tinypy_vm_raised_exception_type(vm) == system_type);
        tinypy_error_release(error);
        error = NULL;
        tinypy_vm_clear_error(vm);
    }
    TEST_CHECK(tinypy_build_value(vm, NULL, "{i}", 7) == NULL);
    TEST_CHECK(tinypy_vm_raised_exception_type(vm) == system_type);
    tinypy_vm_clear_error(vm);
    converter.result = NULL;
    TEST_CHECK(tinypy_build_value(vm, NULL, "O&", __test_build_value_converter, (void *)&converter) == NULL);
    TEST_CHECK(tinypy_vm_raised_exception_type(vm) == system_type);
    tinypy_vm_clear_error(vm);
    tinypy_dict_set(builtins, system_key, system_type);
    tinypy_release(system_type);
    tinypy_release(system_key);

    TEST_CHECK(tinypy_build_value(vm, &error, "{Oi}", shared, 7) == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_TYPE);
    TEST_CHECK(tinypy_refcount(shared) == references);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    /* A pre-existing exception is preserved when an object argument is NULL. */
    (void)tinypy_exception_raise(first_error, NULL, NULL);
    TEST_CHECK(tinypy_build_value(vm, NULL, "O", (tinypy_value_t *)NULL) == NULL);
    TEST_CHECK(tinypy_vm_raised_exception(vm) == first_error);
    tinypy_vm_clear_error(vm);
    tinypy_release(first_error);
    tinypy_release(second_error);
    tinypy_release(shared);

    TEST_CHECK(tinypy_vm_has_error(vm) == 0);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_native_items_state_t {
    tinypy_vm_t *vm;
    size_t calls;
    size_t finalized;
} test_native_items_state_t;
//////////////////////////////////////////////////////////////////////////
/* Returns (self, items, kwargs, function), None standing for NULL. */
static tinypy_value_t *__test_native_items_record(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_native_items_state_t *state = (test_native_items_state_t *)tinypy_native_function_user_data(function);
    tinypy_value_t *none = tinypy_none_get(state->vm);

    (void)out_error;
    state->calls += 1U;
    tinypy_value_t *arguments = tinypy_tuple_from_items(state->vm, items, count);
    tinypy_value_t *record[] = {self != NULL ? self : none, arguments, kwargs != NULL ? kwargs : none, function};
    tinypy_value_t *result = tinypy_tuple_from_items(state->vm, record, sizeof(record) / sizeof(record[0]));
    tinypy_release(arguments);
    tinypy_release(none);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_items_fail(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_native_items_state_t *state = (test_native_items_state_t *)tinypy_native_function_user_data(function);

    (void)self;
    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    state->calls += 1U;
    tinypy_vm_raise_error(state->vm, TINYPY_ERROR_VALUE, "items failure");
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __test_native_items_finalize(void *user_data) {
    test_native_items_state_t *state = (test_native_items_state_t *)user_data;
    state->finalized += 1U;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_native_items(void) {
    static const char source[] =
        "def check(actual, expected):\n"
        "    if actual != expected:\n"
        "        raise AssertionError('%r != %r' % (actual, expected))\n"
        "def expect_error(call, message):\n"
        "    try:\n"
        "        call()\n"
        "    except TypeError as error:\n"
        "        check(str(error), message)\n"
        "    else:\n"
        "        raise AssertionError(message)\n"
        "check(function(1), (None, (1,), None, function))\n"
        "check(function(1, 2), (None, (1, 2), None, function))\n"
        "expect_error(lambda: function(), 'function() takes at least 1 argument (0 given)')\n"
        "expect_error(lambda: function(1, 2, 3), 'function() takes at most 2 arguments (3 given)')\n"
        "expect_error(lambda: function(1, key=2), 'function() takes no keyword arguments')\n"
        "check(keywords(1, key=2), (None, (1,), {'key': 2}, keywords))\n"
        "check(keywords(), (None, (), None, keywords))\n"
        "method = Holder.__dict__['method']\n"
        "def method_calls(holder):\n"
        "    check(holder.method(), (holder, (), None, method))\n"
        "    check(holder.method(5), (holder, (5,), None, method))\n"
        "    check(holder.keywords(1, key=2), (holder, (1,), {'key': 2}, Holder.__dict__['keywords']))\n"
        "    expect_error(lambda: holder.method(1, 2), 'method() takes at most 1 argument (2 given)')\n"
        "    expect_error(lambda: holder.method(key=1), 'method() takes no keyword arguments')\n"
        "class Derived(Holder):\n"
        "    pass\n"
        "holder = Holder()\n"
        "method_calls(holder)\n"
        "method_calls(Derived())\n"
        "bound = holder.method\n"
        "check(bound(5), (holder, (5,), None, bound))\n"
        "check(holder.method(), (holder, (), None, bound))\n"
        "check(Holder.method(holder, 5), (holder, (5,), None, method))\n"
        "expect_error(lambda: Holder.method(), \"descriptor 'method' of 'Holder' object needs an argument\")\n"
        "expect_error(lambda: Holder.method(1), \"descriptor 'method' requires a 'Holder' object but received a 'int'\")\n"
        "class Shadow(object):\n"
        "    value = property(function)\n"
        "shadow = Shadow()\n"
        "check(shadow.value, (None, (shadow,), None, function))\n"
        "try:\n"
        "    holder.fail()\n"
        "except ValueError as error:\n"
        "    check(str(error), 'items failure')\n"
        "else:\n"
        "    raise AssertionError('fail')\n"
        "def call_method(target):\n"
        "    return target.method()\n"
        "check(call_method(holder), (holder, (), None, method))\n"
        "holder.method = lambda: 'shadow'\n"
        "check(call_method(holder), 'shadow')\n"
        "del holder.method\n"
        "check(call_method(holder), (holder, (), None, method))\n"
        "Holder.method = lambda self: 'replaced'\n"
        "check(call_method(holder), 'replaced')\n";
    test_allocator_state_t allocator_state;
    test_native_items_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_native_type_spec_t spec;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&allocator_state);
    config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    state.vm = vm;

    tinypy_value_t *released = tinypy_native_function_new_items(vm, "released", 8U, __test_native_items_record, 0U, 0U, &state, __test_native_items_finalize);
    TEST_CHECK(tinypy_native_function_user_data(released) == &state);
    tinypy_release(released);
    TEST_CHECK(state.finalized == 1U);

    /* An unshared argument tuple is reused; a retained one keeps its items. */
    tinypy_value_t *items[] = {tinypy_integer_from_i64(vm, 1), tinypy_integer_from_i64(vm, 2)};
    tinypy_ref_t references = tinypy_refcount(items[1]);
    tinypy_value_t *empty = tinypy_native_arguments_acquire(vm, NULL, NULL, 0U, &error);
    TEST_CHECK(empty != NULL && error == NULL && tinypy_tuple_size(empty) == 0U);
    tinypy_native_arguments_release(empty);
    tinypy_value_t *arguments = tinypy_native_arguments_acquire(vm, items[0], &items[1], 1U, &error);
    TEST_CHECK(arguments != NULL && error == NULL && tinypy_tuple_size(arguments) == 2U);
    TEST_CHECK(tinypy_tuple_get(arguments, 0U) == items[0] && tinypy_tuple_get(arguments, 1U) == items[1]);
    TEST_CHECK(tinypy_refcount(items[1]) == references + 1);
    tinypy_native_arguments_release(arguments);
    TEST_CHECK(tinypy_refcount(items[1]) == references);
    tinypy_value_t *reused = tinypy_native_arguments_acquire(vm, NULL, items, 2U, &error);
    TEST_CHECK(reused == arguments && tinypy_tuple_get(reused, 0U) == items[0] && tinypy_tuple_get(reused, 1U) == items[1]);
    tinypy_retain(reused);
    tinypy_native_arguments_release(reused);
    TEST_CHECK(tinypy_tuple_size(reused) == 2U && tinypy_tuple_get(reused, 1U) == items[1]);
    tinypy_release(reused);
    TEST_CHECK(tinypy_refcount(items[1]) == references);
    tinypy_release(items[1]);
    tinypy_release(items[0]);

    tinypy_native_type_spec_init(&spec);
    tinypy_type_t *holder = tinypy_native_type_new(vm, "Holder", 6U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(holder != NULL && error == NULL);
    tinypy_value_t *method = tinypy_native_function_new_items(vm, "method", 6U, __test_native_items_record, 0U, 1U, &state, __test_native_items_finalize);
    tinypy_value_t *method_keywords = tinypy_native_function_new_items_keywords(vm, "keywords", 8U, __test_native_items_record, &state, __test_native_items_finalize);
    tinypy_value_t *fail = tinypy_native_function_new_items(vm, "fail", 4U, __test_native_items_fail, 0U, 0U, &state, __test_native_items_finalize);
    tinypy_type_set_attr(holder, "method", 6U, method);
    tinypy_type_set_attr(holder, "keywords", 8U, method_keywords);
    tinypy_type_set_attr(holder, "fail", 4U, fail);
    tinypy_release(fail);
    tinypy_release(method_keywords);
    tinypy_release(method);

    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *function = tinypy_native_function_new_items(vm, "function", 8U, __test_native_items_record, 1U, 2U, &state, __test_native_items_finalize);
    tinypy_value_t *keywords = tinypy_native_function_new_items_keywords(vm, "keywords", 8U, __test_native_items_record, &state, __test_native_items_finalize);
    tinypy_value_t *function_key = tinypy_string_from_bytes(vm, "function", 8U);
    tinypy_value_t *keywords_key = tinypy_string_from_bytes(vm, "keywords", 8U);
    tinypy_value_t *holder_key = tinypy_string_from_bytes(vm, "Holder", 6U);
    tinypy_value_t *holder_value = tinypy_type_as_value(holder);
    tinypy_dict_set(globals, function_key, function);
    tinypy_dict_set(globals, keywords_key, keywords);
    tinypy_dict_set(globals, holder_key, holder_value);
    tinypy_release(holder_key);
    tinypy_release(keywords_key);
    tinypy_release(function_key);
    tinypy_release(keywords);
    tinypy_release(function);

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "native_items.py", 15U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    TEST_CHECK(state.calls == 17U);
    TEST_CHECK(state.finalized == 1U);

    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_release(holder_value);
    TEST_CHECK(tinypy_vm_has_error(vm) == 0);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.finalized == 6U);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_native_contract_state_t {
    tinypy_vm_t *vm;
    size_t calls;
    size_t constructions;
    size_t argument_count;
} test_native_contract_state_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_contract_repr(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    test_native_contract_state_t *state = (test_native_contract_state_t *)user_data;

    (void)instance;
    (void)payload;
    (void)out_error;
    state->calls += 1U;
    tinypy_value_t *result = tinypy_string_from_bytes(state->vm, "<contract>", 10U);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_contract_call(tinypy_value_t *instance, void *payload, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    test_native_contract_state_t *state = (test_native_contract_state_t *)user_data;

    (void)instance;
    (void)payload;
    (void)args;
    (void)kwargs;
    (void)out_error;
    state->calls += 1U;
    tinypy_value_t *result = tinypy_integer_from_i64(state->vm, 7);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* -1 is a hash result the VM must not report: it reads as -2. */
static tinypy_hash_t __test_native_contract_hash(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    test_native_contract_state_t *state = (test_native_contract_state_t *)user_data;

    (void)instance;
    (void)payload;
    (void)out_error;
    state->calls += 1U;
    return (tinypy_hash_t)-1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_contract_get_attribute(tinypy_value_t *instance, void *payload, tinypy_value_t *name, void *user_data, tinypy_error_t **out_error) {
    test_native_contract_state_t *state = (test_native_contract_state_t *)user_data;
    size_t size;
    const void *bytes = tinypy_string_view(name, &size);

    (void)instance;
    (void)payload;
    (void)out_error;
    state->calls += 1U;
    if (size != 6U || memcmp(bytes, "answer", 6U) != 0) {
        tinypy_vm_raise_error(state->vm, TINYPY_ERROR_ATTRIBUTE, "no such contract attribute");
        return NULL;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(state->vm, 42);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_contract_sequence_get(tinypy_value_t *instance, void *payload, tinypy_value_t *key, void *user_data, tinypy_error_t **out_error) {
    test_native_contract_state_t *state = (test_native_contract_state_t *)user_data;
    int64_t index = tinypy_integer_as_i64(key);

    (void)instance;
    (void)payload;
    (void)out_error;
    if (index < 0 || index >= 3) {
        tinypy_vm_raise_error(state->vm, TINYPY_ERROR_INDEX, "contract index out of range");
        return NULL;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(state->vm, index * 10);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __test_native_contract_sequence_length(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)instance;
    (void)payload;
    (void)user_data;
    (void)out_error;
    return 3;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __test_native_contract_construct(tinypy_value_t *instance, void *payload, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    test_native_contract_state_t *state = (test_native_contract_state_t *)user_data;

    (void)instance;
    (void)payload;
    (void)kwargs;
    (void)out_error;
    state->constructions += 1U;
    state->argument_count = tinypy_tuple_size(args);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_contract_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)self;
    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    tinypy_vm_t *vm = tinypy_value_vm(function);
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_native_contract_run(tinypy_vm_t *vm, tinypy_value_t *globals, const char *source) {
    static const char prelude[] =
        "def check(actual, expected):\n"
        "    if actual != expected:\n"
        "        raise AssertionError('%r != %r' % (actual, expected))\n"
        "def expect_error(kind, call, message):\n"
        "    try:\n"
        "        call()\n"
        "    except kind as error:\n"
        "        check(str(error), message)\n"
        "    else:\n"
        "        raise AssertionError(message)\n";
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *checks = tinypy_exec_source(vm, prelude, sizeof(prelude) - 1U, "contract.py", 11U, globals, NULL, &options, &error);
    TEST_CHECK(checks != NULL && error == NULL);
    tinypy_release(checks);
    tinypy_value_t *result = tinypy_exec_source(vm, source, strlen(source), "contract.py", 11U, globals, NULL, &options, &error);
    if (result == NULL) {
        size_t size = 0U;
        const char *message = error != NULL ? tinypy_error_message(error, &size) : "";

        (void)fprintf(stderr, "contract source failed: %.*s\n", (int)size, message);
        return 1;
    }
    tinypy_release(result);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static void __test_native_contract_add_type(tinypy_value_t *globals, const char *name, tinypy_type_t *type) {
    tinypy_vm_t *vm = tinypy_value_vm(globals);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, strlen(name));
    tinypy_value_t *value = tinypy_type_as_value(type);

    tinypy_dict_set(globals, key, value);
    tinypy_release(key);
}
//////////////////////////////////////////////////////////////////////////
/* The contracts of host native types: each type dispatches only the callbacks
   of its own spec, a spec update resets what it removes, the bases and the
   __class__ assignments keep payloads with their spec, a construct callback
   takes the call arguments, sequence callbacks iterate, -1 is no hash, methods
   do not keep their type alive, payloads take the alignment of max_align_t,
   a spec of an older struct_size is read within it and the instance
   attribute functions hold the instance dictionary. */
static int32_t __test_native_type_contracts(void) {
    static const char dispatch_source[] =
        "base = Base()\n"
        "derived = Derived()\n"
        "check(repr(base), '<contract>')\n"
        "check(repr(derived)[:18], '<Derived object at')\n"
        "check(base(), 7)\n"
        "expect_error(TypeError, derived, \"'Derived' object is not callable\")\n"
        "check(base.answer, 42)\n"
        "expect_error(AttributeError, lambda: derived.answer, \"'Derived' object has no attribute 'answer'\")\n"
        "check((list(base), 10 in base, 5 in base, sorted(base, reverse=True)), ([0, 10, 20], True, False, [20, 10, 0]))\n"
        "expect_error(TypeError, lambda: iter(derived), \"'Derived' object is not iterable\")\n"
        "check((hash(base), {base: 1}[base], base in set([base])), (-2, 1, True))\n"
        "check(repr(Own()), '<contract>')\n"
        "class Early(Base):\n"
        "    pass\n"
        "early = Early()\n";
    static const char update_source[] =
        "check(repr(base)[:15], '<Base object at')\n"
        "check(repr(early), '<contract>')\n"
        "expect_error(TypeError, base, \"'Base' object is not callable\")\n"
        "check(base.answer, 42)\n";
    static const char construct_source[] =
        "class First(Plain):\n"
        "    pass\n"
        "class OtherFirst(Plain):\n"
        "    pass\n"
        "class Second(Twin):\n"
        "    pass\n"
        "item = First()\n"
        "expect_error(TypeError, lambda: setattr(item, '__class__', Second), \"__class__ assignment: 'First' object layout differs from 'Second'\")\n"
        "item.__class__ = OtherFirst\n"
        "check(type(item), OtherFirst)\n"
        "check(type(Plain(7, key=1)), Plain)\n"
        "class Initialized(Plain):\n"
        "    def __init__(self, value):\n"
        "        Plain.__init__(self, value)\n"
        "        self.value = value\n"
        "check(Initialized(5).value, 5)\n";
    static const char method_source[] =
        "import _weakref\n"
        "reference = _weakref.ref(Methods)\n"
        "kept = Methods.__dict__['method']\n"
        "check(Methods().method(), None)\n"
        "del Methods\n"
        "check(reference(), None)\n"
        "check(repr(kept), \"<method 'method' of '<deleted type>' objects>\")\n"
        "check(kept.__objclass__, None)\n"
        "expect_error(TypeError, lambda: kept(object()), \"descriptor 'method' requires a '<deleted type>' object but received a 'object'\")\n";
    static const char replace_source[] =
        "class Collider(object):\n"
        "    armed = False\n"
        "    def __hash__(self):\n"
        "        return hash('attr')\n"
        "    def __eq__(self, other):\n"
        "        if Collider.armed:\n"
        "            Collider.armed = False\n"
        "            holder.__dict__ = {}\n"
        "        return False\n"
        "class Holder(object):\n"
        "    attr = 'class'\n"
        "holder = Holder()\n"
        "def arm():\n"
        "    holder.__dict__[Collider()] = 'collider'\n"
        "    Collider.armed = True\n"
        "arm()\n"
        "class Renewing(object):\n"
        "    def __hash__(self):\n"
        "        return hash('attr')\n"
        "    def __eq__(self, other):\n"
        "        renewed.__dict__ = {Renewing(): 'renewing'}\n"
        "        return False\n"
        "renewed = Holder()\n"
        "renewed.__dict__ = {Renewing(): 'renewing'}\n";
    test_allocator_state_t allocator_state;
    test_native_contract_state_t base_state;
    test_native_contract_state_t derived_state;
    tinypy_native_type_spec_t spec;
    tinypy_error_t *error = NULL;
    size_t size;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&base_state, 0, sizeof(base_state));
    (void)memset(&derived_state, 0, sizeof(derived_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    base_state.vm = vm;
    derived_state.vm = vm;
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *name_key = tinypy_string_from_bytes(vm, "__name__", 8U);
    tinypy_value_t *module_name = tinypy_string_from_bytes(vm, "contract", 8U);
    tinypy_dict_set(globals, name_key, module_name);
    tinypy_release(module_name);
    tinypy_release(name_key);

    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(void *);
    spec.user_data = &base_state;
    spec.repr = __test_native_contract_repr;
    spec.call = __test_native_contract_call;
    spec.hash = __test_native_contract_hash;
    spec.get_attribute = __test_native_contract_get_attribute;
    spec.sequence_get = __test_native_contract_sequence_get;
    spec.sequence_length = __test_native_contract_sequence_length;
    tinypy_type_t *base = tinypy_native_type_new(vm, "Base", 4U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(base != NULL && error == NULL);
    tinypy_native_type_spec_t derived_spec;
    tinypy_native_type_spec_init(&derived_spec);
    derived_spec.payload_size = sizeof(void *);
    derived_spec.user_data = &derived_state;
    const tinypy_type_t *derived_bases[1] = {base};
    tinypy_type_t *derived = tinypy_native_type_new(vm, "Derived", 7U, derived_bases, 1U, NULL, &derived_spec, &error);
    TEST_CHECK(derived != NULL && error == NULL);
    derived_spec.repr = __test_native_contract_repr;
    tinypy_type_t *own = tinypy_native_type_new(vm, "Own", 3U, derived_bases, 1U, NULL, &derived_spec, &error);
    TEST_CHECK(own != NULL && error == NULL);
    __test_native_contract_add_type(globals, "Base", base);
    __test_native_contract_add_type(globals, "Derived", derived);
    __test_native_contract_add_type(globals, "Own", own);
    TEST_CHECK(__test_native_contract_run(vm, globals, dispatch_source) == 0);
    TEST_CHECK(derived_state.calls == 1U);

    /* An update resets what the spec no longer provides; the Python
       subclasses created before keep the callbacks they copied. */
    spec.repr = NULL;
    spec.call = NULL;
    TEST_CHECK(tinypy_native_type_update_spec(base, &spec, &error) != 0 && error == NULL);
    TEST_CHECK(__test_native_contract_run(vm, globals, update_source) == 0);

    /* Bases must keep object's layout or a host native one. */
    tinypy_value_t *list = tinypy_list_from_items(vm, NULL, 0U);
    const tinypy_type_t *list_type = tinypy_object_type(list);
    const tinypy_type_t *list_bases[1] = {list_type};
    tinypy_release(list);
    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(void *);
    TEST_CHECK(tinypy_native_type_new(vm, "Listed", 6U, list_bases, 1U, NULL, &spec, &error) == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_TYPE);
    static const char layout_message[] = "native type base 'list' has an incompatible instance layout";
    const char *message = tinypy_error_message(error, &size);
    TEST_CHECK(size == sizeof(layout_message) - 1U && memcmp(message, layout_message, size) == 0);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    tinypy_type_t *python_base = tinypy_type_new(vm, "PythonBase", 10U, NULL, 0U, NULL, NULL, &error);
    const tinypy_type_t *python_bases[1] = {python_base};
    tinypy_type_t *mixed = tinypy_native_type_new(vm, "Mixed", 5U, python_bases, 1U, NULL, &spec, &error);
    TEST_CHECK(mixed != NULL && error == NULL);

    /* A construct callback takes the call arguments as a tp_new of its own,
       and the payload stays with the spec that built it. */
    spec.user_data = &base_state;
    spec.construct = __test_native_contract_construct;
    tinypy_type_t *plain = tinypy_native_type_new(vm, "Plain", 5U, NULL, 0U, NULL, &spec, &error);
    spec.user_data = &derived_state;
    tinypy_type_t *twin = tinypy_native_type_new(vm, "Twin", 4U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(plain != NULL && twin != NULL && error == NULL);
    __test_native_contract_add_type(globals, "Plain", plain);
    __test_native_contract_add_type(globals, "Twin", twin);
    base_state.constructions = 0U;
    TEST_CHECK(__test_native_contract_run(vm, globals, construct_source) == 0);
    TEST_CHECK(base_state.constructions == 3U && base_state.argument_count == 1U);

    /* A heap type references its C methods weakly, so it is freed with its
       last reference while a method outlives it. */
    tinypy_native_type_spec_init(&spec);
    tinypy_type_t *methods = tinypy_native_type_new(vm, "Methods", 7U, NULL, 0U, NULL, &spec, &error);
    tinypy_value_t *method = tinypy_native_function_new_items(vm, "method", 6U, __test_native_contract_method, 0U, 0U, NULL, NULL);
    tinypy_type_set_attr(methods, "method", 6U, method);
    tinypy_release(method);
    __test_native_contract_add_type(globals, "Methods", methods);
    tinypy_value_t *methods_value = tinypy_type_as_value(methods);
    tinypy_release(methods_value);
    TEST_CHECK(__test_native_contract_run(vm, globals, method_source) == 0);

    /* Payloads take any alignment up to that of max_align_t. */
    tinypy_native_type_spec_init(&spec);
    spec.payload_size = 2U * TINYPY_INTERNAL_ALIGNMENT;
    spec.payload_alignment = TINYPY_INTERNAL_ALIGNMENT;
#if !defined(_MSC_VER)
    TEST_CHECK(_Alignof(max_align_t) <= TINYPY_INTERNAL_ALIGNMENT);
    spec.payload_alignment = _Alignof(max_align_t);
#endif
    tinypy_type_t *aligned = tinypy_native_type_new(vm, "Aligned", 7U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(aligned != NULL && error == NULL);
    tinypy_value_t *aligned_instance = tinypy_native_instance_new(aligned);
    TEST_CHECK((uintptr_t)tinypy_native_instance_payload(aligned_instance) % spec.payload_alignment == 0U);
    tinypy_release(aligned_instance);
    spec.payload_alignment = 2U * TINYPY_INTERNAL_ALIGNMENT;
    TEST_CHECK(tinypy_native_type_new(vm, "Overaligned", 11U, NULL, 0U, NULL, &spec, &error) == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_TYPE);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);

    /* A spec of an older struct_size is read only within its size, and the
       layout switches it predates are on, as tinypy_native_type_spec_init
       sets them. */
    size_t older_size = offsetof(tinypy_native_type_spec_t, has_instance_dict);
    tinypy_native_type_spec_init(&spec);
    spec.struct_size = (uint32_t)older_size;
    tinypy_native_type_spec_t *older_spec = (tinypy_native_type_spec_t *)malloc(older_size);
    TEST_CHECK(older_spec != NULL);
    (void)memcpy(older_spec, &spec, older_size);
    tinypy_type_t *older = tinypy_native_type_new(vm, "Older", 5U, NULL, 0U, NULL, older_spec, &error);
    TEST_CHECK(older != NULL && error == NULL);
    TEST_CHECK(tinypy_native_type_update_spec(older, older_spec, &error) != 0 && error == NULL);
    free(older_spec);
    __test_native_contract_add_type(globals, "Older", older);
    TEST_CHECK(__test_native_contract_run(vm, globals, "import _weakref\nolder = Older()\nolder.value = 1\ncheck((older.__dict__, _weakref.ref(older)() is older), ({'value': 1}, True))\n") == 0);

    /* A key comparison of the instance attribute functions replaces the
       instance dictionary: get finishes its probe of the old one, even when
       every comparison replaces it again, and set stores in the old one. */
    TEST_CHECK(__test_native_contract_run(vm, globals, replace_source) == 0);
    tinypy_value_t *holder_key = tinypy_string_from_bytes(vm, "holder", 6U);
    tinypy_value_t *holder = tinypy_dict_get_optional(globals, holder_key);
    tinypy_release(holder_key);
    TEST_CHECK(holder != NULL);
    tinypy_value_t *class_attribute = tinypy_type_get_attr(tinypy_object_type(holder), "attr", 4U);
    TEST_CHECK(class_attribute != NULL);
    TEST_CHECK(tinypy_instance_get_attr(holder, "attr", 4U) == class_attribute);
    tinypy_value_t *renewed_key = tinypy_string_from_bytes(vm, "renewed", 7U);
    tinypy_value_t *renewed = tinypy_dict_get_optional(globals, renewed_key);
    tinypy_release(renewed_key);
    TEST_CHECK(renewed != NULL);
    TEST_CHECK(tinypy_instance_get_attr(renewed, "attr", 4U) == class_attribute);
    TEST_CHECK(__test_native_contract_run(vm, globals, "arm()\n") == 0);
    tinypy_value_t *stored = tinypy_integer_from_i64(vm, 1);
    tinypy_instance_set_attr(holder, "attr", 4U, stored);
    tinypy_release(stored);
    TEST_CHECK(__test_native_contract_run(vm, globals, "check((holder.__dict__, holder.attr), ({}, 'class'))\n") == 0);

    tinypy_type_t *const releases[] = {base, derived, own, python_base, mixed, plain, twin, aligned, older};
    for (size_t index = 0U; index < sizeof(releases) / sizeof(releases[0]); ++index) {
        tinypy_value_t *type_value = tinypy_type_as_value(releases[index]);

        tinypy_release(type_value);
    }
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    TEST_CHECK(tinypy_vm_has_error(vm) == 0);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_global(tinypy_value_t *globals, const char *name) {
    tinypy_vm_t *vm = tinypy_value_vm(globals);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, strlen(name));
    tinypy_value_t *value = tinypy_dict_get_optional(globals, key);

    tinypy_release(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
/* Releases text, a result of tinypy_object_str or tinypy_object_repr, and
   tells whether it is the str expected. */
static tinypy_bool_t __test_text_is(tinypy_value_t *text, const char *expected) {
    if (text == NULL) {
        return TINYPY_FALSE;
    }
    size_t size = 0U;
    const char *bytes = (const char *)tinypy_string_view(text, &size);
    size_t expected_size = strlen(expected);
    tinypy_bool_t equal = tinypy_typeof(text) == TINYPY_VALUE_STRING && size == expected_size && memcmp(bytes, expected, size) == 0 ? TINYPY_TRUE : TINYPY_FALSE;

    tinypy_release(text);
    return equal;
}
//////////////////////////////////////////////////////////////////////////
/* Whether RuntimeError with message is raised, which it then clears, and no
   native recursion level is left behind. */
static tinypy_bool_t __test_raised_recursion_error(tinypy_vm_t *vm, const char *message) {
    tinypy_value_t *runtime_error = &vm->exception_types[TINYPY_EXCEPTION_RUNTIME_ERROR]->base.base;
    tinypy_value_t *raised = tinypy_vm_raised_exception(vm);

    if (raised == NULL || tinypy_vm_raised_exception_type(vm) != runtime_error || vm->native_recursion_depth != 0U) {
        return TINYPY_FALSE;
    }
    tinypy_retain(raised);
    tinypy_vm_clear_error(vm);
    tinypy_value_t *text = tinypy_object_str(raised, NULL);
    tinypy_bool_t matched = __test_text_is(text, message);

    tinypy_release(raised);
    return matched;
}
//////////////////////////////////////////////////////////////////////////
/* A host that hashes nested values outside any call meets the native stack
   guard as Python code does: bound methods and tuples nested at any depth
   hash on the explicit stack of tuplehash, and the hash slot of built-in
   methods and the __hash__ of struct sequences, which recurse natively,
   raise RuntimeError past the native stack budget rather than exhausting
   the stack. */
static int32_t __test_host_hash_native_guard(void) {
    static const char source[] =
        "import sys\n"
        "class Holder(object):\n"
        "    def method(self):\n"
        "        pass\n"
        "method_type = type(Holder().method)\n"
        "sequence_type = type(sys.float_info)\n"
        "bound = Holder()\n"
        "for index in xrange(50000):\n"
        "    bound = method_type(Holder.method.im_func, (bound, index), Holder)\n"
        "builtin = len\n"
        "for index in xrange(100000):\n"
        "    builtin = builtin.__reduce_ex__\n"
        "sequence = 0\n"
        "for index in xrange(20000):\n"
        "    sequence = sequence_type((sequence,) + tuple(range(10)))\n"
        "bound_hash = hash(bound)\n";
    static const char unwind[] =
        "while isinstance(bound, method_type):\n"
        "    bound = bound.im_self[0]\n"
        "while builtin is not len:\n"
        "    builtin = builtin.__self__\n"
        "while isinstance(sequence, sequence_type):\n"
        "    sequence = sequence[0]\n";
    test_allocator_state_t state;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "nested.py", 9U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);

    tinypy_value_t *bound = __test_global(globals, "bound");
    tinypy_value_t *bound_hash = __test_global(globals, "bound_hash");
    tinypy_hash_t hash = tinypy_hash(bound);
    TEST_CHECK(tinypy_vm_has_error(vm) == 0 && hash == (tinypy_hash_t)tinypy_integer_as_i64(bound_hash));
    tinypy_value_t *builtin = __test_global(globals, "builtin");
    (void)tinypy_hash(builtin);
    TEST_CHECK(__test_raised_recursion_error(vm, "maximum recursion depth exceeded") != 0);
    tinypy_value_t *sequence = __test_global(globals, "sequence");
    tinypy_value_t *dict = tinypy_dict_new(vm);
    tinypy_dict_set(dict, sequence, sequence);
    TEST_CHECK(tinypy_dict_size(dict) == 0U);
    TEST_CHECK(__test_raised_recursion_error(vm, "maximum recursion depth exceeded while calling a Python object") != 0);
    tinypy_dict_set(dict, bound, bound);
    TEST_CHECK(tinypy_vm_has_error(vm) == 0 && tinypy_dict_size(dict) == 1U);
    tinypy_release(dict);

    result = tinypy_exec_source(vm, unwind, sizeof(unwind) - 1U, "unwind.py", 9U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_class_is_subclass, as PyClass_IsSubclass, takes no recursion
   level: it follows the last base in a loop, so a single inheritance chain
   of any depth is walked, and recurses into the other bases under the native
   stack guard alone, past the recursion limit, until the native stack budget
   reports false and leaves RuntimeError raised. The chains are linked from
   the top down, so that no assignment walks one. */
static int32_t __test_class_subclass_native_guard(void) {
    static const char source[] =
        "import sys\n"
        "sys.setrecursionlimit(100)\n"
        "class Root:\n"
        "    pass\n"
        "class Other:\n"
        "    pass\n"
        "def chain(count, extra):\n"
        "    classes = [type(Root)('Level%d' % index, (), {}) for index in range(count)]\n"
        "    for index in range(count - 1, 0, -1):\n"
        "        classes[index].__bases__ = (classes[index - 1],) + extra\n"
        "    classes[0].__bases__ = (Root,) + extra\n"
        "    return classes\n"
        "single = chain(50000, ())\n"
        "moderate = chain(300, (Other,))\n"
        "deep = chain(50000, (Other,))\n";
    static const char unwind[] =
        "for classes in (single, moderate, deep):\n"
        "    for level in classes:\n"
        "        level.__bases__ = ()\n";
    test_allocator_state_t state;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "chains.py", 9U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);

    tinypy_value_t *root = __test_global(globals, "Root");
    tinypy_value_t *single = __test_global(globals, "single");
    tinypy_value_t *moderate = __test_global(globals, "moderate");
    tinypy_value_t *deep = __test_global(globals, "deep");
    tinypy_value_t *single_top = tinypy_list_get(single, tinypy_list_size(single) - 1U);
    tinypy_value_t *moderate_top = tinypy_list_get(moderate, tinypy_list_size(moderate) - 1U);
    tinypy_value_t *deep_top = tinypy_list_get(deep, tinypy_list_size(deep) - 1U);
    TEST_CHECK(tinypy_class_is_subclass(single_top, root) != 0 && tinypy_vm_has_error(vm) == 0);
    TEST_CHECK(tinypy_class_is_subclass(root, single_top) == 0 && tinypy_vm_has_error(vm) == 0);
    TEST_CHECK(tinypy_class_is_subclass(moderate_top, root) != 0 && tinypy_vm_has_error(vm) == 0);
    TEST_CHECK(tinypy_class_is_subclass(deep_top, root) == 0);
    TEST_CHECK(__test_raised_recursion_error(vm, "maximum recursion depth exceeded in subclass check") != 0);
    /* As PyErr_GivenExceptionMatches, matching reports the failure as
       unraisable and answers no match. */
    TEST_CHECK(tinypy_exception_matches(deep_top, root, &error) == 0 && error == NULL && tinypy_vm_has_error(vm) == 0 && vm->native_recursion_depth == 0U);

    result = tinypy_exec_source(vm, unwind, sizeof(unwind) - 1U, "unwind.py", 9U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_vm_raise_error raises a complete instance of the class of every
   kind, UnicodeError for the kinds of the codec errors, whose fields
   tinypy_exception_new gives. The str and the repr of a nested exception
   take the levels of PyObject_Str and PyObject_Repr. Nested candidate tuples
   match past the recursion limit, under the native stack budget alone: a
   nesting that exhausts it is reported as unraisable and does not match,
   and the exception pending meanwhile stays. */
static int32_t __test_exception_contracts(void) {
    static const char define[] =
        "import sys\n"
        "def nest(cls, depth):\n"
        "    exception = cls('leaf')\n"
        "    for index in range(depth):\n"
        "        exception = cls(exception)\n"
        "    return exception\n"
        "def candidates(depth):\n"
        "    candidate = (ValueError,)\n"
        "    for index in range(depth):\n"
        "        candidate = (KeyError, candidate)\n"
        "    return candidate\n"
        "str_ok = nest(ValueError, 39)\n"
        "str_deep = nest(ValueError, 40)\n"
        "repr_ok = nest(ValueError, 18)\n"
        "repr_deep = nest(ValueError, 19)\n"
        "within = candidates(30)\n"
        "beyond = candidates(60)\n"
        "exhausting = candidates(100000)\n"
        "instance = ValueError('raised')\n"
        "decode_args = ('utf-8', '\\xff', 0, 1, 'invalid start byte')\n"
        "sys.setrecursionlimit(40)\n";
    static const char restore[] = "sys.setrecursionlimit(1000)\n";
    test_allocator_state_t state;
    tinypy_error_t *error = NULL;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    error = __test_exception_diagnostic(vm, globals, define, sizeof(define) - 1U);
    TEST_CHECK(error == NULL);

    for (int32_t kind = (int32_t)TINYPY_ERROR_TYPE; kind < (int32_t)TINYPY_ERROR_MEMORY; ++kind) {
        tinypy_vm_raise_error(vm, (tinypy_error_kind_e)kind, "host message");
        tinypy_value_t *raised = tinypy_vm_raised_exception(vm);
        tinypy_value_t *raised_type = tinypy_vm_raised_exception_type(vm);
        TEST_CHECK(raised != NULL && raised_type != NULL);
        TEST_CHECK(tinypy_exception_matches(raised, raised_type, &error) == 1 && error == NULL);
        tinypy_retain(raised);
        tinypy_vm_clear_error(vm);
        tinypy_value_t *text = tinypy_object_str(raised, &error);
        TEST_CHECK(__test_text_is(text, kind == (int32_t)TINYPY_ERROR_KEY ? "'host message'" : "host message") != 0 && error == NULL);
        tinypy_release(raised);
    }
    tinypy_value_t *builtins = tinypy_vm_builtins(vm);
    tinypy_value_t *unicode_error = __test_global(builtins, "UnicodeError");
    TEST_CHECK(unicode_error != NULL);
    for (int32_t kind = (int32_t)TINYPY_ERROR_UNICODE_DECODE; kind <= (int32_t)TINYPY_ERROR_UNICODE_ENCODE; ++kind) {
        tinypy_vm_raise_error(vm, (tinypy_error_kind_e)kind, "host message");
        TEST_CHECK(tinypy_vm_raised_exception_type(vm) == unicode_error);
        tinypy_value_t *raised = tinypy_vm_raised_exception(vm);
        tinypy_value_t *text = tinypy_object_repr(raised, &error);
        TEST_CHECK(__test_text_is(text, "UnicodeError('host message',)") != 0 && error == NULL);
        tinypy_value_t *message = tinypy_object_get_attr(raised, "message", 7U, &error);
        TEST_CHECK(__test_text_is(message, "host message") != 0 && error == NULL);
        TEST_CHECK(tinypy_vm_normalize_error(vm, &error) != 0 && error == NULL && tinypy_vm_raised_exception(vm) == raised);
        tinypy_vm_clear_error(vm);
    }
    tinypy_value_t *decode_error = __test_global(builtins, "UnicodeDecodeError");
    tinypy_value_t *decode_args = __test_global(globals, "decode_args");
    TEST_CHECK(decode_error != NULL && decode_args != NULL);
    tinypy_value_t *decoded = tinypy_exception_new(tinypy_value_as_type(decode_error), decode_args, &error);
    TEST_CHECK(decoded != NULL && error == NULL);
    tinypy_value_t *decoded_text = tinypy_object_str(decoded, &error);
    TEST_CHECK(__test_text_is(decoded_text, "'utf-8' codec can't decode byte 0xff in position 0: invalid start byte") != 0 && error == NULL);
    tinypy_release(decoded);

    /* Each nesting takes one level of str and two of repr, its own and that
       of its arguments, so with the limit at 40 the str of 40 nestings and
       the repr of 19 exceed it, as in CPython. */
    static const char *const representations[][3] = {
        {"str_ok", "str_deep", "maximum recursion depth exceeded while getting the str of an object"},
        {"repr_ok", "repr_deep", "maximum recursion depth exceeded while getting the repr of an object"}};
    tinypy_value_t *(*const conversions[2])(tinypy_value_t *, tinypy_error_t **) = {&tinypy_object_str, &tinypy_object_repr};
    for (size_t index = 0U; index < 2U; ++index) {
        tinypy_value_t *fitting = __test_global(globals, representations[index][0]);
        tinypy_value_t *exceeding = __test_global(globals, representations[index][1]);
        tinypy_value_t *fitting_text = conversions[index](fitting, &error);
        TEST_CHECK(fitting_text != NULL && error == NULL);
        tinypy_release(fitting_text);
        tinypy_value_t *exceeding_text = conversions[index](exceeding, &error);
        TEST_CHECK(exceeding_text == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_RUNTIME);
        size_t size = 0U;
        const char *message = tinypy_error_message(error, &size);
        TEST_CHECK(size == strlen(representations[index][2]) && memcmp(message, representations[index][2], size) == 0);
        tinypy_error_release(error);
        error = NULL;
        tinypy_vm_clear_error(vm);
    }

    tinypy_value_t *instance = __test_global(globals, "instance");
    tinypy_value_t *within = __test_global(globals, "within");
    tinypy_value_t *beyond = __test_global(globals, "beyond");
    tinypy_value_t *exhausting = __test_global(globals, "exhausting");
    TEST_CHECK(tinypy_exception_matches(instance, within, &error) == 1 && error == NULL);
    TEST_CHECK(tinypy_exception_matches(instance, beyond, &error) == 1 && error == NULL);
    tinypy_vm_raise_error(vm, TINYPY_ERROR_KEY, "pending");
    tinypy_value_t *pending = tinypy_vm_raised_exception(vm);
    TEST_CHECK(tinypy_exception_matches(instance, exhausting, &error) == 0 && error == NULL);
    TEST_CHECK(tinypy_vm_raised_exception(vm) == pending && vm->native_recursion_depth == 0U);
    tinypy_vm_clear_error(vm);
    error = __test_exception_diagnostic(vm, globals, restore, sizeof(restore) - 1U);
    TEST_CHECK(error == NULL);

    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_equal compares container items and entries with == while holding
   them: a key comparison that empties the outer list and grows the dictionary
   being walked leaves the comparison well defined. Instances compare by
   identity at the top and with == as items, and a raising comparison reports
   false with the exception left raised. */
static int32_t __test_equal_reentrancy(void) {
    static const char source[] =
        "armed = [False]\n"
        "class Key(object):\n"
        "    def __init__(self, number):\n"
        "        self.number = number\n"
        "    def __hash__(self):\n"
        "        return 7\n"
        "    def __eq__(self, other):\n"
        "        if armed[0]:\n"
        "            armed[0] = False\n"
        "            del outer[:]\n"
        "            for index in range(64):\n"
        "                left[index] = index\n"
        "        return isinstance(other, Key) and self.number == other.number\n"
        "class Raising(object):\n"
        "    def __eq__(self, other):\n"
        "        raise KeyError('compare')\n"
        "def build(arm):\n"
        "    global left, right, outer, other\n"
        "    left = dict((Key(index), index) for index in range(6))\n"
        "    right = dict((Key(index), index) for index in range(6))\n"
        "    outer = [left, {1: 2}]\n"
        "    other = [right, {1: 2}]\n"
        "    armed[0] = arm\n"
        "first = Key(1)\n"
        "second = Key(1)\n"
        "items = [first]\n"
        "same_items = [second]\n"
        "raising = [Raising()]\n"
        "same_raising = [Raising()]\n";
    static const char *const builds[] = {"build(True)\n", "build(True)\n", "build(False)\n"};
    static const char *const names[][2] = {{"outer", "other"}, {"left", "right"}, {"outer", "other"}};
    static const tinypy_bool_t expected[] = {TINYPY_FALSE, TINYPY_FALSE, TINYPY_TRUE};
    test_allocator_state_t allocator_state;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *name_key = tinypy_string_from_bytes(vm, "__name__", 8U);
    tinypy_value_t *module_name = tinypy_string_from_bytes(vm, "equal", 5U);
    tinypy_dict_set(globals, name_key, module_name);
    tinypy_release(module_name);
    tinypy_release(name_key);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);
    for (size_t index = 0U; index < sizeof(builds) / sizeof(builds[0]); ++index) {
        TEST_CHECK(__test_native_contract_run(vm, globals, builds[index]) == 0);
        tinypy_value_t *left = __test_global(globals, names[index][0]);
        tinypy_value_t *right = __test_global(globals, names[index][1]);
        TEST_CHECK(left != NULL && right != NULL);
        tinypy_retain(left);
        tinypy_retain(right);
        tinypy_bool_t equal = tinypy_equal(left, right);
        tinypy_release(right);
        tinypy_release(left);
        TEST_CHECK(equal == expected[index]);
        TEST_CHECK(tinypy_vm_has_error(vm) == 0);
    }
    tinypy_value_t *first = __test_global(globals, "first");
    tinypy_value_t *second = __test_global(globals, "second");
    TEST_CHECK(tinypy_equal(first, second) == 0);
    tinypy_value_t *items = __test_global(globals, "items");
    tinypy_value_t *same_items = __test_global(globals, "same_items");
    TEST_CHECK(tinypy_equal(items, same_items) == 1);
    tinypy_value_t *raising = __test_global(globals, "raising");
    tinypy_value_t *same_raising = __test_global(globals, "same_raising");
    TEST_CHECK(tinypy_equal(raising, same_raising) == 0);
    TEST_CHECK(tinypy_vm_has_error(vm) != 0);
    tinypy_vm_clear_error(vm);

    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __test_native_failing_length(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(instance);

    (void)payload;
    (void)user_data;
    (void)out_error;
    tinypy_vm_raise_error(vm, TINYPY_ERROR_RUNTIME, "length failed");
    return -1;
}
//////////////////////////////////////////////////////////////////////////
/* len() and truth testing propagate the failure a native length callback
   reports through the VM error. */
static int32_t __test_native_length_errors(void) {
    static const char source[] =
        "failing = Failing()\n"
        "expect_error(RuntimeError, lambda: len(failing), 'length failed')\n"
        "expect_error(RuntimeError, lambda: bool(failing), 'length failed')\n"
        "expect_error(RuntimeError, lambda: not failing, 'length failed')\n";
    test_allocator_state_t allocator_state;
    tinypy_native_type_spec_t spec;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(void *);
    spec.sequence_length = __test_native_failing_length;
    tinypy_type_t *failing = tinypy_native_type_new(vm, "Failing", 7U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(failing != NULL && error == NULL);
    __test_native_contract_add_type(globals, "Failing", failing);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);

    tinypy_value_t *failing_value = tinypy_type_as_value(failing);
    tinypy_release(failing_value);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    TEST_CHECK(tinypy_vm_has_error(vm) == 0);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_native_destroy_state_t {
    tinypy_type_t *type;
    tinypy_value_t *module;
    size_t finalized;
    tinypy_bool_t data_intact;
} test_native_destroy_state_t;
//////////////////////////////////////////////////////////////////////////
/* The first finalizer makes a new native instance reachable; the second,
   which tinypy_vm_destroy must run before it frees any value, reads a
   module global. */
static void __test_native_destroy_finalize(tinypy_value_t *instance, void *payload, void *user_data) {
    test_native_destroy_state_t *state = (test_native_destroy_state_t *)user_data;

    (void)instance;
    (void)payload;
    state->finalized += 1U;
    if (state->finalized == 1U) {
        tinypy_value_t *late = tinypy_native_instance_new(state->type);

        tinypy_module_add_value(state->module, "late", 4U, late);
        tinypy_release(late);
        return;
    }
    tinypy_value_t *data = tinypy_module_get_value(state->module, "data", 4U);
    state->data_intact = data != NULL && tinypy_typeof(data) == TINYPY_VALUE_LIST && tinypy_list_size(data) == 1U ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_native_destroy_finalizers(void) {
    test_allocator_state_t allocator_state;
    test_native_destroy_state_t state;
    tinypy_native_type_spec_t spec;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    state.module = tinypy_module_new(vm, "app", 3U);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "app", 3U);
    tinypy_value_t *modules = tinypy_vm_modules(vm);
    tinypy_dict_set(modules, key, state.module);
    tinypy_release(key);
    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(void *);
    spec.user_data = &state;
    spec.finalize = __test_native_destroy_finalize;
    state.type = tinypy_native_type_new(vm, "Node", 4U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(state.type != NULL && error == NULL);
    tinypy_value_t *first = tinypy_native_instance_new(state.type);
    tinypy_value_t *item = tinypy_integer_from_i64(vm, 5);
    tinypy_value_t *data = tinypy_list_from_items(vm, &item, 1U);
    tinypy_module_add_value(state.module, "first", 5U, first);
    tinypy_module_add_value(state.module, "data", 4U, data);
    tinypy_release(data);
    tinypy_release(item);
    tinypy_release(first);
    tinypy_value_t *type_value = tinypy_type_as_value(state.type);
    tinypy_release(type_value);
    tinypy_release(state.module);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.finalized == 2U);
    TEST_CHECK(state.data_intact != 0);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
#define TEST_NATIVE_ORDER_MAGIC UINT32_C(0x600d)
//////////////////////////////////////////////////////////////////////////
typedef struct test_native_order_state_t {
    tinypy_value_t *globals;
    size_t calls;
    size_t stale_calls;
    size_t finalized_errors;
    size_t holders_freed;
    size_t instances_finalized;
} test_native_order_state_t;
//////////////////////////////////////////////////////////////////////////
/* The user data of a native function, which its finalizer frees as pybind
   deletes a method holder; a peer call runs Python before. */
typedef struct test_native_order_holder_t {
    uint32_t magic;
    test_native_order_state_t *state;
    const char *peer_call;
} test_native_order_holder_t;
//////////////////////////////////////////////////////////////////////////
static void __test_native_order_exec(tinypy_value_t *globals, const char *source) {
    tinypy_vm_t *vm = tinypy_value_vm(globals);
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *result = tinypy_exec_source(vm, source, strlen(source), "order.py", 8U, globals, NULL, &options, &error);
    if (result != NULL) {
        tinypy_release(result);
    }
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_vm_clear_error(vm);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_order_call(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_native_order_holder_t *holder = (test_native_order_holder_t *)tinypy_native_function_user_data(function);

    (void)self;
    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    holder->state->calls += 1U;
    if (holder->magic != TEST_NATIVE_ORDER_MAGIC) {
        holder->state->stale_calls += 1U;
    }
    tinypy_vm_t *vm = tinypy_value_vm(function);
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_order_record(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_native_order_state_t *state = (test_native_order_state_t *)tinypy_native_function_user_data(function);

    (void)self;
    (void)kwargs;
    (void)out_error;
    if (count == 1U && tinypy_truth(items[0], NULL) == 1) {
        state->finalized_errors += 1U;
    }
    tinypy_vm_t *vm = tinypy_value_vm(function);
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __test_native_order_free_holder(void *user_data) {
    test_native_order_holder_t *holder = (test_native_order_holder_t *)user_data;
    test_native_order_state_t *state = holder->state;

    __test_native_order_exec(state->globals, holder->peer_call);
    holder->magic = 0U;
    state->holders_freed += 1U;
    free(holder);
}
//////////////////////////////////////////////////////////////////////////
static void __test_native_order_finalize(tinypy_value_t *instance, void *payload, void *user_data) {
    test_native_order_state_t *state = (test_native_order_state_t *)user_data;

    (void)instance;
    (void)payload;
    state->instances_finalized += 1U;
    __test_native_order_exec(state->globals, "hook()\n");
}
//////////////////////////////////////////////////////////////////////////
static void __test_native_order_add_function(tinypy_value_t *module, test_native_order_state_t *state, const char *name, const char *peer_call) {
    tinypy_vm_t *vm = tinypy_value_vm(module);
    test_native_order_holder_t *holder = (test_native_order_holder_t *)malloc(sizeof(*holder));

    holder->magic = TEST_NATIVE_ORDER_MAGIC;
    holder->state = state;
    holder->peer_call = peer_call;
    tinypy_value_t *function = tinypy_native_function_new_items_keywords(vm, name, strlen(name), __test_native_order_call, holder, __test_native_order_free_holder);
    tinypy_module_add_value(module, name, strlen(name), function);
    tinypy_release(function);
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_native_chain_state_t {
    tinypy_type_t *type;
    tinypy_value_t *module;
    size_t finalized;
} test_native_chain_state_t;
//////////////////////////////////////////////////////////////////////////
/* Publishes a new instance with this finalizer, as a destructor that posts
   a replacement object would. */
static void __test_native_chain_finalize(tinypy_value_t *instance, void *payload, void *user_data) {
    test_native_chain_state_t *state = (test_native_chain_state_t *)user_data;

    (void)instance;
    (void)payload;
    state->finalized += 1U;
    tinypy_value_t *next = tinypy_native_instance_new(state->type);
    tinypy_module_add_value(state->module, "current", 7U, next);
    tinypy_release(next);
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_vm_destroy finalizes every native instance before any native
   function: instance finalizers still call methods through a bound method
   and through the type, whose holders the method finalizers free. A function
   finalized before raises when Python calls it. A finalizer that always
   publishes a new native instance ends after bounded passes. */
static int32_t __test_native_destroy_order(void) {
    static const char source[] =
        "objects = [Node() for index in range(3)]\n"
        "bound = objects[0].serial\n"
        "def hook():\n"
        "    bound()\n"
        "    objects[1].serial()\n"
        "    first()\n";
    static const char first_peer[] =
        "try:\n"
        "    second()\n"
        "except RuntimeError as error:\n"
        "    record(str(error) == \"native function 'second' was finalized\")\n";
    static const char second_peer[] =
        "try:\n"
        "    first()\n"
        "except RuntimeError as error:\n"
        "    record(str(error) == \"native function 'first' was finalized\")\n";
    test_allocator_state_t allocator_state;
    test_native_order_state_t state;
    test_native_chain_state_t chain;
    tinypy_native_type_spec_t spec;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    (void)memset(&chain, 0, sizeof(chain));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *module = tinypy_module_new(vm, "app", 3U);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "app", 3U);
    tinypy_value_t *modules = tinypy_vm_modules(vm);
    tinypy_dict_set(modules, key, module);
    tinypy_release(key);
    tinypy_value_t *builtins = tinypy_vm_builtins(vm);
    tinypy_module_add_value(module, "__builtins__", 12U, builtins);
    state.globals = tinypy_module_dict(module);
    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(void *);
    spec.user_data = &state;
    spec.finalize = __test_native_order_finalize;
    tinypy_type_t *node = tinypy_native_type_new(vm, "Node", 4U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(node != NULL && error == NULL);
    test_native_order_holder_t *holder = (test_native_order_holder_t *)malloc(sizeof(*holder));
    holder->magic = TEST_NATIVE_ORDER_MAGIC;
    holder->state = &state;
    holder->peer_call = "pass\n";
    tinypy_value_t *method = tinypy_native_function_new_items_keywords(vm, "serial", 6U, __test_native_order_call, holder, __test_native_order_free_holder);
    tinypy_type_set_attr(node, "serial", 6U, method);
    tinypy_release(method);
    tinypy_value_t *node_value = tinypy_type_as_value(node);
    tinypy_module_add_value(module, "Node", 4U, node_value);
    tinypy_release(node_value);
    __test_native_order_add_function(module, &state, "first", first_peer);
    __test_native_order_add_function(module, &state, "second", second_peer);
    tinypy_value_t *record = tinypy_native_function_new_items_keywords(vm, "record", 6U, __test_native_order_record, &state, NULL);
    tinypy_module_add_value(module, "record", 6U, record);
    tinypy_release(record);
    TEST_CHECK(__test_native_contract_run(vm, state.globals, source) == 0);
    tinypy_release(module);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.instances_finalized == 3U);
    TEST_CHECK(state.stale_calls == 0U);
    TEST_CHECK(state.finalized_errors == 1U);
    TEST_CHECK(state.holders_freed == 3U);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);

    /* The chain ends after bounded passes; the instance its last finalizer
       publishes is freed without running the finalizer. */
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    chain.module = tinypy_module_new(vm, "chain", 5U);
    key = tinypy_string_from_bytes(vm, "chain", 5U);
    modules = tinypy_vm_modules(vm);
    tinypy_dict_set(modules, key, chain.module);
    tinypy_release(key);
    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(void *);
    spec.user_data = &chain;
    spec.finalize = __test_native_chain_finalize;
    chain.type = tinypy_native_type_new(vm, "Link", 4U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(chain.type != NULL && error == NULL);
    tinypy_value_t *link = tinypy_native_instance_new(chain.type);
    tinypy_module_add_value(chain.module, "current", 7U, link);
    tinypy_release(link);
    tinypy_value_t *type_value = tinypy_type_as_value(chain.type);
    tinypy_release(type_value);
    tinypy_release(chain.module);
    tinypy_vm_destroy(vm);
    TEST_CHECK(chain.finalized > 1U && chain.finalized < 64U);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_native_release_state_t {
    tinypy_value_t *globals;
    size_t created;
    size_t finalized;
    size_t probes;
    size_t reprs;
    size_t dead_calls;
    size_t refused;
    size_t attempts;
    size_t reports;
    tinypy_error_t *kept;
} test_native_release_state_t;
//////////////////////////////////////////////////////////////////////////
/* The payload of a host object, which its finalizer destroys. */
typedef struct test_native_release_payload_t {
    int64_t value;
    tinypy_bool_t alive;
} test_native_release_payload_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __test_native_release_construct(tinypy_value_t *instance, void *payload, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    test_native_release_state_t *state = (test_native_release_state_t *)user_data;
    test_native_release_payload_t *node = (test_native_release_payload_t *)payload;

    (void)instance;
    (void)kwargs;
    (void)out_error;
    tinypy_value_t *value = tinypy_tuple_get(args, 0U);
    node->value = tinypy_integer_as_i64(value);
    node->alive = TINYPY_TRUE;
    state->created += 1U;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* The owner notifies Python when it is destroyed, which drops the last
   references to other objects. */
static void __test_native_release_finalize(tinypy_value_t *instance, void *payload, void *user_data) {
    test_native_release_state_t *state = (test_native_release_state_t *)user_data;
    test_native_release_payload_t *node = (test_native_release_payload_t *)payload;

    (void)instance;
    node->alive = TINYPY_FALSE;
    state->finalized += 1U;
    if (node->value == 1) {
        __test_native_order_exec(state->globals, "del registry[:]\ndel first, victim\n");
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_release_repr(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    test_native_release_state_t *state = (test_native_release_state_t *)user_data;
    const test_native_release_payload_t *node = (const test_native_release_payload_t *)payload;
    tinypy_vm_t *vm = tinypy_value_vm(instance);

    (void)out_error;
    if (node->alive != 0) {
        state->reprs += 1U;
    }
    else {
        state->dead_calls += 1U;
    }
    tinypy_value_t *result = tinypy_string_from_bytes(vm, "<node>", 6U);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_release_probe(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_native_release_state_t *state = (test_native_release_state_t *)tinypy_native_function_user_data(function);
    const test_native_release_payload_t *node = (const test_native_release_payload_t *)tinypy_native_instance_payload(self);
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    if (node->alive != 0) {
        state->probes += 1U;
    }
    else {
        state->dead_calls += 1U;
    }
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __test_native_release_probe_finalize(void *user_data) {
    (void)user_data;

    //Empty
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_vm_destroy releases an instance whose last reference a finalizer
   drops before the instance's turn as usual: its __del__ runs before its
   finalizer and while the methods are still live. An instance dropped after
   its finalizer ran skips __del__. The native values that such releases make
   are finalized by a later pass. The finalizer of the owner, nested deeper
   than first and victim and less deep than the registered entity, runs
   between them. */
static int32_t __test_native_destroy_release(void) {
    static const char source[] =
        "class Script(object):\n"
        "    def __del__(self):\n"
        "        late.append(Node(6))\n"
        "class Entity(Node):\n"
        "    def __del__(self):\n"
        "        self.probe()\n"
        "        repr(self)\n"
        "        late.append(Node(7))\n"
        "late = []\n"
        "victim = Node(5)\n"
        "victim.script = Script()\n"
        "first = Entity(4)\n"
        "box = [Node(1)]\n"
        "registry = [[Entity(3)]]\n";
    test_allocator_state_t allocator_state;
    test_native_release_state_t state;
    tinypy_native_type_spec_t spec;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *module = tinypy_module_new(vm, "app", 3U);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "app", 3U);
    tinypy_value_t *modules = tinypy_vm_modules(vm);
    tinypy_dict_set(modules, key, module);
    tinypy_release(key);
    tinypy_value_t *builtins = tinypy_vm_builtins(vm);
    tinypy_module_add_value(module, "__builtins__", 12U, builtins);
    state.globals = tinypy_module_dict(module);
    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(test_native_release_payload_t);
    spec.payload_alignment = _Alignof(test_native_release_payload_t);
    spec.user_data = &state;
    spec.construct = __test_native_release_construct;
    spec.finalize = __test_native_release_finalize;
    spec.repr = __test_native_release_repr;
    tinypy_type_t *node = tinypy_native_type_new(vm, "Node", 4U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(node != NULL && error == NULL);
    tinypy_value_t *probe = tinypy_native_function_new_items(vm, "probe", 5U, __test_native_release_probe, 0U, 0U, &state, __test_native_release_probe_finalize);
    tinypy_type_set_attr(node, "probe", 5U, probe);
    tinypy_release(probe);
    tinypy_value_t *node_value = tinypy_type_as_value(node);
    tinypy_module_add_value(module, "Node", 4U, node_value);
    tinypy_release(node_value);
    TEST_CHECK(__test_native_contract_run(vm, state.globals, source) == 0);
    tinypy_release(module);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.created == 6U);
    TEST_CHECK(state.finalized == state.created);
    TEST_CHECK(state.probes == 1U && state.reprs == 1U);
    TEST_CHECK(state.dead_calls == 0U);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* Tells Python which host object its finalizer destroyed, as the destroy
   callbacks of Mengine do. */
static void __test_native_notify_finalize(tinypy_value_t *instance, void *payload, void *user_data) {
    test_native_release_state_t *state = (test_native_release_state_t *)user_data;
    test_native_release_payload_t *node = (test_native_release_payload_t *)payload;
    tinypy_vm_t *vm = tinypy_value_vm(instance);
    tinypy_error_t *error = NULL;

    node->alive = TINYPY_FALSE;
    state->finalized += 1U;
    tinypy_value_t *hook = __test_global(state->globals, "on_finalize");
    tinypy_value_t *value = tinypy_integer_from_i64(vm, node->value);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &value, 1U);
    tinypy_value_t *result = tinypy_call(hook, args, NULL, &error);
    if (result != NULL) {
        tinypy_release(result);
    }
    if (error != NULL && state->kept == NULL) {
        state->kept = error;
    }
    else if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_vm_clear_error(vm);
    tinypy_release(args);
    tinypy_release(value);
}
//////////////////////////////////////////////////////////////////////////
/* Counts a callback that reaches the payload of a finalized instance. */
static void __test_native_zombie_touch(void *user_data, const void *payload) {
    test_native_release_state_t *state = (test_native_release_state_t *)user_data;
    const test_native_release_payload_t *node = (const test_native_release_payload_t *)payload;

    if (node->alive == 0) {
        state->dead_calls += 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __test_native_zombie_hash(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)instance;
    (void)out_error;
    __test_native_zombie_touch(user_data, payload);
    return 7;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_zombie_compare(tinypy_value_t *instance, void *payload, tinypy_value_t *other, tinypy_compare_operation_e operation, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(instance);

    (void)other;
    (void)operation;
    (void)out_error;
    __test_native_zombie_touch(user_data, payload);
    tinypy_value_t *result = tinypy_bool_from_i32(vm, 0);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_zombie_add(tinypy_value_t *instance, void *payload, tinypy_value_t *other, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(instance);

    (void)other;
    (void)out_error;
    __test_native_zombie_touch(user_data, payload);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, 1);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_zombie_call(tinypy_value_t *instance, void *payload, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(instance);

    (void)args;
    (void)kwargs;
    (void)out_error;
    __test_native_zombie_touch(user_data, payload);
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __test_native_zombie_length(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)instance;
    (void)out_error;
    __test_native_zombie_touch(user_data, payload);
    return 1;
}
//////////////////////////////////////////////////////////////////////////
/* The accessors of a property that pybind registers for a member: plain
   native functions that take the instance as their first argument. */
static tinypy_value_t *__test_native_zombie_get_value(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    const test_native_release_payload_t *node = (const test_native_release_payload_t *)tinypy_native_instance_payload(items[0]);
    void *state = tinypy_native_function_user_data(function);
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)self;
    (void)count;
    (void)kwargs;
    (void)out_error;
    __test_native_zombie_touch(state, node);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, node->value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_zombie_set_value(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    const test_native_release_payload_t *node = (const test_native_release_payload_t *)tinypy_native_instance_payload(items[0]);
    void *state = tinypy_native_function_user_data(function);
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)self;
    (void)count;
    (void)kwargs;
    (void)out_error;
    __test_native_zombie_touch(state, node);
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Registers the value property on a type of the destroy tests. */
static void __test_native_zombie_add_property(tinypy_vm_t *vm, tinypy_type_t *type, test_native_release_state_t *state) {
    tinypy_value_t *getter = tinypy_native_function_new_items(vm, "value", 5U, __test_native_zombie_get_value, 1U, 1U, state, NULL);
    tinypy_value_t *setter = tinypy_native_function_new_items(vm, "value", 5U, __test_native_zombie_set_value, 2U, 2U, state, NULL);
    tinypy_value_t *property = tinypy_property_new(vm, getter, setter, NULL, NULL);

    tinypy_type_set_attr(type, "value", 5U, property);
    tinypy_release(property);
    tinypy_release(setter);
    tinypy_release(getter);
}
//////////////////////////////////////////////////////////////////////////
/* note() counts a write of the text that the runtime reports as unraisable
   on sys.stderr. */
static tinypy_value_t *__test_native_zombie_note(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_native_release_state_t *state = (test_native_release_state_t *)tinypy_native_function_user_data(function);
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)self;
    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    state->reports += 1U;
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* record(refused, attempts) reports how many of the attempts raised. */
static tinypy_value_t *__test_native_zombie_record(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_native_release_state_t *state = (test_native_release_state_t *)tinypy_native_function_user_data(function);
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)self;
    (void)count;
    (void)kwargs;
    (void)out_error;
    state->refused = (size_t)tinypy_integer_as_i64(items[0]);
    state->attempts = (size_t)tinypy_integer_as_i64(items[1]);
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Registers the Node type of the destroy tests, whose finalizer calls
   on_finalize with the node value. */
static void __test_native_notify_type(tinypy_value_t *module, test_native_release_state_t *state) {
    tinypy_vm_t *vm = tinypy_value_vm(module);
    tinypy_native_type_spec_t spec;

    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(test_native_release_payload_t);
    spec.payload_alignment = _Alignof(test_native_release_payload_t);
    spec.user_data = state;
    spec.construct = __test_native_release_construct;
    spec.finalize = __test_native_notify_finalize;
    spec.repr = __test_native_release_repr;
    spec.hash = __test_native_zombie_hash;
    spec.compare = __test_native_zombie_compare;
    spec.add = __test_native_zombie_add;
    spec.call = __test_native_zombie_call;
    spec.sequence_length = __test_native_zombie_length;
    tinypy_type_t *node = tinypy_native_type_new(vm, "Node", 4U, NULL, 0U, NULL, &spec, NULL);
    tinypy_value_t *probe = tinypy_native_function_new_items(vm, "probe", 5U, __test_native_release_probe, 0U, 0U, state, __test_native_release_probe_finalize);
    tinypy_type_set_attr(node, "probe", 5U, probe);
    tinypy_release(probe);
    __test_native_zombie_add_property(vm, node, state);
    tinypy_value_t *node_value = tinypy_type_as_value(node);
    tinypy_module_add_value(module, "Node", 4U, node_value);
    tinypy_release(node_value);
    tinypy_value_t *record = tinypy_native_function_new_items(vm, "record", 6U, __test_native_zombie_record, 2U, 2U, state, NULL);
    tinypy_module_add_value(module, "record", 6U, record);
    tinypy_release(record);
    tinypy_value_t *note = tinypy_native_function_new_items(vm, "note", 4U, __test_native_zombie_note, 0U, SIZE_MAX, state, NULL);
    tinypy_module_add_value(module, "note", 4U, note);
    tinypy_release(note);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_vm_t *__test_native_notify_vm(const tinypy_vm_config_t *config, test_native_release_state_t *state, tinypy_value_t **out_module) {
    tinypy_vm_t *vm = tinypy_vm_create(config);
    tinypy_value_t *module = tinypy_module_new(vm, "app", 3U);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "app", 3U);
    tinypy_value_t *modules = tinypy_vm_modules(vm);

    tinypy_dict_set(modules, key, module);
    tinypy_release(key);
    tinypy_value_t *builtins = tinypy_vm_builtins(vm);
    tinypy_module_add_value(module, "__builtins__", 12U, builtins);
    state->globals = tinypy_module_dict(module);
    __test_native_notify_type(module, state);
    *out_module = module;
    return vm;
}
//////////////////////////////////////////////////////////////////////////
/* Python that tinypy_vm_destroy runs after it finalized a reachable instance,
   here the __del__ of an object that a later finalizer drops, reaches the
   slots and the methods of that instance only as a RuntimeError, as it
   reaches a finalized native function: through a bound method made before,
   the method call, the descriptor, each slot and the native accessors of a
   property, through the property or called directly. */
static int32_t __test_native_destroy_finalized(void) {
    static const char source[] =
        "def attempt(call):\n"
        "    try:\n"
        "        call()\n"
        "    except RuntimeError as error:\n"
        "        return str(error)\n"
        "    return 'called'\n"
        "class Probe(object):\n"
        "    def __del__(self):\n"
        "        node = nodes[0]\n"
        "        def assign():\n"
        "            node.value = 7\n"
        "        calls = (lambda: repr(node), lambda: hash(node), lambda: node == 5, lambda: node + 1, node, lambda: len(node),\n"
        "                 bound, lambda: node.probe(), lambda: Node.probe(node), getattr(node, 'probe'), lambda: node.value, assign,\n"
        "                 lambda: Node.value.fget(node), lambda: Node.value.fset(node, 3), lambda: getattr(Node.value, 'fget')(node))\n"
        "        results = [attempt(call) for call in calls]\n"
        "        record(results.count(\"'Node' object was finalized\"), len(results))\n"
        "def on_finalize(value):\n"
        "    if value == 2:\n"
        "        del keep[:]\n"
        "nodes = [Node(1), Node(2)]\n"
        "bound = nodes[0].probe\n"
        "keep = [Probe()]\n";
    test_allocator_state_t allocator_state;
    test_native_release_state_t state;
    tinypy_value_t *module = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = __test_native_notify_vm(&config, &state, &module);
    TEST_CHECK(__test_native_contract_run(vm, state.globals, source) == 0);
    tinypy_release(module);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.finalized == 2U);
    TEST_CHECK(state.attempts == 15U && state.refused == state.attempts);
    TEST_CHECK(state.dead_calls == 0U);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_vm_destroy repeats its finalizer pass for the instances a pass left
   unfinalized: one whose __del__ resurrected it and one a finalizer took
   again through a weak reference. The diagnostics that the finalizers raised
   and the host keeps render at the end of their pass, and the native values
   their str() creates are finalized by another pass. */
static int32_t __test_native_destroy_passes(void) {
    static const char resurrect_source[] =
        "class Entity(Node):\n"
        "    def __del__(self):\n"
        "        resurrected.append(self)\n"
        "def on_finalize(value):\n"
        "    if value == 1:\n"
        "        del registry[:]\n"
        "resurrected = []\n"
        "box = [Node(1)]\n"
        "registry = [[Entity(3)]]\n";
    static const char retake_source[] =
        "import _weakref\n"
        "def on_finalize(value):\n"
        "    if value == 1:\n"
        "        del registry[:]\n"
        "    if value == 5:\n"
        "        retaken.append(references[0][0][0][0]())\n"
        "retaken = []\n"
        "box = [Node(1)]\n"
        "registry = [[Node(4)]]\n"
        "deep = [[[Node(5)]]]\n"
        "references = [[[[_weakref.ref(registry[0][0])]]]]\n";
    static const char render_source[] =
        "class Rendered(Exception):\n"
        "    def __str__(self):\n"
        "        late.append(Node(7))\n"
        "        return 'rendered'\n"
        "def on_finalize(value):\n"
        "    if value == 2:\n"
        "        raise Rendered()\n"
        "late = []\n"
        "box = [Node(2)]\n";
    const char *const sources[] = {resurrect_source, retake_source, render_source};
    static const size_t created[] = {2U, 3U, 2U};
    static const char *const kept_messages[] = {NULL, NULL, "rendered"};
    test_allocator_state_t allocator_state;
    test_native_release_state_t state;
    tinypy_value_t *module = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    for (size_t index = 0U; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        (void)memset(&state, 0, sizeof(state));
        tinypy_vm_t *vm = __test_native_notify_vm(&config, &state, &module);
        TEST_CHECK(__test_native_contract_run(vm, state.globals, sources[index]) == 0);
        tinypy_release(module);
        tinypy_vm_destroy(vm);
        TEST_CHECK(state.created == created[index] && state.finalized == state.created);
        TEST_CHECK(state.dead_calls == 0U);
        TEST_CHECK((state.kept != NULL) == (kept_messages[index] != NULL));
        if (state.kept != NULL) {
            size_t size = 0U;
            const char *message = tinypy_error_message(state.kept, &size);

            TEST_CHECK(size == strlen(kept_messages[index]) && memcmp(message, kept_messages[index], size) == 0);
            tinypy_error_release(state.kept);
        }
        TEST_CHECK(allocator_state.outstanding_allocations == 0U);
        TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_vm_destroy finalizes only the values of the host: the runtime's own
   native values hold no host object, so Python that a host finalizer runs,
   or that runs once the native functions are finalized, still uses them. A
   memoryview reads its owner, a dictproxy stays unhashable, an exception
   still runs its __del__, struct functions still pack, and a class that dies
   after the functions are finalized still leaves its base silently. */
static int32_t __test_native_destroy_runtime_values(void) {
    static const char source[] =
        "import _struct\n"
        "import sys\n"
        "app = sys.modules['app']\n"
        "class Log(object):\n"
        "    def write(self, text):\n"
        "        note(text)\n"
        "class Failure(Exception):\n"
        "    def __del__(self):\n"
        "        deleted.append('failure')\n"
        "class Tail(object):\n"
        "    def __del__(self):\n"
        "        check(_struct.unpack('<d', _struct.pack('<d', 1.5)), (1.5,))\n"
        "        check(view.tobytes(), 'abc')\n"
        "        check(deleted, ['failure'])\n"
        "class Entity(Node):\n"
        "    pass\n"
        "class Plain(object):\n"
        "    x = 1\n"
        "def on_finalize(value):\n"
        "    if value != 1:\n"
        "        return\n"
        "    check(len(view), 3)\n"
        "    check(view[0], 'a')\n"
        "    check(view.tobytes(), 'abc')\n"
        "    expect_error(TypeError, lambda: hash(proxy), \"unhashable type: 'dictproxy'\")\n"
        "    check(proxy['x'], 1)\n"
        "    check(sys.version_info[0], 2)\n"
        "    del app.failure, app.bound, app.Entity\n"
        "deleted = []\n"
        "sys.stderr = Log()\n"
        "view = memoryview(bytearray('abc'))\n"
        "proxy = Plain.__dict__\n"
        "failure = Failure()\n"
        "entity = Entity(3)\n"
        "entity.tail = Tail()\n"
        "bound = entity.probe\n"
        "del entity\n"
        "box = [Node(1)]\n";
    test_allocator_state_t allocator_state;
    test_native_release_state_t state;
    tinypy_value_t *module = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = __test_native_notify_vm(&config, &state, &module);
    TEST_CHECK(__test_native_contract_run(vm, state.globals, source) == 0);
    tinypy_release(module);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.created == 2U && state.finalized == state.created);
    TEST_CHECK(state.dead_calls == 0U);
    TEST_CHECK(state.kept == NULL);
    TEST_CHECK(state.reports == 0U);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* Hands the instance to Python before it releases the host object, as the
   destroy notification of an engine object does; the second unit leaves an
   error raised for the runtime to report. */
static void __test_native_handoff_finalize(tinypy_value_t *instance, void *payload, void *user_data) {
    test_native_release_state_t *state = (test_native_release_state_t *)user_data;
    test_native_release_payload_t *unit = (test_native_release_payload_t *)payload;
    tinypy_vm_t *vm = tinypy_value_vm(instance);
    tinypy_error_t *error = NULL;

    state->finalized += 1U;
    tinypy_value_t *hook = __test_global(state->globals, "on_finalize");
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &instance, 1U);
    tinypy_value_t *result = tinypy_call(hook, args, NULL, &error);
    if (result != NULL) {
        tinypy_release(result);
    }
    if (error != NULL && state->kept == NULL) {
        state->kept = error;
    }
    else if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_vm_clear_error(vm);
    tinypy_release(args);
    unit->alive = TINYPY_FALSE;
    if (unit->value == 2) {
        tinypy_vm_raise_error(vm, TINYPY_ERROR_VALUE, "boom");
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_handoff_late(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)self;
    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    tinypy_value_t *result = tinypy_string_from_bytes(vm, "called", 6U);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Keeps the function its finalizer belongs to alive, so that it outlives its
   finalizer. */
static void __test_native_handoff_late_finalize(void *user_data) {
    tinypy_value_t *function = *(tinypy_value_t **)user_data;

    tinypy_retain(function);
}
//////////////////////////////////////////////////////////////////////////
/* A native instance finalizer may hand its instance to Python, which reaches
   its slots, methods and property accessors until the finalizer returns.
   Once the finalizer ran they raise RuntimeError, and the error it left
   raised is reported with the instance named as object.__repr__ names it,
   without the repr callback of its type. A finalized native function
   registered on the type afterwards stays finalized. */
static int32_t __test_native_finalizer_handoff(void) {
    static const char source[] =
        "import sys\n"
        "class Log(object):\n"
        "    def write(self, text):\n"
        "        log.append(text)\n"
        "def on_finalize(unit):\n"
        "    seen.append((repr(unit), hash(unit), unit.probe(), unit.value))\n"
        "    names.append(object.__repr__(unit))\n"
        "    if unit.value == 3:\n"
        "        kept.append(unit)\n"
        "log = []\n"
        "seen = []\n"
        "names = []\n"
        "kept = []\n"
        "saved = sys.stderr\n"
        "sys.stderr = Log()\n"
        "for value in (1, 2, 3):\n"
        "    unit = Unit(value)\n"
        "    del unit\n"
        "sys.stderr = saved\n"
        "check(seen, [('<node>', 7, None, 1), ('<node>', 7, None, 2), ('<node>', 7, None, 3)])\n"
        "check(names[1].startswith('<Unit object at 0x'), True)\n"
        "check(''.join(log), \"Exception ValueError: 'boom' in %s ignored\\n\" % names[1])\n"
        "unit = kept.pop()\n"
        "def assign():\n"
        "    unit.value = 4\n"
        "for call in (lambda: repr(unit), lambda: hash(unit), unit.probe, lambda: unit.value, assign):\n"
        "    expect_error(RuntimeError, call, \"'Unit' object was finalized\")\n"
        "live = Unit(4)\n"
        "expect_error(RuntimeError, live.late, \"native function 'late' was finalized\")\n"
        "del live\n";
    test_allocator_state_t allocator_state;
    test_native_release_state_t state;
    tinypy_native_type_spec_t spec;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    state.globals = tinypy_dict_new(vm);
    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(test_native_release_payload_t);
    spec.payload_alignment = _Alignof(test_native_release_payload_t);
    spec.user_data = &state;
    spec.construct = __test_native_release_construct;
    spec.finalize = __test_native_handoff_finalize;
    spec.repr = __test_native_release_repr;
    spec.hash = __test_native_zombie_hash;
    tinypy_type_t *unit = tinypy_native_type_new(vm, "Unit", 4U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(unit != NULL && error == NULL);
    tinypy_value_t *probe = tinypy_native_function_new_items(vm, "probe", 5U, __test_native_release_probe, 0U, 0U, &state, NULL);
    tinypy_type_set_attr(unit, "probe", 5U, probe);
    tinypy_release(probe);
    __test_native_zombie_add_property(vm, unit, &state);
    /* Its finalizer keeps it through the variable, so it is registered
       after its finalizer ran. */
    tinypy_value_t *late = tinypy_native_function_new_items(vm, "late", 4U, __test_native_handoff_late, 0U, 0U, &late, __test_native_handoff_late_finalize);
    tinypy_release(late);
    tinypy_type_set_attr(unit, "late", 4U, late);
    tinypy_release(late);
    __test_native_contract_add_type(state.globals, "Unit", unit);
    TEST_CHECK(__test_native_contract_run(vm, state.globals, source) == 0);
    TEST_CHECK(state.created == 4U && state.finalized == state.created);
    TEST_CHECK(state.probes == 4U && state.reprs == 4U && state.dead_calls == 0U);
    TEST_CHECK(state.kept == NULL);

    tinypy_value_t *unit_value = tinypy_type_as_value(unit);
    tinypy_release(unit_value);
    tinypy_dict_clear(state.globals);
    tinypy_release(state.globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* The wrapper an engine object keeps a borrowed pointer to until the
   finalizer of the wrapper clears it. */
typedef struct test_native_embed_state_t {
    tinypy_value_t *embed;
    size_t finalized;
} test_native_embed_state_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __test_native_embed_construct(tinypy_value_t *instance, void *payload, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    test_native_embed_state_t *state = (test_native_embed_state_t *)user_data;

    (void)payload;
    (void)args;
    (void)kwargs;
    (void)out_error;
    state->embed = instance;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __test_native_embed_finalize(tinypy_value_t *instance, void *payload, void *user_data) {
    test_native_embed_state_t *state = (test_native_embed_state_t *)user_data;

    (void)instance;
    (void)payload;
    state->finalized += 1U;
    state->embed = NULL;
}
//////////////////////////////////////////////////////////////////////////
/* Hands the wrapper out again, as the engine does when Python asks it for
   the object. */
static tinypy_value_t *__test_native_embed_lookup(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_native_embed_state_t *state = (test_native_embed_state_t *)tinypy_native_function_user_data(function);
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)self;
    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    if (state->embed == NULL) {
        tinypy_value_t *none = tinypy_none_get(vm);
        return none;
    }
    tinypy_retain(state->embed);
    return state->embed;
}
//////////////////////////////////////////////////////////////////////////
/* A native instance released inside finalizers nested past the depth at which
   the trashcan leaves objects to the outermost finalizer, alone or with a
   __del__ of a Python subclass, still runs its finalizer at once, so the host
   never hands out a wrapper that waits for its finalizer. */
static int32_t __test_native_finalizer_nested(void) {
    static const char source[] =
        "class Child(Node):\n"
        "    def __del__(self):\n"
        "        deleted.append('child')\n"
        "class Holder(object):\n"
        "    def __init__(self, following):\n"
        "        self.following = following\n"
        "        self.node = None\n"
        "    def __del__(self):\n"
        "        deleted.append('holder')\n"
        "        if self.node is not None:\n"
        "            self.node = None\n"
        "            found.append(lookup())\n"
        "        self.following = None\n"
        "found = []\n"
        "deleted = []\n"
        "for kind in (Node, Child):\n"
        "    head = None\n"
        "    for index in range(12):\n"
        "        head = Holder(head)\n"
        "    holder = head\n"
        "    for index in range(7):\n"
        "        holder = holder.following\n"
        "    holder.node = kind()\n"
        "    del holder\n"
        "    del head\n"
        "check(found, [None, None])\n"
        "check((deleted.count('holder'), deleted.count('child')), (24, 1))\n";
    test_allocator_state_t allocator_state;
    test_native_embed_state_t state;
    tinypy_native_type_spec_t spec;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_native_type_spec_init(&spec);
    spec.user_data = &state;
    spec.construct = __test_native_embed_construct;
    spec.finalize = __test_native_embed_finalize;
    tinypy_type_t *node = tinypy_native_type_new(vm, "Node", 4U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(node != NULL && error == NULL);
    __test_native_contract_add_type(globals, "Node", node);
    tinypy_value_t *lookup = tinypy_native_function_new_items(vm, "lookup", 6U, __test_native_embed_lookup, 0U, 0U, &state, NULL);
    tinypy_value_t *lookup_key = tinypy_string_from_bytes(vm, "lookup", 6U);
    tinypy_dict_set(globals, lookup_key, lookup);
    tinypy_release(lookup_key);
    tinypy_release(lookup);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);
    TEST_CHECK(state.finalized == 2U && state.embed == NULL);

    tinypy_value_t *node_value = tinypy_type_as_value(node);
    tinypy_release(node_value);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_native_signal_state_t {
    test_allocator_state_t *allocator_state;
    tinypy_vm_t *vm;
} test_native_signal_state_t;
//////////////////////////////////////////////////////////////////////////
/* A hash callback that fails through out_error alone: the error of a nested
   call whose VM exception it cleared. */
static tinypy_hash_t __test_native_signal_hash(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(instance);
    tinypy_value_t *text = tinypy_string_from_bytes(vm, "zz", 2U);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &text, 1U);
    tinypy_error_t *error = NULL;

    (void)payload;
    (void)user_data;
    tinypy_value_t *int_value = tinypy_type_as_value(&vm->types[TINYPY_VALUE_INTEGER]);
    tinypy_value_t *result = tinypy_call(int_value, args, NULL, &error);
    if (result != NULL) {
        tinypy_release(result);
    }
    tinypy_release(args);
    tinypy_release(text);
    tinypy_vm_clear_error(vm);
    *out_error = error;
    return -1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_signal_iter(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    (void)out_error;
    *(int64_t *)payload = 0;
    tinypy_retain(instance);
    return instance;
}
//////////////////////////////////////////////////////////////////////////
/* Fails through the VM error alone, as pybind does, after the items its
   user data counts. */
static tinypy_value_t *__test_native_signal_next(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(instance);
    int64_t *index = (int64_t *)payload;

    (void)out_error;
    if (*index < (int64_t)(intptr_t)user_data) {
        *index += 1;
        tinypy_value_t *item = tinypy_integer_from_i64(vm, *index);
        return item;
    }
    tinypy_vm_raise_error(vm, TINYPY_ERROR_INDEX, "next failed");
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* refuse(on) makes the allocations of empty diagnostics fail. */
static tinypy_value_t *__test_native_signal_refuse(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_native_signal_state_t *state = (test_native_signal_state_t *)tinypy_native_function_user_data(function);

    (void)self;
    (void)count;
    (void)kwargs;
    (void)out_error;
    int32_t refuse = tinypy_truth(items[0], NULL);
    state->allocator_state->fail_allocation_size = refuse > 0 ? sizeof(tinypy_error_t) + 3U : 0U;
    tinypy_value_t *result = tinypy_none_get(state->vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Failures are told apart from results by what the VM raised: hash() and the
   __hash__ methods report a hash callback that fails through out_error alone,
   an iteration consumer reports the exception a next callback raised even
   when its diagnostic could not be allocated, and tinypy_vm_normalize_error
   normalizes the exception that a failed normalization raised instead, as
   PyErr_NormalizeException does. */
static int32_t __test_native_failure_signals(void) {
    static const char source[] =
        "message = \"invalid literal for int() with base 10: 'zz'\"\n"
        "hashing = Hashing()\n"
        "for call in (lambda: hash(hashing), lambda: {hashing: 1}, lambda: set([hashing]), lambda: hash((1, hashing)), lambda: (1, hashing).__hash__()):\n"
        "    expect_error(ValueError, call, message)\n"
        "stream = Stream()\n"
        "def unpack_short():\n"
        "    first, second = stream\n"
        "def unpack_long():\n"
        "    first, = Single()\n"
        "def loop():\n"
        "    for item in stream:\n"
        "        pass\n"
        "def probe(call):\n"
        "    refuse(True)\n"
        "    try:\n"
        "        call()\n"
        "    except IndexError:\n"
        "        return 'IndexError'\n"
        "    except BaseException as error:\n"
        "        return type(error).__name__\n"
        "    finally:\n"
        "        refuse(False)\n"
        "    return 'returned'\n"
        "calls = (lambda: next(stream), lambda: next(stream, None), lambda: list(stream), lambda: tuple(stream), lambda: [item for item in stream], loop,\n"
        "         unpack_short, unpack_long, lambda: sorted(stream), lambda: sum(stream), lambda: min(stream), lambda: any(stream), lambda: filter(None, stream),\n"
        "         lambda: zip(stream), lambda: map(None, stream), lambda: set(stream), lambda: set().isdisjoint(stream), lambda: dict(stream),\n"
        "         lambda: 1 in stream, lambda: bytearray(stream), lambda: reduce(None, stream), lambda: [].extend(stream))\n"
        "check([probe(call) for call in calls], ['IndexError'] * len(calls))\n";
    test_allocator_state_t allocator_state;
    test_native_signal_state_t state;
    tinypy_native_type_spec_t spec;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    state.allocator_state = &allocator_state;
    state.vm = vm;
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *builtins = tinypy_vm_builtins(vm);
    tinypy_value_t *builtins_key = tinypy_string_from_bytes(vm, "__builtins__", 12U);
    tinypy_dict_set(globals, builtins_key, builtins);
    tinypy_release(builtins_key);
    tinypy_native_type_spec_init(&spec);
    spec.hash = __test_native_signal_hash;
    tinypy_type_t *hashing = tinypy_native_type_new(vm, "Hashing", 7U, NULL, 0U, NULL, &spec, NULL);
    __test_native_contract_add_type(globals, "Hashing", hashing);
    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(int64_t);
    spec.payload_alignment = _Alignof(int64_t);
    spec.iter = __test_native_signal_iter;
    spec.next = __test_native_signal_next;
    tinypy_type_t *stream = tinypy_native_type_new(vm, "Stream", 6U, NULL, 0U, NULL, &spec, NULL);
    __test_native_contract_add_type(globals, "Stream", stream);
    spec.user_data = (void *)(intptr_t)1;
    tinypy_type_t *single = tinypy_native_type_new(vm, "Single", 6U, NULL, 0U, NULL, &spec, NULL);
    __test_native_contract_add_type(globals, "Single", single);
    tinypy_value_t *refuse = tinypy_native_function_new_items(vm, "refuse", 6U, __test_native_signal_refuse, 1U, 1U, &state, NULL);
    tinypy_value_t *refuse_key = tinypy_string_from_bytes(vm, "refuse", 6U);
    tinypy_dict_set(globals, refuse_key, refuse);
    tinypy_release(refuse_key);
    tinypy_release(refuse);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);
    TEST_CHECK(tinypy_vm_has_error(vm) == 0);

    static const char normalize_source[] =
        "import sys\n"
        "class Meta(type):\n"
        "    def __subclasscheck__(cls, other):\n"
        "        return False\n"
        "class Exiting(Exception):\n"
        "    __metaclass__ = Meta\n"
        "    def __init__(self, *arguments):\n"
        "        if arguments != ('first',):\n"
        "            sys.exit(42)\n"
        "raise Exiting('first')\n";
    tinypy_compile_options_t options;
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    TEST_CHECK(tinypy_exec_source(vm, normalize_source, sizeof(normalize_source) - 1U, "normalize.py", 12U, globals, NULL, &options, &error) == NULL);
    TEST_CHECK(error != NULL);
    tinypy_error_release(error);
    error = NULL;
    TEST_CHECK(tinypy_vm_normalize_error(vm, &error) == 0);
    tinypy_type_t *system_exit = vm->exception_types[TINYPY_EXCEPTION_SYSTEM_EXIT];
    tinypy_value_t *raised = tinypy_vm_raised_exception(vm);
    TEST_CHECK(tinypy_vm_raised_exception_type(vm) == tinypy_type_as_value(system_exit) && raised != NULL && raised->type == system_exit);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_RUNTIME);
    tinypy_error_release(error);
    tinypy_vm_clear_error(vm);

    tinypy_type_t *const releases[] = {hashing, stream, single};
    for (size_t index = 0U; index < sizeof(releases) / sizeof(releases[0]); ++index) {
        tinypy_value_t *type_value = tinypy_type_as_value(releases[index]);

        tinypy_release(type_value);
    }
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* refuse(on) makes the host refuse the requests between the pooled sizes
   and the arena size, as an allocator out of memory does. */
static tinypy_value_t *__test_native_refuse_direct(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_native_signal_state_t *state = (test_native_signal_state_t *)tinypy_native_function_user_data(function);

    (void)self;
    (void)count;
    (void)kwargs;
    (void)out_error;
    int32_t refuse = tinypy_truth(items[0], NULL);
    state->allocator_state->fail_allocation_above = refuse > 0 ? TINYPY_INTERNAL_POOL_SMALL_REQUEST : 0U;
    state->allocator_state->fail_allocation_below = refuse > 0 ? TINYPY_INTERNAL_POOL_ARENA_SIZE : 0U;
    tinypy_value_t *result = tinypy_none_get(state->vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __test_module_registered(tinypy_vm_t *vm, const char *name, size_t name_size) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_bool_t registered = tinypy_dict_get_optional(tinypy_vm_modules(vm), key) != NULL ? TINYPY_TRUE : TINYPY_FALSE;

    tinypy_release(key);
    return registered;
}
//////////////////////////////////////////////////////////////////////////
/* Language operations raise MemoryError when the host refuses the memory of
   a computed-size result, and the offset index of a unicode string falls
   back to a scan; each call runs once unrefused first, so that its frame and
   verified code come from the caches. An import whose metadata or entry in
   sys.modules the host refuses fails with MemoryError, leaves no module
   behind and runs nothing, and succeeds once the memory is there. */
static int32_t __test_computed_allocation_refusal(void) {
    static const char source[] =
        "import _struct\n"
        "s = 'a' * 700 + 'b c,d'\n"
        "u = unicode(s)\n"
        "digits = u'1' * 700\n"
        "codec = 'q' * 700\n"
        "template = '{0}' + 'x' * 700\n"
        "z = '\\x00' * 560\n"
        "items = [0] * 100\n"
        "fresh = [u'\\xe9' * 6400, u'\\xe9' * 6400]\n"
        "n = 0\n"
        "def f(*a):\n"
        "    return a\n"
        "class Initialized(object):\n"
        "    def __init__(self, *a):\n"
        "        pass\n"
        "class Callable(object):\n"
        "    def __call__(self, *a):\n"
        "        return a\n"
        "k = Callable()\n"
        "arguments = 'n, ' * 80\n"
        "build_list = eval('lambda: [' + arguments + ']')\n"
        "build_tuple = eval('lambda: (' + arguments + ')')\n"
        "make_function = eval('lambda: lambda ' + ', '.join('a%d=n' % index for index in range(80)) + ': 0')\n"
        "call_object = eval('lambda: k(' + arguments + ')')\n"
        "construct = eval('lambda: Initialized(' + arguments + ')')\n"
        "make_exception = eval('lambda: ValueError(' + arguments + ')')\n"
        "def probe(call):\n"
        "    try:\n"
        "        call()\n"
        "    except (LookupError, ImportError):\n"
        "        pass\n"
        "    refuse(True)\n"
        "    try:\n"
        "        call()\n"
        "    except MemoryError:\n"
        "        return 'MemoryError'\n"
        "    finally:\n"
        "        refuse(False)\n"
        "    return 'returned'\n"
        "calls = (lambda: s.partition('b'), lambda: s.rpartition('b'), lambda: s.split(','), lambda: s.rsplit(','), lambda: s.strip('d'),\n"
        "         lambda: u[1:], lambda: u[::2], lambda: u.strip(u'd'), lambda: u.partition(u','), lambda: unicode(s), lambda: int(digits),\n"
        "         lambda: 'x'.decode(codec), lambda: list(template._formatter_parser()), lambda: f(*items), lambda: _struct.unpack('70d', z),\n"
        "         lambda: memoryview(s).tobytes(), lambda: str(buffer(s)), lambda: __import__(s),\n"
        "         build_list, build_tuple, make_function, call_object, construct, make_exception)\n"
        "check([probe(call) for call in calls], ['MemoryError'] * len(calls))\n"
        "check(probe(lambda: fresh.pop()[3000]), 'returned')\n";
    static const char package_directory[] =
        "packagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackage"
        "packagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackage"
        "packagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackage"
        "packagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackage"
        "packagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackagepackage";
    static const char package_source[] = "VALUE = 1\n";
    static const char module_source[] = "import sys\nsys.runs = getattr(sys, 'runs', 0) + 1\n";
    test_allocator_state_t allocator_state;
    test_native_signal_state_t state;
    tinypy_module_artifact_t artifacts[2];
    tinypy_host_t host;
    tinypy_error_t *error = NULL;

    (void)memset(artifacts, 0, sizeof(artifacts));
    for (size_t index = 0U; index < 2U; ++index) {
        artifacts[index].abi_version = TINYPY_ABI_VERSION;
        artifacts[index].struct_size = (uint32_t)sizeof(artifacts[index]);
        artifacts[index].content_kind = TINYPY_MODULE_CONTENT_SOURCE;
        artifacts[index].canonical_name_size = 3U;
    }
    artifacts[0].canonical_name = "pkg";
    artifacts[0].data = package_source;
    artifacts[0].data_size = sizeof(package_source) - 1U;
    artifacts[0].flags = (uint32_t)TINYPY_MODULE_ARTIFACT_PACKAGE;
    artifacts[0].package_token = package_directory;
    artifacts[0].package_token_size = sizeof(package_directory) - 1U;
    artifacts[1].canonical_name = "mod";
    artifacts[1].data = module_source;
    artifacts[1].data_size = sizeof(module_source) - 1U;
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = artifacts;
    host.resolve_module = __test_heap_budget_resolve;
    host.release_module_artifact = __test_heap_budget_release;
    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    config.host = &host;
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    state.allocator_state = &allocator_state;
    state.vm = vm;
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *refuse = tinypy_native_function_new_items(vm, "refuse", 6U, __test_native_refuse_direct, 1U, 1U, &state, NULL);
    tinypy_value_t *refuse_key = tinypy_string_from_bytes(vm, "refuse", 6U);
    tinypy_dict_set(globals, refuse_key, refuse);
    tinypy_release(refuse_key);
    tinypy_release(refuse);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);

    allocator_state.fail_allocation_above = TINYPY_INTERNAL_POOL_SMALL_REQUEST;
    allocator_state.fail_allocation_below = TINYPY_INTERNAL_POOL_ARENA_SIZE;
    tinypy_value_t *package = tinypy_import_module(vm, "pkg", 3U, NULL, NULL, 0, &error);
    allocator_state.fail_allocation_above = 0U;
    TEST_CHECK(package == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    TEST_CHECK(__test_module_registered(vm, "pkg", 3U) == 0);
    package = tinypy_import_module(vm, "pkg", 3U, NULL, NULL, 0, &error);
    TEST_CHECK(package != NULL && error == NULL);
    tinypy_value_t *path = tinypy_module_get_value(package, "__path__", 8U);
    TEST_CHECK(path != NULL && tinypy_list_size(path) == 1U);
    tinypy_release(package);

    /* The next new entry grows sys.modules beyond the pooled sizes. */
    tinypy_value_t *modules = tinypy_vm_modules(vm);
    for (size_t pad = 0U; (TINYPY_DICT_OBJECT(modules)->fill + 1U) * 3U < (TINYPY_DICT_OBJECT(modules)->mask + 1U) * 2U; ++pad) {
        char name[32];
        int name_size = snprintf(name, sizeof(name), "padding%u", (unsigned int)pad);
        tinypy_value_t *key = tinypy_string_from_bytes(vm, name, (size_t)name_size);

        tinypy_dict_set(modules, key, tinypy_none_get(vm));
        tinypy_release(key);
    }
    allocator_state.fail_allocation_above = TINYPY_INTERNAL_POOL_SMALL_REQUEST;
    tinypy_value_t *module = tinypy_import_module(vm, "mod", 3U, NULL, NULL, 0, &error);
    allocator_state.fail_allocation_above = 0U;
    TEST_CHECK(module == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    TEST_CHECK(__test_module_registered(vm, "mod", 3U) == 0);
    TEST_CHECK(tinypy_module_get_value(vm->sys_module, "runs", 4U) == NULL);
    for (size_t attempt = 0U; attempt < 2U; ++attempt) {
        module = tinypy_import_module(vm, "mod", 3U, NULL, NULL, 0, &error);
        TEST_CHECK(module != NULL && error == NULL);
        tinypy_release(module);
    }
    tinypy_value_t *runs = tinypy_module_get_value(vm->sys_module, "runs", 4U);
    TEST_CHECK(runs != NULL && tinypy_integer_as_i64(runs) == 1);

    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* refuse_items(on) makes the host refuse the requests from an array of 100
   items up to a tuple of 100 items, but not the tuple of 101 items in which
   a call passes them with a receiver. */
static tinypy_value_t *__test_native_refuse_items(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_native_signal_state_t *state = (test_native_signal_state_t *)tinypy_native_function_user_data(function);
    size_t items_size = 100U * sizeof(tinypy_value_t *);

    (void)self;
    (void)count;
    (void)kwargs;
    (void)out_error;
    int32_t refuse = tinypy_truth(items[0], NULL);
    state->allocator_state->fail_allocation_above = refuse > 0 ? items_size - 1U : 0U;
    state->allocator_state->fail_allocation_below = refuse > 0 ? offsetof(tinypy_tuple_object_t, items) + items_size + 1U : 0U;
    tinypy_value_t *result = tinypy_none_get(state->vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Regular expression results sized by the subject, the group count or the
   arguments raise MemoryError when the host refuses their memory: the group
   slices of each subject kind, the marks of a match, groups(), regs,
   group() of many indices and the program _sre.compile copies. */
static int32_t __test_sre_allocation_refusal(void) {
    static const char source[] =
        "import _sre\n"
        "s = 'a' * 700 + 'b'\n"
        "repeated = _sre.compile('(a*)', 0, [21, 0, 29, 6, 0, 4294967295, 19, 97, 1, 21, 1, 1], 1)\n"
        "grouped = _sre.compile('a', 0, [19, 97, 1], 100)\n"
        "program = [19, 97] * 70 + [1]\n"
        "found = [repeated.match(subject) for subject in (s, unicode(s), bytearray(s))]\n"
        "many = grouped.match('a')\n"
        "indices = (many,) + (0,) * 100\n"
        "def probe(call, limit=refuse):\n"
        "    call()\n"
        "    limit(True)\n"
        "    try:\n"
        "        call()\n"
        "    except MemoryError:\n"
        "        return 'MemoryError'\n"
        "    finally:\n"
        "        limit(False)\n"
        "    return 'returned'\n"
        "calls = (lambda: found[0].group(1), lambda: found[1].group(1), lambda: found[2].group(1), lambda: repeated.findall(s),\n"
        "         lambda: repeated.split(s), lambda: grouped.match('a'), lambda: many.groups(), lambda: many.regs,\n"
        "         lambda: _sre.compile('a', 0, program))\n"
        "check([probe(call) for call in calls], ['MemoryError'] * len(calls))\n"
        "check(probe(lambda: type(many).group(*indices), refuse_items), 'MemoryError')\n"
        "check([len(item.group(1)) for item in found] + [len(many.regs)], [700, 700, 700, 101])\n";
    test_allocator_state_t allocator_state;
    test_native_signal_state_t state;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    state.allocator_state = &allocator_state;
    state.vm = vm;
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *refuse = tinypy_native_function_new_items(vm, "refuse", 6U, __test_native_refuse_direct, 1U, 1U, &state, NULL);
    tinypy_value_t *refuse_key = tinypy_string_from_bytes(vm, "refuse", 6U);
    tinypy_dict_set(globals, refuse_key, refuse);
    tinypy_release(refuse_key);
    tinypy_release(refuse);
    tinypy_value_t *refuse_items = tinypy_native_function_new_items(vm, "refuse_items", 12U, __test_native_refuse_items, 1U, 1U, &state, NULL);
    tinypy_value_t *refuse_items_key = tinypy_string_from_bytes(vm, "refuse_items", 12U);
    tinypy_dict_set(globals, refuse_items_key, refuse_items);
    tinypy_release(refuse_items_key);
    tinypy_release(refuse_items);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);

    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* refuse_size(size) makes the host refuse every request of that size until
   refuse_size(0); allocation_size(value) is the size its allocation took. */
static tinypy_value_t *__test_native_refuse_size(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_allocator_state_t *state = (test_allocator_state_t *)tinypy_native_function_user_data(function);
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)self;
    (void)count;
    (void)kwargs;
    (void)out_error;
    state->fail_allocation_size = (size_t)tinypy_integer_as_i64(items[0]);
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_allocation_size(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)self;
    (void)count;
    (void)kwargs;
    (void)out_error;
    size_t size = tinypy_internal_value_allocation_size(items[0]);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Computed-size results the host refuses raise MemoryError: copies and
   negations of longs, true division, parsed literals and the repr their
   ValueError repeats, a buffer added to unicode, the operands of the
   UnicodeDecodeError of '%' and format(), and the mangled names and tuple
   of __slots__. */
static int32_t __test_computed_result_refusal(void) {
    static const char source[] =
        "class L(long):\n"
        "    pass\n"
        "B = 1 << 40000\n"
        "NB = -B\n"
        "LB = L(B)\n"
        "H = B >> 1\n"
        "digits = '9' * 5000\n"
        "nul_digits = ''.join(('9' * 700, '\\x00'))\n"
        "u = u'\\xe9' * 10\n"
        "b = buffer('y' * 700)\n"
        "x = '\\xe9' * 700\n"
        "rest = '%s' + x\n"
        "spec = '{0:' + x + '}'\n"
        "unicode_spec = u'{0!r:' + u'\\xe9' * 350 + u'}'\n"
        "name = 'C' * 300\n"
        "slot = '__' + 'x' * 600\n"
        "slots = tuple('s%d' % index for index in range(100))\n"
        "def probe(size, call):\n"
        "    refuse_size(size)\n"
        "    try:\n"
        "        call()\n"
        "    except Exception as error:\n"
        "        return type(error).__name__\n"
        "    finally:\n"
        "        refuse_size(0)\n"
        "    return 'returned'\n"
        "long_size = allocation_size(B)\n"
        "parsed_size = allocation_size(int(digits))\n"
        "cases = ((long_size, lambda: -B), (long_size, lambda: ~B), (long_size, lambda: abs(NB)), (long_size, lambda: +LB),\n"
        "         (long_size, lambda: B.__neg__()), (long_size, lambda: B.__pos__()), (long_size, lambda: NB.__abs__()), (long_size, lambda: B.__invert__()),\n"
        "         (long_size, lambda: B.__truediv__(H)), (parsed_size, lambda: int(digits)), (parsed_size, lambda: long(digits)),\n"
        "         (allocation_size(repr(nul_digits)), lambda: int(nul_digits, 10)),\n"
        "         (allocation_size('y' * 700), lambda: u + b), (allocation_size(x), lambda: '%s%s' % (x, u'a')),\n"
        "         (allocation_size(''.join(('%s', x))), lambda: rest % u'a'), (allocation_size(x), lambda: spec.format(u'a')),\n"
        "         (allocation_size(u'\\xe9' * 350), lambda: unicode_spec.format(1)),\n"
        "         (1 + len(name) + len(slot), lambda: type(name, (object,), {'__slots__': (slot,)})),\n"
        "         (allocation_size(''.join(('_', name, slot))), lambda: type(name, (object,), {'__slots__': (slot,)})),\n"
        "         (allocation_size(slots), lambda: type('S', (object,), {'__slots__': slots})))\n"
        "check([probe(size, call) for size, call in cases], ['MemoryError'] * len(cases))\n"
        "check([probe(0, call) for size, call in cases[-3:]], ['returned'] * 3)\n";
    test_allocator_state_t allocator_state;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *refuse_size = tinypy_native_function_new_items(vm, "refuse_size", 11U, __test_native_refuse_size, 1U, 1U, &allocator_state, NULL);
    tinypy_value_t *refuse_size_key = tinypy_string_from_bytes(vm, "refuse_size", 11U);
    tinypy_dict_set(globals, refuse_size_key, refuse_size);
    tinypy_release(refuse_size_key);
    tinypy_release(refuse_size);
    tinypy_value_t *allocation_size = tinypy_native_function_new_items(vm, "allocation_size", 15U, __test_native_allocation_size, 1U, 1U, NULL, NULL);
    tinypy_value_t *allocation_size_key = tinypy_string_from_bytes(vm, "allocation_size", 15U);
    tinypy_dict_set(globals, allocation_size_key, allocation_size);
    tinypy_release(allocation_size_key);
    tinypy_release(allocation_size);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);
    TEST_CHECK(vm->allocated_bytes == allocator_state.outstanding_bytes);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_build_profile_create fails with TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED,
   leaving nothing allocated, when the host refuses any of its requests. The
   copy a compile keeps of a profile is VM heap: it counts against the
   budget, and a compile whose copy the host refuses raises MemoryError. */
static int32_t __test_build_profile_allocation_refusal(void) {
    static char text[16384];
    static const char source[] = "x = 1\n";
    tinypy_build_constant_t constant;
    tinypy_build_profile_error_t profile_error;
    tinypy_build_profile_t *profile = NULL;
    test_allocator_state_t state;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    (void)memset(text, 'p', sizeof(text));
    (void)memset(&constant, 0, sizeof(constant));
    constant.abi_version = TINYPY_COMPILER_ABI_VERSION;
    constant.struct_size = (uint32_t)sizeof(constant);
    constant.name = "__TEXT__";
    constant.name_size = 8U;
    tinypy_build_value_init(&constant.value, TINYPY_BUILD_VALUE_STRING);
    constant.value.data = text;
    constant.value.data_size = sizeof(text);
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    TEST_CHECK(tinypy_build_profile_create(&allocator, 0, &constant, 1U, NULL, &profile, &profile_error) == TINYPY_BUILD_PROFILE_OK);
    size_t request_count = state.allocation_calls;
    tinypy_build_profile_destroy(profile);
    for (size_t index = 1U; index <= request_count; ++index) {
        state.fail_allocation_call = state.allocation_calls + index;
        tinypy_build_profile_result_e result = tinypy_build_profile_create(&allocator, 0, &constant, 1U, NULL, &profile, &profile_error);
        state.fail_allocation_call = 0U;
        TEST_CHECK(result == TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED && profile == NULL && profile_error.code == TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED);
        TEST_CHECK(state.outstanding_allocations == 0U);
    }
    TEST_CHECK(tinypy_build_profile_create(&allocator, 0, &constant, 1U, NULL, &profile, &profile_error) == TINYPY_BUILD_PROFILE_OK);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    options.build_profile = profile;
    state.fail_allocation_above = sizeof(text);
    state.fail_allocation_below = TINYPY_INTERNAL_POOL_ARENA_SIZE;
    tinypy_value_t *code = tinypy_compile_source(vm, source, sizeof(source) - 1U, "profile.py", 10U, &options, &error);
    state.fail_allocation_above = 0U;
    TEST_CHECK(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    code = tinypy_compile_source(vm, source, sizeof(source) - 1U, "profile.py", 10U, &options, &error);
    TEST_CHECK(code != NULL && error == NULL);
    tinypy_build_profile_destroy(profile);
    TEST_CHECK(vm->allocated_bytes == state.outstanding_bytes);
    tinypy_release(code);
    TEST_CHECK(vm->allocated_bytes == state.outstanding_bytes);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* An error is a direct allocation of its VM, the message rendered into it
   included: it counts against the heap budget until the host releases it,
   and one the host still holds when the VM is destroyed stays readable and
   is released after it. */
static int32_t __test_error_heap_accounting(void) {
    static const char large_source[] = "raise ValueError('x' * 4096)\n";
    static const char kept_source[] = "raise KeyError('kept')\n";
    test_allocator_state_t state;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_error_t *kept = NULL;
    size_t size = 0U;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    TEST_CHECK(tinypy_exec_source(vm, large_source, sizeof(large_source) - 1U, "large.py", 8U, globals, NULL, &options, &error) == NULL);
    tinypy_vm_clear_error(vm);
    TEST_CHECK(error != NULL && vm->allocated_bytes == state.outstanding_bytes);
    const char *message = tinypy_error_message(error, &size);
    TEST_CHECK(size == 4096U && message[0] == 'x');
    TEST_CHECK(vm->allocated_bytes == state.outstanding_bytes);
    TEST_CHECK(tinypy_exec_source(vm, kept_source, sizeof(kept_source) - 1U, "kept.py", 7U, globals, NULL, &options, &kept) == NULL);
    tinypy_vm_clear_error(vm);
    tinypy_error_release(error);
    TEST_CHECK(kept != NULL && vm->allocated_bytes == state.outstanding_bytes);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    message = tinypy_error_message(kept, &size);
    TEST_CHECK(size == 6U && strcmp(message, "'kept'") == 0 && tinypy_error_kind(kept) == TINYPY_ERROR_KEY);
    tinypy_error_release(kept);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_allocation_watch_t {
    test_allocator_state_t state;
    size_t size;
    size_t call;
} test_allocation_watch_t;
//////////////////////////////////////////////////////////////////////////
/* Records the call that first requests the watched size. */
static void *__test_watch_allocate(void *user_data, size_t size, size_t alignment) {
    test_allocation_watch_t *watch = (test_allocation_watch_t *)user_data;
    void *memory = __test_allocate(&watch->state, size, alignment);

    if (watch->call == 0U && size == watch->size) {
        watch->call = watch->state.allocation_calls;
    }
    return memory;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_vm_create returns NULL, with nothing left allocated, when the host
   refuses the VM object or a request the bootstrap continues after, here
   the first table of a larger bootstrap dictionary. Refused requests do not
   count as allocated, so the heap accounting keeps matching the host. */
static int32_t __test_allocation_refusal(void) {
    test_allocation_watch_t watch;
    test_allocator_state_t state;

    (void)memset(&watch, 0, sizeof(watch));
    watch.size = 128U * sizeof(tinypy_dict_entry_t);
    tinypy_allocator_t watch_allocator = __test_make_allocator(&watch.state);
    watch_allocator.allocate = __test_watch_allocate;
    tinypy_vm_config_t watch_config = __test_make_config(&watch_allocator);
    tinypy_vm_t *watched = tinypy_vm_create(&watch_config);
    TEST_CHECK(watched != NULL && watch.call != 0U);
    tinypy_vm_destroy(watched);
    const size_t refused_calls[] = {1U, watch.call};
    for (size_t index = 0U; index < sizeof(refused_calls) / sizeof(refused_calls[0]); ++index) {
        (void)memset(&state, 0, sizeof(state));
        tinypy_allocator_t allocator = __test_make_allocator(&state);
        tinypy_vm_config_t config = __test_make_config(&allocator);

        state.fail_allocation_call = refused_calls[index];
        tinypy_vm_t *refused = tinypy_vm_create(&config);
        TEST_CHECK(refused == NULL);
        TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    }
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL && vm->allocated_bytes == state.outstanding_bytes);
    tinypy_value_t *dict = tinypy_dict_new(vm);
    tinypy_value_t *list = tinypy_list_from_items(vm, NULL, 0U);
    state.fail_allocation_above = TINYPY_INTERNAL_POOL_SMALL_REQUEST;
    state.fail_allocation_below = TINYPY_INTERNAL_POOL_ARENA_SIZE;
    for (int64_t index = 0; index < 200; ++index) {
        tinypy_value_t *item = tinypy_integer_from_i64(vm, index);

        tinypy_dict_set(dict, item, item);
        tinypy_list_append(list, item);
        tinypy_release(item);
    }
    state.fail_allocation_above = 0U;
    TEST_CHECK(tinypy_dict_size(dict) < 200U && tinypy_list_size(list) < 200U);
    TEST_CHECK(vm->allocated_bytes == state.outstanding_bytes);
    tinypy_release(list);
    tinypy_release(dict);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_error_drop_state_t {
    tinypy_error_t *kept;
    size_t dropped;
} test_error_drop_state_t;
//////////////////////////////////////////////////////////////////////////
/* drop() releases the error the host keeps, as a host releasing its errors
   from a callback does. */
static tinypy_value_t *__test_error_drop(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_error_drop_state_t *state = (test_error_drop_state_t *)tinypy_native_function_user_data(function);
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)self;
    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    if (state->kept != NULL) {
        tinypy_error_release(state->kept);
        state->kept = NULL;
        state->dropped += 1U;
    }
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Runs source in a module with drop() and keeps the error it fails with. */
static tinypy_vm_t *__test_error_drop_vm(const tinypy_vm_config_t *config, test_error_drop_state_t *state, const char *source, tinypy_value_t **out_module) {
    tinypy_vm_t *vm = tinypy_vm_create(config);
    tinypy_value_t *module = tinypy_module_new(vm, "app", 3U);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "app", 3U);
    tinypy_value_t *modules = tinypy_vm_modules(vm);
    tinypy_compile_options_t options;

    tinypy_dict_set(modules, key, module);
    tinypy_release(key);
    tinypy_value_t *builtins = tinypy_vm_builtins(vm);
    tinypy_module_add_value(module, "__builtins__", 12U, builtins);
    tinypy_value_t *drop = tinypy_native_function_new_items(vm, "drop", 4U, __test_error_drop, 0U, 0U, state, NULL);
    tinypy_module_add_value(module, "drop", 4U, drop);
    tinypy_release(drop);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *globals = tinypy_module_dict(module);
    tinypy_value_t *result = tinypy_exec_source(vm, source, strlen(source), "drop.py", 7U, globals, NULL, &options, &state->kept);
    if (result != NULL) {
        tinypy_release(result);
    }
    tinypy_vm_clear_error(vm);
    *out_module = module;
    return vm;
}
//////////////////////////////////////////////////////////////////////////
/* A host may release a diagnostic from code that reading it runs: from the
   str() that renders its message, when the host reads it or when
   tinypy_vm_destroy renders it, and from the release of the class that
   reading its kind drops. */
static int32_t __test_error_reentrant_release(void) {
    static const char render_source[] =
        "class Dropping(Exception):\n"
        "    def __str__(self):\n"
        "        drop()\n"
        "        return 'dropped'\n"
        "raise Dropping()\n";
    static const char kind_source[] =
        "class Guard(object):\n"
        "    def __del__(self):\n"
        "        drop()\n"
        "class Dropping(Exception):\n"
        "    guard = Guard()\n"
        "class Other(Exception):\n"
        "    pass\n"
        "caught = []\n"
        "def fail():\n"
        "    try:\n"
        "        raise Dropping()\n"
        "    except Dropping as error:\n"
        "        caught.append(error)\n"
        "        raise\n"
        "fail()\n";
    static const char forget_source[] =
        "import sys\n"
        "sys.exc_clear()\n"
        "caught[0].__class__ = Other\n"
        "del Dropping, Guard, caught[:], fail\n";
    test_allocator_state_t allocator_state;
    test_error_drop_state_t state;
    tinypy_value_t *module = NULL;
    size_t size = 1U;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = __test_error_drop_vm(&config, &state, render_source, &module);
    TEST_CHECK(state.kept != NULL);
    tinypy_release(module);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.kept == NULL && state.dropped == 1U);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);

    vm = __test_error_drop_vm(&config, &state, render_source, &module);
    TEST_CHECK(state.kept != NULL);
    const char *message = tinypy_error_message(state.kept, &size);
    TEST_CHECK(state.kept == NULL && state.dropped == 2U && size == 0U && message[0] == '\0');
    tinypy_release(module);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);

    vm = __test_error_drop_vm(&config, &state, kind_source, &module);
    TEST_CHECK(state.kept != NULL);
    tinypy_value_t *globals = tinypy_module_dict(module);
    TEST_CHECK(__test_native_contract_run(vm, globals, forget_source) == 0);
    TEST_CHECK(tinypy_error_kind(state.kept) == TINYPY_ERROR_RUNTIME);
    TEST_CHECK(state.kept == NULL && state.dropped == 3U);
    tinypy_release(module);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static size_t __test_error_list_size(const tinypy_error_t *error, tinypy_bool_t pending) {
    size_t size = 0U;

    for (; error != NULL; error = error->next) {
        if ((error->exception != NULL) != (pending != 0)) {
            return SIZE_MAX;
        }
        size += 1U;
    }
    return size;
}
//////////////////////////////////////////////////////////////////////////
/* The diagnostics whose message is unread are linked apart from the other
   errors, and reading or releasing one takes it off that list, so that
   tinypy_vm_destroy renders each diagnostic the host holds in one step. */
static int32_t __test_error_pending_diagnostics(void) {
    static const char source[] = "def fail(index):\n    raise ValueError(index)\n";
    static const char syntax_source[] = "x = (\n";
    tinypy_error_t *held[64];
    size_t held_count = sizeof(held) / sizeof(held[0]);
    test_allocator_state_t state;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_error_t *syntax_error = NULL;

    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "pending.py", 10U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "fail", 4U);
    tinypy_value_t *fail = tinypy_dict_get(globals, key);
    tinypy_release(key);
    TEST_CHECK(fail != NULL);
    for (size_t index = 0U; index < held_count; ++index) {
        tinypy_value_t *argument = tinypy_integer_from_i64(vm, (int64_t)index);
        tinypy_value_t *args = tinypy_tuple_from_items(vm, &argument, 1U);

        held[index] = NULL;
        result = tinypy_call(fail, args, NULL, &held[index]);
        tinypy_vm_clear_error(vm);
        tinypy_release(args);
        tinypy_release(argument);
        TEST_CHECK(result == NULL && held[index] != NULL);
    }
    TEST_CHECK(__test_error_list_size(vm->pending_diagnostics, TINYPY_TRUE) == held_count);
    TEST_CHECK(__test_error_list_size(vm->errors, TINYPY_FALSE) == 0U);
    TEST_CHECK(tinypy_compile_source(vm, syntax_source, sizeof(syntax_source) - 1U, "syntax.py", 9U, &options, &syntax_error) == NULL);
    tinypy_vm_clear_error(vm);
    TEST_CHECK(syntax_error != NULL);
    for (size_t index = 0U; index < held_count; index += 2U) {
        size_t size = 0U;
        const char *message = tinypy_error_message(held[index], &size);

        TEST_CHECK(size != 0U && (size_t)strtoul(message, NULL, 10) == index);
    }
    for (size_t index = 0U; index < held_count; index += 4U) {
        tinypy_error_release(held[index]);
        tinypy_error_release(held[index + 1U]);
        held[index] = NULL;
        held[index + 1U] = NULL;
    }
    TEST_CHECK(__test_error_list_size(vm->pending_diagnostics, TINYPY_TRUE) == held_count / 4U);
    TEST_CHECK(__test_error_list_size(vm->errors, TINYPY_FALSE) == held_count / 4U + 1U);
    TEST_CHECK(vm->allocated_bytes == state.outstanding_bytes);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    for (size_t index = 0U; index < held_count; ++index) {
        if (held[index] == NULL) {
            continue;
        }
        size_t size = 0U;
        const char *message = tinypy_error_message(held[index], &size);

        TEST_CHECK(size != 0U && (size_t)strtoul(message, NULL, 10) == index && tinypy_error_kind(held[index]) == TINYPY_ERROR_VALUE);
        tinypy_error_release(held[index]);
    }
    TEST_CHECK(tinypy_error_kind(syntax_error) == TINYPY_ERROR_SYNTAX);
    tinypy_error_release(syntax_error);
    TEST_CHECK(state.outstanding_allocations == 0U && state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_compile_pending_state_t {
    tinypy_vm_t *vm;
    size_t writes;
    size_t pending_writes;
} test_compile_pending_state_t;
//////////////////////////////////////////////////////////////////////////
static void __test_compile_pending_output(void *user_data, tinypy_output_channel_e channel, const void *bytes, size_t size) {
    test_compile_pending_state_t *state = (test_compile_pending_state_t *)user_data;

    (void)channel;
    (void)bytes;
    (void)size;
    state->writes += 1U;
    if (tinypy_vm_has_error(state->vm) != 0) {
        state->pending_writes += 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_compile_source and tinypy_preprocess_source reset the exception the
   host left pending, as the other top-level entry points do: a compile writes
   its SyntaxWarning without it and a success leaves none pending. */
static int32_t __test_compile_pending_reset(void) {
    static const char source[] = "def f():\n    x = 1\n    global x\n";
    test_allocator_state_t allocator_state;
    test_compile_pending_state_t state;
    tinypy_compile_options_t options;
    tinypy_host_t host;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = &state;
    host.emit_output = __test_compile_pending_output;
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    config.host = &host;
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    state.vm = vm;
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);

    tinypy_vm_raise_error(vm, TINYPY_ERROR_VALUE, "stale");
    tinypy_value_t *code = tinypy_compile_source(vm, source, sizeof(source) - 1U, "warn.py", 7U, &options, &error);
    TEST_CHECK(code != NULL && error == NULL && tinypy_vm_has_error(vm) == 0);
    TEST_CHECK(state.writes != 0U && state.pending_writes == 0U);
    tinypy_release(code);

    tinypy_vm_raise_error(vm, TINYPY_ERROR_VALUE, "stale");
    tinypy_preprocess_result_t *preprocessed = tinypy_preprocess_source(vm, source, sizeof(source) - 1U, "warn.py", 7U, &options, &error);
    TEST_CHECK(preprocessed != NULL && error == NULL && tinypy_vm_has_error(vm) == 0);
    tinypy_preprocess_result_destroy(preprocessed);

    state.writes = 0U;
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_vm_raise_error(vm, TINYPY_ERROR_VALUE, "stale");
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "warn.py", 7U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL && tinypy_vm_has_error(vm) == 0);
    TEST_CHECK(state.writes != 0U && state.pending_writes == 0U);
    tinypy_release(result);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_native_slot_state_t {
    tinypy_vm_t *vm;
    tinypy_native_type_spec_t reset_spec;
} test_native_slot_state_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_slot_iter(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    (void)out_error;
    *(int64_t *)payload = 0;
    tinypy_retain(instance);
    return instance;
}
//////////////////////////////////////////////////////////////////////////
/* The callbacks fail through the VM error alone, as pybind does. */
static tinypy_value_t *__test_native_slot_next(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    test_native_slot_state_t *state = (test_native_slot_state_t *)user_data;
    int64_t *index = (int64_t *)payload;

    (void)instance;
    (void)out_error;
    if (*index == 1) {
        tinypy_vm_raise_error(state->vm, TINYPY_ERROR_RUNTIME, "next failed");
        return NULL;
    }
    if (*index >= 3) {
        return NULL;
    }
    *index += 1;
    tinypy_value_t *result = tinypy_integer_from_i64(state->vm, *index - 1);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_slot_item(tinypy_value_t *instance, void *payload, tinypy_value_t *key, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(instance);
    int64_t index = tinypy_integer_as_i64(key);

    (void)payload;
    (void)out_error;
    if (user_data != NULL && index == 1) {
        tinypy_vm_raise_error(vm, TINYPY_ERROR_RUNTIME, "item failed");
        return NULL;
    }
    if (index < 0 || index >= 3) {
        tinypy_vm_raise_error(vm, TINYPY_ERROR_INDEX, "index out of range");
        return NULL;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, index * 10);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_slot_mapping(tinypy_value_t *instance, void *payload, tinypy_value_t *key, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(instance);

    (void)payload;
    (void)user_data;
    (void)out_error;
    if (tinypy_typeof(key) != TINYPY_VALUE_STRING) {
        tinypy_vm_raise_error(vm, TINYPY_ERROR_KEY, "names only");
        return NULL;
    }
    tinypy_value_t *result = tinypy_string_from_bytes(vm, "by-name", 7U);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __test_native_slot_length(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)payload;
    (void)out_error;
    if (user_data != NULL) {
        tinypy_vm_t *vm = tinypy_value_vm(instance);

        tinypy_vm_raise_error(vm, TINYPY_ERROR_RUNTIME, "length failed");
        return -1;
    }
    return 3;
}
//////////////////////////////////////////////////////////////////////////
/* A mapping length, which len() reads after a sequence length. */
static ptrdiff_t __test_native_slot_mapping_length(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)instance;
    (void)payload;
    (void)user_data;
    (void)out_error;
    return 7;
}
//////////////////////////////////////////////////////////////////////////
/* An iter callback that answers a list, which is no iterator. */
static tinypy_value_t *__test_native_slot_list_iter(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(instance);

    (void)payload;
    (void)user_data;
    (void)out_error;
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* A length callback that fails without reporting an error. */
static ptrdiff_t __test_native_slot_silent_length(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)instance;
    (void)payload;
    (void)user_data;
    (void)out_error;
    return -1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_slot_repr(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(instance);

    (void)payload;
    (void)user_data;
    (void)out_error;
    tinypy_value_t *result = tinypy_string_from_bytes(vm, "<shown>", 7U);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* reset(type) drops the repr callback of a type. */
static tinypy_value_t *__test_native_slot_reset(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    test_native_slot_state_t *state = (test_native_slot_state_t *)tinypy_native_function_user_data(function);

    (void)self;
    (void)count;
    (void)kwargs;
    tinypy_type_t *type = tinypy_value_as_type(items[0]);
    if (tinypy_native_type_update_spec(type, &state->reset_spec, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_none_get(state->vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_type_t *__test_native_slot_type(tinypy_value_t *globals, const char *name, const tinypy_native_type_spec_t *spec) {
    tinypy_vm_t *vm = tinypy_value_vm(globals);
    tinypy_type_t *type = tinypy_native_type_new(vm, name, strlen(name), NULL, 0U, NULL, spec, NULL);

    __test_native_contract_add_type(globals, name, type);
    return type;
}
//////////////////////////////////////////////////////////////////////////
/* Native slots report the failures their callbacks raise through the VM
   error alone, a length that fails without one reads as ValueError,
   sequence iteration reads sq_item before mp_subscript and
   accepts reversed(), len() reads sq_length before mp_length, an iter
   result must be an iterator, a slot whose callback a Python call removed
   during dispatch falls back to the absent slot, attribute lookups set a
   pending exception aside and bases must share the VM. */
static int32_t __test_native_slot_failures(void) {
    static const char source[] =
        "expect_error(RuntimeError, lambda: list(Stream()), 'next failed')\n"
        "expect_error(RuntimeError, lambda: [value for value in Failing()], 'item failed')\n"
        "expect_error(RuntimeError, lambda: 10 in Failing(), 'item failed')\n"
        "both = Both()\n"
        "check((both['name'], list(both), list(reversed(both)), 20 in both), ('by-name', [0, 10, 20], [20, 10, 0], True))\n"
        "import _weakref\n"
        "check((len(both), len(_weakref.proxy(both))), (3, 3))\n"
        "expect_error(TypeError, lambda: iter(Listing()), \"iter() returned non-iterator of type 'list'\")\n"
        "expect_error(TypeError, lambda: [value for value in Listing()], \"iter() returned non-iterator of type 'list'\")\n"
        "expect_error(RuntimeError, lambda: reversed(BadLength()), 'length failed')\n"
        "expect_error(RuntimeError, lambda: iter(BadLength()).__length_hint__(), 'length failed')\n"
        "expect_error(ValueError, lambda: reversed(Silent()), 'length slot returned a negative value')\n"
        "expect_error(ValueError, lambda: iter(Silent()).__length_hint__(), 'length slot returned a negative value')\n"
        "class Collider(str):\n"
        "    def __hash__(self):\n"
        "        return hash('__repr__')\n"
        "    def __eq__(self, other):\n"
        "        if armed:\n"
        "            del armed[:]\n"
        "            reset(Late)\n"
        "        return str.__eq__(self, other)\n"
        "armed = []\n"
        "Late = type('Late', (Shown,), {Collider('key'): 1})\n"
        "late = Late()\n"
        "armed.append(1)\n"
        "check(repr([late])[:28], '[<contract.Late object at 0x')\n"
        "class Dynamic(object):\n"
        "    def __getattr__(self, name):\n"
        "        return name\n"
        "dynamic = Dynamic()\n";
    test_allocator_state_t allocator_state;
    test_native_slot_state_t state;
    tinypy_native_type_spec_t spec;
    tinypy_error_t *error = NULL;
    size_t size;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    state.vm = vm;
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *name_key = tinypy_string_from_bytes(vm, "__name__", 8U);
    tinypy_value_t *module_name = tinypy_string_from_bytes(vm, "contract", 8U);
    tinypy_dict_set(globals, name_key, module_name);
    tinypy_release(module_name);
    tinypy_release(name_key);

    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(int64_t);
    spec.payload_alignment = _Alignof(int64_t);
    spec.user_data = &state;
    spec.iter = __test_native_slot_iter;
    spec.next = __test_native_slot_next;
    tinypy_type_t *stream = __test_native_slot_type(globals, "Stream", &spec);
    tinypy_native_type_spec_init(&spec);
    spec.user_data = &state;
    spec.sequence_get = __test_native_slot_item;
    tinypy_type_t *failing = __test_native_slot_type(globals, "Failing", &spec);
    spec.sequence_length = __test_native_slot_length;
    tinypy_type_t *bad_length = __test_native_slot_type(globals, "BadLength", &spec);
    spec.user_data = NULL;
    spec.sequence_length = __test_native_slot_silent_length;
    tinypy_type_t *silent = __test_native_slot_type(globals, "Silent", &spec);
    spec.sequence_length = __test_native_slot_length;
    spec.mapping_get = __test_native_slot_mapping;
    spec.mapping_length = __test_native_slot_mapping_length;
    tinypy_type_t *both = __test_native_slot_type(globals, "Both", &spec);
    tinypy_native_type_spec_init(&spec);
    spec.iter = __test_native_slot_list_iter;
    tinypy_type_t *listing = __test_native_slot_type(globals, "Listing", &spec);
    tinypy_native_type_spec_init(&state.reset_spec);
    spec = state.reset_spec;
    spec.repr = __test_native_slot_repr;
    tinypy_type_t *shown = __test_native_slot_type(globals, "Shown", &spec);
    TEST_CHECK(stream != NULL && failing != NULL && bad_length != NULL && silent != NULL && both != NULL && listing != NULL && shown != NULL);
    tinypy_value_t *reset = tinypy_native_function_new_items(vm, "reset", 5U, __test_native_slot_reset, 1U, 1U, &state, NULL);
    tinypy_value_t *reset_key = tinypy_string_from_bytes(vm, "reset", 5U);
    tinypy_dict_set(globals, reset_key, reset);
    tinypy_release(reset_key);
    tinypy_release(reset);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);
    TEST_CHECK(tinypy_vm_has_error(vm) == 0);

    /* An attribute lookup sets a pending exception aside: __getattr__ runs
       and a success keeps it, a missing attribute replaces it. */
    tinypy_value_t *dynamic_key = tinypy_string_from_bytes(vm, "dynamic", 7U);
    tinypy_value_t *dynamic = tinypy_dict_get_optional(globals, dynamic_key);
    tinypy_release(dynamic_key);
    TEST_CHECK(dynamic != NULL);
    tinypy_vm_raise_error(vm, TINYPY_ERROR_KEY, "pending");
    tinypy_value_t *pending = tinypy_vm_raised_exception(vm);
    tinypy_value_t *found = tinypy_object_get_attr(dynamic, "spam", 4U, &error);
    TEST_CHECK(found != NULL && error == NULL && tinypy_vm_raised_exception(vm) == pending);
    tinypy_release(found);
    TEST_CHECK(tinypy_object_has_attr(dynamic, "eggs", 4U) != 0 && tinypy_vm_raised_exception(vm) == pending);
    tinypy_value_t *both_value = tinypy_type_as_value(both);
    TEST_CHECK(tinypy_object_has_attr(both_value, "missing", 7U) == 0 && tinypy_vm_raised_exception(vm) == pending);
    TEST_CHECK(tinypy_object_get_attr(both_value, "missing", 7U, &error) == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_ATTRIBUTE && tinypy_vm_raised_exception(vm) != pending);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);

    /* A base from another VM is rejected. */
    tinypy_vm_t *other = tinypy_vm_create(&config);
    TEST_CHECK(other != NULL);
    tinypy_type_t *foreign = tinypy_native_type_new(other, "Foreign", 7U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(foreign != NULL && error == NULL);
    const tinypy_type_t *foreign_bases[1] = {foreign};
    TEST_CHECK(tinypy_native_type_new(vm, "Crossed", 7U, foreign_bases, 1U, NULL, &spec, &error) == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_TYPE);
    static const char foreign_message[] = "native type base 'Foreign' belongs to another VM";
    const char *message = tinypy_error_message(error, &size);
    TEST_CHECK(size == sizeof(foreign_message) - 1U && memcmp(message, foreign_message, size) == 0);
    tinypy_error_release(error);
    tinypy_vm_clear_error(vm);
    tinypy_value_t *foreign_value = tinypy_type_as_value(foreign);
    tinypy_release(foreign_value);
    tinypy_vm_destroy(other);

    tinypy_type_t *const releases[] = {stream, failing, bad_length, silent, both, listing, shown};
    for (size_t index = 0U; index < sizeof(releases) / sizeof(releases[0]); ++index) {
        tinypy_value_t *type_value = tinypy_type_as_value(releases[index]);

        tinypy_release(type_value);
    }
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_native_wrapper_state_t {
    tinypy_vm_t *vm;
    tinypy_value_t *log;
    const char *tag;
} test_native_wrapper_state_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_wrapper_pair(test_native_wrapper_state_t *state, const char *operation, tinypy_value_t *value) {
    tinypy_value_t *items[3] = {tinypy_string_from_bytes(state->vm, operation, strlen(operation)), tinypy_string_from_bytes(state->vm, state->tag, strlen(state->tag)), value};
    tinypy_value_t *pair = tinypy_tuple_from_items(state->vm, items, 3U);

    tinypy_release(items[1]);
    tinypy_release(items[0]);
    return pair;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_wrapper_get_attribute(tinypy_value_t *instance, void *payload, tinypy_value_t *name, void *user_data, tinypy_error_t **out_error) {
    test_native_wrapper_state_t *state = (test_native_wrapper_state_t *)user_data;

    (void)instance;
    (void)payload;
    (void)out_error;
    if (__test_native_attribute_name_equal(name, "x", 1U) == 0) {
        tinypy_vm_raise_error(state->vm, TINYPY_ERROR_ATTRIBUTE, "missing");
        return NULL;
    }
    tinypy_value_t *pair = __test_native_wrapper_pair(state, "attribute", name);
    return pair;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __test_native_wrapper_set_attribute(tinypy_value_t *instance, void *payload, tinypy_value_t *name, tinypy_value_t *value, void *user_data, tinypy_error_t **out_error) {
    test_native_wrapper_state_t *state = (test_native_wrapper_state_t *)user_data;

    (void)instance;
    (void)payload;
    (void)out_error;
    tinypy_value_t *entry = __test_native_wrapper_pair(state, value != NULL ? "set" : "delete", name);
    tinypy_list_append(state->log, entry);
    tinypy_release(entry);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_wrapper_get_item(tinypy_value_t *instance, void *payload, tinypy_value_t *key, void *user_data, tinypy_error_t **out_error) {
    (void)instance;
    (void)payload;
    (void)out_error;
    tinypy_value_t *pair = __test_native_wrapper_pair((test_native_wrapper_state_t *)user_data, "item", key);
    return pair;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_wrapper_add(tinypy_value_t *instance, void *payload, tinypy_value_t *other, void *user_data, tinypy_error_t **out_error) {
    (void)instance;
    (void)payload;
    (void)out_error;
    tinypy_value_t *pair = __test_native_wrapper_pair((test_native_wrapper_state_t *)user_data, "add", other);
    return pair;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_wrapper_call(tinypy_value_t *instance, void *payload, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)instance;
    (void)payload;
    (void)kwargs;
    (void)out_error;
    tinypy_value_t *pair = __test_native_wrapper_pair((test_native_wrapper_state_t *)user_data, "call", args);
    return pair;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_wrapper_repr(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    test_native_wrapper_state_t *state = (test_native_wrapper_state_t *)user_data;

    (void)instance;
    (void)payload;
    (void)out_error;
    tinypy_value_t *text = tinypy_string_from_bytes(state->vm, state->tag, strlen(state->tag));
    return text;
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __test_native_wrapper_length(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)instance;
    (void)payload;
    (void)user_data;
    (void)out_error;
    return 7;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_native_wrapper_contains(tinypy_value_t *instance, void *payload, tinypy_value_t *item, void *user_data, tinypy_error_t **out_error) {
    (void)instance;
    (void)payload;
    (void)item;
    (void)user_data;
    (void)out_error;
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_wrapper_kept(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)function;
    (void)args;
    (void)kwargs;
    (void)out_error;
    tinypy_value_t *kept = (tinypy_value_t *)user_data;
    tinypy_retain(kept);
    return kept;
}
//////////////////////////////////////////////////////////////////////////
/* A host native type publishes a slot wrapper for each callback of its
   spec, as add_operators does, which a Python subclass delegates to and
   which a namespace name or a spec update keeps in step, while instances
   keep dispatching their slots: a host native subtype stays with its own
   spec and a mapping callback does not make a type iterable. set_attribute
   is the tp_setattro of the type, which assignments of descriptors and
   deletions reach, and an instance without item callbacks reports the
   messages of a heap type. */
static int32_t __test_native_slot_wrappers(void) {
    static const char source[] =
        "def wrappers(type_value):\n"
        "    return sorted(name for name in type_value.__dict__ if name.startswith('__') and name not in ('__dict__', '__doc__', '__module__', '__weakref__'))\n"
        "class Sub(Native):\n"
        "    def __getattribute__(self, name): return super(Sub, self).__getattribute__(name)\n"
        "    def __setattr__(self, name, value): super(Sub, self).__setattr__(name, value)\n"
        "    def __delattr__(self, name): super(Sub, self).__delattr__(name)\n"
        "    def __repr__(self): return 'Sub+' + super(Sub, self).__repr__()\n"
        "    def __call__(self, *args): return super(Sub, self).__call__(*args)\n"
        "    def __getitem__(self, key): return super(Sub, self).__getitem__(key)\n"
        "    def __len__(self): return super(Sub, self).__len__()\n"
        "    def __add__(self, other): return super(Sub, self).__add__(other)\n"
        "    def __contains__(self, item): return super(Sub, self).__contains__(item)\n"
        "sub = Sub()\n"
        "native = Native()\n"
        "check(wrappers(Native), ['__add__', '__call__', '__contains__', '__delattr__', '__getattribute__', '__getitem__', '__len__', '__radd__', '__repr__', '__setattr__'])\n"
        "check((sub.x, repr(sub), sub(1), sub[3], len(sub), sub + 1, 1 in sub), (('attribute', 'native', 'x'), 'Sub+native', ('call', 'native', (1,)), ('item', 'native', 3), 7, ('add', 'native', 1), True))\n"
        "check((Native.__getattribute__(sub, u'x'), Native.__radd__(native, 2), Native.__getitem__(native, 'k')), (('attribute', 'native', 'x'), ('add', 'native', 2), ('item', 'native', 'k')))\n"
        "expect_error(AttributeError, lambda: Native.__getattribute__(native, 'y'), 'missing')\n"
        "expect_error(TypeError, lambda: Native.__getattribute__(native, 1), \"attribute name must be string, not 'int'\")\n"
        "expect_error(TypeError, lambda: Native.__getitem__(native), 'expected 1 arguments, got 0')\n"
        "expect_error(TypeError, lambda: Native.__setattr__(native, 'y'), ' expected 2 arguments, got 1')\n"
        "expect_error(TypeError, lambda: Native.__len__(native, k=1), \"wrapper __len__ doesn't take keyword arguments\")\n"
        "expect_error(TypeError, lambda: Native.__len__(1), \"descriptor '__len__' requires a 'Native' object but received a 'int'\")\n"
        "class Plain(object):\n"
        "    pass\n"
        "Native.p = property(lambda self: 1, lambda self, value: None)\n"
        "native.y = 1\n"
        "del native.y\n"
        "delattr(native, 'z')\n"
        "native.__class__ = Plain\n"
        "native.p = 2\n"
        "sub.w = 3\n"
        "del sub.w\n"
        "check(log, [('set', 'native', 'y'), ('delete', 'native', 'y'), ('delete', 'native', 'z'), ('set', 'native', '__class__'), ('set', 'native', 'p'), ('set', 'native', 'w'), ('delete', 'native', 'w')])\n"
        "expect_error(AttributeError, lambda: object.__setattr__(native, 'v', 1), \"'Native' object has no attribute 'v'\")\n"
        "lookup = Lookup()\n"
        "check(wrappers(Lookup), ['__getitem__', '__len__'])\n"
        "check((lookup[1], Lookup.__len__(lookup)), (('item', 'lookup', 1), 'kept'))\n"
        "expect_error(TypeError, lambda: list(lookup), \"'Lookup' object is not iterable\")\n"
        "expect_error(TypeError, lambda: reversed(lookup), 'argument to reversed() must be a sequence')\n"
        "derived = Derived()\n"
        "check(wrappers(Derived), [])\n"
        "check((Native.__getitem__(derived, 2), super(Derived, derived).__getitem__(3)), (('item', 'native', 2), ('item', 'native', 3)))\n"
        "check(repr(derived).startswith('<Derived object at 0x'), True)\n"
        "expect_error(TypeError, lambda: derived[0], \"'Derived' object does not support indexing\")\n"
        "expect_error(TypeError, lambda: derived['key'], \"'Derived' object has no attribute '__getitem__'\")\n"
        "def assign(): derived[0] = 1\n"
        "def delete(): del derived[0]\n"
        "expect_error(TypeError, assign, \"'Derived' object does not support item assignment\")\n"
        "expect_error(TypeError, delete, \"'Derived' object doesn't support item deletion\")\n"
        "expect_error(TypeError, lambda: derived(), \"'Derived' object is not callable\")\n"
        "added = super(Sub, sub).__radd__\n";
    static const char update_source[] =
        "check(wrappers(Native), ['__add__', '__call__', '__contains__', '__delattr__', '__getattribute__', '__getitem__', '__iadd__', '__len__', '__repr__', '__setattr__'])\n"
        "check((Native.__iadd__(native, 4), Native.__getitem__(native, 'key')), (('add', 'native', 4), ('item', 'native', 'key')))\n"
        "check(added(5), NotImplemented)\n"
        "check(Lookup.__dict__['__getitem__'], None)\n";
    test_allocator_state_t allocator_state;
    test_native_wrapper_state_t native_state;
    test_native_wrapper_state_t lookup_state;
    tinypy_native_type_spec_t spec;
    tinypy_native_type_spec_t lookup_spec;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *name_key = tinypy_string_from_bytes(vm, "__name__", 8U);
    tinypy_value_t *module_name = tinypy_string_from_bytes(vm, "contract", 8U);
    tinypy_dict_set(globals, name_key, module_name);
    tinypy_release(module_name);
    tinypy_release(name_key);
    tinypy_value_t *log = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *log_key = tinypy_string_from_bytes(vm, "log", 3U);
    tinypy_dict_set(globals, log_key, log);
    tinypy_release(log_key);
    native_state.vm = vm;
    native_state.log = log;
    native_state.tag = "native";
    lookup_state = native_state;
    lookup_state.tag = "lookup";

    tinypy_native_type_spec_init(&spec);
    spec.has_instance_dict = TINYPY_FALSE;
    spec.user_data = &native_state;
    spec.get_attribute = __test_native_wrapper_get_attribute;
    spec.set_attribute = __test_native_wrapper_set_attribute;
    spec.repr = __test_native_wrapper_repr;
    spec.call = __test_native_wrapper_call;
    spec.mapping_get = __test_native_wrapper_get_item;
    spec.mapping_length = __test_native_wrapper_length;
    spec.add = __test_native_wrapper_add;
    spec.reflected_add = __test_native_wrapper_add;
    spec.contains = __test_native_wrapper_contains;
    tinypy_type_t *native = tinypy_native_type_new(vm, "Native", 6U, NULL, 0U, NULL, &spec, &error);
    TEST_CHECK(native != NULL && error == NULL);
    tinypy_native_type_spec_t derived_spec;
    tinypy_native_type_spec_init(&derived_spec);
    derived_spec.has_instance_dict = TINYPY_FALSE;
    const tinypy_type_t *derived_bases[1] = {native};
    tinypy_type_t *derived = tinypy_native_type_new(vm, "Derived", 7U, derived_bases, 1U, NULL, &derived_spec, &error);
    TEST_CHECK(derived != NULL && error == NULL);
    /* A name the namespace defines keeps its value. */
    tinypy_value_t *namespace_dict = tinypy_dict_new(vm);
    tinypy_value_t *kept = tinypy_string_from_bytes(vm, "kept", 4U);
    tinypy_value_t *keeper = tinypy_native_function_new(vm, "__len__", 7U, __test_native_wrapper_kept, kept, NULL);
    tinypy_value_t *length_key = tinypy_string_from_bytes(vm, "__len__", 7U);
    tinypy_dict_set(namespace_dict, length_key, keeper);
    tinypy_release(length_key);
    tinypy_release(keeper);
    tinypy_native_type_spec_init(&lookup_spec);
    lookup_spec.user_data = &lookup_state;
    lookup_spec.mapping_get = __test_native_wrapper_get_item;
    lookup_spec.mapping_length = __test_native_wrapper_length;
    tinypy_type_t *lookup = tinypy_native_type_new(vm, "Lookup", 6U, NULL, 0U, namespace_dict, &lookup_spec, &error);
    TEST_CHECK(lookup != NULL && error == NULL);
    tinypy_release(namespace_dict);
    __test_native_contract_add_type(globals, "Native", native);
    __test_native_contract_add_type(globals, "Derived", derived);
    __test_native_contract_add_type(globals, "Lookup", lookup);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);

    /* An update publishes the wrappers of the callbacks it adds and withdraws
       those of the callbacks it removes, never replacing another value; a
       bound wrapper of a removed number callback returns NotImplemented. */
    spec.reflected_add = NULL;
    spec.inplace_add = __test_native_wrapper_add;
    TEST_CHECK(tinypy_native_type_update_spec(native, &spec, &error) != 0 && error == NULL);
    tinypy_value_t *none = tinypy_none_get(vm);
    tinypy_type_set_attr(lookup, "__getitem__", 11U, none);
    tinypy_release(none);
    TEST_CHECK(tinypy_native_type_update_spec(lookup, &lookup_spec, &error) != 0 && error == NULL);
    TEST_CHECK(__test_native_contract_run(vm, globals, update_source) == 0);

    tinypy_release(kept);
    tinypy_release(log);
    tinypy_type_t *releases[] = {lookup, derived, native};
    for (size_t index = 0U; index < sizeof(releases) / sizeof(releases[0]); ++index) {
        tinypy_value_t *type_value = tinypy_type_as_value(releases[index]);

        tinypy_release(type_value);
    }
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* A type change withdraws the versions of the type and of its subclasses.
   The builtin types derived from a builtin type are no registered
   subclasses and follow it all the same, and once the versions run out the
   lookups stay right without being cached. */
static int32_t __test_type_versions(void) {
    static const char source[] =
        "def read(value):\n"
        "    return value.probe\n"
        "class Base(object):\n"
        "    probe = 'base'\n"
        "class Child(Base):\n"
        "    pass\n"
        "child = Child()\n"
        "for index in range(3):\n"
        "    check((read(True), read(child)), (1, 'base'))\n";
    static const char changed[] =
        "for index in range(3):\n"
        "    check((read(True), read(5), read(child)), (2, 2, 'base'))\n"
        "Base.probe = 'changed'\n"
        "check(read(child), 'changed')\n";
    static const char exhausted[] =
        "Base.probe = 0\n"
        "class Late(Base):\n"
        "    probe = 0\n"
        "late = Late()\n"
        "for index in range(4):\n"
        "    check((read(late), read(child)), (index, index))\n"
        "    Late.probe = index + 1\n"
        "    Base.probe = index + 1\n";
    test_allocator_state_t allocator_state;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_type_t *integer_type = &vm->types[TINYPY_VALUE_INTEGER];
    tinypy_type_t *bool_type = &vm->types[TINYPY_VALUE_BOOL];
    tinypy_value_t *first = tinypy_integer_from_i64(vm, 1);
    tinypy_value_t *second = tinypy_integer_from_i64(vm, 2);

    tinypy_type_set_attr(integer_type, "probe", 5U, first);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);
    TEST_CHECK(tinypy_type_get_attr(bool_type, "probe", 5U) == first);
    TEST_CHECK(bool_type->version != 0U && integer_type->version != 0U);
    tinypy_type_set_attr(integer_type, "probe", 5U, second);
    TEST_CHECK(bool_type->version == 0U);
    TEST_CHECK(tinypy_type_get_attr(bool_type, "probe", 5U) == second);
    TEST_CHECK(__test_native_contract_run(vm, globals, changed) == 0);

    vm->type_version_next = UINT64_MAX;
    TEST_CHECK(__test_native_contract_run(vm, globals, exhausted) == 0);
    TEST_CHECK(vm->type_version_next == 0U);
    tinypy_release(second);
    tinypy_release(first);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_type_new without a metaclass takes the type of its first base,
   which the __slots__ of the namespace may release by reassigning the
   __class__ of that base; the new type keeps it. */
static int32_t __test_type_new_metaclass(void) {
    static const char source[] =
        "class OtherMeta(type):\n"
        "    pass\n"
        "class Slots(object):\n"
        "    def __iter__(self):\n"
        "        bases[0].__class__ = OtherMeta\n"
        "        return iter(())\n"
        "def make():\n"
        "    Winner = type('Winner', (type,), {})\n"
        "    return Winner('Base', (object,), {})\n"
        "bases = [make()]\n"
        "namespace = {'__slots__': Slots()}\n";
    static const char check_source[] =
        "check((type(created).__name__, type(bases[0]) is OtherMeta), ('Winner', True))\n";
    test_allocator_state_t allocator_state;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);
    tinypy_value_t *bases_key = tinypy_string_from_bytes(vm, "bases", 5U);
    tinypy_value_t *namespace_key = tinypy_string_from_bytes(vm, "namespace", 9U);
    tinypy_value_t *bases = tinypy_dict_get(globals, bases_key);
    const tinypy_type_t *base = tinypy_value_as_const_type(tinypy_list_get(bases, 0U));
    tinypy_value_t *namespace_dict = tinypy_dict_get(globals, namespace_key);
    tinypy_error_t *error = NULL;
    tinypy_type_t *created = tinypy_type_new(vm, "Created", 7U, &base, 1U, NULL, namespace_dict, &error);

    TEST_CHECK(created != NULL && error == NULL);
    __test_native_contract_add_type(globals, "created", created);
    tinypy_release(tinypy_type_as_value(created));
    TEST_CHECK(__test_native_contract_run(vm, globals, check_source) == 0);
    tinypy_release(namespace_key);
    tinypy_release(bases_key);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_set_attr_probe(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    (void)items;
    (void)kwargs;
    (void)out_error;
    tinypy_vm_t *vm = tinypy_value_vm(function);
    int64_t bound = self != NULL ? INT64_C(100) : INT64_C(0);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, bound + (int64_t)count);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_type_set_attr turns a fresh native function, as pybind registers
   its methods, into a method descriptor of the type, and stores one held
   elsewhere too, as the builtin len, a function of a module or a function the
   host keeps in a dictionary, as it is, as PyObject_SetAttrString does: it
   stays a plain function for every holder. A fresh __new__ stays a plain
   function too, which a class call passes the class first. */
static int32_t __test_type_set_attr_sharing(void) {
    static const char source[] =
        "class T(object):\n"
        "    pass\n"
        "class N(object):\n"
        "    pass\n";
    static const char check_source[] =
        "check(type(T.__dict__['fresh']).__name__, 'method_descriptor')\n"
        "check((type(N.__dict__['__new__']).__name__, N(1), N()), ('builtin_function_or_method', 2, 1))\n"
        "check(T().fresh(1), 101)\n"
        "check((T.size is len, len('abc'), T().size('ab')), (True, 3, 2))\n"
        "check((T.helper is host.helper, host.helper(), T().helper(1)), (True, 0, 1))\n"
        "check((T.shared is kept, kept(1, 2), T().shared(1, 2)), (True, 2, 2))\n";
    test_allocator_state_t allocator_state;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);
    tinypy_value_t *type_value = __test_global(globals, "T");
    tinypy_type_t *type = tinypy_value_as_type(type_value);

    tinypy_value_t *fresh = tinypy_native_function_new_items_keywords(vm, "fresh", 5U, __test_set_attr_probe, NULL, NULL);
    tinypy_type_set_attr(type, "fresh", 5U, fresh);
    tinypy_release(fresh);

    tinypy_type_t *constructed = tinypy_value_as_type(__test_global(globals, "N"));
    tinypy_value_t *constructor = tinypy_native_function_new_items_keywords(vm, "__new__", 7U, __test_set_attr_probe, NULL, NULL);
    tinypy_type_set_attr(constructed, "__new__", 7U, constructor);
    tinypy_release(constructor);

    tinypy_value_t *builtins = tinypy_vm_builtins(vm);
    tinypy_value_t *len_key = tinypy_string_from_bytes(vm, "len", 3U);
    tinypy_value_t *len_function = tinypy_dict_get(builtins, len_key);
    tinypy_type_set_attr(type, "size", 4U, len_function);
    tinypy_release(len_key);

    tinypy_value_t *module = tinypy_module_new(vm, "host", 4U);
    tinypy_value_t *helper = tinypy_native_function_new_items_keywords(vm, "helper", 6U, __test_set_attr_probe, NULL, NULL);
    tinypy_module_add_value(module, "helper", 6U, helper);
    tinypy_release(helper);
    tinypy_value_t *published = tinypy_module_get_value(module, "helper", 6U);
    tinypy_type_set_attr(type, "helper", 6U, published);
    tinypy_value_t *module_key = tinypy_string_from_bytes(vm, "host", 4U);
    tinypy_dict_set(globals, module_key, module);
    tinypy_release(module_key);
    tinypy_release(module);

    tinypy_value_t *shared = tinypy_native_function_new_items_keywords(vm, "shared", 6U, __test_set_attr_probe, NULL, NULL);
    tinypy_value_t *kept_key = tinypy_string_from_bytes(vm, "kept", 4U);
    tinypy_dict_set(globals, kept_key, shared);
    tinypy_type_set_attr(type, "shared", 6U, shared);
    tinypy_release(kept_key);
    tinypy_release(shared);

    TEST_CHECK(__test_native_contract_run(vm, globals, check_source) == 0);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_error_message_is(const tinypy_error_t *error, const char *expected) {
    size_t size = 0U;
    const char *message = error != NULL ? tinypy_error_message(error, &size) : "";

    int32_t equal = size == strlen(expected) && memcmp(message, expected, size) == 0 ? 1 : 0;
    return equal;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_generator_throw takes its arguments as tinypy_exception_raise does
   and leaves the generator suspended when they are invalid, and a NULL value
   sends None. Reading the locals of a frame borrowed from a generator runs
   the finalizer of a stale value, which finishes the generator: nothing
   holds the frame by then, so there are no locals to lend. */
static int32_t __test_generator_embedding(void) {
    static const char source[] =
        "thrown = []\n"
        "def catching():\n"
        "    while True:\n"
        "        try:\n"
        "            yield\n"
        "        except KeyError as error:\n"
        "            thrown.append(type(error).__name__)\n"
        "def plain():\n"
        "    yield\n"
        "class Closer(object):\n"
        "    def __del__(self):\n"
        "        closing.close()\n"
        "def closed():\n"
        "    value = Closer()\n"
        "    yield\n"
        "    value = 'rebound'\n"
        "    yield\n"
        "caught = catching()\n"
        "next(caught)\n"
        "raising = plain()\n"
        "next(raising)\n";
    static const char suspend[] =
        "closing = closed()\n"
        "next(closing)\n"
        "closing.gi_frame.f_locals\n"
        "next(closing)\n";
    test_allocator_state_t allocator_state;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);
    tinypy_value_t *caught = __test_global(globals, "caught");
    tinypy_value_t *raising = __test_global(globals, "raising");
    tinypy_value_t *builtins = tinypy_vm_builtins(vm);
    tinypy_value_t *key_error = __test_global(builtins, "KeyError");
    tinypy_value_t *none = tinypy_none_get(vm);
    tinypy_value_t *number = tinypy_integer_from_i64(vm, 7);

    tinypy_value_t *result = tinypy_generator_throw(caught, key_error, none, &error);
    TEST_CHECK(result == none && error == NULL);
    tinypy_release(result);
    TEST_CHECK(tinypy_generator_throw(caught, key_error, number, &error) == NULL);
    TEST_CHECK(__test_error_message_is(error, "raise traceback must be a traceback or None") != 0);
    tinypy_error_release(error);
    tinypy_vm_clear_error(vm);
    error = NULL;
    TEST_CHECK(tinypy_generator_throw(caught, number, NULL, &error) == NULL);
    TEST_CHECK(__test_error_message_is(error, "exceptions must be old-style classes or derived from BaseException") != 0);
    tinypy_error_release(error);
    tinypy_vm_clear_error(vm);
    error = NULL;
    result = tinypy_generator_send(caught, NULL, &error);
    TEST_CHECK(result == none && error == NULL);
    tinypy_release(result);
    TEST_CHECK(__test_native_contract_run(vm, globals, "check(thrown, ['KeyError'])\n") == 0);

    TEST_CHECK(tinypy_generator_throw(raising, key_error, none, &error) == NULL);
    TEST_CHECK(error != NULL && tinypy_generator_finished(raising) != 0);
    for (tinypy_value_t *entry = tinypy_vm_raised_traceback(vm); entry != NULL; entry = tinypy_traceback_next(entry)) {
        TEST_CHECK(tinypy_typeof(entry) == TINYPY_VALUE_TRACEBACK);
    }
    tinypy_error_release(error);
    tinypy_vm_clear_error(vm);

    TEST_CHECK(__test_native_contract_run(vm, globals, suspend) == 0);
    tinypy_value_t *closing = __test_global(globals, "closing");
    tinypy_value_t *frame = tinypy_generator_frame(closing);
    TEST_CHECK(tinypy_frame_locals(frame) == NULL);
    TEST_CHECK(tinypy_generator_finished(closing) != 0);
#if defined(TINYPY_DEBUGGER)
    TEST_CHECK(__test_native_contract_run(vm, globals, suspend) == 0);
    closing = __test_global(globals, "closing");
    frame = tinypy_generator_frame(closing);
    TEST_CHECK(tinypy_debugger_frame_get(frame, TINYPY_DEBUGGER_SCOPE_LOCALS, "absent", 6U) == NULL);
    TEST_CHECK(tinypy_generator_finished(closing) != 0);
#endif
    tinypy_release(number);
    tinypy_release(none);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
#if defined(TINYPY_DEBUGGER)
typedef struct test_debugger_slot_state_t {
    tinypy_bool_t rebind;
    tinypy_bool_t done;
} test_debugger_slot_state_t;
//////////////////////////////////////////////////////////////////////////
static void __test_debugger_slot_event(void *user_data, const tinypy_debugger_event_t *event) {
    test_debugger_slot_state_t *state = (test_debugger_slot_state_t *)user_data;

    if (state->done != 0 || event->event != TINYPY_DEBUGGER_EVENT_LINE) {
        return;
    }
    tinypy_value_t *armed = tinypy_debugger_frame_get(event->frame, TINYPY_DEBUGGER_SCOPE_LOCALS, "armed", 5U);
    if (armed == NULL) {
        return;
    }
    state->done = TINYPY_TRUE;
    if (state->rebind != 0) {
        (void)tinypy_debugger_frame_set(event->frame, TINYPY_DEBUGGER_SCOPE_LOCALS, "x", 1U, armed);
        return;
    }
    (void)tinypy_debugger_frame_delete(event->frame, TINYPY_DEBUGGER_SCOPE_LOCALS, "x", 1U);
}
//////////////////////////////////////////////////////////////////////////
/* The debugger unbinds or rebinds a fast local before it releases the old
   value, as DELETE_FAST and STORE_FAST do: a finalizer that the release runs
   and that reads the frame's locals finds the variable already gone or
   rebound. */
static int32_t __test_debugger_frame_slots(void) {
    static const char source[] =
        "import sys\n"
        "seen = []\n"
        "class Dying(object):\n"
        "    def __del__(self):\n"
        "        seen.append(sys._getframe(1).f_locals.get('x', 'unbound'))\n"
        "def run():\n"
        "    x = [Dying()]\n"
        "    armed = 'rebound'\n"
        "    return 'done'\n"
        "check(run(), 'done')\n";
    static const char *const expected[] = {"check(seen, ['unbound'])\n", "check(seen, ['rebound'])\n"};

    for (size_t index = 0U; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        test_allocator_state_t allocator_state;
        test_debugger_slot_state_t slot_state = {index != 0U ? TINYPY_TRUE : TINYPY_FALSE, TINYPY_FALSE};

        (void)memset(&allocator_state, 0, sizeof(allocator_state));
        tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
        tinypy_vm_config_t config = __test_make_config(&allocator);
        tinypy_vm_t *vm = tinypy_vm_create(&config);
        TEST_CHECK(vm != NULL);
        tinypy_debugger_t debugger = {TINYPY_ABI_VERSION, (uint32_t)sizeof(tinypy_debugger_t), &slot_state, __test_debugger_slot_event};
        TEST_CHECK(tinypy_debugger_set(vm, &debugger) != 0);
        tinypy_value_t *globals = tinypy_dict_new(vm);
        TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);
        TEST_CHECK(slot_state.done != 0);
        TEST_CHECK(__test_native_contract_run(vm, globals, expected[index]) == 0);
        tinypy_dict_clear(globals);
        tinypy_release(globals);
        tinypy_vm_destroy(vm);
        TEST_CHECK(allocator_state.outstanding_allocations == 0U);
        TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_debugger_mapping_state_t {
    size_t lines;
    size_t found;
    size_t changed;
} test_debugger_mapping_state_t;
//////////////////////////////////////////////////////////////////////////
static void __test_debugger_mapping_event(void *user_data, const tinypy_debugger_event_t *event) {
    test_debugger_mapping_state_t *state = (test_debugger_mapping_state_t *)user_data;
    tinypy_vm_t *vm = tinypy_value_vm(event->frame);

    if (event->event != TINYPY_DEBUGGER_EVENT_LINE) {
        return;
    }
    tinypy_value_t *inner = tinypy_debugger_frame_get(event->frame, TINYPY_DEBUGGER_SCOPE_GLOBALS, "inner", 5U);
    if (inner == NULL) {
        return;
    }
    state->lines += 1U;
    if (tinypy_debugger_frame_get(event->frame, TINYPY_DEBUGGER_SCOPE_LOCALS, "a", 1U) != NULL) {
        state->found += 1U;
    }
    tinypy_value_t *none = tinypy_none_get(vm);
    if (tinypy_debugger_frame_set(event->frame, TINYPY_DEBUGGER_SCOPE_LOCALS, "debugger", 8U, none) != 0) {
        state->changed += 1U;
    }
    tinypy_release(none);
    if (tinypy_debugger_frame_delete(event->frame, TINYPY_DEBUGGER_SCOPE_LOCALS, "a", 1U) != 0) {
        state->changed += 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
/* Code that exec runs with a mapping other than a dict as its locals keeps
   them from the debugger, which reads its globals still. */
static int32_t __test_debugger_mapping_locals(void) {
    static const char source[] =
        "class Mapping(object):\n"
        "    def __init__(self):\n"
        "        self.items = {}\n"
        "    def __getitem__(self, key):\n"
        "        return self.items[key]\n"
        "    def __setitem__(self, key, value):\n"
        "        self.items[key] = value\n"
        "    def __delitem__(self, key):\n"
        "        del self.items[key]\n"
        "mapping = Mapping()\n"
        "exec 'a = 1\\nb = a + 1\\n' in {'inner': True}, mapping\n"
        "check(sorted(mapping.items.items()), [('a', 1), ('b', 2)])\n";
    test_allocator_state_t allocator_state;
    test_debugger_mapping_state_t state;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_debugger_t debugger = {TINYPY_ABI_VERSION, (uint32_t)sizeof(tinypy_debugger_t), &state, __test_debugger_mapping_event};
    TEST_CHECK(tinypy_debugger_set(vm, &debugger) != 0);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    TEST_CHECK(__test_native_contract_run(vm, globals, source) == 0);
    TEST_CHECK(state.lines == 2U && state.found == 0U && state.changed == 0U);
    TEST_CHECK(tinypy_debugger_set(vm, NULL) != 0);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_debugger_events_state_t {
    char events[128];
    size_t size;
} test_debugger_events_state_t;
//////////////////////////////////////////////////////////////////////////
/* Records each event but LINE of the functions of the traced source as its
   kind and the initial of the function. */
static void __test_debugger_events_record(void *user_data, const tinypy_debugger_event_t *event) {
    static const char kinds[] = "LCRE";
    test_debugger_events_state_t *state = (test_debugger_events_state_t *)user_data;

    if (event->event == TINYPY_DEBUGGER_EVENT_LINE || state->size + 2U >= sizeof(state->events)) {
        return;
    }
    tinypy_value_t *code = tinypy_frame_code(event->frame);
    tinypy_value_t *name = tinypy_code_name(code);
    size_t name_size = 0U;
    const char *name_bytes = (const char *)tinypy_string_view(name, &name_size);
    if (name_size == 0U || name_bytes[0] == '<') {
        return;
    }
    state->events[state->size] = kinds[(size_t)event->event - 1U];
    state->events[state->size + 1U] = name_bytes[0];
    state->size += 2U;
}
//////////////////////////////////////////////////////////////////////////
/* As the trace function of CPython, the debugger reports a RETURN for every
   CALL, also for a frame left by yield or by an exception, and an EXCEPTION
   for an exception thrown into a generator. */
static int32_t __test_debugger_return_events(void) {
    static const char source[] =
        "def gen():\n"
        "    yield 1\n"
        "    yield 2\n"
        "def raiser():\n"
        "    raise KeyError('k')\n"
        "def caller():\n"
        "    try:\n"
        "        raiser()\n"
        "    except KeyError:\n"
        "        pass\n"
        "    for item in gen():\n"
        "        pass\n"
        "    closing = gen()\n"
        "    next(closing)\n"
        "    closing.close()\n"
        "    thrown = gen()\n"
        "    next(thrown)\n"
        "    try:\n"
        "        thrown.throw(ValueError('v'))\n"
        "    except ValueError:\n"
        "        pass\n"
        "caller()\n";
    static const char expected[] = "CcCrErRrEcCgRgCgRgCgRgCgRgCgEgRgCgRgCgEgRgEcRc";
    test_allocator_state_t allocator_state;
    test_debugger_events_state_t state;
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_debugger_t debugger = {TINYPY_ABI_VERSION, (uint32_t)sizeof(tinypy_debugger_t), &state, __test_debugger_events_record};
    TEST_CHECK(tinypy_debugger_set(vm, &debugger) != 0);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "traced.py", 9U, globals, NULL, &options, &error);
    TEST_CHECK(result != NULL && error == NULL);
    tinypy_release(result);
    TEST_CHECK(tinypy_debugger_set(vm, NULL) != 0);
    TEST_CHECK(state.size == sizeof(expected) - 1U && memcmp(state.events, expected, state.size) == 0);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
typedef struct test_debugger_forward_state_t {
    tinypy_value_t *globals;
    tinypy_bool_t interrupt;
    tinypy_bool_t busy;
    size_t failures;
} test_debugger_forward_state_t;
//////////////////////////////////////////////////////////////////////////
/* Calls the function tracer of the globals with the arguments of a trace
   function of CPython: (event, frame, exception, traceback, exception
   type). As in CPython, the code that it runs is not traced. */
static void __test_debugger_forward_event(void *user_data, const tinypy_debugger_event_t *event) {
    test_debugger_forward_state_t *state = (test_debugger_forward_state_t *)user_data;
    tinypy_value_t *tracer = __test_global(state->globals, "tracer");

    if (state->busy != 0 || tracer == NULL) {
        return;
    }
    tinypy_vm_t *vm = tinypy_value_vm(event->frame);
    tinypy_value_t *none = tinypy_none_get(vm);
    tinypy_value_t *kind = tinypy_integer_from_i64(vm, (int64_t)event->event);
    tinypy_bool_t typed = event->struct_size >= offsetof(tinypy_debugger_event_t, exception_type) + sizeof(event->exception_type) ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_value_t *exception_type = typed != 0 && event->exception_type != NULL ? event->exception_type : none;
    tinypy_value_t *items[] = {kind, event->frame, event->exception != NULL ? event->exception : none, event->traceback != NULL ? event->traceback : none, exception_type};
    tinypy_value_t *args = tinypy_tuple_from_items(vm, items, sizeof(items) / sizeof(items[0]));

    tinypy_retain(tracer);
    state->busy = TINYPY_TRUE;
    tinypy_value_t *result = tinypy_call(tracer, args, NULL, NULL);
    state->busy = TINYPY_FALSE;
    if (result == NULL) {
        state->failures += 1U;
        tinypy_vm_clear_error(vm);
    }
    else {
        tinypy_release(result);
    }
    tinypy_release(tracer);
    tinypy_release(args);
    tinypy_release(kind);
    tinypy_release(none);
}
//////////////////////////////////////////////////////////////////////////
/* An interrupt that arm_interrupt requests is reported once. */
static tinypy_bool_t __test_debugger_poll_interrupt(void *user_data) {
    test_debugger_forward_state_t *state = (test_debugger_forward_state_t *)user_data;
    tinypy_bool_t interrupt = state->interrupt;

    state->interrupt = TINYPY_FALSE;
    return interrupt;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_debugger_arm_interrupt(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    test_debugger_forward_state_t *state = (test_debugger_forward_state_t *)user_data;
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)args;
    (void)kwargs;
    (void)out_error;
    state->interrupt = TINYPY_TRUE;
    tinypy_value_t *none = tinypy_none_get(vm);
    return none;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_debugger_sync_frame(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)kwargs;
    (void)user_data;
    (void)out_error;
    tinypy_debugger_frame_sync(tinypy_tuple_get(args, 0U));
    tinypy_value_t *none = tinypy_none_get(vm);
    return none;
}
//////////////////////////////////////////////////////////////////////////
/* get_local(frame, name) gives what tinypy_debugger_frame_get finds among
   the locals of frame, None when nothing. */
static tinypy_value_t *__test_debugger_get_local(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(function);
    size_t name_size = 0U;
    const char *name = (const char *)tinypy_string_view(tinypy_tuple_get(args, 1U), &name_size);

    (void)kwargs;
    (void)user_data;
    (void)out_error;
    tinypy_value_t *value = tinypy_debugger_frame_get(tinypy_tuple_get(args, 0U), TINYPY_DEBUGGER_SCOPE_LOCALS, name, name_size);
    if (value == NULL) {
        tinypy_value_t *none = tinypy_none_get(vm);
        return none;
    }
    tinypy_retain(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
/* set_local(frame, name, value) */
static tinypy_value_t *__test_debugger_set_local(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(function);
    size_t name_size = 0U;
    const char *name = (const char *)tinypy_string_view(tinypy_tuple_get(args, 1U), &name_size);

    (void)kwargs;
    (void)user_data;
    (void)out_error;
    tinypy_bool_t stored = tinypy_debugger_frame_set(tinypy_tuple_get(args, 0U), TINYPY_DEBUGGER_SCOPE_LOCALS, name, name_size, tinypy_tuple_get(args, 2U));
    tinypy_value_t *result = tinypy_bool_from_i32(vm, stored);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* delete_local(frame, name) */
static tinypy_value_t *__test_debugger_delete_local(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(function);
    size_t name_size = 0U;
    const char *name = (const char *)tinypy_string_view(tinypy_tuple_get(args, 1U), &name_size);

    (void)kwargs;
    (void)user_data;
    (void)out_error;
    tinypy_bool_t deleted = tinypy_debugger_frame_delete(tinypy_tuple_get(args, 0U), TINYPY_DEBUGGER_SCOPE_LOCALS, name, name_size);
    tinypy_value_t *result = tinypy_bool_from_i32(vm, deleted);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_debugger_remove(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_value_vm(function);

    (void)args;
    (void)kwargs;
    (void)user_data;
    (void)out_error;
    tinypy_bool_t removed = tinypy_debugger_set(vm, NULL);
    tinypy_value_t *result = tinypy_bool_from_i32(vm, removed);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __test_debugger_add_function(tinypy_value_t *globals, const char *name, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_vm_t *vm = tinypy_value_vm(globals);
    tinypy_value_t *function = tinypy_native_function_new(vm, name, strlen(name), callback, user_data, NULL);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, strlen(name));

    tinypy_dict_set(globals, key, function);
    tinypy_release(key);
    tinypy_release(function);
}
//////////////////////////////////////////////////////////////////////////
/* Runs each source as an execution of its own, whose checks see the events
   through tracer, in a VM whose host polls for the interrupt that
   arm_interrupt requests. */
static int32_t __test_debugger_forward_run_sources(const char *const *sources, size_t source_count) {
    test_allocator_state_t allocator_state;
    test_debugger_forward_state_t state;
    tinypy_host_t host;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&state, 0, sizeof(state));
    tinypy_allocator_t allocator = __test_make_allocator(&allocator_state);
    tinypy_vm_config_t config = __test_make_config(&allocator);
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = &state;
    host.poll_interrupt = __test_debugger_poll_interrupt;
    config.host = &host;
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    state.globals = globals;
    __test_debugger_add_function(globals, "arm_interrupt", __test_debugger_arm_interrupt, &state);
    __test_debugger_add_function(globals, "sync_frame", __test_debugger_sync_frame, NULL);
    __test_debugger_add_function(globals, "get_local", __test_debugger_get_local, NULL);
    __test_debugger_add_function(globals, "set_local", __test_debugger_set_local, NULL);
    __test_debugger_add_function(globals, "delete_local", __test_debugger_delete_local, NULL);
    __test_debugger_add_function(globals, "remove_debugger", __test_debugger_remove, NULL);
    tinypy_debugger_t debugger = {TINYPY_ABI_VERSION, (uint32_t)sizeof(tinypy_debugger_t), &state, __test_debugger_forward_event};
    TEST_CHECK(tinypy_debugger_set(vm, &debugger) != 0);
    for (size_t index = 0U; index < source_count; ++index) {
        TEST_CHECK(__test_native_contract_run(vm, globals, sources[index]) == 0);
    }
    TEST_CHECK(tinypy_debugger_set(vm, NULL) != 0);
    TEST_CHECK(state.failures == 0U);
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    TEST_CHECK(allocator_state.outstanding_allocations == 0U);
    TEST_CHECK(allocator_state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_debugger_forward_run(const char *source) {
    int32_t result = __test_debugger_forward_run_sources(&source, 1U);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* As maybe_call_line_trace, LINE marks the first instruction of a line
   and a backward jump: none for the lines that a jump passes through or
   that a generator resumes in, one for each iteration of a loop on one
   line. The expected events are those of CPython. */
static int32_t __test_debugger_line_events(void) {
    static const char source[] =
        "events = []\n"
        "def tracer(event, frame, exception, traceback, exception_type):\n"
        "    name = frame.f_code.co_name\n"
        "    if event == 1 and name.startswith('s_'):\n"
        "        events.append((name, frame.f_lineno - frame.f_code.co_firstlineno))\n"
        "def s_except():\n"
        "    try:\n"
        "        raise ValueError\n"
        "    except KeyError:\n"
        "        x = 'never runs'\n"
        "def s_continue():\n"
        "    for i in range(2):\n"
        "        if i < 5:\n"
        "            continue\n"
        "        y = 'never runs'\n"
        "def s_empty_for():\n"
        "    for i in []:\n"
        "        pass\n"
        "    else:\n"
        "        e = 1\n"
        "def s_one_line():\n"
        "    n = 0\n"
        "    for i in range(3): n += i\n"
        "    return n\n"
        "def s_generator():\n"
        "    x = yield 1\n"
        "    yield x\n"
        "for f in (s_except, s_continue, s_empty_for, s_one_line):\n"
        "    try:\n"
        "        f()\n"
        "    except ValueError:\n"
        "        pass\n"
        "list(s_generator())\n"
        "check(events, [('s_except', 1), ('s_except', 2), ('s_except', 3),\n"
        "    ('s_continue', 1), ('s_continue', 2), ('s_continue', 3), ('s_continue', 1), ('s_continue', 2), ('s_continue', 3), ('s_continue', 1),\n"
        "    ('s_empty_for', 1), ('s_empty_for', 4),\n"
        "    ('s_one_line', 1), ('s_one_line', 2), ('s_one_line', 2), ('s_one_line', 2), ('s_one_line', 2), ('s_one_line', 3),\n"
        "    ('s_generator', 1), ('s_generator', 2)])\n";

    int32_t result = __test_debugger_forward_run(source);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* As call_exc_trace, EXCEPTION passes the type, the value as raised and a
   traceback that holds the frame already. */
static int32_t __test_debugger_exception_events(void) {
    static const char source[] =
        "import sys\n"
        "records = []\n"
        "def tracer(event, frame, exception, traceback, exception_type):\n"
        "    name = frame.f_code.co_name\n"
        "    if event == 4 and name.startswith('s_'):\n"
        "        names = []\n"
        "        while traceback is not None:\n"
        "            names.append(traceback.tb_frame.f_code.co_name)\n"
        "            traceback = traceback.tb_next\n"
        "        records.append((name, exception_type.__name__, repr(exception), names))\n"
        "def s_inner():\n"
        "    raise KeyError('k')\n"
        "def s_outer():\n"
        "    s_inner()\n"
        "def s_exit():\n"
        "    sys.exit(3)\n"
        "def s_class():\n"
        "    raise IndexError, 'i'\n"
        "def s_thrown():\n"
        "    yield 1\n"
        "for f in (s_outer, s_exit, s_class):\n"
        "    try:\n"
        "        f()\n"
        "    except BaseException:\n"
        "        pass\n"
        "thrown = s_thrown()\n"
        "next(thrown)\n"
        "expect_error(ValueError, lambda: thrown.throw(ValueError('v')), 'v')\n"
        "check(records, [('s_inner', 'KeyError', \"KeyError('k',)\", ['s_inner']),\n"
        "    ('s_outer', 'KeyError', \"KeyError('k',)\", ['s_outer', 's_inner']),\n"
        "    ('s_exit', 'SystemExit', '3', ['s_exit']),\n"
        "    ('s_class', 'IndexError', \"IndexError('i',)\", ['s_class']),\n"
        "    ('s_thrown', 'ValueError', \"ValueError('v',)\", ['s_thrown'])])\n";

    int32_t result = __test_debugger_forward_run(source);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* A host interrupt that the code of a debugger callback receives, and that
   the callback swallows, still interrupts the program at its next poll. */
static int32_t __test_debugger_interrupt(void) {
    static const char source[] =
        "state = {'armed': True, 'seen': []}\n"
        "def watch():\n"
        "    for i in range(50):\n"
        "        pass\n"
        "def tracer(event, frame, exception, traceback, exception_type):\n"
        "    if event == 1 and frame.f_code.co_name == 'work' and state['armed']:\n"
        "        state['armed'] = False\n"
        "        arm_interrupt()\n"
        "        try:\n"
        "            watch()\n"
        "            state['seen'].append('finished')\n"
        "        except KeyboardInterrupt:\n"
        "            state['seen'].append('interrupted')\n"
        "def work():\n"
        "    total = 0\n"
        "    for i in range(5):\n"
        "        total += i\n"
        "    return total\n"
        "expect_error(KeyboardInterrupt, work, 'execution interrupted by host')\n"
        "check(state, {'armed': False, 'seen': ['interrupted']})\n"
        "check(work(), 10)\n";

    int32_t result = __test_debugger_forward_run(source);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* An interrupt that a debugger callback defers belongs to its execution:
   it is dropped when the execution ends first, so that a later one runs,
   and when the callback removes the debugger. */
static int32_t __test_debugger_interrupt_scope(void) {
    static const char *const sources[] = {
        "state = {'armed': True, 'seen': []}\n"
        "def watch():\n"
        "    arm_interrupt()\n"
        "    try:\n"
        "        for i in range(50):\n"
        "            pass\n"
        "    except KeyboardInterrupt:\n"
        "        state['seen'].append('interrupted')\n"
        "def tracer(event, frame, exception, traceback, exception_type):\n"
        "    if event == 3 and frame.f_back is None and state['armed']:\n"
        "        state['armed'] = False\n"
        "        watch()\n",
        "for i in range(50):\n"
        "    pass\n"
        "check(state, {'armed': False, 'seen': ['interrupted']})\n"
        "def tracer(event, frame, exception, traceback, exception_type):\n"
        "    if event == 1 and frame.f_code.co_name == 'work':\n"
        "        watch()\n"
        "        check(remove_debugger(), True)\n"
        "def work():\n"
        "    total = 0\n"
        "    for i in range(5):\n"
        "        total += i\n"
        "    return total\n"
        "check(work(), 10)\n"
        "check(state['seen'], ['interrupted', 'interrupted'])\n"};

    int32_t result = __test_debugger_forward_run_sources(sources, sizeof(sources) / sizeof(sources[0]));
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* As PyFrame_FastToLocals maps them, the debugger finds an argument that a
   closure captures in its cell, and set and delete rebind the cell as
   PyFrame_LocalsToFast; the free variables of a class body are not its
   locals. The expected values are those of CPython. */
static int32_t __test_debugger_frame_cells(void) {
    static const char source[] =
        "import sys\n"
        "def captured(x):\n"
        "    def inner():\n"
        "        return x\n"
        "    x = 5\n"
        "    frame = sys._getframe()\n"
        "    seen = [get_local(frame, 'x')]\n"
        "    check(set_local(frame, 'x', 7), True)\n"
        "    seen.append(inner())\n"
        "    check(delete_local(frame, 'x'), True)\n"
        "    expect_error(NameError, inner, \"free variable 'x' referenced before assignment in enclosing scope\")\n"
        "    seen.append(get_local(frame, 'x'))\n"
        "    check(delete_local(frame, 'x'), False)\n"
        "    check(set_local(frame, 'x', 9), True)\n"
        "    seen.append(x)\n"
        "    return seen\n"
        "check(captured(1), [5, 7, None, 9])\n"
        "def outer():\n"
        "    x = 1\n"
        "    class Body(object):\n"
        "        y = x\n"
        "        frame = sys._getframe()\n"
        "        seen = get_local(frame, 'x')\n"
        "        check(set_local(frame, 'x', 99), True)\n"
        "        z = x\n"
        "        del frame\n"
        "    return x, Body.seen, Body.z, Body.x\n"
        "check(outer(), (1, None, 1, 99))\n";

    int32_t result = __test_debugger_forward_run(source);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* A key comparison of a debugger lookup in the locals of a suspended frame
   can close its generator, which clears the slots of the frame and lets
   its locals go: frame_sync stores nothing in the cell or the fast local
   that the lookup released, and frame_get finds nothing in the locals that
   only the lookup held. */
static int32_t __test_debugger_generator_close(void) {
    static const char source[] =
        "class Closing(str):\n"
        "    __hash__ = str.__hash__\n"
        "    def __eq__(self, other):\n"
        "        if holder:\n"
        "            holder.pop().close()\n"
        "        return str.__eq__(self, other)\n"
        "class Tracked(object):\n"
        "    pass\n"
        "def suspended():\n"
        "    first = Tracked()\n"
        "    yield 1\n"
        "    cell = 5\n"
        "    def inner():\n"
        "        return cell\n"
        "holder = []\n"
        "for name in ('cell', 'other', 'first'):\n"
        "    generator = suspended()\n"
        "    next(generator)\n"
        "    frame = generator.gi_frame\n"
        "    local_names = frame.f_locals\n"
        "    local_names.pop(name, None)\n"
        "    local_names[Closing(name)] = 0\n"
        "    del local_names\n"
        "    holder.append(generator)\n"
        "    del generator\n"
        "    if name == 'other':\n"
        "        check(get_local(frame, name), None)\n"
        "    else:\n"
        "        sync_frame(frame)\n"
        "    check(holder, [])\n"
        "    check(frame.f_locals, {})\n";

    int32_t result = __test_debugger_forward_run(source);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* tinypy_debugger_frame_sync stores what code changed in the locals that
   tinypy_frame_locals materialized, as PyFrame_LocalsToFast; it stops when
   a value it replaces finishes the generator that suspended the frame. */
static int32_t __test_debugger_frame_sync(void) {
    static const char source[] =
        "def tracer(event, frame, exception, traceback, exception_type):\n"
        "    if event == 1 and frame.f_code.co_name == 'target' and frame.f_lineno - frame.f_code.co_firstlineno == 5:\n"
        "        local_names = frame.f_locals\n"
        "        local_names['value'] = 41\n"
        "        local_names['cell'] = 'new'\n"
        "        sync_frame(frame)\n"
        "def target():\n"
        "    value = 1\n"
        "    cell = 'old'\n"
        "    def inner():\n"
        "        return cell\n"
        "    marker = None\n"
        "    return value + 1, inner()\n"
        "check(target(), (42, 'new'))\n"
        "class Closer(object):\n"
        "    def __del__(self):\n"
        "        holder.pop().close()\n"
        "def suspended():\n"
        "    first = Closer()\n"
        "    second = 'kept'\n"
        "    yield 1\n"
        "holder = [suspended()]\n"
        "next(holder[0])\n"
        "suspended_frame = holder[0].gi_frame\n"
        "local_names = suspended_frame.f_locals\n"
        "local_names['first'] = 0\n"
        "local_names['second'] = 'dropped'\n"
        "sync_frame(suspended_frame)\n"
        "check(holder, [])\n"
        "check(suspended_frame.f_locals, {})\n";

    int32_t result = __test_debugger_forward_run(source);
    return result;
}
#endif
//////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv) {
    if (argc != 2) {
        (void)fprintf(stderr, "usage: %s TEST_NAME\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "allocator") == 0) {
        int return_value_1 = __test_allocator_accounting();
        return return_value_1;
    }
    if (strcmp(argv[1], "pool_allocator") == 0) {
        int return_value_2 = __test_pool_allocator();
        return return_value_2;
    }
    if (strcmp(argv[1], "independent_vms") == 0) {
        int return_value_3 = __test_independent_vms();
        return return_value_3;
    }
    if (strcmp(argv[1], "value_lifetime") == 0) {
        int return_value_4 = __test_value_lifetime();
        return return_value_4;
    }
    if (strcmp(argv[1], "constant_cache") == 0) {
        int return_value_5 = __test_constant_cache();
        return return_value_5;
    }
    if (strcmp(argv[1], "byte_strings") == 0) {
        int return_value_6 = __test_byte_strings();
        return return_value_6;
    }
    if (strcmp(argv[1], "build_value") == 0) {
        int result = __test_build_value();
        return result;
    }
    if (strcmp(argv[1], "unicode_utf8") == 0) {
        int return_value_7 = __test_unicode_utf8();
        return return_value_7;
    }
    if (strcmp(argv[1], "buffer_unicode_refusal") == 0) {
        int result = __test_buffer_unicode_refusal();
        return result;
    }
    if (strcmp(argv[1], "string_release") == 0) {
        int return_value_8 = __test_string_release_contract();
        return return_value_8;
    }
    if (strcmp(argv[1], "numeric_bits") == 0) {
        int return_value_9 = __test_float_complex_bits();
        return return_value_9;
    }
    if (strcmp(argv[1], "float_free_list") == 0) {
        int float_free_list = __test_float_free_list();
        return float_free_list;
    }
    if (strcmp(argv[1], "long_canonical") == 0) {
        int return_value_10 = __test_long_canonical();
        return return_value_10;
    }
    if (strcmp(argv[1], "tuple_ownership") == 0) {
        int return_value_11 = __test_tuple_ownership();
        return return_value_11;
    }
    if (strcmp(argv[1], "tuple_deep") == 0) {
        int return_value_12 = __test_tuple_deep_release();
        return return_value_12;
    }
    if (strcmp(argv[1], "tuple_set_subclass") == 0) {
        int result = __test_tuple_set_subclass();
        return result;
    }
    if (strcmp(argv[1], "length_hint_past_reserve") == 0) {
        int result = __test_length_hint_past_reserve();
        return result;
    }
    if (strcmp(argv[1], "hash_equal") == 0) {
        int return_value_13 = __test_hash_and_equality();
        return return_value_13;
    }
    if (strcmp(argv[1], "dict") == 0) {
        int return_value_14 = __test_dictionary_runtime();
        return return_value_14;
    }
    if (strcmp(argv[1], "type_class") == 0) {
        int return_value_15 = __test_type_class_runtime();
        return return_value_15;
    }
    if (strcmp(argv[1], "code") == 0) {
        int return_value_16 = __test_code_object_runtime();
        if (return_value_16 != 0) {
            return return_value_16;
        }
        int cache_failure = __test_code_cache_allocation_failure();
        return cache_failure;
    }
    if (strcmp(argv[1], "eval_frame") == 0) {
        int return_value_17 = __test_eval_frame_runtime();
        return return_value_17;
    }
    if (strcmp(argv[1], "function_call") == 0) {
        int return_value_18 = __test_function_call_runtime();
        return return_value_18;
    }
    if (strcmp(argv[1], "exception_state_lost") == 0) {
        int return_value_24 = __test_exception_state_lost_runtime();
        return return_value_24;
    }
    if (strcmp(argv[1], "exception_contracts") == 0) {
        int result = __test_exception_contracts();
        return result;
    }
    if (strcmp(argv[1], "exception_diagnostics") == 0) {
        int result = __test_exception_diagnostics();
        return result;
    }
    if (strcmp(argv[1], "exception_normalization") == 0) {
        int result = __test_exception_normalization();
        return result;
    }
    if (strcmp(argv[1], "representation_errors") == 0) {
        int result = __test_representation_errors();
        return result;
    }
    if (strcmp(argv[1], "operator_numeric") == 0) {
        int return_value_19 = __test_operator_numeric_runtime();
        return return_value_19;
    }
    if (strcmp(argv[1], "stack_budget") == 0) {
        int result = __test_stack_budget();
        return result;
    }
    if (strcmp(argv[1], "host_hash_native_guard") == 0) {
        int result = __test_host_hash_native_guard();
        return result;
    }
    if (strcmp(argv[1], "class_subclass_native_guard") == 0) {
        int result = __test_class_subclass_native_guard();
        return result;
    }
    if (strcmp(argv[1], "intern_lifetime") == 0) {
        int result = __test_intern_lifetime();
        return result;
    }
    if (strcmp(argv[1], "float_hex_locale") == 0) {
        int result = __test_float_hex_locale();
        return result;
    }
    if (strcmp(argv[1], "subtype_factory_limits") == 0) {
        int result = __test_subtype_factory_limits();
        return result;
    }
    if (strcmp(argv[1], "container_heap_limits") == 0) {
        int return_value_20 = __test_container_heap_limits();
        if (return_value_20 != 0) {
            return return_value_20;
        }
        int failed_growth = __test_container_failed_growth();
        if (failed_growth != 0) {
            return failed_growth;
        }
        int removal_heap_budget = __test_container_removal_heap_budget();
        return removal_heap_budget;
    }
    if (strcmp(argv[1], "heap_budget_sweep") == 0) {
        int heap_budget = __test_heap_budget_sweep();
        return heap_budget;
    }
    if (strcmp(argv[1], "computed_heap_budget") == 0) {
        int computed_heap_budget = __test_computed_heap_budget();
        return computed_heap_budget;
    }
    if (strcmp(argv[1], "computed_allocation_refusal") == 0) {
        int computed_allocation_refusal = __test_computed_allocation_refusal();
        return computed_allocation_refusal;
    }
    if (strcmp(argv[1], "computed_result_refusal") == 0) {
        int computed_result_refusal = __test_computed_result_refusal();
        return computed_result_refusal;
    }
    if (strcmp(argv[1], "build_profile_allocation_refusal") == 0) {
        int build_profile_allocation_refusal = __test_build_profile_allocation_refusal();
        return build_profile_allocation_refusal;
    }
    if (strcmp(argv[1], "error_heap_accounting") == 0) {
        int error_heap_accounting = __test_error_heap_accounting();
        return error_heap_accounting;
    }
    if (strcmp(argv[1], "allocation_refusal") == 0) {
        int allocation_refusal = __test_allocation_refusal();
        return allocation_refusal;
    }
    if (strcmp(argv[1], "sre_allocation_refusal") == 0) {
        int sre_allocation_refusal = __test_sre_allocation_refusal();
        return sre_allocation_refusal;
    }
    if (strcmp(argv[1], "text_heap_budget") == 0) {
        int text_heap_budget = __test_text_heap_budget();
        return text_heap_budget;
    }
    if (strcmp(argv[1], "marshal_load_memory") == 0) {
        int marshal_load_memory = __test_marshal_load_memory();
        return marshal_load_memory;
    }
    if (strcmp(argv[1], "marshal_load_unhashable_key") == 0) {
        int marshal_load_unhashable_key = __test_marshal_load_unhashable_key();
        return marshal_load_unhashable_key;
    }
    if (strcmp(argv[1], "marshal_dump_memory") == 0) {
        int marshal_dump_memory = __test_marshal_dump_memory();
        return marshal_dump_memory;
    }
    if (strcmp(argv[1], "marshal_one_byte_strings") == 0) {
        int marshal_one_byte_strings = __test_marshal_one_byte_strings();
        return marshal_one_byte_strings;
    }
    if (strcmp(argv[1], "code_new_validation") == 0) {
        int code_new_validation = __test_code_new_validation();
        return code_new_validation;
    }
    if (strcmp(argv[1], "attribute_native_key_errors") == 0) {
        int result = __test_attribute_native_key_errors();
        return result;
    }
    if (strcmp(argv[1], "native_attribute_hook_class") == 0) {
        int result = __test_native_attribute_hook_class();
        return result;
    }
    if (strcmp(argv[1], "native_embedding") == 0) {
        int return_value_21 = __test_native_embedding();
        return return_value_21;
    }
    if (strcmp(argv[1], "native_items") == 0) {
        int result = __test_native_items();
        return result;
    }
    if (strcmp(argv[1], "native_type_contracts") == 0) {
        int result = __test_native_type_contracts();
        return result;
    }
    if (strcmp(argv[1], "native_destroy_finalizers") == 0) {
        int result = __test_native_destroy_finalizers();
        return result;
    }
    if (strcmp(argv[1], "equal_reentrancy") == 0) {
        int result = __test_equal_reentrancy();
        return result;
    }
    if (strcmp(argv[1], "native_length_errors") == 0) {
        int result = __test_native_length_errors();
        return result;
    }
    if (strcmp(argv[1], "native_destroy_order") == 0) {
        int result = __test_native_destroy_order();
        return result;
    }
    if (strcmp(argv[1], "native_destroy_release") == 0) {
        int result = __test_native_destroy_release();
        return result;
    }
    if (strcmp(argv[1], "native_destroy_finalized") == 0) {
        int result = __test_native_destroy_finalized();
        return result;
    }
    if (strcmp(argv[1], "native_destroy_passes") == 0) {
        int result = __test_native_destroy_passes();
        return result;
    }
    if (strcmp(argv[1], "native_destroy_runtime_values") == 0) {
        int result = __test_native_destroy_runtime_values();
        return result;
    }
    if (strcmp(argv[1], "native_finalizer_handoff") == 0) {
        int result = __test_native_finalizer_handoff();
        return result;
    }
    if (strcmp(argv[1], "native_finalizer_nested") == 0) {
        int result = __test_native_finalizer_nested();
        return result;
    }
    if (strcmp(argv[1], "native_failure_signals") == 0) {
        int result = __test_native_failure_signals();
        return result;
    }
    if (strcmp(argv[1], "error_reentrant_release") == 0) {
        int result = __test_error_reentrant_release();
        return result;
    }
    if (strcmp(argv[1], "error_pending_diagnostics") == 0) {
        int result = __test_error_pending_diagnostics();
        return result;
    }
    if (strcmp(argv[1], "native_slot_failures") == 0) {
        int result = __test_native_slot_failures();
        return result;
    }
    if (strcmp(argv[1], "native_slot_wrappers") == 0) {
        int result = __test_native_slot_wrappers();
        return result;
    }
    if (strcmp(argv[1], "type_versions") == 0) {
        int result = __test_type_versions();
        return result;
    }
    if (strcmp(argv[1], "type_new_metaclass") == 0) {
        int result = __test_type_new_metaclass();
        return result;
    }
    if (strcmp(argv[1], "type_set_attr_sharing") == 0) {
        int result = __test_type_set_attr_sharing();
        return result;
    }
    if (strcmp(argv[1], "compile_pending_reset") == 0) {
        int result = __test_compile_pending_reset();
        return result;
    }
    if (strcmp(argv[1], "generator_embedding") == 0) {
        int result = __test_generator_embedding();
        return result;
    }
    if (strcmp(argv[1], "module_finder") == 0) {
        int return_value_22 = __test_module_finder();
        return return_value_22;
    }
    if (strcmp(argv[1], "module_finder_protocol") == 0) {
        int result = __test_module_finder_protocol();
        return result;
    }
    if (strcmp(argv[1], "module_resolver_contract") == 0) {
        int result = __test_module_resolver_contract();
        return result;
    }
    if (strcmp(argv[1], "cycle_diagnostics") == 0) {
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        int return_value_23 = __test_cycle_diagnostics();
        return return_value_23;
#else
        return 0;
#endif
    }
    if (strcmp(argv[1], "debugger_frame_slots") == 0) {
#if defined(TINYPY_DEBUGGER)
        int result = __test_debugger_frame_slots();
        return result;
#else
        return 0;
#endif
    }
    if (strcmp(argv[1], "debugger_mapping_locals") == 0) {
#if defined(TINYPY_DEBUGGER)
        int result = __test_debugger_mapping_locals();
        return result;
#else
        return 0;
#endif
    }
    if (strcmp(argv[1], "debugger_return_events") == 0) {
#if defined(TINYPY_DEBUGGER)
        int result = __test_debugger_return_events();
        return result;
#else
        return 0;
#endif
    }
    if (strcmp(argv[1], "debugger_line_events") == 0) {
#if defined(TINYPY_DEBUGGER)
        int result = __test_debugger_line_events();
        return result;
#else
        return 0;
#endif
    }
    if (strcmp(argv[1], "debugger_exception_events") == 0) {
#if defined(TINYPY_DEBUGGER)
        int result = __test_debugger_exception_events();
        return result;
#else
        return 0;
#endif
    }
    if (strcmp(argv[1], "debugger_interrupt") == 0) {
#if defined(TINYPY_DEBUGGER)
        int result = __test_debugger_interrupt();
        return result;
#else
        return 0;
#endif
    }
    if (strcmp(argv[1], "debugger_frame_sync") == 0) {
#if defined(TINYPY_DEBUGGER)
        int result = __test_debugger_frame_sync();
        return result;
#else
        return 0;
#endif
    }
    if (strcmp(argv[1], "debugger_interrupt_scope") == 0) {
#if defined(TINYPY_DEBUGGER)
        int result = __test_debugger_interrupt_scope();
        return result;
#else
        return 0;
#endif
    }
    if (strcmp(argv[1], "debugger_frame_cells") == 0) {
#if defined(TINYPY_DEBUGGER)
        int result = __test_debugger_frame_cells();
        return result;
#else
        return 0;
#endif
    }
    if (strcmp(argv[1], "debugger_generator_close") == 0) {
#if defined(TINYPY_DEBUGGER)
        int result = __test_debugger_generator_close();
        return result;
#else
        return 0;
#endif
    }

    (void)fprintf(stderr, "unknown test: %s\n", argv[1]);
    return 2;
}
//////////////////////////////////////////////////////////////////////////
