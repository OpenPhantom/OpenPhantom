/* movie_text.c: the waiting line, one cell per language. See the header. */
#include "movie_text.h"

#include "common/language.h"

#include <stdint.h>

/* In common/language.h's column order. The wording follows the multiplayer's own lobby line, so a
 * player reads the same words for the same wait. */
static const char *const WAITING[LANGUAGE_COUNT] = {
    "Waiting for the host",
    "Warte auf den Host",
    "Attente de l'hote",
    "Attesa dell'host",
    "Esperando al anfitrion",
};

const char *movie_text_waiting(uint8_t language)
{
    return language < (uint8_t)LANGUAGE_COUNT ? WAITING[language] : WAITING[LANGUAGE_EN];
}
