/* mp_bank.c: the digest the bank swap gate stands on, and the bank in a process with no game.
 *
 * The digest is the instrument that makes "the swap left no trace" a measurement instead of a
 * sentence, so the test pins the hash to known answers rather than to a second copy of its own
 * arithmetic, and then checks the two properties the gate actually uses: any changed byte changes
 * the value, and the same bytes in another order do too.
 *
 * The no-game half is the same claim every engine-facing module here makes: with nothing
 * resolved, every ask is a refusal and nothing dereferences anything.
 */
#include "unittest.h"

#include "mp_bank.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_digest(void)
{
    uint8_t  bytes[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint32_t whole;
    uint32_t changed;

    ut_section("the digest, pinned to known answers");

    ut_check(mp_bank_fnv1a(MP_BANK_FNV_SEED, "", 0) == 2166136261u,
             "no bytes leave the offset basis untouched");
    ut_check(mp_bank_fnv1a(MP_BANK_FNV_SEED, "a", 1) == 0xE40C292Cu,
             "one byte gives the published FNV-1a answer");
    ut_check(mp_bank_fnv1a(MP_BANK_FNV_SEED, "ab", 2) !=
             mp_bank_fnv1a(MP_BANK_FNV_SEED, "ba", 2),
             "the same bytes in another order digest differently");

    whole = mp_bank_fnv1a(MP_BANK_FNV_SEED, bytes, sizeof bytes);
    bytes[7] ^= 0x01u;
    changed = mp_bank_fnv1a(MP_BANK_FNV_SEED, bytes, sizeof bytes);
    ut_check(whole != changed, "flipping the last bit of the last byte changes the digest");

    ut_check(mp_bank_fnv1a(whole, bytes, 0) == whole,
             "chaining zero bytes carries the seed through unchanged");
}

/* The status writer's bounds are pure: a dword offset inside the 0x4C byte record, on a dword
 * boundary. The writer itself refuses with no bank, which is the only thing about it a process
 * with no game can show. */
static void check_status_writer(void)
{
    ut_section("the status writer's bounds");

    ut_check(mp_bank_status_offset_ok(0), "the health dword at offset 0 is inside the record");
    ut_check(mp_bank_status_offset_ok(4), "so is the next dword");
    ut_check(mp_bank_status_offset_ok(MP_BANK_STATUS_BYTES - 4u),
             "and the last dword of the record");
    ut_check(!mp_bank_status_offset_ok(MP_BANK_STATUS_BYTES),
             "the record's end is refused");
    ut_check(!mp_bank_status_offset_ok(MP_BANK_STATUS_BYTES - 3u),
             "a dword that would straddle the end is refused");
    ut_check(!mp_bank_status_offset_ok(1) && !mp_bank_status_offset_ok(2) &&
             !mp_bank_status_offset_ok(3),
             "an offset off the dword boundary is refused");
    ut_check(!mp_bank_status_offset_ok((size_t)0 - 4u),
             "an offset that wraps is refused rather than read as small");

    ut_check(!mp_bank_status_write_at(1u, 0, 100u),
             "with no bank the writer refuses and touches nothing");
    ut_check(!mp_bank_status_write_at(1u, MP_BANK_STATUS_BYTES, 0u),
             "and an offset past the record is refused before the bank is even asked");
}

static void check_without_a_game(void)
{
    uint32_t digest = 0;

    ut_section("in a process with no game");

    ut_check(!mp_bank_install(), "with no cells resolved the install refuses");
    ut_check(!mp_bank_ready(), "and the bank does not claim readiness");
    ut_check(!mp_bank_digest(&digest), "a digest of nothing is refused, not zero");
    ut_check(!mp_bank_swap_in(), "a swap in without an install is refused");
    ut_check(!mp_bank_swap_out(), "so is a swap out");

    mp_bank_provoke_tick();
    ut_check(!mp_bank_ready(), "a provocation tick without an install skips and changes nothing");

    ut_check(!mp_bank_lend_block_begin(),
             "the block loan refuses while nothing is installed or swapped");
    mp_bank_lend_block_end(false);
    ut_check(!mp_bank_is_swapped(), "and a refused loan leaves no state behind");
}

/* The bank is a field of three. The two rules that decide which index is a bank and which
 * class its body carries are pure, and the names that meant "the second body" have to keep
 * meaning bank 1, or every caller that still says so would silently address nothing. */
static void check_the_field_of_banks(void)
{
    uint8_t byte = 0;

    ut_section("three far banks, addressed by index");

    ut_check(MP_BANK_FAR_MAX == 3u, "four players are three far bodies");
    ut_check(!mp_bank_index_ok(0u), "bank 0 is the engine's own player and never a bank of ours");
    ut_check(mp_bank_index_ok(1u) && mp_bank_index_ok(2u) && mp_bank_index_ok(3u),
             "one to three are banks");
    ut_check(!mp_bank_index_ok(4u), "and four is not");

    ut_check(mp_bank_class_of(0u) == 1, "bank 0's body carries the retail class 1");
    ut_check(mp_bank_class_of(1u) == 5 && mp_bank_class_of(2u) == 6 && mp_bank_class_of(3u) == 7,
             "the far banks carry 5, 6 and 7, which no level's actor records use");
    ut_check(mp_bank_class_of(4u) == 1, "an index that is no bank answers the retail class");
    ut_check(mp_bank_active() == 0u, "with nothing installed no bank is active");
    ut_check(mp_bank_active_class() == 1, "so the active class is the retail one");

    ut_check(!mp_bank_swap_in_at(2u), "a swap into bank 2 without an install is refused");
    ut_check(!mp_bank_swap_in_at(0u) && !mp_bank_swap_in_at(4u),
             "and so is one into an index that is no bank");
    ut_check(!mp_bank_read_at(1u, 0u, &byte, 1u), "a read of bank 1 without an install is refused");
    ut_check(!mp_bank_read_at(4u, 0u, &byte, 1u), "and a read of an index that is no bank");
    ut_check(!mp_bank_status_write_at(3u, 0u, 100u), "a status write into bank 3 is refused too");
    ut_check(!mp_bank_run_at(1u, NULL), "and a tick with no runner");

    ut_check(!mp_bank_read_at(1u, 0u, &byte, 1u) && !mp_bank_second_write(0u, &byte, 1u) &&
                 mp_bank_second_health() == 0 && !mp_bank_run_second(NULL),
             "the names that mean bank 1 refuse the same way without a game");
}

int main(void)
{
    check_digest();
    check_status_writer();
    check_without_a_game();
    check_the_field_of_banks();

    return ut_summary("mp_bank");
}
