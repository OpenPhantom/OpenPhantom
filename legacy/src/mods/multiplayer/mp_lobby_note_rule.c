/* mp_lobby_note_rule.c: which reliable notes a client in its lobby takes. See the header. */
#include "mp_lobby_note_rule.h"

#include "mp_chat_wire.h"
#include "mp_death.h"
#include "mp_dialog.h"
#include "mp_events.h"
#include "mp_host_settings_rule.h"
#include "mp_level_state_rule.h"
#include "mp_lobby.h"
#include "mp_npc_copy_wire.h"
#include "mp_npc_shot.h"
#include "mp_quest.h"
#include "mp_roster.h"
#include "mp_savefile.h"
#include "mp_score.h"
#include "mp_scratch_wire.h"
#include "mp_taken.h"
#include "mp_wire.h"
#include "mp_world.h"
#include "mp_world_state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One row per tag, and no default that names a class: a tag this switch does not list is
 * UNKNOWN, which the lobby drops and the test refuses. The band is asked first, so a row added
 * for a new tag without raising the band's last tag stays dead, and the test that expects its
 * class fails rather than a lobby dropping it without a word. */
mp_lobby_note_class_t mp_lobby_note_class(const uint8_t *note, size_t bytes)
{
    if (note == NULL || bytes == 0u || (unsigned)note[0] < MP_LOBBY_NOTE_BAND_FIRST ||
        (unsigned)note[0] > MP_LOBBY_NOTE_BAND_LAST) {
        return MP_LOBBY_NOTE_UNKNOWN;
    }
    switch ((unsigned)note[0]) {
    /* What a lobby is for. The story is state: it is kept, and written into nothing until a level
     * runs. The savegame's request and acknowledgement are a client's words and never reach a
     * client, but they belong to the transfer a lobby runs, so they share its class. */
    case MP_ROSTER_TAG:
    case MP_LOBBY_SETUP_TAG:
    case MP_LOBBY_CONTENT_TAG:
    case MP_SAVEFILE_CHUNK_TAG:
    case MP_SAVEFILE_REQUEST_TAG:
    case MP_SAVEFILE_ACK_TAG:
    case MP_QUEST_STATE_TAG:
    case MP_HOST_SETTINGS_TAG:
        return MP_LOBBY_NOTE_LOBBY;

    /* The map: a door, its digest and its state. */
    case MP_EVENT_MOVER:
    case MP_WORLD_DIGEST_TAG:
    case MP_WORLD_STATE_TAG:
        return MP_LOBBY_NOTE_MOVER;

    case MP_EVENT_DESPAWN:
        return MP_LOBBY_NOTE_REMOVAL;

    case MP_NPC_SHOT_TAG:
        return MP_LOBBY_NOTE_BOLT;

    case MP_LEVEL_STATE_TAG:
        return MP_LOBBY_NOTE_LEVEL_STATE;

    /* Everything else a running level says: a player's moments, the campaign, a hit, a death,
     * the round's table, the pickups taken, the conversation, the NPC copies, the push blocks, a
     * player's sound and the chat, which is a level's alone. The retired scene note has its row
     * here beside the retired choice: every tag of the band has one, sent or not. */
    case MP_SCENE_NOTE_TAG:
    case MP_EVENT_SHOT:
    case MP_EVENT_PUSH:
    case MP_EVENT_SABRE:
    case MP_EVENT_WEAPON:
    case MP_EVENT_SPAWN:
    case MP_EVENT_SKIN:
    case MP_EVENT_PICKUP:
    case MP_SCRATCH_TAG_BANK:
    case MP_SCRATCH_TAG_AI:
    case MP_EVENT_USE:
    case MP_EVENT_HIT:
    case MP_EVENT_PLAYER_HIT:
    case MP_EVENT_DEATH:
    case MP_SCORE_TAG:
    case MP_TAKEN_TAG:
    case MP_DIALOG_TAG:
    case MP_DIALOG_CHOICE_TAG:
    case MP_DIALOG_PICK_TAG:
    case MP_NPC_COPY_ENTRY_TAG:
    case MP_CRATE_NOTE_TAG:
    case MP_CRATE_FALL_TAG:
    case MP_EVENT_PLAYER_SOUND:
    case MP_CHAT_SAY_TAG:
    case MP_CHAT_LINE_TAG:
        return MP_LOBBY_NOTE_LEVEL_OTHER;

    /* A player's word to its host: a host answers these and passes none of them on. */
    case MP_LOBBY_TAG:
    case MP_QUEST_CLAIM_TAG:
    case MP_NPC_COPY_WISH_TAG:
    case MP_CRATE_PUSH_TAG:
        return MP_LOBBY_NOTE_NEVER_TO_CLIENT;

    default:
        return MP_LOBBY_NOTE_UNKNOWN;
    }
}

bool mp_lobby_note_taken_in_lobby(mp_lobby_note_class_t note_class)
{
    return note_class == MP_LOBBY_NOTE_LOBBY;
}
