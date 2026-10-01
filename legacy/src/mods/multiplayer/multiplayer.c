/* multiplayer.c: the configuration, the installation sequence and the watchpost of the DLL.
 *
 * The order it installs in: the configuration and the two tables, then the bootstrap that puts two
 * module nodes and one task slot into the engine's scheduler, then behind it the second player
 * bank, the second body, the input split, the death hull and the bank health, and last the bridge
 * in one of three shapes. Two idle pumps, the frame hook and a thread timer, keep the sessions
 * alive between substeps and through a level load. One function per stage, so that a stage reads
 * in one screen and its refusal ends the install where it happens.
 *
 * SIZE NOTE: over 600 lines. The round that scores a deathmatch was here until this file was
 * four lines short of the hard limit; it is mp_round.c now, and the only thing this file still
 * says about it is which side keeps the truth. The end of a session went the same way for the
 * same reason: what is left here is the three lines that say WHEN to ask. The idle pumps and
 * what rides the drawn frame left as mp_pumps.c when the file reached the limit again.
 *
 * The stages that
 * stand on the configuration alone left as mp_install.c when the one exit pushed it past the
 * limit again, and the hulls the arming puts on the engine left as mp_arm_hulls.c when it stood
 * two lines short with more of them to come. The next seam is the rest of the arming,
 * arm_bridge_common with the menu's arming beside it, which share this module's state.
 */
#include "multiplayer.h"

#include "mp_arena.h"
#include "mp_arm_hulls.h"
#include "mp_armed.h"
#include "mp_arrival.h"
#include "mp_chat_draw.h"
#include "mp_cutscene.h"
#include "mp_session_over.h"
#include "mp_bank.h"
#include "mp_body.h"
#include "mp_body_wear.h"
#include "mp_bootstrap.h"
#include "mp_bridge.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_bridge_savefile.h"
#include "mp_bridge_statement.h"
#include "mp_config.h"
#include "mp_lobby.h"
#include "mp_damage.h"
#include "mp_input.h"
#include "mp_install.h"
#include "mp_lifecycle.h"
#include "mp_capacity.h"
#include "mp_cells.h"
#include "mp_enemy.h"
#include "mp_enemy_relay.h"
#include "mp_flash.h"
#include "mp_follow.h"
#include "mp_follow_difficulty.h"
#include "mp_enemy_sync.h"
#include "mp_death.h"
#include "mp_hud.h"
#include "mp_bridge_public.h"
#include "mp_memory_watch.h"
#include "mp_menu.h"
#include "mp_mod_allow.h"
#include "mp_module.h"
#include "mp_dialog_relay.h"
#include "mp_quest_relay.h"
#include "mp_npc_copies_bridge.h"
#include "mp_pool.h"
#include "mp_pumps.h"
#include "mp_range_gate.h"
#include "mp_reentry.h"
#include "mp_respawn.h"
#include "mp_round.h"
#include "mp_scratch_bind.h"
#include "mp_settings.h"
#include "mp_scratch_wire.h"
#include "mp_signatures.h"
#include "mp_signatures_dialog.h"
#include "mp_spawnpoints.h"
#include "mp_stopwatch.h"
#include "mp_reports.h"
#include "mp_trust.h"
#include "mp_use_latch.h"
#include "mp_watchpost.h"
#include "mp_start.h"
#include "mp_world.h"
#include "mp_world_holds.h"
#include "mp_world_values.h"

#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct multiplayer_module {
    bool                 entered;
    multiplayer_state_t  state;
    multiplayer_config_t config;

    /* Which side the transport was brought up as: the bridge does not answer it, nothing takes a
     * bound socket down, and it is the one thing a second visit to the lobby cannot change. */
    uint32_t             armed_role;   /* mp_settings_role_t, 0 while nothing is armed */

    /* The game the switches were last set for. The one cell both announcers compare against,
     * where the follow used to keep a latch of its own that the menu's apply never saw. */
    uint32_t             announced_mode;
} multiplayer_module_t;

static multiplayer_module_t module_state;

const multiplayer_config_t *multiplayer_configuration(void)
{
    return &module_state.config;
}

multiplayer_state_t multiplayer_state(void)
{
    return module_state.state;
}

/* The two resolvers report their own exceptions, so what is left here is the verdict, and it is
 * not behind a switch: a partly resolved table is the most important thing this DLL can say. */
static void report(size_t sites, size_t cells)
{
    log_info("%u of %u sites and %u of %u cells resolved",
             (unsigned)sites, (unsigned)MP_SITE_COUNT,
             (unsigned)cells, (unsigned)MP_CELL_COUNT);

    if (sites < (size_t)MP_SITE_COUNT || cells < (size_t)MP_CELL_COUNT) {
        log_warning("this executable is not one the patterns fully describe, or another module has "
                    "moved something they name");
    }
}

/* The frame hook wants a call of its own shape, and the switches the watchpost needs are the
 * installer's. */
static void watchpost(void)
{
    static bool probed = false;

    /* The first drawn frame, and only once the way in was reached: both read pools the foothold
     * puts up, and both are off unless the ini turns them on. */
    if (!probed && mp_bootstrap_has_run()) {
        probed = true;
        if (module_state.config.pool_probe != 0) {
            mp_capacity_probe_pool(module_state.config.pool_probe);
        }
        if (module_state.config.provoke_full_pool) {
            mp_pool_provoke_full();
        }
    }
    /* The dispatcher is re-armed only while something can be touched through it: a session's far
     * bodies, or the body a provocation spawns. In single player the slot stays the engine's. */
    mp_watchpost_frame((module_state.config.body_contact || module_state.config.second_body) &&
                       (mp_armed_transport() ||
                        mp_config_body_provocation(&module_state.config)));
}

const char *multiplayer_mode_name(uint32_t mode)
{
    switch (mode) {
    case MULTIPLAYER_MODE_COOP: return "co-op";
    case MULTIPLAYER_MODE_TDM:  return "team deathmatch";
    default:                    return "unset";
    }
}

/* Registered from the foothold and behind no switch of its own: behind the second body's or the
 * contact instrument's switch, an ini that turns both off would print none of these reports. Each
 * is written to be safe on a module that never installed, and a report behind a switch is one
 * nobody reads. */
static void bank_reports(const char *why)
{
    /* Nothing of a session's to report in single player, where a level end used to print a page
     * about a second body, a bridge and a round that did not exist. The list itself is in
     * mp_reports.c; this is the only part of it that needs the configuration. */
    if (!mp_armed_transport() && !mp_config_local_provocation(&module_state.config)) {
        return;
    }
    mp_reports_run(why);
}

/* The one substep-task client, so the two provocations that ride the tick share it. Both run
 * inside the same swap window guarantee; the bank digest first, then the one-shot spawn. */
static void tick_clients(void)
{
    /* Single player runs none of this. The slot stays registered for the life of the process, so
     * the question is asked on every substep rather than once. */
    if (!mp_armed_transport() && !mp_config_local_provocation(&module_state.config)) {
        return;
    }
    /* The AI switch first: it is a level rather than an edge and the engine's enemy pass reads it
     * before anything else, so it has to be standing before that pass runs again. */
    mp_enemy_tick();
    mp_world_values_note_substep();

    /* The bridge's input half first, so the body ticks on the command that just crossed the
     * wire; its snapshot half last, so the wire carries the post-tick world. */
    if (module_state.config.net_bridge || module_state.config.net_role != 0u) {
        mp_bridge_tick_pre();
    }
    if (module_state.config.provoke_bank_swap) {
        mp_bank_provoke_tick();
    }
    if (module_state.config.second_body) {
        /* Which banks have a player behind them, asked before the spawn rather than assumed by
         * it. A session answers it in two parts, and both halves are needed: somebody has to be
         * on the wire, and their state has to have arrived here, because a peer that has joined
         * the session but is still sitting in its own lobby sends nothing and would otherwise be
         * given a body that stands still wearing this machine's own hero. With no session only a
         * provocation that asks for a body gets one; single player itself gets none. */
        if (module_state.config.net_bridge || module_state.config.net_role != 0u) {
            size_t bank;

            for (bank = 1u; bank <= MP_BANK_FAR_MAX; ++bank) {
                mp_body_note_far_player_at(bank, mp_bridge_joined() &&
                                                     mp_bridge_far_occupied(bank));
                mp_body_spawn_at(bank);
            }
        } else if (mp_config_body_provocation(&module_state.config)) {
            mp_body_spawn_tick();
        }
    }
    /* After the one-shot spawn: the tick advances the body the spawn placed, so it has to follow
     * it in the same tick client rather than race it from the frame hook. */
    if (module_state.config.bank_tick) {
        mp_body_tick_second();
    }
    /* After the body's own tick, so a kill lands on a body that has already moved this substep
     * and a revive is picked up by the next substep's tick. */
    if (module_state.config.provoke_second_death) {
        mp_damage_provoke_tick();
    }
    if (module_state.config.net_bridge || module_state.config.net_role != 0u) {
        mp_bridge_tick_post();
    }
}

/* Everything that has to happen on BOTH ways into a bridge, the menu and the ini, in one place: a
 * step written into only one of them is installed and never reached on every run driven by the
 * other, which has happened here once already. The argument decides who describes the campaign and
 * who applies it, which follows from the level belonging to the host. */
/* A fan out rather than one slot the bootstrap hands on: the order is visible here and a reader
 * added later cannot quietly replace one that was already registered. The enemy sync forgets its
 * table on a level change because a PLACEMENT INDEX means a different enemy in a different level,
 * and both are legitimately index 12. */
static void on_module_message(int msg)
{
    /* Registered at the first arming and never taken out, so it asks whether a session is still
     * there to be told about. */
    if (!mp_armed_transport()) {
        return;
    }
    mp_scratch_bind_note_module_message(msg);
    /* The scoreboard, which acts on message 0x15 and ignores every other one. It is called before
     * the switch below rather than inside it because the message it wants is not one of the five
     * the switch knows, and a case that only calls one function is a case that gets forgotten
     * when the next reader tidies the switch. */
    mp_hud_note_module_message(msg);
    mp_chat_draw_note_module_message(msg);   /* the chat box, on the same message, over the board */
    switch (msg) {
    case 0x19:   /* a world is built: the copies' epoch, the same rule as the overlay's */
        mp_npc_copies_bridge_new_world();
        break;
    case 6:
        /* The copies go with their world, and before the enemies are let go below: a copy given
         * up for a level's end would be one more thing asked of an overlay that is closing it. */
        mp_npc_copies_bridge_new_world();
        /* The farewell ends with the level it belonged to. Without this the line stayed on
         * screen for the life of the process: its only other clearing runs when somebody sets up
         * the NEXT multiplayer session in the menu, so a player who ended a session and then
         * started a single player level had "the host has ended the session" painted over every
         * frame of it, with the scoreboard switched off behind it.
         *
         * Message 6 arrives after the half second fade, so the line is still there for the whole
         * of the fade, which is the part a player needs to read it in. */
        mp_session_over_forget();
        /* The far bodies, while the world holding them stands: the level end walks the module
         * list forward, so this head node hears it before any module has closed the level. A body
         * left standing went with the world and was taken down later through a freed object. The
         * next level builds them again from who is on the wire. */
        mp_session_over_take_bodies_down();
        /* falls through: a level ending is also a level change for the enemy table */
    case 3:     /* the module is initialised, which zeroes the whole campaign bank */
    case 0x0B:  /* a savegame is restored */
        mp_enemy_sync_reset();
        break;
    case 7:     /* a level restarts */
        mp_npc_copies_bridge_new_world();
        mp_enemy_sync_reset();
        break;
    case 0x17:   /* a new game begins, which is what the campaign broadcasts at its restart */
        /* The campaign is on its way to the title screen. This is the broadcast the round makes
         * at its restart label, and it is the only notice this side gets that the player has left
         * the level for the menu rather than for the next level.
         *
         * For a host that matters more than it looks: leaving a level does not close the socket,
         * so the host goes on answering keepalives from the title screen and NO timeout on any
         * client will ever fire. Without this line its clients keep playing, alone, in a world
         * whose authority walked away. The announce refuses itself on a client. */
        /* And a client leaves as well, unless it is on its way to its host's next world. Both
         * go by the one exit, which says the host's word, clears the world, and takes the
         * transport down once the word is out: a host back at its title admits nobody any more. */
        if (!mp_bridge_drain_is_client() || !mp_follow_take_new_game()) {
            mp_session_over_exit(MP_EXIT_WHY_LEFT_LEVEL);
        }
        break;
    case 5:
        /* The host's difficulty first, on a client, so everything below reads the value this
         * level is played at. */
        mp_follow_difficulty_note_level_begin();
        /* What this machine holds that decides the world, said before anything below reads it. */
        mp_world_values_note_level_begin();
        /* A level of the session has begun, which the title door needs to know; a level that
         * begins while an exit still waits is one this side began on its own, and the exit ends
         * here, before the arena decides whether to empty this level. */
        mp_session_over_note_level_begin();
        if (!mp_armed_transport()) {
            break;
        }
        /* The one moment an arena can be emptied. The world stands, no actor exists yet and the
         * activation scan has not run once, so a pass here writes the state that scan already
         * reads as "do not wake this". A pass one substep later would be fighting actors that are
         * already standing, which the arena counts apart so a late pass shows in the log. */
        (void)mp_arena_clear_enemies();
        /* And the standing positions, out of the same table, after it has been buried: a placement
         * that is buried is one nobody is standing on. Built in both games because the table is
         * read once and costs nothing; only a deathmatch ever asks for a point out of it. */
        (void)mp_spawnpoints_build();
        /* And the co-op client learns that it has a level to be moved into. It arms here
         * and acts later, because at this moment the host may still be inside its own
         * savegame restore and have sent nothing to be moved to. */
        mp_arrival_note_level_begin();
        /* And the host names a world it has moved on to; a client learns it has arrived. */
        mp_follow_note_level_begin();
        /* After the follow, which raises a host's world generation for the world it moved on to:
         * the copies stand in the world this machine has entered. */
        mp_npc_copies_bridge_level_begin();
        /* Last, so the reads of everything above, which run once at a level's beginning, are the
         * load's and not the level's: the asking side's table of callers starts over here. */
        mp_memory_watch_level_begins();
        break;
    default:
        break;
    }
}

/* A death this machine has to know about, its own or the far player's, told to the two that make
 * something of it. The order is deliberate: the table is what a death is worth, and the re-entry
 * is what happens to the player next; a re-entry that failed must not cost the round its point. */
static void on_death(const mp_death_note_t *note)
{
    mp_round_take_death(note);
    mp_reentry_note_death(note);
}

/* Which game is being played, said once to the two that have to know: the bridge, whose handshake
 * refuses a peer that chose the other one, and the arena, which empties a level for a deathmatch.
 * Setting one and forgetting the other is a deathmatch full of droids or an empty campaign, and
 * neither failure says anything about a game mode when it happens. */
static void announce_game_mode(uint32_t mode)
{
    module_state.announced_mode = mode;
    mp_bridge_set_game_mode((uint8_t)mode, multiplayer_mode_name(mode));
    mp_arena_set_active(mode == (uint32_t)MULTIPLAYER_MODE_TDM);
    /* And what a host takes on a client's word, for the same game (mp_trust.h). */
    mp_trust_set_checking(mode == (uint32_t)MULTIPLAYER_MODE_TDM);
    /* The scene gates follow the arena exactly, and they are set from here rather than from
     * inside the arena so that the arena stays a rule with no engine binding under it. They are
     * the other half of emptying a level: the pass takes away whoever plays a scene, and these
     * take away the scene itself, including the ones the pass had to leave standing because they
     * drive a lift. */
    mp_cutscene_set_suppressed(mode == (uint32_t)MULTIPLAYER_MODE_TDM);
    /* And no borrowed model on a far body, whose sabre would have no contact sphere. */
    mp_body_wear_set_deathmatch(mode == (uint32_t)MULTIPLAYER_MODE_TDM);
}

/* The session was left, by whichever door. With no session the game is the campaign's again:
 * the arena stops emptying levels and the scenes play, where the last announcement of a
 * deathmatch used to go on doing both in every single player level after it. */
static void on_withdrawn(void)
{
    announce_game_mode((uint32_t)MULTIPLAYER_MODE_COOP);
}

/* The exit has taken the transport down. What the arming wrote here goes with it, so the next
 * lobby is armed from nothing, and the AI switch is written back once more, because the substep
 * that keeps it stops running and a client's suspended AI would stay suspended in single player. */
static void on_unarmed(void)
{
    mp_npc_copies_bridge_disarm();   /* before the enemies go quiet, which lets their replicas go */
    module_state.armed_role      = 0u;
    module_state.config.net_role = 0u;
    mp_enemy_set_client(false);
    mp_enemy_tick();
    mp_enemy_sync_set_enabled(false);
    mp_enemy_relay_set_enabled(false);
    mp_range_gate_set_armed(false);
    mp_round_set_authority(false);
}

/* The title screen is on show: a session with nothing left to be ends here, and the sentence a
 * player is owed about an ending that was not theirs goes to the screen that says it. */
static const char *on_title_shown(void)
{
    return mp_session_over_at_title(module_state.armed_role != 0u);
}

/* A start the lobby promised and the title menu never took. The clients are already on their
 * way into the level, and a host left on its title screen would leave them in a world with no
 * host and no word about it. Ending the session sends them back to the menu with the host's
 * own reason. A client whose drive gave up leaves the same way: a session it could not enter is
 * not one to carry into its next single player level. */
static void on_start_given_up(void)
{
    log_warning("the start the lobby promised never happened, so the session ends here%s",
                mp_bridge_drain_is_client() ? "" : " and its clients are sent back to the menu");
    mp_session_over_exit(MP_EXIT_WHY_START_GIVEN_UP);
}

static void arm_bridge_common(bool as_client)
{
    mp_bridge_set_auto_lag(module_state.config.net_auto_lag);
    announce_game_mode(module_state.config.game_mode);
    mp_bridge_set_player_name(module_state.config.player_name);
    mp_bootstrap_set_shutdown_client(&mp_bridge_leave);
    mp_bootstrap_set_substep_end_client(&mp_bridge_substep_end);
    mp_bridge_lobby_set_withdrawn_listener(&on_withdrawn);
    mp_session_over_set_listeners(&bank_reports, &on_unarmed);
    mp_follow_difficulty_set_exit_question(&mp_session_over_exit_waits);

    /* Only the host talks to people. On a client the use key reaches no script, so an actor the
     * host has not reached yet cannot open a second story here; what the host is told travels
     * through the conversation below. The hull asks the lobby at every call, so the answer follows
     * the session and not this arming. */
    if (mp_use_latch_install()) {
        mp_use_latch_set_held_source(&mp_bridge_lobby_client_is_playing);
    }

    /* And the conversation itself, so that the story the host is told is a story every player
     * hears and reads, when that player stands near enough. Beside the use latch because the two
     * are one experience: the press and the words. */
    if (mp_dialog_relay_install()) {
        mp_dialog_relay_set_host(!as_client);
        /* Where the host's own answer is heard on a client: at the host's body there. */
        mp_dialog_relay_set_place_source(&mp_bridge_drain_far_place);
    }

    mp_enemy_sync_reset();
    /* After the reset, and here rather than beside the game mode because it is not a question
     * about the game. Both ways of arming a session reach this line with the transport already
     * up, which is the whole reason the rule is a function of its own. */
    mp_config_enemies_are_one_world(mp_bridge_drain_is_udp());
    mp_npc_copies_bridge_arm(mp_bridge_drain_is_udp(), as_client,
                             module_state.config.npc_copies_max,
                             module_state.config.npc_copy_corpse_seconds);

    /* And the hits with every listener they answer, the NPCs' bolts and the gate between two
     * players. A death that comes of them is handed to the round's table and the re-entry. */
    mp_arm_hulls_hits(as_client, &on_death);

    /* And the client's arm for the host's repeated table, on the same channel as the setup. */
    mp_bridge_drain_set_note_taker(&mp_round_take_note);
    /* And the savegame's recogniser for the reliable channel. Nothing of the transfer rides that
     * channel any more (it has the bulk lane, mp_session_bulk); the arm stays so that a note from
     * a build of the old shape is named and counted rather than read by the next module. */
    mp_bridge_drain_set_peer_note_taker(&mp_bridge_savefile_take_note);
    /* The enemies' hulls and the world's, in the order they go in. A new hull of either belongs
     * in that run rather than here. */
    mp_arm_hulls_world(&module_state.config, as_client);

    /* The campaign bank and the blackboard. The binding resolves five cells and says so when it
     * cannot; without it the wire layer does nothing rather than sending emptiness. */
    /* The module messages are heard whether or not the bank binds: a level's end, a restart and a
     * world built move the copies' epoch, and a level's beginning and the title close a session,
     * none of which is the bank's business. */
    mp_bootstrap_set_module_message_client(&on_module_message);
    if (mp_scratch_bind_install()) {
        mp_scratch_wire_reset();
        mp_scratch_wire_set_host(!as_client);
    } else {
        log_warning("the world scratchpad did not bind, so campaign progress will NOT travel: "
                    "story bits, keys and the AI blackboard stay whatever each machine makes of "
                    "them, and every machine will end the evening in a campaign of its own");
    }

    /* The shared story: the quest items and the keys, which are the campaign rather than either
     * player's belongings. After the bank, because the relay reads and writes it and will not
     * install while it is unbound. */
    if (mp_quest_relay_install()) {
        mp_quest_relay_set_host(!as_client);
    }
    mp_pumps_arm();
}

/* What the screens decided, applied after the common arming so that it wins over the ini: a host
 * names its game, its password, how many it takes and whether it says so on the LAN; a client
 * offers its password and names the game it heard the host play, or nothing for a typed address,
 * which leaves the host's game to the host. It has two callers, the first visit to the lobby and
 * every visit after it, which is why it is a function and not a block. */
static void apply_menu_settings(const mp_settings_t *settings)
{
    /* A new session is being set up, so the last one's farewell stops being the truth. Without
     * this the line that says why the previous session ended would still be on the screen over
     * the first frame of the next one, and the tick that draws it would refuse to look for a new
     * ending because it believes one is already under way. */
    mp_session_over_forget();
    mp_bridge_set_player_name(settings->name);   /* the menu's, newer than the ini's */
    if (settings->role == MP_SETTINGS_ROLE_HOST) {
        announce_game_mode((uint32_t)settings->mode);
        mp_bridge_set_password(settings->password);
        mp_bridge_set_slots(settings->slots);
        mp_bridge_set_announce(settings->announce, settings->session_name);
        mp_bridge_set_list_public(settings->list_public);
    } else {
        /* Not `join_mode` on its own: it is zero whenever the address was typed rather than
         * picked out of the browser, and zero announced itself as "unset". The four switches
         * below this all test for a deathmatch, so an unset mode behaved like co-op by accident
         * rather than by decision, and the run report named no game at all. */
        /* And the host's note wins once there is one, because this apply runs a second time
         * after the start: a typed address would otherwise announce co-op over the
         * deathmatch the note already named, for the whole level. */
        {
            /* A game no menu of this build offers is not announced: the lobby ends the session
             * over one, and announcing it first would empty the level on the way out. */
            mp_lobby_setup_t setup;
            uint32_t         heard = mp_bridge_lobby_setup(&setup)
                                         ? (uint32_t)setup.mode
                                         : (uint32_t)mp_settings_effective_mode(settings);

            announce_game_mode(mp_settings_mode_offered((int32_t)heard)
                                   ? heard : (uint32_t)MULTIPLAYER_MODE_COOP);
        }
        /* The request names THIS side's own game rather than what an announcement said. Sending
         * back what was heard was the way into a deathmatch no menu here can choose: a host
         * announcing one was simply agreed with. */
        mp_bridge_set_client_handshake_mode((uint8_t)settings->mode);
        mp_bridge_set_password(settings->join_password);
    }
}

/* Puts the transport up for what the screens chose, and answers whether it stands. It runs when
 * the LOBBY opens rather than when the menu closes, because a lobby's whole job is to show who has
 * joined and nobody joins a session that was never bound; a second call finds the bridge installed
 * and says so. A role named by the ini or the environment wins over the menu's, because that path
 * installs the transport at startup and two installers of one transport in one process is not a
 * thing to find out about in the field.
 *
 * A CLIENT chosen here cannot rename its window class any more: that rename has to happen before
 * the engine's own single instance guard runs. It only matters for two instances on ONE machine,
 * and the warning below says which case the player is in. */
static bool menu_arm(const mp_settings_t *settings)
{
    mp_settings_armed_t armed;

    /* An exit still saying its last word is finished first: the lobby being opened is a new
     * session, and nobody of the old one is carried into it. */
    mp_session_over_finish_now();

    /* Whether a session already stands is asked first, and which of the two ways it got there is
     * told apart by `armed_role`: the ini and the environment install their bridge at startup and
     * never write it, the menu writes it on its way in. The order used to be the other way round
     * and the question was asked of `net_role`, which was right only for as long as the menu never
     * wrote that field. It writes it now, and asking that way would have made a second visit to
     * the menu report itself as an ini decision and refuse. */
    if (mp_bridge_installed()) {
        if (module_state.armed_role == 0u) {
            log_warning("the menu's choice is not applied: this run is already a %s, decided "
                        "before the menu by NetRole in the ini or OBI_NET_ROLE in the "
                        "environment, and that one wins. In PowerShell an OBI_NET_ROLE set once "
                        "lasts for the whole window",
                        module_state.config.net_bridge ? "loopback bridge"
                            : (module_state.config.net_role == 1u ? "HOST" : "CLIENT"));
            return false;
        }
        mp_bridge_armed(module_state.armed_role, &armed);
        if (!mp_settings_needs_new_transport(&armed, settings)) {
            apply_menu_settings(settings);
            return true;
        }
        /* The other side, or a host on another port. Both used to be refused until the game
         * was restarted, because nothing took a bound socket and its sessions down; the side
         * was compared and the port not even that, so a second host on a new port went on
         * listening on the old one without a word. The transport is taken down and put up
         * again below, which is the restart without the restart. Not inside a level, whose
         * world the old session is still describing to somebody. */
        if (mp_start_level_running()) {
            log_warning("this run is a %s inside a level and cannot become a %s on port %u "
                        "here: leave the level first",
                        module_state.armed_role == (uint32_t)MP_SETTINGS_ROLE_HOST ? "host"
                                                                                  : "client",
                        settings->role == MP_SETTINGS_ROLE_HOST ? "host" : "client",
                        (unsigned)settings->port);
            return false;
        }
        log_info("this run was a %s and becomes a %s on port %u: the transport is put up again",
                 module_state.armed_role == (uint32_t)MP_SETTINGS_ROLE_HOST ? "host" : "client",
                 settings->role == MP_SETTINGS_ROLE_HOST ? "host" : "client",
                 (unsigned)settings->port);
        mp_session_over_exit(MP_EXIT_WHY_NEW_TRANSPORT);
        mp_session_over_finish_now();
    }
    /* The one door into hosting from the menu, LAN and public alike, before any socket, announce
     * or relay: a machine with a DLL out of the mods folder that is not of this release and that
     * [multiplayer] AllowMods does not name cannot host, and the refusal keeps that DLL's name for
     * the lobby to show (mp_mod_allow_hosting_blocked). */
    if (settings->role == MP_SETTINGS_ROLE_HOST && !mp_mod_allow_may_host()) {
        return false;
    }
    mp_stopwatch_setup_begin();
    if (!mp_bridge_install_for(settings)) {
        return false;
    }
    if (mp_bridge_installed()) {
        /* The role is written into the configuration, and it is the line whose absence made every
         * session started from the menu mute.
         *
         * Eleven places read `net_role` to mean "a real network session is running, and this is
         * which side": the two that call the bridge's task halves, the one that turns the enemy
         * synchronisation on, and the report's own shape among them. Only the ini and the
         * environment ever wrote it, so a session armed from the menu had it at zero and every one
         * of those eleven read "no session".
         *
         * What that cost is the whole feature. The bridge's SENDING half hangs off the task half
         * having run in the same substep, the task half is one of the eleven, and so no snapshot
         * of this machine's own body ever left it: a field run showed 2149 substep ends of which
         * 2149 found no task half, and both sides reported nought states and nought snapshots
         * sent. The two players connected, agreed on a level, loaded it, and then stood in it
         * unable to see each other.
         *
         * It is written before the arming below, because the arming reads it. */
        module_state.config.net_role = (uint32_t)settings->role;
        /* And the rule that hangs off there being a session at all, applied here rather than
         * read out of the field above by somebody else: the far bodies are puppets now, so the
         * pipeline tick that would simulate them here goes off. The ini path applies the same
         * rule at startup, which is the only moment it can. */
        (void)mp_config_far_bodies_are_puppets(&module_state.config);
        (void)mp_world_install(module_state.config.net_movers, module_state.config.net_mover_check);
        arm_bridge_common(settings->role == MP_SETTINGS_ROLE_JOIN);
        mp_enemy_set_client(settings->role == MP_SETTINGS_ROLE_JOIN);
        module_state.armed_role = (uint32_t)settings->role;
        /* Which side keeps the score. A loopback bridge is neither and is never told, which is
         * why this is set at the two places that know rather than read out of a role field one
         * of the three ways in never writes. */
        mp_round_set_authority(settings->role == MP_SETTINGS_ROLE_HOST);
        apply_menu_settings(settings);
        mp_stopwatch_setup_end();
    }
    return mp_bridge_installed();
}

/* The client the menu calls: the same arming, with its answer thrown away, for the path that has
 * no lobby in it. */
static void menu_applied(const mp_settings_t *settings)
{
    (void)menu_arm(settings);
}

uint32_t multiplayer_armed_role(void)    { return module_state.armed_role; }

static void arm_watchpost(void)
{
    if (!frame_hook_add(&watchpost)) {
        log_warning("the frame hook is unavailable, so nothing will report whether the foothold "
                    "was ever actually reached");
    }
}

/* A client plays the host's game, and until it is told so it plays neither: a client that typed an
 * address joins with the mode unset, and the mode decides whether the level is emptied of its
 * enemies. The answer is in the note the host repeats, so this reads it back and re-announces. */
void multiplayer_follow_the_hosts_game(void)
{
    mp_lobby_setup_t setup;

    if (module_state.armed_role != (uint32_t)MP_SETTINGS_ROLE_JOIN) {
        return;
    }
    if (!mp_bridge_lobby_setup(&setup) || (uint32_t)setup.mode == module_state.announced_mode) {
        return;
    }
    if (!mp_settings_mode_offered((int32_t)setup.mode)) {
        return;   /* the lobby ends the session over it, and this side does not follow it there */
    }
    log_info("the host is playing %s, so this side follows", multiplayer_mode_name(setup.mode));
    announce_game_mode(setup.mode);
}

/* The tables: the host image, the sites, the cells and their report, and the two things that
 * stand on the tables alone: the pool reserve, and the modules that resolve sites of their own.
 * False when nothing can be resolved, which ends the install. */
static bool resolve_tables(void)
{
    size_t sites;
    size_t cells;

    /* The shared layer is a static library, so this DLL carries its own copy of the host image
     * state and another DLL having resolved it does nothing here. Without it the scanner searches
     * an empty range and every pattern reads in the log like an unsupported executable. */
    if (!host_image_resolve()) {
        module_state.state = MULTIPLAYER_NO_HOST_IMAGE;
        log_error("no 32-bit host image, nothing can be resolved");
        return false;
    }

    /* The order is not in any type system: a cell is read out of an operand of a site, so the
     * sites have to answer first. */
    sites = mp_signatures_resolve(module_state.config.log_sites);
    (void)mp_signatures_dialog_resolve();
    cells = mp_cells_resolve(module_state.config.log_sites);
    module_state.state = MULTIPLAYER_TABLES_RESOLVED;

    report(sites, cells);
    mp_pool_configure(module_state.config.pool_reserve);
    (void)mp_enemy_install((mp_enemy_suspend_mode_t)module_state.config.enemy_suspend);
    /* Installed in both games and switched on in one. The gate lets everything through
     * while it is off, so a co-op campaign sees the level its authors wrote. */
    (void)mp_arena_install();
    /* Beside the arena, because the two are one feature: the pass empties the level of
     * whoever plays a scene, and these hold back the scenes the pass had to leave
     * standing. Both are inert until a deathmatch turns the arena on. */
    (void)mp_cutscene_install();
    mp_menu_set_applied_client(&menu_applied);
    mp_menu_set_arm_client(&menu_arm, &multiplayer_armed_role);
    mp_menu_set_session_clients(&mp_session_over_exit, &on_title_shown);
    (void)mp_menu_install();

    /* What takes everybody into a level once the lobby says go. Without it every mp_start_* answers
     * false and the lobby refuses its own START, which is exactly what it did. */
    (void)mp_start_install();
    mp_start_set_given_up_listener(&on_start_given_up);

    /* The way back into a level that is still running. It resolves three world probes of its own,
     * so it belongs with the tables rather than behind a session, and its landing listener has to
     * be the dispatcher's re-arm: the engine's own re-entry takes the contact slot away on the way
     * out and its spawn puts the ENGINE's handler back, so whoever owned that slot has lost it.
     * The listener fires only once the body is really standing again; firing it earlier would
     * write over a null the engine is about to overwrite itself. */
    if (mp_respawn_install()) {
        mp_respawn_set_landed_listener(&mp_body_arm_dispatcher);
    }

    /* The live scoreboard. It resolves nine sites of its own, so it belongs with the tables; it
     * draws nothing until a deathmatch round is running inside a running level. */
    (void)mp_hud_install();

    return true;
}

/* The frame begin has one client slot and two readers. The game data's line first, because it
 * reads what this machine holds before the session's holds write anything; then the holds, before
 * the frame's substep length is chosen. */
static void on_frame_begin(void)
{
    mp_world_values_note_frame_begin();
    mp_world_holds_frame();
}

/* The foothold behind its own switch. False when the switch is off or the bootstrap refused,
 * which ends the install: everything after it stands on the bootstrap. */
static bool arm_foothold(void)
{
    if (!module_state.config.bootstrap) {
        log_info("the tables are all this build does, nothing is hooked and no session exists");
        return false;
    }
    if (!mp_bootstrap_arm(module_state.config.provoke)) {
        module_state.state = MULTIPLAYER_BOOTSTRAP_REFUSED;
        return false;
    }
    module_state.state = MULTIPLAYER_BOOTSTRAP_ARMED;
    mp_bootstrap_set_report_client(&bank_reports);
    mp_bootstrap_set_frame_begin_client(&on_frame_begin);
    arm_watchpost();
    log_info("a foothold is armed: it will install two module nodes and one task slot, and there "
             "is still no session and no gameplay change");
    return true;
}

/* The bridge, last of all: it drives the input split and reads the banks, so everything it binds
 * must already stand. The idle pumps are armed for every shape, the loopback included, because its
 * sessions count the wall clock like the UDP roles' and a long menu pause would drop them too. */
static void install_bridge(void)
{
    if (module_state.config.net_bridge && mp_bridge_install(module_state.config.net_loss) &&
        module_state.config.net_spin) {
        mp_bridge_enable_spin();
        log_info("the net spin is armed: the CLIENT authors a full turn every substep, so a "
                 "circling second body means the command crossed the wire");
    }
    if (module_state.config.net_role == 1u) {
        mp_bridge_install_udp(true, NULL, (uint16_t)module_state.config.net_port);
    } else if (module_state.config.net_role == 2u) {
        mp_bridge_install_udp(false, module_state.config.net_address,
                              (uint16_t)module_state.config.net_port);
    }
    if (module_state.config.net_role != 0u) {
        mp_enemy_set_client(module_state.config.net_role == 2u);
    }
    if (mp_bridge_installed()) {
        /* The map, after the bridge, because with no bridge there is no wire for a door to cross
         * and nothing should be hulled. Its two halves are independent, which is the only way to
         * see what two entirely free running maps do. */
        (void)mp_world_install(module_state.config.net_movers, module_state.config.net_mover_check);
        arm_bridge_common(module_state.config.net_role == 2u);
        mp_round_set_authority(module_state.config.net_role == 1u);
    }
}

void multiplayer_install(void)
{
    if (module_state.entered) {
        return;
    }
    module_state.entered = true;

    /* Before anything that logs. Without it every line this DLL writes is dropped, including the
     * warnings that would name the reason it did nothing. */
    log_init("multiplayer", false);

    mp_config_read(&module_state.config);
    if (!module_state.config.enabled) {
        module_state.state = MULTIPLAYER_DISABLED;
        log_info("disabled");
        return;
    }
    /* Before any way into a session can arm the detonation flash: the arming says the distance
     * in its line, and the ini's way in arms inside install_bridge below, the menu's later. */
    mp_flash_set_near(module_state.config.flash_near);
    if (!resolve_tables() || !arm_foothold()) {
        return;
    }
    mp_install_bank_and_body(&module_state.config, &tick_clients);
    mp_install_input_and_death(&module_state.config);
    /* No statement is made while this DLL installs: when the loader runs from its fallback, other
     * mods may still be loading, and a census taken now would miss them for the whole process. Held
     * around the one call that can ask for it, so that no early return leaves it held. */
    mp_bridge_statement_hold(true);
    install_bridge();
    mp_bridge_statement_hold(false);
}
