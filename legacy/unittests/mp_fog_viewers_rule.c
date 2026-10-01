/* mp_fog_viewers_rule.c: the table of fog scripts, the gas room's script bound, and one viewer
 * through the four transitions.
 *
 * The script is a copy of FEDSHIP's "gase" as the export lists it: the entries the row reads and
 * every director entry that sets fog, each with its words as the level carries them. Its
 * fingerprint has to come out as the table's, and the table's numbers were taken with this same
 * function over the eleven shipped levels.
 */
#include "unittest.h"

#include "mp_fog_viewers_rule.h"
#include "mp_level_state_fog_rule.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define GASE_ENTRIES 65u

typedef struct fake_entry {
    uint32_t index;
    int32_t  opcode;
    int32_t  word[3];
} fake_entry_t;

/* FEDSHIP script 51 "gase": the flag test and the states it sends the script to, the two distance
 * tests and the eleven fog commands. Every other entry is a yield. */
static const fake_entry_t GASE[] = {
    { 1u, 0x4602, { 200, 0, (int32_t)0x00000000u } },
    { 2u, 0x4400, { 6, 0, (int32_t)0x00000000u } },
    { 7u, 0x0606, { 6, 0, (int32_t)0x41200419u } },
    { 9u, 0x0606, { 7, 0, (int32_t)0x41800347u } },
    { 11u, 0x4400, { 1, 0, (int32_t)0x00000000u } },
    { 16u, 0x0606, { 11, 20, (int32_t)0x3F800347u } },
    { 18u, 0x4400, { 2, 0, (int32_t)0x00000000u } },
    { 28u, 0x0107, { 0, 1, (int32_t)0x41200069u } },
    { 29u, 0x0606, { 12, 1, (int32_t)0x41800347u } },
    { 31u, 0x4400, { 3, 0, (int32_t)0x00000000u } },
    { 35u, 0x0606, { 6, 0, (int32_t)0x41800347u } },
    { 37u, 0x0606, { 7, 0, (int32_t)0x426005BDu } },
    { 39u, 0x4400, { 4, 0, (int32_t)0x00000000u } },
    { 42u, 0x0107, { 0, 2, (int32_t)0x40600093u } },
    { 43u, 0x0606, { 6, 0, (int32_t)0x41200419u } },
    { 45u, 0x0606, { 7, 0, (int32_t)0x41800347u } },
    { 47u, 0x4400, { 5, 0, (int32_t)0x00000000u } },
    { 51u, 0x0606, { 11, 1, (int32_t)0x40000347u } },
    { 53u, 0x4400, { 2, 0, (int32_t)0x00000000u } },
    { 60u, 0x0606, { 6, 0, (int32_t)0x41800347u } },
    { 62u, 0x0606, { 7, 0, (int32_t)0x426005BDu } },
};

typedef struct fake_script {
    mp_fog_script_entry_t entry[GASE_ENTRIES];
    uint32_t              unreadable;   /* an entry that does not read, or GASE_ENTRIES for none */
} fake_script_t;

static bool read_fake(const void *script, uint32_t index, mp_fog_script_entry_t *out)
{
    const fake_script_t *s = (const fake_script_t *)script;

    if (index >= GASE_ENTRIES || index == s->unreadable) {
        return false;
    }
    *out = s->entry[index];
    return true;
}

static void make_gase(fake_script_t *s)
{
    size_t i;

    memset(s, 0, sizeof *s);
    s->unreadable = GASE_ENTRIES;
    for (i = 0; i < GASE_ENTRIES; ++i) {
        s->entry[i].opcode = 0x4106;   /* a yield, operand inline */
    }
    for (i = 0; i < sizeof GASE / sizeof GASE[0]; ++i) {
        s->entry[GASE[i].index].opcode = GASE[i].opcode;
        memcpy(s->entry[GASE[i].index].word, GASE[i].word, sizeof GASE[i].word);
    }
}

static float float_of(int32_t word)
{
    float out;

    memcpy(&out, &word, sizeof out);
    return out;
}

static void check_the_table(void)
{
    size_t i;
    size_t k;
    size_t per_viewer = 0;

    ut_section("the table: one row per fog script, a fingerprint each");
    ut_checkf(mp_fog_viewers_rows() == 3u, "three scripts of the eleven levels set fog (%u)",
              (unsigned)mp_fog_viewers_rows());
    for (i = 0; i < mp_fog_viewers_rows(); ++i) {
        const mp_fog_script_row_t *row = mp_fog_viewers_row(i);

        per_viewer += row->cls == MP_FOG_CLASS_PER_VIEWER ? 1u : 0u;
        ut_checkf(row->fingerprint != 0u && mp_fog_viewers_row_of(row->fingerprint) == row,
                  "%s is found by its fingerprint %08X", row->name, (unsigned)row->fingerprint);
        ut_checkf((row->cls == MP_FOG_CLASS_PER_VIEWER) == (row->entries != NULL),
                  "%s names entries exactly when it is played per viewer", row->name);
        for (k = 0; k < row->entry_rows; ++k) {
            ut_checkf(row->entries[k].entry < row->entry_count && row->entries[k].role <
                                                                      MP_FOG_ROLES,
                      "%s entry %u lies inside the script and has a role", row->name,
                      (unsigned)row->entries[k].entry);
        }
    }
    ut_check(per_viewer == 1u && strcmp(mp_fog_viewers_row(0)->name, "gase") == 0,
             "one of them is a room's: the gas room");
    ut_check(mp_fog_viewers_row_of(0u) == NULL && mp_fog_viewers_row_of(0x12345678u) == NULL &&
                 mp_fog_viewers_row(99u) == NULL,
             "no fingerprint and an unknown one name no row, and a script in no row is shared");
}

static void check_the_fingerprint(void)
{
    fake_script_t s;
    uint32_t      print = 0;
    uint32_t      fogs = 0;

    ut_section("the fingerprint of the copy is the table's");
    make_gase(&s);
    ut_check(mp_fog_viewers_fingerprint(GASE_ENTRIES, read_fake, &s, &print, &fogs),
             "the copy reads");
    ut_checkf(fogs == 11u && print == mp_fog_viewers_row(0)->fingerprint,
              "eleven fog commands and the table's number, %08X (%08X)",
              (unsigned)mp_fog_viewers_row(0)->fingerprint, (unsigned)print);
    s.entry[37].word[2] = 0x42600000;
    ut_check(mp_fog_viewers_fingerprint(GASE_ENTRIES, read_fake, &s, &print, &fogs) &&
                 mp_fog_viewers_row_of(print) == NULL,
             "one fog value otherwise and it is another script, in no class, and shared");
    make_gase(&s);
    s.entry[16].opcode = 0x0107;
    ut_check(mp_fog_viewers_fingerprint(GASE_ENTRIES, read_fake, &s, &print, &fogs) && fogs == 10u,
             "an entry that is not the director's is no fog command");
    make_gase(&s);
    s.unreadable = 30u;
    ut_check(!mp_fog_viewers_fingerprint(GASE_ENTRIES, read_fake, &s, &print, &fogs) && print == 0u,
             "a script with an entry that does not read has no fingerprint");
    memset(&s, 0, sizeof s);
    s.unreadable = GASE_ENTRIES;
    ut_check(mp_fog_viewers_fingerprint(GASE_ENTRIES, read_fake, &s, &print, &fogs) &&
                 print == 0u && fogs == 0u,
             "and one that sets no fog has none either");
}

static void check_the_binding(mp_fog_bound_t *bound)
{
    fake_script_t  s;
    mp_fog_bound_t wrong;

    ut_section("the gas room's values come out of the script");
    make_gase(&s);
    ut_check(mp_fog_viewers_bind(mp_fog_viewers_row(0), GASE_ENTRIES, read_fake, &s, bound) == 0u &&
                 bound->row == mp_fog_viewers_row(0),
             "every entry the row names holds what it says");
    ut_checkf(bound->leave.mode == 1 && bound->leave.distance == float_of(0x41200069) &&
                  bound->enter.mode == 2 && bound->enter.distance == float_of(0x40600093),
              "leave at >= %.4f, enter at <= %.6f", (double)bound->leave.distance,
              (double)bound->enter.distance);
    ut_check(bound->end_state == 6, "the flag sends it to state 6, its end");
    ut_check(bound->half[MP_FOG_START][0].count == 2u &&
                 bound->half[MP_FOG_START][0].command[0].command == 6 &&
                 bound->half[MP_FOG_START][0].command[1].command == 7 &&
                 bound->half[MP_FOG_START][1].count == 1u &&
                 bound->half[MP_FOG_START][1].command[0].command == 11 &&
                 bound->half[MP_FOG_START][1].command[0].a1 == 20,
             "the start: both edges, and a substep later the twenty second green ramp");
    ut_check(bound->half[MP_FOG_LEAVE][0].count == 1u &&
                 bound->half[MP_FOG_LEAVE][0].command[0].command == 12 &&
                 bound->half[MP_FOG_LEAVE][1].count == 2u,
             "the leave: the ramp back, then the band");
    ut_check(bound->half[MP_FOG_ENTER][0].count == 2u && bound->half[MP_FOG_ENTER][1].count == 1u &&
                 bound->half[MP_FOG_ENTER][1].command[0].a1 == 1,
             "the way back: the band, then a one second green ramp");
    ut_check(bound->half[MP_FOG_END][0].count == 2u && bound->half[MP_FOG_END][1].count == 0u &&
                 bound->half[MP_FOG_END][0].command[1].a2 == 0x426005BDu,
             "the end: the band only, which leaves the colour where it is");

    ut_section("a script that does not hold what the row says does not bind");
    s.entry[29].word[0] = 11;
    ut_check(mp_fog_viewers_bind(mp_fog_viewers_row(0), GASE_ENTRIES, read_fake, &s,
                                 &wrong) == 2u &&
                 wrong.row == NULL,
             "the ramp back where the row reads it is a green ramp: the wrong entry and the leave "
             "it left without a first half, unbound");
    make_gase(&s);
    s.entry[28].word[1] = 7;
    ut_check(mp_fog_viewers_bind(mp_fog_viewers_row(0), GASE_ENTRIES, read_fake, &s, &wrong) != 0u,
             "a distance test with a comparison the engine does not know");
    make_gase(&s);
    s.entry[1].word[0] = 201;
    ut_check(mp_fog_viewers_bind(mp_fog_viewers_row(0), GASE_ENTRIES, read_fake, &s, &wrong) != 0u,
             "a flag test of another flag");
    make_gase(&s);
    ut_check(mp_fog_viewers_bind(mp_fog_viewers_row(0), GASE_ENTRIES - 1u, read_fake, &s, &wrong) !=
                     0u &&
                 mp_fog_viewers_bind(mp_fog_viewers_row(1), GASE_ENTRIES, read_fake, &s, &wrong) !=
                     0u,
             "another entry count, and a shared row, bind nothing");
}

static mp_fog_step_t step(mp_fog_viewer_t *v, const mp_fog_bound_t *b, bool active, bool ended,
                          float distance)
{
    mp_fog_input_t in;

    memset(&in, 0, sizeof in);
    in.active   = active;
    in.ended    = ended;
    in.measured = true;
    in.distance = distance;
    return mp_fog_viewers_decide(v, b, &in);
}

static bool played(mp_fog_step_t s, mp_fog_transition_t transition, uint8_t half)
{
    return s.outcome == (uint8_t)MP_FOG_PLAY && s.transition == (uint8_t)transition &&
           s.half == half;
}

static void check_one_viewer(const mp_fog_bound_t *b)
{
    mp_fog_viewer_t v;
    mp_fog_input_t  in;
    mp_fog_step_t   s;
    float           leave = b->leave.distance;
    float           enter = b->enter.distance;

    ut_section("a viewer in the room when it starts");
    mp_fog_viewers_viewer_init(&v);
    ut_check(step(&v, b, false, false, 1.0f).outcome == (uint8_t)MP_FOG_NOTHING,
             "nothing while the host says the script does not run");
    ut_check(played(step(&v, b, true, false, 1.0f), MP_FOG_START, 0u) &&
                 v.stage == (uint8_t)MP_FOG_STAGE_IN,
             "it starts: the edges");
    ut_check(played(step(&v, b, true, false, 30.0f), MP_FOG_START, 1u),
             "and the green a substep later, wherever he stands by then, as the script's next "
             "state does");
    ut_check(step(&v, b, true, false, leave - 0.01f).outcome == (uint8_t)MP_FOG_NOTHING,
             "just inside the leave distance he stays green");
    ut_check(played(step(&v, b, true, false, leave), MP_FOG_LEAVE, 0u) &&
                 v.stage == (uint8_t)MP_FOG_STAGE_OUT,
             "at it exactly he leaves, as >= does");
    ut_check(played(step(&v, b, true, false, leave), MP_FOG_LEAVE, 1u), "then the band");
    ut_check(step(&v, b, true, false, enter + 0.01f).outcome == (uint8_t)MP_FOG_NOTHING,
             "between the two distances nothing happens: the room's own hysteresis");
    ut_check(played(step(&v, b, true, false, enter), MP_FOG_ENTER, 0u) &&
                 played(step(&v, b, true, false, enter), MP_FOG_ENTER, 1u) &&
                 v.stage == (uint8_t)MP_FOG_STAGE_IN,
             "at the enter distance exactly he comes back in, band and then green");
    s = step(&v, b, false, true, 1.0f);
    ut_check(played(s, MP_FOG_END, 0u) && s.green_at_end && v.stage == (uint8_t)MP_FOG_STAGE_ENDED,
             "the end finds him inside: the band, and he stays green as in single player");
    ut_check(step(&v, b, false, true, 1.0f).outcome == (uint8_t)MP_FOG_NOTHING,
             "and nothing after the end");

    ut_section("a viewer across the level when it starts");
    mp_fog_viewers_viewer_init(&v);
    ut_check(step(&v, b, true, false, 50.0f).outcome == (uint8_t)MP_FOG_STARTED_AWAY &&
                 v.stage == (uint8_t)MP_FOG_STAGE_OUT,
             "sees nothing, not a second of green");
    ut_check(played(step(&v, b, true, false, 2.0f), MP_FOG_ENTER, 0u),
             "and gets the room's fog when he walks in");
    (void)step(&v, b, true, false, 2.0f);
    ut_check(played(step(&v, b, true, false, 20.0f), MP_FOG_LEAVE, 0u), "and loses it walking out");
    (void)step(&v, b, true, false, 20.0f);
    s = step(&v, b, false, true, 20.0f);
    ut_check(played(s, MP_FOG_END, 0u) && !s.green_at_end,
             "the end finds him outside: the band, nothing green left");

    ut_section("what keeps a viewer where he is");
    mp_fog_viewers_viewer_init(&v);
    memset(&in, 0, sizeof in);
    in.active = true;
    in.dead   = true;
    ut_check(mp_fog_viewers_decide(&v, b, &in).outcome == (uint8_t)MP_FOG_UNDECIDED_DEAD &&
                 v.stage == (uint8_t)MP_FOG_STAGE_NEVER,
             "a dead player keeps his state");
    in.dead = false;
    ut_check(mp_fog_viewers_decide(&v, b, &in).outcome == (uint8_t)MP_FOG_UNDECIDED_UNMEASURED,
             "and so does one whose place did not read, a far bank's window among them");
    mp_fog_viewers_viewer_init(&v);
    ut_check(step(&v, b, false, true, 1.0f).outcome == (uint8_t)MP_FOG_ENDED_UNSEEN &&
                 v.stage == (uint8_t)MP_FOG_STAGE_ENDED,
             "an end before any start plays nothing");
    mp_fog_viewers_viewer_init(&v);
    (void)step(&v, b, true, false, 1.0f);
    ut_check(played(step(&v, b, false, true, 1.0f), MP_FOG_START, 1u) &&
                 played(step(&v, b, false, true, 1.0f), MP_FOG_END, 0u),
             "a second half that is due comes before an end that arrives with it");
    ut_check(mp_fog_viewers_decide(&v, NULL, &in).outcome == (uint8_t)MP_FOG_NOTHING,
             "and nothing is decided against no binding");
}

/* FEDSHIP script 0 "beac" as the level carries it: fourteen entries of eight bytes, opcode, branch
 * and operand, and the nine words of its pool behind them. Entry 0 is the script's head with its
 * name and entries 2 and 8 are the label of its state "wait", four letters each where another
 * entry has a pool index. */
static const uint8_t BEAC_ENTRIES[14][8] = {
    { 0x00, 0x00, 0x01, 0x00, 0x62, 0x65, 0x61, 0x63 },
    { 0x06, 0x41, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x01, 0x00, 0x06, 0x00, 0x77, 0x61, 0x69, 0x74 },
    { 0x07, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x09, 0x02, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00 },
    { 0x00, 0x44, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00 },
    { 0x00, 0x42, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x42, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 },
    { 0x01, 0x00, 0xFF, 0xFF, 0x77, 0x61, 0x69, 0x74 },
    { 0x0A, 0x02, 0x04, 0x00, 0x05, 0x00, 0x00, 0x00 },
    { 0x09, 0x02, 0x01, 0x00, 0x07, 0x00, 0x00, 0x00 },
    { 0x0B, 0x42, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x42, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x42, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 },
};
#define BEAC_POOL_WORDS 9u

static mp_fog_operand_t operand_of(const uint8_t raw[8], uint32_t pool_words)
{
    int16_t opcode = 0;
    int32_t operand = 0;

    memcpy(&opcode, raw, sizeof opcode);
    memcpy(&operand, raw + 4, sizeof operand);
    return mp_fog_viewers_operand(opcode, operand, pool_words);
}

static void check_the_operands_of_a_real_script(void)
{
    uint32_t n[MP_FOG_OPERAND_PAST_POOL + 1];
    size_t   i;

    ut_section("which entries of a script read the pool: FEDSHIP script 0 as bytes");
    memset(n, 0, sizeof n);
    for (i = 0; i < sizeof BEAC_ENTRIES / sizeof BEAC_ENTRIES[0]; ++i) {
        ++n[operand_of(BEAC_ENTRIES[i], BEAC_POOL_WORDS)];
    }
    ut_check(operand_of(BEAC_ENTRIES[0], BEAC_POOL_WORDS) == MP_FOG_OPERAND_NAME &&
                 operand_of(BEAC_ENTRIES[2], BEAC_POOL_WORDS) == MP_FOG_OPERAND_NAME &&
                 operand_of(BEAC_ENTRIES[8], BEAC_POOL_WORDS) == MP_FOG_OPERAND_NAME,
             "the head 'beac' and both labels 'wait' are names, and no pool word is read for them; "
             "read as an index each reached far past the level's scripts");
    ut_check(operand_of(BEAC_ENTRIES[3], BEAC_POOL_WORDS) == MP_FOG_OPERAND_POOL,
             "the distance test's three words are read, at index 0 of a pool of nine");
    ut_checkf(n[MP_FOG_OPERAND_POOL] == 1u && n[MP_FOG_OPERAND_NAME] == 3u &&
                  n[MP_FOG_OPERAND_UNUSED] == 3u && n[MP_FOG_OPERAND_INLINE] == 7u &&
                  n[MP_FOG_OPERAND_PAST_POOL] == 0u,
              "one pool read, three names, three entries whose words no row reads, seven inline "
              "(%u, %u, %u, %u)", (unsigned)n[MP_FOG_OPERAND_POOL],
              (unsigned)n[MP_FOG_OPERAND_NAME],
              (unsigned)n[MP_FOG_OPERAND_UNUSED], (unsigned)n[MP_FOG_OPERAND_INLINE]);

    ut_section("a pool index is read only where all three words lie inside the script's own pool");
    ut_check(mp_fog_viewers_operand(0x0606, 6, BEAC_POOL_WORDS) == MP_FOG_OPERAND_POOL,
             "a director entry at 6 of 9 reads 6, 7 and 8");
    ut_check(mp_fog_viewers_operand(0x0606, 7, BEAC_POOL_WORDS) == MP_FOG_OPERAND_PAST_POOL &&
                 mp_fog_viewers_operand(0x0107, 7, BEAC_POOL_WORDS) == MP_FOG_OPERAND_PAST_POOL,
             "at 7 its third word is the next script's, and it is refused");
    ut_check(mp_fog_viewers_operand(0x0606, -1, BEAC_POOL_WORDS) == MP_FOG_OPERAND_PAST_POOL &&
                 mp_fog_viewers_operand(0x0606, 0x63616562, BEAC_POOL_WORDS) ==
                     MP_FOG_OPERAND_PAST_POOL &&
                 mp_fog_viewers_operand(0x0606, 0, 2u) == MP_FOG_OPERAND_PAST_POOL &&
                 mp_fog_viewers_operand(0x0606, 0, 0u) == MP_FOG_OPERAND_PAST_POOL,
             "and so is a negative index, a name standing where an index should, and any index "
             "into a pool of fewer than three words");
    ut_check(mp_fog_viewers_operand(0x4606, 0x63616562, 0u) == MP_FOG_OPERAND_INLINE &&
                 mp_fog_viewers_operand(0x0602, 0, BEAC_POOL_WORDS) == MP_FOG_OPERAND_UNUSED &&
                 mp_fog_viewers_operand(0x0400, 0, BEAC_POOL_WORDS) == MP_FOG_OPERAND_UNUSED,
             "an inline operand is the entry's own, and the flag test and the change of state read "
             "no pool word here: the rows name both inline");
    ut_check(mp_fog_viewers_pool_words(0x1000u, 0x1024u) == 9u &&
                 mp_fog_viewers_pool_words(0x1000u, 0x1027u) == 9u &&
                 mp_fog_viewers_pool_words(0x1000u, 0x1000u) == 0u &&
                 mp_fog_viewers_pool_words(0x1000u, 0u) == 0u,
             "a pool runs to the next record in whole words, and an end not past it is no pool");
}

/* The gas room's script laid out as the engine keeps it, entries of eight bytes and a pool behind
 * them with three words for every entry the row reads, and a head and seven state labels whose
 * operands are names. */
#define LAID_POOL_WORDS (3u * (sizeof GASE / sizeof GASE[0]))

typedef struct laid_script {
    uint8_t  entry[GASE_ENTRIES][8];
    int32_t  pool[LAID_POOL_WORDS];
    uint32_t pool_words;
} laid_script_t;

static const struct {
    uint32_t index;
    char     name[5];
} GASE_NAMES[] = {
    { 0u, "gase" }, { 3u, "0sta" }, { 12u, "1gas" }, { 19u, "2wai" }, { 30u, "3res" },
    { 38u, "4wai" }, { 46u, "5reg" }, { 54u, "zzzz" },
};

static void put_entry(laid_script_t *s, uint32_t index, int16_t opcode, int32_t operand)
{
    memcpy(s->entry[index], &opcode, sizeof opcode);
    memset(s->entry[index] + 2, 0, 2);
    memcpy(s->entry[index] + 4, &operand, sizeof operand);
}

static void lay_gase(laid_script_t *s)
{
    size_t  i;
    int32_t name;

    memset(s, 0, sizeof *s);
    s->pool_words = LAID_POOL_WORDS;
    for (i = 0; i < GASE_ENTRIES; ++i) {
        put_entry(s, (uint32_t)i, 0x4106, 0);
    }
    for (i = 0; i < sizeof GASE_NAMES / sizeof GASE_NAMES[0]; ++i) {
        memcpy(&name, GASE_NAMES[i].name, sizeof name);
        put_entry(s, GASE_NAMES[i].index, i == 0u ? 0x0000 : 0x0001, name);
    }
    for (i = 0; i < sizeof GASE / sizeof GASE[0]; ++i) {
        if ((GASE[i].opcode & MP_FOG_OP_INLINE) != 0) {
            put_entry(s, GASE[i].index, (int16_t)GASE[i].opcode, GASE[i].word[0]);
            continue;
        }
        put_entry(s, GASE[i].index, (int16_t)GASE[i].opcode, (int32_t)(3u * i));
        memcpy(&s->pool[3u * i], GASE[i].word, sizeof GASE[i].word);
    }
}

/* This side's reader as it is built now: the opcode decides, and only inside the pool. */
static bool read_laid(const void *script, uint32_t index, mp_fog_script_entry_t *out)
{
    const laid_script_t *s = (const laid_script_t *)script;
    int16_t              opcode = 0;
    int32_t              operand = 0;

    if (index >= GASE_ENTRIES) {
        return false;
    }
    memset(out, 0, sizeof *out);
    memcpy(&opcode, s->entry[index], sizeof opcode);
    memcpy(&operand, s->entry[index] + 4, sizeof operand);
    out->opcode = opcode;
    switch (mp_fog_viewers_operand(opcode, operand, s->pool_words)) {
    case MP_FOG_OPERAND_INLINE:
        out->word[0] = operand;
        break;
    case MP_FOG_OPERAND_POOL:
        memcpy(out->word, &s->pool[operand], sizeof out->word);
        break;
    default:
        break;
    }
    return true;
}

/* The reader as it was, kept as the reference: every entry that is not inline reads three words
 * at its operand. Here only indices inside the test's own array are followed; in the game the rest
 * went wherever the name pointed. */
static bool read_laid_before(const void *script, uint32_t index, mp_fog_script_entry_t *out)
{
    const laid_script_t *s = (const laid_script_t *)script;
    int16_t              opcode = 0;
    int32_t              operand = 0;

    if (index >= GASE_ENTRIES) {
        return false;
    }
    memset(out, 0, sizeof *out);
    memcpy(&opcode, s->entry[index], sizeof opcode);
    memcpy(&operand, s->entry[index] + 4, sizeof operand);
    out->opcode = opcode;
    if ((opcode & MP_FOG_OP_INLINE) != 0) {
        out->word[0] = operand;
        return true;
    }
    if (operand >= 0 && (uint32_t)operand + 3u <= LAID_POOL_WORDS) {
        memcpy(out->word, &s->pool[operand], sizeof out->word);
    }
    return true;
}

static void check_the_laid_script(void)
{
    laid_script_t  s;
    mp_fog_bound_t now;
    mp_fog_bound_t before;
    uint32_t       print = 0;
    uint32_t       print_before = 0;
    uint32_t       fogs = 0;
    uint32_t       same = 0;
    uint32_t       read = 0;
    size_t         i;

    ut_section("the gas room laid out as bytes, read the new way and the old");
    lay_gase(&s);
    ut_check(mp_fog_viewers_fingerprint(GASE_ENTRIES, read_laid, &s, &print, &fogs) &&
                 mp_fog_viewers_fingerprint(GASE_ENTRIES, read_laid_before, &s, &print_before,
                                            NULL),
             "both readers read every entry");
    ut_checkf(print == 0x60359C12u && print_before == print && fogs == 11u,
              "the fingerprint is the table's, 60359C12, both ways (%08X, %08X)", (unsigned)print,
              (unsigned)print_before);
    ut_check(mp_fog_viewers_bind(mp_fog_viewers_row(0), GASE_ENTRIES, read_laid, &s, &now) == 0u &&
                 mp_fog_viewers_bind(mp_fog_viewers_row(0), GASE_ENTRIES, read_laid_before, &s,
                                     &before) == 0u &&
                 memcmp(&now, &before, sizeof now) == 0,
             "all fifteen entries of the row bind, to the same values the old reader found");
    for (i = 0; i < GASE_ENTRIES; ++i) {
        mp_fog_script_entry_t a;
        mp_fog_script_entry_t b;
        int32_t               code;

        (void)read_laid(&s, (uint32_t)i, &a);
        (void)read_laid_before(&s, (uint32_t)i, &b);
        code = a.opcode & MP_FOG_OP_MASK;
        if ((a.opcode & MP_FOG_OP_INLINE) == 0 &&
            (code == MP_FOG_OP_DIRECTOR || code == MP_FOG_OP_DISTANCE)) {
            ++read;
            same += memcmp(a.word, b.word, sizeof a.word) == 0 ? 1u : 0u;
        }
    }
    ut_checkf(read == 13u && same == read,
              "every director entry and distance test carries the old reader's words (%u of %u)",
              (unsigned)same, (unsigned)read);

    ut_section("a director entry whose words run past the pool");
    s.pool_words = LAID_POOL_WORDS - 1u;   /* zzzz's band end, entry 62, reads words 60 to 62 */
    ut_check(mp_fog_viewers_fingerprint(GASE_ENTRIES, read_laid, &s, &print, &fogs) &&
                 fogs == 10u && mp_fog_viewers_row_of(print) == NULL,
             "is not read: one fog command fewer, another fingerprint, and the script is shared "
             "rather than bound on words that are not its own");
}

static void check_the_comparisons(void)
{
    ut_section("the engine's six comparisons");
    ut_check(mp_fog_viewers_compare(2.0f, 0, 2.0f) && mp_fog_viewers_compare(2.0f, 1, 2.0f) &&
                 mp_fog_viewers_compare(2.0f, 2, 2.0f) && !mp_fog_viewers_compare(2.0f, 3, 2.0f) &&
                 !mp_fog_viewers_compare(2.0f, 4, 2.0f) && !mp_fog_viewers_compare(2.0f, 5, 2.0f),
             "equal, at least, at most, not equal, more, less");
    ut_check(!mp_fog_viewers_compare(2.0f, 6, 1.0f) && !mp_fog_viewers_compare(2.0f, -1, 1.0f),
             "and an unknown one is false");
}

int main(void)
{
    mp_fog_bound_t bound;

    check_the_table();
    check_the_fingerprint();
    check_the_binding(&bound);
    check_one_viewer(&bound);
    check_the_comparisons();
    check_the_operands_of_a_real_script();
    check_the_laid_script();

    return ut_summary("mp_fog_viewers_rule");
}
