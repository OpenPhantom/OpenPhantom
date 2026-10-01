/* memory.c: the guarded reads and writes, checked without the game.
 *
 * There are two forms and the difference is the whole point of this file. The ASKING form,
 * memory_is_readable_range and memory_read, walks the page tables with VirtualQuery before it
 * touches anything, which costs a system call and is worth it while a feature is installing and a
 * refusal wants explaining. The TRYING form catches the fault instead, and belongs on any path the
 * engine drives, where a syscall per read per object is what this project has already paid for
 * once.
 *
 * What both must do, and what is checked here, is refuse an address this process does not own
 * rather than taking the program down with it. The trying form is the one that can only be proved
 * by pointing it at memory that is not there.
 *
 * The run time reads of the multiplayer move from the first form to the second one for one, and
 * that move is only a swap if both forms answer the same for every kind of page the engine can
 * hand over. So every kind is made here, in this process: read and write, read only, no access,
 * reserved and never committed, given back, guarded, the null page, an address above the
 * application's top, and a range that starts on one kind and ends on another. The trying form as it
 * was before the swap is kept below word for word as the reference, because the one place the two
 * were allowed to differ, the guard page, is a difference against it.
 *
 * memory_try_write is the same bargain for a store into a record the engine allocated, as opposed
 * to code, which goes through patch.c. This program's own memory has pages it may write, pages it
 * may only read, and pages that are not mapped at all, so the promise can be held to here too.
 *
 * The counting is checked last: a fault counts once per call however much of the range was read,
 * a refusal before the touch counts as outside and never as a fault, and the asking side names
 * the place it was called from.
 */
#include "unittest.h"

#include "common/memory.h"

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The first page is never mapped on Windows, so this is an address no process can read. */
#define UNMAPPED ((uintptr_t)0x10)

/* Where the first of eight access violations in one field run pointed: above the top of a
 * process that is not large address aware, so the system's and never this process's. */
#define ABOVE_THE_TOP ((uintptr_t)0xAB20E63Cu)

static const unsigned char source[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22, 0x33, 0x44 };

/* The trying read as it was before the swap, word for word: every exception swallowed, nothing
 * refused before the touch, nothing counted. */
static bool reference_try_read(uintptr_t address, void *destination, size_t size)
{
    if (destination == NULL || size == 0) {
        return false;
    }
    if (address + size < address) {
        return false;                              /* wrapped: the caller computed nonsense */
    }

    __try {
        memcpy(destination, (const void *)address, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static size_t page_size(void)
{
    SYSTEM_INFO info;

    GetSystemInfo(&info);
    return info.dwPageSize;
}

/* Two pages, the second given back, and a pointer two bytes before the seam. A range of four
 * from there begins in memory this process holds and ends in memory it only reserved, which is
 * the shape the range rule stands on: a table one entry longer than its region reads fine at
 * its start and faults at its end. */
static unsigned char *edge_of_a_hole(void)
{
    size_t         page = page_size();
    unsigned char *pages;

    pages = (unsigned char *)VirtualAlloc(NULL, 2u * page, MEM_RESERVE | MEM_COMMIT,
                                          PAGE_READWRITE);
    if (pages == NULL) {
        return NULL;
    }
    if (!VirtualFree(pages + page, page, MEM_DECOMMIT)) {
        return NULL;
    }
    return pages + page - 2u;
}

/* One committed page of the given protection, with its first word written while it could be. */
static unsigned char *page_of(DWORD protection)
{
    unsigned char *page = (unsigned char *)VirtualAlloc(NULL, page_size(),
                                                        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD          previous;

    if (page == NULL) {
        return NULL;
    }
    memcpy(page, source, sizeof source);
    if (protection != PAGE_READWRITE && !VirtualProtect(page, 1u, protection, &previous)) {
        return NULL;
    }
    return page;
}

static bool guarded(const void *address)
{
    MEMORY_BASIC_INFORMATION information;

    return VirtualQuery(address, &information, sizeof information) == sizeof information &&
           (information.Protect & PAGE_GUARD) != 0u;
}

static uint32_t faults(memory_watch_kind_t kind)
{
    return memory_watch_stats()->faults[kind];
}

static uint32_t outside(void)
{
    return memory_watch_stats()->outside;
}

/* Written into, so not const. A known pattern, so a partial write would be visible. */
static uint8_t record[16] = {
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA
};

/* Read only data lands on a page the process may not write, which is the fault the write exists to
 * survive. */
static const uint8_t frozen[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

static void check_the_guarded_write(void)
{
    const uint8_t four[4] = { 0x11, 0x22, 0x33, 0x44 };

    ut_section("a write that should land");
    ut_check(memory_try_write((uintptr_t)record, four, sizeof four),
             "four bytes into a writable record are accepted");
    ut_check(memcmp(record, four, sizeof four) == 0, "and are there afterwards");
    ut_check(record[4] == 0xAA, "with nothing written past the length asked for");

    ut_check(memory_try_write((uintptr_t)record + 8u, four, sizeof four),
             "a write in the middle of the record lands too");
    ut_check(memcmp(record + 8, four, sizeof four) == 0,
             "where it was asked to");
    ut_check(record[7] == 0xAA && record[12] == 0xAA,
             "and touches only its own four bytes");

    ut_section("what the write refuses without touching anything");
    ut_check(!memory_try_write((uintptr_t)record, NULL, sizeof four), "no source is refused");
    ut_check(!memory_try_write((uintptr_t)record, four, 0),
             "a length of zero is refused rather than treated as success");
    ut_check(!memory_try_write(UINTPTR_MAX - 1u, four, sizeof four),
             "a range that wraps around the top of the address space is refused before any store");

    ut_section("a page the process may not write");
    ut_check(!memory_try_write(UNMAPPED, four, sizeof four),
             "an unmapped address is refused, and the process is still here to say so");
    ut_check(!memory_try_write((uintptr_t)frozen, four, sizeof four),
             "a read only page is refused too, by the fault its store raises");
    ut_check(frozen[0] == 1 && frozen[3] == 4, "and its contents are what they were");
    ut_check(memcmp(record, four, sizeof four) == 0,
             "and none of those refusals disturbed the record written earlier");
}

/* Both forms, and the reference, over one kind of page: the same answer, and the same word when
 * the answer is yes. */
static void both_forms(const char *kind, uintptr_t address, bool readable)
{
    uint32_t asked = 0;
    uint32_t tried = 0;
    uint32_t referenced = 0;
    bool     ask = memory_read_u32(address, &asked);
    bool     reference = reference_try_read(address, &referenced, sizeof referenced);
    bool     try_word = memory_try_read_u32(address, &tried);

    ut_checkf(ask == readable, "%s: the asking form answers %s", kind, readable ? "yes" : "no");
    ut_checkf(try_word == ask, "%s: the trying form answers as the asking form does", kind);
    ut_checkf(reference == ask, "%s: and so did the trying form before the swap", kind);
    ut_checkf(!readable || (asked == tried && tried == referenced),
              "%s: and all three read the same word", kind);
}

static void check_every_kind_of_page(void)
{
    size_t         page = page_size();
    unsigned char *rw = page_of(PAGE_READWRITE);
    unsigned char *ro = page_of(PAGE_READONLY);
    unsigned char *none = page_of(PAGE_NOACCESS);
    unsigned char *reserved = (unsigned char *)VirtualAlloc(NULL, page, MEM_RESERVE,
                                                            PAGE_NOACCESS);
    unsigned char *released = page_of(PAGE_READWRITE);
    uintptr_t      top;

    ut_section("every kind of page, the asking form against the trying form");
    if (rw == NULL || ro == NULL || none == NULL || reserved == NULL || released == NULL) {
        ut_check(0, "the pages were made (prerequisite)");
        return;
    }
    (void)VirtualFree(released, 0, MEM_RELEASE);
    {
        SYSTEM_INFO info;

        GetSystemInfo(&info);
        top = (uintptr_t)info.lpMaximumApplicationAddress;
    }

    both_forms("read and write", (uintptr_t)rw, true);
    both_forms("read only", (uintptr_t)ro, true);
    both_forms("no access", (uintptr_t)none, false);
    both_forms("reserved and never committed", (uintptr_t)reserved, false);
    both_forms("given back", (uintptr_t)released, false);
    both_forms("the null page", UNMAPPED, false);
    both_forms("above the application's top", ABOVE_THE_TOP, false);
    both_forms("four bytes across the application's top", top - 1u, false);

    {
        uint32_t word = 0;

        ut_check(!memory_read_u32(UINTPTR_MAX - 1u, &word) &&
                     !memory_try_read_u32(UINTPTR_MAX - 1u, &word),
                 "a range that wraps is refused by both");
        ut_check(!memory_read((uintptr_t)rw, &word, 0u) &&
                     !memory_try_read((uintptr_t)rw, &word, 0u),
                 "a length of zero is refused by both");
    }

    (void)VirtualFree(rw, 0, MEM_RELEASE);
    (void)VirtualFree(ro, 0, MEM_RELEASE);
    (void)VirtualFree(none, 0, MEM_RELEASE);
    (void)VirtualFree(reserved, 0, MEM_RELEASE);
}

/* The one kind the two were allowed to answer differently, and now do not. The asking form sees
 * the guard bit and never touches; a touch makes the system take the bit off before it reports.
 * The trying form before the swap left it off, which for another thread's stack guard means a
 * stack that can no longer grow. */
static void check_the_guard_page(void)
{
    unsigned char *page = page_of(PAGE_READWRITE | PAGE_GUARD);
    uint32_t       word = 0;
    uint32_t       before;

    ut_section("a guard page");
    if (page == NULL || !guarded(page)) {
        ut_check(0, "a guarded page was made (prerequisite)");
        return;
    }
    ut_check(!memory_read_u32((uintptr_t)page, &word), "the asking form refuses it");
    ut_check(guarded(page), "without touching it: the guard is still on");

    before = faults(MEMORY_WATCH_READ);
    ut_check(!memory_try_read_u32((uintptr_t)page, &word), "the trying form refuses it too");
    ut_check(guarded(page), "and puts the guard back on, so the page is as it found it");
    ut_check(faults(MEMORY_WATCH_READ) == before + 1u, "and counts one fault for the one touch");

    ut_check(!reference_try_read((uintptr_t)page, &word, sizeof word),
             "the trying form before the swap refused it as well");
    ut_check(!guarded(page), "but left the page without its guard, which the current form puts "
                             "back");

    (void)VirtualFree(page, 0, MEM_RELEASE);
}

/* Twelve bytes, the size of the three pool words the fog's script reader takes at once, over the
 * seam between a page this process holds and one it may not touch. The copy stops at the first
 * byte it may not read, the handler takes over, and the call is one fault, not one per word. */
static void check_twelve_bytes_over_a_seam(void)
{
    size_t         page = page_size();
    unsigned char *pages = (unsigned char *)VirtualAlloc(NULL, 2u * page, MEM_RESERVE | MEM_COMMIT,
                                                         PAGE_READWRITE);
    unsigned char  out[12];
    DWORD          previous;
    uint32_t       before;
    size_t         i;
    bool           unchanged = true;

    ut_section("twelve bytes over the seam into a page nobody may read");
    if (pages == NULL || !VirtualProtect(pages + page, page, PAGE_NOACCESS, &previous)) {
        ut_check(0, "a page and a no access page behind it were made (prerequisite)");
        return;
    }
    memset(pages + page - 8u, 0x5A, 8u);
    memory_watch_reset();

    memset(out, 0xCC, sizeof out);
    before = faults(MEMORY_WATCH_READ);
    ut_check(!memory_try_read((uintptr_t)(pages + page - 8u), out, sizeof out),
             "a read that begins eight bytes before the seam is refused");
    ut_check(faults(MEMORY_WATCH_READ) == before + 1u,
             "and it is exactly one fault for the one call, not one per word");
    for (i = 8u; i < sizeof out; ++i) {
        unchanged = unchanged && out[i] == 0xCC;
    }
    ut_check(unchanged, "the four bytes past the seam were never written into the destination");

    memset(out, 0xCC, sizeof out);
    before = faults(MEMORY_WATCH_READ);
    ut_check(!memory_try_read((uintptr_t)(pages + page + 16u), out, sizeof out),
             "a read wholly inside the no access page is refused");
    ut_check(faults(MEMORY_WATCH_READ) == before + 1u, "with one fault as well");
    unchanged = true;
    for (i = 0u; i < sizeof out; ++i) {
        unchanged = unchanged && out[i] == 0xCC;
    }
    ut_check(unchanged, "and the destination untouched");
    ut_check(memory_watch_stats()->refusers >= 1u &&
                 memory_watch_stats()->refuser[0].kind == (uint8_t)MEMORY_WATCH_READ &&
                 !memory_watch_stats()->refuser[0].outside,
             "the fault is filed against its caller as a read that faulted");

    (void)VirtualFree(pages, 0, MEM_RELEASE);
}

/* The refusal before the touch: an address no process memory can be at is never touched. */
static void check_the_range_before_the_touch(void)
{
    uint32_t word = 0;
    uint32_t read_faults = faults(MEMORY_WATCH_READ);
    uint32_t probe_faults = faults(MEMORY_WATCH_PROBE);
    uint32_t write_faults = faults(MEMORY_WATCH_WRITE);
    uint32_t outside_before = outside();
    SYSTEM_INFO info;

    ut_section("refused before the touch, outside the application's addresses");
    GetSystemInfo(&info);
    ut_check(!memory_try_read_u32(UNMAPPED, &word), "the null page is refused");
    ut_check(!memory_try_readable(UNMAPPED, 4u), "by the probe too");
    ut_check(!memory_try_write(UNMAPPED, &word, sizeof word), "and by the write");
    ut_check(faults(MEMORY_WATCH_READ) == read_faults &&
                 faults(MEMORY_WATCH_PROBE) == probe_faults &&
                 faults(MEMORY_WATCH_WRITE) == write_faults,
             "with no fault among them");
    ut_check(outside() == outside_before + 3u, "and each counted as refused outside");

    if ((uintptr_t)info.lpMaximumApplicationAddress < ABOVE_THE_TOP) {
        ut_check(!memory_try_read_u32(ABOVE_THE_TOP, &word) &&
                     faults(MEMORY_WATCH_READ) == read_faults &&
                     outside() == outside_before + 4u,
                 "an address above the top is refused the same way, before any touch");
    }
    ut_check(!memory_try_read((uintptr_t)info.lpMaximumApplicationAddress - 1u, &word,
                              sizeof word) &&
                 faults(MEMORY_WATCH_READ) == read_faults,
             "and so is a range that begins below the top and ends above it");
}

static void check_the_asking_side_is_counted(void)
{
    const memory_watch_stats_t *s = memory_watch_stats();
    uint32_t                    word = 0;
    int                         i;
    bool                        two_and_none = false;
    bool                        one_and_one = false;
    uint32_t                    row;

    ut_section("the asking side, counted with its caller");
    memory_watch_arm(false, true);
    memory_watch_reset();
    (void)memory_read_u32((uintptr_t)source, &word);
    ut_check(s->calls == 0u, "unarmed, a call is not counted");

    memory_watch_arm(true, true);
    for (i = 0; i < 2; ++i) {
        (void)memory_read_u32((uintptr_t)source, &word);
    }
    (void)memory_read_u32(UNMAPPED, &word);
    ut_check(s->calls == 3u && s->refused == 1u, "armed, three calls and one refusal");
    ut_check(s->callers == 2u, "from two places, named apart");
    for (row = 0; row < s->callers; ++row) {
        two_and_none = two_and_none || (s->caller[row].calls == 2u && s->caller[row].refused == 0u);
        one_and_one = one_and_one || (s->caller[row].calls == 1u && s->caller[row].refused == 1u);
    }
    ut_check(two_and_none && one_and_one,
             "the loop's two calls on one row, the refusal on the other");
    ut_check(s->region[0] + s->region[1] + s->region[2] + s->region[3] == 3u,
             "every call has the run behind its address in a band");
    ut_check(s->minute[0].calls == 3u, "and all three fall into the first minute");

    (void)memory_try_read_u32((uintptr_t)source, &word);
    ut_check(s->calls == 3u, "the trying form is never counted as asking");

    ut_check(memory_watch_region_band(0u) == 0u && memory_watch_region_band(65535u) == 0u &&
                 memory_watch_region_band(65536u) == 1u &&
                 memory_watch_region_band(1024u * 1024u) == 1u &&
                 memory_watch_region_band(1024u * 1024u + 1u) == 2u &&
                 memory_watch_region_band(4u * 1024u * 1024u) == 2u &&
                 memory_watch_region_band(4u * 1024u * 1024u + 1u) == 3u,
             "the bands: under 64 KB, up to 1 MB, up to 4 MB, larger");
    memory_watch_arm(false, true);
}

/* The asking mode is how the census is timed both ways: the two conveniences ask first, and what
 * they then say is exactly what the asking form says, the guard page included. */
static void check_the_asking_mode(void)
{
    unsigned char *page = page_of(PAGE_READWRITE | PAGE_GUARD);
    uint32_t       word = 0;
    uint32_t       before;
    uint32_t       calls;

    ut_section("the asking mode of the two conveniences");
    memory_watch_arm(true, true);
    before = faults(MEMORY_WATCH_READ);
    calls  = memory_watch_stats()->calls;
    memory_watch_asking_mode(true);
    ut_check(memory_try_read_u32((uintptr_t)source, &word) && word == 0xEFBEADDEu,
             "a readable word reads the same");
    ut_check(page != NULL && !memory_try_read_u32((uintptr_t)page, &word) && guarded(page),
             "a guard page is refused without a touch, as the asking form refuses it");
    ut_check(faults(MEMORY_WATCH_READ) == before, "so there is no fault to count");
    ut_check(memory_watch_stats()->calls == calls, "and its questions are not the asking side's");
    memory_watch_asking_mode(false);
    memory_watch_arm(false, true);
    if (page != NULL) {
        (void)VirtualFree(page, 0, MEM_RELEASE);
    }
}

int main(void)
{
    unsigned char  out[8];
    uint8_t        byte = 0;
    uint32_t       word = 0;
    unsigned char *hole = edge_of_a_hole();

    memory_watch_arm(false, true);

    ut_section("the trying form reads what is there");
    memset(out, 0, sizeof out);
    ut_check(memory_try_read((uintptr_t)source, out, sizeof out),
             "a readable range is read");
    ut_check(memcmp(out, source, sizeof out) == 0,
             "and every byte arrives, in order");

    ut_check(memory_try_read_u8((uintptr_t)source, &byte) && byte == 0xDE,
             "the byte helper reads one byte and no more");
    ut_check(memory_try_read_u32((uintptr_t)source, &word) && word == 0xEFBEADDEu,
             "the word helper reads four, little end first, as the engine stores them");

    ut_section("what it refuses rather than faulting on");
    ut_check(!memory_try_read(UNMAPPED, out, sizeof out),
             "an address this process does not own is refused, and the refusal is a return "
             "rather than a crash. This form exists for that alone");
    ut_check(!memory_try_read_u8(UNMAPPED, &byte),
             "the byte helper refuses it too");
    ut_check(!memory_try_read_u32(UNMAPPED, &word),
             "and so does the word helper");
    ut_check(!memory_try_readable(UNMAPPED, 4u),
             "and asking whether it is readable answers no rather than faulting");

    ut_check(!memory_try_read((uintptr_t)source, NULL, sizeof out),
             "a null destination is refused");
    ut_check(!memory_try_read((uintptr_t)source, out, 0u),
             "a length of zero is refused rather than counted as success");

    /* A length that carries the end of the range past the top of the address space. Computed
     * rather than written, so it is right on any word size this ever builds for. */
    ut_check(!memory_try_read((uintptr_t)source, out, (size_t)0 - 1),
             "a length that wraps the address is refused before anything is read");

    ut_section("the asking form agrees about the same addresses");
    ut_check(memory_is_readable_range((uintptr_t)source, sizeof source),
             "it accepts the range the trying form just read");
    ut_check(!memory_is_readable_range(UNMAPPED, 4u),
             "and refuses the one the trying form refused");

    ut_section("a range that starts in memory this process holds and ends outside it");
    if (hole == NULL) {
        ut_check(0, "two pages were allocated and the second given back (prerequisite)");
    } else {
        hole[0] = 0x5A;
        hole[1] = 0xA5;
        ut_check(memory_try_readable((uintptr_t)hole, 2u),
                 "the two bytes up to the seam are readable, so the seam is where it was put");
        ut_check(!memory_try_readable((uintptr_t)hole, 4u),
                 "the trying form refuses a range whose first byte reads and whose last faults, "
                 "which is the property it touches both ends for");
        memset(out, 0, sizeof out);
        ut_check(!memory_try_read((uintptr_t)hole, out, 4u),
                 "and a read of that range is refused");
        ut_check(!memory_is_readable_range((uintptr_t)hole, 4u),
                 "the asking form refuses it too: the walk reaches the second region and finds "
                 "it uncommitted, which a single query at the start address would have missed");
        ut_check(memory_is_readable_range((uintptr_t)hole, 2u),
                 "while the same head on its own, ending on the last byte held, is accepted");
    }

    check_the_guarded_write();
    check_every_kind_of_page();
    check_the_guard_page();
    check_twelve_bytes_over_a_seam();
    check_the_range_before_the_touch();
    check_the_asking_side_is_counted();
    check_the_asking_mode();

    return ut_summary("guarded reads and writes");
}
