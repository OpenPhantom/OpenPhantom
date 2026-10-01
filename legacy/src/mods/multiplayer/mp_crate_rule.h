/* mp_crate_rule.h: every decision about a push block that can be made without the engine.
 *
 * Layer 1, pure. The machines in mp_crate_host.c and mp_crate_client.c run these rules; the
 * engine binding in mp_crate.c reads the records and makes the calls. Nothing here keeps state
 * between calls except what a caller hands in.
 *
 * The numbers are the engine's own. A push moves a block half a unit a second, a substep is a
 * thirty second of a second, so one substep of pushing is a sixty fourth of a unit. The host
 * catches a client up at a quarter more than that, which closes a lag without a block ever moving
 * visibly faster than a player can push it. A client drawn behind the host's block follows it at
 * the same pace and jumps only past a quarter of a unit, which is sixteen substeps of pushing.
 */
#ifndef MULTIPLAYER_MP_CRATE_RULE_H
#define MULTIPLAYER_MP_CRATE_RULE_H

#include "mp_crate_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The host's catch up pace: a quarter above the engine's own push, which is half a unit a second
 * over a substep of 1/32 s. */
#define MP_CRATE_STEP_MAX      (1.25f * 0.5f * 0.03125f)

/* Two positions agree within half a sixty fourth; the wire's 1/256 steps cost at most a seventh of
 * that, so a block the host has not moved agrees with itself after the round trip. */
#define MP_CRATE_AGREE         (1.0f / 128.0f)

/* Past this a client's block is put where the host has it rather than drawn there. */
#define MP_CRATE_JUMP          0.25f

/* A step that does not bring a block this much closer to its target is no progress: the engine
 * slid it along a wall, and a target behind the wall would keep it sliding for good. */
#define MP_CRATE_PROGRESS_MIN  (1.0f / 4096.0f)

/* A block is held by the player who pushed it last, for a second and a half after that player's
 * last push or wish, which is longer than any stall the reliable channel showed with four players;
 * a let-go ends it at once. */
#define MP_CRATE_OWNER_TICKS   48u

/* A client runs at most four wishes ahead of the host's answer, a wish goes at most every four
 * substeps, and a refusal keeps the client's own push off that block for a second. */
#define MP_CRATE_WINDOW        4u
#define MP_CRATE_PUSH_TICKS    4u
#define MP_CRATE_LOCKOUT_TICKS 32u

/* A client waits this long for the host's word on its wishes before it gives the block up, and
 * this long for its own fall to land when the host has sunk the block already. */
#define MP_CRATE_ANSWER_TICKS  64u
#define MP_CRATE_SINK_WAIT_TICKS 64u

/* The note: whole once a second, a change at most once in four substeps. */
#define MP_CRATE_WHOLE_TICKS   32u
#define MP_CRATE_CHANGE_TICKS  4u

/* ==============================================================================================
 * Who holds a block.
 * ============================================================================================ */

typedef struct mp_crate_owner {
    uint8_t  slot;    /* MP_CRATE_NOBODY for a free block */
    uint32_t last;    /* the substep of the holder's last push or wish */
} mp_crate_owner_t;

void mp_crate_owner_init(mp_crate_owner_t *owner);

/* Who holds the block at `now`: the slot whose last act is younger than the decay, or nobody. An
 * idle holder holds nothing, whatever the record still says. */
uint8_t mp_crate_owner_holder(const mp_crate_owner_t *owner, uint32_t now);

/* One push or wish by `slot` at `now`. Allowed when nobody holds the block or `slot` does, and an
 * allowed one holds it from `now`. The host's own player asks the same question as slot 0. */
bool mp_crate_owner_claim(mp_crate_owner_t *owner, uint8_t slot, uint32_t now);

/* `slot` lets go. Nothing for a slot that does not hold the block. */
void mp_crate_owner_release(mp_crate_owner_t *owner, uint8_t slot);

/* ==============================================================================================
 * What a mover is, and whether an index from the wire names one.
 * ============================================================================================ */

/* A push block by its authored rig, never by its kind: the kind changes when the block sinks. */
bool mp_crate_rule_is_block(int32_t rig_flags);

/* Whether the block sinks where it lands, again by the rig. */
bool mp_crate_rule_can_sink(int32_t rig_flags);

typedef enum mp_crate_index {
    MP_CRATE_INDEX_OK = 0,
    MP_CRATE_INDEX_OUT_OF_RANGE,   /* negative, or not below the level's mover count */
    MP_CRATE_INDEX_NO_RECORD,
    MP_CRATE_INDEX_NOT_A_BLOCK,
    MP_CRATE_INDEX_SUNK
} mp_crate_index_t;

/* An index off the wire, held to what the engine's push does not hold it to: the engine refuses
 * only an index past the count and lets the count itself and every negative one through. */
mp_crate_index_t mp_crate_rule_index(int32_t id, uint32_t movers, bool record, int32_t rig_flags,
                                     int32_t kind);

/* Whether a call to the engine's opener is a push block sinking rather than a trigger: the sink
 * retypes the block first and opens it after. */
bool mp_crate_rule_opener_is_sink(int32_t rig_flags, int32_t kind);

/* The flag byte a client writes when it puts a block where the host has it: only the sink bit is
 * the host's. The carried and carrying bits are set on both sides of a pair by the engine's own
 * attach, and the falling bit only by its own drop. */
uint8_t mp_crate_rule_merge_flags(uint8_t here, uint8_t host);

/* Whether the two flag bytes say the same about what a placing puts right: carried, and sink.
 * Carrying is the rider's business and falling the drop's, so they are not compared. */
bool mp_crate_rule_flags_agree(uint8_t here, uint8_t host);

/* ==============================================================================================
 * The host pushes toward a wish.
 * ============================================================================================ */

/* The horizontal distance, which is the one the engine's push moves along; the height is the
 * floor's business. */
float mp_crate_rule_flat_distance(const float a[3], const float b[3]);

/* The step toward `target`, at most MP_CRATE_STEP_MAX long and flat. False, with no step, when
 * the block is already within MP_CRATE_AGREE of it. */
bool mp_crate_rule_step_to(const float at[3], const float target[3], float step[3]);

typedef enum mp_crate_after {
    MP_CRATE_AFTER_FELL = 0,   /* it went over an edge: the fall is the answer, whatever else */
    MP_CRATE_AFTER_REFUSED,    /* the engine answered 0 */
    MP_CRATE_AFTER_REACHED,
    MP_CRATE_AFTER_STALLED,    /* no closer than before */
    MP_CRATE_AFTER_MOVING
} mp_crate_after_t;

/* What one push did. The engine answers 0 for a block that starts to fall as well as for one it
 * refused, so the fall is asked first; otherwise every fall would read as a refusal and lock the
 * client out of a block it pushed perfectly well. */
mp_crate_after_t mp_crate_rule_after_push(int32_t answer, bool falling, float before, float after);

/* ==============================================================================================
 * A client follows the host.
 * ============================================================================================ */

typedef enum mp_crate_chase {
    MP_CRATE_CHASE_AGREED = 0,
    MP_CRATE_CHASE_STEP,
    MP_CRATE_CHASE_JUMP
} mp_crate_chase_t;

/* Where a client's block goes this substep on its way to `goal`: nowhere within MP_CRATE_AGREE,
 * one step of at most MP_CRATE_STEP_MAX within MP_CRATE_JUMP, and straight there past it. */
mp_crate_chase_t mp_crate_rule_chase(const float here[3], const float goal[3], float out[3]);

/* What a client knows about one block when it holds the host's entry against its own record. */
typedef struct mp_crate_view {
    bool  host_sunk;
    bool  host_falling;
    bool  host_carried;
    bool  here_sunk;
    bool  here_falling;
    bool  same_carrier;     /* carried here too, by the mover the host names */
    bool  sink_held;        /* a landing here waits for the host to sink it */
    bool  sink_wait_over;   /* and the host sank it more than two seconds ago */
    bool  mine;             /* this player pushes it, or waits for the host's word on a wish */
    bool  other_pusher;     /* the host names another player as its pusher */
    bool  flags_agree;      /* the carried and sink bits are the same on both sides */
    float distance;
} mp_crate_view_t;

typedef enum mp_crate_act {
    MP_CRATE_ACT_BOTH_SUNK = 0,   /* nothing; the one-shot mover note keeps the sunk pose */
    MP_CRATE_ACT_WAIT_TO_SINK,    /* the host sank it and this side's copy is still falling */
    MP_CRATE_ACT_SINK,            /* put it where the host has it and sink it */
    MP_CRATE_ACT_UNREPAIRABLE,    /* sunk here and whole there: one side reset its level */
    MP_CRATE_ACT_WAIT_HOST_FALL,  /* never written into a fall */
    MP_CRATE_ACT_WAIT_HERE_FALL,
    MP_CRATE_ACT_SECOND,          /* this player pushes a block another one holds */
    MP_CRATE_ACT_RECONCILE,       /* this player's own block: only the host's answers move it */
    MP_CRATE_ACT_CANCEL_SINK,     /* a landing held here that the host never had */
    MP_CRATE_ACT_HANG,            /* carried by the same mover on both sides: left to the carrier */
    MP_CRATE_ACT_AGREED,
    MP_CRATE_ACT_CHASE,
    MP_CRATE_ACT_JUMP             /* also a carried block on the wrong carrier, put there once */
} mp_crate_act_t;

/* The client's table, in its order: sinking before falling, falling before anything is written,
 * this player's own block before the others, and a carried block never drawn along. */
mp_crate_act_t mp_crate_rule_decide(const mp_crate_view_t *view);

typedef enum mp_crate_gate {
    MP_CRATE_GATE_LET = 0,
    MP_CRATE_GATE_OTHER_OWNER,
    MP_CRATE_GATE_AHEAD,          /* MP_CRATE_WINDOW wishes unanswered */
    MP_CRATE_GATE_LOCKED,         /* within a second of a refusal */
    MP_CRATE_GATE_FALLING_OR_SUNK
} mp_crate_gate_t;

typedef struct mp_crate_gate_view {
    bool     host_speaks;       /* a note of this level has arrived: the host runs this protocol */
    bool     other_holds;       /* the host names another player, within the holding time */
    uint16_t ahead;             /* wishes sent and not yet taken */
    bool     locked;
    bool     falling_or_sunk;   /* on either side, or a landing held here */
} mp_crate_gate_view_t;

/* Whether this player's own push of a block goes to the engine. A refused one leaves the player
 * standing, the way a push against an actor in the way does in single player. */
mp_crate_gate_t mp_crate_rule_gate(const mp_crate_gate_view_t *view);

typedef enum mp_crate_reconcile {
    MP_CRATE_RECONCILE_NOTHING = 0,
    MP_CRATE_RECONCILE_CORRECT,   /* move this side's block by the host's deviation */
    MP_CRATE_RECONCILE_TO_HOST    /* refused: back to where the host has it, and locked out */
} mp_crate_reconcile_t;

/* The host's verdict on this player's wish `sequence`, held against the target that wish carried.
 * On the way and stale say nothing yet. Reached says nothing when the host's block is where the
 * wish put it; otherwise the difference moves this side's block and the part of its own push
 * that the host has not seen yet stays. `target` may be NULL for a wish no longer remembered. */
mp_crate_reconcile_t mp_crate_rule_reconcile(uint8_t verdict, const float host[3],
                                             const float *target, float correction[3]);

/* Two sequence numbers read with their sign, so a counter that wrapped is still newer. */
bool     mp_crate_rule_sequence_newer(uint16_t candidate, uint16_t held);
uint16_t mp_crate_rule_sequence_ahead(uint16_t sent, uint16_t taken);

/* Whether the newest target goes to the host now: the first push and a let-go at once, anything
 * else at most every MP_CRATE_PUSH_TICKS substeps. */
bool mp_crate_rule_push_due(bool first, bool let_go, uint32_t now, uint32_t last_sent);

/* Whether a push moved the block toward the player rather than away from it. */
bool mp_crate_rule_is_pull(const float block[3], const float player[3], const float delta[3]);

/* The engine's crush test, a vertical cylinder against a body's own: the spans overlap in height
 * and the two circles overlap on the floor. */
bool mp_crate_rule_in_cylinder(const float centre[3], float radius, float height,
                               const float body[3], float body_radius, float body_height);

/* ==============================================================================================
 * When the host's note goes.
 * ============================================================================================ */

typedef struct mp_crate_cadence {
    bool     started;
    bool     whole_wanted;   /* a peer arrived, or a level began */
    uint32_t last_whole;
    uint32_t last_change;
} mp_crate_cadence_t;

typedef enum mp_crate_due {
    MP_CRATE_DUE_NONE = 0,
    MP_CRATE_DUE_WHOLE,
    MP_CRATE_DUE_CHANGE
} mp_crate_due_t;

void           mp_crate_cadence_init(mp_crate_cadence_t *cadence);
mp_crate_due_t mp_crate_cadence_due(const mp_crate_cadence_t *cadence, uint32_t now, bool changed);
void           mp_crate_cadence_sent(mp_crate_cadence_t *cadence, mp_crate_due_t due,
                                     uint32_t now);
void           mp_crate_cadence_want_whole(mp_crate_cadence_t *cadence);

/* Whether an entry says something the one sent before it did not. The position counts at the
 * wire's own resolution, and not at all while the block falls: a fall moves it every substep and
 * a client waits for it to land anyway. */
bool mp_crate_rule_entry_differs(const mp_crate_entry_t *sent, const mp_crate_entry_t *now);

#endif /* MULTIPLAYER_MP_CRATE_RULE_H */
