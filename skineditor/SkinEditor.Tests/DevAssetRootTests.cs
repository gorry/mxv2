// ユーザーフォルダモード（mxv2.exe の隣で起動したとき）の置き場所の解決。
// 実物の同梱スキンは開発フォルダの assets/skin/ を使い、ユーザーフォルダは
// 一時フォルダに作る（%APPDATA%\mxv2 には触らない）。

using SkinEditor.Model;

namespace SkinEditor.Tests;

public class DevAssetRootTests : IDisposable
{
    private readonly string _tmp;
    private readonly string _exeDir;   // mxv2.exe の隣を模したフォルダ（assets/ は開発フォルダへのコピー）
    private readonly string _userDir;  // ユーザーフォルダを模したフォルダ

    public DevAssetRootTests()
    {
        _tmp = Path.Combine(Path.GetTempPath(), "mxv2-skineditor-test-" + Guid.NewGuid().ToString("N"));
        _exeDir = Path.Combine(_tmp, "exe");
        _userDir = Path.Combine(_tmp, "user");
        Directory.CreateDirectory(_exeDir);
        Directory.CreateDirectory(_userDir);

        // 同梱スキンは Default と Default-Midnight だけあれば足りる（layout.ini と
        // colors.ini があればよく、素材の bmp までは要らない）。
        var dev = TestPaths.FindDevRoot();
        foreach (var name in new[] { "Default", "Default-Midnight" })
        {
            var src = Path.Combine(dev.BundledSkinRootDir, name);
            var dst = Path.Combine(_exeDir, "assets", "skin", name);
            Directory.CreateDirectory(dst);
            foreach (var f in new[] { "layout.ini", "colors.ini" })
            {
                var p = Path.Combine(src, f);
                if (File.Exists(p)) File.Copy(p, Path.Combine(dst, f));
            }
        }
    }

    public void Dispose()
    {
        try { Directory.Delete(_tmp, true); } catch { }
    }

    private DevAssetRoot UserRoot()
    {
        var det = DevAssetRoot.Detect(new[] { _exeDir }, _userDir);
        Assert.Equal(AssetRootKind.ExeFolder, det.Kind);
        Assert.NotNull(det.Root);
        return det.Root!;
    }

    [Fact]
    public void Detect_FolderWithoutCMakeLists_IsExeFolder_AndEditsUserSkins()
    {
        var root = UserRoot();
        Assert.True(root.IsUserMode);
        Assert.Equal(Path.Combine(_userDir, "skin"), root.SkinRootDir);
        Assert.Equal(Path.Combine(_exeDir, "assets", "skin"), root.BundledSkinRootDir);
        // まだユーザーのスキンは無い。同梱ぶんは参照先の候補にだけ出る。
        Assert.Empty(root.ListSkinNames());
        Assert.Equal(new[] { "assets:Default", "assets:Default-Midnight" }, root.ListBaseRefs());
    }

    [Fact]
    public void Detect_DevFolder_StillEditsBundledSkins()
    {
        var dev = TestPaths.FindDevRoot();
        Assert.False(dev.IsUserMode);
        Assert.Equal(dev.BundledSkinRootDir, dev.SkinRootDir);
        Assert.Equal("assets:Default", dev.CanonicalRef("Default"));
        Assert.Equal("assets:Default", dev.CanonicalRefOf("Default"));
        Assert.Equal("Default", dev.DisplayRef("assets:Default"));
    }

    [Fact]
    public void UserMode_RefsResolveLikeMxv2()
    {
        var root = UserRoot();

        // 接頭辞なしでユーザーのスキンが無ければ同梱へ落ちる（本体と同じ）。
        Assert.Equal(Path.Combine(_exeDir, "assets", "skin", "Default"), root.ResolveRefDir("Default"));
        Assert.Equal(Path.Combine(_exeDir, "assets", "skin", "Default"), root.ResolveRefDir("assets:Default"));
        Assert.Equal("assets:Default", root.CanonicalRefOf("Default"));
        Assert.Null(root.ResolveRefDir("NoSuchSkin"));

        // ユーザーのスキン "Default" を作ると、接頭辞なしはそちらを指し、
        // "assets:Default" は変わらず同梱を指す。
        var mine = Path.Combine(root.SkinRootDir, "Default");
        Directory.CreateDirectory(mine);
        File.WriteAllText(Path.Combine(mine, "layout.ini"), "[Skin]\nBase=assets:Default\n");
        Assert.Equal(mine, root.ResolveRefDir("Default"));
        Assert.Equal(Path.Combine(_exeDir, "assets", "skin", "Default"), root.ResolveRefDir("assets:Default"));
        Assert.Equal("Default", root.CanonicalRefOf("Default"));
        Assert.Equal("Default", root.CanonicalRef("Default"));
        Assert.Equal(new[] { "Default", "assets:Default", "assets:Default-Midnight" }, root.ListBaseRefs());
        Assert.Equal("assets:Default", root.DisplayRef("assets:Default"));
        Assert.Equal("Default", root.DisplayRef("Default"));
    }

    [Fact]
    public void UserMode_NewSkinReferringToBundled_ResolvesChainAndWritesPlainBase()
    {
        var root = UserRoot();

        // 同梱の Default-Midnight（→ assets:Default）を土台にした新規スキン。
        var doc = SkinDocument.CreateNew(root, "Mine", "assets:Default-Midnight");
        Assert.Equal("assets:Default-Midnight", doc.BaseRef);
        Assert.Equal(640, doc.Effective.screenW);
        Assert.True(doc.EffectiveColors != null);
        doc.Save();
        Assert.True(File.Exists(Path.Combine(root.SkinRootDir, "Mine", "layout.ini")));

        // ユーザーのスキンを土台にする 2 つ目。Base は接頭辞なしで書かれる。
        var doc2 = SkinDocument.CreateNew(root, "Mine2", "Mine");
        Assert.Equal("Mine", doc2.BaseRef);
        Assert.Equal(640, doc2.Effective.screenW);
        // 候補には自分以外のユーザーのスキンと同梱のスキンが並ぶ。
        Assert.Equal(new[] { "Mine", "assets:Default", "assets:Default-Midnight" }, doc2.ListBaseCandidates());

        // 自分と同じ名前の同梱スキンは自分ではない（循環ではない）。
        var same = SkinDocument.CreateNew(root, "Default", "assets:Default");
        Assert.False(same.BaseCycleDetected);
        Assert.Equal(640, same.Effective.screenW);
        Assert.Equal(new[] { "Mine", "assets:Default", "assets:Default-Midnight" },
            same.ListBaseCandidates().Where(r => r != "Mine2").ToArray());
    }
}
