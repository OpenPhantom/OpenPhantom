/* mp_signatures.c: the shape of the multiplayer site and cell tables, checked without a game.
 *
 * What a test can prove here is not where a site is. Only the real executables can answer
 * that. What it can prove is the set of properties that decide whether the table
 * is capable of being right, and every one of them fails quietly:
 *
 *   * a pattern that requires an absolute address is a pattern for one build and one load order.
 *     It resolves on the developer's machine and stops resolving the day another module relocates
 *     what the operand names. This is the rule, and checking it mechanically is the main reason
 *     this file exists;
 *   * an operand a caller means to READ has to be a wildcard in its own pattern, or the pattern
 *     pins the very value the operand exists to report, and the read can never disagree with it;
 *   * a detour prologue shorter than five bytes does not cover the branch that will be written
 *     over it, and one that reaches past the pattern leaves stage two with no tail at all;
 *   * a cell nothing names is a cell that will read as unknown for ever, in silence.
 *
 * The table lives in two files and is merged behind one enumeration, so the checks run over the
 * second table's own template as well as over the merged view, and the two are held against each
 * other row by row: a template out of step with the enumeration would resolve one site under
 * another's name.
 *
 * The last check drives the resolver itself in a process that has no game in it. Nothing can
 * resolve there, and the property worth pinning is that the tables say so and hand back zero
 * rather than an address they made up.
 *
 * SIZE NOTE: over 600 lines, one section per table. The push block table's section is the newest
 * and the first to leave, into a test of its own, when this one grows.
 */
#include "unittest.h"

#include "mp_world_values.h"

#include "mp_cells.h"
#include "mp_signatures.h"
#include "mp_signatures_crate.h"
#include "mp_signatures_puppet.h"

#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The engine's image runs from 0x00400000 and is well under 0x00900000 in every build that ships.
 * A four byte window inside a pattern that reads as a value in that range is an address, not an
 * immediate, unless something says otherwise; the four windows named below say otherwise. */
#define IMAGE_LOW  0x00400000u
#define IMAGE_HIGH 0x00900000u

/* The scan reads a dword at every offset, so some of what it finds is not an operand at all but
 * the middle of an instruction. Four windows in this table read as an address by coincidence and
 * are named here rather than being wildcarded, because wildcarding them would throw away bytes the
 * pattern needs.
 *
 * Adding a row here is a decision, not a formality: the question to answer first is whether the
 * bytes really are an instruction rather than an operand. Everything else belongs in the mask. */
static const struct {
    const char *site;
    size_t      offset;
    const char *reason;
} KNOWN_NON_ADDRESSES[] = {
    /* The float 1/32 as a required immediate, which is what says this site is the rate arm and not
     * some other test of the same cell. As an integer it is 0x3D000000. */
    { "substep_rate_switch", 0x0Fu, "the 1/32 immediate the rate arm is recognised by" },
    /* The opcode and the first two bytes of `sub esp, 0x80`, read backwards as 0x0080EC81. A frame
     * size between 0x40 and 0x8F puts any such prologue in the range. */
    { "sys_startup",         0x03u, "the stack frame instruction, not an operand" },
    /* The tail of `mov dword ptr [ebp-0x98], 0` followed by `push 0`, read as 0x006A0000. Two
     * zero bytes of an immediate and the opcode of the next instruction; masking it would throw
     * away the push that starts the argument list this pattern exists to reach. */
    { "title_main_menu",     0x27u, "two zeroes and a push opcode, not an operand" },
    /* `push 0x17; push 0`, read backwards as 0x006A176A. Two whole instructions, and the pair is
     * half of what makes this pattern one place in the image rather than an idiom; masking it
     * would throw away the broadcast that separates the round from every other copy of the same
     * two moves. The pattern begins ten bytes in front of the pair, so the restore flag has an
     * operand to be read out of, which puts the pair at 0x14. */
    { "campaign_round",      0x14u, "two push instructions, not an operand" }
};

static int is_known_non_address(const char *site_name, size_t offset)
{
    size_t index;

    for (index = 0; index < sizeof(KNOWN_NON_ADDRESSES) / sizeof(KNOWN_NON_ADDRESSES[0]); ++index) {
        if (strcmp(site_name, KNOWN_NON_ADDRESSES[index].site) == 0 &&
            offset == KNOWN_NON_ADDRESSES[index].offset) {
            return 1;
        }
    }
    return 0;
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* A window is required when every one of its four mask bytes demands a literal match. A pattern
 * with no mask at all requires all of its bytes. */
static int window_is_required(const signature_t *site, size_t offset)
{
    size_t index;

    if (site->mask == NULL) {
        return 1;
    }
    for (index = 0; index < 4u; ++index) {
        if (site->mask[offset + index] == 0u) {
            return 0;
        }
    }
    return 1;
}

static void check_no_required_address(const signature_t *sites, size_t count)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        const signature_t *site = &sites[index];
        size_t             offset;
        int                clean = 1;

        for (offset = 0; offset + 4u <= site->size; ++offset) {
            uint32_t value;

            if (!window_is_required(site, offset)) {
                continue;
            }
            value = read_le32(&site->bytes[offset]);
            if (value < IMAGE_LOW || value >= IMAGE_HIGH) {
                continue;
            }
            if (is_known_non_address(site->name, offset)) {
                continue;
            }
            ut_checkf(0, "%s requires %08X at +%#04x, which reads as an address",
                      site->name, (unsigned)value, (unsigned)offset);
            clean = 0;
        }

        if (clean) {
            ut_checkf(1, "%s requires no absolute address", site->name);
        }
    }
}

static void check_site_shape(const signature_t *sites, size_t count)
{
    size_t index;
    size_t other;

    for (index = 0; index < count; ++index) {
        const signature_t *site = &sites[index];

        ut_checkf(site->name != NULL && site->name[0] != '\0',
                  "site %u is named", (unsigned)index);
        ut_checkf(site->bytes != NULL && site->size >= 8u,
                  "%s carries at least eight bytes of pattern", site->name);

        if (site->detour_prologue != 0u) {
            ut_checkf(site->detour_prologue >= 5u,
                      "%s declares a prologue that covers a five byte branch", site->name);
            ut_checkf(site->detour_prologue < site->size,
                      "%s leaves a tail behind its prologue for stage two", site->name);
        }

        for (other = index + 1u; other < count; ++other) {
            ut_checkf(strcmp(site->name, sites[other].name) != 0,
                      "%s is named only once in the table", site->name);
        }
    }
}

/* The sites in the second table that are DATA ANCHORS rather than function entries, so the check
 * below knows a missing prologue is a decision and not an omission.
 *
 * A prologue is the number of leading bytes a detour would overwrite, and its only purpose is to
 * let the resolver find the site again after another module has branched over its head. A pattern
 * that starts in the middle of a function has no head anyone can branch over, so declaring one
 * would be a lie that stage two would then act on. Adding a name here is a decision: the question
 * to answer first is whether the pattern really starts inside a function rather than at one. */
static const char *const DATA_ANCHOR_SITES[] = {
    /* Reaches into player_spawnHero to name the four hero asset names and the resource loader.
     * The one module that does detour that function writes its branch over the ENTRY, which is
     * 0x75 bytes in front of these bytes. */
    "hero_asset_table",
    /* Reaches into Plr_ReceiveDamage to name the wav a hurt player plays. Nothing detours that
     * function, and this window starts 0xED bytes past its entry, so there is no head to
     * protect: the site exists so one operand falls out of it. */
    "hurt_voice_read",
    /* foot_run is the only footstep handler that asks which hero is walking, and its first
     * instruction past the frame reads that cell. Nothing detours it. Its neighbour
     * footstep_tick IS detoured, but that is a different site with its own entry, and this
     * window begins where that function has already ended. */
    "foot_run",
    /* A call inside enemy_tickAll, 0x3E bytes past its entry: the world anchor rewrites the
     * call's operand and detours nothing, so there is no head here to branch over. The entry's
     * own site is enemy_tick_all, and its pattern ends before this window begins. */
    "enemy_tick_anchor_call"
};

static int is_data_anchor(const char *site_name)
{
    size_t index;

    for (index = 0; index < sizeof(DATA_ANCHOR_SITES) / sizeof(DATA_ANCHOR_SITES[0]); ++index) {
        if (site_name != NULL && strcmp(site_name, DATA_ANCHOR_SITES[index]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* One table's rows against their places in the merged array. Three questions per row: it is
 * merged under its own name, it is merged byte for byte, and it is either something a detour
 * lands on or a data anchor that says so out loud. Taking a table and a base rather than naming
 * one means a table added later is checked by being passed here, not by being copied. */
static void check_merged_rows(const signature_t *merged, size_t merged_count,
                              const signature_t *table, size_t count, size_t base,
                              const char *what)
{
    size_t index;

    for (index = 0; index < count && base + index < merged_count; ++index) {
        const signature_t *row    = &table[index];
        const signature_t *mirror = &merged[base + index];

        ut_checkf(row->name != NULL && mirror->name != NULL &&
                      strcmp(row->name, mirror->name) == 0,
                  "%s row %u is merged under its own name", what, (unsigned)index);
        ut_checkf(row->bytes == mirror->bytes && row->size == mirror->size &&
                      row->mask == mirror->mask &&
                      row->detour_prologue == mirror->detour_prologue,
                  "%s is merged byte for byte", row->name != NULL ? row->name : "?");
        ut_checkf(row->detour_prologue != 0u || is_data_anchor(row->name),
                  "%s is a detour target or a declared data anchor",
                  row->name != NULL ? row->name : "?");
    }
}

static void check_second_table(void)
{
    size_t             merged_count = 0;
    size_t             puppet_count = 0;
    const signature_t *merged = mp_signatures_sites(&merged_count);
    const signature_t *puppet = mp_signatures_puppet_sites(&puppet_count);

    ut_check(merged_count == (size_t)MP_SITE_COUNT,
             "the merged table is as long as the site enumeration");
    ut_check(puppet_count == (size_t)MP_SITE_COUNT - (size_t)MP_SITE_PUPPET_FIRST,
             "the second table is as long as the puppet block of the enumeration");
    ut_check((size_t)MP_SITE_PUPPET_FIRST < (size_t)MP_SITE_COUNT,
             "the puppet block is not empty");

    check_merged_rows(merged, merged_count, puppet, puppet_count,
                      (size_t)MP_SITE_PUPPET_FIRST, "puppet");

    ut_check(strcmp(mp_signatures_site(MP_SITE_PLR_START_SWING)->name, "plr_start_swing") == 0 &&
                 strcmp(mp_signatures_site(MP_SITE_BAPOBJ_STOP_OVERLAY)->name,
                        "bapobj_stop_overlay") == 0 &&
                 strcmp(mp_signatures_site(MP_SITE_RDPUPPET_UPDATE_TRACK)->name,
                        "rdpuppet_update_track") == 0,
             "the three sites the puppet's animation channels stand on resolve to their own rows");
    ut_check(strcmp(mp_signatures_site(MP_SITE_PLR_CLEAR_SWING_CONTACT)->name,
                    "plr_clear_swing_contact") == 0 &&
                 strcmp(mp_signatures_site(MP_SITE_PLR_START_BLOCK_SHOT)->name,
                        "plr_start_block_shot") == 0 &&
                 strcmp(mp_signatures_site(MP_SITE_PLR_BLOCK_ATTACK)->name,
                        "plr_block_attack") == 0 &&
                 strcmp(mp_signatures_site(MP_SITE_PLR_ARMED_CONTACT)->name,
                        "plr_armed_contact") == 0 &&
                 strcmp(mp_signatures_site(MP_SITE_SHOT_KIND_REMAP)->name,
                        "shot_kind_remap") == 0,
             "the four sabre sites and the remap anchor resolve to their own rows");
    ut_check(strcmp(mp_signatures_site(MP_SITE_HERO_ASSET_TABLE)->name, "hero_asset_table") == 0 &&
                 strcmp(mp_signatures_site(MP_SITE_RES_FREE)->name, "res_free") == 0,
             "the two resource anchors an appearance stands on resolve to their own rows");
    ut_check(strcmp(mp_signatures_site(MP_SITE_BAPOBJ_SET_SCALE)->name, "bapobj_set_scale") == 0 &&
                 mp_signatures_site(MP_SITE_BAPOBJ_SET_SCALE)->detour_prologue == 6u,
             "the far body's size setter resolves to its own row, with its prologue of six");

    /* The three offsets the asset layer reads out of the hero asset table site are properties of
     * that site's pattern, and a wrong one does not fail, it answers.
     *
     * The two kinds of offset in this site are not the same kind, and reading one as the other
     * is the mistake this check is here for. RECORD and TABLE point at an
     * OPERAND, one byte past the opcode that takes it, because that is what a reader of an
     * absolute address needs. RES_ALLOC points at the OPCODE, because patch_read_call_target
     * takes the address of the call and verifies the E8 itself before it believes the
     * displacement behind it. Reading the second convention as the first is off by one, and the
     * failure it would cause is not a wrong target, the verifier would refuse, but the
     * appearance silently never installing. */
    {
        const signature_t *anchor = mp_signatures_site(MP_SITE_HERO_ASSET_TABLE);
        size_t             i;

        ut_check(anchor != NULL && MP_HERO_ASSET_SITE_RES_ALLOC + 5u <= anchor->size,
                 "the call opcode and its four byte displacement fit inside the anchor's pattern");
        ut_check(anchor != NULL && anchor->bytes[MP_HERO_ASSET_SITE_RES_ALLOC] == 0xE8u,
                 "the byte AT the call offset is a call opcode, which is what the reader checks");
        ut_check(anchor != NULL && anchor->mask != NULL &&
                     anchor->mask[MP_HERO_ASSET_SITE_TABLE] == 0u &&
                     anchor->mask[MP_HERO_ASSET_SITE_RECORD] == 0u,
                 "the two operands the asset layer reads are wildcards in the anchor's own mask");
        ut_check(anchor != NULL && anchor->mask != NULL &&
                     anchor->mask[MP_HERO_ASSET_SITE_RES_ALLOC] != 0u,
                 "but the call opcode itself is REQUIRED, or the pattern would match a site whose "
                 "call is not a call");

        /* And the displacement behind it has to be free, or the pattern pins the very number the
         * offset exists to report, the second property this file's header names. */
        for (i = 1u; i <= 4u; ++i) {
            ut_checkf(anchor != NULL && anchor->mask != NULL &&
                          anchor->mask[MP_HERO_ASSET_SITE_RES_ALLOC + i] == 0u,
                      "displacement byte %u is a wildcard", (unsigned)i);
        }
    }
}

/* The damage table's hash, pinned: every row moves it, order matters, and zero is reserved for
 * "not read" by construction rather than by luck. It is the one number of the game data the cells
 * give a join; the difficulty, the cheats and the detail level left it with wire 35. */
static void check_damage_table_hash(void)
{
    uint32_t impacts[MP_CELLS_SHOT_ROWS];
    uint32_t base;
    uint32_t row;

    for (row = 0; row < MP_CELLS_SHOT_ROWS; ++row) {
        impacts[row] = 0x29u + row;
    }
    base = mp_cells_damage_table_hash(impacts);

    ut_check(base != 0u, "a hash over ordinary values is never the unread marker");
    ut_check(mp_cells_damage_table_hash(impacts) == base, "the same values hash the same twice");

    impacts[MP_CELLS_SHOT_ROWS - 1u] ^= 1u;
    ut_check(mp_cells_damage_table_hash(impacts) != base,
             "one damage row changed by one is another table");
    impacts[MP_CELLS_SHOT_ROWS - 1u] ^= 1u;

    impacts[0] ^= impacts[1];
    impacts[1] ^= impacts[0];
    impacts[0] ^= impacts[1];
    ut_check(mp_cells_damage_table_hash(impacts) != base, "two rows swapped is another table");
}

/* The hash's byte order written out again, independently, as the reference it is held against. */
static uint32_t reference_dword(uint32_t sum, uint32_t value)
{
    unsigned byte;

    for (byte = 0; byte < 4u; ++byte) {
        sum ^= (value >> (8u * byte)) & 0xFFu;
        sum *= 16777619u;
    }
    return sum;
}

static void check_damage_table_and_substep(void)
{
    uint32_t impacts[MP_CELLS_SHOT_ROWS];
    uint32_t table = 2166136261u;
    uint32_t hash = 0;
    bool     seen = true;
    size_t   row;

    for (row = 0; row < MP_CELLS_SHOT_ROWS; ++row) {
        impacts[row] = 0x29u + (uint32_t)row * 3u;
        table = reference_dword(table, impacts[row]);
    }

    ut_check(mp_cells_damage_table_hash(impacts) == table,
             "the damage table hash is FNV-1a over the 38 impact codes, as before wire 35, so the "
             "line 'the game data before any level' keeps its values");
    impacts[5] ^= 1u;
    ut_check(mp_cells_damage_table_hash(impacts) != table,
             "one impact code changed is another table");
    ut_check(mp_cells_damage_table_hash(NULL) == 0u, "no rows at all answers the unread marker");
    ut_check(!mp_world_values_damage_table(&hash, &seen),
             "with no game the shot table does not resolve and nothing is claimed");

    ut_check(mp_world_values_substep_is_standard(0.03125f), "1/32 is the session's substep");
    ut_check(!mp_world_values_substep_is_standard(0.015625f),
             "1/64, what the 60fps cheat writes, is not");
    ut_check(!mp_world_values_substep_is_standard(0.0f), "and an unwritten cell is not either");
}

static void check_expected_matches(void)
{
    size_t index;

    for (index = 0; index < (size_t)MP_SITE_COUNT; ++index) {
        size_t expected = mp_signatures_expected_matches((mp_site_t)index);

        if (index == (size_t)MP_SITE_DEATH_LATCH_SET) {
            ut_check(expected == MP_DEATH_LATCH_MATCHES,
                     "the death latch is the site that expects two matches");
        } else {
            ut_checkf(expected == 1u, "site %u expects exactly one match", (unsigned)index);
        }
    }
}

static void check_operands(void)
{
    size_t              operand_count = 0;
    const mp_operand_t *operands      = mp_cells_operands(&operand_count);
    int                 named[MP_CELL_COUNT];
    size_t              index;

    for (index = 0; index < (size_t)MP_CELL_COUNT; ++index) {
        named[index] = 0;
    }

    for (index = 0; index < operand_count; ++index) {
        const mp_operand_t *row  = &operands[index];
        const signature_t  *site = mp_signatures_site(row->site);
        size_t              offset;
        int                 wildcarded = 1;

        ut_checkf((size_t)row->cell < (size_t)MP_CELL_COUNT,
                  "operand %u names a cell that exists", (unsigned)index);
        ut_checkf(site != NULL, "operand %u names a site that exists", (unsigned)index);
        if (site == NULL || (size_t)row->cell >= (size_t)MP_CELL_COUNT) {
            continue;
        }

        named[row->cell] = 1;

        ut_checkf((size_t)row->offset + 4u <= site->size,
                  "%s operand +%#04x lies inside its own pattern",
                  mp_cells_name(row->cell), (unsigned)row->offset);
        if ((size_t)row->offset + 4u > site->size) {
            continue;
        }

        for (offset = 0; offset < 4u; ++offset) {
            if (site->mask == NULL || site->mask[row->offset + offset] != 0u) {
                wildcarded = 0;
            }
        }
        ut_checkf(wildcarded, "%s is wildcarded at +%#04x of %s, so the read can disagree",
                  mp_cells_name(row->cell), (unsigned)row->offset, site->name);
    }

    for (index = 0; index < (size_t)MP_CELL_COUNT; ++index) {
        ut_checkf(named[index] != 0, "cell %s is named by at least one operand",
                  mp_cells_name((mp_cell_t)index));
    }
}

/* Nothing has resolved the host image in this process, so the scanner searches an empty range.
 * That is the same state an unsupported executable produces, and what the tables owe the caller
 * there is a zero rather than a leftover. */
static void check_nothing_resolves_without_a_host(void)
{
    size_t index;

    ut_check(mp_signatures_resolve(false) == 0u,
             "no site resolves in a process with no game in it");
    ut_check(mp_cells_resolve(false) == 0u, "and no cell does either");

    for (index = 0; index < (size_t)MP_SITE_COUNT; ++index) {
        ut_checkf(mp_signatures_address((mp_site_t)index) == 0u,
                  "site %u hands back zero rather than a leftover", (unsigned)index);
    }
    for (index = 0; index < MP_DEATH_LATCH_MATCHES; ++index) {
        ut_checkf(mp_signatures_death_latch(index) == 0u,
                  "death latch match %u hands back zero", (unsigned)index);
    }
    for (index = 0; index < (size_t)MP_CELL_COUNT; ++index) {
        ut_checkf(mp_cells_address((mp_cell_t)index) == 0u,
                  "cell %s hands back zero", mp_cells_name((mp_cell_t)index));
    }
}

static void check_out_of_range_is_refused(void)
{
    ut_check(mp_signatures_address((mp_site_t)MP_SITE_COUNT) == 0u,
             "a site index past the end is refused rather than read");
    ut_check(mp_signatures_site((mp_site_t)MP_SITE_COUNT) == NULL,
             "and so is a request for the entry itself");
    ut_check(mp_signatures_prologue((mp_site_t)MP_SITE_COUNT) == 0u,
             "and a prologue for it");
    ut_check(mp_signatures_death_latch(MP_DEATH_LATCH_MATCHES) == 0u,
             "a death latch index past the end is refused");
    ut_check(mp_cells_address((mp_cell_t)MP_CELL_COUNT) == 0u,
             "a cell index past the end is refused");
    ut_check(mp_cells_name((mp_cell_t)MP_CELL_COUNT) == NULL,
             "and it has no name");
}


/* The cell name table against the enum it names.
 *
 * A bare list against a growing enum drifts in silence: a cell added without a name moves every
 * later name onto the wrong cell and leaves the last ones NULL. Nothing calls the accessor yet, so
 * the wrongness would read as an answer rather than as a fault, and a NULL handed to a %s is
 * undefined behaviour.
 */
static void check_every_cell_has_its_own_name(void)
{
    size_t cell;
    size_t other;

    ut_section("every cell names itself, and no two share a name");

    for (cell = 0; cell < MP_CELL_COUNT; ++cell) {
        const char *name = mp_cells_name((mp_cell_t)cell);

        ut_checkf(name != NULL, "cell %u has a name at all", (unsigned)cell);
        if (name == NULL) {
            continue;
        }
        ut_checkf(name[0] != '\0', "cell %u's name is not empty", (unsigned)cell);
        for (other = 0; other < cell; ++other) {
            const char *earlier = mp_cells_name((mp_cell_t)other);

            ut_checkf(earlier == NULL || strcmp(name, earlier) != 0,
                      "cell %u's name is not a repeat of cell %u's", (unsigned)cell,
                      (unsigned)other);
        }
    }
}

/* The push block table: eight call sites, each ending on its E8 with the four operand bytes behind
 * it masked, so the pattern finds its site again after the call is repointed. The three windows an
 * earlier draft named for the push, the drop and the crush were 11, 5 and 8 bytes up to the E8
 * and matched 22, 25 and 3 times in the retail image; the control pass measured windows of 24, 16
 * and 21 bytes as unique, and none of the table's may be shorter. Where each one is unique in every
 * shipped image only the shipped images can prove. */
static void check_the_push_block_table(void)
{
    static const size_t SHORTEST[MP_CRATE_CALL_COUNT] = { 24u, 9u, 16u, 21u, 12u, 15u, 16u, 26u };
    size_t             count = 0;
    const signature_t *sites = mp_signatures_crate_sites(&count);
    size_t             i;

    ut_check(count == (size_t)MP_CRATE_CALL_COUNT, "the push block table has its eight rows");
    check_no_required_address(sites, count);
    for (i = 0; i < count && i < (size_t)MP_CRATE_CALL_COUNT; ++i) {
        size_t call = sites[i].size >= 5u ? sites[i].size - 5u : 0u;
        bool   operand_masked;

        operand_masked = sites[i].mask != NULL && call + 5u <= sites[i].size &&
                         sites[i].mask[call] == 0xFFu && sites[i].mask[call + 1u] == 0u &&
                         sites[i].mask[call + 2u] == 0u && sites[i].mask[call + 3u] == 0u &&
                         sites[i].mask[call + 4u] == 0u;
        ut_checkf(call + 5u == sites[i].size && sites[i].bytes[call] == 0xE8u && operand_masked,
                  "%s ends on its E8 at +%u with the operand behind it masked", sites[i].name,
                  (unsigned)call);
        ut_checkf(call + 1u >= SHORTEST[i],
                  "%s reads %u bytes up to its call, no fewer than the %u measured unique",
                  sites[i].name, (unsigned)(call + 1u), (unsigned)SHORTEST[i]);
        ut_checkf(sites[i].detour_prologue == 0u, "%s is no detour target: its call is repointed "
                  "or only read", sites[i].name);
    }
    ut_check(mp_signatures_crate_call(MP_CRATE_CALL_PUSH) == 0u &&
                 mp_signatures_crate_call(MP_CRATE_CALL_COUNT) == 0u,
             "before anything resolves no call is answered, and past the end none ever is");
}

int main(void)
{
    size_t             merged_count = 0;
    size_t             puppet_count = 0;
    const signature_t *merged = mp_signatures_sites(&merged_count);
    const signature_t *puppet = mp_signatures_puppet_sites(&puppet_count);

    check_every_cell_has_its_own_name();

    ut_section("no pattern requires an absolute address, in either table");
    check_no_required_address(merged, merged_count);
    check_no_required_address(puppet, puppet_count);

    ut_section("the shape of the merged table and of the second table's own template");
    check_site_shape(merged, merged_count);
    check_site_shape(puppet, puppet_count);
    check_second_table();

    ut_section("expected match counts");
    check_expected_matches();

    ut_section("every cell is named by an operand, and every operand is readable");
    check_operands();

    ut_section("the content fingerprint's hash");
    check_damage_table_hash();
    check_damage_table_and_substep();

    ut_section("a process with no game in it");
    check_nothing_resolves_without_a_host();

    ut_section("indices past the end");
    check_out_of_range_is_refused();

    ut_section("the push block call sites");
    check_the_push_block_table();

    return ut_summary("multiplayer signatures");
}
