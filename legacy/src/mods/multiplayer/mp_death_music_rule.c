/* mp_death_music_rule.c: what the music of this player is owed after his own death. See the
 * header. */
#include "mp_death_music_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

bool mp_death_music_is_a_bed(int32_t state)
{
    return state != MP_DEATH_MUSIC_STATE_NULL && state != MP_DEATH_MUSIC_LATCH_FREE;
}

/* Between the death and the judgement the state latch reads the silence the death set. Anything
 * else there was set by somebody since, a sound place or the host's script, and is the newer
 * truth: it is kept as the bed, and nothing is put over it. */
static void note_a_move(mp_death_music_t *music, const mp_death_music_look_t *look)
{
    if (!look->latches_read || look->state == MP_DEATH_MUSIC_STATE_NULL) {
        return;
    }
    music->moved    = true;
    music->have_bed = true;
    music->bed      = look->state;
}

void mp_death_music_step(mp_death_music_t *music, const mp_death_music_look_t *look,
                         mp_death_music_act_t *act)
{
    if (music == NULL || look == NULL || act == NULL) {
        return;
    }
    memset(act, 0, sizeof *act);

    /* Another world: a level ended, a savegame was restored, the session began or ended. The
     * engine has set its own music for it, and a bed out of the last world is not put into it. */
    if (!music->world_known || music->world != look->world) {
        memset(music, 0, sizeof *music);
        music->world_known = true;
        music->world       = look->world;
    }
    if (look->corpse) {
        if (!music->down) {
            music->down  = true;
            music->moved = false;
        }
        music->settling = false;
        note_a_move(music, look);
        return;
    }
    if (music->down || music->settling) {
        note_a_move(music, look);
    }
    /* Parked for a scene, in the fade of a re-entry, or a look whose latches did not read: nothing
     * more is learned and nothing is owed on it. */
    if (!look->lives || !look->latches_read) {
        return;
    }
    if (music->down) {
        music->down      = false;
        music->settling  = true;
        music->landed_ms = look->now_ms;
        act->landed      = true;
        return;
    }
    if (music->settling) {
        if ((uint32_t)(look->now_ms - music->landed_ms) < MP_DEATH_MUSIC_SETTLE_MS) {
            return;
        }
        music->settling = false;
        act->judged     = true;
        if (!music->moved && look->state == MP_DEATH_MUSIC_STATE_NULL && music->have_bed &&
            mp_death_music_is_a_bed(music->bed)) {
            act->put_bed_back = true;
            act->bed          = music->bed;
        }
        if (look->sequence == MP_DEATH_MUSIC_SEQUENCE_DEATH) {
            act->free_sequence = true;
        }
        return;   /* the bed is noted again from the next look on, after the act */
    }
    music->have_bed = true;
    music->bed      = look->state;
}
