/* mp_arrival.h: where a client's body stands when the level it has just entered is the host's
 * saved game.
 *
 * ================================ The defect this closes ======================================
 *
 * A co-operative session starts from a savegame the HOST owns. The host is restored onto the
 * position that file carries; the client has no file, so the lobby's level start drops its
 * "this came from a savegame" bit and the client is put on the level's own start point. Two
 * players who agreed to play one saved game then begin it half a level apart, and the one who
 * did not press the button is the one who is in the wrong place. At byte level the host is put
 * on the player position in the save header and the client on the world's own player start, and
 * neither side reports anything, because from each machine's point of view its own placement is
 * the ordinary one.
 *
 * A client that loaded the level fresh was later seen to run the level's opening scene as well,
 * whose player host record adopted the client's body, so the client now receives the host's
 * file over the reliable channel and restores it through the same path the host does. This
 * module still seats him beside the host rather than on the one point the file names.
 *
 * The rule, and it is the whole of the requirement: the client begins WITH THE HOST, because it is
 * the host's saved game.
 *
 * A fresh co-operative level has the opposite defect. Every machine puts its player on the level's
 * one start point, so four players begin as four bodies in one collision cylinder, which the
 * engine then pushes apart along whatever axis is cheapest. So a client of a fresh co-operative
 * level is seated beside the host as well, on the ring of its own slot.
 *
 * ================================== Nothing new travels =======================================
 *
 * The host's pose is already in every machine's interpolator before that machine has a level at
 * all, because the payload drain runs from the idle pump as well as from a substep. So this
 * module sends nothing, adds no message and costs no packet budget. It reads the pose that is
 * already there, asks for a free point beside it, and hands that point to the placement.
 *
 * ==================================== Beside, never on ========================================
 *
 * An offset onto the host exactly is a contact: two bodies in one collision cylinder, which the
 * engine resolves by pushing one of them out along whichever axis is cheapest, somewhere neither
 * player chose and possibly through a wall. The client is therefore seated NEXT TO the host, on
 * a point the seat search probed and found walkable, clear overhead and reachable from where the
 * host stands. The search's ring is two world units out; the standing height is 2.8 units, byte
 * proven in the head clearance probe, so a body is well under a unit wide and two units puts two
 * of them clear of each other with room to spare.
 *
 * When the search finds nothing, this waits and tries again on the next frame. It never falls
 * back on the host's own point. Four situations produce an empty search that ends by itself: the
 * host is falling, standing on a lift, swimming, or over a drop. One does not: a host standing
 * where no candidate is free. So the search is the one every seat goes through, with its clock:
 * three seconds without a seat on a good anchor take the authored point nearest the host, or the
 * level start, and three more take that point as it is. A client's ring starts in its own slot's
 * direction, and a candidate a far player stands on is taken; that alone did not keep two clients
 * arriving together apart, because a seat about to be handed out is not a body yet. So every
 * client first works out the seats of the lower client slots in the roster, the same way on every
 * machine, and holds those free as well.
 *
 * ================================ Only the host of this world ==================================
 *
 * The host's pose resolves out of one history that runs across every level of a session, so as a
 * level begins here the newest pose in it is often the host's last one in the level before, and
 * one resolved between that and his first in this level is a blend of the two. Every body says
 * which world it was sent from, and a pose of another is no place here; and of this world's poses
 * the arrival takes only one sampled after its own level began, which it marks on the history as
 * the level begins. So even the first level of a session waits the render lag for a new sample.
 *
 * =================================== Why it is two steps ======================================
 *
 * Armed when a level begins, carried out when the host's first pose has resolved, disarmed
 * either way. The two are separate because of the order the two machines do things in: when the
 * client's level begins the host can still be inside its own savegame restore and has sent
 * nothing yet, so a single step at the level begin would find no pose and give up on a session
 * that was about to work. The client reaching its level begin first is not the rare case: on a
 * fast disk against a slow one it usually does.
 *
 * The offset is carried out at most once per level begin. A second one, after the player has
 * begun to move, is the rubber band this exists to avoid rather than to cause.
 *
 * What this module carries out is handing the point over. The body is moved by the placement,
 * which waits for the two engine gates that are shut for the whole of a level load, and which
 * says in its own line whether the point was taken. So the counters here answer "was an offset
 * wanted, and did one get as far as the placement", and the placement's answer "was a body
 * seated" is the line beside it.
 *
 * ============================ What the offset does NOT put right ==============================
 *
 * It makes the PLACE right, not the world. What the two machines bring into line is the campaign
 * bank, the blackboard, the enemies, and the doors and the switches. What they do not: movers of
 * types 3, 4 and 5, the pushable blocks, the items somebody has already picked up, and the
 * inventory. So a client can be standing beside the host and still be standing in front of a
 * door that is shut for him, or on the wrong side of a lift that is at the wrong floor. That is
 * a defect of its own; the offset does not create it, it only makes it visible sooner, because
 * before this the client was somewhere the host had already walked through.
 */
#ifndef MULTIPLAYER_MP_ARRIVAL_H
#define MULTIPLAYER_MP_ARRIVAL_H

#include "mp_seat_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How long an armed offset waits for the host to turn up in a level of his own, in substeps of
 * THIS machine's simulation: thirty seconds at thirty two a second. It is generous because the two
 * machines are not doing the same work: this one is already playing while the other may still be
 * reading a savegame off a disk, and a substep that does not run is not counted, so a slow load on
 * this side costs nothing here. Substeps and not drawn frames, because a frame is anything from
 * four to forty milliseconds and the same wait would otherwise be seven and a half seconds on a
 * machine that draws 240 a second. Thirty seconds is well past any restore the host has been seen
 * to take, and deliberately longer than the placement's own deadline, which starts only once the
 * point has been handed over. */
#define MP_ARRIVAL_DEADLINE_SUBSTEPS 960u

/* Whether a run wants the offset at all, and whether it can tell yet. */
typedef enum mp_arrival_want {
    MP_ARRIVAL_WANT_NO,        /* the host, or no session: the level's own start point is right */
    MP_ARRIVAL_WANT_UNKNOWN,   /* a client that has not read the host's setup note yet */
    MP_ARRIVAL_WANT_YES
} mp_arrival_want_t;

/* What one tick does with one armed offset. */
typedef enum mp_arrival_step {
    MP_ARRIVAL_STEP_IDLE,        /* nothing is armed */
    MP_ARRIVAL_STEP_STAND_DOWN,  /* this run wants no offset, and that is now settled */
    MP_ARRIVAL_STEP_WAIT,        /* wanted, and the host is not standing in a level yet */
    MP_ARRIVAL_STEP_RUN,         /* the host is there: search for a point and hand it over */
    MP_ARRIVAL_STEP_DROP         /* the deadline passed with the host never turning up */
} mp_arrival_step_t;

/* ==============================================================================================
 * The pure decisions, so a test can pin them with no session and no game in the process.
 * ============================================================================================ */

/* Whether this run wants an offset. A host never does: it is where its own savegame, or the
 * level's own start, put it. A client does when the level came from the host's savegame, and in a
 * co-operative game also when it is a fresh level: the engine puts every player of a fresh level
 * on the one start point, so without the offset all of them stand in one body. A fresh level of a
 * deathmatch is left to the level start, because standing beside the host is no rule there.
 *
 * The unknown answer is not an evasion. A client reads the host's setup note off the reliable
 * channel, and a run that has not read one yet cannot tell the cases apart; treating that as
 * "no" would settle the question on the first frame and never revisit it. */
mp_arrival_want_t mp_arrival_wanted_for(bool is_client, bool setup_known, bool from_save,
                                        bool coop);

/* Whether a resolved far pose is one to stand beside. It has to be the HOST's own world slot,
 * because a session with more players in it than this build shows would otherwise put a client
 * beside another client; it has to not be this machine's own slot, which is what a host reading
 * its own echo would look like; and the far machine has to have called that body living, which is
 * how a host still sitting in a menu or still loading is told apart from one standing in a level.
 *
 * `alive` and `dead` are the far machine's own two words and are not each other's negation: a
 * body that has said neither is a body that does not exist yet. */
bool mp_arrival_anchor_is_ready(bool resolved, uint8_t far_slot, uint8_t my_slot,
                                bool alive, bool dead);

/* Whether the host's pose was sampled after the level began here, which is the one anchor that
 * cannot be the host of the level before: only a pose of this world is ever handed over, and of
 * those only one sampled since this side's level began, the first level of a session included.
 *
 * `marked` says the host's history held a sample as the level began, `mark_tick` the newest and
 * `mark_starts` how often that history had begun for a new player. A history begun again since
 * holds nothing from before, whatever its ticks say; otherwise the sample's own tick has to be the
 * newer, compared as a signed difference so a counter that wraps is read the right way round. A
 * history that held nothing at the mark has nothing from before either. */
bool mp_arrival_pose_is_after(bool marked, uint32_t mark_tick, uint32_t mark_starts,
                              uint32_t state_tick, uint32_t starts);

/* Who a client arrives beside. The host when he stands. When he lies dead in this world, the
 * standing far player nearest his body, so a player who arrives while the host is down stands
 * beside the others rather than waiting out the deadline at the level start. Otherwise nobody
 * yet, and the offset waits as it always has: a host who is not dead and not standing is a host
 * still loading, and the players around him may not have their own seats yet. */
typedef enum mp_arrival_anchor {
    MP_ARRIVAL_ANCHOR_NONE = 0,
    MP_ARRIVAL_ANCHOR_HOST,
    MP_ARRIVAL_ANCHOR_OTHER
} mp_arrival_anchor_t;

/* The rule. `host_ready` is mp_arrival_anchor_is_ready's answer for the host; `host_dead` and
 * `host_at` are the host's pose of this world, meaningful only with `host_dead`. `others` are the
 * far players who are not the host and not this player, `known` for a pose of this world and
 * `stands` in their own machines' words. `chosen` receives the index of the other anchor. The
 * nearest is the seat's own anchor order, the one the re-entry asks as well. */
mp_arrival_anchor_t mp_arrival_pick_anchor(bool host_ready, bool host_dead, const float *host_at,
                                           const mp_seat_body_t *others, size_t count,
                                           size_t *chosen);

/* One tick's answer for one armed offset, `substeps_waited` since it was first looked at. The
 * anchor is asked about before the deadline, because a host who turns up on the very substep the
 * wait runs out is a host to stand beside rather than one to give up on. */
mp_arrival_step_t mp_arrival_step(bool armed, mp_arrival_want_t want, bool anchor_ready,
                                  uint32_t substeps_waited);

/* ==============================================================================================
 * What the feature drives, and what it hands back.
 * ============================================================================================ */

/* From the module message that says a level is beginning. It arms the offset and starts its
 * count; anything an earlier level left armed is replaced rather than added to. */
void mp_arrival_note_level_begin(void);

/* From the frame pump, once per drawn frame, with the substep count the seat's clock runs on. This
 * is where the host's pose is read, the point beside him is searched for, and the offset is carried
 * out, given up or stood down. */
void mp_arrival_tick(uint32_t substeps);

/* Wanted, carried out, dropped, and what the waiting was spent on. Without the last of those the
 * question a player asks, "why am I standing at the level start", has no answer in the log. */
void mp_arrival_report(void);

#endif /* MULTIPLAYER_MP_ARRIVAL_H */
