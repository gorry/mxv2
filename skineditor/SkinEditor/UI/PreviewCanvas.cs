// mxv2 スキンエディタ - 本体の画面を模した簡易プレビュー。
//
// 位置・大きさは layout.ini の実効値どおりに描くが、パレット番号での
// 発色（鍵盤の押下・操作ボタンの LED・レベルメータ）はここでは再現しない
// （近似表示。位置とサイズの確認が主目的で、C++ 側の合成式は完全移植しない）。
// クリックで対応するセクションを選ばせ、主要な矩形はドラッグで移動できる。

using System.Drawing;
using System.Drawing.Drawing2D;
using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class PreviewCanvas : Panel
{
    private readonly SkinDocument _doc;
    private readonly BitmapCache _bitmaps = new();
    private float _scale = 1f;
    private PointF _origin;

    public event Action<string>? PartActivated;

    private sealed class DragRegion
    {
        public RectangleF ScreenRect;
        public string SectionTitle = "";
        public Action<int, int>? Move;  // 絶対座標 (skin px) を渡す
    }

    private readonly List<DragRegion> _regions = new();
    private DragRegion? _dragging;
    private PointF _dragStartSkin;
    private PointF _dragOriginSkin;

    // ダミー再生状態（PreviewStateBar から書き換えられる）
    public bool StatePlay = true, StateCont = true, StatePause, StateRepeat;
    public int StateVolume;         // -100..100
    public double StateProgress = 0.4;  // 0..1

    private static readonly string[] DummyFiles =
    {
        "SAMPLE01.MDX  Sample Song A",
        "SAMPLE02.MDX  Sample Song B",
        "FOLDER1",
        "SAMPLE03.MDX  Sample Song C",
    };

    public PreviewCanvas(SkinDocument doc)
    {
        _doc = doc;
        DoubleBuffered = true;
        BackColor = Color.FromArgb(64, 64, 64);
        doc.Changed += () => Invalidate();
        MouseDown += OnMouseDown;
        MouseMove += OnMouseMove;
        MouseUp += (_, _) => _dragging = null;
        Resize += (_, _) => Invalidate();
    }

    public void InvalidateBitmaps()
    {
        _bitmaps.Invalidate();
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.None;
        g.InterpolationMode = InterpolationMode.NearestNeighbor;
        g.PixelOffsetMode = PixelOffsetMode.Half;
        _regions.Clear();

        var eff = _doc.Effective;
        int sw = Math.Max(1, eff.screenW), sh = Math.Max(1, eff.screenH);
        _scale = Math.Max(0.05f, Math.Min((Width - 16f) / sw, (Height - 16f) / sh));
        _origin = new PointF((Width - sw * _scale) / 2f, (Height - sh * _scale) / 2f);

        var save = g.Save();
        g.TranslateTransform(_origin.X, _origin.Y);
        g.ScaleTransform(_scale, _scale);

        g.FillRectangle(Brushes.Black, 0, 0, sw, sh);
        DrawBack(g, eff);
        DrawKeyboard(g, eff);
        DrawBanner(g, eff);
        DrawStatus(g, eff);
        DrawTitle(g, eff);
        DrawFileList(g, eff);
        DrawScrollBar(g, eff);
        DrawProgressBar(g, eff);
        DrawVolumeBar(g, eff);
        DrawPlayKey(g, eff);
        DrawOverflowWarnings(g, eff, sw, sh);

        g.Restore(save);
    }

    // ---- 各部品の描画 ----------------------------------------------------
    private void DrawBack(Graphics g, SkinLayout eff)
    {
        var bmp = _bitmaps.Get(_doc, BitmapRole.Back);
        if (bmp != null) g.DrawImage(bmp, new Rectangle(0, 0, eff.screenW, eff.screenH));
    }

    private void DrawKeyboard(Graphics g, SkinLayout eff)
    {
        var bmp = _bitmaps.Get(_doc, BitmapRole.Kb0);
        var rect = new Rectangle(eff.kbX, eff.kbY, bmp?.Width ?? 400, bmp?.Height ?? 36);
        for (int i = 0; i < 9; i++)
        {
            int y = eff.kbY + eff.chYOffset[i] + eff.kbYOffset;
            if (bmp != null) g.DrawImage(bmp, new Rectangle(eff.kbX, y, bmp.Width, bmp.Height));
        }
        Reg(new Rectangle(eff.kbX, eff.kbY, rect.Width, rect.Height), "鍵盤",
            (nx, ny) => _doc.SetLayoutRaw("Keyboard", "Pos", $"{nx},{ny}"));
    }

    private void DrawBanner(Graphics g, SkinLayout eff)
    {
        var bmp = _bitmaps.Get(_doc, BitmapRole.Banner);
        var dest = new Rectangle(eff.bannerX, eff.bannerY, eff.bannerW, eff.bannerH);
        if (bmp != null)
        {
            var src = new Rectangle(0, 0, Math.Min(bmp.Width, eff.bannerW), Math.Min(bmp.Height, eff.bannerH));
            g.DrawImage(bmp, dest, src, GraphicsUnit.Pixel);
        }
        else DrawPlaceholder(g, dest, "Banner");
        Reg(dest, "バナー", (nx, ny) => _doc.SetLayoutRaw("Banner", "Rect", $"{nx},{ny},{eff.bannerW},{eff.bannerH}"));
    }

    private void DrawStatus(Graphics g, SkinLayout eff)
    {
        using var font = new Font("Consolas", 7f);
        using var brush = new SolidBrush(Color.LightGreen);
        g.FillRectangle(new SolidBrush(Color.FromArgb(160, Color.Black)), eff.statusX, eff.statusY, eff.statusBackW, eff.statusBackH * 9 / 4);
        for (int i = 0; i < 9; i++)
        {
            string label = i < 8 ? $"CH{i + 1} V:99 P:0 D:0" : "PCM V:99 P:0";
            g.DrawString(label, font, brush, eff.statusX, eff.statusY + eff.chYOffset[i] * 0.3f);
        }
        Reg(new Rectangle(eff.statusX, eff.statusY, eff.statusBackW, eff.statusBackH), "ステータス",
            (nx, ny) => _doc.SetLayoutRaw("Status", "Pos", $"{nx},{ny}"));
    }

    private void DrawTitle(Graphics g, SkinLayout eff)
    {
        var rect = new Rectangle(eff.titleX, eff.titleY, eff.titleW, eff.titleH);
        using var font = new Font("Meiryo UI", Math.Max(6, eff.titleH - 4));
        using var bg = new SolidBrush(Color.Black);
        using var fg = new SolidBrush(Color.White);
        g.FillRectangle(bg, rect);
        var clip = g.Save();
        g.SetClip(rect);
        g.DrawString("プレビュー曲名 - スキンエディタ", font, fg, rect.X + 2, rect.Y);
        g.Restore(clip);
        Reg(rect, "曲名", (nx, ny) => _doc.SetLayoutRaw("Title", "Rect", $"{nx},{ny},{eff.titleW},{eff.titleH}"));
    }

    private void DrawFileList(Graphics g, SkinLayout eff)
    {
        int size = 0;
        var rect = new Rectangle(eff.fileListX, eff.fileListY, eff.fileListW, eff.fileListH);
        using var bg = new SolidBrush(Color.FromArgb(160, Color.Black));
        g.FillRectangle(bg, rect);
        using var font = new Font("Meiryo UI", Math.Max(6, eff.fileListItemH[size] - 3));
        using var fg = new SolidBrush(Color.White);
        int itemH = Math.Max(1, eff.fileListItemH[size]);
        int rows = Math.Max(1, eff.fileListRows[size]);
        var clip = g.Save();
        g.SetClip(rect);
        for (int i = 0; i < rows && i < DummyFiles.Length + 3; i++)
        {
            int y = rect.Y + i * itemH;
            string sample = DummyFiles[i % DummyFiles.Length];
            var parts = sample.Split("  ", 2);
            g.DrawString(parts[0], font, fg, rect.X + eff.fileListBaseNameX[size], y);
            if (parts.Length > 1) g.DrawString(parts[1], font, fg, rect.X + eff.fileListTitleX[size], y);
        }
        g.Restore(clip);
        Reg(rect, "ファイラー",
            (nx, ny) => _doc.SetLayoutRaw("FileList", "Rect", $"{nx},{ny},{eff.fileListW},{eff.fileListH}"));
    }

    private void DrawScrollBar(Graphics g, SkinLayout eff)
    {
        var bmp = _bitmaps.Get(_doc, BitmapRole.ScrollBar);
        var outer = new Rectangle(eff.scrollX, eff.scrollY, eff.scrollW, eff.scrollH);
        if (bmp != null)
        {
            DrawSrc(g, bmp, eff.scrollSrcUpArrow, eff.scrollX + eff.scrollPosUpArrow[0], eff.scrollY + eff.scrollPosUpArrow[1]);
            DrawSrc(g, bmp, eff.scrollSrcBar, eff.scrollX + eff.scrollPosBar[0], eff.scrollY + eff.scrollPosBar[1]);
            DrawSrc(g, bmp, eff.scrollSrcDownArrow, eff.scrollX + eff.scrollPosDownArrow[0], eff.scrollY + eff.scrollPosDownArrow[1]);

            int movement = Math.Max(0, eff.ScrollBarMovement());
            int thumbY = eff.scrollY + eff.scrollPosBar[1] + (int)(0.3 * movement);
            DrawSrc(g, bmp, eff.scrollSrcThumb, eff.scrollX + eff.scrollPosBar[0], thumbY);
        }
        else DrawPlaceholder(g, outer, "ScrollBar");
        Reg(outer, "スクロールバー",
            (nx, ny) => _doc.SetLayoutRaw("ScrollBar", "Rect", $"{nx},{ny},{eff.scrollW},{eff.scrollH}"));
    }

    private void DrawProgressBar(Graphics g, SkinLayout eff)
    {
        var bmp = _bitmaps.Get(_doc, BitmapRole.ProgressBar);
        var outer = new Rectangle(eff.progX, eff.progY, eff.progW, eff.progH);
        if (bmp != null && eff.progW > 0 && eff.progH > 0)
        {
            int len = Math.Clamp((int)(eff.progW * StateProgress), 0, eff.progW);
            // 素材は上段=未再生 / 下段=再生済み（skin.cpp のコメント）。
            g.DrawImage(bmp, new Rectangle(eff.progX, eff.progY, len, eff.progH),
                new Rectangle(0, eff.progH, len, eff.progH), GraphicsUnit.Pixel);
            g.DrawImage(bmp, new Rectangle(eff.progX + len, eff.progY, eff.progW - len, eff.progH),
                new Rectangle(len, 0, eff.progW - len, eff.progH), GraphicsUnit.Pixel);
        }
        else DrawPlaceholder(g, outer, "ProgressBar");
        Reg(outer, "プログレスバー",
            (nx, ny) => _doc.SetLayoutRaw("ProgressBar", "Rect", $"{nx},{ny},{eff.progW},{eff.progH}"));
    }

    private void DrawVolumeBar(Graphics g, SkinLayout eff)
    {
        var bmp = _bitmaps.Get(_doc, BitmapRole.VolumeBar);
        var outer = new Rectangle(eff.volX, eff.volY, eff.volW, eff.volH);
        if (bmp != null)
        {
            DrawSrc(g, bmp, eff.volRect[1], eff.volX, eff.volY);
            int movement = Math.Max(0, eff.VolBarMovement());
            int knobX = eff.volX + (int)((StateVolume + 100) / 200.0 * movement);
            DrawSrc(g, bmp, eff.volRect[0], knobX, eff.volY);
        }
        else DrawPlaceholder(g, outer, "VolumeBar");
        Reg(outer, "音量バー",
            (nx, ny) => _doc.SetLayoutRaw("VolumeBar", "Rect", $"{nx},{ny},{eff.volW},{eff.volH}"));
    }

    private void DrawPlayKey(Graphics g, SkinLayout eff)
    {
        var bmp = _bitmaps.Get(_doc, BitmapRole.PlayKey);
        if (bmp == null) return;
        for (int i = 0; i < eff.numPlayKeys && i < 9; i++)
        {
            int x = eff.playKeyX + eff.playKeyPos[i][0];
            int y = eff.playKeyY + eff.playKeyPos[i][1];
            DrawSrc(g, bmp, eff.playKeyRect[i], x, y);
        }
        Reg(new Rectangle(eff.playKeyX, eff.playKeyY, 40, 24), "操作ボタン",
            (nx, ny) => _doc.SetLayoutRaw("PlayKey", "Pos", $"{nx},{ny}"));
    }

    private void DrawOverflowWarnings(Graphics g, SkinLayout eff, int sw, int sh)
    {
        using var pen = new Pen(Color.Red, Math.Max(1f, 1f / _scale));
        void Check(int x, int y, int w, int h)
        {
            if (x < 0 || y < 0 || x + w > sw || y + h > sh) g.DrawRectangle(pen, x, y, w, h);
        }
        Check(eff.bannerX, eff.bannerY, eff.bannerW, eff.bannerH);
        Check(eff.titleX, eff.titleY, eff.titleW, eff.titleH);
        Check(eff.fileListX, eff.fileListY, eff.fileListW, eff.fileListH);
        Check(eff.scrollX, eff.scrollY, eff.scrollW, eff.scrollH);
        Check(eff.progX, eff.progY, eff.progW, eff.progH);
        Check(eff.volX, eff.volY, eff.volW, eff.volH);
    }

    private static void DrawSrc(Graphics g, Bitmap bmp, Xywh src, int destX, int destY)
    {
        var srcRect = new Rectangle(
            Math.Clamp(src.X, 0, bmp.Width), Math.Clamp(src.Y, 0, bmp.Height),
            Math.Clamp(src.W, 0, Math.Max(0, bmp.Width - src.X)), Math.Clamp(src.H, 0, Math.Max(0, bmp.Height - src.Y)));
        if (srcRect.Width <= 0 || srcRect.Height <= 0) return;
        g.DrawImage(bmp, new Rectangle(destX, destY, srcRect.Width, srcRect.Height), srcRect, GraphicsUnit.Pixel);
    }

    private static void DrawPlaceholder(Graphics g, Rectangle rect, string label)
    {
        using var pen = new Pen(Color.Gray) { DashStyle = DashStyle.Dash };
        g.DrawRectangle(pen, rect);
    }

    // ---- 選択・ドラッグ ----------------------------------------------------
    private void Reg(Rectangle skinRect, string sectionTitle, Action<int, int> move)
    {
        _regions.Add(new DragRegion
        {
            ScreenRect = ToScreen(skinRect),
            SectionTitle = sectionTitle,
            Move = (nx, ny) => move(nx, ny),
        });
        // ドラッグの原点計算用に、この呼び出し時点の skin 座標も控える。
        _lastOrigin[sectionTitle] = new PointF(skinRect.X, skinRect.Y);
    }

    private readonly Dictionary<string, PointF> _lastOrigin = new();

    private RectangleF ToScreen(Rectangle r) =>
        new(_origin.X + r.X * _scale, _origin.Y + r.Y * _scale, r.Width * _scale, r.Height * _scale);

    private PointF ToSkin(Point p) => new((p.X - _origin.X) / _scale, (p.Y - _origin.Y) / _scale);

    private void OnMouseDown(object? sender, MouseEventArgs e)
    {
        for (int i = _regions.Count - 1; i >= 0; i--)
        {
            if (!_regions[i].ScreenRect.Contains(e.Location)) continue;
            _dragging = _regions[i];
            _dragStartSkin = ToSkin(e.Location);
            _dragOriginSkin = _lastOrigin.TryGetValue(_dragging.SectionTitle, out var o) ? o : PointF.Empty;
            PartActivated?.Invoke(_dragging.SectionTitle);
            return;
        }
    }

    private void OnMouseMove(object? sender, MouseEventArgs e)
    {
        if (_dragging?.Move == null) return;
        var cur = ToSkin(e.Location);
        int nx = (int)Math.Round(_dragOriginSkin.X + (cur.X - _dragStartSkin.X));
        int ny = (int)Math.Round(_dragOriginSkin.Y + (cur.Y - _dragStartSkin.Y));
        _dragging.Move(nx, ny);
    }
}
