# スキンエディタ スピンコントロールの最小・最大値

現状の実装では、layout.ini 側の項目（`FieldEditControl` / `LabeledValueRow` /
`PcmChannelRow` が生成するスピンボタン）は**項目の意味を無視して全部
Min=-99999 / Max=99999 の決め打ち**になっている（でたらめ）。colors.ini 側
（`ColorFieldEditControl`）は項目ごとに 0-100 か 0-200 が設定済みだが、
こちらも一度全部載せたので、必要なら直してください。

## 使い方

- 表はスキンエディタのタブ表示順に並べてあります（サブタブがある項目は
  「サブタブ」欄に名前を書いてあります）。
- 「値」列は 1 行のスピンボタンが何個の値を持つか（例: 矩形なら x,y,w,h の
  4個）。今の実装は **同じ行の中の全部の値に同じ Min/Max が掛かる**ので、
  基本は「新Min」「新Max」に1組の数値を書いてください。
- もし x,y,w,h など値ごとに範囲を変えたい場合は、「新Min」「新Max」の欄に
  `x:0-640,y:0-480,w:0-640,h:0-480` のように `名前:min-max` をカンマ区切りで
  書いてください（対応して実装します）。
- 「新Min」「新Max」を空欄のまま返してもらえれば、その項目は現状維持
  （変更なし）として扱います。
- 「操作ボタンの数」（PlayKey.Count）は現状 ReadOnly（編集不可）のスピン
  ボタンです。範囲を変えても表示上の制限には影響しません（念のため掲載）。

行の識別子（`Section.Key`）は layout.ini / colors.ini 上のキー名です。
コード側の対応箇所を追うのに使えるので、変更希望が無い行でも消さずに
残しておいてください。

---

## [画面] タブ

| Section.Key | ラベル | 値 | 現Min | 現Max | 新Min | 新Max |
|---|---|---|---:|---:|---|---|
| Screen.Width | 幅 | 単一 | -99999 | 99999 | | |
| Screen.Height | 高さ | 単一 | -99999 | 99999 | | |
| back.bitmapBright | ビットマップの濃さ（配色） | 単一 | 0 | 100 | | |
| back.colorBright | 色の濃さ（配色） | 単一 | 0 | 100 | | |

## [鍵盤] タブ

| Section.Key | ラベル | 値 | 現Min | 現Max | 新Min | 新Max |
|---|---|---|---:|---:|---|---|
| Keyboard.Pos | 位置 | (x,y) | -99999 | 99999 | | |
| Keyboard.XOffset | 1オクターブの鍵のX（C〜B の12鍵＋オクターブ幅、計13個） | 13個 | -99999 | 99999 | | |
| Keyboard.YOffset | Y オフセット | 単一 | -99999 | 99999 | | |
| Keyboard.ChannelY | 各チャンネルのY位置（FM1〜FM8＋PCM、計9個） | 9個 | -99999 | 99999 | | |
| Keyboard.KeyOffset | 鍵の描画原点補正 | 単一 | -99999 | 99999 | | |
| kb.blackBright | 黒鍵側の下地の明るさ（配色） | 単一 | 0 | 200 | | |
| kb.whiteBright | 白鍵側の下地の明るさ（配色） | 単一 | 0 | 200 | | |
| kb.bright | 押している鍵の明るさ（配色） | 単一 | 0 | 200 | | |

## [ステータス] タブ

| Section.Key | サブタブ | ラベル | 値 | 現Min | 現Max | 新Min | 新Max |
|---|---|---|---|---:|---:|---|---|
| Status.Rect | (なし) | 矩形 | (x,y,w,h) | -99999 | 99999 | | |
| Status.PosLevelMeter | レベルメータ | 位置 | (x,y) | -99999 | 99999 | | |
| LevelMeter.PaletteOffset | レベルメータ | パレット開始番号 | 単一 | -99999 | 99999 | | |
| LevelMeter.Cells | レベルメータ | セル数 | 単一 | -99999 | 99999 | | |
| LevelMeter.SrcX | レベルメータ | 素材内: 左端の切り捨て | 単一 | -99999 | 99999 | | |
| Status.PosVolume | 配置 | 音量 | (x,y) | -99999 | 99999 | | |
| Status.PosPanpot | 配置 | パンポット | (x,y) | -99999 | 99999 | | |
| Status.PosDetune | 配置 | デチューン | (x,y) | -99999 | 99999 | | |
| Status.PosVoice | 配置 | 音色番号 | (x,y) | -99999 | 99999 | | |
| Status.PosQ | 配置 | Q | (x,y) | -99999 | 99999 | | |
| Status.PosPtr | 配置 | ポインタ | (x,y) | -99999 | 99999 | | |
| Status.PosLFOPitch | ピッチLFO | ピッチLFO | (x,y) | -99999 | 99999 | | |
| Status.PosLFOPitch1 | ピッチLFO | ピッチLFO 1 | (x,y) | -99999 | 99999 | | |
| Status.PosLFOPitch2 | ピッチLFO | ピッチLFO 2 | (x,y) | -99999 | 99999 | | |
| Status.PosLFOPitch3 | ピッチLFO | ピッチLFO 3 | (x,y) | -99999 | 99999 | | |
| Status.PosLFOPitch4 | ピッチLFO | ピッチLFO 4 | (x,y) | -99999 | 99999 | | |
| Status.PosLFOVolume | 音量LFO | 音量LFO | (x,y) | -99999 | 99999 | | |
| Status.PosLFOVolume1 | 音量LFO | 音量LFO 1 | (x,y) | -99999 | 99999 | | |
| Status.PosLFOVolume2 | 音量LFO | 音量LFO 2 | (x,y) | -99999 | 99999 | | |
| Status.PosLFOVolume3 | 音量LFO | 音量LFO 3 | (x,y) | -99999 | 99999 | | |
| Status.PosPcmVolume | PCM | PCM 音量 | (x,y) | -99999 | 99999 | | |
| Status.PosPcmPtr | PCM | PCM ポインタ | (x,y) | -99999 | 99999 | | |
| Status.PcmX / Status.PcmY | PCM | PCM の位置（PCM 1ch〜8ch、各 x,y） | 8行×(x,y) | -99999 | 99999 | | |
| status.colorBright | (なし) | 文字色の濃さ（配色） | 単一 | 0 | 100 | | |
| status.backColorBright | (なし) | 背景色の濃さ（配色） | 単一 | 0 | 100 | | |

## [ミニフォント] タブ

| Section.Key | ラベル | 値 | 現Min | 現Max | 新Min | 新Max |
|---|---|---|---:|---:|---|---|
| MiniFont.Width | 送り幅 | 単一 | -99999 | 99999 | | |
| MiniFont.Height | 行の高さ | 単一 | -99999 | 99999 | | |

## [バナー] タブ

| Section.Key | ラベル | 値 | 現Min | 現Max | 新Min | 新Max |
|---|---|---|---:|---:|---|---|
| Banner.Rect | 矩形 | (x,y,w,h) | -99999 | 99999 | | |

## [曲名] タブ

| Section.Key | ラベル | 値 | 現Min | 現Max | 新Min | 新Max |
|---|---|---|---:|---:|---|---|
| Title.Rect | 矩形 | (x,y,w,h) | -99999 | 99999 | | |
| mdxTitle.colorBright | 文字色の濃さ（配色） | 単一 | 0 | 100 | | |
| mdxTitle.backColorBright | 背景色の濃さ（配色） | 単一 | 0 | 100 | | |

## [ファイラー] タブ

| Section.Key | ラベル | 値 | 現Min | 現Max | 新Min | 新Max |
|---|---|---|---:|---:|---|---|
| FileList.Rect | 矩形 | (x,y,w,h) | -99999 | 99999 | | |
| FileList.Rows | 行数（小,大） | (小,大) | -99999 | 99999 | | |
| FileList.ItemHeight | 1行の高さ（小,大） | (小,大) | -99999 | 99999 | | |
| FileList.BaseNameX | ファイル名開始X（小,大） | (小,大) | -99999 | 99999 | | |
| FileList.BaseNameWidth | ファイル名幅（小,大） | (小,大) | -99999 | 99999 | | |
| FileList.TitleX | 曲名開始X（小,大） | (小,大) | -99999 | 99999 | | |
| FileList.TitleWidth | 曲名幅（小,大） | (小,大) | -99999 | 99999 | | |
| filer.cursorColorBright | カーソルの効き具合（配色） | 単一 | 0 | 100 | | |
| filer.colorBright | 文字色の濃さ（配色） | 単一 | 0 | 100 | | |
| filer.backColorBright | 背景色の濃さ（配色） | 単一 | 0 | 100 | | |

## [スクロールバー] タブ

| Section.Key | ラベル | 値 | 現Min | 現Max | 新Min | 新Max |
|---|---|---|---:|---:|---|---|
| ScrollBar.Rect | 矩形 | (x,y,w,h) | -99999 | 99999 | | |
| ScrollBar.SrcThumb | 素材内: つまみ | (x,y,w,h) | -99999 | 99999 | | |
| ScrollBar.SrcUpArrowPress | 素材内: 上矢印(押下) | (x,y,w,h) | -99999 | 99999 | | |
| ScrollBar.SrcDownArrowPress | 素材内: 下矢印(押下) | (x,y,w,h) | -99999 | 99999 | | |
| ScrollBar.SrcUpArrow | 素材内: 上矢印 | (x,y,w,h) | -99999 | 99999 | | |
| ScrollBar.SrcBar | 素材内: 溝 | (x,y,w,h) | -99999 | 99999 | | |
| ScrollBar.SrcDownArrow | 素材内: 下矢印 | (x,y,w,h) | -99999 | 99999 | | |
| ScrollBar.PosUpArrow | 配置: 上矢印 | (x,y) | -99999 | 99999 | | |
| ScrollBar.PosBar | 配置: 溝 | (x,y) | -99999 | 99999 | | |
| ScrollBar.PosDownArrow | 配置: 下矢印 | (x,y) | -99999 | 99999 | | |

## [プログレスバー] タブ

| Section.Key | ラベル | 値 | 現Min | 現Max | 新Min | 新Max |
|---|---|---|---:|---:|---|---|
| ProgressBar.Rect | 矩形 | (x,y,w,h) | -99999 | 99999 | | |
| ProgressBar.TimePos | 時刻表示位置 | (x,y) | -99999 | 99999 | | |

## [音量バー] タブ

| Section.Key | ラベル | 値 | 現Min | 現Max | 新Min | 新Max |
|---|---|---|---:|---:|---|---|
| VolumeBar.Rect | 矩形 | (x,y,w,h) | -99999 | 99999 | | |
| VolumeBar.TimePos | 時刻表示位置 | (x,y) | -99999 | 99999 | | |
| VolumeBar.NobWidth | つまみ幅 | 単一 | -99999 | 99999 | | |
| VolumeBar.NobSrc | 素材内: つまみ | (x,y,w,h) | -99999 | 99999 | | |
| VolumeBar.SlideSrc | 素材内: スライド | (x,y,w,h) | -99999 | 99999 | | |

## [操作ボタン] タブ

| Section.Key | サブタブ | ラベル | 値 | 現Min | 現Max | 新Min | 新Max |
|---|---|---|---|---:|---:|---|---|
| PlayKey.Rect | (なし) | 矩形 | (x,y,w,h) | -99999 | 99999 | | |
| PlayKey.Count | (なし) | 使うボタンの数（ReadOnly） | 単一 | -99999 | 99999 | | |
| PlayKey.Src0 | PREV | 素材内位置 | (x,y,w,h) | -99999 | 99999 | | |
| PlayKey.Pos0 | PREV | 配置 | (x,y) | -99999 | 99999 | | |
| PlayKey.Src1 | STOP | 素材内位置 | (x,y,w,h) | -99999 | 99999 | | |
| PlayKey.Pos1 | STOP | 配置 | (x,y) | -99999 | 99999 | | |
| PlayKey.Src2 | PLAY | 素材内位置 | (x,y,w,h) | -99999 | 99999 | | |
| PlayKey.Pos2 | PLAY | 配置 | (x,y) | -99999 | 99999 | | |
| PlayKey.PalPlayLed | PLAY | LEDのパレット | 単一 | -99999 | 99999 | | |
| PlayKey.Src3 | FAST | 素材内位置 | (x,y,w,h) | -99999 | 99999 | | |
| PlayKey.Pos3 | FAST | 配置 | (x,y) | -99999 | 99999 | | |
| PlayKey.Src4 | PAUSE | 素材内位置 | (x,y,w,h) | -99999 | 99999 | | |
| PlayKey.Pos4 | PAUSE | 配置 | (x,y) | -99999 | 99999 | | |
| PlayKey.PalPauseLed | PAUSE | LEDのパレット | 単一 | -99999 | 99999 | | |
| PlayKey.Src5 | NEXT | 素材内位置 | (x,y,w,h) | -99999 | 99999 | | |
| PlayKey.Pos5 | NEXT | 配置 | (x,y) | -99999 | 99999 | | |
| PlayKey.Src6 | CONT | 素材内位置 | (x,y,w,h) | -99999 | 99999 | | |
| PlayKey.Pos6 | CONT | 配置 | (x,y) | -99999 | 99999 | | |
| PlayKey.PalContLed | CONT | LEDのパレット | 単一 | -99999 | 99999 | | |
| PlayKey.Src7 | REPEAT | 素材内位置 | (x,y,w,h) | -99999 | 99999 | | |
| PlayKey.Pos7 | REPEAT | 配置 | (x,y) | -99999 | 99999 | | |
| PlayKey.PalRepeatLed | REPEAT | LEDのパレット | 単一 | -99999 | 99999 | | |
| PlayKey.PalKey | パレット | ボタンの色 | 単一 | -99999 | 99999 | | |
| PlayKey.PalDark | パレット | LED: 消灯 | 単一 | -99999 | 99999 | | |
| PlayKey.PalRed | パレット | LED: 赤 | 単一 | -99999 | 99999 | | |
| PlayKey.PalGreen | パレット | LED: 緑 | 単一 | -99999 | 99999 | | |
| PlayKey.PalYellow | パレット | LED: 黄 | 単一 | -99999 | 99999 | | |
| PlayKey.PalBlue | パレット | LED: 青 | 単一 | -99999 | 99999 | | |
| playKey.colorBright | (なし) | 文字色の濃さ（配色） | 単一 | 0 | 100 | | |
| playKey.keyBright | (なし) | ボタンの明るさ（配色） | 単一 | 0 | 200 | | |

---

補足: パレット番号系（PalKey/PalDark/PalRed/PalGreen/PalYellow/PalBlue/
PalPlayLed/PalPauseLed/PalContLed/PalRepeatLed/LevelMeter.PaletteOffset）は
「色そのもの」ではなく playkey.bmp / levelmeter.bmp 内の**パレット番号**を
指す整数です。適切な Max はパレットの色数（256色パレットなら 0-255 等）に
なると思います。
