/* mp_fuzz_notes.c: every decoder above the network layer fed what no honest peer sends.
 *
 * mp_fuzz.c drives the network layer's decoders: the session, the channel, the body, the
 * snapshot, the command and the moments. The notes that ride on top of them had none: the
 * savegame's slice and mask, the announce, the lobby's three, the roster, the death, the score,
 * the taken list, the story's two, the conversation's two, the map's digest and state, the
 * level's state with every part it can carry, a player's own sound, the host's world settings,
 * the campaign bank and blackboard, the enemy record, the host payload's split into the enemy
 * block and the snapshot, and the three hit messages. Each is fed random bytes and its own valid
 * notes after bit flips, truncations and runs copied over one another, and sometimes random bytes
 * behind its own tag, so the input gets past the recogniser.
 *
 * The property for a codec that encodes what it decodes is that a note it accepts says something
 * the encoder can say again, and that saying it again settles in one round: decode, encode,
 * decode, encode gives the same bytes twice. A decoder that accepted a value its own encoder
 * refuses could not pass it on, and one whose round trip moved would disagree with itself about
 * what a note means. The rest get the property that matters for each: the campaign bank is never
 * written outside itself and never half, a savegame slice's payload and a mask lie inside the note
 * that carried them, an enemy record never claims more bytes than it was handed, every body a host
 * payload yields is finite, and the hit relay claims exactly its own three messages.
 *
 * SIZE NOTE: over 600 lines because every note decoder above the network layer is fuzzed from
 * this one file, each with its adapter and a valid note to mutate. The seam, should it grow, is
 * the host payload with its session pair, which shares nothing with the codec halves.
 */
#include "unittest.h"

#include "mp_fuzz_input.h"

#include "mp_announce.h"
#include "mp_bridge_world.h"
#include "mp_death.h"
#include "mp_dialog.h"
#include "mp_enemy_wire.h"
#include "mp_events.h"
#include "mp_hit_relay.h"
#include "mp_host_settings_rule.h"
#include "mp_level_state_rule.h"
#include "mp_lobby.h"
#include "mp_loopback.h"
#include "mp_quest.h"
#include "mp_roster.h"
#include "mp_rules.h"
#include "mp_savefile.h"
#include "mp_score.h"
#include "mp_scratch.h"
#include "mp_session.h"
#include "mp_snapshot.h"
#include "mp_taken.h"
#include "mp_transport.h"
#include "mp_wire.h"
#include "mp_world.h"
#include "mp_world_state.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Room for the longest note here and the tail a mutation may put behind it. */
#define NOTE_CAP  4096u
#define NOTE_TAIL 64u

static bool finite3(const float *v)
{
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

/* One input for round `i` out of a valid note: the note itself first, then half the time the note
 * mutated, and otherwise random bytes up to the note's length and a tail, half of them behind the
 * note's own first byte. */
static size_t next_input(unsigned i, const uint8_t *valid, size_t valid_len, uint8_t *bytes)
{
    size_t len;

    memcpy(bytes, valid, valid_len);
    if (i == 0u) {
        return valid_len;
    }
    if ((next_random() & 1u) != 0u) {
        return mutate(bytes, valid_len, valid_len + NOTE_TAIL);
    }
    len = random_below(valid_len + NOTE_TAIL + 1u);
    fill_random(bytes, len);
    if (len != 0u && (next_random() & 1u) != 0u) {
        bytes[0] = valid[0];
    }
    return len;
}

/* ============================= The codecs that say again what they read ===================== */

typedef struct quest_state {
    mp_quest_set_t set;
    uint16_t       level;
} quest_state_t;

typedef struct quest_claim {
    uint32_t index;
    bool     value;
    uint16_t level;
} quest_claim_t;

typedef struct dialog_pick {
    uint16_t level;
    uint16_t line;
} dialog_pick_t;

typedef union any_note {
    mp_announce_t         announce;
    mp_lobby_t            lobby;
    mp_lobby_setup_t      setup;
    uint32_t              content;
    mp_roster_t           roster;
    mp_death_note_t       death;
    mp_score_board_t      board;
    mp_taken_t            taken;
    quest_state_t         quest_state;
    quest_claim_t         quest_claim;
    mp_dialog_line_t      line;
    dialog_pick_t         pick;
    mp_world_digest_t     digest;
    mp_world_state_note_t state;
    mp_level_state_note_t level_state;
    mp_event_t            moment;
    mp_host_settings_note_t host_settings;
} any_note_t;

typedef bool (*decode_fn)(const uint8_t *in, size_t len, any_note_t *out);
typedef size_t (*encode_fn)(const any_note_t *in, uint8_t *out, size_t cap);

static bool dec_announce(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_announce_decode(in, len, &out->announce);
}

static size_t enc_announce(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_announce_encode(&in->announce, out, cap);
}

static bool dec_lobby(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_lobby_decode(in, len, &out->lobby);
}

static size_t enc_lobby(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_lobby_encode(&in->lobby, out, cap);
}

static bool dec_setup(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_lobby_setup_decode(in, len, &out->setup);
}

static size_t enc_setup(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_lobby_setup_encode(&in->setup, out, cap);
}

static bool dec_content(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_lobby_content_decode(in, len, &out->content);
}

static size_t enc_content(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_lobby_content_encode(in->content, out, cap);
}

static bool dec_roster(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_roster_decode(in, len, &out->roster);
}

static size_t enc_roster(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_roster_encode(&in->roster, out, cap);
}

static bool dec_death(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_death_decode(in, len, &out->death);
}

static size_t enc_death(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_death_encode(&in->death, out, cap) ? (size_t)MP_EVENT_DEATH_BYTES : 0u;
}

static bool dec_board(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_score_decode(in, len, &out->board);
}

static size_t enc_board(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_score_encode(&in->board, out, cap);
}

static bool dec_taken(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_taken_decode(in, len, &out->taken);
}

static size_t enc_taken(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_taken_encode(&in->taken, out, cap);
}

static bool dec_quest_state(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_quest_decode_state(in, len, &out->quest_state.set, &out->quest_state.level);
}

static size_t enc_quest_state(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_quest_encode_state(&in->quest_state.set, in->quest_state.level, out, cap);
}

static bool dec_quest_claim(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_quest_decode_claim(in, len, &out->quest_claim.index, &out->quest_claim.value,
                                 &out->quest_claim.level);
}

static size_t enc_quest_claim(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_quest_encode_claim(in->quest_claim.index, in->quest_claim.value,
                                 in->quest_claim.level, out, cap);
}

static bool dec_line(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_dialog_decode(in, len, &out->line);
}

static size_t enc_line(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_dialog_encode(&in->line, out, cap);
}

static bool dec_pick(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_dialog_decode_pick(in, len, &out->pick.level, &out->pick.line);
}

static size_t enc_pick(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_dialog_encode_pick(in->pick.level, in->pick.line, out, cap);
}

static bool dec_digest(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_world_digest_decode(in, len, &out->digest);
}

static size_t enc_digest(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_world_digest_encode(&in->digest, out, cap);
}

static bool dec_state(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_world_state_decode(in, len, &out->state);
}

static size_t enc_state(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_world_state_encode(&in->state, out, cap);
}

static bool dec_level_state(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_level_state_decode(in, len, &out->level_state);
}

static size_t enc_level_state(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_level_state_encode(&in->level_state, out, cap);
}

static bool dec_moment(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_event_decode(in, len, &out->moment);
}

static size_t enc_moment(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_event_encode(&in->moment, out, cap);
}

static bool dec_settings(const uint8_t *in, size_t len, any_note_t *out)
{
    return mp_host_settings_decode(in, len, &out->host_settings) == MP_HOST_SETTINGS_TAKEN;
}

static size_t enc_settings(const any_note_t *in, uint8_t *out, size_t cap)
{
    return mp_host_settings_encode(&in->host_settings, out, cap);
}

static any_note_t s_first;
static any_note_t s_second;
static uint8_t    s_valid[NOTE_CAP];
static uint8_t    s_bytes[NOTE_CAP];
static uint8_t    s_again[NOTE_CAP];
static uint8_t    s_third[NOTE_CAP];

/* The note in `s_first` as its encoder writes it, into `s_valid`; the note every round of the codec
 * below starts from. */
static size_t make_valid(encode_fn encode, const char *name)
{
    size_t len = encode(&s_first, s_valid, sizeof s_valid - NOTE_TAIL);

    ut_checkf(len != 0u, "%s: a valid note to start from encodes, %u byte(s)", name, (unsigned)len);
    return len;
}

static void round_trip(const char *name, decode_fn decode, encode_fn encode, size_t valid_len)
{
    unsigned i;
    unsigned accepted = 0u;
    unsigned unsayable = 0u;
    unsigned moved = 0u;

    for (i = 0; valid_len != 0u && i < ROUNDS; ++i) {
        size_t len = next_input(i, s_valid, valid_len, s_bytes);
        size_t once;
        size_t twice;

        memset(&s_first, 0, sizeof s_first);
        if (!decode(s_bytes, len, &s_first)) {
            continue;
        }
        ++accepted;
        once = encode(&s_first, s_again, sizeof s_again);
        if (once == 0u) {
            ++unsayable;
            continue;
        }
        memset(&s_second, 0, sizeof s_second);
        twice = decode(s_again, once, &s_second) ? encode(&s_second, s_third, sizeof s_third) : 0u;
        if (twice != once || memcmp(s_again, s_third, once) != 0) {
            ++moved;
        }
    }
    ut_checkf(accepted != 0u, "%s: %u of %u inputs accepted, the valid one among them", name,
              accepted, ROUNDS);
    ut_checkf(unsayable == 0u && moved == 0u,
              "%s: every accepted note is said again, and alike (%u could not be, %u moved)", name,
              unsayable, moved);
}

/* The note in `s_first` made valid, and its codec driven over it. */
static void round_trip_from_valid(const char *name, decode_fn decode, encode_fn encode)
{
    round_trip(name, decode, encode, make_valid(encode, name));
}

static void check_the_round_trips(void)
{
    ut_section("a note a codec accepts, it says again, and alike");

    memset(&s_first, 0, sizeof s_first);
    s_first.announce.wire        = (uint8_t)MP_WIRE_VERSION;
    s_first.announce.fingerprint = 0x1234ABCDu;
    s_first.announce.game_port   = 7777u;
    s_first.announce.players     = 2u;
    s_first.announce.slots       = 4u;
    s_first.announce.flags       = (uint8_t)MP_ANNOUNCE_F_TDM;
    mp_announce_name_clean("a host", s_first.announce.name);
    round_trip_from_valid("the announce", &dec_announce, &enc_announce);

    memset(&s_first, 0, sizeof s_first);
    s_first.lobby.team  = 1u;
    s_first.lobby.ready = 1u;
    s_first.lobby.hero  = 2u;
    round_trip_from_valid("a lobby choice", &dec_lobby, &enc_lobby);

    memset(&s_first, 0, sizeof s_first);
    s_first.setup.mode        = (uint8_t)MP_LOBBY_MODE_TDM;
    s_first.setup.flags       = (uint8_t)MP_LOBBY_F_STARTED;
    s_first.setup.level_index = 3u;
    s_first.setup.generation  = 9u;
    mp_lobby_clean_field("level\\swamp.b3d", s_first.setup.level, sizeof s_first.setup.level);
    mp_lobby_clean_field("Swamp", s_first.setup.title, sizeof s_first.setup.title);
    mp_rules_default(&s_first.setup.rules);
    round_trip_from_valid("the setup", &dec_setup, &enc_setup);

    /* And with the host's difficulty in its last byte: six, and the highest it says. */
    s_first.setup.host_difficulty = 6u;
    round_trip_from_valid("the setup with a difficulty", &dec_setup, &enc_setup);
    memset(&s_first, 0, sizeof s_first);
    s_first.setup.mode            = (uint8_t)MP_LOBBY_MODE_COOP;
    s_first.setup.flags           = (uint8_t)MP_LOBBY_F_STARTED;
    s_first.setup.host_difficulty = (uint8_t)MP_LOBBY_DIFFICULTY_MAX;
    mp_lobby_clean_field("level\\fedship.b3d", s_first.setup.level, sizeof s_first.setup.level);
    mp_rules_default(&s_first.setup.rules);
    round_trip_from_valid("the setup with difficulty 9", &dec_setup, &enc_setup);

    memset(&s_first, 0, sizeof s_first);
    s_first.content = 0xC0FFEE11u;
    round_trip_from_valid("a content fingerprint", &dec_content, &enc_content);

    memset(&s_first, 0, sizeof s_first);
    s_first.roster.count          = 2u;
    s_first.roster.entry[0].slot  = 0u;
    s_first.roster.entry[0].team  = 1u;
    s_first.roster.entry[0].ready = 1u;
    mp_roster_name_clean("host", s_first.roster.entry[0].name);
    mp_roster_asset_clean("mace.baf", s_first.roster.entry[0].asset);
    s_first.roster.entry[0].asset_kind = MP_SKIN_MODEL;
    s_first.roster.entry[1].slot   = 1u;
    s_first.roster.entry[1].team   = 2u;
    s_first.roster.entry[1].hero   = 1u;
    s_first.roster.entry[1].rtt_ms = 45u;
    mp_roster_name_clean("client", s_first.roster.entry[1].name);
    round_trip_from_valid("the roster", &dec_roster, &enc_roster);

    memset(&s_first, 0, sizeof s_first);
    s_first.death.victim_slot = 1u;
    s_first.death.killer_slot = 2u;
    s_first.death.reason      = MP_DEATH_BY_HIT;
    round_trip_from_valid("a death", &dec_death, &enc_death);

    memset(&s_first, 0, sizeof s_first);
    s_first.board.generation     = 3u;
    s_first.board.count          = 2u;
    s_first.board.outcome        = (uint8_t)MP_SCORE_RUNNING;
    s_first.board.winner         = (uint8_t)MP_SCORE_NOBODY;
    s_first.board.elapsed        = 1234u;
    s_first.board.line[0].slot   = 0u;
    s_first.board.line[0].team   = 1u;
    s_first.board.line[0].points = 5;
    s_first.board.line[0].deaths = 2u;
    s_first.board.line[1].slot   = 1u;
    s_first.board.line[1].team   = 2u;
    s_first.board.line[1].points = -1;
    s_first.board.line[1].deaths = 7u;
    round_trip_from_valid("the score", &dec_board, &enc_board);

    memset(&s_first, 0, sizeof s_first);
    s_first.taken.level = 7u;
    (void)mp_taken_add(&s_first.taken, 4u);
    (void)mp_taken_add(&s_first.taken, 9u);
    (void)mp_taken_add(&s_first.taken, 200u);
    round_trip_from_valid("what is gone", &dec_taken, &enc_taken);
}

static void check_the_story_and_world_round_trips(void)
{
    memset(&s_first, 0, sizeof s_first);
    mp_quest_put(&s_first.quest_state.set, 3u, true);
    mp_quest_put(&s_first.quest_state.set, 20u, true);
    s_first.quest_state.level = 5u;
    round_trip_from_valid("the story", &dec_quest_state, &enc_quest_state);

    memset(&s_first, 0, sizeof s_first);
    s_first.quest_claim.index = 7u;
    s_first.quest_claim.value = true;
    s_first.quest_claim.level = 5u;
    round_trip_from_valid("a quest claim", &dec_quest_claim, &enc_quest_claim);

    memset(&s_first, 0, sizeof s_first);
    s_first.line.level        = 5u;
    s_first.line.line         = 42u;
    s_first.line.has_position = true;
    s_first.line.position[0]  = 1.0f;
    s_first.line.position[1]  = -2.0f;
    s_first.line.position[2]  = 3.5f;
    round_trip_from_valid("a spoken line", &dec_line, &enc_line);

    memset(&s_first, 0, sizeof s_first);
    s_first.pick.level = 5u;
    s_first.pick.line  = 42u;
    round_trip_from_valid("a conversation's answer", &dec_pick, &enc_pick);

    memset(&s_first, 0, sizeof s_first);
    mp_world_digest_init(&s_first.digest, 64u);
    mp_world_digest_set_level(&s_first.digest, 40u);
    (void)mp_world_digest_add(&s_first.digest, 3u, MP_WORLD_TYPE_ALWAYS_ON, 0u, 1u, 0.5f, 2.0f,
                              0.25f);
    round_trip_from_valid("the map's digest", &dec_digest, &enc_digest);

    memset(&s_first, 0, sizeof s_first);
    s_first.state.tick            = 64u;
    s_first.state.level           = 40u;
    s_first.state.generation      = 2u;
    s_first.state.count           = 1u;
    s_first.state.entry[0].id     = 3u;
    s_first.state.entry[0].type   = (uint8_t)MP_WORLD_TYPE_ALWAYS_ON;
    s_first.state.entry[0].active = 1u;
    s_first.state.entry[0].pose   = 0.5f;
    s_first.state.entry[0].dwell  = 0.25f;
    round_trip_from_valid("the map's corrections", &dec_state, &enc_state);

    memset(&s_first, 0, sizeof s_first);
    mp_level_state_note_init(&s_first.level_state, 64u, 40u, 2u);
    s_first.level_state.parts = (uint8_t)(MP_LEVEL_STATE_PART_LIGHTS | MP_LEVEL_STATE_PART_FOG |
                                          MP_LEVEL_STATE_PART_ACTOR_LOOPS |
                                          MP_LEVEL_STATE_PART_FOG_VIEWERS |
                                          MP_LEVEL_STATE_PART_ESCORT |
                                          MP_LEVEL_STATE_PART_JOURNAL);
    s_first.level_state.lights               = 9u;
    s_first.level_state.fog.flags            = (uint8_t)(MP_LEVEL_FOG_RAMP | MP_LEVEL_FOG_COLOUR);
    s_first.level_state.fog.left             = 20u;
    s_first.level_state.loops                = 1u;
    s_first.level_state.loop[0].key          = 17u;
    s_first.level_state.loop[0].call         = 1100u;
    s_first.level_state.fog_viewers          = 1u;
    s_first.level_state.fog_viewer[0].key    = 44u;
    s_first.level_state.fog_viewer[0].flags  = (uint8_t)MP_LEVEL_STATE_FOG_VIEWER_ACTIVE;
    s_first.level_state.escort_health        = 80u;
    s_first.level_state.journal_newest       = 2u;
    s_first.level_state.journal_count        = 2u;
    s_first.level_state.journal[0].sequence  = 1u;
    s_first.level_state.journal[0].kind      = (uint8_t)MP_LEVEL_JOURNAL_CRAWL;
    s_first.level_state.journal[0].b         = 42u;
    s_first.level_state.journal[1].sequence  = 2u;
    s_first.level_state.journal[1].kind      = (uint8_t)MP_LEVEL_JOURNAL_ESCORT;
    s_first.level_state.journal[1].a         = 80u;
    round_trip_from_valid("the level's state", &dec_level_state, &enc_level_state);

    memset(&s_first, 0, sizeof s_first);
    s_first.moment.kind        = MP_EVENT_PLAYER_SOUND;
    s_first.moment.tick        = 64u;
    s_first.moment.source_slot = 2u;
    s_first.moment.sound_what  = MP_PLAYER_SOUND_GROUND;
    s_first.moment.sound_index = 52u;
    round_trip_from_valid("a player's own sound", &dec_moment, &enc_moment);

    memset(&s_first, 0, sizeof s_first);
    s_first.host_settings.cheats    = (uint8_t)MP_HOST_SETTINGS_CHEAT_EVIL_FORCE;
    s_first.host_settings.present   = 0x000Du;   /* the fog band not named */
    s_first.host_settings.values[0] = 2.25f;
    s_first.host_settings.values[2] = 1.0f;
    s_first.host_settings.values[3] = 1.0f;
    round_trip_from_valid("the host's world settings", &dec_settings, &enc_settings);
}

/* ============================= The codecs with a property of their own ====================== */

static uint8_t s_file[3000];

static void check_the_savegame_slice_and_mask(void)
{
    uint8_t             mask[MP_SAVEFILE_MASK_BYTES_FOR(3u)];
    mp_savefile_chunk_t chunk;
    const uint8_t      *seen = NULL;
    uint32_t            file_id = 0;
    uint16_t            count = 0;
    size_t              valid_len;
    unsigned            i;
    unsigned            accepted = 0u;
    unsigned            outside = 0u;

    ut_section("a savegame slice and a mask lie inside the note that carried them");
    fill_random(s_file, sizeof s_file);
    valid_len = mp_savefile_chunk_encode(0x1234u, s_file, (uint32_t)sizeof s_file, 1u, s_valid,
                                         sizeof s_valid - NOTE_TAIL);
    ut_check(valid_len != 0u, "a slice to start from encodes");
    for (i = 0; valid_len != 0u && i < ROUNDS; ++i) {
        size_t len = next_input(i, s_valid, valid_len, s_bytes);

        if (!mp_savefile_chunk_decode(s_bytes, len, &chunk)) {
            continue;
        }
        ++accepted;
        if (chunk.index >= chunk.count || chunk.bytes > MP_SAVEFILE_CHUNK_PAYLOAD ||
            chunk.payload < s_bytes || chunk.payload + chunk.bytes > s_bytes + len ||
            (size_t)MP_SAVEFILE_CHUNK_HEAD + chunk.bytes != len) {
            ++outside;
        }
    }
    ut_checkf(accepted != 0u && outside == 0u,
              "every accepted slice names one of its count and its bytes inside the note (%u of %u "
              "accepted, %u not)", accepted, ROUNDS, outside);

    memset(mask, 0, sizeof mask);
    mask[0] = 0x05u;
    valid_len = mp_savefile_ack_encode(0x1234u, 3u, mask, s_valid, sizeof s_valid - NOTE_TAIL);
    ut_check(valid_len != 0u, "a mask to start from encodes");
    accepted = 0u;
    outside  = 0u;
    for (i = 0; valid_len != 0u && i < ROUNDS; ++i) {
        size_t len = next_input(i, s_valid, valid_len, s_bytes);

        if (!mp_savefile_ack_decode(s_bytes, len, &file_id, &count, &seen)) {
            continue;
        }
        ++accepted;
        if (count > MP_SAVEFILE_MAX_CHUNKS || seen == NULL || seen < s_bytes ||
            seen + MP_SAVEFILE_MASK_BYTES_FOR(count) > s_bytes + len) {
            ++outside;
        }
    }
    ut_checkf(accepted != 0u && outside == 0u,
              "every accepted mask lies inside its note (%u of %u accepted, %u not)", accepted,
              ROUNDS, outside);
}

/* The bank sits between two guards, so a run that wrote past either end shows. */
#define GUARD 16u

static uint8_t s_mirror[MP_SCRATCH_BANK_BYTES];
static uint8_t s_live[MP_SCRATCH_BANK_BYTES];
static uint8_t s_bank[GUARD + MP_SCRATCH_BANK_BYTES + GUARD];
static uint8_t s_bank_before[GUARD + MP_SCRATCH_BANK_BYTES + GUARD];

static void check_the_campaign(void)
{
    mp_scratch_cursor_t cursor = { 0u, 0u };
    mp_scratch_cursor_t after;
    mp_scratch_ai_t     ai;
    int32_t             flag[MP_SCRATCH_AI_SLOTS];
    int32_t             previous[MP_SCRATCH_AI_SLOTS];
    float               expiry[MP_SCRATCH_AI_SLOTS];
    size_t              valid_len = 0;
    size_t              written = 0;
    uint32_t            at;
    unsigned            i;
    unsigned            k;
    unsigned            accepted = 0u;
    unsigned            broken = 0u;

    ut_section("the campaign bank is never written outside itself, nor half, nor a hero's");
    memset(s_mirror, 0, sizeof s_mirror);
    memset(s_live, 0, sizeof s_live);
    for (at = 0u, k = 0u; at < MP_SCRATCH_BANK_BYTES && k < 6u; at += 97u) {
        if (!mp_scratch_is_per_hero(at)) {
            s_live[at] = (uint8_t)(0x40u + k++);
        }
    }
    ut_check(mp_scratch_encode_bank(s_mirror, s_live, &cursor, s_valid, sizeof s_valid - NOTE_TAIL,
                                    &valid_len, &after) && valid_len != 0u,
             "a run list to start from encodes");
    for (i = 0; valid_len != 0u && i < ROUNDS; ++i) {
        size_t len = next_input(i, s_valid, valid_len, s_bytes);
        bool   ok;

        memset(s_bank, 0xA5, sizeof s_bank);
        memcpy(s_bank_before, s_bank, sizeof s_bank);
        ok = mp_scratch_decode_bank(s_bank + GUARD, s_bytes, len, &written);
        for (k = 0u; k < GUARD; ++k) {
            if (s_bank[k] != 0xA5u || s_bank[GUARD + MP_SCRATCH_BANK_BYTES + k] != 0xA5u) {
                ++broken;
                break;
            }
        }
        if (!ok) {
            broken += memcmp(s_bank, s_bank_before, sizeof s_bank) != 0 ? 1u : 0u;
            continue;
        }
        ++accepted;
        broken += written > MP_SCRATCH_BANK_BYTES ? 1u : 0u;
        for (at = 0u; at < MP_SCRATCH_BANK_BYTES; ++at) {
            if (mp_scratch_is_per_hero(at) && s_bank[GUARD + at] != 0xA5u) {
                ++broken;
                break;
            }
        }
    }
    ut_checkf(accepted != 0u && broken == 0u,
              "%u of %u run lists applied, %u wrote where they may not or half", accepted, ROUNDS,
              broken);

    ut_section("the blackboard hands back only times that are a time");
    for (k = 0u; k < MP_SCRATCH_AI_SLOTS; ++k) {
        flag[k]     = (int32_t)k;
        previous[k] = -(int32_t)k;
        expiry[k]   = 12.0f + (float)k;
    }
    valid_len = mp_scratch_encode_ai(flag, previous, expiry, 10.0f, s_valid,
                                     sizeof s_valid - NOTE_TAIL);
    ut_check(valid_len != 0u, "a blackboard to start from encodes");
    accepted = 0u;
    broken   = 0u;
    for (i = 0; valid_len != 0u && i < ROUNDS; ++i) {
        size_t len = next_input(i, s_valid, valid_len, s_bytes);

        memset(&ai, 0, sizeof ai);
        if (!mp_scratch_decode_ai(s_bytes, len, &ai)) {
            continue;
        }
        ++accepted;
        for (k = 0u; k < MP_SCRATCH_AI_SLOTS; ++k) {
            broken += (!isfinite(ai.remaining[k]) || ai.remaining[k] < 0.0f) ? 1u : 0u;
        }
    }
    ut_checkf(accepted != 0u && broken == 0u,
              "%u of %u accepted, %u remaining time(s) not finite or below nought", accepted,
              ROUNDS, broken);
}

static void check_the_enemy_record(void)
{
    mp_enemy_record_t record;
    mp_enemy_record_t out;
    size_t            valid_len = 0;
    size_t            used = 0;
    unsigned          i;
    unsigned          accepted = 0u;
    unsigned          overran = 0u;

    ut_section("an enemy record never claims more bytes than it was handed");
    memset(&record, 0, sizeof record);
    record.value[MP_ENEMY_F_INDEX]      = 5u;
    record.value[MP_ENEMY_F_GENERATION] = 1u;
    record.value[MP_ENEMY_F_POS_X]      = 100u;
    record.value[MP_ENEMY_F_HEALTH]     = 50u;
    ut_check(mp_enemy_wire_encode(&record, NULL, s_valid, sizeof s_valid - NOTE_TAIL, &valid_len) &&
                 valid_len != 0u,
             "a record to start from encodes");
    for (i = 0; valid_len != 0u && i < ROUNDS; ++i) {
        size_t len = next_input(i, s_valid, valid_len, s_bytes);

        used = 0u;
        if (!mp_enemy_wire_decode(s_bytes, len, (i & 1u) != 0u ? &record : NULL, &out, &used)) {
            continue;
        }
        ++accepted;
        overran += (used == 0u || used > len) ? 1u : 0u;
    }
    ut_checkf(accepted != 0u && overran == 0u,
              "%u of %u accepted, %u claimed nothing or more than the buffer", accepted, ROUNDS,
              overran);
}

/* ============================= The host payload, through a session ========================== */

/* Each round crosses the loopback, so fewer of them; still thousands of payloads. */
#define PAYLOAD_ROUNDS 3000u

static mp_loopback_t  s_net;
static mp_session_t   s_host;
static mp_session_t   s_client;
static mp_transport_t s_host_transport;
static mp_transport_t s_client_transport;

static bool connect_pair(uint32_t *now)
{
    int tick;

    mp_loopback_init(&s_net, NULL, 0x3Du);
    s_host_transport   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    s_client_transport = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(&s_host, MP_SESSION_HOST, &s_host_transport, 0xD3u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &s_client_transport, 0x3Du);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    for (tick = 0; tick < 200; ++tick) {
        *now += 16u;
        mp_loopback_pump(&s_net, *now);
        mp_session_update(&s_host, *now);
        mp_session_update(&s_client, *now);
        if (mp_session_is_connected(&s_client) && mp_session_peer_count(&s_host) == 1u) {
            return true;
        }
    }
    return false;
}

static void check_the_host_payload(void)
{
    static uint8_t payload[MP_SESSION_PAYLOAD_BYTES];
    mp_snapshot_t  world;
    mp_snapshot_t  out;
    mp_wire_body_t body;
    uint32_t       now = 0;
    size_t         valid_len = 0;
    size_t         slot;
    unsigned       i;
    unsigned       decoded = 0u;
    unsigned       refused = 0u;
    unsigned       not_finite = 0u;
    int            tick;

    ut_section("a host payload splits into the enemy block and a world, or into nothing");
    mp_bridge_world_reset();
    ut_check(connect_pair(&now), "a host and a client over the loopback");

    /* The shape the host sends: a two byte length, the enemy block, then the world. */
    memset(&body, 0, sizeof body);
    body.position[0] = 3.0f;
    body.alive       = true;
    mp_snapshot_clear(&world);
    world.tick = 1u;
    mp_snapshot_set_body(&world, 0, &body);
    payload[0] = 0u;
    payload[1] = 0u;
    ut_check(mp_snapshot_encode(&world, NULL, payload + 2u, sizeof payload - 2u - NOTE_TAIL,
                                &valid_len),
             "a world to start from encodes");
    valid_len += 2u;
    memcpy(s_valid, payload, valid_len);

    for (i = 0; i < PAYLOAD_ROUNDS; ++i) {
        size_t                 len = next_input(i, s_valid, valid_len, payload);
        mp_bridge_world_read_t read;

        (void)mp_session_set_payload(&s_host, 0, payload, len);
        for (tick = 0; tick < 2; ++tick) {
            now += 16u;
            mp_loopback_pump(&s_net, now);
            mp_session_update(&s_host, now);
            mp_session_update(&s_client, now);
        }
        while ((read = mp_bridge_world_receive(&s_client, &out)) != MP_BRIDGE_WORLD_NOTHING) {
            if (read != MP_BRIDGE_WORLD_DECODED) {
                ++refused;
                continue;
            }
            ++decoded;
            for (slot = 0; slot < MP_SNAPSHOT_MAX_BODIES; ++slot) {
                if (mp_snapshot_has_body(&out, slot) &&
                    (!finite3(out.body[slot].position) || !finite3(out.body[slot].orientation))) {
                    ++not_finite;
                }
            }
        }
    }
    ut_checkf(decoded != 0u && refused != 0u && not_finite == 0u,
              "%u world(s) decoded and %u refused out of %u payloads, %u body or bodies not finite",
              decoded, refused, PAYLOAD_ROUNDS, not_finite);
    mp_session_disconnect(&s_client);
    mp_session_disconnect(&s_host);
}

/* ============================= The hit messages ============================================== */

static void check_the_hit_messages(void)
{
    uint8_t        *note = s_bytes;
    mp_death_note_t death;
    unsigned        i;
    unsigned        wrong = 0u;
    unsigned        claimed = 0u;

    ut_section("the hit relay claims exactly its three messages, whatever it is handed");
    mp_hit_relay_set_death_listener(NULL);
    death.victim_slot = 1u;
    death.killer_slot = 2u;
    death.reason      = MP_DEATH_BY_HIT;
    ut_check(mp_death_encode(&death, s_valid, MP_EVENT_DEATH_BYTES), "a death to start from");
    for (i = 0; i < ROUNDS; ++i) {
        size_t len = next_input(i, s_valid, MP_EVENT_DEATH_BYTES, note);
        bool   ours;

        switch (next_random() % 4u) {
        case 0:
            note[0] = (uint8_t)MP_EVENT_HIT;
            break;
        case 1:
            note[0] = (uint8_t)MP_EVENT_PLAYER_HIT;
            break;
        default:
            break;
        }
        ours = mp_death_is(note, len) ||
               (len == MP_HIT_RELAY_BYTES && note[0] == (uint8_t)MP_EVENT_HIT) ||
               (len == MP_PLAYER_HIT_BYTES && note[0] == (uint8_t)MP_EVENT_PLAYER_HIT);
        if (mp_hit_relay_take_message(1u, note, len) != ours) {
            ++wrong;
        }
        claimed += ours ? 1u : 0u;
    }
    ut_checkf(claimed != 0u && wrong == 0u,
              "%u of %u claimed, %u claimed or left against their tag and length", claimed,
              ROUNDS, wrong);
}

int main(void)
{
    check_the_round_trips();
    check_the_story_and_world_round_trips();
    check_the_savegame_slice_and_mask();
    check_the_campaign();
    check_the_enemy_record();
    check_the_host_payload();
    check_the_hit_messages();
    return ut_summary("mp_fuzz_notes");
}
