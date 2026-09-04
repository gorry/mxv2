// mxv2 スキンエディタ - TabLayout の並びをそのままコントロールにする。
//
// **ここには「どのタブか」による分岐は無い。** ノードの種類ごとに 1 メソッド
// 並んでいるだけで、どこに何を置くかは全部 UI/TabLayout.cs 側のデータ。
// 生成したコントロールは種類ごとのリストに溜めておき、フォームの
// RefreshAll がそれを回す。

using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class TabPageBuilder
{
    private readonly SkinDocument _doc;
    private readonly PreviewCanvas _preview;
    private readonly Control _ctx;   // Dpi.S とフォント継承の基準（フォーム）
    private readonly Action<Control, PreviewBinding, Action<int, int>?> _register;
    private readonly Func<PreviewBinding, string, string, Action<int, int>?> _moveFor;
    private readonly Dictionary<string, ColorSectionDef> _colorSections;

    // RefreshAll が回す生成物。
    public List<FieldEditControl> Fields { get; } = new();
    public List<ColorFieldEditControl> Colors { get; } = new();
    public List<BitmapRoleRow> Bitmaps { get; } = new();
    public List<PcmChannelRow> PcmRows { get; } = new();
    public List<LabeledValueRow> MultiValueRows { get; } = new();
    public List<GroupCheckRow> GroupChecks { get; } = new();

    public TabPageBuilder(SkinDocument doc, PreviewCanvas preview, Control ctx,
        Action<Control, PreviewBinding, Action<int, int>?> register,
        Func<PreviewBinding, string, string, Action<int, int>?> moveFor)
    {
        _doc = doc;
        _preview = preview;
        _ctx = ctx;
        _register = register;
        _moveFor = moveFor;
        _colorSections = ColorFieldSchema.BuildSections().ToDictionary(s => s.Title);
    }

    public void Render(FlowLayoutPanel target, IEnumerable<Node> nodes, int width)
    {
        foreach (var node in nodes)
        {
            switch (node)
            {
                case FieldNode n: AddField(target, n, width); break;
                case BitmapNode n: AddBitmap(target, n, width); break;
                case ColorsNode n: AddColors(target, n, width); break;
                case SubTabsNode n: AddSubTabs(target, n, width); break;
                case MultiValueNode n: AddMultiValue(target, n, width); break;
                case PcmChannelsNode: AddPcmChannels(target, width); break;
                case SliderNode n: AddSlider(target, n); break;
                case TogglesNode n: AddToggles(target, n); break;
                default: throw new NotSupportedException($"未対応のノード {node.GetType().Name}");
            }
        }
    }

    private void AddField(FlowLayoutPanel target, FieldNode node, int width)
    {
        var field = LayoutFieldSchema.Get(node.Section, node.Key);
        var ctrl = new FieldEditControl(_doc, field) { Width = width };
        Fields.Add(ctrl);
        target.Controls.Add(ctrl);
        var binding = PreviewBindings.For(field.Section, field.Key);
        _register(ctrl, binding, _moveFor(binding, field.Section, field.Key));
    }

    private void AddBitmap(FlowLayoutPanel target, BitmapNode node, int width)
    {
        var row = new BitmapRoleRow(_doc, node.Role, _preview, shortLabel: node.ShortLabel) { Width = width };
        Bitmaps.Add(row);
        target.Controls.Add(row);
        _register(row, PreviewBindings.ForBitmap(node.Role), null);
    }

    private void AddColors(FlowLayoutPanel target, ColorsNode node, int width)
    {
        var section = _colorSections[node.SectionTitle];
        // 素の Label は AutoSize=true のまま Width/Height を明示すると文字が
        // 縦に切れることがある（SingleLineLabel のコメント参照）。
        target.Controls.Add(new SingleLineLabel
        {
            Text = "配色", Width = width, Height = Dpi.S(_ctx, 22),
            Font = new System.Drawing.Font(_ctx.Font, System.Drawing.FontStyle.Bold),
            Margin = new Padding(0, Dpi.S(_ctx, 12), 0, Dpi.S(_ctx, 4)),
        });
        foreach (var field in section.Fields)
        {
            var ctrl = new ColorFieldEditControl(_doc, field) { Width = width };
            Colors.Add(ctrl);
            target.Controls.Add(ctrl);
            _register(ctrl, PreviewBindings.ForColorSection(section.Title), null);
        }
    }

    private void AddSubTabs(FlowLayoutPanel target, SubTabsNode node, int width)
    {
        var tabs = new TabControl
        {
            Width = width,
            // 高さは既定では後で実測して決める（ApplyAutoHeights）。ここでは
            // 測るときの基準になる仮の高さだけ入れておく。
            Height = Dpi.S(_ctx, node.Height > 0 ? node.Height : 400),
            Margin = new Padding(0, 0, 0, Dpi.S(_ctx, 4)),
            // タブ名を縮めたので今は 1 行に収まっているが、増えたり長く
            // なったりしたときのために複数行で全部見せる指定は残しておく。
            Multiline = true,
        };
        if (node.Height <= 0) _autoHeightTabs.Add(tabs);
        target.Controls.Add(tabs);

        // サブタブの中の行は、入れ子の TabControl 自身の枠のぶんだけタブ直下の
        // 行より狭くしないと横スクロールが出る（実測して踏んだ。40px 引けば
        // 足りる）。
        int inner = width - Dpi.S(_ctx, 40);
        foreach (var (title, nodes) in node.Pages)
        {
            var flow = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill, FlowDirection = FlowDirection.TopDown,
                WrapContents = false, AutoScroll = true,
            };
            var page = new TabPage(title);
            page.Controls.Add(flow);
            tabs.TabPages.Add(page);
            Render(flow, nodes, inner);
        }
    }

    private void AddMultiValue(FlowLayoutPanel target, MultiValueNode node, int width)
    {
        var group = new GroupCheckRow(_doc, node.CheckLabel, width,
            (node.Section, node.Key, e => SkinLayoutIo.Join(node.Select(e))));
        GroupChecks.Add(group);
        target.Controls.Add(group);
        _register(group, new PreviewBinding(node.CheckRegion), null);

        // 縦に並ぶ行で数値欄の列を揃えたいので、複数個ある行のラベルは
        // まとめて測った最大幅を全行で共通に使う（行ごとに測ると "C" と "C#"
        // で列がずれる）。単独行は揃える相手がいないので自分の文字列だけ。
        var shared = node.Rows.Where(r => r.Length > 1).SelectMany(r => r).ToArray();
        int sharedWidth = shared.Length > 0 ? LabeledValueRow.MeasureLabelWidth(_ctx, shared) : 0;

        int index = 0;
        foreach (var labels in node.Rows)
        {
            int labelWidth = labels.Length > 1 ? sharedWidth : LabeledValueRow.MeasureLabelWidth(_ctx, labels);
            var row = new LabeledValueRow(_doc, node.Section, node.Key, node.Select, index, labels, labelWidth,
                node.Min, node.Max)
            { Width = Dpi.S(_ctx, IndentedRowWidth) };
            group.Rows.Add(row);
            MultiValueRows.Add(row);
            target.Controls.Add(row);
            // 数値欄 1 個が鍵 1 個・チャンネル 1 段に対応するので、行ではなく
            // 数値欄ごとにプレビューへ結び付ける。
            foreach (var (valueIndex, ctrl) in row.ValueControls)
                _register(ctrl, PreviewBindings.For(node.Section, node.Key, valueIndex), null);
            index += labels.Length;
        }
    }

    private void AddPcmChannels(FlowLayoutPanel target, int width)
    {
        var group = new GroupCheckRow(_doc, "PCM の位置", width,
            ("Status", "PcmX", e => SkinLayoutIo.Join(e.pcmXOffset)),
            ("Status", "PcmY", e => SkinLayoutIo.Join(e.pcmYOffset)));
        GroupChecks.Add(group);
        target.Controls.Add(group);
        _register(group, new PreviewBinding(PreviewRegions.Ids.PcmAll), null);

        for (int i = 0; i < 8; i++)
        {
            // PcmChannelRow は自分の左マージンで 24px インデントを持つので、
            // 幅はそのぶん狭くして右端をそろえる。
            var row = new PcmChannelRow(_doc, i) { Width = width - Dpi.S(_ctx, 24) };
            group.Rows.Add(row);
            PcmRows.Add(row);
            target.Controls.Add(row);
            _register(row, PreviewBindings.For("Status", "PcmX", i), null);
        }
    }

    private void AddSlider(FlowLayoutPanel target, SliderNode node)
    {
        var row = new FlowLayoutPanel
        {
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false,
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            Margin = new Padding(0, Dpi.S(_ctx, node.TopMargin), 0, 0),
        };
        row.Controls.Add(new SingleLineLabel
        {
            Text = node.Label,
            // 固定幅の当て推量は文言しだいで破綻するので実測する。ここは
            // 揃える相手がいない単独ラベルなので自分の文字列だけで測ってよい。
            Width = LabeledValueRow.MeasureLabelWidth(_ctx, new[] { node.Label }),
            Height = Dpi.S(_ctx, 24),
            Margin = new Padding(0, Dpi.S(_ctx, 3), Dpi.S(_ctx, 8), 0),
        });
        var slider = new TrackBar
        {
            Minimum = node.Min, Maximum = node.Max, Value = node.Init,
            Width = Dpi.S(_ctx, 150), Margin = new Padding(0),
        };
        slider.ValueChanged += (_, _) => { node.Set(_preview, slider.Value); _preview.Invalidate(); };
        row.Controls.Add(slider);
        target.Controls.Add(row);
        _register(row, new PreviewBinding(node.Region), null);
    }

    private void AddToggles(FlowLayoutPanel target, TogglesNode node)
    {
        var row = new FlowLayoutPanel
        {
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false,
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            Margin = new Padding(0, Dpi.S(_ctx, node.TopMargin), 0, 0),
        };
        var size = new System.Drawing.Size(Dpi.S(_ctx, 72), Dpi.S(_ctx, 28));
        for (int i = 0; i < node.Toggles.Length; i++)
        {
            var def = node.Toggles[i];
            bool last = i == node.Toggles.Length - 1;
            // Appearance.Button で押しボタン然とした見た目にする（継承 ON/OFF の
            // チェックボックスと見分けが付くように意図的に違う見た目）。
            // Margin を明示しないと既定の Padding(3,3,3,3) が効いて隣と縦位置が
            // ずれる。TextAlign も明示しないと CheckBox 既定の MiddleLeft の
            // ままで、Appearance.Button にしても文字が左へ寄る（どちらも実機で
            // 踏んだ。2026-09-05）。
            var toggle = new CheckBox
            {
                Text = def.Text,
                Appearance = Appearance.Button,
                TextAlign = System.Drawing.ContentAlignment.MiddleCenter,
                AutoSize = true,
                MinimumSize = size,
                Checked = def.Get(_preview),
                Margin = new Padding(0, 0, last ? 0 : Dpi.S(_ctx, 8), 0),
            };
            toggle.CheckedChanged += (_, _) => { def.Set(_preview, toggle.Checked); _preview.Invalidate(); };
            row.Controls.Add(toggle);
            _register(toggle, new PreviewBinding(def.Region), null);
        }
        target.Controls.Add(row);
    }

    // グループのチェックボックスにぶら下がる行の幅（インデントぶん狭い）。
    private const int IndentedRowWidth = 566;

    // ---- 入れ子タブの高さの実測 -------------------------------------------
    // TabControl はページごとに高さを変えられないので、一番背の高いページに
    // 合わせるしかない。どのページが一番高いかを人が数えると間違える
    // （実際に「操作ボタン」で、ボタン別ページだと思い込んで「パレット」
    // ページを見落とし、縦スクロールバーを出した）ので、実物を測る。
    private readonly List<TabControl> _autoHeightTabs = new();

    // フォームのハンドルができてレイアウトが確定してから呼ぶこと
    // （Font の継承が済んでいないと AutoSize の行が本来の高さを返さない）。
    public void ApplyAutoHeights()
    {
        foreach (var tabs in _autoHeightTabs)
        {
            // タブ見出しの帯＋枠のぶん。DisplayRectangle は中身に使える領域
            // なので、その差が「中身以外に要る高さ」になる。
            int chrome = tabs.Height - tabs.DisplayRectangle.Height;

            int content = 0;
            foreach (TabPage page in tabs.TabPages)
            {
                foreach (Control child in page.Controls)
                {
                    if (child is FlowLayoutPanel flow) content = Math.Max(content, ContentHeight(flow));
                }
            }
            tabs.Height = content + chrome;
        }
    }

    // FlowLayoutPanel（TopDown・折り返し無し）の中身の高さ。
    // AutoSize の行（トグルやスライダ）は Height がまだ 0 のことがあるので
    // PreferredSize と大きいほうを採る。
    // タブページ側の高さ（＝ウィンドウの最小高さ）を出すのにも使う。
    public static int ContentHeight(FlowLayoutPanel flow)
    {
        int height = flow.Padding.Vertical;
        foreach (Control child in flow.Controls)
        {
            int own = child.AutoSize ? Math.Max(child.Height, child.PreferredSize.Height) : child.Height;
            height += own + child.Margin.Vertical;
        }
        return height;
    }
}
