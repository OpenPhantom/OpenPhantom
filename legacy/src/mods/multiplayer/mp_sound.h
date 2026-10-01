/* mp_sound.h: give a sound a place when the body that caused it is not the one listening.
 *
 * The engine plays a player's own action sounds through `bapsound_playName`, and that entry point
 * passes NO POSITION. Out of the retail image, at 0x00416787:
 *
 *     55                        push ebp
 *     8B EC                     mov  ebp, esp
 *     51                        push ecx
 *     83 3D B8 B4 5B 00 00      cmp  dword [0x5BB4B8], 0      ; sound switched on at all?
 *     75 05 / 83 C8 FF / EB 26  jne +5 / or eax, -1 / jmp end ; no: answer -1
 *     C7 05 14 A9 4A 00 01 ..   mov  dword [0x4AA914], 1      ; the sound tag
 *     8B 45 0C / 50             push flags
 *     6A 00                     push 0                        ; the point
 *     6A 00                     push 0                        ; the handle
 *     8B 4D 08 / 51             push wavename
 *                               call bapsound_playByName      ; 0x004171A1
 *
 * A sound with no point skips the start-distance gate in `bapsound_startChannel` (0x004169BD,
 * `cmp [ebp+0xc], 0 / je` before the sum of squares) and the per-frame rolloff in
 * `bapsound_updateChannel` (0x00415E30, behind `if (pPos != 0)` for the distance and behind
 * SNDF_DISTANCE for the volume), so it plays at the by-name template's volume everywhere in the
 * level, however far away it happened. The template at [0x4AA918] carries volume 2.0, priority 90
 * and distances 3 and 14, and without a place not one of them is used. The tag at [0x4AA914] is
 * referenced fourteen times in the image, every one a write except two self tests inside the
 * by-name calls, so the tag a re-pointed call leaves behind matters to nothing.
 *
 * The class was counted rather than guessed: 137 call sites of `bapsound_playName` and one of
 * `bapsound_playNameVol`. 113 are the front end (88 in the menu, 25 in the control screen) plus
 * the widget wrapper at 0x00463031, where a placeless sound is right. The other 23 sites, in 15
 * functions, all load the player record: the sabre ignite and extinguish (0x0044B268, 0x0045113F
 * and 0x004511A1), the weapon select 0x0044B609, the punch 0x0044855C, the two hits 0x00448729
 * and 0x00449047, the pickup 0x00448894, the key 0x0044C9A0, the water entry 0x0044D1E5, the club
 * 0x0044E6E2, the impact voices 0x00450E98 and the whole footstep module. The one exception is
 * `npc_blockImpactFx` at 0x0042E133, which any character reaches, and which is why that one has
 * a detour of its own.
 *
 * With one player that is right, and cheaper than the alternative: everything he does happens next
 * to the listener, so measuring the distance would only cost time and attenuating it would be
 * wrong. The sabre a far player ignites goes down the same road, and there the assumption is
 * broken. That is the whole of "audio is transmitted even though you are out of range".
 *
 * So the repair does not belong in the engine's routines but around OUR calls into them. While
 * this module holds an anchor, a sound that arrives at the funnel without a position is given the
 * anchor's point and the engine's own software rolloff, and the engine does the rest. Nothing is
 * invented: the volume, the priority and the two distances stay the ones the record already
 * carried, and a sound that happens next to the listener is unchanged, because inside the near
 * field the attenuation is the identity.
 *
 * What the anchor does not cover. `bapsound_playNameVol` is a second positionless entry point and
 * is not detoured, because the funnel below it is, and because its only caller is the footstep
 * module, whose twelve call sites all load the player record, so a puppet makes no footsteps
 * today. It is named here so that the day a puppet does, this is where the answer already is.
 *
 * The listener a re-pointed sound is measured against is the view's ANCHOR point, not the camera
 * eye: the eye is only used for Miles 3D voices, and these are software 2D ones.
 *
 * Two properties that make the anchor safe, both read out of the shipped code rather than assumed:
 *
 *   The point is copied, not subscribed to. `bapsound_startChannel` stores the caller's pointer in
 *   the channel, which would normally mean the storage has to outlive the voice. It also clears
 *   SNDF_STATIC_POS before its first update and raises it again afterwards, and the update copies
 *   `*pPos` into the channel exactly while that bit is down. A sound started with SNDF_STATIC_POS
 *   therefore reads the pointer twice, both times inside the call, once for the start-distance
 *   gate and once for that seeded update, and never after it returns. The channel keeps the
 *   pointer VALUE and null-tests it every frame, but never dereferences it again. That is why one
 *   cell is enough here and a ring of them would only imply a lifetime the engine does not have.
 *   The first version kept sixteen, on the assumption that the channel reads through the pointer
 *   while the voice plays; the tail of `bapsound_startChannel` refuted it:
 *
 *       flags &= 0xFFF7FFFF                    ; SNDF_CHANNEL_FREE down
 *       flags &= 0xFFFFFFDF                    ; SNDF_STATIC_POS down
 *       bapsound_updateChannel(ch, 8)          ; copies *pPos while the bit is down
 *       if (pSound->flags & 0x20) flags |= 0x20
 *
 *   and the update's `c->pos = *c->pPos` sits behind `(flags & 0x20) == 0`.
 *
 *   A one-shot is never killed by distance. The distance cull in the per-frame update is behind
 *   SNDF_LOOP. Every member of the positionless class is a one-shot, so anchoring one cannot leave
 *   a voice stuck or cut one short. What still moves with the listener is the volume: the channel
 *   recomputes its distance every frame even with a static point, so walking away from a far
 *   player's fight makes it quieter.
 *
 * What a placed sound gains is the engine's own curve, not a new one: `2.0 * (1 - f) * 127`
 * clamps at 127 until f reaches 0.5, so the volume is flat at the ceiling out to 8.5 units, then
 * linear to silence at 14, and past 14 the start gate refuses the voice outright. Two players more
 * than 14 units apart therefore hear nothing of each other's action sounds, which is deliberate
 * and is further than the authored sabre hum (8.0) and swing (5.0) reach. One asymmetry is the
 * engine's: the channel seeds its volume as `base * 127 * gain` and the rolloff overwrites it with
 * `base * (1 - f) * 127` without the gain; the volume slider still reaches the voice through the
 * digital master, so what is lost is a second application of the same slider, and every one of
 * the 1122 authored records that carry SNDF_DISTANCE already behaves this way.
 *
 * SNDF_DONT_DUP (0x200) is on the sabre pair, the punch, the club, the key and the weapon select,
 * and the engine tests it BEFORE the distance gate, so a far player's admitted sound still blocks
 * the local player's identically named one for its duration. That rule predates this module;
 * anchoring only changes which of the two got in.
 */
#ifndef MULTIPLAYER_MP_SOUND_H
#define MULTIPLAYER_MP_SOUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The two engine flags the anchor adds to a record it re-points. DISTANCE turns on the software
 * rolloff, which is the 2D one and needs no Miles 3D provider; STATIC_POS is what makes the point
 * a copy rather than a subscription. SNDF_3D was not chosen because that path needs a Miles
 * provider and a free 3D voice handle, and `bapsound_startChannel` demotes it to exactly
 * SNDF_DISTANCE at two places when either is missing: asking for the software path directly is
 * the same result without the dependency. */
#define MP_SOUND_FLAG_DISTANCE   0x00000002u
#define MP_SOUND_FLAG_STATIC_POS 0x00000020u

/* The engine's sound record, as much of it as the anchor touches: the flag word sits at +0x18 and
 * the whole record is 0x40 bytes, which is what `bapsound_playByName` builds on its own stack. */
#define MP_SOUND_RECORD_BYTES 0x40u
#define MP_SOUND_RECORD_FLAGS 0x18u

/* What an anchor window replaced, so a window inside another window can put it back. */
typedef struct mp_sound_anchor {
    float position[3];
    bool  active;
} mp_sound_anchor_t;

/* ---------------------------------------------------------------------------------------------
 * The decisions, as plain functions over plain numbers, so they can be tested with no game.
 * ------------------------------------------------------------------------------------------- */

/* Whether this call at the funnel is one the anchor may re-point: an anchor is held, the caller
 * named no point of its own, and there is a record to copy. A call that already carries a point
 * is left alone, the engine placed it, and it is placed better than we could. */
bool mp_sound_anchor_applies(bool anchor_held, const void *record, const float *position);

/* What the re-pointed copy's flag word becomes. */
uint32_t mp_sound_anchored_flags(uint32_t flags);

/* ---------------------------------------------------------------------------------------------
 * The engine half.
 * ------------------------------------------------------------------------------------------- */

/* Resolves the two sites and detours them. False with a log line when a site did not resolve;
 * nothing is patched then and every sound keeps the engine's own behaviour. */
bool mp_sound_install(void);
bool mp_sound_installed(void);

/* Open a window in which a positionless sound belongs at `position`, and close it again. The
 * previous anchor is handed back rather than dropped, because the engine calls one of these
 * windows from inside another. */
void mp_sound_anchor_open(const float position[3], mp_sound_anchor_t *previous);
void mp_sound_anchor_close(const mp_sound_anchor_t *previous);

void mp_sound_reset(void);
/* The engine's three kinds of blade clang: a ricochet, blade against blade, blade against
 * armour. The numbers are the engine's own; they pick the sound out of a triple. */
#define MP_SOUND_BLOCK_KINDS 3u

/* A host's clang, played again on a client's replica.
 *
 * The clang is rationed by one cooldown cell for every actor in the level, and on the host only a
 * call that cell let through became an event. A client that let its own cell decide again would
 * hold a second clang inside 0.2 s of the first on a clock the host never saw. So the replay opens
 * the cell first, writes 0.0, which is at or below every world time the engine can have, calls
 * the engine's clang through the trampoline inside an anchor at the replica's position, reads the
 * cell again to see the clang happen, and puts back what the cell held. Nothing of the replay is
 * left in the engine: a blocker this client runs itself keeps its own cooldown, and after the
 * session the cell holds no value the session made.
 *
 * The trampoline, never the hull: a replay is not counted as the engine calling the clang, which
 * is what keeps the client's `called` at zero.
 *
 * `replayable` is the one answer to whether a replay can happen: the trampoline stands and the
 * cell resolved. `replay` answers false, and plays nothing, when either is missing or the
 * replica's position does not read; it never plays without an anchor. */
bool mp_sound_block_replayable(void);
bool mp_sound_block_replay(uintptr_t actor, int32_t kind);

void mp_sound_report(void);

#endif /* MULTIPLAYER_MP_SOUND_H */
