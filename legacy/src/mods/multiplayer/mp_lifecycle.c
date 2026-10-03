/* mp_lifecycle.c: five chained detours, each one line of substance around its original.
 *
 * The shape of every hook is the same: count the entry, take the block loan if a bank is swapped
 * in, call the original exactly as the engine would have, give the loan back, return what the
 * original returned. The loan itself lives in mp_bank, because it is bank state choreography and
 * this file should not know how a bank stores its bytes.
 *
 * Two of them do one thing more once the original has returned unlent. After the player's own
 * spawn, and after a savegame read that succeeded, the hero block holds a Jedi's blade vectors
 * read off a mesh another body may have folded, and mp_blade puts the longest reading of that
 * asset in their place. Outside a session it does nothing.
 *
 * The spawn hull also does one thing before its original. In a session, on an unlent call, it has
 * mp_hero_carry write the incoming hero's record from what the player holds, so that a change of
 * hero keeps the player's quest items, keys, health and general ammunition, and the engine's swap
 * of the six inventory bytes stops rewriting the host's story. Outside a session it does nothing.
 *
 * And the respawn's hull asks its listener before anything, which may answer that the respawn is
 * not to be begun: a client's refusal of a warp a script of its own machine asked for.
 *
 * The prototypes are byte evidence, not guesses: the savegame pair is cdecl with one argument and
 * a status return the single caller tests; respawn and spawn are cdecl with an index, a position
 * pointer and a heading, and every caller clears twelve bytes of stack; despawn takes and returns
 * nothing. Three of the five carry a masked operand inside their own prologue, so on a build
 * where another module has already branched over a head, the resolver's second stage and the
 * from-file operand read are what keep these hulls honest.
 *
 * What the loan does not cover: the reader writes the body pointer cell and reads the task node
 * cell, the respawn reads and writes the checkpoint cells, and all of those live outside the hero
 * block, which is the only thing the loan moves besides the player pointer. For pass through
 * calls that is complete; a deliberate bank call has to decide each outside cell for itself.
 *
 * One other DLL carries a data pattern anchored at the writer's head with required head bytes.
 * It resolves at that DLL's own install, which alphabetical load order puts before this one, so
 * it has its answer before the hull writes the branch; a reordering would cost that DLL one
 * optional display and nothing else.
 */
#include "mp_lifecycle.h"

#include "mp_bank.h"
#include "mp_blade.h"
#include "mp_hero_carry.h"
#include "mp_signatures.h"

#include "common/detour.h"
#include "common/logging.h"

#include <intrin.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int32_t(__cdecl *save_fn_t)(int32_t chunk_arg);
typedef int32_t(__cdecl *restore_fn_t)(int32_t subversion);
typedef void(__cdecl *respawn_fn_t)(int32_t hero_index, const void *at, float heading);
typedef void(__cdecl *spawn_fn_t)(int32_t hero_index, const void *at, float heading);
typedef void(__cdecl *despawn_fn_t)(void);

enum {
    HULL_SAVE,
    HULL_RESTORE,
    HULL_RESPAWN_AT,
    HULL_SPAWN_HERO,
    HULL_DESPAWN,
    HULL_COUNT
};

typedef struct mp_lifecycle_state {
    bool     entered_install;
    size_t   installed;
    detour_t hull[HULL_COUNT];
    uint32_t entries[HULL_COUNT];
    uint32_t entries_lent[HULL_COUNT];
    uint32_t respawns_refused;   /* respawns the listener answered no to, never begun */

    mp_lifecycle_respawn_listener_t respawn_listener;
} mp_lifecycle_state_t;

static mp_lifecycle_state_t lifecycle;

/* The five, in the retail image: the savegame writer at 004479D2 (prologue 9, the block base as
 * its chunk buffer argument), the reader at 00447AB1 (9, the base as its read buffer), the
 * respawn at 00447C90 (10, block +0x4 read and written), the spawn at 00447E58 (6, the base into
 * a `rep stosd` of 235 dwords, the clear that wipes bank 0 whatever the pointer says) and the
 * despawn at 00448201 (8, block +0x4 written). None of the five prologues carries a relative
 * operand inside its first five bytes, so all five are detourable. Every pattern matches once on
 * every build, tails included, whole code section, every alignment; the reader's is thirty bytes
 * rather than twenty because the twenty byte tail has a twin at 00432544, the enemy block
 * restore's identical early out arm with a different frame size, which stage two rejects at the
 * prologue today and would accept the day anything detours that twin's head.
 *
 * The callers, counted over the whole image: the writer and the reader once each, from the
 * player module proc at 00447880; the respawn twice, from the hero swap cheat at 004302AA and the
 * change player opcode at 004350C7; the spawn three times and the despawn three times. The five
 * addresses occur nowhere in the image as data, in any section at any alignment, so those direct
 * calls are the complete list: no table, no computed pointer, no callback slot names them. */
static const char *const hull_names[HULL_COUNT] = {
    "player_save", "player_restore", "player_respawn_at", "player_spawn_hero", "player_despawn"
};

/* The first swapped-in entry per hull is worth a line of its own: until deliberate bank calls
 * exist, a lent call means some path reached a lifecycle function inside a swap window that the
 * census said cannot be reached, and that is a finding whether or not the loan covered it. The
 * census was two measurements: a breadth first walk from the phase runner over the phase list,
 * the fourteen mode descriptors and the aux table, three levels deep across 260 functions,
 * reaches none of the five, and the data scan above proves no indirect path exists without
 * computed arithmetic. */
static void note_entry(int which, bool lent)
{
    ++lifecycle.entries[which];
    if (lent) {
        if (lifecycle.entries_lent[which] == 0) {
            log_warning("%s was entered while a bank was swapped in, and the loan covered it",
                        hull_names[which]);
        }
        ++lifecycle.entries_lent[which];
    }
}

static int32_t __cdecl hook_save(int32_t chunk_arg)
{
    bool    lent = mp_bank_lend_block_begin();
    int32_t answer;

    note_entry(HULL_SAVE, lent);
    answer = ((save_fn_t)lifecycle.hull[HULL_SAVE].original)(chunk_arg);
    mp_bank_lend_block_end(lent);
    return answer;
}

static int32_t __cdecl hook_restore(int32_t subversion)
{
    bool    lent = mp_bank_lend_block_begin();
    int32_t answer;

    note_entry(HULL_RESTORE, lent);
    answer = ((restore_fn_t)lifecycle.hull[HULL_RESTORE].original)(subversion);
    mp_bank_lend_block_end(lent);
    if (!lent && answer == 0) {
        mp_blade_after_local_restore();   /* 0 is the reader's success */
    }
    return answer;
}

/* The listener hears who asked before the respawn is begun: the respawn itself runs over several
 * substeps behind a fade, and the question is only whose call this was. A listener that answers
 * no has the respawn not begun at all. The engine's body returns nothing and its first statement
 * is a test that drops the call in silence, so a call not made is one the callers already live
 * with. A refusal is counted here.
 *
 * engine: void player_respawnAt(i32 heroIndex, const vec3 *at, f32 heading) */
static void __cdecl hook_respawn_at(int32_t hero_index, const void *at, float heading)
{
    uintptr_t caller = (uintptr_t)_ReturnAddress();
    bool      lent;

    if (lifecycle.respawn_listener != NULL &&
        !lifecycle.respawn_listener(caller, hero_index, (const float *)at, heading)) {
        ++lifecycle.respawns_refused;
        return;
    }
    lent = mp_bank_lend_block_begin();
    note_entry(HULL_RESPAWN_AT, lent);
    ((respawn_fn_t)lifecycle.hull[HULL_RESPAWN_AT].original)(hero_index, at, heading);
    mp_bank_lend_block_end(lent);
}

/* The carry runs before the original because the engine's swap inside it stores and loads the
 * six inventory and key bytes; the incoming record has to hold the player's things by then. */
static void __cdecl hook_spawn_hero(int32_t hero_index, const void *at, float heading)
{
    bool lent = mp_bank_lend_block_begin();

    note_entry(HULL_SPAWN_HERO, lent);
    mp_hero_carry_before(hero_index, lent);
    ((spawn_fn_t)lifecycle.hull[HULL_SPAWN_HERO].original)(hero_index, at, heading);
    mp_bank_lend_block_end(lent);
    if (!lent) {
        mp_hero_carry_after();
        mp_blade_after_local_spawn();
    }
}

static void __cdecl hook_despawn(void)
{
    bool lent = mp_bank_lend_block_begin();

    note_entry(HULL_DESPAWN, lent);
    ((despawn_fn_t)lifecycle.hull[HULL_DESPAWN].original)();
    mp_bank_lend_block_end(lent);
}

static bool install_one(int which, mp_site_t site, const void *hook)
{
    uintptr_t address  = mp_signatures_address(site);
    size_t    prologue = mp_signatures_prologue(site);

    if (address == 0) {
        log_warning("the %s hull has no site to stand on and is skipped", hull_names[which]);
        return false;
    }
    if (!detour_install(&lifecycle.hull[which], address, hook, prologue)) {
        log_warning("the %s hull could not be installed at %08X", hull_names[which],
                    (unsigned)address);
        return false;
    }
    return true;
}

size_t mp_lifecycle_install(void)
{
    if (lifecycle.entered_install) {
        return lifecycle.installed;
    }
    lifecycle.entered_install = true;

    if (install_one(HULL_SAVE, MP_SITE_PLAYER_SAVE, (const void *)&hook_save)) {
        ++lifecycle.installed;
    }
    if (install_one(HULL_RESTORE, MP_SITE_PLAYER_RESTORE, (const void *)&hook_restore)) {
        ++lifecycle.installed;
    }
    if (install_one(HULL_RESPAWN_AT, MP_SITE_PLAYER_RESPAWN_AT, (const void *)&hook_respawn_at)) {
        ++lifecycle.installed;
    }
    if (install_one(HULL_SPAWN_HERO, MP_SITE_PLAYER_SPAWN_HERO, (const void *)&hook_spawn_hero)) {
        ++lifecycle.installed;
    }
    if (install_one(HULL_DESPAWN, MP_SITE_PLAYER_DESPAWN, (const void *)&hook_despawn)) {
        ++lifecycle.installed;
    }

    log_info("%u of %u lifecycle hulls stand", (unsigned)lifecycle.installed,
             (unsigned)HULL_COUNT);
    return lifecycle.installed;
}

bool mp_lifecycle_installed(void)
{
    return lifecycle.installed == (size_t)HULL_COUNT;
}

bool mp_lifecycle_set_respawn_listener(mp_lifecycle_respawn_listener_t listener)
{
    lifecycle.respawn_listener = listener;
    return lifecycle.hull[HULL_RESPAWN_AT].installed;
}

void mp_lifecycle_report(const char *why)
{
    int which;

    if (lifecycle.installed == 0) {
        return;     /* nothing stands, and the install already said why */
    }
    log_info("the lifecycle hulls at %s: %u of %u installed", why,
             (unsigned)lifecycle.installed, (unsigned)HULL_COUNT);
    for (which = 0; which < HULL_COUNT; ++which) {
        if (lifecycle.entries[which] != 0 || lifecycle.entries_lent[which] != 0) {
            log_info("  %-18s entered %u time(s), %u of them on a lent block",
                     hull_names[which], (unsigned)lifecycle.entries[which],
                     (unsigned)lifecycle.entries_lent[which]);
        }
    }
    if (lifecycle.respawns_refused != 0u) {
        log_info("  %-18s refused %u time(s) by its listener and not begun",
                 hull_names[HULL_RESPAWN_AT], (unsigned)lifecycle.respawns_refused);
    }
}
