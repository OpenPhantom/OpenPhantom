/* mp_scene_client.c: a client is in no scene. See the header.
 *
 * Everything a client asks for goes through one plan a substep, and the plan is empty on every
 * substep in which nothing of a scene stands, which is every one but the first few after a
 * savegame saved in the middle of a scene. The line for what was given back is the release's
 * own; this file counts it for the client's report and says once when a lock has to wait.
 */
#include "mp_scene_client.h"

#include "mp_bridge_drain.h"
#include "mp_scene_free.h"
#include "mp_scene_rule.h"
#include "mp_session_now.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A line for a lock that waits for a menu this many times in a process, and after that the
 * report's count. */
#define WAIT_LINES_MAX 8u

typedef struct client_counts {
    uint32_t locks;           /* locks a scene left behind, released */
    uint32_t bars;            /* and the bars taken down */
    uint32_t cameras;         /* the camera's override cleared along with one of them */
    uint32_t modes;           /* the lock's input mode found over a lock of nought, set to play */
    uint32_t actors;          /* actors that drove this player's body, removed */
    uint32_t stores;          /* the engine's store of a parked module, cleared */
    uint32_t waited;          /* locks that stood under an open menu and went after its close */
} client_counts_t;

typedef struct client_state {
    bool            installed;
    bool            bound;        /* the lock's release resolved */
    bool            waiting;      /* a lock stands under an open menu, and the wait is counted */
    uint32_t        wait_lines;
    client_counts_t n;
} client_state_t;

static client_state_t cs;

bool mp_scene_client_install(void)
{
    if (!cs.installed) {
        cs.installed = true;
        cs.bound     = mp_scene_free_install();
    }
    return cs.bound;
}

/* A lock under an open menu, counted and said once for as long as it stands there. */
static void note_a_wait(bool waits, int32_t level)
{
    if (!waits) {
        cs.waiting = false;
        return;
    }
    if (cs.waiting) {
        return;
    }
    cs.waiting = true;
    ++cs.n.waited;
    if (cs.wait_lines < WAIT_LINES_MAX) {
        ++cs.wait_lines;
        log_info("a lock a scene left behind stands at level %d under an open menu of the "
                 "engine, whose close would put the lock's input mode back over it; it is "
                 "released on the first substep after the menu closes", (int)level);
    }
}

void mp_scene_client_tick(uint32_t substep)
{
    mp_scene_free_look_t look;
    uint32_t             plan;
    uint32_t             given;
    uint8_t              left = 0u;

    if (!cs.installed || !mp_session_now_client_of_a_started_session(NULL, NULL)) {
        return;
    }
    mp_scene_free_look(&look, substep);
    plan = mp_scene_free_plan(&look, MP_SCENE_FREE_CLIENT_ASKS, MP_SCENE_LOCK_LEVEL, &left);
    note_a_wait((left & MP_SCENE_FREE_LEFT_MENU) != 0u, look.lock_level);
    if (plan == 0u) {
        return;
    }
    given = mp_scene_free_now(plan, substep);
    cs.n.locks   += (given & MP_SCENE_FREE_LOCK) != 0u ? 1u : 0u;
    cs.n.bars    += (given & MP_SCENE_FREE_BARS) != 0u ? 1u : 0u;
    cs.n.cameras += (given & MP_SCENE_FREE_CAMERA) != 0u ? 1u : 0u;
    cs.n.modes   += (given & MP_SCENE_FREE_INPUT_MODE) != 0u ? 1u : 0u;
    cs.n.actors  += (given & MP_SCENE_FREE_MODULE) != 0u ? 1u : 0u;
    cs.n.stores  += (given & MP_SCENE_FREE_STORE) != 0u ? 1u : 0u;
}

void mp_scene_client_leave(void)
{
    cs.waiting = false;
}

void mp_scene_client_report(void)
{
    if (!cs.installed) {
        return;
    }
    if (mp_bridge_drain_is_client() || cs.n.locks != 0u || cs.n.bars != 0u ||
        cs.n.actors != 0u) {
        log_info("  the scenes (a client): %u lock(s) released, %u time(s) the bars taken down, "
                 "%u camera override(s) cleared, %u input mode(s) put back, %u actor(s) that "
                 "drove this player's body removed, %u store(s) cleared; %u release(s) waited "
                 "for a menu to close%s",
                 (unsigned)cs.n.locks, (unsigned)cs.n.bars, (unsigned)cs.n.cameras,
                 (unsigned)cs.n.modes, (unsigned)cs.n.actors, (unsigned)cs.n.stores,
                 (unsigned)cs.n.waited,
                 cs.bound ? "" : " (the lock's release is not bound, so a lock stays)");
    }
    mp_scene_free_report();
}
