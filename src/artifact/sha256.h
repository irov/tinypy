#ifndef TINYPY_ARTIFACT_SHA256_H
#define TINYPY_ARTIFACT_SHA256_H

#include "tinypy/types.h"

typedef struct tinypy_sha256_context_t {
    uint32_t state[8];
    uint64_t total_size;
    uint8_t block[64];
    size_t block_size;
} tinypy_sha256_context_t;

void tinypy_sha256_initialize(tinypy_sha256_context_t *context);
void tinypy_sha256_update(tinypy_sha256_context_t *context, const void *data, size_t size);
void tinypy_sha256_finalize(tinypy_sha256_context_t *context, uint8_t digest[32]);
void tinypy_sha256_digest(const void *data, size_t size, uint8_t digest[32]);

#endif
