/* npc_spawn_link.h: the panel's spawner and the multiplayer, joined in the running game.
 *
 * Once a frame this reads the multiplayer's grant record, answers it through the spawner (build
 * under the key the host chose, remove by key, the ridden copy refused) and publishes the panel's
 * wish record; npc_spawn_session.c keeps the rules, this only reaches the engine. In a session in
 * which the multiplayer runs the copies, the placement mode's click, the remove rows and a load's
 * copies become wishes: every player removes their own copies, the host everybody's too, and only
 * the host restores a load's. Every refusal of one of them is a line in the log, with its reason,
 * and every epoch a line with what the session's copies came to so far.
 *
 * This reading is the only one the panel makes of the grant record. The rows' lock asks it too
 * (session_lock.c), so a group cannot be open under one reading and redirected under another.
 *
 * Outside such a session, in single player and on the loopback, the panel spawns, removes and
 * restores as it always did, and this only keeps its record published.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_SPAWN_LINK_H
#define DEV_OVERLAY_NPC_SPAWN_LINK_H

#include "npc_spawn_block.h"
#include "npc_spawn_desc.h"

#include <stdbool.h>
#include <stdint.h>

/* Once a frame, before the held copies of a load are looked at. */
void npc_spawn_link_tick(void);

/* The multiplayer runs the copies, and this machine is a client of somebody else's world. */
bool npc_spawn_link_active(void);
bool npc_spawn_link_is_client(void);

/* The grant record was read this frame, so a wish described now carries the epoch of this frame's
 * world rather than of the last one read. */
bool npc_spawn_link_fresh(void);

/* Where the flyer under `key` flies to, before its height is added: over `standing`, the local
 * player, outside a session that runs the copies and on a client, whose copies are parked; on the
 * host of such a session over the anchor the multiplayer published for the copy's owner. False
 * when there is no anchor of this epoch for that owner: the record then stays where it is. */
bool npc_spawn_link_flyer_point(uint32_t key, const float standing[3], float out[3]);

/* The host's cap, for the row that counts; 0 where it is not known, on a client. */
uint32_t npc_spawn_link_cap(void);

/* The grant record as this panel last read it, raw, for the line a field run is read by. The two
 * numbers that decide everything above are `cap` and `own_slot`: a host names a cap, a client a
 * world slot, and a record carrying neither reads here as a session that runs no copies. */
typedef struct npc_spawn_link_facts {
    bool     read;       /* a grant record has been read at least once */
    bool     fresh;      /* and one was read this frame */
    bool     active;     /* the multiplayer runs the copies */
    bool     client;     /* and this machine is a client in it */
    uint32_t cap;        /* what the host allows at once, 0 on a client */
    uint32_t own_slot;   /* this machine's world slot, 0 on the host */
    uint32_t epoch;      /* the epoch of the last record read */
} npc_spawn_link_facts_t;

void npc_spawn_link_facts(npc_spawn_link_facts_t *out);

/* Why this player's last wish was refused, short enough for a row, or NULL when the last wish was
 * not refused; outside a session, or before a wish could be made, the last refusal decided here.
 * Forgotten at the next wish or spawn and when the world changes. */
const char *npc_spawn_link_refusal(void);

/* A refusal decided on this machine, in the words of the row under the keys; said in the log once,
 * with its "refused here" prefix. NULL or an empty text says nothing. */
void npc_spawn_link_refuse_here(const char *why);

/* The row goes empty: a new spawn is under way. */
void npc_spawn_link_forget_refusal(void);

/* The remove rows in such a session: a wish each. `everybody` removes every player's copies,
 * which only the host may ask; otherwise this player's own. */
bool npc_spawn_link_remove(bool everybody);

/* The placement mode's click in such a session, which is the only way a copy is asked for: a
 * spawn wish for a copy whose place and turn the player chose, asked at once, since the mode's
 * frame is already the engine's own moment. */
bool npc_spawn_link_spawn_described(const npc_spawn_desc_t *desc);

/* Whether the copy under `key` is this player's own: always outside such a session, and in one
 * when its build and every owner change since name this machine's slot, the host's being 0. */
bool npc_spawn_link_owns(uint32_t key);

/* A load's copies on the host of such a session: a restore wish each. Answers how many. */
uint32_t npc_spawn_link_restore(const npc_spawn_saved_t *held, uint32_t count);

#endif /* DEV_OVERLAY_NPC_SPAWN_LINK_H */
