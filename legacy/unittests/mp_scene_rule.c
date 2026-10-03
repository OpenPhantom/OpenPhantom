/* The two scene decisions, driven over everything they can be given.
 *
 * The first of them is the repair for a client that came out of a scene of the host's unable to
 * move. The gate on this side refused the grab of the hero and let the put-back through, and the
 * put-back writes the module state out of a store that on such a machine holds nought. What
 * follows models the engine's own pair off its bytes, runs every sequence of grabs and put-backs
 * up to six long through both the old behaviour and the new rule, and holds the new one to two
 * claims: the module is never left at nought without a grab standing, and the two behaviours
 * differ at exactly one place, a put-back whose store is nought.
 */
#include "unittest.h"

#include "mp_cells.h"
#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The engine's own pair, as the bytes read.
 * ============================================================================================ */

typedef struct hero {
    uint32_t module_state;   /* the record at +0x04 */
    uint32_t saved;          /* the store at +0x08 */
    bool     has_body;       /* the object at +0x0C, non nought */
} hero_t;

/* player_suspend 0x00450F25. No body or no running module and it answers nought without parking
 * anything, which is why the store never holds a nought after a grab that went through. */
static bool engine_grab(hero_t *hero)
{
    if (!hero->has_body || hero->module_state == 0u) {
        return false;
    }
    hero->saved        = hero->module_state;
    hero->module_state = 0u;
    return true;
}

/* player_resume 0x00450FF1. No body and it answers nought; otherwise it writes the store back
 * and asks nothing else. It does NOT clear the store, which is why a second put-back after one
 * grab writes the value the module already holds instead of needing to be refused. */
static void engine_putback(hero_t *hero)
{
    if (!hero->has_body) {
        return;
    }
    hero->module_state = hero->saved;
}

/* ==============================================================================================
 * One run over a sequence.
 * ============================================================================================ */

typedef struct run {
    hero_t   hero;
    bool     client_refuses_grabs;   /* the gate this file has carried since it was written */
    bool     new_rule;               /* whether the put-back is gated as well */
    uint32_t grabs_standing;
    uint32_t putbacks_refused;
} run_t;

static void step_grab(run_t *run)
{
    if (run->client_refuses_grabs) {
        return;
    }
    if (engine_grab(&run->hero)) {
        ++run->grabs_standing;
    }
}

/* Returns true when this step wrote a running module down to nought, which is the damage the
 * whole repair is about. */
static bool step_putback(run_t *run)
{
    uint32_t before = run->hero.module_state;

    if (run->new_rule && !mp_scene_putback_allowed(run->hero.saved)) {
        ++run->putbacks_refused;
        return false;
    }
    engine_putback(&run->hero);
    if (run->grabs_standing > 0u) {
        --run->grabs_standing;
    }
    return before != 0u && run->hero.module_state == 0u;
}

static void start(run_t *run, hero_t start_state, bool client, bool new_rule)
{
    run->hero                 = start_state;
    run->client_refuses_grabs = client;
    run->new_rule             = new_rule;
    run->grabs_standing       = 0u;
    run->putbacks_refused     = 0u;
}

/* ==============================================================================================
 * The store is the whole of the question.
 * ============================================================================================ */

static void check_the_store_decides(void)
{
    static const uint32_t STORES[] = {
        0u, 1u, 2u, 3u, 4u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu
    };
    size_t index;

    ut_section("a put-back is allowed exactly where the store is not nought");

    ut_check(!mp_scene_putback_allowed(0u),
             "a store of nought means nobody ever parked the hero here, so the put-back is "
             "refused");
    for (index = 0; index < sizeof STORES / sizeof STORES[0]; ++index) {
        ut_checkf(mp_scene_putback_allowed(STORES[index]) == (STORES[index] != 0u),
                  "a store of %u answers %s", (unsigned)STORES[index],
                  STORES[index] != 0u ? "allowed" : "refused");
    }

    ut_section("the two cells the rule stands on are four bytes apart and not the same one");
    ut_check(MP_HERO_BLOCK_MODULE_STATE == 0x04u, "the module state is at +0x04");
    ut_check(MP_HERO_BLOCK_SAVED_MODULE_STATE == 0x08u, "its store is at +0x08");
    ut_check(MP_HERO_BLOCK_SAVED_MODULE_STATE != MP_HERO_BLOCK_MODULE_STATE,
             "the rule asks the store and not the state, and reading the state instead would "
             "let a put-back through in the window between a despawn and a spawn");
}

/* ==============================================================================================
 * The pair, over every sequence six long.
 * ============================================================================================ */

#define SEQUENCE_MAX 6u

static void check_every_sequence(void)
{
    static const hero_t STARTS[] = {
        { 1u, 0u, true },    /* a player standing in a level, nothing parked */
        { 0u, 0u, true },    /* the window between a despawn and a spawn */
        { 1u, 0u, false },   /* no body, which the engine answers nought to in both directions */
    };
    size_t   start_index;
    uint32_t length;
    uint32_t bits;
    uint32_t sequences = 0u;
    uint32_t new_wrote_nought = 0u;    /* has to end at none */
    uint32_t old_wrote_nought = 0u;    /* has to end above none, or the reference proves nothing */
    uint32_t disagreed_off_the_empty_store = 0u;
    uint32_t agreed_on_the_empty_store = 0u;
    uint32_t met_an_empty_store = 0u;
    uint32_t module_at_nought_unparked = 0u;

    ut_section("over every sequence of grabs and put-backs, six long, from three starts");

    for (start_index = 0; start_index < sizeof STARTS / sizeof STARTS[0]; ++start_index) {
        int client;

        for (client = 0; client < 2; ++client) {
            for (length = 0u; length <= SEQUENCE_MAX; ++length) {
                for (bits = 0u; bits < (1u << length); ++bits) {
                    run_t    fresh;
                    run_t    old;
                    uint32_t step;

                    start(&fresh, STARTS[start_index], client != 0, true);
                    start(&old, STARTS[start_index], client != 0, false);
                    ++sequences;

                    for (step = 0u; step < length; ++step) {
                        if (((bits >> step) & 1u) != 0u) {
                            /* The reference is the old behaviour, which is "let every put-back
                             * through". Both decisions are taken from ONE state, so what is
                             * compared here is the rule and not two trajectories that have
                             * already drifted apart. */
                            bool store_is_empty = fresh.hero.saved == 0u;
                            bool allowed_new    = mp_scene_putback_allowed(fresh.hero.saved);

                            if (store_is_empty) {
                                ++met_an_empty_store;
                                if (allowed_new) {
                                    ++agreed_on_the_empty_store;
                                }
                            } else if (!allowed_new) {
                                ++disagreed_off_the_empty_store;
                            }
                            if (step_putback(&fresh)) {
                                ++new_wrote_nought;
                            }
                            if (step_putback(&old)) {
                                ++old_wrote_nought;
                            }
                        } else {
                            step_grab(&fresh);
                            step_grab(&old);
                        }
                        if (fresh.hero.module_state == 0u && fresh.grabs_standing == 0u &&
                            STARTS[start_index].module_state != 0u) {
                            ++module_at_nought_unparked;
                        }
                    }
                }
            }
        }
    }

    ut_checkf(sequences == 762u, "%u sequences were driven, three starts on two sides",
              (unsigned)sequences);
    ut_checkf(new_wrote_nought == 0u,
              "%u put-back(s) under the new rule wrote a running module down to nought, and the "
              "answer has to be none", (unsigned)new_wrote_nought);
    ut_checkf(old_wrote_nought > 0u,
              "the old behaviour does write a running module down to nought (%u time(s)), or "
              "this comparison would be proving nothing", (unsigned)old_wrote_nought);
    ut_checkf(module_at_nought_unparked == 0u,
              "the module stood at nought with nothing parked %u time(s), and the answer has to "
              "be none", (unsigned)module_at_nought_unparked);
    ut_checkf(met_an_empty_store > 0u, "a put-back met an empty store %u time(s)",
              (unsigned)met_an_empty_store);
    ut_checkf(agreed_on_the_empty_store == 0u,
              "the new rule agreed with the old one on an empty store %u time(s), and it has to "
              "differ there every time", (unsigned)agreed_on_the_empty_store);
    ut_checkf(disagreed_off_the_empty_store == 0u,
              "the new rule differed from the old one away from an empty store %u time(s), and "
              "it has to differ nowhere else", (unsigned)disagreed_off_the_empty_store);
}

/* ==============================================================================================
 * The old behaviour, as the reference, on the sequence the field ran into.
 * ============================================================================================ */

static void check_the_field_sequence(void)
{
    hero_t standing = { 1u, 0u, true };
    run_t  old;
    run_t  fresh;

    ut_section("the sequence a co-op client ran: the grab refused, the put-back not asked");

    start(&old, standing, true, false);
    step_grab(&old);
    (void)step_putback(&old);
    ut_check(old.hero.module_state == 0u,
             "the old behaviour writes the module to nought, which is the player who could not "
             "move for eighty seven seconds");

    start(&fresh, standing, true, true);
    step_grab(&fresh);
    (void)step_putback(&fresh);
    ut_check(fresh.hero.module_state == 1u, "the new rule leaves the module running");
    ut_check(fresh.putbacks_refused == 1u, "and it counts the refusal, which is the field number");

    ut_section("a host, whose grab goes through, keeps its scene");

    start(&fresh, standing, false, true);
    step_grab(&fresh);
    ut_check(fresh.hero.module_state == 0u, "the grab parks the module, as a scene needs");
    ut_check(fresh.hero.saved == 1u, "and the store holds what it parked");
    (void)step_putback(&fresh);
    ut_check(fresh.hero.module_state == 1u,
             "and the put-back is let through, because a grab was standing");
    ut_check(fresh.putbacks_refused == 0u, "with nothing refused on that side");

    ut_section("a put-back before any grab has ever happened, on any machine");

    start(&fresh, standing, false, true);
    (void)step_putback(&fresh);
    ut_check(fresh.hero.module_state == 1u,
             "a removal that reaches a placement nobody stood on takes nothing from a host "
             "either, which is the same crack in a retail campaign");
}

/* ==============================================================================================
 * Who asked for the camera.
 * ============================================================================================ */

static void check_the_camera_callers(void)
{
    /* The seven callers of bapview_overrideOn in the retail image, by the address each returns
     * to. The first three are a script and are the ones a refusal is for; the last four are
     * ordinary play and a deathmatch reaches three of them constantly. */
    static const uintptr_t SCRIPTS[] = {
        (uintptr_t)0x00434F4Bu,   /* the camera dolly opcode */
        (uintptr_t)0x00434F90u,   /* the lock player opcode */
        (uintptr_t)0x00430D76u,   /* a spoken line */
    };
    static const uintptr_t ORDINARY[] = {
        (uintptr_t)0x0044F22Cu,   /* the death camera when a fall turns fatal */
        (uintptr_t)0x00450505u,   /* mounting a tripod gun */
        (uintptr_t)0x0043FD50u,   /* the death screen */
        (uintptr_t)0x00417EAAu,   /* the view module putting its own group back */
    };
    size_t index;

    ut_section("the camera is refused by return address, never by argument");

    for (index = 0; index < sizeof SCRIPTS / sizeof SCRIPTS[0]; ++index) {
        ut_checkf(mp_scene_camera_is_a_script(SCRIPTS, 3u, SCRIPTS[index]),
                  "the caller returning to %08X is a script", (unsigned)SCRIPTS[index]);
    }
    for (index = 0; index < sizeof ORDINARY / sizeof ORDINARY[0]; ++index) {
        ut_checkf(!mp_scene_camera_is_a_script(SCRIPTS, 3u, ORDINARY[index]),
                  "the caller returning to %08X is ordinary play and keeps the camera",
                  (unsigned)ORDINARY[index]);
    }

    ut_section("and it fails open wherever it cannot answer");

    ut_check(!mp_scene_camera_is_a_script(SCRIPTS, 3u, (uintptr_t)0x00123456u),
             "an address nobody resolved is not a script");
    ut_check(!mp_scene_camera_is_a_script(SCRIPTS, 0u, SCRIPTS[0]),
             "an empty list matches nothing, so a build whose sites did not resolve refuses "
             "nobody");
    ut_check(!mp_scene_camera_is_a_script(NULL, 3u, SCRIPTS[0]), "and neither does no list");
    ut_check(!mp_scene_camera_is_a_script(SCRIPTS, 3u, 0u), "a nought caller is never a script");

    ut_section("a site that did not resolve is a hole in the list and matches nothing");
    {
        static const uintptr_t PARTIAL[] = {
            (uintptr_t)0x00434F4Bu, (uintptr_t)0u, (uintptr_t)0x00430D76u
        };

        ut_check(mp_scene_camera_is_a_script(PARTIAL, 3u, PARTIAL[0]),
                 "the sites that did resolve still answer");
        ut_check(!mp_scene_camera_is_a_script(PARTIAL, 3u, (uintptr_t)0x00434F90u),
                 "and the one that did not keeps its camera rather than taking somebody else's");
    }
}

/* ==============================================================================================
 * Where the camera take lives, read out of its callers.
 * ============================================================================================ */

/* A site whose call at `return_address - 5` goes to `target`, laid out as the processor reads
 * it: the opcode, then the displacement from the return address, low byte first. */
static mp_scene_call_site_t call_to(uintptr_t return_address, uint32_t target)
{
    mp_scene_call_site_t site;
    uint32_t             displacement = target - (uint32_t)return_address;

    site.return_address = return_address;
    site.call[0]        = MP_SCENE_CALL_OPCODE;
    site.call[1]        = (uint8_t)(displacement & 0xFFu);
    site.call[2]        = (uint8_t)((displacement >> 8) & 0xFFu);
    site.call[3]        = (uint8_t)((displacement >> 16) & 0xFFu);
    site.call[4]        = (uint8_t)(displacement >> 24);
    return site;
}

static void check_the_camera_callee(void)
{
    /* The five bytes in front of each script site's return address in the retail image, as they
     * stand there. Each is a call to bapview_overrideOn. */
    static const mp_scene_call_site_t RETAIL[] = {
        { (uintptr_t)0x00434F4Bu, { 0xE8u, 0xBFu, 0x34u, 0xFEu, 0xFFu } },   /* the dolly */
        { (uintptr_t)0x00434F90u, { 0xE8u, 0x7Au, 0x34u, 0xFEu, 0xFFu } },   /* lock player */
        { (uintptr_t)0x00430D76u, { 0xE8u, 0x94u, 0x76u, 0xFEu, 0xFFu } },   /* a spoken line */
    };
    const uintptr_t      take = (uintptr_t)0x0041840Au;
    mp_scene_call_site_t sites[3];
    mp_scene_call_site_t built;
    uintptr_t            entry = 1u;
    size_t               count;
    size_t               bad;

    ut_section("the camera take is where its callers say, when every one of them says the same");

    ut_check(mp_scene_camera_callee(RETAIL, 3u, &entry) == MP_SCENE_CALLEE_AGREED && entry == take,
             "the three retail calls name 0041840A, bapview_overrideOn");
    for (count = 1u; count <= 2u; ++count) {
        entry = 0u;
        ut_checkf(mp_scene_camera_callee(RETAIL, count, &entry) == MP_SCENE_CALLEE_AGREED &&
                      entry == take,
                  "%u resolved site(s) are enough to name it", (unsigned)count);
    }
    built = call_to(RETAIL[0].return_address, 0x0041840Au);
    ut_check(memcmp(built.call, RETAIL[0].call, sizeof built.call) == 0,
             "and the helper below lays a call out byte for byte as the image holds it");

    ut_section("two sites that call two places name nothing");
    for (bad = 0; bad < 3u; ++bad) {
        memcpy(sites, RETAIL, sizeof sites);
        sites[bad] = call_to(RETAIL[bad].return_address, 0x00418421u);   /* the release */
        entry      = 1u;
        ut_checkf(mp_scene_camera_callee(sites, 3u, &entry) == MP_SCENE_CALLEE_DISAGREE &&
                      entry == 0u,
                  "site %u calling the release instead leaves the take unnamed", (unsigned)bad);
    }

    ut_section("a site whose bytes are not a call names nothing");
    for (bad = 0; bad < 3u; ++bad) {
        memcpy(sites, RETAIL, sizeof sites);
        sites[bad].call[0] = 0xE9u;   /* the same displacement behind a jump */
        entry              = 1u;
        ut_checkf(mp_scene_camera_callee(sites, 3u, &entry) == MP_SCENE_CALLEE_NOT_A_CALL &&
                      entry == 0u,
                  "site %u with a jump where the call was is refused, although its displacement "
                  "still points at the take", (unsigned)bad);
    }
    memcpy(sites, RETAIL, sizeof sites);
    memset(sites[1].call, 0, sizeof sites[1].call);
    ut_check(mp_scene_camera_callee(sites, 3u, &entry) == MP_SCENE_CALLEE_NOT_A_CALL,
             "a site whose bytes could not be read keeps its zeros, and zeros are not a call");
    sites[0] = call_to((uintptr_t)0x00401000u, 0u);
    ut_check(mp_scene_camera_callee(sites, 1u, &entry) == MP_SCENE_CALLEE_NOT_A_CALL,
             "and a call to nought is not a call anybody wrote");

    ut_section("and nothing to read it from answers nothing");
    ut_check(mp_scene_camera_callee(RETAIL, 0u, &entry) == MP_SCENE_CALLEE_NO_SITES && entry == 0u,
             "no resolved site");
    ut_check(mp_scene_camera_callee(NULL, 3u, &entry) == MP_SCENE_CALLEE_NO_SITES,
             "no list");
    ut_check(mp_scene_camera_callee(RETAIL, 3u, NULL) == MP_SCENE_CALLEE_NO_SITES,
             "and nowhere to put the answer");

    ut_section("the displacement wraps as the processor's addition does, both ways");
    sites[0] = call_to((uintptr_t)0x00401000u, 0x00498EC4u);
    ut_check(mp_scene_camera_callee(sites, 1u, &entry) == MP_SCENE_CALLEE_AGREED &&
                 entry == (uintptr_t)0x00498EC4u,
             "a call forward");
    sites[0] = call_to((uintptr_t)0x00498EC4u, 0x00401000u);
    ut_check(mp_scene_camera_callee(sites, 1u, &entry) == MP_SCENE_CALLEE_AGREED &&
                 entry == (uintptr_t)0x00401000u,
             "and a call back, whose displacement is negative");
}

/* The half the pair was missing, and the field found it: a host refused its own put-back at the
 * start of a scene and stood in the caption fade with a black screen. Both halves have to ask this
 * one question, or the grab and the put-back are refused on different machines. */
static void check_the_two_halves_ask_one_question(void)
{
    ut_section("where the hero is this module's business");

    ut_check(!mp_scene_hero_is_gated_here(false, false, false),
             "a host in a campaign grabs and puts back exactly as the retail game does");
    ut_check(mp_scene_hero_is_gated_here(true, false, false),
             "a client holding a scene that belongs to the host is where the pair applies");
    ut_check(!mp_scene_hero_is_gated_here(false, true, false),
             "an arena suppresses the bars and the camera and leaves the hero alone, so a "
             "put-back it never refused a grab for must go through");
    ut_check(!mp_scene_hero_is_gated_here(true, true, false),
             "and an arena on a client is the same: the grab passes there, so the put-back does");

    /* The store still decides INSIDE that window, and only there. */
    ut_check(mp_scene_hero_is_gated_here(true, false, false) && !mp_scene_putback_allowed(0u),
             "a client with nothing parked refuses, which is the defect this module was built "
             "for");
    ut_check(mp_scene_hero_is_gated_here(true, false, false) && mp_scene_putback_allowed(3u),
             "and a client that did park lets its own put-back through");
}

/* The third reason, a host being brought to the place of a scene, and every combination of the
 * three. The grab and the put-back ask this one function: a hold that held the grab and not the
 * put-back, or the other way round, would bring back a defect a field run showed on the host or
 * on a client. */
static void check_the_third_reason(void)
{
    unsigned bits;

    ut_section("a host on his way to a scene's place holds the grab, and the put-back asks the "
               "same");
    ut_check(mp_scene_hero_is_gated_here(false, false, true),
             "a host being brought is where the pair applies: the grab waits until he stands "
             "there");
    ut_check(!mp_scene_hero_is_gated_here(false, true, true),
             "an arena brings nobody, and its hero passes as before");
    for (bits = 0u; bits < 8u; ++bits) {
        bool client = (bits & 1u) != 0u;
        bool arena  = (bits & 2u) != 0u;
        bool holds  = (bits & 4u) != 0u;
        bool gated  = mp_scene_hero_is_gated_here(client, arena, holds);

        ut_checkf(gated == ((client || holds) && !arena),
                  "client %u, arena %u, the host's hold %u: %s", (unsigned)client,
                  (unsigned)arena, (unsigned)holds, gated ? "gated" : "the engine's own");
    }

    /* The removal of an actor that waits in its hold asks the put-back. With the hold gating it
     * the store decides: nought is refused, and the module is running then, so nothing stops;
     * anything else is let through and writes a running module onto itself. */
    ut_check(mp_scene_hero_is_gated_here(false, false, true) && !mp_scene_putback_allowed(0u),
             "a removal during a hold on a host that never parked is refused, harmlessly");
    ut_check(mp_scene_hero_is_gated_here(false, false, true) && mp_scene_putback_allowed(1u),
             "and one with an old park in the store goes through");
}

/* The question the scene gate and the movie gate share, held against the inline form the pump
 * asked before it was taken out: a setup note that reads and says started, a client, no end. */
static bool the_inline_answer(bool setup_read, bool started, bool ended, bool is_client)
{
    if (setup_read && started) {
        return is_client && !ended;
    }
    return false;
}

static void check_the_session_question(void)
{
    unsigned bits;

    ut_section("the scene gate and the movie gate ask one question, and it is the old one");

    for (bits = 0u; bits < 16u; ++bits) {
        bool setup_read   = (bits & 1u) != 0u;
        bool started_flag = (bits & 2u) != 0u;
        bool ended_flag   = (bits & 4u) != 0u;
        bool is_client    = (bits & 8u) != 0u;
        /* How the pump reads the note: started is a note that read and says so, and only a
         * started session can have ended. */
        bool started = setup_read && started_flag;
        bool ended   = started && ended_flag;
        bool answer  = mp_scene_client_of_a_started_session(started, ended, is_client);

        ut_checkf(answer == the_inline_answer(setup_read, started_flag, ended_flag, is_client),
                  "note %s, started %u, ended %u, client %u: %s, as the inline form answered",
                  setup_read ? "read" : "unread", (unsigned)started_flag,
                  (unsigned)ended_flag, (unsigned)is_client, answer ? "held" : "free");
        ut_checkf(!answer || mp_scene_session_runs(started, ended),
                  "case %u: a client of a started session is a client of one that runs",
                  bits);
    }

    ut_check(mp_scene_session_runs(true, false), "a started session with no end runs");
    ut_check(!mp_scene_session_runs(true, true), "one the host has ended does not");
    ut_check(!mp_scene_session_runs(false, false), "and one never started does not");
    ut_check(!mp_scene_client_of_a_started_session(true, false, false),
             "a host is nobody's client, so neither its scenes nor its movies wait for anyone");
    ut_check(!mp_scene_client_of_a_started_session(true, true, true),
             "and a client whose host has ended the session holds nothing back any more");
}

int main(void)
{
    check_the_store_decides();
    check_every_sequence();
    check_the_field_sequence();
    check_the_camera_callers();
    check_the_camera_callee();
    check_the_two_halves_ask_one_question();
    check_the_third_reason();
    check_the_session_question();

    return ut_summary("the scene rule");
}
