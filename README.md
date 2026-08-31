# OpenPhantom - Star Wars Episode I: The Phantom Menace PC fixes and modding tools

OpenPhantom makes the 1999 PC version of *Star Wars Episode I: The Phantom Menace* run properly on
modern hardware: widescreen resolutions, uncapped frame rate, a corrected field of view, mouse
look, working music and video, and compatibility with 64-bit Windows. It is also a reverse
engineering and preservation project, rebuilding the engine as documented, maintainable source
with the tools to edit and extend it.

**[Download the latest release](https://github.com/OpenPhantom/OpenPhantom/releases/tag/i1.3)** · [Installation guide](https://github.com/OpenPhantom/OpenPhantom/wiki/Installation-Guide) · [Discord](https://discord.gg/73UbZN2y7x)

The project has two halves that will run side by side for a long time. One patches the retail
executable so the game you already own runs properly today. The other reconstructs the engine from
scratch. The first is useful now; the second is where it ends up.

## What is here right now

| | |
|---|---|
| [`legacy/`](legacy/) | **Working.** Fixes that patch the original 1999 executable in memory: field of view, resolutions, frame rate, mouse look, music, decals and more. A loader and one DLL per feature, built with CMake. See its [README](legacy/README.md) |
| [`installer/`](installer/) | **Working.** A wizard that installs the game from your own disc and then the parts of the patch you tick. It carries no game data: the disc archive is expanded on your machine. Everything it installs ships inside it and nothing is downloaded, so an installation does not depend on somebody else's hosting still being there. See its [README](installer/README.md) |
| `engine/` | The reimplementation. Not started |
| `editor/` | Tools for maps, assets and game content. Not started |
| `architecture/` | How the original engine is put together, written down. Not started |

If you want to play the game with the fixes, go to [`legacy/`](legacy/) and read the installation
section, or take a release archive from the [releases page](../../releases).

## Documentation

Full guides for installation, usage and configuration are in the
[wiki](https://github.com/OpenPhantom/OpenPhantom/wiki):

* [Installation Guide](https://github.com/OpenPhantom/OpenPhantom/wiki/Installation-Guide)
* [In game settings](https://github.com/OpenPhantom/OpenPhantom/wiki/In-Game-Settings)
* [Currently working on, with spoilers](https://github.com/OpenPhantom/OpenPhantom/wiki/Currently-Working-On-Spoilers)
* [Known Issues](https://github.com/OpenPhantom/OpenPhantom/wiki/Known-Issues)
* [Reporting Issues with the OpenPhantom Patches](https://github.com/OpenPhantom/OpenPhantom/wiki/Issues)

## Goals

* Reconstruct the original engine as clean, maintainable source.
* Keep the original gameplay behaviour. Where the 1999 look or feel was deliberate, it stays.
* Run on modern systems, including Linux and 64 bit builds.
* Improve stability, performance and extensibility without changing how the game plays.
* Provide real tools for editing maps, assets and content.
* Support community modding, and stay open to contributions.

## Getting involved

This is a community project and it moves through people digging into details nobody has looked at
yet. Code, documentation, tests, tooling, research notes and good bug reports are all worth
having, and you do not need to know the engine to start.

[`CONTRIBUTING.md`](CONTRIBUTING.md) has how we work: how to report something usefully, what a
pull request needs, and the ground rules for working against a binary you do not have the source
to. Each component adds its own rules on top; `legacy/CONTRIBUTING.md` is the one that exists so
far.

The most useful thing right now is testing. Every fix here has been played with in a full
installation, and several have been confirmed against the specific behaviour they change; each
fix has its own README and that is where the claim for it lives. But played on a couple of
machines is not works everywhere, so a report saying what happened on yours, with
`engine_fixes.log` attached, is worth more than it sounds. Every fix names the build it was
linked from on its own line in that log, which usually answers the first question a report
raises.

## Discord

The Phantom Discord is where the community talks: players, modders and developers sharing videos,
mods and ideas. As OpenPhantom grows it is also where development updates, custom content and
tools land.

**https://discord.gg/73UbZN2y7x**

## You need your own copy of the game

OpenPhantom ships no game data. No assets, no executables, no extracted files, nothing
proprietary, in the repository or in a release. Everything here operates on files you already own,
and you need a legally obtained copy of *Star Wars Episode I: The Phantom Menace* for PC to use
any of it.

## Legal

An independent fan project. Not affiliated with, endorsed by or sponsored by LucasArts, Lucasfilm
Ltd or Disney. All trademarks, game titles and related intellectual property belong to their
respective owners. OpenPhantom claims no ownership of any original game content and redistributes
none of it.

## Licence

MIT. The source here is free to use, modify and distribute under those terms, provided the licence
notice travels with it. See [LICENSE](LICENSE).

The licence covers this source code only, and not the third-party binaries the installer carries
in `installer/dist/`, which are each under their own terms and include GPL and proprietary
components. See [installer/THIRD-PARTY-NOTICES.md](installer/THIRD-PARTY-NOTICES.md), which also
records what a release has to ship alongside the installer to satisfy them.

It grants nothing regarding the original game, its
assets, or anything else its rights holders own. Release archives may also contain third party
components under their own licences, which are named in the release notes.

## Contact

General enquiries and feedback: **openphantom@proton.me**

For anything technical, an issue on this repository will reach more people and stays searchable.
