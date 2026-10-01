/* mp_world_apply.h: the host's map state where it meets the engine.
 *
 * Layer 2. The message this builds and reads is next door in mp_world_state.c and knows nothing
 * about a game; this is the half that walks the world's mover table, counts what the two sides
 * disagree about and writes the few movers it is allowed to write.
 *
 * =============================== What it is allowed to write ==================================
 *
 * Doors, buttons and the three one-shot types (an elevator, a lift, a drawbridge), and only while
 * they are standing still on BOTH sides. Only what the note names is ever touched: a note carries
 * the movers that are away from home, so applying it can put a door into the open state and
 * never back into the closed one. That is what makes a note a second late harmless, the worst it
 * can do is open a door the host had open a second ago and the local integrator closes it again
 * on its own timer, whereas a sweep that acted on the ABSENCE of an id could slam a door the
 * local player opened by prediction before its event reached the host.
 *
 * A mover in the middle of its travel is not touched, for two reasons that agree. The correction
 * would arrive describing a pose that is already a second old, and the local integrator would
 * overtake it inside one substep anyway; and a pose that jumps skips every keyframe segment
 * between the old one and the new one, which is where the engine switches collision faces on and
 * off, starts and stops sounds and reveals bodies. The pose application is edge triggered, it
 * fires a bit only when the segment's flag word has it and the latch does not, so a jump sees the
 * target segment's flags alone. Four of the bits rewrite the flags of every face bound to the sub
 * node, and they do not commute:
 *
 *     bit 0x04:  if (face & 0x10) && !(face & 0x80)   face = (face & ~0x10) | 0x44
 *     bit 0x08:  if (face & 0x40)                     face = (face & 0xBB) | 0x10
 *     bit 0x10:  if (face & 0x04)                     face = (face & ~0x04) | 0x90
 *     bit 0x20:  if (face & 0x80)                     face = (face & 0x6F) | 0x04
 *     bit 0x40:  the bound body is revealed, when mover+0x60 names one
 *     bits 0x01 and 0x02: the zone sound starts and stops
 *
 * so the question can be answered over the shipped data by composing each operator over the
 * sixteen reachable face states. A jump ends in the same face state as a drive for 464 of the 528
 * mover records and in a different one for 64 (two of type 0, 21 of type 2, one of type 3, 18 of
 * type 4, 21 of type 6, one of type 7), and 62 records lose a reveal they would have fired on the
 * way. The count is sensitive to whether the first segment's own edges count as already latched
 * after the level prime, 309 against 219 the other way, and the reveal count is 62 either way.
 *
 * That cost is paid anyway, because the alternative to paying it is a client standing behind a
 * door that is shut for him. It is paid in the one shape the engine itself uses to put a mover
 * somewhere from outside, which is what its savegame restore does: flip the list membership
 * through the engine's own opener or closer, write the pose, rebase the time the travel is
 * measured from, then run ONE integration so the geometry, the faces, the sounds and the rider
 * carrier all come along, then put the direction back on top.
 *
 * Two other shapes were read and refused. The freeze cell at [0x005B5FCC], compared at the head
 * of the integrator, has no writer anywhere in the retail image and cannot serve here: its return
 * sits before the pose application, so a frozen map has no geometry composition, no face
 * switching, no sound and a rider carry of zero, and every lazy tick site the collision and the
 * draw pass depend on stops working. A pose stream every tick was refused by the measurement: a
 * door or a button is at most one substep of travel out of step, at most 0.095 units, and is put
 * back in step by the event that starts its next cycle, and a one-shot writes its pose wrongly
 * exactly once, at its latch; a section carrying every moving mover every tick would pay for 222
 * records that never need it and 142 that need it once.
 *
 * The push blocks, type 7, 43 records, are not written. They have no timeline: the pose is always
 * zero, the block is moved in world space by whoever pushes it, and the direction field is a
 * pointer store. Their state is the world position, the flag byte at +0x50, the three bytes after
 * it and the carry offset, and those have to travel together or not at all, because a position
 * that does not match what the local carrier is doing files the block into the wrong occupancy
 * cell and, when that cell is full, into none, silently.
 *
 * ================================= Why it runs before the substep ==============================
 *
 * The rider carrier ticks the mover somebody stands on before it works out how far to move that
 * rider, and it takes the difference between the sub node's world position now and its world
 * position at the head of the previous pose application. A correction that ran after it would move
 * a standing player by the whole of the correction.
 *
 * The single integration this makes is what actually removes that hazard: it refreshes the sub
 * node's previous world position itself, so the jump's difference is consumed here rather than
 * under somebody's feet. The placement before the substep is the earlier of the two moments the
 * bridge owns and costs nothing, but the engine's own player slot runs earlier still, so the
 * ordering alone would not have been enough.
 */
#ifndef MULTIPLAYER_MP_WORLD_APPLY_H
#define MULTIPLAYER_MP_WORLD_APPLY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Resolves the integrator a corrected mover is settled with. Idempotent. False means this side
 * counts the difference and writes nothing, which is a usable state and is logged as one. */
bool mp_world_apply_install(void);

/* Which side owns the map. The host describes it and never applies; everything else applies and
 * never describes. Set once, from the bridge, when the transport's shape is decided. */
void mp_world_apply_set_authority(bool host);

/* This substep's note, or 0 bytes when none is due, when this side does not own the map, or when
 * nothing in the level is away from the position it was authored with. */
size_t mp_world_apply_build(uint32_t tick, uint8_t *buffer, size_t capacity);

/* Whether one reliable message is the map's state note, and if so, taking it. A note that is ours
 * but cannot be used is still taken, so a torn one cannot be read as something else's message. */
bool mp_world_apply_take(const uint8_t *note, size_t bytes);

/* One note the reliable channel had no room for. It is not kept: a description of a map is only
 * worth what it says about the map now, and the next one is a second away. The count is here
 * because the channel reserves the packet's payload first and seats messages in what is left, so
 * a level with a large actor block can crowd out a large note for a long time without anything
 * failing. */
void mp_world_apply_note_refused(void);

/* Everything the note in hand asks for that this side can honour. Runs once per substep, before
 * the substep, and does nothing at all until a note has arrived. */
void mp_world_apply_pending(void);

/* Forgets the note in hand and the generation believed of the far side. Every level transition
 * rewrites every mover's time base, so a note from before one describes a map that is gone. */
void mp_world_apply_forget(void);

/* How many bank transitions this machine has seen, the generation the host's notes carry. */
uint32_t mp_world_apply_generation(void);

void mp_world_apply_report(void);

#endif /* MULTIPLAYER_MP_WORLD_APPLY_H */
