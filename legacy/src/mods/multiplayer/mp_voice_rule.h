/* mp_voice_rule.h: whether this machine sees and hears a spoken line, asked once per line, and
 * what the engine answered when it was handed one.
 *
 * Layer 1, pure. No engine, no address, no socket.
 *
 * The rule: a subtitle and a voice line are seen and heard only by a player who stands near the
 * event, on every machine, the host included. There is one answer to that question, V, and
 * everything else only carries it out:
 *
 *   A line is PRESENTED on this machine when this machine's own body stands within the HEARING
 *   RADIUS of the place it is spoken at. That radius is a number of the engine's own full volume
 *   radii of a voice, read out of the voice's own code: four units outside a scene's lock and eight
 *   under it, so a voice at the radius plays at a quarter of its amplitude with the default factor
 *   of four. Farther out the engine would still play it, fainter and fainter up to its admission of
 *   a hundred units, and a subtitle for a voice nobody can follow is one to keep off the screen.
 *
 *   A scene that runs for everybody presents every line of it to a player it gathered, because
 *   gathering the players is what makes each of them a listener. A line is the scene's when it is
 *   spoken within the engine's admission of the place the players are gathered around. That place
 *   is the one thing of a scene the host and every client know alike: a client is never told who
 *   speaks a line, so the host does not ask either, and the two cannot answer the same line apart.
 *   The gathering is the barrier; the admission only keeps out a line the engine would not voice
 *   from there. A player the scene left where it stood, dead or with no seat, and any line said
 *   beside the scene, are judged by the radius as outside one.
 *
 *   Otherwise a HOST keeps the line alive at no volume while any player stands within the engine's
 *   admission: the script of the host is paced by its voice channel, and a player near the event is
 *   hearing it on his own machine at that very pace. Presented and kept alive are two questions
 *   with two radii now, so no margin is needed where two machines place one player apart: between
 *   the hearing radius and the admission lie some eighty units. With nobody within the admission
 *   the line is WITHHELD, which is what the engine does with a line no listener hears.
 *
 *   What cannot be measured is not shown: no place, no body, or a distance that is not a number.
 *
 * The engine keeps admitting a voice by its own measure, the eye, and mp_voice_voice_for decides
 * which place it is handed so that its answer is V's. That choice never decides whether a line is
 * shown.
 *
 * The second half is the engine's answer, read around the call that hands a line over: whether
 * it voiced the line on a channel or why it refused, and how a line's voice ended. Both are pure
 * readings of what the binding hands in, so that every answer can be walked in a test.
 */
#ifndef MULTIPLAYER_MP_VOICE_RULE_H
#define MULTIPLAYER_MP_VOICE_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The hearing radius in the engine's own full volume radii of a voice. Four puts a voice at the
 * radius a quarter of its amplitude down, 20 log10(4) = 12.04 dB, which is the one perceptual
 * number of the rule and has not been measured: a voice plays at full scale and the median sound
 * bed at 0.7 of it, so a voice nine decibels under the bed is taken as covered. It is the ini's
 * VoiceHearingRadiusFactor so that a calibration run can move it. The range keeps the radius at
 * least the full volume radius itself and at most the admission of a hundred units under a
 * scene's lock of eight; a value outside it, or one that is no number, is read as the default. */
#define MP_VOICE_HEAR_FACTOR_DEFAULT 4.0f
#define MP_VOICE_HEAR_FACTOR_MIN     1.0f
#define MP_VOICE_HEAR_FACTOR_MAX     12.5f

/* The default as the ini's text, what a key that is absent reads as. */
#define MP_VOICE_HEAR_FACTOR_DEFAULT_TEXT "4.0"

/* How far inside the admission the eye has to stand before the engine's own admission is trusted
 * with the line's own place, and how far outside it before its refusal is. The engine measures in
 * the x87 unit and this in single precision; an ulp at a hundred units is under a thousandth of
 * this, and the band only ever decides which place is handed in, never whether a line is shown. */
#define MP_VOICE_EYE_EDGE 0.01f

/* The resting value of field 0, what the engine's own playNameVol writes back after it has set the
 * field for one call. A field this module lowers is always given this back after the call. */
#define MP_VOICE_RESTING_VOLUME 2.0f

/* The priority a presented voice with a place is handed: one over the highest a shipped sound
 * record carries, so that a line this player is meant to hear takes a channel from any footstep,
 * bolt or blade when all twelve are busy, and never loses one to them. The engine only ever takes
 * a channel ranked strictly lower. A voice kept alive silent keeps the engine's own priority: it
 * must not take a channel from a sound somebody hears. */
#define MP_VOICE_PRESENTED_PRIORITY 101

/* A host measures its far players, at most three. */
#define MP_VOICE_MAX_OTHERS 3u

typedef enum mp_voice_verdict {
    MP_VOICE_PRESENTED = 0,   /* seen and heard on this machine */
    MP_VOICE_SILENT,          /* not here, but alive at no volume for the script of this host */
    MP_VOICE_WITHHELD         /* not here, and nobody near: what the engine does with no listener */
} mp_voice_verdict_t;

/* The radii a line is judged by, from the voice's own code and the factor. */
typedef struct mp_voice_hearing {
    float   free;        /* outside a scene's lock */
    float   scene;       /* under it */
    float   admit;       /* the engine's admission of a placed voice, the keep alive radius */
    float   factor;
    float   min_free;    /* the full volume radii the voice pushes */
    float   min_scene;
    int32_t lock;        /* the lock level the voice asks for before it picks one */
} mp_voice_hearing_t;

/* The factor as the ini gave it: a finite number in the range is taken as it stands, and anything
 * else, a value that is no number included, becomes the default. `as_given` says which. */
float mp_voice_hear_factor(float raw, bool *as_given);

/* The number the ini's text holds, blanks around it allowed; NaN for an empty value, a word or a
 * number with a word behind it, which a plain atof would have read as nought or as the number. */
float mp_voice_hear_number(const char *text);

/* The radii out of the two full volume radii the voice pushes and the lock level it asks for.
 * Both radii have to be finite and above nought, the scene's no smaller than the free one, and the
 * lock level the one the scenes are known by; a radius past the admission is held at it. */
bool mp_voice_hear_from(uint32_t min_free_bits, uint32_t min_scene_bits, int32_t lock_read,
                        int32_t lock_expected, float factor, float admit,
                        mp_voice_hearing_t *out);

typedef struct mp_voice_question {
    bool   scene_for_all;      /* a scene runs for everybody on this machine */
    bool   scene_anchor_known;
    float  scene_anchor[3];    /* where that scene gathers its players */
    bool   gathered;           /* this machine's player was gathered by it */
    bool   source_known;
    float  source[3];          /* where the line is spoken */
    bool   body_known;
    float  body[3];            /* where this machine's own body stands */
    float  hear_free;
    float  hear_scene;
    bool   lock_at_scene;      /* this machine's lock stands at the level the voice asks for */
    float  admit;              /* also how far from where a scene gathers a line is its line */
    bool   keeps_alive;        /* this machine hosts: its scripts are paced by the line */
    size_t others;             /* how many rows of `other` carry a far player */
    float  other[MP_VOICE_MAX_OTHERS][3];
} mp_voice_question_t;

typedef struct mp_voice_answer {
    mp_voice_verdict_t verdict;
    bool  by_scene;    /* presented as a line of the scene, to a player it gathered */
    bool  of_scene;    /* a line of the scene that runs for all, gathered or not */
    bool  ungathered;  /* a line of it, judged by the radius because this player was not gathered */
    bool  beside;      /* a scene runs for all and this line is none of it: judged by the radius */
    bool  unknown;     /* this body or the place could not be measured */
    float distance;    /* this body from the place; negative when unknown */
    float hear;        /* the radius the line was judged by */
} mp_voice_answer_t;

/* V. A body exactly at the radius is within it; a distance that is not a number is not. */
mp_voice_answer_t mp_voice_judge(const mp_voice_question_t *question);

/* Where the engine is handed the line's voice. */
typedef enum mp_voice_place {
    MP_VOICE_AT_SOURCE = 0,    /* the line's own place, unchanged */
    MP_VOICE_AT_EYE,           /* the eye's own place, so the engine admits it */
    MP_VOICE_BEYOND_REACH      /* a place past the admission, so the engine refuses it */
} mp_voice_place_t;

typedef struct mp_voice_voice {
    mp_voice_place_t place;
    bool             silent;   /* field 0 at nought for the call */
} mp_voice_voice_t;

/* The place and the volume that make the engine carry out a verdict, given how far the eye stands
 * from the line's place and the admission it measures that against. With the eye unknown a verdict
 * that must not be heard is made silent: the engine then admits or refuses by its own eye, and
 * neither is audible. */
mp_voice_voice_t mp_voice_voice_for(mp_voice_verdict_t verdict, bool eye_known,
                                    float eye_distance, float admit);

/* The place past the admission: straight above the eye, twice the admission up. */
void mp_voice_place_beyond(const float eye[3], float admit, float out[3]);

/* The admission out of the two immediates the voice pushes, the far distance and the range. Both
 * have to name one number, a finite one above nought, or there is none. */
bool mp_voice_reach_from(uint32_t far_bits, uint32_t range_bits, float *reach);

/* The verdict a machine holds for the line on show. A new one is taken at the engine's own edge,
 * when the speak entry started the line, and when the line changed without a start; the same line
 * held on from tick to tick keeps it, so a player who walks away in the middle of a line keeps the
 * line, as in the engines this follows. */
typedef struct mp_voice_held {
    bool               known;
    int32_t            line;
    mp_voice_verdict_t verdict;
} mp_voice_held_t;

/* True when `fresh` became the verdict held. */
bool mp_voice_adopt(mp_voice_held_t *held, int32_t line, bool started, mp_voice_verdict_t fresh);

/* Whether the subtitle on show is held back: a verdict other than presented about that very line.
 * With the line on show unknown, the held verdict alone answers. */
bool mp_voice_hides_subtitle(const mp_voice_held_t *held, bool shown_known, int32_t shown_line);

/* The one exit: the block closed, the level went, the session ended. */
void mp_voice_forget(mp_voice_held_t *held);

/* ==============================================================================================
 * The engine's answer to a line it was handed.
 * ============================================================================================ */

/* The engine keeps twelve; room for a build with more. */
#define MP_VOICE_CHANNELS_MAX 16u

/* One channel of the engine's bank, as far as an answer needs it. */
typedef struct mp_voice_channel {
    bool     busy;       /* not marked free */
    bool     playing;
    uint32_t priority;
    bool     of_line;    /* its owner's handle is the cell a line's channel is kept in */
} mp_voice_channel_t;

typedef enum mp_voice_heard {
    MP_VOICE_NOT_ASKED = 0,   /* no voice was asked for: the line did not start, or was not said */
    MP_VOICE_VOICED,          /* a channel */
    MP_VOICE_LATCHED,         /* refused: the same line was still latched */
    MP_VOICE_VOICES_OFF,      /* refused: voices are off */
    MP_VOICE_NO_CHANNEL,      /* refused: every channel busy, none below the voice's priority */
    MP_VOICE_WAV_HELD,        /* refused: an older voice of a line still held a channel */
    MP_VOICE_REFUSED_OTHER,   /* refused otherwise: no record, a failed load, sound off, place */
    MP_VOICE_HEARD_KINDS
} mp_voice_heard_t;

/* What was read around one call. */
typedef struct mp_voice_call {
    bool     asked;           /* the speak entry started the line, so the voice was asked for */
    bool     voices_known;
    bool     voices_on;       /* the voice option, before the call */
    bool     latch_known;
    int32_t  latch_before;    /* the line the engine's latch held, before and after the call */
    int32_t  latch_after;
    int32_t  line;
    bool     handle_known;
    int32_t  handle;          /* the line's channel handle, after the call */
    bool     priority_known;
    uint32_t priority;        /* the priority the voice was handed */
    bool     free_known;
    size_t   free_before;     /* channels marked free before the call */
    size_t   channels;        /* the bank after the call; 0 when it was not read */
    mp_voice_channel_t channel[MP_VOICE_CHANNELS_MAX];
} mp_voice_call_t;

typedef struct mp_voice_heard_answer {
    mp_voice_heard_t heard;
    int32_t          channel;   /* the channel voiced on, or the one an older voice held; else -1 */
    bool             stole;     /* voiced with no channel free: a lower sound gave its channel up */
} mp_voice_heard_answer_t;

mp_voice_heard_answer_t mp_voice_heard_of(const mp_voice_call_t *call);

/* Whether the call goes past the two gates the engine asks before it writes -1 into the line's
 * handle, the voice option and the latch against the same line, as read before the call. A call
 * that stops at either leaves the handle, and the voice playing on it, as they were. A gate that
 * did not read answers yes. */
bool mp_voice_reaches_the_handle(const mp_voice_call_t *call);

/* How a line's voice ended, told once a frame from its handle and the channel it was voiced on. */
typedef enum mp_voice_end {
    MP_VOICE_END_PLAYING = 0,   /* the handle still names a channel */
    MP_VOICE_END_OWN,           /* at its own end, or the channel was not read */
    MP_VOICE_END_TAKEN,         /* an older voice's end wrote -1 into the handle while it played */
    MP_VOICE_END_CLOSED,        /* the block closed and let the handle go while it played */
    MP_VOICE_END_NEWER          /* a newer line began while it still played */
} mp_voice_end_t;

mp_voice_end_t mp_voice_end_of(bool handle_known, int32_t handle, bool channel_known,
                               const mp_voice_channel_t *channel, bool block_active);

#endif /* MULTIPLAYER_MP_VOICE_RULE_H */
