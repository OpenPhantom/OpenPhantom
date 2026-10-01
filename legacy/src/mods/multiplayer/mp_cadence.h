/* mp_cadence.h: when the host's world leaves the host, and how it reaches a client's replicas.
 *
 * Layer 1, counters only. Nothing here reads the engine or changes what a player sees; every call
 * adds to a number, and the numbers are printed with the level's report.
 *
 * Why it exists. A client's enemies arrive unevenly: some of its substeps take no world of the
 * host's and some take two, and a replica then stands for a substep or skips one. Three causes
 * fit that picture and they want three different repairs: the host sending unevenly because its
 * census and encode vary, the interest rule letting a key wait on purpose, and two records of one
 * replica falling together in one substep so that the older never reaches its body. Before the
 * wire or the timeline is changed for any of them, these lines say which one it is:
 *
 *   on the host, the wall clock between two sends and what the census and the encode in front of
 *   each send cost;
 *   on a client, how many of the host's payloads each substep took, how far apart two records of
 *   one replica came, and what the flush did with them, including the substeps a parked replica
 *   went without a write while its rotation pair was still open, which the engine then draws
 *   again from its start;
 *   and one replica followed by name, by default Qui-Gon, placement 14 in FEDSHIP, on the client
 *   by its substeps and on the host by the class each peer's view gave it.
 *
 * Two of those causes are repaired in the flush, and the lines go on measuring them: the pairs the
 * flush closes in a substep without a write, and the second record of a replica it writes one
 * substep later instead of dropping. The windows in which one replica took two or more records are
 * counted whatever the flush then made of them, so a run shows how often the case arose as well as
 * what was done about it.
 *
 * The counters run in every session, because they are what a normal run has to show; each is an
 * increment, and the host's two clock readings a substep cost a few tens of nanoseconds. They are
 * kept for the life of the process, like the enemy block's own counters beside them.
 */
#ifndef MULTIPLAYER_MP_CADENCE_H
#define MULTIPLAYER_MP_CADENCE_H

#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The placement followed by name, from the ini. 0 turns the trace off, which leaves placement 0
 * untraceable; a number past the placements is refused with a warning and turns it off too. */
void     mp_cadence_set_traced(int32_t placement);
uint32_t mp_cadence_traced(void);   /* 0 while nothing is traced */

/* A client: one call for every payload of the host's that decoded, from the idle pump or the
 * substep alike, and one when its substep has taken the last of them. */
void mp_cadence_payload_taken(void);
void mp_cadence_substep_closed(void);

/* A host: the census and the encode of every peer's payload, bracketed on the wall clock. The end
 * is the moment the payloads are ready for the service that puts them on the wire. */
void mp_cadence_send_begins(void);
void mp_cadence_send_ends(void);

/* Pure, and what the two above come to: one send whose census and encode took `work_us`, and the
 * wall clock since the end of the one before, 0 for the first. An interval of a second or more is
 * a pause, a load or a join rather than the cadence, and starts the count over. */
void mp_cadence_send_measured(uint32_t work_us, uint32_t interval_us);

/* The views a host keeps, one for every peer it serves; the enemy block holds the same number. */
#define MP_CADENCE_VIEWS 3u

/* A host, once a substep after every peer's payload, for the traced placement and one peer's view:
 * how many blocks that view has built, whether the census listed the placement, the class the
 * view's interest rule last gave it, and whether a record of it left in this substep's payload.
 * A view whose block count did not move built no block in this substep and is not counted. */
void mp_cadence_host_trace(size_t view, uint32_t blocks, bool listed, uint8_t reach, bool sent);

/* A client: the keys an applied block named, counted for every key until the next flush; and the
 * host ticks between two records of one replica, from the block's own gap measure. */
void mp_cadence_records_taken(const bool *named, size_t keys);
void mp_cadence_forget_records(void);
void mp_cadence_record_gap(uint32_t gap);

/* Why records of one replica will never reach its body. The enemy table keeps the two newest
 * records a replica took since the last flush and writes them one substep apart; what falls out of
 * that is counted here by why. */
typedef enum mp_cadence_fall {
    MP_CADENCE_FELL_THIRD,      /* a third came before the flush and the oldest of them fell */
    MP_CADENCE_FELL_NEW_LIFE,   /* a new life: the old one's unwritten records fell */
    MP_CADENCE_FELL_LET_GO,     /* the replica was let go or removed, or has no body to take them */
    MP_CADENCE_FALLS
} mp_cadence_fall_t;

/* `count` records of `key` fell, for `why`. The first two are the ones the next write steps over,
 * and that write is counted as the newest of two or more. */
void mp_cadence_records_fell(size_t key, mp_cadence_fall_t why, uint32_t count);

/* A client's flush, once a substep, one call for every replica it wrote or held.
 *
 * A replica the flush wrote: the record written before this one (NULL when there was none), the
 * one written now, and `late` when that one had already waited through the last flush behind an
 * older record.
 *
 * A parked replica of this side's that the flush gave no record, or whose write the body refused:
 * `ours` says its body was this side's to close, and the two after it which of its pairs the head
 * of the flush brought to one value. A replica whose actor carries this player's body is never
 * closed, and neither is a slot that is not the actor.
 *
 * The run of flushes in a row that ended with a record of one replica still unwritten, and a
 * write the flush should never make; then the end of the flush, after which every key counts its
 * records afresh. */
void mp_cadence_flush_written(size_t key, const mp_enemy_record_t *before,
                              const mp_enemy_record_t *now, bool late);
void mp_cadence_flush_held(size_t key, bool ours, bool rotation_closed, bool position_closed);
void mp_cadence_flush_behind(uint32_t run);

typedef enum mp_cadence_fault {
    MP_CADENCE_FAULT_OTHER_LIFE,     /* a record written for a life other than the table's */
    MP_CADENCE_FAULT_SECOND_WRITE    /* a replica written twice in one substep */
} mp_cadence_fault_t;

void mp_cadence_flush_fault(mp_cadence_fault_t fault);
void mp_cadence_flush_done(void);

/* The lines of a host or of a client, each printed at nought as well. */
void mp_cadence_report(bool host);

#endif /* MULTIPLAYER_MP_CADENCE_H */
