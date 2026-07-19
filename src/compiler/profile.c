#include "tinypy/compiler.h"

#include "../artifact/sha256.h"

#include <assert.h>
#include <string.h>

#define TINYPY_BUILD_PROFILE_STATE UINT32_C(0x54505950)
#define TINYPY_PROFILE_DEFAULT_ALLOCATED_BYTES ((size_t)64U * 1024U * 1024U)
#define TINYPY_PROFILE_DEFAULT_CONSTANTS ((size_t)4096U)
#define TINYPY_PROFILE_DEFAULT_VALUE_NODES ((size_t)65536U)
#define TINYPY_PROFILE_DEFAULT_DEPTH ((size_t)64U)
#define TINYPY_PROFILE_DEFAULT_STRING_BYTES ((size_t)16U * 1024U * 1024U)
#define TINYPY_PROFILE_DEFAULT_TUPLE_ITEMS ((size_t)65536U)

typedef union tinypy_profile_max_align_t {
    void *pointer_value;
    void (*function_value)(void);
    int64_t integer_value;
    long double floating_value;
} tinypy_profile_max_align_t;

typedef struct tinypy_profile_alignment_probe_t {
    char prefix;
    tinypy_profile_max_align_t value;
} tinypy_profile_alignment_probe_t;

#define TINYPY_PROFILE_ALIGNMENT \
    ((size_t)offsetof(tinypy_profile_alignment_probe_t, value))

typedef struct tinypy_profile_allocation_t {
    struct tinypy_profile_allocation_t *next;
    size_t total_size;
    tinypy_profile_max_align_t alignment;
} tinypy_profile_allocation_t;

typedef struct tinypy_profile_constant_t {
    char *name;
    size_t name_size;
    tinypy_build_value_t value;
} tinypy_profile_constant_t;

struct tinypy_build_profile_t {
    uint32_t state;
    int optimize_level;
    tinypy_allocator_t allocator;
    tinypy_build_profile_limits_t limits;
    size_t allocated_bytes;
    size_t value_node_count;
    size_t string_bytes;
    size_t tuple_items;
    size_t constant_count;
    tinypy_profile_constant_t *constants;
    tinypy_profile_allocation_t *allocations;
    uint8_t digest[32];
};

static size_t __tinypy_profile_cstring_size(const char *message)
{
    size_t size = 0U;
    while (message[size] != '\0') {
        size += 1U;
    }
    return size;
}

static void __tinypy_profile_error_clear(tinypy_build_profile_error_t *error)
{
    if (error == NULL) {
        return;
    }
    (void)memset(error, 0, sizeof(*error));
    error->abi_version = TINYPY_COMPILER_ABI_VERSION;
    error->struct_size = (uint32_t)sizeof(*error);
    error->constant_index = SIZE_MAX;
}

static tinypy_build_profile_result_e __tinypy_profile_fail(
    tinypy_build_profile_error_t *error,
    tinypy_build_profile_result_e code,
    size_t constant_index,
    size_t value_depth,
    const char *message)
{
    if (error != NULL) {
        __tinypy_profile_error_clear(error);
        error->code = code;
        error->constant_index = constant_index;
        error->value_depth = value_depth;
        error->message = message;
        error->message_size = __tinypy_profile_cstring_size(message);
    }
    return code;
}

static int __tinypy_profile_allocator_abi_valid(const tinypy_allocator_t *allocator)
{
    const size_t minimum_size =
        offsetof(tinypy_allocator_t, deallocate) + sizeof(allocator->deallocate);
    return allocator->abi_version == TINYPY_ABI_VERSION &&
        (size_t)allocator->struct_size >= minimum_size;
}

void tinypy_build_value_init(
    tinypy_build_value_t *value,
    tinypy_build_value_type_e type)
{
    assert(value != NULL);
    (void)memset(value, 0, sizeof(*value));
    value->abi_version = TINYPY_COMPILER_ABI_VERSION;
    value->struct_size = (uint32_t)sizeof(*value);
    value->type = (uint32_t)type;
}

void tinypy_build_profile_limits_init(tinypy_build_profile_limits_t *limits)
{
    assert(limits != NULL);
    (void)memset(limits, 0, sizeof(*limits));
    limits->abi_version = TINYPY_COMPILER_ABI_VERSION;
    limits->struct_size = (uint32_t)sizeof(*limits);
    limits->max_allocated_bytes = TINYPY_PROFILE_DEFAULT_ALLOCATED_BYTES;
    limits->max_constants = TINYPY_PROFILE_DEFAULT_CONSTANTS;
    limits->max_value_nodes = TINYPY_PROFILE_DEFAULT_VALUE_NODES;
    limits->max_depth = TINYPY_PROFILE_DEFAULT_DEPTH;
    limits->max_string_bytes = TINYPY_PROFILE_DEFAULT_STRING_BYTES;
    limits->max_tuple_items = TINYPY_PROFILE_DEFAULT_TUPLE_ITEMS;
}

static int __tinypy_profile_limits_valid(const tinypy_build_profile_limits_t *limits)
{
    return limits != NULL &&
        limits->abi_version == TINYPY_COMPILER_ABI_VERSION &&
        limits->struct_size >= (uint32_t)sizeof(*limits) &&
        limits->max_allocated_bytes != 0U &&
        limits->max_constants != 0U &&
        limits->max_value_nodes != 0U &&
        limits->max_depth != 0U &&
        limits->max_string_bytes != 0U &&
        limits->max_tuple_items != 0U;
}

int tinypy_preprocessor_name_is_reserved(
    const char *name,
    size_t name_size)
{
    size_t index;
    assert(name != NULL || name_size == 0U);
    if (name_size < 5U ||
        name[0] != '_' || name[1] != '_' ||
        name[name_size - 2U] != '_' || name[name_size - 1U] != '_' ||
        name[2] < 'A' || name[2] > 'Z') {
        return 0;
    }
    for (index = 3U; index + 2U < name_size; ++index) {
        char character = name[index];
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_')) {
            return 0;
        }
    }
    return 1;
}

static int __tinypy_profile_add_size(size_t left, size_t right, size_t *out)
{
    if (right > SIZE_MAX - left) {
        return 0;
    }
    *out = left + right;
    return 1;
}

static int __tinypy_profile_multiply_size(size_t left, size_t right, size_t *out)
{
    if (left != 0U && right > SIZE_MAX / left) {
        return 0;
    }
    *out = left * right;
    return 1;
}

static void *__tinypy_profile_allocate(
    tinypy_build_profile_t *profile,
    size_t payload_size,
    size_t constant_index,
    size_t depth,
    tinypy_build_profile_error_t *error,
    tinypy_build_profile_result_e *out_result)
{
    size_t total_size;
    size_t new_allocated;
    tinypy_profile_allocation_t *allocation;

    if (!__tinypy_profile_add_size(
            sizeof(tinypy_profile_allocation_t), payload_size, &total_size) ||
        !__tinypy_profile_add_size(
            profile->allocated_bytes, total_size, &new_allocated)) {
        *out_result = __tinypy_profile_fail(
            error,
            TINYPY_BUILD_PROFILE_SIZE_OVERFLOW,
            constant_index,
            depth,
            "build profile allocation size overflow");
        return NULL;
    }
    if (new_allocated > profile->limits.max_allocated_bytes) {
        *out_result = __tinypy_profile_fail(
            error,
            TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED,
            constant_index,
            depth,
            "build profile allocation limit exceeded");
        return NULL;
    }
    allocation = (tinypy_profile_allocation_t *)profile->allocator.allocate(
        profile->allocator.user_data,
        total_size,
        TINYPY_PROFILE_ALIGNMENT,
        (uint32_t)TINYPY_BUILD_PROFILE_ALLOC_TAG_DATA);
    assert(allocation != NULL);
    allocation->next = profile->allocations;
    allocation->total_size = total_size;
    profile->allocations = allocation;
    profile->allocated_bytes = new_allocated;
    (void)memset((void *)(allocation + 1), 0, payload_size);
    *out_result = TINYPY_BUILD_PROFILE_OK;
    return (void *)(allocation + 1);
}

static int __tinypy_profile_valid_utf8(const unsigned char *bytes, size_t size)
{
    size_t index = 0U;
    while (index < size) {
        unsigned char first = bytes[index++];
        uint32_t code_point;
        size_t continuation_count;
        size_t continuation;

        if (first <= 0x7fU) {
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            code_point = (uint32_t)(first & 0x1fU);
            continuation_count = 1U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            code_point = (uint32_t)(first & 0x0fU);
            continuation_count = 2U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            code_point = (uint32_t)(first & 0x07U);
            continuation_count = 3U;
        } else {
            return 0;
        }
        if (continuation_count > size - index) {
            return 0;
        }
        for (continuation = 0U;
             continuation != continuation_count;
             ++continuation) {
            unsigned char next = bytes[index++];
            if ((next & 0xc0U) != 0x80U) {
                return 0;
            }
            code_point = (code_point << 6U) | (uint32_t)(next & 0x3fU);
        }
        if ((continuation_count == 2U && code_point < UINT32_C(0x800)) ||
            (continuation_count == 3U && code_point < UINT32_C(0x10000)) ||
            (code_point >= UINT32_C(0xd800) &&
             code_point <= UINT32_C(0xdfff)) ||
            code_point > UINT32_C(0x10ffff)) {
            return 0;
        }
    }
    return 1;
}

static int __tinypy_profile_value_descriptor_valid(
    const tinypy_build_value_t *value)
{
    assert(value != NULL);
    return value->abi_version == TINYPY_COMPILER_ABI_VERSION &&
        value->struct_size >= (uint32_t)sizeof(*value) &&
        value->reserved == 0U && value->reserved2 == 0U;
}

static tinypy_build_profile_result_e __tinypy_profile_copy_value(
    tinypy_build_profile_t *profile,
    const tinypy_build_value_t *source,
    tinypy_build_value_t *destination,
    size_t constant_index,
    size_t depth,
    tinypy_build_profile_error_t *error)
{
    tinypy_build_profile_result_e result;

    assert(source != NULL);
    if (!__tinypy_profile_value_descriptor_valid(source)) {
        return __tinypy_profile_fail(
            error,
            source->abi_version != TINYPY_COMPILER_ABI_VERSION
                ? TINYPY_BUILD_PROFILE_ABI_MISMATCH
                : TINYPY_BUILD_PROFILE_INVALID_ARGUMENT,
            constant_index,
            depth,
            "invalid build constant value descriptor");
    }
    if (depth > profile->limits.max_depth) {
        return __tinypy_profile_fail(
            error,
            TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED,
            constant_index,
            depth,
            "build constant tuple depth limit exceeded");
    }
    if (profile->value_node_count == profile->limits.max_value_nodes) {
        return __tinypy_profile_fail(
            error,
            TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED,
            constant_index,
            depth,
            "build constant value tinypy_cst_node_t limit exceeded");
    }
    profile->value_node_count += 1U;
    tinypy_build_value_init(destination, (tinypy_build_value_type_e)source->type);

    switch ((tinypy_build_value_type_e)source->type) {
    case TINYPY_BUILD_VALUE_NONE:
        return TINYPY_BUILD_PROFILE_OK;
    case TINYPY_BUILD_VALUE_BOOL:
        if (source->integer_value != 0 && source->integer_value != 1) {
            return __tinypy_profile_fail(
                error,
                TINYPY_BUILD_PROFILE_INVALID_VALUE,
                constant_index,
                depth,
                "build bool must be zero or one");
        }
        destination->integer_value = source->integer_value;
        return TINYPY_BUILD_PROFILE_OK;
    case TINYPY_BUILD_VALUE_INTEGER:
        destination->integer_value = source->integer_value;
        return TINYPY_BUILD_PROFILE_OK;
    case TINYPY_BUILD_VALUE_FLOAT:
        (void)memcpy(
            &destination->float_value,
            &source->float_value,
            sizeof(destination->float_value));
        return TINYPY_BUILD_PROFILE_OK;
    case TINYPY_BUILD_VALUE_LONG:
        if (source->long_sign < -1 || source->long_sign > 1 ||
            (source->long_sign == 0) != (source->long_digit_count == 0U) ||
            (source->long_digit_count != 0U &&
             (source->long_digits == NULL ||
              source->long_digits[source->long_digit_count - 1U] == 0U))) {
            return __tinypy_profile_fail(
                error,
                TINYPY_BUILD_PROFILE_INVALID_LONG,
                constant_index,
                depth,
                "build long is not canonical");
        }
        if (source->long_digit_count != 0U) {
            size_t digit_bytes;
            uint16_t *digits;
            size_t index;
            if (!__tinypy_profile_multiply_size(
                    source->long_digit_count,
                    sizeof(uint16_t),
                    &digit_bytes)) {
                return __tinypy_profile_fail(
                    error,
                    TINYPY_BUILD_PROFILE_SIZE_OVERFLOW,
                    constant_index,
                    depth,
                    "build long digit size overflow");
            }
            for (index = 0U; index != source->long_digit_count; ++index) {
                if (source->long_digits[index] > UINT16_C(0x7fff)) {
                    return __tinypy_profile_fail(
                        error,
                        TINYPY_BUILD_PROFILE_INVALID_LONG,
                        constant_index,
                        depth,
                        "build long digit is outside base 2^15");
                }
            }
            digits = (uint16_t *)__tinypy_profile_allocate(
                profile,
                digit_bytes,
                constant_index,
                depth,
                error,
                &result);
            if (digits == NULL) {
                return result;
            }
            (void)memcpy(digits, source->long_digits, digit_bytes);
            destination->long_digits = digits;
        }
        destination->long_sign = source->long_sign;
        destination->long_digit_count = source->long_digit_count;
        return TINYPY_BUILD_PROFILE_OK;
    case TINYPY_BUILD_VALUE_STRING:
    case TINYPY_BUILD_VALUE_UNICODE:
        {
            size_t allocation_size;
            unsigned char *copy;
            if (source->data == NULL && source->data_size != 0U) {
                return __tinypy_profile_fail(
                    error,
                    TINYPY_BUILD_PROFILE_INVALID_VALUE,
                    constant_index,
                    depth,
                    "build string has NULL data");
            }
            if (source->data_size >
                profile->limits.max_string_bytes - profile->string_bytes) {
                return __tinypy_profile_fail(
                    error,
                    TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED,
                    constant_index,
                    depth,
                    "build string byte limit exceeded");
            }
            if (source->type == (uint32_t)TINYPY_BUILD_VALUE_UNICODE &&
                !__tinypy_profile_valid_utf8(
                    (const unsigned char *)source->data,
                    source->data_size)) {
                return __tinypy_profile_fail(
                    error,
                    TINYPY_BUILD_PROFILE_INVALID_UTF8,
                    constant_index,
                    depth,
                    "build unicode is not canonical UTF-8");
            }
            if (!__tinypy_profile_add_size(source->data_size, 1U, &allocation_size)) {
                return __tinypy_profile_fail(
                    error,
                    TINYPY_BUILD_PROFILE_SIZE_OVERFLOW,
                    constant_index,
                    depth,
                    "build string allocation size overflow");
            }
            copy = (unsigned char *)__tinypy_profile_allocate(
                profile,
                allocation_size,
                constant_index,
                depth,
                error,
                &result);
            if (copy == NULL) {
                return result;
            }
            if (source->data_size != 0U) {
                (void)memcpy(copy, source->data, source->data_size);
            }
            copy[source->data_size] = 0U;
            destination->data = copy;
            destination->data_size = source->data_size;
            profile->string_bytes += source->data_size;
            return TINYPY_BUILD_PROFILE_OK;
        }
    case TINYPY_BUILD_VALUE_TUPLE:
        {
            size_t item_bytes;
            tinypy_build_value_t *items;
            size_t index;
            if (source->items == NULL && source->item_count != 0U) {
                return __tinypy_profile_fail(
                    error,
                    TINYPY_BUILD_PROFILE_INVALID_VALUE,
                    constant_index,
                    depth,
                    "build tuple has NULL items");
            }
            if (source->item_count >
                profile->limits.max_tuple_items - profile->tuple_items) {
                return __tinypy_profile_fail(
                    error,
                    TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED,
                    constant_index,
                    depth,
                    "build tuple item limit exceeded");
            }
            if (!__tinypy_profile_multiply_size(
                    source->item_count,
                    sizeof(tinypy_build_value_t),
                    &item_bytes)) {
                return __tinypy_profile_fail(
                    error,
                    TINYPY_BUILD_PROFILE_SIZE_OVERFLOW,
                    constant_index,
                    depth,
                    "build tuple allocation size overflow");
            }
            profile->tuple_items += source->item_count;
            if (source->item_count == 0U) {
                return TINYPY_BUILD_PROFILE_OK;
            }
            items = (tinypy_build_value_t *)__tinypy_profile_allocate(
                profile,
                item_bytes,
                constant_index,
                depth,
                error,
                &result);
            if (items == NULL) {
                return result;
            }
            destination->items = items;
            destination->item_count = source->item_count;
            for (index = 0U; index != source->item_count; ++index) {
                result = __tinypy_profile_copy_value(
                    profile,
                    &source->items[index],
                    &items[index],
                    constant_index,
                    depth + 1U,
                    error);
                if (result != TINYPY_BUILD_PROFILE_OK) {
                    return result;
                }
            }
            return TINYPY_BUILD_PROFILE_OK;
        }
    default:
        return __tinypy_profile_fail(
            error,
            TINYPY_BUILD_PROFILE_INVALID_VALUE_TYPE,
            constant_index,
            depth,
            "unsupported build constant value type");
    }
}

static int __tinypy_profile_name_compare(
    const char *left,
    size_t left_size,
    const char *right,
    size_t right_size)
{
    size_t common_size = left_size < right_size ? left_size : right_size;
    int compared = memcmp(left, right, common_size);
    if (compared != 0) {
        return compared;
    }
    if (left_size < right_size) {
        return -1;
    }
    if (left_size > right_size) {
        return 1;
    }
    return 0;
}

static int __tinypy_profile_name_is_ndebug(const char *name, size_t size)
{
    static const char ndebug_name[] = "__NDEBUG__";
    return size == sizeof(ndebug_name) - 1U &&
        memcmp(name, ndebug_name, sizeof(ndebug_name) - 1U) == 0;
}

static tinypy_build_profile_result_e __tinypy_profile_copy_constant(
    tinypy_build_profile_t *profile,
    const tinypy_build_constant_t *source,
    size_t index,
    tinypy_profile_constant_t *destination,
    tinypy_build_profile_error_t *error)
{
    size_t name_allocation_size;
    tinypy_build_profile_result_e result;

    assert(source != NULL);
    if (source->abi_version != TINYPY_COMPILER_ABI_VERSION ||
        source->struct_size < (uint32_t)sizeof(*source)) {
        return __tinypy_profile_fail(
            error,
            source->abi_version != TINYPY_COMPILER_ABI_VERSION
                ? TINYPY_BUILD_PROFILE_ABI_MISMATCH
                : TINYPY_BUILD_PROFILE_INVALID_ARGUMENT,
            index,
            0U,
            "invalid build constant descriptor");
    }
    if (!tinypy_preprocessor_name_is_reserved(source->name, source->name_size)) {
        return __tinypy_profile_fail(
            error,
            TINYPY_BUILD_PROFILE_INVALID_NAME,
            index,
            0U,
            "build constant name must match ^__[A-Z][A-Z0-9_]*__$");
    }
    if (__tinypy_profile_name_is_ndebug(source->name, source->name_size)) {
        return __tinypy_profile_fail(
            error,
            TINYPY_BUILD_PROFILE_NDEBUG_OVERRIDE,
            index,
            0U,
            "__NDEBUG__ is derived from optimize level");
    }
    if (!__tinypy_profile_add_size(source->name_size, 1U, &name_allocation_size)) {
        return __tinypy_profile_fail(
            error,
            TINYPY_BUILD_PROFILE_SIZE_OVERFLOW,
            index,
            0U,
            "build constant name size overflow");
    }
    destination->name = (char *)__tinypy_profile_allocate(
        profile,
        name_allocation_size,
        index,
        0U,
        error,
        &result);
    if (destination->name == NULL) {
        return result;
    }
    (void)memcpy(destination->name, source->name, source->name_size);
    destination->name[source->name_size] = '\0';
    destination->name_size = source->name_size;
    return __tinypy_profile_copy_value(
        profile,
        &source->value,
        &destination->value,
        index,
        0U,
        error);
}

static void __tinypy_profile_digest_u8(
    tinypy_sha256_context_t *context,
    uint8_t value)
{
    tinypy_sha256_update(context, &value, 1U);
}

static void __tinypy_profile_digest_u16(
    tinypy_sha256_context_t *context,
    uint16_t value)
{
    uint8_t bytes[2];
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    tinypy_sha256_update(context, bytes, sizeof(bytes));
}

static void __tinypy_profile_digest_u32(
    tinypy_sha256_context_t *context,
    uint32_t value)
{
    uint8_t bytes[4];
    size_t index;
    for (index = 0U; index != 4U; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8U));
    }
    tinypy_sha256_update(context, bytes, sizeof(bytes));
}

static void __tinypy_profile_digest_u64(
    tinypy_sha256_context_t *context,
    uint64_t value)
{
    uint8_t bytes[8];
    size_t index;
    for (index = 0U; index != 8U; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8U));
    }
    tinypy_sha256_update(context, bytes, sizeof(bytes));
}

static void __tinypy_profile_digest_value(
    tinypy_sha256_context_t *context,
    const tinypy_build_value_t *value)
{
    size_t index;
    __tinypy_profile_digest_u8(context, (uint8_t)value->type);
    switch ((tinypy_build_value_type_e)value->type) {
    case TINYPY_BUILD_VALUE_NONE:
        break;
    case TINYPY_BUILD_VALUE_BOOL:
        __tinypy_profile_digest_u8(context, (uint8_t)value->integer_value);
        break;
    case TINYPY_BUILD_VALUE_INTEGER:
        __tinypy_profile_digest_u64(context, (uint64_t)value->integer_value);
        break;
    case TINYPY_BUILD_VALUE_LONG:
        __tinypy_profile_digest_u8(context, (uint8_t)value->long_sign);
        __tinypy_profile_digest_u64(context, (uint64_t)value->long_digit_count);
        for (index = 0U; index != value->long_digit_count; ++index) {
            __tinypy_profile_digest_u16(context, value->long_digits[index]);
        }
        break;
    case TINYPY_BUILD_VALUE_FLOAT:
        {
            uint64_t bits;
            (void)memcpy(&bits, &value->float_value, sizeof(bits));
            __tinypy_profile_digest_u64(context, bits);
        }
        break;
    case TINYPY_BUILD_VALUE_STRING:
    case TINYPY_BUILD_VALUE_UNICODE:
        __tinypy_profile_digest_u64(context, (uint64_t)value->data_size);
        tinypy_sha256_update(context, value->data, value->data_size);
        break;
    case TINYPY_BUILD_VALUE_TUPLE:
        __tinypy_profile_digest_u64(context, (uint64_t)value->item_count);
        for (index = 0U; index != value->item_count; ++index) {
            __tinypy_profile_digest_value(context, &value->items[index]);
        }
        break;
    default:
        break;
    }
}

static void __tinypy_profile_compute_digest(tinypy_build_profile_t *profile)
{
    static const uint8_t prefix[] = {
        'T', 'P', 'Y', 'B', 'U', 'I', 'L', 'D', 0U
    };
    tinypy_sha256_context_t context;
    size_t index;
    tinypy_sha256_initialize(&context);
    tinypy_sha256_update(&context, prefix, sizeof(prefix));
    __tinypy_profile_digest_u32(&context, TINYPY_COMPILER_ABI_VERSION);
    __tinypy_profile_digest_u32(&context, TINYPY_PREPROCESSOR_ABI_VERSION);
    __tinypy_profile_digest_u8(&context, (uint8_t)profile->optimize_level);
    __tinypy_profile_digest_u64(&context, (uint64_t)profile->constant_count);
    for (index = 0U; index != profile->constant_count; ++index) {
        const tinypy_profile_constant_t *constant = &profile->constants[index];
        __tinypy_profile_digest_u64(&context, (uint64_t)constant->name_size);
        tinypy_sha256_update(&context, constant->name, constant->name_size);
        __tinypy_profile_digest_value(&context, &constant->value);
    }
    tinypy_sha256_finalize(&context, profile->digest);
}

static void __tinypy_profile_destroy_partial(tinypy_build_profile_t *profile)
{
    tinypy_allocator_t allocator;
    tinypy_profile_allocation_t *allocation;
    assert(profile != NULL);
    allocator = profile->allocator;
    allocation = profile->allocations;
    while (allocation != NULL) {
        tinypy_profile_allocation_t *next = allocation->next;
        allocator.deallocate(
            allocator.user_data,
            allocation,
            allocation->total_size,
            TINYPY_PROFILE_ALIGNMENT,
            (uint32_t)TINYPY_BUILD_PROFILE_ALLOC_TAG_DATA);
        allocation = next;
    }
    allocator.deallocate(
        allocator.user_data,
        profile,
        sizeof(*profile),
        TINYPY_PROFILE_ALIGNMENT,
        (uint32_t)TINYPY_BUILD_PROFILE_ALLOC_TAG_PROFILE);
}

tinypy_build_profile_result_e tinypy_build_profile_create(
    const tinypy_allocator_t *allocator,
    int optimize_level,
    const tinypy_build_constant_t *constants,
    size_t constant_count,
    const tinypy_build_profile_limits_t *limits,
    tinypy_build_profile_t **out_profile,
    tinypy_build_profile_error_t *out_error)
{
    tinypy_build_profile_limits_t default_limits;
    const tinypy_build_profile_limits_t *effective_limits = limits;
    tinypy_build_profile_t *profile;
    size_t total_constants;
    size_t constant_bytes;
    size_t index;
    tinypy_build_profile_result_e result;

    assert(allocator != NULL);
    assert(out_profile != NULL);
    assert(constants != NULL || constant_count == 0U);
    __tinypy_profile_error_clear(out_error);
    *out_profile = NULL;
    if (!__tinypy_profile_allocator_abi_valid(allocator)) {
        return __tinypy_profile_fail(
            out_error,
            TINYPY_BUILD_PROFILE_ABI_MISMATCH,
            SIZE_MAX,
            0U,
            "invalid build profile allocator");
    }
    assert(allocator->allocate != NULL);
    assert(allocator->deallocate != NULL);
    if (optimize_level < 0 || optimize_level > 2) {
        return __tinypy_profile_fail(
            out_error,
            TINYPY_BUILD_PROFILE_INVALID_OPTIMIZE,
            SIZE_MAX,
            0U,
            "optimize level must be 0, 1 or 2");
    }
    if (effective_limits == NULL) {
        tinypy_build_profile_limits_init(&default_limits);
        effective_limits = &default_limits;
    }
    if (!__tinypy_profile_limits_valid(effective_limits)) {
        return __tinypy_profile_fail(
            out_error,
            effective_limits != NULL &&
                effective_limits->abi_version !=
                    TINYPY_COMPILER_ABI_VERSION
                ? TINYPY_BUILD_PROFILE_ABI_MISMATCH
                : TINYPY_BUILD_PROFILE_INVALID_ARGUMENT,
            SIZE_MAX,
            0U,
            "invalid build profile limits");
    }
    if (!__tinypy_profile_add_size(constant_count, 1U, &total_constants) ||
        !__tinypy_profile_multiply_size(
            total_constants, sizeof(tinypy_profile_constant_t), &constant_bytes)) {
        return __tinypy_profile_fail(
            out_error,
            TINYPY_BUILD_PROFILE_SIZE_OVERFLOW,
            SIZE_MAX,
            0U,
            "build profile constant count overflow");
    }
    if (total_constants > effective_limits->max_constants ||
        sizeof(*profile) > effective_limits->max_allocated_bytes) {
        return __tinypy_profile_fail(
            out_error,
            TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED,
            SIZE_MAX,
            0U,
            "build profile constant/allocation limit exceeded");
    }

    profile = (tinypy_build_profile_t *)allocator->allocate(
        allocator->user_data,
        sizeof(*profile),
        TINYPY_PROFILE_ALIGNMENT,
        (uint32_t)TINYPY_BUILD_PROFILE_ALLOC_TAG_PROFILE);
    assert(profile != NULL);
    (void)memset(profile, 0, sizeof(*profile));
    profile->state = TINYPY_BUILD_PROFILE_STATE;
    profile->optimize_level = optimize_level;
    profile->allocator = *allocator;
    profile->limits = *effective_limits;
    profile->allocated_bytes = sizeof(*profile);
    profile->constant_count = total_constants;
    profile->constants = (tinypy_profile_constant_t *)__tinypy_profile_allocate(
        profile,
        constant_bytes,
        SIZE_MAX,
        0U,
        out_error,
        &result);
    if (profile->constants == NULL) {
        __tinypy_profile_destroy_partial(profile);
        return result;
    }

    for (index = 0U; index != constant_count; ++index) {
        result = __tinypy_profile_copy_constant(
            profile,
            &constants[index],
            index,
            &profile->constants[index],
            out_error);
        if (result != TINYPY_BUILD_PROFILE_OK) {
            __tinypy_profile_destroy_partial(profile);
            return result;
        }
    }
    {
        static const char ndebug_name[] = "__NDEBUG__";
        tinypy_profile_constant_t *ndebug = &profile->constants[constant_count];
        size_t name_size = sizeof(ndebug_name) - 1U;
        ndebug->name = (char *)__tinypy_profile_allocate(
            profile,
            sizeof(ndebug_name),
            constant_count,
            0U,
            out_error,
            &result);
        if (ndebug->name == NULL) {
            __tinypy_profile_destroy_partial(profile);
            return result;
        }
        (void)memcpy(ndebug->name, ndebug_name, sizeof(ndebug_name));
        ndebug->name_size = name_size;
        tinypy_build_value_init(&ndebug->value, TINYPY_BUILD_VALUE_BOOL);
        ndebug->value.integer_value = optimize_level == 0 ? 0 : 1;
        profile->value_node_count += 1U;
        if (profile->value_node_count > profile->limits.max_value_nodes) {
            result = __tinypy_profile_fail(
                out_error,
                TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED,
                constant_count,
                0U,
                "build constant value tinypy_cst_node_t limit exceeded");
            __tinypy_profile_destroy_partial(profile);
            return result;
        }
    }

    for (index = 1U; index != total_constants; ++index) {
        tinypy_profile_constant_t current = profile->constants[index];
        size_t position = index;
        while (position != 0U &&
            __tinypy_profile_name_compare(
                current.name,
                current.name_size,
                profile->constants[position - 1U].name,
                profile->constants[position - 1U].name_size) < 0) {
            profile->constants[position] = profile->constants[position - 1U];
            position -= 1U;
        }
        profile->constants[position] = current;
    }
    for (index = 1U; index != total_constants; ++index) {
        if (__tinypy_profile_name_compare(
                profile->constants[index - 1U].name,
                profile->constants[index - 1U].name_size,
                profile->constants[index].name,
                profile->constants[index].name_size) == 0) {
            result = __tinypy_profile_fail(
                out_error,
                TINYPY_BUILD_PROFILE_DUPLICATE_NAME,
                index,
                0U,
                "duplicate build constant name");
            __tinypy_profile_destroy_partial(profile);
            return result;
        }
    }

    __tinypy_profile_compute_digest(profile);
    *out_profile = profile;
    return TINYPY_BUILD_PROFILE_OK;
}

void tinypy_build_profile_destroy(tinypy_build_profile_t *profile)
{
    assert(profile != NULL);
    assert(profile->state == TINYPY_BUILD_PROFILE_STATE);
    profile->state = 0U;
    __tinypy_profile_destroy_partial(profile);
}

int tinypy_build_profile_optimize_level(const tinypy_build_profile_t *profile)
{
    assert(profile != NULL);
    assert(profile->state == TINYPY_BUILD_PROFILE_STATE);
    return profile->optimize_level;
}

size_t tinypy_build_profile_constant_count(const tinypy_build_profile_t *profile)
{
    assert(profile != NULL);
    assert(profile->state == TINYPY_BUILD_PROFILE_STATE);
    return profile->constant_count;
}

void tinypy_build_profile_constant_at(
    const tinypy_build_profile_t *profile,
    size_t index,
    const char **out_name,
    size_t *out_name_size,
    const tinypy_build_value_t **out_value)
{
    const tinypy_profile_constant_t *constant;
    assert(profile != NULL);
    assert(profile->state == TINYPY_BUILD_PROFILE_STATE);
    assert(out_name != NULL);
    assert(out_name_size != NULL);
    assert(out_value != NULL);
    assert(index < profile->constant_count);
    constant = &profile->constants[index];
    *out_name = constant->name;
    *out_name_size = constant->name_size;
    *out_value = &constant->value;
}

int tinypy_build_profile_find(
    const tinypy_build_profile_t *profile,
    const char *name,
    size_t name_size,
    const tinypy_build_value_t **out_value)
{
    size_t lower = 0U;
    size_t upper;
    assert(profile != NULL);
    assert(profile->state == TINYPY_BUILD_PROFILE_STATE);
    assert(name != NULL || name_size == 0U);
    assert(out_value != NULL);
    *out_value = NULL;
    upper = profile->constant_count;
    while (lower < upper) {
        size_t middle = lower + (upper - lower) / 2U;
        const tinypy_profile_constant_t *constant = &profile->constants[middle];
        int compared = __tinypy_profile_name_compare(
            name, name_size, constant->name, constant->name_size);
        if (compared == 0) {
            *out_value = &constant->value;
            return 1;
        }
        if (compared < 0) {
            upper = middle;
        } else {
            lower = middle + 1U;
        }
    }
    return 0;
}

const uint8_t *tinypy_build_profile_digest(const tinypy_build_profile_t *profile)
{
    assert(profile != NULL);
    assert(profile->state == TINYPY_BUILD_PROFILE_STATE);
    return profile->digest;
}

const char *tinypy_build_profile_result_name(tinypy_build_profile_result_e result)
{
    switch (result) {
    case TINYPY_BUILD_PROFILE_OK: return "ok";
    case TINYPY_BUILD_PROFILE_INVALID_ARGUMENT: return "invalid argument";
    case TINYPY_BUILD_PROFILE_ABI_MISMATCH: return "ABI mismatch";
    case TINYPY_BUILD_PROFILE_INVALID_OPTIMIZE: return "invalid optimize level";
    case TINYPY_BUILD_PROFILE_SIZE_OVERFLOW: return "size overflow";
    case TINYPY_BUILD_PROFILE_LIMIT_EXCEEDED: return "limit exceeded";
    case TINYPY_BUILD_PROFILE_INVALID_NAME: return "invalid constant name";
    case TINYPY_BUILD_PROFILE_DUPLICATE_NAME: return "duplicate constant name";
    case TINYPY_BUILD_PROFILE_NDEBUG_OVERRIDE: return "__NDEBUG__ override";
    case TINYPY_BUILD_PROFILE_INVALID_VALUE_TYPE: return "invalid value type";
    case TINYPY_BUILD_PROFILE_INVALID_VALUE: return "invalid value";
    case TINYPY_BUILD_PROFILE_INVALID_LONG: return "invalid long";
    case TINYPY_BUILD_PROFILE_INVALID_UTF8: return "invalid UTF-8";
    default: return "unknown build profile result";
    }
}
