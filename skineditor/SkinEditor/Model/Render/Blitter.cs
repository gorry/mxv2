// mxv2 スキンエディタ - ブリッタ（mxv2 本体 src/bitmap.cpp の移植）。
//
// 合成テーブルの中身・クリップの手順・画素の並びは本体と同じにしてある。
// ここがずれると、配色の Bright（＝背景に対するゲイン）の出方が本体と
// 変わってしまい、プレビューの意味が無くなる。

namespace SkinEditor.Model.Render;

// アルファ指定の特殊値。本体 bitmap.h の BlendMode と同じ値。
public static class Blend
{
    public const int Sub = 101;
    public const int Add = 102;
    public const int Mul = 103;
}

public static class Blitter
{
    // Alpha[i*511 + j] (i=0..100, j=-255..255): j*i/100
    // 添字が負になるので、本体と同じく 256 ずらした位置を原点にする。
    private static readonly byte[] AlphaStorage = new byte[101 * 511 + 512];
    // AlphaMul[i*256 + j] (i=0..255, j=0..255)
    private static readonly byte[] AlphaMul = new byte[256 * 256];
    // Clip[i] (i=-256..511): 0..255 に飽和。こちらも 256 ずらす。
    private static readonly byte[] ClipStorage = new byte[256 * 3];

    private const int AlphaBias = 256;
    private const int ClipBias = 256;

    static Blitter()
    {
        for (int i = 0; i <= 100; i++)
        {
            for (int j = -255; j <= 255; j++)
                AlphaStorage[AlphaBias + i * 511 + j] = (byte)(j * i / 100);
        }
        for (int i = 0; i <= 100; i++)
        {
            for (int j = 0; j < 256; j++) AlphaMul[i * 256 + j] = (byte)(i * j / 100);
        }
        // 本体は i を 0..200 しか埋めていなかった原典を 255 まで延長してある。
        for (int i = 101; i < 256; i++)
        {
            for (int j = 0; j < 256; j++)
                AlphaMul[i * 256 + j] = (byte)ClampByte(j + (255 - j) * (i - 100) / 100);
        }
        for (int i = -256; i < 256 * 2; i++) ClipStorage[ClipBias + i] = (byte)ClampByte(i);
    }

    private static int ClampByte(int v) => v < 0 ? 0 : v > 255 ? 255 : v;

    // 1 チャンネル分の合成。mode は 0..100 か Blend.*。
    private static byte BlendChannel(int mode, int d, int s)
    {
        if (mode == 100) return (byte)s;
        if (mode < 100) return (byte)(d - AlphaStorage[AlphaBias + mode * 511 + (d - s)]);
        if (mode == Blend.Sub) return ClipStorage[ClipBias + d - s];
        if (mode == Blend.Add) return ClipStorage[ClipBias + d + s];
        return AlphaMul[s * 256 + d];
    }

    // コピー系の座標クリップ。本体 ClipCopy と同じ手順。
    private struct ClipRect
    {
        public int XDst, YDst, XSrc, YSrc, Width, Height;
        public bool Empty => Width <= 0 || Height <= 0;
    }

    private static bool ClipCopy(RenderBitmap dst, int xdst, int ydst, int width, int height,
        RenderBitmap src, int xsrc, int ysrc, out ClipRect outRect)
    {
        if (xdst < 0) { width += xdst; xsrc -= xdst; xdst = 0; }
        if (ydst < 0) { height += ydst; ysrc -= ydst; ydst = 0; }
        if (xsrc < 0) { width += xsrc; xdst -= xsrc; xsrc = 0; }
        if (ysrc < 0) { height += ysrc; ydst -= ysrc; ysrc = 0; }
        if (width > dst.Width - xdst) width = dst.Width - xdst;
        if (width > src.Width - xsrc) width = src.Width - xsrc;
        if (height > dst.Height - ydst) height = dst.Height - ydst;
        if (height > src.Height - ysrc) height = src.Height - ysrc;

        outRect = new ClipRect
        {
            XDst = xdst, YDst = ydst, XSrc = xsrc, YSrc = ysrc, Width = width, Height = height,
        };
        return !outRect.Empty;
    }

    // ---- 矩形塗り潰し ----------------------------------------------------
    public static void Fill(RenderBitmap? dst, int xdst, int ydst, int width, int height,
        int r, int g, int b, int alpha)
    {
        if (dst == null || !dst.Valid) return;

        if (xdst < 0) { width += xdst; xdst = 0; }
        if (ydst < 0) { height += ydst; ydst = 0; }
        if (width > dst.Width - xdst) width = dst.Width - xdst;
        if (height > dst.Height - ydst) height = dst.Height - ydst;
        if (width <= 0 || height <= 0) return;

        var bits = dst.Bits;
        if (dst.BitCount == 8)
        {
            // 本体と同じく 8bpp はパレット番号 r をそのまま敷く（合成しない）。
            for (int y = 0; y < height; y++)
            {
                int o = dst.RowFromTop(ydst + y) + xdst;
                for (int x = 0; x < width; x++) bits[o + x] = (byte)r;
            }
            return;
        }

        if (alpha == 100)
        {
            for (int y = 0; y < height; y++)
            {
                int o = dst.RowFromTop(ydst + y) + xdst * 3;
                for (int x = 0; x < width; x++)
                {
                    bits[o++] = (byte)b;
                    bits[o++] = (byte)g;
                    bits[o++] = (byte)r;
                }
            }
            return;
        }

        for (int y = 0; y < height; y++)
        {
            int o = dst.RowFromTop(ydst + y) + xdst * 3;
            for (int x = 0; x < width; x++)
            {
                bits[o] = BlendChannel(alpha, bits[o], b);
                bits[o + 1] = BlendChannel(alpha, bits[o + 1], g);
                bits[o + 2] = BlendChannel(alpha, bits[o + 2], r);
                o += 3;
            }
        }
    }

    // 乗算で塗ったうえで、その結果を alpha (0..100) で元の画素と混ぜる。
    // r,g,b は色ではなく背景に対するゲイン（100 が素通し）。
    public static void FillMul(RenderBitmap? dst, int xdst, int ydst, int width, int height,
        int r, int g, int b, int alpha)
    {
        if (dst == null || !dst.Valid) return;
        if (alpha <= 0) return;
        if (alpha >= 100)
        {
            Fill(dst, xdst, ydst, width, height, r, g, b, Blend.Mul);
            return;
        }

        if (xdst < 0) { width += xdst; xdst = 0; }
        if (ydst < 0) { height += ydst; ydst = 0; }
        if (width > dst.Width - xdst) width = dst.Width - xdst;
        if (height > dst.Height - ydst) height = dst.Height - ydst;
        if (width <= 0 || height <= 0) return;

        // 8bpp はパレット番号を敷くだけで合成できないので、混ぜようがない。
        if (dst.BitCount == 8) return;

        var bits = dst.Bits;
        for (int y = 0; y < height; y++)
        {
            int o = dst.RowFromTop(ydst + y) + xdst * 3;
            for (int x = 0; x < width; x++)
            {
                bits[o] = BlendChannel(alpha, bits[o], BlendChannel(Blend.Mul, bits[o], b));
                bits[o + 1] = BlendChannel(alpha, bits[o + 1], BlendChannel(Blend.Mul, bits[o + 1], g));
                bits[o + 2] = BlendChannel(alpha, bits[o + 2], BlendChannel(Blend.Mul, bits[o + 2], r));
                o += 3;
            }
        }
    }

    // ---- カバレッジ合成（アンチエイリアス文字） ---------------------------
    public static void BlendMask(RenderBitmap? dst, int xdst, int ydst, int width, int height,
        RenderBitmap? mask, int xsrc, int ysrc, RgbColor color, int bright)
    {
        if (dst == null || mask == null || !dst.Valid || !mask.Valid) return;
        if (dst.BitCount != 24 || mask.BitCount != 8) return;
        if (!ClipCopy(dst, xdst, ydst, width, height, mask, xsrc, ysrc, out var c)) return;

        if (bright < 0) bright = 0;
        if (bright > 100) bright = 100;

        var q = dst.Bits;
        var p = mask.Bits;
        for (int y = 0; y < c.Height; y++)
        {
            int oq = dst.RowFromTop(c.YDst + y) + c.XDst * 3;
            int op = mask.RowFromTop(c.YSrc + y) + c.XSrc;
            for (int x = 0; x < c.Width; x++)
            {
                int a = p[op++] * bright / 100;
                if (a > 0)
                {
                    if (a >= 255)
                    {
                        q[oq] = color.B;
                        q[oq + 1] = color.G;
                        q[oq + 2] = color.R;
                    }
                    else
                    {
                        q[oq] = (byte)(q[oq] + (color.B - q[oq]) * a / 255);
                        q[oq + 1] = (byte)(q[oq + 1] + (color.G - q[oq + 1]) * a / 255);
                        q[oq + 2] = (byte)(q[oq + 2] + (color.R - q[oq + 2]) * a / 255);
                    }
                }
                oq += 3;
            }
        }
    }

    // ---- 矩形コピー ------------------------------------------------------
    public static void Copy(RenderBitmap? dst, int xdst, int ydst, int width, int height,
        RenderBitmap? src, int xsrc, int ysrc, int alpha)
    {
        if (dst == null || src == null || !dst.Valid || !src.Valid) return;
        if (!ClipCopy(dst, xdst, ydst, width, height, src, xsrc, ysrc, out var c)) return;

        var q = dst.Bits;
        var p = src.Bits;

        // 8bpp 同士はパレット番号をそのまま転送する（本体と同じ）。
        if (dst.BitCount == 8)
        {
            if (src.BitCount != 8) return;
            for (int y = 0; y < c.Height; y++)
            {
                Array.Copy(p, src.RowFromTop(c.YSrc + y) + c.XSrc,
                    q, dst.RowFromTop(c.YDst + y) + c.XDst, c.Width);
            }
            return;
        }

        if (src.BitCount == 24)
        {
            if (alpha == 100)
            {
                for (int y = 0; y < c.Height; y++)
                {
                    Array.Copy(p, src.RowFromTop(c.YSrc + y) + c.XSrc * 3,
                        q, dst.RowFromTop(c.YDst + y) + c.XDst * 3, c.Width * 3);
                }
                return;
            }
            for (int y = 0; y < c.Height; y++)
            {
                int oq = dst.RowFromTop(c.YDst + y) + c.XDst * 3;
                int op = src.RowFromTop(c.YSrc + y) + c.XSrc * 3;
                for (int x = 0; x < c.Width; x++)
                {
                    q[oq] = BlendChannel(alpha, q[oq], p[op]);
                    q[oq + 1] = BlendChannel(alpha, q[oq + 1], p[op + 1]);
                    q[oq + 2] = BlendChannel(alpha, q[oq + 2], p[op + 2]);
                    oq += 3;
                    op += 3;
                }
            }
            return;
        }

        // 8bpp -> 24bpp
        var pal = src.Palette;
        for (int y = 0; y < c.Height; y++)
        {
            int oq = dst.RowFromTop(c.YDst + y) + c.XDst * 3;
            int op = src.RowFromTop(c.YSrc + y) + c.XSrc;
            if (alpha == 100)
            {
                for (int x = 0; x < c.Width; x++)
                {
                    var col = pal[p[op++]];
                    q[oq++] = col.B;
                    q[oq++] = col.G;
                    q[oq++] = col.R;
                }
            }
            else
            {
                for (int x = 0; x < c.Width; x++)
                {
                    var col = pal[p[op++]];
                    q[oq] = BlendChannel(alpha, q[oq], col.B);
                    q[oq + 1] = BlendChannel(alpha, q[oq + 1], col.G);
                    q[oq + 2] = BlendChannel(alpha, q[oq + 2], col.R);
                    oq += 3;
                }
            }
        }
    }

    // ---- 透過矩形コピー (パレット 0 が透明) --------------------------------
    public static void CopyTransparent(RenderBitmap? dst, int xdst, int ydst, int width, int height,
        RenderBitmap? src, int xsrc, int ysrc, int alpha)
    {
        if (dst == null || src == null || !dst.Valid || !src.Valid) return;
        if (!ClipCopy(dst, xdst, ydst, width, height, src, xsrc, ysrc, out var c)) return;

        var q = dst.Bits;
        var p = src.Bits;

        if (dst.BitCount == 8)
        {
            if (src.BitCount != 8) return;
            for (int y = 0; y < c.Height; y++)
            {
                int oq = dst.RowFromTop(c.YDst + y) + c.XDst;
                int op = src.RowFromTop(c.YSrc + y) + c.XSrc;
                for (int x = 0; x < c.Width; x++)
                {
                    byte v = p[op++];
                    if (v != 0) q[oq] = v;
                    oq++;
                }
            }
            return;
        }
        if (src.BitCount != 8) return;

        var pal = src.Palette;
        for (int y = 0; y < c.Height; y++)
        {
            int oq = dst.RowFromTop(c.YDst + y) + c.XDst * 3;
            int op = src.RowFromTop(c.YSrc + y) + c.XSrc;
            for (int x = 0; x < c.Width; x++)
            {
                byte v = p[op++];
                if (v != 0)
                {
                    var col = pal[v];
                    if (alpha == 100)
                    {
                        q[oq] = col.B;
                        q[oq + 1] = col.G;
                        q[oq + 2] = col.R;
                    }
                    else
                    {
                        q[oq] = BlendChannel(alpha, q[oq], col.B);
                        q[oq + 1] = BlendChannel(alpha, q[oq + 1], col.G);
                        q[oq + 2] = BlendChannel(alpha, q[oq + 2], col.R);
                    }
                }
                oq += 3;
            }
        }
    }

    // ---- 合成矩形コピー ---------------------------------------------------
    // src1 (8bpp) のパレット 0 の画素は src2 (24bpp) をそのまま通す。
    // それ以外は src2 を下地として src1 を alpha 合成する。
    public static void CopyComposite(RenderBitmap? dst, int xdst, int ydst, int width, int height,
        RenderBitmap? src1, int xsrc1, int ysrc1,
        RenderBitmap? src2, int xsrc2, int ysrc2, int alpha)
    {
        if (dst == null || src1 == null || src2 == null) return;
        if (!dst.Valid || !src1.Valid || !src2.Valid) return;
        if (dst.BitCount != 24 || src1.BitCount != 8 || src2.BitCount != 24) return;

        // 3 枚あるので本体と同じ手順で順番にクリップする。
        if (xdst < 0) { width += xdst; xsrc1 -= xdst; xsrc2 -= xdst; xdst = 0; }
        if (ydst < 0) { height += ydst; ysrc1 -= ydst; ysrc2 -= ydst; ydst = 0; }
        if (xsrc1 < 0) { width += xsrc1; xdst -= xsrc1; xsrc2 -= xsrc1; xsrc1 = 0; }
        if (ysrc1 < 0) { height += ysrc1; ydst -= ysrc1; ysrc2 -= ysrc1; ysrc1 = 0; }
        if (xsrc2 < 0) { width += xsrc2; xdst -= xsrc2; xsrc1 -= xsrc2; xsrc2 = 0; }
        if (ysrc2 < 0) { height += ysrc2; ydst -= ysrc2; ysrc1 -= ysrc2; ysrc2 = 0; }
        if (width > dst.Width - xdst) width = dst.Width - xdst;
        if (width > src1.Width - xsrc1) width = src1.Width - xsrc1;
        if (width > src2.Width - xsrc2) width = src2.Width - xsrc2;
        if (height > dst.Height - ydst) height = dst.Height - ydst;
        if (height > src1.Height - ysrc1) height = src1.Height - ysrc1;
        if (height > src2.Height - ysrc2) height = src2.Height - ysrc2;
        if (width <= 0 || height <= 0) return;

        var pal = src1.Palette;
        var q = dst.Bits;
        var p = src1.Bits;
        var r2 = src2.Bits;
        for (int y = 0; y < height; y++)
        {
            int oq = dst.RowFromTop(ydst + y) + xdst * 3;
            int op = src1.RowFromTop(ysrc1 + y) + xsrc1;
            int or2 = src2.RowFromTop(ysrc2 + y) + xsrc2 * 3;
            for (int x = 0; x < width; x++)
            {
                byte v = p[op++];
                if (v != 0)
                {
                    var col = pal[v];
                    if (alpha == 100)
                    {
                        q[oq] = col.B;
                        q[oq + 1] = col.G;
                        q[oq + 2] = col.R;
                    }
                    else
                    {
                        q[oq] = BlendChannel(alpha, r2[or2], col.B);
                        q[oq + 1] = BlendChannel(alpha, r2[or2 + 1], col.G);
                        q[oq + 2] = BlendChannel(alpha, r2[or2 + 2], col.R);
                    }
                }
                else
                {
                    q[oq] = r2[or2];
                    q[oq + 1] = r2[or2 + 1];
                    q[oq + 2] = r2[or2 + 2];
                }
                oq += 3;
                or2 += 3;
            }
        }
    }
}
