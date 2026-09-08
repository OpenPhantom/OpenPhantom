#include "overlay_window.h"

#include "overlay_key_name.h"

#include "common/ini.h"

#include <stdio.h>
#include <string.h>

#include <windows.h>

/* The section belongs to enhanced_resolution, which is the DLL that owns the window. Writing into
 * another feature's section is the established way a row here reaches a setting it does not own. */
#define RESOLUTION_SECTION "enhanced_resolution"

/* Mirrors window_mode_kind_t, which lives in the other DLL's header and is deliberately not
 * included: these are file format values, and the file is the whole of the contract between the
 * two. A number written here that the other side does not know is refused there and logged, which
 * is the behaviour wanted anyway. */
#define MODE_AUTHENTIC        0
#define MODE_BORDERLESS       1
#define MODE_WINDOWED         2
#define MODE_RESIZABLE        3
#define MODE_BORDERLESS_SIZED 4

typedef enum window_slot {
    WINDOW_MODE_ROW_AUTHENTIC,
    WINDOW_MODE_ROW_BORDERLESS,
    WINDOW_MODE_ROW_WINDOWED,
    WINDOW_MODE_ROW_RESIZABLE,
    WINDOW_MODE_ROW_BORDERLESS_SIZED,
    WINDOW_ROW_SIZE,
    WINDOW_ROW_RELEASE_KEY,
    WINDOW_ROW_FULLSCREEN_KEY,
    WINDOW_ROW_FILL,
    WINDOW_ROW_NOTE
} window_slot_t;

/* Indexed by slot, so it follows the order above and not the numbering in the file. */
static const int32_t SLOT_MODE[] = {
    MODE_AUTHENTIC, MODE_BORDERLESS, MODE_WINDOWED, MODE_RESIZABLE, MODE_BORDERLESS_SIZED
};

/* Thirty two is well past what any display reports once duplicates at other depths and refresh
 * rates are folded together; the ones seen in testing offer around fifteen. */
#define SIZE_LIST_MAX 32u

static struct {
    int32_t width;
    int32_t height;
} size_list[SIZE_LIST_MAX];

static uint32_t size_list_count;
static bool     size_list_open;

/* THE DISPLAY'S OWN LIST, asked of Windows rather than of the engine.
 *
 * The engine has a list too, and enhanced_resolution owns it, but that is a different DLL and
 * feature DLLs in this tree do not depend on each other. Windows answers the same question from the
 * same driver, so the two agree on anything that matters, and the DLL that actually writes the
 * resolution checks its own list before it does. A size offered here that the engine somehow does
 * not know is refused there with a line saying so, rather than reaching the settings file.
 *
 * Ordered by width and then height, and the same size at several depths and refresh rates is one
 * entry rather than several.
 *
 * Built once and kept. What a display offers does not change while a game is running, and this is
 * read while rows are being drawn, which is every frame the panel is open. */
static void build_size_list(void)
{
    DEVMODEA mode;
    DWORD    index = 0;

    if (size_list_count != 0u) {
        return;
    }
    memset(&mode, 0, sizeof mode);
    mode.dmSize = sizeof mode;

    while (EnumDisplaySettingsA(NULL, index, &mode) && size_list_count < SIZE_LIST_MAX) {
        int32_t  w = (int32_t)mode.dmPelsWidth;
        int32_t  h = (int32_t)mode.dmPelsHeight;
        uint32_t at;

        index++;
        memset(&mode, 0, sizeof mode);
        mode.dmSize = sizeof mode;

        /* The floor the engine's own mouse re-centring needs: below roughly 640x480 of client area
         * its warp to client (320,240) lands outside the window and a constant delta accumulates. */
        if (w < 640 || h < 480) {
            continue;
        }
        for (at = 0; at < size_list_count; ++at) {
            if (size_list[at].width == w && size_list[at].height == h) {
                break;                 /* the same size at another depth or refresh rate */
            }
        }
        if (at != size_list_count) {
            continue;
        }

        /* Inserted in order rather than sorted afterwards: the list is short, this runs once, and
         * an ordered list is what makes it readable. */
        for (at = size_list_count; at > 0u; --at) {
            if (size_list[at - 1u].width < w ||
                (size_list[at - 1u].width == w && size_list[at - 1u].height <= h)) {
                break;
            }
            size_list[at] = size_list[at - 1u];
        }
        size_list[at].width  = w;
        size_list[at].height = h;
        size_list_count++;
    }
}

void overlay_window_reset(void)
{
    size_list_open = false;
}

uint32_t overlay_window_row_count(void)
{
    return OVERLAY_WINDOW_BASE_ROWS + (size_list_open ? size_list_count : 0u);
}

/* Where a slot lands once the list is open: the rows after the size row are pushed down by the
 * length of the list, and the slots in between are the list itself. */
static bool slot_is_size_entry(uint32_t slot, uint32_t *out_index)
{
    if (!size_list_open || slot <= (uint32_t)WINDOW_ROW_SIZE ||
        slot > (uint32_t)WINDOW_ROW_SIZE + size_list_count) {
        return false;
    }
    *out_index = slot - (uint32_t)WINDOW_ROW_SIZE - 1u;
    return true;
}

static uint32_t slot_without_list(uint32_t slot)
{
    if (size_list_open && slot > (uint32_t)WINDOW_ROW_SIZE + size_list_count) {
        return slot - size_list_count;
    }
    return slot;
}


static void copy_label(char *out, const char *text)
{
    size_t length = strlen(text);

    if (length >= OVERLAY_LABEL_MAX) {
        length = OVERLAY_LABEL_MAX - 1u;
    }
    memcpy(out, text, length);
    out[length] = 0;
}

/* What the engine-shape row gives back when it is switched off again. Kept here rather than in the
 * settings file because it is a memory of a gesture, not a setting: writing it would put a key in
 * the file that nothing else reads and that a player would have to wonder about. */
static int32_t previous_mode = MODE_AUTHENTIC;

static int32_t current_mode(void)
{
    return ini_read_int(RESOLUTION_SECTION, "WindowMode", MODE_AUTHENTIC);
}

/* What the device actually is, which is not the same question as what the file says.
 *
 * WindowedPresent is read by the engine once, when it builds its device, and the device is never
 * rebuilt. So the value in the file after a player has pressed something here is what the device
 * WILL be, and this is what it IS. The two disagree for exactly as long as it takes to restart.
 *
 * That gap had a visible cost: switching fullscreen off un-greyed the shape rows immediately, so
 * they could be pressed while the device was still exclusive, which is the arrangement where
 * choosing a window changes the display resolution instead. The rows have to follow the device.
 *
 * Read once and kept, rather than read each time, because reading it each time would just be the
 * file again and would answer the wrong question. The first read happens when the group is first
 * drawn, which is necessarily before anything in it can be pressed. */
static bool device_is_windowed(void)
{
    static bool known;
    static bool windowed;

    if (!known) {
        known    = true;
        windowed = ini_read_bool(RESOLUTION_SECTION, "WindowedPresent", false);
    }
    return windowed;
}

/* A shape row is usable only when the device can carry it AND fullscreen is not the choice. */
static bool shape_rows_usable(void)
{
    return device_is_windowed() && current_mode() != MODE_AUTHENTIC;
}

/* True while the file and the device disagree, which is the whole of what a restart would settle. */
static bool restart_is_pending(void)
{
    return device_is_windowed() != (current_mode() != MODE_AUTHENTIC);
}

void overlay_window_row(uint32_t slot, const char *editing_text, bool capturing,
                        overlay_row_t *out)
{
    if (out == NULL) {
        return;
    }
    /* Nothing here is typed into any more: the size is chosen from the display's own list, so no
     * row has an edit in progress to be shown. Kept in the signature because the caller hands the
     * same three things to every group and this one having a different shape would be worse. */
    (void)editing_text;

    {
        uint32_t entry = 0;

        if (slot_is_size_entry(slot, &entry)) {
            int32_t chosen_width  = ini_read_int(RESOLUTION_SECTION, "WindowedWidth", 0);
            int32_t chosen_height = ini_read_int(RESOLUTION_SECTION, "WindowedHeight", 0);

            out->kind      = OVERLAY_ROW_CHEAT;
            out->available = shape_rows_usable() && current_mode() >= MODE_WINDOWED;
            out->on        = size_list[entry].width == chosen_width &&
                             size_list[entry].height == chosen_height;
            out->value[0]  = 0;
            _snprintf(out->label, sizeof out->label, "    %dx%d",
                      (int)size_list[entry].width, (int)size_list[entry].height);
            out->label[sizeof out->label - 1] = 0;
            return;
        }
    }
    slot = slot_without_list(slot);

    out->kind      = OVERLAY_ROW_CHEAT;
    out->on        = false;
    out->available = true;
    out->value[0]  = 0;
    out->expanded  = false;
    out->pending   = false;
    out->fraction  = 0.0f;

    switch ((window_slot_t)slot) {
    case WINDOW_MODE_ROW_AUTHENTIC:
        copy_label(out->label, "Fullscreen (restart to take effect)");
        out->on = current_mode() == MODE_AUTHENTIC;
        return;

    /* Greyed while fullscreen is on, all four of them, which makes the row above a switch that
     * governs the group rather than the first of five equals. Nothing below it describes a shape
     * the game is in while fullscreen is on, so nothing below it should look settable: switch
     * fullscreen off and the shape rows come back, already showing the one that will be used.
     *
     * The model refuses to act on a row it has been told is unavailable, so this is the whole of
     * the gate and there is no second check in the toggle to keep in step with it. */
    case WINDOW_MODE_ROW_BORDERLESS:
        copy_label(out->label, "Borderless, the whole monitor");
        out->on        = current_mode() == MODE_BORDERLESS;
        out->available = shape_rows_usable();
        return;

    case WINDOW_MODE_ROW_WINDOWED:
        copy_label(out->label, "In a window, fixed size");
        out->on        = current_mode() == MODE_WINDOWED;
        out->available = shape_rows_usable();
        return;

    case WINDOW_MODE_ROW_RESIZABLE:
        copy_label(out->label, "In a window you can resize");
        out->on        = current_mode() == MODE_RESIZABLE;
        out->available = shape_rows_usable();
        return;

    case WINDOW_MODE_ROW_BORDERLESS_SIZED:
        copy_label(out->label, "Borderless, at the size below");
        out->on        = current_mode() == MODE_BORDERLESS_SIZED;
        out->available = shape_rows_usable();
        return;

    /* The way back to the engine's own shape, as a choice of its own rather than as the side
     * effect of pressing the lit row. It was that side effect first, and it made the group read as
     * broken: pressing the mode you were already in dropped you to a frameless oversized window,
     * which looks exactly like the window feature having stopped working. A row that is lit and
     * pressed should do nothing, and where you want to go should be a row you can see. */
    /* A list rather than two text boxes. Pressing it opens the sizes this display reports
     * and starts again at the smallest.
     *
     * It was two typed numbers, and a typed number can be one no display offers. That matters more
     * than it sounds: the size chosen here is also written to the game's own settings as the
     * resolution to render at, the engine opens whatever it finds there at startup, and a size it
     * cannot open stops the game before it draws anything. Catching that afterwards and explaining
     * it is worse than not being able to say it.
     *
     * GREYED when the mode in force does not read it, which is the whole-monitor borderless mode
     * and the engine's own shape: a size nothing reads should not look settable. */
    case WINDOW_ROW_SIZE: {
        int32_t width  = ini_read_int(RESOLUTION_SECTION, "WindowedWidth", 0);
        int32_t height = ini_read_int(RESOLUTION_SECTION, "WindowedHeight", 0);

        out->kind      = OVERLAY_ROW_ACTION;
        out->available = shape_rows_usable() && current_mode() >= MODE_WINDOWED;
        copy_label(out->label, size_list_open ? "  Window size (pick one)" : "  Window size");
        if (width > 0 && height > 0) {
            _snprintf(out->value, sizeof out->value, "%dx%d", (int)width, (int)height);
            out->value[sizeof out->value - 1] = 0;
        } else {
            copy_label(out->value, "auto");
        }
        return;
    }

    /* The two bindings grey with everything else. Gating them on the device alone was tried and
     * was wrong to look at: with fullscreen chosen and the restart still pending, two rows stayed
     * lit in a group that was otherwise entirely n/a, which reads as two rows that were forgotten.
     *
     * Greying a binding row does not disable the key it names. Alt and Enter is the way out of
     * fullscreen without opening this panel at all, and taking that away with the panel's own
     * appearance would be a trap rather than a tidy-up. What is given up while these are grey is
     * only the ability to REBIND them. */
    case WINDOW_ROW_RELEASE_KEY: {
        int32_t key = ini_read_int(RESOLUTION_SECTION, "PointerReleaseKey", 0x91);

        out->kind      = OVERLAY_ROW_HOTKEY;
        out->available = shape_rows_usable();
        copy_label(out->label, "Key that frees the mouse");
        if (capturing) {
            copy_label(out->value, "...");
        } else if (key == 0) {
            copy_label(out->value, "none");
        } else {
            overlay_key_name(key, out->value, sizeof out->value);
        }
        return;
    }

    case WINDOW_ROW_FULLSCREEN_KEY: {
        int32_t key = ini_read_int(RESOLUTION_SECTION, "FullscreenToggleKey", 0x0D);

        out->kind      = OVERLAY_ROW_HOTKEY;
        out->available = shape_rows_usable();
        /* The label says Alt because Alt is not part of the binding and cannot be unbound: what
         * this row chooses is the key held WITH it. */
        copy_label(out->label, "Alt + this swaps window and screen");
        if (capturing) {
            copy_label(out->value, "...");
        } else if (key == 0) {
            copy_label(out->value, "none");
        } else {
            overlay_key_name(key, out->value, sizeof out->value);
        }
        return;
    }

    case WINDOW_ROW_FILL:
        copy_label(out->label, "Stretch the picture to the window");
        out->on        = ini_read_bool(RESOLUTION_SECTION, "WindowedFill", true);
        out->available = shape_rows_usable();       /* no window, or not yet a windowed device */
        return;

    /* There WAS a row here for the windowed device, and it should not have been one. It has
     * exactly one correct value for each of the choices above it: a window needs it, because
     * without it an exclusive device owns the screen and asking the engine for a smaller picture
     * sets the real resolution smaller, which was measured leaving a 4K desktop at 800x600; and
     * fullscreen wants it off, because an exclusive device is what fullscreen IS. A row whose only
     * settings are one right answer and one broken one is not a choice, it is a trap. The mode rows
     * write it now, and this reports what that means. */
    case WINDOW_ROW_NOTE:
        out->kind = OVERLAY_ROW_INFO;
        if (restart_is_pending()) {
            copy_label(out->label, "  RESTART to finish: the rest follows then");
        } else {
            copy_label(out->label, "  the display and wrapper follow on restart");
        }
        return;

    default:
        out->kind      = OVERLAY_ROW_INFO;
        out->available = false;
        copy_label(out->label, "");
        return;
    }
}

bool overlay_window_row_is_value(uint32_t slot)
{
    (void)slot;
    return false;              /* the size is chosen from a list now, not typed */
}

bool overlay_window_row_is_key(uint32_t slot)
{
    return slot == (uint32_t)WINDOW_ROW_RELEASE_KEY ||
           slot == (uint32_t)WINDOW_ROW_FULLSCREEN_KEY;
}

/* The mode and the device are written together, because there is only one device setting that
 * works for each mode and letting them drift apart is the whole reason the row for it is gone.
 *
 * A window needs the windowed device: without it the device is exclusive, it owns the screen, and
 * asking the engine for a smaller picture sets the real display resolution smaller. Fullscreen
 * wants it off, because an exclusive device is what fullscreen is.
 *
 * The device is read once when the game starts, so its half lands on the next run. The mode's half
 * is live. That is why the fullscreen row says so on its face. */
static bool write_mode(int32_t mode)
{
    bool ok = ini_write_int(RESOLUTION_SECTION, "WindowMode", mode);

    return ini_write_int(RESOLUTION_SECTION, "WindowedPresent",
                         (mode == MODE_AUTHENTIC) ? 0 : 1) && ok;
}

bool overlay_window_toggle(uint32_t slot)
{
    uint32_t entry = 0;

    /* Choosing one closes the list, which is what a list of choices does: the answer is on the row
     * above now and there is nothing left to pick. */
    if (slot_is_size_entry(slot, &entry)) {
        size_list_open = false;
        return ini_write_int(RESOLUTION_SECTION, "WindowedWidth", size_list[entry].width) &&
               ini_write_int(RESOLUTION_SECTION, "WindowedHeight", size_list[entry].height);
    }
    slot = slot_without_list(slot);

    switch ((window_slot_t)slot) {
    case WINDOW_MODE_ROW_BORDERLESS:
    case WINDOW_MODE_ROW_WINDOWED:
    case WINDOW_MODE_ROW_RESIZABLE:
    case WINDOW_MODE_ROW_BORDERLESS_SIZED: {
        int32_t wanted = SLOT_MODE[slot];

        /* Pressing the row that is already lit does NOTHING, which is what a set of choices does
         * everywhere else. Answering true rather than false because nothing failed: the answer is
         * already the one being asked for. */
        if (current_mode() == wanted) {
            return true;
        }
        previous_mode = current_mode();
        return write_mode(wanted);
    }

    /* The one row of the five that is a switch rather than a choice, because that is how it reads
     * and how it gets used. The engine's own shape covers most of a screen, so turning this on
     * looks like going fullscreen, and the next thing anyone does is press it again to come back.
     * As a plain radio option that did nothing, and the group looked like it had stopped working.
     *
     * So it remembers what it replaced and gives it back. The memory does not survive a restart,
     * and does not need to: coming back from it then lands on a window with a caption, which is a
     * window whatever else it is not. */
    case WINDOW_MODE_ROW_AUTHENTIC: {
        int32_t wanted;

        if (current_mode() == MODE_AUTHENTIC) {
            wanted = (previous_mode == MODE_AUTHENTIC) ? MODE_WINDOWED : previous_mode;
        } else {
            previous_mode = current_mode();
            wanted        = MODE_AUTHENTIC;
        }
        return write_mode(wanted);
    }

    /* Opens and closes the list. Stepping one size per press was what this did first, and it works
     * and is horrible: fifteen presses to cross the list, with the one you wanted going past. */
    case WINDOW_ROW_SIZE:
        build_size_list();
        if (size_list_count == 0u) {
            return false;               /* nothing to show, so opening an empty list helps nobody */
        }
        size_list_open = !size_list_open;
        return true;

    case WINDOW_ROW_FILL:
        return ini_write_int(RESOLUTION_SECTION, "WindowedFill",
                             ini_read_bool(RESOLUTION_SECTION, "WindowedFill", true) ? 0 : 1);

    default:
        return false;
    }
}

bool overlay_window_commit(uint32_t slot, const char *text)
{
    (void)slot;
    (void)text;
    return false;              /* nothing in this group is typed into; the size is a list */
}

bool overlay_window_bind(uint32_t slot, int32_t virtual_key)
{
    if (!overlay_window_row_is_key(slot) || virtual_key < 0 || virtual_key > 0xFF) {
        return false;
    }
    if (slot == (uint32_t)WINDOW_ROW_FULLSCREEN_KEY) {
        return ini_write_int(RESOLUTION_SECTION, "FullscreenToggleKey", virtual_key);
    }
    return ini_write_int(RESOLUTION_SECTION, "PointerReleaseKey", virtual_key);
}
