/* overlay_legend.c: what the panel's footer says.
 *
 * Three rules, and each of them is the kind that is wrong in a way nobody notices. The four key
 * caps are the only place the panel says it can be driven from a keyboard at all. The right hand
 * end says either how much is on the open tab or that a multiplayer session is running and which
 * end of it this machine is, and the session half must never appear when none runs, because a
 * panel that claims a session in single player is worse than one that says nothing. And the
 * shortening: the footer may not make the panel wider, so when the room runs out it gives up its
 * own parts in a fixed order rather than pushing the rows' text off their own line.
 *
 * The session note is the real one, published into this process, because that is the channel
 * session_lock.c reads and a stub of it would prove nothing about it.
 */
#include "unittest.h"

#include "overlay_layout.h"
#include "overlay_legend.h"
#include "overlay_look.h"
#include "session_lock.h"

#include "common/session_note.h"
#include "common/text.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* What session_lock.c reaches for and this program has none of. The spawner's link is the
 * multiplayer's answer and the console's table belongs to the image; neither decides anything
 * here. */
bool npc_spawn_link_active(void)
{
    return false;
}

const char *cheats_original_name(uint32_t index)
{
    (void)index;
    return NULL;
}

/* What overlay_layout.c reaches for in its hit test and in keeping the selection on screen,
   neither of which this program uses: the layout is here for the width it computes and for
   nothing else. There is no selection, so the reveal never scrolls anything. */
uint32_t overlay_model_scroll(uint32_t visible)
{
    (void)visible;
    return 0u;
}

uint32_t overlay_model_row_count(void)
{
    return 0u;
}

int32_t overlay_model_selected(void)
{
    return -1;
}

void overlay_model_scroll_by(int32_t rows)
{
    (void)rows;
}

static void session(bool running, bool host)
{
    session_note_t note;

    memset(&note, 0, sizeof note);
    note.running = running;
    note.is_host = host;
    ut_check(session_note_publish(&note), running ? "a session is published" : "and then ended");
    session_lock_refresh();
}

static void test_the_caps(void)
{
    const overlay_legend_key_t *key;

    ut_section("the keys, which are the only place the panel says it has any");
    key = overlay_legend_key(0u);
    ut_check(key != NULL && strcmp(key->cap, "Up/Down") == 0 && strcmp(key->what, "move") == 0,
             "the arrows move the selection");
    key = overlay_legend_key(1u);
    ut_check(key != NULL && strcmp(key->cap, "Left/Right") == 0,
             "left and right fold a heading or change the tab");
    key = overlay_legend_key(2u);
    ut_check(key != NULL && strcmp(key->cap, "Return") == 0, "Return acts on the current row");
    key = overlay_legend_key(3u);
    ut_check(key != NULL && strcmp(key->cap, "Esc") == 0 && strcmp(key->what, "close") == 0,
             "and Escape closes; it is said here and nowhere else, because one sentence in "
             "two places is the pair that drifts apart");
    ut_check(overlay_legend_key(OVERLAY_LEGEND_KEYS) == NULL, "there is no fifth");
}

static void test_the_right_hand_end(void)
{
    char said[32];

    ut_section("the tally, and the session that replaces it");
    session(false, false);
    overlay_legend_right(said, sizeof said, 11u, 78u);
    ut_check(strcmp(said, "11 groups - 78 rows") == 0,
             "with no session it is what the open tab holds");
    overlay_legend_right(said, sizeof said, 2u, 18u);
    ut_check(strcmp(said, "2 groups - 18 rows") == 0, "and it follows the tab that is open");

    session(true, true);
    overlay_legend_right(said, sizeof said, 11u, 78u);
    ut_check(strcmp(said, "session - host") == 0,
             "a session running replaces the tally, and says which end of it this machine is");
    ut_check(strstr(said, "player") == NULL,
             "and says no number of players: the note carries whether a session runs and who "
             "hosts it, and nothing else, so a count here would be invented");

    session(true, false);
    overlay_legend_right(said, sizeof said, 11u, 78u);
    ut_check(strcmp(said, "session - client") == 0, "the other end says so");

    session(false, false);
    overlay_legend_right(said, sizeof said, 11u, 78u);
    ut_check(strcmp(said, "11 groups - 78 rows") == 0,
             "and when it ends the tally is back, with no trace of the session left on the band");
    said[0] = '\0';
    overlay_legend_right(said, 0u, 11u, 78u);
    ut_check(said[0] == '\0', "no room writes nothing rather than one byte past the end");
}

/* The room is the panel's, and the panel is as wide as its longest row needs. Nothing clamps that
 * against the display's width, so a footer that asked for more would either push a row's own text
 * out of its line or run off a narrow screen. */
static void test_the_shortening(void)
{
    ut_section("what the footer gives up when the room runs out");
    ut_check(overlay_legend_fit(1000.0f, 200.0f, 120.0f, 90.0f) == OVERLAY_LEGEND_FULL,
             "with room to spare it shows the caps, the words beside them and the tally");
    ut_check(overlay_legend_fit(400.0f, 200.0f, 120.0f, 90.0f) == OVERLAY_LEGEND_CAPS,
             "one size down the grey words go first: they are the part a player can do without");
    ut_check(overlay_legend_fit(220.0f, 200.0f, 120.0f, 90.0f) == OVERLAY_LEGEND_BARE,
             "and the tally goes next, so the last thing left is which keys work");
    ut_check(overlay_legend_fit(0.0f, 200.0f, 120.0f, 90.0f) == OVERLAY_LEGEND_BARE,
             "no room at all answers the same, rather than asking for width the rows did not");

    /* The boundary, because that is where a rule of this shape goes wrong: the gap it keeps
     * between the caps and the tally is a quarter of the caps, so a room of exactly caps plus gap
     * plus tally still holds them, and a hair under it does not. */
    ut_check(overlay_legend_fit(250.0f, 200.0f, 120.0f, 0.0f) == OVERLAY_LEGEND_CAPS,
             "a room that holds the caps and the gap and nothing else keeps the caps");
    ut_check(overlay_legend_fit(249.0f, 200.0f, 120.0f, 0.0f) == OVERLAY_LEGEND_BARE,
             "and a hair under it drops to the caps alone");

    /* It never goes back up. Which of the three pictures is shown must not depend on anything but
     * the room, or the footer would change as the panel is scrolled or a group folded. */
    ut_check(overlay_legend_fit(1000.0f, 200.0f, 120.0f, 90.0f) == OVERLAY_LEGEND_FULL &&
                 overlay_legend_fit(2000.0f, 200.0f, 120.0f, 90.0f) == OVERLAY_LEGEND_FULL,
             "more room than the line needs changes nothing");

    /* What the pixels actually are on a 640 by 480 screen is the engine font's answer and not
     * this program's: the widths above are the caller's measurements. The section below asks
     * the narrower question that CAN be answered here, which is whether the caps fit the room
     * the layout really gives them. */
}

/* How wide the caps are, in the caller's own unit, at an average character advance of `r`
   text heights. Measured off the real caps rather than off a character count written here, so
   a fifth key or a longer word moves this with it. The padding is paint_cap's own 0.4 H on
   each side. */
static void cap_widths_at(float r, float text_h, float *out)
{
    uint32_t i;

    for (i = 0; i < OVERLAY_LEGEND_KEYS; ++i) {
        const overlay_legend_key_t *key = overlay_legend_key(i);

        out[i] = ((float)strlen(key->cap) * r + 0.8f) * text_h;
    }
}

/* The room the footer really has, from the real layout: the panel's width less the margin it
   keeps inside each border. `content_width` is zero so the panel comes out at its narrowest,
   which is the case the caps have to survive. */
static float narrowest_room(float text_h, float screen_w, float screen_h)
{
    static const float tabs[OVERLAY_LAYOUT_TABS] = { 64.0f, 96.0f };
    const layout_t    *lay;

    overlay_layout_build(text_h, 0.0f, 200u, tabs, screen_w, screen_h);
    lay = overlay_layout();
    return lay->width - 2.0f * OVERLAY_EDGE_PAD * lay->text_h;
}

/* The caps alone, against the room the panel really has.
 *
 * overlay_legend_fit()'s last answer is "the caps alone", and this asks whether those fit. The
 * engine clips nothing, so a cap past the right edge is drawn over the game.
 *
 * 640 by 480 at the smallest dev menu size, at the authored one, which is also the largest a 480
 * line screen allows, and at four times it, and 1280 by 720 beside it. They all give the same
 * answer for the same font, and that is a finding rather than an oversight: prepare_font() sets the
 * glyph scale to DevMenuSize * authored / screen, which cancels the resolution, and every width in
 * this band is a multiple of the measured H. What decides the picture is the average character
 * advance against H, and nothing else, so the resolution cannot distinguish anything; the one
 * number worth measuring in the game is that advance. */
static void test_the_caps_are_clamped(void)
{
    static const struct { float text_h; float w; float h; const char *what; } SCREENS[] = {
        {  4.0f,  640.0f, 480.0f, "640 by 480 at the smallest dev menu size" },
        { 12.0f,  640.0f, 480.0f, "640 by 480 at the authored size" },
        { 48.0f,  640.0f, 480.0f, "640 by 480 at four times the authored size" },
        { 16.0f, 1280.0f, 720.0f, "1280 by 720" }
    };
    const uint32_t screens = (uint32_t)(sizeof SCREENS / sizeof SCREENS[0]);
    float          box[OVERLAY_LEGEND_KEYS];
    uint32_t       i;

    ut_section("the caps themselves, against the room the panel really has");

    for (i = 0; i < screens; ++i) {
        const float room = narrowest_room(SCREENS[i].text_h, SCREENS[i].w, SCREENS[i].h);
        char        note[160];

        cap_widths_at(0.55f, SCREENS[i].text_h, box);
        text_format(note, sizeof note, "a narrow font shows all four caps on %s",
                    SCREENS[i].what);
        ut_check(overlay_legend_caps_that_fit(room, 0.5f * SCREENS[i].text_h, box) ==
                     OVERLAY_LEGEND_KEYS, note);

        cap_widths_at(0.75f, SCREENS[i].text_h, box);
        text_format(note, sizeof note,
                    "and a wide one drops the last cap rather than drawing it off the panel "
                    "on %s", SCREENS[i].what);
        ut_check(overlay_legend_caps_that_fit(room, 0.5f * SCREENS[i].text_h, box) <
                     OVERLAY_LEGEND_KEYS, note);
    }

    /* The band is 22.5 text heights wide at the panel's narrowest, whatever the screen and
       whatever the dev menu size. If that ever stops being true the two checks above become
       four copies of one case. */
    ut_check(narrowest_room(12.0f, 640.0f, 480.0f) == 22.5f * 12.0f &&
                 narrowest_room(48.0f, 640.0f, 480.0f) == 22.5f * 48.0f &&
                 narrowest_room(16.0f, 1280.0f, 720.0f) == 22.5f * 16.0f,
             "the narrowest room is the same number of text heights on every screen");

    /* The ends. Nothing may be drawn in no room, and a caller with no widths gets nothing
       rather than four caps of an unknown size. */
    cap_widths_at(0.65f, 12.0f, box);
    ut_check(overlay_legend_caps_that_fit(0.0f, 6.0f, box) == 0u,
             "no room draws no caps");
    ut_check(overlay_legend_caps_that_fit(10000.0f, 6.0f, NULL) == 0u,
             "and no measurements draw none either, rather than four of an unknown width");
    ut_check(overlay_legend_caps_that_fit(10000.0f, 6.0f, box) == OVERLAY_LEGEND_KEYS,
             "room to spare draws all four and never a fifth");
}

/* While a key row waits for its key, the next key of any kind is the binding, Escape and Return
 * included, so every cap on the line names a key that would not do what the cap says. The line
 * says what the panel waits for instead, because three dots in the row's chip are too little and
 * "Esc close" beside them would be wrong for as long as the wait lasts. */
static void test_the_prompt_while_a_key_is_awaited(void)
{
    const char *prompt;
    char        note[160];

    ut_section("the footer while a key row waits for its key");
    ut_check(overlay_legend_prompt(false) == NULL,
             "with nothing awaited there is no prompt, and the caps stand as they always did");
    prompt = overlay_legend_prompt(true);
    ut_check(prompt != NULL && strcmp(prompt, "press a key") == 0,
             "while a key is awaited the footer says press a key");
    if (prompt == NULL) {
        return;
    }
    /* Held against the narrowest footer at the widest average advance the caps are held to, with
     * no box around it: if this ever fails, the prompt is cut on a small panel. */
    text_format(note, sizeof note, "\"%s\" fits the narrowest footer at a wide font",
                prompt);
    ut_check((float)strlen(prompt) * 0.75f * 12.0f <= narrowest_room(12.0f, 640.0f, 480.0f),
             note);
}

int main(void)
{
    test_the_caps();
    test_the_right_hand_end();
    test_the_shortening();
    test_the_caps_are_clamped();
    test_the_prompt_while_a_key_is_awaited();
    return ut_summary("the panel's footer");
}
