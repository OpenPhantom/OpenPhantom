/* mp_voice_report.c: what the judgement of a spoken line says in the log and the run report. See
 * the header.
 *
 * Split from mp_voice.c along the seam that file's size note named. Everything here reads the
 * counters, the notes and the bindings it is handed and writes nothing, into the engine or into
 * the judgement.
 */
#include "mp_voice_report.h"

#include "common/logging.h"
#include "common/text.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t average(uint64_t sum, uint32_t count)
{
    return count != 0u ? (uint32_t)(sum / count) : 0u;
}

/* ==============================================================================================
 * At the binding.
 * ============================================================================================ */

static void report_the_hearing(const mp_voice_bindings_t *b, const char *why)
{
    const mp_voice_hearing_t *h = &b->hearing;

    if (!b->factor_as_given) {
        log_warning("VoiceHearingRadiusFactor=%s in [multiplayer] is no number from %.1f to %.1f, "
                    "so %.2f is used", b->factor_text, (double)MP_VOICE_HEAR_FACTOR_MIN,
                    (double)MP_VOICE_HEAR_FACTOR_MAX, (double)h->factor);
    }
    if (why != NULL) {
        log_warning("the lines are heard here within the engine's admission of %.1f u: %s, so a "
                    "line is presented as far as the engine plays a voice, and a scene for all "
                    "presents its own lines to a player it gathered", (double)h->admit, why);
        return;
    }
    log_info("the lines are heard here within %.1f u of this body, %.1f u under a scene's lock "
             "(%.2f full-volume radii of a voice, %.1f and %.1f read at %08X and %08X, the lock "
             "level %d at %08X); a host keeps a line alive at no volume while any player stands "
             "within %.1f u, the admission read at %08X and %08X, one number; a scene for all "
             "presents the lines spoken within %.1f u of where it gathers, on the host and on a "
             "client alike, to a player it gathered", (double)h->free, (double)h->scene,
             (double)h->factor, (double)h->min_free, (double)h->min_scene,
             (unsigned)b->hear.free_at, (unsigned)b->hear.scene_at, (int)h->lock,
             (unsigned)b->hear.lock_at, (double)h->admit, (unsigned)b->cells.reach_at[0],
             (unsigned)b->cells.reach_at[1], (double)h->admit);
}

static void report_the_answer(const mp_voice_bindings_t *b, const char *answer_why,
                              const char *priority_why)
{
    const mp_voice_bank_t *bank = &b->answer.bank;

    if (answer_why != NULL) {
        log_warning("the engine's answer to a line is not read here: %s, so a presented line is "
                    "counted as presented and not as heard, and an older voice keeps the handle "
                    "of the line said after it", answer_why);
    } else {
        log_info("the engine's answer to a line is read here: the voice option at %08X and the "
                 "latch against the same line at %08X, each named twice; %u channel(s) of %u "
                 "bytes from %08X, a free one flagged %08X and a playing one %08X at +%u, the "
                 "priority at +%u and the handle of its owner at +%u, named where a channel is "
                 "sought and where it is freed", (unsigned)b->answer.voices_cell,
                 (unsigned)b->answer.latch_cell, (unsigned)bank->count, (unsigned)bank->stride,
                 (unsigned)bank->base, (unsigned)bank->free_bit, (unsigned)bank->playing_bit,
                 (unsigned)bank->flags_at, (unsigned)bank->priority_at, (unsigned)bank->owner_at);
    }
    if (priority_why != NULL) {
        log_warning("a presented voice keeps the priority every voice has: %s, so it is refused a "
                    "channel while every channel is held by a sound of the same rank",
                    priority_why);
        return;
    }
    log_info("a presented voice is handed priority %d through the setter at %08X, one over the "
             "highest a sound record carries, and %d is put back after every call (the cell "
             "named at %08X and %08X)", (int)MP_VOICE_PRESENTED_PRIORITY,
             (unsigned)b->cells.set_field, (int)b->priority.resting,
             (unsigned)b->priority.named_at[0], (unsigned)b->priority.named_at[1]);
}

void mp_voice_report_bound(const mp_voice_bindings_t *bind, const char *rule_why,
                           const char *hear_why, const char *answer_why,
                           const char *priority_why, const char *reply_why)
{
    const mp_voice_cells_t *c = &bind->cells;

    if (rule_why != NULL) {
        log_warning("the lines are not judged here: %s, so every line is heard and read as the "
                    "engine decides, and a client says every line of the host again", rule_why);
    } else {
        report_the_hearing(bind, hear_why);
        log_info("the judgement of a line reaches the engine through four places: the subtitle "
                 "option at %08X, held back in the frame message of the dialogue module at %08X; "
                 "the eye, through the pointer at %08X; field 0 of a voice, through the setter "
                 "at %08X and its cell at %08X; the voice channel of a line, at %08X",
                 (unsigned)c->option_cell, (unsigned)c->module_proc, (unsigned)c->eye_cell,
                 (unsigned)c->set_field, (unsigned)c->volume_cell, (unsigned)c->bark_cell);
        report_the_answer(bind, answer_why, priority_why);
    }
    if (reply_why != NULL) {
        log_warning("the reply of a dead player is voiced as before: %s, so an answer chosen "
                    "while this player is dead is voiced at a place read out of no body",
                    reply_why);
        return;
    }
    log_info("the reply of a dead player is not voiced: the call at %08X that voices the answer "
             "this player chose goes through this machine first, and a reply whose place is the "
             "body offset %u alone, because the engine found no living player, is counted and "
             "dropped", (unsigned)bind->reply.call, (unsigned)bind->reply.body_offset);
}

/* ==============================================================================================
 * A line, its end, and a scene.
 * ============================================================================================ */

static const char *origin_name(mp_voice_origin_t origin)
{
    switch (origin) {
    case MP_VOICE_FROM_HOST:        return "said again for the host";
    case MP_VOICE_FROM_HOST_ANSWER: return "the answer of the host said again";
    case MP_VOICE_FROM_SCRIPT:
    default:                        return "by a script of this machine";
    }
}

/* What follows the verdict: why a scene decided it, or where the voice was handed. */
static const char *extra_of(const mp_voice_line_note_t *n)
{
    if (n->answer.by_scene) {
        return ", because a scene runs for all";
    }
    if (n->answer.ungathered) {
        return ", a line of a scene that did not gather this player";
    }
    if (n->answer.beside) {
        return ", no line of the scene that runs for all";
    }
    if (n->handed && n->place == MP_VOICE_AT_EYE) {
        return ", at the place of the eye";
    }
    return (n->handed && n->place == MP_VOICE_BEYOND_REACH) ? ", put beyond the reach" : "";
}

static const char *verdict_name(mp_voice_verdict_t verdict)
{
    switch (verdict) {
    case MP_VOICE_PRESENTED: return "presented";
    case MP_VOICE_SILENT:    return "kept alive silent";
    case MP_VOICE_WITHHELD:
    default:                 return "withheld";
    }
}

static void heard_text(const mp_voice_line_note_t *n, char *out, size_t size)
{
    const mp_voice_heard_answer_t *a = &n->heard;

    if (!n->answer_read) {
        (void)text_format(out, size, "not read");
    } else if (a->heard == MP_VOICE_VOICED) {
        (void)text_format(out, size, "voiced on channel %d%s", (int)a->channel,
                          a->stole ? ", taken from a lower sound" : "");
    } else if (a->heard == MP_VOICE_LATCHED) {
        (void)text_format(out, size, "refused: the same line was still latched");
    } else if (a->heard == MP_VOICE_VOICES_OFF) {
        (void)text_format(out, size, "refused: voices are off");
    } else if (a->heard == MP_VOICE_NO_CHANNEL) {
        (void)text_format(out, size, "refused: no channel below priority %u",
                          (unsigned)n->priority);
    } else if (a->heard == MP_VOICE_WAV_HELD) {
        (void)text_format(out, size, "refused: its wav or another still held it (%s on channel %d)",
                          n->wav != NULL && n->wav[0] != '\0' ? n->wav : "a wav",
                          (int)a->channel);
    } else if (a->heard == MP_VOICE_REFUSED_OTHER) {
        (void)text_format(out, size, "refused otherwise (no record, a failed load, the sound "
                          "switched off or a place past the admission)");
    } else {
        (void)text_format(out, size, "not asked");
    }
}

void mp_voice_report_line(const mp_voice_line_note_t *n)
{
    char        heard[160];
    char        tail[224];
    const char *origin = origin_name(n->origin);
    const char *extra  = extra_of(n);

    heard_text(n, heard, sizeof heard);
    if (n->placed && n->eye_known) {
        (void)text_format(tail, sizeof tail, "; the eye %.2f u away, lock %d; the engine: %s",
                          (double)n->eye_distance, (int)n->lock, heard);
    } else {
        (void)text_format(tail, sizeof tail, "; lock %d; the engine: %s", (int)n->lock, heard);
    }
    if (!n->placed) {
        log_info("a line was judged here: line %u, %s, with no place, %s%s%s", (unsigned)n->line,
                 origin, verdict_name(n->answer.verdict), extra, tail);
    } else if (n->answer.unknown) {
        log_info("a line was judged here: line %u, %s, at %.2f %.2f %.2f, this body unknown, "
                 "%s%s%s", (unsigned)n->line, origin, (double)n->source[0],
                 (double)n->source[1], (double)n->source[2], verdict_name(n->answer.verdict),
                 extra, tail);
    } else {
        log_info("a line was judged here: line %u, %s, at %.2f %.2f %.2f, this body %.2f u away "
                 "(read off its %s), %s%s%s", (unsigned)n->line, origin, (double)n->source[0],
                 (double)n->source[1], (double)n->source[2], (double)n->answer.distance,
                 n->from_model ? "model" : "block", verdict_name(n->answer.verdict), extra,
                 tail);
    }
}

void mp_voice_report_ending(int32_t line, uint32_t lived_ms, mp_voice_end_t end,
                            bool channel_read)
{
    const char *how;

    switch (end) {
    case MP_VOICE_END_TAKEN:
        how = "its handle was taken by the end of an older voice";
        break;
    case MP_VOICE_END_CLOSED:
        how = "the block closed while it still played";
        break;
    case MP_VOICE_END_NEWER:
        how = "a newer line began while it still played";
        break;
    case MP_VOICE_END_OWN:
    case MP_VOICE_END_PLAYING:
    default:
        how = channel_read ? "at its own end" : "its handle let go, the channel not read";
        break;
    }
    log_info("a line's voice ended here: line %u after %u ms, %s", (unsigned)line,
             (unsigned)lived_ms, how);
}

void mp_voice_report_scene(uint16_t serial, const mp_voice_scene_counts_t *s)
{
    log_info("the lines of a scene that ended here: scene %u, %u judged, %u of the scene, %u "
             "presented, %u voiced, %u refused by the engine, %u withheld; subtitles drawn on %u "
             "frame(s)", (unsigned)serial, (unsigned)s->judged, (unsigned)s->of_scene,
             (unsigned)s->presented, (unsigned)s->voiced, (unsigned)s->refused,
             (unsigned)s->withheld, (unsigned)s->frames);
}

/* ==============================================================================================
 * The report.
 * ============================================================================================ */

static void report_the_lines(const mp_voice_counts_t *c, const mp_voice_bindings_t *b)
{
    const mp_voice_cells_t *cells = &b->cells;

    log_info("  the presentation of the lines: %u judged here (%u by a script of this machine, %u "
             "said again for the host), %u presented (this body within %.1f u), %u kept alive "
             "silent for the script, %u withheld with nobody near, %u with no body or no place "
             "known; %u presented because they were lines of a scene that ran for all, %u withheld "
             "while the lock stood at a scene level and no scene ran for all, %u judged by the "
             "reach while a scene ran for all, being no line of it, %u lines of a scene judged by "
             "the reach because it did not gather this player",
             (unsigned)c->judged, (unsigned)c->by_script, (unsigned)c->by_host,
             (unsigned)c->presented, (double)b->hearing.free, (unsigned)c->silent,
             (unsigned)c->withheld, (unsigned)c->unknown, (unsigned)c->by_scene,
             (unsigned)c->withheld_in_lock, (unsigned)c->beside_scene, (unsigned)c->ungathered);
    log_info("  the voices of the lines: %u given the place of the eye because the eye stood "
             "farther than %.1f u, %u put beyond the reach because the eye stood near a line "
             "nobody here hears; field 0 set to nought %u time(s) and left at nought after a call "
             "%u time(s) (must be 0); subtitles withheld on %u frame(s), the option left changed "
             "after a frame %u time(s) (must be 0); the engine started a line against its own "
             "edge %u time(s) (must be 0); reach %.1f u read at %08X and %08X",
             (unsigned)c->at_eye, (double)cells->reach, (unsigned)c->beyond,
             (unsigned)c->field_lowered, (unsigned)c->field_left, (unsigned)c->frames_withheld,
             (unsigned)c->option_left, (unsigned)c->edge_disagreed, (double)cells->reach,
             (unsigned)cells->reach_at[0], (unsigned)cells->reach_at[1]);
    log_info("  the silent voices: %u kept alive silent, %u of them got no channel all the same "
             "(the engine refused it: a duplicate, a full table, a failed load or the sound "
             "switched off); a silent channel lived %u ms on average and the longest %u ms, "
             "against %u ms on average for a voice heard aloud here, %u heard aloud",
             (unsigned)c->life.silent_played, (unsigned)c->life.silent_no_channel,
             (unsigned)average(c->life.silent_ms, c->life.silent_lives),
             (unsigned)c->life.silent_longest,
             (unsigned)average(c->life.aloud_ms, c->life.aloud_lives),
             (unsigned)c->life.aloud_lives);
}

static void report_the_engine(const mp_voice_counts_t *c, const mp_voice_bindings_t *b)
{
    const uint32_t *h       = c->heard;
    uint32_t        refused = h[MP_VOICE_LATCHED] + h[MP_VOICE_VOICES_OFF] +
                              h[MP_VOICE_NO_CHANNEL] + h[MP_VOICE_WAV_HELD] +
                              h[MP_VOICE_REFUSED_OTHER];

    if (!b->answer_read) {
        log_info("  the engine's answer to the lines presented here: NOT READ, so no line says "
                 "whether it was heard");
    } else {
        log_info("  the engine's answer to the lines presented here: %u voiced, %u refused (%u "
                 "still latched, %u voices off, %u no channel, %u a wav still held, %u other), %u "
                 "not asked; %u handle(s) taken by an older voice's end, %u older voice(s) let go "
                 "of the handle after a line was said again here; subtitles drawn on %u "
                 "frame(s)", (unsigned)h[MP_VOICE_VOICED], (unsigned)refused,
                 (unsigned)h[MP_VOICE_LATCHED], (unsigned)h[MP_VOICE_VOICES_OFF],
                 (unsigned)h[MP_VOICE_NO_CHANNEL], (unsigned)h[MP_VOICE_WAV_HELD],
                 (unsigned)h[MP_VOICE_REFUSED_OTHER], (unsigned)h[MP_VOICE_NOT_ASKED],
                 (unsigned)c->life.handles_taken, (unsigned)c->let_go,
                 (unsigned)c->subtitles_drawn);
    }
    if (!b->priority_bound) {
        log_info("  the priority of a presented voice: NOT RAISED, so it ranks with every other "
                 "voice");
        return;
    }
    log_info("  the priority of a presented voice: %u line(s) handed priority %d, %u line(s) took "
             "a channel from a lower sound; priority %d put back after every call, left changed "
             "after a call %u time(s) (must be 0)", (unsigned)c->priority_raised,
             (int)MP_VOICE_PRESENTED_PRIORITY, (unsigned)c->stole, (int)b->priority.resting,
             (unsigned)c->priority_left);
}

void mp_voice_report_write(const mp_voice_counts_t *count, const mp_voice_bindings_t *bind)
{
    if (bind->bound) {
        report_the_lines(count, bind);
        report_the_engine(count, bind);
        /* On every machine and at nought as well: a client never counts a take here, and a host's
         * nought refused is what a scene of its own has to show. */
        log_info("  the camera of a far line: %u refused on the host, %u passed",
                 (unsigned)count->cameras_refused, (unsigned)count->cameras_passed);
        log_info("  the lines named one by one: %u written (%u of them while a scene ran for all, "
                 "which are never left out), %u left out after the first %u of a level",
                 (unsigned)count->named, (unsigned)count->named_in_scene,
                 (unsigned)count->unnamed, (unsigned)MP_VOICE_LINES_NAMED);
    } else {
        log_info("  the presentation of the lines: NOT JUDGED, so every line is heard and read as "
                 "the engine decides");
    }
    if (bind->reply_guarded) {
        log_info("  the reply voice of this player: %u played, %u not played because this player "
                 "was dead (the engine would have read its place at %08X)",
                 (unsigned)count->replies_played, (unsigned)count->replies_dropped,
                 (unsigned)bind->reply.body_offset);
    } else {
        log_info("  the reply voice of this player: NOT GUARDED, so a reply chosen while this "
                 "player is dead is voiced at a place read out of no body");
    }
}
