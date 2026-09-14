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
    // 文字描画に使う TrueType フォント (font.ttf)。ビットマップではないが、
    // 「スキンのフォルダに置く素材ファイル」としては同じ扱い（own / 継承、
    // インポート、_nouse への退避）なので、この列挙に同居させる。
    TtfFont,
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
        BitmapRole.TtfFont => "font.ttf",
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
        BitmapRole.TtfFont => "TTFフォント",
        _ => role.ToString(),
    };

    // src/bitmap.h の注記のとおり、鍵盤・操作ボタン・レベルメータ・ミニフォントは
    // パレット番号（layout.ini の PalKey 等）で発色させるので、パレット配置に
    // 意味がある。back/banner/scrollbar/progressbar/volbar はそこまでの依存が無い。
    public static bool IsPaletteDependent(BitmapRole role) => role is
        BitmapRole.Kb0 or BitmapRole.Kb1 or BitmapRole.Kb2 or
        BitmapRole.PlayKey or BitmapRole.LevelMeter or BitmapRole.MiniFont;

    // ビットマップではなくフォントファイル（インポートの検査と保存が別）。
    public static bool IsFont(BitmapRole role) => role == BitmapRole.TtfFont;
}
