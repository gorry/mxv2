// 画面の 2 分割とキャンバスの伸縮（fullscreen.md）。本体 src/skin.cpp の
// Skin::PlacedFor / Skin::CanvasSizeFor と 1:1 で移植した
// SkinLayout.PlacedFor が、同梱スキンの旧レイアウトと同じ値を出すことと、
// キャンバスを伸ばしたときにファイラー側だけが伸びることを見る。
//
// **旧レイアウトの値**（2026-09-09 にキーを変える前の layout.ini）:
//   Default [FileList] Rect=4,366,620,110  Rows=11,8  [ScrollBar] Rect=624,366,12,110
//   Phone   [FileList] Rect=4,462,460,250  Rows=15,10 [ScrollBar] Rect=452,462,24,250
// この数字と一致していれば、キーを入れ替えても見た目が変わっていない。

using SkinEditor.Model;

namespace SkinEditor.Tests;

public class SkinPlacementTests
{
    [Fact]
    public void Default_PlacementMatchesOldAbsoluteRects()
    {
        var e = SkinDocument.Open(TestPaths.FindDevRoot(), "Default").Placed;

        Assert.Equal((4, 366, 620, 110), (e.fileListX, e.fileListY, e.fileListW, e.fileListH));
        Assert.Equal((624, 366, 12, 110), (e.scrollX, e.scrollY, e.scrollW, e.scrollH));
        // Default は当たり判定を広げていないので、描画と同じ。
        Assert.Equal((624, 12), (e.scrollHitX, e.scrollHitW));
        Assert.Equal(new[] { 11, 8 }, e.fileListRows);
        // 矢印は上端・下端に貼り付き、溝は残り（旧 PosBar=0,12 / PosDownArrow=0,98）。
        Assert.Equal(new[] { 0, 12 }, e.scrollPosBar);
        Assert.Equal(new[] { 0, 98 }, e.scrollPosDownArrow);
        // 溝の高さは旧 SrcBar の高さ 86 と一致する。
        Assert.Equal(86, e.scrollGrooveH);
        Assert.Equal(86 - 12, e.ScrollBarMovement());
    }

    [Fact]
    public void Phone_PlacementMatchesOldAbsoluteRects()
    {
        var e = SkinDocument.Open(TestPaths.FindDevRoot(), "Phone").Placed;

        Assert.Equal((4, 462, 460, 250), (e.fileListX, e.fileListY, e.fileListW, e.fileListH));
        // 描くのは絵のある右 12px だけ。旧レイアウトの Rect=452,...,24 のうち、
        // 実際に絵が出ていたのは x 464..476 だった。
        Assert.Equal((464, 462, 12, 250), (e.scrollX, e.scrollY, e.scrollW, e.scrollH));
        // 当たり判定は旧 Rect と同じ 452..476（指で掴みやすいように広げてある）。
        Assert.Equal((452, 24), (e.scrollHitX, e.scrollHitW));
        Assert.Equal(new[] { 15, 10 }, e.fileListRows);
        // 曲名の幅は旧 TitleWidth=340,304 と一致する（一覧の幅 - TitleX）。
        Assert.Equal(new[] { 340, 304 }, e.fileListTitleW);
        Assert.Equal(226, e.scrollGrooveH);
    }

    // キャンバスを縦に伸ばすと、伸びるのはファイラー側だけ。
    // ファイラー以外側（バナー・曲名・操作ボタン）は動かない。
    [Fact]
    public void StretchingCanvas_GrowsOnlyTheFilerSide()
    {
        var raw = SkinDocument.Open(TestPaths.FindDevRoot(), "Phone").Effective;
        var a = raw.PlacedFor(480, 720);
        var b = raw.PlacedFor(480, 853);

        Assert.Equal(a.titleY, b.titleY);
        Assert.Equal(a.playKeyY, b.playKeyY);
        Assert.Equal(a.fileListY, b.fileListY);

        // 増えた 133px はそのままファイラーの高さになる。
        Assert.Equal(a.fileListH + 133, b.fileListH);
        Assert.Equal(a.scrollH + 133, b.scrollH);
        Assert.Equal(a.scrollGrooveH + 133, b.scrollGrooveH);
        // 行数も増える（383 / 16 = 23、383 / 24 = 15）。
        Assert.Equal(new[] { 23, 15 }, b.fileListRows);
        // 下矢印は下端に貼り付いたまま。
        Assert.Equal(b.scrollH - raw.scrollSrcDownArrow.H, b.scrollPosDownArrow[1]);
    }

    // ファイラーを上に置くと、ファイラー以外側は下へ寄る。伸ばしたぶんは
    // 上（ファイラー側）が受け取り、下の部品は画面の下端との距離を保つ。
    [Fact]
    public void FilerOnTop_MovesTheOtherSideToTheBottom()
    {
        var raw = SkinDocument.Open(TestPaths.FindDevRoot(), "Phone").Effective;
        raw.filerSide = (int)FilerSide.Top;
        // 上に置くので、境界からの厚み 258 はそのまま「上 258px がファイラー」。
        var a = raw.PlacedFor(480, 720);
        var b = raw.PlacedFor(480, 853);

        Assert.Equal(0, a.fileListY - a.fileListMargin[1]);
        // ファイラー以外側の原点は「キャンバスの高さ - 固定側の厚み」。
        Assert.Equal(720 - (720 - 258), a.placedOtherY);
        Assert.Equal(853 - (720 - 258), b.placedOtherY);
        // 部品はその原点ぶんずれる（下端からの距離が変わらない）。
        Assert.Equal(b.placedOtherY - a.placedOtherY, b.titleY - a.titleY);
    }

    // 出力の縦横比からキャンバスの大きさを決める（fullscreen.md の算出式）。
    [Fact]
    public void CanvasSizeFor_StretchesToOutputAspectWithinLimits()
    {
        var raw = SkinDocument.Open(TestPaths.FindDevRoot(), "Phone").Effective;

        // 480x720 のスキンを 1080x1920 へ。853 まで伸ばせる背景があるとき。
        Assert.Equal(853, CanvasH(raw, 1080, 1920, 853));
        // 背景が宣言サイズと同じなら伸びない（＝上下がレターボックス）。
        Assert.Equal(720, CanvasH(raw, 1080, 1920, 720));
        // 横長の画面では縮む方向になるが、宣言サイズで止まる（左右が余白）。
        Assert.Equal(720, CanvasH(raw, 1920, 1080, 853));
    }

    // 本体 Skin::CanvasSizeFor と同じ式（C# 側には無いので、ここで書いて
    // 期待値を突き合わせる。式を変えたら本体と一緒にここも直すこと）。
    private static int CanvasH(SkinLayout e, int outW, int outH, int limit)
    {
        int h = (int)(((long)outH * e.screenW + outW / 2) / outW);
        if (h < e.screenH) h = e.screenH;
        if (h > limit) h = limit;
        return h;
    }

    // [Screen] FilerSide は名前で書く。読み書きで往復すること。
    [Fact]
    public void WriteAll_ThenApplyLayout_RoundTripsFilerSideAndMargins()
    {
        var src = new SkinLayout
        {
            filerSide = (int)FilerSide.Right,
            filerExtent = 200,
            fileListMargin = new[] { 1, 2, 3, 4 },
            scrollWidth = 9,
            scrollHitWidth = 33,
        };
        var ini = new IniDocument();
        SkinLayoutIo.WriteAll(src, ini);
        Assert.Equal("Right", ini.GetString("Screen", "FilerSide", ""));

        var dst = new SkinLayout();
        SkinLayoutIo.ApplyLayout(ini, dst);

        Assert.Equal((int)FilerSide.Right, dst.filerSide);
        Assert.Equal(200, dst.filerExtent);
        Assert.Equal(new[] { 1, 2, 3, 4 }, dst.fileListMargin);
        Assert.Equal((9, 33), (dst.scrollWidth, dst.scrollHitWidth));
    }
}
