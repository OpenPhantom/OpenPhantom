/* chat_key_row.c: the chat's key row, held against the multiplayer's own reader.
 *
 * The row writes [multiplayer] ChatKey and multiplayer.dll reads it. The two DLLs share no code, so
 * the grammar is written twice, once in each, and this program links both and asks them the same
 * questions. It also drives the row through the settings file, because the defect it exists for is
 * a row that writes a spelling the reader turns down: the row would show the new key while the
 * chat went on opening on T.
 *
 * The rest of the panel is stood in for here. The key that opens it is F6 while OpenKey is unset
 * and the bound key otherwise, as in the real panel; the free camera's key is K; the two placement
 * keys are J and L.
 */
#include "unittest.h"

#include "chat_key_row.h"
#include "cheats_openphantom.h"
#include "open_key_row.h"
#include "overlay_input.h"
#include "overlay_key_name.h"
#include "overlay_notice.h"
#include "spawn_keys.h"

#include "mp_board.h"
#include "mp_chat_key_rule.h"

#include "common/ini.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SECTION "multiplayer"

#define VK_F6_KEY 0x75
#define VK_J_KEY  0x4A
#define VK_K_KEY  0x4B
#define VK_L_KEY  0x4C
#define VK_P_KEY  0x50
#define VK_Q_KEY  0x51
#define VK_U_KEY  0x55

static int32_t panel_key;

bool overlay_input_opens_on(int32_t virtual_key)
{
    return (panel_key == 0) ? virtual_key == VK_F6_KEY : virtual_key == panel_key;
}

void overlay_input_set_key(int32_t virtual_key)
{
    panel_key = virtual_key;
}

int32_t cheats_openphantom_freecam_hotkey(void)
{
    return VK_K_KEY;
}

bool spawn_keys_is(spawn_key_t which, int32_t virtual_key)
{
    return (which == SPAWN_KEY_PLACE && virtual_key == VK_J_KEY) ||
           (which == SPAWN_KEY_FACE && virtual_key == VK_L_KEY);
}

static void raw(const char *key, char *out, size_t size)
{
    (void)ini_read_string(SECTION, key, "", out, size);
}

/* Whether the band holds a whole sentence that begins as given. The band cuts a sentence that is
 * too long with two dots, so a sentence ending in them was written too long for it. */
static bool band_says(const char *start)
{
    const char *said = overlay_notice_text();
    size_t      length;

    if (said == NULL || strncmp(said, start, strlen(start)) != 0) {
        return false;
    }
    length = strlen(said);
    return length < 2u || strcmp(said + length - 2u, "..") != 0;
}

/* Every string of one to three printable characters, and a few longer ones, asked of both
 * readers. */
static void test_the_two_grammars_agree(void)
{
    static const char *const LONGER[] = { "TAB ", "tabs", "F123", "F012", "F0012", "F1 2", "T\t" };
    char     text[4];
    uint32_t asked = 0;
    uint32_t differ = 0;
    uint32_t keys = 0;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    size_t   i;

    ut_section("the row reads a name exactly as the multiplayer does");
    ut_check(chat_key_row_code(NULL) == mp_board_key_code(NULL) && chat_key_row_code(NULL) == 0,
             "no text is no key for either");
    for (a = 0x20u; a <= 0x7Eu; ++a) {
        for (b = 0x1Fu; b <= 0x7Eu; ++b) {
            for (c = 0x1Fu; c <= 0x7Eu; ++c) {
                /* 0x1F stands for the end of the string, so the one and two character strings
                 * come out of the same loops as the three character ones. */
                text[0] = (char)a;
                text[1] = (b == 0x1Fu) ? '\0' : (char)b;
                text[2] = (b == 0x1Fu || c == 0x1Fu) ? '\0' : (char)c;
                text[3] = '\0';
                if (b == 0x1Fu && c != 0x1Fu) {
                    continue;
                }
                ++asked;
                if (chat_key_row_code(text) != mp_board_key_code(text)) {
                    ++differ;
                }
                if (mp_board_key_code(text) != 0) {
                    ++keys;
                }
            }
        }
    }
    for (i = 0; i < sizeof LONGER / sizeof LONGER[0]; ++i) {
        ++asked;
        if (chat_key_row_code(LONGER[i]) != mp_board_key_code(LONGER[i])) {
            ++differ;
        }
    }
    ut_checkf(differ == 0u, "%u strings asked of both readers, %u answered differently", asked,
              differ);
    ut_checkf(asked == 95u + 95u * 95u + 95u * 95u * 95u + 7u,
              "every printable string of one, two and three characters was among them (%u)",
              asked);
    /* 26 letters twice, 10 digits, TAB in eight cases, F1 to F12 in two cases, and F01 to F09
     * in two: a count that would fall to zero if the loops above asked nothing. */
    ut_checkf(keys == 52u + 10u + 8u + 24u + 18u, "and %u of them name a key", keys);
    ut_check(chat_key_row_code("F01") == 0x70 && chat_key_row_code("F13") == 0,
             "including the multiplayer's oddities: F01 is F1, and F13 is no key");
}

/* What the row shows for a ChatKey and a ScoreboardKey, with the file taken out: the same steps as
 * chat_key_row_get, from its own public parts. */
static int32_t row_shows(const char *chat_name, const char *scoreboard_name)
{
    int32_t scoreboard = chat_key_row_code(scoreboard_name);
    int32_t chat = chat_key_row_code(chat_name);

    if (scoreboard == 0) {
        scoreboard = 0x09;
    }
    return (chat_key_row_judge(chat, scoreboard, false) == CHAT_KEY_ACCEPTED)
               ? chat : chat_key_row_fallback(scoreboard);
}

/* The key the row shows and the key the multiplayer's chat opens on, for every name of one and
 * two printable characters beside a handful of scoreboard keys. The two DLLs keep one list of
 * refusals each, and this is where the two lists have to agree. */
static void test_the_row_shows_the_key_the_chat_opens_on(void)
{
    static const char *const BOARDS[] = { "TAB", "T", "t", "U", "Q", "M", "5", "F1", "F10", "",
                                          "no such key", "84" };
    char                  text[3];
    uint32_t              asked = 0;
    uint32_t              differ = 0;
    uint32_t              fallbacks_differ = 0;
    mp_chat_key_verdict_t verdict;
    uint32_t              a;
    uint32_t              b;
    size_t                i;
    int32_t               vk;

    ut_section("the row shows the key the multiplayer's chat opens on");
    for (vk = 0; vk <= 0xFF; ++vk) {
        if (chat_key_row_fallback(vk) != mp_chat_key_fallback(vk)) {
            ++fallbacks_differ;
        }
    }
    ut_checkf(fallbacks_differ == 0u, "the fallback agrees for every scoreboard key (%u differ)",
              fallbacks_differ);
    for (i = 0; i < sizeof BOARDS / sizeof BOARDS[0]; ++i) {
        for (a = 0x20u; a <= 0x7Eu; ++a) {
            for (b = 0x1Fu; b <= 0x7Eu; ++b) {
                text[0] = (char)a;
                text[1] = (b == 0x1Fu) ? '\0' : (char)b;
                text[2] = '\0';
                ++asked;
                if (row_shows(text, BOARDS[i]) != mp_chat_key_pick(text, BOARDS[i], &verdict)) {
                    ++differ;
                }
            }
        }
    }
    ut_checkf(differ == 0u && asked == 12u * (95u + 95u * 95u),
              "%u pairs of a chat name and a scoreboard name asked of both, %u answered "
              "differently", asked, differ);
}

static void test_every_name_the_row_writes(void)
{
    char    name[CHAT_KEY_ROW_NAME_MAX];
    char    small[3];
    int32_t vk;
    int     named = 0;
    int     lost = 0;

    ut_section("every name the row can write reads back as the same key");
    for (vk = 0; vk <= 0xFF; ++vk) {
        if (!chat_key_row_name(vk, name, sizeof name)) {
            if (name[0] != '\0') {
                ++lost;
            }
            continue;
        }
        ++named;
        if (mp_board_key_code(name) != vk || chat_key_row_code(name) != vk) {
            ++lost;
        }
    }
    ut_checkf(named == 26 + 10 + 12 && lost == 0,
              "A to Z, 0 to 9 and F1 to F12 have a name (%d), and each reads back through "
              "mp_board_key_code as itself (%d did not)", named, lost);
    ut_check(!chat_key_row_name(0x7B, small, sizeof small) && small[0] == '\0',
             "a buffer too small for F12 holds nothing rather than F1");
}

static void test_what_is_refused(void)
{
    static const int32_t SHAPE[] = { 0x00, 0x08, 0x09, 0x0D, 0x10, 0x11, 0x12, 0x1B, 0x20,
                                     0x25, 0x60, 0x6B, 0x7C, 0xC0, 0xDC };
    static const int32_t RESERVED[] = { 0x4D, 0x73, 0x75, 0x76, 0x77, 0x79, 0x7A, 0x7B };
    char    name[CHAT_KEY_ROW_NAME_MAX];
    int32_t vk;
    size_t  i;
    bool    all = true;
    int     reserved = 0;

    ut_section("which keys the row turns down");
    for (i = 0; i < sizeof SHAPE / sizeof SHAPE[0]; ++i) {
        all = all && chat_key_row_judge(SHAPE[i], 0x09, false) == CHAT_KEY_REFUSED_SHAPE;
    }
    ut_check(all, "no key without a name the multiplayer reads: Backspace, Tab, Return, Shift, "
                  "Ctrl, Alt, Escape, Space, the arrows, the number pad, F13 and the key below "
                  "Escape");
    all = true;
    for (i = 0; i < sizeof RESERVED / sizeof RESERVED[0]; ++i) {
        all = all && chat_key_row_judge(RESERVED[i], 0x09, false) == CHAT_KEY_REFUSED_RESERVED;
    }
    ut_check(all, "M, F4, F6, F7, F8, F10, F11 and F12, which the multiplayer refuses as well");
    for (vk = 0; vk <= 0xFF; ++vk) {
        if (chat_key_row_name(vk, name, sizeof name) &&
            chat_key_row_judge(vk, 0x09, false) == CHAT_KEY_REFUSED_RESERVED) {
            ++reserved;
        }
    }
    ut_checkf(reserved == 8, "and no other key with a name (%d refused)", reserved);
    ut_check(chat_key_row_judge(VK_Q_KEY, VK_Q_KEY, false) == CHAT_KEY_REFUSED_SCOREBOARD &&
                 chat_key_row_judge(VK_Q_KEY, 0x09, false) == CHAT_KEY_ACCEPTED,
             "the scoreboard's key, and only while it is the scoreboard's");
    ut_check(chat_key_row_judge(VK_U_KEY, 0x09, true) == CHAT_KEY_REFUSED_TAKEN &&
                 chat_key_row_judge(0x54, 0x09, false) == CHAT_KEY_ACCEPTED,
             "a key this panel uses; T, the default, is free");
}

static void test_through_the_file(void)
{
    char    text[32];
    int32_t vk;
    int     written = 0;
    int     lost = 0;
    int     taken = 0;

    ut_section("the row through the settings file");
    ut_check(ini_write_string(SECTION, "ChatKey", "T") &&
                 ini_write_string(SECTION, "ScoreboardKey", "TAB") &&
                 ini_write_string(SECTION, "ChatKey", NULL) &&
                 ini_write_string(SECTION, "ScoreboardKey", NULL),
             "the settings file beside the test can be written, and both keys are gone from it");
    ut_check(chat_key_row_get() == 0x54, "a file with no ChatKey opens the chat on T");

    for (vk = 0; vk <= 0xFF; ++vk) {
        if (chat_key_row_judge(vk, 0x09, false) != CHAT_KEY_ACCEPTED) {
            continue;
        }
        if (vk == VK_J_KEY || vk == VK_K_KEY || vk == VK_L_KEY) {
            taken += chat_key_row_set(vk) ? 0 : 1;
            continue;
        }
        if (!chat_key_row_set(vk)) {
            ++lost;
            continue;
        }
        ++written;
        raw("ChatKey", text, sizeof text);
        if (mp_board_key_code(text) != vk || chat_key_row_get() != vk) {
            ++lost;
        }
    }
    ut_checkf(written == 37 && lost == 0,
              "each of the %d keys the row takes is written so that mp_board_key_code reads it "
              "straight back out of the file, and the row shows it (%d did not)", written, lost);
    ut_check(taken == 3, "the free camera's key and the two placement keys are refused as taken");

    ut_check(chat_key_row_set(VK_U_KEY), "U is taken as the chat key");
    raw("ChatKey", text, sizeof text);
    ut_check(strcmp(text, "U") == 0, "and written as its name, not as the number 85");
    ut_check(band_says("Saved: in a session it works within a second"),
             "the band says a session takes it within a second, whole");
    ut_check(overlay_notice_confirms(),
             "and says it as a confirmation, so it is not drawn in the colour of a refusal");

    ut_section("what the row shows for what a person typed");
    (void)ini_write_string(SECTION, "ChatKey", "84");
    ut_check(mp_board_key_code("84") == 0 && chat_key_row_get() == CHAT_KEY_ROW_DEFAULT,
             "84 is no key to the multiplayer, so the chat opens on T and the row says T");
    (void)ini_write_string(SECTION, "ChatKey", "5");
    ut_check(chat_key_row_get() == '5' && mp_board_key_code("5") == '5',
             "5 is the 5 key to both");
    ut_check(overlay_key_from_name("5", &vk) && vk == 5,
             "which the panel's other reader would take for key code 5, so the row cannot use it");
    (void)ini_write_string(SECTION, "ChatKey", "f9");
    ut_check(chat_key_row_get() == 0x78, "f9 is F9, case ignored");
    (void)ini_write_string(SECTION, "ChatKey", "M");
    ut_check(chat_key_row_get() == CHAT_KEY_ROW_DEFAULT,
             "M, which the multiplayer refuses, shows the T it falls back to");
    (void)ini_write_string(SECTION, "ChatKey", "Ctrl");
    ut_check(chat_key_row_get() == CHAT_KEY_ROW_DEFAULT, "so does a word the grammar lacks");

    ut_section("the scoreboard's key");
    (void)ini_write_string(SECTION, "ChatKey", "Q");
    (void)ini_write_string(SECTION, "ScoreboardKey", "q");
    ut_check(chat_key_row_get() == CHAT_KEY_ROW_DEFAULT,
             "a chat key the scoreboard also holds shows T");
    ut_check(!chat_key_row_set(VK_Q_KEY) && band_says("Refused: that key already holds the"),
             "and binding the scoreboard's key is refused, with a sentence that fits the band");
    (void)ini_write_string(SECTION, "ScoreboardKey", "no such key");
    ut_check(chat_key_row_get() == VK_Q_KEY,
             "a scoreboard name the multiplayer cannot read is TAB there, so Q is free again");

    ut_section("the fallback when the scoreboard holds T");
    ut_check(chat_key_row_fallback(0x54) == VK_U_KEY && chat_key_row_fallback(0x09) == 0x54 &&
                 chat_key_row_fallback(VK_U_KEY) == 0x54,
             "the fallback is U while the scoreboard's key is T, and T for every other");
    (void)ini_write_string(SECTION, "ScoreboardKey", "T");
    (void)ini_write_string(SECTION, "ChatKey", NULL);
    ut_check(chat_key_row_get() == VK_U_KEY,
             "a file with no ChatKey and the scoreboard on T opens the chat on U");
    (void)ini_write_string(SECTION, "ChatKey", "T");
    ut_check(chat_key_row_get() == VK_U_KEY,
             "and ChatKey=T beside the scoreboard on T shows U, the key the chat falls back to");
    (void)ini_write_string(SECTION, "ChatKey", "M");
    ut_check(chat_key_row_get() == VK_U_KEY, "as does a refused key");
    (void)ini_write_string(SECTION, "ChatKey", NULL);
    (void)ini_write_string(SECTION, "ScoreboardKey", NULL);
}

static void test_refusals_leave_the_file_alone(void)
{
    static const int32_t REFUSED[] = { 0x1B, 0x0D, 0x08, 0x09, 0x4D, 0x73, 0x79, 0x7B,
                                       VK_J_KEY, VK_K_KEY, VK_L_KEY, VK_F6_KEY };
    char   before[32];
    char   after[32];
    size_t i;
    bool   kept = true;
    bool   said = true;

    ut_section("a refused key leaves the file and the row as they were");
    ut_check(chat_key_row_set(VK_U_KEY), "U is the chat key before the refusals");
    raw("ChatKey", before, sizeof before);
    for (i = 0; i < sizeof REFUSED / sizeof REFUSED[0]; ++i) {
        overlay_notice_act();
        if (chat_key_row_set(REFUSED[i])) {
            kept = false;
        }
        raw("ChatKey", after, sizeof after);
        kept = kept && strcmp(before, after) == 0 && chat_key_row_get() == VK_U_KEY;
        said = said && band_says("Refused: ") && !overlay_notice_confirms();
    }
    ut_check(kept, "Escape, Return, Backspace, Tab, M, F4, F10, F12, the placement keys, the free "
                   "camera's key and the panel's own are refused and the file keeps U");
    ut_check(said, "each refusal stands in the band as a whole sentence, and as a refusal");
    overlay_notice_act();
    (void)chat_key_row_set(0x60);
    ut_check(band_says("Refused: a chat key is a letter, digit or F1 to F12"),
             "a key with no name says what the chat key may be");
    overlay_notice_act();
    (void)chat_key_row_set(0x7A);
    ut_check(band_says("Refused: that key already does something else"),
             "one the game or the panel keeps says it is in use");
    overlay_notice_act();
    (void)chat_key_row_set(VK_K_KEY);
    ut_check(band_says("Refused: that key is already bound in this panel"),
             "and one this panel binds says so");
}

static void test_the_panel_key_gives_way(void)
{
    ut_section("the key that opens the panel and the chat's key are never one key");
    (void)ini_write_string("dev_overlay", "OpenKey", NULL);
    panel_key = 0;
    ut_check(chat_key_row_set(VK_U_KEY), "the chat opens on U");
    overlay_notice_act();
    ut_check(!open_key_row_set(VK_U_KEY) && open_key_row_get() == 0 && panel_key == 0,
             "so the panel's own row refuses U and keeps its default");
    ut_check(band_says("Refused: that key opens the chat"), "and says why");
    ut_check(open_key_row_set(VK_P_KEY) && panel_key == VK_P_KEY,
             "P is free for the panel");
    ut_check(!chat_key_row_set(VK_P_KEY), "after which the chat's row refuses P");
    ut_check(open_key_row_set(0), "the panel's default can always be put back");
    (void)ini_write_string("dev_overlay", "OpenKey", NULL);
    (void)ini_write_string(SECTION, "ChatKey", NULL);
}

int main(void)
{
    test_the_two_grammars_agree();
    test_the_row_shows_the_key_the_chat_opens_on();
    test_every_name_the_row_writes();
    test_what_is_refused();
    test_through_the_file();
    test_refusals_leave_the_file_alone();
    test_the_panel_key_gives_way();
    return ut_summary("the chat key row");
}
