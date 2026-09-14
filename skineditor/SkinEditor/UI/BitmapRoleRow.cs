// mxv2 スキンエディタ - 素材ビットマップ 1 役割ぶんのインポート行。
// 各レイアウトタブの最上部に置く（ユーザーの指示で「ビットマップ」専用
// タブは廃止し、対応するタブへ振り分けた）。
// 先頭のチェックボックスは「自スキンにファイルがあるか」を表す。
// OFF→ON: Base 側から見えているファイルをそのまま自スキンへコピーする
// （中身は変わらない。「インポート…」はそのあとで差し替えるのに使う）。
// ON→OFF: 自スキンのファイルは削除せず、自スキンフォルダの "_nouse"
// サブフォルダへ退避してから継承に戻す（ユーザー指示。誤って外しても
// 元の素材が消えないように）。
// 「インポート…」はチェック状態によらず常に使え、実行すると自動的に
// 自スキン扱い（チェック ON）になる。
// TTF フォント (BitmapRole.TtfFont) も同じ規則（本体はフォルダに font.ttf が
// あればそれを使う。layout.ini には書かない）。

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class BitmapRoleRow : Panel
{
    private readonly SkinDocument _doc;
    private readonly BitmapRole _role;
    private readonly PreviewCanvas _preview;
    private readonly CheckBox _check;
    private readonly SingleLineLabel _statusLabel;
    private bool _suppressCommit;

    // shortLabel は「素材: <役割名> (<ファイル名>)」ではなく「素材 (<ファイル名>)」
    // にする指定。太字のグループ見出し（例: 「レベルメータ」）の直後に置くとき、
    // 役割名の重複を避けるために使う（2026-09-04、ユーザー指示）。
    public BitmapRoleRow(SkinDocument doc, BitmapRole role, PreviewCanvas preview, bool shortLabel = false)
    {
        _doc = doc;
        _role = role;
        _preview = preview;

        // 親は FlowLayoutPanel（TopDown）。FieldEditControl と同じ理由で
        // 自分自身には Dock を設定しない（フロー配置と衝突してサイズ 0
        // 扱いになる不具合があった）。
        // この行は「インポート…」ボタンを持つので、他の行（FieldEditControl
        // など）より高さに余裕を持たせる。ボタンは中身（文字＋余白）に最低限
        // 必要な高さがあり、DPI 比率どおりに小さくすると 100% 表示環境で
        // 潰れて見えた（実測済み）。
        Height = Dpi.S(this, 50);
        Margin = new Padding(0, 0, 0, Dpi.S(this, 4));

        // AutoSize=false は、Dock と組み合わせたときの高さを明示するため。
        _check = new CheckBox { Width = Dpi.S(this, 24), AutoSize = false, Dock = DockStyle.Left };
        _check.CheckedChanged += OnCheckedChanged;

        // 標準の Label だと、収まらない項目名が折り返されて 2 行になり、
        // 「自スキン」やボタンと文字の高さが揃わなくなる（詳細は
        // SingleLineLabel のコメント）。折り返さないラベルを使う。
        var label = new SingleLineLabel
        {
            Text = shortLabel
                ? $"素材 ({BitmapRoleInfo.DefaultFileName(role)})"
                : $"素材: {BitmapRoleInfo.DisplayName(role)} ({BitmapRoleInfo.DefaultFileName(role)})",
            Width = Dpi.S(this, 360),
            Dock = DockStyle.Left,
        };
        _statusLabel = new SingleLineLabel
        {
            Width = Dpi.S(this, 60),
            Dock = DockStyle.Left,
        };
        var importButton = new Button { Text = "インポート…", AutoSize = true, Dock = DockStyle.Left };
        importButton.Click += (_, _) => OnImport();

        // Dock=Left は追加順と逆に並ぶので、見た目の左からの順で逆順に足す。
        Controls.Add(importButton);
        Controls.Add(_statusLabel);
        Controls.Add(label);
        Controls.Add(_check);

        RefreshStatus();
    }

    public void RefreshStatus()
    {
        bool hasBase = !string.IsNullOrEmpty(_doc.BaseRef);
        var ownPath = Path.Combine(_doc.OwnDir, BitmapRoleInfo.DefaultFileName(_role));
        bool own = File.Exists(ownPath);

        _suppressCommit = true;
        _check.Checked = hasBase ? own : true;
        _check.Enabled = hasBase;
        _suppressCommit = false;

        _statusLabel.Text = own ? "あり" : (FindInherited() != null ? "参照" : "なし");
        _statusLabel.ForeColor = own ? System.Drawing.Color.Black : System.Drawing.Color.SteelBlue;
    }

    private string? FindInherited()
    {
        var name = BitmapRoleInfo.DefaultFileName(_role);
        foreach (var dir in _doc.AllDirsNearToFar())
        {
            var p = Path.Combine(dir, name);
            if (File.Exists(p)) return p;
        }
        return null;
    }

    private void OnCheckedChanged(object? sender, EventArgs e)
    {
        if (_suppressCommit) return;
        var ownPath = Path.Combine(_doc.OwnDir, BitmapRoleInfo.DefaultFileName(_role));

        if (_check.Checked)
        {
            if (!File.Exists(ownPath))
            {
                var src = FindInherited();
                if (src != null)
                {
                    Directory.CreateDirectory(_doc.OwnDir);
                    File.Copy(src, ownPath, overwrite: false);
                }
            }
        }
        else
        {
            if (File.Exists(ownPath)) NouseFolder.Evacuate(_doc.OwnDir, ownPath);
        }
        _preview.InvalidateBitmaps();
        RefreshStatus();
    }

    private void OnImport()
    {
        using var ofd = new OpenFileDialog
        {
            Title = $"{BitmapRoleInfo.DisplayName(_role)} をインポート",
            Filter = BitmapRoleInfo.IsFont(_role)
                ? "TrueType フォント (*.ttf)|*.ttf"
                : BitmapRoleInfo.IsPaletteDependent(_role)
                    ? "8bpp インデックスカラー BMP (*.bmp)|*.bmp"
                    : "画像ファイル (*.bmp;*.png;*.jpg;*.jpeg)|*.bmp;*.png;*.jpg;*.jpeg",
        };
        if (ofd.ShowDialog() != DialogResult.OK) return;

        var result = BitmapImporter.Import(_doc, _role, ofd.FileName);
        if (!result.Success)
        {
            MessageBox.Show(result.Error, "インポートできません", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        _preview.InvalidateBitmaps();
        RefreshStatus();
    }
}
