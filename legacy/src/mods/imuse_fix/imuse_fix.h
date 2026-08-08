/* imuse_fix.h: music service behaviour: pausing it when the game is not in front, and
 * noticing when it has been left paused by nobody.
 */
#ifndef IMUSE_FIX_IMUSE_FIX_H
#define IMUSE_FIX_IMUSE_FIX_H

#include <stdbool.h>

/* Called once by the loader, outside the loader lock. Idempotent. */
void imuse_fix_install(void);

#endif /* IMUSE_FIX_IMUSE_FIX_H */
