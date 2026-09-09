using SkinEditor.Model;

namespace SkinEditor.Tests;

public class DebugListTest
{
    // 同梱スキンは増える（2026-09-09 に横長の Phone-R が加わった）ので、
    // 一覧そのものではなく「昔からある 3 つが、名前順のまま並んでいること」を
    // 見る。ここで確かめたいのは ListSkinNames が assets/skin/ を正しく
    // 拾えているかであって、同梱スキンの本数ではない。
    [Fact]
    public void ListSkinNames_FindsTheBundledSkinsInOrder()
    {
        var root = TestPaths.FindDevRoot();
        var names = root.ListSkinNames();

        foreach (var want in new[] { "Default", "Default-Midnight", "Phone" })
        {
            Assert.Contains(want, names);
        }
        Assert.Equal(names.OrderBy(n => n, StringComparer.Ordinal).ToList(), names.ToList());
        Assert.Equal(names.Distinct().Count(), names.Count);
    }
}
