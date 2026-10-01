/* mp_level_state.c: the level's state as the module runs it, with no engine behind it.
 *
 * No engine means no world, no pool and no director, which is the case the fail-open rule is
 * about: a side that cannot match the host's level refuses none of its own switches, none of its
 * own fog and none of its own text. The director's three hands are taken the way the arming hands
 * them out, through a stand-in for the door's registration, and asked in each of the three roles a
 * session gives a machine. What the module keeps between substeps is read back through the answers
 * it offers the report, and every way out of a level or a session that a test process can take is
 * taken, with the same fields read after each.
 */
#include "unittest.h"

#include "mp_armed.h"
#include "mp_bridge.h"
#include "mp_director_rule.h"
#include "mp_enemy_sync.h"
#include "mp_level_state.h"
#include "mp_level_state_rule.h"
#include "mp_world_state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FLOAT_16  0x41800000   /* 16.0f as the director's argument carries it */
#define FLOAT_56  0x42600000   /* 56.0f */
#define FLOAT_1   0x3F800000   /* 1.0f */
#define FLOAT_NEG 0xBF800000   /* -1.0f, which the engine refuses */

static uint8_t                 s_note[MP_LEVEL_STATE_MAX_BYTES];
static mp_world_door_hand_fn_t s_hand[MP_DIRECTOR_CLASSES];
static uint32_t                s_registered;
static bool                    s_client;
static bool                    s_runs;

static bool fake_register(mp_director_class_t cls, mp_world_door_hand_fn_t hand, const char *name)
{
    (void)name;
    if ((size_t)cls >= (size_t)MP_DIRECTOR_CLASSES) {
        return false;
    }
    s_hand[cls] = hand;
    ++s_registered;
    return true;
}

static bool fake_role(bool *runs, uint8_t *generation)
{
    if (runs != NULL) {
        *runs = s_runs;
    }
    if (generation != NULL) {
        *generation = 1u;
    }
    return s_client;
}

static size_t no_bank(void)
{
    return 0u;
}

static void be(bool runs, bool client)
{
    s_runs   = runs;
    s_client = client;
}

/* The director's hand for one command, as the door would ask it. True withholds. */
static bool ask(int32_t command, int32_t a1, int32_t a2)
{
    mp_director_class_t cls = mp_director_class_of(command);

    return s_hand[cls] != NULL && s_hand[cls](NULL, command, a1, a2);
}

static size_t a_note(void)
{
    mp_level_state_note_t note;

    mp_level_state_note_init(&note, 100u, 97u, 2u);
    note.parts       = (uint8_t)(MP_LEVEL_STATE_PART_FOG | MP_LEVEL_STATE_PART_JOURNAL);
    note.fog.flags   = (uint8_t)(MP_LEVEL_FOG_HAS_START | MP_LEVEL_FOG_RAMP |
                                 MP_LEVEL_FOG_COLOUR);
    note.fog.cur     = (uint32_t)FLOAT_16;
    note.fog.target  = (uint32_t)FLOAT_1;
    note.fog.span    = 0x41A00000u;
    note.fog.left    = 20u;
    note.journal_newest          = 1u;
    note.journal_count           = 1u;
    note.journal[0].sequence     = 1u;
    note.journal[0].kind         = (uint8_t)MP_LEVEL_JOURNAL_FOG;
    note.journal[0].a            = 11u;
    note.journal[0].b            = 20u;
    note.journal[0].c            = (uint32_t)FLOAT_1;
    return mp_level_state_encode(&note, s_note, sizeof s_note);
}

static void check_the_arming(void)
{
    ut_section("the arming hands the director's fog, escort and text to the level's state");
    s_registered = 0u;
    memset(s_hand, 0, sizeof s_hand);
    mp_level_state_arm(&fake_register, &fake_role, &no_bank);
    ut_checkf(s_registered == 3u && s_hand[MP_DIRECTOR_FOG] != NULL &&
                  s_hand[MP_DIRECTOR_ESCORT] != NULL && s_hand[MP_DIRECTOR_CRAWL] != NULL,
              "three classes, one hand each (%u)", (unsigned)s_registered);
    ut_check(s_hand[MP_DIRECTOR_LEVEL_DOOR] == NULL && s_hand[MP_DIRECTOR_SHIELD] == NULL,
             "and no other: the door keeps its own and the shield is somebody else's");
}

static void check_the_take(void)
{
    size_t bytes = a_note();

    ut_section("a note is kept, never applied from where it arrives");
    ut_checkf(bytes > MP_LEVEL_STATE_HEADER_BYTES, "a note to take (%u bytes)", (unsigned)bytes);
    s_note[0] = (uint8_t)MP_WORLD_STATE_TAG;
    ut_check(!mp_level_state_take(true, s_note, bytes), "a map note is somebody else's");
    s_note[0] = (uint8_t)MP_LEVEL_STATE_TAG;
    ut_check(mp_level_state_take(true, s_note, bytes - 1u) && !mp_level_state_holds_a_note(),
             "a torn one is taken as ours and kept by nobody");
    ut_check(mp_level_state_take(false, s_note, bytes) && !mp_level_state_holds_a_note(),
             "one that reaches a host is the host's own level described back, and is dropped");
    ut_check(mp_level_state_take(true, s_note, bytes) && mp_level_state_holds_a_note(),
             "a client keeps it");
    ut_check(mp_level_state_take(true, s_note, bytes) && mp_level_state_holds_a_note(),
             "and a second before the substep replaces it, which the journal's window makes safe");
    ut_check(mp_level_state_flush() == 0u && mp_level_state_holds_a_note(),
             "and with no level open it waits for a substep that has one");
    mp_level_state_reset();
}

static void check_fail_open(void)
{
    ut_section("a client that cannot match the host refuses nothing of its own");
    be(true, true);
    ut_check(!mp_level_state_can_match(), "no engine here, so nothing is bound");
    ut_check(!mp_level_state_switch(MP_LEVEL_KIND_EMITTER, 0u, 1, true),
             "a client of a started session lets its own emitter switch through");
    ut_check(!mp_level_state_switch(MP_LEVEL_KIND_LIGHT, 0u, 0, true), "and its light switch");
    ut_check(!ask(11, 30, FLOAT_16), "and its own fog ramp");
    ut_check(!ask(15, 0, 0), "and its escort's bar, which it could not set to the host's");
    ut_check(!ask(18, 42, 0), "and its own line of text, which it could not replay");
    ut_check(mp_level_state_journal_newest() == 0u,
             "and keeps nothing of any of it: a client's fog is never the host's journal");
    mp_level_state_reset();
}

static void check_alone(void)
{
    ut_section("without a session nothing is refused and nothing is noted");
    be(false, false);
    ut_check(!ask(6, 0, FLOAT_16) && !ask(11, 20, FLOAT_1) && !ask(18, 42, 0) && !ask(15, 0, 0),
             "fog, text and escort all go to the engine");
    ut_check(mp_level_state_journal_newest() == 0u, "and none of it reaches a journal");
    mp_level_state_reset();
}

static void check_the_host(void)
{
    uint16_t before;
    int      i;

    ut_section("the host plays its fog and journals what changed");
    be(true, false);
    ut_check(!ask(6, 0, FLOAT_16) && !ask(7, 0, FLOAT_56),
             "the host's own fog goes to its engine");
    ut_checkf(mp_level_state_journal_newest() == 2u, "the two edges are two entries (%u)",
              (unsigned)mp_level_state_journal_newest());
    before = mp_level_state_journal_newest();
    for (i = 0; i < 199; ++i) {
        (void)ask(6, 0, FLOAT_16);
        (void)ask(7, 0, FLOAT_56);
    }
    ut_checkf(mp_level_state_journal_newest() == before,
              "the same band said again two hundred times changes nothing and is not journaled "
              "(%u)", (unsigned)mp_level_state_journal_newest());
    (void)ask(6, 0, (int32_t)FLOAT_NEG);
    (void)ask(11, 0, FLOAT_1);
    ut_check(mp_level_state_journal_newest() == before,
             "a start below zero and a ramp of no length are refused by the engine, and so are "
             "not journaled");
    ut_check(!ask(11, 20, FLOAT_1) && mp_level_state_journal_newest() == before + 1u,
             "a green ramp is");
    ut_check(!ask(18, 42, 0) && mp_level_state_journal_newest() == before + 2u,
             "and so is a line of text the host shows");
    ut_check(!ask(15, 0, 0) && mp_level_state_journal_newest() == before + 2u,
             "the escort goes to the host's engine and is read from there, not journaled here");
    ut_check(!ask(12, 1, FLOAT_16) && mp_level_state_journal_newest() == before + 3u,
             "and with no viewers' module bound, a room's fog is the level's: let through and "
             "journaled, as every fog was before");
    mp_level_state_reset();
    ut_check(mp_level_state_journal_newest() == 0u, "a new level begins with no journal");
}

/* Fills what a level leaves here, on both sides. */
static void leave_something(void)
{
    size_t bytes = a_note();

    be(true, false);
    (void)mp_level_state_take(true, s_note, bytes);
    (void)ask(6, 0, FLOAT_16);
    (void)ask(7, 0, FLOAT_56);
    (void)ask(18, 7, 0);
}

static bool nothing_left(void)
{
    return !mp_level_state_holds_a_note() && mp_level_state_journal_newest() == 0u;
}

/* The one exit. Five ways lead out of a level or a session and every one of them takes the enemy
 * table's reset: the module messages for a level end, a module start and a savegame restore, the
 * one for a level restart, and the arming, all three in the module's own file which no test
 * compiles; and the bridge's leave and its taking down of the transport. The reset is driven here
 * for the first three, the leave for real, and the transport's own taking down refuses on a bridge
 * that never stood, which is all a test process can offer it. */
static void check_every_way_out(void)
{
    ut_section("every way out of a level or a session leaves nothing of the level's state behind");

    leave_something();
    ut_check(mp_level_state_holds_a_note() && mp_level_state_journal_newest() == 3u,
             "a level leaves a note and a journal of three");
    mp_enemy_sync_reset();
    ut_check(nothing_left(), "the enemy table's reset, which a level end, a module start, a "
                             "savegame, a restart and the arming all take, forgets both");

    leave_something();
    mp_armed_set_transport(true, false);
    mp_bridge_leave();
    mp_armed_set_transport(false, false);
    ut_check(nothing_left(), "and so does leaving the session");

    leave_something();
    ut_check(!mp_bridge_uninstall_udp(), "a transport that never stood is not taken down");
    mp_enemy_sync_reset();
    ut_check(nothing_left(), "and its taking down ends in the same reset");
}

int main(void)
{
    check_the_arming();
    check_the_take();
    check_fail_open();
    check_alone();
    check_the_host();
    check_every_way_out();

    return ut_summary("mp_level_state");
}
