/* mp_menu_screens_band.c: the lobby's two lines of text, the band and the level line.
 *
 * The band is the STATUS line: what is going on, and the only place a refused join is ever
 * explained to the player. The level line says what will be PLAYED, and the rule set with it.
 * Both are written every frame out of the bridge's facts, and neither decides anything: which rows
 * the lobby has, who may start and when are mp_menu_screens_lobby.c's.
 *
 * Out of mp_menu_screens_lobby.c, whose size note named this seam when that file stood near the
 * size limit. What came with the two lines is what only they read, the words for a refused join,
 * the relay's and the join's sentences and the rule set, and the count of players not yet ready,
 * which the host's start asks as well.
 *
 * A refusal for the game data, a required mod or a DLL outside this release says what differs
 * here, and the two sides' values go into the player list, which has two rows free for them while
 * no roster stands; the band alone could not hold a mod's name and two builds. Both come out of one
 * function, so the sentence and the rows are one reading of one refusal.
 */
#include "mp_menu_screens_int.h"

#include "mp_bridge.h"
#include "mp_bridge_content.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_lobby_late.h"
#include "mp_lobby.h"
#include "mp_mod_census.h"
#include "mp_mod_manifest_rule.h"
#include "mp_settings.h"

#include "common/text.h"

/* ==============================================================================================
 * Why a join was refused, in words. Every sentence on the band is cut to its column, so they are
 * kept short enough to arrive whole in the four hundred pixels the bar has.
 * ============================================================================================ */

/* Every reason a host can send has a word here. A reason without one is no refusal to
 * mp_lobby_join_status, and the band of a refused client would go on saying it connects. */
static const char *deny_word(mp_deny_reason_t reason)
{
    switch (reason) {
    case MP_DENY_FULL:        return mp_text(MP_TEXT_DENY_FULL);
    case MP_DENY_PROTOCOL:    return mp_text(MP_TEXT_DENY_PROTOCOL);
    case MP_DENY_CONTENT:     return mp_text(MP_TEXT_DENY_CONTENT);
    case MP_DENY_MODE:        return mp_text(MP_TEXT_DENY_MODE);
    case MP_DENY_PASSWORD:    return mp_text(MP_TEXT_DENY_PASSWORD);
    case MP_DENY_MODS:        return mp_text(MP_TEXT_DENY_MODS);
    case MP_DENY_FOREIGN_DLL: return mp_text(MP_TEXT_DENY_FOREIGN_DLL);
    case MP_DENY_NONE:
    default:                  return NULL;
    }
}

/* The sentence that says what differs, for a refusal whose detail this build can read. A reason
 * and a sub it does not know, from a later host, answer NULL, and the band keeps the reason's word:
 * a sentence guessed for a case this build has never seen would say something the host did not.
 * The sub numbers repeat across reasons, so the reason is asked first. A DLL outside this release
 * is named when the host's list does not name it and when it is the first of more than the request
 * could name; a request with no readable list names none and keeps the word. */
static const char *refusal_format(const mp_mod_refusal_t *refusal)
{
    if (refusal->mod[0] == '\0') {
        return NULL;
    }
    if (refusal->reason == MP_MOD_REFUSE_FOREIGN) {
        switch (refusal->sub) {
        case MP_MOD_SUB_NOT_ALLOWED: return mp_text(MP_TEXT_REFUSED_NOT_ALLOWED);
        case MP_MOD_SUB_NOT_NAMED:   return mp_text(MP_TEXT_REFUSED_TOO_MANY);
        default:                     return NULL;
        }
    }
    if (refusal->reason == MP_MOD_REFUSE_GAME_DATA) {
        if (refusal->sub != MP_MOD_SUB_DAMAGE && refusal->sub != MP_MOD_SUB_ROSTER) {
            return NULL;
        }
        /* A roster fingerprint 0 is a characters.ini that side of the join did not have: the host
         * refuses it as other data, and the player reads that it is missing, and where. Both sides
         * at 0 are equal and are never refused. A damage table at 0 is one the shot table was not
         * read for, not a missing file, and reads as other data. */
        if (refusal->sub == MP_MOD_SUB_ROSTER && refusal->host_stamp == 0u) {
            return mp_text(MP_TEXT_REFUSED_MISSING_AT_HOST);
        }
        if (refusal->sub == MP_MOD_SUB_ROSTER && refusal->here_stamp == 0u) {
            return mp_text(MP_TEXT_REFUSED_MISSING_HERE);
        }
        return mp_text(MP_TEXT_REFUSED_OTHER);
    }
    if (refusal->reason != MP_MOD_REFUSE_MODS) {
        return NULL;
    }
    switch (refusal->sub) {
    case MP_MOD_SUB_MISSING_AT_HOST:   return mp_text(MP_TEXT_REFUSED_MISSING_AT_HOST);
    case MP_MOD_SUB_MISSING_AT_JOINER: return mp_text(MP_TEXT_REFUSED_MISSING_HERE);
    case MP_MOD_SUB_OTHER_BUILD:       return mp_text(MP_TEXT_REFUSED_OTHER);
    default:                           return NULL;
    }
}

/* One side's value on its row, empty where the refusal has none for that side. A data file's
 * fingerprint as eight hex digits, which is no date; a mod's release number and the local time it
 * was built, the same words the log uses; or the word for the side that does not have the mod or
 * characters.ini, whose roster fingerprint is then 0. A build with no release number of its own,
 * one without a version resource, shows the date alone.
 *
 * A DLL outside this release the host's list does not name: the host has no build of it, so its
 * row says what it made of it, and this side's shows the DLL's build as this side's census holds
 * it, the release number only for another release of this project. Nothing for this side when its
 * census does not hold the name, and no rows at all for more DLLs than named, where the host
 * judged how many there are and no build. */
static void side_value(const mp_mod_refusal_t *refusal, bool here, char *out, size_t size)
{
    const uint8_t  lacking = here ? MP_MOD_SUB_MISSING_AT_JOINER : MP_MOD_SUB_MISSING_AT_HOST;
    const uint32_t stamp   = here ? refusal->here_stamp : refusal->host_stamp;
    const char    *version = here ? refusal->here_version : refusal->host_version;
    char           built[32];

    out[0] = '\0';
    if (refusal->reason == MP_MOD_REFUSE_FOREIGN) {
        if (refusal->sub != MP_MOD_SUB_NOT_ALLOWED ||
            (here && stamp == 0u && version[0] == '\0')) {
            return;
        }
        if (!here) {
            text_format(out, size, "%s", mp_text(MP_TEXT_REFUSED_NOT_ALLOWED_WORD));
            return;
        }
    } else if ((refusal->reason == MP_MOD_REFUSE_MODS && refusal->sub == lacking) ||
               (refusal->reason == MP_MOD_REFUSE_GAME_DATA &&
                refusal->sub == MP_MOD_SUB_ROSTER && stamp == 0u)) {
        text_format(out, size, "%s", mp_text(MP_TEXT_REFUSED_MISSING));
        return;
    } else if (refusal->reason == MP_MOD_REFUSE_GAME_DATA) {
        text_format(out, size, "%08X", (unsigned)stamp);
        return;
    }
    mp_mod_census_describe_stamp(stamp, built, sizeof built);
    if (version[0] != '\0') {
        text_format(out, size, "%s, %s", version, built);
    } else {
        text_format(out, size, "%s", built);
    }
}

/* One row of the player list: the side's word and its value, or empty where there is no value. */
static void side_row(const mp_mod_refusal_t *refusal, bool here, char *out, size_t size)
{
    char value[64];

    side_value(refusal, here, value, sizeof value);
    if (value[0] == '\0') {
        out[0] = '\0';
        return;
    }
    text_format(out, size, mp_text(here ? MP_TEXT_REFUSED_HERE_ROW : MP_TEXT_REFUSED_HOST_ROW),
                value);
}

bool mp_screens_refusal_lines(const mp_mod_refusal_t *refusal, char *band, size_t band_size,
                              char *host_row, char *here_row, size_t row_size)
{
    const char *format = (refusal != NULL) ? refusal_format(refusal) : NULL;

    if (format == NULL) {
        return false;
    }
    if (band != NULL && band_size != 0u) {
        text_format(band, band_size, format, refusal->mod);
    }
    if (host_row != NULL && here_row != NULL && row_size != 0u) {
        side_row(refusal, false, host_row, row_size);
        side_row(refusal, true, here_row, row_size);
    }
    return true;
}

/* ==============================================================================================
 * The band.
 * ============================================================================================ */

/* How many players have not said they are ready, and the names of the first of them. The host may
 * not start over somebody still choosing, and the band has to say who is being waited for. */
size_t mp_screens_waiting_players(char *out, size_t out_size)
{
    mp_roster_t roster;
    uint8_t     ready[MP_ROSTER_MAX_ENTRIES];
    size_t      count;
    size_t      waiting;
    size_t      at = 0;
    size_t      i;

    if (out != NULL && out_size != 0u) {
        out[0] = '\0';
    }
    if (!mp_bridge_roster_current(&roster)) {
        return 0;   /* nobody is here yet, which the band says in its own words */
    }
    count = roster.count < MP_ROSTER_MAX_ENTRIES ? roster.count : MP_ROSTER_MAX_ENTRIES;
    for (i = 0; i < count; ++i) {
        ready[i] = roster.entry[i].ready;
    }
    waiting = mp_settings_lobby_waiting(ready, count);
    for (i = 0; i < count && out != NULL; ++i) {
        size_t length;

        if (roster.entry[i].ready != 0u) {
            continue;
        }
        length = strlen(roster.entry[i].name);
        if (at + length + 3u >= out_size) {
            break;
        }
        if (at != 0u) {
            memcpy(out + at, ", ", 2u);
            at += 2u;
        }
        memcpy(out + at, roster.entry[i].name, length);
        at += length;
        out[at] = '\0';
    }
    return waiting;
}

/* A client that is connected and waits: for its host in a lobby before the start, or for itself
 * in a lobby that opened on a session already running. That start is held until this player says
 * ready, so the band says what to do, and then that the level is about to begin, which it says on
 * the same answer the frame takes the start on. It is the last thing the band says: an error line,
 * the host's savegame, the relay, a refusal and a lost host are each said ahead of it. */
static const char *connected_line(void)
{
    mp_lobby_setup_t setup;

    if (!mp_bridge_lobby_joins_running_session()) {
        return mp_text(MP_TEXT_JOIN_CONNECTED);
    }
    return mp_text(mp_screens_lobby_start_to_take(&setup) ? MP_TEXT_BAND_RUNNING_READY
                                                         : MP_TEXT_BAND_RUNNING_PICK);
}

/* The band: the one line that says what is going on, and the only place a refused join is ever
 * explained to the player. Every sentence is kept under the band's 400 pixels: two were over it
 * and were being cut, the content mismatch at 439 and the missing savegame level at 427. */
/* The one line about a client's join: the lobby's facts, in the order mp_lobby_join_status
 * decides. A refusal for a reason this build cannot name is no refusal to it; one for the game
 * data, a mod or a DLL whose detail names what differs says that, in `out`. */
static const char *join_line(char *out, size_t size)
{
    const char      *denied = deny_word(mp_bridge_lobby_last_deny());
    mp_mod_refusal_t refusal;

    if (denied != NULL && mp_bridge_content_refusal(&refusal) &&
        mp_screens_refusal_lines(&refusal, out, size, NULL, NULL, 0u)) {
        denied = out;
    }
    switch (mp_lobby_join_status(mp_bridge_lobby_content_mismatch(), denied != NULL,
                                 mp_bridge_lobby_host_left(), mp_bridge_lobby_gave_up(),
                                 mp_bridge_lobby_connected())) {
    case MP_LOBBY_JOIN_CONTENT:   return mp_text(MP_TEXT_CONTENT_MISMATCH);
    case MP_LOBBY_JOIN_DENIED:    return denied;
    case MP_LOBBY_JOIN_HOST_LEFT: return mp_text(MP_TEXT_JOIN_HOST_LEFT);
    case MP_LOBBY_JOIN_GAVE_UP:   return mp_text(MP_TEXT_JOIN_GAVE_UP);
    case MP_LOBBY_JOIN_CONNECTED: return connected_line();
    case MP_LOBBY_JOIN_ASKING:
    default:                      return mp_text(MP_TEXT_JOIN_CONNECTING);
    }
}

/* The one line about the relay while it is not ready: nothing of a public session happens before
 * it is, so the band says that rather than a join or a count of players. */
static const char *relay_line(void)
{
    switch (mp_bridge_public_state()) {
    case MP_RELAY_STATE_FAILED:
        switch (mp_bridge_public_link_failure()) {
        case MP_RELAY_LINK_UNKNOWN_CODE: return mp_text(MP_TEXT_RELAY_NO_SESSION);
        case MP_RELAY_LINK_FULL:         return mp_text(MP_TEXT_RELAY_FULL);
        case MP_RELAY_LINK_CLOSED:       return mp_text(MP_TEXT_RELAY_CLOSED);
        default:                         return mp_text(MP_TEXT_RELAY_FAILED);
        }
    case MP_RELAY_STATE_WAITING:
        return mp_text(MP_TEXT_RELAY_RETRYING);
    default:
        return mp_text(MP_TEXT_RELAY_CONNECTING);
    }
}

void mp_screens_refresh_band(void)
{
    char said[CAPTION_MAX];

    if (mps.lobby_error[0] != '\0') {
        FIT_SYSFONT(mps.lobby_error, LOB_BAND_W, mps.lobby_band);
        return;
    }
    if (mp_bridge_public_bound() && mp_bridge_public_state() != MP_RELAY_STATE_READY) {
        FIT_SYSFONT(relay_line(), LOB_BAND_W, mps.lobby_band);
        return;
    }
    if (mps.lobby_is_host) {
        char   text[CAPTION_MAX];
        char   names[CAPTION_MAX];
        size_t waiting = mp_screens_waiting_players(names, sizeof names);

        if (waiting != 0u) {
            text_format(text, sizeof text, mp_text(MP_TEXT_BAND_NOT_READY), names);
            FIT_SYSFONT(text, LOB_BAND_W, mps.lobby_band);
            return;
        }
        text_format(text, sizeof text, mp_text(MP_TEXT_BAND_ALL_READY),
                    (unsigned)mp_bridge_lobby_population());
        FIT_SYSFONT(text, LOB_BAND_W, mps.lobby_band);
        return;
    }
    FIT_SYSFONT(join_line(said, sizeof said), LOB_BAND_W, mps.lobby_band);
}

/* ==============================================================================================
 * The level line.
 * ============================================================================================ */

/* The rule set is shown on the level line, and that is a decision rather than a convenience.
 *
 * The lobby has two lines of text and four control rows, and all four rows are already spoken for
 * in a deathmatch host's lobby. Of the two lines, the band is the STATUS line: it carries the
 * error, the reason a join was refused and the list of players who are not ready yet, and all
 * three change from frame to frame, so a rule set put there would appear and vanish. The level
 * line says what is being PLAYED, and the rules are part of that, so they go here.
 *
 * A rule set that is not known yet shows nothing rather than the defaults, because the defaults
 * would be a claim about a session nobody has described. */
static void rules_line(const mp_lobby_setup_t *setup, char *out, size_t out_size)
{
    char points[16];
    char minutes[16];

    if (setup->mode != (uint8_t)MP_LOBBY_MODE_TDM) {
        /* In a campaign the only rule with an effect is whether the players can hurt each
         * other, and the answer to that is normally no. */
        text_format(out, out_size, mp_text(MP_TEXT_RULES_COOP),
                    mp_text(mp_rules_friendly_fire(&setup->rules) ? MP_TEXT_RULES_ON
                                                                  : MP_TEXT_RULES_OFF));
        return;
    }
    if (setup->rules.score_limit != 0u) {
        text_format(points, sizeof points, mp_text(MP_TEXT_RULES_POINTS),
                    (unsigned)setup->rules.score_limit);
    } else {
        text_format(points, sizeof points, "%s", mp_text(MP_TEXT_RULES_NO_POINTS));
    }
    if (setup->rules.time_limit_s != 0u) {
        text_format(minutes, sizeof minutes, mp_text(MP_TEXT_RULES_MINUTES),
                    (unsigned)(setup->rules.time_limit_s / 60u));
    } else {
        text_format(minutes, sizeof minutes, "%s", mp_text(MP_TEXT_RULES_NO_TIME));
    }
    text_format(out, out_size, mp_text(MP_TEXT_RULES_TDM), points, minutes,
                mp_text(mp_rules_teams(&setup->rules) ? MP_TEXT_RULES_TEAMS : MP_TEXT_RULES_FREE),
                mp_text(mp_rules_friendly_fire(&setup->rules) ? MP_TEXT_RULES_ON
                                                              : MP_TEXT_RULES_OFF));
}

/* How wide a string is in the font the level line is drawn in, so the title can be cut by exactly
 * as much as the rules need and no more. Cutting the whole line instead would drop the rules off
 * the end for any level whose name is long. */
static int32_t courier_width(const char *text)
{
    int32_t width = 0;
    size_t  i;

    for (i = 0; text[i] != '\0'; ++i) {
        unsigned char glyph = (unsigned char)text[i];

        width += glyph < 128u ? (int32_t)MP_MENU_ADVANCE_COURIER[glyph] : 0;
    }
    return width;
}

/* What will be played, on the left of the lower window. A client reads it out of the host's
 * repeated note, so it shows the host's choice rather than its own. */
void mp_screens_refresh_level(void)
{
    mp_lobby_setup_t setup;

    if (mp_bridge_lobby_setup(&setup)) {
        char text[CAPTION_MAX];
        char head[CAPTION_MAX];
        char rules[CAPTION_MAX];
        int32_t room;

        rules_line(&setup, rules, sizeof rules);
        room = LOB_LEVEL_W - courier_width(rules) - courier_width("  ");
        if (room < 40) {
            room = 40;   /* a rule set wider than the line still leaves the title something */
        }
        text_format(text, sizeof text, "%s%s", setup.title,
                    (setup.flags & MP_LOBBY_F_FROM_SAVE) != 0u ? mp_text(MP_TEXT_LEVEL_FROM_SAVE)
                                                                : "");
        mp_screens_fit(MP_MENU_ADVANCE_COURIER, text, room, head, sizeof head);
        text_format(text, sizeof text, "%s  %s", head, rules);
        FIT_COURIER(text, LOB_LEVEL_W, mps.lobby_choice);
        return;
    }
    FIT_COURIER(mp_text(mps.lobby_is_host ? MP_TEXT_LEVEL_NONE_HOST : MP_TEXT_LEVEL_NONE_CLIENT),
                LOB_LEVEL_W, mps.lobby_choice);
}
