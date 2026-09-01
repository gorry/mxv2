// mxv2 本体 src/skin.h の struct Xywh の移植。
namespace SkinEditor.Model;

public struct Xywh
{
    public int X, Y, W, H;

    public Xywh() { }
    public Xywh(int x, int y, int w, int h) { X = x; Y = y; W = w; H = h; }

    public override string ToString() => $"{X},{Y},{W},{H}";
}
