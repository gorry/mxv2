// プレビューの「選択」まわり（skineditor_hitcheck.md）の回帰テスト。
//
// PreviewBindings は LayoutFieldSchema / ColorFieldSchema とは別ファイルに
// 対応表を持っているので、**項目を足したときの書き忘れ**をここで拾う。
// 併せて、当たり判定の優先順位 H が同梱スキンで破綻していないかも見る。

using System.Drawing;
using SkinEditor.Model;

namespace SkinEditor.Tests;

public class PreviewBindingTests
{
    private static Dictionary<string, Rectangle> RegionsOf(string skinName)
    {
        var doc = SkinDocument.Open(TestPaths.FindDevRoot(), skinName);
        var assets = new SkinEditor.UI.SkinAssetSource(doc);
        return PreviewRegions.Build(doc.Effective,
            name =>
            {
                var a = assets.Find(name);
                return a == null ? null : new Size(a.Width, a.Height);
            },
            volume: 0);
    }

    // 対応アイテムを持たないのは「ミニフォント」だけ（仕様書で
    // 「対応アイテムなし」と明記されている）。
    [Fact]
    public void EveryLayoutFieldHasABinding()
    {
        var regions = RegionsOf("Default");
        foreach (var section in LayoutFieldSchema.BuildSections())
        {
            foreach (var f in section.Fields)
            {
                var b = PreviewBindings.For(f.Section, f.Key);
                if (f.Section == "MiniFont")
                {
                    Assert.True(string.IsNullOrEmpty(b.Region), $"{f.Section}/{f.Key} は対応アイテムなしのはず");
                    continue;
                }
                Assert.False(string.IsNullOrEmpty(b.Region), $"{f.Section}/{f.Key} の対応アイテムが無い");
                Assert.True(regions.ContainsKey(b.Region), $"{f.Section}/{f.Key} -> 未知のアイテム {b.Region}");
                if (!string.IsNullOrEmpty(b.DragParent))
                    Assert.True(regions.ContainsKey(b.DragParent), $"{f.Section}/{f.Key} -> 未知の親 {b.DragParent}");
            }
        }
    }

    // 「1 キーに複数個の値」の行（LabeledValueRow / PcmChannelRow）と、
    // 素材の行・配色のセクション。
    [Fact]
    public void IndexedAndBitmapAndColorBindingsPointAtRealRegions()
    {
        var regions = RegionsOf("Default");

        for (int i = 0; i < 13; i++)
            Assert.True(regions.ContainsKey(PreviewBindings.For("Keyboard", "XOffset", i).Region), $"XOffset {i}");
        for (int i = 0; i < 9; i++)
            Assert.True(regions.ContainsKey(PreviewBindings.For("Keyboard", "ChannelY", i).Region), $"ChannelY {i}");
        for (int i = 0; i < 8; i++)
            Assert.True(regions.ContainsKey(PreviewBindings.For("Status", "PcmX", i).Region), $"PcmX {i}");

        foreach (BitmapRole role in Enum.GetValues<BitmapRole>())
        {
            var b = PreviewBindings.ForBitmap(role);
            if (role == BitmapRole.MiniFont)
            {
                Assert.True(string.IsNullOrEmpty(b.Region), "ミニフォントの素材は対応アイテムなし");
                continue;
            }
            Assert.True(regions.ContainsKey(b.Region), $"素材 {role} -> {b.Region}");
        }

        foreach (var s in ColorFieldSchema.BuildSections())
        {
            var b = PreviewBindings.ForColorSection(s.Title);
            Assert.True(regions.ContainsKey(b.Region), $"配色 {s.Title} -> {b.Region}");
        }
    }

    // 「同じ H のアイテムが重なると、先に書いた行が勝つ」という決まりは、
    // **重なっていること自体が事故**（狙ったほうが選べなくなる）。同梱スキンで
    // そうなっていないことを見る。Phone はスクロールバーがファイラーの矩形に
    // 食い込んでいて、実際に本体でも当たり判定の順番で不具合を出した場所。
    [Theory]
    [InlineData("Default")]
    [InlineData("Phone")]
    public void NoTwoClickableItemsShareHitPriorityWhereTheyOverlap(string skinName)
    {
        var regions = RegionsOf(skinName);
        var clickable = new Dictionary<string, int>();
        void Add(PreviewBinding b)
        {
            if (b.Hit <= 0 || string.IsNullOrEmpty(b.Region)) return;
            // 同じアイテムに違う H が付く（鍵盤の白鍵側 13 / 黒鍵側 12）のは
            // 意図どおり。ここでは「別のアイテム同士」の重なりだけを見たいので
            // 高いほうを代表にする。
            clickable[b.Region] = Math.Max(clickable.TryGetValue(b.Region, out var h) ? h : 0, b.Hit);
        }

        foreach (var section in LayoutFieldSchema.BuildSections())
        {
            foreach (var f in section.Fields) Add(PreviewBindings.For(f.Section, f.Key));
        }
        for (int i = 0; i < 9; i++) Add(PreviewBindings.For("Keyboard", "ChannelY", i));
        foreach (BitmapRole role in Enum.GetValues<BitmapRole>()) Add(PreviewBindings.ForBitmap(role));

        var ids = clickable.Keys.ToList();
        for (int i = 0; i < ids.Count; i++)
        {
            for (int j = i + 1; j < ids.Count; j++)
            {
                if (clickable[ids[i]] != clickable[ids[j]]) continue;
                var a = regions[ids[i]];
                a.Intersect(regions[ids[j]]);
                // Rectangle.IsEmpty は x,y,w,h が全部 0 のときしか真にならない。
                // 辺で接しているだけ（幅か高さが 0）は重なりではないので、
                // 面積で見ること（Default の鍵盤 x=4..343 とステータス x=344.. が
                // ちょうどこれで、IsEmpty で見ると誤検出する）。
                Assert.True(a.Width <= 0 || a.Height <= 0,
                    $"{skinName}: {ids[i]} と {ids[j]} が同じ H={clickable[ids[i]]} で重なっている ({a})");
            }
        }
    }

    // ドラッグの往復。「アイテムの左上を (X,Y) へ動かす」と書き戻して、
    // 実際にその位置へ来ることを全ドラッグ項目で確かめる。相対値の原点
    // （DragParent）を取り違えるとここで落ちる（PCM の「音量」「ポインタ」で
    // 実際に踏んだ。囲み矩形の左上と、値の原点は別物）。
    [Fact]
    public void DraggingAnItemPutsItsTopLeftWhereAsked()
    {
        var root = TestPaths.FindDevRoot();
        var name = "TestScratch_" + Guid.NewGuid().ToString("N")[..8];
        var doc = SkinDocument.CreateNew(root, name, "Default");
        try
        {
            var assets = new SkinEditor.UI.SkinAssetSource(doc);
            Dictionary<string, Rectangle> Regions() => PreviewRegions.Build(doc.Effective,
                n => { var a = assets.Find(n); return a == null ? null : new Size(a.Width, a.Height); }, 0);

            foreach (var section in LayoutFieldSchema.BuildSections())
            {
                foreach (var f in section.Fields)
                {
                    var b = PreviewBindings.For(f.Section, f.Key);
                    if (b.Drag == PreviewDrag.None) continue;

                    // 元の位置から少しずらした先を狙う（0,0 だと、たまたま
                    // 合っているだけの式でも通ってしまう）。
                    var before = Regions()[b.Region];
                    int tx = before.X + 17, ty = before.Y + 23;
                    var value = PreviewDragMath.ValueFor(b, Regions(), tx, ty);
                    Assert.NotNull(value);
                    doc.SetLayoutRaw(f.Section, f.Key, value!);

                    var after = Regions()[b.Region];
                    Assert.True(after.X == tx && after.Y == ty,
                        $"{f.Section}/{f.Key}: ({tx},{ty}) へ動かしたのに ({after.X},{after.Y}) になった");
                }
            }
        }
        finally
        {
            // 後始末は既存の SkinDocumentTests と同じ作法で。
            if (Directory.Exists(doc.OwnDir)) Directory.Delete(doc.OwnDir, true);
        }
    }

    // 操作ボタンの Rect の w,h は「使うボタン全体を覆う大きさ」なので、
    // 各ボタンのアイテムはその中に収まっていなければならない。
    [Theory]
    [InlineData("Default")]
    [InlineData("Phone")]
    public void PlayKeyButtonsFitInsideThePlayKeyRect(string skinName)
    {
        var doc = SkinDocument.Open(TestPaths.FindDevRoot(), skinName);
        var regions = RegionsOf(skinName);
        var all = regions[PreviewRegions.Ids.PlayKey];
        for (int i = 0; i < doc.Effective.numPlayKeys; i++)
        {
            var btn = regions[PreviewRegions.Ids.Indexed(PreviewRegions.Ids.PlayKeyButton, i)];
            Assert.True(all.Contains(btn), $"{skinName}: ボタン {i} {btn} が {all} からはみ出している");
        }
    }
}
