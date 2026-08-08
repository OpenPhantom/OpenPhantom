#include "steer_log.h"

#include "player_record.h"
#include "steer_lean.h"

#include "common/logging.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fields that are only ever READ here, to say what state the player was in. They are logged as raw
 * numbers under neutral names rather than interpreted, because interpreting them is exactly the
 * step that has been going wrong.
 *
 *   +0x090 / +0x094  the two gates Plr_StartFire and Plr_StartBlockShot test before they run
 *   +0x178           the chest claim: non-zero means the auto-aim owns the chest node
 *   +0x154           moveInput, bit 0 forward, bit 1 backward, bits 2/3 the turn hold
 *   +0x2A4           turnWheel, after everything in this substep has written it                 */
#define PLAYER_FIRE_GATE_A   0x090
#define PLAYER_FIRE_GATE_B   0x094

typedef struct steer_log_state {
    int lines_left;
    int substep;
} steer_log_state_t;

static steer_log_state_t log_state;

void steer_log_configure(int max_lines)
{
    log_state.lines_left = (max_lines > 0) ? max_lines : 0;
    log_state.substep    = 0;

    if (log_state.lines_left > 0) {
        log_info("SteerLog=%d - one line per substep in which the player is steering, then it "
                 "stops. It names the branch that took the substep, what the lean wrote and why "
                 "it did not, and the fire gates, so a silent feature can be told apart from an "
                 "absent one.", max_lines);
    }
}

static const char *branch_name(steer_branch_t branch)
{
    switch (branch) {
    case STEER_BRANCH_DECLINED:   return "DECLINED";
    case STEER_BRANCH_FREE_LOOK:  return "freelook";
    case STEER_BRANCH_MOUSE_LOOK: return "mouselook";
    default:                      return "passive";
    }
}

static float read_float(const uint8_t *record, int offset)
{
    return *(const float *)(record + offset);
}

void steer_log_substep(const uint8_t *record, steer_branch_t branch, float substep_seconds,
                       float yaw_degrees, float travel_degrees, int mode_index)
{
    steer_lean_report_t lean;
    uint32_t            move_input;
    bool                interesting;

    ++log_state.substep;
    if (log_state.lines_left <= 0) {
        return;
    }

    /* The filter is the whole value of this instrument, and the first version got it wrong: it
     * skipped idle substeps only on the DECLINED branch, so a standing player burned all sixty
     * lines before touching a key. There are 32 substeps a second; a log that spends them on
     * nothing is worse than no log, because it looks like an answer.
     *
     * A substep is interesting when the player asked for something (a view or travel angle), when
     * the body is moving (a move bit or a speed), or when a fire gate is open, the last one is
     * what makes "I cannot move sideways while shooting" catchable at all. */
    move_input = (record != NULL) ? *(const uint32_t *)(record + PLAYER_MOVE_INPUT) : 0u;
    if (record != NULL) {
        interesting = (yaw_degrees != 0.0f) || (travel_degrees != 0.0f) ||
                      (move_input != 0u) ||
                      (read_float(record, PLAYER_CURRENT_SPEED) != 0.0f) ||
                      (read_float(record, PLAYER_FIRE_GATE_A) != 0.0f) ||
                      (read_float(record, PLAYER_CHEST_CLAIM) != 0.0f);
    } else {
        interesting = (yaw_degrees != 0.0f) || (travel_degrees != 0.0f);
    }
    if (!interesting) {
        return;
    }
    --log_state.lines_left;

    steer_lean_last_report(&lean);

    if (record == NULL) {
        log_info("steer #%d %s sub=%.5f yaw=%+.3f travel=%+.1f - NO PLAYER RECORD",
                 log_state.substep, branch_name(branch), (double)substep_seconds,
                 (double)yaw_degrees, (double)travel_degrees);
        return;
    }

    log_info("steer #%d %s mode=%d sub=%.5f yaw=%+.3f travel=%+.1f | lean %s chest=%u head=%u "
             "rate=%+.1f used=%+.2f wroteHead=%d wroteChest=%d claim=%.3f | move=0x%02X "
             "speed=%+.2f wheel=%+.2f | fire A=%.2f B=%.2f",
             log_state.substep, branch_name(branch), mode_index, (double)substep_seconds,
             (double)yaw_degrees, (double)travel_degrees,
             lean.status, (unsigned)lean.chest_node, (unsigned)lean.head_node,
             (double)lean.raw_rate, (double)lean.applied_rate,
             lean.wrote_head ? 1 : 0, lean.wrote_chest ? 1 : 0, (double)lean.chest_claim,
             (unsigned)(move_input & 0xFFu),
             (double)read_float(record, PLAYER_CURRENT_SPEED),
             (double)read_float(record, PLAYER_TURN_WHEEL),
             (double)read_float(record, PLAYER_FIRE_GATE_A),
             (double)read_float(record, PLAYER_FIRE_GATE_B));

    if (log_state.lines_left == 0) {
        log_info("SteerLog is spent. Raise SteerLog if more is needed; 0 switches it off.");
    }
}
