#!/usr/bin/env python3
"""Who wrote the misplaced texture data? Pairs a write trace (GOW3_WRITE_TRACE=1, user/writes.bin)
with a texture dump (Documents/gow3/dump.txt, user/dumps/dump_N).

usage: wtrace.py <writes.bin> <dump_dir> <loader.log> [max textures]

1. Finds 512-byte texture chunks that appear at two addresses in the dump (texture data written to a
   second, wrong place: the corruption seen on iOS, docs/NEXT.md).
2. For each texture holding such chunks, lists the traced writes that cover them, newest last.
Record layout: runtime_memory.c WriteRecord; kinds: runtime.h WTRACE_*."""
import collections, os, re, sys
import numpy as np

KINDS = {1: 'fault guest', 2: 'fault host-call', 3: 'fault host', 4: 'file read', 5: 'open',
         6: 'memcpy', 7: 'memmove', 8: 'memset', 9: 'gpu backing', 10: 'dma copy cpu',
         11: 'dma copy gpu', 12: 'dma fill', 13: 'tiler'}
BACKING_SITES = {1: 'WriteData', 2: 'fence/EOP', 3: 'buffer download', 4: 'readback entry',
                 5: 'deferred readback', 6: 'image readback', 7: 'image download (GC)',
                 8: 'image download (deferred)'}
REC = np.dtype([('seq', '<u8'), ('time', '<u8'), ('address', '<u8'), ('a', '<u8'), ('b', '<u8'),
                ('size', '<u4'), ('kind', '<u2'), ('thread', '<u2')])


def load_opens(log):
    opens = {}
    for line in open(log, errors='replace'):
        m = re.match(r'Write trace: open (\d+) fd (\d+) (.*)', line)
        if m:
            opens[int(m.group(1))] = m.group(3).strip()
    return opens


def file_names(recs, opens):
    """seq of each file-read record -> the file its fd named at that moment."""
    current, out = {}, {}
    for r in recs[(recs['kind'] == 4) | (recs['kind'] == 5)]:
        if int(r['kind']) == 5:
            current[int(r['a'])] = opens.get(int(r['b']), '?')
        else:
            out[int(r['seq'])] = current.get(int(r['a']), 'fd %d' % int(r['a']))
    return out


def describe(r, t0, names_at):
    k = int(r['kind'])
    s = f"{(int(r['time']) - t0) / 1e9:9.3f}s thr {int(r['thread']):3d} {KINDS.get(k, k):16s} " \
        f"{int(r['address']):#x}+{int(r['size']):#x}"
    if k == 4:
        s += f"  {names_at.get(int(r['seq']), 'fd %d' % int(r['a']))} @{int(r['b']):#x}"
    elif k in (6, 7):
        s += f"  src {int(r['a']):#x} caller {int(r['b']):#x}"
    elif k == 8:
        s += f"  value {int(r['a'])} caller {int(r['b']):#x}"
    elif k == 9:
        s += f"  {BACKING_SITES.get(int(r['a']), int(r['a']))}{'' if int(r['b']) else ' (not written)'}"
    elif k == 13:
        s += f"  src {int(r['a']):#x} tiling #{int(r['b'])}"
    elif k in (10, 11):
        s += f"  src {int(r['a']):#x}"
    elif k in (1, 2):
        s += f"  rip {int(r['a']):#x}"
    elif k == 3:
        s += f"  host pc {int(r['a']):#x}"
    return s


def main():
    wfile, dump, log = sys.argv[1:4]
    limit = int(sys.argv[4]) if len(sys.argv) > 4 else 8
    recs = np.fromfile(wfile, REC)
    recs = recs[np.argsort(recs['seq'], kind='stable')]
    print(f'{len(recs)} write records; by kind:',
          dict(collections.Counter(KINDS.get(int(k), int(k)) for k in recs['kind'])))
    t0 = int(recs['time'].min()) if len(recs) else 0
    opens = load_opens(log)
    # fds are reused: name each read by the open that preceded it on that fd
    names_at = file_names(recs, opens)

    man = {int(l.split()[0]): l.split() for l in open(os.path.join(dump, 'manifest.txt')) if l[0] != '#'}
    index = collections.defaultdict(list)
    for i, f in man.items():
        if not f[2].startswith('Bc'):
            continue
        m = np.fromfile(os.path.join(dump, f'{i:03d}_mem.bin'), np.uint8)
        base = int(f[1], 16)
        for o in range(0, len(m) - 511, 512):
            c = m[o:o + 512]
            if len(np.unique(c)) >= 48:
                index[c.tobytes()].append((base + o, i))
    victims = collections.defaultdict(list)  # texture -> addresses of chunks that exist elsewhere too
    for places in index.values():
        if len({a for a, _ in places}) > 1:
            for a, i in places:
                victims[i].append(a)
    lo, hi = recs['address'], recs['address'] + np.maximum(recs['size'], 1)
    for i, addrs in sorted(victims.items(), key=lambda kv: -len(kv[1]))[:limit]:
        f = man[i]
        base, size = int(f[1], 16), int(f[13])
        print(f'\n== tex {i} {f[2]} {f[4]}x{f[5]} @{base:#x}+{size:#x}: {len(addrs)} chunks also elsewhere')
        addrs = sorted(addrs)
        ranges = []
        for a in addrs:
            if ranges and a <= ranges[-1][1]:
                ranges[-1][1] = a + 512
            else:
                ranges.append([a, a + 512])
        for a, b in ranges[:6]:
            hit = recs[(lo < b) & (hi > a)]
            print(f'  range {a:#x}..{b:#x} (tex+{a - base:#x}, {b - a} B): {len(hit)} writes')
            for r in hit[-6:]:
                print('    ' + describe(r, t0, names_at))


if __name__ == '__main__':
    main()
