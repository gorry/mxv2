using SkinEditor.Model;

namespace SkinEditor.Tests;

public class NouseFolderTests
{
    [Fact]
    public void Evacuate_MovesFileIntoNouseSubfolder()
    {
        var dir = Directory.CreateTempSubdirectory("nousetest_").FullName;
        try
        {
            var src = Path.Combine(dir, "a.bmp");
            File.WriteAllBytes(src, new byte[] { 9, 9 });

            NouseFolder.Evacuate(dir, src);

            Assert.False(File.Exists(src));
            var dest = Path.Combine(dir, "_nouse", "a.bmp");
            Assert.True(File.Exists(dest));
            Assert.Equal(new byte[] { 9, 9 }, File.ReadAllBytes(dest));
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    [Fact]
    public void Evacuate_AppendsTimestamp_WhenSameNameAlreadyInNouse()
    {
        var dir = Directory.CreateTempSubdirectory("nousetest_").FullName;
        try
        {
            var nouseDir = Path.Combine(dir, NouseFolder.DirName);
            Directory.CreateDirectory(nouseDir);
            File.WriteAllBytes(Path.Combine(nouseDir, "a.bmp"), new byte[] { 1 });

            var src = Path.Combine(dir, "a.bmp");
            File.WriteAllBytes(src, new byte[] { 2 });

            NouseFolder.Evacuate(dir, src);

            Assert.False(File.Exists(src));
            var entries = Directory.GetFiles(nouseDir);
            Assert.Equal(2, entries.Length);
            // 元からあったものは上書きされていない。
            Assert.Equal(new byte[] { 1 }, File.ReadAllBytes(Path.Combine(nouseDir, "a.bmp")));
            // 新しく退避したものは別名で残っている。
            var other = entries.Single(p => Path.GetFileName(p) != "a.bmp");
            Assert.Equal(new byte[] { 2 }, File.ReadAllBytes(other));
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    [Fact]
    public void FilesEqual_ComparesByteContent()
    {
        var dir = Directory.CreateTempSubdirectory("nousetest_").FullName;
        try
        {
            var a = Path.Combine(dir, "a.bin");
            var b = Path.Combine(dir, "b.bin");
            var c = Path.Combine(dir, "c.bin");
            File.WriteAllBytes(a, new byte[] { 1, 2, 3 });
            File.WriteAllBytes(b, new byte[] { 1, 2, 3 });
            File.WriteAllBytes(c, new byte[] { 1, 2, 4 });

            Assert.True(NouseFolder.FilesEqual(a, b));
            Assert.False(NouseFolder.FilesEqual(a, c));
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }
}
