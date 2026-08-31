#!/usr/bin/env python3
"""Upscale this game's own menu artwork so the menus can fill a modern screen.

A tool, not content: this project ships no game assets, converted or otherwise. It reads the
archives in your own legally owned copy of the game and writes bigger copies of the pictures that
are already there, into a folder of their own. big.lab and LOCALIZE.LAB are opened read-only and
never written.

THIS IS THE LINUX AND STEAM DECK HALF OF A PAIR. convert_menu.ps1 beside it does exactly the same
job on Windows, where it can use GDI+ and needs nothing installed. That does not work under Proton:
Wine ships no PowerShell, and System.Drawing.Common is Windows-only on .NET Core, so both halves of
that script are unavailable. This one needs only Python 3 and its standard library, which the Deck
and every mainstream distribution already have.

BECAUSE THEY ARE TWO IMPLEMENTATIONS OF ONE THING, four decisions have to stay in step, and a
change to either file that does not change the other is a bug:

  * the LAB directory format, and the two type tags whose members are pictures
  * nearest neighbour, sampling from pixel centres
  * the output file names, which are the archive member names unchanged
  * the manifest, which is what --remove reads

NEAREST NEIGHBOUR, DELIBERATELY. After the engine converts a bitmap to 16 bit, a pixel that is
exactly zero is a SKIP, i.e. transparent. A smoothing filter invents near-black where black was
transparent, and new exact-zero pixels where there were none, so it would put a halo around every
button and punch holes in dark artwork. Whole pixel replication cannot invent a colour that was not
already in the source, so it is exact on that rule by construction.
"""
import argparse
import io
import os
import struct
import sys

CANVAS_WIDTH = 640
CANVAS_HEIGHT = 480

# The run length encoder writes a literal control word as `run & 0xfff` while advancing the output
# by the whole run, so a canvas wider than 4095 pixels corrupts the stream.
MAX_RATIO = 4095.0 / CANVAS_WIDTH

PICTURE_TAGS = (b'MENU', b'BMPS')
MANIFEST_NAME = 'openphantom_menu_art.txt'
ARCHIVES = ('big.lab', 'LOCALIZE.LAB')


def find_file(directory, name):
    """A case-insensitive lookup, because a Windows game on a case-sensitive filesystem may hold
    BIG.LAB, big.lab or Big.Lab and the installer that put it there decided which."""
    exact = os.path.join(directory, name)
    if os.path.isfile(exact):
        return exact
    lowered = name.lower()
    try:
        for entry in os.listdir(directory):
            if entry.lower() == lowered:
                return os.path.join(directory, entry)
    except OSError:
        pass
    return None


def read_lab_directory(path):
    """Yield (name, data offset, size) for every picture member of a LABN archive.

    16 byte header (magic, version, count, name table size), then one 16 byte record per member
    holding name offset, data offset, size and a BYTE REVERSED type tag, then the name blob, then
    the member data uncompressed.
    """
    with io.open(path, 'rb') as handle:
        magic, _version, count, name_bytes = struct.unpack('<4sIII', handle.read(16))
        if magic != b'LABN':
            raise SystemExit('%s is not a LABN archive' % path)
        records = [struct.unpack('<III4s', handle.read(16)) for _ in range(count)]
        names = handle.read(name_bytes)

    for name_offset, data_offset, size, tag in records:
        if tag[::-1] not in PICTURE_TAGS:
            continue
        end = names.find(b'\0', name_offset)
        if end < 0:
            continue
        name = names[name_offset:end].decode('latin-1')
        if name:
            yield name, data_offset, size


def upscale_bmp(data, ratio_x, ratio_y):
    """Nearest neighbour on a 24 bpp bottom-up BI_RGB bitmap, returning the new file's bytes.

    Rows are built one SOURCE pixel at a time rather than one destination pixel at a time: each
    source pixel becomes a run of however many destination pixels land on it, so a six times scale
    does 640 slice operations per row instead of 3840. A destination row whose source row is the one
    just built is a straight copy of it, which is most of the rows at any real scale.
    """
    if data[:2] != b'BM':
        return None
    pixel_offset, = struct.unpack_from('<I', data, 10)
    width, height, _planes, bpp = struct.unpack_from('<iiHH', data, 18)
    compression, = struct.unpack_from('<I', data, 30)
    if bpp != 24 or compression != 0 or width <= 0 or height <= 0:
        return None

    source_stride = ((width * 3) + 3) & ~3
    target_width = max(1, int(width * ratio_x + 0.5))
    target_height = max(1, int(height * ratio_y + 0.5))
    target_stride = ((target_width * 3) + 3) & ~3

    out = bytearray(data[:pixel_offset])
    struct.pack_into('<ii', out, 18, target_width, target_height)
    struct.pack_into('<I', out, 34, target_stride * target_height)          # biSizeImage
    struct.pack_into('<I', out, 2, pixel_offset + target_stride * target_height)

    # How many destination pixels each source pixel owns. Sampling from pixel centres, which is what
    # keeps the picture from drifting half a source pixel up and left.
    runs = [0] * width
    for x in range(target_width):
        source_x = int((x + 0.5) / ratio_x)
        if source_x >= width:
            source_x = width - 1
        runs[source_x] += 1
    pad = b'\0' * (target_stride - target_width * 3)

    previous_source_y = -1
    previous_row = b''
    for y in range(target_height):
        source_y = int((y + 0.5) / ratio_y)
        if source_y >= height:
            source_y = height - 1
        if source_y != previous_source_y:
            base = pixel_offset + source_y * source_stride
            row = data[base:base + width * 3]
            parts = []
            for x in range(width):
                count = runs[x]
                if count:
                    parts.append(row[x * 3:x * 3 + 3] * count)
            previous_row = b''.join(parts) + pad
            previous_source_y = source_y
        out += previous_row
    return bytes(out)


def primary_screen_size():
    """Best effort, and a 1080p answer rather than a failure when there is nothing to ask."""
    for command, pattern in (('xrandr --current', ' connected primary '),
                             ('xrandr --current', ' connected ')):
        try:
            import subprocess
            text = subprocess.check_output(command.split(), stderr=subprocess.DEVNULL)
            for line in text.decode('utf-8', 'replace').splitlines():
                if pattern in line:
                    for word in line.split():
                        if 'x' in word and word.split('x')[0].isdigit():
                            w, _, rest = word.partition('x')
                            h = rest.split('+')[0]
                            if h.isdigit():
                                return int(w), int(h)
        except Exception:
            pass
    return 1920, 1080


def ask_size():
    width, height = primary_screen_size()
    print('')
    print('What screen size should the menus be made for?')
    print('  Just press Enter for %dx%d.' % (width, height))
    print('  Or type a size like 2560x1440.')
    while True:
        answer = input('Size: ').strip().lower().replace('*', 'x').replace(',', 'x')
        if not answer:
            return width, height
        parts = [p for p in answer.split('x') if p.strip().isdigit()]
        if len(parts) == 2:
            return int(parts[0]), int(parts[1])
        print('  Type it as WIDTHxHEIGHT, for example 3840x2160.')


def ask_fit():
    print('')
    print('How should the 4:3 menu art fill a widescreen display?')
    print('  1. Stretch it edge to edge          (recommended, and the default)')
    print('  2. Keep its shape, black down the sides')
    while True:
        answer = input('Choice [1]: ').strip()
        if answer in ('', '1'):
            return 'stretch'
        if answer == '2':
            return 'uniform'
        print('  Type 1 or 2.')


def ask_game_directory():
    while True:
        print('')
        answer = input('Where is the game installed? (the folder WMAIN.EXE is in): ').strip()
        answer = answer.strip('"').strip("'")
        if answer and find_file(answer, 'WMAIN.EXE'):
            return answer
        print('  There is no WMAIN.EXE in that folder. Try again, or press Ctrl+C to stop.')


def remove_converted(folder, quiet):
    """Only what the manifest lists, so a file somebody put in that folder themselves survives."""
    manifest = os.path.join(folder, MANIFEST_NAME)
    if not os.path.isfile(manifest):
        if not quiet:
            print('Nothing to remove: no %s in %s' % (MANIFEST_NAME, folder))
        return 0
    removed = 0
    with io.open(manifest, 'r', encoding='ascii', errors='replace') as handle:
        for line in handle:
            name = line.strip()
            if not name or name.startswith('#'):
                continue
            path = os.path.join(folder, name)
            if os.path.isfile(path):
                os.remove(path)
                removed += 1
    os.remove(manifest)
    try:
        os.rmdir(folder)
        if not quiet:
            print('Removed %d files and the folder %s' % (removed, folder))
    except OSError:
        if not quiet:
            print('Removed %d files from %s; it was left in place because it is not empty'
                  % (removed, folder))
    return removed


def main():
    parser = argparse.ArgumentParser(
        description='Upscale the menu artwork in your own copy of the game.')
    parser.add_argument('game_directory', nargs='?',
                        help='the folder WMAIN.EXE is in; asked for if omitted')
    parser.add_argument('--output', help='where to write (default <game>/menu_hd)')
    parser.add_argument('--width', type=int, default=0)
    parser.add_argument('--height', type=int, default=0)
    parser.add_argument('--fit', choices=('stretch', 'uniform'), default=None)
    parser.add_argument('--quiet', action='store_true',
                        help='never prompt; requires the game directory')
    parser.add_argument('--remove', action='store_true',
                        help='delete what this tool wrote, using its manifest')
    args = parser.parse_args()

    game = args.game_directory
    if not game:
        if args.quiet:
            raise SystemExit('--quiet needs the game directory, because it must never wait for '
                             'somebody.')
        game = ask_game_directory()
    game = os.path.abspath(os.path.expanduser(game))
    if not find_file(game, 'WMAIN.EXE'):
        raise SystemExit('There is no WMAIN.EXE in %s, so that is not the game folder.' % game)

    output = args.output or os.path.join(game, 'menu_hd')

    if args.remove:
        remove_converted(output, args.quiet)
        return

    width, height = args.width, args.height
    if width <= 0 or height <= 0:
        width, height = primary_screen_size() if args.quiet else ask_size()
    fit = args.fit
    if fit is None:
        fit = 'stretch' if args.quiet else ask_fit()

    if width < CANVAS_WIDTH or height < CANVAS_HEIGHT:
        raise SystemExit('%dx%d is smaller than the menus\' own %dx%d; there is nothing to '
                         'upscale to.' % (width, height, CANVAS_WIDTH, CANVAS_HEIGHT))

    if fit == 'uniform':
        ratio_x = ratio_y = min(width / float(CANVAS_WIDTH), height / float(CANVAS_HEIGHT))
    else:
        ratio_x = width / float(CANVAS_WIDTH)
        ratio_y = height / float(CANVAS_HEIGHT)
    if ratio_x > MAX_RATIO:
        if not args.quiet:
            print("  A canvas wider than 4095 pixels breaks the engine's own run length encoder, "
                  "so the width is capped at %.3f times." % MAX_RATIO)
        ratio_x = MAX_RATIO
    ratio_y = min(ratio_y, MAX_RATIO)

    canvas_w = int(CANVAS_WIDTH * ratio_x + 0.5)
    canvas_h = int(CANVAS_HEIGHT * ratio_y + 0.5)
    if not args.quiet:
        print('')
        print('Converting for %dx%d (%s): %.3f times across, %.3f down, canvas %dx%d'
              % (width, height, fit, ratio_x, ratio_y, canvas_w, canvas_h))
        print('  reading  %s' % game)
        print('  writing  %s' % output)

    if not os.path.isdir(output):
        os.makedirs(output)

    written = skipped = 0
    total = 0
    names = []
    for archive in ARCHIVES:
        path = find_file(game, archive)
        if path is None:
            if not args.quiet:
                print('  %s not present, skipped' % archive)
            continue
        members = list(read_lab_directory(path))
        if not args.quiet:
            print('  %s - %d pictures' % (archive, len(members)))
        with io.open(path, 'rb') as handle:
            for name, offset, size in members:
                handle.seek(offset)
                bigger = upscale_bmp(handle.read(size), ratio_x, ratio_y)
                if bigger is None:
                    skipped += 1
                    continue
                with io.open(os.path.join(output, name), 'wb') as out:
                    out.write(bigger)
                written += 1
                total += len(bigger)
                names.append(name)
                if args.quiet:
                    sys.stdout.write('%s %d\n' % (name, len(bigger)))

    with io.open(os.path.join(output, MANIFEST_NAME), 'w', encoding='ascii') as handle:
        handle.write('# OpenPhantom menu artwork. Written by tools/convert_menu.py from your own '
                     'game.\n')
        handle.write('# %dx%d %s, canvas %dx%d\n' % (width, height, fit, canvas_w, canvas_h))
        handle.write('# Delete this folder, or run this script again with --remove\n')
        for name in names:
            handle.write('%s\n' % name)

    if not args.quiet:
        print('')
        print('%d pictures written, %d skipped, %.1f MB on disk'
              % (written, skipped, total / (1024.0 * 1024.0)))
        if written:
            print('')
            print('Done. Start the game and the menus will fill the screen.')
            print('To undo it, delete %s' % output)


if __name__ == '__main__':
    main()
