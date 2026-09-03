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

// プレビューに出す「演奏状態」。PreviewStateBar から書き換えられる。
public sealed class PreviewState
{
    public bool Play = true;
    public bool Cont = true;
    public bool Pause;
    public bool Repeat;
    public int Volume;             // -100..100
    public double Progress = 0.4;  // 0..1
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

    public string? FindFontFile() => _assets.FindFontFile(_doc.Root.AssetsDir);

    // 当たり判定の矩形を作るのに素材の大きさが要る（鍵盤の下地など）。
    public RenderBitmap? FindAsset(string fileName) => _assets.Find(fileName);

    public IReadOnlyList<TextDraw> TextDraws => _textDraws;

    // 1 枚描いて 32bpp のビットマップを返す（使い回すので破棄しないこと）。
    public Bitmap? Render(PreviewState state)
    {
        var skin = _doc.Effective;
        if (skin.screenW <= 0 || skin.screenH <= 0) return null;

        // レイアウト・配色が変わっていなければ背景を使い回す。SkinDocument は
        // 再計算のたびに Effective を作り直すので、同一性で見分けられる。
        DrawScreenPort port;
        if (_port != null && ReferenceEquals(_port.Skin, skin) &&
            ReferenceEquals(_port.Colors, _doc.EffectiveColors))
        {
            port = _port;
            port.Redraw(DummyTitle);
        }
        else
        {
            port = new DrawScreenPort(skin, _doc.EffectiveColors, _assets);
            port.Reload(DummyTitle);
            _port = port;
        }

        uint status = 0;
        if (state.Play) status |= DrawScreenPort.PlayKeyPlayLed;
        if (state.Pause) status |= DrawScreenPort.PlayKeyPauseLed;
        if (state.Cont) status |= DrawScreenPort.PlayKeyContLed;
        if (state.Repeat) status |= DrawScreenPort.PlayKeyRepeatLed;
        port.PutPlayKey(status);

        port.PutProgressBar((uint)(DummyPlayTimeMs * Math.Clamp(state.Progress, 0, 1)),
            DummyPlayTimeMs);
        port.PutTotalVolBar(state.Volume);

        // ファイラーは先頭から。スクロールバーのつまみが中ほどに来るよう、
        // 行数より多めのダミーを流している。
        int rows = Math.Max(1, port.FileListRows);
        int itemH = Math.Max(1, port.FileListItemH);
        port.PutFileList(DummyFiles, 0, DummyCursor, 0);
        int maxTopPx = Math.Max(0, (DummyFiles.Length - rows) * itemH);
        port.PutScrollBar(0, maxTopPx);

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
