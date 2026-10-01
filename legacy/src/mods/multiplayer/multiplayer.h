/* multiplayer.h: the feature's own entry point and what it decided to do.
 *
 * The DLL resolves its site and cell tables, reports what it found, and behind the master switch
 * installs a foothold in the engine, two module nodes and one task slot, on which everything else
 * stands: the second player bank and body, the input split, the death hull, and last the bridge
 * in one of three shapes, a loopback inside one process or a UDP host or client. The master
 * switch is off in the code and the switches the feature stands on are on, and with the master
 * switch off nothing at all is scanned.
 */
#ifndef MULTIPLAYER_MULTIPLAYER_H
#define MULTIPLAYER_MULTIPLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MULTIPLAYER_SECTION "multiplayer"

/* What the feature settled on at install time. Read only for everything but the installer. */
typedef struct multiplayer_config {
    bool enabled;        /* the master switch, and its default is off */
    bool log_sites;      /* list every resolved site and cell, one line each */
    bool bootstrap;      /* install the foothold: two module nodes and one task slot */
    bool     provoke;    /* install a third node on purpose, to watch the census guard fire */
    uint32_t pool_probe; /* take this many object slots once, and give them all back */
    uint32_t pool_reserve;      /* slots the latch keeps free for the engine */
    bool     provoke_full_pool; /* fill the pool once on purpose, to watch the latch refuse */
    bool     provoke_bank_swap; /* run a full bank swap cycle every substep and require no trace */
    bool     body_contact;      /* replace the contact slot with a counter and install the
                                 * shot hull */
    bool     second_body;       /* spawn a second player body once, at player 0's position */
    bool     bank_tick;         /* tick the second body through the player pipeline every substep */
    uint32_t flash_near;        /* a detonation inside this many units flashes whatever the
                                 * player faces; beyond it, only inside his view. There is
                                 * nothing in the engine to compute it from: kind 10 has no
                                 * radius in the shot table and gets no area ring. */
    bool     bank_input;        /* answer the four input readers per bank, from an injected
                                 * command */
    bool     synthetic_spin;    /* feed bank 1 a constant turn, so the second body circles alone */
    bool     bank_death;        /* route a bank body's death onto the no-latch path */
    bool     provoke_second_death; /* kill and revive the second body once, on a timer */
    bool     bank_health;       /* deliver the second body's contacts into its bank, health
                                 * included */
    bool     net_bridge;        /* host+client sessions in this process, joined over the loopback */
    bool     net_spin;          /* the CLIENT authors the spin, so the command crosses the wire */
    uint32_t net_loss;          /* the loopback's loss dial, percent */
    bool     net_auto_lag;      /* the far body's buffer is measured rather than fixed */
    bool     net_movers;        /* the local player's door, lift and platform triggers travel */
    bool     net_mover_check;   /* both maps describe their moving movers once a second and the
                                 * difference is counted; a client steers its free runners
                                 * back into phase from the host's description */
    uint32_t net_role;          /* 0 off, 1 UDP host, 2 UDP client; OBI_NET_ROLE overrides
                                 * the ini */
    uint32_t net_port;          /* the host's listening port */
    char     net_address[64];   /* client only: the host as a.b.c.d:port */
    uint32_t game_mode;         /* the game this side plays until a menu names one; see below */
    uint32_t enemy_suspend;     /* 0 the AI runs everywhere, 1 it is stopped on this instance,
                                 * 2 it is stopped on whichever instance is the client. Both
                                 * instances share one ini, which is why 2 exists */
    uint32_t npc_copies_max;    /* the NPC copies a host allows at once, 1 to 128 */
    uint32_t npc_copy_corpse_seconds;   /* a copy's corpse lies this long on a host, 0 for the
                                         * engine's own twenty minutes */
    char     player_name[16];   /* what the other players see, cleaned by the roster's rule */
} multiplayer_config_t;

/* THE GAME MODE, and it is not a label on a menu entry.
 *
 * Co-op and deathmatch are two different agreements about who owns what, and the same wire carries
 * both only because the pieces that differ are named rather than assumed. What is the same in both
 * is the level: the host owns it, its scripts run there, and its doors and hazards mirror. What
 * differs is everything that follows from there being one campaign or several scores.
 *
 * A player's INVENTORY is per player in both, which is not a compromise between the two but the
 * same rule read twice: the story bank's bytes 6 to 11 belong to a hero, a hero belongs to a
 * player and a player belongs to the machine they sit at, so those bytes never leave it. Deathmatch
 * needs that because two players must not share a medkit; co-op needs it because two players must
 * not share a key. The door they open with the key travels as an event either way.
 *
 * The two sides must agree, and the handshake refuses them when they do not, with a reason of its
 * own rather than a content mismatch: "you picked a different game" and "your files differ" send a
 * reader to two different places. */
typedef enum multiplayer_mode {
    MULTIPLAYER_MODE_UNSET = 0,   /* named nothing; the handshake skips the comparison */
    MULTIPLAYER_MODE_COOP  = 1,   /* one campaign, shared progress, no damage between players */
    MULTIPLAYER_MODE_TDM   = 2    /* teams and scores, no campaign, damage between players */
} multiplayer_mode_t;

/* The word for a mode, for the log and for the menu. Never NULL. */
const char *multiplayer_mode_name(uint32_t mode);

/* How far the installation got, and where it stopped. Armed is deliberately not the same as run:
 * arming is cheap and silent, and a foothold that was armed and never reached is the failure this
 * feature is most likely to produce. */
typedef enum multiplayer_state {
    MULTIPLAYER_NOT_ENTERED,
    MULTIPLAYER_DISABLED,          /* the switch is off; nothing was scanned */
    MULTIPLAYER_NO_HOST_IMAGE,     /* not a 32 bit host, so no pattern could mean anything */
    MULTIPLAYER_TABLES_RESOLVED,   /* the tables answered; nothing is hooked */
    MULTIPLAYER_BOOTSTRAP_ARMED,   /* a way into the engine is armed; it may not have run yet */
    MULTIPLAYER_BOOTSTRAP_REFUSED  /* no way in could be armed, so the feature stays outside */
} multiplayer_state_t;

/* Called once by the loader through the DLL's exported entry point. Idempotent. */
void multiplayer_install(void);

const multiplayer_config_t *multiplayer_configuration(void);
multiplayer_state_t         multiplayer_state(void);

/* Which side this process armed from the menu, as an mp_settings_role_t, 0 while a role from
 * the ini or the environment holds. The lobby band reads it to say WHY a second arming was
 * refused: the same cell menu_arm judges, read in one more place, not a second judgement. */
uint32_t multiplayer_armed_role(void);

/* A client plays the host's game: reads the host's repeated note and re-announces the mode
 * when it has changed. Called once per drawn frame by the frame pump. */
void multiplayer_follow_the_hosts_game(void);

#endif /* MULTIPLAYER_MULTIPLAYER_H */
