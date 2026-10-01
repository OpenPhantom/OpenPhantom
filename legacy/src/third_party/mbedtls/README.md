# Mbed TLS for the relay key fetch

**Source:** the release archive `mbedtls-4.1.1.tar.bz2` of https://github.com/Mbed-TLS/mbedtls,
which carries TF-PSA-Crypto. The configure step downloads it (`FetchContent` in `../CMakeLists.txt`)
and refuses it unless its SHA-256 is
`3359a349e23db3d5536fcee032ae7b2ecbfc08972fab643089b5cbf2a375c98c`, the value the release publishes.
Nothing of it is kept in this tree. Licence: Apache 2.0 (the project is dual licensed Apache 2.0 or
GPL 2.0 or later; this build takes Apache 2.0).

**What it is for.** `mp_relay_keyfetch.c` fetches `https://relay.swopenphantom.org/mp-relay/v2/key`
over TLS 1.3, proving the server against two anchors only, ISRG Root YE and ISRG Root X2, never the
system store.

**Configuration.** `mp_relay_tls_config.h` replaces the TLS default and `mp_relay_psa_config.h` the
cryptography default: a TLS 1.3 client with ephemeral key exchange, the suites ChaCha20-Poly1305 and
AES-128-GCM, X25519 and P-256, ECDSA certificates on P-256 and P-384. Each header says why each line
is there.

**Updating.** Change the URL and the hash together, build, and run the fetch against the live relay.
