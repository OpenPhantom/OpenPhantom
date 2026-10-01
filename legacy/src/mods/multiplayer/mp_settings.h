/* mp_settings.h: what the player chose in the menu, as a value with no engine and no file in it.
 *
 * What the menu produces is an INTENTION: host, join, or neither, an address and a port, and a
 * short list of servers worth remembering. The menu's screens act on it, not this file: the way
 * into the lobby puts the transport up, and a client joins from the lobby with this side's
 * statement of its game data, which stands from that arming because the shot module's init has
 * run by the time a menu is shown.
 *
 * Keeping that intention in its own file, with no file I/O and no engine call in it, is what lets
 * the awkward parts be driven in a test: an address typed one character at a time is malformed for
 * most of its life, a list has to forget its oldest entry rather than grow, and the same server
 * typed twice is one server.
 *
 * The address is kept WITHOUT its port, and the port beside it. The wire wants them joined and the
 * menu wants them apart, so one of the two has to do the joining, and doing it here means the
 * screen never has to parse anything.
 *
 * The host's side of the intention lives here as well: the name the session announces
 * itself by, the password it asks for, the mode, how many may sit in it, whether pickups are shared
 * in co-op, and whether it announces itself on the LAN at all. A join carries the password it will
 * offer. None of it is read by the menu screens as text; they show what is here and write back
 * what was typed.
 *
 * The rule set of a round is part of that intention, for the same reason the seat count is: it is
 * what the host will PROPOSE next time. What is played is decided by the note the host repeats to
 * everybody, and a client never reads its own copy of these; this one only survives a restart so
 * that a host does not have to set seven numbers again.
 */
#ifndef MULTIPLAYER_MP_SETTINGS_H
#define MULTIPLAYER_MP_SETTINGS_H

#include "mp_rules.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* "255.255.255.255" is fifteen characters. Twenty four leaves room for a terminator and for the
 * spare byte the engine's own edit widget wants in front of a buffer it may write before. */
#define MP_SETTINGS_ADDRESS_MAX 24u

/* Long enough for "65535" and its terminator. */
#define MP_SETTINGS_PORT_TEXT_MAX 8u

/* Address plus colon plus port plus terminator. */
#define MP_SETTINGS_ENDPOINT_MAX (MP_SETTINGS_ADDRESS_MAX + MP_SETTINGS_PORT_TEXT_MAX)

/* How many servers are remembered. Eight fits a list box on a 640x480 screen with room for the
 * buttons under it, and a player who needs a ninth is a player who wants a browser. */
#define MP_SETTINGS_SERVERS_MAX 8u

/* The player's name: the session's sixteen bytes, NUL inside them. */
#define MP_SETTINGS_NAME_MAX 16u

/* The session's name as the announce carries it, and the password as the handshake carries it:
 * both NUL terminated inside their field, both printable ASCII only (the roster's rule). */
#define MP_SETTINGS_SESSION_NAME_MAX 24u
#define MP_SETTINGS_PASSWORD_MAX     16u

/* How many may sit in a session, the listen host included. Two is the floor because one is not a
 * session. Co-op is four, because a campaign level was authored for one player and four is already
 * generous. A deathmatch is meant to reach sixteen and gets four as well, because four is what one
 * machine can show: its own body and a far body per far bank, and every far body needs a collision
 * class of its own inside the engine's solid band (mp_bank.h). A fifth player would stand in nobody
 * else's world while their shots still landed. The two ceilings stay apart so that the day a
 * deathmatch carries more is one number here. mp_settings_slots_max answers the ceiling for a mode;
 * MP_SETTINGS_SLOTS_MAX is the larger of the two, which is what a buffer or an array has to be
 * sized for and what every host caps its seats at. */
#define MP_SETTINGS_SLOTS_MIN      2u
#define MP_SETTINGS_SLOTS_MAX      4u
#define MP_SETTINGS_SLOTS_COOP_MAX 4u
#define MP_SETTINGS_SLOTS_TDM_MAX  4u
#define MP_SETTINGS_SLOTS_DEFAULT  4u

typedef enum mp_settings_role {
    MP_SETTINGS_ROLE_OFF = 0,
    MP_SETTINGS_ROLE_HOST = 1,
    MP_SETTINGS_ROLE_JOIN = 2
} mp_settings_role_t;

/* The two games the feature is bound to. The numbers are the ones the handshake
 * compares, so they are not free to change: the bridge's MULTIPLAYER_MODE_COOP / _TDM. */
typedef enum mp_settings_mode {
    MP_SETTINGS_MODE_COOP = 1,
    MP_SETTINGS_MODE_TDM  = 2
} mp_settings_mode_t;

/* The two networks a session runs on, chosen in the hub. The LAN is the machines talking to each
 * other directly over UDP, found by the LAN announce or by a typed address, and it never touches
 * the relay. Public is the relay and nothing else: a host is given a code, and a player joins by
 * the code or out of the relay's list. */
typedef enum mp_settings_net {
    MP_SETTINGS_NET_LAN    = 0,
    MP_SETTINGS_NET_PUBLIC = 1
} mp_settings_net_t;

/* A relay code as text, "ABCD-EFGH", and its terminator. */
#define MP_SETTINGS_CODE_MAX 10u

typedef struct mp_settings_server {
    char     address[MP_SETTINGS_ADDRESS_MAX];
    uint16_t port;
} mp_settings_server_t;

typedef struct mp_settings {
    mp_settings_role_t   role;
    char                 address[MP_SETTINGS_ADDRESS_MAX];
    uint16_t             port;
    char                 name[MP_SETTINGS_NAME_MAX];   /* "Player" until somebody types one */

    mp_settings_server_t server[MP_SETTINGS_SERVERS_MAX];
    size_t               servers;      /* how many of the slots above are in use */

    /* The host's side. */
    char               session_name[MP_SETTINGS_SESSION_NAME_MAX];
    char               password[MP_SETTINGS_PASSWORD_MAX];   /* empty means none is asked for */
    mp_settings_mode_t mode;
    uint8_t            slots;
    bool               announce;         /* say so on the LAN, once a second */
    mp_rules_t         rules;            /* what the host will propose for the next round */

    /* The network, and a public host's choice to be shown in the relay's list, off unless asked
     * for. Both are kept in the ini. */
    mp_settings_net_t net;
    bool              list_public;

    /* The join's side: what will be offered when the chosen server asks, and which game the
     * chosen server was heard to be playing, 0 when it was not heard (a typed address), in which
     * case the handshake skips the mode comparison and the host's game is played. Not saved. */
    char    join_password[MP_SETTINGS_PASSWORD_MAX];
    uint8_t join_mode;
    char    join_code[MP_SETTINGS_CODE_MAX];   /* public: the session, as the screen wrote it */
} mp_settings_t;

/* Off, the loopback address and the default port. What a fresh installation means. */
void mp_settings_default(mp_settings_t *settings);

/* Whether a string is a numeric IPv4 address: four decimal parts, each 0 to 255, nothing else.
 * A partially typed address is not one, which is the point: the screen shows the value as the
 * player types it and only the apply has to be refused. */
bool mp_settings_valid_address(const char *text);

/* Ports the engine may bind. Zero means "any" to a socket and "unset" to a player, so it is not
 * one; anything above 65535 does not fit the field it ends up in. */
bool mp_settings_valid_port(uint32_t port);

/* Reads "a.b.c.d" or "a.b.c.d:port". The port is only written when the string carried one, so a
 * caller can seed it with a default first. False leaves both outputs untouched. */
bool mp_settings_parse_endpoint(const char *text, char *address, size_t address_size,
                                uint16_t *port);

/* Joins them back into the "a.b.c.d:port" the transport wants. False when the pair is not one the
 * transport could use, in which case nothing is written. */
bool mp_settings_format_endpoint(const char *address, uint16_t port, char *out, size_t out_size);

/* Puts an address at the head of the remembered list. The same address and port already in the
 * list is moved to the head rather than added again; a full list forgets its oldest. False when
 * the pair is not valid, in which case the list is untouched. */
bool mp_settings_remember(mp_settings_t *settings, const char *address, uint16_t port);

/* Drops one entry, closing the gap behind it. False when the index names nothing. */
bool mp_settings_forget(mp_settings_t *settings, size_t index);

/* The remembered entry as one string, for a list box row. False when the index names nothing. */
bool mp_settings_server_text(const mp_settings_t *settings, size_t index, char *out,
                             size_t out_size);

/* Copies a typed string into a fixed field, keeping only printable ASCII, cutting at the field
 * and trimming the blanks off both ends. An empty result is allowed: an empty password is no
 * password. */
void mp_settings_clean_text(const char *from, char *out, size_t out_size);

/* Reads a port typed on its own: decimal digits and nothing else, 1 to 65535. False leaves `*port`
 * untouched, so a field being typed does not destroy the value it is replacing. */
bool mp_settings_parse_port(const char *text, uint16_t *port);

/* Reads a whole number written out in full: an optional minus, then decimal digits and nothing
 * else. False leaves `*value` untouched.
 *
 * It exists because the profile API's integer read cannot tell a key that is absent from a key
 * whose value is empty: the first answers the default and the second answers zero. Zero is a legal
 * setting for four of the seven rules and means "no limit", so a key somebody blanked out by hand
 * would silently turn a limit off rather than leave it alone. Reading the value as text and
 * parsing it here is what makes "there is nothing there" a case the caller can see. */
bool mp_settings_parse_int(const char *text, int32_t *value);

/* The ceiling for one game: co-op four, and a deathmatch four as well until more far bodies are
 * built. An unknown mode answers the co-op ceiling, which is the safe one. */
uint8_t mp_settings_slots_max(mp_settings_mode_t mode);

/* Clamps a slot count into the range a session can hold, for that game. There is deliberately no
 * mode-blind version: every caller knows which game it is asking about, and the one that did not
 * was how a co-op session came to be settable to sixteen. */
uint8_t mp_settings_clamp_slots_for(int32_t slots, mp_settings_mode_t mode);

/* How many positions the seat slider has for a game: one per seat count it allows. Two seat
 * counts is the floor, so this is never below two and a slider is always a slider. */
uint8_t mp_settings_slot_notches(mp_settings_mode_t mode);

/* Whether a mode number is one of the two. */
bool mp_settings_valid_mode(int32_t mode);

/* Whether this build's menus offer that game at all. The deathmatch is still built: it encodes
 * and decodes on the wire as it always did, every rule it owns keeps its test, and this answers
 * false for it only because nothing on a screen can choose it. That is one question in one place
 * on purpose. A game a menu cannot pick must not arrive by another door either, so the ini reads
 * it as co-op, a client offers its own game in the handshake, and a note naming it ends the lobby
 * it came out of rather than emptying a level nobody asked to have emptied. */
bool mp_settings_mode_offered(int32_t mode);

/* What carries the session the settings ask for. A net that is not PUBLIC, whatever number it
 * holds, is the LAN: no value a file or a stray write leaves in the field puts a LAN session on
 * the relay. */
typedef enum mp_settings_transport {
    MP_SETTINGS_TRANSPORT_NONE = 0,   /* no side chosen */
    MP_SETTINGS_TRANSPORT_LAN,        /* direct UDP; the relay is never used on this path */
    MP_SETTINGS_TRANSPORT_RELAY
} mp_settings_transport_t;

mp_settings_transport_t mp_settings_transport_for(const mp_settings_t *settings);

/* Whether the settings name the public network: what the screens show and what the install puts
 * up both ask this, and nothing reads the field on its own. */
bool mp_settings_is_public(const mp_settings_t *settings);

/* The seats a public host asks the relay for: every seat of the session but its own. */
uint8_t mp_settings_relay_seats(const mp_settings_t *settings);

/* What the transport that stands was put up for. */
typedef struct mp_settings_armed {
    uint32_t          role;         /* the side the menu armed, 0 for none */
    mp_settings_net_t net;
    uint16_t          bound_port;   /* a LAN host's socket, 0 otherwise */
    uint8_t           seats;        /* a public host's seats at the relay, 0 otherwise */
    char              code[MP_SETTINGS_CODE_MAX];   /* a public player's session, "" otherwise */
} mp_settings_armed_t;

/* Whether the transport a run stands on has to be put up again for `settings`: the menu asks for
 * the other side, the other network, a LAN host for another port, a public host for other seats,
 * or a public player for another session. A LAN client chooses its host in the lobby, over the
 * transport it has, so its address never asks for a new one; a public player's transport joined
 * one session, so its code does. */
bool mp_settings_needs_new_transport(const mp_settings_armed_t *armed,
                                     const mp_settings_t *settings);

/* ==============================================================================================
 * What a lobby offers, which follows from the game and from the side and from nothing else.
 *
 * These live beside the intention rather than in the screen because they are decisions, not
 * drawing: a co-op player has no team to choose (every co-op player is on none), a deathmatch has
 * no campaign savegame to carry on from, and the host is a player with a hero of its own only in
 * a deathmatch, because in co-op the level prescribes one. Deciding that here is what lets it be
 * checked without a game in the process.
 * ============================================================================================ */

typedef enum mp_settings_lobby_row {
    MP_SETTINGS_ROW_NONE = 0,
    MP_SETTINGS_ROW_MAP,     /* the host picks the level that will be played */
    MP_SETTINGS_ROW_SAVE,    /* co-op host only: carry on from a savegame instead */
    MP_SETTINGS_ROW_FRIENDLY_FIRE,   /* co-op host only: may the players hurt each other */
    MP_SETTINGS_ROW_HERO,    /* a client always; the host only in a deathmatch */
    MP_SETTINGS_ROW_TEAM,    /* a deathmatch only */
    MP_SETTINGS_ROW_READY,   /* a client says it is ready */
    MP_SETTINGS_ROW_START    /* the host sends everybody in */
} mp_settings_lobby_row_t;

/* Four is the most any side ever shows: the co-op host with map, savegame, friendly fire and
 * start, and the deathmatch host with map, hero, team and start. */
#define MP_SETTINGS_LOBBY_ROWS_MAX 4u

/* Writes the rows this side shows, in the order they stand, and answers how many. Nothing is
 * written past `capacity`, and the answer is then the number that would have fitted. */
/* Whether this side picks its own hero, which is the same question as whether the lobby offers it
 * a hero row. In co-op the level prescribes the host's hero and its savegame carries it; a client
 * and every deathmatch player pick. Read by the lobby's row list and by the level begin that puts
 * the pick on, so the two cannot drift apart. */
bool mp_settings_side_chooses_hero(bool is_host, mp_settings_mode_t mode);

size_t mp_settings_lobby_rows(bool is_host, mp_settings_mode_t mode,
                              mp_settings_lobby_row_t *out, size_t capacity);

/* Which game a lobby is showing. A host shows what it chose. A client shows what the HOST said,
 * because the host decides; `heard` is the mode out of the host's setup note, or 0 while none has
 * arrived, in which case the client falls back to its own last setting rather than to nothing. */
mp_settings_mode_t mp_settings_lobby_mode(bool is_host, mp_settings_mode_t own, uint8_t heard);

/* The same rule over a whole settings record, so that the lobby, the report and the switches that
 * take actors away cannot come to three different answers about which game is being played.
 *
 * The reason it is worth a function of its own is that a joining side has two mode fields and only
 * one of them means anything. `mode` is what this installation last chose while HOSTING and it is
 * kept in the ini across sessions; `join_mode` is what the host announced, and it is filled in only
 * when the server was picked out of the browser. An address typed by hand leaves it at zero, which
 * is neither mode, and until this existed that zero fell through to `mode`. A player who joined a
 * co-operative host was then shown a deathmatch lobby, complete with team rows, because months ago
 * that installation had once hosted a deathmatch. */
mp_settings_mode_t mp_settings_effective_mode(const mp_settings_t *settings);

/* How many of `count` players have not said they are ready. Zero is the host's licence to start:
 * a round that begins over somebody still choosing is the defect this answers. */
size_t mp_settings_lobby_waiting(const uint8_t *ready, size_t count);

#endif /* MULTIPLAYER_MP_SETTINGS_H */
