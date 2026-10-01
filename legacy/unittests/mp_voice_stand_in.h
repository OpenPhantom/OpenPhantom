/* unittests/mp_voice_stand_in.h: the stand-in engine and world the tests of mp_voice run against.
 *
 * mp_voice is the layer that reads the engine and hands it what the rule decided, so a test of it
 * needs cells to read and write. They are the stand-in's own: the bindings are answered with cells
 * in the test's memory, a field setter that writes two of them, an eye behind a pointer as the
 * engine keeps it, a bank of twelve channels of 0x80 bytes laid out as the engine's, and a dialogue
 * module of six bytes in a page of its own, which answers the subtitle option it finds while it
 * runs:
 *
 *     A1 xx xx xx xx    mov eax, [option]
 *     C3                ret
 *
 * The first five bytes are the prologue the hull copies, so the trampoline reads the cell and
 * returns. Everything the module asks about the world is answered from the one small world here;
 * the session itself is the real reading of the setup note, mp_session_now, and the bank is read by
 * the real mp_voice_engine, so neither is a stand-in for the thing under test.
 *
 * Two test programs link it, mp_voice and mp_voice_heard: one holds the judgement to the session,
 * the scene, the resting values and the subtitle, the other to the hearing radius and the engine's
 * answer. The log is kept in memory so both can read back what was written.
 */
#ifndef UNITTESTS_MP_VOICE_STAND_IN_H
#define UNITTESTS_MP_VOICE_STAND_IN_H

#include "mp_cutscene.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SI_REACH            100.0f
#define SI_RESTING          2.0f
#define SI_PRIORITY_RESTING 90
#define SI_CHANNELS         12u
#define SI_CHANNEL_BYTES    0x80u
#define SI_FREE_BIT         0x80000u
#define SI_PLAYING_BIT      0x20000u

/* How the stand-in engine answers the speak entry's call. */
typedef struct si_call {
    int32_t starts;              /* the entry's answer: 1 a line started, 0 not */
    bool    holds_the_speaker;   /* the conversation record names this speaker already */
    bool    has_a_record;        /* the book has the line, so the voice is reached */
    int32_t channel;             /* the channel the voice is put on, -1 when it is refused */
} si_call_t;

/* A sound reference as the engine lays one out: flags, then the name. */
typedef struct si_ref {
    uint32_t flags;
    char     name[0x34];
} si_ref_t;

typedef struct stand_in {
    /* The world the module asks about. */
    bool     note_known;      /* the host's setup note has arrived, or this side is the host */
    uint8_t  flags;           /* MP_LOBBY_F_STARTED, MP_LOBBY_F_ENDED */
    bool     is_client;
    bool     joined;          /* the bridge has seen a peer in a substep */
    bool     body_known;
    float    body[3];
    size_t   far_players;     /* 0 or 1, standing at `far_at` */
    float    far_at[3];
    bool     scene_for_all;
    bool     anchor_known;
    float    anchor[3];
    bool     gathered;        /* the scene gathered this player */
    uint16_t serial;
    int32_t  lock;            /* the lock level this machine stands at */
    bool     hearing_refused; /* the radii did not bind */

    /* The engine's cells. */
    float    volume;
    int32_t  priority;
    uint32_t eye_pointer;
    float    eye[3];
    int32_t  option;
    int32_t  shown;
    int32_t  bark;
    uint32_t speaker_held;
    uint32_t restart_latch;
    uint32_t block_active;
    int32_t  voices;
    int32_t  latch;
    uint8_t  bank[SI_CHANNELS * SI_CHANNEL_BYTES];
    si_ref_t ref;

    si_call_t                    call;
    mp_cutscene_line_camera_fn_t line_camera;
    uint8_t                     *module_proc;
} stand_in_t;

extern stand_in_t si;

/* Two speakers: a line of the scene's actor and one of anybody else are asked the same question. */
extern const uint32_t si_actor_body;
extern const uint32_t si_other_body;

/* Where every line of these tests is spoken, and the eye stands on it. */
extern const float SI_LINE_AT[3];

/* The engine answering every call by starting the line and voicing it on channel 5. */
extern const si_call_t SI_ENGINE_PLAYS;

/* Builds the stand-in and installs the module against it. False when it could not. */
bool si_start(void);

/* A fresh world: the session as given, this body far from every line, the engine playing. */
void si_set_the_world(bool note_known, uint8_t flags, bool is_client, bool joined);

/* The bank, every channel free; one channel held by a sound, playing or not. */
void     si_bank_clear(void);
void     si_bank_hold(size_t channel, uint32_t priority, bool playing, const void *owner);
void     si_bank_free(size_t channel);
uint32_t si_bank_owner(size_t channel);

/* What one line through the speak entry left behind. */
typedef struct si_spoken {
    const void *handed;            /* the place the engine was handed */
    float       volume_during;     /* field 0 as the engine reads it inside the call */
    int32_t     priority_during;   /* field 5 likewise */
    bool        camera;            /* the answer to the line's camera take, inside the call */
    float       volume_after;      /* both once the call has come back */
    int32_t     priority_after;
    int32_t     line;
} si_spoken_t;

si_spoken_t si_say(const void *speaker, const float *position);
si_spoken_t si_speak(void);   /* by another speaker, at SI_LINE_AT */

/* The same with the line's number given, so that one line can be said twice. */
si_spoken_t si_say_line(const void *speaker, const float *position, int32_t line);

/* A line of the host said again here, at SI_LINE_AT, the engine answering as it does in si_say.
 * True when it was said. The second form says a given line, so that one line can come twice. */
bool si_say_again(void);
bool si_say_again_line(int32_t line);

/* The number the next line is given, and one the test takes for itself. */
int32_t si_next_line(void);

/* One message into the stand-in dialogue module; it answers the option it saw while it ran. */
uint32_t si_message(int32_t which);

#define SI_FRAME_2D 0x15

/* The log, kept in memory since the last forget. */
bool   si_logged(const char *text);
size_t si_logged_count(const char *text);
void   si_forget_the_log(void);

#endif /* UNITTESTS_MP_VOICE_STAND_IN_H */
