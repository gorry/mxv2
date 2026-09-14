// mxv2 スキンエディタ - ツールバーに置く [Skin].Base の切替ドロップダウン。
// 「参照」タブは廃止し、ここへ統合した（ユーザー指示）。
// ドロップダウンで選び直すこと自体が切替の実行になる。「(なし)」を選べば
// 参照をやめる、既存スキン名を選べばそれを Base に設定する、のどちらも
// 確認ダイアログを挟んだうえでその場で保存する（skineditor.md の指示どおり）。

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class BaseRefDropdown : FlowLayoutPanel
{
    private const string NoneItem = "(なし)";

    private readonly SkinDocument _doc;
    private readonly ComboBox _combo;
    // コンボの項目（表示名）と同じ並びの ref。項目 0 は「(なし)」で ref は ""。
    private readonly List<string> _refs = new();
    private bool _suppress;

    public event Action? BaseSwitched;

    public BaseRefDropdown(SkinDocument doc)
    {
        _doc = doc;

        // 親はツールバーの FlowLayoutPanel なので、自分自身には Dock を
        // 設定しない（FieldEditControl で踏んだのと同じ理由）。
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;
        WrapContents = false;
        Margin = new Padding(Dpi.S(this, 16), 0, 0, 0);

        var label = new Label
        {
            Text = "参照先スキン",
            AutoSize = true,
            Padding = new Padding(0, Dpi.S(this, 10), Dpi.S(this, 4), 0),
        };
        _combo = new ComboBox
        {
            DropDownStyle = ComboBoxStyle.DropDownList,
            Width = Dpi.S(this, 180),
            Margin = new Padding(0, Dpi.S(this, 6), 0, 0),
        };
        _combo.SelectedIndexChanged += OnSelectedIndexChanged;

        Controls.Add(label);
        Controls.Add(_combo);

        RefreshItems();
    }

    public void RefreshItems()
    {
        _suppress = true;
        _combo.Items.Clear();
        _refs.Clear();
        _combo.Items.Add(NoneItem);
        _refs.Add("");
        // 表示は Root.DisplayRef（開発フォルダモードなら名前だけ、ユーザー
        // フォルダモードなら "assets:" の有無で同梱かユーザーかが分かる形）。
        foreach (var r in _doc.ListBaseCandidates())
        {
            _combo.Items.Add(_doc.Root.DisplayRef(r));
            _refs.Add(r);
        }

        _combo.SelectedIndex = Math.Max(0, CurrentIndex());
        _suppress = false;
    }

    // 今の [Skin].Base に当たる項目。無参照なら 0、候補に無ければ -1。
    private int CurrentIndex()
    {
        if (string.IsNullOrEmpty(_doc.BaseRef)) return 0;
        var current = _doc.Root.CanonicalRefOf(_doc.BaseRef);
        for (int i = 1; i < _refs.Count; i++)
        {
            if (string.Equals(_doc.Root.CanonicalRefOf(_refs[i]), current, StringComparison.OrdinalIgnoreCase)) return i;
        }
        return -1;
    }

    private void OnSelectedIndexChanged(object? sender, EventArgs e)
    {
        if (_suppress) return;
        int selectedIdx = _combo.SelectedIndex;
        if (selectedIdx < 0 || selectedIdx == CurrentIndex()) return;
        var selected = (string)_combo.SelectedItem!;
        var selectedRef = _refs[selectedIdx];

        bool ok;
        if (selectedIdx == 0)
        {
            ok = MessageBox.Show(
                "参照をやめます。実行すると、今の実効値（座標・素材ファイル・配色）がすべて自スキン側へコピーされ、" +
                "現在編集中の内容もその場で保存されます。よろしいですか？",
                "参照の切替の確認", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes;
            if (ok) _doc.SwitchBaseOff();
        }
        else
        {
            ok = MessageBox.Show(
                $"参照先を \"{selected}\" にします。実行すると、Base 側と同じ値になった項目は自スキンから間引かれ、" +
                "現在編集中の内容もその場で保存されます。よろしいですか？",
                "参照の切替の確認", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes;
            if (ok) _doc.SwitchBaseOn(selectedRef);
        }

        RefreshItems();  // 実行しなかった場合も、選択を元の値へ戻す
        if (ok) BaseSwitched?.Invoke();
    }
}
