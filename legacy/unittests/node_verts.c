/* node_verts.c: the x87 balance's instruction replacement, driven on bytes of our own.
 *
 * The repair turns four `fld dword [abs32]` and three `fstp st(0)` into no-ops through the patch
 * journal. What is checked here is the part that decides whether to write at all: the opcode at
 * the site has to be the one expected, a refusal writes nothing, a write is exactly the
 * instruction's length, and the journal puts everything back.
 */
#include "unittest.h"

#include "node_verts.h"

#include "common/patch.h"

#include <stdint.h>
#include <string.h>

/* A run of bytes in writable memory standing in for a stretch of .text. patch_write_bytes goes
 * through VirtualProtect and reads back, which works on ordinary memory the same way. */
static uint8_t site[16];

static const uint8_t FLD_THEN_JMP[]  = { 0xD9, 0x05, 0x28, 0x81, 0x4A, 0x00, 0xE9, 0x87, 0x00,
                                          0x00, 0x00, 0xCC };
static const uint8_t POP_THEN_ADD[]  = { 0xDD, 0xD8, 0x83, 0xC4, 0x0C, 0xCC };

static int bytes_are(const uint8_t *at, const uint8_t *expected, size_t size)
{
    return memcmp(at, expected, size) == 0;
}

static void test_fld(void)
{
    patch_journal_t journal;
    const uint8_t   six_nops[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

    ut_section("the float return");

    patch_journal_reset(&journal);
    memcpy(site, FLD_THEN_JMP, sizeof FLD_THEN_JMP);
    ut_check(node_verts_replace(&journal, (uintptr_t)site, NODE_VERTS_FLD_M32, "a test fld"),
             "an fld of a cell is accepted");
    ut_check(bytes_are(site, six_nops, 6), "and becomes six no-ops");
    ut_check(bytes_are(site + 6, FLD_THEN_JMP + 6, sizeof FLD_THEN_JMP - 6),
             "the jump after it is untouched");
    ut_check(journal.count == 2, "a six byte write is two journal entries");

    patch_journal_undo(&journal);
    ut_check(bytes_are(site, FLD_THEN_JMP, sizeof FLD_THEN_JMP),
             "undoing the journal puts the fld back byte for byte");
}

static void test_pop(void)
{
    patch_journal_t journal;
    const uint8_t   two_nops[2] = { 0x90, 0x90 };

    ut_section("the pop");

    patch_journal_reset(&journal);
    memcpy(site, POP_THEN_ADD, sizeof POP_THEN_ADD);
    ut_check(node_verts_replace(&journal, (uintptr_t)site, NODE_VERTS_FSTP_ST0, "a test pop"),
             "an fstp st(0) is accepted");
    ut_check(bytes_are(site, two_nops, 2), "and becomes two no-ops");
    ut_check(bytes_are(site + 2, POP_THEN_ADD + 2, sizeof POP_THEN_ADD - 2),
             "the add esp after it is untouched");
    ut_check(journal.count == 1, "a two byte write is one journal entry");

    patch_journal_undo(&journal);
    ut_check(bytes_are(site, POP_THEN_ADD, sizeof POP_THEN_ADD),
             "undoing the journal puts the pop back");
}

static void test_refusals(void)
{
    patch_journal_t journal;

    ut_section("what is refused");

    patch_journal_reset(&journal);
    memcpy(site, POP_THEN_ADD, sizeof POP_THEN_ADD);
    ut_check(!node_verts_replace(&journal, (uintptr_t)site, NODE_VERTS_FLD_M32, "a test"),
             "an fld expected where an fstp sits is refused");
    ut_check(bytes_are(site, POP_THEN_ADD, sizeof POP_THEN_ADD), "and nothing is written");
    ut_check(journal.count == 0, "and the journal stays empty");

    memcpy(site, FLD_THEN_JMP, sizeof FLD_THEN_JMP);
    ut_check(!node_verts_replace(&journal, (uintptr_t)site, NODE_VERTS_FSTP_ST0, "a test"),
             "an fstp expected where an fld sits is refused");
    ut_check(bytes_are(site, FLD_THEN_JMP, sizeof FLD_THEN_JMP), "and nothing is written");

    memcpy(site, FLD_THEN_JMP, sizeof FLD_THEN_JMP);
    ut_check(node_verts_replace(&journal, (uintptr_t)site, NODE_VERTS_FLD_M32, "a test"),
             "the fld is replaced once");
    ut_check(!node_verts_replace(&journal, (uintptr_t)site, NODE_VERTS_FLD_M32, "a test"),
             "and a second run finds no-ops, not the fld, and declines");
    patch_journal_undo(&journal);

    ut_check(!node_verts_replace(&journal, 0x10u, NODE_VERTS_FSTP_ST0, "a test"),
             "an address nothing can read is refused, not faulted on");
}

int main(void)
{
    test_fld();
    test_pop();
    test_refusals();
    return ut_summary("node verts");
}
