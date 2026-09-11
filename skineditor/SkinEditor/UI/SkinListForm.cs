// mxv2 スキンエディタ - 起動時のスキン一覧。開く・新規作成・終了。

using System.Drawing;
using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class SkinListForm : Form
{
    private DevAssetRoot? _root;
    private readonly ListBox _list;
    private readonly Label _rootLabel;
    private readonly Button _openButton;
    private readonly Button _newButton;

    public SkinListForm()
    {
        Text = "mxv2 スキンエディタ";
        // SkinEditForm と同じ理由で Dpi.S() を使う（AutoScaleMode はこの環境
        // では効かなかった）。
        Width = Dpi.S(this, 640);
        Height = Dpi.S(this, 480);
        StartPosition = FormStartPosition.CenterScreen;

        _rootLabel = new Label { Dock = DockStyle.Top, Height = Dpi.S(this, 40), Text = "" };
        _list = new ListBox { Dock = DockStyle.Fill };
        _list.DoubleClick += (_, _) => OpenSelected();

        // 固定 Height + WrapContents=false は、幅が足りないと右端が見切れる
        // （高 DPI で実測済み）。折り返しを許したうえで AutoSize にし、
        // 折り返した分は高さのほうを自動で増やす。
        var buttons = new FlowLayoutPanel
        {
            Dock = DockStyle.Bottom,
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            WrapContents = true,
            Padding = new Padding(Dpi.S(this, 4)),
        };
        _openButton = new Button { Text = "開く", AutoSize = true, MinimumSize = new Size(Dpi.S(this, 80), 0) };
        _newButton = new Button { Text = "新規作成…", AutoSize = true, MinimumSize = new Size(Dpi.S(this, 90), 0) };
        var rescan = new Button { Text = "再読込", AutoSize = true, MinimumSize = new Size(Dpi.S(this, 80), 0) };
        var chooseFolder = new Button { Text = "フォルダを選ぶ…", AutoSize = true, MinimumSize = new Size(Dpi.S(this, 100), 0) };
        var exit = new Button { Text = "終了", AutoSize = true, MinimumSize = new Size(Dpi.S(this, 80), 0) };
        _openButton.Click += (_, _) => OpenSelected();
        // Enter で [開く]（ユーザーの指示）。一覧で選んで Enter だけで入れる。
        AcceptButton = _openButton;
        _newButton.Click += (_, _) => CreateNew();
        rescan.Click += (_, _) => Reload();
        chooseFolder.Click += (_, _) => ChooseFolder();
        exit.Click += (_, _) => Close();
        buttons.Controls.Add(_openButton);
        buttons.Controls.Add(_newButton);
        buttons.Controls.Add(rescan);
        buttons.Controls.Add(chooseFolder);
        buttons.Controls.Add(exit);

        // Fill (_list) を最初に追加する（= Z 順序の最背面に自然に置かれる）。
        // 後から SendToBack() で回すと、初回レイアウト確定前の矩形で
        // 兄弟コントロールに対する描画クリップ領域が固定されてしまい、
        // ListBox の先頭行が永久に再描画されない不具合があった。
        Controls.Add(_list);
        Controls.Add(_rootLabel);
        Controls.Add(buttons);

        Reload();
    }

    private void Reload()
    {
        var detection = DevAssetRoot.DetectDefault();
        ApplyDetection(detection);
    }

    private void ApplyDetection(AssetRootDetection detection)
    {
        _rootLabel.Text = detection.Message;
        _list.Items.Clear();

        if (detection.Kind != AssetRootKind.DevFolder || detection.Root == null)
        {
            _root = null;
            _openButton.Enabled = false;
            _newButton.Enabled = false;
            return;
        }

        _root = detection.Root;
        // 読み直す前に選んでいたものがあれば、それを選び直す。
        string? before = _list.SelectedItem as string;
        foreach (var name in _root.ListSkinNames()) _list.Items.Add(name);
        // 必ずどれかを選択状態にしておく（先頭）。フォーカスの点線枠だけで
        // 未選択だと、[開く] を押しても何も起きない（ユーザーの指摘）。
        if (_list.Items.Count > 0)
        {
            int idx = before == null ? -1 : _list.Items.IndexOf(before);
            _list.SelectedIndex = idx >= 0 ? idx : 0;
        }
        _openButton.Enabled = _list.Items.Count > 0;
        _newButton.Enabled = true;
    }

    private void ChooseFolder()
    {
        using var dlg = new FolderBrowserDialog { Description = "mxv2 本体の CMakeLists.txt があるフォルダを選んでください" };
        if (dlg.ShowDialog() != DialogResult.OK) return;
        ApplyDetection(DevAssetRoot.Detect(new[] { dlg.SelectedPath }));
    }

    private void OpenSelected()
    {
        if (_root == null || _list.SelectedItem is not string name) return;
        try
        {
            var doc = SkinDocument.Open(_root, name);
            using var form = new SkinEditForm(doc);
            form.ShowDialog(this);
            Reload();
        }
        catch (Exception ex)
        {
            MessageBox.Show($"開けませんでした: {ex.Message}", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void CreateNew()
    {
        if (_root == null) return;
        using var dlg = new NewSkinDialog(_root);
        if (dlg.ShowDialog(this) != DialogResult.OK) return;

        try
        {
            var doc = dlg.CopyFromBase && dlg.BaseSkinName != null
                ? SkinDocument.CreateNewAsCopy(_root, dlg.SkinName, dlg.BaseSkinName)
                : SkinDocument.CreateNew(_root, dlg.SkinName, dlg.BaseSkinName);
            using var form = new SkinEditForm(doc);
            form.ShowDialog(this);
            Reload();
        }
        catch (Exception ex)
        {
            MessageBox.Show($"作成できませんでした: {ex.Message}", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }
}
