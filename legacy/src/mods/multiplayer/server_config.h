/* server_config.h: everything the dedicated server needs to be told, in one value.
 *
 * It is a value rather than a set of globals so that the window can edit a COPY while the server
 * runs on the one it was started with, and so that "apply" is one assignment at a moment the
 * server chooses rather than a dozen fields changing under it mid-tick.
 *
 * WHERE IT LIVES. The shared ini helper, in a section of its own, so a server folder needs no
 * file format of its own and an operator who already knows the game's ini knows this one. The
 * helper puts the file next to the running executable, which for the server is the server's own
 * folder.
 *
 * SIZE NOTE: under 120 lines, no seam.
 */
#ifndef MULTIPLAYER_SERVER_CONFIG_H
#define MULTIPLAYER_SERVER_CONFIG_H

#include "mp_lobby.h"
#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SERVER_CONFIG_SECTION "dedicated"

/* Long enough for a folder, a name and an extension, and it is a relative path the CLIENT
 * resolves: this machine never opens it. */
#define SERVER_CONFIG_LOG_PATH_MAX 260u

typedef struct server_config {
    uint16_t port;
    char     name[MP_SESSION_NAME_MAX];        /* what the server calls itself */
    char     password[MP_SESSION_PASSWORD_MAX];/* empty for an open server */
    uint8_t  slots;                            /* players, this machine not being one of them */

    /* What is played: the level, its title, and the rule set. It is a setup note in waiting, and
     * handing it to the match is the whole of starting one. */
    mp_lobby_setup_t setup;

    uint32_t log_categories;
    uint32_t log_ring;                         /* records; rounded up to a power of two */
    char     log_path[SERVER_CONFIG_LOG_PATH_MAX];   /* empty if the log only reaches the screen */
} server_config_t;

/* A server nobody has configured: an open deathmatch on the first shipped level, twenty five
 * points, fifteen minutes, teams on, friendly fire off. */
void server_config_default(server_config_t *config);

/* Reads the ini over the defaults, then clamps everything into range. A value the file does not
 * carry keeps its default rather than becoming zero. */
void server_config_load(server_config_t *config);

/* Writes every field back. False when the file could not be written, and the caller says so
 * rather than reporting a save that did not happen. */
bool server_config_save(const server_config_t *config);

/* Puts every field inside the range this build accepts. Answers whether anything had to move,
 * which is what lets the window tell an operator that their number was not taken. */
bool server_config_clamp(server_config_t *config);

/* The eleven levels the game shipped, for the window's dropdown.
 *
 * It is a convenience, not a source of truth. The server never opens a level and has no game data
 * to look in; what actually travels is whatever string is in the setup note, and the CLIENT
 * resolves it against its own installation. An operator with their own map types its path and the
 * list is not consulted. */
size_t      server_config_level_count(void);
const char *server_config_level_path(size_t index);
const char *server_config_level_title(size_t index);

#endif /* MULTIPLAYER_SERVER_CONFIG_H */
