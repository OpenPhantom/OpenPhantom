/* spawn_scripts.c: the NPC spawner's compiled behaviour scripts, checked without the game.
 *
 * The records are generated from the .bais sources by the level editor's compiler and carried
 * as data, so what can go wrong is the data: a state label that does not point at a mode entry,
 * a branch that leaves the record, a slot the spawner would fill outside the pool, or a record
 * that outgrew the buffer each copy carries. None of that needs the game. The clip filling
 * itself needs the archive and is refused without it, which is the last claim here.
 */
#include "unittest.h"

#include "spawn_scripts.h"
#include "spawn_script_data.h"

#include <stdio.h>
#include <string.h>

#define RECORD_HEAD  0x20u
#define OP_MASK      0x0FFFu
#define OP_AI_MODE   0x001u
#define OP_MOVE_SPEED 0x408u
#define INLINE_BIT   0x4000u

static void check_record(const spawn_script_record_t *r)
{
    char     what[160];
    uint32_t i;
    int      labels_ok = 1;
    int      branches_ok = 1;
    int      slots_ok = 1;
    int      modes = 0;
    uint32_t bytes;

    ut_section(r->name);
    ut_check(r->entry_count > 0 && r->pool_count > 0, "the record has entries and a pool");
    for (i = 0; i < r->entry_count; ++i) {
        int32_t target = (int32_t)i + r->entries[i].word1;

        if ((r->entries[i].word0 & OP_MASK) == OP_AI_MODE) {
            modes++;
        }
        if (target < 0 || (uint32_t)target > r->entry_count) {
            branches_ok = 0;
        }
    }
    snprintf(what, sizeof what, "every branch lands inside the record's %u entries",
             r->entry_count);
    ut_check(branches_ok, what);
    snprintf(what, sizeof what, "the %u state labels are the record's %d mode entries, in order",
             r->label_count, modes);
    for (i = 0; i < r->label_count; ++i) {
        uint16_t at = r->labels[i];

        if (at >= r->entry_count || (r->entries[at].word0 & OP_MASK) != OP_AI_MODE ||
            (i > 0 && at <= r->labels[i - 1])) {
            labels_ok = 0;
        }
    }
    ut_check(labels_ok && (int)r->label_count == modes, what);
    ut_check(r->label_count >= 3, "a die mode and a gone mode follow the behaviour's own");
    for (i = 0; i < r->slot_count; ++i) {
        const spawn_slot_t *s = &r->slots[i];

        if (s->in_entry) {
            if (s->index >= r->entry_count ||
                (r->entries[s->index].word0 & OP_MASK) != OP_MOVE_SPEED ||
                !(r->entries[s->index].word0 & INLINE_BIT)) {
                slots_ok = 0;
            }
        } else if (s->index >= r->pool_count) {
            slots_ok = 0;
        }
    }
    snprintf(what, sizeof what, "each of the %u slots names a pool word or an inline move speed",
             r->slot_count);
    ut_check(slots_ok, what);
    bytes = RECORD_HEAD + r->entry_count * 8u + r->pool_count * 4u + r->label_count * 2u;
    snprintf(what, sizeof what, "laid out it is %u bytes, within the %u each copy carries", bytes,
             SPAWN_SCRIPT_BYTES);
    ut_check(bytes <= SPAWN_SCRIPT_BYTES, what);
}

int main(void)
{
    uint32_t i;
    uint8_t  buffer[SPAWN_SCRIPT_BYTES];
    char     note[64];
    bool     shoots = true;

    for (i = 0; i < SPAWN_SCRIPT_COUNT; ++i) {
        check_record(&SPAWN_SCRIPTS[i]);
    }

    ut_section("the behaviours");
    ut_check(strcmp(spawn_behaviour_name(SPAWN_BEHAVIOUR_STAND), "Stand") == 0 &&
             strcmp(spawn_behaviour_name(SPAWN_BEHAVIOUR_FOLLOW), "Follow") == 0 &&
             strcmp(spawn_behaviour_name(SPAWN_BEHAVIOUR_ATTACK), "Attack") == 0 &&
             strcmp(spawn_behaviour_name(SPAWN_BEHAVIOUR_HELP), "Help") == 0,
             "the four behaviours are named as the panel lists them");
    ut_check(strcmp(spawn_behaviour_name(SPAWN_BEHAVIOUR_COUNT), "") == 0,
             "a behaviour past the end has no name");
    ut_check(spawn_script_flyer_mode("hovdroid.baf") == 2 &&
             spawn_script_flyer_mode("HOVDROID.BAF") == 2 &&
             spawn_script_flyer_mode("tusken.baf") == 0 && spawn_script_flyer_mode(NULL) == 0,
             "the hover cannon flies at mode 2, case aside; a walker and no name at 0");
    ut_check(spawn_script_model_weapon("drdfitr.baf") == 13 &&
             spawn_script_model_weapon("tusken.baf") == 0,
             "the droid fighter is armed with kind 13; an unarmed model answers 0");

    ut_section("without the archive");
    ut_check(spawn_script_prepare(SPAWN_BEHAVIOUR_STAND, "tusken.baf", false, buffer,
                                  sizeof buffer, note, sizeof note, &shoots) == NULL,
             "a record cannot be laid out where big.lab is not beside the host");
    ut_check(spawn_script_prepare(SPAWN_BEHAVIOUR_STAND, "tusken.baf", false, NULL, 0u, NULL,
                                  0u, NULL) == NULL,
             "no buffer is refused before anything else is asked");

    return ut_summary("spawn scripts");
}
