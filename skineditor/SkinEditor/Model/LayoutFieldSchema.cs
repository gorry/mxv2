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
//
// Suffix は「(x,y)」「(x,y,w,h)」のような、値の並び順を示す注記。ラベルの
// 末尾に埋め込まず、FieldEditControl が数値欄の右へ別に描く（2026-09-03、
// ユーザー指示。ラベルと数値欄の間が間延びして見えるのを避けるため）。
// 「(小,大)」（FileList の 2 値）は並び順ではなく意味の違いを示す注記なので
// 対象外で、これまでどおりラベルに埋め込んだまま。
//
// Group は同じタブの中でさらに項目をまとめたいときの見出し。空でなければ、
// 直前の項目と Group が変わったところに太字の見出し行を挟み、その項目自体も
// インデントする（SkinEditForm 側）。タブそのものを分けるほどではないが、
// ひとまとまりだと分かってほしい項目向け（2026-09-03、ユーザー指示。
// 「ステータス」タブの「レベルメータ」「配置」に使った）。
//
// LeadingBitmaps / TrailingBitmaps は、この項目の直前／直後に素材の
// インポート行を置きたいときに使う。「このタブ（またはこのグループ）には
// 必ずこの素材がある」という関連付けではなく、**たまたまこのページの
// この項目の隣にその素材行がある、というだけの位置情報**にしてある
// （2026-09-04、ユーザー指示。以前は BitmapRowsByTab / GroupBitmapRole と
// いう「タブ名／グループ名 → 素材」の対応表が SkinEditForm.cs にあったが、
// それをやめてここへ一本化した）。BitmapShortLabel は「素材: 役割名
// (ファイル名)」ではなく「素材 (ファイル名)」と短く出す指定
// （見出しなどで役割名が既に分かっているとき用）。
public sealed record FieldDef(string Section, string Key, string Label, FieldKind Kind,
    Func<SkinLayout, string> Format, bool ReadOnly = false, string Suffix = "", string Group = "",
    IReadOnlyList<BitmapRole>? LeadingBitmaps = null, IReadOnlyList<BitmapRole>? TrailingBitmaps = null,
    bool BitmapShortLabel = false);

public sealed record FieldSectionDef(string Title, IReadOnlyList<FieldDef> Fields);

public static class LayoutFieldSchema
{
    public static IReadOnlyList<FieldSectionDef> BuildSections()
    {
        var status = new List<FieldDef>
        {
            // ステータス欄全体の位置・大きさなので、タブの一番上（レベルメータ
            // グループより前）に置く（2026-09-04、ユーザー指示）。
            new("Status", "Pos", "位置", FieldKind.IntList, e => $"{e.statusX},{e.statusY}", Suffix: "(x,y)"),
            new("Status", "BackWidth", "背景幅", FieldKind.Int, e => $"{e.statusBackW}"),
            new("Status", "BackHeight", "背景高さ", FieldKind.Int, e => $"{e.statusBackH}"),
            // 「レベルメータ」タブは廃止してここへ統合した。素材行は
            // このすぐ下の項目の直前（＝「レベルメータ」見出しの直後）に
            // たまたま置いてある、というだけ。
            new("LevelMeter", "PaletteOffset", "パレット開始番号", FieldKind.Int, e => $"{e.levelMeterPalOfs}",
                Group: "レベルメータ", LeadingBitmaps: new[] { BitmapRole.LevelMeter }, BitmapShortLabel: true),
            new("LevelMeter", "Cells", "セル数", FieldKind.Int, e => $"{e.levelMeterWidthCells}",
                Group: "レベルメータ"),
            new("LevelMeter", "SrcX", "素材内: 左端の切り捨て", FieldKind.Int, e => $"{e.levelMeterSrcX}",
                Group: "レベルメータ"),
            // ミニフォントは 2026-09-03 に独立したタブへ戻した（ユーザー指示）。
            // PcmX/PcmY (各8個) はここには含めない。「PCM 1ch」～「PCM 8ch」の
            // (x,y) 2値編集として SkinEditForm 側で専用に描画する
            // （PcmChannelRow。8個のスピンボタン列にはしない、というユーザー指示）。
        };
        // 1 段の中での各項目の位置。FM の 16 項目は [Status] Pos からの相対、
        // PCM の 2 項目は PcmX/PcmY のスロットからの相対（StatusItems 参照）。
        for (int i = 0; i < StatusItems.Count; i++)
        {
            int idx = i;
            // 「配置」グループの見出しで分かるので、項目名に「配置: 」は
            // 付けない（2026-09-04、ユーザー指示）。
            status.Add(new FieldDef("Status", StatusItems.Keys[idx], StatusItems.Labels[idx],
                FieldKind.IntList, e => SkinLayoutIo.Join(e.statusPos[idx]), Suffix: "(x,y)", Group: "配置"));
        }

        var list = new List<FieldSectionDef>
        {
            new("画面", new List<FieldDef>
            {
                // back.bmp の素材行は、たまたまこの項目の直前にある。
                new("Screen", "Width", "幅", FieldKind.Int, e => $"{e.screenW}",
                    LeadingBitmaps: new[] { BitmapRole.Back }),
                new("Screen", "Height", "高さ", FieldKind.Int, e => $"{e.screenH}"),
            }),
            new("鍵盤", new List<FieldDef>
            {
                // kb0/kb1/kb2 の素材行は、たまたま「位置」の直後（「1オクターブの
                // 鍵のX」より前）にある。「位置」を一番上に出す都合で、この項目の
                // 自分の入力欄の後・関連グループの前、という位置になる
                // （2026-09-04、ユーザー指示）。
                new("Keyboard", "Pos", "位置", FieldKind.IntList, e => $"{e.kbX},{e.kbY}", Suffix: "(x,y)",
                    TrailingBitmaps: new[] { BitmapRole.Kb0, BitmapRole.Kb1, BitmapRole.Kb2 }),
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
            // 送りだけ。素材行は、たまたまこのタブの最初の項目の直前にある。
            new("ミニフォント", new List<FieldDef>
            {
                new("MiniFont", "Width", "送り幅", FieldKind.Int, e => $"{e.miniFontW}",
                    LeadingBitmaps: new[] { BitmapRole.MiniFont }),
                new("MiniFont", "Height", "行の高さ", FieldKind.Int, e => $"{e.miniFontH}"),
            }),
            new("バナー", new List<FieldDef>
            {
                new("Banner", "Rect", "矩形", FieldKind.Xywh,
                    e => $"{e.bannerX},{e.bannerY},{e.bannerW},{e.bannerH}", Suffix: "(x,y,w,h)",
                    LeadingBitmaps: new[] { BitmapRole.Banner }),
            }),
            new("曲名", new List<FieldDef>
            {
                new("Title", "Rect", "矩形", FieldKind.Xywh,
                    e => $"{e.titleX},{e.titleY},{e.titleW},{e.titleH}", Suffix: "(x,y,w,h)"),
            }),
            new("ファイラー", new List<FieldDef>
            {
                new("FileList", "Rect", "矩形", FieldKind.Xywh,
                    e => $"{e.fileListX},{e.fileListY},{e.fileListW},{e.fileListH}", Suffix: "(x,y,w,h)"),
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
                new("ScrollBar", "Rect", "矩形", FieldKind.Xywh,
                    e => $"{e.scrollX},{e.scrollY},{e.scrollW},{e.scrollH}", Suffix: "(x,y,w,h)",
                    LeadingBitmaps: new[] { BitmapRole.ScrollBar }),
                new("ScrollBar", "SrcThumb", "素材内: つまみ", FieldKind.Xywh, e => e.scrollSrcThumb.ToString(),
                    Suffix: "(x,y,w,h)"),
                new("ScrollBar", "SrcUpArrowPress", "素材内: 上矢印(押下)", FieldKind.Xywh,
                    e => e.scrollSrcUpArrowPress.ToString(), Suffix: "(x,y,w,h)"),
                new("ScrollBar", "SrcDownArrowPress", "素材内: 下矢印(押下)", FieldKind.Xywh,
                    e => e.scrollSrcDownArrowPress.ToString(), Suffix: "(x,y,w,h)"),
                new("ScrollBar", "SrcUpArrow", "素材内: 上矢印", FieldKind.Xywh, e => e.scrollSrcUpArrow.ToString(),
                    Suffix: "(x,y,w,h)"),
                new("ScrollBar", "SrcBar", "素材内: 溝", FieldKind.Xywh, e => e.scrollSrcBar.ToString(),
                    Suffix: "(x,y,w,h)"),
                new("ScrollBar", "SrcDownArrow", "素材内: 下矢印", FieldKind.Xywh,
                    e => e.scrollSrcDownArrow.ToString(), Suffix: "(x,y,w,h)"),
                new("ScrollBar", "PosUpArrow", "配置: 上矢印", FieldKind.IntList,
                    e => SkinLayoutIo.Join(e.scrollPosUpArrow), Suffix: "(x,y)"),
                new("ScrollBar", "PosBar", "配置: 溝", FieldKind.IntList, e => SkinLayoutIo.Join(e.scrollPosBar),
                    Suffix: "(x,y)"),
                new("ScrollBar", "PosDownArrow", "配置: 下矢印", FieldKind.IntList,
                    e => SkinLayoutIo.Join(e.scrollPosDownArrow), Suffix: "(x,y)"),
            }),
            new("プログレスバー", new List<FieldDef>
            {
                new("ProgressBar", "Rect", "矩形", FieldKind.Xywh,
                    e => $"{e.progX},{e.progY},{e.progW},{e.progH}", Suffix: "(x,y,w,h)",
                    LeadingBitmaps: new[] { BitmapRole.ProgressBar }),
                new("ProgressBar", "TimeX", "時刻表示 X オフセット", FieldKind.Int, e => $"{e.progTimeXOfs}"),
                new("ProgressBar", "TimeY", "時刻表示 Y オフセット", FieldKind.Int, e => $"{e.progTimeYOfs}"),
            }),
            new("音量バー", new List<FieldDef>
            {
                new("VolumeBar", "Rect", "矩形", FieldKind.Xywh,
                    e => $"{e.volX},{e.volY},{e.volW},{e.volH}", Suffix: "(x,y,w,h)",
                    LeadingBitmaps: new[] { BitmapRole.VolumeBar }),
                new("VolumeBar", "TimeX", "時刻表示 X オフセット", FieldKind.Int, e => $"{e.volTimeXOfs}"),
                new("VolumeBar", "TimeY", "時刻表示 Y オフセット", FieldKind.Int, e => $"{e.volTimeYOfs}"),
                new("VolumeBar", "NobWidth", "つまみ幅", FieldKind.Int, e => $"{e.volNobW}"),
                new("VolumeBar", "NobSrc", "素材内: つまみ", FieldKind.Xywh, e => e.volRect[0].ToString(),
                    Suffix: "(x,y,w,h)"),
                new("VolumeBar", "SlideSrc", "素材内: スライド", FieldKind.Xywh, e => e.volRect[1].ToString(),
                    Suffix: "(x,y,w,h)"),
            }),
        };

        var playKey = new List<FieldDef>
        {
            new("PlayKey", "Pos", "位置", FieldKind.IntList, e => $"{e.playKeyX},{e.playKeyY}", Suffix: "(x,y)",
                LeadingBitmaps: new[] { BitmapRole.PlayKey }),
            // SHUFFLE (index 8) が未実装で当分実装の予定も無いので、
            // 9 にして出してしまわないよう編集不可にする（ユーザー指示）。
            new("PlayKey", "Count", "使うボタンの数", FieldKind.Int, e => $"{e.numPlayKeys}", ReadOnly: true),
        };
        string[] names = { "PREV", "STOP", "PLAY", "FAST", "PAUSE", "NEXT", "CONT", "REPEAT", "SHUFFLE" };
        // SHUFFLE (index 8) は当分実装の予定が無いので出さない（ユーザー指示）。
        // playKey.Add(new FieldDef("PlayKey", "Src8", "素材内: SHUFFLE", FieldKind.Xywh,
        //     e => e.playKeyRect[8].ToString(), Suffix: "(x,y,w,h)"));
        for (int i = 0; i < 8; i++)
        {
            int idx = i;
            playKey.Add(new FieldDef("PlayKey", $"Src{idx}", $"素材内: {names[idx]}", FieldKind.Xywh,
                e => e.playKeyRect[idx].ToString(), Suffix: "(x,y,w,h)"));
        }
        for (int i = 0; i < 9; i++)
        {
            int idx = i;
            playKey.Add(new FieldDef("PlayKey", $"Pos{idx}", $"配置: {names[idx]}", FieldKind.IntList,
                e => SkinLayoutIo.Join(e.playKeyPos[idx]), Suffix: "(x,y)"));
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
