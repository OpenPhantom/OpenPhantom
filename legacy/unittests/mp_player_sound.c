/* mp_player_sound.c: a player's own sounds and shield, from the engine's call on one machine to
 * the engine's entry at the puppet on another.
 *
 * SIZE NOTE: over 600 lines. A third of it is the engine and the machine the test plays, which
 * every section shares; the seam, when it grows, is that third into a file of its own, as
 * mp_body_gate_neighbours is for the fan test.
 *
 * Both halves are the real modules. What the test plays is the engine around them: the sites the
 * pattern table would have found, four call instructions to repoint, the sound entries, the burst
 * and the shield pool, the tables of names and voices, a player record and a puppet's body. The
 * start gate of the played voice entry refuses a place farther than fourteen units from a listener
 * at the origin, which is the engine's by-name default, so what is heard is decided where the
 * engine decides it.
 *
 * And the moment itself on the way between: every kind of it passed on by a host, restamped with
 * the sender's slot and the host's tick and nothing else changed.
 */
#include "unittest.h"

#include "mp_bank.h"
#include "mp_body.h"
#include "mp_cells.h"
#include "mp_events.h"
#include "mp_player_sound.h"
#include "mp_player_sound_play.h"
#include "mp_player_sound_rule.h"
#include "mp_signatures_player_sound.h"
#include "mp_armed.h"

#include "common/text.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- the machine around the modules ---------------------------------------------------------- */

static bool    session = true;
static int32_t bank_class = 1;
static bool    stands[MP_BANK_FAR_MAX + 1u];

bool mp_armed_transport(void)
{
    return session;
}

int32_t mp_bank_active_class(void)
{
    return bank_class;
}

bool mp_body_far_player_stands(size_t index)
{
    return index <= MP_BANK_FAR_MAX && stands[index];
}

/* The player record, and the cell the player pointer is read from. */
static uint8_t  record[0x400];
static uint32_t pr_cell;

uintptr_t mp_cells_address(mp_cell_t cell)
{
    return cell == MP_CELL_PR ? (uintptr_t)&pr_cell : 0u;
}

/* ---- the engine the test plays --------------------------------------------------------------- */

static const char *names[MP_PLAYER_SOUND_NAMES];
static int32_t     death_voices[4] = { 33, 34, 35, 36 };
static int32_t     burn_voices[4]  = { 37, 38, 38, 38 };

/* Three named calls and the shield's end, as the engine lays them out: E8 and a distance. */
static uint8_t call_ground[5] = { 0xE8, 0, 0, 0, 0 };
static uint8_t call_key[5]    = { 0xE8, 0, 0, 0, 0 };
static uint8_t call_water[5]  = { 0xE8, 0, 0, 0, 0 };
static uint8_t call_down[5]   = { 0xE8, 0, 0, 0, 0 };

typedef struct heard {
    const char *name;
    uint32_t    flags;
    int32_t     cue;
    float       at[3];
    bool        handle_given;
} heard_t;

static unsigned played_names;
static uint32_t played_name_flags;
static heard_t  heard[32];
static unsigned heard_count;
static unsigned refused_far;
static unsigned bursts;
static float    burst_speed_seen;
static unsigned shields_made;
static unsigned shields_released;
static uint8_t *shield_owner[32];

static int32_t __cdecl engine_play_name(const char *name, uint32_t flags)
{
    (void)name;
    ++played_names;
    played_name_flags = flags;
    return 2;
}

static int32_t __cdecl engine_play_by_name(int32_t cue, const char *name, int32_t *handle,
                                           const float *at, uint32_t flags)
{
    float distance = at != NULL ? sqrtf(at[0] * at[0] + at[1] * at[1] + at[2] * at[2]) : 0.0f;

    if (at != NULL && distance > 14.0f) {
        ++refused_far;
        return -1;
    }
    if (heard_count < 32u) {
        heard[heard_count].name  = name;
        heard[heard_count].flags = flags;
        heard[heard_count].cue   = cue;
        heard[heard_count].handle_given = handle != NULL;
        if (at != NULL) {
            memcpy(heard[heard_count].at, at, sizeof heard[heard_count].at);
        }
    }
    ++heard_count;
    return 5;
}

/* The engine's burst, as far as the body goes: a drawn body is hidden, and nothing else of it is
 * written. */
static void __cdecl engine_burst(uint32_t body, float speed, int32_t unused)
{
    uint32_t flags;

    (void)unused;
    memcpy(&flags, (void *)(uintptr_t)body, sizeof flags);
    if ((flags & 1u) == 0u) {
        return;
    }
    flags &= ~1u;
    memcpy((void *)(uintptr_t)body, &flags, sizeof flags);
    burst_speed_seen = speed;
    ++bursts;
}

static int32_t __cdecl engine_shield_create(uint32_t owner)
{
    int32_t id = (int32_t)(1u + shields_made % 31u);

    shield_owner[id] = (uint8_t *)(uintptr_t)owner;
    ++shields_made;
    return id;
}

/* Release empties the owner's slot, as the engine's does. */
static int32_t __cdecl engine_shield_release(int32_t id)
{
    int32_t none = -1;

    if (id <= 0 || id >= 32 || shield_owner[id] == NULL) {
        return 0;
    }
    memcpy(shield_owner[id] + 0x100, &none, sizeof none);
    shield_owner[id] = NULL;
    ++shields_released;
    return 1;
}

static mp_player_sound_engine_t engine;

const mp_player_sound_engine_t *mp_signatures_player_sound_engine(void)
{
    return &engine;
}

static void build_the_engine(void)
{
    static char text[MP_PLAYER_SOUND_NAMES][16];
    size_t      i;

    for (i = 0; i < MP_PLAYER_SOUND_NAMES; ++i) {
        (void)text_format(text[i], sizeof text[i], "wav%02u", (unsigned)i);
        names[i] = text[i];
    }
    memset(&engine, 0, sizeof engine);
    engine.resolved         = MP_PLAYER_SOUND_SITE_COUNT;
    engine.ground_call      = (uintptr_t)call_ground;
    engine.key_call         = (uintptr_t)call_key;
    engine.water_call       = (uintptr_t)call_water;
    engine.shield_down_call = (uintptr_t)call_down;
    engine.play_name        = (uintptr_t)&engine_play_name;
    engine.shield_release   = (uintptr_t)&engine_shield_release;
    engine.names            = (uint32_t)(uintptr_t)names;
    engine.ground_name      = (uint32_t)(uintptr_t)&names[52];
    engine.key_name         = (uint32_t)(uintptr_t)&names[58];
    engine.water_name       = (uint32_t)(uintptr_t)&names[59];
    engine.pickup_name      = (uint32_t)(uintptr_t)&names[53];
    engine.death_voices     = (uint32_t)(uintptr_t)death_voices;
    engine.burn_voices      = (uint32_t)(uintptr_t)burn_voices;
    engine.play_by_name     = (uintptr_t)&engine_play_by_name;
    engine.burst            = (uintptr_t)&engine_burst;
    engine.burst_speed      = 2.0f;
    engine.shield_create    = (uintptr_t)&engine_shield_create;
    engine.shield_timer     = 100.0f;
    engine.agree            = true;
}

/* The function a repointed call now reaches. */
typedef int32_t(__cdecl *named_hook_t)(const char *name, uint32_t flags);
typedef int32_t(__cdecl *release_hook_t)(int32_t id);

static uintptr_t reached_by(const uint8_t call[5])
{
    uint32_t distance;

    memcpy(&distance, call + 1, sizeof distance);
    return (uintptr_t)call + 5u + distance;
}

/* ---- the queue of this player's moments ------------------------------------------------------ */

static mp_event_t queued[32];
static unsigned   queued_count;

static void queue(const mp_event_t *moment)
{
    if (queued_count < 32u) {
        queued[queued_count] = *moment;
        queued[queued_count].tick = 1000u + queued_count;
    }
    ++queued_count;
}

static void forget_the_queue(void)
{
    memset(queued, 0, sizeof queued);
    queued_count = 0u;
}

/* ---- a puppet -------------------------------------------------------------------------------- */

static uint8_t puppet_record[0x200];
static uint8_t body[0x110];

#define BODY_CONTACT_NODE 0xA4u

static void place_puppet(float x, float y, float z)
{
    float    at[3];
    uint32_t flags = 0x3u;   /* drawn and casting its shadow */
    int32_t  none = -1;
    uint32_t node = 0x5EEDu;

    at[0] = x;
    at[1] = y;
    at[2] = z;
    memcpy(puppet_record + 0x118, at, sizeof at);
    memcpy(body, &flags, sizeof flags);
    memcpy(body + 0x100, &none, sizeof none);
    memcpy(body + BODY_CONTACT_NODE, &node, sizeof node);
}

static uint32_t body_flags(void)
{
    uint32_t flags;

    memcpy(&flags, body, sizeof flags);
    return flags;
}

static int32_t body_shield(void)
{
    int32_t id;

    memcpy(&id, body + 0x100, sizeof id);
    return id;
}

static void deliver(uint8_t what, uint8_t sound, uint8_t flags, uint32_t tick, uint32_t render)
{
    mp_event_t moment;

    memset(&moment, 0, sizeof moment);
    moment.kind        = MP_EVENT_PLAYER_SOUND;
    moment.tick        = tick;
    moment.source_slot = 2u;
    moment.sound_what  = what;
    moment.sound_index = sound;
    moment.sound_flags = flags;
    mp_player_sound_due(1u, (uint32_t)(uintptr_t)puppet_record, (uint32_t)(uintptr_t)body,
                        &moment, render, true);
    mp_player_sound_after_window(1u);
}

/* ---- the checks ------------------------------------------------------------------------------ */

static void check_the_senders_hooks(void)
{
    named_hook_t   ground;
    named_hook_t   key;
    named_hook_t   water;
    release_hook_t down;
    unsigned       i;
    unsigned       naming_52 = 0u;

    ut_section("this player's sounds are caught at the engine's own calls");
    ut_check(mp_player_sound_install(&queue), "all four calls are repointed");
    ground = (named_hook_t)reached_by(call_ground);
    key    = (named_hook_t)reached_by(call_key);
    water  = (named_hook_t)reached_by(call_water);
    down   = (release_hook_t)reached_by(call_down);
    ut_check((uintptr_t)ground != (uintptr_t)&engine_play_name &&
                 (uintptr_t)down != (uintptr_t)&engine_shield_release,
             "and each now reaches a hook rather than the engine");

    forget_the_queue();
    played_names = 0u;
    ut_check(ground(names[52], 0u) == 2 && key(names[58], 0x200u) == 2 &&
                 water(names[59], 0u) == 2 && played_names == 3u &&
                 played_name_flags == 0u,
             "every hook plays the engine's own sound with the engine's own arguments and hands "
             "its answer back");
    ut_check(queued_count == 3u && queued[0].sound_what == MP_PLAYER_SOUND_GROUND &&
                 queued[0].sound_index == 52u && queued[1].sound_what == MP_PLAYER_SOUND_KEY &&
                 queued[1].sound_index == 58u && queued[2].sound_what == MP_PLAYER_SOUND_WATER &&
                 queued[2].sound_index == 59u,
             "and each tells the far side which name, read out of the engine's own operand");
    ut_check(down(0) == 0 && queued_count == 4u &&
                 queued[3].sound_what == MP_PLAYER_SOUND_SHIELD_OFF,
             "the shield's time running out is told as its end");

    forget_the_queue();
    session = false;
    played_names = 0u;
    (void)ground(names[52], 0u);
    (void)key(names[58], 0x200u);
    pr_cell = (uint32_t)(uintptr_t)record;
    mp_player_sound_note_death(3, (uintptr_t)record);
    mp_player_sound_note_pickup(0x0A);
    ut_check(played_names == 2u && queued_count == 0u,
             "without a session the engine plays its own sounds and nothing is told: single "
             "player is left to the engine");
    session = true;
    bank_class = 6;
    (void)water(names[59], 0u);
    ut_check(played_names == 3u && queued_count == 0u,
             "and a body a bank window swapped in is not this player's");
    bank_class = 1;

    /* Nothing this module tells is the pain of a hit. Of everything it sends, the one moment that
     * names entry 52, which the hit's pain plays as well, is the burning ground's own call. */
    forget_the_queue();
    for (i = 0; i <= 4u; ++i) {
        mp_player_sound_note_death((int32_t)i, (uintptr_t)record);
    }
    mp_player_sound_note_pickup(0x0D);
    mp_player_sound_note_pickup(0x0A);
    (void)ground(names[52], 0u);
    (void)key(names[58], 0x200u);
    (void)water(names[59], 0u);
    for (i = 0; i < queued_count && i < 32u; ++i) {
        bool named = queued[i].sound_what >= MP_PLAYER_SOUND_PICKUP &&
                     queued[i].sound_what <= MP_PLAYER_SOUND_GROUND;

        naming_52 += (named && queued[i].sound_index == 52u) ? 1u : 0u;
    }
    ut_checkf(naming_52 == 1u, "one moment names the pain's entry, the ground's, and no hit "
              "sends one (%u)", naming_52);
}

static void check_what_a_death_and_a_pickup_send(void)
{
    uint32_t hero = 2u;
    float    timer = 99.2f;

    ut_section("a death and a pickup, from their hulls");
    memcpy(record + 0x6C, &hero, sizeof hero);
    memcpy(record + 0x94, &timer, sizeof timer);
    pr_cell = (uint32_t)(uintptr_t)record;

    forget_the_queue();
    mp_player_sound_note_death(1, (uintptr_t)record);
    ut_check(queued_count == 1u && queued[0].sound_what == MP_PLAYER_SOUND_DEATH &&
                 queued[0].sound_index == 2u && queued[0].sound_flags == 1u,
             "a death sends the cry with the hero's row and the cause");
    forget_the_queue();
    mp_player_sound_note_death(3, (uintptr_t)record);
    ut_check(queued_count == 2u && queued[1].sound_what == MP_PLAYER_SOUND_BURN &&
                 queued[1].sound_flags == 3u,
             "a death by fire sends the burning cry behind it");
    forget_the_queue();
    mp_player_sound_note_death(4, (uintptr_t)record);
    ut_check(queued_count == 1u && queued[0].sound_flags == 4u,
             "a scripted death sends its cause, which the far side leaves silent");

    forget_the_queue();
    mp_player_sound_note_pickup(0x0D);
    ut_check(queued_count == 1u && queued[0].sound_what == MP_PLAYER_SOUND_PICKUP &&
                 queued[0].sound_index == 53u,
             "a medipack taken sends the pickup's name");
    forget_the_queue();
    mp_player_sound_note_pickup(0x0A);
    ut_check(queued_count == 2u && queued[1].sound_what == MP_PLAYER_SOUND_SHIELD_ON &&
                 queued[1].sound_index == 100u,
             "the shield pickup sends its rise with the seconds the engine's timer holds");
}

static void check_the_far_side_hears(void)
{
    float at[3];

    ut_section("the far side plays at the puppet through the engine's own entry");
    place_puppet(3.0f, 4.0f, 0.0f);
    heard_count = 0u;
    deliver(MP_PLAYER_SOUND_DEATH, 2u, 0u, 500u, 500u);
    memcpy(at, puppet_record + 0x118, sizeof at);
    ut_check(heard_count == 1u && strcmp(heard[0].name, "wav35") == 0 &&
                 heard[0].flags == 0x24u && heard[0].cue == 0 && !heard[0].handle_given &&
                 heard[0].at[0] == at[0] && heard[0].at[1] == at[1],
             "a death cry is this machine's own death voice for the hero row, at the puppet's "
             "place, 3D, with no handle");
    heard_count = 0u;
    deliver(MP_PLAYER_SOUND_DEATH, 2u, 4u, 500u, 500u);
    ut_check(heard_count == 0u, "a scripted death stays silent, as the engine leaves it");

    heard_count = 0u;
    deliver(MP_PLAYER_SOUND_KEY, 58u, 0u, 500u, 500u);
    deliver(MP_PLAYER_SOUND_GROUND, 52u, 0u, 500u, 500u);
    ut_check(heard_count == 2u && strcmp(heard[0].name, "wav58") == 0 &&
                 heard[0].flags == 0x224u && heard[0].cue == -1 &&
                 strcmp(heard[1].name, "wav52") == 0 && heard[1].flags == 0x24u,
             "the key with the engine's guard against playing it twice at once, the ground "
             "once, both named out of the table");

    heard_count = 0u;
    deliver(MP_PLAYER_SOUND_PICKUP, 53u, 0u, 500u, 511u);
    ut_check(heard_count == 0u, "a pickup eleven substeps behind its body is too late to play");

    refused_far = 0u;
    place_puppet(30.0f, 0.0f, 0.0f);
    deliver(MP_PLAYER_SOUND_WATER, 59u, 0u, 500u, 500u);
    ut_check(heard_count == 0u && refused_far == 1u,
             "a plunge thirty units away is refused by the engine's own start gate, not by us");

    heard_count = 0u;
    deliver(MP_PLAYER_SOUND_DEATH, 9u, 0u, 500u, 500u);
    ut_check(heard_count == 0u, "a hero row past the table plays nothing");
}

static void check_the_burst(void)
{
    uint32_t node_before;
    uint32_t node_after;
    uint32_t i;

    ut_section("a death by fire bursts the puppet, and the far player's return shows it again");
    place_puppet(1.0f, 1.0f, 0.0f);
    memcpy(&node_before, body + BODY_CONTACT_NODE, sizeof node_before);
    stands[1] = false;
    heard_count = 0u;
    bursts = 0u;
    deliver(MP_PLAYER_SOUND_DEATH, 0u, 3u, 700u, 700u);
    deliver(MP_PLAYER_SOUND_BURN, 0u, 3u, 700u, 700u);
    ut_check(bursts == 1u && (body_flags() & 1u) == 0u && burst_speed_seen == 2.0f,
             "the body bursts at the engine's own speed and is hidden");
    ut_check(heard_count == 2u && strcmp(heard[0].name, "wav33") == 0 &&
                 strcmp(heard[1].name, "wav37") == 0,
             "the death cry and the burning cry both sound, from their two tables");
    ut_check((body_flags() & 2u) != 0u, "the shadow bit is left as the death left it");

    mp_player_sound_after_window(1u);
    ut_check((body_flags() & 1u) == 0u, "while he lies, the body stays hidden");
    stands[1] = true;
    mp_player_sound_after_window(1u);
    memcpy(&node_after, body + BODY_CONTACT_NODE, sizeof node_after);
    ut_check((body_flags() & 1u) != 0u && (body_flags() & 2u) != 0u && node_after == node_before,
             "once he stands again the body is shown, and its shadow and its own contact node "
             "are what they were");

    /* The state that says he lies can come a sample after the moment, when one was lost. */
    place_puppet(1.0f, 1.0f, 0.0f);
    bursts = 0u;
    deliver(MP_PLAYER_SOUND_BURN, 0u, 3u, 700u, 700u);
    ut_check(bursts == 0u && (body_flags() & 1u) != 0u,
             "a burst whose pose still says he stands waits rather than hide a standing body");
    stands[1] = false;
    mp_player_sound_after_window(1u);
    ut_check(bursts == 1u && (body_flags() & 1u) == 0u,
             "and bursts the substep the pose says he lies");
    stands[1] = true;
    mp_player_sound_after_window(1u);
    ut_check((body_flags() & 1u) != 0u, "and is shown again when he stands");

    place_puppet(1.0f, 1.0f, 0.0f);
    bursts = 0u;
    deliver(MP_PLAYER_SOUND_BURN, 0u, 3u, 700u, 700u);
    for (i = 0; i < 12u; ++i) {
        mp_player_sound_after_window(1u);
    }
    stands[1] = false;
    mp_player_sound_after_window(1u);
    ut_check(bursts == 0u && (body_flags() & 1u) != 0u,
             "one that stood all through the late window is given up, and a later lying down "
             "bursts nothing: a standing body is never left hidden");
}

static void check_the_shield(void)
{
    uint32_t i;

    ut_section("the shield a pickup put round the far player");
    place_puppet(1.0f, 1.0f, 0.0f);
    stands[1] = true;
    shields_made = 0u;
    shields_released = 0u;
    deliver(MP_PLAYER_SOUND_SHIELD_ON, 100u, 0u, 800u, 900u);
    ut_check(shields_made == 1u && body_shield() > 0,
             "it goes up on the puppet as the pickup puts it on the player, however late");
    deliver(MP_PLAYER_SOUND_SHIELD_OFF, 0u, 0u, 1000u, 1000u);
    ut_check(shields_released == 1u && body_shield() == -1,
             "and comes down when the far player's time is up");

    deliver(MP_PLAYER_SOUND_SHIELD_ON, 100u, 0u, 1100u, 1100u);
    stands[1] = false;
    mp_player_sound_after_window(1u);
    ut_check(body_shield() > 0, "a far player lying dead keeps it, as his corpse does");
    stands[1] = true;
    mp_player_sound_after_window(1u);
    ut_check(body_shield() == -1, "and it is gone once he is back in a new body");

    /* The window that puts it on counts its first substep. */
    deliver(MP_PLAYER_SOUND_SHIELD_ON, 1u, 0u, 1200u, 1200u);
    for (i = 1; i < (1u + 2u) * 32u; ++i) {
        mp_player_sound_after_window(1u);
    }
    ut_check(body_shield() > 0, "a shield of one second is worn its length and its slack");
    mp_player_sound_after_window(1u);
    ut_check(body_shield() == -1, "and no longer, when no word of its end arrived");

    deliver(MP_PLAYER_SOUND_SHIELD_ON, 100u, 0u, 1300u, 1300u);
    mp_player_sound_forget(1u);
    ut_check(body_shield() > 0,
             "a far body that goes is forgotten without touching the engine, which lets its "
             "shield go with it");
}

static void check_no_body_and_torn(void)
{
    mp_event_t moment;

    ut_section("no body here, and torn");
    memset(&moment, 0, sizeof moment);
    moment.kind       = MP_EVENT_PLAYER_SOUND;
    moment.sound_what = MP_PLAYER_SOUND_DEATH;
    heard_count = 0u;
    mp_player_sound_unplaced(&moment);
    ut_check(heard_count == 0u, "a moment for a slot no puppet here shows is counted, not played");
    moment.sound_what = (uint8_t)(MP_PLAYER_SOUND_KIND_MAX + 1u);
    mp_player_sound_due(1u, (uint32_t)(uintptr_t)puppet_record, (uint32_t)(uintptr_t)body,
                        &moment, 0u, false);
    mp_player_sound_after_window(1u);
    ut_check(heard_count == 0u, "a kind past the eight is torn and plays nothing");
    mp_player_sound_report();
}

static void check_passed_on(void)
{
    uint8_t what;
    unsigned whole = 0u;

    ut_section("every kind of it is passed on by a host with the sender's slot and its own tick");
    for (what = 0u; what <= MP_PLAYER_SOUND_KIND_MAX; ++what) {
        mp_event_t sent;
        mp_event_t got;
        uint8_t    note[MP_EVENT_MAX_BYTES];
        size_t     bytes;

        memset(&sent, 0, sizeof sent);
        sent.kind        = MP_EVENT_PLAYER_SOUND;
        sent.tick        = 1000u;
        sent.source_slot = 2u;
        sent.sound_what  = what;
        sent.sound_index = (uint8_t)(50u + what);
        sent.sound_flags = (uint8_t)(what % 5u);
        bytes = mp_event_encode(&sent, note, sizeof note);
        if (bytes == MP_EVENT_PLAYER_SOUND_BYTES && mp_event_restamp(note, bytes, 3u, 5000u) &&
            mp_event_decode(note, bytes, &got) && got.source_slot == 3u && got.tick == 5000u &&
            got.sound_what == what && got.sound_index == sent.sound_index &&
            got.sound_flags == sent.sound_flags) {
            ++whole;
        }
    }
    ut_checkf(whole == MP_PLAYER_SOUND_KIND_MAX + 1u,
              "the death, the burning, a pickup, a key, the water, the ground and the shield's "
              "rise and end each arrive at the third player whole (%u)", whole);
}

int main(void)
{
    build_the_engine();
    check_the_senders_hooks();
    check_what_a_death_and_a_pickup_send();
    check_the_far_side_hears();
    check_the_burst();
    check_the_shield();
    check_no_body_and_torn();
    check_passed_on();
    return ut_summary("mp_player_sound");
}
