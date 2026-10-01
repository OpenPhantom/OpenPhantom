/* The records between the multiplayer and the overlay: shape, name rule, soundness, round trip. */
#include "unittest.h"

#include "common/model_wear_note.h"
#include "common/shared_note.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static void set_name(char field[MODEL_WEAR_NAME_MAX], const char *name)
{
    memset(field, 0, MODEL_WEAR_NAME_MAX);
    memcpy(field, name, strlen(name));
}

static void check_the_shape(void)
{
    ut_section("the shape, and that every far bank fits one note");
    ut_check(sizeof(model_wear_want_record_t) == 148u, "the wish is 148 bytes for three banks");
    ut_check(sizeof(model_wear_done_record_t) == 136u, "the answer is 136 bytes for three banks");
    ut_check(sizeof(model_wear_want_record_t) <= SHARED_NOTE_BYTES &&
                 sizeof(model_wear_done_record_t) <= SHARED_NOTE_BYTES,
             "and both fit the shared note's payload");
}

static void check_the_name_rule(void)
{
    static const struct { const char *name; bool sound; } NAMES[] = {
        { "",             true  },
        { "anakin.baf",   true  },
        { "a",            true  },
        { "abcdefgh.baf", true  },
        { "abcdefghi.baf", false },
        { ".baf",         false },
        { "a..b",         false },
        { "a.bafx",       false },
        { "a.",           false },
        { "stnd-wp1.baf", false },
        { "a b.baf",      false },
    };
    char   field[MODEL_WEAR_NAME_MAX];
    size_t i;

    ut_section("an asset name is empty or the engine's 8.3 rule, and the same bytes for one name");
    for (i = 0; i < sizeof NAMES / sizeof NAMES[0]; ++i) {
        set_name(field, NAMES[i].name);
        ut_checkf(model_wear_name_is_sound(field) == NAMES[i].sound, "\"%s\" is %s",
                  NAMES[i].name, NAMES[i].sound ? "sound" : "refused");
    }
    memset(field, 'a', sizeof field);
    ut_check(!model_wear_name_is_sound(field), "a field with no terminator is refused");
    set_name(field, "anakin.baf");
    field[MODEL_WEAR_NAME_MAX - 1u] = 'x';
    ut_check(!model_wear_name_is_sound(field), "and so is one with bytes after the terminator");
    ut_check(!model_wear_name_is_sound(NULL), "and none is no name at all");
}

static void check_the_wish(void)
{
    model_wear_want_record_t want;

    ut_section("a wish: a body has a block, and a bank with no body wants nothing");
    memset(&want, 0, sizeof want);
    ut_check(model_wear_want_is_sound(&want), "three empty banks are a sound wish");
    want.bank[0].serial = 3u;
    want.bank[0].object = 0x07C18618u;
    want.bank[0].block  = 0x10020000u;
    want.bank[0].slot   = 2u;
    set_name(want.bank[0].model, "anakin.baf");
    ut_check(model_wear_want_is_sound(&want), "a body with its block and a model is sound");
    want.bank[0].block = 0u;
    ut_check(!model_wear_want_is_sound(&want), "a body without a block is refused");
    want.bank[0].block  = 0x10020000u;
    want.bank[1].object = 0u;
    set_name(want.bank[1].model, "anakin.baf");
    ut_check(!model_wear_want_is_sound(&want), "a model for a bank with no body is refused");
    set_name(want.bank[1].model, "");
    want.bank[2].reserved[1] = 1u;
    ut_check(!model_wear_want_is_sound(&want), "and a reserved byte that is not zero");
}

static void check_the_answer(void)
{
    model_wear_done_record_t done;

    ut_section("an answer: WORN names what is worn, a refusal echoes only another model");
    memset(&done, 0, sizeof done);
    done.ready = MODEL_WEAR_READY_LISTENING | MODEL_WEAR_READY_ABLE;
    ut_check(model_wear_done_is_sound(&done), "a listening overlay with nothing to say is sound");

    done.bank[0].serial = 3u;
    done.bank[0].state  = MODEL_WEAR_STATE_WORN;
    done.bank[0].scale  = 1.1f;
    set_name(done.bank[0].model, "anakin.baf");
    ut_check(model_wear_done_is_sound(&done), "WORN with the model and its scale is sound");
    done.bank[0].scale = (float)sqrt(-1.0);
    ut_check(!model_wear_done_is_sound(&done), "a scale that is not a number is refused");
    done.bank[0].scale = 11.0f;
    ut_check(!model_wear_done_is_sound(&done), "and one outside the bounds");
    done.bank[0].scale = 1.1f;
    set_name(done.bank[0].model, "");
    ut_check(!model_wear_done_is_sound(&done), "WORN with no model named is refused");

    done.bank[0].state  = MODEL_WEAR_STATE_REFUSED;
    done.bank[0].reason = MODEL_WEAR_REASON_WEARS_OTHER;
    done.bank[0].scale  = 0.0f;
    set_name(done.bank[0].model, "baron.baf");
    ut_check(model_wear_done_is_sound(&done),
             "a refusal because the body wears another model names that model");
    done.bank[0].reason = MODEL_WEAR_REASON_FIT;
    ut_check(!model_wear_done_is_sound(&done), "any other refusal echoes nothing");
    set_name(done.bank[0].model, "");
    ut_check(model_wear_done_is_sound(&done), "and with the echo empty it is sound");
    done.bank[0].reason = (uint8_t)(MODEL_WEAR_REASON_MAX + 1u);
    ut_check(!model_wear_done_is_sound(&done), "a reason this build does not know is refused");
    done.bank[0].reason = MODEL_WEAR_REASON_NONE;
    ut_check(!model_wear_done_is_sound(&done), "and so is a refusal with no reason");
    done.bank[0].state = 3u;
    ut_check(!model_wear_done_is_sound(&done), "and a state this build does not know");
    memset(&done.bank[0], 0, sizeof done.bank[0]);
    done.ready = 0x08u;
    ut_check(!model_wear_done_is_sound(&done), "and a ready bit this build does not know");
}

/* The flag that says a worn far body carries its player's own weapon at the borrowed rig's hand.
 * Everything the multiplayer does with a worn body's blade hangs off it, so a record that carries
 * it where it means nothing is refused rather than read. */
static void check_the_weapon_flag(void)
{
    model_wear_done_record_t done;

    ut_section("the weapon flag: 0 or 1, and nothing but a worn body may carry one");
    memset(&done, 0, sizeof done);
    done.ready = MODEL_WEAR_READY_LISTENING | MODEL_WEAR_READY_ABLE | MODEL_WEAR_READY_WEAPON;
    ut_check(model_wear_done_is_sound(&done),
             "an overlay that says where a borrowed weapon is drawn from is sound");

    done.bank[1].serial = 4u;
    done.bank[1].state  = MODEL_WEAR_STATE_WORN;
    done.bank[1].scale  = 1.0f;
    set_name(done.bank[1].model, "anakin.baf");
    ut_check(model_wear_done_is_sound(&done), "a worn body that carries no weapon is sound");
    done.bank[1].weapon = 1u;
    ut_check(model_wear_done_is_sound(&done), "and so is one that carries its player's");
    done.bank[1].weapon = 2u;
    ut_check(!model_wear_done_is_sound(&done), "anything but 0 or 1 is refused");

    done.bank[1].weapon = 1u;
    done.bank[1].state  = MODEL_WEAR_STATE_REFUSED;
    done.bank[1].reason = MODEL_WEAR_REASON_WEARS_OTHER;
    done.bank[1].scale  = 0.0f;
    ut_check(!model_wear_done_is_sound(&done),
             "a refusal that claims a weapon is refused: a body the overlay did not dress "
             "carries nothing the overlay hung");
    memset(&done.bank[1], 0, sizeof done.bank[1]);
    done.bank[1].weapon = 1u;
    ut_check(!model_wear_done_is_sound(&done), "and so is a bank with nothing said and a weapon");
    done.bank[1].reserved = 1u;
    done.bank[1].weapon   = 0u;
    ut_check(!model_wear_done_is_sound(&done),
             "the reserved byte beside it is still held to zero");
}

static void check_the_round_trip(void)
{
    model_wear_want_record_t want;
    model_wear_want_record_t back;
    model_wear_done_record_t done;
    model_wear_done_record_t got;
    uint32_t                 first = 0;
    uint32_t                 second = 0;

    ut_section("the records cross, with their version, and a repeat is a new publication");
    ut_check(!model_wear_read_want(&back, NULL) && !model_wear_read_done(&got, NULL),
             "before anybody published, there is nothing to read");

    memset(&want, 0, sizeof want);
    want.bank[1].serial = 7u;
    want.bank[1].object = 0x07C18618u;
    want.bank[1].block  = 0x10020000u;
    want.bank[1].slot   = 1u;
    set_name(want.bank[1].model, "anakin.baf");
    ut_check(model_wear_publish_want(&want) && model_wear_read_want(&back, &first),
             "a wish is published and read back");
    ut_check(back.version == MODEL_WEAR_NOTE_VERSION &&
                 memcmp(&back.bank, &want.bank, sizeof want.bank) == 0,
             "field for field, with the version filled in");
    ut_check(MODEL_WEAR_NOTE_VERSION == 2u, "which is 2, the version that names the weapon flag");
    ut_check(model_wear_publish_want(&want) && model_wear_read_want(&back, &second) &&
                 second != first,
             "publishing the same wish again is told apart by its serial");

    want.bank[1].block = 0u;
    ut_check(!model_wear_publish_want(&want), "a wish that is not sound is not published");

    back.version = 1u;
    ut_check(shared_note_publish(MODEL_WEAR_WANT_NOTE_NAME, &back, sizeof back) &&
                 !model_wear_read_want(&want, NULL),
             "a record of version 1 is not read, though the wish itself did not change: the two "
             "sides ship as one mod and change over together");
    back.version = 3u;
    ut_check(shared_note_publish(MODEL_WEAR_WANT_NOTE_NAME, &back, sizeof back) &&
                 !model_wear_read_want(&want, NULL),
             "and neither is one from a build later than this");

    memset(&done, 0, sizeof done);
    done.ready = MODEL_WEAR_READY_LISTENING;
    done.bank[1].serial = 7u;
    done.bank[1].state  = MODEL_WEAR_STATE_REFUSED;
    done.bank[1].reason = MODEL_WEAR_REASON_BLADE;
    ut_check(model_wear_publish_done(&done) && model_wear_read_done(&got, NULL) &&
                 got.ready == MODEL_WEAR_READY_LISTENING && got.bank[1].serial == 7u &&
                 got.bank[1].reason == MODEL_WEAR_REASON_BLADE,
             "an answer crosses the same way");
}

int main(void)
{
    check_the_shape();
    check_the_name_rule();
    check_the_wish();
    check_the_answer();
    check_the_weapon_flag();
    check_the_round_trip();
    return ut_summary("model_wear_note");
}
