#include "sha256.h"

#include <string.h>

#define TINYPY_SHA256_ROTATE_RIGHT(value, count) \
    (((value) >> (count)) | ((value) << (32U - (count))))

static const uint32_t tinypy_sha256_round_constants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
};

static uint32_t __tinypy_sha256_read_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) |
        ((uint32_t)data[1] << 16U) |
        ((uint32_t)data[2] << 8U) |
        (uint32_t)data[3];
}

static void __tinypy_sha256_write_u32_be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static void __tinypy_sha256_transform(
    tinypy_sha256_context_t *context,
    const uint8_t block[64])
{
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    size_t index;

    for (index = 0U; index != 16U; ++index) {
        words[index] = __tinypy_sha256_read_u32_be(block + index * 4U);
    }

    for (index = 16U; index != 64U; ++index) {
        uint32_t x = words[index - 15U];
        uint32_t y = words[index - 2U];
        uint32_t sigma0 = TINYPY_SHA256_ROTATE_RIGHT(x, 7U) ^
            TINYPY_SHA256_ROTATE_RIGHT(x, 18U) ^ (x >> 3U);
        uint32_t sigma1 = TINYPY_SHA256_ROTATE_RIGHT(y, 17U) ^
            TINYPY_SHA256_ROTATE_RIGHT(y, 19U) ^ (y >> 10U);
        words[index] = words[index - 16U] + sigma0 +
            words[index - 7U] + sigma1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (index = 0U; index != 64U; ++index) {
        uint32_t sum1 = TINYPY_SHA256_ROTATE_RIGHT(e, 6U) ^
            TINYPY_SHA256_ROTATE_RIGHT(e, 11U) ^
            TINYPY_SHA256_ROTATE_RIGHT(e, 25U);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temporary1 = h + sum1 + choice +
            tinypy_sha256_round_constants[index] + words[index];
        uint32_t sum0 = TINYPY_SHA256_ROTATE_RIGHT(a, 2U) ^
            TINYPY_SHA256_ROTATE_RIGHT(a, 13U) ^
            TINYPY_SHA256_ROTATE_RIGHT(a, 22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void tinypy_sha256_initialize(tinypy_sha256_context_t *context)
{
    context->state[0] = UINT32_C(0x6a09e667);
    context->state[1] = UINT32_C(0xbb67ae85);
    context->state[2] = UINT32_C(0x3c6ef372);
    context->state[3] = UINT32_C(0xa54ff53a);
    context->state[4] = UINT32_C(0x510e527f);
    context->state[5] = UINT32_C(0x9b05688c);
    context->state[6] = UINT32_C(0x1f83d9ab);
    context->state[7] = UINT32_C(0x5be0cd19);
    context->total_size = UINT64_C(0);
    context->block_size = 0U;
}

void tinypy_sha256_update(
    tinypy_sha256_context_t *context,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;

    if (size == 0U) {
        return;
    }

    context->total_size += (uint64_t)size;

    if (context->block_size != 0U) {
        size_t available = 64U - context->block_size;
        size_t copied = size < available ? size : available;
        (void)memcpy(context->block + context->block_size, bytes, copied);
        context->block_size += copied;
        bytes += copied;
        size -= copied;

        if (context->block_size == 64U) {
            __tinypy_sha256_transform(context, context->block);
            context->block_size = 0U;
        }
    }

    while (size >= 64U) {
        __tinypy_sha256_transform(context, bytes);
        bytes += 64U;
        size -= 64U;
    }

    if (size != 0U) {
        (void)memcpy(context->block, bytes, size);
        context->block_size = size;
    }
}

void tinypy_sha256_finalize(
    tinypy_sha256_context_t *context,
    uint8_t digest[32])
{
    uint64_t bit_size = context->total_size * UINT64_C(8);
    size_t index;

    context->block[context->block_size++] = UINT8_C(0x80);
    if (context->block_size > 56U) {
        (void)memset(
            context->block + context->block_size,
            0,
            64U - context->block_size);
        __tinypy_sha256_transform(context, context->block);
        context->block_size = 0U;
    }

    (void)memset(
        context->block + context->block_size,
        0,
        56U - context->block_size);
    for (index = 0U; index != 8U; ++index) {
        context->block[63U - index] = (uint8_t)(bit_size >> (index * 8U));
    }
    __tinypy_sha256_transform(context, context->block);

    for (index = 0U; index != 8U; ++index) {
        __tinypy_sha256_write_u32_be(digest + index * 4U, context->state[index]);
    }

    (void)memset(context, 0, sizeof(*context));
}

void tinypy_sha256_digest(const void *data, size_t size, uint8_t digest[32])
{
    tinypy_sha256_context_t context;
    tinypy_sha256_initialize(&context);
    tinypy_sha256_update(&context, data, size);
    tinypy_sha256_finalize(&context, digest);
}
