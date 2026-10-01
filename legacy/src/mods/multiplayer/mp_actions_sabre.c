/* mp_actions_sabre.c: four hulls on the engine's sabre functions, one listener.
 *
 * The swing starter is the one function that arms a swing: every chain link, both spins, the
 * running attack and the midair one enter it with a row of the swing table, so one hull catches
 * them all and the row is the whole operand. The deflect and the parry choose their clip inside
 * the call and answer 1 only when it plays, so their hulls run the original first and read the
 * overlay clip off the body afterwards; an answer of 0 is a refusal and not an event. The swing
 * end clears the blade's contact sphere and is entered by every end of every sabre action, but
 * also by the savegame restore and the level teardown, so its hull only notes a disarm when the
 * blade was armed at the moment of the call.
 *
 * Three of the four heads carry the dev overlay's branch whenever that DLL is on. The resolver
 * found them by their tails in that case, the installer chains in front of the branch, and the
 * player is read through its own cell rather than out of any operand in a prologue, because in
 * a chained head those bytes are another module's jump distance.
 */
#include "mp_actions_sabre.h"

#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_events.h"
#include "mp_signatures.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The object the player record names, and the two of its fields the hulls read: the contact code
 * the blade's sphere carries while armed, and the overlay clip the deflect or parry started.
 * Where each offset was read from: +0x0C is the `8B 48 0C` (ecx = [eax+0xC]) the swing end loads
 * right after its prologue, having loaded the record into eax; +0xAC is the field that same
 * function stores zero into to switch the sphere off; +0xF4 is where the engine's overlay clip
 * starter at 0x004128C1 stores the clip it was handed, and both the deflect and the parry start
 * their clip through it. */
#define PLAYER_RECORD_OBJECT 0x0Cu
#define BAP_OBJ_CONTACT_CODE 0xACu
#define BAP_OBJ_OVERLAY_CLIP 0xF4u

/* The swing table has 28 rows; a row past it would read behind the table on the far side. The
 * table sits at 0x004B4E00 in the retail image, 0x20 bytes a row, and the row the starter is
 * handed is the argument in [ebp+8]. */
#define SWING_TABLE_LAST_ROW 27

/* The four prototypes, all cdecl with the caller cleaning up. The swing starter takes its row on
 * the stack; the deflect and the parry answer 1 when their clip plays. Byte confirmed: the swing
 * starter's caller at 0x0044BF40 calls it and cleans up with `add esp, 4` at 0x0044BF45; the
 * deflect and the parry take no argument and answer in eax, and their callers test that answer
 * against 1, which is what the hulls test. */
typedef void(__cdecl *start_swing_fn_t)(int32_t row);
typedef void(__cdecl *clear_swing_fn_t)(void);
typedef int32_t(__cdecl *block_fn_t)(void);

typedef enum sabre_hull {
    SABRE_HULL_SWING,
    SABRE_HULL_DISARM,
    SABRE_HULL_BLOCK,
    SABRE_HULL_PARRY,
    SABRE_HULL_COUNT
} sabre_hull_t;

typedef struct mp_actions_sabre_state {
    bool                        installed;
    size_t                      standing;
    detour_t                    hull[SABRE_HULL_COUNT];
    mp_actions_sabre_listener_t listener;
    uint32_t                    caught;
    uint32_t                    refused;
    bool                        refused_logged;
} mp_actions_sabre_state_t;

static mp_actions_sabre_state_t sabre;

_Static_assert((size_t)SABRE_HULL_COUNT == (size_t)MP_ACTIONS_SABRE_HULLS,
               "the hull table and the public hull count differ");

size_t   mp_actions_sabre_installed(void) { return sabre.standing; }
uint32_t mp_actions_sabre_caught(void)    { return sabre.caught; }
uint32_t mp_actions_sabre_refused(void)   { return sabre.refused; }

/* Bank 0 is the local player. A swapped bank is the puppet being made to act by the far player's
 * event, and noting that would bounce the event straight back to its sender. */
static bool local_player_acting(void)
{
    return mp_armed_transport() && mp_bank_active_class() == 1;
}

static void refuse(const char *why)
{
    ++sabre.refused;
    if (!sabre.refused_logged) {
        sabre.refused_logged = true;
        log_warning("a sabre action was not sent because %s; later ones are counted, not logged",
                    why);
    }
}

static void note(uint8_t action, uint8_t operand)
{
    if (sabre.listener != NULL) {
        sabre.listener(action, operand);
        ++sabre.caught;
    }
}

/* The body object behind the player record, through the record cell the engine itself reads. */
static bool read_player_object(uint32_t *object)
{
    uintptr_t pr_cell = mp_cells_address(MP_CELL_PR);
    uint32_t  record  = 0;

    return pr_cell != 0 && memory_read_u32(pr_cell, &record) && record != 0 &&
           memory_read_u32(record + PLAYER_RECORD_OBJECT, object) && *object != 0;
}

/* A block or a parry whose clip plays: the clip is what travels, read off the object after the
 * original chose and started it. */
static void note_overlay(uint8_t action)
{
    uint32_t object = 0;
    uint32_t clip   = 0;

    if (!read_player_object(&object) || !memory_read_u32(object + BAP_OBJ_OVERLAY_CLIP, &clip)) {
        refuse("the body behind the player record did not read");
        return;
    }
    if (clip > 255u) {
        refuse("the overlay clip ordinal does not fit the wire's byte");
        return;
    }
    note(action, (uint8_t)clip);
}

/* One hull on the starter is every swing. A census of the writers of the four contact fields on
 * the player body (the node at +0xA8, the code at +0xAC, the radius at +0xB0 and the direction
 * class at +0xB4) finds one armer, this starter, and one disarmer, the swing end below; the
 * deflect and the parry arm the node, the radius and code 0x1C themselves but never the
 * direction class. The starter has eighteen call sites: the midair attack aux (row 24), the sabre
 * action selector (eight rows), the sabre attack tick (three, the chain links), the Panaka entry
 * (two) and the Panaka update (four).
 *
 * The row is noted before the original runs, because nothing about it changes inside the call
 * and the order costs nothing. */
static void __cdecl hook_start_swing(int32_t row)
{
    if (local_player_acting()) {
        if (row < 0 || row > SWING_TABLE_LAST_ROW) {
            refuse("the swing row lies past the 28 row table");
        } else {
            note(MP_SABRE_SWING, (uint8_t)row);
        }
    }
    ((start_swing_fn_t)sabre.hull[SABRE_HULL_SWING].original)(row);
}

/* The disarm is noted before the original runs, because the original is what clears the code
 * this reads: a blade whose contact code is already 0 is not being disarmed, it is a restore or
 * a teardown passing through.
 *
 * The whole body of the swing end at 0x004509BB, which is where the field offsets come from:
 *
 *   004509BB  55 8B EC A1 20 52 4B 00                  push ebp / mov ebp,esp / eax = pr
 *   004509C3  8B 48 0C                                 ecx = pr->hActor
 *   004509C6  C7 81 B4 00 00 00 00 00 00 00            obj->swingDirClass = 0
 *   004509D0  8B 15 .. 8B 42 0C C7 40 0C 29 00 00 00   obj->impactCode = 0x29, unarmed identity
 *   004509E0  8B 0D .. 8B 51 0C C7 82 A8 00 00 00 ..   obj->contactNode = 0
 *   004509F3  A1 ..    8B 48 0C C7 81 AC 00 00 00 ..   obj->contactCode = 0, the sphere is off
 *   00450A05  8B 15 .. 8B 42 0C C7 80 B0 00 00 00 ..   obj->contactRadius = 0
 *   00450A18  8B 0D .. 8B 51 0C 52 E8 A5 23 FC FF      call 0x412DCC(obj), the render bit
 *   00450A27  83 C4 04 5D C3
 *
 * Its callers: the savegame restore at 0x00447AB1, the two block auxes, the midair aux's end, the
 * sabre attack tick, the Panaka update (twice) and the player suspend at 0x00450F25. Every end of
 * every sabre action passes through here, and so do the restore and the suspend. */
static void __cdecl hook_clear_swing_contact(void)
{
    if (local_player_acting()) {
        uint32_t object = 0;
        uint32_t code   = 0;

        if (read_player_object(&object) &&
            memory_read_u32(object + BAP_OBJ_CONTACT_CODE, &code) && code != 0u) {
            note(MP_SABRE_DISARM, 0u);
        }
    }
    ((clear_swing_fn_t)sabre.hull[SABRE_HULL_DISARM].original)();
}

/* After the deflect answers 1 the block clip is in obj+0xF4 and pr+0x64 holds the deflect's
 * continuation (the store at 0x0044E142 is `C7 41 64 44 BC 44 00`). The clip is read after the
 * original because the original chooses it against the incoming bolt; the ordinals seen on a
 * sender are 0x60 to 0x69. An answer of 0 is an aux already running, the shield timer out, or
 * the dev overlay's bladeless refusal in front of this hull, and none of those is an event. */
static int32_t __cdecl hook_start_block_shot(void)
{
    bool    local  = local_player_acting();
    int32_t result = ((block_fn_t)sabre.hull[SABRE_HULL_BLOCK].original)();

    if (local && result == 1) {
        note_overlay(MP_SABRE_BLOCK);
    }
    return result;
}

/* The same after the parry answers 1: the clip in obj+0xF4 is one of 0x5D to 0x5F and pr+0x64
 * holds the parry's continuation (the store at 0x0044E26F is `C7 42 64 A0 BC 44 00`). The dev
 * overlay's parry guard sits in front of this hull in either load order and refuses before the
 * original for a rig without a blade, which is exactly an answer of 0 here. */
static int32_t __cdecl hook_block_attack(void)
{
    bool    local  = local_player_acting();
    int32_t result = ((block_fn_t)sabre.hull[SABRE_HULL_PARRY].original)();

    if (local && result == 1) {
        note_overlay(MP_SABRE_PARRY);
    }
    return result;
}

typedef struct sabre_site {
    mp_site_t   site;
    const void *hook;
    const char *what;        /* for the log line, in the player's terms */
    bool        needs_body;  /* reads the object behind the player record */
} sabre_site_t;

/* In hull order. The hook pointers are stored as data because that is what the detour installer
 * takes; each is called back only through the prototype declared above it.
 *
 * The four sites in the retail image, with the prologue each hull copies. Every length is an
 * instruction boundary with no relative operand inside it:
 *
 *   0044E858  55 8B EC A1 [20 52 4B 00]       push ebp / mov ebp,esp / eax = pr    0/1/3/8
 *   004509BB  55 8B EC A1 [20 52 4B 00]       the same shape                       0/1/3/8
 *   0044DBC6  55 8B EC 81 EC 98 00 00 00      push ebp / mov ebp,esp / sub esp,98h 0/1/3/9
 *   0044E166  55 8B EC 83 EC 0C               push ebp / mov ebp,esp / sub esp,0Ch 0/1/3/6
 *
 * The bytes are identical on the alternate link, the two boxed retail images and the German
 * retail image; the recompile moves every one of them by 0x60 and resolves all four the same
 * way. Two of the prologues carry the player pointer as an operand, and in a chained head those
 * four bytes are the other module's jump distance, which is why no hull reads the player out of
 * a prologue and every one goes through the record cell instead.
 *
 * The loader takes the mod DLLs in case insensitive name order, so the dev overlay loads before
 * this one and three of the four heads already carry its branch when this installer runs: the
 * swing starter, the deflect and the parry. The installer chains in front of an E9 it finds on a
 * head and copies the prologue into a trampoline otherwise; `original` is the previous hook in
 * the first case and the trampoline in the second, and the hook calls it the same way either
 * way. Neither load order breaks the catch. The swing end is clean: nothing else in the tree
 * names it. */
static const sabre_site_t SABRE_SITES[SABRE_HULL_COUNT] = {
    { MP_SITE_PLR_START_SWING,         (const void *)&hook_start_swing,         "swings",
      false },
    { MP_SITE_PLR_CLEAR_SWING_CONTACT, (const void *)&hook_clear_swing_contact, "swing ends",
      true },
    { MP_SITE_PLR_START_BLOCK_SHOT,    (const void *)&hook_start_block_shot,    "blocks",
      true },
    { MP_SITE_PLR_BLOCK_ATTACK,        (const void *)&hook_block_attack,        "parries",
      true }
};

size_t mp_actions_sabre_install(mp_actions_sabre_listener_t listener)
{
    bool   body_readable;
    size_t index;

    if (sabre.installed) {
        return sabre.standing;
    }
    if (listener == NULL) {
        log_warning("the sabre hulls are not installed: nothing would receive what they catch");
        return 0u;
    }
    sabre.installed = true;
    sabre.listener  = listener;

    /* Three of the hulls read the body behind the player record. Without the record cell they
     * could catch nothing right, so they are left off rather than refusing once per call. */
    body_readable = mp_cells_address(MP_CELL_PR) != 0;

    for (index = 0; index < (size_t)SABRE_HULL_COUNT; ++index) {
        const sabre_site_t *entry = &SABRE_SITES[index];
        uintptr_t           site  = mp_signatures_address(entry->site);

        if (site == 0) {
            log_warning("the local player's sabre %s do not reach the far side: their site did "
                        "not resolve", entry->what);
            continue;
        }
        if (entry->needs_body && !body_readable) {
            log_warning("the local player's sabre %s do not reach the far side: the player record "
                        "cell is unknown, so the hull could not read the blade or the clip",
                        entry->what);
            continue;
        }
        if (!detour_install(&sabre.hull[index], site, entry->hook,
                            mp_signatures_prologue(entry->site))) {
            log_warning("the local player's sabre %s do not reach the far side: the hull did not "
                        "take at %08X", entry->what, (unsigned)site);
            continue;
        }
        ++sabre.standing;
        log_info("the local player's sabre %s are caught at %08X%s", entry->what, (unsigned)site,
                 sabre.hull[index].chained ? ", in front of another module's branch" : "");
    }
    return sabre.standing;
}
