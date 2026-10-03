/* mp_range_gate.c: an enemy's range is measured against the nearest player. See the header. */
#include "mp_range_gate.h"

#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_range_gate_rule.h"
#include "mp_scene_claim_rule.h"
#include "mp_signatures.h"
#include "mp_stopwatch.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"

#include <intrin.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The engine's own test, `within_range`. Three axes squared against a squared radius, strictly
 * less than, and it returns an int the two callers read as a flag. */
typedef int(__cdecl *within_range_fn_t)(const float at[3], const float player[3], float radius);

/* How many callers the census must find. Not a limit and not a guess: it is the assertion. */
#define EXPECTED_CALLERS 2u

/* Which of the two return addresses is the activation scan's. The census hands them over in
 * ascending order, and the scan lies above the entity loop in this build; the install line names
 * the two by the same index. */
#define SCAN_CALLER 1u

/* A near call's displacement is the four bytes in front of its return address. */
#define CALL_DISPLACEMENT_BYTES 4u

typedef struct range_gate_state {
    detour_t          hull;
    within_range_fn_t original;
    uintptr_t         target;                     /* the range test itself */
    uint32_t          callers[EXPECTED_CALLERS];   /* return addresses, not call addresses */
    size_t            caller_count;

    mp_range_gate_far_body_fn_t far_body;
    mp_range_gate_players_t     players;

    bool installed;
    bool host;
    bool armed;

    uint32_t asked;            /* every call, which is the denominator for all of the below */
    uint32_t engine_said_yes;  /* the engine's own answer stood, nothing was widened */
    uint32_t widened;          /* the engine said no and a far player said yes */
    uint32_t foreign_caller;   /* a call from neither of the two sites; must stay 0 */
    uint32_t refreshes;        /* one a substep, which makes it the clock of the wakings below */
    uint32_t bodies_seen;      /* far bodies copied over all refreshes */

    /* Which far player the activation scan was last answered yes for, by placement record. */
    mp_scene_woke_t woke;
} range_gate_state_t;

static range_gate_state_t gate;

/* ==============================================================================================
 * The caller census, repeated on the running image.
 *
 * The bytes come from host_image_read_original, not from live memory: another module may have
 * written a branch over a caller by the time this runs, and a detoured caller is still a caller.
 *
 * The section is read in ONE piece and searched in memory. It used to be read a five byte window
 * at a time, which is an open, a seek, a read and a close of the executable for every one of some
 * 680 thousand offsets, and the host's menu stood still for eleven seconds at "On to the lobby".
 * A failed read or allocation is not remembered: it says nothing about the file, and the next
 * arming asks again.
 * ============================================================================================ */

typedef enum census_outcome {
    CENSUS_COUNTED,
    CENSUS_NO_MEMORY,
    CENSUS_NO_READ
} census_outcome_t;

typedef struct census {
    census_outcome_t outcome;
    size_t           callers;
    uint32_t         returns[EXPECTED_CALLERS];
    uint32_t         bytes;
    uint32_t         read_us;
    uint32_t         search_us;
} census_t;

static census_t take_census(uintptr_t target)
{
    census_t  census;
    uintptr_t code_va = host_image_text();
    size_t    size    = host_image_text_size();
    uint8_t  *code    = NULL;
    uint64_t  started = 0u;
    uint64_t  read    = 0u;

    memset(&census, 0, sizeof census);
    census.bytes   = (uint32_t)size;
    census.outcome = CENSUS_NO_READ;
    if (code_va != 0u && size != 0u) {
        code = (uint8_t *)malloc(size);
        if (code == NULL) {
            census.outcome = CENSUS_NO_MEMORY;
        } else {
            started = mp_stopwatch_ticks();
            if (host_image_read_original(code_va, code, size)) {
                census.outcome = CENSUS_COUNTED;
            }
            read = mp_stopwatch_ticks();
            census.read_us = mp_stopwatch_micros(started, read);
        }
    }
    if (census.outcome == CENSUS_COUNTED) {
        /* Both addresses live: the section's where it was loaded, the target where the signature
         * found it. */
        census.callers   = mp_range_gate_count_callers(code, size, (uint32_t)code_va,
                                                       (uint32_t)target, census.returns,
                                                       EXPECTED_CALLERS);
        census.search_us = mp_stopwatch_micros(read, mp_stopwatch_ticks());
    }
    free(code);
    return census;
}

static bool is_one_of_the_two(uintptr_t caller)
{
    size_t i;

    for (i = 0; i < gate.caller_count; ++i) {
        if (gate.callers[i] == caller) {
            return true;
        }
    }
    return false;
}

/* ==============================================================================================
 * The hook.
 *
 * Called tens of thousands of times a second, so it holds no lock, reads no engine memory, logs
 * nothing and allocates nothing. The engine's own answer is taken first and returned untouched
 * when it is yes, which is the common case and costs one call and one branch.
 * ============================================================================================ */

/* The activation scan was answered yes for a placement the engine itself had out of range: which
 * far player stood in it is written down by the placement's record. The two callers hand the test
 * different places, and that is how the record is known with no read: the scan measures from the
 * placement's authored position inside its record, the removal from the actor's own position.
 *
 *   004371F5  81 C2 AC 00 00 00   add edx, 0ACh    the record's position
 *   004371FB  52                  push edx
 *   004371FC  E8 B2 1C FF FF      call within_range      returns to 00437201, the scan
 *   004332EC  05 D0 00 00 00      add eax, 0D0h    the actor's position
 *   004332F1  50                  push eax
 *   004332F2  E8 BC 5B FF FF      call within_range      returns to 004332F7, the removal
 *
 * The first far bank in range is the one named; two far players within the same radius of one
 * placement stand together, and either's place is the scene's. */
static void note_the_waking(const float at[3], float radius)
{
    size_t rows = sizeof gate.players.have / sizeof gate.players.have[0];
    size_t bank;

    for (bank = 1u; bank < gate.players.count && bank < rows; ++bank) {
        if (gate.players.have[bank] &&
            mp_range_gate_within(at, gate.players.positions[bank], radius)) {
            mp_scene_woke_note(&gate.woke, (uintptr_t)at - MP_PLACEMENT_POSITION, (uint8_t)bank,
                               gate.refreshes);
            return;
        }
    }
}

static int __cdecl hook_within_range(const float at[3], const float player[3], float radius)
{
    uintptr_t caller = (uintptr_t)_ReturnAddress();
    int       answer = gate.original(at, player, radius);

    ++gate.asked;
    if (answer != 0) {
        ++gate.engine_said_yes;
        return answer;
    }
    if (!gate.armed || !gate.host) {
        return answer;
    }
    if (!is_one_of_the_two(caller)) {
        ++gate.foreign_caller;
        return answer;
    }
    if (mp_range_gate_any_within(&gate.players, at, radius)) {
        ++gate.widened;
        if (caller == gate.callers[SCAN_CALLER]) {
            note_the_waking(at, radius);
        }
        return 1;
    }
    return answer;
}

/* ============================================================================================ */

bool mp_range_gate_install(void)
{
    uintptr_t target;
    census_t  census;

    if (gate.installed) {
        return true;
    }
    if (!gate.host) {
        return false;   /* a client never widens its own activation; the level is the host's */
    }
    target = mp_signatures_address(MP_SITE_WITHIN_RANGE);
    if (target == 0u) {
        log_warning("the range test did not resolve, so an enemy standing beside a far player is "
                    "still measured against this machine alone");
        return false;
    }
    census = take_census(target);
    if (census.outcome == CENSUS_NO_MEMORY) {
        log_warning("the range test's caller census could not have the %u bytes of the "
                    "executable's code section to read them into, so nothing is widened and an "
                    "enemy standing beside a far player is still measured against this machine "
                    "alone; the next arming asks again",
                    (unsigned)census.bytes);
        return false;
    }
    if (census.outcome == CENSUS_NO_READ) {
        log_warning("the range test's caller census could not read the %u bytes of the "
                    "executable's code section in one piece (%u.%03u ms), so nothing is widened "
                    "and an enemy standing beside a far player is still measured against this "
                    "machine alone; the next arming asks again",
                    (unsigned)census.bytes, (unsigned)(census.read_us / 1000u),
                    (unsigned)(census.read_us % 1000u));
        return false;
    }
    if (census.callers != EXPECTED_CALLERS) {
        log_warning("the range test at %08X has %u caller(s) in this image and this build knows "
                    "%u; nothing is widened, because a caller this build has not read may not be "
                    "asking whether a player is near (the census read %u bytes in %u.%03u ms)",
                    (unsigned)target, (unsigned)census.callers, (unsigned)EXPECTED_CALLERS,
                    (unsigned)census.bytes, (unsigned)(census.read_us / 1000u),
                    (unsigned)(census.read_us % 1000u));
        return false;
    }
    memcpy(gate.callers, census.returns, sizeof gate.callers);
    gate.caller_count = census.callers;
    if (!detour_install(&gate.hull, target, (const void *)&hook_within_range,
                        mp_signatures_prologue(MP_SITE_WITHIN_RANGE))) {
        log_warning("the range test at %08X did not take a hull", (unsigned)target);
        return false;
    }
    gate.original  = (within_range_fn_t)gate.hull.original;
    gate.target    = target;
    gate.installed = true;
    log_info("the range an enemy is kept or woken by is measured against the NEAREST player, at "
             "%08X. Its two callers are the activation scan (returns to %08X) and the removal "
             "test in the entity loop (returns to %08X), and the census was taken again on this "
             "image rather than believed: %u bytes of the executable read in one piece in "
             "%u.%03u ms and searched in %u.%03u ms. The engine's own answer is asked first and "
             "a yes is never touched, so this can only keep an actor that would have been taken "
             "away",
             (unsigned)target, (unsigned)gate.callers[1], (unsigned)gate.callers[0],
             (unsigned)census.bytes, (unsigned)(census.read_us / 1000u),
             (unsigned)(census.read_us % 1000u), (unsigned)(census.search_us / 1000u),
             (unsigned)(census.search_us % 1000u));
    return true;
}

void mp_range_gate_set_host(bool host)
{
    gate.host = host;
}

void mp_range_gate_set_armed(bool armed)
{
    gate.armed = armed;
    if (!armed) {
        memset(&gate.players, 0, sizeof gate.players);
        mp_range_gate_forget_woken();
    }
}

void mp_range_gate_set_far_body(mp_range_gate_far_body_fn_t far_body)
{
    gate.far_body = far_body;
}

bool mp_range_gate_widens(void)
{
    return gate.installed && gate.host && gate.armed;
}

void mp_range_gate_refresh(void)
{
    size_t bank;
    size_t rows = sizeof gate.players.have / sizeof gate.players.have[0];

    if (!gate.installed || !gate.armed || gate.far_body == NULL) {
        return;
    }
    memset(&gate.players, 0, sizeof gate.players);
    /* Bank 0 is this machine's own body and it is already what the engine measured against, so
     * only the far banks are copied. Its row stays empty on purpose. */
    for (bank = 1u; bank <= MP_BANK_FAR_MAX && bank < rows; ++bank) {
        if (gate.far_body(bank, gate.players.positions[bank])) {
            gate.players.have[bank] = true;
            ++gate.bodies_seen;
        }
    }
    gate.players.count = (MP_BANK_FAR_MAX + 1u < rows) ? MP_BANK_FAR_MAX + 1u : rows;
    ++gate.refreshes;
}

bool mp_range_gate_player(size_t bank, float out[3])
{
    size_t rows = sizeof gate.players.have / sizeof gate.players.have[0];

    if (out == NULL || bank == 0u || bank >= rows || bank >= gate.players.count ||
        !gate.players.have[bank]) {
        return false;
    }
    memcpy(out, gate.players.positions[bank], sizeof gate.players.positions[bank]);
    return true;
}

bool mp_range_gate_measuring(void)
{
    return gate.installed && gate.armed && gate.far_body != NULL;
}

bool mp_range_gate_woke_for_far(uintptr_t record, uint8_t *bank)
{
    return gate.installed && mp_scene_woke_for(&gate.woke, record, gate.refreshes, bank);
}

void mp_range_gate_forget_woken(void)
{
    memset(gate.woke.row, 0, sizeof gate.woke.row);
    gate.woke.next = 0u;
}

bool mp_range_gate_wake_redirected(void)
{
    uint32_t displacement = 0;
    uint32_t after;

    if (!gate.installed || gate.caller_count <= SCAN_CALLER) {
        return false;
    }
    after = gate.callers[SCAN_CALLER];
    if (!memory_try_read((uintptr_t)(after - CALL_DISPLACEMENT_BYTES), &displacement,
                         sizeof displacement)) {
        return false;
    }
    return (uintptr_t)(after + displacement) != gate.target;
}

void mp_range_gate_report(void)
{
    if (!gate.installed) {
        return;
    }
    log_info("  the range an enemy is measured by: %u ask(s), %u the engine answered itself, "
             "%u kept for a far player the engine could not see, %u from a caller this build "
             "does not know (must be 0)",
             (unsigned)gate.asked, (unsigned)gate.engine_said_yes, (unsigned)gate.widened,
             (unsigned)gate.foreign_caller);
    log_info("    the far bodies it measured against: %u refresh(es) carrying %u body(s)",
             (unsigned)gate.refreshes, (unsigned)gate.bodies_seen);
    log_info("    the placements woken for a far player: %u answer(s) to the activation scan "
             "written down with the player they were for, kept %u substep(s) each; %u row(s) "
             "taken by a newer one while still fresh",
             (unsigned)gate.woke.noted, (unsigned)MP_SCENE_WOKE_SUBSTEPS,
             (unsigned)gate.woke.replaced);
}
