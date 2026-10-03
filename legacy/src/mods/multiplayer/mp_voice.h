/* mp_voice.h: the one judgement of a spoken line, carried out on this machine.
 *
 * Layer 3. The rule is mp_voice_rule's and is pure; this is where it is asked and where the engine
 * is made to follow it, on the host and on a client alike:
 *
 *   The speak entry's hull asks before every call it passes on. At the engine's own edge, a line
 *   that starts, and when the line changes, the line is judged: this machine's own body from the
 *   same reads the send makes of it, against the hearing radius its own lock level selects; the far
 *   players where the range gate measures them; and, on the host, the one question whether a scene
 *   of the host's stands and whether the line is that scene's, which it is when the script that
 *   speaks it is a run of the host's (mp_scene_claim). A client is in no scene, so there a line is
 *   judged by the radius alone. The engine is then handed the place that makes its own admission
 *   agree with the verdict, field 0 at nought for a line kept alive silent and field 5 raised for
 *   a presented line; after the call both are given their resting values back, on every path.
 *
 *   Around that call the engine's answer is read: whether it voiced the line and on which channel,
 *   or why it refused, and later how that voice ended. A line is named in the log with its answer,
 *   up to a cap each level and always while a scene of the host's stands, and such a scene is
 *   summed up once, when it ends here.
 *
 *   A line of the host said again on a client is judged the same way before it is said, and is not
 *   said at all where this body is not near it. Once the engine has voiced it, an older voice of a
 *   line lets go of the handle it shares with it, so that voice's end cannot cut the new line's
 *   subtitle; a line the engine turned away leaves every voice what it owned.
 *
 *   The subtitle is held back through the option cell for the one message the dialogue module
 *   draws it in, when the verdict held for the line on show is not "presented". The value read is
 *   written back in the same call, so the player's option is never changed: that is the lower of
 *   the pair, and it is unconditional.
 *
 *   The camera of a spoken line is refused on the host where the line is not presented. A scene
 *   of the host's presents every line of it to him, so it keeps its camera; a line a far player's
 *   script speaks while it stands is no line of it and is refused its camera as outside a scene,
 *   unless the host stands within the radius of it.
 *
 *   The answer a player chose is voiced at his own body. With the player dead the engine takes that
 *   place from no body at all and reads address 0x18; the call that voices it goes through here
 *   first, and such a reply is dropped and counted.
 *
 * Nothing here runs without a session: every hull and the repointed call ask the one question,
 * whether the session the host started runs and has not ended, read out of the setup note by
 * mp_session_now as the scene and movie gates read it, and hand the engine its own call when the
 * answer is no. Installed on the session's way in, beside the conversation relay, never when the
 * DLL loads.
 */
#ifndef MULTIPLAYER_MP_VOICE_H
#define MULTIPLAYER_MP_VOICE_H

#include "mp_voice_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum mp_voice_origin {
    MP_VOICE_FROM_SCRIPT = 0,    /* a script of this machine spoke it through the speak entry */
    MP_VOICE_FROM_HOST,          /* a line of the host, said again here */
    MP_VOICE_FROM_HOST_ANSWER    /* the answer the host chose, said again here */
} mp_voice_origin_t;

/* Resolves the voice sites, binds the rule and guards the reply. The block's active cell is handed
 * in by the relay, which reads it out of the answer menu's pattern. Idempotent. True when either
 * half is bound; each half says in its own line whether it is. */
bool mp_voice_install(uintptr_t block_active_cell);

/* The admission read out of the voice's own code, 0 while the rule is not bound. */
float mp_voice_reach(void);

/* The radius a line is heard within outside a scene's lock, 0 while the rule is not bound. */
float mp_voice_hearing_radius(void);

/* The channel a line is voiced on now, below nought for none, for the line that measures a
 * standing scene. False while the rule is not bound or the cell does not read. */
bool mp_voice_channel_now(int32_t *channel);

/* The world a line was said in ended: a level's end, a restart, a savegame restored, the session's
 * start and its end. A scene of the host's still open is summed up, and the lines of the next
 * level are named from the first again. Called from the scene's one exit. */
void mp_voice_world_ended(void);

/* Around the original of the speak entry, from its hull. The place the engine is to be handed
 * comes back; the line's own place when nothing needs changing, and always outside a session. */
const void *mp_voice_line_begin(const void *speaker, int32_t line_id, const void *position);
void        mp_voice_line_end(int32_t line_id, int32_t started);

/* A line of the host, before it is said again here. `position` is the line's place and has to
 * outlive the call, or NULL for a line with no place. */
typedef struct mp_voice_replay {
    bool        say;        /* the line is presented here: say it */
    bool        measured;   /* this body and the place were both known */
    const void *place;      /* where to say it */
} mp_voice_replay_t;

mp_voice_replay_t mp_voice_replay_begin(mp_voice_origin_t origin, int32_t line_id,
                                        const float *position);
void              mp_voice_replay_end(int32_t line_id, int32_t started);

void mp_voice_report(void);

#endif /* MULTIPLAYER_MP_VOICE_H */
