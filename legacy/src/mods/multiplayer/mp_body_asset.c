/* mp_body_asset.c: the actor asset a far body wears; the header carries why each step exists. */
#include "mp_body_asset.h"

#include "mp_cells.h"
#include "mp_signatures.h"
#include "mp_signatures_puppet.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The four character tag the resource layer files actors under, as the spawn pushes it. */
#define ACTOR_TAG 0x42414653u   /* 'BAFS' */

/* The clip count in the actor's own header, the field the wire's ordinals are resolved against. */
#define ACTOR_NUM_CLIPS 0xC8u

/* What an asset has to carry, and the two ordinals are not the same kind of requirement.
 *
 * player_spawnHero ends with a play of ordinal zero on the new body, every spawn, whatever the
 * asset. The clip player range checks that ordinal against the actor's own count and answers
 * nothing when it is out of range, so an empty clip table does not end the process; it produces a
 * body with no track at all, which every later placement then writes a position onto. A census of
 * the shipped actors puts eight of 265 in that class. That is the refusal below.
 *
 * The replica death plays ordinal 0x19, the forward death clip, on a far body whose owner reports
 * itself dead. That one is range checked as well, and it is deliberately NOT a refusal: 258 of the
 * 265 shipped actors carry fewer clips than that, so refusing on it would refuse nearly every
 * asset this module exists to carry. What an asset below it costs is a death with no death clip,
 * and the death is still visible because the shadow and the collision go away with it. Said once
 * and counted, so the field can see how often it happens before anyone raises the bar.
 *
 * The one clip play in the image that can end the process is the overlay player: its range test is
 * an assert with no return in front of the dereference, and it is off by one, so an ordinal equal
 * to the count walks past the table. The spawn never calls it. This feature reaches it two ways:
 * directly, where the state path and the block refuse an ordinal at or past the count before the
 * call, and through three engine starters that choose their clip themselves, the weapon setter's
 * draw clips 0x20 to 0x23, the force push's 0x71 and the midair swing's 0x55, which the puppet
 * asks mp_puppet_starter about before each call. Every hero carries the draw clips, and only the
 * two Jedi carry the other two. So it is a condition on the call and not on the asset, and it is
 * not tested here.
 *
 * The evidence. The clip player at 0x0041263F opens with an assert on a null actor and then
 * tests `clip < [pActor+0xC8]`, answering 0 otherwise, which is the range check and where the
 * clip count offset was read from; the overlay player at 0x004128C1 tests `[pActor+0xC8] < clip`
 * at 0x004128FA through the assert service and then indexes the table. The census over the 265
 * shipped actors, reading that same count: 8 carry no clip at all, 258 carry fewer than 26, the
 * two jedi carry 114 each, Panaka 83, the Queen 71, Maul 37, Jar Jar 19 and a battle droid 8.
 *
 * A second lethal site was claimed for a non hero asset, the node child hide at 0x00414147 that
 * the spawn calls over the record's +0x40, and refuted: it loads the render handle at +0x9C,
 * tests the index against the node count at [[handle+4]+0x54] and returns 0 when it is past,
 * and the index it is given comes from a usage lookup that answers 0 when the usage is not
 * found. A small model reaches it with 0 and it hides node 0's children; nothing ends. */
#define SPAWN_CLIP_ORDINAL 0u
#define REPLICA_DEATH_CLIP 0x19u

typedef void *(__cdecl *res_alloc_fn_t)(uint32_t tag, const char *name);
typedef int32_t(__cdecl *res_free_fn_t)(void *data);

typedef struct mp_body_asset_state {
    bool           resolved;
    uintptr_t      name_table;
    res_alloc_fn_t res_alloc;
    res_free_fn_t  res_free;      /* NULL is a log line, not a refusal */

    /* At most one reference outstanding, and it is handed back after the bind that uses it. */
    void          *probe;

    /* The storage a borrowed slot points at. The engine keeps the pointer rather than copying the
     * string, so it has to be memory of ours; the borrow is closed inside the same call, but a
     * buffer on the stack would still be the wrong shape for a table the engine holds. */
    char           lent_name[MP_ACTOR_NAME_BYTES];
    bool           lent_open;
    int32_t        lent_slot;
    uint32_t       lent_previous;

    uint32_t       probes;
    uint32_t       refused_missing;
    uint32_t       refused_clips;
    uint32_t       no_death_clip;
    uint32_t       borrows;
    uint32_t       borrow_faults;
    uint32_t       bound_checks;
    uint32_t       bound_mismatches;
    bool           bound_logged;
    bool           death_clip_logged;
    bool           free_missing_logged;
} mp_body_asset_state_t;

static mp_body_asset_state_t assets;

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

/* The site is one instruction run inside the spawn at 0x00447E58. Retail bytes at 0x00447ECF,
 * where the pattern resolves in all five retail builds:
 *
 *   00447ECF  A1 20 52 4B 00              mov  eax, [004B5220]         the player record
 *   00447ED4  8B 48 6C                    mov  ecx, [eax+0x6C]         the hero index
 *   00447ED7  8B 14 8D 80 51 4B 00        mov  edx, [ecx*4+004B5180]   the name
 *   00447EDE  52                          push edx
 *   00447EDF  68 53 46 41 42              push 'BAFS'
 *   00447EE4  E8 57 A1 02 00              call 00472040                res_Alloc
 *
 * The call target checks out (0x00447EE9 + 0x0002A157 = 0x00472040). An earlier account put the
 * site at 0x00447ECD, two bytes early, inside the tail of the previous instruction; the table
 * read is site + 8 against this start. The recompile moves the site to 0x00447E6F and the
 * release to 0x004721BF, which is the evidence the whole signature design exists for.
 *
 * The table at 0x004B5180 holds four pointers, to "obiwan.baf", "quigon.baf", "panaka.baf" and
 * "queen.baf". It has two readers in the whole image: this one, and 0x00447B58 in the savegame
 * restore, which loads the record through the `8B 15` form and does not answer the pattern. */
bool mp_body_asset_install(void)
{
    uintptr_t site;
    uintptr_t loader = 0;
    uint32_t  table = 0;

    if (assets.resolved) {
        return true;
    }

    site = mp_signatures_address(MP_SITE_HERO_ASSET_TABLE);
    if (site == 0) {
        log_warning("the hero asset table did not resolve, so a far body can only wear the hero "
                    "this machine's own player wears");
        return false;
    }
    if (!memory_read_u32(site + MP_HERO_ASSET_SITE_TABLE, &table) || table == 0u) {
        log_warning("the hero asset table operand at %08X did not read",
                    (unsigned)(site + MP_HERO_ASSET_SITE_TABLE));
        return false;
    }
    if (!patch_read_call_target(site + MP_HERO_ASSET_SITE_RES_ALLOC, &loader) || loader == 0) {
        log_warning("no usable call to the resource loader at %08X",
                    (unsigned)(site + MP_HERO_ASSET_SITE_RES_ALLOC));
        return false;
    }
    /* The whole table is read once here rather than one entry at a time later, because a range
     * that is only partly there is exactly the case a check of the first entry would pass. */
    if (!memory_is_readable_range((uintptr_t)table, 4u * MP_BODY_ASSET_SLOTS)) {
        log_warning("the hero asset table at %08X is not four readable pointers", (unsigned)table);
        return false;
    }

    assets.name_table = (uintptr_t)table;
    assets.res_alloc  = (res_alloc_fn_t)loader;
    assets.res_free   = (res_free_fn_t)mp_signatures_address(MP_SITE_RES_FREE);
    assets.resolved   = true;

    if (assets.res_free == NULL) {
        log_warning("the resource release did not resolve, so the reference each proof takes "
                    "cannot be handed back and every proof raises that asset's use count for "
                    "good; a far body's asset still works and an unknown name is still refused");
    }
    log_info("the far bodies' asset layer stands: name table at %08X, loader at %08X, release %s",
             (unsigned)table, (unsigned)loader, assets.res_free != NULL ? "resolved" : "MISSING");
    return true;
}

bool mp_body_asset_ready(void)
{
    return assets.resolved;
}

/* ==============================================================================================
 * Names.
 * ============================================================================================ */

/* One rule in one place: at most `bytes` out, cut at the first zero byte of the source, lower
 * cased. Both callers below read a 32 byte field that is not required to be terminated. */
static bool copy_name(const char *raw, size_t raw_bytes, char *out, size_t bytes)
{
    size_t index;

    if (out == NULL || bytes == 0u) {
        return false;
    }
    out[0] = '\0';
    for (index = 0; index + 1u < bytes && index < raw_bytes; ++index) {
        char c = raw[index];

        if (c == '\0') {
            break;
        }
        out[index] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    out[index] = '\0';
    return out[0] != '\0';
}

bool mp_body_asset_name_of(uint32_t actor, char *out, size_t bytes)
{
    char raw[MP_ACTOR_NAME_BYTES];

    if (actor == 0u || !memory_try_read((uintptr_t)actor + MP_ACTOR_NAME, raw, sizeof raw)) {
        if (out != NULL && bytes != 0u) {
            out[0] = '\0';
        }
        return false;
    }
    return copy_name(raw, sizeof raw, out, bytes);
}

bool mp_body_asset_slot_name(int32_t slot, char *out, size_t bytes)
{
    uint32_t pointer = 0;
    char     raw[MP_ACTOR_NAME_BYTES];

    if (out != NULL && bytes != 0u) {
        out[0] = '\0';
    }
    if (!assets.resolved || slot < 0 || slot >= MP_BODY_ASSET_SLOTS) {
        return false;
    }
    if (!memory_read_u32(assets.name_table + 4u * (uintptr_t)slot, &pointer) || pointer == 0u ||
        !memory_try_read((uintptr_t)pointer, raw, sizeof raw)) {
        return false;
    }
    return copy_name(raw, sizeof raw, out, bytes);
}

/* ==============================================================================================
 * The proof.
 * ============================================================================================ */

const char *mp_body_asset_verdict_text(mp_body_asset_verdict_t verdict)
{
    switch (verdict) {
    case MP_BODY_ASSET_OK:         return "loadable";
    case MP_BODY_ASSET_NOT_FOUND:  return "unknown to the resource layer";
    case MP_BODY_ASSET_NO_CLIPS:   return "loaded but carrying no clip the spawn can play";
    case MP_BODY_ASSET_NO_ANCHORS:
    default:                       return "unprovable, the asset layer did not install";
    }
}

void mp_body_asset_release_probe(void)
{
    if (assets.probe != NULL && assets.res_free != NULL) {
        (void)assets.res_free(assets.probe);
    } else if (assets.probe != NULL && !assets.free_missing_logged) {
        assets.free_missing_logged = true;
        log_warning("a far body's asset reference cannot be handed back on this build, so its "
                    "use count stays one higher than it was; later cases are silent");
    }
    assets.probe = NULL;
}

/* The loader at 0x00472040, read whole: it normalises the name at 0x0047205E, looks the node up
 * at 0x00472075, and the `je` at 0x00472083 on a miss jumps straight to the return of 0, PAST
 * the use count increment at 0x00472092 (the word at node+0x1E); a hit increments, registers the
 * node at 0x004720AB and answers node+0x14. So a miss raises no counter, which an earlier
 * account had wrong, and a refused probe leaves no debt. The release at 0x0047221F decrements
 * the same word and, when it reaches 0 and bit 0 of node+0x1C is set, frees the node at once,
 * which is why the new reference is taken before the old one is handed back.
 *
 * A function at 0x004722BA calls the same lookup and returns the data without touching the
 * counter, and reads exactly like a reference free existence test. It is not usable: it loads
 * the resource all the same, leaves the node at count 0 where the layer's own sweep may evict
 * it, and skips the normalisation, so it does not find a name spelt any other way.
 *
 * Why an unproved name is a program end: the bind at 0x00412458 stores the actor at 0x00412468
 * unconditionally, calls the assert service at 0x0041246B when it is null, and dereferences it
 * at 0x0041249F with no return in between. The service slot at +0x18 of the service record is
 * filled with a routine that ends with a message box and then the exit the C runtime's own exit
 * calls. The spawn hands the loader's result to the bind with no null test of its own. */
mp_body_asset_verdict_t mp_body_asset_probe(const char *name, void **asset_out)
{
    void    *asset;
    uint32_t clips = 0;

    if (asset_out != NULL) {
        *asset_out = NULL;
    }
    if (!assets.resolved || name == NULL || name[0] == '\0') {
        return MP_BODY_ASSET_NO_ANCHORS;
    }

    ++assets.probes;
    asset = assets.res_alloc(ACTOR_TAG, name);
    if (asset == NULL) {
        /* A miss raises no counter, so there is nothing to give back for this one; the previous
         * proof's reference is stale all the same and goes now. */
        mp_body_asset_release_probe();
        ++assets.refused_missing;
        return MP_BODY_ASSET_NOT_FOUND;
    }

    /* The new reference is in hand before the old one is handed back. Asking twice for the same
     * name would otherwise let its use count touch zero in between, and a node at zero with its
     * evictable bit set is freed on the spot, underneath a body that is playing it. */
    mp_body_asset_release_probe();
    assets.probe = asset;

    if (!memory_try_read((uintptr_t)asset + ACTOR_NUM_CLIPS, &clips, sizeof clips) ||
        clips <= SPAWN_CLIP_ORDINAL) {
        ++assets.refused_clips;
        log_warning("the asset %s carries %u clip(s), and the spawn plays ordinal %u on every "
                    "body it builds, so a body wearing it would have no animation at all",
                    name, (unsigned)clips, (unsigned)SPAWN_CLIP_ORDINAL);
        return MP_BODY_ASSET_NO_CLIPS;
    }

    if (clips <= REPLICA_DEATH_CLIP) {
        ++assets.no_death_clip;
        if (!assets.death_clip_logged) {
            assets.death_clip_logged = true;
            log_warning("the asset %s carries %u clip(s) and the replica death plays ordinal %u, "
                        "so a far player wearing it dies here without a death clip; the shadow "
                        "and the collision still go, and later cases are counted",
                        name, (unsigned)clips, (unsigned)REPLICA_DEATH_CLIP);
        }
    }

    if (asset_out != NULL) {
        *asset_out = asset;
    }
    return MP_BODY_ASSET_OK;
}

/* ==============================================================================================
 * The borrowed slot.
 * ============================================================================================ */

bool mp_body_asset_borrow_open(void)
{
    return assets.lent_open;
}

bool mp_body_asset_lend_name(int32_t slot, const char *name)
{
    uint32_t previous = 0;

    if (!assets.resolved || assets.lent_open || name == NULL || name[0] == '\0' ||
        slot < 0 || slot >= MP_BODY_ASSET_SLOTS) {
        return false;
    }
    if (!memory_read_u32(assets.name_table + 4u * (uintptr_t)slot, &previous)) {
        ++assets.borrow_faults;
        return false;
    }

    /* Written before the table is pointed at it, because the table entry is what makes the buffer
     * reachable and an entry pointing at a half filled buffer is a name nobody chose. */
    (void)text_format(assets.lent_name, sizeof assets.lent_name, "%s", name);

    if (patch_write_pointer32(assets.name_table + 4u * (uintptr_t)slot,
                              (const void *)assets.lent_name) != PATCH_RESULT_OK) {
        ++assets.borrow_faults;
        log_error("hero slot %d could not be pointed at %s, so no far body wears it",
                  (int)slot, assets.lent_name);
        return false;
    }
    assets.lent_open     = true;
    assets.lent_slot     = slot;
    assets.lent_previous = previous;
    ++assets.borrows;
    return true;
}

void mp_body_asset_return_name(int32_t slot)
{
    if (!assets.lent_open || assets.lent_slot != slot) {
        return;
    }
    /* This has to succeed. A table left naming our buffer decides which asset the LOCAL player's
     * next respawn is built out of, and that respawn happens minutes later somewhere else. */
    if (patch_write_pointer32(assets.name_table + 4u * (uintptr_t)slot,
                              (const void *)(uintptr_t)assets.lent_previous) != PATCH_RESULT_OK) {
        ++assets.borrow_faults;
        log_error("hero slot %d could not be given back what it held, so it still names %s and "
                  "the next respawn into that slot would be built out of it",
                  (int)slot, assets.lent_name);
    }
    assets.lent_open = false;
}

/* ==============================================================================================
 * The measurement.
 * ============================================================================================ */

/* The spawn reads the name as [[0x004B5220] + 0x6C] * 4 + 0x004B5180, so the read runs through
 * the player pointer, and inside a bank window that pointer names the bank. The comparison is
 * of the bank block's +0x00 after the window closes against what the probe returned, which is
 * the same pointer the spawn stores there. */
void mp_body_asset_note_bound(size_t index, uint32_t bound_actor, const void *probed)
{
    char bound_name[MP_ACTOR_NAME_BYTES];

    if (probed == NULL) {
        return;   /* nothing was borrowed, so there is nothing to hold the block against */
    }
    ++assets.bound_checks;
    if ((uintptr_t)bound_actor == (uintptr_t)probed) {
        return;
    }
    ++assets.bound_mismatches;
    if (assets.bound_logged) {
        return;
    }
    assets.bound_logged = true;
    if (!mp_body_asset_name_of(bound_actor, bound_name, sizeof bound_name)) {
        bound_name[0] = '\0';
    }
    log_error("bank %u's body was built from actor %08X while the name that was lent to its slot "
              "proves out as %08X%s%s: the borrowed name did not reach the spawn through the "
              "bank, so the body wears something nobody chose; later cases are counted",
              (unsigned)index, (unsigned)bound_actor, (unsigned)(uintptr_t)probed,
              bound_name[0] != '\0' ? ", the built one being " : "", bound_name);
}

void mp_body_asset_report(void)
{
    if (!assets.resolved) {
        return;
    }
    log_info("the far bodies' asset layer: %u name(s) asked for, %u unknown to the resource "
             "layer, %u without the spawn's own clip, %u without the replica death clip",
             (unsigned)assets.probes, (unsigned)assets.refused_missing,
             (unsigned)assets.refused_clips, (unsigned)assets.no_death_clip);
    log_info("  %u name slot borrow(s), %u borrow fault(s), %u bind(s) measured against the "
             "proof with %u disagreeing", (unsigned)assets.borrows,
             (unsigned)assets.borrow_faults, (unsigned)assets.bound_checks,
             (unsigned)assets.bound_mismatches);
    if (assets.lent_open) {
        log_error("  a name slot borrow is still open on slot %d, naming %s",
                  (int)assets.lent_slot, assets.lent_name);
    }
}
