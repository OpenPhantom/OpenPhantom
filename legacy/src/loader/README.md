# dinput_loader

**Produces:** `dinput.dll`, goes **next to `WMAIN.EXE`**, not into `mods\`.

The loader patches nothing. It is a jumping-off point: it loads every DLL in `mods\` and hands
each one its entry point.

## Supported executables

Any 32-bit PE. The loader itself reads no engine address, so it cannot pick the wrong build.

## When it loads the mods

`dinput.dll` is a static import of `WMAIN.EXE`, so its `DllMain` runs before a single instruction
of the game. Loading the mods there is what must not happen, `LoadLibrary` under the loader lock
is how deadlocks are made. But nothing has to be loaded there. `DllMain` only writes five bytes:

```
save the first 5 bytes of the host's entry point, write `jmp our_stub` over them
the stub:  restore those 5 bytes, load the mods, jump back to the entry point
```

Restoring before jumping back is what makes this safe without decoding an instruction, the entry
point is re-executed from its first byte, so it does not matter that the five bytes may end
mid-instruction. Only one thread exists at that point. The address comes from the PE header
(`AddressOfEntryPoint`), not from a byte pattern, so it cannot be wrong on another build.

**Why not simply `DirectInputCreateA`.** That was the first design and it is too late.
`WMAIN.EXE` imports exactly one function from `DINPUT.dll`, verified in all three engine builds:
the import descriptor names only `DirectInputCreateA`, the IAT slot is `0x008C148C`, and it is
reached through a single thunk at `0x00499220` with exactly one caller at `0x0048D0CF`, whose
result is stored to a local and never tested. It runs outside the loader lock, which is what made
it attractive. But **graphics startup runs before input startup**: by the time it is called,
`graphics_buildModeList` has already filtered the display-mode list, so lifting the 4:3 gate had
no effect and the options screen still offered a single resolution. The proof is in the log, the
old build reported "display mode not set yet" at install time, the new one reported "mode size
1920x1080".

`DirectInputCreateA` is still wired up as a fallback. `mod_loader_run_once()` is idempotent, so
whichever trigger fires first wins.

## Configuration: `[loader]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | `0` loads nothing at all; the game runs exactly as before |
| `ModDirectory` | `mods` | Relative to `WMAIN.EXE` |
| `ChainDll` | *(empty)* | Explicit forward target, absolute or relative to the game folder |

## The chain

We take the `dinput.dll` name, so whatever answered to it before needs a new one. Resolution order,
each step logged:

1. `ChainDll`
2. `<game folder>\dinput_orig.dll`
3. `<system directory>\dinput.dll`

**If your game folder already has a `dinput.dll`**, a graphics wrapper, an ASI loader, rename
it to `dinput_orig.dll` rather than overwriting it. Every export is forwarded to it and it keeps
working.

## Load order

Alphabetical, so the sequence is reproducible rather than dependent on the file system.
**Order encodes no dependencies:** no feature calls into another, and where two of them detour the
same engine function, `common/detour.c` chains them so the result is identical either way.

At most 64 DLLs; beyond that the surplus is skipped and reported.

## Limitations

* A DLL without an `engine_fix_install` export is loaded anyway and noted as such. That is
  deliberate, an ordinary third-party DLL is a legitimate thing to put in `mods\`.
* Feature DLLs are never unloaded. The detour chain holds pointers into them for the life of the
  process, and there is no supported way to remove a link from the middle of that chain.

## Testing status

Built and linked (MSVC x86, `/W4 /WX`, clean). Exports verified against the built PE: all seven
present, PE32. The entry-point stub was disassembled out of the built DLL and is exactly
`pushad / pushfd / call restore_and_load / popfd / popad / jmp dword ptr [saved entry]`.
**Accepted in game.** Every DLL in the patch reaches the process through this loader, so any
played build exercises it by definition, and the 1.5.0 build was played through by hand.
