/* mp_dialog_relay.c: the hull on "say this line", and the replay on the far side.
 *
 * The header carries the reasoning. What is here is the edge that keeps the channel quiet, the
 * anchor storage the voice needs to outlive the call, the judgement of each line around the call
 * and around the replay, the host's watch on its own answer, and the counters.
 */
#include "mp_dialog_relay.h"

#include "mp_armed.h"
#include "mp_dialog.h"
#include "mp_enemy_spawn.h"
#include "mp_session_now.h"
#include "mp_signatures_dialog.h"
#include "mp_voice.h"
#include "mp_world_event.h"

#include "common/detour.h"
#include "common/signature.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int32_t(__cdecl *speak_single_fn_t)(void *speaker, int32_t camera_group, int32_t line_id,
                                            const void *position);
typedef void(__cdecl *force_restart_fn_t)(void *speaker);

/* The answer rows, byte-proven out of Dialog_AddChoice's own operands and immediates: the array
 * base and the row count are the two masked absolutes, the stride is its `shl eax,4` and the cap
 * its `cmp ...,9`, and both of those are required bytes of the pattern. The field offsets are the
 * engine's own dlgChoice, and only two of the four are read here. */
#define CHOICE_STRIDE     0x10u
#define CHOICE_SELECTED   0x00u   /* raised 0.25 s after a commit; THIS is what the script polls */
#define CHOICE_LINE_ID    0x0Cu

/* How many anchors are kept, and why any are kept at all.
 *
 * The replayed voice is started with a pointer to a place, and the channel does not always copy
 * it: `bapsound_startChannel` copies the point during the call only for a record that carries
 * SNDF_STATIC_POS, and a record without it keeps the pointer and reads through it every frame so
 * that the sound can follow a body. That bit is set in 0 of the 2424 shipped sound records, so
 * the channel keeps the pointer and the place has to outlive the call.
 *
 * Four, in a ring, rather than one: one would be correct only while a new line always stops the
 * previous voice, and that is a property of the engine this module would then depend on without
 * having proven it. Four triples is forty eight bytes and the question does not arise. */
#define ANCHOR_SLOTS 4u

/* Where an actor keeps its position. Both of the engine's calls of the speak entry, the statement
 * and the menu of a script, hand the actor's BODY as the speaker and this position as the place,
 * so the actor is the place less this offset. */
#define ACTOR_POS 0xD0u

typedef struct mp_dialog_relay {
    bool     installed;
    bool     host;
    bool     replaying;   /* this side is saying a far line again; its own hull must stand aside */

    detour_t speak_hull;
    force_restart_fn_t force_restart;
    mp_dialog_relay_place_fn_t far_place;   /* where the far player's body stands here */

    /* The block's own cells, read out of the add-choice pattern's operands rather than written
     * down; the host watches them for the row the engine marked chosen. 0 when the pattern did
     * not resolve, and then no answer is watched. */
    uintptr_t active_cell;
    uintptr_t count_cell;
    uintptr_t choice_cell;

    mp_dialog_relay_send_fn_t send;

    /* The last line this side put on the wire, so a held line is not sent on every tick. */
    uint16_t last_line;
    bool     last_line_known;

    /* The answer told for the menu now open, so the flag staying up does not send it again. */
    uint16_t sent_pick;
    bool     sent_pick_known;

    float    anchor[ANCHOR_SLOTS][3];
    uint32_t anchor_at;

    /* The hull, both roles. On the host `spoken` is what was described; on a client it is what
     * a local script said, which is the witness that no script there should have spoken, and
     * `withheld` what a local script of an actor whose life the host describes asked for. */
    uint32_t spoken;
    uint32_t withheld;
    uint32_t sent;
    uint32_t unsent;         /* the channel refused it */
    uint32_t held;           /* a repeat of a line already sent, deliberately not sent again */
    uint32_t no_level;       /* spoken with no level identity, so nothing could be addressed */
    uint32_t refused_encode; /* an id past the book, or a place the fixed point will not hold */
    uint32_t picks_sent;     /* the host's answers, one per menu */
    uint32_t picks_refused;  /* a pick that reached the host, which is never answered */

    /* A client's side. A line that cannot be measured and is not said is counted by the judgement
     * of the lines, not here. */
    uint32_t taken;
    uint32_t applied;
    uint32_t out_of_earshot; /* held back: this body stands too far from where it was spoken */
    uint32_t unmeasured;     /* said with no distance to measure */
    uint32_t elsewhere;      /* a line about a level this side is not in */
    uint32_t torn;
    uint32_t no_level_here;  /* arrived while this side had no level open */
    uint32_t picks_taken;
    uint32_t replies_heard;          /* the host's answer, said again here at the far body */
    uint32_t replies_unplaced;       /* of those, said with no far body known */
    uint32_t replies_out_of_earshot;
} mp_dialog_relay_t;

static mp_dialog_relay_t dialog;

/* ==============================================================================================
 * The hull. It runs on both roles; only the host describes.
 * ============================================================================================ */

static void describe(int32_t line_id, const void *position, int32_t started)
{
    mp_dialog_line_t line;
    uint8_t          note[MP_DIALOG_BYTES];
    size_t           bytes;

    if (dialog.send == NULL) {
        return;
    }
    /* THE EDGE. The 0x500 worker calls the entry again on every tick a line is held, and the entry
     * answers 0 for those because the speaker did not change. Sending only a line that STARTED, or
     * one whose id differs from the last, turns a conversation into one note per line instead of
     * one per tick. The engine's own answer is the edge; this module does not invent one. */
    if (started == 0 && dialog.last_line_known && (uint16_t)line_id == dialog.last_line) {
        ++dialog.held;
        return;
    }
    memset(&line, 0, sizeof line);
    if (!mp_enemy_spawn_level_identity(&line.level)) {
        ++dialog.no_level;
        return;
    }
    line.line = (uint16_t)line_id;
    if (position != NULL) {
        line.has_position = true;
        memcpy(line.position, position, sizeof line.position);
    }
    bytes = mp_dialog_encode(&line, note, sizeof note);
    if (bytes == 0u) {
        ++dialog.refused_encode;
        return;
    }
    dialog.last_line       = (uint16_t)line_id;
    dialog.last_line_known = true;
    if (dialog.send(note, bytes)) {
        ++dialog.sent;
    } else {
        ++dialog.unsent;
    }
}

/* engine: int Dialog_SpeakSingle(void *pSpeaker, int cameraGroup, i32 lineId, const void *pos) */
static int32_t __cdecl hook_speak_single(void *speaker, int32_t camera_group, int32_t line_id,
                                         const void *position)
{
    speak_single_fn_t original = (speak_single_fn_t)dialog.speak_hull.original;
    const void       *place;
    int32_t           started;

    if (dialog.replaying) {
        /* A far line said again here: the replay judged it and handed the place itself. */
        return original(speaker, camera_group, line_id, position);
    }
    /* A client's own line of an actor whose life the host describes is the host's: the host says
     * it and it arrives through the relay, so saying it here as well would say it twice. The one
     * question finds the actor only among the replicas this side holds, so a place that is no
     * actor's never withholds anything. The answer is the engine's own for a line not started. */
    if (mp_world_event_output_is_the_hosts(
            position != NULL ? (uintptr_t)position - ACTOR_POS : 0u,
            mp_session_now_client_of_a_started_session(NULL, NULL))) {
        ++dialog.withheld;
        return 0;
    }
    /* The line is judged around the call: before it, because the place and the volume the engine
     * is handed decide what it admits, and after it at the engine's own answer. The original and
     * that answer are kept: it is the caller's pacing gate, and it is also the edge this module
     * sends on. A hull that invented either would be guessing at both. */
    place   = mp_voice_line_begin(speaker, line_id, position);
    started = original(speaker, camera_group, line_id, place);
    mp_voice_line_end(line_id, started);
    ++dialog.spoken;
    /* ONE DIRECTION. Only the host's conversations travel; a line a local script speaks on a
     * client is counted above and goes nowhere. See the header for why sending it would be worse
     * than dropping it. Such lines did exist: before the use press was latched on a client, an
     * actor the host had not reached yet ran its own script there, and one run opened five
     * conversations of the client's own that way. The note carries the line's own place, never
     * the one handed over. */
    if (dialog.host && mp_armed_transport()) {
        describe(line_id, position, started);
    }
    return started;
}

/* ==============================================================================================
 * The replay, where the line is presented here.
 * ============================================================================================ */

static const float *stow_anchor(const mp_dialog_line_t *line)
{
    float *slot;

    if (!line->has_position) {
        return NULL;
    }
    slot = dialog.anchor[dialog.anchor_at % ANCHOR_SLOTS];
    ++dialog.anchor_at;
    memcpy(slot, line->position, sizeof line->position);
    return slot;
}

/* Says a far line again here, where the judgement of the line presents it on this machine. True
 * when it was said; `measured` says whether this body and the place were both known. */
static bool replay(mp_voice_origin_t origin, const mp_dialog_line_t *line, bool *measured)
{
    speak_single_fn_t speak  = (speak_single_fn_t)dialog.speak_hull.original;
    const float      *anchor = stow_anchor(line);
    mp_voice_replay_t judged = mp_voice_replay_begin(origin, (int32_t)line->line, anchor);
    int32_t           started;

    /* The reach is measured from this body. The engine's voice gate measures a placed voice from
     * the camera eye instead, view+0x24, because the voice is started with flags 0x4A04 and the
     * 3D bit in there picks the eye as the listener. With the follow camera the two are about 3.6
     * units apart, the default offset (0, -2.5, 3.0) with the behind component scaled by the
     * cosine of the pitch; in a fixed lookat camera region (29 records) or a fixed world region
     * (19 of 176) the eye is parked at an authored point and the gap is the size of the room. */
    *measured = judged.measured;
    if (!judged.say) {
        return false;
    }
    dialog.replaying = true;
    /* The restart latch first. The speaker stays NULL, a far machine's actor pointer is not a
     * pointer here and the reply path would read a float through it, and NULL against the NULL
     * already held is what makes the latch arm, so every replayed line plays its voice instead of
     * only refreshing the subtitle. The latch at 00430E69 is four instructions:
     *
     *     A1 abs32           mov eax,[g_dlg.pSpeakerLock]
     *     3B 45 08           cmp eax,[ebp+8]
     *     75 0A              jne over
     *     C7 05 abs32 01     mov [g_dlg.bForceRestart],1
     *
     * so it arms exactly when the speaker handed in matches the one held. */
    dialog.force_restart(NULL);
    /* Camera group -1: a scene camera is the host's answer to where the HOST is standing, and
     * taking a second player's view away for a conversation they may be nowhere near is worse
     * than letting them keep it. */
    started = speak(NULL, -1, (int32_t)line->line, judged.place);
    dialog.replaying = false;
    mp_voice_replay_end((int32_t)line->line, started);
    return true;
}

/* ==============================================================================================
 * The host's answer, heard and read at the far body.
 *
 * The host plays its own reply through Dialog_PlayVoice in mode 1: at its own body, with the talk
 * gesture, on the reply channel. None of that is this machine's to repeat, but the line is, and it
 * was the one thing of a conversation a client could not hear. It goes through the same replay as
 * an NPC's line, at the place the host's body stands here, under the same judgement. On a client
 * no script polls the block, so the row count the speak entry clears is nobody's loss.
 *
 * The first form played it through Dialog_PlayVoice in mode 0 as a fourth resolved site, to
 * spare that row count. The count matters only on a machine whose script polls the block, which
 * is the host and never a client, so the simpler path replaced it and the fourth site is gone.
 * ============================================================================================ */

static void hear_the_reply(uint16_t level, uint16_t line)
{
    mp_dialog_line_t said;
    bool             measured = false;

    memset(&said, 0, sizeof said);
    said.level = level;
    said.line  = line;
    said.has_position = dialog.far_place != NULL && dialog.far_place(said.position);
    if (replay(MP_VOICE_FROM_HOST_ANSWER, &said, &measured)) {
        ++dialog.replies_heard;
        dialog.replies_unplaced += said.has_position ? 0u : 1u;
    } else if (measured) {
        ++dialog.replies_out_of_earshot;
    }
}

/* ==============================================================================================
 * The pick, on the host.
 *
 * Nothing is detoured for it. The engine's own Dialog_Update raises bSelected on the committed row
 * a quarter of a second after the commit, and that flag is exactly what the script polls through
 * Dialog_IsChoiceSelected. So the host watches its own block and tells the client the LINE it
 * answered with, which is what the client says again; a row index would mean nothing on a machine
 * that has no menu.
 * ============================================================================================ */

static bool block_is_active(void)
{
    uint32_t active = 0;

    return dialog.active_cell != 0u && memory_try_read_u32(dialog.active_cell, &active) &&
           active != 0u;
}

static bool row_count(uint32_t *out)
{
    return dialog.count_cell != 0u && memory_try_read_u32(dialog.count_cell, out);
}

bool mp_dialog_relay_answers_open(void)
{
    uint32_t count = 0;

    return block_is_active() && row_count(&count) && count != 0u;
}

static bool selected_line(uint16_t *out)
{
    uint32_t count = 0;
    uint32_t index;

    if (!row_count(&count) || count > MP_DIALOG_MAX_CHOICES) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        uintptr_t row      = dialog.choice_cell + index * CHOICE_STRIDE;
        uint32_t  selected = 0;
        uint32_t  line     = 0;

        if (memory_try_read_u32(row + CHOICE_SELECTED, &selected) && selected != 0u &&
            memory_try_read_u32(row + CHOICE_LINE_ID, &line) && line < MP_DIALOG_MAX_ID) {
            *out = (uint16_t)line;
            return true;
        }
    }
    return false;
}

void mp_dialog_relay_tick(void)
{
    uint8_t  note[MP_DIALOG_PICK_BYTES];
    uint16_t line  = 0;
    uint16_t level = 0;
    uint32_t count = 0;
    size_t   bytes;

    if (!dialog.installed || !dialog.host || dialog.send == NULL) {
        return;
    }
    /* A menu that has closed lets the same line be answered again in a later conversation. A new
     * spoken line always sets the count to nought, so this is the engine's own end of a menu
     * rather than a guess at one. */
    if (!block_is_active() || !row_count(&count) || count == 0u) {
        dialog.sent_pick_known = false;
        return;
    }
    if (!selected_line(&line)) {
        return;
    }
    if (dialog.sent_pick_known && dialog.sent_pick == line) {
        return;   /* said once: the flag stays up until the menu closes */
    }
    if (!mp_enemy_spawn_level_identity(&level)) {
        return;
    }
    bytes = mp_dialog_encode_pick(level, line, note, sizeof note);
    if (bytes != 0u && dialog.send(note, bytes)) {
        dialog.sent_pick       = line;
        dialog.sent_pick_known = true;
        ++dialog.picks_sent;
        log_info("this player answered with line %u, and a client near enough hears it at this "
                 "body", (unsigned)line);
    }
}

static bool take_pick(const uint8_t *note, size_t bytes)
{
    uint16_t line  = 0;
    uint16_t level = 0;
    uint16_t here  = 0;

    if (!mp_dialog_is_pick(note, bytes)) {
        return false;
    }
    ++dialog.picks_taken;
    if (!dialog.installed) {
        return true;
    }
    if (dialog.host) {
        ++dialog.picks_refused;   /* the host answers and is never answered */
        return true;
    }
    if (!mp_dialog_decode_pick(note, bytes, &level, &line) ||
        !mp_enemy_spawn_level_identity(&here) || here != level) {
        ++dialog.torn;
        return true;
    }
    hear_the_reply(level, line);
    return true;
}

bool mp_dialog_relay_take_message(const uint8_t *note, size_t bytes)
{
    mp_dialog_line_t line;
    uint16_t         here     = 0;
    bool             measured = false;

    if (take_pick(note, bytes)) {
        return true;
    }
    if (!mp_dialog_is(note, bytes)) {
        return false;
    }
    ++dialog.taken;
    if (!dialog.installed) {
        return true;
    }
    if (!mp_dialog_decode(note, bytes, &line)) {
        ++dialog.torn;
        return true;
    }
    /* A level has to be open, and it has to be the RIGHT one. Both matter for the same reason and
     * one of them is a crash rather than a wrong subtitle: the entry reads `g_level->worldTime`
     * for its own stamps, and the dialogue book is bound per level, so a line from elsewhere would
     * index another level's text. */
    if (!mp_enemy_spawn_level_identity(&here)) {
        ++dialog.no_level_here;
        return true;
    }
    if (here != line.level) {
        ++dialog.elsewhere;
        return true;
    }
    if (replay(MP_VOICE_FROM_HOST, &line, &measured)) {
        ++dialog.applied;
        dialog.unmeasured += measured ? 0u : 1u;
    } else if (measured) {
        ++dialog.out_of_earshot;
    }
    return true;
}

/* ==============================================================================================
 * Installation and the report.
 * ============================================================================================ */

void mp_dialog_relay_set_host(bool host)
{
    dialog.host = host;
}

void mp_dialog_relay_set_send(mp_dialog_relay_send_fn_t send)
{
    dialog.send = send;
}

void mp_dialog_relay_set_place_source(mp_dialog_relay_place_fn_t far_place)
{
    dialog.far_place = far_place;
}

/* The three cells of the answer menu, read out of the add-choice pattern's own operands rather
 * than written down, and nothing is hulled for them. The row count is named TWICE inside that one
 * pattern, at +17 and +25, and both are read and required to agree: a cell named twice must be
 * named the same twice, and here one pattern can keep that rule by itself.
 *
 * Nothing hulls the function, and once something did, with a prologue of six over `push ebp;
 * mov ebp,esp; push ecx; cmp dword [bActive],0`, whose compare is seven bytes. The trampoline
 * read its own appended jump as the compare's operand and ran off its end, and the client
 * crashed the first time its own menu opened. The declared prologue is eleven now, the first
 * instruction boundary past the compare. */
static bool resolve_menu(void)
{
    const signature_t *site = mp_signatures_dialog_site(MP_DIALOG_SITE_ADD_CHOICE);
    uintptr_t          again = 0;

    if (site == NULL || site->address == 0u) {
        return false;
    }
    if (!signature_read_address_operand(site, 6u, &dialog.active_cell) ||
        !signature_read_address_operand(site, 17u, &dialog.count_cell) ||
        !signature_read_address_operand(site, 25u, &again) ||
        !signature_read_address_operand(site, 33u, &dialog.choice_cell)) {
        return false;
    }
    if (again != dialog.count_cell) {
        log_error("the answer menu's row count is named twice in one function and the two names "
                  "disagree (%08X against %08X), so the pattern is matching something that is not "
                  "Dialog_AddChoice", (unsigned)dialog.count_cell, (unsigned)again);
        dialog.count_cell = 0u;
        return false;
    }
    log_info("the host's answers are watched: the block says active at %08X, %u row(s) at %08X "
             "in steps of %u, and the row the engine marks chosen is the line a client is told",
             (unsigned)dialog.active_cell, (unsigned)MP_DIALOG_MAX_CHOICES,
             (unsigned)dialog.choice_cell, (unsigned)CHOICE_STRIDE);
    return true;
}

bool mp_dialog_relay_install(void)
{
    uintptr_t speak;
    uintptr_t restart;

    if (dialog.installed) {
        return true;
    }
    speak   = mp_signatures_dialog_address(MP_DIALOG_SITE_SPEAK_SINGLE);
    restart = mp_signatures_dialog_address(MP_DIALOG_SITE_FORCE_RESTART);
    if (speak == 0u || restart == 0u) {
        log_warning("a conversation cannot travel: the dialogue %s did not resolve, so a client "
                    "will hear nothing of what the host is told",
                    speak == 0u ? "speak entry" : "restart latch");
        return false;
    }
    if (!detour_install(&dialog.speak_hull, speak, (const void *)&hook_speak_single,
                        mp_signatures_dialog_prologue(MP_DIALOG_SITE_SPEAK_SINGLE))) {
        log_warning("the dialogue speak entry at %08X would not take a hull, so a conversation "
                    "stays on the machine it is spoken on", (unsigned)speak);
        return false;
    }
    dialog.force_restart = (force_restart_fn_t)restart;

    /* The answer is optional and the spoken line is not. A build whose add-choice pattern did not
     * match still relays what is said, which is most of the value; what it loses is the host's
     * half of a menu, and it says so once rather than failing the whole module. */
    if (!resolve_menu()) {
        log_warning("the host's answers will not travel: the answer menu's cells did not resolve, "
                    "so a client hears what the host is told and not what it says back");
    }
    dialog.installed = true;
    /* The judgement of every line, on both roles, beside the hull it is asked from. It says in its
     * own lines whether it is bound; without it every line is said as the engine decides. */
    (void)mp_voice_install(dialog.active_cell);
    if (mp_voice_reach() > 0.0f) {
        log_info("a conversation travels: the host's spoken line is hulled at %08X and a client "
                 "near enough says it again out of its own dialogue book, with the restart latch "
                 "at %08X so every line is heard and not only read; earshot is %.1f u from this "
                 "player's body, the hearing radius of a voice outside a scene's lock",
                 (unsigned)speak, (unsigned)restart, (double)mp_voice_hearing_radius());
    } else {
        log_info("a conversation travels: the host's spoken line is hulled at %08X and a client "
                 "says every line again out of its own dialogue book, with the restart latch at "
                 "%08X so every line is heard and not only read; the lines are not judged, so the "
                 "engine alone decides who hears them", (unsigned)speak, (unsigned)restart);
    }
    return true;
}

void mp_dialog_relay_report(void)
{
    if (!dialog.installed) {
        log_info("  the conversation: NOT RUNNING, so a client hears nothing of a line the host "
                 "is told");
        return;
    }
    if (dialog.host) {
        log_info("  the conversation (the host): %u line(s) spoken here, %u sent, %u held as "
                 "repeats of a line already sent, %u refused by a full channel, %u with no level, "
                 "%u the codec refused",
                 (unsigned)dialog.spoken, (unsigned)dialog.sent, (unsigned)dialog.held,
                 (unsigned)dialog.unsent, (unsigned)dialog.no_level,
                 (unsigned)dialog.refused_encode);
        log_info("  the answers (the host): %u sent from here, one per menu; %u arrived here and "
                 "were refused, because the host answers and is never answered%s",
                 (unsigned)dialog.picks_sent, (unsigned)dialog.picks_refused,
                 dialog.count_cell == 0u ? " | THE MENU CELLS DID NOT BIND, so no answer is "
                                           "watched" : "");
        mp_voice_report();
        return;
    }
    /* The first line is the witness for the use latch: a client with the latch held should have
     * no script speaking on it, and a count here is one that did. */
    log_info("  the conversation (a client): %u line(s) spoken here by a local script and not "
             "sent, because only the host's conversations travel; %u withheld for an actor whose "
             "life the host describes, whose line the host says", (unsigned)dialog.spoken,
             (unsigned)dialog.withheld);
    log_info("  the conversation heard: %u note(s) taken, %u said again here, %u held back as out "
             "of earshot (farther than %.1f u from this player), %u said with no distance to "
             "measure, %u about another level, %u with no level open here, %u torn",
             (unsigned)dialog.taken, (unsigned)dialog.applied, (unsigned)dialog.out_of_earshot,
             (double)mp_voice_hearing_radius(), (unsigned)dialog.unmeasured,
             (unsigned)dialog.elsewhere,
             (unsigned)dialog.no_level_here, (unsigned)dialog.torn);
    log_info("  the answers (a client): %u taken, %u heard and read here as the host's line at "
             "the far body, %u of them with no far body known, %u held back as out of earshot",
             (unsigned)dialog.picks_taken, (unsigned)dialog.replies_heard,
             (unsigned)dialog.replies_unplaced, (unsigned)dialog.replies_out_of_earshot);
    mp_voice_report();
}
