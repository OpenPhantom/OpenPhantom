/* mp_body_shot.c: the bank-aware shot hull of the far bodies.
 *
 * Every player fire site passes a literal 1 as the shooter class, so the hull cannot switch on its
 * own argument. It asks the active bank instead, which is answerable because the fire path runs
 * inside the swap window, calls the original with class 1 so the three class-1 gates still fall
 * the player's way, and then writes the real class into the shot's own body.
 *
 * Left mp_body.c along the seam that file's size note named. The hull shares nothing with the
 * dispatcher, the tick or the far bodies' records: the install places it on the site the body
 * module resolved, and the report reads its counters. The listeners are declared in mp_body.h,
 * the three doors the body module uses in mp_body_internal.h.
 */
#include "mp_body.h"

#include "mp_body_internal.h"

#include "mp_bank.h"
#include "mp_shot_sites.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <intrin.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Inside the shot NODE, the pointer to the body it flies as. The node opens with the active-shot
 * list's next pointer, so the head of the node is another shot and never this one's body. The
 * offset comes from the spawner itself: it allocates the node, then the body, stores the body as
 * the node's dword 0x28 (which is +0xA0), and only then writes the head as the next pointer of
 * the singly linked active shot list. */
#define SHOT_NODE_OBJECT        0xA0u

/* shot_spawn(kind, muzzle, pitch, yaw, shooterClass) returns the shot NODE, cdecl. The node's own
 * body pointer is at +0xA0, NOT at its head: the head is the active-shot list's next pointer,
 * which the spawner writes one instruction earlier. The body carries the side at +0x08, and the
 * hull rewrites that after the class-1 call.
 *
 * The literal 1 is byte confirmed at the four player fire sites, 0x0044BB38, 0x0044BD7E,
 * 0x0045068F and 0x004506EA, each a `6A 01` in front of the call. The three gates inside the
 * spawner that test the class against 1 are the missile capture, the happy cheat and the scatter
 * rosette, and a bank's shot has to fall the player's way through all three. */
typedef void *(__cdecl *shot_spawn_fn_t)(int32_t kind, const void *muzzle, float pitch, float yaw,
                                         int32_t shooter_class);

typedef struct shot_state {
    detour_t     hull;
    bool         installed;
    uint32_t     side_faults;        /* a spawned shot whose side could not be stamped */
    bool         npc_replay;         /* the NPC bolt relay is firing its copy of a bolt */
    uint32_t     ally_shots;         /* class 1 shots the AI fired, told as an NPC's */
    uint32_t     player_shots;       /* class 1 shots told as the player's own */
    uint32_t     foreign_returns;    /* class 1 shots whose caller was not in the host image */
} shot_state_t;

static shot_state_t shots;

static mp_body_shot_listener_t shot_listener;

void mp_body_set_shot_listener(mp_body_shot_listener_t listener)
{
    shot_listener = listener;
}

static mp_body_shot_object_listener_t shot_object_listener;

void mp_body_set_shot_object_listener(mp_body_shot_object_listener_t listener)
{
    shot_object_listener = listener;
}

static mp_body_npc_shot_listener_t npc_shot_listener;

void mp_body_set_npc_shot_listener(mp_body_npc_shot_listener_t listener)
{
    npc_shot_listener = listener;
}

static mp_body_shot_object_listener_t ally_shot_listener;

void mp_body_set_ally_shot_listener(mp_body_shot_object_listener_t listener)
{
    ally_shot_listener = listener;
}

static mp_body_shot_made_listener_t shot_made_listener;

void mp_body_set_shot_made_listener(mp_body_shot_made_listener_t listener)
{
    shot_made_listener = listener;
}

void mp_body_note_npc_replay(bool firing)
{
    shots.npc_replay = firing;
}

/* Whose a class 1 shot is, from where the engine called: the nine calls of the AI fire for an
 * ally, everything else of class 1 is the player's. A caller outside the host image is another
 * DLL in front of this hull; it keeps the old answer, the player's, and is counted. This
 * project's own copy of an ally's bolt is nobody's and is not counted there. */
static mp_shot_whose_t whose_shot(int32_t shooter_class, uintptr_t caller)
{
    mp_shot_whose_t whose = mp_shot_whose(shooter_class,
                                          shooter_class == 1 && mp_shot_sites_is_ai(caller),
                                          shots.npc_replay);

    if (shooter_class == 1 && !shots.npc_replay &&
        (caller < host_image_base() || caller >= host_image_end())) {
        ++shots.foreign_returns;
    }
    return whose;
}

static void *__cdecl shot_hook(int32_t kind, const void *muzzle, float pitch, float yaw,
                               int32_t shooter_class)
{
    uintptr_t caller     = (uintptr_t)_ReturnAddress();
    int32_t   bank_class = mp_bank_active_class();
    void     *shot;

    if (bank_class == 1) {
        /* Bank 0 or nothing swapped: the retail path, byte for byte, told to whoever listens for
         * the player's own shots first. A class 1 spawn is the player firing unless the AI made
         * it for an ally; enemies pass their own class, and the muzzle flare, sub-shots, chunks
         * and blast spheres pass 0. */
        mp_shot_whose_t whose  = whose_shot(shooter_class, caller);
        uint32_t        object = 0;

        if (shot_listener != NULL && muzzle != NULL && whose == MP_SHOT_PLAYERS) {
            shot_listener(kind, (const float *)muzzle, pitch, yaw);
        }
        shot = ((shot_spawn_fn_t)shots.hull.original)(kind, muzzle, pitch, yaw, shooter_class);
        if (shot != NULL && !memory_try_read_u32((uintptr_t)shot + SHOT_NODE_OBJECT, &object)) {
            object = 0u;   /* a read that faulted part way may have left bytes behind */
        }
        /* Every shot, first: the object may be one a remembered bolt flew as, and a sub shot of
         * an impact is told here and nowhere else, since its class 0 is nobody's below. */
        if (shot_made_listener != NULL && object != 0u) {
            shot_made_listener(kind, shooter_class, object);
        }
        if (whose == MP_SHOT_PLAYERS) {
            ++shots.player_shots;
            if (shot_object_listener != NULL && object != 0u) {
                shot_object_listener(object);
            }
        }
        if (whose == MP_SHOT_NPCS && shooter_class == 1) {
            ++shots.ally_shots;
            if (ally_shot_listener != NULL && object != 0u) {
                ally_shot_listener(object);
            }
        }
        /* An NPC's bolt, an ally's included, told once the engine has made it, with the object
         * a contact will name as its sender. */
        if (npc_shot_listener != NULL && shot != NULL && muzzle != NULL &&
            whose == MP_SHOT_NPCS) {
            npc_shot_listener(kind, (const float *)muzzle, pitch, yaw, shooter_class, object);
        }
        return shot;
    }

    /* A swapped bank fires: call the original as class 1 so its three class-1 gates fall the
     * player's way, then stamp the real class onto the object so the pair filter reads it. */
    shot = ((shot_spawn_fn_t)shots.hull.original)(kind, muzzle, pitch, yaw, 1);
    if (shot != NULL) {
        uint32_t object = 0;

        /* The node's body is at +0xA0; its head is the next node in the active-shot list. Reading
         * the head instead left every shot of the far body on side 1, so the pair pass no longer
         * let it miss a body of class 5 and the bolt died in the muzzle it left, and the write
         * landed in the NEIGHBOURING node's ballistic anchor, which took that bolt with it: the
         * previous node's +0x08 is the y of its arc origin, and the bit pattern 5 there is a
         * denormal, effectively zero, so a bolt already in flight left the world. The field
         * report was "the bolts explode in the weapon and no projectiles fly", one wrong offset.
         * The side fault counter would have read zero before the fix all the same, because the
         * wrong pointer read successfully; a counter only answers the question it was given. */
        if (memory_try_read_u32((uintptr_t)shot + SHOT_NODE_OBJECT, &object) && object != 0) {
            if (patch_write_u32((uintptr_t)object + BAPOBJ_SHOOTER_CLASS, (uint32_t)bank_class)
                    != PATCH_RESULT_OK) {
                ++shots.side_faults;
            }
            if (shot_made_listener != NULL) {
                shot_made_listener(kind, bank_class, object);
            }
        } else {
            ++shots.side_faults;
        }
    }
    return shot;
}

bool mp_body_shot_install(uintptr_t site, size_t prologue)
{
    if (!detour_install(&shots.hull, site, (const void *)&shot_hook, prologue)) {
        return false;
    }
    shots.installed = true;
    (void)mp_shot_sites_resolve(site);
    return true;
}

uint32_t mp_body_shot_side_faults(void)
{
    return shots.side_faults;
}

void mp_body_shot_report(void)
{
    log_info("  class 1 shots at bank 0: %u the player's own, %u an ally's out of the AI (its nine "
             "calls %s), %u with a caller outside the game's image",
             (unsigned)shots.player_shots, (unsigned)shots.ally_shots,
             mp_shot_sites_resolved() ? "known" : "NOT known, so every one counted as the player's",
             (unsigned)shots.foreign_returns);
}
