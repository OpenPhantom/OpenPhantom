/* mp_armed.c: whether a session's transport stands, and the note that says so. See the header. */
#include "mp_armed.h"

#include "common/session_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct armed_state {
    bool     transport_standing;
    bool     is_host;
    uint32_t holders;        /* mp_armed_holder_t bits; none while no transport stands */
    uint32_t note_said;
    uint32_t note_refused;
} armed_state_t;

static armed_state_t armed;

/* The whole note, from the cell's own values, zeroed first so that no field is published as
 * whatever the stack held. A channel that refused leaves the other mods seeing the note before
 * this one; it is counted, and the pause menu's report prints the count. */
static bool say_the_session(void)
{
    session_note_t note;

    memset(&note, 0, sizeof note);
    note.running    = armed.transport_standing;
    note.is_host    = armed.transport_standing && armed.is_host;
    note.input_held = armed.transport_standing && armed.holders != 0u;
    ++armed.note_said;
    if (session_note_publish(&note)) {
        return true;
    }
    ++armed.note_refused;
    return false;
}

void mp_armed_set_transport(bool standing, bool is_host)
{
    armed.transport_standing = standing;
    armed.is_host            = standing && is_host;
    if (!standing) {
        armed.holders = 0u;   /* nothing holds input for a session that is not there */
    }
    (void)say_the_session();
}

bool mp_armed_transport(void)
{
    return armed.transport_standing;
}

bool mp_armed_is_host(void)
{
    return armed.transport_standing && armed.is_host;
}

bool mp_armed_hold_input(mp_armed_holder_t holder, bool held)
{
    if (!held) {
        armed.holders &= ~(uint32_t)holder;
    } else if (armed.transport_standing) {
        armed.holders |= (uint32_t)holder;
    }
    return say_the_session();
}

bool mp_armed_input_held(void)
{
    return armed.transport_standing && armed.holders != 0u;
}

uint32_t mp_armed_holders(void)
{
    return armed.transport_standing ? armed.holders : 0u;
}

void mp_armed_note_counts(uint32_t *said, uint32_t *refused)
{
    if (said != NULL) {
        *said = armed.note_said;
    }
    if (refused != NULL) {
        *refused = armed.note_refused;
    }
}
