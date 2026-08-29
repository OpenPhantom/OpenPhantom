# sound_lifetime_fix

**Produces:** `sound_lifetime_fix.dll` -> `mods\`

Stops a pinned voice keeping the address of a local belonging to a function that has returned.

## The symptom

Save while enemy droids have blaster bolts in flight, then load that save, and the game crashes at
the end of the load. The fault is an `EXECUTE` at `FFFFFFFF` reached through the window message
pump, with no engine frame beneath it: the visible call chain is `d3d9.dll`, `USER32.dll` and the
display driver's shader compiler.

Turning sound effects off stops it. Turning the SFX volume slider to zero does not.

That pair is the whole diagnosis in one line. The volume gates no branch anywhere in the engine, it
is handed to Miles and nothing else reads it, so a silent game still allocates every channel and
still runs every code path. The checkbox is different: it makes `bapsound_play` return before it
allocates anything.

## What is actually wrong

`bapsound_play` records the address the caller passed for its channel handle:

```c
g_channel[ch].pOwnerHandle = pHandle;
```

and `bapsound_freeChannel` writes `-1` back through that pointer when the voice ends:

```c
if (c->pOwnerHandle != 0) {
    *c->pOwnerHandle = -1;
    c->pOwnerHandle  = 0;
}
```

That is a sound protocol as long as the handle outlives the voice. Three call sites in the
projectile code pass the address of a stack local instead. `shot_spawn` is the one that runs on
every bolt fired:

```c
u32 handle;
if (sound_play3d(0, g_sfxName[i], &handle, &pShot->pos, sndFlags) >= 0)
    bapsound_pinChannel(handle, &pShot->pos);
```

**`bapsound_pinChannel` exists to detach a voice from its caller, and it does half of that job.** It
copies the position by value, so the channel stops reading the caller's `vec3`. It leaves
`pOwnerHandle` alone:

```
00417826  55 8B EC 51      push ebp / mov ebp,esp / push ecx
          8B 45 08         mov eax,[ebp+8]          the channel index
          C1 E0 07         shl eax,7                the bank stride, 0x80
          05 A0 AE 5B 00   add eax,&g_channel[0]    the channel bank
          83 79 0C 00      cmp dword [ecx+0x0C],0   c->pRef
          0C 20            or  al,0x20              SNDF_STATIC_POS
```

The frame returns, the pointer does not, and when the voice ends the engine writes `FFFFFFFF` into
a frame that has already gone.

Loading a save fires that write for every voice at once: the load broadcasts module message 6, that
reaches `bapsound_removeLevelSounds`, and it stops all twelve channels.

## What was measured

A probe in `diagnostics` read `pOwnerHandle` before the original cleared it and compared it against
the thread's own stack extent. On the save that crashes:

```
STALE OWNER ch 0 "bdlaser2.wav" -> 001AFC2C, in the stack 00130000..001B0000, ABOVE esp by 1644 bytes
STALE OWNER ch 1 "obifire1.wav" -> 001AFB9C, in the stack 00130000..001B0000, ABOVE esp by 1500 bytes
STALE OWNER ch 3 "bdlaser1.wav" -> 001AFC2C, in the stack 00130000..001B0000, ABOVE esp by 1644 bytes
```

All three at the same instant, which is the message 6 burst. All three blaster sounds. All three
above the stack pointer, so every one of them wrote into a frame still in use.

The flags settle which bug it is. Every channel that carried a stack owner handle was flagged
`SNDF_STATIC_POS`, and no channel without that flag did: three for three, no false positives.
`SNDF_STATIC_POS` is the bit `bapsound_pinChannel` sets and nothing else sets, so the dangling
handles are exactly the pinned ones.

## The fix

Clear `pOwnerHandle` where the pin happens, which is what the pin was already trying to do.

Only a handle that points into the calling thread's own stack is cleared. A channel whose owner
lives anywhere else keeps the lifetime protocol it was written for, so this does not have to be
right about call sites nobody has looked at. All three known pin sites pass a stack local, and the
detour reads the channel bank out of the matched operand rather than writing the address down.

## Settings

Section `[sound_lifetime_fix]`.

| key | default | what it does |
| --- | --- | --- |
| `Enabled` | `1` | clear a pinned voice's owner handle when it points into the caller's stack. `0` leaves the engine writing `FFFFFFFF` into dead frames |

## Limitations, and what this does not settle

**The causal chain was not traced instruction by instruction.** The dangling write is real, it is
byte proven, and it lands in live frames at exactly the moment the crash happens. What was never
demonstrated is that one of those particular writes is what produced the faulting instruction about
half a second later. Removing a genuine memory corruption bug and observing that the crash stops is
strong evidence, not a proof.

If the crash survives this, the corruption is somewhere else and the count of detached handles in
the log is still the useful half of the answer.

**Related engine defects this does not touch**, all found while investigating and all real:

* `shot_restore` never re-seats `trailEmitter` to `-1`, so a restored shot comes back carrying `0`
  and `shot_free` then hands `0` to `emitter_destroy`, destroying whatever occupies emitter slot 0.
* `bapsound_loadRef` is idempotent and takes no reference, while `bapsound_freeChannel` frees the
  WAV unconditionally, so the first of several channels sharing a sound frees the buffer out from
  under the others while Miles is still reading it.
* `enemy_delete` never frees `actor->soundHandle`, leaving a channel pointing into a recycled pool
  slot.

Each is a separate fix and none of them is this one.

## Tested

Built Win32 Release, `/W4 /WX` clean. `sound_owner` unit test covers the predicate that decides
whether a handle is touched, including the half open top of the extent and a reversed extent.

Played: see the commit that adds this. The probe results above were taken in game on the save that
reproduces the crash.
