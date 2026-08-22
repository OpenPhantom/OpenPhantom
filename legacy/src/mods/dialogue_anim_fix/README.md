# dialogue_anim_fix

**Produces:** `dialogue_anim_fix.dll` -> `mods\`

Level 6, Mos Espa, the opening in-engine cutscene: Obi-Wan and Qui-Gon talk, and Obi-Wan's head
keeps moving as if he were still talking during Qui-Gon's own line. Scoped to exactly that one
conversation, on purpose; see "Why this narrow" below.

## Supported executables

Retail `WMAIN.EXE`. Every site resolves by pattern; if any of the four does not match, that piece
stays off and the log says so. The animation trigger and `campaign_loadLevel` are required for the
fix to do anything at all; either dialogue-trigger site alone is enough to catch this conversation,
since it uses opcode `0x504` "Statement", not `0x500` "Dialog Box".

## Configuration: `[dialogue_anim_fix]`

| Key | Default | Range | Meaning |
|---|---|---|---|
| `Enabled` | `1` | | |
| `HoldSeconds` | `3.0` | 0.5-30.0 | how long with neither actor speaking before the fix disarms itself for the rest of the level |

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| `campaign_loadLevel` | `0x0043F70A` | detoured; arms only when the loaded path contains `espa.b3d` |
| opcode `0x500` "Dialog Box" | `0x004358B0` | detoured; names an actor starting a line |
| opcode `0x504` "Statement" | `0x00435A0A` | detoured; names an actor starting a line (the one this scene actually uses) |
| `FUN_0042E3AD`, the primary-animation debounce/trigger | `0x0042E3AD` | resolved but never detoured, only called |

## What is actually broken

The dialogue system itself is clean: the single global "who is speaking" cell
(`Dialog_SpeakSingle`, `0x00430D12`) latches and clears correctly for every line, with no stale
value and no skipped switch. The head motion is not driven by dialogue state at all. It is a
separate animation channel, script opcode `0x202` "Animation" (the FSM interpreter's own case for
it, inside `0x00433D0B`):

```
case 0x202:
  actor+0x1C0 = local_c[1];         <- ALWAYS rewritten, every time this node is visited
  if (actor+0x1BC != actor+0x1C0) { ... }
  local_1c = FUN_0042E3AD(actor, duration);
  break;
```

`FUN_0042E3AD` only calls the real trigger (`FUN_0041263F`, "SetPrimaryAnim") when `actor+0x1C0`
and `actor+0x1BC` differ, then latches `actor+0x1BC` to match. A live capture across the whole
exchange, made with a diagnostics build that watched both actors' internal state frame by frame,
shows the reported shape exactly: Obi-Wan's `actor+0x1C0` sits at his talk animation id for the
entire time Qui-Gon is speaking, only changing right before Obi-Wan's own next line.

The critical detail, learned from a first attempt at this fix that had no visible effect at all:
the FSM interpreter does not run this case once and move on. It stays parked on this exact node,
frame after frame, for as long as its own return value keeps saying "not done yet" (indefinitely,
for a plain Animation node with no explicit stop condition), and **every visit rewrites
`actor+0x1C0` back to that line's own talk animation id unconditionally.** A one-time correction the
instant Qui-Gon's line starts gets silently overwritten on the very next frame by Obi-Wan's own
still-running node.

## What this does

A per-frame correction while it is armed, and it is only ever armed for this one conversation:

1. `campaign_loadLevel` names the level file being loaded. Arming requires the path to contain
   `espa.b3d`. Any other level disarms and forgets everything that was being watched.
2. Even while armed, an actor is only ever watched if their own body resolves (through the same
   body -> `rdThing` -> `model3` name-string chain the diagnostics build used to first isolate this)
   to a name starting `obinpc` or `pquigon`. No other actor in Mos Espa, dialogue or not, is ever
   touched.
3. Once armed and watching, an actor who is not the current global speaker and whose own
   talk-animation target is still non-idle is switched to idle through `FUN_0042E3AD` - exactly
   what a correctly authored "Animation: idle" node would do - but only **once** per stale streak,
   not every frame. `actor+0x1BC` is then kept in sync with whatever `actor+0x1C0` the superseded
   actor's own script node keeps rewriting every frame, without calling the trigger again, so their
   own next visit to that node sees no change and does not retrigger anything itself either. The
   idle animation switched to on the first frame is left alone after that, free to keep playing and
   looping normally. This runs late enough in the frame (the shared `render_frameEnd` hook every
   other fix in this project's DLL set already uses) to land after that frame's own FSM tick, so
   the idle pose it forces is the one that actually gets drawn.
4. The moment nobody has actually been speaking for `HoldSeconds` (the same single speaker cell and
   the dialogue-active flag `Dialog_SpeakSingle`'s own timeout handler already clears between
   lines, so no extra bookkeeping is needed), this disarms itself completely: not just released
   until the next line, but off for the rest of this level, until the next `campaign_loadLevel`
   re-arms it. Those two globals blink to "nobody" for a moment between every line of the same
   exchange too, not only at its end, which is why this needs an actual hold timer rather than
   reacting to the first gap it sees.

## Two mistakes already made here, so nobody repeats them

**Correcting every frame by calling the real trigger every frame.** The obvious-looking fix - the
instant a different actor's line starts, force the previous speaker's target to idle and call the
real trigger - has no visible effect, because the superseded actor's own script node rewrites
`actor+0x1C0` right back on the very next frame and the correction was one-shot. Making the
correction run every frame instead, but still calling the real trigger every time, produces a worse
symptom: the superseded actor's own node and this fix's own correction each retrigger a fresh
animation from its own first frame, every single frame, forever, in an endless tug of war - which
reads as the actor freezing solid rather than talking, because neither pose ever gets past its
opening frame. The fix is to trigger for real exactly once and then only keep the engine's own
bookkeeping quietly satisfied afterward, described in step 3 above.

**No expiry on "not the current speaker".** A first working version of the correction above had no
scope at all: it watched every actor who had ever spoken a line, anywhere in the game, and kept
correcting them for the rest of the session whenever they were not the current speaker, which is
true of them forever after their one line. Opcode `0x202` "Animation" is not dialogue-specific - a
level's own script reaches for it for ordinary gameplay animation too - and that unscoped rule was
overwriting *that* the instant it landed on `actor+0x1C0`. The symptom was other, unrelated
characters going completely static well after this cutscene had ended. Arming only for `espa.b3d`
and watching only two specific names, both described above, is the fix: this cannot act on
anything this bug was never about.

## What this does NOT fix

Nothing outside this one conversation. It never arms outside `espa.b3d`, and even there it never
touches an actor whose name does not start `obinpc` or `pquigon`. Any other actor whose talk
animation lingers past their own line, in any other scene, is a different report and would need its
own name (and, if it is in a different level, its own level file) added, or a deliberately more
general version of this fix written and re-scoped with the same care given to this one.

## Testing status: ACCEPTED IN GAME (2026-08-22)

Confirmed live against the Mos Espa opening cutscene: Obi-Wan's head stops the moment Qui-Gon's
line starts and stays stopped, without freezing him solid, and every other actor in the level keeps
animating normally both during and after the exchange.
