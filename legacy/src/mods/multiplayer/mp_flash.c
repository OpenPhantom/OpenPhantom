/* mp_flash.c: the white screen of a thermal detonator, only when it is near or in the picture. */
#include "mp_flash.h"

#include "mp_armed.h"
#include "mp_bank.h"
#include "mp_cells.h"

#include "common/logging.h"
#include "common/memory.h"

/* The shot table's rows and the two fields of a row this needs. The handler of kind 10 is what is
 * written; the stride and the field are the engine's own layout. */
#define SHOT_DESC_STRIDE  0x44u
#define SHOT_DESC_HANDLER 0x3Cu
#define SHOT_KIND_THERMAL 10u

/* The engine's own address for the arm we replace, which is what makes the write an exchange
 * rather than a guess: the entry is only taken when it still holds this. */
#define SHOT_HANDLER_THERMAL 0x00455DBDu

/* The two events whose arm is nothing but the flash. Everything else this handler is asked goes
 * straight back to the engine's own. */
#define SHOT_EV_ACTOR_HIT 3
#define SHOT_EV_EXPIRE    6

/* A shot names the object it flies as, and an object carries its place. */
#define SHOT_OBJECT     0xA0u
#define OBJECT_POSITION 0x18u

typedef int32_t(__cdecl *shot_handler_fn_t)(void *shot, int32_t event);

typedef struct flash_state {
    uintptr_t         entry;       /* the table cell we write, 0 until it resolves */
    shot_handler_fn_t original;
    float             near_units;
    bool              armed;
    uint32_t          arms;        /* times the rule was armed in this process */

    uint32_t seen_hit;
    uint32_t seen_expire;
    uint32_t let_through;
    uint32_t withheld;
    uint32_t undecided_no_block;   /* the hero block did not read */
    uint32_t undecided_no_object;  /* the shot carried no object */
    uint32_t undecided_banked;     /* a far bank held the hero block: must stay zero */
} flash_state_t;

static flash_state_t flash;

/* Where this machine's own player stands and what he faces. False leaves both alone, and the
 * reason is counted rather than guessed at: a bank window holding the hero block would have this
 * measure the distance to a PUPPET, which is the one way this rule could be silently wrong. */
static bool own_eye(float eye[3], float *heading)
{
    uintptr_t block = mp_cells_address(MP_CELL_HERO_BLOCK);

    if (mp_bank_active() != 0u) {
        ++flash.undecided_banked;
        return false;
    }
    if (block == 0u || !memory_try_read(block + MP_HERO_BLOCK_POS, eye, sizeof(float) * 3u) ||
        !memory_try_read(block + MP_HERO_BLOCK_HEADING, heading, sizeof(float))) {
        ++flash.undecided_no_block;
        return false;
    }
    return true;
}

static bool detonation_is_seen(void *shot)
{
    float    eye[3];
    float    at[3];
    float    heading = 0.0f;
    uint32_t object = 0;

    if (shot == NULL || !memory_try_read_u32((uintptr_t)shot + SHOT_OBJECT, &object) ||
        object == 0u || !memory_try_read((uintptr_t)object + OBJECT_POSITION, at, sizeof at)) {
        ++flash.undecided_no_object;
        return true;
    }
    if (!own_eye(eye, &heading)) {
        return true;
    }
    return mp_flash_is_seen(eye, heading, at, flash.near_units);
}

/* Our arm. Seven of the nine events go straight back to the engine's; the two that are nothing
 * but the flash are decided here, and refusing one means returning what the engine's own arm
 * returns for them, which is zero. */
static int32_t __cdecl flash_handler(void *shot, int32_t event)
{
    if (event != SHOT_EV_ACTOR_HIT && event != SHOT_EV_EXPIRE) {
        return flash.original != NULL ? flash.original(shot, event) : 0;
    }
    if (event == SHOT_EV_ACTOR_HIT) {
        ++flash.seen_hit;
    } else {
        ++flash.seen_expire;
    }
    if (!mp_armed_transport() || detonation_is_seen(shot)) {
        ++flash.let_through;
        return flash.original != NULL ? flash.original(shot, event) : 0;
    }
    ++flash.withheld;
    return 0;
}

void mp_flash_set_near(uint32_t units)
{
    flash.near_units = (float)units;
}

bool mp_flash_arm(void)
{
    uintptr_t table = mp_cells_address(MP_CELL_SHOT_TABLE);
    uint32_t  held = 0;
    uint32_t  ours;

    if (flash.armed) {
        return true;
    }
    if (table == 0u) {
        log_warning("a detonation still flashes the whole screen this session: the shot table "
                    "did not resolve, so the kind's own arm could not be taken");
        return false;
    }
    flash.entry = table + SHOT_KIND_THERMAL * SHOT_DESC_STRIDE + SHOT_DESC_HANDLER;
    if (!memory_read_u32(flash.entry, &held) || held != SHOT_HANDLER_THERMAL) {
        log_warning("the thermal detonator's handler is %08X and not the engine's own %08X, so "
                    "the entry is left alone: somebody else holds this kind", (unsigned)held,
                    (unsigned)SHOT_HANDLER_THERMAL);
        flash.entry = 0u;
        return false;
    }
    flash.original = (shot_handler_fn_t)(uintptr_t)held;
    ours           = (uint32_t)(uintptr_t)&flash_handler;
    if (!memory_try_write(flash.entry, &ours, sizeof ours)) {
        flash.entry = 0u;
        return false;
    }
    flash.armed = true;
    ++flash.arms;
    log_info("a detonation flashes the screen only within %u unit(s) or inside the player's own "
             "view: the shot table's arm for the kind is ours until the session ends",
             (unsigned)flash.near_units);
    return true;
}

void mp_flash_disarm(void)
{
    uint32_t held = 0;
    uint32_t back;

    if (!flash.armed || flash.entry == 0u) {
        return;
    }
    flash.armed = false;
    back = (uint32_t)(uintptr_t)flash.original;
    if (!memory_read_u32(flash.entry, &held) || held != (uint32_t)(uintptr_t)&flash_handler) {
        log_warning("the thermal detonator's handler was taken by something else while the "
                    "session ran; the entry is left as it stands rather than overwritten");
        flash.entry = 0u;
        return;
    }
    (void)memory_try_write(flash.entry, &back, sizeof back);
    flash.entry = 0u;
}

void mp_flash_report(void)
{
    /* Said even with nothing seen: a rule that never armed looks exactly like one that armed
     * and saw no detonation, unless the report tells the two apart. */
    if (flash.arms == 0u) {
        log_info("  the detonation flash: the rule was never armed in this process, so every "
                 "detonation flashed the whole screen (a session's transport arms it)");
        return;
    }
    if (flash.seen_hit == 0u && flash.seen_expire == 0u) {
        return;
    }
    log_info("  the detonation flash: %u on a body and %u on expiry, %u let through, %u withheld "
             "as neither near nor in view", (unsigned)flash.seen_hit,
             (unsigned)flash.seen_expire, (unsigned)flash.let_through, (unsigned)flash.withheld);
    /* A sentence of its own, so the line above keeps the words it is found by when two runs'
     * reports are compared. Every number here is a flash that was let through without the rule
     * being asked, and the last one must stay at zero: it means the arm ran while a far player's
     * bank held the hero block, and the distance would then have been measured to a puppet. */
    log_info("    let through undecided: %u with no hero block, %u with no object under the "
             "shot, %u while a far bank was active",
             (unsigned)flash.undecided_no_block, (unsigned)flash.undecided_no_object,
             (unsigned)flash.undecided_banked);
}
