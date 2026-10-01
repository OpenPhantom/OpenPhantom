/* mp_relay_tls_config.h: the Mbed TLS side of the relay key fetch, and nothing more.
 *
 * One TLS 1.3 client that proves the server with a certificate chain and an ephemeral key
 * exchange. No server, no TLS 1.2, no DTLS, no pre-shared keys, no renegotiation. Session
 * tickets stay compiled in only because the server sends two after the handshake whether or not
 * the client wants them, and a client built without them would have to take that as an
 * unexpected message; they resume nothing, since no key exchange mode with a pre-shared key is
 * built.
 *
 * The TLS 1.3 compatibility mode keeps a middlebox between a player and the internet from dropping
 * the handshake as an unknown protocol, which costs one change-cipher-spec record.
 */
#ifndef MP_RELAY_TLS_CONFIG_H
#define MP_RELAY_TLS_CONFIG_H

#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_PROTO_TLS1_3
#define MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ENABLED
#define MBEDTLS_SSL_TLS1_3_COMPATIBILITY_MODE
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_SESSION_TICKETS

/* TLS 1.3 in this release requires the peer certificate to be kept for the connection. */
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE

#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_USE_C

/* The key document is one line of at most 4096 bytes, so a record is never larger than a
 * certificate chain, which the input buffer has to hold whole. */
#define MBEDTLS_SSL_IN_CONTENT_LEN  16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096

#endif /* MP_RELAY_TLS_CONFIG_H */
