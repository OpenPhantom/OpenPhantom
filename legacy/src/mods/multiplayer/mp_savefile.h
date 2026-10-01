/* mp_savefile.h: a savegame cut into slices for the bulk lane, and put back together.
 *
 * Layer 1, pure: bytes in, bytes out, no socket, no engine, no file.
 *
 * ======================================== Why it exists =======================================
 *
 * A co-op session that begins from a savegame has two machines and one file. The host restores
 * the file through the game's own load path and stands in the world the file describes: the
 * story flags, the actors that are alive and where, the doors that are open, the pickups that
 * are gone. The client has no such file, so until now it loaded the same LEVEL fresh and stood
 * in the world the level's authors wrote for the first minute of play, with the opening scene
 * running, the scene's actor adopting the player's body, and a story bank that says nothing has
 * happened yet. Everything the wire corrects afterwards is a correction against the wrong
 * starting point.
 *
 * The right starting point is the file itself. A savegame is one self-contained description of
 * the level's state that the engine already knows how to load, and the eleven snapshots the game
 * ships with load on every machine, so nothing in one is tied to the machine that wrote it. An
 * earlier objection, that a save names a level and a story bank rather than carrying them, is
 * wrong on the byte: the enemy restore reads the story flags and their checkpoint copy, 0x4E2
 * bytes each, out of the file, and every live actor's record after them. What was missing was a
 * way to carry 71 to 85 kilobytes across a transport whose largest note is a little over a
 * kilobyte. This file is that way.
 *
 * The field log that produced it read, on the client, the host restored from a savegame while the
 * lobby started the same level fresh, then the level's own opening scene ran on the client alone
 * and its scene actor, a placement carrying the 0x2000 player-host bit, which the spawn builds no
 * body for and the tick fills with the player's own actor, wore the player's model; on the host
 * no scene ran at all.
 *
 * ======================================== The two notes =======================================
 *
 *   A CHUNK (0x98, host to client): one piece of the file, with everything a receiver needs to
 *   place it without having heard any other piece: which file (its digest), how long the whole
 *   file is, which piece this is and how many there are. Every chunk but the last carries
 *   MP_SAVEFILE_CHUNK_PAYLOAD bytes; the last carries the remainder.
 *
 *   AN ACKNOWLEDGEMENT (0x9F, client to host): the whole bitmask of the chunks this side holds,
 *   said again on a timer. It is at once the request ("a mask of nothing means send me all of
 *   it"), the repair ("these are the ones I am still missing") and the only sentence that may
 *   call a transfer finished. The client speaks; the host never pushes on its own. That is what
 *   makes a late joiner, a client that missed the host's choice, and a client eight chunks short
 *   all one case with no state on the host to tell them apart.
 *
 *   A REQUEST (0x99) was the older form of that, a cursor rather than a mask: "send me FILE from
 *   chunk N". It is kept only so a note from such a build is recognised and counted instead of
 *   being read by whatever looks at it next.
 *
 * The lane underneath is not reliable and not ordered, and that is deliberate. Until 2026-09-09
 * these notes rode the reliable channel, and all three of that channel's virtues were vices here:
 * ordering meant one chunk that could not be placed stopped every message behind it; the reserved
 * payload meant a chunk of a kilobyte was never offered a seat large enough while a level was
 * running; and sixty four queue slots were filled by one file. Seventy of seventy eight chunks
 * arrived and the rest could not, for ever, in silence. So the repair lives HERE now: the sender
 * resends what the mask does not name, and a lost chunk is a named chunk.
 *
 * ====================================== The digest is the name ================================
 *
 * The file is named by the FNV-1a of its bytes rather than by its file name, because two hosts
 * may both have a "Zanzi17.sav" that are different games, and one host may overwrite its own.
 * The same digest is what the receiver checks the assembled bytes against before it calls them
 * complete: a file that does not hash to its own name is thrown away and asked for again, which
 * is the one repair this layer does make. The engine's restore chain is tolerant by design and
 * would load a torn file as a half restored world rather than refuse it, so the check has to
 * happen here, before the bytes ever reach a disk.
 *
 * SIZE NOTE: under 200 lines, no seam.
 */
#ifndef MULTIPLAYER_MP_SAVEFILE_H
#define MULTIPLAYER_MP_SAVEFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The tags: 0x91..0x93 are the lobby's, 0x95 the score, 0x96 the world state, 0x97 the taken
 * pickups; 0x94 stays free because a number once refused on the wire is easier left alone. */
#define MP_SAVEFILE_CHUNK_TAG   0x98u
#define MP_SAVEFILE_REQUEST_TAG 0x99u
#define MP_SAVEFILE_ACK_TAG     0x9Fu

/* Payload per chunk. With the fifteen byte head a chunk is 1039 bytes, which leaves room inside
 * the bulk lane's note for the session envelope in front of it. The number is not free: it is the
 * classic slice size for this problem, small enough that one loss costs one kilobyte and large
 * enough that a file of eighty kilobytes is under a hundred pieces. */
#define MP_SAVEFILE_CHUNK_PAYLOAD 1024u
#define MP_SAVEFILE_CHUNK_HEAD    15u   /* tag, file id, total, index, count, bytes */
#define MP_SAVEFILE_CHUNK_BYTES   (MP_SAVEFILE_CHUNK_HEAD + MP_SAVEFILE_CHUNK_PAYLOAD)
#define MP_SAVEFILE_REQUEST_BYTES 7u    /* tag, file id, first chunk wanted */

/* The acknowledgement: tag, file id, chunk count, then one bit per chunk, least significant bit
 * of the first byte for chunk 0. Under two dozen bytes for the largest file this layer carries. */
#define MP_SAVEFILE_MASK_BYTES_FOR(count) (((size_t)(count) + 7u) / 8u)
#define MP_SAVEFILE_ACK_HEAD  7u
#define MP_SAVEFILE_ACK_BYTES (MP_SAVEFILE_ACK_HEAD + \
                               MP_SAVEFILE_MASK_BYTES_FOR(MP_SAVEFILE_MAX_CHUNKS))

/* How large a file may be. The eleven shipped saves run 71 to 85 kilobytes and a player's own
 * are the same shape; the ceiling leaves half again for a level with more actors alive than any
 * shipped save has, and refuses anything that is not a savegame at all. */
#define MP_SAVEFILE_MAX_BYTES  (128u * 1024u)
#define MP_SAVEFILE_MAX_CHUNKS ((MP_SAVEFILE_MAX_BYTES + MP_SAVEFILE_CHUNK_PAYLOAD - 1u) / \
                                MP_SAVEFILE_CHUNK_PAYLOAD)

/* FNV-1a over the bytes, the name a file travels under. Seeded with the standard offset basis. */
#define MP_SAVEFILE_FNV_SEED 2166136261u
uint32_t mp_savefile_digest(const void *bytes, size_t size);

/* How many chunks a file of `total` bytes is cut into: 0 for an empty file and for one past the
 * ceiling, both of which are refused everywhere below. */
uint16_t mp_savefile_chunk_count(uint32_t total);

/* ---- the chunk --------------------------------------------------------------------------- */

typedef struct mp_savefile_chunk {
    uint32_t       file_id;
    uint32_t       total;     /* bytes in the whole file */
    uint16_t       index;
    uint16_t       count;
    uint16_t       bytes;     /* in this chunk */
    const uint8_t *payload;   /* into the note the chunk was decoded from */
} mp_savefile_chunk_t;

/* Cuts chunk `index` of `file` and writes the note. Zero when the index is past the file, the
 * file is empty or past the ceiling, or the buffer is short. */
size_t mp_savefile_chunk_encode(uint32_t file_id, const uint8_t *file, uint32_t total,
                                uint16_t index, uint8_t *out, size_t capacity);
bool   mp_savefile_is_chunk(const uint8_t *note, size_t bytes);
/* Refuses a note whose fields do not agree with each other: an index past the count, a count
 * that is not the one the total implies, a byte count that is not the one the index implies, or
 * a note whose length is not head plus bytes. */
bool   mp_savefile_chunk_decode(const uint8_t *note, size_t bytes, mp_savefile_chunk_t *out);

/* ---- the request, of which only the recogniser is left ------------------------------------- */

/* The cursor shaped request is gone with the shape that needed it; see the head of this file.
 * What remains is the recogniser, so that such a note from an older build is named and counted
 * rather than handed on to whichever module looks at an unclaimed note next. */
bool mp_savefile_is_request(const uint8_t *note, size_t bytes);

/* ---- the acknowledgement, which is also the request --------------------------------------- */

/* The whole state, every time. The receiver says which chunks it holds by sending the entire
 * bitmask, not a cursor and not a delta, and it says it again on a timer. That is what makes the
 * acknowledgement need no reliability of its own: any single one that arrives tells the sender
 * everything, so losing nine in a row costs nothing but time. It is the form Glenn Fiedler
 * describes for a large block over UDP and the one Valve's transport uses under the name ack
 * vector, from DCCP and QUIC.
 *
 * A mask of all zeroes is how a transfer begins: "I hold none of this file". So there is no
 * separate request note in the new path, asking and acknowledging are the same sentence, and the
 * asymmetry that let a host believe a transfer finished while eight chunks were missing cannot be
 * stated any more. */
size_t mp_savefile_ack_encode(uint32_t file_id, uint16_t count, const uint8_t *mask, uint8_t *out,
                              size_t capacity);
bool   mp_savefile_is_ack(const uint8_t *note, size_t bytes);
/* On true `*mask` points into the note and holds MP_SAVEFILE_MASK_BYTES_FOR(*count) bytes. */
bool   mp_savefile_ack_decode(const uint8_t *note, size_t bytes, uint32_t *file_id,
                              uint16_t *count, const uint8_t **mask);

/* One bit of such a mask, and how many of `count` are set. Shared so that the sender counting a
 * peer's progress and the receiver counting its own use the same reading of the same bytes. */
bool     mp_savefile_mask_has(const uint8_t *mask, uint16_t index);
void     mp_savefile_mask_set(uint8_t *mask, uint16_t index);
uint16_t mp_savefile_mask_count(const uint8_t *mask, uint16_t count);
/* The lowest index not set, or `count` when every one is. */
uint16_t mp_savefile_mask_first_missing(const uint8_t *mask, uint16_t count);

/* ---- the receiving end ------------------------------------------------------------------- */

/* One file being put back together. Large, because it holds the whole file; whoever owns one
 * keeps it static rather than on a stack. */
typedef struct mp_savefile_assembly {
    bool     open;                /* a file has been named and chunks are wanted */
    bool     complete;            /* every chunk is in and the bytes hash to the name */
    uint32_t file_id;
    uint32_t total;
    uint16_t count;
    uint16_t received;            /* distinct chunks in */
    uint8_t  have[(MP_SAVEFILE_MAX_CHUNKS + 7u) / 8u];
    uint8_t  bytes[MP_SAVEFILE_MAX_BYTES];

    uint32_t chunks_taken;        /* over the assembly's life, all files */
    uint32_t chunks_repeated;     /* already had that one */
    uint32_t chunks_refused;      /* wrong file, wrong total, or malformed */
    uint32_t files_rejected;      /* assembled and did not hash to their name */
} mp_savefile_assembly_t;

void mp_savefile_assembly_reset(mp_savefile_assembly_t *assembly);

/* Names the file that is wanted. A different id or total than the one open throws away what was
 * gathered; the same one changes nothing, so this is safe to call on every repeat of the note
 * that carries it. False for a total this layer refuses. */
bool mp_savefile_assembly_open(mp_savefile_assembly_t *assembly, uint32_t file_id, uint32_t total);

/* Places a chunk. False when it is not for the open file or does not fit it, which is counted
 * rather than acted on. On the chunk that completes the set the bytes are hashed: a match makes
 * the assembly complete, a mismatch empties it and counts a rejected file. */
bool mp_savefile_assembly_take(mp_savefile_assembly_t *assembly, const mp_savefile_chunk_t *chunk);

/* Hundredths of the file that are in, 0..100, for a line on a screen. */
uint32_t mp_savefile_assembly_percent(const mp_savefile_assembly_t *assembly);

/* ===================================== The lane's pace =======================================
 *
 * A bucket of slices that fills with wall time and empties by sending. A sender lays no more in a
 * tick than the bucket holds, so its rate is a number of slices a second whatever the rate of the
 * pump that ticks it. Kept in thousandths of a slice, so a pump that adds less than a whole slice
 * between two ticks still adds it. */
typedef struct mp_savefile_pace {
    uint32_t milli;     /* thousandths of a slice held */
    uint32_t last_ms;   /* when it was last filled */
    bool     started;   /* filled once; the first fill fills it to the brim */
} mp_savefile_pace_t;

/* Fills the bucket for the time since the last fill at `per_second` slices a second, never past
 * `burst` slices, and answers how many whole slices it holds. */
uint32_t mp_savefile_pace_fill(mp_savefile_pace_t *pace, uint32_t now_ms, uint32_t per_second,
                               uint32_t burst);

/* Takes `slices` out, never below empty. */
void mp_savefile_pace_spend(mp_savefile_pace_t *pace, uint32_t slices);

#endif /* MULTIPLAYER_MP_SAVEFILE_H */
