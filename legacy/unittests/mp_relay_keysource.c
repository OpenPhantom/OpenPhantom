/* mp_relay_keysource.c: which relay key a handshake uses, and what the fetch hands back.
 *
 * The rules are those of the relay server's reference client and its tests: an hour for the
 * key in hand, seven days for the kept one and never a negative age, one refetch a minute and three
 * a start. The line on disk and the HTTP answer are read by the same module and pinned here too.
 */
#include "unittest.h"

#include "mp_relay_keysource.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define DAY (24 * 3600)

static mp_relay_key_t a_key(uint8_t id, uint8_t fill)
{
    mp_relay_key_t key;

    memset(&key, 0, sizeof key);
    key.id = id;
    memset(key.public_key, fill, sizeof key.public_key);
    return key;
}

static void test_the_hour_and_the_fetch(void)
{
    mp_relay_keysource_t source;
    mp_relay_key_t       key = a_key(1u, 0x11u);

    ut_section("the key in hand is used for an hour");
    mp_relay_keysource_init(&source);
    ut_check(mp_relay_keysource_step(&source, 1000) == MP_RELAY_KEY_FETCH,
             "with nothing in hand a handshake fetches first");
    mp_relay_keysource_fetch_began(&source, 1000);
    ut_check(mp_relay_keysource_fetch_ended(&source, true, &key, 1000) == MP_RELAY_FETCH_FRESH,
             "a fetch that read the document is fresh");
    ut_check(mp_relay_keysource_step(&source, 1000 + 3599) == MP_RELAY_KEY_USE,
             "the key is used for the hour");
    ut_check(mp_relay_keysource_step(&source, 1000 + 3600) == MP_RELAY_KEY_FETCH,
             "and fetched again after it");
    ut_check(mp_relay_keysource_step(&source, 999) == MP_RELAY_KEY_FETCH,
             "a clock that moved back fetches rather than trusting the key's age");
}

static void test_what_may_stand_in(void)
{
    mp_relay_keysource_t source;
    mp_relay_key_t       kept = a_key(2u, 0x22u);

    ut_section("what may stand in when the fetch fails");
    mp_relay_keysource_init(&source);
    ut_check(mp_relay_keysource_fetch_ended(&source, false, NULL, 5000) == MP_RELAY_FETCH_FAILED,
             "nothing kept: the failure stands");
    mp_relay_keysource_set_cache(&source, &kept, 5000);
    ut_check(mp_relay_keysource_fetch_ended(&source, false, NULL, 5000 + 7 * DAY) ==
                     MP_RELAY_FETCH_STAND_IN &&
                 source.have && source.key.id == 2u,
             "a key kept seven days ago stands in");
    ut_check(mp_relay_keysource_step(&source, 5000 + 7 * DAY + 10) == MP_RELAY_KEY_USE,
             "and counts as fetched now, so the next attempt waits an hour");
    mp_relay_keysource_init(&source);
    mp_relay_keysource_set_cache(&source, &kept, 5000);
    ut_check(mp_relay_keysource_fetch_ended(&source, false, NULL, 5000 + 7 * DAY + 1) ==
                     MP_RELAY_FETCH_TOO_OLD &&
                 !source.have,
             "one second older does not");
    ut_check(mp_relay_keysource_fetch_ended(&source, false, NULL, 4999) == MP_RELAY_FETCH_FUTURE &&
                 !source.have,
             "a kept key from the future does not either: the clock moved back");
}

static void test_the_refetch_budget(void)
{
    mp_relay_keysource_t source;
    mp_relay_key_t       key = a_key(3u, 0x33u);

    ut_section("a refused key is fetched again once a minute and three times a start");
    mp_relay_keysource_init(&source);
    mp_relay_keysource_fetch_began(&source, 0);
    (void)mp_relay_keysource_fetch_ended(&source, true, &key, 0);
    ut_check(mp_relay_keysource_refetch(&source, 30) == MP_RELAY_REFETCH_HOLD,
             "within the minute the key in hand stands");
    ut_check(mp_relay_keysource_refetch(&source, 60) == MP_RELAY_REFETCH_GO, "the first refetch");
    mp_relay_keysource_fetch_began(&source, 60);
    ut_check(mp_relay_keysource_refetch(&source, 120) == MP_RELAY_REFETCH_GO, "the second");
    mp_relay_keysource_fetch_began(&source, 120);
    ut_check(mp_relay_keysource_refetch(&source, 180) == MP_RELAY_REFETCH_GO, "the third");
    mp_relay_keysource_fetch_began(&source, 180);
    ut_check(mp_relay_keysource_refetch(&source, 999999) == MP_RELAY_REFETCH_SPENT,
             "and no fourth, however long it waits");

    mp_relay_keysource_init(&source);
    mp_relay_keysource_fetch_began(&source, 0);
    (void)mp_relay_keysource_fetch_ended(&source, false, NULL, 0);
    ut_check(mp_relay_keysource_refetch(&source, 10) == MP_RELAY_REFETCH_NONE,
             "within the minute with no key in hand there is nothing to try");
}

static void test_the_line_on_disk(void)
{
    mp_relay_key_t key = a_key(9u, 0xABu);
    mp_relay_key_t back;
    char           line[MP_RELAY_CACHE_LINE_BYTES];
    int64_t        at = 0;

    ut_section("the line kept on disk");
    ut_check(mp_relay_cache_line_write(&key, 1789599227, line, sizeof line) != 0u &&
                 strncmp(line, "9:abababab", 10u) == 0,
             "it is written as id, hex, suite and seconds");
    ut_check(mp_relay_cache_line_read(line, &back, &at) && back.id == 9u &&
                 memcmp(back.public_key, key.public_key, 32u) == 0 && at == 1789599227,
             "and reads back");
    ut_check(!mp_relay_cache_line_read("0:abababababababababababababababababababababababababababab"
                                       "abababab:0:1", &back, &at),
             "an id of 0 is refused");
    ut_check(!mp_relay_cache_line_read("1:ABABABABABABABABABABABABABABABABABABABABABABABABABABABAB"
                                       "ABABABAB:0:1", &back, &at),
             "upper case hex is refused");
    ut_check(!mp_relay_cache_line_read("1:abababababababababababababababababababababababababababab"
                                       "abababab:1:1", &back, &at),
             "a suite this build does not speak is refused");
    ut_check(!mp_relay_cache_line_read("1:abababababababababababababababababababababababababababab"
                                       "abababab:0:1x", &back, &at),
             "anything after the seconds is refused");
    ut_check(!mp_relay_cache_line_read("1:00000000000000000000000000000000000000000000000000000000"
                                       "00000000:0:1", &back, &at),
             "a key of zeros is refused");
}

static void test_the_answer(void)
{
    static const char OK[] = "HTTP/1.1 200 OK\r\nServer: nginx\r\ncontent-LENGTH: 5\r\n"
                             "Connection: close\r\n\r\nhello and more";
    static const char MOVED[] = "HTTP/1.1 301 Moved\r\nContent-Length: 0\r\n\r\n";
    static const char CHUNKED[] = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"
                                  "Content-Length: 5\r\n\r\nhello";
    static const char NO_LENGTH[] = "HTTP/1.1 200 OK\r\nServer: nginx\r\n\r\nhello";
    static const char TOO_LONG[] = "HTTP/1.1 200 OK\r\nContent-Length: 4097\r\n\r\nhello";
    static const char SHORT[] = "HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nhello";
    static const char TWICE[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\n"
                                "hello";
    static const char HALF[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n";
    const char *body = NULL;
    size_t      body_bytes = 0;
    unsigned    status = 0;

    ut_section("the answer to the fetch");
    ut_check(mp_relay_http_body(OK, sizeof OK - 1u, &status, &body, &body_bytes) ==
                     MP_RELAY_HTTP_OK &&
                 status == 200u && body_bytes == 5u && memcmp(body, "hello", 5u) == 0,
             "a 200 with a length gives exactly that many bytes, the header name in any case");
    ut_check(mp_relay_http_body(MOVED, sizeof MOVED - 1u, &status, &body, &body_bytes) ==
                     MP_RELAY_HTTP_STATUS &&
                 status == 301u,
             "a redirect is not followed, it is refused");
    ut_check(mp_relay_http_body(CHUNKED, sizeof CHUNKED - 1u, &status, &body, &body_bytes) ==
                 MP_RELAY_HTTP_CHUNKED,
             "a Transfer-Encoding is refused even beside a length");
    ut_check(mp_relay_http_body(NO_LENGTH, sizeof NO_LENGTH - 1u, &status, &body, &body_bytes) ==
                 MP_RELAY_HTTP_NO_LENGTH,
             "no length is refused");
    ut_check(mp_relay_http_body(TWICE, sizeof TWICE - 1u, &status, &body, &body_bytes) ==
                 MP_RELAY_HTTP_NO_LENGTH,
             "two lengths are refused");
    ut_check(mp_relay_http_body(TOO_LONG, sizeof TOO_LONG - 1u, &status, &body, &body_bytes) ==
                 MP_RELAY_HTTP_TOO_LONG,
             "a length over 4096 is refused before a byte is read");
    ut_check(mp_relay_http_body(SHORT, sizeof SHORT - 1u, &status, &body, &body_bytes) ==
                 MP_RELAY_HTTP_SHORT,
             "fewer bytes than the length said is refused");
    ut_check(mp_relay_http_body(HALF, sizeof HALF - 1u, &status, &body, &body_bytes) ==
                 MP_RELAY_HTTP_INCOMPLETE,
             "headers that never end are refused");
}

int main(void)
{
    test_the_hour_and_the_fetch();
    test_what_may_stand_in();
    test_the_refetch_budget();
    test_the_line_on_disk();
    test_the_answer();
    return ut_summary("mp_relay_keysource");
}
