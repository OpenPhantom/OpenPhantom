/* signature_operand.c: which copy of a site's bytes to believe when reading an operand out of it.
 *
 * The rule under test decides between memory and the executable on disk. Memory is right while
 * the site still matches its own pattern; once another module has written a branch over the head,
 * the operand that used to sit there is gone from memory and only the file has it. The decision
 * could not be checked before: reaching it needs a resolved site in the host's own code section
 * with a foreign branch written across it, and a test process has no engine.
 *
 * It has code of its own, though. The site here is a function of this program, its pattern is
 * whatever bytes the compiler gave that function, and the foreign branch is written by the test
 * with the same patch call a fix would use. The function is never called after that, and the byte
 * is put back once the check is made.
 */
#include "unittest.h"

#include "common/host_image.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SITE_SIZE   8u
#define OPERAND_AT  1u
#define JMP_OPCODE  0xE9u

/* The site. It is never called, so all that matters is that it has a body of at least SITE_SIZE
 * bytes in the code section and that nothing else is folded into it, which the distinctive
 * constants see to. Pure register arithmetic on the argument, so none of its bytes are an absolute
 * address the loader would rewrite when the image is rebased: the copy on disk and the copy in
 * memory are then the same bytes whatever base the test executable landed at. */
static __declspec(noinline) uint32_t stand_in(uint32_t x)
{
    return (x * 0x9E3779B1u) ^ 0x0B1CAFE5u;
}

/* Under incremental linking a function's name resolves to a jump thunk rather than to its body,
 * and a site made of the thunk would begin with the very branch this test writes: the check that
 * memory carries the branch would pass before anything was written, and the disk path would
 * never be taken. The body is where the thunk points. */
static uintptr_t body_of(const void *function)
{
    const uint8_t *at = (const uint8_t *)function;

    if (at[0] == JMP_OPCODE) {
        int32_t rel;

        memcpy(&rel, at + 1, sizeof rel);
        return (uintptr_t)(at + 5 + rel);
    }
    return (uintptr_t)function;
}

static uint32_t read_u32(const uint8_t *at)
{
    uint32_t value;

    memcpy(&value, at, sizeof value);
    return value;
}

int main(void)
{
    uint8_t     pattern[SITE_SIZE];
    uint8_t     mask[SITE_SIZE];
    uint8_t     original[SITE_SIZE];
    uint8_t     from_disk[SITE_SIZE];
    signature_t site;
    uintptr_t   value = 0;
    uintptr_t   from_memory = 0;
    uintptr_t   site_at;

    ut_section("the site is this program's own code");
    site_at = body_of((const void *)stand_in);
    ut_check(*(const uint8_t *)site_at != JMP_OPCODE,
             "the site is the function's body and not a linker thunk in front of it");
    ut_check(host_image_resolve(), "the running image resolves");
    ut_check(site_at >= host_image_text() &&
             site_at + SITE_SIZE <= host_image_text() + host_image_text_size(),
             "and the stand in function sits inside its code section");

    /* The pattern is the function's own bytes, with the four that play the operand left as
     * wildcards, the way a real site leaves an absolute address out of its pattern. */
    memcpy(original, (const void *)site_at, SITE_SIZE);
    memcpy(pattern, original, SITE_SIZE);
    memset(mask, 0xFF, SITE_SIZE);
    memset(mask + OPERAND_AT, 0x00, sizeof(uint32_t));

    memset(&site, 0, sizeof site);
    site.name    = "stand_in";
    site.bytes   = pattern;
    site.mask    = mask;
    site.size    = SITE_SIZE;
    site.address = site_at;

    ut_section("while the site still looks like itself, memory is believed");
    ut_check(signature_read_address_operand(&site, OPERAND_AT, &from_memory),
             "the operand is read");
    ut_check(from_memory == read_u32(original + OPERAND_AT),
             "and it is the four bytes standing at that offset in memory");

    ut_check(signature_read_address_operand(&site, SITE_SIZE + 2u, &value),
             "an operand past the end of the pattern is read as well");
    ut_check(value == read_u32((const uint8_t *)site_at + SITE_SIZE + 2u),
             "from memory, since the window the pattern covers still matches");

    ut_section("what is refused");
    ut_check(!signature_read_address_operand(NULL, OPERAND_AT, &value), "no site");
    ut_check(!signature_read_address_operand(&site, OPERAND_AT, NULL), "nowhere to put the answer");
    {
        signature_t unresolved = site;

        unresolved.address = 0;
        ut_check(!signature_read_address_operand(&unresolved, OPERAND_AT, &value),
                 "a site that never resolved");
    }
    {
        signature_t elsewhere = site;

        elsewhere.address = (uintptr_t)pattern;
        ut_check(!signature_read_address_operand(&elsewhere, OPERAND_AT, &value),
                 "a site outside the code section, which no copy on disk could vouch for");
    }
    ut_check(!signature_read_address_operand(&site, 4096u, &value),
             "an operand further away than the window reaches");
    {
        uint8_t     wrong[SITE_SIZE];
        signature_t stranger = site;

        memcpy(wrong, pattern, SITE_SIZE);
        wrong[SITE_SIZE - 1u] ^= 0xFFu;
        stranger.bytes = wrong;
        ut_check(!signature_read_address_operand(&stranger, OPERAND_AT, &value),
                 "a site that matches neither memory nor the file is refused rather than guessed");
    }

    ut_section("once a foreign branch covers the head, the file is believed");
    ut_check(host_image_read_original(site_at, from_disk, SITE_SIZE),
             "the executable on disk gives up the site's original bytes");
    ut_check(memcmp(from_disk, original, SITE_SIZE) == 0,
             "and they are the bytes memory had before anything touched it");

    ut_check(patch_write_bytes(site_at, "\xE9", 1u) == PATCH_RESULT_OK,
             "a branch opcode is written over the first byte, as a detour would");
    ut_check(*(const uint8_t *)site_at == JMP_OPCODE, "and memory now carries it");

    value = 0;
    ut_check(signature_read_address_operand(&site, OPERAND_AT, &value),
             "the operand is still read");
    ut_check(value == (uintptr_t)read_u32(from_disk + OPERAND_AT) +
                      (uintptr_t)host_image_relocation_delta(),
             "and it is the file's value with the image's relocation delta added, not the four "
             "bytes of somebody's jump distance");
    ut_check(value == from_memory + (uintptr_t)host_image_relocation_delta(),
             "which for an operand the loader never rewrites is memory's value plus the delta");

    ut_check(patch_write_bytes(site_at, original, 1u) == PATCH_RESULT_OK,
             "the original byte is put back");
    ut_check(signature_read_address_operand(&site, OPERAND_AT, &value) && value == from_memory,
             "and with the head restored memory is believed again");

    return ut_summary("signature operand");
}
