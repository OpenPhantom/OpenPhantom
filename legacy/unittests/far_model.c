/* far_model.c: the far bodies' answer, through the real notes, in a process with no engine.
 *
 * Both records are filed through the shared note and read back in the same process, which is the
 * situation they serve: the multiplayer and the overlay are two DLLs in one process. Nothing of the
 * swap resolves here, so no body can be dressed; what is held still is everything around the
 * dressing: that the overlay says it listens from load on, that an empty bank is answered as empty,
 * that a bank asking for a model is answered for the body serial it asked with, that a block the
 * contract does not allow is refused, and that an answer is published once and not again every
 * scene end.
 */
#include "unittest.h"

#include "far_model.h"

#include "common/model_wear_note.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_BYTES 0x3ACu

/* On the heap and not in the program's own data: a block inside the executable's image is one the
 * contract refuses, and a test's static data is inside the test's image. */
static uint8_t *block_a;
static uint8_t *block_b;

static void put_u32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

static void set_bank(model_wear_want_record_t *want, uint32_t b, uint32_t serial,
                     uint32_t object, uint8_t *block, uint8_t slot, const char *model)
{
    model_wear_want_bank_t *bank = &want->bank[b];

    memset(bank, 0, sizeof *bank);
    bank->serial = serial;
    bank->object = object;
    bank->block = (uint32_t)(uintptr_t)block;
    bank->slot = slot;
    if (model != NULL) {
        memcpy(bank->model, model, strlen(model));
    }
}

int main(void)
{
    model_wear_want_record_t want;
    model_wear_done_record_t done;
    uint32_t                 serial = 0;
    uint32_t                 later = 0;

    block_a = (uint8_t *)calloc(1u, BLOCK_BYTES);
    block_b = (uint8_t *)calloc(1u, BLOCK_BYTES);
    if (block_a == NULL || block_b == NULL) {
        return 1;
    }

    ut_section("the overlay says it listens from load on");
    far_model_install();
    ut_check(model_wear_read_done(&done, &serial), "the answer record is there before any level");
    ut_check((done.ready & MODEL_WEAR_READY_LISTENING) != 0u, "and it says somebody listens");
    ut_check((done.ready & MODEL_WEAR_READY_ABLE) == 0u,
             "but not that a body can be dressed, with nothing of the swap resolved");
    ut_check(done.bank[0].state == MODEL_WEAR_STATE_NONE, "and it answers nothing yet");

    ut_section("a scene end with no wish changes nothing");
    far_model_tick(1u);
    ut_check(model_wear_read_done(&done, &later) && later == serial,
             "the same answer is not filed again");

    ut_section("a bank that asks is answered for the body it asked with");
    memset(&want, 0, sizeof want);
    put_u32(block_a + 0x0Cu, 0x5000u);
    put_u32(block_a + 0x6Cu, 2u);
    set_bank(&want, 0u, 7u, 0x5000u, block_a, 2u, "anakin.baf");
    set_bank(&want, 1u, 3u, 0x6000u, block_b, 1u, NULL);
    ut_check(model_wear_publish_want(&want), "the multiplayer's wish is filed");
    far_model_tick(1u);
    ut_check(model_wear_read_done(&done, &serial), "and answered");
    ut_check(done.bank[0].serial == 7u, "the answer names the serial it answers");
    ut_checkf(done.bank[0].state == MODEL_WEAR_STATE_REFUSED &&
              done.bank[0].reason == MODEL_WEAR_REASON_NOT_ABLE,
              "with nothing resolved the model is refused as not able (%u, %u)",
              (unsigned)done.bank[0].state, (unsigned)done.bank[0].reason);
    ut_check(done.bank[0].model[0] == '\0', "and a refusal for any reason but another model "
                                            "echoes nothing");
    ut_check(done.bank[1].serial == 3u && done.bank[1].state == MODEL_WEAR_STATE_NONE,
             "a bank with a body and no model is answered as nothing asked");
    ut_check(done.bank[2].state == MODEL_WEAR_STATE_NONE, "and a bank with no body as well");

    far_model_tick(1u);
    ut_check(model_wear_read_done(&done, &later) && later == serial,
             "the answer holds and is not filed again at the next scene end");

    ut_section("a block the contract does not allow is refused before anything else is asked");
    put_u32(block_a + 0x6Cu, 3u);
    set_bank(&want, 0u, 8u, 0x5000u, block_a, 2u, "anakin.baf");
    ut_check(model_wear_publish_want(&want),
             "a rebuilt body on a block whose hero is not its slot");
    far_model_tick(1u);
    ut_check(model_wear_read_done(&done, &serial), "answered");
    ut_checkf(done.bank[0].serial == 8u && done.bank[0].state == MODEL_WEAR_STATE_REFUSED &&
              done.bank[0].reason == MODEL_WEAR_REASON_BAD_BLOCK,
              "the new serial, refused for its block (%u, %u)", (unsigned)done.bank[0].state,
              (unsigned)done.bank[0].reason);

    ut_section("a model is one name whatever its case");
    put_u32(block_a + 0x6Cu, 2u);
    set_bank(&want, 0u, 8u, 0x5000u, block_a, 2u, "ANAKIN.BAF");
    ut_check(model_wear_publish_want(&want),
             "the same body asks for ANAKIN.BAF, on a block that would pass now");
    far_model_tick(1u);
    ut_check(model_wear_read_done(&done, &later) && later == serial &&
                 done.bank[0].serial == 8u && done.bank[0].reason == MODEL_WEAR_REASON_BAD_BLOCK,
             "and nothing new is answered: the answer given for anakin.baf is the answer for "
             "ANAKIN.BAF, as the multiplayer holds an echo to its wish without case");

    put_u32(block_b + 0x6Cu, 2u);
    set_bank(&want, 0u, 9u, 0x5000u, block_b, 2u, "anakin.baf");
    ut_check(model_wear_publish_want(&want), "the bank names a second block");
    far_model_tick(2u);
    ut_check(model_wear_read_done(&done, &serial), "answered");
    ut_check(done.bank[0].state == MODEL_WEAR_STATE_REFUSED &&
             done.bank[0].reason == MODEL_WEAR_REASON_BAD_BLOCK,
             "a bank's block is the multiplayer's for the process: a second one is refused");
    ut_check(model_wear_done_is_sound(&done), "and every answer filed is one the note accepts");

    free(block_a);
    free(block_b);
    return ut_summary("the far bodies' answer");
}
