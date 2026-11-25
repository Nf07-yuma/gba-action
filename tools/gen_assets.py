#!/usr/bin/env python3
"""STEEL CHAIN アセット生成ツール

assets/ 以下のテキスト(スプライトのASCIIアート・ステージ定義)と
手続き生成のタイル/背景から、GBA用の C ソース (assets.c / assets.h) を生成する。
外部ライブラリ不要 (プレビュー出力のみ Pillow があれば使う)。
"""
import os
import sys
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")

# ---------------------------------------------------------------------------
# パレット (RGB 0-31)
# ---------------------------------------------------------------------------
# スプライト共通パレット: ASCII 文字 -> インデックス
SPR_CHARS = ".kbBLWgGhcCyorRe"
SPR_PAL = [
    (0, 0, 0),       # . 透明
    (2, 2, 5),       # k 輪郭
    (3, 6, 15),      # b 濃い青
    (6, 12, 26),     # B 青
    (13, 21, 31),    # L 水色
    (29, 30, 31),    # W 白
    (8, 9, 12),      # g 濃い灰
    (15, 16, 20),    # G 灰
    (23, 24, 27),    # h 明るい灰
    (4, 24, 28),     # c シアン
    (18, 31, 31),    # C 明るいシアン
    (31, 27, 6),     # y 黄
    (31, 15, 3),     # o 橙
    (25, 4, 5),      # r 赤
    (12, 2, 4),      # R 暗い赤
    (6, 28, 10),     # e 緑
]
# 被弾フラッシュ用 (全部白)
SPR_PAL_FLASH = [(0, 0, 0)] + [(31, 31, 31)] * 15
# 敵バリエーション(紫系): 赤系を紫に置換
SPR_PAL_ALT = list(SPR_PAL)
SPR_PAL_ALT[13] = (20, 6, 26)
SPR_PAL_ALT[14] = (9, 2, 14)
SPR_PAL_ALT[12] = (28, 10, 31)
# ダメージ受けた時のプレイヤー (赤み)
SPR_PAL_HURT = [(0, 0, 0)] + [(min(31, r + 10), g // 2, b // 2) for (r, g, b) in SPR_PAL[1:]]

# ダッシュ残像 (シアンの濃淡)
SPR_PAL_GHOST = [(0, 0, 0)] + [(0, min(31, 6 + (r + g + b) // 5), min(31, 10 + (r + g + b) // 4)) for (r, g, b) in SPR_PAL[1:]]

# BG パレット 0: ステージタイル (工業地帯)
BG_PAL_LEVEL = [
    (3, 2, 8),       # 0 背景色(夜空)
    (1, 1, 3),       # 1 輪郭
    (5, 5, 10),      # 2 鋼・最暗
    (8, 8, 15),      # 3 鋼・暗
    (12, 12, 20),    # 4 鋼
    (17, 17, 25),    # 5 鋼・明
    (24, 24, 30),    # 6 鋼・ハイライト
    (31, 25, 4),     # 7 警告黄
    (8, 6, 2),       # 8 警告黒
    (4, 22, 28),     # 9 ライト シアン
    (18, 31, 31),    # 10 ライト 明
    (26, 5, 8),      # 11 赤ランプ
    (31, 16, 18),    # 12 赤ランプ明
    (4, 4, 8),       # 13 奥の壁 暗
    (7, 6, 12),      # 14 奥の壁
    (10, 9, 16),     # 15 奥の壁 明
]
# BG パレット 1: 遠景 (夜の都市)
BG_PAL_SKY = [
    (3, 2, 8),       # 0
    (4, 3, 10),      # 1 空 上
    (6, 3, 12),      # 2 空 中
    (9, 4, 14),      # 3 空 下 (地平線の光)
    (13, 5, 15),     # 4 地平線
    (2, 2, 6),       # 5 ビル 最遠
    (3, 3, 8),       # 6 ビル 遠
    (4, 4, 10),      # 7 ビル 近
    (20, 18, 8),     # 8 窓 黄
    (8, 20, 24),     # 9 窓 シアン
    (26, 8, 18),     # 10 ネオン ピンク
    (31, 31, 24),    # 11 星
    (6, 5, 12),      # 12 ビル 縁
    (14, 12, 6),     # 13 窓 暗
    (5, 12, 15),     # 14 窓 シアン暗
    (31, 20, 28),    # 15 ネオン 明
]
# BG パレット 2: HUD / 文字
BG_PAL_HUD = [
    (0, 0, 0),       # 0 透明
    (1, 1, 3),       # 1 輪郭
    (31, 31, 31),    # 2 白
    (18, 31, 31),    # 3 シアン
    (31, 27, 6),     # 4 黄
    (28, 6, 6),      # 5 赤
    (6, 26, 10),     # 6 緑
    (8, 8, 14),      # 7 枠 暗
    (14, 14, 22),    # 8 枠
    (22, 22, 28),    # 9 枠 明
    (31, 15, 3),     # 10 橙
    (4, 10, 14),     # 11 ゲージ空
    (12, 2, 4),      # 12 暗赤
    (4, 18, 24),     # 13 シアン暗
    (20, 20, 20),    # 14 灰
    (31, 31, 31),    # 15
]
# BG パレット 3: HUD 文字 (黄色版)
BG_PAL_HUD_Y = list(BG_PAL_HUD)
BG_PAL_HUD_Y[2] = (31, 27, 6)
BG_PAL_HUD_Y[3] = (31, 15, 3)
# BG パレット 4: HUD 文字 (シアン版)
BG_PAL_HUD_C = list(BG_PAL_HUD)
BG_PAL_HUD_C[2] = (18, 31, 31)
BG_PAL_HUD_C[3] = (4, 18, 24)


# BG パレット 5: タイトルロゴ (鋼のグラデーション + 赤い鎖)
BG_PAL_LOGO = [
    (0, 0, 0),       # 0 透明
    (1, 1, 4),       # 1 輪郭
    (31, 31, 31),    # 2 ハイライト
    (22, 26, 31),    # 3 鋼 明
    (14, 18, 27),    # 4 鋼
    (8, 10, 19),     # 5 鋼 暗
    (4, 5, 12),      # 6 影
    (31, 10, 8),     # 7 赤
    (20, 3, 5),      # 8 赤 暗
    (31, 22, 10),    # 9 赤 明
    (18, 31, 31),    # 10 シアン
    (6, 20, 26),     # 11 シアン暗
    (0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0),
]


def gen_logo():
    """タイトルロゴ 'STEEL CHAIN' (240x48)。5x7 フォントを 4 倍に拡大し、ベベルと輪郭を付ける"""
    W, H = 240, 48
    p = blank(W, H, 0)
    SX, SY = 3, 4

    def word(text, x0, y0, chain=False):
        x = x0
        mask = set()
        for ch in text:
            src = FONT_SRC[ch]
            for gy, row in enumerate(src):
                for gx, b in enumerate(row):
                    if b == "1":
                        for dy in range(SY):
                            for dx in range(SX):
                                mask.add((x + gx * SX + dx, y0 + gy * SY + dy))
            x += 6 * SX
        return mask

    m1 = word("STEEL", 22, 8)
    m2 = word("CHAIN", 130, 8)
    for mask, red in ((m1, False), (m2, True)):
        for (x, y) in mask:
            ry = y - 8
            if red:
                c = 9 if ry < 6 else (7 if ry < 20 else 8)
            else:
                c = 2 if ry < 3 else (3 if ry < 10 else (4 if ry < 20 else 5))
            # 右下の縁は暗く
            if (x + 1, y) not in mask or (x, y + 1) not in mask:
                c = 8 if red else 6
            if (x - 1, y) not in mask or (x, y - 1) not in mask:
                c = 9 if red else 2
            p[y][x] = c
        for (x, y) in mask:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (2, 2)):
                q = (x + dx, y + dy)
                if q not in mask and 0 <= q[0] < W and 0 <= q[1] < H and p[q[1]][q[0]] == 0:
                    p[q[1]][q[0]] = 1
    # 下線: シアンのエネルギーライン
    for x in range(20, W - 20):
        p[42][x] = 10 if (x // 4) % 2 == 0 else 11
        p[43][x] = 11
    return p


# ---- ステージ配色テーマ ----
def _mix(c, t, k):
    return tuple(max(0, min(31, int(round(a + (b - a) * k)))) for a, b in zip(c, t))


def _theme(level_tint, level_k, sky_map, light=None):
    lv = [_mix(c, level_tint, level_k) for c in BG_PAL_LEVEL]
    if light:
        lv[9], lv[10] = light
    sky = [sky_map(c) for c in BG_PAL_SKY]
    return lv, sky


THEMES = [
    # 0: 工場の夜 (そのまま)
    (list(BG_PAL_LEVEL), list(BG_PAL_SKY)),
    # 1: 夕暮れのスカイウェイ (空が橙〜紫)
    _theme((20, 14, 12), 0.18, lambda c: (min(31, c[0] + c[2] // 2 + 2), min(31, c[1] + 2), max(0, c[2] - 2)),
           ((31, 20, 6), (31, 29, 18))),
    # 2: 灼熱の鋳造所 (赤みの鋼, 橙の照明)
    _theme((26, 8, 4), 0.22, lambda c: (min(31, c[0] + 4), c[1] // 2, c[2] // 2), ((31, 14, 2), (31, 26, 10))),
    # 3: データハイウェイ (緑のネオン)
    _theme((4, 18, 10), 0.2, lambda c: (c[0] // 3, min(31, c[1] + 3), c[2] // 2 + 2), ((6, 31, 12), (24, 31, 24))),
    # 4: 中枢コア (紫とシアン)
    _theme((16, 6, 24), 0.25, lambda c: (min(31, c[0] + 3), c[1] // 2, min(31, c[2] + 6)), ((26, 10, 31), (31, 24, 31))),
]


def rgb15(c):
    r, g, b = c
    return r | (g << 5) | (b << 10)


# ---------------------------------------------------------------------------
# 汎用: 8x8 タイル化
# ---------------------------------------------------------------------------
def tile_bytes(pix, x0, y0):
    """pix[y][x] (0-15) の 8x8 領域を 4bpp 32 バイトへ"""
    out = []
    for y in range(8):
        for x in range(0, 8, 2):
            lo = pix[y0 + y][x0 + x] & 15
            hi = pix[y0 + y][x0 + x + 1] & 15
            out.append(lo | (hi << 4))
    return bytes(out)


def sprite_to_tiles(pix, w, h):
    """スプライト画像を 1D マッピング順のタイル列へ"""
    data = b""
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            data += tile_bytes(pix, tx, ty)
    return data


def tileize(pix, w, h, tiles, tile_index, base=0, pal=0):
    """画像をタイル化し重複排除。tiles/tile_index を更新し map(u16) を返す"""
    m = []
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            t = tile_bytes(pix, tx, ty)
            if t not in tile_index:
                tile_index[t] = len(tiles)
                tiles.append(t)
            m.append((base + tile_index[t]) | (pal << 12))
    return m


# ---------------------------------------------------------------------------
# スプライト (ASCII アート)
# ---------------------------------------------------------------------------
def load_sprites(path):
    sprites = []  # (name, w, h, pix)
    cur = None
    with open(path, encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            if line.startswith("#") or (not line.strip() and cur is None):
                continue
            if line.startswith("@"):
                parts = line[1:].split()
                name, w, h = parts[0], int(parts[1]), int(parts[2])
                cur = [name, w, h, []]
                sprites.append(cur)
                continue
            if cur is None:
                continue
            if not line.strip():
                continue
            row = line.strip()
            if len(row) != cur[1]:
                sys.exit(f"{path}:{lineno}: {cur[0]} の行幅 {len(row)} != {cur[1]}")
            try:
                cur[3].append([SPR_CHARS.index(ch) for ch in row])
            except ValueError as e:
                sys.exit(f"{path}:{lineno}: 不明な文字 {e}")
    out = []
    for name, w, h, rows in sprites:
        if len(rows) != h:
            sys.exit(f"{path}: {name} の行数 {len(rows)} != {h}")
        out.append((name, w, h, rows))
    return out


def mirror(pix):
    return [list(reversed(r)) for r in pix]


# ---------------------------------------------------------------------------
# ステージタイル (手続き生成, 16x16 メタタイル)
# ---------------------------------------------------------------------------
def blank(w, h, c=0):
    return [[c] * w for _ in range(h)]


def rect(p, x0, y0, x1, y1, c):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            p[y][x] = c


def gen_metatiles():
    """メタタイル(16x16)の画像を生成して返す: name -> pix"""
    mt = {}
    rng = random.Random(1234)

    # 空
    mt["empty"] = blank(16, 16, 0)

    # 鋼ブロック(内部)
    def steel(top=False, left=False, right=False, bottom=False, variant=0):
        p = blank(16, 16, 4)
        # パネル: 明暗の縁取り
        rect(p, 0, 0, 15, 0, 5)
        rect(p, 0, 0, 0, 15, 5)
        rect(p, 15, 0, 15, 15, 3)
        rect(p, 0, 15, 15, 15, 3)
        rect(p, 1, 1, 14, 1, 6 if variant == 0 else 5)
        # 中央のプレート
        if variant == 0:
            rect(p, 3, 4, 12, 11, 3)
            rect(p, 4, 5, 11, 10, 4)
            rect(p, 4, 5, 11, 5, 5)
        elif variant == 1:
            for y in range(3, 13, 3):
                rect(p, 2, y, 13, y, 3)
                rect(p, 2, y + 1, 13, y + 1, 5)
        else:
            rect(p, 2, 2, 13, 13, 3)
            for i in range(2, 14):
                p[i][i] = 5
                p[i][15 - i] = 5
        # リベット
        for (x, y) in ((2, 2), (13, 2), (2, 13), (13, 13)):
            p[y][x] = 6
            p[y + 1 if y < 15 else y][x] = 2
        if top:
            # 地表: 警告ストライプ
            rect(p, 0, 0, 15, 3, 8)
            for x in range(16):
                for y in range(0, 4):
                    if ((x + y) // 3) % 2 == 0:
                        p[y][x] = 7
            rect(p, 0, 0, 15, 0, 6)
            rect(p, 0, 4, 15, 4, 1)
        if left:
            rect(p, 0, 0 if not top else 1, 0, 15, 1)
        if right:
            rect(p, 15, 0 if not top else 1, 15, 15, 1)
        if bottom:
            rect(p, 0, 15, 15, 15, 1)
        return p

    mt["steel"] = steel()
    mt["steel_b"] = steel(variant=1)
    mt["steel_c"] = steel(variant=2)
    mt["steel_top"] = steel(top=True)
    mt["steel_top_l"] = steel(top=True, left=True)
    mt["steel_top_r"] = steel(top=True, right=True)
    mt["steel_top_lr"] = steel(top=True, left=True, right=True)
    mt["steel_l"] = steel(left=True)
    mt["steel_r"] = steel(right=True)

    # 足場(一方通行): 鉄骨トラス
    p = blank(16, 16, 0)
    rect(p, 0, 0, 15, 0, 6)
    rect(p, 0, 1, 15, 1, 5)
    rect(p, 0, 2, 15, 2, 3)
    rect(p, 0, 7, 15, 7, 3)
    rect(p, 0, 8, 15, 8, 1)
    for x in range(16):
        y = 3 + (x % 8) // 2 if (x // 8) % 2 == 0 else 6 - (x % 8) // 2
        if 3 <= y <= 6:
            p[y][x] = 4
    for x in (0, 8):
        rect(p, x, 3, x, 6, 4)
    mt["girder"] = p

    # トゲ(電撃床): セル上端近くまで届く鋭い突起
    p = blank(16, 16, 0)
    for i in range(4):
        cx = i * 4
        for y in range(2, 14):
            half = (y - 2) // 6
            for x in range(cx + 1 - half, cx + 3 + half):
                if 0 <= x < 16:
                    p[y][x] = 6 if y < 4 else (5 if x <= cx + 1 else 3)
    rect(p, 0, 13, 15, 13, 11)
    rect(p, 0, 14, 15, 15, 2)
    for x in range(0, 16, 4):
        p[12][x] = 12
    mt["spikes"] = p

    # 奥の壁 (通過可能, 装飾)
    def backwall(kind):
        p = blank(16, 16, 13)
        rect(p, 0, 0, 15, 0, 14)
        rect(p, 0, 0, 0, 15, 14)
        if kind == 0:
            # パイプ
            rect(p, 5, 0, 10, 15, 14)
            rect(p, 6, 0, 6, 15, 15)
            rect(p, 10, 0, 10, 15, 2)
            rect(p, 4, 6, 11, 8, 15)
            rect(p, 4, 8, 11, 8, 2)
        elif kind == 1:
            # 窓 + ランプ
            rect(p, 3, 3, 12, 10, 1)
            rect(p, 4, 4, 11, 9, 2)
            for x in range(4, 12):
                p[4 + (x % 3)][x] = 9 if x % 4 else 10
            p[13][7] = 11
            p[13][8] = 12
        else:
            # 格子
            for y in range(2, 16, 4):
                rect(p, 0, y, 15, y, 14)
            for x in range(2, 16, 4):
                rect(p, x, 0, x, 15, 14)
        return p

    mt["back_pipe"] = backwall(0)
    mt["back_window"] = backwall(1)
    mt["back_grid"] = backwall(2)
    mt["back_plain"] = blank(16, 16, 13)

    # ゴールゲート(背景タイル装飾) は スプライトで描くので省略
    return mt


METATILE_ORDER = [
    "empty", "steel", "steel_b", "steel_c", "steel_top", "steel_top_l", "steel_top_r",
    "steel_top_lr", "steel_l", "steel_r", "girder", "spikes",
    "back_pipe", "back_window", "back_grid", "back_plain",
]

# ---------------------------------------------------------------------------
# 遠景 (256x256, 夜の都市)
# ---------------------------------------------------------------------------
def gen_skyline():
    W, H = 256, 256
    rng = random.Random(42)
    p = blank(W, H, 1)
    # 空のグラデーション(ディザ)
    bands = [(0, 1), (70, 2), (110, 3), (135, 4)]
    for y in range(H):
        c = 1
        for (yy, cc) in bands:
            if y >= yy:
                c = cc
        for x in range(W):
            p[y][x] = c
            # ディザで境界を柔らかく
            for (yy, cc) in bands[1:]:
                if yy - 6 <= y < yy and (x + y) % 2 == 0 and (yy - y) < 4:
                    p[y][x] = cc
    # 星
    for _ in range(24):
        x, y = rng.randrange(W), rng.randrange(0, 90)
        p[y][x] = 11 if rng.random() < 0.3 else 3
    # ビル(3層)
    def buildings(layer_col, edge_col, base_y, hmin, hmax, wmin, wmax, win_cols, win_rate):
        x = 0
        while x < W:
            bw = rng.randint(wmin, wmax)
            bh = rng.randint(hmin, hmax)
            top = base_y - bh
            for yy in range(top, H):
                for xx in range(x, min(W, x + bw)):
                    p[yy][xx] = layer_col
            for xx in range(x, min(W, x + bw)):
                p[top][xx] = edge_col
            # アンテナ
            if rng.random() < 0.3:
                ax = x + bw // 2
                for yy in range(top - rng.randint(3, 8), top):
                    if 0 <= ax < W:
                        p[yy][ax] = edge_col
            # 窓
            for wy in range(top + 3, H - 2, 4):
                for wx in range(x + 2, x + bw - 2, 3):
                    if wx < W and rng.random() < win_rate:
                        col = rng.choice(win_cols)
                        p[wy][wx] = col
                        if wy + 1 < H:
                            p[wy + 1][wx] = col
            x += bw + rng.randint(0, 3)

    buildings(5, 6, 200, 40, 100, 12, 28, [13, 14], 0.15)
    buildings(6, 12, 225, 30, 90, 14, 30, [8, 9, 13, 14], 0.3)
    buildings(7, 12, 256, 30, 80, 18, 36, [8, 9, 10], 0.35)
    # ネオン看板
    for _ in range(5):
        x = rng.randrange(8, W - 16)
        y = rng.randrange(150, 210)
        if p[y][x] in (6, 7):
            for i in range(rng.randint(5, 10)):
                if x + i < W:
                    p[y][x + i] = 10 if i % 2 else 15
    return p


# ---------------------------------------------------------------------------
# フォント 5x7 (8x8 タイルに輪郭付きで描画)
# ---------------------------------------------------------------------------
FONT_SRC = {
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01111"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "I": ["01110", "00100", "00100", "00100", "00100", "00100", "01110"],
    "J": ["00111", "00010", "00010", "00010", "00010", "10010", "01100"],
    "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["10001", "10001", "11001", "10101", "10011", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "W": ["10001", "10001", "10001", "10101", "10101", "10101", "01010"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11111", "00010", "00100", "00010", "00001", "10001", "01110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    "!": ["00100", "00100", "00100", "00100", "00100", "00000", "00100"],
    "?": ["01110", "10001", "00001", "00010", "00100", "00000", "00100"],
    ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
    ",": ["00000", "00000", "00000", "00000", "01100", "00100", "01000"],
    ":": ["00000", "01100", "01100", "00000", "01100", "01100", "00000"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    "+": ["00000", "00100", "00100", "11111", "00100", "00100", "00000"],
    "/": ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
    "'": ["00100", "00100", "01000", "00000", "00000", "00000", "00000"],
    "x": ["00000", "00000", "10001", "01010", "00100", "01010", "10001"],
    ">": ["01000", "00100", "00010", "00001", "00010", "00100", "01000"],
    "<": ["00010", "00100", "01000", "10000", "01000", "00100", "00010"],
    "(": ["00010", "00100", "01000", "01000", "01000", "00100", "00010"],
    ")": ["01000", "00100", "00010", "00010", "00010", "00100", "01000"],
    "#": ["01010", "01010", "11111", "01010", "11111", "01010", "01010"],
    "*": ["00000", "10101", "01110", "11111", "01110", "10101", "00000"],
    "=": ["00000", "00000", "11111", "00000", "11111", "00000", "00000"],
}
FONT_FIRST = 32
FONT_LAST = 127


def gen_font():
    """ASCII 32..127 の 8x8 タイル (色2=本体, 色3=下側影, 色1=輪郭)"""
    tiles = []
    for code in range(FONT_FIRST, FONT_LAST + 1):
        ch = chr(code)
        src = FONT_SRC.get(ch) or FONT_SRC.get(ch.upper())
        p = blank(8, 8, 0)
        if src:
            body = [[0] * 8 for _ in range(8)]
            for y, row in enumerate(src):
                for x, b in enumerate(row):
                    if b == "1":
                        body[y][x + 1] = 1
            # 輪郭
            for y in range(8):
                for x in range(8):
                    if body[y][x]:
                        continue
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            yy, xx = y + dy, x + dx
                            if 0 <= yy < 8 and 0 <= xx < 8 and body[yy][xx]:
                                p[y][x] = 1
            for y in range(8):
                for x in range(8):
                    if body[y][x]:
                        p[y][x] = 3 if y >= 5 else 2
        tiles.append(tile_bytes(p, 0, 0))
    return tiles


def gen_hud_tiles():
    """HUD 用の特殊タイル: HPゲージ(空/満/半), 枠"""
    t = {}
    # HP セル: 満
    p = blank(8, 8, 0)
    rect(p, 0, 1, 7, 6, 1)
    rect(p, 1, 2, 6, 5, 6)
    rect(p, 1, 2, 6, 2, 2)
    t["hp_full"] = p
    p = blank(8, 8, 0)
    rect(p, 0, 1, 7, 6, 1)
    rect(p, 1, 2, 6, 5, 11)
    t["hp_empty"] = p
    p = blank(8, 8, 0)
    rect(p, 0, 1, 7, 6, 1)
    rect(p, 1, 2, 6, 5, 5)
    rect(p, 1, 2, 6, 2, 4)
    t["hp_low"] = p
    # ゲージ左端 "HP" ラベル枠
    p = blank(8, 8, 0)
    rect(p, 0, 0, 7, 7, 1)
    rect(p, 1, 1, 7, 6, 8)
    rect(p, 1, 1, 7, 1, 9)
    for (x, y) in ((2, 2), (2, 3), (2, 4), (2, 5), (3, 3), (4, 2), (4, 3), (4, 4), (4, 5)):
        p[y][x] = 3
    for (x, y) in ((5, 2), (5, 3), (5, 4), (5, 5), (6, 2), (7, 3), (6, 3)):
        p[y][x] = 3
    t["hp_label"] = p
    # 半透明風の黒帯
    p = blank(8, 8, 1)
    t["solid_dark"] = p
    p = blank(8, 8, 7)
    rect(p, 0, 0, 7, 0, 8)
    t["panel"] = p
    # ロボの顔アイコン(残機)
    p = blank(8, 8, 0)
    rect(p, 1, 0, 6, 6, 1)
    rect(p, 2, 1, 5, 5, 2)
    rect(p, 2, 3, 5, 3, 3)
    rect(p, 1, 7, 6, 7, 1)
    t["life_icon"] = p
    # ボス HP ゲージ
    p = blank(8, 8, 0)
    rect(p, 0, 2, 7, 5, 1)
    rect(p, 0, 3, 7, 4, 5)
    rect(p, 0, 3, 7, 3, 10)
    t["boss_full"] = p
    p = blank(8, 8, 0)
    rect(p, 0, 2, 7, 5, 1)
    rect(p, 0, 3, 7, 4, 12)
    t["boss_empty"] = p
    return t


HUD_TILE_ORDER = ["hp_full", "hp_empty", "hp_low", "hp_label", "solid_dark", "panel", "life_icon", "boss_full", "boss_empty"]

# ---------------------------------------------------------------------------
# ステージ
# ---------------------------------------------------------------------------
CELL_EMPTY, CELL_SOLID, CELL_ONEWAY, CELL_HAZARD, CELL_BACK = 0, 1, 2, 3, 4
OBJ_TYPES = {
    "c": 1,  # クローラー
    "d": 2,  # ドローン
    "t": 3,  # タレット
    "C": 4,  # チェックポイント
    "G": 5,  # ゴール
    "+": 6,  # 回復
    "j": 7,  # ジャンパー
    "B": 8,  # ボス
    "s": 9,  # シールド兵
    "L": 10,  # 横に動く足場
    "V": 11,  # 縦に動く足場
    "l": 10,  # 横に動く足場 (逆位相)
    "v": 11,  # 縦に動く足場 (逆位相)
    "Z": 8,   # 最終ボス
}
OBJ_PARAMS = {"l": 32, "v": 32, "Z": 1}
BACK_KINDS = {"p": 0, "w": 1, "g": 2, "b": 3}


def load_level(path):
    rows = []
    meta = {}
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if line.startswith(";"):
                k, _, v = line[1:].partition("=")
                meta[k.strip()] = v.strip()
                continue
            if line.startswith("|"):
                rows.append(line[1:].rstrip("|"))
    h = len(rows)
    w = max(len(r) for r in rows)
    rows = [r.ljust(w, " ") for r in rows]
    cells = []
    backs = []
    objs = []
    spawn = (2, h - 3)
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            c = CELL_EMPTY
            back = 0xFF
            if ch == "#":
                c = CELL_SOLID
            elif ch == "=":
                c = CELL_ONEWAY
            elif ch == "^":
                c = CELL_HAZARD
            elif ch in BACK_KINDS:
                c = CELL_BACK
                back = BACK_KINDS[ch]
            elif ch == "P":
                spawn = (x, y)
            elif ch in OBJ_TYPES:
                objs.append((OBJ_TYPES[ch], OBJ_PARAMS.get(ch, 0), x, y))
            cells.append(c)
            backs.append(back)
    # オブジェクトを置いたマスは左右の背景装飾を引き継ぐ (壁に穴が空かないように)
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch in OBJ_TYPES or ch == "P":
                i = y * w + x
                for nx in (x - 1, x + 1, x - 2, x + 2):
                    if 0 <= nx < w and cells[y * w + nx] == CELL_BACK:
                        cells[i] = CELL_BACK
                        backs[i] = backs[y * w + nx]
                        break
    # 背景装飾の種類を cells の上位ビットへ
    packed = []
    for c, b in zip(cells, backs):
        if c == CELL_BACK:
            packed.append(c | (b << 4))
        else:
            packed.append(c)
    return {
        "w": w, "h": h, "cells": packed, "spawn": spawn, "objs": objs,
        "name": meta.get("name", os.path.basename(path)),
        "theme": int(meta.get("theme", "0")),
    }


# ---------------------------------------------------------------------------
# 音楽 (簡易トラッカー形式)
# ---------------------------------------------------------------------------
NOTE_BASE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
MUS_HOLD, MUS_OFF = 0, 1
DRUMS = {".": 0, "K": 1, "S": 2, "H": 3}


def parse_note(tok, where):
    if tok == "---":
        return MUS_HOLD
    if tok == "...":
        return MUS_OFF
    try:
        n = NOTE_BASE[tok[0]]
        if tok[1] == "#":
            n += 1
        octave = int(tok[2])
    except (KeyError, IndexError, ValueError):
        sys.exit(f"{where}: 不明な音符 {tok}")
    midi = 12 * (octave + 1) + n
    if not 24 <= midi <= 108:
        sys.exit(f"{where}: 音域外 {tok}")
    return midi


def load_song(path):
    speed = 8
    loop = 1
    chans = {"lead": [], "bass": [], "drum": []}
    cur = None
    with open(path, encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.split(";")[0].strip() if not raw.startswith(";speed") else raw.strip()
            if raw.startswith(";speed"):
                speed = int(raw.split("=")[1])
                continue
            if raw.startswith(";loop"):
                loop = int(raw.split("=")[1])
                continue
            if not line:
                continue
            if line.startswith("["):
                cur = line.strip("[]")
                continue
            where = f"{path}:{lineno}"
            for tok in line.split():
                if cur == "drum":
                    for ch in tok:
                        if ch not in DRUMS:
                            sys.exit(f"{where}: 不明なドラム {ch}")
                        chans["drum"].append(DRUMS[ch])
                else:
                    chans[cur].append(parse_note(tok, where))
    for k in chans:
        if not chans[k]:
            chans[k] = [MUS_OFF]
    return speed, loop, chans


def freq_tables():
    sq, wv = [], []
    for m in range(128):
        f = 440.0 * 2 ** ((m - 69) / 12)
        sq.append(max(0, min(2047, int(round(2048 - 131072 / f)))))
        wv.append(max(0, min(2047, int(round(2048 - 65536 / f)))))
    return sq, wv


# ---------------------------------------------------------------------------
# C 出力
# ---------------------------------------------------------------------------
def c_bytes_as_u32(data):
    assert len(data) % 4 == 0
    words = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]
    lines = []
    for i in range(0, len(words), 8):
        lines.append("    " + ", ".join(f"0x{w:08X}" for w in words[i:i + 8]) + ",")
    return "\n".join(lines)


def c_u16(vals, per=16):
    lines = []
    for i in range(0, len(vals), per):
        lines.append("    " + ", ".join(f"0x{v:04X}" for v in vals[i:i + per]) + ",")
    return "\n".join(lines)


def c_u8(vals, per=32):
    lines = []
    for i in range(0, len(vals), per):
        lines.append("    " + ",".join(str(v) for v in vals[i:i + per]) + ",")
    return "\n".join(lines)


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "gen")
    os.makedirs(outdir, exist_ok=True)
    H = []
    C = ['#include "assets.h"', ""]
    H += ["/* 自動生成: tools/gen_assets.py (編集しないこと) */", "#ifndef ASSETS_H", "#define ASSETS_H",
          '#include "gba.h"', ""]

    # ---- パレット
    def pal_block(name, pals):
        vals = []
        for p in pals:
            vals += [rgb15(c) for c in p]
        H.append(f"extern const u16 {name}[{len(vals)}];")
        C.append(f"const u16 {name}[{len(vals)}] = {{\n{c_u16(vals)}\n}};")

    pal_block("pal_obj", [SPR_PAL, SPR_PAL_FLASH, SPR_PAL_ALT, SPR_PAL_HURT, SPR_PAL_GHOST])
    pal_block("pal_bg", [BG_PAL_LEVEL, BG_PAL_SKY, BG_PAL_HUD, BG_PAL_HUD_Y, BG_PAL_HUD_C, BG_PAL_LOGO])
    tv = []
    for lv, sky in THEMES:
        tv += [rgb15(c) for c in lv] + [rgb15(c) for c in sky]
    H.append(f"#define THEME_COUNT {len(THEMES)}")
    H.append(f"extern const u16 theme_pals[THEME_COUNT * 32];")
    C.append(f"const u16 theme_pals[THEME_COUNT * 32] = {{\n{c_u16(tv)}\n}};")
    H += ["#define OBJPAL_NORMAL 0", "#define OBJPAL_FLASH 1", "#define OBJPAL_ALT 2", "#define OBJPAL_HURT 3", "#define OBJPAL_GHOST 4",
          "#define BGPAL_LEVEL 0", "#define BGPAL_SKY 1", "#define BGPAL_HUD 2", "#define BGPAL_HUD_Y 3",
          "#define BGPAL_HUD_C 4", "#define BGPAL_LOGO 5", ""]

    # ---- スプライト
    sprites = []
    for fn in ("sprites_player.txt", "sprites_fx.txt", "sprites.txt", "sprites_boss.txt"):
        sprites += load_sprites(os.path.join(ASSETS, fn))
    H.append("typedef struct { const u32* data; u16 tiles; u8 w, h; } SpriteFrame;")
    H.append("enum {")
    names = []
    for (name, w, h, pix) in sprites:
        H.append(f"    SPR_{name.upper()},")
        names.append(name)
        data = sprite_to_tiles(pix, w, h)
        C.append(f"static const u32 spr_{name}[] = {{\n{c_bytes_as_u32(data)}\n}};")
    H.append("    SPR_COUNT\n};")
    H.append("extern const SpriteFrame sprite_frames[SPR_COUNT];")
    C.append("const SpriteFrame sprite_frames[SPR_COUNT] = {")
    for (name, w, h, pix) in sprites:
        C.append(f"    {{ spr_{name}, {w * h // 64}, {w}, {h} }},")
    C.append("};")

    # ---- ステージタイル (CBB0)
    mts = gen_metatiles()
    tiles, index = [], {}
    tiles.append(bytes(32))
    index[bytes(32)] = 0
    meta_map = []
    for name in METATILE_ORDER:
        m = tileize(mts[name], 16, 16, tiles, index, pal=0)
        meta_map += m
    H.append("enum {")
    for name in METATILE_ORDER:
        H.append(f"    MT_{name.upper()},")
    H.append("    MT_COUNT\n};")
    H.append(f"extern const u32 level_tiles[{len(tiles) * 8}];")
    H.append(f"#define LEVEL_TILE_COUNT {len(tiles)}")
    H.append(f"extern const u16 metatiles[MT_COUNT * 4];")
    C.append(f"const u32 level_tiles[{len(tiles) * 8}] = {{\n{c_bytes_as_u32(b''.join(tiles))}\n}};")
    C.append(f"const u16 metatiles[MT_COUNT * 4] = {{\n{c_u16(meta_map, 4)}\n}};")

    # ---- 遠景 + フォント + HUD (CBB1 を共有)
    font_tiles = gen_font()
    hud = gen_hud_tiles()
    cb1, cb1_index = [], {}
    # 0: 空タイル
    cb1.append(bytes(32))
    cb1_index[bytes(32)] = 0
    font_base = len(cb1)
    for t in font_tiles:
        cb1.append(t)  # フォントは重複排除しない(文字コードで直接引くため)
    hud_base = len(cb1)
    for name in HUD_TILE_ORDER:
        cb1.append(tile_bytes(hud[name], 0, 0))
    # タイトルロゴ (CBB1 に追加, 重複排除)
    logo_index = {}
    logo_tiles = []
    logo_base = len(cb1)
    logo_map = tileize(gen_logo(), 240, 48, logo_tiles, logo_index, base=logo_base, pal=5)
    # 透明タイルは 0 番を使う
    blank_t = bytes(32)
    logo_map = [0 if logo_tiles[(v & 0x3FF) - logo_base] == blank_t else v for v in logo_map]
    cb1 += logo_tiles
    H.append("extern const u16 logo_map[30 * 6];")
    C.append(f"const u16 logo_map[30 * 6] = {{\n{c_u16(logo_map)}\n}};")
    if len(cb1) > 512:
        sys.exit(f"CBB1 タイル数超過: {len(cb1)}")
    # 遠景は CBB2 に単独配置
    sky_pix = gen_skyline()
    sky_tiles, sky_index = [bytes(32)], {bytes(32): 0}
    sky_map = tileize(sky_pix, 256, 256, sky_tiles, sky_index, base=0, pal=1)
    if len(sky_tiles) > 512:
        sys.exit(f"遠景タイル数超過: {len(sky_tiles)}")
    H.append(f"extern const u32 sky_tiles[{len(sky_tiles) * 8}];")
    C.append(f"const u32 sky_tiles[{len(sky_tiles) * 8}] = {{\n{c_bytes_as_u32(b''.join(sky_tiles))}\n}};")
    H.append(f"#define FONT_TILE_BASE {font_base}")
    H.append(f"#define FONT_FIRST {FONT_FIRST}")
    H.append(f"#define FONT_LAST {FONT_LAST}")
    for i, name in enumerate(HUD_TILE_ORDER):
        H.append(f"#define HUD_TILE_{name.upper()} {hud_base + i}")
    H.append(f"extern const u32 ui_tiles[{len(cb1) * 8}];")
    H.append(f"#define UI_TILE_COUNT {len(cb1)}")
    H.append("extern const u16 sky_map[32 * 32];")
    C.append(f"const u32 ui_tiles[{len(cb1) * 8}] = {{\n{c_bytes_as_u32(b''.join(cb1))}\n}};")
    C.append(f"const u16 sky_map[32 * 32] = {{\n{c_u16(sky_map)}\n}};")

    # ---- ステージ
    H.append("")
    H.append("typedef struct { u8 type, param; u16 x, y; } LevelObj;")
    H.append("typedef struct { u16 w, h; const u8* cells; s16 spawn_x, spawn_y; const LevelObj* objs; u16 nobjs; u8 theme; const char* name; } LevelDef;")
    H += ["#define CELL_EMPTY 0", "#define CELL_SOLID 1", "#define CELL_ONEWAY 2", "#define CELL_HAZARD 3",
          "#define CELL_BACK 4"]
    for k, v in OBJ_TYPES.items():
        pass
    H += ["enum { OBJ_NONE, OBJ_CRAWLER, OBJ_DRONE, OBJ_TURRET, OBJ_CHECKPOINT, OBJ_GOAL, OBJ_HEAL, OBJ_JUMPER, OBJ_BOSS, OBJ_SHIELD, OBJ_LIFT_H, OBJ_LIFT_V };"]
    level_dir = os.path.join(ASSETS, "levels")
    level_files = sorted(f for f in os.listdir(level_dir) if f.endswith(".txt"))
    lv_names = []
    for i, fn in enumerate(level_files):
        lv = load_level(os.path.join(level_dir, fn))
        if lv["h"] > 16:
            sys.exit(f"{fn}: 高さは16以下")
        n = f"lv{i}"
        lv_names.append((n, lv))
        C.append(f"static const u8 {n}_cells[{lv['w'] * lv['h']}] = {{\n{c_u8(lv['cells'])}\n}};")
        objs = sorted(lv["objs"], key=lambda o: o[2])
        C.append(f"static const LevelObj {n}_objs[{max(1, len(objs))}] = {{")
        for (t, prm, x, y) in objs:
            C.append(f"    {{ {t}, {prm}, {x}, {y} }},")
        if not objs:
            C.append("    { 0, 0, 0, 0 },")
        C.append("};")
    H.append(f"#define LEVEL_COUNT {len(lv_names)}")
    H.append("extern const LevelDef levels[LEVEL_COUNT];")
    C.append("const LevelDef levels[LEVEL_COUNT] = {")
    for (n, lv) in lv_names:
        name = lv["name"].replace('"', "")
        C.append(f"    {{ {lv['w']}, {lv['h']}, {n}_cells, {lv['spawn'][0]}, {lv['spawn'][1]}, {n}_objs, {len(lv['objs'])}, {lv['theme']}, \"{name}\" }},")
    C.append("};")

    # ---- 音楽
    sq, wv = freq_tables()
    H.append("extern const u16 freq_square[128];")
    H.append("extern const u16 freq_wave[128];")
    C.append(f"const u16 freq_square[128] = {{\n{c_u16(sq)}\n}};")
    C.append(f"const u16 freq_wave[128] = {{\n{c_u16(wv)}\n}};")
    H.append("typedef struct { const u8* lead; const u8* bass; const u8* drum; u16 lead_len, bass_len, drum_len; u8 speed, loop; } Song;")
    H.append("#define MUS_HOLD 0")
    H.append("#define MUS_OFF 1")
    music_dir = os.path.join(ASSETS, "music")
    song_files = sorted(f for f in os.listdir(music_dir) if f.endswith(".txt"))
    H.append("enum {")
    for fn in song_files:
        H.append(f"    SONG_{os.path.splitext(fn)[0].split('_', 1)[1].upper()},")
    H.append("    SONG_COUNT\n};")
    H.append("extern const Song songs[SONG_COUNT];")
    entries = []
    for i, fn in enumerate(song_files):
        speed, loop, ch = load_song(os.path.join(music_dir, fn))
        for k in ("lead", "bass", "drum"):
            C.append(f"static const u8 song{i}_{k}[{len(ch[k])}] = {{\n{c_u8(ch[k])}\n}};")
        entries.append(f"    {{ song{i}_lead, song{i}_bass, song{i}_drum, {len(ch['lead'])}, {len(ch['bass'])}, {len(ch['drum'])}, {speed}, {loop} }},")
    C.append("const Song songs[SONG_COUNT] = {")
    C += entries
    C.append("};")

    H += ["", "#endif", ""]
    with open(os.path.join(outdir, "assets.h"), "w") as f:
        f.write("\n".join(H))
    with open(os.path.join(outdir, "assets.c"), "w") as f:
        f.write("\n".join(C) + "\n")

    # プレビュー (任意)
    if os.environ.get("ASSET_PREVIEW"):
        write_preview(sprites, mts, sky_pix, os.environ["ASSET_PREVIEW"])
    print(f"assets: sprites={len(sprites)} level_tiles={len(tiles)} ui_tiles={len(cb1)} sky_tiles={len(sky_tiles)} levels={len(lv_names)}")


def write_preview(sprites, mts, sky, outdir):
    try:
        from PIL import Image
    except ImportError:
        print("Pillow がないためプレビュー省略")
        return
    os.makedirs(outdir, exist_ok=True)

    def to_rgb(c):
        return tuple(v * 255 // 31 for v in c)

    # スプライトシート
    S = 4
    cols = 8
    cell = 34
    rows = (len(sprites) + cols - 1) // cols
    img = Image.new("RGB", (cols * cell * S, rows * cell * S), (60, 60, 80))
    for i, (name, w, h, pix) in enumerate(sprites):
        ox, oy = (i % cols) * cell, (i // cols) * cell
        for y in range(h):
            for x in range(w):
                v = pix[y][x]
                if v:
                    for dy in range(S):
                        for dx in range(S):
                            img.putpixel(((ox + x) * S + dx, (oy + y) * S + dy), to_rgb(SPR_PAL[v]))
    img.save(os.path.join(outdir, "sprites.png"))
    # メタタイル
    img = Image.new("RGB", (len(METATILE_ORDER) * 18 * 3, 18 * 3), (255, 0, 255))
    for i, name in enumerate(METATILE_ORDER):
        p = mts[name]
        for y in range(16):
            for x in range(16):
                for d in range(9):
                    img.putpixel(((i * 18 + x) * 3 + d % 3, y * 3 + d // 3), to_rgb(BG_PAL_LEVEL[p[y][x]]))
    img.save(os.path.join(outdir, "metatiles.png"))
    img = Image.new("RGB", (256, 256))
    for y in range(256):
        for x in range(256):
            img.putpixel((x, y), to_rgb(BG_PAL_SKY[sky[y][x]]))
    img.save(os.path.join(outdir, "skyline.png"))


if __name__ == "__main__":
    main()
