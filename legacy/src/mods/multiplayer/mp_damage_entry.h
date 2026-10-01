/* mp_damage_entry.h: one death entry per life of this machine's player, and the line each entry
 * writes.
 *
 * The engine enters a player's death once per life and asks nothing to make sure of it: the death
 * entry itself does not test whether the player is already dead. Three things keep a corpse from
 * entering it again. The death descriptor skips the phase that turns zero health into a death, an
 * empty contact slot refuses every contact, and a script asking for the player is answered with
 * nobody once he is dead. A session walked past two of them, and a field run entered the same death
 * on substep after substep of two fades. So in a session the death hull asks the question Quake 3
 * asks before its player_die and Source asks through its life state: a corpse, the dead flag with
 * the death descriptor up, does not enter again, and nothing is told about it either, so no second
 * wish brings the player back twice.
 *
 * Without a session nothing is refused and nothing is written, and the retail death runs as it
 * always has. What is counted either way is how many lives have ended, which the re-entry reads to
 * tell a new death from another report of the same one.
 */
#ifndef MULTIPLAYER_MP_DAMAGE_ENTRY_H
#define MULTIPLAYER_MP_DAMAGE_ENTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum mp_damage_entry_verdict {
    MP_DAMAGE_ENTRY_ENTER,
    MP_DAMAGE_ENTRY_REFUSE
} mp_damage_entry_verdict_t;

/* The rule, pure: in a session a corpse is refused, and nothing else ever is. */
mp_damage_entry_verdict_t mp_damage_entry_verdict(bool session, bool corpse);

/* The death entry is asked for this machine's own player, with `cause`, on the record the player
 * pointer names, from `caller`. Counts the life, and in a session counts the entry and writes its
 * line. The answer is whether the engine's death may run. */
mp_damage_entry_verdict_t mp_damage_entry_judge(int32_t cause, uintptr_t record,
                                                uintptr_t caller);

/* How many lives of this machine's player have ended in the death entry since the process began:
 * entries on a living body, in a session or not. False when the death hull never stood, because
 * then no entry was ever seen and the number says nothing. */
bool mp_damage_entry_lives_ended(uint32_t *lives);

void mp_damage_entry_report(void);

#endif /* MULTIPLAYER_MP_DAMAGE_ENTRY_H */
