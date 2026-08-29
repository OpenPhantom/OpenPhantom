/* ground_clip_fix.c: a contact must not push a character nothing will collision test.
 *
 * THE SYMPTOM. A character sitting on a box can be walked down through it and under the level by
 * bumping into her. Standing on her head does it fastest, she never comes back up, and the 1999
 * game as shipped does it too.
 *
 * THE CAUSE, and it is not a collision test getting the wrong answer. FUN_004362C8, the character
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
 * What gives her something to commit is contact. FUN_00436A68, the contact message handler in
 * enemy.c, adds an impulse to a character's velocity without asking whether that character can be
 * moved safely. Standing on her points it down. FUN_004362C8 integrates it and commits the result
 * at 0x0043655E with nothing consulted, and the landing path then clears the velocity while
 * leaving the position where it ended up, so the next push starts from there.
 *
 * ==================== WHAT THIS DOES, AND THE REGRESSION THAT SHAPED IT ======================
 *
 * The obvious repair is to clear the velocity of any character the engine does not collision test,
 * on the reasoning that such a character is static. THAT WAS BUILT, SHIPPED, AND WAS WRONG. Two
 * populations are exempt from collision and only one of them is static:
 *
 *   The seated background characters. They never move, and clearing a velocity they never carry
 *   costs nothing.
 *
 *   Ships, birds, and the droids on flying platforms. They are exempt PRECISELY BECAUSE they fly,
 *   and they move by velocity like everything else. Clearing it froze every one of them in place,
 *   which was reported from the game within a day.
 *
 * No field separates the two, and this no longer pretends one does. What separates the cases is
 * where the velocity came from. A scripted mover sets its own; a contact adds one through the
 * handler above. So this hooks the CONTACT HANDLER, remembers the velocity of the character being
 * contacted, lets the handler run, and puts the velocity back if that character is one nothing
 * will collision test.
 *
 * A ship's own velocity is identical on both sides of the handler, so there is nothing to undo and
 * it is never touched. Only a velocity the handler itself changed is treated as a push. Everything
 * else the handler does, damage included, is left exactly as the engine wrote it.
 *
 * ========================== The dead ends, kept so nobody repeats them ========================
 *
 * Each looked right from the disassembly and each cost a play session.
 *
 *   Gravity settling her onto a wrongly chosen floor. Refuted by measurement: the steps were
 *   exactly one sixteenth every time and never accelerated, and they paused for seconds at a time
 *   while the player stood beside her. Gravity does none of that.
 *
 *   A refused move failing to clear her downward velocity, so it accumulated. Refuted: her
 *   velocity reads zero while she stands still and spikes only on the steps she actually moves.
 *
 *   The swept collision test raising its ray origin by a step-over allowance, hiding a small
 *   descent. Built, shipped to a test install, and changed nothing. The instrument added to explain
 *   that failure is what found the real cause: in four thousand sweeps, six were descending, all
 *   six were the PLAYER landing, and not one carried the allowance the character move test passes.
 *   Her move never reaches that function at all.
 *
 *   Clearing the velocity of every collision exempt character. Built, shipped, and it froze the
 *   ships and birds, as described above.
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

/* --- 0x00436A68  the contact message handler, enemy.c --------------------------------------- *
 *   55                    push ebp
 *   8B EC                 mov  ebp,esp
 *   83 EC 44              sub  esp,0x44           the six bytes a detour replaces end here
 *   A1 <addr32>           mov  eax,[g_contactBody]     the address at +0x07
 *   89 45 F0              mov  [ebp-0x10],eax
 *   8B 0D <addr32>        mov  ecx,[g_contactOther]
 *   89 4D F4              mov  [ebp-0x0C],ecx
 *
 * A few instructions later it reads the character out of that body with `mov eax,[edx+0xA0]`,
 * which is the same owner field the diagnostics character census already reads back the other way.
 *
 * Both global addresses are wildcarded and the first is read out of its operand rather than
 * written down. Counted against the retail executable, 829,952 bytes, MD5
 * 7c5af8428c19b17cca09ae3a49bd10ef: one match. Registered with the detour form of the macro so a
 * second DLL wanting this site still resolves after this one replaces the prologue. */
static const uint8_t SIG_CONTACT[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x44, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x89,
    0x45, 0xF0, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x89, 0x4D, 0xF4
};
static const uint8_t MASK_CONTACT[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF
};
#define CONTACT_PROLOGUE       6u
#define OFFSET_CONTACT_BODY    0x07u

enum {
    SITE_CONTACT,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("contact_handler", SIG_CONTACT, MASK_CONTACT, CONTACT_PROLOGUE)
};

/* Fields read, all three confirmed by the diagnostics character census before this was written. */
#define BODY_OWNER_OFFSET          0xA0u   /* body -> the character that owns it */
#define CHARACTER_MOVE_MODE_OFFSET 0x98u
#define CHARACTER_VELOCITY_OFFSET  0xDCu

/* Returns int32_t, takes nothing, plain cdecl: it reads the objects in contact out of globals
 * rather than receiving them. The return value decides whether the caller treats the contact as
 * handled, so dropping it would change behaviour in a way that compiles cleanly. */
typedef int32_t(__cdecl *contact_fn_t)(void);

typedef struct ground_clip_state {
    bool       installed;
    bool       active;
    detour_t   contact;
    uint32_t  *body_slot;   /* the global holding the body being contacted */
    uint32_t   undone;      /* pushes taken back off an untested character */
    uint32_t   reported;
} ground_clip_state_t;

static ground_clip_state_t clip_state;

/* Loud on the first one, then occasional. A fix that installs and never fires looks exactly like a
 * fix that works, and this investigation was misled by that difference more than once. */
#define REPORT_EVERY 50u

/* The character about to be contacted, or NULL when the chain does not read. */
static uintptr_t contacted_character(void)
{
    uint32_t body = 0;
    uint32_t character = 0;

    if (clip_state.body_slot == NULL) {
        return 0;
    }
    if (!memory_try_read((uintptr_t)clip_state.body_slot, &body, sizeof(body)) || body == 0) {
        return 0;
    }
    if (!memory_try_read((uintptr_t)body + BODY_OWNER_OFFSET, &character, sizeof(character))) {
        return 0;
    }
    return (uintptr_t)character;
}

static int32_t __cdecl hook_contact(void)
{
    contact_fn_t original = (contact_fn_t)clip_state.contact.original;
    uintptr_t    character = 0;
    int32_t      move_mode = 0;
    float        before[3];
    bool         watching = false;
    int32_t      result;

    if (clip_state.active) {
        character = contacted_character();
        if (character != 0 &&
            memory_try_read(character + CHARACTER_MOVE_MODE_OFFSET, &move_mode,
                            sizeof(move_mode)) &&
            move_mode_skips_collision(move_mode) &&
            memory_try_read(character + CHARACTER_VELOCITY_OFFSET, before, sizeof(before))) {
            /* Only a character nothing will collision test is worth remembering. Every other one
               keeps whatever the handler gives it, because the engine can stop it properly. */
            watching = true;
        }
    }

    result = original();

    if (watching) {
        float after[3];

        if (memory_try_read(character + CHARACTER_VELOCITY_OFFSET, after, sizeof(after)) &&
            move_mode_contact_pushed(move_mode, before, after)) {
            /* Put back exactly what was there. Not zero: a flying character contacted mid flight
               must keep the velocity it arrived with, and only the part the handler added is
               undone. Written through a volatile pointer, the way every feature here reaches a
               live field, so the compiler cannot decide these stores are dead. */
            volatile float *live = (volatile float *)(character + CHARACTER_VELOCITY_OFFSET);

            live[0] = before[0];
            live[1] = before[1];
            live[2] = before[2];

            ++clip_state.undone;
            if (clip_state.undone == 1u ||
                (clip_state.undone - clip_state.reported) >= REPORT_EVERY) {
                clip_state.reported = clip_state.undone;
                log_info("contact pushes taken back off characters the engine does not collision "
                         "test: %u", (unsigned)clip_state.undone);
            }
        }
    }

    return result;
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
        log_info("Enabled=0, a contact can still push a character the engine never collision "
                 "tests through the floor");
        return;
    }

    clip_state.installed = true;

    if (signature_resolve_table(sites, SITE_COUNT) != SITE_COUNT) {
        log_error("the contact handler did not resolve, nothing patched");
        return;
    }

    /* Read the global's address out of the matched operand rather than writing it down, and refuse
       it if it does not land inside the image. That is the "no patch without a check" rule applied
       to a read, and it is what keeps this working if a build places the global elsewhere. */
    {
        uint32_t address = 0;

        if (!memory_read_u32(sites[SITE_CONTACT].address + OFFSET_CONTACT_BODY, &address) ||
            !memory_is_inside_image(address, sizeof(uint32_t))) {
            log_error("the contacted body global read as %08X, which is outside the image, "
                      "nothing patched", (unsigned)address);
            return;
        }
        clip_state.body_slot = (uint32_t *)(uintptr_t)address;
    }
    if (!detour_install(&clip_state.contact, sites[SITE_CONTACT].address,
                        (const void *)hook_contact, CONTACT_PROLOGUE)) {
        log_error("the contact handler could not be detoured, nothing patched");
        return;
    }

    clip_state.active = true;
    log_info("armed on the contact handler at %08X, reading the contacted body from %08X: a "
             "contact can no longer push a character the engine does not collision test",
             (unsigned)sites[SITE_CONTACT].address, (unsigned)(uintptr_t)clip_state.body_slot);
}
