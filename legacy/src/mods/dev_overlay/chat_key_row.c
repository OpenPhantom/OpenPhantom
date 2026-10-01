/* chat_key_row.c: see chat_key_row.h. */
#include "chat_key_row.h"

#include "cheats_openphantom.h"
#include "overlay_input.h"
#include "overlay_notice.h"
#include "spawn_keys.h"

#include "common/ini.h"
#include "common/logging.h"
#include "common/text.h"

#define MULTIPLAYER_SECTION "multiplayer"
#define CHAT_KEY_KEY        "ChatKey"
#define SCOREBOARD_KEY_KEY  "ScoreboardKey"

/* Room for any value a person might type. A name the grammar accepts is three characters at most,
 * so a longer value stays no key as long as neither reader cuts it below four characters. */
#define CHAT_KEY_TEXT_MAX 32u

/* Virtual key codes, written as numbers the way the multiplayer's reader writes them. */
#define CHAT_VK_TAB 0x09
#define CHAT_VK_F1  0x70
#define CHAT_VK_F12 0x7B

static bool is_tab(const char *name)
{
    return (name[0] == 'T' || name[0] == 't') && (name[1] == 'A' || name[1] == 'a') &&
           (name[2] == 'B' || name[2] == 'b') && name[3] == '\0';
}

/* Deliberately the multiplayer's reading and nothing kinder, including its one oddity: "F01" is F1
 * there, so it is F1 here. A friendlier reader would show a key the chat does not open on. */
int32_t chat_key_row_code(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (is_tab(name)) {
        return CHAT_VK_TAB;
    }
    if (name[1] == '\0') {
        /* A letter or a digit is its own code on this platform, upper case for a letter. */
        if (name[0] >= 'a' && name[0] <= 'z') {
            return (int32_t)(name[0] - 'a' + 'A');
        }
        if ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9')) {
            return (int32_t)name[0];
        }
        return 0;
    }
    if ((name[0] == 'F' || name[0] == 'f') && name[1] >= '0' && name[1] <= '9') {
        int32_t number = name[1] - '0';

        if (name[2] >= '0' && name[2] <= '9' && name[3] == '\0') {
            number = number * 10 + (name[2] - '0');
        } else if (name[2] != '\0') {
            return 0;
        }
        if (number >= 1 && number <= (CHAT_VK_F12 - CHAT_VK_F1 + 1)) {
            return CHAT_VK_F1 + number - 1;
        }
    }
    return 0;
}

bool chat_key_row_name(int32_t vk, char *out, size_t size)
{
    size_t wanted;
    size_t stored;

    if (out == NULL || size == 0u) {
        return false;
    }
    out[0] = '\0';
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        wanted = 1u;
        stored = text_format(out, size, "%c", (char)vk);
    } else if (vk >= CHAT_VK_F1 && vk <= CHAT_VK_F12) {
        wanted = (vk - CHAT_VK_F1 + 1 >= 10) ? 3u : 2u;
        stored = text_format(out, size, "F%d", (int)(vk - CHAT_VK_F1 + 1));
    } else {
        return false;
    }
    /* Half a name is a different key, "F1" out of "F12", so a buffer too small holds none. */
    if (stored != wanted) {
        out[0] = '\0';
        return false;
    }
    return true;
}

/* The shape first, because only a key this row can write as a name is one the multiplayer can
 * read back. Then the keys the multiplayer refuses whatever the file says, each for a reason of its
 * own: M opens the multiplayer's own menu; F4 is half of Alt+F4 and the free camera's way out; F6
 * is the key this panel opens on by default, and the multiplayer cannot see whether it was
 * rebound; the engine's graphics handler sees every key before the hook the chat sits on and keeps
 * F7 and F8 for the gamma and F11 and F12 for the resolution; and F10 arrives as a system key,
 * which the chat does not open on. Then the scoreboard's key, since one key cannot hold the board
 * and open the chat. Last what this panel already uses, which the multiplayer cannot know about and
 * so does not refuse. */
chat_key_refusal_t chat_key_row_judge(int32_t vk, int32_t scoreboard_vk, bool taken_in_panel)
{
    char name[CHAT_KEY_ROW_NAME_MAX];

    if (!chat_key_row_name(vk, name, sizeof name)) {
        return CHAT_KEY_REFUSED_SHAPE;
    }
    switch (vk) {
    case 0x4D:      /* M */
    case 0x73:      /* F4 */
    case 0x75:      /* F6 */
    case 0x76:      /* F7 */
    case 0x77:      /* F8 */
    case 0x79:      /* F10 */
    case 0x7A:      /* F11 */
    case 0x7B:      /* F12 */
        return CHAT_KEY_REFUSED_RESERVED;
    default:
        break;
    }
    if (vk == scoreboard_vk) {
        return CHAT_KEY_REFUSED_SCOREBOARD;
    }
    return taken_in_panel ? CHAT_KEY_REFUSED_TAKEN : CHAT_KEY_ACCEPTED;
}

/* The scoreboard's key as the multiplayer reads it, which falls back to TAB for a name it cannot
 * read. TAB is never a chat key, so that fallback refuses nothing here. */
static int32_t scoreboard_key(void)
{
    char    text[CHAT_KEY_TEXT_MAX];
    int32_t code;

    (void)ini_read_string(MULTIPLAYER_SECTION, SCOREBOARD_KEY_KEY, "TAB", text, sizeof text);
    code = chat_key_row_code(text);
    return (code != 0) ? code : CHAT_VK_TAB;
}

/* The keys this panel already reads: the ones that open it, the two placement keys, and the free
 * camera's teleport, which the camera polls whatever else has the keyboard. */
static bool taken_in_this_panel(int32_t vk)
{
    return overlay_input_opens_on(vk) || spawn_keys_is(SPAWN_KEY_PLACE, vk) ||
           spawn_keys_is(SPAWN_KEY_FACE, vk) ||
           (vk != 0 && cheats_openphantom_freecam_hotkey() == vk);
}

int32_t chat_key_row_fallback(int32_t scoreboard_vk)
{
    return (scoreboard_vk == CHAT_KEY_ROW_DEFAULT) ? CHAT_KEY_ROW_SECOND : CHAT_KEY_ROW_DEFAULT;
}

int32_t chat_key_row_get(void)
{
    char    text[CHAT_KEY_TEXT_MAX];
    int32_t scoreboard = scoreboard_key();
    int32_t vk;

    if (!ini_read_string(MULTIPLAYER_SECTION, CHAT_KEY_KEY, "", text, sizeof text)) {
        return chat_key_row_fallback(scoreboard);
    }
    /* What the chat will open on, which is the fallback for anything the multiplayer turns down:
     * showing the file's word instead would name a key that does nothing. The panel's own claims
     * are left out, because the multiplayer does not know them and opens on such a key all the
     * same. */
    vk = chat_key_row_code(text);
    if (chat_key_row_judge(vk, scoreboard, false) != CHAT_KEY_ACCEPTED) {
        return chat_key_row_fallback(scoreboard);
    }
    return vk;
}

bool chat_key_row_set(int32_t vk)
{
    char name[CHAT_KEY_ROW_NAME_MAX];

    /* The row goes on showing the key it already has, so the band is the only sign of a refusal. */
    switch (chat_key_row_judge(vk, scoreboard_key(), taken_in_this_panel(vk))) {
    case CHAT_KEY_ACCEPTED:
        break;
    case CHAT_KEY_REFUSED_SHAPE:
        overlay_notice_say("Refused: a chat key is a letter, digit or F1 to F12");
        return false;
    case CHAT_KEY_REFUSED_SCOREBOARD:
        overlay_notice_say("Refused: that key already holds the scoreboard");
        return false;
    case CHAT_KEY_REFUSED_TAKEN:
        overlay_notice_say("Refused: that key is already bound in this panel");
        return false;
    case CHAT_KEY_REFUSED_RESERVED:
    default:
        overlay_notice_say("Refused: that key already does something else");
        return false;
    }
    (void)chat_key_row_name(vk, name, sizeof name);
    if (!ini_write_string(MULTIPLAYER_SECTION, CHAT_KEY_KEY, name)) {
        log_warning("chat key: [multiplayer] ChatKey=%s could not be written to the settings file",
                    name);
        overlay_notice_say("That key could not be saved to the settings file");
        return false;
    }
    log_info("chat key: [multiplayer] ChatKey is now %s", name);
    /* Said although nothing was refused, and put up as a confirmation so it is not drawn in the
     * colour of a refusal. The multiplayer reads the file once a second, so for up to a second in
     * a session the old key still opens the chat, and a player who tried the new one at once
     * would take the binding for lost. */
    overlay_notice_confirm("Saved: in a session it works within a second");
    return true;
}
