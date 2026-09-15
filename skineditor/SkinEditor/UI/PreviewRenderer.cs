// mxv2 スキンエディタ - プレビュー 1 枚を組み立てる。
//
// mxv2 本体の main.cpp が毎フレーム呼んでいる順番をなぞる:
//   DrawScreen::Reload()（背景 + ステータス全部 0 + 曲名）
//   -> PutPlayKey / PutProgressBar / PutTotalVolBar（visualizer.cpp UpdateChrome）
//   -> PutFileList / PutScrollBar
// 演奏していない状態の絵になるので、鍵盤は押さず、ステータスは 0 のまま。

using System.Drawing;
using SkinEditor.Model;
using SkinEditor.Model.Render;

namespace SkinEditor.UI;

// プレビューに出す「演奏状態」。プレビューの下にあった状態バーは廃止し、
// 各項目は対応するタブへ移した（2026-09-04、ユーザー指示）: 操作ボタンの
// 各サブタブの「押す」「LED」トグル、[プログレスバー]の「プレイ時間」、
// [音量バー]の「音量」、[スクロールバー]の「スクロール」「上矢印」「下矢印」、
// [ステータス]の[レベルメータ]サブタブの「レベル」、[鍵盤]の「押す」。
public sealed class PreviewState
{
    // 押されているボタン。並びは LayoutFieldSchema の playKey サブタブと同じ
    // （0=PREV 1=STOP 2=PLAY 3=FAST 4=PAUSE 5=NEXT 6=CONT 7=REPEAT。
    // SHUFFLE は未実装なので枠だけ確保して常に false）。
    public readonly bool[] ButtonPressed = new bool[9];
    public bool LedPlay = true;
    public bool LedPause;
    public bool LedCont = true;
    public bool LedRepeat;
    public int Scroll;              // 0..100（ファイラーの一覧の何%スクロールしたか）
    public bool ScrollUpPressed;
    public bool ScrollDownPressed;
    public int Level;               // 0..100（レベルメータを左から何%点灯させるか）
    // ファイラーの文字の大小（本体の TAB キー＝DrawScreen::SetFileListFontSize）。
    // false = 小さい文字 (0) / true = 大きい文字 (1)。[FileList] の「小,大」の
    // 2 つ組はどちらが効いているか見ないと分からないので、切り替えられるように
    // してある。
    public bool FileListBigFont;
    // ステータス欄の表示モード（[ステータス] の [音色] サブタブの
    // 「音色データを表示」トグル。本体はステータス欄の長押しで切り替える）。
    public bool ToneMode;
    // OPM レジスタ一覧のオーバーレイ（[レジスタ一覧] タブを選んでいる間だけ true。
    // 本体は鍵盤の長押しで切り替える。regmap.md）。
    public bool RegMapMode;
    public int Volume;             // -100..100
    public double Progress = 0.4;  // 0..1

    // [鍵盤]タブの「押す」トグル。ON にするたびに PreviewCanvas 側で
    // 乱数を選び直し、ここへ書き込む（2026-09-04、ユーザー指示）。
    // FM1-8ch: 主鍵＋そこから ±1-5 鍵離れたベンド鍵（同色、bendMode=1）。
    // PCM ch : 独立に選んだ鍵 8 個（bendMode=0）。
    public bool KeysPressed;
    public readonly int[] FmKey = new int[8];
    public readonly int[] FmColor = new int[8];
    public readonly int[] FmBendKey = new int[8];
    public readonly int[] PcmKey = new int[8];
    public readonly int[] PcmColor = new int[8];
}

public sealed class PreviewRenderer : IDisposable
{
    // ファイラーのダミー行。配色（フォルダ・ドライブ・ファイルシステム・
    // カーソル）の効きが見えるよう、種別を一通り並べてある。
    private static readonly PreviewFileItem[] DummyFiles =
    {
        new("..", "", DrawScreenPort.FileItemDir),
        new("[FS]", "", DrawScreenPort.FileItemFileSystem),
        new("C:", "", DrawScreenPort.FileItemDrive),
        new("FOLDER1", "", DrawScreenPort.FileItemDir),
        new("SAMPLE01.MDX", "Sample Song A", DrawScreenPort.FileItemMdx),
        new("SAMPLE02.MDX", "Sample Song B", DrawScreenPort.FileItemMdx),
        new("SAMPLE03.MDX", "サンプル曲 3", DrawScreenPort.FileItemMdx),
        new("SAMPLE04.MDX", "Sample Song D", DrawScreenPort.FileItemMdx),
        new("SAMPLE05.MDX", "Sample Song E", DrawScreenPort.FileItemMdx),
        new("SAMPLE06.MDX", "Sample Song F", DrawScreenPort.FileItemMdx),
        new("SAMPLE07.MDX", "Sample Song G", DrawScreenPort.FileItemMdx),
        new("SAMPLE08.MDX", "Sample Song H", DrawScreenPort.FileItemMdx),
        new("SAMPLE09.MDX", "Sample Song I", DrawScreenPort.FileItemMdx),
        new("SAMPLE10.MDX", "Sample Song J", DrawScreenPort.FileItemMdx),
        new("SAMPLE11.MDX", "Sample Song K", DrawScreenPort.FileItemMdx),
        new("SAMPLE12.MDX", "Sample Song L", DrawScreenPort.FileItemMdx),
        new("SAMPLE13.MDX", "Sample Song M", DrawScreenPort.FileItemMdx),
        new("SAMPLE14.MDX", "Sample Song N", DrawScreenPort.FileItemMdx),
        new("SAMPLE15.MDX", "Sample Song O", DrawScreenPort.FileItemMdx),
        new("SAMPLE16.MDX", "Sample Song P", DrawScreenPort.FileItemMdx),
    };

    private const int DummyCursor = 4;   // カーソルの行（MDX の行に置く）
    private const string DummyTitle = "サンプル曲 3 - スキンエディタ";
    // 曲の長さと再生位置（プログレスバーの見本）。
    private const uint DummyPlayTimeMs = 3 * 60 * 1000;

    private readonly SkinDocument _doc;
    private SkinAssetSource _assets;
    private Bitmap? _canvas;
    private List<TextDraw> _textDraws = new();
    private DrawScreenPort? _port;

    public PreviewRenderer(SkinDocument doc)
    {
        _doc = doc;
        _assets = new SkinAssetSource(doc);
    }

    // 素材ファイルが差し替わったときに呼ぶ。
    public void InvalidateAssets()
    {
        _assets = new SkinAssetSource(_doc);
        _port = null;
    }

    public string? FindFontFile() => _assets.FindFontFile(_doc.Root.FontRootDirs());

    // 当たり判定の矩形を作るのに素材の大きさが要る（鍵盤の下地など）。
    public RenderBitmap? FindAsset(string fileName) => _assets.Find(fileName);

    public IReadOnlyList<TextDraw> TextDraws => _textDraws;

    // 1 枚描いて 32bpp のビットマップを返す（使い回すので破棄しないこと）。
    public Bitmap? Render(PreviewState state)
    {
        var skin = _doc.Placed;
        if (skin.screenW <= 0 || skin.screenH <= 0) return null;

        // レイアウト・配色が変わっていなければ背景を使い回す。SkinDocument は
        // 再計算のたびに Effective を作り直すので、同一性で見分けられる。
        DrawScreenPort port;
        if (_port != null && ReferenceEquals(_port.Skin, skin) &&
            ReferenceEquals(_port.Colors, _doc.EffectiveColors))
        {
            port = _port;
            port.ToneMode = state.ToneMode;
            port.Redraw(DummyTitle);
        }
        else
        {
            port = new DrawScreenPort(skin, _doc.EffectiveColors, _assets);
            port.ToneMode = state.ToneMode;
            port.Reload(DummyTitle);
            _port = port;
        }

        uint status = 0;
        for (int i = 0; i < state.ButtonPressed.Length && i < 9; i++)
        {
            if (state.ButtonPressed[i]) status |= 1u << i;
        }
        if (state.LedPlay) status |= DrawScreenPort.PlayKeyPlayLed;
        if (state.LedPause) status |= DrawScreenPort.PlayKeyPauseLed;
        if (state.LedCont) status |= DrawScreenPort.PlayKeyContLed;
        if (state.LedRepeat) status |= DrawScreenPort.PlayKeyRepeatLed;
        port.PutPlayKey(status);

        // 鍵盤は CompositeBack が下地を敷くだけで、既定では何も押されていない。
        // [鍵盤] タブの「押す」トグルが ON のときだけ、PreviewCanvas が選んだ
        // 乱数の鍵を重ねる（2026-09-04、ユーザー指示）。値そのものの決め方は
        // PreviewCanvas.RandomizePressedKeys を見ること。
        if (state.KeysPressed)
        {
            for (int row = 0; row < 8; row++)
            {
                port.PutNoteOn(state.FmKey[row], row, state.FmColor[row], false);
                port.PutNoteOn(state.FmBendKey[row], row, state.FmColor[row], true);
            }
            for (int i = 0; i < 8; i++)
            {
                port.PutNoteOn(state.PcmKey[i], 8, state.PcmColor[i], false);
            }
        }

        port.PutProgressBar((uint)(DummyPlayTimeMs * Math.Clamp(state.Progress, 0, 1)),
            DummyPlayTimeMs);
        port.PutTotalVolBar(state.Volume);

        // レベルメータは Redraw が呼ぶ PutStatusZero で毎回いったん全消灯に
        // なるので、[ステータス]の[レベルメータ]サブタブの「レベル」
        // スライダー (0..100%) で上書きする（2026-09-04、ユーザー指示）。
        // 左から何セル点灯させるかは PutLevelMeter の levelMeterInfo[i] の
        // 並びと同じ（i が小さいほど左＝先に点く）。FM 8ch 全段に同じ値を
        // 適用する（levelMeterWidthCells 等の項目はどの段にも共通のため）。
        // ついでにステータスの音量値（V000〜V127）も同じ「レベル」に連動させる
        // （2026-09-04、ユーザー指示。0..100% を 0..127 へ比例配分）。
        {
            int cells = Math.Max(1, skin.levelMeterWidthCells);
            int level = Math.Clamp(state.Level, 0, 100);
            int lit = (int)Math.Round(cells * level / 100.0);
            var meter = new byte[cells];
            for (int i = 0; i < lit; i++) meter[i] = 1;
            int volume = (int)Math.Round(127 * level / 100.0);
            for (int row = 0; row < 8; row++)
            {
                port.PutLevelMeter(meter, row);
                port.PutVolume(volume, row);
            }
        }

        // ファイラーは行数より多めのダミーを流してあるので、スクロールバーで
        // 動かせる余地がある。位置は [スクロールバー] タブの「スクロール」
        // スライダー (0..100%) で決める（2026-09-04、ユーザー指示）。
        // 行数も 1 行の高さも文字の大小で変わるので、**数える前に**決める。
        port.FileListFontSize = state.FileListBigFont ? 1 : 0;
        int rows = Math.Max(1, port.FileListRows);
        int itemH = Math.Max(1, port.FileListItemH);
        int maxTopPx = Math.Max(0, (DummyFiles.Length - rows) * itemH);
        int topPx = maxTopPx <= 0 ? 0 : (int)((long)maxTopPx * Math.Clamp(state.Scroll, 0, 100) / 100);
        // top は行単位（PutFileList が丸ごと描く行の先頭）、offset はその中の
        // 端数ピクセル。本体 Filer::topPx() / top() と同じ切り分け方。
        int top = topPx / itemH;
        int offset = topPx % itemH;
        port.PutFileList(DummyFiles, top, DummyCursor, offset);
        // 上下矢印の押下表示も [スクロールバー] タブのトグルボタンから。
        port.ScrollBarFlags =
            (state.ScrollUpPressed ? DrawScreenPort.ScrollBarUpArrowDown : 0) |
            (state.ScrollDownPressed ? DrawScreenPort.ScrollBarDownArrowDown : 0);
        port.PutScrollBar(topPx, maxTopPx);

        // OPM レジスタ一覧は一番上に乗る（本体は BlitTo のときに乗せる）。
        if (state.RegMapMode) port.OverlayRegMap();

        _textDraws = port.TextDraws;

        var fresh = BmpLoader.ToDrawingBitmap(port.Screen);
        _canvas?.Dispose();
        _canvas = fresh;
        return _canvas;
    }

    public void Dispose()
    {
        _canvas?.Dispose();
        _canvas = null;
    }
}
