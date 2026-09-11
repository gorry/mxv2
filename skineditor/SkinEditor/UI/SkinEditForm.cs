// mxv2 スキンエディタ - エディタ本体。左にプレビュー、右にプロパティタブ。
//
// このファイルが持つのはウィンドウの骨格（ツールバー・スプリッタ・タブの器）と、
// プレビューの選択まわり、そして全体の再描画だけ。
// **タブの中身の並びは UI/TabLayout.cs、その組み立ては UI/TabPageBuilder.cs。**

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class SkinEditForm : Form
{
    private readonly SkinDocument _doc;
    private readonly PreviewCanvas _preview;
    private bool _refreshAfterDrag;  // ドラッグ中に見送った RefreshAll があるか
    private readonly TabPageBuilder _builder;
    // ウィンドウの最小高さと初期サイズを中身から算出する（OnLoad）ために持っておく。
    private readonly FlowLayoutPanel _toolbar;
    private readonly TabControl _tabs;
    private readonly SplitContainer _split;
    private readonly int _tabViewWidth;
    private readonly List<FlowLayoutPanel> _tabFlows = new();
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
        var toolbar = _toolbar = new FlowLayoutPanel
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
        };
        // 左だけ空けて、**上下と右は [保存] と同じ余白にする**。
        // FlowLayoutPanel は「行の上端 + Margin.Top」に置くので、ここを 0 に
        // すると（既定の Margin.Top が 3 の）[保存] より 3px 上へずれる
        // （実測で 8px と 5px。高さはどちらも同じ 38px）。
        _revertColorsButton.Margin = new Padding(
            Dpi.S(this, 16), _saveButton.Margin.Top,
            _saveButton.Margin.Right, _saveButton.Margin.Bottom);
        _revertColorsButton.Click += (_, _) => { _doc.RevertColorsToInherited(); RefreshAll(); };
        // 外部のツールで素材（.bmp / font.ttf）を描き換えたときに、
        // プレビューへ反映させるためのボタン。エディタは読んだ素材を
        // キャッシュしているので、ファイルの中身だけが変わっても自分では
        // 気付けない（インポートし直さないかぎり古い絵のままになる）。
        var reloadAssetsButton = new Button
        {
            Text = "素材の再読み込み", AutoSize = true,
        };
        reloadAssetsButton.Margin = new Padding(
            Dpi.S(this, 16), _saveButton.Margin.Top,
            _saveButton.Margin.Right, _saveButton.Margin.Bottom);
        reloadAssetsButton.Click += (_, _) => ReloadAssets();
        toolbar.Controls.Add(_saveButton);
        toolbar.Controls.Add(dirtyLabel);
        toolbar.Controls.Add(_baseRefDropdown);
        toolbar.Controls.Add(_revertColorsButton);
        toolbar.Controls.Add(reloadAssetsButton);

        var split = _split = new SplitContainer { Dock = DockStyle.Fill };

        // Fill (split) を先に追加する（= Z 順序の最背面に自然に置かれる）。
        // 後から BringToFront()/SendToBack() で回すと、初回レイアウト確定前の
        // 矩形で兄弟コントロールに対する描画クリップ領域が固定されてしまい、
        // 一部の子コントロールが永久に再描画されない不具合があった
        // （SkinListForm の ListBox で踏んだのと同じ原因）。
        Controls.Add(split);
        Controls.Add(toolbar);

        // プレビュー側の幅。ドットバイドット（スキン 1px = 画面 1px）にしたいので
        // **DPI 倍率は掛けない**（Dpi.S を通さない）。最終的な値は中身と最小
        // サイズを見て OnLoad の ApplyInitialSize が入れ直すので、ここは
        // SplitterDistance を一度設定するための初期値。
        int panel1Width = doc.Effective.screenW + PreviewCanvas.Margin;
        // 行 1 本ぶんの幅。「(x,y,w,h)」サフィックスの右端が切れないところまで
        // 何度か広げてきた値（620→670→720→760）。数値欄の幅（SpinWidth）を
        // 変えるとここも足りなくなるので、変えたら「タブ直下の一番幅を食う行」と
        // 「サブタブの中の一番幅を食う行」の両方を実機で確認すること
        // （サブタブは入れ子 TabControl の枠のぶん 40px 狭い）。
        int rowWidth = Dpi.S(this, 760);
        // 各タブの中身（rowWidth の行、FlowLayoutPanel.AutoScroll=true）が
        // 縦に収まりきらないと、その FlowLayoutPanel は縦スクロールバーの
        // ぶんだけ実効の横幅が削られる。既定の Panel2 幅がこれより少し狭く、
        // 横スクロールバーも出てしまっていた（実測して踏んだ）。右パネルの
        // 幅を「rowWidth + 縦スクロールバーの幅ぶん」に固定し、ウィンドウの
        // 既定幅・最小幅の両方をこれに合わせることで、横スクロールバーが
        // 出ない幅を最初から確保する。
        int tabViewWidth = _tabViewWidth = rowWidth + SystemInformation.VerticalScrollBarWidth + Dpi.S(this, 8);

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
        // 実際の初期サイズは OnLoad で入れ直す（ApplyInitialSize）。ここでは
        // SplitterDistance / Panel2MinSize を設定できるだけの仮サイズにする。
        ClientSize = new System.Drawing.Size(
            panel1Width + split.SplitterWidth + tabViewWidth, Dpi.S(this, 800));
        split.SplitterDistance = panel1Width;
        split.Panel2MinSize = tabViewWidth;

        // 音量・進捗スライダは、それぞれ対応するタブへ移したので、プレビューの
        // 下に状態バーを置く必要が無くなった（2026-09-04、ユーザー指示）。
        // プレビュー側パネルはキャンバスだけになる。
        _preview = new PreviewCanvas(doc) { Dock = DockStyle.Fill };
        split.Panel1.Controls.Add(_preview);

        // 幅は「横スクロールバーが出ない幅」。高さは中身を実測して OnLoad で
        // 入れ直す（ApplyMinimumHeight）ので、ここでは仮の値。
        MinimumSize = new System.Drawing.Size(
            Size.Width - ClientSize.Width + split.Panel1MinSize + split.SplitterWidth + tabViewWidth,
            Dpi.S(this, 500));

        // タブ数が多いので、矢印での横スクロールではなく複数行で全部並べて見せる。
        var tabs = _tabs = new TabControl { Dock = DockStyle.Fill, Multiline = true };
        split.Panel2.Controls.Add(tabs);

        _builder = new TabPageBuilder(doc, _preview, this, RegisterPreview, MoveActionFor);
        foreach (var tab in TabLayout.Build())
        {
            var flow = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown,
                WrapContents = false, AutoScroll = true,
            };
            _builder.Render(flow, tab.Nodes, rowWidth);
            _tabFlows.Add(flow);

            var page = new TabPage(tab.Title);
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

        // ドラッグ中は Changed のたびに全コントロールを更新しない（重くて
        // プレビューの再描画が後回しになる。PreviewCanvas.Dragging）。
        // **動かしている項目の欄だけ**はその場で追従させて、ドラッグしながら
        // 座標を読めるようにする（ユーザーの指示）。残りは離したときに 1 回。
        doc.Changed += () =>
        {
            if (_preview.Dragging)
            {
                _refreshAfterDrag = true;
                if (_selectedTarget?.Ctrl is FieldEditControl dragged) dragged.Refresh();
                return;
            }
            RefreshAll();
        };
        _preview.DragEnded += () =>
        {
            if (!_refreshAfterDrag) return;
            _refreshAfterDrag = false;
            RefreshAll();
        };
        KeyPreview = true;
        KeyDown += (_, e) => { if (e.Control && e.KeyCode == Keys.S) DoSave(); };
        FormClosing += OnFormClosing;

        RefreshAll();
    }

    // 入れ子タブ（サブタブ）の高さは、中身を実測して決める。ここでやるのは
    // ハンドルができてレイアウトが確定してからでないと、AutoSize の行の高さも
    // TabControl の DisplayRectangle（＝タブ見出しの帯を除いた領域）も
    // 正しい値にならないため。表示前なので、ちらつきは出ない。
    protected override void OnLoad(EventArgs e)
    {
        base.OnLoad(e);
        // サブタブの高さを先に確定させること。タブページの中身の高さ
        // （＝下のウィンドウ最小高さ）は、それを含んだ値になるため。
        // 初期サイズは最小サイズを見て決めるので、この順でしか呼べない。
        _builder.ApplyAutoHeights();
        ApplyMinimumHeight();
        ApplyInitialSize();
    }

    // 初期サイズは「開いたスキンがドットバイドットで出る大きさ」
    // （2026-09-06、ユーザー指示）。画面（スクリーン）より大きくなっても
    // 縮めない。ただし最小サイズ（＝タブページにスクロールバーが出ない高さ）を
    // 下回るときは、余ったぶんプレビューを拡大して埋める。
    //
    // PreviewCanvas は「幅・高さのうち厳しいほうに合わせる」ので、幅と高さの
    // 両方を同じ倍率ぶん確保しないと、片方が余って拡大されない（等倍のまま
    // 上下に余白が付く）。だから倍率を先に決めて、幅もそれに合わせる。
    private void ApplyInitialSize()
    {
        int skinW = Math.Max(1, _doc.Effective.screenW);
        int skinH = Math.Max(1, _doc.Effective.screenH);

        int frameH = Height - ClientSize.Height;      // タイトルバーと枠
        int previewTop = _toolbar.Height;             // プレビューはツールバーの下
        // 最小サイズのぶん、プレビューに必ず割り当たる高さ。
        int forcedPreviewH = MinimumSize.Height - frameH - previewTop;

        // 等倍で足りるならそのまま。足りない（＝最小サイズのほうが大きい）
        // ときだけ、その高さを埋める倍率まで拡大する。
        float scale = Math.Max(1f, (forcedPreviewH - PreviewCanvas.Margin) / (float)skinH);
        int previewW = (int)Math.Ceiling(skinW * scale) + PreviewCanvas.Margin;
        int previewH = (int)Math.Ceiling(skinH * scale) + PreviewCanvas.Margin;

        ClientSize = new System.Drawing.Size(
            previewW + _split.SplitterWidth + _tabViewWidth,
            previewTop + Math.Max(previewH, forcedPreviewH));
        _split.SplitterDistance = previewW;

        // CenterScreen はこのサイズ変更より前に効いているので、置き直す。
        // スキンが大きくてウィンドウが画面からはみ出す場合でも縮めない
        // （ユーザー指示）が、タイトルバーだけは掴めるように左上は
        // 作業領域の内側に留める。
        var area = Screen.FromControl(this).WorkingArea;
        Location = new System.Drawing.Point(
            Math.Max(area.X, area.X + (area.Width - Width) / 2),
            Math.Max(area.Y, area.Y + (area.Height - Height) / 2));
    }

    // ウィンドウの最小の高さ＝「一番背の高いタブページが縦スクロールバー
    // 無しで収まる高さ」。決め打ちの 500 だとどのタブでもスクロールバーが
    // 出ていた（2026-09-06、ユーザー指示）。サブタブの高さと同じく、
    // 中身と枠を実測して出す。
    private void ApplyMinimumHeight()
    {
        int content = 0;
        foreach (var flow in _tabFlows) content = Math.Max(content, TabPageBuilder.ContentHeight(flow));

        // 外側タブの見出し帯（このアプリは 2 行）＋枠のぶん。
        int tabChrome = _tabs.Height - _tabs.DisplayRectangle.Height;
        // クライアント領域に要る高さ。ツールバーはタブの上に載っている。
        int client = _toolbar.Height + tabChrome + content;
        // ウィンドウ枠とタイトルバーのぶん。
        int frame = Height - ClientSize.Height;

        MinimumSize = new System.Drawing.Size(MinimumSize.Width, client + frame);
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
        foreach (var c in _builder.Fields) c.Refresh();
        foreach (var c in _builder.Colors) c.Refresh();
        foreach (var r in _builder.Bitmaps) r.RefreshStatus();
        foreach (var r in _builder.PcmRows) r.Refresh();
        foreach (var r in _builder.MultiValueRows) r.Refresh();
        // グループのチェックは、ぶら下がる行の編集可否も一緒に配る。
        foreach (var g in _builder.GroupChecks) g.Refresh();

        _baseRefDropdown.RefreshItems();
        _revertColorsButton.Enabled = _doc.HasOwnColors && !string.IsNullOrEmpty(_doc.BaseRef);
        Text = $"スキンエディタ - {_doc.Name}{(_doc.IsDirty ? " *" : "")}";
        _saveButton.Enabled = _doc.IsDirty;
    }

    // 素材を読み直してプレビューを描き直す。**layout.ini / colors.ini は
    // 読み直さない**（エディタが編集中の値のほうが新しいので、外から
    // 上書きされては困る）。RefreshAll を続けて呼ぶのは、素材の大きさで
    // 決まるものを更新するため——素材行の「あり / なし」と、「素材内: …」
    // のスピンボタンの上限（SkinDocument.ResolveBitmapSize）。
    private void ReloadAssets()
    {
        _preview.InvalidateBitmaps();
        RefreshAll();
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
