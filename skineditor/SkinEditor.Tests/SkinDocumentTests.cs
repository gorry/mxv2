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
        Assert.Equal("minifont.bmp", doc.Effective.miniFontBitmap);
        Assert.Equal(6, doc.Effective.miniFontW);
        Assert.Equal("", doc.BaseRef);
    }

    // ステータス欄の項目ごとの位置は 2026-09-03 に drawscreen.cpp から
    // [Status] Pos<名前> へ出したもの。Default の値は旧 mxv の draw.cpp
    // （CX_D / CY_D）そのままで、既定値とも一致する。
    [Fact]
    public void Default_HasStatusItemPositionsFromLayoutIni()
    {
        var root = TestPaths.FindDevRoot();
        var doc = SkinDocument.Open(root, "Default");

        Assert.Equal(new[] { 2, 0 }, doc.Effective.statusPos[(int)StatusItem.Volume]);
        Assert.Equal(new[] { 26, 0 }, doc.Effective.statusPos[(int)StatusItem.LevelMeter]);
        Assert.Equal(new[] { 122, 0 }, doc.Effective.statusPos[(int)StatusItem.Panpot]);
        Assert.Equal(new[] { 104, 18 }, doc.Effective.statusPos[(int)StatusItem.LFOPitch4]);
        Assert.Equal(new[] { 24, 0 }, doc.Effective.statusPos[(int)StatusItem.PcmPtr]);
        Assert.Equal(32, doc.Effective.levelMeterSrcX);

        // 全項目が layout.ini に明示してあること（本体の既定値と二重管理に
        // なっていても、書き忘れた項目があれば気付けるように）。
        foreach (var key in StatusItems.Keys) Assert.True(doc.IsLayoutOwn("Status", key), key);
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

    // [Status] と [PlayKey] の矩形（2026-09-04 に [Status] Pos/BackWidth/
    // BackHeight と [PlayKey] Pos を Rect 1 つへまとめた）。**Phone の値を
    // 見るのは、既定値と違う値だから**——Default の [Status] は既定値と
    // 同じなので、キー名を間違えて既定値へ落ちていても気付けない。
    [Fact]
    public void Phone_ReadsStatusAndPlayKeyRects()
    {
        var root = TestPaths.FindDevRoot();
        var e = SkinDocument.Open(root, "Phone").Effective;

        // 使う 8 個のボタンを覆う大きさが Rect の w,h になっている。
        Assert.Equal((168, 348, 304, 82), (e.playKeyX, e.playKeyY, e.playKeyW, e.playKeyH));
        int coverW = 0, coverH = 0;
        for (int i = 0; i < e.numPlayKeys; i++)
        {
            coverW = Math.Max(coverW, e.playKeyPos[i][0] + e.playKeyRect[i].W);
            coverH = Math.Max(coverH, e.playKeyPos[i][1] + e.playKeyRect[i].H);
        }
        Assert.Equal((coverW, coverH), (e.playKeyW, e.playKeyH));

        Assert.Equal((344, 4, 128, 35), (e.statusX, e.statusY, e.statusW, e.statusH));
    }

    // Rect のキー名が読み書きでずれていないこと（WriteAll は「参照 → 無参照」で
    // 実効値を全キー書き出すときに通る道なので、ここがずれると値が既定値へ
    // 化ける）。値は既定値と違うものを入れて確かめる。
    [Fact]
    public void WriteAll_ThenApplyLayout_RoundTripsRects()
    {
        var src = new SkinLayout
        {
            statusX = 11, statusY = 22, statusW = 33, statusH = 44,
            playKeyX = 55, playKeyY = 66, playKeyW = 77, playKeyH = 88,
        };
        var ini = new IniDocument();
        SkinLayoutIo.WriteAll(src, ini);

        var dst = new SkinLayout();
        SkinLayoutIo.ApplyLayout(ini, dst);

        Assert.Equal((11, 22, 33, 44), (dst.statusX, dst.statusY, dst.statusW, dst.statusH));
        Assert.Equal((55, 66, 77, 88), (dst.playKeyX, dst.playKeyY, dst.playKeyW, dst.playKeyH));
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
