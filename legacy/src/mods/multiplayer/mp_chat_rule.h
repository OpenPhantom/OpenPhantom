/* mp_chat_rule.h: what a line of chat may hold, how fast a player may say one, and what a host
 * makes of one.
 *
 * Layer 1, pure. No engine, no socket, no clock of its own: bytes and a time in, a verdict out. The
 * listen host and the dedicated server call the same host rule below, which is why it is here and
 * not in the module that runs the chat in the game.
 *
 * ================================= What a line may hold ======================================
 *
 * Printable ASCII, 0x20 to 0x7E, without the backslash, at most MP_CHAT_TEXT_MAX bytes. A line is
 * drawn with the engine's own font and measured against width tables that end at 0x7F; the fonts
 * of the five shipped languages have not all been counted above that, and a character a font has
 * no glyph for is drawn at coordinates that are not numbers. The backslash is out because the
 * engine's text drawer reads it as the start of an escape, so a player could break the rows of
 * everybody's chat box with two characters. Player names follow the same rule for the same reason.
 *
 * Keyboard input arrives as bytes of the process's ANSI code page, because the game's window class
 * is an ANSI one. On the Western code page, 1252, the letters the five languages type are written
 * out the way their writers do without them: the German umlauts as ae, oe, ue and the sharp s as
 * ss, a letter with an accent as its plain letter, the two ligatures of French and Danish spelled
 * out, and the inverted marks of Spanish as the upright ones. On any other code page the same byte
 * is another letter, so every byte above 0x7E becomes a question mark there. Control characters
 * and DEL are dropped, and a backslash becomes a slash.
 *
 * ================================= How fast ====================================================
 *
 * A token bucket per player: a burst of lines at once, then one per interval. The same rule runs
 * twice with two sizes. The typing side holds its own line back before it sends it, three at once
 * and then one a second; the host refuses only beyond that, five at once and then one every 700
 * ms, so that resends and jitter on the way cannot make a line the typing side let through arrive
 * too early and be refused, which its writer would only notice as a line that never came back.
 * The clock is any millisecond count that wraps at 32 bits; only differences of it are taken.
 *
 * ================================= The host's rule =============================================
 *
 * A player says only its text. The host decides who said it, from the connection the say arrived
 * on, and whose name it carries, from its own table; the say names neither, because a peer's word
 * about its own slot settles nothing. The host trims the text, refuses a say that is torn or empty
 * after the trim, takes one line out of that slot's bucket, and builds the line every player gets,
 * the one who said it included, in the same order everywhere.
 */
#ifndef MULTIPLAYER_MP_CHAT_RULE_H
#define MULTIPLAYER_MP_CHAT_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The longest text a line carries, in bytes after the transliteration. An input field that is
 * full takes nothing more, so nothing is ever cut on the way out. */
#define MP_CHAT_TEXT_MAX 120u

/* The world slots a line may name: the listen host's player is 0, the peers 1 upward. */
#define MP_CHAT_SLOTS 16u

/* The listen host's own player. A dedicated server has no player and leaves this slot empty, so a
 * line the host rule builds for slot 0 is always one the host said itself. */
#define MP_CHAT_HOST_SLOT 0u

/* The Western ANSI code page, the only one whose letters above 0x7E are written out. */
#define MP_CHAT_CODE_PAGE_WESTERN 1252u

/* Appends what one byte of keyboard input becomes on the wire and answers how many bytes were
 * appended: 0 when the byte has no place in a line or the whole of its transliteration does not
 * fit. `capacity` counts text bytes without the terminator, so the buffer holds capacity + 1
 * bytes; `length` is the text already in it. The terminator is written behind the text either way,
 * and nothing is written when `length` is past `capacity`. `code_page` is the process's ANSI code
 * page, as GetACP answers it.
 *
 * The 0 is two answers, and a caller that counts them apart reads the byte: it means no room only
 * for `typed >= 0x20 && typed != 0x7F`; for any other byte, a control character or DEL, it means
 * the byte is one a line does not carry. A byte that went in as something else, a transliteration,
 * a question mark or the slash for a backslash, is one whose answer is not 1, or whose one byte in
 * the line is not `typed`. */
size_t mp_chat_rule_append_typed(char *line, size_t length, size_t capacity, uint8_t typed,
                                 uint32_t code_page);

/* Trims the spaces at both ends in place and answers the length left; 0 means there is nothing
 * to say. When anything was trimmed, the byte behind what is left becomes a terminator, which is
 * inside the original length, so the function never writes past `length`. */
size_t mp_chat_rule_trim(char *line, size_t length);

/* Whether every byte is one a line may carry, 0x20 to 0x7E without the backslash, and the length
 * is 1 to MP_CHAT_TEXT_MAX. */
bool mp_chat_rule_text_is_sound(const char *text, size_t length);

/* A bucket of lines. All zero is a bucket that has not been used, and the first take fills it.
 *
 * It holds milliseconds of refill rather than fractions of a line: a line costs `every_ms` of them
 * and every millisecond adds one, so an interval of 700 ms loses nothing to rounding however often
 * the bucket is asked. Thousandths of a line would gain 1.43 a millisecond, and a bucket asked
 * every millisecond would drift by the part that does not divide. */
typedef struct mp_chat_pace {
    uint32_t level_ms;   /* at most burst times every_ms */
    uint32_t last_ms;    /* the time up to which the bucket has been filled */
    bool     started;
} mp_chat_pace_t;

/* Takes one line from the bucket at `now_ms`, answering false while it is empty. It holds `burst`
 * lines and gains one every `every_ms`; a bucket is asked with the same two every time. A clock
 * that runs backwards fills nothing, and the bucket fills again from that time on. */
bool mp_chat_pace_take(mp_chat_pace_t *pace, uint32_t now_ms, uint32_t burst, uint32_t every_ms);

/* Puts back the line the last take removed, for a line that was then not sent at all. */
void mp_chat_pace_give_back(mp_chat_pace_t *pace, uint32_t burst, uint32_t every_ms);

#define MP_CHAT_PACE_CLIENT_BURST    3u
#define MP_CHAT_PACE_CLIENT_EVERY_MS 1000u
#define MP_CHAT_PACE_HOST_BURST      5u
#define MP_CHAT_PACE_HOST_EVERY_MS   700u

/* What a host counts. The report line of a listen host and the status line of a dedicated server
 * print these; neither ever holds a text or a name. */
typedef struct mp_chat_host_counts {
    uint32_t taken;           /* says of other players turned into a line */
    uint32_t unsound;         /* says that were torn, or empty after the trim */
    uint32_t too_fast;        /* says beyond the bucket of their slot */
    uint32_t no_seat;         /* says from a slot no player can hold */
    uint32_t said_here;       /* lines the listen host's own player said */
    uint32_t lines_out;       /* lines given to the session for every player */
    uint32_t copies;          /* one per player a line reached */
    uint32_t copies_unsent;   /* one per connected player it did not reach: neither that
                               * player's channel nor its hold took it, and it was sent away */
} mp_chat_host_counts_t;

typedef struct mp_chat_host {
    mp_chat_pace_t        pace[MP_CHAT_SLOTS];
    mp_chat_host_counts_t counts;
} mp_chat_host_t;

typedef enum mp_chat_host_verdict {
    MP_CHAT_HOST_LINE = 0,   /* a line was built */
    MP_CHAT_HOST_UNSOUND,
    MP_CHAT_HOST_TOO_FAST,
    MP_CHAT_HOST_NO_SEAT
} mp_chat_host_verdict_t;

/* What a host makes of the say `say` that arrived from `slot`, whose player the host calls
 * `name`: the line for every player, written to `line` with its length in `line_bytes`, or a
 * refusal. Each verdict is counted in `host`, a line for MP_CHAT_HOST_SLOT as said here. `line`
 * must hold MP_CHAT_LINE_MAX_BYTES; a smaller buffer is refused as unsound. A name that is not
 * sound is cleaned the way the roster cleans one. */
mp_chat_host_verdict_t mp_chat_rule_host_line(mp_chat_host_t *host, const uint8_t *say,
                                              size_t say_bytes, uint8_t slot, const char *name,
                                              uint32_t now_ms, uint8_t *line, size_t capacity,
                                              size_t *line_bytes);

/* Counts one line handed to the session: `peers` connected before the call, `reached` as the
 * session's broadcast answered. What it did not reach is counted as unsent copies. */
void mp_chat_rule_host_sent(mp_chat_host_t *host, size_t peers, size_t reached);

#endif /* MULTIPLAYER_MP_CHAT_RULE_H */
