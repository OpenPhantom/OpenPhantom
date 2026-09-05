/* vertex_table.c: the fifteen words that move the vertex cache, and the three gates in front of
 * them.
 *
 * ==============================================================================================
 * 0. what is and is not touched
 *
 *     0x005bf9f0  vertex cache   0x4000 slots * 0x40 B = 0x100000 B (1 MiB), a raw BSS blob
 *     0x006bf9f0  a live "current light/draw-state" record, immediately adjacent, NO slack
 *
 * The cache is not a table of pointers or structs Ghidra ever named as one; it is addressed by
 * three DIFFERENT literal forms, all baked directly into instruction operands rather than read
 * from a pointer variable: `SHL reg,6` then `ADD reg,0x5bf9f0` (five call sites, two encodings
 * of ADD depending on which register: `81 /0 id` for EDX/ESI, the EAX-only short form `05 id` for
 * EAX), one direct `MOV byte ptr [reg+0x5bf9f1],1` (the per-slot "touched" flag, offset +1), and
 * one seed pointer `MOV ECX,0x5bf9f2` walked with `ADD ECX,0x40` per iteration (offset +2, the
 * per-frame touched-flag RESET loop). Twelve address-bearing sites in total, found by exhaustive
 * xref census across the three functions that read or write the cache
 * (0x004199B0, 0x0041B070, 0x0041BAF0) and confirmed byte-for-byte against the running retail
 * image rather than assumed from the disassembly view's mnemonics alone; the EAX short form in
 * particular would have been guessed wrong (the long form was the default assumption) had the
 * actual bytes not been checked.
 *
 * NOT touched, and why:
 *   * The trailer record at 0x006bf9f0. Nothing here reserves or reads it; it is a wholly
 *     different structure that starts exactly where the cache's declared span ends, is written by
 *     eight different functions, and has ZERO slack: the first byte past the cache is the first
 *     byte OF something else. This is why the buffer is relocated whole rather than grown in
 *     place; growing in place would corrupt the trailer on the very first extra slot.
 *   * The counter itself, 0x005bb5c0 (g_numVertCache). It is a counter, not an address, exactly
 *     the same reasoning draw_table.c already gives its own counter at 0x59DEBC.
 *
 * ==============================================================================================
 * 1. THE THREE GATES, and why unlike the cell table this one aborts clean
 *
 * cell_watchdog.h already documents this precisely, "unlike the cell table the vertex cache
 * aborts CLEANLY (all three gates branch to a return before every write)". That is confirmed here
 * again, independently, because it is the fact this whole relocation leans on:
 *
 *   gate 1  0x0041A0DF  bapvrt_transformWorld (FUN_004199B0): computes the slot address from the
 *           counter's value BEFORE the increment, writes the whole vertex into that slot, THEN
 *           increments, compares the NEW value against 0x4000, and only bails the function if it
 *           would be the ONE PAST the last valid slot. No slot beyond 0x3FFF is ever written.
 *   gate 2  0x0041B6CC  FUN_0041B070: computes count-to-add + current counter BEFORE the loop and
 *           bails the WHOLE function (return 0, untouched) if the sum would exceed 0x4000. This is
 *           the gate cell_watchdog.h names as the dangerous one; bailing here jumps behind the
 *           per-frame touched-flag RESET loop at the end of the same function, so vertices this
 *           frame could not cache keep last frame's stale touched flag into the NEXT frame, and
 *           FUN_004199B0 then wrongly reuses whatever was in their old cache slot. That is the
 *           torn/stretched geometry that does not self-correct until the level reloads.
 *   gate 3  0x0041C0D5  FUN_0041BAF0: the same shape as gate 2, in the second (mover) entry point.
 *
 * All three are genuine pre-checks, not entry-only checks with unchecked appends after (the cell
 * table's own failure mode). That is why this file needs NO reserve the way draw_table.c's 8192
 * entries do: the retail code itself never writes past whatever limit these three gates enforce.
 * Raise all three together and the buffer's true capacity is exactly what gets used: a straight
 * multiplication, not an overshoot allowance.
 *
 * ==============================================================================================
 * 2. THE SIZES
 *
 *     buffer  0x8000 = 32768 slots = 0x200000 B (2 MiB), committed RW
 *     guard   1 page = 0x1000 B immediately behind it, PAGE_NOACCESS
 *     limit   0x4000 = 16384 -> 0x8000 = 32768
 *
 * Double, matching the ratio draw_table.c already field-proved for the cell table, rather than a
 * larger multiple with no field measurement behind it. The lesson of the ViewRangeScale 2.0 to
 * 4.0 revert (view_distance_fix.c) is that an argument sound on paper is not the same as one
 * confirmed in play. The guard page is not sized to absorb an overshoot (there is none to absorb,
 * see above); it exists so that a site this census missed, if one exists, faults at the causing
 * instruction instead of corrupting whatever memory happens to sit next.
 *
 * ==============================================================================================
 * 3. ALL OR NOTHING, same discipline as draw_table.c
 *
 *   pre-flight (NO write) -> buffer -> the twelve address operands -> the three gates LAST.
 *
 * If one gate fails to resolve, or one operand does not currently hold what byte evidence says it
 * must, NOTHING is written. If a write fails midway, everything already written is restored from
 * the backup. The three gate immediates are written last and together: raising only some of them
 * would leave one producer path silently capped at the old limit while the buffer holding its
 * output had already moved, exactly the half-relocated state draw_table.c calls "the worst
 * imaginable".
 */
#include "vertex_table.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ENTRY_STRIDE   0x40u
#define OLD_LIMIT      0x4000u
#define NEW_LIMIT      0x8000u
#define BUFFER_BYTES   (NEW_LIMIT * ENTRY_STRIDE)
#define GUARD_BYTES    0x1000u
#define OLD_BASE       0x005bf9f0u

_Static_assert((BUFFER_BYTES % ENTRY_STRIDE) == 0,
    "The vertex-cache buffer must be a whole number of entries");

/* --- the five address-bearing instruction forms, each fully fixed (no wildcard): the immediate
 * IS the evidence that a site belongs to the cache at all, so a pattern that matched with any
 * other value would be matching something else. Confirmed byte-for-byte against the running
 * retail image; see the file header for how the EAX short form was resolved rather than guessed. */
static const uint8_t PAT_ADD_EDX[] = { 0x81, 0xC2, 0xF0, 0xF9, 0x5B, 0x00 };   /* +0x0041A0 */
static const uint8_t PAT_ADD_ESI[] = { 0x81, 0xC6, 0xF0, 0xF9, 0x5B, 0x00 };
static const uint8_t PAT_ADD_EAX[] = { 0x05, 0xF0, 0xF9, 0x5B, 0x00 };         /* short form */
static const uint8_t PAT_TOUCHED_STORE[] = { 0xC6, 0x82, 0xF1, 0xF9, 0x5B, 0x00, 0x01 };
static const uint8_t PAT_SEED_POINTER[]  = { 0xB9, 0xF2, 0xF9, 0x5B, 0x00 };

/* --- the three gates. Each anchor is specific enough to be individually unique (unlike the five
 * patterns above, these are not expected to repeat), and each carries the counter's own absolute
 * address as part of the anchor rather than as a separate cross-checked operand: 0x005bb5c0 is a
 * plain global that nothing in this codebase ever relocates, so pinning it in the pattern makes
 * the match MORE specific, not more fragile. */
/* 0x0041A0DF: `cmp edx,0x4000` then `mov [counter],edx`, the same 8-byte anchor
 * cell_watchdog.c already ships. */
static const uint8_t PAT_GATE1[] = {
    0x81, 0xFA, 0x00, 0x40, 0x00, 0x00, 0x89, 0x15
};
static const uint8_t PAT_GATE2[] = {              /* 0x0041B6C3..0x0041B6CC:  */
    0x8B, 0x0D, 0xC0, 0xB5, 0x5B, 0x00,           /*   mov ecx,[0x005bb5c0]   */
    0x8D, 0x14, 0x08,                             /*   lea edx,[eax+ecx]      */
    0x81, 0xFA, 0x00, 0x40, 0x00, 0x00            /*   cmp edx,0x4000         */
};
static const uint8_t PAT_GATE3[] = {              /* 0x0041C0CC..0x0041C0D5:  */
    0x8B, 0x15, 0xC0, 0xB5, 0x5B, 0x00,           /*   mov edx,[0x005bb5c0]   */
    0x8D, 0x0C, 0x10,                             /*   lea ecx,[eax+edx]      */
    0x81, 0xF9, 0x00, 0x40, 0x00, 0x00            /*   cmp ecx,0x4000         */
};

typedef struct address_pattern {
    const char    *name;
    const uint8_t *bytes;
    size_t         size;
    size_t         expected_count;
    size_t         immediate_offset;   /* where the 4-byte operand starts within the pattern */
    uint32_t       value_offset;       /* 0, 1 (touched flag) or 2 (seed pointer), added to base */
} address_pattern_t;

static const address_pattern_t ADDRESS_PATTERNS[] = {
    { "add_edx",        PAT_ADD_EDX,       sizeof PAT_ADD_EDX,       1, 2, 0 },
    { "add_esi",        PAT_ADD_ESI,       sizeof PAT_ADD_ESI,       5, 2, 0 },
    { "add_eax",        PAT_ADD_EAX,       sizeof PAT_ADD_EAX,       4, 1, 0 },
    { "touched_store",  PAT_TOUCHED_STORE, sizeof PAT_TOUCHED_STORE, 1, 2, 1 },
    { "seed_pointer",   PAT_SEED_POINTER,  sizeof PAT_SEED_POINTER,  1, 1, 2 },
};
#define ADDRESS_PATTERN_COUNT (sizeof(ADDRESS_PATTERNS) / sizeof(ADDRESS_PATTERNS[0]))
#define ADDRESS_WORD_COUNT 12u   /* 1 + 5 + 4 + 1 + 1, the exact census from the xref sweep */

typedef struct gate_pattern {
    const char    *name;
    const uint8_t *bytes;
    size_t         size;
    size_t         immediate_offset;
} gate_pattern_t;

static const gate_pattern_t GATE_PATTERNS[] = {
    { "gate1", PAT_GATE1, sizeof PAT_GATE1, 2  },
    { "gate2", PAT_GATE2, sizeof PAT_GATE2, 11 },
    { "gate3", PAT_GATE3, sizeof PAT_GATE3, 11 },
};
#define GATE_WORD_COUNT (sizeof(GATE_PATTERNS) / sizeof(GATE_PATTERNS[0]))

#define WORD_COUNT (ADDRESS_WORD_COUNT + GATE_WORD_COUNT)

typedef struct patched_word {
    uintptr_t   address;      /* the VA of the 4-byte operand itself */
    uint32_t    expected;     /* what must be there, or nothing is written */
    uint32_t    old_value;    /* what really was there before we wrote */
    uint32_t    new_value;
    bool        written;
    const char *description;
} patched_word_t;

typedef struct vertex_table_state {
    patched_word_t words[WORD_COUNT];
    size_t         word_count;
    bool           active;
    bool           has_run;
    uint8_t       *buffer;
    uintptr_t      guard_address;
} vertex_table_state_t;

static vertex_table_state_t table_state;

/* ============================================================================================ */
static bool allocate_buffer(void)
{
    MEMORY_BASIC_INFORMATION information;
    uint8_t                 *reserved;

    reserved = (uint8_t *)VirtualAlloc(NULL, BUFFER_BYTES + GUARD_BYTES, MEM_RESERVE,
                                       PAGE_NOACCESS);
    if (reserved == NULL) {
        log_error("vertex table: VirtualAlloc(RESERVE %u B) failed (%lu), NOT ONE BYTE patched",
                  (unsigned)(BUFFER_BYTES + GUARD_BYTES), (unsigned long)GetLastError());
        return false;
    }
    if (VirtualAlloc(reserved, BUFFER_BYTES, MEM_COMMIT, PAGE_READWRITE) == NULL) {
        log_error("vertex table: VirtualAlloc(COMMIT %u B) failed (%lu), NOT ONE BYTE patched",
                  (unsigned)BUFFER_BYTES, (unsigned long)GetLastError());
        VirtualFree(reserved, 0, MEM_RELEASE);
        return false;
    }
    if (VirtualAlloc(reserved + BUFFER_BYTES, GUARD_BYTES, MEM_COMMIT, PAGE_NOACCESS) == NULL) {
        log_error("vertex table: the guard page at %08X could not be committed (%lu), NOT "
                  "ONE BYTE patched", (unsigned)(uintptr_t)(reserved + BUFFER_BYTES),
                  (unsigned long)GetLastError());
        VirtualFree(reserved, 0, MEM_RELEASE);
        return false;
    }
    if (VirtualQuery(reserved + BUFFER_BYTES, &information, sizeof(information))
            != sizeof(information) ||
        information.Protect != PAGE_NOACCESS || information.State != MEM_COMMIT) {
        log_error("vertex table: the guard page at %08X does not carry PAGE_NOACCESS "
                  "(State %08lX, Protect %08lX), NOT ONE BYTE patched",
                  (unsigned)(uintptr_t)(reserved + BUFFER_BYTES),
                  (unsigned long)information.State, (unsigned long)information.Protect);
        VirtualFree(reserved, 0, MEM_RELEASE);
        return false;
    }
    if ((uintptr_t)reserved >= 0x80000000u) {
        log_error("vertex table: the buffer at %08X lies above 2 GiB, refused",
                  (unsigned)(uintptr_t)reserved);
        VirtualFree(reserved, 0, MEM_RELEASE);
        return false;
    }

    /* Left as MEM_COMMIT hands it over: zero-filled, identical to the BSS it replaces. */
    table_state.buffer        = reserved;
    table_state.guard_address = (uintptr_t)(reserved + BUFFER_BYTES);
    return true;
}

static bool roll_back(const char *why)
{
    size_t index = table_state.word_count;
    bool   ok = true;

    while (index-- > 0) {
        if (!table_state.words[index].written) {
            continue;
        }
        if (patch_write_u32(table_state.words[index].address,
                            table_state.words[index].old_value) == PATCH_RESULT_OK) {
            table_state.words[index].written = false;
        } else {
            ok = false;
            log_error("vertex table: ROLLBACK of word %u (%s, %08X -> %08X) FAILED",
                      (unsigned)(index + 1), table_state.words[index].description,
                      (unsigned)table_state.words[index].address,
                      (unsigned)table_state.words[index].old_value);
        }
    }
    if (ok) {
        log_info("vertex table: %s, every already-written site was restored, the game runs "
                 "unchanged", why);
    }
    return ok;
}

/* ============================================================================================
 * The gates. Each returns false without writing anything.
 * ============================================================================================ */
static bool find_address_sites(uintptr_t hits[ADDRESS_PATTERN_COUNT][SIGNATURE_MAX_REPORTED],
                                size_t counts[ADDRESS_PATTERN_COUNT])
{
    size_t index;
    bool   all_ok = true;

    for (index = 0; index < ADDRESS_PATTERN_COUNT; ++index) {
        const address_pattern_t *pattern = &ADDRESS_PATTERNS[index];

        counts[index] = signature_count_matches(pattern->bytes, NULL, pattern->size,
                                                 hits[index], SIGNATURE_MAX_REPORTED);
        log_info("vertex table: pattern %s %u/%u", pattern->name, (unsigned)counts[index],
                 (unsigned)pattern->expected_count);
        if (counts[index] != pattern->expected_count) {
            all_ok = false;
        }
    }
    if (!all_ok) {
        log_warning("vertex table: unexpected match count, unknown image, NOT ONE BYTE patched");
        return false;
    }
    return true;
}

static bool find_gate_sites(uintptr_t gates[GATE_WORD_COUNT])
{
    size_t index;

    for (index = 0; index < GATE_WORD_COUNT; ++index) {
        gates[index] = signature_find_unique(GATE_PATTERNS[index].bytes, NULL,
                                             GATE_PATTERNS[index].size);
        if (gates[index] == 0) {
            log_warning("vertex table: %s did not resolve uniquely, NOT ONE BYTE patched",
                        GATE_PATTERNS[index].name);
            return false;
        }
    }
    return true;
}

/* ============================================================================================ */
static void add_word(uintptr_t address, uint32_t expected, uint32_t new_value,
                     const char *description)
{
    patched_word_t *word = &table_state.words[table_state.word_count];

    word->address     = address;
    word->expected    = expected;
    word->new_value    = new_value;
    word->description = description;
    word->written     = false;
    ++table_state.word_count;
}

static bool build_word_list(uintptr_t hits[ADDRESS_PATTERN_COUNT][SIGNATURE_MAX_REPORTED],
                            const size_t counts[ADDRESS_PATTERN_COUNT],
                            const uintptr_t gates[GATE_WORD_COUNT], uint32_t buffer_base)
{
    size_t pattern_index;
    size_t hit_index;
    size_t word_index;

    table_state.word_count = 0;

    for (pattern_index = 0; pattern_index < ADDRESS_PATTERN_COUNT; ++pattern_index) {
        const address_pattern_t *pattern = &ADDRESS_PATTERNS[pattern_index];
        uint32_t expected_old = OLD_BASE + pattern->value_offset;
        uint32_t new_value    = buffer_base + pattern->value_offset;

        for (hit_index = 0; hit_index < counts[pattern_index]; ++hit_index) {
            add_word(hits[pattern_index][hit_index] + pattern->immediate_offset, expected_old,
                     new_value, pattern->name);
        }
    }
    for (pattern_index = 0; pattern_index < GATE_WORD_COUNT; ++pattern_index) {
        add_word(gates[pattern_index] + GATE_PATTERNS[pattern_index].immediate_offset, OLD_LIMIT,
                 NEW_LIMIT, GATE_PATTERNS[pattern_index].name);
    }

    if (table_state.word_count != WORD_COUNT) {
        log_error("vertex table: %u instead of %u sites collected, NOT ONE BYTE patched",
                  (unsigned)table_state.word_count, (unsigned)WORD_COUNT);
        table_state.word_count = 0;
        return false;
    }

    for (word_index = 0; word_index < WORD_COUNT; ++word_index) {
        if (!memory_read_u32(table_state.words[word_index].address,
                             &table_state.words[word_index].old_value)) {
            table_state.word_count = 0;
            return false;
        }
        if (table_state.words[word_index].old_value != table_state.words[word_index].expected) {
            log_warning("vertex table: pre-flight: site %u/%u (%s) at %08X carries %08X, "
                        "expected %08X, NOT ONE BYTE patched", (unsigned)(word_index + 1),
                        (unsigned)WORD_COUNT, table_state.words[word_index].description,
                        (unsigned)table_state.words[word_index].address,
                        (unsigned)table_state.words[word_index].old_value,
                        (unsigned)table_state.words[word_index].expected);
            table_state.word_count = 0;
            return false;
        }
    }
    return true;
}

static bool write_all_words(void)
{
    size_t index;

    for (index = 0; index < WORD_COUNT; ++index) {
        if (patch_repoint_operand(table_state.words[index].address,
                                  table_state.words[index].old_value,
                                  table_state.words[index].new_value) == PATCH_RESULT_OK) {
            table_state.words[index].written = true;
            continue;
        }

        log_error("vertex table: write %u/%u (%s, %08X: %08X -> %08X) FAILED",
                  (unsigned)(index + 1), (unsigned)WORD_COUNT, table_state.words[index].description,
                  (unsigned)table_state.words[index].address,
                  (unsigned)table_state.words[index].old_value,
                  (unsigned)table_state.words[index].new_value);

        if (!roll_back("partial failure")) {
            log_error("vertex table: THE CACHE IS HALF RELOCATED AND CANNOT BE ROLLED BACK. Some "
                      "writers now target the NEW buffer and some the OLD one, or some gates allow "
                      "more slots than the buffer they still point at has. Continuing would mean "
                      "driving into silent memory corruption. The game is terminated in a "
                      "controlled way.");
            log_shutdown();
            TerminateProcess(GetCurrentProcess(), 0xDEAD0002u);
        }
        table_state.word_count = 0;
        return false;
    }
    return true;
}

/* ============================================================================================ */
void vertex_table_relocate(void)
{
    uintptr_t hits[ADDRESS_PATTERN_COUNT][SIGNATURE_MAX_REPORTED];
    size_t    counts[ADDRESS_PATTERN_COUNT];
    uintptr_t gates[GATE_WORD_COUNT];
    uint32_t  base;

    if (table_state.has_run) {
        log_info("vertex table: vertex_table_relocate called a second time, ignored (the table "
                 "is %s)", table_state.active ? "already relocated" : "not relocated");
        return;
    }
    table_state.has_run = true;

    if (!find_address_sites(hits, counts)) {
        return;
    }
    /* Idempotency: if the FIRST (and only) add_edx site no longer holds the retail base, either
     * this DLL already relocated it, or something incompatible has already touched it. Either
     * way we refuse rather than guess. */
    {
        uint32_t current = 0;
        if (!memory_read_u32(hits[0][0] + ADDRESS_PATTERNS[0].immediate_offset, &current)) {
            return;
        }
        if (!memory_is_inside_image(current, sizeof(uint32_t))) {
            log_info("vertex table: the base at %08X lies outside the image, the table is "
                     "ALREADY RELOCATED. Nothing to do.", (unsigned)current);
            return;
        }
    }
    if (!find_gate_sites(gates)) {
        return;
    }
    if (!allocate_buffer()) {
        return;
    }
    base = (uint32_t)(uintptr_t)table_state.buffer;

    if (!build_word_list(hits, counts, gates, base)) {
        return;
    }

    log_info("vertex table: pre-flight %u/%u in order. Base %08X (%u slots of %u B), gates at "
             "%08X/%08X/%08X = %u.", (unsigned)WORD_COUNT, (unsigned)WORD_COUNT,
             (unsigned)OLD_BASE, (unsigned)OLD_LIMIT, (unsigned)ENTRY_STRIDE, (unsigned)gates[0],
             (unsigned)gates[1], (unsigned)gates[2], (unsigned)OLD_LIMIT);

    /* Order: the twelve address operands first, the three gates LAST. If one of the twelve fails,
     * the gates stay low and the engine runs on with the old boundary, never a bigger limit
     * pointed at a buffer that is not there. */
    if (!write_all_words()) {
        return;
    }

    table_state.active = true;
    log_info("vertex table relocated to %08X (%u slots of %u B = %u B), guard page %08X "
             "(PAGE_NOACCESS), gates %u -> %u, %u/%u operands written.", (unsigned)base,
             (unsigned)NEW_LIMIT, (unsigned)ENTRY_STRIDE, (unsigned)BUFFER_BYTES,
             (unsigned)table_state.guard_address, (unsigned)OLD_LIMIT, (unsigned)NEW_LIMIT,
             (unsigned)WORD_COUNT, (unsigned)WORD_COUNT);
    log_info("vertex table: the trailer record at 0x006bf9f0 is UNCHANGED and untouched by this "
             "relocation; it was never part of the cache, only adjacent to it, which is exactly "
             "why the cache moves whole rather than growing in place.");
}

void vertex_table_restore(void)
{
    size_t index;
    size_t restored = 0;
    bool   ok = true;

    if (!table_state.active) {
        return;
    }

    /* Backwards: lower the gates first, then the operands. A frame running through the middle of
     * this would at worst see gates that are too TIGHT, never ones that allow more than the
     * buffer they point at can hold. */
    index = WORD_COUNT;
    while (index-- > 0) {
        if (!table_state.words[index].written) {
            continue;
        }
        if (patch_write_u32(table_state.words[index].address,
                            table_state.words[index].old_value) == PATCH_RESULT_OK) {
            table_state.words[index].written = false;
            ++restored;
        } else {
            ok = false;
            log_error("vertex table: restoring word %u (%s) at %08X failed",
                      (unsigned)(index + 1), table_state.words[index].description,
                      (unsigned)table_state.words[index].address);
        }
    }
    table_state.active = false;

    log_info("vertex table: restored %u/%u words %s; the buffer %08X is deliberately left "
             "standing (a frame already under way can still hold pointers into it).",
             (unsigned)restored, (unsigned)WORD_COUNT, ok ? "in order" : "WITH ERRORS",
             (unsigned)(uintptr_t)table_state.buffer);
}

bool vertex_table_is_active(void)
{
    return table_state.active;
}

uint32_t vertex_table_limit(void)
{
    return table_state.active ? NEW_LIMIT : OLD_LIMIT;
}
