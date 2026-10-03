/* mp_voice_rule.c: whether this machine sees and hears a spoken line, and what the engine answered
 * when it was handed one. The header carries the reasoning. */
#include "mp_voice_rule.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Whether `body` stands within `reach` of `place`, "not farther" on the squares. */
static bool within(const float place[3], const float body[3], float reach)
{
    float dx;
    float dy;
    float dz;

    if (place == NULL || body == NULL || !(reach >= 0.0f)) {
        return false;
    }
    dx = place[0] - body[0];
    dy = place[1] - body[1];
    dz = place[2] - body[2];
    /* "Not farther" written so that a NaN anywhere answers false: every comparison with a NaN is
     * false, and false is the answer that keeps an unmeasured line off the screen. */
    return dx * dx + dy * dy + dz * dz <= reach * reach;
}

static bool anyone_near(const mp_voice_question_t *q, float reach)
{
    size_t row;
    size_t rows = q->others < MP_VOICE_MAX_OTHERS ? q->others : MP_VOICE_MAX_OTHERS;

    if (q->body_known && within(q->source, q->body, reach)) {
        return true;
    }
    for (row = 0; row < rows; ++row) {
        if (within(q->source, q->other[row], reach)) {
            return true;
        }
    }
    return false;
}

static float bits_to_float(uint32_t bits)
{
    float value;

    memcpy(&value, &bits, sizeof value);
    return value;
}

/* ==============================================================================================
 * The hearing radius.
 * ============================================================================================ */

float mp_voice_hear_factor(float raw, bool *as_given)
{
    /* Written so that a NaN fails the test: every comparison with it is false. */
    bool in_range = isfinite(raw) && raw >= MP_VOICE_HEAR_FACTOR_MIN &&
                    raw <= MP_VOICE_HEAR_FACTOR_MAX;

    if (as_given != NULL) {
        *as_given = in_range;
    }
    return in_range ? raw : MP_VOICE_HEAR_FACTOR_DEFAULT;
}

float mp_voice_hear_number(const char *text)
{
    char  *end = NULL;
    double value;

    if (text == NULL) {
        return (float)NAN;
    }
    value = strtod(text, &end);
    if (end == text) {
        return (float)NAN;   /* not a digit to start with: an empty value, or a word */
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    return *end == '\0' ? (float)value : (float)NAN;
}

bool mp_voice_hear_from(uint32_t min_free_bits, uint32_t min_scene_bits, int32_t lock_read,
                        int32_t lock_expected, float factor, float admit,
                        mp_voice_hearing_t *out)
{
    float min_free  = bits_to_float(min_free_bits);
    float min_scene = bits_to_float(min_scene_bits);

    if (out == NULL || !isfinite(min_free) || !(min_free > 0.0f) || !isfinite(min_scene) ||
        !(min_scene >= min_free) || lock_read != lock_expected || !isfinite(factor) ||
        !(factor >= MP_VOICE_HEAR_FACTOR_MIN) || !isfinite(admit) || !(admit > 0.0f)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->min_free  = min_free;
    out->min_scene = min_scene;
    out->factor    = factor;
    out->admit     = admit;
    out->lock      = lock_read;
    out->free      = min_free * factor;
    out->scene     = min_scene * factor;
    /* A hearing radius past the admission would present a line the engine never plays by its
     * own distance; it stops where the engine stops. */
    out->free  = out->free > admit ? admit : out->free;
    out->scene = out->scene > admit ? admit : out->scene;
    return true;
}

/* ==============================================================================================
 * V.
 * ============================================================================================ */

mp_voice_answer_t mp_voice_judge(const mp_voice_question_t *question)
{
    mp_voice_answer_t answer;
    float             dx;
    float             dy;
    float             dz;

    memset(&answer, 0, sizeof answer);
    answer.verdict  = MP_VOICE_WITHHELD;
    answer.distance = -1.0f;
    if (question == NULL) {
        answer.unknown = true;
        return answer;
    }
    /* The engine asks the lock in the same call that pushes the radius, so this machine asks its
     * own lock: a host in a scene of his own stands at the scene's level, a client never does. */
    answer.hear = question->lock_at_scene ? question->hear_scene : question->hear_free;
    if (question->source_known && question->body_known) {
        dx = question->source[0] - question->body[0];
        dy = question->source[1] - question->body[1];
        dz = question->source[2] - question->body[2];
        answer.distance = sqrtf(dx * dx + dy * dy + dz * dz);
    }
    /* A distance that is not a number is a body that cannot be measured, the same as none. */
    answer.unknown = !question->source_known || !question->body_known ||
                     !(answer.distance >= 0.0f);
    if (answer.unknown) {
        answer.distance = -1.0f;
    }

    /* A line of the scene that stands is one a run of the host's speaks. It is asked of the
     * speaker's run and not of the place: a far player's conversation beside the scene is spoken
     * at the scene's place and is none of it, and an actor of the scene speaks its lines from
     * wherever its script walks it. */
    answer.of_scene = question->scene_for_all && question->scene_speaker;
    if (answer.of_scene && question->gathered) {
        answer.verdict  = MP_VOICE_PRESENTED;
        answer.by_scene = true;
        return answer;
    }
    answer.ungathered = answer.of_scene;
    answer.beside     = question->scene_for_all && !answer.of_scene;
    if (!question->source_known) {
        return answer;   /* a line with no place is near nobody */
    }
    if (!answer.unknown && within(question->source, question->body, answer.hear)) {
        answer.verdict = MP_VOICE_PRESENTED;
        return answer;
    }
    if (question->keeps_alive && anyone_near(question, question->admit)) {
        answer.verdict = MP_VOICE_SILENT;
    }
    return answer;
}

mp_voice_voice_t mp_voice_voice_for(mp_voice_verdict_t verdict, bool eye_known,
                                    float eye_distance, float admit)
{
    mp_voice_voice_t voice;

    voice.place  = MP_VOICE_AT_SOURCE;
    voice.silent = false;
    switch (verdict) {
    case MP_VOICE_PRESENTED:
        /* The engine admits by the eye. Where the eye stands inside the admission by more than
         * the band the line keeps its own place and rolls off as every voice does; anywhere else
         * the eye's own place is the one the engine cannot refuse. */
        if (eye_known && !(eye_distance <= admit - MP_VOICE_EYE_EDGE)) {
            voice.place = MP_VOICE_AT_EYE;
        }
        break;
    case MP_VOICE_SILENT:
        voice.silent = true;
        if (eye_known) {
            voice.place = MP_VOICE_AT_EYE;
        }
        break;
    case MP_VOICE_WITHHELD:
    default:
        if (!eye_known) {
            /* Neither an admission nor a refusal by an eye that did not read is heard. */
            voice.silent = true;
        } else if (!(eye_distance > admit + MP_VOICE_EYE_EDGE)) {
            voice.place = MP_VOICE_BEYOND_REACH;
        }
        break;
    }
    return voice;
}

void mp_voice_place_beyond(const float eye[3], float admit, float out[3])
{
    out[0] = eye[0];
    out[1] = eye[1];
    out[2] = eye[2] + 2.0f * admit;
}

bool mp_voice_reach_from(uint32_t far_bits, uint32_t range_bits, float *reach)
{
    float value;

    if (reach == NULL || far_bits != range_bits) {
        return false;
    }
    value = bits_to_float(far_bits);
    if (!isfinite(value) || !(value > 0.0f)) {
        return false;
    }
    *reach = value;
    return true;
}

bool mp_voice_adopt(mp_voice_held_t *held, int32_t line, bool started, mp_voice_verdict_t fresh)
{
    if (held == NULL) {
        return false;
    }
    if (held->known && held->line == line && !started) {
        return false;
    }
    held->known   = true;
    held->line    = line;
    held->verdict = fresh;
    return true;
}

bool mp_voice_hides_subtitle(const mp_voice_held_t *held, bool shown_known, int32_t shown_line)
{
    if (held == NULL || !held->known || held->verdict == MP_VOICE_PRESENTED) {
        return false;
    }
    return !shown_known || shown_line == held->line;
}

void mp_voice_forget(mp_voice_held_t *held)
{
    if (held != NULL) {
        memset(held, 0, sizeof *held);
    }
}

/* ==============================================================================================
 * The engine's answer.
 * ============================================================================================ */

/* Every channel busy and none of them strictly below the voice's priority: the engine's search
 * takes a free channel first and otherwise only one ranked strictly lower. With the voice's own
 * priority unknown, a bank with no free channel is all that can be said. */
static bool no_channel_below(const mp_voice_call_t *call)
{
    size_t count = call->channels < MP_VOICE_CHANNELS_MAX ? call->channels : MP_VOICE_CHANNELS_MAX;
    size_t index;

    if (count == 0u) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        if (!call->channel[index].busy ||
            (call->priority_known && call->channel[index].priority < call->priority)) {
            return false;
        }
    }
    return true;
}

static int32_t older_voice(const mp_voice_call_t *call)
{
    size_t count = call->channels < MP_VOICE_CHANNELS_MAX ? call->channels : MP_VOICE_CHANNELS_MAX;
    size_t index;

    for (index = 0; index < count; ++index) {
        if (call->channel[index].busy && call->channel[index].of_line) {
            return (int32_t)index;
        }
    }
    return -1;
}

static bool voices_off(const mp_voice_call_t *call)
{
    return call->voices_known && !call->voices_on;
}

bool mp_voice_reaches_the_handle(const mp_voice_call_t *call)
{
    if (call == NULL) {
        return true;
    }
    return !voices_off(call) && !(call->latch_known && call->latch_before == call->line);
}

/* The order is the engine's own: the voice option is asked first, then the latch, then the voice
 * itself, which answers with a channel or -1. */
mp_voice_heard_answer_t mp_voice_heard_of(const mp_voice_call_t *call)
{
    mp_voice_heard_answer_t answer;

    answer.heard   = MP_VOICE_NOT_ASKED;
    answer.channel = -1;
    answer.stole   = false;
    if (call == NULL || !call->asked) {
        return answer;
    }
    if (!mp_voice_reaches_the_handle(call)) {
        answer.heard = voices_off(call) ? MP_VOICE_VOICES_OFF : MP_VOICE_LATCHED;
        return answer;
    }
    if (call->handle_known && call->handle >= 0) {
        answer.heard   = MP_VOICE_VOICED;
        answer.channel = call->handle;
        answer.stole   = call->free_known && call->free_before == 0u;
        return answer;
    }
    answer.heard = MP_VOICE_REFUSED_OTHER;
    /* A latch the call did not move means the engine left before it: no record for the line. */
    if (call->latch_known && call->latch_after != call->line) {
        return answer;
    }
    if (no_channel_below(call)) {
        answer.heard = MP_VOICE_NO_CHANNEL;
        return answer;
    }
    answer.channel = older_voice(call);
    if (answer.channel >= 0) {
        answer.heard = MP_VOICE_WAV_HELD;
    }
    return answer;
}

mp_voice_end_t mp_voice_end_of(bool handle_known, int32_t handle, bool channel_known,
                               const mp_voice_channel_t *channel, bool block_active)
{
    if (handle_known && handle >= 0) {
        return MP_VOICE_END_PLAYING;
    }
    /* The handle is gone while the channel it named still plays for a line: something other than
     * its own end wrote -1 into the cell. With the block open that is an older voice's end, whose
     * owner pointer named the same cell; with the block shut it is the close letting it go. */
    if (channel_known && channel != NULL && channel->busy && channel->playing && channel->of_line) {
        return block_active ? MP_VOICE_END_TAKEN : MP_VOICE_END_CLOSED;
    }
    return MP_VOICE_END_OWN;
}
