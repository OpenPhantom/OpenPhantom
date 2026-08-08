/* host_image.h: the geometry and the location of the process that hosts us.
 *
 * One responsibility: answer "where is the main executable, where is its code section, and which
 * directory did it come from". Everything that scans, range-checks or writes engine memory needs
 * those answers, and nothing else in `common` should be computing them a second time.
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

/* The directory of the host executable, with a trailing backslash. Empty string if unknown.
 * This is where the shared ini and the shared log live, because that is the game folder,
 * a feature DLL itself sits in <game>\mods\. */
const char *host_directory(void);

#endif /* COMMON_HOST_IMAGE_H */
