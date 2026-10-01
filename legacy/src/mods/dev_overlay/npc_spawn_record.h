/* npc_spawn_record.h: the placement record a spawned copy is raised from, settled.
 *
 * Taken out of npc_spawner.c when it passed the hard size limit: what a copy's record holds is a
 * question of bytes in a buffer and needs no engine, so it is kept where a test can reach it.
 *
 * A copy's record is 0xFC bytes, the smallest a level's own record is: the placement's 0xD8 bytes
 * of fields, the reveal list, and a route of one node, the spot the copy stands on. No script of
 * this project walks a route, and every one of the engine's route readers stays inside a route of
 * one. Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_SPAWN_RECORD_H
#define DEV_OVERLAY_NPC_SPAWN_RECORD_H

#include "npc_spawn_desc.h"

#include <stdbool.h>
#include <stdint.h>

#define NPC_SPAWN_RECORD_BYTES      0xFCu
#define NPC_SPAWN_RECORD_REVEALS    0xD8u   /* eight reveal ids, u16 each */
#define NPC_SPAWN_RECORD_ROUTE      0xE8u   /* how many route nodes follow */
#define NPC_SPAWN_RECORD_ROUTE_NODE 0xECu   /* node 0: x, y, z and a word nothing reads */

/* How far over the player's feet a flyer is raised and kept (npc_spawner.c). */
#define NPC_SPAWN_FLYER_HEIGHT 1.8f

/* Whether the record's move mode flies: 2 hovers, 4 to 6 pitch toward where they go. */
bool npc_spawn_record_flies(const uint8_t *record);

/* The copy's own numbers over its source's, in a record already holding the source placement:
 * where it stands and looks, its class, its hit points, its weapon and reload, the scan's flag, no
 * removal by distance, no reveals, a route of one node. */
void npc_spawn_record_settle(uint8_t *record, const npc_spawn_desc_t *desc, const float *position,
                             float facing, bool shoots);

#endif /* DEV_OVERLAY_NPC_SPAWN_RECORD_H */
