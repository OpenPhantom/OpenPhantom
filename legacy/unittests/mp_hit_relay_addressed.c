/* A hit the host saw on a far player goes to that player's machine alone.
 *
 * The session tests prove that a note addressed to a slot reaches the peer at that slot; what they
 * cannot prove is that the hit relay addresses it at all. This drives the relay's own path, from
 * the host's contact on a puppet to the note it sends, with the engine played by the test: the
 * contact globals are fields of this file, the contact handler the relay hulls is a few bytes of
 * code the test lays out, and the modules around the relay answer as a host with three clients
 * would. Nothing is ever called through the hull.
 */
#include "unittest.h"

#include "mp_bank.h"
#include "mp_body.h"
#include "mp_cells.h"
#include "mp_enemy_bind.h"
#include "mp_enemy_spawn.h"
#include "mp_enemy_sync.h"
#include "mp_hit_relay.h"
#include "mp_hit_wire.h"
#include "mp_knockback.h"
#include "mp_signatures.h"
#include "mp_target.h"
#include "mp_trust.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The engine and the neighbours, as a host with three clients.
 * ============================================================================================ */

/* push ebp / mov ebp, esp / sub esp, 10h / leave / ret: a head the hull can take six bytes of. */
static const uint8_t CONTACT_HANDLER[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0xC9, 0xC3 };

static uint8_t *handler;          /* executable, so the hull's trampoline has a head to copy */
static uint32_t cell_self;
static uint32_t cell_other;
static uint32_t cell_code = 0x27u;
static uint32_t cell_impact = 3u;

uintptr_t mp_cells_address(mp_cell_t cell)
{
    switch (cell) {
    case MP_CELL_MSG_SELF:   return (uintptr_t)&cell_self;
    case MP_CELL_MSG_OTHER:  return (uintptr_t)&cell_other;
    case MP_CELL_MSG_CODE:   return (uintptr_t)&cell_code;
    case MP_CELL_MSG_IMPACT: return (uintptr_t)&cell_impact;
    default:                 return 0u;
    }
}

uintptr_t mp_signatures_address(mp_site_t site)
{
    return site == MP_SITE_ENEMY_ON_CONTACT ? (uintptr_t)handler : 0u;
}

/* Bank b shows the player at world slot b, as on a listen host. */
bool mp_body_bank_slot(size_t index, uint8_t *world_slot)
{
    if (index == 0u || index > MP_BANK_FAR_MAX || world_slot == NULL) {
        return false;
    }
    *world_slot = (uint8_t)index;
    return true;
}

bool mp_bank_read_at(size_t index, size_t offset, void *out, size_t size)
{
    (void)index;
    (void)offset;
    (void)out;
    (void)size;
    return false;
}

bool mp_body_exists_at(size_t index)
{
    (void)index;
    return false;
}

bool mp_body_run_engine_contact(void)
{
    return false;
}

bool mp_enemy_bind_index(uintptr_t actor, uint32_t *out)
{
    (void)actor;
    (void)out;
    return false;
}

bool mp_enemy_bind_is_parked(uintptr_t actor)
{
    (void)actor;
    return false;
}

bool mp_enemy_spawn_actor_is_live(uintptr_t actor, uint32_t key)
{
    (void)actor;
    (void)key;
    return false;
}

uintptr_t mp_enemy_sync_actor_for(uint32_t key)
{
    (void)key;
    return 0u;
}

uintptr_t mp_enemy_sync_replica_for(uint32_t key)
{
    (void)key;
    return 0u;
}

void mp_enemy_sync_note_struck(uint32_t key, uint8_t slot)
{
    (void)key;
    (void)slot;
}

void mp_target_note_attack(uintptr_t victim_actor, uint32_t attacker_object)
{
    (void)victim_actor;
    (void)attacker_object;
}

void mp_knockback_before(mp_knockback_watch_t *watch, uintptr_t victim, uint32_t sender,
                         uintptr_t code_cell)
{
    (void)watch;
    (void)victim;
    (void)sender;
    (void)code_cell;
}

void mp_knockback_after(const mp_knockback_watch_t *watch)
{
    (void)watch;
}

void mp_knockback_note_refused(uint32_t key, size_t bank)
{
    (void)key;
    (void)bank;
}

void mp_knockback_report(void)
{
}

mp_trust_hit_verdict_t mp_trust_host_performs_hit(uint32_t key, uint8_t code)
{
    (void)key;
    (void)code;
    return (mp_trust_hit_verdict_t)0;
}

/* ---- the two ways out the bridge gives the relay --------------------------------------------- */

typedef struct outbox {
    uint32_t to_slot;              /* notes addressed to one slot */
    uint8_t  slot;
    uint8_t  note[MP_PLAYER_HIT_BYTES];
    size_t   bytes;
    uint32_t to_everybody;         /* notes broadcast */
    mp_hit_relay_addressed_t answer;
} outbox_t;

static outbox_t out;

static mp_hit_relay_addressed_t fake_send_to_slot(uint8_t slot, const uint8_t *bytes, size_t count)
{
    ++out.to_slot;
    out.slot  = slot;
    out.bytes = count <= sizeof out.note ? count : sizeof out.note;
    memcpy(out.note, bytes, out.bytes);
    return out.answer;
}

static bool fake_send(const uint8_t *bytes, size_t count)
{
    (void)bytes;
    (void)count;
    ++out.to_everybody;
    return true;
}

/* ============================================================================================== */

static void check_a_hit_on_a_far_player_goes_to_his_machine(void)
{
    mp_player_hit_note_t note;

    ut_section("the host's hit on the puppet of bank 2 goes to slot 2's machine and nowhere else");
    memset(&out, 0, sizeof out);
    mp_hit_relay_set_client(false);
    mp_hit_relay_set_send(&fake_send);
    mp_hit_relay_set_send_to_slot(&fake_send_to_slot);
    out.answer = MP_HIT_ADDRESSED_SENT;

    mp_hit_relay_note_puppet_hit(2u);
    ut_checkf(out.to_slot == 1u && out.slot == 2u && out.to_everybody == 0u,
              "one note to slot 2, none to everybody (%u addressed, to slot %u, %u broadcast)",
              (unsigned)out.to_slot, (unsigned)out.slot, (unsigned)out.to_everybody);
    ut_check(out.bytes == MP_PLAYER_HIT_BYTES &&
                 mp_player_hit_decode(out.note, out.bytes, &note) && note.slot == 2u &&
                 note.code == 0x27u && note.attacker == (uint16_t)MP_PLAYER_HIT_NO_ATTACKER,
             "and it is the player hit note for slot 2, with the contact's code and no attacker");

    ut_section("a slot nobody plays at on this side is not broadcast instead");
    out.answer = MP_HIT_ADDRESSED_NO_PEER;
    mp_hit_relay_note_puppet_hit(3u);
    ut_check(out.to_slot == 2u && out.slot == 3u && out.to_everybody == 0u,
             "asked for slot 3, answered no peer, and nothing went to everybody");

    ut_section("without an addressed way, as on the loopback, it goes to everybody");
    mp_hit_relay_set_send_to_slot(NULL);
    mp_hit_relay_note_puppet_hit(1u);
    ut_check(out.to_slot == 2u && out.to_everybody == 1u, "one broadcast, nothing addressed");

    ut_section("a client reports no hit on a far player at all");
    mp_hit_relay_set_send_to_slot(&fake_send_to_slot);
    mp_hit_relay_set_client(true);
    mp_hit_relay_note_puppet_hit(2u);
    ut_check(out.to_slot == 2u && out.to_everybody == 1u, "nothing addressed, nothing broadcast");
    mp_hit_relay_set_client(false);
}

int main(void)
{
    DWORD protection = 0;

    handler = (uint8_t *)VirtualAlloc(NULL, 64u, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ut_check(handler != NULL, "room for the contact handler the relay hulls");
    if (handler == NULL) {
        return ut_summary("mp_hit_relay_addressed");
    }
    memcpy(handler, CONTACT_HANDLER, sizeof CONTACT_HANDLER);
    (void)VirtualProtect(handler, 64u, PAGE_EXECUTE_READWRITE, &protection);

    ut_check(mp_hit_relay_install(), "the hit relay installs over the engine this test plays");
    check_a_hit_on_a_far_player_goes_to_his_machine();
    mp_hit_relay_report();
    return ut_summary("mp_hit_relay_addressed");
}
