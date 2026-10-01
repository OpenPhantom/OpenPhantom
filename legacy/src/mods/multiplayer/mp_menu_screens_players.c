/* mp_menu_screens_players.c: SPIELER, who is here, on the load game overlay, over the pause
 * screen.
 *
 * Team and ready are read only here. They were buttons, and pressing one mid-level changed a byte
 * that nothing in a running level reads: no respawn is bound to a team, and readiness only means
 * anything before a round starts. A control that does nothing is worse than none, so this screen
 * shows both and the lobby sets them.
 */
#include "mp_menu_screens.h"

#include "mp_menu_screens_int.h"

#include "mp_bridge_lobby.h"
#include "mp_lobby.h"

#include "common/text.h"

/* ==============================================================================================
 * SPIELER: who is here, over the pause screen.
 * ============================================================================================ */

static void build_players(void)
{
    mp_screen_t *s = &mps.players;

    if (s->built) {
        return;
    }
    mp_screen_init(s);
    mp_screens_put_frame(s, MP_BMP_SLOAD, mp_text(MP_TEXT_BACK), SW_ACTION_CANCEL);
    mps.players_list = mp_screen_put_list(s, ID_PLAYERS_LIST, true, LOAD_LIST_X, LOAD_LIST_Y,
                                         LOAD_LIST_W, LOAD_LIST_H);
    (void)mp_screen_put_pic(s, ID_PICTURE, MP_BMP_OBIBIO,
                            LOAD_FRAME_X + (LOAD_FRAME_W - LOAD_PORTRAIT_W) / 2,
                            LOAD_FRAME_Y + (LOAD_FRAME_H - LOAD_PORTRAIT_H) / 2, LOAD_PORTRAIT_W,
                            LOAD_PORTRAIT_H, false);
    (void)mp_screen_put_text(s, ID_PLAYERS_READ1, SW_ACTION_STATIC, mp_text(MP_TEXT_PLAYERS_TITLE),
                             MP_FONT_INDUST,
                             MP_ALIGN_CENTRE, LOAD_READ1_X, LOAD_READ1_Y, LOAD_READ_W, LOAD_READ_H);
    (void)mp_screen_put_text(s, ID_PLAYERS_READ2, SW_ACTION_STATIC, mps.players_read,
                             MP_FONT_INDUST, MP_ALIGN_CENTRE, LOAD_READ1_X, LOAD_READ2_Y,
                             LOAD_READ_W, LOAD_READ_H);
    (void)mp_screen_put_text(s, ID_PLAYERS_TEAM, SW_ACTION_STATIC, mps.players_team,
                             MP_FONT_INDUST, MP_ALIGN_CENTRE, LOAD_SLOT_X, LOAD_SLOT1_Y,
                             LOAD_SLOT_W, LOAD_SLOT_H);
    (void)mp_screen_put_text(s, ID_PLAYERS_READY, SW_ACTION_STATIC, mps.players_ready,
                             MP_FONT_INDUST, MP_ALIGN_CENTRE, LOAD_SLOT_X, LOAD_SLOT2_Y,
                             LOAD_SLOT_W, LOAD_SLOT_H);
    (void)mp_screen_put_text(s, ID_PLAYERS_FOOT, SW_ACTION_STATIC, mps.players_foot,
                             MP_FONT_COURIER, MP_ALIGN_CENTRE, LOAD_FOOT_X, LOAD_FOOT_Y,
                             LOAD_FOOT_W, LOAD_FOOT_H);
}

static const char *team_word(uint8_t team)
{
    return mp_text(team == 1u ? MP_TEXT_TEAM_1 : (team == 2u ? MP_TEXT_TEAM_2 : MP_TEXT_NO_TEAM));
}

/* Whether this session has teams at all, read off the host's own repeated note rather than off
 * this machine's settings: this screen is opened during a game, and a client that joined a typed
 * address never chose a mode of its own. No note yet means no session worth showing teams for. */
static bool session_has_teams(void)
{
    mp_lobby_setup_t setup;

    return mp_bridge_lobby_setup(&setup) && setup.mode == (uint8_t)MP_LOBBY_MODE_TDM;
}

static void players_frame(void *ctx)
{
    const bool  with_team = session_has_teams();
    mp_roster_t roster;
    char        text[MP_ROSTER_MAX_ENTRIES + 1u][MP_SCREEN_ROW_TEXT_MAX];
    const char *rows[MP_ROSTER_MAX_ENTRIES + 1u];
    size_t      count = 0;
    size_t      i;
    int32_t     selected;
    uint8_t     own_team;
    bool        own_ready;
    char        readout[CAPTION_MAX];

    (void)ctx;
    memset(&roster, 0, sizeof roster);
    /* The heading and the lines come out of the same column table, so a column says what stands
     * under it: the old heading promised name, team, status and ping, in that order, over lines
     * that carried the four in another order and gave the host no ping cell at all. The roster
     * is what the bridge holds, and the reliable notes that carry it are drained only inside a
     * substep, so a roster that changes while the pause screen is up shows on the next one. */
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
    mp_screens_set_rows(mps.players_list, LOAD_LIST_W, rows, count);

    selected = mp_screen_get_state(&mps.players, ID_PLAYERS_LIST);
    if (selected >= 1 && (size_t)selected < count) {
        const mp_roster_entry_t *e = &roster.entry[(size_t)selected - 1u];
        char                     upper[MP_ROSTER_NAME_MAX];

        for (i = 0; i < MP_ROSTER_NAME_MAX; ++i) {
            char c = e->name[i];

            upper[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
            if (c == '\0') {
                break;
            }
        }
        upper[MP_ROSTER_NAME_MAX - 1u] = '\0';
        text_format(readout, sizeof readout, "%s - %s", upper, team_word(e->team));
    } else {
        text_format(readout, sizeof readout, mp_text(MP_TEXT_PLAYERS_IN_GAME),
                    (unsigned)(count - 1u));
    }
    FIT_INDUST(readout, LOAD_READ_W, mps.players_read);

    mp_bridge_get_lobby(&own_team, &own_ready);
    FIT_INDUST(team_word(own_team), LOAD_SLOT_W, mps.players_team);
    FIT_INDUST(mp_text(own_ready ? MP_TEXT_READY_WORD : MP_TEXT_WAITING_WORD), LOAD_SLOT_W,
               mps.players_ready);
    FIT_COURIER(mp_text(MP_TEXT_PLAYERS_FOOT), LOAD_FOOT_W, mps.players_foot);
}

static bool players_activate(int32_t id, void *ctx)
{
    (void)ctx;
    if (id == ID_BACK) {
        return false;
    }
    return true;
}

void mp_menu_screens_run_players(void)
{
    if (!mp_bridge_lobby_connected() || !mp_screen_toolkit_ready()) {
        return;
    }
    build_players();
    mp_screen_set_state(&mps.players, ID_PLAYERS_LIST, -1);
    players_frame(NULL);
    ++mps.opened;
    (void)mp_screen_run(&mps.players, ID_PLAYERS_LIST, &players_activate, &players_frame, NULL);
}
