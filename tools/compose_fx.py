#!/usr/bin/env python3
"""エフェクト系スプライト(斬撃の軌跡・爆発・火花・土煙・ゴールゲート)を生成し
assets/sprites_fx.txt を出力する。"""
import math
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CH = ".kbBLWgGhcCyorRe"
I = {c: CH.index(c) for c in CH}


def canvas(w, h):
    return [[0] * w for _ in range(h)]


def to_rows(p):
    return ["".join(CH[v] for v in r) for r in p]


def arc(w, h, cx, cy, r_in, r_out, a0, a1, fade=1.0):
    """三日月形の斬撃。a0→a1 (度) へ振る。先端(a1側)ほど太く明るい"""
    p = canvas(w, h)
    span = a1 - a0
    for y in range(h):
        for x in range(w):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            r = math.hypot(dx, dy)
            ang = math.degrees(math.atan2(dy, dx))
            # 角度を a0 基準で正規化
            t = (ang - a0) / span if span else 0
            if span < 0:
                t = (ang - a0) / span
            if not (0 <= t <= 1):
                continue
            # 先端ほど太い
            thick = (r_out - r_in) * (0.25 + 0.75 * t) * fade
            if r_out - thick <= r <= r_out:
                edge = (r_out - r) / max(thick, 0.01)
                if edge < 0.3:
                    c = I["W"]
                elif edge < 0.65:
                    c = I["C"]
                else:
                    c = I["c"]
                if t < 0.25 and edge > 0.3:
                    c = I["c"] if (x + y) % 2 == 0 else 0
                p[y][x] = c
    return p


def ring(w, h, cx, cy, r_in, r_out, cols):
    p = canvas(w, h)
    for y in range(h):
        for x in range(w):
            r = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if r_in <= r <= r_out:
                k = (r - r_in) / max(0.01, r_out - r_in)
                p[y][x] = cols[min(len(cols) - 1, int(k * len(cols)))]
    return p


def explosion(frame):
    rng = random.Random(7 + frame)
    p = canvas(16, 16)
    if frame == 0:
        q = ring(16, 16, 8, 8, 0, 4.5, [I["W"], I["W"], I["y"]])
    elif frame == 1:
        q = ring(16, 16, 8, 8, 0, 7, [I["W"], I["y"], I["y"], I["o"], I["r"]])
    elif frame == 2:
        q = ring(16, 16, 8, 8, 2.5, 7.8, [I["y"], I["o"], I["r"], I["R"]])
    else:
        q = ring(16, 16, 8, 8, 4.5, 7.8, [I["G"], I["g"]])
    for y in range(16):
        for x in range(16):
            if q[y][x] and (frame < 2 or rng.random() > 0.25 * (frame - 1)):
                p[y][x] = q[y][x]
    return p


def spark(frame):
    p = canvas(8, 8)
    if frame == 0:
        for i in range(8):
            p[3][i] = p[4][i] = I["y"] if i in (0, 7) else I["W"]
            p[i][3] = p[i][4] = I["y"] if i in (0, 7) else I["W"]
        p[1][1] = p[6][6] = p[1][6] = p[6][1] = I["y"]
    else:
        for (x, y) in ((0, 0), (7, 0), (0, 7), (7, 7), (3, 1), (4, 6), (1, 4), (6, 3)):
            p[y][x] = I["y"] if (x + y) % 2 else I["o"]
        p[3][3] = p[4][4] = I["W"]
    return p


def dust(frame):
    p = canvas(8, 8)
    r = 2.0 + frame * 1.2
    for y in range(8):
        for x in range(8):
            d = math.hypot(x + 0.5 - 4, y + 0.5 - 5)
            if d <= r and (frame == 0 or (x + y + frame) % 2 == 0):
                p[y][x] = I["h"] if d < r - 1 else I["G"]
    return p


def gate(frame):
    """ゴールのテレポートゲート 32x32"""
    p = canvas(32, 32)
    # 柱
    for x0 in (2, 25):
        for y in range(2, 32):
            for x in range(x0, x0 + 5):
                c = I["G"]
                if x == x0:
                    c = I["k"]
                elif x == x0 + 4:
                    c = I["k"]
                elif x == x0 + 1:
                    c = I["h"]
                elif x == x0 + 3:
                    c = I["g"]
                p[y][x] = c
        for y in range(6, 30, 5):
            p[y][x0 + 2] = I["C"] if (y // 5 + frame) % 2 else I["c"]
    # 上部の梁
    for x in range(2, 30):
        p[1][x] = I["k"]
        p[2][x] = I["h"]
        p[3][x] = I["G"]
        p[4][x] = I["k"]
    # 中央のフィールド (縞が流れる)
    for y in range(5, 32):
        for x in range(7, 25):
            v = (y + frame * 2) % 6
            if v == 0:
                p[y][x] = I["W"] if (x % 3 == 0) else I["C"]
            elif v in (1, 4) and (x + y) % 2 == 0:
                p[y][x] = I["c"]
    return p


def shockwave(frame):
    """地を這う衝撃波 16x16"""
    p = canvas(16, 16)
    for y in range(4, 16):
        for x in range(16):
            h = 12 - abs(x - 8) * (1.4 if frame == 0 else 1.1)
            top = 16 - h
            if y >= top:
                d = y - top
                p[y][x] = I["W"] if d < 1.5 else (I["y"] if d < 4 else I["o"])
                if (x + y + frame) % 3 == 0 and d >= 4:
                    p[y][x] = I["r"]
    return p


def debris(frame):
    """機械の破片 8x8 (回転 2 コマ)"""
    p = canvas(8, 8)
    pts = [(2, 2), (3, 2), (4, 3), (5, 4), (4, 5), (3, 5), (2, 4), (3, 3), (3, 4), (4, 4)] if frame == 0 else \
          [(2, 4), (3, 3), (4, 2), (5, 2), (5, 3), (4, 4), (3, 5), (4, 3), (3, 4)]
    for (x, y) in pts:
        p[y][x] = I["G"]
    p[pts[0][1]][pts[0][0]] = I["h"]
    p[pts[3][1]][pts[3][0]] = I["o"]
    # 輪郭
    q = [r[:] for r in p]
    for y in range(8):
        for x in range(8):
            if not p[y][x] and any(0 <= x + dx < 8 and 0 <= y + dy < 8 and p[y + dy][x + dx]
                                   for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                q[y][x] = I["k"]
    return q


def main():
    S = {}
    # 斬撃 (32x32, プレイヤー前方に置く。右向き基準)
    S["slash1_0"] = arc(32, 32, 4, 18, 10, 20, -75, 35)
    S["slash1_1"] = arc(32, 32, 4, 18, 13, 20, -60, 40, 0.5)
    S["slash2_0"] = arc(32, 32, 6, 22, 10, 20, 60, -85)
    S["slash2_1"] = arc(32, 32, 6, 22, 13, 20, 40, -85, 0.5)
    S["slash3_0"] = arc(32, 32, 2, 6, 12, 25, -60, 80)
    S["slash3_1"] = arc(32, 32, 2, 6, 16, 25, -30, 80, 0.5)
    S["slashair_0"] = arc(32, 32, 16, 16, 9, 15, -150, 170)
    S["slashair_1"] = arc(32, 32, 16, 16, 11, 15, -60, 200, 0.5)
    for i in range(4):
        S[f"boom{i}"] = explosion(i)
    for i in range(2):
        S[f"spark{i}"] = spark(i)
    for i in range(2):
        S[f"dust{i}"] = dust(i)
    for i in range(2):
        S[f"gate{i}"] = gate(i)
    for i in range(2):
        S[f"wave{i}"] = shockwave(i)
    for i in range(2):
        S[f"debris{i}"] = debris(i)
    out = ["# 自動生成: tools/compose_fx.py"]
    for name, p in S.items():
        out.append(f"@{name} {len(p[0])} {len(p)}")
        out += to_rows(p)
        out.append("")
    path = os.path.join(ROOT, "assets", "sprites_fx.txt")
    with open(path, "w") as f:
        f.write("\n".join(out))
    print(f"wrote {path} ({len(S)} frames)")


if __name__ == "__main__":
    main()
