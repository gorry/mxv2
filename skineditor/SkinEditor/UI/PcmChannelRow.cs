// mxv2 スキンエディタ - 「ステータス」タブの PCM 1ch〜8ch の (x,y) 編集行。
//
// layout.ini の実体は [Status] PcmX / PcmY という「8個をカンマ区切りにした
// 1 キー」だが、チャンネルごとに (x,y) の 2 値として編集できるほうが
// 分かりやすいというユーザー指示で、1 チャンネル = 1 行にしている。
// 値を変えると、そのキーの実効値 8 個ぶんを読み直し、該当チャンネルの
// 場所だけ書き換えてキー全体を書き戻す（他チャンネルの値はそのまま）。
// 継承のON/OFFはチャンネル単位ではなく PCM グループ全体（PcmX/PcmY）の
// チェックボックス 1 つでまとめて行う（このチェックボックスは行の直前に
// 単独で置いてある）ので、この行自体はチェックボックスを持たず、
// Editable で編集可否だけ切り替える。

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class PcmChannelRow : Panel
{
    private readonly SkinDocument _doc;
    private readonly int _index;
    private readonly Label _label;
    private readonly NumericUpDown _x;
    private readonly NumericUpDown _y;
    private bool _suppressCommit;

    public PcmChannelRow(SkinDocument doc, int index)
    {
        _doc = doc;
        _index = index;

        // 親は FlowLayoutPanel（TopDown）なので自分自身には Dock を設定しない
        // （FieldEditControl と同じ理由）。枠は使わず、左マージンで
        // 「PCM の位置」チェックボックスの下にぶら下がっていることを示す
        // （インデント）。「PCM の位置」は 2026-09-04 に「配置（PCM）」
        // サブタブの中へ移った（ユーザー指示）ので、そのサブページの中での
        // 相対的なインデント（24px）で足りる。
        Height = Dpi.S(this, 34);
        Margin = new Padding(Dpi.S(this, 24), 0, 0, Dpi.S(this, 4));

        // 折り返し対策で、他の行と同じ折り返さないラベルを使う
        // （詳細は SingleLineLabel のコメント）。
        _label = new SingleLineLabel
        {
            Text = $"PCM {index + 1}ch (x,y)",
            Width = Dpi.S(this, 236),
            Dock = DockStyle.Left,
        };
        _x = MakeNumeric();
        _y = MakeNumeric();
        _x.ValueChanged += (_, _) => Commit(_x, "PcmX", e => e.pcmXOffset);
        _y.ValueChanged += (_, _) => Commit(_y, "PcmY", e => e.pcmYOffset);

        // Dock=Left は追加順と逆に並ぶので、見た目の左からの順で逆順に足す。
        Controls.Add(_y);
        Controls.Add(_x);
        Controls.Add(_label);

        Refresh();
    }

    private NumericUpDown MakeNumeric() => new()
    {
        Width = Dpi.S(this, 72),
        Minimum = -99999,
        Maximum = 99999,
        DecimalPlaces = 0,
        Dock = DockStyle.Left,
        Margin = new Padding(0, Dpi.S(this, 3), Dpi.S(this, 4), Dpi.S(this, 3)),
        TextAlign = HorizontalAlignment.Right,
    };

    private void Commit(NumericUpDown numeric, string key, Func<SkinLayout, int[]> select)
    {
        if (_suppressCommit) return;
        var values = (int[])select(_doc.Effective).Clone();
        values[_index] = (int)numeric.Value;
        _doc.SetLayoutRaw("Status", key, SkinLayoutIo.Join(values));
        Refresh();
    }

    // PCM グループのチェックボックス（PcmGroupBox）から呼ばれる。
    public bool Editable
    {
        set { _x.Enabled = value; _y.Enabled = value; }
    }

    public new void Refresh()
    {
        _suppressCommit = true;
        _x.Value = Math.Clamp(_doc.Effective.pcmXOffset[_index], (int)_x.Minimum, (int)_x.Maximum);
        _y.Value = Math.Clamp(_doc.Effective.pcmYOffset[_index], (int)_y.Minimum, (int)_y.Maximum);
        _suppressCommit = false;
    }
}
