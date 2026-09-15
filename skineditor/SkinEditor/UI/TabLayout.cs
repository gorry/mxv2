// mxv2 スキンエディタ - 編集画面のタブの中身（画面の並びそのもの）。
//
// **このファイルが画面の見た目を決める唯一の場所。** 上から下へ書いた順に
// 画面へ並ぶので、タブを開いたときの並びと突き合わせて読める。値そのものの
// 定義（ラベル・範囲・ini のキー）は Model/LayoutFieldSchema.cs 側。
//
// 2026-09-06 にこの形へ変えた。それまでは LayoutFieldSchema の並び順が画面の
// 並び順を兼ねていて、そこで表せない配置（素材行の位置・サブタブ・特定タブ
// だけのトグルやスライダ）は SkinEditForm.cs の
// `if (section.Title == "…")` という文字列キーの特例分岐（7 か所）と、
// FieldDef に足された配置用の引数（Group/SubTab/LeadingBitmaps/
// TrailingBitmaps/BitmapShortLabel）で表現していた。
// 動的に増減する項目は 1 つも無いので、素直に静的な並びとして書けばよかった。
//
// **項目の付け忘れ・重複は TabLayoutTests が拾う**（旧方式はスキーマを
// 全部なめる作りだったので構造的に取りこぼしが起きなかった。宣言に変えた
// ぶん、その保証はテストで担保する）。

using SkinEditor.Model;

namespace SkinEditor.UI;

public abstract record Node;

// layout.ini の 1 項目（FieldEditControl の 1 行）。
public sealed record FieldNode(string Section, string Key) : Node;

// 素材のインポート行。ShortLabel は「素材: 役割名 (ファイル名)」ではなく
// 「素材 (ファイル名)」と短く出す指定（サブタブ名で役割が分かるとき用）。
public sealed record BitmapNode(BitmapRole Role, bool ShortLabel = false) : Node;

// 太字の「配色」見出し＋その colors.ini セクションの全項目。
public sealed record ColorsNode(string SectionTitle) : Node;

// 入れ子のタブ。高さは全ページ共通（TabControl はページごとに高さを変えられ
// ない）なので、一番背の高いページに合わせる必要がある。
//
// Height は既定の 0 なら**実行時に算出する**（全ページの中身を測り、どの
// ページも縦スクロールバーが出ない最小の高さにする。TabPageBuilder.
// ApplyAutoHeights）。それまでは 480 / 320 という手で詰めた数値を書いて
// いたが、「一番項目数が多いのはどのページか」を人が数え間違えると
// スクロールバーが出たり余白が余ったりする（実際に両方やった）。
// 0 より大きい値を入れれば、その高さに固定できる（今のところ使っていない）。
public sealed record SubTabsNode(params (string Title, Node[] Nodes)[] Pages) : Node
{
    public int Height { get; init; }

    // 選ばれているページが変わったときに呼ばれる（選ばれたページの題名）。
    // プレビューの状態をページに連動させるのに使う（[ステータス] の
    // 音色データ表示。2026-09-15、ユーザーの指示でトグルをやめて自動にした。
    // 親のタブを離れても戻さない——サブタブの切り替えだけが表示の切り替え）。
    public Action<PreviewCanvas, string>? OnPageSelected { get; init; }
}

// 「1 キーに複数個の値」を、短いラベル付きで数個ずつの行に分けたもの。
// Rows の 1 要素が 1 行ぶんのラベル（[Keyboard] XOffset なら音名 4 個ずつ）。
public sealed record MultiValueNode(
    string CheckLabel, string Section, string Key,
    Func<SkinLayout, int[]> Select, string[][] Rows, string CheckRegion,
    int Min = -9999, int Max = 9999) : Node;

// [Status] PcmX/PcmY を「PCM 1ch」〜「PCM 8ch」の (x,y) 行に分けたもの。
public sealed record PcmChannelsNode : Node;

// プレビュー確認用のスライダ。layout.ini には何も書かない一時的な状態。
// TopMargin は直前の行との間隔（サブタブの中に置く「レベル」だけ 8px、
// タブ直下に置くものは 12px。現状の見た目をそのまま踏襲した値）。
public sealed record SliderNode(
    string Label, int Min, int Max, int Init, Action<PreviewCanvas, int> Set, string Region,
    int TopMargin = 12) : Node;

// プレビュー確認用のトグル（Appearance.Button）。1 行に横並びで置く。
public sealed record ToggleDef(
    string Text, string Region, Func<PreviewCanvas, bool> Get, Action<PreviewCanvas, bool> Set);
public sealed record TogglesNode(int TopMargin, params ToggleDef[] Toggles) : Node;

public sealed record TabDef(string Title, Node[] Nodes)
{
    // このタブが選ばれた (true) / 外れた (false) ときに呼ばれる。プレビューの
    // 状態をタブに連動させるのに使う（[レジスタ一覧] のオーバーレイ。鍵盤と
    // ステータスを覆うので、他のタブへ移ったら消す）。
    public Action<PreviewCanvas, bool>? OnSelected { get; init; }
}

public static class TabLayout
{
    // ---- 読みやすさのための短縮 ------------------------------------------
    private static Node Field(string section, string key) => new FieldNode(section, key);
    private static Node Bitmap(BitmapRole role, bool shortLabel = false) => new BitmapNode(role, shortLabel);
    private static Node Colors(string title) => new ColorsNode(title);
    private static Node Slider(string label, int min, int max, int init,
        Action<PreviewCanvas, int> set, string region, int topMargin = 12) =>
        new SliderNode(label, min, max, init, set, region, topMargin);

    private static Node[] Fields(string section, params string[] keys) =>
        keys.Select(k => Field(section, k)).ToArray();

    private static readonly string[] PlayKeyNames =
        { "PREV", "STOP", "PLAY", "FAST", "PAUSE", "NEXT", "CONT", "REPEAT" };

    public static IReadOnlyList<TabDef> Build() => new[]
    {
        new TabDef("画面", new[]
        {
            Bitmap(BitmapRole.Back),
            Field("Screen", "Width"),
            Field("Screen", "Height"),
            Field("Screen", "FilerSide"),
            Field("Screen", "FilerExtent"),
            Colors("背景 (Back)"),
        }),

        // 「フォント」タブ（旧「ミニフォント」。2026-09-14 にユーザーの指示で改名）。
        // 一番上は文字描画に使う TTF フォント (font.ttf)、その下がミニフォント。
        new TabDef("フォント", new[]
        {
            Bitmap(BitmapRole.TtfFont),
            Bitmap(BitmapRole.MiniFont),
            Field("MiniFont", "Width"),
            Field("MiniFont", "Height"),
        }),

        new TabDef("鍵盤", new[]
        {
            // 「位置」はこのページの全アイテムの原点なので一番上
            // （2026-09-04、ユーザー指示）。素材行はその直後。
            Field("Keyboard", "Pos"),
            Bitmap(BitmapRole.Kb0),
            Bitmap(BitmapRole.Kb1),
            Bitmap(BitmapRole.Kb2),
            // 13 個並びのスピンボタン列は分かりにくい、というユーザー指示で
            // 音名ラベル付きの 4 行にする。最後の 1 個は「オクターブ幅」で、
            // 次オクターブの C の位置そのものではない（skin.h の kbXOffset[12]
            // は drawscreen.cpp で oct 倍される倍率）。
            new MultiValueNode("1オクターブの鍵のX", "Keyboard", "XOffset", e => e.kbXOffset,
                new[]
                {
                    new[] { "C", "C#", "D", "D#" },
                    new[] { "E", "F", "F#", "G" },
                    new[] { "G#", "A", "A#", "B" },
                    new[] { "オクターブ幅" },
                },
                PreviewRegions.Ids.KeyboardOctave, Min: 0, Max: 9999),
            Field("Keyboard", "YOffset"),
            new MultiValueNode("各チャンネルのY位置", "Keyboard", "ChannelY", e => e.chYOffset,
                new[]
                {
                    new[] { "FM1", "FM2", "FM3", "FM4" },
                    new[] { "FM5", "FM6", "FM7", "FM8" },
                    new[] { "PCM" },
                },
                PreviewRegions.Ids.KeyboardAll),
            Field("Keyboard", "KeyOffset"),
            // ランダムに選んだ鍵を押した状態にする（ON のたびに選び直す）。
            new TogglesNode(12, new ToggleDef("押す", PreviewRegions.Ids.KeyboardAll,
                p => p.StateKeysPressed, (p, v) => p.StateKeysPressed = v)),
            Colors("鍵盤 (KB)"),
        }),

        new TabDef("ステータス", new Node[]
        {
            Field("Status", "Rect"),
            new SubTabsNode(
                ("レベルメータ", new[]
                {
                    Bitmap(BitmapRole.LevelMeter, shortLabel: true),
                    Field("Status", "PosLevelMeter"),
                    Field("LevelMeter", "PaletteOffset"),
                    Field("LevelMeter", "Cells"),
                    Field("LevelMeter", "SrcX"),
                    // レベルごとの点灯具合を見るため。FM 8ch 全段に同じ値を配る
                    // （レベルメータの項目は段によらず共通なので分けていない）。
                    Slider("レベル", 0, 100, 0, (p, v) => p.StateLevel = v, PreviewRegions.Ids.LevelMeter,
                        topMargin: 8),
                }),
                ("配置", Fields("Status",
                    "PosVolume", "PosPanpot", "PosDetune", "PosVoice", "PosQ", "PosPtr")),
                ("ピッチLFO", Fields("Status",
                    "PosLFOPitch", "PosLFOPitch1", "PosLFOPitch2", "PosLFOPitch3", "PosLFOPitch4")),
                ("音量LFO", Fields("Status",
                    "PosLFOVolume", "PosLFOVolume1", "PosLFOVolume2", "PosLFOVolume3")),
                ("PCM", new Node[]
                {
                    Field("Status", "PosPcmVolume"),
                    Field("Status", "PosPcmPtr"),
                    new PcmChannelsNode(),
                }),
                // 音色データ表示（tonedata.md）。本体はステータス欄の長押しで
                // 切り替えるが、プレビューは「音色」系のサブタブを選ぶと音色データ
                // 表示になり、他のサブタブを選ぶと戻る（下の OnPageSelected）。
                // 他のタブへ移っても表示はそのまま（ユーザーの指示）。項目が多いので
                // 3 ページに分ける（1 ページの高さが他のサブタブを超えないように）。
                ("音色", new Node[]
                {
                    // 並びは OPM のスロット順（M1, M2, C1, C2）。ラベルは MML の
                    // オペレータ番号（tonedata.md のコメントどおり）。
                    // 4 個を 1 行に並べると右端が切れる（実測）ので 2 行に分ける。
                    new MultiValueNode("オペレータの段のY", "Status", "OPMOperatorY", e => e.opmOperatorY,
                        new[]
                        {
                            new[] { "オペレータ1", "オペレータ3" },
                            new[] { "オペレータ2", "オペレータ4" },
                        },
                        PreviewRegions.Ids.Status),
                    Field("Status", "PosOPMAlgorythm"),
                    Field("Status", "PosOPMFeedback"),
                    Field("Status", "PosOPMAttackRate"),
                    Field("Status", "PosOPMDecayRate"),
                    Field("Status", "PosOPMSustainRate"),
                    Field("Status", "PosOPMReleaseRate"),
                }),
                ("音色 2", Fields("Status",
                    "PosOPMSustainLevel", "PosOPMTotalLevel", "PosOPMKeyScaling",
                    "PosOPMMultiple", "PosOPMDetune1", "PosOPMDetune2", "PosOPMAMSEnable")),
                ("音色 (PCM段)", Fields("Status",
                    "PosOPMNoise", "PosOPMClockB", "PosOPMLFOFreq",
                    "PosOPMLFOPMD", "PosOPMLFOAMD", "PosOPMLFOWAVE")))
            {
                // 「音色」で始まるページを選ぶと音色データ表示、それ以外のページで従来の表示。
                OnPageSelected = (p, title) => p.StateToneMode = title.StartsWith("音色"),
            },
            Colors("ステータス (Status)"),
        }),

        // OPM レジスタ一覧（regmap.md）。本体は鍵盤の長押しで出すオーバーレイ。
        // プレビューはこのタブを選んでいる間だけ出す（鍵盤とステータスを覆うので、
        // 他のタブへ移ったら消す。[音色] サブタブの「移っても戻さない」とは逆）。
        new TabDef("レジスタ一覧", new Node[]
        {
            Field("RegMap", "Rect"),
            Field("RegMap", "Pos"),
            Colors("OPM レジスタ一覧 (RegMap)"),
        })
        {
            OnSelected = (p, selected) => p.StateRegMapMode = selected,
        },

        new TabDef("バナー", new[]
        {
            Bitmap(BitmapRole.Banner),
            Field("Banner", "Rect"),
        }),

        new TabDef("操作ボタン", new Node[]
        {
            Bitmap(BitmapRole.PlayKey),
            Field("PlayKey", "Rect"),
            Field("PlayKey", "Count"),
            // ボタンごとのサブタブ。中身は 8 個とも同じ形なので組み立てで作る
            // （ここで作っているのは並びのデータで、描画側に分岐は増えない）。
            new SubTabsNode(
                PlayKeyNames.Select((name, i) => (name, PlayKeyPage(name, i)))
                    .Append(("パレット", Fields("PlayKey",
                        "PalKey", "PalDark", "PalRed", "PalGreen", "PalYellow", "PalBlue")))
                    .ToArray()),
            Colors("操作ボタン (PlayKey)"),
        }),

        new TabDef("音量バー", new[]
        {
            Bitmap(BitmapRole.VolumeBar),
            Field("VolumeBar", "Rect"),
            Field("VolumeBar", "VolumePos"),
            Field("VolumeBar", "SrcThumb"),
            Field("VolumeBar", "SrcBarLeft"),
            Field("VolumeBar", "SrcBarRight"),
            Field("VolumeBar", "SrcBar"),
            Slider("音量", -100, 100, 0, (p, v) => p.StateVolume = v, PreviewRegions.Ids.VolumeBar),
        }),

        new TabDef("プログレスバー", new[]
        {
            Bitmap(BitmapRole.ProgressBar),
            Field("ProgressBar", "Rect"),
            Field("ProgressBar", "ProgressPos"),
            Field("ProgressBar", "TimePos"),
            Field("ProgressBar", "SrcBarLeft"),
            Field("ProgressBar", "SrcBarRight"),
            Field("ProgressBar", "SrcBar"),
            Slider("プレイ時間", 0, 100, 40, (p, v) => p.StateProgress = v / 100.0, PreviewRegions.Ids.ProgressBar),
        }),

        new TabDef("曲名", new[]
        {
            Field("Title", "Rect"),
            Field("Title", "ScrollSpeed"),
            Colors("曲名 (MDXTitle)"),
        }),

        new TabDef("ファイラー", new Node[]
        {
            Field("FileList", "Margin"),
            Field("FileList", "ItemHeight"),
            Field("FileList", "BaseNameX"),
            Field("FileList", "BaseNameWidth"),
            Field("FileList", "TitleX"),
            Field("FileList", "ScrollSpeed"),
            // このタブの「小,大」の 2 つ組は、どちらが効いているか見ないと
            // 分からない。本体の TAB キーと同じ切り替えをプレビューにも置く。
            // 行数も曲名の幅も大小で変わるので、両方の見え方を確かめられる。
            new TogglesNode(12, new ToggleDef("大きい文字", PreviewRegions.Ids.FileList,
                p => p.StateFileListBigFont, (p, v) => p.StateFileListBigFont = v)),
            Colors("ファイラー (Filer)"),
        }),

        new TabDef("スクロールバー", new[]
        {
            Bitmap(BitmapRole.ScrollBar),
            Field("ScrollBar", "Width"),
            Field("ScrollBar", "HitWidth"),
            Field("ScrollBar", "SrcThumb"),
            Field("ScrollBar", "SrcUpArrowPress"),
            Field("ScrollBar", "SrcDownArrowPress"),
            Field("ScrollBar", "SrcUpArrow"),
            Field("ScrollBar", "SrcBar"),
            Field("ScrollBar", "SrcDownArrow"),
            Slider("スクロール", 0, 100, 0, (p, v) => p.StateScroll = v, PreviewRegions.Ids.ScrollBar),
            // 上矢印・下矢印は別のアイテムなので、行ごとではなくトグルごとに
            // プレビューへ結び付ける（片方だけ選んで枠を出せるように）。
            new TogglesNode(8,
                new ToggleDef("上矢印", PreviewRegions.Ids.ScrollUpArrow,
                    p => p.StateScrollUpPressed, (p, v) => p.StateScrollUpPressed = v),
                new ToggleDef("下矢印", PreviewRegions.Ids.ScrollDownArrow,
                    p => p.StateScrollDownPressed, (p, v) => p.StateScrollDownPressed = v)),
        }),
    };

    // 操作ボタン 1 個ぶんのサブタブ。LED のパレットを持つ 4 個
    // （PLAY/PAUSE/CONT/REPEAT）だけ項目とトグルが 1 つずつ増える。
    private static Node[] PlayKeyPage(string name, int index)
    {
        var nodes = new List<Node>
        {
            Field("PlayKey", $"Src{index}"),
            Field("PlayKey", $"Pos{index}"),
        };

        (string Key, Func<PreviewCanvas, bool> Get, Action<PreviewCanvas, bool> Set)? led = name switch
        {
            "PLAY" => ("PalPlayLed", p => p.StateLedPlay, (p, v) => p.StateLedPlay = v),
            "PAUSE" => ("PalPauseLed", p => p.StateLedPause, (p, v) => p.StateLedPause = v),
            "CONT" => ("PalContLed", p => p.StateLedCont, (p, v) => p.StateLedCont = v),
            "REPEAT" => ("PalRepeatLed", p => p.StateLedRepeat, (p, v) => p.StateLedRepeat = v),
            _ => null,
        };
        if (led is { } l) nodes.Add(Field("PlayKey", l.Key));

        string region = PreviewRegions.Ids.Indexed(PreviewRegions.Ids.PlayKeyButton, index);
        var toggles = new List<ToggleDef>
        {
            new("押す", region, p => p.GetButtonPressed(index), (p, v) => p.SetButtonPressed(index, v)),
        };
        if (led is { } l2) toggles.Add(new ToggleDef("LED", region, l2.Get, l2.Set));
        nodes.Add(new TogglesNode(8, toggles.ToArray()));

        return nodes.ToArray();
    }
}
