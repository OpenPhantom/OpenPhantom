/* mp_phases.h: the player pipeline's loop, owned by this feature, over the pristine phase table.
 *
 * The engine ticks the player by walking a table of thirteen default phases and filtering each
 * through the current mode's descriptor. Calling the engine's own loop for the second body works
 * on a bare game and goes wrong the moment another fix is loaded: the input fix replaces two
 * entries of that table in memory with thunks that drain the host's mouse bank and step the
 * player's strafe damper, so a second call per substep through the live table would steal half
 * the player's input and hand it to the second body. Two more of its detours sit on the auto aim
 * and fire paths, reached only when the ticked body attacks.
 *
 * This module therefore replicates the loop and runs it over the entries the executable carries
 * ON DISK, read once at installation and range checked against the code section, so no thunk is
 * ever reached however many fixes are loaded. Owning the loop also gives this feature a place to
 * decide, per phase, what a second body does not get to do: the health death check, and the
 * ground publish that would move the shared camera and fire the player's plates from the second
 * body's feet. The input phase runs, off the injected command, since the input split.
 */
#ifndef MULTIPLAYER_MP_PHASES_H
#define MULTIPLAYER_MP_PHASES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Thirteen default phases, then a terminator word that is the literal 1. */
#define MP_PHASES_COUNT 13u

/* Every phase, default or mode-owned, is cdecl and takes no argument. */
typedef void(__cdecl *mp_phase_fn_t)(void);

/* A plan is the default table as this feature wants it: a NULL entry means the default phase at
 * that index is skipped, while the descriptor's own function entries are still honoured. */
typedef struct mp_phases_plan {
    mp_phase_fn_t phase[MP_PHASES_COUNT];

    /* A placed body: the plan's own entries run in order and the record's descriptor is not
     * consulted at all. A simulated body's mode owns some of its phases (the stand mode selects
     * the walk and idle clips from the body's own speed and input in phase four); a body whose
     * clips arrive from the wire must not have that selection run over them. */
    bool placed;
} mp_phases_plan_t;

/* The puppet's own phase one, handed in by the module that owns the puppet: the engine's timers
 * phase is not run for a placed body, and what a puppet needs of it (the blade ticks and the aux
 * action) is rebuilt there. May be called before or after install. */
void mp_phases_set_puppet_timers(mp_phase_fn_t timers);

/* The engine's loop, exactly, over a player record: the descriptor pointer at +0x60 names one
 * word per phase, 0 runs the default, 1 runs the default and pins the descriptor cursor so every
 * later default runs unfiltered, 2 skips, and any other value is a function to call in the
 * default's place. The word is read again after the call, because a phase may change the mode,
 * and the mode-changed flag at +0x5c ends the walk early and is cleared on the way out; the
 * terminator ends it without clearing. Pure over the record it is given, so a test can drive it
 * with a local block. Returns false when a read faulted, in which case the walk stopped there. */
bool mp_phases_dispatch(const mp_phases_plan_t *plan, uintptr_t record);

/* Reads the phase table out of the executable on disk, relocates and range checks its entries,
 * and compares them with the live table so a foreign entry is named in the log rather than run.
 * Refuses when the table cell did not resolve, the file cannot be read, or the table on disk is
 * not thirteen code addresses and a 1. */
bool mp_phases_install(void);

bool mp_phases_ready(void);

/* How many live entries are not the pristine ones. Zero on a bare game; two with the input fix. */
size_t mp_phases_foreign_slots(void);

/* The second body's tick: the pristine table with the death check withheld until
 * mp_phases_enable_death_check puts it back and the ground publish replaced by a yaw refresh,
 * run over whatever the player pointer aims at. The input phase runs and reads the second body's
 * bank through the input split. Shaped for mp_bank_run_second, which installs the second body's
 * block first. */
void __cdecl mp_phases_run_second(void);

/* The puppet walk: the puppet's own timers, the yaw refresh and the pose commit, for a body whose
 * position and heading were just written into the block from a snapshot. The engine's draw
 * interpolation then blends the object between placements. Runs over whatever the player pointer
 * aims at, like the tick, and consults no descriptor: the stand mode's own phase four would
 * restart its idle clip over the replicated one every substep. Animation advance does not depend
 * on any player phase; it is the track update in the object draw loop. */
void __cdecl mp_phases_run_puppet(void);

/* What the puppet plan costs, measured rather than assumed.
 *
 * The plan takes the ground publish away, because a body whose state arrives from the wire is
 * placed rather than simulated. The engine's footstep module, though, leaves at once when the
 * body it is handed has no floor polygon. So whether a far player can ever leave a footprint,
 * a sand puff, a water ring or a footfall sound is decided entirely by whether that cell is
 * ever set on a far body, and nothing in this tree had ever looked.
 *
 * `note_ground` reads it once per substep of a far body's window, after the plan has
 * committed, because that is the moment the engine itself would ask. Nothing is written and
 * nothing behaves differently; the two counts are the answer. */
void mp_phases_note_ground(uint32_t object);

/* Whether this machine ticks its far bodies through the player pipeline itself. It does
 * under the in process loopback and not over a socket, and the difference decides whether
 * the ground count above measures the wire or measures this machine. */
void mp_phases_set_bodies_are_ticked(bool ticked);
void mp_phases_ground_counts(uint32_t *on_floor, uint32_t *airborne, uint32_t *ticked);

/* True once a tick ended with the second body's block carrying the dead flag. The next walk over
 * a dead body runs the death table, whose one function writes the level outcome and ends the
 * level for everyone, so the caller must stop ticking the body when this answers true. The one
 * exception is a death carrying cause 5, which the death table's function answers with nothing;
 * that is the no-latch death the damage module builds on. */
bool mp_phases_second_dead(void);

/* The same latch per far bank: the tick that found the body dead was bank `index`'s, which the
 * bank names while it ticks. `mp_phases_second_dead` is bank 1's. */
bool mp_phases_dead_at(size_t index);
void mp_phases_note_revived_at(size_t index);

/* The damage module has revived the second body: the dead flag in its block is cleared and its
 * mode is a living one again, so the stop above no longer applies and ticking may resume. Calling
 * this with the flag still set in the block would re-arm the stop after one more walk. */
void mp_phases_note_second_revived(void);

/* Put the pristine death check back into the second body's plan. Only correct when the tick banks
 * the status record content (so the health read is the second body's) AND the death hull stands
 * (so the death it enters is the no-latch one); the caller vouches for both. False before the
 * loop is installed. */
bool mp_phases_enable_death_check(void);

uint32_t mp_phases_second_faults(void);

#endif /* MULTIPLAYER_MP_PHASES_H */
