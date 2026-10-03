/* mp_menu_screens_lobby.c: LOBBY, on the presets overlay, who is here, what will be played, and
 * the moment everybody goes into it together.
 *
 * SIZE NOTE: over 600 lines. One screen, but it is the only screen with two sides, and almost every
 * function here is one of them answering a question the other one does not have. The choosing, what
 * a host picks and the note that says so, left as mp_menu_screens_choice.c when the one exit pushed
 * this file past the limit; the band and the level line, the two lines of text under the rows, left
 * as mp_menu_screens_band.c when this file stood near the limit again. The next seam is a client's
 * start in lobby_frame, the wait for the host's savegame, which reads nothing of the rows or the
 * lines.
 *
 * The lobby is where this feature stops being a setting and becomes a room. It is opened from
 * HOSTEN and from BEITRETEN, and the transport is brought up on the way in rather than on the way
 * out, because a lobby that cannot show who has joined is a screen with nothing to say.
 *
 * The game decides which controls exist. Not the code below: mp_settings_lobby_rows does, and it
 * is a pure function with a test. A co-op player has no team switch, because every co-op player is
 * on no team; a deathmatch host has no savegame to carry on from and does choose a hero, because
 * there is no level prescribing one; a co-op host does not choose a hero, because the level and
 * the savegame both do. The four rows on the right are therefore not four fixed buttons but the
 * answer to that question, rebuilt every frame, which is what lets a client change what it shows
 * the moment it learns from the host which game is being played.
 *
 * The state does not survive the screen. The chosen level, its title, its index, the savegame,
 * the from-a-save flag and the hero are wiped when the lobby opens. They used to persist, so a
 * player who hosted co-op from a savegame, went back, and hosted a deathmatch got a screen saying
 * "choose a map" over a start that restored the savegame.
 */
#include "mp_menu_screens_int.h"

#include "mp_bridge.h"
#include "mp_bridge_content.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_savefile.h"
#include "mp_levels.h"
#include "mp_lobby.h"
#include "mp_menu.h"
#include "mp_menu_screens.h"
#include "mp_mod_allow.h"
#include "mp_saves.h"
#include "mp_settings.h"
#include "mp_start.h"

#include "common/text.h"

/* ==============================================================================================
 * Ids. The shared 1..9 are in the internal header; the lobby's own start at 60.
 * ============================================================================================ */

#define ID_LOBBY_LIST  60
#define ID_LOBBY_ROW0  61   /* .. 64: what the four control rows are is decided per frame */
#define ID_LOBBY_BAND  65
#define ID_LOBBY_LEVEL 66

/* ==============================================================================================
 * The screen.
 * ============================================================================================ */

/* The overlay paints three slot plates on the right and a deathmatch host needs four rows, so
 * the fourth takes the right hand end of the lower window, directly under the third plate and in
 * its column, the level line takes that window's left hand end, and the status band moves down
 * onto the edit bar, which a lobby has no field to type in. Three other layouts were measured
 * and rejected: four rows at the shipped pitch of 50 put the fourth half on a bright metal edge
 * and half on the band; rows of 34 fit four inside the plates but no longer line up with the
 * three the overlay paints; and narrowing the band to make room beside it leaves it about 330
 * pixels for sentences of 380 to 440. */
static void build_lobby(void)
{
    mp_screen_t *s = &mps.lobby;
    size_t       i;

    if (s->built) {
        return;
    }
    mp_screen_init(s);
    mp_screens_put_frame(s, MP_BMP_PRESETS, mp_text(MP_TEXT_BACK), SW_ACTION_CANCEL);
    (void)mp_screen_put_text(s, ID_TITLE, SW_ACTION_STATIC, mp_text(MP_TEXT_LOBBY_TITLE),
                             MP_FONT_INDUST,
                             MP_ALIGN_CENTRE, PRE_TITLE_X, PRE_TITLE_Y, PRE_TITLE_W, PRE_TITLE_H);
    mps.lobby_list = mp_screen_put_list(s, ID_LOBBY_LIST, true, PRE_LIST_X, PRE_LIST_Y,
                                        PRE_LIST_W, PRE_LIST_H);
    for (i = 0; i < MP_SETTINGS_LOBBY_ROWS_MAX; ++i) {
        int32_t y = i < 3u ? PRE_SLOT_Y + (int32_t)i * PRE_SLOT_PITCH : LOB_ROW4_Y;

        (void)mp_screen_put_text(s, ID_LOBBY_ROW0 + (int32_t)i, SW_ACTION_SELECT,
                                 mps.lobby_row_text[i], MP_FONT_SYSFONT, MP_ALIGN_CENTRE,
                                 PRE_SLOT_X, y, PRE_SLOT_W, PRE_SLOT_H);
    }
    (void)mp_screen_put_text(s, ID_LOBBY_LEVEL, SW_ACTION_STATIC, mps.lobby_choice,
                             MP_FONT_COURIER, MP_ALIGN_CENTRE, LOB_LEVEL_X, LOB_LEVEL_Y,
                             LOB_LEVEL_W, LOB_LEVEL_H);
    (void)mp_screen_put_text(s, ID_LOBBY_BAND, SW_ACTION_STATIC, mps.lobby_band, MP_FONT_SYSFONT,
                             MP_ALIGN_CENTRE, LOB_BAND_X, LOB_BAND_Y, LOB_BAND_W, LOB_BAND_H);
}

/* Which game this side is showing. A host shows its own choice; a client shows the host's, which
 * travels in the setup note, and falls back to what the announce said while none has arrived. */
static mp_settings_mode_t lobby_mode(void)
{
    mp_lobby_setup_t setup;
    uint8_t          heard = mps.settings->join_mode;

    if (!mps.lobby_is_host && mp_bridge_lobby_setup(&setup)) {
        heard = setup.mode;
    }
    return mp_settings_lobby_mode(mps.lobby_is_host, mps.settings->mode, heard);
}

/* A row's caption cut to its slot, and one line in the log the first time a cut takes something
 * off. Every caption here is a fixed text, or a fixed text around a hero's name or a team number,
 * so a cut is a translation too long for the slot rather than a player's typing: on screen it is a
 * word without its last letters, and the log is where that is seen before a player reads it. */
static void fit_row(const char *text, char *out, size_t out_size)
{
    static bool told;

    mp_screens_fit(MP_MENU_ADVANCE_SYSFONT, text, PRE_SLOT_W, out, out_size);
    if (!told && strcmp(out, text) != 0) {
        told = true;
        log_warning("a lobby row does not fit its %d pixel slot and is drawn cut: '%s' shows as "
                    "'%s'", (int)PRE_SLOT_W, text, out);
    }
}

/* What one row says. A button names the effect of pressing it, and the team and ready rows name
 * the value they will take, which is how the current one stays readable without the caption being
 * a state that does nothing. The longest captions measure 187 pixels for the hero row with the
 * longest hero name and 172 for the team row, against the 190 of the slot. */
/* `out_size` is passed rather than measured, because `out` is a POINTER here: this function is
 * handed one row of a table its caller owns. Measuring it with sizeof gave four, and every
 * caption in the lobby came out three characters long. */
static void row_caption(mp_settings_lobby_row_t kind, uint8_t team, bool ready, char *out,
                        size_t out_size)
{
    char text[CAPTION_MAX];

    switch (kind) {
    case MP_SETTINGS_ROW_MAP:
        fit_row(mp_text(MP_TEXT_ROW_PICK_MAP), out, out_size);
        return;
    case MP_SETTINGS_ROW_SAVE:
        fit_row(mp_text(MP_TEXT_ROW_PICK_SAVE), out, out_size);
        return;
    case MP_SETTINGS_ROW_FRIENDLY_FIRE:
        /* The row names the value it holds rather than the one pressing it would take, like the
         * level line above it and unlike the buttons: this is a setting, and a setting whose row
         * says the opposite of what is set is read wrongly every time. */
        text_format(text, sizeof text, mp_text(MP_TEXT_ROW_FRIENDLY_FIRE),
                    mp_text(mp_rules_friendly_fire(&mps.settings->rules) ? MP_TEXT_RULES_ON
                                                                         : MP_TEXT_RULES_OFF));
        fit_row(text, out, out_size);
        return;
    case MP_SETTINGS_ROW_HERO:
        text_format(text, sizeof text, mp_text(MP_TEXT_ROW_HERO),
                    mp_screens_hero_name(mps.lobby_hero));
        fit_row(text, out, out_size);
        return;
    case MP_SETTINGS_ROW_TEAM:
        if (team >= (uint8_t)MP_LOBBY_TEAM_MAX) {
            fit_row(mp_text(MP_TEXT_ROW_LEAVE_TEAM), out, out_size);
            return;
        }
        text_format(text, sizeof text, mp_text(MP_TEXT_ROW_TO_TEAM), (unsigned)(team + 1u));
        fit_row(text, out, out_size);
        return;
    case MP_SETTINGS_ROW_READY:
        fit_row(mp_text(ready ? MP_TEXT_ROW_NOT_READY : MP_TEXT_ROW_READY), out, out_size);
        return;
    case MP_SETTINGS_ROW_START:
        fit_row(mp_text(MP_TEXT_ROW_START), out, out_size);
        return;
    case MP_SETTINGS_ROW_NONE:
    default:
        out[0] = '\0';
        return;
    }
}

/* The rows this side shows, and the captions on them. Rebuilt every frame because a client can
 * learn which game is being played at any moment, and the answer changes with it. */
static void refresh_rows(void)
{
    mp_settings_mode_t mode = lobby_mode();
    uint8_t            team;
    bool               ready;
    size_t             i;

    mp_bridge_get_lobby(&team, &ready);
    if (mode != MP_SETTINGS_MODE_TDM && team != (uint8_t)MP_LOBBY_TEAM_NONE) {
        /* A co-op player is on no team. Somebody who chose one in a deathmatch and then found
         * themselves in a co-op lobby would otherwise keep a team nothing here can act on. */
        team = (uint8_t)MP_LOBBY_TEAM_NONE;
        mp_bridge_set_lobby(team, ready);
    }
    mps.lobby_rows = mp_settings_lobby_rows(mps.lobby_is_host, mode, mps.lobby_row,
                                            MP_SETTINGS_LOBBY_ROWS_MAX);
    if (mps.lobby_rows > MP_SETTINGS_LOBBY_ROWS_MAX) {
        mps.lobby_rows = MP_SETTINGS_LOBBY_ROWS_MAX;
    }
    for (i = 0; i < MP_SETTINGS_LOBBY_ROWS_MAX; ++i) {
        bool used = i < mps.lobby_rows;

        if (used) {
            row_caption(mps.lobby_row[i], team, ready, mps.lobby_row_text[i],
                        sizeof mps.lobby_row_text[i]);
        } else {
            mps.lobby_row_text[i][0] = '\0';
        }
        mp_screen_set_visible(&mps.lobby, ID_LOBBY_ROW0 + (int32_t)i, used);
    }
}

/* One row per player, out of the roster the host broadcasts, under a heading written from the
 * same column table so that what a column promises is what stands under it. The host's own line
 * is in it too, so a host alone in its lobby still sees itself. */
static void refresh_players(void)
{
    const bool  with_team = lobby_mode() == MP_SETTINGS_MODE_TDM;
    mp_roster_t roster;
    char        text[MP_ROSTER_MAX_ENTRIES + 3u][MP_SCREEN_ROW_TEXT_MAX];
    const char *rows[MP_ROSTER_MAX_ENTRIES + 3u];
    size_t      count = 0;
    size_t      i;

    mp_screens_player_header(with_team, text[count], sizeof text[0]);
    rows[count] = text[count];
    ++count;
    if (mp_bridge_roster_current(&roster)) {
        for (i = 0; i < roster.count && count < MP_ROSTER_MAX_ENTRIES + 1u; ++i) {
            mp_screens_player_row(&roster.entry[i], with_team, text[count], sizeof text[0]);
            rows[count] = text[count];
            ++count;
        }
    }
    /* A client the host refused for its game data, a required mod or a DLL outside this release
     * has no room to be in, and the rows under the heading say each side's value of what differs;
     * the band says what it is. The band has no room for a name and two builds together. A row the
     * refusal has no value for is left out, and a row that stays is moved up into its place. */
    if (count == 1u && !mps.lobby_is_host) {
        mp_mod_refusal_t refusal;
        size_t           row;

        if (mp_bridge_content_refusal(&refusal) &&
            mp_screens_refusal_lines(&refusal, NULL, 0u, text[1], text[2], sizeof text[0])) {
            for (row = 1u; row <= 2u; ++row) {
                if (text[row][0] == '\0') {
                    continue;
                }
                if (row != count) {
                    memcpy(text[count], text[row], sizeof text[0]);
                }
                rows[count] = text[count];
                ++count;
            }
        }
    }
    if (count == 1u) {
        rows[count++] = mp_text(mps.lobby_is_host ? MP_TEXT_LOBBY_NOBODY_YET
                                                  : MP_TEXT_LOBBY_WAIT_HOST);
    }
    /* A public session is reached by its code, so both sides see it under the players: the host
     * to pass it on, a player to check it is the one they meant. */
    if (mp_bridge_public_bound()) {
        char code[MP_RELAY_CODE_TEXT_BYTES];

        text[count][0] = '\0';
        rows[count]    = text[count];
        ++count;
        if (mp_bridge_public_code(code)) {
            text_format(text[count], sizeof text[0], mp_text(MP_TEXT_LOBBY_CODE), code);
        } else {
            mp_screens_fit(MP_MENU_ADVANCE_COURIER, mp_text(MP_TEXT_LOBBY_CODE_WAIT),
                           PRE_LIST_W - LIST_INSET, text[count], sizeof text[0]);
        }
        rows[count] = text[count];
        ++count;
    }
    mp_screens_set_rows(mps.lobby_list, PRE_LIST_W, rows, count);
}

/* ==============================================================================================
 * Going in.
 * ============================================================================================ */

/* Asks mp_start for the way in that matches what was chosen (mp_start_level, which a client
 * following its host into a later world asks as well).
 *
 * The hero is NOT asked for here. It used to be, and this screen was then the only place that
 * asked at all, so the pick lasted exactly one level: the next world, whether the host ended the
 * level or loaded one, put the player back on whatever that world prescribes. Every level begin of
 * a session now puts the pick on, and this start ends in the engine's own level begin broadcast
 * like every other one, so asking here as well would ask twice for the first level and never for
 * the second. */
static bool begin_level(const char *level, uint8_t level_index, bool from_save, const char *save)
{
    return mp_start_level(level, level_index, from_save, save);
}

static bool host_starts(void)
{
    /* Nothing starts from a lobby whose transport would not arm: the level would load as a
     * single player one on a transport armed the other way round, and end in its first
     * frame as a lost host. The band already says why. */
    if (mps.lobby_arm_refused) {
        return true;
    }
    if (mps.lobby_level[0] == '\0') {
        FIT_SYSFONT(mp_text(MP_TEXT_START_MAP_FIRST), LOB_BAND_W, mps.lobby_error);
        return true;
    }
    if (mp_screens_waiting_players(NULL, 0u) != 0u) {
        /* Starting over a player who is still choosing a hero or a team drops them into a level
         * with whatever they happened to have, so the start waits for them instead. No error line
         * is set: the band already names who is being waited for, and it keeps naming them as they
         * report in, which a line frozen at the moment of the press would not. */
        return true;
    }
    if (!begin_level(mps.lobby_level, mps.lobby_level_index, mps.lobby_from_save,
                     mps.lobby_save)) {
        FIT_SYSFONT(mp_text(MP_TEXT_START_LEVEL_FAILED), LOB_BAND_W, mps.lobby_error);
        return true;
    }
    /* Only now, because a start that was refused must not send everybody else into a level this
     * machine is not going to. */
    mp_bridge_lobby_start();
    mps.lobby_started = true;
    return false;
}

/* ==============================================================================================
 * The loop.
 * ============================================================================================ */

/* The client's join, made to the address the menu holds NOW. The way into the lobby and the
 * second try after a password both go through here, so the two cannot drift apart. */
static void client_connects(void)
{
    char endpoint[MP_SETTINGS_ENDPOINT_MAX];

    mp_bridge_connect_in_lobby(
        mp_settings_format_endpoint(mps.settings->address, mps.settings->port,
                                    endpoint, sizeof endpoint) ? endpoint : NULL);
}

/* The password, asked when the host has said it needs one and not before.
 *
 * Nothing on this side knows whether a session has one until the host answers: an announce says
 * so, a typed address and a session code do not (mp_lobby_password_question). So the join goes
 * out without one and lands here, on the one refusal a player can do something about. The answer
 * goes to the session and the join is made again.
 *
 * Every refusal is put once, which is what the count is for. A player who backs out of the
 * question is not asked again until they have made another attempt; and a question that could not
 * be turned into one, because the relay went away between the refusal and the answer, does not
 * come back on every frame for the rest of the lobby. */
static void password_asked_for(void)
{
    uint32_t denials = mp_bridge_lobby_denials();
    char     typed[MP_SETTINGS_PASSWORD_MAX];

    if (denials == mps.lobby_denials_answered ||
        mp_bridge_lobby_last_deny() != MP_DENY_PASSWORD) {
        return;
    }
    mps.lobby_denials_answered = denials;
    if (!mp_screens_run_entry(mp_text(MP_TEXT_PASSWORD_TITLE),
                              mp_text(MP_TEXT_JOIN_PASSWORD_WANTED),
                              mps.settings->join_password, MP_SETTINGS_PASSWORD_MAX - 1u,
                              typed, sizeof typed)) {
        return;
    }
    memcpy(mps.settings->join_password, typed, sizeof typed);
    mp_bridge_set_password(mps.settings->join_password);
    client_connects();
}

/* CLIENT: a start the host has given and this player may take now, which is only once they have
 * said ready. A player who joins a running session finds its start waiting the moment the lobby
 * hears its host, and took it there and then, with hero 0 and without ever pressing ready; the
 * rule is mp_lobby_start_may_be_taken. In a lobby before the start every client is ready by the
 * time the host may start, so there the answer is the one it always was.
 *
 * The start is looked at here and not taken: taking it marks the generation acted on, and the
 * frame below may still have to wait for the host's savegame. The band asks the same question to
 * say that the level is about to begin, so the words and the take cannot disagree. */
bool mp_screens_lobby_start_to_take(mp_lobby_setup_t *out)
{
    bool ready = false;

    mp_bridge_get_lobby(NULL, &ready);
    return mp_lobby_start_may_be_taken(ready, mp_bridge_lobby_peek_start(out));
}

/* The host's start, taken once this player is ready and, for a savegame, once its file is
 * here; until then the band says what is being waited for. */
static void take_the_start(void)
{
    mp_lobby_setup_t setup;

    /* The start arrives as an edge on a bit in a note the host repeats, so it cannot be missed by
     * a client that was still loading its list when the host pressed the button. It is acted on
     * only once this player is ready; until then the band says what to do. */
    if (mp_screens_lobby_start_to_take(&setup)) {
        bool        from_save = (setup.flags & MP_LOBBY_F_FROM_SAVE) != 0u;
        const char *save      = NULL;

        /* The start waits for the file, and it keeps waiting. A host that restores a savegame
         * names it in this note, and the level it sits in is only half the answer: the file is the
         * story flags, the actors, the doors and the pickups as they stood when it was written. So
         * the start is looked at and not taken until the file is on disk, and the level is then
         * entered through the same restore the host used.
         *
         * It used to give up and load the level fresh, and that was the wrong end of the trade.
         * A client that begins the level fresh is not a client with a slow connection, it is a
         * SECOND CAMPAIGN. Its doors are shut, its pickups are back, its story flags are whatever
         * a new level starts with, and none of that is visible: it looks exactly like single
         * player, because it is. The player reported it as one, and the log line that said so
         * scrolled past unread while they were looking at the game.
         *
         * So the wait no longer ends in a start. It ends in a sentence on the band, and the wait
         * goes on, because waiting is not merely hopeful: the client re-asks for the chunks it is
         * missing, so a transfer that stalled resumes on its own. A player who does not want to
         * wait has ZURUECK, which is a decision they can see themselves making. */
        if (from_save && setup.save_id != 0u) {
            uint32_t        now   = mp_wallclock_ms();
            bool            ready = mp_bridge_savefile_ready(setup.save_id, setup.save_bytes);
            mp_saves_look_t look  = MP_SAVES_LOOK_UNREADABLE;
            mp_save_t       header;

            if (ready) {
                save = mp_bridge_savefile_path();
                look = mp_bridge_savefile_look(setup.save_id, setup.save_bytes, &header);
            }
            if (ready && look == MP_SAVES_LOOK_NO_LEVEL) {
                /* A file whose header names no shipped level is a save of a level loaded by path,
                 * and such a save carries the HOST's absolute path. It would
                 * be opened here as written, on a disk that has no such folder. The level itself
                 * is what this side loads then, as every client did before the file travelled. */
                log_warning("the host's savegame is here but its header names no level of "
                            "the game's own table, so this side begins the level fresh");
                save = NULL;
            } else if (!ready || look != MP_SAVES_LOOK_SAVE) {
                /* Not here yet, or here and not readable in this moment. Neither is a reason to
                 * begin the level fresh; the band says the file is coming and the next frame
                 * looks again. A file that stays unreadable is written or asked for again by its
                 * own module, which makes it not ready in between. */
                char text[CAPTION_MAX];
                bool overdue;

                save = NULL;

                /* A host that has gone is not a transfer that stalls: the band says what the
                 * connection is doing instead, and the wait goes on in case it comes back. */
                if (!mp_bridge_lobby_connected()) {
                    mps.lobby_error[0] = '\0';
                    return;
                }
                if (mps.lobby_save_wait_ms == 0u) {
                    mps.lobby_save_wait_ms = now != 0u ? now : 1u;
                }
                overdue = (now - mps.lobby_save_wait_ms) >= MP_BRIDGE_SAVEFILE_WAIT_MS;
                if (overdue && !mps.lobby_save_slow_logged) {
                    mps.lobby_save_slow_logged = true;
                    log_warning("the host's savegame is taking longer than %u ms (%u %% of it is "
                                "here). This side WAITS: beginning the level fresh would be a "
                                "second campaign with its own doors, pickups and story flags, and "
                                "nothing on screen would say so. The missing chunks are asked for "
                                "again on their own",
                                (unsigned)MP_BRIDGE_SAVEFILE_WAIT_MS,
                                (unsigned)mp_bridge_savefile_percent());
                }
                text_format(text, sizeof text,
                            mp_text(overdue ? MP_TEXT_SAVE_STALLED : MP_TEXT_SAVE_COMING),
                            (unsigned)mp_bridge_savefile_percent());
                FIT_SYSFONT(text, LOB_BAND_W, mps.lobby_error);
                return;
            }
        }
        (void)mp_bridge_lobby_take_start(&setup);
        mps.lobby_save_wait_ms     = 0u;
        mps.lobby_save_slow_logged = false;
        mps.lobby_error[0]     = '\0';
        if (begin_level(setup.level, setup.level_index, from_save, save)) {
            mps.lobby_started = true;
            mp_screen_request_close(&mps.lobby);   /* a frame callback's only way out */
        } else {
            FIT_SYSFONT(mp_text(MP_TEXT_HOST_MAP_MISSING), LOB_BAND_W, mps.lobby_error);
        }
    }
}

static void lobby_frame(void *ctx)
{
    mp_lobby_setup_t setup;

    (void)ctx;
    refresh_players();
    refresh_rows();
    mp_screens_refresh_band();
    mp_screens_refresh_level();

    if (mps.lobby_is_host) {
        return;
    }
    password_asked_for();
    /* The host ended the session, left it, or went quiet after it had spoken: the lobby is gone,
     * and the screen goes with it rather than waiting on a BACK nobody has a reason to press. */
    if (mp_bridge_lobby_sent_away()) {
        mps.lobby_gone = MP_LOBBY_OVER_BEHIND;
    } else if (mp_bridge_lobby_ended() || mp_bridge_lobby_host_left()) {
        mps.lobby_gone = MP_LOBBY_OVER_HOST_ENDED;
    } else if (mp_bridge_lobby_setup(&setup) && mp_bridge_lobby_gave_up()) {
        mps.lobby_gone = MP_LOBBY_OVER_HOST_LOST;
    }
    /* The host's note names a game no menu of this build offers. Following it would switch the
     * arena on and empty the level under a room that has no row for any of it, so the room ends
     * instead, with the sentence a refused handshake would have shown. */
    if (mps.lobby_gone == MP_LOBBY_OVER_NO && mp_bridge_lobby_setup(&setup) &&
        !mp_settings_mode_offered((int32_t)setup.mode)) {
        mps.lobby_gone = MP_LOBBY_OVER_CONTENT;
        log_warning("the host's note names game %u, which no menu of this build offers. The room "
                    "ends here rather than following it into an arena nothing else knows about",
                    (unsigned)setup.mode);
    }
    if (mps.lobby_gone != MP_LOBBY_OVER_NO) {
        mp_screen_request_close(&mps.lobby);
        return;
    }
    take_the_start();
}

static bool lobby_activate(int32_t id, void *ctx)
{
    uint8_t team;
    bool    ready;
    size_t  index;

    (void)ctx;
    if (id == ID_BACK) {
        return false;
    }
    if (id < ID_LOBBY_ROW0 || id >= ID_LOBBY_ROW0 + (int32_t)MP_SETTINGS_LOBBY_ROWS_MAX) {
        return true;   /* the list: its selection moved, nothing else */
    }
    index = (size_t)(id - ID_LOBBY_ROW0);
    if (index >= mps.lobby_rows) {
        return true;   /* a row the mode has just taken away; the next frame hides it */
    }
    if (!mps.lobby_arm_refused) {
        mps.lobby_error[0] = '\0';   /* a refused arming stays on the band */
    }
    mp_bridge_get_lobby(&team, &ready);
    switch (mps.lobby_row[index]) {
    case MP_SETTINGS_ROW_MAP:
        mp_screens_pick_map();
        return true;
    case MP_SETTINGS_ROW_SAVE:
        mp_screens_pick_save();
        return true;
    case MP_SETTINGS_ROW_FRIENDLY_FIRE:
        /* The flag is the session's, so it goes out in the host's note with everything else the
         * lobby decides; the ini keeps it for the next session on its own. */
        mps.settings->rules.flags ^= (uint8_t)MP_RULES_F_FRIENDLY_FIRE;
        mp_screens_publish_choice();
        return true;
    case MP_SETTINGS_ROW_HERO:
        mp_screens_pick_hero();
        return true;
    case MP_SETTINGS_ROW_TEAM:
        mp_bridge_set_lobby((uint8_t)((team + 1u) % (MP_LOBBY_TEAM_MAX + 1u)), ready);
        return true;
    case MP_SETTINGS_ROW_READY:
        mp_bridge_set_lobby(team, !ready);
        return true;
    case MP_SETTINGS_ROW_START:
        return host_starts();
    case MP_SETTINGS_ROW_NONE:
    default:
        return true;
    }
}

bool mp_screens_run_lobby(bool as_host)
{
    build_lobby();
    mps.lobby_is_host  = as_host;
    mps.lobby_started  = false;
    mps.lobby_arm_refused = false;
    mps.lobby_gone        = MP_LOBBY_OVER_NO;
    mps.lobby_error[0] = '\0';

    /* The choice is the screen's, not the session's, so it goes when the screen opens. */
    mps.lobby_from_save   = false;
    mps.lobby_save_wait_ms = 0u;
    mps.lobby_hero        = 0u;
    mps.lobby_level_index = 0u;
    mps.lobby_level[0]    = '\0';
    mps.lobby_title[0]    = '\0';
    mps.lobby_save[0]     = '\0';

    /* The transport comes up HERE, on the way in. A host that cannot bind and a client that
     * cannot even try are both told so on the band rather than left with an empty list. */
    mps.lobby_arm_refused = !mp_menu_arm(mps.settings);
    if (mps.lobby_arm_refused) {
        /* The refusals are told apart from the same facts menu_arm judged: a transport that stands
         * with no role armed is the ini's or the environment's; one the menu armed cannot change
         * sides or port while a level runs; a host with no transport whose hosting was refused has
         * a DLL of its own that blocks it, and the band names it; no transport otherwise is the
         * socket or the address. The name is a file's own of any length and stands last in the
         * sentence, so the band's cut takes its end and not the words. */
        const char *why = mp_text(MP_TEXT_ARM_NO_CONNECTION);
        char        blocked[CAPTION_MAX];
        char        text[CAPTION_MAX];

        if (mp_bridge_installed()) {
            uint32_t armed = mp_menu_armed_role();

            why = mp_text(armed == 0u ? MP_TEXT_ARM_ROLE_FROM_INI
                          : armed == (uint32_t)MP_SETTINGS_ROLE_HOST
                          ? MP_TEXT_ARM_ALREADY_HOST
                          : MP_TEXT_ARM_ALREADY_CLIENT);
        } else if (as_host && mp_mod_allow_hosting_blocked(blocked, sizeof blocked)) {
            text_format(text, sizeof text, mp_text(MP_TEXT_ARM_FOREIGN_DLL), blocked);
            why = text;
        }
        FIT_SYSFONT(why, LOB_BAND_W, mps.lobby_error);
    } else if (!as_host) {
        client_connects();
    }
    /* Refusals from before this lobby are not this lobby's: the password question is put for the
     * ones that arrive from here on. */
    mps.lobby_denials_answered = mp_bridge_lobby_denials();
    mp_bridge_lobby_reset();
    if (as_host) {
        mp_screens_choose_default();
        mp_screens_publish_choice();
        mp_bridge_set_lobby(MP_LOBBY_TEAM_NONE, true);   /* a host is always ready */
    } else {
        /* A client scans the catalogue too, and until now only the host did.
         *
         * Without it mp_levels_shipped answers NULL for every index, so a client always took the
         * by-path way in and its campaign index stayed at -1. Two things follow from that number,
         * and both are worse than they look: when a co-op level ends the campaign steps to 0 and
         * loads the first level of the game rather than the next one, and the restart arm of the
         * death screen indexes the level table at -1, which is a read in front of the table whose
         * result is then dereferenced as a string.
         *
         * This line was refused once before, and rightly: while mp_start_shipped wrote only the
         * counter the campaign had already copied, a filled catalogue would have pushed the client
         * into the table path and landed it on the first level of the game every time. That
         * counter is written now, so the path is the right one. */
        (void)mp_levels_scan();
        mp_bridge_set_lobby(MP_LOBBY_TEAM_NONE, false);
    }
    mp_bridge_set_lobby_hero(mps.lobby_hero);

    /* From here the idle pump does what a substep would: without this the session connects and
     * the lobby never learns it. */
    mp_bridge_lobby_set_open(true);
    lobby_frame(NULL);
    ++mps.opened;
    (void)mp_screen_run(&mps.lobby, ID_LOBBY_ROW0 + (int32_t)(mps.lobby_rows - 1u),
                        &lobby_activate, &lobby_frame, NULL);
    mp_bridge_lobby_set_open(false);

    /* Backed out, or the host ended it: the session goes with the screen, by the one exit, so the
     * next lobby starts from nothing and a host that left admits nobody any more. A lobby whose
     * arming was refused has nothing of its own to take down. */
    if (!mps.lobby_started && !mps.lobby_arm_refused) {
        mp_menu_exit(mps.lobby_gone != MP_LOBBY_OVER_NO ? MP_EXIT_WHY_HOST_GONE
                                                        : MP_EXIT_WHY_LOBBY_BACK);
    }
    if (mps.lobby_gone == MP_LOBBY_OVER_CONTENT) {
        mp_menu_screens_run_notice(mp_text(MP_TEXT_DENY_MODE));
    } else if (mps.lobby_gone == MP_LOBBY_OVER_BEHIND) {
        mp_menu_screens_run_notice(mp_text(MP_TEXT_OVER_BEHIND));
    } else if (mps.lobby_gone != MP_LOBBY_OVER_NO) {
        mp_menu_screens_run_notice(mp_text(mps.lobby_gone == MP_LOBBY_OVER_HOST_LOST
                                               ? MP_TEXT_OVER_HOST_LOST
                                               : MP_TEXT_OVER_HOST_ENDED));
    }
    return mps.lobby_started;
}
