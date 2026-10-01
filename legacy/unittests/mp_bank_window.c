/* mp_bank_window.c: a far body's window into the engine's data, against the window it was before
 * its stores stopped going through the protecting write.
 *
 * The window puts a far body's block at the hero block, its hold accumulator in the cell and its
 * status into the live record, runs the player pipeline, and carries everything back. Every one of
 * those stores went through patch_write, two protection changes and an instruction cache flush
 * each, thirteen a substep for every far body; they go through the guarded store now. That is only
 * a swap if the window leaves the same bytes behind, so the window as it was is kept below word for
 * word as the reference and both are run over the same engine data made up in this process: the
 * hero block, the three cells, the status record, a far body with a block of its own, and a
 * pipeline that changes all of them and writes the far player's health from inside.
 *
 * Then the one kind of page where the two stores differ, a read only one: the protecting write
 * lifts the protection and lands, the guarded store is refused and counted. That difference is why
 * the installation proves the two pointer cells with the protecting write and nothing else does.
 *
 * The engine side of the cell table is played here: the player pointer, the status pointer, the
 * hold accumulator and the hero block are the only cells the bank asks for.
 */
#include "unittest.h"

#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_signatures.h"

#include "common/memory.h"
#include "common/patch.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The engine's data, and its cell table.
 * ============================================================================================ */

typedef struct engine {
    uint32_t pr;                                   /* aims at the hero block */
    uint32_t status_pointer;                       /* aims at the status record */
    uint32_t accum;
    uint8_t  hero_block[MP_BANK_HERO_BLOCK_BYTES];
    uint8_t  status[MP_BANK_STATUS_BYTES];
} engine_t;

static engine_t engine;

uintptr_t mp_cells_address(mp_cell_t cell)
{
    switch (cell) {
    case MP_CELL_PR:
        return (uintptr_t)&engine.pr;
    case MP_CELL_PLR_STATUS_POINTER:
        return (uintptr_t)&engine.status_pointer;
    case MP_CELL_ATTACK_HOLD_ACCUM:
        return (uintptr_t)&engine.accum;
    case MP_CELL_HERO_BLOCK:
        return (uintptr_t)engine.hero_block;
    default:
        return 0u;
    }
}

uintptr_t mp_signatures_address(mp_site_t site)
{
    (void)site;
    return 0u;
}

static void fill_engine(void)
{
    size_t i;

    for (i = 0; i < sizeof engine.hero_block; ++i) {
        engine.hero_block[i] = (uint8_t)(i * 7u + 1u);
    }
    for (i = 0; i < sizeof engine.status; ++i) {
        engine.status[i] = (uint8_t)(0xA0u + i);
    }
    engine.pr             = (uint32_t)(uintptr_t)engine.hero_block;
    engine.status_pointer = (uint32_t)(uintptr_t)engine.status;
    engine.accum          = 0x11223344u;
}

/* ==============================================================================================
 * The reference: the tick of a bank's body as mp_bank.c had it, word for word, over a bank of its
 * own and the same engine data. One name is changed: windows.h defines `far` away to nothing, so
 * the module's local of that name is `far_bank` here.
 * ============================================================================================ */

typedef struct reference_far {
    uint8_t  block[MP_BANK_HERO_BLOCK_BYTES];
    uint8_t  status[MP_BANK_STATUS_BYTES];
    uint32_t accum;
} reference_far_t;

typedef struct reference_bank {
    bool      installed;
    bool      swapped;
    bool      running;
    size_t    active;
    uintptr_t status_cell;
    uintptr_t accum_cell;
    uintptr_t hero_block;
    reference_far_t far_bank[MP_BANK_FAR_MAX];
    bool      status_placed;
    bool      fault_logged;
} reference_bank_t;

static reference_bank_t bank;
static uint8_t          loan_scratch[MP_BANK_HERO_BLOCK_BYTES];

static reference_far_t *far_of(size_t index)
{
    return mp_bank_index_ok(index) ? &bank.far_bank[index - 1u] : NULL;
}

static void log_tick_fault(size_t index, const char *step)
{
    (void)index;
    (void)step;
    bank.fault_logged = true;
}

static bool reference_run_at(size_t index, void (*run_phases)(void))
{
    reference_far_t *far_bank = far_of(index);
    uint8_t  status_scratch[MP_BANK_STATUS_BYTES];
    uint32_t bank0_accum = 0;
    uint32_t live_status = 0;
    bool     status_banked = false;
    bool     ok = true;

    if (!bank.installed || far_bank == NULL || run_phases == NULL) {
        return false;
    }
    if (bank.swapped || bank.running) {
        return false;
    }

    if (!memory_try_read(bank.hero_block, loan_scratch, sizeof loan_scratch) ||
        !memory_read_u32(bank.accum_cell, &bank0_accum)) {
        log_tick_fault(index, "reading bank 0's block, so nothing was changed");
        return false;
    }
    if (patch_write_bytes(bank.hero_block, far_bank->block, sizeof far_bank->block)
            != PATCH_RESULT_OK) {
        log_tick_fault(index, "placing the body's block, so nothing was changed");
        return false;
    }
    if (patch_write_u32(bank.accum_cell, far_bank->accum) != PATCH_RESULT_OK) {
        patch_write_bytes(bank.hero_block, loan_scratch, sizeof loan_scratch);
        log_tick_fault(index, "placing the body's accumulator, so the block went back");
        return false;
    }

    if (memory_read_u32(bank.status_cell, &live_status) && live_status != 0 &&
        memory_try_read((uintptr_t)live_status, status_scratch, sizeof status_scratch) &&
        patch_write_bytes((uintptr_t)live_status, far_bank->status, sizeof far_bank->status)
            == PATCH_RESULT_OK) {
        status_banked = true;
    }

    bank.running       = true;
    bank.active        = index;
    bank.status_placed = status_banked;
    run_phases();
    bank.running       = false;
    bank.active        = 0u;
    bank.status_placed = false;

    if (!memory_try_read(bank.hero_block, far_bank->block, sizeof far_bank->block) ||
        !memory_read_u32(bank.accum_cell, &far_bank->accum)) {
        log_tick_fault(index, "reading the pipeline's result back into the bank");
        ok = false;
    }
    if (patch_write_bytes(bank.hero_block, loan_scratch, sizeof loan_scratch) != PATCH_RESULT_OK) {
        log_tick_fault(index, "restoring bank 0's block, which bank 0 will notice");
        ok = false;
    }
    if (patch_write_u32(bank.accum_cell, bank0_accum) != PATCH_RESULT_OK) {
        log_tick_fault(index, "restoring bank 0's accumulator");
        ok = false;
    }
    if (status_banked) {
        if (!memory_try_read((uintptr_t)live_status, far_bank->status, sizeof far_bank->status)) {
            log_tick_fault(index, "reading the walk's status back into the bank");
            ok = false;
        }
        if (patch_write_bytes((uintptr_t)live_status, status_scratch, sizeof status_scratch)
                != PATCH_RESULT_OK) {
            log_tick_fault(index, "restoring bank 0's status record, which bank 0 will notice");
            ok = false;
        }
    }
    return ok;
}

static bool reference_status_write_at(size_t index, size_t offset, uint32_t value)
{
    reference_far_t *far_bank = far_of(index);

    if (!bank.installed || far_bank == NULL || !mp_bank_status_offset_ok(offset)) {
        return false;
    }
    if (bank.running) {
        uint32_t live = 0;

        if (bank.active != index || !bank.status_placed ||
            !memory_read_u32(bank.status_cell, &live) || live == 0) {
            return false;
        }
        return patch_write_u32((uintptr_t)live + offset, value) == PATCH_RESULT_OK;
    }
    memcpy(far_bank->status + offset, &value, sizeof value);
    return true;
}

/* ==============================================================================================
 * The pipeline both windows run: it moves the body, spends the accumulator and hurts it, the last
 * through the status writer each window has, as a health from the wire would arrive.
 * ============================================================================================ */

static bool (*status_writer)(size_t index, size_t offset, uint32_t value);
static bool   status_written;

static void pipeline(void)
{
    size_t i;

    for (i = 0x10u; i < 0x40u; ++i) {
        engine.hero_block[i] ^= 0x5Au;
    }
    engine.hero_block[MP_BANK_HERO_BLOCK_BYTES - 1u] = 0xEEu;
    engine.accum += 3u;
    engine.status[8] = 0x42u;
    status_written = status_writer(1u, 0u, 61u);
}

typedef struct after {
    engine_t engine;
    uint8_t  far_block[MP_BANK_HERO_BLOCK_BYTES];
    int32_t  far_health;
    bool     ran;
    bool     wrote;
} after_t;

static void far_block_of(uint8_t *block)
{
    size_t i;

    for (i = 0; i < MP_BANK_HERO_BLOCK_BYTES; ++i) {
        block[i] = (uint8_t)(0xF0u - i);
    }
}

static void run_swapped(after_t *out)
{
    uint8_t block[MP_BANK_HERO_BLOCK_BYTES];

    fill_engine();
    far_block_of(block);
    (void)mp_bank_write_at(1u, 0u, block, sizeof block);
    (void)mp_bank_status_write_at(1u, 0u, 90u);
    status_writer = &mp_bank_status_write_at;
    out->ran      = mp_bank_run_at(1u, &pipeline);
    out->wrote    = status_written;
    out->engine   = engine;
    (void)mp_bank_read_at(1u, 0u, out->far_block, sizeof out->far_block);
    out->far_health = mp_bank_health_at(1u);
}

static void run_reference(after_t *out)
{
    reference_far_t *far_bank = far_of(1u);
    int32_t          health;

    fill_engine();
    far_block_of(far_bank->block);
    (void)reference_status_write_at(1u, 0u, 90u);
    status_writer = &reference_status_write_at;
    out->ran      = reference_run_at(1u, &pipeline);
    out->wrote    = status_written;
    out->engine   = engine;
    memcpy(out->far_block, far_bank->block, sizeof out->far_block);
    memcpy(&health, far_bank->status, sizeof health);
    out->far_health = health;
}

static void check_the_window(void)
{
    after_t swapped;
    after_t reference;

    ut_section("one far body's window, the guarded stores against the protecting ones");
    fill_engine();
    ut_check(mp_bank_install(), "the bank installs on the engine data made up here");
    bank.installed   = true;
    bank.status_cell = (uintptr_t)&engine.status_pointer;
    bank.accum_cell  = (uintptr_t)&engine.accum;
    bank.hero_block  = (uintptr_t)engine.hero_block;

    memset(&swapped, 0, sizeof swapped);
    memset(&reference, 0, sizeof reference);
    run_swapped(&swapped);
    run_reference(&reference);

    ut_check(swapped.ran && reference.ran, "both windows ran");
    ut_check(swapped.wrote && reference.wrote,
             "and the health from the wire was written inside both, into the live record");
    ut_check(memcmp(swapped.engine.hero_block, reference.engine.hero_block,
                    sizeof swapped.engine.hero_block) == 0,
             "bank 0's block is back, the same bytes as the reference leaves");
    ut_check(swapped.engine.accum == reference.engine.accum &&
                 swapped.engine.accum == 0x11223344u,
             "and so is bank 0's accumulator");
    ut_check(memcmp(swapped.engine.status, reference.engine.status,
                    sizeof swapped.engine.status) == 0,
             "and bank 0's status record");
    ut_check(swapped.engine.pr == reference.engine.pr &&
                 swapped.engine.status_pointer == reference.engine.status_pointer,
             "and neither pointer moved");
    ut_check(memcmp(swapped.far_block, reference.far_block, sizeof swapped.far_block) == 0,
             "the far body kept what the pipeline did to it, as in the reference");
    ut_check(swapped.far_health == 61 && reference.far_health == 61,
             "and its health is the one written inside the window");

    run_swapped(&swapped);
    run_reference(&reference);
    ut_check(memcmp(&swapped.engine, &reference.engine, sizeof swapped.engine) == 0 &&
                 memcmp(swapped.far_block, reference.far_block, sizeof swapped.far_block) == 0,
             "a second window, as the next substep runs it, leaves the same again");
}

static void check_the_swap(void)
{
    uint32_t pr = 0;
    uint32_t status = 0;

    ut_section("the pointer swap and back");
    fill_engine();
    ut_check(mp_bank_swap_in_at(1u), "the pointers move to the far bank");
    ut_check(engine.pr == (uint32_t)mp_bank_block_at(1u), "the player pointer aims at its block");
    ut_check(mp_bank_swap_out(), "and come back");
    pr     = engine.pr;
    status = engine.status_pointer;
    ut_check(pr == (uint32_t)(uintptr_t)engine.hero_block &&
                 status == (uint32_t)(uintptr_t)engine.status && engine.accum == 0x11223344u,
             "to exactly where they were, and the accumulator with them");
}

static void check_the_read_only_page(void)
{
    SYSTEM_INFO info;
    uint8_t    *page;
    DWORD       previous;
    uint32_t    word = 0;

    ut_section("a read only page: the one difference, and why the installation keeps it");
    GetSystemInfo(&info);
    page = (uint8_t *)VirtualAlloc(NULL, info.dwPageSize, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_READWRITE);
    if (page == NULL || !VirtualProtect(page, info.dwPageSize, PAGE_READONLY, &previous)) {
        ut_check(0, "a read only page was made (prerequisite)");
        return;
    }
    ut_check(patch_write_u32((uintptr_t)page, 0x77u) == PATCH_RESULT_OK,
             "the protecting write lifts the protection and lands");
    ut_check(memory_try_read_u32((uintptr_t)page, &word) && word == 0x77u, "the word is there");
    ut_check(!mp_bank_window_write_u32((uintptr_t)page + 4u, 0x88u),
             "the guarded store is refused, where the old window would have written");
    ut_check(memory_try_read_u32((uintptr_t)page + 4u, &word) && word == 0u,
             "and nothing was written");
    (void)VirtualFree(page, 0, MEM_RELEASE);
}

int main(void)
{
    memory_watch_arm(false, true);
    check_the_window();
    check_the_swap();
    check_the_read_only_page();
    return ut_summary("a far body's window into the engine's data");
}
