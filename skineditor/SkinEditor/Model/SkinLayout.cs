// mxv2 スキンエディタ - layout.ini の実効値（mxv2 本体 src/skin.h の struct Skin
// の移植）。フィールド名・既定値は skin.cpp の Skin::Skin() と 1:1 対応させて
// あるので、あえて C++ 側と同じ lowerCamelCase のままにしてある
// （突き合わせやすさを優先。C# の命名規約より整合性を優先した）。

namespace SkinEditor.Model;

public sealed class SkinLayout
{
    // ---- 画面 --------------------------------------------------------
    public int screenW = 640, screenH = 480;

    // ---- 鍵盤 --------------------------------------------------------
    public int kbX = 4, kbY = 4;
    public int[] kbXOffset = { 0, 3, 6, 9, 12, 18, 21, 24, 27, 30, 33, 36, 42 };
    public int kbYOffset = 0;
    public int[] chYOffset = { 0, 38, 76, 114, 152, 190, 228, 266, 304 };
    public int keyOffset = 3;

    // ---- 5x7 フォント --------------------------------------------------
    public int fontW = 6, fontH = 8;

    // ---- ステータス ---------------------------------------------------
    public int statusX = 344, statusY = 4;
    public int statusBackW = 128, statusBackH = 35;
    public int[] pcmXOffset = { 0, 0, 0, 0, 68, 68, 68, 68 };
    public int[] pcmYOffset = { 0, 9, 18, 27, 0, 9, 18, 27 };

    // ---- レベルメータ ---------------------------------------------------
    public int levelMeterPalOfs = 32;
    public int levelMeterWidthCells = 64;

    // ---- バナー ---------------------------------------------------------
    public int bannerX = 476, bannerY = 4, bannerW = 160, bannerH = 54;

    // ---- 曲名 ------------------------------------------------------------
    public int titleX = 4, titleY = 348, titleW = 632, titleH = 14;

    // ---- ファイラー -------------------------------------------------------
    public int fileListX = 4, fileListY = 366, fileListW = 620, fileListH = 110;
    public int[] fileListRows = { 11, 8 };
    public int[] fileListItemH = { 10, 13 };
    public int[] fileListBaseNameX = { 5, 6 };
    public int[] fileListBaseNameW = { 120, 156 };
    public int[] fileListTitleX = { 125, 162 };
    public int[] fileListTitleW = { 480, 624 };

    // ---- スクロールバー -----------------------------------------------
    public int scrollX = 624, scrollY = 366, scrollW = 12, scrollH = 110;
    public Xywh scrollSrcThumb = new(0, 0, 12, 12);
    public Xywh scrollSrcUpArrowPress = new(0, 12, 12, 12);
    public Xywh scrollSrcDownArrowPress = new(0, 24, 12, 12);
    public Xywh scrollSrcUpArrow = new(0, 36, 12, 12);
    public Xywh scrollSrcBar = new(0, 48, 12, 86);
    public Xywh scrollSrcDownArrow = new(0, 134, 12, 12);
    public int[] scrollPosUpArrow = { 0, 0 };
    public int[] scrollPosBar = { 0, 12 };
    public int[] scrollPosDownArrow = { 0, 98 };

    // ---- プログレスバー -----------------------------------------------
    public int progX = 476, progY = 270, progW = 160, progH = 6;
    public int progTimeXOfs = 16, progTimeYOfs = 8;

    // ---- 音量バー -----------------------------------------------------
    public int volX = 476, volY = 300, volW = 64, volH = 16;
    public int volTimeXOfs = 64, volTimeYOfs = 6;
    public int volNobW = 8;
    public Xywh[] volRect = { new(0, 0, 8, 16), new(8, 0, 64, 16) };  // [0]=つまみ [1]=スライド

    // ---- 操作ボタン -----------------------------------------------------
    public int playKeyX = 476, playKeyY = 300;
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
    public int palDark = 2, palRed = 16, palGreen = 17;

    // ---- 素材のファイル名 ---------------------------------------------
    public string backBitmap = "back.bmp";
    public string kb0Bitmap = "kb0.bmp";
    public string kb1Bitmap = "kb1.bmp";
    public string kb2Bitmap = "kb2.bmp";
    public string font5x7Bitmap = "font5x7.bmp";
    public string levelMeterBitmap = "levelmeter.bmp";
    public string bannerBitmap = "banner.bmp";
    public string playKeyBitmap = "playkey.bmp";
    public string progressBarBitmap = "progressbar.bmp";
    public string volBarBitmap = "volbar.bmp";
    public string scrollBarBitmap = "scrollbar.bmp";

    // 導出値（skin.h の fileListMaxItemH / scrollBarMovement / volBarMovement）
    public int FileListMaxItemH() => Math.Max(fileListItemH[0], fileListItemH[1]);
    public int ScrollBarMovement() => scrollSrcBar.H - scrollSrcThumb.H;
    public int VolBarMovement() => volW - volNobW;

    public SkinLayout Clone()
    {
        var c = (SkinLayout)MemberwiseClone();
        c.kbXOffset = (int[])kbXOffset.Clone();
        c.chYOffset = (int[])chYOffset.Clone();
        c.pcmXOffset = (int[])pcmXOffset.Clone();
        c.pcmYOffset = (int[])pcmYOffset.Clone();
        c.fileListRows = (int[])fileListRows.Clone();
        c.fileListItemH = (int[])fileListItemH.Clone();
        c.fileListBaseNameX = (int[])fileListBaseNameX.Clone();
        c.fileListBaseNameW = (int[])fileListBaseNameW.Clone();
        c.fileListTitleX = (int[])fileListTitleX.Clone();
        c.fileListTitleW = (int[])fileListTitleW.Clone();
        c.scrollPosUpArrow = (int[])scrollPosUpArrow.Clone();
        c.scrollPosBar = (int[])scrollPosBar.Clone();
        c.scrollPosDownArrow = (int[])scrollPosDownArrow.Clone();
        c.volRect = (Xywh[])volRect.Clone();
        c.playKeyRect = (Xywh[])playKeyRect.Clone();
        c.playKeyPos = playKeyPos.Select(p => (int[])p.Clone()).ToArray();
        return c;
    }
}
