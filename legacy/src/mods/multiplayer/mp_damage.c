/* mp_damage.c: the no-latch death, the one detour that routes a bank body onto it, and the
 * provocation that proves a death does not end the level.
 *
 * SIZE NOTE: over 600 lines, one subject: a death, the player's own and a bank body's, taken at the
 * one hull on the engine's death entry. The length is the byte proof of the record fields, the
 * descriptor and the death counter, folded into the comments. The next seam is the provocation,
 * mp_damage_provoke_* with the outcome watch, which only a test run switches on and which shares
 * nothing with the hull but the state record.
 *
 * The replica works through the player pointer cell, so it acts on whichever record is current:
 * inside the second body's tick that is the hero block carrying bank 1's content, and inside a
 * pointer swap it is the bank's own block. The dispatch is the active bank class rather than the
 * swapped flag, for the same reason the shot hull's is: bank 1 is active during the content-swap
 * tick while the swapped flag is down.
 *
 * The kill and the revive both run inside a bank window opened through mp_bank_run_second, with a
 * window function in place of the phase walk. That reuses the one proven "bank 0 steps aside"
 * mechanism instead of growing a second one, and it means the class answers the bank's own value
 * for the engine calls made inside, the clip play and the stand entry.
 */
#include "mp_damage.h"

#include "mp_bank.h"
#include "mp_body.h"
#include "mp_cells.h"
#include "mp_damage_entry.h"
#include "mp_phases.h"
#include "mp_player_sound.h"
#include "mp_signatures.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <intrin.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Player record fields, byte-confirmed against the retail death and the death descriptor store.
 * The dead flag is the death entry's own `mov [ecx+0x394],1` at 004500C8, six bytes after the
 * player pointer load `mov ecx,[0x4b5220]`; the mode slot and the cause are the two stores 0x70
 * into the same function, the descriptor to +0x60 and the cause to +0x364, which is the store
 * the descriptor cell is read out of. */
#define RECORD_MODE        0x60u
#define RECORD_HACTOR      0x0Cu
#define RECORD_DEATH_CAUSE 0x364u
#define RECORD_DEAD        0x394u

/* ==============================================================================================
 * Finding the death statistic, which the survived death has to put back.
 *
 * The retail death increments one counter, and that counter is also the level's own death count:
 * the skills tick reads it and takes a step off the difficulty at three deaths and another at
 * seven. The difficulty is the column the damage table is indexed by, so a session in which one
 * player dies three times would quietly start dealing different damage, and the two machines
 * would then disagree about every hit. The survived death therefore writes the counter back.
 *
 * The counter has no cell of its own, so its address is read out of the increment's own operands,
 * anchored on the death entry this module has already resolved by signature. The pattern is the
 * load, the add of one and the store, with both absolute operands wildcarded and then required to
 * be EQUAL, which is what makes it a read-modify-write of one cell rather than a coincidence.
 * ============================================================================================ */
#define STAT_WINDOW_BYTES 0x60u
#define STAT_PATTERN_SIZE 15u
#define STAT_LOAD_OPERAND 2u
#define STAT_STORE_OPERAND 11u

/* The fifteen bytes the pattern is cut from, inside the retail death entry:
 *
 *     004500DC  8B 15 C8 2E 87 00   mov edx,[0x872ec8]    entry +0x2C, operand at +0x2E
 *     004500E2  83 C2 01            add edx,1
 *     004500E5  89 15 C8 2E 87 00   mov [0x872ec8],edx    entry +0x35, operand at +0x37
 *
 * Both absolute operands are masked, which is why they are read back and compared. The shape
 * occurs 35 times in the retail code section, 56 and 53 times in the alternate link and the
 * editor's recompile,
 * and exactly once inside the death entry's first 0x60 bytes in every shipped image: at 004500DC
 * naming 00872EC8 in the three retail images, at 0045007C naming 00872E68 in the recompiled one,
 * whose death entry sits at 00450050. The next hit in either build, 00455056 and 00454FF6, lies
 * far outside the window. */
static const uint8_t SIG_DEATH_STAT_BUMP[STAT_PATTERN_SIZE] = {
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC2, 0x01, 0x89, 0x15, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_DEATH_STAT_BUMP[STAT_PATTERN_SIZE] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};

/* The object's flag word is its first dword; bit 1 is the projected ground shadow, which the
 * retail death clears and the revive puts back. */
#define OBJ_FLAGS            0x00u
#define OBJ_CASTS_SHADOW     0x02u

/* The forward death clip's ordinal and the clip mode the retail death plays every death with.
 * The retail path picks between three hit clips by probing the floor and the wall behind; the
 * replica always plays the forward one, which is a stated simplification, not an oversight. */
#define DEATH_CLIP_FORWARD   0x19
#define CLIP_MODE_ONE_SHOT   4

typedef int32_t(__cdecl *play_clip_fn_t)(void *obj, int32_t clip, int32_t mode);
typedef void(__cdecl *enter_stand_fn_t)(void);
typedef void(__cdecl *enter_death_fn_t)(int32_t cause);
typedef void(__cdecl *set_health_fn_t)(int32_t hp);

typedef struct mp_damage_state {
    bool             installed;
    detour_t         death_hull;

    play_clip_fn_t   play_clip;
    enter_stand_fn_t enter_stand;
    set_health_fn_t  set_health;
    bool             spawn_health_seen;
    int32_t          spawn_health;
    uintptr_t        pr_cell;
    uintptr_t        outcome_cell;
    uintptr_t        death_desc;      /* the descriptor's address, read out of the store operand */
    uintptr_t        stand_desc;
    uintptr_t        stat_cell;       /* the death counter, read out of its own increment */

    uint32_t         replica_deaths;  /* bank deaths routed onto the no-latch path by the hull */
    bool             replica_logged;
    uint32_t         faults;          /* a write or read inside the replica that did not take */

    bool             survives_death;  /* the player's own death may leave the level running */
    uint32_t         own_survived;    /* own deaths made inert */
    uint32_t         own_retail;      /* own deaths left to end the level, and why */
    uint32_t         own_retail_scripted;
    bool             own_survived_logged;
    uint32_t         stat_restored;   /* survived deaths whose counter was put back */
    uint32_t         stat_unprotected;

    bool             provoking;
    uint32_t         provoke_count;
    bool             provoke_killed;
    bool             provoke_revived;
    uint32_t         outcome_samples;
    uint32_t         outcome_breaks;  /* samples between kill and revive that did not read 2 */
} mp_damage_state_t;

static mp_damage_state_t damage;

bool mp_damage_installed(void)
{
    return damage.installed;
}

void mp_damage_set_survives_death(bool survives)
{
    damage.survives_death = survives;
}

bool mp_damage_survives_death(void)
{
    return damage.survives_death;
}

/* The rule was first written as "health at zero", from reading the two arms of the death check
 * phase alone. The census of the death entry's callers refuted it: a hard landing, a long fall
 * and a fire also enter here, with causes 1, 2 and 3, and would still have ended the level for
 * everyone. What is actually required, that a failed objective keeps ending the level, is
 * exactly the cause 4 arm. */
mp_damage_death_route_t mp_damage_death_route(int32_t bank_class, int32_t cause, bool survives)
{
    if (bank_class != MP_DAMAGE_LOCAL_BANK_CLASS) {
        return MP_DAMAGE_DEATH_NO_LATCH;
    }
    if (!survives || cause == MP_DAMAGE_CAUSE_SCRIPTED) {
        return MP_DAMAGE_DEATH_RETAIL;
    }
    return MP_DAMAGE_DEATH_SURVIVE;
}

/* The provoked kill enters through the engine's own death entry, hull and all, with a cause that
 * would ordinarily end the level. That is one more claim per run than calling the replica
 * directly: the run then also proves the hull's dispatch routed a bank body away from the retail
 * path in the live process. */
static void provoke_kill_via_engine(void)
{
    ((enter_death_fn_t)mp_signatures_address(MP_SITE_PLR_ENTER_DEATH))(1);
}

mp_damage_provoke_action_t mp_damage_provoke_decide(uint32_t count, bool killed, bool revived)
{
    if (!killed) {
        return (count >= MP_DAMAGE_PROVOKE_KILL_AT) ? MP_DAMAGE_PROVOKE_KILL
                                                    : MP_DAMAGE_PROVOKE_NOTHING;
    }
    if (!revived) {
        return (count >= MP_DAMAGE_PROVOKE_KILL_AT + MP_DAMAGE_PROVOKE_REVIVE_AFTER)
                   ? MP_DAMAGE_PROVOKE_REVIVE
                   : MP_DAMAGE_PROVOKE_NOTHING;
    }
    return MP_DAMAGE_PROVOKE_NOTHING;
}

/* ==============================================================================================
 * The no-latch death and the revive. Both act on whatever record the player pointer names, and
 * both count a fault instead of pressing on when a read or write does not take.
 *
 * The death descriptor at 004B5448 is what makes the corpse safe rather than a loop: thirteen
 * words and a terminator, 02 00 02 02 | 00450309 | 02 02 02 02 02 02 | 00 00 || 01. Phase 0 is
 * 2, which is skip, and phase 0 is the death check, so a body whose health is zero and whose
 * mode is the death descriptor never re-enters the death; phase 4 is the descriptor's own
 * function at 00450309, and everything else but the timers is skipped.
 *
 * No health write in the replica: the writer at 00459EA9 reaches the record through the status
 * pointer cell 0086D57C, which the bank does not swap for the content swap tick, so the zero
 * would land on the player. The slot nulling through [0x868724]+0x18 goes too, because inside a
 * bank tick that +0x18 sits in this feature's own task record and is not a contact slot.
 * ============================================================================================ */

static bool current_record(uintptr_t *record_out)
{
    uint32_t record = 0;

    if (damage.pr_cell == 0 || !memory_read_u32(damage.pr_cell, &record) || record == 0) {
        ++damage.faults;
        return false;
    }
    *record_out = (uintptr_t)record;
    return true;
}

/* The record's actor object, for the shadow bit and the clip. A record without one is answered
 * with NULL rather than a fault: a body can be between spawn and bind for a moment. */
static void *record_object(uintptr_t record)
{
    uint32_t object = 0;

    if (!memory_try_read(record + RECORD_HACTOR, &object, sizeof(object))) {
        return NULL;
    }
    return (void *)(uintptr_t)object;
}

static void no_latch_death(void)
{
    uintptr_t record = 0;
    void     *object;

    if (!current_record(&record)) {
        return;
    }
    if (patch_write_u32(record + RECORD_DEAD, 1u) != PATCH_RESULT_OK ||
        patch_write_u32(record + RECORD_MODE, (uint32_t)damage.death_desc) != PATCH_RESULT_OK ||
        patch_write_u32(record + RECORD_DEATH_CAUSE, MP_DAMAGE_CAUSE_FINISHED) != PATCH_RESULT_OK) {
        ++damage.faults;
        return;
    }

    object = record_object(record);
    if (object != NULL) {
        uint32_t flags = 0;

        if (memory_try_read((uintptr_t)object + OBJ_FLAGS, &flags, sizeof(flags))) {
            patch_write_u32((uintptr_t)object + OBJ_FLAGS, flags & ~(uint32_t)OBJ_CASTS_SHADOW);
        }
        damage.play_clip(object, DEATH_CLIP_FORWARD, CLIP_MODE_ONE_SHOT);
    }
}

static void revive_from_death(void)
{
    uintptr_t record = 0;
    uint32_t  mode = 0;
    void     *object;

    if (!current_record(&record)) {
        return;
    }
    if (patch_write_u32(record + RECORD_DEAD, 0u) != PATCH_RESULT_OK ||
        patch_write_u32(record + RECORD_DEATH_CAUSE, 0u) != PATCH_RESULT_OK) {
        ++damage.faults;
        return;
    }

    object = record_object(record);
    if (object != NULL) {
        uint32_t flags = 0;

        if (memory_try_read((uintptr_t)object + OBJ_FLAGS, &flags, sizeof(flags))) {
            patch_write_u32((uintptr_t)object + OBJ_FLAGS, flags | OBJ_CASTS_SHADOW);
        }
    }

    /* The health goes back BEFORE the stand entry, and inside this window that is safe for the
     * first time: the status record's content is the bank's, so the engine's own health writer
     * lands on the second body. With the death check back in the plan, a revived body carrying
     * zero health would be killed again on its very next tick, which is what this refill
     * prevents. */
    if (damage.spawn_health_seen && damage.spawn_health > 0) {
        damage.set_health(damage.spawn_health);
    }

    /* The engine's own return-to-normal entry: it hangs the stand descriptor on the mode slot and
     * rebuilds the walking state, through the player pointer, so it acts on the window's bank. */
    damage.enter_stand();

    if (memory_try_read(record + RECORD_MODE, &mode, sizeof(mode)) &&
        (uintptr_t)mode != damage.stand_desc) {
        ++damage.faults;
        log_warning("the revive's stand entry left mode %08X where the stand descriptor %08X was "
                    "expected", (unsigned)mode, (unsigned)damage.stand_desc);
    }
}

/* ==============================================================================================
 * The hull on the retail death. A bank body dies the no-latch way and the original is deliberately
 * not called for it. The player's own body always runs the original, and then either keeps the
 * retail corpse or is made inert.
 *
 * The site the detour lands on, the retail death entry:
 *
 *     004500B0  55                push ebp
 *     004500B1  8B EC             mov ebp,esp
 *     004500B3  83 EC 14          sub esp,0x14
 *     004500B6  A1 24 87 86 00    mov eax,[0x868724]
 *
 * A jump needs five bytes and the fifth is the middle of the sub, so the shortest prologue a
 * detour can copy is the six bytes up to 004500B6, the first instruction boundary past them.
 * The pattern matches exactly once in every shipped image; the recompiled build has the entry at
 * 00450050.
 * ============================================================================================ */

/* The whole of the survived death, after the original has run. The cause the original stored is
 * replaced by the one the descriptor's function has no arm for, so the corpse stays a corpse and
 * the level outcome is never raised.
 *
 * The counter the original incremented goes back to what it held. It is the level's death count as
 * well as the campaign's, and three of them take a step off the difficulty the damage table is
 * indexed by; a session in which the damage tables drift apart is worse than one that does not
 * count deaths. A counter that could not be found is counted here rather than passed over, because
 * the drift it causes is invisible in the log otherwise.
 *
 * The counter has one incrementer, one reader and two resets in the whole image. The reader is
 * the skills tick at 00457FC5, which calls 00457EEE at the third death and again at the seventh,
 * and that takes one off the difficulty cell 00872FA0. The impact lookup at 004477D0 uses that
 * cell as the column of the damage table at 004B4840 whenever its second argument is 1, and the
 * player's damage entry at 00448729 always passes 1, so every point the player takes comes out
 * of that column. */
static void make_death_inert(uint32_t statistic_before, bool statistic_known)
{
    uintptr_t record = 0;

    if (!current_record(&record)) {
        return;
    }
    if (patch_write_u32(record + RECORD_DEATH_CAUSE, MP_DAMAGE_CAUSE_FINISHED) != PATCH_RESULT_OK) {
        ++damage.faults;
        log_error("the player's death could not be made inert, so the level is about to end for "
                  "everyone");
        return;
    }
    ++damage.own_survived;

    if (!statistic_known) {
        ++damage.stat_unprotected;
    } else if (memory_try_write(damage.stat_cell, &statistic_before, sizeof statistic_before)) {
        ++damage.stat_restored;
    } else {
        ++damage.stat_unprotected;
        ++damage.faults;
    }

    if (!damage.own_survived_logged) {
        damage.own_survived_logged = true;
        log_info("the player's own death was made inert: the retail death ran in full and the "
                 "cause is now %d, so the level outcome stays where it is",
                 (int)MP_DAMAGE_CAUSE_FINISHED);
    }
}

static void __cdecl hook_enter_death(int32_t cause)
{
    uintptr_t               caller = (uintptr_t)_ReturnAddress();
    mp_damage_death_route_t route = mp_damage_death_route(mp_bank_active_class(), cause,
                                                          damage.survives_death);
    uint32_t                statistic = 0;
    uint32_t                record = 0;
    bool                    statistic_known;

    if (route == MP_DAMAGE_DEATH_NO_LATCH) {
        ++damage.replica_deaths;
        if (!damage.replica_logged) {
            damage.replica_logged = true;
            log_info("a bank body's death (cause %d) was routed onto the no-latch path; the level "
                     "outcome is untouched", (int)cause);
        }
        no_latch_death();
        return;
    }

    /* This machine's own player. In a session a corpse enters no second death: not the original,
     * and not the note after it, which would be a second wish for one player. */
    (void)memory_read_u32(damage.pr_cell, &record);
    if (mp_damage_entry_judge(cause, (uintptr_t)record, caller) == MP_DAMAGE_ENTRY_REFUSE) {
        return;
    }

    /* Read before the call, because the call is what increments it. */
    statistic_known = damage.stat_cell != 0 &&
                      memory_try_read(damage.stat_cell, &statistic, sizeof statistic);

    ((enter_death_fn_t)damage.death_hull.original)(cause);
    /* The cries the engine has just played, for the far side: only for an entry let through. */
    mp_player_sound_note_death(cause, (uintptr_t)record);

    if (route != MP_DAMAGE_DEATH_SURVIVE) {
        ++damage.own_retail;
        if (cause == MP_DAMAGE_CAUSE_SCRIPTED) {
            ++damage.own_retail_scripted;
        }
        return;
    }
    make_death_inert(statistic, statistic_known);
    /* And the same event told to the half that brings him back, from here rather than only from
     * the contact dispatcher.
     *
     * This is the one arm that suppresses the level's own end, so the wish that undoes that
     * suppression is raised here too. Raised only by the dispatcher, which sees a death only while
     * it is delivering a contact, every death with no contact behind it, a fall, a drowning, a
     * burn, a crush, was made inert here and asked back nowhere, and the player stayed a corpse in
     * a level that would not end, with no pause menu to leave it by.
     *
     * After the cause has been rewritten, so that what the listener reads about this death is what
     * the death finally is; the report stands aside when a contact delivery is in progress,
     * because that half is a few instructions from reporting the same death with a killer named. */
    mp_body_death_note_engine_death();
}

/* ==============================================================================================
 * Installation, the provocation, and the report.
 * ============================================================================================ */

/* The death counter's address, out of the increment inside the death entry. The window starts
 * PAST the prologue, because the prologue is what a detour overwrites and reading an operand out
 * of another module's jump distance is a way to get four plausible bytes that mean nothing. It is
 * read before this module places its own detour, for the same reason.
 *
 * A window rather than the whole image: the read-modify-write shape occurs thirty five times in
 * the retail code section, so it identifies nothing on its own. Inside the death entry's first
 * ninety six bytes it occurs exactly once, and that one is the death counter. */
static uintptr_t resolve_death_statistic(uintptr_t death_site, size_t death_prologue)
{
    uint8_t   window[STAT_WINDOW_BYTES];
    size_t    offset = 0;
    size_t    matches;
    uint32_t  load = 0;
    uint32_t  store = 0;
    uintptr_t start = death_site + death_prologue;
    size_t    size  = STAT_WINDOW_BYTES - death_prologue;

    if (death_prologue >= STAT_WINDOW_BYTES ||
        !memory_try_read(start, window, size)) {
        return 0;
    }
    matches = signature_count_in_buffer(window, size, SIG_DEATH_STAT_BUMP, MSK_DEATH_STAT_BUMP,
                                        sizeof SIG_DEATH_STAT_BUMP, &offset, 1u);
    if (matches != 1u) {
        return 0;
    }

    /* The two operands must name the same cell. That is what separates the counter's own
     * read-modify-write from a load and a store that merely stand next to each other. */
    memcpy(&load, window + offset + STAT_LOAD_OPERAND, sizeof load);
    memcpy(&store, window + offset + STAT_STORE_OPERAND, sizeof store);
    if (load != store || load == 0u || !memory_is_readable_range((uintptr_t)load, sizeof load)) {
        return 0;
    }
    return (uintptr_t)load;
}

bool mp_damage_install(void)
{
    uintptr_t death_site;
    size_t    death_prologue;

    if (damage.installed) {
        return true;
    }

    /* Each of the four sites matches exactly once in every shipped image. The recompiled build
     * has the death entry, the health writer and the stand entry 0x60 lower (00450050, 00459E49,
     * 0044CCBC) and the clip player unchanged at 0041263F, because its object file is in the half
     * that build did not touch. The descriptor cell is read out of the store 0x70 into the death
     * entry, behind the prologue this detour occupies, so that read survives the hook. */
    death_site     = mp_signatures_address(MP_SITE_PLR_ENTER_DEATH);
    death_prologue = mp_signatures_prologue(MP_SITE_PLR_ENTER_DEATH);
    damage.play_clip    = (play_clip_fn_t)mp_signatures_address(MP_SITE_BAPOBJ_PLAY_CLIP);
    damage.enter_stand  = (enter_stand_fn_t)mp_signatures_address(MP_SITE_PLR_ENTER_STAND);
    damage.set_health   = (set_health_fn_t)mp_signatures_address(MP_SITE_STATUS_SET_HEALTH);
    damage.pr_cell      = mp_cells_address(MP_CELL_PR);
    damage.outcome_cell = mp_cells_address(MP_CELL_LEVEL_OUTCOME);
    damage.death_desc   = mp_cells_address(MP_CELL_MODE_DEATH_DESC);
    damage.stand_desc   = mp_cells_address(MP_CELL_MODE_STAND_DESC);

    if (death_site == 0 || damage.play_clip == NULL || damage.enter_stand == NULL ||
        damage.set_health == NULL || damage.pr_cell == 0 || damage.outcome_cell == 0 ||
        damage.death_desc == 0 || damage.stand_desc == 0) {
        log_warning("the damage module cannot install: a death site or descriptor cell did not "
                    "resolve");
        return false;
    }

    /* Read before the detour is placed, so the window is the engine's own bytes. */
    damage.stat_cell = resolve_death_statistic(death_site, death_prologue);

    if (!detour_install(&damage.death_hull, death_site, (const void *)&hook_enter_death,
                        death_prologue)) {
        log_warning("the damage module cannot install: the death hull did not take at %08X",
                    (unsigned)death_site);
        return false;
    }

    damage.installed = true;
    log_info("the death hull stands at %08X: a bank body dies without touching the level outcome, "
             "and the player's own death runs the retail path in full", (unsigned)death_site);
    if (damage.stat_cell != 0) {
        log_info("the death counter is at %08X and a survived death puts it back, so three deaths "
                 "cannot take a step off the difficulty the damage table is indexed by",
                 (unsigned)damage.stat_cell);
    } else {
        log_warning("the death counter was not found inside the death entry, so a survived death "
                    "cannot put it back; after three of them the difficulty drops a step and the "
                    "two machines start dealing different damage");
    }
    return true;
}

void mp_damage_enable_provocation(void)
{
    damage.provoking = true;
}

static void watch_outcome(void)
{
    uint32_t outcome = 0;

    if (!memory_try_read(damage.outcome_cell, &outcome, sizeof(outcome))) {
        return;
    }
    ++damage.outcome_samples;
    if (outcome != 2u) {
        ++damage.outcome_breaks;
        log_error("the level outcome read %u during the provoked death, which is the failure this "
                  "module exists to prevent", (unsigned)outcome);
    }
}

void mp_damage_provoke_tick(void)
{
    if (!damage.installed || !damage.provoking || !mp_body_second_exists()) {
        return;
    }
    /* The health the second body was born with, read once outside any window, so the revive can
     * put it back. Zero would mean the bank was not filled yet; then the refill is skipped. */
    if (!damage.spawn_health_seen) {
        damage.spawn_health_seen = true;
        damage.spawn_health      = mp_bank_second_health();
    }
    ++damage.provoke_count;

    switch (mp_damage_provoke_decide(damage.provoke_count, damage.provoke_killed,
                                     damage.provoke_revived)) {
    case MP_DAMAGE_PROVOKE_KILL:
        damage.provoke_killed = true;
        if (mp_bank_run_second(&provoke_kill_via_engine)) {
            log_info("the provocation killed the second body on purpose after %u substep(s); it "
                     "should fall, lie still and not end the level",
                     (unsigned)damage.provoke_count);
        } else {
            log_warning("the provocation could not open a bank window to kill the second body");
        }
        break;
    case MP_DAMAGE_PROVOKE_REVIVE:
        damage.provoke_revived = true;
        if (mp_bank_run_second(&revive_from_death)) {
            mp_phases_note_second_revived();
            mp_body_note_second_revived();
            log_info("the provocation revived the second body; it should stand up and be ticked "
                     "again, with the level outcome sampled %u time(s) in between and %u of them "
                     "not reading 2", (unsigned)damage.outcome_samples,
                     (unsigned)damage.outcome_breaks);
        } else {
            log_warning("the provocation could not open a bank window to revive the second body");
        }
        break;
    case MP_DAMAGE_PROVOKE_NOTHING:
    default:
        break;
    }

    if (damage.provoke_killed && !damage.provoke_revived) {
        watch_outcome();
    }
}

void mp_damage_report(const char *why)
{
    if (!damage.installed) {
        return;
    }
    log_info("the damage module at %s: %u bank death(s) through the no-latch path, provocation "
             "%s, %u outcome sample(s) with %u not reading 2, %u fault(s)",
             why, (unsigned)damage.replica_deaths,
             damage.provoke_revived ? "completed" : (damage.provoke_killed ? "mid-death"
                                                                           : "not fired"),
             (unsigned)damage.outcome_samples, (unsigned)damage.outcome_breaks,
             (unsigned)damage.faults);
    log_info("  the player's own deaths (survival %s): %u made inert, %u left to end the level, "
             "%u of those ordered by a script | the death counter was put back %u time(s) and "
             "went unprotected %u time(s)",
             damage.survives_death ? "on" : "off", (unsigned)damage.own_survived,
             (unsigned)damage.own_retail, (unsigned)damage.own_retail_scripted,
             (unsigned)damage.stat_restored, (unsigned)damage.stat_unprotected);
    mp_damage_entry_report();
}
