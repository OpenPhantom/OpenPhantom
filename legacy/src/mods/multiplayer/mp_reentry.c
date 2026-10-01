/* mp_reentry.c: which of the two rule sets a death falls under, and the wish it turns into.
 *
 * The machinery is mp_respawn and the arena's standing positions are mp_spawnpoints. What is here
 * is the decision between them, the wait the rule set asks for, and the switch that lets a death
 * leave the level running at all.
 *
 * SIZE NOTE: past 600 lines since the co-op wipe grew a role, because the machine that used to end
 * the level on every side now ends it on one and waits on the others. The seam, if this grows
 * again, is the corpse watch and the net under it, about ninety lines from `watch_the_corpse`
 * down: they are a second question about the same player, asked once a frame rather than once a
 * death, and they share nothing with the rules above but the state block. Everything above them is
 * one subject and does not split.
 */
#include "mp_reentry.h"

#include "mp_body.h"
#include "mp_body_gate.h"
#include "mp_cells.h"
#include "mp_damage.h"
#include "mp_damage_entry.h"
#include "mp_lobby.h"
#include "mp_respawn.h"
#include "mp_rules.h"
#include "mp_seat.h"
#include "mp_seat_rule.h"
#include "mp_spawnpoints.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct mp_reentry {
    uint8_t my_slot;
    bool    my_slot_known;

    /* The session, as the host's repeated setup note last read. */
    bool       session;
    bool       is_client;    /* whose continue screen decides when nobody is standing */
    uint8_t    mode;
    mp_rules_t rules;

    /* The far players, as the interpolator last resolved them, one per far bank. Only the first
     * bank was asked, so a co-op death with the host down and a second client standing a step
     * away was the last one, and the level ended. */
    mp_reentry_peer_t peers[MP_REENTRY_MAX_PEERS];

    /* The host's substep count as the last tick read it. A death arrives inside a substep and
     * this is stamped once a frame, so a wish is at most one frame's worth of substeps late in
     * starting its wait, which is a resolution the pump does not have anyway. */
    uint32_t clock;

    /* The wish held between the death and the rule set's wait running out. */
    bool     waiting;
    bool     handed_over;      /* a wish of this module's is in the seat search, not yet landed */
    uint32_t died_at;          /* the host substep count the death was heard on */
    bool     died_suppressed;  /* this death was made inert, so nothing else ends the level */
    bool     died_position_known;
    float    died_position[3];

    /* Which life the last wish was made for: the death hull's count of lives ended when the note
     * of that death arrived. A note that finds the count where it was is the same death again. */
    bool     down_known;
    uint32_t down_life;
    uint32_t notes_of_a_life_down;

    bool survives;             /* what the damage module was last told */
    bool survives_told;

    /* Every path out of this module increments exactly one of these. */
    uint32_t deaths_seen;
    uint32_t deaths_far;        /* somebody else's, counted and dropped */
    uint32_t deaths_outside;    /* no started session: the retail death, untouched */
    uint32_t wished_beside;
    uint32_t wished_at_point;
    uint32_t carried_out;
    uint32_t refused;           /* the re-entry module turned the wish down */
    uint32_t given_up;          /* the wait ran out with no seat and no point */
    uint32_t no_anchor;         /* co-op, and nobody is standing */
    uint32_t no_point;          /* deathmatch, and the level has no spawn point */
    uint32_t levels_ended;      /* the last player standing died and the level was told so */
    uint32_t level_end_faults;
    uint32_t survival_grants;   /* transitions of the switch, so the log shows it moving */
    uint32_t survival_revokes;
    bool     wipe_said;         /* the waiting line is one per wipe, not one per frame */
    uint32_t wipes_waited;      /* wipes this client waited out rather than ending the level */
    uint32_t taken_back;        /* wishes the seat search held when nobody stood any more */
    uint32_t taken_back_ended;  /* of those, the host's: the level ended */
    uint32_t taken_back_waited; /* and a client's: it waited for the host's screen */

    /* THE CORPSE WATCH. The engine's dead flag as this tick reads it, how long it has stood, and
     * whether this life has already been spoken about. A player who is dead and not on his way
     * back is the one state this feature can produce that a player cannot get out of: the engine
     * refuses a corpse its pause menu, so there is no screen to leave the level by and no line
     * anywhere saying why. Everything above counts what DID happen; this counts what stopped. */
    bool     corpse;
    uint32_t corpse_frames;
    bool     spoke_waiting;     /* the line about a way back being tried was written */
    bool     spoke_stuck;       /* and the one about a corpse nothing brings back */
    uint32_t corpses_stuck;     /* lives that stood still long enough to be named */
} mp_reentry_t;

static mp_reentry_t re;

/* ==============================================================================================
 * The pure decisions.
 * ============================================================================================ */

/* Who ends a co-op level in which nobody is standing.
 *
 * Every machine reads this for itself, out of the far players it can see, and every machine used
 * to answer the same thing: end the level. The engine then handed each player their own continue
 * screen, and what each of them picked there was their own world: a client that picked RESTART
 * before the host had picked anything began a second campaign, with its own doors, its own
 * pickups and its own story flags, and nothing on either screen said so.
 *
 * So the decision belongs to the one machine whose world everybody else follows. A client waits
 * instead, as a corpse in a level that is still running, and takes whatever the host picks through
 * the world change it announces. It is not a wait without an end: the host's choice, the host
 * ending the session and the host going quiet for thirty seconds are all ways out of it that are
 * already built. */
mp_reentry_rule_t mp_reentry_rule_for(bool session, uint8_t mode, bool anchor_alive,
                                      bool is_client)
{
    if (!session) {
        return MP_REENTRY_RULE_NONE;
    }
    if (mode == (uint8_t)MP_LOBBY_MODE_TDM) {
        return MP_REENTRY_RULE_AT_POINT;
    }
    if (mode == (uint8_t)MP_LOBBY_MODE_COOP) {
        if (anchor_alive) {
            return MP_REENTRY_RULE_BESIDE;
        }
        return is_client ? MP_REENTRY_RULE_WAIT_FOR_HOST : MP_REENTRY_RULE_LAST_MAN;
    }
    return MP_REENTRY_RULE_NONE;
}

bool mp_reentry_may_survive(bool session, uint8_t mode, bool anchor_alive,
                            bool machinery_ready, bool is_client)
{
    mp_reentry_rule_t rule = mp_reentry_rule_for(session, mode, anchor_alive, is_client);

    if (!machinery_ready) {
        return false;
    }
    /* A client waiting for its host survives too, and that is the whole of the waiting: the death
     * stays inert, so the engine never writes the outcome and never reaches for the continue
     * screen. Take this away and the switch is revoked while the body is down, the retail death
     * runs, and the client is looking at its own screen again. */
    return rule == MP_REENTRY_RULE_BESIDE || rule == MP_REENTRY_RULE_AT_POINT ||
           rule == MP_REENTRY_RULE_WAIT_FOR_HOST;
}

uint32_t mp_reentry_wait_for(mp_reentry_rule_t rule, const mp_rules_t *rules)
{
    if (rule != MP_REENTRY_RULE_AT_POINT || rules == NULL) {
        return 0u;
    }
    return mp_rules_respawn_substeps(rules);
}

bool mp_reentry_wait_is_over(uint32_t died_at, uint32_t now, uint32_t wait_substeps)
{
    return (uint32_t)(now - died_at) >= wait_substeps;
}

bool mp_reentry_is_ours(uint8_t victim_slot, uint8_t my_slot, bool my_slot_known)
{
    return my_slot_known && victim_slot == my_slot;
}

bool mp_reentry_death_is_new(bool lives_known, uint32_t lives, bool down_known,
                             uint32_t down_life)
{
    /* Without the death hull's count every note is taken for a death, as it always was. */
    return !lives_known || !down_known || lives != down_life;
}

/* ==============================================================================================
 * What the frame pump hands in.
 * ============================================================================================ */

void mp_reentry_note_my_slot(uint8_t slot)
{
    re.my_slot       = slot;
    re.my_slot_known = true;
}

void mp_reentry_note_session(uint8_t mode, bool is_client, const mp_rules_t *rules)
{
    re.session   = true;
    re.is_client = is_client;
    re.mode      = mode;
    if (rules != NULL) {
        re.rules = *rules;
    }
    /* The gate in front of the handler is told the one answer to "does this player live" that the
     * landing and the corpse watch ask, so that its count of an empty slot on a living body means
     * the same body they mean. */
    mp_body_gate_set_life_test(&mp_respawn_player_lives);
}

void mp_reentry_note_no_session(void)
{
    re.session = false;
}

/* The pose is the one the interpolator resolved for the far body, kept beside the puppet's copy
 * rather than recomputed. The heading is the blend of the hero block's word at +0x2A0, the same
 * field the engine's own re-entry writes and the same unit a level's authored placement carries,
 * so it round trips with no conversion. `alive` and the wire's dead flag are not each other's
 * negation: a body can be neither yet, which is why the rule asks for alive and not dead. */
void mp_reentry_note_peer(size_t index, const float position[3], float heading, bool alive)
{
    if (index >= MP_REENTRY_MAX_PEERS || position == NULL) {
        return;
    }
    re.peers[index].known = true;
    re.peers[index].alive = alive;
    memcpy(re.peers[index].position, position, sizeof re.peers[index].position);
    re.peers[index].heading = heading;
}

void mp_reentry_note_no_peer(size_t index)
{
    if (index < MP_REENTRY_MAX_PEERS) {
        re.peers[index].known = false;
        re.peers[index].alive = false;
    }
}

/* The seat search tries the standing players in an order of its own on every look, and this is
 * the first of that order, asked of the same rule so that the two cannot disagree about who stands
 * nearest. */
int mp_reentry_pick_anchor(const mp_reentry_peer_t *peers, size_t count, const float *died_at)
{
    mp_seat_body_t bodies[MP_REENTRY_MAX_PEERS];
    size_t         order[1];
    size_t         i;

    if (peers == NULL) {
        return -1;
    }
    if (count > MP_REENTRY_MAX_PEERS) {
        count = MP_REENTRY_MAX_PEERS;
    }
    for (i = 0; i < count; ++i) {
        bodies[i].known  = peers[i].known;
        bodies[i].stands = peers[i].alive;
        memcpy(bodies[i].position, peers[i].position, sizeof bodies[i].position);
        bodies[i].heading = peers[i].heading;
    }
    return mp_seat_rule_anchor_order(bodies, count, died_at, order, 1u) == 1u ? (int)order[0] : -1;
}

/* Whether there is somebody to come back beside: ANY far player standing. */
static bool anchor_is_alive(void)
{
    return mp_reentry_pick_anchor(re.peers, MP_REENTRY_MAX_PEERS, NULL) >= 0;
}

/* How many far players this machine has a pose for, and how many of those are standing. */
static void count_peers(uint32_t *resolved, uint32_t *standing)
{
    size_t i;

    *resolved = 0u;
    *standing = 0u;
    for (i = 0; i < MP_REENTRY_MAX_PEERS; ++i) {
        if (re.peers[i].known) {
            ++*resolved;
            if (re.peers[i].alive) {
                ++*standing;
            }
        }
    }
}

/* ==============================================================================================
 * The switch that lets a death leave the level running.
 * ============================================================================================ */

/* Set from the tick rather than once at the install, because every one of its inputs moves: a
 * session starts, a mode is learned from the host, and a far player goes down and comes back.
 * The value is at most one frame behind the world, which is the resolution the pump has. */
static void apply_survival(void)
{
    /* Three modules, and a death is only survivable while all three stand: the hull that makes
     * the corpse inert, the machinery that brings the player back, and something listening for
     * the death, because a death nobody hears about never becomes a wish. */
    bool ready   = mp_respawn_installed() && mp_damage_installed() && mp_body_death_is_reported();
    bool allowed = mp_reentry_may_survive(re.session, re.mode, anchor_is_alive(), ready,
                                          re.is_client);

    if (re.survives_told && allowed == re.survives) {
        return;
    }
    re.survives_told = true;
    re.survives      = allowed;
    mp_damage_set_survives_death(allowed);
    if (allowed) {
        ++re.survival_grants;
        log_info("a death of this player no longer ends the level: there is a way back into it");
    } else {
        ++re.survival_revokes;
        log_info("a death of this player ends the level again, as it does without a session: %s",
                 !ready ? "the death hull, the engine's own re-entry or the report of a death "
                          "is missing"
                     : (!re.session ? "no session has started"
                                    : "nobody else is standing to come back beside"));
    }
}

/* ==============================================================================================
 * The last player standing.
 * ============================================================================================ */

/* Nobody is left to come back beside, and the death that would have ended the level was made
 * inert, so nothing else will raise the outcome. The level is told here instead.
 *
 * Only when this feature really did suppress the death: one that ran the retail path has raised
 * the cell already, and writing over that could turn a completed level into a failed one.
 *
 * A host reloading the world so that every player carries on is the step that will replace this;
 * until it exists, ending the level is what stops a session standing still with nobody but
 * corpses in it, and it is the outcome a player already knows from dying alone. */
/* Whether the campaign loop still holds this level open. It spins while the outcome cell reads 2
 * and leaves it for any other value, so any other value means the engine is already on its way to
 * a screen and nothing here should write over that. A cell that did not resolve answers "running",
 * which is the reading that makes a caller act rather than pass over a stuck player in silence.
 * The cell is read out of an operand of the level handover site; 1 rolls the credits, 3 is a
 * completed level and 4 and above are a death, which is the continue screen. Nobody has yet
 * watched a co-op wipe end a level through the write below; the value is this tree's own reading
 * of the campaign loop and of the cell's recorded meaning. */
static bool level_is_still_running(void)
{
    uintptr_t cell = mp_cells_address(MP_CELL_GAME_MODE);
    uint32_t  outcome = 0u;

    return cell == 0u || !memory_try_read_u32(cell, &outcome) ||
           outcome == MP_REENTRY_OUTCOME_RUNNING;
}

/* The write itself, shared by the two situations that need it, so the value and the fault counting
 * cannot drift apart between them. */
static bool raise_the_outcome(const char *why)
{
    uintptr_t cell = mp_cells_address(MP_CELL_GAME_MODE);

    if (cell == 0u || patch_write_u32(cell, MP_REENTRY_OUTCOME_DEATH) != PATCH_RESULT_OK) {
        ++re.level_end_faults;
        log_error("%s and the level outcome could not be raised, so the level will go on running "
                  "with nobody alive in it", why);
        return false;
    }
    ++re.levels_ended;
    return true;
}

static void end_the_level(void)
{
    /* Asked of the DEATH rather than of the switch as it stands now. By the time this runs the
     * switch has already been revoked for the very reason that brought us here, so reading it
     * would say the death was never suppressed and leave the level running. */
    if (!re.died_suppressed) {
        return;   /* the engine ended it itself */
    }
    if (raise_the_outcome("nobody is left standing")) {
        log_info("nobody is left standing, so the level is ended the way a death ends it. "
                 "Reloading the world for everybody is a step this build does not have");
    }
}

mp_reentry_corpse_t mp_reentry_corpse_state(bool corpse, uint32_t corpse_frames,
                                            bool wish_pending, bool waiting_for_host,
                                            bool session, bool level_running)
{
    if (!corpse || corpse_frames < MP_REENTRY_CORPSE_PATIENCE_FRAMES) {
        return MP_REENTRY_CORPSE_NO;
    }
    /* Waiting for the host outranks the general "a wish is being tried", because it is the one
     * wish that is kept on purpose and has three ways out of itself. */
    if (waiting_for_host) {
        return MP_REENTRY_CORPSE_HOST;
    }
    if (wish_pending) {
        return MP_REENTRY_CORPSE_WAITING;
    }
    /* No session to be stuck in, or a level the engine has ended itself and is already showing a
     * screen to leave by. */
    if (!session || !level_running) {
        return MP_REENTRY_CORPSE_NO;
    }
    return MP_REENTRY_CORPSE_STUCK;
}

/* The net under a corpse nothing is bringing back. It does not ask who suppressed the death,
 * because the answer is not knowable from here and the player's situation is the same either way:
 * the level's own outcome says it is still running, and the body in it is dead. */
static void end_the_level_for_a_stuck_corpse(void)
{
    if (raise_the_outcome("this player is a corpse nothing is bringing back")) {
        log_warning("the level is ended the way a death ends it, so this player at least reaches "
                    "the continue screen instead of a level he cannot leave. This is a net, not a "
                    "way back: the line above it says which link failed");
    }
}

/* ==============================================================================================
 * A death, and the wish it becomes.
 * ============================================================================================ */

/* Where this machine's player was when he died, read off the hero block while the corpse is still
 * standing there. It is what keeps a deathmatch from putting somebody back on the square he was
 * just killed on. False leaves the choice to the distance from the living alone, which is a worse
 * choice rather than a wrong one. */
static bool read_death_position(float out[3])
{
    return mp_cells_hero_position(out);
}

void mp_reentry_note_death(const mp_death_note_t *note)
{
    uint32_t lives = 0u;
    bool     lives_known;

    if (note == NULL) {
        return;
    }
    ++re.deaths_seen;
    if (!mp_reentry_is_ours(note->victim_slot, re.my_slot, re.my_slot_known)) {
        ++re.deaths_far;
        return;
    }
    if (!re.session) {
        ++re.deaths_outside;
        return;
    }

    /* One wish per life. A note of a death whose life is already down, the corpse struck again
     * or a report that was on its way, makes no second wish and does not move the place of the
     * death to wherever the body lies by then. A death of the next life, even one that ends it
     * before it was seen standing, is a new one and replaces the wish. */
    lives_known = mp_damage_entry_lives_ended(&lives);
    if (!mp_reentry_death_is_new(lives_known, lives, re.down_known, re.down_life)) {
        ++re.notes_of_a_life_down;
        return;
    }
    re.down_known = lives_known;
    re.down_life  = lives;
    mp_seat_note_life_ended();

    re.waiting             = true;
    re.died_at             = re.clock;
    re.died_suppressed     = mp_damage_survives_death();
    re.died_position_known = read_death_position(re.died_position);
    log_info("this player died on world slot %u and wants back into the level",
             (unsigned)note->victim_slot);
}

/* Co-op: beside the players who are standing, the one nearest to where this one died first, with
 * no delay. The wish carries where the death was and this player's slot, not a pose: the seat is
 * searched when the gates open, around the far players as they stand then, and a pose copied here
 * would be several seconds stale by that time and would freeze an anchor that may have walked into
 * a place with no room beside it. The poses are the interpolator's, so nothing new travels. */
static bool run_beside(void)
{
    ++re.wished_beside;
    if (anchor_is_alive() &&
        mp_respawn_beside(re.died_position_known ? re.died_position : NULL, re.my_slot, 0u)) {
        return true;
    }
    ++re.refused;
    return false;
}

/* Deathmatch: a standing position the level's own author put a body on, chosen away from whoever
 * is up and away from the square this player was just killed on. The choice is made now rather
 * than at the death, because the living have moved since. */
static bool run_at_point(void)
{
    float  living[MP_REENTRY_MAX_PEERS][3];
    size_t living_count = 0u;
    float  position[3];
    float  heading = 0.0f;
    size_t i;

    for (i = 0; i < MP_REENTRY_MAX_PEERS; ++i) {
        if (re.peers[i].known && re.peers[i].alive) {
            memcpy(living[living_count], re.peers[i].position, sizeof living[0]);
            ++living_count;
        }
    }
    if (!mp_spawnpoints_take(living, living_count,
                             re.died_position_known ? re.died_position : NULL,
                             mp_spawnpoints_now(), position, &heading)) {
        ++re.no_point;
        return false;
    }
    ++re.wished_at_point;
    if (mp_respawn_at(position, heading, re.my_slot, 0u)) {
        return true;
    }
    ++re.refused;
    return false;
}

/* Whether the engine holds this machine's player for a corpse right now, asked of the one answer
 * the death hull and the landing ask as well. False when the block cannot be read, which is a
 * level load or the front end and is not a death. The dead flag at +0x394 is read first because
 * the death entry writes the 1 and nothing clears it in place: the body that comes back is a new
 * one out of the hero spawn, so a 0 again means a spawn happened rather than that somebody changed
 * their mind. The pause refusal that makes a corpse a trap is the engine's own: its pause entry at
 * 0x0043F678 tests whether the player is dead first and opens nothing for one. */
static bool player_is_a_corpse(void)
{
    return mp_respawn_player_is_a_corpse();
}

/* The one line that answers "I died and nothing happened".
 *
 * Every counter in this file grows when something works. None of them grows when a player dies
 * and no part of this chain hears about it, and that is exactly the state a player cannot leave:
 * the corpse stands, the level does not end because the death was made inert, and the engine will
 * not open a pause menu for a corpse. Until this line existed the whole failure was silent on
 * both machines.
 *
 * Said once per life, and it names which link is missing rather than that one is: the wish, the
 * session, the anchor, or the death nobody heard. */
static void watch_the_corpse(void)
{
    bool     corpse = player_is_a_corpse();
    uint32_t resolved = 0;
    uint32_t standing = 0;

    if (!corpse) {
        re.corpse        = false;
        re.corpse_frames = 0u;
        re.spoke_waiting = false;
        re.spoke_stuck   = false;
        return;
    }
    re.corpse = true;
    ++re.corpse_frames;
    switch (mp_reentry_corpse_state(true, re.corpse_frames,
                                    mp_respawn_pending() || re.waiting,
                                    mp_reentry_waiting_for_host(), re.session,
                                    level_is_still_running())) {
    case MP_REENTRY_CORPSE_HOST:
        /* A state with an end and three ways out of it, so it is said plainly rather than warned
         * about: this is not the stuck corpse the watch exists for. */
        if (!re.spoke_waiting) {
            re.spoke_waiting = true;
            log_info("this player has been down for %u frames with nobody standing, waiting for "
                     "the host to pick the next world", (unsigned)re.corpse_frames);
        }
        return;
    case MP_REENTRY_CORPSE_WAITING:
        if (!re.spoke_waiting) {
            re.spoke_waiting = true;
            log_warning("this player has been a corpse for %u frames and the way back is still "
                        "being tried: %s. The pause menu stays shut until he stands",
                        (unsigned)re.corpse_frames,
                        re.waiting ? "the rule set is holding the wish"
                                   : "a seat is being looked for");
        }
        return;
    case MP_REENTRY_CORPSE_STUCK:
        break;
    case MP_REENTRY_CORPSE_NO:
    default:
        return;
    }
    if (re.spoke_stuck) {
        return;   /* named and let out once; the outcome is raised by the net below, not here */
    }
    re.spoke_stuck = true;
    ++re.corpses_stuck;
    count_peers(&resolved, &standing);
    log_error("THIS PLAYER IS A CORPSE THAT NOTHING IS BRINGING BACK, %u frames on, in a level "
              "that is still running. The engine will not open a pause menu for a corpse, so "
              "there is no way out of it at all. Missing link: %s. The game is %u, %u other "
              "player(s) are standing of %u resolved here, and %u death(s) have been heard "
              "here",
              (unsigned)re.corpse_frames,
              re.deaths_seen == 0u
                  ? "NO DEATH WAS EVER REPORTED HERE, so no wish was ever made"
                  : (anchor_is_alive()
                         ? "a death was heard and the wish is gone: it was given up or refused"
                         : "nobody is standing to come back beside, and the level was not ended "
                           "either"),
              (unsigned)re.mode, (unsigned)standing, (unsigned)resolved,
              (unsigned)re.deaths_seen);

    /* And the player is let out, because a level with no way out is worse than a level that ends.
     *
     * This is a net rather than a feature, and it is written to catch a state nobody can name in
     * advance: whatever went wrong upstream, what the player is looking at is a corpse, a world
     * that goes on running around it and a menu key that does nothing. Raising the outcome hands
     * him the continue screen he already knows from dying alone, from which he can leave.
     *
     * Its conditions are what keep it from ever firing in ordinary play: the level's own outcome
     * cell still has to read "running", so a death the engine handled itself is not touched; no
     * wish may be held and no re-entry may be on its way, so a slow seat search is not cut short;
     * and it waits out the patience first. If this fires at all, something above it is broken and
     * the line before it says what. */
    end_the_level_for_a_stuck_corpse();
}

/* Nobody standing has one answer, the rule set's, and it is asked again for as long as a wish that
 * was handed over is still looking for its seat. At the hand-over somebody stood; if nobody does
 * any more before the seat is found, the wish comes back here, where the host's rule ends the level
 * and a client's waits for the host's screen, exactly as if nobody had stood at the death. Left to
 * the seat search, the fallback would put this player alone on an authored point and no wipe would
 * take place. A body the engine is already fading back in stands up of its own and is not taken. */
static void ask_the_rule_again(void)
{
    mp_reentry_rule_t rule;

    if (re.waiting || !re.handed_over) {
        return;
    }
    if (!mp_respawn_pending()) {
        re.handed_over = false;   /* it landed, or the re-entry dropped it */
        return;
    }
    rule = mp_reentry_rule_for(re.session, re.mode, anchor_is_alive(), re.is_client);
    if (rule != MP_REENTRY_RULE_LAST_MAN && rule != MP_REENTRY_RULE_WAIT_FOR_HOST) {
        return;
    }
    if (!mp_respawn_withdraw()) {
        return;
    }
    re.handed_over = false;
    re.waiting     = true;   /* the wish is the rule set's again, and the tick below takes it */
    ++re.taken_back;
    if (rule == MP_REENTRY_RULE_LAST_MAN) {
        ++re.taken_back_ended;
    } else {
        ++re.taken_back_waited;
    }
    log_info("nobody is standing any more, so the re-entry handed to the seat search is taken back "
             "before any fallback could seat this player: %s",
             rule == MP_REENTRY_RULE_LAST_MAN ? "the level ends here"
                                              : "this client waits for the host's screen");
}

void mp_reentry_tick(uint32_t host_substeps)
{
    mp_reentry_rule_t rule;
    uint32_t          wait;

    re.clock = host_substeps;
    apply_survival();
    /* Before the wish is acted on, so a corpse that is on its way back this frame is described as
     * one rather than as a corpse nothing is doing anything about. */
    watch_the_corpse();
    /* Before the seat search's own tick, which the frame pump runs after this one. */
    ask_the_rule_again();

    if (!re.waiting) {
        return;
    }

    rule = mp_reentry_rule_for(re.session, re.mode, anchor_is_alive(), re.is_client);
    if (rule == MP_REENTRY_RULE_NONE) {
        re.waiting = false;
        ++re.given_up;
        /* And whatever was already handed over, because a wish carried out after the session it
         * belonged to has gone puts a player back into a level nobody is playing with him. */
        mp_respawn_cancel();
        log_warning("a re-entry was being held and the session it belonged to is gone, so it is "
                    "dropped");
        return;
    }
    if (rule == MP_REENTRY_RULE_LAST_MAN) {
        re.waiting = false;
        ++re.no_anchor;
        end_the_level();
        return;
    }
    if (rule == MP_REENTRY_RULE_WAIT_FOR_HOST) {
        /* The wish is KEPT. The moment any far player stands again the rule turns back into
         * BESIDE and the wish is carried out beside them, which is what happens when the host
         * comes back through its own re-entry rather than through a new world. */
        if (!re.wipe_said) {
            re.wipe_said = true;
            ++re.wipes_waited;
            log_info("every player is down as far as this client can see; it waits as a corpse "
                     "for the host's continue screen, which decides the next world for everybody");
        }
        return;
    }
    wait = mp_reentry_wait_for(rule, &re.rules);
    if (!mp_reentry_wait_is_over(re.died_at, host_substeps, wait)) {
        return;
    }
    if (mp_respawn_pending()) {
        return;   /* one is already being carried out; a second would ask for a seat twice */
    }

    if (rule == MP_REENTRY_RULE_BESIDE ? run_beside() : run_at_point()) {
        re.waiting     = false;
        re.handed_over = true;
        re.wipe_said   = false;   /* a later wipe is a later line */
        ++re.carried_out;
        return;
    }
    /* Kept and tried again next frame. A level that has not opened yet has no spawn points and no
     * standing body to step away from, and both of those end. */
    if (mp_reentry_wait_is_over(re.died_at, host_substeps, wait + MP_REENTRY_GIVE_UP_SUBSTEPS)) {
        re.waiting = false;
        ++re.given_up;
        log_warning("this player asked to come back %u substep(s) ago and neither a seat beside "
                    "the living nor a free spawn point has answered since, so the wish is "
                    "dropped", (unsigned)(host_substeps - re.died_at));
    }
}

bool mp_reentry_waiting_for_host(void)
{
    return re.waiting &&
           mp_reentry_rule_for(re.session, re.mode, anchor_is_alive(), re.is_client) ==
               MP_REENTRY_RULE_WAIT_FOR_HOST;
}

void mp_reentry_report(void)
{
    uint32_t resolved = 0;
    uint32_t standing = 0;

    count_peers(&resolved, &standing);
    log_info("  the wipes: %u time(s) every player was down and this side waited for the host's "
             "screen instead of ending its own level", (unsigned)re.wipes_waited);
    log_info("  the wipe after a hand-over: %u time(s) nobody was standing any more while this "
             "player's seat was still being searched, so the wish went back to the rule set: %u "
             "of them ended the level here, %u waited for the host's screen",
             (unsigned)re.taken_back, (unsigned)re.taken_back_ended,
             (unsigned)re.taken_back_waited);
    log_info("  the re-entry rules: %u death(s) heard, %u of them somebody else's, %u with no "
             "session; %u asked beside a living player, %u at a spawn point, %u handed over, %u "
             "refused, %u given up; %u death note(s) of a life already down, not a new wish",
             (unsigned)re.deaths_seen, (unsigned)re.deaths_far, (unsigned)re.deaths_outside,
             (unsigned)re.wished_beside, (unsigned)re.wished_at_point, (unsigned)re.carried_out,
             (unsigned)re.refused, (unsigned)re.given_up, (unsigned)re.notes_of_a_life_down);
    log_info("  the two dead ends: %u time(s) nobody was left standing in a co-op level (%u level "
             "end(s) written, %u fault(s)), %u time(s) the level offered no spawn point out of the "
             "%u it holds", (unsigned)re.no_anchor, (unsigned)re.levels_ended,
             (unsigned)re.level_end_faults, (unsigned)re.no_point,
             (unsigned)mp_spawnpoints_count());
    log_info("  the corpse watch: this player is %s and %u life/lives stood still long enough to "
             "be named and let out. A life named here is a level with no pause menu and no way "
             "out, so any number above nought is the defect the player reported",
             re.corpse ? "A CORPSE RIGHT NOW" : "not a corpse", (unsigned)re.corpses_stuck);
    log_info("  the survival switch: %s, granted %u time(s) and revoked %u; this machine holds "
             "world slot %u (%s), the game is %u and %u other player(s) are standing of %u "
             "resolved here",
             re.survives ? "ON, a death does not end the level" : "off, a death ends the level",
             (unsigned)re.survival_grants, (unsigned)re.survival_revokes, (unsigned)re.my_slot,
             re.my_slot_known ? "set" : "NEVER SET", (unsigned)re.mode, (unsigned)standing,
             (unsigned)resolved);
}
