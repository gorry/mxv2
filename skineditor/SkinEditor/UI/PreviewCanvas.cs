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

    // クリックした点（skin 座標）。どのアイテムを選ぶかは SkinEditForm が決める
    // （優先順位 H はコントロール側に付いていて、ここからは見えないため）。
    public event Action<Point>? PreviewClicked;

    // 選択中のアイテム。枠を出す矩形と、ドラッグでの書き戻し先。
    private string? _selectedRegion;
    private Action<int, int>? _selectedMove;

    private bool _pressed;
    private bool _canDrag;
    private bool _dragging;
    private Point _pressPoint;
    private PointF _dragStartSkin;
    private PointF _dragOriginSkin;
    private Dictionary<string, Rectangle> _regions = new();

    // ダミー再生状態（操作ボタンの各サブタブのトグルボタン、状態バーの
    // 音量・進捗スライダから書き換えられる）。
    public bool GetButtonPressed(int buttonIndex) =>
        buttonIndex >= 0 && buttonIndex < _state.ButtonPressed.Length && _state.ButtonPressed[buttonIndex];

    public void SetButtonPressed(int buttonIndex, bool pressed)
    {
        if (buttonIndex < 0 || buttonIndex >= _state.ButtonPressed.Length) return;
        _state.ButtonPressed[buttonIndex] = pressed;
        _dirty = true;
    }

    public bool StateLedPlay
    {
        get => _state.LedPlay;
        set { _state.LedPlay = value; _dirty = true; }
    }
    public bool StateLedPause
    {
        get => _state.LedPause;
        set { _state.LedPause = value; _dirty = true; }
    }
    public bool StateLedCont
    {
        get => _state.LedCont;
        set { _state.LedCont = value; _dirty = true; }
    }
    public bool StateLedRepeat
    {
        get => _state.LedRepeat;
        set { _state.LedRepeat = value; _dirty = true; }
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
    public int StateScroll
    {
        get => _state.Scroll;
        set { _state.Scroll = value; _dirty = true; }
    }
    public bool StateScrollUpPressed
    {
        get => _state.ScrollUpPressed;
        set { _state.ScrollUpPressed = value; _dirty = true; }
    }
    public bool StateScrollDownPressed
    {
        get => _state.ScrollDownPressed;
        set { _state.ScrollDownPressed = value; _dirty = true; }
    }
    public int StateLevel
    {
        get => _state.Level;
        set { _state.Level = value; _dirty = true; }
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
        MouseUp += OnMouseUp;
        Resize += (_, _) => Invalidate();
    }

    // 選択中のアイテムを差し替える。move が null ならドラッグでは動かせない。
    public void SetSelection(string? regionId, Action<int, int>? move)
    {
        _selectedRegion = string.IsNullOrEmpty(regionId) ? null : regionId;
        _selectedMove = move;
        Invalidate();
    }

    // 今の実効値での全アイテムの矩形（skin 座標）。当たり判定に使う。
    public Dictionary<string, Rectangle> BuildRegions() =>
        PreviewRegions.Build(_doc.Effective, FindAssetSize, _state.Volume);

    private Size? FindAssetSize(string fileName)
    {
        var a = _renderer.FindAsset(fileName);
        return a == null ? null : new Size(a.Width, a.Height);
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

        _regions = BuildRegions();

        var save = g.Save();
        g.TranslateTransform(_origin.X, _origin.Y);
        g.ScaleTransform(_scale, _scale);
        DrawOverflowWarnings(g, eff, sw, sh);
        g.Restore(save);

        DrawSelectionFrame(g);
    }

    // 選択中のアイテムの枠。アイテムのすぐ外側に白 1 画素、その外に黒 1 画素
    // （skineditor_hitcheck.md）。1 画素は **スキンの 1 画素**なので拡大率に
    // 比例させるが、縮小表示で消えないよう画面 1 画素は必ず確保する。
    private void DrawSelectionFrame(Graphics g)
    {
        if (_selectedRegion == null) return;
        if (!_regions.TryGetValue(_selectedRegion, out var item)) return;

        var r = ToScreen(item);
        float t = Math.Max(1f, _scale);
        using (var white = new Pen(Color.White, t))
        {
            g.DrawRectangle(white, r.X - t / 2f, r.Y - t / 2f, r.Width + t, r.Height + t);
        }
        using (var black = new Pen(Color.Black, t))
        {
            g.DrawRectangle(black, r.X - t * 1.5f, r.Y - t * 1.5f, r.Width + t * 3f, r.Height + t * 3f);
        }
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
    private RectangleF ToScreen(Rectangle r) =>
        new(_origin.X + r.X * _scale, _origin.Y + r.Y * _scale, r.Width * _scale, r.Height * _scale);

    private PointF ToSkin(Point p) => new((p.X - _origin.X) / _scale, (p.Y - _origin.Y) / _scale);

    // 押した時点では、まだクリック（＝選択の切り替え）かドラッグ（＝移動）か
    // 決まらない。**選択中のアイテムの上で押して、動かしたときだけ移動**にする
    // （動かさずに離せばクリック扱いで、同じ点の次の優先順位へ選択が移る）。
    private void OnMouseDown(object? sender, MouseEventArgs e)
    {
        if (e.Button != MouseButtons.Left) return;
        _pressed = true;
        _dragging = false;
        _pressPoint = e.Location;
        _canDrag = false;

        if (_selectedRegion == null || _selectedMove == null) return;
        if (!_regions.TryGetValue(_selectedRegion, out var item)) return;
        if (!ToScreen(item).Contains(e.Location)) return;
        _canDrag = true;
        _dragStartSkin = ToSkin(e.Location);
        _dragOriginSkin = new PointF(item.X, item.Y);
    }

    private void OnMouseMove(object? sender, MouseEventArgs e)
    {
        if (!_pressed || !_canDrag) return;
        if (!_dragging)
        {
            // 手の震えで移動が始まらないよう、少し動かしてからドラッグにする。
            if (Math.Abs(e.X - _pressPoint.X) < 3 && Math.Abs(e.Y - _pressPoint.Y) < 3) return;
            _dragging = true;
        }
        var cur = ToSkin(e.Location);
        int nx = (int)Math.Round(_dragOriginSkin.X + (cur.X - _dragStartSkin.X));
        int ny = (int)Math.Round(_dragOriginSkin.Y + (cur.Y - _dragStartSkin.Y));
        _selectedMove?.Invoke(nx, ny);
    }

    private void OnMouseUp(object? sender, MouseEventArgs e)
    {
        bool wasDrag = _dragging;
        _pressed = false;
        _canDrag = false;
        _dragging = false;
        if (e.Button != MouseButtons.Left || wasDrag) return;
        var p = ToSkin(e.Location);
        PreviewClicked?.Invoke(new Point((int)Math.Floor(p.X), (int)Math.Floor(p.Y)));
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
