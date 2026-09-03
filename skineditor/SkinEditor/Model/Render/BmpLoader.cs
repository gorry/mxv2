// mxv2 スキンエディタ - 素材の読み込み。
//
// 本体は src/bmpfile.cpp が自前で BMP を解いて 8bpp / 24bpp の Bitmap にする。
// こちらは System.Drawing に読ませてから RenderBitmap へ移す。**8bpp は
// パレットとインデックスをそのまま持ってくる**のが肝で、ここを 32bpp へ
// 変換してしまうと、パレット番号で発色する素材（鍵盤・操作ボタンの LED・
// レベルメータ）が本体と同じに描けない。
//
// bmpfile.cpp の 1/4/16bpp 展開は移植していない。System.Drawing が読めた
// 形式は 8bpp 以外まとめて 24bpp (B,G,R) へ落とす。同梱スキンの素材は
// 11 枚とも 8bpp インデックスカラーで、パレットに意味のある役割
// (BitmapRoleInfo.IsPaletteDependent) は 8bpp しか受け付けない作りなので、
// 実際に効いてくるのは背景くらい。

using System.Drawing;
using System.Drawing.Imaging;

namespace SkinEditor.Model.Render;

public static class BmpLoader
{
    public static RenderBitmap? Load(string path)
    {
        try
        {
            using var src = new Bitmap(path);
            return FromDrawingBitmap(src);
        }
        catch
        {
            // 壊れた／未対応形式のファイルはプレビューに出さない（本体は
            // 起動を止めるが、編集中は一時的に欠けうるので続行する）。
            return null;
        }
    }

    public static RenderBitmap? FromDrawingBitmap(Bitmap src)
    {
        return src.PixelFormat == PixelFormat.Format8bppIndexed ? FromIndexed8(src) : FromAnyTo24(src);
    }

    private static RenderBitmap? FromIndexed8(Bitmap src)
    {
        var dst = new RenderBitmap();
        if (!dst.Create(src.Width, src.Height, 8)) return null;

        var entries = src.Palette.Entries;
        for (int i = 0; i < 256; i++)
        {
            var c = i < entries.Length ? entries[i] : Color.Black;
            dst.SetPalette(i, c.R, c.G, c.B);
        }

        var data = src.LockBits(new Rectangle(0, 0, src.Width, src.Height),
            ImageLockMode.ReadOnly, PixelFormat.Format8bppIndexed);
        try
        {
            var row = new byte[data.Stride];
            for (int y = 0; y < src.Height; y++)
            {
                // LockBits は上から下、RenderBitmap は下から上。
                System.Runtime.InteropServices.Marshal.Copy(
                    data.Scan0 + y * data.Stride, row, 0, data.Stride);
                Array.Copy(row, 0, dst.Bits, dst.RowFromTop(y), src.Width);
            }
        }
        finally
        {
            src.UnlockBits(data);
        }
        return dst;
    }

    private static RenderBitmap? FromAnyTo24(Bitmap src)
    {
        var dst = new RenderBitmap();
        if (!dst.Create(src.Width, src.Height, 24)) return null;

        var data = src.LockBits(new Rectangle(0, 0, src.Width, src.Height),
            ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
        try
        {
            var row = new byte[data.Stride];
            for (int y = 0; y < src.Height; y++)
            {
                System.Runtime.InteropServices.Marshal.Copy(
                    data.Scan0 + y * data.Stride, row, 0, data.Stride);
                // Format24bppRgb は B,G,R の順で RenderBitmap と同じ並び。
                Array.Copy(row, 0, dst.Bits, dst.RowFromTop(y), src.Width * 3);
            }
        }
        finally
        {
            src.UnlockBits(data);
        }
        return dst;
    }

    // 24bpp の RenderBitmap を画面表示用の 32bpp へ移す（本体 DrawScreen::BlitTo 相当）。
    public static Bitmap ToDrawingBitmap(RenderBitmap src)
    {
        var dst = new Bitmap(src.Width, src.Height, PixelFormat.Format32bppRgb);
        var data = dst.LockBits(new Rectangle(0, 0, src.Width, src.Height),
            ImageLockMode.WriteOnly, PixelFormat.Format32bppRgb);
        try
        {
            var row = new byte[data.Stride];
            for (int y = 0; y < src.Height; y++)
            {
                int o = src.RowFromTop(y);
                if (src.BitCount == 24)
                {
                    for (int x = 0; x < src.Width; x++)
                    {
                        row[x * 4] = src.Bits[o + x * 3];
                        row[x * 4 + 1] = src.Bits[o + x * 3 + 1];
                        row[x * 4 + 2] = src.Bits[o + x * 3 + 2];
                        row[x * 4 + 3] = 255;
                    }
                }
                else
                {
                    for (int x = 0; x < src.Width; x++)
                    {
                        var c = src.Palette[src.Bits[o + x]];
                        row[x * 4] = c.B;
                        row[x * 4 + 1] = c.G;
                        row[x * 4 + 2] = c.R;
                        row[x * 4 + 3] = 255;
                    }
                }
                System.Runtime.InteropServices.Marshal.Copy(
                    row, 0, data.Scan0 + y * data.Stride, data.Stride);
            }
        }
        finally
        {
            dst.UnlockBits(data);
        }
        return dst;
    }
}
