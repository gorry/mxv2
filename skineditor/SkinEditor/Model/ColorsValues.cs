// mxv2 スキンエディタ - colors.ini の値（mxv2 本体 src/colors.h の
// struct Colors の移植）。既定値は colors.cpp の Colors::Colors() と同じ。

namespace SkinEditor.Model;

public sealed class ColorsValues
{
    public struct BackT
    {
        public int bitmap;
        public int bitmapBright;
        public RgbColor color;
        public int colorBright;
    }
    public struct KbT
    {
        public int blackBright;
        public int whiteBright;
        public int bright;
    }
    public struct StatusT
    {
        public RgbColor color;
        public int colorBright;
        public RgbColor backColor;
        public int backColorBright;
    }
    public struct MdxTitleT
    {
        public RgbColor color;
        public int colorBright;
        public RgbColor backColor;
        public int backColorBright;
    }
    public struct FilerT
    {
        public RgbColor cursorColor;
        public int cursorColorBright;
        public RgbColor color;
        public int colorBright;
        public RgbColor folderColor;
        public RgbColor driveColor;
        public RgbColor fileSystemColor;
        public RgbColor backColor;
        public int backColorBright;
    }
    public struct PlayKeyT
    {
        public RgbColor color;
        public int colorBright;
        public int keyBright;
    }

    public BackT back;
    public KbT kb;
    public StatusT status;
    public MdxTitleT mdxTitle;
    public FilerT filer;
    public PlayKeyT playKey;

    public ColorsValues()
    {
        back.bitmap = 0;
        back.bitmapBright = 50;
        back.color = new RgbColor(0, 0, 255);
        back.colorBright = 50;

        kb.blackBright = 50;
        kb.whiteBright = 150;
        kb.bright = 100;

        status.color = new RgbColor(255, 255, 255);
        status.colorBright = 100;
        status.backColor = new RgbColor(0, 0, 0);
        status.backColorBright = 50;

        mdxTitle.color = new RgbColor(255, 255, 255);
        mdxTitle.colorBright = 100;
        mdxTitle.backColor = new RgbColor(0, 0, 0);
        mdxTitle.backColorBright = 50;

        filer.cursorColor = new RgbColor(50, 50, 200);
        filer.cursorColorBright = 100;
        filer.color = new RgbColor(255, 255, 255);
        filer.colorBright = 100;
        filer.folderColor = new RgbColor(128, 255, 128);
        filer.driveColor = new RgbColor(255, 128, 128);
        filer.fileSystemColor = new RgbColor(255, 255, 128);
        filer.backColor = new RgbColor(0, 0, 0);
        filer.backColorBright = 50;

        playKey.color = new RgbColor(255, 255, 255);
        playKey.colorBright = 100;
        playKey.keyBright = 150;
    }

    public ColorsValues Clone() => (ColorsValues)MemberwiseClone();
}
