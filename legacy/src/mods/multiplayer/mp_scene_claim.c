/* mp_scene_claim.c: whose a script's doors are on the host. See the header.
 *
 * Everything here is asked from inside the engine, in the middle of an actor's script, except the
 * substep's tick, the owed release and the ways out, which come from the host's scene between two
 * runs. So nothing here waits, and every read of an actor is a guarded one.
 *
 * SIZE NOTE: over 600 lines, a hundred of them the report. The run, the doors and the ways out
 * stay together because they read and write the one record, the marks with the latch and what
 * is owed, and every decision is mp_scene_claim_rule's. The next seam is the report with its
 * counters, which the doors only add to.
 */
#include "mp_scene_claim.h"

#include "mp_body.h"
#include "mp_bridge.h"
#include "mp_cells.h"
#include "mp_cutscene.h"
#include "mp_enemy_bind.h"
#include "mp_range_gate.h"
#include "mp_target.h"
#include "mp_wire.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The three things a script takes, as rows of the counts. */
enum { AT_CAMERA = 0, AT_LOCK, AT_BARS, TAKEN_KINDS };

typedef struct claim_counts {
    uint32_t taken[TAKEN_KINDS];        /* takes of a run of the host's, through */
    uint32_t kept[TAKEN_KINDS];         /* takes of a far player's run, refused and remembered */
    uint32_t made_up[TAKEN_KINDS];      /* of those, made up at the door of a scene */
    uint32_t lapsed;                    /* and those that went with their run */
    uint32_t spoken;                    /* cameras of a spoken line marked */
    uint32_t latched;                   /* takes refused for a placement written down */
    uint32_t given_back[TAKEN_KINDS];   /* releases in a run of the host's */
    uint32_t own_back[TAKEN_KINDS];     /* releases in a far player's run of what its actor took */
    uint32_t refused[TAKEN_KINDS];      /* releases refused */
    uint32_t release_lines;
    int32_t  release_named[MP_SCENE_CLAIM_RELEASE_LINES];   /* the placements said so far */
    uint32_t fell_with_actor;           /* marks that fell with the removal of their actor */
    uint32_t forgotten;                 /* times every mark fell at once */
    uint32_t latches;                   /* windows opened */
    uint32_t adopted;                   /* scenes taken over with no door, told here */
    uint32_t takers_written;            /* placements written down for what they had taken */
    uint32_t heroes_written;            /* placements written down as a hero's */
    uint32_t grabs_refused;             /* grabs refused to a hero on a placement written down */
    uint32_t foreign_unwritten;         /* a foreign lock whose placement could not be written */
} claim_counts_t;

typedef struct claim_state {
    uint32_t  substep;      /* the host's substep, as its tick last told it */
    uintptr_t running;      /* the actor whose script runs now, nought outside every script */

    mp_scene_run_doors_t doors;                     /* what this run was refused before a door */
    uint8_t              marks[MP_WIRE_KEY_COUNT];  /* what each placement's actor took here */
    uint32_t             marked;                    /* placements with a mark */
    mp_scene_latch_t     latch;
    mp_scene_owed_t      owed;
    /* The substep each placement's actor was last refused a release of the lock in, plus one. */
    uint32_t             refused_at[MP_WIRE_KEY_COUNT];

    /* The scene of the host's that stands, as the host's scene told it. */
    bool      scene_stands;
    bool      adopted;      /* it was taken over with no door heard */
    uintptr_t door_actor;   /* the actor whose script opened its door, nought for none */
    uint32_t  door_key;
    bool      door_keyed;
    bool      hero_keyed;   /* the placement its hero was spawned on */
    uint32_t  hero_key;
    uintptr_t driver;       /* the actor that drives the host's body, as of the last tick */

    /* The actor of the scene that ended last. */
    bool      after_known;
    uintptr_t after_actor;
    uint32_t  after_key;
    uint32_t  after_since;

    claim_counts_t n;
} claim_state_t;

static claim_state_t claim;

/* ==============================================================================================
 * Small readings.
 * ============================================================================================ */

static bool key_of(uintptr_t actor, uint32_t *key)
{
    return actor != 0u && mp_enemy_bind_index(actor, key) && *key < MP_WIRE_KEY_COUNT;
}

static size_t row_of(uint8_t bit)
{
    return bit == MP_SCENE_MARK_CAMERA ? (size_t)AT_CAMERA
           : bit == MP_SCENE_MARK_LOCK ? (size_t)AT_LOCK
                                       : (size_t)AT_BARS;
}

static const char *bit_text(uint8_t bit)
{
    return bit == MP_SCENE_MARK_CAMERA ? "the camera"
           : bit == MP_SCENE_MARK_LOCK ? "the lock"
                                       : "the bars";
}

/* The one writer of a placement's marks, so the count of marked placements cannot drift. */
static void set_marks(uint32_t key, uint8_t marks)
{
    if (claim.marks[key] == 0u && marks != 0u) {
        ++claim.marked;
    } else if (claim.marks[key] != 0u && marks == 0u && claim.marked != 0u) {
        --claim.marked;
    }
    claim.marks[key] = marks;
}

/* Whether `actor` is an actor of the host's scene: the one whose script opened its door, the hero
 * it spawned, the actor that drives the host's body, or the actor of the scene that ended within
 * the last MP_SCENE_CLAIM_AFTER_SUBSTEPS. An address is believed only with its placement, because
 * the pool hands a slot it took back to the next actor. The age is taken in unsigned arithmetic,
 * so an end stamped after now reads as long ago. */
static bool of_the_scene(uintptr_t actor, uint32_t key, bool keyed)
{
    if (claim.scene_stands) {
        if (claim.door_actor != 0u && actor == claim.door_actor &&
            (!claim.door_keyed || (keyed && key == claim.door_key))) {
            return true;
        }
        if (claim.hero_keyed && keyed && key == claim.hero_key) {
            return true;
        }
    }
    if (claim.driver != 0u && actor == claim.driver) {
        return true;
    }
    return claim.after_known && keyed && actor == claim.after_actor && key == claim.after_key &&
           claim.substep - claim.after_since <= MP_SCENE_CLAIM_AFTER_SUBSTEPS;
}

/* ==============================================================================================
 * The run.
 * ============================================================================================ */

uintptr_t mp_scene_claim_run_begins(uintptr_t actor)
{
    uintptr_t outer = claim.running;

    claim.n.lapsed += (uint32_t)claim.doors.count;
    mp_scene_run_doors_forget(&claim.doors);
    claim.running = actor;
    return outer;
}

void mp_scene_claim_run_ends(uintptr_t outer)
{
    claim.n.lapsed += (uint32_t)claim.doors.count;
    mp_scene_run_doors_forget(&claim.doors);
    claim.running = outer;
}

uintptr_t mp_scene_claim_running(void)
{
    return claim.running;
}

/* The actor's health is the field the engine's wait for a death, opcode 0x102, holds against
 * nought: an actor at or below it is one whose death a script is reacting to. Its placement's
 * record is what the range gate keeps a waking by. */
void mp_scene_claim_whose(uintptr_t actor, mp_scene_claim_run_t *out)
{
    mp_scene_run_evidence_t e;
    uint32_t                key    = 0u;
    uint32_t                age    = 0u;
    uint32_t                record = 0u;
    int32_t                 health = 1;
    bool                    player = false;
    bool                    keyed  = key_of(actor, &key);
    bool                    on_record;

    memset(&e, 0, sizeof e);
    e.joined       = mp_bridge_joined();
    e.of_the_scene = of_the_scene(actor, key, keyed);
    e.taker        = keyed && mp_scene_mark_keeps_the_host(claim.marks[key]);
    on_record      = mp_target_last_answer(actor, &player, &e.actor.own_bank, &age);
    e.actor.own    = mp_scene_answer_of(on_record, player, age);
    e.actor.died   = memory_try_read(actor + MP_CHARACTER_HEALTH, &health, sizeof health) &&
                     health <= 0;
    e.actor.attacker_known = mp_target_last_attacker(actor, &e.actor.attacker_bank);
    e.woke_for_far = memory_try_read_u32(actor + MP_CHARACTER_PLACEMENT, &record) &&
                     record != 0u && mp_range_gate_woke_for_far(record, &e.woke_bank);
    out->rule = mp_scene_run(&e, &out->bank);
    out->own  = e.actor.own;
}

bool mp_scene_claim_run_is_the_hosts(void)
{
    mp_scene_claim_run_t run;

    if (claim.running == 0u) {
        return true;
    }
    mp_scene_claim_whose(claim.running, &run);
    return run.bank == 0u;
}

/* Asked for every question a script puts about the player, so the common case leaves early: with
 * nobody joined, or with no scene, no driver, no scene just ended and no mark anywhere, nothing
 * of the actor is read at all. */
void mp_scene_claim_answer_evidence(uintptr_t actor, mp_target_claim_evidence_t *evidence)
{
    uint32_t key = 0u;
    bool     keyed;

    evidence->joined = mp_bridge_joined();
    if (!evidence->joined ||
        (!claim.scene_stands && claim.driver == 0u && !claim.after_known && claim.marked == 0u)) {
        return;
    }
    keyed = key_of(actor, &key);
    if (of_the_scene(actor, key, keyed)) {
        /* The one test serves both reasons, and which it was is told apart for the count. */
        bool after = !claim.scene_stands && actor != claim.driver;

        evidence->of_the_scene    = !after;
        evidence->after_the_scene = after;
    }
    evidence->taker = keyed && mp_scene_mark_keeps_the_host(claim.marks[key]);
}

/* ==============================================================================================
 * The doors.
 * ============================================================================================ */

bool mp_scene_claim_latched_door(uintptr_t actor, bool hosts_run)
{
    uint32_t key = 0u;

    if (!key_of(actor, &key) ||
        !mp_scene_latch_door(&claim.latch, key, claim.substep, hosts_run)) {
        return false;
    }
    ++claim.n.latched;
    return true;
}

void mp_scene_claim_took(uintptr_t actor, uint8_t bit, bool spoken)
{
    uint32_t key = 0u;

    if (spoken) {
        ++claim.n.spoken;
    } else {
        ++claim.n.taken[row_of(bit)];
    }
    if (key_of(actor, &key)) {
        set_marks(key, mp_scene_mark_taken(claim.marks[key], bit, spoken));
    }
}

mp_scene_claim_take_t mp_scene_claim_take(uintptr_t actor, const mp_scene_claim_run_t *run,
                                          uint8_t bit, int32_t argument)
{
    bool hosts = run->bank == 0u;

    if (mp_scene_claim_latched_door(actor, hosts)) {
        return MP_SCENE_CLAIM_LATCHED;
    }
    if (hosts) {
        mp_scene_claim_took(actor, bit, false);
        return MP_SCENE_CLAIM_PASSES;
    }
    ++claim.n.kept[row_of(bit)];
    (void)mp_scene_run_doors_keep(&claim.doors, bit, argument);
    return MP_SCENE_CLAIM_KEPT;
}

/* The engine is called past the scene gates, in the order the script asked: the bars before the
 * camera for the lock opcode. Each is marked as the actor's own take, so its script gives them
 * back as it would have, had the host stood there from the first. */
uint8_t mp_scene_claim_make_up(uintptr_t actor)
{
    uint8_t made = 0u;
    size_t  index;

    for (index = 0u; index < claim.doors.count && index < MP_SCENE_RUN_DOORS; ++index) {
        const mp_scene_run_door_t *door = &claim.doors.door[index];

        if (door->bit == MP_SCENE_MARK_BARS) {
            mp_cutscene_engine_bars(door->argument != 0);
        } else {
            mp_cutscene_engine_camera(door->argument);
        }
        mp_scene_claim_took(actor, door->bit, false);
        ++claim.n.made_up[row_of(door->bit)];
        made |= door->bit;
    }
    mp_scene_run_doors_forget(&claim.doors);
    return made;
}

/* A refused release is said once for each placement, the first MP_SCENE_CLAIM_RELEASE_LINES of
 * them: some scripts give back on every tick, and one of those would otherwise take every line.
 * The end of an opcode is three calls, so the first of the camera and the lock is the one that
 * is named. The bars are only counted: the lock opcode tells them to go whenever its second
 * operand is nought, in the very call that takes the lock, and that is no end of anything. */
static void say_the_refused_release(const mp_scene_claim_run_t *run, uint8_t bit, bool keyed,
                                    uint32_t key)
{
    int32_t  placement = keyed ? (int32_t)key : -1;
    char     slot_text[16];
    uint8_t  slot = 0u;
    uint32_t said;

    if (bit == MP_SCENE_MARK_BARS || claim.n.release_lines >= MP_SCENE_CLAIM_RELEASE_LINES) {
        return;
    }
    for (said = 0u; said < claim.n.release_lines; ++said) {
        if (claim.n.release_named[said] == placement) {
            return;
        }
    }
    claim.n.release_named[claim.n.release_lines] = placement;
    ++claim.n.release_lines;
    if (mp_body_bank_slot(run->bank, &slot)) {
        (void)text_format(slot_text, sizeof slot_text, "%u", (unsigned)slot);
    } else {
        (void)text_format(slot_text, sizeof slot_text, "unknown");
    }
    log_info("a script's release was refused on the host: placement %d asked to give back %s "
             "and had not taken it here; its run meant world slot %s by %s, so what the host "
             "holds stays his. Said once a placement, %u of %u; every refusal is in the report",
             (int)placement, bit_text(bit), slot_text, mp_scene_run_text(run->rule),
             (unsigned)claim.n.release_lines, (unsigned)MP_SCENE_CLAIM_RELEASE_LINES);
}

/* A release went through, so the engine holds that thing for nobody: the engine has one lock, one
 * camera and one pair of bars, and the mark of whoever took it would otherwise stand until that
 * actor is removed. An actor that keeps a mark is measured against the host alone, so a
 * companion whose lock another actor gave back would follow nobody but the host for the rest of
 * its life. */
static void nobody_holds(uint8_t bit)
{
    uint32_t key;

    for (key = 0u; key < MP_WIRE_KEY_COUNT && claim.marked != 0u; ++key) {
        uint8_t marks = claim.marks[key];

        if ((marks & bit) != 0u) {
            (void)mp_scene_mark_given_back(&marks, bit, true);
            set_marks(key, marks);
        }
    }
}

/* One refusal is a release that may never come again, and it is kept as owed. A second one a run
 * later is an actor that gives back on every run, whatever stands: nothing is owed to it, and its
 * row is given up, so it neither frees the host when it goes away nor holds one of the rows. */
static void note_a_release_owed(uint32_t key, uintptr_t actor)
{
    bool again = mp_scene_owed_again(claim.refused_at[key], claim.substep);

    claim.refused_at[key] = claim.substep + 1u;
    if (again) {
        mp_scene_owed_drop(&claim.owed, key);
        return;
    }
    mp_scene_owed_note(&claim.owed, key, actor, claim.substep);
}

/* A placement whose doors are refused gives back as a far player's run does, whoever its run
 * means: it took nothing here since it was written down, so it has nothing of its own to return,
 * and what it would let go of is another scene's.
 *
 * While a scene taken over with no door heard stands, every other release goes through as a run
 * of the host's does. A savegame restored its lock, its bars and its camera past every door, so
 * no actor carries a mark for them, and a release refused there could only leave the host held. */
bool mp_scene_claim_gives_back(uintptr_t actor, const mp_scene_claim_run_t *run, uint8_t bit)
{
    uint32_t key   = 0u;
    bool     keyed = key_of(actor, &key);
    bool     hosts = (run->bank == 0u || (claim.scene_stands && claim.adopted)) &&
                     !(keyed && mp_scene_latch_holds(&claim.latch, key) != MP_SCENE_REFUSAL_NONE);
    uint8_t  marks = keyed ? claim.marks[key] : 0u;
    bool     went  = mp_scene_mark_given_back(keyed ? &marks : NULL, bit, hosts);

    if (keyed) {
        set_marks(key, marks);
    }
    if (went) {
        if (hosts) {
            ++claim.n.given_back[row_of(bit)];
        } else {
            ++claim.n.own_back[row_of(bit)];
        }
        nobody_holds(bit);
        return true;
    }
    ++claim.n.refused[row_of(bit)];
    if (bit == MP_SCENE_MARK_LOCK && claim.scene_stands && keyed) {
        note_a_release_owed(key, actor);
    }
    say_the_refused_release(run, bit, keyed, key);
    return false;
}

/* ==============================================================================================
 * What the host's scene tells.
 * ============================================================================================ */

void mp_scene_claim_tick(uint32_t substep, uintptr_t driver)
{
    claim.substep = substep;
    claim.driver  = driver;
    if (claim.after_known &&
        substep - claim.after_since > MP_SCENE_CLAIM_AFTER_SUBSTEPS) {
        claim.after_known = false;
    }
}

void mp_scene_claim_scene_adopted(uintptr_t driver)
{
    mp_scene_claim_scene_began(driver, false, 0u);
    claim.adopted = true;
    ++claim.n.adopted;
}

void mp_scene_claim_scene_began(uintptr_t actor, bool hero_keyed, uint32_t hero_key)
{
    claim.scene_stands = true;
    claim.adopted      = false;
    claim.door_actor   = actor;
    claim.door_keyed   = key_of(actor, &claim.door_key);
    claim.hero_keyed   = hero_keyed;
    claim.hero_key     = hero_key;
    claim.after_known  = false;
}

void mp_scene_claim_scene_hero(uint32_t hero_key)
{
    if (claim.scene_stands) {
        claim.hero_keyed = true;
        claim.hero_key   = hero_key;
    }
}

void mp_scene_claim_scene_ended(void)
{
    /* The actor of a lock that was no scene of the host's is not kept for him. */
    claim.after_known = claim.scene_stands && claim.door_actor != 0u && claim.door_keyed &&
                        mp_scene_latch_holds(&claim.latch, claim.door_key) ==
                            MP_SCENE_REFUSAL_NONE;
    claim.after_actor  = claim.door_actor;
    claim.after_key    = claim.door_key;
    claim.after_since  = claim.substep;
    claim.scene_stands = false;
    claim.adopted      = false;
    claim.door_actor   = 0u;
    claim.door_keyed   = false;
    claim.hero_keyed   = false;
}

int32_t mp_scene_claim_foreign(void)
{
    if (!claim.scene_stands || !claim.door_keyed) {
        ++claim.n.foreign_unwritten;
        return -1;
    }
    set_marks(claim.door_key, 0u);
    if (!mp_scene_latch_foreign(&claim.latch, claim.door_key)) {
        ++claim.n.foreign_unwritten;
    }
    return (int32_t)claim.door_key;
}

bool mp_scene_claim_owed_due(bool scene_stands, uint32_t *key)
{
    size_t row;

    for (row = 0u; row < MP_SCENE_OWED_ROWS; ++row) {
        const mp_scene_owed_row_t kept = claim.owed.row[row];
        bool                      lives;

        if (!kept.have) {
            continue;
        }
        lives = mp_enemy_bind_is_live(kept.actor, kept.key, NULL);
        if (mp_scene_owed_step(&claim.owed, row, claim.substep, lives, scene_stands) ==
            MP_SCENE_OWED_DUE) {
            *key = kept.key;
            return true;
        }
    }
    return false;
}

/* ==============================================================================================
 * The ways out.
 * ============================================================================================ */

void mp_scene_claim_forget_marks(void)
{
    if (claim.marked != 0u) {
        ++claim.n.forgotten;
    }
    memset(claim.marks, 0, sizeof claim.marks);
    claim.marked = 0u;
}

void mp_scene_claim_actor_removed(uint32_t key)
{
    if (key >= MP_WIRE_KEY_COUNT) {
        return;
    }
    if (claim.marks[key] != 0u) {
        ++claim.n.fell_with_actor;
        set_marks(key, 0u);
    }
    claim.refused_at[key] = 0u;
    mp_scene_latch_forget_foreign(&claim.latch, key);
    if (claim.door_keyed && claim.door_key == key) {
        claim.door_actor = 0u;   /* its slot is the pool's again */
        claim.door_keyed = false;
    }
    if (claim.after_known && claim.after_key == key) {
        claim.after_known = false;
    }
}

/* Who had taken the camera, the lock or the bars here by then, and the actor of the scene that
 * was just left, would raise them again in a later state of their script, long after the window:
 * they are written down now, and then every mark falls. A speaker's camera alone writes nobody
 * down. */
void mp_scene_claim_latch(void)
{
    uint32_t key;

    for (key = 0u; key < MP_WIRE_KEY_COUNT && claim.marked != 0u; ++key) {
        if (mp_scene_mark_keeps_the_host(claim.marks[key]) &&
            mp_scene_latch_repaired(&claim.latch, key)) {
            ++claim.n.takers_written;
        }
    }
    if (claim.after_known) {
        if (mp_scene_latch_repaired(&claim.latch, claim.after_key)) {
            ++claim.n.takers_written;
        }
        claim.after_known = false;
    }
    mp_scene_claim_forget_marks();
    mp_scene_latch_open(&claim.latch, claim.substep);
    ++claim.n.latches;
}

void mp_scene_claim_latch_hero(uint32_t key)
{
    if (key < MP_WIRE_KEY_COUNT && mp_scene_latch_repaired(&claim.latch, key)) {
        ++claim.n.heroes_written;
    }
}

bool mp_scene_claim_any_written(void)
{
    return claim.latch.count != 0u;
}

bool mp_scene_claim_key_repaired(uint32_t key)
{
    return mp_scene_latch_holds(&claim.latch, key) == MP_SCENE_REFUSAL_REPAIRED;
}

bool mp_scene_claim_repaired(uintptr_t actor)
{
    uint32_t key = 0u;

    return key_of(actor, &key) && mp_scene_claim_key_repaired(key);
}

bool mp_scene_claim_grab_refused(uintptr_t actor)
{
    if (!mp_scene_claim_repaired(actor)) {
        return false;
    }
    ++claim.n.grabs_refused;
    return true;
}

size_t mp_scene_claim_latched(uint32_t *keys, size_t max)
{
    return mp_scene_latch_keys(&claim.latch, keys, max);
}

void mp_scene_claim_world_ended(void)
{
    mp_scene_latch_clear(&claim.latch);
    mp_scene_owed_clear(&claim.owed);
    mp_scene_run_doors_forget(&claim.doors);
    memset(claim.marks, 0, sizeof claim.marks);
    memset(claim.refused_at, 0, sizeof claim.refused_at);
    claim.marked       = 0u;
    claim.scene_stands = false;
    claim.adopted      = false;
    claim.door_actor   = 0u;
    claim.door_keyed   = false;
    claim.hero_keyed   = false;
    claim.driver       = 0u;
    claim.after_known  = false;
    mp_range_gate_forget_woken();
}

void mp_scene_claim_report(void)
{
    const claim_counts_t *n = &claim.n;

    log_info("  the doors of the scripts (the host): %u take(s) by a run of the host's (%u of the "
             "camera, %u of the lock, %u of the bars), %u camera(s) of a spoken line marked; %u "
             "take(s) of a far player's run refused and remembered (%u of the camera, %u of the "
             "bars), %u of them made up at the door of a scene (%u of the camera, %u of the "
             "bars), %u gone with their run, %u past the room of a run; %u take(s) refused for a "
             "placement written down",
             (unsigned)(n->taken[AT_CAMERA] + n->taken[AT_LOCK] + n->taken[AT_BARS]),
             (unsigned)n->taken[AT_CAMERA], (unsigned)n->taken[AT_LOCK],
             (unsigned)n->taken[AT_BARS], (unsigned)n->spoken,
             (unsigned)(n->kept[AT_CAMERA] + n->kept[AT_BARS]), (unsigned)n->kept[AT_CAMERA],
             (unsigned)n->kept[AT_BARS],
             (unsigned)(n->made_up[AT_CAMERA] + n->made_up[AT_BARS]),
             (unsigned)n->made_up[AT_CAMERA], (unsigned)n->made_up[AT_BARS],
             (unsigned)n->lapsed, (unsigned)claim.doors.left_out, (unsigned)n->latched);
    log_info("  the releases of the scripts (the host): %u in a run of the host's (%u of the "
             "camera, %u of the lock, %u of the bars); %u in a far player's run by the actor that "
             "had taken (%u, %u, %u); %u refused, their actor having taken nothing here (%u, %u, "
             "%u); of the refused releases of the lock %u were kept as owed, %u made up for the "
             "host after their actor went away, %u let go, %u past the room, %u given up because "
             "their actor was refused again a run later",
             (unsigned)(n->given_back[AT_CAMERA] + n->given_back[AT_LOCK] +
                        n->given_back[AT_BARS]),
             (unsigned)n->given_back[AT_CAMERA], (unsigned)n->given_back[AT_LOCK],
             (unsigned)n->given_back[AT_BARS],
             (unsigned)(n->own_back[AT_CAMERA] + n->own_back[AT_LOCK] + n->own_back[AT_BARS]),
             (unsigned)n->own_back[AT_CAMERA], (unsigned)n->own_back[AT_LOCK],
             (unsigned)n->own_back[AT_BARS],
             (unsigned)(n->refused[AT_CAMERA] + n->refused[AT_LOCK] + n->refused[AT_BARS]),
             (unsigned)n->refused[AT_CAMERA], (unsigned)n->refused[AT_LOCK],
             (unsigned)n->refused[AT_BARS], (unsigned)claim.owed.noted, (unsigned)claim.owed.due,
             (unsigned)claim.owed.forgotten, (unsigned)claim.owed.left_out,
             (unsigned)claim.owed.habitual);
    log_info("  the marks of the takers (the host): %u placement(s) carry one at the report, %u "
             "mark(s) fell with the removal of their actor, every mark fell at once %u time(s)",
             (unsigned)claim.marked, (unsigned)n->fell_with_actor, (unsigned)n->forgotten);
    log_info("  the latch after a player's own release (the host): %u window(s) opened, %u "
             "door(s) refused for a placement written down, %u placement(s) not written down "
             "with the %u full; %u lock(s) of a far player's written down as no scene of the "
             "host's, %u of them taken back by a run of the host's, %u not written down",
             (unsigned)n->latches, (unsigned)claim.latch.refused,
             (unsigned)claim.latch.left_out, (unsigned)MP_SCENE_LATCH_KEYS,
             (unsigned)claim.latch.foreign, (unsigned)claim.latch.taken_back,
             (unsigned)n->foreign_unwritten);
    log_info("  the placements a player's own release wrote down (the host): %u for what their "
             "actor had taken by then, %u as the placement of a hero; %u grab(s) of the host "
             "refused to a hero on one of them; %u scene(s) taken over with no door, in which "
             "every release went through",
             (unsigned)n->takers_written, (unsigned)n->heroes_written,
             (unsigned)n->grabs_refused, (unsigned)n->adopted);
}
