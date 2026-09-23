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
//   本体: 実際に描かれる範囲 (textAsc - textDesc) が cellHeight に収まる倍率
//         （src/textrender.cpp の MeasureTextExtent）。hhea の ascent/descent は
//         行間のぶん余裕があり、そのまま使うと字が 7 割ほどに縮む
//   ここ: 同じ考え方で、**見本を焼いてインクの上下を実測**し、その高さが
//         cellHeight になる em サイズにする（GDI+ からフォントの字形の
//         範囲は引けないので測る）。測れなければ ascent+descent へ退避
// どちらも「実際に描かれる上下が行の高さになる」ので、同じ大きさの字になる。

using System.Drawing;
using System.Drawing.Text;
using SkinEditor.Model.Render;

namespace SkinEditor.UI;

public sealed class PreviewTextLayer : IDisposable
{
    // インクの実測に使う見本。上下に出っ張る字（約物・下付き・濁点）を入れる。
    // **どの和文フォントにもある字だけにすること。** 無い字は GDI+ が別の
    // フォントへ逃がす（または豆腐を出す）ので、測り値が他人のものになる。
    // 半角カナや半角濁点は持たないフォントがあるので入れない。
    private const string kInkProbe = "AQgjy|(){}[]0123 漢字あぁゐばンヴ「」、。〜";

    private PrivateFontCollection? _collection;
    private FontFamily? _family;
    private string? _loadedPath;
    private bool _inkMeasured;
    private float _inkTopPerEm;   // 描き出しからインク上端までの距離 / em
    private float _inkSpanPerEm;  // インクの高さ / em（0 なら未測定）

    public bool Available => _family != null;

    // スキンの font.ttf を読む。同じパスなら読み直さない。
    // 同じパスなら読み直さない（毎フレーム呼ばれる）。外部でフォントを
    // 差し替えたときだけ ReloadFont() で読み直させる。
    public void ReloadFont()
    {
        var path = _loadedPath;
        Release();
        _loadedPath = null;
        SetFontFile(path);
    }

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

            MeasureInk();
            if (_inkSpanPerEm > 0f)
            {
                // 実際に描かれる上下が cellHeight ぴったりになる大きさ。
                float inkEm = cellHeight / _inkSpanPerEm;
                if (inkEm < 1f) inkEm = 1f;
                // インクの上端が行の上端に来るよう、描き出しを上へ戻す。
                topPad = _inkTopPerEm * inkEm;
                return new Font(_family, inkEm, style, GraphicsUnit.Pixel);
            }

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

    // 見本を大きく焼いて、インクの上下が描き出し位置からどれだけ離れているかを
    // em に対する比で覚える（本体の MeasureTextExtent に相当）。フォント 1 つに
    // つき 1 回。測れなければ 0 のままにして、呼び出し側が旧来の式へ退避する。
    private void MeasureInk()
    {
        if (_inkMeasured) return;
        _inkMeasured = true;
        if (_family == null) return;

        try
        {
            const float probeEm = 128f;
            using var font = new Font(_family, probeEm, FontStyle.Regular, GraphicsUnit.Pixel);
            int w = (int)(probeEm * 24f);
            int h = (int)(probeEm * 3f);
            using var bmp = new Bitmap(w, h, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(bmp))
            {
                g.Clear(Color.Black);
                g.TextRenderingHint = TextRenderingHint.AntiAlias;
                g.DrawString(kInkProbe, font, Brushes.White, 0f, probeEm,
                    StringFormat.GenericTypographic);
            }

            int top = -1, bottom = -1;
            for (int y = 0; y < h; y++)
            {
                bool ink = false;
                for (int x = 0; x < w; x++)
                {
                    if (bmp.GetPixel(x, y).R > 24) { ink = true; break; }
                }
                if (!ink) continue;
                if (top < 0) top = y;
                bottom = y;
            }
            if (top < 0 || bottom <= top) return;

            // 描き出しは y = probeEm。そこからの距離を em 比で持つ。
            _inkTopPerEm = (top - probeEm) / probeEm;
            _inkSpanPerEm = (bottom + 1 - top) / probeEm;
        }
        catch
        {
            _inkTopPerEm = 0f;
            _inkSpanPerEm = 0f;
        }
    }

    private void Release()
    {
        _family?.Dispose();
        _family = null;
        _collection?.Dispose();
        _collection = null;
        _inkMeasured = false;
        _inkTopPerEm = 0f;
        _inkSpanPerEm = 0f;
    }

    public void Dispose() => Release();
}
