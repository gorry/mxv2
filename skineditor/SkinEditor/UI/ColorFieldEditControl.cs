// mxv2 スキンエディタ - colors.ini の 1 項目ぶんの入力行。

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class ColorFieldEditControl : Panel
{
    private readonly SkinDocument _doc;
    private readonly ColorFieldDef _field;
    private readonly Button? _swatch;
    private readonly NumericUpDown? _numeric;
    private readonly CheckBox? _toggle;

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
        else if (field.Kind == ColorFieldKind.Bool)
        {
            // 実体は 0/1 の int だが、数値欄ではなく [鍵盤]「押す」などと同じ
            // Appearance.Button のトグルボタンで編集させる（2026-09-04、
            // ユーザー指示）。
            //
            // ここだけ Dock=Left にしない: Dock=Left は幅は自前
            // （AutoSize/MinimumSize）のままでも**高さは親の全高（34px）まで
            // 引き伸ばされる**。他のトグル（[鍵盤]「押す」など）は
            // FlowLayoutPanel の行に置いていて本来の高さ（AutoSize が実際に
            // 決める値。この環境では 32px。MinimumSize の 28px より大きい）
            // のまま収まっているのに対し、ここだけ大きく引き伸ばされた矩形に
            // なり、ネイティブのボタン描画がその中でテキストを正しく中央寄せ
            // できず左下へ寄る不具合を実機で確認した（2026-09-05、ユーザー
            // 報告）。Dock を外し、**AutoSize が実際に決めた高さ**（明示値を
            // 決め打ちしない）で縦中央に置くことで直す。決め打ちすると
            // AutoSize の自然な高さより小さくなり、同じ理由でまた崩れる
            // （一度それで再発させた）。
            _toggle = new CheckBox
            {
                Text = "使う", Appearance = Appearance.Button,
                TextAlign = System.Drawing.ContentAlignment.MiddleCenter,
                AutoSize = true,
                MinimumSize = new System.Drawing.Size(Dpi.S(this, 72), Dpi.S(this, 28)),
            };
            _toggle.CheckedChanged += OnToggleChanged;
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
        if (_swatch != null) Controls.Add(_swatch);
        else if (_toggle != null) Controls.Add(_toggle);
        else Controls.Add(_numeric!);
        Controls.Add(label);
        Controls.Add(spacer);

        if (_toggle != null)
        {
            // 親に足した後でないと、フォントの継承が済んでおらず AutoSize が
            // 正しい高さを返さない（既定フォントのままで測ってしまう）。
            // spacer/label は Dock=Left で確定した固定幅なので、その右端が
            // トグルの本来の X 位置（Dock=Left の並びをそのまま踏襲）。
            _toggle.Left = spacer.Width + label.Width;
            _toggle.Top = (Height - _toggle.Height) / 2;
        }

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

    private void OnToggleChanged(object? sender, EventArgs e)
    {
        if (_toggle == null) return;
        int v = _toggle.Checked ? 1 : 0;
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
        if (_toggle != null)
        {
            bool v = _field.GetInt!(_doc.EffectiveColors) != 0;
            if (_toggle.Checked != v) _toggle.Checked = v;
        }
    }
}
