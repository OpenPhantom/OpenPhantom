/* overlay_dump.c: every row of both tabs, printed rather than checked.
 *
 * It is not a test and checks nothing. It walks both tabs with every heading and every fold open,
 * once with no session and once with one published, and prints one line per row: the tab, the
 * heading it is drawn under, its group and id, its kind, its name, and everything the panel reads
 * off it. The OpenPhantom tab is walked once more with the entity spawner's list of kinds open,
 * because that list cannot be reached by pressing rows. Two runs either side of a change
 * are diffed, and a row that moved, was renamed or changed what it says is a line in that diff.
 *
 * The third pass also PICKS a word on the row of segments, so the column that says which word is
 * in force is not the same number on every line of the file: a column that reads 0 on every line
 * carries no cover at all.
 *
 * That is the guard the row builders otherwise have only where somebody wrote a check for a
 * particular row. session_lock.c decides by a group number and a slot number, so a row inserted
 * or removed inside a source hands a player a hashed setting in the middle of a session, and a
 * table that builds rows on behalf of a group is exactly the kind of change that does it without
 * touching the source it moves.
 *
 * What a line carries: kind, name, `on`, `value`, `available`, the reason's word, and `expanded`,
 * `pending`, `fraction` and `chosen`, the fields the row tables recompute. A SLIDER row printed
 * without its fraction would carry nothing beyond its own existence, so a wrong track end, a wrong
 * rounding step or a swapped pair of bounds would be invisible to the one guard that walks every
 * row. `chosen` is the same case one kind along: it is which word of a row of segments is the one
 * in force, and a row that printed without it would say nothing at all about the only state it has.
 *
 * Why the walk runs twice. Against whatever the file says, which in a build directory is the
 * defaults, a row whose availability or reason hangs on a setting is only seen in one of its two
 * states. `Draw distance follows the frame rate` reads `n/a` with StrictViewRange off and
 * `needs row` with it on, and one walk would not show that difference. The whole
 * walk therefore runs twice, once with the setting off and once with it on, and the setting is
 * put back to off at the end. The second run's lines carry `strict-` in front of the pass name.
 *
 * Every session in those runs is a HOST, which is the side that takes the most. A client takes the
 * same rows, and four of them then carry the host's value in `value`, with the note under the draw
 * distance reading what view_distance_fix applied. So one more pass, `client-session`, runs last,
 * with the host's four values and that acknowledgement published, and everything is withdrawn
 * after it; the runs before it print what they always printed.
 *
 * That last pass is also the one in which the two buttons under Multiplayer are offered, because
 * a session offers them only where the multiplayer says it listens: the pass files that, presses
 * Repair lock and files the answer, so it prints the line under the buttons as well. The passes
 * before it print the buttons in their two greyed states.
 */
#include "overlay_model.h"
#include "overlay_reason.h"
#include "overlay_spawn.h"
#include "player_help_row.h"
#include "session_lock.h"
#include "strict_range_row.h"

#include "common/host_settings_note.h"
#include "common/player_help_note.h"
#include "common/session_note.h"
#include "common/text.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *const KIND[] = {
    "GROUP", "CHEAT", "ACTION", "HOTKEY", "VALUE", "INFO", "SLIDER", "CHOICE", "SEGMNT"
};

static const char *kind_word(overlay_row_kind_t kind)
{
    return ((size_t)kind < sizeof KIND / sizeof KIND[0]) ? KIND[kind] : "?";
}

static void session(bool running)
{
    session_note_t note;

    memset(&note, 0, sizeof note);
    note.running = running;
    note.is_host = running;
    if (!session_note_publish(&note)) {
        printf("!! the session note refused to publish; the run below is not what it says\n");
    }
}

/* The two buttons under Multiplayer as a client of a listening session sees them: the multiplayer
 * says it would carry both out, Repair lock is pressed and answered, so the pass prints both
 * buttons offered and the line under them. Every pass before it prints them greyed, without a
 * session for want of one and as a host with nobody listening. Withdrawn, the answer says nobody
 * listens; the line stays where it is, being about the press and not about the session. */
static void buttons(bool listening)
{
    player_help_answer_t answer;
    player_help_ask_t    ask;

    memset(&answer, 0, sizeof answer);
    answer.version = PLAYER_HELP_NOTE_VERSION;
    if (listening) {
        answer.ready = PLAYER_HELP_READY_LISTENING | PLAYER_HELP_READY_CAN_REPAIR |
                       PLAYER_HELP_READY_CAN_TELEPORT;
    }
    if (!player_help_answer_publish(&answer)) {
        printf("!! the buttons' answer was refused; the run below is not what it says\n");
    }
    session_lock_refresh();   /* the session the buttons ask about, as the note stands now */
    player_help_row_tick(0u);
    if (!listening) {
        return;
    }
    if (!player_help_row_press(PLAYER_HELP_KIND_REPAIR) || !player_help_ask_read(&ask)) {
        printf("!! Repair lock was not taken; the run below is not what it says\n");
        return;
    }
    answer.serial  = ask.serial;
    answer.kind    = ask.kind;
    answer.outcome = PLAYER_HELP_OUTCOME_DONE;
    if (!player_help_answer_publish(&answer)) {
        printf("!! the answer to the press was refused; the run below is not what it says\n");
    }
    player_help_row_tick(0u);
}

/* A session this machine is a client of, with the host's four values and view_distance_fix's
 * acknowledgement of the first, or all of it withdrawn. A refused publication is printed, for the
 * reason the other two helpers print theirs. */
static void client(bool running)
{
    session_note_t        note;
    host_settings_t       host;
    host_settings_taken_t taken;

    memset(&host, 0, sizeof host);
    memset(&taken, 0, sizeof taken);
    if (running) {
        host.running = true;
        host.generation = 1u;
        host.present = (uint16_t)((1u << HOST_SETTING_COUNT) - 1u);
        host.values[HOST_SETTING_VIEW_RANGE_SCALE]   = 1.5f;
        host.values[HOST_SETTING_FOG_BAND_SCALE]     = 0.5f;
        host.values[HOST_SETTING_AUTHORED_FOG_BAND]  = 1.0f;
        host.values[HOST_SETTING_DISMEMBERMENT_MODE] = 2.0f;
        taken.in_force = (uint16_t)(1u << HOST_SETTING_VIEW_RANGE_SCALE);
        taken.generation = 1u;
        taken.effective[HOST_SETTING_VIEW_RANGE_SCALE] = 1.25f;
    }
    memset(&note, 0, sizeof note);
    note.running = running;
    note.is_host = false;
    if (!host_settings_publish(&host) ||
        !host_settings_publish_taken("view_distance_fix", &taken) ||
        !session_note_publish(&note)) {
        printf("!! the client's records were refused; the run below is not what it says\n");
    }
    buttons(running);
}

/* The settings the second run is made under. It writes the file beside this executable, which in
 * a build tree is the program's own directory and nothing the game reads. A refused write is
 * printed rather than swallowed: the walk that follows would otherwise be one run printed under
 * the other one's name. */
static void settings(bool strict)
{
    if (!strict_range_row_set(strict)) {
        printf("!! StrictViewRange=%d was refused; the run below is not what it says\n",
               strict ? 1 : 0);
    }
}

/* Every heading and every fold on the open tab, opened, so the walk sees every row the tab can
 * put on screen. Found by what they look like, the same way the model's own tests do it. */
static void open_everything(void)
{
    bool opened = true;

    while (opened) {
        const uint32_t count = overlay_model_row_count();
        uint32_t       i;

        opened = false;
        for (i = 0; i < count; ++i) {
            overlay_row_t row;

            if (!overlay_model_row(i, &row) || row.expanded) {
                continue;
            }
            if (row.kind != OVERLAY_ROW_GROUP &&
                !(row.kind == OVERLAY_ROW_INFO && row.label[0] == '+')) {
                continue;
            }
            if (overlay_model_activate(i)) {
                overlay_model_rebuild();
                opened = true;
                break;
            }
        }
    }
}

/* Whether the entity spawner's list of kinds is open for this pass.
 *
 * It is opened here rather than by the walk above, because the walk cannot reach it: it hangs off
 * an ACTION row, and overlay_model_activate() refuses a row that is not available. With no level
 * under a test process the whole group is unavailable, so a walk that only presses what it can
 * press sees the kind row shut and never the rows inside it. That is why
 * unittests/overlay_session.c pins "Entity to spawn" rather than "Entity to spawn (pick one)".
 *
 * The four behaviours are one row that carries its four words itself, so there is no second list
 * to open and no pass of its own for it. What this pass carries as well is the word that row
 * stands on: it is set to the first in the plain passes and to the third in this one, which is the
 * only thing in the file that moves the `chosen` column and the only place a band reading the name
 * of a chosen entry is seen reading two different names. */
typedef enum spawn_list {
    SPAWN_LIST_SHUT = 0,
    SPAWN_LIST_KIND
} spawn_list_t;

#define SPAWN_WORD_PLAIN  0u
#define SPAWN_WORD_PICKED 2u

static void open_the_spawn_list(spawn_list_t which)
{
    /* Written on every pass and not only on the one that moves it, so a pass prints the same
     * lines whatever the pass before it did. */
    (void)overlay_spawn_choose(OVERLAY_SPAWN_BEHAVIOUR_SLOT,
                               (which == SPAWN_LIST_KIND) ? SPAWN_WORD_PICKED : SPAWN_WORD_PLAIN);
    if (which == SPAWN_LIST_KIND) {
        (void)overlay_spawn_toggle(OVERLAY_SPAWN_KIND_SLOT);
    }
    overlay_model_rebuild();
}

/* The tab is named by its enum rather than by the word on the tab itself: the word is the
 * drawing's and may be changed, and a dump that moved with it would report that change on every
 * row of the tab. */
static void dump_tab(const char *when, overlay_tab_t tab, const char *tab_name,
                     spawn_list_t list)
{
    char     heading[OVERLAY_LABEL_MAX] = "";
    uint32_t count;
    uint32_t i;

    overlay_model_reset();
    overlay_model_set_tab(tab);
    overlay_model_rebuild();
    open_everything();
    open_the_spawn_list(list);

    count = overlay_model_row_count();
    for (i = 0; i < count; ++i) {
        overlay_row_t row;

        if (!overlay_model_row(i, &row)) {
            continue;
        }
        if (row.kind == OVERLAY_ROW_GROUP) {
            text_format(heading, sizeof heading, "%s", row.label);
        }
        /* The reason as its WORD rather than as its code: the word is what the panel puts on the
         * row, and a code renumbered by an entry leaving the enum would otherwise be a difference
         * on every unavailable row of both tabs with nothing changed on screen. */
        printf("%-23s %-11s | %-34s | g%02u s%03u | %-6s | %-48s | on=%d | exp=%d | pend=%d | "
               "value=[%s] | frac=%.4f | chosen=%u | avail=%d | reason=[%s]\n",
               when, tab_name, heading, (unsigned)row.group, (unsigned)row.id,
               kind_word(row.kind), row.label, row.on ? 1 : 0, row.expanded ? 1 : 0,
               row.pending ? 1 : 0, row.value, (double)row.fraction, (unsigned)row.chosen,
               row.available ? 1 : 0, overlay_reason_word(row.reason));
    }
    printf("-- %s %s: %u rows\n", when, tab_name, (unsigned)count);
}

/* Both tabs, then the OpenPhantom tab once more with the spawner's list of kinds open. The list
 * holds nothing without a level, so what the pass pins is the row that carries it. */
static void dump_everything(const char *when)
{
    char pass[32];

    dump_tab(when, OVERLAY_TAB_ORIGINAL, "ORIGINAL", SPAWN_LIST_SHUT);
    dump_tab(when, OVERLAY_TAB_OPENPHANTOM, "OPENPHANTOM", SPAWN_LIST_SHUT);
    text_format(pass, sizeof pass, "%s+picked", when);
    dump_tab(pass, OVERLAY_TAB_OPENPHANTOM, "OPENPHANTOM", SPAWN_LIST_KIND);
}

int main(void)
{
    settings(false);
    session(false);
    dump_everything("no-session");
    session(true);
    dump_everything("session");

    settings(true);
    session(false);
    dump_everything("strict-no-session");
    session(true);
    dump_everything("strict-session");

    settings(false);
    client(true);
    dump_everything("client-session");

    /* All of it put back where the run found it, so a second run prints what the first did. */
    (void)overlay_spawn_choose(OVERLAY_SPAWN_BEHAVIOUR_SLOT, SPAWN_WORD_PLAIN);
    client(false);
    settings(false);
    session(false);
    return 0;
}
