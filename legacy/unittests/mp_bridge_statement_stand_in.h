/* unittests/mp_bridge_statement_stand_in.h: the world the tests of mp_bridge_statement run in.
 *
 * mp_bridge_statement.c is compiled as it ships. Around it stand the bridge's state and its two
 * sessions, who put the transport up, the shot table's damage and its witness, the roster, the
 * census's builds (counted) and its DLLs outside this release (not counted), the host's
 * [multiplayer] AllowMods, the host's judge and the log (kept, so a test can read the lines back).
 * Nothing of the engine is needed, and the statement is made once per process, so each process of
 * a test sees one story.
 */
#ifndef UNITTESTS_MP_BRIDGE_STATEMENT_STAND_IN_H
#define UNITTESTS_MP_BRIDGE_STATEMENT_STAND_IN_H

#include "mp_bridge_shared.h"
#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The roster and the multiplayer's build the stand-ins answer. */
#define STAND_IN_ROSTER      0xE1D7140Du
#define STAND_IN_BUILD_STAMP 0x6ABB2222u
#define STAND_IN_BUILD_IMAGE 0x00A43000u

/* What the shot table answers: whether it resolves, whether shot_init has run, and the hash. */
void stand_in_set_damage(bool resolves, bool seen, uint32_t damage);

/* How often the statement asked the census for the builds. The census's other answers are not
 * counted: one statement asks each of them once. */
unsigned stand_in_census_calls(void);

/* The DLLs outside this release the census answers: `listed` names, `count` in all, and whether
 * this build judged. None, and judged, until a test says otherwise. */
void stand_in_set_foreign(const char *const *names, size_t listed, unsigned count, bool judged);

/* Whether a session takes the statement it is handed; it does until a test says otherwise. */
void stand_in_set_statement_taken(bool taken);

/* Who put the transport up, as mp_bridge_armed_by_menu answers: the ini's way until a test says
 * the menu's. */
void stand_in_set_armed_by_menu(bool by_menu);

/* How often a client session was told to connect. */
unsigned stand_in_connects(void);

/* The bridge's state as the module sees it through mp_bridge_shared. */
mp_bridge_state_t *stand_in_state(void);

/* The two sessions the statement is handed to. */
mp_session_t *stand_in_host(void);
mp_session_t *stand_in_client(void);

/* Whether a kept log line contains `text`, and forgetting every line kept so far. */
bool stand_in_log_has(const char *text);
void stand_in_log_clear(void);

#endif /* UNITTESTS_MP_BRIDGE_STATEMENT_STAND_IN_H */
