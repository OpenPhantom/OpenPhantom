/* speaker_rest.h: in a scene, a speaker left frozen after their line goes to their idle.
 *
 * The engine has two dialogue paths and they differ. After a menu line (opcode 0x500) the engine
 * itself drops the speaker's clip latch to 0, the model's stand, the moment the voice ends, so
 * the speaker idles as any character does. After a scripted line (opcode 0x504) it plays the
 * voice and touches no clip; what the script put on stays. A survey of the 2119 scripted lines
 * in the game found 47% followed by an Animation in its play-once mode, which freezes the body
 * on the clip's last frame, 2% ever reaching a looping stand: a speaker frozen mid gesture
 * through the reply and after the exchange is the authored shape, at any frame rate, with or
 * without this project. It was confirmed in the shipped game before this was written.
 *
 * So this applies the menu rule to the scripted path. Everyone who has spoken is watched:
 * whenever their body has sat for half a second on a clip the engine itself has parked, while
 * they are not the one talking, one of two things. The stand itself, put on by the script in
 * its play-once mode, has the freeze bit taken off its track and carries on from where it
 * stopped, no new clip, no crossfade. Anything else, a one-shot gesture or a talk clip the
 * script froze to end the talking, is followed by the stand with the clip's own flags, through
 * the crossfade the script interpreter uses; the talk clip the moment the voice ends, the
 * gesture once its pass is done. The stand is the model's unarmed one where it has one, found
 * by name, else clip 0, the engine's own idle: an armed character's model has the armed stance
 * as clip 0, right in play and wrong in a scene with his hands empty. "Parked" is the engine's
 * own mark, not a guess: the track's clock pinned at the clip's last frame by the freeze bit or
 * the hold bit. A walk, a run, a loop or a gesture still in flight is never at its end for
 * longer than a frame, so a moving body is never touched; a first version tested "complete and
 * not looping" and stopped walking characters, because a walk wraps without a loop flag. A body
 * on its death clip is left down.
 *
 * Not only while the scene lasts. A first version let go of everyone the moment the scene's
 * lock dropped, and the last speaker of every conversation stayed frozen on the look-around the
 * script had put on him as it ended. The parked test is the safety, not the lock; a body is
 * watched for a minute after its last line, wherever the game has gone by then.
 */
#ifndef SPEAKER_REST_H
#define SPEAKER_REST_H

#include <stdbool.h>
#include <stdint.h>

typedef int32_t (__cdecl *speaker_play_clip_fn_t)(uint32_t body, int32_t clip, int32_t mode);

/* `play_clip` is bapobj_playClip. */
void speaker_rest_install(speaker_play_clip_fn_t play_clip);

/* A line spoken on `body` has ended, `track` being the puppet track its base clip is on (0
 * when it does not read): the body is watched for a while, and a talk clip the script put on
 * in its play-once mode is cut with the voice, straight to the stand, as the engine does after
 * a menu line. */
void speaker_rest_note_line_end(uint32_t body, uintptr_t track);

/* Once a frame. `speaker` is the body holding the speaker lock while a voice plays, 0 when
 * none; `body_track` gives the puppet track a body's base clip is on, 0 when it does not read. */
void speaker_rest_on_frame(uint32_t speaker, uintptr_t (*body_track)(uint32_t body));

#endif /* SPEAKER_REST_H */
