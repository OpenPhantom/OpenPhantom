# crt_copy_fix

**Produces:** `crt_copy_fix.dll` -> `mods\`

Repairs an inlined MSVC copy loop that reads four bytes before its source, 40 times over.

## Supported executables

Any build carrying the pattern. It contains **neither an address nor a rel32** (both `rel8`
branches are self-relative), so this is the one patch in the project that is inherently ASLR-proof
and version-agnostic. If the pattern is absent, nothing is changed and the log says so.

## Configuration: `[crt_copy_fix]`

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | |

## The defect

MSVC inlined a hand-written, backwards-running copy loop that **loads before it checks the bound**:

```
0x49222D  8B 0C 06   mov ecx,[esi+eax]     ; loads before it checks the bound
0x492230  7D EA      jge 0x49221C
```

It reads `[-4 .. N-4]` and writes `[0 .. N-4]`. On the first row that read is `pixels - 4`.

On heap memory this never shows, because `mem_alloc` puts a `0x10`-byte header in front. It becomes
fatal exactly when the source is a locked DirectDraw surface whose preceding page is not mapped:

```
ACCESS_VIOLATION at 0049222D, READ at 09BEEFFC
esi=09BEEFFC  ->  row start 09BEF000, page aligned
```

And because the copy dies on the **first** row, the menu backdrop was never filled in any of the
four observed cases. The frozen pause background is therefore probably garbage in the retail state
as well; this repairs a picture, not only a crash.

## The replacement

Not an insertion: the whole loop is replaced. 25 bytes are available, 11 are needed. The result
reads `[0 .. N-4]`, writes `[0 .. N-4]`, runs `N/4` iterations and descends in steps of four,**the same order as the original**, which matters because the backwards direction is what makes
overlapping ranges safe. The only difference is the removed load at -4.

## Why the site is deliberately not unique

The pattern occurs 40 times and all 40 are the same defect, so a uniqueness requirement would refuse
every one of them. This scans for all matches with the same strictness instead: the 25 bytes about
to be replaced are read back and compared before each write. A count other than 40 is reported as a
warning, but each individual replacement is still verified.

Idempotent: a second run no longer finds the rewritten sequence.

## Testing status

Built and linked, `/W4 /WX` clean. Offline verification confirms exactly 40 matches on all four
retail executables. **Not accepted in game.**
