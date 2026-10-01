/* mp_netsync.c: the whole network layer, end to end, over a lossy link.
 *
 * This is the proof that the pieces built separately actually carry a world between two endpoints.
 * A host builds a snapshot every tick, delta encodes it against the baseline the client last
 * acknowledged, and sends it as the unreliable payload; the client decodes against the baseline it
 * holds, stores it, and acknowledges the newest tick it has as a reliable message. The link drops a
 * fifth of everything. The property that has to hold is the Quake-3 one: a lost snapshot is healed
 * by the next, so after enough ticks the client's newest state matches the host's, without a single
 * snapshot being retransmitted.
 *
 * Everything is static: a session is over two megabytes and the loopback ring another three
 * hundred kilobytes, far past a stack. The real feature keeps these in static or heap memory too.
 */
#include "unittest.h"

#include "mp_loopback.h"
#include "mp_session.h"
#include "mp_snapshot.h"
#include "mp_snapshot_history.h"
#include "mp_transport.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_loopback_t         s_net;
static mp_session_t          s_host;
static mp_session_t          s_client;
static mp_snapshot_history_t s_host_history;
static mp_snapshot_history_t s_client_history;

/* The transports are static, not local to the setup function: a session holds a pointer to its
 * transport and uses it for the whole run, so a transport on the setup function's stack would be a
 * dangling pointer the moment that function returned. */
static mp_transport_t        s_host_transport;
static mp_transport_t        s_client_transport;

/* Where the host's body 0 is at a given tick: it walks along x by one unit per tick, so the client
 * reconstructing it can be checked against a known answer. Body 1 stands still. */
static void build_world(mp_snapshot_t *snapshot, uint32_t tick)
{
    mp_wire_body_t moving;
    mp_wire_body_t still;

    memset(&moving, 0, sizeof moving);
    memset(&still, 0, sizeof still);
    moving.position[0] = (float)tick;
    moving.alive = true;
    moving.hero = 1u;
    still.position[0] = 500.0f;
    still.alive = true;
    still.hero = 2u;

    mp_snapshot_clear(snapshot);
    snapshot->tick = tick;
    mp_snapshot_set_body(snapshot, 0, &moving);
    mp_snapshot_set_body(snapshot, 1, &still);
}

static bool connect_host_and_client(uint32_t *now)
{
    mp_loopback_conditions_t cond;
    int                      tick;

    memset(&cond, 0, sizeof cond);
    cond.loss_percent  = 20u;
    cond.delay_ms      = 16u;
    mp_loopback_init(&s_net, &cond, 0x5E1F1234u);
    s_host_transport   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    s_client_transport = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);

    mp_session_init(&s_host, MP_SESSION_HOST, &s_host_transport, 0xA1u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_client_transport, 0xB2u);
    mp_snapshot_history_init(&s_host_history);
    mp_snapshot_history_init(&s_client_history);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);

    for (tick = 0; tick < 4000; ++tick) {
        *now += 16u;
        mp_loopback_pump(&s_net, *now);
        mp_session_update(&s_host, *now);
        mp_session_update(&s_client, *now);
        if (mp_session_is_connected(&s_client) && mp_session_peer_count(&s_host) == 1u) {
            return true;
        }
    }
    return false;
}

static void check_world_sync(void)
{
    uint32_t now = 0;
    uint32_t server_tick = 1u;
    uint32_t client_acked = 0u;   /* the newest tick the host believes the client holds */
    int      step;
    uint32_t last_built = 0u;

    ut_section("a world crosses a lossy link and stays in sync");

    ut_check(connect_host_and_client(&now), "the host and client connect first");

    for (step = 0; step < 3000; ++step) {
        uint8_t  payload[MP_SESSION_PAYLOAD_BYTES];
        uint8_t  ack[8];
        size_t   bytes;

        now += 16u;
        mp_loopback_pump(&s_net, now);

        /* Host: build this tick's world, keep it, and send it delta encoded against the baseline
         * the client last acknowledged. */
        {
            mp_snapshot_t       current;
            const mp_snapshot_t *baseline;
            size_t               encoded = 0;

            build_world(&current, server_tick);
            mp_snapshot_history_store(&s_host_history, &current);
            last_built = server_tick;
            baseline = (client_acked != 0u)
                           ? mp_snapshot_history_get(&s_host_history, client_acked)
                           : NULL;
            if (mp_snapshot_encode(&current, baseline, payload, sizeof payload, &encoded)) {
                mp_session_set_payload(&s_host, 0, payload, encoded);
            }
            ++server_tick;
        }

        mp_session_update(&s_host, now);
        mp_session_update(&s_client, now);

        /* Client: decode against the baseline it holds, store it, and acknowledge its newest. */
        if (mp_session_read_payload(&s_client, 0, payload, sizeof payload, &bytes)) {
            uint32_t             baseline_tick = 0;
            const mp_snapshot_t *baseline = NULL;
            mp_snapshot_t        decoded;

            if (mp_snapshot_baseline_tick(payload, bytes, &baseline_tick)) {
                if (baseline_tick != 0u) {
                    baseline = mp_snapshot_history_get(&s_client_history, baseline_tick);
                }
                if (mp_snapshot_decode(payload, bytes, baseline, &decoded)) {
                    mp_snapshot_history_store(&s_client_history, &decoded);
                    {
                        const mp_snapshot_t *newest =
                            mp_snapshot_history_newest(&s_client_history);
                        uint32_t             newest_tick = newest->tick;

                        memcpy(ack, &newest_tick, sizeof newest_tick);
                        mp_session_send_reliable(&s_client, 0, ack, sizeof newest_tick);
                    }
                }
            }
        }

        /* Host: read the client's acknowledgement so its next delta is against a baseline the
         * client actually has. */
        if (mp_session_read_reliable(&s_host, 0, ack, sizeof ack, &bytes) &&
            bytes == sizeof(uint32_t)) {
            uint32_t acked = 0;

            memcpy(&acked, ack, sizeof acked);
            client_acked = acked;
        }
    }

    /* After all that loss, the client holds a recent world, and the body that was moving is where
     * the host put it at that tick, to the wire's tolerance. */
    {
        const mp_snapshot_t *newest = mp_snapshot_history_newest(&s_client_history);
        float                expected;
        float                got;
        float                error;

        ut_check(newest != NULL, "the client holds a world at the end");
        ut_check(newest != NULL && newest->tick + 30u >= last_built,
                 "and it is a recent one, not one stalled far behind");
        ut_check(newest != NULL && mp_snapshot_has_body(newest, 0) &&
                 mp_snapshot_has_body(newest, 1), "with both bodies present");

        if (newest != NULL && mp_snapshot_has_body(newest, 0)) {
            expected = (float)newest->tick;
            got = newest->body[0].position[0];
            error = got - expected;
            if (error < 0) {
                error = -error;
            }
            ut_check(error <= MP_WIRE_POSITION_ERROR,
                     "the moving body is where the host put it at that tick");
        }
        ut_check(s_net.delivered > 0u && s_net.dropped > 0u,
                 "and the link really dropped and delivered, so the sync survived real loss");
    }
}

int main(void)
{
    check_world_sync();

    return ut_summary("mp_netsync");
}
