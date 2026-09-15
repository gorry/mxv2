スキンエディタのプレビューを、mxv2 本体の描画そのものに置き換えるための計画。

**2026-09-03、Phase 0〜6 をすべて実装して完了。** 結果は末尾の「実装結果」を参照。

## 現状

`skineditor/SkinEditor/UI/PreviewCanvas.cs`（324 行）が GDI+ で「それらしい絵」を
描いている。位置と大きさは layout.ini の実効値どおりだが、発色の仕組みは
再現していない（ファイル冒頭のコメントにも「近似表示」と明記してある）。

具体的な不足:

| 箇所 | 本体 | 今のプレビュー |
|---|---|---|
| 素材の読み込み | 8bpp のパレットとインデックスを保つ | `new Bitmap(loaded)` で **32bpp へ変換＝パレットを捨てている**（BitmapCache.cs） |
| 背景合成 | `back.bmp` → 配色の色 → バナー/ステータス下地/ファイラー下地/鍵盤下地 を**乗算とアルファで順に重ねる** | 背景画像をそのまま描き、半透明の黒矩形を置くだけ |
| colors.ini の Bright 系 | ゲイン（100 が素通し）として全部品に効く | ほぼ効かない |
| 鍵盤 | `kb0/kb1/kb2` のパレットを配色で書き換えて合成 | `kb0.bmp` をそのまま 9 段並べるだけ |
| ステータス欄 | 素材のミニフォントを背景と一発合成 (`BmpCopyComposite`) | Consolas 7pt のダミー文字列 |
| レベルメータ | パレット 32〜95 を点灯色/消灯色で差し替え | 描いていない |
| 操作ボタン | LED のパレットに `PalGreen`/`PalRed`/`PalDark` をコピー | 素材をそのまま貼るだけ（LED は変わらない） |
| プログレスバー | バー（左端・中央の繰り返し・右端）を上段/下段で再生位置により貼り分け＋時刻文字 | 貼り分けのみ、文字なし |
| 音量バー | バー（左端・中央の繰り返し・右端）＋つまみ＋音量文字 | バー＋つまみのみ |
| スクロールバー | つまみ位置の計算・矢印の押下表示 | つまみを 0.3 固定で描画 |
| ファイラー | カーソルは乗算 (`BmpFillMul`)、文字は TTF を**出力解像度**で重ねる | Meiryo UI でキャンバス座標に直接描画 |
| 曲名 | 下地を配色で敷いて TTF | 黒矩形＋Meiryo UI |

## 方針

**採用案: mxv2 本体の描画コードを C# へ 1:1 移植する。**

比較した案:

| 案 | 内容 | 判断 |
|---|---|---|
| **A. C# へ移植** | `bitmap.cpp` / `drawscreen.cpp` を C# に書き写し、エディタ内で 24bpp キャンバスを組み立てる | **採用**。この構造は既に定着している（`skin.cpp` → `SkinLayout.cs`/`SkinLayoutIo.cs`、`colors.cpp` → `ColorsValues.cs`/`ColorsIo.cs` が 1:1 移植で、テストで同梱スキンを実際に開いて突き合わせている）。未保存の編集内容をその場で描け、ドラッグ移動にも即応できる |
| B. ネイティブ DLL 化して P/Invoke | 描画コアを共有ライブラリにして C# から叩く | 見送り。`drawscreen.cpp` は `filer.h`（`PutFileList` の引数）・`message.h`・`screen.h`/`textlayer.h`（SDL）に依存しており、切り出すには本体側の分解が要る。エディタのビルドに C++ ツールチェインと CMake が要るようになるのも重い。**文字まで画素一致が要求になったときの代替として残す** |
| C. mxv2.exe に静止画出力モードを足す | `-shot <png>` で 1 枚描いて終了、エディタはそれを呼んで表示 | 見送り。描画は完全一致するが、(1) 未保存の内容を映すには一時スキンフォルダへ書き出す必要がある、(2) プロセス起動が 1 回 100〜300ms でドラッグ操作に追随できない、(3) exe の場所の解決が要る。**「最終確認用に本物を出す」補助機能としてなら後から足せる** |

## 移植の対象

| 本体 (src/) | 行数 | エディタ側 | 移植 |
|---|---|---|---|
| `bitmap.h` / `bitmap.cpp` | 619 | `Model/Render/RenderBitmap.cs` / `Blitter.cs` | **する**（合成テーブルとブリッタ 6 種） |
| `bmpfile.cpp` | 157 | — | **しない**。System.Drawing で読み、8bpp はインデックスとパレットを保ったまま `RenderBitmap` へ移す（同梱素材は 11 枚すべて `Format8bppIndexed` であることを実測確認済み） |
| `colors.h` / `colors.cpp` | 243 | `Model/ColorsValues.cs` / `ColorsIo.cs` | **済**（既存） |
| `skin.h` / `skin.cpp` | 712 | `Model/SkinLayout.cs` / `SkinLayoutIo.cs` | **済**（既存） |
| `drawscreen.cpp` の描画部 | 約 950 | `Model/Render/DrawScreenPort.cs`（分割可） | **する** |
| `drawscreen.cpp` の `HitCheck*` | 約 100 | — | **しない**。プレビューの選択・ドラッグは今の `Reg()`（レイアウト値から矩形を作る）を維持する |
| `drawscreen.cpp` の差分更新 (`*Last_` / `refresh`) | — | — | **しない**。プレビューは毎回全部描く |
| `textlayer.*` / `textrender.*`（stb_truetype） | 622 | `Model/Render/TextLayerPort.cs` | **近似**（下記 Phase 4） |
| `screen.*`（SDL） | — | — | **しない**。`BlitTo` 相当は 32bpp の `System.Drawing.Bitmap` へ書き出す |

新規に書く C# はおおよそ 1500〜1800 行の見込み。

## フェーズ

### Phase 0 — 素材の読み込み（パレットを保つ）

- `Model/Render/RenderBitmap.cs`: `Bitmap` クラスの移植。ボトムアップ・4 バイト境界・
  8bpp/24bpp・256 色パレットという **DIB のメモリレイアウトをそのまま**保つ
  （本体がそうしているのは、ブリッタを原典からほぼそのまま持ってくるため）。
- `Model/Render/BmpLoader.cs`: System.Drawing で読み、`LockBits` + `Palette` から
  `RenderBitmap` を作る。8bpp はインデックスとパレットをそのまま、24/32bpp は BGR24 へ。
- `BitmapCache` を `RenderBitmap` 返しに差し替える（`BitmapRoleRow` のプレビュー表示は
  従来どおり `System.Drawing.Bitmap` が要るので、両方返せるようにする）。
- 検証: 同梱スキンの 11 枚を読み、幅・高さ・bpp・パレット代表値をテストで確認。

### Phase 1 — ブリッタ

- `InitBlendTables` 相当（`Alpha` / `AlphaMul` / `Clip` の 3 表）と
  `BmpFill` / `BmpFillMul` / `BmpCopy` / `BmpCopyTransparent` / `BmpCopyComposite` /
  `BmpBlendMask` を移植。クリップ手順（`ClipCopy`）も含める。
- `kBlendSub`(101) / `kBlendAdd`(102) / `kBlendMul`(103) の特殊値もそのまま。
- 検証: 合成表の代表値（`alpha[50*511+255]`、`alphaMul[150*256+100]` など）と、
  小さな既知ケースのブリッタ結果を単体テストで固定する。

### Phase 2 — 背景の合成

`LoadBackBitmap` → `CompositeBanner` → `CompositeStatusBack` → `CompositeFileList` →
`CompositeKeyboard` → `CompositeBack` を移植。ここまでで
「背景・バナー・ステータス下地・ファイラー下地・鍵盤の下地」が本物と同じになる。
`kb0` のパレット 1/2 を配色の `BlackBright`/`WhiteBright` で書き換える処理もここ。

### Phase 3 — 部品の描画

差分更新を外し、毎回全部描く形にして移植する。

1. ミニフォント: `PrintMini` / `PrintMiniCompose` / `MiniGlyphSrc`（グリフ表は
   素材の大きさ ÷ 16 列 x 5 行。文字→グリフの対応は本体 `kMiniGlyphIndex` の写し）
2. ステータス: `StatusItemPos` / `PutStatusText` / 各 `Put*` / `PutLevelMeter` /
   `PutStatusZero`（**本体の演奏前と同じ「全部 0」**がプレビューの既定になる）
3. 鍵盤: `CutKeyboardBitmap` と `PutNoteOn`（押鍵の見本を出すかは Phase 5 で判断。
   既定は押さない＝本体の停止時と同じ）
4. 操作ボタン: `PutPlayKey`（LED のパレットコピーを含む）
5. `PutProgressBar` / `PutTotalVolBar`（時刻・音量の文字も）
6. `PutScrollBar`（`SetScrollBarThumb` / `scrollBarFlags` も。押下表示はプレビューでは常に無し）
7. `PutMDXTitle` / `PutFileList` の**下地とカーソルまで**（文字は Phase 4）

### Phase 4 — 文字（TTF）

本体は「キャンバスを拡大 → その上に**出力解像度**で文字を重ねる」2 段構え
（`textlayer.h` の理由: 半端な倍率で拡大すると字が汚れるため）。プレビューも
同じ 2 段構えにする。

- 論理座標で受け取り、拡大後の解像度で描く。クリップ（`clipY`/`clipH`）と
  `Bright` の扱いは本体に合わせる。
  ※ 実装は `UI/PreviewTextLayer.cs`（GDI+ に依存するので Model ではなく UI 側）。
  描画コア側は文字を焼かず、`DrawScreenPort.TextDraws` に依頼を積むだけにした。
- グリフのラスタライズは **GDI+ + `PrivateFontCollection`**（スキンの `font.ttf` を
  インストールせずに読む）で近似する。本体は stb_truetype なので、
  **ここだけは画素一致しない**（字送りと AA の出方が違う）。位置・折り返し・
  クリップの確認には十分という判断。
- 完全一致が要るときの手段は 2 つ。どちらも重いので、必要になってから:
  - stb_truetype の C# 移植（StbTrueTypeSharp などの外部依存が増える）
  - 上記 案B / 案C（ネイティブ側に描かせる）

### Phase 5 — PreviewCanvas の差し替え

- `PreviewCanvas` を「オフスクリーンに 1 枚描いて、拡大して貼るだけ」に変える。
  拡大は最近傍（本体の既定は sharp-bilinear だが、プレビューは画素位置を
  確かめる用途なので最近傍のまま）。
- 選択・ドラッグの当たり判定は今の `Reg()` 方式を維持し、**描画と切り離す**
  （レイアウト値から矩形を作るので、描画順に依存しない）。
- 状態バー（PLAY / CONT / PAUSE / REPEAT / 音量 / 進捗）は、そのまま
  `PutPlayKey` / `PutTotalVolBar` / `PutProgressBar` の引数へつなぐ。
- ダミーのファイラー内容（`DummyFiles`）は `PutFileList` が読める形へ移す。
  本体は `Filer` を引数に取るが、プレビューは `top` / `cursor` / `topOffsetPx` /
  `itemCount` / `item(j)` しか使わないので、その 5 つだけのダミーで足りる。
- **素材が欠けているときは絵を出さずに続ける**（本体の `DrawScreen::Init` は
  1 枚でも読めなければ false で止まるが、編集中は一時的に欠けうるので、
  プレビューは placeholder を描いて続行する。意図的な差異）。

### Phase 6 — 再現性の検証

「似ている」で終わらせないために、**本物との画素比較**を用意する。

1. `mxv2.exe -skin Default -zoom 100` を起動し、クライアント領域（640x480）を
   撮って `skineditor/SkinEditor.Tests/refimage/ref-Default.png` として保存
   （手順は既存のスクリーンショット検証と同じ。DPI 対応必須）。
   ※ 当初 `testdata/` に置く計画だったが、`.gitignore` の `testdata/` に
   食われて追跡されないので `refimage/` にした。
2. 同じスキンをエディタの描画で 640x480 に起こし、参照 PNG と画素比較する
   テストを追加する。**文字レイヤーの矩形（ファイラーと曲名）は除外**するか、
   許容差を別に設ける（Phase 4 の近似ぶん）。
3. `Phone`（480x720）と `Default-Midnight`（配色だけ違う）でも同じことをして、
   スキンサイズと配色の両方が効いていることを確かめる。

## 決めごと・仮定

- **差分更新は移植しない。** プレビューは値が変わるたびに全部描き直す。
  背景の合成だけで 640x480 を 5 回ほどなめるので、C# では十数 ms 掛かる見込み。
  スピンボタンの連打で引っ掛かるようなら、背景（`back_`）をキャッシュして
  部品だけ描き直す（本体と同じ構造）へ寄せる。
- **`HitCheck*` は移植しない。** 現行のドラッグ・選択は layout の値から作った
  矩形で足りており、描画と結合させる利点がない。
- 押鍵・レベルメータの点灯・演奏中のステータス値は、既定では出さない
  （本体の「演奏前」と同じ絵になる）。見本を出したくなったら状態バーに
  スイッチを足す。
- 本体の描画を直したら、エディタ側も直す必要がある。触る場所の一覧
  （現状 5 か所）に「`Model/Render/` の移植」が加わる。

## 積み残し・リスク

- 文字（ファイラー・曲名）は画素一致しない（Phase 4 に記載）。
- `bmpfile.cpp` の 1/4/16/32bpp 展開を移植しないので、変わった BMP を
  インポートしたときの見え方が本体と違う可能性がある。
  ただし `BitmapRoleInfo.IsPaletteDependent` の役割は 8bpp しか受け付けない
  作りなので、実害が出るのは `back.bmp` くらい。
- 移植した描画コードと本体がずれる可能性。Phase 6 の画素比較テストが
  そのための歯止めになる（同梱スキンが変わったら参照 PNG も撮り直す）。

---

## 実装結果（2026-09-03）

Phase 0〜6 をすべて実装し、**本物のスクリーンショットとの画素比較で全一致**した。

### 作ったもの

| ファイル | 行 | 中身 |
|---|---|---|
| `Model/Render/RenderBitmap.cs` | 72 | `bitmap.h` の `Bitmap`（ボトムアップ DIB・4 バイト境界・8/24bpp・256 色パレット） |
| `Model/Render/Blitter.cs` | 417 | `bitmap.cpp` の合成表 3 種とブリッタ 6 種 |
| `Model/Render/BmpLoader.cs` | 142 | System.Drawing で読んで `RenderBitmap` へ（8bpp はパレットを保つ）。24bpp → 32bpp の書き出しも |
| `Model/Render/DrawScreenPort.cs` | 778 | `drawscreen.cpp` の描画部 |
| `UI/SkinAssetSource.cs` | 46 | `Skin::FindFile` 相当（自スキン → 土台）とキャッシュ |
| `UI/PreviewTextLayer.cs` | 133 | `textlayer.*` 相当。GDI+ + PrivateFontCollection |
| `UI/PreviewRenderer.cs` | 139 | 本体 main.cpp の毎フレームの呼び出し順をなぞる |
| `UI/PreviewCanvas.cs` | 248 | 描いた 1 枚を貼るだけに作り直し。当たり判定は layout から別に組む |
| `SkinEditor.Tests/RenderTests.cs` | 194 | ブリッタ・素材読み込み・描画のスモークテスト |
| `SkinEditor.Tests/RenderCompareTests.cs` | 111 | 本物のスクリーンショットとの画素比較 |
| `SkinEditor.Tests/DumpPreviewTest.cs` | 44 | 確認用の道具（PNG 出力と時間計測。環境変数を入れたときだけ動く） |

`UI/BitmapCache.cs` は使う場所が無くなったので `nouse/` へ退避した。

### 再現性の検証

`SkinEditor.Tests/RenderCompareTests.cs` が、`mxv2.exe -skin Default -zoom 100` の
クライアント領域を撮った `refimage/ref-Default.png` と画素で突き合わせる。

- **比較 156,060 画素、不一致 0**（バナー / ステータス欄 9 段 / 鍵盤 9 段）
- 除外したのは実行時の状態で変わる場所（ファイラー・曲名・操作ボタン・
  プログレスバー・音量バー・スクロールバー）

ブリッタ単体は `RenderTests.cs` が readme.md の乗算ゲインの表
（0=黒 / 100=素通し / 200=白）で固定している。

### 速さ

1 枚あたり **背景から作り直して 15ms / 背景を使い回して 4ms**（Release ではなく
Debug ビルドでの実測）。レイアウトや配色を触ったときだけ 15ms の方を通り、
状態バー（音量・進捗のスライダ）を動かしている間は 4ms の方を通る。
そのために `DrawScreenPort.Redraw()`（本体には無い切り分け）を足してある。

### 本体で見つかった不具合（未修正）

**操作ボタンの LED が、スキンを切り替えた直後だけ点灯色にならない。**

`DrawScreen::Reload()` が `playKeyStatusLast_ = kPlayKeyStatusNever`
(= `0xffffffff`) を入れる一方で、`LoadAssets()` は LED のパレットを消灯色
(25,25,25) で初期化する。`PutPlayKey` の LED は
`now == (playKeyStatusLast_ & bit)` で差分を見るので、**最初から点いている
LED は「変化なし」と判定されて**パレットが塗り替えられない。

参照画像で実測: CONT が有効な状態で起動しても、CONT の LED の画素は
(33,33,27) の消灯色のまま（点灯色なら (255,67,54)）。

移植側は「LED は毎回 status から塗り直す」ことにしたので、プレビューでは
意図どおりに点く。本体を直すなら `PutPlayKey` の LED の差分判定を外すのが
最小の修正（4 回のパレットコピーが毎フレーム増えるだけ）。**直したら
`refimage/ref-Default.png` を撮り直すこと。**

### 参照画像の扱い

**`skineditor/SkinEditor.Tests/refimage/ref-Default.png` は git に入れる。**
これが無いと `RenderCompareTests` は動かず、「本体と同じ絵か」を確かめる
手段が消えるため。352KB で、`.git` 全体 6.7MB・追跡済みバイナリ 1.5MB に対して
一度きり上乗せする程度。ただし**PNG は差分が効かないので、撮り直すたびに
履歴へ丸ごと 1 枚積まれる**。頻繁に撮り直す事態になったら、比較する矩形
（全体の約半分）だけを切り出して保存する形に変えるとよい。

`.gitignore` の `testdata/` は深さを問わず効く（本来は原典 MDX を除くための
規則）ので、置き場所は `testdata/` ではなく `refimage/` にしてある。

### 撮り直しの仕掛け

`refimage/capture-ref.ps1` が、mxv2.exe の起動から保存まで自動でやる。

```
powershell -ExecutionPolicy Bypass -File skineditor/SkinEditor.Tests/refimage/capture-ref.ps1
```

- `build/Release/mxv2.exe` を **専用のスクラッチ `-userdir`** で起動するので、
  ユーザーの `mxv2.ini` には触れない
- 自分が起動したウィンドウだけを最前面にしてから撮る（別のウィンドウが
  上に被っていると、そちらを撮ってしまう。実際に踏んだ）
- 撮った絵がほぼ単色だったら「別のウィンドウを撮った」とみなして失敗させる
- クライアント領域が 640x480 でなければ失敗させる（倍率やスキンの取り違え）
- 終わったら自分が起動したプロセスだけを終了し、スクラッチを消す
- **すでに mxv2.exe が動いていたら何もせず失敗する**（ユーザーが使っている
  かもしれないものを掴まない）

**検出はテストの失敗が担う。** 本体の描画か同梱スキンを変えると
`RenderCompareTests` が落ちるので、そこで「移植を直す」のか
「参照画像を撮り直す」のかを判断する。判断の目安はテストのコメントと
失敗メッセージに書いてある。撮り直しを完全自動（テストが勝手に mxv2 を
起動して上書き）にはしていない。GUI アプリを黙って起動することになるし、
「本体が変わった」のか「移植が壊れた」のかをテストが勝手に決めてしまうため。
