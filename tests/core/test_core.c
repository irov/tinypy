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

    if ((state->fail_allocation_above != 0U && size > state->fail_allocation_above) || size > SIZE_MAX - sizeof(*header)) {
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
    if (header->fields.size != old_size || header->fields.alignment != alignment || (state->fail_allocation_above != 0U && new_size > state->fail_allocation_above) || new_size > SIZE_MAX - sizeof(*header)) {
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
static int32_t __test_pool_allocator(void) {
    const size_t value_count = 20000U;
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm;
    tinypy_value_t **values;
    size_t base_allocations;
    size_t allocation_calls;
    size_t pooled_allocation_calls;
    size_t index;

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
    vm = tinypy_vm_create(&config);
    TEST_CHECK(vm != NULL);
    values = (tinypy_value_t **)malloc(value_count * sizeof(*values));
    TEST_CHECK(values != NULL);
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
    TEST_CHECK(state.outstanding_allocations == base_allocations);

    free(values);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
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
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm = NULL;
    tinypy_value_t *value = NULL;
    tinypy_value_t *empty = NULL;
    const char *view = NULL;
    size_t view_size = 0U;
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

    empty = tinypy_unicode_from_utf8(vm, NULL, 0U);
    view = tinypy_unicode_utf8_view(empty,
                                    &view_size,
                                    &code_point_count);
    TEST_CHECK(view != NULL);
    TEST_CHECK(view_size == 0U);
    TEST_CHECK(code_point_count == 0U);
    TEST_CHECK(view[0] == '\0');

    tinypy_release(value);
    tinypy_release(empty);
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
    TEST_CHECK(state.outstanding_allocations == base_allocations);

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
    TEST_CHECK(state.outstanding_allocations == base_allocations);
    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
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

    tinypy_dict_set(dict, key_a, dict);
    tinypy_dict_clear(dict);
    size = tinypy_dict_size(dict);
    TEST_CHECK(size == 0U);
    contains = tinypy_dict_contains(dict, key_a);
    TEST_CHECK(contains == 0);

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
    /* The dictionary and type lookup cache each retain the name. */
    TEST_CHECK(TINYPY_REFCNT(method_key) == method_key_refs + 2);
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
static int32_t __test_float_hex_locale(void) {
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
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
    tinypy_value_t *sort_items[128];
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

    for (index = 0U; index < sizeof(sort_items) / sizeof(sort_items[0]); ++index) {
        sort_items[index] = tinypy_integer_from_i64(vm, (int64_t)index);
    }
    sort_list = tinypy_list_from_items(vm, sort_items, sizeof(sort_items) / sizeof(sort_items[0]));
    for (index = 0U; index < sizeof(sort_items) / sizeof(sort_items[0]); ++index) {
        tinypy_release(sort_items[index]);
    }
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

    state.fail_allocation_above = 512U;
    sort_result = tinypy_call(sort_method, sort_args, NULL, &error);
    TEST_CHECK(sort_result == NULL);
    TEST_CHECK(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_MEMORY);
    TEST_CHECK(tinypy_list_size(sort_list) == sizeof(sort_items) / sizeof(sort_items[0]));
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    state.fail_allocation_above = 0U;
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
static int32_t __test_native_embedding(void) {
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
    tinypy_error_release(error);

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
    if (strcmp(argv[1], "string_release") == 0) {
        int return_value_8 = __test_string_release_contract();
        return return_value_8;
    }
    if (strcmp(argv[1], "numeric_bits") == 0) {
        int return_value_9 = __test_float_complex_bits();
        return return_value_9;
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
        return return_value_16;
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
    if (strcmp(argv[1], "operator_numeric") == 0) {
        int return_value_19 = __test_operator_numeric_runtime();
        return return_value_19;
    }
    if (strcmp(argv[1], "stack_budget") == 0) {
        int result = __test_stack_budget();
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
        return return_value_20;
    }
    if (strcmp(argv[1], "native_embedding") == 0) {
        int return_value_21 = __test_native_embedding();
        return return_value_21;
    }
    if (strcmp(argv[1], "module_finder") == 0) {
        int return_value_22 = __test_module_finder();
        return return_value_22;
    }
    if (strcmp(argv[1], "cycle_diagnostics") == 0) {
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        int return_value_23 = __test_cycle_diagnostics();
        return return_value_23;
#else
        return 0;
#endif
    }

    (void)fprintf(stderr, "unknown test: %s\n", argv[1]);
    return 2;
}
//////////////////////////////////////////////////////////////////////////
