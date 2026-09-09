/* memory.c: the guarded reads, checked without the game.
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
 */
#include "unittest.h"

#include "common/memory.h"

#include <string.h>

/* The first page is never mapped on Windows, so this is an address no process can read. */
#define UNMAPPED ((uintptr_t)0x10)

static const unsigned char source[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22, 0x33, 0x44 };

int main(void)
{
    unsigned char out[8];
    uint8_t       byte = 0;
    uint32_t      word = 0;

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

    return ut_summary("guarded reads");
}
