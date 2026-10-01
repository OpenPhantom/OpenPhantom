/* What a host takes on a player's word.
 *
 * The pure half first: which game checks its clients, which of the wire's tags a player may send
 * at all and which of those a dedicated server passes between players, and whether a note names
 * only its sender's slot. Then a listen host and one client over the loopback, the host read
 * through the bridge's own drain: a note only a host sends is refused, a death written for another
 * player is refused and the client's own is not, and a client whose content differs is sent away
 * with the reason by a deathmatch host and kept by a co-op one.
 */
#include "unittest.h"

#include "mp_bridge_content.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_chat_wire.h"
#include "mp_death.h"
#include "mp_dialog.h"
#include "mp_events.h"
#include "mp_hit_relay.h"
#include "mp_host_settings_rule.h"
#include "mp_level_state_rule.h"
#include "mp_lobby.h"
#include "mp_loopback.h"
#include "mp_npc_copy_wire.h"
#include "mp_npc_shot.h"
#include "mp_quest.h"
#include "mp_roster.h"
#include "mp_savefile.h"
#include "mp_score.h"
#include "mp_scratch_wire.h"
#include "mp_session.h"
#include "mp_taken.h"
#include "mp_transport.h"
#include "mp_trust.h"
#include "mp_wire.h"
#include "mp_world.h"
#include "mp_world_state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Every tag on the wire, what it is, and the two answers it gets. A tag added to the wire belongs
 * in this table, and the census below counts the band so a gap shows. */
typedef struct tag_case {
    uint8_t     tag;
    bool        player_may_say;
    bool        passes_between;
    const char *name;
} tag_case_t;

static const tag_case_t TAGS[] = {
    { (uint8_t)MP_EVENT_SHOT,           true,  true,  "a shot" },
    { (uint8_t)MP_EVENT_PUSH,           true,  true,  "a push" },
    { (uint8_t)MP_EVENT_SABRE,          true,  true,  "a sabre action" },
    { (uint8_t)MP_EVENT_WEAPON,         true,  true,  "a weapon change" },
    { (uint8_t)MP_EVENT_MOVER,          true,  true,  "a mover" },
    /* A player may say it, and a server does not pass it on: a digest measures ONE machine's
     * map, and two clients corrected against each other would chase a drift neither owns. */
    { (uint8_t)MP_WORLD_DIGEST_TAG,     true,  false, "the map's digest" },
    { (uint8_t)MP_EVENT_SPAWN,          false, false, "an actor made" },
    { (uint8_t)MP_EVENT_DESPAWN,        false, false, "an actor removed" },
    { (uint8_t)MP_EVENT_SKIN,           true,  true,  "an appearance" },
    { (uint8_t)MP_EVENT_PICKUP,         true,  false, "a pickup claim" },
    { (uint8_t)MP_SCRATCH_TAG_BANK,     false, false, "the campaign bank" },
    { (uint8_t)MP_SCRATCH_TAG_AI,       false, false, "the campaign's AI flags" },
    { (uint8_t)MP_EVENT_USE,            false, false, "the retired use press" },
    { (uint8_t)MP_EVENT_HIT,            true,  false, "a hit report" },
    { (uint8_t)MP_ROSTER_TAG,           false, false, "the roster" },
    { (uint8_t)MP_EVENT_PLAYER_HIT,     false, false, "a hit on a far player" },
    { (uint8_t)MP_LOBBY_TAG,            true,  false, "a lobby choice" },
    { (uint8_t)MP_LOBBY_SETUP_TAG,      false, false, "the setup" },
    { (uint8_t)MP_LOBBY_CONTENT_TAG,    true,  false, "a content fingerprint" },
    { (uint8_t)MP_EVENT_DEATH,          true,  true,  "a death" },
    { (uint8_t)MP_SCORE_TAG,            false, false, "the score" },
    { (uint8_t)MP_WORLD_STATE_TAG,      false, false, "the map's corrections" },
    { (uint8_t)MP_TAKEN_TAG,            false, false, "what is gone" },
    { (uint8_t)MP_SAVEFILE_CHUNK_TAG,   false, false, "a savegame chunk" },
    { (uint8_t)MP_SAVEFILE_REQUEST_TAG, true,  false, "a savegame request" },
    { (uint8_t)MP_QUEST_STATE_TAG,      false, false, "the story" },
    { (uint8_t)MP_QUEST_CLAIM_TAG,      true,  false, "a quest claim" },
    { (uint8_t)MP_DIALOG_TAG,           false, false, "a spoken line" },
    { (uint8_t)MP_DIALOG_CHOICE_TAG,    false, false, "a conversation's choices" },
    { (uint8_t)MP_DIALOG_PICK_TAG,      false, false, "a conversation's answer" },
    { (uint8_t)MP_SAVEFILE_ACK_TAG,     true,  false, "a savegame acknowledgement" },
    { (uint8_t)MP_NPC_SHOT_TAG,         false, false, "an NPC's bolt" },
    { (uint8_t)MP_NPC_COPY_WISH_TAG,    true,  false, "a wish for an NPC copy" },
    { (uint8_t)MP_NPC_COPY_ENTRY_TAG,   false, false, "an NPC copy granted or refused" },
    { (uint8_t)MP_LEVEL_STATE_TAG,      false, false, "the level's state" },
    /* The host's scene: a client mirrors it and never says one. A note of this tag from a player
     * would lock every other player into a scene nobody runs. */
    { (uint8_t)MP_SCENE_NOTE_TAG,       false, false, "the host's scene" },
    { (uint8_t)MP_CRATE_NOTE_TAG,       false, false, "the host's push blocks" },
    /* A player may wish a push block somewhere; the host pushes it and nobody passes the wish
     * on, because the other players are told the block, not the wish. */
    { (uint8_t)MP_CRATE_PUSH_TAG,       true,  false, "a wish to push a block" },
    { (uint8_t)MP_CRATE_FALL_TAG,       false, false, "a push block starting to fall" },
    { (uint8_t)MP_EVENT_PLAYER_SOUND,   true,  true,  "a sound a player's body made" },
    /* A player says its line to the host, which stamps who said it and sends everybody the
     * line; the say itself goes on to nobody, and the line is never a player's to send. */
    { (uint8_t)MP_CHAT_SAY_TAG,         true,  false, "a line of chat said to the host" },
    { (uint8_t)MP_CHAT_LINE_TAG,        false, false, "the host's line of chat" },
    /* The host's world settings are the host's alone: from a player they would set every other
     * player's draw distance and cheats. */
    { (uint8_t)MP_HOST_SETTINGS_TAG,    false, false, "the host's world settings" },
};

#define TAG_CASES (sizeof TAGS / sizeof TAGS[0])

/* The rule as it stood before code 0x22 had a reason of its own, copied from the function it
 * replaced so the new verdict is held against it rather than against itself: a co-op game
 * performs every reported hit, a deathmatch only one on a copy an editor spawned. */
static bool old_rule_performs(uint32_t key)
{
    return !mp_trust_checking() || mp_wire_key_is_copy(key);
}

/* Every code on every kind of key in both games answers as the old rule did, with one exception:
 * a report with a wave's code, which the old rule performed, is refused now with a reason of its
 * own, because the host's copy of the same wave performs it. */
static void check_the_hits_a_host_performs(void)
{
    static const uint32_t keys[] = { 0u, 12u, 255u, 256u, 300u, 383u, 384u, 0xFFFFu };
    unsigned game;
    unsigned code;
    unsigned rows = 0u;
    unsigned wrong = 0u;
    unsigned moved = 0u;
    unsigned moved_off_the_wave = 0u;
    size_t   k;

    ut_section("which reported hits a host performs, against the rule it replaced");
    for (game = 0u; game < 2u; ++game) {
        mp_trust_set_checking(game != 0u);
        for (k = 0; k < sizeof keys / sizeof keys[0]; ++k) {
            for (code = 0u; code <= 0xFFu; ++code) {
                mp_trust_hit_verdict_t verdict = mp_trust_host_performs_hit(keys[k],
                                                                           (uint8_t)code);
                bool                   old     = old_rule_performs(keys[k]);
                mp_trust_hit_verdict_t want    = MP_TRUST_HIT_PERFORM;

                if (!old) {
                    want = MP_TRUST_HIT_DEATHMATCH;
                } else if (code == MP_TRUST_WAVE_CODE) {
                    want = MP_TRUST_HIT_HOST_WAVE;
                }
                if (verdict != want) {
                    if (wrong < 4u) {   /* the first few by name, the rest in the count */
                        ut_checkf(false, "%s, key %u, code 0x%02X: verdict %d, wanted %d",
                                  game != 0u ? "deathmatch" : "co-op", (unsigned)keys[k], code,
                                  (int)verdict, (int)want);
                    }
                    ++wrong;
                }
                if ((verdict == MP_TRUST_HIT_PERFORM) != old) {
                    ++moved;
                    moved_off_the_wave += code != MP_TRUST_WAVE_CODE ? 1u : 0u;
                }
                ++rows;
            }
        }
    }
    ut_checkf(wrong == 0u, "%u of %u rows (256 codes, 8 keys, 2 games) answer as they should",
              rows - wrong, rows);
    ut_checkf(moved == 11u && moved_off_the_wave == 0u,
              "only the wave's code moved off the old rule: %u row(s), %u of them another code "
              "(8 keys in co-op and 3 copies in a deathmatch)", moved, moved_off_the_wave);

    mp_trust_set_checking(false);
    ut_check(mp_trust_host_performs_hit(12u, 0x21u) == MP_TRUST_HIT_PERFORM &&
                 mp_trust_host_performs_hit(300u, 0x20u) == MP_TRUST_HIT_PERFORM,
             "in co-op a blade and a bolt are performed on a placement and on a copy");
    ut_check(mp_trust_host_performs_hit(12u, (uint8_t)MP_TRUST_WAVE_CODE) ==
                     MP_TRUST_HIT_HOST_WAVE &&
                 mp_trust_host_performs_hit(300u, (uint8_t)MP_TRUST_WAVE_CODE) ==
                     MP_TRUST_HIT_HOST_WAVE,
             "and a wave is not, with its own reason and not the deathmatch's");
    mp_trust_set_checking(true);
    ut_check(mp_trust_host_performs_hit(12u, (uint8_t)MP_TRUST_WAVE_CODE) ==
                 MP_TRUST_HIT_DEATHMATCH,
             "a deathmatch refuses a wave on a placement as it refused it before");
    ut_check(mp_trust_host_performs_hit(256u, 0x21u) == MP_TRUST_HIT_PERFORM &&
                 mp_trust_host_performs_hit(383u, (uint8_t)MP_TRUST_WAVE_CODE) ==
                     MP_TRUST_HIT_HOST_WAVE,
             "and on a copy performs a blade and leaves a wave to the host's own");
    ut_check(mp_trust_host_performs_hit(384u, 0x21u) == MP_TRUST_HIT_DEATHMATCH,
             "and nothing on a key past the copies");
    mp_trust_set_checking(false);
}

static void check_the_game(void)
{
    ut_section("which game checks its clients");
    ut_check(!mp_trust_checking(), "nobody is checked before a game is announced");
    mp_trust_set_checking(true);
    ut_check(mp_trust_checking(), "a deathmatch checks");
    mp_trust_set_checking(false);
    ut_check(!mp_trust_checking(), "and a co-op game trusts again");
}

static void check_the_tags(void)
{
    uint8_t  note[8];
    unsigned tag;
    unsigned gaps = 0u;
    size_t   i;

    ut_section("which tags a player may send, and which a dedicated server passes on");
    for (i = 0; i < TAG_CASES; ++i) {
        memset(note, 0, sizeof note);
        note[0] = TAGS[i].tag;
        ut_checkf(mp_trust_player_may_say(note, sizeof note) == TAGS[i].player_may_say,
                  "%s is %s", TAGS[i].name,
                  TAGS[i].player_may_say ? "a player's to say" : "a host's alone");
        ut_checkf(mp_trust_passes_between_players(note, sizeof note) == TAGS[i].passes_between,
                  "%s %s", TAGS[i].name,
                  TAGS[i].passes_between ? "goes on to the other players"
                                         : "stops at a dedicated server");
    }
    for (tag = 0x81u; tag <= MP_HOST_SETTINGS_TAG; ++tag) {
        size_t seen = 0;

        for (i = 0; i < TAG_CASES; ++i) {
            seen += TAGS[i].tag == tag ? 1u : 0u;
        }
        gaps += seen == 1u ? 0u : 1u;
    }
    ut_checkf(gaps == 0u, "the table names every tag from 0x81 to 0x%02X once: %u gap(s)",
              (unsigned)MP_HOST_SETTINGS_TAG, gaps);

    note[0] = 2u;
    ut_check(!mp_trust_player_may_say(note, 1u), "the slot byte is a host's");
    ut_check(!mp_trust_player_may_say(NULL, 0u) && !mp_trust_passes_between_players(NULL, 0u),
             "and nothing at all is nobody's");
}

static void check_a_player_speaks_for_itself(void)
{
    mp_death_note_t death;
    mp_event_t      event;
    uint8_t         note[MP_EVENT_MAX_BYTES];
    size_t          bytes;

    ut_section("a player speaks for its own slot, whatever the game");

    death.victim_slot = 2u;
    death.killer_slot = 1u;
    death.reason      = MP_DEATH_BY_HIT;
    ut_check(mp_death_encode(&death, note, MP_EVENT_DEATH_BYTES), "a death encodes");
    ut_check(mp_trust_speaks_for_itself(note, MP_EVENT_DEATH_BYTES, 2u),
             "its victim may report it");
    ut_check(!mp_trust_speaks_for_itself(note, MP_EVENT_DEATH_BYTES, 1u),
             "its killer may not: that is one player writing another's death");

    memset(&event, 0, sizeof event);
    event.kind      = MP_EVENT_SKIN;
    event.tick      = 1u;
    event.skin_kind = MP_SKIN_CHARACTER;
    event.skin_hero = 1u;
    event.skin_slot = 3u;
    memcpy(event.skin_asset, "mace.baf", sizeof "mace.baf");
    bytes = mp_event_encode(&event, note, sizeof note);
    ut_check(bytes != 0u && mp_trust_speaks_for_itself(note, bytes, 3u),
             "an appearance comes from the player whose body wears it");
    ut_check(!mp_trust_speaks_for_itself(note, bytes, 1u), "and from nobody else");

    memset(&event, 0, sizeof event);
    event.kind        = MP_EVENT_SHOT;
    event.tick        = 1u;
    event.source_slot = 3u;
    bytes = mp_event_encode(&event, note, sizeof note);
    ut_check(bytes != 0u && mp_trust_speaks_for_itself(note, bytes, 1u),
             "a moment's slot is the host's to write on the way, so a shot is held to nothing");
    ut_check(mp_trust_speaks_for_itself(NULL, 0u, 1u), "and nothing names nobody");

    ut_check(mp_trust_takes_from(note, bytes, 1u), "a host takes a player's shot");
    ut_check(mp_death_encode(&death, note, MP_EVENT_DEATH_BYTES) &&
                 !mp_trust_takes_from(note, MP_EVENT_DEATH_BYTES, 1u) &&
                 mp_trust_takes_from(note, MP_EVENT_DEATH_BYTES, 2u),
             "and a death only from its victim");
    note[0] = (uint8_t)MP_ROSTER_TAG;
    ut_check(!mp_trust_takes_from(note, MP_EVENT_DEATH_BYTES, 1u),
             "and no roster from anybody, which names no slot and is a host's alone");
}

/* ================================ A listen host and one client ================================ */

/* Static: a session is over two megabytes now that every peer carries a bulk ring. */
static mp_loopback_t     s_net;
static mp_session_t      s_host;
static mp_session_t      s_client;
static mp_transport_t    s_host_transport;
static mp_transport_t    s_client_transport;
static mp_bridge_drain_t s_drain;

static bool connect_pair(uint32_t *now)
{
    int tick;

    mp_loopback_init(&s_net, NULL, 0x7Eu);
    s_host_transport   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    s_client_transport = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(&s_host, MP_SESSION_HOST, &s_host_transport, 0x71u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_client_transport, 0x17u);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    for (tick = 0; tick < 200; ++tick) {
        *now += 16u;
        mp_loopback_pump(&s_net, *now);
        mp_session_update(&s_host, *now);
        mp_session_update(&s_client, *now);
        if (mp_session_is_connected(&s_client) && mp_session_peer_count(&s_host) == 1u) {
            mp_bridge_drain_bind(&s_drain, MP_BRIDGE_UDP_HOST, &s_host, &s_client);
            return true;
        }
    }
    return false;
}

/* A reliable message needs more than one pump to cross, so the host's drain runs after each. */
static void pump_and_drain(uint32_t *now)
{
    int round;
    int tick;

    for (round = 0; round < 4; ++round) {
        for (tick = 0; tick < 4; ++tick) {
            *now += 16u;
            mp_loopback_pump(&s_net, *now);
            mp_session_update(&s_host, *now);
            mp_session_update(&s_client, *now);
        }
        mp_bridge_drain_reliable_notes(&s_drain);
    }
}

static void client_says(const uint8_t *note, size_t bytes, const char *what)
{
    ut_checkf(mp_session_send_reliable(&s_client, 0, note, bytes), "the client sends %s", what);
}

static void check_the_listen_host_gate(void)
{
    uint32_t        now = 0;
    uint8_t         note[MP_EVENT_MAX_BYTES];
    mp_death_note_t death;

    ut_section("a listen host takes from a client only what a player says, about itself");
    ut_check(connect_pair(&now), "a host and a client over the loopback");

    /* Only a host sends a hit on a far player. From a client it is a client hurting another
     * player in the host's name. */
    memset(note, 0, sizeof note);
    note[0] = (uint8_t)MP_EVENT_PLAYER_HIT;
    note[1] = (uint8_t)MP_BRIDGE_HOST_SLOT;
    note[2] = 0xFFu;   /* the attacker's key, two bytes: nobody */
    note[3] = 0xFFu;
    client_says(note, MP_PLAYER_HIT_BYTES, "a hit on the host's own player");
    pump_and_drain(&now);
    ut_check(s_drain.notes_refused == 1u, "a hit on a far player from a client is refused");

    death.victim_slot = (uint8_t)MP_BRIDGE_HOST_SLOT;
    death.killer_slot = mp_session_slot_of_peer(0u);
    death.reason      = MP_DEATH_BY_HIT;
    ut_check(mp_death_encode(&death, note, MP_EVENT_DEATH_BYTES), "a death encodes");
    client_says(note, MP_EVENT_DEATH_BYTES, "a death it wrote for the host's player");
    pump_and_drain(&now);
    ut_check(s_drain.notes_refused == 2u, "so is a death the client wrote for somebody else");

    death.victim_slot = mp_session_slot_of_peer(0u);
    death.killer_slot = (uint8_t)MP_BRIDGE_HOST_SLOT;
    ut_check(mp_death_encode(&death, note, MP_EVENT_DEATH_BYTES), "its own death encodes");
    client_says(note, MP_EVENT_DEATH_BYTES, "its own death");
    pump_and_drain(&now);
    ut_check(s_drain.notes_refused == 2u, "while its own death goes through");
}

#define HOST_CONTENT   0xAAAA0001u
#define CLIENT_CONTENT 0xBBBB0002u

static void check_a_client_whose_content_differs(void)
{
    uint32_t now = 0;
    uint8_t  note[MP_LOBBY_CONTENT_BYTES];

    ut_section("a deathmatch host sends away a client whose content differs, a co-op one not");
    ut_check(mp_lobby_content_encode(CLIENT_CONTENT, note, sizeof note) == MP_LOBBY_CONTENT_BYTES,
             "the client's fingerprint encodes");

    ut_check(connect_pair(&now), "a host and a client");
    mp_bridge_content_bind(&s_host, &s_client, false);
    mp_bridge_content_reset();
    mp_trust_set_checking(false);
    mp_bridge_lobby_content_settled(HOST_CONTENT, true);
    client_says(note, sizeof note, "its fingerprint");
    pump_and_drain(&now);
    ut_check(mp_bridge_lobby_content_mismatch() && mp_session_peer_count(&s_host) == 1u,
             "a co-op host names the difference and keeps the client, which goes by itself");

    mp_trust_set_checking(true);
    client_says(note, sizeof note, "its fingerprint again");
    pump_and_drain(&now);
    ut_check(mp_session_peer_count(&s_host) == 0u, "a deathmatch host sends it away");
    ut_check(!mp_session_is_connected(&s_client) &&
                 mp_session_last_deny(&s_client) == MP_DENY_CONTENT,
             "and the client is told it was the content");

    /* The client speaks first, while the host has no level and so no fingerprint of its own. */
    ut_check(connect_pair(&now), "a host and a client again");
    mp_bridge_content_reset();
    client_says(note, sizeof note, "its fingerprint before the host has one");
    pump_and_drain(&now);
    ut_check(mp_session_peer_count(&s_host) == 1u, "a host with nothing to compare judges nobody");
    mp_bridge_lobby_content_settled(HOST_CONTENT, true);
    pump_and_drain(&now);
    ut_check(mp_session_peer_count(&s_host) == 0u &&
                 mp_session_last_deny(&s_client) == MP_DENY_CONTENT,
             "and sends the client away once its own is known");
    mp_trust_set_checking(false);
}

/* A digest measures ONE machine's map. A client that steers its own free runners from it has to
 * know whose map it was: passed between two clients of a dedicated server, which has no map at
 * all, the two would measure each other and chase a drift neither of them owns. */
static void check_the_digest_is_not_passed_on(void)
{
    uint8_t digest[8];

    ut_section("a map digest travels between a player and the side that holds the map");
    memset(digest, 0, sizeof digest);
    digest[0] = (uint8_t)MP_WORLD_DIGEST_TAG;
    ut_check(!mp_trust_passes_between_players(digest, sizeof digest),
             "and no further: a server passing it on would have two clients correcting each other");
}

int main(void)
{
    check_the_game();
    check_the_hits_a_host_performs();
    check_the_tags();
    check_a_player_speaks_for_itself();
    check_the_listen_host_gate();
    check_a_client_whose_content_differs();
    check_the_digest_is_not_passed_on();
    return ut_summary("mp_trust");
}
