#include "tinypy/tinypy.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef char test_double_size_must_match_uint64_t[sizeof(double) == sizeof(uint64_t) ? 1 : -1];
//////////////////////////////////////////////////////////////////////////
typedef union test_allocation_header_t {
    struct {
        size_t size;
        size_t alignment;
        uint32_t tag;
    } fields;
    void *pointer_alignment;
    void (*function_alignment)(void);
    int64_t integer_alignment;
    long double floating_alignment;
} test_allocation_header_t;
//////////////////////////////////////////////////////////////////////////
typedef struct test_allocator_state_t {
    size_t allocation_calls;
    size_t error_allocation_calls;
    size_t deallocation_calls;
    size_t outstanding_allocations;
    size_t outstanding_bytes;
    size_t last_allocation_size;
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
static void *__test_allocate(void *user_data, size_t size, size_t alignment, uint32_t tag) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;
    test_allocation_header_t *header;

    state->allocation_calls += 1U;
    if (tag == (uint32_t)TINYPY_ALLOC_TAG_ERROR) {
        state->error_allocation_calls += 1U;
    }
    state->last_allocation_size = size;

    if (size > SIZE_MAX - sizeof(*header)) {
        return NULL;
    }

    header = (test_allocation_header_t *)malloc(sizeof(*header) + size);
    if (header == NULL) {
        return NULL;
    }

    header->fields.size = size;
    header->fields.alignment = alignment;
    header->fields.tag = tag;
    state->outstanding_allocations += 1U;
    state->outstanding_bytes += size;
    return (void *)(header + 1);
}
//////////////////////////////////////////////////////////////////////////
static void *__test_reallocate(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment, uint32_t tag) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;
    test_allocation_header_t *header;
    test_allocation_header_t *resized;

    if (memory == NULL) {
        return __test_allocate(user_data, new_size, alignment, tag);
    }

    state->allocation_calls += 1U;
    if (tag == (uint32_t)TINYPY_ALLOC_TAG_ERROR) {
        state->error_allocation_calls += 1U;
    }
    header = ((test_allocation_header_t *)memory) - 1;
    state->last_allocation_size = new_size;
    if (header->fields.size != old_size || header->fields.alignment != alignment || header->fields.tag != tag || new_size > SIZE_MAX - sizeof(*header)) {
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
    resized->fields.tag = tag;
    return (void *)(resized + 1);
}
//////////////////////////////////////////////////////////////////////////
static void __test_deallocate(void *user_data, void *memory, size_t size, size_t alignment, uint32_t tag) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;
    test_allocation_header_t *header;

    if (memory == NULL) {
        return;
    }

    header = ((test_allocation_header_t *)memory) - 1;
    if (header->fields.size != size || header->fields.alignment != alignment || header->fields.tag != tag) {
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
static int __test_allocator_accounting(void) {
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
static int __test_pool_allocator(void) {
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
static int __test_independent_vms(void) {
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
static int __test_value_lifetime(void) {
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
static int __test_constant_cache(void) {
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
static int __test_byte_strings(void) {
    //////////////////////////////////////////////////////////////////////////
    unsigned char bytes[] = {
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
    TEST_CHECK(((const unsigned char *)view)[0] == 0x61U);
    TEST_CHECK(memcmp(((const unsigned char *)view) + 1, bytes + 1, sizeof(bytes) - 1U) == 0);
    TEST_CHECK(((const unsigned char *)view)[view_size] == 0U);

    empty = tinypy_string_from_bytes(vm, NULL, 0U);
    view = tinypy_string_view(empty, &view_size);
    TEST_CHECK(view != NULL);
    TEST_CHECK(view_size == 0U);
    TEST_CHECK(((const unsigned char *)view)[0] == 0U);

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
static int __test_unicode_utf8(void) {
    //////////////////////////////////////////////////////////////////////////
    unsigned char utf8[] = {
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
    TEST_CHECK(((const unsigned char *)view)[0] == 0x41U);
    TEST_CHECK(
        memcmp(view + 1, utf8 + 1, sizeof(utf8) - 1U) == 0);
    TEST_CHECK(((const unsigned char *)view)[view_size] == 0U);

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
static int __test_string_release_contract(void) {
    unsigned char bytes[257];
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
        bytes[index] = (unsigned char)(index & 0xffU);
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
static int __test_float_complex_bits(void) {
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
static int __test_long_canonical(void) {
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
        int sign = 0;
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
        int sign = 0;
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

    tinypy_vm_destroy(vm);
    TEST_CHECK(state.outstanding_allocations == 0U);
    TEST_CHECK(state.outstanding_bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_tuple_ownership(void) {
    static const unsigned char child_bytes[] = {0x61U, 0x00U, 0x62U};
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
static int __test_tuple_deep_release(void) {
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
static int __test_hash_and_equality(void) {
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
    TEST_CHECK(type_dict_size == 0U);

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
static int __test_dictionary_runtime(void) {
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
    int contains;
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
static int __test_type_class_runtime(void) {
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
    size_t error_allocation_calls;
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

    type_dict = tinypy_object_get_attr(tinypy_type_as_value(base_a), "__dict__", 8U, &error);
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
    error_allocation_calls = state.error_allocation_calls;
    for (probe = 0U; probe != 100000U; ++probe) {
        TEST_CHECK(tinypy_object_has_attr(instance, "absent", 6U) == 0);
    }
    TEST_CHECK(state.error_allocation_calls == error_allocation_calls);
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
static int __test_code_object_runtime(void) {
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
static int __test_eval_frame_runtime(void) {
    //////////////////////////////////////////////////////////////////////////
    static const unsigned char instructions[] = {
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
static int __test_function_call_runtime(void) {
    static const unsigned char function_instructions[] = {124U, 0U, 0U, 124U, 1U, 0U, 102U, 2U, 0U, 83U};
    static const unsigned char module_instructions[] = {100U, 0U, 0U, 100U, 1U, 0U, 132U, 1U, 0U, 90U, 0U, 0U, 101U, 0U, 0U, 100U, 2U, 0U, 131U, 1U, 0U, 83U};
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
static int __test_operator_numeric_runtime(void) {
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm;
    tinypy_error_t *error = NULL;
    tinypy_value_t *maximum;
    tinypy_value_t *one;
    tinypy_value_t *overflow;
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
    const uint16_t *digits;
    const void *bytes;
    size_t digit_count;
    size_t byte_size;
    int sign;
    uint16_t large_digits[5] = {0U, 0U, 0U, 0U, 1024U};

    (void)memset(&state, 0, sizeof(state));
    allocator = __test_make_allocator(&state);
    config = __test_make_config(&allocator);
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
    int32_t compare_calls;
    int32_t compare_order[8];
    size_t compare_order_count;
    int32_t binary_calls;
    int32_t reflected_calls;
    int32_t inplace_calls;
} test_native_state_t;
//////////////////////////////////////////////////////////////////////////
static int32_t __test_native_construct(tinypy_value_t *instance, void *payload, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;
    test_native_state_t *state = (test_native_state_t *)user_data;

    (void)instance;
    (void)kwargs;
    (void)out_error;
    if (tinypy_tuple_size(args) != 0U) {
        return INT32_C(0);
    }
    native_payload->value = 73;
    state->constructed += 1;
    return INT32_C(1);
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
    return tinypy_string_from_bytes(value_vm, "native-73", 9U);
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
        return tinypy_not_implemented_get(state->vm);
    }
    equal = native_payload->value == tinypy_integer_as_i64(other) ? INT32_C(1) : INT32_C(0);
    if (operation == TINYPY_COMPARE_EQUAL) {
        return tinypy_bool_from_i32(state->vm, equal);
    }
    if (operation == TINYPY_COMPARE_NOT_EQUAL) {
        return tinypy_bool_from_i32(state->vm, equal == 0 ? INT32_C(1) : INT32_C(0));
    }
    return tinypy_not_implemented_get(state->vm);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __test_native_hash(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;

    (void)instance;
    (void)user_data;
    (void)out_error;
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
    return tinypy_integer_from_i64(state->vm, native_payload->value + tinypy_integer_as_i64(tinypy_tuple_get(args, 0U)));
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_negative(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;
    test_native_state_t *state = (test_native_state_t *)user_data;

    (void)instance;
    (void)out_error;
    return tinypy_integer_from_i64(state->vm, -(int64_t)native_payload->value);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_absolute(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    test_native_payload_t *native_payload = (test_native_payload_t *)payload;
    test_native_state_t *state = (test_native_state_t *)user_data;
    int64_t result = native_payload->value < 0 ? -(int64_t)native_payload->value : (int64_t)native_payload->value;

    (void)instance;
    (void)out_error;
    return tinypy_integer_from_i64(state->vm, result);
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
        return tinypy_not_implemented_get(state->vm);
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
    return tinypy_integer_from_i64(state->vm, result);
}
//////////////////////////////////////////////////////////////////////////
#define TEST_NATIVE_BINARY_CALLBACK(name, operation, reflected) \
    static tinypy_value_t *name(tinypy_value_t *instance, void *payload, tinypy_value_t *other, void *user_data, tinypy_error_t **out_error) { \
        return __test_native_binary_result(instance, payload, other, user_data, out_error, operation, reflected); \
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
        return tinypy_not_implemented_get(state->vm);
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
#define TEST_NATIVE_INPLACE_CALLBACK(name, operation) \
    static tinypy_value_t *name(tinypy_value_t *instance, void *payload, tinypy_value_t *other, void *user_data, tinypy_error_t **out_error) { \
        return __test_native_inplace_result(instance, payload, other, user_data, out_error, operation); \
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
    return size == expected_size && (size == 0U || memcmp(bytes, expected, size) == 0) ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_native_get_attribute(tinypy_value_t *instance, void *payload, tinypy_value_t *name, void *user_data, tinypy_error_t **out_error) {
    test_native_state_t *state = (test_native_state_t *)user_data;

    (void)instance;
    (void)payload;
    (void)out_error;
    state->attribute_calls += 1;
    if (__test_native_attribute_name_equal(name, "present", 7U) != 0) {
        return tinypy_integer_from_i64(state->vm, 73);
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
static int __test_native_embedding(void) {
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
    tinypy_value_t *base_instance;
    tinypy_value_t *other_instance;
    tinypy_value_t *args;
    tinypy_value_t *call_args;
    tinypy_value_t *representation;
    tinypy_value_t *value;
    tinypy_value_t *native_function;
    tinypy_value_t *native_result;
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
    TEST_CHECK(tinypy_compare_bool(instance, other_instance, TINYPY_COMPARE_EQUAL, &error) == 0);
    TEST_CHECK(error == NULL);
    TEST_CHECK(native_state.compare_order_count == 2U);
    TEST_CHECK(native_state.compare_order[0] == 2 && native_state.compare_order[1] == 2);

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
    incompatible_type = tinypy_native_type_new(vm, "Other", 5U, NULL, 0U, NULL, &incompatible_spec, &error);
    TEST_CHECK(incompatible_type != NULL);
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
    size_t find_count;
    size_t load_count;
} test_module_finder_state_t;
//////////////////////////////////////////////////////////////////////////
static int __test_module_name_is(tinypy_value_t *value, const char *expected, size_t expected_size) {
    const void *bytes;
    size_t size;

    if (tinypy_typeof(value) != TINYPY_VALUE_STRING) {
        return 0;
    }
    bytes = tinypy_string_view(value, &size);
    return size == expected_size && memcmp(bytes, expected, expected_size) == 0;
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
        return tinypy_none_get(state->vm);
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
    } {
        tinypy_value_t *answer = tinypy_integer_from_i64(state->vm, 42);

        tinypy_module_add_value(module, "answer", 6U, answer);
        tinypy_release(answer);
    }
    return module;
}
//////////////////////////////////////////////////////////////////////////
static int __test_module_finder(void) {
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
    tinypy_release(module);
    module = tinypy_import_module(vm, "finder_sample", 13U, NULL, NULL, 0, &error);
    TEST_CHECK(module != NULL);
    tinypy_release(module);
    TEST_CHECK(finder_state.find_count == 1U && finder_state.load_count == 1U);

    module = tinypy_import_module(vm, "finder_broken", 13U, NULL, NULL, 0, &error);
    TEST_CHECK(module == NULL);
    TEST_CHECK(error != NULL);
    tinypy_error_release(error);
    error = NULL;
    tinypy_vm_clear_error(vm);
    key = tinypy_string_from_bytes(vm, "finder_broken", 13U);
    TEST_CHECK(tinypy_dict_contains(tinypy_vm_modules(vm), key) == 0);
    tinypy_release(key);
    TEST_CHECK(finder_state.find_count == 2U && finder_state.load_count == 2U);

    tinypy_vm_set_module_finder(vm, NULL);
    TEST_CHECK(tinypy_vm_module_finder(vm) == NULL);
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
        return __test_allocator_accounting();
    }
    if (strcmp(argv[1], "pool_allocator") == 0) {
        return __test_pool_allocator();
    }
    if (strcmp(argv[1], "independent_vms") == 0) {
        return __test_independent_vms();
    }
    if (strcmp(argv[1], "value_lifetime") == 0) {
        return __test_value_lifetime();
    }
    if (strcmp(argv[1], "constant_cache") == 0) {
        return __test_constant_cache();
    }
    if (strcmp(argv[1], "byte_strings") == 0) {
        return __test_byte_strings();
    }
    if (strcmp(argv[1], "unicode_utf8") == 0) {
        return __test_unicode_utf8();
    }
    if (strcmp(argv[1], "string_release") == 0) {
        return __test_string_release_contract();
    }
    if (strcmp(argv[1], "numeric_bits") == 0) {
        return __test_float_complex_bits();
    }
    if (strcmp(argv[1], "long_canonical") == 0) {
        return __test_long_canonical();
    }
    if (strcmp(argv[1], "tuple_ownership") == 0) {
        return __test_tuple_ownership();
    }
    if (strcmp(argv[1], "tuple_deep") == 0) {
        return __test_tuple_deep_release();
    }
    if (strcmp(argv[1], "hash_equal") == 0) {
        return __test_hash_and_equality();
    }
    if (strcmp(argv[1], "dict") == 0) {
        return __test_dictionary_runtime();
    }
    if (strcmp(argv[1], "type_class") == 0) {
        return __test_type_class_runtime();
    }
    if (strcmp(argv[1], "code") == 0) {
        return __test_code_object_runtime();
    }
    if (strcmp(argv[1], "eval_frame") == 0) {
        return __test_eval_frame_runtime();
    }
    if (strcmp(argv[1], "function_call") == 0) {
        return __test_function_call_runtime();
    }
    if (strcmp(argv[1], "operator_numeric") == 0) {
        return __test_operator_numeric_runtime();
    }
    if (strcmp(argv[1], "native_embedding") == 0) {
        return __test_native_embedding();
    }
    if (strcmp(argv[1], "module_finder") == 0) {
        return __test_module_finder();
    }

    (void)fprintf(stderr, "unknown test: %s\n", argv[1]);
    return 2;
}
//////////////////////////////////////////////////////////////////////////
