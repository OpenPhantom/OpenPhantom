/* mp_announcer.c: once a second, to everybody. See the header. */
#include "mp_announcer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void mp_announcer_init(mp_announcer_t *a)
{
    if (a == NULL) {
        return;
    }
    memset(a, 0, sizeof *a);
}

void mp_announcer_configure(mp_announcer_t *a, const char *name, uint8_t slots, uint8_t flags,
                            bool enabled)
{
    if (a == NULL) {
        return;
    }
    mp_announce_name_clean(name, a->announce.name);
    a->announce.slots = slots;
    a->announce.flags = flags & (uint8_t)MP_ANNOUNCE_F_KNOWN;
    a->enabled        = enabled;
    a->configured     = true;
    if (!enabled) {
        mp_discovery_close(&a->socket);
    }
}

void mp_announcer_set_facts(mp_announcer_t *a, uint8_t wire, uint32_t fingerprint,
                            uint16_t game_port, uint8_t players)
{
    if (a == NULL) {
        return;
    }
    a->announce.wire        = wire;
    a->announce.fingerprint = fingerprint;
    a->announce.game_port   = game_port;
    a->announce.players     = players;
}

bool mp_announcer_tick(mp_announcer_t *a, uint32_t now_ms, uint16_t announce_port)
{
    uint8_t datagram[MP_ANNOUNCE_BYTES];
    size_t  bytes;

    if (a == NULL || !a->enabled || !a->configured || a->open_failed) {
        return false;
    }
    if (a->spoke && now_ms - a->last_ms < MP_ANNOUNCER_CYCLE_MS) {
        return false;
    }
    if (!a->socket.up) {
        if (!mp_discovery_open_sender(&a->socket, announce_port)) {
            a->open_failed = true;   /* said once through the report, not once a second */
            return false;
        }
    }
    /* The clock is stamped whether or not the send stood, so a refusal does not become a burst. */
    a->last_ms = now_ms;
    a->spoke   = true;
    bytes = mp_announce_encode(&a->announce, datagram, sizeof datagram);
    if (bytes == 0u || !mp_discovery_broadcast(&a->socket, datagram, bytes)) {
        ++a->failed;
        return false;
    }
    ++a->sent;
    return true;
}

void mp_announcer_close(mp_announcer_t *a)
{
    if (a == NULL) {
        return;
    }
    mp_discovery_close(&a->socket);
}
