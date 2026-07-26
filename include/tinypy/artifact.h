#ifndef TINYPY_ARTIFACT_H
#define TINYPY_ARTIFACT_H

#include "tinypy/types.h"

#define TINYPY_ARTIFACT_ABI_VERSION UINT32_C(1)
#define TINYPY_ARTIFACT_FORMAT_VERSION UINT16_C(1)
#define TINYPY_ARTIFACT_HEADER_SIZE ((size_t)192U)
#define TINYPY_ARTIFACT_DIGEST_SIZE ((size_t)32U)
#define TINYPY_CPYTHON_27_MAGIC UINT32_C(0x0A0DF303)

/* Unless documented as optional, pointers are C API preconditions: debug
 * builds with TINYPY_ENABLE_ASSERTS assert them and other builds leave
 * violations undefined. Statuses
 * describe artifact contents, capacity, compatibility and limits only. */

typedef enum tinypy_artifact_status_e {
    TINYPY_ARTIFACT_OK = 0,
    TINYPY_ARTIFACT_INVALID_ARGUMENT = 1,
    TINYPY_ARTIFACT_ABI_MISMATCH = 2,
    TINYPY_ARTIFACT_BUFFER_TOO_SMALL = 3,
    TINYPY_ARTIFACT_SIZE_OVERFLOW = 4,
    TINYPY_ARTIFACT_TRUNCATED = 5,
    TINYPY_ARTIFACT_BAD_MAGIC = 6,
    TINYPY_ARTIFACT_UNSUPPORTED_FORMAT = 7,
    TINYPY_ARTIFACT_UNSUPPORTED_LANGUAGE = 8,
    TINYPY_ARTIFACT_UNSUPPORTED_PAYLOAD = 9,
    TINYPY_ARTIFACT_CORRUPT_HEADER = 10,
    TINYPY_ARTIFACT_CORRUPT_PAYLOAD = 11,
    TINYPY_ARTIFACT_TRAILING_DATA = 12,
    TINYPY_ARTIFACT_PAYLOAD_LIMIT = 13,
    TINYPY_ARTIFACT_PROFILE_MISMATCH = 14
} tinypy_artifact_status_e;

typedef enum tinypy_artifact_payload_kind_e {
    TINYPY_ARTIFACT_PAYLOAD_MARSHAL_V2 = 1
} tinypy_artifact_payload_kind_e;

typedef enum tinypy_artifact_feature_e {
    TINYPY_ARTIFACT_FEATURE_PREPROCESSOR = UINT32_C(1) << 0,
    TINYPY_ARTIFACT_FEATURE_META = UINT32_C(1) << 1,
    TINYPY_ARTIFACT_FEATURE_SOURCE_MAP = UINT32_C(1) << 2
} tinypy_artifact_feature_e;

typedef enum tinypy_artifact_check_e {
    TINYPY_ARTIFACT_CHECK_BYTECODE_MAGIC = UINT32_C(1) << 0,
    TINYPY_ARTIFACT_CHECK_RUNTIME_ABI = UINT32_C(1) << 1,
    TINYPY_ARTIFACT_CHECK_COMPILER_ABI = UINT32_C(1) << 2,
    TINYPY_ARTIFACT_CHECK_META_ABI = UINT32_C(1) << 3,
    TINYPY_ARTIFACT_CHECK_PREPROCESSOR_ABI = UINT32_C(1) << 4,
    TINYPY_ARTIFACT_CHECK_OPTIMIZE_LEVEL = UINT32_C(1) << 5,
    TINYPY_ARTIFACT_CHECK_FEATURE_FLAGS = UINT32_C(1) << 6,
    TINYPY_ARTIFACT_CHECK_FUTURE_FLAGS = UINT32_C(1) << 7,
    TINYPY_ARTIFACT_CHECK_CONSTANT_PROFILE = UINT32_C(1) << 8,
    TINYPY_ARTIFACT_CHECK_SOURCE_HASH = UINT32_C(1) << 9,
    TINYPY_ARTIFACT_CHECK_SOURCE_MAP_HASH = UINT32_C(1) << 10,
    TINYPY_ARTIFACT_CHECK_PAYLOAD_KIND = UINT32_C(1) << 11
} tinypy_artifact_check_e;

typedef struct tinypy_artifact_metadata_t {
    uint32_t abi_version;
    uint32_t struct_size;

    uint32_t bytecode_magic;
    uint32_t runtime_abi;
    uint32_t compiler_abi;
    uint32_t meta_abi;
    uint32_t preprocessor_abi;
    uint32_t future_flags;
    uint32_t feature_flags;

    uint8_t optimize_level;
    uint8_t payload_kind;
    uint16_t reserved;

    uint8_t constant_profile_digest[32];
    uint8_t source_hash[32];
    uint8_t source_map_hash[32];
    uint8_t payload_hash[32];
    uint64_t payload_size;
} tinypy_artifact_metadata_t;

typedef struct tinypy_artifact_expectation_t {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t check_flags;
    uint32_t reserved;
    tinypy_artifact_metadata_t expected;
} tinypy_artifact_expectation_t;

typedef struct tinypy_artifact_view_t {
    uint32_t abi_version;
    uint32_t struct_size;
    tinypy_artifact_metadata_t metadata;
    const void *payload;
    size_t payload_size;
} tinypy_artifact_view_t;

/* Encodes one complete artifact into a caller-owned memory buffer.  Pass NULL
 * with capacity 0 to query the required size; BUFFER_TOO_SMALL is returned and
 * out_size is still populated.  payload_hash/payload_size in metadata are
 * derived from payload and do not need to be initialized by the caller. */
tinypy_artifact_status_e tinypy_artifact_encode(const tinypy_artifact_metadata_t *metadata, const void *payload, size_t payload_size, void *output, size_t output_capacity, size_t *out_size);

/* Decodes and verifies a complete memory artifact.  The returned view borrows
 * the input buffer.  max_payload_size == 0 means no host-imposed limit. */
tinypy_artifact_status_e tinypy_artifact_decode(const void *artifact, size_t artifact_size, size_t max_payload_size, tinypy_artifact_view_t *out_view);

tinypy_artifact_status_e tinypy_artifact_check_profile(const tinypy_artifact_view_t *view, const tinypy_artifact_expectation_t *expectation);

const char *tinypy_artifact_status_name(tinypy_artifact_status_e status);

#endif
