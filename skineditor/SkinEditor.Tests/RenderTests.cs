// mxv2 本体の描画を移植した Model/Render/ の回帰テスト。
//
// ブリッタの合成は readme.md の「背景に対する乗算合成（ゲイン）」の表
// （0=黒 / 100=素通し / 200=白）がそのまま仕様なので、そこを固定する。

using SkinEditor.Model;
using SkinEditor.Model.Render;
using SkinEditor.UI;

namespace SkinEditor.Tests;

public class RenderTests
{
    private static RenderBitmap Rgb24(int w, int h, int r, int g, int b)
    {
        var bmp = new RenderBitmap();
        bmp.Create(w, h, 24);
        Blitter.Fill(bmp, 0, 0, w, h, r, g, b, 100);
        return bmp;
    }

    private static (int r, int g, int b) PixelAt(RenderBitmap bmp, int x, int y)
    {
        int o = bmp.RowFromTop(y) + x * 3;
        return (bmp.Bits[o + 2], bmp.Bits[o + 1], bmp.Bits[o]);
    }

    // プレビューの文字が出るには、フォントが見つかっていることが要る。
    //
    // 自分の font.ttf を持つのは Default だけで、他のスキンは同梱の
    // MPLUS1p-Regular.ttf へ落ちる。エディタ側が同梱フォントの名前を
    // 見ていなかったため、**Phone のプレビューだけ曲名とファイラーの文字が
    // 出なかった**（2026-09-09）。本体 src/textrender.cpp と同じ 2 段の
    // 探し方になっていることを、同梱スキン全部で押さえる。
    [Theory]
    [InlineData("Default")]
    [InlineData("Default-Midnight")]
    [InlineData("Phone")]
    public void EveryBundledSkinResolvesAFont(string skinName)
    {
        var root = TestPaths.FindDevRoot();
        var doc = SkinDocument.Open(root, skinName);
        var assets = new SkinAssetSource(doc);

        var path = assets.FindFontFile(root.AssetsDir);
        Assert.False(string.IsNullOrEmpty(path), $"{skinName}: フォントが見つからない");
        Assert.True(File.Exists(path), $"{skinName}: {path} が無い");
    }

    [Fact]
    public void Fill_Alpha100_WritesTheColor()
    {
        var bmp = Rgb24(4, 4, 10, 20, 30);
        Assert.Equal((10, 20, 30), PixelAt(bmp, 0, 0));
        Assert.Equal((10, 20, 30), PixelAt(bmp, 3, 3));
    }

    // 乗算のゲイン: 0 = 黒 / 100 = 素通し / 200 = 白（readme.md の表）。
    [Theory]
    [InlineData(0, 0)]
    [InlineData(50, 100)]
    [InlineData(100, 200)]
    [InlineData(150, 227)]
    [InlineData(200, 255)]
    public void Fill_Mul_FollowsTheGainTable(int gain, int expected)
    {
        var bmp = Rgb24(2, 2, 200, 200, 200);
        Blitter.Fill(bmp, 0, 0, 2, 2, gain, gain, gain, Blend.Mul);
        Assert.Equal((expected, expected, expected), PixelAt(bmp, 0, 0));
    }

    // alpha 0..100 は「元の画素と混ぜる」。d - (d-s)*alpha/100。
    [Fact]
    public void Fill_Alpha50_MixesHalfway()
    {
        var bmp = Rgb24(2, 2, 200, 200, 200);
        Blitter.Fill(bmp, 0, 0, 2, 2, 100, 100, 100, 50);
        Assert.Equal((150, 150, 150), PixelAt(bmp, 0, 0));
    }

    // FillMul は「乗算した色を alpha で乗せる」。alpha=100 は素の乗算と同じ。
    [Fact]
    public void FillMul_Alpha100_SameAsMul()
    {
        var a = Rgb24(2, 2, 200, 200, 200);
        var b = Rgb24(2, 2, 200, 200, 200);
        Blitter.Fill(a, 0, 0, 2, 2, 50, 50, 50, Blend.Mul);
        Blitter.FillMul(b, 0, 0, 2, 2, 50, 50, 50, 100);
        Assert.Equal(PixelAt(a, 0, 0), PixelAt(b, 0, 0));
    }

    [Fact]
    public void FillMul_Alpha0_DoesNothing()
    {
        var bmp = Rgb24(2, 2, 200, 100, 50);
        Blitter.FillMul(bmp, 0, 0, 2, 2, 0, 0, 0, 0);
        Assert.Equal((200, 100, 50), PixelAt(bmp, 0, 0));
    }

    // パレット 0 は透明（本体 BmpCopyTransparent）。
    [Fact]
    public void CopyTransparent_SkipsPaletteZero()
    {
        var dst = Rgb24(2, 1, 10, 10, 10);
        var src = new RenderBitmap();
        src.Create(2, 1, 8);
        src.SetPalette(1, 200, 150, 100);
        int o = src.RowFromTop(0);
        src.Bits[o] = 0;      // 透明
        src.Bits[o + 1] = 1;  // 不透明
        Blitter.CopyTransparent(dst, 0, 0, 2, 1, src, 0, 0, 100);
        Assert.Equal((10, 10, 10), PixelAt(dst, 0, 0));
        Assert.Equal((200, 150, 100), PixelAt(dst, 1, 0));
    }

    // 合成コピー: src1 のパレット 0 の画素は src2 がそのまま出る。
    [Fact]
    public void CopyComposite_PassesThroughUnderlay()
    {
        var dst = Rgb24(2, 1, 0, 0, 0);
        var under = Rgb24(2, 1, 40, 50, 60);
        var src = new RenderBitmap();
        src.Create(2, 1, 8);
        src.SetPalette(1, 200, 200, 200);
        int o = src.RowFromTop(0);
        src.Bits[o] = 0;
        src.Bits[o + 1] = 1;
        Blitter.CopyComposite(dst, 0, 0, 2, 1, src, 0, 0, under, 0, 0, 100);
        Assert.Equal((40, 50, 60), PixelAt(dst, 0, 0));
        Assert.Equal((200, 200, 200), PixelAt(dst, 1, 0));
    }

    // 素材は 8bpp のパレットを保ったまま読めていること（ここを落とすと
    // 鍵盤・LED・レベルメータが本体と同じに描けない）。
    [Fact]
    public void BmpLoader_KeepsIndexedPalette()
    {
        var root = TestPaths.FindDevRoot();
        var dir = root.SkinDir("Default");
        Assert.NotNull(dir);

        var meter = BmpLoader.Load(Path.Combine(dir!, "levelmeter.bmp"));
        Assert.NotNull(meter);
        Assert.Equal(8, meter!.BitCount);
        Assert.Equal(128, meter.Width);
        // 32〜95 が点灯色、96〜159 が消灯色（readme.md「素材ビットマップの約束事」）。
        Assert.NotEqual(meter.Palette[32].ToColorRef(), meter.Palette[96].ToColorRef());
    }

    private static DrawScreenPort Render(string skinName, out SkinDocument doc)
    {
        var root = TestPaths.FindDevRoot();
        doc = SkinDocument.Open(root, skinName);
        var port = new DrawScreenPort(doc.Placed, doc.EffectiveColors, new SkinAssetSource(doc));
        port.Reload("テスト");
        port.PutPlayKey(DrawScreenPort.PlayKeyPlayLed);
        port.PutProgressBar(0, 180000);
        port.PutTotalVolBar(0);
        port.PutScrollBar(0, 100);
        return port;
    }

    [Fact]
    public void Render_Default_ProducesCanvasOfSkinSize()
    {
        var port = Render("Default", out var doc);
        Assert.Equal(640, port.Width);
        Assert.Equal(480, port.Height);
        Assert.Equal(640, port.Screen.Width);
        Assert.Equal(480, port.Screen.Height);
        Assert.Equal(24, port.Screen.BitCount);

        // 単色ではない（＝何かが描かれている）。
        var first = PixelAt(port.Screen, 0, 0);
        bool varied = false;
        for (int y = 0; y < port.Height && !varied; y += 7)
        {
            for (int x = 0; x < port.Width; x += 7)
            {
                if (PixelAt(port.Screen, x, y) != first) { varied = true; break; }
            }
        }
        Assert.True(varied);
        Assert.NotEmpty(port.TextDraws);  // 曲名とファイラーの文字が積まれている
        GC.KeepAlive(doc);
    }

    [Fact]
    public void Render_Phone_UsesItsOwnScreenSize()
    {
        var port = Render("Phone", out _);
        Assert.Equal(480, port.Width);
        Assert.Equal(720, port.Height);
    }

    // 配色だけ違うスキンは、レイアウトが同じでも絵が変わる
    // （= colors.ini が実際に描画へ効いている）。
    [Fact]
    public void Render_DefaultMidnight_DiffersFromDefault()
    {
        var a = Render("Default", out _);
        var b = Render("Default-Midnight", out _);
        Assert.Equal(a.Width, b.Width);
        Assert.Equal(a.Height, b.Height);

        int diff = 0;
        for (int y = 0; y < a.Height; y += 3)
        {
            for (int x = 0; x < a.Width; x += 3)
            {
                if (PixelAt(a.Screen, x, y) != PixelAt(b.Screen, x, y)) diff++;
            }
        }
        Assert.True(diff > 1000, $"配色の差が画面に出ていない (diff={diff})");
    }
}
