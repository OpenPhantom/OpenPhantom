/* diag_characters.c: name the characters standing near the player, and say which way each one is
 * moving vertically.
 *
 * WHAT THIS IS FOR. A field report names what a character looked like, not what the engine calls
 * it, and the two are not easy to connect while playing. This walks the engine's own character
 * pool every so often and reports the ones within a radius of the player by name and position, so
 * a report can say which record it means. The vertical column is the second reason it exists: a
 * character sinking through the floor it is standing on shows up here as a negative step, and the
 * size of that step says whether it is being displaced in discrete shoves or falling.
 *
 * The name is NOT a unique identifier. The spawn path copies it out of the placement, and a
 * placement name turns out to be a reused archetype label rather than a per-placement id; two
 * earlier attempts to identify a specific placement by name matching were both wrong for that
 * reason. Position is what distinguishes one from another, which is why every line carries it.
 *
 * ================================ The pool, and why it is walked by hand ======================
 *
 * The character pool is a fixed size slab allocator compiled from util/list.c. Its allocator at
 * 0x0046e92e scans the slot array for a link word of -1, claims that slot, threads it onto a list
 * whose head is header[1], and returns link + 4; the free path at 0x0046eaff unlinks the slot and
 * writes -1 back. So the slot array alone says which slots are occupied, with no list walk.
 *
 * The engine also offers an iterator, FUN_0046eabf, and this deliberately does not use it. That
 * function keeps its cursor in the list header itself, at header[2], and advances it on every
 * call. An observer calling it from a frame callback would move a cursor the engine is in the
 * middle of using. Reading the slot array touches nothing, and a census that perturbs the thing it
 * measures is not a census. The read only walk is also bounded by the pool's own capacity, so a
 * corrupted link cannot turn a report into a hang.
 *
 * ================================ The record, confirmed twice ================================
 *
 * Every offset below was already established by another feature in this project and independently
 * agreed with the spawn path at 0x00437250, which is the function that fills a new record in:
 *
 *   +0x04  name, char[12]   the spawn path's byte copy loop writes from local_8[1] onward, sourced
 *                           from the placement at +0xB8. dialogue_anim_fix reads the same field.
 *   +0x20  state            local_8[8], set to 1, or to 0x10 for a record with flag 0x2000.
 *   +0x34  body             local_8[0xd], the object created by FUN_0041223e a few lines earlier.
 *   +0x7C  AI mode          local_8[0x1f], taken from the placement's own first dword.
 *
 * The body is the same structure the player's own +0x0C points at: both carry an rdThing at +0x9C,
 * which is what ties the two independent readings of this layout together.
 *
 *   +0x18  position           float[3], world x/y/z
 *   +0x54  previous position  float[3], the same at the end of the previous simulation step
 *   +0xA0  owner              back to the character record, written by the spawn path
 *
 * That last field is not needed to report anything. It is read anyway and compared against the
 * record the walk arrived from, because it is a free check that the slot really is a character and
 * that the body pointer really is a body. A mismatch is reported rather than hidden, since the
 * whole value of this file is that its numbers can be trusted.
 *
 * Z is up. The five placements recorded from one lift sit within a tenth of a unit of each other
 * in Z while their X and Y differ by whole units.
 *
 * ================================ Cost ========================================================
 *
 * Reads go through memory_try_read, not memory_read. The per slot reads are the many ones here,
 * and the guarded readers cost a structured exception frame rather than a VirtualQuery syscall.
 * This project has already paid once for getting that the wrong way round in a walk that runs
 * often; the rule is written down in CONTRIBUTING.md.
 */
#include "diag_characters.h"

#include "character_scan.h"
#include "diag_install.h"
#include "diag_log.h"
#include "diag_write_watch.h"

#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- 0x00431ff3, the character pool teardown. A DATA SITE ONLY, never hooked ------------------ *
 * Chosen because its very first test is against the pool pointer itself, so the address sits at a
 * fixed offset from a prologue:
 *
 *   00431ff3  55                 push ebp
 *   00431ff4  8B EC              mov  ebp,esp
 *   00431ff6  83 EC 08           sub  esp,8
 *   00431ff9  83 3D <addr32> 00  cmp  dword ptr [g_characterPool],0     the address at +0x08
 *   00432000  75 0A              jnz  0043200c
 *   00432002  B8 01 00 00 00     mov  eax,1
 *   00432007  E9 <rel32>         jmp  to the tail
 *   0043200c  A1 <addr32>        mov  eax,[g_characterPool]
 *
 * The four address bytes are wildcarded and read out of the operand rather than written down, so
 * this survives a build that places the global somewhere else. Writing the address into the
 * pattern would have defeated the point of searching for it.
 *
 * The pattern HAS TO run this far. The first twenty bytes alone match twice in the retail image:
 * a second function at 0x00415B38 opens identically against a different global, 0x005BB4B8, and a
 * pattern
 * that matched both would have resolved to nothing and switched this observer off. The two
 * diverge at the instruction after the jump, where this one loads the global it just tested and
 * the other stores a zero somewhere else, so the pattern runs on to that A1 opcode. Counted
 * against the retail executable, 829,952 bytes, MD5 7c5af8428c19b17cca09ae3a49bd10ef: one match.
 * The jump displacement and both addresses are wildcarded, since all three are what differ
 * between builds. */
static const uint8_t SIG_CHARACTER_POOL[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0x83, 0x3D, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x75, 0x0A, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xE9, 0x00, 0x00, 0x00,
    0x00, 0xA1, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MASK_CHARACTER_POOL[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof SIG_CHARACTER_POOL == sizeof MASK_CHARACTER_POOL,
               "the character pool pattern and its mask are different lengths");
#define OFFSET_CHARACTER_POOL_POINTER 0x08u

/* --- Plr_RunPhases 0x00448297, a data site only, never hooked here ---------------------------- *
 * Byte identical to diag_flow.c's own copy of this site, and to the reasoning behind it. Each
 * observer group owns its own table so the evidence sits beside the code that depends on it.
 *   +0x27 : &pPlayer */
static const uint8_t SIG_PLAYER_RUN_PHASES[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x45, 0xF8, 0x83,
    0x3C, 0x85, 0x28, 0x52, 0x4B, 0x00, 0x01, 0x0F, 0x84, 0x95, 0x00, 0x00,
    0x00, 0x8B, 0x0D, 0x20, 0x52, 0x4B, 0x00
};
#define OFFSET_PLAYER_POINTER 0x27u

enum {
    SITE_CHARACTER_POOL,
    SITE_PLAYER_RUN_PHASES,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("character_pool", SIG_CHARACTER_POOL, MASK_CHARACTER_POOL),
    SIGNATURE_ENTRY("player_run_phases", SIG_PLAYER_RUN_PHASES)
};

#define CHARACTER_NAME_OFFSET    0x04u
#define CHARACTER_NAME_SIZE      12u
#define CHARACTER_STATE_OFFSET   0x20u
#define CHARACTER_BODY_OFFSET    0x34u
#define CHARACTER_AI_MODE_OFFSET 0x7Cu
/* The character's OWN position, and the authoritative one. FUN_004333e1 runs every simulation step
 * and copies it into the body at +0x18, so the body's position is a courier's copy that is always
 * one propagation behind whatever actually moved the character. A watch belongs here, not there. */
#define CHARACTER_POSITION_OFFSET 0xD0u
/* The velocity FUN_004362c8 integrates that position from.
 *
 * Measured, and this refuted the guess that stood here first: the velocity is zero while a
 * character stands still and spikes downward only on the steps it actually moves, so it is an
 * intermittent impulse from a contact rather than a retained velocity that nothing clears.
 *
 * The move flag at body+0xC0 was reported here too and taken out again. FUN_00435c67 sets it back
 * to 1 at the top of every step, so it reads 1 at frame end whether or not any individual move was
 * refused: a column that looks like an answer and is not one. */
#define CHARACTER_VELOCITY_OFFSET 0xDCu
/* The movement mode FUN_004362c8 consults BEFORE any collision test. Bit 0 set makes it jump
 * straight past the test at 0x004363EA and commit whatever the integrator produced, because the
 * allow flag was already reset to permitted at the top of the step. Reported because a character
 * that is never collision tested cannot be fixed by correcting the collision test. */
#define CHARACTER_MOVE_MODE_OFFSET 0x98u

#define OBJECT_POSITION_OFFSET          0x18u
#define OBJECT_PREVIOUS_POSITION_OFFSET 0x54u
#define OBJECT_OWNER_OFFSET             0xA0u

#define PLAYER_OBJECT_OFFSET 0x0Cu

/* Long enough that a walk is a rounding error against a frame, short enough that a character
 * crossing a floor is still being reported while it happens. */
#define CHARACTER_REPORT_EVERY_FRAMES 60u

/* A ceiling on how many lines one report may write, so a crowded street cannot flood the log. The
 * count in the summary is not capped, only the naming. */
#define CHARACTER_NAMED_MAX 12u

typedef struct character_census {
    bool      armed;
    bool      per_frame;
    bool      report_everything;   /* level 2: ignore the radius */
    float     radius;
    uint32_t  frame_count;
    uint32_t *pool_slot;           /* the global that holds the pool pointer */
    uint32_t *player_slot;         /* the global that holds the player pointer */
    uint32_t  tracked_pool;        /* the pool the table below belongs to */
    char      watch_name[16];      /* empty: watch nothing */
    bool      watch_velocity;      /* watch the velocity Z rather than the position Z */
    bool      watch_prepared;
    character_track_t tracked[CHARACTER_TRACK_MAX];
} character_census_t;

static character_census_t character_census;

/* The player's own world position, through the same two hops diag_flow.c and the spawn census
 * take. False when any link in the chain is not readable, which reads as "no player yet", the
 * normal state in a menu. */
static bool read_player_position(float out_position[3])
{
    uint32_t player = 0;
    uint32_t object = 0;

    if (character_census.player_slot == NULL) {
        return false;
    }
    if (!memory_try_read((uintptr_t)character_census.player_slot, &player, sizeof(player)) ||
        player == 0) {
        return false;
    }
    if (!memory_try_read((uintptr_t)player + PLAYER_OBJECT_OFFSET, &object, sizeof(object)) ||
        object == 0) {
        return false;
    }
    return memory_try_read((uintptr_t)object + OBJECT_POSITION_OFFSET, out_position,
                           sizeof(float) * 3u);
}

/* Whether this is the character the ini asked to watch. Matched on the whole name rather than a
 * prefix, since the names are short and a prefix would match a whole archetype family. */
static bool watch_wanted(const char *name)
{
    if (character_census.watch_name[0] == '\0' || name == NULL) {
        return false;
    }
    return strcmp(name, character_census.watch_name) == 0;
}

/* One occupied slot. Returns false when the record does not read as a character, which the caller
 * counts but does not name: a slot that fails here is far more likely to mean a wrong offset than
 * a broken character, and a log full of unreadable slots would say the same thing once. */
static bool report_character(uintptr_t record, const float player_position[3], bool *out_named)
{
    char     raw_name[CHARACTER_NAME_SIZE];
    char     name[CHARACTER_NAME_SIZE + 1];
    uint32_t body = 0;
    uint32_t owner = 0;
    int32_t  state = 0;
    int32_t  ai_mode = 0;
    int32_t  move_mode = 0;
    float    position[3];
    float    previous[3];
    float    velocity[3] = { 0.0f, 0.0f, 0.0f };
    float    vertical;
    float    since = 0.0f;
    bool     known;

    *out_named = false;

    if (!memory_try_read(record + CHARACTER_BODY_OFFSET, &body, sizeof(body)) || body == 0) {
        return false;
    }
    if (!memory_try_read((uintptr_t)body + OBJECT_POSITION_OFFSET, position, sizeof(position)) ||
        !memory_try_read((uintptr_t)body + OBJECT_PREVIOUS_POSITION_OFFSET, previous,
                         sizeof(previous))) {
        return false;
    }
    if (!character_census.report_everything &&
        !character_scan_within_radius(position, player_position, character_census.radius)) {
        return true;
    }

    if (!memory_try_read(record + CHARACTER_NAME_OFFSET, raw_name, sizeof(raw_name))) {
        return false;
    }
    character_scan_copy_name(raw_name, sizeof(raw_name), name, sizeof(name));
    (void)memory_try_read(record + CHARACTER_STATE_OFFSET, &state, sizeof(state));
    (void)memory_try_read(record + CHARACTER_AI_MODE_OFFSET, &ai_mode, sizeof(ai_mode));
    (void)memory_try_read(record + CHARACTER_MOVE_MODE_OFFSET, &move_mode, sizeof(move_mode));
    (void)memory_try_read((uintptr_t)body + OBJECT_OWNER_OFFSET, &owner, sizeof(owner));
    (void)memory_try_read(record + CHARACTER_VELOCITY_OFFSET, velocity, sizeof(velocity));


    /* `step` is a genuine one step delta, not a broken one. FUN_004333e1 copies the body's current
       position into +0x54 and only then writes the new one in, so the two differ exactly on a step
       the character moved. It reads zero almost always for a different reason: this reports once
       every sixty frames while the simulation runs thirty two steps a second, so a character that
       moves on one step in thirty is almost never caught mid step. An earlier version of this
       comment claimed the field was structurally always zero, which the disassembly disproved.
       `since` below is the column to read for whether a character is sinking, because it spans the
       whole gap between reports and cannot miss a movement inside it. */
    vertical = character_scan_vertical_delta(position, previous);
    known = character_scan_track(character_census.tracked, CHARACTER_TRACK_MAX, (uint32_t)record,
                                 position[2], &since);

    if (known) {
        diag_log_write("chr    %-12s at (%.1f, %.1f, %.1f)  d=%.1f  state=%d ai=%d  step=%+.3f  "
                       "since=%+.3f %s  v=(%.2f, %.2f, %.2f) mode=%d%s%s",
                       name, (double)position[0], (double)position[1], (double)position[2],
                       (double)character_scan_distance(position, player_position), (int)state,
                       (int)ai_mode, (double)vertical, (double)since,
                       character_scan_motion_text(character_scan_classify(since)),
                       (double)velocity[0], (double)velocity[1], (double)velocity[2],
                       (int)move_mode, (move_mode & 1) ? " NOT COLLISION TESTED" : "",
                       ((uintptr_t)owner == record) ? "" : "  (owner mismatch, offsets suspect)");
    } else {
        diag_log_write("chr    %-12s at (%.1f, %.1f, %.1f)  d=%.1f  state=%d ai=%d  step=%+.3f  "
                       "first sighting  v=(%.2f, %.2f, %.2f) mode=%d%s%s",
                       name, (double)position[0], (double)position[1], (double)position[2],
                       (double)character_scan_distance(position, player_position), (int)state,
                       (int)ai_mode, (double)vertical,
                       (double)velocity[0], (double)velocity[1], (double)velocity[2],
                       (int)move_mode, (move_mode & 1) ? " NOT COLLISION TESTED" : "",
                       ((uintptr_t)owner == record) ? "" : "  (owner mismatch, offsets suspect)");
    }
    /* Arming on the Z rather than the whole position: the field this bug moves is the only one
       worth a breakpoint, and the four watchable bytes have to be exactly the four being written
       or the report names the wrong instruction. */
    if (watch_wanted(name) && !diag_write_watch_is_armed()) {
        char      label[48];
        uintptr_t field;

        field = character_census.watch_velocity
                    ? record + CHARACTER_VELOCITY_OFFSET + (2u * sizeof(float))
                    : record + CHARACTER_POSITION_OFFSET + (2u * sizeof(float));
        (void)_snprintf(label, sizeof(label) - 1u, "%s %s", name,
                        character_census.watch_velocity ? "velocity Z" : "position Z");
        label[sizeof(label) - 1u] = '\0';
        (void)diag_write_watch_arm(field, label);
    }

    *out_named = true;
    return true;
}

static void character_census_tick(void)
{
    uint32_t pool = 0;
    uint32_t element_size = 0;
    uint32_t capacity = 0;
    uint32_t index;
    uint32_t live = 0;
    uint32_t named = 0;
    uint32_t unreadable = 0;
    float    player_position[3];

    if (!character_census.armed) {
        return;
    }
    ++character_census.frame_count;
    if (character_census.frame_count < CHARACTER_REPORT_EVERY_FRAMES) {
        return;
    }
    character_census.frame_count = 0;

    /* Prepared from here because this callback runs on the simulation thread, which is the only
       thread whose debug registers are worth anything. */
    if (character_census.watch_name[0] != '\0' && !character_census.watch_prepared) {
        character_census.watch_prepared = diag_write_watch_prepare();
    }
    diag_write_watch_report();

    if (!read_player_position(player_position)) {
        return;
    }
    if (character_census.pool_slot == NULL ||
        !memory_try_read((uintptr_t)character_census.pool_slot, &pool, sizeof(pool)) || pool == 0) {
        return;   /* no level loaded; the pool is torn down between levels */
    }
    /* Slot addresses from one level say nothing about the next, and the pool is torn down and
       rebuilt between them, so a remembered height would be compared against a different
       character entirely. */
    if (pool != character_census.tracked_pool) {
        character_census.tracked_pool = pool;
        character_scan_track_reset(character_census.tracked, CHARACTER_TRACK_MAX);
        /* The watched address belonged to a character in the previous level. Whatever occupies
           that memory now is not it, and reporting writes to it would be worse than useless. */
        diag_write_watch_disarm();
    }
    if (!memory_try_read((uintptr_t)pool + CHARACTER_POOL_ELEMENT_SIZE_OFFSET, &element_size,
                         sizeof(element_size)) ||
        !memory_try_read((uintptr_t)pool + CHARACTER_POOL_CAPACITY_OFFSET, &capacity,
                         sizeof(capacity))) {
        return;
    }
    if (!character_scan_pool_is_sane(element_size, capacity)) {
        diag_log_write("chr  pool at %08X does not describe itself sensibly: element %u, "
                       "capacity %u. Not walked", (unsigned)pool, (unsigned)element_size,
                       (unsigned)capacity);
        return;
    }

    for (index = 0; index < capacity; ++index) {
        uint32_t offset = character_scan_slot_offset(element_size, index);
        uint32_t link = 0;
        bool     was_named = false;

        if (offset == 0u) {
            break;
        }
        if (!memory_try_read((uintptr_t)pool + offset, &link, sizeof(link))) {
            break;
        }
        if (!character_scan_slot_is_live(link)) {
            continue;
        }
        ++live;
        if (named >= CHARACTER_NAMED_MAX) {
            continue;
        }
        if (!report_character((uintptr_t)pool + offset + CHARACTER_POOL_LINK_SIZE, player_position,
                              &was_named)) {
            ++unreadable;
            continue;
        }
        if (was_named) {
            ++named;
        }
    }

    diag_log_write("chr  census: %u named of %u live characters%s, player at (%.1f, %.1f, %.1f)%s",
                   (unsigned)named, (unsigned)live,
                   character_census.report_everything ? "" : " within the radius",
                   (double)player_position[0], (double)player_position[1],
                   (double)player_position[2],
                   unreadable == 0u ? "" : " (some slots did not read as characters)");
}

int diag_characters_install(int characters_level, int radius, const char *watch_name,
                            int watch_velocity)
{
    if (characters_level <= 0) {
        return 0;
    }

    signature_resolve_table(sites, SITE_COUNT);

    character_census.pool_slot = (uint32_t *)diag_derive_address(
        sites, SITE_CHARACTER_POOL, OFFSET_CHARACTER_POOL_POINTER, "g_characterPool");
    character_census.player_slot = (uint32_t *)diag_derive_address(
        sites, SITE_PLAYER_RUN_PHASES, OFFSET_PLAYER_POINTER, "pPlayer");

    if (character_census.pool_slot == NULL || character_census.player_slot == NULL) {
        log_warning("  character census OFF, the pool or the player pointer could not be derived");
        return 0;
    }

    character_census.armed             = true;
    character_census.report_everything = characters_level >= 2;
    character_census.radius            = (float)radius;
    character_census.watch_velocity    = watch_velocity != 0;
    if (watch_name != NULL) {
        size_t length = strlen(watch_name);

        if (length >= sizeof(character_census.watch_name)) {
            length = sizeof(character_census.watch_name) - 1u;
        }
        memcpy(character_census.watch_name, watch_name, length);
        character_census.watch_name[length] = '\0';
    }
    character_census.per_frame         = frame_hook_add(character_census_tick);

    log_info("character census armed on the pool at %08X, reporting %s every %u frames%s",
             (unsigned)(uintptr_t)character_census.pool_slot,
             character_census.report_everything ? "every live character"
                                                : "the characters near the player",
             (unsigned)CHARACTER_REPORT_EVERY_FRAMES,
             character_census.per_frame
                 ? ""
                 : ". The per-frame hook is NOT available, so nothing will ever be reported");
    return character_census.per_frame ? 1 : 0;
}
