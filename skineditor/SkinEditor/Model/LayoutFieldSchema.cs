// mxv2 スキンエディタ - layout.ini の GUI 生成用スキーマ。
//
// 表示文字列は src/skin.h と同じ書式（カンマ区切り）。値そのものの読み書きは
// SkinDocument.SetLayoutRaw / OwnLayoutIni がそのまま文字列で扱うので、
// ここでは「表示ラベル」と「今の実効値をどう文字列にするか」だけを持つ。

namespace SkinEditor.Model;

public enum FieldKind { Int, IntList, Xywh, Str }

// ReadOnly は「値は表示するが、継承チェックボックスも数値欄も編集不可にする」
// 指定（FieldEditControl 側で見る）。SHUFFLE 未実装の間、[PlayKey] Count を
// うっかり 9 にしてボタンを増やせないようにするために追加した。
public sealed record FieldDef(string Section, string Key, string Label, FieldKind Kind,
    Func<SkinLayout, string> Format, bool ReadOnly = false);

public sealed record FieldSectionDef(string Title, IReadOnlyList<FieldDef> Fields);

public static class LayoutFieldSchema
{
    public static IReadOnlyList<FieldSectionDef> BuildSections()
    {
        var status = new List<FieldDef>
        {
            // レベルメータの素材行（BitmapRowsByTab）の次に来るよう、
            // レベルメータの設定をこのタブの先頭に置く
            // （「レベルメータ」タブは廃止してここへ統合した）。
            new("LevelMeter", "PaletteOffset", "パレット開始番号", FieldKind.Int, e => $"{e.levelMeterPalOfs}"),
            new("LevelMeter", "Cells", "セル数", FieldKind.Int, e => $"{e.levelMeterWidthCells}"),
            new("LevelMeter", "SrcX", "素材内: 左端の切り捨て", FieldKind.Int, e => $"{e.levelMeterSrcX}"),
            // ミニフォントは 2026-09-03 に独立したタブへ戻した（ユーザー指示）。
            new("Status", "Pos", "位置 (x,y)", FieldKind.IntList, e => $"{e.statusX},{e.statusY}"),
            new("Status", "BackWidth", "背景幅", FieldKind.Int, e => $"{e.statusBackW}"),
            new("Status", "BackHeight", "背景高さ", FieldKind.Int, e => $"{e.statusBackH}"),
            // PcmX/PcmY (各8個) はここには含めない。「PCM 1ch」～「PCM 8ch」の
            // (x,y) 2値編集として SkinEditForm 側で専用に描画する
            // （PcmChannelRow。8個のスピンボタン列にはしない、というユーザー指示）。
        };
        // 1 段の中での各項目の位置。FM の 16 項目は [Status] Pos からの相対、
        // PCM の 2 項目は PcmX/PcmY のスロットからの相対（StatusItems 参照）。
        for (int i = 0; i < StatusItems.Count; i++)
        {
            int idx = i;
            status.Add(new FieldDef("Status", StatusItems.Keys[idx], $"配置: {StatusItems.Labels[idx]} (x,y)",
                FieldKind.IntList, e => SkinLayoutIo.Join(e.statusPos[idx])));
        }

        var list = new List<FieldSectionDef>
        {
            new("画面", new List<FieldDef>
            {
                new("Screen", "Width", "幅", FieldKind.Int, e => $"{e.screenW}"),
                new("Screen", "Height", "高さ", FieldKind.Int, e => $"{e.screenH}"),
            }),
            new("鍵盤", new List<FieldDef>
            {
                new("Keyboard", "Pos", "位置 (x,y)", FieldKind.IntList, e => $"{e.kbX},{e.kbY}"),
                // XOffset (13個) はここには含めない。「1オクターブの鍵のX」として
                // 音名ラベル付きの 4 行（C,C#,D,D#／E,F,F#,G／G#,A,A#,B／
                // オクターブ幅）に SkinEditForm 側で専用に描画する
                // （LabeledValueRow。13個並びのスピンボタン列にはしない、
                // というユーザー指示）。
                new("Keyboard", "YOffset", "Y オフセット", FieldKind.Int, e => $"{e.kbYOffset}"),
                // ChannelY (9個) もここには含めない。「各チャンネルのY位置」として
                // FM1-4／FM5-8／PCM の3行に SkinEditForm 側で専用に描画する
                // （同じく LabeledValueRow。9個並びのスピンボタン列にはしない、
                // というユーザー指示）。
                new("Keyboard", "KeyOffset", "鍵の描画原点補正", FieldKind.Int, e => $"{e.keyOffset}"),
            }),
            new("ステータス", status),
            // ミニフォント（ステータス欄などのビットマップ文字）。1 文字の
            // 大きさは素材そのもので決まるので、ここにあるのは画面に置くときの
            // 送りだけ。素材の行は BitmapRowsByTab がこのタブの先頭に置く。
            new("ミニフォント", new List<FieldDef>
            {
                new("MiniFont", "Width", "送り幅", FieldKind.Int, e => $"{e.miniFontW}"),
                new("MiniFont", "Height", "行の高さ", FieldKind.Int, e => $"{e.miniFontH}"),
            }),
            new("バナー", new List<FieldDef>
            {
                new("Banner", "Rect", "矩形 (x,y,w,h)", FieldKind.Xywh,
                    e => $"{e.bannerX},{e.bannerY},{e.bannerW},{e.bannerH}"),
            }),
            new("曲名", new List<FieldDef>
            {
                new("Title", "Rect", "矩形 (x,y,w,h)", FieldKind.Xywh,
                    e => $"{e.titleX},{e.titleY},{e.titleW},{e.titleH}"),
            }),
            new("ファイラー", new List<FieldDef>
            {
                new("FileList", "Rect", "矩形 (x,y,w,h)", FieldKind.Xywh,
                    e => $"{e.fileListX},{e.fileListY},{e.fileListW},{e.fileListH}"),
                new("FileList", "Rows", "行数 (小,大)", FieldKind.IntList, e => SkinLayoutIo.Join(e.fileListRows)),
                new("FileList", "ItemHeight", "1行の高さ (小,大)", FieldKind.IntList,
                    e => SkinLayoutIo.Join(e.fileListItemH)),
                new("FileList", "BaseNameX", "ファイル名開始X (小,大)", FieldKind.IntList,
                    e => SkinLayoutIo.Join(e.fileListBaseNameX)),
                new("FileList", "BaseNameWidth", "ファイル名幅 (小,大)", FieldKind.IntList,
                    e => SkinLayoutIo.Join(e.fileListBaseNameW)),
                new("FileList", "TitleX", "曲名開始X (小,大)", FieldKind.IntList,
                    e => SkinLayoutIo.Join(e.fileListTitleX)),
                new("FileList", "TitleWidth", "曲名幅 (小,大)", FieldKind.IntList,
                    e => SkinLayoutIo.Join(e.fileListTitleW)),
            }),
            new("スクロールバー", new List<FieldDef>
            {
                new("ScrollBar", "Rect", "矩形 (x,y,w,h)", FieldKind.Xywh,
                    e => $"{e.scrollX},{e.scrollY},{e.scrollW},{e.scrollH}"),
                new("ScrollBar", "SrcThumb", "素材内: つまみ (x,y,w,h)", FieldKind.Xywh, e => e.scrollSrcThumb.ToString()),
                new("ScrollBar", "SrcUpArrowPress", "素材内: 上矢印(押下) (x,y,w,h)", FieldKind.Xywh,
                    e => e.scrollSrcUpArrowPress.ToString()),
                new("ScrollBar", "SrcDownArrowPress", "素材内: 下矢印(押下) (x,y,w,h)", FieldKind.Xywh,
                    e => e.scrollSrcDownArrowPress.ToString()),
                new("ScrollBar", "SrcUpArrow", "素材内: 上矢印 (x,y,w,h)", FieldKind.Xywh, e => e.scrollSrcUpArrow.ToString()),
                new("ScrollBar", "SrcBar", "素材内: 溝 (x,y,w,h)", FieldKind.Xywh, e => e.scrollSrcBar.ToString()),
                new("ScrollBar", "SrcDownArrow", "素材内: 下矢印 (x,y,w,h)", FieldKind.Xywh,
                    e => e.scrollSrcDownArrow.ToString()),
                new("ScrollBar", "PosUpArrow", "配置: 上矢印 (x,y)", FieldKind.IntList,
                    e => SkinLayoutIo.Join(e.scrollPosUpArrow)),
                new("ScrollBar", "PosBar", "配置: 溝 (x,y)", FieldKind.IntList, e => SkinLayoutIo.Join(e.scrollPosBar)),
                new("ScrollBar", "PosDownArrow", "配置: 下矢印 (x,y)", FieldKind.IntList,
                    e => SkinLayoutIo.Join(e.scrollPosDownArrow)),
            }),
            new("プログレスバー", new List<FieldDef>
            {
                new("ProgressBar", "Rect", "矩形 (x,y,w,h)", FieldKind.Xywh,
                    e => $"{e.progX},{e.progY},{e.progW},{e.progH}"),
                new("ProgressBar", "TimeX", "時刻表示 X オフセット", FieldKind.Int, e => $"{e.progTimeXOfs}"),
                new("ProgressBar", "TimeY", "時刻表示 Y オフセット", FieldKind.Int, e => $"{e.progTimeYOfs}"),
            }),
            new("音量バー", new List<FieldDef>
            {
                new("VolumeBar", "Rect", "矩形 (x,y,w,h)", FieldKind.Xywh,
                    e => $"{e.volX},{e.volY},{e.volW},{e.volH}"),
                new("VolumeBar", "TimeX", "時刻表示 X オフセット", FieldKind.Int, e => $"{e.volTimeXOfs}"),
                new("VolumeBar", "TimeY", "時刻表示 Y オフセット", FieldKind.Int, e => $"{e.volTimeYOfs}"),
                new("VolumeBar", "NobWidth", "つまみ幅", FieldKind.Int, e => $"{e.volNobW}"),
                new("VolumeBar", "NobSrc", "素材内: つまみ (x,y,w,h)", FieldKind.Xywh, e => e.volRect[0].ToString()),
                new("VolumeBar", "SlideSrc", "素材内: スライド (x,y,w,h)", FieldKind.Xywh, e => e.volRect[1].ToString()),
            }),
        };

        var playKey = new List<FieldDef>
        {
            new("PlayKey", "Pos", "位置 (x,y)", FieldKind.IntList, e => $"{e.playKeyX},{e.playKeyY}"),
            // SHUFFLE (index 8) が未実装で当分実装の予定も無いので、
            // 9 にして出してしまわないよう編集不可にする（ユーザー指示）。
            new("PlayKey", "Count", "使うボタンの数", FieldKind.Int, e => $"{e.numPlayKeys}", ReadOnly: true),
        };
        string[] names = { "PREV", "STOP", "PLAY", "FAST", "PAUSE", "NEXT", "CONT", "REPEAT", "SHUFFLE" };
        // SHUFFLE (index 8) は当分実装の予定が無いので出さない（ユーザー指示）。
        // playKey.Add(new FieldDef("PlayKey", "Src8", "素材内: SHUFFLE (x,y,w,h)", FieldKind.Xywh,
        //     e => e.playKeyRect[8].ToString()));
        for (int i = 0; i < 8; i++)
        {
            int idx = i;
            playKey.Add(new FieldDef("PlayKey", $"Src{idx}", $"素材内: {names[idx]} (x,y,w,h)", FieldKind.Xywh,
                e => e.playKeyRect[idx].ToString()));
        }
        for (int i = 0; i < 9; i++)
        {
            int idx = i;
            playKey.Add(new FieldDef("PlayKey", $"Pos{idx}", $"配置: {names[idx]} (x,y)", FieldKind.IntList,
                e => SkinLayoutIo.Join(e.playKeyPos[idx])));
        }
        playKey.Add(new FieldDef("PlayKey", "PalKey", "パレット: ボタンの色", FieldKind.Int, e => $"{e.palPlayKeyKey}"));
        playKey.Add(new FieldDef("PlayKey", "PalPlayLed", "パレット: PLAY LED", FieldKind.Int, e => $"{e.palPlayLed}"));
        playKey.Add(new FieldDef("PlayKey", "PalPauseLed", "パレット: PAUSE LED", FieldKind.Int, e => $"{e.palPauseLed}"));
        playKey.Add(new FieldDef("PlayKey", "PalContLed", "パレット: CONT LED", FieldKind.Int, e => $"{e.palContLed}"));
        playKey.Add(new FieldDef("PlayKey", "PalRepeatLed", "パレット: REPEAT LED", FieldKind.Int, e => $"{e.palRepeatLed}"));
        playKey.Add(new FieldDef("PlayKey", "PalDark", "パレット: 消灯", FieldKind.Int, e => $"{e.palDark}"));
        playKey.Add(new FieldDef("PlayKey", "PalRed", "パレット: 赤", FieldKind.Int, e => $"{e.palRed}"));
        playKey.Add(new FieldDef("PlayKey", "PalGreen", "パレット: 緑", FieldKind.Int, e => $"{e.palGreen}"));
        list.Add(new FieldSectionDef("操作ボタン", playKey));

        return list;
    }
}
