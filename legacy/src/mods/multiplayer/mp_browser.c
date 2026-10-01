/* mp_browser.c: the session list a player picks from. See mp_browser.h. */
#include "mp_browser.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool same_endpoint(const mp_browser_row_t *row, const char *address, uint16_t port)
{
    return row->port == port && strncmp(row->address, address, MP_SETTINGS_ADDRESS_MAX) == 0;
}

static mp_browser_row_t *find(mp_browser_t *browser, const char *address, uint16_t port)
{
    size_t i;

    for (i = 0; i < browser->rows; ++i) {
        if (same_endpoint(&browser->row[i], address, port)) {
            return &browser->row[i];
        }
    }
    return NULL;
}

/* A new row, or NULL when the list is full. The list does not evict: a row that is in it was put
 * there by a broadcast somebody is still sending or by the player, and dropping one of those to
 * make room for the next broadcast would make the list churn instead of fill. Ageing is what
 * frees space, and it is the only thing that does. */
static mp_browser_row_t *add(mp_browser_t *browser, const char *address, uint16_t port)
{
    mp_browser_row_t *row;

    if (browser->rows >= MP_BROWSER_ROWS_MAX) {
        return NULL;
    }
    row = &browser->row[browser->rows++];
    memset(row, 0, sizeof *row);
    strncpy(row->address, address, MP_SETTINGS_ADDRESS_MAX - 1u);
    row->port = port;
    return row;
}

static void judge(mp_browser_t *browser, mp_browser_row_t *row)
{
    row->joinable = row->heard &&
                    mp_announce_joinable(&row->announce, browser->wire, browser->fingerprint);
}

void mp_browser_init(mp_browser_t *browser, uint8_t wire, uint32_t fingerprint)
{
    if (browser == NULL) {
        return;
    }
    memset(browser, 0, sizeof *browser);
    browser->wire        = wire;
    browser->fingerprint = fingerprint;
}

void mp_browser_set_fingerprint(mp_browser_t *browser, uint32_t fingerprint)
{
    size_t i;

    if (browser == NULL || browser->fingerprint == fingerprint) {
        return;
    }
    browser->fingerprint = fingerprint;

    /* Every row was judged against the value this one replaces, so every row is re-judged. A
     * browser opened before this side could make its statement has no fingerprint at all and
     * marks everything unjoinable; the moment the statement exists, the list has to stop saying
     * that. */
    for (i = 0; i < browser->rows; ++i) {
        judge(browser, &browser->row[i]);
    }
}

uint32_t mp_browser_row_age_cycles(const mp_browser_t *browser, const mp_browser_row_t *row)
{
    uint32_t elapsed;

    if (browser == NULL || row == NULL || !row->heard) {
        return MP_BROWSER_DROP_CYCLES;
    }

    /* The clock only goes forwards, but a caller that hands back an older value should not turn
     * into a row from the future. */
    elapsed = browser->now_ms >= row->heard_at_ms ? browser->now_ms - row->heard_at_ms : 0u;
    return elapsed / MP_BROWSER_CYCLE_MS;
}

bool mp_browser_row_live(const mp_browser_t *browser, const mp_browser_row_t *row)
{
    return mp_browser_row_age_cycles(browser, row) < MP_BROWSER_STALE_CYCLES;
}

/* A row is kept while anything still points at it: a fresh enough announce, a favourite, or the
 * address the player is typing. Only a LAN row that nobody saved and nobody typed ever leaves. */
static bool keep(const mp_browser_t *browser, const mp_browser_row_t *row)
{
    if (row->favourite || row->typed) {
        return true;
    }
    return mp_browser_row_age_cycles(browser, row) < MP_BROWSER_DROP_CYCLES;
}

void mp_browser_tick(mp_browser_t *browser, uint32_t now_ms)
{
    size_t read;
    size_t write = 0;

    if (browser == NULL) {
        return;
    }
    browser->now_ms = now_ms;

    for (read = 0; read < browser->rows; ++read) {
        if (!keep(browser, &browser->row[read])) {
            continue;
        }
        if (write != read) {
            browser->row[write] = browser->row[read];
        }
        ++write;
    }
    browser->rows = write;
    if (browser->selected >= browser->rows) {
        browser->selected = browser->rows > 0u ? browser->rows - 1u : 0u;
    }
}

bool mp_browser_heard(mp_browser_t *browser, const char *address, const uint8_t *datagram,
                      size_t bytes)
{
    mp_announce_t     announce;
    mp_browser_row_t *row;

    if (browser == NULL || address == NULL || address[0] == '\0') {
        return false;
    }
    if (!mp_announce_decode(datagram, bytes, &announce)) {
        ++browser->refused;
        return false;
    }

    /* The row is keyed by the GAME port out of the announce, not by the port the datagram arrived
     * from: the announce leaves the announce port and a join goes to the game port, and two hosts
     * on one machine differ in the second, never the first. */
    row = find(browser, address, announce.game_port);
    if (row == NULL) {
        row = add(browser, address, announce.game_port);
        if (row == NULL) {
            ++browser->dropped_full;
            return false;
        }
    }
    row->announce    = announce;
    row->heard       = true;
    row->heard_at_ms = browser->now_ms;
    judge(browser, row);
    ++browser->heard;
    return true;
}

void mp_browser_set_favourites(mp_browser_t *browser, const mp_settings_t *settings)
{
    size_t i;

    if (browser == NULL) {
        return;
    }
    for (i = 0; i < browser->rows; ++i) {
        browser->row[i].favourite = false;
    }
    if (settings == NULL) {
        return;
    }
    for (i = 0; i < settings->servers && i < MP_SETTINGS_SERVERS_MAX; ++i) {
        const mp_settings_server_t *saved = &settings->server[i];
        mp_browser_row_t           *row = find(browser, saved->address, saved->port);

        if (row == NULL) {
            row = add(browser, saved->address, saved->port);
        }
        if (row != NULL) {
            row->favourite = true;
        }
    }
}

void mp_browser_set_typed(mp_browser_t *browser, const char *address, uint16_t port)
{
    mp_browser_row_t *row;
    size_t            i;

    if (browser == NULL) {
        return;
    }
    for (i = 0; i < browser->rows; ++i) {
        browser->row[i].typed = false;
    }
    if (address == NULL || !mp_settings_valid_address(address) || !mp_settings_valid_port(port)) {
        return;
    }

    row = find(browser, address, port);
    if (row == NULL) {
        row = add(browser, address, port);
    }
    if (row != NULL) {
        row->typed = true;
    }
}

/* ==============================================================================================
 * The order.
 * ============================================================================================ */

/* The rank of a row, smaller first. Liveness outranks joinability because a live server this build
 * cannot join is still news, it tells the player their build is behind, while a stale row is
 * only a memory. */
static int rank(const mp_browser_t *browser, const mp_browser_row_t *row)
{
    int at = mp_browser_row_live(browser, row) ? 0 : 2;

    if (!row->joinable) {
        at += 1;
    }
    return at;
}

/* The text the order is settled by when the ranks tie. Deliberately the same string the screen
 * shows, so the list is in the order it looks like it is in. */
static void caption_of(const mp_browser_t *browser, const mp_browser_row_t *row,
                       char out[MP_ANNOUNCE_NAME_MAX + MP_SETTINGS_ENDPOINT_MAX])
{
    (void)mp_browser_row_caption(browser, row, out);
}

static bool before(const mp_browser_t *browser, const mp_browser_row_t *a,
                   const mp_browser_row_t *b)
{
    char a_text[MP_ANNOUNCE_NAME_MAX + MP_SETTINGS_ENDPOINT_MAX];
    char b_text[MP_ANNOUNCE_NAME_MAX + MP_SETTINGS_ENDPOINT_MAX];
    int  a_rank = rank(browser, a);
    int  b_rank = rank(browser, b);
    int  by_text;

    if (a_rank != b_rank) {
        return a_rank < b_rank;
    }
    caption_of(browser, a, a_text);
    caption_of(browser, b, b_text);
    by_text = strcmp(a_text, b_text);
    if (by_text != 0) {
        return by_text < 0;
    }

    /* Two rows with the same rank and the same caption are two endpoints, and the order between
     * them still has to be stable or the list shuffles under the cursor every frame. */
    by_text = strcmp(a->address, b->address);
    return by_text != 0 ? by_text < 0 : a->port < b->port;
}

size_t mp_browser_ordered(const mp_browser_t *browser, size_t position)
{
    size_t i;

    if (browser == NULL || position >= browser->rows) {
        return MP_BROWSER_ROWS_MAX;
    }

    /* A selection sort by counting: for each row, how many come before it. The list is at most
     * twenty four long and this is asked once per drawn row, so the cost is nothing and the
     * alternative, keeping a sorted index and maintaining it, is a second thing to get wrong. */
    for (i = 0; i < browser->rows; ++i) {
        size_t ahead = 0;
        size_t k;

        for (k = 0; k < browser->rows; ++k) {
            if (k != i && before(browser, &browser->row[k], &browser->row[i])) {
                ++ahead;
            }
        }
        if (ahead == position) {
            return i;
        }
    }
    return MP_BROWSER_ROWS_MAX;
}

const char *mp_browser_row_caption(const mp_browser_t *browser, const mp_browser_row_t *row,
                                   char out[MP_ANNOUNCE_NAME_MAX + MP_SETTINGS_ENDPOINT_MAX])
{
    (void)browser;

    if (out == NULL) {
        return "";
    }
    memset(out, 0, MP_ANNOUNCE_NAME_MAX + MP_SETTINGS_ENDPOINT_MAX);
    if (row == NULL) {
        return out;
    }
    if (row->heard && row->announce.name[0] != '\0') {
        memcpy(out, row->announce.name, MP_ANNOUNCE_NAME_MAX);
        out[MP_ANNOUNCE_NAME_MAX - 1u] = '\0';
        return out;
    }
    if (!mp_settings_format_endpoint(row->address, row->port, out, MP_SETTINGS_ENDPOINT_MAX)) {
        /* An endpoint that will not format is still a row somebody saved, and a blank row is one
         * nobody can pick. The address alone is the most that is certainly true about it. */
        strncpy(out, row->address, MP_SETTINGS_ADDRESS_MAX - 1u);
    }
    return out;
}

void mp_browser_move_selection(mp_browser_t *browser, int32_t delta)
{
    size_t  at;
    int64_t wanted;

    if (browser == NULL || browser->rows == 0u) {
        return;
    }

    /* The selection is an index into row[], but a player moves it in SCREEN order, so it is
     * converted there and back. */
    for (at = 0; at < browser->rows; ++at) {
        if (mp_browser_ordered(browser, at) == browser->selected) {
            break;
        }
    }
    if (at >= browser->rows) {
        at = 0;
    }
    wanted = (int64_t)at + delta;
    if (wanted < 0) {
        wanted = 0;
    }
    if (wanted > (int64_t)browser->rows - 1) {
        wanted = (int64_t)browser->rows - 1;
    }
    browser->selected = mp_browser_ordered(browser, (size_t)wanted);
    if (browser->selected >= browser->rows) {
        browser->selected = 0;
    }
}

const mp_browser_row_t *mp_browser_selected(const mp_browser_t *browser)
{
    if (browser == NULL || browser->rows == 0u || browser->selected >= browser->rows) {
        return NULL;
    }
    return &browser->row[browser->selected];
}
