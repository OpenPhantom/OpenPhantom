/* The watch over a corpse, through the real rule set: a wish that was lost on its way is made
 * again, and the level is ended for a corpse by the rule set or as the last resort only.
 *
 * The rule set is the real one. The re-entry under it is this file's: it takes a wish or refuses
 * it, holds it or loses it, as each section says, which is what the engine and its gates do to a
 * wish in a level. Whether this player is a corpse is a flag of this file as well, and the
 * level's outcome cell a field of it.
 *
 * What it pins is the defect it was written after. A death made one wish, and whatever lost that
 * wish left a corpse in a level that went on running; four seconds on, the watch ended the level.
 * A landing that outlived its deadline under a held world did exactly that in a field run.
 */
#include "unittest.h"

#include "mp_body.h"
#include "mp_body_gate.h"
#include "mp_cells.h"
#include "mp_damage.h"
#include "mp_damage_entry.h"
#include "mp_death.h"
#include "mp_lobby.h"
#include "mp_reentry.h"
#include "mp_respawn.h"
#include "mp_rules.h"
#include "mp_seat.h"
#include "mp_spawnpoints.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One drawn frame is one substep here, a thirty second of a second, so the frames of the watch's
 * patience are reached after its milliseconds. */
#define FRAME_MS 32u
#define PATIENCE MP_REENTRY_CORPSE_PATIENCE_FRAMES

#define RUNNING MP_REENTRY_OUTCOME_RUNNING
#define ENDED   MP_REENTRY_OUTCOME_DEATH

static const float MATE_AT[3] = { 20.0f, 0.0f, 0.0f };
static const float DIED_AT[3] = { 10.0f, 0.0f, 0.0f };

typedef struct rig {
    uint32_t outcome;      /* the level's outcome cell */
    bool     corpse;       /* the engine holds this player for dead */
    bool     survives;     /* what the rule set last told the damage module */
    uint32_t lives;        /* lives ended, as the death hull counts them */
    uint8_t  my_slot;

    bool     mate_stands;  /* the one far player this machine resolved */

    /* The re-entry this file plays. */
    bool     accepts;      /* it takes a wish it is asked for */
    bool     keeps;        /* and goes on holding it; false loses it before the next frame */
    bool     pending;
    bool     place_lost;   /* a place read off this player is one no level has */
    uint32_t beside;       /* wishes beside the living it was asked for */
    bool     knew_the_place;
    uint32_t life_notes;   /* times the seat search was told a life ended */
    uint32_t withdrawn;    /* wishes taken back from it before a seat was found */

    uint32_t substeps;
    uint32_t ms;
} rig_t;

static rig_t rig;

/* ---- what the rule set asks of its neighbours ------------------------------------------------ */

uintptr_t mp_cells_address(mp_cell_t cell)
{
    return cell == MP_CELL_GAME_MODE ? (uintptr_t)&rig.outcome : 0u;
}

bool mp_cells_hero_position(float out[3])
{
    memcpy(out, DIED_AT, sizeof DIED_AT);
    return true;
}

bool mp_body_death_is_reported(void)
{
    return true;
}

void mp_body_gate_set_life_test(mp_body_gate_life_fn_t lives)
{
    (void)lives;
}

bool mp_damage_installed(void)
{
    return true;
}

void mp_damage_set_survives_death(bool survives)
{
    rig.survives = survives;
}

bool mp_damage_survives_death(void)
{
    return rig.survives;
}

bool mp_damage_entry_lives_ended(uint32_t *lives)
{
    *lives = rig.lives;
    return true;
}

uint32_t mp_rules_respawn_substeps(const mp_rules_t *rules)
{
    (void)rules;
    return 0u;
}

void mp_seat_note_life_ended(void)
{
    ++rig.life_notes;
}

bool mp_spawnpoints_take(const float (*living)[3], size_t living_count, const float *died_at,
                         uint32_t now, float position[3], float *heading)
{
    (void)living;
    (void)living_count;
    (void)died_at;
    (void)now;
    (void)position;
    (void)heading;
    return false;
}

uint32_t mp_spawnpoints_now(void)
{
    return 0u;
}

size_t mp_spawnpoints_count(void)
{
    return 0u;
}

/* ---- the re-entry, as this file plays it ----------------------------------------------------- */

bool mp_respawn_installed(void)
{
    return true;
}

bool mp_respawn_player_is_a_corpse(void)
{
    return rig.corpse;
}

bool mp_respawn_player_lives(void)
{
    return !rig.corpse;
}

bool mp_respawn_place_is_lost(const float position[3])
{
    (void)position;
    return rig.place_lost;
}

bool mp_respawn_pending(void)
{
    return rig.pending;
}

void mp_respawn_cancel(void)
{
    rig.pending = false;
}

bool mp_respawn_withdraw(void)
{
    bool held = rig.pending;

    rig.pending = false;
    rig.withdrawn += held ? 1u : 0u;
    return held;
}

bool mp_respawn_beside(const float *died_at, uint8_t slot, uint32_t delay_frames)
{
    (void)slot;
    (void)delay_frames;
    ++rig.beside;
    rig.knew_the_place = died_at != NULL;
    if (!rig.accepts) {
        return false;
    }
    rig.pending = rig.keeps;
    return true;
}

bool mp_respawn_at(const float position[3], float heading, uint8_t slot, uint32_t delay_frames)
{
    (void)position;
    (void)heading;
    (void)slot;
    (void)delay_frames;
    return false;
}

/* ---- the session and the frames -------------------------------------------------------------- */

/* One drawn frame as the pump hands it in. A clock that stands is a held world: the frames go on
 * and the milliseconds do not. */
static void one_frame(bool the_clock_runs)
{
    ++rig.substeps;
    if (the_clock_runs) {
        rig.ms += FRAME_MS;
    }
    mp_reentry_note_peer(0u, MATE_AT, 0.0f, rig.mate_stands);
    mp_reentry_tick(rig.substeps, rig.ms);
}

static void frames(uint32_t count)
{
    uint32_t i;

    for (i = 0u; i < count; ++i) {
        one_frame(true);
    }
}

static void held_frames(uint32_t count)
{
    uint32_t i;

    for (i = 0u; i < count; ++i) {
        one_frame(false);
    }
}

/* A co-op session and a fresh level in it: this player and his mate stand, and the re-entry takes
 * a wish and holds it. The two frames let the watch see him standing, which is what ends
 * whatever the section before left behind. */
static void stand_in_a_fresh_level(bool is_client)
{
    mp_rules_t rules;

    memset(&rules, 0, sizeof rules);
    rig.my_slot = is_client ? 1u : 0u;
    mp_reentry_note_session((uint8_t)MP_LOBBY_MODE_COOP, is_client, &rules);
    mp_reentry_note_my_slot(rig.my_slot);

    rig.outcome     = RUNNING;
    rig.corpse      = false;
    rig.pending     = false;
    rig.accepts     = true;
    rig.keeps       = true;
    rig.place_lost  = false;
    rig.mate_stands = true;
    frames(2u);
}

static void hear_the_death(void)
{
    mp_death_note_t note;

    note.victim_slot = rig.my_slot;
    note.killer_slot = (uint8_t)MP_DEATH_NO_KILLER;
    note.reason      = (uint8_t)MP_DEATH_BY_HIT;
    mp_reentry_note_death(&note);
}

static void die(bool heard)
{
    rig.corpse = true;
    ++rig.lives;
    if (heard) {
        hear_the_death();
    }
}

/* ============================================================================================== */

static void check_a_lost_wish_is_made_again(void)
{
    uint32_t asked;

    ut_section("the host dies beside a mate: one wish, handed to the re-entry and held there");
    stand_in_a_fresh_level(false);
    asked = rig.beside;
    die(true);
    frames(PATIENCE + 10u);
    ut_checkf(rig.beside == asked + 1u && rig.pending,
              "past the patience it is still the one wish, and the watch lets it be tried (%u "
              "asked)", (unsigned)(rig.beside - asked));
    ut_check(rig.outcome == RUNNING, "the level runs");

    ut_section("the re-entry loses it, as a deadline at a gate or a silent no of the engine does");
    rig.pending = false;
    frames(1u);
    ut_checkf(rig.beside == asked + 2u && rig.pending,
              "the wish is made again on the next look and handed over again (%u asked)",
              (unsigned)(rig.beside - asked));
    ut_check(rig.outcome == RUNNING,
             "and the level is not ended for him, which it was four seconds after a wish was "
             "lost");
}

static void check_the_last_resort(void)
{
    uint32_t asked;
    uint32_t again;

    ut_section("a re-entry that loses every wish at once: one wish a patience, and the level runs");
    stand_in_a_fresh_level(false);
    rig.keeps = false;
    asked     = rig.beside;
    die(true);
    frames(PATIENCE * MP_REENTRY_WISHES_AGAIN_MAX);
    again = rig.beside - asked - 1u;
    ut_checkf(again == MP_REENTRY_WISHES_AGAIN_MAX,
              "the wish of the one corpse was made again %u time(s), once a patience",
              (unsigned)again);
    ut_check(rig.outcome == RUNNING, "and through all of them the level went on");

    ut_section("only when every one of them came to nothing is the level ended for him, once");
    frames(PATIENCE);
    ut_checkf(rig.outcome == ENDED && rig.beside == asked + 1u + MP_REENTRY_WISHES_AGAIN_MAX,
              "the last resort: the outcome is raised, and no further wish is made (the outcome "
              "reads %u)", (unsigned)rig.outcome);
    rig.outcome = RUNNING;
    frames(PATIENCE * 2u);
    ut_check(rig.outcome == RUNNING && rig.beside == asked + 1u + MP_REENTRY_WISHES_AGAIN_MAX,
             "and it is done once for one corpse, whatever the cell reads afterwards");
}

static void check_a_death_nobody_heard(void)
{
    uint32_t asked;
    uint32_t notes;

    ut_section("a death no note ever told of: the corpse is wished back all the same");
    stand_in_a_fresh_level(false);
    asked = rig.beside;
    notes = rig.life_notes;
    die(false);
    frames(PATIENCE - 1u);
    ut_check(rig.beside == asked,
             "inside the patience nothing is done: it is a death the engine is still playing "
             "out");
    frames(1u);
    ut_check(rig.beside == asked + 1u && rig.pending,
             "past it the wish is made out of the state he is in, with no death to make it");
    ut_check(rig.life_notes == notes + 1u,
             "and the seat search is told that a life ended, as a death tells it");

    ut_section("and the note of that death, arriving late, is the same death");
    hear_the_death();
    frames(3u);
    rig.pending = false;   /* the body stands */
    rig.corpse  = false;
    frames(3u);
    ut_check(rig.beside == asked + 1u && rig.life_notes == notes + 1u,
             "no second wish stands a living player up again");
}

static void check_nobody_standing(void)
{
    uint32_t asked;

    ut_section("the host lies dead with nobody standing and no wish: the rule set ends the level");
    stand_in_a_fresh_level(false);
    asked           = rig.beside;
    rig.mate_stands = false;
    die(false);
    frames(PATIENCE);
    ut_checkf(rig.outcome == ENDED && rig.beside == asked,
              "the wish made again goes to the rule set, whose answer with nobody standing is "
              "the wipe, at the first look and not after %u wishes",
              (unsigned)MP_REENTRY_WISHES_AGAIN_MAX);

    ut_section("a client in the same place waits for the host's screen, however long");
    stand_in_a_fresh_level(true);
    asked           = rig.beside;
    rig.mate_stands = false;
    die(false);
    frames(PATIENCE * (MP_REENTRY_WISHES_AGAIN_MAX + 2u));
    ut_check(mp_reentry_waiting_for_host() && rig.outcome == RUNNING && rig.beside == asked,
             "it ends no level of its own, and the wait is not counted towards the last resort");
    rig.mate_stands = true;
    frames(2u);
    ut_check(rig.beside == asked + 1u && rig.pending,
             "and when the host stands again the client is wished back beside him");
}

static void check_a_death_place_that_is_no_place(void)
{
    ut_section("where he died is handed on, unless the body lies at a place no level has");
    stand_in_a_fresh_level(false);
    die(true);
    frames(1u);
    ut_check(rig.pending && rig.knew_the_place,
             "a death inside the level: the mate nearest to it is tried first");

    stand_in_a_fresh_level(false);
    rig.place_lost = true;
    die(true);
    frames(1u);
    ut_check(rig.pending && !rig.knew_the_place,
             "a body that fell out of the world: the wish is asked with the place not known, "
             "which the re-entry takes, where it refuses a place that is no number");
}

/* A wish the watch made is for a corpse. The engine can finish a re-entry that was believed lost,
 * behind a menu that parked the player past a deadline, and the body then stands with a wish
 * still waiting that would send it through a second fade. */
static void check_a_body_that_stands_after_all(void)
{
    uint32_t asked;
    uint32_t taken;

    ut_section("the body stands by a re-entry that was believed lost: the watch's wish goes");
    stand_in_a_fresh_level(false);
    die(false);
    frames(PATIENCE);
    asked = rig.beside;
    taken = rig.withdrawn;
    ut_check(rig.pending, "the watch made the wish, and it waits for its seat");
    rig.corpse = false;   /* the engine finished the re-entry it had */
    frames(1u);
    ut_check(!rig.pending && rig.withdrawn == taken + 1u,
             "it is taken back from the seat search before it could move a living player");
    frames(PATIENCE * 2u);
    ut_check(rig.beside == asked, "and nothing asks again for a player who stands");

    ut_section("the same while a client's wish waits for its host");
    stand_in_a_fresh_level(true);
    rig.mate_stands = false;
    die(false);
    frames(PATIENCE);
    ut_check(mp_reentry_waiting_for_host(), "the watch made the wish, and the rule set keeps it");
    rig.corpse = false;
    frames(1u);
    ut_check(!mp_reentry_waiting_for_host(), "a client that stands waits for nobody");
}

static void check_a_held_world(void)
{
    uint32_t asked;

    ut_section("a world that is held: the patience is the clock it is handed, not the frames");
    stand_in_a_fresh_level(false);
    asked = rig.beside;
    die(false);
    held_frames(PATIENCE * 4u);
    ut_check(rig.beside == asked && rig.outcome == RUNNING,
             "a thousand frames drawn over a clock that stands name no corpse and make no wish");
    frames(PATIENCE);
    ut_check(rig.beside == asked + 1u, "and once the clock runs again the wish is made");
}

int main(void)
{
    check_a_lost_wish_is_made_again();
    check_the_last_resort();
    check_a_death_nobody_heard();
    check_nobody_standing();
    check_a_death_place_that_is_no_place();
    check_a_held_world();
    check_a_body_that_stands_after_all();

    mp_reentry_report();
    return ut_summary("mp_reentry_again");
}
