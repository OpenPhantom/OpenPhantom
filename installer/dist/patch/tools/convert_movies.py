#!/usr/bin/env python3
"""Convert this game's own Bink movies to MP4, so fmv_player can play them.

A tool, not content: this project ships no game assets, converted or otherwise. It reads the .BIK
files in your own legally owned copy of the game and writes .mp4 files beside them, in a folder of
its own.

THIS IS THE LINUX AND STEAM DECK HALF OF A PAIR. convert_movies.ps1 beside it does the same job on
Windows. That script cannot run here: Wine ships no PowerShell, so under Proton or Lutris the
installer's own conversion step fails and there was, until this file existed, no way to convert the
movies on Linux at all.

BECAUSE THEY ARE TWO IMPLEMENTATIONS OF ONE THING, these have to stay in step, and a change to
either file that does not change the other is a bug:

  * the FFmpeg arguments, including the codec, the preset, the CRF and the filter chain
  * the per-movie height cap that keeps the logo inside a width decoders will accept
  * the output file names, which are the .BIK name lowercased with an .mp4 extension
  * the machine-readable lines -q prints, which the installer parses

FFMPEG. The Windows script carries a pinned FFmpeg and can download one. This does neither. It looks
for a system ffmpeg first, because every distribution has one and the Steam Deck's SteamOS includes
it; failing that it falls back to the ffmpeg.exe the installer put in mods/fmv, run through Wine,
which is present by definition if the game was installed under Wine. If neither is there it says so
rather than guessing.
"""
import argparse
import io
import os
import shutil
import struct
import subprocess
import sys

# The one movie that is a different shape from the other ten. See the note in convert_movies.ps1:
# scale=-2:<height> lets the width follow the source, and LOGO.BIK is 640x272, so at 2160 lines it
# comes out 5082 wide. Most H.264 decoders refuse a frame past 4096: the file encodes, the player
# opens it, and the picture is black.
MAX_ENCODED_WIDTH = 3840

VIDEO_CODEC = "libx264"
CRF = "18"


def find_file(directory, name):
    """Case-insensitive, because a Windows game on a case-sensitive filesystem may hold WMAIN.EXE,
    wmain.exe or Wmain.Exe and whatever installed it decided which."""
    exact = os.path.join(directory, name)
    if os.path.exists(exact):
        return exact
    lowered = name.lower()
    try:
        for entry in os.listdir(directory):
            if entry.lower() == lowered:
                return os.path.join(directory, entry)
    except OSError:
        pass
    return None


def bink_frame_size(path):
    """Bink stores its frame size as two little-endian dwords at offset 20 and 24."""
    try:
        with io.open(path, "rb") as handle:
            header = handle.read(28)
        if len(header) != 28 or header[:3] != b"BIK":
            return None
        width, height = struct.unpack_from("<II", header, 20)
        if not (0 < width <= 16384 and 0 < height <= 16384):
            return None
        return width, height
    except OSError:
        return None


def encode_height(path, requested):
    """The height to actually encode at: the one asked for, unless it would make this film too wide.

    A source whose header cannot be read keeps the requested height, which is what happened before
    the cap existed.
    """
    if requested <= 0:
        return requested
    size = bink_frame_size(path)
    if size is None:
        return requested
    widest = int(MAX_ENCODED_WIDTH * size[1] / size[0])
    if widest >= requested:
        return requested
    capped = (widest // 2) * 2          # H.264 4:2:0 needs both sides even
    return capped if capped >= 2 else requested


def resolve_ffmpeg(game):
    """(argv prefix, description), or (None, reason)."""
    found = shutil.which("ffmpeg")
    if found:
        return [found], found

    bundled = os.path.join(game, "mods", "fmv", "ffmpeg.exe")
    if os.path.isfile(bundled) and shutil.which("wine"):
        return [shutil.which("wine"), bundled], "%s (through wine)" % bundled

    return None, ("no ffmpeg found. Install your distribution's ffmpeg package, or run this from a "
                  "system that has one. On the Steam Deck it is already present.")


def main():
    parser = argparse.ArgumentParser(
        description="Convert the game's Bink movies to MP4 for OpenPhantom's cutscene player.")
    parser.add_argument("game_directory", nargs="?",
                        help="the folder WMAIN.EXE is in; asked for if omitted")
    parser.add_argument("--output", help="where to write (default <game>/movies_hd)")
    parser.add_argument("--height", type=int, default=0,
                        help="scale to this many lines; 0 keeps each film's own size")
    parser.add_argument("--force", action="store_true",
                        help="re-encode films that already have an .mp4")
    parser.add_argument("--quiet", action="store_true",
                        help="never prompt; one machine-readable line per film")
    args = parser.parse_args()

    game = args.game_directory
    if not game:
        if args.quiet:
            raise SystemExit("--quiet needs the game directory, because it must never wait for "
                             "somebody.")
        print("")
        game = input("Where is the game installed? (the folder WMAIN.EXE is in): ").strip()
        game = game.strip('"').strip("'")
    game = os.path.abspath(os.path.expanduser(game))
    if not find_file(game, "WMAIN.EXE"):
        raise SystemExit("There is no WMAIN.EXE in %s, so that is not the game folder." % game)

    source = find_file(game, "GAMEDATA")
    source = find_file(source, "MOVIE") if source else None
    if source is None:
        raise SystemExit("No GAMEDATA/MOVIE folder in %s." % game)

    output = args.output or os.path.join(game, "movies_hd")
    if not os.path.isdir(output):
        os.makedirs(output)

    height = args.height
    if height <= 0 and not args.quiet:
        print("")
        print("What height should the films be, in lines?")
        print("  Enter keeps each film's own size, which is the recommendation: they are 640x405")
        print("  and enlarging them costs disc space rather than showing more.")
        answer = input("Height [keep]: ").strip()
        if answer.isdigit():
            height = int(answer)

    ffmpeg, description = resolve_ffmpeg(game)
    if ffmpeg is None:
        raise SystemExit(description)
    if args.quiet:
        sys.stdout.write("FFMPEG %s\n" % description)
    else:
        print("")
        print("Using %s" % description)

    films = sorted(n for n in os.listdir(source) if n.lower().endswith(".bik"))
    if not films:
        if args.quiet:
            sys.stdout.write("DONE converted=0 skipped=0 failed=0\n")
        else:
            print("No .BIK files in %s" % source)
        return

    converted = skipped = failed = 0
    for name in films:
        bik = os.path.join(source, name)
        base = os.path.splitext(name)[0].lower()
        final = os.path.join(output, base + ".mp4")
        temp = os.path.join(output, base + ".converting.mp4")

        if os.path.isfile(final) and not args.force:
            skipped += 1
            if args.quiet:
                sys.stdout.write("SKIP %s\n" % name)
            else:
                print("  %-14s already converted" % name)
            continue

        if height > 0:
            this_height = encode_height(bik, height)
            if this_height != height and not args.quiet:
                print("  %s: %d lines rather than %d, so it stays inside %d pixels wide"
                      % (name, this_height, height, MAX_ENCODED_WIDTH))
            filters = "scale=-2:%d:flags=lanczos" % this_height
        else:
            # These films are not a size H.264 can encode: ten of the eleven have an odd height.
            # Cropping drops at most one row and one column and leaves every remaining pixel as it
            # was decoded, where scaling would resample the whole frame and padding would bake in a
            # black row.
            filters = "crop=trunc(iw/2)*2:trunc(ih/2)*2"

        command = list(ffmpeg) + ["-y", "-i", bik, "-vf", filters,
                                  "-c:v", VIDEO_CODEC, "-preset", "slow", "-crf", CRF,
                                  "-pix_fmt", "yuv420p", "-profile:v", "high",
                                  "-c:a", "aac", "-b:a", "192k",
                                  "-movflags", "+faststart"]
        if args.quiet:
            command += ["-loglevel", "quiet"]
        command.append(temp)

        if not args.quiet:
            print("  %-14s converting..." % name)
        try:
            code = subprocess.call(command)
        except OSError as error:
            code = -2
            if not args.quiet:
                print("    could not run ffmpeg: %s" % error)

        if code == 0 and os.path.isfile(temp):
            if os.path.isfile(final):
                os.remove(final)
            os.rename(temp, final)
            converted += 1
            if args.quiet:
                sys.stdout.write("OK %s\n" % name)
        else:
            failed += 1
            if os.path.isfile(temp):
                os.remove(temp)
            if args.quiet:
                sys.stdout.write("FAIL %s %d\n" % (name, code if code != 0 else -1))
            else:
                print("    failed (%s)" % ("ffmpeg exit code %d" % code if code != 0
                                           else "ffmpeg reported success but wrote no file"))

    if args.quiet:
        sys.stdout.write("DONE converted=%d skipped=%d failed=%d\n"
                         % (converted, skipped, failed))
    else:
        print("")
        print("%d converted, %d already there, %d failed" % (converted, skipped, failed))
        if converted:
            print("Films are in %s" % output)


if __name__ == "__main__":
    main()
