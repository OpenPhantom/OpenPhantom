/* mp_reentry.h: what a death of this machine's player means, and which of the two rule sets
 * decides where he comes back.
 *
 * ================================ One machinery, two rule sets ================================
 *
 * mp_respawn is the machinery: it writes the health, calls the engine's own re-entry, probes a
 * seat and waits for the two engine gates. It knows no game mode on purpose, because a caller
 * says WHERE and WHEN and nothing else. This file is that caller, and it is the whole of the
 * difference between the two games:
 *
 *   co-op        beside whoever is still standing, at once. The pose of the far player is already
 *                in every machine's interpolator, so nothing new travels for this;
 *   deathmatch   at a spawn point, after the wait the host configured, chosen away from the
 *                living and away from the square the player was just killed on.
 *
 * ============================= The last player standing is a real end =========================
 *
 * A death in a session is survivable only while the re-entry can actually happen, and in co-op
 * that means somebody else is still up. When the last one dies there is nothing to come back
 * beside, and the correct behaviour is the one the game already has: the level ends and the
 * player is offered the continue screen. So the survival switch is withheld in that case rather
 * than granted and then regretted, and a session that loses its last standing player after the
 * switch was already granted has the level ended here instead, because the death that would have
 * ended it was made inert and nothing else will raise the outcome.
 *
 * That answer is this file's alone, and it holds until the player stands again: a wish already
 * handed to the seat search is taken back when nobody stands any more, before the search's own
 * fallback could put the player alone on an authored point. On a client the answer is to wait for
 * the host's screen, which decides the next world for everybody.
 *
 * What this file does not do is reload the world. A host taking everybody into a fresh level
 * after a wipe is its own step and is not built; what is built is that the level ENDS rather than
 * standing there with two corpses in it.
 *
 * ================================== Everything is pushed in ===================================
 *
 * Which slot this machine holds, where the other players are and what game is being played are
 * all the bridge's answers. They are handed to this file once per frame rather than read out of
 * it, so that the rules can be driven with no session, no socket and no game in the process, and
 * so that the wire layer stays ignorant of the rule that consumes it.
 *
 * ==================================== The two clocks ==========================================
 *
 * The rule set counts its wait in SUBSTEPS, because that is the ladder both machines agree on.
 * mp_respawn counts its gate deadline in drawn FRAMES, because its own tick is the frame pump and
 * a wish must not expire while a level loads. The two are not the same ladder, so the wait is held
 * here, in substeps, and mp_respawn is asked for a re-entry with no delay once it has run out.
 * Handing the rule set's substeps to mp_respawn as a frame count would have been a silent unit
 * change: at sixty drawn frames a second the shipped default of five seconds, 160 substeps,
 * would have come back after 2.7 seconds, and at thirty after 5.3, neither the number the host
 * set. Holding it here also means the spawn point is chosen when the wait is over rather than
 * when the player died, against where the living are standing at that moment. The seat's own
 * clock, which ends a search that finds nothing, counts substeps again, handed to mp_respawn by
 * the pump.
 */
#ifndef MULTIPLAYER_MP_REENTRY_H
#define MULTIPLAYER_MP_REENTRY_H

#include "mp_hit_relay.h"
#include "mp_rules.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How long past its own wait a wish keeps trying before it is given up, in substeps of the ladder
 * the host counts. Twenty seconds: long enough for a level load and a lift to finish moving, short
 * enough that a player who is never coming back learns it from the log rather than from waiting. */
#define MP_REENTRY_GIVE_UP_SUBSTEPS 640u

/* How long a corpse may lie before the run report says so, in DRAWN FRAMES, because the corpse
 * watch runs from the frame pump and the pump is the one clock that keeps running while the
 * simulation does not. Four seconds at sixty: past every fade the engine's own death plays, and
 * well short of the twenty a wish is given before it is dropped, so a player who is genuinely
 * stuck is named while he is still looking at the screen wondering. */
#define MP_REENTRY_CORPSE_PATIENCE_FRAMES 240u

/* What the outcome cell is raised to when a co-op session has nobody left standing. The campaign
 * loop spins while that cell reads 2 and leaves it for any other value; 1 rolls the credits and 3
 * is a completed level, so neither of those is a death. Four is the lowest value the loop reads as
 * a death, which is the continue screen a player already knows from dying alone. */
#define MP_REENTRY_OUTCOME_DEATH 4u

/* And what the same cell reads while the level is simply being played. Any other value means the
 * engine is already on its way to a screen of its own, and a reader that wants to raise the
 * outcome has to leave it alone: writing over a 3 turns a completed level into a failed one. */
#define MP_REENTRY_OUTCOME_RUNNING 2u

/* Which rule the moment falls under. */
typedef enum mp_reentry_rule {
    MP_REENTRY_RULE_NONE,       /* no started session, or a mode this build does not know */
    MP_REENTRY_RULE_BESIDE,     /* co-op: beside whoever is still standing, at once */
    MP_REENTRY_RULE_AT_POINT,   /* deathmatch: a spawn point, after the rule set's wait */
    MP_REENTRY_RULE_LAST_MAN,   /* co-op with nobody standing, on the host: the level ends */
    MP_REENTRY_RULE_WAIT_FOR_HOST /* the same on a client: the host's screen decides for all */
} mp_reentry_rule_t;

/* ==============================================================================================
 * The pure decisions, so a test can pin them with no game in the process.
 * ============================================================================================ */

/* Which rule applies. A deathmatch never asks whether anybody else is standing: a player who is
 * alone in one still comes back, because a round with one player in it is a round that is waiting
 * for a second. Co-op is the opposite, because coming back beside nobody is not a place. */
mp_reentry_rule_t mp_reentry_rule_for(bool session, uint8_t mode, bool anchor_alive,
                                      bool is_client);

/* Whether a death of this machine's player may leave the level running at all. It is the rule
 * above and one more condition, and it is defined in terms of the rule rather than beside it so
 * that the two cannot drift apart: the switch may go on exactly when there is a re-entry to come
 * back through and the machinery standing to carry it out.
 *
 * `machinery_ready` is TWO modules and both have to stand: the one that makes a death survivable,
 * and the one that brings the player back afterwards. Without the first the switch is a promise
 * nothing keeps; without the second a build where a site failed to resolve would hold a player as
 * a corpse for ever, which is worse than the retail death it replaced. */
bool mp_reentry_may_survive(bool session, uint8_t mode, bool anchor_alive,
                            bool machinery_ready, bool is_client);

/* How long the rule makes a dead player wait, in substeps. Co-op waits not at all: the anchor is a
 * body that keeps walking, and every substep of waiting only puts it somewhere else. A deathmatch
 * waits exactly what the host set, which is the one number in the rule set a player watching a
 * respawn counter can see. */
uint32_t mp_reentry_wait_for(mp_reentry_rule_t rule, const mp_rules_t *rules);

/* Whether the rule set's wait has run out. Written as a difference so that a counter which has
 * wrapped is still read right, and a wait of zero is over on the substep it started. */
bool mp_reentry_wait_is_over(uint32_t died_at, uint32_t now, uint32_t wait_substeps);

/* ---- the corpse watch ----------------------------------------------------------------------- */

/* What the watch does about this machine's player this frame. */
typedef enum mp_reentry_corpse {
    MP_REENTRY_CORPSE_NO = 0,   /* no corpse, or one the engine is still playing out */
    MP_REENTRY_CORPSE_WAITING,  /* a way back is being tried: said once, and left to be tried */
    MP_REENTRY_CORPSE_HOST,     /* down with nobody standing, waiting for the host's next world */
    MP_REENTRY_CORPSE_STUCK     /* nothing is bringing him back: named, and let out */
} mp_reentry_corpse_t;

/* The rule, over facts the caller has gathered.
 *
 * It is a rule and not a flag because of the defect it repairs. The watch kept ONE latch for
 * everything it said, so a corpse that had a wish in flight when the patience ran out was
 * described once and never looked at again; when the wish was given up twenty seconds later the
 * net under it was already mute, and the player sat in a level with no pause menu and no way out
 * of it (field run 2026-09-17, team deathmatch on bridge.b3d). WAITING and STUCK are the same
 * facts with and without a wish, so each of them carries its own latch at the caller. */
mp_reentry_corpse_t mp_reentry_corpse_state(bool corpse, uint32_t corpse_frames,
                                            bool wish_pending, bool waiting_for_host,
                                            bool session, bool level_running);

/* Whether a death that arrived is one this machine has to act on. A death with no slot set for
 * this machine is nobody's, because acting on it would put THIS player back for somebody else's
 * death. */
bool mp_reentry_is_ours(uint8_t victim_slot, uint8_t my_slot, bool my_slot_known);

/* Whether a note of this player's death is a death of its own or another report of one already
 * wished for. `lives` is the death hull's count of lives ended when the note arrived and
 * `down_life` the count the last wish was made at; the same count is the same death, however many
 * reports of it arrive. Quake 3 counts spawns in the player's state for the same question. A hull
 * that never stood answers `lives_known` false, and every note is a death, as before the count. */
bool mp_reentry_death_is_new(bool lives_known, uint32_t lives, bool down_known,
                             uint32_t down_life);

/* ==============================================================================================
 * What the frame pump hands in, once per frame.
 * ============================================================================================ */

void mp_reentry_note_my_slot(uint8_t slot);

/* A started session, and the rules it is played under. */
void mp_reentry_note_session(uint8_t mode, bool is_client, const mp_rules_t *rules);
void mp_reentry_note_no_session(void);

/* How many far players the rules are told about, one for each far bank. */
#define MP_REENTRY_MAX_PEERS 3u

/* One far player as the interpolator last resolved it. */
typedef struct mp_reentry_peer {
    bool  known;
    bool  alive;
    float position[3];
    float heading;
} mp_reentry_peer_t;

/* Where far player `index` (0 to MP_REENTRY_MAX_PEERS - 1, the far banks in order) is and
 * whether it is standing; no_peer when this machine resolved no pose for it. */
void mp_reentry_note_peer(size_t index, const float position[3], float heading, bool alive);
void mp_reentry_note_no_peer(size_t index);

/* Which far player a co-op re-entry tries first: the one standing nearest to `died_at`, where
 * this player died, or the first one standing when that place is not known. The seat search tries
 * the others after him on the same look, as they stand then. -1 when nobody of the `count` is
 * standing, which in co-op makes this player the last one. A player this machine has no pose for,
 * or one the wire says is down, is nobody to stand beside. */
int mp_reentry_pick_anchor(const mp_reentry_peer_t *peers, size_t count, const float *died_at);

/* ==============================================================================================
 * The two ends of a death.
 * ============================================================================================ */

/* Every death this machine hears about, its own and the far player's. Only its own turns into a
 * wish; the rest are counted so that a report can say a death was heard and not acted on. */
void mp_reentry_note_death(const mp_death_note_t *note);

/* Once per drawn frame, with the host's substep count. It keeps the survival switch true to the
 * moment, and it is where a held wish is carried out, retried or given up. */
void mp_reentry_tick(uint32_t host_substeps);

/* True while this client is down with nobody standing and is waiting for the host's choice. The
 * picture asks, because a player looking at a corpse needs to know that somebody else is picking
 * and that the wait has an end. */
bool mp_reentry_waiting_for_host(void);

void mp_reentry_report(void);

#endif /* MULTIPLAYER_MP_REENTRY_H */
