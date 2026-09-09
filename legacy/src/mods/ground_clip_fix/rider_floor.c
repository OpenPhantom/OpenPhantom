/* rider_floor.c: see rider_floor.h. */
#include "rider_floor.h"

#include "creeping_mover.h"

#include "common/detour.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/signature.h"

#include <stdint.h>

#define GROUND_CLIP_SECTION "ground_clip_fix"

/* --- bapmap_carryRider 0x0040AF4C -------------------------------------------------------------- *
 *   55 8B EC              push ebp / mov ebp,esp
 *   81 EC 90 00 00 00     sub esp,0x90
 *   33 C0                 xor eax,eax
 *   8B 4D 0C              mov ecx,[ebp+0x0C]      the ground contact, its second argument
 *   83 C1 40              add ecx,0x40            the translation delta, which it clears first
 *   89 01                 mov [ecx],eax
 *
 * Sixteen bytes are already unique; twenty are taken so a build with one instruction different does
 * not match by luck. The nine-byte prologue is an instruction boundary with no relative operand. */
static const uint8_t SIG_CARRY_RIDER[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x33, 0xC0, 0x8B,
    0x4D, 0x0C, 0x83, 0xC1, 0x40, 0x89, 0x01, 0x89
};
#define CARRY_RIDER_PROLOGUE 9u

/* --- move_snapToGround 0x0042ADB4 -------------------------------------------------------------- *
 *   55 8B EC              push ebp / mov ebp,esp
 *   8B 45 08              mov eax,[ebp+0x08]      the character
 *   83 B8 98 00 00 00 02  cmp dword [eax+0x98],2  its move mode, the first of its two exemptions
 *   0F 8D 92 00 00 00     jge  out
 *   8B 4D 08              mov ecx,[ebp+0x08]
 *   83 79 20 0E           cmp dword [ecx+0x20],0xE   the corpse state, the second exemption
 *
 * Twelve bytes are unique; twenty are taken. The six-byte prologue is an instruction boundary. */
static const uint8_t SIG_SNAP_TO_GROUND[] = {
    0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x83, 0xB8, 0x98, 0x00, 0x00, 0x00,
    0x02, 0x0F, 0x8D, 0x92, 0x00, 0x00, 0x00, 0x8B
};
#define SNAP_TO_GROUND_PROLOGUE 6u

/* The ground contact's rider position, and the mover being ridden. A character carries its own
 * contact block at +0xE8, so its mover pointer is that plus the block's own +0x20. */
#define GROUND_DISTANCE           0x00u
#define GROUND_MOVER              0x20u
#define GROUND_RIDER_Z            (0x34u + (2u * sizeof(float)))
#define CHARACTER_GROUND          0xE8u
#define CHARACTER_GROUND_MOVER    (CHARACTER_GROUND + GROUND_MOVER)

#define MOVER_ID      0x08u
#define MOVER_TYPE    0x04u
#define MOVER_CRUSHER 3u        /* kMover_Kervorkian */

typedef void(__cdecl *carry_rider_fn_t)(void *world, uint8_t *ground);
typedef void(__cdecl *snap_to_ground_fn_t)(uint8_t *actor, void *position);

static struct {
    detour_t carry;
    detour_t snap;
    creeping_mover_set_t creeping;
} rider;

static bool is_known_creeper(const uint8_t *mover)
{
    return creeping_mover_known(&rider.creeping, *(const uint32_t *)(mover + MOVER_ID), mover);
}

static bool is_crusher(const uint8_t *mover)
{
    return mover != NULL && *(const uint32_t *)(mover + MOVER_TYPE) == MOVER_CRUSHER;
}

/* HALF ONE: a crusher carries nobody.
 *
 * The engine's own body still runs, so every field it maintains is written exactly as before, the
 * deltas included. Only the position it hands back is refused. */
static void __cdecl hook_carry_rider(void *world, uint8_t *ground)
{
    carry_rider_fn_t original = (carry_rider_fn_t)rider.carry.original;
    float            before;
    float            fell;

    if (ground == NULL || !is_crusher(*(const uint8_t *const *)(ground + GROUND_MOVER))) {
        original(world, ground);
        return;
    }

    before = *(const float *)(ground + GROUND_RIDER_Z);
    original(world, ground);

    /* Downward, and slower than any real platform in the game. Both halves of that test matter:
     * a crusher rising with somebody on it is left alone, and so is one travelling at a rate that
     * could actually take them somewhere. */
    fell = before - *(const float *)(ground + GROUND_RIDER_Z);
    if (!creeping_mover_is_creep(fell)) {
        return;
    }
    *(float *)(ground + GROUND_RIDER_Z) = before;

    {
    const uint8_t *mover = *(const uint8_t *const *)(ground + GROUND_MOVER);

    if (creeping_mover_note(&rider.creeping, *(const uint32_t *)(mover + MOVER_ID), mover)) {
        log_info("crusher %u creeps down at %.5f of a unit a tick and is no longer carrying "
                 "characters. At that rate it is transporting nobody anywhere, and the only "
                 "thing carrying a rider on it achieves is to sink them through the floor. Every "
                 "genuine platform in the shipped levels moves at least twenty times faster and "
                 "is untouched",
                 (unsigned)*(const uint32_t *)(mover + MOVER_ID), (double)fell);
    }
    }
}

/* HALF TWO, and without it the first half achieves nothing.
 *
 * Refusing the carry only stops one of the two ways a crusher takes a character down with it. The
 * ground snap is the other: it pulls an actor onto any floor within 0.35 units below their feet,
 * every tick, and the crusher's own collision polygon is that floor. So a character released by the
 * carry is simply snapped back down onto the descending polygon a moment later, and follows it just
 * the same, only more slowly. Measured: refusing the carry alone reduced the fall from five
 * centimetres to one and a half, and the watch then named this function as the writer of every
 * remaining step.
 *
 * The exemption is the shape this function already uses. It exempts two populations outright, a
 * corpse and anything with a move mode of 2 or more, and both keep their authored Z. A character
 * standing on a crusher the carry has already refused joins them, for the same reason: the
 * surface underneath is not one the engine should be settling anybody onto. */
static void __cdecl hook_snap_to_ground(uint8_t *actor, void *position)
{
    snap_to_ground_fn_t original = (snap_to_ground_fn_t)rider.snap.original;

    /* The same mover the carry refused, recognised by the set it recorded, and only while the
     * floor is BELOW the feet: a negative distance means the snap would pull the character down
     * onto the creeping surface, while a positive one would lift them, which is the engine
     * putting somebody back on top of something and is left alone. */
    if (actor != NULL &&
        is_crusher(*(const uint8_t *const *)(actor + CHARACTER_GROUND_MOVER)) &&
        is_known_creeper(*(const uint8_t *const *)(actor + CHARACTER_GROUND_MOVER)) &&
        *(const float *)(actor + CHARACTER_GROUND + GROUND_DISTANCE) < 0.0f) {
        return;
    }
    original(actor, position);
}

bool rider_floor_install(void)
{
    uintptr_t carry;
    uintptr_t snap;

    if (!ini_read_bool(GROUND_CLIP_SECTION, "CrusherCarry", true)) {
        log_info("CrusherCarry=0, so a crusher carries and settles characters exactly as the "
                 "engine shipped it, including down through the floor drawn under them");
        return false;
    }

    carry = signature_find_detour_target(SIG_CARRY_RIDER, NULL, sizeof SIG_CARRY_RIDER,
                                         CARRY_RIDER_PROLOGUE);
    snap  = signature_find_detour_target(SIG_SNAP_TO_GROUND, NULL, sizeof SIG_SNAP_TO_GROUND,
                                         SNAP_TO_GROUND_PROLOGUE);
    if (carry == 0 || snap == 0) {
        log_warning("the rider carry %s and the ground snap %s, so a crusher still takes "
                    "characters down with it. The contact guard is unaffected",
                    (carry != 0) ? "was found" : "was NOT found",
                    (snap != 0) ? "was found" : "was NOT found");
        return false;
    }

    /* BOTH OR NEITHER. Either half on its own leaves the fault in place, the first because the snap
     * puts the character back onto the descending polygon and the second because the carry moves it
     * there first, so a partial install would report success and change nothing. */
    if (!detour_install(&rider.carry, carry, (const void *)hook_carry_rider,
                        CARRY_RIDER_PROLOGUE)) {
        log_warning("the rider carry at %08X could not be detoured, so a crusher still takes "
                    "characters down with it", (unsigned)carry);
        return false;
    }
    if (!detour_install(&rider.snap, snap, (const void *)hook_snap_to_ground,
                        SNAP_TO_GROUND_PROLOGUE)) {
        log_warning("the ground snap at %08X could not be detoured. The rider carry is already "
                    "hooked and stays so, which on its own slows the fall rather than stopping it",
                    (unsigned)snap);
        return false;
    }

    log_info("a crusher no longer takes characters down with it, guarded at both places it did: "
             "the rider carry at %08X and the ground snap at %08X. Measured in the final level's "
             "opening, where a crusher's collision polygon sits at floor height, is selected "
             "as the floor two characters are standing on, and descends five centimetres through "
             "the floor that is actually drawn there", (unsigned)carry, (unsigned)snap);
    return true;
}
