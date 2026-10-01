/* mp_level_state_bind.h: the level's state on the running engine: where it lies, and the three
 * engine functions a client calls to match it.
 *
 * Layer 2. The decisions are in mp_level_state_rule; this reads the world's emitter placements,
 * the emitter pool and the level lights, and calls the engine's own switches, never writing a
 * field itself: a spawn, a pool flag and a light's slot all belong to those functions.
 *
 * The three functions are not found by their own heads. Another module may hull a head (the
 * diagnostics hull the emitter switch when their effect switch is on), and a head overwritten by a
 * branch no longer matches. Each is read out of the call its script arm makes, which nobody is
 * entitled to change, and then checked against the bytes the engine shipped behind its prologue,
 * with the head allowed to be the shipped prologue or a branch.
 *
 * The director is held here as well, for the modules that replay one of its commands: the fog and
 * the crawling text with a stand-in for the actor, and the droideka's shield with the replica
 * itself. It is the trampoline of this feature's own hull, so a replayed command is never counted
 * as one of this side's own. The escort's bar is found through the director too, out of the call
 * its command 15 makes; a client sets it with the engine's own setter, never through the arm,
 * which divides by the hit points of an actor a stand-in does not have.
 */
#ifndef MULTIPLAYER_MP_LEVEL_STATE_BIND_H
#define MULTIPLAYER_MP_LEVEL_STATE_BIND_H

#include "mp_level_state_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The director's own shape: `int op_extraFunc(character *actor, u32 cmd, int a1, f32 a2)`, the
 * float taken as its bits. */
typedef int32_t(__cdecl *mp_level_director_fn_t)(void *actor, int32_t command, int32_t a1,
                                                 int32_t a2);

/* Resolves once and says so in one line. False when any of the three functions or the pool did
 * not resolve, and then nothing here is called. */
bool mp_level_state_bind_install(void);
bool mp_level_state_bind_ready(void);

/* The director's trampoline and the address it hulls, handed over by the module that hulls it. */
void      mp_level_state_bind_set_director(mp_level_director_fn_t trampoline, uintptr_t entry);
bool      mp_level_state_bind_has_director(void);
uintptr_t mp_level_state_bind_director_entry(void);

/* One director command past this feature's own hull. False when there is no trampoline. */
bool mp_level_state_bind_call_director(uintptr_t actor, int32_t command, int32_t a1, int32_t a2);

/* The actor a replayed fog or text command is handed: a character's worth of zeros. The director's
 * head reads the body pointer at +0x34 out of whatever it is given, and the arms of those commands
 * read nothing of it, so a real actor would be a lie. */
uintptr_t mp_level_state_bind_stand_in(void);

/* Where the director's jump table sends `command`, proved as the engine shipped the dispatch, or
 * 0. The director must have been handed over first. */
uintptr_t mp_level_state_bind_director_arm(int32_t command);

/* The escort's health bar: the function the director's command 15 calls and the two words it
 * writes, read out of that call and proved past its head. Resolved on the first ask and said in one
 * line; false when it did not resolve, and then the bar is neither read nor set. */
bool mp_level_state_bind_escort(void);

/* The bar as this side's engine holds it: the health it shows, clamped 0..100 by the engine, and
 * whether it is up. False when unbound or unreadable. */
bool mp_level_state_bind_escort_read(uint8_t *health, bool *shown);

/* The engine's own setter, which raises the bar for any health above 0 and lets it fall for 0. */
bool mp_level_state_bind_escort_set(uint8_t health);

/* The function a near call names, read out of the five bytes in front of the address it returns
 * to, or 0 when they are not a call. */
uintptr_t mp_level_state_bind_callee(uintptr_t return_address);

/* A function a call named, proved where it stands: the `tail_size` bytes behind its eight byte
 * head exactly as the engine shipped them where `mask` is not 0, and the head either the one the
 * four functions this feature calls share or a branch another module put there. The address, or 0.
 * The patterns start behind the head, so they read nothing another module may write. */
uintptr_t mp_level_state_bind_proved(uintptr_t address, const uint8_t *tail, const uint8_t *mask,
                                     size_t tail_size);

/* The world the level runs in, 0 when none is open. */
uint32_t mp_level_state_bind_world(void);

/* How many emitter placements and level lights this world holds. False on a read that failed or a
 * count that is not one. */
bool mp_level_state_bind_counts(uint32_t world, uint32_t *emitters, uint32_t *lights);

/* What this side sees of one placement, by the rule's classification. */
mp_level_emitter_t mp_level_state_bind_emitter(uint32_t world, uint32_t index);

/* One level light's active word. False when it did not read. */
bool mp_level_state_bind_light(uint32_t world, uint32_t index, bool *on);

/* The engine's own switches. False only when nothing was called. */
bool mp_level_state_bind_set_emitter(uint32_t index, bool on);
bool mp_level_state_bind_set_light(uint32_t world, uint32_t index, bool on);

#endif /* MULTIPLAYER_MP_LEVEL_STATE_BIND_H */
