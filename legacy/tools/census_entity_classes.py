"""Write the entity spawner's fact table, entity_class_data.h, from the game's own data.

The spawner offers an actor file when the data say it is safe to raise: it has clips, it has a
head or a chest, or the retail levels themselves raise it as a living thing, a pickup or the one
gun the player can mount. Two of those facts live in the archive (the clip count and the node
names of every .baf in big.lab) and one lives only in the levels (the class each retail placement
gives the file). The panel reads the archive at run time but never the levels, so the classes are
carried as data, one row per archive file, together with the archive facts the rule reads, so the
unit test can run the rule over every file of the game without the game.

This tool only extracts. It decides nothing: the rule is entity_offer.c's, in one place.

    python tools/census_entity_classes.py --editor <editor source> [--game <install>]
    python tools/census_entity_classes.py --editor <...> --check

--check derives the table again and compares it with the committed header; it exits 1 when they
differ, which means the header is stale or the data changed. The editor is a sibling project and is
not vendored here; its container codec reads the levels, and --editor is the folder its core
package is in. The generated header is committed, so building the patch never needs either.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'src', 'mods', 'dev_overlay', 'entity_class_data.h')
DEFAULT_GAME = r'C:\Program Files (x86)\LucasArts\The Phantom Menace'

# The eleven levels the retail game plays. The level directory holds a twelfth, bridge.b3d, which
# no episode reaches, so what it places proves nothing about the game.
RETAIL = ('assault', 'bigcity', 'espa', 'fedship', 'final', 'garden', 'gunga', 'maul', 'queen',
          'race', 'swamp')

# The actor file layout, as actor_catalog.c reads it.
BAF_HEADER = 0x120
BAF_GEO_SIZE = 0xC0
BAF_CLIP_COUNT = 0xC8
BAF_ANIM_SIZE = 0xD4
GEO_NODE_COUNT = 0x54
NODE_RECORD = 0xB4
NODE_NAME_MAX = 0x44
NODES_MAX = 400

# An ENMY record: a head of 0xEC bytes and sixteen per route node; the class at +0x34, the model
# index at +0xA8.
ENMY_HEAD = 0xEC
ENMY_CLASS = 0x34
ENMY_MODEL = 0xA8

FACT_BODY = 0x01    # a node named "head" or "chest"
FACT_MOUNT = 0x02   # nodes named "turret" and "target", the two the mount asks for


def lab_directory(path):
    with open(path, 'rb') as fh:
        head = fh.read(16)
        if head[:4] != b'LABN':
            raise SystemExit('%s does not begin with LABN' % path)
        entries, names_size = struct.unpack_from('<II', head, 8)
        directory = fh.read(entries * 16)
        names = fh.read(names_size)
    out = []
    for i in range(entries):
        off, data, size, _kind = struct.unpack_from('<IIII', directory, i * 16)
        end = names.find(b'\0', off)
        out.append((names[off:end].decode('latin1'), data, size))
    return out


def baf_facts(fh, data, size):
    """(clips, facts) of one actor file, walked the way actor_catalog.c walks it."""
    fh.seek(data)
    header = fh.read(BAF_HEADER)
    if len(header) < BAF_HEADER:
        return None
    geo_size = struct.unpack_from('<I', header, BAF_GEO_SIZE)[0]
    anim_size = struct.unpack_from('<I', header, BAF_ANIM_SIZE)[0]
    clips = struct.unpack_from('<I', header, BAF_CLIP_COUNT)[0]
    if anim_size > size or geo_size > size - anim_size:
        return None
    anim_start = size - anim_size
    geo = anim_start - geo_size
    if geo < BAF_HEADER or geo + GEO_NODE_COUNT + 4 > size:
        return None
    fh.seek(data + geo + GEO_NODE_COUNT)
    count = struct.unpack('<I', fh.read(4))[0]
    if count == 0 or count > NODES_MAX or count * NODE_RECORD > anim_start - geo:
        return None
    fh.seek(data + anim_start - count * NODE_RECORD)
    blob = fh.read(count * NODE_RECORD)
    names = set()
    for i in range(count):
        raw = blob[i * NODE_RECORD:i * NODE_RECORD + NODE_NAME_MAX]
        end = raw.find(b'\0')
        if end >= 0:
            names.add(raw[:end].decode('latin1'))
    facts = 0
    if 'head' in names or 'chest' in names:
        facts |= FACT_BODY
    if 'turret' in names and 'target' in names:
        facts |= FACT_MOUNT
    return clips, facts


def enmy_records(payload):
    """B3D_ENMY: n sizes, then n records of 0xEC + 16 * k bytes."""
    if not payload:
        return []
    n = 0
    total = 0
    while 4 * (n + 1) <= len(payload):
        size = struct.unpack_from('<I', payload, 4 * n)[0]
        if size < ENMY_HEAD or (size - ENMY_HEAD) % 16 or size > 1 << 20:
            break
        total += size
        n += 1
        if 4 * n + total >= len(payload):
            break
    if 4 * n + total != len(payload):
        raise SystemExit('an ENMY chunk does not walk to its end')
    sizes = struct.unpack_from('<%dI' % n, payload, 0)
    out = []
    at = 4 * n
    for size in sizes:
        rec = payload[at:at + size]
        at += size
        out.append((struct.unpack_from('<i', rec, ENMY_CLASS)[0],
                    struct.unpack_from('<i', rec, ENMY_MODEL)[0]))
    return out


def anam_names(payload):
    """B3D_ANAM: n lengths, then n paths; the first path starts with a drive letter."""
    first = -1
    for i in range(len(payload) - 2):
        if chr(payload[i]).isalpha() and payload[i + 1] == 0x3A and payload[i + 2] == 0x5C:
            first = i
            break
    if first < 0 or first % 4:
        raise SystemExit('an ANAM chunk has no path list')
    k = first // 4
    lengths = struct.unpack_from('<%dI' % k, payload, 0)
    names = []
    at = first
    for n in lengths:
        raw = payload[at:at + n]
        at += n
        end = raw.find(b'\0')
        path = raw[:end if end >= 0 else len(raw)].decode('latin1')
        names.append(path.replace('/', '\\').rsplit('\\', 1)[-1].lower())
    return names


def derive(editor, game):
    sys.path.insert(0, editor)
    from core.container import B3DFile  # noqa: E402  the editor's own codec

    rows = {}
    lab = os.path.join(game, 'big.lab')
    with open(lab, 'rb') as fh:
        for name, data, size in lab_directory(lab):
            if not name.lower().endswith('.baf'):
                continue
            facts = baf_facts(fh, data, size)
            if facts is None:
                raise SystemExit('%s does not read as an actor file' % name)
            rows[name.lower()] = [facts[0], facts[1], 0, 0]

    for level in RETAIL:
        b3d = B3DFile.read(os.path.join(game, 'GAMEDATA', 'LEVEL', level.upper() + '.B3D'))
        models = anam_names(b3d.payload('B3D_ANAM'))
        for klass, model in enmy_records(b3d.payload('B3D_ENMY')):
            if not 0 <= model < len(models) or models[model] not in rows:
                continue
            if not 0 <= klass < 32:
                raise SystemExit('%s places a class %d, which the table cannot hold' %
                                 (level, klass))
            row = rows[models[model]]
            row[2] |= 1 << klass
            row[3] += 1
    return rows


def render(rows):
    lines = [
        '/* entity_class_data.h: the facts the entity spawner\'s offer rule reads, one row per',
        ' * actor file of the retail archive.',
        ' *',
        ' * GENERATED by tools/census_entity_classes.py from big.lab and the eleven retail levels;',
        ' * regenerate rather than edit. Each row: the file, lower-cased; its clip count; '
        'ENTITY_FACT_*',
        ' * read from its node names; a mask with bit c set when a retail level places the file as',
        ' * class c; and how many retail placements use it. Sorted by name, so a lookup can halve.',
        ' * Included by entity_offer.c alone, which holds the one copy of the table. */',
        '#ifndef DEV_OVERLAY_ENTITY_CLASS_DATA_H',
        '#define DEV_OVERLAY_ENTITY_CLASS_DATA_H',
        '',
        '#include "entity_offer.h"',
        '',
        '#define ENTITY_CLASS_ROWS %du' % len(rows),
        '',
        'static const entity_class_row_t ENTITY_CLASS_TABLE[ENTITY_CLASS_ROWS] = {',
    ]
    for name in sorted(rows):
        clips, facts, classes, placements = rows[name]
        lines.append('    { "%s", %d, 0x%02Xu, 0x%08Xu, %d },' % (name, clips, facts, classes,
                                                                placements))
    lines += ['};', '', '#endif /* DEV_OVERLAY_ENTITY_CLASS_DATA_H */', '']
    return '\r\n'.join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--editor', required=True, help='path to the obi-editor checkout')
    ap.add_argument('--game', default=DEFAULT_GAME, help='the installed game')
    ap.add_argument('--check', action='store_true', help='compare with the committed header')
    args = ap.parse_args()

    text = render(derive(args.editor, args.game))
    if args.check:
        with open(OUT, 'r', newline='') as fh:
            if fh.read() != text:
                print('entity_class_data.h is stale: the data give another table')
                return 1
        print('entity_class_data.h matches the data')
        return 0
    with open(OUT, 'w', newline='') as fh:
        fh.write(text)
    print('wrote %s, %d rows' % (OUT, text.count('\r\n    { ')))
    return 0


if __name__ == '__main__':
    sys.exit(main())
