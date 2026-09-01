// mxv2 スキンエディタ - colors.ini の読み書き（src/colors.cpp の
// Colors::Load / Colors::Save の移植）。
//
// 重要: mxv2 本体の colors.ini は layout.ini と違って「Base 鎖をキー単位で
// 重ねる」のではなく、「Base 鎖の中で最初に見つかった 1 ファイルをまるごと
// 読む」（drawscreen.cpp: colors_ = Colors(); colors_.Load(FindColorsFile())）。
// そのファイルの中で書かれていないキーは、Base 側の値ではなく既定値へ
// フォールバックする。だから colors.ini は「無い（＝完全に継承）」か
// 「ある（＝そのファイルが全キーの基準になる）」の二択で扱う必要があり、
// layout.ini のようなキー単位の差分保存はできない。
// エディタはこれに合わせて、own の colors.ini を持つと決めた時点で
// 全キーを書き出す（ColorsIo.WriteAll）。
namespace SkinEditor.Model;

public static class ColorsIo
{
    public static void Apply(IniDocument ini, ColorsValues c)
    {
        c.back.bitmap = ini.GetInt("Back", "Bitmap", c.back.bitmap);
        c.back.bitmapBright = ini.GetInt("Back", "BitmapBright", c.back.bitmapBright);
        c.back.color = RgbColor.FromColorRef(ini.GetInt("Back", "Color", c.back.color.ToColorRef()));
        c.back.colorBright = ini.GetInt("Back", "ColorBright", c.back.colorBright);

        c.kb.blackBright = ini.GetInt("KB", "BlackBright", c.kb.blackBright);
        c.kb.whiteBright = ini.GetInt("KB", "WhiteBright", c.kb.whiteBright);
        c.kb.bright = ini.GetInt("KB", "Bright", c.kb.bright);

        c.status.color = RgbColor.FromColorRef(ini.GetInt("Status", "Color", c.status.color.ToColorRef()));
        c.status.colorBright = ini.GetInt("Status", "ColorBright", c.status.colorBright);
        c.status.backColor =
            RgbColor.FromColorRef(ini.GetInt("Status", "BackColor", c.status.backColor.ToColorRef()));
        c.status.backColorBright = ini.GetInt("Status", "BackColorBright", c.status.backColorBright);

        c.mdxTitle.color =
            RgbColor.FromColorRef(ini.GetInt("MDXTitle", "Color", c.mdxTitle.color.ToColorRef()));
        c.mdxTitle.colorBright = ini.GetInt("MDXTitle", "ColorBright", c.mdxTitle.colorBright);
        c.mdxTitle.backColor =
            RgbColor.FromColorRef(ini.GetInt("MDXTitle", "BackColor", c.mdxTitle.backColor.ToColorRef()));
        c.mdxTitle.backColorBright =
            ini.GetInt("MDXTitle", "BackColorBright", c.mdxTitle.backColorBright);

        {
            // 旧い名前 CursorBright も読む。両方あれば新しい方 (CursorColor) が勝つ
            // （colors.cpp と同じ順で読む）。
            int r = ini.GetInt("Filer", "CursorBright", c.filer.cursorColor.ToColorRef());
            r = ini.GetInt("Filer", "CursorColor", r);
            c.filer.cursorColor = RgbColor.FromColorRef(r);
        }
        c.filer.cursorColorBright = ini.GetInt("Filer", "CursorColorBright", c.filer.cursorColorBright);
        c.filer.color = RgbColor.FromColorRef(ini.GetInt("Filer", "Color", c.filer.color.ToColorRef()));
        c.filer.colorBright = ini.GetInt("Filer", "ColorBright", c.filer.colorBright);
        c.filer.folderColor =
            RgbColor.FromColorRef(ini.GetInt("Filer", "FolderColor", c.filer.folderColor.ToColorRef()));
        c.filer.driveColor =
            RgbColor.FromColorRef(ini.GetInt("Filer", "DriveColor", c.filer.driveColor.ToColorRef()));
        c.filer.fileSystemColor = RgbColor.FromColorRef(
            ini.GetInt("Filer", "FileSystemColor", c.filer.fileSystemColor.ToColorRef()));
        c.filer.backColor =
            RgbColor.FromColorRef(ini.GetInt("Filer", "BackColor", c.filer.backColor.ToColorRef()));
        c.filer.backColorBright = ini.GetInt("Filer", "BackColorBright", c.filer.backColorBright);

        c.playKey.color = RgbColor.FromColorRef(ini.GetInt("PlayKey", "Color", c.playKey.color.ToColorRef()));
        c.playKey.colorBright = ini.GetInt("PlayKey", "ColorBright", c.playKey.colorBright);
        c.playKey.keyBright = ini.GetInt("PlayKey", "KeyBright", c.playKey.keyBright);
    }

    public static void WriteAll(ColorsValues c, IniDocument ini)
    {
        ini.SetInt("Back", "Bitmap", c.back.bitmap);
        ini.SetInt("Back", "BitmapBright", c.back.bitmapBright);
        ini.SetInt("Back", "Color", c.back.color.ToColorRef());
        ini.SetInt("Back", "ColorBright", c.back.colorBright);

        ini.SetInt("KB", "BlackBright", c.kb.blackBright);
        ini.SetInt("KB", "WhiteBright", c.kb.whiteBright);
        ini.SetInt("KB", "Bright", c.kb.bright);

        ini.SetInt("Status", "Color", c.status.color.ToColorRef());
        ini.SetInt("Status", "ColorBright", c.status.colorBright);
        ini.SetInt("Status", "BackColor", c.status.backColor.ToColorRef());
        ini.SetInt("Status", "BackColorBright", c.status.backColorBright);

        ini.SetInt("MDXTitle", "Color", c.mdxTitle.color.ToColorRef());
        ini.SetInt("MDXTitle", "ColorBright", c.mdxTitle.colorBright);
        ini.SetInt("MDXTitle", "BackColor", c.mdxTitle.backColor.ToColorRef());
        ini.SetInt("MDXTitle", "BackColorBright", c.mdxTitle.backColorBright);

        ini.Remove("Filer", "CursorBright");
        ini.SetInt("Filer", "CursorColor", c.filer.cursorColor.ToColorRef());
        ini.SetInt("Filer", "CursorColorBright", c.filer.cursorColorBright);
        ini.SetInt("Filer", "Color", c.filer.color.ToColorRef());
        ini.SetInt("Filer", "ColorBright", c.filer.colorBright);
        ini.SetInt("Filer", "FolderColor", c.filer.folderColor.ToColorRef());
        ini.SetInt("Filer", "DriveColor", c.filer.driveColor.ToColorRef());
        ini.SetInt("Filer", "FileSystemColor", c.filer.fileSystemColor.ToColorRef());
        ini.SetInt("Filer", "BackColor", c.filer.backColor.ToColorRef());
        ini.SetInt("Filer", "BackColorBright", c.filer.backColorBright);

        ini.SetInt("PlayKey", "Color", c.playKey.color.ToColorRef());
        ini.SetInt("PlayKey", "ColorBright", c.playKey.colorBright);
        ini.SetInt("PlayKey", "KeyBright", c.playKey.keyBright);
    }
}
