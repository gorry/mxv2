// mxv2 スキンエディタ - 描画コア（mxv2 本体 src/drawscreen.cpp の移植）。
//
// 本体と同じ手順で 24bpp のキャンバスを組み立てる。関数名・順番・定数は
// 本体に合わせてあるので、本体を直したらここも直すこと。
//
// 本体から**持ってこないもの**:
//   - 差分更新 (*Last_ / refresh)。プレビューは毎回全部描く
//   - HitCheck*。プレビューの選択・ドラッグは layout の値から作った矩形を使う
//   - Screen / SDL への転送。BlitTo 相当は BmpLoader.ToDrawingBitmap
//   - TextLayer の実装。文字は TextDraws に積んで、表示側が出力解像度で描く
//     （本体が文字だけ別レイヤーにしているのと同じ形。textlayer.h 参照）
//
// 本体との意図的な差:
//   - 素材が 1 枚でも読めないと本体は起動を止めるが、編集中は一時的に
//     欠けうるので、こちらは**その部品を描かずに続行**する
//   - 操作ボタンの LED は毎回 status から塗り直す（本体は差分判定に
//     引っかかって、スキンを切り替えた直後だけ点灯色が反映されない）

namespace SkinEditor.Model.Render;

// 素材の解決。本体の Skin::FindFile 相当（自スキン -> 土台 の順で探す）。
public interface IAssetSource
{
    RenderBitmap? Find(string fileName);
}

// ファイラーの 1 行。本体 src/filer.h の FileItem のうち、描画に要るものだけ。
public readonly record struct PreviewFileItem(string BaseName, string Title, int Type);

// 文字の描画依頼。本体は TextLayer が出力解像度で描くので、キャンバスには
// 焼かずにここへ積んで、表示側に渡す。
public readonly record struct TextDraw(
    int X, int Y, int W, int H, string Text, RgbColor Color, int Bright, int ClipY, int ClipH);

public sealed class DrawScreenPort
{
    // ---- 本体 drawscreen.cpp の無名 namespace の定数 ----------------------
    private const int MiniFontCols = 16;
    private const int MiniFontRows = 5;

    // 文字コード (0x00〜0x7F) → グリフ表の番号 (0x00〜0x4F)。本体 kMiniGlyphIndex の写し。
    // 0x00〜0x3F は ASCII 0x20〜0x5F の順、0x40〜0x49 は制御コード 0x10〜0x19 の記号、
    // 0x4A〜0x4F が ` ~ DEL { | }。小文字は大文字と同じグリフ。0x80 以上は 0 番（空白）。
    private static readonly byte[] MiniGlyphIndex =
    {
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47, 0x48,0x49,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07, 0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17, 0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
        0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27, 0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
        0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37, 0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
        0x4a,0x21,0x22,0x23,0x24,0x25,0x26,0x27, 0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
        0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37, 0x38,0x39,0x3a,0x4d,0x4e,0x4f,0x4b,0x4c,
    };
    private const int MaxAsciiChars = 128;

    // ファイラーの行の種別。本体 src/filer.h の FileItemType。
    public const int FileItemDir = 1;
    public const int FileItemDrive = 2;
    public const int FileItemMdx = 4;
    public const int FileItemFileSystem = 8;

    // 操作ボタンの状態ビット。本体 DrawScreen::PlayKeyStatus と同じ。
    public const uint PlayKeyPrev = 1 << 0;
    public const uint PlayKeyStop = 1 << 1;
    public const uint PlayKeyPlay = 1 << 2;
    public const uint PlayKeyFastPlay = 1 << 3;
    public const uint PlayKeyPause = 1 << 4;
    public const uint PlayKeyNext = 1 << 5;
    public const uint PlayKeyCont = 1 << 6;
    public const uint PlayKeyRepeat = 1 << 7;
    public const uint PlayKeyPlayLed = 1 << 16;
    public const uint PlayKeyPauseLed = 1 << 17;
    public const uint PlayKeyContLed = 1 << 18;
    public const uint PlayKeyRepeatLed = 1 << 19;

    private readonly SkinLayout _skin;
    private readonly ColorsValues _colors;

    private readonly RenderBitmap _screen = new();
    private readonly RenderBitmap _back = new();
    private readonly RenderBitmap _backBitmap = new();

    private RenderBitmap? _kb0;
    private readonly RenderBitmap?[] _keyboard = new RenderBitmap?[12];
    private RenderBitmap? _miniFont;
    private RenderBitmap? _levelMeter;
    private RenderBitmap? _banner;
    private RenderBitmap? _playKey;
    private RenderBitmap? _progressBarBase;
    private RenderBitmap? _progressBar;
    private RenderBitmap? _totalVolBarBase;
    private RenderBitmap? _totalVolBar;
    private RenderBitmap? _scrollBarBase;
    private RenderBitmap? _scrollBar;
    private RenderBitmap? _backSource;

    private readonly RgbColor[] _kbPalette = new RgbColor[12];
    private readonly RgbColor[] _palLevelMeter = new RgbColor[128];

    private int _miniGlyphW, _miniGlyphH;
    private int _fileListFontSize;

    // 使い回してよいかを呼び出し側が見分けるため（PreviewRenderer）。
    public SkinLayout Skin => _skin;
    public ColorsValues Colors => _colors;

    public int Width => _skin.screenW;
    public int Height => _skin.screenH;
    public RenderBitmap Screen => _screen;
    public List<TextDraw> TextDraws { get; } = new();

    // スクロールバーの見た目の状態（本体 DrawScreen::ScrollBarFlag）。
    // 本体の kScrollBarDrag（つまみドラッグ中は自動再配置しない）はプレビューに
    // ドラッグの概念が無いので移植していない。矢印の押下表示の2つだけ
    // [スクロールバー] タブの「上矢印」「下矢印」トグルボタンから使う
    // （2026-09-04、ユーザー指示）。
    public const int ScrollBarUpArrowDown = 1 << 1;
    public const int ScrollBarDownArrowDown = 1 << 2;
    public int ScrollBarFlags { get; set; }
    public int ScrollBarThumb { get; private set; }

    public int FileListFontSize
    {
        get => _fileListFontSize;
        set => _fileListFontSize = value & 1;
    }

    // ステータス欄の表示モード（本体 DrawScreen::StatusMode。tonedata.md）。
    // false = チャンネルステータス / true = 音色データ。チャンネルステータスの
    // Put* と音色データの PutOPM* は、それぞれ自分のモードのときだけ描く。
    public bool ToneMode { get; set; }

    public int FileListRows => _skin.fileListRows[_fileListFontSize & 1];
    public int FileListItemH => _skin.fileListItemH[_fileListFontSize & 1];
    public int TotalVolBarMovement => _skin.VolBarMovement();
    public int ScrollBarMovement => _skin.ScrollBarMovement();

    public DrawScreenPort(SkinLayout skin, ColorsValues colors, IAssetSource assets)
    {
        _skin = skin;
        _colors = colors;

        _screen.Create(Width, Height, 24);
        _back.Create(Width, Height, 24);
        _backBitmap.Create(Width, Height, 24);

        LoadAssets(assets);
    }

    // ---- 素材の読み込み（本体 LoadAssets） --------------------------------
    private void LoadAssets(IAssetSource assets)
    {
        _kb0 = assets.Find(_skin.kb0Bitmap);
        var kb1 = assets.Find(_skin.kb1Bitmap);
        var kb2 = assets.Find(_skin.kb2Bitmap);
        _miniFont = assets.Find(_skin.miniFontBitmap);
        _levelMeter = assets.Find(_skin.levelMeterBitmap);
        _banner = assets.Find(_skin.bannerBitmap);
        _playKey = assets.Find(_skin.playKeyBitmap);
        _progressBarBase = assets.Find(_skin.progressBarBitmap);
        _totalVolBarBase = assets.Find(_skin.volBarBitmap);
        _scrollBarBase = assets.Find(_skin.scrollBarBitmap);
        _backSource = assets.Find(_skin.backBitmap);

        if (_miniFont is { Valid: true })
        {
            // 1 文字の大きさは素材から決まる（スキンが持つのは送り幅と行の高さだけ）。
            _miniGlyphW = _miniFont.Width / MiniFontCols;
            _miniGlyphH = _miniFont.Height / MiniFontRows;
        }

        // 鍵ビットマップの切り出し。奇数番の音は kb2 (黒鍵) から取る。
        // パレット 0x11 を 1 (影)、0x12+n を 2 (点灯色) へ寄せる。
        int[] useKb2 = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
        for (int i = 0; i < 12; i++)
        {
            var src = useKb2[i] != 0 ? kb2 : kb1;
            if (src is { Valid: true } && i < _skin.kbXOffset.Length)
                _keyboard[i] = CutKeyboardBitmap(src, _skin.kbXOffset[i], 7, 0x11, 0x12 + i);
        }
        if (kb1 is { Valid: true })
        {
            for (int i = 0; i < 12; i++) _kbPalette[i] = kb1.Palette[i + 0x12];
        }

        // レベルメータの点灯色 / 消灯色
        if (_levelMeter is { Valid: true })
        {
            int n = Math.Min(_skin.levelMeterWidthCells * 2, _palLevelMeter.Length);
            for (int i = 0; i < n; i++)
            {
                int idx = i + _skin.levelMeterPalOfs;
                if (idx >= 0 && idx < 256) _palLevelMeter[i] = _levelMeter.Palette[idx];
            }
        }

        // 組み立て用のバッファ（8bpp。パレットは素材から引き継ぐ）
        _progressBar = CreateScratch(_skin.progW, _skin.progH, _progressBarBase);
        _totalVolBar = CreateScratch(_skin.volW, _skin.volH, _totalVolBarBase);
        _scrollBar = CreateScratch(_skin.scrollW, _skin.scrollH, _scrollBarBase);

        // 操作ボタンの LED は消灯状態から始める
        if (_playKey is { Valid: true })
        {
            _playKey.SetPalette(_skin.palPlayLed, 25, 25, 25);
            _playKey.SetPalette(_skin.palPauseLed, 25, 25, 25);
            _playKey.SetPalette(_skin.palContLed, 25, 25, 25);
            _playKey.SetPalette(_skin.palRepeatLed, 25, 25, 25);
        }
    }

    private static RenderBitmap? CreateScratch(int w, int h, RenderBitmap? paletteFrom)
    {
        if (w <= 0 || h <= 0 || paletteFrom is not { Valid: true }) return null;
        var b = new RenderBitmap();
        if (!b.Create(w, h, 8)) return null;
        Array.Copy(paletteFrom.Palette, b.Palette, 256);
        return b;
    }

    private static RenderBitmap CutKeyboardBitmap(RenderBitmap src, int xsrc, int cutWidth,
        int pal1, int pal2)
    {
        var outBmp = new RenderBitmap();
        outBmp.Create(cutWidth, src.Height, 8);
        for (int y = 0; y < src.Height; y++)
        {
            int p = src.RowFromTop(y) + xsrc;
            int q = outBmp.RowFromTop(y);
            for (int x = 0; x < cutWidth; x++)
            {
                int sx = xsrc + x;
                int c = (sx >= 0 && sx < src.Width) ? src.Bits[p + x] : 0;
                outBmp.Bits[q + x] = (byte)(c == pal1 ? 1 : c == pal2 ? 2 : 0);
            }
        }
        return outBmp;
    }

    // ---- 背景合成（本体 LoadBackBitmap / Composite*） ----------------------
    private void LoadBackBitmap()
    {
        Blitter.Fill(_backBitmap, 0, 0, Width, Height, 0, 0, 0, 100);
        // 背景を貼る原点は「ファイラー以外側」の外の角（本体 LoadBackBitmap）。
        // エディタは常に宣言サイズで描くので、下・右配置なら左上のまま。
        if (_colors.back.bitmap == 0) return;
        if (_backSource is not { Valid: true }) return;
        {
            int dx = _skin.filerSide == (int)FilerSide.Left ? Width - _backSource.Width : 0;
            int dy = _skin.filerSide == (int)FilerSide.Top ? Height - _backSource.Height : 0;
            Blitter.Copy(_backBitmap, dx, dy, _backSource.Width, _backSource.Height,
                _backSource, 0, 0, 100);
        }
    }

    private void CompositeBanner()
    {
        if (_banner is not { Valid: true }) return;
        Blitter.Copy(_back, _skin.bannerX, _skin.bannerY, _skin.bannerW, _skin.bannerH,
            _banner, 0, 0, Blend.Mul);
    }

    // ステータス欄 1 段ぶんの矩形（0..7 = FM ch.1-8 / 8 = PCM）。
    public bool StatusRect(int row, out int x, out int y, out int w, out int h)
    {
        x = y = w = h = 0;
        if (row < 0 || row >= 9) return false;
        x = _skin.statusX;
        y = _skin.statusY + _skin.chYOffset[row];
        w = _skin.statusW;
        h = _skin.statusH;
        return true;
    }

    private void CompositeStatusBack()
    {
        for (int i = 0; i < 9; i++)
        {
            if (!StatusRect(i, out int x, out int y, out int w, out int h)) continue;
            Blitter.Fill(_back, x, y, w, h, _colors.status.backColor.R, _colors.status.backColor.G,
                _colors.status.backColor.B, _colors.status.backColorBright);
        }
    }

    private void CompositeFileList()
    {
        Blitter.Fill(_back, _skin.fileListX, _skin.fileListY, _skin.fileListW, _skin.fileListH,
            _colors.filer.backColor.R, _colors.filer.backColor.G, _colors.filer.backColor.B,
            _colors.filer.backColorBright);
    }

    private void CompositeKeyboard()
    {
        if (_kb0 is not { Valid: true }) return;
        _kb0.SetPalette(1, _colors.kb.blackBright, _colors.kb.blackBright, _colors.kb.blackBright);
        _kb0.SetPalette(2, _colors.kb.whiteBright, _colors.kb.whiteBright, _colors.kb.whiteBright);
        for (int i = 0; i < 9; i++)
        {
            Blitter.Copy(_back, _skin.kbX, _skin.kbY + _skin.chYOffset[i] + _skin.kbYOffset,
                _kb0.Width, _kb0.Height, _kb0, 0, 0, Blend.Mul);
        }
    }

    private void CompositeBack()
    {
        Blitter.Fill(_back, 0, 0, Width, Height, 0, 0, 0, 100);
        Blitter.Copy(_back, 0, 0, Width, Height, _backBitmap, 0, 0, _colors.back.bitmapBright);
        Blitter.Fill(_back, 0, 0, Width, Height, _colors.back.color.R, _colors.back.color.G,
            _colors.back.color.B, _colors.back.colorBright);

        CompositeBanner();
        CompositeStatusBack();
        CompositeFileList();
        CompositeKeyboard();
    }

    // 本体 Reload。背景を作り直して全面を描き直す。
    public void Reload(string mdxTitle)
    {
        LoadBackBitmap();
        CompositeBack();
        Redraw(mdxTitle);
    }

    // 背景 (back_) はそのまま使い、上物だけ描き直す。
    // 本体には無い切り分けで、プレビューが「演奏状態だけ変わった」ときに
    // 背景の合成（画面 5 回ぶんのなめ）を省くために足した。背景は
    // レイアウトと配色だけで決まるので、それらが変わらない限り使い回せる。
    public void Redraw(string mdxTitle)
    {
        TextDraws.Clear();
        Blitter.Copy(_screen, 0, 0, Width, Height, _back, 0, 0, 100);
        PutStatusZero();
        PutMDXTitle(mdxTitle);
    }

    // ---- 文字描画 (ミニフォント) ------------------------------------------
    private void MiniGlyphSrc(int ch, out int sx, out int sy)
    {
        int g = (ch >= 0 && ch < 0x80) ? MiniGlyphIndex[ch] : 0;
        sx = g % MiniFontCols * _miniGlyphW;
        sy = g / MiniFontCols * _miniGlyphH;
    }

    private void PrintMini(int x, int y, string msg, RgbColor color, int alpha)
    {
        if (_miniFont is not { Valid: true }) return;
        _miniFont.SetPalette(1, color.R, color.G, color.B);
        foreach (char ch in msg)
        {
            MiniGlyphSrc(ch, out int sx, out int sy);
            Blitter.CopyTransparent(_screen, x, y, _miniGlyphW, _miniGlyphH, _miniFont, sx, sy, alpha);
            x += _skin.miniFontW;
        }
    }

    private void PrintMiniCompose(int x, int y, string msg, RgbColor color, int alpha)
    {
        if (_miniFont is not { Valid: true }) return;
        _miniFont.SetPalette(1, color.R, color.G, color.B);
        foreach (char ch in msg)
        {
            MiniGlyphSrc(ch, out int sx, out int sy);
            Blitter.CopyComposite(_screen, x, y, _miniGlyphW, _miniGlyphH, _miniFont, sx, sy,
                _back, x, y, alpha);
            x += _skin.miniFontW;
        }
    }

    // ---- ステータス欄 ------------------------------------------------------
    // 本体 StatusItemPos。FM は段の y をそのまま、PCM 2 項目だけ PCM 行の
    // 中の 8 スロットから選ぶ。
    public void StatusItemPos(StatusItem item, int row, out int x, out int y)
    {
        var pos = _skin.statusPos[(int)item];
        if (item is StatusItem.PcmVolume or StatusItem.PcmPtr)
        {
            int i = row - 8;
            x = _skin.statusX + _skin.pcmXOffset[i] + pos[0];
            y = _skin.statusY + _skin.chYOffset[8] + _skin.pcmYOffset[i] + pos[1];
            return;
        }
        x = _skin.statusX + pos[0];
        y = _skin.statusY + _skin.chYOffset[row] + pos[1];
    }

    private void PutStatusText(StatusItem item, int row, string text)
    {
        if (ToneMode) return;
        StatusItemPos(item, row, out int x, out int y);
        PrintMiniCompose(x, y, text, _colors.status.color, _colors.status.colorBright);
    }

    // ---- 音色データ表示（本体 DrawScreen::PutToneText / PutOPM*） ----------
    // row は FM の段 (0..7) か PCM の段 (8)、slot はオペレータごとの項目の
    // OPM スロット（それ以外は 0）。
    private void PutToneText(StatusItem item, int row, int slot, string text)
    {
        if (!ToneMode) return;
        if (row < 0 || row >= 9) return;
        var pos = _skin.statusPos[(int)item];
        int x = _skin.statusX + pos[0];
        int y = _skin.statusY + _skin.chYOffset[row] + pos[1];
        if (StatusItems.IsOpmOperatorItem(item)) y += _skin.opmOperatorY[slot & 3];
        PrintMiniCompose(x, y, text, _colors.status.color, _colors.status.colorBright);
    }

    public void PutOPMChannel(int reg20, int row)
    {
        if (row < 0 || row >= 8) return;
        PutToneText(StatusItem.OPMAlgorithm, row, 0, $"A{reg20 & 7:X}");
        PutToneText(StatusItem.OPMFeedback, row, 0, $"F{(reg20 >> 3) & 7:X}");
    }

    // 本体 DrawScreen::PutOPMOperator と同じ並び。値は 1 スロットぶんの
    // 6 レジスタ（DT1/MUL, KS/AR, AME/D1R, DT2/D2R, D1L/RR, TL）のバイト。
    public void PutOPMOperator(int row, int slot, int dt1mul, int ksar, int amed1r, int dt2d2r, int d1lrr, int tl)
    {
        if (row < 0 || row >= 8) return;
        PutToneText(StatusItem.OPMMultiple, row, slot, $"{dt1mul & 0x0f:X}");
        PutToneText(StatusItem.OPMDetune1, row, slot, $"{(dt1mul >> 4) & 0x07:X}");
        PutToneText(StatusItem.OPMAttackRate, row, slot, $"{ksar & 0x1f:X2}");
        PutToneText(StatusItem.OPMKeyScaling, row, slot, $"{(ksar >> 6) & 0x03:X}");
        PutToneText(StatusItem.OPMDecayRate, row, slot, $"{amed1r & 0x1f:X2}");
        PutToneText(StatusItem.OPMAMSEnable, row, slot, $"{(amed1r >> 7) & 0x01:X}");
        PutToneText(StatusItem.OPMSustainRate, row, slot, $"{dt2d2r & 0x1f:X2}");
        PutToneText(StatusItem.OPMDetune2, row, slot, $"{(dt2d2r >> 6) & 0x03:X}");
        PutToneText(StatusItem.OPMReleaseRate, row, slot, $"{d1lrr & 0x0f:X}");
        PutToneText(StatusItem.OPMSustainLevel, row, slot, $"{(d1lrr >> 4) & 0x0f:X}");
        PutToneText(StatusItem.OPMTotalLevel, row, slot, $"{tl & 0x7f:X2}");
    }

    // OPM 全体の値（PCM の段）。value が null なら "--"。
    public void PutOPMGlobal(StatusItem item, int? value)
    {
        string label = item switch
        {
            StatusItem.OPMNoise => "NOISE:",
            StatusItem.OPMClockB => "CLKB :",
            StatusItem.OPMLFOFreq => "LFRQ :",
            StatusItem.OPMLFOPMD => "PMD  :",
            StatusItem.OPMLFOAMD => "AMD  :",
            StatusItem.OPMLFOWave => "WAVE :",
            _ => "",
        };
        if (label.Length == 0) return;
        string text = item == StatusItem.OPMLFOWave
            ? $"{label}{(value ?? 0) & 3:X}"
            : value == null ? $"{label}--" : $"{label}{value.Value & 0xff:X2}";
        PutToneText(item, 8, 0, text);
    }

    // 音色データ表示のダミー（プレビュー用。本体には無い）。全段に同じ
    // 「それらしい」音色を出して、位置の調整ができるようにする。
    public void PutOPMDummy()
    {
        // アルゴリズム 4 / フィードバック 7、4 スロットの値は適当な典型値。
        var dt1mul = new[] { 0x31, 0x02, 0x11, 0x01 };
        var ksar = new[] { 0x5f, 0x1f, 0x5c, 0x1a };
        var amed1r = new[] { 0x0a, 0x85, 0x08, 0x83 };
        var dt2d2r = new[] { 0x40, 0x03, 0x02, 0x44 };
        var d1lrr = new[] { 0x27, 0xf8, 0x36, 0xf9 };
        var tl = new[] { 0x1e, 0x00, 0x28, 0x02 };
        for (int row = 0; row < 8; row++)
        {
            PutOPMChannel(0x3c, row);
            for (int s = 0; s < 4; s++)
                PutOPMOperator(row, s, dt1mul[s], ksar[s], amed1r[s], dt2d2r[s], d1lrr[s], tl[s]);
        }
        PutOPMGlobal(StatusItem.OPMNoise, null);
        PutOPMGlobal(StatusItem.OPMClockB, 0xc8);
        PutOPMGlobal(StatusItem.OPMLFOFreq, 0x2a);
        PutOPMGlobal(StatusItem.OPMLFOPMD, 0x10);
        PutOPMGlobal(StatusItem.OPMLFOAMD, null);
        PutOPMGlobal(StatusItem.OPMLFOWave, 2);
    }

    public void PutVolume(int volume, int row)
    {
        if (row < 0 || row >= 9) return;
        string s = volume >= 128
            ? $"V{127 - (volume & 127):D3}"
            : "V" + volume.ToString().PadRight(3);
        PutStatusText(StatusItem.Volume, row, s);
    }

    public void PutPanpot(int panpot, int row)
    {
        if (row < 0 || row >= 9) return;
        PutStatusText(StatusItem.Panpot, row, "-LRC"[panpot & 3].ToString());
    }

    public void PutDetune(int detune, int row)
    {
        if (row < 0 || row >= 9) return;
        int v = (short)detune;
        PutStatusText(StatusItem.Detune, row, $"D{(v >= 0 ? '+' : '-')}{Math.Abs(v) % 10000:D4}");
    }

    public void PutVoice(int voice, int row)
    {
        if (row < 0 || row >= 9) return;
        PutStatusText(StatusItem.Voice, row, $"@{voice % 1000:D3}");
    }

    public void PutQ(int q, int row)
    {
        if (row < 0 || row >= 9) return;
        PutStatusText(StatusItem.Q, row, $"Q{q % 1000:D3}");
    }

    public void PutPtr(int ptr, int row)
    {
        if (row < 0 || row >= 9) return;
        PutStatusText(StatusItem.Ptr, row, $"${ptr & 0xfffff:X5}");
    }

    public void PutLFOPitch(int v, int row)
    {
        if (row < 0 || row >= 9) return;
        int t = (short)v;
        PutStatusText(StatusItem.LFOPitch, row, $"P{(t >= 0 ? '+' : '-')}{Math.Abs(t) % 10000:D4}");
    }

    public void PutLFOPitch1(int v, int row)
    {
        if (row < 0 || row >= 9) return;
        PutStatusText(StatusItem.LFOPitch1, row, ((char)(0x15 + v)).ToString());
    }

    public void PutLFOPitch2(int v, int row)
    {
        if (row < 0 || row >= 9) return;
        PutStatusText(StatusItem.LFOPitch2, row, $"{(ushort)v % 10000:D4}");
    }

    public void PutLFOPitch3(int v, int row)
    {
        if (row < 0 || row >= 9) return;
        PutStatusText(StatusItem.LFOPitch3, row, $"{(v >= 0 ? '+' : '-')}{Math.Abs(v) % 10000:D4}");
    }

    public void PutLFOPitch4(int v, int row)
    {
        if (row < 0 || row >= 9) return;
        PutStatusText(StatusItem.LFOPitch4, row, $"D{(ushort)v % 1000:D3}");
    }

    public void PutLFOVolume(int v, int row)
    {
        if (row < 0 || row >= 9) return;
        int t = (short)v;
        PutStatusText(StatusItem.LFOVolume, row, $"A{(t >= 0 ? '+' : '-')}{Math.Abs(t) % 10000:D4}");
    }

    public void PutLFOVolume1(int v, int row)
    {
        if (row < 0 || row >= 9) return;
        PutStatusText(StatusItem.LFOVolume1, row, ((char)(0x15 + v)).ToString());
    }

    public void PutLFOVolume2(int v, int row)
    {
        if (row < 0 || row >= 9) return;
        PutStatusText(StatusItem.LFOVolume2, row, $"{(ushort)v % 10000:D4}");
    }

    public void PutLFOVolume3(int v, int row)
    {
        if (row < 0 || row >= 9) return;
        PutStatusText(StatusItem.LFOVolume3, row, $"{(v >= 0 ? '+' : '-')}{Math.Abs(v) % 10000:D4}");
    }

    public void PutLevelMeter(byte[] levelMeterInfo, int row)
    {
        if (row < 0 || row >= 9) return;
        if (_levelMeter is not { Valid: true }) return;

        // 点灯しているセルだけ明るいパレットに差し替える。
        for (int i = 0; i < _skin.levelMeterWidthCells; i++)
        {
            int pal = i + _skin.levelMeterPalOfs;
            if (pal < 0 || pal >= 256) continue;
            int src = i < levelMeterInfo.Length && levelMeterInfo[i] != 0
                ? i
                : i + _skin.levelMeterWidthCells;
            if (src >= 0 && src < _palLevelMeter.Length) _levelMeter.Palette[pal] = _palLevelMeter[src];
        }

        if (ToneMode) return;
        // 素材は 1 段ぶんの幅で作ってあり、音量表示と重なる左端 (SrcX) を切って描く。
        int skip = _skin.levelMeterSrcX;
        int w = _levelMeter.Width - skip;
        int h = _levelMeter.Height;
        StatusItemPos(StatusItem.LevelMeter, row, out int x, out int y);
        Blitter.CopyComposite(_screen, x, y, w, h, _levelMeter, skip, 0, _back, x, y,
            _colors.status.colorBright);
    }

    public void PutPCMVolume(int volume, int row)
    {
        if (row < 8 || row >= 16) return;
        string s = volume >= 128
            ? $"V{127 - (volume & 127):D3}"
            : "V" + volume.ToString().PadRight(3);
        PutStatusText(StatusItem.PcmVolume, row, s);
    }

    public void PutPCMPtr(int ptr, int row)
    {
        if (row < 8 || row >= 16) return;
        PutStatusText(StatusItem.PcmPtr, row, $"${ptr & 0xfffff:X5}");
    }

    // 演奏を始める前のステータス欄（本体 PutStatusZero）。
    public void PutStatusZero()
    {
        var meter = new byte[Math.Max(1, _skin.levelMeterWidthCells)];
        for (int row = 0; row < 8; row++)
        {
            PutVolume(0, row);
            PutPanpot(0, row);
            PutDetune(0, row);
            PutVoice(0, row);
            PutQ(0, row);
            PutPtr(0, row);
            PutLFOPitch(0, row);
            PutLFOPitch1(0, row);
            PutLFOPitch2(0, row);
            PutLFOPitch3(0, row);
            PutLFOPitch4(0, row);
            PutLFOVolume(0, row);
            PutLFOVolume1(0, row);
            PutLFOVolume2(0, row);
            PutLFOVolume3(0, row);
            PutLevelMeter(meter, row);
        }
        for (int i = 0; i < 8; i++)
        {
            PutPCMVolume(0, 8 + i);
            PutPCMPtr(0, 8 + i);
        }
        // 音色データ表示のモードのときは、こちらが描かれる（本体の PutStatusZero
        // は全部 0 を敷くが、プレビューでは位置合わせのためにダミーの値を出す）。
        PutOPMDummy();
    }

    // ---- 鍵盤 --------------------------------------------------------------
    public void PutNoteOn(int key, int row, int color, bool bendMode)
    {
        if (row < 0 || row >= 9) return;
        key += _skin.keyOffset;
        if (key < 0) return;
        int oct = key / 12;
        int note = key % 12;
        var b = _keyboard[note];
        if (b is not { Valid: true }) return;

        b.Palette[2] = _kbPalette[color % 12];

        int x = _skin.kbX + _skin.kbXOffset[note] + _skin.kbXOffset[12] * oct
                - _skin.kbXOffset[_skin.keyOffset];
        int y = _skin.kbY + _skin.chYOffset[row] + _skin.kbYOffset;
        Blitter.CopyTransparent(_screen, x, y, b.Width, b.Height, b, 0, 0,
            bendMode ? _colors.kb.bright / 2 : _colors.kb.bright);
    }

    // ---- 曲名 --------------------------------------------------------------
    public void PutMDXTitle(string titleUtf8)
    {
        // 背景を戻してからタイトル欄の下地を敷く
        Blitter.Copy(_screen, _skin.titleX, _skin.titleY, _skin.titleW, _skin.titleH,
            _back, _skin.titleX, _skin.titleY, 100);
        Blitter.Fill(_screen, _skin.titleX, _skin.titleY, _skin.titleW, _skin.titleH,
            _colors.mdxTitle.backColor.R, _colors.mdxTitle.backColor.G,
            _colors.mdxTitle.backColor.B, _colors.mdxTitle.backColorBright);

        if (string.IsNullOrEmpty(titleUtf8)) return;

        // 文字はキャンバスではなく出力解像度のレイヤーへ（本体と同じ）。
        TextDraws.Add(new TextDraw(_skin.titleX + 4, _skin.titleY, _skin.titleW - 8, _skin.titleH,
            titleUtf8, _colors.mdxTitle.color, _colors.mdxTitle.colorBright, 0, 0));
    }

    // ---- ファイラー --------------------------------------------------------
    public void PutFileList(IReadOnlyList<PreviewFileItem> items, int top, int cursor, int offset)
    {
        int rows = FileListRows;
        int fs = _fileListFontSize & 1;
        int itemH = _skin.fileListItemH[fs];
        int drawRows = offset > 0 ? rows + 1 : rows;
        int listTop = _skin.fileListY;
        int listBottom = _skin.fileListY + _skin.fileListH;
        if (itemH <= 0) return;

        for (int i = 0; i < drawRows; i++)
        {
            int j = top + i;
            var shown = j >= 0 && j < items.Count ? items[j] : default;

            int x = _skin.fileListX;
            // 文字を置く基準は切る前の行の上辺。ここを動かすと字が縦に潰れる。
            int rowY = listTop + i * itemH - offset;
            int y = rowY;
            int h = itemH;
            if (y < listTop)
            {
                h -= listTop - y;
                y = listTop;
            }
            if (y + h > listBottom) h = listBottom - y;
            if (h <= 0) break;

            // 背景を戻す -> カーソル -> 文字
            Blitter.Copy(_screen, x, y, _skin.fileListW, h, _back, x, y, 100);
            if (j == cursor && j < items.Count)
            {
                Blitter.FillMul(_screen, x, y, _skin.fileListW, h, _colors.filer.cursorColor.R,
                    _colors.filer.cursorColor.G, _colors.filer.cursorColor.B,
                    _colors.filer.cursorColorBright);
            }
            if (string.IsNullOrEmpty(shown.BaseName) && string.IsNullOrEmpty(shown.Title)) continue;

            // 種別で文字色を変える。
            var color = _colors.filer.color;
            if ((shown.Type & FileItemFileSystem) != 0) color = _colors.filer.fileSystemColor;
            else if ((shown.Type & FileItemDrive) != 0) color = _colors.filer.driveColor;
            else if ((shown.Type & FileItemDir) != 0) color = _colors.filer.folderColor;

            if (!string.IsNullOrEmpty(shown.BaseName))
            {
                TextDraws.Add(new TextDraw(x + _skin.fileListBaseNameX[fs], rowY,
                    _skin.fileListBaseNameW[fs], itemH, shown.BaseName, color,
                    _colors.filer.colorBright, y, h));
            }
            if (!string.IsNullOrEmpty(shown.Title))
            {
                TextDraws.Add(new TextDraw(x + _skin.fileListTitleX[fs], rowY,
                    _skin.fileListTitleW[fs], itemH, shown.Title, color,
                    _colors.filer.colorBright, y, h));
            }
        }

        // 最終行の余りを背景で埋める（行数 * 行高がぴったりでないスキン用）
        {
            int y = listTop + drawRows * itemH - offset;
            int h = listBottom - y;
            if (h > 0) Blitter.Copy(_screen, _skin.fileListX, y, _skin.fileListW, h, _back, _skin.fileListX, y, 100);
        }
    }

    // ---- スクロールバー ----------------------------------------------------
    public void SetScrollBarThumb(int y) => ScrollBarThumb = Math.Max(0, Math.Min(ScrollBarMovement, y));

    public void PutScrollBar(int topPx, int maxTopPx)
    {
        if (_scrollBar is not { Valid: true } || _scrollBarBase is not { Valid: true }) return;

        SetScrollBarThumb(maxTopPx > 0 ? ScrollBarMovement * topPx / maxTopPx : 0);

        // 書かれない隙間はパレット 0 = 透明にしたいので、まず消す。
        Blitter.Fill(_scrollBar, 0, 0, _skin.scrollW, _skin.scrollH, 0, 0, 0, 100);

        var up = _skin.scrollSrcUpArrow;
        var bar = _skin.scrollSrcBar;
        var down = _skin.scrollSrcDownArrow;
        var thumb = _skin.scrollSrcThumb;

        Blitter.Copy(_scrollBar, _skin.scrollPosUpArrow[0], _skin.scrollPosUpArrow[1], up.W, up.H,
            _scrollBarBase, up.X, up.Y, 100);
        // 溝は素材を上から繰り返して敷く。足りない最後の 1 枚は途中で切る。
        if (bar.H > 0)
        {
            for (int gy = 0; gy < _skin.scrollGrooveH; gy += bar.H)
            {
                int gh = Math.Min(bar.H, _skin.scrollGrooveH - gy);
                Blitter.Copy(_scrollBar, _skin.scrollPosBar[0], _skin.scrollPosBar[1] + gy,
                    bar.W, gh, _scrollBarBase, bar.X, bar.Y, 100);
            }
        }
        Blitter.Copy(_scrollBar, _skin.scrollPosDownArrow[0], _skin.scrollPosDownArrow[1], down.W, down.H,
            _scrollBarBase, down.X, down.Y, 100);
        // つまみは溝の中を動く。
        Blitter.Copy(_scrollBar, _skin.scrollPosBar[0], _skin.scrollPosBar[1] + ScrollBarThumb,
            thumb.W, thumb.H, _scrollBarBase, thumb.X, thumb.Y, 100);

        // 矢印の押下表示は通常の矢印と同じ場所に差し替える。
        if ((ScrollBarFlags & ScrollBarUpArrowDown) != 0)
        {
            var s = _skin.scrollSrcUpArrowPress;
            Blitter.Copy(_scrollBar, _skin.scrollPosUpArrow[0], _skin.scrollPosUpArrow[1], s.W, s.H,
                _scrollBarBase, s.X, s.Y, 100);
        }
        if ((ScrollBarFlags & ScrollBarDownArrowDown) != 0)
        {
            var s = _skin.scrollSrcDownArrowPress;
            Blitter.Copy(_scrollBar, _skin.scrollPosDownArrow[0], _skin.scrollPosDownArrow[1], s.W, s.H,
                _scrollBarBase, s.X, s.Y, 100);
        }

        // 描画の矩形は一覧の矩形と必ず隣り合う（PlacedFor がファイラーの
        // 矩形を分けて決めている）ので、一度に描いてよい。指で掴みやすく
        // するための広い当たり判定は [ScrollBar] HitWidth のほうで、
        // そちらは一覧に食い込むが描画はしない。
        CompositeScrollBar(_skin.scrollX, _skin.scrollY, _skin.scrollW, _skin.scrollH);
    }

    private void CompositeScrollBar(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0) return;
        Blitter.CopyComposite(_screen, x, y, w, h, _scrollBar, x - _skin.scrollX,
            y - _skin.scrollY, _back, x, y, Blend.Mul);
    }

    // ---- プログレスバー ----------------------------------------------------
    public void PutProgressBar(uint nowTimeMs, uint playTimeMs)
    {
        if (_progressBar is not { Valid: true } || _progressBarBase is not { Valid: true }) return;

        uint now = nowTimeMs;
        if (playTimeMs != 0 && now > playTimeMs) now = playTimeMs;

        int len = 0;
        if (playTimeMs != 0) len = (int)((long)_skin.progW * now / playTimeMs);

        // バーは 左端 / 中央の繰り返し / 右端 の 3 つで敷く（音量バーと同じ。
        // つまみが無いだけ）。進んだ部分 [0,len) は素材の下段、残りは上段。
        // 下段は同じ矩形をその高さぶん下へずらした位置。
        Blitter.Fill(_progressBar, 0, 0, _skin.progW, _skin.progH, 0, 0, 0, 100);
        var left = _skin.progSrcBarLeft;
        var right = _skin.progSrcBarRight;
        var bar = _skin.progSrcBar;
        int middleW = _skin.progW - left.W - right.W;
        for (int pass = 0; pass < 2; pass++)
        {
            bool played = pass == 0;
            int xFrom = played ? 0 : len;
            int xTo = played ? len : _skin.progW;
            PutBarPiece(left, 0, left.W, xFrom, xTo, played ? left.H : 0);
            if (bar.W > 0 && bar.H > 0)
            {
                for (int x = 0; x < middleW; x += bar.W)
                    PutBarPiece(bar, left.W + x, Math.Min(bar.W, middleW - x), xFrom, xTo, played ? bar.H : 0);
            }
            PutBarPiece(right, _skin.progW - right.W, right.W, xFrom, xTo, played ? right.H : 0);
        }
        Blitter.CopyComposite(_screen, _skin.progX, _skin.progY, _skin.progW, _skin.progH,
            _progressBar, 0, 0, _back, _skin.progX, _skin.progY, Blend.Mul);

        int nowSec = (int)(nowTimeMs / 1000);
        int t = Math.Min(nowSec, 99 * 60 + 59);
        int t2 = Math.Min((int)(playTimeMs / 1000), 99 * 60 + 59);
        string s = $"PLAY TIME: {t / 60:D2}:{t % 60:D2} / {t2 / 60:D2}:{t2 % 60:D2}";
        PrintMiniCompose(_skin.progX + _skin.progTimePos[0], _skin.progY + _skin.progTimePos[1], s,
            _colors.playKey.color, _colors.playKey.colorBright);
    }

    // プログレスバーの部品 1 つを、横の範囲 [xFrom, xTo) に掛かるぶんだけ写す
    // （drawscreen.cpp の PutBarPiece）。
    private void PutBarPiece(Xywh piece, int dx, int pieceW, int xFrom, int xTo, int srcYOfs)
    {
        int start = Math.Max(dx, xFrom);
        int end = Math.Min(dx + pieceW, xTo);
        if (end <= start) return;
        Blitter.Copy(_progressBar, start, 0, end - start, piece.H, _progressBarBase,
            piece.X + (start - dx), piece.Y + srcYOfs, 100);
    }

    // ---- 音量バー ----------------------------------------------------------
    public int TotalVolBarPosFromVolume(int volume)
    {
        int m = TotalVolBarMovement;
        if (m <= 0) return 0;
        return Math.Max(0, Math.Min(m, (volume + 100) * m / 200));
    }

    public void PutTotalVolBar(int volume)
    {
        if (_totalVolBar is not { Valid: true } || _totalVolBarBase is not { Valid: true }) return;

        volume = Math.Max(-100, Math.Min(100, volume));
        int barPos = TotalVolBarPosFromVolume(volume);

        // バーは 左端 / 中央の繰り返し / 右端 の 3 つで敷く（スクロールバーの溝と
        // 同じ作法。あちらは縦、こちらは横）。書かれない隙間はパレット 0 に
        // したいので、まず消す。
        Blitter.Fill(_totalVolBar, 0, 0, _skin.volW, _skin.volH, 0, 0, 0, 100);

        var left = _skin.volSrcBarLeft;
        var right = _skin.volSrcBarRight;
        var bar = _skin.volSrcBar;
        var thumb = _skin.volSrcThumb;
        Blitter.Copy(_totalVolBar, 0, 0, left.W, left.H, _totalVolBarBase, left.X, left.Y, 100);
        int middleW = _skin.volW - left.W - right.W;
        if (bar.W > 0 && bar.H > 0)
        {
            for (int x = 0; x < middleW; x += bar.W)
            {
                int w = Math.Min(bar.W, middleW - x);
                Blitter.Copy(_totalVolBar, left.W + x, 0, w, bar.H, _totalVolBarBase, bar.X, bar.Y, 100);
            }
        }
        Blitter.Copy(_totalVolBar, _skin.volW - right.W, 0, right.W, right.H, _totalVolBarBase,
            right.X, right.Y, 100);
        Blitter.Copy(_totalVolBar, barPos, 0, thumb.W, thumb.H, _totalVolBarBase, thumb.X, thumb.Y, 100);
        Blitter.CopyComposite(_screen, _skin.volX, _skin.volY, _skin.volW, _skin.volH,
            _totalVolBar, 0, 0, _back, _skin.volX, _skin.volY, Blend.Mul);

        // 桁数は固定にする。短い文字列を書くと前の表示の末尾が残る。
        string s = $"{(volume >= 0 ? '+' : '-')}{Math.Abs(volume):D3}";
        PrintMiniCompose(_skin.volX + _skin.volVolumePos[0], _skin.volY + _skin.volVolumePos[1], s,
            _colors.playKey.color, _colors.playKey.colorBright);
    }

    // ---- 操作ボタン --------------------------------------------------------
    public void PutPlayKey(uint status)
    {
        if (_playKey is not { Valid: true }) return;

        _playKey.SetPalette(_skin.palPlayKeyKey, _colors.playKey.keyBright,
            _colors.playKey.keyBright, _colors.playKey.keyBright);

        // LED は毎回塗り直す（本体は差分判定つきだが、プレビューは状態を
        // 持ち越さないので、そのまま持ってくると常に消灯になってしまう）。
        (uint bit, int pal, int onPal)[] leds =
        {
            (PlayKeyPlayLed, _skin.palPlayLed, _skin.palGreen),
            // PAUSE だけ黄（2026-09-04、ユーザー指示）。
            (PlayKeyPauseLed, _skin.palPauseLed, _skin.palYellow),
            (PlayKeyContLed, _skin.palContLed, _skin.palRed),
            (PlayKeyRepeatLed, _skin.palRepeatLed, _skin.palRed),
        };
        foreach (var led in leds)
        {
            int from = (status & led.bit) != 0 ? led.onPal : _skin.palDark;
            if (led.pal is >= 0 and < 256 && from is >= 0 and < 256)
                _playKey.Palette[led.pal] = _playKey.Palette[from];
        }

        for (int i = 0; i < _skin.numPlayKeys && i < 9; i++)
        {
            uint sw = status & (1u << i);
            int w = _skin.playKeyRect[i].W;
            int h = _skin.playKeyRect[i].H;
            int x = _skin.playKeyX + _skin.playKeyPos[i][0];
            int y = _skin.playKeyY + _skin.playKeyPos[i][1];
            Blitter.CopyComposite(_screen, x, y, w, h, _playKey, _skin.playKeyRect[i].X,
                _skin.playKeyRect[i].Y + (sw != 0 ? h : 0), _back, x, y, Blend.Mul);
        }
    }
}
