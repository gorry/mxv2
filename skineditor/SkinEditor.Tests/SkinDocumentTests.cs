// mxv2 本体の同梱スキン (Default / Default-Midnight / Phone) を実際に開いて、
// Base 鎖の解決と実効値が本体の skin.cpp / colors.cpp と同じ結果になることを
// 確認する回帰テスト。

using SkinEditor.Model;

namespace SkinEditor.Tests;

public class SkinDocumentTests
{
    [Fact]
    public void Default_MatchesLayoutIniLiterally()
    {
        var root = TestPaths.FindDevRoot();
        var doc = SkinDocument.Open(root, "Default");

        Assert.Equal(640, doc.Effective.screenW);
        Assert.Equal(480, doc.Effective.screenH);
        Assert.Equal(new[] { 11, 8 }, doc.Effective.fileListRows);
        Assert.Equal(8, doc.Effective.numPlayKeys);
        Assert.Equal("", doc.BaseRef);
    }

    [Fact]
    public void DefaultMidnight_InheritsLayoutFromDefault_ButHasOwnColors()
    {
        var root = TestPaths.FindDevRoot();
        var def = SkinDocument.Open(root, "Default");
        var midnight = SkinDocument.Open(root, "Default-Midnight");

        Assert.Equal("assets:Default", midnight.BaseRef);
        // layout.ini は Base=assets:Default の2行だけなので、実効値は Default と完全に一致するはず。
        Assert.True(ReflectionEquals.DeepEquals(def.Effective, midnight.Effective));

        // colors.ini は自分のものを持っている（Default とは異なる配色）。
        Assert.True(midnight.HasOwnColors);
        Assert.False(ReflectionEquals.DeepEquals(def.EffectiveColors, midnight.EffectiveColors));
    }

    [Fact]
    public void Phone_HasOwnLayoutAndDiffersFromDefault()
    {
        var root = TestPaths.FindDevRoot();
        var phone = SkinDocument.Open(root, "Phone");

        Assert.Equal(480, phone.Effective.screenW);
        Assert.Equal(720, phone.Effective.screenH);
        Assert.Equal(new[] { 15, 10 }, phone.Effective.fileListRows);
        Assert.Equal("", phone.BaseRef);
    }

    [Fact]
    public void SwitchBaseOff_ThenOn_RoundTripsToEquivalentEffectiveValues()
    {
        var root = TestPaths.FindDevRoot();
        var name = "TestScratch_" + Guid.NewGuid().ToString("N")[..8];
        var doc = SkinDocument.CreateNew(root, name, "Default-Midnight");
        try
        {
            var beforeLayout = doc.Effective;
            var beforeColors = doc.EffectiveColors;

            doc.SwitchBaseOff();
            Assert.Equal("", doc.BaseRef);
            Assert.True(ReflectionEquals.DeepEquals(beforeLayout, doc.Effective));
            Assert.True(ReflectionEquals.DeepEquals(beforeColors, doc.EffectiveColors));

            // 主要なビットマップが実ファイルとしてコピーされていること。
            Assert.True(File.Exists(Path.Combine(doc.OwnDir, "back.bmp")));
            Assert.True(File.Exists(Path.Combine(doc.OwnDir, "playkey.bmp")));

            doc.SwitchBaseOn("Default-Midnight");
            Assert.Equal("assets:Default-Midnight", doc.BaseRef);
            Assert.True(ReflectionEquals.DeepEquals(beforeLayout, doc.Effective));
        }
        finally
        {
            if (Directory.Exists(doc.OwnDir)) Directory.Delete(doc.OwnDir, true);
        }
    }

    [Fact]
    public void SwitchBaseOff_EvacuatesDivergedOwnFile_ToNouseFolder()
    {
        var root = TestPaths.FindDevRoot();
        var name = "TestScratch_" + Guid.NewGuid().ToString("N")[..8];
        var doc = SkinDocument.CreateNew(root, name, "Default");
        try
        {
            // 参照が生きている間に「自スキン」扱いだった素材ファイルを装う
            // （中身は Default の back.bmp とは絶対に異なるものにする）。
            Directory.CreateDirectory(doc.OwnDir);
            var destPath = Path.Combine(doc.OwnDir, "back.bmp");
            var stray = new byte[] { 1, 2, 3, 4 };
            File.WriteAllBytes(destPath, stray);

            doc.SwitchBaseOff();

            var nousePath = Path.Combine(doc.OwnDir, "_nouse", "back.bmp");
            Assert.True(File.Exists(nousePath));
            Assert.Equal(stray, File.ReadAllBytes(nousePath));

            // 差し替え後の back.bmp は参照先 (Default) のものと同じになっている。
            var defaultDir = root.SkinDir("Default")!;
            Assert.True(NouseFolder.FilesEqual(destPath, Path.Combine(defaultDir, "back.bmp")));
        }
        finally
        {
            if (Directory.Exists(doc.OwnDir)) Directory.Delete(doc.OwnDir, true);
        }
    }

    [Fact]
    public void SwitchBaseOff_SkipsEvacuation_WhenOwnFileAlreadyMatchesBase()
    {
        var root = TestPaths.FindDevRoot();
        var name = "TestScratch_" + Guid.NewGuid().ToString("N")[..8];
        var doc = SkinDocument.CreateNew(root, name, "Default");
        try
        {
            var defaultDir = root.SkinDir("Default")!;
            Directory.CreateDirectory(doc.OwnDir);
            var destPath = Path.Combine(doc.OwnDir, "back.bmp");
            File.Copy(Path.Combine(defaultDir, "back.bmp"), destPath);

            doc.SwitchBaseOff();

            // 中身が参照先と同じなのでコピー不要 = 退避も不要（ユーザー指示）。
            Assert.False(Directory.Exists(Path.Combine(doc.OwnDir, "_nouse")));
            Assert.True(File.Exists(destPath));
        }
        finally
        {
            if (Directory.Exists(doc.OwnDir)) Directory.Delete(doc.OwnDir, true);
        }
    }
}
