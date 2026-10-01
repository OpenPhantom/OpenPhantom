/* mp_blade.c: a Jedi body's sabre vectors and a puppet's blade tick, read off the engine. See the
 * header.
 *
 * The block is the one player_spawnHero 0x00447E58 fills: it clears all 0xEB dwords first
 * (0x00447E5F rep stosd), stores the hero index it was given at +0x6C (0x00447EA2), the body at
 * +0x0C (0x00447ECC), the actor res_Alloc handed back at +0x00 (0x00447EF2), and the node named
 * sabreblad01 at +0x4C (0x0044818E). Only for hero 0 or 1 (0x004481AD-0x004481C2) does it call the
 * capture 0x00449835, which reads the blade node's four vertices into +0x1C8 (the buffer pushed at
 * 0x00449861), works out the two deltas behind them and folds the mesh. A block of any other hero
 * carries zeros there. player_restore 0x00447AB1 reads the whole block from the savegame instead
 * and hands the saved vectors to the length setter through the rebind at 0x00447C80; it answers 0
 * when it did (0x00447C8A), and its one caller tests exactly that (0x0044794D).
 *
 * The capture reads the mesh as it stands, and since no far body writes the mesh any more, it
 * stands as the local player's length left it: folded while his sabre is away. That is why a far
 * body still takes the longest reading anybody has of its asset.
 */
#include "mp_blade.h"

#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_blade_draw.h"
#include "mp_body.h"
#include "mp_body_asset.h"
#include "mp_cells.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(MP_BLADE_NAME_BYTES == MP_ACTOR_NAME_BYTES,
               "the book is keyed by the actor header's own name field");

#define BLOCK_ACTOR  0x00u
#define BLOCK_OBJECT 0x0Cu
#define BLOCK_NODE   0x4Cu
#define BLOCK_HERO   0x6Cu
#define BLOCK_BLADE  0x1C8u

typedef struct mp_blade_state {
    mp_blade_book_t     book;
    bool                foreign_said;
    bool                local_folded_said;
    bool                write_fault_said;
    mp_blade_counters_t counters;
} mp_blade_state_t;

static mp_blade_state_t blade;

/* The body a choice is about and the blocks it is held against, each with its asset's name. */
typedef struct blade_set {
    char            name[1u + MP_BANK_FAR_MAX][MP_BLADE_NAME_BYTES];   /* 0 is the body's own */
    mp_blade_body_t self;
    mp_blade_body_t others[MP_BANK_FAR_MAX];
    size_t          from[MP_BANK_FAR_MAX];   /* the far bank each other is, 0 the local player */
    size_t          count;
} blade_set_t;

static bool read_u32(uintptr_t base, uint32_t offset, uint32_t *out)
{
    return base != 0u && memory_try_read(base + offset, out, sizeof *out);
}

/* A block as the rule reads it. One whose hero index or vectors do not read is left out. */
static bool read_block(uintptr_t block, mp_blade_body_t *out, char name[MP_BLADE_NAME_BYTES])
{
    uint32_t actor = 0u;

    memset(out, 0, sizeof *out);
    name[0]   = '\0';
    out->name = name;
    out->hero = MP_BLADE_NO_HERO;
    if (!read_u32(block, BLOCK_HERO, &out->hero) ||
        !memory_try_read(block + BLOCK_BLADE, &out->vectors, sizeof out->vectors)) {
        out->hero = MP_BLADE_NO_HERO;
        return false;
    }
    if (read_u32(block, BLOCK_ACTOR, &actor)) {
        (void)mp_body_asset_name_of(actor, name, MP_BLADE_NAME_BYTES);
    }
    return true;
}

static void add_other(blade_set_t *set, uintptr_t block, size_t from)
{
    if (set->count < MP_BANK_FAR_MAX &&
        read_block(block, &set->others[set->count], set->name[1u + set->count])) {
        set->from[set->count++] = from;
    }
}

static const char *shown(const char *name)
{
    return name[0] != '\0' ? name : "an unnamed asset";
}

/* What the book did with a choice, said at the end of the line that reports it. */
static const char *book_news(mp_blade_learned_t learned)
{
    if (learned == MP_BLADE_LEARNED_NEW) {
        return "; the book keeps it now";
    }
    return learned == MP_BLADE_LEARNED_RAISED ? "; the book is raised to it" : "";
}

/* The book's counts after a choice, and a reading with another hilt under the asset's name, said
 * once and then counted. Bank 0 is the player. */
static void note_book(size_t bank, const char *name, const mp_blade_choice_t *choice)
{
    blade.counters.book_known     = (uint32_t)blade.book.known;
    blade.counters.book_raised    = blade.book.raised;
    blade.counters.book_refused   = blade.book.refused;
    blade.counters.foreign_hilts += choice->foreign;
    if (choice->foreign == 0u || blade.foreign_said) {
        return;
    }
    blade.foreign_said = true;
    if (bank == 0u) {
        log_warning("the player's blade: %u reading(s) of %s carry another hilt than the one "
                    "taken, so they are not used; later ones are counted",
                    (unsigned)choice->foreign, shown(name));
    } else {
        log_warning("bank %u's blade: %u reading(s) of %s carry another hilt than the one taken, "
                    "so they are not used; later ones are counted", (unsigned)bank,
                    (unsigned)choice->foreign, shown(name));
    }
}

static void note_write_fault(const char *whose)
{
    ++blade.counters.write_faults;
    if (!blade.write_fault_said) {
        blade.write_fault_said = true;
        log_warning("%s blade vectors could not be written, so it keeps what was read; later "
                    "refusals are counted", whose);
    }
}

/* One line for every far Jedi body, naming where its vectors came from. */
static void note_far(size_t bank, const blade_set_t *set, const mp_blade_choice_t *choice)
{
    const char *name = shown(set->name[0]);
    const char *news = book_news(choice->learned);

    ++blade.counters.far_jedi;
    if (choice->source == MP_BLADE_BOOK) {
        ++blade.counters.far_book;
        log_info("bank %u's blade takes the vectors the book keeps for %s", (unsigned)bank, name);
    } else if (choice->source == MP_BLADE_DONOR && set->from[choice->donor] == 0u) {
        ++blade.counters.far_local;
        log_info("bank %u's blade takes the local player's vectors: both wear %s%s",
                 (unsigned)bank, name, news);
    } else if (choice->source == MP_BLADE_DONOR) {
        ++blade.counters.far_bank;
        log_info("bank %u's blade takes bank %u's vectors: both wear %s%s", (unsigned)bank,
                 (unsigned)set->from[choice->donor], name, news);
    } else if (choice->source == MP_BLADE_OWN) {
        ++blade.counters.far_own;
        log_info("bank %u's blade keeps what its spawn read off %s: no body and no book had a "
                 "longer blade of it%s", (unsigned)bank, name, news);
    } else {
        ++blade.counters.far_folded;
        log_warning("bank %u's blade was read off a folded %s, and neither a body nor the book "
                    "has its full blade: it will not extend", (unsigned)bank, name);
    }
}

mp_blade_source_t mp_blade_far_spawn_over(size_t bank, uintptr_t local_block,
                                          mp_blade_choice_t *out)
{
    blade_set_t       set;
    mp_blade_choice_t choice;
    uint32_t          object = 0u;
    size_t            other;

    memset(&set, 0, sizeof set);
    memset(&choice, 0, sizeof choice);
    if (out != NULL) {
        memset(out, 0, sizeof *out);
    }
    if (!mp_bank_index_ok(bank) ||
        !read_block(mp_bank_block_at(bank), &set.self, set.name[0])) {
        return MP_BLADE_NONE;
    }
    if (read_u32(local_block, BLOCK_OBJECT, &object) && object != 0u) {
        add_other(&set, local_block, 0u);
    }
    for (other = 1u; other <= MP_BANK_FAR_MAX; ++other) {
        if (other != bank && mp_body_exists_at(other)) {
            add_other(&set, mp_bank_block_at(other), other);
        }
    }
    mp_blade_rule_choose(&set.self, set.others, set.count, &blade.book, &choice);
    if (out != NULL) {
        *out = choice;
    }
    if (choice.source == MP_BLADE_NONE) {
        return MP_BLADE_NONE;
    }
    note_far(bank, &set, &choice);
    note_book(bank, set.name[0], &choice);
    if (mp_blade_rule_writes(choice.source) &&
        memcmp(&choice.vectors, &set.self.vectors, sizeof choice.vectors) != 0 &&
        !mp_bank_write_at(bank, BLOCK_BLADE, &choice.vectors, sizeof choice.vectors)) {
        note_write_fault("a far body's");
    }
    return choice.source;
}

void mp_blade_after_far_spawn(size_t bank)
{
    (void)mp_blade_far_spawn_over(bank, mp_cells_address(MP_CELL_HERO_BLOCK), NULL);
    mp_blade_draw_fill(bank);
}

/* The player's line, only when his vectors were replaced: a death and a respawn are frequent. */
static void note_local(const blade_set_t *set, const mp_blade_choice_t *choice, bool restored)
{
    const char *name = shown(set->name[0]);
    const char *what = restored ? "the savegame held" : "its spawn read";

    ++blade.counters.local_put_right;
    if (choice->source == MP_BLADE_BOOK) {
        log_info("the player's blade takes the vectors the book keeps for %s, in place of what %s",
                 name, what);
    } else {
        log_info("the player's blade takes bank %u's vectors for %s, in place of what %s",
                 (unsigned)set->from[choice->donor], name, what);
    }
}

mp_blade_source_t mp_blade_local_over(uintptr_t block, bool restored, mp_blade_choice_t *out)
{
    blade_set_t       set;
    mp_blade_choice_t choice;
    size_t            bank;

    memset(&set, 0, sizeof set);
    memset(&choice, 0, sizeof choice);
    if (out != NULL) {
        memset(out, 0, sizeof *out);
    }
    if (!read_block(block, &set.self, set.name[0])) {
        return MP_BLADE_NONE;
    }
    for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
        if (mp_body_exists_at(bank)) {
            add_other(&set, mp_bank_block_at(bank), bank);
        }
    }
    mp_blade_rule_choose(&set.self, set.others, set.count, &blade.book, &choice);
    if (out != NULL) {
        *out = choice;
    }
    if (choice.source == MP_BLADE_NONE) {
        return MP_BLADE_NONE;
    }
    ++blade.counters.local_settled;
    note_book(0u, set.name[0], &choice);
    if (choice.source == MP_BLADE_FOLDED) {
        ++blade.counters.local_folded;
        if (!blade.local_folded_said) {
            blade.local_folded_said = true;
            log_warning("the player's blade was read off a folded %s, and neither a far body nor "
                        "the book has its full blade: it will not extend; later cases are counted",
                        shown(set.name[0]));
        }
    }
    if (!mp_blade_rule_writes(choice.source) ||
        memcmp(&choice.vectors, &set.self.vectors, sizeof choice.vectors) == 0) {
        return choice.source;
    }
    if (!memory_try_write(block + BLOCK_BLADE, &choice.vectors, sizeof choice.vectors)) {
        note_write_fault("the player's");
        return choice.source;
    }
    note_local(&set, &choice, restored);
    return choice.source;
}

/* No session, no multiplayer: the hulls this is called from stay installed for the life of the
 * process. And a bank window open means the spawn belonged to a far body whose loan was refused. */
static void after_local(bool restored)
{
    if (!mp_armed_transport() || mp_bank_active() != 0u) {
        return;
    }
    (void)mp_blade_local_over(mp_cells_address(MP_CELL_HERO_BLOCK), restored, NULL);
}

void mp_blade_after_local_spawn(void)
{
    after_local(false);
}

void mp_blade_after_local_restore(void)
{
    after_local(true);
}

mp_blade_tick_t mp_blade_puppet_tick(uintptr_t record, bool worn)
{
    mp_blade_tick_facts_t facts;

    memset(&facts, 0, sizeof facts);
    facts.worn = worn;
    if (!read_u32(record, BLOCK_HERO, &facts.hero)) {
        facts.hero = MP_BLADE_NO_HERO;
    }
    if (!read_u32(record, BLOCK_NODE, &facts.node)) {
        facts.node = 0u;
    }
    return mp_blade_rule_tick(&facts);
}

void mp_blade_get_counters(mp_blade_counters_t *out)
{
    if (out != NULL) {
        *out = blade.counters;
    }
}
