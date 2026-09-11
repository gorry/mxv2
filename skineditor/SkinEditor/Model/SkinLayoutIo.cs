// mxv2 スキンエディタ - layout.ini の読み書き（mxv2 本体 src/skin.cpp の
// Skin::ApplyLayout / Skin::Load の中身を 1:1 で移植したもの）。
//
// ApplyLayout は「キーが書いてあれば上書き、無ければ今の値のまま」なので、
// SkinLayout を土台（Base の一番遠いところ）から順に重ねていけば、
// mxv2 本体とまったく同じ実効値になる。WriteAll はその逆で、今の実効値を
// 全キーぶん書き出す（「参照→無参照」の実効値コピーで使う）。

namespace SkinEditor.Model;

public static class SkinLayoutIo
{
    public static string ReadBaseRef(IniDocument ini) => ini.GetString("Skin", "Base", "");

    // ---- 読み込み（土台から順に呼ぶ） ----------------------------------
    public static void ApplyLayout(IniDocument ini, SkinLayout t)
    {
        t.screenW = ini.GetInt("Screen", "Width", t.screenW);
        t.screenH = ini.GetInt("Screen", "Height", t.screenH);
        t.filerSide = FilerSides.FromName(ini.GetString("Screen", "FilerSide", ""), t.filerSide);
        t.filerExtent = ini.GetInt("Screen", "FilerExtent", t.filerExtent);
        t.backBitmap = ini.GetString("Screen", "ImgBack", t.backBitmap);

        GetXy(ini, "Keyboard", "Pos", ref t.kbX, ref t.kbY);
        GetIntList(ini, "Keyboard", "XOffset", t.kbXOffset, 13);
        t.kbYOffset = ini.GetInt("Keyboard", "YOffset", t.kbYOffset);
        GetIntList(ini, "Keyboard", "ChannelY", t.chYOffset, 9);
        t.keyOffset = ini.GetInt("Keyboard", "KeyOffset", t.keyOffset);
        t.kb0Bitmap = ini.GetString("Keyboard", "ImgKeyboard0", t.kb0Bitmap);
        t.kb1Bitmap = ini.GetString("Keyboard", "ImgKeyboard1", t.kb1Bitmap);
        t.kb2Bitmap = ini.GetString("Keyboard", "ImgKeyboard2", t.kb2Bitmap);

        t.miniFontW = ini.GetInt("MiniFont", "Width", t.miniFontW);
        t.miniFontH = ini.GetInt("MiniFont", "Height", t.miniFontH);
        t.miniFontBitmap = ini.GetString("MiniFont", "ImgMiniFont", t.miniFontBitmap);

        {
            var r = new Xywh(t.statusX, t.statusY, t.statusW, t.statusH);
            GetXywh(ini, "Status", "Rect", ref r);
            t.statusX = r.X; t.statusY = r.Y; t.statusW = r.W; t.statusH = r.H;
        }
        GetIntList(ini, "Status", "PcmX", t.pcmXOffset, 8);
        GetIntList(ini, "Status", "PcmY", t.pcmYOffset, 8);
        for (int i = 0; i < StatusItems.Count; i++)
            GetIntList(ini, "Status", StatusItems.Keys[i], t.statusPos[i], 2);

        t.levelMeterPalOfs = ini.GetInt("LevelMeter", "PaletteOffset", t.levelMeterPalOfs);
        t.levelMeterWidthCells = ini.GetInt("LevelMeter", "Cells", t.levelMeterWidthCells);
        t.levelMeterSrcX = ini.GetInt("LevelMeter", "SrcX", t.levelMeterSrcX);
        t.levelMeterBitmap = ini.GetString("LevelMeter", "ImgLevelMeter", t.levelMeterBitmap);

        {
            var r = new Xywh(t.bannerX, t.bannerY, t.bannerW, t.bannerH);
            GetXywh(ini, "Banner", "Rect", ref r);
            t.bannerX = r.X; t.bannerY = r.Y; t.bannerW = r.W; t.bannerH = r.H;
        }
        t.bannerBitmap = ini.GetString("Banner", "ImgBanner", t.bannerBitmap);
        {
            var r = new Xywh(t.titleX, t.titleY, t.titleW, t.titleH);
            GetXywh(ini, "Title", "Rect", ref r);
            t.titleX = r.X; t.titleY = r.Y; t.titleW = r.W; t.titleH = r.H;
        }
        t.titleScrollSpeed = Math.Clamp(ini.GetInt("Title", "ScrollSpeed", t.titleScrollSpeed), 1, 1000);
        GetIntList(ini, "FileList", "Margin", t.fileListMargin, 4);
        t.fileListScrollSpeed = Math.Clamp(ini.GetInt("FileList", "ScrollSpeed", t.fileListScrollSpeed), 1, 1000);
        GetFontSizePair(ini, "FileList", "ItemHeight", t.fileListItemH);
        GetFontSizePair(ini, "FileList", "BaseNameX", t.fileListBaseNameX);
        GetFontSizePair(ini, "FileList", "BaseNameWidth", t.fileListBaseNameW);
        GetFontSizePair(ini, "FileList", "TitleX", t.fileListTitleX);

        t.scrollWidth = ini.GetInt("ScrollBar", "Width", t.scrollWidth);
        t.scrollHitWidth = ini.GetInt("ScrollBar", "HitWidth", t.scrollHitWidth);
        GetXywh(ini, "ScrollBar", "SrcThumb", ref t.scrollSrcThumb);
        GetXywh(ini, "ScrollBar", "SrcUpArrowPress", ref t.scrollSrcUpArrowPress);
        GetXywh(ini, "ScrollBar", "SrcDownArrowPress", ref t.scrollSrcDownArrowPress);
        GetXywh(ini, "ScrollBar", "SrcUpArrow", ref t.scrollSrcUpArrow);
        GetXywh(ini, "ScrollBar", "SrcBar", ref t.scrollSrcBar);
        GetXywh(ini, "ScrollBar", "SrcDownArrow", ref t.scrollSrcDownArrow);
        t.scrollBarBitmap = ini.GetString("ScrollBar", "ImgScrollBar", t.scrollBarBitmap);

        {
            var r = new Xywh(t.progX, t.progY, t.progW, t.progH);
            GetXywh(ini, "ProgressBar", "Rect", ref r);
            t.progX = r.X; t.progY = r.Y; t.progW = r.W; t.progH = r.H;
        }
        GetIntList(ini, "ProgressBar", "TimePos", t.progTimePos, 2);
        GetXywh(ini, "ProgressBar", "SrcBarLeft", ref t.progSrcBarLeft);
        GetXywh(ini, "ProgressBar", "SrcBarRight", ref t.progSrcBarRight);
        GetXywh(ini, "ProgressBar", "SrcBar", ref t.progSrcBar);
        t.progressBarBitmap = ini.GetString("ProgressBar", "ImgProgressBar", t.progressBarBitmap);

        {
            var r = new Xywh(t.volX, t.volY, t.volW, t.volH);
            GetXywh(ini, "VolumeBar", "Rect", ref r);
            t.volX = r.X; t.volY = r.Y; t.volW = r.W; t.volH = r.H;
        }
        GetIntList(ini, "VolumeBar", "VolumePos", t.volVolumePos, 2);
        GetXywh(ini, "VolumeBar", "SrcThumb", ref t.volSrcThumb);
        GetXywh(ini, "VolumeBar", "SrcBarLeft", ref t.volSrcBarLeft);
        GetXywh(ini, "VolumeBar", "SrcBarRight", ref t.volSrcBarRight);
        GetXywh(ini, "VolumeBar", "SrcBar", ref t.volSrcBar);
        t.volBarBitmap = ini.GetString("VolumeBar", "ImgVolumeBar", t.volBarBitmap);

        {
            var r = new Xywh(t.playKeyX, t.playKeyY, t.playKeyW, t.playKeyH);
            GetXywh(ini, "PlayKey", "Rect", ref r);
            t.playKeyX = r.X; t.playKeyY = r.Y; t.playKeyW = r.W; t.playKeyH = r.H;
        }
        t.numPlayKeys = Math.Clamp(ini.GetInt("PlayKey", "Count", t.numPlayKeys), 0, 9);
        for (int i = 0; i < 9; i++)
        {
            var r = t.playKeyRect[i];
            GetXywh(ini, "PlayKey", IndexedKey("Src", i), ref r);
            t.playKeyRect[i] = r;
            GetIntList(ini, "PlayKey", IndexedKey("Pos", i), t.playKeyPos[i], 2);
        }
        t.palPlayKeyKey = ini.GetInt("PlayKey", "PalKey", t.palPlayKeyKey);
        t.palPlayLed = ini.GetInt("PlayKey", "PalPlayLed", t.palPlayLed);
        t.palPauseLed = ini.GetInt("PlayKey", "PalPauseLed", t.palPauseLed);
        t.palContLed = ini.GetInt("PlayKey", "PalContLed", t.palContLed);
        t.palRepeatLed = ini.GetInt("PlayKey", "PalRepeatLed", t.palRepeatLed);
        t.palDark = ini.GetInt("PlayKey", "PalDark", t.palDark);
        t.palRed = ini.GetInt("PlayKey", "PalRed", t.palRed);
        t.palGreen = ini.GetInt("PlayKey", "PalGreen", t.palGreen);
        t.palYellow = ini.GetInt("PlayKey", "PalYellow", t.palYellow);
        t.palBlue = ini.GetInt("PlayKey", "PalBlue", t.palBlue);
        t.playKeyBitmap = ini.GetString("PlayKey", "ImgPlayKey", t.playKeyBitmap);
    }

    // ---- 書き出し（実効値をすべて明示キーとして書く） --------------------
    public static void WriteAll(SkinLayout t, IniDocument ini)
    {
        ini.SetInt("Screen", "Width", t.screenW);
        ini.SetInt("Screen", "Height", t.screenH);
        ini.SetString("Screen", "FilerSide", FilerSides.Name(t.filerSide));
        ini.SetInt("Screen", "FilerExtent", t.filerExtent);
        ini.SetString("Screen", "ImgBack", t.backBitmap);

        ini.SetString("Keyboard", "Pos", $"{t.kbX},{t.kbY}");
        ini.SetString("Keyboard", "XOffset", Join(t.kbXOffset));
        ini.SetInt("Keyboard", "YOffset", t.kbYOffset);
        ini.SetString("Keyboard", "ChannelY", Join(t.chYOffset));
        ini.SetInt("Keyboard", "KeyOffset", t.keyOffset);
        ini.SetString("Keyboard", "ImgKeyboard0", t.kb0Bitmap);
        ini.SetString("Keyboard", "ImgKeyboard1", t.kb1Bitmap);
        ini.SetString("Keyboard", "ImgKeyboard2", t.kb2Bitmap);

        ini.SetInt("MiniFont", "Width", t.miniFontW);
        ini.SetInt("MiniFont", "Height", t.miniFontH);
        ini.SetString("MiniFont", "ImgMiniFont", t.miniFontBitmap);

        ini.SetString("Status", "Rect", $"{t.statusX},{t.statusY},{t.statusW},{t.statusH}");
        ini.SetString("Status", "PcmX", Join(t.pcmXOffset));
        ini.SetString("Status", "PcmY", Join(t.pcmYOffset));
        for (int i = 0; i < StatusItems.Count; i++)
            ini.SetString("Status", StatusItems.Keys[i], Join(t.statusPos[i]));

        ini.SetInt("LevelMeter", "PaletteOffset", t.levelMeterPalOfs);
        ini.SetInt("LevelMeter", "Cells", t.levelMeterWidthCells);
        ini.SetInt("LevelMeter", "SrcX", t.levelMeterSrcX);
        ini.SetString("LevelMeter", "ImgLevelMeter", t.levelMeterBitmap);

        ini.SetString("Banner", "Rect", $"{t.bannerX},{t.bannerY},{t.bannerW},{t.bannerH}");
        ini.SetString("Banner", "ImgBanner", t.bannerBitmap);

        ini.SetString("Title", "Rect", $"{t.titleX},{t.titleY},{t.titleW},{t.titleH}");

        ini.SetInt("Title", "ScrollSpeed", t.titleScrollSpeed);
        ini.SetString("FileList", "Margin", Join(t.fileListMargin));
        ini.SetInt("FileList", "ScrollSpeed", t.fileListScrollSpeed);
        ini.SetString("FileList", "ItemHeight", Join(t.fileListItemH));
        ini.SetString("FileList", "BaseNameX", Join(t.fileListBaseNameX));
        ini.SetString("FileList", "BaseNameWidth", Join(t.fileListBaseNameW));
        ini.SetString("FileList", "TitleX", Join(t.fileListTitleX));

        ini.SetInt("ScrollBar", "Width", t.scrollWidth);
        ini.SetInt("ScrollBar", "HitWidth", t.scrollHitWidth);
        ini.SetString("ScrollBar", "SrcThumb", t.scrollSrcThumb.ToString());
        ini.SetString("ScrollBar", "SrcUpArrowPress", t.scrollSrcUpArrowPress.ToString());
        ini.SetString("ScrollBar", "SrcDownArrowPress", t.scrollSrcDownArrowPress.ToString());
        ini.SetString("ScrollBar", "SrcUpArrow", t.scrollSrcUpArrow.ToString());
        ini.SetString("ScrollBar", "SrcBar", t.scrollSrcBar.ToString());
        ini.SetString("ScrollBar", "SrcDownArrow", t.scrollSrcDownArrow.ToString());
        ini.SetString("ScrollBar", "ImgScrollBar", t.scrollBarBitmap);

        ini.SetString("ProgressBar", "Rect", $"{t.progX},{t.progY},{t.progW},{t.progH}");
        ini.SetString("ProgressBar", "TimePos", Join(t.progTimePos));
        ini.SetString("ProgressBar", "SrcBarLeft", t.progSrcBarLeft.ToString());
        ini.SetString("ProgressBar", "SrcBarRight", t.progSrcBarRight.ToString());
        ini.SetString("ProgressBar", "SrcBar", t.progSrcBar.ToString());
        ini.SetString("ProgressBar", "ImgProgressBar", t.progressBarBitmap);

        ini.SetString("VolumeBar", "Rect", $"{t.volX},{t.volY},{t.volW},{t.volH}");
        ini.SetString("VolumeBar", "VolumePos", Join(t.volVolumePos));
        ini.SetString("VolumeBar", "SrcThumb", t.volSrcThumb.ToString());
        ini.SetString("VolumeBar", "SrcBarLeft", t.volSrcBarLeft.ToString());
        ini.SetString("VolumeBar", "SrcBarRight", t.volSrcBarRight.ToString());
        ini.SetString("VolumeBar", "SrcBar", t.volSrcBar.ToString());
        ini.SetString("VolumeBar", "ImgVolumeBar", t.volBarBitmap);

        ini.SetString("PlayKey", "Rect", $"{t.playKeyX},{t.playKeyY},{t.playKeyW},{t.playKeyH}");
        ini.SetInt("PlayKey", "Count", t.numPlayKeys);
        for (int i = 0; i < 9; i++)
        {
            ini.SetString("PlayKey", IndexedKey("Src", i), t.playKeyRect[i].ToString());
            ini.SetString("PlayKey", IndexedKey("Pos", i), Join(t.playKeyPos[i]));
        }
        ini.SetInt("PlayKey", "PalKey", t.palPlayKeyKey);
        ini.SetInt("PlayKey", "PalPlayLed", t.palPlayLed);
        ini.SetInt("PlayKey", "PalPauseLed", t.palPauseLed);
        ini.SetInt("PlayKey", "PalContLed", t.palContLed);
        ini.SetInt("PlayKey", "PalRepeatLed", t.palRepeatLed);
        ini.SetInt("PlayKey", "PalDark", t.palDark);
        ini.SetInt("PlayKey", "PalRed", t.palRed);
        ini.SetInt("PlayKey", "PalGreen", t.palGreen);
        ini.SetInt("PlayKey", "PalYellow", t.palYellow);
        ini.SetInt("PlayKey", "PalBlue", t.palBlue);
        ini.SetString("PlayKey", "ImgPlayKey", t.playKeyBitmap);
    }

    // ---- src/skin.cpp のローカルヘルパー群の移植 -----------------------
    public static int GetIntList(IniDocument ini, string section, string key, int[] outArr, int count)
    {
        string s = ini.GetString(section, key, "");
        if (s.Length == 0) return 0;
        int n = 0, pos = 0;
        while (pos <= s.Length && n < count)
        {
            int comma = s.IndexOf(',', pos);
            string item = comma < 0 ? s[pos..] : s[pos..comma];
            if (item.Length > 0) outArr[n++] = AtoI(item);
            if (comma < 0) break;
            pos = comma + 1;
        }
        return n;
    }

    private static void GetFontSizePair(IniDocument ini, string section, string key, int[] outPair)
    {
        int[] v = { outPair[0], outPair[1] };
        int n = GetIntList(ini, section, key, v, 2);
        if (n == 0) return;
        outPair[0] = v[0];
        outPair[1] = n >= 2 ? v[1] : v[0];
    }

    public static void GetXywh(IniDocument ini, string section, string key, ref Xywh outVal)
    {
        int[] v = { outVal.X, outVal.Y, outVal.W, outVal.H };
        GetIntList(ini, section, key, v, 4);
        outVal = new Xywh(v[0], v[1], v[2], v[3]);
    }

    private static void GetXy(IniDocument ini, string section, string key, ref int x, ref int y)
    {
        int[] v = { x, y };
        GetIntList(ini, section, key, v, 2);
        x = v[0]; y = v[1];
    }

    private static string IndexedKey(string prefix, int i) => $"{prefix}{i}";

    public static string Join(IReadOnlyList<int> v) => string.Join(",", v);

    public static int AtoI(string s)
    {
        int i = 0, n = s.Length;
        while (i < n && (s[i] == ' ' || (s[i] >= 0x09 && s[i] <= 0x0d))) i++;
        int sign = 1;
        if (i < n && (s[i] == '+' || s[i] == '-'))
        {
            if (s[i] == '-') sign = -1;
            i++;
        }
        long val = 0;
        bool any = false;
        while (i < n && s[i] >= '0' && s[i] <= '9')
        {
            any = true;
            val = val * 10 + (s[i] - '0');
            if (val > int.MaxValue) val = int.MaxValue;
            i++;
        }
        return any ? (int)(sign * val) : 0;
    }
}
