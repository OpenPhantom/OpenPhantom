/* mp_script_sound.h: the sounds an actor's script plays, heard on every machine they concern.
 *
 * Layer 2. The decisions are mp_script_sound_rule.h, the loops mp_actor_loop.h; this is where
 * they meet the engine: the one call the script's sound opcode makes, repointed while a session is
 * armed, the host's side of it in mp_script_sound.c and a client's in mp_script_sound_client.c.
 *
 * The opcode plays "sound call N at the actor", and it is the only way a script plays a sound. It
 * runs in the actor's script, which runs on the host alone: a client's replica is parked and runs
 * none, so the fall, the burst and the steam of a dead droid and the music a fight turns on were
 * the host's to hear. At the call the host now plays what it played before and says it: a sound
 * played once as a world event at the actor, a loop in the level's state, music as a claim for
 * each peer it is meant for. Its own music it plays only when it is meant for its own player.
 *
 * A client plays what the host said through the engine's own routines, so the volume, the
 * priority, the falloff and the start gate are the engine's: a sound played once at the replica,
 * or at the place the host named when there is no replica; the music in every substep the host
 * holds it, where the engine's latch and gate decide against this machine's own player. A client's
 * own script runs only for an actor the host has not described, and for one it has, its sound,
 * like its every other script output, is the host's to make.
 *
 * With no session every call goes to the engine unchanged.
 */
#ifndef MULTIPLAYER_MP_SCRIPT_SOUND_H
#define MULTIPLAYER_MP_SCRIPT_SOUND_H

#include <stdbool.h>

/* Resolves the sites, checks the call names the play routine and repoints it, hands the loops,
 * the world events and the director their parts. False with a line when anything is missing, and
 * then nothing is repointed and every sound is the engine's alone. */
bool mp_script_sound_install(void);

/* From every substep of a client, behind the replicas the substep built: the music the host holds,
 * and the loops it wants. */
void mp_script_sound_flush(void);

void mp_script_sound_report(void);

#endif /* MULTIPLAYER_MP_SCRIPT_SOUND_H */
