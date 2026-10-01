/* mp_player_sound_play.h: a far player's own sounds and shield, carried out at his puppet.
 *
 * The other half of mp_player_sound.h. A moment of a far player's body arrives with his world
 * slot, waits in his puppet's queue for the render tick it was made in, and is handed here from
 * inside the puppet's window, where the puppet's record is the engine's player record and its
 * place is the one the engine's own cry would be played at. What the engine then does with it
 * happens right after the window, outside it:
 *
 *   a sound     through the engine's own by-name entry with the far player's place and the flags
 *               the engine's own death cry passes, 3D and a copied place, so the engine's start
 *               gate decides whether this machine's listener is near enough to hear it, and its
 *               priority and its falloff stay the engine's. The cries are read out of this
 *               machine's own tables by the hero row the moment names; a scripted death, which the
 *               engine leaves silent, stays silent
 *   the burst   of a death by fire, through the engine's own burst on the puppet, before the
 *               burning cry as the engine does it. The burst hides the body, and the body is shown
 *               again when the far player stands again, because his machine has built him a new
 *               one; the burst touches nothing else of the puppet
 *   the shield  created on the puppet as the pickup creates it on the player, and released when
 *               the far player's timer ends, when he comes back from a death, or when it has
 *               outlasted the length it was told by more than two seconds
 *
 * Outside the window, because the engine's burst refuses the body the player record names as
 * alive, and inside the window that is the puppet itself.
 *
 * A moment older than ten substeps is counted and not played; a shield's rise and end are never
 * too late, since they are the state of a body rather than a sound at an instant. A moment for a
 * slot no puppet here shows is counted as having no body. A player who joins later is not told of
 * a shield that is already up.
 */
#ifndef MULTIPLAYER_MP_PLAYER_SOUND_PLAY_H
#define MULTIPLAYER_MP_PLAYER_SOUND_PLAY_H

#include "mp_events.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* From inside the window of far bank `bank`: `moment` is due. `record` is the puppet's record the
 * window installed, `object` its body or 0, and the render tick the puppet is drawn at, known or
 * not, decides whether the moment is too late. */
void mp_player_sound_due(size_t bank, uint32_t record, uint32_t object, const mp_event_t *moment,
                         uint32_t render_tick, bool render_known);

/* Right after the window of far bank `bank` has closed: what was due is carried out, and the
 * shield and the burst are looked after. */
void mp_player_sound_after_window(size_t bank);

/* A moment for a world slot no far body on this machine shows. */
void mp_player_sound_unplaced(const mp_event_t *moment);

/* The far body of `bank` is gone or starts over: whatever this file remembers of it goes too. The
 * engine releases a shield with the body it hung on. */
void mp_player_sound_forget(size_t bank);

/* The lines of both halves. */
void mp_player_sound_report(void);

#endif /* MULTIPLAYER_MP_PLAYER_SOUND_PLAY_H */
