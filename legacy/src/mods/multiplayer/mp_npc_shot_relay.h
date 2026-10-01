/* mp_npc_shot_relay.h: the host's NPC bolts, repeated on every client.
 *
 * On the host the shot hull tells this module about every bolt one of its NPCs fires; a bolt of a
 * kind that travels goes to the clients and its object is remembered. On a client the message
 * waits until the replay of the host reaches its tick, and then the engine fires the same bolt
 * from the same muzzle; the object of that copy is remembered as well.
 *
 * A hit by a remembered bolt belongs to the machine its victim sits at. The host does not report
 * it on a far player, whose own machine fires the copy and sees it hit or miss, and a client does
 * not report it on an actor the host owns, where the host's own bolt lands. A bolt a player's
 * sabre has turned back carries that player's class and is reported like any hit of theirs. */
#ifndef MP_NPC_SHOT_RELAY_H
#define MP_NPC_SHOT_RELAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*mp_npc_shot_relay_send_fn_t)(const uint8_t *bytes, size_t count);

/* Resolves the projectile table and the engine's shot entry and takes the shot hull's NPC
 * listener. False, with nothing taken, when either is missing. */
bool mp_npc_shot_relay_install(void);
void mp_npc_shot_relay_set_host(bool host);
void mp_npc_shot_relay_set_send(mp_npc_shot_relay_send_fn_t send);

/* The substep every bolt caught from now on carries, and the clock the memory ages by. */
void mp_npc_shot_relay_set_tick(uint32_t tick);

/* True when the message is an NPC's bolt, whether or not anything was done with it. */
bool mp_npc_shot_relay_take(const uint8_t *note, size_t bytes);

/* A client fires every held bolt the replay behind world clock `clock` has reached. */
void mp_npc_shot_relay_run_due(size_t clock);

/* Whether a contact whose sender is `object` is a remembered bolt that is still the NPC's. */
bool mp_npc_shot_relay_npc_owned(uint32_t object);

void mp_npc_shot_relay_report(void);

#endif
