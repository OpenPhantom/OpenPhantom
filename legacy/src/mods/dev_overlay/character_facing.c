/* character_facing.c: the backface drop, taken off the borrowed body and put straight back. */
#include "character_facing.h"

#include "character_nodemap.h"
#include "character_rebind.h"

#include "common/detour.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEV_OVERLAY_SECTION "dev_overlay"

/* The one instruction in the image that consumes the backface switch, and the four bytes of it
 * that name the switch. It sits at the end of the facing test in rdMesh_draw: the dot product of
 * the face's authored normal with the camera has come out at or below zero, the face points away,
 * and this is what decides whether it is dropped or drawn.
 *
 * The address is masked out and read from the matched bytes rather than written into the pattern.
 * That is not decoration: the Edit Tool's recompile of the engine carries the same instruction
 * with the switch at 00868640 instead of 008686A0, and an address in the pattern would have
 * refused the build that proves the pattern is right. */
static const uint8_t SIG_FACING_CULL_WORD[] = {
    0x8A, 0x1D, 0x00, 0x00, 0x00, 0x00,        /* mov bl,[the backface switch]                  */
    0xB8, 0x01, 0x00, 0x00, 0x00,              /* mov eax,1                                     */
    0x84, 0xD8,                                /* test al,bl                                    */
    0x75, 0x32                                 /* jne  -> the face is dropped                   */
};
static const uint8_t MSK_FACING_CULL_WORD[] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF
};
_Static_assert(sizeof(SIG_FACING_CULL_WORD) == sizeof(MSK_FACING_CULL_WORD),
               "the backface switch pattern and its mask are different lengths");

#define OFFSET_CULL_WORD  2u

/* rdThing_Draw, the call that draws one render handle. It is compiled without a frame pointer, so
 * it opens on the stack reservation and the word count of the matrix copy rather than on a push of
 * ebp, and the two cdecl arguments are loaded off the stack after the two pushes that follow.
 *
 * Decoded instruction boundaries are at 0, 3 and 8. Eight is a boundary AND the first one at or
 * past the five bytes a jump needs, which is why it is the prologue and why it was decoded rather
 * than counted. */
static const uint8_t SIG_FACING_THING_DRAW[] = {
    0x83, 0xEC, 0x48,                          /* sub esp,0x48                                  */
    0xB9, 0x0C, 0x00, 0x00, 0x00,              /* mov ecx,0x0C     the twelve words of a matrix */
    0x55,
    0x8B, 0x6C, 0x24, 0x50,                    /* mov ebp,[esp+0x50]   the handle               */
    0x56,
    0x8B, 0x74, 0x24, 0x58,                    /* mov esi,[esp+0x58]   the matrix               */
    0x89, 0x2D, 0x00, 0x00, 0x00, 0x00,        /* mov [the handle being drawn],ebp              */
    0x8B, 0x45, 0x04                           /* mov eax,[ebp+4]      the model it wears       */
};
static const uint8_t MSK_FACING_THING_DRAW[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF
};
_Static_assert(sizeof(SIG_FACING_THING_DRAW) == sizeof(MSK_FACING_THING_DRAW),
               "the body draw pattern and its mask are different lengths");

#define FACING_THING_DRAW_PROLOGUE 8u

/* The prototype carries the return value. rdThing_Draw answers the caller's visibility code, and
 * four callers read it: the object draw where it becomes visCode, the shadow gate, the shot path
 * and one more. A hook that dropped it would hand all four whatever happened to be in eax. */
typedef int32_t (__cdecl *thing_draw_fn_t)(void *thing, void *matrix);

typedef struct facing_state {
    bool            tried;
    bool            installed;
    detour_t        detour;
    thing_draw_fn_t original;

    uint8_t        *cull_word;
} facing_state_t;

static facing_state_t facing;

/* ============================================================================================ */

uint32_t character_facing_lower(uint8_t cull_word)
{
    if ((cull_word & CHARACTER_FACING_CULL_BIT) == 0u) {
        return CHARACTER_FACING_KEEP;
    }
    return (uint32_t)cull_word & ~(uint32_t)CHARACTER_FACING_CULL_BIT;
}

/* ============================================================================================ */

/* THE WINDOW, and it lasts exactly one call. Every other handle the frame draws sees the switch
 * the engine put there, which is what keeps the level's walls, its props and the player's own
 * borrowed weapon one sided and off the fill rate this costs.
 *
 * Whose draw it is comes out of the table of bodies, and the question whether the handle still
 * wears what the swap put on it is asked of the engine on every draw rather than remembered: a
 * level change frees the render handle and the allocator is free to hand the same address to a
 * body this module never touched.
 *
 * A far body's own weapon nodes are hidden again first. The engine shows a weapon node by NAME on
 * every weapon change of that player, and a borrowed rig that carries a node of that name would
 * wear it from then on; hiding it before each draw is what keeps it out of the picture.
 *
 * The engine's own value is restored rather than a constant written back, because the switch
 * carries two more bits the game sets at 0x0043F4D4 and this module has no business touching
 * either. */
static int32_t __cdecl hook_thing_draw(void *thing, void *matrix)
{
    body_entry_t body;
    uint8_t      saved;
    uint32_t     lowered;
    int32_t      result;

    if (!character_nodemap_holds((uintptr_t)thing, &body)) {
        return facing.original(thing, matrix);
    }
    if (!body.local && body.hidden_count != 0u) {
        character_rebind_hide(body.thing, body.target, body.hidden, body.hidden_count);
    }
    if (!body.two_sided) {
        return facing.original(thing, matrix);
    }
    saved = *facing.cull_word;
    lowered = character_facing_lower(saved);
    if (lowered == CHARACTER_FACING_KEEP) {
        return facing.original(thing, matrix);
    }

    *facing.cull_word = (uint8_t)lowered;
    result = facing.original(thing, matrix);
    *facing.cull_word = saved;
    return result;
}

/* ============================================================================================ */

/* Resolved and hooked on the first swap, not at load, and the delay is the whole safety of it.
 *
 * view_distance_fix resolves the same draw for its severed limb pass, through a table entry that
 * carries no detour prologue, so it cannot find the site again once somebody has written a jump
 * over the head of it. A swap needs a running level, an open panel and a chosen row, all of which
 * come long after every mod has installed, so by the time this runs that resolution has already
 * happened. Detouring at load would have made this module's presence decide whether another one
 * worked.
 *
 * Going in second is not a problem for this module: detour_install sees the jump that is already
 * there and chains in front of it, and the hook it chains onto is called as the original. */
static bool install_once(void)
{
    uintptr_t anchor;
    uintptr_t site;
    uint32_t  word = 0;

    if (facing.tried) {
        return facing.installed;
    }
    facing.tried = true;

    if (!ini_read_bool(DEV_OVERLAY_SECTION, "TwoSidedSwap", true)) {
        log_info("TwoSidedSwap is off, so a borrowed body keeps the engine's backface drop and "
                 "shows nothing where a shell the asset never marked two sided turns its open "
                 "side to the camera");
        return false;
    }

    anchor = signature_find_unique(SIG_FACING_CULL_WORD, MSK_FACING_CULL_WORD,
                                   sizeof SIG_FACING_CULL_WORD);
    if (anchor == 0u) {
        log_warning("the backface switch did not resolve, so a borrowed body is drawn the way the "
                    "asset was built and a one sided shell shows nothing from behind");
        return false;
    }
    if (!memory_read_u32(anchor + OFFSET_CULL_WORD, &word) || word == 0u ||
        !memory_is_inside_image((uintptr_t)word, sizeof(uint8_t))) {
        log_warning("the operand at %08X does not name a byte inside the image, so the backface "
                    "switch is not touched", (unsigned)anchor);
        return false;
    }

    site = signature_find_detour_target(SIG_FACING_THING_DRAW, MSK_FACING_THING_DRAW,
                                        sizeof SIG_FACING_THING_DRAW, FACING_THING_DRAW_PROLOGUE);
    if (site == 0u) {
        log_warning("the body draw did not resolve, so there is no call to take the backface drop "
                    "off and a borrowed body keeps it");
        return false;
    }
    if (!detour_install(&facing.detour, site, (const void *)&hook_thing_draw,
                        FACING_THING_DRAW_PROLOGUE)) {
        log_warning("the body draw at %08X could not be hooked", (unsigned)site);
        return false;
    }

    facing.original = (thing_draw_fn_t)facing.detour.original;
    facing.cull_word = (uint8_t *)(uintptr_t)word;
    facing.installed = true;
    log_info("the backface switch is at %08X and the body draw at %08X is hooked, so a borrowed "
             "body is drawn two sided and nothing else in the frame is", (unsigned)word,
             (unsigned)site);
    return true;
}

bool character_facing_arm(uintptr_t thing)
{
    if (thing == 0u) {
        return false;
    }
    if (!install_once()) {
        return false;
    }
    return character_nodemap_set_two_sided(thing, true);
}

bool character_facing_is_armed(void)
{
    return character_nodemap_any_two_sided();
}
