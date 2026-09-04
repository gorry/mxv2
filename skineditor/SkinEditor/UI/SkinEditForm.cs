// mxv2 スキンエディタ - エディタ本体。左にプレビュー、右にプロパティタブ。

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class SkinEditForm : Form
{
    // 素材のインポート行は、どのタブ／どのグループのものかという対応表では
    // 持たない。「たまたまこのページのこの項目の隣にある」というだけの
    // 位置情報として、各項目（FieldDef）の LeadingBitmaps / TrailingBitmaps
    // に直接持たせてある（Model/LayoutFieldSchema.cs 参照。2026-09-04、
    // ユーザー指示。以前はここに BitmapRowsByTab / GroupBitmapRole という
    // 「タブ名／グループ名 → 素材」の対応表があった）。

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

    // プレビューの「選択」対象。**登録順が仕様書の並び順**で、優先順位 H が
    // 同じときはこの順で先に書いたものが勝つ（skineditor_hitcheck.md）。
    private sealed class PreviewTarget
    {
        public Control Ctrl = null!;             // フォーカスを持つコントロール
        public PreviewBinding Binding = null!;   // 対応するアイテムと H
        public Action<int, int>? Move;           // ドラッグでの書き戻し（絶対座標）
    }

    private readonly List<PreviewTarget> _previewTargets = new();
    private PreviewTarget? _selectedTarget;

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
        // 620 だと「(x,y,w,h)」サフィックスの右端が切れる項目があった
        // （バナー等の矩形項目。2026-09-04、ユーザー報告）ので広げてある。
        int rowWidth = Dpi.S(this, 670);
        // 各タブの中身（rowWidth の行、FlowLayoutPanel.AutoScroll=true）が
        // 縦に収まりきらないと、その FlowLayoutPanel は縦スクロールバーの
        // ぶんだけ実効の横幅が削られる。既定の Panel2 幅がこれより少し狭く、
        // 横スクロールバーも出てしまっていた（実測して踏んだ）。右パネルの
        // 幅を「rowWidth + 縦スクロールバーの幅ぶん」に固定し、ウィンドウの
        // 既定幅・最小幅の両方をこれに合わせることで、横スクロールバーが
        // 出ない幅を最初から確保する。
        int tabViewWidth = rowWidth + SystemInformation.VerticalScrollBarWidth + Dpi.S(this, 8);

        // 右側（タブページ）の幅は「横スクロールバーが出ない幅」ちょうどに
        // 固定し、ウィンドウをどう広げても縮めても変えない（2026-09-04、
        // ユーザー指示：最小サイズ＝最大サイズにして動かないようにする）。
        // `FixedPanel = Panel2` にすると、コンテナが伸縮したとき Panel2 の
        // 幅は据え置かれ、伸縮ぶんは全部 Panel1（プレビュー）側が吸収する
        // （＝ウィンドウサイズの変更に追従するのはプレビューだけになる）。
        // `IsSplitterFixed = true` はユーザーがスプリッタをドラッグして
        // 動かすこと自体を禁止する（プログラムからの SplitterDistance 設定は
        // 引き続きできる）。
        split.FixedPanel = FixedPanel.Panel2;
        split.IsSplitterFixed = true;

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

        // 音量・進捗スライダは、それぞれ対応するタブ（[音量バー]「音量」/
        // [プログレスバー]「プレイ時間」）へ移したので、プレビューの下に
        // 状態バーを置く必要が無くなった（2026-09-04、ユーザー指示）。
        // プレビュー側パネルはキャンバスだけになる。
        _preview = new PreviewCanvas(doc) { Dock = DockStyle.Fill };
        split.Panel1.Controls.Add(_preview);

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
            // サブタブの中の行は、入れ子の TabControl 自身の枠のぶんだけ、
            // タブ直下（flow）の行より少し狭くしないと横スクロールが出てしまう
            // （実測して踏んだ）。40px 引けば足りる（2026-09-04、「操作ボタン」
            // タブに (x,y,w,h) 付きの項目をサブタブへ入れたときに実測。それまでの
            // 566px 固定は (x,y) までしか無かった頃の値で、Xywh には足りず
            // サフィックスが欠けていた）。
            int RowWidthFor(FlowLayoutPanel target) => ReferenceEquals(target, flow) ? rowWidth : rowWidth - Dpi.S(this, 40);

            // 素材のインポート行 1 つぶんを、渡された flow へ追加する。indented は
            // 他のインデント行（PCM グループなど）と同じ幅・左マージンに揃えるか。
            void AddBitmapRow(FlowLayoutPanel target, BitmapRole role, bool shortLabel, bool indented)
            {
                var row = new BitmapRoleRow(doc, role, _preview, shortLabel: shortLabel) { Width = RowWidthFor(target) };
                if (indented)
                {
                    row.Width = Dpi.S(this, 566);
                    row.Margin = new Padding(Dpi.S(this, 24), 0, 0, Dpi.S(this, 4));
                }
                _bitmapRows.Add(row);
                target.Controls.Add(row);
                RegisterPreview(row, PreviewBindings.ForBitmap(role), null);
            }

            // 1 項目ぶん（LeadingBitmaps → 入力欄 → TrailingBitmaps）を、渡された
            // flow へ描画する。サブタブ（下記）とタブ直下の両方から呼べるように
            // target を引数にしてある。
            void RenderField(FlowLayoutPanel target, FieldDef field, bool indented)
            {
                // この項目の直前に素材行がある、というだけの位置情報
                // （FieldDef.LeadingBitmaps。2026-09-04、ユーザー指示）。
                if (field.LeadingBitmaps != null)
                {
                    foreach (var role in field.LeadingBitmaps) AddBitmapRow(target, role, field.BitmapShortLabel, indented);
                }

                var ctrl = new FieldEditControl(doc, field) { Width = RowWidthFor(target) };
                if (indented)
                {
                    // グループの一員だと分かるよう、他のインデント行（PCM や
                    // 1オクターブの鍵のX など）と同じ幅・左マージンに揃える。
                    ctrl.Width = Dpi.S(this, 566);
                    ctrl.Margin = new Padding(Dpi.S(this, 24), 0, 0, Dpi.S(this, 4));
                }
                _layoutControls.Add(ctrl);
                target.Controls.Add(ctrl);
                var binding = PreviewBindings.For(field.Section, field.Key);
                RegisterPreview(ctrl, binding, MoveActionFor(binding, field.Section, field.Key));

                // この項目の直後に素材行がある、というだけの位置情報
                // （FieldDef.TrailingBitmaps）。「鍵盤」の「位置」のように、
                // 項目の入力欄自体は前へ出したいが、素材行は元の並び順の
                // ままにしたい（＝この項目の直後）ときに使う
                // （2026-09-04、ユーザー指示）。
                if (field.TrailingBitmaps != null)
                {
                    foreach (var role in field.TrailingBitmaps) AddBitmapRow(target, role, field.BitmapShortLabel, indented);
                }
            }

            // FieldDef.SubTab が変わったところで、入れ子の TabControl に
            // ページを作る（「ステータス」タブの「レベルメータ」「配置」
            // 「ピッチLFO」「音量LFO」「PCM」。
            // 2026-09-04、ユーザー指示）。同じ SubTab の項目は同じページへ
            // まとめる。呼び出し側（下の PCM グループ）からも使うので、
            // ループの外に出してある。
            TabControl? subTabs = null;
            var subTabFlows = new Dictionary<string, FlowLayoutPanel>();
            FlowLayoutPanel GetSubTabFlow(string subTabTitle)
            {
                if (subTabFlows.TryGetValue(subTabTitle, out var existingFlow)) return existingFlow;
                if (subTabs == null)
                {
                    subTabs = new TabControl
                    {
                        Width = rowWidth,
                        // 一番項目数が多い「PCM」サブタブ（PCM音量/PCMポインタ
                        // + PCMの位置 + チャンネル行8つ = 11行、約408の高さ）が
                        // スクロールせずに収まる高さ（2026-09-04、ユーザー指示）。
                        Height = Dpi.S(this, 480),
                        Margin = new Padding(0, 0, 0, Dpi.S(this, 4)),
                        // タブ名を縮めたので今は 1 行に収まっているが、
                        // 増えたり長くなったりしたときのために外側のタブと
                        // 同じく複数行で全部見せる指定は残しておく。
                        Multiline = true,
                    };
                    flow.Controls.Add(subTabs);
                }
                var newFlow = new FlowLayoutPanel
                {
                    Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown,
                    WrapContents = false, AutoScroll = true,
                };
                var subPage = new TabPage(subTabTitle);
                subPage.Controls.Add(newFlow);
                subTabs.TabPages.Add(subPage);
                subTabFlows[subTabTitle] = newFlow;
                return newFlow;
            }

            // FieldDef.Group が変わったところに太字の見出しを挟み、その項目を
            // インデントする（タブを割るほどではないまとまりを示す。
            // 2026-09-03、ユーザー指示）。
            string lastGroup = "";
            foreach (var field in section.Fields)
            {
                if (!string.IsNullOrEmpty(field.SubTab))
                {
                    // サブタブのページ自体がまとまりを表すので、その中では
                    // 太字見出し・インデントは使わない。
                    RenderField(GetSubTabFlow(field.SubTab), field, indented: false);
                    continue;
                }

                if (field.Group != lastGroup)
                {
                    lastGroup = field.Group;
                    if (!string.IsNullOrEmpty(field.Group))
                    {
                        flow.Controls.Add(new SingleLineLabel
                        {
                            Text = field.Group, Width = rowWidth, Height = Dpi.S(this, 22),
                            Font = new System.Drawing.Font(Font, System.Drawing.FontStyle.Bold),
                            Margin = new Padding(0, Dpi.S(this, 12), 0, Dpi.S(this, 4)),
                        });
                    }
                }

                RenderField(flow, field, indented: !string.IsNullOrEmpty(field.Group));

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
                    RegisterPreview(kbXOffsetCheckRow,
                        new PreviewBinding(PreviewRegions.Ids.KeyboardOctave), null);

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
                        // 数値欄 1 個が鍵 1 個（最後の「オクターブ幅」だけは
                        // オクターブ全体）に対応する。
                        foreach (var (vi, vc) in noteRow.ValueControls)
                            RegisterPreview(vc, PreviewBindings.For("Keyboard", "XOffset", vi), null);
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
                    RegisterPreview(kbChannelYCheckRow,
                        new PreviewBinding(PreviewRegions.Ids.KeyboardAll), null);

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
                        // 数値欄 1 個がチャンネル 1 段の鍵盤に対応する。
                        foreach (var (vi, vc) in chRow.ValueControls)
                            RegisterPreview(vc, PreviewBindings.For("Keyboard", "ChannelY", vi), null);
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
                //
                // 「PCM の位置」は「PCM」を含む項目なので「PCM」サブタブへ
                // 置く（2026-09-04、ユーザー指示）。サブタブ名は
                // LayoutFieldSchema.cs の StatusItems ループの subTab と
                // 一致させること（タブ名を短縮したときにこの文字列だけ
                // 直し忘れると、同じ名前のはずのタブが 2 つ出てしまう）。
                // サブページの中では他の項目（PCM 音量・PCM ポインタ）と
                // 同じくインデント無し、8 行のチャンネル行はその 1 段下
                // （PcmChannelRow 側で 24px）。
                var pcmFlow = GetSubTabFlow("PCM");
                var pcmCheckRow = new Panel
                {
                    Width = RowWidthFor(pcmFlow),
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
                pcmFlow.Controls.Add(pcmCheckRow);
                RegisterPreview(pcmCheckRow, new PreviewBinding(PreviewRegions.Ids.PcmAll), null);

                for (int i = 0; i < 8; i++)
                {
                    // PcmChannelRow は自分の左マージンで 24px インデントを
                    // 持つので、幅はサブタブの基準幅よりそのぶん狭くする
                    // （右端をそろえる）。
                    var row = new PcmChannelRow(doc, i) { Width = RowWidthFor(pcmFlow) - Dpi.S(this, 24) };
                    _pcmRows.Add(row);
                    pcmFlow.Controls.Add(row);
                    RegisterPreview(row, PreviewBindings.For("Status", "PcmX", i), null);
                }

                // 「レベル」スライダー。[レベルメータ] サブタブへ、レベルごとの
                // 点灯具合を確認するために追加する（2026-09-04、ユーザー指示）。
                // layout.ini には何も書かない一時的な状態（PreviewCanvas.
                // StateLevel）。FM 8ch 全段に同じ値を適用する（レベルメータの
                // 項目はどの段にも共通のため、段ごとには分けていない）。
                var levelFlow = GetSubTabFlow("レベルメータ");
                var levelRow = new FlowLayoutPanel
                {
                    FlowDirection = FlowDirection.LeftToRight,
                    WrapContents = false,
                    AutoSize = true,
                    AutoSizeMode = AutoSizeMode.GrowAndShrink,
                    Margin = new Padding(0, Dpi.S(this, 8), 0, 0),
                };
                var levelLabel = new SingleLineLabel
                {
                    Text = "レベル",
                    Width = LabeledValueRow.MeasureLabelWidth(this, new[] { "レベル" }),
                    Height = Dpi.S(this, 24),
                    Margin = new Padding(0, Dpi.S(this, 3), Dpi.S(this, 8), 0),
                };
                var levelSlider = new TrackBar
                {
                    Minimum = 0, Maximum = 100, Value = 0,
                    Width = Dpi.S(this, 150), Margin = new Padding(0),
                };
                levelSlider.ValueChanged += (_, _) => { _preview.StateLevel = levelSlider.Value; _preview.Invalidate(); };
                levelRow.Controls.Add(levelLabel);
                levelRow.Controls.Add(levelSlider);
                levelFlow.Controls.Add(levelRow);
                RegisterPreview(levelRow, new PreviewBinding(PreviewRegions.Ids.LevelMeter), null);
            }
            if (section.Title == "操作ボタン")
            {
                // 各ボタンのサブタブに、プレビュー確認用の「押す」トグルボタンを
                // 追加する（2026-09-04、ユーザー指示）。layout.ini には何も
                // 書かない、プレビューだけの一時的な状態（PreviewCanvas.
                // SetButtonPressed / StateLed*）。LED パレットを持つボタン
                // （PLAY/PAUSE/CONT/REPEAT）には「LED」トグルも並べて置く。
                // これで編集画面下部にあった PLAY/CONT/PAUSE/REPEAT の4チェック
                // は不要になったので廃止した（BuildStateBar 側）。
                string[] buttonNames = { "PREV", "STOP", "PLAY", "FAST", "PAUSE", "NEXT", "CONT", "REPEAT" };
                for (int i = 0; i < buttonNames.Length; i++)
                {
                    int idx = i;
                    var buttonFlow = GetSubTabFlow(buttonNames[idx]);
                    var toggleRow = new FlowLayoutPanel
                    {
                        FlowDirection = FlowDirection.LeftToRight,
                        WrapContents = false,
                        AutoSize = true,
                        AutoSizeMode = AutoSizeMode.GrowAndShrink,
                        Margin = new Padding(0, Dpi.S(this, 8), 0, 0),
                    };
                    var toggleSize = new System.Drawing.Size(Dpi.S(this, 72), Dpi.S(this, 28));

                    // Appearance.Button で押しボタン然とした見た目にする
                    // （継承 ON/OFF のチェックボックスと見分けが付くように、
                    // このタブだけ意図的に違う見た目にしてある）。
                    // Margin を明示しないと既定値 Padding(3,3,3,3) になり、
                    // 上マージン 0 を明示した ledToggle と縦位置がずれる
                    // （実際に踏んだ不具合。FlowLayoutPanel は各コントロール
                    // 自身の上マージン分だけ行の上端から下げて配置するため）。
                    var pressToggle = new CheckBox
                    {
                        Text = "押す", Appearance = Appearance.Button,
                        AutoSize = true, MinimumSize = toggleSize,
                        Checked = _preview.GetButtonPressed(idx),
                        Margin = new Padding(0, 0, Dpi.S(this, 8), 0),
                    };
                    pressToggle.CheckedChanged += (_, _) =>
                    {
                        _preview.SetButtonPressed(idx, pressToggle.Checked);
                        _preview.Invalidate();
                    };
                    toggleRow.Controls.Add(pressToggle);

                    (Func<bool> Get, Action<bool> Set)? led = buttonNames[idx] switch
                    {
                        "PLAY" => (() => _preview.StateLedPlay, v => _preview.StateLedPlay = v),
                        "PAUSE" => (() => _preview.StateLedPause, v => _preview.StateLedPause = v),
                        "CONT" => (() => _preview.StateLedCont, v => _preview.StateLedCont = v),
                        "REPEAT" => (() => _preview.StateLedRepeat, v => _preview.StateLedRepeat = v),
                        _ => null,
                    };
                    if (led is { } l)
                    {
                        var ledToggle = new CheckBox
                        {
                            Text = "LED", Appearance = Appearance.Button,
                            AutoSize = true, MinimumSize = toggleSize,
                            // 左右の間隔は pressToggle の右マージンで確保済みなので、
                            // ここは上下だけ 0 にそろえる（左を空けると 2 重に空く）。
                            Checked = l.Get(), Margin = new Padding(0),
                        };
                        ledToggle.CheckedChanged += (_, _) =>
                        {
                            l.Set(ledToggle.Checked);
                            _preview.Invalidate();
                        };
                        toggleRow.Controls.Add(ledToggle);
                    }

                    buttonFlow.Controls.Add(toggleRow);
                    RegisterPreview(toggleRow,
                        new PreviewBinding(PreviewRegions.Ids.Indexed(PreviewRegions.Ids.PlayKeyButton, idx)), null);
                }
            }
            // プレビュー確認用のスライダ。layout.ini には何も書かない一時的な
            // 状態（2026-09-04、ユーザー指示）。以前はプレビューの下に
            // 「音量」「進捗」という状態バーでまとめて置いていたが、対応する
            // タブへ移した（進捗は「プレイ時間」に改名）。これでプレビューの
            // 下に何も要らなくなったので、状態バー自体を廃止した
            // （BuildStateBar は削除済み）。[スクロールバー] の「スクロール」
            // も同じ形で追加した。
            (string Label, int Min, int Max, int Init, Action<int> Set, string Region)? sliderDef =
                section.Title switch
                {
                    "プログレスバー" => ("プレイ時間", 0, 100, 40,
                        v => _preview.StateProgress = v / 100.0, PreviewRegions.Ids.ProgressBar),
                    "音量バー" => ("音量", -100, 100, 0,
                        v => _preview.StateVolume = v, PreviewRegions.Ids.VolumeBar),
                    "スクロールバー" => ("スクロール", 0, 100, 0,
                        v => _preview.StateScroll = v, PreviewRegions.Ids.ScrollBar),
                    _ => null,
                };
            if (sliderDef is { } sd)
            {
                var sliderRow = new FlowLayoutPanel
                {
                    FlowDirection = FlowDirection.LeftToRight,
                    WrapContents = false,
                    AutoSize = true,
                    AutoSizeMode = AutoSizeMode.GrowAndShrink,
                    Margin = new Padding(0, Dpi.S(this, 12), 0, 0),
                };
                var sliderLabel = new SingleLineLabel
                {
                    Text = sd.Label,
                    // 固定幅の当て推量は文言しだいで破綻する（"ラベル幅の教訓"）ので、
                    // 他の行と同じく実測する。ここは揃える相手がいない単独ラベルなので
                    // 自分の文字列だけで測ってよい。
                    Width = LabeledValueRow.MeasureLabelWidth(this, new[] { sd.Label }),
                    Height = Dpi.S(this, 24),
                    Margin = new Padding(0, Dpi.S(this, 3), Dpi.S(this, 8), 0),
                };
                var slider = new TrackBar
                {
                    Minimum = sd.Min, Maximum = sd.Max, Value = sd.Init,
                    Width = Dpi.S(this, 150), Margin = new Padding(0),
                };
                slider.ValueChanged += (_, _) => { sd.Set(slider.Value); _preview.Invalidate(); };
                sliderRow.Controls.Add(sliderLabel);
                sliderRow.Controls.Add(slider);
                flow.Controls.Add(sliderRow);
                RegisterPreview(sliderRow, new PreviewBinding(sd.Region), null);
            }
            if (section.Title == "スクロールバー")
            {
                // 上下矢印の押下状態をプレビューで確認するためのトグルボタン
                // （2026-09-04、ユーザー指示）。layout.ini には何も書かない
                // 一時的な状態。[操作ボタン] の「押す」と同じ Appearance.Button。
                var arrowRow = new FlowLayoutPanel
                {
                    FlowDirection = FlowDirection.LeftToRight,
                    WrapContents = false,
                    AutoSize = true,
                    AutoSizeMode = AutoSizeMode.GrowAndShrink,
                    Margin = new Padding(0, Dpi.S(this, 8), 0, 0),
                };
                var arrowToggleSize = new System.Drawing.Size(Dpi.S(this, 72), Dpi.S(this, 28));
                var upToggle = new CheckBox
                {
                    Text = "上矢印", Appearance = Appearance.Button,
                    AutoSize = true, MinimumSize = arrowToggleSize,
                    Margin = new Padding(0, 0, Dpi.S(this, 8), 0),
                };
                upToggle.CheckedChanged += (_, _) =>
                {
                    _preview.StateScrollUpPressed = upToggle.Checked;
                    _preview.Invalidate();
                };
                var downToggle = new CheckBox
                {
                    Text = "下矢印", Appearance = Appearance.Button,
                    AutoSize = true, MinimumSize = arrowToggleSize,
                    Margin = new Padding(0),
                };
                downToggle.CheckedChanged += (_, _) =>
                {
                    _preview.StateScrollDownPressed = downToggle.Checked;
                    _preview.Invalidate();
                };
                arrowRow.Controls.Add(upToggle);
                arrowRow.Controls.Add(downToggle);
                flow.Controls.Add(arrowRow);
                // 上矢印・下矢印は別々のアイテムなので、それぞれ別に登録する
                // （行全体を1つの領域に結び付けると、片方だけ選択して枠を
                // 出すことができなくなる）。
                RegisterPreview(upToggle, new PreviewBinding(PreviewRegions.Ids.ScrollUpArrow), null);
                RegisterPreview(downToggle, new PreviewBinding(PreviewRegions.Ids.ScrollDownArrow), null);
            }
            if (section.Title == "鍵盤")
            {
                // ランダムに選んだ鍵を押している状態を確認するためのトグル
                // ボタン。[配色] の直前に置く（2026-09-04、ユーザー指示）。
                // layout.ini には何も書かない一時的な状態で、ON にするたびに
                // 乱数を選び直す（PreviewCanvas.RandomizePressedKeys）。
                var pressKeysToggle = new CheckBox
                {
                    Text = "押す", Appearance = Appearance.Button,
                    AutoSize = true,
                    MinimumSize = new System.Drawing.Size(Dpi.S(this, 72), Dpi.S(this, 28)),
                    Margin = new Padding(0, Dpi.S(this, 12), 0, 0),
                };
                pressKeysToggle.CheckedChanged += (_, _) =>
                {
                    _preview.StateKeysPressed = pressKeysToggle.Checked;
                    _preview.Invalidate();
                };
                flow.Controls.Add(pressKeysToggle);
                RegisterPreview(pressKeysToggle, new PreviewBinding(PreviewRegions.Ids.KeyboardAll), null);
            }

            // 「配色」単独タブは廃止したので、対応するセクションがあれば
            // このタブの末尾（レイアウト項目の後）へ続けて置く。
            if (ColorSectionByTab.TryGetValue(section.Title, out var colorTitle) &&
                colorSectionsByTitle.TryGetValue(colorTitle, out var colorSection))
            {
                // 素の Label は AutoSize=true のまま Width/Height を明示すると
                // 文字が縦に切れることがある（SingleLineLabel のコメント参照）。
                // この見出しはタブ 1 個につき 1 回しか作らないコードだが、
                // 対応する全タブ（画面/鍵盤/ステータス/曲名/ファイラー/
                // 操作ボタン）で共有しているので、ここを直せば全部直る。
                var head = new SingleLineLabel
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
                    RegisterPreview(ctrl, PreviewBindings.ForColorSection(colorSection.Title), null);
                }
            }

            page.Controls.Add(flow);
            tabs.TabPages.Add(page);
        }

        // プレビューのクリック。その点で拾えるアイテムを優先順位の高い順に
        // 並べ、選択中のものが居ればその次へ、最後まで行ったら選択を外す
        // （skineditor_hitcheck.md の「プレビュー画面での操作」）。
        _preview.PreviewClicked += pt =>
        {
            var regions = _preview.BuildRegions();
            var candidates = _previewTargets
                .Where(t => t.Binding.Hit > 0
                            && regions.TryGetValue(t.Binding.Region, out var r) && r.Contains(pt))
                // OrderByDescending は安定なので、H が同じものは登録順
                // （＝仕様書の並び順）のまま残る。
                .OrderByDescending(t => t.Binding.Hit)
                .ToList();
            if (candidates.Count == 0)
            {
                SelectTarget(null, moveFocus: false);
                return;
            }
            int cur = _selectedTarget == null ? -1 : candidates.IndexOf(_selectedTarget);
            if (cur < 0) SelectTarget(candidates[0], moveFocus: true);
            else if (cur + 1 < candidates.Count) SelectTarget(candidates[cur + 1], moveFocus: true);
            else SelectTarget(null, moveFocus: false);
        };

        doc.Changed += RefreshAll;
        KeyPreview = true;
        KeyDown += (_, e) => { if (e.Control && e.KeyCode == Keys.S) DoSave(); };
        FormClosing += OnFormClosing;

        RefreshAll();
    }

    // ---- プレビューの選択（枠の表示とクリックでの行き来） --------------------

    // 1 つのコントロール（またはその中の入力欄）にフォーカスが来たら、
    // 対応するアイテムを選択中にする。binding.Region が空（対応アイテムが
    // 無い項目。ミニフォントなど）のときは、逆に選択を外す。
    private void RegisterPreview(Control host, PreviewBinding binding, Action<int, int>? move)
    {
        PreviewTarget? target = null;
        if (!string.IsNullOrEmpty(binding.Region))
        {
            target = new PreviewTarget { Ctrl = host, Binding = binding, Move = move };
            _previewTargets.Add(target);
        }
        HookEnter(host, target);
    }

    private void HookEnter(Control c, PreviewTarget? target)
    {
        c.Enter += (_, _) => SelectTarget(target, moveFocus: false);
        foreach (Control child in c.Controls) HookEnter(child, target);
    }

    private void SelectTarget(PreviewTarget? target, bool moveFocus)
    {
        _selectedTarget = target;
        _preview.SetSelection(target?.Binding.Region, target?.Move);
        if (target == null || !moveFocus) return;
        ShowControl(target.Ctrl);
        FocusInto(target.Ctrl);
    }

    // そのコントロールが載っているタブページを（入れ子のサブタブも含めて）
    // 手前に出す。
    private static void ShowControl(Control c)
    {
        for (Control? p = c; p != null; p = p.Parent)
        {
            if (p is TabPage page && page.Parent is TabControl tc) tc.SelectedTab = page;
        }
    }

    private static IEnumerable<Control> SelfAndChildren(Control c)
    {
        yield return c;
        foreach (Control child in c.Controls)
        {
            foreach (var d in SelfAndChildren(child)) yield return d;
        }
    }

    // 行そのもの（Panel）はフォーカスを取れないので、中の入力欄へ入れる。
    // 優先順は 数値欄 -> チェックボックス -> その他。
    // - 数値欄は Dock=Left の都合で **追加順が見た目と逆**（x の小さい順に
    //   並べ直さないと、いきなり右端の値にカーソルが入る）。
    // - ボタン（素材行の「インポート...」）を最後に回すのは、うっかり
    //   スペースキーでファイル選択が開くのを避けるため。
    private static bool FocusInto(Control host)
    {
        var numerics = SelfAndChildren(host).OfType<NumericUpDown>()
            .Where(n => n.CanFocus && n.Enabled)
            .OrderBy(n => n.Left)
            .ToList();
        if (numerics.Count > 0) return numerics[0].Focus();

        foreach (var c in SelfAndChildren(host).OfType<CheckBox>())
        {
            if (c.CanFocus && c.TabStop && c.Enabled) return c.Focus();
        }
        foreach (var c in SelfAndChildren(host))
        {
            if (c.CanFocus && c.TabStop && c.Enabled) return c.Focus();
        }
        return false;
    }

    // ドラッグでの書き戻し。渡ってくるのはアイテムの左上の絶対座標 (skin px)。
    // 値の作り方そのものは PreviewDragMath（テストで往復を確認している）。
    private Action<int, int>? MoveActionFor(PreviewBinding b, string section, string key)
    {
        if (b.Drag == PreviewDrag.None) return null;
        return (nx, ny) =>
        {
            var value = PreviewDragMath.ValueFor(b, _preview.BuildRegions(), nx, ny);
            if (value != null) _doc.SetLayoutRaw(section, key, value);
        };
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
