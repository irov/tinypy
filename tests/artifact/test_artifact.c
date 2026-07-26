#include "tinypy/artifact.h"

#include "sha256.h"

#include <stdio.h>
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
            return 1;                        \
        }                                    \
    } while (0)

static int32_t __digest_matches_hex(const uint8_t digest[32], const char *expected) {
    static const char digits[] = "0123456789abcdef";
    size_t index;
    for (index = 0U; index != 32U; ++index) {
        if (expected[index * 2U] != digits[digest[index] >> 4U] || expected[index * 2U + 1U] != digits[digest[index] & 15U]) {
            return 0;
        }
    }
    return expected[64] == '\0';
}

static int32_t __test_sha256_vectors(void) {
    uint8_t digest[32];
    tinypy_sha256_context_t context;
    size_t index;

    tinypy_sha256_digest(NULL, 0U, digest);
    TEST_CHECK(__digest_matches_hex(
        digest,
        "e3b0c44298fc1c149afbf4c8996fb924"
        "27ae41e4649b934ca495991b7852b855"));

    tinypy_sha256_digest("abc", 3U, digest);
    TEST_CHECK(__digest_matches_hex(
        digest,
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad"));

    tinypy_sha256_initialize(&context);
    for (index = 0U; index != 1000000U; ++index) {
        tinypy_sha256_update(&context, "a", 1U);
    }
    tinypy_sha256_finalize(&context, digest);
    TEST_CHECK(__digest_matches_hex(
        digest,
        "cdc76e5c9914fb9281a1c7e284d73e67"
        "f1809a48a497200e046d39ccc7112cd0"));
    return 0;
}

static void __fill_digest(uint8_t digest[32], uint8_t seed) {
    size_t index;
    for (index = 0U; index != 32U; ++index) {
        digest[index] = (uint8_t)(seed + (uint8_t)index);
    }
}

static tinypy_artifact_metadata_t __valid_metadata(void) {
    tinypy_artifact_metadata_t metadata;
    (void)memset(&metadata, 0, sizeof(metadata));
    metadata.abi_version = TINYPY_ARTIFACT_ABI_VERSION;
    metadata.struct_size = (uint32_t)sizeof(metadata);
    metadata.bytecode_magic = TINYPY_CPYTHON_27_MAGIC;
    metadata.runtime_abi = 3U;
    metadata.compiler_abi = 7U;
    metadata.meta_abi = 5U;
    metadata.preprocessor_abi = 11U;
    metadata.future_flags = UINT32_C(0x00022000);
    metadata.feature_flags =
        (uint32_t)TINYPY_ARTIFACT_FEATURE_PREPROCESSOR | (uint32_t)TINYPY_ARTIFACT_FEATURE_META;
    metadata.optimize_level = 2U;
    metadata.payload_kind = (uint8_t)TINYPY_ARTIFACT_PAYLOAD_MARSHAL_V2;
    __fill_digest(metadata.constant_profile_digest, 1U);
    __fill_digest(metadata.source_hash, 33U);
    return metadata;
}

static int32_t __encode_sample(uint8_t *artifact, size_t capacity, size_t *out_size, tinypy_artifact_metadata_t *out_metadata) {
    static const uint8_t payload[] = {'c', 0U, 1U, 2U, 255U};
    tinypy_artifact_metadata_t metadata = __valid_metadata();
    tinypy_artifact_status_e status = tinypy_artifact_encode(
        &metadata,
        payload,
        sizeof(payload),
        artifact,
        capacity,
        out_size);
    if (out_metadata != NULL) {
        *out_metadata = metadata;
    }
    return status == TINYPY_ARTIFACT_OK ? 0 : 1;
}

static int32_t __test_encode_decode_round_trip(void) {
    static const uint8_t payload[] = {'c', 0U, 1U, 2U, 255U};
    uint8_t artifact[256];
    tinypy_artifact_metadata_t metadata = __valid_metadata();
    tinypy_artifact_view_t view;
    size_t required = 0U;
    tinypy_artifact_status_e status;

    status = tinypy_artifact_encode(
        &metadata, payload, sizeof(payload), NULL, 0U, &required);
    TEST_CHECK(status == TINYPY_ARTIFACT_BUFFER_TOO_SMALL);
    TEST_CHECK(required == TINYPY_ARTIFACT_HEADER_SIZE + sizeof(payload));

    status = tinypy_artifact_encode(
        &metadata,
        payload,
        sizeof(payload),
        artifact,
        required - 1U,
        &required);
    TEST_CHECK(status == TINYPY_ARTIFACT_BUFFER_TOO_SMALL);

    status = tinypy_artifact_encode(
        &metadata,
        payload,
        sizeof(payload),
        artifact,
        sizeof(artifact),
        &required);
    TEST_CHECK(status == TINYPY_ARTIFACT_OK);
    TEST_CHECK(memcmp(artifact, "TINYPY27", 8U) == 0);
    TEST_CHECK(artifact[12] == 2U && artifact[13] == 7U);
    TEST_CHECK(artifact[14] == 18U && artifact[15] == 2U);
    TEST_CHECK(artifact[44] == 2U && artifact[45] == 1U);
    TEST_CHECK(artifact[46] == 1U && artifact[47] == 0U);

    status = tinypy_artifact_decode(artifact, required, 0U, &view);
    TEST_CHECK(status == TINYPY_ARTIFACT_OK);
    TEST_CHECK(view.abi_version == TINYPY_ARTIFACT_ABI_VERSION);
    TEST_CHECK(view.struct_size == sizeof(view));
    TEST_CHECK(view.payload_size == sizeof(payload));
    TEST_CHECK(memcmp(view.payload, payload, sizeof(payload)) == 0);
    TEST_CHECK(view.metadata.bytecode_magic == TINYPY_CPYTHON_27_MAGIC);
    TEST_CHECK(view.metadata.runtime_abi == metadata.runtime_abi);
    TEST_CHECK(view.metadata.compiler_abi == metadata.compiler_abi);
    TEST_CHECK(view.metadata.meta_abi == metadata.meta_abi);
    TEST_CHECK(view.metadata.preprocessor_abi == metadata.preprocessor_abi);
    TEST_CHECK(view.metadata.future_flags == metadata.future_flags);
    TEST_CHECK(view.metadata.feature_flags == metadata.feature_flags);
    TEST_CHECK(view.metadata.optimize_level == metadata.optimize_level);
    TEST_CHECK(memcmp(view.metadata.constant_profile_digest,
                      metadata.constant_profile_digest, 32U) == 0);
    TEST_CHECK(memcmp(
                   view.metadata.source_hash, metadata.source_hash, 32U) == 0);
    TEST_CHECK(memcmp(
                   artifact + TINYPY_ARTIFACT_HEADER_SIZE,
                   payload,
                   sizeof(payload)) == 0);
    return 0;
}

static int32_t __test_corruption_and_bounds(void) {
    uint8_t artifact[256];
    uint8_t changed[257];
    size_t artifact_size = 0U;
    tinypy_artifact_view_t view;
    tinypy_artifact_status_e status;

    TEST_CHECK(__encode_sample(
                   artifact, sizeof(artifact), &artifact_size, NULL) == 0);

    status = tinypy_artifact_decode(
        artifact, TINYPY_ARTIFACT_HEADER_SIZE - 1U, 0U, &view);
    TEST_CHECK(status == TINYPY_ARTIFACT_TRUNCATED);
    status = tinypy_artifact_decode(artifact, artifact_size - 1U, 0U, &view);
    TEST_CHECK(status == TINYPY_ARTIFACT_TRUNCATED);
    status = tinypy_artifact_decode(artifact, artifact_size, 4U, &view);
    TEST_CHECK(status == TINYPY_ARTIFACT_PAYLOAD_LIMIT);

    (void)memcpy(changed, artifact, artifact_size);
    changed[0] ^= 1U;
    TEST_CHECK(tinypy_artifact_decode(changed, artifact_size, 0U, &view) ==
               TINYPY_ARTIFACT_BAD_MAGIC);

    (void)memcpy(changed, artifact, artifact_size);
    changed[8] = 2U;
    TEST_CHECK(tinypy_artifact_decode(changed, artifact_size, 0U, &view) ==
               TINYPY_ARTIFACT_UNSUPPORTED_FORMAT);

    (void)memcpy(changed, artifact, artifact_size);
    changed[12] = 3U;
    TEST_CHECK(tinypy_artifact_decode(changed, artifact_size, 0U, &view) ==
               TINYPY_ARTIFACT_UNSUPPORTED_LANGUAGE);

    (void)memcpy(changed, artifact, artifact_size);
    changed[45] = 9U;
    TEST_CHECK(tinypy_artifact_decode(changed, artifact_size, 0U, &view) ==
               TINYPY_ARTIFACT_UNSUPPORTED_PAYLOAD);

    (void)memcpy(changed, artifact, artifact_size);
    changed[80] ^= 1U;
    TEST_CHECK(tinypy_artifact_decode(changed, artifact_size, 0U, &view) ==
               TINYPY_ARTIFACT_CORRUPT_HEADER);

    (void)memcpy(changed, artifact, artifact_size);
    changed[TINYPY_ARTIFACT_HEADER_SIZE] ^= 1U;
    TEST_CHECK(tinypy_artifact_decode(changed, artifact_size, 0U, &view) ==
               TINYPY_ARTIFACT_CORRUPT_PAYLOAD);

    (void)memcpy(changed, artifact, artifact_size);
    changed[artifact_size] = 0U;
    TEST_CHECK(tinypy_artifact_decode(changed, artifact_size + 1U, 0U, &view) ==
               TINYPY_ARTIFACT_TRAILING_DATA);
    return 0;
}

static int32_t __test_exhaustive_truncation_and_single_byte_damage(void) {
    uint8_t artifact[256];
    uint8_t changed[256];
    size_t artifact_size = 0U;
    size_t index;
    tinypy_artifact_view_t view;

    TEST_CHECK(__encode_sample(
                   artifact, sizeof(artifact), &artifact_size, NULL) == 0);
    for (index = 0U; index != artifact_size; ++index) {
        TEST_CHECK(tinypy_artifact_decode(artifact, index, 0U, &view) !=
                   TINYPY_ARTIFACT_OK);
        TEST_CHECK(view.abi_version == TINYPY_ARTIFACT_ABI_VERSION);
        TEST_CHECK(view.payload == NULL);
    }

    for (index = 0U; index != artifact_size; ++index) {
        (void)memcpy(changed, artifact, artifact_size);
        changed[index] ^= UINT8_C(0x80);
        TEST_CHECK(tinypy_artifact_decode(
                       changed, artifact_size, 0U, &view) != TINYPY_ARTIFACT_OK);
    }
    return 0;
}

static int32_t __test_metadata_validation(void) {
    static const uint8_t payload[] = {0U};
    uint8_t artifact[256];
    tinypy_artifact_metadata_t metadata = __valid_metadata();
    size_t output_size = 0U;

    metadata.bytecode_magic += 1U;
    TEST_CHECK(tinypy_artifact_encode(&metadata, payload, sizeof(payload),
                                      artifact, sizeof(artifact), &output_size) ==
               TINYPY_ARTIFACT_INVALID_ARGUMENT);

    metadata = __valid_metadata();
    metadata.feature_flags |= (uint32_t)TINYPY_ARTIFACT_FEATURE_SOURCE_MAP;
    TEST_CHECK(tinypy_artifact_encode(&metadata, payload, sizeof(payload),
                                      artifact, sizeof(artifact), &output_size) ==
               TINYPY_ARTIFACT_INVALID_ARGUMENT);
    __fill_digest(metadata.source_map_hash, 91U);
    TEST_CHECK(tinypy_artifact_encode(&metadata, payload, sizeof(payload),
                                      artifact, sizeof(artifact), &output_size) == TINYPY_ARTIFACT_OK);

    metadata = __valid_metadata();
    __fill_digest(metadata.source_map_hash, 91U);
    TEST_CHECK(tinypy_artifact_encode(&metadata, payload, sizeof(payload),
                                      artifact, sizeof(artifact), &output_size) ==
               TINYPY_ARTIFACT_INVALID_ARGUMENT);

    metadata = __valid_metadata();
    metadata.abi_version += 1U;
    TEST_CHECK(tinypy_artifact_encode(&metadata, payload, sizeof(payload),
                                      artifact, sizeof(artifact), &output_size) ==
               TINYPY_ARTIFACT_ABI_MISMATCH);

    metadata = __valid_metadata();
    TEST_CHECK(tinypy_artifact_encode(
                   &metadata, payload, SIZE_MAX, artifact, sizeof(artifact),
                   &output_size) == TINYPY_ARTIFACT_SIZE_OVERFLOW);
    return 0;
}

static int32_t __test_profile_check(void) {
    uint8_t artifact[256];
    size_t artifact_size = 0U;
    tinypy_artifact_metadata_t metadata;
    tinypy_artifact_view_t view;
    tinypy_artifact_expectation_t expectation;
    uint32_t all_checks =
        (uint32_t)TINYPY_ARTIFACT_CHECK_BYTECODE_MAGIC | (uint32_t)TINYPY_ARTIFACT_CHECK_RUNTIME_ABI | (uint32_t)TINYPY_ARTIFACT_CHECK_COMPILER_ABI | (uint32_t)TINYPY_ARTIFACT_CHECK_META_ABI | (uint32_t)TINYPY_ARTIFACT_CHECK_PREPROCESSOR_ABI | (uint32_t)TINYPY_ARTIFACT_CHECK_OPTIMIZE_LEVEL | (uint32_t)TINYPY_ARTIFACT_CHECK_FEATURE_FLAGS | (uint32_t)TINYPY_ARTIFACT_CHECK_FUTURE_FLAGS | (uint32_t)TINYPY_ARTIFACT_CHECK_CONSTANT_PROFILE | (uint32_t)TINYPY_ARTIFACT_CHECK_SOURCE_HASH | (uint32_t)TINYPY_ARTIFACT_CHECK_SOURCE_MAP_HASH | (uint32_t)TINYPY_ARTIFACT_CHECK_PAYLOAD_KIND;

    TEST_CHECK(__encode_sample(
                   artifact, sizeof(artifact), &artifact_size, &metadata) == 0);
    TEST_CHECK(tinypy_artifact_decode(artifact, artifact_size, 0U, &view) ==
               TINYPY_ARTIFACT_OK);

    (void)memset(&expectation, 0, sizeof(expectation));
    expectation.abi_version = TINYPY_ARTIFACT_ABI_VERSION;
    expectation.struct_size = (uint32_t)sizeof(expectation);
    expectation.check_flags = all_checks;
    expectation.expected = view.metadata;
    TEST_CHECK(tinypy_artifact_check_profile(&view, &expectation) ==
               TINYPY_ARTIFACT_OK);

    expectation.expected.optimize_level = 1U;
    TEST_CHECK(tinypy_artifact_check_profile(&view, &expectation) ==
               TINYPY_ARTIFACT_PROFILE_MISMATCH);
    expectation.expected.optimize_level = view.metadata.optimize_level;
    expectation.expected.constant_profile_digest[17] ^= 1U;
    TEST_CHECK(tinypy_artifact_check_profile(&view, &expectation) ==
               TINYPY_ARTIFACT_PROFILE_MISMATCH);

    expectation.check_flags = UINT32_C(0x80000000);
    TEST_CHECK(tinypy_artifact_check_profile(&view, &expectation) ==
               TINYPY_ARTIFACT_INVALID_ARGUMENT);
    expectation.check_flags = 0U;
    expectation.abi_version += 1U;
    TEST_CHECK(tinypy_artifact_check_profile(&view, &expectation) ==
               TINYPY_ARTIFACT_ABI_MISMATCH);
    return 0;
}

static int32_t __test_status_names(void) {
    int32_t status;
    for (status = (int32_t)TINYPY_ARTIFACT_OK;
         status <= (int32_t)TINYPY_ARTIFACT_PROFILE_MISMATCH;
         ++status) {
        const char *name = tinypy_artifact_status_name(
            (tinypy_artifact_status_e)status);
        TEST_CHECK(name != NULL);
        TEST_CHECK(strcmp(name, "unknown artifact status") != 0);
    }
    TEST_CHECK(strcmp(
                   tinypy_artifact_status_name((tinypy_artifact_status_e)999),
                   "unknown artifact status") == 0);
    return 0;
}

int main(void) {
    if (__test_sha256_vectors() != 0 || __test_encode_decode_round_trip() != 0 || __test_corruption_and_bounds() != 0 || __test_exhaustive_truncation_and_single_byte_damage() != 0 || __test_metadata_validation() != 0 || __test_profile_check() != 0 || __test_status_names() != 0) {
        return 1;
    }
    return 0;
}
