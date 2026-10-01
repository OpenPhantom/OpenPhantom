/* scene_scope.h: the scenes dialogue_anim_fix acts in, and which actor in one is a participant.
 *
 * One row per level: the actors by the first letters of their model's own name, which is the
 * same string the census in diagnostics resolves, the clip the actor is put in once their line
 * is over, and whether the exchange ends. A new report adds a row here from one census run with
 * Dialogue=1 and Characters=1. The table and the two reads that match an actor against a row
 * live here so the hooks and the per-frame pass in dialogue_anim_fix.c stay the fix and nothing
 * else.
 *
 *   espa.b3d   Mos Espa's opening cutscene: Obi-Wan's head keeps talking through Qui-Gon's line.
 *              The exchange ends and the fix disarms after it.
 *   queen.b3d  the Theed jail: a prisoner spoken to keeps the talking animation after the
 *              conversation is over, his script parked on its talk node for the rest of the
 *              level (issue 26). The census read him at 6/6 before he is spoken to, 8/8 through
 *              both his lines, 2/2 running between them, and 8/8 in script mode 7 for good after
 *              the second. His model, nabcit2, carries ten clips: 0 stnd1, 1 walk1, 2 run1,
 *              3 talk1, 4 hit1, 5 die1, 6 lmout, 7 talk2, 8 talk3, 9 butn1. Clips 0, 3, 6 and
 *              7 were each tried in the cell and none reads as a man waiting: the stand is a
 *              held pose and the other three are talking. So the stand, his own idle, is
 *              written over with the generated one in idle_clip.c before he is put in it, and
 *              the hands-up pass he sits in before he is spoken to is left as shipped. The row
 *              names him twice over, the model nabcit2 and the placement enemy031, so the other
 *              citizens in the level, some on the same model family, are never watched; the
 *              idle is written over the model's stand only once he is being held, so a run in
 *              which he is never spoken to changes nothing at all. Nothing ends, so nothing
 *              disarms.
 */
#ifndef SCENE_SCOPE_H
#define SCENE_SCOPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct scene_scope {
    const char *level_file;
    const char *placement;          /* one placement label, or NULL for any with the model */
    int32_t     rest_anim;          /* the clip the actor is put in after their line */
    bool        generate_idle;      /* write the idle in idle_clip.c over that clip first */
    bool        exchange_ends;      /* disarm after HoldSeconds of silence */
    const char *prefixes[3];        /* NULL terminated */
} scene_scope_t;

/* The row whose level file the path names, or NULL for a level this fix does nothing in. */
const scene_scope_t *scene_scope_for_level(const char *path);

/* How many rows there are, and the row at an index, for the install log. */
size_t               scene_scope_count(void);
const scene_scope_t *scene_scope_at(size_t index);

/* Whether the actor record is one the row watches: its model's name begins with one of the
 * row's prefixes, and, when the row names a placement, its placement label is that one. Any
 * unreadable link in the chain answers false, which leaves the actor untouched. */
bool scene_scope_actor_matches(const scene_scope_t *scope, int32_t actor_record);

#endif /* SCENE_SCOPE_H */
