/* host_image.c: mapping an address back to the bytes it came from on disk.
 *
 * The reason this mapping exists is that memory stops being a source of truth as soon as another
 * module writes a branch over an engine prologue. The reason it is tested here rather than against
 * the game is that the interesting cases are the ones no shipped executable has: a section whose
 * virtual size is larger than its raw size, which is where the loader's zero fill begins and the
 * file ends, and a read that starts inside the file and runs off that edge.
 *
 * Only the mapping is pure. Opening the host's own executable and parsing its headers needs a
 * process with an executable in it, and the test executable is one, so that half is driven
 * against this very program at the end.
 */
#include "unittest.h"

#include "common/host_image.h"

#include <stddef.h>
#include <stdint.h>

/* The shape of every build of this engine, with the numbers rounded to something readable: a code
 * section whose raw size is rounded up past its virtual size, and a data section that is mapped far
 * larger than it is stored, because most of it is zero filled at load time. */
static const host_image_section_t SECTIONS[] = {
    /* virtual_address, virtual_size, raw_offset, raw_size */
    { 0x1000u, 0x0800u, 0x0400u, 0x0A00u },   /* code: stored past its virtual size */
    { 0x2000u, 0x4000u, 0x0E00u, 0x0200u },   /* data: mapped far past what is stored */
    { 0x7000u, 0x0100u, 0x1000u, 0x0000u }    /* nothing on disk at all */
};

#define SECTION_COUNT (sizeof(SECTIONS) / sizeof(SECTIONS[0]))

static size_t map(uint32_t rva, size_t size)
{
    return host_image_file_offset(SECTIONS, SECTION_COUNT, rva, size);
}

int main(void)
{
    ut_section("an address inside a section");

    ut_check(map(0x1000u, 4) == 0x0400u, "the first byte of a section maps to its raw offset");
    ut_check(map(0x1234u, 4) == 0x0634u, "an address inside it keeps its distance from the start");
    ut_check(map(0x2000u, 4) == 0x0E00u, "the second section is found as well as the first");

    ut_section("the edge where the file ends");

    /* This is the case the mapping exists to refuse. A section is mapped to its virtual size and
     * stored to its raw size, and everything in between is zero filled by the loader. Those bytes
     * exist in the process and exist nowhere in the file, so there is no original to hand back. */
    ut_check(map(0x21FCu, 4) == 0x0FFCu, "the last four stored bytes of a section still map");
    ut_check(map(0x21FDu, 4) == HOST_IMAGE_NO_FILE_OFFSET,
             "a read that runs one byte off the stored end is refused, not truncated");
    ut_check(map(0x2200u, 1) == HOST_IMAGE_NO_FILE_OFFSET,
             "an address in the loader's zero fill has no bytes in the file");
    ut_check(map(0x7000u, 1) == HOST_IMAGE_NO_FILE_OFFSET,
             "a section with no raw bytes answers for none of its addresses");

    ut_section("an address in no section");

    ut_check(map(0x0000u, 4) == HOST_IMAGE_NO_FILE_OFFSET,
             "the PE headers are below the first section and are not mapped here");
    ut_check(map(0x1900u, 4) == HOST_IMAGE_NO_FILE_OFFSET,
             "the gap between two sections belongs to neither");
    ut_check(map(0xF000u, 4) == HOST_IMAGE_NO_FILE_OFFSET, "an address past the last section too");

    ut_section("what the section headers claim");

    /* A section is mapped to its virtual size, so an address past that belongs to no section even
     * when the raw size would still cover it. Getting this the other way round would hand back
     * bytes that are in the file and are not at that address in the process. */
    ut_check(map(0x17FCu, 4) == 0x0BFCu, "the last mapped byte of the code section maps");
    ut_check(map(0x1800u, 4) == HOST_IMAGE_NO_FILE_OFFSET,
             "one byte past the virtual size is outside the section, stored or not");

    ut_section("refusals that are not addresses");

    ut_check(host_image_file_offset(NULL, 0, 0x1000u, 4) == HOST_IMAGE_NO_FILE_OFFSET,
             "no section table means no answer");
    ut_check(map(0x1000u, 0) == HOST_IMAGE_NO_FILE_OFFSET, "a read of nothing is refused");
    ut_check(host_image_file_offset(SECTIONS, 0, 0x1000u, 4) == HOST_IMAGE_NO_FILE_OFFSET,
             "an empty section table answers for nothing");

    ut_section("reading this test's own executable back off disk");

    /* The mapping above is arithmetic. This is the rest of the chain, driven against a real PE:
     * find the running image, open the file it came from, parse its headers and read code bytes
     * back. The test executable is one, so none of it needs the game. */
    {
        uint8_t   from_disk[16];
        uintptr_t text = 0;

        ut_check(host_image_resolve(), "the running image resolves");
        text = host_image_text();
        ut_check(text != 0, "and it has a code section");

        ut_check(host_image_read_original(text, from_disk, sizeof(from_disk)),
                 "sixteen bytes of that code section are readable from the file");

        /* The comparison only holds where the image loaded where it asked to. A rebased image has
         * had every absolute address in it rewritten by the loader, and the file still carries the
         * original ones, which is exactly the difference the delta describes. */
        if (host_image_relocation_delta() == 0) {
            const uint8_t *live = (const uint8_t *)text;
            size_t         index;
            int            same = 1;

            for (index = 0; index < sizeof(from_disk); ++index) {
                if (live[index] != from_disk[index]) {
                    same = 0;
                }
            }
            ut_check(same, "and they are the bytes standing at that address in memory");
        } else {
            ut_checkf(1, "this image is rebased by %+d, so the file's absolute addresses are not "
                         "expected to match memory byte for byte",
                      (int)host_image_relocation_delta());
        }

        ut_check(!host_image_read_original(host_image_end(), from_disk, sizeof(from_disk)),
                 "an address past the end of the image is refused");
        ut_check(!host_image_read_original(text, from_disk, 0),
                 "a read of nothing is refused");
    }

    return ut_summary("host image");
}
