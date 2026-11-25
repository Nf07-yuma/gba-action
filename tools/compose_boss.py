#!/usr/bin/env python3
"""ボス「GUARDIAN」(工場区画の守護ロボ, 64x64) を図形の組み合わせで合成し
assets/sprites_boss.txt を出力する。左向き(プレイヤー側)が基準。"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compose_player as cp  # noqa: E402

CH = cp.CH
I = {c: CH.index(c) for c in CH}
W = H = 64
cp.W = W
cp.H = H


def ellipse(cx, cy, rx, ry, fill, light=None, dark=None):
    px = {}
    for y in range(H):
        for x in range(W):
            dx, dy = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
            d = dx * dx + dy * dy
            if d <= 1.0:
                c = fill
                if light is not None and dx + dy < -0.55:
                    c = light
                elif dark is not None and dx + dy > 0.7:
                    c = dark
                px[(x, y)] = c
    return px


def box(x0, y0, x1, y1, fill, light=None, dark=None, stripe=None):
    px = {}
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            c = fill
            if light is not None and (y == y0 or x == x0):
                c = light
            if dark is not None and (y == y1 or x == x1):
                c = dark
            if stripe is not None and stripe[0] <= y <= stripe[1]:
                c = I["y"] if ((x + y) // 2) % 2 == 0 else I["k"]
            px[(x, y)] = c
    return px


def frame(bob=0, lean=0, eye="e", crouch=0, recoil=0, muzzle=False, legs_tuck=0):
    by = bob + crouch
    layers = []
    # 奥の脚
    hip_b = (40 + lean, 40 + by)
    knee_b = (46 + lean, 49 + by - legs_tuck)
    foot_b = (42, 60 - legs_tuck)
    layers.append(cp.limb([hip_b, knee_b, foot_b], 2.6, I["g"], I["G"]))
    layers.append(box(36, 60 - legs_tuck, 47, 62 - legs_tuck, I["g"], I["G"]))
    # 奥の腕 (肩)
    layers.append(ellipse(46 + lean, 24 + by, 7, 7, I["R"], I["r"]))
    # 胴体
    layers.append(box(18 + lean, 18 + by, 50 + lean, 42 + by, I["r"], I["o"], I["R"], stripe=(36 + by, 38 + by)))
    layers.append(box(22 + lean, 22 + by, 34 + lean, 32 + by, I["G"], I["h"], I["g"]))  # 胸部装甲
    # コア (弱点っぽく光る)
    layers.append(ellipse(28 + lean, 27 + by, 3.5, 3.5, I["C"], I["W"], I["c"]))
    # 頭
    layers.append(ellipse(32 + lean, 13 + by, 10, 7, I["G"], I["h"], I["g"]))
    layers.append(box(23 + lean, 11 + by, 37 + lean, 14 + by, I["k"]))
    ex = 25 + lean
    layers.append({(ex, 12 + by): I[eye], (ex + 1, 12 + by): I[eye], (ex + 2, 12 + by): I["W"],
                   (ex, 13 + by): I[eye], (ex + 1, 13 + by): I[eye], (ex + 3, 12 + by): I[eye]})
    # 手前の脚
    hip_f = (26 + lean, 41 + by)
    knee_f = (20 + lean, 50 + by - legs_tuck)
    foot_f = (24, 60 - legs_tuck)
    layers.append(cp.limb([hip_f, knee_f, foot_f], 2.8, I["G"], I["h"], I["g"]))
    layers.append(box(16, 59 - legs_tuck, 30, 62 - legs_tuck, I["G"], I["h"], I["g"]))
    # 手前の腕: 大砲
    sx, sy = 20 + lean, 26 + by
    layers.append(ellipse(sx, sy, 7, 7, I["r"], I["o"], I["R"]))
    cx0 = 2 + recoil
    layers.append(box(cx0, sy + 2, sx - 2, sy + 9, I["G"], I["h"], I["g"]))
    layers.append(box(cx0, sy + 3, cx0 + 3, sy + 8, I["g"], None, None))
    layers.append(box(cx0 + 6, sy + 1, cx0 + 8, sy + 10, I["R"], I["r"]))
    if muzzle:
        layers.append(ellipse(cx0 - 1, sy + 5.5, 3, 4.5, I["y"], I["W"], I["o"]))
    c = cp.compose(layers)
    return ["".join(CH[c.get((x, y), 0)] for x in range(W)) for y in range(H)]


def core_frame(open_k=0, eye="r", glow=False):
    """最終ボス OVERLORD CORE (64x64): 球体の装甲が上下に開いて目が露出する"""
    layers = []
    # 外周のリング (固定アーム)
    layers.append(ellipse(32, 32, 30, 30, I["g"], I["G"], I["k"]))
    layers.append(ellipse(32, 32, 26, 26, I["k"]))
    # 内部 (目)
    eye_c = I[eye]
    layers.append(ellipse(32, 32, 18, 18, I["R"], I["r"]))
    layers.append(ellipse(32, 32, 12, 12, eye_c, I["W"] if glow else I["o"], I["R"]))
    layers.append(ellipse(30, 30, 4, 4, I["W"], I["W"]))
    # 装甲 (上下の殻): open_k ピクセル開く
    top = ellipse(32, 32 - open_k, 24, 24, I["G"], I["h"], I["g"])
    top = {(x, y): c for (x, y), c in top.items() if y < 32 - open_k}
    bot = ellipse(32, 32 + open_k, 24, 24, I["G"], I["h"], I["g"])
    bot = {(x, y): c for (x, y), c in bot.items() if y >= 32 + open_k}
    for part in (top, bot):
        # 装甲の継ぎ目とランプ
        for (x, y) in list(part.keys()):
            if (x - 32) % 12 == 0:
                part[(x, y)] = I["g"]
        layers.append(part)
    lamp = {}
    for i, x in enumerate((20, 32, 44)):
        lamp[(x, 14 - open_k)] = I["C"] if glow else I["c"]
        lamp[(x, 50 + open_k)] = I["C"] if glow else I["c"]
    layers.append(lamp)
    c = cp.compose(layers)
    return ["".join(CH[c.get((x, y), 0)] for x in range(W)) for y in range(H)]


def main():
    frames = {
        "boss_idle0": frame(0),
        "boss_idle1": frame(1),
        "boss_charge": frame(2, lean=-3, eye="y", crouch=1),
        "boss_jump": frame(-2, legs_tuck=5),
        "boss_shoot": frame(0, recoil=3, eye="y", muzzle=True),
        "core_closed": core_frame(0),
        "core_half": core_frame(5),
        "core_open": core_frame(11, eye="r"),
        "core_glow": core_frame(11, eye="y", glow=True),
    }
    out = ["# 自動生成: tools/compose_boss.py"]
    for name, rows in frames.items():
        out.append(f"@{name} 64 64")
        out += rows
        out.append("")
    path = os.path.join(cp.ROOT, "assets", "sprites_boss.txt")
    with open(path, "w") as f:
        f.write("\n".join(out))
    print(f"wrote {path} ({len(frames)} frames)")


if __name__ == "__main__":
    main()
