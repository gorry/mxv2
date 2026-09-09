# Hairline Texture Generator

Python の OpenCV と Numpy を使用して、ヘアライン加工された金属パネルのようなテクスチャ画像を生成するスクリプトです。

## 必要な環境

* Python 3.x
* 必要なパッケージ: `opencv-python`, `numpy`

インストールコマンド:
```bash
pip install opencv-python numpy
```
※ Windows の場合は `py -m pip install opencv-python numpy` を推奨します。

## ファイル構成

* `hairline.py` : テクスチャ生成のメインスクリプト

## スクリプトの仕組みと知識

このスクリプトは、以下のステップでヘアラインテクスチャを生成します。

1. **ノイズの生成**
   `numpy.random` を使用して、白黒の完全な一様ランダムノイズ画像を生成します。これが金属の微細な凹凸のベースとなります。
2. **モーションブラーによる線の形成**
   `cv2.filter2D` を用いて、指定された方向（水平または垂直）に細長いカーネル（1次元のブラーフィルタ）を適用します。これにより、点状のノイズが指定方向に引き伸ばされ、髪の毛のような細かい線（ヘアライン）になります。
3. **1px微細ライン生成・低周波うねり除去（`--fine`）**
   乱数ノイズを長軸方向にブラーしただけでは、短軸方向（直角方向）にランダムな低周波うねり（隣接ピクセルの平均の偏り）が残り、コントラストを上げると太い溝や縞模様が生じてしまいます。
   本スクリプトでは、短軸方向の1次差分（微分）を作用させることで低周波うねりを完全ゼロカットし、**「幅1pxの均一で極細なエンボスライン（上辺が明、下辺が暗）」**が画面全体に稠密に並んだリアルな金属研磨面を生成します。
4. **明るさレンジ・コントラスト調整（`--range`, `--intensity`）**
   乱数ノイズとブラー・微細化で形成されたベーステクスチャに対して、パーセンタイル正規化を行った上で指定された輝度レンジ（デフォルト: `0x90`〜`0xdf`）とコントラスト（`--intensity`）を適用します。
5. **追加の溝の立体感（`--groove`）**
   出来上がったベーステクスチャに対して、追加のエンボスフィルタを適用して溝の彫りの深さやエッジのシャープネスを強化します（`--fine 1.0` の時はベース自体が自然な1pxエンボス構造を持つため、デフォルト `0.0` でも十分リアルな立体感になります）。
6. **オーバーレイ合成による着色**
   生成されたグレースケール画像に対して色付けを行います。金属のハイライト（白）や深いシャドウ（黒）を塗りつぶさないよう、「オーバーレイ合成」のアルゴリズムを `numpy.where` を使って実装しています。これにより、プリセット色や任意のRGBA値を自然な形で金属に乗せることができます。
7. **光の反射表現（異方性ハイライト）**
   ヘアライン加工された金属特有の**「研磨筋と直角の方向に光が帯状に伸びる現象（異方性反射 / Anisotropic Reflection）」**をシミュレートします。
   * 水平方向のヘアラインでは光が縦長の帯状に、垂直方向では横長に引き伸ばされます。
   * 単純にグラデーションを加算するのではなく、金属表面の微細な凹凸（研磨筋の明暗）と掛け合わせて合成することで、光が当たった部分でもヘアラインの質感がくっきりと浮かび上がるリアルなハイライトを実現しています。

## 使い方

コマンドプロンプトや PowerShell からスクリプトを実行します。

```bash
# 基本の実行（デフォルト: 1920x1080, 水平, 1px極細ヘアライン, 軽いシルバー）
py hairline.py

# 縦方向のヘアラインで 1024x1024 を生成
py hairline.py --direction vertical --width 1024 --height 1024 --out hairline_vertical.png
```

### サンプル画像（back.bmp）の再現

太い溝のない1px微細ヘアライン、シャンパンゴールド色、左上からの反射光を持つテクスチャ（`back.bmp`）を再現するコマンド例:

```bash
py hairline.py --width 640 --height 480 --length 180 --range 80,168 --intensity 1.1 --fine 1.0 --groove 0.0 --rgba 252,246,170,255 --color-intensity 0.65 --light --light-pos 0.20,0.0 --light-color 255,255,225 --light-intensity 0.5 --light-radius 0.45 --light-spread 0.5 --light-stretch 1.5 --out hairline_back_reproduced.png
```

### 光の反射（ライティング）の追加

```bash
# 基本の反射光を追加（中央に自然な白いハイライト）
py hairline.py --light --out hairline_light.png

# 光源の位置・色・強さをカスタマイズ
py hairline.py --light-pos 0.35,0.4 --light-color 255,245,220 --light-intensity 1.0 --out hairline_spot.png

# 暖色系の光とゴールドプリセットを組み合わせる
py hairline.py --preset gold-light --light --light-color 255,255,220 --out hairline_gold_light.png
```

### 同じ絵をもう一度出す（`--seed`）

ベースのノイズは実行するたびに変わるため、**同じコマンドでも毎回違う模様**に
なります。実行時に使った種は必ず表示されるので、気に入った絵が出たらその値を
`--seed` に渡せば、同じものを出し直せます。

```bash
$ py hairline.py --out hairline.png
Random seed: 2748493113 (use --seed 2748493113 to reproduce)
...

# 同じ模様をもう一度（色や光だけ変えて出し直したいときにも使える）
py hairline.py --seed 2748493113 --out hairline.png
```

同じ種でも、`--width` / `--height` / `--length` を変えると別の絵になります
（生成は余白 `--length` ぶんを足した大きさで行い、あとから中央を切り出して
いるため、ノイズの大きさ自体が変わります）。色・明るさ・光のオプションは
ノイズより後の工程なので、**種を固定したまま自由に変えられます**。

### 明るさレンジ（明暗・コントラスト）の調整

`--range`（または `--brightness-range`）でベーステクスチャの最小・最大輝度を指定できます。16進数（`0x20,0x80`）または10進数（`32,128`）で指定可能です。

```bash
# 暗めのヘアライン（ガンメタル・ブラックメタル風）
py hairline.py --range 0x20,0x70 --out hairline_dark.png

# 輝度差（コントラスト）を強くして筋を際立たせる
py hairline.py --range 0x10,0xf0 --out hairline_contrast.png

# 暗めのベースに反射光を当てる
py hairline.py --range 0x20,0x60 --light --out hairline_dark_lit.png
```

### 色の調整

色味はプリセット、または RGBA 値で指定可能です。

**プリセットを使用する:**
```bash
py hairline.py --preset gold-light --out hairline_gold.png
```
利用可能なプリセット: `silver-light`, `silver-heavy`, `gold-light`, `gold-heavy`, `dark-light`, `dark-heavy`

**RGBA値を直接指定する:**
カンマ区切りで `R,G,B,A` を指定します（0〜255）。A（アルファ）は出力PNG画像の透明度として適用されます。
```bash
py hairline.py --rgba 255,180,180,255 --color-intensity 0.5 --out hairline_rosegold.png
```

### オプション一覧

* `--width` : 画像の幅 (デフォルト: 1920)
* `--height` : 画像の高さ (デフォルト: 1080)
* `--direction` : ヘアラインの方向 (`horizontal` または `vertical`, デフォルト: `horizontal`)
* `--length`, `--hairline-length` : ヘアライン（研磨筋）の長さ（ピクセル単位, デフォルト: 150）
* `--fine`, `--fine-lines` : 1px微細ライン（低周波うねりカット）の適用比率（1.0: 太い溝のない1px極細線, 0.0: 従来のブラーノイズ, デフォルト: 1.0）
* `--intensity` : ヘアラインの彫りの深さ・強さ倍率 (デフォルト: 1.0)
* `--groove`, `--emboss`, `--depth` : 追加の溝の立体感・エンボス効果の強さ (デフォルト: 0.0, fine=1.0の時は0.0〜0.5推奨)
* `--range`, `--brightness-range` : ベースの明るさレンジ `min,max` (例: `0x20,0x80` または `30,220`, デフォルト: `0x90,0xdf`)
* `--preset` : カラープリセットの指定
* `--rgba` : カスタムカラーの指定 (例: `255,215,0,255`)
* `--color-intensity` : 色のブレンド率 (0.0〜1.0)
* `--out` : 出力ファイル名 (デフォルト: `hairline.png`)
* `--seed` : ベースノイズの乱数の種 (省略時は毎回ランダム。使った値は必ず表示される)
* `--light` : 光の反射効果を有効化
* `--light-pos` : 反射の中心位置 `X,Y` (0.0〜1.0, デフォルト: `0.5,0.5`)
* `--light-color` : 光源の色 `R,G,B` (0〜255, デフォルト: `255,255,255`)
* `--light-intensity` : 光の強さ (デフォルト: `0.8`)
* `--light-radius` : 光源のサイズ/半径比率 (デフォルト: `0.35`)
* `--light-spread` : 周囲への光の拡散度・なだらかさ (デフォルト: `0.5`)
* `--light-stretch` : 異方性反射の伸長率（1.0で真円、デフォルト: `2.5`）


