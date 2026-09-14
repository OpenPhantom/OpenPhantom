# OpenPhantom: Star Wars Episode I, The Phantom Menace PC fixes and modding tools

[![Latest Release](https://img.shields.io/github/v/release/OpenPhantom/OpenPhantom?style=for-the-badge&label=Latest-Release)](https://github.com/OpenPhantom/OpenPhantom/releases/latest)
[![All Downloads](https://img.shields.io/github/downloads/OpenPhantom/OpenPhantom/total?style=for-the-badge&label=Downloads)](https://github.com/OpenPhantom/OpenPhantom/releases)
[![GitHub Stars](https://img.shields.io/github/stars/OpenPhantom/OpenPhantom?style=for-the-badge&label=Stars)](https://github.com/OpenPhantom/OpenPhantom/stargazers)
[![License](https://img.shields.io/github/license/OpenPhantom/OpenPhantom?style=for-the-badge)](https://github.com/OpenPhantom/OpenPhantom/blob/main/LICENSE)

OpenPhantom is a PC fix and modding project for Star Wars Episode I: The Phantom Menace (1999). It gets the original PC game running on Windows 10 and Windows 11, and on Linux and Steam Deck through Proton, with a modern installer that works from your original CD, widescreen and ultrawide resolutions, uncapped FPS, a corrected field of view, mouse look, and working music and video. Alongside the fixes, OpenPhantom reverse engineers and reconstructs the game's engine as documented, maintainable source code, with tools for modding, editing and extending the game.

**[Download the latest release](https://github.com/OpenPhantom/OpenPhantom/releases/latest)** · [Installation guide](https://github.com/OpenPhantom/OpenPhantom/wiki/Installation-Guide) · [Discord](https://discord.gg/73UbZN2y7x)

The project has two parts. The legacy patches are useful today: they modify the retail executable in memory so the game you already own runs with modern fixes. The reconstructed engine is the long-term work: rebuilding the game's systems from the ground up as clean source code that can support deeper modding and new features.

<img width="1920" height="400" alt="Star Wars Episode I: The Phantom Menace running at ultrawide resolution with OpenPhantom." src="https://github.com/user-attachments/assets/ad9cd6fb-8bcd-4b25-8d0a-65e221888bfc" />

*Ultra-wide support with OpenPhantom.*

<img width="1920" height="1080" alt="Star Wars Episode I: The Phantom Menace running in 4K with expanded draw distance using OpenPhantom" src="https://github.com/user-attachments/assets/8ffa0c2f-d9a4-439c-a880-6eb123f29090" />

*4K rendering with expanded draw distance.*

<img width="1920" height="1080" alt="Star Wars Episode 1 The Phantom Menace Screenshot 2026 09 14 - 11 16 27 84" src="https://github.com/user-attachments/assets/59fd4625-e932-47fa-ad1a-18be13ede3e0" />

*Free Camera/ No Clip/ Photo mode.*

**Want to play?** Download the [latest release](https://github.com/OpenPhantom/OpenPhantom/releases/latest) and run the OpenPhantom installer. It installs the original game from your own disc and applies the available patches and fixes for modern Windows PCs. You don't need to build anything from source.

## What's Included  

| | |
|---|---|
| [`installer/`](installer/) | **Working** A wizard that installs the game from your own disc and then the parts of the patch you tick. It carries no game data; the disc is read on your machine. Everything it installs is inside the installer and nothing is downloaded. See its [README](installer/README.md) |
| [`legacy/`](legacy/) | **Working**  Fixes that patch the original 1999 executable in memory: field of view, resolutions, frame rate, mouse look, music, decals and more. A loader and one DLL per feature, built with CMake. See its [README](legacy/README.md) |
| `engine/` | The reimplementation. Not started |
| `editor/` | Tools for maps, assets and game content. Not started |
| `architecture/` | How the original engine is put together, written down. Not started |

## Version numbers

The patch and the installer have **two separate version numbers**. They always did, apart from one
release.

| | numbering | tags |
|---|---|---|
| the patch, `legacy/` | `0.4.x` | `v0.4.0`, `v0.4.1`, ... |
| the installer that carries it | `1.4.x` | `i1.4`, `i1.4.1`, ... |

The last digit moves together on a release. The DLLs carry the **patch** number, so a DLL's
properties and the first line of `engine_fixes.log` say `0.4.3` while the installer that delivered
them says `1.4.3`.

**One release merged the two into a single number**, published as `v1.5.0` and `i1.5.0`. That's
been undone. On GitHub that release is now **`v0.4.1` and `i1.4.1`**, and the one after it is
`v0.4.2` and `i1.4.2`.

**Only the release name changed; nothing was rebuilt.** The installer in that release still reports
`1.5.0` in its properties and in Add and Remove Programs, because it's the same file. If you have it
installed, that's why the number doesn't match the release name.

The installer doesn't compare version numbers. It finds an existing installation by its
application id and asks what you want to do with it.

## Documentation

Full guides for installation, usage and configuration are in the
[wiki](https://github.com/OpenPhantom/OpenPhantom/wiki):

* [Installation Guide](https://github.com/OpenPhantom/OpenPhantom/wiki/Installation-Guide)
* [Currently working on, with spoilers](https://github.com/OpenPhantom/OpenPhantom/wiki/Currently-Working-On-Spoilers)
* [Known Issues](https://github.com/OpenPhantom/OpenPhantom/wiki/Known-Issues)
* [Reporting Issues with the OpenPhantom Patches](https://github.com/OpenPhantom/OpenPhantom/wiki/Issues)

## Goals

* Reconstruct the original engine as clean, maintainable source code.
* Keep the original gameplay. If the 1999 look or feel was intended, it stays.
* Run on modern systems, including Linux and 64 bit builds.
* Improve stability, performance and extensibility without changing how the game plays.
* Provide real tools for editing maps, assets and content.
* Support community modding, and stay open to contributions.

## Getting involved

This is a community project. Code, documentation, tests, tooling, research notes and good bug
reports are all welcome, and you don't need to know the engine to start.

[`CONTRIBUTING.md`](CONTRIBUTING.md) has how we work: how to report something usefully, what a
pull request needs, and the ground rules for working against a binary you do not have the source
to. Each component adds its own rules on top; `legacy/CONTRIBUTING.md` is the one that exists so
far.

The most useful thing right now is testing. Every fix has been played in a full installation, and
each one's README says what was checked. But a couple of machines isn't everywhere, so a report of
what happened on yours, with `engine_fixes.log` attached, helps a lot. The log names the build each
fix came from, which is usually the first thing we need to know.

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

For anything technical, open an issue here; more people will see it and it stays searchable.
