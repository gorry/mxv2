// 移植した描画が mxv2 本体と同じ絵になっているかを、**本物のスクリーン
// ショットと画素で突き合わせる**テスト。
//
// 参照画像 refimage/ref-Default.png は、mxv2.exe を
//   mxv2.exe -skin Default -zoom 100 -userdir <スクラッチ>
// で起動してクライアント領域 (640x480) を撮ったもの。
//
// **同梱スキンの素材・配色・レイアウトか、本体の描画を変えたら撮り直す。**
// 撮り直しは refimage/capture-ref.ps1（mxv2.exe の起動から保存まで自動）:
//   powershell -ExecutionPolicy Bypass -File skineditor/SkinEditor.Tests/refimage/capture-ref.ps1
//
// このテストが落ちたときは、次のどれかを見分けること:
//   (1) 移植 (Model/Render/) が本体とずれた           -> 移植を直す
//   (2) 本体の描画を変えた（移植も直した）             -> 参照画像を撮り直す
//   (3) 同梱スキンの素材・配色・レイアウトを変えた     -> 参照画像を撮り直す
//
// 比較するのは**演奏していない状態で決まりきっている場所だけ**:
//   - バナー（素材を乗算合成しただけ）
//   - ステータス欄 9 段（演奏前は全部 0。ミニフォントとレベルメータを含む）
//   - 鍵盤 9 段（押鍵なし。下地の乗算合成）
// 除外するのは、実行時の状態で変わる場所:
//   - ファイラー（本物は実際のフォルダの中身、プレビューはダミー）
//   - 曲名（本物は空、プレビューは見本の文字）
//   - 操作ボタン / プログレスバー / 音量バー / スクロールバー
//     （LED・再生位置・音量・行数が mxv2.ini 次第）

using System.Drawing;
using System.Drawing.Imaging;
using SkinEditor.Model;
using SkinEditor.Model.Render;
using SkinEditor.UI;

namespace SkinEditor.Tests;

public class RenderCompareTests
{
    private static (int r, int g, int b) PixelAt(RenderBitmap bmp, int x, int y)
    {
        int o = bmp.RowFromTop(y) + x * 3;
        return (bmp.Bits[o + 2], bmp.Bits[o + 1], bmp.Bits[o]);
    }

    [Fact]
    public void Default_MatchesRealMxv2Screenshot()
    {
        var refPath = Path.Combine(TestPaths.RefImageDir, "ref-Default.png");
        Assert.True(File.Exists(refPath), $"参照画像がありません: {refPath}");

        var root = TestPaths.FindDevRoot();
        var doc = SkinDocument.Open(root, "Default");
        var skin = doc.Effective;
        var port = new DrawScreenPort(skin, doc.EffectiveColors, new SkinAssetSource(doc));
        port.Reload("");  // 本物は起動直後で曲名が空

        using var refBmp = new Bitmap(refPath);
        Assert.Equal(skin.screenW, refBmp.Width);
        Assert.Equal(skin.screenH, refBmp.Height);

        // 比較する矩形を集める。
        var rects = new List<Rectangle>
        {
            new(skin.bannerX, skin.bannerY, skin.bannerW, skin.bannerH),
        };
        for (int i = 0; i < 9; i++)
        {
            Assert.True(port.StatusRect(i, out int sx, out int sy, out int sw, out int sh));
            rects.Add(new Rectangle(sx, sy, sw, sh));
        }
        var kb0 = new SkinAssetSource(doc).Find(skin.kb0Bitmap);
        Assert.NotNull(kb0);
        for (int i = 0; i < 9; i++)
        {
            rects.Add(new Rectangle(skin.kbX, skin.kbY + skin.chYOffset[i] + skin.kbYOffset,
                kb0!.Width, kb0.Height));
        }

        var data = refBmp.LockBits(new Rectangle(0, 0, refBmp.Width, refBmp.Height),
            ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
        int total = 0, diff = 0;
        var worst = (x: -1, y: -1, mine: (0, 0, 0), theirs: (0, 0, 0));
        try
        {
            var row = new byte[data.Stride];
            var byRow = new Dictionary<int, byte[]>();
            for (int y = 0; y < refBmp.Height; y++)
            {
                System.Runtime.InteropServices.Marshal.Copy(
                    data.Scan0 + y * data.Stride, row, 0, data.Stride);
                byRow[y] = (byte[])row.Clone();
            }
            foreach (var r in rects)
            {
                for (int y = r.Top; y < r.Bottom && y < refBmp.Height; y++)
                {
                    var line = byRow[y];
                    for (int x = r.Left; x < r.Right && x < refBmp.Width; x++)
                    {
                        total++;
                        var mine = PixelAt(port.Screen, x, y);
                        var theirs = (line[x * 3 + 2], line[x * 3 + 1], line[x * 3]);
                        if (mine == theirs) continue;
                        diff++;
                        if (worst.x < 0) worst = (x, y, mine, theirs);
                    }
                }
            }
        }
        finally
        {
            refBmp.UnlockBits(data);
        }

        Assert.True(total > 100000, $"比較した画素が少なすぎる ({total})");
        Assert.True(diff == 0,
            $"本体と一致しない画素が {diff}/{total}。最初のずれ: ({worst.x},{worst.y}) " +
            $"移植={worst.mine} 本体={worst.theirs}\n" +
            "移植 (Model/Render/) を直すか、本体・同梱スキンを変えたのなら " +
            "refimage/capture-ref.ps1 で参照画像を撮り直すこと。");
    }
}
