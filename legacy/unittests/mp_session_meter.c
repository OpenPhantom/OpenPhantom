/* mp_session_meter.c: bytes and packets a second, and the size of a payload packet. */
#include "unittest.h"

#include "mp_session_meter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_the_second(void)
{
    mp_meter_second_t second;

    ut_section("a second is counted from its first datagram, and the most of any is kept");
    memset(&second, 0, sizeof second);
    mp_meter_second_add(&second, 1000u, 100u);
    mp_meter_second_add(&second, 1000u, 600u);
    mp_meter_second_add(&second, 1000u, 1099u);
    ut_check(second.bytes == 3000u && second.packets == 3u,
             "three datagrams inside one second from the first are one second");
    mp_meter_second_add(&second, 500u, 1100u);
    ut_check(second.bytes == 500u && second.most_bytes == 3000u && second.most_packets == 3u,
             "the next a second later opens the next second, and the peak stays");
    mp_meter_second_add(&second, 200u, 90000u);
    ut_check(second.bytes == 200u && second.most_bytes == 3000u,
             "a long quiet stretch opens one new second and reads as no peak");
}

static void check_the_packets(void)
{
    mp_session_meter_t meter;
    uint32_t           size;

    ut_section("the payload packets: mean, 95th, most, and what they were made of");
    memset(&meter, 0, sizeof meter);
    ut_check(mp_session_meter_percentile(&meter, 95u) == 0u, "no packet has no percentile");
    for (size = 0; size < 100u; ++size) {
        mp_session_meter_payload_packet(&meter, size < 95u ? 400u : 1190u, 30u);
    }
    ut_checkf(mp_session_meter_percentile(&meter, 95u) == 416u,
              "95 of 100 packets were 400 bytes: the 95th is their step, 416 (%u)",
              (unsigned)mp_session_meter_percentile(&meter, 95u));
    ut_checkf(mp_session_meter_percentile(&meter, 96u) == 1200u && meter.bytes_most == 1190u,
              "the 96th is the large ones' step (%u), the most 1190",
              (unsigned)mp_session_meter_percentile(&meter, 96u));
    ut_check(meter.messages_sum / meter.packets == 30u, "and the messages beside them 30");
    mp_session_meter_parts(&meter, 222u, 700u);
    mp_session_meter_parts(&meter, 150u, 900u);
    ut_check(meter.bodies_most == 222u && meter.enemy_sum / meter.parts == 800u &&
             meter.enemy_most == 900u,
             "two payloads: bodies at most 222, the enemy block 800 on average and 900 at most");
}

int main(void)
{
    check_the_second();
    check_the_packets();
    return ut_summary("mp_session_meter");
}
