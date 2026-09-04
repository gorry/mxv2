// mxv2 スキンエディタ - 「1つのキーにまとまった複数個の数値」を、短い
// ラベル付きで数個ずつ横に並べる行。
//
// layout.ini 側は「N個をカンマ区切りにした1キー」（例: [Keyboard] XOffset
// は13個、[Keyboard] ChannelY は9個）だが、スピンボタンをそのままN個
// 並べただけでは何番目が何を表すか分かりにくい、というユーザー指示で、
// 音名やチャンネル名などのラベル付きで数個ずつの行に分けて表示する。
// 継承のON/OFFはこの行単体ではなくキー全体のチェックボックス1つで
// まとめて行う（SkinEditForm 側。行自体はチェックボックスを持たず、
// Editable で編集可否だけ切り替える）。

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class LabeledValueRow : Panel, IEditableRow
{
    private readonly SkinDocument _doc;
    private readonly string _section;
    private readonly string _key;
    private readonly Func<SkinLayout, int[]> _select;
    private readonly int _startIndex;
    private readonly NumericUpDown[] _numerics;
    private bool _suppressCommit;

    // 複数の行を縦に並べて「列」として見せたいとき（1オクターブの鍵のX の
    // C/C#/D/D#/... など）、行ごとに個別測定した幅を使うと、文字数が違う
    // ラベル（"C" と "C#" など）のせいで列がずれてしまう（実際に踏んだ）。
    // グループ全体のラベル文字列をまとめて測り、その最大幅を全行・全列で
    // 共通に使うことで列を揃える。
    public static int MeasureLabelWidth(Control ctx, IEnumerable<string> labels) =>
        labels.Select(s => TextRenderer.MeasureText(s, Control.DefaultFont).Width).Max() + Dpi.S(ctx, 8);

    public LabeledValueRow(SkinDocument doc, string section, string key, Func<SkinLayout, int[]> select,
        int startIndex, string[] labels, int labelWidth, int min = -9999, int max = 9999)
    {
        _doc = doc;
        _section = section;
        _key = key;
        _select = select;
        _startIndex = startIndex;

        // 親は FlowLayoutPanel（TopDown）なので自分自身には Dock を設定しない
        // （FieldEditControl と同じ理由）。左マージンで、対応するグループの
        // チェックボックスの下にぶら下がっていることを示す（インデント）。
        Height = Dpi.S(this, 34);
        Margin = new Padding(Dpi.S(this, 24), 0, 0, Dpi.S(this, 4));

        _numerics = new NumericUpDown[labels.Length];
        var visualOrder = new List<Control>();
        for (int i = 0; i < labels.Length; i++)
        {
            var label = new SingleLineLabel
            {
                Text = labels[i],
                Width = labelWidth,
                Dock = DockStyle.Left,
            };
            var numeric = new NumericUpDown
            {
                // 60px だと3桁の値（例: 304）でスピンボタンと重なって桁が
                // 切れて見えた（実際に踏んだ）ので広げる。符号が要る
                // （Minimum が負の）項目はさらに広げる（SpinWidth 参照）。
                Width = SpinWidth.For(this, 66, min),
                Minimum = min,
                Maximum = max,
                DecimalPlaces = 0,
                Dock = DockStyle.Left,
                Margin = new Padding(0, Dpi.S(this, 3), Dpi.S(this, 8), Dpi.S(this, 3)),
                TextAlign = HorizontalAlignment.Right,
            };
            numeric.ValueChanged += (_, _) => Commit();
            _numerics[i] = numeric;
            visualOrder.Add(label);
            visualOrder.Add(numeric);
        }

        // Dock=Left は追加順と逆に並ぶので、見た目の左からの順で逆順に足す。
        for (int i = visualOrder.Count - 1; i >= 0; i--) Controls.Add(visualOrder[i]);

        Refresh();
    }

    private void Commit()
    {
        if (_suppressCommit) return;
        var values = (int[])_select(_doc.Effective).Clone();
        for (int i = 0; i < _numerics.Length; i++) values[_startIndex + i] = (int)_numerics[i].Value;
        _doc.SetLayoutRaw(_section, _key, SkinLayoutIo.Join(values));
        Refresh();
    }

    // 対応するグループのチェックボックス（SkinEditForm 側）から呼ばれる。
    public bool Editable
    {
        set { foreach (var n in _numerics) n.Enabled = value; }
    }

    // 数値欄と、キー全体の中での通し番号。1 行に何個か並んでいて、それぞれ
    // 別のアイテム（鍵 1 個・チャンネル 1 段）に対応するので、プレビューの
    // 選択は行ではなく数値欄ごとに結び付ける。
    public IEnumerable<(int Index, Control Ctrl)> ValueControls =>
        _numerics.Select((n, i) => (_startIndex + i, (Control)n));

    public new void Refresh()
    {
        _suppressCommit = true;
        var values = _select(_doc.Effective);
        for (int i = 0; i < _numerics.Length; i++)
        {
            var n = _numerics[i];
            n.Value = Math.Clamp(values[_startIndex + i], (int)n.Minimum, (int)n.Maximum);
        }
        _suppressCommit = false;
    }
}
