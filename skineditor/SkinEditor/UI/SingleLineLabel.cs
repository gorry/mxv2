// mxv2 スキンエディタ - 折り返さないラベル。
//
// 標準の Label は、テキストが幅に収まらないと自動で折り返す。折り返して
// 2 行になったテキストを TextAlign=Middle* で縦中央に置くと、「2 行ぶんの
// 高さ」が中央寄せされるため、1 行目は行の中央よりかなり上に描かれ、
// 2 行目は行の高さからはみ出て見えなくなる。
// その結果、同じ行に並ぶ他のコントロール（1 行に収まっているラベル、
// ボタン、数値入力）とテキストの高さが揃わない。
//
// 実測例（168dpi・Yu Gothic UI 9pt・行の高さ 50px）:
//   "素材: 鍵盤の下地 (kb0.bmp)" は 1 行だと 265px 必要だが幅は 240px。
//   → 折り返して 2 行 60px になり、描画位置は (50-60)/2 = -5px。
//     一方 "自スキン" は 1 行 30px なので (50-30)/2 = +10px。
//     15px ずれる。さらに 2 行目の "(kb0.bmp)" は箱の外へ出て消える。
//
// 項目名は環境（DPI）と文言しだいでいくらでも長くなり得るので、幅を広げる
// 対症療法では防ぎきれない（かつて "素材: 5x7 フォント（ビジュアライザ用）
// (font5x7.bmp)" という項目名があり、行の幅すべてを使っても収まらなかった）。
// そこで折り返しを禁止し、収まらないぶんは "…" で省略する。省略時は元の文字列を
// ツールチップで読めるようにしてある。
//
// なお AutoSize は既定の true のままだと Dock と組み合わせたときに高さが
// 文字ぶんに縮むので、このクラスでは常に false にしている。

namespace SkinEditor.UI;

public sealed class SingleLineLabel : Label
{
    private readonly ToolTip _tip = new();
    private string? _tipText;

    public SingleLineLabel()
    {
        AutoSize = false;
        TextAlign = System.Drawing.ContentAlignment.MiddleLeft;
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing) _tip.Dispose();
        base.Dispose(disposing);
    }

    private TextFormatFlags Flags()
    {
        // 折り返さない・収まらなければ末尾を省略する、が肝。
        // WordBreak を付けないことが折り返し禁止にあたる。
        var flags = TextFormatFlags.SingleLine
            | TextFormatFlags.EndEllipsis
            | TextFormatFlags.NoPrefix
            | TextFormatFlags.PreserveGraphicsClipping;

        flags |= TextAlign switch
        {
            System.Drawing.ContentAlignment.TopLeft or
            System.Drawing.ContentAlignment.TopCenter or
            System.Drawing.ContentAlignment.TopRight => TextFormatFlags.Top,
            System.Drawing.ContentAlignment.BottomLeft or
            System.Drawing.ContentAlignment.BottomCenter or
            System.Drawing.ContentAlignment.BottomRight => TextFormatFlags.Bottom,
            _ => TextFormatFlags.VerticalCenter,
        };
        flags |= TextAlign switch
        {
            System.Drawing.ContentAlignment.TopCenter or
            System.Drawing.ContentAlignment.MiddleCenter or
            System.Drawing.ContentAlignment.BottomCenter => TextFormatFlags.HorizontalCenter,
            System.Drawing.ContentAlignment.TopRight or
            System.Drawing.ContentAlignment.MiddleRight or
            System.Drawing.ContentAlignment.BottomRight => TextFormatFlags.Right,
            _ => TextFormatFlags.Left,
        };
        return flags;
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        // 背景は OnPaintBackground が描いてくれるので、ここでは文字だけ描く。
        // base.OnPaint を呼ぶと標準の（折り返す）描画になってしまうので呼ばない。
        var color = Enabled ? ForeColor : SystemColors.GrayText;
        TextRenderer.DrawText(e.Graphics, Text, Font, ClientRectangle, color, Flags());
        UpdateTooltip();
    }

    // 省略が起きているときだけ、元の文字列をツールチップで補う。
    private void UpdateTooltip()
    {
        var needed = TextRenderer.MeasureText(Text, Font).Width;
        var wanted = needed > ClientRectangle.Width ? Text : "";
        if (wanted == _tipText) return;
        _tipText = wanted;
        _tip.SetToolTip(this, wanted);
    }
}
