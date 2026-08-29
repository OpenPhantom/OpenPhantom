/* sound_lifetime_fix.h: a pinned voice must not keep a pointer into its caller's stack frame.
 *
 * Produces: sound_lifetime_fix.dll
 */
#ifndef SOUND_LIFETIME_FIX_H
#define SOUND_LIFETIME_FIX_H

#include <stdbool.h>
#include <stdint.h>

void sound_lifetime_fix_install(void);

/* Exposed for the unit test: true when this owner handle points into the given stack extent, which
 * is the test that decides whether a pinned voice is holding a local of a frame that will return.
 * limit is the low end and base the high end, as the thread information block records them. */
bool sound_owner_is_on_stack(uintptr_t owner, uintptr_t limit, uintptr_t base);

#endif /* SOUND_LIFETIME_FIX_H */
