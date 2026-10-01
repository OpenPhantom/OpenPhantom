/* mp_host_settings.h: the host's world settings, said by the host and held by each client for the
 * mods of its own process, for as long as it plays in the host's session.
 *
 * Layer 3. The draw distance, the fog band and the dismemberment mode belong to mods that may not
 * be called, and in a session the host's value has to be the one in force on every machine
 * without a byte of a client's engine_fixes.ini changing. So:
 *
 *   THE HOST reads its own values out of its own ini, only for the mods loaded in its process and
 *   exactly as those mods read them, and its two cheat cells, and sends them as the state note
 *   0xAB in front of every setup note (mp_bridge_lobby's send_setup). That covers the repeat once
 *   a second, every choice, every start, every world change and the ending.
 *
 *   A CLIENT takes the note off its reliable channel (the drain's receive_event), and both idle
 *   pumps publish common/host_settings for the mods of this process whenever what it says
 *   changes: running while this machine plays in a running session as a client of it (the one
 *   answer of mp_session_now, which for a client includes the connection to its host), and the
 *   host's values while a note of that host's has been taken on that connection. Each mod asks
 *   the record in the poll it already has, and files an acknowledgement this module reads for
 *   the report.
 *
 *   THE EXIT is one place, beside the movie gate's: the transport coming down files a record that
 *   says no session, and the host's word is forgotten, so a second session in the same process
 *   starts from nothing. A session that ends while the transport stands is seen by the pump at the
 *   next frame. A host never publishes the record: its own ini is what it says.
 */
#ifndef MULTIPLAYER_MP_HOST_SETTINGS_H
#define MULTIPLAYER_MP_HOST_SETTINGS_H

#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* HOST: said to every peer of `host`, from the top of send_setup. Nothing on a client. */
void mp_host_settings_send(mp_session_t *host);

/* Offered every reliable note the drain takes. True when it was a note of the host's settings,
 * taken or refused; false for any other note. `is_client` is the drain's own role: a host and the
 * in-process loopback hold nothing for anybody. */
bool mp_host_settings_take(bool is_client, const uint8_t *note, size_t bytes);

/* Both pumps. Asks mp_session_now_plays_in_a_running_session, and publishes only when what the
 * record says has changed; nothing while no transport stands. */
void mp_host_settings_pump(void);

/* The transport is down: a record that says no session, and the host's word forgotten. */
void mp_host_settings_withdraw(void);

/* The host's two cheat bits (MP_HOST_SETTINGS_CHEAT_*), for the world holds. False while no note
 * of the host this client is connected to has been taken. */
bool mp_host_settings_cheats(uint8_t *cheats);

void mp_host_settings_report(void);

#endif /* MULTIPLAYER_MP_HOST_SETTINGS_H */
