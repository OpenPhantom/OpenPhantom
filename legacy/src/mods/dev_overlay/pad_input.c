/* pad_input.c: see pad_input.h.
 *
 * XInputGetState is all the reading there is, once a frame on the game thread, the same call
 * controller_input makes on a thread of its own; two readers of one pad are fine, the API keeps
 * no state per caller. A slot with no pad in it is the expensive case of that call, documented
 * by Microsoft, so an empty slot is asked again only every two seconds.
 *
 * The reading alone lives here, so the free camera can link it without the panel; what the
 * panel does with a frame of it is pad_panel.c's.
 */
#include "pad_input.h"

#include "common/ini.h"
#include "common/logging.h"
#include "common/text.h"

#include <windows.h>
#include <xinput.h>

#include <math.h>
#include <string.h>

_Static_assert(PAD_BUTTON_LEFT_THUMB == XINPUT_GAMEPAD_LEFT_THUMB &&
               PAD_BUTTON_RIGHT_THUMB == XINPUT_GAMEPAD_RIGHT_THUMB &&
               PAD_BUTTON_X == XINPUT_GAMEPAD_X && PAD_BUTTON_Y == XINPUT_GAMEPAD_Y &&
               PAD_BUTTON_START == XINPUT_GAMEPAD_START,
               "the button bits in pad_input.h are XInput's");
_Static_assert(PAD_BUTTON_DPAD_UP == XINPUT_GAMEPAD_DPAD_UP &&
               PAD_BUTTON_DPAD_DOWN == XINPUT_GAMEPAD_DPAD_DOWN &&
               PAD_BUTTON_DPAD_LEFT == XINPUT_GAMEPAD_DPAD_LEFT &&
               PAD_BUTTON_DPAD_RIGHT == XINPUT_GAMEPAD_DPAD_RIGHT &&
               PAD_BUTTON_BACK == XINPUT_GAMEPAD_BACK &&
               PAD_BUTTON_LEFT_SHOULDER == XINPUT_GAMEPAD_LEFT_SHOULDER &&
               PAD_BUTTON_RIGHT_SHOULDER == XINPUT_GAMEPAD_RIGHT_SHOULDER &&
               PAD_BUTTON_A == XINPUT_GAMEPAD_A && PAD_BUTTON_B == XINPUT_GAMEPAD_B,
               "the button bits in pad_input.h are XInput's");

#define SECTION           "dev_overlay"
#define PAD_SECTION       "controller_input"   /* the slot is that DLL's setting; read, not owned */
#define DEADZONE_DEFAULT  0.24f    /* controller_input's own, XInput's 8689 of 32767 rounded */
#define POINTER_DEFAULT   0.6f     /* screen widths a second with the stick fully over */
#define LOOK_DEFAULT      120.0f   /* degrees a second with the stick fully over */
#define RETRY_SECONDS     2.0f     /* how often an empty slot is asked again */
#define DT_MOST           0.1f     /* a stall or the first frame counts as this at most */
/* The button that opens the panel, on its press. The game's own joystick reading sees the
 * same press and runs whatever its controls screen has on that button, which is the player's
 * to clear; a half second hold of it was tried first and gave the game the whole half second,
 * and both stick clicks in its place were on the game's list too (played 2026-09-16). */
#define OPEN_DEFAULT      "View"

static struct {
    bool     enabled;
    int32_t  slot;
    float    deadzone;
    float    pointer_speed;
    float    look_speed;
    uint32_t open_buttons;     /* held together to open or close the panel */
    bool     found;            /* a pad answered last time, so a loss is reported once */
    bool     ever_found;
    float    retry_in;         /* seconds until an empty slot is asked again */
    uint32_t held_before;
    LARGE_INTEGER frequency;
    LONGLONG last_tick;
    pad_state_t state;
} st;

void pad_shape_stick(int16_t raw_x, int16_t raw_y, float deadzone, float *out_x, float *out_y)
{
    float x = (float)raw_x / 32767.0f;
    float y = (float)raw_y / 32767.0f;
    float magnitude = sqrtf(x * x + y * y);
    float scale;

    if (magnitude <= deadzone || magnitude <= 0.0f) {
        *out_x = 0.0f;
        *out_y = 0.0f;
        return;
    }
    if (magnitude > 1.0f) {
        x /= magnitude;   /* the corners of a square report past the circle */
        y /= magnitude;
        magnitude = 1.0f;
    }
    scale = (deadzone < 1.0f) ? ((magnitude - deadzone) / (1.0f - deadzone)) / magnitude : 0.0f;
    *out_x = x * scale;
    *out_y = y * scale;
}

uint32_t pad_buttons_named(const char *words)
{
    static const struct {
        const char *name;
        uint32_t    bit;
    } NAMES[] = {
        { "A", PAD_BUTTON_A }, { "B", PAD_BUTTON_B }, { "X", PAD_BUTTON_X }, { "Y", PAD_BUTTON_Y },
        { "LB", PAD_BUTTON_LEFT_SHOULDER }, { "RB", PAD_BUTTON_RIGHT_SHOULDER },
        { "LS", PAD_BUTTON_LEFT_THUMB }, { "RS", PAD_BUTTON_RIGHT_THUMB },
        { "View", PAD_BUTTON_BACK }, { "Back", PAD_BUTTON_BACK },
        { "Menu", PAD_BUTTON_START }, { "Start", PAD_BUTTON_START },
        { "Up", PAD_BUTTON_DPAD_UP }, { "Down", PAD_BUTTON_DPAD_DOWN },
        { "Left", PAD_BUTTON_DPAD_LEFT }, { "Right", PAD_BUTTON_DPAD_RIGHT }
    };
    uint32_t bits = 0;
    const char *at = words;

    while (at != NULL && *at != '\0') {
        char     word[16];
        uint32_t n = 0;
        uint32_t i;

        while (*at == ' ' || *at == ',' || *at == '+') {
            at++;
        }
        while (*at != '\0' && *at != ' ' && *at != ',' && *at != '+' && n + 1u < sizeof word) {
            word[n++] = *at++;
        }
        word[n] = '\0';
        if (n == 0) {
            break;
        }
        for (i = 0; i < sizeof NAMES / sizeof NAMES[0]; ++i) {
            if (_stricmp(word, NAMES[i].name) == 0) {
                bits |= NAMES[i].bit;
            }
        }
    }
    return bits;
}

void pad_input_install(void)
{
    char open_words[64];

    st.enabled       = ini_read_bool(SECTION, "PadEnabled", true);
    if (!ini_read_string(SECTION, "PadOpenButtons", OPEN_DEFAULT, open_words,
                         sizeof open_words)) {
        text_format(open_words, sizeof open_words, "%s", OPEN_DEFAULT);
    }
    st.open_buttons = pad_buttons_named(open_words);
    if (st.open_buttons == 0) {
        log_warning("pad: PadOpenButtons=%s names no button this reads (A B X Y LB RB LS RS View "
                    "Menu Up Down Left Right), so %s is used", open_words, OPEN_DEFAULT);
        st.open_buttons = pad_buttons_named(OPEN_DEFAULT);
        text_format(open_words, sizeof open_words, "%s", OPEN_DEFAULT);
    }
    st.slot          = ini_read_int(PAD_SECTION, "ControllerIndex", 0);
    st.deadzone      = ini_read_float(SECTION, "PadDeadzone", DEADZONE_DEFAULT);
    st.pointer_speed = ini_read_float(SECTION, "PadPointerSpeed", POINTER_DEFAULT);
    st.look_speed    = ini_read_float(SECTION, "PadLookSpeed", LOOK_DEFAULT);
    if (st.slot < 0 || st.slot > 3) {
        st.slot = 0;
    }
    if (!(st.deadzone >= 0.0f && st.deadzone < 1.0f)) {
        st.deadzone = DEADZONE_DEFAULT;
    }
    if (!(st.pointer_speed > 0.0f && st.pointer_speed <= 5.0f)) {
        st.pointer_speed = POINTER_DEFAULT;
    }
    if (!(st.look_speed > 0.0f && st.look_speed <= 720.0f)) {
        st.look_speed = LOOK_DEFAULT;
    }
    QueryPerformanceFrequency(&st.frequency);
    if (!st.enabled) {
        log_info("pad: PadEnabled=0, so the panel and the free camera take nothing from a "
                 "controller");
        return;
    }
    log_info("pad: slot %d is read once a frame for the panel and the free camera. Press %s "
             "to open or close the panel; in it the left stick glides the pointer at %.2f "
             "screen widths a second and the D-pad steps it row by row, A presses, B is "
             "Escape, the triggers move a slider, the right stick scrolls and the bumpers page; "
             "flying, the sticks fly and look at %.0f degrees a second, the triggers climb and "
             "dive, the bumpers change speed, A brings the player here and B leaves them where "
             "they were. Deadzone %.2f.",
             st.slot, open_words, (double)st.pointer_speed, (double)st.look_speed,
             (double)st.deadzone);
}

static float frame_seconds(void)
{
    LARGE_INTEGER now;
    float         dt = 0.0f;

    QueryPerformanceCounter(&now);
    if (st.last_tick != 0 && st.frequency.QuadPart > 0) {
        dt = (float)((double)(now.QuadPart - st.last_tick) / (double)st.frequency.QuadPart);
    }
    st.last_tick = now.QuadPart;
    if (dt < 0.0f || dt > DT_MOST) {
        dt = DT_MOST;
    }
    return dt;
}

/* Reads the pad into st.state; false when there is none this frame. */
static bool read_pad(float dt)
{
    XINPUT_STATE raw;
    DWORD        result;

    if (st.retry_in > 0.0f) {
        st.retry_in -= dt;
        return false;
    }
    memset(&raw, 0, sizeof raw);
    result = XInputGetState((DWORD)st.slot, &raw);
    if (result != ERROR_SUCCESS) {
        if (st.found) {
            log_info("pad: the controller in slot %d has gone", st.slot);
        } else if (!st.ever_found) {
            log_info("pad: no controller in slot %d; asked again every %.0f seconds, quietly",
                     st.slot, (double)RETRY_SECONDS);
            st.ever_found = true;   /* said once */
        }
        st.found    = false;
        st.retry_in = RETRY_SECONDS;
        return false;
    }
    if (!st.found) {
        log_info("pad: a controller answered in slot %d", st.slot);
        st.found      = true;
        st.ever_found = true;
    }
    pad_shape_stick(raw.Gamepad.sThumbLX, raw.Gamepad.sThumbLY, st.deadzone, &st.state.left_x,
                    &st.state.left_y);
    pad_shape_stick(raw.Gamepad.sThumbRX, raw.Gamepad.sThumbRY, st.deadzone, &st.state.right_x,
                    &st.state.right_y);
    {
        float rx = (float)raw.Gamepad.sThumbRX / 32767.0f;

        st.state.raw_right_x = (rx < 0.0f) ? -rx : rx;
    }
    st.state.trigger_left  = (float)raw.Gamepad.bLeftTrigger / 255.0f;
    st.state.trigger_right = (float)raw.Gamepad.bRightTrigger / 255.0f;
    st.state.held    = raw.Gamepad.wButtons;
    st.state.pressed = raw.Gamepad.wButtons & ~st.held_before;
    st.held_before   = raw.Gamepad.wButtons;
    st.state.present = true;
    return true;
}

float pad_input_poll(void)
{
    float dt;

    st.state.present = false;
    if (!st.enabled) {
        return 0.0f;
    }
    dt = frame_seconds();
    (void)read_pad(dt);
    return dt;
}

const pad_state_t *pad_input_state(void)
{
    return &st.state;
}

float pad_input_look_speed(void)
{
    return st.look_speed;
}

float pad_input_pointer_speed(void)
{
    return st.pointer_speed;
}

uint32_t pad_input_open_buttons(void)
{
    return st.open_buttons;
}
