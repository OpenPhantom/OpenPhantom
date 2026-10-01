/* The one write of a session's pause menu: the key hook's call of sys_pause, repointed while a
 * transport stands and put back when it comes down, each time only after the call proves to be what
 * it should be.
 *
 * Played against two call sites in this test's own image, because the proof reads a call's target
 * and refuses one outside the image: the key hook's call of a sys_pause that is a function of this
 * file, and sys_pause's call of its menu. Everything else the binding reads is answered here as not
 * resolved, which costs the pump and the close reasons their lines and nothing the arming needs.
 * Nothing is ever called through the sites; only their bytes are read and written.
 */
#include "unittest.h"

#include "mp_cells.h"
#include "mp_cutscene.h"
#include "mp_input.h"
#include "mp_pause.h"
#include "mp_signatures.h"
#include "mp_signatures_pause.h"
#include "mp_task.h"

#include "common/host_image.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CALL_BYTES 5u

/* Each entry does something of its own, or the linker folds identical bodies into one address. */
static volatile uint32_t entered[3];

static void __cdecl fake_sys_pause(void)
{
    ++entered[0];
}

static int32_t __cdecl fake_pause_menu(void)
{
    ++entered[1];
    return 0;
}

/* Another module's entry, which the key hook's call may be pointed at between two sessions. */
static void __cdecl somebody_elses_pause(void)
{
    ++entered[2];
}

/* The two call sites, and the three cells sys_pause writes. */
static uint8_t  key_hook_call[8];
static uint8_t  menu_call[8];
static uint32_t gate_cell;
static uint32_t outcome_cell;
static uint32_t restore_cell;

static void write_call(uint8_t *site, const void *target)
{
    uint32_t displacement = (uint32_t)((uintptr_t)target - ((uintptr_t)site + CALL_BYTES));

    site[0] = 0xE8u;
    memcpy(site + 1, &displacement, sizeof displacement);
}

static uintptr_t lands_at(const uint8_t *site)
{
    uint32_t displacement;

    memcpy(&displacement, site + 1, sizeof displacement);
    return (uintptr_t)site + CALL_BYTES + displacement;
}

/* ---- what the binding asks for -------------------------------------------------------------- */

size_t mp_signatures_pause_menu_resolve(void)
{
    return 2u;
}

uintptr_t mp_signatures_pause_call(mp_pause_call_t call)
{
    switch (call) {
    case MP_PAUSE_CALL_PAUSE: return (uintptr_t)key_hook_call;
    case MP_PAUSE_CALL_MENU:  return (uintptr_t)menu_call;
    default:                  return 0u;
    }
}

uintptr_t mp_signatures_pause_address(mp_pause_site_t site)
{
    return site == MP_PAUSE_SITE_SYS_PAUSE ? (uintptr_t)&fake_sys_pause : 0u;
}

uintptr_t mp_signatures_pause_cell(mp_pause_cell_t cell)
{
    switch (cell) {
    case MP_PAUSE_CELL_SIM_GATE: return (uintptr_t)&gate_cell;
    case MP_PAUSE_CELL_OUTCOME:  return (uintptr_t)&outcome_cell;
    case MP_PAUSE_CELL_RESTORE:  return (uintptr_t)&restore_cell;
    default:                     return 0u;
    }
}

uintptr_t mp_signatures_address(mp_site_t site)
{
    (void)site;
    return 0u;
}

uintptr_t mp_cells_address(mp_cell_t cell)
{
    (void)cell;
    return 0u;
}

uintptr_t mp_cutscene_lock_level_cell(void)
{
    return 0u;
}

bool mp_input_installed(void)
{
    return true;
}

uint32_t mp_task_ticks(void)
{
    return 0u;
}

/* ============================================================================================== */

static void check_the_call_is_repointed_and_put_back(void)
{
    uint8_t   original[CALL_BYTES];
    uintptr_t entry;

    ut_section("while a transport stands the call is repointed, and put back byte for byte");
    write_call(key_hook_call, (const void *)&fake_sys_pause);
    write_call(menu_call, (const void *)&fake_pause_menu);
    memcpy(original, key_hook_call, sizeof original);

    ut_check(mp_pause_arm(), "the arming binds the sites and repoints the call");
    entry = lands_at(key_hook_call);
    ut_check(key_hook_call[0] == 0xE8u && entry != (uintptr_t)&fake_sys_pause,
             "the call is still a call, and no longer lands on sys_pause");
    ut_check(entry >= host_image_base() && entry < host_image_end(),
             "it lands on the feature's entry, inside the image");
    ut_check(mp_pause_arm() && lands_at(key_hook_call) == entry,
             "a second arming writes nothing");

    mp_pause_disarm();
    ut_check(memcmp(key_hook_call, original, sizeof original) == 0,
             "the disarming puts the five bytes back as they were");
    mp_pause_disarm();
    ut_check(memcmp(key_hook_call, original, sizeof original) == 0,
             "and a second one writes nothing");
}

static void check_a_call_somebody_else_holds_is_left_alone(void)
{
    ut_section("identified before it is replaced, in both directions");

    ut_check((uintptr_t)&somebody_elses_pause != (uintptr_t)&fake_sys_pause,
             "the other module's entry is a function of its own");
    write_call(key_hook_call, (const void *)&somebody_elses_pause);
    ut_check(!mp_pause_arm(), "a call that lands on another module's entry is not repointed");
    ut_check(lands_at(key_hook_call) == (uintptr_t)&somebody_elses_pause,
             "and is left pointing where that module put it");

    write_call(key_hook_call, (const void *)&fake_sys_pause);
    ut_check(mp_pause_arm(), "back on sys_pause, the call is repointed again");
    write_call(key_hook_call, (const void *)&somebody_elses_pause);
    mp_pause_disarm();
    ut_check(lands_at(key_hook_call) == (uintptr_t)&somebody_elses_pause,
             "a call another module repointed meanwhile is not put back over its write");

    write_call(key_hook_call, (const void *)&fake_sys_pause);
    mp_pause_report();
    ut_check(true, "and the report runs");
}

int main(void)
{
    ut_check(host_image_resolve(), "this test's own image stands in for the engine's");
    check_the_call_is_repointed_and_put_back();
    check_a_call_somebody_else_holds_is_left_alone();
    return ut_summary("mp_pause_arm");
}
