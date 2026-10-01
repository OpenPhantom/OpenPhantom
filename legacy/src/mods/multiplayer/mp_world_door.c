/* mp_world_door.c: the doors out of one world and into another, held shut on a client.
 *
 * Which world everybody is in is the host's to decide: it is announced in the setup note, and a
 * client follows it there. There were two ways for a client to leave that agreement without
 * anybody noticing, and both of them looked to the player like an ordinary thing to do.
 *
 * The first is the load button, which the pause screen and the death screen both offer. It goes
 * through one wrapper that broadcasts 6 and restores the slot, and nothing asked whether this side
 * was in a session: the client loaded its own savegame, kept the socket, and from then on the two
 * machines were in different worlds while every counter said the session was fine.
 *
 * The second is the script director. On a client the enemies the host lists are parked, but an
 * actor the client's own activation scan woke and the host has not listed yet runs its script
 * locally, and 18 of those scripts in the shipped levels carry command 1, which ends the level.
 * The client then walks into the next level on its own.
 *
 * Both are held rather than repaired afterwards, and both say so: a refusal that leaves no trace is
 * a silent exit, and a silent exit is a blind spot.
 *
 * The director's hull is also where every other module that owns a class of its commands hears
 * them. The class of a command comes from one table (mp_director_rule.h); a module hands in one
 * function for its class, and a class nobody took goes to the engine exactly as it did before.
 */
#include "mp_world_door.h"

#include "mp_director_rule.h"
#include "mp_level_state_bind.h"
#include "mp_session_now.h"
#include "mp_signatures_door.h"
#include "mp_text.h"
#include "mp_wallclock.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* The one of the two level doors that ends the level, out of the director's jump table: command
 * 1 writes 3 into the level outcome, and the other door, 14, writes the failure and its reason. */
#define DIRECTOR_COMMAND_LEVEL_END 1

typedef int32_t(__cdecl *load_fn_t)(int32_t slot);
typedef int32_t(__cdecl *director_fn_t)(void *actor, int32_t command, int32_t a1, int32_t a2);

/* What the director's hull saw of one class, on a client of a started session or elsewhere. */
typedef struct director_counts {
    uint32_t seen;
    uint32_t withheld;    /* answered 0 without the engine's arm: the door, or the class's hand */
    uint32_t to_engine;
} director_counts_t;

typedef struct world_door_state {
    bool          installed;
    detour_t      load_detour;
    detour_t      director_detour;
    load_fn_t     load_original;
    director_fn_t director_original;

    uint32_t loads_refused;
    uint32_t ends_refused;
    uint32_t failures_refused;
    uint32_t loads_let_through;
    bool     load_said;

    mp_world_door_hand_fn_t hand[MP_DIRECTOR_CLASSES];
    const char             *hand_name[MP_DIRECTOR_CLASSES];
    uint32_t                hands_refused;
    director_counts_t       director[2][MP_DIRECTOR_CLASSES];   /* [client of a session][class] */

    const char *notice;        /* mp_text's own storage, so a pointer is enough */
    uint32_t    notice_until;  /* the wall clock this line is shown until */
} world_door_state_t;

/* How long a refusal stays on the band. Long enough to be read after a button press, short enough
 * that it is gone before the player has pressed anything else. */
#define DOOR_NOTICE_MS 4000u

static world_door_state_t door;

bool mp_world_door_command_ends_level(int32_t command)
{
    return mp_director_class_of(command) == MP_DIRECTOR_LEVEL_DOOR;
}

bool mp_world_door_hand(mp_director_class_t cls, mp_world_door_hand_fn_t hand, const char *name)
{
    if ((size_t)cls >= (size_t)MP_DIRECTOR_CLASSES || cls == MP_DIRECTOR_LEVEL_DOOR ||
        hand == NULL) {
        return false;
    }
    if (door.hand[cls] != NULL && door.hand[cls] != hand) {
        ++door.hands_refused;
        log_warning("a second module asked for the director's %s commands and was refused: %s "
                    "has them", mp_director_class_name(cls),
                    door.hand_name[cls] != NULL ? door.hand_name[cls] : "another");
        return false;
    }
    door.hand[cls]      = hand;
    door.hand_name[cls] = name;
    return true;
}

/* A client that is playing in a session refuses every door; a host, a side with no session and a
 * side whose session has ended refuse none, which is retail behaviour byte for byte. The session
 * is the one the HOST said, not as this side's own bridge remembers it, read the way every gate
 * of a client reads it. */
static bool this_side_refuses(mp_world_door_t door_kind)
{
    return (size_t)door_kind < (size_t)MP_WORLD_DOOR_COUNT &&
           mp_session_now_client_of_a_started_session(NULL, NULL);
}

/* A non-zero answer is what the two screens read as "the load did not happen": both test it and
 * keep themselves up. Zero would close the screen on a level that was never loaded. */
static int32_t __cdecl hook_load_from_screen(int32_t slot)
{
    if (this_side_refuses(MP_WORLD_DOOR_LOAD)) {
        ++door.loads_refused;
        door.notice       = mp_text(MP_TEXT_HUD_HOST_LOADS);
        door.notice_until = mp_wallclock_ms() + DOOR_NOTICE_MS;
        if (!door.load_said) {
            door.load_said = true;
            log_info("this client's load of slot %d was refused: in a co-op session the host loads "
                     "the world and this side follows it there; later ones are counted",
                     (int)slot);
        }
        return 1;
    }
    ++door.loads_let_through;
    return door.load_original(slot);
}

/* The command is read, not the door, and its class says whose it is. The level doors are held
 * here; every other class goes to the one module that took it, and a class nobody took goes to the
 * engine. The answer for every arm of the engine's own body is zero, so a refusal answers zero too
 * and the script carries on exactly as it does when the arm it asked for did nothing.
 *
 * engine: int op_extraFunc(character *actor, u32 cmd, int a1, f32 a2) */
static int32_t __cdecl hook_director(void *actor, int32_t command, int32_t a1, int32_t a2)
{
    mp_director_class_t cls    = mp_director_class_of(command);
    bool                client = mp_session_now_client_of_a_started_session(NULL, NULL);
    director_counts_t  *counts = &door.director[client ? 1 : 0][cls];

    ++counts->seen;
    if (mp_world_door_command_ends_level(command)) {
        mp_world_door_t kind = command == DIRECTOR_COMMAND_LEVEL_END ? MP_WORLD_DOOR_LEVEL_END
                                                                     : MP_WORLD_DOOR_FAILURE;

        if (this_side_refuses(kind)) {
            if (kind == MP_WORLD_DOOR_LEVEL_END) {
                ++door.ends_refused;
            } else {
                ++door.failures_refused;
            }
            ++counts->withheld;
            return 0;
        }
    }
    if (door.hand[cls] != NULL && door.hand[cls](actor, command, a1, a2)) {
        ++counts->withheld;
        return 0;
    }
    ++counts->to_engine;
    return door.director_original(actor, command, a1, a2);
}

bool mp_world_door_install(void)
{
    uintptr_t load;
    uintptr_t director;

    if (door.installed) {
        return true;
    }
    (void)mp_signatures_door_resolve();
    load     = mp_signatures_door_address(MP_DOOR_SITE_LOAD_FROM_SCREEN);
    director = mp_signatures_door_address(MP_DOOR_SITE_DIRECTOR);
    if (load == 0u || director == 0u) {
        log_warning("the doors out of a world are NOT held: the load wrapper %s and the script "
                    "director %s. A client could load its own savegame or let a local script end "
                    "the level, and the two sides would be in different worlds",
                    load != 0u ? "resolved" : "did not resolve",
                    director != 0u ? "resolved" : "did not resolve");
        return false;
    }
    if (!detour_install(&door.load_detour, load, (void *)hook_load_from_screen,
                        mp_signatures_door_prologue(MP_DOOR_SITE_LOAD_FROM_SCREEN)) ||
        !detour_install(&door.director_detour, director, (void *)hook_director,
                        mp_signatures_door_prologue(MP_DOOR_SITE_DIRECTOR))) {
        log_error("a door out of a world could not be hulled, so neither is held");
        return false;
    }
    door.load_original     = (load_fn_t)door.load_detour.original;
    door.director_original = (director_fn_t)door.director_detour.original;
    door.installed         = true;
    /* The trampoline, for the two modules that replay a command of the host's past this hull. */
    mp_level_state_bind_set_director((mp_level_director_fn_t)door.director_original, director);
    log_info("the doors out of a world are held on a client: the load button at %08X and the "
             "script director at %08X, so the host decides which world everybody is in",
             (unsigned)load, (unsigned)director);
    return true;
}

const char *mp_world_door_notice(void)
{
    if (door.notice == NULL) {
        return NULL;
    }
    if ((int32_t)(mp_wallclock_ms() - door.notice_until) >= 0) {
        door.notice = NULL;
        return NULL;
    }
    return door.notice;
}

/* One line per side the hull ran on: a client of a started session, and everything else. Each
 * class says seen, withheld and handed to the engine, and how a client may replay it. */
static void report_the_director(size_t client)
{
    char   line[900];
    size_t at = 0;
    size_t cls;

    line[0] = '\0';
    for (cls = 0; cls < (size_t)MP_DIRECTOR_CLASSES && at + 1u < sizeof line; ++cls) {
        const director_counts_t *c = &door.director[client][cls];
        mp_director_replay_t     replay = mp_director_replay_of((mp_director_class_t)cls);

        at += text_format(line + at, sizeof line - at, "%s%s %u/%u/%u%s", cls == 0u ? "" : ", ",
                          mp_director_class_name((mp_director_class_t)cls), (unsigned)c->seen,
                          (unsigned)c->withheld, (unsigned)c->to_engine,
                          replay == MP_DIRECTOR_REPLAY_REPLICA    ? " (by the replica)"
                          : replay == MP_DIRECTOR_REPLAY_STAND_IN ? " (by a stand-in)"
                                                                  : "");
    }
    log_info("the director (%s): per class seen/withheld/to the engine: %s; the fog heard by %s",
             client != 0u ? "client" : "host or alone", line,
             door.hand_name[MP_DIRECTOR_FOG] != NULL ? door.hand_name[MP_DIRECTOR_FOG]
                                                     : "nobody");
}

void mp_world_door_report(void)
{
    if (!door.installed) {
        log_info("  the doors out of a world are not held, so a client can load or end its own "
                 "level and leave the session's world without saying so");
        return;
    }
    log_info("  the doors out of a world: %u load(s) refused here and %u let through, %u level "
             "end(s) and %u failure(s) held back from this side's own scripts",
             (unsigned)door.loads_refused, (unsigned)door.loads_let_through,
             (unsigned)door.ends_refused, (unsigned)door.failures_refused);
    report_the_director(0u);
    report_the_director(1u);
    if (door.hands_refused != 0u) {
        log_warning("the director: %u second hand(s) for a class were refused",
                    (unsigned)door.hands_refused);
    }
}
