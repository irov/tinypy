#include "tinypy/marshal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CHECK(condition)                \
    do { \
        if (!(condition)) { \
            (void)fprintf(                   \
                stderr,                      \
                "%s:%d: check failed: %s\n", \
                __FILE__,                    \
                __LINE__,                    \
                #condition);                 \
            return 0;                        \
        }                                    \
    } while (0)

typedef struct test_allocator_state_t {
    size_t calls;
    size_t live_allocations;
    size_t allocations;
    size_t deallocations;
} test_allocator_state_t;

typedef struct test_writer_t {
    uint8_t bytes[16384];
    size_t size;
    int32_t failed;
} test_writer_t;

static void *__test_allocate(void *user_data, size_t size, size_t alignment, tinypy_allocation_tag_e tag) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;
    void *memory;
    (void)alignment;
    (void)tag;

    state->calls += 1U;
    memory = malloc(size);
    if (memory != NULL) {
        state->live_allocations += 1U;
        state->allocations += 1U;
    }
    return memory;
}

static void *__test_reallocate(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment, tinypy_allocation_tag_e tag) {
    (void)user_data;
    (void)old_size;
    (void)alignment;
    (void)tag;
    void *return_value_1 = realloc(memory, new_size);
    return return_value_1;
}

static void __test_deallocate(void *user_data, void *memory, size_t size, size_t alignment, tinypy_allocation_tag_e tag) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;
    (void)size;
    (void)alignment;
    (void)tag;
    if (memory != NULL) {
        state->live_allocations -= 1U;
        state->deallocations += 1U;
        free(memory);
    }
}

static tinypy_allocator_t __test_allocator(test_allocator_state_t *state) {
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

static void __test_state_init(test_allocator_state_t *state) {
    (void)memset(state, 0, sizeof(*state));
}

static void __writer_byte(test_writer_t *writer, uint8_t value) {
    if (writer->size == sizeof(writer->bytes)) {
        writer->failed = 1;
        return;
    }
    writer->bytes[writer->size] = value;
    writer->size += 1U;
}

static void __writer_u16(test_writer_t *writer, uint16_t value) {
    __writer_byte(writer, (uint8_t)(value & UINT16_C(0xff)));
    __writer_byte(writer, (uint8_t)((value >> 8U) & UINT16_C(0xff)));
}

static void __writer_i32(test_writer_t *writer, int32_t value) {
    uint32_t bits;
    if (value >= 0) {
        bits = (uint32_t)value;
    }
    else {
        bits = UINT32_C(0x80000000) + (uint32_t)((int64_t)value - (int64_t)INT32_MIN);
    }
    __writer_byte(writer, (uint8_t)(bits & UINT32_C(0xff)));
    __writer_byte(writer, (uint8_t)((bits >> 8U) & UINT32_C(0xff)));
    __writer_byte(writer, (uint8_t)((bits >> 16U) & UINT32_C(0xff)));
    __writer_byte(writer, (uint8_t)((bits >> 24U) & UINT32_C(0xff)));
}

static void __writer_u64(test_writer_t *writer, uint64_t value) {
    size_t index;
    for (index = 0U; index != 8U; ++index) {
        __writer_byte(writer, (uint8_t)((value >> (index * 8U)) & UINT64_C(0xff)));
    }
}

static void __writer_data(test_writer_t *writer, const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;
    for (index = 0U; index != size; ++index) {
        __writer_byte(writer, bytes[index]);
    }
}

static void __writer_bytes(test_writer_t *writer, const void *data, size_t size) {
    __writer_byte(writer, (uint8_t)'s');
    __writer_i32(writer, (int32_t)size);
    __writer_data(writer, data, size);
}

static void __writer_text_float(test_writer_t *writer, uint8_t type, const char *text) {
    size_t size = strlen(text);
    __writer_byte(writer, type);
    __writer_byte(writer, (uint8_t)size);
    __writer_data(writer, text, size);
}

static void __writer_empty_tuple(test_writer_t *writer) {
    __writer_byte(writer, (uint8_t)'(');
    __writer_i32(writer, 0);
}

static void __writer_code(test_writer_t *writer, int32_t include_nested, int32_t include_intern_references, int32_t invalid_bytecode_type) {
    static const uint8_t bytecode[] = {(uint8_t)'d', 0U};

    __writer_byte(writer, (uint8_t)'c');
    __writer_i32(writer, include_nested ? 1 : 0);
    __writer_i32(writer, include_nested ? 1 : 0);
    __writer_i32(writer, 2);
    __writer_i32(writer, 0x43);

    if (invalid_bytecode_type) {
        __writer_byte(writer, (uint8_t)'N');
    }
    else {
        __writer_bytes(writer, bytecode, sizeof(bytecode));
    }

    __writer_byte(writer, (uint8_t)'(');
    __writer_i32(writer, include_nested ? 1 : 0);
    if (include_nested) {
        __writer_code(writer, 0, 0, 0);
    }

    __writer_byte(writer, (uint8_t)'(');
    __writer_i32(writer, include_intern_references ? 2 : 0);
    if (include_intern_references) {
        __writer_byte(writer, (uint8_t)'t');
        __writer_i32(writer, 4);
        __writer_data(writer, "name", 4U);
        __writer_byte(writer, (uint8_t)'R');
        __writer_i32(writer, 0);
    }

    __writer_byte(writer, (uint8_t)'(');
    __writer_i32(writer, include_intern_references ? 1 : 0);
    if (include_intern_references) {
        __writer_byte(writer, (uint8_t)'R');
        __writer_i32(writer, 0);
    }
    __writer_empty_tuple(writer);
    __writer_empty_tuple(writer);
    __writer_bytes(writer, "fixture.py", 10U);
    __writer_bytes(writer, include_nested ? "outer" : "inner", 5U);
    __writer_i32(writer, include_nested ? 10 : 20);
    __writer_bytes(writer, "\0\1", 2U);
}

static tinypy_marshal_result_e __read_writer(test_writer_t *writer, test_allocator_state_t *state, const tinypy_marshal_limits_t *limits, tinypy_marshal_document_t **out_document, tinypy_marshal_error_t *out_error) {
    tinypy_allocator_t allocator = __test_allocator(state);
    tinypy_marshal_result_e return_value_1 = tinypy_marshal_read_v2(
        writer->bytes,
        writer->size,
        &allocator,
        limits,
        out_document,
        out_error);
    return return_value_1;
}

static int32_t __test_exact_dump(const tinypy_marshal_document_t *document, const uint8_t *expected, size_t expected_size, test_allocator_state_t *state) {
    uint8_t output[16384];
    uint8_t short_output[16384];
    size_t required_size = 0U;
    size_t index;
    size_t calls_before = state->calls;
    tinypy_marshal_error_t error;
    tinypy_marshal_document_t *roundtrip = NULL;
    tinypy_allocator_t allocator = __test_allocator(state);

    TEST_CHECK(expected_size <= sizeof(output));
    TEST_CHECK(
        tinypy_marshal_write_v2(
            document,
            NULL,
            0U,
            &required_size,
            NULL,
            &error) == TINYPY_MARSHAL_OK);
    TEST_CHECK(required_size == expected_size);
    TEST_CHECK(state->calls == calls_before);

    (void)memset(short_output, 0xa5, sizeof(short_output));
    TEST_CHECK(
        tinypy_marshal_write_v2(
            document,
            short_output,
            required_size - 1U,
            &required_size,
            NULL,
            &error) == TINYPY_MARSHAL_BUFFER_TOO_SMALL);
    TEST_CHECK(required_size == expected_size);
    TEST_CHECK(error.offset == expected_size - 1U);
    TEST_CHECK(state->calls == calls_before);
    for (index = 0U; index != sizeof(short_output); ++index) {
        TEST_CHECK(short_output[index] == UINT8_C(0xa5));
    }

    (void)memset(output, 0, sizeof(output));
    TEST_CHECK(
        tinypy_marshal_write_v2(
            document,
            output,
            sizeof(output),
            &required_size,
            NULL,
            &error) == TINYPY_MARSHAL_OK);
    TEST_CHECK(required_size == expected_size);
    TEST_CHECK(memcmp(output, expected, expected_size) == 0);
    TEST_CHECK(state->calls == calls_before);

    TEST_CHECK(
        tinypy_marshal_read_v2(
            output,
            required_size,
            &allocator,
            NULL,
            &roundtrip,
            &error) == TINYPY_MARSHAL_OK);
    TEST_CHECK(roundtrip != NULL);
    tinypy_marshal_document_destroy(roundtrip);
    TEST_CHECK(state->live_allocations != 0U);
    return 1;
}

static int32_t __test_nested_code_and_interns(void) {
    test_writer_t writer;
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_marshal_document_t *document = NULL;
    tinypy_marshal_error_t error;
    const tinypy_marshal_object_t *root;
    const tinypy_marshal_code_t *code;
    const tinypy_marshal_object_t *first_name;
    const tinypy_marshal_object_t *second_name;
    const tinypy_marshal_object_t *varname;
    const tinypy_marshal_object_t *nested;
    const void *bytes;
    size_t size;
    tinypy_bool_t interned;

    (void)memset(&writer, 0, sizeof(writer));
    __test_state_init(&state);
    __writer_code(&writer, 1, 1, 0);
    TEST_CHECK(!writer.failed);
    allocator = __test_allocator(&state);
    TEST_CHECK(
        tinypy_marshal_read_v2(
            writer.bytes,
            writer.size,
            &allocator,
            NULL,
            &document,
            &error) == TINYPY_MARSHAL_OK);
    TEST_CHECK(error.code == TINYPY_MARSHAL_OK);
    TEST_CHECK(document != NULL);
    TEST_CHECK(tinypy_marshal_document_input_size(document) == writer.size);
    TEST_CHECK(tinypy_marshal_document_object_count(document) > 10U);
    TEST_CHECK(tinypy_marshal_document_allocated_bytes(document) > writer.size);

    root = tinypy_marshal_document_root(document);
    TEST_CHECK(tinypy_marshal_object_type(root) == TINYPY_MARSHAL_TYPE_CODE);
    code = tinypy_marshal_code_view(root);
    TEST_CHECK(code != NULL);
    TEST_CHECK(code->abi_version == TINYPY_MARSHAL_ABI_VERSION);
    TEST_CHECK(code->argcount == 1);
    tinypy_marshal_bytes_view(code->bytecode, &bytes, &size, &interned);
    TEST_CHECK(size == 2U);
    TEST_CHECK(((const uint8_t *)bytes)[0] == (uint8_t)'d');
    TEST_CHECK(((const uint8_t *)bytes)[1] == 0U);
    TEST_CHECK(!interned);

    nested = tinypy_marshal_sequence_item(code->consts, 0U);
    TEST_CHECK(tinypy_marshal_object_type(nested) == TINYPY_MARSHAL_TYPE_CODE);
    TEST_CHECK(tinypy_marshal_code_view(nested)->firstlineno == 20);

    first_name = tinypy_marshal_sequence_item(code->names, 0U);
    second_name = tinypy_marshal_sequence_item(code->names, 1U);
    varname = tinypy_marshal_sequence_item(code->varnames, 0U);
    TEST_CHECK(first_name == second_name);
    TEST_CHECK(first_name == varname);
    tinypy_marshal_bytes_view(first_name, &bytes, &size, &interned);
    TEST_CHECK(size == 4U && interned);
    TEST_CHECK(memcmp(bytes, "name", 4U) == 0);

    TEST_CHECK(__test_exact_dump(document, writer.bytes, writer.size, &state));

    (void)memset(writer.bytes, 0xa5, writer.size);
    tinypy_marshal_bytes_view(code->filename, &bytes, &size, &interned);
    TEST_CHECK(size == 10U && memcmp(bytes, "fixture.py", 10U) == 0);

    allocator.deallocate = NULL;
    tinypy_marshal_document_destroy(document);
    TEST_CHECK(state.live_allocations == 0U);
    TEST_CHECK(state.allocations == state.deallocations);
    return 1;
}

static int32_t __test_all_wire_types(void) {
    test_writer_t writer;
    uint8_t golden[16384];
    size_t golden_size;
    test_allocator_state_t state;
    tinypy_marshal_document_t *document = NULL;
    tinypy_marshal_error_t error;
    const tinypy_marshal_object_t *root;
    const tinypy_marshal_object_t *item;
    const void *bytes;
    const char *utf8;
    const uint16_t *digits;
    size_t size;
    size_t points;
    size_t digit_count;
    tinypy_bool_t interned;
    int32_t sign;
    tinypy_bool_t bool_value;
    int64_t integer_value;
    double real;
    double imaginary;

    (void)memset(&writer, 0, sizeof(writer));
    __test_state_init(&state);
    __writer_byte(&writer, (uint8_t)'(');
    __writer_i32(&writer, 22);
    __writer_byte(&writer, (uint8_t)'N');
    __writer_byte(&writer, (uint8_t)'F');
    __writer_byte(&writer, (uint8_t)'T');
    __writer_byte(&writer, (uint8_t)'S');
    __writer_byte(&writer, (uint8_t)'.');
    __writer_byte(&writer, (uint8_t)'i');
    __writer_i32(&writer, -123);
    __writer_byte(&writer, (uint8_t)'I');
    __writer_u64(&writer, UINT64_C(0x100000001));
    __writer_byte(&writer, (uint8_t)'l');
    __writer_i32(&writer, -2);
    __writer_u16(&writer, 1U);
    __writer_u16(&writer, 2U);
    __writer_text_float(&writer, (uint8_t)'f', "-1.25e2");
    __writer_byte(&writer, (uint8_t)'g');
    __writer_u64(&writer, UINT64_C(0x3ff8000000000000));
    __writer_byte(&writer, (uint8_t)'x');
    __writer_byte(&writer, 3U);
    __writer_data(&writer, "2.5", 3U);
    __writer_byte(&writer, 4U);
    __writer_data(&writer, "-3.5", 4U);
    __writer_byte(&writer, (uint8_t)'y');
    __writer_u64(&writer, UINT64_C(0x4000000000000000));
    __writer_u64(&writer, UINT64_C(0xc008000000000000));
    __writer_bytes(&writer, "a\0b", 3U);
    __writer_byte(&writer, (uint8_t)'t');
    __writer_i32(&writer, 3);
    __writer_data(&writer, "key", 3U);
    __writer_byte(&writer, (uint8_t)'R');
    __writer_i32(&writer, 0);
    __writer_byte(&writer, (uint8_t)'u');
    __writer_i32(&writer, 2);
    __writer_byte(&writer, UINT8_C(0xc3));
    __writer_byte(&writer, UINT8_C(0xa9));
    __writer_empty_tuple(&writer);
    __writer_byte(&writer, (uint8_t)'[');
    __writer_i32(&writer, 0);
    __writer_byte(&writer, (uint8_t)'{');
    __writer_bytes(&writer, "k", 1U);
    __writer_byte(&writer, (uint8_t)'i');
    __writer_i32(&writer, 42);
    __writer_byte(&writer, (uint8_t)'0');
    __writer_byte(&writer, (uint8_t)'<');
    __writer_i32(&writer, 0);
    __writer_byte(&writer, (uint8_t)'>');
    __writer_i32(&writer, 0);
    __writer_code(&writer, 0, 0, 0);

    golden_size = writer.size;
    (void)memcpy(golden, writer.bytes, golden_size);

    TEST_CHECK(__read_writer(&writer, &state, NULL, &document, &error) == TINYPY_MARSHAL_OK);
    root = tinypy_marshal_document_root(document);
    TEST_CHECK(tinypy_marshal_sequence_size(root) == 22U);
    TEST_CHECK(tinypy_marshal_object_type(tinypy_marshal_sequence_item(root, 0U)) == TINYPY_MARSHAL_TYPE_NONE);
    bool_value = tinypy_marshal_bool_value(tinypy_marshal_sequence_item(root, 1U));
    TEST_CHECK(!bool_value);
    bool_value = tinypy_marshal_bool_value(tinypy_marshal_sequence_item(root, 2U));
    TEST_CHECK(bool_value);
    TEST_CHECK(tinypy_marshal_object_type(tinypy_marshal_sequence_item(root, 3U)) == TINYPY_MARSHAL_TYPE_STOP_ITERATION);
    TEST_CHECK(tinypy_marshal_object_type(tinypy_marshal_sequence_item(root, 4U)) == TINYPY_MARSHAL_TYPE_ELLIPSIS);
    integer_value = tinypy_marshal_integer_value(tinypy_marshal_sequence_item(root, 5U));
    TEST_CHECK(integer_value == -123);
    integer_value = tinypy_marshal_integer_value(tinypy_marshal_sequence_item(root, 6U));
    TEST_CHECK(integer_value == INT64_C(0x100000001));
    tinypy_marshal_long_view(
        tinypy_marshal_sequence_item(root, 7U), &sign, &digits, &digit_count);
    TEST_CHECK(sign == -1 && digit_count == 2U && digits[0] == 1U && digits[1] == 2U);
    real = tinypy_marshal_float_value(tinypy_marshal_sequence_item(root, 8U));
    TEST_CHECK(real == -125.0);
    real = tinypy_marshal_float_value(tinypy_marshal_sequence_item(root, 9U));
    TEST_CHECK(real == 1.5);
    tinypy_marshal_complex_value(
        tinypy_marshal_sequence_item(root, 10U), &real, &imaginary);
    TEST_CHECK(real == 2.5 && imaginary == -3.5);
    tinypy_marshal_complex_value(
        tinypy_marshal_sequence_item(root, 11U), &real, &imaginary);
    TEST_CHECK(real == 2.0 && imaginary == -3.0);
    tinypy_marshal_bytes_view(
        tinypy_marshal_sequence_item(root, 12U), &bytes, &size, &interned);
    TEST_CHECK(size == 3U && ((const uint8_t *)bytes)[1] == 0U);
    TEST_CHECK(tinypy_marshal_sequence_item(root, 13U) == tinypy_marshal_sequence_item(root, 14U));
    tinypy_marshal_unicode_view(
        tinypy_marshal_sequence_item(root, 15U), &utf8, &size, &points);
    TEST_CHECK(size == 2U && points == 1U && (uint8_t)utf8[0] == UINT8_C(0xc3));
    TEST_CHECK(tinypy_marshal_object_type(tinypy_marshal_sequence_item(root, 16U)) == TINYPY_MARSHAL_TYPE_TUPLE);
    TEST_CHECK(tinypy_marshal_object_type(tinypy_marshal_sequence_item(root, 17U)) == TINYPY_MARSHAL_TYPE_LIST);
    TEST_CHECK(tinypy_marshal_object_type(tinypy_marshal_sequence_item(root, 18U)) == TINYPY_MARSHAL_TYPE_DICT);
    TEST_CHECK(tinypy_marshal_dict_size(tinypy_marshal_sequence_item(root, 18U)) == 1U);
    tinypy_marshal_bytes_view(
        tinypy_marshal_dict_key(tinypy_marshal_sequence_item(root, 18U), 0U),
        &bytes,
        &size,
        &interned);
    TEST_CHECK(size == 1U && ((const uint8_t *)bytes)[0] == (uint8_t)'k');
    integer_value = tinypy_marshal_integer_value(
        tinypy_marshal_dict_value(tinypy_marshal_sequence_item(root, 18U), 0U));
    TEST_CHECK(integer_value == 42);
    TEST_CHECK(tinypy_marshal_object_type(tinypy_marshal_sequence_item(root, 19U)) == TINYPY_MARSHAL_TYPE_SET);
    TEST_CHECK(tinypy_marshal_object_type(tinypy_marshal_sequence_item(root, 20U)) == TINYPY_MARSHAL_TYPE_FROZENSET);
    item = tinypy_marshal_sequence_item(root, 21U);
    TEST_CHECK(tinypy_marshal_object_type(item) == TINYPY_MARSHAL_TYPE_CODE);
    (void)memset(writer.bytes, 0xa5, writer.size);
    TEST_CHECK(__test_exact_dump(document, golden, golden_size, &state));
    tinypy_marshal_document_destroy(document);
    TEST_CHECK(state.live_allocations == 0U);
    return 1;
}

static int32_t __expect_error(test_writer_t *writer, tinypy_marshal_limits_t *limits, tinypy_marshal_result_e expected) {
    test_allocator_state_t state;
    tinypy_marshal_document_t *document = NULL;
    tinypy_marshal_error_t error;
    tinypy_marshal_result_e result;

    __test_state_init(&state);
    result = __read_writer(writer, &state, limits, &document, &error);
    TEST_CHECK(result == expected);
    TEST_CHECK(error.code == expected);
    TEST_CHECK(error.message != NULL && error.message_size != 0U);
    TEST_CHECK(document == NULL);
    TEST_CHECK(state.live_allocations == 0U);
    return 1;
}

static int32_t __test_truncation_and_offsets(void) {
    test_writer_t writer;
    size_t full_size;
    size_t prefix;

    (void)memset(&writer, 0, sizeof(writer));
    __writer_code(&writer, 1, 1, 0);
    full_size = writer.size;
    for (prefix = 0U; prefix != full_size; ++prefix) {
        size_t saved_size = writer.size;
        test_allocator_state_t state;
        tinypy_marshal_document_t *document = NULL;
        tinypy_marshal_error_t error;
        tinypy_marshal_result_e result;
        writer.size = prefix;
        __test_state_init(&state);
        result = __read_writer(&writer, &state, NULL, &document, &error);
        TEST_CHECK(result == TINYPY_MARSHAL_TRUNCATED);
        TEST_CHECK(error.offset == prefix);
        TEST_CHECK(document == NULL && state.live_allocations == 0U);
        writer.size = saved_size;
    }

    __writer_byte(&writer, UINT8_C(0xff));
    TEST_CHECK(__expect_error(&writer, NULL, TINYPY_MARSHAL_TRAILING_DATA));
    return 1;
}

static int32_t __test_malformed_values(void) {
    test_writer_t writer;

    (void)memset(&writer, 0, sizeof(writer));
    __writer_byte(&writer, (uint8_t)'s');
    __writer_i32(&writer, -1);
    TEST_CHECK(__expect_error(&writer, NULL, TINYPY_MARSHAL_INVALID_SIZE));

    (void)memset(&writer, 0, sizeof(writer));
    __writer_byte(&writer, (uint8_t)'l');
    __writer_i32(&writer, 1);
    __writer_u16(&writer, UINT16_C(32768));
    TEST_CHECK(__expect_error(&writer, NULL, TINYPY_MARSHAL_INVALID_LONG));

    (void)memset(&writer, 0, sizeof(writer));
    __writer_byte(&writer, (uint8_t)'l');
    __writer_i32(&writer, 1);
    __writer_u16(&writer, 0U);
    TEST_CHECK(__expect_error(&writer, NULL, TINYPY_MARSHAL_INVALID_LONG));

    (void)memset(&writer, 0, sizeof(writer));
    __writer_byte(&writer, (uint8_t)'R');
    __writer_i32(&writer, 0);
    TEST_CHECK(__expect_error(&writer, NULL, TINYPY_MARSHAL_INVALID_STRING_REF));

    (void)memset(&writer, 0, sizeof(writer));
    __writer_byte(&writer, (uint8_t)'u');
    __writer_i32(&writer, 2);
    __writer_byte(&writer, UINT8_C(0xc0));
    __writer_byte(&writer, UINT8_C(0x80));
    TEST_CHECK(__expect_error(&writer, NULL, TINYPY_MARSHAL_INVALID_UTF8));

    (void)memset(&writer, 0, sizeof(writer));
    __writer_byte(&writer, (uint8_t)'(');
    __writer_i32(&writer, 1);
    __writer_byte(&writer, (uint8_t)'0');
    TEST_CHECK(__expect_error(&writer, NULL, TINYPY_MARSHAL_NULL_OUTSIDE_DICT));

    (void)memset(&writer, 0, sizeof(writer));
    __writer_code(&writer, 0, 0, 1);
    TEST_CHECK(__expect_error(&writer, NULL, TINYPY_MARSHAL_INVALID_CODE));

    (void)memset(&writer, 0, sizeof(writer));
    __writer_byte(&writer, (uint8_t)'?');
    TEST_CHECK(__expect_error(&writer, NULL, TINYPY_MARSHAL_UNKNOWN_TYPE));

    (void)memset(&writer, 0, sizeof(writer));
    __writer_byte(&writer, (uint8_t)'0');
    TEST_CHECK(__expect_error(&writer, NULL, TINYPY_MARSHAL_NULL_OUTSIDE_DICT));

    (void)memset(&writer, 0, sizeof(writer));
    __writer_byte(&writer, (uint8_t)'{');
    __writer_byte(&writer, (uint8_t)'N');
    __writer_byte(&writer, (uint8_t)'0');
    TEST_CHECK(__expect_error(&writer, NULL, TINYPY_MARSHAL_NULL_OUTSIDE_DICT));

    (void)memset(&writer, 0, sizeof(writer));
    __writer_text_float(&writer, (uint8_t)'f', "1x2");
    TEST_CHECK(__expect_error(&writer, NULL, TINYPY_MARSHAL_INVALID_FLOAT));
    return 1;
}

static int32_t __test_argument_and_abi_errors(void) {
    static const uint8_t none_object[] = {(uint8_t)'N'};
    test_allocator_state_t state;
    tinypy_allocator_t allocator;
    tinypy_marshal_document_t *document = NULL;
    tinypy_marshal_error_t error;

    __test_state_init(&state);
    allocator = __test_allocator(&state);
    allocator.abi_version += 1U;
    TEST_CHECK(
        tinypy_marshal_read_v2(
            none_object,
            sizeof(none_object),
            &allocator,
            NULL,
            &document,
            &error) == TINYPY_MARSHAL_ABI_MISMATCH);
    TEST_CHECK(error.code == TINYPY_MARSHAL_ABI_MISMATCH);
    TEST_CHECK(document == NULL && state.calls == 0U);

    return 1;
}

static int32_t __test_limits(void) {
    test_writer_t writer;
    tinypy_marshal_limits_t limits;

    (void)memset(&writer, 0, sizeof(writer));
    __writer_byte(&writer, (uint8_t)'(');
    __writer_i32(&writer, 1);
    __writer_byte(&writer, (uint8_t)'(');
    __writer_i32(&writer, 1);
    __writer_byte(&writer, (uint8_t)'(');
    __writer_i32(&writer, 1);
    __writer_byte(&writer, (uint8_t)'N');

    tinypy_marshal_limits_init(&limits);
    limits.max_depth = 2U;
    TEST_CHECK(__expect_error(&writer, &limits, TINYPY_MARSHAL_DEPTH_LIMIT));

    tinypy_marshal_limits_init(&limits);
    limits.max_objects = 2U;
    TEST_CHECK(__expect_error(&writer, &limits, TINYPY_MARSHAL_OBJECT_LIMIT));

    tinypy_marshal_limits_init(&limits);
    limits.max_container_items = 0U; {
        test_allocator_state_t state;
        tinypy_marshal_document_t *document = NULL;
        tinypy_marshal_error_t error;
        __test_state_init(&state);
        TEST_CHECK(__read_writer(&writer, &state, &limits, &document, &error) == TINYPY_MARSHAL_INVALID_ARGUMENT);
        TEST_CHECK(state.live_allocations == 0U);
    }

    (void)memset(&writer, 0, sizeof(writer));
    __writer_byte(&writer, (uint8_t)'(');
    __writer_i32(&writer, 2);
    __writer_byte(&writer, (uint8_t)'N');
    __writer_byte(&writer, (uint8_t)'N');
    tinypy_marshal_limits_init(&limits);
    limits.max_container_items = 0x1U;
    TEST_CHECK(__expect_error(&writer, &limits, TINYPY_MARSHAL_CONTAINER_LIMIT));

    (void)memset(&writer, 0, sizeof(writer));
    __writer_bytes(&writer, "abcd", 4U);
    tinypy_marshal_limits_init(&limits);
    limits.max_string_bytes = 3U;
    TEST_CHECK(__expect_error(&writer, &limits, TINYPY_MARSHAL_STRING_LIMIT));

    tinypy_marshal_limits_init(&limits);
    limits.max_input_bytes = writer.size - 1U;
    TEST_CHECK(__expect_error(&writer, &limits, TINYPY_MARSHAL_BYTE_LIMIT));

    tinypy_marshal_limits_init(&limits);
    limits.max_allocated_bytes = 1U;
    TEST_CHECK(__expect_error(&writer, &limits, TINYPY_MARSHAL_BYTE_LIMIT));
    return 1;
}

static int32_t __test_writer_limits_and_structured_errors(void) {
    test_writer_t writer;
    test_allocator_state_t state;
    tinypy_marshal_document_t *document = NULL;
    tinypy_marshal_error_t error;
    tinypy_marshal_write_options_t options;
    size_t required_size = 0U;
    size_t calls_before;

    (void)memset(&writer, 0, sizeof(writer));
    __writer_code(&writer, 1, 1, 0);
    __test_state_init(&state);
    TEST_CHECK(__read_writer(&writer, &state, NULL, &document, &error) == TINYPY_MARSHAL_OK);
    TEST_CHECK(
        tinypy_marshal_write_v2(
            document,
            NULL,
            0U,
            &required_size,
            NULL,
            &error) == TINYPY_MARSHAL_OK);

    calls_before = state.calls;
    tinypy_marshal_write_options_init(&options);
    options.max_output_bytes = required_size - 1U;
    TEST_CHECK(
        tinypy_marshal_write_v2(
            document,
            NULL,
            0U,
            &required_size,
            &options,
            &error) == TINYPY_MARSHAL_OUTPUT_LIMIT);
    TEST_CHECK(error.code == TINYPY_MARSHAL_OUTPUT_LIMIT);
    TEST_CHECK(error.object != NULL);
    TEST_CHECK(error.object_index != SIZE_MAX);
    TEST_CHECK(error.wire_type != 0U);
    TEST_CHECK(state.calls == calls_before);

    tinypy_marshal_write_options_init(&options);
    options.max_depth = 2U;
    TEST_CHECK(
        tinypy_marshal_write_v2(
            document,
            NULL,
            0U,
            &required_size,
            &options,
            &error) == TINYPY_MARSHAL_DEPTH_LIMIT);
    TEST_CHECK(error.code == TINYPY_MARSHAL_DEPTH_LIMIT);
    TEST_CHECK(error.object != NULL);
    TEST_CHECK(error.object_index != SIZE_MAX);
    TEST_CHECK(state.calls == calls_before);

    tinypy_marshal_write_options_init(&options);
    options.abi_version += 1U;
    TEST_CHECK(
        tinypy_marshal_write_v2(
            document,
            NULL,
            0U,
            &required_size,
            &options,
            &error) == TINYPY_MARSHAL_ABI_MISMATCH);
    TEST_CHECK(state.calls == calls_before);

    tinypy_marshal_document_destroy(document);
    TEST_CHECK(state.live_allocations == 0U);
    return 1;
}

typedef int32_t (*test_function_t)(void);

typedef struct test_case_t {
    const char *name;
    test_function_t function;
} test_case_t;

int main(void) {
    static const test_case_t tests[] = { {"nested_code_and_interns", __test_nested_code_and_interns},
        {"all_wire_types", __test_all_wire_types}, {"truncation_and_offsets", __test_truncation_and_offsets},
        {"malformed_values", __test_malformed_values}, {"argument_and_abi_errors", __test_argument_and_abi_errors},
        {"limits", __test_limits}, {"writer_limits_and_structured_errors", __test_writer_limits_and_structured_errors},
    };
    size_t index;

    for (index = 0U; index != sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].function()) {
            (void)fprintf(stderr, "FAILED: %s\n", tests[index].name);
            return 1;
        }
        (void)printf("ok: %s\n", tests[index].name);
    }
    return 0;
}
