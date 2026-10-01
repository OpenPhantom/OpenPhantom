/* movie_text.h: the one line this DLL draws itself, in the five languages the game shipped in.
 *
 * A client whose movie is over before the host's holds a black picture until the host's world
 * moves, and the picture says why. The language is the one the multiplayer draws its own texts in,
 * which the movie gate carries, so the waiting line and the session's other notices agree. Every
 * cell is printable ASCII, as the multiplayer's own are.
 */
#ifndef MOVIE_TEXT_H
#define MOVIE_TEXT_H

#include <stdint.h>

/* "Waiting for the host" in the language with this number (common/language.h's order), and the
 * English line for a number past the five. */
const char *movie_text_waiting(uint8_t language);

#endif /* MOVIE_TEXT_H */
