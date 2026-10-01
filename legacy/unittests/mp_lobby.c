/* The two notes a lobby speaks, pure: what a player says about themselves, and what the host says
 * about the session. Every refusal is a case, because both notes decide something that cannot be
 * taken back, the second one loads a level.
 *
 * SIZE NOTE: over 600 lines, because the setup note carries the session's rules, its generation,
 * the host's savegame and its difficulty, and each is a section of refusals of its own. The seam is
 * the setup note's sections into a file of their own, beside the player's note and the rules that
 * read the two.
 */
#include "unittest.h"

#include "mp_lobby.h"
#include "mp_rules.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_the_player_note(void)
{
    mp_lobby_t lobby;
    mp_lobby_t back;
    uint8_t    note[MP_LOBBY_BYTES];

    ut_section("a team, a ready and a hero cross as four bytes");
    lobby.team  = 2u;
    lobby.ready = 1u;
    lobby.hero  = 3u;
    ut_check(mp_lobby_encode(&lobby, note, sizeof note) == MP_LOBBY_BYTES, "encoded");
    ut_check(note[0] == MP_LOBBY_TAG, "the tag leads");
    ut_check(mp_lobby_is_lobby(note, sizeof note), "recognised by tag and length");
    ut_check(mp_lobby_decode(note, sizeof note, &back), "decoded");
    ut_check(back.team == 2u && back.ready == 1u && back.hero == 3u, "as sent");

    ut_section("what the player note refuses");
    ut_check(mp_lobby_encode(&lobby, note, MP_LOBBY_BYTES - 1u) == 0u, "a buffer one byte short");
    lobby.team = MP_LOBBY_TEAM_MAX + 1u;
    ut_check(mp_lobby_encode(&lobby, note, sizeof note) == 0u, "a team past the last");
    lobby.team = 1u;
    lobby.hero = MP_LOBBY_HERO_MAX + 1u;
    ut_check(mp_lobby_encode(&lobby, note, sizeof note) == 0u, "a hero that is not shipped");
    lobby.hero  = 0u;
    lobby.ready = 7u;
    ut_check(mp_lobby_encode(&lobby, note, sizeof note) == MP_LOBBY_BYTES && note[2] == 1u,
             "a ready that is not 0 or 1 goes out as 1 rather than as itself");

    ut_check(!mp_lobby_is_lobby(note, 2u), "a short buffer is not a player note");
    note[0] = 0x8Fu;
    ut_check(!mp_lobby_is_lobby(note, sizeof note), "and neither is a roster tag");
    note[0] = (uint8_t)MP_LOBBY_TAG;
    note[1] = 5u;
    ut_check(!mp_lobby_decode(note, sizeof note, &back), "a team past the last does not decode");
    note[1] = 0u;
    note[2] = 2u;
    ut_check(!mp_lobby_decode(note, sizeof note, &back), "a ready of two does not decode");
    note[2] = 1u;
    note[3] = 9u;
    ut_check(!mp_lobby_decode(note, sizeof note, &back), "a hero of nine does not decode");
    ut_check(!mp_lobby_decode(NULL, sizeof note, &back), "nothing does not decode");
}

static void fill_setup(mp_lobby_setup_t *setup)
{
    memset(setup, 0, sizeof *setup);
    setup->mode        = 2u;
    setup->flags       = 0u;
    setup->level_index = 1u;
    mp_lobby_clean_field("level\\swamp.b3d", setup->level, sizeof setup->level);
    mp_lobby_clean_field("Naboo Swamp", setup->title, sizeof setup->title);
}

static void check_the_setup_note(void)
{
    mp_lobby_setup_t setup;
    mp_lobby_setup_t back;
    uint8_t          note[MP_LOBBY_SETUP_BYTES];

    ut_section("what the host says about the session");
    fill_setup(&setup);
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == MP_LOBBY_SETUP_BYTES, "encoded");
    ut_check(note[0] == MP_LOBBY_SETUP_TAG, "the tag leads");
    ut_check(mp_lobby_is_setup(note, sizeof note), "recognised by tag and length");
    ut_check(!mp_lobby_is_lobby(note, sizeof note), "and it is not a player note");
    ut_check(mp_lobby_setup_decode(note, sizeof note, &back), "decoded");
    ut_check(back.mode == 2u && back.level_index == 1u, "the game and the index survive");
    ut_check(strcmp(back.level, "level\\swamp.b3d") == 0, "and so does the path, byte for byte");
    ut_check(strcmp(back.title, "Naboo Swamp") == 0, "and the name it is shown under");
    ut_check(mp_lobby_setup_equal(&setup, &back), "the round trip compares equal");

    ut_section("the start is a bit in the repeat, not a message of its own");
    setup.flags = MP_LOBBY_F_STARTED;
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == MP_LOBBY_SETUP_BYTES &&
                 mp_lobby_setup_decode(note, sizeof note, &back) &&
                 (back.flags & MP_LOBBY_F_STARTED) != 0u,
             "a started setup crosses as the same message with one bit set");
    ut_check(!mp_lobby_setup_equal(&setup, &back) == false, "and still compares equal to itself");
    setup.flags = MP_LOBBY_F_FROM_SAVE;
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == MP_LOBBY_SETUP_BYTES,
             "so does a session that restores a save");

    ut_section("a custom level is carried by its path, with no index to lean on");
    fill_setup(&setup);
    setup.level_index = MP_LOBBY_LEVEL_CUSTOM;
    mp_lobby_clean_field("level\\arena.b3d", setup.level, sizeof setup.level);
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == MP_LOBBY_SETUP_BYTES &&
                 mp_lobby_setup_decode(note, sizeof note, &back) &&
                 back.level_index == MP_LOBBY_LEVEL_CUSTOM &&
                 strcmp(back.level, "level\\arena.b3d") == 0,
             "the path is the whole answer");

    ut_section("what the setup refuses, and it refuses everything it did not promise");
    fill_setup(&setup);
    setup.mode = 3u;
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == 0u, "a game that is neither");
    fill_setup(&setup);
    setup.flags = 0x80u;
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == 0u, "a flag bit nobody knows");
    fill_setup(&setup);
    setup.level_index = 11u;
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == 0u,
             "an index past the eleven the table holds");
    fill_setup(&setup);
    setup.level[0] = '\0';
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == 0u, "an empty path");
    fill_setup(&setup);
    memset(setup.level, 'x', sizeof setup.level);   /* no terminator inside the field */
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == 0u, "a path that never ends");
    fill_setup(&setup);
    setup.title[2] = 0x07;
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == 0u,
             "a name with a byte the font has no glyph for");

    ut_section("and it refuses the same things when a stranger sent them");
    fill_setup(&setup);
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == MP_LOBBY_SETUP_BYTES,
             "a sound one");
    note[1] = 9u;
    ut_check(!mp_lobby_setup_decode(note, sizeof note, &back), "a game that is neither");
    note[1] = 1u;
    note[2] = 0x40u;
    ut_check(!mp_lobby_setup_decode(note, sizeof note, &back), "an unknown flag bit is REFUSED, "
             "not ignored: the bits decide whether a level loads");
    note[2] = 0u;
    note[3] = 200u;
    ut_check(!mp_lobby_setup_decode(note, sizeof note, &back), "an index past the table");
    note[3] = 0u;
    note[4] = '\0';
    ut_check(!mp_lobby_setup_decode(note, sizeof note, &back), "an empty path");
    ut_check(!mp_lobby_is_setup(note, MP_LOBBY_SETUP_BYTES - 1u), "a length that is not exact");
}

static void check_the_field_cleaner(void)
{
    char out[MP_LOBBY_TITLE_MAX];

    ut_section("what a typed name becomes before it goes on the wire");
    mp_lobby_clean_field("  Naboo Hangar  ", out, sizeof out);
    ut_check(strcmp(out, "Naboo Hangar") == 0, "the blanks at both ends fall away");
    mp_lobby_clean_field("Gr\x81\x9f", out, sizeof out);
    ut_check(strcmp(out, "Gr") == 0, "a byte with no glyph is dropped rather than guessed at");
    mp_lobby_clean_field("0123456789012345678901234567890", out, sizeof out);
    ut_check(strlen(out) == sizeof out - 1u, "a long one is cut to the field");
    mp_lobby_clean_field(NULL, out, sizeof out);
    ut_check(out[0] == '\0', "nothing becomes nothing");
    mp_lobby_clean_field("      ", out, sizeof out);
    ut_check(out[0] == '\0', "and so does a name of only blanks");
}

/* The team byte's only consumer: without this rule the byte travels the whole way and changes
 * nothing, so these are the cases that decide whether picking a team means anything. */
static void check_who_may_hurt_whom(void)
{
    const uint8_t coop = (uint8_t)MP_LOBBY_MODE_COOP;
    const uint8_t tdm  = (uint8_t)MP_LOBBY_MODE_TDM;
    const uint8_t none = (uint8_t)MP_LOBBY_TEAM_NONE;

    ut_section("who may hurt whom");

    ut_check(!mp_lobby_may_damage(coop, none, none, false),
             "two players on the same campaign are one side and cannot kill each other");
    ut_check(!mp_lobby_may_damage(coop, 1u, 2u, false),
             "and a team number they should not have been offered changes nothing about that");
    ut_check(mp_lobby_may_damage(coop, none, none, true),
             "unless the session turned friendly fire on, which is what that switch means");

    ut_check(mp_lobby_may_damage(tdm, 1u, 2u, false), "opposite teams fight");
    ut_check(!mp_lobby_may_damage(tdm, 1u, 1u, false), "the same team does not");
    ut_check(!mp_lobby_may_damage(tdm, 2u, 2u, false), "on either number");
    ut_check(mp_lobby_may_damage(tdm, 1u, 1u, true),
             "friendly fire outranks the teams; what it costs the shooter is a score rule");

    ut_check(mp_lobby_may_damage(tdm, none, none, false),
             "a session where nobody took a team IS the free-for-all, with no second rule");
    ut_check(mp_lobby_may_damage(tdm, 1u, none, false),
             "and somebody standing outside the teams may be hit by one of them");
    ut_check(mp_lobby_may_damage(tdm, none, 1u, false), "in both directions");

    ut_check(!mp_lobby_may_damage(0u, 1u, 2u, false),
             "an unknown game is not a licence to hurt anybody");
    ut_check(!mp_lobby_may_damage(99u, 1u, 2u, true),
             "not even with friendly fire on, because the mode is read off the wire");
}

/* The one rule is asked from both sides of a contact now, always about the FAR player's slot, and
 * that only works while the rule itself gives the same verdict however the pair is named. It does
 * today, in every case. This is what stops a later change from making the answer depend on which
 * of the two is called the attacker, because a gate built on an answer like that protects one of
 * the two players and looks perfectly healthy in every log. */
static void check_the_rule_reads_the_same_both_ways(void)
{
    const uint8_t MODES[] = { (uint8_t)MP_LOBBY_MODE_COOP, (uint8_t)MP_LOBBY_MODE_TDM, 0u, 99u };
    const uint8_t TEAMS[] = { (uint8_t)MP_LOBBY_TEAM_NONE, 1u, 2u, 7u };
    unsigned      asymmetric = 0u;
    unsigned      pairs = 0u;
    size_t        m;
    size_t        a;
    size_t        v;
    size_t        f;

    ut_section("the damage rule answers the same whichever of the two is called the attacker");
    for (m = 0; m < sizeof MODES / sizeof MODES[0]; ++m) {
        for (a = 0; a < sizeof TEAMS / sizeof TEAMS[0]; ++a) {
            for (v = 0; v < sizeof TEAMS / sizeof TEAMS[0]; ++v) {
                for (f = 0; f < 2u; ++f) {
                    bool one = mp_lobby_may_damage(MODES[m], TEAMS[a], TEAMS[v], f != 0u);
                    bool two = mp_lobby_may_damage(MODES[m], TEAMS[v], TEAMS[a], f != 0u);

                    ++pairs;
                    if (one != two) {
                        ++asymmetric;
                    }
                }
            }
        }
    }
    ut_checkf(asymmetric == 0u, "%u of %u pairs answer differently when the two are swapped",
              asymmetric, pairs);
}

/* The rule set and the generation ride in the setup note, and both were appended behind the
 * ninety two bytes it shipped with. What is checked here is that they survive the crossing, that
 * a rule nobody can play is refused in both directions, and that the two of them are part of what
 * makes one setup different from another: a host that suppressed the repeat because everything
 * else matched would have raised a generation and told nobody.
 */
static void check_the_rules_on_the_wire(void)
{
    mp_lobby_setup_t setup;
    mp_lobby_setup_t back;
    uint8_t          note[MP_LOBBY_SETUP_BYTES];

    ut_section("the rule set rides in the note the host repeats anyway");

    ut_check(MP_LOBBY_SETUP_BYTES == 92u + MP_RULES_BYTES + 1u + 8u + 1u,
             "ninety two as it shipped, plus nine of rules, one of generation, eight of the "
             "savegame's name and size and one of the host's difficulty");

    fill_setup(&setup);
    mp_rules_default(&setup.rules);
    setup.generation = 3u;
    setup.save_id    = 0xC0FFEE11u;
    setup.save_bytes = 78992u;
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == MP_LOBBY_SETUP_BYTES,
             "a setup carrying the default rules encodes");
    ut_check(mp_lobby_setup_decode(note, sizeof note, &back), "and decodes");
    ut_check(mp_rules_equal(&setup.rules, &back.rules), "with the rules field for field");
    ut_check(back.generation == 3u, "and the generation as sent");
    ut_check(back.save_id == 0xC0FFEE11u && back.save_bytes == 78992u,
             "and the savegame's name and size, which a client compares against what it holds");
    ut_check(mp_lobby_setup_equal(&setup, &back), "the round trip compares equal");
    back.save_id = 1u;
    ut_check(!mp_lobby_setup_equal(&setup, &back),
             "and a different savegame is a different setup, so the host repeats it at once");

    ut_section("a rule nobody can play never leaves, and is refused when a stranger sends it");

    fill_setup(&setup);
    mp_rules_default(&setup.rules);
    setup.rules.score_limit = (uint16_t)(MP_RULES_SCORE_LIMIT_MAX + 1u);
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == 0u,
             "a points limit past the ceiling stops the whole note");
    setup.rules.score_limit = 10u;
    setup.rules.flags = 0x40u;
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == 0u,
             "and so does a rule flag this build does not know");

    fill_setup(&setup);
    mp_rules_default(&setup.rules);
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == MP_LOBBY_SETUP_BYTES,
             "a sound one exists to forge from");
    note[92 + 4] = 0x80u;   /* the rule set's flag byte, which is the fifth of its nine */
    ut_check(!mp_lobby_setup_decode(note, sizeof note, &back),
             "an unknown rule flag off the wire is refused, not ignored: these bits decide who "
             "may be hurt");
    note[92 + 4] = (uint8_t)MP_RULES_FLAGS_DEFAULT;
    note[92 + 8] = 1u;      /* the held back byte */
    ut_check(!mp_lobby_setup_decode(note, sizeof note, &back),
             "and so is a held back byte that is not held back");
    note[92 + 8] = 0u;
    ut_check(mp_lobby_setup_decode(note, sizeof note, &back), "with both put back, it decodes");
}

/* The host's difficulty is the note's last byte: nought for none, otherwise the difficulty plus
 * one. A note of the old length is another build's and is refused, and so is a byte past ten in
 * either direction, because the decoder takes exactly what the encoder writes. */
static void check_the_hosts_difficulty(void)
{
    mp_lobby_setup_t setup;
    mp_lobby_setup_t back;
    uint8_t          note[MP_LOBBY_SETUP_BYTES];
    unsigned         byte;
    bool             every = true;

    ut_section("the host's difficulty rides the note it repeats");
    for (byte = 0; byte <= MP_LOBBY_DIFFICULTY_MAX; ++byte) {
        fill_setup(&setup);
        mp_rules_default(&setup.rules);
        setup.host_difficulty = (uint8_t)byte;
        if (mp_lobby_setup_encode(&setup, note, sizeof note) != MP_LOBBY_SETUP_BYTES ||
            note[MP_LOBBY_SETUP_BYTES - 1u] != byte ||
            !mp_lobby_setup_decode(note, sizeof note, &back) || back.host_difficulty != byte ||
            !mp_lobby_setup_equal(&setup, &back)) {
            every = false;
        }
    }
    ut_check(every, "none and every difficulty from 0 to 9 cross as its last byte, and back");
    back.host_difficulty = 1u;
    ut_check(!mp_lobby_setup_equal(&setup, &back),
             "and another difficulty is another setup, which goes out with the host's next repeat "
             "of the note, at most a second later");

    fill_setup(&setup);
    mp_rules_default(&setup.rules);
    setup.host_difficulty = (uint8_t)(MP_LOBBY_DIFFICULTY_MAX + 1u);
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == 0u,
             "eleven is no difficulty and is not written");
    setup.host_difficulty = 0u;
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == MP_LOBBY_SETUP_BYTES,
             "(a sound one to forge from)");
    note[MP_LOBBY_SETUP_BYTES - 1u] = (uint8_t)(MP_LOBBY_DIFFICULTY_MAX + 1u);
    ut_check(!mp_lobby_setup_decode(note, sizeof note, &back), "nor read");
    note[MP_LOBBY_SETUP_BYTES - 1u] = 0u;
    ut_check(!mp_lobby_is_setup(note, MP_LOBBY_SETUP_BYTES - 1u) &&
                 !mp_lobby_setup_decode(note, MP_LOBBY_SETUP_BYTES - 1u, &back),
             "and a note of 110 bytes, the length before the difficulty, is refused");
}

/* A client took every start whose generation it had not played, and ready was asked nowhere: a
 * player who joined a running session was in the level a second later with hero 0. */
static void check_a_start_waits_for_ready(void)
{
    ut_section("a client's lobby takes a start only with its own player's ready");
    ut_check(mp_lobby_start_may_be_taken(true, true),
             "offered and ready: taken, which is every start a host makes in its lobby, since the "
             "host starts only once everybody is ready");
    ut_check(!mp_lobby_start_may_be_taken(false, true),
             "offered and not ready: held, which is the player who joined a running session");
    ut_check(!mp_lobby_start_may_be_taken(true, false) &&
                 !mp_lobby_start_may_be_taken(false, false),
             "and nothing offered is nothing to take, ready or not");
}

/* The generation is what makes the note usable more than once. The start bit is an edge that has
 * already fired, and it is never cleared, so a host can send everybody into a level exactly once
 * per session. A generation is a state: a side that sees one it has not acted on acts on it.
 */
static void check_the_generation(void)
{
    mp_lobby_setup_t a;
    mp_lobby_setup_t b;

    ut_section("a new generation is a DIFFERENT one, never a larger one");

    ut_check(!mp_lobby_generation_is_new(0u, 0u), "the same generation is not news");
    ut_check(!mp_lobby_generation_is_new(7u, 7u), "at any value");
    ut_check(mp_lobby_generation_is_new(0u, 1u), "the next one is");
    ut_check(mp_lobby_generation_is_new(255u, 0u),
             "and so is the one that wrapped: a greater-than would sit there and let that world "
             "change through unnoticed, then refuse every one after it for the rest of the "
             "session");
    ut_check(mp_lobby_generation_is_new(3u, 1u),
             "a generation that went backwards is new as well, because a host that restarted is "
             "still a host that changed the world");

    ut_section("a raised generation makes the note news, so the repeat is not suppressed");

    fill_setup(&a);
    mp_rules_default(&a.rules);
    b = a;
    ut_check(mp_lobby_setup_equal(&a, &b), "two identical setups are identical");
    b.generation = (uint8_t)(a.generation + 1u);
    ut_check(!mp_lobby_setup_equal(&a, &b),
             "one whose generation was raised is not, or the host would say it to nobody");
    b = a;
    b.rules.score_limit = (uint16_t)(a.rules.score_limit + 1u);
    ut_check(!mp_lobby_setup_equal(&a, &b), "and neither is one whose rules changed");
}

/* THE WHOLE RANGE, walked, because the bridge now leans on this byte for every world change of a
 * session and the interesting value is the one nobody reaches by hand.
 *
 * The bridge keeps the generation it last acted on and compares an arriving one against it; the
 * host raises its own by one on every start. So the run below is what a session of two hundred and
 * fifty six world changes actually does, the wrap from 255 to 0 included, and what it has to show
 * is that every raise is seen exactly once: once as news when it arrives, and not again when the
 * same note comes round on the next repeat.
 *
 * The setup goes through the real encoder and decoder on every step rather than being copied,
 * because a generation that did not survive the crossing would fail here in exactly the same way
 * as one the comparison got wrong, and the bridge cannot tell those apart either. */
static void check_a_whole_session_of_world_changes(void)
{
    mp_lobby_setup_t setup;
    mp_lobby_setup_t heard;
    uint8_t          note[MP_LOBBY_SETUP_BYTES];
    uint8_t          acted_on = 0u;
    uint32_t         acts = 0;
    uint32_t         repeats_acted_on = 0;
    uint32_t         crossings = 0;
    uint32_t         change;

    ut_section("every one of 256 world changes is acted on exactly once");

    fill_setup(&setup);
    mp_rules_default(&setup.rules);
    setup.flags     = (uint8_t)MP_LOBBY_F_STARTED;
    setup.generation = 0u;

    for (change = 0; change < 256u; ++change) {
        int repeat;

        ++setup.generation;   /* what the host does when it sends everybody into a world */
        for (repeat = 0; repeat < 3; ++repeat) {
            if (mp_lobby_setup_encode(&setup, note, sizeof note) != MP_LOBBY_SETUP_BYTES ||
                !mp_lobby_setup_decode(note, sizeof note, &heard)) {
                continue;
            }
            ++crossings;
            if (!mp_lobby_generation_is_new(acted_on, heard.generation)) {
                continue;
            }
            if (repeat != 0) {
                ++repeats_acted_on;
            }
            acted_on = heard.generation;
            ++acts;
        }
    }
    ut_checkf(crossings == 768u, "every note crossed the codec (%u of 768)", (unsigned)crossings);
    ut_checkf(acts == 256u, "256 world changes were acted on 256 times (%u)", (unsigned)acts);
    ut_checkf(repeats_acted_on == 0u,
              "and no repeat of an already acted on note was acted on again (%u)",
              (unsigned)repeats_acted_on);
    ut_check(acted_on == setup.generation, "the last one acted on is the last one sent");
}

/* When a session is over, and, the harder half, when it is NOT.
 *
 * Every arm here is a way of being thrown out of a level, so a rule that says yes too readily is
 * worse than one that says no: a host between levels is gone for thirty seconds and comes back,
 * and a session that ended itself over that is a session nobody can play. */
static void check_when_a_session_is_over(void)
{
    /* Nothing has started, so nothing can end, and this is the arm that runs on every frame of
     * every menu, before anybody has connected to anything. */
    ut_check(mp_lobby_session_over(false, true, true, true, false, true, false, false) ==
                 MP_LOBBY_OVER_NO,
             "a session that never started cannot end, even with every other sign of an ending");
    ut_check(mp_lobby_session_over(true, false, true, true, false, true, false, false) ==
                 MP_LOBBY_OVER_NO,
             "and neither can one with no level running: there is nothing to be sent out of");

    /* The client, told. */
    ut_check(mp_lobby_session_over(true, true, true, true, true, false, false, false) ==
                 MP_LOBBY_OVER_HOST_ENDED,
             "a host that says it is finished is believed WHILE STILL CONNECTED");
    ut_check(mp_lobby_session_over(true, true, true, true, false, true, false, false) ==
                 MP_LOBBY_OVER_HOST_ENDED,
             "and its word outranks its silence when both are there");

    /* The client, not told. This is the pair that matters most: the difference between a host
     * that is between levels and a host that is gone is the session giving up, and nothing else. */
    ut_check(mp_lobby_session_over(true, true, true, false, false, false, false, false) ==
                 MP_LOBBY_OVER_NO,
             "a silent host is NOT an ended session while the session is still trying");
    ut_check(mp_lobby_session_over(true, true, true, false, false, true, false, false) ==
                 MP_LOBBY_OVER_HOST_LOST,
             "it becomes one when the session stops trying");
    /* The client's own content refusal is an ending of its own, and it outranks the two
     * that describe the host, because the absence that follows it is the client's doing. */
    ut_check(mp_lobby_session_over(true, true, true, false, false, true, true, false) ==
             MP_LOBBY_OVER_CONTENT,
             "a client that refused the host's content reports that, not a lost host");
    ut_check(mp_lobby_session_over(true, true, true, true, false, true, true, false) ==
             MP_LOBBY_OVER_CONTENT,
             "and it does so even if the host's ended flag arrived in the same breath");
    ut_check(mp_lobby_session_over(true, true, false, false, false, true, true, false) ==
             MP_LOBBY_OVER_ALL_LEFT,
             "a host never reports a content ending; that is the client's verdict alone");
    ut_check(mp_lobby_session_over(true, true, true, false, true, true, false, false) ==
                 MP_LOBBY_OVER_NO,
             "and a host that is present is never lost, whatever the session gave up on");
    /* The host sent this client away for falling behind. Shown as a lost host, the player would
     * look for the fault in the wire; the notice says what the host decided. */
    ut_check(mp_lobby_session_over(true, true, true, false, false, true, false, true) ==
             MP_LOBBY_OVER_BEHIND,
             "a client the host sent away for falling behind is told that, not a lost host");
    ut_check(mp_lobby_session_over(true, true, true, true, false, true, false, true) ==
             MP_LOBBY_OVER_BEHIND,
             "the notice is the host's last word to this client and outranks an ended flag");
    ut_check(mp_lobby_session_over(true, true, true, false, false, true, true, true) ==
             MP_LOBBY_OVER_CONTENT,
             "and a client that cut itself off for different data still says that first");
    ut_check(mp_lobby_session_over(true, true, false, false, false, true, false, true) ==
             MP_LOBBY_OVER_ALL_LEFT,
             "a host is never sent away; the flag means nothing on its side");

    /* The host. It has no one to be told by, and the trap is the session it started alone. */
    ut_check(mp_lobby_session_over(true, true, false, false, false, false, false, false) ==
                 MP_LOBBY_OVER_NO,
             "a host whose first client has not arrived yet is not a host whose clients all left");
    ut_check(mp_lobby_session_over(true, true, false, false, false, true, false, false) ==
                 MP_LOBBY_OVER_ALL_LEFT,
             "a host that HAD somebody and now has nobody is done");
    ut_check(mp_lobby_session_over(true, true, false, false, true, true, false, false) ==
                 MP_LOBBY_OVER_NO,
             "and one that still has somebody plays on");
    ut_check(mp_lobby_session_over(true, true, false, true, true, true, false, false) ==
                 MP_LOBBY_OVER_NO,
             "the ended flag is the host's OWN word read back, and it does not end the host");
}

/* Every flag has to make it through the encoder AND the decoder.
 *
 * This is a regression test. When MP_LOBBY_F_ENDED was added, the mask of known flags was left at
 * 0x03, so both halves of the codec refused every setup note carrying it: the end-of-session
 * announcement that flag exists for could not be sent, could not be read, and said nothing about it
 * in any log. The rule that decides an ending had a unit test and passed it; what had no test was
 * whether the message could travel. */
static void check_every_flag_survives_its_own_codec(void)
{
    static const uint8_t FLAGS[] = {
        0u,
        (uint8_t)MP_LOBBY_F_FROM_SAVE,
        (uint8_t)MP_LOBBY_F_STARTED,
        (uint8_t)MP_LOBBY_F_ENDED,
        (uint8_t)(MP_LOBBY_F_STARTED | MP_LOBBY_F_ENDED),
        (uint8_t)MP_LOBBY_F_KNOWN
    };
    mp_lobby_setup_t setup;
    mp_lobby_setup_t heard;
    uint8_t          note[MP_LOBBY_SETUP_BYTES];
    size_t           i;

    ut_section("every known flag crosses, alone and together");

    for (i = 0; i < sizeof FLAGS / sizeof FLAGS[0]; ++i) {
        memset(&setup, 0, sizeof setup);
        setup.mode        = (uint8_t)MP_LOBBY_MODE_TDM;
        setup.level_index = 0u;
        setup.flags       = FLAGS[i];
        memcpy(setup.level, "ASSAULT", 8u);
        mp_rules_default(&setup.rules);
        setup.generation = 3u;

        ut_checkf(mp_lobby_setup_encode(&setup, note, sizeof note) == MP_LOBBY_SETUP_BYTES,
                  "flag byte 0x%02X encodes", (unsigned)FLAGS[i]);
        ut_checkf(mp_lobby_setup_decode(note, sizeof note, &heard),
                  "flag byte 0x%02X decodes", (unsigned)FLAGS[i]);
        ut_checkf(heard.flags == FLAGS[i], "flag byte 0x%02X arrives unchanged",
                  (unsigned)FLAGS[i]);
    }

    ut_section("and a flag from a newer build is still refused");

    memset(&setup, 0, sizeof setup);
    setup.mode        = (uint8_t)MP_LOBBY_MODE_TDM;
    setup.level_index = 0u;
    setup.flags       = (uint8_t)(MP_LOBBY_F_KNOWN | 0x80u);
    memcpy(setup.level, "ASSAULT", 8u);
    mp_rules_default(&setup.rules);
    ut_check(mp_lobby_setup_encode(&setup, note, sizeof note) == 0u,
             "an unknown bit is refused rather than sent with the half we understand");
}

/* The band's cascade as it stood, every combination of its five facts against the rule that
 * replaced it: the same answer for all thirty two, which pins the order of the arms. */
static mp_lobby_join_t old_band(bool mismatch, bool denied, bool left, bool gave_up,
                                bool connected)
{
    if (mismatch) {
        return MP_LOBBY_JOIN_CONTENT;
    }
    if (denied) {
        return MP_LOBBY_JOIN_DENIED;
    }
    if (left) {
        return MP_LOBBY_JOIN_HOST_LEFT;
    }
    if (gave_up) {
        return MP_LOBBY_JOIN_GAVE_UP;
    }
    return connected ? MP_LOBBY_JOIN_CONNECTED : MP_LOBBY_JOIN_ASKING;
}

static void check_every_join_state(void)
{
    unsigned facts;
    unsigned differ = 0u;

    ut_section("where a joining client stands, for every combination of its facts");
    for (facts = 0u; facts < 32u; ++facts) {
        bool mismatch  = (facts & 1u) != 0u;
        bool denied    = (facts & 2u) != 0u;
        bool left      = (facts & 4u) != 0u;
        bool gave_up   = (facts & 8u) != 0u;
        bool connected = (facts & 16u) != 0u;

        if (mp_lobby_join_status(mismatch, denied, left, gave_up, connected) !=
            old_band(mismatch, denied, left, gave_up, connected)) {
            ++differ;
        }
    }
    ut_checkf(differ == 0u, "all thirty two combinations answer as the band did: %u did not",
              differ);
    ut_check(mp_lobby_join_status(false, true, true, true, true) == MP_LOBBY_JOIN_DENIED,
             "a refusal is said before a goodbye, which it also is");
    ut_check(mp_lobby_join_status(false, false, true, true, false) == MP_LOBBY_JOIN_HOST_LEFT,
             "and a goodbye before the silence it also ends in");
}

/* Whether a join asks for a password before it sends the request.
 *
 * Three answers and not two, and the third is the point. A session that was HEARD says in its
 * announce whether it wants one. A typed address and a session code were never heard, and every
 * player joining by code was asked in advance, for a password almost no host has. What was not
 * heard therefore asks nobody: the join goes out, and the host's refusal is the first thing that
 * knows. */
static void check_when_a_password_is_asked(void)
{
    ut_section("a password is asked for when a session says it wants one, and not before");

    ut_check(mp_lobby_password_question(true, true) == MP_LOBBY_PASSWORD_ASK,
             "a session that says it wants one is asked about before the join");
    ut_check(mp_lobby_password_question(true, false) == MP_LOBBY_PASSWORD_NONE,
             "one that says it wants none is offered none, and nobody is asked");
    ut_check(mp_lobby_password_question(false, false) == MP_LOBBY_PASSWORD_WAIT,
             "a code that was never heard asks nobody: the refusal is what says one is needed");
    ut_check(mp_lobby_password_question(false, true) == MP_LOBBY_PASSWORD_WAIT,
             "and a bit nobody heard is not a fact, so it waits either way");
}

int main(void)
{
    check_the_player_note();
    check_the_setup_note();
    check_the_field_cleaner();
    check_who_may_hurt_whom();
    check_the_rule_reads_the_same_both_ways();
    check_the_rules_on_the_wire();
    check_the_hosts_difficulty();
    check_the_generation();
    check_a_start_waits_for_ready();
    check_a_whole_session_of_world_changes();
    check_when_a_session_is_over();
    check_every_flag_survives_its_own_codec();
    check_every_join_state();
    check_when_a_password_is_asked();
    return ut_summary("mp_lobby");
}
