# camera_handback_fix

**Produces:** `camera_handback_fix.dll` -> `mods\`

A conversation takes the camera and never gives it back. The camera stays on that shot for the rest
of the level and nothing the player can reach will bring it back. Found in Otoh Gunga on the run up
the escape route after Jar Jar joins, but nothing about it is specific to that level: it can happen
anywhere a character speaks a line that names a camera group, which is most lines in the game.

It is a fault of the shipped 1999 game.

## Supported executables

Retail `WMAIN.EXE`. Five sites resolve by pattern and all five are required; if any one of them
does not match, the fix stays off and the log says which.

## What is wrong

The engine keeps one flag saying **a script owns the camera**, with a companion cell holding the
region that script forced. While the flag is set, `bapview_updateCam` takes its region index from
the forced cell instead of the region the player is standing in. The flag is not advisory: it
decides which camera the level uses. The save writer copies it into the savegame block too, so a
save taken while it is stuck carries the fault out of the session.

Exactly two functions in the whole image write it, `bapview_overrideOn` and `bapview_overrideOff`.
Seven callers take the camera and six release it, and they do not pair up.

`Dialog_SpeakSingle` takes it for any spoken line that names a camera group. `Dialog_Close` is
supposed to give it back, and this is the bug:

```c
if (Dialog_LeaveInputLock(1) != 0 && g_dlg.choiceCount != 0)
    bapview_overrideOff();
```

`choiceCount` is the number of rows in the choice **menu**, and `Dialog_SpeakSingle` sets it to zero
at the top of every new line. So an ordinary spoken line always closes with the count at zero, and
the camera is never handed back. Nothing else clears the cell, so it survives until the level is
reloaded.

There is a second way the same line fails: `Dialog_LeaveInputLock` returns zero when the lock is
already zero, so for a line that took no input lock at all that branch is unreachable regardless of
the count.

## How it was established

By measurement, not by reading. A census armed on both writers logged every take and release with
the caller that asked for it. Across three field runs:

* every take in the level came from a spoken line, not one from the cutscene opcode, the tripod gun
  or the fall-death camera
* the healthy ones were released a moment later by the dialogue closing or by the cutscene opcode
* the take that broke the camera had no release after it at all, until the level tore down

A fourth run watched `Dialog_Close` itself, reading the flag on the way in and again on the way out,
and reported the choice count as zero. That separates the two halves of the condition and names
the count as the half that refused.

The free look gate in `enhanced_input` reads the same flag and had been reporting it all along:
`gOver 1` with `camera state 0`, meaning the camera object was in ordinary follow while the flag
still claimed a script had it.

## The repair

When a dialogue closes still holding a camera it took itself, the camera is handed back.

**Only the count test is dropped.** The lock half is kept, in the form "nobody above this dialogue
is still holding the input lock". That stops this from stealing a camera a cutscene is
holding: a cutscene takes the lock to level 5, and it is still standing when a dialogue nested
inside it closes. Asking whether the lock is clear *now* also covers the second failure above,
which deleting the count test on its own would have left in place.

**Nothing else's camera is ever touched.** The setter is called from inside `Dialog_SpeakSingle`, so
a take is credited to the dialogue by that function being on the stack when it happens: a detour on
it counts how deep inside a spoken line the thread is, and the setter's hook reads that count. A
take from the cutscene opcode, a menu, the tripod gun or the fall-death camera is remembered as not
ours.

An earlier version read the address control returned to from the setter instead, which is the
engine's only while this module's hook is the outermost link of the detour chain. The loader
installs in name order, so `diagnostics` chains in front of it, and with its camera owner census
on the take was credited to nobody and the fix never fired, in the session somebody was
instrumenting the fault in.

Installed all four detours or none. Without the spoken line nothing knows whose camera it is;
without the setter and the clearer it would think the dialogue still holds one the engine already
gave back; and the close is the only moment it acts at. Any three of them is not a smaller version
of this fix; it is a wrong one.

## Configuration: `[camera_handback_fix]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | off restores the engine's own behaviour, the camera that never comes back included |

## What was tested, and what was not

**The repair was watched working, in the game.** In a logged session all five sites resolved, and
three consecutive lines show the whole mechanism: free look released with the scripted-camera
flag set, this module reported handing the camera back, and free look re-armed on the very next
line with the flag clear. That is the fault and its repair inside three lines of one log. That
session ran the earlier attribution, by return address, with three detours. The spoken-line count
that replaced it was built and run in the game afterwards, and the "closed still holding the
camera and it was LEFT alone" line, which only prints for a take this module credited to the
dialogue, appeared in play; the hand-back itself was not watched again under the new mechanism.

**The original visible symptom was not cured by a direct before-and-after.** It was found on the
run up Otoh Gunga's escape route after Jar Jar joins, and by the time this module worked that
reproduction had stopped happening, because a separate repair in `enhanced_input` had removed
the thing that provoked it: the left stick used to keep walking the player during a conversation,
so it was easy to leave a dialogue in a state it never recovered from. So the leak is confirmed
fixed by observation of the flag, not by re-running the symptom.

That distinction matters, because the leak is real either way. It fired again in the session
above, with the stick already fixed, and the engine did not release the camera on its own. The
two faults were compounding rather than being one fault seen twice.
