/* spawn_marks.h: what the placement mode draws over the picture.
 *
 * The ghost cannot say whether it may be placed, since the engine has no transparency per object,
 * so the marks say it: four corners around it, green where a click places it and red where it will
 * not, and a line under the pointer that names the entity, its facing, the count against the cap
 * and the reason for a no. Every copy standing in the level gets thin corners of its own, white for
 * this player's and grey for another player's, and the one under the pointer bright corners and its
 * name, since a right click removes it and removing what one cannot see is a mistake waiting.
 *
 * The corners are drawn where the same projection the pointer is turned round by puts the copy, so
 * they are also the plainest test of the camera arithmetic there is: they sit on the copies or the
 * arithmetic is wrong, visibly, before anything is placed.
 *
 * Drawn at the end of the scene, where the panel is drawn, with the panel's own primitives. The
 * scene is filled by spawn_mode.c; this only draws. Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_SPAWN_MARKS_H
#define DEV_OVERLAY_SPAWN_MARKS_H

#include <stdbool.h>
#include <stdint.h>

#define SPAWN_MARKS_BRACKETS 129u   /* every copy the ring holds, and the ghost */
#define SPAWN_MARKS_LINE     64u

typedef enum spawn_mark_kind {
    SPAWN_MARK_GHOST_OK = 0,
    SPAWN_MARK_GHOST_REFUSED,
    SPAWN_MARK_OWN,
    SPAWN_MARK_FOREIGN,
    SPAWN_MARK_HOVER_OWN,
    SPAWN_MARK_HOVER_FOREIGN
} spawn_mark_kind_t;

/* A box on the screen, in the picture's pixels. */
typedef struct spawn_mark_bracket {
    float             left;
    float             top;
    float             right;
    float             bottom;
    spawn_mark_kind_t kind;
} spawn_mark_bracket_t;

typedef struct spawn_marks_scene {
    spawn_mark_bracket_t bracket[SPAWN_MARKS_BRACKETS];
    uint32_t             brackets;
    float                pointer_x;
    float                pointer_y;
    bool                 good;                  /* the first line in green, else red */
    char                 first[SPAWN_MARKS_LINE];   /* the entity, its facing and the count */
    char                 second[SPAWN_MARKS_LINE];  /* the verdict, or the copy under the pointer */
    char                 hover[SPAWN_MARKS_LINE];   /* the name over the copy under the pointer */
    float                hover_x;
    float                hover_y;
} spawn_marks_scene_t;

/* The screen box of a body standing on `feet_sx/sy` with its head at `head_sx/sy`: as tall as the
 * two are apart and half as wide, never thinner than a few pixels. Pure, for the test. */
void spawn_marks_box(float feet_sx, float feet_sy, float head_sx, float head_sy,
                     spawn_mark_bracket_t *out);

/* Draws the scene: the corners, the pointer, the two lines under it, the name over the copy under
 * it and a line of help at the foot of the picture. */
void spawn_marks_draw(const spawn_marks_scene_t *scene);

/* One line on a band of its own, centred at the top of the picture, where nothing else in this
 * mode draws: the ghost and its two lines follow the pointer and the help sits at the bottom. It
 * is what the mode says about itself while it runs and for a moment after it ends; the words are
 * spawn_banner.c's. */
void spawn_marks_banner(const char *text);

#endif /* DEV_OVERLAY_SPAWN_MARKS_H */
