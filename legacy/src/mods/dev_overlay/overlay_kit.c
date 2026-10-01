/* overlay_kit.c: see overlay_kit.h. */
#include "overlay_kit.h"

#include "overlay_reason.h"
#include "overlay_row_fill.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Where a drawn slot lands in the table: which entry it belongs to, and whether it is the track
 * under a number rather than the number itself. */
typedef struct kit_at {
    const overlay_kit_entry_t *entry;
    bool                       track;
} kit_at_t;

static kit_at_t at(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot)
{
    kit_at_t found;
    uint32_t i;

    found.entry = NULL;
    found.track = false;
    if (rows == NULL) {
        return found;
    }
    for (i = 0; i < entries; ++i) {
        if (slot == 0u) {
            found.entry = &rows[i];
            return found;
        }
        if (rows[i].type == OVERLAY_KIT_NUMBER && slot == 1u) {
            found.entry = &rows[i];
            found.track = true;
            return found;
        }
        slot -= (rows[i].type == OVERLAY_KIT_NUMBER) ? 2u : 1u;
    }
    return found;
}

uint32_t overlay_kit_count(const overlay_kit_entry_t *rows, uint32_t entries)
{
    uint32_t count = 0;
    uint32_t i;

    if (rows == NULL) {
        return 0;
    }
    for (i = 0; i < entries; ++i) {
        count += (rows[i].type == OVERLAY_KIT_NUMBER) ? 2u : 1u;
    }
    return count;
}

static void name_it(const overlay_kit_entry_t *entry, overlay_row_t *out)
{
    if (entry->label_now != NULL) {
        entry->label_now(out->label, OVERLAY_LABEL_MAX);
        out->label[OVERLAY_LABEL_MAX - 1u] = '\0';
        return;
    }
    overlay_row_label(out->label, entry->label);
}

/* Rounded onto the row's own step, which is what its formatter shows. Without it a drag writes
 * more decimals than the text beside it displays and the two disagree about what was set. A step
 * has to contain both ends of the row that uses it: the fog thickness starts at 0.25, which is
 * not a multiple of a fiftieth, so on that grid dragging fully left rounded up to 0.26 and the
 * documented minimum could not be reached at all. */
static float on_the_step(const overlay_kit_entry_t *entry, float value)
{
    /* Multiplied and divided by the same number rather than divided by the step twice, which is
     * what the groups did by hand and what keeps a hundredth grid landing on the same float. */
    const float per = (entry->step > 0.0f) ? (1.0f / entry->step) : 0.0f;

    if (!(per > 0.0f)) {
        return value;
    }
    return (float)((int32_t)(value * per + 0.5f)) / per;
}

static bool row_is_offered(const overlay_kit_entry_t *entry)
{
    return entry->offered == NULL || entry->offered();
}

/* The one answer to "can this row be used at all", and what overlay_kit_fill() puts in
 * `available`. Exported rather than kept private and wrapped, so that every path in this
 * file and every caller outside it read the same function and not two spellings of it.
 * See overlay_kit.h for why it is one answer and not two. */
bool overlay_kit_usable(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot)
{
    const overlay_kit_entry_t *entry = at(rows, entries, slot).entry;
    float                      value;

    if (entry == NULL || !row_is_offered(entry)) {
        return false;
    }
    if (entry->type != OVERLAY_KIT_NUMBER) {
        return true;
    }
    return entry->number != NULL && entry->number(&value);
}

/* Every number on an entry, with the two ends resolved: the entry's own, or whatever the
 * file says when the row has an `ends`. The drag, the value under the handle, the sideways
 * keys and Default all go through it, so `coarse` and the standard are not a corner of the
 * entry that nothing ever looks at. */
bool overlay_kit_limits(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot,
                        overlay_number_t *out)
{
    const overlay_kit_entry_t *entry = at(rows, entries, slot).entry;

    if (entry == NULL || entry->type != OVERLAY_KIT_NUMBER || out == NULL) {
        return false;
    }
    out->minimum  = entry->minimum;
    out->maximum  = entry->maximum;
    out->step     = entry->step;
    out->coarse   = entry->coarse;
    out->standard = 0.0f;
    /* Asked rather than stored, because the one row whose standard is not a constant reads it
     * out of a settings file every time. A row with no `standard` at all answers false, which is
     * what leaves it with no Default instead of one that puts it at zero. */
    out->has_standard = entry->standard != NULL && entry->standard(&out->standard);
    if (entry->ends != NULL) {
        entry->ends(&out->minimum, &out->maximum);
    }
    return true;
}

/* Only what a readable number puts on the row. Whether the row is available at all was
 * decided by overlay_kit_usable() before this ran; deciding it twice here is how the two
 * answers came apart. */
static void fill_number(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot,
                        const overlay_kit_entry_t *entry, bool track,
                        const char *editing_text, overlay_row_t *out)
{
    overlay_number_t limits;
    float            value = 0.0f;

    if (entry->number == NULL || !entry->number(&value)) {
        overlay_row_label(out->value, "");
        return;
    }
    if (!track) {
        overlay_row_typed(out, editing_text, entry->format, value);
        return;
    }
    if (!overlay_kit_limits(rows, entries, slot, &limits)) {
        return;
    }
    /* Guarded rather than assumed: a row whose ends come out of a file has somebody who can set
     * them equal. */
    if (!overlay_number_fraction_of(&limits, value, &out->fraction)) {
        out->fraction = 0.0f;
    }
    overlay_row_clamp_fraction(out);
    /* The number stands at the end of the track as well as in the chip above it, so that the eye
     * reading a handle does not have to travel a row and the width of the panel to find out what
     * it is set to. It is the SAME value the row above formats, through the same formatter: the
     * two are one reading of one file, and while the track is held the drawing replaces both from
     * the one fraction the hand has. */
    if (entry->format != NULL) {
        entry->format(value, out->value, sizeof out->value);
        out->value[sizeof out->value - 1u] = '\0';
    }
}

/* The host's value in the chip, over whatever the row put there: a number formatted out of this
 * machine's ini, or nothing for a switch. Only the row itself, never the track under a number.
 * Written into a buffer of its own first, so a hook that answers false leaves the row's value as
 * the row wrote it. A switch takes the host's state as well, which is what the heading above it
 * counts. */
static void host_decides(const kit_at_t *here, overlay_row_t *out)
{
    char word[sizeof out->value];
    bool on = out->on;

    word[0] = '\0';
    if (here->track || here->entry->host_word == NULL ||
        !here->entry->host_word(word, sizeof word, &on)) {
        return;
    }
    word[sizeof word - 1u] = '\0';
    memcpy(out->value, word, sizeof out->value);
    out->host_value = true;
    if (here->entry->type == OVERLAY_KIT_TOGGLE) {
        out->on = on;
    }
}

void overlay_kit_fill(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot,
                      const char *editing_text, overlay_row_t *out)
{
    const kit_at_t here = at(rows, entries, slot);

    if (out == NULL) {
        return;
    }
    overlay_row_defaults(out);
    if (here.entry == NULL) {
        /* Past the end. Answered as an empty unavailable row rather than left as whatever the
         * caller's struct held: a caller asking for a slot that does not exist has a bug, and a
         * blank row makes that bug visible instead of showing stale text. */
        overlay_row_label(out->label, "");
        out->available = false;
        return;
    }

    if (!overlay_kit_usable(rows, entries, slot)) {
        out->available = false;
        if (here.entry->reason != NULL) {
            out->reason = here.entry->reason();
        }
    }

    switch (here.entry->type) {
    case OVERLAY_KIT_TOGGLE:
        out->kind = OVERLAY_ROW_CHEAT;
        name_it(here.entry, out);
        out->on = here.entry->get_on != NULL && here.entry->get_on();
        host_decides(&here, out);
        return;

    case OVERLAY_KIT_ACTION:
        out->kind = OVERLAY_ROW_ACTION;
        name_it(here.entry, out);
        return;

    case OVERLAY_KIT_NOTE:
        out->kind = OVERLAY_ROW_INFO;
        name_it(here.entry, out);
        return;

    case OVERLAY_KIT_NUMBER:
    default:
        out->kind = here.track ? OVERLAY_ROW_SLIDER : OVERLAY_ROW_VALUE;
        if (here.track) {
            overlay_row_label(out->label, "");
        } else {
            name_it(here.entry, out);
        }
        fill_number(rows, entries, slot, here.entry, here.track, editing_text, out);
        host_decides(&here, out);
        return;
    }
}

bool overlay_kit_activate(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot)
{
    const kit_at_t here = at(rows, entries, slot);

    if (here.entry == NULL || here.track || !overlay_kit_usable(rows, entries, slot)) {
        return false;
    }
    if (here.entry->type == OVERLAY_KIT_TOGGLE) {
        return here.entry->get_on != NULL && here.entry->set_on != NULL &&
               here.entry->set_on(!here.entry->get_on());
    }
    if (here.entry->type == OVERLAY_KIT_ACTION) {
        return here.entry->run != NULL && here.entry->run();
    }
    return false;   /* a number is typed into and a note is read; neither is pressed */
}

bool overlay_kit_commit(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot,
                        const char *text)
{
    const kit_at_t here = at(rows, entries, slot);
    float          parsed;

    if (here.entry == NULL || here.track || !overlay_kit_usable(rows, entries, slot) ||
        here.entry->type != OVERLAY_KIT_NUMBER ||
        text == NULL || text[0] == '\0' || here.entry->parse == NULL ||
        here.entry->set_number == NULL) {
        return false;
    }
    /* Refused rather than clamped when the text is not a number: each of these would turn a typing
     * mistake into an extreme. */
    return here.entry->parse(text, &parsed) && here.entry->set_number(parsed);
}

/* What a handle at `fraction` stands for: the inverse of the reading fill_number() takes, and the
 * one arithmetic both the write below and the number the row shows go through. Without it the
 * number beside a track has to be worked out a second time somewhere else, and two spellings of
 * one drag disagree the moment either of them is touched. */
static bool value_at(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot,
                     const overlay_kit_entry_t *entry, float fraction, float *out)
{
    overlay_number_t limits;
    float            value;

    if (!overlay_kit_limits(rows, entries, slot, &limits) ||
        !overlay_number_value_at(&limits, fraction, &value)) {
        return false;
    }
    *out = on_the_step(entry, value);
    return true;
}

bool overlay_kit_slider(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot,
                        float fraction)
{
    const kit_at_t here = at(rows, entries, slot);
    float          value;

    if (here.entry == NULL || !here.track || here.entry->set_number == NULL ||
        !overlay_kit_usable(rows, entries, slot) ||
        !value_at(rows, entries, slot, here.entry, fraction, &value)) {
        return false;
    }
    return here.entry->set_number(value);
}

bool overlay_kit_value_at(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot,
                          float fraction, char *out, size_t size)
{
    const kit_at_t here = at(rows, entries, slot);
    float          value;

    if (here.entry == NULL || !here.track || here.entry->format == NULL || out == NULL ||
        size == 0u || !overlay_kit_usable(rows, entries, slot) ||
        !value_at(rows, entries, slot, here.entry, fraction, &value)) {
        return false;
    }
    here.entry->format(value, out, size);
    out[size - 1u] = '\0';
    return true;
}

bool overlay_kit_full_rate(const overlay_kit_entry_t *rows, uint32_t entries, uint32_t slot)
{
    const kit_at_t here = at(rows, entries, slot);

    return here.entry != NULL && here.track && here.entry->full_rate;
}
