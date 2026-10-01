/* overlay_number.h: the arithmetic of a number row, and the room the end of its track needs.
 *
 * A number row is five numbers (two ends, the press size, the larger press size and what
 * Default puts back) and three questions asked of them: where a fraction of the track lands,
 * where a value sits along it, and which fraction one press away is. Those used to be four
 * copies, one per group, and the fifth was about to be written for the arrow keys. They are here
 * instead, and the groups hand their own five numbers in.
 *
 * Nothing here reads a setting, writes one, or knows what a row is. That is deliberate: the one
 * thing in this panel that can be wrong without anything looking wrong is a slider whose value
 * and whose handle are two different sums, and a file that can be driven from a test with five
 * floats is the only version of it a test can pin at absolute values rather than by agreeing
 * with itself.
 *
 * The second half is the geometry of a track row's right hand end, in the shape overlay_legend.h
 * already uses for the footer: the caller measures its own text and this answers what fits. It is
 * here rather than in the drawing because what gives way when the panel is narrow is a rule with
 * an answer, and a sequence of ifs in a painter is the version of it nobody can check.
 */
#ifndef DEV_OVERLAY_OVERLAY_NUMBER_H
#define DEV_OVERLAY_OVERLAY_NUMBER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct overlay_number {
    float minimum;
    float maximum;
    /* `step` is the smallest change the row recognises: what a drag rounds to and what one press
     * of a sideways key moves. `coarse` is that press with the modifier held. */
    float step;
    float coarse;
    /* What Default puts back. Read only when `has_standard`, because a row can have none and a
     * plain float cannot say so: a table entry that simply left it out would read as zero, and
     * zero is a value on some of these tracks. The one row whose standard is not a constant, the
     * field of view, answers it through a hook of its own; see overlay_kit.h. */
    float standard;
    bool  has_standard;
} overlay_number_t;

/* Whether the two ends are far enough apart to divide by. Everything below answers false for a
 * row that fails it rather than dividing by zero: the field of view's ends come out of a settings
 * file and somebody can set them equal. */
bool overlay_number_usable(const overlay_number_t *n);

/* Where `value` sits along the track, 0 at the minimum and 1 at the maximum, clamped to the
 * track. A value outside the ends is left honest on the row above; a fraction outside them would
 * draw the handle past the end of the track. */
bool overlay_number_fraction_of(const overlay_number_t *n, float value, float *out);

/* What a handle at `fraction` stands for, NOT rounded onto the row's step: the rounding belongs
 * to whoever writes the value, because a row whose ends come out of a file has a grid that has to
 * contain both of them and only that row knows it. */
bool overlay_number_value_at(const overlay_number_t *n, float fraction, float *out);

/* The fraction one press away from `fraction`: `by` is -1 or 1, and `coarse` is the modifier.
 * Clamped at both ends, so a press at an end leaves the value where it is rather than running
 * past it. False when the row has no press size, which is a row nothing can step.
 *
 * It answers in fractions and not in values because the fraction is what every hand on a track
 * already speaks: the pointer, the pad's triggers and now the keys all reach the settings file
 * through overlay_slider.c with one, and a second unit here would be a second arithmetic. */
bool overlay_number_stepped(const overlay_number_t *n, float fraction, int32_t by, bool coarse,
                            float *out);

/* Where Default puts the handle. False for a row that has no standard, and then no Default is
 * drawn and none can be pressed. */
bool overlay_number_standard(const overlay_number_t *n, float *out);

/* What fits on the right of a track row, given the room there is. The number and the Default are
 * given up in that order backwards: the Default goes first, because a track that cannot be
 * grabbed is worse than a button that is not there, and the number goes second, because a track
 * that has shrunk to nothing says nothing at all. */
typedef enum overlay_number_fit {
    OVERLAY_NUMBER_FIT_BOTH = 0,   /* the number and the Default */
    OVERLAY_NUMBER_FIT_VALUE,      /* the number alone           */
    OVERLAY_NUMBER_FIT_NONE        /* the track alone            */
} overlay_number_fit_t;

/* `room` is the whole width the track row has, `least` the shortest track still worth grabbing,
 * and the three widths are the caller's own measurements in the caller's own unit. `gap` is what
 * the painter leaves between two of the pieces; it is charged once before the number and once
 * before the Default. A `have_default` of false answers at most OVERLAY_NUMBER_FIT_VALUE. */
overlay_number_fit_t overlay_number_fit(float room, float least, float value_w, float default_w,
                                        float gap, bool have_default);

/* The same question answered as EDGES, and the reason it is a second function is the word
 * `value_w` above.
 *
 * The painter used to hand overlay_number_fit() the width of THIS ROW'S number and then put
 * the number where that width left it. Three track rows one under the other read 1.21, 0.05
 * and 240, three different widths, so the number began in three places, ended in three places,
 * and the track ended in three places with it. The eye reads a column of numbers by their
 * right hand edge, and there was no column.
 *
 * `number_w` here is the width of the COLUMN, measured once from a sample of the widest number
 * a row can show, and it is the same for every row on the tab. The number is then drawn right
 * aligned against `number_x1`, so 1.21, 0.05 and 240 end on one line and the point of the two
 * that have one falls on another. `track_x1` and `default_x0` stop moving for the same reason:
 * neither is measured off a number any more.
 *
 * `x0` is where the track begins and `edge` is the panel's inner right margin. False when the
 * row has no usable width at all, and then nothing on it is drawn. */
typedef struct overlay_number_columns {
    overlay_number_fit_t fit;
    float track_x1;      /* where the track ends, whatever this row's number says   */
    float number_x1;     /* the number is drawn right aligned against this          */
    float default_x0;    /* equal to default_x1 when no Default fits, which is the  */
    float default_x1;    /* same "no box here" an empty track already says          */
} overlay_number_columns_t;

bool overlay_number_columns(float x0, float edge, float least, float number_w, float default_w,
                            float gap, bool have_default, overlay_number_columns_t *out);

/* Whether a press at `x` takes hold of the track running from `x0` to `x1`, in the caller's own
 * unit. A little outside each end counts, because the handle is drawn centred on an end when the
 * value is at it and half of it then sits beyond the track; without that slack the two extremes
 * would be the only values a drag could not be started from.
 *
 * The two ends get DIFFERENT slack, and that is the whole of the function. Behind the track is
 * the row's indent, which is empty, so a text height of it is free. Ahead of it the number is
 * drawn `gap` away, so the slack there is the gap and not a hair more. It is here rather than in
 * the painter for the same reason overlay_number_fit() is: it is a rule with an answer, and an
 * inequality written into a painter is the version of it nobody can check. */
bool overlay_number_grabs(float x, float x0, float x1, float text_h, float gap);

#endif /* DEV_OVERLAY_OVERLAY_NUMBER_H */
