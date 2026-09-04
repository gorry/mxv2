// 画面の並び（UI/TabLayout.cs）と、項目の定義（Model/LayoutFieldSchema.cs）の
// 突き合わせ。
//
// 2026-09-06 の整理前は「スキーマを全部なめて 1 項目 1 行を作る」実装だったので、
// **項目を足せば必ず画面に出る**ことが構造的に保証されていた。並びを宣言に
// 変えたぶんその保証が無くなる（TabLayout に書き忘れると黙って画面から
// 消える）ので、ここで担保する。

using SkinEditor.Model;
using SkinEditor.UI;

namespace SkinEditor.Tests;

public class TabLayoutTests
{
    private static IEnumerable<Node> Flatten(IEnumerable<Node> nodes)
    {
        foreach (var node in nodes)
        {
            yield return node;
            if (node is not SubTabsNode sub) continue;
            foreach (var (_, pageNodes) in sub.Pages)
            {
                foreach (var inner in Flatten(pageNodes)) yield return inner;
            }
        }
    }

    private static List<Node> AllNodes() =>
        Flatten(TabLayout.Build().SelectMany(t => t.Nodes)).ToList();

    // layout.ini の項目は、どれか 1 つのタブにちょうど 1 回出る。
    // 「1 キーを複数行に分けて見せる」もの（XOffset / ChannelY / PcmX,PcmY）は
    // FieldDef を持たないので、MultiValueNode / PcmChannelsNode 側で数える。
    [Fact]
    public void EveryFieldAppearsExactlyOnce()
    {
        var placed = AllNodes().OfType<FieldNode>()
            .Select(n => $"{n.Section}/{n.Key}")
            .ToList();

        var duplicated = placed.GroupBy(k => k).Where(g => g.Count() > 1).Select(g => g.Key).ToList();
        Assert.True(duplicated.Count == 0, $"画面に 2 回以上出ている項目: {string.Join(", ", duplicated)}");

        var missing = LayoutFieldSchema.All()
            .Select(f => $"{f.Section}/{f.Key}")
            .Where(k => !placed.Contains(k))
            .ToList();
        Assert.True(missing.Count == 0, $"どのタブにも出ていない項目: {string.Join(", ", missing)}");
    }

    // 逆向き。TabLayout の書き間違い（Section/Key のタイポ）を拾う。
    // 実際の生成時も LayoutFieldSchema.Get が例外を投げるが、
    // GUI を起動しないと分からないのでテストでも見る。
    [Fact]
    public void EveryPlacedFieldExistsInTheSchema()
    {
        foreach (var node in AllNodes().OfType<FieldNode>())
        {
            var ex = Record.Exception(() => LayoutFieldSchema.Get(node.Section, node.Key));
            Assert.True(ex == null, $"TabLayout の {node.Section}/{node.Key} は LayoutFieldSchema に無い");
        }
    }

    // 「1 キーを複数行に分けて見せる」ものが、キーの値の個数と食い違って
    // いないこと（行のラベル数の合計＝値の個数）。
    [Fact]
    public void MultiValueRowsCoverEveryValue()
    {
        var doc = SkinDocument.Open(TestPaths.FindDevRoot(), "Default");
        foreach (var node in AllNodes().OfType<MultiValueNode>())
        {
            int labels = node.Rows.Sum(r => r.Length);
            int values = node.Select(doc.Effective).Length;
            Assert.True(labels == values,
                $"{node.Section}/{node.Key}: 行のラベルは {labels} 個だが値は {values} 個");
        }
    }

    // 配色（colors.ini）のセクションも、どれか 1 つのタブにちょうど 1 回出る。
    [Fact]
    public void EveryColorSectionAppearsExactlyOnce()
    {
        var placed = AllNodes().OfType<ColorsNode>().Select(n => n.SectionTitle).ToList();

        var duplicated = placed.GroupBy(k => k).Where(g => g.Count() > 1).Select(g => g.Key).ToList();
        Assert.True(duplicated.Count == 0, $"画面に 2 回以上出ている配色: {string.Join(", ", duplicated)}");

        var missing = ColorFieldSchema.BuildSections()
            .Select(s => s.Title)
            .Where(t => !placed.Contains(t))
            .ToList();
        Assert.True(missing.Count == 0, $"どのタブにも出ていない配色: {string.Join(", ", missing)}");
    }

    // 素材（BitmapRole）も、どれか 1 つのタブにちょうど 1 回出る
    // （素材行が無いとインポートする手段が無くなる）。
    [Fact]
    public void EveryBitmapRoleAppearsExactlyOnce()
    {
        var placed = AllNodes().OfType<BitmapNode>().Select(n => n.Role).ToList();

        var duplicated = placed.GroupBy(r => r).Where(g => g.Count() > 1).Select(g => g.Key).ToList();
        Assert.True(duplicated.Count == 0, $"画面に 2 回以上出ている素材: {string.Join(", ", duplicated)}");

        var missing = Enum.GetValues<BitmapRole>().Where(r => !placed.Contains(r)).ToList();
        Assert.True(missing.Count == 0, $"どのタブにも出ていない素材: {string.Join(", ", missing)}");
    }
}
