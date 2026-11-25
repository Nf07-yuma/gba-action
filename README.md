# STEEL CHAIN（スティール・チェイン）

ゲームボーイアドバンス（GBA）向けの横スクロール・アクションゲームです。
実機（フラッシュカート）や mGBA などの GBA エミュレータで遊べます。

> 機械に支配された工業都市。小型戦闘ロボ **STEEL** はエネルギーブレードを手に、
> 中枢コアを目指して工場区画を駆け抜ける——。

## 遊び方

最新の ROM は [Releases](../../releases) から `steel_chain-build-*.gba` をダウンロードし、
[mGBA](https://mgba.io/) などで開いてください。

| ボタン | 操作 |
| --- | --- |
| 十字キー | 移動 |
| A | ジャンプ（長押しで高く） / 下+A で足場をすり抜け |
| B | 斬る（連打で 3 段コンボ、空中では回転斬り） |
| R / L | ダッシュ（無敵時間あり、空中でも 1 回） |
| START | 開始 / ポーズ |

### ゲームの特徴

- **CHAIN（連鎖）システム**: 攻撃を当て続けると CHAIN が伸び、スコア倍率が上がる。ステージクリア時に最大 CHAIN がボーナスになる
- **3 段コンボ**: 横薙ぎ → 斬り上げ → 叩きつけ。3 段目は盾持ちの敵のガードも砕く
- **攻撃キャンセル**: 攻撃後の硬直はジャンプ・ダッシュでキャンセル可能
- **弾消し**: 斬撃で敵弾をかき消せる（CHAIN も伸びる）

## ビルド方法

Ubuntu / Debian の標準パッケージだけでビルドできます（devkitPro 不要）。

```sh
sudo apt-get install gcc-arm-none-eabi python3
make            # steel_chain.gba が生成される
```

### テスト（ヘッドレス自動プレイ）

mGBA のコアライブラリを使い、ROM を画面なしで実行して自動操作・状態検証・スクリーンショット保存・BGM の録音（WAV）を行います。

```sh
sudo apt-get install libmgba-dev zlib1g-dev
make test                                        # スモークテスト (build/test-out/*.png)
build/runner steel_chain.gba build/bot-out bot   # ボットによる通しプレイ
```

ROM は EWRAM 先頭 (`0x02000000`) にデバッグ情報ブロック（状態・座標・HP・スコア等）を置いており、
テストランナーはこれを読んで判定します。

## ディレクトリ構成

```
src/            ゲーム本体 (C)
  crt0.s        起動コード・ROM ヘッダ・割り込み
  main.c        メインループ・画面遷移・HUD
  player.c      主人公 (移動/ジャンプ/ダッシュ/コンボ)
  enemy.c       敵・チェックポイント・ゴール・弾
  level.c       ステージ・当たり判定・スクロール・カメラ
  fx.c          エフェクト
  sound.c       効果音 (PSG)
include/gba.h   GBA ハードウェア定義
assets/         スプライト (ASCII アート) / ステージ定義 (テキスト)
tools/          アセット生成 (Python)
tests/runner.c  ヘッドレステストランナー (libmgba)
```

### アセットの編集

- `assets/sprites.txt` … 敵・アイテムのドット絵（1 文字 = 1 ピクセル、文字とパレットの対応はファイル先頭）
- `tools/compose_player.py` … 主人公のポーズ（関節座標）を編集して `make sprites` で再合成
- `assets/music/*.txt` … BGM の楽譜（`C-5` 音符、`---` 伸ばす、`...` 消音、ドラムは `K`=キック `S`=スネア `H`=ハット）
- `assets/levels/*.txt` … ステージ（`#`=壁 `=`=足場 `^`=トゲ `P`=開始位置 `c/d/t/j/s`=敵 `C`=中間地点 `G`=ゴール `+`=回復）

## CI / CD

- Pull Request ごとに GitHub Actions で ROM をビルドし、ヘッドレステストを実行（スクリーンショットは Artifacts に保存）
- `main` へのマージで ROM を自動ビルドし、GitHub Releases に公開

開発の進め方と今後の計画は [ROADMAP.md](ROADMAP.md) を参照してください。

## 注意

ROM ヘッダの任天堂ロゴ領域は空になっています。エミュレータではそのまま動作しますが、
実機で起動する場合は devkitPro の `gbafix` 等でロゴを書き込んでください。
