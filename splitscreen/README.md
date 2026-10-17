# Split screen on one machine

Two copies of the game side by side on one monitor, joined as a two player co-op session over
`127.0.0.1`, each reading its own pad. Built from OpenPhantom's multiplayer plus three pieces:

- `multi_instance.dll` lets the second copy start and gives each copy its own joystick;
- `[enhanced_resolution] WindowedLeft` / `WindowedTop` place each window instead of centring both;
- the two scripts here.

## Use

```powershell
# once: player 2's folder is hard links to player 1's, with its own ini, saves and logs
.\setup.ps1 -Player1 'C:\Games\phantom\' -Player2 'C:\Games\phantom-p2'
# each time
.\launch.ps1 -Player1 'C:\Games\phantom\' -Player2 'C:\Games\phantom-p2'
```

`multi_instance.dll` has to be in player 1's `mods\` before `setup.ps1` runs. Setup splits the
primary monitor into two halves, gives player 1 joystick 0 and player 2 joystick 1, and mutes player
2's music and sound so the soundtrack plays once.

In the game: on player 1's window press **M**, **HOST A GAME**, then **ON TO THE LOBBY**. Click player
2's window, press **M**, **JOIN A GAME**, and join `127.0.0.1:27960`. Menus need the mouse and
keyboard, which go to the window that was clicked last; in a level each copy reads its own pad.

If the pads come out swapped, swap the two `Joystick` values in `[multi_instance]`; each copy's
`engine_fixes.log` lists the joysticks it found.
