# dialogue_anim_fix

**Produces:** `dialogue_anim_fix.dll` -> `mods\`, from `dialogue_anim_fix.c` (the hold),
`idle_clip.c` (the generated idle the jail row rests on) and `speaker_gesture.c` (a speaker
animates for the whole of their line).

Two faults, one narrow and one wide. A character whose script parks on its talking animation and
never leaves it: two scenes are known and the hold acts in exactly those two, on purpose; see "Why
this narrow" below. And a speaker whose gesture plays through part way into a long line and holds
its last frame for the rest of it: every scene does that, and the gesture repeat acts on whoever
holds the speaker lock.

* Level 6, Mos Espa, the opening in-engine cutscene: Obi-Wan and Qui-Gon talk, and Obi-Wan's head
  keeps moving as if he were still talking during Qui-Gon's own line.
* The Theed jail in `queen.b3d`: a prisoner spoken to keeps the talking animation after the
  conversation is over, for the rest of the level (issue 26).

## Supported executables

Retail `WMAIN.EXE`. Every site resolves by pattern; if any of the five does not match, that piece
stays off and the log says so. The animation trigger, `campaign_loadLevel` and `Dialog_SpeakSingle`
are required for the fix to do anything at all; either dialogue-trigger site alone is enough to
catch this conversation, since it uses opcode `0x504` "Statement", not `0x500` "Dialog Box".

## Configuration: `[dialogue_anim_fix]`

| Key | Default | Range | Meaning |
|---|---|---|---|
| `Enabled` | `1` | | |
| `HoldSeconds` | `3.0` | 0.5-30.0 | how long with nobody speaking before the fix disarms itself for the rest of the level, in a scene whose exchange ends (Mos Espa; the jail row never disarms, since the parked node lasts the level) |
| `SpeakerGestureRepeat` | `1` | | a speaker keeps animating for the whole of their line: while a body holds the speaker lock and the voice is still playing, a clip that has played through on it is started again (issue 23). Whoever is speaking, in every scene. `0` leaves a gesture holding its last frame |

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| `campaign_loadLevel` | `0x0043F70A` | detoured; arms only when the loaded path contains a level from the scope table, `espa.b3d` or `queen.b3d` |
| opcode `0x500` "Dialog Box" | `0x004358B0` | detoured; names an actor starting a line |
| opcode `0x504` "Statement" | `0x00435A0A` | detoured; names an actor starting a line (the one this scene actually uses) |
| `FUN_0042E3AD`, the primary-animation debounce/trigger | `0x0042E3AD` | resolved but never detoured, only called |
| `Dialog_SpeakSingle` | `0x00430D12` | resolved, never detoured; the speaker cell and the line-in-progress flag are read out of its operands |

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

A per-frame correction while it is armed, and it is only ever armed in the scenes of the scope
table, which for each one names the level file, the model names of the actors reported, the clip
the actor is put in after their line, and whether the exchange ends:

1. `campaign_loadLevel` names the level file being loaded. Arming requires the path to contain a
   level in the scope table, `espa.b3d` or `queen.b3d`. Any other level disarms and forgets
   everything that was being watched.
2. Even while armed, an actor is only ever watched if their own body resolves (through the same
   body -> `rdThing` -> `model3` name-string chain the diagnostics build used to first isolate this)
   to a name starting with one of that scene's prefixes: `obinpc` or `pquigon` in Mos Espa,
   `nabcit2` in the jail, and, where the row names one, their placement label matches too:
   `enemy031` in the jail, so the other citizens there, some on the same model family, are never
   watched. No other actor in the level, dialogue or not, is ever touched.
3. While a watched actor is the current global speaker, the id their own script keeps asking for
   in `actor+0x1C0` is remembered: that is the clip their line was played with, and the only
   clip ever held off. The frame their line ends, if the script is still asking for that same id
   and the actor is alive (a death cry goes through the same say path, with the die clip asked
   for throughout), they are switched to the scene's rest clip through `FUN_0042E3AD`, exactly
   what a correctly authored "Animation: idle" node would do, **once**. `actor+0x1BC` is then
   kept at the remembered id every frame, without calling the trigger again, so their own next
   visit to the parked node sees no change and does not retrigger anything itself either. This
   runs late enough in the frame (the shared `render_frameEnd` hook every other fix in this
   project's DLL set already uses) to land after that frame's own FSM tick, so the rest pose it
   forces is the one that actually gets drawn.
4. The rest clip is started again each time its track reports complete; the engine's own idle
   mode replays a clip the same way. The stand and talk clips on these models are authored
   as one pass of a few seconds; `FUN_0042E3AD` itself returns 1 on that same flag so a script can
   move on, and a script parked on a talk node never does. Without the replay, clips 0, 3 and 7
   on the prisoner each played once and froze.
5. The hold ends the moment the script asks for anything else, or the engine puts a clip on the
   body by a path of its own (the base clip at `body+0xE8` is checked every frame). `actor+0x1BC`
   still names the held id, so a new request reads as a change to `FUN_0042E3AD` and plays for
   real: a walk, a gesture, or the actor's next line all go through untouched.
6. In a scene whose exchange ends, the moment nobody has actually been speaking for `HoldSeconds`
   (the same single speaker cell and the dialogue-active flag `Dialog_SpeakSingle`'s own timeout
   handler already clears between lines, so no extra bookkeeping is needed), this disarms itself
   completely: not just released until the next line, but off for the rest of this level, until
   the next `campaign_loadLevel` re-arms it. Those two globals blink to "nobody" for a moment
   between every line of the same exchange too, not only at its end, so this needs an actual hold
   timer rather than reacting to the first gap it sees. The jail row stays armed, because the
   parked node keeps asking for as long as the level lasts.

## The generated idle

The jail row rests the prisoner on clip 0, his model's own stand, and `idle_clip.c` writes a
different animation over that clip in memory before he is put in it. A `.baf` model's clips are
keyframe blocks loaded as they are: a header, a node table and per-node entry arrays (frame,
flags, position, rotation and the per-frame deltas) that the puppet interpolates between. The
block is a plain heap image, so a clip is given new entries by pointing its nodes at arrays of
this DLL's own, with the node positions copied out of the clip being replaced and the rotations
authored as slow sums of sines over an eight second loop that closes on itself: weight shifting
between the legs, breathing in the chest, the head looking about, the arms drifting, and no roll
on the trunk, since a first pass swayed him side to side. The clip is flagged the way the model's
own stand is, so it loops. Nothing on disk changes; the model's other clips, its geometry and its
rig are untouched; and the write is idempotent per load. It happens only once he is being held,
so a run in which he is never spoken to changes nothing, and it is the model's clip that changes,
so another character on `nabcit2` idling on clip 0 in that level after that point would show it
too; the shipped stand is a held pose, so nothing visible is lost on them.

Why: every clip he was shipped with either freezes or reads as talking once the stuck talk clip
is taken away, and a man waiting in a cell should look like one.

## Four mistakes already made here, so nobody repeats them

**Correcting every frame by calling the real trigger every frame.** The obvious-looking fix,
forcing the previous speaker's target to idle and calling the real trigger the instant a different
actor's line starts, has no visible effect, because the superseded actor's own script node rewrites
`actor+0x1C0` right back on the very next frame and the correction was one-shot. Making the
correction run every frame instead, but still calling the real trigger every time, produces a worse
symptom: the superseded actor's own node and this fix's own correction each retrigger a fresh
animation from its own first frame, every single frame, forever, in an endless tug of war, which
reads as the actor freezing solid rather than talking, because neither pose ever gets past its
opening frame. The fix is to trigger for real exactly once and then only keep the engine's own
bookkeeping quietly satisfied afterward, described in step 3 above.

**No expiry on "not the current speaker".** A first working version of the correction above had no
scope at all: it watched every actor who had ever spoken a line, anywhere in the game, and kept
correcting them for the rest of the session whenever they were not the current speaker, which is
true of them forever after their one line. Opcode `0x202` "Animation" is not dialogue-specific,
since a level's own script reaches for it for ordinary gameplay animation too, and that unscoped
rule was overwriting *that* the instant it landed on `actor+0x1C0`. The symptom was other, unrelated
characters going completely static well after this cutscene had ended. Arming only for the
scenes in the table and watching only the names each one lists is the fix: this cannot act on
anything this bug was never about.

**Holding off every non-idle id.** The first jail build kept the Mos Espa rule, any tracked actor
who is not the current speaker and whose target is not idle gets idled, and idled the prisoner's
run between his two lines, which was "when he's running he has no animation". A script asks for
ordinary clips between lines too. Only the id seen during the actor's own line is held, and only
while the script keeps asking for exactly that, described in steps 3 and 5 above. The same build
also disarmed three seconds after the second line, while the parked node was the thing that
needed holding. A row now says whether its exchange ends.

**Holding a dead man.** The prisoner's death cry goes through the same say path as a line, with
the die clip asked for throughout, so the first hold with the idle in place saw "a line by this
actor, played with clip 5, still asked for after it ended" and stood him back up out of his own
death. The census caught it: `ai=4 anim=5/5 body=5 hp=-7`. A hold never begins on an actor whose
health is below 1.

## The speaker who stops moving part way through a line

Traced with the player's body clips in the diagnostics log through a scene with four lines. The
scene puts a short clip on the speaker's body when their line starts: a stand fidget of 2.9
seconds (`stnd-no2`), a cutscene gesture of 1.7 (`cutscn3`), read by name and length out of the
player model's own clip table. Each is authored as one pass, its track reports complete two or
three seconds in, and the body holds the last frame for the rest of the line; a line of seven
seconds is spoken standing still from a third of the way in. Between lines the plain stand loops
as authored, arms down, which is the "freeze" of the report. The scene actors' bodies do the same
with their own gesture clips.

`speaker_gesture.c` follows the conversation's speaker lock, the body a line is credited to, and
once a frame while the voice channel is live starts that body's base clip again whenever its
track reports complete, with the crossfade the script interpreter itself uses. That covers every
speaker without naming any: a first version followed the player's own body and matched nothing,
because the player character's lines in a scene are spoken through a scene actor of their own. A
script waiting on the gesture's completion is delayed until the voice ends, which is the pacing
the scene already has. The listener's stand between lines is left as authored: it loops, and
inventing fidgets for it would be new behaviour.

## What this does NOT fix

Nothing outside the two scenes in the scope table. It never arms in any other level, and even in
those two it never touches an actor whose model name is not one the scene names. Any other actor
whose talk animation lingers past their own line, in any other scene, is a different report: play it
with `[diagnostics] Dialogue=1` and `Characters=1`, which give the level file and the speaker, and
add a row to the table with the model name. The jail row was added exactly that way.

## Testing status: accepted in game (2026-08-22; the jail 2026-09-12; the gesture 2026-09-13)

Confirmed live against the Mos Espa opening cutscene: Obi-Wan's head stops the moment Qui-Gon's
line starts and stays stopped, without freezing him solid, and every other actor in the level keeps
animating normally both during and after the exchange.

The gesture repeat was played in two levels' scenes with lines of seven and eight seconds: every
speaker kept moving for the whole of their line, and the log named each line with the clip and
the number of times it was started again.

The jail was traced before it was added: the prisoner's placement is `enemy031`, his model
`nabcit2.3do`, his script mode goes to 7 on his second bark and stays there for the rest of the
level, which is the parked talk node, and his two lines come through the Statement opcode this
already hooks. The census with its `anim` column read him at clip 6 before he is spoken to, 8
(`nc2talk3`) through both lines, 2 (`nc2run1`) between them, and 8 for good afterwards. His model
carries ten clips, read out of `nabcit2.baf` in `big.lab`: 0 `stnd1`, 1 `walk1`, 2 `run1`,
3 `talk1`, 4 `hit1`, 5 `die1`, 6 `lmout`, 7 `talk2`, 8 `talk3`, 9 `butn1`. Clips 0, 3, 6 and 7
were each tried in the cell and none reads as a man waiting, so the generated idle was written.
Confirmed live: he runs normally between his lines, after the second he settles into the
generated idle and stays in it, he dies properly when struck, and everyone else in the jail is
untouched.
