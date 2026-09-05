# Engine identification

What we actually know, from the binary itself, about which toolchain built this game and which
engine family it descends from. Nothing here is guessed - every claim below traces back to either
a PE header field read directly out of the retail executable, or a symbol name already recovered
independently through this project's own Ghidra work (see `legacy/src/mods/*/README.md` for the
signature-level evidence behind those names).

Starting point / inspiration: [a write-up on decompiling the Spider-Man (2000) movie game](
https://krystalgamer.github.io/open-tobey-september-2025/index.html), which pinned its target's
exact compiler via the PE Rich Header, then used a *different* game built on the same engine
(Kelly Slater's Pro Surfer, with public source) to match functions against. The same two-part
approach - identify the toolchain, then find a same-engine sibling to match against - is what the
rest of this document works through for Phantom Menace.

## 1. The toolchain

Target binary: `GAMEDATA\BIN\WMAIN.EXE`, 829,952 bytes. (Two other builds ship in the same
install - the root `wmain.exe`, 10,417 bytes different, and `obi.exe`, a recompile whose `.text`
differs by 60.4% and whose code section is 0x60 shorter - see `common/signature.h`'s own header
comment. This document covers the `BIN\WMAIN.EXE` build specifically.)

**No PE Rich Header.** The DOS stub is the plain, unmodified default MSVC stub (the standard "This
program cannot be run in DOS mode" message and nothing else), running from offset `0x40` to
`0x80`, where `e_lfanew` points straight at the PE header - there is no room for a Rich Header at
all, not a stripped one. That is consistent with an older linker rather than evidence of tampering:
Rich Headers were not reliably emitted until the VC6/VS98-era toolchain.

**The Optional Header settles it directly**, no XOR-decoding required:

| field | value |
|---|---|
| `MajorLinkerVersion.MinorLinkerVersion` | **5.0** |
| `TimeDateStamp` | `0x371D4700` -> **1999-04-21 03:33:20 UTC** |
| `Machine` | `0x014C` (`IMAGE_FILE_MACHINE_I386`) |
| `NumberOfSections` | 5 |
| Optional Header `Magic` | `0x010B` (PE32) |

Linker version **5.0** means **Visual C++ 5.0 / Visual Studio 97**, not the VC6/VS98 toolchain most
PC titles released around 1999 actually shipped with - this game's engine is a build (or two)
older than that. The embedded `TimeDateStamp` matches the file's own on-disk modification date
(April 21, 1999) exactly, which cross-confirms this is a genuine, unmodified retail link rather
than something re-linked or touched later.

**Cross-confirmed against a sibling module.** `GAMEDATA\BIN\IMUSE.DLL` - LucasArts' own proprietary
audio engine, not third-party middleware - shows the identical signature: no Rich Header, linker
version 5.0, the same bare DOS stub, built `1999-04-04 03:01:14 UTC`, seventeen days before
`WMAIN.EXE`. Two different modules, built over two weeks apart on what looks like the same internal
machine, both consistent - this is the shape of the whole build environment, not an artifact of one
file.

**Where the trail goes cold, for now.** The Optional Header's `LinkerVersion` field is only ever
major.minor (`5.0`); VC5's service packs bumped the *build* number, a resolution that field
structurally cannot hold - only a Rich Header carries that, and this toolchain consistently never
emitted one. The entry point (`0x0049CB80`) disassembles to a completely ordinary MSVC
`WinMainCRTStartup` - SEH frame setup, the command-line quote-parsing loop, `GetStartupInfoA`,
`GetModuleHandleA(NULL)` - a shape shared across VC4.x through VC6 that doesn't by itself
distinguish a service pack. No dynamic CRT is imported either (the runtime is statically linked, so
there is no `MSVCRT*.DLL` version to read off the import table), and there is no PE Debug Directory.
Pinning the exact VC5 SP from here on would need either a genuine reference build compiled with a
known SP to diff the CRT startup bytes against, or a proper `comp.id`-style database entry from
some other binary sharing this exact environment - neither of which turned up in a first search.
"VC++5.0 / VS97, exact service pack unknown" is where this stood until the next lead below.

**A development build turned up a real Rich Header.** A leaked folder of Phantom Menace developer
tools (a local copy, not redistributed here) contains several pre-release executables beyond
retail `WMAIN.EXE` - `obi.exe`, `obiold.exe`, `DLL\OBI.EXE`, `RobsObi.exe`, `bafobi.exe`, and
`netobi.exe`, spanning builds from October 1998 through late April 1999. All of them match the
same signature as retail (linker `5.0`, no Rich Header) - except `netobi.exe`, built 1999-04-25,
four days after retail: linker version **5.10**, and a genuine, decodable Rich Header. Decoded
(XOR key `0x131FE67D`, `DanS` marker present), its entries are:

| ProductID | Build | Use count |
|---|---|---|
| `4` | `8168` | 61 |
| `19` (`0x13`) | `8034` | 8 |

These are real, precise numbers - a much finer fingerprint than "linker 5.0" - but translating them
into a human tool name came up empty against the one public `comp.id` database checked so far
([dishather/richprint](https://github.com/dishather/richprint)): its coverage starts around
product-ID range `0x0100`-`0x010e`, which doesn't reach back to VS97-era product IDs this low.
Worth being honest about scope, too: `netobi.exe` is the odd one out among these builds (everything
else, including retail, shows plain `5.0` with no Rich Header), so this is evidence about *a* build
in the same development window, not confirmed proof of what built the shipped `WMAIN.EXE` itself.
Still, it is the first actual Rich Header found anywhere in this game's build lineage, and a
concrete, unambiguous fingerprint to search against if a lower-range VS97 `comp.id` table ever
turns up.

**A public demo build (`TPMDEMO.EXE`) confirms a toolchain upgrade, not the retail SP.** Built
1999-06-05, six weeks after retail, its linker version is **6.0** - a full major-version jump to
VC++6.0/VS98, not the VC++5.0/VS97 that built retail `WMAIN.EXE`. So it answers a different
question: it pins down *when* LucasArts moved toolchains (sometime between 1999-04-21 and
1999-06-05), not what SP retail itself used. Its own Rich Header decodes cleanly (nine entries,
product IDs 1/6/10/11/12/14/19, various builds) - none of them matched the same `comp.id` database
either, so that lookup remains unresolved for this build too, whether from a genuine gap in that
database's coverage or a limitation in how much of a large reference file a web fetch can reliably
search; this document does not claim to know which. One specific, useful cross-confirmation
survived the search failure regardless: **ProductID 19 / Build 8034** - the exact same entry
`netobi.exe` carries - appears unchanged in this VC6 demo too. Whatever that tool is, it persisted
across LucasArts' VC5-to-VC6 transition rather than being replaced with it.

Also noted in the same folder, purely as a build-history data point: `LABUNDLE.EXE` (their internal
LAB archive-packing tool) is dated February 1997 with linker `4.20` - a VC4.2-era tool still
sitting untouched in their pipeline as of 1999.

**The folder cross-validates what this project already knew.** Its own `wmain.exe` differs from
retail `WMAIN.EXE` by exactly 10,417 bytes - the precise figure `common/signature.h`'s header
comment already cites for "the second build." Its `obi.exe` differs from that `wmain.exe` by
470,717 bytes (57% of the file), consistent with the same comment's "a RECOMPILE, 60.4% of its
`.text` differs." Retail's own install directory ships no file named `obi.exe` at all, so this
folder - not the shipped game - is the real source of that build. Independent confirmation, not new
information, but it means everything else pulled from this folder can be trusted at the same level
as everything already established from the retail binary itself.

The folder also holds `stap.cfg`/`tank.cfg` (named `V_*` vehicle-physics tuning tables) and
`emitter1.txt` (named per-emitter parameters - `accel`, `particle_life`, `emit_rate`,
`emission_time`, `directional_variance`, ...) that line up directly with struct fields
`mods/framerate_fix/particle_clock.c` already reverse-engineered by raw offset alone (`+0xE0`,
`+0x60`, `+0x54`). Real confirmed names for those fields, not yet applied - a good next target for
whoever next touches that file, separate from the toolchain question this document tracks.

## 1a. RESOLVED: the toolchain, by byte-for-byte compilation

Everything above was fingerprint work - real, but circumstantial. This is not. Chip tracked down
actual 1997-era Visual C++ installers and ran the compile/diff loop against retail `WMAIN.EXE`,
producing genuine byte-for-byte compiler proof (the `GAMEDATA\BIN` copy specifically - its MD5,
`7c5af8428c19b17cca09ae3a49bd10ef`, was cross-checked against this document's own analysis and
confirmed as the exact "virgin retail" reference the work targeted; the install's top-level
`WMAIN.EXE` is a separate, patched/no-CD build, MD5 `98666221ffd11e1df1035f4465550095` - keep using
the `GAMEDATA\BIN` copy for any future codegen comparison work).

**The recipe**: `wibo` (a Windows-binary compatibility layer for Linux) running actual VC5 `cl.exe`,
grading output against retail with `reccmp`/`objdiff`-style masked comparison (relocations and
absolute addresses wildcarded, everything else byte-for-byte).

**Confirmed**: Visual C++ 5.0 **RTM** (not SP3, not VC6) is the code-generating compiler. Four
functions compiled from independently-reconstructed C source and matched retail exactly, modulo
relocation bytes:

| Function | VA | Bytes matched | Flags |
|---|---|---|---|
| a small switch/case utility | `0x0040E840` | 41/41 | `/Od /MT` |
| a grid-cell address lookup | `0x00406E22` | 24/24 (4 reloc bytes wildcarded) | `/Od /MT` |
| a plane-axis solver (`bapmap.c`) | `0x0040DCEE` | 376/376 (8 reloc bytes wildcarded) | `/Od /MT` |
| `bapView_UpdateProjection` (FOV/projection update) | `0x0040EF40` | 81/81 (48 reloc bytes wildcarded) | `/O2 /MT` |

The last two are full, real functions with reconstructed source, not toy snippets - the projection
one exposed the actual FOV-controlling globals by address (`focal` at `0x5B6438`, `zFar` at
`0x5B9074`, clip bounds, screen-center cells), a genuine and directly useful find for
`mods/variable_fov` independent of anything else in this document. **`/MT`** (static runtime, no
`MSVCRT.DLL`) is confirmed project-wide, matching what section 1's import-table check already
showed. Per-module optimization is **mixed** - some translation units compiled `/Od`, others
`/O2`/`/Ox` - which any future matching work needs to determine per source file rather than assume
uniformly, the same lesson the LEGO Island decompilation project learned.

**One real nuance, not fully closed**: the compiler (`cl.exe`) matches RTM cleanly, but the linked
`LIBCMT` startup/heap code sits between RTM and VC6 in an instruction-level comparison, closer in
shape to a VS97 SP1 or SP2 revision that touched `wincrt0`/heap init specifically (SP2, September
1997, is the stated best guess) - while library functions elsewhere (`memcpy`, `qsort`, `sprintf`,
`fread`, `fwrite`, `fopen`, `malloc`) match RTM's `LIBCMT` exactly. This is genuinely finer-grained
than "which SP" as a single question: the compiler and the linked library can be, and evidently
are, from different points in the SP timeline. SP1/SP2 installers were download-only and are
confirmed absent from several checked archives (Softlib, multiple MSDN discs); if never found, the
documented fallback is to link RTM's `LIBCMT` and carry the ~1-2 KB of divergent startup/heap code
as original bytes lifted from retail, marked as library code rather than reconstructed - it does
not block matching any of this project's own game logic, which is unaffected either way.

**Also resolved, as a byproduct of the reconstructed source's own embedded assert strings**:
- The engine's internal name is **"Big Ape"** - explaining the `bap`/`bp` prefix on every
  `bapobj_`/`bapmap_`/`bapview_`/`bapdraw_`/`bapsound_`/`bapview_` symbol this project has
  independently recovered by pure signature work all along (section 2 below).
- The project's internal codename is **"obi"** (source root `c:\proj\win95\obi\`) - which is why
  every pre-release executable in the leaked dev-tools folder above is named `obi.exe`,
  `netobi.exe`, `RobsObi.exe`, `OBI.INI`, and so on. That thread from earlier in this document is
  now explained rather than just cross-validated.
- Real (partial) source layout, recovered from assert-string paths: `bp\` (engine core -
  `bapactor.c`, `baplight.c`, `bapmap.c`, `bapobj.c`, `bapsnd3d.c`, `bapview.c`, `bppartic.c`,
  `bpsprite.c`, `features.c`), `game\` (`dialog.c`, `enemy.c`, `footstep.c`, `info.c`, `menu.c`,
  `player.c`, `shot.c`, `status.c`), `std\` (`std3d.c`, `stdcolor.c`), `swift\` (UI/menu layer -
  `stdbitmp.c`, `stdbmp.c`, `swmenu.c`, `swpic.c`), `util\` (`control.c`, `graphics.c`, `list.c`,
  `res.c`).
- The real naming convention is **`file_FunctionName`** (e.g. `main_ProcessCommandLine`,
  `graphics_startMode`, `res_fOpen`, `stdBmp_Write`) - the same shape as the LEC Sith-family code
  section 3 already connects this engine to, now confirmed rather than inferred from a sibling
  project's own convention.

**Not yet done, deliberately**: the reconstructed source files this produced (matched functions
from `bapmap.c` and the `bapview_`-prefixed projection code) currently live outside this
repository. Whether and how to bring byte-matched reconstructed source into the project proper -
and under what documentation/attribution standard - is a real decision this document is not making
unilaterally; it is recorded here as an open question for Chip, not a next step already underway.

## 2. Engine lineage: already-recovered symbol names

Independently of any of the above, a large number of function names already established in this
project's own RE work (via signature-based byte evidence, not guessed) follow a naming convention:

```
rdCamera_BuildProjection   rdCamera_init            rdCamera_new
rdCamera_project           rdCamera_setCanvas       rdCamera_setProjType
rdCamera_updateProjection  rdClipFrustum            rdClip_testSphere
rdMaterial_pageStage       rdMaterial_selectCel     rdMesh_draw
rdModel3_concatHierarchy   rdPuppet_advanceTrack    rdPuppet_buildJointMatrices
rdThing_Draw               rdThing_GetNodeMatrix    rdThing_SetModel

bapmap_closeMover     bapmap_eulerToMatrixT   bapmap_firePlate    bapmap_matMul3
bapmap_openMover      bapmap_polyToWorld      bapmap_setWorldClock
bapmap_tickMover      bapmap_waterWave
bapobj_animSlot       bapobj_collidePairs     bapobj_detachNode
bapobj_drawAll        bapobj_hideMeshesBelow  bapobj_hitNodeSpheresVsCylinder
bapobj_playClip       bapobj_setNodePitch     bapobj_setNodeYaw
```

(A weaker, secondary data point: `dismemberment.c`'s own census of actor model files includes one
named `sithmrc2.baf` - a character asset name referencing "Sith", not a code symbol, so it counts
for much less than the function names above, but it is at least consistent with the same
terminology.)

## 3. OpenJones3D, OpenJKDF2, and the Sith/rdroid family

Three public projects, found while looking into this:

- **[smlu/OpenJones3D](https://github.com/smlu/OpenJones3D)** - a reimplementation (not a literal
  decompile) of the **Jones3D** engine used by *Indiana Jones and the Infernal Machine* (1999),
  built from binary analysis plus recovered debug symbols/strings. It documents Jones3D as "an
  upgraded version of the **Sith** game engine, originally used in games like *Star Wars Jedi
  Knight: Dark Forces II*." As of this writing it is roughly 87% complete (2,526 of 2,892
  functions), with its `rdroid` render module fully reconstructed.
- **[shinyquagsire23/OpenJKDF2](https://github.com/shinyquagsire23/OpenJKDF2)** - a clean-room
  reimplementation of the **original Sith engine** itself, as shipped in *Star Wars Jedi Knight:
  Dark Forces II* (1997) - one step earlier in the lineage than Jones3D. Its own README names
  OpenJones3D as an explicit sister project sharing "the same engine and internal libraries," which
  independently corroborates the Sith -> Jones3D line OpenJones3D itself describes. 56.8% complete
  by code weight (95.9% excluding its largely-unported rasterizer). Partly built from symbols
  recovered out of the Grim Fandango Remaster port - a different, not-yet-explored source of real
  (not reconstructed) internal names, worth a look on its own merits some other time. Says nothing
  about the original JK.EXE's compiler/toolchain either, being a reimplementation rather than a
  byte-matched decompile.
- **[Jones3D-The-Infernal-Engine](https://github.com/Jones3D-The-Infernal-Engine)** - an
  asset/mod-preservation org for the same game. No stated claim about engine lineage or a
  relationship to Phantom Menace.

None of the three mentions Phantom Menace. The `rd*` naming convention above (`rdCamera_`,
`rdModel3_`, `rdPuppet_`, `rdThing_`, ...) is exactly OpenJones3D's own naming for its `rdroid`
module, which is the strongest available circumstantial link: it means Phantom Menace's engine
sits somewhere in the same Sith/rdroid family tree as Dark Forces II, Jedi Knight, and Indiana
Jones and the Infernal Machine - not proof of an identical build, but a real, specific,
independently-arrived-at match rather than a guess.

## 4. A direct structural test: the model node struct

`bapobj_hideMeshesBelow` (`0x00414BD7`, byte evidence in `legacy/src/mods/dismemberment/
dismemberment.c`) recursively walks a 3D model's node tree comparing each node's mesh index
against a target, using three confirmed struct fields: a child count, a first-child pointer, and a
next-sibling pointer.

OpenJones3D's own reconstructed node struct (`rdModel3HNode`, in `Libs/rdroid/Primitives/
rdModel3.h`) has fields in this order: `aName`, `num`, `type`, `meshIdx`, `pParent`,
`numChildren`, `pChild`, `pSibling`, `pivot`, `pos`, `pyr`, `meshOrient`. Converting the fields
around the traversal triplet to byte offsets (32-bit, standard alignment) and comparing directly
against Phantom Menace's own confirmed offsets:

| field | Phantom Menace (confirmed) | OpenJones3D `rdModel3HNode` |
|---|---|---|
| type | `0x48` | - |
| meshIdx | `0x4C` | - |
| *(unaccounted 4 bytes)* | `0x50` | - |
| numChildren | `0x54` | `0x90` |
| pChild | `0x58` | `0x94` |
| pSibling | `0x5C` | `0x98` |

The absolute offsets differ, which is expected (different preceding fields, different build) -
but the **field order matches for six consecutive fields**: type, then meshIdx, then a
pointer-sized gap in our own data landing exactly where OpenJones3D's struct has `pParent`, then
numChildren, pChild, pSibling, in that order, in both. Three fields matching in sequence could be
coincidence; six in a row, including an unexplained gap lining up with a plausible field neither
side had reason to fabricate, is a genuine structural fingerprint rather than two engines
independently converging on the same design.

## Where this leaves us

Not proof that Phantom Menace IS Jones3D, or that a source tree for it exists anywhere - but a
toolchain now pinned to VC++5/VS97 (older than most 1999-era competitors), and real, specific,
independently-derived evidence that the engine is a sibling somewhere in the Sith/rdroid family
rather than something unrelated. Neither of the two public projects found so far mentions Phantom
Menace by name, so this project remains the first place that connection has been written down.

Possible next steps, none started yet:
- Use OpenJones3D's naming as a Rosetta stone to rename the `FUN_xxxxxx` functions and struct
  fields this project has already reverse-engineered, wherever a match looks solid.
- Diff specific function *bodies* (not just struct layout) between the two, the same
  neighbour-matching trick the Spider-Man write-up used against Kelly Slater's Pro Surfer.
- Check whether `obi.exe`'s larger divergence from `WMAIN.EXE` (60.4% of `.text` differs, per
  `common/signature.h`) lines up with anything Jones3D-specific, since a "recompile" is exactly
  the kind of build event that might have pulled in an updated engine version.
