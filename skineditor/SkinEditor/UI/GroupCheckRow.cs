// mxv2 スキンエディタ - 「1 キーに複数個の値」をまとめて継承 ON/OFF する行。
//
// layout.ini の継承（own か Base 由来か）はキー単位でしか効かないので、
// [Keyboard] XOffset(13個) / [Keyboard] ChannelY(9個) / [Status] PcmX,PcmY(8ch)
// のように「1 キーを複数行に分けて見せている」ところは、行ごとではなく
// グループ全体で 1 つのチェックボックスを持つ。
//
// このチェックボックスは「グループの中の 1 項目」ではなく「グループ全体に
// 対する適用スイッチ」。GroupBox で囲むと境界が強調されすぎる（ユーザー指示）
// ので枠は使わず、ぶら下がる行をインデントするだけで所属を示す。
// 文字を CheckBox.Text に持たせず隣にラベルを別に置くのは他の行と同じ作法
// （CheckBox.Text は Enabled=false で灰色になり、行ごとに見た目が揃わない）。
//
// 2026-09-06 の整理で 1 クラスにまとめた（それまでは SkinEditForm.cs に
// ほぼ同じ内容が 3 か所コピーされ、_suppress フラグも 3 つあった）。

using SkinEditor.Model;

namespace SkinEditor.UI;

// まとめて編集可否を切り替えられる行（LabeledValueRow / PcmChannelRow）。
public interface IEditableRow
{
    bool Editable { set; }
}

public sealed class GroupCheckRow : Panel
{
    private readonly SkinDocument _doc;
    private readonly (string Section, string Key, Func<SkinLayout, string> Value)[] _keys;
    private readonly CheckBox _check;
    private bool _suppress;

    // このチェックボックスにぶら下がる行。Refresh() で編集可否を配る。
    public List<IEditableRow> Rows { get; } = new();

    public GroupCheckRow(SkinDocument doc, string label, int width,
        params (string Section, string Key, Func<SkinLayout, string> Value)[] keys)
    {
        _doc = doc;
        _keys = keys;

        Width = width;
        Height = Dpi.S(this, 24);
        Margin = new Padding(0, 0, 0, Dpi.S(this, 4));

        _check = new CheckBox { Width = Dpi.S(this, 24), AutoSize = false, Dock = DockStyle.Left };
        var text = new SingleLineLabel { Text = label, Width = Dpi.S(this, 240), Dock = DockStyle.Left };
        _check.CheckedChanged += OnCheckedChanged;

        // Dock=Left は追加順と逆に並ぶので、見た目の左からの順で逆順に足す。
        Controls.Add(text);
        Controls.Add(_check);

        Refresh();
    }

    private void OnCheckedChanged(object? sender, EventArgs e)
    {
        if (_suppress) return;
        foreach (var (section, key, value) in _keys)
        {
            // ON にした瞬間の実効値（＝今まで継承していた値）をそのまま own へ
            // 書き込む。値そのものは変えない。SetLayoutRaw / RevertLayout は
            // SkinDocument.Changed を上げるので、画面全体の再描画はフォーム側の
            // ハンドラが面倒を見る（ここから呼び直さない）。
            if (_check.Checked) _doc.SetLayoutRaw(section, key, value(_doc.Effective));
            else _doc.RevertLayout(section, key);
        }
    }

    public new void Refresh()
    {
        bool hasBase = !string.IsNullOrEmpty(_doc.BaseRef);
        // 1 つでも own にあればグループ全体が own（PCM は PcmX/PcmY の 2 キー）。
        bool own = _keys.Any(k => _doc.IsLayoutOwn(k.Section, k.Key));

        _suppress = true;
        _check.Checked = hasBase ? own : true;
        _check.Enabled = hasBase;
        _suppress = false;

        foreach (var row in Rows) row.Editable = _check.Checked;
    }
}
