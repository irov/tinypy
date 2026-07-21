#include "tinypy/artifact.h"

#include "sha256.h"

#include <assert.h>
#include <string.h>

#define TINYPY_ARTIFACT_OFFSET_FORMAT_VERSION 8U
#define TINYPY_ARTIFACT_OFFSET_HEADER_SIZE 10U
#define TINYPY_ARTIFACT_OFFSET_PYTHON_MAJOR 12U
#define TINYPY_ARTIFACT_OFFSET_MARSHAL_VERSION 15U
#define TINYPY_ARTIFACT_OFFSET_BYTECODE_MAGIC 16U
#define TINYPY_ARTIFACT_OFFSET_RUNTIME_ABI 20U
#define TINYPY_ARTIFACT_OFFSET_COMPILER_ABI 24U
#define TINYPY_ARTIFACT_OFFSET_META_ABI 28U
#define TINYPY_ARTIFACT_OFFSET_PREPROCESSOR_ABI 32U
#define TINYPY_ARTIFACT_OFFSET_FUTURE_FLAGS 36U
#define TINYPY_ARTIFACT_OFFSET_FEATURE_FLAGS 40U
#define TINYPY_ARTIFACT_OFFSET_OPTIMIZE_LEVEL 44U
#define TINYPY_ARTIFACT_OFFSET_PAYLOAD_KIND 45U
#define TINYPY_ARTIFACT_OFFSET_WIRE_ENCODING 46U
#define TINYPY_ARTIFACT_OFFSET_RESERVED_BYTE 47U
#define TINYPY_ARTIFACT_OFFSET_CONSTANT_PROFILE 48U
#define TINYPY_ARTIFACT_OFFSET_SOURCE_HASH 80U
#define TINYPY_ARTIFACT_OFFSET_SOURCE_MAP_HASH 112U
#define TINYPY_ARTIFACT_OFFSET_PAYLOAD_HASH 144U
#define TINYPY_ARTIFACT_OFFSET_PAYLOAD_SIZE 176U
#define TINYPY_ARTIFACT_OFFSET_HEADER_CRC 184U
#define TINYPY_ARTIFACT_OFFSET_RESERVED_WORD 188U

#define TINYPY_ARTIFACT_WIRE_ENCODING_LE UINT8_C(1)
#define TINYPY_ARTIFACT_ALL_FEATURES                  \
    ((uint32_t)TINYPY_ARTIFACT_FEATURE_PREPROCESSOR | \
     (uint32_t)TINYPY_ARTIFACT_FEATURE_META |         \
     (uint32_t)TINYPY_ARTIFACT_FEATURE_SOURCE_MAP)
#define TINYPY_ARTIFACT_ALL_CHECKS                      \
    ((uint32_t)TINYPY_ARTIFACT_CHECK_BYTECODE_MAGIC |   \
     (uint32_t)TINYPY_ARTIFACT_CHECK_RUNTIME_ABI |      \
     (uint32_t)TINYPY_ARTIFACT_CHECK_COMPILER_ABI |     \
     (uint32_t)TINYPY_ARTIFACT_CHECK_META_ABI |         \
     (uint32_t)TINYPY_ARTIFACT_CHECK_PREPROCESSOR_ABI | \
     (uint32_t)TINYPY_ARTIFACT_CHECK_OPTIMIZE_LEVEL |   \
     (uint32_t)TINYPY_ARTIFACT_CHECK_FEATURE_FLAGS |    \
     (uint32_t)TINYPY_ARTIFACT_CHECK_FUTURE_FLAGS |     \
     (uint32_t)TINYPY_ARTIFACT_CHECK_CONSTANT_PROFILE | \
     (uint32_t)TINYPY_ARTIFACT_CHECK_SOURCE_HASH |      \
     (uint32_t)TINYPY_ARTIFACT_CHECK_SOURCE_MAP_HASH |  \
     (uint32_t)TINYPY_ARTIFACT_CHECK_PAYLOAD_KIND)

static const uint8_t tinypy_artifact_magic[8] = {
    'T', 'I', 'N', 'Y', 'P', 'Y', '2', '7'};

//////////////////////////////////////////////////////////////////////////
static uint16_t __tinypy_artifact_read_u16_le(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_artifact_read_u32_le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

//////////////////////////////////////////////////////////////////////////
static uint64_t __tinypy_artifact_read_u64_le(const uint8_t *data) {
    uint64_t value = UINT64_C(0);
    size_t index;
    for (index = 0U; index != 8U; ++index) {
        value |= (uint64_t)data[index] << (index * 8U);
    }
    return value;
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_artifact_write_u16_le(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_artifact_write_u32_le(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_artifact_write_u64_le(uint8_t *data, uint64_t value) {
    size_t index;
    for (index = 0U; index != 8U; ++index) {
        data[index] = (uint8_t)(value >> (index * 8U));
    }
}

//////////////////////////////////////////////////////////////////////////
static uint32_t __tinypy_artifact_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = UINT32_C(0xffffffff);
    size_t index;
    for (index = 0U; index != size; ++index) {
        uint32_t value = crc ^ (uint32_t)data[index];
        unsigned int bit;
        for (bit = 0U; bit != 8U; ++bit) {
            uint32_t mask = UINT32_C(0) - (value & UINT32_C(1));
            value = (value >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
        crc = value;
    }
    return ~crc;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_artifact_metadata_valid(const tinypy_artifact_metadata_t *metadata) {
    static const uint8_t zero_digest[32] = {0};
    int has_source_map;
    int source_map_is_zero;

    assert(metadata != NULL);
    has_source_map = (metadata->feature_flags & (uint32_t)TINYPY_ARTIFACT_FEATURE_SOURCE_MAP) != 0U;
    source_map_is_zero = memcmp(
                             metadata->source_map_hash,
                             zero_digest,
                             sizeof(zero_digest)) == 0;

    return metadata->abi_version == TINYPY_ARTIFACT_ABI_VERSION && metadata->struct_size >= sizeof(tinypy_artifact_metadata_t) && metadata->reserved == 0U && metadata->bytecode_magic == TINYPY_CPYTHON_27_MAGIC && metadata->runtime_abi != 0U && metadata->compiler_abi != 0U && metadata->meta_abi != 0U && metadata->preprocessor_abi != 0U && metadata->optimize_level <= 2U && metadata->payload_kind == (uint8_t)TINYPY_ARTIFACT_PAYLOAD_MARSHAL_V2 && (metadata->feature_flags & ~TINYPY_ARTIFACT_ALL_FEATURES) == 0U && has_source_map == (source_map_is_zero == 0);
}

//////////////////////////////////////////////////////////////////////////
tinypy_artifact_status_e tinypy_artifact_encode(const tinypy_artifact_metadata_t *metadata, const void *payload, size_t payload_size, void *output, size_t output_capacity, size_t *out_size) {
    uint8_t *bytes;
    uint8_t payload_hash[32];
    size_t required_size;

    assert(metadata != NULL);
    assert(out_size != NULL);
    assert(payload != NULL || payload_size == 0U);
    assert(output != NULL || output_capacity == 0U);
    *out_size = 0U;

    if (!__tinypy_artifact_metadata_valid(metadata)) {
        return metadata->abi_version != TINYPY_ARTIFACT_ABI_VERSION
                   ? TINYPY_ARTIFACT_ABI_MISMATCH
                   : TINYPY_ARTIFACT_INVALID_ARGUMENT;
    }
    if (payload_size > SIZE_MAX - TINYPY_ARTIFACT_HEADER_SIZE) {
        return TINYPY_ARTIFACT_SIZE_OVERFLOW;
    }
    required_size = TINYPY_ARTIFACT_HEADER_SIZE + payload_size;
    *out_size = required_size;
    if (output_capacity < required_size || output == NULL) {
        return TINYPY_ARTIFACT_BUFFER_TOO_SMALL;
    }

    bytes = (uint8_t *)output;
    (void)memset(bytes, 0, TINYPY_ARTIFACT_HEADER_SIZE);
    (void)memcpy(bytes, tinypy_artifact_magic, sizeof(tinypy_artifact_magic));
    __tinypy_artifact_write_u16_le(
        bytes + TINYPY_ARTIFACT_OFFSET_FORMAT_VERSION,
        TINYPY_ARTIFACT_FORMAT_VERSION);
    __tinypy_artifact_write_u16_le(
        bytes + TINYPY_ARTIFACT_OFFSET_HEADER_SIZE,
        (uint16_t)TINYPY_ARTIFACT_HEADER_SIZE);
    bytes[TINYPY_ARTIFACT_OFFSET_PYTHON_MAJOR] = 2U;
    bytes[TINYPY_ARTIFACT_OFFSET_PYTHON_MAJOR + 1U] = 7U;
    bytes[TINYPY_ARTIFACT_OFFSET_PYTHON_MAJOR + 2U] = 18U;
    bytes[TINYPY_ARTIFACT_OFFSET_MARSHAL_VERSION] = 2U;
    __tinypy_artifact_write_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_BYTECODE_MAGIC,
        metadata->bytecode_magic);
    __tinypy_artifact_write_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_RUNTIME_ABI,
        metadata->runtime_abi);
    __tinypy_artifact_write_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_COMPILER_ABI,
        metadata->compiler_abi);
    __tinypy_artifact_write_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_META_ABI,
        metadata->meta_abi);
    __tinypy_artifact_write_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_PREPROCESSOR_ABI,
        metadata->preprocessor_abi);
    __tinypy_artifact_write_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_FUTURE_FLAGS,
        metadata->future_flags);
    __tinypy_artifact_write_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_FEATURE_FLAGS,
        metadata->feature_flags);
    bytes[TINYPY_ARTIFACT_OFFSET_OPTIMIZE_LEVEL] = metadata->optimize_level;
    bytes[TINYPY_ARTIFACT_OFFSET_PAYLOAD_KIND] = metadata->payload_kind;
    bytes[TINYPY_ARTIFACT_OFFSET_WIRE_ENCODING] = TINYPY_ARTIFACT_WIRE_ENCODING_LE;
    (void)memcpy(
        bytes + TINYPY_ARTIFACT_OFFSET_CONSTANT_PROFILE,
        metadata->constant_profile_digest,
        32U);
    (void)memcpy(
        bytes + TINYPY_ARTIFACT_OFFSET_SOURCE_HASH,
        metadata->source_hash,
        32U);
    (void)memcpy(
        bytes + TINYPY_ARTIFACT_OFFSET_SOURCE_MAP_HASH,
        metadata->source_map_hash,
        32U);

    tinypy_sha256_digest(payload, payload_size, payload_hash);
    (void)memcpy(
        bytes + TINYPY_ARTIFACT_OFFSET_PAYLOAD_HASH,
        payload_hash,
        sizeof(payload_hash));
    __tinypy_artifact_write_u64_le(
        bytes + TINYPY_ARTIFACT_OFFSET_PAYLOAD_SIZE,
        (uint64_t)payload_size);
    uint32_t artifact_crc32 = __tinypy_artifact_crc32(bytes, TINYPY_ARTIFACT_OFFSET_HEADER_CRC);
    __tinypy_artifact_write_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_HEADER_CRC,
        artifact_crc32);

    if (payload_size != 0U) {
        (void)memmove(bytes + TINYPY_ARTIFACT_HEADER_SIZE, payload, payload_size);
    }
    return TINYPY_ARTIFACT_OK;
}

//////////////////////////////////////////////////////////////////////////
tinypy_artifact_status_e tinypy_artifact_decode(const void *artifact, size_t artifact_size, size_t max_payload_size, tinypy_artifact_view_t *out_view) {
    const uint8_t *bytes = (const uint8_t *)artifact;
    uint64_t payload_size_u64;
    size_t payload_size;
    uint8_t actual_hash[32];

    assert(out_view != NULL);
    assert(artifact != NULL || artifact_size == 0U);
    (void)memset(out_view, 0, sizeof(*out_view));
    out_view->abi_version = TINYPY_ARTIFACT_ABI_VERSION;
    out_view->struct_size = (uint32_t)sizeof(*out_view);
    out_view->metadata.abi_version = TINYPY_ARTIFACT_ABI_VERSION;
    out_view->metadata.struct_size =
        (uint32_t)sizeof(out_view->metadata);

    if (artifact_size < TINYPY_ARTIFACT_HEADER_SIZE) {
        return TINYPY_ARTIFACT_TRUNCATED;
    }
    if (memcmp(bytes, tinypy_artifact_magic, sizeof(tinypy_artifact_magic)) != 0) {
        return TINYPY_ARTIFACT_BAD_MAGIC;
    }
    if (__tinypy_artifact_read_u16_le(
            bytes + TINYPY_ARTIFACT_OFFSET_FORMAT_VERSION) !=
            TINYPY_ARTIFACT_FORMAT_VERSION || __tinypy_artifact_read_u16_le(bytes + TINYPY_ARTIFACT_OFFSET_HEADER_SIZE) !=
            TINYPY_ARTIFACT_HEADER_SIZE) {
        return TINYPY_ARTIFACT_UNSUPPORTED_FORMAT;
    }
    if (bytes[TINYPY_ARTIFACT_OFFSET_PYTHON_MAJOR] != 2U || bytes[TINYPY_ARTIFACT_OFFSET_PYTHON_MAJOR + 1U] != 7U || bytes[TINYPY_ARTIFACT_OFFSET_PYTHON_MAJOR + 2U] != 18U || bytes[TINYPY_ARTIFACT_OFFSET_MARSHAL_VERSION] != 2U) {
        return TINYPY_ARTIFACT_UNSUPPORTED_LANGUAGE;
    }
    if (bytes[TINYPY_ARTIFACT_OFFSET_PAYLOAD_KIND] !=
            (uint8_t)TINYPY_ARTIFACT_PAYLOAD_MARSHAL_V2 || bytes[TINYPY_ARTIFACT_OFFSET_WIRE_ENCODING] !=
            TINYPY_ARTIFACT_WIRE_ENCODING_LE) {
        return TINYPY_ARTIFACT_UNSUPPORTED_PAYLOAD;
    }
    if (bytes[TINYPY_ARTIFACT_OFFSET_RESERVED_BYTE] != 0U || __tinypy_artifact_read_u32_le(
            bytes + TINYPY_ARTIFACT_OFFSET_RESERVED_WORD) != 0U || __tinypy_artifact_read_u32_le(bytes + TINYPY_ARTIFACT_OFFSET_HEADER_CRC) !=
            __tinypy_artifact_crc32(bytes, TINYPY_ARTIFACT_OFFSET_HEADER_CRC)) {
        return TINYPY_ARTIFACT_CORRUPT_HEADER;
    }

    payload_size_u64 = __tinypy_artifact_read_u64_le(
        bytes + TINYPY_ARTIFACT_OFFSET_PAYLOAD_SIZE);
    if (payload_size_u64 > (uint64_t)SIZE_MAX) {
        return TINYPY_ARTIFACT_SIZE_OVERFLOW;
    }
    payload_size = (size_t)payload_size_u64;
    if (max_payload_size != 0U && payload_size > max_payload_size) {
        return TINYPY_ARTIFACT_PAYLOAD_LIMIT;
    }
    if (payload_size > artifact_size - TINYPY_ARTIFACT_HEADER_SIZE) {
        return TINYPY_ARTIFACT_TRUNCATED;
    }
    if (payload_size != artifact_size - TINYPY_ARTIFACT_HEADER_SIZE) {
        return TINYPY_ARTIFACT_TRAILING_DATA;
    }

    tinypy_sha256_digest(
        bytes + TINYPY_ARTIFACT_HEADER_SIZE,
        payload_size,
        actual_hash);
    if (memcmp(
            actual_hash,
            bytes + TINYPY_ARTIFACT_OFFSET_PAYLOAD_HASH,
            sizeof(actual_hash)) != 0) {
        return TINYPY_ARTIFACT_CORRUPT_PAYLOAD;
    }

    out_view->metadata.bytecode_magic = __tinypy_artifact_read_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_BYTECODE_MAGIC);
    out_view->metadata.runtime_abi = __tinypy_artifact_read_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_RUNTIME_ABI);
    out_view->metadata.compiler_abi = __tinypy_artifact_read_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_COMPILER_ABI);
    out_view->metadata.meta_abi = __tinypy_artifact_read_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_META_ABI);
    out_view->metadata.preprocessor_abi = __tinypy_artifact_read_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_PREPROCESSOR_ABI);
    out_view->metadata.future_flags = __tinypy_artifact_read_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_FUTURE_FLAGS);
    out_view->metadata.feature_flags = __tinypy_artifact_read_u32_le(
        bytes + TINYPY_ARTIFACT_OFFSET_FEATURE_FLAGS);
    out_view->metadata.optimize_level =
        bytes[TINYPY_ARTIFACT_OFFSET_OPTIMIZE_LEVEL];
    out_view->metadata.payload_kind =
        bytes[TINYPY_ARTIFACT_OFFSET_PAYLOAD_KIND];
    (void)memcpy(
        out_view->metadata.constant_profile_digest,
        bytes + TINYPY_ARTIFACT_OFFSET_CONSTANT_PROFILE,
        32U);
    (void)memcpy(
        out_view->metadata.source_hash,
        bytes + TINYPY_ARTIFACT_OFFSET_SOURCE_HASH,
        32U);
    (void)memcpy(
        out_view->metadata.source_map_hash,
        bytes + TINYPY_ARTIFACT_OFFSET_SOURCE_MAP_HASH,
        32U);
    (void)memcpy(
        out_view->metadata.payload_hash,
        bytes + TINYPY_ARTIFACT_OFFSET_PAYLOAD_HASH,
        32U);
    out_view->metadata.payload_size = payload_size_u64;
    out_view->payload = bytes + TINYPY_ARTIFACT_HEADER_SIZE;
    out_view->payload_size = payload_size;

    if (!__tinypy_artifact_metadata_valid(&out_view->metadata)) {
        return TINYPY_ARTIFACT_CORRUPT_HEADER;
    }
    return TINYPY_ARTIFACT_OK;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_artifact_field_equal(uint32_t flags, uint32_t flag, uint32_t actual, uint32_t expected) {
    return (flags & flag) == 0U || actual == expected;
}

//////////////////////////////////////////////////////////////////////////
tinypy_artifact_status_e tinypy_artifact_check_profile(const tinypy_artifact_view_t *view, const tinypy_artifact_expectation_t *expectation) {
    uint32_t flags;
    const tinypy_artifact_metadata_t *actual;
    const tinypy_artifact_metadata_t *expected;

    assert(view != NULL);
    assert(expectation != NULL);
    if (view->abi_version != TINYPY_ARTIFACT_ABI_VERSION || view->struct_size < sizeof(tinypy_artifact_view_t) || view->metadata.abi_version != TINYPY_ARTIFACT_ABI_VERSION || view->metadata.struct_size < sizeof(tinypy_artifact_metadata_t) || expectation->abi_version != TINYPY_ARTIFACT_ABI_VERSION || expectation->struct_size < sizeof(tinypy_artifact_expectation_t) || expectation->expected.abi_version !=
            TINYPY_ARTIFACT_ABI_VERSION || expectation->expected.struct_size <
            sizeof(tinypy_artifact_metadata_t)) {
        return TINYPY_ARTIFACT_ABI_MISMATCH;
    }
    flags = expectation->check_flags;
    if (expectation->reserved != 0U || (flags & ~TINYPY_ARTIFACT_ALL_CHECKS) != 0U) {
        return TINYPY_ARTIFACT_INVALID_ARGUMENT;
    }

    actual = &view->metadata;
    expected = &expectation->expected;
    if (!__tinypy_artifact_field_equal(flags, TINYPY_ARTIFACT_CHECK_BYTECODE_MAGIC,
                                       actual->bytecode_magic, expected->bytecode_magic) || !__tinypy_artifact_field_equal(flags, TINYPY_ARTIFACT_CHECK_RUNTIME_ABI,
                                       actual->runtime_abi, expected->runtime_abi) || !__tinypy_artifact_field_equal(flags, TINYPY_ARTIFACT_CHECK_COMPILER_ABI,
                                       actual->compiler_abi, expected->compiler_abi) || !__tinypy_artifact_field_equal(flags, TINYPY_ARTIFACT_CHECK_META_ABI,
                                       actual->meta_abi, expected->meta_abi) || !__tinypy_artifact_field_equal(flags, TINYPY_ARTIFACT_CHECK_PREPROCESSOR_ABI,
                                       actual->preprocessor_abi, expected->preprocessor_abi) || !__tinypy_artifact_field_equal(flags, TINYPY_ARTIFACT_CHECK_FEATURE_FLAGS,
                                       actual->feature_flags, expected->feature_flags) || !__tinypy_artifact_field_equal(flags, TINYPY_ARTIFACT_CHECK_FUTURE_FLAGS,
                                       actual->future_flags, expected->future_flags) || ((flags & TINYPY_ARTIFACT_CHECK_OPTIMIZE_LEVEL) != 0U && actual->optimize_level != expected->optimize_level) || ((flags & TINYPY_ARTIFACT_CHECK_PAYLOAD_KIND) != 0U && actual->payload_kind != expected->payload_kind) || ((flags & TINYPY_ARTIFACT_CHECK_CONSTANT_PROFILE) != 0U && memcmp(actual->constant_profile_digest,
                expected->constant_profile_digest, 32U) != 0) || ((flags & TINYPY_ARTIFACT_CHECK_SOURCE_HASH) != 0U && memcmp(actual->source_hash, expected->source_hash, 32U) != 0) || ((flags & TINYPY_ARTIFACT_CHECK_SOURCE_MAP_HASH) != 0U && memcmp(actual->source_map_hash,
                expected->source_map_hash, 32U) != 0)) {
        return TINYPY_ARTIFACT_PROFILE_MISMATCH;
    }
    return TINYPY_ARTIFACT_OK;
}

//////////////////////////////////////////////////////////////////////////
const char *tinypy_artifact_status_name(tinypy_artifact_status_e status) {
    switch (status) {
    case TINYPY_ARTIFACT_OK:
        return "ok";
    case TINYPY_ARTIFACT_INVALID_ARGUMENT:
        return "invalid argument";
    case TINYPY_ARTIFACT_ABI_MISMATCH:
        return "ABI mismatch";
    case TINYPY_ARTIFACT_BUFFER_TOO_SMALL:
        return "buffer or limit too small";
    case TINYPY_ARTIFACT_SIZE_OVERFLOW:
        return "size overflow";
    case TINYPY_ARTIFACT_TRUNCATED:
        return "truncated artifact";
    case TINYPY_ARTIFACT_BAD_MAGIC:
        return "bad artifact magic";
    case TINYPY_ARTIFACT_UNSUPPORTED_FORMAT:
        return "unsupported artifact format";
    case TINYPY_ARTIFACT_UNSUPPORTED_LANGUAGE:
        return "unsupported language ABI";
    case TINYPY_ARTIFACT_UNSUPPORTED_PAYLOAD:
        return "unsupported payload";
    case TINYPY_ARTIFACT_CORRUPT_HEADER:
        return "corrupt artifact header";
    case TINYPY_ARTIFACT_CORRUPT_PAYLOAD:
        return "corrupt artifact payload";
    case TINYPY_ARTIFACT_TRAILING_DATA:
        return "trailing artifact data";
    case TINYPY_ARTIFACT_PAYLOAD_LIMIT:
        return "artifact payload limit exceeded";
    case TINYPY_ARTIFACT_PROFILE_MISMATCH:
        return "artifact profile mismatch";
    default:
        return "unknown artifact status";
    }
}
