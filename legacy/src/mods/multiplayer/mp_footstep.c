/* mp_footstep.c: a far player's footfalls, made where they are seen. */
#include "mp_footstep.h"

#include "mp_bank.h"
#include "mp_cells.h"
#include "mp_node_map.h"
#include "mp_signatures.h"
#include "mp_signatures_world.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <string.h>

/* The body's position, the same field every other module in this feature reads off an object. */
#define OBJECT_POSITION 0x18u

/* The floor the body last stood on. `footstep_tick` reads it as its second instruction and
 * returns at once when it is zero, which is why a puppet leaves no footfall by itself. */
#define OBJECT_FLOOR_POLY 0xE4u

/* The footstep module's own globals, as offsets from `g_footThing`, whose address is the operand
 * in the tick's prologue. They sit in one contiguous run, so one anchor names them all. */
#define FOOT_LAST_VARIANT_OFFSET 0x18u
#define FOOT_FRAME_PREV_OFFSET   0x14u

/* The two feet, as the name table numbers them. The engine resolves them by its OWN name id
 * inside the tick, and answers zero, the root, for a name a rig does not carry. So a model
 * out of the model swap without them lays its prints in the body's middle. */
#define FOOT_NODE_LEFT  5u
#define FOOT_NODE_RIGHT 6u

/* The ground contact block the probe fills, in dwords, and the one field read out of it. */
#define GROUND_CONTACT_WORDS 0x22u
#define GROUND_POLY_WORD     5u

typedef void(__cdecl *footstep_tick_fn_t)(int32_t loco, int32_t female, uint32_t thing);
typedef void(__cdecl *probe_floor_fn_t)(const float position[3], void *ground);

typedef union ground_contact {
    float    f[GROUND_CONTACT_WORDS];
    uint32_t i[GROUND_CONTACT_WORDS];
} ground_contact_t;

typedef struct footstep_state {
    detour_t           tick_detour;
    footstep_tick_fn_t original;     /* the engine's tick, behind our hull */
    probe_floor_fn_t   probe_floor;
    uintptr_t          module;       /* g_footThing, the base of the module's globals */
    uintptr_t          current_player;
    bool               installed;

    /* The frame cursor and the sound variant, one saved pair per far bank. The engine keeps one
     * of each for every body in the game, and that is exactly the defect: see the head of the
     * header. Bank 0 is this machine's own and keeps the engine's own cells. */
    uint32_t bank_frame_prev[MP_BANK_FAR_MAX + 1u];
    uint32_t bank_last_variant[MP_BANK_FAR_MAX + 1u];

    /* The rig question, asked once per model. It is a string scan over every node, and the
     * object only changes when the body is respawned or its model swapped. */
    uint32_t bank_rig_object[MP_BANK_FAR_MAX + 1u];
    bool     bank_rig_has_feet[MP_BANK_FAR_MAX + 1u];

    uint8_t  local_state;            /* what the engine last asked for, cleared on read */

    uint32_t offered;
    uint32_t played;
    uint32_t no_floor;               /* nothing under the reported position was floor */
    uint32_t no_position;            /* the body's own position did not read */
    uint32_t unresolved;             /* the tick or the probe has no address */
    uint32_t cell_was_dirty;         /* the floor cell held something before we wrote it */
    uint32_t without_feet;           /* run over a rig carrying neither foot node */
    uint32_t by_state[16];           /* which states were actually reached */
} footstep_state_t;

static footstep_state_t footstep;

static uint32_t read_word_at(uintptr_t address)
{
    uint32_t value = 0;

    (void)memory_try_read(address, &value, sizeof value);
    return value;
}

static void write_word_at(uintptr_t address, uint32_t value)
{
    (void)memory_try_write(address, &value, sizeof value);
}

/* The hull, on the machine where the player really runs. It needs no guard against our own call
 * below, because that call goes through the trampoline and never reaches this function. What it
 * does need is the bank: with BankTick standing, which is the in-process loopback rather than a
 * session over a socket, the engine ticks a far body through the whole player pipeline and this
 * would then record the PUPPET'S state as the local player's. */
static void __cdecl hook_footstep_tick(int32_t loco, int32_t female, uint32_t thing)
{
    if (loco > 0 && loco < 16 && mp_bank_active() == 0u) {
        footstep.local_state = (uint8_t)loco;
    }
    if (footstep.original != NULL) {
        footstep.original(loco, female, thing);
    }
}

bool mp_footstep_install(void)
{
    uintptr_t site = mp_signatures_address(MP_SITE_FOOTSTEP_TICK);

    if (footstep.installed) {
        return true;
    }
    footstep.module         = mp_cells_address(MP_CELL_FOOT_MODULE);
    footstep.current_player = mp_cells_address(MP_CELL_CURRENT_PLAYER);
    footstep.probe_floor    =
        (probe_floor_fn_t)mp_signatures_world_address(MP_WORLD_SITE_PROBE_FLOOR);

    if (site == 0 || footstep.module == 0 || footstep.current_player == 0 ||
        footstep.probe_floor == NULL) {
        log_warning("a far player makes no footfalls this run: the tick, its globals, the hero "
                    "cell or the floor probe did not resolve");
        return false;
    }
    if (!detour_install(&footstep.tick_detour, site, (const void *)&hook_footstep_tick,
                        mp_signatures_prologue(MP_SITE_FOOTSTEP_TICK))) {
        log_error("the footstep tick at %08X refused the detour", (unsigned)site);
        return false;
    }
    footstep.original  = (footstep_tick_fn_t)footstep.tick_detour.original;
    footstep.installed = true;
    return true;
}

uint8_t mp_footstep_take_local_state(void)
{
    uint8_t state = footstep.local_state;

    footstep.local_state = MP_FOOTSTEP_NONE;
    return state;
}

/* Whether this bank's body carries feet to put a print under, remembered per model. A rig
 * without them still makes the right SOUND, which is the half that matters, so this counts
 * rather than refuses. */
static bool rig_has_feet(size_t bank, uint32_t object)
{
    if (footstep.bank_rig_object[bank] != object) {
        footstep.bank_rig_object[bank] = object;
        footstep.bank_rig_has_feet[bank] =
            mp_node_map_body_has(object, FOOT_NODE_LEFT) &&
            mp_node_map_body_has(object, FOOT_NODE_RIGHT);
    }
    return footstep.bank_rig_has_feet[bank];
}

void mp_footstep_run(size_t bank, uint32_t object, uint8_t loco, uint8_t hero)
{
    ground_contact_t ground;
    float            position[3];
    uintptr_t        frame_prev;
    uintptr_t        last_variant;
    uint32_t         saved_frame_prev;
    uint32_t         saved_last_variant;
    uint32_t         saved_player;
    uint32_t         saved_floor;
    uint32_t         poly;

    if (loco == MP_FOOTSTEP_NONE || object == 0u || bank > MP_BANK_FAR_MAX) {
        return;
    }
    ++footstep.offered;
    if (!footstep.installed || footstep.original == NULL) {
        ++footstep.unresolved;
        return;
    }
    if (!memory_try_read((uintptr_t)object + OBJECT_POSITION, position, sizeof position)) {
        ++footstep.no_position;
        return;
    }

    /* The floor is probed, never read. The cell a puppet carries is zero for the life of the
     * session, and one that ever fills stays filled: nothing nulls it again, and after a level
     * change it points into freed world memory that the tick would read every substep. */
    memset(&ground, 0, sizeof ground);
    footstep.probe_floor(position, &ground);
    poly = ground.i[GROUND_POLY_WORD];
    if (poly == 0u) {
        ++footstep.no_floor;
        return;
    }

    frame_prev   = footstep.module + FOOT_FRAME_PREV_OFFSET;
    last_variant = footstep.module + FOOT_LAST_VARIANT_OFFSET;

    saved_frame_prev   = read_word_at(frame_prev);
    saved_last_variant = read_word_at(last_variant);
    saved_player       = read_word_at(footstep.current_player);
    saved_floor        = read_word_at((uintptr_t)object + OBJECT_FLOOR_POLY);
    if (saved_floor != 0u) {
        ++footstep.cell_was_dirty;
    }

    write_word_at(frame_prev, footstep.bank_frame_prev[bank]);
    write_word_at(last_variant, footstep.bank_last_variant[bank]);
    write_word_at(footstep.current_player, hero);
    write_word_at((uintptr_t)object + OBJECT_FLOOR_POLY, poly);

    footstep.original((int32_t)loco,
                      hero == (uint8_t)MP_FOOTSTEP_HERO_QUEEN ? 1 : 0, object);

    footstep.bank_frame_prev[bank]   = read_word_at(frame_prev);
    footstep.bank_last_variant[bank] = read_word_at(last_variant);

    write_word_at((uintptr_t)object + OBJECT_FLOOR_POLY, saved_floor);
    write_word_at(footstep.current_player, saved_player);
    write_word_at(last_variant, saved_last_variant);
    write_word_at(frame_prev, saved_frame_prev);

    ++footstep.played;
    if (loco < 16u) {
        ++footstep.by_state[loco];
    }
    if (!rig_has_feet(bank, object)) {
        ++footstep.without_feet;
    }
}

void mp_footstep_report(void)
{
    if (footstep.offered == 0u && footstep.played == 0u) {
        return;
    }
    log_info("  a far player's footfalls: %u offered, %u run, %u with nothing but air under the "
             "reported position, %u whose body did not read, %u with no tick to call",
             (unsigned)footstep.offered, (unsigned)footstep.played, (unsigned)footstep.no_floor,
             (unsigned)footstep.no_position, (unsigned)footstep.unresolved);
    /* A sentence of its own, so the line above keeps the words it is found by when two runs'
     * reports are compared. The states are the engine's own numbers; walking and running are the
     * two that carry a session, and a run in which both stay at zero says the byte is not arriving
     * rather than that nobody walked. The last number must stay at zero: it means something else
     * filled a cell that this feature is the only writer of. */
    log_info("    by locomotion state: walking %u, running %u, landing %u, backwards %u, "
             "swimming %u, slipping %u, other %u; %u floor cell(s) were not empty",
             (unsigned)footstep.by_state[1], (unsigned)footstep.by_state[2],
             (unsigned)footstep.by_state[3], (unsigned)footstep.by_state[4],
             (unsigned)footstep.by_state[6], (unsigned)footstep.by_state[7],
             (unsigned)(footstep.by_state[8] + footstep.by_state[9] + footstep.by_state[10] +
                        footstep.by_state[11]),
             (unsigned)footstep.cell_was_dirty);
    if (footstep.without_feet != 0u) {
        /* The rig carries no lfoot and no rfoot, so the engine resolved both to the root and
         * every print, sand puff and water ring of these went into the body's middle. The
         * sound was right. A model out of the model swap is how this happens. */
        log_info("    %u of them over a rig with no feet, whose prints landed in the body's "
                 "middle", (unsigned)footstep.without_feet);
    }
}
