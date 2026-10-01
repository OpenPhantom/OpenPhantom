/* mp_scratch_bind.h: the world scratchpad where it meets the engine.
 *
 * Layer 2, the binding. The logic that decides WHAT travels is next door and has no engine in it;
 * this file is the half that reads the cells and writes them back, and it holds every rule that
 * only exists because there is a running game on the other side of the call.
 *
 * ================================ When the bank may be touched ================================
 *
 * In steady play, at any time. No arm of the enemy module runs per frame or per substep, no code
 * holds a pointer into the bank across an instruction boundary, and the script opcodes read,
 * change and write back in three instructions on one thread. There is nothing to race against.
 * The sampling point is task slot 4 and the AI runs in slot 0, so a sample taken there already
 * sees this substep's bit changes.
 *
 * The dangerous moments are TRANSITIONS, not frames. Six module messages move the banks wholesale:
 *
 *   3      the module is initialised: the blackboard is zeroed and the living bank with it
 *   5      a level begins: the living bank is copied into the checkpoint
 *   6      a level ends: the blackboard is zeroed, the living bank is left alone
 *   7      a level restarts: the checkpoint is copied BACK over the living bank
 *   0x0b   a savegame is restored: both banks and all three registers are overwritten
 *   0x12   a new game
 *   0x17   a new game, the other entry
 *
 * A client that keeps applying deltas across one of these is describing a bank that no longer
 * exists on the machine receiving them. So a message from that set raises a resync, and the
 * applying side has to sit out until it has been given a fresh full state to build on.
 *
 * ============================== Why the blackboard does not travel far =========================
 *
 * Messages 3 and 6 zero all twelve blackboard dwords. The registers therefore have no meaning
 * across a level boundary and are matched WITHIN a level only. Carrying them over would hand the
 * next level four flags its scripts never set.
 *
 * ================================== The clock, and its unit ====================================
 *
 * The expiry times are absolute, measured against the world clock at `world+0x54`, in SECONDS,
 * counted from the beginning of the level. The offset is a required byte in two of the patterns
 * behind these cells, so it is read out of the image rather than taken from a header.
 *
 * Two machines do not share that clock. Nothing here ever puts an absolute time on the wire.
 */
#ifndef MULTIPLAYER_MP_SCRATCH_BIND_H
#define MULTIPLAYER_MP_SCRATCH_BIND_H

#include "mp_quest.h"
#include "mp_scratch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Resolves the five cells and checks that each is readable over its whole length. Idempotent.
 * Returns false and installs nothing when any of them is missing: a partial binding would read
 * one bank and silently leave the other at whatever the mirror happened to hold. */
bool mp_scratch_bind_install(void);
bool mp_scratch_bind_installed(void);

/* The world clock, in seconds since the level began. False when no level is open, which is a
 * normal state and not a fault: it is what a menu looks like. */
bool mp_scratch_bind_world_now(float *out);

/* The living campaign bank, all 1250 bytes of it, into a caller's buffer. */
bool mp_scratch_bind_read_bank(uint8_t *out);

/* Writes back only the bytes that differ between `want` and the live bank, and never a byte of
 * the per-hero window whatever `want` holds there.
 *
 * A caller cannot get the window wrong by forgetting, because this refuses it here as well as in
 * the decoder: the two guards are independent on purpose, since the cost of one of them being
 * wrong is one player's keys disappearing on another player's door. */
bool mp_scratch_bind_write_bank(const uint8_t *want, size_t *bytes_written);

/* ============================================================================================
 * The one way into the per-hero window, and it is deliberately a separate function rather than a
 * flag on the writer above.
 *
 * Bits 51 to 84 of the bank are the quest items and the keys, and they sit inside the window the
 * writer above refuses. They are STORY rather than a hero's belongings, so in a session they
 * belong to the host, and mp_quest is the module that says so. Everything either side of them,
 * bits 48 to 50 and 85 to 95, stays the hero's and is not touched here.
 *
 * A flag on `write_bank` would have been smaller and would have put the two rules in one place,
 * where a caller passing the wrong value takes one player's keys away silently. Two functions
 * cannot be confused by a boolean, and the one that opens the window has the word quest in its
 * name at every call site.
 *
 * The bank cell is resolved once, here, which is why these live in this file rather than beside
 * the codec: a second module resolving the same cell is a second thing to be wrong about it.
 * ========================================================================================== */
bool mp_scratch_bind_read_quest(mp_quest_set_t *out);
bool mp_scratch_bind_write_quest(const mp_quest_set_t *set, bool *changed);

/* The three blackboard registers, live. `expiry` receives the engine's own absolute times. */
bool mp_scratch_bind_read_ai(int32_t *flag, int32_t *previous, float *expiry);

/* Writes the blackboard from a decoded message, turning each remaining time back into an absolute
 * one against THIS machine's clock. A slot whose remaining time is zero is written as zero, which
 * is the engine's own way of saying there is no timer on that slot. */
bool mp_scratch_bind_write_ai(const mp_scratch_ai_t *ai);

/* Every module message passes through here. The six that move a bank wholesale raise the resync
 * below; everything else is ignored, cheaply, because this runs on the broadcast path. */
void mp_scratch_bind_note_module_message(int msg);

/* True once after a transition, and clearing as it is read. A sender uses it to start its sweep
 * over, a receiver to stop applying until the sweep arrives. */
bool mp_scratch_bind_take_resync(void);

/* How many transitions have been seen and how many bytes have been written into the bank, for the
 * report. A binding that resolved and then never wrote is the failure this is here to make
 * visible: it looks exactly like a binding that had nothing to do. */
void mp_scratch_bind_counters(uint32_t *transitions, uint32_t *bank_bytes, uint32_t *ai_writes,
                              uint32_t *quest_writes);

#endif /* MULTIPLAYER_MP_SCRATCH_BIND_H */
