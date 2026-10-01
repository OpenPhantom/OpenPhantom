/* mp_seat_order.c: which clients of the session an arriving client is seated after. See the
 * header. */
#include "mp_seat_order.h"

#include "mp_bridge_drain.h"
#include "mp_bridge_far.h"
#include "mp_bridge_roster.h"
#include "mp_roster.h"
#include "mp_seat.h"
#include "mp_seat_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void mp_seat_order_wish_beside_host(mp_seat_wish_t *wish, const char *who, uint8_t my_slot,
                                    bool late)
{
    mp_roster_t roster;
    uint8_t     slots[MP_ROSTER_MAX_ENTRIES];
    uint8_t     lower[MP_SEAT_FAR_BODIES];
    uint8_t     bodies[MP_SEAT_FAR_BODIES];
    size_t      listed = 0u;
    size_t      count;
    size_t      kept = 0u;
    size_t      i;

    if (wish == NULL) {
        return;
    }
    mp_seat_wish_beside_body(wish, who, my_slot);
    if (!mp_bridge_roster_current(&roster)) {
        mp_seat_wish_seat_after(wish, false, NULL, NULL, 0u);
        return;
    }
    for (i = 0; i < roster.count && i < MP_ROSTER_MAX_ENTRIES; ++i) {
        slots[listed++] = roster.entry[i].slot;
    }
    count = mp_seat_rule_lower_slots(slots, listed, (uint8_t)MP_BRIDGE_HOST_SLOT, my_slot, lower,
                                     MP_SEAT_FAR_BODIES);
    for (i = 0; i < count; ++i) {
        size_t bank = mp_bridge_far_bank_of_slot(lower[i]);

        /* A late player's lower slot that already stands in this world is a body the search
         * steps around, not a seat to hold for it. Only a late player asks: in a start that
         * everybody takes together, a lower slot's body can be in this world and still at the
         * level start, a moment before its own seat is handed to it. */
        if (late && bank != 0u && mp_bridge_far_occupied(bank)) {
            continue;
        }
        lower[kept] = lower[i];
        /* A lower slot this machine shows no body of yet has none to leave out of its view. */
        bodies[kept] = bank >= 1u && bank <= MP_SEAT_FAR_BODIES ? (uint8_t)(bank - 1u)
                                                                : (uint8_t)MP_SEAT_ORDER_NO_BODY;
        ++kept;
    }
    mp_seat_wish_seat_after(wish, true, lower, bodies, kept);
}
