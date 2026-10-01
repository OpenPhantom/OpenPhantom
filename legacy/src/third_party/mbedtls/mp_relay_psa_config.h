/* mp_relay_psa_config.h: the cryptography the relay key fetch's TLS 1.3 client needs.
 *
 * Key exchange with X25519 or P-256; the two TLS 1.3 suites ChaCha20-Poly1305 and AES-128-GCM
 * with SHA-256, and SHA-384 because the key schedule of a server that prefers it needs it;
 * certificates verified with ECDSA on P-256 and P-384, which is what both ISRG roots, the
 * intermediate and the relay's own certificate are. No RSA key exchange and no RSA anchor, no
 * classic Diffie-Hellman, no other curve, no key storage.
 */
#ifndef PSA_CRYPTO_CONFIG_H
#define PSA_CRYPTO_CONFIG_H

#define PSA_WANT_ALG_CHACHA20_POLY1305          1
#define PSA_WANT_ALG_GCM                        1
#define PSA_WANT_ALG_ECDH                       1
#define PSA_WANT_ALG_ECDSA                      1
#define PSA_WANT_ALG_HKDF                       1
#define PSA_WANT_ALG_HKDF_EXTRACT               1
#define PSA_WANT_ALG_HKDF_EXPAND                1
#define PSA_WANT_ALG_HMAC                       1
#define PSA_WANT_ALG_SHA_256                    1
#define PSA_WANT_ALG_SHA_384                    1

/* The relay sends four certificates, and the last is ISRG Root X2 cross-signed by the RSA root
 * X1. The chain is proved from the relay's certificate through YE1 to the anchor Root YE with
 * ECDSA alone, but a certificate whose signature algorithm is unknown cannot be parsed, and the
 * whole chain is then refused. So the RSA signature is known; no RSA key is ever an anchor here.
 * This release lists the RSA signature identifiers only while the RSA key pair type is built. */
#define PSA_WANT_ALG_RSA_PKCS1V15_SIGN          1
#define PSA_WANT_KEY_TYPE_RSA_PUBLIC_KEY        1
#define PSA_WANT_KEY_TYPE_RSA_KEY_PAIR_BASIC    1

#define PSA_WANT_ECC_MONTGOMERY_255             1
#define PSA_WANT_ECC_SECP_R1_256                1
#define PSA_WANT_ECC_SECP_R1_384                1

#define PSA_WANT_KEY_TYPE_AES                   1
#define PSA_WANT_KEY_TYPE_CHACHA20              1
#define PSA_WANT_KEY_TYPE_DERIVE                1
#define PSA_WANT_KEY_TYPE_HMAC                  1
#define PSA_WANT_KEY_TYPE_RAW_DATA              1
#define PSA_WANT_KEY_TYPE_ECC_PUBLIC_KEY        1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_BASIC    1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_IMPORT   1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_EXPORT   1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_GENERATE 1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_DERIVE   1

#define MBEDTLS_PSA_CRYPTO_C
#define MBEDTLS_PSA_BUILTIN_GET_ENTROPY
#define MBEDTLS_PSA_KEY_STORE_DYNAMIC
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_MD_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_ASN1_PARSE_C

/* A certificate's validity is checked against the clock. */
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE
#define MBEDTLS_PLATFORM_C

#define MBEDTLS_HAVE_ASM
#define MBEDTLS_ECP_NIST_OPTIM

#endif /* PSA_CRYPTO_CONFIG_H */
