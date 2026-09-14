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

    // コメント・空行・並びを保ったまま値だけ差し替える（2026-09-15、ユーザーの指示。
    // 開発フォルダモードで同梱スキンを保存すると説明コメントが全部消えていた）。
    [Fact]
    public void Save_KeepsCommentsBlankLinesAndOrder()
    {
        var ini = new IniDocument();
        ini.LoadText(
            "; head comment\n" +
            "\n" +
            "[Screen]\n" +
            "; width comment\n" +
            "Width = 640\n" +
            "Height=480\n" +
            "\n" +
            "; tail comment of Screen\n" +
            "[Skin]\n" +
            "Base=assets:Default\n");

        ini.SetInt("Screen", "Width", 720);          // 既存: その場で差し替え
        ini.SetString("Screen", "FilerSide", "Top"); // 新規: 最後のキー行の直後
        ini.Remove("Skin", "Base");                  // 削除: 行ごと消える
        ini.SetInt("Font", "Size", 12);              // 新セクション: 末尾

        Assert.Equal(
            "; head comment\n" +
            "\n" +
            "[Screen]\n" +
            "; width comment\n" +
            "Width=720\n" +
            "Height=480\n" +
            "FilerSide=Top\n" +
            "\n" +
            "; tail comment of Screen\n" +
            "[Skin]\n" +
            "\n" +
            "[Font]\n" +
            "Size=12\n",
            ini.ToText());
    }

    // 同梱スキンの layout.ini は、読んで書き戻しても中身が変わらない
    // （コメントも並びもそのまま。改行だけは LF に揃える——作業ツリーが git の
    // autocrlf で CRLF になっていることがあるので、比べる前に揃える）。
    [Theory]
    [InlineData("Default")]
    [InlineData("Phone")]
    public void BundledLayout_RoundTripsByteForByte(string skin)
    {
        var root = TestPaths.FindDevRoot();
        var path = Path.Combine(root.BundledSkinRootDir, skin, "layout.ini");
        var original = File.ReadAllText(path).Replace("\r\n", "\n");
        var ini = new IniDocument();
        Assert.True(ini.Load(path));
        Assert.Equal(original, ini.ToText());
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
