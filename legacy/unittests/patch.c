/* patch.c: the guards on writing into a live process, checked without one.
 *
 * These write into this program's own memory rather than into the game. That is not a stub: it is
 * the same function the fixes call, doing the same VirtualProtect, memcpy, read back and restore,
 * against a range whose contents this test knows. What it cannot cover is the engine, and nothing
 * here pretends to.
 *
 * The case worth having is the read back. A write that does not land used to be reported as a
 * success, and a feature that reports itself installed and then does nothing is the failure that
 * looks most like success.
 */
#include "unittest.h"

#include "common/patch.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

/* Written into, so not const. Given a known pattern so a partial write would be visible. */
static unsigned char target[64] = {
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA
};

/* Two pages, the second given back. A range that starts on the first and ends on the second
 * begins readable and ends in memory this process reserved and does not hold, which is the
 * shape of the mistake the range rule exists for: a table one entry longer than its region. */
static unsigned char *edge_of_a_hole(void)
{
    SYSTEM_INFO    info;
    unsigned char *pages;

    GetSystemInfo(&info);
    pages = (unsigned char *)VirtualAlloc(NULL, 2u * info.dwPageSize, MEM_RESERVE | MEM_COMMIT,
                                          PAGE_READWRITE);
    if (pages == NULL) {
        return NULL;
    }
    if (!VirtualFree(pages + info.dwPageSize, info.dwPageSize, MEM_DECOMMIT)) {
        return NULL;
    }
    return pages + info.dwPageSize - 2u;
}

/* Whether the null page is free here. On Windows it always is, so the check below runs. Wine
 * keeps the bottom of the address space reserved for its own loader, and a write refused for
 * that reason is refused for a different reason, so there the check is skipped and says so
 * instead of failing on an address this loader never left free. */
static int null_page_is_free(void)
{
    MEMORY_BASIC_INFORMATION information;

    if (VirtualQuery((LPCVOID)0x10, &information, sizeof information) != sizeof information) {
        return 1;
    }
    return information.State == MEM_FREE;
}

int main(void)
{
    const unsigned char four[4] = { 0x11, 0x22, 0x33, 0x44 };
    unsigned char      *hole = edge_of_a_hole();

    ut_section("a write that should land");
    ut_check(patch_write_bytes((uintptr_t)target, four, sizeof four) == PATCH_RESULT_OK,
             "four bytes into a readable, writable range are accepted");
    ut_check(memcmp(target, four, sizeof four) == 0,
             "and are there afterwards, which is the read back this function now does");
    ut_check(target[4] == 0xAA,
             "with nothing written past the length asked for");

    ut_section("what it refuses");
    ut_check(patch_write_bytes((uintptr_t)target, NULL, sizeof four) ==
             PATCH_RESULT_INVALID_ARGUMENT, "no data is refused");
    ut_check(patch_write_bytes((uintptr_t)target, four, 0) == PATCH_RESULT_INVALID_ARGUMENT,
             "a length of zero is refused rather than treated as success");

    /* The first page is never mapped on Windows, so this is an address no process can write. */
    if (null_page_is_free()) {
        ut_check(patch_write_bytes((uintptr_t)0x10, four, sizeof four) ==
                 PATCH_RESULT_INVALID_ARGUMENT,
                 "an unmapped address is refused, and named as a bad range rather than reported "
                 "as a protection failure");
    } else {
        printf("skip  the null page is not free under this loader, so the unmapped address "
               "check is not run\n");
    }
    ut_check(memcmp(target, four, sizeof four) == 0,
             "and none of those refusals disturbed the range that was written earlier");

    ut_section("a range that starts in memory this process holds and ends outside it");
    if (hole == NULL) {
        ut_check(0, "two pages were allocated and the second given back (prerequisite)");
    } else {
        hole[0] = 0x5A;
        hole[1] = 0x5A;
        ut_check(patch_write_bytes((uintptr_t)hole, four, sizeof four) ==
                 PATCH_RESULT_INVALID_ARGUMENT,
                 "a write whose first byte is writable and whose last is not is refused as a bad "
                 "range: the protection change is asked for the whole run and refuses it whole");
        ut_check(hole[0] == 0x5A && hole[1] == 0x5A,
                 "and the readable head of the range was not written before the refusal");
        ut_check(patch_write_bytes((uintptr_t)hole, four, 2u) == PATCH_RESULT_OK &&
                 hole[0] == 0x11 && hole[1] == 0x22,
                 "while the same head on its own, ending on the last byte held, is written");
    }

    ut_section("validating before writing");
    ut_check(patch_validate_bytes((uintptr_t)target, four, sizeof four),
             "the bytes just written validate against themselves");
    ut_check(!patch_validate_bytes((uintptr_t)target, (const unsigned char *)"\xAA\xAA\xAA\xAA", 4),
             "and do not validate against what was there before, so a second install declines "
             "rather than writing again");
    if (null_page_is_free()) {
        ut_check(!patch_validate_bytes((uintptr_t)0x10, four, sizeof four),
                 "an unmapped address validates as false rather than faulting");
    }
    if (hole != NULL) {
        ut_check(!patch_validate_bytes((uintptr_t)hole, four, sizeof four),
                 "a range that runs off the end of what this process holds validates as false, "
                 "even though its first bytes are the expected ones");
    }

    ut_section("a journal puts a half applied patch back");
    {
        static unsigned char   pair[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        static patch_journal_t journal;
        const unsigned char    first[4]  = { 9, 9, 9, 9 };
        const unsigned char    second[4] = { 8, 8, 8, 8 };

        patch_journal_reset(&journal);
        ut_check(patch_journal_write_bytes(&journal, (uintptr_t)pair, first, 4) == PATCH_RESULT_OK
                     && journal.count == 1 && pair[0] == 9,
                 "the first write lands and is remembered");
        ut_check(patch_journal_repoint_operand(&journal, (uintptr_t)(pair + 4), 0x08070605u,
                                               0x08080808u) == PATCH_RESULT_OK
                     && journal.count == 2 && memcmp(pair + 4, second, 4) == 0,
                 "an operand that holds what was expected is repointed and remembered");
        ut_check(patch_journal_repoint_operand(&journal, (uintptr_t)(pair + 4), 0x08070605u, 1u)
                     == PATCH_RESULT_UNEXPECTED_BYTES && journal.count == 2,
                 "an operand that no longer holds the expected value is refused and not recorded");
        if (null_page_is_free()) {
            ut_check(patch_journal_write_bytes(&journal, (uintptr_t)0x10, first, 4)
                         != PATCH_RESULT_OK && journal.count == 2,
                     "a write that cannot land records nothing");
        }
        patch_journal_undo(&journal);
        ut_check(journal.count == 0 && pair[0] == 1 && pair[3] == 4 && pair[4] == 5 && pair[7] == 8,
                 "undoing puts every recorded byte back and empties the journal");
        ut_check(patch_journal_write_bytes(&journal, (uintptr_t)pair, first, 5)
                     == PATCH_RESULT_INVALID_ARGUMENT && pair[0] == 1,
                 "a write wider than an entry is refused before anything is touched");
    }

    return ut_summary("patch");
}
