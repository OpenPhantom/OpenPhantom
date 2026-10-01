/* What a client in its lobby takes of the reliable notes, and what it drops.
 *
 * A client that joins a session whose host already plays sits in its lobby until its player says
 * ready, and the host goes on sending it everything its level says. The lobby used to hand all of
 * it to the modules: a mover event waited with no level and fired into the next one. Every tag of
 * the wire is a row below with its class, and the census walks the whole band so
 * that a tag added without a row is a failure here rather than a note a lobby acts on.
 */
#include "unittest.h"

#include "mp_chat_wire.h"
#include "mp_death.h"
#include "mp_dialog.h"
#include "mp_events.h"
#include "mp_host_settings_rule.h"
#include "mp_level_state_rule.h"
#include "mp_lobby.h"
#include "mp_lobby_note_rule.h"
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
#include <string.h>

typedef struct note_case {
    uint8_t               tag;
    mp_lobby_note_class_t note_class;
    const char           *name;
} note_case_t;

static const note_case_t NOTES[] = {
    { (uint8_t)MP_EVENT_SHOT,           MP_LOBBY_NOTE_LEVEL_OTHER,     "a shot" },
    { (uint8_t)MP_EVENT_PUSH,           MP_LOBBY_NOTE_LEVEL_OTHER,     "a push" },
    { (uint8_t)MP_EVENT_SABRE,          MP_LOBBY_NOTE_LEVEL_OTHER,     "a sabre action" },
    { (uint8_t)MP_EVENT_WEAPON,         MP_LOBBY_NOTE_LEVEL_OTHER,     "a weapon change" },
    { (uint8_t)MP_EVENT_MOVER,          MP_LOBBY_NOTE_MOVER,           "a mover event" },
    { (uint8_t)MP_WORLD_DIGEST_TAG,     MP_LOBBY_NOTE_MOVER,           "the map's digest" },
    { (uint8_t)MP_EVENT_SPAWN,          MP_LOBBY_NOTE_LEVEL_OTHER,     "a spawn" },
    { (uint8_t)MP_EVENT_DESPAWN,        MP_LOBBY_NOTE_REMOVAL,         "a removal" },
    { (uint8_t)MP_EVENT_SKIN,           MP_LOBBY_NOTE_LEVEL_OTHER,     "an appearance" },
    { (uint8_t)MP_EVENT_PICKUP,         MP_LOBBY_NOTE_LEVEL_OTHER,     "a pickup" },
    { (uint8_t)MP_SCRATCH_TAG_BANK,     MP_LOBBY_NOTE_LEVEL_OTHER,     "the campaign bank" },
    { (uint8_t)MP_SCRATCH_TAG_AI,       MP_LOBBY_NOTE_LEVEL_OTHER,     "the blackboard" },
    { (uint8_t)MP_EVENT_USE,            MP_LOBBY_NOTE_LEVEL_OTHER,     "the retired use press" },
    { (uint8_t)MP_EVENT_HIT,            MP_LOBBY_NOTE_LEVEL_OTHER,     "a hit" },
    { (uint8_t)MP_ROSTER_TAG,           MP_LOBBY_NOTE_LOBBY,           "the roster" },
    { (uint8_t)MP_EVENT_PLAYER_HIT,     MP_LOBBY_NOTE_LEVEL_OTHER,     "a hit on a far player" },
    { (uint8_t)MP_LOBBY_TAG,            MP_LOBBY_NOTE_NEVER_TO_CLIENT, "a player's lobby line" },
    { (uint8_t)MP_LOBBY_SETUP_TAG,      MP_LOBBY_NOTE_LOBBY,           "the host's setup" },
    { (uint8_t)MP_LOBBY_CONTENT_TAG,    MP_LOBBY_NOTE_LOBBY,           "the fingerprint" },
    { (uint8_t)MP_EVENT_DEATH,          MP_LOBBY_NOTE_LEVEL_OTHER,     "a death" },
    { (uint8_t)MP_SCORE_TAG,            MP_LOBBY_NOTE_LEVEL_OTHER,     "the round's table" },
    { (uint8_t)MP_WORLD_STATE_TAG,      MP_LOBBY_NOTE_MOVER,           "the map's state" },
    { (uint8_t)MP_TAKEN_TAG,            MP_LOBBY_NOTE_LEVEL_OTHER,     "the pickups taken" },
    { (uint8_t)MP_SAVEFILE_CHUNK_TAG,   MP_LOBBY_NOTE_LOBBY,           "a savegame chunk" },
    { (uint8_t)MP_SAVEFILE_REQUEST_TAG, MP_LOBBY_NOTE_LOBBY,           "a savegame request" },
    { (uint8_t)MP_QUEST_STATE_TAG,      MP_LOBBY_NOTE_LOBBY,           "the shared story" },
    { (uint8_t)MP_QUEST_CLAIM_TAG,      MP_LOBBY_NOTE_NEVER_TO_CLIENT, "a claim on the story" },
    { (uint8_t)MP_DIALOG_TAG,           MP_LOBBY_NOTE_LEVEL_OTHER,     "a line spoken" },
    { (uint8_t)MP_DIALOG_CHOICE_TAG,    MP_LOBBY_NOTE_LEVEL_OTHER,     "the retired choice" },
    { (uint8_t)MP_DIALOG_PICK_TAG,      MP_LOBBY_NOTE_LEVEL_OTHER,     "the host's answer" },
    { (uint8_t)MP_SAVEFILE_ACK_TAG,     MP_LOBBY_NOTE_LOBBY,
      "a savegame acknowledgement" },
    { (uint8_t)MP_NPC_SHOT_TAG,         MP_LOBBY_NOTE_BOLT,            "an NPC's bolt" },
    { (uint8_t)MP_NPC_COPY_WISH_TAG,    MP_LOBBY_NOTE_NEVER_TO_CLIENT, "a wish for a copy" },
    { (uint8_t)MP_NPC_COPY_ENTRY_TAG,   MP_LOBBY_NOTE_LEVEL_OTHER,     "a copy's entry" },
    { (uint8_t)MP_LEVEL_STATE_TAG,      MP_LOBBY_NOTE_LEVEL_STATE,     "the level's state" },
    { (uint8_t)MP_SCENE_NOTE_TAG,       MP_LOBBY_NOTE_SCENE,           "the scene" },
    { (uint8_t)MP_CRATE_NOTE_TAG,       MP_LOBBY_NOTE_LEVEL_OTHER,     "the push blocks' note" },
    { (uint8_t)MP_CRATE_PUSH_TAG,       MP_LOBBY_NOTE_NEVER_TO_CLIENT, "a wish to push" },
    { (uint8_t)MP_CRATE_FALL_TAG,       MP_LOBBY_NOTE_LEVEL_OTHER,     "a push block falling" },
    { (uint8_t)MP_EVENT_PLAYER_SOUND,   MP_LOBBY_NOTE_LEVEL_OTHER,     "a player's sound" },
    { (uint8_t)MP_CHAT_SAY_TAG,         MP_LOBBY_NOTE_LEVEL_OTHER,     "a player's chat line" },
    { (uint8_t)MP_CHAT_LINE_TAG,        MP_LOBBY_NOTE_LEVEL_OTHER,     "the host's chat line" },
    { (uint8_t)MP_HOST_SETTINGS_TAG,    MP_LOBBY_NOTE_LOBBY,           "the host's settings" },
};

#define NOTE_CASES (sizeof NOTES / sizeof NOTES[0])

static void check_every_tag_has_its_class(void)
{
    uint8_t note[8];
    size_t  i;

    ut_section("every tag of the wire has the class the rule gives it");
    for (i = 0; i < NOTE_CASES; ++i) {
        memset(note, 0, sizeof note);
        note[0] = NOTES[i].tag;
        ut_checkf(mp_lobby_note_class(note, sizeof note) == NOTES[i].note_class,
                  "%s (0x%02X) is %s in a lobby", NOTES[i].name, (unsigned)NOTES[i].tag,
                  mp_lobby_note_taken_in_lobby(NOTES[i].note_class) ? "taken" : "dropped");
    }
}

/* The band walked tag by tag: every one is named once in the table above and answered with a
 * class by the rule. A tag the wire gains without a row fails both halves. */
static void check_the_whole_band(void)
{
    uint8_t  note[8];
    unsigned tag;
    unsigned gaps     = 0u;
    unsigned unknown  = 0u;
    size_t   i;

    ut_section("the census: every tag from the first to the last has a row");
    for (tag = MP_LOBBY_NOTE_BAND_FIRST; tag <= MP_LOBBY_NOTE_BAND_LAST; ++tag) {
        size_t seen = 0;

        for (i = 0; i < NOTE_CASES; ++i) {
            seen += NOTES[i].tag == tag ? 1u : 0u;
        }
        gaps += seen == 1u ? 0u : 1u;
        memset(note, 0, sizeof note);
        note[0] = (uint8_t)tag;
        unknown += mp_lobby_note_class(note, sizeof note) == MP_LOBBY_NOTE_UNKNOWN ? 1u : 0u;
    }
    ut_checkf(gaps == 0u, "the table names every tag from 0x%02X to 0x%02X once: %u gap(s)",
              (unsigned)MP_LOBBY_NOTE_BAND_FIRST, (unsigned)MP_LOBBY_NOTE_BAND_LAST, gaps);
    ut_checkf(unknown == 0u, "and the rule has a class for each of them: %u without one",
              unknown);
    /* Equal, not at least: a band that ends past the newest tag would walk tags nobody defines,
     * and one that ends before it leaves the newest tag UNKNOWN. The tag census holds the same
     * number against every header, so a new tag fails there even before this line is updated. */
    ut_checkf(MP_LOBBY_NOTE_BAND_LAST == MP_HOST_SETTINGS_TAG,
              "the band ends at the newest tag on the wire, the host's settings (0x%02X)",
              (unsigned)MP_LOBBY_NOTE_BAND_LAST);
}

static void check_what_is_nobody_s(void)
{
    uint8_t note[4];

    ut_section("what no row names is dropped");
    memset(note, 0, sizeof note);
    ut_check(mp_lobby_note_class(NULL, 4u) == MP_LOBBY_NOTE_UNKNOWN, "no note");
    ut_check(mp_lobby_note_class(note, 0u) == MP_LOBBY_NOTE_UNKNOWN, "an empty note");
    note[0] = 0x80u;
    ut_check(mp_lobby_note_class(note, sizeof note) == MP_LOBBY_NOTE_UNKNOWN,
             "a byte below the band, which is an event kind and no tag");
    note[0] = (uint8_t)(MP_LOBBY_NOTE_BAND_LAST + 1u);
    ut_check(mp_lobby_note_class(note, sizeof note) == MP_LOBBY_NOTE_UNKNOWN,
             "a tag past the last one this build knows");
    ut_check(!mp_lobby_note_taken_in_lobby(MP_LOBBY_NOTE_UNKNOWN) &&
                 !mp_lobby_note_taken_in_lobby(MP_LOBBY_NOTE_NEVER_TO_CLIENT) &&
                 !mp_lobby_note_taken_in_lobby(MP_LOBBY_NOTE_MOVER),
             "and none of those, nor a player's word or a door, is taken");
    ut_check(mp_lobby_note_taken_in_lobby(MP_LOBBY_NOTE_LOBBY), "only the lobby's own class is");
}

int main(void)
{
    check_every_tag_has_its_class();
    check_the_whole_band();
    check_what_is_nobody_s();
    return ut_summary("mp_lobby_note_rule");
}
