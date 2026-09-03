// mxv2 スキンエディタ - layout.ini の 1 項目ぶんの入力行。
// 先頭のチェックボックスで「継承か / 自スキンの値か」を切り替える。
// OFF＝継承（実効値を表示するが編集不可）、ON＝非継承（編集可能。ON にした
// 瞬間の実効値をそのまま own の値として書き込むので、チェックを入れただけ
// では値は変わらない）。Base が無いスキンでは継承のしようが無いので、
// チェックボックスは常に ON かつ操作不可にする。

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class FieldEditControl : Panel
{
    private readonly SkinDocument _doc;
    private readonly FieldDef _field;
    private readonly CheckBox _check;
    private readonly SingleLineLabel _label;
    private readonly NumericUpDown[] _numerics;
    private bool _suppressCommit;

    public FieldEditControl(SkinDocument doc, FieldDef field)
    {
        _doc = doc;
        _field = field;

        // 親は FlowLayoutPanel（TopDown）なので、ここで Dock を設定すると
        // フロー配置と衝突してサイズ 0 扱いになり、行が丸ごと表示されなく
        // なる不具合があった。Dock は使わず明示サイズだけで渡す。
        Height = Dpi.S(this, 34);
        Margin = new Padding(0, 0, 0, Dpi.S(this, 4));

        _check = new CheckBox { Width = Dpi.S(this, 24), Dock = DockStyle.Left };
        _check.CheckedChanged += OnCheckedChanged;

        // 折り返すと数値入力欄と文字の高さが揃わなくなるので、折り返さない
        // ラベルを使う（詳細は SingleLineLabel のコメント）。
        _label = new SingleLineLabel
        {
            Text = field.Label,
            Width = Dpi.S(this, 240),
            Dock = DockStyle.Left,
        };

        int count = field.Format(doc.Effective).Split(',').Length;
        _numerics = new NumericUpDown[count];
        for (int i = 0; i < count; i++)
        {
            var n = new NumericUpDown
            {
                Width = Dpi.S(this, 72),
                Minimum = -99999,
                Maximum = 99999,
                DecimalPlaces = 0,
                Dock = DockStyle.Left,
                Margin = new Padding(0, Dpi.S(this, 3), Dpi.S(this, 4), Dpi.S(this, 3)),
                TextAlign = HorizontalAlignment.Right,
            };
            n.ValueChanged += (_, _) => Commit();
            _numerics[i] = n;
        }

        // Dock=Left は追加順と逆に並ぶので、見た目の左からの順で逆順に足す。
        for (int i = count - 1; i >= 0; i--) Controls.Add(_numerics[i]);
        Controls.Add(_label);
        Controls.Add(_check);

        Refresh();
    }

    private void Commit()
    {
        if (_suppressCommit) return;
        var joined = string.Join(",", Array.ConvertAll(_numerics, n => ((int)n.Value).ToString()));
        _doc.SetLayoutRaw(_field.Section, _field.Key, joined);
        Refresh();
    }

    private void OnCheckedChanged(object? sender, EventArgs e)
    {
        if (_suppressCommit) return;
        if (_check.Checked)
        {
            // ON にした瞬間の実効値（＝今まで継承していた値）をそのまま
            // own の値として書き込む。値そのものは変えない。
            _doc.SetLayoutRaw(_field.Section, _field.Key, _field.Format(_doc.Effective));
        }
        else
        {
            _doc.RevertLayout(_field.Section, _field.Key);
        }
        Refresh();
    }

    public new void Refresh()
    {
        bool hasBase = !string.IsNullOrEmpty(_doc.BaseRef);
        bool own = _doc.IsLayoutOwn(_field.Section, _field.Key);

        _suppressCommit = true;
        _check.Checked = hasBase ? own : true;
        // ReadOnly は継承チェックボックスごと触らせない（値は見えるが変更不可）。
        _check.Enabled = hasBase && !_field.ReadOnly;

        var parts = _field.Format(_doc.Effective).Split(',');
        for (int i = 0; i < _numerics.Length && i < parts.Length; i++)
        {
            if (int.TryParse(parts[i], out var v))
            {
                v = Math.Clamp(v, (int)_numerics[i].Minimum, (int)_numerics[i].Maximum);
                if (_numerics[i].Value != v) _numerics[i].Value = v;
            }
            _numerics[i].Enabled = _check.Checked && !_field.ReadOnly;
        }
        _suppressCommit = false;
    }
}
