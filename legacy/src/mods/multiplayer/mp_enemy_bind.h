/* mp_enemy_bind.h: the engine's live actors, walked, read and written.
 *
 * Layer 2, the binding. What an enemy IS on the wire is decided next door and has no engine in it;
 * this is the half that finds the actors and moves values in and out of them.
 *
 * ================================= How the actors are found ===================================
 *
 * The pool is an intrusive chain, not a table. There is no pool index: a slot begins with a link
 * word and the actor is the four bytes after it, the next node is what the link word holds, and a
 * free slot carries -1. The list header in front of the slab holds the chain head at +0x04 and
 * the capacity at +0x10.
 *
 * The chain is walked BY HAND and never through the engine's own list_next, which is not a matter
 * of taste: the iteration cursor lives in the list, not in the caller, so an engine walk started
 * here would derail any walk the engine already has open. The same reasoning is already written
 * into the object pool walk this feature does elsewhere.
 *
 * The actor is the node PLUS FOUR. Treating the chain head as the first actor reads four bytes
 * short and hands back the script index where the placement index was wanted.
 *
 * ============================ Taking the drive away from a replica =============================
 *
 * Two ways, and they do different things.
 *
 * The AI switch is one dword and it stops everything for every actor: no pre-tick, no state arm,
 * no post-tick, no pose commit, and no local spawning either. That is the whole client case, and
 * it is what mp_enemy already owns.
 *
 * Parking is per actor. A parked actor is skipped before the pre-tick AND before the removal
 * decision, so the client's own deactivation radius cannot throw a replica away while the host
 * still has it. The cost is that a parked actor never gets its pose committed again, so the
 * receiver has to write the body's pose pair itself; that is required anyway.
 *
 * A parked actor also loses the fade and shatter arms. A parked corpse does not go away by
 * itself, and something has to remove it.
 *
 * ================================= The playhead is arithmetic =================================
 *
 * Reading how far an animation has run needs no engine call. The thing is at body+0x9c, the puppet
 * at thing+0x18, the track array is the puppet's own at +0x08 with a stride of 0x14c, and the
 * playhead is the float at +0x120 of a track, counted in frames. The base slot is at body+0xec and
 * the overlay at body+0xf8, each -1 when empty. mp_enemy_pose does it, with the check on the slot
 * the engine's own accessor leaves out.
 *
 * =============================== What may be written, and what not ============================
 *
 * Positions, angles, health, the reaction state, the volatile flag bits and the body's pose pair;
 * the drawn bit, the shadow bit, the class, the alpha and the dissolve of the body as the host's
 * body has them, which mp_enemy_body writes; and the hidden flags of the body's nodes and meshes,
 * which mp_enemy_nodes writes the way the engine's own savegame restores them. Everything else
 * in the actor is a pointer or bookkeeping: the placement, the script,
 * the template, the body, the instruction pointer, the placement index, the floor block, the
 * tracked shot, the last attacker, the target, and the effect handle. Breaking any of those takes
 * the engine apart, so nothing here writes them.
 */
#ifndef MULTIPLAYER_MP_ENEMY_BIND_H
#define MULTIPLAYER_MP_ENEMY_BIND_H

#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Resolves the pool and checks that its header has the shape these patterns describe. Idempotent.
 * False when anything is missing, and nothing is half installed. */
bool mp_enemy_bind_install(void);
bool mp_enemy_bind_installed(void);

/* One live actor, as the walk hands it over. The address is the actor, not the node. */
typedef void (*mp_enemy_bind_visit_fn_t)(uintptr_t actor, void *user);

/* Walks the chain and calls back once per live actor. Returns how many it saw, and zero when the
 * pool could not be read, which a caller cannot tell apart from an empty level and does not need
 * to: both mean there is nothing to send.
 *
 * A chain that does not end within its own capacity is reported and abandoned rather than
 * followed, because the alternative is walking a corrupted pool forever inside a substep. */
uint32_t mp_enemy_bind_walk(mp_enemy_bind_visit_fn_t visit, void *user);

/* The same walk for a caller that must know whether it saw the whole chain, the NPC copies'
 * census: `complete` is false with no level open, with a chain that runs past its capacity and
 * with a link that could not be read, and `capacity` is the pool's. Not counted among the walks,
 * which count the enemy block's census alone. */
uint32_t mp_enemy_bind_walk_whole(mp_enemy_bind_visit_fn_t visit, void *user, bool *complete,
                                  uint32_t *capacity);

/* Fills a wire record from a live actor, presence bits included. False when the actor or its body
 * cannot be read, in which case the record is left alone rather than half filled. */
bool mp_enemy_bind_read(uintptr_t actor, mp_enemy_record_t *out);

/* The part of that read that changes nothing: the actor's own fields, the puppet's clip, playheads
 * and turned nodes, and a flyer's pitch and roll, by the same code the census runs, with no counter
 * moved and nothing remembered for the next substep. The shield, the hidden nodes and the body's
 * end are left out, because they count and remember. `body` gets the body read on the way. For the
 * double probe that times the census both ways; the census itself reads through the call above. */
bool mp_enemy_bind_peek(uintptr_t actor, mp_enemy_record_t *out, uint32_t *body);

/* Applies a decoded record to a live actor.
 *
 * `previous` is what was last applied to this actor and becomes the body's previous pose, which is
 * what buys the free interpolation between substeps; pass NULL for the first application after a
 * spawn, and the actor's current position is used instead so it does not smear in from wherever
 * the slot last was.
 *
 * The ANIMATION is applied here too, and the first version of this file did not do it: position
 * and heading landed and nothing chose a clip, so a replica stood in the right place and did not
 * move. The clip is started when it differs from what the body plays or the host began it again,
 * and the playhead is left to the puppet, which advances on its own once per drawn frame; a start
 * the host made long ago is entered at its head once (mp_enemy_pose.h).
 *
 * The clip is bounded rather than trusted: the engine's own pose commit checks a clip against the
 * model's clip count, and the direct play call does not get that check for free.
 *
 * How the body ends, lying down, fading, dissolving, is written from the host's body field and
 * not worked out from the reaction state (mp_enemy_body.h). */
bool mp_enemy_bind_write(uintptr_t actor, uint32_t key, const mp_enemy_record_t *record,
                         const mp_enemy_record_t *previous);

/* What closing one parked replica's pose pairs did. */
typedef struct mp_enemy_pair_close {
    bool ours;       /* the body was this module's: the actor carries no player and has a body */
    bool rotation;   /* the previous rotation now holds the current one, all three angles */
    bool position;   /* and the previous position the current one */
} mp_enemy_pair_close_t;

/* Brings a parked replica's two pose pairs to one value each, as the engine's own pose commit does
 * for an actor that did not move in a substep. The commit does not run for a parked actor, so
 * without this a substep with no record kept the pairs the last write opened, and every frame of
 * it drew that turn and that step again from their start.
 *
 * Asked of the same body as mp_enemy_bind_write and refused where that is refused: an actor that
 * carries the player's body, or has none, is left alone and `ours` says so. True when both pairs
 * were closed; false with no `out` to say it in. */
bool mp_enemy_bind_close_pair(uintptr_t actor, mp_enemy_pair_close_t *out);

/* Parks an actor so the local simulation steps over it entirely, or lets it go again. */
bool mp_enemy_bind_park(uintptr_t actor, bool parked);
bool mp_enemy_bind_is_parked(uintptr_t actor);

/* Lets a replica go AND puts it in the state the far side last reported, rather than the state
 * it was parked in, and a body a removal kept as a corpse whatever the record says. A
 * replica is parked the moment it is first seen, which is nearly always
 * while it is active, and it is let go when the host stops listing it, which for a fighter is
 * nearly always after it died. Restoring the parked state would hand this machine's simulation
 * an ACTIVE actor wearing a death clip, and the script would stand it up. With the reported
 * state the engine's own corpse arms run instead and the body fades the way it does anywhere.
 *
 * The standby state and the player-host state are never written: the first is the parking
 * itself, the second belongs to a placement no receiver creates. `last` may be NULL, and then
 * this is a plain unpark. */
bool mp_enemy_bind_release(uintptr_t actor, const mp_enemy_record_t *last);

/* The placement index of a live actor, which is how the two machines agree on WHICH enemy this
 * is. It has exactly one writer in the engine and is stable for an actor's whole life. */
bool mp_enemy_bind_index(uintptr_t actor, uint32_t *out);

/* What the two rules below read of one actor: the link word of its pool slot, its placement
 * index, the record it was made from, and that record's live word. */
typedef struct mp_enemy_liveness {
    bool     read;        /* all four words could be read */
    uint32_t link;        /* the slot's link word, the free mark once the pool took it back */
    uint32_t index;       /* the actor's placement index */
    uint32_t record;      /* the record the actor was made from */
    uint32_t live_word;   /* that record's live actor, zero once a removal ran on it */
} mp_enemy_liveness_t;

/* What a remembered actor address holds now, which is two questions and not one.
 *
 * Is it still THIS placement's actor: the slot is linked, carries the key, and its record names
 * this actor or nobody. And is that actor ALIVE: the record names it. The two part at exactly
 * one point, a body a removal kept. The engine's removal for a corpse that stays clears the
 * record's live word and leaves the body linked and ticking as a corpse, and the host goes on
 * listing and describing it. A receiver that asked "alive" before every write stopped writing
 * the moment it performed that removal, and its replica stood in whatever clip it was playing.
 *
 * Writing, letting go and performing a removal ask the first question; whether a body has to be
 * built asks the second. */
typedef enum mp_enemy_slot {
    MP_ENEMY_SLOT_UNREAD,   /* not every word could be read */
    MP_ENEMY_SLOT_FREED,    /* the pool took the slot back */
    MP_ENEMY_SLOT_OTHER,    /* linked, but another placement's actor or another life's */
    MP_ENEMY_SLOT_KEPT,     /* this actor, a body a removal kept */
    MP_ENEMY_SLOT_LIVE      /* this actor, alive */
} mp_enemy_slot_t;

/* The rule on words already read, so a test can hold it without an engine. The index is
 * compared as well as the record: a slot freed and taken by another placement between the
 * census and the question points at THAT placement's record, whose word names the slot. */
mp_enemy_slot_t mp_enemy_slot_of(const mp_enemy_liveness_t *seen, uintptr_t actor, uint32_t key);

/* Whether a slot is still the actor it was remembered as, alive or a kept corpse. */
bool mp_enemy_slot_is_actor(mp_enemy_slot_t slot);

/* The slot judged for the life a receiver describes. A kept corpse is that placement's actor
 * only for the life this side kept it for, `kept_actor` under `kept_generation` (0 for none).
 * The host starts a new life of a placement only after the old body left its chain, so a newer
 * life described while the old corpse still lies here belongs to a body of its own; written onto
 * the corpse, it would stand a living actor up in a body with no class. */
mp_enemy_slot_t mp_enemy_slot_for_life(mp_enemy_slot_t slot, uintptr_t actor, uint8_t generation,
                                       uintptr_t kept_actor, uint8_t kept_generation);

/* The two marks the engine's death arms take off a body: its class, which is what makes it
 * collide and a target, and bit 1 of its flags, which gives it a ground shadow. A replica's end
 * is written from the host's body field; the burst of a replica takes both itself, before it
 * breaks the body up, as the engine's burst does. */
typedef struct mp_enemy_marks {
    int32_t  objclass;
    uint32_t flags;
} mp_enemy_marks_t;

#define MP_ENEMY_BODY_SHADOW 0x02u

/* The marks of a live actor's body, read and written. */
bool mp_enemy_bind_marks(uintptr_t actor, mp_enemy_marks_t *out);
bool mp_enemy_bind_set_marks(uintptr_t actor, const mp_enemy_marks_t *marks);

/* Reads the four words of `actor` and answers what its slot holds for `key`. They are read
 * with the faulting read: a freed pool slot keeps its bytes, and a record is only freed with
 * its whole level. `seen` may be NULL; when given it holds what was read, for a log line. */
mp_enemy_slot_t mp_enemy_bind_slot(uintptr_t actor, uint32_t key, mp_enemy_liveness_t *seen);

/* Whether `actor` is still the live actor of `key`, asked of the record the actor was made
 * from rather than of the level's directory. The spawner writes the record's live word with
 * the new actor's address and every delete clears it, so while an actor lives the word names
 * exactly that actor. The index is compared as well: a pool slot freed and taken by another
 * placement between the census and this question points at THAT placement's record, whose
 * word names the slot, and without the index the answer would be yes for the wrong enemy.
 * `seen` may be NULL; when given it holds what was read, for a log line. */
bool mp_enemy_bind_is_live(uintptr_t actor, uint32_t key, mp_enemy_liveness_t *seen);

/* The liveness rule on words already read: the slot answers MP_ENEMY_SLOT_LIVE. */
bool mp_enemy_liveness_holds(const mp_enemy_liveness_t *seen, uintptr_t actor, uint32_t key);

/* Counters for the report: a binding that resolved and then never wrote is the failure worth
 * seeing, and it looks exactly like one that had nothing to do. */
void mp_enemy_bind_counters(uint32_t *walks, uint32_t *reads, uint32_t *writes, uint32_t *refused);

/* The same refusals split by reason, one line after the binding line: an actor that carries the
 * player's body (per placement), one with no body, a position that would not write. */
void mp_enemy_bind_report_refusals(void);

/* How many walks of the pool stopped at a link that would not read. The first is logged with how
 * far it got, and the rest only counted here, because three walks a substep meet the same link. */
uint32_t mp_enemy_bind_broken_links(void);

/* How full the actor pool is: the chain's length against the header's capacity, which the shipped
 * pool creates as 128. False when no level is open. A walk, so a caller asks once per decision and
 * not once per placement. */
bool mp_enemy_bind_occupancy(uint32_t *live, uint32_t *capacity);


#endif /* MULTIPLAYER_MP_ENEMY_BIND_H */
