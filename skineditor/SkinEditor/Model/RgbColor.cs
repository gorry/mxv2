// mxv2 本体 src/bitmap.h の Rgb と、src/colors.cpp の COLORREF 変換の移植。
// COLORREF は 0x00BBGGRR（R が下位バイト）。
namespace SkinEditor.Model;

public struct RgbColor
{
    public byte R, G, B;

    public RgbColor() { }
    public RgbColor(int r, int g, int b) { R = (byte)r; G = (byte)g; B = (byte)b; }

    public static RgbColor FromColorRef(int c) =>
        new(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff);

    public int ToColorRef() => R | (G << 8) | (B << 16);

    public System.Drawing.Color ToDrawingColor() => System.Drawing.Color.FromArgb(R, G, B);
}
