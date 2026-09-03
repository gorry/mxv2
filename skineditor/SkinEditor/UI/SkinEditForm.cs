// mxv2 スキンエディタ - エディタ本体。左にプレビュー、右にプロパティタブ。

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class SkinEditForm : Form
{
    // どのタブの最上部に、どの素材のインポート行を置くか。
    // 「ビットマップ」専用タブは廃止し、対応するタブへ振り分ける（ユーザー指示）。
    private static readonly Dictionary<string, BitmapRole[]> BitmapRowsByTab = new()
    {
        ["画面"] = new[] { BitmapRole.Back },
        ["鍵盤"] = new[] { BitmapRole.Kb0, BitmapRole.Kb1, BitmapRole.Kb2 },
        // 「レベルメータ」タブは廃止して「ステータス」へ統合した。
        // 「ミニフォント」は一度ここへ統合したが、2026-09-03 に独立したタブへ戻した。
        ["ステータス"] = new[] { BitmapRole.LevelMeter },
        ["ミニフォント"] = new[] { BitmapRole.MiniFont },
        ["バナー"] = new[] { BitmapRole.Banner },
        ["操作ボタン"] = new[] { BitmapRole.PlayKey },
        ["プログレスバー"] = new[] { BitmapRole.ProgressBar },
        ["音量バー"] = new[] { BitmapRole.VolumeBar },
        ["スクロールバー"] = new[] { BitmapRole.ScrollBar },
    };

    // 「配色」単独タブは廃止し、各セクションを対応するレイアウトタブへ
    // 移した（ユーザー指示）。キーは LayoutFieldSchema 側のタブ名、値は
    // ColorFieldSchema.BuildSections() が返す ColorSectionDef.Title。
    private static readonly Dictionary<string, string> ColorSectionByTab = new()
    {
        ["画面"] = "背景 (Back)",
        ["鍵盤"] = "鍵盤 (KB)",
        ["ステータス"] = "ステータス (Status)",
        ["曲名"] = "曲名 (MDXTitle)",
        ["ファイラー"] = "ファイラー (Filer)",
        ["操作ボタン"] = "操作ボタン (PlayKey)",
    };

    private readonly SkinDocument _doc;
    private readonly PreviewCanvas _preview;
    private readonly List<FieldEditControl> _layoutControls = new();
    private readonly List<ColorFieldEditControl> _colorControls = new();
    private readonly List<BitmapRoleRow> _bitmapRows = new();
    private readonly List<PcmChannelRow> _pcmRows = new();
    private CheckBox? _pcmCheck;
    private bool _suppressPcmCommit;
    private readonly List<LabeledValueRow> _kbXOffsetRows = new();
    private CheckBox? _kbXOffsetCheck;
    private bool _suppressKbXOffsetCommit;
    private readonly List<LabeledValueRow> _kbChannelYRows = new();
    private CheckBox? _kbChannelYCheck;
    private bool _suppressKbChannelYCommit;
    private readonly Button _saveButton;
    private readonly BaseRefDropdown _baseRefDropdown;
    private readonly Button _revertColorsButton;

    public SkinEditForm(SkinDocument doc)
    {
        _doc = doc;
        Text = $"スキンエディタ - {doc.Name}";
        // コード中の Width/Height/Margin の数値は、このアプリを作った環境
        // （175% 表示、Control.DeviceDpi で 168）でスクリーンショットを見ながら
        // 調整した値をそのまま書いている。WinForms 標準の AutoScaleMode は
        // この環境では何もスケールしてくれなかった（実測済み）ので、
        // Dpi.S() で Control.DeviceDpi を直接見て自前で拡大縮小する
        // （基準は 96 ではなく 168。100% 表示の環境では約 0.57 倍に縮む）。
        StartPosition = FormStartPosition.CenterScreen;

        // 固定 Height だと高 DPI 環境での実サイズを読み違えて見切れる
        // （実測済み）。AutoSize で中身に合わせて高さを決めさせる。
        var toolbar = new FlowLayoutPanel
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            WrapContents = false,
            Padding = new Padding(Dpi.S(this, 4)),
        };
        _saveButton = new Button
        {
            Text = "保存 (Ctrl+S)", AutoSize = true,
            MinimumSize = new System.Drawing.Size(Dpi.S(this, 100), 0),
        };
        _saveButton.Click += (_, _) => DoSave();
        var dirtyLabel = new Label
        {
            Text = "", AutoSize = true,
            Padding = new Padding(Dpi.S(this, 8), Dpi.S(this, 8), 0, 0), Name = "dirty",
        };
        // 「参照」タブは廃止し、ツールバーのドロップダウンへ統合した。
        _baseRefDropdown = new BaseRefDropdown(doc);
        _baseRefDropdown.BaseSwitched += RefreshAll;
        // 「配色を継承に戻す」ボタンも配色タブから移し、参照先スキンの
        // ドロップダウンの右へ置く。参照先が無い、または own の配色が無い
        // ときは戻す先が無いので無効化する。
        _revertColorsButton = new Button
        {
            Text = "配色を参照先スキンに戻す", AutoSize = true,
            Margin = new Padding(Dpi.S(this, 16), 0, 0, 0),
        };
        _revertColorsButton.Click += (_, _) => { _doc.RevertColorsToInherited(); RefreshAll(); };
        toolbar.Controls.Add(_saveButton);
        toolbar.Controls.Add(dirtyLabel);
        toolbar.Controls.Add(_baseRefDropdown);
        toolbar.Controls.Add(_revertColorsButton);

        var split = new SplitContainer { Dock = DockStyle.Fill };

        // Fill (split) を先に追加する（= Z 順序の最背面に自然に置かれる）。
        // 後から BringToFront()/SendToBack() で回すと、初回レイアウト確定前の
        // 矩形で兄弟コントロールに対する描画クリップ領域が固定されてしまい、
        // 一部の子コントロールが永久に再描画されない不具合があった
        // （SkinListForm の ListBox で踏んだのと同じ原因）。
        Controls.Add(split);
        Controls.Add(toolbar);

        int panel1Width = Dpi.S(this, 640);
        int rowWidth = Dpi.S(this, 620);
        // 各タブの中身（rowWidth の行、FlowLayoutPanel.AutoScroll=true）が
        // 縦に収まりきらないと、その FlowLayoutPanel は縦スクロールバーの
        // ぶんだけ実効の横幅が削られる。既定の Panel2 幅がこれより少し狭く、
        // 横スクロールバーも出てしまっていた（実測して踏んだ）。右パネルの
        // 幅を「rowWidth + 縦スクロールバーの幅ぶん」に固定し、ウィンドウの
        // 既定幅・最小幅の両方をこれに合わせることで、横スクロールバーが
        // 出ない幅を最初から確保する。
        int tabViewWidth = rowWidth + SystemInformation.VerticalScrollBarWidth + Dpi.S(this, 8);

        // SplitContainer.SplitterDistance / Panel1MinSize / Panel2MinSize は
        // どれも「その時点の実際の Width」に対して検証される
        // （範囲外だと例外）。だから先に ClientSize でフォーム（＝ split の
        // Width）を最終サイズに確定させてから、SplitterDistance →
        // Panel2MinSize の順で設定する。SplitterDistance は、コンテナが
        // まだ実サイズを持たない構築直後に指定すると無視・クランプされる
        // （右側パネルが極端に狭くなる不具合の原因だった）ので、これも
        // ClientSize の後で設定する。
        ClientSize = new System.Drawing.Size(
            panel1Width + split.SplitterWidth + tabViewWidth, Dpi.S(this, 800));
        split.SplitterDistance = panel1Width;
        split.Panel2MinSize = tabViewWidth;

        _preview = new PreviewCanvas(doc) { Dock = DockStyle.Fill };
        var previewHost = new Panel { Dock = DockStyle.Fill };
        previewHost.Controls.Add(_preview);
        var stateBar = BuildStateBar();
        previewHost.Controls.Add(stateBar);
        split.Panel1.Controls.Add(previewHost);

        // 状態バーが折り返さずに収まる幅を、プレビュー側パネルの最小幅として
        // 確保する。ウィンドウやスプリッタをそれより狭くできなくすることで、
        // 折り返し（項目が千切れて見える）自体を起こさせない。
        int stateBarMinWidth = stateBar.PreferredSize.Width + Dpi.S(this, 24);
        split.Panel1MinSize = Math.Max(split.Panel1MinSize, stateBarMinWidth);
        MinimumSize = new System.Drawing.Size(
            Size.Width - ClientSize.Width + split.Panel1MinSize + split.SplitterWidth + tabViewWidth,
            Dpi.S(this, 500));

        // タブ数が多いので、矢印での横スクロールではなく複数行で全部並べて見せる。
        var tabs = new TabControl { Dock = DockStyle.Fill, Multiline = true };
        split.Panel2.Controls.Add(tabs);

        var colorSectionsByTitle = ColorFieldSchema.BuildSections().ToDictionary(s => s.Title);

        foreach (var section in LayoutFieldSchema.BuildSections())
        {
            var page = new TabPage(section.Title);
            var flow = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown,
                WrapContents = false, AutoScroll = true,
            };
            if (BitmapRowsByTab.TryGetValue(section.Title, out var roles))
            {
                foreach (var role in roles)
                {
                    var row = new BitmapRoleRow(doc, role, _preview) { Width = rowWidth };
                    _bitmapRows.Add(row);
                    flow.Controls.Add(row);
                }
            }
            foreach (var field in section.Fields)
            {
                var ctrl = new FieldEditControl(doc, field) { Width = rowWidth };
                _layoutControls.Add(ctrl);
                flow.Controls.Add(ctrl);

                // 「1オクターブの鍵のX」(XOffset, 13個) は「位置 (x,y)」の
                // 直後、この位置に音名ラベル付きの 4 行として挿入する
                // （13個並びのスピンボタン列は分かりにくい、というユーザー指示）。
                if (section.Title == "鍵盤" && field.Key == "Pos")
                {
                    // 継承（own/inherited）はキー単位（XOffset 全体）でしか
                    // 効かないので、先頭のチェックボックス 1 つで 4 行ぶん
                    // まとめて ON/OFF する（PCM グループと同じ構造。枠は使わず
                    // インデントで「チェックボックスに属する行」を示す）。
                    var kbXOffsetCheckRow = new Panel
                    {
                        Width = rowWidth,
                        Height = Dpi.S(this, 24),
                        Margin = new Padding(0, 0, 0, Dpi.S(this, 4)),
                    };
                    _kbXOffsetCheck = new CheckBox
                    {
                        Width = Dpi.S(this, 24),
                        AutoSize = false,
                        Dock = DockStyle.Left,
                    };
                    var kbXOffsetCheckLabel = new SingleLineLabel
                    {
                        Text = "1オクターブの鍵のX",
                        Width = Dpi.S(this, 240),
                        Dock = DockStyle.Left,
                    };
                    _kbXOffsetCheck.CheckedChanged += (_, _) =>
                    {
                        if (_suppressKbXOffsetCommit) return;
                        if (_kbXOffsetCheck.Checked)
                        {
                            doc.SetLayoutRaw("Keyboard", "XOffset", SkinLayoutIo.Join(doc.Effective.kbXOffset));
                        }
                        else
                        {
                            doc.RevertLayout("Keyboard", "XOffset");
                        }
                        RefreshAll();
                    };
                    kbXOffsetCheckRow.Controls.Add(kbXOffsetCheckLabel);
                    kbXOffsetCheckRow.Controls.Add(_kbXOffsetCheck);
                    flow.Controls.Add(kbXOffsetCheckRow);

                    // C,C#,D,D# / E,F,F#,G / G#,A,A#,B / オクターブ幅 の4行。
                    // 最後の1個は「オクターブ幅」であって次オクターブの C の
                    // 位置そのものではない（src/skin.h の kbXOffset[12] は
                    // drawscreen.cpp で oct 倍されて使われる倍率）。
                    // 上3行は「C」「D」等の1文字と「C#」等の2文字が混在する。
                    // 行ごとに幅を測ると "C" は "C#" より狭くなり、行をまたいで
                    // 数値欄の列がずれてしまった（実際に踏んだ）。この3行は
                    // ひとまとまりの列として揃えたいので、3行ぶんの音名を
                    // まとめて測った幅を全部の行・全部の列で共通に使う。
                    // 最後の「オクターブ幅」は単独行で列を揃える相手が
                    // いないので、自分の文字列だけで幅を決める。
                    string[] noteLabels = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                    int noteLabelWidth = LabeledValueRow.MeasureLabelWidth(this, noteLabels);
                    int octaveLabelWidth = LabeledValueRow.MeasureLabelWidth(this, new[] { "オクターブ幅" });
                    string[][] noteRows =
                    {
                        new[] { "C", "C#", "D", "D#" },
                        new[] { "E", "F", "F#", "G" },
                        new[] { "G#", "A", "A#", "B" },
                        new[] { "オクターブ幅" },
                    };
                    int idx = 0;
                    foreach (var names in noteRows)
                    {
                        int labelWidth = names.Length > 1 ? noteLabelWidth : octaveLabelWidth;
                        var noteRow = new LabeledValueRow(doc, "Keyboard", "XOffset", e => e.kbXOffset, idx, names, labelWidth)
                            { Width = Dpi.S(this, 566) };
                        _kbXOffsetRows.Add(noteRow);
                        flow.Controls.Add(noteRow);
                        idx += names.Length;
                    }
                }

                // 「各チャンネルのY位置」(ChannelY, 9個) は「Y オフセット」の
                // 直後に、チャンネル名ラベル付きの 3 行（FM1-4／FM5-8／PCM）
                // として挿入する（9個並びのスピンボタン列は分かりにくい、
                // というユーザー指示）。構造は「1オクターブの鍵のX」と同じ
                // （枠なし・チェックボックス1つで全行の継承をまとめる・
                // インデントで所属を示す）。
                if (section.Title == "鍵盤" && field.Key == "YOffset")
                {
                    var kbChannelYCheckRow = new Panel
                    {
                        Width = rowWidth,
                        Height = Dpi.S(this, 24),
                        Margin = new Padding(0, 0, 0, Dpi.S(this, 4)),
                    };
                    _kbChannelYCheck = new CheckBox
                    {
                        Width = Dpi.S(this, 24),
                        AutoSize = false,
                        Dock = DockStyle.Left,
                    };
                    var kbChannelYCheckLabel = new SingleLineLabel
                    {
                        Text = "各チャンネルのY位置",
                        Width = Dpi.S(this, 240),
                        Dock = DockStyle.Left,
                    };
                    _kbChannelYCheck.CheckedChanged += (_, _) =>
                    {
                        if (_suppressKbChannelYCommit) return;
                        if (_kbChannelYCheck.Checked)
                        {
                            doc.SetLayoutRaw("Keyboard", "ChannelY", SkinLayoutIo.Join(doc.Effective.chYOffset));
                        }
                        else
                        {
                            doc.RevertLayout("Keyboard", "ChannelY");
                        }
                        RefreshAll();
                    };
                    kbChannelYCheckRow.Controls.Add(kbChannelYCheckLabel);
                    kbChannelYCheckRow.Controls.Add(_kbChannelYCheck);
                    flow.Controls.Add(kbChannelYCheckRow);

                    // FM1-4／FM5-8 の2行はひとまとまりの列として揃えたいので、
                    // 「1オクターブの鍵のX」と同じ理由でラベル幅をまとめて測る
                    // （FM1〜FM8 は全部同じ文字数だが、念のため他のグループと
                    // 同じ作り方に揃えておく）。PCM は単独行。
                    string[] chLabels = { "FM1", "FM2", "FM3", "FM4", "FM5", "FM6", "FM7", "FM8" };
                    int chLabelWidth = LabeledValueRow.MeasureLabelWidth(this, chLabels);
                    int pcmLabelWidth = LabeledValueRow.MeasureLabelWidth(this, new[] { "PCM" });
                    string[][] channelRows =
                    {
                        new[] { "FM1", "FM2", "FM3", "FM4" },
                        new[] { "FM5", "FM6", "FM7", "FM8" },
                        new[] { "PCM" },
                    };
                    int chIdx = 0;
                    foreach (var names in channelRows)
                    {
                        int labelWidth = names.Length > 1 ? chLabelWidth : pcmLabelWidth;
                        var chRow = new LabeledValueRow(doc, "Keyboard", "ChannelY", e => e.chYOffset, chIdx, names, labelWidth)
                            { Width = Dpi.S(this, 566) };
                        _kbChannelYRows.Add(chRow);
                        flow.Controls.Add(chRow);
                        chIdx += names.Length;
                    }
                }
            }
            if (section.Title == "ステータス")
            {
                // PcmX/PcmY (各8個) は「PCM 1ch」〜「PCM 8ch」の (x,y) 編集行にする
                // （8個並びのスピンボタン列は分かりにくい、というユーザー指示）。
                // 継承（own/inherited）はチャンネルごとではなく PcmX/PcmY という
                // キー単位でしか効かないので、先頭のチェックボックス 1 つで
                // 8 行ぶんまとめて ON/OFF する。このチェックボックスは
                // 「グループの中の1項目」ではなく「グループ全体に対する
                // 適用スイッチ」。GroupBox で囲むと逆にグループの境界が
                // 強調されすぎる（ユーザー指示）ので、枠は使わず、8行を
                // インデントするだけでチェックボックスにぶら下がっている
                // ことを示す。
                // 他の行（FieldEditControl / BitmapRoleRow）は「チェックボックス
                // 自体には文字を持たせず、隣に別のラベルを置く」形になっている。
                // ラベルは Enabled を触らないので、チェックボックスが無効化
                // （Base 無し＝常に own）されても文字は灰色にならない。
                // ここも Text を直接 CheckBox に持たせず同じ形に合わせる
                // （CheckBox.Text は Enabled=false で自動的に灰色になり、
                // 他の行と見た目が揃わなかった）。
                var pcmCheckRow = new Panel
                {
                    Width = rowWidth,
                    Height = Dpi.S(this, 24),
                    Margin = new Padding(0, 0, 0, Dpi.S(this, 4)),
                };
                _pcmCheck = new CheckBox
                {
                    Width = Dpi.S(this, 24),
                    AutoSize = false,
                    Dock = DockStyle.Left,
                };
                var pcmCheckLabel = new SingleLineLabel
                {
                    Text = "PCM の位置",
                    Width = Dpi.S(this, 240),
                    Dock = DockStyle.Left,
                };
                _pcmCheck.CheckedChanged += (_, _) =>
                {
                    if (_suppressPcmCommit) return;
                    if (_pcmCheck.Checked)
                    {
                        doc.SetLayoutRaw("Status", "PcmX", SkinLayoutIo.Join(doc.Effective.pcmXOffset));
                        doc.SetLayoutRaw("Status", "PcmY", SkinLayoutIo.Join(doc.Effective.pcmYOffset));
                    }
                    else
                    {
                        doc.RevertLayout("Status", "PcmX");
                        doc.RevertLayout("Status", "PcmY");
                    }
                    RefreshAll();
                };
                pcmCheckRow.Controls.Add(pcmCheckLabel);
                pcmCheckRow.Controls.Add(_pcmCheck);
                flow.Controls.Add(pcmCheckRow);

                for (int i = 0; i < 8; i++)
                {
                    var row = new PcmChannelRow(doc, i) { Width = Dpi.S(this, 566) };
                    _pcmRows.Add(row);
                    flow.Controls.Add(row);
                }
            }

            // 「配色」単独タブは廃止したので、対応するセクションがあれば
            // このタブの末尾（レイアウト項目の後）へ続けて置く。
            if (ColorSectionByTab.TryGetValue(section.Title, out var colorTitle) &&
                colorSectionsByTitle.TryGetValue(colorTitle, out var colorSection))
            {
                var head = new Label
                {
                    Text = "配色", Width = rowWidth, Height = Dpi.S(this, 22),
                    Font = new System.Drawing.Font(Font, System.Drawing.FontStyle.Bold),
                    Margin = new Padding(0, Dpi.S(this, 12), 0, Dpi.S(this, 4)),
                };
                flow.Controls.Add(head);
                foreach (var field in colorSection.Fields)
                {
                    var ctrl = new ColorFieldEditControl(doc, field) { Width = rowWidth };
                    _colorControls.Add(ctrl);
                    flow.Controls.Add(ctrl);
                }
            }

            page.Controls.Add(flow);
            tabs.TabPages.Add(page);
        }

        _preview.PartActivated += title =>
        {
            for (int i = 0; i < tabs.TabPages.Count; i++)
            {
                if (tabs.TabPages[i].Text == title) { tabs.SelectedIndex = i; break; }
            }
        };

        doc.Changed += RefreshAll;
        KeyPreview = true;
        KeyDown += (_, e) => { if (e.Control && e.KeyCode == Keys.S) DoSave(); };
        FormClosing += OnFormClosing;

        RefreshAll();
    }

    private Panel BuildStateBar()
    {
        // 折り返すと項目が中途半端に千切れて見えるため、折り返さない。
        // その代わり、必要な最小幅を呼び出し側（split.Panel1MinSize /
        // フォームの MinimumSize）で確保して、ウィンドウを狭めても
        // 折り返しが起きないようにする。
        var bar = new FlowLayoutPanel
        {
            Dock = DockStyle.Bottom,
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false,
            Padding = new Padding(Dpi.S(this, 4)),
        };
        var checkSize = new System.Drawing.Size(0, Dpi.S(this, 32));
        var play = new CheckBox { Text = "PLAY", Checked = _preview.StatePlay, AutoSize = true, MinimumSize = checkSize };
        var cont = new CheckBox { Text = "CONT", Checked = _preview.StateCont, AutoSize = true, MinimumSize = checkSize };
        var pause = new CheckBox { Text = "PAUSE", AutoSize = true, MinimumSize = checkSize };
        var repeat = new CheckBox { Text = "REPEAT", AutoSize = true, MinimumSize = checkSize };
        play.CheckedChanged += (_, _) => { _preview.StatePlay = play.Checked; _preview.Invalidate(); };
        cont.CheckedChanged += (_, _) => { _preview.StateCont = cont.Checked; _preview.Invalidate(); };
        pause.CheckedChanged += (_, _) => { _preview.StatePause = pause.Checked; _preview.Invalidate(); };
        repeat.CheckedChanged += (_, _) => { _preview.StateRepeat = repeat.Checked; _preview.Invalidate(); };

        var volLabel = new Label { Text = "音量", AutoSize = true, Padding = new Padding(Dpi.S(this, 8), Dpi.S(this, 6), 0, 0) };
        var vol = new TrackBar { Minimum = -100, Maximum = 100, Value = 0, Width = Dpi.S(this, 100) };
        vol.ValueChanged += (_, _) => { _preview.StateVolume = vol.Value; _preview.Invalidate(); };

        var progLabel = new Label { Text = "進捗", AutoSize = true, Padding = new Padding(Dpi.S(this, 8), Dpi.S(this, 6), 0, 0) };
        var prog = new TrackBar { Minimum = 0, Maximum = 100, Value = 40, Width = Dpi.S(this, 100) };
        prog.ValueChanged += (_, _) => { _preview.StateProgress = prog.Value / 100.0; _preview.Invalidate(); };

        bar.Controls.Add(play);
        bar.Controls.Add(cont);
        bar.Controls.Add(pause);
        bar.Controls.Add(repeat);
        bar.Controls.Add(volLabel);
        bar.Controls.Add(vol);
        bar.Controls.Add(progLabel);
        bar.Controls.Add(prog);
        return bar;
    }

    private void RefreshAll()
    {
        foreach (var c in _layoutControls) c.Refresh();
        foreach (var c in _colorControls) c.Refresh();
        foreach (var r in _bitmapRows) r.RefreshStatus();
        foreach (var r in _pcmRows) r.Refresh();
        if (_pcmCheck != null)
        {
            bool hasBase = !string.IsNullOrEmpty(_doc.BaseRef);
            bool own = _doc.IsLayoutOwn("Status", "PcmX") || _doc.IsLayoutOwn("Status", "PcmY");
            _suppressPcmCommit = true;
            _pcmCheck.Checked = hasBase ? own : true;
            _pcmCheck.Enabled = hasBase;
            _suppressPcmCommit = false;
            foreach (var r in _pcmRows) r.Editable = _pcmCheck.Checked;
        }
        foreach (var r in _kbXOffsetRows) r.Refresh();
        if (_kbXOffsetCheck != null)
        {
            bool hasBase = !string.IsNullOrEmpty(_doc.BaseRef);
            bool own = _doc.IsLayoutOwn("Keyboard", "XOffset");
            _suppressKbXOffsetCommit = true;
            _kbXOffsetCheck.Checked = hasBase ? own : true;
            _kbXOffsetCheck.Enabled = hasBase;
            _suppressKbXOffsetCommit = false;
            foreach (var r in _kbXOffsetRows) r.Editable = _kbXOffsetCheck.Checked;
        }
        foreach (var r in _kbChannelYRows) r.Refresh();
        if (_kbChannelYCheck != null)
        {
            bool hasBase = !string.IsNullOrEmpty(_doc.BaseRef);
            bool own = _doc.IsLayoutOwn("Keyboard", "ChannelY");
            _suppressKbChannelYCommit = true;
            _kbChannelYCheck.Checked = hasBase ? own : true;
            _kbChannelYCheck.Enabled = hasBase;
            _suppressKbChannelYCommit = false;
            foreach (var r in _kbChannelYRows) r.Editable = _kbChannelYCheck.Checked;
        }
        _baseRefDropdown.RefreshItems();
        _revertColorsButton.Enabled = _doc.HasOwnColors && !string.IsNullOrEmpty(_doc.BaseRef);
        Text = $"スキンエディタ - {_doc.Name}{(_doc.IsDirty ? " *" : "")}";
        _saveButton.Enabled = _doc.IsDirty;
    }

    private void DoSave()
    {
        _doc.Save();
        RefreshAll();
    }

    private void OnFormClosing(object? sender, FormClosingEventArgs e)
    {
        if (!_doc.IsDirty) return;
        var result = MessageBox.Show(
            "編集中の内容があります。保存しますか？",
            "確認", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Warning);
        if (result == DialogResult.Cancel) { e.Cancel = true; return; }
        if (result == DialogResult.Yes) _doc.Save();
    }
}
