/* mp_enemy_relay.h: what leaves the level on the host leaves it on the client, and the host's
 * level sees every player.
 *
 * Layer 2. Three things, all on the same site or the same table:
 *
 * ============================== 1. A removal travels with its reason =========================
 *
 * The presence bitmap says an actor is GONE and not why, and the why is the whole world
 * bookkeeping: `enemy_delete` with reason 0 puts the placement back to respawnable, with reason 1
 * marks it dead for good and fires its reveal list, and with reason 0xE keeps the body as a corpse.
 * A receiver that only lets go of what the bitmap dropped keeps a body the host removed, standing
 * where the host has a gap, with a placement that can be woken again by its own scan.
 *
 * So the host's `enemy_delete 0x00437850` is detoured, and every removal for reason 0, 1 or 0xE
 * goes out as MP_EVENT_DESPAWN on the reliable channel with the level, the placement, the
 * generation and the reason. The client performs the same call on its own replica with the same
 * reason, so the two placements end up in the same state and the same reveal list fires.
 *
 * Reasons 3 and 4 do not travel: 3 is the host releasing a player-hosted placement, which no
 * receiver creates, and 4 is the level being torn down, which every machine does for itself.
 *
 * ============================ 2. The host's level sees every player ==========================
 *
 * The activation scan wakes a placement when the LOCAL player is inside its range, and the
 * removal test throws an actor away when the LOCAL player is outside the placement's own radius.
 * With the level belonging to the host, "the player" on the host has to mean every player, or
 * the enemies around a client exist only as that client's own local copies, which the host never
 * sees and which snap to fresh spawns whenever the host comes near.
 *
 * Both questions are the engine's one range test, and on the host mp_range_gate.h measures that
 * test against the nearest player. So the host's own scan wakes what any player stands near, and
 * its removal test keeps what any player stands near. Nothing here walks the placements a second
 * time and nothing holds a removal back; the tick only stamps the messages.
 *
 * ================================== 3. On a client ==============================================
 *
 * The detour is installed on both roles and passes through on a client, so the client's own
 * removals of its own local actors behave exactly as they always did. What the client adds is the
 * receiving half: a despawn message names a placement and a generation, and if the replica for
 * that life exists here it is let go in the state the host last reported and then removed with
 * the host's reason through the engine's own function.
 *
 * The removal also carries a burst into pieces the actor made just before it, and the replica
 * bursts first, in the same call (mp_enemy_burst.h). Which removal is performed is decided as it
 * was before the burst existed; the burst only goes in front of a performed one.
 *
 * One removal a client performs with no message: mp_enemy_relay_remove_here, for an actor that
 * drives the client's own body.
 */
#ifndef MULTIPLAYER_MP_ENEMY_RELAY_H
#define MULTIPLAYER_MP_ENEMY_RELAY_H

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*mp_enemy_relay_send_fn_t)(const uint8_t *bytes, size_t count);

/* Where the far body of bank `bank` is on this machine, or false when that bank has none. */
typedef bool (*mp_enemy_relay_far_body_fn_t)(size_t bank, float out[3]);

/* Resolves the removal site and installs the detour, on both roles. False when the site is
 * missing, and then removals neither travel nor are held back. */
bool mp_enemy_relay_install(void);
bool mp_enemy_relay_installed(void);

void mp_enemy_relay_set_host(bool host);

/* Whether this module may remove anything at all. On for every session over a socket, in both
 * games, and off for the in-process loopback, which would be describing and applying over one set
 * of actors.
 *
 * It is not a game mode question, and reading it as one cost a day. This switch was written on
 * 2026-09-07 as "off unless a deathmatch turns it on", on the reasoning that a host telling a
 * client to delete four hundred and seventy seven actors is taking a campaign apart rather than
 * synchronising one. That number appears in no log in this tree, and the run it was reasoned
 * from is the campaign level of 2026-09-07 in which the four hundred and two placements were
 * taken by `mp_arena_clear_enemies`, the arena, a different mechanism, gated on the game for
 * good reasons and correctly so.
 *
 * What this path actually did in a co-operative field run is on record. The field run of
 * 2026-09-06, the
 * last run in which the two machines saw the same world: the host sent 451 removals and held
 * 29591 back because the far player was near them, and the client performed SIX, 359 having gone
 * on their own before the note arrived. The level was not taken apart.
 *
 * And it is one mechanism with `mp_enemy_sync`, not an option beside it. A client that only lets
 * go of what the presence bitmap dropped keeps a body the host has already freed, on a placement
 * its own activation scan can wake again, and never fires a reveal list the host has fired. The
 * host's other half is the range gate, without which the host does not populate the ground a
 * client is standing on. */
void mp_enemy_relay_set_enabled(bool enabled);
void mp_enemy_relay_set_send(mp_enemy_relay_send_fn_t send);
void mp_enemy_relay_set_far_body(mp_enemy_relay_far_body_fn_t far_body);

/* A client's hearing of a copy's removal the host sent, which only the copies' module may carry
 * out, because only the overlay removes a copy (common/npc_spawn_note.h). It answers the replica
 * to make a corpse of, for a removal that keeps one, and 0 for anything it gave up itself. NULL
 * outside a client's session. */
typedef uintptr_t (*mp_enemy_relay_copy_fn_t)(uint32_t k, uint8_t generation, uint8_t reason);
void mp_enemy_relay_set_copy_listener(mp_enemy_relay_copy_fn_t listener);

/* A host's hearing of every removal the engine makes there, once the engine's removal is back,
 * with the key the actor carried: what was remembered about an actor by its placement is not the
 * next life's. Not told for a level's own teardown, which ends every memory by itself, and not
 * for an actor whose key did not read. NULL for nobody. */
typedef void (*mp_enemy_relay_removed_fn_t)(uint32_t key);
void mp_enemy_relay_set_removed_listener(mp_enemy_relay_removed_fn_t listener);

/* Once per substep from inside the substep: the tick the messages sent until the next call
 * carry. */
void mp_enemy_relay_tick(uint32_t tick);

/* Takes a despawn message if it is one. On a client it performs the removal; on a host it is
 * taken and dropped, because the host is where removals come from. */
bool mp_enemy_relay_take_message(const uint8_t *note, size_t bytes);

/* On a client, removes the live actor of placement `key` through the engine's own removal, by
 * the door a removal of the host's is performed through, with no word from the host: the one
 * case is an actor that carries the handover bit and drives this client's own body, which a
 * savegame restored there and which no scene may hold a client under. The engine's removal puts
 * the player back first and leaves the body, which is the player's, alone. The actor is asked of
 * its own record first, because a remembered address the pool has taken back holds old bytes.
 * False, and nothing removed, on a host, before the install, and for an actor that is not that
 * placement's live one. Never from inside the engine's own walk of the actor list. */
bool mp_enemy_relay_remove_here(uintptr_t actor, uint32_t key);

/* The lives a removal has been sent for, so a second note for the same life is counted. A hook that
 * keeps corpses answers the engine's removal test, which runs every substep for a body out of
 * range, with a kept corpse every time it is asked; whether that reaches the wire once or once a
 * substep is what this counts. Pure, so a test drives it. */
typedef struct mp_enemy_relay_lives {
    uint16_t level[MP_WIRE_KEY_COUNT];
    uint8_t  generation[MP_WIRE_KEY_COUNT];
    uint8_t  known[(MP_WIRE_KEY_COUNT + 7u) / 8u];
} mp_enemy_relay_lives_t;

/* True when this level, placement and generation were noted before; notes them otherwise. A new
 * level or a new generation on the same placement is a new life. False with no table. */
bool mp_enemy_relay_life_sent_before(mp_enemy_relay_lives_t *lives, uint16_t level,
                                     uint16_t key, uint8_t generation);

void mp_enemy_relay_report(void);

#endif /* MULTIPLAYER_MP_ENEMY_RELAY_H */
