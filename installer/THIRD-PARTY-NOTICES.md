# Third-party components

Everything under `dist/` except `saves/` is third-party, carried here so the
installer needs no network, during installation or afterwards. `dist/ffmpeg/ffmpeg.exe` is the
exact build `convert_movies.ps1` pins, so it is what the converter would have downloaded on first
use; carrying it is what makes cutscene conversion work offline too. Each is redistributable; this file records what they are, what
they are under, and what shipping them obliges us to do.

The repository's own MIT licence in [../LICENSE](../LICENSE) does **not** cover any of it.

| Component | Version | Licence | Upstream |
|---|---|---|---|
| OpenPhantom patch | v0.2.1 | this project's | https://github.com/OpenPhantom/OpenPhantom |
| VLC / libVLC | 3.0.23 | **GPL v2** | https://www.videolan.org/vlc/ |
| DSOAL | r694 | **LGPL 2.1** | https://github.com/kcat/dsoal |
| OpenAL Soft (inside DSOAL) | as shipped in DSOAL r694 | **LGPL 2** | https://github.com/kcat/openal-soft |
| FFmpeg (gyan.dev `essentials` build) | 9.0 | **GPL v3** (the build bundles x264) | https://github.com/GyanD/codexffmpeg |
| dxwrapper (the `dx7.games` build) | 1.8.8600.25 | see `dist/dxwrapper/dxwrapper-License.txt` | https://github.com/elishacloud/dxwrapper |

`dist/dxwrapper/dxwrapper.ini` is **modified from upstream**. Six settings differ from the
`dx7.games` file dxwrapper ships, and every one of them tunes this game rather than repairing
dxwrapper:

| setting | upstream | here |
|---|---|---|
| `DdrawEmulateSurface` | 0 | 1 |
| `DdrawUseShadowSurface` | 0 | 1 |
| `DdrawEnableByteAlignment` | 0 | 1 |
| `AnisotropicFiltering` | 0 | 16 |
| `DepthBiasFactor` | 0 | 16 |
| `ForceVsyncMode` | 0 | 1 |

Everything else in the file is upstream's, taken from the release being shipped, and the
binaries are unaltered. Those six are the ones to re-apply when the folder is refreshed, and
the way to find them is to diff the shipped file against the `dxwrapper.ini` inside the
release archive rather than to trust this list.

The licence text of every one of these is installed beside the game, and the files are in
`dist/` next to the binaries they cover.

## What has to ship with a release

VLC is under the GPL v2 and DSOAL and OpenAL Soft under the LGPL. Both licences require that
a binary be **accompanied** by its corresponding source. A link to the upstream project is not
enough: fetching source from a network server only became an option in GPL v3, and only FFmpeg
is v3.

That was somebody else's obligation while the installer downloaded these. It is ours now that it
carries them, so **every release that includes the installer must also carry `source/`**, either
inside the same archive or as assets on the same release page.

The archives in `source/` are the corresponding source, pinned to the exact revisions these
binaries were built from, not to the tip of any branch:

| Archive | Revision | How that was established |
|---|---|---|
| `vlc-3.0.23.tar.xz` | 3.0.23 | the version the binaries are from |
| `dsoal-r694-5c65e5fe.tar.gz` | `5c65e5fea13474a8bf346627e2944d28fe0c9cb5` | `DSOAL/Documentation/DSOAL-Version.txt` in the release archive |
| `openal-soft-r10633-2dc741b5.tar.gz` | `2dc741b54a49fc6a7716afd1504ca1056cff7db4` | `DSOAL/Documentation/OpenALSoft-Version.txt`, same archive |
| `ffmpeg-9.0-d32b387f2b.tar.gz` | `d32b387f2b0a484599d4587d651891f0c63c4238` | "Source Code:" line in gyan.dev's `README.txt` |
| `x264-v0.165.3223-0480cb05.tar.gz` | `0480cb05fa188d37ae87e8f4fd8f1aea3711f7ee` | see below |

x264 is the only one that had to be worked out. gyan.dev records it as `v0.165.3223`, which is
x264's own format of API version and commit count rather than a hash. Both halves match that
commit: `X264_BUILD` in `x264.h` is 165, and `git rev-list --count` reaches 3223 there. If x264
is ever rebuilt from a newer FFmpeg, redo that check rather than assuming.

sha256, so a mirrored release can be checked against this file:

    42900afbd94abec8863ba2df53c6767af43a8c2fa2969e212e28b9d13cf39c51  dsoal-r694-5c65e5fe.tar.gz
    8a830a34bfaf98514b5d45cf6c01b1fe78b38d5e4c10eab0de2531b783c15f90  ffmpeg-9.0-d32b387f2b.tar.gz
    058d11133c398d4294e034bfd58dc7899f6ac6ca9349bd8c419dd06ea95ab466  openal-soft-r10633-2dc741b5.tar.gz
    e891cae6aa3ccda69bf94173d5105cbc55c7a7d9b1d21b9b21666e69eff3e7e0  vlc-3.0.23.tar.xz
    4ec57a3e29e6782e34b089a0525f74e8d77455b4d99aef7c3b71c7539832c3cf  x264-v0.165.3223-0480cb05.tar.gz

x264 is there because FFmpeg's "essentials" build links it statically, which is also why that
build is GPL v3 rather than LGPL. FFmpeg being v3 does allow its source to be offered from a
network server instead, but shipping all five together is simpler than tracking which component
gets which treatment.

Nothing here is installed onto anyone's machine. These sit beside the installer for whoever
wants them.

**Mirrors.** Prefer one archive containing the installer and `source/` together over separate
downloads on a page. Releases get re-uploaded elsewhere, and a mirror carrying only the
installer strands whoever takes it from there.

## The binaries themselves

The archives above are the *source*. `THIRD-PARTY-BINARIES.sha256` beside this file records the
sha256 of every third-party binary the installer ships, 37 of them, in the format `sha256sum -c`
reads. From `installer/`:

    sha256sum -c THIRD-PARTY-BINARIES.sha256

This existed as a gap rather than a decision: the source archives have been pinned and hashed
since they were added, and the binaries built from them never were, which is the wrong way round
for anyone asking what they are about to run. A licence file says what the terms are; it does not
say that the DLL beside it is the one the project meant to ship.

Only third-party binaries are listed. The project builds its own DLLs from the source in this
repository and they change with every build, so recording their hashes in a checked-in file would
be stale before it was committed. The point of this manifest is the code that came from somewhere
else, which is exactly the code the repository cannot otherwise account for.

Regenerate it whenever a component is refreshed, in the same commit as the refresh. A manifest
that lags the binaries it describes is worse than none, because it reads as verification.


## Refreshing a component

The version above and the files in `dist/` have to move together, and so does the source
archive on the release. See the comment at the top of each `src/*.iss` for what a given
component's rows expect to find.
