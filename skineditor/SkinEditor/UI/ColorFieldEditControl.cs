// mxv2 スキンエディタ - colors.ini の 1 項目ぶんの入力行。

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class ColorFieldEditControl : Panel
{
    private readonly SkinDocument _doc;
    private readonly ColorFieldDef _field;
    private readonly Button? _swatch;
    private readonly NumericUpDown? _numeric;

    public ColorFieldEditControl(SkinDocument doc, ColorFieldDef field)
    {
        _doc = doc;
        _field = field;

        // 親は FlowLayoutPanel（TopDown）。理由は FieldEditControl と同じで
        // Dock は設定しない。高さと下マージンも同じ理由で余裕を持たせる。
        Height = Dpi.S(this, 34);
        Margin = new Padding(0, 0, 0, Dpi.S(this, 4));

        // 他タブ（FieldEditControl）はチェックボックス分の幅だけラベルが
        // 右にずれる。配色には行ごとの継承チェックボックスは無いが、
        // 見た目の左端をそろえるためにチェックボックスと同じ幅の
        // 空きを入れておく。
        var spacer = new Panel { Width = Dpi.S(this, 24), Dock = DockStyle.Left };
        // 折り返すと値の入力欄と文字の高さが揃わなくなるので、折り返さない
        // ラベルを使う（詳細は SingleLineLabel のコメント）。
        var label = new SingleLineLabel
        {
            Text = field.Label,
            Width = Dpi.S(this, 240),
            Dock = DockStyle.Left,
        };

        if (field.Kind == ColorFieldKind.Color)
        {
            // "#RRGGBB" (7文字) が高 DPI 環境でも全部読めるよう、十分広く取る。
            _swatch = new Button { Width = Dpi.S(this, 130), Dock = DockStyle.Left };
            _swatch.Click += OnSwatchClick;
        }
        else
        {
            _numeric = new NumericUpDown
            {
                Width = Dpi.S(this, 80),
                Dock = DockStyle.Left,
                Minimum = field.IntMin,
                Maximum = field.IntMax,
            };
            _numeric.ValueChanged += OnNumericChanged;
        }

        // Dock=Left は追加順と逆に並ぶので、見た目の左からの順
        // （spacer, label, 値）の逆順で足す（FieldEditControl と同じ流儀）。
        if (_swatch != null) Controls.Add(_swatch); else Controls.Add(_numeric!);
        Controls.Add(label);
        Controls.Add(spacer);

        Refresh();
    }

    private void OnSwatchClick(object? sender, EventArgs e)
    {
        var current = _field.GetColor!(_doc.EffectiveColors);
        using var dlg = new ColorDialog { Color = current.ToDrawingColor(), FullOpen = true };
        if (dlg.ShowDialog() != DialogResult.OK) return;
        var picked = new RgbColor(dlg.Color.R, dlg.Color.G, dlg.Color.B);
        _doc.MutateColors(c => _field.SetColor!(c, picked));
        Refresh();
    }

    private void OnNumericChanged(object? sender, EventArgs e)
    {
        if (_numeric == null) return;
        int v = (int)_numeric.Value;
        if (v == _field.GetInt!(_doc.EffectiveColors)) return;
        _doc.MutateColors(c => _field.SetInt!(c, v));
    }

    public new void Refresh()
    {
        if (_swatch != null)
        {
            var c = _field.GetColor!(_doc.EffectiveColors);
            _swatch.BackColor = c.ToDrawingColor();
            _swatch.Text = $"#{c.R:X2}{c.G:X2}{c.B:X2}";
            _swatch.ForeColor = (c.R + c.G + c.B) / 3 < 128 ? System.Drawing.Color.White : System.Drawing.Color.Black;
        }
        if (_numeric != null)
        {
            int v = Math.Clamp(_field.GetInt!(_doc.EffectiveColors), (int)_numeric.Minimum, (int)_numeric.Maximum);
            if (_numeric.Value != v) _numeric.Value = v;
        }
    }
}
