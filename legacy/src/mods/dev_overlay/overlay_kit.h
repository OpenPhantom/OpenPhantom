/* overlay_kit.h: a group's rows as a table instead of as a switch.
 *
 * To add a row: write one entry in the group's table, in the place it is drawn, and give it the
 * two or three functions its type needs. A TOGGLE needs `get_on` and `set_on`, an ACTION needs
 * `run`, a NUMBER needs `number`, `set_number`, `format`, `parse`, its two ends and its two press
 * sizes, a NOTE needs `label_now`. A NUMBER that is to have a Default needs `standard` as well,
 * and one without it simply has no Default.
 *
 * Three other places move with it, and the build only stops you at the second:
 *   - the group's row count in its header, which the id budgets are cut from;
 *   - the group's slot enum, held against that count by a _Static_assert, so an entry added
 *     without a name in the enum does not build. The entry above said "nothing else has to be
 *     touched" and left this out, which is the one of the three a reader finds out about by
 *     having the compiler stop;
 *   - the slot numbers session_lock.c addresses, if the row goes anywhere but the end. Nothing in
 *     the build notices one that moved. unittests/overlay_session.c does, by name.
 *
 * A NUMBER produces TWO rows, the value and its track underneath, the way the groups already draw
 * one; overlay_kit_count() counts that, so an entry inserted above another moves that one's slot
 * by two and not by one.
 *
 * The limits sit on the entry rather than in the drawing so that the sideways keys and Default
 * are written once, in overlay_number.c, and every converted row gets them.
 * overlay_kit_limits() is what reads them, and unittests/overlay_kit.c holds them against the row
 * they belong to. A wrong `coarse` in a GROUP's table is still nothing the build sees, because a
 * group's table is static and no caller outside that file can reach it.
 */
#ifndef DEV_OVERLAY_OVERLAY_KIT_H
#define DEV_OVERLAY_OVERLAY_KIT_H

#include "overlay_model.h"
#include "overlay_number.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum overlay_kit_type {
    OVERLAY_KIT_TOGGLE = 0,   /* a switch, drawn as OVERLAY_ROW_CHEAT     */
    OVERLAY_KIT_ACTION,       /* runs once                                */
    OVERLAY_KIT_NUMBER,       /* a number, and a track of its own beneath */
    OVERLAY_KIT_NOTE          /* a line of text, drawn as OVERLAY_ROW_INFO */
} overlay_kit_type_t;

typedef struct overlay_kit_entry {
    overlay_kit_type_t type;

    /* The name. `label` when it never changes, `label_now` when the row writes its own, which is
     * every row whose name carries a number the panel reads out of the game. Exactly one of the
     * two is set. */
    const char *label;
    void      (*label_now)(char *out, size_t size);

    bool      (*get_on)(void);          /* TOGGLE */
    bool      (*set_on)(bool on);       /* TOGGLE, false when the write did not land */
    bool      (*run)(void);             /* ACTION */

    /* NUMBER. `number` answers false when there is none to show, which greys the value and its
     * track: the field of view needs a width only variable_fov can publish, and inventing one
     * would be wrong on some canvas. `ends` is for a row whose ends come out of a file rather
     * than out of `minimum` and `maximum`. */
    bool      (*number)(float *out);
    bool      (*set_number)(float value);
    void      (*format)(float value, char *out, size_t size);
    bool      (*parse)(const char *text, float *out);
    void      (*ends)(float *low, float *high);

    /* `step` is the smallest change the row recognises, so it is both what a drag rounds to and
     * what one sideways press will move. Rounding matters: a drag that wrote more decimals than
     * the text beside it shows makes the two disagree about what was set. */
    float minimum;
    float maximum;
    float step;
    float coarse;     /* a held modifier's larger step */

    /* What Default puts back, as a function rather than as a number. A number could not say
     * "this row has none", because a table entry that left it out would read as zero and zero is
     * a value on some of these tracks; NULL says it and cannot be written by accident. It also
     * lets the one row whose standard is not a constant answer for itself: the field of view's
     * is its width with no offset at all, which is whatever base variable_fov has published for
     * this canvas, and FOV_ROW_MIN_DEFAULT is the low end of its track and not that. Wiring the
     * two together sets the picture to the narrowest the row allows and calls it the default. */
    bool      (*standard)(float *out);

    bool  full_rate;  /* a drag on this track reaches the file at the full rate the panel allows */

    /* Whether the row can be used at all, and why not. A row that can never be missing leaves
     * both NULL.
     *
     * They are two hooks and not one because the reason alone cannot say it: the panel's oldest
     * unavailable state is a site that never resolved, and its code is OVERLAY_REASON_NONE, the
     * same one that means there is nothing wrong. So `offered` answers whether, `reason` answers
     * why, and `reason` is read only when `offered` said no. That is the shape overlay_row_t
     * already has. */
    bool      (*offered)(void);
    uint32_t  (*reason)(void);

    /* For a row whose setting the host of a running session decides: writes the host's value as
     * the chip reads it, "host 1.50x" or "host ON", and answers true, while this machine is a
     * client of a session whose host named that setting. False in every other case, and the row
     * then reads as it always did. The kit puts the words in `value` and sets `host_value`; the
     * track under a number keeps its own and is hidden while the session holds it. NULL for a row
     * the host does not decide, which is every row but four.
     *
     * A switch also writes the host's state into `on`, and the kit puts it in the row's `on`, so
     * a folded heading counts what the session plays with and not this machine's own key; its
     * chip said "2 on" over a row reading "host OFF". A number leaves `on` alone. */
    bool      (*host_word)(char *out, size_t size, bool *on);
} overlay_kit_entry_t;

/* How many rows the table draws: one per entry and two per NUMBER. */
uint32_t overlay_kit_count(const overlay_kit_entry_t *rows, uint32_t entries);

/* Whether the row at `slot` can be used at all: it is offered, and if it is a number it has one
 * to show. It is ONE answer because there were two, and they disagreed about how much they shut:
 * `offered` stopped a press and a drag, a number that could not be read stopped neither, and a
 * typed commit asked neither. Nothing reached the gap, because the model tested `row.available`
 * first, but `row.available` is what this says, so the next caller that goes straight to the kit
 * would have found the field of view's track open. Every acting path here reads this, and so
 * should any that is added.
 *
 * False for a slot past the end of the table. */
bool overlay_kit_usable(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot);

/* The numbers behind a number row, the row's own ends included: `minimum` and `maximum` as the
 * table wrote them, or what `ends` says for a row whose ends come out of a settings file, and the
 * standard as `standard` answered it. False for a slot that is not a number row.
 *
 * It is the one reader of the entry's limits: the kit's own drag, its typed commit, the sideways
 * keys and Default all go through it, so this is where a test can hold `coarse` and the standard
 * against the row they belong to. */
bool overlay_kit_limits(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot,
                        overlay_number_t *out);

/* Fills everything about one drawn slot except `group` and `id`, which belong to the caller's
 * numbering. `editing_text` is what has been typed so far when this row is the one being typed
 * into, and NULL otherwise. A slot past the end answers as an empty unavailable row rather than
 * as whatever the caller's buffer held. */
void overlay_kit_fill(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot,
                      const char *editing_text, overlay_row_t *out);

/* Flips a switch or runs an action. False for a slot that is neither, for a row
 * overlay_kit_usable() turns down, and for a write that did not land, and in every one of those
 * the caller leaves the row where it was. */
bool overlay_kit_activate(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot);

/* Commits typed text to a number. False when the slot is no number row, when
 * overlay_kit_usable() turns the row down, or when the text is not a number, which leaves the
 * setting alone rather than writing a zero. */
bool overlay_kit_commit(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot,
                        const char *text);

/* Drags the track at `slot` to `fraction`, 0 to 1, rounded onto the row's own step. False for a
 * slot with no track, a row overlay_kit_usable() turns down, or a write that failed. */
bool overlay_kit_slider(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot,
                        float fraction);

/* What the row above the track at `slot` would read with the handle at `fraction`: the same
 * arithmetic and the same rounding overlay_kit_slider() writes with, formatted by the row's own
 * formatter. False for a slot with no track, a row that has nothing to show, or a track whose two
 * ends are the same.
 *
 * It is here rather than in the drawing because the drawing would then hold a second copy of a
 * sum the row already knows: while a track is held the handle comes from the hand and the value
 * beside it comes from the file, four times a second apart, and the two would be two spellings of
 * one drag. */
bool overlay_kit_value_at(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot,
                          float fraction, char *out, size_t size);

/* Whether a drag on that track should reach the file at the full rate. */
bool overlay_kit_full_rate(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot);

#endif /* DEV_OVERLAY_OVERLAY_KIT_H */
