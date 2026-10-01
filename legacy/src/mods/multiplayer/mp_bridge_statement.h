/* mp_bridge_statement.h: what this side states about its game data and its build, made once per
 * process and handed to each transport.
 *
 * Layer 3. The statement is the damage table out of damage.txt, the roster out of characters.ini
 * and the builds of the required mods (mp_mod_manifest_rule.h), and behind them the DLLs outside
 * this release that this side has loaded from its mods folder. A client's join request carries it
 * and the host judges it against its own and against its [multiplayer] AllowMods; the content note
 * 0x93 carries the first three folded into one number, the content fingerprint, so the check the
 * note makes and the one a join makes count the same things. Since the statement stands from the
 * arming, that note goes out in the lobby already.
 *
 * The damage table is damage.txt's only once the shot module's init has run, and the witness for
 * that is the shot table's actor column. From the menu it has always run, so a lobby's join states
 * everything; the ini path arms the bridge when the DLL starts, before that init, and then learns
 * the statement from the first substep, as it always did, and the client's join waits for it.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_STATEMENT_H
#define MULTIPLAYER_MP_BRIDGE_STATEMENT_H

#include "mp_mod_manifest_rule.h"

#include <stdbool.h>
#include <stddef.h>

/* Learns the statement and hands it to the session this side speaks through, once per transport.
 * At the arming (`in_substep` false) only when the shot module's init has been seen; in a substep
 * it keeps asking for a second of substeps and then goes on without the damage table. A client
 * whose join waited for it joins from the substep that learned it. */
void mp_bridge_statement_learn(bool in_substep);

/* The statement this side made, false before it has made one. */
bool mp_bridge_statement_own(mp_mod_manifest_t *out);

/* This side's statement as the one number the content note 0x93 and the LAN announce carry, for
 * anybody who has to hold a far number against it, a browser sorting hosts among them. Made on the
 * first question if nothing has asked yet, so it answers in the menu before any transport stands;
 * 0 only before the shot module's init has run, when this side has nothing it could state, and
 * while the statement is held. */
uint32_t mp_bridge_statement_fingerprint(void);

/* Holds the statement back while `hold` is true: it is not made, and a transport that asks says
 * so and learns it from its first substep. The DLL holds it while it installs, because the loader
 * may still be loading other mods then, and a statement made in that moment would carry a census
 * of half the folder for the rest of the process. */
void mp_bridge_statement_hold(bool hold);

/* What this side's statement said of its DLLs outside this release: how many it has loaded from
 * the mods folder, how many of them the list behind its mods named, and into `first_unnamed` the
 * first it could not name, empty when it named them all. False, with nothing filled, before a
 * statement is made. */
bool mp_bridge_statement_foreign(unsigned *count, unsigned *named, char *first_unnamed,
                                 size_t capacity);

#endif /* MULTIPLAYER_MP_BRIDGE_STATEMENT_H */
