/* unittests/mp_chat_input_stand_in.h: the engine and the modules the chat's input line runs
 * against.
 *
 * mp_chat_input hooks the head of the engine's key handler, reads four cells and asks a few other
 * modules, so a test of it needs a handler to hook and cells to read. They are the stand-in's
 * own: the detour hands out the hook it was given and stands for the handlers further in, the
 * developer menu's and the engine's, whose answer the test sets; the level outcome, the menu on
 * show, the movie cell and the simulation gate are fields of the stand-in, and so is the operand
 * the movie cell is read out of; the settings, the input split, the clock and the one call that
 * says a line are answered from it. The key table, the typing rule, the holders and the session
 * note are the real ones and are not played here.
 *
 * The log is kept in the stand-in as well, line by line, and the module's counters, which speak
 * only through its report, are read back out of the report's line as it is written. The stand-in
 * offers no function of its own: everything it defines is a function the module calls, and
 * everything the test reads is a field.
 */
#ifndef UNITTESTS_MP_CHAT_INPUT_STAND_IN_H
#define UNITTESTS_MP_CHAT_INPUT_STAND_IN_H

#include "mp_chat.h"
#include "mp_chat_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int32_t(__cdecl *stand_in_key_hook_t)(uint32_t window, int32_t message, int32_t wparam,
                                              uint32_t lparam);

/* Where the key handler is said to resolve. Nothing is ever written or read there: the detour
 * only keeps the hook it is handed. */
#define STAND_IN_KEY_HANDLER 0x0043F603u

/* The level outcome while a level runs. */
#define STAND_IN_LEVEL_RUNNING 2u

/* How many log lines are kept, and how long one may be; the report's line is a little over 500. */
#define STAND_IN_LINES      64u
#define STAND_IN_LINE_BYTES 1100u

/* The numbers of the report's line, in the order it prints them. */
typedef enum count {
    C_READS,
    C_CHANGES,
    C_REFUSED_NAMES,
    C_OPENS,
    C_LEFT_TO_CHAIN,
    C_NO_HOLD,
    C_TYPED,
    C_TRANSLITERATED,
    C_NO_ROOM,
    C_ENTER,
    C_ESCAPE,
    C_MENU,
    C_MOVIE,
    C_LEVEL,
    C_SESSION,
    C_REPAIR,
    C_HELD_FRAMES,
    C_HELD_AFTER_CLOSE,
    C_COUNT
} count_t;

typedef struct counts {
    bool          read;      /* the line was written and every number in it was read */
    char          key[8];    /* the name of the key the chat opens on */
    unsigned long value[C_COUNT];
} counts_t;

typedef struct stand_in {
    stand_in_key_hook_t  hook;              /* what the chat placed on the key handler */
    size_t               prologue;          /* the prologue it declared for it */
    bool                 refuse_operand;    /* the movie cell's operand cannot be read */
    int32_t              further_in_answer; /* what the handlers further in answer */
    uint32_t             further_in_calls;
    uint32_t             further_in_message;
    uint32_t             outcome;
    uint32_t             menu;
    uint32_t             movie;
    uint32_t             gate;
    bool                 input_split;
    const char          *chat_key;          /* ChatKey as the file says it; NULL when absent */
    const char          *board_key;         /* ScoreboardKey likewise */
    uint32_t             now_ms;
    mp_chat_say_result_t say;               /* what saying a line answers */
    uint32_t             says;
    char                 said[MP_CHAT_TEXT_MAX + 1u];

    /* Every line written since the test last set log_count to 0, the oldest first, and the
     * report's line read back into its numbers each time it is written. */
    char     log[STAND_IN_LINES][STAND_IN_LINE_BYTES];
    size_t   log_count;
    counts_t report;
} stand_in_t;

extern stand_in_t engine;

#endif /* UNITTESTS_MP_CHAT_INPUT_STAND_IN_H */
