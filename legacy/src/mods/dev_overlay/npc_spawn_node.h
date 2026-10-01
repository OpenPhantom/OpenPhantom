/* npc_spawn_node.h: the panel's own engine module, which writes the copies into a savegame and
 * raises them again after a load.
 *
 * The engine's modules are a list of nodes, each with a procedure the engine sends messages to;
 * saving sends every node message 0xA with its own id, and loading hands each block to the node
 * whose name the block carries, with message 0xB. The panel installs one node, "OPNpcCopies", from
 * a hull on sys_startup, right after the engine has built its own 27, so it exists before anything
 * can be loaded. The original game has no such node and steps over the block by its length.
 *
 * When a loaded copy is raised. Not while the block is read: the loader is halfway through the
 * file and the world is being rebuilt around it. The block is decoded and held; the load is over
 * when message 0x18 arrives; the copies are raised at the next frame with no savegame open. Every
 * world the engine builds (message 0x19) and every level end (message 6) begins a new epoch, and a
 * block held from an earlier epoch is dropped, since a load that sends a new world before 0x18 did
 * not finish. The spawner follows the same epoch, because a world rebuilt at the same address is a
 * new world that a pointer comparison cannot see.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_SPAWN_NODE_H
#define DEV_OVERLAY_NPC_SPAWN_NODE_H

#include <stdbool.h>
#include <stdint.h>

/* Places the hull on sys_startup, from the DLL's entry, before the engine starts. Answers whether
 * the node can come to exist. */
bool npc_spawn_node_install(void);

/* Once a frame: builds the node if sys_startup came and went without it, and raises the copies a
 * finished load left held. */
void npc_spawn_node_tick(void);

/* The world this machine is in, counted by the node: it changes with every world built and every
 * level ended. */
uint32_t npc_spawn_node_epoch(void);

/* How many substeps the simulation has run since the node stood: the engine sends every node a
 * message at the end of each. Only differences mean anything, and only while
 * npc_spawn_node_standing() says the node is there to count them. */
uint32_t npc_spawn_node_substeps(void);

/* A savegame is being read right now. */
bool npc_spawn_node_loading(void);

/* The node is in the engine's list, so a save carries the copies. */
bool npc_spawn_node_standing(void);

#endif /* DEV_OVERLAY_NPC_SPAWN_NODE_H */
