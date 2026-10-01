/* mp_session_access.c: what a session is told about itself, and what it answers about a peer.
 *
 * Split from mp_session.c on 2026-09-06 at the seam its size note named: these touch no packet
 * and no state machine, they write a field before the handshake or read one after it. The
 * handshake that compares the fields stays where the packets are.
 */
#include "mp_session.h"

#include "mp_roster.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

bool mp_session_set_statement(mp_session_t *session, const uint8_t *bytes, size_t length)
{
    if (session == NULL) {
        return false;
    }
    session->statement_bytes = 0u;
    if (length == 0u) {
        return true;
    }
    if (bytes == NULL || length > MP_SESSION_STATEMENT_BYTES) {
        return false;
    }
    memcpy(session->statement, bytes, length);
    session->statement_bytes = (uint16_t)length;
    return true;
}

void mp_session_set_judge(mp_session_t *session, mp_session_judge_fn judge)
{
    if (session != NULL) {
        session->judge = judge;
    }
}

size_t mp_session_statement_bytes(const mp_session_t *session)
{
    return session != NULL ? session->statement_bytes : 0u;
}

void mp_session_set_mode(mp_session_t *session, uint8_t mode)
{
    session->mode = mode;
}

void mp_session_set_capacity(mp_session_t *session, uint8_t peers)
{
    if (session == NULL) {
        return;
    }
    session->capacity = peers > MP_SESSION_MAX_PEERS ? (uint8_t)MP_SESSION_MAX_PEERS : peers;
}

void mp_session_set_password(mp_session_t *session, const char *password)
{
    size_t i = 0;

    if (session == NULL) {
        return;
    }
    memset(session->password, 0, sizeof session->password);
    if (password == NULL) {
        return;
    }
    for (; password[i] != '\0' && i + 1u < MP_SESSION_PASSWORD_MAX; ++i) {
        session->password[i] = (password[i] >= 0x20 && password[i] <= 0x7E) ? password[i] : '?';
    }
}

void mp_session_set_name(mp_session_t *session, const char *name)
{
    if (session != NULL) {
        mp_roster_name_clean(name, session->name);
    }
}

const char *mp_session_peer_name(const mp_session_t *session, size_t index)
{
    if (session == NULL || index >= MP_SESSION_MAX_PEERS) {
        return "Player";
    }
    return session->peers[index].name[0] != '\0' ? session->peers[index].name : "Player";
}

bool mp_session_peer_of_slot(uint8_t slot, size_t *index)
{
    if (slot == 0u || (size_t)slot > MP_SESSION_MAX_PEERS || index == NULL) {
        return false;
    }
    *index = (size_t)slot - 1u;
    return mp_session_slot_of_peer(*index) == slot;
}

uint8_t mp_session_slot_of_peer(size_t index)
{
    return (uint8_t)(index + 1u);
}

uint32_t mp_session_peer_rtt_ms(const mp_session_t *session, size_t index)
{
    if (session == NULL || index >= MP_SESSION_MAX_PEERS ||
        session->peers[index].state != MP_PEER_CONNECTED) {
        return 0u;
    }
    return mp_channel_rtt_ms(&session->peers[index].channel);
}
