/* mp_body.h: the far player bodies, their classes, and the contact path that proves they exist.
 *
 * A player body is one object out of the pool with objClass and shooterClass both 1 in the retail
 * game. The pair filter in bapobj_collidePairs throws out any pair whose classes match, so four
 * bodies all on class 1 can never touch. The co-op answer is to give player 1..3 the classes
 * 5, 6, 7, which appear in no level's actor records, so every player pair passes the filter and
 * every self hit is rejected without a line of engine code.
 *
 * This module has two halves that ship together because they are one decision. The count dispatcher
 * replaces the shared contact slot so that contact deliveries can be seen at all: with the co-op
 * classes the delivered message reaches no arm of the engine's own player_onContact, so "two bodies
 * exchange contact messages" is invisible in the game and needs an instrument. The shot hull makes
 * a bank's shot carry the bank's own class, which the fire sites cannot do on their own, because
 * every player fire site passes a literal 1.
 *
 * The dispatcher is a chained replacement of a data slot, not a code detour: player_spawnHero
 * writes the slot to the engine's own handler on every spawn, so the replacement is re-armed after
 * each of the player's own spawns rather than installed once. A spawn this module drives for a far
 * body puts the slot back to what it held before, an empty one included, and a far body in a
 * session delivers through a node of its own (mp_body_gate.h).
 *
 * ============================== A far body wears a named actor ================================
 *
 * A far body used to be spawned as the hero the LOCAL player is, and the reason given was that
 * this is the one template proven loaded. That reason was about the proof rather than about the
 * hero, and the proof is now a call: an actor name is asked of the resource layer before the
 * spawn can hand it to a bind that would otherwise show a message box and end the process. So the
 * far player's own choice can be worn, which needs two things this file's callers have to know
 * apart: WHICH HERO the far player chose, and WHICH SLOT of the engine's four entry hero table
 * the body rides. They are the same number only while the far player wears a shipped hero's own
 * asset; anything else rides one borrowed slot, and everything the engine keys on a hero index
 * follows the slot.
 *
 * ================================ One body became three ========================================
 *
 * The first form held one far body in bank 1. Four players are three far bodies, one per far bank,
 * each spawned, ticked and restored through the same doors by index; the names that said "second"
 * mean bank 1 and stay until their last caller has learned the index. A far body in a session is
 * a puppet of the far player's own machine, and that is one switch for all of them.
 */
#ifndef MULTIPLAYER_MP_BODY_H
#define MULTIPLAYER_MP_BODY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The co-op collision classes. Player 0 keeps the retail 1; the rest take values that are inside
 * the solid-body band [1,10), are not the ally value 9 or the enemy value 2, and were shown by a
 * census of 2250 shipped actor records to appear in none of them. `mp_bank_class_of` is the rule
 * these four are instances of. */
#define MP_BODY_CLASS_PLAYER0 1
#define MP_BODY_CLASS_PLAYER1 5
#define MP_BODY_CLASS_PLAYER2 6
#define MP_BODY_CLASS_PLAYER3 7

/* Resolves the cells and sites the module needs and installs the shot hull. The count dispatcher
 * is armed separately, after a spawn, because a spawn overwrites the slot it lives in. Returns
 * false with a log line when a required site did not resolve. */
bool mp_body_install(void);

bool mp_body_installed(void);

/* Read the contact dispatch slot, keep its current value as the handler to pass on to, and write
 * our counting procedure in its place. Called after each spawn, because player_spawnHero restores
 * the engine's own handler there. Idempotent against our own procedure. */
void mp_body_arm_dispatcher(void);

/* How many contacts the dispatcher has counted, in total and split by whether the receiver was the
 * body at bank 0's block or another. Until a far body exists every contact is bank 0's, so a
 * nonzero "other" count is the first evidence a far body is being touched. */
uint32_t mp_body_contacts_total(void);
uint32_t mp_body_contacts_other(void);

/* Spawn bank `index`'s body, at player 0's own position so the two cylinders overlap and the push
 * is visible. It takes player 0's position, decides which hero table entry the body rides and
 * whether that entry's name has to be borrowed, proves the borrowed asset is loadable, swaps the
 * bank in through the spawn's window, spawns through the lifecycle hull so the block loan makes
 * the absolute clear land in the bank, gives the name back at once, stamps the bank's co-op
 * class onto the new object, puts the active status index back on the LOCAL hero, and swaps
 * out; the swap out gives back the inventory bytes and the hero records the spawn wrote, which
 * the index alone does not. The local player's contact slot the spawn overwrote goes back to what
 * it held, and in a session the new body takes a contact node of its own.
 *
 * It also carries out a recorded appearance, which is why it is called every substep rather than
 * once: a change takes the standing body down first and builds the new one after. Otherwise a
 * no-op once a body stands, after one failure, or when the pool reserve would be breached.
 *
 * MUST NOT be called with a bank window open; it opens its own, and the bank refuses a second. */
void mp_body_spawn_at(size_t index);

/* Take bank `index`'s body down: off the wire's paths first, then the engine's own despawn inside
 * a PERSISTENT window of that bank, then the module state a rebuilt body must not inherit. True
 * when nothing stands afterwards, which includes the case where nothing stood before. False, with
 * a line, when the window did not open or the site did not resolve; the body is then left
 * standing rather than half taken down.
 *
 * MUST NOT be called with a bank window open. */
bool mp_body_teardown_at(size_t index);

/* What bank `index`'s body is to look like: the hero the far player chose, 0..3, and the actor
 * asset it wears. A NULL or empty asset means that hero's own shipped asset and needs no borrowed
 * name slot.
 *
 * The change is RECORDED and carried out from the substep tick, because the engine will not build
 * a body unless a level is running and because a rebuild may not run inside a bank window. It is
 * idempotent: asking for what the body already wears does nothing, which matters because the far
 * side's appearance is state and is resent. A wish that cannot be carried out within its deadline
 * is dropped with a line rather than applied at some later moment nobody chose it.
 *
 * False for an index that is no bank, a hero outside 0..3, or before the module installed. True
 * means the wish stands or was already satisfied, NOT that the body has been rebuilt yet. */
bool mp_body_set_asset_at(size_t index, int32_t hero, const char *asset);

/* The sizes a far body may be told to take. They are the appearance event's own bounds in the
 * units this layer thinks in, so a number that crossed the wire cannot be refused here. */
#define MP_BODY_SCALE_MIN 0.1f
#define MP_BODY_SCALE_MAX 6.5f

/* How big that body is drawn, 1.0 being the size its own asset asks for.
 *
 * The two player scales of the developer overlay are a factor composed into the LOCAL player's
 * own draw matrix every frame, which is a thing no second machine can see. What travels is the
 * number (mp_events, the appearance), and this is where it lands: a factor the wear tick
 * multiplies by the scale of the asset the body shows and puts on through the engine's own
 * setter, the one a model swap uses to size a borrowed body.
 *
 * A wish, like the appearance beside it: the object is reachable from the spawn tick and not from
 * the wire, and a body that is rebuilt has to be told again. False for an index no bank shows or
 * a factor outside the wire's own bounds. */
bool mp_body_set_scale_at(size_t index, float scale);

/* Which hero table entry bank `index`'s body actually rides, or -1 before it stands. It is the
 * far player's hero when that hero's own asset is worn, and the borrowed slot otherwise, so this
 * and not the hero is what the sabre arm and the engine's hero-keyed modes follow. */
int32_t mp_body_slot_at(size_t index);

/* The actor asset bank `index`'s body was really built from, lower case, or "" when it wears the
 * shipped asset of the slot it rides. It is "" after a refused appearance as well: what the far
 * side asked for is kept so a repetition stays idempotent, but this answers what is worn. */
const char *mp_body_asset_at(size_t index);

/* A far body has been taken down and, if an appearance drove it, is about to be built again out
 * of another actor. Wire this to mp_puppet_reset: the puppet's clip, weapon and sabre state
 * describes the skeleton that has just left, and a clip ordinal carried across means something
 * else against the new actor's table, which shows up as a body running the wrong animation with
 * the right number on the wire. Nothing here knows about a puppet; an unwired listener is said
 * once in the log rather than left to be found in the field. */
typedef void (*mp_body_rebuilt_fn_t)(size_t bank);
void mp_body_set_rebuilt_listener(mp_body_rebuilt_fn_t listener);

bool mp_body_exists_at(size_t index);

/* A listener for the local player's own shots: the kind, muzzle, pitch and yaw of every spawn the
 * hull sees while bank 0 is active. One listener; a later call replaces the earlier one. */
typedef void (*mp_body_shot_listener_t)(int32_t kind, const float muzzle[3], float pitch,
                                        float yaw);
void mp_body_set_shot_listener(mp_body_shot_listener_t listener);

/* A listener for an NPC's shots: every spawn the hull sees at bank 0 with a class above 1,
 * neither the engine's own effects (0) nor a player (1), told once the engine has made the
 * bolt, with the body object it made for it. One listener; a later call replaces it. */
typedef void (*mp_body_npc_shot_listener_t)(int32_t kind, const float muzzle[3], float pitch,
                                            float yaw, int32_t shooter_class,
                                            uint32_t object);
void mp_body_set_npc_shot_listener(mp_body_npc_shot_listener_t listener);

/* A listener for the object each class 1 shot flies as, told once the engine has made it. The
 * local player's shots and a puppet's both pass here; the puppet names its own afterwards. One
 * listener; a later call replaces it. */
typedef void (*mp_body_shot_object_listener_t)(uint32_t object);
void mp_body_set_shot_object_listener(mp_body_shot_object_listener_t listener);

/* The same for the object an ally's class 1 shot flies as: the AI fired it for an actor of the
 * player's side. One listener; a later call replaces it. */
void mp_body_set_ally_shot_listener(mp_body_shot_object_listener_t listener);

/* A listener for every shot the hull sees made, at any bank and of any class, sub shots and the
 * engine's own effects included: the kind asked for, the class it carries and its object. Told
 * before every listener above. One listener; a later call replaces it. */
typedef void (*mp_body_shot_made_listener_t)(int32_t kind, int32_t shooter_class,
                                             uint32_t object);
void mp_body_set_shot_made_listener(mp_body_shot_made_listener_t listener);

/* The NPC bolt relay is calling the engine to fire its copy of a bolt, and a class 1 one is an
 * ally's: it is nobody's to tell again. */
void mp_body_note_npc_replay(bool firing);

/* Tick bank `index`'s body through the thirteen-phase player pipeline for one substep, so it
 * moves, collides and commits its own position rather than standing at its spawn point. The walk
 * is this feature's own loop over the pristine phase table, with the death check, the input phase
 * and the ground publish withheld from the body. A no-op until the body exists, on a build where
 * the table could not be read back from disk, and for good once the body has died inside a tick.
 * One effect is known and accepted for now: a contact struck during the tick is charged to the
 * ticking bank. Because it runs the bank's block at the absolute hero block address, it must not
 * run while the swap provocation is also enabled, which would overwrite that block; the two are
 * mutually exclusive by configuration. */
void mp_body_tick_at(size_t index);

/* The damage module has revived bank `index`'s body. Clears the once-only "no longer ticked"
 * report latch, so a later real death is reported again rather than swallowed by the first one's
 * line. */
void mp_body_note_revived_at(size_t index);

/* Deliver a contact whose receiver is not bank 0's body inside a persistent bank window of the
 * bank whose body was touched, so the engine's own handler hurts, shoves and kills that body
 * instead of the player. The caller vouches that the death hull stands, because a crush or burn
 * delivered in that window enters the death path with the bank active. */
void mp_body_enable_contact_routing(void);

/* The far bodies are puppets of far players. What touches one is decided on that player's own
 * machine, so a contact whose receiver is a puppet is counted and answered without the engine's
 * handler: no bank window, no health loss here, no HUD flash, no pain sound, no pickup into the
 * shared inventory, and no lasting immunity from a drop timer nobody ticks. The shot itself still
 * ends through its own task. The routed delivery above stays for a far body that is not a puppet.
 * One switch for every far body: a session has no far body that is not a puppet. */
void mp_body_set_second_is_puppet(bool is_puppet);

/* A contact whose receiver was a far body was DROPPED here, and the bank says whose it was.
 * The listener is what turns that drop into a message: nothing in this file knows about a
 * wire, and nothing on the wire needs to know about the dispatcher. */
typedef void (*mp_body_puppet_hit_fn_t)(size_t bank);
void mp_body_set_puppet_hit_listener(mp_body_puppet_hit_fn_t listener);

/* Which bank's body this object is, or 0 for the local player's own and for anything else.
 * The mirror of a hit needs it to know WHICH far player was touched. */
size_t mp_body_bank_of_object(uint32_t object);

/* ============================= Whether this pair may hurt at all =============================
 *
 * A contact from this machine's player against a far player's body is the one contact a rule set
 * has an opinion about: two players in a co-operative campaign are one side, and two on the same
 * team in a deathmatch are one side as well. Everything else, an enemy's shot, a mover, the level
 * itself, is not a player and is never judged here.
 *
 * The gate is a listener rather than a call for the reason the two above are: what decides it is
 * the game, the rule set and two team bytes, and all three of those live with the session. The
 * dispatcher only knows which bank was touched and which world slot that bank stands for.
 *
 * With no gate set the dispatcher behaves as it did before there was one, and every refused
 * contact and every unjudged one is counted, so a build in which this was never wired says so
 * instead of quietly letting players kill their own side. */
typedef bool (*mp_body_damage_gate_fn_t)(uint8_t peer_slot);
void mp_body_set_damage_gate(mp_body_damage_gate_fn_t gate);

/* ============================ Who died here, and who did it ==================================
 *
 * The dispatcher is the one place on this machine that can answer both halves of that question at
 * once. It sees the contact that killed the local player, because the engine's own handler runs
 * inside it and the player record's dead flag is 0 in front of that call and 1 behind it; and it
 * can name the attacker, because a body's collision class says which bank it came out of and the
 * shot hull stamps the same class onto every projectile a bank fires.
 *
 * Which WORLD SLOT a bank stands for is not this module's to know. It is the session's answer, so
 * it is set from there: index 0 is this machine's own player and 1..MP_BANK_FAR_MAX are the far
 * ones. A death with no slot set for this machine cannot be reported at all, and one whose
 * attacker's bank has no slot is reported with no killer named.
 *
 * The report goes out through a listener rather than a call, for the same reason the puppet hit
 * listener is one: nothing in this file knows about a wire, and nothing on the wire needs to know
 * about a contact dispatcher. The signature is the death message's own, so the wiring is one
 * name. */
void mp_body_set_bank_slot(size_t index, uint8_t world_slot);

/* The world slot bank `index` stands for, as last set; false for a bank nobody has set. A hit the
 * host drops for a far body is addressed with it. */
bool mp_body_bank_slot(size_t index, uint8_t *world_slot);

/* Whether a far player occupies bank `index` right now, told by whoever knows about the session.
 * A bank nobody occupies gets no body, and a body whose bank empties is taken down on the next
 * spawn tick.
 *
 * Without this a body was built on the first substep of every level and stood there whatever the
 * wire said. A host waiting alone in its own level therefore stood beside a silent copy of
 * itself, wearing its own hero, because no appearance had ever arrived for a player who was not
 * there. Being connected is not enough either: a peer that has joined the session but is still in
 * its lobby sends no state, so the body would stand still until it arrived.
 *
 * Every bank counts as occupied until this says otherwise, which leaves the local provocation
 * that spawns a second body with no session under it exactly as it was. */
void mp_body_note_far_player_at(size_t index, bool present);

typedef void (*mp_body_death_fn_t)(uint8_t victim_slot, uint8_t killer_slot, uint8_t reason);
void mp_body_set_death_listener(mp_body_death_fn_t listener);

/* Whether anything is listening for this player's death at all. A death that is seen here and
 * handed nowhere is a death nothing acts on, and whoever lets a death leave the level running has
 * to ask: granting that with no listener leaves a corpse standing for the rest of the level. */
bool mp_body_death_is_reported(void);

/* The engine itself says the player died, told by whoever hulls the death entry.
 *
 * The dispatcher reads the dead flag on both sides of ONE CONTACT DELIVERY, which is the only
 * moment at which the death and the attacker behind it are in front of one reader. That makes the
 * attribution honest and the DETECTION narrow: a fall, a drowning, a burn, a crush and a scripted
 * kill enter the death without any contact being delivered, and none of them was ever seen here.
 *
 * What that costs is the whole way back. The switch that lets a death leave the level running
 * hangs off the death ENTRY, and the wish that brings the player back hangs off this report. Two
 * mechanisms for one event, and where they disagreed the corpse was made inert and nothing ever
 * asked for it back: the player lay there for the rest of the level, and the engine refuses a
 * corpse its pause menu (`0x0043F678` tests `player_isDead` before `sys_pause`), so the game had
 * no way out but the task manager.
 *
 * A death that IS a contact is left to the delivery, which names the killer. This one names none,
 * because outside a delivery the message globals hold whatever happened last and a killer read
 * out of them would be a guess with a score behind it. */
void mp_body_death_note_engine_death(void);

/* Deaths of this machine's player the dispatcher saw, and how many of them were handed on. They
 * differ when no world slot was ever set or no listener was ever wired, and both of those are the
 * kind of fault that is otherwise invisible until somebody counts the score and finds it empty. */
uint32_t mp_body_deaths_seen(void);
uint32_t mp_body_deaths_reported(void);

/* A hit the host judged against this machine's player, delivered the way the engine delivers any
 * contact: through task_run with the player's node, so the health, the HUD, the pain and the death
 * are the engine's own and an empty contact slot, a corpse's or a body's on its way back, refuses
 * it as the engine refuses it. True when it was delivered, false when the slot refused it or there
 * was nothing to deliver to. Lives in mp_body_gate.c. */
bool mp_body_run_engine_contact(void);
uint32_t mp_body_contacts_suppressed(void);

/* Whether the far player of bank `index` stands, in his own machine's words, read off the pose
 * the interpolator resolved for that bank: the one reading the re-entry's anchors, the copies'
 * owners and the world's anchor are fed from. A far player who does not stand is nobody an NPC
 * fights and nobody a contact on the host is reported for. */
bool mp_body_far_player_stands(size_t index);

/* Which hero bank `index`'s body was built for, or -1 before it stands. This is the FAR player's
 * choice once one has arrived through mp_body_set_asset_at, and the local player's hero before
 * that, which is what the body used to be built from unconditionally. What the engine sees is
 * mp_body_slot_at, and the two differ whenever a foreign asset is worn. */
int32_t mp_body_hero_at(size_t index);

/* Put bank `index`'s body's collision back after a death clip took it away. The death clip's
 * authored event makes the draw pass zero the object's class and both cylinder words, after which
 * the pair pass never sees the body again; the far player's respawn gives him a new body on his
 * machine and the puppet has none, so this writes the co-op classes and the cylinder saved at the
 * spawn back onto the same object. Checked writes, counted, logged once; false when nothing was
 * saved or a write refused. */
bool mp_body_collision_restore_at(size_t index);

/* ================================ The names that mean bank 1 ===================================
 * Every caller that still says "second" means the first far body. These call index 1. */
void    mp_body_spawn_tick(void);
bool    mp_body_set_second_asset(int32_t hero, const char *asset);
bool    mp_body_second_exists(void);
void    mp_body_tick_second(void);
void    mp_body_note_second_revived(void);

void mp_body_report(const char *why);

#endif /* MULTIPLAYER_MP_BODY_H */
