/* mp_server_log.h: the server's log, written so that logging cannot slow the server down.
 *
 * THE PROBLEM. A server that logs every player position writes sixteen lines per substep, thirty
 * two substeps a second: five hundred lines a second, and that is before pings, pickups and
 * deaths, some 530 lines at about ninety characters each, close to fifty kilobytes a second. Done
 * the obvious way, each of those is an sprintf and a write on the thread that also has to
 * receive, aggregate and broadcast a world every 31.25 ms. A single flush that blocks for twenty
 * milliseconds costs a whole tick, and a tick that is late is a tick every client interpolates
 * through: their ladder is indexed by tick and a missing one is a hole in their history, not a
 * pause.
 *
 * So nothing is formatted on that thread and nothing is written on it. The server thread fills a
 * fixed size record with numbers and hands it to a ring: a bounded store, a memory barrier and an
 * index increment, with no allocation, no formatting, no lock and no system call. A second thread
 * owns the other end of the ring, turns records into text and writes them with buffered I/O. The
 * server thread never waits for the disk, never waits for a lock, and never waits for the writer.
 *
 * When the ring is full the record is dropped and counted. That is the whole back pressure
 * policy, and it is deliberate: a log is a description of the run, and a description that stops
 * the run to be complete has stopped being a description. The drop count is itself logged, so a
 * reader is told what they are missing rather than quietly given less.
 *
 * One producer, one consumer, and the discipline that makes the ring safe without a lock: only
 * the server thread moves `head`, only the writer thread moves `tail`, each publishes its index
 * after the data it describes, and each reads the other's index before the data it describes.
 *
 * Categories are checked before the record is built. Position logging off is then not a cheap
 * write, it is no write at all: one load and one branch on the server thread.
 *
 * SIZE NOTE: under 200 lines, no seam.
 */
#ifndef MULTIPLAYER_MP_SERVER_LOG_H
#define MULTIPLAYER_MP_SERVER_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What may be logged, as a mask. Position is its own category because it is the one that is
 * hundreds of lines a second and the one anybody will want off. */
#define MP_SERVER_LOG_SESSION  0x01u   /* joins, leaves, refusals, the server's own lines */
#define MP_SERVER_LOG_MATCH    0x02u   /* rounds, deaths, teams, the score */
#define MP_SERVER_LOG_PICKUP   0x04u
#define MP_SERVER_LOG_PING     0x08u
#define MP_SERVER_LOG_POSITION 0x10u
#define MP_SERVER_LOG_ALL      0x1Fu
#define MP_SERVER_LOG_DEFAULT  (MP_SERVER_LOG_SESSION | MP_SERVER_LOG_MATCH | MP_SERVER_LOG_PICKUP)

/* What one record is. Kinds are what the writer thread switches on to build a line. */
#define MP_SERVER_LOG_KIND_TEXT     0u   /* a whole line the caller wrote, in `text` */
#define MP_SERVER_LOG_KIND_JOIN     1u
#define MP_SERVER_LOG_KIND_LEAVE    2u
#define MP_SERVER_LOG_KIND_DENY     3u
#define MP_SERVER_LOG_KIND_POSITION 4u
#define MP_SERVER_LOG_KIND_PING     5u
#define MP_SERVER_LOG_KIND_DEATH    6u
#define MP_SERVER_LOG_KIND_PICKUP   7u
#define MP_SERVER_LOG_KIND_TEAM     8u
#define MP_SERVER_LOG_KIND_ROUND    9u   /* `a` the outcome, `b` the winner, `n` the generation */

/* Long enough for a WHOLE LINE, not just for a name. It was 28 to begin with, which held the
 * longest player name three times over and cut the server's own opening line in half: the first
 * thing it ever wrote came out as "listening on UDP port 27999" with the players, the level and
 * the rules missing. A record is 120 bytes at this width and the default ring is under a
 * megabyte, which is the right trade for a log that is read by people. */
#define MP_SERVER_LOG_TEXT_MAX 96u

/* One size for every kind on purpose: the ring is then an array, a write is a store, and there is
 * no length to carry and no branch to take. */
typedef struct mp_server_log_record {
    uint32_t ms;       /* when it happened, on the server's own wall clock */
    uint8_t  kind;
    uint8_t  slot;
    uint8_t  a;        /* team, killer slot, reason, outcome: whatever the kind means */
    uint8_t  b;
    int32_t  n;        /* a round trip in ms, a placement index, a generation, a point total */
    float    pos[3];
    char     text[MP_SERVER_LOG_TEXT_MAX];   /* a player name, or a whole line; always terminated */
} mp_server_log_record_t;

typedef struct mp_server_log_counters {
    uint32_t offered;    /* records the server thread built */
    uint32_t written;    /* lines the writer thread put on disk */
    uint32_t dropped;    /* records the ring had no room for */
    uint32_t filtered;   /* records not built at all, because their category was off */
} mp_server_log_counters_t;

/* A formatted line, as it reaches the screen. The writer thread calls this, so it must not block
 * and must not call back into the log. It exists so the window can show what the file gets
 * without either of them reading the other. */
typedef void (*mp_server_log_line_fn_t)(const char *line, void *context);

/* Opens the log. `path` may be NULL for a log that only feeds the screen. `ring_records` is
 * rounded up to a power of two so the index wrap is a mask rather than a division; zero takes the
 * default. False when the ring could not be allocated or the writer thread did not start, and in
 * that case nothing is logged rather than logged slowly. */
bool mp_server_log_open(const char *path, uint32_t categories, size_t ring_records);

/* Stops the writer thread after draining what is still in the ring, then closes the file. */
void mp_server_log_close(void);

void     mp_server_log_set_categories(uint32_t categories);
void     mp_server_log_set_line_listener(mp_server_log_line_fn_t listener, void *context);
void     mp_server_log_counters(mp_server_log_counters_t *out);

/* Whether a category is on. The callers use it to skip building a record at all, which is the
 * difference between position logging off costing one branch and costing a memcpy. */
bool mp_server_log_wants(uint32_t category);

/* The hot path. Never blocks, never allocates, never formats, never fails loudly: a record the
 * ring has no room for is dropped and counted. */
void mp_server_log_put(uint32_t category, const mp_server_log_record_t *record);

/* The shapes a caller actually has. Each one checks its category first and returns without
 * touching a record when it is off. */
void mp_server_log_text(uint32_t category, const char *line);
void mp_server_log_join(uint8_t slot, const char *name);

/* A leave names one of these. The server tells a peer the session sent away for falling behind
 * from every other departure; a goodbye and a timeout it does not tell apart, so both are gone. */
#define MP_SERVER_LEAVE_GONE      "gone"
#define MP_SERVER_LEAVE_SENT_AWAY "sent away for falling behind"
void mp_server_log_leave(uint8_t slot, const char *name, const char *why);
void mp_server_log_position(uint8_t slot, const float pos[3]);
void mp_server_log_ping(uint8_t slot, const char *name, uint32_t rtt_ms);
void mp_server_log_death(uint8_t victim, uint8_t killer, uint8_t reason);
void mp_server_log_pickup(uint8_t slot, uint32_t placement_index);
void mp_server_log_team(uint8_t slot, const char *name, uint8_t team, bool asked_for_it);
void mp_server_log_round(uint8_t outcome, uint8_t winner, uint32_t generation);

#endif /* MULTIPLAYER_MP_SERVER_LOG_H */
