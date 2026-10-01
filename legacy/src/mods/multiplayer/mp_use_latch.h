/* mp_use_latch.h: on a client, the use key reaches no script.
 *
 * ================================== The cell, and who reads it =================================
 *
 * An NPC script asks whether the player pressed use by reading one global cell, three ticks wide,
 * which the player's ground actions raise through a one line engine function. The cell names no
 * player, and there is one for the whole game. It is [0x006C4DAC], and a sweep of the image for
 * its operand bytes `AC 4D 6C 00` finds five instructions, which are all there are:
 *
 *     00433CF2  C7 05 AC 4D 6C 00 03 00 00 00  mov dword [0x6C4DAC], 3   the setter
 *     00433311  83 3D AC 4D 6C 00 00           cmp dword [0x6C4DAC], 0   the enemy tick
 *     0043331A  8B 15 AC 4D 6C 00              mov edx, [0x6C4DAC]         reads it,
 *     00433323  89 15 AC 4D 6C 00              mov [0x6C4DAC], edx         counts it down
 *     0042EBB4  83 3D AC 4D 6C 00 00           cmp dword [0x6C4DAC], 0   the script question
 *
 * So that function is the cell's only writer, the enemy tick counts it down, and the script
 * opcode "Check For?" in mode 0, "did the player just press use", is its only reader; pickups,
 * switches and doors do not read it. Holding the setter shut takes exactly one thing away from a
 * machine: the script-visible press.
 *
 * The setter has one caller, the call at 0x0044AF8A inside the ground actions at 0x0044AE65, and
 * the order there decides what the hull can reach: the turret probe returns first, then the wall
 * button test at 0x0044C9A0 and the push block test at 0x0044CBA7 are tried, and only when
 * neither claimed the press is the latch raised. Buttons and push blocks are decided BEFORE the
 * latch, so a held latch cannot touch them.
 *
 * Which scripts ask, counted over the decoded scripts of all eleven levels rather than assumed:
 * mode 0 of that opcode is asked 133 times in 101 machines, and 99 of those machines also speak,
 * so they are the talkers. The two that do not are a door in the last level and a switch in the
 * garden, driven by the use key through a script. On a client with the latch held those two do
 * not answer the client's press while the host has not activated them, and both would be a client
 * local change of the world either way. That is the whole exposure.
 *
 * =================================== Why a client loses it ====================================
 *
 * Only the host talks to people. That is a decision, and the reason behind it is what a client
 * did when it could talk: every actor the host has activated is parked on the client and runs no
 * script, but an actor the host has not reached yet runs its own script there, and on the
 * client's press that script opened a conversation of its own: a second story in the same
 * session, told to one player only. A run counted five of them. With the setter held shut on a
 * client no script there can be asked to talk, and what the host is told still reaches the client
 * through the conversation relay, within earshot.
 *
 * ============================ Why a hull, and not a one byte patch ============================
 *
 * The stored 3, the byte at 0x00433CF8, could be patched to a 0 and the cell would never rise. The
 * hull is preferred for two reasons: it counts what it swallows, which is the line a run report
 * needs to say whether a
 * client ever pressed at all, and it follows the session rather than the process, because it asks
 * at every call whether this side is a joined client. A patched byte looks the same in the log
 * whether it held or not.
 *
 * The setter is called once per substep while the key is held, because the ground actions read
 * the key as a level, so the counters count calls and say so; one press is a few of them.
 */
#ifndef MULTIPLAYER_MP_USE_LATCH_H
#define MULTIPLAYER_MP_USE_LATCH_H

#include <stdbool.h>

/* Whether a press made here is to be swallowed: this side is a client of a session that is
 * joined and has not ended. Handed in by the composition root, which knows the bridge, and asked
 * at every call so that the answer follows the session rather than the moment the hull went in:
 * a client whose session has ended and who plays on alone in the same process gets its presses
 * back without anybody having to release anything. NULL passes every press through, which is
 * also what a host does. */
typedef bool (*mp_use_latch_held_fn_t)(void);
void mp_use_latch_set_held_source(mp_use_latch_held_fn_t held);

/* Resolves the setter and hulls it. False when the site did not resolve or would not take a
 * hull, and then every press passes through as it always did. Idempotent. */
bool mp_use_latch_install(void);

void mp_use_latch_report(void);

#endif /* MULTIPLAYER_MP_USE_LATCH_H */
