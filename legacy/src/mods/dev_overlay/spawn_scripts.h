/* spawn_scripts.h: a behaviour of this project's own for a spawned actor, in the engine's
 * bytecode.
 *
 * A copy of a placement that ran its source's script ran the level's business: a story
 * character's opens his cutscene, a boss's sends him to the marks it names, a companion's
 * follows the player; and the archive's creatures have no script in the level at all. So a
 * spawned copy gets a script written here in the language of the project's level editor,
 * compiled by the editor's compiler into the engine's own AI record
 * (tools/compile_spawn_scripts.py, spawn_script_data.h), and swapped in before the copy's
 * first tick. Four behaviours: stand, which turns to face the player; follow, which walks
 * after them and stops close; attack, which is two records chosen by the model; and help, the
 * same two turned the other way, following the player and going for the nearest class 2 actor,
 * the enemies and the other spawned copies, when one comes within reach. A helper is raised as
 * class 1, the player's own side, so the other helpers are not its enemies and the enemies'
 * fire is aimed at the player, not at it. Attack, in detail, A model
 * with a swing clip gets the hunter the editor proved in the Federation ship, which runs at the
 * player and swings with the calls the Tatooine duel's Maul makes; a model with a fire clip and
 * no swing gets a shooter, which walks to five units and fires its record's first weapon at
 * the player, as the retail guards do; the destroyer droid gets a record of its own, the shape
 * of the game's, which rolls, unfolds behind its shield and fires the twin muzzles. So a boss
 * or an archive creature can fight without the level's script and its marks. Each dies with
 * the model's own death clip and fades eight seconds later.
 *
 * A script plays clips by number, and the numbers are the model's. The records carry
 * placeholders in the slots that hold a clip, and this fills each slot from the actor file the
 * copy wears: the stand is clip 0 in every file; the walk, the run, the two swings and the fire
 * are found by the clip names and the death by its usage mark, read out of the archive. Two
 * numbers are the model's as well: the chase speed, the duel's run for a model with a run clip
 * and a walk for one without, and the reach an attacker closes to before it swings, by the
 * weapon node the archive shows, a sabre, a mount or bare hands. A model with no swing and no
 * fire swings its stand, which lands a hit all the same: the contact is the weapon node's, not
 * the clip's.
 *
 * Internal to dev_overlay; npc_spawner.c prepares one per spawn.
 */
#ifndef DEV_OVERLAY_SPAWN_SCRIPTS_H
#define DEV_OVERLAY_SPAWN_SCRIPTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The behaviours, in the order the panel lists them. */
typedef enum spawn_behaviour {
    SPAWN_BEHAVIOUR_STAND = 0,
    SPAWN_BEHAVIOUR_FOLLOW,
    SPAWN_BEHAVIOUR_ATTACK,
    SPAWN_BEHAVIOUR_HELP,
    SPAWN_BEHAVIOUR_COUNT
} spawn_behaviour_t;

const char *spawn_behaviour_name(spawn_behaviour_t which);

/* Room for the largest record: the engine's record head, the entries, the pool and the state
 * labels; the droideka's is 752 bytes. */
#define SPAWN_SCRIPT_BYTES 1024u

/* Lays the behaviour's script out in `buffer` as the engine's aiScript, with the clip slots
 * filled from `actor_file`'s own clips, and answers the script's address, or NULL when the
 * behaviour is unknown, the buffer is too small, or the file's clips could not be read (the
 * placeholders would then be played as they are, which on a small file is nothing). `note`
 * receives which clips were chosen, for the log. `flies` says the record moves in the air
 * (its move mode is 2 or more), so the copy moves on its hover clip where a walker needs a walk
 * clip. `shoots`, when given, receives whether the record laid out is the shooter, whose
 * cadence the spawner then sets on the record. */
void *spawn_script_prepare(spawn_behaviour_t which, const char *actor_file, bool flies,
                           uint8_t *buffer, size_t size, char *note, size_t note_size,
                           bool *shoots);

/* The move mode an archive model flies with, for a record this project writes itself: the
 * mode the retail levels place the model with (read out of all 22, 2026-09-16), 0 for a
 * walker. A level's own kind keeps its placement's. */
int32_t spawn_script_flyer_mode(const char *actor_file);

/* The shot kind the retail levels arm an archive model with in its first weapon slot, for a
 * record this project writes itself; 0 for a model they never arm, which then gets the
 * blaster. A level's own kind keeps its placement's. */
uint16_t spawn_script_model_weapon(const char *actor_file);

#endif /* DEV_OVERLAY_SPAWN_SCRIPTS_H */
