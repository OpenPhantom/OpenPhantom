/* mp_cutscene_sites.c: where a script takes what a scene holds, and where it gives it back. See
 * the header for the two opcodes and their bytes.
 */
#include "mp_cutscene_sites.h"

#include "mp_scene_free_rule.h"
#include "mp_scene_rule.h"
#include "mp_signatures_scene.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A jmp rel32, which is what every detour in this tree leaves on the head it takes. The head of
 * the camera take is read before its hull goes on, because afterwards the byte is the hull's. */
#define SCENE_BRANCH_OPCODE 0xE9u

/* The clearing of the camera's override, end to end. */
#define CAMERA_OFF_BYTES sizeof SIG_SCENE_VIEW_RELEASE

/* A site matched only to learn the address behind its call. */
typedef struct scene_take {
    const uint8_t *bytes;
    const uint8_t *mask;
    size_t         size;
    size_t         after_the_call;
    const char    *what;
} scene_take_t;

static const scene_take_t SCENE_TAKES[MP_CUTSCENE_TAKES] = {
    { SIG_SCENE_DOLLY_TAKE, MSK_SCENE_DOLLY_TAKE, sizeof SIG_SCENE_DOLLY_TAKE,
      SCENE_DOLLY_TAKE_RETURN, "the camera dolly opcode" },
    { SIG_SCENE_LOCK_TAKE, MSK_SCENE_LOCK_TAKE, sizeof SIG_SCENE_LOCK_TAKE,
      SCENE_LOCK_TAKE_RETURN, "the lock player opcode" },
    { SIG_SCENE_SPEAK_TAKE, MSK_SCENE_SPEAK_TAKE, sizeof SIG_SCENE_SPEAK_TAKE,
      SCENE_SPEAK_TAKE_RETURN, "a spoken line" },
};

/* ==============================================================================================
 * The takes.
 * ============================================================================================ */

/* The three script sites, resolved for their return addresses and for nothing else. A site that
 * does not resolve is left out of the list, and its opcode then keeps the camera. */
static void resolve_the_script_takes(mp_cutscene_takes_t *out)
{
    size_t index;

    out->script_return_count = 0u;
    for (index = 0; index < MP_CUTSCENE_TAKES; ++index) {
        uintptr_t site = signature_find_unique(SCENE_TAKES[index].bytes, SCENE_TAKES[index].mask,
                                               SCENE_TAKES[index].size);

        if (site == 0u) {
            log_warning("the camera take of %s did not resolve, so a script of this machine can "
                        "still swing the view there", SCENE_TAKES[index].what);
            continue;
        }
        out->take_return[index] = site + (uintptr_t)SCENE_TAKES[index].after_the_call;
        out->script_returns[out->script_return_count] = out->take_return[index];
        ++out->script_return_count;
    }
}

/* Where the camera take lives, read out of the calls the resolved script sites make to it. True
 * with the address in `entry` when every one of them calls the same place. A site whose bytes
 * cannot be read keeps its zeros, and zeros are not a call. */
static bool camera_entry_from_the_calls(mp_cutscene_takes_t *out, uintptr_t *entry)
{
    mp_scene_call_site_t sites[MP_CUTSCENE_TAKES] = { { 0u, { 0u } } };
    size_t               index;

    out->camera_operands = 0u;
    for (index = 0; index < out->script_return_count; ++index) {
        sites[index].return_address = out->script_returns[index];
        (void)memory_read(out->script_returns[index] - MP_SCENE_CALL_BYTES, sites[index].call,
                          MP_SCENE_CALL_BYTES);
    }
    switch (mp_scene_camera_callee(sites, out->script_return_count, entry)) {
    case MP_SCENE_CALLEE_AGREED:
        out->camera_operands = out->script_return_count;
        return true;
    case MP_SCENE_CALLEE_NOT_A_CALL:
        log_warning("the arena cannot hold back the camera a script takes: a script site does "
                    "not end in a call, so where the take lives cannot be read from it");
        return false;
    case MP_SCENE_CALLEE_DISAGREE:
        log_warning("the arena cannot hold back the camera a script takes: its %u script sites "
                    "call different addresses, so which one is the take cannot be told",
                    (unsigned)out->script_return_count);
        return false;
    case MP_SCENE_CALLEE_NO_SITES:
    default:
        return false;   /* every site that did not resolve has said so already */
    }
}

/* The camera take, at the address its callers name, checked against its own pattern.
 *
 * Two ways, and they are independent. The callers name the address in their call operands. The
 * pattern finds it by its bytes, sifting the tail when a foreign branch has replaced the head.
 * Where both answer they must answer the same, or one of them has found something that is not
 * this function and nothing is hulled. Where the pattern answers nothing, the bytes at the called
 * address are proved instead, tail exactly and head as authored or already a branch. */
void mp_cutscene_sites_takes(mp_cutscene_takes_t *out)
{
    uintptr_t entry = 0u;
    uintptr_t found;
    uint8_t   head = 0u;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    resolve_the_script_takes(out);
    if (out->script_return_count == 0u || !camera_entry_from_the_calls(out, &entry)) {
        return;
    }
    found = signature_find_detour_target(SIG_SCENE_VIEW_OVERRIDE, MSK_SCENE_VIEW_OVERRIDE,
                                         sizeof SIG_SCENE_VIEW_OVERRIDE,
                                         SCENE_VIEW_OVERRIDE_PROLOGUE);
    if (found != 0u && found != entry) {
        log_warning("the arena cannot hold back the camera a script takes: its callers name "
                    "%08X and its pattern resolves at %08X", (unsigned)entry, (unsigned)found);
        return;
    }
    if (found == 0u &&
        signature_find_at(entry, SIG_SCENE_VIEW_OVERRIDE, MSK_SCENE_VIEW_OVERRIDE,
                          sizeof SIG_SCENE_VIEW_OVERRIDE, SCENE_VIEW_OVERRIDE_PROLOGUE) == 0u) {
        log_warning("the arena cannot hold back the camera a script takes: its callers name "
                    "%08X and the bytes there are not the function its pattern was cut from",
                    (unsigned)entry);
        return;
    }
    out->camera_head_branched  = memory_read_u8(entry, &head) && head == SCENE_BRANCH_OPCODE;
    out->camera_pattern_agrees = found != 0u;
    out->camera_entry          = entry;
}

/* ==============================================================================================
 * The releases.
 * ============================================================================================ */

/* Whether the five bytes in front of `return_address` call `callee`. */
static bool calls(uintptr_t return_address, uintptr_t callee)
{
    mp_scene_call_site_t call   = { 0u, { 0u } };
    uintptr_t            target = 0u;

    if (return_address <= MP_SCENE_CALL_BYTES || callee == 0u) {
        return false;
    }
    call.return_address = return_address;
    (void)memory_read(return_address - MP_SCENE_CALL_BYTES, call.call, MP_SCENE_CALL_BYTES);
    return mp_scene_camera_callee(&call, 1u, &target) == MP_SCENE_CALLEE_AGREED &&
           target == callee;
}

/* The two script ends as the one reading of them takes them: where each one's call of the lock's
 * release returns to, counted from the camera take of its opcode, and the twelve bytes in front
 * of it. An end whose take did not resolve, or whose bytes do not read, is left unread. */
static void read_the_script_ends(const mp_cutscene_takes_t *takes,
                                 mp_scene_free_end_t ends[MP_SCENE_FREE_ENDS])
{
    size_t end;

    memset(ends, 0, MP_SCENE_FREE_ENDS * sizeof ends[0]);
    if (takes->take_return[MP_CUTSCENE_TAKE_DOLLY] != 0u) {
        ends[MP_CUTSCENE_END_DOLLY].return_address =
            takes->take_return[MP_CUTSCENE_TAKE_DOLLY] + SCENE_DOLLY_RELEASE_PAST_THE_TAKE;
    }
    if (takes->take_return[MP_CUTSCENE_TAKE_LOCK] != 0u) {
        ends[MP_CUTSCENE_END_LOCK].return_address =
            takes->take_return[MP_CUTSCENE_TAKE_LOCK] + SCENE_LOCK_RELEASE_PAST_THE_TAKE;
    }
    for (end = 0u; end < MP_SCENE_FREE_ENDS; ++end) {
        ends[end].read = ends[end].return_address > MP_SCENE_FREE_END_BYTES &&
                         memory_read(ends[end].return_address - MP_SCENE_FREE_END_BYTES,
                                     ends[end].bytes, MP_SCENE_FREE_END_BYTES);
    }
}

/* The clearing of the camera's override, at the address both ends call: inside the image, and
 * with the function's own bytes there, the tail exactly and the head as authored or already a
 * branch of another module. Then each end's call of it is asked again by itself, which is what
 * names the address that call returns to. */
static void prove_the_camera_off(uintptr_t address, mp_cutscene_releases_t *out)
{
    size_t end;

    if (address == 0u || !memory_is_inside_image(address, CAMERA_OFF_BYTES) ||
        signature_find_at(address, SIG_SCENE_VIEW_RELEASE, MSK_SCENE_VIEW_RELEASE,
                          sizeof SIG_SCENE_VIEW_RELEASE, SCENE_VIEW_RELEASE_PROLOGUE) == 0u) {
        log_warning("the clearing of the camera's override is not held on this host: the two "
                    "script ends do not both name one address inside the image with the "
                    "function's bytes there (read: %08X), so a far player's script can still give "
                    "the host's camera back", (unsigned)address);
        return;
    }
    out->camera_off_entry = address;
    for (end = 0u; end < MP_CUTSCENE_ENDS; ++end) {
        uintptr_t returns = out->lock_off_return[end] > SCENE_CAMERA_OFF_BEFORE_THE_RELEASE
                                ? out->lock_off_return[end] - SCENE_CAMERA_OFF_BEFORE_THE_RELEASE
                                : 0u;

        if (calls(returns, address)) {
            out->camera_off_return[end] = returns;
        }
    }
}

_Static_assert(MP_CUTSCENE_ENDS == MP_SCENE_FREE_ENDS,
               "the two script ends are the ones the reading of them takes");

void mp_cutscene_sites_releases(const mp_cutscene_takes_t *takes, mp_cutscene_releases_t *out)
{
    mp_scene_free_end_t  ends[MP_SCENE_FREE_ENDS];
    mp_scene_free_ends_t said;
    uintptr_t            address;
    size_t               end;

    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (takes == NULL) {
        return;
    }
    read_the_script_ends(takes, ends);
    address = signature_find_detour_target(SIG_SCENE_LOCK_LEAVE, MSK_SCENE_LOCK_LEAVE,
                                           sizeof SIG_SCENE_LOCK_LEAVE,
                                           SCENE_LOCK_LEAVE_PROLOGUE);
    mp_scene_free_read_ends(address, ends, &said);
    if (address == 0u || !said.release_holds) {
        log_warning("the releases of a script are not held on this host: the lock's release "
                    "resolves at %08X by its pattern and a script end calls %08X in its place "
                    "(nought for none), so a far player's script can still end the host's scene",
                    (unsigned)address, (unsigned)said.disagreeing);
        return;
    }
    out->lock_off_entry = address;
    for (end = 0u; end < MP_CUTSCENE_ENDS; ++end) {
        if (ends[end].read && calls(ends[end].return_address, address)) {
            out->lock_off_return[end] = ends[end].return_address;
        }
    }
    prove_the_camera_off(said.camera_off, out);
}
