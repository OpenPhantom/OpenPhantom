/* player_help_row.c: see player_help_row.h. */
#include "player_help_row.h"

#include "input_freeze.h"
#include "input_owner.h"
#include "overlay_input.h"
#include "overlay_reason.h"
#include "session_lock.h"

#include "common/logging.h"
#include "common/player_help_note.h"
#include "common/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct help_state {
    /* The last press. The serial is this file's own count and is never read back out of the
     * note: the ask has one writer, and that writer is here. */
    uint32_t serial;         /* 0 while no button has been pressed in this process */
    uint8_t  kind;           /* which button it was, a PLAYER_HELP_KIND_* */
    uint8_t  said;           /* the outcome of that press the log has been told, NONE for none */

    /* The reading of the answer record. It is STICKY on a refusal, the way the session lock's
     * reading is and for the same reason: a read fails when it loses its race against a write,
     * and taking that as "nobody listens" would grey both buttons for one picture in the middle
     * of a session. */
    bool                 read;           /* `answer` holds a record this file read */
    player_help_answer_t answer;
    bool                 found_once;     /* a read has found the record, so a read is a copy now */
    bool                 missed_once;
    uint32_t             missed_at_ms;
} help_state_t;

static help_state_t help;

/* The button's name in a log line, which is its label in the panel in lower case. */
static const char *button_name(uint8_t kind)
{
    return kind == PLAYER_HELP_KIND_TELEPORT ? "teleport to host" : "repair lock";
}

/* Read only while a session runs. Without one nobody files the record, the buttons are greyed
 * whatever it says, and a module that asked anyway would run a lookup for the multiplayer in a
 * single player game. Until the first read that found the record the misses are spaced out,
 * because each one is a failed mapping lookup in the operating system: that is a session whose
 * multiplayer is of another build and never files an answer. */
static void take_the_answer(uint32_t now_ms)
{
    player_help_answer_t answer;

    if (!session_lock_running()) {
        return;
    }
    if (!help.found_once && help.missed_once &&
        now_ms - help.missed_at_ms < PLAYER_HELP_NOTE_RETRY_MS) {
        return;
    }
    if (player_help_answer_read(&answer)) {
        help.found_once = true;
        help.answer     = answer;
        help.read       = true;
        return;
    }
    if (!help.found_once) {
        help.missed_once  = true;
        help.missed_at_ms = now_ms;
    }
}

/* What the session says it would carry out. Nothing while no session runs: the record of the
 * last session stays on file and in this file's reading, and its bits say nothing about now. */
static uint8_t ready_bits(void)
{
    if (!session_lock_running() || !help.read) {
        return 0u;
    }
    return help.answer.ready;
}

/* The answer to the last press, or NULL while it has none. An answer counts when it names that
 * press's serial and kind and says something about it; the record of an older press, and one the
 * multiplayer armed on without an outcome, are no answer to this one. */
static const player_help_answer_t *answer_to_the_press(void)
{
    if (help.serial == 0u || !help.read || help.answer.serial != help.serial ||
        help.answer.kind != help.kind || help.answer.outcome == PLAYER_HELP_OUTCOME_NONE) {
        return NULL;
    }
    return &help.answer;
}

bool player_help_row_offered(uint8_t kind, uint32_t *reason)
{
    const player_help_answer_t *answer  = answer_to_the_press();
    const uint32_t              can     = (kind == PLAYER_HELP_KIND_TELEPORT)
                                              ? PLAYER_HELP_READY_CAN_TELEPORT
                                              : PLAYER_HELP_READY_CAN_REPAIR;
    uint32_t                    why     = (uint32_t)OVERLAY_REASON_NONE;
    bool                        offered = false;

    if (kind != PLAYER_HELP_KIND_REPAIR && kind != PLAYER_HELP_KIND_TELEPORT) {
        offered = false;                 /* no such button */
    } else if (!session_lock_running()) {
        why = (uint32_t)OVERLAY_REASON_NEEDS_SESSION;
    } else if (kind == PLAYER_HELP_KIND_TELEPORT && session_lock_is_host()) {
        /* Before the bit is asked for: the host is the host whether or not anybody listens, and
         * that is the answer a player on that machine can do something with. */
        why = (uint32_t)OVERLAY_REASON_IS_HOST;
    } else if ((ready_bits() & can) == 0u) {
        /* A session runs and its multiplayer does not answer for this button: it is of another
         * build, the sites the button needs did not resolve, or its reader is not armed yet. No
         * reason is named, so the row reads `n/a`, the panel's word for what this build cannot
         * do. "Only in a multiplayer session" would be a wrong sentence in a session. */
        why = (uint32_t)OVERLAY_REASON_NONE;
    } else if (answer != NULL && answer->outcome == PLAYER_HELP_OUTCOME_OPEN) {
        /* The last press was this button and is still being worked on. `answer` is the answer to
         * the last press, so its kind is this press's kind and the other button stays offered:
         * a repair is what ends a teleport that waits. */
        if (help.kind == kind) {
            why = (uint32_t)OVERLAY_REASON_UNDER_WAY;
        } else {
            offered = true;
        }
    } else {
        offered = true;
    }
    if (reason != NULL) {
        *reason = why;
    }
    return offered;
}

bool player_help_row_press(uint8_t kind)
{
    player_help_ask_t ask;
    uint32_t          serial;

    if (!player_help_row_offered(kind, NULL)) {
        return false;
    }
    /* The panel goes first, and the free camera is the one thing that keeps it: while the camera
     * flies, closing the panel lets the player's input go under a camera that reads the same
     * keys (input_owner.c), so the panel stays and the flag below says the player is held. */
    if (!input_owner_free_camera_holds_panel()) {
        overlay_input_close();
    }

    /* Never 0, which is the empty ask's, so the count steps over it when it wraps. */
    serial = help.serial + 1u;
    if (serial == 0u) {
        serial = 1u;
    }
    memset(&ask, 0, sizeof ask);
    ask.version = PLAYER_HELP_NOTE_VERSION;
    ask.kind    = kind;
    ask.serial  = serial;
    /* Asked AFTER the close, which let the panel's own holder go. Whoever holds now holds on. */
    if (input_freeze_holders() != 0u) {
        ask.flags = PLAYER_HELP_ASK_F_OVERLAY_HOLDS;
    }
    if (!player_help_ask_publish(&ask)) {
        log_warning("player help: %s could not be asked of the session, the note was refused",
                    button_name(kind));
        return false;
    }
    help.serial = serial;
    help.kind   = kind;
    help.said   = PLAYER_HELP_OUTCOME_NONE;
    log_info("player help: %s is asked of the session, press %u%s", button_name(kind),
             (unsigned)serial,
             ask.flags != 0u ? ", and the overlay still holds the player" : "");
    return true;
}

/* Why the session did not carry a press out, short enough to stand behind "no teleport, " on
 * one line of the panel. NULL for no reason and for one this build does not know. */
static const char *reason_words(uint8_t reason)
{
    switch (reason) {
    case PLAYER_HELP_REASON_NO_LEVEL:         return "no level is running";
    case PLAYER_HELP_REASON_DEAD:             return "you are dead";
    case PLAYER_HELP_REASON_AT_A_GUN:         return "leave the gun first";
    case PLAYER_HELP_REASON_BUSY:             return "you are being moved";
    case PLAYER_HELP_REASON_IS_HOST:          return "you are the host";
    case PLAYER_HELP_REASON_HOST_ELSEWHERE:   return "host in another level";
    case PLAYER_HELP_REASON_HOST_HAS_NO_BODY: return "the host has no body yet";
    case PLAYER_HELP_REASON_HOST_DEAD:        return "the host is dead";
    case PLAYER_HELP_REASON_NEAR_ALREADY:     return "you are beside the host";
    case PLAYER_HELP_REASON_NO_SEAT:          return "no room beside the host";
    case PLAYER_HELP_REASON_OVERLAY_HOLDS:    return "the free camera flies";
    case PLAYER_HELP_REASON_NOT_BOUND:        return "not on this executable";
    case PLAYER_HELP_REASON_GAVE_UP:          return "it gave up waiting";
    case PLAYER_HELP_REASON_MENU_OPEN:        return "a menu is open";
    case PLAYER_HELP_REASON_CONVERSATION:     return "a conversation is open";
    case PLAYER_HELP_REASON_WORLD_CHANGED:    return "the level changed";
    default:                                  return NULL;
    }
}

/* A press that found nothing to do and one that was refused read alike once there is a reason:
 * "no teleport, you are the host" needs no word for which of the two it was, and the room is
 * thirty seven characters. Without a reason the two are told apart, because nothing to do is
 * the answer the repair button gives on an ordinary day and must not read as a failure. A
 * repair that was done carries a reason when it left something standing. */
void player_help_row_sentence(uint8_t kind, uint8_t outcome, uint8_t reason, char *out,
                              size_t size)
{
    const bool  teleport = kind == PLAYER_HELP_KIND_TELEPORT;
    const char *why      = reason_words(reason);
    const char *words;

    switch (outcome) {
    case PLAYER_HELP_OUTCOME_OPEN:
        words = teleport ? "teleport is on its way" : "repair lock is still at work";
        break;
    case PLAYER_HELP_OUTCOME_DONE:
        /* A repair that let go of something and left something standing says why the rest
         * stayed: the player is still held, and by what is the one thing he can act on. */
        if (!teleport && why != NULL) {
            (void)text_format(out, size, "freed, but %s", why);
            return;
        }
        words = teleport ? "teleport put you beside the host"
                         : "repair lock let go of what held you";
        break;
    case PLAYER_HELP_OUTCOME_NOTHING:
    case PLAYER_HELP_OUTCOME_REFUSED:
        if (why != NULL) {
            (void)text_format(out, size, "%s, %s", teleport ? "no teleport" : "no repair", why);
            return;
        }
        if (outcome == PLAYER_HELP_OUTCOME_NOTHING) {
            words = teleport ? "teleport had nothing to do"
                             : "repair lock found nothing holding you";
        } else {
            words = teleport ? "teleport was refused" : "repair lock was refused";
        }
        break;
    default:
        words = teleport ? "teleport asked, no answer yet" : "repair lock asked, no answer yet";
        break;
    }
    (void)text_format(out, size, "%s", words);
}

bool player_help_row_last(char *out, size_t size, bool *refused)
{
    const player_help_answer_t *answer = answer_to_the_press();

    if (help.serial == 0u) {
        return false;
    }
    if (refused != NULL) {
        *refused = answer != NULL && answer->outcome == PLAYER_HELP_OUTCOME_REFUSED;
    }
    if (out != NULL && size != 0u) {
        player_help_row_sentence(help.kind,
                                 answer != NULL ? answer->outcome : PLAYER_HELP_OUTCOME_NONE,
                                 answer != NULL ? answer->reason : PLAYER_HELP_REASON_NONE, out,
                                 size);
    }
    return true;
}

void player_help_row_tick(uint32_t now_ms)
{
    const player_help_answer_t *answer;
    char                        words[PLAYER_HELP_ROW_WORDS_MAX];

    take_the_answer(now_ms);

    /* Once per outcome of a press, so a teleport says when it was taken and again when it ended.
     * The panel closed at the press, so this line is where the answer is first seen. */
    answer = answer_to_the_press();
    if (answer == NULL || answer->outcome == help.said) {
        return;
    }
    help.said = answer->outcome;
    player_help_row_sentence(help.kind, answer->outcome, answer->reason, words, sizeof words);
    if (answer->outcome == PLAYER_HELP_OUTCOME_REFUSED) {
        log_warning("player help: press %u is answered: %s (outcome %u, reason %u, released "
                    "0x%04X)", (unsigned)help.serial, words, (unsigned)answer->outcome,
                    (unsigned)answer->reason, (unsigned)answer->released);
    } else {
        log_info("player help: press %u is answered: %s (outcome %u, reason %u, released "
                 "0x%04X)", (unsigned)help.serial, words, (unsigned)answer->outcome,
                 (unsigned)answer->reason, (unsigned)answer->released);
    }
}

void player_help_row_install(void)
{
    player_help_ask_t empty;

    memset(&empty, 0, sizeof empty);
    empty.version = PLAYER_HELP_NOTE_VERSION;
    if (player_help_ask_publish(&empty)) {
        log_info("player help: the two buttons under Multiplayer ask the session through a "
                 "record; an empty ask is filed now, so a session's reader finds it at its "
                 "first look");
    } else {
        log_warning("player help: the empty ask could not be filed, so a session's reader "
                    "finds no record until the first press files one");
    }
}
