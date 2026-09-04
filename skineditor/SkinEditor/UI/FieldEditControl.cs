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
    private readonly SingleLineLabel? _suffixLabel;
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
            // ComponentMin/ComponentMax（x,y,w,h で範囲が違う Xywh 項目向け）が
            // あればそれを、無ければ IntMin/IntMax を全部の値に使う
            // （skineditor_spin_ranges.md でユーザーがレビューした値。
            // 2026-09-05）。
            int min = field.ComponentMin != null && i < field.ComponentMin.Length ? field.ComponentMin[i] : field.IntMin;
            int max = field.ComponentMax != null && i < field.ComponentMax.Length ? field.ComponentMax[i] : field.IntMax;
            // 幅は実際の Min ではなく「Xywh の列としての桁数」で揃える。
            // Rect は x,y が符号付き（5桁）・w,h が符号無し（4桁）の項目が
            // 多いが、素材内位置のように x,y も 0 始まり（符号無し）の項目が
            // 混ざると、同じ Xywh なのに行によって「5,5,4,4桁」「4,4,4,4桁」と
            // 幅がバラバラになり見比べにくい（2026-09-06、ユーザー指摘）。
            // Xywh は x,y(index 0,1) を常に符号付き幅、w,h(index 2,3) を
            // 常に符号無し幅にして列を揃える（クランプに使う実際の
            // Minimum/Maximum は変えない。見た目の幅だけの話）。
            int widthMin = field.Kind == FieldKind.Xywh ? (i < 2 ? -1 : 0) : min;
            var n = new NumericUpDown
            {
                Width = SpinWidth.For(this, widthMin),
                Minimum = min,
                Maximum = max,
                DecimalPlaces = 0,
                Dock = DockStyle.Left,
                Margin = new Padding(0, Dpi.S(this, 3), Dpi.S(this, 4), Dpi.S(this, 3)),
                TextAlign = HorizontalAlignment.Right,
            };
            n.ValueChanged += (_, _) => Commit();
            _numerics[i] = n;
        }

        // 「(x,y)」等の並び順の注記は、ラベルへ埋め込まず数値欄の右へ置く
        // （2026-09-03、ユーザー指示）。Dock=Left は最初に追加したものが
        // 最も右に来るので、数値欄より先に足す。
        if (!string.IsNullOrEmpty(field.Suffix))
        {
            _suffixLabel = new SingleLineLabel
            {
                Text = field.Suffix,
                Width = TextRenderer.MeasureText(field.Suffix, Control.DefaultFont).Width + Dpi.S(this, 8),
                Dock = DockStyle.Left,
                Margin = new Padding(0, 0, 0, 0),
                ForeColor = SystemColors.GrayText,
            };
            Controls.Add(_suffixLabel);
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

        // 「素材内: ...」項目（SizeBoundRole 付き）は、x,y,w,h 全部の Max を
        // 決め打ちの ComponentMax ではなく実際の素材の大きさから計算する
        // （2026-09-06、ユーザー指示）。`SkinDocument.ClipRect`（素材
        // インポート時に同じことをしている）と同じ考え方:
        //   x: 0..素材の横幅-1／y: 0..素材の縦幅-1（矩形の原点は素材内）
        //   w: 1..素材の横幅-現在のx／h: 1..素材の縦幅-現在のy
        //     （w,h の上限は「素材の端までの残り幅」なので x,y に連動する）
        // 素材が見つからない・読めないときは ComponentMax の決め打ち値へ
        // フォールバックする。x,y も w,h も再入力のたびに変わりうるので、
        // 呼ぶたびに測り直す。
        if (_field.SizeBoundRole is { } role && _field.Kind == FieldKind.Xywh && _numerics.Length == 4)
        {
            var size = _doc.ResolveBitmapSize(role);
            int curX = int.TryParse(parts[0], out var xv) ? xv : 0;
            int curY = int.TryParse(parts[1], out var yv) ? yv : 0;
            _numerics[0].Maximum = size != null ? Math.Max(0, size.Value.Width - 1) : _field.ComponentMax![0];
            _numerics[1].Maximum = size != null ? Math.Max(0, size.Value.Height - 1) : _field.ComponentMax![1];
            _numerics[2].Maximum = size != null ? Math.Max(1, size.Value.Width - curX) : _field.ComponentMax![2];
            _numerics[3].Maximum = size != null ? Math.Max(1, size.Value.Height - curY) : _field.ComponentMax![3];
        }
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
