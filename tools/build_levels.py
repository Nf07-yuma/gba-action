#!/usr/bin/env python3
"""ステージ 2 以降をチャンク(区画)の連結で組み立てて assets/levels/ に書き出す補助ツール。
ステージ 1 は手で編集したテキストをそのまま使う。

chunk(幅, {行: 文字列}) で区画を作る。文字列の先頭に "@列:" を付けると開始列を指定できる。
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def chunk(H, w, rows, ground=None):
    g = [[" "] * w for _ in range(H)]
    if ground:
        # ground: (行の開始, [(列0, 列1), ...]) 指定行から下を全部埋める
        top, spans = ground
        for (a, b) in spans:
            for y in range(top, H):
                for x in range(a, b + 1):
                    g[y][x] = "#"
    for y, items in rows.items():
        if isinstance(items, str):
            items = [items]
        for it in items:
            x = 0
            if it.startswith("@"):
                pos, it = it[1:].split(":", 1)
                x = int(pos)
            for i, ch in enumerate(it):
                if ch != "_" and x + i < w:
                    g[y][x + i] = ch
    return ["".join(r) for r in g]


def save(name, title, theme, H, chunks):
    rows = ["".join(c[i] for c in chunks) for i in range(H)]
    path = os.path.join(ROOT, "assets", "levels", name)
    with open(path, "w") as f:
        f.write(f";name={title}\n;theme={theme}\n")
        for r in rows:
            f.write("|" + r + "|\n")
    print(f"{path}: {len(rows[0])}x{H}")


def stage2():
    H = 14
    G = 12  # 地面の行
    c = []
    c.append(chunk(H, 16, {8: "@2:w_____w", 9: "@2:p_____p", 10: "@2:p_____p", 11: ["@2:p_____p", "@5:P"]}, (G, [(0, 15)])))
    c.append(chunk(H, 20, {11: ["@6:c", "@14:j"]}, (G, [(0, 19)])))
    # 鉄骨の足場で穴を越える
    c.append(chunk(H, 24, {10: "@6:====", 7: "@12:====", 5: "@13:d", 11: "@21:+"}, (G, [(0, 3), (20, 23)])))
    c.append(chunk(H, 16, {10: "@6:####", 11: "@6:####", 9: "@7:t"}, (G, [(0, 15)])))
    # 柱渡り (隙間 2〜3 マス, 高低差 1 マス)
    c.append(chunk(H, 20, {10: ["@4:##", "@9:##", "@14:##"], 11: ["@4:##", "@9:##", "@14:##"],
                           9: "@9:##"},
                   (G, [(0, 1), (4, 5), (9, 10), (14, 15), (18, 19)])))
    c.append(chunk(H, 16, {7: "@3:bbbbbbbbbb", 8: "@3:bwbbbbbbwb", 9: "@3:bbbbbbbbbb", 10: "@3:bbbbbbbbbb",
                           11: ["@3:bbbbbbbbbb", "@7:C"]}, (G, [(0, 15)])))
    # 鉄骨の階段
    c.append(chunk(H, 26, {9: ["@3:=====", "@17:====="], 6: "@9:=====", 5: "@11:j", 3: "@20:d", 8: "@19:c",
                           11: "@12:c"}, (G, [(0, 25)])))
    c.append(chunk(H, 20, {11: ["@6:c", "@13:s"]}, (G, [(0, 19)])))
    # 長い穴 + 鉄骨 + ドローン
    c.append(chunk(H, 26, {10: ["@4:====", "@11:====", "@18:===="], 6: ["@8:d", "@16:d"]},
                   (G, [(0, 1), (24, 25)])))
    c.append(chunk(H, 18, {9: ["@3:###", "@12:###"], 10: ["@3:###", "@12:###"], 11: ["@3:###", "@12:###", "@8:c"],
                           8: ["@4:t", "@13:t"], 6: "@8:+"}, (G, [(0, 17)])))
    c.append(chunk(H, 20, {11: ["@4:s", "@12:j", "@16:c"]}, (G, [(0, 19)])))
    c.append(chunk(H, 14, {6: "@3:wwwww", 7: "@3:ppppp", 8: "@3:ppppp", 9: "@3:ppppp", 10: "@3:ppppp",
                           11: "@3:ppGpp"}, (G, [(0, 13)])))
    save("stage2.txt", "SECTOR 2  SKYWAY", 1, H, c)


def stage3():
    H = 12
    G = 10
    c = []
    ceil = "################"
    def indoor(w, extra, ground_spans=None, ceiling=True):
        rows = {}
        if ceiling:
            rows[0] = "#" * w
            rows[1] = "#" * w
        for y in range(2, G):
            rows.setdefault(y, [])
        bg = {y: ("g" * w if y % 4 else "b" * w) for y in range(2, G)}
        merged = {}
        for y in range(H):
            lst = []
            if y in bg:
                lst.append(bg[y])
            if y in rows:
                v = rows[y]
                lst += v if isinstance(v, list) else [v]
            if y in extra:
                v = extra[y]
                lst += v if isinstance(v, list) else [v]
            if lst:
                merged[y] = lst
        return chunk(H, w, merged, (G, ground_spans or [(0, w - 1)]))
    c.append(indoor(16, {9: "@4:P", 5: "@9:bwwb"}))
    c.append(indoor(20, {9: ["@8:s", "@15:c"]}))
    # トゲ床と足場
    c.append(indoor(24, {7: ["@4:====", "@11:====", "@18:===="], 6: "@12:j"},
                    [(0, 2), (21, 23)]))
    c[-1] = [r[:3] + ("^" * 18 if i >= G else r[3:21]) + r[21:] for i, r in enumerate(c[-1])]
    c.append(indoor(18, {9: ["@3:###", "@12:###"], 8: ["@3:###", "@12:###"], 7: ["@4:t", "@13:t"], 5: "@8:d"}))
    c.append(indoor(20, {9: ["@3:s", "@9:s", "@15:c"], 5: "@8:+"}))
    c.append(indoor(16, {9: "@7:C", 4: "@4:bwwbbwwb"}))
    # 天井の低い通路 + 跳ねる敵
    c.append(indoor(24, {2: "#" * 24, 3: "#" * 24, 9: ["@5:j", "@12:j", "@19:j"]}))
    c.append(indoor(22, {9: ["@2:##", "@10:##", "@18:##"], 8: ["@2:##", "@10:##", "@18:##"],
                         7: ["@3:t", "@11:d"], 5: "@19:+"}, [(0, 21)]))
    c.append(indoor(24, {8: ["@6:====", "@14:===="], 9: ["@3:s", "@11:c", "@20:s"], 5: "@10:d"},
                    [(0, 4), (8, 12), (17, 23)]))
    c[-1] = [r[:5] + ("^^^" if i >= G else r[5:8]) + r[8:13] + ("^^^^" if i >= G else r[13:17]) + r[17:] for i, r in enumerate(c[-1])]
    c.append(indoor(20, {9: ["@3:###", "@10:s", "@15:j"], 8: "@3:###", 7: "@4:t"}))
    c.append(indoor(12, {9: ["@4:C", "@8:+"], 5: "@3:bwwbbwwb"}))
    # ボス闘技場 (末尾 15 マスでカメラ固定)
    c.append(indoor(19, {4: "@6:bwwbbwwb", 9: "@14:B"}))
    save("stage3.txt", "SECTOR 3  CORE FOUNDRY", 2, H, c)


if __name__ == "__main__":
    stage2()
    stage3()
