/* mp_bootstrap.h: getting into the engine at a moment when the engine exists.
 *
 * A mod's entry point runs before the host's CRT start. At that moment the module registry is a
 * silent no op and the task pool is wiped later from under anything registered in it, so the
 * engine side has to be installed from inside the engine's own startup rather than from the DLL's.
 *
 * What this module installs is a foothold and nothing else: two module nodes that count what they
 * receive, and one task slot. No gameplay changes.
 */
#ifndef MULTIPLAYER_MP_BOOTSTRAP_H
#define MULTIPLAYER_MP_BOOTSTRAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Which way in was taken. Logged by name, because a fallback that is used and not named is not a
 * fallback: the next reader would take the ordinary path for granted and look for the fault
 * somewhere else entirely. */
typedef enum mp_bootstrap_path {
    MP_BOOTSTRAP_NONE,          /* nothing is armed; the engine side will never run */
    MP_BOOTSTRAP_SYS_STARTUP,   /* the ordinary way: a detour on the engine's own startup */
    MP_BOOTSTRAP_CAMPAIGN_RUN,  /* the first fallback, one step later */
    MP_BOOTSTRAP_FRAME_HOOK     /* the last resort, at the end of the first rendered frame */
} mp_bootstrap_path_t;

/* Arms the deferred entry. Returns false when no way in could be armed, in which case the feature
 * stays out of the engine entirely rather than half in it.
 *
 * `provoke_double_delivery` installs a THIRD node carrying the same procedure as the tail node. It
 * exists so that the census's ordering guard can be seen firing rather than trusted: with it on,
 * every broadcast enters the tail procedure twice and once for the head, and the census must report
 * that the message never reached the head. A guard nobody has watched fail is a guard nobody has
 * tested. Off in every ordinary build. */
bool mp_bootstrap_arm(bool provoke_double_delivery);

/* Which way in was armed, and whether the engine side has actually run. The second question is not
 * the first: arming is cheap and silent, and a foothold that was armed and never reached is the
 * failure this whole feature is most likely to produce. */
mp_bootstrap_path_t mp_bootstrap_path(void);
bool                mp_bootstrap_has_run(void);

/* What the engine side got, once it has run. All zero before that. */
typedef struct mp_bootstrap_result {
    uintptr_t tail_node;      /* the node left where module_install put it */
    uintptr_t head_node;      /* the node moved to the head, so it is the LAST to hear a
                               * broadcast */
    uintptr_t task_record;    /* what task_register handed back */
    bool      list_is_sane;   /* the walk from both ends terminated and agreed */
    size_t    module_count;   /* how many nodes the list had afterwards */
} mp_bootstrap_result_t;

const mp_bootstrap_result_t *mp_bootstrap_result(void);

/* One client from a layer above, entered at the two reporting moments (a level end and the quit)
 * after this file's own reports. The bootstrap neither knows nor cares what it prints. NULL turns
 * it off. */
void mp_bootstrap_set_report_client(void (*client)(const char *why));

/* One client entered at the quit before the reports, for whatever must still reach the far side of
 * a wire while the process is alive to send it. NULL turns it off. */
void mp_bootstrap_set_shutdown_client(void (*client)(void));

/* Called from the head module node at the END of every substep, after every task and after the
 * collision pass. It is the last moment in a substep at which anything can still have changed, so
 * it is where a sender reads. The bridge sets this; the module layer knows nothing about what a
 * substep is for. */
void mp_bootstrap_set_substep_end_client(void (*client)(void));

/* Every module message, not only the substep end. Six of them move a whole campaign bank at once,
 * and a reader that does not see them applies deltas to a bank that has just been replaced. This
 * runs on the broadcast path, so a client that is not interested in a message must return
 * cheaply. */
void mp_bootstrap_set_module_message_client(void (*client)(int msg));

/* Called from the tail module node on every frame begin (0x0c). The backward walk reaches the tail
 * first, so this runs after the frame's message pump and key hook, and before the frame's substep
 * rate is chosen and its first substep runs; in menus and in levels. NULL turns it off. */
void mp_bootstrap_set_frame_begin_client(void (*client)(void));

#endif /* MULTIPLAYER_MP_BOOTSTRAP_H */
