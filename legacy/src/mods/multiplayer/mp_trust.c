/* mp_trust.c: the host's questions about a player's word. The model is in the header. */
#include "mp_trust.h"

#include "mp_chat_wire.h"
#include "mp_death.h"
#include "mp_events.h"
#include "mp_lobby.h"
#include "mp_npc_copy_wire.h"
#include "mp_quest.h"
#include "mp_savefile.h"
#include "mp_wire.h"
#include "mp_world.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Module state because a process plays one game at a time, and the relays that ask are many. */
static bool checking;

void mp_trust_set_checking(bool on)
{
    checking = on;
}

bool mp_trust_checking(void)
{
    return checking;
}

/* The deathmatch is asked first, so a refusal it always made keeps its reason and its count, and
 * the wave's reason covers exactly the reports that used to be performed. */
mp_trust_hit_verdict_t mp_trust_host_performs_hit(uint32_t key, uint8_t code)
{
    if (checking && !mp_wire_key_is_copy(key)) {
        return MP_TRUST_HIT_DEATHMATCH;
    }
    if (code == MP_TRUST_WAVE_CODE) {
        return MP_TRUST_HIT_HOST_WAVE;
    }
    return MP_TRUST_HIT_PERFORM;
}

bool mp_trust_player_may_say(const uint8_t *note, size_t bytes)
{
    if (note == NULL || bytes == 0u) {
        return false;
    }
    switch ((unsigned)note[0]) {
    case MP_EVENT_SHOT:
    case MP_EVENT_PUSH:
    case MP_EVENT_SABRE:
    case MP_EVENT_WEAPON:
    case MP_EVENT_PLAYER_SOUND:      /* a sound its own body made */
    case MP_EVENT_MOVER:             /* a plate this player stood on */
    case MP_WORLD_DIGEST_TAG:        /* what this side's map looks like */
    case MP_EVENT_SKIN:
    case MP_EVENT_PICKUP:            /* a claim; the grant is the host's */
    case MP_EVENT_HIT:               /* a report; the host performs it or not */
    case MP_EVENT_DEATH:
    case MP_LOBBY_TAG:               /* team, ready and hero */
    case MP_LOBBY_CONTENT_TAG:
    case MP_QUEST_CLAIM_TAG:         /* a claim; the story is the host's */
    case MP_SAVEFILE_REQUEST_TAG:
    case MP_SAVEFILE_ACK_TAG:
    case MP_NPC_COPY_WISH_TAG:       /* a wish for a copy; the grant is the host's */
    case MP_CRATE_PUSH_TAG:          /* a wish to push a block; the host pushes it or not */
    case MP_CHAT_SAY_TAG:            /* a line of chat; the host stamps who said it */
        return true;
    default:
        return false;
    }
}

bool mp_trust_speaks_for_itself(const uint8_t *note, size_t bytes, uint8_t sender_slot)
{
    mp_death_note_t death;
    mp_event_t      event;

    if (mp_death_decode(note, bytes, &death)) {
        return death.victim_slot == sender_slot;
    }
    if (mp_event_decode(note, bytes, &event) && event.kind == MP_EVENT_SKIN) {
        return event.skin_slot == sender_slot;
    }
    return true;
}

bool mp_trust_takes_from(const uint8_t *note, size_t bytes, uint8_t sender_slot)
{
    return mp_trust_player_may_say(note, bytes) &&
           mp_trust_speaks_for_itself(note, bytes, sender_slot);
}

bool mp_trust_passes_between_players(const uint8_t *note, size_t bytes)
{
    if (note == NULL || bytes == 0u) {
        return false;
    }
    switch ((unsigned)note[0]) {
    case MP_EVENT_SHOT:
    case MP_EVENT_PUSH:
    case MP_EVENT_SABRE:
    case MP_EVENT_WEAPON:
    case MP_EVENT_PLAYER_SOUND:
    case MP_EVENT_MOVER:
    case MP_EVENT_SKIN:
    case MP_EVENT_DEATH:
        return true;
    /* Not the map digest. It is a measurement of one machine's map, and a client that corrects
     * its own phase against it has to know whose map it was: against a dedicated server, which
     * has no map of its own, two clients would otherwise measure each other and chase a drift
     * neither of them owns. A digest therefore travels between a player and the side that holds
     * the map, and no further. */
    default:
        return false;
    }
}
