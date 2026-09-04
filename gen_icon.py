#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 exe 图标 icon.ico（暗色科幻风：网络节点 + 环）。纯 Python，无第三方依赖。"""
import struct, zlib, math

W = 1024  # 超采样渲染尺寸（4x 抗锯齿）

def lerp(a, b, t):
    return a + (b - a) * t

def mix(c1, c2, t):
    return tuple(int(lerp(c1[i], c2[i], t)) for i in range(3))

BG      = (10, 16, 26)     # #0A101A
BG2     = (13, 22, 38)     # #0D1626
BORDER  = (24, 48, 76)     # #18304C
CYAN    = (0, 212, 255)    # #00D4FF
PURPLE  = (124, 108, 255)  # #7C6CFF
WHITE   = (225, 248, 255)

def rounded_sq_alpha(x, y, size, rad):
    """返回 [0,1] 抗锯齿覆盖率：圆角矩形内为 1。"""
    half = size / 2.0
    cx, cy = half, half
    dx = abs(x - cx) - (half - rad)
    dy = abs(y - cy) - (half - rad)
    if dx <= 0 and dy <= 0:
        return 1.0
    d = math.hypot(max(dx, 0), max(dy, 0))
    return max(0.0, min(1.0, rad - d))

def stroke_alpha(d, r, w):
    """距离圆心 d、半径 r、线宽 w 的环覆盖率。"""
    return max(0.0, min(1.0, w/2 - abs(d - r)))

def disk_alpha(d, r):
    return max(0.0, min(1.0, r - d))

def make_icon(size):
    S = 4 * size
    img = [[(0,0,0,0) for _ in range(S)] for _ in range(S)]
    cx = cy = S / 2.0
    rad = 0.28 * S      # 圆环半径
    stroke = 0.045 * S  # 圆环线宽
    lat_w = 0.5 * rad   # 纬线半短轴
    node_r = 0.045 * S

    def put(x, y, col, a):
        if a <= 0 or not (0 <= x < S and 0 <= y < S):
            return
        xi, yi = int(x), int(y)
        px, py = img[yi][xi]
        na = a
        # 背景
        bg = mix(BG, BG2, (x+y)/(2*S))
        r = int(bg[0]*(1-na) + col[0]*na + 0.5)
        g = int(bg[1]*(1-na) + col[1]*na + 0.5)
        b = int(bg[2]*(1-na) + col[2]*na + 0.5)
        a_out = na
        img[yi][xi] = (r, g, b, a_out)

    # 圆角矩形背景
    for y in range(S):
        for x in range(S):
            a = rounded_sq_alpha(x+0.5, y+0.5, S, 0.22*S)
            bg = mix(BG, BG2, (x+y)/(2*S))
            img[y][x] = (bg[0], bg[1], bg[2], a)
    # 边框（细描边）
    for y in range(S):
        for x in range(S):
            a_in = rounded_sq_alpha(x+0.5, y+0.5, S, 0.22*S)
            a_out = rounded_sq_alpha(x+0.5, y+0.5, S+0.015*S, 0.225*S)
            edge = a_out - a_in
            if edge > 0:
                r,g,b,a = img[y][x]
                col = BORDER
                img[y][x] = (col[0], col[1], col[2], a + edge*0.9)

    # 主圆环（经线）——青色
    for y in range(S):
        for x in range(S):
            dx, dy = x+0.5-cx, y+0.5-cy
            d = math.hypot(dx, dy)
            a = stroke_alpha(d, rad, stroke)
            if a > 0:
                r,g,b,o = img[y][x]
                col = CYAN
                r = int(r*(1-a) + col[0]*a + 0.5)
                g = int(g*(1-a) + col[1]*a + 0.5)
                b = int(b*(1-a) + col[2]*a + 0.5)
                img[y][x] = (r, g, b, min(1.0, o + a))

    # 纬线（水平椭圆）——紫色
    for y in range(S):
        for x in range(S):
            dx, dy = x+0.5-cx, y+0.5-cy
            if dy == 0:
                continue
            # 椭圆参数化：点在半径为 rad 的圆上投影到椭圆
            t = math.atan2(dy, dx)
            ex = rad * math.cos(t)
            ey = lat_w * math.sin(t)
            d = math.hypot(dx - ex, dy - ey)
            a = stroke_alpha(d, 0, 0.03*S)  # 细线
            if a > 0:
                r,g,b,o = img[y][x]
                col = PURPLE
                r = int(r*(1-a) + col[0]*a + 0.5)
                g = int(g*(1-a) + col[1]*a + 0.5)
                b = int(b*(1-a) + col[2]*a + 0.5)
                img[y][x] = (r, g, b, min(1.0, o + a))

    # 中心节点（亮）
    c_pt = (cx, cy)
    for y in range(S):
        for x in range(S):
            d = math.hypot(x+0.5-c_pt[0], y+0.5-c_pt[1])
            a = disk_alpha(d, node_r*1.15)
            if a > 0:
                r,g,b,o = img[y][x]
                col = WHITE
                r = int(r*(1-a) + col[0]*a + 0.5)
                g = int(g*(1-a) + col[1]*a + 0.5)
                b = int(b*(1-a) + col[2]*a + 0.5)
                img[y][x] = (r, g, b, min(1.0, o + a))
    # 卫星节点（左上青、右下紫）
    for (px, py, col) in [(cx - rad*0.62, cy - rad*0.62, CYAN),
                          (cx + rad*0.62, cy + rad*0.62, PURPLE)]:
        for y in range(S):
            for x in range(S):
                d = math.hypot(x+0.5-px, y+0.5-py)
                a = disk_alpha(d, node_r)
                if a > 0:
                    r,g,b,o = img[y][x]
                    r = int(r*(1-a) + col[0]*a + 0.5)
                    g = int(g*(1-a) + col[1]*a + 0.5)
                    b = int(b*(1-a) + col[2]*a + 0.5)
                    img[y][x] = (r, g, b, min(1.0, o + a))

    # 下采样到目标尺寸
    step = 4
    out = []
    for y in range(size):
        row = []
        for x in range(size):
            rs = gs = bs = asum = 0.0
            for dy in range(step):
                for dx in range(step):
                    r,g,b,a = img[y*step+dy][x*step+dx]
                    rs += r; gs += g; bs += b; asum += a
            n = step*step
            a = asum/n
            if a <= 0:
                row.append((0,0,0,0))
            else:
                row.append((int(rs/n+0.5), int(gs/n+0.5), int(bs/n+0.5), int(a*255+0.5)))
        out.append(row)
    return out

def png_from_rgba(rows):
    h = len(rows); w = len(rows[0])
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter none
        for x in range(w):
            r,g,b,a = rows[y][x]
            raw += bytes((r,g,b,a))
    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr)
            + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))

def build_ico(path, sizes=(256, 48, 32)):
    datas = []
    entries = b''
    offset = 6 + 16*len(sizes)
    for s in sizes:
        png = png_from_rgba(make_icon(s))
        datas.append(png)
        b = 0 if s >= 256 else s
        entries += struct.pack('<BBBBHHII', b, b, 0, 0, 1, 32, len(png), offset)
        offset += len(png)
    with open(path, 'wb') as f:
        f.write(struct.pack('<HHH', 0, 1, len(sizes)))
        f.write(entries)
        for d in datas:
            f.write(d)
    print('icon written:', path)

if __name__ == '__main__':
    import os
    build_ico(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'icon.ico'))
