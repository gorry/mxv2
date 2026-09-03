// mxv2 スキンエディタ - 新規スキン作成ダイアログ。
// 名前と、元にするスキン（既存スキンから必ず選ぶ）、それを「参照する」か
// 「コピーして新規スキンにする」かを選ぶ（ユーザー指示で「元にするスキン
// なし・既定値から作成」は廃止した）。

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class NewSkinDialog : Form
{
    private readonly TextBox _name;
    private readonly ComboBox _baseCombo;
    private readonly RadioButton _referenceRadio;
    private readonly RadioButton _copyRadio;
    private readonly DevAssetRoot _root;

    public string SkinName => _name.Text.Trim();

    // 既存スキンが 1 つも無いときだけ null（この場合は既定値から作る
    // 以外に選びようが無いので、コンボ・ラジオごと無効化してある）。
    public string? BaseSkinName => _baseCombo.Items.Count == 0 ? null : (string)_baseCombo.SelectedItem!;
    public bool CopyFromBase => _copyRadio.Checked;

    public NewSkinDialog(DevAssetRoot root)
    {
        _root = root;
        Text = "新規スキンの作成";
        // SkinEditForm と同じ理由で Dpi.S() を使う（AutoScaleMode はこの環境
        // では効かなかった）。元のコードは Form.Height（枠・タイトルバーを
        // 含む外側の高さ）にクライアント領域のつもりの数値を入れていたため、
        // ボタン行が枠の外へはみ出て下端が切れていた。ClientSize なら
        // 枠・タイトルバーの実寸を推測せずに済むので、行の高さの合計から
        // 直接決める（後段で ClientSize を設定する）。
        FormBorderStyle = FormBorderStyle.FixedDialog;
        StartPosition = FormStartPosition.CenterParent;
        MaximizeBox = false;
        MinimizeBox = false;
        // ラベルは折り返すと 2 行目が箱の外に出て消えるので SingleLineLabel を
        // 使う（"元にするスキン" は Label だと既定 Width=100 に収まらず、
        // 折り返して "元にするス" だけが見える形で欠けていた）。
        // fieldWidth はスキン名の候補（今の最長は "Default-Midnight"）が
        // コンボの中で見切れない幅で決めた。
        var labelWidth = Dpi.S(this, 160);
        var fieldWidth = Dpi.S(this, 260);
        var rowHeight = Dpi.S(this, 40);
        var margin = Dpi.S(this, 12);

        // 3 行を 1 枚の Panel にまとめ、行の積み順は Dock ではなく Top の
        // 数値で決める（Dock=Top を複数付けると z 順序で積み順が決まり
        // 分かりにくいので避ける）。
        var fieldsPanel = new Panel { Dock = DockStyle.Top, Height = rowHeight * 3 };

        var nameLabel = new SingleLineLabel { Left = margin, Top = 0, Width = labelWidth, Height = rowHeight, Text = "スキン名" };
        _name = new TextBox { Left = margin + labelWidth, Top = Dpi.S(this, 9), Width = fieldWidth };

        var baseLabel = new SingleLineLabel { Left = margin, Top = rowHeight, Width = labelWidth, Height = rowHeight, Text = "元にするスキン" };
        _baseCombo = new ComboBox
        {
            Left = margin + labelWidth, Top = rowHeight + Dpi.S(this, 5), Width = fieldWidth,
            DropDownStyle = ComboBoxStyle.DropDownList,
        };
        foreach (var n in root.ListSkinNames()) _baseCombo.Items.Add(n);
        if (_baseCombo.Items.Count > 0) _baseCombo.SelectedIndex = 0;

        // 参照する/コピーするの選択は、既存スキンが選べているときだけ意味を
        // 持つ（AutoSize な FlowLayoutPanel で幅を決め打ちしない。
        // BaseRefDropdown/SkinListForm のボタン行と同じ理由）。
        var modePanel = new FlowLayoutPanel
        {
            Left = margin + labelWidth, Top = rowHeight * 2 + Dpi.S(this, 4),
            AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink, WrapContents = false,
        };
        _referenceRadio = new RadioButton { Text = "参照する", AutoSize = true, Checked = true, Margin = new Padding(0, 0, Dpi.S(this, 20), 0) };
        _copyRadio = new RadioButton { Text = "コピーして新規スキンにする", AutoSize = true };
        modePanel.Controls.Add(_referenceRadio);
        modePanel.Controls.Add(_copyRadio);

        // 幅はテキスト行とラジオ行のうち広いほうに合わせる。ラジオ行は
        // 「コピーして新規スキンにする」が長く、フィールド行の想定幅
        // (fieldWidth) より必要幅が大きいことがあるため、ボタン行と同じく
        // 決め打ちせず PreferredSize で実測する。高さは後段で確定する。
        var rowContentWidth = Math.Max(fieldWidth, modePanel.PreferredSize.Width);
        Width = margin * 2 + labelWidth + rowContentWidth + Dpi.S(this, 20);

        bool hasCandidate = _baseCombo.Items.Count > 0;
        _baseCombo.Enabled = hasCandidate;
        _referenceRadio.Enabled = hasCandidate;
        _copyRadio.Enabled = hasCandidate;

        fieldsPanel.Controls.Add(nameLabel);
        fieldsPanel.Controls.Add(_name);
        fieldsPanel.Controls.Add(baseLabel);
        fieldsPanel.Controls.Add(_baseCombo);
        fieldsPanel.Controls.Add(modePanel);

        // ボタン行は SkinListForm と同じ「Dock=Bottom + AutoSize な
        // FlowLayoutPanel」。ボタンの既定サイズは DPI・フォントで変わるので、
        // 座標を決め打ちせずここに任せる。
        var buttonsRow = new FlowLayoutPanel
        {
            Dock = DockStyle.Bottom,
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            FlowDirection = FlowDirection.RightToLeft,
            Padding = new Padding(margin),
        };
        var cancel = new Button
        {
            Text = "キャンセル", AutoSize = true, MinimumSize = new Size(Dpi.S(this, 90), 0),
            DialogResult = DialogResult.Cancel,
        };
        var ok = new Button
        {
            Text = "作成", AutoSize = true, MinimumSize = new Size(Dpi.S(this, 80), 0),
            DialogResult = DialogResult.OK,
        };
        // RightToLeft フローは先に追加したものが右端に来るので、右→左の
        // 見た目順（キャンセルが右）にするにはキャンセルを先に追加する。
        buttonsRow.Controls.Add(cancel);
        buttonsRow.Controls.Add(ok);
        ok.Click += OnOkClick;

        Controls.Add(fieldsPanel);
        Controls.Add(buttonsRow);
        AcceptButton = ok;
        CancelButton = cancel;

        // ボタン行の実サイズ（フォント・DPI で変わる）を確定させてから、
        // 行の高さの合計でクライアント領域を決める。
        buttonsRow.PerformLayout();
        ClientSize = new Size(ClientSize.Width, fieldsPanel.Height + buttonsRow.Height);
    }

    private void OnOkClick(object? sender, EventArgs e)
    {
        var err = SkinNameValidator.Validate(SkinName);
        if (err == null && _root.SkinExists(SkinName)) err = "同じ名前のスキンが既にあります。";
        if (err != null)
        {
            MessageBox.Show(err, "作成できません", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            DialogResult = DialogResult.None;
        }
    }
}
