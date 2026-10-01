/* mp_puppet_sabre.c: the far player's sabre actions performed on the puppet.
 *
 * Two of the four actions go through the engine's own function. The swing starter takes the
 * row of the swing table and does everything a swing is: it arms the blade's contact sphere out
 * of the row (node, radius, direction class, impact) and plays the row's clip, on the base
 * channel for every row but the midair one, whose clip is an overlay. The swing end is the one
 * function every sabre action ends through, and it clears the same sphere. Both read the player
 * through the record pointer, which inside the bank window names the puppet's record, so neither
 * touches the local player.
 *
 * The block and the parry cannot go through their engine functions: the deflect scans the shot
 * pool for a bolt to block and the parry scans for an attacker, and on this machine the bolt is
 * a copy in a different place and the attacker is the local player. The far machine already made
 * that choice and sent its result, the clip. So this module makes the stores those two functions
 * make after their scan (the contact node, the reflect radius and code), plays the clip on the
 * overlay channel, and installs the engine's own continuation in the record's aux slot, which
 * the puppet's rebuilt phase one runs every substep and which ends the block by itself when the
 * clip completes. The continuation reads the overlay slot without checking it, so it is installed
 * only when the clip actually plays; a block whose clip did not play is still armed and is ended
 * by the disarm event or the fallback.
 *
 * The parry's own function does not clear the deflect's success latch; the puppet clears it for
 * the parry as well, because a latch left at 1 by an earlier deflect would end the parry on its
 * first substep. That is a stated departure from the engine's stores.
 *
 * A far body in a borrowed model swings at a node the starter found by the hero's blade name,
 * and a rig a player may wear lacks that node, hides it under its own weapon, or carries it
 * without a mesh. Where the overlay hangs that player's own weapon on the rig's hand, the engine
 * answers that hand for the name and the swing has the drawn weapon's own sphere; where it does
 * not, and for the six rows that swing a fist or a foot, the node is put back to 0 after the
 * starter, which the pair pass reads as no sphere at all. The block and the parry take the same
 * node, and so do the sparks, so all three land on the weapon the wearer is seen holding.
 *
 * SIZE NOTE: a little over 600 lines, of which the byte evidence behind every store and both
 * continuations is the largest part. One subject, the sabre actions on a far body, decided and
 * then performed; the banner between the two halves is the seam if it grows.
 */
#include "mp_puppet_sabre.h"

#include "mp_bank.h"
#include "mp_body_wear.h"
#include "mp_cells.h"
#include "mp_events.h"
#include "mp_puppet_anim.h"
#include "mp_puppet_starter.h"
#include "mp_signatures.h"

#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Player record fields: the deflect's success latch, the parry's absorb count, the sabre node the
 * spawn found on the body (name id 9, the blade node), and the running aux updater (0 for none).
 * The deflect at 0x0044DBC6 and the parry at 0x0044E166 make the stores this module repeats, and
 * write their continuations into +0x64 with `C7 41 64 44 BC 44 00` at 0x0044E142 (the deflect's,
 * 0x0044BC44) and `C7 42 64 A0 BC 44 00` at 0x0044E26F (the parry's, 0x0044BCA0); both end their
 * action on the overlay track's complete latch or on the latch at +0x38. */
#define RECORD_DEFLECT_LATCH 0x38u
#define RECORD_PARRY_ABSORB  0x3Cu
#define RECORD_SABRE_NODE    0x4Cu
#define RECORD_AUX_ACTION    0x64u

/* Body object fields: the blade's contact node, code and radius (a code of 0 means the sphere is
 * off), the render thing, the current overlay clip, and the two channel slots. A census of the
 * writers of the sphere words (+0xA8 node, +0xAC code, +0xB0 radius, +0xB4 direction class, and
 * +0x0C the impact code a swing also writes) finds one armer for a swing, the starter at
 * 0x0044E858, which reads the row of the swing table and plays the row's clip with mode 4 on the
 * base channel, or for row 24 the overlay 0x55; and one disarmer, the swing end at 0x004509BB,
 * which zeroes the four words and writes the unarmed impact identity 0x29. The deflect and the
 * parry arm the node, the radius 0.25 and the reflect code 0x1C themselves, never the direction
 * class. The starter is cdecl with the caller cleaning up (`call 0x44E858; add esp, 4` at
 * 0x0044BF40); the swing end takes no argument. */
#define OBJECT_THING          0x9Cu
#define OBJECT_CONTACT_NODE   0xA8u
#define OBJECT_CONTACT_CODE   0xACu
#define OBJECT_CONTACT_RADIUS 0xB0u
#define OBJECT_CLIP_SLOT      0xECu
#define OBJECT_CUR_OVERLAY    0xF4u
#define OBJECT_OVERLAY_SLOT   0xF8u

/* The render thing's puppet, whose four tracks start eight bytes in and are 0x14C apart; per
 * track the flag word (0 means free), the keyframe it plays, and the complete latch the draw
 * loop sets when the track reaches its end. */
#define THING_PUPPET   0x18u
#define PUPPET_TRACKS  0x08u
#define TRACK_STRIDE   0x14Cu
#define TRACK_FLAGS    0x000u
#define TRACK_KEYFRAME 0x128u
#define TRACK_COMPLETE 0x140u

/* What the engine's deflect and parry write into the sphere: the reflect code and a quarter
 * unit of radius. */
#define CONTACT_CODE_REFLECT  0x1Cu
#define CONTACT_RADIUS_BLOCK  0.25f

typedef void(__cdecl *start_swing_fn_t)(int32_t row);
typedef void(__cdecl *clear_swing_fn_t)(void);
typedef int32_t(__cdecl *play_overlay_fn_t)(void *obj, int32_t clip, int32_t mode);

typedef struct track_state {
    bool     found;
    uint32_t flags;
    uint32_t keyframe;
    uint32_t complete;
} track_state_t;

/* One far body's blade. Each far bank shows its own player, whose blade that player's events
 * alone arm and disarm; one record for all of them would let one player's disarm end another's
 * block. */
typedef struct sabre_peer {
    bool     armed;                   /* a blade this module armed and nothing has disarmed */
    uint8_t  armed_action;
    uint32_t armed_substeps;
    uint32_t armed_slot_offset;       /* the channel slot the action's clip plays on */
    uint32_t armed_keyframe;          /* that clip's keyframe as the track held it; 0 unknown */
    bool     armed_across_reset;      /* the peer changed while it was armed; the fallback's turn */

    uint32_t aux_wait;                /* substeps the block or parry at the head has waited */
    uint32_t track_wait;              /* substeps the midair swing has waited for a free track */
} sabre_peer_t;

typedef struct mp_puppet_sabre_state {
    start_swing_fn_t  start_swing;
    clear_swing_fn_t  clear_swing_contact;
    play_overlay_fn_t play_overlay;
    uint32_t          block_aux;      /* the deflect's continuation; 0 when the cell is unknown */
    uint32_t          parry_aux;      /* the parry's */

    sabre_peer_t peer[MP_BANK_FAR_MAX + 1u];   /* by far bank, 0 the spare */

    mp_puppet_sabre_counters_t counters;
    bool refused_logged;
    bool fault_logged;
    bool fallback_logged;
    bool dropped_logged;
    bool unplayed_logged;
    bool aux_unknown_logged;
} mp_puppet_sabre_state_t;

static mp_puppet_sabre_state_t sabre;

/* One far body's blade by the bank that shows it. An index that is no far bank lands on the
 * spare at 0, which is what a caller outside every window, a unit test, gets. */
static sabre_peer_t *peer_of(size_t bank)
{
    return &sabre.peer[bank <= MP_BANK_FAR_MAX ? bank : 0u];
}

/* ==============================================================================================
 * The decisions. Nothing below this banner touches the engine.
 * ============================================================================================ */

uint32_t mp_puppet_sabre_stores(uint8_t action)
{
    switch (action) {
    case MP_SABRE_BLOCK:
        return MP_PUPPET_SABRE_STORE_NODE | MP_PUPPET_SABRE_STORE_RADIUS |
               MP_PUPPET_SABRE_STORE_CODE | MP_PUPPET_SABRE_STORE_LATCH;
    case MP_SABRE_PARRY:
        return MP_PUPPET_SABRE_STORE_NODE | MP_PUPPET_SABRE_STORE_RADIUS |
               MP_PUPPET_SABRE_STORE_CODE | MP_PUPPET_SABRE_STORE_LATCH |
               MP_PUPPET_SABRE_STORE_ABSORB;
    default:
        return 0u;
    }
}

bool mp_puppet_sabre_row_ok(uint8_t row)
{
    return row < MP_PUPPET_SABRE_SWING_ROWS;
}

bool mp_puppet_sabre_operand_ok(uint8_t action, uint8_t operand)
{
    switch (action) {
    case MP_SABRE_SWING:
        return mp_puppet_sabre_row_ok(operand);
    case MP_SABRE_BLOCK:
        return operand >= MP_PUPPET_SABRE_BLOCK_FIRST && operand <= MP_PUPPET_SABRE_BLOCK_LAST;
    case MP_SABRE_PARRY:
        return operand >= MP_PUPPET_SABRE_PARRY_FIRST && operand <= MP_PUPPET_SABRE_PARRY_LAST;
    case MP_SABRE_DISARM:
        return operand == 0u;
    default:
        return false;
    }
}

bool mp_puppet_sabre_aux_is_ours(uint32_t aux, uint32_t block_aux, uint32_t parry_aux)
{
    return aux != 0u && (aux == block_aux || aux == parry_aux);
}

mp_puppet_sabre_aux_verdict_t mp_puppet_sabre_aux_verdict(uint32_t current_aux,
                                                          uint32_t block_aux, uint32_t parry_aux)
{
    if (current_aux == 0u) {
        return MP_PUPPET_SABRE_AUX_FREE;
    }
    if (mp_puppet_sabre_aux_is_ours(current_aux, block_aux, parry_aux)) {
        return MP_PUPPET_SABRE_AUX_OVERWRITE;
    }
    return MP_PUPPET_SABRE_AUX_WAIT;
}

bool mp_puppet_sabre_fallback_due(bool track_gone, bool track_complete, uint32_t armed_substeps)
{
    return track_gone || track_complete || armed_substeps >= MP_PUPPET_SABRE_FALLBACK_SUBSTEPS;
}

/* ==============================================================================================
 * The engine half.
 * ============================================================================================ */

bool mp_puppet_sabre_armed(size_t bank)
{
    return peer_of(bank)->armed;
}

void mp_puppet_sabre_counters(mp_puppet_sabre_counters_t *out)
{
    if (out != NULL) {
        *out = sabre.counters;
    }
}

static bool inside_text(uintptr_t address)
{
    uintptr_t text = host_image_text();

    return text != 0 && address >= text && address < text + host_image_text_size();
}

static void refuse(const char *why)
{
    ++sabre.counters.refused;
    if (!sabre.refused_logged) {
        sabre.refused_logged = true;
        log_warning("a sabre action was not performed on the puppet because %s; later refusals "
                    "are counted, not logged", why);
    }
}

static void note_write_fault(const char *what)
{
    ++sabre.counters.write_faults;
    if (!sabre.fault_logged) {
        sabre.fault_logged = true;
        log_warning("the puppet's %s write was refused; later refusals are counted", what);
    }
}

/* The track a channel slot names, through the object's render thing and its puppet. A slot
 * outside 0..3 (-1 is none) is not followed: the engine's own slot reader does not check it and
 * would hand back memory before the array. */
static bool read_track(uint32_t object, uint32_t slot_offset, track_state_t *out)
{
    uint32_t thing = 0;
    uint32_t puppet = 0;
    uint32_t slot = 0;
    uint32_t track;

    memset(out, 0, sizeof *out);
    if (object == 0u || !memory_try_read_u32(object + OBJECT_THING, &thing) || thing == 0u ||
        !memory_try_read_u32(thing + THING_PUPPET, &puppet) || puppet == 0u ||
        !memory_try_read_u32(object + slot_offset, &slot) || slot >= MP_PUPPET_ANIM_TRACKS) {
        return false;
    }
    track = puppet + PUPPET_TRACKS + slot * TRACK_STRIDE;
    if (!memory_try_read_u32(track + TRACK_FLAGS, &out->flags) ||
        !memory_try_read_u32(track + TRACK_KEYFRAME, &out->keyframe) ||
        !memory_try_read_u32(track + TRACK_COMPLETE, &out->complete)) {
        return false;
    }
    out->found = true;
    return true;
}

/* Remember what was armed and the track its clip plays on, right after the start, while the
 * slot names the fresh track: that keyframe is what the fallback later checks the slot against.
 * A track that does not read leaves the keyframe unknown, and the fallback then has only the
 * timer. */
static void latch_armed(sabre_peer_t *p, uint32_t object, uint8_t action, uint32_t slot_offset,
                        bool clip_played)
{
    track_state_t track;

    p->armed              = true;
    p->armed_action       = action;
    p->armed_substeps     = 0u;
    p->armed_slot_offset  = slot_offset;
    p->armed_keyframe     = 0u;
    p->armed_across_reset = false;
    if (clip_played && read_track(object, slot_offset, &track) && track.flags != 0u) {
        p->armed_keyframe = track.keyframe;
    }
}

/* The disarm clears the aux slot only when it holds one of the two continuations this module
 * installs; a weapon change or a push in flight keeps its own. */
static void clear_own_aux(uint32_t record)
{
    uint32_t aux = 0;

    if (memory_read_u32(record + RECORD_AUX_ACTION, &aux) &&
        mp_puppet_sabre_aux_is_ours(aux, sabre.block_aux, sabre.parry_aux) &&
        patch_write_u32(record + RECORD_AUX_ACTION, 0u) != PATCH_RESULT_OK) {
        note_write_fault("aux slot");
    }
}

/* The starter has just armed a sphere on the node it found by the hero's blade name. On a worn rig
 * that node is missing (the answer is 0 already), hidden under the rig's own weapon, whose matrix
 * is never chained and leaves the sphere near the world's origin, or, on obi.baf, a node without a
 * mesh: bapobj_nodeSphere turns back at 0x0041429C before it writes the centre, and the pair pass
 * at 0x00411A11 then takes the centre off its own stack. A node of 0 is the pass's own test for no
 * sphere, at 0x00411A6A for the first object of a pair and at 0x00411F7A, 0x00412053 and
 * 0x004121A9 for the second. The code and the radius stay, so the fallback still sees the swing
 * and ends it.
 *
 * WHERE THE CONTACT stays. A body that carries its player's own weapon answers that blade name
 * with the hand the weapon is drawn at, and the sphere there is the drawn weapon's, measured off
 * its meshes; that is a contact worth keeping, and the blow lands where it looks like it lands.
 * It is kept only for a row that swings a weapon: the node column at +0x08 of the swing table at
 * 0x004B4E00 reads lhand, rhand or lfoot in rows 0 to 5 and sabreblad01 in rows 6 to 27, and no
 * weapon hangs on a fist, so those six are still put back. */
bool mp_puppet_sabre_withhold_contact(uint32_t object, bool worn, bool weapon, uint8_t row)
{
    const uint32_t none = 0u;
    uint32_t       node = 0;

    if (!worn || object == 0u) {
        return false;
    }
    /* The decision, counted whatever the node: every swing of a model wearer shows here, and the
     * two counts below divide it into the swings that keep what the starter armed and the ones
     * whose node it found and this puts back. */
    ++sabre.counters.worn_swings;
    if (weapon && row >= MP_PUPPET_SABRE_UNARMED_ROWS) {
        ++sabre.counters.swing_contacts_kept;
        return false;
    }
    if (!memory_read_u32(object + OBJECT_CONTACT_NODE, &node) || node == 0u) {
        return false;
    }
    if (!memory_try_write(object + OBJECT_CONTACT_NODE, &none, sizeof none)) {
        note_write_fault("contact node");
        return false;
    }
    ++sabre.counters.swing_contacts_withheld;
    return true;
}

/* The midair row is the one swing whose clip goes on the overlay channel, and the starter plays
 * it through the engine's player, which takes no answer for "no track" and ends the process on a
 * clip the actor lacks. Which row that is, is read once from the table the starter reads, and that
 * one answer decides the guard, the wait and the track the fallback watches. A swing the actor
 * cannot play is dropped before anything else, the swing starter's own absence included, so
 * nothing is armed; one whose actor did not read waits for a free track the way a block waits for
 * the aux slot, and is dropped after the same two seconds. Every other row plays on the base
 * channel, a single track the player always has, whose player refuses a missing clip itself. */
static mp_puppet_sabre_result_t perform_swing(size_t bank, sabre_peer_t *p, uint32_t object,
                                              uint8_t row, bool worn, bool weapon)
{
    uintptr_t            table = mp_cells_address(MP_CELL_SWING_TABLE);
    bool                 overlay = mp_puppet_starter_swing_overlay(table, row);
    mp_starter_verdict_t verdict = mp_puppet_starter_swing(bank, object, overlay);

    if (verdict == MP_STARTER_NEVER) {
        return MP_PUPPET_SABRE_DONE;   /* said and counted by the guard */
    }
    if (sabre.start_swing == NULL) {
        refuse("the swing starter did not resolve");
        return MP_PUPPET_SABRE_DONE;
    }
    if (object == 0u) {
        refuse("the puppet's object did not read");
        return MP_PUPPET_SABRE_DONE;
    }
    if (overlay && (verdict != MP_STARTER_GO || !mp_puppet_anim_overlay_track_free(object))) {
        if (++p->track_wait > MP_PUPPET_SABRE_AUX_WAIT_LIMIT) {
            p->track_wait = 0u;
            ++sabre.counters.track_dropped;
            return MP_PUPPET_SABRE_DONE;
        }
        return MP_PUPPET_SABRE_HOLD;
    }
    p->track_wait = 0u;
    sabre.start_swing((int32_t)row);
    ++sabre.counters.swings;
    (void)mp_puppet_sabre_withhold_contact(object, worn, weapon, row);
    latch_armed(p, object, MP_SABRE_SWING, overlay ? OBJECT_OVERLAY_SLOT : OBJECT_CLIP_SLOT,
                true);
    return MP_PUPPET_SABRE_DONE;
}

static void perform_disarm(sabre_peer_t *p, uint32_t record)
{
    if (sabre.clear_swing_contact == NULL) {
        refuse("the swing end did not resolve");
        return;
    }
    sabre.clear_swing_contact();
    clear_own_aux(record);
    p->armed = false;
    ++sabre.counters.disarms;
}

/* The stores the engine's deflect and parry make after their scan, in an order that leaves the
 * sphere off if anything before the code refuses: a code with a wrong node or radius would be
 * a sphere somewhere on the body. The success latch is cleared for the parry as well. Two of
 * the deflect's stores are not made: the chest reset through the node yaw setter and the speed
 * store, because the twists arrive from the wire every substep and the puppet's plan reads no
 * speed; and the render bit at 0x00412DF7 is not set, because it is a render flag and not an
 * arming. The other exit the continuations have, the latch at +0x38, is set by the armed contact
 * handler when a bolt hits the armed blade, and that handler never runs for the puppet, whose
 * contacts are answered without the engine's handler, so here a block ends on its clip's end or
 * on the disarm event. */
static bool arm_blade(size_t bank, uint32_t record, uint32_t object, uint8_t action)
{
    uint32_t stores = mp_puppet_sabre_stores(action);
    uint32_t node = 0;

    if ((stores & MP_PUPPET_SABRE_STORE_NODE) != 0u) {
        /* A body carrying its player's own weapon is asked of the engine, which names the hand
         * that weapon is drawn at; every other body answers 0 there and keeps the node its own
         * spawn left in the record. */
        node = mp_body_wear_worn_blade_node(bank, object);
        if (node == 0u && !memory_read_u32(record + RECORD_SABRE_NODE, &node)) {
            refuse("the record's sabre node did not read");
            return false;
        }
        if (patch_write_u32(object + OBJECT_CONTACT_NODE, node) != PATCH_RESULT_OK) {
            note_write_fault("contact node");
            return false;
        }
    }
    if ((stores & MP_PUPPET_SABRE_STORE_RADIUS) != 0u &&
        patch_write_f32(object + OBJECT_CONTACT_RADIUS, CONTACT_RADIUS_BLOCK) != PATCH_RESULT_OK) {
        note_write_fault("contact radius");
        return false;
    }
    if ((stores & MP_PUPPET_SABRE_STORE_LATCH) != 0u &&
        patch_write_u32(record + RECORD_DEFLECT_LATCH, 0u) != PATCH_RESULT_OK) {
        note_write_fault("deflect latch");
        return false;
    }
    if ((stores & MP_PUPPET_SABRE_STORE_ABSORB) != 0u &&
        patch_write_u32(record + RECORD_PARRY_ABSORB, 0u) != PATCH_RESULT_OK) {
        note_write_fault("parry absorb count");
        return false;
    }
    if ((stores & MP_PUPPET_SABRE_STORE_CODE) != 0u &&
        patch_write_u32(object + OBJECT_CONTACT_CODE, CONTACT_CODE_REFLECT) != PATCH_RESULT_OK) {
        note_write_fault("contact code");
        return false;
    }
    return true;
}

/* The clip on the overlay channel with a crossfade, as the engine's deflect plays it. Played
 * means the object records it as the current overlay afterwards and its slot names a track;
 * only then may the continuation, which reads that slot unchecked, be installed. */
static bool play_block_overlay(uint32_t object, uint8_t clip)
{
    uint32_t current = 0;
    uint32_t slot = 0;

    if (sabre.play_overlay == NULL || !mp_puppet_anim_clip_exists(object, clip) ||
        !mp_puppet_anim_overlay_track_free(object)) {
        return false;
    }
    (void)sabre.play_overlay((void *)(uintptr_t)object, (int32_t)clip, MP_PUPPET_ANIM_MODE_FADE);
    return memory_read_u32(object + OBJECT_CUR_OVERLAY, &current) && current == clip &&
           memory_read_u32(object + OBJECT_OVERLAY_SLOT, &slot) && slot < MP_PUPPET_ANIM_TRACKS;
}

static bool install_aux(uint32_t record, uint8_t action)
{
    uint32_t aux = action == MP_SABRE_BLOCK ? sabre.block_aux : sabre.parry_aux;

    if (aux == 0u || !inside_text(aux)) {
        if (!sabre.aux_unknown_logged) {
            sabre.aux_unknown_logged = true;
            log_warning("the puppet's %s plays without its continuation: the cell did not "
                        "resolve or lies outside the code, so only the disarm event or the "
                        "fallback ends it", action == MP_SABRE_BLOCK ? "block" : "parry");
        }
        return false;
    }
    if (patch_write_u32(record + RECORD_AUX_ACTION, aux) != PATCH_RESULT_OK) {
        note_write_fault("aux slot");
        return false;
    }
    return true;
}

static mp_puppet_sabre_result_t perform_block(size_t bank, sabre_peer_t *p, uint32_t record,
                                              uint32_t object, uint8_t action, uint8_t clip,
                                              bool *aux_taken)
{
    uint32_t aux = 0;
    bool     played;

    if (object == 0u) {
        refuse("the puppet's object did not read");
        return MP_PUPPET_SABRE_DONE;
    }
    /* An aux slot that cannot be read counts as busy: a block over a guess would tear the
     * continuation a weapon change is waiting on. */
    if (*aux_taken || !memory_try_read_u32(record + RECORD_AUX_ACTION, &aux) ||
        mp_puppet_sabre_aux_verdict(aux, sabre.block_aux, sabre.parry_aux) ==
            MP_PUPPET_SABRE_AUX_WAIT) {
        if (++p->aux_wait > MP_PUPPET_SABRE_AUX_WAIT_LIMIT) {
            p->aux_wait = 0u;
            ++sabre.counters.held_dropped;
            if (!sabre.dropped_logged) {
                sabre.dropped_logged = true;
                log_warning("a far %s waited two seconds for the puppet's aux slot and was "
                            "dropped; later drops are counted",
                            action == MP_SABRE_BLOCK ? "block" : "parry");
            }
            return MP_PUPPET_SABRE_DONE;
        }
        *aux_taken = true;
        return MP_PUPPET_SABRE_HOLD;
    }
    p->aux_wait = 0u;

    if (!arm_blade(bank, record, object, action)) {
        return MP_PUPPET_SABRE_DONE;
    }
    played = play_block_overlay(object, clip);
    if (played) {
        if (install_aux(record, action)) {
            *aux_taken = true;
        }
    } else {
        ++sabre.counters.overlays_unplayed;
        if (!sabre.unplayed_logged) {
            sabre.unplayed_logged = true;
            log_warning("the puppet's blade is armed for a %s but clip %u did not play (missing, "
                        "refused, or every track busy); the disarm event or the fallback ends it",
                        action == MP_SABRE_BLOCK ? "block" : "parry", (unsigned)clip);
        }
    }
    if (action == MP_SABRE_BLOCK) {
        ++sabre.counters.blocks;
    } else {
        ++sabre.counters.parries;
    }
    latch_armed(p, object, action, OBJECT_OVERLAY_SLOT, played);
    return MP_PUPPET_SABRE_DONE;
}

mp_puppet_sabre_result_t mp_puppet_sabre_perform(size_t bank, uint32_t record, uint32_t object,
                                                 const mp_event_t *event, bool *aux_taken)
{
    sabre_peer_t *p = peer_of(bank);

    if (event == NULL || aux_taken == NULL || event->kind != MP_EVENT_SABRE || record == 0u) {
        return MP_PUPPET_SABRE_DONE;
    }
    if (!mp_puppet_sabre_operand_ok(event->action, event->operand)) {
        refuse("its operand is outside what the action may name");
        return MP_PUPPET_SABRE_DONE;
    }
    switch (event->action) {
    case MP_SABRE_SWING:
        return perform_swing(bank, p, object, event->operand, mp_body_wear_worn(bank),
                             mp_body_wear_weapon(bank));
    case MP_SABRE_BLOCK:
    case MP_SABRE_PARRY:
        return perform_block(bank, p, record, object, event->action, event->operand,
                             aux_taken);
    case MP_SABRE_DISARM:
    default:
        perform_disarm(p, record);
        return MP_PUPPET_SABRE_DONE;
    }
}

/* The fallback runs after the rebuilt phase one, so a continuation that ended its block this
 * substep has already cleared the code, and the tick sees a blade nobody needs to disarm. */
void mp_puppet_sabre_tick(size_t bank, uint32_t record, uint32_t object)
{
    sabre_peer_t *p = peer_of(bank);
    track_state_t track;
    uint32_t      code = 0;
    bool          gone = false;
    bool          complete = false;

    if (!p->armed || record == 0u || object == 0u) {
        return;
    }
    ++p->armed_substeps;
    if (memory_try_read_u32(object + OBJECT_CONTACT_CODE, &code) && code == 0u) {
        p->armed = false;   /* the continuation or the disarm event got there first */
        return;
    }
    if (p->armed_keyframe != 0u) {
        gone     = !read_track(object, p->armed_slot_offset, &track) || track.flags == 0u ||
                   track.keyframe != p->armed_keyframe;
        complete = !gone && track.complete != 0u;
    }
    if (!mp_puppet_sabre_fallback_due(gone, complete, p->armed_substeps)) {
        return;
    }
    if (sabre.clear_swing_contact == NULL) {
        p->armed = false;   /* nothing to disarm it with; the engine's own stores stand */
        return;
    }
    sabre.clear_swing_contact();
    clear_own_aux(record);
    p->armed = false;
    ++sabre.counters.fallback_disarms;
    if (!sabre.fallback_logged) {
        sabre.fallback_logged = true;
        log_info("the puppet's blade was disarmed by the fallback after %u substep(s) (%s); the "
                 "disarm event did not arrive first; later fallbacks are counted",
                 (unsigned)p->armed_substeps,
                 p->armed_across_reset ? "the peer changed while it was armed"
                 : gone                   ? "its track is gone"
                 : complete               ? "its clip completed"
                                          : "timed out");
    }
}

/* The two continuation cells are read out of the immediates of the armed contact handler at
 * 0x0044855C, at +0x0E and +0x39 of it, behind the dev overlay's branch on that head, and are code
 * addresses; one is written into +0x64 only when it lies inside the image's text, the same test
 * the puppet's phase one makes before calling what it finds there. */
void mp_puppet_sabre_resolve(void)
{
    size_t bank;

    sabre.start_swing = (start_swing_fn_t)mp_signatures_address(MP_SITE_PLR_START_SWING);
    sabre.clear_swing_contact =
        (clear_swing_fn_t)mp_signatures_address(MP_SITE_PLR_CLEAR_SWING_CONTACT);
    sabre.play_overlay = (play_overlay_fn_t)mp_signatures_address(MP_SITE_BAPOBJ_PLAY_OVERLAY);
    sabre.block_aux = (uint32_t)mp_cells_address(MP_CELL_BLOCK_SHOT_AUX);
    sabre.parry_aux = (uint32_t)mp_cells_address(MP_CELL_BLOCK_ATTACK_AUX);
    for (bank = 0u; bank <= MP_BANK_FAR_MAX; ++bank) {
        mp_puppet_sabre_reset(bank);
    }

    if (sabre.start_swing == NULL || sabre.clear_swing_contact == NULL ||
        sabre.play_overlay == NULL || sabre.block_aux == 0u || sabre.parry_aux == 0u) {
        log_warning("the puppet's sabre is dressed with less than the full set: swing starter "
                    "%s, swing end %s, overlay player %s, deflect continuation %s, parry "
                    "continuation %s; a missing one leaves its own action unperformed",
                    sabre.start_swing != NULL ? "ok" : "MISSING",
                    sabre.clear_swing_contact != NULL ? "ok" : "MISSING",
                    sabre.play_overlay != NULL ? "ok" : "MISSING",
                    sabre.block_aux != 0u ? "ok" : "MISSING",
                    sabre.parry_aux != 0u ? "ok" : "MISSING");
    }
}

/* The armed blade is kept and its timer is put at the fallback's span, so the first window after
 * the arrival disarms it through the fallback: the disarm event that would have ended it was in
 * the rings the arrival emptied, and a blade left armed would reflect the local player's bolts
 * and hurt him at a touch until the far player's next swing. It is not disarmed here, because the
 * swing end reads the player through the record pointer, which names the puppet's record only
 * inside the bank window; outside it the call would strike the local player's blade. Only the
 * wait for the aux slot starts over: the block it waited for is gone with the rings. An earlier
 * reset cleared the armed flag instead, which took from the fallback exactly the case it exists
 * for: the sphere stayed on the object with its code and radius, the tick turned back on the
 * clear flag, and nothing called the swing end until the far player's next swing. */
void mp_puppet_sabre_reset(size_t bank)
{
    sabre_peer_t *p = peer_of(bank);

    if (p->armed) {
        p->armed_substeps     = MP_PUPPET_SABRE_FALLBACK_SUBSTEPS;
        p->armed_across_reset = true;
    }
    p->aux_wait   = 0u;
    p->track_wait = 0u;
}
