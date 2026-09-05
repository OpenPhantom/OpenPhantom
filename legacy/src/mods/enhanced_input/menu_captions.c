/* menu_captions.c: the wording of this DLL's added widgets, in five languages.
 *
 * The seam taken here is the one input_menu.c had already named: the five-language table and the
 * language pick depend on nothing in that file except the two slots they are written into. What is
 * left on the other side is the widget group, its layout, its check-box plumbing and the install
 * order, none of which changes when a caption does.
 */
#include "menu_captions.h"

#include <windows.h>

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

/* An approximation, and it is named as one: the game has no language selector to ask. Every
 * language was a separate release with its own LOCALIZE.LAB. */
static menu_language_t resolve_language(void)
{
    LANGID          ui_language = GetUserDefaultUILanguage();
    menu_language_t language    = MENU_LANGUAGE_EN;

    switch (ui_language & PRIMARY_LANGUAGE_MASK) {
    case LANG_PRIMARY_GERMAN:  language = MENU_LANGUAGE_DE; break;
    case LANG_PRIMARY_FRENCH:  language = MENU_LANGUAGE_FR; break;
    case LANG_PRIMARY_ITALIAN: language = MENU_LANGUAGE_IT; break;
    case LANG_PRIMARY_SPANISH: language = MENU_LANGUAGE_ES; break;
    default:                   language = MENU_LANGUAGE_EN; break;
    }

    return language;
}

const char *menu_captions_strafe(void)
{
    return STRAFE_CAPTION[resolve_language()];
}

const char *menu_captions_free_look(void)
{
    return FREE_LOOK_CAPTION[resolve_language()];
}

const char *menu_captions_mouse_speed_format(void)
{
    return MOUSE_SPEED_CAPTION[resolve_language()];
}
