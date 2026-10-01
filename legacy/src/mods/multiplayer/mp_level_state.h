/* mp_level_state.h: what a host's scripts have switched on its level, said to its clients, and
 * one writer for it on each client.
 *
 * Layer 2. The note and the decisions are mp_level_state_rule; the engine is
 * mp_level_state_bind; the count of every switch is mp_level_state_count. This is the half that
 * runs them: the host reads its level once a substep and sends a note when the rule says so, a
 * client keeps the newest note and applies it at the start of a substep, and a client of a started
 * session refuses its own scripts the same switches and the director's fog, escort bar and
 * crawling text, so that the host is the one writer. The shared fog is mp_level_state_fog, the fog
 * of a room each player sees alone is mp_fog_viewers; the note carries both, the escort's bar, and
 * the journal of the last sixteen changes that took effect.
 *
 * FAIL OPEN. A client refuses its own switches only when it can match the host's: the three hulls
 * stand, the three engine functions and the pool resolved, and the director is hulled. With any of
 * that missing it plays its own level as it did before this module, and says so in its count. The
 * escort and the text follow the same rule on their own bindings.
 *
 * ONE EXIT. Everything a session or a level leaves here is forgotten by mp_level_state_reset, and
 * the enemy table's own reset calls it, which every way out of a level or a session already takes.
 */
#ifndef MULTIPLAYER_MP_LEVEL_STATE_H
#define MULTIPLAYER_MP_LEVEL_STATE_H

#include "mp_director_rule.h"
#include "mp_level_state_count.h"
#include "mp_level_state_rule.h"
#include "mp_world_door.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*mp_level_state_send_fn_t)(const uint8_t *note, size_t bytes);

/* What the level's state needs from the session and cannot ask itself. `register_hand` takes a
 * hand for one class of the director's commands; `role` answers whether this side is a client of
 * a started session and, in `runs`, whether a session runs at all; `bank` names the far player's
 * bank that is open right now, 0 for none. */
typedef bool (*mp_level_state_register_fn_t)(mp_director_class_t cls, mp_world_door_hand_fn_t hand,
                                             const char *name);
typedef bool (*mp_level_state_role_fn_t)(bool *runs, uint8_t *generation);
typedef size_t (*mp_level_state_bank_fn_t)(void);

/* Binds the engine half. `arms_hulled` says the three switch hulls all stand; without them this
 * side cannot refuse its own switches and so refuses nothing. */
bool mp_level_state_install(bool arms_hulled);

/* Hands the director's fog, escort and crawl classes to this module and keeps the two questions.
 * Before the director's hull asks them; nothing is refused or noted without a session. */
void mp_level_state_arm(mp_level_state_register_fn_t register_hand, mp_level_state_role_fn_t role,
                        mp_level_state_bank_fn_t bank);

/* Whether a client of a started session refuses its own switches and fog: the one answer. */
bool mp_level_state_can_match(void);

/* Which level this substep runs in, told once a substep with the enemy table's identity. */
void mp_level_state_set_level(bool known, uint16_t identity);

/* A level switch arm was entered. True when the call is refused and must not reach the engine. */
bool mp_level_state_switch(mp_level_kind_t kind, uintptr_t actor, int32_t mode,
                           bool client_of_started);

/* The host, at the end of a substep. `generation` is the bank transitions it has seen. */
void mp_level_state_send(uint32_t tick, uint32_t generation, mp_level_state_send_fn_t send);

/* A note off the reliable channel. True when it was one, whatever became of it; kept, never
 * applied here, because this can run between substeps. */
bool mp_level_state_take(bool as_client, const uint8_t *note, size_t bytes);

/* A client, at the start of a substep: the newest note taken, applied, and this side's own viewer
 * of a room's fog driven. Returns the changes made. */
uint32_t mp_level_state_flush(void);

/* Forgets everything a level or a session left here. The counters stay for the report. */
void mp_level_state_reset(void);

/* Whether a client holds a note it has not applied yet. */
bool mp_level_state_holds_a_note(void);

/* The newest number of the host's journal, 0 before any. */
uint16_t mp_level_state_journal_newest(void);

void mp_level_state_report(bool host);

#endif /* MULTIPLAYER_MP_LEVEL_STATE_H */
