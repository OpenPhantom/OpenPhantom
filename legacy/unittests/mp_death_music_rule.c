/* What the music of a player is owed after his own death in a session.
 *
 * The rule is driven the way the frame pump drives it, one look a frame, with the two latches set
 * the way the engine sets them: the death puts the silence into the state and the death piece into
 * the sequence, a sound place puts its bed into the state when its turn comes. What the rule asks
 * to be done is carried out on the latches here as the binding does it, so a run of looks reads as
 * the latches would in the game.
 */
#include "unittest.h"

#include "mp_death_music_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SILENCE     MP_DEATH_MUSIC_STATE_NULL
#define DEATH_PIECE MP_DEATH_MUSIC_SEQUENCE_DEATH

/* Two beds of a level and a piece a fight asks for, as numbers no cue of the rule is. */
#define BED_OF_THE_SWAMP 1011
#define BED_OF_THE_CAVE  1023
#define A_FIGHT          2107

/* A frame at sixty a second, rounded up. */
#define FRAME_MS 17u

typedef struct rig {
    mp_death_music_t music;
    int32_t          state;      /* the engine's two latches */
    int32_t          sequence;
    uint32_t         world;
    uint32_t         now_ms;
    bool             read;       /* whether the latches read on the next look */

    unsigned landings;
    unsigned judgements;
    unsigned beds_back;
    unsigned freed;
    int32_t  last_bed;
} rig_t;

static rig_t rig;

static void a_new_rig(int32_t state, int32_t sequence)
{
    memset(&rig, 0, sizeof rig);
    rig.state    = state;
    rig.sequence = sequence;
    rig.world    = 7u;
    rig.now_ms   = 1000u;
    rig.read     = true;
}

/* One look, and what it asks carried out on the latches. */
static mp_death_music_act_t look_once(bool lives, bool corpse)
{
    mp_death_music_look_t look;
    mp_death_music_act_t  act;

    memset(&look, 0, sizeof look);
    look.world        = rig.world;
    look.now_ms       = rig.now_ms;
    look.lives        = lives;
    look.corpse       = corpse;
    look.latches_read = rig.read;
    look.state        = rig.state;
    look.sequence     = rig.sequence;
    mp_death_music_step(&rig.music, &look, &act);

    rig.landings   += act.landed ? 1u : 0u;
    rig.judgements += act.judged ? 1u : 0u;
    if (act.free_sequence) {
        rig.sequence = MP_DEATH_MUSIC_LATCH_FREE;
        ++rig.freed;
    }
    if (act.put_bed_back) {
        rig.state    = act.bed;   /* the setter latches the cue it is handed */
        rig.last_bed = act.bed;
        ++rig.beds_back;
    }
    rig.now_ms += FRAME_MS;
    return act;
}

static void looks(bool lives, bool corpse, unsigned count)
{
    unsigned i;

    for (i = 0u; i < count; ++i) {
        (void)look_once(lives, corpse);
    }
}

static void lives_for(unsigned count)
{
    looks(true, false, count);
}

static void lies_for(unsigned count)
{
    looks(false, true, count);
}

/* Neither living nor a corpse: the fade of the engine's re-entry, or parked for a scene. */
static void fades_for(unsigned count)
{
    looks(false, false, count);
}

/* The engine's own death: both setters, with the cues the death entry pushes. */
static void the_engine_kills_him(void)
{
    rig.state    = SILENCE;
    rig.sequence = DEATH_PIECE;
}

/* Enough looks to be past the half second, whatever the frame is. */
#define PAST_THE_SETTLE (MP_DEATH_MUSIC_SETTLE_MS / FRAME_MS + 2u)

static void check_the_bed_comes_back(void)
{
    ut_section("a death in a level that goes on: the bed he had, half a second after he stands");
    a_new_rig(BED_OF_THE_SWAMP, 0);
    lives_for(120u);
    ut_check(rig.landings == 0u && rig.judgements == 0u && rig.beds_back == 0u && rig.freed == 0u,
             "a living player is owed nothing, however long he lives");

    the_engine_kills_him();
    lies_for(60u);
    fades_for(60u);
    ut_check(rig.landings == 0u && rig.beds_back == 0u,
             "nothing is done while he lies there, nor in the fade of his re-entry");

    (void)look_once(true, false);
    ut_check(rig.landings == 1u && rig.judgements == 0u,
             "the first look of the new life is the landing, and judges nothing yet");
    lives_for(MP_DEATH_MUSIC_SETTLE_MS / FRAME_MS - 2u);
    ut_check(rig.judgements == 0u && rig.state == SILENCE,
             "inside the half second the silence stands: a sound place may still have its turn");
    lives_for(4u);
    ut_check(rig.judgements == 1u && rig.beds_back == 1u && rig.last_bed == BED_OF_THE_SWAMP,
             "and after it the bed he had is put back, once");
    ut_check(rig.freed == 1u && rig.sequence == MP_DEATH_MUSIC_LATCH_FREE,
             "and the death piece is taken out of the sequence latch");

    lives_for(600u);
    ut_check(rig.judgements == 1u && rig.beds_back == 1u && rig.freed == 1u,
             "one death, one judgement: nothing more is done in the life after it");

    ut_section("so the next death plays its piece, and gets the bed back again");
    ut_check(rig.sequence != DEATH_PIECE,
             "the setter is handed a cue its latch does not hold, which is what makes it play");
    the_engine_kills_him();
    lies_for(30u);
    lives_for(PAST_THE_SETTLE + 1u);
    ut_check(rig.landings == 2u && rig.beds_back == 2u && rig.freed == 2u &&
                 rig.state == BED_OF_THE_SWAMP,
             "the second death is treated as the first was");
}

static void check_what_is_no_bed(void)
{
    ut_section("a player who died in a place of silence is given no bed");
    a_new_rig(SILENCE, 0);
    lives_for(10u);
    the_engine_kills_him();
    lies_for(10u);
    lives_for(PAST_THE_SETTLE + 1u);
    ut_check(rig.judgements == 1u && rig.beds_back == 0u,
             "the silence he had is the silence he keeps");
    ut_check(rig.freed == 1u, "the death piece leaves the latch all the same");

    ut_section("nor one whose latch read nought, as a restored savegame can leave it");
    a_new_rig(MP_DEATH_MUSIC_LATCH_FREE, 0);
    lives_for(10u);
    the_engine_kills_him();
    lies_for(10u);
    lives_for(PAST_THE_SETTLE + 1u);
    ut_check(rig.judgements == 1u && rig.beds_back == 0u, "nought is no cue to hand a setter");

    ut_check(mp_death_music_is_a_bed(BED_OF_THE_SWAMP) && !mp_death_music_is_a_bed(SILENCE) &&
                 !mp_death_music_is_a_bed(MP_DEATH_MUSIC_LATCH_FREE),
             "a bed is any state but those two");
}

static void check_a_place_that_spoke_first(void)
{
    ut_section("a sound place he came back inside has the newer word");
    a_new_rig(BED_OF_THE_SWAMP, 0);
    lives_for(10u);
    the_engine_kills_him();
    lies_for(10u);
    lives_for(5u);
    rig.state = BED_OF_THE_CAVE;   /* the place's turn came, inside the half second */
    lives_for(PAST_THE_SETTLE);
    ut_check(rig.judgements == 1u && rig.beds_back == 0u && rig.state == BED_OF_THE_CAVE,
             "its bed is left standing, and the old one is not put over it");

    the_engine_kills_him();
    lies_for(10u);
    lives_for(PAST_THE_SETTLE + 1u);
    ut_check(rig.beds_back == 1u && rig.last_bed == BED_OF_THE_CAVE,
             "and it is the bed the next death puts back");

    ut_section("a latch that moved and fell silent again was moved by somebody: hands off");
    a_new_rig(BED_OF_THE_SWAMP, 0);
    lives_for(10u);
    the_engine_kills_him();
    lies_for(5u);
    rig.state = BED_OF_THE_CAVE;   /* the host's script set a state for this player */
    lies_for(5u);
    rig.state = SILENCE;           /* and took it away again */
    lies_for(5u);
    lives_for(PAST_THE_SETTLE + 1u);
    ut_check(rig.judgements == 1u && rig.beds_back == 0u && rig.state == SILENCE,
             "the silence that stands is not the death's, so nothing is put over it");

    ut_section("with the music switched off the setters latch nothing, and nothing is done");
    a_new_rig(BED_OF_THE_SWAMP, A_FIGHT);
    lives_for(10u);
    lies_for(10u);                 /* a death that left both latches as they were */
    lives_for(PAST_THE_SETTLE + 1u);
    ut_check(rig.judgements == 1u && rig.beds_back == 0u && rig.freed == 0u &&
                 rig.state == BED_OF_THE_SWAMP && rig.sequence == A_FIGHT,
             "neither latch reads what a death leaves, so neither is touched");
}

static void check_the_sequence_latch(void)
{
    ut_section("only the death piece is taken out of the sequence latch");
    a_new_rig(BED_OF_THE_SWAMP, 0);
    lives_for(10u);
    the_engine_kills_him();
    lies_for(10u);
    rig.sequence = A_FIGHT;        /* the host holds a script's piece for this player */
    lives_for(PAST_THE_SETTLE + 1u);
    ut_check(rig.judgements == 1u && rig.freed == 0u && rig.sequence == A_FIGHT,
             "a piece somebody asked for since is left where it is");
    ut_check(rig.beds_back == 1u, "and the bed is its own question, answered as before");
}

static void check_what_forgets(void)
{
    const uint32_t       top = 0xFFFFFF00u;   /* a clock 256 ms under its top */
    mp_death_music_act_t act;

    ut_section("another world forgets: a bed of the last level is not put into the next");
    a_new_rig(BED_OF_THE_SWAMP, 0);
    lives_for(10u);
    the_engine_kills_him();
    lies_for(10u);
    ++rig.world;                   /* everybody was down, and the host chose the next world */
    rig.state    = BED_OF_THE_CAVE;
    rig.sequence = 0;
    lives_for(PAST_THE_SETTLE + 1u);
    ut_check(rig.landings == 0u && rig.judgements == 0u && rig.beds_back == 0u,
             "the death of the old world is not a death in the new one");
    ut_check(rig.music.have_bed && rig.music.bed == BED_OF_THE_CAVE,
             "and what is remembered from here on is the new world's bed");

    ut_section("a death inside the half second starts over, and is judged once");
    a_new_rig(BED_OF_THE_SWAMP, 0);
    lives_for(10u);
    the_engine_kills_him();
    lies_for(10u);
    lives_for(5u);
    the_engine_kills_him();        /* a no-op on both latches: they hold these cues already */
    lies_for(10u);
    ut_check(rig.judgements == 0u, "the first landing was never judged");
    lives_for(PAST_THE_SETTLE + 1u);
    ut_check(rig.landings == 2u && rig.judgements == 1u && rig.beds_back == 1u && rig.freed == 1u,
             "the second is, with the bed from before the first");

    ut_section("a look whose latches did not read learns nothing and asks nothing");
    a_new_rig(BED_OF_THE_SWAMP, 0);
    lives_for(10u);
    the_engine_kills_him();
    lies_for(10u);
    rig.read = false;
    lives_for(PAST_THE_SETTLE + 1u);
    ut_check(rig.landings == 0u && rig.judgements == 0u,
             "the landing waits for a look that reads");
    rig.read = true;
    lives_for(PAST_THE_SETTLE + 1u);
    ut_check(rig.landings == 1u && rig.judgements == 1u && rig.beds_back == 1u,
             "and is then judged half a second after that look");

    ut_section("the clock running past its top is still half a second");
    a_new_rig(BED_OF_THE_SWAMP, 0);
    lives_for(10u);
    the_engine_kills_him();
    lies_for(10u);
    rig.now_ms = top;
    (void)look_once(true, false);
    rig.now_ms = top;
    rig.now_ms += MP_DEATH_MUSIC_SETTLE_MS - 1u;
    act = look_once(true, false);
    ut_check(!act.judged, "one millisecond short, across the top: not yet");
    rig.now_ms = top;
    rig.now_ms += MP_DEATH_MUSIC_SETTLE_MS;
    act = look_once(true, false);
    ut_check(act.judged && act.put_bed_back && act.bed == BED_OF_THE_SWAMP,
             "and on the millisecond it is");

    ut_section("and nothing in hand is answered, not dereferenced");
    memset(&act, 0xFF, sizeof act);
    mp_death_music_step(NULL, NULL, &act);
    mp_death_music_step(&rig.music, NULL, NULL);
    ut_check(true, "a call with nothing in it returns");
}

int main(void)
{
    check_the_bed_comes_back();
    check_what_is_no_bed();
    check_a_place_that_spoke_first();
    check_the_sequence_latch();
    check_what_forgets();
    return ut_summary("mp_death_music_rule");
}
