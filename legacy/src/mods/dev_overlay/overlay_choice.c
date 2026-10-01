/* overlay_choice.c: see overlay_choice.h. */
#include "overlay_choice.h"

#include "overlay_row_ids.h"
#include "overlay_spawn.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Which source a row belongs to, and its slot inside that source. The id a row carries is offset
 * by the group's own base (overlay_row_ids.h), which is what keeps two groups' ids apart, so the
 * base has to come off again before the group is asked anything. Written once here rather than at
 * each of the three places below. */
static bool slot_of(const overlay_row_t *row, uint32_t *slot)
{
    if (row == NULL || row->kind != OVERLAY_ROW_SEGMENT) {
        return false;
    }
    switch ((overlay_group_t)row->group) {
    case OVERLAY_GROUP_OPENPHANTOM_SPAWN:
        *slot = row->id - SPAWN_FIRST_ID;
        return true;
    default:
        return false;      /* the entity spawner's behaviour row is the only one today */
    }
}

uint32_t overlay_choice_segments(const overlay_row_t *row, const char **out, uint32_t max)
{
    uint32_t slot;

    if (out == NULL || max == 0u || !slot_of(row, &slot)) {
        return 0;
    }
    if (max > OVERLAY_CHOICE_SEGMENTS_MAX) {
        max = OVERLAY_CHOICE_SEGMENTS_MAX;
    }
    return overlay_spawn_segments(slot, out, max);
}

/* Appends what fits and terminates. The strip is built a piece at a time rather than formatted in
 * one go, because the caller's buffer is the only thing that decides how much of it there is. */
static size_t put(char *out, size_t size, size_t at, const char *text)
{
    size_t i;

    for (i = 0; text[i] != '\0' && at + 1u < size; ++i) {
        out[at++] = text[i];
    }
    out[at] = '\0';
    return at;
}

bool overlay_choice_strip(const overlay_row_t *row, char *out, size_t size)
{
    const char *words[OVERLAY_CHOICE_SEGMENTS_MAX];
    uint32_t    count;
    uint32_t    i;
    size_t      at = 0;

    if (out == NULL || size == 0u) {
        return false;
    }
    out[0] = '\0';
    count = overlay_choice_segments(row, words, OVERLAY_CHOICE_SEGMENTS_MAX);
    if (count == 0u) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        /* One space at each side of every word, which is the box's own padding written as text.
         * The boxes abut, so two spaces stand between one word and the next and the strip is
         * measured with the padding the drawing puts there rather than without it. */
        at = put(out, size, at, " ");
        at = put(out, size, at, words[i]);
        at = put(out, size, at, " ");
    }
    return true;
}

uint32_t overlay_choice_row_width(const overlay_row_t *row)
{
    /* Room for four words as long as a label can be, so a strip is never measured shorter here
     * than it is drawn: a truncation in this buffer would report a row as fitting that does
     * not. */
    char strip[OVERLAY_CHOICE_SEGMENTS_MAX * (OVERLAY_LABEL_MAX + 2u)];

    if (!overlay_choice_strip(row, strip, sizeof strip)) {
        return 0;
    }
    return (uint32_t)(strlen(row->label) + strlen(strip));
}

bool overlay_choice_is_chosen(const overlay_row_t *row, uint32_t index)
{
    const char *words[OVERLAY_CHOICE_SEGMENTS_MAX];

    return index < overlay_choice_segments(row, words, OVERLAY_CHOICE_SEGMENTS_MAX) &&
           row->chosen == index;
}

bool overlay_choice_is_strip(const overlay_row_t *row)
{
    return row != NULL && row->kind == OVERLAY_ROW_SEGMENT && row->available;
}

bool overlay_choice_chosen_name(const overlay_row_t *row, char *out, size_t size)
{
    const char *words[OVERLAY_CHOICE_SEGMENTS_MAX];
    const char *name = NULL;
    size_t      i;

    if (row == NULL || out == NULL || size < 4u) {
        return false;
    }
    /* The same reading of "this row is a strip of words" the paint, the panel's width and the
     * sideways keys take, and it has to be the same one: a strip that cannot be used says so on
     * a line of its own ("Pickups and the tripod ignore the behaviour"), and a band that read the
     * word anyway put `Stand` on the heading of a group whose behaviour row the panel had greyed
     * out three lines below. Asked here without `available`, it was one state with two answers. */
    if (overlay_choice_is_strip(row)) {
        if (row->chosen < overlay_choice_segments(row, words, OVERLAY_CHOICE_SEGMENTS_MAX)) {
            name = words[row->chosen];
        }
    } else if (row->kind == OVERLAY_ROW_CHOICE && row->on) {
        name = row->label;
    }
    if (name == NULL) {
        return false;
    }
    /* A list entry is indented on screen so that it reads as one of a set; a band word stands on
     * its own and carries none of that. */
    while (*name == ' ') {
        ++name;
    }
    for (i = 0; i + 1u < size && name[i] != '\0'; ++i) {
        out[i] = name[i];
    }
    out[i] = '\0';
    if (out[0] == '\0') {
        return false;
    }
    /* Cut the way the drawing cuts a label that will not fit, two dots and no more, so a band
     * word that was shortened says so in the panel's own spelling. */
    if (name[i] != '\0') {
        out[size - 3u] = '.';
        out[size - 2u] = '.';
        out[size - 1u] = '\0';
    }
    return true;
}

bool overlay_choice_pick(const overlay_row_t *row, uint32_t index)
{
    const char *words[OVERLAY_CHOICE_SEGMENTS_MAX];
    uint32_t    slot;

    if (!slot_of(row, &slot) || !row->available ||
        index >= overlay_choice_segments(row, words, OVERLAY_CHOICE_SEGMENTS_MAX)) {
        return false;
    }
    return overlay_spawn_choose(slot, index);
}

int32_t overlay_choice_step(const overlay_row_t *row, int32_t by)
{
    const char *words[OVERLAY_CHOICE_SEGMENTS_MAX];
    uint32_t    count = overlay_choice_segments(row, words, OVERLAY_CHOICE_SEGMENTS_MAX);
    int32_t     at;

    if (count == 0u) {
        return -1;
    }
    at = (int32_t)row->chosen + by;
    /* A step off either end answers nothing rather than wrapping round. Wrapping would mean
     * holding one arrow down cycles a setting forever; answering nothing lets the caller hand the
     * key on to whatever it means elsewhere in the panel, which is what a heading already does
     * with left and right once it cannot fold any further. */
    if (at < 0 || at >= (int32_t)count) {
        return -1;
    }
    return at;
}

uint32_t overlay_choice_edges(const float *word_widths, uint32_t count, float pad, float x1,
                              float *out, uint32_t max)
{
    float    total = 0.0f;
    uint32_t i;

    if (word_widths == NULL || out == NULL || count == 0u || count + 1u > max) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        total += word_widths[i] + pad * 2.0f;
    }
    out[0] = x1 - total;
    for (i = 0; i < count; ++i) {
        out[i + 1u] = out[i] + word_widths[i] + pad * 2.0f;
    }
    return count + 1u;
}

int32_t overlay_choice_hit(const float *edges, uint32_t count, float x)
{
    uint32_t i;

    if (edges == NULL || count == 0u) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        /* The left edge belongs to the box it opens and the right edge to the box after it, so
         * two boxes that abut never both answer for one pixel and no pixel between the ends is
         * answered by neither. The last box keeps its own right edge, which is the end of the
         * strip and belongs to nothing else. */
        if (x >= edges[i] && (x < edges[i + 1u] || (i + 1u == count && x <= edges[i + 1u]))) {
            return (int32_t)i;
        }
    }
    return -1;
}
