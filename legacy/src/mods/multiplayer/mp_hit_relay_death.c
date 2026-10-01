/* mp_hit_relay_death.c: the death message, taken out of the hit relay. The model is in the header.
 *
 * It holds the listener, the five counters and a copy of the relay's send function, and nothing
 * else. The slot this machine holds stays with the relay, which hands it in with every death off
 * the wire, so there is one place that slot is set.
 */
#include "mp_hit_relay_death.h"

#include "mp_death.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct hit_relay_death_state {
    bool (*send)(const uint8_t *bytes, size_t count);
    mp_hit_relay_death_fn_t death_listener;
    uint32_t deaths_noted;      /* deaths this machine reported */
    uint32_t deaths_unsent;
    uint32_t deaths_taken;      /* deaths that arrived off the wire */
    uint32_t deaths_refused;    /* and arrived malformed */
    uint32_t deaths_echoed;     /* addressed to this machine's own slot */
} hit_relay_death_state_t;

static hit_relay_death_state_t death;

void mp_hit_relay_set_death_listener(mp_hit_relay_death_fn_t listener)
{
    death.death_listener = listener;
}

void mp_hit_relay_death_set_send(bool (*send)(const uint8_t *bytes, size_t count))
{
    death.send = send;
}

void mp_hit_relay_note_death(uint8_t victim_slot, uint8_t killer_slot, uint8_t reason)
{
    mp_death_note_t note;
    uint8_t         bytes[MP_EVENT_DEATH_BYTES];

    note.victim_slot = victim_slot;
    note.killer_slot = killer_slot;
    note.reason      = reason;
    if (!mp_death_encode(&note, bytes, sizeof bytes)) {
        ++death.deaths_refused;
        return;
    }
    if (death.send != NULL && death.send(bytes, sizeof bytes)) {
        ++death.deaths_noted;
    } else {
        ++death.deaths_unsent;
    }

    /* The local listener is fed whether or not the message went out. A machine that keeps the
     * score has to count its own player's death, and nothing brings that one back off the wire. */
    if (death.death_listener != NULL) {
        death.death_listener(&note);
    }
}

/* A death off the wire. One that claims this machine's own slot is dropped rather than counted
 * twice: the local report above has already delivered it, and no other machine is the authority
 * on how this one died. */
void mp_hit_relay_take_death(const uint8_t *note, size_t bytes, uint32_t own_slot)
{
    mp_death_note_t decoded;

    if (!mp_death_decode(note, bytes, &decoded)) {
        ++death.deaths_refused;
        return;
    }
    if (decoded.victim_slot == (uint8_t)own_slot) {
        ++death.deaths_echoed;
        return;
    }
    ++death.deaths_taken;
    if (death.death_listener != NULL) {
        death.death_listener(&decoded);
    }
}

void mp_hit_relay_death_report(void)
{
    log_info("  deaths: %u reported from here, %u with nowhere to send them, %u taken off the "
             "wire, %u echoes of this machine's own slot, %u refused as malformed | a listener "
             "%s attached",
             (unsigned)death.deaths_noted, (unsigned)death.deaths_unsent,
             (unsigned)death.deaths_taken, (unsigned)death.deaths_echoed,
             (unsigned)death.deaths_refused, death.death_listener != NULL ? "is" : "is not");
}
