/* mp_bridge_seats.c: the listen host's ledger of who sits at which world slot. See the header. */
#include "mp_bridge_seats.h"

#include "mp_bank.h"
#include "mp_settings.h"
#include "mp_text.h"
#include "mp_wallclock.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* How long the host's picture names a player it sent away. Long enough to be read in a fight,
 * short enough not to stand over the next one. */
#define SENT_AWAY_NOTICE_MS 6000u

/* The seats a session offers are the bodies one machine can show: its own and one per far bank. A
 * seat past that would put a player in nobody else's world while their shots still landed. */
_Static_assert(MP_SETTINGS_SLOTS_MAX == MP_BANK_FAR_MAX + 1u,
               "the seats a session offers are not the bodies one machine can show");

typedef struct bridge_seat {
    uint64_t connection_id;   /* who sat here at the last walk, 0 for nobody */
    bool     told;            /* that connection has been sent its slot */
} bridge_seat_t;

typedef struct bridge_seats_state {
    bridge_seat_t seat[MP_SESSION_MAX_PEERS];
    uint32_t      arrivals;
    uint32_t      departures;
    uint32_t      told;
    uint32_t      unsent;   /* the channel had no room; offered again on the next walk */
    uint32_t      sent_away;        /* departures the host decided, for falling behind */
    char          notice[96];       /* who went and why, in the menus' language */
    uint32_t      notice_until;
    bool          notice_on;
    uint32_t      notice_frames;    /* frames the picture took the notice to draw */
} bridge_seats_state_t;

static bridge_seats_state_t seats;

void mp_bridge_seats_reset(void)
{
    memset(&seats, 0, sizeof seats);
}

size_t mp_bridge_seats_update(mp_session_t *host)
{
    size_t changed = 0;
    size_t i;

    if (host == NULL) {
        return 0u;
    }
    for (i = 0; i < MP_SESSION_MAX_PEERS; ++i) {
        const mp_peer_t *peer = mp_session_peer(host, i);
        bridge_seat_t   *seat = &seats.seat[i];
        uint64_t         now  = (peer != NULL && peer->state == MP_PEER_CONNECTED)
                                    ? peer->connection_id
                                    : 0u;
        uint8_t          slot = mp_session_slot_of_peer(i);

        /* A different id at the same index is somebody new and no id is somebody gone; a
         * replacement in place is both, in that order. */
        if (now != seat->connection_id) {
            if (seat->connection_id != 0u && mp_session_peer_dropped_behind(host, i)) {
                /* The one departure the host decided, so the one its player is shown: nothing
                 * else on the host's screen would say that a player went, or why. */
                ++seats.departures;
                ++seats.sent_away;
                ++changed;
                log_info("world slot %u is free again: %s was sent away because that machine "
                         "fell too far behind", (unsigned)slot, mp_session_peer_name(host, i));
                text_format(seats.notice, sizeof seats.notice, mp_text(MP_TEXT_HUD_PLAYER_BEHIND),
                            mp_session_peer_name(host, i));
                seats.notice_until = mp_wallclock_ms() + SENT_AWAY_NOTICE_MS;
                seats.notice_on    = true;
            } else if (seat->connection_id != 0u) {
                ++seats.departures;
                ++changed;
                log_info("world slot %u is free again: the player who sat there has gone",
                         (unsigned)slot);
            }
            if (now != 0u) {
                ++seats.arrivals;
                ++changed;
            }
            seat->connection_id = now;
            seat->told          = false;
        }
        if (now == 0u || seat->told) {
            continue;
        }
        if (!mp_session_send_reliable(host, i, &slot, sizeof slot)) {
            ++seats.unsent;
            continue;
        }
        seat->told = true;
        ++seats.told;
        log_info("%s was told world slot %u", mp_session_peer_name(host, i), (unsigned)slot);
    }
    return changed;
}

const char *mp_bridge_seats_notice(void)
{
    if (!seats.notice_on) {
        return NULL;
    }
    if ((int32_t)(mp_wallclock_ms() - seats.notice_until) >= 0) {
        seats.notice_on = false;
        return NULL;
    }
    /* The picture asks only on a frame it can draw the band on, and draws what it is given, so a
     * frame the text is handed out on is a frame it was drawn on. */
    ++seats.notice_frames;
    return seats.notice;
}

void mp_bridge_seats_report(bool is_host, uint8_t my_slot)
{
    if (!is_host) {
        log_info("  the seat: this player holds world slot %u", (unsigned)my_slot);
        return;
    }
    log_info("  the seats: %u arrival(s) and %u departure(s), %u slot note(s) sent, %u offer(s) "
             "the channel had no room for",
             (unsigned)seats.arrivals, (unsigned)seats.departures, (unsigned)seats.told,
             (unsigned)seats.unsent);
    log_info("  the players this host sent away: %u for falling behind, each named on its screen "
             "for %u s; the notice was drawn on %u frame(s)", (unsigned)seats.sent_away,
             (unsigned)(SENT_AWAY_NOTICE_MS / 1000u), (unsigned)seats.notice_frames);
}
