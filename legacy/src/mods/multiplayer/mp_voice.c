/* mp_voice.c: the judgement of a spoken line, carried out on this machine. See the header.
 *
 * SIZE NOTE: over 600 lines. The hulls, the judgement of a line and the reading of the engine's
 * answer around it share the flight of one line and its counters, so they stay together. Where the
 * judgement reaches the engine is mp_voice_bind, what the engine holds is read in mp_voice_engine,
 * the life of a voiced line's channel is mp_voice_life, and every line of the log and the report is
 * written in mp_voice_report. The next seam is the scene's tally and the cap, which only read the
 * flight's verdict and write the report's counters.
 */
#include "mp_voice.h"

#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_bridge.h"
#include "mp_cutscene.h"
#include "mp_own_body.h"
#include "mp_range_gate.h"
#include "mp_scene_claim.h"
#include "mp_scene_host.h"
#include "mp_session_now.h"
#include "mp_voice_bind.h"
#include "mp_voice_engine.h"
#include "mp_voice_life.h"
#include "mp_voice_report.h"
#include "multiplayer.h"

#include "common/detour.h"
#include "common/ini.h"
#include "common/memory.h"
#include "common/patch.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The engine's field setter: which field and the value, cdecl. */
typedef void(__cdecl *set_field_fn_t)(int32_t field, float value);

/* The dialogue module's handler: the message, its argument and the frame's time step. */
typedef uint32_t(__cdecl *module_proc_fn_t)(int32_t message, int32_t argument, float dt);

/* The voice: the wav, the channel handle's cell and the place. The body leaves in eax what the
 * name player answered, the channel or -1, and that is carried through. */
typedef int32_t(__cdecl *play_voice_fn_t)(const char *wav, int32_t *handle, const void *place);

/* The frame's 2D message, in which the dialogue module updates and draws the conversation; its arm
 * is the only caller of Dialog_Render and Dialog_Update. */
#define DIALOG_MSG_FRAME_2D 0x15

/* Field 0 of a voice is its volume, field 5 its priority. */
#define FIELD_VOLUME   0
#define FIELD_PRIORITY 5

/* A place handed to the engine has to outlive the call: the channel keeps the pointer and reads
 * through it every frame. Four, like the relay's own anchors. */
#define PLACE_SLOTS 4u

typedef struct flight {
    bool              active;        /* a judgement waits for the call to come back */
    bool              edge;          /* the engine's own test says the line starts */
    bool              placed;        /* the line came with a place */
    bool              from_model;    /* this body was read off its model */
    bool              field_set;     /* field 0 went to nought for the call */
    bool              priority_set;  /* field 5 went up for the call */
    bool              scene;         /* a scene of the host's stood when the line was judged */
    bool              named;         /* its line was written */
    bool              let_go;        /* a client says the host's line: older owners let go after */
    bool              reaches;       /* the call gets past the option and the latch to the handle */
    bool              eye_known;
    float             eye_distance;
    int32_t           lock;
    mp_voice_origin_t origin;
    mp_voice_answer_t answer;
    mp_voice_place_t  place;
    float             source[3];
    mp_voice_call_t   call;          /* what was read around the call */
} flight_t;

typedef struct scene_tally {
    bool                    open;
    uint16_t                serial;
    mp_voice_scene_counts_t n;
} scene_tally_t;

typedef struct voice_state {
    bool installed;

    mp_voice_bindings_t bind;
    set_field_fn_t      set_field;
    uintptr_t           active_cell;
    detour_t            module_hull;
    play_voice_fn_t     play_voice;

    mp_voice_held_t held;
    flight_t        flight;
    float           places[PLACE_SLOTS][3];
    uint32_t        place_at;
    scene_tally_t   scene;
    uint32_t        named_level;   /* lines named since the level began */

    mp_voice_counts_t count;
} voice_state_t;

static voice_state_t voice;

/* ==============================================================================================
 * The one question every part asks first, on both roles.
 * ============================================================================================ */

/* A session the host started and has not ended, read out of the host's setup note by the one
 * reading the scene gate, the movie gate, the doors, the switches and the fog ask as well. It
 * answers on the host and on a client alike, and from the first frame of a level, before a substep
 * of it has run. A session that ended, or a side that holds no note, is answered "no" without
 * anybody releasing anything, and the engine then does what it does in a single player game. */
static bool session_runs(void)
{
    bool runs = false;

    (void)mp_session_now_client_of_a_started_session(&runs, NULL);
    return runs;
}

static bool judging(void)
{
    return voice.bind.bound && session_runs();
}

static float distance(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static const float *stow(const float at[3])
{
    float *slot = voice.places[voice.place_at % PLACE_SLOTS];

    ++voice.place_at;
    memcpy(slot, at, 3u * sizeof(float));
    return slot;
}

/* ==============================================================================================
 * A scene of the host's, told once when it ends here.
 * ============================================================================================ */

static void close_the_scene(void)
{
    if (!voice.scene.open) {
        return;
    }
    mp_voice_report_scene(voice.scene.serial, &voice.scene.n);
    memset(&voice.scene, 0, sizeof voice.scene);
}

static void open_the_scene(uint16_t serial)
{
    if (voice.scene.open && voice.scene.serial == serial) {
        return;
    }
    close_the_scene();
    voice.scene.open   = true;
    voice.scene.serial = serial;
}

/* The falling edge of the one question: the host's scene ends at its own end. A client is in no
 * scene, so there the question never rises. Asked once a frame, and by every judged line in
 * between. */
static void watch_the_scene(void)
{
    mp_scene_known_t known;
    bool             now;

    memset(&known, 0, sizeof known);
    now = mp_scene_host_stands(&known);
    if (!now) {
        close_the_scene();
        return;
    }
    open_the_scene(known.serial);
}

void mp_voice_world_ended(void)
{
    if (!voice.installed) {
        return;
    }
    close_the_scene();
    voice.named_level = 0u;
    /* The level's end stops every channel; a life cut by that is not the line's own. */
    mp_voice_life_drop();
}

/* ==============================================================================================
 * The judgement.
 * ============================================================================================ */

/* A line is the scene's by whose run speaks it: a scene of the host's stands and the script that
 * speaks is a run of the host's (mp_scene_claim). Only a host is ever in a scene, so on a client
 * the scene's clause is off and every line is judged by the radius around this player's own
 * body. */
static void judge(mp_voice_origin_t origin, const float *source)
{
    mp_voice_question_t q;
    mp_scene_known_t    scene;
    size_t              bank;

    memset(&q, 0, sizeof q);
    memset(&scene, 0, sizeof scene);
    voice.flight.origin = origin;
    voice.flight.placed = source != NULL;
    if (source != NULL) {
        q.source_known = true;
        memcpy(q.source, source, sizeof q.source);
        memcpy(voice.flight.source, source, sizeof voice.flight.source);
    }
    q.scene_for_all = mp_scene_host_stands(&scene);
    if (q.scene_for_all) {
        q.scene_speaker = mp_scene_claim_run_is_the_hosts();
        q.gathered      = scene.gathered;
        open_the_scene(scene.serial);
    }
    voice.flight.scene = q.scene_for_all;
    voice.flight.lock  = mp_cutscene_lock_level();
    q.lock_at_scene    = voice.flight.lock >= voice.bind.hearing.lock;
    q.hear_free        = voice.bind.hearing.free;
    q.hear_scene       = voice.bind.hearing.scene;
    q.admit            = voice.bind.hearing.admit;
    q.body_known       = mp_own_body_place(q.body, &voice.flight.from_model);
    /* Only a host's scripts are paced by a line, and it measures the far players where the range
     * gate does, so "near" means the same player in the same place for both. The bridge refreshes
     * the gate only while a peer is joined, so a host whose clients have all left measures nobody
     * rather than the places they last stood at. */
    q.keeps_alive = origin == MP_VOICE_FROM_SCRIPT && mp_armed_is_host();
    if (q.keeps_alive && mp_bridge_joined()) {
        for (bank = 1u; bank <= MP_BANK_FAR_MAX && q.others < MP_VOICE_MAX_OTHERS; ++bank) {
            if (mp_range_gate_player(bank, q.other[q.others])) {
                ++q.others;
            }
        }
    }
    voice.flight.answer = mp_voice_judge(&q);
    voice.flight.active = true;
}

/* Lines of a level are named up to a cap and then only counted; a line said while a scene of the
 * host's stands is always named. */
static bool may_name(void)
{
    if (!voice.flight.scene && voice.named_level >= MP_VOICE_LINES_NAMED) {
        ++voice.count.unnamed;
        return false;
    }
    if (voice.flight.scene) {
        ++voice.count.named_in_scene;
    } else {
        ++voice.named_level;
    }
    ++voice.count.named;
    return true;
}

static void name_the_line(int32_t line_id, bool handed, const mp_voice_heard_answer_t *heard)
{
    const flight_t      *f = &voice.flight;
    mp_voice_line_note_t note;
    char                 wav[56];

    voice.flight.named = may_name();
    if (!voice.flight.named) {
        return;
    }
    wav[0] = '\0';
    if (heard->heard == MP_VOICE_WAV_HELD) {
        mp_voice_engine_wav(&voice.bind.answer.bank, heard->channel, wav, sizeof wav);
    }
    memset(&note, 0, sizeof note);
    note.line         = line_id;
    note.origin       = f->origin;
    note.placed       = f->placed;
    note.answer       = f->answer;
    note.from_model   = f->from_model;
    note.handed       = handed;
    note.place        = f->place;
    note.eye_known    = f->eye_known;
    note.eye_distance = f->eye_distance;
    note.lock         = f->lock;
    note.answer_read  = voice.bind.answer_read;
    note.heard        = *heard;
    note.priority     = f->call.priority;
    note.wav          = wav;
    memcpy(note.source, f->source, sizeof note.source);
    mp_voice_report_line(&note);
}

static void tally_the_scene(const mp_voice_heard_answer_t *heard, bool presented)
{
    mp_voice_scene_counts_t *n = &voice.scene.n;

    ++n->judged;
    n->of_scene += voice.flight.answer.of_scene ? 1u : 0u;
    if (!presented) {
        ++n->withheld;
        return;
    }
    ++n->presented;
    if (heard->heard == MP_VOICE_VOICED) {
        ++n->voiced;
    } else if (heard->heard != MP_VOICE_NOT_ASKED) {
        ++n->refused;
    }
}

static void count_the_verdict(const mp_voice_heard_answer_t *heard)
{
    const mp_voice_answer_t *a         = &voice.flight.answer;
    bool                     presented = a->verdict == MP_VOICE_PRESENTED;

    ++voice.count.judged;
    if (voice.flight.origin == MP_VOICE_FROM_SCRIPT) {
        ++voice.count.by_script;
    } else {
        ++voice.count.by_host;
    }
    voice.count.unknown += a->unknown ? 1u : 0u;
    voice.count.beside_scene += a->beside ? 1u : 0u;
    voice.count.ungathered += a->ungathered ? 1u : 0u;
    if (voice.flight.scene && voice.scene.open) {
        tally_the_scene(heard, presented);
    }
    if (presented) {
        if (a->by_scene) {
            ++voice.count.by_scene;
        } else {
            ++voice.count.presented;
        }
        if (voice.bind.answer_read) {
            ++voice.count.heard[heard->heard];
            voice.count.stole += heard->stole ? 1u : 0u;
        }
        return;
    }
    if (a->verdict == MP_VOICE_SILENT) {
        ++voice.count.silent;
    } else {
        ++voice.count.withheld;
    }
    voice.count.withheld_in_lock +=
        (voice.flight.lock >= voice.bind.hearing.lock && !a->beside) ? 1u : 0u;
}

/* The place and the volume the engine is handed so that its own admission carries out the
 * verdict, and the priority of a presented line. Field 0 only with a place: without one the voice
 * is flat and never reads the field, and neither does it read the priority's. */
static const void *hand_the_voice(const void *position)
{
    mp_voice_voice_t v;
    float            eye[3];
    float            beyond[3];
    const void      *place = position;
    flight_t        *f     = &voice.flight;

    f->eye_known    = mp_voice_engine_eye(voice.bind.cells.eye_cell, eye);
    f->eye_distance = (f->eye_known && f->placed) ? distance(f->source, eye) : 0.0f;
    v = mp_voice_voice_for(f->answer.verdict, f->eye_known, f->eye_distance,
                           voice.bind.cells.reach);
    f->place = v.place;
    if (f->eye_known && v.place == MP_VOICE_AT_EYE) {
        place = stow(eye);
    } else if (f->eye_known && v.place == MP_VOICE_BEYOND_REACH) {
        mp_voice_place_beyond(eye, voice.bind.cells.reach, beyond);
        place = stow(beyond);
    }
    if (v.silent && place != NULL) {
        voice.set_field(FIELD_VOLUME, 0.0f);
        f->field_set = true;
    }
    if (place != NULL && f->answer.verdict == MP_VOICE_PRESENTED && voice.bind.priority_bound) {
        voice.set_field(FIELD_PRIORITY, (float)MP_VOICE_PRESENTED_PRIORITY);
        f->priority_set = true;
    }
    return place;
}

/* ==============================================================================================
 * Around the call: the fields, and the engine's answer.
 * ============================================================================================ */

static void before_the_call(int32_t line_id)
{
    mp_voice_call_t *call = &voice.flight.call;

    if (voice.bind.answer_read) {
        mp_voice_engine_before(&voice.bind.answer, line_id, call);
    }
    call->line           = line_id;
    call->priority_known = voice.bind.priority_bound;
    call->priority       = voice.flight.priority_set ? (uint32_t)MP_VOICE_PRESENTED_PRIORITY
                                                     : (uint32_t)voice.bind.priority.resting;
    /* Asked before the call, while the last line's channel is still what it was, and only for a
     * call that reaches the handle: one the engine turns away at the voice option or the latch
     * leaves the voice that plays, and its life, as they were. The answer is kept for the end of
     * the call, so that the life is neither ended nor begun on two readings. */
    voice.flight.reaches = mp_voice_reaches_the_handle(call);
    if (voice.flight.reaches) {
        mp_voice_life_overtake();
    }
}

/* The resting values on every path, as the engine's own playNameVol and playByName write them
 * back. A field left changed would carry into the next voice that reads it, anywhere in the game.
 * Answers whether the engine reached the voice with field 0 lowered: still at nought after the
 * call means it never did (voices off, the same line twice, no sound), back at rest means it did,
 * with or without a channel. */
static bool put_the_fields_back(void)
{
    float   volume   = 0.0f;
    int32_t priority = 0;
    bool    consumed = false;

    if (voice.flight.field_set) {
        consumed = memory_try_read(voice.bind.cells.volume_cell, &volume, sizeof volume) &&
                   volume != 0.0f;
        voice.set_field(FIELD_VOLUME, MP_VOICE_RESTING_VOLUME);
        ++voice.count.field_lowered;
        if (!memory_try_read(voice.bind.cells.volume_cell, &volume, sizeof volume) ||
            volume == 0.0f) {
            ++voice.count.field_left;
        }
    }
    if (voice.flight.priority_set) {
        voice.set_field(FIELD_PRIORITY, (float)voice.bind.priority.resting);
        ++voice.count.priority_raised;
        if (!memory_try_read(voice.bind.priority.cell, &priority, sizeof priority) ||
            priority != voice.bind.priority.resting) {
            ++voice.count.priority_left;
        }
    }
    return consumed;
}

static mp_voice_heard_answer_t hear_the_engine(bool asked)
{
    mp_voice_heard_answer_t none = { MP_VOICE_NOT_ASKED, -1, false };

    if (!voice.bind.answer_read) {
        return none;
    }
    mp_voice_engine_after(&voice.bind.answer, voice.bind.cells.bark_cell, asked,
                          &voice.flight.call);
    return mp_voice_heard_of(&voice.flight.call);
}

/* A client says a line of the host again at the moment the note arrives, so an older voice can
 * outlast the line the host has already ended, and its end would write -1 into the handle of the
 * line said now. Once the call is back and the handle names the channel the engine gave, every
 * other channel owning the handle lets go of it and plays to its own end. Asked after the call and
 * not before: the engine can still turn the line away at its latch before it touches the handle,
 * and the voice that then keeps the handle must keep its owner, or its end writes nothing and the
 * block never closes. Nothing ends in between, the call runs on this thread. A host only counts
 * what the engine does to it. */
static void let_the_older_voices_go(void)
{
    const mp_voice_call_t *call = &voice.flight.call;

    if (!voice.flight.let_go || !call->handle_known || call->handle < 0) {
        return;
    }
    voice.count.let_go +=
        mp_voice_engine_let_go(&voice.bind.answer.bank, voice.bind.cells.bark_cell, call->handle);
}

static void finish(int32_t line_id, int32_t started)
{
    mp_voice_heard_answer_t heard;
    bool                    consumed;
    bool                    handed = started != 0 && voice.flight.edge;

    if (!voice.flight.active) {
        return;
    }
    consumed = put_the_fields_back();
    voice.count.edge_disagreed += ((started != 0) != voice.flight.edge) ? 1u : 0u;
    heard = hear_the_engine(started != 0);
    let_the_older_voices_go();
    if (mp_voice_adopt(&voice.held, line_id, started != 0, voice.flight.answer.verdict)) {
        count_the_verdict(&heard);
        name_the_line(line_id, handed, &heard);
    }
    if (handed) {
        voice.count.at_eye += voice.flight.place == MP_VOICE_AT_EYE ? 1u : 0u;
        voice.count.beyond += voice.flight.place == MP_VOICE_BEYOND_REACH ? 1u : 0u;
        if (voice.flight.reaches) {
            mp_voice_life_begin(line_id, voice.flight.field_set, consumed, voice.flight.named);
        }
    }
    voice.flight.active = false;
}

/* ==============================================================================================
 * The speak entry and the replay.
 * ============================================================================================ */

const void *mp_voice_line_begin(const void *speaker, int32_t line_id, const void *position)
{
    float       source[3];
    bool        placed;
    const void *place;

    memset(&voice.flight, 0, sizeof voice.flight);
    if (!judging()) {
        return position;
    }
    voice.flight.edge = mp_voice_engine_will_start(&voice.bind.cells, speaker);
    if (!voice.flight.edge && voice.held.known && voice.held.line == line_id) {
        return position;   /* the same line held on: its verdict stands */
    }
    placed = position != NULL &&
             memory_try_read((uintptr_t)position, source, sizeof source);
    judge(MP_VOICE_FROM_SCRIPT, placed ? source : NULL);
    /* Without a start the engine only shows the new line's subtitle, and nothing is handed. */
    if (!voice.flight.edge) {
        return position;
    }
    place = hand_the_voice(position);
    before_the_call(line_id);
    return place;
}

void mp_voice_line_end(int32_t line_id, int32_t started)
{
    finish(line_id, started);
}

mp_voice_replay_t mp_voice_replay_begin(mp_voice_origin_t origin, int32_t line_id,
                                        const float *position)
{
    mp_voice_heard_answer_t none = { MP_VOICE_NOT_ASKED, -1, false };
    mp_voice_replay_t       replay;

    replay.say      = true;
    replay.measured = false;
    replay.place    = position;
    memset(&voice.flight, 0, sizeof voice.flight);
    if (!judging()) {
        return replay;
    }
    /* The replay arms the restart latch before it speaks, so the engine starts every one. */
    voice.flight.edge = true;
    judge(origin, position);
    replay.measured = !voice.flight.answer.unknown;
    if (voice.flight.answer.verdict != MP_VOICE_PRESENTED) {
        count_the_verdict(&none);
        name_the_line(line_id, false, &none);
        voice.flight.active = false;
        replay.say = false;
        return replay;
    }
    replay.place = hand_the_voice(position);
    before_the_call(line_id);
    voice.flight.let_go = voice.bind.answer_read && !mp_armed_is_host();
    return replay;
}

void mp_voice_replay_end(int32_t line_id, int32_t started)
{
    finish(line_id, started);
}

/* ==============================================================================================
 * The camera of a line, the subtitle, and the reply of a dead player.
 * ============================================================================================ */

/* Asked by the scene gates at the speak entry's camera take, inside the call the flight belongs
 * to. Only a host refuses: a client's own script cameras are refused before this is asked. A line
 * of a scene of the host's is presented and keeps its camera; a far line said beside such a scene
 * is refused as it would be outside one. */
static bool camera_of_the_line(void)
{
    if (!voice.flight.active || !session_runs() || !mp_armed_is_host()) {
        return true;
    }
    if (voice.flight.answer.verdict == MP_VOICE_PRESENTED) {
        ++voice.count.cameras_passed;
        return true;
    }
    ++voice.count.cameras_refused;
    return false;
}

static bool block_active(void)
{
    uint32_t active = 1u;

    return voice.active_cell == 0u || !memory_try_read_u32(voice.active_cell, &active) ||
           active != 0u;
}

static bool subtitle_held_back(void)
{
    int32_t shown = 0;
    bool    known = memory_try_read(voice.bind.cells.shown_cell, &shown, sizeof shown);

    return mp_voice_hides_subtitle(&voice.held, known, shown);
}

static void a_subtitle_was_drawn(void)
{
    ++voice.count.subtitles_drawn;
    voice.scene.n.frames += voice.scene.open ? 1u : 0u;
}

/* The subtitle option is set to nought for the one message that draws the subtitle and put back to
 * the value read in the same call, which is the whole of the pair: the lower writes back what the
 * raise found, unconditionally. The option's other readers are the menus, which never run inside
 * this message. A subtitle counts as drawn when the block stood open around the whole message with
 * the option on and nothing held back.
 *
 * engine: u32 dialog_moduleProc(int msg, int arg, f32 dt) */
static uint32_t __cdecl hook_module_proc(int32_t message, int32_t argument, float dt)
{
    module_proc_fn_t original = (module_proc_fn_t)voice.module_hull.original;
    int32_t          option   = 0;
    int32_t          after    = 0;
    bool             option_on;
    uint32_t         answer;

    if (message != DIALOG_MSG_FRAME_2D) {
        return original(message, argument, dt);
    }
    /* The one exit of the verdict held: the session is gone, or the block closed. A channel still
     * being timed when the session goes is dropped rather than counted with a life the session
     * did not see. */
    if (!session_runs()) {
        mp_voice_forget(&voice.held);
        mp_voice_life_drop();
        return original(message, argument, dt);
    }
    watch_the_scene();
    if (!block_active()) {
        mp_voice_forget(&voice.held);
        mp_voice_life_frame(false);
        return original(message, argument, dt);
    }
    mp_voice_life_frame(true);
    option_on = memory_try_read(voice.bind.cells.option_cell, &option, sizeof option) &&
                option != 0;
    if (!option_on || !subtitle_held_back() ||
        !memory_try_write(voice.bind.cells.option_cell, &(int32_t){ 0 }, sizeof(int32_t))) {
        answer = original(message, argument, dt);
        if (option_on && block_active()) {
            a_subtitle_was_drawn();
        }
        return answer;
    }
    answer = original(message, argument, dt);
    (void)memory_try_write(voice.bind.cells.option_cell, &option, sizeof option);
    ++voice.count.frames_withheld;
    if (!memory_try_read(voice.bind.cells.option_cell, &after, sizeof after) || after != option) {
        ++voice.count.option_left;
    }
    return answer;
}

/* The reply is voiced at the player's own body, and with the player dead the engine found none:
 * the place is then the body offset alone, and the voice would read it. Dropped and counted; the
 * handle stays at the -1 the stop of the reply channel left in it, which is what a refused voice
 * leaves as well.
 *
 * engine: void bapsound_playVoice(const char *wavename, i32 *pHandle, const vec3 *pPos) */
static int32_t __cdecl hook_reply_voice(const char *wav, int32_t *handle, const void *place)
{
    if (!session_runs()) {
        return voice.play_voice(wav, handle, place);
    }
    if ((uintptr_t)place == (uintptr_t)voice.bind.reply.body_offset) {
        ++voice.count.replies_dropped;
        return -1;
    }
    ++voice.count.replies_played;
    return voice.play_voice(wav, handle, place);
}

/* ==============================================================================================
 * Installation.
 * ============================================================================================ */

/* Every cell first, the hull last, because it is the one write: all it reads is bound before it
 * can run. The hearing, the answer and the priority fail on their own and say so. */
static const char *bind_the_rule(const char **hear_why, const char **answer_why,
                                 const char **priority_why)
{
    mp_voice_bindings_t *b   = &voice.bind;
    const char          *why = mp_voice_bind_cells(&b->cells);

    if (why != NULL) {
        return why;
    }
    voice.set_field    = (set_field_fn_t)b->cells.set_field;
    /* The text, not ini_read_float: that answers atof, which reads a word as nought. A key that is
     * absent reads as the default's own text and is taken as given. */
    (void)ini_read_string(MULTIPLAYER_SECTION, "VoiceHearingRadiusFactor",
                          MP_VOICE_HEAR_FACTOR_DEFAULT_TEXT, b->factor_text,
                          sizeof b->factor_text);
    b->factor_raw      = mp_voice_hear_number(b->factor_text);
    *hear_why          = mp_voice_bind_radii(&b->cells, b->factor_raw, &b->hear, &b->hearing,
                                             &b->factor_as_given);
    *answer_why        = mp_voice_bind_answer(&b->cells, &b->answer);
    *priority_why      = mp_voice_bind_priority(&b->cells, &b->priority);
    b->answer_read     = *answer_why == NULL;
    b->priority_bound  = *priority_why == NULL;
    mp_voice_life_bind(b);
    if (!detour_install(&voice.module_hull, b->cells.module_proc,
                        (const void *)&hook_module_proc, b->cells.module_prologue)) {
        return "the dialogue module would not take a hull";
    }
    mp_cutscene_set_line_camera(&camera_of_the_line);
    return NULL;
}

/* The target is stored before the operand moves: the hull can be entered the moment it has. */
static const char *guard_the_reply(void)
{
    const char *why = mp_voice_bind_reply(&voice.bind.reply);

    if (why == NULL) {
        voice.play_voice = (play_voice_fn_t)voice.bind.reply.voice;
        if (patch_redirect_call(voice.bind.reply.call, (const void *)&hook_reply_voice) !=
            PATCH_RESULT_OK) {
            why = "the reply call did not move";
        }
    }
    return why;
}

bool mp_voice_install(uintptr_t block_active_cell)
{
    const char *rule_why;
    const char *hear_why     = NULL;
    const char *answer_why   = NULL;
    const char *priority_why = NULL;
    const char *reply_why;

    if (voice.installed) {
        return voice.bind.bound || voice.bind.reply_guarded;
    }
    voice.installed         = true;
    voice.active_cell       = block_active_cell;
    rule_why                = bind_the_rule(&hear_why, &answer_why, &priority_why);
    voice.bind.bound        = rule_why == NULL;
    reply_why               = guard_the_reply();
    voice.bind.reply_guarded = reply_why == NULL;
    mp_voice_report_bound(&voice.bind, rule_why, hear_why, answer_why, priority_why, reply_why);
    return voice.bind.bound || voice.bind.reply_guarded;
}

float mp_voice_reach(void)
{
    return voice.bind.bound ? voice.bind.cells.reach : 0.0f;
}

float mp_voice_hearing_radius(void)
{
    return voice.bind.bound ? voice.bind.hearing.free : 0.0f;
}

/* The cell is the one the engine's own "is somebody talking", Dialog_ActorTalking 0x0043116F,
 * reads while voices are on: the handle of the channel a line is voiced on, below nought for
 * none. A script that waits for a line to end waits on it. */
bool mp_voice_channel_now(int32_t *channel)
{
    return channel != NULL && voice.bind.bound &&
           memory_try_read(voice.bind.cells.bark_cell, channel, sizeof *channel);
}

/* ==============================================================================================
 * The report, written in mp_voice_report.c from what is handed to it here.
 * ============================================================================================ */

void mp_voice_report(void)
{
    if (!voice.installed) {
        return;
    }
    mp_voice_life_counts(&voice.count.life);
    mp_voice_report_write(&voice.count, &voice.bind);
}
