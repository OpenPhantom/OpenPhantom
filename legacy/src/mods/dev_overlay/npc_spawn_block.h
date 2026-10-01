/* npc_spawn_block.h: the panel's own block in a savegame, as bytes.
 *
 * The engine writes one block per module and on loading hands each block to the module whose
 * name it carries; a block under a name no module carries is stepped over by its length, so the
 * original game loads a savegame with this block in it and only the copies are missing. The
 * copies themselves are left out of the enemy block and written here instead, each as its
 * description and the few things that change while it lives: where it stands, where it looks,
 * its health. Not the record the engine wrote into, which would be a second way to build one.
 *
 * The payload carries its own length, because the handler reading it back is told the block's
 * subversion and not its length:
 *
 *   u32  the length of what follows
 *   then entries, each: u8 type, u8 version, u16 length, and that many bytes
 *
 * An entry of a type or a version this build does not know is stepped over by its length and
 * counted, so a later build can add one and this one still loads what it knows. Type 1 version 1
 * is a copy, 52 bytes:
 *
 *   u8 source, u8 behaviour, u8 flags (bit 0: an archive kind), u8 zero
 *   the file, 12 bytes, NUL padded
 *   f32 x3 the described position, f32 the described facing
 *   f32 x3 where it stands, f32 its yaw, i32 its health
 *
 * Little endian throughout, like the engine's own blocks. Pure: no engine, no file.
 *
 * Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_NPC_SPAWN_BLOCK_H
#define DEV_OVERLAY_NPC_SPAWN_BLOCK_H

#include "npc_spawn_desc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The most copies one block carries: the engine's actor pool. */
#define NPC_SPAWN_BLOCK_COPIES_MAX NPC_SPAWN_COPIES_MAX

#define NPC_SPAWN_BLOCK_LENGTH_BYTES 4u
#define NPC_SPAWN_BLOCK_ENTRY_HEAD   4u
#define NPC_SPAWN_BLOCK_COPY_BYTES   52u
#define NPC_SPAWN_BLOCK_TYPE_COPY    1u
#define NPC_SPAWN_BLOCK_COPY_VERSION 1u

/* The largest block this build writes: 7172 bytes. The largest shipped savegame is 84888
 * bytes, and the multiplayer carries a savegame to a joining player in at most 128 KiB, so a
 * savegame with a full block still travels. */
#define NPC_SPAWN_BLOCK_MAX_BYTES                                                               \
    (NPC_SPAWN_BLOCK_LENGTH_BYTES +                                                             \
     NPC_SPAWN_BLOCK_COPIES_MAX * (NPC_SPAWN_BLOCK_ENTRY_HEAD + NPC_SPAWN_BLOCK_COPY_BYTES))

/* One saved copy: how to raise it again, and how it stood. */
typedef struct npc_spawn_saved {
    npc_spawn_desc_t desc;
    float            position[3];
    float            yaw;
    int32_t          health;
} npc_spawn_saved_t;

/* What a decode found beside the copies it took. */
typedef struct npc_spawn_block_read {
    uint32_t copies;    /* taken into the caller's array */
    uint32_t unknown;   /* entries of a type or version this build does not know, stepped over */
    uint32_t invalid;   /* copies no build could raise: a name that is no file name, a number
                         * that is not finite, a level kind with no placement, a flag unknown */
    uint32_t over;      /* valid copies past the caller's array */
} npc_spawn_block_read_t;

/* Whether a copy is one this codec writes and reads: the file 1 to 12 printable characters, a
 * level kind with a placement, every number finite. */
bool npc_spawn_block_copy_is_valid(const npc_spawn_saved_t *copy);

/* The bytes a block of `count` copies takes. */
size_t npc_spawn_block_bytes(uint32_t count);

/* Lays out `count` copies. False, with nothing promised in `out`, when the room is too small,
 * `count` is past the most a block carries, or a copy is not valid. */
bool npc_spawn_block_encode(const npc_spawn_saved_t *copies, uint32_t count, uint8_t *out,
                            size_t capacity, size_t *bytes);

/* Reads a whole block, its length word included, into at most `max` copies. False when the
 * block is torn: a length word that does not match `bytes`, or an entry that runs past the end.
 * A torn block gives no copies at all, since half a block cannot be told from a whole one. */
bool npc_spawn_block_decode(const uint8_t *in, size_t bytes, npc_spawn_saved_t *out, uint32_t max,
                            npc_spawn_block_read_t *read);

#endif /* DEV_OVERLAY_NPC_SPAWN_BLOCK_H */
