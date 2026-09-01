// mxv2 スキンエディタ - 新規スキン作成ダイアログ。
// 名前と、元にするスキン（無しも選べる）を選ぶ。

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class NewSkinDialog : Form
{
    private readonly TextBox _name;
    private readonly ComboBox _baseCombo;
    private readonly DevAssetRoot _root;

    public string SkinName => _name.Text.Trim();
    public string? BaseSkinName => _baseCombo.SelectedIndex <= 0 ? null : (string)_baseCombo.SelectedItem!;

    public NewSkinDialog(DevAssetRoot root)
    {
        _root = root;
        Text = "新規スキンの作成";
        // SkinEditForm と同じ理由で Dpi.S() を使う（AutoScaleMode はこの環境
        // では効かなかった）。
        Width = Dpi.S(this, 420);
        Height = Dpi.S(this, 200);
        FormBorderStyle = FormBorderStyle.FixedDialog;
        StartPosition = FormStartPosition.CenterParent;
        MaximizeBox = false;
        MinimizeBox = false;

        var nameLabel = new Label { Text = "スキン名", Left = Dpi.S(this, 12), Top = Dpi.S(this, 16), Width = Dpi.S(this, 100) };
        _name = new TextBox { Left = Dpi.S(this, 120), Top = Dpi.S(this, 12), Width = Dpi.S(this, 260) };

        var baseLabel = new Label { Text = "元にするスキン", Left = Dpi.S(this, 12), Top = Dpi.S(this, 52), Width = Dpi.S(this, 100) };
        _baseCombo = new ComboBox
        {
            Left = Dpi.S(this, 120), Top = Dpi.S(this, 48), Width = Dpi.S(this, 260),
            DropDownStyle = ComboBoxStyle.DropDownList,
        };
        _baseCombo.Items.Add("(元にするスキンなし・既定値から作成)");
        foreach (var n in root.ListSkinNames()) _baseCombo.Items.Add(n);
        _baseCombo.SelectedIndex = 0;

        var ok = new Button
        {
            Text = "作成", Left = Dpi.S(this, 200), Top = Dpi.S(this, 120), Width = Dpi.S(this, 80),
            DialogResult = DialogResult.OK,
        };
        var cancel = new Button
        {
            Text = "キャンセル", Left = Dpi.S(this, 290), Top = Dpi.S(this, 120), Width = Dpi.S(this, 90),
            DialogResult = DialogResult.Cancel,
        };
        ok.Click += OnOkClick;

        Controls.Add(nameLabel);
        Controls.Add(_name);
        Controls.Add(baseLabel);
        Controls.Add(_baseCombo);
        Controls.Add(ok);
        Controls.Add(cancel);
        AcceptButton = ok;
        CancelButton = cancel;
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
