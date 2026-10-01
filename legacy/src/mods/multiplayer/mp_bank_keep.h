/* mp_bank_keep.h: what a far body's spawn takes from the player sitting here, and gives back.
 *
 * Layer 1, pure. It works on byte images and knows no address.
 *
 * The engine's hero spawn runs status_setActivePlayer at 0x00459AB7 and then writes the hero's
 * starting kit. The first sets the current player index, stores the six inventory and key bytes
 * of the campaign bank into the record the status pointer names, points that pointer at the
 * spawned hero's record and loads the six bytes back out of it. The second writes an ammunition
 * count and a start weapon into that record (0x00448046 to 0x004480BB); the count is what reaches
 * play: 200 for Panaka, 1 for the others. The start weapon at +0x0C has no reader.
 *
 * Inside the window a far body is spawned in, the status pointer names the bank's copy, so the
 * store lands in the copy, and the load after the window brings back what the local record held
 * at its last status_setActivePlayer: the player's own spawn, the level's begin or a savegame.
 * Every key and quest item picked up since then was gone after any far spawn, and the host,
 * whose story decides, told everybody. A spawn of the player's own
 * hero also put that player's ammunition back to the starting count.
 *
 * So the window takes a snapshot of the six bytes, all four hero records and the current player
 * index before it swaps in, and puts back every one of them that differs when it swaps out, which
 * is after the spawn's last engine writer. Nothing a script or the relays do can land in between:
 * the window is one synchronous call inside a task slot, and nothing in the spawn pumps messages.
 */
#ifndef MULTIPLAYER_MP_BANK_KEEP_H
#define MULTIPLAYER_MP_BANK_KEEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The six bytes status_setActivePlayer moves (the length at 0x00459AFC and 0x00459BC9), the four
 * records its assert allows (0x00459AC6), and the record stride of its imul (0x00459BBA). */
#define MP_BANK_KEEP_FLAG_BYTES   6u
#define MP_BANK_KEEP_RECORDS      4u
#define MP_BANK_KEEP_RECORD_BYTES 0x4Cu

typedef struct mp_bank_keep {
    uint8_t  flags[MP_BANK_KEEP_FLAG_BYTES];
    uint8_t  records[MP_BANK_KEEP_RECORDS][MP_BANK_KEEP_RECORD_BYTES];
    uint32_t current_player;
} mp_bank_keep_t;

/* Which regions go back, and how many bytes of each had moved. */
typedef struct mp_bank_keep_back {
    bool     flags;
    bool     records[MP_BANK_KEEP_RECORDS];
    bool     current_player;
    uint32_t flag_bytes;
    uint32_t record_bytes;
} mp_bank_keep_back_t;

/* Compares what the engine left (`live`) with the snapshot, notes in `back` every region that
 * differs, and copies those regions from `kept` into `live`, so `live` is afterwards the image to
 * write. True when anything had moved. */
bool mp_bank_keep_restore(const mp_bank_keep_t *kept, mp_bank_keep_t *live,
                          mp_bank_keep_back_t *back);

/* Bytes in which two images differ, over all three regions. */
uint32_t mp_bank_keep_differing(const mp_bank_keep_t *a, const mp_bank_keep_t *b);

/* The window's precondition: the status pointer names the local hero's record and the current
 * player index names the local hero. The swap out puts the pointer back from what it saved and
 * the spawn's own set_active puts the index back from the hero; if the two named different
 * heroes before the window, they would afterwards, and the window is refused. */
bool mp_bank_keep_precondition(uint32_t status_pointer, uint32_t record_base,
                               uint32_t current_player, int32_t local_hero);

/* How often a 32 bit value stands at any offset of `code`. The census that proves
 * status_setActivePlayer names the same cells the window keeps. */
size_t mp_bank_keep_operands(const uint8_t *code, size_t size, uint32_t value);

#endif /* MULTIPLAYER_MP_BANK_KEEP_H */
