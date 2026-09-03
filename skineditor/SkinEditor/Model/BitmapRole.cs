namespace SkinEditor.Model;

public enum BitmapRole
{
    Back,
    Kb0,
    Kb1,
    Kb2,
    MiniFont,
    LevelMeter,
    Banner,
    PlayKey,
    ProgressBar,
    VolumeBar,
    ScrollBar,
}

public static class BitmapRoleInfo
{
    // src/skin.cpp の Skin::Skin() にある既定ファイル名（skineditor.md の指示で、
    // ファイル名はこの既定から変えない）。
    public static string DefaultFileName(BitmapRole role) => role switch
    {
        BitmapRole.Back => "back.bmp",
        BitmapRole.Kb0 => "kb0.bmp",
        BitmapRole.Kb1 => "kb1.bmp",
        BitmapRole.Kb2 => "kb2.bmp",
        BitmapRole.MiniFont => "minifont.bmp",
        BitmapRole.LevelMeter => "levelmeter.bmp",
        BitmapRole.Banner => "banner.bmp",
        BitmapRole.PlayKey => "playkey.bmp",
        BitmapRole.ProgressBar => "progressbar.bmp",
        BitmapRole.VolumeBar => "volbar.bmp",
        BitmapRole.ScrollBar => "scrollbar.bmp",
        _ => throw new ArgumentOutOfRangeException(nameof(role)),
    };

    public static string DisplayName(BitmapRole role) => role switch
    {
        BitmapRole.Back => "背景",
        BitmapRole.Kb0 => "鍵盤の下地",
        BitmapRole.Kb1 => "鍵盤（白鍵側）",
        BitmapRole.Kb2 => "鍵盤（黒鍵側）",
        BitmapRole.MiniFont => "ミニフォント",
        BitmapRole.LevelMeter => "レベルメータ",
        BitmapRole.Banner => "バナー",
        BitmapRole.PlayKey => "操作ボタン",
        BitmapRole.ProgressBar => "プログレスバー",
        BitmapRole.VolumeBar => "音量バー",
        BitmapRole.ScrollBar => "スクロールバー",
        _ => role.ToString(),
    };

    // src/bitmap.h の注記のとおり、鍵盤・操作ボタン・レベルメータ・ミニフォントは
    // パレット番号（layout.ini の PalKey 等）で発色させるので、パレット配置に
    // 意味がある。back/banner/scrollbar/progressbar/volbar はそこまでの依存が無い。
    public static bool IsPaletteDependent(BitmapRole role) => role is
        BitmapRole.Kb0 or BitmapRole.Kb1 or BitmapRole.Kb2 or
        BitmapRole.PlayKey or BitmapRole.LevelMeter or BitmapRole.MiniFont;
}
