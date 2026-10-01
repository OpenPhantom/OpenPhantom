/* mp_world_event_internal.h: what the host's half of the world events asks of a client's half.
 *
 * The one exit for every event state and the report are the host's file's; a client's queue and
 * memory are in mp_world_event_client.c and are reset and reported from there through these two.
 * Nothing here is public: mp_world_event.h stays the interface.
 */
#ifndef MULTIPLAYER_MP_WORLD_EVENT_INTERNAL_H
#define MULTIPLAYER_MP_WORLD_EVENT_INTERNAL_H

/* A client's queue, the part it holds from a stage, its memory of the numbers and the music the
 * host last said, all forgotten. */
void mp_world_event_client_reset(void);

/* The client's line of the report. */
void mp_world_event_client_report(void);

#endif /* MULTIPLAYER_MP_WORLD_EVENT_INTERNAL_H */
