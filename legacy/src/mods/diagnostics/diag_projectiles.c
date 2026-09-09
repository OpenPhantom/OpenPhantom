/* diag_projectiles.c: how many entries are alive on the engine's own projectile list right now.
 *
 * FUN_004524b9 (0x004524b9), decompiled during the field report investigation this file supports:
 * a `do { ... local_10 = local_c; } while (true)` loop over a global linked list, head at
 * DAT_00872fb8, next pointer at the record's own +0x00, walked once per simulation step for every
 * live entry regardless of what kind of object it turns out to be. The record carries gravity,
 * velocity, a lifetime timer and a bounce/impact callback, which is the shape of a generic
 * physics object, not something the engine's own naming ties to "projectile" specifically. Blaster
 * bolts are the confirmed inhabitant; whether anything else (a dropped weapon, a settling ragdoll)
 * shares the same list is exactly the question this measures rather than assumes.
 *
 * Each entry runs its OWN world-collision trace every tick for as long as it is alive, and an
 * earlier stage of the same investigation measured that trace to be dramatically more expensive
 * against the dense geometry of an enclosed lift shaft than the same trace in open space. A count
 * that spikes at the moment a droid dies on a lift platform, rather than at the moment it is
 * created or fought, points straight at whatever the death handler feeds into this same list.
 *
 * No detour, no signature: DAT_00872fb8 is a fixed global this build's own FUN_004524b9 reads as a
 * literal absolute operand, not a relocatable target, and this is a plain periodic read, nothing
 * hooked.
 *
 * ============================== What this census established, and what it refuted ==============
 *
 * Kept here because it is the only file left that reads this list. A DLL called droid_fix used to
 * force-remove entries from it and was deleted once these measurements were in; this is the
 * evidence it was built on and the evidence that removed it.
 *
 * HOW AN ENTRY GETS STUCK, confirmed by decompiling the removal path rather than inferred from the
 * counts. FUN_004524b9 removes an entry one of two ways: an external flag (bit 0x40000000 of the
 * flags word at entry+0x84), or immediately on first contact, but only for an entry that does NOT
 * persist on impact (flag bit 0x2 clear). Bit 0x40000000 is set from exactly one place outside that
 * function, FUN_00454aa1 (0x00454aa1), an event-driven on-contact handler that only ever runs in
 * response to a real collision. A persisting entry that collides has a probabilistic stick outcome
 * (a random roll at 0x00452c8c against a threshold at 0x004a8790); on a stick, FUN_004524b9 zeroes
 * BOTH the entry's velocity (+0x2c/+0x30/+0x34) and its acceleration (+0x38/+0x3c/+0x40). Next-tick
 * position is computed purely from those two, so the entry never moves again, never collides again,
 * and never generates the one event that could flag it for removal. It is event starvation, not a
 * position-staleness check: nothing in the traced call graph compares a remembered position against
 * a current one, and nothing treats a moving surface differently from a static one.
 *
 * That mechanism is real and is still present. Measured live: the list climbs during a fight and
 * then holds, in one session at exactly 57 entries for nine consecutive censuses, in another
 * settling at 38 and holding it for eighteen, clearing only when the level unloads. An entry with
 * the persist bit is the only kind that can reach this state; an ordinary blaster bolt never
 * carries that flag and is removed correctly on its own first hit.
 *
 * WHAT IT COSTS: nothing measurable, and this is the part that was got wrong for a long time. The
 * pileup was believed to cause a severe frame-rate stall at two lift platforms, on the evidence
 * that a fix which force-removed stuck entries did make the stall smaller. It was removing
 * collision traces that were hammering a hook in another DLL. With that hook repaired (see
 * framerate_fix's mover_interpolation.c), holding a full pileup costs nothing at all: 57
 * entries at a flat 30 fps with no mods loaded, 57 entries at a flat 60 fps with the whole mod set
 * loaded and no cleanup running. Do not reintroduce a cleanup for this list on frame-rate grounds
 * without measuring the list against the frame rate first, the way these lines were.
 *
 * A count on its own never answered the question either way. What answered it was correlating the
 * count against per-second frame timing BY TIMESTAMP, so switch this census and diag_frame.c on
 * together. */
#include "diag_projectiles.h"

#include "diag_install.h"
#include "diag_log.h"

#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x0045243E, the only function that both tests the projectile list head and then loads it ---
 *   55 8B EC              push ebp / mov ebp,esp
 *   83 3D <head> 00       cmp  dword ptr [g_projectileListHead], 0
 *   74 10                 je   +0x10
 *   A1 <head>             mov  eax, [g_projectileListHead]
 *   50                    push eax
 *   E8 <rel32>            call the walker
 *
 * The address was written here as a literal. Three builds of this engine ship at the same file
 * size and the globals move between them, so a literal reads a different cell on two of the three
 * and reports numbers that look real. Both operands are masked and the address is read back out of
 * the first, which is the rule this directory is held to.
 *
 * Nineteen bytes, and with both operands wildcarded the pattern still matches exactly once in the
 * retail image; the operand it then reads is 0x00872FB8, the value that used to be written here. */
static const uint8_t SIG_PROJECTILE_HEAD[] = {
    0x55, 0x8B, 0xEC, 0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x74, 0x10, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0xE8
};
static const uint8_t MSK_PROJECTILE_HEAD[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF
};
_Static_assert(sizeof SIG_PROJECTILE_HEAD == sizeof MSK_PROJECTILE_HEAD,
               "the projectile list head pattern and its mask are different lengths");
#define OFFSET_PROJECTILE_HEAD_OPERAND       5u
/* The same cell again, in the mov that follows the compare. Both are read and required to agree,
 * the way cheats_no_fog.c checks its own pair: one operand proves the pattern matched something
 * shaped right, two agreeing prove it matched the function meant. */
#define OFFSET_PROJECTILE_HEAD_OPERAND_TWO 13u

/* Filled in at install from the operand above; 0 means the site did not resolve and the census
 * declines rather than reading a cell it guessed at. */
static uintptr_t projectile_list_head;

/* Where that address was read from, so the log can say so rather than assert it. */
static uintptr_t projectile_head_site;
#define PROJECTILE_NEXT_OFFSET       0x00u
#define PROJECTILE_POSITION_OFFSET   0x04u   /* float[3], world x/y/z */

/* A cap on the walk itself, not a belief about how many can really be alive: a corrupted or
 * cyclic list must not turn a periodic report into a hang. */
#define PROJECTILE_WALK_MAX 8192u

/* Short on purpose, unlike the other censuses' 200 frames: at the frame rates this investigation
 * is chasing (single digits during the worst of a stall), 200 frames is tens of seconds between
 * reports, which is longer than the whole event this exists to catch. */
#define PROJECTILE_REPORT_EVERY_FRAMES 30u

/* Past this many live entries, the report also names the first few by position, because a count
 * on its own does not say whether they are spread across the level (normal, many fights at once)
 * or stacked on top of each other (the shape a stuck or repeatedly-retriggered spawn would
 * take). */
#define PROJECTILE_SAMPLE_THRESHOLD 10u
#define PROJECTILE_SAMPLE_COUNT     5u

typedef struct projectile_census {
    bool     armed;
    bool     per_frame;
    uint32_t frame_count;
} projectile_census_t;

static projectile_census_t projectile_census;

static void projectile_census_tick(void)
{
    uint32_t entry;
    uint32_t count;
    uint32_t sampled;
    uint32_t i;
    float    sample[PROJECTILE_SAMPLE_COUNT][3];
    uint32_t sample_index[PROJECTILE_SAMPLE_COUNT];

    if (!projectile_census.armed) {
        return;
    }
    ++projectile_census.frame_count;
    if (projectile_census.frame_count < PROJECTILE_REPORT_EVERY_FRAMES) {
        return;
    }
    projectile_census.frame_count = 0;

    if (!memory_try_read(projectile_list_head, &entry, sizeof(entry))) {
        return;
    }

    /* Kept rather than written out as they are walked, because whether they are worth writing at
     * all depends on the TOTAL, and the total is not known until the walk ends. Comparing the walk
     * index against the threshold instead made the threshold a start offset: the first ten were
     * skipped and entries ten to fourteen named, so a list of ten reported nothing at all and a
     * list of twelve named two, neither of them the first few. */
    count = 0;
    sampled = 0;
    while (entry != 0 && count < PROJECTILE_WALK_MAX) {
        if (sampled < PROJECTILE_SAMPLE_COUNT) {
            if (memory_try_read((uintptr_t)entry + PROJECTILE_POSITION_OFFSET, sample[sampled],
                            sizeof(sample[sampled]))) {
                sample_index[sampled] = count;
                ++sampled;
            }
        }

        {
            uint32_t next;

            if (!memory_try_read((uintptr_t)entry + PROJECTILE_NEXT_OFFSET, &next, sizeof(next))) {
                break;
            }
            entry = next;
        }
        ++count;
    }

    diag_log_write("prj  census: %u live entries on the projectile list%s", (unsigned)count,
                   count >= PROJECTILE_WALK_MAX ? " (hit the walk cap, list may be longer or "
                                                   "cyclic)" : "");

    if (count < PROJECTILE_SAMPLE_THRESHOLD) {
        return;                 /* a short list says nothing a count has not already said */
    }
    for (i = 0; i < sampled; ++i) {
        diag_log_write("prj    #%u at (%.1f, %.1f, %.1f)", (unsigned)sample_index[i],
                       (double)sample[i][0], (double)sample[i][1], (double)sample[i][2]);
    }
}

int diag_projectiles_install(int projectiles_level)
{
    if (projectiles_level <= 0) {
        return 0;
    }

    {
        uintptr_t site = signature_find_unique(SIG_PROJECTILE_HEAD, MSK_PROJECTILE_HEAD,
                                               sizeof SIG_PROJECTILE_HEAD);
        uint32_t  address = 0;

        uint32_t second = 0;

        if (site == 0 ||
            !memory_read_u32(site + OFFSET_PROJECTILE_HEAD_OPERAND, &address) ||
            !memory_read_u32(site + OFFSET_PROJECTILE_HEAD_OPERAND_TWO, &second) ||
            address != second ||
            !memory_is_inside_image(address, sizeof(uint32_t))) {
            log_warning("the projectile list head did not resolve, so this census is off. It "
                        "would otherwise read whatever sits at a guessed address and report "
                        "counts that look real.");
            return 0;
        }
        projectile_list_head = (uintptr_t)address;
        projectile_head_site = site;
    }

    projectile_census.armed     = true;
    projectile_census.per_frame = frame_hook_add(projectile_census_tick);

    log_info("projectile list census armed, head at %08X read from the operand at %08X, "
             "reporting every %u frames%s",
             (unsigned)projectile_list_head, (unsigned)projectile_head_site,
             (unsigned)PROJECTILE_REPORT_EVERY_FRAMES,
             projectile_census.per_frame
                 ? ""
                 : ". The per-frame hook is NOT available, so nothing will ever be reported");
    return projectile_census.per_frame ? 1 : 0;
}
