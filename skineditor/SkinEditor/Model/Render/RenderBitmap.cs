// mxv2 スキンエディタ - ビットマップ（mxv2 本体 src/bitmap.h の class Bitmap の移植）。
//
// 本体は「旧 mxv の DIB のメモリレイアウトをそのまま保つ」方針で作られている。
// ここでもそれを崩さない。崩すとブリッタ (Blitter.cs) を本体からそのまま
// 持ってこられなくなる。
//   - 行は下から上へ並ぶ（ボトムアップ DIB）
//   - 行の長さは 4 バイト境界へ切り上げ
//   - 8bpp はパレット付き、24bpp は B,G,R の順
//
// C# 側の都合で変えたのは、bits を byte[] にしたことだけ。

namespace SkinEditor.Model.Render;

public sealed class RenderBitmap
{
    public int Width { get; private set; }
    public int Height { get; private set; }
    public int BitCount { get; private set; }
    public int Stride { get; private set; }
    public byte[] Bits { get; private set; } = Array.Empty<byte>();

    // 8bpp のときだけ意味を持つ。常に 256 要素。
    public RgbColor[] Palette { get; } = new RgbColor[256];

    public bool Valid => Bits.Length > 0;

    // 4 バイト境界への切り上げ。本体 bitmap.h の LineWidth()。
    public static int LineWidth(int bytes) => (bytes + 3) & -4;

    // bitCount は 8 か 24。既存の内容は破棄される。
    public bool Create(int width, int height, int bitCount)
    {
        Destroy();
        if (width <= 0 || height <= 0) return false;
        if (bitCount != 8 && bitCount != 24) return false;

        Width = width;
        Height = height;
        BitCount = bitCount;
        Stride = LineWidth(width * (bitCount / 8));
        Bits = new byte[(long)Stride * height];
        Array.Clear(Palette);
        return true;
    }

    public void Destroy()
    {
        Width = 0;
        Height = 0;
        BitCount = 0;
        Stride = 0;
        Bits = Array.Empty<byte>();
    }

    public void SetPalette(int index, int r, int g, int b)
    {
        if (index < 0 || index > 255) return;
        Palette[index] = new RgbColor(r, g, b);
    }

    // ボトムアップなので、論理的な y 行目の先頭はここ。
    public int RowFromTop(int y) => (Height - 1 - y) * Stride;

    public RenderBitmap Clone()
    {
        var c = new RenderBitmap();
        c.Create(Width, Height, BitCount);
        Array.Copy(Bits, c.Bits, Bits.Length);
        Array.Copy(Palette, c.Palette, Palette.Length);
        return c;
    }
}
