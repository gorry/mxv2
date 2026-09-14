// mxv2 スキンエディタ - 起動時のスキン一覧。開く・新規作成・終了。

using System.Drawing;
using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class SkinListForm : Form
{
    private DevAssetRoot? _root;
    private readonly ListBox _list;
    // 認識したフォルダの説明。Label ではなく読み取り専用の TextBox。
    // ユーザーフォルダモードでは長いパス（空白が無い）を出すので、Label の
    // 単語単位の折り返しでは右で切れて残りが見えない（実測で踏んだ）。
    // TextBox の WordWrap は長い語も文字単位で折り返す。
    private readonly TextBox _rootLabel;
    // 初期サイズを文面から決めたか（ApplyDetection の最初の 1 回だけ）。
    private bool _sized;
    private readonly Button _openButton;
    private readonly Button _newButton;

    // detection は Program.Main が起動フォルダから決めたもの（NotFound は
    // ここへ来る前に Program.Main が知らせて終了する）。
    public SkinListForm(AssetRootDetection detection)
    {
        _detection = detection;
        Text = "mxv2 スキンエディタ";
        // SkinEditForm と同じ理由で Dpi.S() を使う（AutoScaleMode はこの環境
        // では効かなかった）。
        // 大きさは ApplyDetection が文面の幅から決め直す（下）。ここは仮の値。
        Width = Dpi.S(this, 640);
        Height = Dpi.S(this, 480);
        StartPosition = FormStartPosition.CenterScreen;

        _rootLabel = new TextBox
        {
            Dock = DockStyle.Top,
            Height = Dpi.S(this, 64),
            Text = "",
            ReadOnly = true,
            Multiline = true,
            WordWrap = true,
            BorderStyle = BorderStyle.None,
            TabStop = false,
            BackColor = SystemColors.Control,
        };
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
        // モード（開発フォルダ / ユーザーフォルダ）は起動したフォルダで一律に
        // 決まる（Program.Main）。[フォルダを選ぶ…] は廃止した（ユーザーの指示）。
        var rescan = new Button { Text = "スキン一覧の再読み込み", AutoSize = true, MinimumSize = new Size(Dpi.S(this, 80), 0) };
        // 名前・版・ビルド日付・著作権（Profile.ini 由来。Model/AppProfile.cs）。
        var about = new Button { Text = "バージョン情報…", AutoSize = true, MinimumSize = new Size(Dpi.S(this, 80), 0) };
        var exit = new Button { Text = "終了", AutoSize = true, MinimumSize = new Size(Dpi.S(this, 80), 0) };
        _openButton.Click += (_, _) => OpenSelected();
        // Enter で [開く]（ユーザーの指示）。一覧で選んで Enter だけで入れる。
        AcceptButton = _openButton;
        _newButton.Click += (_, _) => CreateNew();
        rescan.Click += (_, _) => Reload();
        about.Click += (_, _) => MessageBox.Show(this, AppProfile.AboutText, "バージョン情報",
            MessageBoxButtons.OK, MessageBoxIcon.Information);
        exit.Click += (_, _) => Close();
        buttons.Controls.Add(_openButton);
        buttons.Controls.Add(_newButton);
        buttons.Controls.Add(rescan);
        buttons.Controls.Add(about);
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

    // 起動時の判定結果。モードは起動後に変わらないので、[スキン一覧の
    // 再読み込み] は同じ Root で一覧を読み直すだけ。
    private readonly AssetRootDetection _detection;

    // select は読み直したあとに選び直す名前。省略時は読み直す前に選んでいたもの
    // （編集画面を閉じて戻ってきたとき、カーソルが先頭へ戻らないように。
    // 2026-09-15、ユーザーの指摘）。
    private void Reload(string? select = null) => ApplyDetection(_detection, select);

    private void ApplyDetection(AssetRootDetection detection, string? select = null)
    {
        // Items.Clear() より前に読むこと。消したあとでは SelectedItem は常に
        // null で、選び直しが効かない（実際にそうなっていた）。
        string? before = select ?? _list.SelectedItem as string;
        // TextBox は "\n" だけでは改行しない（"\r\n" が要る）。
        _rootLabel.Text = detection.Message.Replace("\r\n", "\n").Replace("\n", Environment.NewLine);
        // ウィンドウの初期サイズは「横幅 = 文面の（いちばん長い行の）幅の 1.2 倍、
        // 縦幅 = 横幅の 9/16」（ユーザーの指示）。起動時の 1 回だけで、
        // [スキン一覧の再読み込み] では変えない（ユーザーが変えた大きさを
        // 勝手に戻さない）。
        if (!_sized)
        {
            _sized = true;
            int widest = 0;
            foreach (var line in detection.Message.Split('\n'))
            {
                var w = TextRenderer.MeasureText(line.TrimEnd('\r'), _rootLabel.Font).Width;
                if (w > widest) widest = w;
            }
            int width = Math.Max(Dpi.S(this, 400), (int)(widest * 1.2));
            Width = width;
            Height = width * 9 / 16;
        }
        // 文面の行数はモードとパスの長さで変わる（ユーザーフォルダモードは
        // 編集先のフォルダも出す）。決め打ちの高さだと 3 行目以降が隠れる
        // （実測で踏んだ）ので、折り返した最後の文字の位置から高さを決める。
        if (_rootLabel.Text.Length > 0)
        {
            var last = _rootLabel.GetPositionFromCharIndex(_rootLabel.Text.Length - 1);
            _rootLabel.Height = last.Y + _rootLabel.Font.Height + Dpi.S(this, 12);
        }
        _list.Items.Clear();

        // 開発フォルダモードでも mxv2.exe の隣（ユーザーフォルダモード）でも
        // 同じ一覧画面。違いは Root が「編集できるスキンの置き場所」を
        // どこにするか（DevAssetRoot）だけ。
        if (detection.Kind == AssetRootKind.NotFound || detection.Root == null)
        {
            _root = null;
            _openButton.Enabled = false;
            _newButton.Enabled = false;
            return;
        }

        _root = detection.Root;
        // 読み直す前に選んでいたもの（または指定された名前）を選び直す。
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
            var doc = dlg.CopyFromBase && dlg.BaseSkinRef != null
                ? SkinDocument.CreateNewAsCopy(_root, dlg.SkinName, dlg.BaseSkinRef)
                : SkinDocument.CreateNew(_root, dlg.SkinName, dlg.BaseSkinRef);
            using var form = new SkinEditForm(doc);
            form.ShowDialog(this);
            Reload(dlg.SkinName);  // 作ったスキンを選んだ状態で戻る
        }
        catch (Exception ex)
        {
            MessageBox.Show($"作成できませんでした: {ex.Message}", "エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }
}
