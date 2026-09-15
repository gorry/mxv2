// mxv2 スキンエディタ - layout.ini の項目そのものの定義（値の側）。
//
// **ここには「画面のどこに出るか」は書かない。** 並び順・タブ・サブタブ・
// 素材行との前後関係・見出しといった配置の話は UI/TabLayout.cs が持つ
// （2026-09-06 に分離。それまではこのファイルの並び順がそのまま画面の
// 並び順で、配置の都合で増えた指定 Group/SubTab/LeadingBitmaps/
// TrailingBitmaps/BitmapShortLabel がこのレコードに溜まっていた）。
//
// 表示文字列は src/skin.h と同じ書式（カンマ区切り）。値そのものの読み書きは
// SkinDocument.SetLayoutRaw / OwnLayoutIni がそのまま文字列で扱うので、
// ここでは「表示ラベル」「今の実効値をどう文字列にするか」「値の取りうる
// 範囲」だけを持つ。

namespace SkinEditor.Model;

// Enum は「決まった名前のどれか」（今は [Screen] FilerSide だけ）。
// FieldEditControl は数値欄ではなくコンボボックスを出し、値は
// FieldDef.EnumNames の名前そのものを layout.ini へ書く。
public enum FieldKind { Int, IntList, Xywh, Str, Enum }

// ReadOnly は「値は表示するが、継承チェックボックスも数値欄も編集不可にする」
// 指定（FieldEditControl 側で見る）。SHUFFLE 未実装の間、[PlayKey] Count を
// うっかり 9 にしてボタンを増やせないようにするために追加した。
//
// Suffix は「(x,y)」「(x,y,w,h)」「(小,大)」のような、値の並び順や意味を示す
// 注記。ラベルの末尾に埋め込まず、FieldEditControl が数値欄の右へ別に描く
// （2026-09-03、ユーザー指示。ラベルと数値欄の間が間延びして見えるのを
// 避けるため）。
//
// IntMin/IntMax はスピンボタンの範囲（`skineditor_spin_ranges.md` で
// ユーザーがレビューした値）。既定は「(x,y)」系の項目にそのまま使える
// -9999〜9999。ComponentMin/ComponentMax は Xywh（x,y,w,h の4値）で
// w,h だけ別の範囲にしたいときの上書き（配列の添字は x,y,w,h の並びと
// 一致させる。null なら IntMin/IntMax がそのまま全部の値に掛かる）。
// SizeBoundRole は「素材内: ...」のような、値が実際にインポートされている
// 素材のサイズを超えられない項目に付ける。付いている場合、x,y,w,h の Max は
// ComponentMax の値ではなく `SkinDocument.ResolveBitmapSize` で得た実際の
// 素材の大きさから決まる（素材が見つからないときだけ ComponentMax へ
// フォールバック）。2026-09-05〜06、ユーザー指示。
public sealed record FieldDef(string Section, string Key, string Label, FieldKind Kind,
    Func<SkinLayout, string> Format, bool ReadOnly = false, string Suffix = "",
    int IntMin = -9999, int IntMax = 9999, int[]? ComponentMin = null, int[]? ComponentMax = null,
    BitmapRole? SizeBoundRole = null, string[]? EnumNames = null, string[]? EnumLabels = null);

public static class LayoutFieldSchema
{
    // 「x,y は符号付き 5 桁ぶん・w,h は符号無し 4 桁ぶん」という矩形の既定。
    // 素材内の矩形（Src 系）は x,y も 0 始まりなのでこちらではない。
    private static readonly int[] RectMin = { -9999, -9999, 1, 1 };
    private static readonly int[] RectMax = { 9999, 9999, 9999, 9999 };
    // 素材内の矩形。x,y は 0 始まり、w,h は 1 以上（実際の上限は素材の大きさ）。
    private static readonly int[] SrcMin = { 0, 0, 1, 1 };
    private static readonly int[] SrcMax = { 9999, 9999, 9999, 9999 };

    private static List<FieldDef>? _all;

    // 全項目。並び順に意味は無い（画面の並びは UI/TabLayout.cs）。
    public static IReadOnlyList<FieldDef> All() => _all ??= Build();

    // Section/Key で 1 項目を引く。TabLayout の書き間違いは
    // ここで例外になって起動時に分かる（テストでも突き合わせている）。
    public static FieldDef Get(string section, string key)
    {
        foreach (var f in All())
        {
            if (f.Section == section && f.Key == key) return f;
        }
        throw new KeyNotFoundException($"layout.ini の項目 {section}/{key} は LayoutFieldSchema に無い");
    }

    private static List<FieldDef> Build()
    {
        var list = new List<FieldDef>
        {
            // ---- 画面 ------------------------------------------------------
            new("Screen", "Width", "幅", FieldKind.Int, e => $"{e.screenW}", IntMin: 1, IntMax: 9999),
            new("Screen", "Height", "高さ", FieldKind.Int, e => $"{e.screenH}", IntMin: 1, IntMax: 9999),
            // 画面を 2 つに分ける位置（fullscreen.md）。キャンバスが伸びたぶんは
            // すべてファイラー側が受け取り、ファイラー以外側の厚みは変わらない。
            new("Screen", "FilerSide", "ファイラーを置く辺", FieldKind.Enum,
                e => FilerSides.Name(e.filerSide),
                EnumNames: FilerSides.Names, EnumLabels: FilerSides.Labels),
            new("Screen", "FilerExtent", "ファイラー側の厚み", FieldKind.Int,
                e => $"{e.filerExtent}", IntMin: 1, IntMax: 9999),

            // ---- 鍵盤 ------------------------------------------------------
            new("Keyboard", "Pos", "位置", FieldKind.IntList, e => $"{e.kbX},{e.kbY}", Suffix: "(x,y)"),
            new("Keyboard", "YOffset", "Y オフセット", FieldKind.Int, e => $"{e.kbYOffset}"),
            new("Keyboard", "KeyOffset", "鍵の描画原点補正", FieldKind.Int, e => $"{e.keyOffset}",
                IntMin: 0, IntMax: 12),
            // XOffset (13個) と ChannelY (9個) はここに無い。音名／チャンネル名の
            // ラベル付きで数個ずつの行に分けて描くので（LabeledValueRow）、
            // 1 項目 1 行の FieldDef では表せない。配置ともども TabLayout 側。

            // ---- ステータス ------------------------------------------------
            // x,y は 9 段全体の左上、w,h は 1 段ぶんの背景の大きさ。
            new("Status", "Rect", "矩形", FieldKind.Xywh,
                e => $"{e.statusX},{e.statusY},{e.statusW},{e.statusH}", Suffix: "(x,y,w,h)",
                ComponentMin: new[] { -9999, -9999, 0, 0 }, ComponentMax: RectMax),
            new("LevelMeter", "PaletteOffset", "パレット開始番号", FieldKind.Int,
                e => $"{e.levelMeterPalOfs}", IntMin: 0, IntMax: 255),
            new("LevelMeter", "Cells", "セル数", FieldKind.Int,
                e => $"{e.levelMeterWidthCells}", IntMin: 0, IntMax: 192),
            new("LevelMeter", "SrcX", "素材内: 左端の切り捨て", FieldKind.Int,
                e => $"{e.levelMeterSrcX}", IntMin: 0, IntMax: 9999),
            // PcmX/PcmY (各8個) もここに無い（「PCM 1ch」〜「PCM 8ch」の
            // (x,y) 行に分けて描く。PcmChannelRow）。

            // ---- ミニフォント ----------------------------------------------
            // 1 文字の大きさは素材そのもので決まるので、あるのは送りだけ。
            new("MiniFont", "Width", "送り幅", FieldKind.Int, e => $"{e.miniFontW}", IntMin: 0, IntMax: 9999),
            new("MiniFont", "Height", "行の高さ", FieldKind.Int, e => $"{e.miniFontH}", IntMin: 0, IntMax: 9999),

            // ---- OPM レジスタ一覧（regmap.md） -----------------------------
            // 鍵盤の長押しで出すオーバーレイ。矩形と、その左上からの文字の描き始め。
            new("RegMap", "Rect", "矩形", FieldKind.Xywh,
                e => $"{e.regMapX},{e.regMapY},{e.regMapW},{e.regMapH}", Suffix: "(x,y,w,h)",
                ComponentMin: RectMin, ComponentMax: RectMax),
            new("RegMap", "Pos", "文字の位置", FieldKind.IntList,
                e => $"{e.regMapPosX},{e.regMapPosY}", Suffix: "(x,y)"),

            // ---- バナー / 曲名 ---------------------------------------------
            new("Banner", "Rect", "矩形", FieldKind.Xywh,
                e => $"{e.bannerX},{e.bannerY},{e.bannerW},{e.bannerH}", Suffix: "(x,y,w,h)",
                ComponentMin: RectMin, ComponentMax: RectMax),
            new("Title", "Rect", "矩形", FieldKind.Xywh,
                e => $"{e.titleX},{e.titleY},{e.titleW},{e.titleH}", Suffix: "(x,y,w,h)",
                ComponentMin: RectMin, ComponentMax: RectMax),
            // 100 で「文字の高さ × 40/24 px/秒」。曲名欄とファイラーで式は同じ。
            new("Title", "ScrollSpeed", "スクロール速度", FieldKind.Int,
                e => $"{e.titleScrollSpeed}", Suffix: "(%)", IntMin: 1, IntMax: 1000),

            // ---- ファイラー ------------------------------------------------
            // 矩形は「ファイラー側の矩形からの内側マージン」で書く。行数と
            // 曲名幅は矩形から求まるので項目が無い（fullscreen.md）。
            new("FileList", "Margin", "内側マージン", FieldKind.IntList,
                e => SkinLayoutIo.Join(e.fileListMargin), Suffix: "(左,上,右,下)",
                IntMin: 0, IntMax: 9999),
            new("FileList", "ItemHeight", "1行の高さ", FieldKind.IntList,
                e => SkinLayoutIo.Join(e.fileListItemH), Suffix: "(小,大)", IntMin: 1, IntMax: 999),
            new("FileList", "BaseNameX", "ファイル名開始X", FieldKind.IntList,
                e => SkinLayoutIo.Join(e.fileListBaseNameX), Suffix: "(小,大)", IntMin: 0, IntMax: 9999),
            new("FileList", "BaseNameWidth", "ファイル名幅", FieldKind.IntList,
                e => SkinLayoutIo.Join(e.fileListBaseNameW), Suffix: "(小,大)", IntMin: 1, IntMax: 9999),
            new("FileList", "TitleX", "曲名開始X", FieldKind.IntList,
                e => SkinLayoutIo.Join(e.fileListTitleX), Suffix: "(小,大)", IntMin: 0, IntMax: 9999),
            new("FileList", "ScrollSpeed", "曲名スクロール速度", FieldKind.Int,
                e => $"{e.fileListScrollSpeed}", Suffix: "(%)", IntMin: 1, IntMax: 1000),

            // ---- スクロールバー --------------------------------------------
            // 位置はファイラーの矩形から決まる。書くのは幅だけで、
            // 当たり判定は描画より広くできる（左へ広がる）。
            new("ScrollBar", "Width", "描く幅", FieldKind.Int,
                e => $"{e.scrollWidth}", IntMin: 0, IntMax: 9999),
            new("ScrollBar", "HitWidth", "当たり判定の幅", FieldKind.Int,
                e => $"{e.scrollHitWidth}", IntMin: 0, IntMax: 9999),
            new("ScrollBar", "SrcThumb", "素材内: つまみ", FieldKind.Xywh,
                e => e.scrollSrcThumb.ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.ScrollBar),
            new("ScrollBar", "SrcUpArrowPress", "素材内: 上矢印(押下)", FieldKind.Xywh,
                e => e.scrollSrcUpArrowPress.ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.ScrollBar),
            new("ScrollBar", "SrcDownArrowPress", "素材内: 下矢印(押下)", FieldKind.Xywh,
                e => e.scrollSrcDownArrowPress.ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.ScrollBar),
            new("ScrollBar", "SrcUpArrow", "素材内: 上矢印", FieldKind.Xywh,
                e => e.scrollSrcUpArrow.ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.ScrollBar),
            new("ScrollBar", "SrcBar", "素材内: 溝", FieldKind.Xywh,
                e => e.scrollSrcBar.ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.ScrollBar),
            new("ScrollBar", "SrcDownArrow", "素材内: 下矢印", FieldKind.Xywh,
                e => e.scrollSrcDownArrow.ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.ScrollBar),

            // ---- プログレスバー --------------------------------------------
            new("ProgressBar", "Rect", "矩形", FieldKind.Xywh,
                e => $"{e.progX},{e.progY},{e.progW},{e.progH}", Suffix: "(x,y,w,h)",
                ComponentMin: RectMin, ComponentMax: RectMax),
            new("ProgressBar", "TimePos", "時刻表示位置", FieldKind.IntList,
                e => SkinLayoutIo.Join(e.progTimePos), Suffix: "(x,y)"),
            // バーの素材を置く位置（Rect の左上からの相対）。
            new("ProgressBar", "ProgressPos", "位置", FieldKind.IntList,
                e => SkinLayoutIo.Join(e.progPos), Suffix: "(x,y)"),
            new("ProgressBar", "SrcBarLeft", "素材内: バー左端", FieldKind.Xywh,
                e => e.progSrcBarLeft.ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.ProgressBar),
            new("ProgressBar", "SrcBarRight", "素材内: バー右端", FieldKind.Xywh,
                e => e.progSrcBarRight.ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.ProgressBar),
            new("ProgressBar", "SrcBar", "素材内: バー中央", FieldKind.Xywh,
                e => e.progSrcBar.ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.ProgressBar),

            // ---- 音量バー --------------------------------------------------
            new("VolumeBar", "Rect", "矩形", FieldKind.Xywh,
                e => $"{e.volX},{e.volY},{e.volW},{e.volH}", Suffix: "(x,y,w,h)",
                ComponentMin: RectMin, ComponentMax: RectMax),
            new("VolumeBar", "VolumePos", "音量表示位置", FieldKind.IntList,
                e => SkinLayoutIo.Join(e.volVolumePos), Suffix: "(x,y)"),
            new("VolumeBar", "SrcThumb", "素材内: つまみ", FieldKind.Xywh,
                e => e.volSrcThumb.ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.VolumeBar),
            new("VolumeBar", "SrcBarLeft", "素材内: バー左端", FieldKind.Xywh,
                e => e.volSrcBarLeft.ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.VolumeBar),
            new("VolumeBar", "SrcBarRight", "素材内: バー右端", FieldKind.Xywh,
                e => e.volSrcBarRight.ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.VolumeBar),
            new("VolumeBar", "SrcBar", "素材内: バー中央", FieldKind.Xywh,
                e => e.volSrcBar.ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.VolumeBar),

            // ---- 操作ボタン ------------------------------------------------
            // x,y は Pos<n> の原点、w,h は使うボタン全体を覆う大きさ。
            new("PlayKey", "Rect", "矩形", FieldKind.Xywh,
                e => $"{e.playKeyX},{e.playKeyY},{e.playKeyW},{e.playKeyH}", Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax),
            // SHUFFLE (index 8) が未実装で当分実装の予定も無いので、
            // 9 にして出してしまわないよう編集不可にする（ユーザー指示）。
            new("PlayKey", "Count", "使うボタンの数", FieldKind.Int, e => $"{e.numPlayKeys}",
                ReadOnly: true, IntMin: 1, IntMax: 9),
            new("PlayKey", "PalKey", "ボタンの色", FieldKind.Int, e => $"{e.palPlayKeyKey}", IntMin: 0, IntMax: 255),
            new("PlayKey", "PalDark", "LED: 消灯", FieldKind.Int, e => $"{e.palDark}", IntMin: 0, IntMax: 255),
            new("PlayKey", "PalRed", "LED: 赤", FieldKind.Int, e => $"{e.palRed}", IntMin: 0, IntMax: 255),
            new("PlayKey", "PalGreen", "LED: 緑", FieldKind.Int, e => $"{e.palGreen}", IntMin: 0, IntMax: 255),
            new("PlayKey", "PalYellow", "LED: 黄", FieldKind.Int, e => $"{e.palYellow}", IntMin: 0, IntMax: 255),
            new("PlayKey", "PalBlue", "LED: 青", FieldKind.Int, e => $"{e.palBlue}", IntMin: 0, IntMax: 255),
            new("PlayKey", "PalPlayLed", "LEDのパレット", FieldKind.Int, e => $"{e.palPlayLed}", IntMin: 0, IntMax: 255),
            new("PlayKey", "PalPauseLed", "LEDのパレット", FieldKind.Int, e => $"{e.palPauseLed}", IntMin: 0, IntMax: 255),
            new("PlayKey", "PalContLed", "LEDのパレット", FieldKind.Int, e => $"{e.palContLed}", IntMin: 0, IntMax: 255),
            new("PlayKey", "PalRepeatLed", "LEDのパレット", FieldKind.Int, e => $"{e.palRepeatLed}", IntMin: 0, IntMax: 255),
        };

        // ステータス 1 段の中での各項目の位置（StatusItems の 18 項目）。
        // FM の 16 項目は [Status] Rect の左上からの相対、PCM の 2 項目は
        // PcmX/PcmY のスロットからの相対。レベルメータだけはラベルが
        // 「位置」（サブタブ名で何のことか分かるため。2026-09-04、ユーザー指示）。
        for (int i = 0; i < StatusItems.Count; i++)
        {
            int idx = i;
            string label = idx == (int)StatusItem.LevelMeter ? "位置" : StatusItems.Labels[idx];
            list.Add(new FieldDef("Status", StatusItems.Keys[idx], label, FieldKind.IntList,
                e => SkinLayoutIo.Join(e.statusPos[idx]), Suffix: "(x,y)"));
        }

        // 操作ボタン 8 個ぶんの「素材内位置」「配置」。
        // SHUFFLE (index 8) は当分実装の予定が無いので出さない。
        for (int i = 0; i < 8; i++)
        {
            int idx = i;
            list.Add(new FieldDef("PlayKey", $"Src{idx}", "素材内位置", FieldKind.Xywh,
                e => e.playKeyRect[idx].ToString(), Suffix: "(x,y,w,h)",
                ComponentMin: SrcMin, ComponentMax: SrcMax, SizeBoundRole: BitmapRole.PlayKey));
            list.Add(new FieldDef("PlayKey", $"Pos{idx}", "配置", FieldKind.IntList,
                e => SkinLayoutIo.Join(e.playKeyPos[idx]), Suffix: "(x,y)"));
        }

        return list;
    }
}
