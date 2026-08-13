/* input_menu.c: the controls screen's added widgets: two check boxes, one slider, the captions
 * for all of them, and the label trap.
 *
 * ==============================================================================================
 * There is no gameplay screen
 *
 * A census of all 22 swmenu_build call sites in the image finds exactly four options screens:
 * video, performance, audio, controls. Every setting offered here is part of the control scheme, so
 * they belong on controls, and its widget array is reached the way every screen's is, as a single
 * `push imm32` handed to swmenu_build, which the shared menu patcher repoints at a copy with our
 * entries appended. No engine code is rewritten.
 *
 * The eight authored widgets, read out of the retail data:
 *
 *     [0] PIC 50 backdrop      [1] PIC 0 panel        [2] TEXT 1 title  382,19,235,67
 *     [3] TEXT 2   7,175,199,53   [4] TEXT 3   7,277,199,53   [5] TEXT 4 BACK 484,400,106,67
 *     [6] PIC 5  hidden          [7] TEXT 6 hidden, both 170,140,300,200
 *
 * The authored ids are 0, 1, 2, 3, 4, 5, 6 and 50, so every id this file adds is free, and the
 * patcher is asked again at run time rather than trusting that list.
 *
 * The authored widgets occupy the LEFT column down to y=330 and the BACK button's row from y=400 at
 * x 484..590. Everything added here lives in the empty right-hand region above that row, which was
 * MEASURED from the background artwork rather than inferred from the rectangles, because the two do
 * not agree. The layout and its compile-time checks are further down.
 *
 * ==============================================================================================
 * Why there is no switch for mouse look itself, and why that is the precondition
 *
 * Both settings offered here need this DLL's two phase thunks, and those are only swapped in when
 * MouseLook=1. With MouseLook=0 install returns before this file is ever called, so the controls
 * screen is left exactly as it shipped, no boxes at all, rather than boxes that could not do
 * anything. That is also the whole handling of "free look requires mouse look": the box for it
 * cannot exist on a screen that is not patched.
 *
 * Mouse look itself gets no box for the same reason in reverse: turning it OFF would have to
 * unswap two phase pointers, and the detour machinery here deliberately cannot be uninstalled.
 *
 * ==============================================================================================
 * And why the third widget is a slider, which is only possible because of a shared table
 *
 * Mouse sensitivity is not a switch, so it is not a check box. A slider needs two bitmaps, a
 * track and a knob, and a widget names them as INDICES into the screen's own bitmap-name table,
 * not by file name. A screen whose table does not carry them cannot show a slider at all, however
 * correct the widget record is.
 *
 * EVERY options screen in this game is built with the SAME table, [0x4AEE10], controls, video and
 * audio alike. Read out of the retail image it holds eighteen entries, in order:
 *
 *     0 quit      1 bindings  2 slgauge   3 slslide   4 chkbxoff  5 chkbxon
 *     6 perform   7 controls  8 dialog    9 volume   10 sopts    11 splashol
 *    12 saveup1  13 saveup2  14 savedwn1 15 savedwn2 16 rusure   17 popup
 *
 * So the gamma slider's own track and knob are already loaded on this screen, and 2 and 3 mean here
 * exactly what they mean over there. That was checked before the widget was written, because the
 * failure mode is a control that installs, logs success and draws nothing.
 *
 * The same shared table once looked like a way to borrow the AUDIO screen's background (index 9)
 * as a plate for this group. It is not, and the layout comment below records why in full: a widget
 * rectangle cannot crop or scale a bitmap, so borrowing a full-screen background draws the WHOLE of
 * it.
 *
 * ==============================================================================================
 * Why a check box and not a selectable label
 *
 * swchkbox_activate toggles its own state, beeps, and ends in `or eax,-1`; it always returns -1.
 * Every screen loop runs `while (result < 0)`, so a check box is the one widget class that CANNOT
 * close a screen no matter what it is wired to. A selectable text widget returns its own id
 * instead, which would end the screen the first time it was clicked. That is the whole reason for
 * the choice, and it is also why nothing here has to touch the screen's hand-written navigation
 * switch.
 *
 * The state lives in OUR array, the engine writes the toggle straight into our memory, so
 * reading the setting back needs no engine call and cannot fail.
 *
 * ==============================================================================================
 * The label trap, and the one extra hook it costs
 *
 * A check box names its label by string id, and swmenu_getString indexes the localised table with
 * NO bounds check: it clears the destination, then reads a pointer at [table + id*8 + 4] and
 * copies from it if it is non-NULL. The authored table defines ids 0..423, so an invented id is an
 * unchecked read of whatever happens to lie past it, not an empty label.
 *
 * So both ids are answered before they ever reach the table: the getter is hooked, each of our
 * reserved ids is served from its own buffer, everything else is forwarded untouched. And no check
 * box is appended until that hook stands. Without it no widget is added at all, because a label
 * that reads past a table is not an acceptable fallback for a convenience. The getter answers a
 * reserved id whether or not its box was appended, so an id we have claimed can never reach the
 * table by any route.
 *
 * ==============================================================================================
 * One DLL per screen, and therefore one appender for every widget
 *
 * menu_patcher_begin requires the source table to lie inside the HOST image. Once a DLL has
 * repointed the push operand at its own buffer, that address is inside that DLL's module, so a
 * second DLL attempting the same screen is refused and adds nothing. This DLL owns the controls
 * screen; the field-of-view slider owns the video screen.
 *
 * That is why every further widget is another entry in this one patch rather than a patch of its
 * own: the copy is taken once, at loader time, before any swmenu_build has run, and everything this
 * DLL offers is appended to that same copy. A widget whose append fails is simply absent, the
 * others are unaffected, because each one's value is read back through its own recorded index.
 *
 * SIZE NOTE (rule 9): over 800 lines. It appends a whole widget group to a screen the engine
 * authored, which means the widget records, the string ids, the check-box plumbing and the install
 * order all live here. A reader who cannot see why the string getter is hooked
 * BEFORE a widget exists cannot review this file at all, and that fact lives nowhere in the code.
 *
 * The slider moved to input_slider.c when this file reached 920 lines, past the hard limit, along
 * the seam this note had already named. What stayed here is the LAYOUT of the whole group, boxes,
 * slider, caption, because a group needs one owner and one place where its rectangles can be
 * checked against the region they have to fit in.
 *
 * 16 lines under the hard limit. The next thing to leave, if this file has to grow again, is the
 * five-language caption table and resolve_captions(), about 60 lines that depend on nothing here
 * except the two slots they are written into.
 */
#include "input_menu.h"

#include "enhanced_input.h"
#include "input_slider.h"
#include "mouse_look.h"

#include "common/detour.h"
#include "common/engine_types.h"
#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/menu_patcher.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 0x00442A98, options_controls: the controls screen, and the push in its head that names its
 * widget table. The opcode is checked before the operand is believed.
 * The screen is built by one call at the end of that head, which takes the widget table, the
 * bitmap names and the screen record. */
static const uint8_t SIG_OPTIONS_CONTROLS[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C,
    0xC7, 0x45, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF,
    0xC7, 0x45, 0xF4, 0x00, 0x00, 0x00, 0x00,
    0x6A, 0x00,
    0x68, 0x00, 0x00, 0x00, 0x00,
    0x68, 0x00, 0x00, 0x00, 0x00,
    0x6A, 0x00,
    0x68, 0x00, 0x00, 0x00, 0x00,
    0x68, 0x00, 0x00, 0x00, 0x00,
    0xE8
};
static const uint8_t MSK_OPTIONS_CONTROLS[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF
};
#define OPTIONS_CONTROLS_PROLOGUE      6u
#define OPTIONS_CONTROLS_PUSH_OFFSET  22u   /* the 0x68 opcode, verified before the operand */
#define OPTIONS_CONTROLS_TABLE_OFFSET 23u
/* The third `push imm32` of the same prologue: the screen's bitmap-name table, which is what
 * bounds every bitmap index this file appends. */
#define OPTIONS_CONTROLS_BMP_PUSH_OFFSET  34u
#define OPTIONS_CONTROLS_BMP_TABLE_OFFSET 35u
#define OPCODE_PUSH_IMM32           0x68u

/* 0x0045EB7B, swmenu_getString(stringId, destination). It clears the destination first and then
 * indexes its table with NO BOUNDS CHECK, which is why every id handed to it has to be one this
 * module owns. */
static const uint8_t SIG_GET_STRING[] = {
    0x55, 0x8B, 0xEC, 0x56, 0x57,
    0x8B, 0x45, 0x0C,
    0xC6, 0x00, 0x00,
    0x8B, 0x4D, 0x08,
    0x8B, 0x15, 0x00, 0x00, 0x00, 0x00,
    0x83, 0x7C, 0xCA, 0x04, 0x00
};
static const uint8_t MSK_GET_STRING[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#define GET_STRING_PROLOGUE 5u

/* A mask shorter than its pattern is read past its end, and the shortfall is invisible in the
 * source; both are long columns of hex. Every pair in this file is checked here instead. */
_Static_assert(sizeof(SIG_OPTIONS_CONTROLS) == sizeof(MSK_OPTIONS_CONTROLS),
               "the controls-screen pattern and its mask are different lengths");
_Static_assert(sizeof(SIG_GET_STRING) == sizeof(MSK_GET_STRING),
               "the string-getter pattern and its mask are different lengths");

enum {
    SITE_OPTIONS_CONTROLS,
    SITE_GET_STRING,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_MASKED("options_controls", SIG_OPTIONS_CONTROLS, MSK_OPTIONS_CONTROLS),
    SIGNATURE_ENTRY_MASKED("swmenu_get_string", SIG_GET_STRING,      MSK_GET_STRING)
};

/* Ids the shipped controls screen does not use. Authored: 0,1,2,3,4,5,6,50. */
#define WIDGET_ID_STRAFE       0x70
#define WIDGET_ID_FREE_LOOK    0x71
#define WIDGET_ID_MOUSE_SLIDER 0x72
#define WIDGET_ID_MOUSE_LABEL  0x73

/* Ids the shipped screen MUST have, or this is not the controls screen we think it is. */
#define WIDGET_ID_CONFIGURE 2
#define WIDGET_ID_JOYSTICK  3
#define WIDGET_ID_BACK      4

/* String ids far outside anything the game ships. They never reach the localised table, because
 * the getter is hooked, but they are chosen out of range anyway so that a failure to hook shows up
 * as a blank label rather than as a plausible wrong one.
 *
 * Two different numbers are involved and only one of them is the bound. 414 is the highest string
 * id any authored WIDGET references, over a census of all 370 widgets in all 20 screen tables,
 * that is a fact about the widgets, not about the table, and it is NOT the safe limit. The table
 * itself is loaded from xswinfo.txt, which declares 425 STRING lines covering ids 0..423 with no
 * gaps and one duplicate (415, "Jedi Knight" then "Cheater"), i.e. 424 distinct slots. Because
 * swmenu_getString indexes POSITIONALLY; it reads a pointer at [table + id*8 + 4] with no bounds
 * check at all, anything above 423 is already past the authored data.
 *
 * 0x7655 is 30293, so both ids here are far outside either number and stay as they are. */
#define STRING_ID_STRAFE    0x7655
#define STRING_ID_FREE_LOOK 0x7656

/* Index into the screen's own bitmap-name table: 4 = chkbxoff.bmp (34x34), 5 = chkbxon.bmp. The
 * frame drawn is `parameter + state`, so the two have to be adjacent, and they are.
 *
 * SEVENTEEN of the eighteen authored check boxes in the image carry 4, a census over all twenty
 * distinct widget tables reached from the twenty-two swmenu_build call sites. The eighteenth, at
 * table [0x4B7108] index 2 (widget id 55), carries 8. This file used to claim all eighteen did,
 * which mattered only because it was offered as evidence that 4 is universal; it is the convention,
 * not a rule, and the pair is verified against this screen's own table at run time regardless. */
#define BITMAP_CHECKBOX_UNCHECKED 4
/* The same table carries the slider's two frames, which is what makes a slider on THIS screen
 * possible at all: 2 = slgauge.bmp (the track), 3 = slslide.bmp (the knob). The controls screen and
 * the video screen share one bitmap-name table; both are built with [0x4AEE10], so the gamma
 * slider's own artwork is already loaded here and costs no new resource. */
#define BITMAP_GAUGE 2
#define BITMAP_KNOB  3
/* There is deliberately no BITMAP_PLATE. Two of them were tried, 9 (volume.bmp) and 17
 * (popup.bmp), and the layout comment below records why neither can work. A plate index is not
 * merely unused here; adding one back without reading that comment first would repeat the defect. */
/* Font table: 0 indust, 1 sysfont, 2 courier. All eighteen authored check boxes use 2; the
 * authored body text on this screen uses 1, and the slider's caption is body text. */
#define FONT_CHECKBOX 2
#define FONT_CAPTION  1

/* ---- The layout, and the fact that ended three attempts at it -------------------------------- *
 *
 * There is no plate, and that is the correction. Three versions of this group were built on the
 * belief that a widget's rectangle crops or scales its bitmap. it does not and cannot: the blit
 * every widget in this toolkit ends in takes canvas, bitmap, x and y, no width, no height, and
 * clips only against a hard-coded 640x480.
 *
 * What that meant for the version this replaces is worth writing down, because it was invisible in
 * the source and obvious the moment the artwork was rendered. The plate was `volume.bmp`, the AUDIO
 * screen's full-screen background, asked for as a 286x232 window at (350,4) onto the panel that
 * screen carries on its RIGHT. Since the rectangle does nothing, what was actually drawn was the
 * whole 640x480 bitmap starting at x=350, i.e. its LEFT 290 columns, which are the audio screen's
 * own furniture, over this screen's title and buttons, with the panel it was reaching for clipped
 * off the right edge of the canvas entirely. Both comments in this file that reasoned about
 * cropping were false, and they are gone.
 *
 * `popup.bmp` was tried instead. It is 300x200 and does fit the empty region, but its usable dark
 * recess is only 267x91, so the second check box lands on a light metal bar and its white caption
 * becomes unreadable. That was settled by rendering it over the real extracted background and
 * looking at it, not by arithmetic on rectangles.
 *
 * So the group is drawn with no plate at all, and that is acceptable here for a reason specific to
 * this screen: what lies behind the empty region is NOT the live 3-D scene. `controls.bmp` is
 * 73.3 % transparent, opaque only over columns 1..280 and 382..616, and what shows through is
 * `splashol.bmp`, the brown machinery backdrop. It is static, dark and low-contrast, and white
 * captions read cleanly on it.
 *
 * ---- where the space is -----------------------------------------------------------------------
 *
 * Authored rectangles:
 *     title    382, 19,235, 67   buttons  7,175,199,53 and 7,277,199,53
 *     BACK     484,400,106, 67   hidden 170,140,300,200  (PIC 5 and TEXT 6, both visible = 0)
 *
 * and `controls.bmp` is opaque over rows 19..85, 116..374 and 399..465 within its two column bands.
 * The one genuinely empty region, verified transparent over the whole rectangle, is
 * x 281..639, y 86..398, which is 359x313.
 *
 * The hidden pair is the one thing this group overlaps, and it is named rather than designed
 * around: it is the screen's own message plate, it ships invisible, and the two buttons here open
 * sub-screens instead of raising a box. If some build does raise it, it draws over these widgets
 * for as long as it is up and nothing is damaged.
 *
 * ---- what each widget really occupies, which is not what its rectangle says --------------------
 *
 * This difference is the whole reason the previous compile-time checks were worthless:
 *
 *   SLIDER    exactly slgauge.bmp, 250x50, from its own (x,y). Rewritten from that bitmap on every
 *             screen open, so the width passed below is a declaration of intent and nothing more.
 *   CHECKBOX  a 34x34 box at (x,y), chkbxoff.bmp, plus a caption at x + 34 + 4 that is ALWAYS
 *             200 wide. So a box's right edge is x + 238, and ONLY the 34x34 square is clickable:
 *             the words beside it are not a hit target however wide the rectangle claims.
 *   TEXT      the rectangle is used as given.                                                    */
#define FREE_REGION_X       281
#define FREE_REGION_Y        86
#define FREE_REGION_RIGHT   639
#define FREE_REGION_BOTTOM  398

#define GROUP_X             330
#define SLIDER_X            330
#define SLIDER_Y             96
#define SLIDER_LABEL_Y      148
#define SLIDER_LABEL_HEIGHT  40
#define CHECKBOX_Y_STRAFE   192
#define CHECKBOX_Y_FREE_LOOK 246

/* Declared intent. The engine rewrites a slider's pair from slgauge.bmp and a check box's from
 * chkbxoff.bmp; of a check box's four rect fields only HEIGHT is read, and only by its caption. */
#define CHECKBOX_WIDTH      255
#define CHECKBOX_HEIGHT      50
#define SLIDER_WIDTH        255
#define SLIDER_HEIGHT        50

/* THE DRAWN FOOTPRINTS, which is what the assertions below are allowed to reason about. */
#define GAUGE_WIDTH         250   /* slgauge.bmp  */
#define GAUGE_HEIGHT         50
#define CHECKBOX_BOX_SIZE    34   /* chkbxoff.bmp */
#define CHECKBOX_LABEL_GAP    4
#define CHECKBOX_LABEL_WIDTH 200  /* forced by the toolkit, whatever the rectangle says */
#define CHECKBOX_DRAWN_WIDTH (CHECKBOX_BOX_SIZE + CHECKBOX_LABEL_GAP + CHECKBOX_LABEL_WIDTH)

/* These assert the real footprint. The previous set checked CHECKBOX_WIDTH (255) and SLIDER_WIDTH
 * (255) against a plate rectangle; two numbers the engine discards, against a plate that was
 * never drawn where it was asked for. An assertion that is neither the constraint nor an
 * approximation of it is worse than none, because it reads as a guarantee. */
_Static_assert(SLIDER_X >= FREE_REGION_X && SLIDER_X + GAUGE_WIDTH - 1 <= FREE_REGION_RIGHT,
               "the mouse-speed slider leaves the empty region horizontally");
_Static_assert(GROUP_X >= FREE_REGION_X &&
               GROUP_X + CHECKBOX_DRAWN_WIDTH - 1 <= FREE_REGION_RIGHT,
               "a check box and its 200-wide caption leave the empty region horizontally");
_Static_assert(SLIDER_Y >= FREE_REGION_Y &&
               CHECKBOX_Y_FREE_LOOK + CHECKBOX_BOX_SIZE - 1 <= FREE_REGION_BOTTOM,
               "the controls group leaves the empty region vertically");
/* The rows must not overlap: the slider's own 50 pixels, its caption, then two boxes whose
 * captions are as tall as the rectangle they are given. */
_Static_assert(SLIDER_Y + GAUGE_HEIGHT <= SLIDER_LABEL_Y &&
               SLIDER_LABEL_Y + SLIDER_LABEL_HEIGHT <= CHECKBOX_Y_STRAFE &&
               CHECKBOX_Y_STRAFE + CHECKBOX_HEIGHT <= CHECKBOX_Y_FREE_LOOK,
               "the controls group's rows overlap each other");
/* The authored BACK button is the one widget the group could still reach. */
_Static_assert(CHECKBOX_Y_FREE_LOOK + CHECKBOX_HEIGHT <= 400,
               "the controls group reaches BACK's row");

#define WIDGET_CAPACITY 32

/* The caption buffer the getter answers from. swmenu_getString's callers pass fixed buffers of the
 * toolkit's own size; every authored menu string is far shorter than this. */
#define MAX_CAPTION 32

typedef int32_t (__cdecl *options_controls_fn_t)(void);
typedef void    (__cdecl *get_string_fn_t)(int32_t string_id, char *destination);

/* ==============================================================================================
 * The captions, in five languages
 *
 * ASCII only, and that is not laziness: the menu fonts are bitmap fonts and their coverage above
 * 0x7F has never been read out of the assets. SEITWAERTS rather than the umlaut, PAS CHASSE and
 * CAMERA LIBRE rather than the accents.
 *
 * Neither German caption may be the English word. "Strafe" is German for punishment, and it is the
 * one word in this feature that machine-translates into nonsense. FREIE KAMERA rather than FREIER
 * BLICK because the feature turns the camera; the gaze is a separate field this DLL never writes.
 * ============================================================================================ */
typedef enum menu_language {
    MENU_LANGUAGE_EN,
    MENU_LANGUAGE_DE,
    MENU_LANGUAGE_FR,
    MENU_LANGUAGE_IT,
    MENU_LANGUAGE_ES,
    MENU_LANGUAGE_COUNT
} menu_language_t;

static const char *const STRAFE_CAPTION[MENU_LANGUAGE_COUNT] = {
    "STRAFE",
    "SEITWAERTS LAUFEN",
    "PAS CHASSE",
    "PASSO LATERALE",
    "PASO LATERAL"
};

static const char *const FREE_LOOK_CAPTION[MENU_LANGUAGE_COUNT] = {
    "FREE LOOK",
    "FREIE KAMERA",
    "CAMERA LIBRE",
    "TELECAMERA LIBERA",
    "CAMARA LIBRE"
};

/* The mouse caption is a FORMAT, and its one conversion is the setting in thousandths of a degree
 * per count, 0.030 shows as 30. That is the same number the ini carries with the decimal point
 * moved, so a player who reads the caption and then opens engine_fixes.ini finds what they expect.
 * A raw 0.030 on screen would be three characters of noise at this font size. */
static const char *const MOUSE_SPEED_CAPTION[MENU_LANGUAGE_COUNT] = {
    "MOUSE SPEED %d",
    "MAUSGESCHWINDIGKEIT %d",
    "VITESSE SOURIS %d",
    "VELOCITA MOUSE %d",
    "VELOCIDAD RATON %d"
};

/* The primary language identifier of a LANGID, per the Windows LANGID layout. */
#define PRIMARY_LANGUAGE_MASK 0x3FFu
#define LANG_PRIMARY_GERMAN   0x07u
#define LANG_PRIMARY_FRENCH   0x0Cu
#define LANG_PRIMARY_ITALIAN  0x10u
#define LANG_PRIMARY_SPANISH  0x0Au

/* ==============================================================================================
 * One description per box
 *
 * The two boxes differ only in their id, their row, their wording and the pair of functions they
 * drive, so they are described rather than duplicated: one immutable table and one mutable slot
 * each. The alternative, a second copy of the seed, poll, apply and read-back path, is four
 * places where the two could drift apart, and the read-back is the one this DLL cannot afford to
 * get wrong.
 * ============================================================================================ */
typedef bool (*read_setting_fn_t)(void);
typedef void (*write_setting_fn_t)(bool enabled);

typedef struct checkbox_spec {
    int32_t            widget_id;
    int32_t            string_id;
    int32_t            y;
    const char *const *captions;      /* MENU_LANGUAGE_COUNT entries */
    read_setting_fn_t  read_setting;
    write_setting_fn_t write_setting;
} checkbox_spec_t;

enum {
    BOX_STRAFE,
    BOX_FREE_LOOK,
    BOX_COUNT
};

static const checkbox_spec_t CHECKBOX_SPEC[BOX_COUNT] = {
    { WIDGET_ID_STRAFE,    STRING_ID_STRAFE,    CHECKBOX_Y_STRAFE,    STRAFE_CAPTION,
      enhanced_input_strafe_enabled,   enhanced_input_set_strafe },
    { WIDGET_ID_FREE_LOOK, STRING_ID_FREE_LOOK, CHECKBOX_Y_FREE_LOOK, FREE_LOOK_CAPTION,
      enhanced_input_free_look_enabled, enhanced_input_set_free_look }
};

typedef struct checkbox_slot {
    bool    appended;        /* in our copy of the array, which the engine has not seen yet */
    bool    present;         /* appended AND committed, so its state really is read back    */
    size_t  widget_index;
    int32_t last_state;
    char    caption[MAX_CAPTION];
} checkbox_slot_t;

typedef struct input_menu_state {
    bool                 attempted;
    bool                 armed;         /* at least one box is on the screen and is read back */
    bool                 screen_open;
    bool                 live_preview;

    menu_patch_context_t patch;
    sw_widget_t          widgets[WIDGET_CAPACITY];
    checkbox_slot_t      boxes[BOX_COUNT];

    detour_t options_controls_detour;
    detour_t get_string_detour;
} input_menu_state_t;

static input_menu_state_t menu_state;

/* ============================================================================================ */
/* An approximation, and it is named as one: the game has no language selector to ask. Every
 * language was a separate release with its own LOCALIZE.LAB. */
static void resolve_captions(void)
{
    LANGID          ui_language = GetUserDefaultUILanguage();
    menu_language_t language    = MENU_LANGUAGE_EN;
    size_t          index;

    switch (ui_language & PRIMARY_LANGUAGE_MASK) {
    case LANG_PRIMARY_GERMAN:  language = MENU_LANGUAGE_DE; break;
    case LANG_PRIMARY_FRENCH:  language = MENU_LANGUAGE_FR; break;
    case LANG_PRIMARY_ITALIAN: language = MENU_LANGUAGE_IT; break;
    case LANG_PRIMARY_SPANISH: language = MENU_LANGUAGE_ES; break;
    default:                   language = MENU_LANGUAGE_EN; break;
    }

    for (index = 0; index < BOX_COUNT; ++index) {
        char  *caption = menu_state.boxes[index].caption;
        size_t size    = sizeof(menu_state.boxes[index].caption);

        _snprintf(caption, size, "%s", CHECKBOX_SPEC[index].captions[language]);
        caption[size - 1] = '\0';
    }

    input_slider_set_caption_format(MOUSE_SPEED_CAPTION[language]);
}

/* Answers a reserved id from our own buffer whether or not its box was appended: the ids are
 * claimed the moment this hook stands, and an id we have claimed must never reach the unchecked
 * table by any route. */
static void __cdecl hook_get_string(int32_t string_id, char *destination)
{
    get_string_fn_t original = (get_string_fn_t)menu_state.get_string_detour.original;
    size_t          index;

    if (destination != NULL) {
        for (index = 0; index < BOX_COUNT; ++index) {
            if (string_id == CHECKBOX_SPEC[index].string_id) {
                const char *caption = menu_state.boxes[index].caption;

                memcpy(destination, caption, strlen(caption) + 1u);
                return;
            }
        }
    }

    original(string_id, destination);
}

/* The single apply path for a check box, used by the live poll and by the read-back on close, so
 * the two can never disagree. The engine has been writing the toggle straight into our array, so
 * the value is simply there and no engine call is needed to fetch it. */
static void apply_checkbox_state(size_t index)
{
    const checkbox_spec_t *spec = &CHECKBOX_SPEC[index];
    checkbox_slot_t       *slot = &menu_state.boxes[index];
    int32_t                state;

    if (!slot->present) {
        return;
    }

    state = menu_state.widgets[slot->widget_index].state;
    if (state == slot->last_state) {
        return;
    }
    slot->last_state = state;
    spec->write_setting(state != 0);

    /* A setter may refuse, strafing needs the keyboard axis, free look needs a camera it
     * recognises, so the widget is put back in step with what actually happened rather than left
     * showing a lie. */
    state = spec->read_setting() ? 1 : 0;
    menu_state.widgets[slot->widget_index].state = state;
    slot->last_state = state;
}

static void apply_all_checkbox_states(void)
{
    size_t index;

    for (index = 0; index < BOX_COUNT; ++index) {
        apply_checkbox_state(index);
    }
}

static void seed_all_checkbox_states(void)
{
    size_t index;

    for (index = 0; index < BOX_COUNT; ++index) {
        checkbox_slot_t *slot = &menu_state.boxes[index];

        if (!slot->present) {
            continue;
        }
        slot->last_state = CHECKBOX_SPEC[index].read_setting() ? 1 : 0;
        menu_state.widgets[slot->widget_index].state = slot->last_state;
    }

    input_slider_seed();
}

/* Runs inside options_controls' own `while (result < 0) { sys_frame(); ... }` loop, because
 * render_frameEnd is part of sys_frame. */
static void on_frame(void)
{
    if (!menu_state.armed || !menu_state.screen_open) {
        return;
    }
    apply_all_checkbox_states();
    input_slider_apply(false);
}

static int32_t __cdecl hook_options_controls(void)
{
    options_controls_fn_t original =
        (options_controls_fn_t)menu_state.options_controls_detour.original;
    int32_t result;

    if (!menu_state.armed) {
        return original();
    }

    seed_all_checkbox_states();
    menu_state.screen_open = true;

    result = original();

    menu_state.screen_open = false;

    /* The fallback that must really exist: without the per-frame hook nothing polled the boxes
     * while they were being clicked, so the final values are read and applied here. With the hook
     * this is still correct, the screen may have closed on a frame that never polled. */
    apply_all_checkbox_states();
    input_slider_apply(false);

    if (input_slider_is_present() && input_slider_notch() != input_slider_seed_notch()) {
        log_info("controls closed, mouse sensitivity %.3f degrees per count (notch %d of %d)",
                 (double)mouse_look_degrees_per_count(), input_slider_notch(),
                 MOUSE_SLIDER_NOTCH_COUNT - 1);
    }
    return result;
}

/* ============================================================================================ */
static bool table_has_shipped_shape(const menu_patch_context_t *context)
{
    bool   has_configure = false;
    bool   has_joystick  = false;
    bool   has_back      = false;
    size_t index;

    for (index = 0; index < context->original_count; ++index) {
        const sw_widget_t *widget = &context->widgets[index];

        if (widget->type != SW_TYPE_TEXT) {
            continue;
        }
        if (widget->id == WIDGET_ID_CONFIGURE) { has_configure = true; }
        if (widget->id == WIDGET_ID_JOYSTICK)  { has_joystick  = true; }
        if (widget->id == WIDGET_ID_BACK && widget->action == SW_ACTION_CANCEL) {
            has_back = true;
        }
    }

    if (has_configure && has_joystick && has_back) {
        return true;
    }

    log_warning("the table at the controls screen does not have the shipped shape "
                "(configure %d, joystick %d, back %d), no check box is added",
                has_configure ? 1 : 0, has_joystick ? 1 : 0, has_back ? 1 : 0);
    return false;
}

static bool read_widget_table(uintptr_t site, uintptr_t *out_table, uintptr_t *out_pointer)
{
    uintptr_t push_opcode = site + OPTIONS_CONTROLS_PUSH_OFFSET;
    uint8_t   opcode      = 0;
    uint32_t  table       = 0;

    if (!memory_read_u8(push_opcode, &opcode) || opcode != OPCODE_PUSH_IMM32) {
        log_warning("expected `push imm32` (68) at %08X on the controls screen, found %02X - no "
                    "check box is added", (unsigned)push_opcode, opcode);
        return false;
    }
    if (!memory_read_u32(site + OPTIONS_CONTROLS_TABLE_OFFSET, &table) || table == 0) {
        return false;
    }

    *out_pointer = site + OPTIONS_CONTROLS_TABLE_OFFSET;
    *out_table   = (uintptr_t)table;
    return true;
}

/* The screen's own bitmap-name table, from the third `push imm32` of the same prologue. Every
 * bitmap index appended below is bounded by its length, because the engine will not do it: its
 * lookup rejects a negative index and checks nothing else, so one index past the end is an
 * unchecked read that ends in a file loader. */
static bool read_bitmap_name_table(uintptr_t site, uintptr_t *out_table)
{
    uintptr_t push_opcode = site + OPTIONS_CONTROLS_BMP_PUSH_OFFSET;
    uint8_t   opcode      = 0;
    uint32_t  table       = 0;

    if (!memory_read_u8(push_opcode, &opcode) || opcode != OPCODE_PUSH_IMM32) {
        log_warning("expected `push imm32` (68) for the bitmap-name table at %08X, found %02X - "
                    "no check box is added", (unsigned)push_opcode, opcode);
        return false;
    }
    if (!memory_read_u32(site + OPTIONS_CONTROLS_BMP_TABLE_OFFSET, &table) || table == 0) {
        return false;
    }

    *out_table = (uintptr_t)table;
    return true;
}

/* Appends one box and records where it landed. The id is asked of the live table rather than
 * checked against the authored list, so an id taken by a widget this build happens to have is
 * caught rather than assumed away. */
static bool append_checkbox(size_t index)
{
    const checkbox_spec_t *spec = &CHECKBOX_SPEC[index];
    checkbox_slot_t       *slot = &menu_state.boxes[index];

    if (menu_patcher_has_widget_id(&menu_state.patch, spec->widget_id)) {
        log_error("widget id %d is already taken on the controls screen - \"%s\" is not added",
                  spec->widget_id, slot->caption);
        return false;
    }

    slot->appended = menu_patcher_append_checkbox(&menu_state.patch, spec->widget_id,
                                                  spec->string_id, GROUP_X, spec->y,
                                                  CHECKBOX_WIDTH, CHECKBOX_HEIGHT, FONT_CHECKBOX,
                                                  BITMAP_CHECKBOX_UNCHECKED,
                                                  spec->read_setting() ? 1 : 0,
                                                  &slot->widget_index);
    return slot->appended;
}

static bool build_widgets(uintptr_t site)
{
    uintptr_t table    = 0;
    uintptr_t pointer  = 0;
    size_t    appended = 0;

    uintptr_t bitmap_names = 0;

    if (!read_widget_table(site, &table, &pointer) ||
        !read_bitmap_name_table(site, &bitmap_names)) {
        return false;
    }
    if (!menu_patcher_begin(&menu_state.patch, table, pointer, bitmap_names,
                            menu_state.widgets, WIDGET_CAPACITY)) {
        return false;
    }
    if (!table_has_shipped_shape(&menu_state.patch)) {
        return false;
    }

    /* No plate is appended. See the layout above: this toolkit cannot crop or scale a bitmap, so
     * the plate that used to be here drew the wrong 290 columns of the audio screen over this
     * screen's own furniture. What backs these widgets is splashol.bmp, showing through the 73 %
     * of controls.bmp that is transparent. */
    /* Sideways walking gets a box only when there is a key it could be driven from. The keyboard
     * axis reader is what supplies that key, and on a build where its signature missed, the switch
     * would refuse every time it was clicked. */
    if (enhanced_input_strafe_available()) {
        appended += append_checkbox(BOX_STRAFE) ? 1u : 0u;
    } else {
        log_warning("no sideways walking check box: the keyboard turn axis was not recognised in "
                    "this build, so there is no key the walk could be driven from. The mouse "
                    "sensitivity slider is not affected.");
    }

    /* Free look gets a box only when its camera machinery stands. It is installed whether the
     * feature is on or off, so this asks whether the follow camera in this build was recognised at
     * all, and a switch for a camera that cannot be turned would mislead rather than fail. */
    if (enhanced_input_free_look_available()) {
        appended += append_checkbox(BOX_FREE_LOOK) ? 1u : 0u;
    } else {
        log_warning("no free look check box: the follow camera in this build was not recognised, "
                    "so the box could never turn anything.");
    }

    {
        const input_slider_layout_t slider_layout = {
            SLIDER_X, SLIDER_Y, SLIDER_WIDTH, SLIDER_HEIGHT,
            SLIDER_LABEL_Y, SLIDER_LABEL_HEIGHT,
            WIDGET_ID_MOUSE_SLIDER, WIDGET_ID_MOUSE_LABEL,
            BITMAP_KNOB, BITMAP_GAUGE, FONT_CAPTION
        };

        appended += input_slider_append(&menu_state.patch, menu_state.widgets,
                                        &slider_layout) ? 1u : 0u;
    }

    return appended != 0;
}

void input_menu_install(void)
{
    size_t index;

    if (menu_state.attempted) {
        return;
    }
    menu_state.attempted = true;

    if (!enhanced_input_is_active()) {
        log_warning("the player phases are not hooked, so no check box is added, a switch for "
                    "something that cannot run would only mislead");
        return;
    }

    resolve_captions();
    (void)signature_resolve_table(sites, SITE_COUNT);

    /* The getter FIRST, and without it nothing else happens: a check box's label id would
     * otherwise reach an unchecked table read. */
    if (sites[SITE_GET_STRING].address == 0 ||
        !detour_install(&menu_state.get_string_detour, sites[SITE_GET_STRING].address,
                        (const void *)hook_get_string, GET_STRING_PROLOGUE)) {
        log_warning("the menu string getter could not be hooked, so no check box is added - "
                    "sideways walking and free look stay ini settings");
        return;
    }

    if (sites[SITE_OPTIONS_CONTROLS].address == 0) {
        log_warning("the controls screen did not resolve, so no check box is added, sideways "
                    "walking and free look stay ini settings");
        return;
    }
    if (!build_widgets(sites[SITE_OPTIONS_CONTROLS].address)) {
        return;
    }

    /* The screen detour BEFORE the commit. A box the screen never reads back is worse than no box
     * at all, so the failure has to leave the engine owning its own array, and at this point it
     * still does. */
    if (!detour_install(&menu_state.options_controls_detour,
                        sites[SITE_OPTIONS_CONTROLS].address,
                        (const void *)hook_options_controls, OPTIONS_CONTROLS_PROLOGUE)) {
        log_warning("the controls screen could not be hooked, so no check box is added, their "
                    "state would never have been read back. Sideways walking and free look stay "
                    "ini settings.");
        return;
    }
    if (!menu_patcher_commit(&menu_state.patch)) {
        log_error("the widget table could not be repointed, the screen detour is inert and no "
                  "check box is on the screen");
        return;
    }

    /* Only now is a box really on the screen, so only now may its state be read back. */
    for (index = 0; index < BOX_COUNT; ++index) {
        menu_state.boxes[index].present = menu_state.boxes[index].appended;
        if (!menu_state.boxes[index].present) {
            continue;
        }
        menu_state.armed = true;
        log_info("check box \"%s\" (widget %d, string %04X) added to the controls screen at "
                 "(%d,%d)", menu_state.boxes[index].caption, CHECKBOX_SPEC[index].widget_id,
                 CHECKBOX_SPEC[index].string_id, GROUP_X, CHECKBOX_SPEC[index].y);
    }

    input_slider_commit();
    if (input_slider_is_present()) {
        menu_state.armed = true;
        log_info("mouse sensitivity slider (widget %d, %d notches) added to the controls screen at "
                 "(%d,%d), caption \"%s\" at (%d,%d), the band is %.3f to %.3f degrees per count "
                 "and it is seeded at notch %d",
                 WIDGET_ID_MOUSE_SLIDER, MOUSE_SLIDER_NOTCH_COUNT, SLIDER_X, SLIDER_Y,
                 input_slider_caption(), SLIDER_X, SLIDER_LABEL_Y,
                 (double)input_slider_degrees_from_notch(0),
                 (double)input_slider_degrees_from_notch(MOUSE_SLIDER_NOTCH_COUNT - 1),
                 input_slider_seed_notch());
    }

    menu_state.live_preview = frame_hook_add(on_frame);
    if (!menu_state.live_preview) {
        log_warning("the per-frame hook is unavailable, so a check box takes effect when the "
                    "screen closes rather than on the click. It is still read, applied and "
                    "saved.");
    }
}
