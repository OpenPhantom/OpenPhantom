/* mp_browser.h: the list of sessions a player picks from, as a value with no socket in it.
 *
 * Layer 1, pure. It is fed announces and favourites and a typed address, and it answers "what does
 * the screen show, in what order, and what is selected". Driving that in a test is the point:
 * every awkward part of a server browser is a timing question, and a timing question answered in
 * the field costs a field run each time.
 *
 * ==================================== One list, three sources =================================
 *
 * A row can come from a broadcast a host is sending, from the eight favourites in the ini, or from
 * an address the player typed. The SOURCE is a column rather than three separate lists, because a
 * favourite that is running has to be ONE row and not two: the favourite gives it a place in the
 * list when it is quiet and the announce gives it live numbers when it is not.
 *
 * The identity of a row is its ENDPOINT, the address it was heard from together with the game
 * port it named. Not the session name, which a host may change while the browser is open, and not
 * the announce port, which is the same for everybody.
 *
 * ======================================= Why rows age =========================================
 *
 * A lost broadcast is a lost packet, not a server that stopped. So a row that has not been heard
 * from goes STALE first, it stays in place, keeps its last numbers and is shown as not current;
 * and only later goes away. A favourite never goes away at all; it loses its numbers and stays,
 * because a player who saved a server wants to see it in the list whether or not it is up.
 *
 * A row that jumps out of a list under the cursor is worse than a row that is briefly wrong, which
 * is why the two stages exist and why the order below never depends on liveness alone.
 *
 * ======================================= Why the order ========================================
 *
 * Live before stale, joinable before not, then by name. Sorting by ping was considered and left
 * out: the ping of a session is not known until there is a session, so the column is empty for
 * exactly the rows a player is choosing between, and a sort on it would shuffle the list as the
 * numbers arrive.
 */
#ifndef MULTIPLAYER_MP_BROWSER_H
#define MULTIPLAYER_MP_BROWSER_H

#include "mp_announce.h"
#include "mp_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many rows the list holds. The screen shows eight at a time (eight fits a 640x480
 * screen with room for the buttons under it); the model keeps more so that a full LAN does not
 * push the player's own favourites out of the list before they can be seen. */
#define MP_BROWSER_ROWS_MAX 24u

/* The announce cycle, in milliseconds: how often a host is expected to speak. Everything below is
 * a multiple of this rather than a time of its own, so changing the cycle changes the ages with
 * it. */
#define MP_BROWSER_CYCLE_MS 1000u

/* Missed cycles before a row is stale, and before a row that is not a favourite is dropped. Three
 * is enough that a single lost datagram is invisible; eight is long enough that a host reloading a
 * level does not vanish from the list while it does. */
#define MP_BROWSER_STALE_CYCLES 3u
#define MP_BROWSER_DROP_CYCLES  8u

typedef enum mp_browser_source {
    MP_BROWSER_SOURCE_LAN = 0,       /* heard on the wire */
    MP_BROWSER_SOURCE_FAVOURITE = 1, /* remembered in the ini */
    MP_BROWSER_SOURCE_TYPED = 2      /* what is in the address field right now */
} mp_browser_source_t;

typedef struct mp_browser_row {
    char     address[MP_SETTINGS_ADDRESS_MAX];
    uint16_t port;             /* the GAME port: where a join goes */

    bool          heard;       /* an announce has ever been decoded for this row */
    mp_announce_t announce;    /* the last one; untouched while the row is stale */
    uint32_t      heard_at_ms; /* when, on the browser's own clock */

    bool favourite;            /* also one of the saved eight */
    bool typed;                /* also what the player has in the address field */
    bool joinable;             /* the announce agreed with this build, as of when it was heard */
} mp_browser_row_t;

typedef struct mp_browser {
    mp_browser_row_t row[MP_BROWSER_ROWS_MAX];
    size_t           rows;

    uint32_t now_ms;           /* last time given to mp_browser_tick */
    size_t   selected;         /* index into row[]; meaningless when rows == 0 */

    uint8_t  wire;             /* this build's, for the joinable answer */
    uint32_t fingerprint;      /* this build's statement as one number; 0 while it has none */

    uint32_t heard;            /* announces taken in */
    uint32_t refused;          /* datagrams that were not an announce, or did not decode */
    uint32_t dropped_full;     /* announces from a new endpoint with no row left to give it */
} mp_browser_t;

/* Empties the list and takes this build's identity. The fingerprint is this build's statement as
 * one number, which the menu can ask for before any transport stands; with a 0 no row is ever
 * joinable, which is honest rather than optimistic. */
void mp_browser_init(mp_browser_t *browser, uint8_t wire, uint32_t fingerprint);

/* The fingerprint, once it is known or has changed. Rows already in the list are re-judged
 * against it, because they were judged against a value that was not yet true. */
void mp_browser_set_fingerprint(mp_browser_t *browser, uint32_t fingerprint);

/* Advances the clock, which is what makes rows go stale and then go away. Called once a frame with
 * a millisecond count that only goes forwards. */
void mp_browser_tick(mp_browser_t *browser, uint32_t now_ms);

/* An announce arrived from `address`. False when the datagram was not an announce, did not decode,
 * or came from an address the list has no room for. The address is the DATAGRAM's source and the
 * port inside the announce is the game port; together they are the row's identity. */
bool mp_browser_heard(mp_browser_t *browser, const char *address, const uint8_t *datagram,
                      size_t bytes);

/* Puts the saved servers into the list, merging with anything already heard from the same
 * endpoint. Called whenever the settings change, and once when the screen opens. */
void mp_browser_set_favourites(mp_browser_t *browser, const mp_settings_t *settings);

/* Marks one endpoint as the typed one, and unmarks whatever was before. An empty or malformed
 * address only unmarks. A typed endpoint that is in the list already is not duplicated. */
void mp_browser_set_typed(mp_browser_t *browser, const char *address, uint16_t port);

/* ---------------------------------------------------------------------------------------------
 * What the screen asks.
 * ------------------------------------------------------------------------------------------- */

/* How old the row's last announce is, in whole cycles. A row never heard from answers the drop
 * threshold, which is what makes a favourite that has never answered read as "not up". */
uint32_t mp_browser_row_age_cycles(const mp_browser_t *browser, const mp_browser_row_t *row);

/* Heard recently enough to believe its numbers. */
bool mp_browser_row_live(const mp_browser_t *browser, const mp_browser_row_t *row);

/* The rows in the order the screen draws them, one index at a time: live before stale, joinable
 * before not, then by name, and an endpoint's own text when it has no name yet. `position` is the
 * row on screen; the answer is an index into row[], or MP_BROWSER_ROWS_MAX past the end. */
size_t mp_browser_ordered(const mp_browser_t *browser, size_t position);

/* The row's first column, ready to draw: the session name if one was heard, otherwise the
 * endpoint. Never NULL, never empty. */
const char *mp_browser_row_caption(const mp_browser_t *browser, const mp_browser_row_t *row,
                                   char out[MP_ANNOUNCE_NAME_MAX + MP_SETTINGS_ENDPOINT_MAX]);

/* Moves the selection by `delta` rows in SCREEN order, clamped at both ends. Does nothing to an
 * empty list. */
void mp_browser_move_selection(mp_browser_t *browser, int32_t delta);

/* The selected row, or NULL when the list is empty. */
const mp_browser_row_t *mp_browser_selected(const mp_browser_t *browser);

#endif /* MULTIPLAYER_MP_BROWSER_H */
