using SkinEditor.Model;

namespace SkinEditor.Tests;

public class DebugListTest
{
    [Fact]
    public void ListSkinNames_ContainsAllThree()
    {
        var root = TestPaths.FindDevRoot();
        var names = root.ListSkinNames();
        Assert.Equal(new[] { "Default", "Default-Midnight", "Phone" }, names);
    }
}
