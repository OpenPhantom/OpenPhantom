/* mp_bank.c: the far player banks, the swap, and the provocation that proves it traceless.
 *
 * SIZE NOTE: over 600 lines. Three banks cost no more code than one; what the file carries is the
 * byte level contract of a window into the engine's player, written out once per kind of window
 * (swap, spawn, loan, tick) because each has its own restore order. The next seam is the
 * provocation, which is a test instrument and shares nothing with the windows but the digest.
 *
 * ==================================== What a swap consists of ==================================
 *
 * Three engine-visible things change and everything else is our own memory. The player pointer is
 * stored to, the status pointer is stored to, and the hold accumulator is saved so that a later
 * bank tick cannot destroy bank 0's value: that cell is the out parameter of an input query, but
 * the input phase writes it from INSIDE the pipeline, so it is player state whether it wants to
 * be or not. The hero block itself is never touched; every far bank lives in this DLL.
 *
 * Every address here comes out of a resolved operand. The recompile build is the reason that is
 * not style: it moves the player pointer cell by 0x50 and leaves an unrelated value at the retail
 * address (the bytes there are 01 CD 44 00), so a written down number would swap a stranger's
 * dword.
 *
 * Nobody caches the player pointer across the window. All 1668 occurrences of the cell's dword in
 * the retail code section classify as loads (A1, 8B 0D, 8B 15, three 8B 35, one 8B 3D); there is
 * no store to the cell anywhere in the image, and forward tracing from every load found no copy
 * of the value into another absolute cell. The same trace over the status pointer's 104 sites
 * found none either. This module is the first writer either cell has ever had.
 *
 * The hold accumulator is the out parameter of the engine's hold time query, one dword past the
 * hero block, with exactly one naming site in the whole image: the push inside the input handler.
 * The anchor pattern that finds it, 83 C4 08 68 ?? ?? ?? ?? 6A 02 E8, matches once on every
 * build; the bare eight byte form matches thirteen times on every build, past the resolver's
 * eight candidate cap everywhere, which is why the anchor carries the three bytes in front.
 *
 * ======================================= What restores it ======================================
 *
 * The swap out writes back the exact values read at swap in, never assumptions about them. The
 * provocation then verifies both cells byte-identical and the digest of everything bank 0 owns
 * unchanged, once per substep, over real play. A mismatch names the first differing byte, because
 * a digest that only says "different" sends the next person hunting.
 *
 * ================================ One bank became three ========================================
 *
 * The state holds MP_BANK_FAR_MAX far banks and the index of the one that is active. Exactly one
 * can be: the engine has one player pointer and one hero block address, so a second window
 * inside a window is refused, as the single bank's was before.
 */
#include "mp_bank.h"

#include "mp_bank_spawn.h"
#include "mp_cells.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One far bank: a body's hero block, status record and hold accumulator, kept here between the
 * windows that place them in the engine. */
typedef struct mp_bank_far {
    uint8_t  block[MP_BANK_HERO_BLOCK_BYTES];
    uint8_t  status[MP_BANK_STATUS_BYTES];
    uint32_t accum;     /* the body's own hold accumulator; the cell sits one dword past the
                         * block, so a content swap of the block alone would let the body's input
                         * phase overwrite bank 0's value */
} mp_bank_far_t;

typedef struct mp_bank_state {
    bool      installed;
    bool      swapped;          /* the pointers aim at a bank */
    bool      running;          /* a bank's block is placed at the hero block for a tick */
    size_t    active;           /* which bank, 1..MP_BANK_FAR_MAX, while either is true */

    uintptr_t pr_cell;          /* the cell holding the player pointer */
    uintptr_t status_cell;      /* the cell holding the status record pointer */
    uintptr_t accum_cell;       /* the hold accumulator */
    uintptr_t hero_block;       /* where the player pointer aimed at install time */

    mp_bank_far_t far[MP_BANK_FAR_MAX];

    uint32_t  saved_pr;
    uint32_t  saved_status;
    uint32_t  saved_accum;
    bool      status_placed;    /* the tick put the bank's status content into the live record */
    bool      keep_armed;       /* a spawn's snapshot waits for the swap out */
    bool      fault_logged;

    uint32_t  cycles;
    uint32_t  digest_mismatches;
    uint32_t  restore_faults;
    uint32_t  skipped_ticks;
    bool      fault_detailed;   /* the first mismatch is dissected; the rest are counted */

    uint32_t  window_cells;     /* stores of a word or less that landed in engine data */
    uint32_t  window_blocks;    /* stores of more */
    uint32_t  window_refused;
} mp_bank_state_t;

static mp_bank_state_t bank;

/* ==============================================================================================
 * The two pure rules.
 * ============================================================================================ */

bool mp_bank_index_ok(size_t index)
{
    return index >= 1u && index <= MP_BANK_FAR_MAX;
}

int32_t mp_bank_class_of(size_t index)
{
    return mp_bank_index_ok(index) ? (int32_t)(MP_BANK_CLASS_FIRST - 1 + (int32_t)index) : 1;
}

static mp_bank_far_t *far_of(size_t index)
{
    return mp_bank_index_ok(index) ? &bank.far[index - 1u] : NULL;
}

static mp_bank_far_t *active_far(void)
{
    return (bank.swapped || bank.running) ? far_of(bank.active) : NULL;
}

uint32_t mp_bank_fnv1a(uint32_t seed, const void *bytes, size_t size)
{
    const uint8_t *at  = (const uint8_t *)bytes;
    uint32_t       sum = seed;
    size_t         index;

    for (index = 0; index < size; ++index) {
        sum ^= at[index];
        sum *= 16777619u;
    }
    return sum;
}

bool mp_bank_ready(void)
{
    return bank.installed;
}

/* Every store a window makes lands in engine data, never in code: the player and status pointer
 * cells, the hold accumulator and the hero block sit in the executable's writable data, the status
 * record in an array there, and a body's flag word in the object slab. So the store needs no
 * protection lifted and no instruction cache flushed, and the guarded store is all of it. The one
 * kind of page where it and the protecting write answer differently is a read only one, which is
 * why the installation proves the two pointer cells with the protecting write, once. */
bool mp_bank_window_write(uintptr_t at, const void *data, size_t size)
{
    if (!memory_try_write(at, data, size)) {
        ++bank.window_refused;
        return false;
    }
    if (size <= sizeof(uint32_t)) {
        ++bank.window_cells;
    } else {
        ++bank.window_blocks;
    }
    return true;
}

bool mp_bank_window_write_u32(uintptr_t at, uint32_t value)
{
    return mp_bank_window_write(at, &value, sizeof value);
}

bool mp_bank_window_write_f32(uintptr_t at, float value)
{
    return mp_bank_window_write(at, &value, sizeof value);
}

void mp_bank_window_report(void)
{
    log_info("the far bodies' windows: %u cell write(s) and %u block write(s) straight into the "
             "engine's data, %u refused (must be 0)",
             (unsigned)bank.window_cells, (unsigned)bank.window_blocks,
             (unsigned)bank.window_refused);
}

/* The one-time checks. Everything later assumes them, so everything later refuses quietly when
 * this did. */
bool mp_bank_install(void)
{
    uint32_t pr_value = 0;

    if (bank.installed) {
        return true;
    }

    bank.pr_cell     = mp_cells_address(MP_CELL_PR);
    bank.status_cell = mp_cells_address(MP_CELL_PLR_STATUS_POINTER);
    bank.accum_cell  = mp_cells_address(MP_CELL_ATTACK_HOLD_ACCUM);
    bank.hero_block  = mp_cells_address(MP_CELL_HERO_BLOCK);

    if (bank.pr_cell == 0 || bank.status_cell == 0 || bank.accum_cell == 0 ||
        bank.hero_block == 0) {
        log_warning("the bank cannot install: a cell it stands on did not resolve");
        return false;
    }

    /* The size constant belongs to the hero block. A player pointer aiming anywhere else means a
     * build or a moment this file does not understand, and copying 0x3AC bytes from an unknown
     * object would be a guess wearing a bounds check. */
    if (!memory_read_u32(bank.pr_cell, &pr_value) || (uintptr_t)pr_value != bank.hero_block) {
        log_warning("the bank cannot install: the player pointer holds %08X and the hero block "
                    "is %08X, so somebody else has already moved it or this build is not "
                    "understood", (unsigned)pr_value, (unsigned)bank.hero_block);
        return false;
    }

    /* Writability, proven once by writing the values that are already there. Both cells live in
     * the data section with characteristics 0xC0000040 (read, write, initialised) in all three
     * shipped builds: the player pointer cell in the section's raw portion, where the image
     * itself initialises it with the hero block address, and the status pointer cell past the
     * raw size in the virtual tail, so it loads zeroed. Neither needs a protection change;
     * patch_write_u32 is used anyway because it restores page protection and reports failure,
     * and after this the swap trusts the two cells. */
    if (patch_write_u32(bank.pr_cell, pr_value) != PATCH_RESULT_OK) {
        log_warning("the bank cannot install: the player pointer cell refused a write");
        return false;
    }
    {
        uint32_t status_value = 0;

        if (!memory_read_u32(bank.status_cell, &status_value) ||
            patch_write_u32(bank.status_cell, status_value) != PATCH_RESULT_OK) {
            log_warning("the bank cannot install: the status pointer cell refused a write");
            return false;
        }
    }

    bank.installed = true;
    log_info("the bank is installed: player pointer cell %08X, status pointer cell %08X, "
             "accumulator %08X, hero block %08X, %u far bank(s)",
             (unsigned)bank.pr_cell, (unsigned)bank.status_cell, (unsigned)bank.accum_cell,
             (unsigned)bank.hero_block, (unsigned)MP_BANK_FAR_MAX);
    return true;
}

/* What bank 0 owns, digested in one fixed order. The status record is followed through the live
 * pointer: a swap that left the pointer on our copy would change this value through the pointer
 * term even if every byte it aims at matched.
 *
 * Known holes, named on purpose. The pipeline also writes player adjacent state outside
 * everything this covers: the HUD ease cells at 0x6CFAF8 to 0x6CFB2C (display current and
 * target, written by the status helpers), the statistics counters at 0x872EC8, 0x872EE0 and
 * 0x872EF0, and the level outcome cell. None of them is touched by a swap cycle in which no
 * pipeline runs, so the provocation's claim is complete; a bank's tick has to decide for each
 * whether it is banked, shared or deliberately ignored. */
bool mp_bank_digest(uint32_t *digest_out)
{
    uint8_t  block[MP_BANK_HERO_BLOCK_BYTES];
    uint8_t  status[MP_BANK_STATUS_BYTES];
    uint32_t pr_value     = 0;
    uint32_t status_value = 0;
    uint32_t accum        = 0;
    uint32_t sum;

    if (!bank.installed || digest_out == NULL) {
        return false;
    }
    if (!memory_try_read_u32(bank.pr_cell, &pr_value) ||
        !memory_try_read_u32(bank.status_cell, &status_value) ||
        !memory_try_read_u32(bank.accum_cell, &accum)) {
        return false;
    }
    if (!memory_try_read(bank.hero_block, block, sizeof block)) {
        return false;
    }
    /* Outside a level the status pointer is NULL and a digest with no record term is still a
     * digest; the pointer value itself stays in. The cell has two writers, not one: the active
     * one inside the engine's active player setter (imul ecx, 0x4C / add ecx, base / mov [cell],
     * ecx) and a C7 05 <cell> 00000000 at 0x00458BB2, the new game arm clearing it. The second
     * never runs inside a substep, so it cannot race the swap, but a NULL there is a moment the
     * digest and the copy must survive, which is why the record term is skipped rather than
     * dereferenced. */
    if (status_value != 0) {
        if (!memory_try_read((uintptr_t)status_value, status, sizeof status)) {
            return false;
        }
    } else {
        memset(status, 0, sizeof status);
    }

    sum = mp_bank_fnv1a(MP_BANK_FNV_SEED, block, sizeof block);
    sum = mp_bank_fnv1a(sum, status, sizeof status);
    sum = mp_bank_fnv1a(sum, &accum, sizeof accum);
    sum = mp_bank_fnv1a(sum, &pr_value, sizeof pr_value);
    sum = mp_bank_fnv1a(sum, &status_value, sizeof status_value);

    *digest_out = sum;
    return true;
}

/* ==============================================================================================
 * The pointer swap.
 * ============================================================================================ */

/* The swap window is clean, measured rather than assumed. The scheduler's dispatch, the whole
 * cluster from 0x4755C0 to 0x475BE1, publishes the running record, delivers the wake flag before
 * the call, calls the record's entry at +0x14, and afterwards touches only its own bookkeeping
 * cells. The dwords of both pointer cells appear nowhere in that cluster, nor in the substep
 * frame around it, in any of the three shipped builds. With no second thread, nothing engine
 * side can observe the pointers while they are swapped inside our task tick.
 *
 * The two swap flavours share everything but the copy. The refreshing one is the provocation's
 * and the spawn's: the bank becomes a fresh copy of bank 0 before the pointers move, which is what
 * a digest comparison and a spawn-in-progress want. The persistent one is the contact route's: the
 * bank's own block and status are what the pointers must aim at, and refreshing them here would
 * overwrite that body's whole life with bank 0's. */
static bool swap_pointers_in(size_t index, bool refresh)
{
    mp_bank_far_t *far = far_of(index);

    if (!bank.installed || far == NULL || bank.swapped || bank.running) {
        return false;
    }
    if (!memory_try_read_u32(bank.pr_cell, &bank.saved_pr) ||
        !memory_try_read_u32(bank.status_cell, &bank.saved_status) ||
        !memory_try_read_u32(bank.accum_cell, &bank.saved_accum)) {
        return false;
    }
    if (refresh) {
        if (!memory_try_read((uintptr_t)bank.saved_pr, far->block, sizeof far->block)) {
            return false;
        }
        if (bank.saved_status != 0 &&
            !memory_try_read((uintptr_t)bank.saved_status, far->status, sizeof far->status)) {
            return false;
        }
    }

    if (!mp_bank_window_write_u32(bank.pr_cell, (uint32_t)(uintptr_t)far->block)) {
        return false;
    }
    if (!mp_bank_window_write_u32(bank.status_cell, (uint32_t)(uintptr_t)far->status)) {
        /* Half a swap is worse than none: the first store goes back before this reports. */
        mp_bank_window_write_u32(bank.pr_cell, bank.saved_pr);
        return false;
    }

    bank.swapped = true;
    bank.active  = index;
    return true;
}

bool mp_bank_swap_in_at(size_t index)
{
    return swap_pointers_in(index, true);
}

bool mp_bank_swap_in_persistent_at(size_t index)
{
    return swap_pointers_in(index, false);
}

/* The spawn's window: the refreshing swap with a snapshot of what the engine's hero spawn writes
 * outside the block (mp_bank_spawn.h). The snapshot is given back in the swap out, which the
 * caller runs after the spawn's last engine writer, so no caller can give it back too early. */
bool mp_bank_swap_in_for_spawn(size_t index, int32_t local_hero)
{
    if (!bank.installed || bank.swapped || bank.running) {
        return false;
    }
    if (!mp_bank_spawn_take(bank.status_cell, index, local_hero)) {
        return false;
    }
    if (!swap_pointers_in(index, true)) {
        return false;   /* nothing ran, so the snapshot is simply never given back */
    }
    bank.keep_armed = true;
    return true;
}

bool mp_bank_swap_out(void)
{
    bool ok = true;

    if (!bank.installed || !bank.swapped) {
        return false;
    }
    if (bank.keep_armed) {
        bank.keep_armed = false;
        mp_bank_spawn_give_back();
    }
    if (!mp_bank_window_write_u32(bank.pr_cell, bank.saved_pr)) {
        ok = false;
    }
    if (!mp_bank_window_write_u32(bank.status_cell, bank.saved_status)) {
        ok = false;
    }
    if (!mp_bank_window_write_u32(bank.accum_cell, bank.saved_accum)) {
        ok = false;
    }

    /* Swapped is cleared even on a failed restore: staying "in" would make the next cycle save
     * our own buffer addresses as the thing to restore to, which turns one fault into a state. */
    bank.swapped = false;
    bank.active  = 0u;
    return ok;
}

bool mp_bank_is_swapped(void)
{
    return bank.swapped;
}

size_t mp_bank_active(void)
{
    return (bank.swapped || bank.running) ? bank.active : 0u;
}

/* A bank is active in two different ways: swapped in through the pointer, or ticked through a
 * content swap of the block. A shot fired from inside either must carry that bank's class, so
 * both answer it; the swapped flag alone would have told the shot hull "bank 0" during the tick. */
int32_t mp_bank_active_class(void)
{
    return mp_bank_class_of(mp_bank_active());
}

/* ==============================================================================================
 * The block loan.
 * ============================================================================================ */

/* One scratch block for the loan. Static because a kilobyte does not belong on the stack of an
 * engine hook, and single because the loan cannot nest: the engine is single threaded and a
 * lifecycle function does not call another one. */
static uint8_t loan_scratch[MP_BANK_HERO_BLOCK_BYTES];

bool mp_bank_lend_block_begin(void)
{
    mp_bank_far_t *far = active_far();

    if (!bank.installed || !bank.swapped || far == NULL) {
        return false;
    }
    if (!memory_try_read(bank.hero_block, loan_scratch, sizeof loan_scratch)) {
        log_error("the block loan could not read bank 0's block, so the call runs unlent");
        return false;
    }
    if (!mp_bank_window_write(bank.hero_block, far->block, sizeof far->block)) {
        log_error("the block loan could not place the bank's bytes, so the call runs unlent");
        return false;
    }
    if (!mp_bank_window_write_u32(bank.pr_cell, (uint32_t)bank.hero_block)) {
        /* Half a loan is the exact confusion the loan exists to prevent, so it goes back. */
        mp_bank_window_write(bank.hero_block, loan_scratch, sizeof loan_scratch);
        log_error("the block loan could not move the player pointer, so the call runs unlent");
        return false;
    }
    return true;
}

void mp_bank_lend_block_end(bool lent)
{
    mp_bank_far_t *far = active_far();

    if (!lent || far == NULL) {
        return;
    }
    /* The callee's changes belong to the bank; then bank 0 gets its block and the pointer back. */
    if (!memory_try_read(bank.hero_block, far->block, sizeof far->block)) {
        log_error("the block loan could not read the callee's result back into the bank");
    }
    if (!mp_bank_window_write(bank.hero_block, loan_scratch, sizeof loan_scratch)) {
        log_error("the block loan could not put bank 0's block back, which bank 0 will notice");
    }
    if (!mp_bank_window_write_u32(bank.pr_cell, (uint32_t)(uintptr_t)far->block)) {
        log_error("the block loan could not point the player pointer back at the bank");
    }
}

/* ==============================================================================================
 * The tick of a bank's body.
 * ============================================================================================ */

/* A tick fault is logged once with the step that failed, then only counted by the caller. The
 * step matters: a fault before the pipeline ran leaves bank 0 untouched, a fault after it may have
 * left bank 0's block or accumulator holding the body's values, which the player would notice as
 * suddenly being somewhere else. */
static void log_tick_fault(size_t index, const char *step)
{
    if (bank.fault_logged) {
        return;
    }
    bank.fault_logged = true;
    log_error("bank %u's tick faulted while %s; later faults are counted, not logged",
              (unsigned)index, step);
}

/* Why the whole pipeline is safe to run under a content swap of the block. The phase runner at
 * 0x448297 reads exactly two globals, the player pointer and the constant phase table, and the
 * mode descriptor at pr+0x60:
 *
 *   00448297  8B 0D 20 52 4B 00       mov ecx, [0x4B5220]        pr
 *   0044829D  8B 51 60                mov edx, [ecx+0x60]        pr->pCurMode
 *   004482E3  FF 14 8D 28 52 4B 00    call [ecx*4+0x4B5228]      the phase table
 *
 * A per phase write census over the 247 functions reachable in the tick to depth six found every
 * absolute per player write in deeper callees, fired only by an event: the HUD ease cells at
 * 0x6CFAF8, FC, B00, B20 and B24 (damage, force regen, weapon switch), the statistics inc at
 * 0x4500B0 (0x872EC8, a death) and at 0x449047 (0x872EF0, the damage floor). None fires on a
 * plain movement tick of a living body at full health that is not switching weapon.
 *
 * Phase 12, the pose commit, reads obj = pr->hActor at pr+0x0C, sets obj->prevPos from obj->pos,
 * then obj->pos from pr->pos at pr+0x118, all relative to pr; with the block's content being the
 * body's, obj is the body's own actor, so its position is what moves. Phase 10, the collision
 * resolve at 0x44C207, calls the cylinder push at 0x4131EB on pr->hActor against pr->desiredPos
 * at pr+0x124, so the body is pushed against the level and other bodies by its own tick and no
 * separate physics poke is needed.
 *
 * Two accepted effects, byte sourced, not crashes. Phase 11, the ground publish at 0x44CC2F,
 * feeds the camera target from the ticking bank unconditionally, through a pointer rather than
 * an absolute cell this file could bank, so a bank tick pulls the shared camera onto that body.
 * And a contact struck during the tick runs the contact handler at 0x448369 inline through the
 * task runner at 0x475953, which saves and restores the current task cell at 0x868724 so the
 * scheduler is not corrupted; that handler lays the knockback on pr (0x4483B8: mov eax,
 * [0x4B5220]), the active bank, not the struck body. */
bool mp_bank_run_at(size_t index, void (*run_phases)(void))
{
    mp_bank_far_t *far = far_of(index);
    uint8_t  status_scratch[MP_BANK_STATUS_BYTES];
    uint32_t bank0_accum = 0;
    uint32_t live_status = 0;
    bool     status_banked = false;
    bool     ok = true;

    if (!bank.installed || far == NULL || run_phases == NULL) {
        return false;
    }
    if (bank.swapped || bank.running) {
        return false;
    }

    /* Set bank 0's block aside and put the body's persistent block at the hero block, the absolute
     * address the unswapped player pointer already aims at. The pipeline then reads and writes
     * that body for one call. Reusing the loan scratch is safe: this is not a loan inside a loan,
     * it is the same "bank 0 steps aside for one borrowed call" that the loan is, and the engine
     * is single threaded so the spawn's loan has long returned. */
    if (!memory_try_read(bank.hero_block, loan_scratch, sizeof loan_scratch) ||
        !memory_try_read_u32(bank.accum_cell, &bank0_accum)) {
        log_tick_fault(index, "reading bank 0's block, so nothing was changed");
        return false;
    }
    if (!mp_bank_window_write(bank.hero_block, far->block, sizeof far->block)) {
        log_tick_fault(index, "placing the body's block, so nothing was changed");
        return false;
    }
    /* The accumulator is written by the input phase from inside the pipeline, so it is the ticking
     * body's state for the duration; the body keeps its own value across ticks. Found by review,
     * not by the census: the first build of this function swapped the 0x3AC bytes only, so the
     * body's phase 6 would have overwritten bank 0's accumulator every substep while the file's
     * own header promised the opposite. */
    if (!mp_bank_window_write_u32(bank.accum_cell, far->accum)) {
        mp_bank_window_write(bank.hero_block, loan_scratch, sizeof loan_scratch);
        log_tick_fault(index, "placing the body's accumulator, so the block went back");
        return false;
    }

    /* The status record's CONTENT goes with the block: the status pointer itself stays bank 0's,
     * but the bodies share one hero index and therefore one record address, so a content swap is
     * what makes a health read inside the walk, the death check above all, see the body's own
     * value. Outside a level the pointer is null and the walk runs without a record, which is an
     * answer and not a fault. */
    if (memory_try_read_u32(bank.status_cell, &live_status) && live_status != 0 &&
        memory_try_read((uintptr_t)live_status, status_scratch, sizeof status_scratch) &&
        mp_bank_window_write((uintptr_t)live_status, far->status, sizeof far->status)) {
        status_banked = true;
    }

    bank.running       = true;
    bank.active        = index;
    bank.status_placed = status_banked;
    run_phases();
    bank.running       = false;
    bank.active        = 0u;
    bank.status_placed = false;

    /* The pipeline's changes are the body's next state; carry them into the block and the banked
     * accumulator, then bank 0 gets both of its own back at the absolute addresses. */
    if (!memory_try_read(bank.hero_block, far->block, sizeof far->block) ||
        !memory_try_read_u32(bank.accum_cell, &far->accum)) {
        log_tick_fault(index, "reading the pipeline's result back into the bank");
        ok = false;
    }
    if (!mp_bank_window_write(bank.hero_block, loan_scratch, sizeof loan_scratch)) {
        log_tick_fault(index, "restoring bank 0's block, which bank 0 will notice");
        ok = false;
    }
    if (!mp_bank_window_write_u32(bank.accum_cell, bank0_accum)) {
        log_tick_fault(index, "restoring bank 0's accumulator");
        ok = false;
    }
    if (status_banked) {
        if (!memory_try_read((uintptr_t)live_status, far->status, sizeof far->status)) {
            log_tick_fault(index, "reading the walk's status back into the bank");
            ok = false;
        }
        if (!mp_bank_window_write((uintptr_t)live_status, status_scratch, sizeof status_scratch)) {
            log_tick_fault(index, "restoring bank 0's status record, which bank 0 will notice");
            ok = false;
        }
    }
    return ok;
}

/* ==============================================================================================
 * Reading and writing a bank between windows.
 * ============================================================================================ */

/* The first dword of the status record is the health, which is what the death check reads: that
 * phase loads the record pointer and dereferences it with no offset, which is where the layout
 * claim comes from. Only meaningful once a spawn has filled the bank; a process with no bank
 * answers zero. */
int32_t mp_bank_health_at(size_t index)
{
    mp_bank_far_t *far = far_of(index);
    int32_t        health = 0;

    if (!bank.installed || far == NULL) {
        return 0;
    }
    memcpy(&health, far->status, sizeof health);
    return health;
}

uintptr_t mp_bank_block_at(size_t index)
{
    const mp_bank_far_t *far = far_of(index);

    return far != NULL ? (uintptr_t)far->block : 0u;
}

bool mp_bank_read_at(size_t index, size_t offset, void *out, size_t size)
{
    mp_bank_far_t *far = far_of(index);

    if (!bank.installed || far == NULL || out == NULL || size == 0 ||
        offset > sizeof far->block || size > sizeof far->block - offset) {
        return false;
    }
    memcpy(out, far->block + offset, size);
    return true;
}

/* While this bank's window is open its block has been placed into the engine's record and is read
 * back from there when the window closes, so a write to the copy would be read over without a
 * word. Anything a window wants changed is written into the record it installed. */
bool mp_bank_write_at(size_t index, size_t offset, const void *data, size_t size)
{
    mp_bank_far_t *far = far_of(index);

    if (!bank.installed || far == NULL || data == NULL || size == 0 || mp_bank_is_swapped() ||
        offset > sizeof far->block || size > sizeof far->block - offset) {
        return false;
    }
    if (bank.running && bank.active == index) {
        return false;
    }
    memcpy(far->block + offset, data, size);
    return true;
}

bool mp_bank_status_offset_ok(size_t offset)
{
    return offset % sizeof(uint32_t) == 0u &&
           offset <= (size_t)MP_BANK_STATUS_BYTES - sizeof(uint32_t);
}

/* Where the record's content is decides where the write goes. Swapped in through the pointers,
 * the bank's copy IS the live record. Inside that bank's tick the content has been placed into
 * the engine's record and is read back afterwards, so the copy is the wrong place for the
 * duration. Otherwise the copy is current and the next window carries the write in.
 *
 * The engine's own setters were refused for this. The immediate one at 0x00459EA9 writes the
 * value and the two HUD cells of the local player's bar, absolute cells, so a call inside the
 * puppet's window blinks the local HUD with the far player's health; the plain one at 0x00459E85
 * does not clamp, and the wire's byte is clamped at its encoder instead. So the writer is one
 * dword store. */
bool mp_bank_status_write_at(size_t index, size_t offset, uint32_t value)
{
    mp_bank_far_t *far = far_of(index);

    if (!bank.installed || far == NULL || !mp_bank_status_offset_ok(offset)) {
        return false;
    }
    if (bank.running) {
        uint32_t live = 0;

        if (bank.active != index || !bank.status_placed ||
            !memory_try_read_u32(bank.status_cell, &live) || live == 0) {
            return false;
        }
        return mp_bank_window_write_u32((uintptr_t)live + offset, value);
    }
    memcpy(far->status + offset, &value, sizeof value);
    return true;
}

/* ==============================================================================================
 * The names that mean bank 1.
 * ============================================================================================ */

bool mp_bank_swap_in(void)
{
    return mp_bank_swap_in_at(1u);
}

bool mp_bank_swap_in_persistent(void)
{
    return mp_bank_swap_in_persistent_at(1u);
}

int32_t mp_bank_second_health(void)
{
    return mp_bank_health_at(1u);
}

bool mp_bank_second_write(size_t offset, const void *data, size_t size)
{
    return mp_bank_write_at(1u, offset, data, size);
}

bool mp_bank_run_second(void (*run_phases)(void))
{
    return mp_bank_run_at(1u, run_phases);
}

/* ==============================================================================================
 * The provocation.
 * ============================================================================================ */

/* Name the first byte that differs between what the block was and what it is, because the digest
 * only says that one exists. The copies bank 1 took at swap in are exactly the before image. */
static void dissect_mismatch(void)
{
    const mp_bank_far_t *far = far_of(1u);
    uint8_t now[MP_BANK_HERO_BLOCK_BYTES];
    size_t  index;

    if (bank.fault_detailed || far == NULL) {
        return;
    }
    bank.fault_detailed = true;

    if (!memory_try_read(bank.hero_block, now, sizeof now)) {
        log_error("  and the hero block is no longer readable, which is its own finding");
        return;
    }
    for (index = 0; index < sizeof now; ++index) {
        if (now[index] != far->block[index]) {
            log_error("  first difference inside the hero block at +%#05x: %02X was %02X",
                      (unsigned)index, now[index], far->block[index]);
            return;
        }
    }
    log_error("  the hero block is intact, so the difference is in the record, the accumulator "
              "or a pointer value");
}

void mp_bank_provoke_tick(void)
{
    const mp_bank_far_t *far = far_of(1u);
    uint32_t before = 0;
    uint32_t after  = 0;
    uint32_t value  = 0;

    if (!bank.installed || far == NULL) {
        ++bank.skipped_ticks;
        return;
    }

    if (!mp_bank_digest(&before)) {
        ++bank.skipped_ticks;   /* a moment the state is not readable is skipped, not failed */
        return;
    }

    if (!mp_bank_swap_in_at(1u)) {
        ++bank.skipped_ticks;
        return;
    }

    if (!memory_try_read_u32(bank.pr_cell, &value) || value != (uint32_t)(uintptr_t)far->block) {
        ++bank.restore_faults;
        log_error("the swap in did not take: the player pointer reads %08X", (unsigned)value);
    }
    if (!memory_try_read_u32(bank.status_cell, &value) ||
        value != (uint32_t)(uintptr_t)far->status) {
        ++bank.restore_faults;
        log_error("the swap in did not take: the status pointer reads %08X", (unsigned)value);
    }

    if (!mp_bank_swap_out()) {
        ++bank.restore_faults;
        log_error("the swap out reported a failed restore on cycle %u", (unsigned)bank.cycles);
    }

    if (!memory_try_read_u32(bank.pr_cell, &value) || value != bank.saved_pr) {
        ++bank.restore_faults;
        log_error("the player pointer did not come back: %08X against %08X",
                  (unsigned)value, (unsigned)bank.saved_pr);
    }
    if (!memory_try_read_u32(bank.status_cell, &value) || value != bank.saved_status) {
        ++bank.restore_faults;
        log_error("the status pointer did not come back: %08X against %08X",
                  (unsigned)value, (unsigned)bank.saved_status);
    }

    if (mp_bank_digest(&after) && after != before) {
        ++bank.digest_mismatches;
        if (bank.digest_mismatches <= 4u) {
            log_error("the bank swap left a trace on cycle %u: digest %08X against %08X",
                      (unsigned)bank.cycles, (unsigned)after, (unsigned)before);
            dissect_mismatch();
        }
    }

    ++bank.cycles;
}

void mp_bank_report(const char *why)
{
    if (!bank.installed) {
        log_warning("the bank at %s: never installed, so no swap has ever run", why);
        return;
    }
    log_info("the bank swap provocation at %s: %u cycles, %u digest mismatches, %u pointer "
             "faults, %u ticks skipped outside readable moments",
             why, (unsigned)bank.cycles, (unsigned)bank.digest_mismatches,
             (unsigned)bank.restore_faults, (unsigned)bank.skipped_ticks);
    if (bank.cycles != 0 && bank.digest_mismatches == 0 && bank.restore_faults == 0) {
        log_info("  every cycle came back bit-identical, which is the claim this post makes");
    }
    mp_bank_spawn_report();
}
