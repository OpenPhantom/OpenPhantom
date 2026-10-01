/* mp_relay_keyfetch.c: the relay's key document over TLS 1.3, in a thread of its own. See the
 * header.
 *
 * One fetch runs at a time. The thread fills a result that nothing else touches while it runs, and
 * says it has finished by moving one shared number from "running" to "finished"; the game thread
 * copies the result out only after that, and moves the number back to "idle". No lock is needed
 * for a hand-over that has exactly one writer before the move and exactly one reader after it.
 */
#include "mp_relay_keyfetch.h"

#include "mp_relay_anchors.h"
#include "mp_relay_keysource.h"

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include "common/text.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STATE_IDLE     0
#define STATE_RUNNING  1
#define STATE_FINISHED 2

/* A response carries the headers and a document of at most 4096 bytes; four times that is room for
 * any headers a server sends with it. */
#define RESPONSE_MAX 16384u

/* What the socket callbacks answer Mbed TLS when the socket refused; any negative value is an
 * error to it, and these are the numbers its own network layer uses. */
#define BIO_SEND_FAILED (-0x004E)
#define BIO_RECV_FAILED (-0x004C)

#define THREAD_STACK_BYTES (256u * 1024u)

#define STATE_FILE_DIRECTORY "OpenPhantom"
#define STATE_FILE_NAME      "relay-state.ini"
#define STATE_SECTION        "relay"
#define STATE_KEY            "RelayKeyCache"

static volatile LONG           fetch_state;
static mp_relay_fetch_result_t fetch_result;

typedef struct connection {
    SOCKET    socket;
    ULONGLONG deadline;
} connection_t;

static uint32_t remaining_ms(ULONGLONG deadline)
{
    ULONGLONG now = GetTickCount64();

    return now >= deadline ? 0u : (uint32_t)(deadline - now);
}

static int bio_send(void *context, const unsigned char *buffer, size_t bytes)
{
    const connection_t *c = (const connection_t *)context;
    int                 sent;

    if (remaining_ms(c->deadline) == 0u) {
        return BIO_SEND_FAILED;
    }
    sent = send(c->socket, (const char *)buffer, (int)(bytes > 65536u ? 65536u : bytes), 0);
    return sent == SOCKET_ERROR ? BIO_SEND_FAILED : sent;
}

static int bio_recv(void *context, unsigned char *buffer, size_t bytes)
{
    const connection_t *c = (const connection_t *)context;
    int                 got;

    if (remaining_ms(c->deadline) == 0u) {
        return BIO_RECV_FAILED;
    }
    got = recv(c->socket, (char *)buffer, (int)(bytes > 65536u ? 65536u : bytes), 0);
    return got == SOCKET_ERROR ? BIO_RECV_FAILED : got;
}

/* A TCP connection to one address, given up at the deadline. */
static SOCKET connect_by(const struct addrinfo *address, ULONGLONG deadline)
{
    SOCKET         s = socket(address->ai_family, SOCK_STREAM, IPPROTO_TCP);
    u_long         non_blocking = 1u;
    fd_set         writable;
    fd_set         failed;
    struct timeval wait;
    uint32_t       left;
    int            error = 0;
    int            error_bytes = sizeof error;
    DWORD          io_timeout;

    if (s == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    if (ioctlsocket(s, FIONBIO, &non_blocking) != 0 ||
        (connect(s, address->ai_addr, (int)address->ai_addrlen) != 0 &&
         WSAGetLastError() != WSAEWOULDBLOCK)) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    left = remaining_ms(deadline);
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(s, &writable);
    FD_SET(s, &failed);
    wait.tv_sec  = (long)(left / 1000u);
    wait.tv_usec = (long)((left % 1000u) * 1000u);
    if (left == 0u || select(0, NULL, &writable, &failed, &wait) != 1 || !FD_ISSET(s, &writable) ||
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&error, &error_bytes) != 0 || error != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    non_blocking = 0u;
    io_timeout   = (DWORD)MP_RELAY_FETCH_TIMEOUT_MS;
    if (ioctlsocket(s, FIONBIO, &non_blocking) != 0 ||
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&io_timeout, sizeof io_timeout) != 0 ||
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&io_timeout, sizeof io_timeout) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

/* The relay's UDP addresses, IPv6 first, from one lookup. */
static void look_up_addresses(mp_relay_fetch_result_t *result)
{
    struct addrinfo  hints;
    struct addrinfo *list = NULL;
    char             port[8];
    int              pass;

    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    (void)text_format(port, sizeof port, "%u", (unsigned)MP_RELAY_UDP_PORT);
    if (getaddrinfo(MP_RELAY_HOST, port, &hints, &list) != 0) {
        return;
    }
    for (pass = 0; pass < 2; ++pass) {
        const struct addrinfo *ai;

        for (ai = list; ai != NULL && result->addresses < MP_RELAY_ADDRESSES_MAX;
             ai = ai->ai_next) {
            mp_relay_address_t *out = &result->address[result->addresses];

            if (pass == 0 && ai->ai_family == AF_INET6) {
                const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)ai->ai_addr;

                memset(out, 0, sizeof *out);
                out->ipv6 = true;
                memcpy(out->bytes, &in6->sin6_addr, 16u);
                out->port = ntohs(in6->sin6_port);
                ++result->addresses;
            } else if (pass == 1 && ai->ai_family == AF_INET) {
                const struct sockaddr_in *in4 = (const struct sockaddr_in *)ai->ai_addr;

                memset(out, 0, sizeof *out);
                memcpy(out->bytes, &in4->sin_addr, 4u);
                out->port = ntohs(in4->sin_port);
                ++result->addresses;
            }
        }
    }
    freeaddrinfo(list);
}

/* Every certificate the server sent carries an ECDSA key: an RSA key on the way to the relay's
 * name would mean something changed that nobody decided. */
static bool every_key_is_ecdsa(const mbedtls_ssl_context *ssl)
{
    const mbedtls_x509_crt *certificate = mbedtls_ssl_get_peer_cert(ssl);

    if (certificate == NULL) {
        return false;
    }
    for (; certificate != NULL; certificate = certificate->next) {
        if (!PSA_KEY_TYPE_IS_ECC(mbedtls_pk_get_key_type(&certificate->pk))) {
            return false;
        }
    }
    return true;
}

static bool write_all(mbedtls_ssl_context *ssl, const char *text, ULONGLONG deadline)
{
    size_t length = strlen(text);
    size_t done   = 0;

    while (done < length) {
        int ret = mbedtls_ssl_write(ssl, (const unsigned char *)text + done, length - done);

        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (remaining_ms(deadline) == 0u) {
                return false;
            }
            continue;
        }
        if (ret <= 0) {
            return false;
        }
        done += (size_t)ret;
    }
    return true;
}

/* Reads until the server closes, the buffer is full or the deadline passes. The server sends its
 * session tickets after the handshake whether or not anybody asked; they are passed over. */
static bool read_all(mbedtls_ssl_context *ssl, char *response, size_t *bytes, ULONGLONG deadline)
{
    *bytes = 0u;
    for (;;) {
        int ret;

        if (*bytes >= RESPONSE_MAX || remaining_ms(deadline) == 0u) {
            return *bytes != 0u;
        }
        ret = mbedtls_ssl_read(ssl, (unsigned char *)response + *bytes, RESPONSE_MAX - *bytes);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE ||
            ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
            continue;
        }
        if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
            ret == MBEDTLS_ERR_SSL_CONN_EOF) {
            return true;
        }
        if (ret < 0) {
            return false;
        }
        *bytes += (size_t)ret;
    }
}

static void fetch_document(mp_relay_fetch_result_t *result, ULONGLONG deadline)
{
    static const int SUITES[] = {
        MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256, MBEDTLS_TLS1_3_AES_128_GCM_SHA256, 0
    };
    static const uint16_t GROUPS[] = {
        MBEDTLS_SSL_IANA_TLS_GROUP_X25519, MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1, 0
    };
    static const char REQUEST[] = "GET " MP_RELAY_KEY_PATH " HTTP/1.1\r\n"
                                  "Host: " MP_RELAY_HOST "\r\n"
                                  "Connection: close\r\n"
                                  "Accept-Encoding: identity\r\n"
                                  "\r\n";
    static char         response[RESPONSE_MAX + 1u];
    struct addrinfo     hints;
    struct addrinfo    *list = NULL;
    const struct addrinfo *ai;
    connection_t        connection = { INVALID_SOCKET, 0 };
    mbedtls_x509_crt    anchors;
    mbedtls_ssl_config  config;
    mbedtls_ssl_context ssl;
    const char         *body = NULL;
    size_t              body_bytes = 0;
    size_t              got = 0;
    int                 ret;

    connection.deadline = deadline;
    if (!mp_relay_anchors_intact()) {
        result->error = MP_RELAY_FETCH_ERROR_ANCHORS;
        return;
    }
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (getaddrinfo(MP_RELAY_HOST, "443", &hints, &list) != 0) {
        result->error = MP_RELAY_FETCH_ERROR_RESOLVE;
        return;
    }
    for (ai = list; ai != NULL && connection.socket == INVALID_SOCKET; ai = ai->ai_next) {
        connection.socket = connect_by(ai, deadline);
        if (connection.socket != INVALID_SOCKET) {
            result->over_ipv6 = ai->ai_family == AF_INET6;
        }
    }
    freeaddrinfo(list);
    if (connection.socket == INVALID_SOCKET) {
        result->error = MP_RELAY_FETCH_ERROR_CONNECT;
        return;
    }

    mbedtls_x509_crt_init(&anchors);
    mbedtls_ssl_config_init(&config);
    mbedtls_ssl_init(&ssl);
    if (psa_crypto_init() != PSA_SUCCESS ||
        mbedtls_x509_crt_parse_der(&anchors, MP_RELAY_ANCHOR_YE, sizeof MP_RELAY_ANCHOR_YE) != 0 ||
        mbedtls_x509_crt_parse_der(&anchors, MP_RELAY_ANCHOR_X2, sizeof MP_RELAY_ANCHOR_X2) != 0) {
        result->error = MP_RELAY_FETCH_ERROR_ANCHORS;
        goto done;
    }
    ret = mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret == 0) {
        mbedtls_ssl_conf_min_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_max_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&config, &anchors, NULL);
        mbedtls_ssl_conf_ciphersuites(&config, SUITES);
        mbedtls_ssl_conf_groups(&config, GROUPS);
        ret = mbedtls_ssl_setup(&ssl, &config);
    }
    if (ret == 0) {
        ret = mbedtls_ssl_set_hostname(&ssl, MP_RELAY_HOST);
    }
    if (ret != 0) {
        result->error  = MP_RELAY_FETCH_ERROR_TLS;
        result->detail = ret;
        goto done;
    }
    mbedtls_ssl_set_bio(&ssl, &connection, bio_send, bio_recv, NULL);
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if ((ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) ||
            remaining_ms(deadline) == 0u) {
            result->error  = MP_RELAY_FETCH_ERROR_TLS;
            result->detail = ret;
            goto done;
        }
    }
    if (!every_key_is_ecdsa(&ssl)) {
        result->error = MP_RELAY_FETCH_ERROR_NOT_ECDSA;
        goto done;
    }
    if (!write_all(&ssl, REQUEST, deadline) || !read_all(&ssl, response, &got, deadline)) {
        result->error = MP_RELAY_FETCH_ERROR_READ;
        goto done;
    }
    response[got] = '\0';
    ret = (int)mp_relay_http_body(response, got, &result->status, &body, &body_bytes);
    if (ret != (int)MP_RELAY_HTTP_OK) {
        result->error  = MP_RELAY_FETCH_ERROR_HTTP;
        result->detail = ret;
        goto done;
    }
    ret = (int)mp_relay_keydoc_parse(body, body_bytes, &result->key);
    if (ret != (int)MP_RELAY_KEYDOC_OK) {
        result->error  = MP_RELAY_FETCH_ERROR_DOCUMENT;
        result->detail = ret;
        goto done;
    }
    result->key_fetched = true;
    (void)mbedtls_ssl_close_notify(&ssl);

done:
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
    mbedtls_x509_crt_free(&anchors);
    closesocket(connection.socket);
}

static DWORD WINAPI fetch_thread(LPVOID parameter)
{
    ULONGLONG start    = GetTickCount64();
    ULONGLONG deadline = start + MP_RELAY_FETCH_TIMEOUT_MS;
    WSADATA   wsa;

    memset(&fetch_result, 0, sizeof fetch_result);
    fetch_result.wanted_key = parameter != NULL;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fetch_result.error = MP_RELAY_FETCH_ERROR_RESOLVE;
    } else {
        look_up_addresses(&fetch_result);
        if (fetch_result.wanted_key) {
            fetch_document(&fetch_result, deadline);
        }
        WSACleanup();
    }
    fetch_result.elapsed_ms = (uint32_t)(GetTickCount64() - start);
    InterlockedExchange(&fetch_state, STATE_FINISHED);
    return 0;
}

bool mp_relay_fetch_begin(bool want_key)
{
    HANDLE thread;

    if (InterlockedCompareExchange(&fetch_state, STATE_RUNNING, STATE_IDLE) != STATE_IDLE) {
        return false;
    }
    thread = CreateThread(NULL, THREAD_STACK_BYTES, fetch_thread, want_key ? (LPVOID)1 : NULL,
                          STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (thread == NULL) {
        memset(&fetch_result, 0, sizeof fetch_result);
        fetch_result.wanted_key = want_key;
        fetch_result.error      = MP_RELAY_FETCH_ERROR_THREAD;
        InterlockedExchange(&fetch_state, STATE_FINISHED);
        return false;
    }
    CloseHandle(thread);
    return true;
}

bool mp_relay_fetch_running(void)
{
    return InterlockedCompareExchange(&fetch_state, STATE_RUNNING, STATE_RUNNING) ==
           STATE_RUNNING;
}

bool mp_relay_fetch_take(mp_relay_fetch_result_t *out)
{
    if (out == NULL ||
        InterlockedCompareExchange(&fetch_state, STATE_FINISHED, STATE_FINISHED) !=
            STATE_FINISHED) {
        return false;
    }
    *out = fetch_result;
    InterlockedExchange(&fetch_state, STATE_IDLE);
    return true;
}

const char *mp_relay_fetch_error_text(mp_relay_fetch_error_t error)
{
    switch (error) {
    case MP_RELAY_FETCH_ERROR_NONE:      return "no error";
    case MP_RELAY_FETCH_ERROR_THREAD:    return "the fetch thread would not start";
    case MP_RELAY_FETCH_ERROR_ANCHORS:   return "the built-in anchors are damaged";
    case MP_RELAY_FETCH_ERROR_RESOLVE:   return "the relay's name did not resolve";
    case MP_RELAY_FETCH_ERROR_CONNECT:   return "no connection to the relay in ten seconds";
    case MP_RELAY_FETCH_ERROR_TLS:       return "the TLS 1.3 handshake failed";
    case MP_RELAY_FETCH_ERROR_NOT_ECDSA: return "a certificate on the way is not ECDSA";
    case MP_RELAY_FETCH_ERROR_READ:      return "the answer broke off";
    case MP_RELAY_FETCH_ERROR_HTTP:      return "the answer is not one that is taken";
    case MP_RELAY_FETCH_ERROR_DOCUMENT:  return "the key document was refused";
    case MP_RELAY_FETCH_ERROR_COUNT:
    default:                             return "an unnamed error";
    }
}

/* ==============================================================================================
 * The key kept between runs.
 * ============================================================================================ */

static bool state_path(char *out, size_t capacity, bool create)
{
    char   base[MAX_PATH];
    DWORD  length = GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof base);
    size_t whole;

    if (length == 0u || length >= sizeof base) {
        return false;
    }
    /* Each path is the base and fixed names, so its whole length is known, and a buffer that
     * took fewer characters than that cut it. */
    whole = (size_t)length + sizeof "\\" STATE_FILE_DIRECTORY - 1u;
    if (text_format(out, capacity, "%s\\%s", base, STATE_FILE_DIRECTORY) != whole) {
        return false;
    }
    if (create && !CreateDirectoryA(out, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    whole += sizeof "\\" STATE_FILE_NAME - 1u;
    return text_format(out, capacity, "%s\\%s\\%s", base, STATE_FILE_DIRECTORY,
                       STATE_FILE_NAME) == whole;
}

bool mp_relay_state_load(mp_relay_key_t *key, int64_t *at)
{
    char path[MAX_PATH];
    char line[MP_RELAY_CACHE_LINE_BYTES];

    if (!state_path(path, sizeof path, false)) {
        return false;
    }
    (void)GetPrivateProfileStringA(STATE_SECTION, STATE_KEY, "", line, sizeof line, path);
    return line[0] != '\0' && mp_relay_cache_line_read(line, key, at);
}

bool mp_relay_state_save(const mp_relay_key_t *key, int64_t at)
{
    char path[MAX_PATH];
    char line[MP_RELAY_CACHE_LINE_BYTES];

    return state_path(path, sizeof path, true) &&
           mp_relay_cache_line_write(key, at, line, sizeof line) != 0u &&
           WritePrivateProfileStringA(STATE_SECTION, STATE_KEY, line, path) != 0;
}

int64_t mp_relay_unix_now(void)
{
    FILETIME       now;
    ULARGE_INTEGER ticks;

    GetSystemTimeAsFileTime(&now);
    ticks.LowPart  = now.dwLowDateTime;
    ticks.HighPart = now.dwHighDateTime;
    return (int64_t)((ticks.QuadPart - 116444736000000000ull) / 10000000ull);
}
