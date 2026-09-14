// mxv2 スキンエディタ - layout.ini の実効値（mxv2 本体 src/skin.h の struct Skin
// の移植）。フィールド名・既定値は skin.cpp の Skin::Skin() と 1:1 対応させて
// あるので、あえて C++ 側と同じ lowerCamelCase のままにしてある
// （突き合わせやすさを優先。C# の命名規約より整合性を優先した）。

namespace SkinEditor.Model;

// ファイラーを画面のどの辺に置くか（skin.h の FilerSide）。
public enum FilerSide { Bottom = 0, Top, Left, Right }

public static class FilerSides
{
    // layout.ini に書く名前。並びは FilerSide と同じ。
    public static readonly string[] Names = { "Bottom", "Top", "Left", "Right" };
    // 画面に出す名前。
    public static readonly string[] Labels = { "下", "上", "左", "右" };

    public static string Name(int side) =>
        side >= 0 && side < Names.Length ? Names[side] : Names[0];

    public static int FromName(string name, int fallback)
    {
        if (string.IsNullOrEmpty(name)) return fallback;
        for (int i = 0; i < Names.Length; i++)
        {
            if (string.Equals(name, Names[i], StringComparison.OrdinalIgnoreCase)) return i;
        }
        return fallback;
    }

    public static bool Vertical(int side) => side == (int)FilerSide.Bottom || side == (int)FilerSide.Top;
}

public sealed class SkinLayout
{
    // ---- 画面 --------------------------------------------------------
    // 「宣言サイズ」。部品の座標も配色もこの大きさで書く（fullscreen.md）。
    public int screenW = 640, screenH = 480;

    // ---- 画面の 2 分割 ------------------------------------------------
    // ファイラーを置く辺と、その辺からファイラー側の矩形が何ピクセルあるか。
    // キャンバスが伸びたぶんはすべてファイラー側が受け取る。
    public int filerSide = (int)FilerSide.Bottom;
    public int filerExtent = 114;  // 480-366（旧 mxv の一覧の上辺）

    // ---- 鍵盤 --------------------------------------------------------
    public int kbX = 4, kbY = 4;
    public int[] kbXOffset = { 0, 3, 6, 9, 12, 18, 21, 24, 27, 30, 33, 36, 42 };
    public int kbYOffset = 0;
    public int[] chYOffset = { 0, 38, 76, 114, 152, 190, 228, 266, 304 };
    public int keyOffset = 3;

    // ---- ミニフォント --------------------------------------------------
    public int miniFontW = 6, miniFontH = 8;

    // ---- ステータス ---------------------------------------------------
    // [Status] Rect の x,y は 9 段全体の左上、w,h は 1 段ぶんの背景の大きさ。
    public int statusX = 344, statusY = 4;
    public int statusW = 128, statusH = 35;
    public int[] pcmXOffset = { 0, 0, 0, 0, 68, 68, 68, 68 };
    public int[] pcmYOffset = { 0, 9, 18, 27, 0, 9, 18, 27 };
    // 項目ごとの位置（StatusItem の並びで [x, y]）。
    public int[][] statusPos = StatusItems.DefaultPos();
    // 音色データ表示で、オペレータごとの項目を置く段の y（[Status] OPMOperatorY。
    // 並びは OPM のスロット順 M1, M2, C1, C2。本体 skin.h の opmOperatorY）。
    public int[] opmOperatorY = StatusItems.DefaultOperatorY();

    // ---- レベルメータ ---------------------------------------------------
    public int levelMeterPalOfs = 32;
    public int levelMeterWidthCells = 64;
    public int levelMeterSrcX = 32;  // 素材の左端を何画素捨てるか

    // ---- バナー ---------------------------------------------------------
    public int bannerX = 476, bannerY = 4, bannerW = 160, bannerH = 54;

    // ---- 曲名 ------------------------------------------------------------
    public int titleX = 4, titleY = 348, titleW = 632, titleH = 14;
    public int titleScrollSpeed = 100;      // 横スクロールの速さ (%)。1..1000

    // ---- ファイラー -------------------------------------------------------
    // layout.ini に書くのは「ファイラー側の矩形からの内側マージン」だけ
    // （左,上,右,下）。矩形はキャンバスの大きさで変わるので絶対座標では書けない。
    public int[] fileListMargin = { 4, 0, 4, 4 };
    public int[] fileListItemH = { 10, 13 };
    public int fileListScrollSpeed = 150;   // 曲名の横スクロールの速さ (%)。小大の別なし
    public int[] fileListBaseNameX = { 5, 6 };
    public int[] fileListBaseNameW = { 120, 156 };
    public int[] fileListTitleX = { 125, 162 };

    // ---- スクロールバー -----------------------------------------------
    // スクロールバーはファイラーの矩形の右端を分け合う。書くのは幅だけ。
    public int scrollWidth = 12;
    public int scrollHitWidth = 12;
    public Xywh scrollSrcThumb = new(0, 0, 12, 12);
    public Xywh scrollSrcUpArrowPress = new(0, 12, 12, 12);
    public Xywh scrollSrcDownArrowPress = new(0, 24, 12, 12);
    public Xywh scrollSrcUpArrow = new(0, 36, 12, 12);
    public Xywh scrollSrcBar = new(0, 48, 12, 86);
    public Xywh scrollSrcDownArrow = new(0, 134, 12, 12);

    // ---- 導出値（PlacedFor が埋める。layout.ini からは読まない） ---------
    public int placedCanvasW = 640, placedCanvasH = 480;
    public int placedOtherX = 0, placedOtherY = 0;
    public int fileListX = 4, fileListY = 366, fileListW = 620, fileListH = 110;
    public int[] fileListRows = { 11, 8 };
    public int[] fileListTitleW = { 495, 458 };
    public int scrollX = 624, scrollY = 366, scrollW = 12, scrollH = 110;
    public int scrollHitX = 624, scrollHitW = 12;
    public int scrollGrooveH = 86;
    public int[] scrollPosUpArrow = { 0, 0 };
    public int[] scrollPosBar = { 0, 12 };
    public int[] scrollPosDownArrow = { 0, 98 };

    // ---- プログレスバー -----------------------------------------------
    // TimePos は時刻表示の位置（Rect の左上からの相対）。
    public int progX = 476, progY = 270, progW = 160, progH = 6;
    public int[] progTimePos = { 16, 8 };
    // 素材内の バー左端 / バー右端 / バー中央（上段 = 未再生。下段は同じ矩形を
    // その高さぶん下へずらした位置）。skin.h の progSrc*。
    public Xywh progSrcBarLeft = new(0, 0, 12, 6);
    public Xywh progSrcBarRight = new(12, 0, 12, 6);
    public Xywh progSrcBar = new(24, 0, 136, 6);

    // ---- 音量バー -----------------------------------------------------
    public int volX = 476, volY = 300, volW = 64, volH = 16;
    public int[] volVolumePos = { 64, 6 };  // 音量値の表示位置（旧 TimePos）
    // 素材内の つまみ / バー左端 / バー右端 / バー中央（skin.h の volSrc*）。
    // バーは左端・中央の繰り返し・右端で敷く（スクロールバーの溝と同じ作法）。
    public Xywh volSrcThumb = new(0, 0, 8, 16);
    public Xywh volSrcBarLeft = new(8, 0, 8, 16);
    public Xywh volSrcBarRight = new(16, 0, 8, 16);
    public Xywh volSrcBar = new(24, 0, 48, 16);

    // ---- 操作ボタン -----------------------------------------------------
    // [PlayKey] Rect の x,y は Pos<n> の原点、w,h は使うボタン全体を覆う
    // 大きさ（描画には使わない。掴む範囲などの目安）。
    public int playKeyX = 476, playKeyY = 300;
    public int playKeyW = 160, playKeyH = 44;
    public int numPlayKeys = 8;
    public Xywh[] playKeyRect =
    {
        new(0, 0, 24, 24), new(24, 0, 24, 24), new(48, 0, 33, 24), new(81, 0, 15, 24),
        new(96, 0, 24, 24), new(120, 0, 24, 24), new(144, 0, 32, 13), new(176, 0, 32, 13),
        new(208, 0, 40, 13),
    };
    public int[][] playKeyPos =
    {
        new[] { 0, 20 }, new[] { 28, 20 }, new[] { 56, 20 }, new[] { 89, 20 }, new[] { 108, 20 },
        new[] { 136, 20 }, new[] { 93, 0 }, new[] { 128, 0 }, new[] { 76, 0 },
    };

    public int palPlayKeyKey = 6, palPlayLed = 7, palPauseLed = 8, palContLed = 9, palRepeatLed = 10;
    // palYellow / palBlue は素材のパレットにある色玉。今はどの LED にも
    // 割り当てていないので描画には出てこない（2026-09-04 に追加）。
    public int palDark = 2, palRed = 16, palGreen = 17, palYellow = 18, palBlue = 19;

    // ---- 素材のファイル名 ---------------------------------------------
    public string backBitmap = "back.bmp";
    public string kb0Bitmap = "kb0.bmp";
    public string kb1Bitmap = "kb1.bmp";
    public string kb2Bitmap = "kb2.bmp";
    public string miniFontBitmap = "minifont.bmp";
    public string levelMeterBitmap = "levelmeter.bmp";
    public string bannerBitmap = "banner.bmp";
    public string playKeyBitmap = "playkey.bmp";
    public string progressBarBitmap = "progressbar.bmp";
    public string volBarBitmap = "volbar.bmp";
    public string scrollBarBitmap = "scrollbar.bmp";

    // 導出値（skin.h の fileListMaxItemH / scrollBarMovement / volBarMovement）
    public int FileListMaxItemH() => Math.Max(fileListItemH[0], fileListItemH[1]);
    // つまみが動ける幅は「**描いた**溝の高さ - つまみの高さ」。溝は繰り返して
    // 敷くので、素材の高さではなく描く高さで決まる。
    public int ScrollBarMovement() => scrollGrooveH - scrollSrcThumb.H;
    public int VolBarMovement() => volW - volSrcThumb.W;

    // ---- キャンバスの大きさに合わせる（skin.cpp の Skin::PlacedFor） -------
    //
    // ファイラー側／それ以外側に画面を分け、一覧とスクロールバーの矩形、
    // 行数、曲名の幅を求めたコピーを返す。ファイラー以外側の部品の座標には、
    // その矩形の原点が足される（ファイラーを上や左に置いたときのずれ）。
    // **プレビューと当たり判定はこのコピーを見ること。**
    //
    // エディタは常に宣言サイズで描くので canvasW/H は screenW/H を渡すが、
    // 本体と式を 1:1 で保つために引数のまま残してある。
    public SkinLayout PlacedFor(int canvasW, int canvasH)
    {
        var s = Clone();
        if (canvasW < screenW) canvasW = screenW;
        if (canvasH < screenH) canvasH = screenH;
        s.placedCanvasW = canvasW;
        s.placedCanvasH = canvasH;

        int fixedV = screenH - filerExtent;
        int fixedH = screenW - filerExtent;
        Xywh filer;
        int otherX = 0, otherY = 0;
        switch ((FilerSide)filerSide)
        {
            case FilerSide.Top:
                filer = new Xywh(0, 0, canvasW, canvasH - fixedV);
                otherY = filer.H;
                break;
            case FilerSide.Left:
                filer = new Xywh(0, 0, canvasW - fixedH, canvasH);
                otherX = filer.W;
                break;
            case FilerSide.Right:
                filer = new Xywh(fixedH, 0, canvasW - fixedH, canvasH);
                break;
            default:
                filer = new Xywh(0, fixedV, canvasW, canvasH - fixedV);
                break;
        }
        filer = new Xywh(filer.X, filer.Y, Math.Max(1, filer.W), Math.Max(1, filer.H));
        s.placedOtherX = otherX;
        s.placedOtherY = otherY;

        int innerX = filer.X + fileListMargin[0];
        int innerY = filer.Y + fileListMargin[1];
        int innerW = Math.Max(1, filer.W - fileListMargin[0] - fileListMargin[2]);
        int innerH = Math.Max(1, filer.H - fileListMargin[1] - fileListMargin[3]);

        int sw = Math.Clamp(scrollWidth, 0, Math.Max(0, innerW - 1));
        s.fileListX = innerX;
        s.fileListY = innerY;
        s.fileListW = innerW - sw;
        s.fileListH = innerH;
        s.scrollX = innerX + innerW - sw;
        s.scrollY = innerY;
        s.scrollW = sw;
        s.scrollH = innerH;

        int hitW = Math.Clamp(scrollHitWidth, sw, innerW);
        s.scrollHitX = innerX + innerW - hitW;
        s.scrollHitW = hitW;

        int upH = scrollSrcUpArrow.H;
        int downH = scrollSrcDownArrow.H;
        s.scrollGrooveH = Math.Max(0, s.scrollH - upH - downH);
        s.scrollPosUpArrow = new[] { 0, 0 };
        s.scrollPosBar = new[] { 0, upH };
        s.scrollPosDownArrow = new[] { 0, s.scrollH - downH };

        for (int i = 0; i < 2; i++)
        {
            int ih = fileListItemH[i] > 0 ? fileListItemH[i] : 1;
            s.fileListRows[i] = Math.Max(1, s.fileListH / ih);
            s.fileListTitleW[i] = Math.Max(0, s.fileListW - fileListTitleX[i]);
        }

        if (otherX != 0 || otherY != 0)
        {
            s.kbX += otherX; s.kbY += otherY;
            s.statusX += otherX; s.statusY += otherY;
            s.bannerX += otherX; s.bannerY += otherY;
            s.titleX += otherX; s.titleY += otherY;
            s.progX += otherX; s.progY += otherY;
            s.volX += otherX; s.volY += otherY;
            s.playKeyX += otherX; s.playKeyY += otherY;
        }
        return s;
    }

    public SkinLayout Clone()
    {
        var c = (SkinLayout)MemberwiseClone();
        c.kbXOffset = (int[])kbXOffset.Clone();
        c.chYOffset = (int[])chYOffset.Clone();
        c.pcmXOffset = (int[])pcmXOffset.Clone();
        c.pcmYOffset = (int[])pcmYOffset.Clone();
        c.statusPos = statusPos.Select(p => (int[])p.Clone()).ToArray();
        c.opmOperatorY = (int[])opmOperatorY.Clone();
        c.fileListMargin = (int[])fileListMargin.Clone();
        c.fileListRows = (int[])fileListRows.Clone();
        c.fileListItemH = (int[])fileListItemH.Clone();
        c.fileListBaseNameX = (int[])fileListBaseNameX.Clone();
        c.fileListBaseNameW = (int[])fileListBaseNameW.Clone();
        c.fileListTitleX = (int[])fileListTitleX.Clone();
        c.fileListTitleW = (int[])fileListTitleW.Clone();
        c.scrollPosUpArrow = (int[])scrollPosUpArrow.Clone();
        c.scrollPosBar = (int[])scrollPosBar.Clone();
        c.scrollPosDownArrow = (int[])scrollPosDownArrow.Clone();
        c.playKeyRect = (Xywh[])playKeyRect.Clone();
        c.playKeyPos = playKeyPos.Select(p => (int[])p.Clone()).ToArray();
        return c;
    }
}
