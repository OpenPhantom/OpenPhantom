/* diag_install.h: the two things every observer group does identically.
 *
 * Each observer group (audio, world, flow) owns its own signature table, so the byte evidence
 * stays next to the hook that depends on it. What they must NOT each own is the scaffolding
 * around it: installing a detour with a uniform log line, and deriving an address out of a
 * resolved site with the image check that makes it safe.
 */
#ifndef DIAG_INSTALL_H
#define DIAG_INSTALL_H

#include "common/detour.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Installs `hook` on `table[index]`, or logs why it did not. `what` is the one-line description
 * that ends up in the install record. Returns true when the observer is live. */
bool diag_install_observer(signature_t *table, int index, detour_t *detour, const void *hook,
                           size_t prologue_size, const char *what);

/* Reads a 32-bit operand out of a resolved site. Returns NULL, with a log line, when the site
 * did not resolve or the value does not land inside the image. That is the "no patch without a
 * check" rule applied to reads. */
void *diag_derive_address(signature_t *table, int index, uint32_t offset, const char *what);

#endif /* DIAG_INSTALL_H */
