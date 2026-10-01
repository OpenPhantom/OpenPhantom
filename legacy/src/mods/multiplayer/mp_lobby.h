/* mp_lobby.h: the two notes a lobby speaks, as bytes with no socket and no engine in them.
 *
 * Layer 1, pure. A lobby is two questions asked in opposite directions, so it is two messages:
 *
 *   What a player says about themselves (0x91, client to host): the team they are on, whether they
 *   are ready, and which hero they will play. The host puts all three into the next roster, so
 *   everybody sees them; the host's own line is set by a call rather than a message to itself.
 *
 *   What the host says about the session (0x92, host to everybody): the game, the level that will
 *   be loaded, what to call it on the screen, and whether the moment has come to load it. It is
 *   REPEATED rather than sent once, so a player who joins late learns the state of the lobby from
 *   the next repeat instead of from a history nobody keeps. The start is a BIT in that repeat and
 *   not a message of its own, for the same reason: a client that missed the start note would sit
 *   in a lobby whose host has left, and a repeated flag cannot be missed.
 *
 * The level travels as a path, not as an index. The eleven shipped levels have indices, but a
 * host may also pick a .b3d of its own out of the game's level folder, and a client that only
 * understood indices could not follow it there. The index rides along for the two things it is
 * still good for: naming the level in a log, and letting a client tell a shipped level from one
 * it may not have.
 *
 * The rules ride in the same note, and they ride here for the reason the note is repeated at all.
 * A rule set belongs to the session and not to the machine, so a player who joins in the middle
 * has to be told what is being played, and the only thing that can tell somebody who missed every
 * earlier message is a message that comes round again.
 *
 * The generation is what makes the note usable more than once. The start bit is an edge that has
 * already fired: once it is set it is never cleared, so a second load of the same level is
 * indistinguishable from the first and the host can send everybody into a level exactly once per
 * session. The generation is a state rather than an edge. The host raises it whenever it changes
 * the world, which covers loading again, loading a savegame and going on to the next level, and a
 * side that sees a generation it has not acted on yet acts on it. It cannot be missed, not even
 * by a client that is away for exactly the gap between two repeats.
 */
#ifndef MULTIPLAYER_MP_LOBBY_H
#define MULTIPLAYER_MP_LOBBY_H

#include "mp_rules.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The tags: the events run 0x81..0x8E, the roster is 0x8F, the player hit 0x90. */
#define MP_LOBBY_TAG       0x91u
#define MP_LOBBY_BYTES     4u    /* tag, team, ready, hero */
#define MP_LOBBY_SETUP_TAG 0x92u

/* The third note, and the one that pays for the lobby existing at all.
 *
 * The handshake refuses a peer whose content fingerprint differs from this build's, but that
 * fingerprint hashes the shot module's damage table and is only true once a LEVEL has loaded, and
 * a lobby is by definition the time before that. So the lobby connects with no fingerprint (which
 * the session reads as "skip the check", the same as a build that names none) and the comparison
 * moves HERE: both sides send theirs the moment they have one, and the client leaves if they
 * differ. The host cannot drop one peer on its own, so the side that must go is the side that
 * goes: the client compares and disconnects itself. */
#define MP_LOBBY_CONTENT_TAG   0x93u
#define MP_LOBBY_CONTENT_BYTES 5u   /* tag, fingerprint */

/* The two games, by the numbers the handshake compares. They are written out here as well as in
 * multiplayer.h because this layer may not include that one and because four places in this file
 * tested them as bare 1 and 2. The numbers are not free to change on either side. */
#define MP_LOBBY_MODE_COOP 1u
#define MP_LOBBY_MODE_TDM  2u

/* Teams. Zero is "none", which is what a co-op player is; the two numbered teams are the
 * deathmatch's. */
#define MP_LOBBY_TEAM_NONE 0u
#define MP_LOBBY_TEAM_MAX  2u

/* The heroes the game ships, in the order its own hero swap takes them: Obi-Wan,
 * Qui-Gon, Panaka, Amidala. A client picks one of these; the host plays whichever the level
 * prescribes and never picks. */
#define MP_LOBBY_HERO_MAX 3u

/* The level as a path the loader takes verbatim ("level\\swamp.b3d", or one of the player's own),
 * and the name the lobby shows for it. Both NUL terminated inside their field, printable ASCII. */
#define MP_LOBBY_LEVEL_MAX 64u
#define MP_LOBBY_TITLE_MAX 24u

/* An index into the game's own level table, or this when the level is not one of them. */
#define MP_LOBBY_LEVEL_CUSTOM 0xFFu

/* tag, mode, flags, index, level, title, the rule set, the generation, the savegame's name and
 * size, the host's difficulty. Ninety two of these bytes are the note as it first shipped; the
 * nine of the rule set and the one of the generation were appended behind them, the eight of the
 * savegame behind those, and the difficulty last, so the head of the message is where it always
 * was. */
#define MP_LOBBY_SETUP_BYTES \
    (4u + MP_LOBBY_LEVEL_MAX + MP_LOBBY_TITLE_MAX + MP_RULES_BYTES + 1u + 8u + 1u)

/* The host's difficulty in the note: nought when the host says none, which a dedicated server and
 * a host whose cell did not read say, and otherwise the engine's difficulty plus one, 0 to 9 as
 * 1 to 10. A client plays the host's column of the damage table, because the host sets it for
 * every player of the session; the engine raises it at every level end and puts it back to four
 * at every new game, and a client that follows its host through the title took the second path
 * while the host took the first. */
#define MP_LOBBY_DIFFICULTY_NONE 0u
#define MP_LOBBY_DIFFICULTY_MAX  10u

/* The setup's flag byte. */
#define MP_LOBBY_F_FROM_SAVE 0x01u   /* the host restores a savegame; the level is what it
                                      * sits in */
#define MP_LOBBY_F_STARTED   0x02u   /* load it now */
/* The host is finished with this session: everyone goes back to the title screen.
 *
 * It rides the setup note rather than a message of its own because the setup is ALREADY repeated
 * once a second for as long as a session is known, in the lobby and inside a level both. A host
 * that leaves its level without quitting the process is the one case a timeout can never catch:
 * it keeps answering keepalives from the title screen, so the far side sees a healthy session
 * forever, and this is the bit that makes that case say so. */
#define MP_LOBBY_F_ENDED     0x04u

/* Every flag this build knows, and both directions of the codec refuse a setup carrying anything
 * else: an unknown bit is a note from a newer build, and acting on the half of it we understand
 * is worse than refusing the whole.
 *
 * It is derived rather than written out, because it was written out once and went stale the same
 * day MP_LOBBY_F_ENDED was added. The mask stayed at 0x03, so every setup note carrying the new
 * flag was refused by its own encoder and the end-of-session announcement it existed for never
 * left the host. Nothing failed and nothing was logged; the feature was simply absent. */
#define MP_LOBBY_F_KNOWN \
    ((uint8_t)(MP_LOBBY_F_FROM_SAVE | MP_LOBBY_F_STARTED | MP_LOBBY_F_ENDED))

typedef struct mp_lobby {
    uint8_t team;    /* 0..MP_LOBBY_TEAM_MAX */
    uint8_t ready;   /* 0 or 1 */
    uint8_t hero;    /* 0..MP_LOBBY_HERO_MAX */
} mp_lobby_t;

typedef struct mp_lobby_setup {
    uint8_t    mode;         /* the feature's own numbers: 1 co-op, 2 team deathmatch */
    uint8_t    flags;
    uint8_t    level_index;  /* 0..10, or MP_LOBBY_LEVEL_CUSTOM */
    char       level[MP_LOBBY_LEVEL_MAX];
    char       title[MP_LOBBY_TITLE_MAX];
    mp_rules_t rules;        /* what is being played, for a player who joined after it was set */
    uint8_t    generation;   /* raised by the host on every world change; wraps and may be any
                              * value, so it is compared for INEQUALITY and never for order */

    /* The savegame the host restores, named by the digest of its bytes and its length, so a
     * client can tell whether the file it holds is the one and ask for it otherwise (mp_savefile).
     * Both zero when the session begins from a fresh level, and both zero is what a setup with
     * MP_LOBBY_F_FROM_SAVE from a host that could not read its own file carries, which a client
     * reads as "load the level, there is nothing to fetch". */
    uint32_t   save_id;
    uint32_t   save_bytes;
    uint8_t    host_difficulty;   /* MP_LOBBY_DIFFICULTY_NONE, or the difficulty plus one */
} mp_lobby_setup_t;

/* ---- what a player says about themselves --------------------------------------------------- */

size_t mp_lobby_encode(const mp_lobby_t *lobby, uint8_t *buffer, size_t capacity);
bool   mp_lobby_is_lobby(const uint8_t *buffer, size_t bytes);
bool   mp_lobby_decode(const uint8_t *buffer, size_t bytes, mp_lobby_t *out);

/* ---- what the host says about the session --------------------------------------------------- */

/* Copies a level path or a title in, keeping printable ASCII only, cutting at the field and
 * trimming the blanks off both ends, so the encoder below always accepts what these produced. */
void mp_lobby_clean_field(const char *from, char *out, size_t out_size);

/* Refuses a mode that is neither, a flag bit this build does not know, an empty level path, and a
 * path or title that is not printable ASCII with its terminator inside the field. A level index
 * that is not MP_LOBBY_LEVEL_CUSTOM and not below eleven is refused as well: it names a table
 * entry that does not exist. A rule set outside the range this build accepts is refused in the
 * same way and by the same test in both directions, and so is a difficulty past
 * MP_LOBBY_DIFFICULTY_MAX; the generation is a free byte and every value of it is legal. */
size_t mp_lobby_setup_encode(const mp_lobby_setup_t *setup, uint8_t *buffer, size_t capacity);
bool   mp_lobby_is_setup(const uint8_t *buffer, size_t bytes);
bool   mp_lobby_setup_decode(const uint8_t *buffer, size_t bytes, mp_lobby_setup_t *out);

/* Whether two setups say the same thing, so a host repeats rather than resends on every tick.
 * The rules and the generation are part of the comparison: a raised generation is the host saying
 * "again", and a host that suppressed the repeat because everything else matched would have said
 * it to nobody. */
bool mp_lobby_setup_equal(const mp_lobby_setup_t *a, const mp_lobby_setup_t *b);

/* Whether a generation off the wire is one this side has not acted on.
 *
 * This is a test for INEQUALITY and it must never become a test for order. The byte wraps after
 * two hundred and fifty six world changes, and the change that takes it from 255 back to 0 is as
 * new as any other; a greater-than would sit there and let that one through unnoticed, and would
 * then reject every world change for the rest of the session. */
bool mp_lobby_generation_is_new(uint8_t acted_on, uint8_t heard);

/* Whether a client's lobby may take a start now. `start_offered` is the host's setup saying
 * STARTED and not ENDED under a generation this client has not acted on yet, which is what
 * mp_bridge_lobby_peek_start answers; `this_player_ready` is the ready this player said.
 *
 * One rule for every client: a start is taken only with this player's own ready. For a lobby the
 * host starts, nothing changes, because the host starts only once every player is ready. For a
 * player who joins a session that is already running, it is what keeps the level from loading
 * before a hero was chosen. A player who takes the ready back while the start is on its way stays
 * in the lobby and joins later, which is the same case. */
bool mp_lobby_start_may_be_taken(bool this_player_ready, bool start_offered);

/* ---- what the team byte is FOR --------------------------------------------------------------
 *
 * Until this rule existed the team travelled the whole way and nothing read it: a player could
 * pick a team, see it in everybody's roster, and it changed not one thing in the game. This is
 * its consumer, and it is the only one, which is why it is a plain function over four values
 * rather than a branch somewhere in the contact path.
 *
 * `friendly_fire` outranks the teams, in both games. With it on nobody is friendly and every hit
 * counts; the penalty for hitting your own side is a score rule and does not belong here.
 *
 * Co-operative play with it off refuses every hit between players, because two players on the same
 * campaign are one side.
 *
 * A deathmatch player with no team is in a free-for-all and may be hit by anyone, including by
 * somebody who does have one. That is what makes "teams off" cost nothing: a session where
 * nobody has a team IS the free-for-all, with no second rule to write. */
bool mp_lobby_may_damage(uint8_t mode, uint8_t attacker_team, uint8_t victim_team,
                         bool friendly_fire);

/* ---- when a session is over ------------------------------------------------------------------ */

/* Why a session ended. The text belongs to whoever draws it; what is decided here is only which
 * of the three it was, because the three are told apart by different evidence and a caller that
 * merged them would have to tell them apart again. */
typedef enum mp_lobby_over {
    MP_LOBBY_OVER_NO = 0,       /* it is not over */
    MP_LOBBY_OVER_HOST_ENDED,   /* the host said so: MP_LOBBY_F_ENDED arrived */
    MP_LOBBY_OVER_HOST_LOST,    /* the host stopped answering and the rejoin window ran out */
    MP_LOBBY_OVER_ALL_LEFT,     /* host side: nobody is left to play with */
    MP_LOBBY_OVER_CONTENT,      /* client side: the two builds disagree about the content */
    MP_LOBBY_OVER_BEHIND        /* client side: the host sent this side away for falling behind */
} mp_lobby_over_t;

/* The rule, over facts the caller has already gathered.
 *
 * `started` is what makes this safe to run every frame: before a session has begun there is
 * nothing to end, and a lobby that never started must not throw anybody anywhere. `level_running`
 * is the second half of that, the engine is only asked to leave a level while it is in one.
 *
 * The order of the arms is the decision. A host that ENDED the session outranks a host that is
 * merely silent, because the first is an answer and the second is the absence of one; a client
 * that has both has heard the answer and should not wait out a window for it. `sent_away` is the
 * client's: the host's notice that it could hold no more for this side, which is the host's last
 * word to this client and outranks both. */
mp_lobby_over_t mp_lobby_session_over(bool started, bool level_running, bool is_client,
                                      bool ended_flag, bool far_side_present,
                                      bool far_side_given_up, bool content_mismatch,
                                      bool sent_away);

/* ---- where a joining client stands ---------------------------------------------------------- */

/* Which of six a client's join is in, for the one line a lobby shows about it. */
typedef enum mp_lobby_join {
    MP_LOBBY_JOIN_ASKING = 0,   /* the handshake is under way */
    MP_LOBBY_JOIN_CONNECTED,
    MP_LOBBY_JOIN_DENIED,       /* the host refused, for a reason this build can name */
    MP_LOBBY_JOIN_HOST_LEFT,    /* the host said goodbye */
    MP_LOBBY_JOIN_GAVE_UP,      /* the host stopped answering */
    MP_LOBBY_JOIN_CONTENT       /* the two builds play different data */
} mp_lobby_join_t;

/* The rule, over facts the caller has gathered. The order of the arms is the decision: different
 * data outranks everything, a refusal outranks silence, a goodbye is told apart from a host that
 * stopped answering, and only then is a client connected or still asking. */
mp_lobby_join_t mp_lobby_join_status(bool content_mismatch, bool denied, bool host_left,
                                     bool gave_up, bool connected);

/* ---- whether a join asks for a password ----------------------------------------------------- */

/* What a join does about a password before it sends its request. */
typedef enum mp_lobby_password {
    MP_LOBBY_PASSWORD_NONE = 0,  /* the session says it wants none: none is offered */
    MP_LOBBY_PASSWORD_ASK,       /* it says it wants one: ask before the join goes out */
    MP_LOBBY_PASSWORD_WAIT       /* nothing is known: join, and ask only if the host refuses */
} mp_lobby_password_t;

/* The rule. `heard` is whether an announce for this session is in hand; `wants` is its password
 * bit, which means nothing without the first.
 *
 * WAIT is why this is a rule and not two lines. A typed address and a session code are never
 * heard, and the question used to be asked for them in advance, in case: every player joining a
 * public session by its code was asked for a password almost no host has. A refusal names its
 * reason, so the join can be made without one and the question asked when the host says it needs
 * one, which is the first moment anything on this side knows. */
mp_lobby_password_t mp_lobby_password_question(bool heard, bool wants);

/* ---- what both sides say once they have loaded a level ------------------------------------- */

/* A fingerprint of zero is refused: zero is the value that MEANS "not known yet", and sending it
 * would tell the far side nothing while looking like an answer. */
size_t mp_lobby_content_encode(uint32_t fingerprint, uint8_t *buffer, size_t capacity);
bool   mp_lobby_is_content(const uint8_t *buffer, size_t bytes);
bool   mp_lobby_content_decode(const uint8_t *buffer, size_t bytes, uint32_t *out);

#endif /* MULTIPLAYER_MP_LOBBY_H */
