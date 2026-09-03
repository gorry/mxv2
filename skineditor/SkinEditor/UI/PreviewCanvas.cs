// mxv2 スキンエディタ - 本体の描画をそのまま使ったプレビュー。
//
// 2026-09-03 に、GDI+ で「それらしく」描いていた近似表示をやめ、
// mxv2 本体 src/drawscreen.cpp の移植 (Model/Render/) が描いた
// 24bpp のキャンバスを貼るだけの作りにした。パレット番号での発色
// （鍵盤・操作ボタンの LED・レベルメータ）も、背景への乗算合成
// （配色の Bright 系）も本体と同じ結果になる。
//
// 文字（ファイラーと曲名）だけは本体と同じく別レイヤー扱いで、
// キャンバスを拡大したうえに出力解像度で描く（PreviewTextLayer）。
// ラスタライザが違うので画素は一致しないが、大きさは合わせてある。
//
// クリックで対応するセクションを選ばせ、主要な矩形はドラッグで移動できる。
// **当たり判定は描画とは切り離し**、layout の値から作った矩形で持つ。

using System.Drawing;
using System.Drawing.Drawing2D;
using SkinEditor.Model;

namespace SkinEditor.UI;

public sealed class PreviewCanvas : Panel
{
    private readonly SkinDocument _doc;
    private readonly PreviewRenderer _renderer;
    private readonly PreviewTextLayer _textLayer = new();
    private readonly PreviewState _state = new();
    private bool _dirty = true;
    private Bitmap? _canvas;

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
    private readonly Dictionary<string, PointF> _lastOrigin = new();

    // ダミー再生状態（PreviewStateBar から書き換えられる）
    public bool StatePlay
    {
        get => _state.Play;
        set { _state.Play = value; _dirty = true; }
    }
    public bool StateCont
    {
        get => _state.Cont;
        set { _state.Cont = value; _dirty = true; }
    }
    public bool StatePause
    {
        get => _state.Pause;
        set { _state.Pause = value; _dirty = true; }
    }
    public bool StateRepeat
    {
        get => _state.Repeat;
        set { _state.Repeat = value; _dirty = true; }
    }
    public int StateVolume
    {
        get => _state.Volume;
        set { _state.Volume = value; _dirty = true; }
    }
    public double StateProgress
    {
        get => _state.Progress;
        set { _state.Progress = value; _dirty = true; }
    }

    public PreviewCanvas(SkinDocument doc)
    {
        _doc = doc;
        _renderer = new PreviewRenderer(doc);
        DoubleBuffered = true;
        BackColor = Color.FromArgb(64, 64, 64);
        doc.Changed += () => { _dirty = true; Invalidate(); };
        MouseDown += OnMouseDown;
        MouseMove += OnMouseMove;
        MouseUp += (_, _) => _dragging = null;
        Resize += (_, _) => Invalidate();
    }

    // 素材ファイルが差し替わったとき（BitmapRoleRow のインポート）。
    public void InvalidateBitmaps()
    {
        _renderer.InvalidateAssets();
        _dirty = true;
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

        if (_dirty || _canvas == null)
        {
            _canvas = _renderer.Render(_state);
            _textLayer.SetFontFile(_renderer.FindFontFile());
            _dirty = false;
        }

        // キャンバスは最近傍で拡大（画素の位置を確かめる用途なので、
        // 本体の既定 sharp-bilinear ではなく最近傍のままにしてある）。
        if (_canvas != null)
        {
            g.DrawImage(_canvas, new RectangleF(_origin.X, _origin.Y, sw * _scale, sh * _scale),
                new RectangleF(-0.5f, -0.5f, _canvas.Width, _canvas.Height), GraphicsUnit.Pixel);
        }

        // 文字は拡大後の解像度で重ねる（本体 TextLayer と同じ 2 段構え）。
        _textLayer.Render(g, _renderer.TextDraws, _origin.X, _origin.Y, _scale);

        var save = g.Save();
        g.TranslateTransform(_origin.X, _origin.Y);
        g.ScaleTransform(_scale, _scale);
        BuildRegions(eff);
        DrawOverflowWarnings(g, eff, sw, sh);
        g.Restore(save);
    }

    // ---- 当たり判定の矩形（描画とは独立。layout の値だけで決まる） ----------
    private void BuildRegions(SkinLayout eff)
    {
        var kb0 = _renderer.FindAsset(eff.kb0Bitmap);
        int kbW = kb0?.Width ?? 400;
        int kbH = kb0?.Height ?? 36;
        Reg(new Rectangle(eff.kbX, eff.kbY, kbW, kbH), "鍵盤",
            (nx, ny) => _doc.SetLayoutRaw("Keyboard", "Pos", $"{nx},{ny}"));

        Reg(new Rectangle(eff.statusX, eff.statusY, eff.statusBackW, eff.statusBackH), "ステータス",
            (nx, ny) => _doc.SetLayoutRaw("Status", "Pos", $"{nx},{ny}"));

        Reg(new Rectangle(eff.bannerX, eff.bannerY, eff.bannerW, eff.bannerH), "バナー",
            (nx, ny) => _doc.SetLayoutRaw("Banner", "Rect", $"{nx},{ny},{eff.bannerW},{eff.bannerH}"));

        Reg(new Rectangle(eff.titleX, eff.titleY, eff.titleW, eff.titleH), "曲名",
            (nx, ny) => _doc.SetLayoutRaw("Title", "Rect", $"{nx},{ny},{eff.titleW},{eff.titleH}"));

        Reg(new Rectangle(eff.fileListX, eff.fileListY, eff.fileListW, eff.fileListH), "ファイラー",
            (nx, ny) => _doc.SetLayoutRaw("FileList", "Rect", $"{nx},{ny},{eff.fileListW},{eff.fileListH}"));

        Reg(new Rectangle(eff.scrollX, eff.scrollY, eff.scrollW, eff.scrollH), "スクロールバー",
            (nx, ny) => _doc.SetLayoutRaw("ScrollBar", "Rect", $"{nx},{ny},{eff.scrollW},{eff.scrollH}"));

        Reg(new Rectangle(eff.progX, eff.progY, eff.progW, eff.progH), "プログレスバー",
            (nx, ny) => _doc.SetLayoutRaw("ProgressBar", "Rect", $"{nx},{ny},{eff.progW},{eff.progH}"));

        Reg(new Rectangle(eff.volX, eff.volY, eff.volW, eff.volH), "音量バー",
            (nx, ny) => _doc.SetLayoutRaw("VolumeBar", "Rect", $"{nx},{ny},{eff.volW},{eff.volH}"));

        // 操作ボタンは使うボタンぶんの外接矩形（掴める範囲を実物に合わせる）。
        int pkW = 0, pkH = 0;
        for (int i = 0; i < eff.numPlayKeys && i < 9; i++)
        {
            pkW = Math.Max(pkW, eff.playKeyPos[i][0] + eff.playKeyRect[i].W);
            pkH = Math.Max(pkH, eff.playKeyPos[i][1] + eff.playKeyRect[i].H);
        }
        if (pkW <= 0 || pkH <= 0) { pkW = 40; pkH = 24; }
        Reg(new Rectangle(eff.playKeyX, eff.playKeyY, pkW, pkH), "操作ボタン",
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

    // ---- 選択・ドラッグ ----------------------------------------------------
    private void Reg(Rectangle skinRect, string sectionTitle, Action<int, int> move)
    {
        _regions.Add(new DragRegion
        {
            ScreenRect = ToScreen(skinRect),
            SectionTitle = sectionTitle,
            Move = (nx, ny) => move(nx, ny),
        });
        // ドラッグの原点計算用に、この時点の skin 座標も控える。
        _lastOrigin[sectionTitle] = new PointF(skinRect.X, skinRect.Y);
    }

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

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _renderer.Dispose();
            _textLayer.Dispose();
        }
        base.Dispose(disposing);
    }
}
