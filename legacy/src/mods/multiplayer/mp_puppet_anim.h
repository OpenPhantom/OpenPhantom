/* mp_puppet_anim.h: the puppet's two animation channels, decided without the engine and applied
 * through it.
 *
 * The far body's animation arrives as a play state per channel: an ordinal, a playhead in frames,
 * and a bit that says whether the channel is live at all. The puppet already has a track of its
 * own on each channel, advanced by the engine's draw loop at this machine's frame rate, so what
 * the module decides each substep is not "what pose", which the engine composes, but "does the
 * puppet's track still agree with the sender's": start a clip, seek a running one, stop an
 * overlay, or leave it alone. Those decisions are plain functions over plain numbers and are
 * pinned in a unit test. The engine reads and calls that carry them out follow in the second half
 * of the module.
 *
 * Two families of ordinals are never started from state. An engine starter that the puppet runs
 * for an event (the weapon setter, the push starter, and later the sabre starters) plays its own
 * clip and installs the aux action that waits for that clip's marker; a second start of the same
 * ordinal from the state path would reset the marker the aux is waiting for and leave the puppet
 * refusing every later weapon change and push. Those ordinals are only recorded as applied.
 */
#ifndef MULTIPLAYER_MP_PUPPET_ANIM_H
#define MULTIPLAYER_MP_PUPPET_ANIM_H

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The wire's channel mask, as the sender sets it: the base track holds a clip, the overlay slot
 * holds a track that still plays the overlay ordinal, and the last base change was a crossfade
 * rather than a cut. Mirrored here so the decisions can be tested without the wire module.
 *
 * The overlay bit is the engine's own retire test, the one the overlay player makes before it
 * fades an old overlay out: the slot is not -1, the track's flag word is not zero, and the
 * track's keyframe equals the keyframe of the descriptor the clip table names for the ordinal.
 * It is needed because three places in the player end an overlay without touching the ordinal
 * (the held midair pose on landing and on timeout, and the talk gesture), and because a one shot
 * track retires itself and leaves the ordinal and the slot index behind. The fade bit rests on
 * the object's fading word at +0xF0, written by the base player's mode 4 arm and the clip stop's
 * mode 1 arm, initialised to -1 and read by nothing else in the image; the sender reads it after
 * its tick and writes -1 back, so a set bit means one crossfade since the last sample. */
#define MP_PUPPET_ANIM_BASE_LIVE    0x1u
#define MP_PUPPET_ANIM_OVERLAY_LIVE 0x2u
#define MP_PUPPET_ANIM_BASE_FADED   0x4u

/* A playhead on the wire is frames times sixteen. */
#define MP_PUPPET_ANIM_HEAD_SCALE 16.0f

/* The two modes of the engine's clip players: a cut resets the old track and plays the new one at
 * full weight from frame zero; a fade lets the old track fade out and the new one fade in over the
 * clip's authored time (the descriptor's word at +0x08, a tenth of a second by default). The
 * player has exactly one mode test, 4 against everything else. A census of every call in the
 * image found 41 sites passing 4 and 7 passing 2, and the seven cuts are the spawn clip, the hang
 * move restart on each marker, the sidle end, the equip carry and the suspend clip. The puppet
 * used to start every base clip with 2, which was the pop at every walk, run, jump and landing
 * transition the field report described. */
#define MP_PUPPET_ANIM_MODE_CUT  2
#define MP_PUPPET_ANIM_MODE_FADE 4

/* A puppet has four tracks. The flag word of one: fading out, holding at its end, keeping its slot
 * after fading. A track with a zero flag word is free. The three bits are the ones the engine's
 * own track search tests: it takes the first slot whose flag word is zero, failing that the first
 * that is fading out (bit 3) and neither holding at its end (bit 6) nor keeping its slot (bit 8),
 * which it resets and takes, and failing that answers -1, which the overlay player does not check.
 * A guard that counted every non zero flag word as busy would refuse every overlay for a tenth of
 * a second after every base crossfade, because the old base track is fading out for that long. */
#define MP_PUPPET_ANIM_TRACKS         4u
#define MP_PUPPET_ANIM_TRACK_FADEOUT  0x008u
#define MP_PUPPET_ANIM_TRACK_ENDHOLD  0x040u
#define MP_PUPPET_ANIM_TRACK_KEEPSLOT 0x100u

/* The clip's own mode word, copied onto its track: hold at the last frame, or hold and mark the
 * track held. A clip with neither wraps, either to its loop start or freely: the loop mode is
 * bit 2, and the free wrap of a clip with no end mode is the else branch of the advance and has
 * no bit of its own, which is why the predicate tests the two end bits and not a loop bit. */
#define MP_PUPPET_ANIM_MODE_HOLDEND    0x01u
#define MP_PUPPET_ANIM_MODE_RELEASEEND 0x02u

/* The swing table: 28 rows of 0x20 bytes in the cell the swing starter's operand names, and the
 * first word of each is the clip the swing plays. Those clips come with a swing event and are
 * never started from state. */
#define MP_PUPPET_ANIM_SWING_ROWS 28u

typedef struct mp_puppet_anim_exclusions {
    uint16_t swing_clip[MP_PUPPET_ANIM_SWING_ROWS];
    size_t   swing_count;   /* 0 until the table was read; then the state path may start a swing */
} mp_puppet_anim_exclusions_t;

/* A base ordinal only a swing event starts, an overlay ordinal only an engine starter or an event
 * starts, and an overlay the state path may seed after starting (the two aux-owned overlays are
 * not, because a seed past their marker would leave the aux waiting for ever). */
bool mp_puppet_anim_base_event_owned(const mp_puppet_anim_exclusions_t *exclusions, uint32_t clip);
bool mp_puppet_anim_overlay_event_owned(const mp_puppet_anim_exclusions_t *exclusions,
                                        uint32_t clip);
bool mp_puppet_anim_overlay_seeds(uint32_t clip);

/* A base clip loops when its mode word holds neither end mode; wrapping freely is not a bit. */
bool mp_puppet_anim_loops(uint32_t mode_flags);

/* Whether the engine's overlay player would find a track: one free, or one fading out that is
 * neither holding at its end nor keeping its slot, which the player resets and takes. */
bool mp_puppet_anim_track_free(const uint32_t flags[MP_PUPPET_ANIM_TRACKS]);

/* The playhead a fresh track is seeded to: the wire's head, held half a frame short of the clip's
 * end so the track never starts at or past its end. Zero means no seed. */
float mp_puppet_anim_seed_frames(float head, float num_frames);

typedef enum mp_puppet_anim_action {
    MP_PUPPET_ANIM_NONE,     /* the puppet's track agrees, or has nothing to agree with yet */
    MP_PUPPET_ANIM_RECORD,   /* note the ordinal as applied and touch nothing */
    MP_PUPPET_ANIM_START,    /* play the ordinal in `mode`, then seed `frames` if not zero */
    MP_PUPPET_ANIM_SEEK,     /* write `frames` into the running track */
    MP_PUPPET_ANIM_STOP      /* end the puppet's overlay */
} mp_puppet_anim_action_t;

typedef struct mp_puppet_anim_decision {
    mp_puppet_anim_action_t action;
    int32_t                 mode;
    float                   frames;
    float                   distance;   /* SEEK only: how far the head had drifted, signed, in
                                         * frames */
    bool                    withheld;   /* RECORD where a start was due, held back for a running
                                         * aux */
} mp_puppet_anim_decision_t;

/* How far a seek had to move, in buckets of frames. The count alone cannot tell a slow drift that
 * always creeps just past the tolerance from a resynchronisation after lost samples, and those two
 * want opposite fixes: the first is the tolerance or the advance being wrong, the second cures
 * itself once the samples stop being lost. One is a bug here, the other is a bug elsewhere.
 *
 * The edges are in frames and they straddle the tolerance of one and a half: a bucket that holds
 * almost everything just above it is the drift, and a spread into the wide buckets is the
 * resynchronisation. */
#define MP_PUPPET_ANIM_SEEK_BUCKETS 5u
size_t mp_puppet_anim_seek_bucket(float distance);

/* What the base decision sees: the wire's channel, what was last applied, the puppet's own track
 * and the clip's shape. A head below zero means there is none. The fade bit is trusted only when
 * the sample is the one right after the last applied: the sender reports a crossfade once, in the
 * sample of the substep it happened in, so after a lost sample a clear bit may be the reset after
 * a crossfade that was never seen, and a change then fades rather than cuts. */
typedef struct mp_puppet_anim_base_in {
    uint32_t wire_clip;
    float    wire_head;       /* frames */
    bool     wire_live;
    bool     faded;
    bool     contiguous;      /* the sample's tick is the last applied sample's tick plus one */
    int32_t  applied_clip;    /* -1 before the first */
    float    applied_head;    /* the last wire head seen on this channel */
    float    puppet_head;     /* the puppet's own track time on the applied clip */
    bool     loops;           /* the puppet's running clip wraps */
    float    num_frames;      /* of the wire clip; 0 when unknown */
    bool     event_owned;
} mp_puppet_anim_base_in_t;

void mp_puppet_anim_base_decide(const mp_puppet_anim_base_in_t *in,
                                mp_puppet_anim_decision_t *out);

/* What the overlay decision sees. Two rules stand on the puppet's own slot rather than on the
 * wire. While the record's aux action is set it waits for the marker of the overlay the puppet
 * plays, so nothing is started over that overlay; a start would move the slot to the new track
 * and leave the aux waiting on a clip that may carry no marker, for the rest of the level. And
 * the state path stops only what it started itself: an overlay a starter or an event began (the
 * draw and holster clips, a push, a block, a parry, the midair swing) retires itself as a one
 * shot, and a stop from here would cut the far player's draw clip short whenever the wire's
 * overlay and the puppet's own run out of step, which they do around every weapon change. */
typedef struct mp_puppet_anim_overlay_in {
    uint32_t wire_clip;
    float    wire_head;
    bool     wire_live;
    int32_t  applied_clip;
    float    applied_head;
    int32_t  puppet_clip;        /* the overlay the puppet's slot still plays; -1 for none */
    float    puppet_head;
    bool     aux_running;        /* the record's aux action is set; its overlay is its own */
    bool     puppet_event_owned; /* `puppet_clip` belongs to a starter or an event */
    float    num_frames;
    bool     event_owned;        /* `wire_clip` belongs to a starter or an event */
} mp_puppet_anim_overlay_in_t;

void mp_puppet_anim_overlay_decide(const mp_puppet_anim_overlay_in_t *in,
                                   mp_puppet_anim_decision_t *out);

/* When an event stamped with the sender's tick is performed against the puppet's render tick. The
 * differences are read signed, so a wrapped counter and a reordered arrival both come out right.
 * Late is due as well, only counted; forced is an event so far ahead that waiting for its tick
 * would hold it for a second or more, which no sender produces on purpose. Without a render tick
 * every event is due at arrival. */
typedef enum mp_puppet_anim_due {
    MP_PUPPET_ANIM_WAIT,
    MP_PUPPET_ANIM_DUE,
    MP_PUPPET_ANIM_LATE,
    MP_PUPPET_ANIM_FORCED
} mp_puppet_anim_due_t;

mp_puppet_anim_due_t mp_puppet_anim_event_due(uint32_t event_tick, uint32_t render_tick,
                                              bool render_known);

/* Degrees into 0..360. */
float mp_puppet_anim_wrap360(float degrees);

/* A point given relative to a body, placed in the world by the body's position and its pitch, yaw
 * and roll in degrees, with the rotation built the way the engine builds an object's pose matrix:
 * forward is (-sin yaw, cos yaw, 0) for an upright body and a positive pitch lifts. The nine
 * products are the ones the engine's Euler to matrix builder forms from the rotation at +0x3C
 * and applies as columns, and the forward pair is the same one the player's knockback and facing
 * vectors use, so the sign convention is confirmed twice, once through the matrix and once
 * through code that never touches it. The pitch and roll arms on a tilted body are taken from
 * the matrix reading and not from a run. */
void mp_puppet_anim_muzzle(const float position[3], const float rotation[3], const float local[3],
                           float out[3]);

/* ==============================================================================================
 * The engine half: what the decisions are carried out with.
 * ============================================================================================ */

typedef struct mp_puppet_anim_counters {
    uint32_t base_starts_cut;
    uint32_t base_starts_fade;
    uint32_t seeds;
    uint32_t seeks;
    uint32_t seeks_across_marker;     /* a forward seek whose span held the clip's marker frame */
    uint32_t seeks_back;              /* of those, the ones that went backwards, which cost most */
    uint32_t seek_bucket[MP_PUPPET_ANIM_SEEK_BUCKETS];   /* how far each one had to move */
    uint32_t overlay_starts;
    uint32_t overlay_stops;
    uint32_t overlay_starts_withheld; /* a start held back because the aux owns the slot */
    uint32_t guard_refusals;          /* an overlay the track guard would not let start */
    uint32_t refused_ordinals;        /* a clip past the actor's clip count */
    uint32_t write_faults;            /* a track write the patch layer refused */

    /* The clip events of the moves (mp_puppet_anim_seek.c). */
    uint32_t seeds_in_window;         /* a start left at frame 0, inside the window */
    uint32_t seed_events_left;        /* events a seed past the window moved past */
    uint32_t events_fired;            /* events the dispatcher fired over a forward seek */
    uint32_t jumps_fired;             /* forward seeks the dispatcher went over */
    uint32_t no_dispatch;             /* forward seeks with no dispatcher to fire their events */
    uint32_t holds;                   /* a track held instead of going back */
    uint32_t events_not_twice;        /* events going back would have played a second time */
    uint32_t released;                /* holds released when the sender caught up */
    uint32_t held_in_place;           /* forward seeks of a track the engine itself held */
} mp_puppet_anim_counters_t;

/* Resolve the four clip functions. A function that did not resolve disables what needed it and
 * says so. The swing table's clip column is read by the reset below. */
void mp_puppet_anim_resolve(void);

/* Forget what far bank `bank`'s puppet last applied on both channels, and read the swing table's
 * clip column again: the dev overlay's character profile rewrites that column in place, so the
 * exclusion has to be as fresh as the peer. A table that does not read leaves the exclusion empty
 * and says so once. */
void mp_puppet_anim_reset(size_t bank);

/* Whether the engine's overlay player would find a track on this object: the same search the
 * state path makes before starting an overlay of its own, for the callers that reach an engine
 * starter which plays one. The player has no guard of its own and writes into the track before
 * the array when all four are busy, and a puppet carries more overlapping tracks than the sender
 * does, because the state path's crossfades and the event clips share them. Refusals are not
 * counted here; each caller counts and holds its own. */
bool mp_puppet_anim_overlay_track_free(uint32_t object);

/* Whether the actor carries a clip of this ordinal. The wire is not trusted with an index into
 * the clip table: the overlay player has no bound of its own. Refusals are counted, the first is
 * logged. */
bool mp_puppet_anim_clip_exists(uint32_t object, uint32_t clip);

/* The swing table's clip column into the exclusion set, out of the cell the swing starter's
 * operand names. A table that does not read empties the set and says so once; a table whose
 * clips changed since the last read says so too, because that is the dev overlay's character
 * profile at work. In mp_puppet_anim_engine.c. */
void mp_puppet_anim_read_swing_table(mp_puppet_anim_exclusions_t *exclusions);

/* One substep of the base channel and one of the overlay channel, inside bank `bank`'s window
 * over that puppet's object, against what that far body last applied. `tick` is the sender tick
 * the sample was taken at, which is how the base channel tells a sample that follows the last one
 * from a sample after a gap. */
void mp_puppet_anim_apply_base(size_t bank, uint32_t object, const mp_wire_anim_t *wire,
                               uint32_t tick);
void mp_puppet_anim_apply_overlay(size_t bank, uint32_t record, uint32_t object,
                                  const mp_wire_anim_t *wire);

void mp_puppet_anim_counters(mp_puppet_anim_counters_t *out);

/* The track a channel's slot names on this object: the puppet, the slot and the track's address.
 * False for a slot of -1, or one past the four tracks, or a puppet that does not read. */
bool mp_puppet_anim_track_of(uint32_t object, uint32_t slot_offset, uint32_t *puppet,
                             uint32_t *slot, uint32_t *track);

/* One channel of the puppet as the state path read it this substep: whether its slot still plays
 * the clip, the track's head, its mode word and length, and where the track lives. */
typedef struct mp_puppet_anim_channel {
    bool     live;
    float    head;         /* frames: the larger of the track's two time words */
    uint32_t mode_flags;
    float    num_frames;
    uint32_t puppet;
    uint32_t slot;
    uint32_t track;
} mp_puppet_anim_channel_t;

/* ==============================================================================================
 * Moving a track, in mp_puppet_anim_seek.c.
 *
 * A puppet's track follows the sender's head, and the engine hands a clip's own events out of
 * the span its draw advanced the track, once. A move this feature makes is not an advance of the
 * draw, so each kind of move takes care of the events itself:
 *
 *   a seed     a fresh track put at the sender's head when that lies past the window
 *              (mp_clip_rule.h); inside it the track runs from its first frame and the draw fires
 *              every event on the way, the first frame's included. Past it the events are old.
 *   forward    the engine's own advance with a zero time step, then the dispatcher over the same
 *              span, so the events it passed fire once, as the draw would have fired them.
 *   back       the track is held where it is, with the engine's own hold bit, until the sender's
 *              head has caught up; nothing is set back, and nothing the track already played
 *              plays twice.
 * ============================================================================================ */

/* Resolves the track advance and the dispatcher. False when the advance is missing, and then a
 * forward move writes both time words and fires nothing. */
bool mp_puppet_anim_seek_resolve(void);

/* A track the channel at `slot_offset` has just started, put at `frames` when that is past the
 * window. */
void mp_puppet_anim_seed(uint32_t object, uint32_t slot_offset, float frames);

/* Before bank `bank`'s base channel acts this substep: a hold is kept while `decision` still
 * wants that same track back, and released for anything else. */
void mp_puppet_anim_settle_hold(size_t bank, uint32_t object,
                                const mp_puppet_anim_channel_t *channel,
                                const mp_puppet_anim_decision_t *decision);

/* A running track moved to the decision's frames: forward with its events, back by a hold. */
void mp_puppet_anim_seek(size_t bank, uint32_t object, const mp_puppet_anim_channel_t *channel,
                         const mp_puppet_anim_decision_t *decision);

/* The seek's share of the counters above: the seeds, the seeks and their buckets, the write
 * faults and the clip events of the moves. */
void mp_puppet_anim_seek_counters(mp_puppet_anim_counters_t *out);

/* One line: the events the moves fired, held back and left behind. */
void mp_puppet_anim_seek_report(void);

#endif /* MULTIPLAYER_MP_PUPPET_ANIM_H */
