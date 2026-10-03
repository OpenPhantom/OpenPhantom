/* mp_repair_lock.c: "Repair lock", the first of the developer menu's two buttons, carried out.
 * See the header.
 *
 * Two things about the code are worth having in front of a maintainer:
 *
 * Everything runs from the frame pump, between two substeps. No bank window is open there, so
 * the player's record is this machine's own, and the engine is outside its walk of the actor
 * list, which is what the release of a scene's hold asks of its caller.
 *
 * The two looks after a press on a host are timed on the world's own clock, not on a count of
 * substeps: the count the pump can read advances only while a peer is joined, and a host nobody
 * has joined yet repairs as well. The world's clock stands while no substep runs, so a look is
 * never taken across a pause and then read as an actor that does not tick.
 */
#include "mp_repair_lock.h"

#include "mp_armed.h"
#include "mp_bridge_drain.h"
#include "mp_chat_input.h"
#include "mp_scene_bind.h"
#include "mp_scene_claim.h"
#include "mp_scene_free.h"
#include "mp_scene_host.h"
#include "mp_seat.h"

#include "common/logging.h"
#include "common/player_help_note.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The lowest lock level the button counts: one, a menu's or a conversation's own level, which
 * can be left standing with nothing behind it. A script's scene takes five. */
#define REPAIR_MIN_LOCK 1

/* How many placements the latch's line names. */
#define LATCH_KEYS_MAX 8u

/* Room for a list of names in one line of the log: every part of a repair at once is a little
 * over three hundred characters. */
#define WORDS_BYTES 384u

typedef struct repair_counts {
    uint32_t let_go;           /* presses that let go of something */
    uint32_t nothing;          /* presses that found nothing to do */
    uint32_t refused;          /* presses refused at the door */
    uint32_t not_carried_out;  /* presses whose plan the engine carried out none of */
    uint32_t left_scene;
    uint32_t left_teleport;
    uint32_t closed_chat;
    uint32_t dropped_holds;
    uint32_t latches;
    uint32_t looks_again;      /* second looks at an actor that was told to leave */
    uint32_t still_driven;     /* of those, the actor still drove the body */
    uint32_t left_stopped;     /* and the actor gone with the module still stopped */
    uint32_t looks_dropped;    /* looks owed in a world that had gone, or to an earlier press */
} repair_counts_t;

/* What a repair on a host looks at again, and the world's clock at the press. */
typedef struct repair_after {
    bool     actor;     /* an actor was told to leave */
    bool     latch;     /* the latch was armed */
    uint32_t world;
    float    seconds;
} repair_after_t;

typedef struct repair_state {
    repair_after_t  after;
    repair_counts_t n;
} repair_state_t;

static repair_state_t rs;

/* ==============================================================================================
 * A list of names for one line of the log. It stops at its room and never writes past it.
 * ============================================================================================ */

typedef struct words {
    char     text[WORDS_BYTES];
    size_t   used;
    uint32_t parts;
} words_t;

typedef struct part {
    uint32_t    bit;
    const char *words;
} part_t;

static void add(words_t *words, const char *part)
{
    words->used += text_format(words->text + words->used, sizeof words->text - words->used,
                               "%s%s", words->parts != 0u ? ", " : "", part);
    ++words->parts;
}

static void name_the_bits(words_t *words, const part_t *parts, size_t count, uint32_t bits)
{
    size_t i;

    for (i = 0u; i < count; ++i) {
        if ((bits & parts[i].bit) != 0u) {
            add(words, parts[i].words);
        }
    }
}

static const char *or_nothing(const words_t *words)
{
    return words->parts != 0u ? words->text : "nothing";
}

/* What a repair let go of, in the order it does it, over the answer's `released` word. */
static const part_t WENT[] = {
    { MP_REPAIR_LEFT_SCENE,       "what the mod held of a scene of the host's" },
    { MP_REPAIR_LEFT_TELEPORT,    "a teleport under way" },
    { MP_REPAIR_CLOSED_CHAT,      "the chat's open line" },
    { MP_REPAIR_DROPPED_PAUSE,    "the pause's input hold with no menu open" },
    { MP_REPAIR_DROPPED_CHAT,     "the chat's input hold with no line open" },
    { MP_SCENE_FREE_MODULE,       "the actor that drove the player's body, removed" },
    { MP_SCENE_FREE_ACTOR,
      "the actor that drove the player's body, told to leave, which ends its script" },
    { MP_SCENE_FREE_CAMERA,       "the camera's override (whether one stood is not read)" },
    { MP_SCENE_FREE_LOCK,         "the lock" },
    { MP_SCENE_FREE_INPUT_MODE,   "the input mode" },
    { MP_SCENE_FREE_BARS,         "the bars" },
    { MP_SCENE_FREE_STORE,        "the engine's store of a parked module" },
    { MP_SCENE_FREE_MODULE_ALONE, "a stopped module nobody drove" },
};

/* And what the plan left standing, over the reasons it gives. */
static const part_t STAYED[] = {
    { MP_SCENE_FREE_LEFT_MENU,         "the lock under an open menu" },
    { MP_SCENE_FREE_LEFT_CONVERSATION, "what an open conversation's answers hold" },
    { MP_SCENE_FREE_LEFT_BANK,         "the player's record inside a bank window" },
    { MP_SCENE_FREE_LEFT_OVERLAY,      "a stopped module the developer menu holds" },
    { MP_SCENE_FREE_LEFT_GUN,          "the camera at the tripod gun" },
    { MP_SCENE_FREE_LEFT_DEAD,         "the camera of a dead player" },
    { MP_SCENE_FREE_LEFT_UNBOUND,      "the camera, whose clearing is not bound" },
};

/* ==============================================================================================
 * The press.
 * ============================================================================================ */

static bool pause_hold_stands(void)
{
    return (mp_armed_holders() & (uint32_t)MP_ARMED_HOLDER_PAUSE) != 0u;
}

/* The mod's own state first, so that nothing of it raises or holds again in the next substep.
 * Answers what was left, as MP_REPAIR_* bits. */
static uint32_t leave_what_the_mod_holds(bool is_client, bool left_a_teleport)
{
    const bool     typing = mp_chat_input_is_typing();
    const uint32_t before = mp_armed_holders();
    uint32_t       left   = 0u;
    uint32_t       orphans;

    if (!is_client && mp_scene_host_repair()) {
        left |= MP_REPAIR_LEFT_SCENE;
        ++rs.n.left_scene;
    }
    if (left_a_teleport) {
        left |= MP_REPAIR_LEFT_TELEPORT;
        ++rs.n.left_teleport;
    }
    /* The chat's one way out, for an open line and for the hold a closing key left behind. */
    mp_chat_input_close(MP_CHAT_CLOSE_REPAIR);
    if (typing) {
        left |= MP_REPAIR_CLOSED_CHAT;
        ++rs.n.closed_chat;
    } else if ((before & (uint32_t)MP_ARMED_HOLDER_CHAT) != 0u) {
        left |= MP_REPAIR_DROPPED_CHAT;
    }
    orphans = mp_repair_orphans(mp_armed_holders(), mp_scene_bind_menu_open(),
                                mp_chat_input_is_typing());
    if ((orphans & (uint32_t)MP_ARMED_HOLDER_PAUSE) != 0u) {
        (void)mp_armed_hold_input(MP_ARMED_HOLDER_PAUSE, false);
        left |= MP_REPAIR_DROPPED_PAUSE;
    }
    if ((orphans & (uint32_t)MP_ARMED_HOLDER_CHAT) != 0u) {
        (void)mp_armed_hold_input(MP_ARMED_HOLDER_CHAT, false);
        left |= MP_REPAIR_DROPPED_CHAT;
    }
    if ((left & (MP_REPAIR_DROPPED_PAUSE | MP_REPAIR_DROPPED_CHAT)) != 0u) {
        ++rs.n.dropped_holds;
    }
    return left;
}

/* The looks a host's repair owes: at the actor it told to leave, and at what the latch caught.
 * A press replaces whatever an earlier press still waited for. */
static void remember_to_look_again(uint32_t given, bool latched)
{
    if (rs.after.actor || rs.after.latch) {
        ++rs.n.looks_dropped;
    }
    memset(&rs.after, 0, sizeof rs.after);
    rs.after.actor = (given & MP_SCENE_FREE_ACTOR) != 0u;
    rs.after.latch = latched;
    if ((rs.after.actor || rs.after.latch) &&
        !mp_seat_world_now(&rs.after.world, &rs.after.seconds)) {
        ++rs.n.looks_dropped;   /* no world clock to measure the wait against */
        memset(&rs.after, 0, sizeof rs.after);
    }
}

static void say_the_repair(bool is_client, uint32_t plan, uint32_t given, uint32_t of_the_mod,
                           uint8_t left_because)
{
    const char    *who    = is_client ? "a client" : "the host";
    const uint32_t missed = mp_repair_not_carried_out(plan, given);
    const bool     camera = (given & MP_SCENE_FREE_CAMERA) != 0u;
    words_t        went;
    words_t        stayed;
    char           undone[48];

    memset(&went, 0, sizeof went);
    memset(&stayed, 0, sizeof stayed);
    name_the_bits(&stayed, STAYED, sizeof STAYED / sizeof STAYED[0], left_because);
    if (pause_hold_stands()) {
        add(&stayed, "the pause menu's input hold, its menu is open");
    }
    if (missed != 0u) {
        (void)text_format(undone, sizeof undone, "planned and not carried out: %04X",
                          (unsigned)missed);
        add(&stayed, undone);
    }
    if (mp_repair_found_nothing(given, of_the_mod)) {
        const char *of_the_camera =
            camera ? "the camera's override was cleared (whether one stood is not read)"
                   : "the camera's override was not cleared";

        if (stayed.parts == 0u) {
            log_info("repair lock: nothing holds this player; %s", of_the_camera);
        } else if (missed != 0u) {
            log_warning("repair lock (%s): nothing was let go; %s; left as it is: %s", who,
                        of_the_camera, or_nothing(&stayed));
        } else {
            log_info("repair lock (%s): nothing was let go; %s; left as it is: %s", who,
                     of_the_camera, or_nothing(&stayed));
        }
        return;
    }
    name_the_bits(&went, WENT, sizeof WENT / sizeof WENT[0],
                  mp_repair_released(given, of_the_mod));
    if (missed != 0u) {
        log_warning("repair lock (%s): let go of %s; left as it is: %s", who, or_nothing(&went),
                    or_nothing(&stayed));
    } else {
        log_info("repair lock (%s): let go of %s; left as it is: %s", who, or_nothing(&went),
                 or_nothing(&stayed));
    }
    if (mp_repair_only_left_the_mod(of_the_mod, given)) {
        log_info("repair lock (the host): the engine held nothing of that scene yet, the host "
                 "was still on his way to it; it plays from here as the engine takes it, and a "
                 "second press lets go of its actor");
    }
}

mp_player_help_verdict_t mp_repair_lock_press(bool overlay_holds, bool left_a_teleport,
                                              uint32_t substeps, uint16_t *released)
{
    const bool               is_client = mp_bridge_drain_is_client();
    mp_scene_free_look_t     look;
    mp_player_help_verdict_t verdict;
    uint32_t                 of_the_mod;
    uint32_t                 plan;
    uint32_t                 given;
    uint8_t                  left_because = 0u;
    bool                     latched;

    if (released != NULL) {
        *released = 0u;
    }
    verdict.outcome = PLAYER_HELP_OUTCOME_REFUSED;
    if (mp_repair_refused(mp_seat_level_running(), mp_scene_free_install(), &verdict.reason)) {
        ++rs.n.refused;
        log_info("repair lock (%s): refused, %s", is_client ? "a client" : "the host",
                 mp_player_help_reason_text(verdict.reason));
        return verdict;
    }
    mp_scene_free_look(&look, substeps);
    /* The button acts for a client whether or not its session counts as started: one answer to
     * "a client" for what is asked and for what is planned. */
    look.client        = is_client;
    look.button        = true;
    look.overlay_holds = overlay_holds;
    of_the_mod = leave_what_the_mod_holds(is_client, left_a_teleport);
    plan       = mp_scene_free_plan(&look, mp_repair_asks(is_client), REPAIR_MIN_LOCK,
                                    &left_because);
    given      = mp_scene_free_now(plan, substeps);
    latched    = mp_repair_latches(is_client, given);
    if (latched) {
        /* Writes down who had taken by now and the hero sent away, lets every mark fall and
         * opens the window. */
        mp_scene_host_latch(substeps);
        ++rs.n.latches;
    } else if (!is_client && ((of_the_mod & MP_REPAIR_LEFT_SCENE) != 0u ||
                              (given & MP_SCENE_FREE_CAMERA) != 0u)) {
        /* Nothing was taken back that an actor would raise again, so nobody is written down;
         * what the takers took is no longer taken all the same, and an actor that kept its
         * mark would later give back another scene's. */
        mp_scene_claim_forget_marks();
    }
    remember_to_look_again(given, latched);
    /* A pause hold that still stands has its menu open: without one it fell a moment ago. */
    verdict = mp_repair_verdict(plan, given, of_the_mod, left_because, pause_hold_stands());
    switch (verdict.outcome) {
    case PLAYER_HELP_OUTCOME_NOTHING:
        ++rs.n.nothing;
        break;
    case PLAYER_HELP_OUTCOME_REFUSED:
        ++rs.n.not_carried_out;
        break;
    default:
        ++rs.n.let_go;
        break;
    }
    say_the_repair(is_client, plan, given, of_the_mod, left_because);
    if (released != NULL) {
        *released = mp_repair_released(given, of_the_mod);
    }
    return verdict;
}

/* ==============================================================================================
 * The two looks after a press on a host.
 * ============================================================================================ */

/* The actor that was told to leave, looked at again once the world's clock has moved past its
 * next tick. */
static void look_at_the_actor_again(uint32_t substeps, float waited)
{
    mp_scene_free_look_t look;

    mp_scene_free_look(&look, substeps);
    ++rs.n.looks_again;
    switch (mp_repair_actor_after(&look)) {
    case MP_REPAIR_ACTOR_STILL_DRIVES:
        ++rs.n.still_driven;
        log_warning("repair lock (the host): the actor that was told to leave still drives the "
                    "player's body %.2f s of the world's time later, with the player module "
                    "stopped: the actors do not tick, so the engine has not removed it, and the "
                    "player stays held until they tick again", (double)waited);
        break;
    case MP_REPAIR_ACTOR_LEFT_STOPPED:
        ++rs.n.left_stopped;
        log_warning("repair lock (the host): the actor that was told to leave is gone and the "
                    "player module still stands stopped %.2f s of the world's time later; a "
                    "second press sets a stopped module nobody drives running", (double)waited);
        break;
    case MP_REPAIR_ACTOR_LEFT:
    default:
        log_info("repair lock (the host): the actor that was told to leave is gone and the "
                 "player module runs, %.2f s of the world's time after the press",
                 (double)waited);
        break;
    }
}

static void say_what_the_latch_caught(void)
{
    uint32_t keys[LATCH_KEYS_MAX];
    size_t   caught;
    size_t   i;
    words_t  named;
    char     one[16];

    memset(keys, 0, sizeof keys);
    memset(&named, 0, sizeof named);
    caught = mp_scene_host_latched(keys, LATCH_KEYS_MAX);
    if (caught == 0u) {
        log_info("repair lock (the host): no actor opened a door of a scene in the latch's "
                 "window after the press, so no placement lost its doors");
        return;
    }
    for (i = 0u; i < caught && i < LATCH_KEYS_MAX; ++i) {
        (void)text_format(one, sizeof one, "%u", (unsigned)keys[i]);
        add(&named, one);
    }
    log_info("repair lock (the host): %u placement(s) opened a door of a scene in the latch's "
             "window after the press, and their doors are refused until the level ends: %s%s",
             (unsigned)caught, named.text, caught > LATCH_KEYS_MAX ? " and more" : "");
}

/* One of the two looks, by the world's clock. True when it is over, taken or dropped; `due`
 * says which, and `waited` how much of the world's time has passed since the press. */
static bool after_is_over(float wait_seconds, bool *due, float *waited)
{
    uint32_t world = 0u;
    float    now   = 0.0f;
    bool     reads = mp_seat_world_now(&world, &now);

    *due    = false;
    *waited = now - rs.after.seconds;
    switch (mp_repair_after(reads, world == rs.after.world, rs.after.seconds, now,
                            wait_seconds)) {
    case MP_REPAIR_AFTER_DUE:
        *due = true;
        return true;
    case MP_REPAIR_AFTER_DROP:
        ++rs.n.looks_dropped;
        return true;
    case MP_REPAIR_AFTER_WAIT:
    default:
        return false;
    }
}

void mp_repair_lock_frame(uint32_t substeps)
{
    bool  due    = false;
    float waited = 0.0f;

    if (rs.after.actor && after_is_over(MP_REPAIR_LOOK_AGAIN_SECONDS, &due, &waited)) {
        rs.after.actor = false;
        if (due) {
            look_at_the_actor_again(substeps, waited);
        }
    }
    if (rs.after.latch && after_is_over(MP_REPAIR_LATCH_SECONDS, &due, &waited)) {
        rs.after.latch = false;
        if (due) {
            say_what_the_latch_caught();
        }
    }
}

void mp_repair_lock_session_ended(void)
{
    if (rs.after.actor || rs.after.latch) {
        ++rs.n.looks_dropped;
    }
    memset(&rs.after, 0, sizeof rs.after);
}

void mp_repair_lock_report(void)
{
    const repair_counts_t *n = &rs.n;

    log_info("  the player's help, the repairs: %u let go of something, %u found nothing to do, "
             "%u refused at the door, %u with a plan the engine carried out none of; %u left a "
             "scene of the host's, %u a teleport under way, "
             "%u closed the chat's line, %u dropped an input hold nobody owned; %u armed the "
             "latch, %u looked again at an actor told to leave, %u of them found it still "
             "driving and %u gone with the module stopped, %u look(s) dropped with their world "
             "or by a later press",
             (unsigned)n->let_go, (unsigned)n->nothing, (unsigned)n->refused,
             (unsigned)n->not_carried_out,
             (unsigned)n->left_scene, (unsigned)n->left_teleport, (unsigned)n->closed_chat,
             (unsigned)n->dropped_holds, (unsigned)n->latches, (unsigned)n->looks_again,
             (unsigned)n->still_driven, (unsigned)n->left_stopped, (unsigned)n->looks_dropped);
}
