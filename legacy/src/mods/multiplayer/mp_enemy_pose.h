/* mp_enemy_pose.h: what an actor's PUPPET is doing, read and applied.
 *
 * This is the animation half of the enemy binding, moved out when the binding reached the size its
 * own note had named. It shares nothing with the other half but a body pointer: the walk is about
 * a pool of actors, and this is about the keyframe player behind one of them, with its evidence in
 * the puppet layout rather than the enemy one.
 *
 * ================================ Reading is arithmetic ======================================
 *
 * Not one engine call to read. The three things this file reads are reached by adding offsets:
 *
 *   the base track    body+0x9c -> thing+0x18 -> puppet+0x08 + slot*0x14c; the slot is body+0xec
 *                     and the overlay's body+0xf8, each -1 when empty. A track keeps two playheads
 *                     in FRAMES, the time at +0x120 and the time an advance starts from at +0x124
 *   the clip          body+0xe8, bounded against body+0x14 -> actor+0xc8, the model's clip count
 *   node rotations    body+0x9c -> thing+0x24 + node*0x0c, pitch at +0x00 and yaw at +0x04, with
 *                     the node count at thing+0x04 -> actor+0x54
 *
 * The engine's own playhead accessor does its arithmetic WITHOUT bounds checking the slot, so a
 * body with no overlay hands it a nonsense pointer. Doing it here means the check happens.
 *
 * ================================ The playhead is not written ================================
 *
 * The replica's track runs on its own clock, advanced by the engine's draw at this machine's
 * frame rate from the time at +0x124. A head written into +0x120 is overwritten from +0x124 by the
 * next advance before it is ever drawn, so writing the host's head on every record did nothing and
 * is gone. What a record moves is the clip: a new ordinal starts it, and so does a change of the
 * body field's start count, which is the host beginning the same clip again. A start whose head on
 * the host lies past the window (mp_clip_rule.h) is put there once, through the engine's own track
 * advance with a zero time step, as the engine's savegame restore puts a clip back on its frame;
 * from there the track runs by itself. The same happens for the first record of a replica, whose
 * spawn started clip 0 at frame 0 whatever the host plays.
 *
 * Two calls, then: the clip player and the track advance. The clip player is bounded here because
 * the engine's own bound lives in the pose commit and the play function does not get it for free.
 */
#ifndef MULTIPLAYER_MP_ENEMY_POSE_H
#define MULTIPLAYER_MP_ENEMY_POSE_H

#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stdint.h>

/* Resolves the clip player and the track advance. False only when the clip player is missing,
 * and a replica then holds whatever pose it was left in; without the advance a replica starts
 * every clip at its first frame. */
bool mp_enemy_pose_install(void);

/* The base track of a body as its keyframe player holds it. */
typedef struct mp_enemy_pose_track {
    int32_t  slot;
    uint32_t puppet;
    uint32_t track;         /* the address of the track */
    uint32_t keyframe;      /* the keyframe it plays */
    float    head;          /* frames: the larger of its two time words */
    float    time_prev;     /* frames: where the next advance starts from */
    float    fps;
    float    num_frames;    /* 0 when the keyframe did not read */
    uint32_t mode_flags;
    bool     loops;         /* neither end mode: the clip wraps */
} mp_enemy_pose_track_t;

/* False when the body plays no base track or it does not read. */
bool mp_enemy_pose_base_track(uint32_t body, mp_enemy_pose_track_t *out);

/* Fills the animation fields of a record from a live body: the clip, both playheads, the overlay
 * presence and the node rotations. */
void mp_enemy_pose_read(uint32_t body, mp_enemy_record_t *record);

/* Applies them to a replica: the rotations, the clip and, on a start or a first record, the entry
 * into the host's head. `previous` is the record written to this replica before, or NULL for its
 * first. */
void mp_enemy_pose_apply(uint32_t body, const mp_enemy_record_t *record,
                         const mp_enemy_record_t *previous);

/* The animation line and the entry line of the enemies' report. */
void mp_enemy_pose_report(void);

#endif /* MULTIPLAYER_MP_ENEMY_POSE_H */
