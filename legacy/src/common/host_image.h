/* host_image.h: the geometry and the location of the process that hosts us.
 *
 * One responsibility: answer "where is the main executable, where is its code section, and which
 * directory did it come from". Everything that scans, range-checks or writes engine memory needs
 * those answers; no other file in `common` should be computing them a second time.
 *
 * host_image_resolve() is idempotent and must be called before any other function here returns
 * anything useful. The feature entry point does that once.
 */
#ifndef COMMON_HOST_IMAGE_H
#define COMMON_HOST_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Locates the main module and its first code section. Returns false on anything that is not a
 * 32-bit PE, which is the safe answer: every caller then refuses to patch. */
bool host_image_resolve(void);

uintptr_t host_image_base(void);
uintptr_t host_image_end(void);       /* base + SizeOfImage, exclusive */
uintptr_t host_image_text(void);      /* VA of the first section carrying CNT_CODE */
size_t    host_image_text_size(void);

/* ==============================================================================================
 * The second copy of the image: the file it was loaded from.
 *
 * Memory is the truth about what the process is doing now, and it is the wrong source for the
 * question "what did this instruction say before anybody touched it". Several DLLs load into this
 * process and write branches over engine prologues, and once a prologue is a branch the operand
 * that used to sit there is gone from memory entirely. The trampoline holding the original bytes
 * belongs to whichever DLL installed first, and since the shared layer is a static library every
 * DLL has its own private copy of that state, so there is no cross module way to ask for them.
 * The executable on disk still has them.
 *
 * Reading it back is a read of somebody else's file, so it is done sparingly: at installation
 * time, for a handful of operands, never on a drawing path.
 * ============================================================================================ */

/* One PE section reduced to the four numbers that map an address to a file offset. */
typedef struct host_image_section {
    uint32_t virtual_address;
    uint32_t virtual_size;
    uint32_t raw_offset;
    uint32_t raw_size;
} host_image_section_t;

/* Returned when the range has no bytes in the file. Offset zero is a legitimate answer, so it
 * cannot double as the failure value. */
#define HOST_IMAGE_NO_FILE_OFFSET ((size_t)-1)

/* Maps a relative virtual address to a file offset over a section table. Refuses a range that
 * crosses out of the section's raw bytes, because everything past those is zero filled by the
 * loader and has no original on disk. Exposed rather than kept private so the mapping can be
 * driven by a test with section tables that no shipped executable has. */
size_t host_image_file_offset(const host_image_section_t *sections, size_t count,
                              uint32_t rva, size_t size);

/* The bytes this address had when the process started, read from the executable on disk.
 * False when the file cannot be opened or parsed, or when the range has no bytes in it. */
bool host_image_read_original(uintptr_t address, void *destination, size_t size);

/* Where the image actually loaded, minus the base it asked for. Zero in the ordinary case.
 *
 * Every shipped build of this engine asks for 0x400000, keeps its relocation table and leaves the
 * dynamic base flag clear, so Windows loads it where it asks unless mandatory ASLR is switched on
 * for the process, and then it may not. An absolute address read out of the file therefore needs
 * this added; one read out of memory has already had it applied by the loader. */
intptr_t host_image_relocation_delta(void);

/* The directory of the host executable, with a trailing backslash. Empty string if unknown.
 * This is where the shared ini and the shared log live, because that is the game folder,
 * a feature DLL itself sits in <game>\mods\. */
const char *host_directory(void);

#endif /* COMMON_HOST_IMAGE_H */
