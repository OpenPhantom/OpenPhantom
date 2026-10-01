/* mp_own_body.c: where this machine's own body stands, one answer for the send and for the voice.
 *
 * The hero block and the player's object are played in memory, the block where the engine keeps
 * this player and the object at the world's origin, which is where a body the re-entry has just
 * spawned stands until its first pose commit. The module state runs through all five values the
 * engine writes, and in each the two readers are asked: the send, which takes the model when the
 * model answers and the block otherwise, and the judgement of a spoken line.
 *
 * What would be silent if it were wrong: a line judged at the world's origin while its speaker's
 * player dies or comes back, shown or held back or kept alive at no volume for the wrong reason,
 * while the far side is told the block.
 */
#include "unittest.h"

#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_own_body.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define OBJECT_POSITION 0x18u

static struct {
    uint8_t block[0x400];
    uint8_t object[0x60];
    size_t  bank;
} eng;

uintptr_t mp_cells_address(mp_cell_t cell)
{
    return cell == MP_CELL_HERO_BLOCK ? (uintptr_t)eng.block : 0u;
}

bool mp_cells_hero_position(float out[3])
{
    memcpy(out, eng.block + MP_HERO_BLOCK_POS, 3u * sizeof(float));
    return true;
}

size_t mp_bank_active(void)
{
    return eng.bank;
}

static void put_u32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

/* This player in module state `state`: the block at x = 100, the model at the origin. */
static void stand(uint32_t state)
{
    const float block[3] = { 100.0f, 2.0f, 3.0f };

    memset(&eng, 0, sizeof eng);
    memcpy(eng.block + MP_HERO_BLOCK_POS, block, sizeof block);
    put_u32(eng.block + MP_HERO_BLOCK_MODULE_STATE, state);
    put_u32(eng.block + MP_HERO_BLOCK_OBJECT, (uint32_t)(uintptr_t)eng.object);
}

/* What the send puts on the wire: the model when the model answers, the block otherwise. */
static float sent_x(void)
{
    float    position[3] = { 0.0f, 0.0f, 0.0f };
    float    heading     = 0.0f;
    uint32_t state       = 0u;

    if (mp_own_body_model(position, &heading, &state) == MP_OWN_MODEL_READ) {
        return position[0];
    }
    return 100.0f;
}

static void check_every_state(void)
{
    static const char *const NAMES[MP_HERO_MODULE_STATES] = { "idle", "running", "quitting",
                                                              "dying", "respawning" };
    uint32_t state;

    ut_section("the send and a spoken line measure this body at the same place in every state");
    for (state = 0u; state < MP_HERO_MODULE_STATES; ++state) {
        float judged[3] = { -1.0f, -1.0f, -1.0f };
        bool  model     = false;
        bool  known;

        stand(state);
        known = mp_own_body_place(judged, &model);
        ut_checkf(known && judged[0] == sent_x() && model == (sent_x() == 0.0f),
                  "%s: the line is judged where the far side is told this body stands (%.0f, "
                  "sent %.0f)", NAMES[state], (double)judged[0], (double)sent_x());
    }

    stand(3u);
    ut_check(sent_x() == 100.0f, "dying, the block: the engine moves nothing then");
    stand(4u);
    ut_check(sent_x() == 100.0f,
             "respawning, the block: the new body stands at the world's origin until its first "
             "pose commit");
    stand(2u);
    ut_check(sent_x() == 0.0f, "held for anything else, the model, which a script drives");
    stand(1u);
    ut_check(sent_x() == 100.0f, "running, the block, which the player module writes");

    stand(2u);
    eng.bank = 1u;
    {
        float out[3];

        ut_check(!mp_own_body_place(NULL, NULL) && !mp_own_body_place(out, NULL),
                 "inside a bank window the block is a far body's, and nothing is answered");
    }
}

int main(void)
{
    check_every_state();
    return ut_summary("where this machine's own body stands");
}
