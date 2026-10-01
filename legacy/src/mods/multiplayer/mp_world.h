/* mp_world.h: the map on the wire, and the instrument that says how far apart the two maps run.
 *
 * Layer 2. A door, a lift and a moving platform are all the same thing to this engine, a mover,
 * and until now none of them travelled: each machine opened only what its own player triggered,
 * so one player stepped on a plate and walked through while the other ran into a closed door.
 * Every player side trigger, the walk plate, the use button and the script slot, reaches the
 * engine through one opener and one closer, so one event closes the visible half of that.
 *
 * What this module does NOT do is reconcile. The two maps go on integrating independently, and
 * three of the eight mover types drift while they do: the lift type freezes on the pose of the
 * frame before it finished, the door and the button types start their dwell on a frame boundary,
 * and the free runners were never in phase to begin with because the two world clocks are not.
 * Whether that drift is visible is a number nobody has, so the second half of this module
 * measures it: once a second each side describes the movers that are not at rest, and the far
 * side holds that description against its own and counts the difference per mover type. The
 * digest itself applies nothing. A correction that wrote a POSE into a mover would skip the
 * keyframe edges between the old pose and the new one, and those edges are what switch collision
 * faces on and off. What is corrected is narrower and lives next door: the free runners' time
 * base is nudged by the phase controller, which leaves the mover travelling through every pose in
 * between, and the movers the host's state note describes as away from home are settled by
 * mp_world_apply in the one shape the engine's own savegame restore uses.
 *
 * The two halves are separable on purpose: the events can run without the measurement, and the
 * measurement can run without the events, which is how the drift of two entirely free running
 * maps gets its baseline.
 *
 * Nor does this couple the level lifecycle. The engine's messages 5, 7 and 0x12 reset the map and
 * the two machines run them independently, so a client that respawns resets its whole map while
 * the host's keeps running; that is a separate matter and is not addressed here.
 */
#ifndef MULTIPLAYER_MP_WORLD_H
#define MULTIPLAYER_MP_WORLD_H

#include "mp_events.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The engine's mover types, as its own integrator switches on them. Only the two this module
 * treats specially are named; the rest are numbers in a report. Type 0 free runs and its pose
 * wraps, so its two ends are neighbours rather than opposites. Type 7 is a push block, which has
 * no timeline at all: its pose is always zero and it is moved directly in world space by whoever
 * is pushing it, so a pose comparison would report agreement it has not earned. */
#define MP_WORLD_MOVER_TYPES     8u
#define MP_WORLD_TYPE_ALWAYS_ON  0u
#define MP_WORLD_TYPE_PUSH_BLOCK 7u

/* How many movers this module will look at in one world. The largest shipped level carries a
 * hundred, and the count is read out of the world rather than assumed; this is the bound past
 * which a count is treated as a pointer that is not a world rather than as a very large level. */
#define MP_WORLD_MOVER_LIMIT 1024u

/* ==============================================================================================
 * The digest: what one side says about its own moving movers, once a second.
 * ============================================================================================ */

/* Its own tag, and a length no event has. The recogniser on the reliable channel tests length and
 * first byte together, so a message of this shape can be told from every event, from the one byte
 * world slot note and from the four byte acknowledgement without a reader having to guess. */
#define MP_WORLD_DIGEST_TAG          0x86u
#define MP_WORLD_DIGEST_HEADER_BYTES 8u    /* the tag, the sender's tick, the level, the count */
#define MP_WORLD_DIGEST_ENTRY_BYTES  7u    /* id, status, pose, dwell; see mp_world_entry_put */

/* How many entries one message of this shape carries.
 *
 * Sixty four used to be the cap, on the argument that the movers permanently in motion are the
 * free runners and the level with the most of those has seventeen. That argument is about which
 * movers MOVE, and the selection is about which movers are away from the position the level was
 * authored with, which is a much larger set: a door that has been opened and has latched is still
 * away from its authored position when nothing about it is moving at all.
 *
 * The bound is therefore the level rather than the traffic. The largest shipped level carries a
 * hundred movers, six of them push blocks, which this shape does not describe, so ninety four is
 * what a description of that whole level costs. Ninety six leaves the cap a round number above it
 * and the largest message inside 60 per cent of the reliable channel's own maximum. A sender that
 * finds more stops adding and counts what it dropped rather than overflowing. */
#define MP_WORLD_DIGEST_MAX_ENTRIES 96u
#define MP_WORLD_DIGEST_MAX_BYTES \
    (MP_WORLD_DIGEST_HEADER_BYTES + MP_WORLD_DIGEST_MAX_ENTRIES * MP_WORLD_DIGEST_ENTRY_BYTES)

/* Substeps between digests. The substep rate is 32 a second, so this is one a second. */
#define MP_WORLD_DIGEST_TICKS 32u

/* One mover as it travels. The pose is a fraction of the mover's own authored travel length,
 * which is the only form both machines can compare: the length is authored, so it is the same on
 * both, and the fraction is therefore dimensionless and needs no unit on the wire.
 *
 * `active` says whether the engine's own integrator still has this mover on its list, and it is
 * not derivable from the direction: a door that has latched open and a door that has finished
 * closing are both standing still, and only this bit tells them apart. `dwell` is the seconds the
 * mover has spent in the phase it is in, which is what decides when a held door lets go. */
typedef struct mp_world_entry {
    uint16_t id;
    uint8_t  type;
    uint8_t  dir;
    uint8_t  active;
    float    pose;   /* 0 to 1, the mover's pose divided by its length */
    float    dwell;  /* seconds; quantised to milliseconds on the wire */
} mp_world_entry_t;

/* One entry into its seven bytes and back. Two messages carry this entry, the digest that measures
 * and the host's state note that corrects, so the packing is written once rather than twice. The
 * reader clamps nothing and refuses nothing; whoever asked for it decides what a value means.
 *
 * The seven bytes, and where each number comes from. The id is two bytes, the index into the
 * world's mover table, which is what the engine's own opener takes: it bounds the index against
 * world+0x620 and indexes world+0x624, and the savegame restore passes mover+0x08 into the same
 * argument, so the engine treats the id and the index as one thing. The status byte is the type
 * in bits 0 to 2, the direction in bits 3 to 5 and the list membership in bit 6. The pose is two
 * bytes of fraction; the travel length is authored 29.0 in all 528 shipped records, at every type
 * and in every level, so the resolution is 29/65535, about 0.00044 pose units, far finer than the
 * engine's own float pose is stable to over a level. The dwell is two bytes of milliseconds; the
 * field is seconds in the record and is compared against the authored hold, and two bytes reach
 * 65.5 s, past any shipped hold. */
void mp_world_entry_put(mp_wire_writer_t *writer, const mp_world_entry_t *entry);
void mp_world_entry_get(mp_wire_reader_t *reader, mp_world_entry_t *entry);

typedef struct mp_world_digest {
    uint32_t         tick;    /* the sender's substep the description was taken at */
    uint16_t         level;   /* which level this describes; see mp_world_level_identity */
    uint8_t          count;
    mp_world_entry_t entry[MP_WORLD_DIGEST_MAX_ENTRIES];
} mp_world_digest_t;

/* WHICH LEVEL a message describes, as the number of movers the world holds.
 *
 * A mover id is an index into one level's table and means nothing outside it, and the session
 * deliberately survives a level load, so a message that crosses a level change describes a map
 * that no longer exists on the receiving side. Until now nothing on the wire said which map an
 * entry belonged to.
 *
 * The mover count is a usable identity for this game rather than in general, and that is worth
 * saying plainly: all eleven shipped levels have a DIFFERENT number of movers (100, 78, 60, 57,
 * 56, 55, 51, 30, 24, 14 and 3), the table is built once at level load, and nothing at run time
 * adds or removes a mover. A push block changes its own type but not the count. So two machines in
 * the same level agree, two machines in different levels do not, and a machine still building its
 * map answers a count nobody else has.
 *
 * It costs two bytes and no new site. A mod that shipped two levels with the same mover count
 * would defeat it, which is why this is written down here rather than left to be discovered. */
void mp_world_digest_set_level(mp_world_digest_t *digest, uint16_t level);

/* Why an entry did not go in. The two are counted apart because they mean different things: a
 * refusal is one mover this build cannot describe, and a full message is a level with more moving
 * movers than the cap, which is a reason to raise the cap. */
typedef enum mp_world_add {
    MP_WORLD_ADD_OK,
    MP_WORLD_ADD_REFUSED,
    MP_WORLD_ADD_FULL
} mp_world_add_t;

void mp_world_digest_init(mp_world_digest_t *digest, uint32_t tick);

/* Adds one mover. Refuses a type or a direction outside what the engine has, an id past what the
 * wire's two bytes hold, a pose that is not a number, and a length that is not positive. That
 * last one is not defensiveness: the pose travels as a fraction of the length, so a zero length
 * is a division, and the engine's own integrator hangs on such a mover in its wrap loop. */
mp_world_add_t mp_world_digest_add(mp_world_digest_t *digest, uint32_t id, uint32_t type,
                                   uint32_t dir, uint32_t active, float pose, float length,
                                   float dwell);

size_t mp_world_digest_encode(const mp_world_digest_t *digest, uint8_t *buffer, size_t capacity);
bool   mp_world_is_digest(const uint8_t *buffer, size_t bytes);
bool   mp_world_digest_decode(const uint8_t *buffer, size_t bytes, mp_world_digest_t *out);

/* ==============================================================================================
 * The comparison.
 * ============================================================================================ */

/* Five buckets over the deviation, each a quarter of the one above it in exponent: below a
 * 256th of the travel, below a 64th, below a 16th, below a quarter, and everything else. A door
 * a 256th of its travel out of step is invisible; one a quarter out is a door somebody can see
 * is in the wrong place. */
#define MP_WORLD_BUCKETS 5u

typedef struct mp_world_type_stats {
    uint32_t compared;
    uint32_t dir_mismatch;
    uint32_t type_mismatch;
    uint32_t worst_milli;   /* the largest deviation seen, in thousandths of the travel length */
    uint32_t bucket[MP_WORLD_BUCKETS];
} mp_world_type_stats_t;

/* The direction field holds 0 to 5 and travels in three bits, so a value past this is a mover
 * record that did not read rather than a direction. Both halves need it: the encoder refuses such
 * a record, and the reader refuses to believe one. */
#define MOVER_DIR_MAX 7u

/* The two of those six that this feature writes. Both halves of every mover type use them for the
 * same two things: one is the arm that drives the pose along its travel, two is the arm it rests
 * in once it has arrived. A door closed and a lift at the bottom are both direction zero. */
#define MOVER_DIR_RUNNING 1u
#define MOVER_DIR_LATCHED 2u

typedef struct mp_world_stats {
    /* The events. */
    uint32_t caught;          /* triggers of the local player's that became messages */
    uint32_t refreshed;       /* of those, repeats that only put a held plate's dwell back */
    uint32_t unchanged;       /* triggers that changed nothing and were not sent */
    uint32_t unread;          /* triggers whose mover record did not read */
    uint32_t performed;       /* the far player's triggers run through the engine's opener */
    uint32_t perform_refused; /* an id this level has no mover for, or no opener resolved */
    uint32_t events_late;
    uint32_t events_forced;

    /* The comparison. */
    mp_world_type_stats_t type[MP_WORLD_MOVER_TYPES];
    uint32_t              missing;   /* an id the receiving side has no mover for */
    uint32_t              unreadable;/* a local mover that did not read, or had no usable length */
    uint32_t              capped;    /* entries a full digest could not carry */
    uint32_t              refused;   /* entries the encoder would not describe */
    uint32_t              torn;      /* digests that did not decode */
    uint32_t              digests_in;
    uint32_t              digests_out;
    uint32_t              digests_late;/* arrived later than a resend-free channel could deliver */
    uint32_t              digests_elsewhere;/* described a level this side is not in */

    /* The free runners' phase. */
    uint32_t              write_faults;/* a timeBase write into a record that had gone away */
} mp_world_stats_t;

/* The deviation between two poses of the same mover, both as fractions of the same authored
 * length. The free runner's pose wraps at its length, so its two ends are a hair apart and the
 * shorter way round is the honest answer; every other type is clamped between zero and its
 * length, and there a deviation of nearly one whole travel is a mover in the opposite state,
 * not one that has just wrapped. */
float mp_world_pose_delta(uint32_t type, float local_pose, float wire_pose);

/* Which bucket a deviation falls in, 0 to MP_WORLD_BUCKETS - 1. */
size_t mp_world_bucket(float delta);

/* One compared pair into the counters. */
void mp_world_note(mp_world_stats_t *stats, const mp_world_entry_t *wire,
                   const mp_world_entry_t *local);

/* ==============================================================================================
 * The engine half.
 * ============================================================================================ */

/* The world record, and the fields of it this feature reads. The mover count sits in mp_cells.h,
 * where the enemy binding reads the same field for the level's identity. The table of mover
 * pointers is inline in the world rather than behind a pointer: the engine indexes it as
 * world + 0x624 + index * 4 with one indirection, not two. The clock the movers integrate against
 * is seconds since the level began, and it is the only clock in the mover path: every timeBase the
 * engine's own opener writes is that field plus a thousandth, at eight sites, one each in the arms
 * for types 1, 3, 4 and 5 and two each in the arms for types 2 and 6.
 *
 * The world pointer and both offsets are read out of the mover ticker at 0x0040A7D1, which loads
 * the world three times in seventy bytes and has to agree with itself:
 *
 *     0040A7D1  55 8B EC 83 EC 0C        push ebp; mov ebp, esp; sub esp, 0xC
 *     0040A7D7  83 3D 60 00 8A 00 00     cmp  dword [008A0060], 0     ; operand at +0x08
 *     0040A7FD  8B 0D 60 00 8A 00        mov  ecx, [008A0060]         ; operand at +0x2E
 *     0040A806  3B 91 20 06 00 00        cmp  edx, [ecx+0x620]        ; the mover count
 *     0040A811  8B 0D 60 00 8A 00        mov  ecx, [008A0060]         ; operand at +0x42
 *     0040A817  8B 94 81 24 06 00 00     mov  edx, [ecx+eax*4+0x624]  ; the mover table
 *
 * That last instruction is one indirection, and the opener does the same at 0x00408B75 with
 * `8B 8C 90 24 06 00 00`. A reconstruction that declares the field as a pointer to a table reads
 * one level too many. */
#define WORLD_MOVER_TABLE 0x624u
#define WORLD_CLOCK       0x54u

/* The mover record. Only the fields this feature reads or writes, every one read out of the
 * integrator at 0x00409170, which is cdecl over a record pointer and a float and touches exactly
 * these plus the frame dt at 0x3C and the hold at 0x40. */
#define MOVER_ACTIVE   0x00u
#define MOVER_TYPE     0x04u
#define MOVER_SPEED    0x1Cu
#define MOVER_LENGTH   0x28u
#define MOVER_POSE     0x2Cu
#define MOVER_TIMEBASE 0x30u
#define MOVER_DIR      0x34u
#define MOVER_DWELL    0x38u

/* One mover record as this feature reads it. Seven fields out of a record the engine keeps far
 * more in; the rest is authored geometry, the sub node table and the push block's own bookkeeping,
 * and nothing here has a use for any of it. */
typedef struct mp_world_mover {
    uint32_t type;
    uint32_t dir;
    uint32_t active;
    float    pose;
    float    length;
    float    speed;
    float    dwell;
} mp_world_mover_t;

/* The world record, the mover table in it and one record out of that table. Shared with the half
 * that applies the host's description, which reads the same records through the same faults.
 *
 * `mp_world_pointer` answers 0 when there is no level open, which is what a menu looks like and is
 * not a fault. `mp_world_mover_read` refuses a record whose type or direction is outside what the
 * engine has, whose pose is not a number, or whose travel length is not positive: every use of the
 * length divides by it, and the engine's own wrap loop does not terminate on a mover without
 * one. */
uint32_t mp_world_pointer(void);
bool     mp_world_mover_count(uint32_t world, uint32_t *count);
bool     mp_world_mover_at(uint32_t world, uint32_t index, uint32_t *mover);
bool     mp_world_mover_read(uint32_t world, uint32_t index, mp_world_mover_t *out);

/* One record in the shape the wire describes it, for holding a received entry against. */
void mp_world_as_entry(uint32_t index, const mp_world_mover_t *read, mp_world_entry_t *entry);

/* Runs the engine's own opener or closer for one mover as THIS module's call rather than the
 * player's, so the hull on those two functions does not read it back out as a local trigger and
 * send it to the peer that asked for it. False when neither gate resolved. */
bool mp_world_gate(uint32_t world, uint32_t id, bool open);

/* Resolves the opener and the closer, and with `catch_events` hulls both so the local player's
 * triggers become events. Idempotent. Answers true only when everything asked for stands; a site
 * that did not resolve disables what needed it and says so. With both arguments false nothing is
 * hulled and nothing is scanned. */
bool mp_world_install(bool catch_events, bool measure);

/* The sender tick every trigger caught from now on carries, set once per substep by the bridge. */
void mp_world_set_tick(uint32_t tick);

/* How a caller puts one reliable message out. False means the channel refused it, and the map
 * then keeps what it has not sent rather than losing it. */
typedef bool (*mp_world_send_fn_t)(const uint8_t *bytes, size_t count);

/* Everything the map has to say this substep: the local player's mover moments, oldest first and
 * stopping at the first the channel refuses so the order is kept, and once a second the digest.
 * The digest is not kept when it is refused, because a digest a second late describes a map a
 * second ago. */
void mp_world_send(uint32_t tick, mp_world_send_fn_t send);

/* Whose clock a mover event is due on, one per far bank and numbered like the banks. A host's
 * players each count their own substeps, so a door one of them opened is due when this side's
 * replay of that player reaches it; a client's far players all arrive on the host's worlds, and
 * every moment reaches it restamped on the host's clock, so a client has the one clock, the
 * first. Index 0 is this side's own player and never used. */
#define MP_WORLD_CLOCKS 4u

/* Whether one reliable message off the wire is the map's, and if so, taking it, a mover event
 * onto clock `clock`. A mover event and a digest are the map's; everything else is somebody's and
 * is left alone. A message that is the map's but does not decode is still taken, so a torn one
 * cannot be read as a body's moment. */
bool mp_world_take_message(size_t clock, const uint8_t *note, size_t bytes);

/* Forget everything uncollected and everything unperformed, on every arrival of a peer. */
void mp_world_clear(void);

/* A peer arrived while others were already here: every mover is described again on its next
 * refresh, because the newcomer holds none of them. The queues are the others' and are kept. */
void mp_world_note_arrival(void);

/* A mover event off the wire, held until clock `clock` reaches the tick it carries. */
void mp_world_queue_event(size_t clock, const mp_event_t *event);

/* Where the replay behind clock `clock` stands, from the same resolve that places the puppet. */
void mp_world_note_render_tick(size_t clock, uint32_t tick);

/* The tick the replay behind clock `clock` has reached. False until one has been told. */
bool mp_world_render_tick(size_t clock, uint32_t *out);

/* Perform every held event its clock has reached, through the engine's own opener. One that is
 * not due yet holds the later ones of its own clock and nobody else's. */
void mp_world_run_due(void);

/* Whether this side corrects the phase of the map's free runners. Only a client over a real
 * link does: the host owns the map, and in the loopback both ends share one map, so a
 * controller there would push the movers against their own measurement. Set once, before any
 * substep runs; changing it forgets every debt, because a debt is a claim about a link. */
void mp_world_set_correcting(bool correcting);

/* This substep's digest, or 0 bytes when none is due, the measurement is off, or there is
 * nothing moving to describe. */
size_t mp_world_build_digest(uint32_t tick, uint8_t *buffer, size_t capacity);

/* One digest from the far side, held against this side's own movers and counted. On a side that
 * corrects it also measures each free runner's phase debt, which the substep then pays off; no
 * other mover is written. */
void mp_world_receive_digest(const uint8_t *buffer, size_t bytes);

void mp_world_report(void);

/* The counters, so a test can read them and the report can print them. */
const mp_world_stats_t *mp_world_statistics(void);

#endif /* MULTIPLAYER_MP_WORLD_H */
