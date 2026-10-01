/* mp_pause.c: the pause menu of a session, bound to the engine. See the header.
 *
 * Nothing here is a detour. The key hook's call of sys_pause is the one write, four operand bytes,
 * set while a transport stands and put back when it comes down, each time only after the bytes are
 * proved to be what they should be: sys_pause's own address before the write, this file's entry
 * before the put back. Everything else is a read of cells whose addresses come out of the
 * instructions that use them, and the writes the one exit makes, which are the writes sys_pause
 * makes for the same answer.
 */
#include "mp_pause.h"

#include "mp_armed.h"
#include "mp_cells.h"
#include "mp_cutscene.h"
#include "mp_input.h"
#include "mp_pause_rule.h"
#include "mp_signatures.h"
#include "mp_signatures_pause.h"
#include "mp_task.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef void(__cdecl *engine_void_fn)(void);
typedef int32_t(__cdecl *pause_menu_fn)(void);

/* The E8 of a near call, and its length. */
#define CALL_REL32_OPCODE 0xE8u
#define CALL_REL32_LENGTH 5u

/* The engine's answers the close line names. */
#define PAUSE_REPLY_CLOSED  0
#define PAUSE_REPLY_RESUMED 5

typedef struct pause_binding {
    bool      tried;
    bool      ready;
    uint32_t  bind_us;       /* how long the sites took to bind and prove, for the hull line */
    uintptr_t call;          /* the E8 in the key hook */
    uintptr_t sys_pause;
    uintptr_t menu;          /* pausemenu_run, out of sys_pause's own call */
    uintptr_t world_pump;    /* sys_frame; 0 leaves the player list on the menu's own pump */
    uintptr_t gate;
    uintptr_t outcome;
    uintptr_t restore;
    uintptr_t backdrop;
    uintptr_t lock;
    uintptr_t hero_block;
    uintptr_t status_cell;
} pause_binding_t;

typedef struct pause_state {
    pause_binding_t    bound;
    bool               redirected;
    bool               hull_said;
    bool               unheld_said;
    bool               note_refusal_said;
    bool               put_back_said;
    bool               write_fault_said;
    mp_pause_session_t session;
    uint32_t           arms;
    uint32_t           disarms;
    uint32_t           arm_refusals;
    uint32_t           engine_pauses;
    uint32_t           unheld_pauses;
    uint32_t           reentries;
    uint32_t           world_pumps;
    uint32_t           menu_pumps;
    uint32_t           write_faults;
} pause_state_t;

static pause_state_t pause;

static void __cdecl hook_pause_entry(void);

/* ==============================================================================================
 * Binding: every address out of the bytes that use it, and each one proved before it is believed.
 * ============================================================================================ */

/* Whether the E8 at `call` lands on `target`. Read by hand rather than through the patch module,
 * whose reader refuses a target outside the image, and this file's entry is outside it. */
static bool call_lands_at(uintptr_t call, uintptr_t target)
{
    uint8_t  opcode = 0u;
    uint32_t displacement = 0u;

    return memory_read_u8(call, &opcode) && opcode == CALL_REL32_OPCODE &&
           memory_read_u32(call + 1u, &displacement) &&
           call + CALL_REL32_LENGTH + displacement == target;
}

/* A cell this file writes has to be the cell the rest of the feature knows by that name, where
 * the rest of the feature knows it at all. */
static bool agrees_with_the_table(uintptr_t read, mp_cell_t cell)
{
    uintptr_t known = mp_cells_address(cell);

    return known == 0u || known == read;
}

/* The player list over the menu pumps the world's frame only when the pump choice proves to be
 * the menu's: its second call has to land on the menu pump the toolkit binding already knows. */
static void bind_the_world_pump(pause_binding_t *b)
{
    uintptr_t world_call = mp_signatures_pause_call(MP_PAUSE_CALL_WORLD_PUMP);
    uintptr_t menu_call  = mp_signatures_pause_call(MP_PAUSE_CALL_MENU_PUMP);
    uintptr_t menu_pump  = 0u;
    uintptr_t known_pump = mp_signatures_address(MP_SITE_SWMENU_PUMP_FRAME);

    b->backdrop = mp_signatures_pause_cell(MP_PAUSE_CELL_BACKDROP);
    if (world_call == 0u || menu_call == 0u || b->backdrop == 0u ||
        !patch_read_call_target(menu_call, &menu_pump) ||
        (known_pump != 0u && menu_pump != known_pump) ||
        !patch_read_call_target(world_call, &b->world_pump)) {
        b->world_pump = 0u;
        log_warning("the pause menu's choice of a frame pump did not prove itself, so the player "
                    "list over a session's pause menu keeps the menu's own pump and stops the "
                    "world while it is up");
    }
}

/* What the world under the menu is read from. Each one missing costs one close reason, said. */
static void bind_the_looks(pause_binding_t *b)
{
    b->lock        = mp_cutscene_lock_level_cell();
    b->hero_block  = mp_cells_address(MP_CELL_HERO_BLOCK);
    b->status_cell = mp_cells_address(MP_CELL_PLR_STATUS_POINTER);
    if (b->lock == 0u) {
        log_warning("the dialogue and scene lock did not resolve, so a scene that begins under a "
                    "session's pause menu does not close it");
    }
    if (b->hero_block == 0u && b->status_cell == 0u) {
        log_warning("neither the hero block nor the status record resolved, so a death under a "
                    "session's pause menu does not close it");
    }
}

/* Why the sites cannot carry a session's pause menu, or NULL when they can. */
static const char *prove_the_sites(pause_binding_t *b)
{
    uintptr_t target    = 0u;
    uintptr_t menu_call = mp_signatures_pause_call(MP_PAUSE_CALL_MENU);

    b->call      = mp_signatures_pause_call(MP_PAUSE_CALL_PAUSE);
    b->sys_pause = mp_signatures_pause_address(MP_PAUSE_SITE_SYS_PAUSE);
    b->gate      = mp_signatures_pause_cell(MP_PAUSE_CELL_SIM_GATE);
    b->outcome   = mp_signatures_pause_cell(MP_PAUSE_CELL_OUTCOME);
    b->restore   = mp_signatures_pause_cell(MP_PAUSE_CELL_RESTORE);
    if (b->call == 0u || b->sys_pause == 0u) {
        return "the key hook's call or sys_pause did not resolve";
    }
    /* Identify before replacing: the call has to land on sys_pause, or on this file's own entry
     * after a put back that did not happen. */
    if ((!patch_read_call_target(b->call, &target) || target != b->sys_pause) &&
        !call_lands_at(b->call, (uintptr_t)&hook_pause_entry)) {
        return "the key hook's call does not land on sys_pause";
    }
    if (menu_call == 0u || !patch_read_call_target(menu_call, &b->menu) || b->menu == 0u) {
        return "sys_pause's call of its menu did not read";
    }
    if (b->gate == 0u || b->outcome == 0u || b->restore == 0u) {
        return "a cell sys_pause writes did not resolve";
    }
    if (!agrees_with_the_table(b->outcome, MP_CELL_LEVEL_OUTCOME) ||
        !agrees_with_the_table(b->restore, MP_CELL_RESTORE_PENDING)) {
        return "a cell sys_pause writes is not the cell the rest of the feature knows by its name";
    }
    return NULL;
}

static bool bind_once(void)
{
    pause_binding_t *b = &pause.bound;
    LARGE_INTEGER    start;
    LARGE_INTEGER    end;
    LARGE_INTEGER    rate;
    const char      *why;

    if (b->tried) {
        return b->ready;
    }
    b->tried = true;
    QueryPerformanceCounter(&start);
    (void)mp_signatures_pause_menu_resolve();
    why = prove_the_sites(b);
    if (why != NULL) {
        log_warning("the pause menu cannot be hulled: %s; in a session it stays the engine's own "
                    "and holds the world for everybody", why);
        return false;
    }
    bind_the_world_pump(b);
    bind_the_looks(b);
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&rate);
    b->bind_us = rate.QuadPart > 0
                     ? (uint32_t)(((end.QuadPart - start.QuadPart) * 1000000) / rate.QuadPart)
                     : 0u;
    b->ready = true;
    return true;
}

/* ==============================================================================================
 * The menu.
 * ============================================================================================ */

static bool read_u32(uintptr_t cell, uint32_t *out)
{
    return cell != 0u && memory_try_read(cell, out, sizeof *out);
}

static const char *reply_word(int32_t reply)
{
    if (reply < 0) {
        return "refused by the engine";
    }
    switch (reply) {
    case PAUSE_REPLY_CLOSED:
        return "closed";
    case PAUSE_REPLY_RESUMED:
        return "resumed";
    case MP_PAUSE_REPLY_QUIT:
        return "left the level";
    default:
        break;
    }
    return "answered";
}

static void say_the_reason(void)
{
    log_info("the pause menu in a session is being closed for this player: %s",
             mp_pause_rule_reason_text(pause.session.reason));
}

static void apply_the_exit(const mp_pause_write_t *writes, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        uintptr_t cell = writes[i].cell == MP_PAUSE_WRITE_OUTCOME   ? pause.bound.outcome
                         : writes[i].cell == MP_PAUSE_WRITE_RESTORE ? pause.bound.restore
                                                                    : pause.bound.gate;

        if (memory_try_write(cell, &writes[i].value, sizeof writes[i].value)) {
            continue;
        }
        ++pause.write_faults;
        if (!pause.write_fault_said) {
            pause.write_fault_said = true;
            log_warning("the pause menu's exit could not write the cell at %08X; later faults are "
                        "counted", (unsigned)cell);
        }
    }
}

static void say_the_close(void)
{
    const mp_pause_left_t *left = &pause.session.last;

    log_info("the pause menu in a session closed after %u ms: %s (reply %d), %s%s; %u frame(s), "
             "%u with the simulation gate held, %u drawn over %u ms after a substep, the longest "
             "stretch without one %u ms; %u substep(s) ran; %u cancel(s) handed to its screens",
             (unsigned)left->duration_ms, reply_word(left->reply), (int)left->reply,
             left->reason == MP_PAUSE_REASON_NONE ? "by the player" : "for this player: ",
             left->reason == MP_PAUSE_REASON_NONE ? "" : mp_pause_rule_reason_text(left->reason),
             (unsigned)left->frames, (unsigned)left->gate_frames, (unsigned)left->stalled_frames,
             (unsigned)MP_PAUSE_STALL_MS, (unsigned)left->longest_stretch_ms,
             (unsigned)left->substeps, (unsigned)left->cancels);
}

/* The engine's pause menu over the running world, and the one way out of it. */
static void run_the_session_pause(void)
{
    mp_pause_write_t writes[MP_PAUSE_EXIT_WRITES_MAX];
    uint32_t         lock = 0u;
    uint32_t         backdrop = 0u;
    bool             lock_read = read_u32(pause.bound.lock, &lock);
    bool             backdrop_read = read_u32(pause.bound.backdrop, &backdrop);
    bool             said = false;
    int32_t          reply;
    size_t           count;

    if (!mp_pause_rule_open(&pause.session, lock_read, (int32_t)lock, mp_task_ticks(),
                            GetTickCount(), &said)) {
        ++pause.reentries;
        return;
    }
    if (!said && !pause.note_refusal_said) {
        pause.note_refusal_said = true;
        log_warning("the session note refused the input hold, so another mod's mouse and pad path "
                    "may still turn this player while the menu is up; the engine's own readers "
                    "are held all the same");
    }
    /* The cell only the SUSPEND broadcast sets, and nothing here sends one, so it should read
     * nought already. It is cleared anyway, as the engine's own console clears it before it pumps
     * over a running world: a one left behind by the debug console's single step would put the
     * menu on the pump that runs no substep and stop the world again. */
    if (backdrop_read && backdrop != 0u) {
        uint32_t nought = 0u;

        (void)memory_try_write(pause.bound.backdrop, &nought, sizeof nought);
    }
    log_info("the pause menu opens in a session as %s: the world keeps running and this player's "
             "input is held; the backdrop cell read %s",
             mp_armed_is_host() ? "the host" : "a client",
             !backdrop_read ? "nothing" : (backdrop != 0u ? "1" : "0"));

    reply = ((pause_menu_fn)pause.bound.menu)();

    count = mp_pause_rule_leave(&pause.session, reply, GetTickCount(), writes,
                                MP_PAUSE_EXIT_WRITES_MAX);
    apply_the_exit(writes, count);
    say_the_close();
}

/* engine: void sys_pause(void) */
static void __cdecl hook_pause_entry(void)
{
    switch (mp_pause_rule_way(pause.session.open, mp_armed_transport(), mp_input_installed())) {
    case MP_PAUSE_WAY_ALREADY_OPEN:
        ++pause.reentries;
        return;
    case MP_PAUSE_WAY_SESSION:
        run_the_session_pause();
        return;
    case MP_PAUSE_WAY_ENGINE_UNHELD:
        ++pause.unheld_pauses;
        if (!pause.unheld_said) {
            pause.unheld_said = true;
            log_warning("the pause menu in a session cannot hold this player's input, because the "
                        "input split is not installed; the engine's own pause runs instead and "
                        "holds the world for everybody");
        }
        break;
    case MP_PAUSE_WAY_ENGINE:
    default:
        ++pause.engine_pauses;
        break;
    }
    ((engine_void_fn)pause.bound.sys_pause)();
}

/* ==============================================================================================
 * The transport's two moments, the frame, the navigation read, the player list.
 * ============================================================================================ */

bool mp_pause_arm(void)
{
    patch_result_t result;

    if (pause.redirected) {
        return true;
    }
    if (!bind_once()) {
        ++pause.arm_refusals;
        return false;
    }
    if (call_lands_at(pause.bound.call, (uintptr_t)&hook_pause_entry)) {
        pause.redirected = true;   /* a put back that did not happen left it pointing here */
        return true;
    }
    /* Identified again at every arming, not only at the first: between two sessions another
     * module may have repointed the same call, and its target is not this file's to overwrite. */
    if (!call_lands_at(pause.bound.call, pause.bound.sys_pause)) {
        ++pause.arm_refusals;
        log_warning("the pause menu's call at %08X no longer lands on sys_pause, so it is left as "
                    "it is; in a session the pause stays the engine's own",
                    (unsigned)pause.bound.call);
        return false;
    }
    result = patch_redirect_call(pause.bound.call, (const void *)&hook_pause_entry);
    if (result != PATCH_RESULT_OK) {
        ++pause.arm_refusals;
        log_warning("the pause menu's call at %08X could not be repointed (%s); in a session the "
                    "pause stays the engine's own and holds the world for everybody",
                    (unsigned)pause.bound.call, patch_result_text(result));
        return false;
    }
    pause.redirected = true;
    ++pause.arms;
    if (!pause.hull_said) {
        pause.hull_said = true;
        log_info("the pause menu is hulled at its one call %08X, which called %08X (sys_pause, its "
                 "menu %08X), bound in %u.%03u ms: while a transport stands it opens over a "
                 "running world and holds only this player's input; without one the call is put "
                 "back and the pause is the engine's own",
                 (unsigned)pause.bound.call, (unsigned)pause.bound.sys_pause,
                 (unsigned)pause.bound.menu, (unsigned)(pause.bound.bind_us / 1000u),
                 (unsigned)(pause.bound.bind_us % 1000u));
    }
    return true;
}

void mp_pause_disarm(void)
{
    patch_result_t result;

    if (mp_pause_rule_session_ended(&pause.session, GetTickCount())) {
        say_the_reason();
    }
    if (!pause.redirected) {
        return;
    }
    /* Identify before replacing, the other way round: only a call that still lands here is put
     * back. One that lands elsewhere belongs to whoever wrote it since, and is left as it is. */
    if (!call_lands_at(pause.bound.call, (uintptr_t)&hook_pause_entry)) {
        pause.redirected = false;
        if (!pause.put_back_said) {
            pause.put_back_said = true;
            log_warning("the pause menu's call at %08X no longer lands on this feature's entry, so "
                        "it is left as it is", (unsigned)pause.bound.call);
        }
        return;
    }
    result = patch_redirect_call(pause.bound.call, (const void *)pause.bound.sys_pause);
    if (result != PATCH_RESULT_OK) {
        if (!pause.put_back_said) {
            pause.put_back_said = true;
            log_warning("the pause menu's call at %08X could not be put back (%s); it stays on "
                        "this feature's entry, which hands every press to sys_pause while no "
                        "transport stands", (unsigned)pause.bound.call,
                        patch_result_text(result));
        }
        return;
    }
    pause.redirected = false;
    ++pause.disarms;
}

void mp_pause_frame(void)
{
    mp_pause_look_t   look;
    mp_pause_reason_t before = pause.session.reason;
    uint32_t          gate = 0u;
    uint32_t          record = 0u;
    uint32_t          corpse = 0u;
    uint32_t          lock = 0u;

    if (!pause.session.open) {
        return;
    }
    memset(&look, 0, sizeof look);
    look.now_ms       = GetTickCount();
    look.substeps     = mp_task_ticks();
    look.gate_held    = read_u32(pause.bound.gate, &gate) && gate != 0u;
    look.health_read  = read_u32(pause.bound.status_cell, &record) && record != 0u &&
                        memory_try_read(record, &look.health, sizeof look.health);
    look.dead         = pause.bound.hero_block != 0u &&
                        read_u32(pause.bound.hero_block + MP_HERO_BLOCK_DEAD, &corpse) &&
                        corpse != 0u;
    look.lock_read    = read_u32(pause.bound.lock, &lock);
    look.lock         = (int32_t)lock;
    look.outcome_read = read_u32(pause.bound.outcome, &look.outcome);
    mp_pause_rule_look(&pause.session, &look);
    if (before == MP_PAUSE_REASON_NONE && pause.session.reason != MP_PAUSE_REASON_NONE) {
        say_the_reason();
    }
}

int32_t mp_pause_nav(int32_t code)
{
    uint32_t now;
    bool     gave_up = false;
    int32_t  answer;

    if (!pause.session.open) {
        return code;
    }
    now = GetTickCount();
    if (!mp_armed_transport() && mp_pause_rule_session_ended(&pause.session, now)) {
        say_the_reason();
    }
    answer = mp_pause_rule_nav(&pause.session, code, now, &gave_up);
    if (gave_up) {
        log_warning("the pause menu would not close for %s after %u ms and %u cancel(s) handed "
                    "to its screens: the screen on top does not act on one, so it is left to "
                    "the player until his next key, which starts it over",
                    mp_pause_rule_reason_text(pause.session.reason),
                    (unsigned)(now - pause.session.reason_ms), (unsigned)pause.session.cancels);
    }
    return answer;
}

bool mp_pause_pump_world(void)
{
    uint32_t backdrop = 0u;
    bool     read;

    if (!pause.session.open) {
        return false;
    }
    read = read_u32(pause.bound.backdrop, &backdrop);
    if (mp_pause_rule_pump(true, read, backdrop, pause.bound.world_pump != 0u) !=
        MP_PAUSE_PUMP_WORLD) {
        ++pause.menu_pumps;
        return false;
    }
    ++pause.world_pumps;
    ((engine_void_fn)pause.bound.world_pump)();
    return true;
}

void mp_pause_report(void)
{
    const mp_pause_session_t *s = &pause.session;
    uint32_t                  said = 0u;
    uint32_t                  refused = 0u;

    if (!pause.bound.tried) {
        return;   /* no transport ever stood in this process */
    }
    log_info("  the pause menu, opened: %u in a session, %u by the engine's own pause, %u by the "
             "engine's own because the input split is not installed, %u press(es) found it "
             "already open; its call hulled %u time(s), put back %u time(s), refused %u time(s)",
             (unsigned)s->opened_total, (unsigned)pause.engine_pauses,
             (unsigned)pause.unheld_pauses, (unsigned)pause.reentries, (unsigned)pause.arms,
             (unsigned)pause.disarms, (unsigned)pause.arm_refusals);
    log_info("  the pause menu, while a session's was up: %u frame(s), %u with the simulation gate "
             "held, %u drawn over %u ms after a substep, the longest stretch without one %u ms; "
             "%u substep(s) ran",
             (unsigned)s->frames_total, (unsigned)s->gate_frames_total,
             (unsigned)s->stalled_frames_total, (unsigned)MP_PAUSE_STALL_MS,
             (unsigned)s->longest_stretch_total_ms, (unsigned)s->substeps_total);
    log_info("  the pause menu, left: %u time(s) to play (%u after a load inside it), %u to quit "
             "the level, %u refused by the engine; closed for the player by a death %u, a scene "
             "lock that rose %u, the level's outcome %u, the end of the session %u; given up %u "
             "time(s); %u exit write(s) faulted",
             (unsigned)s->left_to_play, (unsigned)s->left_after_load, (unsigned)s->left_to_quit,
             (unsigned)s->refused_by_engine, (unsigned)s->closed_by[MP_PAUSE_REASON_DEATH],
             (unsigned)s->closed_by[MP_PAUSE_REASON_SCENE],
             (unsigned)s->closed_by[MP_PAUSE_REASON_LEVEL],
             (unsigned)s->closed_by[MP_PAUSE_REASON_SESSION], (unsigned)s->given_up_total,
             (unsigned)pause.write_faults);
    log_info("  the pause menu's forced close, taken up again: %u time(s) a key of the player's "
             "started it over after it had been given up", (unsigned)s->rearmed_total);
    log_info("  the player list over a session's pause menu: the world pumped on %u frame(s), the "
             "menu's own pump on %u", (unsigned)pause.world_pumps, (unsigned)pause.menu_pumps);
    mp_armed_note_counts(&said, &refused);
    log_info("  the session note: said %u time(s), %u refused by the shared channel, %u of them "
             "an input hold", (unsigned)said, (unsigned)refused, (unsigned)s->held_note_refusals);
}
