/* diag_characters.h: which characters are near the player right now, what they are called, and
 * which way they are moving vertically.
 */
#ifndef DIAG_CHARACTERS_H
#define DIAG_CHARACTERS_H

/* `watch_velocity` chooses which four bytes the write watch is armed on: the character's velocity
 * Z when nonzero, its position Z otherwise. */
int diag_characters_install(int characters_level, int radius, const char *watch_name,
                            int watch_velocity);

#endif /* DIAG_CHARACTERS_H */
