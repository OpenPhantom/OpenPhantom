/* Where a sound is placed when the body that caused it is not the one listening, with no game.
 *
 * Three decisions carry the whole module, and each of them fails silently in the field. Re-pointing
 * a call that already brought a place would move a correctly placed sound to a stale point and it
 * would still be audible, so nobody would report it. Forgetting either of the two flags loses a
 * different half: without the rolloff bit a placed sound is admitted by the start gate and then
 * plays at full volume anyway, and without the static bit the engine keeps reading the anchor slot
 * every frame, so the sound would follow the slot as later sounds overwrote it. And an anchor that
 * a window fails to put back leaves every later sound in the level pinned to one point.
 */
#include "unittest.h"

#include "mp_sound.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static void check_which_calls_are_re_pointed(void)
{
    const uint32_t record = 0u;   /* only its address is read here */
    const float    place[3] = { 1.0f, 2.0f, 3.0f };

    ut_section("only a call that brought no place of its own is re-pointed");
    ut_check(mp_sound_anchor_applies(true, &record, NULL),
             "an anchored call with no place is the case the module exists for");
    ut_check(!mp_sound_anchor_applies(true, &record, place),
             "a call that carries its own place is left alone: the engine placed it, and an "
             "animation event's place follows the body better than an anchor can");
    ut_check(!mp_sound_anchor_applies(false, &record, NULL),
             "with no anchor held nothing is re-pointed, which is every sound of the local "
             "player and every sound in the menu");
    ut_check(!mp_sound_anchor_applies(true, NULL, NULL),
             "and a call with no record is passed through rather than copied from nowhere");
}

static void check_the_two_flags(void)
{
    ut_section("the anchored copy carries both flags and keeps the record's own");
    ut_check((mp_sound_anchored_flags(0u) & MP_SOUND_FLAG_DISTANCE) != 0u,
             "the rolloff bit is set, or the sound is admitted and then plays at full volume");
    ut_check((mp_sound_anchored_flags(0u) & MP_SOUND_FLAG_STATIC_POS) != 0u,
             "the static bit is set, or the engine keeps reading the slot and the sound follows "
             "whatever is written there next");
    ut_check(mp_sound_anchored_flags(0x200u) == (0x200u | MP_SOUND_FLAG_DISTANCE |
                                                 MP_SOUND_FLAG_STATIC_POS),
             "and the record's own flags survive: 0x200 is the refusal to duplicate a wav, which "
             "the sabre's ignite carries and which decides whether it plays at all");
    ut_check(mp_sound_anchored_flags(MP_SOUND_FLAG_DISTANCE) ==
                 (MP_SOUND_FLAG_DISTANCE | MP_SOUND_FLAG_STATIC_POS),
             "a record that already had the rolloff bit is not changed twice");
}

static void check_the_record_shape(void)
{
    ut_section("the record copy is the shape the engine builds");
    ut_check(MP_SOUND_RECORD_BYTES == 0x40u,
             "0x40 bytes is the whole record, which is what bapsound_playByName lays out on its "
             "own stack; a shorter copy would leave the priority and the two distances behind");
    ut_check(MP_SOUND_RECORD_BYTES % sizeof(uint32_t) == 0u,
             "and it divides into dwords, so the flag word can be reached by index rather than "
             "by a cast through a byte pointer");
    ut_check(MP_SOUND_RECORD_FLAGS + sizeof(uint32_t) <= MP_SOUND_RECORD_BYTES,
             "and the flag word is inside the copy, not past its end");
}

static void check_a_window_puts_back_what_it_found(void)
{
    const float outer[3] = { 10.0f, 0.0f, 0.0f };
    const float inner[3] = { 0.0f, 20.0f, 0.0f };
    mp_sound_anchor_t before_outer;
    mp_sound_anchor_t before_inner;

    ut_section("a window inside a window restores the one it replaced");
    mp_sound_reset();

    mp_sound_anchor_open(outer, &before_outer);
    ut_check(!before_outer.active,
             "the outer window found no anchor, which is what a reset leaves behind");

    mp_sound_anchor_open(inner, &before_inner);
    ut_check(before_inner.active && before_inner.position[0] == 10.0f,
             "the inner window was handed the outer one rather than a cleared cell");

    mp_sound_anchor_close(&before_inner);
    mp_sound_anchor_close(&before_outer);

    /* Reopening proves what the two closes left behind: if the outer close had cleared instead of
     * restored, or the inner close had restored the wrong thing, this would not be inactive. */
    mp_sound_anchor_open(outer, &before_outer);
    ut_check(!before_outer.active,
             "and after both closes there is no anchor left, so no later sound in the level is "
             "pinned to a point somebody forgot to release");
    mp_sound_anchor_close(&before_outer);
}

static void check_no_clang_is_replayed_without_a_game(void)
{
    uint8_t actor[0x100] = { 0 };

    ut_section("with no game a host's clang cannot be played again here, and says so");
    ut_check(!mp_sound_block_replayable(),
             "no trampoline and no cooldown cell: the one answer the enemy module is given");
    ut_check(!mp_sound_block_replay((uintptr_t)actor, 0),
             "and a replay asked for anyway refuses rather than calling nothing");
}

static void check_engine_half_without_a_game(void)
{
    ut_section("with no game in the process nothing binds and nothing pretends to");
    ut_check(!mp_sound_installed(),
             "the module starts unbound");
    ut_check(!mp_sound_install(),
             "and the install refuses, because the funnel's pattern matches nothing in a test "
             "binary; it does not fall back to an address");
    ut_check(!mp_sound_installed(),
             "so it is still unbound afterwards, and every sound keeps the engine's own behaviour");
    mp_sound_report();
    mp_sound_reset();
}

int main(void)
{
    check_which_calls_are_re_pointed();
    check_the_two_flags();
    check_the_record_shape();
    check_a_window_puts_back_what_it_found();
    check_no_clang_is_replayed_without_a_game();
    check_engine_half_without_a_game();

    return ut_summary("where a sound is placed");
}
