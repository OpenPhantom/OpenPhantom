/* mp_dialog_relay.h: what the host is told, said again on a client that stands near enough.
 *
 * Layer 2, the binding. The codec next door has no engine in it; this file holds the detour, the
 * replay and the two rules that only exist because there is a running conversation on the other
 * side of the call: which side speaks, and how far it carries.
 *
 * ================================= What a client hears today ==================================
 *
 * Nothing at all, and the reason is worth stating because it is not an oversight in the wire.
 * A conversation is driven by the script of an NPC, and a script only runs in the `kEnemy_Active`
 * arm of `enemy_tickAll`. Every actor the host owns is PARKED on the client (state 3, stepped over
 * before the pre-tick), so its script does not run here and no line is ever spoken.
 *
 * ==================================== The shape of the fix ====================================
 *
 * `Dialog_SpeakSingle 0x00430D12` is hulled on both sides, and the HOST describes: it is the one
 * door a spoken line takes and its whole content is a line id, so the host sends the number and
 * the client produces the subtitle and the voice out of its OWN dialogue book and its own
 * VOICE.LAB. The host's own answer travels the same way, as the line it picked, and is said again
 * on the client at the place the host's body stands there.
 *
 * Only the host talks to people, and only the host's lines travel. A client's hull counts a line a
 * local script speaks and does not send it: replaying a stray client line on the host would go
 * through the same entry the host's running conversation lives in, and that entry clears the row
 * count and the speaker lock, which would kill an open menu on the host. With the use latch held
 * on a client such a line should not exist, and the count is how a run says whether one did.
 *
 * Only near the event, on every machine. Every line is judged once, around this hull on the machine
 * whose script speaks it and around the replay on a client, by one rule (mp_voice): presented where
 * this machine's own body stands within the reach the engine gives a placed voice, read out of the
 * voice's own code; not said again on a client that stands farther; kept alive at no volume on a
 * host whose script it paces while another player is near it. The engine is handed the place that
 * makes its own admission, measured from the camera eye, agree with that judgement.
 *
 * The hook must keep the return value. `Dialog_SpeakSingle` answers whether the line actually
 * STARTED, and that answer is the whole pacing gate for the caller. It is also what this module
 * uses to send an edge instead of a flood, because the 0x500 worker calls the entry again on every
 * tick that a line is being held.
 *
 * ================================ Why the replay is not a call ================================
 *
 * The client does call the same entry, and two things have to be arranged around it first.
 *
 * **The speaker stays NULL.** A speaker is a pointer into the sender's actor pool. Passing a made
 * up value would be a crash rather than a mistake: `Dialog_PlayVoice`'s reply path reads
 * `*(f32*)((u8*)pSpeakerLock + 0xbc)` whenever it is not null. NULL is the value the engine itself
 * handles, and it falls back to the default gain.
 *
 * **But NULL alone would silence every line after the first.** `Dialog_SpeakSingle` only plays a
 * voice when the speaker CHANGED, so a second line with the same NULL speaker would refresh the
 * subtitle and play nothing. The engine has the latch for exactly this case:
 * `Dialog_ForceRestart 0x00430E69` arms "re-open even for the same speaker", and it arms it when
 * the speaker it is given matches the one held, which NULL against NULL does. So the replay is
 * `Dialog_ForceRestart(NULL)` and then the speak, and every replayed line restarts and is heard.
 *
 * **`diagnostics` detours the same function** when it is switched on, and it is off by default.
 * `common/detour.c` chains in front of an existing `jmp rel32` rather than writing over it, and
 * the signature resolver falls back to a site's tail when another module has already branched over
 * its head, so both hooks run whichever loads first. The evidence that this arrangement holds is a
 * run with `[diagnostics] Enabled=1` in which both lines appear.
 *
 * SIZE NOTE: under 300 lines, no seam.
 */
#ifndef MULTIPLAYER_MP_DIALOG_RELAY_H
#define MULTIPLAYER_MP_DIALOG_RELAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*mp_dialog_relay_send_fn_t)(const uint8_t *bytes, size_t count);

/* Where the far player's body stands on this machine, for the host's answer: the bridge knows,
 * this module does not, and it is handed in rather than looked up so the layers stay where they
 * are. False when no far body is known; the answer then plays unplaced, which is heard everywhere
 * and counted. */
typedef bool (*mp_dialog_relay_place_fn_t)(float out[3]);
void mp_dialog_relay_set_place_source(mp_dialog_relay_place_fn_t far_place);

/* Resolves the sites and hulls the speak entry. False when the speak entry or the restart latch is
 * missing, and then this module does nothing rather than half of it: a hull with no restart latch
 * would replay lines that are seen and never heard, which is harder to diagnose than silence.
 *
 * The cells of the answer menu are optional inside that: a build whose add-choice pattern did
 * not match still relays what is said and says once that the host's answers cannot be watched. */
bool mp_dialog_relay_install(void);

/* Which side this is. The host describes and watches its own answers; a client says again and
 * never sends. */
void mp_dialog_relay_set_host(bool host);

/* Somewhere to send, handed in once per substep like the other relays that report from inside an
 * engine call rather than from a tick of their own. */
void mp_dialog_relay_set_send(mp_dialog_relay_send_fn_t send);

/* Once per substep, on both roles. On the host it watches its own answer menu for a row the
 * engine has marked as chosen and tells the client the line, once per menu. Nothing is detoured
 * for that: the engine raises the flag the script itself polls, so watching it is the honest
 * reading of what the player did. On a client it does nothing. */
void mp_dialog_relay_tick(void);

bool mp_dialog_relay_take_message(const uint8_t *note, size_t bytes);

void mp_dialog_relay_report(void);

#endif /* MULTIPLAYER_MP_DIALOG_RELAY_H */
