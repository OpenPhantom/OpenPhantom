/* mp_damage.h: a bank body's death without the level ending, and the way back from it.
 *
 * The campaign loop spins while the level outcome cell reads 2 and leaves it for ANY other value,
 * so one player's death ends the level for everyone. The retail death does not write that cell
 * itself: it hangs the death descriptor on the player's mode slot, and the descriptor's one
 * function raises the outcome on a LATER walk, keyed by the death cause. Cause 5 falls through
 * that function's switch untouched. A body whose mode is the death descriptor and whose cause is
 * 5 is therefore an inert corpse: it can even keep being walked and nothing ends.
 *
 * That is the whole design. One detour on the retail death entry routes a bank body's death to a
 * replica that sets exactly the corpse (dead flag, death descriptor, cause 5, shadow off, the
 * death clip) and none of the campaign machinery: no outcome, no death statistic, no music, no
 * health write, and no nulling of the current task's contact slot, which during a bank tick would
 * be this feature's own task record. The player's own death still runs the retail path untouched.
 *
 * What this module does NOT give a bank body is health. The bank swaps the hero block and the
 * hold accumulator; the status record the health lives in stays bank 0's, which is why the death
 * check phase is withheld from the second body and why nothing here calls the health writer
 * inside a tick window. Routing damage into per-bank health is the module's open half.
 *
 * ========================= The player's own death, and why it is different ====================
 *
 * A bank body gets the replica because none of the campaign machinery is meant for it. The
 * player's own body is the opposite case: everything the retail death does is right for it, the
 * health write, the death voice, the death music, the clip chosen from the floor and the wall
 * behind, and the nulling of the contact slot. Replacing that would be a worse death, not a
 * survivable one.
 *
 * So the player's own death keeps the whole retail path and gets ONE write afterwards: the cause
 * becomes 5. The corpse is then exactly the retail corpse, and the descriptor's function finds no
 * arm for it on the next walk, so the level outcome is never raised and the campaign loop keeps
 * spinning. That is the same trick as the replica, applied from the other side.
 *
 * Two things gate it, and neither is optional. It happens only while a session asks for it, so a
 * single player installation dies exactly as it always has; and it happens only for a death the
 * player's own body suffered. A script that ordered the death has to keep ending the level for
 * everyone, or a failed objective leaves the level running with nothing left to do in it.
 *
 * In front of all of it the hull asks whether the player is a corpse already, and in a session a
 * corpse enters no second death (mp_damage_entry.h).
 */
#ifndef MULTIPLAYER_MP_DAMAGE_H
#define MULTIPLAYER_MP_DAMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The two death causes this module names. The engine passes the cause to the death entry and
 * stores it in the record, and the descriptor's own function at 00450309 switches on it: 0 and 1
 * raise the level outcome when the clip completes, 3 when the burn timer ends, 2 at once, 4
 * completes without raising it, and 5 has no arm at all and is therefore the inert corpse.
 *
 * 4 is the cause a SCRIPT ordered. The death entry has exactly five callers in the whole image:
 * the death check phase at 00448ECF twice, with 0 when health is at zero and with 4 when the
 * level outcome already reads 5 or more, which is how a failed objective ends a level; the
 * landing check with 1 for a fall faster than 6; the fall timer with 2 after two seconds; and
 * the contact handler's fire code 0x1E with 3. Every cause but 4 is the player's own body dying
 * in the world. That is the difference this module decides on, and it is the only one the entry
 * is given: the record, the health and the outcome cell all read the same either way. */
#define MP_DAMAGE_CAUSE_SCRIPTED 4
#define MP_DAMAGE_CAUSE_FINISHED 5

/* The collision class of the player's own body. Every far body carries one of this feature's own
 * classes instead, so the class is what tells a bank body's death from the player's, and it is
 * read rather than the swapped flag because a bank is active during the content swap tick while
 * that flag is still down. */
#define MP_DAMAGE_LOCAL_BANK_CLASS 1

/* What the death hull does with one call. */
typedef enum mp_damage_death_route {
    MP_DAMAGE_DEATH_RETAIL,     /* run the original and touch nothing: the retail death */
    MP_DAMAGE_DEATH_SURVIVE,    /* run the original, then make the corpse inert */
    MP_DAMAGE_DEATH_NO_LATCH    /* a bank body: the replica, and the original is not run */
} mp_damage_death_route_t;

/* The hull's whole dispatch as one decision, pure so the unit test can drive every arm of it. A
 * body that is not the player's own always takes the replica, whatever the switch says; the
 * player's own body survives only while the switch is on and only when the cause is not the
 * scripted one. */
mp_damage_death_route_t mp_damage_death_route(int32_t bank_class, int32_t cause, bool survives);

/* Whether the player's own death is allowed to leave the level running. Off by default, so an
 * installation with no session behaves exactly as the retail game does; the module cannot see the
 * game mode from here, so whoever knows it sets this. */
void mp_damage_set_survives_death(bool survives);
bool mp_damage_survives_death(void);

/* The provocation's schedule, in substeps of the second body's life: killed once at the first
 * count, revived once that many substeps later. Ten seconds standing, five seconds dead. */
#define MP_DAMAGE_PROVOKE_KILL_AT     320u
#define MP_DAMAGE_PROVOKE_REVIVE_AFTER 160u

typedef enum mp_damage_provoke_action {
    MP_DAMAGE_PROVOKE_NOTHING,
    MP_DAMAGE_PROVOKE_KILL,
    MP_DAMAGE_PROVOKE_REVIVE
} mp_damage_provoke_action_t;

/* The provocation's decision, pure so the unit test can drive its edges: kill exactly once when
 * the count reaches the mark, revive exactly once at the second mark, nothing anywhere else. */
mp_damage_provoke_action_t mp_damage_provoke_decide(uint32_t count, bool killed, bool revived);

/* Resolves the death entry, the clip player, the stand entry and the descriptor cells, then
 * installs the one detour, all or nothing: a missing site refuses before anything is written. */
bool mp_damage_install(void);

bool mp_damage_installed(void);

/* Arms the timed kill-and-revive. Off, the module is only the death hull and the tick below does
 * nothing. */
void mp_damage_enable_provocation(void);

/* One substep of the provocation, called from the tick client after the second body's own tick:
 * counts while the second body exists, kills it on purpose at the mark, revives it at the second
 * mark, and samples the level outcome cell on every substep in between, because the outcome
 * staying 2 through a death is the claim this whole module makes. */
void mp_damage_provoke_tick(void);

void mp_damage_report(const char *why);

#endif /* MULTIPLAYER_MP_DAMAGE_H */
