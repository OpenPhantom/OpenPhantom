/* mp_entropy.c: BCryptGenRandom with the system's preferred generator, and HMAC-SHA256 from the
 * same provider. See the header. */
#include "mp_entropy.h"

#include <windows.h>
#include <bcrypt.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The library ships with Windows, so nothing has to be redistributed for it. The feature DLL
 * delay loads it, so it arrives with the first handshake and not with every launch. */
#pragma comment(lib, "bcrypt.lib")

static uint32_t refused;

bool mp_entropy_fill(void *out, size_t bytes)
{
    if (out == NULL || bytes == 0u) {
        return false;
    }
    if (!BCRYPT_SUCCESS(BCryptGenRandom(NULL, (PUCHAR)out, (ULONG)bytes,
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        ++refused;
        return false;
    }
    return true;
}

uint32_t mp_entropy_failures(void)
{
    return refused;
}

/* The algorithm is opened once and kept: opening it costs more than the digest, and a handshake
 * asks for several. */
static BCRYPT_ALG_HANDLE hmac_algorithm;

bool mp_entropy_hmac(const void *key, size_t key_bytes, const void *message,
                     size_t message_bytes, uint8_t out[MP_ENTROPY_HMAC_BYTES])
{
    static const uint8_t ZERO_KEY = 0u;
    BCRYPT_HASH_HANDLE   hash = NULL;
    bool                 done;

    if (out == NULL || (message == NULL && message_bytes != 0u)) {
        return false;
    }
    if (key == NULL || key_bytes == 0u) {
        key       = &ZERO_KEY;
        key_bytes = 1u;
    }
    if (hmac_algorithm == NULL &&
        !BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hmac_algorithm, BCRYPT_SHA256_ALGORITHM,
                                                    NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        hmac_algorithm = NULL;
        ++refused;
        return false;
    }
    done = BCRYPT_SUCCESS(BCryptCreateHash(hmac_algorithm, &hash, NULL, 0, (PUCHAR)key,
                                           (ULONG)key_bytes, 0)) &&
           BCRYPT_SUCCESS(BCryptHashData(hash, (PUCHAR)message, (ULONG)message_bytes, 0)) &&
           BCRYPT_SUCCESS(BCryptFinishHash(hash, out, MP_ENTROPY_HMAC_BYTES, 0));
    if (hash != NULL) {
        (void)BCryptDestroyHash(hash);
    }
    if (!done) {
        ++refused;
    }
    return done;
}

bool mp_entropy_equal(const uint8_t *a, const uint8_t *b, size_t bytes)
{
    uint8_t diff = 0u;
    size_t  i;

    if (a == NULL || b == NULL) {
        return false;
    }
    for (i = 0; i < bytes; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0u;
}
