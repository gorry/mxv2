// mxv2 スキンエディタ - colors.ini の GUI 生成用スキーマ（src/colors.h 参照）。
// Bright の範囲は colors.h の注記のとおり:
//   文字・背景の不透明度 (alpha) は 0..100 / 素材へのゲインは 100 が素通しで 0..200

namespace SkinEditor.Model;

public enum ColorFieldKind { Color, Int, Bool }

public sealed record ColorFieldDef(
    string Label, ColorFieldKind Kind,
    Func<ColorsValues, RgbColor>? GetColor, Action<ColorsValues, RgbColor>? SetColor,
    Func<ColorsValues, int>? GetInt, Action<ColorsValues, int>? SetInt,
    int IntMin = 0, int IntMax = 100)
{
    public static ColorFieldDef Color(string label, Func<ColorsValues, RgbColor> get, Action<ColorsValues, RgbColor> set) =>
        new(label, ColorFieldKind.Color, get, set, null, null);

    public static ColorFieldDef Int(string label, Func<ColorsValues, int> get, Action<ColorsValues, int> set,
        int min = 0, int max = 100) =>
        new(label, ColorFieldKind.Int, null, null, get, set, min, max);

    // 実体は 0/1 の int（colors.h 側の型は変えない）だが、数値欄ではなく
    // 「使う」トグルボタンで編集させる（2026-09-04、ユーザー指示）。
    public static ColorFieldDef Bool(string label, Func<ColorsValues, int> get, Action<ColorsValues, int> set) =>
        new(label, ColorFieldKind.Bool, null, null, get, set, 0, 1);
}

public sealed record ColorSectionDef(string Title, IReadOnlyList<ColorFieldDef> Fields);

public static class ColorFieldSchema
{
    public static IReadOnlyList<ColorSectionDef> BuildSections() => new List<ColorSectionDef>
    {
        new("背景 (Back)", new List<ColorFieldDef>
        {
            ColorFieldDef.Bool("背景画像を使う", c => c.back.bitmap, (c, v) => c.back.bitmap = v),
            ColorFieldDef.Int("画像の明るさ", c => c.back.bitmapBright, (c, v) => c.back.bitmapBright = v, 0, 100),
            ColorFieldDef.Color("背景色", c => c.back.color, (c, v) => c.back.color = v),
            ColorFieldDef.Int("背景色不透明度", c => c.back.colorBright, (c, v) => c.back.colorBright = v, 0, 100),
        }),
        new("鍵盤 (KB)", new List<ColorFieldDef>
        {
            ColorFieldDef.Int("黒鍵側の下地の明るさ", c => c.kb.blackBright, (c, v) => c.kb.blackBright = v, 0, 200),
            ColorFieldDef.Int("白鍵側の下地の明るさ", c => c.kb.whiteBright, (c, v) => c.kb.whiteBright = v, 0, 200),
            ColorFieldDef.Int("押している鍵の明るさ", c => c.kb.bright, (c, v) => c.kb.bright = v, 0, 200),
        }),
        new("ステータス (Status)", new List<ColorFieldDef>
        {
            ColorFieldDef.Color("文字色", c => c.status.color, (c, v) => c.status.color = v),
            ColorFieldDef.Int("文字の不透明度", c => c.status.colorBright, (c, v) => c.status.colorBright = v),
            ColorFieldDef.Color("背景色", c => c.status.backColor, (c, v) => c.status.backColor = v),
            ColorFieldDef.Int("背景の不透明度", c => c.status.backColorBright, (c, v) => c.status.backColorBright = v),
        }),
        new("曲名 (MDXTitle)", new List<ColorFieldDef>
        {
            ColorFieldDef.Color("文字色", c => c.mdxTitle.color, (c, v) => c.mdxTitle.color = v),
            ColorFieldDef.Int("文字の不透明度", c => c.mdxTitle.colorBright, (c, v) => c.mdxTitle.colorBright = v),
            ColorFieldDef.Color("背景色", c => c.mdxTitle.backColor, (c, v) => c.mdxTitle.backColor = v),
            ColorFieldDef.Int("背景の不透明度", c => c.mdxTitle.backColorBright, (c, v) => c.mdxTitle.backColorBright = v),
        }),
        new("OPM レジスタ一覧 (RegMap)", new List<ColorFieldDef>
        {
            ColorFieldDef.Color("文字色", c => c.regMap.color, (c, v) => c.regMap.color = v),
            ColorFieldDef.Int("文字の不透明度", c => c.regMap.colorBright, (c, v) => c.regMap.colorBright = v),
            ColorFieldDef.Color("背景色", c => c.regMap.backColor, (c, v) => c.regMap.backColor = v),
            ColorFieldDef.Int("背景の不透明度", c => c.regMap.backColorBright, (c, v) => c.regMap.backColorBright = v),
        }),
        new("ファイラー (Filer)", new List<ColorFieldDef>
        {
            ColorFieldDef.Color("カーソル (背景へのゲイン)", c => c.filer.cursorColor, (c, v) => c.filer.cursorColor = v),
            ColorFieldDef.Int("カーソル不透明度", c => c.filer.cursorColorBright, (c, v) => c.filer.cursorColorBright = v),
            ColorFieldDef.Color("文字色", c => c.filer.color, (c, v) => c.filer.color = v),
            ColorFieldDef.Int("文字の不透明度", c => c.filer.colorBright, (c, v) => c.filer.colorBright = v),
            ColorFieldDef.Color("フォルダの文字色", c => c.filer.folderColor, (c, v) => c.filer.folderColor = v),
            ColorFieldDef.Color("ドライブの文字色", c => c.filer.driveColor, (c, v) => c.filer.driveColor = v),
            ColorFieldDef.Color("ファイルシステムの文字色", c => c.filer.fileSystemColor, (c, v) => c.filer.fileSystemColor = v),
            ColorFieldDef.Color("背景色", c => c.filer.backColor, (c, v) => c.filer.backColor = v),
            ColorFieldDef.Int("背景の不透明度", c => c.filer.backColorBright, (c, v) => c.filer.backColorBright = v),
        }),
        new("操作ボタン (PlayKey)", new List<ColorFieldDef>
        {
            ColorFieldDef.Color("文字色", c => c.playKey.color, (c, v) => c.playKey.color = v),
            ColorFieldDef.Int("文字の不透明度", c => c.playKey.colorBright, (c, v) => c.playKey.colorBright = v),
            ColorFieldDef.Int("ボタンの明るさ", c => c.playKey.keyBright, (c, v) => c.playKey.keyBright = v, 0, 200),
        }),
    };
}
