#!/usr/bin/env python3
"""主人公ロボ "STEEL" のスプライトを関節ポーズから合成し assets/sprites_player.txt を出力する。

頭部・胴体は手描きのテンプレート、手足とマフラーは関節座標からの太線描画で作る。
パーツごとに輪郭を付けて奥から手前へ重ねるため、ポーズを数値で調整するだけで
フレームを量産できる。
"""
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CH = ".kbBLWgGhcCyorRe"
W = H = 32

HEAD = [
    "....kkkkk....",
    "..kkWWWWWkk..",
    ".kWWWWWWWLLk.",
    "kWWLLLLLLLBBk",
    "kWLBkkkkkkkkk",
    "kLBkgcCCCCCCk",
    "kLBkgccccCCCk",
    "kBBBkkkkkkkkk",
    ".kBBBBBBBBBk.",
    "..kkkkkkkkk..",
]
TORSO = [
    "..kWWWWWWk..",
    ".kWWLLLLWWk.",
    ".kWLBccBLWk.",
    ".kLBBccBBLk.",
    ".kBBBBBBBBk.",
    "..kgggggggk.",
    "..kGGGGGGk..",
]


def template(rows):
    out = {}
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch != ".":
                out[(x, y)] = CH.index(ch)
    return out


def seg_dist(px, py, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay
    l2 = vx * vx + vy * vy
    t = 0 if l2 == 0 else max(0, min(1, ((px - ax) * vx + (py - ay) * vy) / l2))
    cx, cy = ax + vx * t, ay + vy * t
    return math.hypot(px - cx, py - cy), t


def limb(points, radius, fill, light, dark=None):
    """関節列をなぞる太線。上側をハイライト、下側を影にする"""
    px = {}
    for y in range(H):
        for x in range(W):
            best = None
            for i in range(len(points) - 1):
                (ax, ay), (bx, by) = points[i], points[i + 1]
                d, t = seg_dist(x + 0.5, y + 0.5, ax + 0.5, ay + 0.5, bx + 0.5, by + 0.5)
                if best is None or d < best[0]:
                    best = (d, i, t)
            if best[0] <= radius:
                d, i, t = best
                (ax, ay), (bx, by) = points[i], points[i + 1]
                # 法線方向で明暗
                nx, ny = -(by - ay), (bx - ax)
                ln = math.hypot(nx, ny) or 1
                side = ((x + 0.5 - ax - 0.5) * nx + (y + 0.5 - ay - 0.5) * ny) / ln
                c = fill
                if side < -radius * 0.35:
                    c = light
                elif dark is not None and side > radius * 0.45:
                    c = dark
                px[(x, y)] = c
    return px


def rect_part(x0, y0, x1, y1, c, top=None):
    px = {}
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px[(x, y)] = top if (top is not None and y == y0) else c
    return px


def merge(*parts):
    out = {}
    for p in parts:
        out.update(p)
    return out


def shift(part, dx, dy):
    return {(x + dx, y + dy): c for (x, y), c in part.items()}


def compose(layers):
    """layers: 奥→手前の順のパーツ。各パーツに輪郭を付けて重ねる"""
    canvas = {}
    for part in layers:
        if part is None:
            continue
        own_outline = {p for p, c in part.items() if c == 1}
        for (x, y) in list(part.keys()):
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                q = (x + dx, y + dy)
                if q not in part and 0 <= q[0] < W and 0 <= q[1] < H:
                    canvas[q] = 1
        for p, c in part.items():
            if 0 <= p[0] < W and 0 <= p[1] < H:
                canvas[p] = c
    return canvas


def foot(ax, ay, back=False, toe=1):
    """足: 足首位置から前向きのブーツ"""
    c = CH.index("b") if back else CH.index("B")
    w = CH.index("G") if back else CH.index("W")
    return merge(rect_part(ax - 1, ay, ax + 1 + toe, ay + 1, c),
                 rect_part(ax - 1, ay + 1, ax + 1 + toe, ay + 1, w))


def fist(x, y, back=False):
    c = CH.index("G") if back else CH.index("h")
    return {(x, y): c, (x + 1, y): c, (x, y + 1): c, (x + 1, y + 1): CH.index("g")}


def blade(x0, y0, x1, y1, length_glow=True):
    """エネルギーブレード"""
    return limb([(x0, y0), (x1, y1)], 1.0, CH.index("C"), CH.index("W"), CH.index("c"))


IDX = {c: CH.index(c) for c in CH}


def make_frame(p):
    """p: ポーズ辞書"""
    bx, by = p.get("body", (0, 0))  # 胴体のオフセット
    lean = p.get("lean", 0)         # 頭の前後
    head = shift(template(HEAD), 10 + bx + lean, 4 + by)
    torso = shift(template(TORSO), 10 + bx, 14 + by)
    # マフラー (首の後ろからたなびく)
    scarf_pts = [(12 + bx, 14 + by)] + [(x + bx, y + by) for (x, y) in p.get("scarf", [(9, 15), (6, 16), (3, 18)])]
    scarf = limb(scarf_pts, 1.1, IDX["r"], IDX["o"], IDX["R"])
    collar = rect_part(12 + bx, 13 + by, 19 + bx, 14 + by, IDX["r"], IDX["o"])

    def leg(hip, knee, ankle, back):
        fill, light, dark = (IDX["b"], IDX["B"], None) if back else (IDX["B"], IDX["L"], IDX["b"])
        l = limb([hip, knee, ankle], 1.6, fill, light, dark)
        l[knee] = IDX["g"] if back else IDX["G"]
        return merge(l, foot(ankle[0], ankle[1], back, p.get("toe", 1)))

    def arm(sh, el, ha, back):
        fill, light = (IDX["b"], IDX["B"]) if back else (IDX["B"], IDX["L"])
        a = limb([sh, el, ha], 1.2, fill, light)
        # 肩アーマー
        sx, sy = sh
        pad = rect_part(sx - 1, sy - 1, sx + 1, sy, IDX["G"] if back else IDX["W"], IDX["h"] if back else IDX["W"])
        return merge(a, pad, fist(ha[0] - 1, ha[1], back))

    def off(pt):
        return (pt[0] + bx, pt[1] + by)

    L = p["legs"]   # ((hip,knee,ankle) 奥, (hip,knee,ankle) 手前)  ※脚は body オフセットを腰のみに適用
    A = p["arms"]   # ((sh,el,hand) 奥, (sh,el,hand) 手前)
    back_leg = leg(L[0][0], L[0][1], L[0][2], True)
    front_leg = leg(L[1][0], L[1][1], L[1][2], False)
    back_arm = arm(off(A[0][0]), off(A[0][1]), off(A[0][2]), True)
    front_arm = arm(off(A[1][0]), off(A[1][1]), off(A[1][2]), False)
    layers = [scarf, back_arm, back_leg, torso, front_leg, collar, head, front_arm]
    if "blade" in p:
        (x0, y0), (x1, y1) = p["blade"]
        layers.append(blade(x0 + bx, y0 + by, x1 + bx, y1 + by))
        layers.append(fist(off(A[1][2])[0] - 1, off(A[1][2])[1], False))
    if p.get("front_arm_behind"):
        layers = [scarf, back_arm, front_arm, back_leg, torso, front_leg, collar, head]
    c = compose(layers)
    rows = []
    for y in range(H):
        rows.append("".join(CH[c.get((x, y), 0)] for x in range(W)))
    return rows


# --- ポーズ定義 (右向き, 足裏 y=31) ---------------------------------------
def idle(b=0, scarf=None):
    return {
        "body": (0, b),
        "legs": (((13, 22 + b), (13, 26), (13, 29)), ((18, 22 + b), (18, 26), (18, 29))),
        "arms": (((10, 16), (9, 19), (9, 21)), ((21, 16), (22, 19), (22, 21))),
        "scarf": scarf or [(9, 15), (7, 17), (5, 19)],
    }


def run(i):
    # 6 フレーム: 脚の振り(角度)と上下動
    ph = i / 6.0 * 2 * math.pi
    bob = 0 if i in (1, 4) else (1 if i in (0, 3) else 0)
    def legpos(phase, hipx):
        sw = math.sin(phase)
        lift = max(0, math.cos(phase))
        hip = (hipx, 22 + bob)
        ankle = (int(round(hipx + sw * 5)), int(round(29 - lift * 3)))
        knee = (int(round(hipx + sw * 2 + 2 * lift)), int(round(26 - lift * 1)))
        return (hip, knee, ankle)
    front = legpos(ph, 17)
    back = legpos(ph + math.pi, 14)
    asw = math.sin(ph)
    return {
        "body": (1, bob),
        "lean": 1,
        "legs": (back, front),
        "arms": (((10, 16), (int(9 + asw * 2), 19), (int(10 + asw * 4), 20)),
                 ((21, 16), (int(21 - asw * 2), 19), (int(21 - asw * 4), 21))),
        "scarf": [(8, 14 + (i % 2)), (5, 14 + ((i + 1) % 2)), (2, 15 + (i % 3 == 0))],
    }


POSES = {}
POSES["player_idle0"] = idle(0)
POSES["player_idle1"] = idle(1, [(9, 16), (7, 18), (5, 20)])
for i in range(6):
    POSES[f"player_run{i}"] = run(i)
POSES["player_jump"] = {
    "body": (0, -1),
    "legs": (((13, 21), (12, 24), (11, 27)), ((18, 21), (20, 24), (18, 27))),
    "arms": (((10, 15), (8, 13), (7, 11)), ((21, 15), (24, 14), (25, 12))),
    "scarf": [(9, 17), (8, 20), (7, 23)],
}
POSES["player_fall"] = {
    "body": (0, -1),
    "legs": (((13, 21), (12, 25), (12, 28)), ((18, 21), (20, 24), (20, 27))),
    "arms": (((10, 15), (7, 14), (5, 13)), ((21, 15), (24, 15), (26, 14))),
    "scarf": [(10, 11), (8, 9), (6, 8)],
}
# 攻撃1: 横薙ぎ (腕を前へ伸ばしブレードを水平に)
POSES["player_atk1"] = {
    "body": (1, 0), "lean": 1,
    "legs": (((13, 22), (11, 26), (10, 29)), ((18, 22), (21, 26), (22, 29))),
    "arms": (((10, 16), (8, 18), (7, 19)), ((21, 16), (25, 17), (27, 17))),
    "blade": ((28, 16), (31, 14)),
    "scarf": [(8, 14), (5, 13), (2, 13)],
}
# 攻撃2: 振り上げ
POSES["player_atk2"] = {
    "body": (0, 0), "lean": 0,
    "legs": (((13, 22), (12, 26), (11, 29)), ((18, 22), (20, 26), (21, 29))),
    "arms": (((10, 16), (8, 19), (8, 21)), ((21, 16), (24, 13), (25, 9))),
    "blade": ((25, 7), (27, 2)),
    "scarf": [(8, 16), (5, 17), (2, 19)],
}
# 攻撃3: 叩きつけ (大きく踏み込む)
POSES["player_atk3"] = {
    "body": (2, 2), "lean": 1,
    "legs": (((13, 23), (10, 26), (8, 29)), ((19, 23), (23, 26), (24, 29))),
    "arms": (((10, 16), (8, 17), (6, 17)), ((21, 16), (25, 19), (27, 22))),
    "blade": ((28, 23), (31, 27)),
    "scarf": [(7, 13), (4, 11), (1, 10)],
}
# 空中攻撃: 回転斬り
POSES["player_airatk"] = {
    "body": (0, -1), "lean": 0,
    "legs": (((13, 21), (11, 24), (12, 27)), ((18, 21), (21, 23), (21, 26))),
    "arms": (((10, 15), (8, 17), (8, 19)), ((21, 15), (25, 16), (27, 19))),
    "blade": ((28, 20), (30, 25)),
    "scarf": [(9, 12), (7, 10), (4, 10)],
}
POSES["player_hurt"] = {
    "body": (-1, 1), "lean": -1,
    "legs": (((12, 23), (11, 26), (10, 29)), ((17, 23), (19, 26), (21, 28))),
    "arms": (((10, 16), (7, 14), (6, 12)), ((21, 16), (23, 14), (25, 12))),
    "scarf": [(10, 11), (9, 8), (8, 6)],
}
POSES["player_dash"] = {
    "body": (2, 2), "lean": 2,
    "legs": (((14, 23), (10, 25), (6, 26)), ((19, 23), (22, 26), (23, 29))),
    "arms": (((11, 17), (7, 18), (4, 18)), ((22, 17), (19, 19), (16, 20))),
    "scarf": [(7, 13), (3, 12), (0, 12)],
    "front_arm_behind": False,
}


def main():
    out = ["# 自動生成: tools/compose_player.py (ポーズはそちらを編集)"]
    for name, pose in POSES.items():
        out.append(f"@{name} 32 32")
        out += make_frame(pose)
        out.append("")
    path = os.path.join(ROOT, "assets", "sprites_player.txt")
    with open(path, "w") as f:
        f.write("\n".join(out))
    print(f"wrote {path} ({len(POSES)} frames)")


if __name__ == "__main__":
    main()
