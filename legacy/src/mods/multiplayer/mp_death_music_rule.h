/* mp_death_music_rule.h: what the music of this player is owed after his own death in a session.
 *
 * Layer 1, pure. No engine and no address: the binding is mp_death_music.c, and every decision it
 * makes is the one function here.
 *
 * The engine's music has two latches, a state and a sequence. The state is the bed of a place, set
 * when the camera's anchor walks into the small box of a sound place at a doorway and left
 * standing when it walks out. The sequence is a piece over the bed. A setter does nothing when it
 * is handed the cue its latch already holds.
 *
 * The engine's death sets the state to its null cue, the silence, and the sequence to the death
 * piece. In single player the level then ends, and the restart or the load brings the music back.
 * In a session the level goes on and the player comes back into it a second later, beside a team
 * mate, without having walked through any doorway: the bed stays the silence until he next crosses
 * the box of a sound place, which in a level with few of them is a long time. And the sequence
 * latch keeps naming the death piece, so his next death asks for the cue the latch already holds
 * and plays nothing.
 *
 * So the bed the living player last had is remembered, and once he stands again in the same world
 * two things are looked at, half a second after the landing so that a sound place he came back
 * inside has had its turn first. A state latch that read the silence from the death to that look
 * gets the remembered bed back. A sequence latch that still names the death piece is released, by
 * its cell alone and with no call into the music, so the piece plays out and the next sequence
 * asked for is not swallowed, the next death piece and a fight's loop among them.
 *
 * What is deliberately not done: the sequence that ran before the death is not started again. The
 * latch may name a piece that played once and ended long ago, and only the music library knows a
 * loop from such a piece; starting it again would play it at every return. A loop comes back
 * through whoever asks for it, now that the latch no longer swallows the request.
 *
 * What it cannot tell. A place of silence the player comes back inside sets the silence over the
 * silence, which the latch does not show, so the bed is put back and the place takes it away again
 * at its next turn. And a place that set its bed in the very substep of the death, ahead of the
 * player's own tick, was never seen, so the bed from before it is the one that goes back.
 */
#ifndef MULTIPLAYER_MP_DEATH_MUSIC_RULE_H
#define MULTIPLAYER_MP_DEATH_MUSIC_RULE_H

#include <stdbool.h>
#include <stdint.h>

/* The cues the engine's own death names: it pushes the first as the state and the second as the
 * sequence. */
#define MP_DEATH_MUSIC_STATE_NULL     1000
#define MP_DEATH_MUSIC_SEQUENCE_DEATH 0xAB4

/* What is written into the sequence latch to release it. Nought, which is what the engine itself
 * writes into both latches before it sets the cues of a restored savegame. Nought is a cue only
 * a restored savegame and the audio screen hand over, and each of them sets the null sequence
 * first, so with nought in the latch no request is swallowed. */
#define MP_DEATH_MUSIC_LATCH_FREE 0

/* How long after the landing the latches are judged. A sound place whose box the player stands in
 * sets its state when its turn comes, a few times a second for a place with no interval of its
 * own; half a second lets it. A place with a longer interval speaks later and then has the
 * last word, over the bed that was put back. */
#define MP_DEATH_MUSIC_SETTLE_MS 500u

typedef struct mp_death_music {
    bool     world_known;
    uint32_t world;        /* the world this memory belongs to: the enemy table's reset count */
    bool     have_bed;
    int32_t  bed;          /* the state latch as the living player last had it */
    bool     down;         /* a corpse was seen since the bed was last noted */
    bool     settling;     /* he stands again and the half second has not run out */
    bool     moved;        /* the state latch read another cue than the silence since the death */
    uint32_t landed_ms;
} mp_death_music_t;

/* One look, once a frame. */
typedef struct mp_death_music_look {
    uint32_t world;
    uint32_t now_ms;
    bool     lives;         /* the player module runs and the body is no corpse */
    bool     corpse;
    bool     latches_read;
    int32_t  state;         /* the two latches as they read now */
    int32_t  sequence;
} mp_death_music_look_t;

/* What the look asks to be done. */
typedef struct mp_death_music_act {
    bool    landed;         /* this look is the first of a life after a death */
    bool    judged;         /* the half second ran out on this look */
    bool    put_bed_back;   /* call the state setter with `bed` */
    int32_t bed;
    bool    free_sequence;  /* write MP_DEATH_MUSIC_LATCH_FREE into the sequence latch */
} mp_death_music_act_t;

/* Whether a state is a bed worth putting back: neither the silence nor the nought a restored
 * savegame can leave in the latch. */
bool mp_death_music_is_a_bed(int32_t state);

void mp_death_music_step(mp_death_music_t *music, const mp_death_music_look_t *look,
                         mp_death_music_act_t *act);

#endif /* MULTIPLAYER_MP_DEATH_MUSIC_RULE_H */
