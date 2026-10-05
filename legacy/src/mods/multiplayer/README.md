# multiplayer

**Produces:** `multiplayer.dll` in `mods\`

Co-operative play through the campaign for up to four players, set up from the game's own main
menu. One player hosts, and the others play in the host's world with heroes of their own. It runs
over the LAN, or through the relay at `relay.swopenphantom.org` for players who are not on one
network.

## Playing

### Hosting

On the title screen, press M. **PLAYER NAME** sets the name the others see, and the network row
switches between **NETWORK: LAN** and **NETWORK: PUBLIC**, which goes through the relay.

**HOST A GAME** asks for a session name, a password (empty means none) and the number of seats,
two to four with the host included. **Show on the LAN**, or in public **Public list**, lets other
players find the session in their list; without it they need its address or, in public, its code.
**ON TO THE LOBBY** opens the session. A public lobby shows the code, such as `Code: ABCD-EFGH`.

In the lobby, pick **Choose a map** or **Choose a saved game** and set **Friendly fire**.
**START THE GAME** does nothing until every player is ready, and the line under the list says who
is not; press it again once they are.
With a saved game, every player is sent the host's file first and starts beside the host.

### Joining

**JOIN A GAME** lists the sessions of the chosen network. On the LAN you can also type an address
as `a.b.c.d:port` and keep it with **Remember**; in public you can type a session's code. **Join**
connects and asks for the password when the host wants one. In the lobby, pick a hero and press
**I am ready**.

To join a session that is already being played, do the same: the lobby says
`The session is on. Pick a hero, then READY.`, and after **I am ready** the level loads and you
arrive beside the host, or beside the nearest player standing while the host is down.

### In the level

* **T** opens the chat. Enter sends the line, Escape closes it. While you type, your body stands
  still and the game goes on.
* The pause menu does not stop the game for the others, and you can be hit while it is open.
* Only the host talks to other characters. On a client the use key starts no conversation; what
  the host is told is heard and read by players standing near enough.
* A cutscene is the host's alone. The other players go on playing; when one of them sets a
  scene off, the host is brought there and the scene waits for him.
* When the level itself moves the host to another place, as the assault on the palace does each
  time it changes the hero, the other players are brought beside him and keep their own heroes.
* The developer menu has two buttons under **Multiplayer**, for the player who presses them:
  **Repair lock** gives you your controls and camera back when a scene still holds them, and
  **Teleport to host** puts a client beside the host.
* A player who is killed comes back beside a player who is still standing. When everybody is down,
  the host decides how the level goes on.
* When the host leaves, the session ends for everybody. When the last other player leaves, the
  host keeps playing.

## What every player needs

* **The same release.** The host compares `multiplayer.dll` (the exact build), `damage.txt` and
  `characters.ini`, and refuses a player whose copy differs, naming the file in the lobby.
* **No other DLLs in `mods\`.** A session is played with this release's DLLs only. With any other
  DLL there, a machine cannot host (`Hosting blocked by` and its name), and a host refuses a player
  who has one (`Refused by the host:` and its name). The host can let DLLs in by name with
  `[multiplayer] AllowMods`; a player's own list counts only when that player hosts.
* **The level the host plays.** The shipped levels are in every installation; a custom map has to
  be on every machine.
* **The network.** On the LAN, UDP port 27960 (or the port set on the host screen) and 27961, on
  which a host shows itself. In public, UDP to the relay on port 27970, and HTTPS to fetch the
  relay's key, which is kept in `%LOCALAPPDATA%\OpenPhantom\relay-state.ini`. The connection to the
  relay is encrypted; the LAN is not.

## What the host decides

For the length of a session a client plays with the host's draw distance and fog band
(`[view_distance_fix] ViewRangeScale`, `FogBandScale`, `AuthoredFogBand`), the host's
dismemberment mode (`[dismemberment] Mode`), the host's difficulty and the host's happy and evil
force cheats. The 60fps cheat is off for everybody. None of it is written into a player's
`engine_fixes.ini`, and after the session each machine has its own settings back. On a client the
developer menu shows the host's value on the rows it locks during a session.

When a player's hero changes during a session, the player keeps quest items, keys, health and the
ammunition every hero can use. Single player is unchanged.

## Configuration: `[multiplayer]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `0` in the code, `1` shipped | the multiplayer on or off. An ini from before the multiplayer has no line and leaves the game alone; the shipped file turns it on |
| `PlayerName` | `Player` | the name the others see, up to fifteen ASCII characters. The menu writes it |
| `Language` | empty | the language of the multiplayer's own texts: `en`, `de`, `fr`, `it` or `es`. Empty follows the Windows language, and a Windows language outside the five gives English |
| `ChatKey` | `T` | the chat key: a letter, a digit or `F1` to `F12`. M, TAB, F4, F6, F7, F8, F10, F11 and F12 are taken and fall back to T. The developer menu's row **Key that opens the chat** sets it, also during a session |
| `VoiceHearingRadiusFactor` | `4.0` | how far away a spoken line and its subtitle reach a player, 1.0 to 12.5. 4.0 is sixteen units, thirty two for the host during a scene; the lines of his own scene reach him at any distance |
| `NpcCopiesMax` | `16` | NPCs the developer menu's entity spawner may have standing at once in a session, 1 to 128 |
| `NpcCopyCorpseSeconds` | `0` | seconds before the host removes the corpse of such an NPC, up to 1200. 0 leaves it to the game |
| `AllowMods` | empty | DLLs outside this release that a session hosted here accepts, by name, separated by commas. Empty for a normal game |
| `MenuServer`, `SessionName`, `Slots`, `Announce`, `MenuNet`, `ListPublic` | `127.0.0.1:27960`, `Phantom Menace`, `4`, `1`, `lan`, `0` | what the menu last used: the typed address, the session name, the seats, **Show on the LAN**, the network, **Public list**. The menu writes them |
| `MenuMode` | `coop` | the game the host offers; co-op is the only one |
| `FriendlyFire` | `0` | the lobby's **Friendly fire**: 1 lets the players hurt each other |
| `Server0` to `Server7` | none | the servers kept with **Remember** |

Passwords are never written to the file; they last until the game is closed.

## Testing status

Everything here is compiled, and its rules are unit tested where that works without the game.
Played means a session in the game on the retail `WMAIN.EXE` whose log shows the part working, on
the one or two PCs it was played on.

Played:

* hosting and joining over the LAN with two players on two PCs, and through the relay by code with
  two, three and four players; a session with a password;
* the lobby, the host's saved game carried to a joining player, and each player on their own hero;
* the other players moving, animating, changing weapons and using the lightsaber;
* the host's world on every machine: doors, pickups, enemies and their shots, push blocks, quest
  items and keys, spoken lines within earshot, the gas room's green fog, and the host's movie
  taking everybody on to the next level at the host's difficulty;
* being killed and coming back, a dead host's level going on for the others, the pause menu
  leaving the game running, and friendly fire both off and on;
* the other players following the host when the level moves him, and coming back after a death
  by a long fall with the camera behind the player again;
* the chat reaching every player;
* cutscenes as the host's alone, and the host brought to a player who set a scene off;
* the developer menu's **Repair lock** and **Teleport to host**;
* joining a level that is already running, though without the wait for **I am ready**;
* the host ending the session, and the host keeping its level when the last player leaves;
* single player with the DLL enabled, where no second body appears.

Not played yet:

* a respawn that got lost being asked for again, and a respawn waiting out an open developer menu;
* a level that starts with the jump button stuck down, which the engine can leave behind;
* the lobby holding a late player until **I am ready**;
* the host's settings on a client, and the items a player keeps when the hero changes;
* the check of builds and DLLs, `AllowMods`, and the refusals that name a file or a DLL;
* changing the chat key, the chat's flood limit, its written out umlauts and Escape;
* joining from the relay's list instead of by code;
* three or four players over the LAN or on separate PCs: those sessions ran on a single PC;
* what happens when everybody is down at once;
* the multiplayer's texts in any language but German.

For a bug report, attach `engine_fixes.log` from every machine that took part. It ends each level
with the multiplayer's report of what was sent, received and refused.

## Limitations

* Four players at most, and no new host when the host leaves.
* A late player loads the level from its start, or from the lobby's saved game, and is then brought
  up to date with the story, doors, crates and enemies; there is no snapshot of the host's game.
* The chat works in a level, not in the lobby. A line is plain ASCII of up to 120 characters; on a
  Western Windows umlauts are written out as ae, oe and ue, elsewhere they become question marks.
* The DLL check guards against accidents, not against intent. It looks only directly into `mods\`.
* The host is in one cutscene at a time. A scene another player sets off meanwhile plays without
  the host.
* **Repair lock** on the host in the middle of a scene ends that scene by force, and what its
  script would have done afterwards does not happen. In FEDSHIP, MAUL, ASSAULT and FINAL that
  can leave the level without its end.
* A scene of the host's and something another player sets off at the same moment share the
  level's script registers and its one spoken line.
* Team deathmatch is not offered yet.

## For developers

### Keys the shipped file leaves out

Read from `[multiplayer]` as well. Their code default is what the multiplayer plays with, and none
does anything while `Enabled` is 0.

| Key | Default | Meaning |
|---|---|---|
| `LogSites` | `0` | one log line per resolved site and cell |
| `Bootstrap` | `1` | the foothold: two module nodes and one task slot. 0 resolves the tables and hooks nothing |
| `ProvokeDoubleDelivery` | `0` | a third module node with the tail node's procedure, so the census reports a message that never reached the head |
| `PoolProbe` | `0` | take this many object pool slots, 0 to 64, on the first drawn frame and give them back |
| `PoolReserve` | `64` | object pool slots the reserve latch keeps free for the engine, at most 192 |
| `ProvokeFullPool` | `0` | fill the pool once and watch the latch refuse |
| `EnemySuspend` | `0` | stop the AI: 1 on this instance, 2 on the client |
| `SubstepStopwatch` | `0` | time each stage of the substep, reported at a level end |
| `TraceReplicaPlacement` | `14` | the enemy placement the cadence counters follow by name (Qui-Gon in `FEDSHIP`); 0 is off |
| `GameMode` | `coop` | the game a session started by `NetRole` names; the menu names its own |
| `ProvokeBankSwap` | `0` | a full bank swap every substep with a digest check; turns `BankTick` off and what needs it |
| `BodyContact` | `1` | the contact counter and the shot hull |
| `SecondBody` | `1` | the bank machinery far bodies are spawned through |
| `BankTick` | `1` | tick a far body through the player pipeline; a session over a socket turns it off, because a far body is a puppet |
| `BankInput` | `1` | the four input readers answer a far bank from an injected command. Needs `BankTick` |
| `SyntheticSpin` | `0` | bank 1 holds a constant turn. Needs `BankInput` |
| `BankDeath` | `1` | a far body's death is an inert corpse and does not end the level. Needs `BankTick` |
| `ProvokeSecondDeath` | `0` | kill the second body after ten seconds, revive it five seconds later. Needs `BankDeath` |
| `BankHealth` | `1` | a far body's contacts land in its own bank. Needs `BankDeath` |
| `NetBridge`, `NetSpin`, `NetLoss` | `0`, `0`, `10` | a host and a client in one process over an in-memory network losing `NetLoss` per cent, 0 to 90; `NetSpin` has the client author a turn |
| `NetAutoLag` | `1` | the far body's buffer follows the stream; 0 holds it at three ticks, about 94 ms |
| `NetMovers` | `1` | doors, lifts and platforms a player triggers travel to the other machines |
| `NetMoverCheck` | `1` | once a second the movers are compared into the report, and a client pulls the free running ones back into phase; off, they drift apart |
| `NetRole`, `NetPort`, `NetAddress` | `0`, `27960`, `127.0.0.1:27960` | a session without the menu: 1 hosts on `NetPort`, 2 joins `NetAddress`, which needs its port. `OBI_NET_ROLE` (`host`, `client`, `off`) wins over `NetRole`, and while either names a role the menu can neither host nor join |
| `FlashNearUnits` | `20` | in a session, a thermal detonator's flash fills the screen within this many units, beyond that only in view |

The multiplayer also reads `[loader] ModDirectory`, the host settings named above, and
`[view_distance_fix] NpcRangeScale`, which widens the circle of enemies a host sends to each player.

Keys for team deathmatch, not used by co-op and left out of the shipped file:

| Key | Default | Meaning |
|---|---|---|
| `ScoreboardKey` | `TAB` | the key that holds the scoreboard; the chat key falls back to U while this is T |
| `ScoreLimit`, `TimeLimitSeconds`, `Teams`, `BetrayalPenalty`, `SuicidePenalty`, `RespawnTenths` | `25`, `900`, `1`, `-1`, `-1`, `50` | a round's rules, kept by the menu |
| `[dedicated]` `Port`, `Slots`, `Name`, `Password`, `Level`, `Title`, `LevelIndex`, `ScoreLimit`, `TimeLimit`, `Teams`, `FriendlyFire`, `TeamPenalty`, `SuicidePenalty`, `RespawnTenths`, `LogCategories`, `LogRing`, `LogFile` | in the code; the shipped file leaves them out | read by `obi_dedicated.exe`, which the build places beside `dinput.dll` |

### Building and testing

```
cmake -S . -B build -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release -R mp_
```

`-R mp_` runs every multiplayer test except `npc_spawn_session_sim`. To play your own build, copy
the whole `build\dist` to every machine: another machine's `multiplayer.dll` is another build even
from the same source.

### Where to start reading

Every file's head says what it answers. Start with these:

| File | What its head answers |
|---|---|
| `multiplayer.c` | the order the DLL installs in, and where a refused stage stops |
| `mp_config.h` | the keys and the rules between them |
| `mp_session.h` | the handshake, what a join states, leaving and coming back |
| `mp_wire.h` | what a body sends, and what the codec guarantees |
| `mp_bank.h` | the far player banks, and the swap that makes the engine tick one of them |
| `mp_puppet.h` | a far player's body, placed and dressed from samples |
| `mp_mod_manifest_rule.h` | what two machines must hold the same to play together |
| `mp_host_settings.h` | the host's settings, held by a client for the session |

Layers 0 and 1, the transport and the protocol, know no engine address, so most tests run
without the game. Layer 2 binds to the engine, layer 3 is the session and what stands on it.

### Engine locations

Every site is found by a pattern in the `mp_signatures*.c` and `mp_*_sites.c` files, and every
data cell is read out of an operand of such a site. The feature touches the module registry and
the scheduler, the player pointer and the hero block with the lifecycle functions that address it
directly, the player spawn and death, the contact slot, the shot spawn, the input readers, the
world draw, the key handler, the pause key, the use latch, the mover opener and closer, the
activation scan and actor spawn, three script arms, the speak entry, the front end's menus, the
cheat and difficulty cells and one entry of the shot table.

For cutscenes it hulls the script runner, the player lock and its release, the letterbox, the
camera's take and its clearing, and the hero's grab and its put-back. `camera_handback_fix`
detours the camera's take and its clearing as well, and the chaining detour lets both stand.

### The hook on the world draw

A far body's node rotations are drawn once per rendered frame from a hook on `bapobj_drawAll`,
outside any bank window and for the whole draw list. So the body it writes must be named by the
handle the puppet's window captured and by the same handle in that bank's copy of the block, and
must not be the handle the live hero block names, which is the local player's. Any doubt is a
counted refusal. `enhanced_input` detours the same function; the chaining detour and the
resolver's second stage let both stand on it.

### The one write into the module list

One of the foothold's two module nodes is moved from the end of the engine's module list to the
front, so that it hears the backward broadcasts last: after the input latch, and over the game's
own interface. Two of the six writes are easy to leave out, and without either a forward walk of
the list never returns and the game hangs at every level end. The result is walked from both ends
before it is believed.
