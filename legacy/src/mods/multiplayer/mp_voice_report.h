/* mp_voice_report.h: what the judgement of a spoken line says in the log and the run report.
 *
 * The counters are counted in mp_voice.c and printed in mp_voice_report.c. This header is the one
 * record the two agree on, and the report is handed it together with the bindings it quotes, so
 * the judgement's own state stays private to its file. The lines written once at the binding, once
 * a line and once a scene are here as well: each is handed what it prints and reads nothing else.
 */
#ifndef MULTIPLAYER_MP_VOICE_REPORT_H
#define MULTIPLAYER_MP_VOICE_REPORT_H

#include "mp_voice.h"
#include "mp_voice_bind.h"
#include "mp_voice_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many lines of a level are named one by one before they are only counted. A line said
 * while a scene of the host's stands is always named: a scene is where a line is most wanted
 * and least often said. */
#define MP_VOICE_LINES_NAMED 64u

/* Everything the judgement bound, as the report quotes it. */
typedef struct mp_voice_bindings {
    bool                      bound;           /* the rule, with every cell it reads */
    mp_voice_cells_t          cells;
    mp_voice_hear_cells_t     hear;            /* where the radii were read */
    mp_voice_hearing_t        hearing;         /* the radii judged by, read or the fallback */
    bool                      factor_as_given;
    float                     factor_raw;      /* NaN when the ini's text held no number */
    char                      factor_text[32]; /* as read, cut to 31; the warning quotes it */
    bool                      answer_read;     /* the engine's answer to a line */
    mp_voice_answer_cells_t   answer;
    bool                      priority_bound;  /* the priority of a presented voice */
    mp_voice_priority_cells_t priority;
    bool                      reply_guarded;
    mp_voice_reply_site_t     reply;
} mp_voice_bindings_t;

/* The lives of the channels lines were voiced on, counted where they are timed. */
typedef struct mp_voice_life_counts {
    uint32_t silent_played;
    uint32_t silent_no_channel;
    uint32_t silent_lives;
    uint32_t silent_longest;       /* ms */
    uint64_t silent_ms;
    uint32_t aloud_lives;
    uint64_t aloud_ms;
    uint32_t handles_taken;        /* by an older voice's end */
} mp_voice_life_counts_t;

/* What the report says, in the order it says it. */
typedef struct mp_voice_counts {
    uint32_t judged;
    uint32_t by_script;
    uint32_t by_host;
    uint32_t presented;
    uint32_t by_scene;
    uint32_t ungathered;
    uint32_t beside_scene;
    uint32_t silent;
    uint32_t withheld;
    uint32_t unknown;
    uint32_t withheld_in_lock;
    uint32_t at_eye;
    uint32_t beyond;
    uint32_t field_lowered;
    uint32_t field_left;           /* must stay 0 */
    uint32_t frames_withheld;
    uint32_t option_left;          /* must stay 0 */
    uint32_t edge_disagreed;       /* must stay 0 */
    mp_voice_life_counts_t life;
    uint32_t cameras_refused;
    uint32_t cameras_passed;
    uint32_t replies_played;
    uint32_t replies_dropped;
    uint32_t heard[MP_VOICE_HEARD_KINDS];   /* the engine's answer to a presented line */
    uint32_t let_go;               /* older voices let go of the handle after a line said again */
    uint32_t subtitles_drawn;      /* frames */
    uint32_t priority_raised;
    uint32_t stole;                /* a presented line took the channel of a lower sound */
    uint32_t priority_left;        /* must stay 0 */
    uint32_t named;
    uint32_t named_in_scene;
    uint32_t unnamed;
} mp_voice_counts_t;

/* One scene of the host's, from the first line or frame it was seen running to its end here. */
typedef struct mp_voice_scene_counts {
    uint32_t judged;
    uint32_t of_scene;
    uint32_t presented;
    uint32_t voiced;
    uint32_t refused;
    uint32_t withheld;
    uint32_t frames;
} mp_voice_scene_counts_t;

/* One judged line, as its line in the log says it. */
typedef struct mp_voice_line_note {
    int32_t                 line;
    mp_voice_origin_t       origin;        /* who said it */
    bool                    placed;
    float                   source[3];
    mp_voice_answer_t       answer;
    bool                    from_model;    /* this body was read off its model */
    bool                    handed;        /* the voice was handed over, at `place` */
    mp_voice_place_t        place;
    bool                    eye_known;
    float                   eye_distance;
    int32_t                 lock;
    bool                    answer_read;   /* the engine's answer was read at all */
    mp_voice_heard_answer_t heard;
    uint32_t                priority;
    const char             *wav;           /* the wav of the channel a refusal names */
} mp_voice_line_note_t;

/* The lines written once, when the judgement binds: the rule or why not, the hearing radius, the
 * engine's answer, the priority and the reply's guard. A NULL reason is a binding that took. */
void mp_voice_report_bound(const mp_voice_bindings_t *bind, const char *rule_why,
                           const char *hear_why, const char *answer_why,
                           const char *priority_why, const char *reply_why);

void mp_voice_report_line(const mp_voice_line_note_t *note);

/* How a named line's voice ended. */
void mp_voice_report_ending(int32_t line, uint32_t lived_ms, mp_voice_end_t end,
                            bool channel_read);

/* One scene of the host's ended here. A client is in no scene and writes none. */
void mp_voice_report_scene(uint16_t serial, const mp_voice_scene_counts_t *scene);

/* The report's lines: how the lines were presented and heard when the rule bound, and the reply
 * voice of this player when its call was guarded, each with the line that says it was not. */
void mp_voice_report_write(const mp_voice_counts_t *count, const mp_voice_bindings_t *bind);

#endif /* MULTIPLAYER_MP_VOICE_REPORT_H */
