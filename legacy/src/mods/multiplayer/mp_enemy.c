/* mp_enemy.c: stop the AI on one machine, and count what is alive on both.
 *
 * Three cells and no site of its own. The suspend cell and the two counters all fall out of
 * operands of sites the feature already resolves for other reasons, which is why this file adds
 * no scanning and cannot fail in a new way.
 *
 * The two counters come out of one comparison inside the enemy pass, `if (live > peak) peak =
 * live`, which names both cells twice each, four operands, two cells, one pattern:
 *
 *     00433329  A1 00208800       mov eax,[g_liveCount]
 *     0043332E  3B 05 20208800    cmp eax,[g_liveCountPeak]
 *     00433334  7E 0C             jle +0x0C
 *     00433336  8B 0D 00208800    mov ecx,[g_liveCount]
 *     0043333C  89 0D 20208800    mov [g_liveCountPeak],ecx
 *
 * Those five instructions are not unique: the same idiom sits at 00473E2C over another pair of
 * cells. The pattern is 30 bytes rather than 25 because it carries the load that follows, whose
 * opcode differs (A1 against 8B) and whose operand is masked, being an address; with it the count
 * is one in all six checked images, the recompile included. The suspend cell is the first read
 * the AI pass makes before it returns, which is why a level and not an edge is what matters.
 *
 * The switch is written as a LEVEL rather than as an edge: every substep the cell is read and only
 * written when it disagrees. That costs one read per substep and it survives anything else in the
 * process writing the cell, which matters because this module does not own it. Every write is
 * counted, so a log that shows thousands of them is telling you somebody else is writing it back.
 *
 * Nothing here replicates anything yet. What it produces is two numbers and one observation, and
 * all three are inputs to a design rather than parts of one.
 */
#include "mp_enemy.h"

#include "mp_cells.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mp_enemy_state {
    bool                    installed;
    mp_enemy_suspend_mode_t mode;
    bool                    is_client;
    bool                    role_known;

    uintptr_t               suspend;
    uintptr_t               live;
    uintptr_t               peak;

    bool                    want;        /* what this side wants the switch to say */
    uint32_t                writes;      /* times the cell disagreed and was put back */
    uint32_t                read_faults;

    uint32_t                last_live;
    uint32_t                last_peak;
    uint32_t                most_live;   /* the highest live count this module itself has seen */
    uint32_t                samples;
} mp_enemy_state_t;

static mp_enemy_state_t enemy;

bool mp_enemy_install(mp_enemy_suspend_mode_t mode)
{
    enemy.mode    = mode;
    enemy.suspend = mp_cells_address(MP_CELL_ENEMY_SUSPEND);
    enemy.live    = mp_cells_address(MP_CELL_ENEMY_LIVE);
    enemy.peak    = mp_cells_address(MP_CELL_ENEMY_LIVE_PEAK);

    if (enemy.live == 0u || enemy.peak == 0u) {
        log_warning("the enemy module has no counters: the live count or its peak did not resolve, "
                    "so this run cannot say how many actors a level really holds");
    }
    if (mode != MP_ENEMY_SUSPEND_OFF && enemy.suspend == 0u) {
        log_warning("the enemy module cannot suspend the AI: the switch did not resolve, so this "
                    "run measures a world with its enemies running");
        enemy.mode = MP_ENEMY_SUSPEND_OFF;
    }
    enemy.installed = enemy.suspend != 0u || enemy.live != 0u;
    return enemy.installed;
}

void mp_enemy_set_client(bool is_client)
{
    enemy.is_client  = is_client;
    enemy.role_known = true;
}

/* What this side wants the switch to say. A mode that depends on the role answers "running" until
 * the role is known, because turning the world off on a machine whose part in the session is not
 * settled yet would be a guess with a visible consequence. */
static bool wanted(void)
{
    switch (enemy.mode) {
    case MP_ENEMY_SUSPEND_ALWAYS:
        return true;
    case MP_ENEMY_SUSPEND_WHEN_CLIENT:
        return enemy.role_known && enemy.is_client;
    case MP_ENEMY_SUSPEND_OFF:
    default:
        return false;
    }
}

void mp_enemy_tick(void)
{
    uint32_t value = 0;

    if (!enemy.installed) {
        return;
    }

    enemy.want = wanted();
    if (enemy.suspend != 0u) {
        if (!memory_try_read(enemy.suspend, &value, sizeof value)) {
            ++enemy.read_faults;
        } else if ((value != 0u) != enemy.want) {
            uint32_t put = enemy.want ? 1u : 0u;

            if (memory_try_write(enemy.suspend, &put, sizeof put)) {
                ++enemy.writes;
            } else {
                ++enemy.read_faults;
            }
        }
    }

    if (enemy.live != 0u && memory_try_read(enemy.live, &value, sizeof value)) {
        enemy.last_live = value;
        if (value > enemy.most_live) {
            enemy.most_live = value;
        }
        ++enemy.samples;
    }
    if (enemy.peak != 0u && memory_try_read(enemy.peak, &value, sizeof value)) {
        enemy.last_peak = value;
    }
}

uint32_t mp_enemy_live(void)      { return enemy.last_live; }
uint32_t mp_enemy_live_peak(void) { return enemy.last_peak; }

void mp_enemy_report(void)
{
    static const char *const MODE[] = { "left running", "stopped on this instance",
                                        "stopped on the client" };

    if (!enemy.installed) {
        return;
    }
    log_info("  the enemies: the AI is %s and this side %s it; %u live now, engine peak %u, "
             "highest this module saw %u over %u sample(s); %u write(s) to the switch, %u fault(s)",
             MODE[enemy.mode <= MP_ENEMY_SUSPEND_WHEN_CLIENT ? (size_t)enemy.mode : 0u],
             enemy.want ? "is holding" : "is not holding",
             (unsigned)enemy.last_live, (unsigned)enemy.last_peak, (unsigned)enemy.most_live,
             (unsigned)enemy.samples, (unsigned)enemy.writes, (unsigned)enemy.read_faults);
}
