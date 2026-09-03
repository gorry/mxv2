// mxv2 スキンエディタ - 文字だけを出力解像度で描くレイヤー
// （mxv2 本体 src/textlayer.* / src/textrender.* に相当）。
//
// 本体はキャンバス (640x480) を拡大したうえに、**拡大後の解像度**で
// 文字を描いて重ねる（半端な倍率で拡大すると字が汚れるため。textlayer.h）。
// プレビューも同じ 2 段構えにする。
//
// ラスタライズは本体が stb_truetype、こちらは GDI+。**画素は一致しない**
// （字送りとアンチエイリアスの出方が違う）。「大きさがほぼ合っていればよい」
// という前提で、字の高さだけ本体と揃うように em サイズを計算している:
//   本体: stbtt_ScaleForPixelHeight(cellHeight) …「ascent-descent が
//         cellHeight に収まる倍率」で、baseline = ascent * scale
//   ここ: emSize = cellHeight * emHeight / (ascent + descent)
// どちらも「ascent+descent が行の高さになる」ので、同じ大きさの字になる。

using System.Drawing;
using System.Drawing.Text;
using SkinEditor.Model.Render;

namespace SkinEditor.UI;

public sealed class PreviewTextLayer : IDisposable
{
    private PrivateFontCollection? _collection;
    private FontFamily? _family;
    private string? _loadedPath;

    public bool Available => _family != null;

    // スキンの font.ttf を読む。同じパスなら読み直さない。
    public void SetFontFile(string? path)
    {
        if (path == _loadedPath) return;
        Release();
        _loadedPath = path;
        if (string.IsNullOrEmpty(path) || !File.Exists(path)) return;

        try
        {
            _collection = new PrivateFontCollection();
            _collection.AddFontFile(path);
            if (_collection.Families.Length > 0) _family = _collection.Families[0];
        }
        catch
        {
            // 読めないフォントは無かったことにする（本体も available() が
            // false になり、呼び出し側がミニフォントへ退避する）。
            Release();
        }
    }

    // 積んである文字を、拡大後の座標へ描く。
    // originX/originY と scale はキャンバスを貼った場所と倍率。
    public void Render(Graphics g, IReadOnlyList<TextDraw> draws, float originX, float originY,
        float scale)
    {
        if (_family == null || draws.Count == 0) return;

        var savedClip = g.Clip;
        var savedMode = g.TextRenderingHint;
        g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
        try
        {
            foreach (var d in draws)
            {
                if (string.IsNullOrEmpty(d.Text) || d.H <= 0) continue;

                // 書き込んでよい縦の範囲。指定が無ければ 1 行ぶん（本体と同じ）。
                int clipY = d.ClipH > 0 ? d.ClipY : d.Y;
                int clipH = d.ClipH > 0 ? d.ClipH : d.H;

                var clipRect = new RectangleF(
                    originX + d.X * scale, originY + clipY * scale,
                    d.W * scale, clipH * scale);
                if (clipRect.Width <= 0 || clipRect.Height <= 0) continue;
                g.Clip = new Region(clipRect);

                float cellHeight = d.H * scale;
                using var font = MakeFont(cellHeight, out float topPad);
                if (font == null) continue;

                // 配色の Bright は文字の濃さ (0..100)。
                int bright = Math.Clamp(d.Bright, 0, 100);
                var color = Color.FromArgb(bright * 255 / 100, d.Color.R, d.Color.G, d.Color.B);
                using var brush = new SolidBrush(color);
                g.DrawString(d.Text, font, brush,
                    originX + d.X * scale, originY + d.Y * scale - topPad,
                    StringFormat.GenericTypographic);
            }
        }
        finally
        {
            g.Clip = savedClip;
            g.TextRenderingHint = savedMode;
        }
    }

    // cellHeight（出力画素）に収まるフォントを作る。topPad は、GDI+ が
    // 行ボックスの上端を基準に描くぶんのずれ。
    private Font? MakeFont(float cellHeight, out float topPad)
    {
        topPad = 0;
        if (_family == null || cellHeight <= 0) return null;
        try
        {
            const FontStyle style = FontStyle.Regular;
            float em = _family.GetEmHeight(style);
            float ascent = _family.GetCellAscent(style);
            float descent = _family.GetCellDescent(style);
            float lineSpacing = _family.GetLineSpacing(style);
            if (em <= 0 || ascent + descent <= 0) return null;

            float emSize = cellHeight * em / (ascent + descent);
            if (emSize < 1f) emSize = 1f;
            topPad = (lineSpacing - (ascent + descent)) * emSize / em / 2f;
            return new Font(_family, emSize, style, GraphicsUnit.Pixel);
        }
        catch
        {
            return null;
        }
    }

    private void Release()
    {
        _family?.Dispose();
        _family = null;
        _collection?.Dispose();
        _collection = null;
    }

    public void Dispose() => Release();
}
