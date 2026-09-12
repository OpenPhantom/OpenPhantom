# Engine identification

Which toolchain built the retail executable, and which engine family it belongs to. Every claim
here rests on one of three things: a field read out of the retail binary, a symbol name this
project has already recovered by signature work in `legacy/`, or a public source that is linked
where it is used. Which one is said each time.

The method is the one a write-up on decompiling the 2000 Spider-Man game used: pin the compiler
from the PE headers, then find a game built on the same engine to match functions against.

## The binary

`GAMEDATA\BIN\WMAIN.EXE`, 829,952 bytes, MD5 `7c5af8428c19b17cca09ae3a49bd10ef`. The installation
carries two more builds at the same size, the root `wmain.exe` (10,417 bytes different) and
`obi.exe` (a recompile, 60.4 per cent of `.text` different); `legacy/src/common/signature.h` has
the measurements. This note is about the `BIN` copy.

## The toolchain, from the headers

**No Rich Header.** The DOS stub is the plain MSVC stub, the "This program cannot be run in DOS
mode" message alone, from offset `0x40` to `0x80`, where `e_lfanew` points straight at the PE
header. There is no room for a Rich Header, stripped or otherwise. That is what an older linker
produces; the Rich Header arrived with the VC6 era.

The Optional Header:

| field | value |
|---|---|
| `MajorLinkerVersion.MinorLinkerVersion` | 5.0 |
| `TimeDateStamp` | `0x371D4700`, 1999-04-21 03:33:20 UTC |
| `Machine` | `0x014C`, i386 |
| `NumberOfSections` | 5 |
| `Magic` | `0x010B`, PE32 |

Linker 5.0 is Visual C++ 5.0, Visual Studio 97. The stamp matches the file's own modification date
on the disc to the day, so this is the retail link and not something re-linked later.

`GAMEDATA\BIN\IMUSE.DLL`, LucasArts' own audio engine, shows the same shape: no Rich Header, linker
5.0, the same bare stub, built 1999-04-04 03:01:14 UTC, seventeen days before `WMAIN.EXE`. Two
modules a fortnight apart from the same environment.

The headers stop there. The linker version field is major and minor only, the service packs moved
the build number, and this toolchain never wrote a Rich Header to carry it. The entry point at
`0x0049CB80` is an ordinary `WinMainCRTStartup`, the same shape from VC4 to VC6. The C runtime is
statically linked, so there is no `MSVCRT*.DLL` version to read off the import table, and there is
no debug directory.

The public demo, `TPMDEMO.EXE`, was built 1999-06-05 with linker 6.0, so LucasArts moved to VC6
between the retail link and the demo. That dates the move; it says nothing about which service
pack built retail.

## The toolchain, by compiling

The headers give a fingerprint. Compiling gives proof, and it has been done: Visual C++ 5.0 RTM,
`cl.exe` version 11.00.7022, run against the retail image with relocations and absolute addresses
masked and everything else compared byte for byte.

| function | address | bytes matched | flags |
|---|---|---|---|
| a small switch utility | `0x0040E840` | 41 of 41 | `/Od /MT` |
| a grid cell address lookup | `0x00406E22` | 24 of 24, 4 relocation bytes masked | `/Od /MT` |
| a plane axis solver from `bapmap.c` | `0x0040DCEE` | 376 of 376, 8 masked | `/Od /MT` |
| `bapview_UpdateProjection` | `0x0040EF40` | 81 of 81, 48 masked | `/O2 /MT` |

The projection function exposes the field of view globals by address, `focal` at `0x5B6438` and
`zFar` at `0x5B9074` among them, which `legacy/src/mods/variable_fov` reaches by signature.

What the matches establish: the compiler is VC5 RTM, the runtime is static (`/MT`), and the
optimisation level differs per translation unit, some `/Od` and some `/O2`, so any matching work
has to settle it per source file rather than assume one setting.

What they leave open: the linked `LIBCMT` startup and heap code sits between RTM and VC6 in an
instruction level comparison, nearer a VS97 service pack revision of `wincrt0`, while the library
functions elsewhere (`memcpy`, `qsort`, `sprintf`, `fread`, `fwrite`, `fopen`, `malloc`) match
RTM's `LIBCMT` exactly. The compiler and the linked library can come from different points in
the service pack line, and here they appear to. The service pack installers were download only
and have not been found in any archive checked. If they never are, the fallback is to link RTM's
`LIBCMT` and carry the divergent startup and heap code as original bytes marked as library code;
it blocks no game logic.

## What the binary says about its own source

The assert strings in the retail image carry file paths, and from them:

* the engine's internal name is "Big Ape", the `bap` and `bp` prefix on every `bapobj_`,
  `bapmap_`, `bapview_`, `bapdraw_` and `bapsound_` symbol this project has recovered;
* the project's codename is "obi", the name of the folder the source tree is rooted in, which is
  also why the settings file is `OBI.INI` and the recompile is `obi.exe`;
* the layout under that root, as far as the strings reach: `bp\` for the engine core
  (`bapactor.c`, `baplight.c`, `bapmap.c`, `bapobj.c`, `bapsnd3d.c`, `bapview.c`, `bppartic.c`,
  `bpsprite.c`, `features.c`), `game\` (`dialog.c`, `enemy.c`, `footstep.c`, `info.c`, `menu.c`,
  `player.c`, `shot.c`, `status.c`), `std\` (`std3d.c`, `stdcolor.c`), `swift\` for the menus
  (`stdbitmp.c`, `stdbmp.c`, `swmenu.c`, `swpic.c`) and `util\` (`control.c`, `graphics.c`,
  `list.c`, `res.c`);
* the naming convention is `file_FunctionName`: `main_ProcessCommandLine`, `graphics_startMode`,
  `res_fOpen`, `stdBmp_Write`.

The source files reconstructed by the matching above live outside this repository. Whether and
how byte matched source comes into the tree, and under what attribution, has not been decided.

## The engine family

Independently of the toolchain, the names this project recovered by signature follow a convention:

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

The `rd*` names are the naming of the `rdroid` render module in
[OpenJones3D](https://github.com/smlu/OpenJones3D), a reimplementation of the Jones3D engine of
Indiana Jones and the Infernal Machine (1999), which its own documentation describes as an
upgraded Sith engine, the engine of Jedi Knight: Dark Forces II (1997).
[OpenJKDF2](https://github.com/shinyquagsire23/OpenJKDF2) reimplements that original Sith engine
and names OpenJones3D as a sister project sharing the same libraries. Neither mentions this game.
A weaker point in the same direction: the actor census in `legacy/src/mods/dismemberment` lists a
model named `sithmrc2.baf`.

One structural test. `bapobj_hideMeshesBelow` at `0x00414BD7` (the byte evidence is in
`legacy/src/mods/dismemberment/dismemberment.c`) walks a model's node tree through three
confirmed fields: a child count, a first child pointer and a next sibling pointer. OpenJones3D's
`rdModel3HNode` lays its fields out as `aName`, `num`, `type`, `meshIdx`, `pParent`,
`numChildren`, `pChild`, `pSibling`, `pivot`, `pos`, `pyr`, `meshOrient`. Against this game's
confirmed offsets:

| field | this game | OpenJones3D |
|---|---|---|
| type | `0x48` | earlier in the record |
| meshIdx | `0x4C` | earlier in the record |
| unaccounted four bytes | `0x50` | `pParent` sits here |
| numChildren | `0x54` | `0x90` |
| pChild | `0x58` | `0x94` |
| pSibling | `0x5C` | `0x98` |

The absolute offsets differ, as they would between builds with different preceding fields. The
order matches for six consecutive fields, including a gap on this side that lands exactly where
the other has a parent pointer. Three in a row could be coincidence; six, with the gap, is a
shared ancestor.

## Where this leaves it

The toolchain is VC5 RTM with a static runtime, proven by compilation. The engine is a sibling in
the Sith and rdroid family, with the naming and one structure to show for it; neither public
project mentions this game, so this is the first place the connection is written down.

Possible next steps, none started:

* use OpenJones3D's naming to rename the `FUN_xxxxxx` functions and fields this project has
  already reverse engineered, where the match is solid;
* diff function bodies between the two, not only structure layouts;
* check whether `obi.exe`'s larger divergence from `WMAIN.EXE` lines up with anything Jones3D
  specific, since a recompile is where an updated engine would show.
