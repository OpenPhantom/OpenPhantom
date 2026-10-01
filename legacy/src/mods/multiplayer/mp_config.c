#include "mp_config.h"

#include "mp_cadence.h"
#include "mp_crate.h"
#include "mp_enemy.h"
#include "mp_enemy_interest.h"
#include "mp_enemy_relay.h"
#include "mp_enemy_sync.h"
#include "mp_memory_watch.h"
#include "mp_range_gate.h"
#include "mp_stopwatch.h"
#include "mp_pool.h"
#include "mp_roster.h"
#include "mp_text.h"
#include "mp_world_anchor.h"

#include "common/ini.h"
#include "common/logging.h"
#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The longest a host keeps a copy's corpse: the engine's own twenty minutes. */
#define NPC_COPY_CORPSE_SECONDS_MAX 1200

/* Read before anything is scanned, so that a machine with the feature off pays nothing and, more
 * to the point, is touched by nothing. Four groups in the order they are installed, because a
 * switch's gate looks at the switches before it: the foothold and the pools, the bank and the
 * bodies, the in-process bridge, the two-machine role. */
static void read_foothold_configuration(multiplayer_config_t *config)
{
    /* The master switch is off here, so an ini that predates the feature leaves the game alone;
     * an installation that wants the multiplayer turns it on in its ini. The switches the
     * feature stands on, from the foothold to the bank health, are on here, so an ini that names
     * only what a player sets plays the same game as one that names them all. None of them does
     * anything while the master switch is off, because the install stops right after this read.
     * The provocations and the pure measurements are off. */
    config->enabled   = ini_read_bool(MULTIPLAYER_SECTION, "Enabled", false);
    config->log_sites = ini_read_bool(MULTIPLAYER_SECTION, "LogSites", false);
    config->bootstrap = ini_read_bool(MULTIPLAYER_SECTION, "Bootstrap", true);
    config->provoke   = ini_read_bool(MULTIPLAYER_SECTION, "ProvokeDoubleDelivery", false);
    {
        int32_t probe = ini_read_int(MULTIPLAYER_SECTION, "PoolProbe", 0);

        if (probe < 0 || probe > 64) {
            log_warning("PoolProbe %d is outside 0 to 64, using 0", (int)probe);
            probe = 0;
        }
        config->pool_probe = (uint32_t)probe;
    }
    {
        int32_t reserve = ini_read_int(MULTIPLAYER_SECTION, "PoolReserve",
                                       (int32_t)MP_POOL_RESERVE_DEFAULT);

        if (reserve < 0) {
            log_warning("PoolReserve %d is negative, using the default of %u", (int)reserve,
                        (unsigned)MP_POOL_RESERVE_DEFAULT);
            reserve = (int32_t)MP_POOL_RESERVE_DEFAULT;
        }
        config->pool_reserve = (uint32_t)reserve;    /* the upper clamp is the latch's own */
    }
    config->provoke_full_pool = ini_read_bool(MULTIPLAYER_SECTION, "ProvokeFullPool", false);

    {
        /* Named as a word rather than as a number, the two words MenuMode takes, because
         * "Mode=2" tells nobody which game that is. The default is coop, the one game the menu
         * offers, and the menu names its own over it once it arms a session. An empty or an
         * unknown word leaves the mode unset, which makes the handshake stand aside rather than
         * refuse everybody over a typing mistake. */
        char word[16];

        {
            char who[64];

            (void)ini_read_string(MULTIPLAYER_SECTION, "PlayerName", "Player", who, sizeof who);
            mp_roster_name_clean(who, config->player_name);
        }
        {
            char tag[16];

            /* The language of this feature's own texts, chosen before any of them is drawn. */
            (void)ini_read_string(MULTIPLAYER_SECTION, "Language", "", tag, sizeof tag);
            mp_text_choose(tag);
        }
        (void)ini_read_string(MULTIPLAYER_SECTION, "GameMode", "coop", word, sizeof word);
        if (_stricmp(word, "coop") == 0) {
            config->game_mode = MULTIPLAYER_MODE_COOP;
        } else if (_stricmp(word, "tdm") == 0) {
            config->game_mode = MULTIPLAYER_MODE_TDM;
        } else {
            config->game_mode = MULTIPLAYER_MODE_UNSET;
            if (word[0] != '\0') {
                log_warning("GameMode '%s' is neither coop nor tdm, so this side names no mode and "
                            "the handshake will not compare it", word);
            }
        }
    }

    {
        int32_t suspend = ini_read_int(MULTIPLAYER_SECTION, "EnemySuspend", 0);

        if (suspend < 0 || suspend > (int32_t)MP_ENEMY_SUSPEND_WHEN_CLIENT) {
            log_warning("EnemySuspend %d is not 0, 1 or 2, so the AI is left running",
                        (int)suspend);
            suspend = 0;
        }
        config->enemy_suspend = (uint32_t)suspend;
    }
    {
        int32_t cap    = ini_read_int(MULTIPLAYER_SECTION, "NpcCopiesMax", 16);
        int32_t corpse = ini_read_int(MULTIPLAYER_SECTION, "NpcCopyCorpseSeconds", 0);

        if (cap < 1 || cap > (int32_t)NPC_SPAWN_COPIES_MAX) {
            log_warning("NpcCopiesMax %d is not 1 to %u, so a session holds 16 copies", (int)cap,
                        (unsigned)NPC_SPAWN_COPIES_MAX);
            cap = 16;
        }
        if (corpse < 0 || corpse > NPC_COPY_CORPSE_SECONDS_MAX) {
            log_warning("NpcCopyCorpseSeconds %d is not 0 to %d, so a copy's corpse is left to "
                        "the engine", (int)corpse, NPC_COPY_CORPSE_SECONDS_MAX);
            corpse = 0;
        }
        config->npc_copies_max          = (uint32_t)cap;
        config->npc_copy_corpse_seconds = (uint32_t)corpse;
    }
}

static void read_bank_configuration(multiplayer_config_t *config)
{
    config->provoke_bank_swap = ini_read_bool(MULTIPLAYER_SECTION, "ProvokeBankSwap", false);
    config->body_contact      = ini_read_bool(MULTIPLAYER_SECTION, "BodyContact", true);
    config->second_body       = ini_read_bool(MULTIPLAYER_SECTION, "SecondBody", true);
    config->bank_tick         = ini_read_bool(MULTIPLAYER_SECTION, "BankTick", true);
    /* A measurement, not a game setting. Off it costs one comparison per stage and
     * reads no clock at all. */
    mp_stopwatch_set_armed(ini_read_bool(MULTIPLAYER_SECTION, "SubstepStopwatch",
                                         false));
    mp_cadence_set_traced(ini_read_int(MULTIPLAYER_SECTION, "TraceReplicaPlacement", 14));
    mp_memory_watch_arm(config->enabled,
                        ini_read_bool(MULTIPLAYER_SECTION, "SubstepStopwatch", false));

    /* The bank tick installs the second body's block at the absolute hero block for its pipeline
     * call, and the swap provocation overwrites that block each substep, so the two cannot both
     * run. The tick is the one to drop, because the provocation is a diagnostic and the tick is a
     * feature under test; the field is told about it rather than silently reinterpreted. */
    if (config->bank_tick && config->provoke_bank_swap) {
        log_warning("BankTick and ProvokeBankSwap cannot both run, because the swap overwrites the "
                    "block the tick advances; BankTick is disabled for this run");
        config->bank_tick = false;
    }
    if (config->bank_tick && !config->second_body) {
        log_warning("BankTick needs SecondBody, because there is nothing to tick until a second "
                    "body is spawned; BankTick is disabled for this run");
        config->bank_tick = false;
    }

    config->bank_input     = ini_read_bool(MULTIPLAYER_SECTION, "BankInput", true);
    config->synthetic_spin = ini_read_bool(MULTIPLAYER_SECTION, "SyntheticSpin", false);

    /* After the tick's own gates, so a tick those gates dropped drops the split with it. */
    if (config->bank_input && !config->bank_tick) {
        log_warning("BankInput needs BankTick, because nothing reads input for bank 1 until the "
                    "second body is ticked; BankInput is disabled for this run");
        config->bank_input = false;
    }
    if (config->synthetic_spin && !config->bank_input) {
        log_warning("SyntheticSpin needs BankInput, because without the split the command has no "
                    "reader; SyntheticSpin is disabled for this run");
        config->synthetic_spin = false;
    }

    config->bank_death           = ini_read_bool(MULTIPLAYER_SECTION, "BankDeath", true);
    config->provoke_second_death = ini_read_bool(MULTIPLAYER_SECTION, "ProvokeSecondDeath", false);

    if (config->bank_death && !config->bank_tick) {
        log_warning("BankDeath needs BankTick, because only a ticked bank body can reach the "
                    "death path; BankDeath is disabled for this run");
        config->bank_death = false;
    }
    if (config->provoke_second_death && !config->bank_death) {
        log_warning("ProvokeSecondDeath needs BankDeath, because the no-latch death is what it "
                    "provokes; ProvokeSecondDeath is disabled for this run");
        config->provoke_second_death = false;
    }

    config->bank_health = ini_read_bool(MULTIPLAYER_SECTION, "BankHealth", true);
    if (config->bank_health && !config->bank_death) {
        log_warning("BankHealth needs BankDeath, because a crush or burn delivered inside the "
                    "bank window enters the death path; BankHealth is disabled for this run");
        config->bank_health = false;
    }
}

static void read_bridge_configuration(multiplayer_config_t *config)
{
    config->net_bridge   = ini_read_bool(MULTIPLAYER_SECTION, "NetBridge", false);
    config->net_spin     = ini_read_bool(MULTIPLAYER_SECTION, "NetSpin", false);
    config->net_auto_lag = ini_read_bool(MULTIPLAYER_SECTION, "NetAutoLag", true);
    config->net_movers      = ini_read_bool(MULTIPLAYER_SECTION, "NetMovers", true);
    /* On although it is a measurement as well: the digest it sends once a second is the only
     * thing a client steers the free runners back into phase with (mp_world_phase.h), so with
     * it off they drift apart for the rest of the level. */
    config->net_mover_check = ini_read_bool(MULTIPLAYER_SECTION, "NetMoverCheck", true);
    {
        int32_t loss = ini_read_int(MULTIPLAYER_SECTION, "NetLoss", 10);

        if (loss < 0 || loss > 90) {
            log_warning("NetLoss %d is outside 0 to 90, using 10", (int)loss);
            loss = 10;
        }
        config->net_loss = (uint32_t)loss;
    }
    if (config->net_bridge && !config->bank_input) {
        log_warning("NetBridge needs BankInput, because a command from the wire has no reader "
                    "without the split; NetBridge is disabled for this run");
        config->net_bridge = false;
    }
    if (config->net_spin && !config->net_bridge) {
        log_warning("NetSpin needs NetBridge, because without the bridge there is no wire for the "
                    "command to cross; NetSpin is disabled for this run");
        config->net_spin = false;
    }
    if (config->net_spin && config->synthetic_spin) {
        log_warning("NetSpin and SyntheticSpin cannot both feed the command; the wire wins and "
                    "SyntheticSpin is disabled for this run");
        config->synthetic_spin = false;
    }

}

/* The two-instance role. Both instances share one ini, so the role that differs per instance
 * comes from the environment when it is set; the ini value is the fallback for a machine that
 * only ever plays one role. */
static void read_role_configuration(multiplayer_config_t *config)
{
    int32_t     role = ini_read_int(MULTIPLAYER_SECTION, "NetRole", 0);
    int32_t     port = ini_read_int(MULTIPLAYER_SECTION, "NetPort", 27960);
    const char *env  = getenv("OBI_NET_ROLE");
    const char *from = "the ini (NetRole)";

    if (env != NULL) {
        from = "the environment (OBI_NET_ROLE), which beats the ini";
        if (_stricmp(env, "host") == 0) {
            role = 1;
        } else if (_stricmp(env, "client") == 0) {
            role = 2;
        } else if (_stricmp(env, "off") == 0) {
            role = 0;
        } else {
            log_warning("OBI_NET_ROLE '%s' is not host, client or off and is ignored", env);
        }
    }
    if (role < 0 || role > 2) {
        log_warning("NetRole %d is not 0, 1 or 2, using 0", (int)role);
        role = 0;
    }
    if (port < 1 || port > 65535) {
        log_warning("NetPort %d is outside 1 to 65535, using 27960", (int)port);
        port = 27960;
    }
    config->net_role = (uint32_t)role;
    config->net_port = (uint32_t)port;
    /* Said out loud, because it decides whether the MENU is allowed to decide anything, and
     * because it used to be visible only as a side effect three hundred lines further down. In
     * PowerShell `$env:OBI_NET_ROLE` lasts for the whole session, so a role set for one experiment
     * is still there for the next game started from that window, and a lobby that quietly refuses
     * to host is the result. */
    if (config->net_role == 0u) {
        log_info("no network role is set (%s), so the MENU decides: hosting, joining or neither",
                 env != NULL ? "the environment says off" : "the ini says 0 and the environment "
                 "says nothing");
    } else {
        log_warning("the network role is %s, from %s. THE MENU CANNOT CHANGE THIS: a lobby will "
                    "refuse to host or to join anything else. Clear it (OBI_NET_ROLE=off, or "
                    "NetRole=0) for a run the menu drives",
                    config->net_role == 1u ? "HOST" : "CLIENT", from);
    }
    {
        int32_t near_units = ini_read_int(MULTIPLAYER_SECTION, "FlashNearUnits", 20);

        /* Zero turns the near half off and leaves the view test alone; a negative value is
         * a typo rather than an intent. The default sits between the widest blast ring the
         * shipped table has, three and a half units, and the hundred units this feature
         * already treats as out of earshot. */
        config->flash_near = near_units < 0 ? 20u : (uint32_t)near_units;
    }
    ini_read_string(MULTIPLAYER_SECTION, "NetAddress", "127.0.0.1:27960",
                    config->net_address, sizeof config->net_address);

    if (config->net_role != 0u && !config->bank_input) {
        log_warning("NetRole needs BankInput, because both roles stand on the input split; the "
                    "role is disabled for this run");
        config->net_role = 0u;
    }
    if (config->net_role != 0u && config->net_bridge) {
        log_warning("NetRole and NetBridge cannot both drive the wire; the role wins and the "
                    "in-process bridge is disabled for this run");
        config->net_bridge = false;
        config->net_spin   = false;
    }
    if (config->net_role != 0u) {
        (void)mp_config_far_bodies_are_puppets(config);
    }
}

/* What the defect this closes looked like in the field, and it took two runs to see. Every
 * counter said the feature worked: the host landed 817 client states and placed the puppet 725
 * times with no write fault, the client decoded 731 worlds with no refusal, and both players
 * reported that neither could see the other move. The two lines that named the cause were the
 * ones nobody reads: bank 1's body ticked 731 substeps, which is 0 in every run that worked, and
 * the input split answered 1462 digital, 731 relative, 11725 button and 731 hold reads out of an
 * injected command that was empty. The last run in which two machines saw each other move had
 * been started with the role in the environment, so the role was set at startup and the old
 * four line rule beside the ini read fired.
 *
 * The clamps around it stay where they are on purpose. The input split is NOT turned off with
 * the tick: it stays installed and answers nothing, which is what the working run recorded, and
 * its readers are what a body ticked for any other reason would need. The death gate is read
 * after the tick's own gates and before this rule, so it survives the same way it always did. */
bool mp_config_far_bodies_are_puppets(multiplayer_config_t *config)
{
    if (config == NULL || !config->bank_tick) {
        return false;
    }
    config->bank_tick = false;
    log_info("a session over the wire makes every far body a puppet of the state its own machine "
             "sends, so BankTick is off from here on: a ticked body commits its own physics over "
             "the position the wire just placed it at, and the peer then stands still on both "
             "screens. Simulating a remote body from commands returns with prediction");
    return true;
}

/* The move under the game mode was made to answer a real defect and pointed at the wrong mechanism:
 * the co-operative level that had been emptied was emptied by the arena pass, 402 placements in one
 * run, which is gated on the game correctly. What the move did was leave every co-operative session
 * with no enemy replication at all, and nothing flagged it, because no check on one machine sees
 * whether two machines are looking at the same world. */
void mp_config_enemies_are_one_world(bool pass_over_a_socket)
{
    mp_enemy_sync_set_enabled(pass_over_a_socket);
    mp_enemy_relay_set_enabled(pass_over_a_socket);
    mp_range_gate_set_armed(pass_over_a_socket);
    mp_enemy_interest_set_armed(pass_over_a_socket);
    /* One world has one waker: the host, dead or alive, and never a client's own scan. Bound here
     * because this is the session's arming; the binds ask their own questions at every call. */
    if (pass_over_a_socket) {
        (void)mp_world_anchor_install();
        /* And one owner of the push blocks: the host pushes every one, a client asks. */
        (void)mp_crate_install();
    }
    log_info("the enemies are %s, because the transport is %s. The game does not decide this: a "
             "campaign wants one world as much as a deathmatch does",
             pass_over_a_socket ? "ONE WORLD, described by the host and applied by the client"
                                : "each machine's own and unsynchronised",
             pass_over_a_socket ? "a socket"
                                : "the in-process loopback, which cannot replicate to itself");
}

bool mp_config_body_provocation(const multiplayer_config_t *config)
{
    return config != NULL && (config->provoke_second_death || config->synthetic_spin);
}

bool mp_config_local_provocation(const multiplayer_config_t *config)
{
    return mp_config_body_provocation(config) ||
           (config != NULL &&
            (config->provoke_bank_swap ||
             config->enemy_suspend == (uint32_t)MP_ENEMY_SUSPEND_ALWAYS));
}

void mp_config_read(multiplayer_config_t *config)
{
    read_foothold_configuration(config);
    read_bank_configuration(config);
    read_bridge_configuration(config);
    read_role_configuration(config);
}
