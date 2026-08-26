/* ground_clip_fix.c: a character the engine never collision tests must not be given a velocity.
 *
 * THE SYMPTOM. A character sitting on a box can be walked down through it and under the level by
 * bumping into her. Standing on her head does it fastest, she never comes back up, and the 1999
 * game as shipped does it too.
 *
 * THE CAUSE, and it is not a collision test getting the wrong answer. FUN_004362c8, the character
 * movement function, tests bit 0 of the movement mode at character+0x98 before anything else and
 * jumps clean past the entire collision block when it is set:
 *
 *     004363DF  mov  eax,[edx+0x98]
 *     004363E5  and  eax,1
 *     004363EA  jnz  0043650B          past the swept test, straight to the commit
 *
 * The allow flag it then commits against was reset to permitted at the top of the step by
 * FUN_00435c67, so the move is committed unconditionally. She is not failing a collision test. She
 * is not taking one.
 *
 * That exemption is sound while its assumption holds: these are static seated characters that
 * never go anywhere, so testing them would be work for nothing. Measured in one level, the ordinary
 * NPCs were mode 0 and tested, while the seated background characters were mode 3 and exempt.
 *
 * What breaks the assumption is contact. The handler in enemy.c at FUN_00436a68 adds an impulse to
 * a character's velocity on contact without asking whether that character can be moved safely.
 * Standing on her points the impulse down. FUN_004362c8 then integrates it and commits the result
 * at 0x0043655E with nothing consulted, and the landing path clears the velocity while leaving the
 * position where it ended up, so the next push starts from there.
 *
 * WHAT THIS DOES. Before the movement function runs, a character that is exempt from collision has
 * its velocity cleared. That restores the engine's own invariant, that an untested character does
 * not move, rather than arguing with the exemption. A collision tested character is never touched,
 * which is every ordinary NPC and the player.
 *
 * ============================ Three earlier attempts, and why they failed =====================
 *
 * Recorded because each looked right from the disassembly and each cost a play session, and
 * because the next person reading this will be tempted by at least one of them again.
 *
 *   Gravity settling her onto a wrongly chosen floor. Refuted by measurement: the steps were
 *   exactly one sixteenth every time and never accelerated, and they paused for seconds at a time
 *   while the player stood beside her. Gravity does none of that.
 *
 *   A refused move failing to clear her downward velocity, so it accumulated. Refuted: her
 *   velocity reads zero while she stands still and spikes only on the steps she actually moves, so
 *   it is an impulse and not something retained.
 *
 *   The swept collision test raising its ray origin by a step-over allowance, hiding a small
 *   descent. This one was built, shipped to a test install, and changed nothing. The instrument
 *   that was added to find out why is what found the real cause: in four thousand sweeps, six were
 *   descending, all six were the PLAYER landing, and not one carried the allowance the character
 *   move test passes. Her move never reaches that function at all.
 *
 * The lesson worth keeping is the one that ended it: counting what a hook actually sees is worth
 * more than reasoning about what it should see.
 */
#include "ground_clip_fix.h"

#include "move_mode.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GROUND_CLIP_SECTION "ground_clip_fix"

/* --- 0x004362C8  the character movement function ---------------------------------------------- *
 *   55                    push ebp
 *   8B EC                 mov  ebp,esp
 *   83 EC 44              sub  esp,0x44           the six bytes a detour replaces end here
 *   8B 45 08              mov  eax,[ebp+8]        the character
 *   8B 48 14              mov  ecx,[eax+0x14]
 *   81 E1 00 00 00 10     and  ecx,0x10000000
 *
 * Counted against the retail executable, 829,952 bytes, MD5 7c5af8428c19b17cca09ae3a49bd10ef: one
 * match at sixteen bytes and still one at twenty four, so the pattern has margin rather than
 * sitting exactly on the edge of uniqueness. Registered with the detour form of the macro so a
 * second DLL wanting this site still resolves after this one has replaced the prologue. */
static const uint8_t SIG_CHARACTER_MOVE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x44, 0x8B, 0x45, 0x08, 0x8B, 0x48, 0x14,
    0x81, 0xE1, 0x00, 0x00, 0x00, 0x10, 0x85, 0xC9
};
#define CHARACTER_MOVE_PROLOGUE 6u

enum {
    SITE_CHARACTER_MOVE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("character_move", SIG_CHARACTER_MOVE, CHARACTER_MOVE_PROLOGUE)
};

/* The two fields this reads, both confirmed by the diagnostics character census before a line of
 * this fix was written: the mode the movement function branches on, and the velocity it would
 * integrate. */
#define CHARACTER_MOVE_MODE_OFFSET 0x98u
#define CHARACTER_VELOCITY_OFFSET  0xDCu

typedef void(__cdecl *character_move_fn_t)(void *character);

typedef struct ground_clip_state {
    bool     installed;
    bool     active;
    detour_t move;
    uint32_t cleared;    /* velocities taken off an untested character */
    uint32_t reported;
} ground_clip_state_t;

static ground_clip_state_t clip_state;

/* Loud enough to prove the fix is doing something on the first run, quiet enough not to fill a log
 * afterwards. A fix that installs and then never fires looks exactly like a fix that works, and
 * this project has now been caught by that difference twice in one investigation. */
#define REPORT_EVERY 50u

static void __cdecl hook_character_move(void *character)
{
    character_move_fn_t original = (character_move_fn_t)clip_state.move.original;

    if (clip_state.active && character != NULL) {
        int32_t move_mode = 0;
        float   velocity[3];

        /* Guarded reads rather than the checked ones: this runs for every character on every
           simulation step, which is exactly the shape CONTRIBUTING.md warns against putting a
           VirtualQuery behind. */
        if (memory_try_read((uintptr_t)character + CHARACTER_MOVE_MODE_OFFSET, &move_mode,
                            sizeof(move_mode)) &&
            memory_try_read((uintptr_t)character + CHARACTER_VELOCITY_OFFSET, velocity,
                            sizeof(velocity)) &&
            move_mode_move_is_uncontested(move_mode, velocity)) {
            /* The read above already proved this range is there, and it is ordinary game data
               rather than code, so it needs no protection change. Written through a volatile
               pointer, the same way every other feature here reaches a live field, so the compiler
               cannot decide these three stores are dead. */
            volatile float *live = (volatile float *)((uintptr_t)character +
                                                      CHARACTER_VELOCITY_OFFSET);

            live[0] = 0.0f;
            live[1] = 0.0f;
            live[2] = 0.0f;

            ++clip_state.cleared;
            /* The FIRST one is always reported, and then only every REPORT_EVERY after it. The
               first run of this fix worked and printed nothing, because the threshold alone needed
               fifty clears and the character was pushed fewer times than that, which left the log
               unable to say whether the hook had fired at all. One line at the start settles
               that. */
            if (clip_state.cleared == 1u ||
                (clip_state.cleared - clip_state.reported) >= REPORT_EVERY) {
                clip_state.reported = clip_state.cleared;
                log_info("velocities cleared from characters the engine does not collision test: "
                         "%u", (unsigned)clip_state.cleared);
            }
        }
    }

    /* The original may be another DLL's hook rather than the engine, so it is called exactly as if
       it were the real function. It returns nothing, so there is nothing to pass back. */
    original(character);
}

void ground_clip_fix_install(void)
{
    log_init("ground_clip_fix", false);

    if (clip_state.installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, nothing patched");
        return;
    }
    if (!ini_read_bool(GROUND_CLIP_SECTION, "Enabled", true)) {
        log_info("Enabled=0, a character the engine never collision tests can still be pushed "
                 "through the floor");
        return;
    }

    clip_state.installed = true;

    if (signature_resolve_table(sites, SITE_COUNT) != SITE_COUNT) {
        log_error("the character movement function did not resolve, nothing patched");
        return;
    }
    if (!detour_install(&clip_state.move, sites[SITE_CHARACTER_MOVE].address,
                        (const void *)hook_character_move, CHARACTER_MOVE_PROLOGUE)) {
        log_error("the character movement function could not be detoured, nothing patched");
        return;
    }

    clip_state.active = true;
    log_info("armed on the character movement function at %08X: a character the engine skips "
             "collision for no longer carries a velocity",
             (unsigned)sites[SITE_CHARACTER_MOVE].address);
}
