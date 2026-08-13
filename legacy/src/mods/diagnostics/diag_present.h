#ifndef DIAG_PRESENT_H
#define DIAG_PRESENT_H

/* diag_present.h: which of the engine's two presentation paths is live, and what the present costs.
 *
 * Levels: 0 off, 1 the live path and the flip's own cost, 2 also the addresses it resolved to.
 * Returns 1 when it armed, so the caller can count observers. */
int diag_present_install(int level);

#endif /* DIAG_PRESENT_H */
