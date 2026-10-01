# dialogue_menu_fix

**Produces:** `dialogue_menu_fix.dll` -> `mods\`

A conversation that opens, lets you go after the character's first line, says the line again and
only then holds you in the menu. Reported on the soldier held in the palace in the final level,
whose "Queen Amidala, you saved us!" flashes twice (issue 19); the same shape shows on other
talking characters now and then.

## Supported executables

Retail `WMAIN.EXE`. Every site resolves by pattern, and every cell of the conversation record is
read out of the engine's own operands and confirmed from a second place before it is used. If any
of the four sites does not resolve, or the record does not have the shape expected, the fix stays
off with nothing installed and says so.

## Configuration: `[dialogue_menu_fix]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |

## Engine locations

| Site | Retail VA | What |
|---|---|---|
| `Dialog_SpeakSingle` | `0x00430D12` | resolved, never detoured; the speaker lock and the active flag are read out of its operands |
| `Dialog_Render` | `0x00430434` | resolved, never detoured; the row count and the bark channel handle are read out of its operands |
| `Dialog_HoldChannel` | `0x00430DEC` | resolved and called once a frame while the hold is in force; the pacing stamp is read out of its two operands as a check |
| `enemy_isFacingTarget` | `0x00435656` | detoured; the original answers first |
| `render_frameEnd` | `0x0046C139` | the shared per-frame hook every fix in this DLL set uses |

## What is actually broken

Two gates stand in front of a branching conversation's script node, opcode `0x500`, and the
conversation only stays open while that node is visited every simulation step. Each visit refreshes
a pacing stamp through `Dialog_SpeakSingle`: the line's end is pushed out to now plus the longer of
two seconds and the text's length at a twentieth of a second a character. The engine has no clip
length lookup, so that timer follows the string and not the voice. `Dialog_Render` closes the
conversation, and releases the player, the frame the world clock passes the stamp while rows are on
the screen.

**The script's own gate.** The soldier's state, read off the opcode trace, is Chk Global, Check
Death, Mode Jmp, Set Count, Move To, Check For, Range Check, Switch, Dialog Box. The Check For is
mode 6, `Dialog_ActorTalking`, which with voices on answers "is the bark channel live", and the menu
is reached only while it says no. So the tick the menu opens the voice starts, and for the length of
the clip the node is never visited and nothing refreshes the stamp. Short text, a two second stamp,
a voice a little over two seconds: the box closes a few frames before the voice ends, the voice ends,
the gate passes, and the same line starts from the top. A bark from another soldier in between
resets `Dialog_PlayVoice`'s same-line debounce, which is the run that started it three times. A line
whose text outlasts its voice never shows this.

**The dispatcher's gate.** Once the script reaches the node, the dispatcher only runs it while
`enemy_isFacingTarget(actor, 1)` says yes: the player in the actor's field of view, facing him within
45 degrees, within a unit in height and two units on x and y. The rows lock the input to the menu,
so the player cannot walk away, but a heading that drifts out of the 45 degrees skips the node all
the same. A run with the voice hold in place still restarted the line a second after the voice
ended, with the node not visited in between, and the next build's log showed the facing test saying
no for 151 steps inside an open menu.

## What this does

One thing per gate.

Once a frame, while a conversation is active with rows on the screen and its speaker's bark channel
is live, `Dialog_HoldChannel` is called: the engine's own "keep the channel warm while a `0x500` is
being held off", which sets the pacing stamp to at least one second from now. That is what
`ai_runMenu` itself does when it refuses a visit, applied to the visits the script never makes. The
moment the voice ends the script's gate passes and the node refreshes the stamp itself. A voice
channel that never reported done would hold the box with the player in it, so one conversation is
held for at most fifteen seconds, after which the engine closes it as it always did.

`enemy_isFacingTarget` is detoured. The original answers first and its answer stands, with one
exception: when it says no, the second argument is set, a conversation is active with rows, and its
speaker lock is this actor's own body, the answer is yes. That is the one state the test cannot
legitimately end, since the player is already held in this actor's menu. The first visit still needs
the real test to pass, a menu with no rows is not touched, another actor's menu is not touched, and
the moment the conversation closes the test is the original again.

## What this does NOT fix

A conversation that never opens. Both parts act only on a menu that is already open with rows on the
screen. The Coruscant platform droid that does not always start the platform (issue 42) is a
different report until a trace says otherwise.

## Testing status: accepted in game (2026-09-12)

Traced with `[diagnostics] Dialogue=1`, whose opcode observers were written for this: the say
lines, the menu and statement opcodes and the Check For gate, with the full opcode trace at `Fsm=2`
for the one run that named the state. Played against the soldier six times in one session with both
parts in place: held every time, the line said once, and the log showed six voice holds of two to
three seconds and one facing hold of 151 steps.
