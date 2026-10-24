#!/usr/bin/env python3
"""Which dumped textures were overwritten after the game tiled them, and by what?
Pairs a 0.1.39 tiling log (GOW3_TILE_CHECK=1, user/tiles.txt) with a texture dump and the write trace.

usage: tilecheck.py <tiles.txt> <dump_dir> <writes.bin> <loader.log> <game EXEC/WAD/PS4_LE dir> [max]

Each tiling in tiles.txt names the game file bytes it was made from and passed the on-device checks
(source = file, tiled result = source). So the expected bytes of each tiled range are rebuilt here from
the game file with the same 1D-thin tiler, and compared with guest memory in the dump: any difference
was written after tiling. For those ranges the write trace lists what wrote there afterwards."""
import collections, os, re, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from wtrace import REC, KINDS, describe, file_names, load_opens

TILE = re.compile(r'#(\d+) ([\d.]+) dst (0x[0-9a-f]+) \[(0x[0-9a-f]+)\.\.(0x[0-9a-f]+)\] src (0x[0-9a-f]+)\+(0x[0-9a-f]+) '
                  r'(\d+)x(\d+) bpp (\d+) micro (\d+) region (\d+),(\d+),(\d+)\.\.(\d+),(\d+),(\d+) pitch (\d+) slice (\d+)'
                  r'.*?\| (.*?) \| (.*)$')
SOURCE = re.compile(r'(\S+) @(0x[0-9a-f]+) ok \((\d+)/(\d+) B')
CHANGED = re.compile(r'#(\d+) ([\d.]+) changed after tiling at ([\d.]+)')


def element_index(x, y):
    return (x & 1) | (y & 1) << 1 | (x >> 1 & 1) << 2 | (y >> 1 & 1) << 3 | (x >> 2 & 1) << 4 | (y >> 2 & 1) << 5


def tile(src, t):
    """Expected tiled bytes of tiling t over [lo, hi), or None (thin micro mode, one slice only)."""
    if t['micro'] != 1 or t['region'][2] != 0 or t['region'][5] != 1:
        return None
    left, top, _, right, bottom, _ = t['region']
    b = t['bpp'] // 8
    w, h = right - left, bottom - top
    rows = (bottom + 7) // 8 - top // 8
    per_row = (t['hi'] - t['lo']) // (rows * 64 * b)
    lines = np.frombuffer(src, np.uint8)[:((h - 1) * t['pitch'] + w) * b]
    out = np.zeros(t['hi'] - t['lo'], np.uint8)
    have = np.zeros(t['hi'] - t['lo'], bool)
    ys, xs = np.mgrid[top:bottom, left:right]
    at = ((ys // 8) * per_row + xs // 8) * 64 * b + element_index(xs, ys) * b - (top // 8) * per_row * 64 * b
    frm = ((ys - top) * t['pitch'] + (xs - left)) * b
    for k in range(b):
        out[(at + k).ravel()] = lines[(frm + k).ravel()]
        have[(at + k).ravel()] = True
    return out, have


def main():
    tiles_txt, dump, wfile, log, wads = sys.argv[1:6]
    limit = int(sys.argv[6]) if len(sys.argv) > 6 else 12
    tiles, changed = {}, {}
    for line in open(tiles_txt, errors='replace'):
        m = TILE.match(line)
        if m:
            g = m.groups()
            s = SOURCE.search(g[19])
            tiles[int(g[0])] = dict(call=int(g[0]), time=float(g[1]), dst=int(g[2], 16), lo=int(g[3], 16), hi=int(g[4], 16),
                                    width=int(g[7]), height=int(g[8]), bpp=int(g[9]), micro=int(g[10]),
                                    region=tuple(int(v) for v in g[11:17]), pitch=int(g[17]),
                                    file=s.group(1) if s else None, offset=int(s.group(2), 16) if s else None,
                                    span=int(g[6], 16))
            continue
        m = CHANGED.match(line)
        if m:
            changed[int(m.group(1))] = (float(m.group(2)), float(m.group(3)))
    print(f'{len(tiles)} tilings, {len(changed)} reported changed later')

    recs = np.fromfile(wfile, REC)
    recs = recs[np.argsort(recs['seq'], kind='stable')]
    t0 = int(recs['time'].min()) if len(recs) else 0
    names_at = file_names(recs, load_opens(log))
    tile_time = {int(r['b']): int(r['time']) for r in recs[recs['kind'] == 13]}
    lo_all, hi_all = recs['address'], recs['address'] + np.maximum(recs['size'], 1)

    man = {int(l.split()[0]): l.split() for l in open(os.path.join(dump, 'manifest.txt')) if l[0] != '#'}
    by_lo = sorted(tiles.values(), key=lambda t: t['lo'])
    results = []
    for i, f in man.items():
        base, size = int(f[1], 16), int(f[13])
        mem = np.fromfile(os.path.join(dump, f'{i:03d}_mem.bin'), np.uint8)
        # the newest tiling of each range inside this texture
        newest = {}
        for t in by_lo:
            if t['hi'] <= base or t['lo'] >= base + size:
                continue
            key = (t['lo'], t['hi'])
            if key not in newest or newest[key]['call'] < t['call']:
                newest[key] = t
        bad_total, checked, bad_ranges = 0, 0, []
        for t in newest.values():
            if not t['file'] or t['lo'] < base or t['hi'] > base + len(mem):
                continue
            path = os.path.join(wads, t['file'])
            if not os.path.exists(path):
                continue
            with open(path, 'rb') as fh:
                fh.seek(t['offset'])
                src = fh.read(t['span'])
            if len(src) < t['span']:
                continue
            expected = tile(src, t)
            if expected is None:
                continue
            out, have = expected
            got = mem[t['lo'] - base:t['hi'] - base]
            diff = (got != out) & have
            checked += int(have.sum())
            n = int(diff.sum())
            if n:
                bad_total += n
                where = np.nonzero(diff)[0]
                bad_ranges.append((t, n, t['lo'] + int(where[0]), t['lo'] + int(where[-1]) + 1))
        results.append((i, f, checked, bad_total, bad_ranges))

    print(f'\n{len(results)} dumped textures; checked against the game files:')
    clean = [r for r in results if r[2] and not r[3]]
    bad = sorted([r for r in results if r[3]], key=lambda r: -r[3])
    print(f'  {len(clean)} exactly as tiled, {len(bad)} overwritten after tiling, '
          f'{sum(1 for r in results if not r[2])} not checkable (no tiling record)')
    for i, f, checked, n, ranges in bad[:limit]:
        print(f'\n== tex {i} {f[2]} {f[4]}x{f[5]} @{f[1]}: {n} of {checked} bytes differ from the tiled file data')
        for t, k, a, b in sorted(ranges, key=lambda r: -r[1])[:3]:
            ch = changed.get(t['call'])
            print(f'  tiling #{t["call"]} at {t["time"]:.3f}s from {t["file"]} @{t["offset"]:#x}: {k} bytes differ in '
                  f'{a:#x}..{b:#x}' + (f'; watcher saw the change at {ch[0]:.3f}s' if ch else '; watcher saw no change'))
            after = tile_time.get(t['call'], 0)
            hit = recs[(lo_all < b) & (hi_all > a) & (recs['time'] > after) & (recs['kind'] != 13)]
            tiled_again = recs[(lo_all < b) & (hi_all > a) & (recs['kind'] == 13) & (recs['time'] > after)]
            print(f'    afterwards: {len(hit)} traced writes, {len(tiled_again)} later tilings over it')
            for r in list(tiled_again[:3]) + list(hit[-6:]):
                print('      ' + describe(r, t0, names_at))


if __name__ == '__main__':
    main()
