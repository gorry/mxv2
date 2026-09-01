using SkinEditor.Model;

namespace SkinEditor.Tests;

public class IniDocumentTests
{
    [Fact]
    public void LoadThenGet_ReturnsWrittenValues()
    {
        var dir = Path.Combine(Path.GetTempPath(), "SkinEditorTests_" + Guid.NewGuid());
        Directory.CreateDirectory(dir);
        var path = Path.Combine(dir, "t.ini");
        File.WriteAllText(path, "[Screen]\nWidth=640\nHeight=480\n; comment\n[Skin]\nBase=assets:Default\n");

        var ini = new IniDocument();
        Assert.True(ini.Load(path));
        Assert.Equal(640, ini.GetInt("Screen", "Width", -1));
        Assert.Equal(480, ini.GetInt("Screen", "Height", -1));
        Assert.Equal("assets:Default", ini.GetString("Skin", "Base", ""));
        Assert.False(ini.Has("Screen", "NoSuchKey"));

        Directory.Delete(dir, true);
    }

    [Fact]
    public void SaveThenLoad_RoundTrips()
    {
        var dir = Path.Combine(Path.GetTempPath(), "SkinEditorTests_" + Guid.NewGuid());
        Directory.CreateDirectory(dir);
        var path = Path.Combine(dir, "t.ini");

        var ini = new IniDocument();
        ini.SetInt("A", "X", 1);
        ini.SetString("A", "Y", "hello");
        ini.SetInt("B", "Z", -5);
        ini.Save(path);

        var reloaded = new IniDocument();
        reloaded.Load(path);
        Assert.Equal(1, reloaded.GetInt("A", "X", 0));
        Assert.Equal("hello", reloaded.GetString("A", "Y", ""));
        Assert.Equal(-5, reloaded.GetInt("B", "Z", 0));

        Directory.Delete(dir, true);
    }

    [Fact]
    public void Remove_DropsKeyFromOrderAndValues()
    {
        var ini = new IniDocument();
        ini.SetInt("A", "X", 1);
        ini.SetInt("A", "Y", 2);
        ini.Remove("A", "X");
        Assert.False(ini.Has("A", "X"));
        Assert.Equal(new[] { "Y" }, ini.Keys("A"));
    }
}
