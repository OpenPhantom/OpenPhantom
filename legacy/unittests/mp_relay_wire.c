/* mp_relay_wire.c and mp_relay_inner.c: the relay protocol's messages, byte for byte.
 *
 * Every line of the relay's own vector file that a client encodes or decodes is held against the
 * code here: an encoder must write the vector's bytes exactly, and a decoder must read the vector
 * back into the inputs the relay's generator used. The refusals follow, because a message the
 * relay would drop in silence is one the client must never send and never believe.
 */
#include "unittest.h"

#include "mp_relay_inner.h"
#include "mp_relay_vectors.h"
#include "mp_relay_wire.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define EPOCH   15000000u
#define NONCE   0x0807060504030201ull
#define COUNTER 0x0102030405060708ull

static const mp_relay_vector_t *vector(const char *name)
{
    size_t i;

    for (i = 0; i < MP_RELAY_VECTOR_COUNT; ++i) {
        if (strcmp(MP_RELAY_VECTORS[i].name, name) == 0) {
            return &MP_RELAY_VECTORS[i];
        }
    }
    ut_check(false, name);
    return NULL;
}

static bool encodes_as(const char *name, const uint8_t *bytes, size_t size)
{
    const mp_relay_vector_t *v = vector(name);

    return v != NULL && v->size == size && memcmp(v->bytes, bytes, size) == 0;
}

static void run_of(uint8_t *out, size_t bytes, uint8_t first)
{
    size_t i;

    for (i = 0; i < bytes; ++i) {
        out[i] = (uint8_t)(first + i);
    }
}

static void test_open_messages(void)
{
    uint8_t               buffer[MP_RELAY_LIST_QUERY_BYTES];
    mp_relay_cookie_t     cookie;
    mp_relay_head_t       head;
    uint8_t               prologue[MP_RELAY_PROLOGUE_BYTES];
    mp_relay_nudge_t      nudge;
    mp_relay_list_query_t query;
    mp_relay_list_page_t  page;
    mp_relay_nack_t       nack;
    uint8_t               cookie_bytes[MP_RELAY_COOKIE_MAC_BYTES];
    uint8_t               announce[MP_RELAY_ANNOUNCE_BYTES];
    const mp_relay_vector_t *v;
    size_t                n;

    ut_section("the open messages match the relay's vectors");
    run_of(cookie_bytes, sizeof cookie_bytes, 0xA0u);
    run_of(announce, sizeof announce, 0x30u);

    n = mp_relay_hello_encode((uint8_t)MP_RELAY_ROLE_MEMBER, NONCE, buffer, sizeof buffer);
    ut_check(n == MP_RELAY_HELLO_BYTES && encodes_as("hello_role_member", buffer, n),
             "a Hello for a player");

    v = vector("cookie_role_member");
    ut_check(v != NULL && mp_relay_cookie_decode(v->bytes, v->size, &cookie) &&
                 cookie.role == MP_RELAY_ROLE_MEMBER && cookie.nonce == NONCE &&
                 cookie.epoch == EPOCH && memcmp(cookie.cookie, cookie_bytes, 16u) == 0,
             "a Cookie reads back its role, nonce, epoch and MAC");

    memset(&head, 0, sizeof head);
    head.epoch  = EPOCH;
    memcpy(head.cookie, cookie_bytes, sizeof head.cookie);
    head.nonce  = NONCE;
    head.key_id = 4u;
    n = mp_relay_head_encode(&head, (uint8_t)MP_RELAY_TYPE_REGISTER, buffer, sizeof buffer);
    ut_check(n == MP_RELAY_REQUEST_BYTES && encodes_as("register_head_key_id_4_suite_0", buffer, n),
             "a Register's head, zero behind it");
    mp_relay_prologue(buffer, prologue);
    ut_check(encodes_as("prologue_register_key_id_4_suite_0", prologue, sizeof prologue),
             "the Register's prologue");
    (void)mp_relay_head_encode(&head, (uint8_t)MP_RELAY_TYPE_JOIN, buffer, sizeof buffer);
    mp_relay_prologue(buffer, prologue);
    ut_check(encodes_as("prologue_join_key_id_4_suite_0", prologue, sizeof prologue),
             "the Join's prologue");
    ut_check(mp_relay_head_encode(&head, (uint8_t)MP_RELAY_TYPE_HELLO, buffer, sizeof buffer) == 0u,
             "a head for any other type is refused");

    v = vector("nudge");
    ut_check(v != NULL && mp_relay_nudge_decode(v->bytes, v->size, &nudge) &&
                 nudge.session_index == 5u && nudge.relay_epoch == EPOCH,
             "a Nudge reads back its session index and epoch");

    memset(&query, 0, sizeof query);
    query.epoch = EPOCH;
    memcpy(query.cookie, cookie_bytes, sizeof query.cookie);
    query.nonce = NONCE;
    query.page  = 1u;
    n = mp_relay_list_query_encode(&query, buffer, sizeof buffer);
    ut_check(n == MP_RELAY_LIST_QUERY_BYTES && encodes_as("list_query_page_1", buffer, n),
             "a ListQuery for page 1");

    v = vector("list_page_two_entries");
    ut_check(v != NULL && mp_relay_list_page_decode(v->bytes, v->size, &page) &&
                 page.page == 1u && page.pages == 2u && page.count == 2u &&
                 page.entry[0].code[0] == 1u && page.entry[1].code[4] == 10u &&
                 memcmp(page.entry[1].announce, announce, sizeof announce) == 0,
             "a ListPage reads back both entries");

    v = vector("nack_unknown_key_ref_suite");
    ut_check(v != NULL && mp_relay_nack_decode(v->bytes, v->size, &nack) &&
                 nack.reason == MP_RELAY_NACK_UNKNOWN_KEY && nack.ref == 2u,
             "a Nack for an unknown suite");
    v = vector("nack_seat_held");
    ut_check(v != NULL && mp_relay_nack_decode(v->bytes, v->size, &nack) &&
                 nack.reason == MP_RELAY_NACK_SEAT_HELD && nack.ref == 0u,
             "a Nack for a held seat");
}

static void test_handshake_bodies(void)
{
    uint8_t                    buffer[MP_RELAY_REGISTERED_BODY_BYTES];
    mp_relay_register_body_t   reg;
    mp_relay_registered_body_t registered;
    mp_relay_join_body_t       join;
    mp_relay_joined_body_t     joined;
    uint8_t                    expect[MP_RELAY_SECRET_BYTES];
    const mp_relay_vector_t   *v;
    size_t                     n;

    ut_section("the handshake bodies match the relay's vectors");
    memset(&reg, 0, sizeof reg);
    reg.flags     = (uint8_t)(MP_RELAY_REGISTER_RESUME | MP_RELAY_REGISTER_HAS_KEY);
    reg.seats     = 3u;
    run_of(reg.key, sizeof reg.key, 0x40u);
    reg.resume_id = 0x1122334455667788ull;
    run_of(reg.resume_proof, sizeof reg.resume_proof, 0xD0u);
    n = mp_relay_register_body_encode(&reg, buffer, sizeof buffer);
    ut_check(n == MP_RELAY_REGISTER_BODY_BYTES &&
                 encodes_as("register_body_resume_with_key", buffer, n),
             "a Register body resuming with a key");
    reg.flags = 0x04u;
    ut_check(mp_relay_register_body_encode(&reg, buffer, sizeof buffer) == 0u,
             "an unknown Register flag is refused");

    v = vector("registered_body");
    run_of(expect, sizeof expect, 0x60u);
    ut_check(v != NULL && mp_relay_registered_body_decode(v->bytes, v->size, &registered) &&
                 registered.session_index == 5u &&
                 registered.session_id == 0x1122334455667788ull &&
                 memcmp(registered.host_secret, expect, sizeof expect) == 0 &&
                 registered.code[0] == 0x12u && registered.code[4] == 0x9Au &&
                 registered.refresh_ms == 20000u && registered.member_refresh_ms == 5000u &&
                 registered.relay_epoch == EPOCH,
             "a Registered body reads back every field");

    memset(&join, 0, sizeof join);
    join.code[0] = 0x12u;
    join.code[1] = 0x34u;
    join.code[2] = 0x56u;
    join.code[3] = 0x78u;
    join.code[4] = 0x9Au;
    join.flags   = (uint8_t)MP_RELAY_JOIN_HAS_PROOF;
    run_of(join.r, sizeof join.r, 0xC0u);
    run_of(join.proof, sizeof join.proof, 0xD0u);
    n = mp_relay_join_body_encode(&join, buffer, sizeof buffer);
    ut_check(n == MP_RELAY_JOIN_BODY_BYTES && encodes_as("join_body_with_proof", buffer, n),
             "a Join body with a proof");

    v = vector("joined_body_continued");
    run_of(expect, sizeof expect, 0x80u);
    ut_check(v != NULL && mp_relay_joined_body_decode(v->bytes, v->size, &joined) &&
                 joined.member_index == 44u && joined.gen == 3u && joined.seats == 3u &&
                 joined.flags == MP_RELAY_JOINED_CONTINUED && joined.member_refresh_ms == 5000u &&
                 joined.r[0] == 0xC0u && memcmp(joined.seat_secret, expect, sizeof expect) == 0,
             "a Joined body that continues a seat reads back every field");
}

static void test_inner_messages(void)
{
    uint8_t                  buffer[64];
    uint8_t                  announce[MP_RELAY_ANNOUNCE_BYTES];
    mp_relay_refresh_ack_t   ack;
    mp_relay_member_open_t   opened;
    mp_relay_member_closed_t closed;
    mp_relay_nack_t          nack;
    const mp_relay_vector_t *v;
    size_t                   n;

    ut_section("the sealed control messages match the relay's vectors");
    run_of(announce, sizeof announce, 0x30u);
    n = mp_relay_refresh_encode(EPOCH, (uint8_t)MP_RELAY_REFRESH_LISTED, announce, buffer,
                                sizeof buffer);
    ut_check(n == MP_RELAY_REFRESH_BYTES &&
                 encodes_as("inner_refresh_listed_epoch_15000000", buffer, n),
             "a listed Refresh");
    v = vector("inner_refresh_ack_counter_0102030405060708");
    ut_check(v != NULL && mp_relay_refresh_ack_decode(v->bytes, v->size, &ack) &&
                 ack.epoch == EPOCH && ack.counter == COUNTER,
             "a RefreshAck reads back its epoch and counter");
    v = vector("inner_member_open_continues");
    ut_check(v != NULL && mp_relay_member_open_decode(v->bytes, v->size, &opened) &&
                 opened.slot == 1u && opened.flags == MP_RELAY_MEMBER_OPEN_CONTINUES &&
                 opened.gen == 3u && opened.handle[0] == 0xB0u && opened.r[15] == 0xCFu,
             "a MemberOpen that continues a seat");
    v = vector("inner_member_closed_kicked");
    ut_check(v != NULL && mp_relay_member_closed_decode(v->bytes, v->size, &closed) &&
                 closed.slot == 1u && closed.reason == MP_RELAY_CLOSE_KICKED && closed.gen == 3u,
             "a MemberClosed for a kick");
    n = mp_relay_close_encode(0x1122334455667788ull, buffer, sizeof buffer);
    ut_check(n == MP_RELAY_CLOSE_BYTES && encodes_as("inner_close", buffer, n), "a Close");
    n = mp_relay_member_refresh_encode(buffer, sizeof buffer);
    ut_check(n == MP_RELAY_MEMBER_REFRESH_BYTES && encodes_as("inner_member_refresh", buffer, n),
             "a MemberRefresh, padded");
    v = vector("inner_member_ack");
    ut_check(v != NULL && mp_relay_member_ack_check(v->bytes, v->size), "a MemberAck");
    n = mp_relay_leave_encode(buffer, sizeof buffer);
    ut_check(n == MP_RELAY_LEAVE_BYTES && encodes_as("inner_leave", buffer, n), "a Leave");
    v = vector("inner_nack_reconnect");
    ut_check(v != NULL && mp_relay_leg_nack_decode(v->bytes, v->size, &nack) &&
                 nack.reason == MP_RELAY_NACK_RECONNECT && nack.ref == 0u,
             "a sealed Nack asking for a new leg");

    ut_section("which inner type travels which way");
    ut_check(mp_relay_inner_allowed((uint8_t)MP_RELAY_TYPE_RELAY_TO_HOST_CONTROL,
                                    (uint8_t)MP_RELAY_INNER_MEMBER_OPEN),
             "a MemberOpen travels from the relay to the host");
    ut_check(!mp_relay_inner_allowed((uint8_t)MP_RELAY_TYPE_RELAY_TO_MEMBER_CONTROL,
                                     (uint8_t)MP_RELAY_INNER_MEMBER_OPEN),
             "and not to a player");
    ut_check(!mp_relay_inner_allowed((uint8_t)MP_RELAY_TYPE_RELAY_TO_HOST_CONTROL,
                                     (uint8_t)MP_RELAY_INNER_REFRESH),
             "a Refresh never comes from the relay");
    ut_check(MP_RELAY_REFRESH_BYTES + MP_RELAY_SEALED_OVERHEAD >= MP_RELAY_NACK_MIN_REQUEST &&
                 MP_RELAY_MEMBER_REFRESH_BYTES + MP_RELAY_SEALED_OVERHEAD >=
                     MP_RELAY_NACK_MIN_REQUEST,
             "both refreshes are large enough for a relay that lost the leg to answer them");
}

static void test_refusals(void)
{
    uint8_t                  buffer[MP_RELAY_LIST_QUERY_BYTES];
    const mp_relay_vector_t *v;
    mp_relay_cookie_t        cookie;
    mp_relay_nack_t          nack;
    mp_relay_list_page_t     page;
    mp_relay_joined_body_t   joined;
    mp_relay_sealed_header_t header;

    ut_section("what the relay would drop is refused");
    v = vector("cookie_role_member");
    memcpy(buffer, v->bytes, v->size);
    ut_check(!mp_relay_cookie_decode(buffer, v->size - 1u, &cookie), "a Cookie one byte short");
    buffer[5] = 1u;
    ut_check(!mp_relay_cookie_decode(buffer, v->size, &cookie), "a Cookie of version 1");
    buffer[5] = 2u;
    buffer[2] = 'X';
    ut_check(!mp_relay_cookie_decode(buffer, v->size, &cookie), "a Cookie with the wrong magic");
    buffer[2] = 'P';
    buffer[10] = 1u;
    ut_check(!mp_relay_cookie_decode(buffer, v->size, &cookie),
             "a Cookie with a reserved byte set");

    v = vector("nack_seat_held");
    memcpy(buffer, v->bytes, v->size);
    buffer[20] = 1u;
    ut_check(!mp_relay_nack_decode(buffer, v->size, &nack), "a Nack with padding set");

    v = vector("list_page_two_entries");
    memcpy(buffer, v->bytes, v->size);
    ut_check(!mp_relay_list_page_decode(buffer, v->size + 44u, &page),
             "a ListPage longer than its count says");
    buffer[12] = 25u;
    ut_check(!mp_relay_list_page_decode(buffer, 16u + 25u * 44u, &page),
             "a ListPage of more than 24 entries");

    v = vector("joined_body_continued");
    memcpy(buffer, v->bytes, v->size);
    buffer[7] = 0x02u;
    ut_check(!mp_relay_joined_body_decode(buffer, v->size, &joined),
             "a Joined body with an unknown flag");

    memset(buffer, 0, sizeof buffer);
    buffer[0] = (uint8_t)MP_RELAY_TYPE_RELAY_TO_MEMBER;
    ut_check(!mp_relay_sealed_header_read(buffer, 31u, &header),
             "a sealed datagram under 32 bytes");
    ut_check(!mp_relay_sealed_header_read(buffer, MP_RELAY_DATAGRAM_MAX + 1u, &header),
             "a sealed datagram over 1232 bytes");
    buffer[0] = (uint8_t)MP_RELAY_TYPE_COOKIE;
    ut_check(!mp_relay_sealed_header_read(buffer, 64u, &header), "an open type as a sealed one");
}

static void test_code_text(void)
{
    static const uint8_t CODE[MP_RELAY_CODE_BYTES] = { 0x12, 0x34, 0x56, 0x78, 0x9A };
    const mp_relay_vector_t *v = vector("code_text_ascii_of_123456789a");
    char                     text[MP_RELAY_CODE_TEXT_BYTES];
    uint8_t                  back[MP_RELAY_CODE_BYTES];
    uint8_t                  code[MP_RELAY_CODE_BYTES];
    unsigned                 value;
    bool                     every = true;

    ut_section("a code as a person reads and types it");
    mp_relay_code_text(CODE, text);
    ut_check(v != NULL && strlen(text) == v->size && memcmp(text, v->bytes, v->size) == 0,
             "12 34 56 78 9a is 28T5-CY4T");
    ut_check(mp_relay_code_parse("28T5-CY4T", back) && memcmp(back, CODE, sizeof back) == 0,
             "and 28T5-CY4T reads back");
    ut_check(mp_relay_code_parse("28t5 cy4t", back) && memcmp(back, CODE, sizeof back) == 0,
             "in lower case and with a space");
    ut_check(mp_relay_code_parse("OOOO-IIII", back) && mp_relay_code_parse("0000-1111", code) &&
                 memcmp(back, code, sizeof back) == 0,
             "O reads as 0 and I as 1");
    ut_check(!mp_relay_code_parse("28T5-CY4U", back), "U is not in the alphabet");
    ut_check(!mp_relay_code_parse("28T5-CY4", back), "seven symbols are not a code");
    ut_check(!mp_relay_code_parse("28T5-CY4TT", back), "nine symbols are not a code");
    ut_check(!mp_relay_code_parse("192.0.2.58", back), "an address is not a code");
    for (value = 0; value < 4096u; ++value) {
        memset(code, 0, sizeof code);
        code[1] = (uint8_t)(value * 37u);
        code[3] = (uint8_t)(value >> 4);
        code[4] = (uint8_t)value;
        mp_relay_code_text(code, text);
        every = every && mp_relay_code_parse(text, back) && memcmp(back, code, sizeof back) == 0;
    }
    ut_check(every, "4096 codes read back as they were written");
}

int main(void)
{
    test_open_messages();
    test_handshake_bodies();
    test_inner_messages();
    test_refusals();
    test_code_text();
    return ut_summary("mp_relay_wire");
}
