/* mp_phases.c: the player pipeline's loop, replicated, over the phase table as shipped on disk.
 *
 * The loop is small and its every detail is load bearing, so it is written out rather than
 * approximated: the descriptor word is read twice per phase, before the call to decide what runs
 * and after it to decide whether the cursor advances, because a phase that changes the mode also
 * changes which descriptor the second read sees. The walk ends on the terminator without touching
 * the mode-changed flag, and on the flag with clearing it; a mode entered in the last phase would
 * otherwise carry a set flag into the next substep and end its first walk after one phase.
 *
 * Two phases are not the engine's for the second body. The death check would kill the second
 * body off the PLAYER's health, because the status pointer is not banked; and the ground publish
 * would copy the second body's ground record over the player's in the level, fire the plates
 * under the second body's feet as the player's, and feed the shared camera. What the publish also
 * does, and the second body needs, is copy its heading onto its actor's yaw, so a yaw-only
 * replacement stands in its place. The input phase RUNS: it reads its keys through the input
 * split, which answers the second body's bank from the injected command, so weapon select,
 * attack and force push there are the far player's and nobody else's. The one key it would misuse
 * is the objectives key, whose quick save is the local machine's; the bridge maps that bit to no
 * action. This matters for the loopback proof only: under a network role the second body is a
 * puppet of the peer's own state and is never walked, so nothing of phase six reaches the wire.
 */
#include "mp_phases.h"

#include "mp_bank.h"

#include "mp_cells.h"

#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Player record fields the loop and the replacement phase touch, byte-confirmed against the phase
 * runner and the ground publish. +0x5C and +0x60 are the two fields the engine's loop at
 * 0x00448297 reads (the do-while condition and the descriptor it indexes); +0xF8, +0x1C4 and
 * +0x2A0 are the three the ground publish at 0x0044CC2F reads and writes; +0x394 is the flag the
 * death entry at 0x004500B0 writes. */
#define RECORD_MODE_CHANGED     0x5Cu   /* set by a mode entry, ends the walk, cleared on exit */
#define RECORD_DESCRIPTOR       0x60u   /* pointer to the current mode's phase word list */
#define RECORD_ACTOR_YAW        0xF8u   /* what the actor is drawn facing */
#define RECORD_CAM_ANCHOR_PINNED 0x1C4u /* 1 while a fall holds the camera at a fixed point */
#define RECORD_HEADING          0x2A0u  /* the steered heading */
#define RECORD_DEAD             0x394u  /* set to 1 by the death entry, never cleared in place */

/* The three descriptor words that are not function pointers. */
#define PHASE_OP_RUN         0u
#define PHASE_OP_RUN_AND_PIN 1u
#define PHASE_OP_SKIP        2u

/* The terminator that follows the thirteen default entries in the table. The table is at
 * 0x004B5228 in the retail image and its terminator dword of 1 at 0x004B525C; its thirteen
 * entries, in order: the death check 0x00448ECF, the timers 0x00448C0F (which writes the frame
 * dt at +0x74 from the frame delta cell), the steer 0x00449EE8, the floor drift 0x0044AAAA, the
 * mode tick 0x00451365 (empty; every descriptor puts a function here), the ground actions
 * 0x0044AE65 (sidle, jump, the USE chain), the input 0x0044AF93, the integrate 0x0044A59E, the
 * ground probe 0x0044C150, the vault gate 0x0044C36D, the collision 0x0044C207, the ground
 * publish 0x0044CC2F and the pose commit 0x0044C06B. */
#define PHASE_TABLE_TERMINATOR 1u
#define PHASE_TABLE_WORDS      (MP_PHASES_COUNT + 1u)

/* The default phases the second body does not get, or gets replaced.
 *
 * Phase 0's whole body: return unless the level is running and not paused; if the outcome is
 * below 5, read the health through the status pointer at 0x0086D57C and enter the death when it
 * is below 1; else enter the death with cause 4. The status pointer is not part of the bank, so
 * the health it reads is the player's, and a second body walked through it dies the moment the
 * player does. It writes nothing else; an earlier reading had it writing the frame dt on the way
 * in, and the decompile puts that write in phase 1, so skipping phase 0 costs no timing.
 *
 * Phase 11 copies 0x22 dwords of the ground record from +0x2CC into the level at +0xA20, feeds
 * the camera from the record's position and heading, and, unless the camera anchor is pinned,
 * fires the plate under the feet (except in the push block mode) and copies the heading at
 * +0x2A0 onto the actor yaw at +0xF8. Everything but the yaw copy is for the level or the
 * camera: the ground record would overwrite the player's, the plate would fire as the player's,
 * and the camera feed is what dragged the shared camera onto the ticked bank in the first
 * version. The yaw copy is what the actor is drawn facing, and without it the body walks
 * sideways, so a yaw-only replacement stands in the slot.
 *
 * Phase 6 selects weapons on keys 0x0B to 0x10 and the previous and next keys, handles the
 * objectives key with its quick save and subtitle, the attack tap through the hold accumulator
 * at 0x006CF9EC, the attack hold through the sabre action select, the fire start and force
 * push. It was withheld while its readers were the player's own keys; with the input split it
 * runs, and the accumulator one dword past the hero block is banked with the block. */
#define PHASE_DEATH_CHECK    0u
#define PHASE_TIMERS         1u
#define PHASE_PUBLISH_GROUND 11u

typedef struct mp_phases_state {
    bool             installed;
    uintptr_t        table;                        /* the live phase table's address */
    mp_phase_fn_t    pristine[MP_PHASES_COUNT];    /* the entries the executable carries on disk */
    size_t           foreign;                      /* live entries that are not the pristine ones */
    mp_phases_plan_t second;                       /* the second body's plan */
    mp_phases_plan_t puppet;                       /* timers, yaw and commit, for a
                                                    * snapshot-driven body */
    mp_phase_fn_t    puppet_timers;                /* the puppet module's phase one, or NULL */

    bool             dead[MP_BANK_FAR_MAX];   /* per far bank: its tick ended on the dead flag */
    bool             fault_logged;
    uint32_t         faults;
} mp_phases_state_t;

static mp_phases_state_t phases;

bool     mp_phases_ready(void)         { return phases.installed; }
size_t   mp_phases_foreign_slots(void) { return phases.foreign; }
uint32_t mp_phases_second_faults(void) { return phases.faults; }

bool mp_phases_dead_at(size_t index)
{
    return mp_bank_index_ok(index) && phases.dead[index - 1u];
}

void mp_phases_note_revived_at(size_t index)
{
    if (mp_bank_index_ok(index)) {
        phases.dead[index - 1u] = false;
    }
}

bool mp_phases_second_dead(void)
{
    return mp_phases_dead_at(1u);
}

void mp_phases_note_second_revived(void)
{
    mp_phases_note_revived_at(1u);
}

/* The death check goes back into the second body's plan. Safe only once two things hold that were
 * not true when the plan was first cut: the tick banks the status record's content, so the health
 * the check reads is the second body's own, and the death hull routes the entry it calls onto the
 * no-latch path. The caller vouches for both; this only edits the plan. */
bool mp_phases_enable_death_check(void)
{
    if (!phases.installed) {
        return false;
    }
    phases.second.phase[PHASE_DEATH_CHECK] = phases.pristine[PHASE_DEATH_CHECK];
    return true;
}

static bool read_word(uintptr_t address, uint32_t *word)
{
    return memory_try_read(address, word, sizeof(*word));
}

/* The current descriptor word for the cursor, through the record's descriptor pointer. Read as a
 * pair on purpose: a phase may have changed the pointer since the last read. */
static bool read_descriptor_word(uintptr_t record, size_t cursor, uint32_t *word)
{
    uint32_t descriptor = 0;

    if (!read_word(record + RECORD_DESCRIPTOR, &descriptor) || descriptor == 0) {
        return false;
    }
    return read_word((uintptr_t)descriptor + cursor * sizeof(uint32_t), word);
}

/* The engine's loop at 0x00448297, as the decompiler reads it, which is what the test pins:
 *
 *     local_c = 0;  local_8 = 0;                 index into the table, cursor into the descriptor
 *     do {
 *         if (table[local_c] == 1) return;                    the terminator, flag untouched
 *         op = [[pr+0x60] + local_8*4];
 *         if (op == 0 || op == 1) table[local_c]();           the default
 *         else if (op != 2) op();                             the mode's own function
 *         if ([[pr+0x60] + local_8*4] != 1) local_8++;        read AGAIN after the call
 *         local_c++;
 *     } while ([pr+0x5c] == 0);
 *     [pr+0x5c] = 0;
 *
 * The pin (word 1) runs the default AND holds the cursor, so every later default runs unfiltered;
 * that is how the shipped descriptors are shorter than thirteen words (the stand is five words
 * and a pin). Seven of the fourteen descriptors put 2 at word 0, so the airborne modes skip the
 * death check by the engine's own hand. A NULL in the plan withholds a default only where the
 * descriptor's word is 0 or 1; a function word is the mode's own and always runs, which is the
 * right reading because the jump and fall modes carry the midair attack trigger at word 5. */
bool mp_phases_dispatch(const mp_phases_plan_t *plan, uintptr_t record)
{
    size_t   index = 0;
    size_t   cursor = 0;
    uint32_t changed = 0;

    if (plan == NULL || record == 0) {
        return false;
    }

    if (plan->placed) {
        for (index = 0; index < MP_PHASES_COUNT; ++index) {
            if (plan->phase[index] != NULL) {
                plan->phase[index]();
            }
        }
        return true;
    }

    do {
        uint32_t op = 0;

        if (index >= MP_PHASES_COUNT) {
            return true;    /* the terminator: out without clearing the flag, as the engine does */
        }
        if (!read_descriptor_word(record, cursor, &op)) {
            return false;
        }

        if (op == PHASE_OP_RUN || op == PHASE_OP_RUN_AND_PIN) {
            if (plan->phase[index] != NULL) {
                plan->phase[index]();
            }
        } else if (op != PHASE_OP_SKIP) {
            ((mp_phase_fn_t)(uintptr_t)op)();
        }

        /* After the call, because the phase may have swapped the descriptor: the pin decision is
         * made on whatever word is there now. */
        if (!read_descriptor_word(record, cursor, &op)) {
            return false;
        }
        if (op != PHASE_OP_RUN_AND_PIN) {
            ++cursor;
        }
        ++index;

        if (!read_word(record + RECORD_MODE_CHANGED, &changed)) {
            return false;
        }
    } while (changed == 0);

    return mp_bank_window_write_u32(record + RECORD_MODE_CHANGED, 0);
}

/* What the ground publish does for the body itself and nothing of what it does for the level or
 * the camera. The engine skips the yaw refresh while the camera anchor is pinned, so this does
 * too, or a falling second body would turn under a camera that is not even watching it. */
static void __cdecl second_publish_yaw(void)
{
    uintptr_t pr_cell = mp_cells_address(MP_CELL_PR);
    uint32_t  record = 0;
    uint32_t  pinned = 0;
    uint32_t  heading = 0;

    if (pr_cell == 0 || !read_word(pr_cell, &record) || record == 0) {
        return;
    }
    if (!read_word((uintptr_t)record + RECORD_CAM_ANCHOR_PINNED, &pinned) || pinned == 1u) {
        return;
    }
    if (!read_word((uintptr_t)record + RECORD_HEADING, &heading)) {
        return;
    }
    mp_bank_window_write_u32((uintptr_t)record + RECORD_ACTOR_YAW, heading);
}

static bool inside_text(uintptr_t address)
{
    uintptr_t text = host_image_text();

    return text != 0 && address >= text && address < text + host_image_text_size();
}

/* The table as the file carries it, relocated, with every entry required to be code. A table that
 * fails this is not the table the loop was written for, whatever the pattern that found it says. */
static bool read_pristine_table(uintptr_t table)
{
    uint32_t words[PHASE_TABLE_WORDS];
    intptr_t delta;
    size_t   index;

    if (!host_image_read_original(table, words, sizeof(words))) {
        log_warning("the phase table could not be read from the executable on disk, so the second "
                    "body's loop has no pristine entries to run");
        return false;
    }
    if (words[MP_PHASES_COUNT] != PHASE_TABLE_TERMINATOR) {
        log_warning("the phase table on disk does not end in the terminator after %u entries, so "
                    "it is not the table this loop knows", (unsigned)MP_PHASES_COUNT);
        return false;
    }

    delta = host_image_relocation_delta();
    for (index = 0; index < MP_PHASES_COUNT; ++index) {
        uintptr_t entry = (uintptr_t)words[index] + (uintptr_t)delta;

        if (!inside_text(entry)) {
            log_warning("phase %u on disk is %08X, outside the code section, so the table is "
                        "refused", (unsigned)index, (unsigned)entry);
            return false;
        }
        phases.pristine[index] = (mp_phase_fn_t)entry;
    }
    return true;
}

/* Name every live entry that is not the pristine one. A foreign entry outside the code section is
 * another fix's thunk and is what this loop exists to bypass; one inside the code section that
 * differs from the file is a table this feature does not understand, and refuses. The input fix
 * replaces words 2 and 7: its steer thunk drains the host mouse bank into the steer, zeroes the
 * substep mouse, steps the strafe damper and halves the aim hold seconds, and its integrate thunk
 * does the integrate side; each is written for one body per substep and consumes what it reads,
 * so a second walk through the live table would drain a bank already drained and double step the
 * damper. With that fix loaded the foreign count is two. */
static bool compare_live_table(uintptr_t table)
{
    uint32_t live[PHASE_TABLE_WORDS];
    size_t   index;

    if (!memory_read(table, live, sizeof(live))) {
        log_warning("the live phase table at %08X is not readable", (unsigned)table);
        return false;
    }
    if (live[MP_PHASES_COUNT] != PHASE_TABLE_TERMINATOR) {
        log_warning("the live phase table has lost its terminator, so it is refused");
        return false;
    }

    phases.foreign = 0;
    for (index = 0; index < MP_PHASES_COUNT; ++index) {
        uintptr_t entry = (uintptr_t)live[index];

        if (entry == (uintptr_t)phases.pristine[index]) {
            continue;
        }
        if (inside_text(entry)) {
            log_warning("phase %u is %08X live and %08X on disk, both inside the code section, "
                        "which is a table this feature does not know", (unsigned)index,
                        (unsigned)entry, (unsigned)(uintptr_t)phases.pristine[index]);
            return false;
        }
        ++phases.foreign;
        log_info("phase %u is held by another fix at %08X; the second body's loop bypasses it and "
                 "runs the original at %08X", (unsigned)index, (unsigned)entry,
                 (unsigned)(uintptr_t)phases.pristine[index]);
    }
    return true;
}

bool mp_phases_install(void)
{
    uintptr_t table;

    if (phases.installed) {
        return true;
    }

    table = mp_cells_address(MP_CELL_PHASE_TABLE);
    if (table == 0) {
        log_warning("the phase table cell did not resolve, so the second body cannot be ticked");
        return false;
    }
    if (!read_pristine_table(table) || !compare_live_table(table)) {
        return false;
    }

    memcpy(phases.second.phase, phases.pristine, sizeof(phases.second.phase));
    phases.second.phase[PHASE_DEATH_CHECK]    = NULL;
    phases.second.phase[PHASE_PUBLISH_GROUND] = &second_publish_yaw;

    /* The puppet plan: a body whose state arrives from the wire is not simulated, it is placed.
     * The yaw refresh and the pose commit run, so the engine's own draw interpolation blends the
     * object from its previous placement to the new one, exactly as it does for a tick; the
     * puppet module's phase one runs, because a replicated weapon only commits and a blade only
     * grows inside the engine's timers; and the record's descriptor is NOT consulted, because the
     * stand mode's own phase four would select clips from a speed and an input the puppet does
     * not have and play its idle over the replicated clip every substep. The stand descriptor at
     * 0x004B5260 reads RUN, RUN, RUN, RUN, a clip select, PIN, and that clip select ends in the
     * idle cycle, which compares the record's clip mirror at +0x36C with the animation set's idle
     * range and restarts the idle with a crossfade when the mirror is outside it; the mirror is
     * refreshed by the engine's phase one, which a puppet never runs, so the moment the weapon
     * commit swapped the animation set the idle restarted every substep over the wire's clip. */
    memset(phases.puppet.phase, 0, sizeof(phases.puppet.phase));
    phases.puppet.placed                      = true;
    phases.puppet.phase[PHASE_TIMERS]         = phases.puppet_timers;
    phases.puppet.phase[PHASE_PUBLISH_GROUND] = &second_publish_yaw;
    phases.puppet.phase[12]                   = phases.pristine[12];

    phases.table     = table;
    phases.installed = true;
    log_info("the second body's loop is ready over the phase table at %08X: %u entries from disk, "
             "%u held by other fixes and bypassed; the death check is skipped, the input phase "
             "runs off the injected command, and the ground publish refreshes yaw only",
             (unsigned)table, (unsigned)MP_PHASES_COUNT, (unsigned)phases.foreign);
    return true;
}

void mp_phases_set_puppet_timers(mp_phase_fn_t timers)
{
    phases.puppet_timers              = timers;
    phases.puppet.phase[PHASE_TIMERS] = timers;
}

static void log_fault_once(const char *what)
{
    ++phases.faults;
    if (phases.fault_logged) {
        return;
    }
    phases.fault_logged = true;
    log_error("the second body's loop %s; later faults are counted, not logged", what);
}

/* The floor polygon a body last stood on, null while airborne. The offset is the
 * reconstruction's own `bapThing` layout (`game/footstep.c`), and the same structure's `pos`
 * at +0x18 is what this feature already reads off an object elsewhere, so the two agree on
 * what kind of pointer it is. */
#define OBJECT_FLOOR_POLY 0xE4u

static uint32_t ground_yes;
static uint32_t ground_no;
static uint32_t ground_ticked;
static bool     ground_bodies_are_ticked;

void mp_phases_set_bodies_are_ticked(bool ticked)
{
    ground_bodies_are_ticked = ticked;
}

void mp_phases_note_ground(uint32_t object)
{
    uint32_t floor = 0;

    if (object == 0u) {
        return;
    }
    /* A body this machine ticks through the whole player pipeline sets its own floor
     * polygon in phase one, so the cell says nothing about what the wire delivers. That
     * happens under the in process loopback, where BankTick stands because it is only
     * switched off for a session over a socket. Counted apart, because counted together it
     * would answer a question nobody asked and answer it encouragingly. */
    if (ground_bodies_are_ticked) {
        ++ground_ticked;
        return;
    }
    if (memory_try_read((uintptr_t)object + OBJECT_FLOOR_POLY, &floor, sizeof floor) &&
        floor != 0u) {
        ++ground_yes;
    } else {
        ++ground_no;
    }
}

void mp_phases_ground_counts(uint32_t *on_floor, uint32_t *airborne, uint32_t *ticked)
{
    if (on_floor != NULL) {
        *on_floor = ground_yes;
    }
    if (airborne != NULL) {
        *airborne = ground_no;
    }
    if (ticked != NULL) {
        *ticked = ground_ticked;
    }
}

void __cdecl mp_phases_run_puppet(void)
{
    uintptr_t pr_cell = mp_cells_address(MP_CELL_PR);
    uint32_t  record = 0;

    if (!phases.installed) {
        return;
    }
    if (pr_cell == 0 || !read_word(pr_cell, &record) || record == 0) {
        log_fault_once("could not read the player pointer for the puppet");
        return;
    }
    if (!mp_phases_dispatch(&phases.puppet, (uintptr_t)record)) {
        log_fault_once("stopped on an unreadable descriptor or record in the puppet plan");
    }
}

void __cdecl mp_phases_run_second(void)
{
    uintptr_t pr_cell = mp_cells_address(MP_CELL_PR);
    uint32_t  record = 0;
    uint32_t  dead = 0;
    size_t    which = mp_bank_active();   /* the bank whose block is at the hero block right now */

    if (!phases.installed || !mp_bank_index_ok(which) || phases.dead[which - 1u]) {
        return;
    }
    if (pr_cell == 0 || !read_word(pr_cell, &record) || record == 0) {
        log_fault_once("could not read the player pointer");
        return;
    }
    if (!mp_phases_dispatch(&phases.second, (uintptr_t)record)) {
        log_fault_once("stopped on an unreadable descriptor or record");
        return;
    }

    /* The death check is skipped, but a pit, a drowning or a crush kills from inside a mode's
     * own phase. One more walk over a dead body would run the death table, whose one function
     * writes the level outcome for everyone. The death entry at 0x004500B0 writes the dead flag,
     * hangs the death descriptor at 0x004B5448 on +0x60 and nulls the current task's contact
     * slot; it does not write the outcome. That descriptor reads 02 00 02 02, then the function
     * 0x00450309, then 02 02 02 02 02 02 00 00, and that one function is what writes the outcome
     * cell at 0x00881368 on later walks: cause 2 at once, cause 3 after two seconds, causes 0
     * and 1 when the animation ends. The campaign loop at 0x0043EB2A spins while the cell reads
     * 2 and opens the death menu at 4 or more.
     *
     * The scheduler's order is why the mod ticks after the player: the task runner at
     * 0x0047582A walks slots 0 to 0x3F in order and registration at 0x0047563D takes the lowest
     * free one, so the shipped order is the enemy at 0, the player at 1, the emitter at 2, the
     * module at 0x00452260 at 3 and this feature's task at 4, with the player's mouse consumed
     * and its position committed before any second body is walked. A crush (contact 0x1E)
     * delivered to the second body's object is misattributed the other way: the contact handler
     * runs inside the task runner, which points the current task at the receiver's record, the
     * shared player node, so the death entry nulls the PLAYER's contact slot. */
    if (read_word((uintptr_t)record + RECORD_DEAD, &dead) && dead != 0u) {
        phases.dead[which - 1u] = true;
        log_warning("bank %u's body died inside its tick and will not be ticked again, so its "
                    "death cannot end the level", (unsigned)which);
    }
}
