#!/usr/bin/env python3
"""Compare 0.1.34 texture dumps: detile NNN_mem.bin on the CPU (port of tiling.comp, micro-tiled
modes), compare with NNN_gpu.bin, decode both to PNG and build contact sheets.
usage: dumpcheck.py <dump_dir> <out_dir>"""
import os, sys
import numpy as np
from PIL import Image

DISPLAY, THIN, DEPTH = 0, 1, 2
# tile mode -> (array mode 1D=2 / 2D=4 / linear, micro mode)
MICRO_1D = {5: DEPTH, 9: DISPLAY, 13: THIN}
LINEAR = {8, 31}

def bit(v, b):
    return (v >> b) & 1

def pixel_index(x, y, mode, bpp):
    x0, x1, x2 = bit(x, 0), bit(x, 1), bit(x, 2)
    y0, y1, y2 = bit(y, 0), bit(y, 1), bit(y, 2)
    if mode == DISPLAY:
        p = {8: (x0, x1, x2, y1, y0, y2), 16: (x0, x1, x2, y0, y1, y2),
             32: (x0, x1, y0, x2, y1, y2), 64: (x0, y0, x1, x2, y1, y2),
             128: (y0, x0, x1, x2, y1, y2)}[bpp]
    else:
        p = (x0, y0, x1, y1, x2, y2)
    return p[0] | p[1] << 1 | p[2] << 2 | p[3] << 3 | p[4] << 4 | p[5] << 5

def detile_micro(mem, offset, pitch, height, bpe, mode):
    bpp = bpe * 8
    ys, xs = np.mgrid[0:height, 0:pitch].astype(np.int64)
    mtpr = pitch // 8
    tile = ((ys // 8) * mtpr + xs // 8) * 64 * bpe
    addr = offset + tile + pixel_index(xs, ys, mode, bpp) * bpe
    out = np.zeros((height, pitch, bpe), np.uint8)
    ok = addr + bpe <= len(mem)
    idx = addr[..., None] + np.arange(bpe)
    out[ok] = mem[idx[ok]]
    return out, int((~ok).sum())

def rgb565(c):
    r = ((c >> 11) & 31) * 255 // 31
    g = ((c >> 5) & 63) * 255 // 63
    b = (c & 31) * 255 // 31
    return np.stack([r, g, b], -1).astype(np.int32)

def bc1_color(blk, force4=False):
    c0 = blk[..., 0].astype(np.int32) | blk[..., 1].astype(np.int32) << 8
    c1 = blk[..., 2].astype(np.int32) | blk[..., 3].astype(np.int32) << 8
    idx = (blk[..., 4].astype(np.uint32) | blk[..., 5].astype(np.uint32) << 8 |
           blk[..., 6].astype(np.uint32) << 16 | blk[..., 7].astype(np.uint32) << 24)
    p0, p1 = rgb565(c0), rgb565(c1)
    four = (c0 > c1)[..., None] | force4
    p2 = np.where(four, (2 * p0 + p1) // 3, (p0 + p1) // 2)
    p3 = np.where(four, (p0 + 2 * p1) // 3, 0)
    pal = np.stack([p0, p1, p2, p3], -2)  # ... 4 3
    sel = np.stack([(idx >> (2 * i)) & 3 for i in range(16)], -1)  # ... 16
    rgb = np.take_along_axis(pal, sel[..., None].astype(np.int64), -2)
    alpha = np.where((~four[..., 0])[..., None] & (sel == 3), 0, 255)
    return rgb, alpha

def bc4_channel(blk):
    a0 = blk[..., 0].astype(np.int32)
    a1 = blk[..., 1].astype(np.int32)
    bits = np.zeros(blk.shape[:-1], np.uint64)
    for i in range(6):
        bits |= blk[..., 2 + i].astype(np.uint64) << np.uint64(8 * i)
    sel = np.stack([((bits >> np.uint64(3 * i)) & np.uint64(7)).astype(np.int64) for i in range(16)], -1)
    eight = (a0 > a1)[..., None]
    pal = [a0, a1]
    for i in range(1, 7):
        pal.append(((7 - i) * a0 + i * a1) // 7)
    pal6 = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [a0 * 0, a0 * 0 + 255]
    p8 = np.stack(pal, -1)
    p6 = np.stack(pal6, -1)
    return np.where(eight, np.take_along_axis(p8, sel, -1), np.take_along_axis(p6, sel, -1))

def blocks_to_image(texels, bh, bw):
    # texels: bh bw 16 C -> (bh*4) (bw*4) C
    c = texels.shape[-1]
    t = texels.reshape(bh, bw, 4, 4, c).transpose(0, 2, 1, 3, 4)
    return t.reshape(bh * 4, bw * 4, c)

def decode(fmt, data, w, h):
    """data: rows x units x bytes array -> RGBA uint8 image of w x h (or None)."""
    if fmt.startswith('Bc'):
        bh, bw = data.shape[:2]
        if fmt.startswith('Bc1'):
            rgb, a = bc1_color(data)
            px = np.concatenate([rgb, a[..., None]], -1)
        elif fmt.startswith('Bc2'):
            rgb, _ = bc1_color(data[..., 8:], True)
            a = np.stack([((data[..., i // 2] >> (4 * (i % 2))) & 15) * 17 for i in range(16)], -1)
            px = np.concatenate([rgb, a[..., None].astype(np.int32)], -1)
        elif fmt.startswith('Bc3'):
            rgb, _ = bc1_color(data[..., 8:], True)
            a = bc4_channel(data[..., :8])
            px = np.concatenate([rgb, a[..., None]], -1)
        elif fmt.startswith('Bc4'):
            r = bc4_channel(data)
            px = np.stack([r, r, r, r * 0 + 255], -1)
        elif fmt.startswith('Bc5'):
            r = bc4_channel(data[..., :8])
            g = bc4_channel(data[..., 8:])
            px = np.stack([r, g, r * 0 + 128, r * 0 + 255], -1)
        else:
            return None
        img = blocks_to_image(np.clip(px, 0, 255).astype(np.uint8), bh, bw)
        return img[:h, :w]
    if fmt.startswith('R8G8B8A8') or fmt.startswith('B8G8R8A8'):
        img = data[:h, :w, :4].copy()
        if fmt.startswith('B8'):
            img = img[..., [2, 1, 0, 3]]
        return img
    if fmt.startswith('R8Unorm'):
        r = data[:h, :w, 0]
        return np.stack([r, r, r, r * 0 + 255], -1)
    return None

def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    rows = []
    for line in open(os.path.join(src, 'manifest.txt')):
        if line.startswith('#') or not line.strip():
            continue
        f = line.split()
        rows.append(dict(idx=int(f[0]), addr=f[1], fmt=f[2], w=int(f[4]), h=int(f[5]),
                         pitch=int(f[6]), tile=int(f[7]), array=int(f[8]), mips=int(f[9]),
                         layers=int(f[10]), bits=int(f[11]), swz=int(f[12]), gsize=int(f[13]),
                         m_off=int(f[14]), m_size=int(f[15]), m_pitch=int(f[16]),
                         m_height=int(f[17]), flags=int(f[18], 16), usage=f[19],
                         gpu_bytes=int(f[20])))
    report = []
    thumbs_gpu, thumbs_mem = [], []
    for r in rows:
        i = r['idx']
        block = r['fmt'].startswith('Bc')
        bpe = r['bits'] // 8
        gpu = np.fromfile(os.path.join(src, f'{i:03d}_gpu.bin'), np.uint8)
        mem = np.fromfile(os.path.join(src, f'{i:03d}_mem.bin'), np.uint8)
        units = (r['pitch'] + 3) // 4 if block else r['pitch']
        hrows = (r['h'] + 3) // 4 if block else r['h']
        gpu_lin = gpu[:units * hrows * bpe].reshape(hrows, units, bpe)
        wu = (r['w'] + 3) // 4 if block else r['w']
        note = ''
        mem_lin = None
        mp = max((r['m_pitch'] + 3) // 4, 1) if block else r['m_pitch']
        mh = max((r['m_height'] + 3) // 4, 1) if block else r['m_height']
        if r['tile'] in MICRO_1D and r['array'] == 2:
            if mp % 8 == 0:
                lin, oob = detile_micro(mem, r['m_off'], mp, max(mh, (mh + 7) // 8 * 8),
                                        bpe, MICRO_1D[r['tile']])
                mem_lin = lin[:hrows]
                if oob:
                    note += f' oob={oob}'
            else:
                note += f' pitch {mp} not /8'
        elif r['tile'] in LINEAR or r['array'] in (0, 1):
            need = mp * hrows * bpe
            m = mem[r['m_off']:r['m_off'] + need]
            if len(m) == need:
                mem_lin = m.reshape(hrows, mp, bpe)
        else:
            note += ' macro-tiled: not compared'
        diff = ''
        if mem_lin is not None:
            a = gpu_lin[:hrows, :wu]
            b = mem_lin[:hrows, :wu]
            bad = np.any(a != b, -1)
            diff = f'{100.0 * bad.mean():6.2f}% units differ'
            if bad.any():
                ys, xs = np.nonzero(bad)
                diff += f' (first y{ys[0]} x{xs[0]}, rows {ys.min()}-{ys.max()})'
        g_img = decode(r['fmt'], gpu_lin, r['w'], r['h'])
        m_img = decode(r['fmt'], mem_lin, r['w'], r['h']) if mem_lin is not None else None
        if g_img is not None:
            Image.fromarray(g_img, 'RGBA').save(os.path.join(dst, f'{i:03d}_gpu.png'))
            thumbs_gpu.append((i, g_img))
        if m_img is not None:
            Image.fromarray(m_img, 'RGBA').save(os.path.join(dst, f'{i:03d}_mem.png'))
            thumbs_mem.append((i, m_img))
        report.append(f"{i:03d} {r['addr']} {r['fmt']:22s} {r['w']}x{r['h']} p{r['pitch']} "
                      f"t{r['tile']} a{r['array']} mips{r['mips']} L{r['layers']} {r['usage']} "
                      f"flags{r['flags']:#x}: {diff}{note}")
    open(os.path.join(dst, 'report.txt'), 'w').write('\n'.join(report) + '\n')
    print('\n'.join(report))
    for name, thumbs in (('sheet_gpu', thumbs_gpu), ('sheet_mem', thumbs_mem)):
        if not thumbs:
            continue
        cell, cols = 160, 8
        sheet = Image.new('RGB', (cell * cols, cell * ((len(thumbs) + cols - 1) // cols)), (40, 0, 40))
        for n, (i, img) in enumerate(thumbs):
            t = Image.fromarray(img, 'RGBA').convert('RGB')
            t.thumbnail((cell - 4, cell - 4))
            sheet.paste(t, ((n % cols) * cell + 2, (n // cols) * cell + 2))
        sheet.save(os.path.join(dst, f'{name}.png'))

if __name__ == '__main__':
    main()
