/* session_lock.c: see the header. */
#include "session_lock.h"

#include "cheats_original.h"
#include "cheats_original_actions.h"
#include "npc_spawn_link.h"
#include "overlay_levels.h"
#include "overlay_picture.h"
#include "overlay_reason.h"

#include "common/session_note.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The last thing the note said. It is STICKY on a refusal, and that is the safe direction: a read
 * can fail because nobody has published (an installation whose multiplayer never armed, which
 * never locks anything anyway) or because it lost its race against a write (a two instruction
 * window). The two are not distinguishable from here, and treating a lost race as "no session"
 * would unlock every row for one frame in the middle of a session. */
static bool running;

/* And who hosts it, read out of the same note and in the same place, because the panel says
 * so in its footer. Kept beside `running` rather than asked for again when the footer draws:
 * two readings of one note a frame apart are two answers. */
static bool is_host;

/* Whether the multiplayer runs the NPC copies in this session. Not read here: the spawner's link
 * reads the grant record once a frame and redirects the group's rows by that reading, so the lock
 * asks the link. Two readings could disagree for a frame, and a click in that frame would build a
 * copy in a session that hands out no key for it. */
static bool copies_run;

/* One reading, wherever it is taken from. The two callers below differ in when they ask and not in
 * what they take from the answer, and a second copy of these two lines would be a second answer. */
static void take_the_note(void)
{
    session_note_t note;

    if (session_note_read(&note)) {
        running = note.running;
        is_host = note.running && note.is_host;
        return;
    }
    /* A note filed in a shape this build does not read is a multiplayer from another build.
     * What it says about a session cannot be judged here, and the two answers do not cost the
     * same: locking costs a click, unlocking costs the shared world. The host flag stays
     * false, which locks the most: a client's panel holds back everything a host's does and
     * the host's own rows besides. No note at all is not this case and leaves the panel open,
     * which is the single player it has always been. */
    if (session_note_unreadable()) {
        running = true;
        is_host = false;
    }
}

void session_lock_refresh(void)
{
    take_the_note();
    copies_run = npc_spawn_link_active();
}

bool session_lock_running(void)
{
    return running;
}

bool session_lock_is_host(void)
{
    return running && is_host;
}

/* The world a session stands in belongs to everyone standing in it, so one player opening the
 * panel may not stop it for the rest: with N players nobody is frozen because one of them went
 * into a menu. That is a decision of its own and it also repairs the NPC spawner, which looked
 * dead in a session for a reason that had nothing to do with the spawner: a wish is granted in a
 * substep, a held simulation runs none, and so nothing at all happened until the panel was closed
 * again. In a field run every one of ten wishes waited for that.
 *
 * The reading is the sticky one above and for the same reason, and it is taken here rather than
 * from the last refresh because the panel opens between two pictures. */
bool session_lock_panel_may_pause(void)
{
    take_the_note();
    return !running;
}

/* The three toggles of the shipped console that a session cannot take, by the code's own word
 * rather than by its index: the table is the image's and its order is not this file's to assume.
 *
 * `happy` and `but i feel so good` are the two cheat cells that remap a shot kind at the spawn
 * point, for every shooter on this machine (0x8822A4 and 0x882290, which is the flag array at
 * 0x882280 plus four times the row). In a session they are the host's: the multiplayer hands the
 * host's two bits to every client and puts a client's cells back on them every frame, so on a
 * client the toggle is undone on the next frame, and on the host it changes the projectiles of
 * every machine at once. `60fps` writes 1/64 into the frame delta cell and a session counts in
 * 1/32, so the multiplayer holds that cell at 0 on every machine. */
static const char *const TOGGLES_A_SESSION_CANNOT_TAKE[] = {
    "happy", "but i feel so good", "60fps"
};

uint32_t session_lock_taken_toggle_count(void)
{
    return (uint32_t)(sizeof TOGGLES_A_SESSION_CANNOT_TAKE /
                      sizeof TOGGLES_A_SESSION_CANNOT_TAKE[0]);
}

const char *session_lock_taken_toggle(uint32_t index)
{
    return (index < session_lock_taken_toggle_count())
           ? TOGGLES_A_SESSION_CANNOT_TAKE[index] : NULL;
}

static bool toggle_is_locked(uint32_t index)
{
    const char *name  = cheats_original_name(index);
    const uint32_t count = session_lock_taken_toggle_count();
    uint32_t    i;

    if (name == NULL) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (strcmp(name, session_lock_taken_toggle(i)) == 0) {
            return true;
        }
    }
    return false;
}

/* The one-shot codes that change a value the session decides. The difficulty, by three different
 * words: the host's is put onto every client at each level begin and put back whenever it differs
 * during a level. The graphics detail level: it decides which placements the activation scan
 * wakes, which on the host is the world everybody plays in and on a client nothing, since a
 * client parks its own scan. The credits code writes the level status cell, which is the
 * campaign's own place in the world and not one machine's to move. The rest are local: killing
 * yourself is a death, and a death travels; health, ammunition and the four play-as codes cost
 * the session nothing. */
static bool action_is_locked(uint32_t id)
{
    switch ((cheats_action_id_t)id) {
    case CHEATS_ACTION_LOWER_DIFFICULTY_A:
    case CHEATS_ACTION_LOWER_DIFFICULTY_B:
    case CHEATS_ACTION_INCREASE_DIFFICULTY:
    case CHEATS_ACTION_GRAPHICS_DETAIL:
    case CHEATS_ACTION_VIEW_CREDITS:
        return true;
    default:
        return false;
    }
}

/* The picture group holds the draw distance, whose value a session's host hands out, and its two
 * switches, which are this machine's own; all four rows write `[view_distance_fix]`. Beside them
 * stand the field of view and the subtitle size, which nobody compares. The live reading between
 * them is a note and writes nothing.
 *
 * By name rather than as 0, 1, 3 and 4. The numbers were a third spelling of an order that the
 * group's table and the group's enum already spell twice, and the one the build could not
 * check: a row swapped with its neighbour leaves every count correct and moves what a session
 * takes. */
static bool picture_is_locked(uint32_t slot)
{
    return slot == (uint32_t)OVERLAY_PICTURE_VIEW_RANGE ||
           slot == (uint32_t)OVERLAY_PICTURE_VIEW_RANGE_TRACK ||
           slot == (uint32_t)OVERLAY_PICTURE_AUTO_RANGE ||
           slot == (uint32_t)OVERLAY_PICTURE_STRICT_RANGE;
}

/* A session runs and the multiplayer does not run the NPC copies in it: a copy travels only once
 * the multiplayer hands the keys out, so until then the group's rows are greyed and the placement
 * mode will not start. Both of those used to decide it for themselves, and the mode's half asked
 * only whether a session was running: in every co-op session where the copies did run it refused,
 * beside rows that were open, with words that named a state nobody had looked at. */
static bool the_spawner_is_held(void)
{
    return running && !copies_run;
}

bool session_lock_holds_the_spawner(void)
{
    session_lock_refresh();   /* the mode is asked between pictures, when nothing has read for it */
    return the_spawner_is_held();
}

static bool row_is_locked(uint32_t group, uint32_t slot)
{
    switch ((overlay_group_t)group) {
    case OVERLAY_GROUP_ORIGINAL_TOGGLES:
        return toggle_is_locked(slot);
    case OVERLAY_GROUP_ORIGINAL_ACTIONS:
        return action_is_locked(slot);
    case OVERLAY_GROUP_OPENPHANTOM_PICTURE:
        return picture_is_locked(slot);
    case OVERLAY_GROUP_OPENPHANTOM_FOG:
        /* Every row but the switch at the top: the band and what it follows are the host's in a
         * session, "No fog" writes this mod's own section. */
        return slot != 0u;
    case OVERLAY_GROUP_OPENPHANTOM_FRAMERATE:
        return true;    /* every row of it, although `[framerate_fix]` is this machine's own */
    case OVERLAY_GROUP_OPENPHANTOM_DISMEMBER:
        return true;    /* `[dismemberment] Mode`, which is the host's in a session */
    case OVERLAY_GROUP_OPENPHANTOM_LEVELS:
        /* The skip alone. What a new game starts at is read when a new game starts and never
         * during one, so it costs a session nothing. */
        return slot == OVERLAY_LEVELS_SKIP_SLOT;
    case OVERLAY_GROUP_OPENPHANTOM_SPAWN:
        return the_spawner_is_held();
    default:
        return false;
    }
}

bool session_lock_take(uint32_t group, uint32_t slot, overlay_row_t *row)
{
    if (row == NULL || !running || !row_is_locked(group, slot)) {
        return false;
    }
    /* Every kind a player can act on, asked of the one list of them (overlay_model.h). The
     * slider is on it: it looked at first like a decoration under the value row it belongs to,
     * and it is not, because overlay_model_slider_set refuses a track whose row is unavailable
     * and accepts every other one, so a fog band or a draw distance left open here would have
     * written its ini section by being dragged, which is the exact thing this file exists to
     * prevent. An info line is not acted on and keeps its colour; the row above it already says
     * what happened. */
    if (!overlay_row_kind_is_acted_on(row->kind)) {
        return false;
    }
    row->available = false;
    /* And why, so the row says it in its own chip instead of the bare `n/a` that four different
     * situations shared. The group's sentence is written once under the first row taken; the word
     * is on every one of them.
     *
     * A row that already names a reason of its own keeps it when that reason outranks a passing
     * state. Three rows of the shipped console are held back whatever else is true and one of
     * them, the credits, is on this file's own list as well; while the session took the word from
     * it, a player read `session` on a row a session has nothing to do with. The spawner's rows
     * are the same case for a different reason: the group answers for itself, in words that say
     * which of its four states this is. */
    if (!overlay_reason_is_final(row->reason)) {
        row->reason = (uint32_t)OVERLAY_REASON_SESSION;
    }
    return true;
}

bool session_lock_holds_group(uint32_t group)
{
    if (!running) {
        return false;
    }
    switch ((overlay_group_t)group) {
    case OVERLAY_GROUP_ORIGINAL_TOGGLES:
    case OVERLAY_GROUP_ORIGINAL_ACTIONS:
    case OVERLAY_GROUP_OPENPHANTOM_PICTURE:
    case OVERLAY_GROUP_OPENPHANTOM_FOG:
    case OVERLAY_GROUP_OPENPHANTOM_FRAMERATE:
    case OVERLAY_GROUP_OPENPHANTOM_DISMEMBER:
    case OVERLAY_GROUP_OPENPHANTOM_LEVELS:
        return true;
    case OVERLAY_GROUP_OPENPHANTOM_SPAWN:
        return the_spawner_is_held();
    default:
        return false;
    }
}

const char *session_lock_word(uint32_t group)
{
    /* Nothing for the spawner: that group writes its own sentence out of spawn_reason.c, a few
     * rows above the ones this would stand under, and the two said one state twice. */
    return (overlay_group_t)group == OVERLAY_GROUP_OPENPHANTOM_SPAWN ? NULL : SESSION_LOCK_WORD;
}
