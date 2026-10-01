/* mp_watchpost.c: the frame that asks whether this build is doing what it says.
 *
 * Three questions, each answerable at a different moment, and the reason they are apart is that
 * every one of them was once answered at the wrong one and produced a number that read like a
 * finding and was not one.
 *
 *   Was the foothold installed?  At the first drawn frame, the earliest it can be asked at all.
 *   Is the task being fed?       At the frame it first is, and once as a warning when it still is
 *                                not after the deadline. Substeps run inside a level, so twenty
 *                                seconds in the menu reach that deadline in an ordinary session.
 *   Is it still being fed?       Three hundred drawn frames with a peer on the wire and no substep
 *                                among them, said once per stall, with what the engine says about
 *                                itself at that moment, and the return said too.
 *
 * The third question comes out of a field run. A client drew 3738 frames and stepped its world on
 * two of them: it stood in its level while the far body and the replicas moved, because those are
 * written from the idle pump and not from the substep, and nothing here said so. The only reason
 * anyone could tell afterwards was another module's frame counter, printed for a different purpose.
 *
 * The pool probe and the full-pool provocation used to hang off the first question. They are the
 * installer's, not the watchpost's, and they stayed behind with the configuration that switches
 * them on.
 */
#include "mp_watchpost.h"

#include "mp_body.h"
#include "mp_bootstrap.h"
#include "mp_bridge.h"
#include "mp_cells.h"
#include "mp_module.h"
#include "mp_signatures.h"
#include "mp_signatures_pause.h"
#include "mp_task.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Armed is not run, and the difference is invisible from the log otherwise: the arming line is
 * written whatever happens next, so a build where the way in is never reached reads exactly like a
 * build where it worked.
 *
 * There are two questions, and the first field run showed why they cannot share one moment. Asking
 * "was it installed" is only meaningful at the earliest frame there is. Asking "is it being fed"
 * is meaningless there: substeps run inside a level, the first frame is a logo, and a tick count of
 * zero at that point says nothing at all. It was reported as zero anyway, which is a number that
 * reads like a finding and is not one.
 *
 * So the installation is reported once at the first frame, and the feeding is reported at the frame
 * it actually starts. When it has not started by the deadline that is said once as a warning, not
 * an error, because a player sitting in the menu for twenty seconds is the ordinary way to reach
 * it; the first tick is still reported when it comes, so a level opened after the deadline is not
 * left looking as if the task were never fed. */
#define WATCHPOST_TICK_DEADLINE 1200u

/* What the engine says about itself while the substeps are missing. Read at the moment of the
 * report, not held: a stall report that only says a number leaves the next question open, and
 * these four cells answer it between them.
 *
 *   a level        the world pointer: nothing there means no level is open, and no level is the
 *                  ordinary reason for no substep
 *   a menu         the screen on show, and whether it is the title screen; a menu inside a
 *                  session is the second ordinary reason
 *   the AI switch  our own suspend cell, in case this module stopped the actors itself
 *   the outcome    the death latch that ends a level for everyone
 *
 * Each is printed as "?" when its cell did not resolve, because a zero that was never read is
 * a number that reads like a finding and is not one. */
/* What the outcome cell means, rather than whether it is zero. The old line printed SET for every
 * number but nought, and the number a running level carries is 2: every stall line in every field
 * log said the level had been ended, which is the opposite of what was true. The values are the
 * campaign loop's own: it writes 2 before the level and spins while the cell still reads 2, a
 * script writes 3 for the exit and 1 for the credits, and a death writes 4 or more, which the loop
 * turns into the continue screen. */
static const char *outcome_name(uint32_t outcome)
{
    switch (outcome) {
    case 0u:
        return "none";
    case 1u:
        return "the credits";
    case 2u:
        return "running";
    case 3u:
        return "complete";
    default:
        break;
    }
    return outcome <= 10u ? "a death" : "unknown";
}

static void describe_the_stall(char *out, size_t bytes)
{
    uintptr_t level_cell   = mp_cells_address(MP_CELL_LEVEL);
    uintptr_t menu_cell    = mp_cells_address(MP_CELL_CURRENT_MENU);
    uintptr_t title_cell   = mp_cells_address(MP_CELL_TITLE_SCREEN);
    uintptr_t suspend_cell = mp_cells_address(MP_CELL_ENEMY_SUSPEND);
    uintptr_t outcome_cell = mp_cells_address(MP_CELL_LEVEL_OUTCOME);
    uintptr_t gate_cell    = mp_signatures_pause_cell(MP_PAUSE_CELL_SIM_GATE);
    uintptr_t latch_cell   = mp_signatures_pause_cell(MP_PAUSE_CELL_LATCH);
    uint32_t  level = 0, menu = 0, title = 0, suspend = 0, outcome = 0, gate = 0, latch = 0;
    bool      have_level   = level_cell != 0u && memory_read_u32(level_cell, &level);
    bool      have_menu    = menu_cell != 0u && memory_read_u32(menu_cell, &menu);
    bool      have_title   = title_cell != 0u && memory_read_u32(title_cell, &title);
    bool      have_suspend = suspend_cell != 0u && memory_read_u32(suspend_cell, &suspend);
    bool      have_outcome = outcome_cell != 0u && memory_read_u32(outcome_cell, &outcome);
    bool      have_gate    = gate_cell != 0u && memory_read_u32(gate_cell, &gate);
    bool      have_latch   = latch_cell != 0u && memory_read_u32(latch_cell, &latch);
    char      outcome_text[32];

    if (have_outcome) {
        text_format(outcome_text, sizeof outcome_text, "%u (%s)", (unsigned)outcome,
                    outcome_name(outcome));
    } else {
        outcome_text[0] = '?';
        outcome_text[1] = '\0';
    }

    /* The gate and the latch are the two cells that answer WHY there is no substep, which is the
     * question this line exists for: the gate is the one thing in sys_frame that holds the
     * substeps, and the latch says the pause screen itself is up rather than a load or a menu. */
    text_format(out, bytes,
                "level %s, menu %s%s, AI switch %s, level outcome %s, simulation gate %s, pause "
                "latch %s",
                have_level ? (level != 0u ? "open" : "NONE") : "?",
                have_menu ? (menu != 0u ? "OPEN" : "none") : "?",
                (have_menu && have_title && menu != 0u && menu == title) ? " (the title screen)"
                                                                        : "",
                have_suspend ? (suspend != 0u ? "SUSPENDED" : "running") : "?",
                outcome_text,
                have_gate ? (gate != 0u ? "HELD" : "open") : "?",
                have_latch ? (latch != 0u ? "SET" : "clear") : "?");
}

/* How many drawn frames a joined session may go without a single substep before it is said out
 * loud. At a hundred frames a second this is three seconds, which no level change takes and no
 * hitch reaches; a menu opened inside a session reaches it, and that is worth a line too.
 *
 * One field run is why this exists. The client drew 3738 frames and stepped its world on two of
 * them: it stood in its level, drawing, with the far body and the replicas moving because
 * the idle pump writes them from the message loop, while its own simulation ran not at all. The
 * only reason anyone could tell afterwards was another module's counter, and by then the run was
 * over. A frozen client cannot follow the host, so the moment it stops is a finding. */
#define WATCHPOST_STALL_FRAMES 300u

/* The spread of the substeps over the drawn frames. Kept per frame because that is the only
 * place both numbers are known, and reported once, because a per-second line about a steady
 * state is noise.
 *
 * A later field run is why. Its client drew a hundred frames a second and carried a substep on
 * four of them; the host drew the same hundred and carried thirty two, one at a time. Both
 * sides simulated at about the same RATE, which the host counted from the other end: 2048 of
 * the client's states landed over 2575 host substeps. So the client was not slow, it was
 * BUNCHED, and everything it simulated moved in steps a tenth of a second apart. That is what a
 * player sees as jumping, and no single rate number can tell it from smooth motion. */
typedef struct spread {
    uint32_t frames;
    uint32_t substeps;
    uint32_t none;        /* frames that carried no substep at all */
    uint32_t one;
    uint32_t many;        /* two or more in one frame, which is a burst */
    uint32_t gap;         /* the current run of frames without one */
    uint32_t worst_gap;
} spread_t;

static spread_t spread;

static void note_spread(uint32_t stepped)
{
    ++spread.frames;
    spread.substeps += stepped;
    if (stepped == 0u) {
        ++spread.none;
        ++spread.gap;
        if (spread.gap > spread.worst_gap) {
            spread.worst_gap = spread.gap;
        }
        return;
    }
    spread.gap = 0u;
    if (stepped == 1u) {
        ++spread.one;
    } else {
        ++spread.many;
    }
}

void mp_watchpost_report(void)
{
    uint32_t frames = spread.frames != 0u ? spread.frames : 1u;

    log_info("  the substeps over the drawn frames: %u substep(s) on %u frame(s); %u frame(s) "
             "carried none (%u%%), %u carried one, %u carried two or more; longest run "
             "without one %u frame(s). An even side carries at most one at a time and its "
             "longest run is the rate; bursts and a long run are what a player sees as jumping.",
             (unsigned)spread.substeps, (unsigned)spread.frames, (unsigned)spread.none,
             (unsigned)((spread.none * 100u) / frames), (unsigned)spread.one,
             (unsigned)spread.many, (unsigned)spread.worst_gap);
}

void mp_watchpost_frame(bool arm_dispatcher)
{
    static bool     install_reported = false;
    static bool     ticks_reported   = false;
    static bool     deadline_reported = false;
    static uint32_t frames           = 0;
    static uint32_t last_ticks       = 0;
    static uint32_t last_tick_frame  = 0;
    static bool     stall_reported   = false;
    static uint32_t stalls           = 0;
    uint32_t        ticks;
    const mp_bootstrap_result_t *result;

    ++frames;
    mp_module_note_present();   /* the census counts against this, not against the engine clock */

    /* Re-arm each frame, idempotently: a spawn restores the engine's own handler in the slot, and
     * the frame hook is a coarse enough clock for that, since a contact the frame after a spawn is
     * as good as one the substep after. Cheap: two reads and a compare when it is already ours. */
    if (arm_dispatcher) {
        mp_body_arm_dispatcher();
    }

    if (!install_reported) {
        install_reported = true;

        if (!mp_bootstrap_has_run()) {
            log_error("a frame has been drawn and the foothold has NOT been installed. The way in "
                      "was armed and never reached, which is the failure this check exists for.");
            ticks_reported = true;
            return;
        }

        result = mp_bootstrap_result();
        log_info("foothold live: %u modules in the list, our nodes at %08X and %08X, task record "
                 "%08X", (unsigned)result->module_count, (unsigned)result->tail_node,
                 (unsigned)result->head_node, (unsigned)result->task_record);
    }

    /* The substeps of a joined session, watched from the frame that drew them. */
    ticks = mp_task_ticks();
    note_spread(ticks - last_ticks);
    if (ticks != last_ticks) {
        if (stall_reported) {
            log_info("the substeps are running again after %u frame(s) without one",
                     (unsigned)(frames - last_tick_frame));
            stall_reported = false;
        }
        last_ticks      = ticks;
        last_tick_frame = frames;
    } else if (!stall_reported && ticks != 0u && mp_bridge_joined() &&
               frames - last_tick_frame >= WATCHPOST_STALL_FRAMES) {
        char why[256];

        stall_reported = true;
        ++stalls;
        describe_the_stall(why, sizeof why);
        log_warning("%u frame(s) have been drawn with a peer on the wire and NOT ONE substep "
                    "among them (stall %u, last substep at frame %u). This side is not "
                    "simulating: its own player stands still, and what moves on it is what the "
                    "idle pump writes. The engine says: %s",
                    (unsigned)(frames - last_tick_frame), (unsigned)stalls,
                    (unsigned)last_tick_frame, why);
    }

    if (ticks_reported) {
        return;
    }

    if (ticks != 0) {
        ticks_reported = true;
        log_info("the substep task is being ticked, first seen on frame %u: %u ticks, %u module "
                 "messages so far", (unsigned)frames, (unsigned)ticks,
                 (unsigned)mp_module_total_arrivals());
    } else if (frames >= WATCHPOST_TICK_DEADLINE && !deadline_reported) {
        deadline_reported = true;
        log_warning("%u frames and the substep task has not been ticked yet, although it holds a "
                    "slot; substeps run inside a level, so this is the menu until the first tick "
                    "is reported. The module nodes have had %u messages in the same time.",
                    (unsigned)frames, (unsigned)mp_module_total_arrivals());
    }
}
