# is3_extract

Reads an InstallShield 3 `.Z` archive and writes one member out.

It exists for one job. The disc carries `GAMEDATA\GOBS\BIG.Z` and the game needs `big.lab`. Nothing
on a current Windows expands that archive and the disc's own installer is 16 bit and no longer runs,
so without this the installer would have to ship 121 MB of the publisher's game data.

## Usage

```
is3_extract --list <archive>
is3_extract <archive> <member> <output-file> [--log <file>]
```

Member names are matched without regard to case, because the archive records `big.lab` while the disc
spells everything else in capitals.

## Exit codes

They are the whole interface for the installer and must not be renumbered.

| code | meaning |
|---|---|
| 0 | a member was written **and** its length matched the archive's own directory |
| 1 | wrong arguments |
| 2 | the archive could not be opened |
| 3 | not an InstallShield 3 archive |
| 4 | the member directory does not parse, or a member points outside the file |
| 5 | no member of that name |
| 6 | the compressed stream is damaged |
| 7 | the output could not be written |
| 8 | the extracted size is not the size the archive records |

Code 0 means the file is *right*, not merely present, and an output that fails the length check is
deleted rather than left where something might pick it up.

## Files

`blast.c` is PKWARE DCL implode decompression, `is3_archive.c` is the container, `main.c` is the
command line and the exit codes. Memory is bounded by the compressed member rather than the expanded
one, so a 121 MB member never exists in memory.

## The format

Measured on a retail disc and cross checked against `INSTALL\_SETUP.LIB`, which is the same format.
Nothing here is taken from a specification.

| offset | width | meaning |
|---|---|---|
| `0x00` | u32 | signature `0x8C655D13` |
| `0x0C` | u16 | number of members |
| `0x12` | u32 | total archive size |
| `0x33` | u32 | file offset of the member directory |
| `0x37` | u32 | length of the member directory |
| `0xFF` | | first member's data |

**The header is 255 bytes, not 256.** The obvious reading is wrong by one, and the directory's own
offsets settle it: on `BIG.Z`, `0xFF + 0x04D39DB1` lands exactly on the directory.

Directory records are variable length and are addressed relative to the record's name length byte,
which is the one position findable by inspection. Writing `p` for it: uncompressed size at `p - 26`,
compressed size at `p - 22`, absolute data offset at `p - 18`, DOS date at `p - 14`, attributes at
`p - 10`, the record's own length at `p - 6`, the name length at `p`, the name after it. Record length
is always name length plus 43, and the next record's name length byte is at `p + record length`.

**The directory offset in the header does not point at the first record's first byte.** It lands 26
bytes in on `BIG.Z` and 29 on `_SETUP.LIB`, so the extractor scans a short bounded window for the
first record whose length agrees with its name length, and refuses the archive if none does.

The payload starts `00 06`: literal mode 0 and a 4096 byte window, which is PKWARE implode. The one
detail easy to get wrong is that **codes are stored inverted, most significant bit first**, while
every other field in the stream is least significant bit first.

## Build

```
cmake -S . -B build -A Win32
cmake --build build --config Release
```

32 bit, `/W4 /WX`, no warnings. Output: `build\Release\is3_extract.exe`.

## Testing status

* **Verified against the retail disc.** `BIG.Z` to `big.lab` produces 120,859,357 bytes with MD5
  `0489beac493747bc8d672201e2fcc23e`, identical to a known good copy, in about a second.
* **Verified against a second archive.** All five members of `_SETUP.LIB` extract to the exact sizes
  recorded.
* **Not tested:** a damaged disc, an archive with more than five members, and any pressing other than
  the German one that was available.

One check looked like a failure and is not. `_isres.dll` from `_SETUP.LIB` differs from the loose
`INSTALL\_ISRES.DLL` in 69 % of its bytes, but both are 107,008 bytes and carry the same PE timestamp
`0x3263F26B`: same build, English resources in the archive and German ones on the disc.
