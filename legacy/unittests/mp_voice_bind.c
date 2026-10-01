/* unittests/mp_voice_bind.c: every reading of the voice's bindings is named twice, and a window
 * whose two names disagree is refused.
 *
 * mp_voice_bind takes the addresses it needs out of the voice's own table, and that table is empty
 * until it has been resolved once. The binding of the reply resolves the table itself, so it has to
 * read after that and not before: the first build read both addresses first, and bound only
 * because the binding of the rule happened to resolve the table a moment earlier. Here the reply
 * is bound first and alone, against windows of this test's own.
 *
 * The hearing radius, the engine's answer and the priority are bound against windows laid out as
 * the engine's code lays them out, byte for byte at every offset the binding reads, with cells in
 * this test's memory. Each is taken to pieces once for every pair of names it checks.
 */
#include "unittest.h"

#include "mp_signatures_dialog.h"
#include "mp_signatures_voice.h"
#include "mp_voice_bind.h"

#include "common/host_image.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CALL_BYTES  5u
#define BODY_OFFSET 0x18u

/* Each entry does something of its own, or the linker folds identical bodies into one address. */
static volatile uint32_t entered;

static void __cdecl another_function(void)
{
    ++entered;
}

/* The windows, as far as the binding reads them. The voice's pattern and the hear pattern behind
 * it are one window, as they are one function; so are the setter, its switch table and its arms. */
static uint8_t calls[0x48];
static uint8_t voice_fn[0x80];
static uint8_t setter_fn[0x80];
static uint8_t reset_fn[0x40];
static uint8_t search_fn[0xA4];
static uint8_t free_fn[0x70];
static uint8_t option_fn[0x24];
static uint8_t latch_fn[0x20];
static uint8_t render_fn[0x2D];

static uint8_t  bank[12u * 0x80u];
static int32_t  voices_cell;
static int32_t  latch_cell;
static int32_t  bark_cell;
static int32_t  priority_cell;
static float    volume_cell;
static uint32_t stray_cell;

#define SETTER_TABLE 0x60u
#define SETTER_ARM   0x4Fu
#define HEAR_AT      (voice_fn + MP_VOICE_HEAR_BEHIND_PLAY)

static bool   resolved;
static size_t resolves;

static void write_call(uint8_t *site, const void *target)
{
    uint32_t displacement = (uint32_t)((uintptr_t)target - ((uintptr_t)site + CALL_BYTES));

    site[0] = 0xE8u;
    memcpy(site + 1, &displacement, sizeof displacement);
}

static void put_u32(uint8_t *window, size_t at, uint32_t value)
{
    memcpy(window + at, &value, sizeof value);
}

static void put_cell(uint8_t *window, size_t at, const void *cell)
{
    put_u32(window, at, (uint32_t)(uintptr_t)cell);
}

/* ---- what the binding asks for -------------------------------------------------------------- */

static uint8_t *hear_window = NULL;

size_t mp_signatures_voice_resolve(void)
{
    resolved = true;
    ++resolves;
    return MP_VOICE_SITE_COUNT;
}

/* Nothing is known before the table is resolved, as in the real table. */
uintptr_t mp_signatures_voice_address(mp_voice_site_t site)
{
    if (!resolved) {
        return 0u;
    }
    switch (site) {
    case MP_VOICE_SITE_VOICE_CALLS:    return (uintptr_t)calls;
    case MP_VOICE_SITE_PLAY:           return (uintptr_t)voice_fn;
    case MP_VOICE_SITE_HEAR:           return (uintptr_t)hear_window;
    case MP_VOICE_SITE_SET_FIELD:      return (uintptr_t)setter_fn;
    case MP_VOICE_SITE_BY_NAME_RESET:  return (uintptr_t)reset_fn;
    case MP_VOICE_SITE_CHANNEL_SEARCH: return (uintptr_t)search_fn;
    case MP_VOICE_SITE_FREE_CHANNEL:   return (uintptr_t)free_fn;
    case MP_VOICE_SITE_OPTION:         return (uintptr_t)option_fn;
    case MP_VOICE_SITE_LATCH:          return (uintptr_t)latch_fn;
    case MP_VOICE_SITE_RENDER_VOICES:  return (uintptr_t)render_fn;
    default:                           return 0u;
    }
}

size_t mp_signatures_voice_prologue(mp_voice_site_t site)
{
    (void)site;
    return 0u;
}

const signature_t *mp_signatures_voice_site(mp_voice_site_t site)
{
    (void)site;
    return NULL;
}

const signature_t *mp_signatures_dialog_site(mp_dialog_site_t site)
{
    (void)site;
    return NULL;
}

/* ---- the windows, as the engine lays them out ------------------------------------------------ */

static mp_voice_cells_t rule;

static void lay_the_windows(void)
{
    uint32_t free_bits  = 0x40800000u;   /* 4.0f */
    uint32_t scene_bits = 0x41000000u;   /* 8.0f */

    memset(&rule, 0, sizeof rule);
    rule.reach       = 100.0f;
    rule.set_field   = (uintptr_t)setter_fn;
    rule.volume_cell = (uintptr_t)&volume_cell;
    rule.bark_cell   = (uintptr_t)&bark_cell;
    hear_window      = HEAR_AT;

    /* push 5; call; ...; push 8.0f; push 3; call the setter; ...; push 4.0f; push 3; call. */
    HEAR_AT[MP_VOICE_HEAR_LOCK_PUSH]      = 0x6Au;
    HEAR_AT[MP_VOICE_HEAR_LOCK_PUSH + 1u] = 5u;
    HEAR_AT[MP_VOICE_HEAR_SCENE_PUSH]     = 0x68u;
    put_u32(HEAR_AT, MP_VOICE_HEAR_SCENE_PUSH + 1u, scene_bits);
    HEAR_AT[MP_VOICE_HEAR_FREE_PUSH] = 0x68u;
    put_u32(HEAR_AT, MP_VOICE_HEAR_FREE_PUSH + 1u, free_bits);
    write_call(HEAR_AT + MP_VOICE_HEAR_SCENE_SET_CALL, setter_fn);
    write_call(HEAR_AT + MP_VOICE_HEAR_FREE_SET_CALL, setter_fn);

    /* The setter's table, its sixth entry the arm of field 5: fld [ebp+0xC], call, mov [cell],
     * eax. */
    put_cell(setter_fn, MP_VOICE_SET_FIELD_TABLE, setter_fn + SETTER_TABLE);
    put_cell(setter_fn, SETTER_TABLE + MP_VOICE_SET_FIELD_PRIORITY * 4u, setter_fn + SETTER_ARM);
    setter_fn[SETTER_ARM]      = 0xD9u;
    setter_fn[SETTER_ARM + 1u] = 0x45u;
    setter_fn[SETTER_ARM + 2u] = 0x0Cu;
    write_call(setter_fn + SETTER_ARM + 3u, (const void *)&another_function);
    setter_fn[SETTER_ARM + MP_VOICE_PRIORITY_ARM_STORE] = 0xA3u;
    put_cell(setter_fn, SETTER_ARM + MP_VOICE_PRIORITY_ARM_STORE + 1u, &priority_cell);

    /* playByName's reset: field 0 first, field 5 last with its resting value. */
    put_cell(reset_fn, MP_VOICE_RESET_VOLUME_CELL, &volume_cell);
    reset_fn[MP_VOICE_RESET_PRIORITY_STORE] = 0xC7u;
    put_cell(reset_fn, MP_VOICE_RESET_PRIORITY_CELL, &priority_cell);
    put_u32(reset_fn, MP_VOICE_RESET_PRIORITY_VALUE, 90u);

    /* The option and the latch, each named twice, and the render's test of the bark. */
    put_cell(option_fn, MP_VOICE_OPTION_CELL, &voices_cell);
    put_cell(render_fn, MP_VOICE_RENDER_VOICES_CELL, &voices_cell);
    put_cell(render_fn, MP_VOICE_RENDER_BARK_CELL, &bark_cell);
    put_cell(latch_fn, MP_VOICE_LATCH_COMPARE, &latch_cell);
    put_cell(latch_fn, MP_VOICE_LATCH_STORE, &latch_cell);

    /* The bank: twelve channels, flags at +0x10, the priority at +0x70, the owner at +0x78. */
    search_fn[MP_VOICE_SEARCH_COUNT] = 12u;
    put_cell(search_fn, MP_VOICE_SEARCH_FLAGS_CELL, bank + 0x10u);
    put_u32(search_fn, MP_VOICE_SEARCH_FREE_BIT, 0x80000u);
    put_cell(search_fn, MP_VOICE_SEARCH_PRIORITY, bank + 0x70u);
    put_cell(search_fn, MP_VOICE_SEARCH_PRIORITY_LOAD, bank + 0x70u);
    put_cell(free_fn, MP_VOICE_FREE_BANK, bank);
    free_fn[MP_VOICE_FREE_FLAGS_AT] = 0x10u;
    put_u32(free_fn, MP_VOICE_FREE_PLAYING_BIT, 0x20000u);
    free_fn[MP_VOICE_FREE_OWNER_TEST]  = 0x78u;
    free_fn[MP_VOICE_FREE_OWNER_LOAD]  = 0x78u;
    free_fn[MP_VOICE_FREE_OWNER_CLEAR] = 0x78u;
    free_fn[MP_VOICE_FREE_REF_AT]      = 0x0Cu;
}

/* ============================================================================================== */

static void check_the_reply_binds_on_its_own(void)
{
    mp_voice_reply_site_t site;

    ut_section("the reply binds first and alone, reading only what it resolved");

    write_call(calls + MP_VOICE_CALLS_REPLY, voice_fn);
    write_call(calls + MP_VOICE_CALLS_BARK, voice_fn);
    calls[MP_VOICE_CALLS_PLACE_OFFSET] = (uint8_t)BODY_OFFSET;

    ut_check(!resolved, "nothing has resolved the voice's sites before the reply is bound");
    ut_check(mp_voice_bind_reply(&site) == NULL, "the reply binds all the same");
    ut_check(resolves == 1u, "because it resolved them itself, once");
    ut_check(site.call == (uintptr_t)calls + MP_VOICE_CALLS_REPLY,
             "the call it repoints is the reply's");
    ut_check(site.voice == (uintptr_t)voice_fn, "and it calls the voice");
    ut_check(site.body_offset == BODY_OFFSET, "the place inside a body is read, 0x18");

    ut_section("and refuses a window that is not the one it was written for");

    write_call(calls + MP_VOICE_CALLS_BARK, (const void *)&another_function);
    ut_check(mp_voice_bind_reply(&site) != NULL && site.call == 0u,
             "a reply and a bark that call two functions are no voice's two calls");
    write_call(calls + MP_VOICE_CALLS_BARK, voice_fn);
    calls[MP_VOICE_CALLS_PLACE_OFFSET] = 0u;
    ut_check(mp_voice_bind_reply(&site) != NULL, "a place of nought inside a body is no place");
    calls[MP_VOICE_CALLS_PLACE_OFFSET] = (uint8_t)BODY_OFFSET;
    ut_check(mp_voice_bind_reply(&site) == NULL, "put right, it binds again");
}

static const char *radii(mp_voice_hear_cells_t *h, mp_voice_hearing_t *hearing, float factor)
{
    bool as_given = false;

    return mp_voice_bind_radii(&rule, factor, h, hearing, &as_given);
}

static void check_the_hearing(void)
{
    mp_voice_hear_cells_t h;
    mp_voice_hearing_t    hearing;

    ut_section("the lock and the two radii are read right behind the voice's own pushes");

    lay_the_windows();
    ut_check(radii(&h, &hearing, 4.0f) == NULL, "the hearing binds");
    ut_check(h.min_free_bits == 0x40800000u && h.min_scene_bits == 0x41000000u && h.lock_level == 5,
             "4.0 outside a scene's lock, 8.0 under it, and the lock level 5");
    ut_check(h.free_at == (uintptr_t)(HEAR_AT + MP_VOICE_HEAR_FREE_PUSH) &&
                 h.scene_at == (uintptr_t)(HEAR_AT + MP_VOICE_HEAR_SCENE_PUSH) &&
                 h.lock_at == (uintptr_t)HEAR_AT,
             "and it says where it read each of them");
    ut_check(hearing.free == 16.0f && hearing.scene == 32.0f && hearing.admit == 100.0f,
             "made radii by the factor: sixteen units, thirty two under a lock, the admission 100");
    ut_check(radii(&h, &hearing, 0.0f) == NULL && hearing.free == 16.0f,
             "a factor of nought from the ini is no factor in the range: the default, sixteen "
             "units");

    ut_section("and refuses pushes that are not the voice's, keeping the admission as the radius");

    hear_window = HEAR_AT + 2u;
    ut_check(radii(&h, &hearing, 4.0f) != NULL && hearing.free == 100.0f && hearing.scene == 100.0f,
             "a hear pattern found anywhere but right behind the voice's own is refused");
    lay_the_windows();
    write_call(HEAR_AT + MP_VOICE_HEAR_FREE_SET_CALL, (const void *)&another_function);
    ut_check(radii(&h, &hearing, 4.0f) != NULL,
             "a radius handed to anything but the voice's setter is refused");
    lay_the_windows();
    HEAR_AT[MP_VOICE_HEAR_SCENE_PUSH] = 0x6Au;
    ut_check(radii(&h, &hearing, 4.0f) != NULL, "a push that is no push of a float is refused");
    lay_the_windows();
    HEAR_AT[MP_VOICE_HEAR_LOCK_PUSH + 1u] = 4u;
    ut_check(radii(&h, &hearing, 4.0f) != NULL && hearing.free == 100.0f,
             "a lock level other than the scenes' is refused, and the admission stays the radius");
}

static void check_the_answer(void)
{
    mp_voice_answer_cells_t a;

    ut_section("the option, the latch and the bank are each named twice");

    lay_the_windows();
    ut_check(mp_voice_bind_answer(&rule, &a) == NULL, "the engine's answer binds");
    ut_check(a.voices_cell == (uintptr_t)&voices_cell && a.latch_cell == (uintptr_t)&latch_cell,
             "the voice option and the latch");
    ut_check(a.bank.base == (uintptr_t)bank && a.bank.count == 12u && a.bank.stride == 0x80u,
             "twelve channels of 0x80 bytes from the bank");
    ut_check(a.bank.flags_at == 0x10u && a.bank.free_bit == 0x80000u &&
                 a.bank.playing_bit == 0x20000u && a.bank.priority_at == 0x70u &&
                 a.bank.owner_at == 0x78u && a.bank.ref_at == 0x0Cu,
             "the flags, the two bits, the priority, the owner and the reference");

    ut_section("and a window whose two names disagree is refused");

    put_cell(render_fn, MP_VOICE_RENDER_VOICES_CELL, &stray_cell);
    ut_check(mp_voice_bind_answer(&rule, &a) != NULL, "two voice options are none");
    lay_the_windows();
    put_cell(render_fn, MP_VOICE_RENDER_BARK_CELL, &stray_cell);
    ut_check(mp_voice_bind_answer(&rule, &a) != NULL,
             "a render that tests another channel than the rule's is refused");
    lay_the_windows();
    put_cell(latch_fn, MP_VOICE_LATCH_STORE, &stray_cell);
    ut_check(mp_voice_bind_answer(&rule, &a) != NULL, "a latch compared and set in two cells");
    lay_the_windows();
    put_cell(search_fn, MP_VOICE_SEARCH_FLAGS_CELL, bank + 0x14u);
    ut_check(mp_voice_bind_answer(&rule, &a) != NULL,
             "flags the search reads elsewhere than the freeing does");
    lay_the_windows();
    put_cell(search_fn, MP_VOICE_SEARCH_PRIORITY_LOAD, bank + 0x74u);
    ut_check(mp_voice_bind_answer(&rule, &a) != NULL, "a priority named twice in two places");
    lay_the_windows();
    free_fn[MP_VOICE_FREE_OWNER_CLEAR] = 0x7Cu;
    ut_check(mp_voice_bind_answer(&rule, &a) != NULL, "an owner the freeing clears elsewhere");
    lay_the_windows();
    search_fn[MP_VOICE_SEARCH_COUNT] = 0u;
    ut_check(mp_voice_bind_answer(&rule, &a) != NULL, "a bank of no channels");
    lay_the_windows();
    put_u32(free_fn, MP_VOICE_FREE_PLAYING_BIT, 0x80000u);
    ut_check(mp_voice_bind_answer(&rule, &a) != NULL, "a playing bit that is the free bit");
    lay_the_windows();
    ut_check(mp_voice_bind_answer(&rule, &a) == NULL, "put right, it binds again");
}

static void check_the_priority(void)
{
    mp_voice_priority_cells_t p;

    ut_section("field 5 is the cell the setter stores and playByName writes back");

    lay_the_windows();
    ut_check(mp_voice_bind_priority(&rule, &p) == NULL, "the priority binds");
    ut_check(p.cell == (uintptr_t)&priority_cell && p.resting == 90,
             "its cell, and the resting value 90 the reset writes");

    ut_section("and refuses a setter or a reset that is not the voice's");

    setter_fn[SETTER_ARM] = 0x8Bu;
    ut_check(mp_voice_bind_priority(&rule, &p) != NULL,
             "an arm of field 5 that is no load, conversion and store");
    lay_the_windows();
    put_cell(reset_fn, MP_VOICE_RESET_PRIORITY_CELL, &stray_cell);
    ut_check(mp_voice_bind_priority(&rule, &p) != NULL,
             "a reset that writes back another cell than the setter stores");
    lay_the_windows();
    put_cell(reset_fn, MP_VOICE_RESET_VOLUME_CELL, &stray_cell);
    ut_check(mp_voice_bind_priority(&rule, &p) != NULL,
             "a reset whose field 0 is not the rule's field 0");
    lay_the_windows();
    put_u32(reset_fn, MP_VOICE_RESET_PRIORITY_VALUE, 101u);
    ut_check(mp_voice_bind_priority(&rule, &p) != NULL,
             "a resting priority no lower than the one a presented voice is handed");
    lay_the_windows();
    ut_check(mp_voice_bind_priority(&rule, &p) == NULL, "put right, it binds again");
}

int main(void)
{
    ut_check(host_image_resolve(), "this test's own image stands in for the engine's");
    check_the_reply_binds_on_its_own();
    check_the_hearing();
    check_the_answer();
    check_the_priority();
    return ut_summary("mp_voice_bind");
}
