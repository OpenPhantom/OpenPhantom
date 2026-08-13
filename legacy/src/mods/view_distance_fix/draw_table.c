/* draw_table.c: the nine words that move the cell table, and the six gates in front of them.
 *
 * ==============================================================================================
 * 0. what is and is not touched
 *
 *     0x008A4880  g_drawEntry   0x2000 entries * 0x0C = 0x18000 B
 *     0x008BC880  g_bucket      33 dwords   <-- 0x8A4880 + 0x2000*0x0C, not one BYTE of slack
 *     0x008BC920  g_faceKeyDrawn, then the draw counters, from 0x008C1000 the IMPORT TABLE
 *
 * NOT touched, and why:
 *   * The 14 references to g_bucket [0x8BC880]. The heads store POINTERS, not indices
 *     (`lea eax,[eax+0x8A4880]` -> `mov [ecx*4+0x8BC880],eax`); they point into the new buffer by
 *     themselves. The two consumers in bapvrt walk `e = e->next` with a NULL stop; there is
 *     NOWHERE a test "is this pointer still inside the table" that could answer wrongly after the
 *     move.
 *   * The two clear loops 0x4047C7 / 0x4050C7 (`mov ecx,0x21 / mov edi,0x8BC880 / rep stosd`).
 *     They clear the 33 HEADS, not the table, the table is never cleared at all, only the
 *     counter is reset. MEM_COMMIT hands back zeroed pages, so behaviour is identical from frame
 *     one.
 *   * The 30 references to the counter [0x59DEBC]; it is a counter, not an address.
 *   * The five references of the dead old renderer (bapdrawOld_*).
 *   * `0x406959 cmp ecx,0x2000` in baplight_applyLight. That is a batch limit with a flush, not a
 *     capacity. baplight keeps the old BSS table and stays cleanly inside it (last byte
 *     0x8A4880 + 12*0x1FFF + 3 = 0x8BC877 < 0x8BC880).
 *
 * ==============================================================================================
 * 1. ADDRESSING, patterns carry, but only with the right EXPECTED COUNT
 *
 * The four append blocks fall into two register forms, so the right key is not "one unique
 * pattern" but the TRIPLE (address-free pattern, expected match count, address read from the
 * operand):
 *
 *   anchor        bytes                                    WMAIN  obi.exe  netobi  obiold
 *   APPEND_EDX    8D 04 52 C1 E0 02 42 89 98                 1       1       1       1
 *   APPEND_ECX    8D 04 49 C1 E0 02 41 89 B8                 3       3       3      *4*
 *   GATHER_GATE   83 EC 18 3D ?? ?? ?? ?? 53 56 57 0F 83     1       1       1       1
 *
 *   * All three anchors are ADDRESS-FREE. The image is neither /DYNAMICBASE nor
 *     LARGE_ADDRESS_AWARE, but even under forced ASLR the patch stays right, because it READS the
 *     table address out of the operand instead of knowing it.
 *   * obiold.exe fails on the match count (4 instead of 3), netobi.exe on the cross-check
 *     (table + 0x2000*12 != bucket). Both gates are needed; neither alone suffices.
 *   * The immediate in GATHER_GATE is deliberately a WILDCARD. The obvious anchor
 *     `83 EC 18 3D 00 20 00 00 ...` would stop matching after cell_watchdog lowered the limit to
 *     0x1C00, and the relocation would refuse exactly when it is most needed. The mask changes
 *     nothing about uniqueness (exactly one hit in all nine images, measured) and makes the
 *     anchor independent of whatever was previously done to that limit. That is also why gate 4
 *     accepts two old values.
 *
 * ==============================================================================================
 * 2. THE SIZES, and why exactly these numbers
 *
 *     buffer     0x6000 = 24576 entries = 0x48000 B = 294,912 B (committed, RW)
 *     guard      1 page = 0x1000 B immediately behind it, PAGE_NOACCESS
 *     limit      0x4000 = 16384
 *     reserve    0x6000 - 0x4000 = 8192 entries
 *
 * The reserve has to cover the worst case that can happen AFTER the gate has tripped:
 *
 *  (a) gatherCell only checks ON ENTRY. Entered with n = limit-1, it appends the WHOLE cell. The
 *      body has exactly one append block in one loop over `numPoly` slots, so at most numPoly
 *      entries. Largest cell in the game: QUEEN with 135 slots. => 135.
 *
 *  (b) gatherCellMovers has NO limit at all. After the gate trips gatherCell returns immediately,
 *      but drawWorld calls gatherCellMovers FIRST per cell and gatherCell only afterwards, so
 *      the mover path keeps appending for every further visible cell. It is capped only by
 *      g_moverDrawState (each mover once per frame). Upper bound PER FRAME = the level's mover
 *      faces, maximum GUNGA 4117.
 *
 *      And the number that shows why a small reserve is not enough: a SINGLE mover carries up to
 *      855 faces (FINAL), and because a mover is collected once per frame, ALL of its faces must
 *      be appended in the SAME call. One gatherCellMovers call can therefore overshoot the
 *      reserve by 855 on its own.
 *
 *     Worst case total = 135 + 4117 = 4252 entries. Reserve 8192 / 4252 = factor 1.93. The guard
 *     page is therefore never touched in normal operation; if it ever is, something happened that
 *     none of the three analyses foresaw, and then it faults at the CAUSING instruction instead
 *     of ten minutes later in the import table.
 *
 * Why the limit goes to 16384 AND NOT HIGHER: the vertex cache has 16384 slots of 0x40 B (exactly
 * 1 MiB) and is the NEXT wall. Because unique vertices per face exceed 1.0 in all eleven levels
 * (1.075 .. 1.522), it always trips before a 16384-entry table does, at roughly 10,200 to
 * 11,900 faces.
 *
 * Why a guard page and not "a few thousand more entries": additional committed entries would
 * SWALLOW the overflow and hide the defect. And the proof that ONE page suffices is arithmetic:
 * the four append blocks write strictly ascending with a stride of 12, and every touched field of
 * an entry lies within the same 12 bytes, +0 (poly), +4 (next) and +8 (clipMask, written by the
 * CONSUMER at 0x41A17C). The highest touched byte of an entry is therefore base + 12n + 11.
 * 12 < 4096, so the write head cannot skip a page. And 0x48000 is exactly divisible by 12, so
 * entry 24576 begins precisely at the buffer end and hits the guard page with its first write.
 * Were the buffer size not a multiple of 12, the last entry could straddle the page boundary and
 * still write half of its bytes legally.
 *
 * ==============================================================================================
 * 3. ALL OR NOTHING
 *
 * A half-relocated state is the worst imaginable: four append blocks of which one writes into the
 * new buffer and the others into the old table would chain `next` pointers across two buffers,
 * and the bucket heads would point alternately into both. So:
 *
 *   pre-flight (NO write) -> buffer -> the eight operands -> the limit LAST.
 *
 * If one gate fails, NOTHING is written. If a write fails midway, everything already written is
 * restored from the backup. If THAT fails too, it is a state with no way back, then all that is
 * left is the log and a controlled exit.
 *
 * SIZE NOTE (rule 9): 645 lines, of which 419 are code and 155 are the arithmetic that proves
 * the buffer size, the reserve and the single guard page. That proof cannot live elsewhere.
 */
#include "draw_table.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ENTRY_STRIDE        12u
#define OLD_ENTRY_COUNT 0x2000u
#define BUFFER_ENTRIES  0x6000u
#define BUFFER_BYTES    (BUFFER_ENTRIES * ENTRY_STRIDE)
#define GUARD_BYTES     0x1000u
#define GATE_NEW        0x4000u
#define GATE_RETAIL     0x2000u
#define GATE_LOWERED    0x1C00u   /* what cell_watchdog writes, if it already ran */

/* The guard-page proof depends on the buffer being an exact multiple of the stride. A compile
 * error is the right reaction here, not a log line. */
_Static_assert((BUFFER_BYTES % ENTRY_STRIDE) == 0,
    "The draw-table buffer must be a whole number of entries");

#define WORD_COUNT 9

/* --- 0x004067CF  bapdraw_gatherCell, the edx append block ------------------------------------ */
static const uint8_t SIG_APPEND_EDX[] = { 0x8D, 0x04, 0x52, 0xC1, 0xE0, 0x02, 0x42, 0x89, 0x98 };

/* --- 0x00405EE4 / 0x004061AC / 0x00406472  the three ecx append blocks ------------------------ *
 * This pattern is deliberately not unique. It MUST match exactly THREE times (gatherCellMovers
 * twice, emitFace once). All three get the same patch. Any other count means an unknown image,
 * obiold.exe has four and is rejected on exactly that. */
static const uint8_t SIG_APPEND_ECX[] = { 0x8D, 0x04, 0x49, 0xC1, 0xE0, 0x02, 0x41, 0x89, 0xB8 };

/* --- 0x004064B5  bapdraw_gatherCell: the head with the only live limit ------------------------ *
 *   83 EC 18              sub  esp,0x18
 *   3D <imm32>            cmp  eax,<limit>       <- the immediate at +0x04, FOUR bytes, wildcard
 *   53 56 57              push ebx / esi / edi
 *   0F 83 <rel32>         jae  drop-the-cell                                                    */
static const uint8_t SIG_GATHER_GATE[] = {
    0x83, 0xEC, 0x18, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x53, 0x56, 0x57, 0x0F, 0x83
};
static const uint8_t MSK_GATHER_GATE[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

#define OFFSET_TABLE_PLUS_FOUR 0x09u   /* operand = table base + 4 */
#define OFFSET_TABLE           0x0Fu   /* operand = table base */
#define OFFSET_BUCKET          0x16u   /* edx anchor only; stays as it is */
#define OFFSET_COUNT           0x1Cu   /* edx anchor only; stays as it is */
#define OFFSET_GATE_IMMEDIATE  0x04u

#define EXPECTED_APPEND_EDX 1u
#define EXPECTED_APPEND_ECX 3u
#define EXPECTED_GATHER_GATE 1u

/* Expected raw census over .text, counted in both retail builds and cross-checked against the
 * PE relocation table:
 *   base   `0x8A4880`  10 = 4 dead (bapdrawOld_*) + 4 live appenders + 2 baplight
 *   base+4 `0x8A4884`   5 = 1 dead + 4 live
 * After the relocation those are 6 and 1, which is how a second run recognises itself. */
#define CENSUS_TABLE       10u
#define CENSUS_TABLE_PLUS4  5u

typedef struct patched_word {
    uintptr_t   address;      /* the VA of the 4-byte word itself */
    uint32_t    expected;     /* what must be there, or nothing is written */
    uint32_t    old_value;    /* what really was there before we wrote */
    uint32_t    new_value;
    bool        is_base;      /* 1 = the operand carries the table base, 0 = base+4 or the limit */
    bool        written;
    const char *description;
} patched_word_t;

typedef struct draw_table_state {
    patched_word_t words[WORD_COUNT];
    size_t         word_count;
    bool           active;
    bool           has_run;
    uint8_t       *buffer;
    uintptr_t      guard_address;
} draw_table_state_t;

static draw_table_state_t table_state;

/* ============================================================================================ */
static bool allocate_buffer(void)
{
    MEMORY_BASIC_INFORMATION information;
    uint8_t                 *reserved;

    /* Reserve the WHOLE range first (payload + guard) so nobody can slot in between, then commit
     * separately. The guard page is EXPLICITLY committed as PAGE_NOACCESS rather than left merely
     * reserved: then the protection is in the memory map, is checkable with VirtualQuery, and no
     * later allocation can take it. It costs 4 KiB of commit and no working set, because it is
     * never touched. */
    reserved = (uint8_t *)VirtualAlloc(NULL, BUFFER_BYTES + GUARD_BYTES, MEM_RESERVE,
                                       PAGE_NOACCESS);
    if (reserved == NULL) {
        log_error("VirtualAlloc(RESERVE %u B) failed (%lu) - NOT ONE BYTE patched",
                  (unsigned)(BUFFER_BYTES + GUARD_BYTES), (unsigned long)GetLastError());
        return false;
    }
    if (VirtualAlloc(reserved, BUFFER_BYTES, MEM_COMMIT, PAGE_READWRITE) == NULL) {
        log_error("VirtualAlloc(COMMIT %u B) failed (%lu) - NOT ONE BYTE patched",
                  (unsigned)BUFFER_BYTES, (unsigned long)GetLastError());
        VirtualFree(reserved, 0, MEM_RELEASE);
        return false;
    }
    if (VirtualAlloc(reserved + BUFFER_BYTES, GUARD_BYTES, MEM_COMMIT, PAGE_NOACCESS) == NULL) {
        log_error("the guard page at %08X could not be committed (%lu) - NOT ONE BYTE patched",
                  (unsigned)(uintptr_t)(reserved + BUFFER_BYTES), (unsigned long)GetLastError());
        VirtualFree(reserved, 0, MEM_RELEASE);
        return false;
    }

    /* Cross-check, so that "guard page" is not merely a word in the log. */
    if (VirtualQuery(reserved + BUFFER_BYTES, &information, sizeof(information))
            != sizeof(information) ||
        information.Protect != PAGE_NOACCESS || information.State != MEM_COMMIT) {
        log_error("the guard page at %08X does not carry PAGE_NOACCESS (State %08lX, "
                  "Protect %08lX) - NOT ONE BYTE patched",
                  (unsigned)(uintptr_t)(reserved + BUFFER_BYTES),
                  (unsigned long)information.State, (unsigned long)information.Protect);
        VirtualFree(reserved, 0, MEM_RELEASE);
        return false;
    }

    /* Without LARGE_ADDRESS_AWARE this cannot happen; the check costs nothing and pins the
     * assumption down instead of believing it. */
    if ((uintptr_t)reserved >= 0x80000000u) {
        log_error("the buffer at %08X lies above 2 GiB - refused", (unsigned)(uintptr_t)reserved);
        VirtualFree(reserved, 0, MEM_RELEASE);
        return false;
    }

    /* The buffer is left as MEM_COMMIT hands it over: zero-filled. Behaviour is identical to the
     * BSS from frame one, and a memset would touch 288 KiB the engine may never need. */
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
            log_error("ROLLBACK of word %u (%s, %08X -> %08X) FAILED",
                      (unsigned)(index + 1), table_state.words[index].description,
                      (unsigned)table_state.words[index].address,
                      (unsigned)table_state.words[index].old_value);
        }
    }

    if (ok) {
        log_info("%s, every already-written site was restored, the game runs unchanged", why);
    }
    return ok;
}

/* ============================================================================================
 * The gates. Each returns false without writing anything.
 * ============================================================================================ */
static bool find_anchors(uintptr_t *append_edx, uintptr_t *append_ecx, uintptr_t *gate)
{
    size_t edx_hits;
    size_t ecx_hits;
    size_t gate_hits;

    edx_hits  = signature_count_matches(SIG_APPEND_EDX, NULL, sizeof(SIG_APPEND_EDX),
                                        append_edx, 4);
    ecx_hits  = signature_count_matches(SIG_APPEND_ECX, NULL, sizeof(SIG_APPEND_ECX),
                                        append_ecx, 8);
    gate_hits = signature_count_matches(SIG_GATHER_GATE, MSK_GATHER_GATE,
                                        sizeof(SIG_GATHER_GATE), gate, 4);

    log_info("anchors (all three ADDRESS-FREE, so ASLR-proof; the table address is READ out of "
             "the operand): append_edx %u/%u, append_ecx %u/%u, gather_gate %u/%u",
             (unsigned)edx_hits, (unsigned)EXPECTED_APPEND_EDX,
             (unsigned)ecx_hits, (unsigned)EXPECTED_APPEND_ECX,
             (unsigned)gate_hits, (unsigned)EXPECTED_GATHER_GATE);

    if (edx_hits != EXPECTED_APPEND_EDX || ecx_hits != EXPECTED_APPEND_ECX ||
        gate_hits != EXPECTED_GATHER_GATE) {
        log_warning("unexpected match count, unknown image, NOT ONE BYTE patched. (obiold.exe "
                    "carries four ecx blocks instead of three and is recognised by exactly that.)");
        return false;
    }
    return true;
}

static bool check_table_addresses(uintptr_t append_edx, uint32_t *out_table)
{
    uint32_t table_plus_four;
    uint32_t table;
    uint32_t bucket;
    uint32_t counter;

    if (!memory_read_u32(append_edx + OFFSET_TABLE_PLUS_FOUR, &table_plus_four) ||
        !memory_read_u32(append_edx + OFFSET_TABLE,           &table) ||
        !memory_read_u32(append_edx + OFFSET_BUCKET,          &bucket) ||
        !memory_read_u32(append_edx + OFFSET_COUNT,           &counter)) {
        return false;
    }

    if (!memory_is_inside_image(table, ENTRY_STRIDE)) {
        log_info("the table base %08X lies outside the image, the table is ALREADY RELOCATED. "
                 "Nothing to do.", (unsigned)table);
        return false;
    }
    if (table_plus_four != table + 4 ||
        table + OLD_ENTRY_COUNT * ENTRY_STRIDE != bucket ||
        !memory_is_inside_image(bucket, sizeof(uint32_t)) ||
        !memory_is_inside_image(counter, sizeof(uint32_t))) {
        log_warning("cross-check failed, table %08X, +4 %08X, table+0x2000*12 = %08X, "
                    "buckets %08X, counter %08X. NOT ONE BYTE patched. (netobi.exe fails exactly "
                    "here.)",
                    (unsigned)table, (unsigned)table_plus_four,
                    (unsigned)(table + OLD_ENTRY_COUNT * ENTRY_STRIDE),
                    (unsigned)bucket, (unsigned)counter);
        return false;
    }

    *out_table = table;
    return true;
}

static bool check_ecx_anchors(const uintptr_t *append_ecx, uint32_t table)
{
    size_t index;

    for (index = 0; index < EXPECTED_APPEND_ECX; ++index) {
        uint32_t plus_four = 0;
        uint32_t base = 0;

        if (!memory_read_u32(append_ecx[index] + OFFSET_TABLE_PLUS_FOUR, &plus_four) ||
            !memory_read_u32(append_ecx[index] + OFFSET_TABLE, &base)) {
            return false;
        }
        if (plus_four != table + 4 || base != table) {
            log_warning("ecx anchor %08X carries %08X/%08X instead of %08X/%08X - NOT ONE BYTE "
                        "patched", (unsigned)append_ecx[index], (unsigned)plus_four,
                        (unsigned)base, (unsigned)(table + 4), (unsigned)table);
            return false;
        }
    }
    return true;
}

static bool check_gate_immediate(uintptr_t gate_immediate, uint32_t *out_value)
{
    uint32_t value;

    if (!memory_read_u32(gate_immediate, &value)) {
        return false;
    }
    if (value == GATE_NEW) {
        log_info("the limit at %08X already reads %u - ALREADY RELOCATED, nothing to do",
                 (unsigned)gate_immediate, (unsigned)value);
        return false;
    }
    if (value != GATE_RETAIL && value != GATE_LOWERED) {
        log_warning("the limit at %08X reads %u; expected %u (shipped) or %u (lowered by the cell "
                    "watchdog) - NOT ONE BYTE patched",
                    (unsigned)gate_immediate, (unsigned)value,
                    (unsigned)GATE_RETAIL, (unsigned)GATE_LOWERED);
        return false;
    }

    *out_value = value;
    return true;
}

static bool check_census(uint32_t table)
{
    size_t table_hits = signature_count_dword(table);
    size_t plus4_hits = signature_count_dword(table + 4);

    if (table_hits == CENSUS_TABLE && plus4_hits == CENSUS_TABLE_PLUS4) {
        return true;
    }
    if (table_hits == CENSUS_TABLE - 4 && plus4_hits == CENSUS_TABLE_PLUS4 - 4) {
        log_info("census %u/%u instead of %u/%u, the four live appenders already point "
                 "elsewhere, the table is ALREADY RELOCATED. Nothing to do.",
                 (unsigned)table_hits, (unsigned)plus4_hits,
                 (unsigned)CENSUS_TABLE, (unsigned)CENSUS_TABLE_PLUS4);
        return false;
    }
    log_warning("census %u/%u instead of %u/%u, unknown image, NOT ONE BYTE patched",
                (unsigned)table_hits, (unsigned)plus4_hits,
                (unsigned)CENSUS_TABLE, (unsigned)CENSUS_TABLE_PLUS4);
    return false;
}

/* ============================================================================================ */
static void add_word(uintptr_t address, uint32_t expected, bool is_base, const char *description)
{
    patched_word_t *word = &table_state.words[table_state.word_count];

    word->address     = address;
    word->expected    = expected;
    word->is_base     = is_base;
    word->description = description;
    word->written     = false;
    ++table_state.word_count;
}

/* Every site carries its own expected value, rather than a parity computation over the index that
 * silently flips at the next rework. */
static bool build_word_list(const uintptr_t *append_ecx, uintptr_t append_edx,
                            uintptr_t gate_immediate, uint32_t table, uint32_t gate_value)
{
    size_t index;

    table_state.word_count = 0;

    for (index = 0; index < EXPECTED_APPEND_ECX; ++index) {
        add_word(append_ecx[index] + OFFSET_TABLE_PLUS_FOUR, table + 4, false,
                 "ecx block: entry->next");
        add_word(append_ecx[index] + OFFSET_TABLE, table, true, "ecx block: &entry");
    }
    add_word(append_edx + OFFSET_TABLE_PLUS_FOUR, table + 4, false, "edx block: entry->next");
    add_word(append_edx + OFFSET_TABLE, table, true, "edx block: &entry");
    add_word(gate_immediate, gate_value, false, "the limit");

    if (table_state.word_count != WORD_COUNT) {
        log_error("%u instead of %u sites collected - NOT ONE BYTE patched",
                  (unsigned)table_state.word_count, (unsigned)WORD_COUNT);
        table_state.word_count = 0;
        return false;
    }

    for (index = 0; index < WORD_COUNT; ++index) {
        if (!memory_read_u32(table_state.words[index].address,
                             &table_state.words[index].old_value)) {
            table_state.word_count = 0;
            return false;
        }
        if (table_state.words[index].old_value != table_state.words[index].expected) {
            log_warning("pre-flight: site %u/%u (%s) at %08X carries %08X, expected %08X - NOT "
                        "ONE BYTE patched",
                        (unsigned)(index + 1), (unsigned)WORD_COUNT,
                        table_state.words[index].description,
                        (unsigned)table_state.words[index].address,
                        (unsigned)table_state.words[index].old_value,
                        (unsigned)table_state.words[index].expected);
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

        log_error("write %u/%u (%s, %08X: %08X -> %08X) FAILED",
                  (unsigned)(index + 1), (unsigned)WORD_COUNT,
                  table_state.words[index].description,
                  (unsigned)table_state.words[index].address,
                  (unsigned)table_state.words[index].old_value,
                  (unsigned)table_state.words[index].new_value);

        if (!roll_back("partial failure")) {
            log_error("THE COLLECTION PATH IS HALF RELOCATED AND CANNOT BE ROLLED BACK. The "
                      "append blocks now write into TWO different buffers, and the bucket list "
                      "heads point alternately into both. Continuing would mean driving into a "
                      "silent memory corruption. The game is terminated in a controlled way.");
            log_shutdown();
            TerminateProcess(GetCurrentProcess(), 0xDEAD0001u);
        }

        /* The buffer is DELIBERATELY not freed: if a frame did run between the first write and
         * the rollback, the bucket list heads still hold pointers into it. A freed page turns a
         * harmless stale pointer into an access violation. 288 KiB of address space is cheaper. */
        table_state.word_count = 0;
        return false;
    }

    return true;
}

/* ============================================================================================ */
void draw_table_relocate(void)
{
    uintptr_t append_edx[4];
    uintptr_t append_ecx[8];
    uintptr_t gate[4];
    uintptr_t gate_immediate;
    uint32_t  table = 0;
    uint32_t  gate_value = 0;
    uint32_t  base;
    size_t    index;

    if (table_state.has_run) {
        log_info("draw_table_relocate called a second time, ignored (the table is %s)",
                 table_state.active ? "already relocated" : "not relocated");
        return;
    }
    table_state.has_run = true;

    if (!find_anchors(append_edx, append_ecx, gate)) {
        return;
    }
    gate_immediate = gate[0] + OFFSET_GATE_IMMEDIATE;

    if (!check_table_addresses(append_edx[0], &table)) {
        return;
    }
    if (!check_ecx_anchors(append_ecx, table)) {
        return;
    }
    if (!check_gate_immediate(gate_immediate, &gate_value)) {
        return;
    }
    if (!check_census(table)) {
        return;
    }
    if (!build_word_list(append_ecx, append_edx[0], gate_immediate, table, gate_value)) {
        return;
    }

    log_info("pre-flight %u/%u in order. Table %08X (%u entries of %u B), limit %08X = %u.",
             (unsigned)WORD_COUNT, (unsigned)WORD_COUNT, (unsigned)table,
             (unsigned)OLD_ENTRY_COUNT, (unsigned)ENTRY_STRIDE,
             (unsigned)gate_immediate, (unsigned)gate_value);

    if (!allocate_buffer()) {
        table_state.word_count = 0;
        return;
    }
    base = (uint32_t)(uintptr_t)table_state.buffer;

    for (index = 0; index < WORD_COUNT - 1; ++index) {
        table_state.words[index].new_value = table_state.words[index].is_base ? base : base + 4;
    }
    table_state.words[WORD_COUNT - 1].new_value = GATE_NEW;

    /* Order: the EIGHT operands first, the limit LAST. If one of the eight fails, the limit stays
     * low and the engine runs on with the old boundary. */
    if (!write_all_words()) {
        return;
    }

    table_state.active = true;
    log_info("draw table relocated to %08X (%u entries of %u B = %u B), limit %u -> %u, guard "
             "page %08X (PAGE_NOACCESS), %u/%u operands written.",
             (unsigned)base, (unsigned)BUFFER_ENTRIES, (unsigned)ENTRY_STRIDE,
             (unsigned)BUFFER_BYTES, (unsigned)gate_value, (unsigned)GATE_NEW,
             (unsigned)table_state.guard_address, (unsigned)WORD_COUNT, (unsigned)WORD_COUNT);
    log_info("reserve between limit and buffer end: %u entries against a byte-proven worst case "
             "of 4252 (135 cell slots QUEEN + 4117 mover faces GUNGA) = factor 1.93. A SINGLE "
             "mover carries up to 855 faces and appends them all in one call, that is why this "
             "size and not 256 or 1024.", (unsigned)(BUFFER_ENTRIES - GATE_NEW));
    log_info("the bucket heads and the counter are UNCHANGED - the heads store pointers, not "
             "indices, and point into the new buffer by themselves. The old BSS table stays put; "
             "it now belongs to baplight_applyLight, which keeps using it as a light batch and "
             "stays cleanly inside it.");
    log_warning("the next wall is now the vertex cache (16384 slots of 0x40 B). It aborts "
                "cleanly but leaves torn geometry until the level reloads.");
}

void draw_table_restore(void)
{
    size_t index;
    size_t restored = 0;
    bool   ok = true;

    if (!table_state.active) {
        return;
    }

    /* Backwards: lower the limit first, then the operands. A frame running through the middle of
     * this would at worst see a limit that is too TIGHT, never one that is too wide. */
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
            log_error("restoring word %u (%s) at %08X failed",
                      (unsigned)(index + 1), table_state.words[index].description,
                      (unsigned)table_state.words[index].address);
        }
    }
    table_state.active = false;

    /* The buffer is not freed. g_bucket can still hold pointers into it (the heads are only
     * zeroed in the next bapdraw_drawWorld), and at this point we do not know whether another
     * frame is coming. A stale pointer into valid memory is harmless; one into freed address
     * space is an access violation. Windows cleans up at process exit. */
    log_info("restored %u/%u words %s; the buffer %08X is deliberately left standing (the bucket "
             "list heads can still point into it).",
             (unsigned)restored, (unsigned)WORD_COUNT, ok ? "in order" : "WITH ERRORS",
             (unsigned)(uintptr_t)table_state.buffer);
}

bool draw_table_is_active(void)
{
    return table_state.active;
}

uint32_t draw_table_limit(void)
{
    return table_state.active ? GATE_NEW : GATE_RETAIL;
}
