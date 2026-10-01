/* mp_world_event.h: the moments of the world, from a hull on the host to the engine on a client.
 *
 * Layer 3. The codec and the decisions are mp_world_event_rule.h; this is the host's ring, what
 * each peer has been sent and has acknowledged, and a client's queue and its players.
 *
 * ======================================== On the host ==========================================
 *
 * A hull posts an event while this side describes its enemies, which is a host in a session and
 * nobody else. It takes its number when it is posted, from a count that is never reset, and its KEY
 * from the actor that made it. Its LIFE comes from the census that follows in the same substep: the
 * census counts the lives, and a death the engine made in this substep is already a new life's
 * business by then. An actor that is gone keeps the life last counted for its key; an actor the
 * census's walk still met, but did not read under its key because another one carries it, is no
 * life the host describes, and its event is dropped and counted apart; an event with neither is
 * dropped and counted.
 *
 * A substep takes 32 events of every kind together. When it is full, an event of a kind that is
 * seen takes the place of the newest sound of the same substep, which no peer has been offered
 * yet; only a substep full of what is seen refuses one.
 *
 * Every block to a peer carries every event that concerns that peer and that no acknowledgement
 * has yet proved delivered, newest first when the part is full, for at most the window. Whether an
 * event concerns a peer is asked once, when it is first offered to that peer's block, so the
 * question for the floor and the block itself get the same answer. Delivered means an
 * acknowledgement named a block that carried it. Sending is not delivering; the enemy records
 * believe a send and heal a gap later, an event cannot be healed and so waits for the proof. The
 * acknowledgement names only the newest payload a client decoded, and because an event rides every
 * block until it is proved, the newest one proves everything it carried; an event that runs out of
 * window without that proof is counted as unacknowledged, which is not the same as lost.
 *
 * ======================================== On a client ==========================================
 *
 * The part is read with the rest of the block and taken only when the whole block is. A number seen
 * before is not taken twice. What is taken is performed at the start of a substep, behind the
 * bodies the host's blocks asked for, so that the replica an event belongs to is there if it can
 * be: one that is not waits for the rest of the window and is then dropped and counted. The players
 * are the modules that own each kind, one per kind, handed in by those modules.
 */
#ifndef MULTIPLAYER_MP_WORLD_EVENT_H
#define MULTIPLAYER_MP_WORLD_EVENT_H

#include "mp_world_event_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ==============================================================================================
 * The host.
 * ============================================================================================ */

/* An event of `kind` made by `actor`, with `a` and `tail_bytes` of tail as the kind's table says.
 * Taken only while this side describes its enemies; true when it was numbered. */
bool mp_world_event_post_at_actor(uint8_t kind, uintptr_t actor, uint16_t a, const uint8_t *tail,
                                  size_t tail_bytes);

/* The same, and where it happened: `place` in world units, and `radius` how far a kind heard at a
 * place carries, 0 for a kind that is not heard. A place off every shipped level is not one the
 * record's quantisation can carry, and the event then travels without it. */
bool mp_world_event_post_at_actor_placed(uint8_t kind, uintptr_t actor, uint16_t a,
                                         const float place[3], float radius, const uint8_t *tail,
                                         size_t tail_bytes);

/* An event of a kind that is heard, made by `actor` standing at `place`, which a peer is concerned
 * by while its player stands within `radius` and the margin. A place the enemy record cannot carry
 * leaves the event at the actor alone, and then the interest rule decides who hears it. */
bool mp_world_event_post_heard(uint8_t kind, uintptr_t actor, uint16_t a, const float place[3],
                               float radius);

/* The music the host claims for one peer, asked whenever that peer's part is written: a sound call
 * for each of the two music channels, 0 for none. Handed in by the module that owns the scripts'
 * music; without one every block says none. */
typedef void (*mp_world_event_music_fn_t)(size_t view, uint16_t *state, uint16_t *sequence);
void mp_world_event_set_music_source(mp_world_event_music_fn_t source);

/* The census's walk met `actor` in the pool, whether or not it reads it under a key. Called for
 * every actor of every walk; only a side that posted something in this substep looks. */
void mp_world_event_census_saw(uintptr_t actor);

/* The census read `actor` under `key` in life `life`: its events of this substep are that
 * life's. */
void mp_world_event_census_row(uint32_t key, uint8_t life, uintptr_t actor);

/* The census is over: an event whose actor it did not read takes the life last counted for its
 * key when the walk no longer met the actor; one whose actor the walk met is dropped and counted
 * apart, and one with no such life is dropped and counted. */
void mp_world_event_census_over(void);

/* The end of every substep, whether a census ran or not. An event no census settled is dropped, the
 * clock moves on, and an event past the window leaves the ring. */
void mp_world_event_census_done(void);

/* The bytes the events for view `view` take behind the part's head this substep. A pure question,
 * asked by the floor and by the block: whether an event concerns the view is decided the first
 * time either asks and then kept. */
size_t mp_world_event_part_bytes(size_t view);

/* The part's head and as many of those events as `room` holds, into `out`; the bytes written, or 0
 * when `room` does not hold the head. */
size_t mp_world_event_write(size_t view, uint8_t *out, size_t room);

/* Whether an event for view `view` stands on the life `life` of `key`, unproved and inside the
 * window. Pure: the interest rule asks it for the block and for the floor alike. */
bool mp_world_event_waits_on(size_t view, uint32_t key, uint8_t life);

/* The four moments of a view's payload, from the enemy block's own four: it went out on `tick`, it
 * was acknowledged, it did not go out, and the view is a new connection. */
void mp_world_event_sent_for(size_t view, uint32_t tick);
void mp_world_event_acked_for(size_t view, uint32_t tick);
void mp_world_event_abandon_for(size_t view);
void mp_world_event_forget_view(size_t view);

/* ==============================================================================================
 * A client.
 * ============================================================================================ */

/* The first pass over a block: the part at `*at` is read and held, and `*at` moves behind it. False
 * when the part is torn, and then the whole block is refused. */
bool mp_world_event_stage(const uint8_t *block, size_t bytes, size_t *at);

/* The block of substep `tick` was taken: the part held by the stage is taken with it. */
void mp_world_event_take_staged(uint32_t tick);

/* Performs what was taken, from a substep. Returns how many were performed. */
uint32_t mp_world_event_flush(void);

/* The one player of a kind, handed in by the module that owns the kind. It answers false when it
 * could not perform the event. `replica` is 0 for an event performed at its place. A second player
 * for a kind that has one is refused and said so. */
typedef bool (*mp_world_event_player_fn_t)(const mp_world_event_t *event, uintptr_t replica);
bool mp_world_event_set_player(uint8_t kind, mp_world_event_player_fn_t player);

/* The music the host said in the last block this side took, 0 for none on either channel. */
void mp_world_event_music_heard(uint16_t *state, uint16_t *sequence);

/* Whether an output of `actor`'s own script belongs to a life of the host's, and so is withheld on
 * this side: true only on a client of a started session (`client_of_started`, the session's own
 * answer), for an actor that stands here for a key whose life the host has described. That is the
 * replica this side holds for the key, parked or let go, until a removal forgets it; an actor under
 * a key the host never described, or one beside the key's replica, plays its own outputs as it
 * always did. The one question for every script output a client could double: the sounds (opcode
 * 0x603), the lines (0x504), the emitters (0x608) and the director's commands 8, 9, 17 and 19. */
bool mp_world_event_output_is_the_hosts(uintptr_t actor, bool client_of_started);

/* ==============================================================================================
 * Both.
 * ============================================================================================ */

/* The one exit for every event state: the ring, every view, a client's queue and its memory. Taken
 * by the enemy block's own reset, which every way out of a level and a session takes. The host's
 * number is not reset: a client that has not reset yet must not take a new event for an old one. */
void mp_world_event_reset(void);

void mp_world_event_report(void);

#endif /* MULTIPLAYER_MP_WORLD_EVENT_H */
