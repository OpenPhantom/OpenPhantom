/* mp_enemy_interest_rule.h: which enemies a host describes to one peer in one substep, and in
 * what order.
 *
 * Layer 1, pure. The block that carries the enemies has a fixed room and a peer who stands in one
 * corner of a level shares it with enemies that stand in another. The presence bitmap still names
 * every live key in every block, because it is what makes a receiver let go of what the host no
 * longer has; only the RECORDS are chosen here, and each record is still a delta against what that
 * peer is believed to hold.
 *
 * The distances are the engine's own. Every placement carries a wake radius, which the activation
 * scan measures from the placement's authored position, and a keep radius, which the entity loop
 * measures from the actor. They answer the question the engine asks of its one player: would it
 * have woken this enemy here, would it have kept it. Asked of the peer's player they give three
 * classes, and the arithmetic is the range gate's, so the two cannot disagree at a radius:
 *
 *   near    the peer's own engine would wake it here;
 *   middle  not near, but its engine would keep it;
 *   far     its engine would have taken it away long ago, and the host keeps it for somebody else.
 *
 * A radius of zero is no distance test at all, so it is never the farther class. A class once
 * reached is kept until the distance is a tenth past the radius, so a body on the line does not
 * change its class every substep. With no position for the peer every key is middle, which with
 * equal priorities is the round robin this block had before.
 *
 * Some records may not wait, because waiting loses something rather than delaying it. They go
 * first, in every substep they are due:
 *
 *   a change of the state a watcher sees, against what this peer holds: a death, a throw, a
 *   shield, whether the body is drawn and whether it collides, a node hidden or shown;
 *   a new life of a key this peer had described in an earlier life, since the peer still holds
 *   that one.
 *
 * Then the ones that may overflow but not wait behind the rest: the first description of a key in
 * the near class, a key a world event waits on that this peer does not hold yet, so the replica
 * arrives with its event and not behind it, and a key that has waited past its class limit (2, 8
 * and 32 substeps). The events themselves do not ride the records: they travel in a part of their
 * own in front of them, and as a reason that may not wait they would pull a whole record into the
 * floor for every event on a key the peer has never been told of. The rest
 * is ranked by a priority accumulator in the manner of Fiedler's state synchronization: every
 * substep each key adds its priority, the ones written start again at nought, the ones left over
 * keep what they had. The priority is 8, 3 or 1 by class, doubled when the enemy is going for this
 * peer's player or hit that player within the last second, and doubled when a bolt of its own is in
 * flight, at most four times. Going for the player and having hit it are one reason, not two: an
 * enemy that does both is doubled once for it.
 *
 * A far key goes no more often than every eighth substep unless something above requires it, and a
 * far key this peer was never told about is not described at all until it comes nearer: the peer
 * would build a replica of an enemy its own engine would never have had.
 *
 * A peer whose acknowledgements have been silent for half a second is sent the records that may
 * not wait and at most four more, the oldest first. Quake 3 sends a client that is not active one
 * snapshot a second for the same reason: what goes to a side that does not answer is paid for
 * without being read.
 */
#ifndef MULTIPLAYER_MP_ENEMY_INTEREST_RULE_H
#define MULTIPLAYER_MP_ENEMY_INTEREST_RULE_H

#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The classes. NONE is a key nobody could measure, which is ranked as middle. */
typedef enum mp_enemy_reach {
    MP_ENEMY_REACH_NONE = 0,
    MP_ENEMY_REACH_NEAR,
    MP_ENEMY_REACH_MIDDLE,
    MP_ENEMY_REACH_FAR
} mp_enemy_reach_t;

/* How far past a radius a key that had reached a class keeps it, as a factor on the radius. */
#define MP_ENEMY_INTEREST_HYSTERESIS 1.1f

/* The priority a key adds every substep, by class. */
#define MP_ENEMY_INTEREST_BASE_NEAR   8u
#define MP_ENEMY_INTEREST_BASE_MIDDLE 3u
#define MP_ENEMY_INTEREST_BASE_FAR    1u

/* The most the boosts multiply a priority by. */
#define MP_ENEMY_INTEREST_BOOST_CAP 4u

/* How many substeps a key may go without a record before it goes ahead of every ranked one. */
#define MP_ENEMY_INTEREST_LIMIT_NEAR   2u
#define MP_ENEMY_INTEREST_LIMIT_MIDDLE 8u
#define MP_ENEMY_INTEREST_LIMIT_FAR    32u

/* A far key waits this many substeps between two records when nothing requires one: 4 Hz. */
#define MP_ENEMY_INTEREST_FAR_CADENCE 8u

/* How many substeps a hit on a peer's player keeps the enemy that made it going for that player,
 * whatever its target says afterwards: a second. */
#define MP_ENEMY_INTEREST_STRUCK_SUBSTEPS 32u

/* After how many substeps without an acknowledgement a peer counts as silent, and how many records
 * past the ones that may not wait it is then sent in a substep. */
#define MP_ENEMY_INTEREST_SILENT        16u
#define MP_ENEMY_INTEREST_SILENT_EXTRAS 4u

/* Why a record may not wait. A record can have both reasons. */
#define MP_ENEMY_MUST_STATE 0x02u   /* a change of the state a watcher sees */
#define MP_ENEMY_MUST_LIFE  0x04u   /* a new life of a key this peer had described */

/* Where one peer's player stands, as the host measures it this substep. */
typedef struct mp_enemy_viewer {
    bool     placed;        /* a position was there to read */
    float    position[3];
    uint32_t body;          /* the player's body object on the host, 0 for none */
    uint8_t  slot;          /* the player's world slot */
} mp_enemy_viewer_t;

/* What one key is doing this substep, read once for every peer. */
typedef struct mp_enemy_subject {
    bool     read;              /* the rest was read */
    bool     has_placement;     /* a copy an editor spawned has none, and is middle */
    float    placement[3];      /* where the engine measures the wake test from */
    float    position[3];       /* the actor, where it measures the keep test from */
    float    wake;              /* the wake radius as the activation site uses it */
    float    keep;              /* the keep radius */
    uint32_t target;            /* the body its attacks go at, 0 for none */
    bool     shooting;          /* a bolt of its own is in flight */
    uint8_t  owner;             /* a copy's owner slot, 0 for the host or none */
} mp_enemy_subject_t;

/* One key as one peer's view remembers it between substeps. */
typedef struct mp_enemy_interest_row {
    uint32_t watched;     /* what a watcher sees, as the last record to this peer had it */
    uint16_t accumulator;
    uint8_t  age;         /* substeps since the last record of this key went to this peer */
    uint8_t  reach;       /* the class last judged, for the hysteresis */
    uint8_t  life;        /* the generation of the life last described to this peer */
    bool     seen;        /* a life of this key has been described to this peer */
    uint8_t  struck;      /* substeps a hit of this key on this peer's player still counts for */
} mp_enemy_interest_row_t;

/* What the rule is asked about one key for one peer in one substep. */
typedef struct mp_enemy_interest_ask {
    uint8_t          generation;
    mp_enemy_reach_t reach;
    bool             event_waiting; /* a world event of this life waits for this peer */
    uint32_t         watched;       /* what a watcher sees now, mp_enemy_interest_watched */
    bool             engaged;       /* going for this peer's player, or a copy that player owns */
    bool             struck;        /* hit this peer's player within the last second */
    bool             shooting;
} mp_enemy_interest_ask_t;

typedef enum mp_enemy_tier {
    MP_ENEMY_TIER_SKIP = 0,   /* not in this block */
    MP_ENEMY_TIER_MUST,       /* may not wait */
    MP_ENEMY_TIER_FIRST,      /* a first description near, or of a key an event waits on */
    MP_ENEMY_TIER_STARVED,    /* waited past its class limit */
    MP_ENEMY_TIER_RANKED      /* by its accumulator */
} mp_enemy_tier_t;

/* One key offered for a block, in the shape the ordering sorts. `turn` is how far the key lies
 * behind the round robin's cursor, which breaks the last tie and keeps the old fairness. */
typedef struct mp_enemy_interest_pick {
    uint16_t key;
    uint16_t turn;
    uint16_t accumulator;
    uint8_t  tier;
    uint8_t  age;
} mp_enemy_interest_pick_t;

/* The engine's own test, the range gate's arithmetic, against a radius the hysteresis may widen. */
mp_enemy_reach_t mp_enemy_interest_reach(const mp_enemy_viewer_t *viewer,
                                         const mp_enemy_subject_t *subject,
                                         mp_enemy_reach_t previous);

/* The wake radius the activation site tests with once view_distance_fix scales it: `authored`
 * times `scale`, never as far as 0.89 of the keep radius and never below `authored`, 0 kept as 0.
 * The same arithmetic as that module's own, which a unit test compares it with. */
float mp_enemy_interest_wake(float authored, float keep, float scale);

/* What a watcher sees of an enemy, as one number two records can be compared by: the reaction
 * state inside a throw or a death and nothing for the ordinary ones, whether the body is drawn and
 * collides, the force throw, the shield, and a fold of the hidden nodes and meshes. Kept per view
 * with the life rather than read off the view's mirror, because a view given up whole forgets its
 * mirror while its peer still shows what it was sent.
 *
 * The alpha and the dissolve are not in it: they change in every substep of a fade and would make
 * every dying enemy a record that may not wait for as long as it fades. Two different sets of masks
 * can fold to the same byte, which delays such a change to the next ranked record and loses
 * nothing, since every record carries the masks whole. */
uint32_t mp_enemy_interest_watched(const mp_enemy_record_t *record);

/* Whether this peer holds the life `generation` of a key, as far as the rule knows. */
bool mp_enemy_interest_holds(const mp_enemy_interest_row_t *row, uint8_t generation);

/* Why a record may not wait, as MP_ENEMY_MUST_ bits, 0 when it may. Pure, and the one answer both
 * the block and the question for the floor under it ask. */
uint32_t mp_enemy_interest_must(const mp_enemy_interest_row_t *row,
                                const mp_enemy_interest_ask_t *ask);

/* The priority a key adds this substep. */
uint32_t mp_enemy_interest_priority(const mp_enemy_interest_ask_t *ask);

/* One substep of a live key: its age grows, its accumulator takes its priority, its class is kept
 * for the next hysteresis. A far key this peer was never told about does neither. */
void mp_enemy_interest_step(mp_enemy_interest_row_t *row, const mp_enemy_interest_ask_t *ask);

/* A key that is not live this substep starts over, and keeps which life this peer last had. */
void mp_enemy_interest_idle(mp_enemy_interest_row_t *row);

/* The enemy of this row hit this peer's player, as the host addressed the hit to that peer: for the
 * next MP_ENEMY_INTEREST_STRUCK_SUBSTEPS steps it counts as going for the player. A second hit
 * starts the second again. */
void mp_enemy_interest_note_struck(mp_enemy_interest_row_t *row);

/* Whether a hit of this row's enemy on this peer's player still counts, which is what the ask of
 * the next step says. */
bool mp_enemy_interest_struck(const mp_enemy_interest_row_t *row);

/* Where a key stands for this block, after its step. */
mp_enemy_tier_t mp_enemy_interest_tier(const mp_enemy_interest_row_t *row,
                                       const mp_enemy_interest_ask_t *ask, uint32_t must);

/* The order the picks are written in: may not wait, then first descriptions and the starved by age,
 * then the accumulator, then age, then the round robin. For a silent peer everything after the
 * first group goes by age alone. Deterministic: the turn is unique per key. */
void mp_enemy_interest_order(mp_enemy_interest_pick_t *picks, size_t count, bool silent);

/* Whether a pick in that order is still offered to a silent peer, given how many it has had
 * already past the ones that may not wait. */
bool mp_enemy_interest_admits(const mp_enemy_interest_pick_t *pick, bool silent, size_t extras);

/* A record of the life `generation`, showing `watched`, went to this peer: it starts again at
 * nought. */
void mp_enemy_interest_written(mp_enemy_interest_row_t *row, uint8_t generation, uint32_t watched);

#endif /* MULTIPLAYER_MP_ENEMY_INTEREST_RULE_H */
